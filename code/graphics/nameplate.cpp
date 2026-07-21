/*
 * Copyright (C) Freespace Open 2026.  All rights reserved.
 *
 * All source code herein is the property of Freespace Open. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 */

#include "graphics/nameplate.h"

#include "bmpman/bmpman.h"
#include "graphics/2d.h"
#include "graphics/render.h"
#include "graphics/software/FontManager.h"

int nameplate_generate_texture(const char* text, int font_index, float font_scale, int width, int height, color text_color)
{
	if (text == nullptr || *text == '\0')
		return -1;

	if (width <= 0 || height <= 0)
		return -1;

	if (font_scale <= 0.0f)
		font_scale = 1.0f;

	// create a render target to draw the text onto (returns -1 if FBOs/graphics are unavailable)
	int handle = bm_make_render_target(width, height, BMP_FLAG_RENDER_TARGET_STATIC);
	if (handle < 0) {
		mprintf(("Nameplate: could not create a %dx%d render target for text '%s'\n", width, height, text));
		return -1;
	}

	// remember the state we are about to modify so we can put it back afterwards
	const int saved_font = font::FontManager::getCurrentFontIndex();
	const color saved_clear_color = gr_screen.current_clear_color;
	const int saved_target = gr_screen.rendering_to_texture;

	if (!bm_set_render_target(handle)) {
		bm_release(handle);
		return -1;
	}

	const int saved_cull = gr_set_cull(0);

	// clear to fully transparent so only the rendered text ends up on the ship's hull
	gr_init_alphacolor(&gr_screen.current_clear_color, 0, 0, 0, 0);
	gr_clear();

	// select the requested font (fall back to whatever is current if the index is invalid)
	if (font_index >= 0 && font_index < font::FontManager::numberOfFonts())
		font::FontManager::setCurrentFontIndex(font_index);

	// center the string on the texture
	int str_w = 0;
	int str_h = 0;
	gr_get_string_size(&str_w, &str_h, text, font_scale);
	const float x = (width - str_w) / 2.0f;
	const float y = (height - str_h) / 2.0f;

	gr_set_color_fast(&text_color);
	gr_string(x, y, text, GR_RESIZE_NONE, font_scale);

	// restore the state we changed
	gr_set_cull(saved_cull);
	gr_screen.current_clear_color = saved_clear_color;
	font::FontManager::setCurrentFontIndex(saved_font);

	// go back to whatever we were rendering to before (usually the back buffer)
	bm_set_render_target(saved_target);

	return handle;
}
