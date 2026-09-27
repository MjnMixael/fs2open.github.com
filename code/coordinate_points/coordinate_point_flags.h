#pragma once

#include "globalincs/flagset.h"

namespace CoordinatePoint {

FLAG_LIST(Flags){
	Visible_in_mission,    // Render the shape in-game (not just in the editor) and allow target-in-front pickup.
	Always_render_labels,  // While Visible_in_mission, draw the HUD name/group label even when not targeted.

	NUM_VALUES};

} // namespace CoordinatePoint
