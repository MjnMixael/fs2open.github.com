/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 *
*/



#ifndef _NEBULA_H
#define _NEBULA_H

#include "globalincs/pstypes.h"

// mainly only needed by Fred
extern int Nebula_pitch;
extern int Nebula_bank;
extern int Nebula_heading;

struct angles;

// A procedural "old" (FS1-style) background nebula pattern.  These are data-driven (defined in
// the old_nebula.tbm default table and any *-neb.tbm), so modders can add/modify/remove them.
struct old_nebula_pattern {
	SCP_string name;
	float density  = 0.30f;    // fraction of bright "knots" (mostly-black otherwise)
	float freq_u   = 2.0f;     // noise frequency along longitude (unequal u/v => streaky)
	float freq_v   = 5.0f;     // noise frequency along latitude
	float warp     = 0.4f;     // directional warp strength (stretches the wisps)
	float contrast = 1.5f;     // falloff curve applied to bright knots
	float intensity = 1.0f;    // overall brightness multiplier (channels are clamped to max)
	int   seed     = 0;        // noise seed (gives each pattern a distinct look)
	int   res_lon  = 24;       // longitude grid resolution (coarse = retro chunky gouraud)
	int   res_lat  = 12;       // latitude grid resolution
	float band_min = 0.05f;    // latitude extent (skip the poles, matching the originals)
	float band_max = 0.93f;
};

// A named tint color for the old nebula.  Also data-driven via the table.
struct old_nebula_color {
	SCP_string name;
	ubyte r = 255;
	ubyte g = 255;
	ubyte b = 255;
};

extern SCP_vector<old_nebula_pattern> Old_nebula_patterns;
extern SCP_vector<old_nebula_color>   Old_nebula_colors;

// Clear the registries above and load the built-in default patterns/colors.  Called once from
// neb2_init(), before the neb2 tables are parsed (game data overrides/extends the defaults).
void old_nebula_init();

// Parse any #Old Nebula Patterns / #Old Nebula Colors sections out of the table text already
// loaded in the parse buffer.  Called by parse_nebula_table() so the old-nebula data is read as
// part of the neb2 nebula.tbl / *-neb.tbm pass rather than re-reading those files.
void old_nebula_parse_buffer();

// Look up a pattern/color by name; returns the registry index or -1 if not found.
int old_nebula_pattern_lookup(const char *name);
int old_nebula_color_lookup(const char *name);

// Safe name accessors for the registries (return "" when the index is out of range).
const char *old_nebula_pattern_name(int index);
const char *old_nebula_color_name(int index);

// PBH = Pitch, Bank, Heading (in degrees).  index < 0 disables the nebula.
void nebula_init( int index, int pitch, int bank, int heading );
void nebula_close();

// Renders the procedural background nebula (no-op if none is active).
void nebula_render();

#endif	//_NEBULA_H
