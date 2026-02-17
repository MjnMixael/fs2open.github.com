#include "ui/dialogs/PropEditorDialog.h"

#include "ui_PropEditorDialog.h"

#include <ui/util/SignalBlockers.h>
#include <QListWidgetItem>

namespace fso::fred::dialogs {

PropEditorDialog::PropEditorDialog(FredView* parent, EditorViewport* viewport)
	: QDialog(parent), ui(new Ui::PropEditorDialog()), _model(new PropEditorDialogModel(this, viewport)) {
	ui->setupUi(this);

	initializeUi();
	updateUi();

	connect(_model.get(), &PropEditorDialogModel::modelDataChanged, this, [this]() {
		initializeUi();
		updateUi();
	});

	connect(ui->propFlagsListWidget, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
		auto idx = ui->propFlagsListWidget->row(item);
		_model->setFlagState(static_cast<size_t>(idx), item->checkState() == Qt::Checked);
		_model->apply();
	});

	resize(QDialog::sizeHint());
}

PropEditorDialog::~PropEditorDialog() = default;

void PropEditorDialog::initializeUi() {
	util::SignalBlockers blockers(this);

	ui->propFlagsListWidget->clear();

	const auto& labels = _model->getFlagLabels();
	for (size_t i = 0; i < labels.size(); ++i) {
		auto item = new QListWidgetItem(QString::fromStdString(labels[i].first));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(_model->getFlagState()[i] ? Qt::Checked : Qt::Unchecked);
		ui->propFlagsListWidget->addItem(item);
	}

	const auto enable = _model->hasValidSelection();
	ui->propNameLineEdit->setEnabled(enable);
	ui->propFlagsListWidget->setEnabled(enable);
	ui->nextButton->setEnabled(enable);
	ui->prevButton->setEnabled(enable);
}

void PropEditorDialog::updateUi() {
	util::SignalBlockers blockers(this);

	ui->propNameLineEdit->setText(QString::fromStdString(_model->getPropName()));

	for (int i = 0; i < ui->propFlagsListWidget->count(); ++i) {
		ui->propFlagsListWidget->item(i)->setCheckState(_model->getFlagState()[i] ? Qt::Checked : Qt::Unchecked);
	}
}

void PropEditorDialog::on_propNameLineEdit_editingFinished() {
	_model->setPropName(ui->propNameLineEdit->text().toUtf8().constData());
	if (!_model->apply()) {
		updateUi();
	}
}

void PropEditorDialog::on_nextButton_clicked() {
	_model->selectNextProp();
}

void PropEditorDialog::on_prevButton_clicked() {
	_model->selectPreviousProp();
}

} // namespace fso::fred::dialogs
