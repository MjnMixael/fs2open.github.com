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
#include "utils/unicode.h"

#include <algorithm>

int nameplate_font_index(const SCP_string& font_name)
{
	if (font_name.empty())
		return -1;

	const int index = font::FontManager::getFontIndex(font_name);
	return (index >= 0) ? index : font::FontManager::getFontIndexByFilename(font_name);
}

bool nameplate_font_usable(int font_index)
{
	if (font_index < 0 || font_index >= font::FontManager::numberOfFonts())
		return false;

	const auto* fnt = font::FontManager::getFont(font_index);
	return fnt != nullptr && !(fnt->getType() == font::NVG_FONT && fnt->getAutoScaleBehavior());
}

int nameplate_resolve_font(const SCP_string& font_name)
{
	const int named = nameplate_font_index(font_name);
	if (nameplate_font_usable(named))
		return named;

	for (int i = 0; i < font::FontManager::numberOfFonts(); ++i) {
		if (nameplate_font_usable(i))
			return i;
	}
	return -1;
}

void nameplate_model_size(int model_num, int* width, int* height)
{
	const polymodel* pm = (model_num >= 0) ? model_get(model_num) : nullptr;
	*width = (pm != nullptr && pm->nameplate_width > 0) ? pm->nameplate_width : NAMEPLATE_DEFAULT_WIDTH;
	*height = (pm != nullptr && pm->nameplate_height > 0) ? pm->nameplate_height : NAMEPLATE_DEFAULT_HEIGHT;
	*width = std::clamp(*width, NAMEPLATE_MIN_SIZE, NAMEPLATE_MAX_SIZE);
	*height = std::clamp(*height, NAMEPLATE_MIN_SIZE, NAMEPLATE_MAX_SIZE);
}

int nameplate_generate_texture(const char* text, int font_index, float font_scale, float letter_spacing, int width,
	int height, color text_color, float offset_x, float offset_y)
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

	// select the requested font, or the first one, so the result never depends on what drew last
	if (font_index < 0 || font_index >= font::FontManager::numberOfFonts())
		font_index = 0;
	font::FontManager::setCurrentFontIndex(font_index);

	// The font's scaling flags are for the HUD and menus: one follows the player's font size option,
	// the other the screen resolution, and the editor uses neither. A texture should be the same
	// everywhere, so they are off while it is drawn.
	auto* fnt = font::FontManager::getFont(font_index);
	const bool saved_can_scale = (fnt != nullptr) && fnt->getScaleBehavior();
	const bool saved_auto_scale = (fnt != nullptr) && fnt->getAutoScaleBehavior();
	if (fnt != nullptr) {
		fnt->setScaleBehavior(false);
		fnt->setAutoScaleBehavior(false);
	}

	// center the string on the texture, counting the extra space between characters, then move it by the offset
	const char* text_end = text + strlen(text);
	const size_t num_chars = unicode::num_codepoints(text, text_end);
	int str_w = 0;
	int str_h = 0;
	gr_get_string_size(&str_w, &str_h, text, font_scale);
	const float total_w = str_w + letter_spacing * static_cast<float>(num_chars > 0 ? num_chars - 1 : 0);
	const float x = (width - total_w) / 2.0f + offset_x;
	const float y = (height - str_h) / 2.0f + offset_y;

	gr_set_color_fast(&text_color);
	if (letter_spacing == 0.0f) {
		gr_string(x, y, text, GR_RESIZE_NONE, font_scale);
	} else {
		// one character at a time, each placed after the width of the text before it plus the spacing
		size_t i = 0;
		for (const char* p = text; p < text_end; ++i) {
			const char* next = p;
			unicode::advance(next, 1, text_end);

			int prefix_w = 0;
			if (p > text)
				gr_get_string_size(&prefix_w, nullptr, text, font_scale, static_cast<size_t>(p - text));
			gr_string(x + prefix_w + letter_spacing * static_cast<float>(i), y, p, GR_RESIZE_NONE, font_scale,
				static_cast<size_t>(next - p));

			p = next;
		}
	}

	// restore the state we changed
	if (fnt != nullptr) {
		fnt->setScaleBehavior(saved_can_scale);
		fnt->setAutoScaleBehavior(saved_auto_scale);
	}
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
	SCP_string font_name;
	float font_scale;
	float letter_spacing;
	int color_r;
	int color_g;
	int color_b;
	int offset_x;
	int offset_y;
	SCP_string texture_file;
	int width;
	int height;
	int model_width;
	int model_height;
	int handle;
};

