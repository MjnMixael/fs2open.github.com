#include "LayerManagerDialog.h"
#include "ui_LayerManagerDialog.h"
#include "ui/util/KeyboardNavigation.h"

#include <QCheckBox>
#include <QInputDialog>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QUndoStack>

#include "mission/commands/FredCommands.h"
#include "ui/Theme.h"
#include "ui/util/DialogUndo.h"

namespace fso::fred::dialogs {

namespace {

// A toolbar lock icon for the current theme; faded marks a layer whose objects are only partly locked
QIcon layerLockIcon(const QString& baseName, bool faded)
{
	const QPixmap pm(QStringLiteral(":/images/toolbar/") + baseName +
		(currentThemeIsDark() ? QStringLiteral("-dark.png") : QStringLiteral("-light.png")));
	if (!faded)
		return {pm};
	QPixmap out(pm.size());
	out.fill(Qt::transparent);
	QPainter painter(&out);
	painter.setOpacity(0.4);
	painter.drawPixmap(0, 0, pm);
	painter.end();
	return {out};
}

} // namespace

LayerManagerDialog::LayerManagerDialog(FredView* parent, EditorViewport* viewport)
	: QDialog(parent)
	, _fredView(parent)
	, _viewport(viewport)
	, ui(new Ui::LayerManagerDialog())
	, _model(new LayerManagerDialogModel(this, viewport))
{
	ui->setupUi(this);
	util::installMainStackUndoShortcuts(this, _fredView->mainUndoStack());

	initializeUi();
	updateUi();

	connect(_model.get(), &LayerManagerDialogModel::modelChanged, this, &LayerManagerDialog::updateUi);

	// Undo/redo rewrites the layer structure behind our back, so refresh on any
	// mission change rather than only on our own model's signal.
	connect(_viewport->editor, &Editor::missionChanged, this, &LayerManagerDialog::updateUi);
	_uiReady = true;
}

LayerManagerDialog::~LayerManagerDialog() = default;

void LayerManagerDialog::changeEvent(QEvent* e)
{
	if (e->type() == QEvent::ActivationChange && isActiveWindow())
		_fredView->undoGroup()->setActiveStack(_fredView->mainUndoStack());
	if (e->type() == QEvent::PaletteChange && _uiReady)
		updateLockButton(); // theme switch: the light/dark icon set
	QDialog::changeEvent(e);
}

bool LayerManagerDialog::runStructureOp(const QString& text, const std::function<bool()>& op)
{
	auto* cmd = new LayerStructureCommand(_viewport, _viewport->editor, text);
	if (!op()) {
		delete cmd;
		return false;
	}
	cmd->captureAfter();
	_fredView->mainUndoStack()->push(cmd);
	return true;
}

void LayerManagerDialog::initializeUi() {
	// Populate IFF team checkboxes dynamically; insert before the spacer at the end of iffLayout
	const int iffCount = _model->getIffCount();
	const int spacerIndex = ui->iffLayout->count() - 1;
	for (int i = 0; i < iffCount; ++i) {
		auto* check = new QCheckBox(QString::fromStdString(_model->getIffName(i)), ui->iffScrollContents);
		connect(check, &QCheckBox::toggled, this, [this, i](bool checked) {
			_model->setShowIff(i, checked);
		});
		ui->iffLayout->insertWidget(spacerIndex + i, check);
		_iffChecks.append(check);
	}

	// Created after setupUi(), so they'd come after OK/Cancel; put them after the object types
	QList<QWidget*> chain{ui->showCoordinatePointsCheck};
	for (auto* check : _iffChecks)
		chain.append(check);
	chain.append(ui->buttonBox);
	util::setTabChain(chain);
}

void LayerManagerDialog::updateUi() {
	_refreshing = true;

	// Sync layer list
	const int previousRow = ui->layerList->currentRow();
	ui->layerList->clear();
	for (const auto& name : _model->getLayerNames()) {
		auto* item = new QListWidgetItem(QString::fromStdString(name), ui->layerList);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		item->setCheckState(_model->getLayerVisibility(name) ? Qt::Checked : Qt::Unchecked);
		if (_model->isDefaultLayer(name)) {
			item->setToolTip(tr("The default layer always exists and cannot be deleted."));
		}
	}
	if (ui->layerList->count() > 0) {
		ui->layerList->setCurrentRow(qMax(0, qMin(previousRow, ui->layerList->count() - 1)));
	}

	// Sync object type filter checkboxes
	{
		QSignalBlocker b1(ui->showShipsCheck);
		QSignalBlocker b2(ui->showStartsCheck);
		QSignalBlocker b3(ui->showWaypointsCheck);
		QSignalBlocker b4(ui->showPropsCheck);
		QSignalBlocker b5(ui->showJumpNodesCheck);
		QSignalBlocker b6(ui->showCoordinatePointsCheck);
		ui->showShipsCheck->setChecked(_model->getShowShips());
		ui->showStartsCheck->setChecked(_model->getShowStarts());
		ui->showWaypointsCheck->setChecked(_model->getShowWaypoints());
		ui->showPropsCheck->setChecked(_model->getShowProps());
		ui->showJumpNodesCheck->setChecked(_model->getShowJumpNodes());
		ui->showCoordinatePointsCheck->setChecked(_model->getShowCoordinatePoints());
	}

	// Sync IFF checkboxes
	for (int i = 0; i < _iffChecks.size(); ++i) {
		QSignalBlocker blocker(_iffChecks[i]);
		_iffChecks[i]->setChecked(_model->getShowIff(i));
	}

	_refreshing = false;

	// Update rename/delete buttons based on selection (row 0 is the default layer)
	const bool nonDefaultSelected = ui->layerList->currentRow() > 0;
	ui->renameLayerButton->setEnabled(nonDefaultSelected);
	ui->deleteLayerButton->setEnabled(nonDefaultSelected);
	updateLockButton();
}

void LayerManagerDialog::updateLockButton() {
	const auto* item = ui->layerList->currentItem();
	const auto objs = item != nullptr ? _model->getLayerLockObjects(item->text().toUtf8().constData()) : SCP_vector<int>();
	const auto state = transformLockState(objs);

	ui->lockLayerButton->setEnabled(!objs.empty());
	ui->lockLayerButton->setIcon(layerLockIcon(state == Qt::Unchecked ? QStringLiteral("unlock") : QStringLiteral("lock"),
		state == Qt::PartiallyChecked));
	if (objs.empty()) {
		ui->lockLayerButton->setToolTip(tr("Lock the position and orientation of every object in the layer (the layer is empty)"));
	} else if (state == Qt::Checked) {
		ui->lockLayerButton->setToolTip(tr("Every object in this layer is locked. Click to unlock them all."));
	} else if (state == Qt::PartiallyChecked) {
		ui->lockLayerButton->setToolTip(tr("Some objects in this layer are locked. Click to lock them all."));
	} else {
		ui->lockLayerButton->setToolTip(tr("No object in this layer is locked. Click to lock the position and orientation of them all."));
	}
}

void LayerManagerDialog::on_lockLayerButton_clicked() {
	const auto* item = ui->layerList->currentItem();
	if (item == nullptr)
		return;
	const auto objs = _model->getLayerLockObjects(item->text().toUtf8().constData());
	// a partly locked layer locks the rest; the mission change refreshes the button
	pushTransformLock(objs, transformLockState(objs) != Qt::Checked, _viewport->editor, _fredView->mainUndoStack());
}

void LayerManagerDialog::on_addLayerButton_clicked() {
	bool ok = false;
	auto name = QInputDialog::getText(this, tr("Add Layer"), tr("Layer name:"), QLineEdit::Normal, QString(), &ok).trimmed();
	if (!ok || name.isEmpty()) {
		if (ok && name.isEmpty()) {
			QMessageBox::warning(this, tr("Layer Error"), tr("Layer name cannot be empty."));
		}
		return;
	}

	SCP_string error;
	if (!runStructureOp(tr("Add Layer"), [&] { return _model->addLayer(name.toUtf8().constData(), &error); })) {
		QMessageBox::warning(this, tr("Layer Error"), QString::fromStdString(error));
		return;
	}

	// Select the newly added layer
	for (int i = 0; i < ui->layerList->count(); ++i) {
		if (ui->layerList->item(i)->text() == name) {
			ui->layerList->setCurrentRow(i);
			break;
		}
	}
}

void LayerManagerDialog::on_renameLayerButton_clicked() {
	auto* item = ui->layerList->currentItem();
	if (item == nullptr) {
		return;
	}

	const QString oldName = item->text();
	if (_model->isDefaultLayer(oldName.toUtf8().constData())) {
		QMessageBox::warning(this, tr("Layer Error"), tr("The default layer cannot be renamed."));
		return;
	}

	bool ok = false;
	auto newName = QInputDialog::getText(this, tr("Rename Layer"), tr("Layer name:"), QLineEdit::Normal, oldName, &ok).trimmed();
	if (!ok || newName == oldName) {
		if (ok && newName.isEmpty()) {
			QMessageBox::warning(this, tr("Layer Error"), tr("Layer name cannot be empty."));
		}
		return;
	}

	SCP_string error;
	if (!runStructureOp(tr("Rename Layer"), [&] {
		    return _model->renameLayer(oldName.toUtf8().constData(), newName.toUtf8().constData(), &error);
	    })) {
		QMessageBox::warning(this, tr("Layer Error"), QString::fromStdString(error));
		return;
	}

	// Keep the renamed layer selected
	for (int i = 0; i < ui->layerList->count(); ++i) {
		if (ui->layerList->item(i)->text() == newName) {
			ui->layerList->setCurrentRow(i);
			break;
		}
	}
}

void LayerManagerDialog::on_deleteLayerButton_clicked() {
	auto* item = ui->layerList->currentItem();
	if (item == nullptr) {
		return;
	}

	const SCP_string layerName = item->text().toUtf8().constData();
	if (_model->isDefaultLayer(layerName)) {
		QMessageBox::warning(this, tr("Layer Error"), tr("The default layer cannot be deleted."));
		return;
	}

	SCP_string error;
	if (!runStructureOp(tr("Delete Layer"), [&] { return _model->deleteLayer(layerName, &error); })) {
		QMessageBox::warning(this, tr("Layer Error"), QString::fromStdString(error));
	}
}

void LayerManagerDialog::on_layerList_currentRowChanged(int row) {
	ui->renameLayerButton->setEnabled(row > 0);
	ui->deleteLayerButton->setEnabled(row > 0);
	updateLockButton();
}

void LayerManagerDialog::on_layerList_itemChanged(QListWidgetItem* item) {
	if (_refreshing || item == nullptr) {
		return;
	}

	const SCP_string layerName = item->text().toUtf8().constData();
	const bool visible = item->checkState() == Qt::Checked;

	SCP_string error;
	if (!_model->setLayerVisibility(layerName, visible, &error)) {
		QMessageBox::warning(this, tr("Layer Error"), QString::fromStdString(error));
	}
}

void LayerManagerDialog::on_showShipsCheck_toggled(bool checked)     { _model->setShowShips(checked); }
void LayerManagerDialog::on_showStartsCheck_toggled(bool checked)    { _model->setShowStarts(checked); }
void LayerManagerDialog::on_showWaypointsCheck_toggled(bool checked) { _model->setShowWaypoints(checked); }
void LayerManagerDialog::on_showPropsCheck_toggled(bool checked)     { _model->setShowProps(checked); }
void LayerManagerDialog::on_showJumpNodesCheck_toggled(bool checked) { _model->setShowJumpNodes(checked); }
void LayerManagerDialog::on_showCoordinatePointsCheck_toggled(bool checked) { _model->setShowCoordinatePoints(checked); }

} // namespace fso::fred::dialogs
