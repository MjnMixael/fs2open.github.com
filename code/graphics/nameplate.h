/*
 * Copyright (C) Freespace Open 2026.  All rights reserved.
 *
 * All source code herein is the property of Freespace Open. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 */

#ifndef _NAMEPLATE_H
#define _NAMEPLATE_H

#include "globalincs/pstypes.h"
#include "graphics/2d.h"		// for color

// Nameplate config for a ship's "nameplate" model texture slot - either a generated text
// texture or a picked file.  Applies only to models that have a texture named "nameplate".
typedef struct nameplate_info {
	bool		enabled = false;			// feature active for this ship
	bool		use_file = false;			// true = use texture_file; false = generate from text
	SCP_string	text;						// generate mode: the string to render
	SCP_string	font_filename;				// generate mode: font, stored by filename
	float		font_scale = 1.0f;			// generate mode: gr_string scale multiplier
	SCP_string	texture_file;				// file mode: replacement texture (bare name, no extension)
	int			width = -1;					// per-instance override, -1 = use POF/default
	int			height = -1;				// per-instance override, -1 = use POF/default

	bool operator==(const nameplate_info& o) const
	{
		return enabled == o.enabled && use_file == o.use_file && text == o.text && font_filename == o.font_filename &&
			font_scale == o.font_scale && texture_file == o.texture_file && width == o.width && height == o.height;
	}
	bool operator!=(const nameplate_info& o) const { return !(*this == o); }
} nameplate_info; // kept here rather than in missionparse.h so ship.h can hold one

// Engine default size (in pixels) for a generated nameplate texture, used when neither the
// POF ($nameplate_width / $nameplate_height) nor a per-ship override provide one.
constexpr int NAMEPLATE_DEFAULT_WIDTH = 256;
constexpr int NAMEPLATE_DEFAULT_HEIGHT = 64;

// Generate a transparent texture with the given text centered on it, rendered with the
// specified font (by FontManager index) at the given scale.  This is used to fill a ship's
// "nameplate" model texture slot without any file I/O.
//
// Returns a new render-target bitmap handle owned by the caller; ships get theirs via nameplate_acquire().
// Returns -1 on failure (e.g. empty text, bad dimensions, or no render-target support).
int nameplate_generate_texture(const char* text, int font_index, float font_scale, int width, int height, color text_color);

// The bitmap for a nameplate on the given model (generated, or loaded in file mode), or -1 if there
// is none. In game it comes from a cache that lives for the level: ship_page_in() fills it for every
// ship the mission can bring in, so arrivals only look theirs up, and ships with the same nameplate
// (wing waves, respawns) share one bitmap. The cache also outlives any one ship, which debris needs,
// since it shares its ship's texture replacements. In the editor every call makes a new bitmap.
int nameplate_acquire(const nameplate_info& np, int model_num);

// Done with a bitmap from nameplate_acquire(): the editor frees it, in game the cache keeps it.
void nameplate_release(int handle);

// Frees the level's cached nameplate bitmaps.
void nameplate_level_close();

#endif // _NAMEPLATE_H
