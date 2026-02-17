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
	if (!hasValidSelection()) {
		return true;
	}

	if (!validateData()) {
		return false;
	}

	auto this_instance = Objects[_editor->currentObject].instance;
	auto prp = prop_id_lookup(this_instance);
	Assertion(prp != nullptr, "Selected prop could not be found.");

	strcpy_s(prp->prop_name, _propName.c_str());

	for (size_t i = 0; i < _flagLabels.size(); ++i) {
		auto flag_index = _flagLabels[i].second;
		if (flag_index >= Num_parse_prop_flags) {
			continue;
		}

		auto& def = Parse_prop_flags[flag_index];
		if (!stricmp(def.name, "no_collide")) {
			Objects[_editor->currentObject].flags.set(Object::Object_Flags::Collides, !_flagState[i]);
		}
	}

	_editor->missionChanged();
	return true;
}

void PropEditorDialogModel::reject() {
	// no-op
}

void PropEditorDialogModel::initializeData() {
	_flagLabels.clear();
	_flagState.clear();

	for (size_t i = 0; i < Num_parse_prop_flags; ++i) {
		auto& def = Parse_prop_flags[i];
		auto& desc = Parse_prop_flag_descriptions[i];
		SCP_string label = def.name;
		label += " (";
		label += desc.flag_desc;
		label += ")";
		_flagLabels.emplace_back(label, i);
		_flagState.push_back(false);
	}

	if (hasValidSelection()) {
		auto prp = prop_id_lookup(Objects[_editor->currentObject].instance);
		Assertion(prp != nullptr, "Selected prop could not be found.");
		_propName = prp->prop_name;

		for (size_t i = 0; i < _flagLabels.size(); ++i) {
			auto flag_index = _flagLabels[i].second;
			auto& def = Parse_prop_flags[flag_index];
			if (!stricmp(def.name, "no_collide")) {
				_flagState[i] = !Objects[_editor->currentObject].flags[Object::Object_Flags::Collides];
			}
		}
	} else {
		_propName.clear();
	}

	Q_EMIT modelDataChanged();
}

bool PropEditorDialogModel::validateData() {
	_bypass_errors = false;

	SCP_trim(_propName);
	if (_propName.empty()) {
		showErrorDialogNoCancel("A prop name cannot be empty.");
		return false;
	}

	auto this_instance = Objects[_editor->currentObject].instance;
	for (size_t i = 0; i < Props.size(); ++i) {
		if (static_cast<int>(i) == this_instance || !Props[i].has_value()) {
			continue;
		}

		if (!stricmp(_propName.c_str(), Props[i].value().prop_name)) {
			showErrorDialogNoCancel("This prop name is already being used by another prop.");
			return false;
		}
	}

	return true;
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

bool PropEditorDialogModel::hasValidSelection() const {
	return query_valid_object(_editor->currentObject) && Objects[_editor->currentObject].type == OBJ_PROP;
}

const SCP_string& PropEditorDialogModel::getPropName() const {
	return _propName;
}

void PropEditorDialogModel::setPropName(const SCP_string& name) {
	modify(_propName, name);
}

const SCP_vector<std::pair<SCP_string, size_t>>& PropEditorDialogModel::getFlagLabels() const {
	return _flagLabels;
}

const SCP_vector<bool>& PropEditorDialogModel::getFlagState() const {
	return _flagState;
}

void PropEditorDialogModel::setFlagState(size_t index, bool enabled) {
	if (!SCP_vector_inbounds(_flagState, index)) {
		return;
	}

	if (_flagState[index] != enabled) {
		_flagState[index] = enabled;
		set_modified();
		Q_EMIT modelChanged();
	}
}

void PropEditorDialogModel::selectNextProp() {
	if (!hasValidSelection()) {
		return;
	}

	if (apply()) {
		selectPropFromObjectList(GET_NEXT(&Objects[_editor->currentObject]), true);
	}
}

void PropEditorDialogModel::selectPreviousProp() {
	if (!hasValidSelection()) {
		return;
	}

	if (apply()) {
		selectPropFromObjectList(GET_PREV(&Objects[_editor->currentObject]), false);
	}
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

}
