#include "ControlsDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace fso {
namespace fred {
namespace dialogs {

ControlsDialog::ControlsDialog(QWidget* parent) : QDialog(parent) {
	setWindowTitle(tr("Controls"));
	resize(500, 400);

	auto* layout = new QVBoxLayout(this);
	auto* form = new QFormLayout();
	layout->addLayout(form);

	auto& bindings = ControlBindings::instance();
	for (const auto& def : bindings.definitions()) {
		auto* edit = new QKeySequenceEdit(bindings.keyFor(def.action), this);
		_editors.emplace(def.action, edit);
		form->addRow(def.label + ':', edit);
	}

	auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	auto* resetButton = buttonBox->addButton(tr("Reset to Defaults"), QDialogButtonBox::ResetRole);

	connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
		applyChanges();
		accept();
	});
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(resetButton, &QPushButton::clicked, this, &ControlsDialog::resetDefaults);

	layout->addWidget(buttonBox);
}

void ControlsDialog::applyChanges() {
	auto& bindings = ControlBindings::instance();
	for (const auto& editor : _editors) {
		bindings.setKey(editor.first, editor.second->keySequence());
	}
	bindings.save();
}

void ControlsDialog::resetDefaults() {
	auto& bindings = ControlBindings::instance();
	bindings.resetToDefaults();
	for (const auto& def : bindings.definitions()) {
		auto it = _editors.find(def.action);
		if (it != _editors.end()) {
			it->second->setKeySequence(bindings.keyFor(def.action));
		}
	}
}

} // namespace dialogs
} // namespace fred
} // namespace fso
