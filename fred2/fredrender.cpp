/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 *
 */



#include "stdafx.h"
#include "FRED.h"
#include "FREDDoc.h"
#include "FREDView.h"
#include "MainFrm.h"

#include "FredRender.h"
#include "Management.h"
#include "wing.h"

#include "asteroid/asteroid.h"
#include "bmpman/bmpman.h"
#include "cfile/cfile.h"
#include "cmdline/cmdline.h"
#include "globalincs/linklist.h"
#include "graphics/2d.h"
#include "graphics/matrix.h"
#include "graphics/light.h"
#include "graphics/font.h"
#include "graphics/tmapper.h"
#include "iff_defs/iff_defs.h"
#include "io/key.h"
#include "io/spacemouse.h"
#include "io/timer.h"
#include "jumpnode/jumpnode.h"
#include "lighting/lighting.h"
#include "math/floating.h"
#include "math/fvi.h"
#include "math/vecmat.h"
#include "mission/missiongrid.h"
#include "mission/missionparse.h"
#include "model/model.h"
#include "model/model.h"
#include "mod_table/mod_table.h"
#include "object/object.h"
#include "physics/physics.h"
#include "render/3d.h"
#include "render/3dinternal.h"
#include "ship/ship.h"
#include "sound/audiostr.h"
#include "starfield/starfield.h"
#include "weapon/weapon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <windows.h>

extern float flFrametime;
extern subsys_to_render Render_subsys;

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define MAX_FRAMETIME   (F1_0/4)    // Frametime gets saturated at this.
#define MIN_FRAMETIME   (F1_0/120)
#define LOLLIPOP_SIZE   2.5f
#define CONVERT_DEGREES 57.29578f   // conversion factor from radians to degrees

#define FRED_COLOUR_WHITE			0xffffff
#define FRED_COLOUR_YELLOW_GREEN	0xc8ff00

const float FRED_DEFAULT_HTL_FOV = 0.485f;
const float FRED_BRIEFING_HTL_FOV = 0.325f;
const float FRED_DEAFULT_HTL_DRAW_DIST = 300000.0f;

int Aa_gridlines = 0;
int Control_mode = 0;
int Editing_mode = 1;
int Fixed_briefing_size = 1;
int Flying_controls_mode = 1;
int Fred_outline = 0;
int Group_rotate = 1;
int info_popup_active = 0;
int inited = -1;
int Last_cursor_over = -1;
int last_x = 0;
int last_y = 0;
int Lookat_mode = 0;
int rendering_order[MAX_SHIPS];
int render_count = 0;
int Show_asteroid_field = 1;
int Show_coordinates = 0;
int Show_distances = 0;
int Show_grid = 1;
int Show_grid_positions = 1;
int Show_horizon = 0;
int Show_outlines = 0;
bool Draw_outlines_on_selected_ships = true;
bool Draw_outline_at_warpin_position = false;
bool Always_save_display_names = false;
bool Error_checker_checks_potential_issues = true;
bool Error_checker_checks_potential_issues_once = false;
int Show_stars = 1;
int Single_axis_constraint = 0;
int True_rw, True_rh;
int Universal_heading = 0;

fix lasttime = 0;
vec3d my_pos = { 0.0f, 0.0f, -5.0f };
vec3d view_pos, eye_pos, Viewer_pos, Last_eye_pos = { 0.0f };
vec3d Last_control_pos = { 0.0f };

vec3d Constraint = { 1.0f, 0.0f, 1.0f };
vec3d Anticonstraint = { 0.0f, 1.0f, 0.0f };

vec3d Grid_center;
matrix Grid_gmatrix;

matrix my_orient = IDENTITY_MATRIX;
matrix trackball_orient = IDENTITY_MATRIX;
matrix view_orient = IDENTITY_MATRIX, eye_orient, Last_eye_orient = IDENTITY_MATRIX;
matrix Last_control_orient = IDENTITY_MATRIX;

physics_info view_physics;
control_info view_controls;

CWnd info_popup;

color colour_black;
color colour_green;
color colour_white;
color colour_yellow_green;

static vec3d Global_light_world = { 0.208758f, -0.688253f, -0.694782f };

int Fred_grid_colors_inited = 0;
color Fred_grid_bright, Fred_grid_dark, Fred_grid_bright_aa, Fred_grid_dark_aa;

/**
 * @brief Enables HTL
 */
void fred_enable_htl();

/**
 * @brief Disables HTL
 */
void fred_disable_htl();

/**
 * @brief Makes the orientation "wings level" with the grid plane.
 *
 * @param[in,out] orient Orientation matrix to modify
 */
void level_object(matrix *orient);

/**
 * @brief Snaps the given vector to the closest unit vector
 *
 * @param[in,out] v Vector to modify
 */
void align_vector_to_axis(vec3d *v);

/**
 * @brief Snap the given orientation to the closest unit vector
 *
 * @param[in,out] orient Orientation matrix of the object to adjust
 */
void verticalize_object(matrix *orient);

/**
 * @brief Process the given key. If it turns out to be a system key then do something.
 *
 * @param[in] key Key to process
 */
void process_system_keys(int key);

/**
 * @brief Get the world position for a given subobject on a ship
 *
 * @param[in]  parent_obj The ship
 * @param[in]  subsys     The subsystem
 * @param[out] world_pos  The world position of the subsystem
 *
 * @TODO Need a better way of error handling here. Maybe return a bool instead.
 */
vec3d* get_subsystem_world_pos2(object* parent_obj, ship_subsys* subsys, vec3d* world_pos);

/**
 * @brief Gets the bounding rectangle (2D) for the given subsystem of a given ship
 *
 * @param[in] ship_obj The ship
 * @param[in] subsys   The subsystem
 * @param[out] x1      The first X coordinate
 * @param[out] x2      The second X coordinate
 * @param[out] y1      The first Y coordinate
 * @param[out] y2      The second Y coordinate
 *
 * @returns 1 if successful, or
 * @returns 0 if not
 */
int get_subsys_bounding_rect(object *ship_obj, ship_subsys *subsys, int *x1, int *x2, int *y1, int *y2);

/**
 * @brief Renders all models
 */
void render_models(void);

/**
 * @brief Renders the bounding box for the given subsystem with HTL
 *
 * @param[in] s2r Subsystem to render a box for
 */
void fredhtl_render_subsystem_bounding_box(subsys_to_render * s2r);

/**
 * @brief Draws the X from a elevation line on the grid
 *
 * @param[in] pos        Position of the object. The X's position is calculated from this.
 * @param[in] gridp      The grid we're referencing against
 * @param[in] col_scheme Color scheme?
 */
void render_model_x(const vec3d *pos, const grid *gridp, int col_scheme = 0);

/**
 * @brief Draws the X from a elevation line on the grid with HTL
 *
 * @param[in] pos        Position of the object. The X's position is calculated from this.
 * @param[in] gridp      The grid we're referencing against
 * @param[in] col_scheme Color scheme?
 */
void render_model_x_htl(vec3d *pos, grid *gridp, int col_scheme = 0);

/**
 * @brief Draws a model with HTL
 *
 * @param[in] objp Object to maybe draw
 */
void render_one_model_htl(object *objp);

/**
 * @brief Draws a single briefing icon, if the given object is an icon
 *
 * @param[in] objp Object to maybe draw
 */
void render_one_model_briefing_screen(object *objp);

/**
 * @brief Draws the boundries of the asteroid field
 */
void draw_asteroid_field();

/**
 * @brief Highlights the bitmaps used on the skybox
 *
 * @note Currently does nothing
 */
void hilight_bitmap();

/**
 * @brief Displays the active ship subsystem
 */
void display_active_ship_subsystem();

/**
 * @brief Displays the distances between each object in the mission.
 */
void display_distances();

/**
 * @brief Draws an arrow for the compass
 *
 * @param[in] Vector of the arrow we're drawing
 *
 * @details Actually draws a stick, but whatever.
 */
void draw_compass_arrow(vec3d *v0);

/**
 * @brief Draws a lollipop
 *
 * @param[in] obj Object to draw a lollipop for
 * @param[in] r The red color value
 * @param[in] g The green color value
 * @param[in] b The blue color value
 */
void draw_orient_sphere(object *obj, int r, int g, int b);

/**
 * @brief Draws a lollipop
 *
 * @param[in] col
 * @param[in] obj Object to draw a lollipop for
 * @param[in] r The red color value
 * @param[in] g The green color value
 * @param[in] b The blue color value
 */
void draw_orient_sphere2(int col, object *obj, int r, int g, int b);

/**
 * @brief Renders the given grid
 *
 * @param[in] gridp Grid to render
 */
void fred_render_grid(grid *gridp);

/**
 * @brief Renders the selection rectangle
 */
void render_active_rect(void);

/**
 * @brief Render the universal compass
 */
void render_compass(void);

/**
 * See if object obstructs the vector from point p0 to point p1.
 *
 * @param[in] objp Object to check
 * @param[in] p0 Origin of vector
 * @param[in] p1 Destination of vector
 * @param[out] hitpost If the object does obstuct the vector, this is the tosition where a collision occurs
 *
 * @returns TRUE if a collision occured, or
 * @returns FALSE if no collision
 */
int object_check_collision(object *objp, vec3d *p0, vec3d *p1, vec3d *hitpos);

/**
 * @brief Process the controls
 *
 * @param[in,out] pos       Position of the camera or controlled object to modify
 * @param[in,out] orient    Orientation of the camera or controlled object to modify
 * @param[in]     frametime Frametime of when this function was called
 * @param[in]     key       Key to (maybe) process
 * @param[in]     mode      Optional. Mode that we're currently in
 */
