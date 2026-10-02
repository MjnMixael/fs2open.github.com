#include "NameplateDialogModel.h"

#include "ship/ship.h"

#include <algorithm>

namespace fso::fred::dialogs {

NameplateDialogModel::NameplateDialogModel(QObject* parent, EditorViewport* viewport, int shipnum)
	: AbstractDialogModel(parent, viewport), _shipnum(shipnum)
{
	if (_shipnum >= 0) {
		_original = _nameplate = Ships[_shipnum].nameplate;
		nameplate_model_size(Ship_info[Ships[_shipnum].ship_info_index].model_num, &_modelWidth, &_modelHeight);
	}

	// with no font to draw text, a text nameplate can't exist, so the dialog starts on texture files
	if (!canGenerate())
		_nameplate.use_file = true;
}

bool NameplateDialogModel::canGenerate()
{
	return nameplate_resolve_font(SCP_string()) >= 0;
}

bool NameplateDialogModel::apply()
{
	if (_shipnum < 0)
		return true;

	// usually already showing from preview(); this covers an edit made just before OK
	preview();

	_editor->missionChanged();
	return true;
}

void NameplateDialogModel::reject()
{
	if (_shipnum < 0)
		return;

	// previews changed the ship; put back what it had
	if (Ships[_shipnum].nameplate != _original) {
		Ships[_shipnum].nameplate = _original;
		Ships[_shipnum].apply_nameplate();
		_viewport->needsUpdate();
	}
}

void NameplateDialogModel::preview()
{
	if (_shipnum < 0 || Ships[_shipnum].nameplate == _nameplate)
		return;

	Ships[_shipnum].nameplate = _nameplate;
	Ships[_shipnum].apply_nameplate();
	_viewport->needsUpdate();
}

void NameplateDialogModel::setEnabled(bool enabled)
{
	modify(_nameplate.enabled, enabled);
}
bool NameplateDialogModel::getEnabled() const
{
	return _nameplate.enabled;
}

void NameplateDialogModel::setUseFile(bool useFile)
{
	modify(_nameplate.use_file, useFile || !canGenerate());
}
bool NameplateDialogModel::getUseFile() const
{
	return _nameplate.use_file;
}

void NameplateDialogModel::setText(const SCP_string& text)
{
	modify(_nameplate.text, text);
}
SCP_string NameplateDialogModel::getText() const
{
	return _nameplate.text;
}

void NameplateDialogModel::setFontName(const SCP_string& name)
{
	modify(_nameplate.font_name, name);
}
SCP_string NameplateDialogModel::getFontName() const
{
	return _nameplate.font_name;
}

void NameplateDialogModel::setFontScale(float scale)
{
	modify(_nameplate.font_scale, scale);
}
float NameplateDialogModel::getFontScale() const
{
	return _nameplate.font_scale;
}

void NameplateDialogModel::setLetterSpacing(float spacing)
{
	modify(_nameplate.letter_spacing,
		std::clamp(spacing, -NAMEPLATE_MAX_LETTER_SPACING, NAMEPLATE_MAX_LETTER_SPACING));
}
float NameplateDialogModel::getLetterSpacing() const
{
	return _nameplate.letter_spacing;
}

void NameplateDialogModel::setColorR(int r)
{
	modify(_nameplate.color_r, std::clamp(r, 0, 255));
}
void NameplateDialogModel::setColorG(int g)
{
	modify(_nameplate.color_g, std::clamp(g, 0, 255));
}
void NameplateDialogModel::setColorB(int b)
{
	modify(_nameplate.color_b, std::clamp(b, 0, 255));
}
int NameplateDialogModel::getColorR() const
{
	return _nameplate.color_r;
}
int NameplateDialogModel::getColorG() const
{
	return _nameplate.color_g;
}
int NameplateDialogModel::getColorB() const
{
	return _nameplate.color_b;
}

void NameplateDialogModel::setOffsetX(int x)
{
	modify(_nameplate.offset_x, std::clamp(x, -NAMEPLATE_MAX_SIZE, NAMEPLATE_MAX_SIZE));
}
void NameplateDialogModel::setOffsetY(int y)
{
	modify(_nameplate.offset_y, std::clamp(y, -NAMEPLATE_MAX_SIZE, NAMEPLATE_MAX_SIZE));
}
int NameplateDialogModel::getOffsetX() const
{
	return _nameplate.offset_x;
}
int NameplateDialogModel::getOffsetY() const
{
	return _nameplate.offset_y;
}

int NameplateDialogModel::getWidth() const
{
	return (_nameplate.width > 0) ? _nameplate.width : _modelWidth;
}
int NameplateDialogModel::getHeight() const
{
	return (_nameplate.height > 0) ? _nameplate.height : _modelHeight;
}
int NameplateDialogModel::getModelWidth() const
{
	return _modelWidth;
}
int NameplateDialogModel::getModelHeight() const
{
	return _modelHeight;
}
bool NameplateDialogModel::isModelSize() const
{
	return _nameplate.width <= 0 || _nameplate.height <= 0;
}
void NameplateDialogModel::setSize(int width, int height)
{
	width = std::clamp(width, NAMEPLATE_MIN_SIZE, NAMEPLATE_MAX_SIZE);
	height = std::clamp(height, NAMEPLATE_MIN_SIZE, NAMEPLATE_MAX_SIZE);
	if (width == _modelWidth && height == _modelHeight) {
		width = -1;
		height = -1;
	}
	modify(_nameplate.width, width);
	modify(_nameplate.height, height);
}

void NameplateDialogModel::setTextureFile(const SCP_string& file)
{
	modify(_nameplate.texture_file, file);
}
SCP_string NameplateDialogModel::getTextureFile() const
{
	return _nameplate.texture_file;
}

} // namespace fso::fred::dialogs
