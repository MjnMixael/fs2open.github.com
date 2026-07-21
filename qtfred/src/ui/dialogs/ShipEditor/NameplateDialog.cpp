#include "NameplateDialog.h"

#include "ui_NameplateDialog.h"

#include <globalincs/globals.h>
#include <graphics/software/FontManager.h>
#include <mission/util.h>
#include <ui/util/SignalBlockers.h>

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>

namespace fso::fred::dialogs {

NameplateDialog::NameplateDialog(QDialog* parent, EditorViewport* viewport)
	: QDialog(parent), ui(new Ui::NameplateDialog()),
	  _model(new NameplateDialogModel(this, viewport)), _viewport(viewport)
{
	ui->setupUi(this);

	ui->textEdit->setMaxLength(NAME_LENGTH - 1);
	ui->fileEdit->setMaxLength(MAX_FILENAME_LEN - 1);

	// populate the font list: a "default" entry (empty filename = use current font), then all fonts
	ui->fontCombo->addItem(tr("(default font)"), QString());
	for (int i = 0; i < font::FontManager::numberOfFonts(); ++i) {
		auto* fnt = font::FontManager::getFont(i);
		if (fnt == nullptr)
			continue;
		ui->fontCombo->addItem(fnt->getName().c_str(), QString(fnt->getFilename().c_str()));
	}

	connect(_model.get(), &AbstractDialogModel::modelChanged, this, &NameplateDialog::updateUi);

	updateUi();

	// Resize the dialog to the minimum size
	resize(QDialog::sizeHint());
}

NameplateDialog::~NameplateDialog() = default;

void NameplateDialog::accept()
{ // If apply() returns true, close the dialog
	if (_model->apply()) {
		QDialog::accept();
	}
}

void NameplateDialog::reject()
{
	if (rejectOrCloseHandler(this, _model.get(), _viewport)) {
		QDialog::reject();
	}
}

void NameplateDialog::closeEvent(QCloseEvent* e)
{
	reject();
	if (isVisible()) {
		e->ignore();
	} else {
		e->accept();
	}
}

void NameplateDialog::on_buttonBox_accepted()
{
	accept();
}
void NameplateDialog::on_buttonBox_rejected()
{
	reject();
}

void NameplateDialog::on_enabledCheck_toggled(bool state)
{
	_model->setEnabled(state);
}

void NameplateDialog::on_modeGenerateRadio_toggled(bool state)
{
	// this fires for both radios; the generate radio's state is the source of truth
	_model->setUseFile(!state);
}

void NameplateDialog::on_textEdit_editingFinished()
{
	_model->setText(ui->textEdit->text().toUtf8().constData());
}

void NameplateDialog::on_fontCombo_currentIndexChanged(int index)
{
	if (index < 0)
		return;
	const QString filename = ui->fontCombo->itemData(index).toString();
	_model->setFontFilename(filename.toUtf8().constData());
}

void NameplateDialog::on_fontScaleSpin_valueChanged(double value)
{
	_model->setFontScale(static_cast<float>(value));
}

void NameplateDialog::on_fileEdit_editingFinished()
{
	_model->setTextureFile(ui->fileEdit->text().toUtf8().constData());
}

void NameplateDialog::on_browseButton_clicked()
{
	const QString path = QFileDialog::getOpenFileName(this,
		tr("Select nameplate texture"),
		QString(),
		tr("Texture files (*.dds *.png *.tga *.jpg);;All files (*.*)"));
	if (path.isEmpty())
		return;

	// store the bare texture name (no path, no extension), like other texture references
	const QString base = QFileInfo(path).completeBaseName();
	_model->setTextureFile(base.toUtf8().constData());
	ui->fileEdit->setText(base);
}

void NameplateDialog::on_widthSpin_valueChanged(int value)
{
	_model->setWidth(value <= 0 ? -1 : value);
}

void NameplateDialog::on_heightSpin_valueChanged(int value)
{
	_model->setHeight(value <= 0 ? -1 : value);
}

void NameplateDialog::updateUi()
{
	util::SignalBlockers blockers(this);

	const bool enabled = _model->getEnabled();
	const bool useFile = _model->getUseFile();

	ui->enabledCheck->setChecked(enabled);

	ui->modeGenerateRadio->setChecked(!useFile);
	ui->modeFileRadio->setChecked(useFile);

	ui->textEdit->setText(_model->getText().c_str());

	// select the font entry matching the stored filename (fall back to "(default font)")
	{
		const QString wantFile = _model->getFontFilename().c_str();
		int idx = ui->fontCombo->findData(wantFile);
		if (idx < 0)
			idx = 0;
		ui->fontCombo->setCurrentIndex(idx);
	}

	ui->fontScaleSpin->setValue(_model->getFontScale());

	ui->fileEdit->setText(_model->getTextureFile().c_str());

	ui->widthSpin->setValue(_model->getWidth() > 0 ? _model->getWidth() : 0);
	ui->heightSpin->setValue(_model->getHeight() > 0 ? _model->getHeight() : 0);

	// enable/disable the mode-specific groups based on the overall state
	ui->modeBox->setEnabled(enabled);
	ui->generateBox->setEnabled(enabled && !useFile);
	ui->fileBox->setEnabled(enabled && useFile);
	ui->sizeBox->setEnabled(enabled);
}

} // namespace fso::fred::dialogs