void process_controls(vec3d *pos, matrix *orient, float frametime, int key, int mode = 0);

/**
 * @brief Processes the given key. Does something if it is a movement key.
 *
 * @param[in] key Key to process
 * @param[in,out] mvec Movement vector to modify
 * @param[in,out] angs Angles to modify
 */
void process_movement_keys(int key, vec3d *mvec, angles *angs);

/**
 * @brief Increments the mission timer
 */
void inc_mission_time();

/**
 * @brief Disables rendering of the active ship subsystem
 */
void cancel_display_active_ship_subsystem();


void align_vector_to_axis(vec3d *v) {
	float x, y, z;

	x = v->xyz.x;
	if (x < 0)
		x = -x;

	y = v->xyz.y;
	if (y < 0)
		y = -y;

	z = v->xyz.z;
	if (z < 0)
		z = -z;

	if ((x > y) && (x > z)) {  // x axis
		if (v->xyz.x < 0)  // negative x
			vm_vec_make(v, -1.0f, 0.0f, 0.0f);
		else  // positive x
			vm_vec_make(v, 1.0f, 0.0f, 0.0f);

	} else if (y > z) {  // y axis
		if (v->xyz.y < 0)  // negative y
			vm_vec_make(v, 0.0f, -1.0f, 0.0f);
		else  // positive y
			vm_vec_make(v, 0.0f, 1.0f, 0.0f);

	} else {  // z axis
		if (v->xyz.z < 0)  // negative z
			vm_vec_make(v, 0.0f, 0.0f, -1.0f);
		else  // positive z
			vm_vec_make(v, 0.0f, 0.0f, 1.0f);
	}
}

void cancel_display_active_ship_subsystem() {
	Render_subsys.do_render = false;
	Render_subsys.ship_obj = NULL;
	Render_subsys.cur_subsys = NULL;
}

void display_active_ship_subsystem() {
	if (cur_object_index != -1) {
		if (Objects[cur_object_index].type == OBJ_SHIP) {

			object *objp = &Objects[cur_object_index];
			
			// if this option is checked, we want to render info for all subsystems, not just the ones we select with K and Shift-K
			if (Highlight_selectable_subsys) {
				auto shipp = &Ships[objp->instance];

				for (auto ss = GET_FIRST(&shipp->subsys_list); ss != END_OF_LIST(&shipp->subsys_list); ss = GET_NEXT(ss)) {
					if (ss->system_info->subobj_num != -1) {
						subsys_to_render s2r = { true, objp, ss };
						fredhtl_render_subsystem_bounding_box(&s2r);
					}
				}
			}
			// otherwise select individual subsystems, or not, as normal
			else {
				// switching to a new ship, so reset
				if (objp != Render_subsys.ship_obj) {
					cancel_display_active_ship_subsystem();
					return;
				}

				if (Render_subsys.do_render) {
					fredhtl_render_subsystem_bounding_box(&Render_subsys);
				} else {
					cancel_display_active_ship_subsystem();
				}
			}
		}
	}
}

void display_distances() {
	char buf[20];
	object *objp, *o2;
	vec3d pos;
	vertex v;


	gr_set_color(255, 0, 0);
	objp = GET_FIRST(&obj_used_list);
	while (objp != END_OF_LIST(&obj_used_list))
	{
		if (objp->flags[Object::Object_Flags::Marked])
		{
			o2 = GET_NEXT(objp);
			while (o2 != END_OF_LIST(&obj_used_list))
			{
				if (o2->flags[Object::Object_Flags::Marked])
				{
					rpd_line(&objp->pos, &o2->pos);
					vm_vec_avg(&pos, &objp->pos, &o2->pos);
					g3_rotate_vertex(&v, &pos);
					if (!(v.codes & CC_BEHIND))
						if (!(g3_project_vertex(&v) & PF_OVERFLOW))	{
							sprintf(buf, "%.1f", vm_vec_dist(&objp->pos, &o2->pos));
							gr_set_color_fast(&colour_white);
							gr_string((int) v.screen.xyw.x, (int) v.screen.xyw.y, buf);
						}
				}



				o2 = GET_NEXT(o2);
			}
		}

		objp = GET_NEXT(objp);
	}
}

void display_ship_info() {
	char buf[512], pos[80];
	int render = 1;
	object *objp;
	vertex	v;

	objp = GET_FIRST(&obj_used_list);
	while (objp != END_OF_LIST(&obj_used_list)) {
		Assert(objp->type != OBJ_NONE);
		Fred_outline = 0;
		render = 1;
		if (OBJ_INDEX(objp) == cur_object_index)
			Fred_outline = FRED_COLOUR_WHITE;
		else if (objp->flags[Object::Object_Flags::Marked])  // is it a marked object?
			Fred_outline = FRED_COLOUR_YELLOW_GREEN;
		else
			Fred_outline = 0;

		if ((objp->type == OBJ_WAYPOINT) && !Show_waypoints)
			render = 0;

		if ((objp->type == OBJ_START) && !Show_starts)
			render = 0;

		if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) {
			if (!Show_ships)
				render = 0;

			if (!Show_iff[Ships[objp->instance].team])
				render = 0;
		}

		if (objp->flags[Object::Object_Flags::Hidden])
			render = 0;

		g3_rotate_vertex(&v, &objp->pos);
		if (!(v.codes & CC_BEHIND) && render)
			if (!(g3_project_vertex(&v) & PF_OVERFLOW)) {
				*buf = 0;
				if (Show_ship_info) {
					if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) {
						ship *shipp;
						int ship_type;

						shipp = &Ships[objp->instance];
						ship_type = shipp->ship_info_index;
						ASSERT(ship_type >= 0);
						sprintf(buf, "%s\n%s", shipp->ship_name, Ship_info[ship_type].short_name);

					} else if (objp->type == OBJ_WAYPOINT) {
						int idx;
						waypoint_list *wp_list = find_waypoint_list_with_instance(objp->instance, &idx);
						Assert(wp_list != NULL);
						sprintf(buf, "%s\nWaypoint %d", wp_list->get_name(), idx + 1);

					} else if (objp->type == OBJ_POINT) {
						if (objp->instance == BRIEFING_LOOKAT_POINT_ID)
							strcpy_s(buf, "Camera lookat point");
						else
							strcpy_s(buf, "Briefing icon");

					} else if (objp->type == OBJ_JUMP_NODE) {
						CJumpNode* jnp = jumpnode_get_by_objnum(OBJ_INDEX(objp));
						sprintf(buf, "%s\n%s", jnp->GetName(), jnp->GetDisplayName());
					} else
						Assert(0);
				}

				if (Show_coordinates) {
					sprintf(pos, "(%.0f,%.0f,%.0f)", objp->pos.xyz.x, objp->pos.xyz.y, objp->pos.xyz.z);
					if (*buf)
						strcat_s(buf, "\n");

					strcat_s(buf, pos);
				}

				if (*buf) {
					if (Fred_outline == FRED_COLOUR_WHITE)
						gr_set_color_fast(&colour_green);
					else if (Fred_outline == FRED_COLOUR_YELLOW_GREEN)
						gr_set_color_fast(&colour_yellow_green);
					else
						gr_set_color_fast(&colour_white);

					gr_string((int) v.screen.xyw.x, (int) v.screen.xyw.y, buf);
				}
			}

		objp = GET_NEXT(objp);
	}
}

void draw_asteroid_field() {
	int i, j;
	vec3d p[8], ip[8];
	vertex v[8], iv[8];

	for (i = 0; i < 1 /*MAX_ASTEROID_FIELDS*/; i++)
		if (Asteroid_field.num_initial_asteroids) {
			p[0].xyz.x = p[2].xyz.x = p[4].xyz.x = p[6].xyz.x = Asteroid_field.min_bound.xyz.x;
			p[1].xyz.x = p[3].xyz.x = p[5].xyz.x = p[7].xyz.x = Asteroid_field.max_bound.xyz.x;
			p[0].xyz.y = p[1].xyz.y = p[4].xyz.y = p[5].xyz.y = Asteroid_field.min_bound.xyz.y;
			p[2].xyz.y = p[3].xyz.y = p[6].xyz.y = p[7].xyz.y = Asteroid_field.max_bound.xyz.y;
			p[0].xyz.z = p[1].xyz.z = p[2].xyz.z = p[3].xyz.z = Asteroid_field.min_bound.xyz.z;
			p[4].xyz.z = p[5].xyz.z = p[6].xyz.z = p[7].xyz.z = Asteroid_field.max_bound.xyz.z;

			for (j = 0; j < 8; j++)
				g3_rotate_vertex(&v[j], &p[j]);

			g3_draw_line(&v[0], &v[1]);
			g3_draw_line(&v[2], &v[3]);
			g3_draw_line(&v[4], &v[5]);
			g3_draw_line(&v[6], &v[7]);
			g3_draw_line(&v[0], &v[2]);
			g3_draw_line(&v[1], &v[3]);
			g3_draw_line(&v[4], &v[6]);
			g3_draw_line(&v[5], &v[7]);
			g3_draw_line(&v[0], &v[4]);
			g3_draw_line(&v[1], &v[5]);
			g3_draw_line(&v[2], &v[6]);
			g3_draw_line(&v[3], &v[7]);


			// maybe draw inner box
			if (Asteroid_field.has_inner_bound) {

				gr_set_color(16, 192, 92);

				ip[0].xyz.x = ip[2].xyz.x = ip[4].xyz.x = ip[6].xyz.x = Asteroid_field.inner_min_bound.xyz.x;
				ip[1].xyz.x = ip[3].xyz.x = ip[5].xyz.x = ip[7].xyz.x = Asteroid_field.inner_max_bound.xyz.x;
				ip[0].xyz.y = ip[1].xyz.y = ip[4].xyz.y = ip[5].xyz.y = Asteroid_field.inner_min_bound.xyz.y;
				ip[2].xyz.y = ip[3].xyz.y = ip[6].xyz.y = ip[7].xyz.y = Asteroid_field.inner_max_bound.xyz.y;
				ip[0].xyz.z = ip[1].xyz.z = ip[2].xyz.z = ip[3].xyz.z = Asteroid_field.inner_min_bound.xyz.z;
				ip[4].xyz.z = ip[5].xyz.z = ip[6].xyz.z = ip[7].xyz.z = Asteroid_field.inner_max_bound.xyz.z;

				for (j = 0; j < 8; j++)
					g3_rotate_vertex(&iv[j], &ip[j]);

				g3_draw_line(&iv[0], &iv[1]);
				g3_draw_line(&iv[2], &iv[3]);
				g3_draw_line(&iv[4], &iv[5]);
				g3_draw_line(&iv[6], &iv[7]);
				g3_draw_line(&iv[0], &iv[2]);
				g3_draw_line(&iv[1], &iv[3]);
				g3_draw_line(&iv[4], &iv[6]);
				g3_draw_line(&iv[5], &iv[7]);
				g3_draw_line(&iv[0], &iv[4]);
				g3_draw_line(&iv[1], &iv[5]);
				g3_draw_line(&iv[2], &iv[6]);
				g3_draw_line(&iv[3], &iv[7]);
			}

		}
}

