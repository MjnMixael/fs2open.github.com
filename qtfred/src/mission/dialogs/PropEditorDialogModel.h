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
	const SCP_string& getPropName() const;
	void setPropName(const SCP_string& name);

	const SCP_vector<std::pair<SCP_string, size_t>>& getFlagLabels() const;
	const SCP_vector<bool>& getFlagState() const;
	void setFlagState(size_t index, bool enabled);

	void selectNextProp();
	void selectPreviousProp();

 signals:
	void modelDataChanged();

 private slots:
	void onSelectedObjectChanged(int);
	void onSelectedObjectMarkingChanged(int, bool);
	void onMissionChanged();

 private: // NOLINT(readability-redundant-access-specifiers)
	void initializeData();
	bool validateData();
	void showErrorDialogNoCancel(const SCP_string& message);
	void selectPropFromObjectList(object* start, bool forward);

	SCP_string _propName;
	SCP_vector<std::pair<SCP_string, size_t>> _flagLabels;
	SCP_vector<bool> _flagState;
	bool _bypass_errors = false;
};

}
