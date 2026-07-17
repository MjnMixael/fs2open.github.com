#include "mission/dialogs/PropEditorDialogModel.h"

#include <globalincs/linklist.h>
#include <mission/object.h>
#include <prop/prop.h>

namespace fso::fred::dialogs {

PropEditorDialogModel::PropEditorDialogModel(QObject* parent, EditorViewport* viewport) : AbstractDialogModel(parent, viewport) {
	connect(viewport->editor, &Editor::currentObjectChanged, this, &PropEditorDialogModel::onSelectedObjectChanged);
	connect(viewport->editor, &Editor::objectMarkingChanged, this, &PropEditorDialogModel::onSelectedObjectMarkingChanged);
	connect(viewport->editor, &Editor::missionChanged, this, &PropEditorDialogModel::onMissionChanged);

	initializeData();
}

bool PropEditorDialogModel::apply() {
	return true;
}

void PropEditorDialogModel::reject() {}


void PropEditorDialogModel::initializeData() {
	_flagLabels.clear();
	_flagState.clear();
	_selectedPropObjects = computeSelectedPropObjects();

	for (size_t i = 0; i < Num_parse_prop_flags; ++i) {
		_flagLabels.emplace_back(Parse_prop_flags[i].name, i);
		_flagState.push_back(Qt::Unchecked);
	}

	// cues are only meaningful for a single selected prop
	_spawnFormula = -1;
	_despawnFormula = -1;
	_spawnDelay = 0;
	_despawnDelay = 0;

	// prop class: shared class across the selection, or -1 if mixed/none
	_propClass = -1;
	{
		bool first = true;
		for (auto obj_idx : _selectedPropObjects) {
			if (!query_valid_object(obj_idx) || Objects[obj_idx].type != OBJ_PROP)
				continue;
			auto prp = prop_id_lookup(Objects[obj_idx].instance);
			if (prp == nullptr)
				continue;
			if (first) {
				_propClass = prp->prop_info_index;
				first = false;
			} else if (_propClass != prp->prop_info_index) {
				_propClass = -1;
				break;
			}
		}
	}

	if (hasValidSelection()) {
		if (!hasMultipleSelection()) {
			auto prp = prop_id_lookup(Objects[_selectedPropObjects.front()].instance);
			Assertion(prp != nullptr, "Selected prop could not be found.");
			_propName = prp->prop_name;
			_spawnFormula = prp->spawn_cue;
			_despawnFormula = prp->despawn_cue;
			_spawnDelay = prp->spawn_delay;
			_despawnDelay = prp->despawn_delay;
		} else {
			_propName.clear();
		}

		for (size_t i = 0; i < _flagLabels.size(); ++i) {
			bool first = true;
			for (auto obj_idx : _selectedPropObjects) {
				if (!query_valid_object(obj_idx) || Objects[obj_idx].type != OBJ_PROP) {
					continue;
				}

				auto value = getFlagValueForObject(Objects[obj_idx], _flagLabels[i].second);
				if (first) {
					_flagState[i] = value ? Qt::Checked : Qt::Unchecked;
					first = false;
				} else {
					_flagState[i] = tristate_set(value, _flagState[i]);
				}
			}
		}
	} else {
		_propName.clear();
	}

	Q_EMIT modelDataChanged();
	_modified = false;
}


void PropEditorDialogModel::showErrorDialogNoCancel(const SCP_string& message) {
	if (_bypass_errors) {
		return;
	}

	_bypass_errors = true;
	_viewport->dialogProvider->showButtonDialog(DialogType::Error, "Error", message, {DialogButton::Ok});
}

void PropEditorDialogModel::selectPropFromObjectList(object* start, bool forward) {
	auto ptr = start;

	while (ptr != END_OF_LIST(&obj_used_list)) {
		if (ptr->type == OBJ_PROP) {
			_editor->unmark_all();
			_editor->markObject(OBJ_INDEX(ptr));
			return;
		}
		ptr = forward ? GET_NEXT(ptr) : GET_PREV(ptr);
	}

	ptr = forward ? GET_FIRST(&obj_used_list) : GET_LAST(&obj_used_list);
	while (ptr != END_OF_LIST(&obj_used_list)) {
		if (ptr->type == OBJ_PROP) {
			_editor->unmark_all();
			_editor->markObject(OBJ_INDEX(ptr));
			return;
		}
		ptr = forward ? GET_NEXT(ptr) : GET_PREV(ptr);
	}
}

void PropEditorDialogModel::selectFirstPropInMission() {
	for (auto* ptr = GET_FIRST(&obj_used_list); ptr != END_OF_LIST(&obj_used_list); ptr = GET_NEXT(ptr)) {
		if (ptr->type == OBJ_PROP) {
			_editor->unmark_all();
			_editor->markObject(OBJ_INDEX(ptr));
			return;
		}
	}
}

const SCP_vector<int>& PropEditorDialogModel::getSelectedPropObjects() const {
	return _selectedPropObjects;
}

SCP_vector<int> PropEditorDialogModel::computeSelectedPropObjects() const {
	SCP_vector<int> selected;
	for (auto* ptr = GET_FIRST(&obj_used_list); ptr != END_OF_LIST(&obj_used_list); ptr = GET_NEXT(ptr)) {
		if (ptr->type == OBJ_PROP && ptr->flags[Object::Object_Flags::Marked]) {
			selected.push_back(OBJ_INDEX(ptr));
		}
	}

	if (selected.empty() && query_valid_object(_editor->currentObject) && Objects[_editor->currentObject].type == OBJ_PROP) {
		selected.push_back(_editor->currentObject);
	}

	return selected;
}

bool PropEditorDialogModel::getFlagValueForObject(const object& obj, size_t flag_index) {
	if (flag_index >= Num_parse_prop_flags) {
		return false;
	}

	auto& def = Parse_prop_flags[flag_index];
	if (!stricmp(def.name, "no_collide")) {
		return !obj.flags[Object::Object_Flags::Collides];
	}

	return false;
}

int PropEditorDialogModel::tristate_set(bool value, int current_state) {
	if (value) {
		if (current_state == Qt::Unchecked) {
			return Qt::PartiallyChecked;
		}
	} else {
		if (current_state == Qt::Checked) {
			return Qt::PartiallyChecked;
		}
	}

	if (current_state == Qt::PartiallyChecked) {
		return Qt::PartiallyChecked;
	}

	return value ? Qt::Checked : Qt::Unchecked;
}

bool PropEditorDialogModel::hasValidSelection() const {
	return !_selectedPropObjects.empty();
}

bool PropEditorDialogModel::hasMultipleSelection() const {
	return _selectedPropObjects.size() > 1;
}

int PropEditorDialogModel::getSelectedPropObject() const {
	if (_selectedPropObjects.size() != 1) {
		return -1;
	}
	return _selectedPropObjects.front();
}

bool PropEditorDialogModel::hasAnyPropsInMission() {
	for (auto* ptr = GET_FIRST(&obj_used_list); ptr != END_OF_LIST(&obj_used_list); ptr = GET_NEXT(ptr)) {
		if (ptr->type == OBJ_PROP) {
			return true;
		}
	}

	return false;
}

const SCP_string& PropEditorDialogModel::getPropName() const {
	return _propName;
}

bool PropEditorDialogModel::setPropName(const SCP_string& name) {
	if (hasMultipleSelection()) {
		return true;
	}

	_bypass_errors = false;

	SCP_string trimmed = name;
	SCP_trim(trimmed);

	if (trimmed.empty()) {
		showErrorDialogNoCancel("A prop name cannot be empty.");
		return false;
	}

	auto obj_idx = _selectedPropObjects.front();

	// prop names share a single namespace with ships, wings, waypoints, jump nodes, etc.
	SCP_string collision = fred_object_name_collision(trimmed.c_str(), obj_idx);
	if (!collision.empty()) {
		showErrorDialogNoCancel("This prop name is already being used by " + collision + ".");
		return false;
	}

	auto prp = prop_id_lookup(Objects[obj_idx].instance);
	if (prp == nullptr) {
		return false;
	}

	strcpy_s(prp->prop_name, trimmed.c_str());
	_propName = trimmed;
	set_modified();
	_editor->missionChanged();
	return true;
}

const SCP_vector<std::pair<SCP_string, size_t>>& PropEditorDialogModel::getFlagLabels() const {
	return _flagLabels;
}

const SCP_vector<int>& PropEditorDialogModel::getFlagState() const {
	return _flagState;
}

void PropEditorDialogModel::setFlagState(size_t index, int state) {
	if (!SCP_vector_inbounds(_flagState, index)) {
		return;
	}

	if (_flagState[index] == state) {
		return;
	}

	_flagState[index] = state;

	if (state == Qt::PartiallyChecked) {
		return;
	}

	auto flag_index = _flagLabels[index].second;
	if (flag_index >= Num_parse_prop_flags) {
		return;
	}

	auto& def = Parse_prop_flags[flag_index];
	for (auto obj_idx : _selectedPropObjects) {
		if (!query_valid_object(obj_idx) || Objects[obj_idx].type != OBJ_PROP) {
			continue;
		}
		if (!stricmp(def.name, "no_collide")) {
			Objects[obj_idx].flags.set(Object::Object_Flags::Collides, state != Qt::Checked);
		}
	}
	set_modified();
	// Caller is responsible for triggering missionChanged() (deferred to avoid FlagListWidget re-entrancy)
}

SCP_string PropEditorDialogModel::getLayer() const
{
	SCP_string result;
	bool first = true;
	for (auto obj_idx : _selectedPropObjects) {
		if (!query_valid_object(obj_idx) || Objects[obj_idx].type != OBJ_PROP)
			continue;
		SCP_string layer = _viewport->getObjectLayerName(obj_idx);
		if (first) {
			result = layer;
			first = false;
		} else if (result != layer) {
			return "";
		}
	}
	return result;
}

void PropEditorDialogModel::setLayer(const SCP_string& layer)
{
	for (auto obj_idx : _selectedPropObjects) {
		if (!query_valid_object(obj_idx) || Objects[obj_idx].type != OBJ_PROP)
			continue;
		_viewport->moveObjectToLayer(obj_idx, layer);
	}
	set_modified();
	_editor->missionChanged();
}

int PropEditorDialogModel::getPropClass() const
{
	return _propClass;
}

void PropEditorDialogModel::setPropClass(int prop_class)
{
	if (prop_class < 0 || prop_class >= prop_info_size())
		return;
	if (_propClass == prop_class)
		return;

	_propClass = prop_class;
	for (auto obj_idx : _selectedPropObjects) {
		if (!query_valid_object(obj_idx) || Objects[obj_idx].type != OBJ_PROP)
			continue;
		auto prp = prop_id_lookup(Objects[obj_idx].instance);
		if (prp == nullptr)
			continue;
		if (prp->prop_info_index != prop_class)
			change_prop_type(Objects[obj_idx].instance, prop_class);
	}

	set_modified();
	_editor->missionChanged();
	Q_EMIT modelDataChanged();
}

int PropEditorDialogModel::getSpawnFormula() const
{
	return _spawnFormula;
}

int PropEditorDialogModel::getDespawnFormula() const
{
	return _despawnFormula;
}

void PropEditorDialogModel::setSpawnTreeDirty(int formula)
{
	int obj_idx = getSelectedPropObject();
	if (obj_idx < 0)
		return;
	auto prp = prop_id_lookup(Objects[obj_idx].instance);
	if (prp == nullptr)
		return;

	_spawnFormula = formula;
	if (prp->spawn_cue >= 0 && prp->spawn_cue != formula)
		free_sexp2(prp->spawn_cue);
	prp->spawn_cue = formula;

	set_modified();
	_editor->missionChanged();
}

void PropEditorDialogModel::setDespawnTreeDirty(int formula)
{
	int obj_idx = getSelectedPropObject();
	if (obj_idx < 0)
		return;
	auto prp = prop_id_lookup(Objects[obj_idx].instance);
	if (prp == nullptr)
		return;

	_despawnFormula = formula;
	if (prp->despawn_cue >= 0 && prp->despawn_cue != formula)
		free_sexp2(prp->despawn_cue);
	prp->despawn_cue = formula;

	set_modified();
	_editor->missionChanged();
}

int PropEditorDialogModel::getSpawnDelay() const
{
	return _spawnDelay;
}

void PropEditorDialogModel::setSpawnDelay(int delay)
{
	int obj_idx = getSelectedPropObject();
	if (obj_idx < 0)
		return;
	auto prp = prop_id_lookup(Objects[obj_idx].instance);
	if (prp == nullptr)
		return;
	if (delay < 0)
		delay = 0;

	_spawnDelay = delay;
	prp->spawn_delay = delay; // FRED stores the positive value; the game negates it on load
	set_modified();
	_editor->missionChanged();
}

int PropEditorDialogModel::getDespawnDelay() const
{
	return _despawnDelay;
}

void PropEditorDialogModel::setDespawnDelay(int delay)
{
	int obj_idx = getSelectedPropObject();
	if (obj_idx < 0)
		return;
	auto prp = prop_id_lookup(Objects[obj_idx].instance);
	if (prp == nullptr)
		return;
	if (delay < 0)
		delay = 0;

	_despawnDelay = delay;
	prp->despawn_delay = delay;
	set_modified();
	_editor->missionChanged();
}

void PropEditorDialogModel::selectNextProp() {
	if (!hasValidSelection()) {
		if (hasAnyPropsInMission()) {
			selectFirstPropInMission();
		}
		return;
	}
	selectPropFromObjectList(GET_NEXT(&Objects[_selectedPropObjects.front()]), true);
}

void PropEditorDialogModel::selectPreviousProp() {
	if (!hasValidSelection()) {
		if (hasAnyPropsInMission()) {
			selectFirstPropInMission();
		}
		return;
	}
	selectPropFromObjectList(GET_PREV(&Objects[_selectedPropObjects.front()]), false);
}

void PropEditorDialogModel::onSelectedObjectChanged(int) {
	initializeData();
}

void PropEditorDialogModel::onSelectedObjectMarkingChanged(int, bool) {
	initializeData();
}

void PropEditorDialogModel::onMissionChanged() {
	initializeData();
}

SCP_vector<std::pair<SCP_string, SCP_string>> PropEditorDialogModel::getPropFlagDescriptions()
{
	const size_t num_descs = Num_parse_prop_flag_descriptions;
	SCP_vector<std::pair<SCP_string, SCP_string>> descriptions;
	descriptions.reserve(Num_parse_prop_flags);
	for (size_t i = 0; i < Num_parse_prop_flags; ++i) {
		const auto& flagDef = Parse_prop_flags[i];
		for (size_t j = 0; j < num_descs; ++j) {
			if (Parse_prop_flag_descriptions[j].def == flagDef.def) {
				descriptions.emplace_back(flagDef.name, Parse_prop_flag_descriptions[j].flag_desc);
				break;
			}
		}
	}
	return descriptions;
}

}