void draw_compass_arrow(vec3d *v0) {
	vec3d	v1 = { 0.0f };
	vertex	tv0, tv1;

	g3_rotate_vertex(&tv0, v0);
	g3_rotate_vertex(&tv1, &v1);
	g3_project_vertex(&tv0);
	g3_project_vertex(&tv1);
	//	tv0.sx = (tv0.sx - tv1.sx) * 1 + tv1.sx;
	//	tv0.sy = (tv0.sy - tv1.sy) * 1 + tv1.sy;
	g3_draw_line(&tv0, &tv1);
}

void draw_orient_sphere(object *obj, int r, int g, int b) {
	int		flag = 0;
	vertex	v;
	vec3d	v1, v2;
	float		size;

	size = fl_sqrt(vm_vec_dist(&eye_pos, &obj->pos) / 20.0f);
	if (size < LOLLIPOP_SIZE)
		size = LOLLIPOP_SIZE;

	if ((obj->type != OBJ_WAYPOINT) && (obj->type != OBJ_POINT)) {
		flag = (vm_vec_dot(&eye_orient.vec.fvec, &obj->orient.vec.fvec) < 0.0f);
		v1 = v2 = obj->pos;
		vm_vec_scale_add2(&v1, &obj->orient.vec.fvec, size);
		vm_vec_scale_add2(&v2, &obj->orient.vec.fvec, size * 1.5f);

		if (!flag) {
			gr_set_color(192, 192, 192);
			rpd_line(&v1, &v2);
		}
	}

	gr_set_color(r, g, b);
	g3_rotate_vertex(&v, &obj->pos);
	if (!(v.codes & CC_BEHIND))
		if (!(g3_project_vertex(&v) & PF_OVERFLOW))
			g3_draw_sphere(&v, size);

	if (flag) {
		gr_set_color(192, 192, 192);
		rpd_line(&v1, &v2);
	}
}

void draw_orient_sphere2(int col, object *obj, int r, int g, int b) {
	int		flag = 0;
	vertex	v;
	vec3d	v1, v2;
	float		size;

	size = fl_sqrt(vm_vec_dist(&eye_pos, &obj->pos) / 20.0f);
	if (size < LOLLIPOP_SIZE)
		size = LOLLIPOP_SIZE;

	if ((obj->type != OBJ_WAYPOINT) && (obj->type != OBJ_POINT)) {
		flag = (vm_vec_dot(&eye_orient.vec.fvec, &obj->orient.vec.fvec) < 0.0f);

		v1 = v2 = obj->pos;
		vm_vec_scale_add2(&v1, &obj->orient.vec.fvec, size);
		vm_vec_scale_add2(&v2, &obj->orient.vec.fvec, size * 1.5f);

		if (!flag) {
			gr_set_color(192, 192, 192);
			rpd_line(&v1, &v2);
		}
	}

	g3_rotate_vertex(&v, &obj->pos);
	if (!(v.codes & CC_BEHIND))
		if (!(g3_project_vertex(&v) & PF_OVERFLOW)) {
			gr_set_color((col >> 16) & 0xff, (col >> 8) & 0xff, col & 0xff);
			g3_draw_sphere(&v, size);
			gr_set_color(r, g, b);
			g3_draw_sphere(&v, size * 0.75f);
		}

	if (flag) {
		gr_set_color(192, 192, 192);
		rpd_line(&v1, &v2);
	}
}

void fredhtl_render_subsystem_bounding_box(subsys_to_render *s2r)
{
	vertex text_center;
	SCP_string buf;

	auto objp = s2r->ship_obj;
	auto ss = s2r->cur_subsys;

	auto pmi = model_get_instance(Ships[objp->instance].model_instance_num);
	auto pm = model_get(pmi->model_num);
	int subobj_num = ss->system_info->subobj_num;

	auto bsp = &pm->submodel[subobj_num];

	vec3d front_top_left = bsp->bounding_box[7];
	vec3d front_top_right = bsp->bounding_box[6];
	vec3d front_bot_left = bsp->bounding_box[4];
	vec3d front_bot_right = bsp->bounding_box[5];
	vec3d back_top_left = bsp->bounding_box[3];
	vec3d back_top_right = bsp->bounding_box[2];
	vec3d back_bot_left = bsp->bounding_box[0];
	vec3d back_bot_right = bsp->bounding_box[1];

	gr_set_color(255, 32, 32);

	fred_enable_htl();

	// get into the frame of reference of the submodel
	int g3_count = 1;
	g3_start_instance_matrix(&objp->pos, &objp->orient, true);
	int mn = subobj_num;
	while ((mn >= 0) && (pm->submodel[mn].parent >= 0))
	{
		vec3d offset = pm->submodel[mn].offset;
		vm_vec_add2(&offset, &pmi->submodel[mn].canonical_offset);

		g3_start_instance_matrix(&offset, &pmi->submodel[mn].canonical_orient, true);
		g3_count++;
		mn = pm->submodel[mn].parent;
	}


	//draw a cube around the subsystem
	g3_draw_htl_line(&front_top_left, &front_top_right);
	g3_draw_htl_line(&front_top_right, &front_bot_right);
	g3_draw_htl_line(&front_bot_right, &front_bot_left);
	g3_draw_htl_line(&front_bot_left, &front_top_left);

	g3_draw_htl_line(&back_top_left, &back_top_right);
	g3_draw_htl_line(&back_top_right, &back_bot_right);
	g3_draw_htl_line(&back_bot_right, &back_bot_left);
	g3_draw_htl_line(&back_bot_left, &back_top_left);

	g3_draw_htl_line(&front_top_left, &back_top_left);
	g3_draw_htl_line(&front_top_right, &back_top_right);
	g3_draw_htl_line(&front_bot_left, &back_bot_left);
	g3_draw_htl_line(&front_bot_right, &back_bot_right);


	//draw another cube around a gun for a two-part turret
	if ((ss->system_info->turret_gun_sobj >= 0) && (ss->system_info->turret_gun_sobj != ss->system_info->subobj_num))
	{
		bsp_info *bsp_turret = &pm->submodel[ss->system_info->turret_gun_sobj];

		front_top_left = bsp_turret->bounding_box[7];
		front_top_right = bsp_turret->bounding_box[6];
		front_bot_left = bsp_turret->bounding_box[4];
		front_bot_right = bsp_turret->bounding_box[5];
		back_top_left = bsp_turret->bounding_box[3];
		back_top_right = bsp_turret->bounding_box[2];
		back_bot_left = bsp_turret->bounding_box[0];
		back_bot_right = bsp_turret->bounding_box[1];

		g3_start_instance_matrix(&bsp_turret->offset, &pmi->submodel[ss->system_info->turret_gun_sobj].canonical_orient, true);

		g3_draw_htl_line(&front_top_left, &front_top_right);
		g3_draw_htl_line(&front_top_right, &front_bot_right);
		g3_draw_htl_line(&front_bot_right, &front_bot_left);
		g3_draw_htl_line(&front_bot_left, &front_top_left);

		g3_draw_htl_line(&back_top_left, &back_top_right);
		g3_draw_htl_line(&back_top_right, &back_bot_right);
		g3_draw_htl_line(&back_bot_right, &back_bot_left);
		g3_draw_htl_line(&back_bot_left, &back_top_left);

		g3_draw_htl_line(&front_top_left, &back_top_left);
		g3_draw_htl_line(&front_top_right, &back_top_right);
		g3_draw_htl_line(&front_bot_left, &back_bot_left);
		g3_draw_htl_line(&front_bot_right, &back_bot_right);

		g3_done_instance(true);
	}

	for (int i = 0; i < g3_count; i++)
		g3_done_instance(true);

	fred_disable_htl();

	// get text
	buf = ss->system_info->subobj_name;

	// add weapons if present
	for (int i = 0; i < ss->weapons.num_primary_banks; ++i)
	{
		int wi = ss->weapons.primary_bank_weapons[i];
		if (wi >= 0)
		{
			buf += "\n";
			buf += Weapon_info[wi].name;
		}
	}
	for (int i = 0; i < ss->weapons.num_secondary_banks; ++i)
	{
		int wi = ss->weapons.secondary_bank_weapons[i];
		if (wi >= 0)
		{
			buf += "\n";
			buf += Weapon_info[wi].name;
		}
	}

	//draw the text.  rotate the center of the subsystem into place before finding out where to put the text
	vec3d center_pt;
	vm_vec_unrotate(&center_pt, &bsp->offset, &objp->orient);
	vm_vec_add2(&center_pt, &objp->pos);
	g3_rotate_vertex(&text_center, &center_pt);
	g3_project_vertex(&text_center);
	if (!(text_center.flags & PF_OVERFLOW)) {
		gr_set_color_fast(&colour_white);
		gr_string((int)text_center.screen.xyw.x, (int)text_center.screen.xyw.y, buf.c_str());
	}
}

