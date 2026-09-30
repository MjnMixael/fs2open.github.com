#pragma once

#include "missioneditor/sexp_tree_model.h"

#include <globalincs/pstypes.h>
#include <math/vecmat.h>

namespace fso::fred {

// Where a cutscene camera is and what it shows at some moment: the editor's stand-in for
// the engine's camera (camera/camera.cpp), worked out from a sexp tree without running it.
struct CameraShot {
	vec3d pos = vmd_zero_vector;
	matrix orient = vmd_identity_matrix;
	float fov = 0.0f; // engine zoom units, like EditorViewport::viewFov()
	int hostObj = -1; // Objects[] index, or -1
	int targetObj = -1;
	bool hasAimPoint = false; // what set-camera-facing(-object) or a target turned it to
	vec3d aimPoint = vmd_zero_vector;
};

// One value moving the way the engine's avd_movement moves it: from start to end over time
// seconds, easing in over accel and out over decel. A time of 0 is a value that isn't moving.
struct CameraMove {
	float start = 0.0f;
	float end = 0.0f;
	float time = 0.0f;
	float accel = 0.0f;
	float decel = 0.0f;

	float at(float t) const;
};

// A camera as the engine keeps it: position relative to a host, a rotation applied on top of
// a target's facing or the host's orientation, and a field of view, each a move per value so
// a shot can be played.
struct CameraTrack {
	CameraMove pos[3];
	CameraMove rot[9];
	CameraMove fov;
	int host = -1;
	int target = -1;
	bool hasAimPoint = false;
	vec3d aimPoint = vmd_zero_vector;

	// Seconds until every move has finished
	float duration() const;
	// The camera t seconds into the shot; a negative t is its end
	CameraShot at(float t) const;
};

// An event, for carrying a camera on from earlier events. Supplied by a dialog whose tree
// holds events (the Events editor).
struct CameraEventInfo {
	SCP_string name;
	int formula = -1;        // root tree node
	bool chained = false;    // runs after the event before it in the list
	SCP_string startsAfter;  // the saved choice: empty = automatic, SEXP_NONE_STRING = new camera
	bool hasCameraSexps = false; // filled in by the sexp tree
};

// Lets the preview choose an event's starts-after event
class CameraEventSource {
  public:
	virtual ~CameraEventSource() = default;
	virtual SCP_vector<CameraEventInfo> cameraEventInfo() const = 0;
	virtual void setCameraStartsAfter(int eventIndex, const SCP_string& name) = 0;
};

// A selected camera sexp: its operator, the shot once it has run, the whole event's shot for
// playing, and the arguments the editor can write.
struct CameraSexpPreview {
	int op = -1;     // OP_CUTSCENES_... constant
	int opNode = -1; // tree_nodes[] index of the operator
	CameraShot shot; // at the end of the selected sexp's moves

	// Every camera sexp of the selected event runs at once, as in the game; this is that shot
	CameraTrack play;
	float duration = 0.0f;

	// The event the selected sexp is in, and the one its camera carries on from, when the tree
	// has events. startsAfterAutomatic: worked out rather than chosen. startsAfterMissing: the
	// chosen event doesn't exist or has no camera sexps, so it was worked out instead.
	int eventIndex = -1;
	int startsAfterEvent = -1;
	int inferredStartsAfter = -1; // what automatic would pick
	bool startsAfterAutomatic = true;
	bool startsAfterMissing = false;

	// The selected operator's own point (the set-camera-position position or the
	// set-camera-facing point), in world space, for its drag handle. pointNodes are set when
	// its three arguments are plain numbers, so they can be written.
	bool hasPoint = false;
	vec3d point = vmd_zero_vector;
	int pointNodes[3] = {-1, -1, -1};

	// The event's sexps that place the camera (Set from View, flying the camera): its
	// set-camera-position, and its set-camera-rotation or set-camera-facing. -1 when there
	// is none with plain number arguments.
	int positionNodes[3] = {-1, -1, -1};
	int rotationNodes[3] = {-1, -1, -1};
	bool rotationIsFacing = false;
	vec3d facingPoint = vmd_zero_vector;

	// The orientation the rotation is applied on top of, and the host the position is
	// relative to
	matrix rotationBase = vmd_identity_matrix;
	int positionHost = -1;
};

// The nearest camera operator at or above `node` (a node inside one selects it), or -1.
int findCameraOperator(const SCP_vector<sexp_tree_item>& nodes, int node);

// Whether an event's formula has any camera sexps
bool formulaHasCameraSexps(const SCP_vector<sexp_tree_item>& nodes, int formula);

// The event an event's camera carries on from, worked out: a chained event carries on from
// the event before it, and otherwise from an event its formula waits for (is-event-true and
// its -delay forms). Only events with camera sexps count. -1 if none.
int inferCameraStartsAfter(const SCP_vector<sexp_tree_item>& nodes, const SCP_vector<CameraEventInfo>& events, int index);

// Runs the camera sexps that lead up to `opNode`: those of the events its event carries on
// from, then its own up to and including it. Without events, its formula starts from a new
// camera.
bool previewCameraSexp(const SCP_vector<sexp_tree_item>& nodes, int opNode, CameraSexpPreview* out,
	const SCP_vector<CameraEventInfo>* events = nullptr);

// The argument values that put the selected operator's point at `world`. False if it has no
// writable point.
bool cameraPointArgs(const CameraSexpPreview& preview, const vec3d& world, int out[3]);

// The event's set-camera-position arguments for a camera at `world` (relative to a host).
bool cameraPositionArgs(const CameraSexpPreview& preview, const vec3d& world, int out[3]);

// The event's set-camera-rotation arguments (degrees) for a camera turned to `orient`, or its
// set-camera-facing point for one at `eye` looking along it.
bool cameraRotationArgs(const CameraSexpPreview& preview, const vec3d& eye, const matrix& orient, int out[3]);

} // namespace fso::fred
