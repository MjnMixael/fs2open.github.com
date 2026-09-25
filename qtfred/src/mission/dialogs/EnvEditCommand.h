#pragma once

#include "AsteroidEditorDialogModel.h"
#include "VolumetricNebulaDialogModel.h"

#include "mission/EditorViewport.h"

#include <QUndoCommand>

#include <utility>

namespace fso::fred::dialogs {

// Undo command for an environment gizmo edit (volumetric nebula position,
// asteroid field bounds) made with no editor dialog open. Lives on the main
// undo stack. The snapshot covers only the fields a gizmo can change, so it
// restores through the model's static path with no dialog alive. If the
// matching dialog is open by the time it runs, that dialog's working copy and
// Cancel baseline are moved to the restored values too, so a later OK or
// Cancel can't bring the undone edit back.
//
// A gizmo edit made while the dialog is open goes on the dialog's own stack
// instead (see EditorViewport::commitEnvEdit).
class EnvEditCommand : public QUndoCommand {
public:
	enum class Kind { VolumetricNebula, AsteroidField };

private:
	Kind _kind;
	EditorViewport* _viewport;
	QByteArray _before, _after;
	bool _skipFirstRedo;

	void apply(const QByteArray& data) {
		if (_kind == Kind::VolumetricNebula) {
			VolumetricNebulaDialogModel::restoreGizmoState(data);
			if (auto* model = _viewport->volumetricEditModel()) {
				model->syncGizmoFromGlobals(true);
			}
		} else {
			AsteroidEditorDialogModel::restoreGizmoState(data);
			if (auto* model = _viewport->asteroidEditModel()) {
				model->syncGizmoFromGlobals(true);
			}
		}
		if (_viewport->editor != nullptr) {
			_viewport->editor->missionChanged();
		}
		_viewport->needsUpdate();
	}

public:
	EnvEditCommand(Kind kind, EditorViewport* viewport, QByteArray before, QByteArray after, const QString& text,
		bool skipFirstRedo = true)
		: QUndoCommand(text), _kind(kind), _viewport(viewport), _before(std::move(before)), _after(std::move(after)),
		  _skipFirstRedo(skipFirstRedo)
	{
	}

	void undo() override { apply(_before); }
	void redo() override {
		if (_skipFirstRedo) {
			_skipFirstRedo = false;
			return;
		}
		apply(_after);
	}

	// Deliberately not mergeable: one gesture (press to release) is one undo
	// step, the same rule the background drag uses.
	int id() const override { return -1; }
};

} // namespace fso::fred::dialogs
