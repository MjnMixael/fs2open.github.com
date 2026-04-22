#include "globalincs/pstypes.h"
#include "graphics/2d.h"
#include "io/key.h"
#include "io/mouse.h"
#include "lab/renderer/lab_cameras.h"
#include "lab/labv2_internal.h"


LabCamera::~LabCamera() {
	cam_delete(FS_camera);
}

namespace {
bool point_in_rect(int x, int y, int rect_x, int rect_y, int rect_w, int rect_h)
{
	return x >= rect_x && x < rect_x + rect_w && y >= rect_y && y < rect_y + rect_h;
}
}

void OrbitCamera::handleInput(
	int dx, int dy, int dz, bool, bool lmbPressed, bool rmbDown, int modifierKeys, int mouseX, int mouseY) {
	if (lmbPressed && handleOrientationWidgetClick(mouseX, mouseY)) {
		return;
	}

	if (dx == 0 && dy == 0 && dz == 0)
		return;

	if (dz > 0) {
		for (int i = 0; i < dz; ++i) {
			distance *= 0.9f;
		}
	} else if (dz < 0) {
		for (int i = 0; i < -dz; ++i) {
			distance *= 1.1f;
		}
	}
	CLAMP(distance, 1.0f, 10000000.0f);

	if (rmbDown) {
		if (modifierKeys & KEY_SHIFTED) {
			const float pan_factor = distance / 500.0f;

			vec3d camera_offset;
			camera_offset.xyz.x = sinf(phi) * cosf(theta);
			camera_offset.xyz.y = cosf(phi);
			camera_offset.xyz.z = sinf(phi) * sinf(theta);

			vec3d view_forward;
			vm_vec_copy_scale(&view_forward, &camera_offset, -1.0f);

			vec3d world_up = vmd_y_vector;
			vec3d view_right;
			vm_vec_cross(&view_right, &world_up, &view_forward);

			if (vm_vec_mag_squared(&view_right) <= 1e-6f) {
				world_up = vmd_x_vector;
				vm_vec_cross(&view_right, &world_up, &view_forward);
			}

			vm_vec_normalize_safe(&view_right);

			vec3d view_up;
			vm_vec_cross(&view_up, &view_forward, &view_right);
			vm_vec_normalize_safe(&view_up);

			vm_vec_scale_add2(&pan_offset, &view_right, -dx * pan_factor);
			vm_vec_scale_add2(&pan_offset, &view_up, dy * pan_factor);
		} else {
			theta -= dx / 100.0f;
			phi -= dy / 100.0f;

			CLAMP(phi, 0.01f, PI - 0.01f);
		}
	}

	updateCamera();
}

bool OrbitCamera::handleOrientationWidgetClick(int mouseX, int mouseY)
{
	const int widget_size = (WIDGET_BUTTON_SIZE * 3) + (WIDGET_GAP * 2);
	const int widget_left = gr_screen.center_offset_x + gr_screen.center_w - widget_size - WIDGET_MARGIN;
	const int widget_top = gr_screen.center_offset_y + WIDGET_MARGIN;
	const int center_x = widget_left + widget_size / 2;
	const int center_y = widget_top + widget_size / 2;
	const int half_button = WIDGET_BUTTON_SIZE / 2;
	const int button_offset = WIDGET_BUTTON_SIZE + WIDGET_GAP;

	const auto button_rect = [&](SnapDirection direction, int& x, int& y) {
		x = center_x - half_button;
		y = center_y - half_button;

		switch (direction) {
		case SnapDirection::Top:
			y -= button_offset;
			break;
		case SnapDirection::Bottom:
			y += button_offset;
			break;
		case SnapDirection::Left:
			x -= button_offset;
			break;
		case SnapDirection::Right:
			x += button_offset;
			break;
		}
	};

	for (auto direction : {SnapDirection::Top, SnapDirection::Bottom, SnapDirection::Left, SnapDirection::Right}) {
		int button_x = 0;
		int button_y = 0;
		button_rect(direction, button_x, button_y);

		if (point_in_rect(mouseX, mouseY, button_x, button_y, WIDGET_BUTTON_SIZE, WIDGET_BUTTON_SIZE)) {
			snapToDirection(direction);
			return true;
		}
	}

	return false;
}

bool OrbitCamera::isOverlayHit(int mouseX, int mouseY) const
{
	const int widget_size = (WIDGET_BUTTON_SIZE * 3) + (WIDGET_GAP * 2);
	const int widget_left = gr_screen.center_offset_x + gr_screen.center_w - widget_size - WIDGET_MARGIN;
	const int widget_top = gr_screen.center_offset_y + WIDGET_MARGIN;
	const int center_x = widget_left + widget_size / 2;
	const int center_y = widget_top + widget_size / 2;
	const int half_button = WIDGET_BUTTON_SIZE / 2;
	const int button_offset = WIDGET_BUTTON_SIZE + WIDGET_GAP;

	for (auto direction : {SnapDirection::Top, SnapDirection::Bottom, SnapDirection::Left, SnapDirection::Right}) {
		int button_x = center_x - half_button;
		int button_y = center_y - half_button;

		switch (direction) {
		case SnapDirection::Top:
			button_y -= button_offset;
			break;
		case SnapDirection::Bottom:
			button_y += button_offset;
			break;
		case SnapDirection::Left:
			button_x -= button_offset;
			break;
		case SnapDirection::Right:
			button_x += button_offset;
			break;
		}

		if (point_in_rect(mouseX, mouseY, button_x, button_y, WIDGET_BUTTON_SIZE, WIDGET_BUTTON_SIZE)) {
			return true;
		}
	}

	return false;
}

