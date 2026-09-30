#pragma once

#include "missioneditor/sexp_tree_model.h"

#include <globalincs/pstypes.h>
#include <math/vecmat.h>

namespace fso::fred {

// Where a cutscene camera is and what it shows once a run of camera sexps has finished: the
// editor's stand-in for the engine's camera (camera/camera.cpp), worked out from a sexp tree
// without running it. Transitions are skipped; this is the pose each one ends on.
struct CameraShot {
	vec3d pos = vmd_zero_vector;
	matrix orient = vmd_identity_matrix;
	float fov = 0.0f; // engine zoom units, like EditorViewport::viewFov()
	int hostObj = -1; // Objects[] index, or -1
	int targetObj = -1;
	bool hasAimPoint = false; // what set-camera-facing(-object) or a target turned it to
	vec3d aimPoint = vmd_zero_vector;
};

// A selected camera sexp: its operator, the shot once it has run, and what the editor can
// write back into it.
struct CameraSexpPreview {
	int op = -1;     // OP_CUTSCENES_... constant
	int opNode = -1; // tree_nodes[] index of the operator
	CameraShot shot;

	// The operator's own point (the set-camera-position position or the set-camera-facing
	// point), in world space, and its three argument nodes. The nodes are only set when all
	// three are plain numbers, so they can be written.
	bool hasPoint = false;
	vec3d point = vmd_zero_vector;
	int pointNodes[3] = {-1, -1, -1};

	// set-camera-rotation's pitch, bank and heading nodes, when they are plain numbers
	int angleNodes[3] = {-1, -1, -1};

	// The orientation the rotation is applied on top of (a target's facing, the host's
	// orientation, or identity), and the host the position is relative to, both as of the
	// selected operator
	matrix rotationBase = vmd_identity_matrix;
	int positionHost = -1;
};

// The nearest camera operator at or above `node` (a node inside one selects it), or -1.
int findCameraOperator(const SCP_vector<sexp_tree_item>& nodes, int node);

// Runs the camera sexps of the formula that holds `opNode`, in order, up to and including it.
// Each formula starts from a new camera, since earlier events may or may not have run.
bool previewCameraSexp(const SCP_vector<sexp_tree_item>& nodes, int opNode, CameraSexpPreview* out);

// The argument values that put the selected operator's point at `world` (set-camera-position
// arguments are relative to a host). False if the operator has no writable point.
bool cameraPointArgs(const CameraSexpPreview& preview, const vec3d& world, int out[3]);

// The set-camera-rotation arguments (degrees) that turn the camera to `orient`. False if the
// operator isn't a rotation with writable arguments.
bool cameraRotationArgs(const CameraSexpPreview& preview, const matrix& orient, int out[3]);

} // namespace fso::fred
