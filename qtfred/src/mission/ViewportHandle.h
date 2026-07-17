#pragma once

#include <globalincs/pstypes.h>

#include <functional>
#include <vector>

namespace fso::fred {

// Opaque id returned when a dialog registers a group of handles with
// EditorViewport. Wraps a small generation counter so a stale id from a
// previously-closed dialog never accidentally addresses a new group.
struct HandleGroupId {
	int index = -1;
	int generation = 0;

	bool valid() const { return index >= 0; }
};

// A single draggable marker overlaid on a viewport visualizer (asteroid box
// face/corner/center, volumetric nebula center, etc.). Not a mission object:
// the picking pre-pass tests these before select_object() runs and consumes
// the click on a hit, leaving the rest of the editing flow untouched.
struct ViewportHandle {
	enum class Kind {
		Center, // translates the whole shape; constrained by toolbar axis-lock
		Face,   // moves one AABB face along its normal axis
		Corner, // moves three adjacent AABB faces together
	};

	Kind kind = Kind::Center;
	vec3d world_pos = vmd_zero_vector;

	// For Face handles, the axis the face moves along (unit vector, +/-).
	// For Corner handles, a vector whose components are each +/-1, indicating
	// which side of the AABB this corner lies on (e.g. (+1,+1,-1) is the
	// max-x/max-y/min-z corner).
	// Ignored for Center handles.
	vec3d axis = vmd_zero_vector;

	// Render color (rgb). 0..255 components.
	int color_r = 255;
	int color_g = 255;
	int color_b = 255;

	// Called per mouse-move tick with the constrained world-space delta from
	// the previous tick. Implementors should route writes through their dialog
	// model so live UI refresh and apply/reject continue to work.
	std::function<void(const vec3d& delta_world)> on_drag;

	// Optional. Called once when a genuine mouse-release ends the drag (NOT on
	// Escape/right-click cancel). Direct-edit handles use this to mark the
	// mission modified exactly once per drag, matching object-drag behavior.
	std::function<void()> on_release;

	// Optional. Return false to render this handle as inert (grayed out, not
	// pickable). Used to honor the toolbar axis-lock for Face handles whose
	// axis is currently constrained out.
	std::function<bool()> is_enabled;

	// Optional. When non-empty and Show Info is on, this label is drawn next to
	// the handle (like a ship's name); coordinates are appended when Show
	// Coordinates is on. Also used as the hover-balloon text.
	SCP_string info_label;

	// When true, the handle's owning environment entity is selected: the label
	// renders green (matching a selected object) instead of white.
	bool is_selected = false;
};

} // namespace fso::fred