void fred_disable_htl() {
	gr_end_proj_matrix();
	gr_end_view_matrix();
}

void fred_enable_htl() {
	gr_set_proj_matrix((4.0f / 9.0f) * PI * (Briefing_dialog ? Briefing_window_FOV : FRED_DEFAULT_HTL_FOV), gr_screen.aspect*(float) gr_screen.clip_width / (float) gr_screen.clip_height, 1.0f, FRED_DEAFULT_HTL_DRAW_DIST);
	gr_set_view_matrix(&Eye_position, &Eye_matrix);
}

void fred_render_grid(grid *gridp) {
	int	i, ncols, nrows;

	fred_enable_htl();
	gr_zbuffer_set(0);

	if (!Fred_grid_colors_inited) {
		Fred_grid_colors_inited = 1;

		gr_init_alphacolor(&Fred_grid_dark_aa, 64, 64, 64, 255);
		gr_init_alphacolor(&Fred_grid_bright_aa, 128, 128, 128, 255);
		gr_init_color(&Fred_grid_dark, 64, 64, 64);
		gr_init_color(&Fred_grid_bright, 128, 128, 128);
	}

	ncols = gridp->ncols;
	nrows = gridp->nrows;
	if (double_fine_gridlines) {
		ncols *= 2;
		nrows *= 2;
	}

	if (Aa_gridlines)
		gr_set_color_fast(&Fred_grid_dark_aa);
	else
		gr_set_color_fast(&Fred_grid_dark);

	//	Draw the column lines.
	for (i = 0; i <= ncols; i++) {
		g3_draw_htl_line(&gridp->gpoints1[i], &gridp->gpoints2[i]);
	}
	//	Draw the row lines.
	for (i = 0; i <= nrows; i++) {
		g3_draw_htl_line(&gridp->gpoints3[i], &gridp->gpoints4[i]);
	}

	ncols = gridp->ncols / 2;
	nrows = gridp->nrows / 2;

	// now draw the larger, brighter gridlines that is x10 the scale of smaller one.
	if (Aa_gridlines)
		gr_set_color_fast(&Fred_grid_bright_aa);
	else
		gr_set_color_fast(&Fred_grid_bright);

	for (i = 0; i <= ncols; i++) {
		g3_draw_htl_line(&gridp->gpoints5[i], &gridp->gpoints6[i]);
	}

	for (i = 0; i <= nrows; i++) {
		g3_draw_htl_line(&gridp->gpoints7[i], &gridp->gpoints8[i]);
	}

	fred_disable_htl();
	gr_zbuffer_set(1);
}

void fred_render_init() {
	vec3d f, u, r;

	physics_init(&view_physics);
	view_physics.max_vel.xyz.z = 5.0f;		//forward/backward
	view_physics.max_rotvel.xyz.x = 1.5f;		//pitch	
	memset(&view_controls, 0, sizeof(control_info));

	vm_vec_make(&view_pos, 0.0f, 150.0f, -200.0f);
	vm_vec_make(&f, 0.0f, -0.5f, 0.866025404f);  // 30 degree angle
	vm_vec_make(&u, 0.0f, 0.866025404f, 0.5f);
	vm_vec_make(&r, 1.0f, 0.0f, 0.0f);
	vm_vector_2_matrix(&view_orient, &f, &u, &r);

	The_grid = create_default_grid();
	maybe_create_new_grid(The_grid, &view_pos, &view_orient, 1);
	//	vm_set_identity(&view_orient);

	gr_init_alphacolor(&colour_white, 255, 255, 255, 255);
	gr_init_alphacolor(&colour_green, 0, 200, 0, 255);
	gr_init_alphacolor(&colour_yellow_green, 200, 255, 0, 255);
	gr_init_alphacolor(&colour_black, 0, 0, 0, 255);
}

void game_do_frame() {
	int key, cmode;
	vec3d viewer_position, control_pos;
	object *objp;
	matrix control_orient;

	inc_mission_time();

	// sync all timestamps across the entire frame
	timer_start_frame();

	viewer_position = my_orient.vec.fvec;
	vm_vec_scale(&viewer_position, my_pos.xyz.z);

	if ((viewpoint == 1) && !query_valid_object(view_obj))
		viewpoint = 0;

	//If the music player dialog is open and music is selected
	if (Music_player_dialog.IsPlayerActive()) {
		Music_player_dialog.DoFrame();
	}

	key = key_inkey();
	process_system_keys(key);
	cmode = Control_mode;
	if ((viewpoint == 1) && !cmode)
		cmode = 2;

	control_pos = Last_control_pos;
	control_orient = Last_control_orient;

	//	if ((key & KEY_MASK) == key)  // unmodified
	switch (cmode) {
	case 0:		//	Control the viewer's location and orientation
		process_controls(&view_pos, &view_orient, f2fl(Frametime), key, 1);
		control_pos = view_pos;
		control_orient = view_orient;
		break;

	case 2:  // Control viewpoint object
		if (!Objects[view_obj].flags[Object::Object_Flags::Locked_from_editing]) {
			process_controls(&Objects[view_obj].pos, &Objects[view_obj].orient, f2fl(Frametime), key);
			object_moved(&Objects[view_obj]);
			control_pos = Objects[view_obj].pos;
			control_orient = Objects[view_obj].orient;
		}
		break;

	case 1:  //	Control the current object's location and orientation
		if (query_valid_object() && !Objects[cur_object_index].flags[Object::Object_Flags::Locked_from_editing]) {
			vec3d delta_pos, leader_old_pos;
			matrix leader_orient, leader_transpose, tmp;
			object *leader;

			leader = &Objects[cur_object_index];
			leader_old_pos = leader->pos;  // save original position
			leader_orient = leader->orient;			// save original orientation
			vm_copy_transpose(&leader_transpose, &leader_orient);

			process_controls(&leader->pos, &leader->orient, f2fl(Frametime), key);
			vm_vec_sub(&delta_pos, &leader->pos, &leader_old_pos);  // get position change
			control_pos = leader->pos;
			control_orient = leader->orient;

			objp = GET_FIRST(&obj_used_list);
			while (objp != END_OF_LIST(&obj_used_list)) {
				Assert(objp->type != OBJ_NONE);
				if ((objp->flags[Object::Object_Flags::Marked]) && (cur_object_index != OBJ_INDEX(objp))) {
					if (Group_rotate) {
						matrix rot_trans;
						vec3d tmpv1, tmpv2;

						// change rotation matrix to rotate in opposite direction.  This rotation
						// matrix is what the leader ship has rotated by.
						vm_copy_transpose(&rot_trans, &view_physics.last_rotmat);

						// get point relative to our point of rotation (make POR the origin).  Since
						// only the leader has been moved yet, and not the objects, we have to use
						// the old leader's position.
						vm_vec_sub(&tmpv1, &objp->pos, &leader_old_pos);

						// convert point from real-world coordinates to leader's relative coordinate
						// system (z=forward vec, y=up vec, x=right vec
						vm_vec_rotate(&tmpv2, &tmpv1, &leader_orient);

						// now rotate the point by the transpose from above.
						vm_vec_rotate(&tmpv1, &tmpv2, &rot_trans);

						// convert point back into real-world coordinates
						vm_vec_rotate(&tmpv2, &tmpv1, &leader_transpose);

						// and move origin back to real-world origin.  Object is now at its correct
						// position.  Note we used the leader's new position, instead of old position.
						vm_vec_add(&objp->pos, &leader->pos, &tmpv2);

						// Now fix the object's orientation to what it should be.
						vm_matrix_x_matrix(&tmp, &objp->orient, &view_physics.last_rotmat);
						vm_orthogonalize_matrix(&tmp);  // safety check
						objp->orient = tmp;

					} else {
						vm_vec_add2(&objp->pos, &delta_pos);
						vm_matrix_x_matrix(&tmp, &objp->orient, &view_physics.last_rotmat);
						objp->orient = tmp;
					}
				}

				objp = GET_NEXT(objp);
			}

			objp = GET_FIRST(&obj_used_list);
			while (objp != END_OF_LIST(&obj_used_list)) {
				if (objp->flags[Object::Object_Flags::Marked])
					object_moved(objp);

				objp = GET_NEXT(objp);
			}

			set_modified();
		}

		break;

	default:
		Assert(0);
	}

	if (Lookat_mode && query_valid_object()) {
		float dist;

		dist = vm_vec_dist(&view_pos, &Objects[cur_object_index].pos);
		vm_vec_scale_add(&view_pos, &Objects[cur_object_index].pos, &view_orient.vec.fvec, -dist);
	}

	switch (viewpoint) {
	case 0:
		eye_pos = view_pos;
		eye_orient = view_orient;
		break;

	case 1:
		eye_pos = Objects[view_obj].pos;
		eye_orient = Objects[view_obj].orient;
		break;

	default:
		Assert(0);
	}

	maybe_create_new_grid(The_grid, &eye_pos, &eye_orient);

	if (Cursor_over != Last_cursor_over) {
		Last_cursor_over = Cursor_over;
		Update_window = 1;
	}

	// redraw screen if controlled object moved or rotated
	if (vm_vec_cmp(&control_pos, &Last_control_pos) || vm_matrix_cmp(&control_orient, &Last_control_orient)) {
		Update_window = 1;
		Last_control_pos = control_pos;
		Last_control_orient = control_orient;
	}

	// redraw screen if current viewpoint moved or rotated
	if (vm_vec_cmp(&eye_pos, &Last_eye_pos) || vm_matrix_cmp(&eye_orient, &Last_eye_orient)) {
		Update_window = 1;
		Last_eye_pos = eye_pos;
		Last_eye_orient = eye_orient;
	}
}

