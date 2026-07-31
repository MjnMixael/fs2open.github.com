#include "CheckpointsDialog.h"

#include "ui_CheckpointsDialog.h"

#include "ui/util/SignalBlockers.h"

#include <QCloseEvent>

namespace fso::fred::dialogs {

CheckpointsDialog::CheckpointsDialog(QWidget* parent, EditorViewport* viewport)
	: QDialog(parent), ui(new Ui::CheckpointsDialog()), _model(new CheckpointsDialogModel(this, viewport))
{
	ui->setupUi(this);
}

CheckpointsDialog::~CheckpointsDialog() = default;

void CheckpointsDialog::setInitial(const CheckpointSettings& settings)
{
	_model->setInitial(settings);
	syncUiFromModel();
}

CheckpointSettings CheckpointsDialog::settings() const
{
	return _model->settings();
}

void CheckpointsDialog::closeEvent(QCloseEvent* e)
{
	reject();
	// reject() hides the dialog when it actually closes. Let that close
	// proceed (so a dialog created with WA_DeleteOnClose is destroyed),
	// and only veto it when reject() decided to keep the dialog open (e.g.
	// the user cancelled the unsaved-changes prompt).
	if (isVisible()) {
		e->ignore();
	} else {
		e->accept();
	}
}

void CheckpointsDialog::syncUiFromModel()
{
	util::SignalBlockers blockers(this);

	const auto& s = _model->settings();
	ui->disallowInCampaignCheck->setChecked(s.disallowInCampaign);
	ui->disallowInSimulatorCheck->setChecked(s.disallowInSimulator);
	ui->noResumePromptCheck->setChecked(s.noResumePrompt);
	ui->keepPlayerLoadoutCheck->setChecked(s.keepPlayerLoadout);
	ui->keepWingLoadoutCheck->setChecked(s.keepWingLoadout);
	ui->deleteOnCompletionCheck->setChecked(s.deleteOnCompletion);

	updateControlStates();
}

void CheckpointsDialog::updateControlStates()
{
	const auto& s = _model->settings();

	// With both modes switched off there is no situation left in which a checkpoint can be
	// written or read, so the rest of the dialog has nothing to act on.
	const bool anyModeAllowed = !(s.disallowInCampaign && s.disallowInSimulator);
	ui->resumeGroup->setEnabled(anyModeAllowed);
	ui->lifetimeGroup->setEnabled(anyModeAllowed);

	// The two loadout options only matter for the entry prompt, which the first box suppresses.
	// load-checkpoint takes its own options, so those are unaffected either way.
	const bool promptShown = anyModeAllowed && !s.noResumePrompt;
	ui->keepPlayerLoadoutCheck->setEnabled(promptShown);
	ui->keepWingLoadoutCheck->setEnabled(promptShown);
}

void CheckpointsDialog::on_okAndCancelButtons_accepted()
{
	accept();
}

void CheckpointsDialog::on_okAndCancelButtons_rejected()
{
	reject();
}

void CheckpointsDialog::on_disallowInCampaignCheck_toggled(bool value)
{
	_model->setDisallowInCampaign(value);
	updateControlStates();
}

void CheckpointsDialog::on_disallowInSimulatorCheck_toggled(bool value)
{
	_model->setDisallowInSimulator(value);
	updateControlStates();
}

void CheckpointsDialog::on_noResumePromptCheck_toggled(bool value)
{
	_model->setNoResumePrompt(value);
	updateControlStates();
}

void CheckpointsDialog::on_keepPlayerLoadoutCheck_toggled(bool value)
{
	_model->setKeepPlayerLoadout(value);
}

void CheckpointsDialog::on_keepWingLoadoutCheck_toggled(bool value)
{
	_model->setKeepWingLoadout(value);
}

void CheckpointsDialog::on_deleteOnCompletionCheck_toggled(bool value)
{
	_model->setDeleteOnCompletion(value);
}

} // namespace fso::fred::dialogs