void OrbitCamera::snapToDirection(SnapDirection direction)
{
	static constexpr float POLE_EPSILON = 0.01f;

	switch (direction) {
	case SnapDirection::Top:
		phi = POLE_EPSILON;
		break;
	case SnapDirection::Bottom:
		phi = PI - POLE_EPSILON;
		break;
	case SnapDirection::Left:
		phi = PI_2;
		theta = PI;
		break;
	case SnapDirection::Right:
		phi = PI_2;
		theta = 0.0f;
		break;
	}

	updateCamera();
}

void OrbitCamera::resetView()
{
	phi = DEFAULT_PHI;
	theta = DEFAULT_THETA;
	distance = DEFAULT_DISTANCE;
	pan_offset = vmd_zero_vector;

	displayedObjectChanged();
}

void OrbitCamera::displayedObjectChanged() {
	float distance_multiplier = 1.6f;

	if (getLabManager()->CurrentObject != -1) {
		object* obj = &Objects[getLabManager()->CurrentObject];

		// Reset camera panning
		pan_offset = vmd_zero_vector;
		
		// Ships and Missiles use the object radius to get a camera distance
		distance = obj->radius * distance_multiplier;

		// Beams use the muzzle radius
		if (obj->type == OBJ_BEAM) {
			weapon_info* wip = &Weapon_info[Beams[obj->instance].weapon_info_index];
			if (wip != nullptr) {
				distance = wip->b_info.beam_muzzle_radius * distance_multiplier;
			}
		// Lasers use the laser length
		} else if (obj->type == OBJ_WEAPON) {
			weapon_info* wip = &Weapon_info[Weapons[obj->instance].weapon_info_index];
			if (wip != nullptr && wip->render_type == WRT_LASER) {
				distance = wip->laser_length * distance_multiplier;
			}
		}
	}

	updateCamera();
}

void OrbitCamera::updateCamera() {
	auto cam = FS_camera.getCamera();
	vec3d new_position;
	new_position.xyz.x = sinf(phi) * cosf(theta);
	new_position.xyz.y = cosf(phi);
	new_position.xyz.z = sinf(phi) * sinf(theta);

	vm_vec_scale(&new_position, distance);

	object* obj = &Objects[getLabManager()->CurrentObject];
	vec3d target = obj->pos;

	if (obj->type == OBJ_WEAPON) {
		weapon_info* wip = &Weapon_info[Weapons[obj->instance].weapon_info_index];
		if (wip != nullptr && wip->render_type == WRT_LASER) {
			// Offset target by half the laser length forward along the facing
			vec3d forward;
			vm_vec_copy_normalize(&forward, &obj->orient.vec.fvec);
			vm_vec_scale_add2(&target, &forward, wip->laser_length * 0.5f);
		}
	}

	vm_vec_add2(&target, &pan_offset);
	vm_vec_add2(&new_position, &target);

	cam->set_position(&new_position);

	// If these are the same then that's not great so do nothing and use the last facing value
	if (!vm_vec_same(&new_position, &target)) {
		cam->set_rotation_facing(&target);
	}
}

void OrbitCamera::renderOverlay() const
{
	const int widget_size = (WIDGET_BUTTON_SIZE * 3) + (WIDGET_GAP * 2);
	const int widget_left = gr_screen.center_offset_x + gr_screen.center_w - widget_size - WIDGET_MARGIN;
	const int widget_top = gr_screen.center_offset_y + WIDGET_MARGIN;
	const int center_x = widget_left + widget_size / 2;
	const int center_y = widget_top + widget_size / 2;
	const int half_button = WIDGET_BUTTON_SIZE / 2;
	const int button_offset = WIDGET_BUTTON_SIZE + WIDGET_GAP;

	color background;
	gr_init_alphacolor(&background, 24, 24, 24, 96);
	gr_set_color_fast(&background);
	gr_rect(widget_left, widget_top, widget_size, widget_size, GR_RESIZE_NONE);

	int mouse_x = 0;
	int mouse_y = 0;
	mouse_get_pos(&mouse_x, &mouse_y);

	const auto draw_button = [&](SnapDirection direction, const char* label) {
		int button_x = center_x - half_button;
		int button_y = center_y - half_button;

		switch (direction) {
		case SnapDirection::Top:
			button_y -= button_offset;
			break;
		case SnapDirection::Bottom:
			button_y += button_offset;
			break;
		case SnapDirection::Left:
			button_x -= button_offset;
			break;
		case SnapDirection::Right:
			button_x += button_offset;
			break;
		}

		const bool hovered = point_in_rect(mouse_x, mouse_y, button_x, button_y, WIDGET_BUTTON_SIZE, WIDGET_BUTTON_SIZE);

		color button_color;
		gr_init_alphacolor(&button_color, 220, 220, 220, hovered ? 200 : 140);
		gr_set_color_fast(&button_color);
		gr_rect(button_x, button_y, WIDGET_BUTTON_SIZE, WIDGET_BUTTON_SIZE, GR_RESIZE_NONE);

		int text_w = 0;
		int text_h = 0;
		gr_get_string_size(&text_w, &text_h, label);
		gr_set_color_fast(&Color_black);
		gr_string(button_x + (WIDGET_BUTTON_SIZE - text_w) / 2, button_y + (WIDGET_BUTTON_SIZE - text_h) / 2, label, GR_RESIZE_NONE);
	};

	draw_button(SnapDirection::Top, "T");
	draw_button(SnapDirection::Bottom, "B");
	draw_button(SnapDirection::Left, "L");
	draw_button(SnapDirection::Right, "R");
}