vec3d* get_subsystem_world_pos2(object* parent_obj, ship_subsys* subsys, vec3d* world_pos) {
	if (subsys == NULL) {
		*world_pos = parent_obj->pos;
		return world_pos;
	}

	vm_vec_unrotate(world_pos, &subsys->system_info->pnt, &parent_obj->orient);
	vm_vec_add2(world_pos, &parent_obj->pos);

	return world_pos;
}

int get_subsys_bounding_rect(object *ship_obj, ship_subsys *subsys, int *x1, int *x2, int *y1, int *y2) {
	if (subsys != NULL) {
		vertex subobj_vertex;
		vec3d	subobj_pos;

		get_subsystem_world_pos2(ship_obj, subsys, &subobj_pos);

		g3_rotate_vertex(&subobj_vertex, &subobj_pos);

		g3_project_vertex(&subobj_vertex);
		if (subobj_vertex.flags & PF_OVERFLOW)  // if overflow, no point in drawing brackets
			return 0;

		int bound_rc;

		bound_rc = subobj_find_2d_bound(subsys->system_info->radius, &ship_obj->orient, &subobj_pos, x1, y1, x2, y2);
		if (bound_rc != 0)
			return 0;

		return 1;
	}

	return 0;
}

void hilight_bitmap() {
	/*
	int i;
	vertex p[4];

	if (Starfield_bitmaps[Cur_bitmap].bitmap_index == -1)  // can't draw if no bitmap
	return;

	for (i=0; i<4; i++)
	{
	g3_rotate_faraway_vertex(&p[i], &Starfield_bitmaps[Cur_bitmap].points[i]);
	if (p[i].codes & CC_BEHIND)
	return;

	g3_project_vertex(&p[i]);
	if (p[i].flags & PF_OVERFLOW)
	return;
	}

	gr_set_color(255, 255, 255);
	g3_draw_line(&p[0], &p[1]);
	g3_draw_line(&p[1], &p[2]);
	g3_draw_line(&p[2], &p[3]);
	g3_draw_line(&p[3], &p[0]);
	*/
}

void inc_mission_time() {
	fix thistime;

	thistime = timer_get_fixed_seconds();
	if (!lasttime) {
		Frametime = F1_0 / 30;
	} else {
		Frametime = thistime - lasttime;
	}

	if (Frametime > MAX_FRAMETIME) {
		Frametime = MAX_FRAMETIME;
	} else if (Frametime < 0) {
		Frametime = thistime = MIN_FRAMETIME;
	} else if (Frametime < MIN_FRAMETIME) {
		if (!Cmdline_NoFPSCap) {
			thistime = MIN_FRAMETIME - Frametime;
			Sleep(DWORD(f2fl(thistime) * 1000.0f));
			thistime = timer_get_fixed_seconds();
		}

		Frametime = MIN_FRAMETIME;
	}

	Missiontime += Frametime;
	lasttime = thistime;
}

void level_controlled() {
	int cmode, count = 0;
	object *objp;

	cmode = Control_mode;
	if ((viewpoint == 1) && !cmode)
		cmode = 2;

	switch (cmode) {
	case 0:		//	Control the viewer's location and orientation
		level_object(&view_orient);
		break;

	case 2:  // Control viewpoint object
		if (!Objects[view_obj].flags[Object::Object_Flags::Locked_from_editing]) {
			level_object(&Objects[view_obj].orient);
			object_moved(&Objects[view_obj]);
			set_modified();
			FREDDoc_ptr->autosave("level object");
		}
		break;

	case 1:  //	Control the current object's location and orientation
		objp = GET_FIRST(&obj_used_list);
		while (objp != END_OF_LIST(&obj_used_list)) {
			if (objp->flags[Object::Object_Flags::Marked])
				level_object(&objp->orient);

			objp = GET_NEXT(objp);
		}

		objp = GET_FIRST(&obj_used_list);
		while (objp != END_OF_LIST(&obj_used_list)) {
			if (objp->flags[Object::Object_Flags::Marked]) {
				object_moved(objp);
				count++;
			}

			objp = GET_NEXT(objp);
		}

		if (count) {
			if (count > 1)
				FREDDoc_ptr->autosave("level objects");
			else
				FREDDoc_ptr->autosave("level object");

			set_modified();
		}

		break;
	}

	return;
}

void level_object(matrix *orient) {
	vec3d u;

	u = orient->vec.uvec = The_grid->gmatrix.vec.uvec;
	if (u.xyz.x)  // y-z plane
	{
		orient->vec.fvec.xyz.x = orient->vec.rvec.xyz.x = 0.0f;

	} else if (u.xyz.y) {  // x-z plane
		orient->vec.fvec.xyz.y = orient->vec.rvec.xyz.y = 0.0f;

	} else if (u.xyz.z) {  // x-y plane
		orient->vec.fvec.xyz.z = orient->vec.rvec.xyz.z = 0.0f;
	}

	vm_fix_matrix(orient);
}

void move_mouse(int btn, int mdx, int mdy) {
	int dx, dy;

	dx = mdx - last_x;
	dy = mdy - last_y;
	last_x = mdx;
	last_y = mdy;

	if (btn & 1) {
		matrix tempm, mousem;

		if (dx || dy) {
			vm_trackball(dx, dy, &mousem);
			vm_matrix_x_matrix(&tempm, &trackball_orient, &mousem);
			trackball_orient = tempm;
			view_orient = trackball_orient;
		}
	}

	if (btn & 2) {
		my_pos.xyz.z += (float) dy;
	}
}

int object_check_collision(object *objp, vec3d *p0, vec3d *p1, vec3d *hitpos) {
	mc_info mc;

	if ((objp->type == OBJ_NONE) || (objp->type == OBJ_POINT))
		return 0;

	if ((objp->type == OBJ_WAYPOINT) && !Show_waypoints)
		return 0;

	if ((objp->type == OBJ_START) && !Show_starts)
		return 0;

	if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) {
		if (!Show_ships)
			return 0;

		if (!Show_iff[Ships[objp->instance].team])
			return 0;
	}

	if (objp->flags[Object::Object_Flags::Hidden, Object::Object_Flags::Locked_from_editing])
		return 0;

	if ((Show_ship_models || Show_outlines) && (objp->type == OBJ_SHIP)) {
		mc.model_num = Ship_info[Ships[objp->instance].ship_info_index].model_num;			// Fill in the model to check

	} else if ((Show_ship_models || Show_outlines) && (objp->type == OBJ_START)) {
		mc.model_num = Ship_info[Ships[objp->instance].ship_info_index].model_num;			// Fill in the model to check

	} else
		return fvi_ray_sphere(hitpos, p0, p1, &objp->pos, (objp->radius > 0.1f) ? objp->radius : LOLLIPOP_SIZE);

	mc.model_instance_num = -1;
	mc.orient = &objp->orient;	// The object's orient
	mc.pos = &objp->pos;			// The object's position
	mc.p0 = p0;					// Point 1 of ray to check
	mc.p1 = p1;					// Point 2 of ray to check
	mc.flags = MC_CHECK_MODEL | MC_CHECK_RAY;  // flags
	model_collide(&mc);
	*hitpos = mc.hit_point_world;
	if (mc.num_hits < 1) {
		// check shield
		mc.orient = &objp->orient;	// The object's orient
		mc.pos = &objp->pos;			// The object's position
		mc.p0 = p0;					// Point 1 of ray to check
		mc.p1 = p1;					// Point 2 of ray to check
		mc.flags = MC_CHECK_SHIELD;	// flags
		model_collide(&mc);
		*hitpos = mc.hit_point_world;
	}

	return mc.num_hits;
}

