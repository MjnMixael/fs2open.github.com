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
#include "model/model.h"

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
		bm_release(handle, 1);
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

namespace {

// One bitmap per distinct nameplate this level. width/height are the resolved size (0 in file mode).
struct cached_nameplate {
	bool use_file;
	SCP_string text;
	SCP_string font_filename;
	float font_scale;
	SCP_string texture_file;
	int width;
	int height;
	int handle;
};

SCP_vector<cached_nameplate> Nameplate_cache;

int nameplate_make(const nameplate_info& np, int width, int height)
{
	if (np.use_file) {
		int bm = bm_load_either(np.texture_file.c_str());
		if (bm < 0) {
			mprintf(("Nameplate: could not load texture '%s'
", np.texture_file.c_str()));
		} else if (!Fred_running) {
			// mark it for this level's page-in, which would otherwise unload it again
			bm_page_in_texture(bm);
		}
		return bm;
	}

	// resolve the font by filename; -1 means "use whatever font is current"
	int font_index = -1;
	if (!np.font_filename.empty())
		font_index = font::FontManager::getFontIndexByFilename(np.font_filename);

	color text_color;
	gr_init_color(&text_color, 255, 255, 255);

	return nameplate_generate_texture(np.text.c_str(), font_index, np.font_scale, width, height, text_color);
}

} // namespace

int nameplate_acquire(const nameplate_info& np, int model_num)
{
	if (!np.enabled || (np.use_file ? np.texture_file.empty() : np.text.empty()))
		return -1;

	// resolve the size: per-ship override, then the POF's, then the engine default
	int width = 0;
	int height = 0;
	if (!np.use_file) {
		const polymodel* pm = (model_num >= 0) ? model_get(model_num) : nullptr;
		width = (np.width > 0) ? np.width
			: ((pm != nullptr && pm->nameplate_width > 0) ? pm->nameplate_width : NAMEPLATE_DEFAULT_WIDTH);
		height = (np.height > 0) ? np.height
			: ((pm != nullptr && pm->nameplate_height > 0) ? pm->nameplate_height : NAMEPLATE_DEFAULT_HEIGHT);
	}

	if (Fred_running)
		return nameplate_make(np, width, height);

	for (const auto& c : Nameplate_cache) {
		if (c.use_file != np.use_file)
			continue;
		if (np.use_file ? lcase_equal(c.texture_file, np.texture_file)
				: (c.text == np.text && lcase_equal(c.font_filename, np.font_filename) && c.font_scale == np.font_scale
					&& c.width == width && c.height == height))
			return c.handle;
	}

	// a failure is cached too, so later arrivals don't try again
	Nameplate_cache.push_back({np.use_file, np.text, np.font_filename, np.font_scale, np.texture_file, width, height,
		nameplate_make(np, width, height)});
	return Nameplate_cache.back().handle;
}

void nameplate_release(int handle)
{
	// in game the level cache owns the bitmap; see nameplate_level_close()
	if (Fred_running && handle >= 0) {
		// render targets are only freed with clear_render_targets set; a file bitmap just drops a load count
		bm_release(handle, 1);
	}
}

void nameplate_level_close()
{
	for (const auto& c : Nameplate_cache) {
		if (c.handle >= 0)
			bm_release(c.handle, 1);
	}
	Nameplate_cache.clear();
}
