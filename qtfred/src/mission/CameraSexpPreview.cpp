#include "CameraSexpPreview.h"

#include <camera/camera.h>
#include <coordinate_points/coordinate_point.h>
#include <mod_table/mod_table.h>
#include <object/object.h>
#include <object/waypoint.h>
#include <parse/sexp.h>
#include <physics/physics.h>
#include <render/3d.h>
#include <ship/ship.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fso::fred {

namespace {

// sexp_get_set_camera()'s camera. Names are kept lowercase, as camera names match regardless of case
constexpr auto DefaultCameraName = "sexp camera";

// Guards a chain of earlier events that loops or runs very long
constexpr int MaxChainLength = 64;

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

// The argument holding a camera sexp's move time, or -1 if it has none
int timeArgIndex(int op)
{
	switch (op) {
	case OP_CUTSCENES_SET_CAMERA_POSITION:
	case OP_CUTSCENES_SET_CAMERA_ROTATION:
	case OP_CUTSCENES_SET_CAMERA_FACING:
		return 3;
	case OP_CUTSCENES_SET_CAMERA_FACING_OBJECT:
	case OP_CUTSCENES_SET_CAMERA_FOV:
		return 1;
	default:
		return -1;
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

// A move's time, acceleration and deceleration arguments (milliseconds), as eval_nums() reads
// them: missing ones are 0, and with only two given the deceleration is the acceleration
void moveTimes(const SCP_vector<sexp_tree_item>& nodes, const int* args, float& time, float& accel, float& decel)
{
	int v[3] = {0, 0, 0};
	int count = 0;
	for (int i = 0; i < 3; ++i) {
		bool plain = false;
		if (!numberArg(nodes, args[i], v[i], plain))
			break;
		++count;
	}
	time = i2fl(v[0]) / 1000.0f;
	accel = i2fl(v[1]) / 1000.0f;
	decel = i2fl(count == 2 ? v[1] : v[2]) / 1000.0f;
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

// Where a value is when a sexp of the current event runs: all of an event's sexps run at the
// start of its shot, so a move under way hasn't left its start yet
float now(const CameraMove& m)
{
	return (m.time > 0.0f) ? m.start : m.end;
}

// avd_movement::setAVD() from where the value is now; without a time it jumps
void moveTo(CameraMove& m, float target, float time, float accel, float decel)
{
	if (time <= 0.0f) {
		m = CameraMove();
		m.start = m.end = target;
		return;
	}
	m.start = now(m);
	m.end = target;
	m.time = time;
	m.accel = accel;
	m.decel = decel;
}

void settle(CameraMove& m)
{
	m.start = m.end;
	m.time = m.accel = m.decel = 0.0f;
}

// camera::get_info(): where a camera at `local` is
vec3d worldPosition(const CameraTrack& cam, const vec3d& local)
{
	if (!validObject(cam.host))
		return local;
	vec3d pos;
	vm_vec_unrotate(&pos, &local, &Objects[cam.host].orient);
	vm_vec_add2(&pos, &Objects[cam.host].pos);
	return pos;
}

// camera::get_info(): the orientation the camera's rotation is applied to
matrix rotationBase(const CameraTrack& cam, const vec3d& pos)
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

vec3d localNow(const CameraTrack& cam)
{
	vec3d local;
	local.xyz.x = now(cam.pos[0]);
	local.xyz.y = now(cam.pos[1]);
	local.xyz.z = now(cam.pos[2]);
	return local;
}

void rotateTo(CameraTrack& cam, const matrix& m, float time, float accel, float decel)
{
	for (int i = 0; i < 9; ++i)
		moveTo(cam.rot[i], m.a1d[i], time, accel, decel);
}

// camera::set_rotation_facing(): turns toward `point` from where the camera is right now
void faceTowards(CameraTrack& cam, const vec3d& point, float time, float accel, float decel)
{
	const vec3d pos = worldPosition(cam, localNow(cam));
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

	rotateTo(cam, m, time, accel, decel);
	cam.hasAimPoint = true;
	cam.aimPoint = point;
}

int findEvent(const SCP_vector<CameraEventInfo>& events, const char* name)
{
	for (int i = 0; i < static_cast<int>(events.size()); ++i) {
		if (!stricmp(events[i].name.c_str(), name))
			return i;
	}
	return -1;
}

// An event with camera sexps that the formula waits for with is-event-true or its -delay forms
int waitedForCameraEvent(const SCP_vector<sexp_tree_item>& nodes, const SCP_vector<CameraEventInfo>& events,
	int index, int node)
{
	if (!validNode(nodes, node))
		return -1;
	const int op = operatorOf(nodes, node);
	if (op == OP_EVENT_TRUE || op == OP_EVENT_TRUE_DELAY || op == OP_EVENT_TRUE_MSECS_DELAY) {
		const int arg = nodes[node].child;
		if (validNode(nodes, arg) && !(nodes[arg].type & SEXPT_OPERATOR)) {
			const int k = findEvent(events, nodes[arg].text);
			if (k >= 0 && k != index && formulaHasCameraSexps(nodes, events[k].formula))
				return k;
		}
	}
	for (int c = nodes[node].child; validNode(nodes, c); c = nodes[c].next) {
		const int k = waitedForCameraEvent(nodes, events, index, c);
		if (k >= 0)
			return k;
	}
	return -1;
}

// The automatic starts-after event: a chained event's predecessor in the list, else an event its
// formula waits for. Only events with camera sexps count. -1 if none.
int inferStartsAfter(const SCP_vector<sexp_tree_item>& nodes, const SCP_vector<CameraEventInfo>& events, int index)
{
	if (events[index].chained && index > 0 && formulaHasCameraSexps(nodes, events[index - 1].formula))
		return index - 1;
	return waitedForCameraEvent(nodes, events, index, events[index].formula);
}

struct StartsAfter {
	int index = -1;
	bool automatic = true;
	bool missing = false;
};

// The chosen starts-after event if it is usable, else the automatic one
StartsAfter resolveStartsAfter(const SCP_vector<sexp_tree_item>& nodes, const SCP_vector<CameraEventInfo>& events, int index)
{
	StartsAfter result;
	const auto& chosen = events[index].startsAfter;
	if (!chosen.empty()) {
		if (!stricmp(chosen.c_str(), SEXP_NONE_STRING)) {
			result.automatic = false;
			return result;
		}
		const int k = findEvent(events, chosen.c_str());
		if (k >= 0 && k != index && formulaHasCameraSexps(nodes, events[k].formula)) {
			result.index = k;
			result.automatic = false;
			return result;
		}
		result.missing = true;
	}
	result.index = inferStartsAfter(nodes, events, index);
	return result;
}

class CameraRun {
  public:
	explicit CameraRun(const SCP_vector<sexp_tree_item>& nodes) : _nodes(&nodes) {}

	// Recorded as the selected sexp runs: which camera it works on, and where that camera is
	int selected = -1;
	SCP_string selectedCamera;
	vec3d selectedFrom = vmd_zero_vector;

	CameraTrack& camera(const SCP_string& name)
	{
		auto it = _cameras.find(name);
		if (it == _cameras.end()) {
			CameraTrack fresh;
			for (int i = 0; i < 9; ++i)
				fresh.rot[i].start = fresh.rot[i].end = vmd_identity_matrix.a1d[i];
			fresh.fov.start = fresh.fov.end = g3_get_hfov(VIEWER_ZOOM_DEFAULT);
			it = _cameras.emplace(name, fresh).first;
		}
		return it->second;
	}
	CameraTrack& camera() { return camera(_current); }

	// Runs every camera sexp under `root` in tree order, the order the actions run in
	void runFormula(int root) { visit(root); }

	// Every move done, as if the event's shot played out before the next event
	void settleAll()
	{
		for (auto& entry : _cameras) {
			for (auto& m : entry.second.pos)
				settle(m);
			for (auto& m : entry.second.rot)
				settle(m);
			settle(entry.second.fov);
		}
	}

  private:
	const SCP_vector<sexp_tree_item>* _nodes;
	SCP_unordered_map<SCP_string, CameraTrack> _cameras;
	SCP_string _current = DefaultCameraName;

	void visit(int node)
	{
		const auto& nodes = *_nodes;
		if (!validNode(nodes, node))
			return;

		const int op = operatorOf(nodes, node);
		if (op >= 0 && isCameraOp(op)) {
			if (node == selected) {
				selectedCamera = _current;
				selectedFrom = worldPosition(camera(), localNow(camera()));
			}
			apply(node, op);
			if (node == selected && op == OP_CUTSCENES_SET_CAMERA)
				selectedCamera = _current; // it selects the camera it names
		}

		for (int c = nodes[node].child; validNode(nodes, c); c = nodes[c].next)
			visit(c);
	}

	void apply(int node, int op)
	{
		const auto& nodes = *_nodes;
		int args[6];
		argNodes(nodes, node, args, 6);
		int values[3];
		bool plain = false;
		float time = 0.0f;
		float accel = 0.0f;
		float decel = 0.0f;

		switch (op) {
		case OP_CUTSCENES_SET_CAMERA:
			// A named camera keeps its own state; no name goes back to the sexp camera
			if (validNode(nodes, args[0]) && !(nodes[args[0]].type & SEXPT_OPERATOR)) {
				_current = nodes[args[0]].text;
				SCP_tolower(_current);
			} else {
				_current = DefaultCameraName;
			}
			break;

		case OP_CUTSCENES_SET_CAMERA_POSITION:
			if (numberArgs3(nodes, args, values, plain)) {
				moveTimes(nodes, args + 3, time, accel, decel);
				auto& cam = camera();
				for (int i = 0; i < 3; ++i)
					moveTo(cam.pos[i], i2fl(values[i]), time, accel, decel);
				// The camera keeps its rotation, so it no longer looks at what it faced
				if (!validObject(cam.target))
					cam.hasAimPoint = false;
			}
			break;

		case OP_CUTSCENES_SET_CAMERA_ROTATION:
			if (numberArgs3(nodes, args, values, plain)) {
				moveTimes(nodes, args + 3, time, accel, decel);
				// eval_angles(): pitch, bank, heading in degrees
				angles a;
				a.p = fl_radians(values[0] % 360);
				a.b = fl_radians(values[1] % 360);
				a.h = fl_radians(values[2] % 360);
				matrix m;
				vm_angles_2_matrix(&m, &a);
				auto& cam = camera();
				rotateTo(cam, m, time, accel, decel);
				cam.hasAimPoint = false;
			}
			break;

		case OP_CUTSCENES_SET_CAMERA_FACING:
			if (numberArgs3(nodes, args, values, plain)) {
				moveTimes(nodes, args + 3, time, accel, decel);
				vec3d point;
				point.xyz.x = i2fl(values[0]);
				point.xyz.y = i2fl(values[1]);
				point.xyz.z = i2fl(values[2]);
				faceTowards(camera(), point, time, accel, decel);
			}
			break;

		case OP_CUTSCENES_SET_CAMERA_FACING_OBJECT: {
			const int obj = objectArg(nodes, args[0]);
			if (obj >= 0) {
				moveTimes(nodes, args + 1, time, accel, decel);
				faceTowards(camera(), Objects[obj].pos, time, accel, decel);
			}
			break;
		}

		case OP_CUTSCENES_SET_CAMERA_HOST:
			camera().host = objectArg(nodes, args[0]);
			break;

		case OP_CUTSCENES_SET_CAMERA_TARGET:
			camera().target = objectArg(nodes, args[0]);
			break;

		case OP_CUTSCENES_SET_CAMERA_FOV:
			if (numberArg(nodes, args[0], values[0], plain)) {
				moveTimes(nodes, args + 1, time, accel, decel);
				moveTo(camera().fov, fl_radians(values[0] % 360), time, accel, decel);
			}
			break;

		default:
			break;
		}
	}
};

// A world point as set-camera-position arguments: relative to the host, if there is one
void positionToArgs(const CameraSexpPreview& preview, const vec3d& world, int out[3])
{
	vec3d p = world;
	if (validObject(preview.positionHost)) {
		const object& host = Objects[preview.positionHost];
		vec3d offset;
		vm_vec_sub(&offset, &world, &host.pos);
		vm_vec_rotate(&p, &offset, &host.orient);
	}
	out[0] = static_cast<int>(std::lround(p.xyz.x));
	out[1] = static_cast<int>(std::lround(p.xyz.y));
	out[2] = static_cast<int>(std::lround(p.xyz.z));
}

} // namespace

float CameraMove::at(float t) const
{
	if (t < 0.0f || time <= 0.0f)
		return end;
	avd_movement m;
	m.set(start);
	m.setAVD(end, time, accel, decel, 0.0f);
	float value = end;
	m.get(t, &value, nullptr);
	return value;
}

float CameraTrack::duration() const
{
	float d = fov.time;
	for (const auto& m : pos)
		d = std::max(d, m.time);
	for (const auto& m : rot)
		d = std::max(d, m.time);
	return d;
}

CameraShot CameraTrack::at(float t) const
{
	CameraShot shot;
	vec3d local;
	local.xyz.x = pos[0].at(t);
	local.xyz.y = pos[1].at(t);
	local.xyz.z = pos[2].at(t);
	shot.pos = worldPosition(*this, local);

	matrix rotation;
	for (int i = 0; i < 9; ++i)
		rotation.a1d[i] = rot[i].at(t);
	const matrix base = rotationBase(*this, shot.pos);
	vm_matrix_x_matrix(&shot.orient, &base, &rotation);
	vm_orthogonalize_matrix(&shot.orient);

	shot.fov = fov.at(t);
	shot.hostObj = validObject(host) ? host : -1;
	shot.targetObj = validObject(target) ? target : -1;
	if (shot.targetObj >= 0) {
		shot.hasAimPoint = true;
		shot.aimPoint = Objects[shot.targetObj].pos;
	} else {
		shot.hasAimPoint = hasAimPoint;
		shot.aimPoint = aimPoint;
	}
	return shot;
}

int findCameraOperator(const SCP_vector<sexp_tree_item>& nodes, int node)
{
	for (int i = node; validNode(nodes, i); i = nodes[i].parent) {
		const int op = operatorOf(nodes, i);
		if (op >= 0 && isCameraOp(op))
			return i;
	}
	return -1;
}

bool formulaHasCameraSexps(const SCP_vector<sexp_tree_item>& nodes, int formula)
{
	if (!validNode(nodes, formula))
		return false;
	const int op = operatorOf(nodes, formula);
	if (op >= 0 && isCameraOp(op))
		return true;
	for (int c = nodes[formula].child; validNode(nodes, c); c = nodes[c].next) {
		if (formulaHasCameraSexps(nodes, c))
			return true;
	}
	return false;
}

bool previewCameraSexp(const SCP_vector<sexp_tree_item>& nodes, int opNode, CameraSexpPreview* out,
	const SCP_vector<CameraEventInfo>* events)
{
	const int op = operatorOf(nodes, opNode);
	if (op < 0 || !isCameraOp(op))
		return false;

	int root = opNode;
	while (validNode(nodes, nodes[root].parent))
		root = nodes[root].parent;

	CameraSexpPreview result;
	result.op = op;
	result.opNode = opNode;

	CameraRun run(nodes);

	// Start where the shots this one starts after ended, oldest first, each finished
	if (events != nullptr) {
		for (int i = 0; i < static_cast<int>(events->size()); ++i) {
			if ((*events)[i].formula == root) {
				result.eventIndex = i;
				break;
			}
		}
	}
	if (result.eventIndex >= 0) {
		const auto start = resolveStartsAfter(nodes, *events, result.eventIndex);
		result.startsAfterEvent = start.index;
		result.startsAfterAutomatic = start.automatic;
		result.startsAfterMissing = start.missing;
		result.inferredStartsAfter = inferStartsAfter(nodes, *events, result.eventIndex);

		SCP_vector<int> chain;
		SCP_vector<bool> seen(events->size(), false);
		seen[result.eventIndex] = true;
		for (int k = start.index; k >= 0 && !seen[k] && static_cast<int>(chain.size()) < MaxChainLength;
			 k = resolveStartsAfter(nodes, *events, k).index) {
			seen[k] = true;
			chain.push_back(k);
		}
		for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
			run.runFormula((*events)[*it].formula);
			run.settleAll();
		}
	}

	// The selected sexp's event as one shot
	run.selected = opNode;
	run.runFormula(root);
	const auto& track = run.camera(run.selectedCamera);
	result.play = track;
	result.duration = track.duration();

	// A timed sexp shows the end of the shot, one without a time its start
	int args[6];
	argNodes(nodes, opNode, args, 6);
	const int timeArg = timeArgIndex(op);
	if (timeArg >= 0) {
		float time = 0.0f;
		float accel = 0.0f;
		float decel = 0.0f;
		moveTimes(nodes, args + timeArg, time, accel, decel);
		result.atEnd = time > 0.0f;
	}
	result.shot = track.at(result.atEnd ? -1.0f : 0.0f);
	result.rotationBase = rotationBase(track, result.shot.pos);
	result.positionHost = result.shot.hostObj;
	result.facingFrom = run.selectedFrom;

	int values[3];
	bool plain = false;
	switch (op) {
	case OP_CUTSCENES_SET_CAMERA_POSITION:
	case OP_CUTSCENES_SET_CAMERA_FACING:
		if (numberArgs3(nodes, args, values, plain)) {
			vec3d p;
			p.xyz.x = i2fl(values[0]);
			p.xyz.y = i2fl(values[1]);
			p.xyz.z = i2fl(values[2]);
			result.hasPoint = true;
			result.point = (op == OP_CUTSCENES_SET_CAMERA_POSITION) ? worldPosition(track, p) : p;
			if (plain) {
				for (int i = 0; i < 3; ++i)
					result.pointNodes[i] = args[i];
			}
		}
		break;
	case OP_CUTSCENES_SET_CAMERA_ROTATION:
		if (numberArgs3(nodes, args, values, plain) && plain) {
			for (int i = 0; i < 3; ++i)
				result.angleNodes[i] = args[i];
		}
		break;
	case OP_CUTSCENES_SET_CAMERA_FOV:
		if (numberArg(nodes, args[0], values[0], plain) && plain)
			result.fovNode = args[0];
		break;
	default:
		break;
	}

	*out = result;
	return true;
}

bool cameraPointArgs(const CameraSexpPreview& preview, const vec3d& world, int out[3])
{
	if (preview.pointNodes[0] < 0)
		return false;
	if (preview.op == OP_CUTSCENES_SET_CAMERA_POSITION) {
		positionToArgs(preview, world, out);
	} else {
		out[0] = static_cast<int>(std::lround(world.xyz.x));
		out[1] = static_cast<int>(std::lround(world.xyz.y));
		out[2] = static_cast<int>(std::lround(world.xyz.z));
	}
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

bool cameraFacingArgs(const CameraSexpPreview& preview, const matrix& orient, int out[3])
{
	if (preview.op != OP_CUTSCENES_SET_CAMERA_FACING || preview.pointNodes[0] < 0)
		return false;
	const float depth = std::max(vm_vec_dist(&preview.facingFrom, &preview.point), 100.0f);
	vec3d point;
	vm_vec_scale_add(&point, &preview.facingFrom, &orient.vec.fvec, depth);
	return cameraPointArgs(preview, point, out);
}

bool cameraFovArg(const CameraSexpPreview& preview, float fov, int* out)
{
	if (preview.fovNode < 0)
		return false;
	*out = static_cast<int>(std::lround(fl_degrees(fov)));
	return true;
}

} // namespace fso::fred