void process_controls(vec3d *pos, matrix *orient, float frametime, int key, int mode) {
	static std::unique_ptr<io::spacemouse::SpaceMouse> spacemouse = io::spacemouse::SpaceMouse::searchSpaceMice(0);

	if (Flying_controls_mode) {
		grid_read_camera_controls(&view_controls, frametime);
		if (spacemouse != nullptr) {
			auto spacemouse_movement = spacemouse->getMovement();
			spacemouse_movement.handleNonlinearities(Fred_spacemouse_nonlinearity);
			view_controls.pitch += spacemouse_movement.rotation.p;
			view_controls.vertical += spacemouse_movement.translation.xyz.z;
			view_controls.heading += spacemouse_movement.rotation.h;
			view_controls.sideways += spacemouse_movement.translation.xyz.x;
			view_controls.bank += spacemouse_movement.rotation.b;
			view_controls.forward += spacemouse_movement.translation.xyz.y;
		}

		if (key_get_shift_status())
			memset(&view_controls, 0, sizeof(control_info));

		if ((fabs(view_controls.pitch) > (frametime / 100)) &&
			(fabs(view_controls.vertical) > (frametime / 100)) &&
			(fabs(view_controls.heading) > (frametime / 100)) &&
			(fabs(view_controls.sideways) > (frametime / 100)) &&
			(fabs(view_controls.bank) > (frametime / 100)) &&
			(fabs(view_controls.forward) > (frametime / 100)))
			Update_window = 1;

		flFrametime = frametime;
		physics_read_flying_controls(orient, &view_physics, &view_controls, flFrametime);
		if (mode)
			physics_sim_editor(pos, orient, &view_physics, frametime);
		else
			physics_sim(pos, orient, &view_physics, &vmd_zero_vector, frametime);

	} else {
		vec3d		movement_vec, rel_movement_vec;
		angles		rotangs;
		matrix		newmat, rotmat;

		process_movement_keys(key, &movement_vec, &rotangs);
		if (spacemouse != nullptr) {
			auto spacemouse_movement = spacemouse->getMovement();
			spacemouse_movement.handleNonlinearities(Fred_spacemouse_nonlinearity);
			movement_vec += spacemouse_movement.translation;
			rotangs += spacemouse_movement.rotation;
		}

		vm_vec_rotate(&rel_movement_vec, &movement_vec, &The_grid->gmatrix);
		vm_vec_add2(pos, &rel_movement_vec);

		vm_angles_2_matrix(&rotmat, &rotangs);
		if (rotangs.h && Universal_heading)
			vm_transpose(orient);
		vm_matrix_x_matrix(&newmat, orient, &rotmat);
		*orient = newmat;
		if (rotangs.h && Universal_heading)
			vm_transpose(orient);
	}
}

void process_movement_keys(int key, vec3d *mvec, angles *angs) {
	int	raw_key;

	mvec->xyz.x = 0.0f;
	mvec->xyz.y = 0.0f;
	mvec->xyz.z = 0.0f;
	angs->p = 0.0f;
	angs->b = 0.0f;
	angs->h = 0.0f;

	raw_key = key & 0xff;

	switch (raw_key) {
	case KEY_PAD1:		mvec->xyz.x += -1.0f;	break;
	case KEY_PAD3:		mvec->xyz.x += +1.0f;	break;
	case KEY_PADPLUS:	mvec->xyz.y += -1.0f;	break;
	case KEY_PADMINUS:	mvec->xyz.y += +1.0f;	break;
	case KEY_A:			mvec->xyz.z += +1.0f;	break;
	case KEY_Z:			mvec->xyz.z += -1.0f;	break;
	case KEY_PAD4:		angs->h += -0.1f;	break;
	case KEY_PAD6:		angs->h += +0.1f;	break;
	case KEY_PAD8:		angs->p += -0.1f;	break;
	case KEY_PAD2:		angs->p += +0.1f;	break;
	case KEY_PAD7:		angs->b += -0.1f;	break;
	case KEY_PAD9:		angs->b += +0.1f;	break;

	}

	if (key & KEY_SHIFTED) {
		vm_vec_scale(mvec, 5.0f);
		angs->p *= 5.0f;
		angs->b *= 5.0f;
		angs->h *= 5.0f;
	}
}

void process_system_keys(int key) {
	//	mprintf(("Key = %d\n", key));
	switch (key) {

	case KEY_LAPOSTRO:
		CFREDView::GetView()->cycle_constraint();
		break;

	case KEY_R:  // for some stupid reason, an accelerator for 'R' doesn't work.
		Editing_mode = 2;
		break;

	case KEY_SPACEBAR:
		Selection_lock = !Selection_lock;
		break;

	case KEY_ESC:
		if (button_down)
			cancel_drag();

		break;
	}
}

void render_active_rect(void) {
	if (box_marking) {
		gr_set_color(255, 255, 255);
		gr_line(marking_box.x1, marking_box.y1, marking_box.x1, marking_box.y2);
		gr_line(marking_box.x1, marking_box.y2, marking_box.x2, marking_box.y2);
		gr_line(marking_box.x2, marking_box.y2, marking_box.x2, marking_box.y1);
		gr_line(marking_box.x2, marking_box.y1, marking_box.x1, marking_box.y1);
	}
}

void render_compass(void) {
	vec3d v, eye = { 0.0f };

	if (!Show_compass)
		return;

	gr_set_clip(gr_screen.max_w - 100, 0, 100, 100);
	g3_start_frame(0);  // ** Accounted for
	// required !!!
	vm_vec_scale_add2(&eye, &eye_orient.vec.fvec, -1.5f);
	g3_set_view_matrix(&eye, &eye_orient, 1.0f);

	v.xyz.x = 1.0f;
	v.xyz.y = v.xyz.z = 0.0f;
	if (vm_vec_dot(&eye, &v) < 0.0f)
		gr_set_color(159, 20, 20);
	else
		gr_set_color(255, 32, 32);
	draw_compass_arrow(&v);

	v.xyz.y = 1.0f;
	v.xyz.x = v.xyz.z = 0.0f;
	if (vm_vec_dot(&eye, &v) < 0.0f)
		gr_set_color(20, 159, 20);
	else
		gr_set_color(32, 255, 32);
	draw_compass_arrow(&v);

	v.xyz.z = 1.0f;
	v.xyz.x = v.xyz.y = 0.0f;
	if (vm_vec_dot(&eye, &v) < 0.0f)
		gr_set_color(20, 20, 159);
	else
		gr_set_color(32, 32, 255);
	draw_compass_arrow(&v);

	g3_end_frame(); // ** Accounted for

}

void render_frame() {
	GR_DEBUG_SCOPE("Fred render frame");

	char buf[256];
	int x, y, w, h, inst;
	vec3d pos;
	vertex v;
	angles a, a_deg;  //a is in rads, a_deg is in degrees

	g3_end_frame();	 // ** Accounted for

	gr_reset_clip();
	gr_clear();

	if (Briefing_dialog) {
		CRect rect;

		Fred_main_wnd->GetClientRect(rect);
		True_rw = rect.Width();
		True_rh = rect.Height();
		if (Fixed_briefing_size) {
			True_rw = Briefing_window_resolution[0];
			True_rh = Briefing_window_resolution[1];

		} else {
			if ((float) True_rh / (float) True_rw > (float) Briefing_window_resolution[1] / (float) Briefing_window_resolution[0]) {
				True_rh = (int) ((float) Briefing_window_resolution[1] * (float) True_rw / (float) Briefing_window_resolution[0]);

			} else {  // Fred is wider than briefing window
				True_rw = (int) ((float) Briefing_window_resolution[0] * (float) True_rh / (float) Briefing_window_resolution[1]);
			}
		}

		g3_start_frame(0); // ** Accounted for
		gr_set_color(255, 255, 255);
		gr_line(0, True_rh, True_rw, True_rh);
		gr_line(True_rw, 0, True_rw, True_rh);
		g3_end_frame();	 // ** Accounted for
		gr_set_clip(0, 0, True_rw, True_rh);
	}

	g3_start_frame(1);  // ** Accounted for
	// 1 means use zbuffering

	font::set_font(font::FONT1);
	light_reset();

	g3_set_view_matrix(&eye_pos, &eye_orient, (Briefing_dialog ? Briefing_window_FOV : FRED_DEFAULT_HTL_FOV));
	Viewer_pos = eye_pos;  // for starfield code

	fred_enable_htl();
	if (Bg_bitmap_dialog) {
		stars_draw(Show_stars, 1, Show_stars, 0, 0);
	} else {
		stars_draw(Show_stars, Show_stars, Show_stars, 0, 0);
	}
	fred_disable_htl();

	if (Show_horizon) {
		gr_set_color(128, 128, 64);
		g3_draw_horizon_line();
	}

	if (Show_asteroid_field) {
		gr_set_color(192, 96, 16);
		draw_asteroid_field();
	}

	if (Show_grid)
		fred_render_grid(The_grid);
	if (Bg_bitmap_dialog)
		hilight_bitmap();

	gr_set_color(0, 0, 64);
	render_models();

	if (Show_distances) {
		display_distances();
	}

	display_ship_info();
	display_active_ship_subsystem();
	render_active_rect();

	if (query_valid_object(Cursor_over)) {  // display a tool-tip like infobox
		pos = Objects[Cursor_over].pos;
		inst = Objects[Cursor_over].instance;
		if ((Objects[Cursor_over].type == OBJ_SHIP) || (Objects[Cursor_over].type == OBJ_START)) {
			vm_extract_angles_matrix(&a, &Objects[Cursor_over].orient);

			a_deg.h = a.h * CONVERT_DEGREES; // convert angles to more readable degrees
			a_deg.p = a.p * CONVERT_DEGREES;
			a_deg.b = a.b * CONVERT_DEGREES;

			sprintf(buf, "%s\n%s\n( %.1f , %.1f , %.1f ) \nPitch: %.2f\nBank: %.2f\nHeading: %.2f",
					Ships[inst].ship_name, Ship_info[Ships[inst].ship_info_index].short_name,
					pos.xyz.x, pos.xyz.y, pos.xyz.z, a_deg.p, a_deg.b, a_deg.h);

		} else if (Objects[Cursor_over].type == OBJ_WAYPOINT) {
			int idx;
			waypoint_list *wp_list = find_waypoint_list_with_instance(inst, &idx);
			Assert(wp_list != NULL);
			sprintf(buf, "%s\nWaypoint %d\n( %.1f , %.1f , %.1f ) ", wp_list->get_name(), idx + 1, pos.xyz.x, pos.xyz.y, pos.xyz.z);

		} else if (Objects[Cursor_over].type == OBJ_POINT) {
			sprintf(buf, "Briefing icon\n( %.1f , %.1f , %.1f ) ", pos.xyz.x, pos.xyz.y, pos.xyz.z);

		} else
			sprintf(buf, "( %.1f , %.1f , %.1f ) ", pos.xyz.x, pos.xyz.y, pos.xyz.z);

		g3_rotate_vertex(&v, &pos);
		if (!(v.codes & CC_BEHIND))
			if (!(g3_project_vertex(&v) & PF_OVERFLOW)) {
				GR_DEBUG_SCOPE("Draw tooltip");

				gr_get_string_size(&w, &h, buf);

				x = (int) v.screen.xyw.x;
				y = (int) v.screen.xyw.y + 20;

				gr_set_color_fast(&colour_white);
				gr_rect(x - 7, y - 6, w + 8, h + 7);

				gr_set_color_fast(&colour_black);
				gr_rect(x - 5, y - 5, w + 5, h + 5);

				gr_set_color_fast(&colour_white);
				gr_string(x, y, buf);
			}
	}

	gr_set_color(0, 160, 0);

	fred_enable_htl();
	jumpnode_render_all();
	fred_disable_htl();

	sprintf(buf, "(%.1f,%.1f,%.1f)", eye_pos.xyz.x, eye_pos.xyz.y, eye_pos.xyz.z);
	gr_get_string_size(&w, &h, buf);
	gr_set_color_fast(&colour_white);
	gr_string(gr_screen.max_w - w - 2, 2, buf);

	g3_end_frame();	 // ** Accounted for
	render_compass();

	gr_flip();

	gr_reset_clip();
	if (Briefing_dialog)
		gr_set_clip(0, 0, True_rw, True_rh);

	g3_start_frame(0);	 // ** Accounted for
	g3_set_view_matrix(&eye_pos, &eye_orient, (Briefing_dialog ? Briefing_window_FOV : FRED_DEFAULT_HTL_FOV));
}

