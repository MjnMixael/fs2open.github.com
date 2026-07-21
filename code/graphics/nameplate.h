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

// Engine default size (in pixels) for a generated nameplate texture, used when neither the
// POF ($nameplate_width / $nameplate_height) nor a per-ship override provide one.
constexpr int NAMEPLATE_DEFAULT_WIDTH = 256;
constexpr int NAMEPLATE_DEFAULT_HEIGHT = 64;

// Generate a transparent texture with the given text centered on it, rendered with the
// specified font (by FontManager index) at the given scale.  This is used to fill a ship's
// "nameplate" model texture slot without any file I/O.
//
// Returns a new bitmap handle; the caller owns it and is responsible for calling bm_release().
// Returns -1 on failure (e.g. empty text, bad dimensions, or no render-target support).
int nameplate_generate_texture(const char* text, int font_index, float font_scale, int width, int height, color text_color);

#endif // _NAMEPLATE_H
