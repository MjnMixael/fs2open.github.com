#include "ui/dialogs/PropEditorDialog.h"
#include "ui/dialogs/PropTextureReplacementDialog.h"

#include <ui/util/DialogUndo.h>

#include "ui_PropEditorDialog.h"

#include <globalincs/globals.h>
#include <mission/commands/FredCommands.h>
#include <mission/missionparse.h>
#include <mission/object.h>
#include <object/object.h>
#include <prop/prop.h>
#include <ui/util/SignalBlockers.h>
#include <ui/widgets/FlagList.h>
#include <ui/widgets/sexp_tree_view.h>

#include <QMetaObject>

namespace fso::fred::dialogs {

PropEditorDialog::PropEditorDialog(FredView* parent, EditorViewport* viewport)
	: QDialog(parent), SexpTreeEditorInterface(flagset<TreeFlags>()), _fredView(parent), _viewport(viewport),
	  ui(new ::Ui::PropEditorDialog()), _model(new PropEditorDialogModel(this, viewport)) {

	ui->setupUi(this);
	util::installMainStackUndoShortcuts(this, _fredView->mainUndoStack());

	ui->propNameLineEdit->setMaxLength(NAME_LENGTH - 1);

	// SEXP operator help panel, toggled via Preferences (same as the Ship/Wing editors)
	ui->cueHelpTitle->setVisible(viewport->Show_sexp_help_prop_editor);
	ui->cueHelpText->setVisible(viewport->Show_sexp_help_prop_editor);

	initializeUi();
	updateUi();
	updateCues();

	connect(_model.get(), &PropEditorDialogModel::modelDataChanged, this, [this]() {
		initializeUi();
		updateUi();
	});

	// Reload the cue trees only when the selection changes, not on every mission edit, so editing a
	// tree doesn't reset it out from under the user.
	connect(viewport->editor, &Editor::currentObjectChanged, this, [this](int) { updateCues(); });
	connect(viewport->editor, &Editor::objectMarkingChanged, this, [this](int, bool) { updateCues(); });

	connect(ui->spawnCueTree, &sexp_tree_view::modified, this, &PropEditorDialog::on_spawnCueTree_modified);
	connect(ui->spawnCueTree, &sexp_tree_view::helpChanged, this, &PropEditorDialog::on_spawnCueTree_helpChanged);
	connect(ui->spawnCueTree, &sexp_tree_view::miniHelpChanged, this, &PropEditorDialog::on_spawnCueTree_miniHelpChanged);
	connect(ui->despawnCueTree, &sexp_tree_view::modified, this, &PropEditorDialog::on_despawnCueTree_modified);
	connect(ui->despawnCueTree, &sexp_tree_view::helpChanged, this, &PropEditorDialog::on_despawnCueTree_helpChanged);
	connect(ui->despawnCueTree, &sexp_tree_view::miniHelpChanged, this, &PropEditorDialog::on_despawnCueTree_miniHelpChanged);

	connect(ui->propFlagsListWidget, &fso::fred::FlagListWidget::flagsChanged, this,
		[this](const QVector<std::pair<QString, int>>& snapshot) {
			const auto& labels = _model->getFlagLabels();
			const auto& selectedObjs = _model->getSelectedPropObjects();

			auto* cmd = new FieldEditCommand<bool>(
				FieldId::Prop_Flags, _viewport->editor, tr("Change Prop Flag"), true);
			// Merge-identity key: the selected prop signatures, so flag edits
			// merge only while the selection is unchanged.
			SCP_string targetKey;
			for (int obj_idx : selectedObjs) {
				if (query_valid_object(obj_idx)) {
					targetKey += std::to_string(Objects[obj_idx].signature);
					targetKey += ',';
				}
			}
			cmd->setTargetKey(std::move(targetKey));

			for (const auto& [name, newState] : snapshot) {
				if (newState == Qt::PartiallyChecked) continue;
				for (size_t i = 0; i < labels.size(); ++i) {
					if (name != QString::fromStdString(labels[i].first)) continue;
					const size_t flag_index = labels[i].second;
					for (int obj_idx : selectedObjs) {
						if (!query_valid_object(obj_idx) || Objects[obj_idx].type != OBJ_PROP) continue;
						const bool before = PropEditorDialogModel::getFlagValueForObject(Objects[obj_idx], flag_index);
						const bool after = (newState == Qt::Checked);
						if (before == after) continue;
						const int sig = Objects[obj_idx].signature;
						cmd->addEntry(before, after, [sig, flag_index](const bool& v) {
							const int cur = obj_get_by_signature(sig);
							if (cur < 0 || Objects[cur].type != OBJ_PROP) return;
							const auto& def = Parse_prop_flags[flag_index];
							if (!stricmp(def.name, "no_collide")) {
								Objects[cur].flags.set(Object::Object_Flags::Collides, !v);
							}
						});
					}
					_model->setFlagState(i, newState);
					break;
				}
			}

			if (!cmd->isEmpty()) {
				_fredView->mainUndoStack()->push(cmd);
			} else {
				delete cmd;
			}

			// Defer missionChanged to avoid re-entering FlagListWidget while it processes itemChanged.
			QMetaObject::invokeMethod(this, [this]() { _viewport->editor->missionChanged(); }, Qt::QueuedConnection);
		});

	resize(QDialog::sizeHint());
}

PropEditorDialog::~PropEditorDialog() = default;

void PropEditorDialog::changeEvent(QEvent* e)
{
	if (e->type() == QEvent::ActivationChange && isActiveWindow())
		_fredView->undoGroup()->setActiveStack(_fredView->mainUndoStack());
	QDialog::changeEvent(e);
}

void PropEditorDialog::initializeUi() {
	util::SignalBlockers blockers(this);

	const auto& labels = _model->getFlagLabels();
	QVector<std::pair<QString, int>> toWidget;
	toWidget.reserve(static_cast<int>(labels.size()));

	for (size_t i = 0; i < labels.size(); ++i) {
		toWidget.append({QString::fromStdString(labels[i].first), _model->getFlagState()[i]});
	}
	ui->propFlagsListWidget->setFlags(toWidget);

	const auto& descs = _model->getPropFlagDescriptions();
	QVector<std::pair<QString, QString>> qtDescs;
	qtDescs.reserve(static_cast<int>(descs.size()));
	for (const auto& d : descs)
		qtDescs.append({QString::fromUtf8(d.first.c_str()), QString::fromUtf8(d.second.c_str())});
	ui->propFlagsListWidget->setFlagDescriptions(qtDescs);

	ui->propFlagsListWidget->setFilterVisible(true);
	ui->propFlagsListWidget->setToolbarVisible(true);

	ui->layerCombo->clear();
	for (const auto& name : _viewport->getLayerNames()) {
		ui->layerCombo->addItem(QString::fromStdString(name), QString::fromStdString(name));
	}

	// prop class list (Prop_info is sorted by category, so this groups naturally); skip classes
	// flagged no_fred, matching how props are offered when adding them
	ui->propClassCombo->clear();
	for (int i = 0; i < prop_info_size(); ++i) {
		if (Prop_info[i].flags[Prop::Info_Flags::No_fred])
			continue;
		ui->propClassCombo->addItem(QString::fromStdString(Prop_info[i].name), QVariant(i));
	}

	const auto enable = _model->hasValidSelection();
	const auto has_props = _model->hasAnyPropsInMission();
	ui->propNameLineEdit->setEnabled(enable && !_model->hasMultipleSelection());
	ui->propFlagsListWidget->setEnabled(enable);
	ui->nextButton->setEnabled(has_props);
	ui->prevButton->setEnabled(has_props);
	ui->layerCombo->setEnabled(enable);
	ui->propClassCombo->setEnabled(enable);
	// texture replacement operates on a single prop
	ui->textureReplacementButton->setEnabled(_model->getSelectedPropObject() >= 0);
}

void PropEditorDialog::updateUi() {
	util::SignalBlockers blockers(this);

	ui->propNameLineEdit->setText(QString::fromStdString(_model->getPropName()));
	ui->layerCombo->setCurrentIndex(ui->layerCombo->findData(QString::fromStdString(_model->getLayer())));
	ui->propClassCombo->setCurrentIndex(ui->propClassCombo->findData(_model->getPropClass()));
}

void PropEditorDialog::on_propNameLineEdit_editingFinished() {
	const SCP_string newName = ui->propNameLineEdit->text().toUtf8().constData();
	const SCP_string oldName = _model->getPropName();
	if (newName == oldName) return;
	if (!_model->setPropName(newName)) {
		updateUi();
		return;
	}
	const auto& selected = _model->getSelectedPropObjects();
	if (selected.empty()) return;
	_fredView->mainUndoStack()->push(
		new RenameObjectCommand(selected.front(), oldName, newName, _viewport->editor, true));

	// Edit committed; clear the field's text-undo history so Ctrl+Z hits the mission stack.
	util::SignalBlockers b(this);
	ui->propNameLineEdit->setText(QString::fromStdString(_model->getPropName()));
}

void PropEditorDialog::on_nextButton_clicked() {
	_model->selectNextProp();
}

void PropEditorDialog::on_prevButton_clicked() {
	_model->selectPreviousProp();
}

void PropEditorDialog::on_layerCombo_currentIndexChanged(int index) {
	if (index < 0) return;
	const SCP_string newLayer = ui->layerCombo->itemData(index).toString().toStdString();

	SCP_vector<ObjectLayerChange> changes;
	for (int obj_idx : _model->getSelectedPropObjects()) {
		if (!query_valid_object(obj_idx) || Objects[obj_idx].type != OBJ_PROP) continue;
		SCP_string oldLayer = _viewport->getObjectLayerName(obj_idx);
		if (oldLayer == newLayer) continue;
		changes.push_back({ Objects[obj_idx].signature, std::move(oldLayer), newLayer });
	}
	if (changes.empty()) return;
	// MoveLayerCommand::redo() applies the change, so do not also call _model->setLayer().
	_fredView->mainUndoStack()->push(
		new MoveLayerCommand(std::move(changes), _viewport, _viewport->editor));
}

void PropEditorDialog::on_propClassCombo_currentIndexChanged(int index) {
	if (index < 0)
		return;
	_model->setPropClass(ui->propClassCombo->itemData(index).toInt());
}

void PropEditorDialog::on_textureReplacementButton_clicked() {
	const int propObjNum = _model->getSelectedPropObject();
	if (propObjNum < 0)
		return;

	auto dialog = new dialogs::PropTextureReplacementDialog(this, _viewport, propObjNum);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->show();
}

void PropEditorDialog::updateCues() {
	util::SignalBlockers blockers(this);

	// cues are edited for a single selected prop only
	const bool single = _model->getSelectedPropObject() >= 0;

	ui->spawnCueTree->initializeEditor(_viewport->editor, this, _viewport);
	ui->despawnCueTree->initializeEditor(_viewport->editor, this, _viewport);

	if (single) {
		ui->spawnCueTree->load_tree(_model->getSpawnFormula(), "true");
		ui->spawnCueTree->expandAll();
		ui->despawnCueTree->load_tree(_model->getDespawnFormula(), "false");
		ui->despawnCueTree->expandAll();
		ui->spawnDelaySpinBox->setValue(_model->getSpawnDelay());
		ui->despawnDelaySpinBox->setValue(_model->getDespawnDelay());
	} else {
		ui->spawnCueTree->clear_tree("");
		ui->despawnCueTree->clear_tree("");
		ui->spawnDelaySpinBox->setValue(0);
		ui->despawnDelaySpinBox->setValue(0);
	}

	ui->spawnCueTree->setEnabled(single);
	ui->despawnCueTree->setEnabled(single);
	ui->spawnDelaySpinBox->setEnabled(single);
	ui->despawnDelaySpinBox->setEnabled(single);
}

void PropEditorDialog::on_spawnCueTree_modified() {
	_model->setSpawnTreeDirty(ui->spawnCueTree->_model.save_tree());
}

void PropEditorDialog::on_spawnCueTree_helpChanged(const QString& help) {
	ui->cueHelpText->setPlainText(help);
}

void PropEditorDialog::on_spawnCueTree_miniHelpChanged(const QString& help) {
	ui->cueHelpTitle->setText(help);
}

void PropEditorDialog::on_spawnDelaySpinBox_valueChanged(int value) {
	_model->setSpawnDelay(value);
}

void PropEditorDialog::on_despawnCueTree_modified() {
	_model->setDespawnTreeDirty(ui->despawnCueTree->_model.save_tree());
}

void PropEditorDialog::on_despawnCueTree_helpChanged(const QString& help) {
	ui->cueHelpText->setPlainText(help);
}

void PropEditorDialog::on_despawnCueTree_miniHelpChanged(const QString& help) {
	ui->cueHelpTitle->setText(help);
}

void PropEditorDialog::on_despawnDelaySpinBox_valueChanged(int value) {
	_model->setDespawnDelay(value);
}

} // namespace fso::fred::dialogs