void render_models(void) {
	GR_DEBUG_SCOPE("Render models");

	gr_set_color_fast(&colour_white);

	render_count = 0;

	if ((ENVMAP == -1) && strlen(The_mission.envmap_name)) {
		ENVMAP = bm_load(The_mission.envmap_name);
	}

	bool f = false;
	fred_enable_htl();

	obj_render_all(render_one_model_htl, &f);

	fred_disable_htl();

	if (Briefing_dialog) {
		obj_render_all(render_one_model_briefing_screen, &f);
		Briefing_dialog->batch_render();
	}

}

void render_model_x(const vec3d *pos, const grid *gridp, int col_scheme) {
	vec3d	gpos;	//	Location of point on grid.
	vec3d	tpos;
	float	dxz;
	plane	tplane;
	const vec3d	*gv;

	if (!Show_grid_positions)
		return;

	tplane.A = gridp->gmatrix.vec.uvec.xyz.x;
	tplane.B = gridp->gmatrix.vec.uvec.xyz.y;
	tplane.C = gridp->gmatrix.vec.uvec.xyz.z;
	tplane.D = gridp->planeD;

	compute_point_on_plane(&gpos, &tplane, pos);
	dxz = vm_vec_dist(pos, &gpos) / 8.0f;
	gv = &gridp->gmatrix.vec.uvec;
	if (gv->xyz.x * pos->xyz.x + gv->xyz.y * pos->xyz.y + gv->xyz.z * pos->xyz.z < -gridp->planeD)
		gr_set_color(0, 127, 0);
	else
		gr_set_color(192, 192, 192);


	rpd_line(&gpos, pos);	//	Line from grid to object center.

	tpos = gpos;

	vm_vec_scale_add2(&gpos, &gridp->gmatrix.vec.rvec, -dxz / 2);
	vm_vec_scale_add2(&gpos, &gridp->gmatrix.vec.fvec, -dxz / 2);

	vm_vec_scale_add2(&tpos, &gridp->gmatrix.vec.rvec, dxz / 2);
	vm_vec_scale_add2(&tpos, &gridp->gmatrix.vec.fvec, dxz / 2);

	rpd_line(&gpos, &tpos);

	vm_vec_scale_add2(&gpos, &gridp->gmatrix.vec.rvec, dxz);
	vm_vec_scale_add2(&tpos, &gridp->gmatrix.vec.rvec, -dxz);

	rpd_line(&gpos, &tpos);
}

void render_model_x_htl(vec3d *pos, grid *gridp, int col_scheme) {
	vec3d	gpos;	//	Location of point on grid.
	vec3d	tpos;
	float	dxz;
	plane	tplane;
	vec3d	*gv;

	if (!Show_grid_positions)
		return;

	tplane.A = gridp->gmatrix.vec.uvec.xyz.x;
	tplane.B = gridp->gmatrix.vec.uvec.xyz.y;
	tplane.C = gridp->gmatrix.vec.uvec.xyz.z;
	tplane.D = gridp->planeD;

	compute_point_on_plane(&gpos, &tplane, pos);
	dxz = vm_vec_dist(pos, &gpos) / 8.0f;
	gv = &gridp->gmatrix.vec.uvec;
	if (gv->xyz.x * pos->xyz.x + gv->xyz.y * pos->xyz.y + gv->xyz.z * pos->xyz.z < -gridp->planeD)
		gr_set_color(0, 127, 0);
	else
		gr_set_color(192, 192, 192);


	g3_draw_htl_line(&gpos, pos);	//	Line from grid to object center.

	tpos = gpos;

	vm_vec_scale_add2(&gpos, &gridp->gmatrix.vec.rvec, -dxz / 2);
	vm_vec_scale_add2(&gpos, &gridp->gmatrix.vec.fvec, -dxz / 2);

	vm_vec_scale_add2(&tpos, &gridp->gmatrix.vec.rvec, dxz / 2);
	vm_vec_scale_add2(&tpos, &gridp->gmatrix.vec.fvec, dxz / 2);

	g3_draw_htl_line(&gpos, &tpos);

	vm_vec_scale_add2(&gpos, &gridp->gmatrix.vec.rvec, dxz);
	vm_vec_scale_add2(&tpos, &gridp->gmatrix.vec.rvec, -dxz);

	g3_draw_htl_line(&gpos, &tpos);
}

void render_one_model_briefing_screen(object *objp) {
	if (objp->type == OBJ_POINT) {
		if (objp->instance != BRIEFING_LOOKAT_POINT_ID) {
			Assert(Briefing_dialog);
			Briefing_dialog->draw_icon(objp);
			render_model_x_htl(&objp->pos, The_grid);
			render_count++;
		}

	}
}

