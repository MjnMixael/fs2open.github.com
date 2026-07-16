#pragma once

#include "AbstractDialogModel.h"

#include <mission/missionparse.h>

namespace fso::fred::dialogs {

// Texture replacement editor for a single prop instance. Mirrors the ship version, but the
// replacements live directly on the prop (its from_table == false entries) rather than in the
// global Fred_texture_replacements store.
class PropTextureReplacementDialogModel : public AbstractDialogModel {
	Q_OBJECT
  public:
	PropTextureReplacementDialogModel(QObject* parent, EditorViewport* viewport, int propObjNum);

	bool apply() override;
	void reject() override;

	size_t getSize() const;
	SCP_string getDefaultName(size_t index) const;

	void setMap(size_t index, const SCP_string& type, const SCP_string& newName);
	SCP_string getMap(size_t index, const SCP_string& type) const;

	SCP_map<SCP_string, bool> getSubtypesForMap(size_t index) const;
	SCP_map<SCP_string, bool> getReplace(size_t index) const;
	SCP_map<SCP_string, bool> getInherit(size_t index) const;
	void setReplace(size_t index, const SCP_string& type, bool state);
	void setInherit(size_t index, const SCP_string& type, bool state);

  private: // NOLINT(readability-redundant-access-specifiers)
	void initializeData();
	void initSubTypes(polymodel* model, int mapNum);
	void saveSubMap(size_t index, const SCP_string& type, SCP_vector<texture_replace>& out);
	static bool testTexture(const SCP_string& name);

	int _propObjNum;
	SCP_vector<SCP_map<SCP_string, bool>> _subTypesAvailable;
	SCP_vector<SCP_map<SCP_string, bool>> _replaceMap;
	SCP_vector<SCP_map<SCP_string, bool>> _inheritMap;
	SCP_vector<SCP_string> _defaultTextures;
	SCP_vector<SCP_map<SCP_string, SCP_string>> _currentTextures;
	bool _mainChanged = false;
};

} // namespace fso::fred::dialogs