SCP_vector<cached_nameplate> Nameplate_cache;

int nameplate_make(const nameplate_info& np, int width, int height, int model_width, int model_height)
{
	if (np.use_file) {
		int bm = bm_load_either(np.texture_file.c_str());
		if (bm < 0) {
			mprintf(("Nameplate: could not load texture '%s'\n", np.texture_file.c_str()));
		} else if (!Fred_running) {
			// mark it for this level's page-in, which would otherwise unload it again
			bm_page_in_texture(bm);
		}
		return bm;
	}

	// a font that isn't loaded or can't be used falls back to the first usable one
	const int font_index = nameplate_resolve_font(np.font_name);
	if (font_index < 0) {
		mprintf(("Nameplate: no font can draw nameplates, so text '%s' is skipped\n", np.text.c_str()));
		return -1;
	}

	color text_color;
	gr_init_color(&text_color, np.color_r, np.color_g, np.color_b);

	// the text settings are in pixels of the model's size, so a different size only changes how sharp
	// the text is: the font follows the height, spacing and offsets their own axis
	const float scale_x = static_cast<float>(width) / static_cast<float>(model_width);
	const float scale_y = static_cast<float>(height) / static_cast<float>(model_height);

	return nameplate_generate_texture(np.text.c_str(), font_index, np.font_scale * scale_y,
		np.letter_spacing * scale_x, width, height, text_color, np.offset_x * scale_x, np.offset_y * scale_y);
}

} // namespace

int nameplate_acquire(const nameplate_info& np, int model_num)
{
	if (!np.enabled || (np.use_file ? np.texture_file.empty() : np.text.empty()))
		return -1;

	// resolve the size: the ship's override, else the model's (its POF props, else the engine default)
	int width = 0;
	int height = 0;
	int model_width = 0;
	int model_height = 0;
	if (!np.use_file) {
		nameplate_model_size(model_num, &model_width, &model_height);
		width = (np.width > 0) ? std::clamp(np.width, NAMEPLATE_MIN_SIZE, NAMEPLATE_MAX_SIZE) : model_width;
		height = (np.height > 0) ? std::clamp(np.height, NAMEPLATE_MIN_SIZE, NAMEPLATE_MAX_SIZE) : model_height;
	}

	if (Fred_running)
		return nameplate_make(np, width, height, model_width, model_height);

	for (const auto& c : Nameplate_cache) {
		if (c.use_file != np.use_file)
			continue;
		if (np.use_file ? lcase_equal(c.texture_file, np.texture_file)
				: (c.text == np.text && lcase_equal(c.font_name, np.font_name) && c.font_scale == np.font_scale
					&& c.letter_spacing == np.letter_spacing
					&& c.color_r == np.color_r && c.color_g == np.color_g && c.color_b == np.color_b
					&& c.offset_x == np.offset_x && c.offset_y == np.offset_y && c.width == width && c.height == height
					&& c.model_width == model_width && c.model_height == model_height))
			return c.handle;
	}

	// a failure is cached too, so later arrivals don't try again
	Nameplate_cache.push_back({np.use_file, np.text, np.font_name, np.font_scale, np.letter_spacing, np.color_r,
		np.color_g, np.color_b, np.offset_x, np.offset_y, np.texture_file, width, height, model_width, model_height,
		nameplate_make(np, width, height, model_width, model_height)});
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
