#pragma once

#include "mission/dialogs/AbstractDialogModel.h"
#include "mission/dialogs/MissionSpecDialogModel.h"

namespace fso::fred::dialogs {

class CheckpointsDialogModel final : public AbstractDialogModel {
  public:
	CheckpointsDialogModel(QObject* parent, EditorViewport* viewport);

	bool apply() override;
	void reject() override;

	void setInitial(const CheckpointSettings& settings);
	const CheckpointSettings& settings() const;

	void setNoResumePrompt(bool value);
	void setKeepPlayerLoadout(bool value);
	void setKeepWingLoadout(bool value);
	void setDeleteOnCompletion(bool value);
	void setDisallowInCampaign(bool value);
	void setDisallowInSimulator(bool value);

  private:
	CheckpointSettings _working;
};

} // namespace fso::fred::dialogs
