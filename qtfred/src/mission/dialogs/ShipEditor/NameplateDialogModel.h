#pragma once

#include "../AbstractDialogModel.h"

#include "mission/missionparse.h" // for nameplate_info

namespace fso::fred::dialogs {

// Model for the ship Nameplate sub-dialog.  Edits the current ship's nameplate config (either a
// generated text texture or a picked file) and applies it to the ship on apply().
class NameplateDialogModel : public AbstractDialogModel {
	Q_OBJECT
  public:
	NameplateDialogModel(QObject* parent, EditorViewport* viewport);

	bool apply() override;
	void reject() override;

	void setEnabled(bool enabled);
	bool getEnabled() const;

	void setUseFile(bool useFile);
	bool getUseFile() const;

	void setText(const SCP_string& text);
	SCP_string getText() const;

	void setFontFilename(const SCP_string& filename);
	SCP_string getFontFilename() const;

	void setFontScale(float scale);
	float getFontScale() const;

	void setTextureFile(const SCP_string& file);
	SCP_string getTextureFile() const;

	// width/height use -1 to mean "no override" (fall back to POF/engine default)
	void setWidth(int width);
	int getWidth() const;

	void setHeight(int height);
	int getHeight() const;

  private: // NOLINT(readability-redundant-access-specifiers)
	void initializeData();

	int _shipnum = -1;
	nameplate_info _nameplate;
};

} // namespace fso::fred::dialogs