void render_one_model_htl(object *objp) {
	int z;
	uint debug_flags = 0;
	object *o2;

	Assert(objp->type != OBJ_NONE);

	if (objp->type == OBJ_JUMP_NODE) {
		return;
	}

	if ((objp->type == OBJ_WAYPOINT) && !Show_waypoints)
		return;

	if ((objp->type == OBJ_START) && !Show_starts)
		return;

	if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) {
		if (!Show_ships)
			return;

		if (!Show_iff[Ships[objp->instance].team])
			return;
	}

	if (objp->flags[Object::Object_Flags::Hidden])
		return;

	rendering_order[render_count] = OBJ_INDEX(objp);
	Fred_outline = 0;

	if (!Draw_outlines_on_selected_ships && ((OBJ_INDEX(objp) == cur_object_index) || (objp->flags[Object::Object_Flags::Marked])))
		/* don't draw the outlines we would normally draw */;

	else if ((OBJ_INDEX(objp) == cur_object_index) && !Bg_bitmap_dialog)
		Fred_outline = FRED_COLOUR_WHITE;

	else if ((objp->flags[Object::Object_Flags::Marked]) && !Bg_bitmap_dialog)  // is it a marked object?
		Fred_outline = FRED_COLOUR_YELLOW_GREEN;

	else if ((objp->type == OBJ_SHIP) && Show_outlines) {
		color *iff_color = iff_get_color_by_team_and_object(Ships[objp->instance].team, -1, 1, objp);

		Fred_outline = (iff_color->red << 16) | (iff_color->green << 8) | (iff_color->blue);

	} else if ((objp->type == OBJ_START) && Show_outlines) {
		Fred_outline = 0x007f00;

	} else
		Fred_outline = 0;

	// build flags
	if ((Show_ship_models || Show_outlines) && ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START))) {
		g3_start_instance_matrix(&Eye_position, &Eye_matrix, 0);

		uint64_t flags = MR_NORMAL;

		if (Show_dock_points) {
			debug_flags |= MR_DEBUG_BAY_PATHS;
		}

		if (Show_paths_fred) {
			debug_flags |= MR_DEBUG_PATHS;
		}

		z = objp->instance;

		model_clear_instance(Ship_info[Ships[z].ship_info_index].model_num);

		if (!Lighting_on) {
			flags |= MR_NO_LIGHTING;
		}

		if (FullDetail) {
			flags |= MR_FULL_DETAIL;
		}

		model_render_params render_info;

		render_info.set_debug_flags(debug_flags);
		render_info.set_replacement_textures(model_get_instance(Ships[z].model_instance_num)->texture_replace);

		if (Fred_outline) {
			render_info.set_color(Fred_outline >> 16, (Fred_outline >> 8) & 0xff, Fred_outline & 0xff);
			render_info.set_flags(flags | MR_SHOW_OUTLINE_HTL | MR_NO_LIGHTING | MR_NO_POLYS | MR_NO_TEXTURING);
			model_render_immediate(&render_info, Ship_info[Ships[z].ship_info_index].model_num, Ships[z].model_instance_num, &objp->orient, &objp->pos);
		}

		if (Draw_outline_at_warpin_position
			&& (Ships[z].arrival_cue != Locked_sexp_true || Ships[z].arrival_delay > 0)
			&& Ships[z].arrival_cue != Locked_sexp_false
			&& !Ships[z].flags[Ship::Ship_Flags::No_arrival_warp])
		{
			int warp_type = Warp_params[Ships[z].warpin_params_index].warp_type;
			if (warp_type == WT_DEFAULT || warp_type == WT_KNOSSOS || warp_type == WT_DEFAULT_THEN_KNOSSOS || (warp_type & WT_DEFAULT_WITH_FIREBALL)) {
				float warpin_dist = shipfx_calculate_arrival_warp_distance(objp);

				// project the ship forward as far as it should go
				vec3d warpin_pos;
				vm_vec_scale_add(&warpin_pos, &objp->pos, &objp->orient.vec.fvec, warpin_dist);

				render_info.set_color(65, 65, 65);	// grey; see rgba_defaults
				render_info.set_flags(flags | MR_SHOW_OUTLINE_HTL | MR_NO_LIGHTING | MR_NO_POLYS | MR_NO_TEXTURING);
				model_render_immediate(&render_info, Ship_info[Ships[z].ship_info_index].model_num, Ships[z].model_instance_num, &objp->orient, &warpin_pos);
			}
		}

		g3_done_instance(0);

		if (Show_ship_models) {
			render_info.set_flags(flags);
			model_render_immediate(&render_info, Ship_info[Ships[z].ship_info_index].model_num, Ships[z].model_instance_num, &objp->orient, &objp->pos);
		}
	} else {
		int r = 0, g = 0, b = 0;

		if (objp->type == OBJ_SHIP) {
			if (!Show_ships) {
				return;
			}

			color *iff_color = iff_get_color_by_team_and_object(Ships[objp->instance].team, -1, 1, objp);

			r = iff_color->red;
			g = iff_color->green;
			b = iff_color->blue;

		} else if (objp->type == OBJ_START) {
			r = 0;	g = 127;	b = 0;

		} else if (objp->type == OBJ_WAYPOINT) {
			r = 96;	g = 0;	b = 112;

		} else if (objp->type == OBJ_POINT) {
			if (objp->instance != BRIEFING_LOOKAT_POINT_ID) {
				Assert(Briefing_dialog);
				return;
			}

			r = 196;	g = 32;	b = 196;

		} else
			Assert(0);

		float size = fl_sqrt(vm_vec_dist(&eye_pos, &objp->pos) / 20.0f);

		if (size < LOLLIPOP_SIZE)
			size = LOLLIPOP_SIZE;

		if (Fred_outline) {
			gr_set_color(__min(r * 2, 255), __min(g * 2, 255), __min(b * 2, 255));
			g3_draw_htl_sphere(&objp->pos, size * 1.5f);
		} else {
			gr_set_color(r, g, b);
			g3_draw_htl_sphere(&objp->pos, size);
		}
	}

	if (objp->type == OBJ_WAYPOINT) {
		for (int j = 0; j < render_count; j++) {
			o2 = &Objects[rendering_order[j]];
			if (o2->type == OBJ_WAYPOINT) {
				if ((o2->instance == objp->instance - 1) || (o2->instance == objp->instance + 1)) {
					g3_draw_htl_line(&o2->pos, &objp->pos);
				}
			}
		}
	}

	render_model_x_htl(&objp->pos, The_grid);
	render_count++;
}

void render_waypoints(void) {
	vertex v;

	for (const auto &ii: Waypoint_lists) {
		const vec3d *prev_vec = nullptr;
		for (const auto &jj: ii.get_waypoints()) {
			g3_rotate_vertex(&v, jj.get_pos());
			if (!(v.codes & CC_BEHIND)) {
				if (!(g3_project_vertex(&v) & PF_OVERFLOW)) {
					if (jj.get_objnum() == cur_waypoint->get_objnum())
						gr_set_color(255, 255, 255);
					else if (Objects[jj.get_objnum()].flags[Object::Object_Flags::Marked])
						gr_set_color(160, 255, 0);
					else
						gr_set_color(160, 96, 0);

					g3_draw_sphere(&v, LOLLIPOP_SIZE);
					if (prev_vec == NULL)
						gr_set_color(160, 96, 0);
					else
						gr_set_color(0, 0, 0);

					g3_draw_sphere(&v, LOLLIPOP_SIZE * 0.66667f);
					gr_set_color(160, 96, 0);
					g3_draw_sphere(&v, LOLLIPOP_SIZE * 0.33333f);
				}
			}

			render_model_x(jj.get_pos(), The_grid, 1);

			gr_set_color(160, 96, 0);
			if (prev_vec != NULL)
				rpd_line(prev_vec, jj.get_pos());
			prev_vec = jj.get_pos();
		}
	}
}

int select_object(int cx, int cy) {
	int		best = -1;
	double	dist, best_dist = 9e99;
	vec3d	p0, p1, v, hitpos;
	vertex	vt;
	object *ptr;

	if (Briefing_dialog) {
		best = Briefing_dialog->check_mouse_hit(cx, cy);
		if (best >= 0) {
			if ((Selection_lock && !Objects[best].flags[Object::Object_Flags::Marked]) || Objects[best].flags[Object::Object_Flags::Locked_from_editing]) {
				return -1;
			}
			return best;
		}
	}

	/*	gr_reset_clip();
	g3_start_frame(0); ////////////////
	g3_set_view_matrix(&eye_pos, &eye_orient, 0.5f);*/

	//	Get 3d vector specified by mouse cursor location.
	g3_point_to_vec(&v, cx, cy);

	//	g3_end_frame();
	if (!v.xyz.x && !v.xyz.y && !v.xyz.z)  // zero vector
		return -1;

	p0 = view_pos;
	vm_vec_scale_add(&p1, &p0, &v, 100.0f);

	ptr = GET_FIRST(&obj_used_list);
	while (ptr != END_OF_LIST(&obj_used_list)) {
		if (object_check_collision(ptr, &p0, &p1, &hitpos)) {
			hitpos.xyz.x = ptr->pos.xyz.x - view_pos.xyz.x;
			hitpos.xyz.y = ptr->pos.xyz.y - view_pos.xyz.y;
			hitpos.xyz.z = ptr->pos.xyz.z - view_pos.xyz.z;
			dist = hitpos.xyz.x * hitpos.xyz.x + hitpos.xyz.y * hitpos.xyz.y + hitpos.xyz.z * hitpos.xyz.z;
			if (dist < best_dist) {
				best = OBJ_INDEX(ptr);
				best_dist = dist;
			}
		}

		ptr = GET_NEXT(ptr);
	}

	if (best >= 0) {
		if ((Selection_lock && !Objects[best].flags[Object::Object_Flags::Marked]) || Objects[best].flags[Object::Object_Flags::Locked_from_editing]) {
			return -1;
		}
		return best;
	}
	ptr = GET_FIRST(&obj_used_list);
	while (ptr != END_OF_LIST(&obj_used_list)) {
		g3_rotate_vertex(&vt, &ptr->pos);
		if (!(vt.codes & CC_BEHIND))
			if (!(g3_project_vertex(&vt) & PF_OVERFLOW)) {
				hitpos.xyz.x = vt.screen.xyw.x - cx;
				hitpos.xyz.y = vt.screen.xyw.y - cy;
				dist = hitpos.xyz.x * hitpos.xyz.x + hitpos.xyz.y * hitpos.xyz.y;
				if ((dist < 8) && (dist < best_dist)) {
					best = OBJ_INDEX(ptr);
					best_dist = dist;
				}
			}

		ptr = GET_NEXT(ptr);
	}

	if ((Selection_lock && !Objects[best].flags[Object::Object_Flags::Marked]) || Objects[best].flags[Object::Object_Flags::Locked_from_editing]) {
		return -1;
	}

	return best;
}

void verticalize_controlled() {
	int cmode, count = 0;
	object *objp;

	cmode = Control_mode;
	if ((viewpoint == 1) && !cmode)
		cmode = 2;

	switch (cmode) {
	case 0:		//	Control the viewer's location and orientation
		verticalize_object(&view_orient);
		break;

	case 2:  // Control viewpoint object
		if (!Objects[view_obj].flags[Object::Object_Flags::Locked_from_editing]) {
			verticalize_object(&Objects[view_obj].orient);
			object_moved(&Objects[view_obj]);
			FREDDoc_ptr->autosave("align object");
			set_modified();
		}
		break;

	case 1:  //	Control the current object's location and orientation
		objp = GET_FIRST(&obj_used_list);
		while (objp != END_OF_LIST(&obj_used_list)) {
			if (objp->flags[Object::Object_Flags::Marked])
				verticalize_object(&objp->orient);

			objp = GET_NEXT(objp);
		}

		objp = GET_FIRST(&obj_used_list);
		while (objp != END_OF_LIST(&obj_used_list)) {
			if (objp->flags[Object::Object_Flags::Marked]) {
				object_moved(objp);
				count++;
			}

			objp = GET_NEXT(objp);
		}

		if (count) {
			if (count > 1)
				FREDDoc_ptr->autosave("align objects");
			else
				FREDDoc_ptr->autosave("align object");

			set_modified();
		}

		break;
	}

	return;
}

void verticalize_object(matrix *orient) {
	align_vector_to_axis(&orient->vec.fvec);
	align_vector_to_axis(&orient->vec.uvec);
	align_vector_to_axis(&orient->vec.rvec);
	vm_fix_matrix(orient);  // just in case something odd occurs.
}
