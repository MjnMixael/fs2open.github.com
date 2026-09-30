#include "CameraSexpPreview.h"

#include <camera/camera.h>
#include <coordinate_points/coordinate_point.h>
#include <mod_table/mod_table.h>
#include <object/object.h>
#include <object/waypoint.h>
#include <parse/sexp.h>
#include <render/3d.h>
#include <ship/ship.h>

#include <cmath>
#include <cstdlib>

namespace fso::fred {

namespace {

// One camera's state as the engine keeps it: the position is relative to the host, and the
// rotation is applied on top of the target's facing or the host's orientation.
struct CameraState {
	vec3d localPos = vmd_zero_vector;
	matrix rotation = vmd_identity_matrix;
	int host = -1;
	int target = -1;
	float fov = 0.0f;
	bool hasAimPoint = false;
	vec3d aimPoint = vmd_zero_vector;
};

// sexp_get_set_camera()'s camera. Names are kept lowercase, as camera names match regardless of case
constexpr auto DefaultCameraName = "sexp camera";

bool isCameraOp(int op)
{
	switch (op) {
	case OP_CUTSCENES_SET_CAMERA:
	case OP_CUTSCENES_SET_CAMERA_POSITION:
	case OP_CUTSCENES_SET_CAMERA_ROTATION:
	case OP_CUTSCENES_SET_CAMERA_FACING:
	case OP_CUTSCENES_SET_CAMERA_FACING_OBJECT:
	case OP_CUTSCENES_SET_CAMERA_HOST:
	case OP_CUTSCENES_SET_CAMERA_TARGET:
	case OP_CUTSCENES_SET_CAMERA_FOV:
		return true;
	default:
		return false;
	}
}

bool validNode(const SCP_vector<sexp_tree_item>& nodes, int node)
{
	return node >= 0 && node < static_cast<int>(nodes.size()) && nodes[node].type != SEXPT_UNUSED;
}

int operatorOf(const SCP_vector<sexp_tree_item>& nodes, int node)
{
	if (!validNode(nodes, node) || !(nodes[node].type & SEXPT_OPERATOR))
		return -1;
	return get_operator_const(nodes[node].text);
}

// The first `count` argument nodes of an operator; missing ones are -1
void argNodes(const SCP_vector<sexp_tree_item>& nodes, int op, int* out, int count)
{
	int c = validNode(nodes, op) ? nodes[op].child : -1;
	for (int i = 0; i < count; ++i) {
		out[i] = validNode(nodes, c) ? c : -1;
		if (out[i] >= 0)
			c = nodes[c].next;
	}
}

// A number argument's value: a plain number, or a number variable's initial value (its node
// reads "name(value)"). `plain` says whether it can be written back.
bool numberArg(const SCP_vector<sexp_tree_item>& nodes, int node, int& value, bool& plain)
{
	if (!validNode(nodes, node))
		return false;
	const int type = nodes[node].type;
	if ((type & (SEXPT_OPERATOR | SEXPT_CONTAINER_NAME | SEXPT_CONTAINER_DATA | SEXPT_MODIFIER)) ||
		!(type & SEXPT_NUMBER))
		return false;

	const char* text = nodes[node].text;
	plain = !(type & SEXPT_VARIABLE);
	if (!plain) {
		text = strrchr(text, '(');
		if (text == nullptr)
			return false;
		++text;
	}
	value = atoi(text);
	return true;
}

// Three number arguments, and whether all of them are plain numbers
bool numberArgs3(const SCP_vector<sexp_tree_item>& nodes, const int* args, int* values, bool& plain)
{
	plain = true;
	for (int i = 0; i < 3; ++i) {
		bool p = false;
		if (!numberArg(nodes, args[i], values[i], p))
			return false;
		plain = plain && p;
	}
	return true;
}

bool validObject(int objnum)
{
	return objnum >= 0 && objnum < MAX_OBJECTS && Objects[objnum].type != OBJ_NONE;
}

// An object_ship_wing_point_team argument, as eval_object_ship_wing_point_team() finds it
int resolveObject(const char* name)
{
	const int ship = ship_name_lookup(name, 1);
	if (ship >= 0)
		return Ships[ship].objnum;

	const int wing = wing_name_lookup(name, 1);
	if (wing >= 0) {
		const auto& w = Wings[wing];
		if (w.special_ship >= 0 && w.ship_index[w.special_ship] >= 0)
			return Ships[w.ship_index[w.special_ship]].objnum;
		return -1;
	}

	if (auto* wp = find_matching_waypoint(name))
		return wp->get_objnum();
	if (auto* cp = find_coordinate_point_by_name(name))
		return cp->objnum;
	return -1;
}

int objectArg(const SCP_vector<sexp_tree_item>& nodes, int node)
{
	if (!validNode(nodes, node) || (nodes[node].type & SEXPT_OPERATOR))
		return -1;
	const int objnum = resolveObject(nodes[node].text);
	return validObject(objnum) ? objnum : -1;
}

// camera::get_info(): where the camera is
vec3d worldPosition(const CameraState& cam)
{
	if (!validObject(cam.host))
		return cam.localPos;
	vec3d pos;
	vm_vec_unrotate(&pos, &cam.localPos, &Objects[cam.host].orient);
	vm_vec_add2(&pos, &Objects[cam.host].pos);
	return pos;
}

// camera::get_info(): the orientation the camera's rotation is applied to
matrix rotationBase(const CameraState& cam, const vec3d& pos)
{
	if (validObject(cam.target)) {
		const vec3d& target = Objects[cam.target].pos;
		if (vm_vec_dist(&target, &pos) > 0.001f) {
			vec3d dir;
			vm_vec_normalized_dir(&dir, &target, &pos);
			matrix m;
			vm_vector_2_matrix_norm(&m, &dir, nullptr, nullptr);
			return m;
		}
	}
	if (validObject(cam.host))
		return Objects[cam.host].orient;
	return vmd_identity_matrix;
}

// camera::set_rotation_facing()
void faceTowards(CameraState& cam, const vec3d& point)
{
	const vec3d pos = worldPosition(cam);
	if (vm_vec_same(&point, &pos))
		return; // the engine refuses ("Camera tried to point to self")

	vec3d dir;
	vm_vec_normalized_dir(&dir, &point, &pos);

	matrix m;
	if (Use_host_orientation_for_set_camera_facing) {
		matrix base = rotationBase(cam, pos);
		vm_vector_2_matrix_norm(&m, &dir, &base.vec.uvec, nullptr);
		if (validObject(cam.host)) {
			vm_transpose(&base);
			m = m * base;
		}
	} else {
		vm_vector_2_matrix_norm(&m, &dir, nullptr, nullptr);
	}

	cam.rotation = m;
	cam.hasAimPoint = true;
	cam.aimPoint = point;
}

class CameraRun {
  public:
	CameraRun(const SCP_vector<sexp_tree_item>& nodes, int selectedOp, CameraSexpPreview* out)
		: _nodes(nodes), _selectedOp(selectedOp), _out(out)
	{
	}

