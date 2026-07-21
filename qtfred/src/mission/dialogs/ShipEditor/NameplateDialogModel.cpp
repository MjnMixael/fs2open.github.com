#include "NameplateDialogModel.h"

#include "ship/ship.h"

namespace fso::fred::dialogs {

NameplateDialogModel::NameplateDialogModel(QObject* parent, EditorViewport* viewport)
	: AbstractDialogModel(parent, viewport)
{
	initializeData();
}

void NameplateDialogModel::initializeData()
{
	_shipnum = _editor->cur_ship;
	if (_shipnum >= 0)
		_nameplate = Ships[_shipnum].nameplate;
}

bool NameplateDialogModel::apply()
{
	if (_shipnum < 0)
		return true;

	Ships[_shipnum].nameplate = _nameplate;

	// (re)generate and inject the texture so the change is reflected in the viewport immediately
	Ships[_shipnum].apply_nameplate();

	_editor->missionChanged();
	return true;
}

void NameplateDialogModel::reject() {}

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
	modify(_nameplate.use_file, useFile);
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

void NameplateDialogModel::setFontFilename(const SCP_string& filename)
{
	modify(_nameplate.font_filename, filename);
}
SCP_string NameplateDialogModel::getFontFilename() const
{
	return _nameplate.font_filename;
}

void NameplateDialogModel::setFontScale(float scale)
{
	modify(_nameplate.font_scale, scale);
}
float NameplateDialogModel::getFontScale() const
{
	return _nameplate.font_scale;
}

void NameplateDialogModel::setTextureFile(const SCP_string& file)
{
	modify(_nameplate.texture_file, file);
}
SCP_string NameplateDialogModel::getTextureFile() const
{
	return _nameplate.texture_file;
}

void NameplateDialogModel::setWidth(int width)
{
	modify(_nameplate.width, width);
}
int NameplateDialogModel::getWidth() const
{
	return _nameplate.width;
}

void NameplateDialogModel::setHeight(int height)
{
	modify(_nameplate.height, height);
}
int NameplateDialogModel::getHeight() const
{
	return _nameplate.height;
}

} // namespace fso::fred::dialogs
