#pragma once

#include "mission/dialogs/MissionSpecs/CheckpointsDialogModel.h"

#include <QDialog>

namespace fso::fred::dialogs {

namespace Ui {
class CheckpointsDialog;
}

class CheckpointsDialog : public QDialog {
	Q_OBJECT
  public:
	explicit CheckpointsDialog(QWidget* parent, EditorViewport* viewport);
	~CheckpointsDialog() override;

	void setInitial(const CheckpointSettings& settings);
	CheckpointSettings settings() const;

  protected:
	void closeEvent(QCloseEvent* e) override;

  private slots:
	void on_okAndCancelButtons_accepted();
	void on_okAndCancelButtons_rejected();
	void on_disallowInCampaignCheck_toggled(bool);
	void on_disallowInSimulatorCheck_toggled(bool);
	void on_noResumePromptCheck_toggled(bool);
	void on_keepPlayerLoadoutCheck_toggled(bool);
	void on_keepWingLoadoutCheck_toggled(bool);
	void on_deleteOnCompletionCheck_toggled(bool);

  private: // NOLINT(readability-redundant-access-specifiers)
	void syncUiFromModel();
	void updateControlStates();

	std::unique_ptr<Ui::CheckpointsDialog> ui;
	std::unique_ptr<CheckpointsDialogModel> _model;
};

} // namespace fso::fred::dialogs
