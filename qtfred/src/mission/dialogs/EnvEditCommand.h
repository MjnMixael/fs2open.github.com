#pragma once

#include "AsteroidEditorDialogModel.h"
#include "VolumetricNebulaDialogModel.h"

#include <QPointer>
#include <QUndoCommand>

#include <typeinfo>
#include <utility>

namespace fso::fred::dialogs {

// Undo command for the always-on environment gizmos (volumetric nebula centre,
// asteroid field bounds). Mirrors BackgroundEditCommand: it lives on the main
// undo stack and may outlive the dialog, so it restores through the model's
// static global path and only uses the (guarded) model pointer to resync a
// dialog that happens to be open.
//
// Kind selects which globals the snapshot covers. Both kinds are handled here
// rather than in two near-identical classes because the only difference is
// which static pair to call.
class EnvEditCommand : public QUndoCommand {
public:
	enum class Kind { VolumetricNebula, AsteroidField };

private:
	Kind _kind;
	QPointer<VolumetricNebulaDialogModel> _volModel;
	QPointer<AsteroidEditorDialogModel> _astModel;
	Editor* _editor;
	QByteArray _before, _after;
	bool _skipFirstRedo;

	void apply(const QByteArray& data) {
		if (_kind == Kind::VolumetricNebula) {
			VolumetricNebulaDialogModel::restoreGlobalState(data);
			if (_volModel) {
				_volModel->resyncFromGlobals();
			}
		} else {
			AsteroidEditorDialogModel::restoreGlobalState(data);
			if (_astModel) {
				_astModel->resyncFromGlobals();
			}
		}
		if (_editor != nullptr) {
			_editor->missionChanged();
		}
	}

public:
	EnvEditCommand(Kind kind, VolumetricNebulaDialogModel* volModel, AsteroidEditorDialogModel* astModel,
		Editor* editor, QByteArray before, QByteArray after, const QString& text,
		bool skipFirstRedo = true)
		: QUndoCommand(text), _kind(kind), _volModel(volModel), _astModel(astModel), _editor(editor),
		  _before(std::move(before)), _after(std::move(after)), _skipFirstRedo(skipFirstRedo)
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

	// Deliberately not mergeable: one gesture (press to release) is one drag is
	// one undo step, the same rule the background drag uses.
	int id() const override { return -1; }
};

} // namespace fso::fred::dialogs