	bool run(int root)
	{
		visit(root);
		return _found;
	}

  private:
	const SCP_vector<sexp_tree_item>& _nodes;
	int _selectedOp;
	CameraSexpPreview* _out;
	bool _found = false;

	SCP_unordered_map<SCP_string, CameraState> _cameras;
	SCP_string _current = DefaultCameraName;

	CameraState& camera()
	{
		auto it = _cameras.find(_current);
		if (it == _cameras.end()) {
			CameraState fresh;
			fresh.fov = g3_get_hfov(VIEWER_ZOOM_DEFAULT);
			it = _cameras.emplace(_current, fresh).first;
		}
		return it->second;
	}

	// Depth first in tree order, which is the order the actions run in
	void visit(int node)
	{
		if (_found || !validNode(_nodes, node))
			return;

		const int op = operatorOf(_nodes, node);
		if (op >= 0 && isCameraOp(op)) {
			apply(node, op);
			if (node == _selectedOp) {
				finish(node, op);
				return;
			}
		}

		for (int c = _nodes[node].child; validNode(_nodes, c) && !_found; c = _nodes[c].next)
			visit(c);
	}

	void apply(int node, int op)
	{
		int args[3];
		argNodes(_nodes, node, args, 3);
		int values[3];
		bool plain = false;

		switch (op) {
		case OP_CUTSCENES_SET_CAMERA:
			// A named camera keeps its own state; no name goes back to the sexp camera
			if (validNode(_nodes, args[0]) && !(_nodes[args[0]].type & SEXPT_OPERATOR)) {
				_current = _nodes[args[0]].text;
				SCP_tolower(_current);
			} else {
				_current = DefaultCameraName;
			}
			break;

		case OP_CUTSCENES_SET_CAMERA_POSITION:
			if (numberArgs3(_nodes, args, values, plain)) {
				auto& cam = camera();
				cam.localPos.xyz.x = i2fl(values[0]);
				cam.localPos.xyz.y = i2fl(values[1]);
				cam.localPos.xyz.z = i2fl(values[2]);
				// The camera keeps its rotation, so it no longer looks at what it faced
				if (!validObject(cam.target))
					cam.hasAimPoint = false;
			}
			break;

		case OP_CUTSCENES_SET_CAMERA_ROTATION:
			if (numberArgs3(_nodes, args, values, plain)) {
				// eval_angles(): pitch, bank, heading in degrees
				angles a;
				a.p = fl_radians(values[0] % 360);
				a.b = fl_radians(values[1] % 360);
				a.h = fl_radians(values[2] % 360);
				auto& cam = camera();
				vm_angles_2_matrix(&cam.rotation, &a);
				cam.hasAimPoint = false;
			}
			break;

		case OP_CUTSCENES_SET_CAMERA_FACING:
			if (numberArgs3(_nodes, args, values, plain)) {
				vec3d point;
				point.xyz.x = i2fl(values[0]);
				point.xyz.y = i2fl(values[1]);
				point.xyz.z = i2fl(values[2]);
				faceTowards(camera(), point);
			}
			break;

		case OP_CUTSCENES_SET_CAMERA_FACING_OBJECT: {
			const int obj = objectArg(_nodes, args[0]);
			if (obj >= 0)
				faceTowards(camera(), Objects[obj].pos);
			break;
		}

		case OP_CUTSCENES_SET_CAMERA_HOST:
			camera().host = objectArg(_nodes, args[0]);
			break;

		case OP_CUTSCENES_SET_CAMERA_TARGET:
			camera().target = objectArg(_nodes, args[0]);
			break;

		case OP_CUTSCENES_SET_CAMERA_FOV: {
			bool p = false;
			if (numberArg(_nodes, args[0], values[0], p))
				camera().fov = fl_radians(values[0] % 360);
			break;
		}

		default:
			break;
		}
	}

