#pragma once

#include "mission/dialogs/AbstractDialogModel.h"
#include "mission/missionparse.h"

namespace fso::fred::dialogs {

class PropEditorDialogModel : public AbstractDialogModel {
	Q_OBJECT

 public:
	PropEditorDialogModel(QObject* parent, EditorViewport* viewport);

	bool apply() override;
	void reject() override;

	bool hasValidSelection() const;
	bool hasMultipleSelection() const;
	// object number of the single selected prop, or -1 if there isn't exactly one
	int getSelectedPropObject() const;
	static bool hasAnyPropsInMission();
	const SCP_vector<int>& getSelectedPropObjects() const;
	const SCP_string& getPropName() const;
	bool setPropName(const SCP_string& name);

	const SCP_vector<std::pair<SCP_string, size_t>>& getFlagLabels() const;
	const SCP_vector<int>& getFlagState() const;
	void setFlagState(size_t index, int state);
	static SCP_vector<std::pair<SCP_string, SCP_string>> getPropFlagDescriptions();
	static bool getFlagValueForObject(const object& obj, size_t flag_index);

	void selectNextProp();
	void selectPreviousProp();

	SCP_string getLayer() const;
	void setLayer(const SCP_string& layer);

	// prop class - index into Prop_info, or -1 if the selection has no single shared class
	int getPropClass() const;
	void setPropClass(int prop_class);

	// spawn/despawn cues - edited only for a single selected prop (see getSelectedPropObject).
	// The formula getters return the sexp node index to load into the tree; the dirty-setters take
	// the index produced by the tree's save_tree() and store it on the prop, freeing the old cue.
	int getSpawnFormula() const;
	int getDespawnFormula() const;
	void setSpawnTreeDirty(int formula);
	void setDespawnTreeDirty(int formula);
	int getSpawnDelay() const;
	void setSpawnDelay(int delay);
	int getDespawnDelay() const;
	void setDespawnDelay(int delay);

 signals:
	void modelDataChanged();

 private slots:
	void onSelectedObjectChanged(int);
	void onSelectedObjectMarkingChanged(int, bool);
	void onMissionChanged();

 private: // NOLINT(readability-redundant-access-specifiers)
	void initializeData();
	void showErrorDialogNoCancel(const SCP_string& message);
	void selectPropFromObjectList(object* start, bool forward);
	void selectFirstPropInMission();
	SCP_vector<int> computeSelectedPropObjects() const;
	static int tristate_set(bool value, int current_state);

	SCP_string _propName;
	SCP_vector<std::pair<SCP_string, size_t>> _flagLabels;
	SCP_vector<int> _flagState;
	SCP_vector<int> _selectedPropObjects;
	bool _bypass_errors = false;
	int _propClass = -1;
	int _spawnFormula = -1;
	int _despawnFormula = -1;
	int _spawnDelay = 0;
	int _despawnDelay = 0;
};

}
