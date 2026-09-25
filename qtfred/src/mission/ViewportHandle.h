#pragma once

#include <globalincs/pstypes.h>
#include <math/vecmat.h>

#include <functional>
#include <vector>

namespace fso::fred {

// Opaque id returned when a group of handles is registered with EditorViewport.
// Wraps a generation counter so a stale id for an unregistered group never
// addresses whatever group replaced it.
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
	// the previous tick. Returns the delta actually applied, which is less than
	// the requested one when a clamp stopped the move; the drag anchors on it so
	// the handle stays under the cursor once the cursor comes back.
	std::function<vec3d(const vec3d& delta_world)> on_drag;

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

	// When true, this handle's coordinates are drawn (under Show Coordinates)
	// and shown in its hover balloon, independent of info_label. Lets handles
	// without a name (asteroid faces/corners) still surface their position.
	bool show_coords = false;

	// When true, a grid-drop indicator line is drawn from this handle to the
	// grid plane (under Show Grid Positions), like an object's.
	bool show_grid_position = false;

	// Bitmask of axes (bit0=X, bit1=Y, bit2=Z) the transform-toolbar spinboxes
	// may edit when this handle is the selected one. Face handles restrict to
	// their single axis; corners/centers allow all three.
	int movable_axes = 0x7;
};

} // namespace fso::fred
