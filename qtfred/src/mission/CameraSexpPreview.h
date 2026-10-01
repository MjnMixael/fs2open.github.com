#pragma once

#include "missioneditor/sexp_tree_model.h"

#include <globalincs/pstypes.h>
#include <math/vecmat.h>

namespace fso::fred {

// Where a cutscene camera is and what it shows: the editor's preview of the engine's camera
// (camera/camera.cpp), worked out from a sexp tree without running it. The rules it follows
// are in the Cutscene Camera Preview help page.
struct CameraShot {
	vec3d pos = vmd_zero_vector;
	matrix orient = vmd_identity_matrix;
	float fov = 0.0f; // engine zoom units, like EditorViewport::viewFov()
	int hostObj = -1; // Objects[] index, or -1
	int targetObj = -1;
	bool hasAimPoint = false; // what set-camera-facing(-object) or a target turned it to
	vec3d aimPoint = vmd_zero_vector;
};

// One value of the camera over a shot, as the engine's avd_movement moves it: from start to end
// over time seconds, easing in over accel and out over decel. A time of 0 doesn't move.
struct CameraMove {
	float start = 0.0f;
	float end = 0.0f;
	float time = 0.0f;
	float accel = 0.0f;
	float decel = 0.0f;

	float at(float t) const; // a negative t is the end
};

// A camera over a shot: position relative to a host, a rotation applied on top of a target's
// facing or the host's orientation, and a field of view
struct CameraTrack {
	CameraMove pos[3];
	CameraMove rot[9];
	CameraMove fov;
	int host = -1;
	int target = -1;
	// What it is turned to face, at the end of the shot and at its start (a timed facing only
	// changes the end)
	bool hasAimPoint = false;
	vec3d aimPoint = vmd_zero_vector;
	bool hasStartAim = false;
	vec3d startAim = vmd_zero_vector;

	float duration() const;       // seconds until every move has finished
	CameraShot at(float t) const; // t seconds into the shot; a negative t is its end
};

// An event, for starting a shot where an earlier one ended. Supplied by a dialog whose tree
// holds events (the Events editor).
struct CameraEventInfo {
	SCP_string name;
	int formula = -1;            // root tree node
	bool chained = false;        // runs after the event before it in the list
	SCP_string startsAfter;      // the chosen event: empty = automatic, SEXP_NONE_STRING = new camera
	bool hasCameraSexps = false; // filled in by the sexp tree
};

class CameraEventSource {
  public:
	virtual ~CameraEventSource() = default;
	virtual SCP_vector<CameraEventInfo> cameraEventInfo() const = 0;
	virtual void setCameraStartsAfter(int eventIndex, const SCP_string& name) = 0;
};

// A selected camera sexp: the shot it is in, the pose to show for it, and what the editor can
// write into it.
struct CameraSexpPreview {
	int op = -1;     // OP_CUTSCENES_... constant
	int opNode = -1; // tree_nodes[] index of the operator

	// The selected sexp's whole event, as one shot: sexps without a time set its start, timed
	// sexps move it to its end. `shot` is its start for a selected sexp without a time, its end
	// for a timed one (atEnd).
	CameraTrack play;
	float duration = 0.0f;
	bool atEnd = false;
	CameraShot shot;

	// The selected sexp's own pose: `shot` with what the sexp sets (position, direction or field
	// of view) as the sexp leaves it. hasOwnShot only when that differs from `shot`, which is
	// when a later sexp of the event overrides it; the viewport draws it as a ghost.
	bool hasOwnShot = false;
	CameraShot ownShot;

	// The event the selected sexp is in, and the event its shot starts after, when the tree has
	// events. startsAfterAutomatic: worked out rather than chosen. startsAfterMissing: the chosen
	// event doesn't exist or has no camera sexps, so it was worked out instead.
	int eventIndex = -1;
	int startsAfterEvent = -1;
	int inferredStartsAfter = -1; // what automatic picks
	bool startsAfterAutomatic = true;
	bool startsAfterMissing = false;

	// The selected sexp's own point (the set-camera-position position or the set-camera-facing
	// point), in world space
	bool hasPoint = false;
	vec3d point = vmd_zero_vector;

	// Its arguments the editor can write, when they are plain numbers (-1 otherwise): the
	// point's three, set-camera-rotation's pitch, bank and heading, set-camera-fov's degrees
	int pointNodes[3] = {-1, -1, -1};
	int angleNodes[3] = {-1, -1, -1};
	int fovNode = -1;

	// For writing: the orientation a rotation is applied on top of at the shown pose, the host a
	// position is relative to, and where the camera is when a set-camera-facing runs (the game
	// aims it from there)
	matrix rotationBase = vmd_identity_matrix;
	int positionHost = -1;
	vec3d facingFrom = vmd_zero_vector;
};

// The nearest camera operator at or above `node` (a node inside one selects it), or -1.
int findCameraOperator(const SCP_vector<sexp_tree_item>& nodes, int node);

// Whether a formula has any camera sexps
bool formulaHasCameraSexps(const SCP_vector<sexp_tree_item>& nodes, int formula);

// Works out the selected sexp's shot: the shots of the events it starts after, oldest first,
// each finished, then its own event. Without events, the shot starts from a new camera.
bool previewCameraSexp(const SCP_vector<sexp_tree_item>& nodes, int opNode, CameraSexpPreview* out,
	const SCP_vector<CameraEventInfo>* events = nullptr);

// The selected sexp's point arguments for a point at `world` (a position is relative to a host).
bool cameraPointArgs(const CameraSexpPreview& preview, const vec3d& world, int out[3]);

// The selected set-camera-rotation's arguments (degrees) for a camera turned to `orient`.
bool cameraRotationArgs(const CameraSexpPreview& preview, const matrix& orient, int out[3]);

// The selected set-camera-facing's point for a camera looking along `orient`: straight ahead of
// where the facing aims from, as far away as the current point.
bool cameraFacingArgs(const CameraSexpPreview& preview, const matrix& orient, int out[3]);

// The selected set-camera-fov's argument (degrees) for a field of view in engine zoom units.
bool cameraFovArg(const CameraSexpPreview& preview, float fov, int* out);

} // namespace fso::fred
