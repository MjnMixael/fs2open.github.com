#pragma once

#include "BackgroundEditorDialogModel.h"

#include <QPointer>
#include <QUndoCommand>

#include <typeinfo>
#include <utility>

namespace fso::fred::dialogs {

// Undo command for background-editor changes. Lives on the main undo stack and
// may outlive the dialog, so it restores through the model's static path and
// only uses the (guarded) model pointer to resync an open dialog. Shared by the
// dialog's spinbox edits and the viewport's mouse drags.
class BackgroundEditCommand : public QUndoCommand {
	QPointer<BackgroundEditorDialogModel> _model;
	Editor* _editor;
	QByteArray _before, _after;
	int _fieldId;
	bool _skipFirstRedo;

	void apply(const QByteArray& data) {
		const auto selection = BackgroundEditorDialogModel::restoreGlobalState(data, _editor);
		if (_model) {
			_model->applyRestoredSelection(selection.first, selection.second);
		}
	}

public:
	BackgroundEditCommand(BackgroundEditorDialogModel* model, Editor* editor,
	                      QByteArray before, QByteArray after,
	                      int fieldId, const QString& text,
	                      bool skipFirstRedo = true)
		: QUndoCommand(text), _model(model), _editor(editor), _before(std::move(before)),
		  _after(std::move(after)), _fieldId(fieldId), _skipFirstRedo(skipFirstRedo) {}

	void undo() override { apply(_before); }
	void redo() override {
		if (_skipFirstRedo) { _skipFirstRedo = false; return; }
		apply(_after);
	}

	bool mergeWith(const QUndoCommand* other) override {
		if (_fieldId < 0) return false;
		if (typeid(*other) != typeid(*this)) return false;
		const auto* o = static_cast<const BackgroundEditCommand*>(other);
		if (o->_fieldId != _fieldId) return false;
		_after = o->_after;
		return true;
	}

	int id() const override { return _fieldId; }
};

} // namespace fso::fred::dialogs