	void finish(int node, int op)
	{
		_found = true;
		auto& cam = camera();

		auto& out = *_out;
		out = CameraSexpPreview();
		out.op = op;
		out.opNode = node;

		auto& shot = out.shot;
		shot.pos = worldPosition(cam);
		out.rotationBase = rotationBase(cam, shot.pos);
		vm_matrix_x_matrix(&shot.orient, &out.rotationBase, &cam.rotation);
		vm_orthogonalize_matrix(&shot.orient);
		shot.fov = cam.fov;
		shot.hostObj = validObject(cam.host) ? cam.host : -1;
		shot.targetObj = validObject(cam.target) ? cam.target : -1;
		if (shot.targetObj >= 0) {
			shot.hasAimPoint = true;
			shot.aimPoint = Objects[shot.targetObj].pos;
		} else {
			shot.hasAimPoint = cam.hasAimPoint;
			shot.aimPoint = cam.aimPoint;
		}
		out.positionHost = shot.hostObj;

		int args[3];
		argNodes(_nodes, node, args, 3);
		int values[3];
		bool plain = false;
		if (op == OP_CUTSCENES_SET_CAMERA_POSITION || op == OP_CUTSCENES_SET_CAMERA_FACING) {
			if (numberArgs3(_nodes, args, values, plain)) {
				out.hasPoint = true;
				if (op == OP_CUTSCENES_SET_CAMERA_POSITION) {
					out.point = shot.pos;
				} else {
					out.point.xyz.x = i2fl(values[0]);
					out.point.xyz.y = i2fl(values[1]);
					out.point.xyz.z = i2fl(values[2]);
				}
				if (plain) {
					for (int i = 0; i < 3; ++i)
						out.pointNodes[i] = args[i];
				}
			}
		} else if (op == OP_CUTSCENES_SET_CAMERA_ROTATION) {
			if (numberArgs3(_nodes, args, values, plain) && plain) {
				for (int i = 0; i < 3; ++i)
					out.angleNodes[i] = args[i];
			}
		}
	}
};

} // namespace

int findCameraOperator(const SCP_vector<sexp_tree_item>& nodes, int node)
{
	for (int i = node; validNode(nodes, i); i = nodes[i].parent) {
		const int op = operatorOf(nodes, i);
		if (op >= 0 && isCameraOp(op))
			return i;
	}
	return -1;
}

bool previewCameraSexp(const SCP_vector<sexp_tree_item>& nodes, int opNode, CameraSexpPreview* out)
{
	const int op = operatorOf(nodes, opNode);
	if (op < 0 || !isCameraOp(op))
		return false;

	int root = opNode;
	while (validNode(nodes, nodes[root].parent))
		root = nodes[root].parent;

	return CameraRun(nodes, opNode, out).run(root);
}

bool cameraPointArgs(const CameraSexpPreview& preview, const vec3d& world, int out[3])
{
	if (preview.pointNodes[0] < 0)
		return false;

	vec3d p = world;
	if (preview.op == OP_CUTSCENES_SET_CAMERA_POSITION && validObject(preview.positionHost)) {
		const object& host = Objects[preview.positionHost];
		vec3d offset;
		vm_vec_sub(&offset, &world, &host.pos);
		vm_vec_rotate(&p, &offset, &host.orient);
	}

	out[0] = static_cast<int>(std::lround(p.xyz.x));
	out[1] = static_cast<int>(std::lround(p.xyz.y));
	out[2] = static_cast<int>(std::lround(p.xyz.z));
	return true;
}

bool cameraRotationArgs(const CameraSexpPreview& preview, const matrix& orient, int out[3])
{
	if (preview.op != OP_CUTSCENES_SET_CAMERA_ROTATION || preview.angleNodes[0] < 0)
		return false;

	// The shot is base * rotation, so the rotation is base^T * orient
	matrix baseT = preview.rotationBase;
	vm_transpose(&baseT);
	matrix rotation;
	vm_matrix_x_matrix(&rotation, &baseT, &orient);
	vm_orthogonalize_matrix(&rotation);

	angles a;
	vm_extract_angles_matrix(&a, &rotation);
	out[0] = static_cast<int>(std::lround(fl_degrees(a.p)));
	out[1] = static_cast<int>(std::lround(fl_degrees(a.b)));
	out[2] = static_cast<int>(std::lround(fl_degrees(a.h)));
	return true;
}

} // namespace fso::fred
