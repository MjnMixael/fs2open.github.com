#include "CheckpointsDialogModel.h"

namespace fso::fred::dialogs {

CheckpointsDialogModel::CheckpointsDialogModel(QObject* parent, EditorViewport* viewport)
	: AbstractDialogModel(parent, viewport)
{
}

bool CheckpointsDialogModel::apply()
{
	// No direct application; the parent MissionSpecDialogModel reads settings() and applies them.
	return true;
}

void CheckpointsDialogModel::reject()
{
	// No direct rejection; the parent simply discards this model on cancel/close.
}

void CheckpointsDialogModel::setInitial(const CheckpointSettings& settings)
{
	_working = settings;
}

const CheckpointSettings& CheckpointsDialogModel::settings() const
{
	return _working;
}

void CheckpointsDialogModel::setNoResumePrompt(bool value)
{
	modify(_working.noResumePrompt, value);
}

void CheckpointsDialogModel::setKeepPlayerLoadout(bool value)
{
	modify(_working.keepPlayerLoadout, value);
}

void CheckpointsDialogModel::setKeepWingLoadout(bool value)
{
	modify(_working.keepWingLoadout, value);
}

void CheckpointsDialogModel::setDeleteOnCompletion(bool value)
{
	modify(_working.deleteOnCompletion, value);
}

void CheckpointsDialogModel::setDisallowInCampaign(bool value)
{
	modify(_working.disallowInCampaign, value);
}

void CheckpointsDialogModel::setDisallowInSimulator(bool value)
{
	modify(_working.disallowInSimulator, value);
}

} // namespace fso::fred::dialogs
