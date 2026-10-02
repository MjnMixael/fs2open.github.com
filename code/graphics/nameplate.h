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
// texture or a picked file.  Applies only to models that have a texture named "nameplate".  Font scale,
// letter spacing and offsets are in pixels of the model's size, so a size override only changes sharpness.
typedef struct nameplate_info {
	bool		enabled = false;			// feature active for this ship
	bool		use_file = false;			// true = use texture_file; false = generate from text
	SCP_string	text;						// generate mode: the string to render
	SCP_string	font_name;					// generate mode: font by name (unique, unlike filenames); empty = first font
	float		font_scale = 1.0f;			// generate mode: gr_string scale multiplier
	float		letter_spacing = 0.0f;		// generate mode: extra pixels between characters (negative tightens)
	int			color_r = 255;				// generate mode: text color
	int			color_g = 255;
	int			color_b = 255;
	int			offset_x = 0;				// generate mode: pixels to move the text from centered
	int			offset_y = 0;
	int			width = -1;					// generate mode: texture size override, -1 = the model's size
	int			height = -1;
	SCP_string	texture_file;				// file mode: replacement texture (bare name, no extension)

	bool operator==(const nameplate_info& o) const
	{
		return enabled == o.enabled && use_file == o.use_file && text == o.text && font_name == o.font_name &&
			font_scale == o.font_scale && letter_spacing == o.letter_spacing && color_r == o.color_r && color_g == o.color_g && color_b == o.color_b &&
			offset_x == o.offset_x && offset_y == o.offset_y && width == o.width && height == o.height &&
			texture_file == o.texture_file;
	}
	bool operator!=(const nameplate_info& o) const { return !(*this == o); }
} nameplate_info; // kept here rather than in missionparse.h so ship.h can hold one

// Engine default size (in pixels) for a generated nameplate texture, used when the POF doesn't give one
// ($nameplate_width / $nameplate_height). A ship can override it, but the model's UVs stretch the whole
// texture over the same area, so a size with other proportions than the model's stretches the text.
constexpr int NAMEPLATE_DEFAULT_WIDTH = 256;
constexpr int NAMEPLATE_DEFAULT_HEIGHT = 64;

// Smallest and largest generated nameplate side; the largest is also the offset limit either way.
constexpr int NAMEPLATE_MIN_SIZE = 16;
constexpr int NAMEPLATE_MAX_SIZE = 1024;

// The size a model asks for generated nameplates: its POF props, else the engine default.
void nameplate_model_size(int model_num, int* width, int* height);

// Letter spacing limit either way, in pixels.
constexpr float NAMEPLATE_MAX_LETTER_SPACING = 100.0f;

// The FontManager index of a nameplate font, or -1 if it isn't loaded. Looks the name up, then a
// filename, which is what missions saved by early test builds hold.
int nameplate_font_index(const SCP_string& font_name);

// Whether a font can draw nameplates. A TrueType font with +Auto Size cannot: it is sized from the
// screen resolution when it loads (the editor skips that), so it would come out differently in game.
bool nameplate_font_usable(int font_index);

// The font a nameplate draws with: the named font if it is usable, else the first usable font, or -1
// if there is none, in which case only texture files work.
int nameplate_resolve_font(const SCP_string& font_name);

// Generate a transparent texture with the given text centered on it and then moved by the offset,
// rendered with the specified font (by FontManager index; an invalid one uses the first font) at the
// given scale, with letter_spacing extra pixels between characters (all in pixels of this texture).
// This is used to fill a ship's "nameplate" model texture slot without any file I/O.
//
// Returns a new render-target bitmap handle owned by the caller; ships get theirs via nameplate_acquire().
// Returns -1 on failure (e.g. empty text, bad dimensions, or no render-target support).
int nameplate_generate_texture(const char* text, int font_index, float font_scale, float letter_spacing, int width,
	int height, color text_color, float offset_x, float offset_y);

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
