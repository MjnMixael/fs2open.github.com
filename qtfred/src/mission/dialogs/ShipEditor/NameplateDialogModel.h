#pragma once

#include "../AbstractDialogModel.h"

#include "mission/missionparse.h" // for nameplate_info

namespace fso::fred::dialogs {

// Model for the ship Nameplate sub-dialog.  Edits one ship's nameplate config (either a generated
// text texture or a picked file). preview() shows the working copy on the ship while the dialog is
// open, reject() puts the ship's own settings back, and apply() keeps the working copy; the Ship
// Editor records the undo.
class NameplateDialogModel : public AbstractDialogModel {
	Q_OBJECT
  public:
	NameplateDialogModel(QObject* parent, EditorViewport* viewport, int shipnum);

	bool apply() override;
	void reject() override;

	// show the working copy on the ship in the viewport, without marking the mission changed
	void preview();

	void setEnabled(bool enabled);
	bool getEnabled() const;

	void setUseFile(bool useFile);
	bool getUseFile() const;

	// false when no loaded font can draw nameplates; then only texture files are offered
	static bool canGenerate();

	void setText(const SCP_string& text);
	SCP_string getText() const;

	void setFontName(const SCP_string& name);
	SCP_string getFontName() const;

	void setFontScale(float scale);
	float getFontScale() const;

	void setLetterSpacing(float spacing);
	float getLetterSpacing() const;

	void setColorR(int r);
	void setColorG(int g);
	void setColorB(int b);
	int getColorR() const;
	int getColorG() const;
	int getColorB() const;

	void setOffsetX(int x);
	void setOffsetY(int y);
	int getOffsetX() const;
	int getOffsetY() const;

	// texture size for generated text: the ship's override, or the model's size when there is none
	int getWidth() const;
	int getHeight() const;
	int getModelWidth() const;
	int getModelHeight() const;
	bool isModelSize() const;
	// a size equal to the model's is stored as no override, so the ship keeps following the model
	void setSize(int width, int height);

	void setTextureFile(const SCP_string& file);
	SCP_string getTextureFile() const;


  private: // NOLINT(readability-redundant-access-specifiers)
	int _shipnum = -1;
	nameplate_info _original; // the ship's settings when the dialog opened, restored by reject()
	nameplate_info _nameplate;
	int _modelWidth = NAMEPLATE_DEFAULT_WIDTH;
	int _modelHeight = NAMEPLATE_DEFAULT_HEIGHT;
};

} // namespace fso::fred::dialogs
