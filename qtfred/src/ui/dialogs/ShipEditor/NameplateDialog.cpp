#include "NameplateDialog.h"

#include "ui_NameplateDialog.h"

#include <globalincs/globals.h>
#include <graphics/software/FontManager.h>
#include <mission/util.h>
#include <ui/util/SignalBlockers.h>

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QSignalBlocker>

namespace fso::fred::dialogs {

NameplateDialog::NameplateDialog(QDialog* parent, EditorViewport* viewport, int shipnum)
	: QDialog(parent), ui(new Ui::NameplateDialog()),
	  _model(new NameplateDialogModel(this, viewport, shipnum)), _viewport(viewport)
{
	ui->setupUi(this);

	ui->textEdit->setMaxLength(NAME_LENGTH - 1);
	ui->fileEdit->setMaxLength(MAX_FILENAME_LEN - 1);

	// populate the font list by name, which is unique (several fonts can share one file): a "default"
	// entry (empty = the first usable font), then every font that can draw nameplates. Blocked, because
	// the first item selects itself and the auto-connected slot would overwrite the ship's font.
	QSignalBlocker fontComboBlocker(ui->fontCombo);
	ui->fontCombo->addItem(tr("(default font)"), QString());
	for (int i = 0; i < font::FontManager::numberOfFonts(); ++i) {
		auto* fnt = font::FontManager::getFont(i);
		if (fnt == nullptr || !nameplate_font_usable(i))
			continue;
		ui->fontCombo->addItem(fnt->getName().c_str(), QString(fnt->getName().c_str()));
	}

	// a font the mission names that isn't loaded, or can't draw nameplates, stays visible instead of
	// quietly becoming the default
	{
		const SCP_string wanted = _model->getFontName();
		if (!wanted.empty() && !nameplate_font_usable(nameplate_font_index(wanted)))
			ui->fontCombo->insertItem(1, tr("%1 (not available)").arg(wanted.c_str()), QString(wanted.c_str()));
	}

	if (!_model->canGenerate()) {
		ui->modeGenerateRadio->setEnabled(false);
		ui->modeGenerateRadio->setToolTip(
			tr("No loaded font can draw nameplates (TrueType fonts with +Auto Size can't), so only texture files work"));
	}

	ui->offsetXSpin->setRange(-NAMEPLATE_MAX_SIZE, NAMEPLATE_MAX_SIZE);
	ui->offsetYSpin->setRange(-NAMEPLATE_MAX_SIZE, NAMEPLATE_MAX_SIZE);
	ui->letterSpacingSpin->setRange(-NAMEPLATE_MAX_LETTER_SPACING, NAMEPLATE_MAX_LETTER_SPACING);
	{
		// raising the minimum moves the spinboxes off 0, which would otherwise reach the model as an edit
		QSignalBlocker widthBlocker(ui->widthSpin);
		QSignalBlocker heightBlocker(ui->heightSpin);
		ui->widthSpin->setRange(NAMEPLATE_MIN_SIZE, NAMEPLATE_MAX_SIZE);
		ui->heightSpin->setRange(NAMEPLATE_MIN_SIZE, NAMEPLATE_MAX_SIZE);
	}
	ui->modelSizeLabel->setText(
		tr("Model size: %1 x %2").arg(_model->getModelWidth()).arg(_model->getModelHeight()));

	// start locked when the ship's size has the model's proportions (always, unless an override changed them)
	{
		QSignalBlocker lockBlocker(ui->keepProportionsCheck);
		ui->keepProportionsCheck->setChecked(proportionalHeight(_model->getWidth()) == _model->getHeight());
	}

	connect(_model.get(), &AbstractDialogModel::modelChanged, this, &NameplateDialog::updateUi);

	// show each change on the ship in the viewport shortly after it stops changing
	_previewTimer.setSingleShot(true);
	_previewTimer.setInterval(100);
	connect(&_previewTimer, &QTimer::timeout, this, [this]() { _model->preview(); });
	connect(_model.get(), &AbstractDialogModel::modelChanged, &_previewTimer, qOverload<>(&QTimer::start));

	updateUi();

	// Resize the dialog to the minimum size
	resize(QDialog::sizeHint());
}

NameplateDialog::~NameplateDialog() = default;

void NameplateDialog::accept()
{ // If apply() returns true, close the dialog
	_previewTimer.stop();
	if (_model->apply()) {
		QDialog::accept();
	}
}

void NameplateDialog::reject()
{
	_previewTimer.stop();
	if (rejectOrCloseHandler(this, _model.get(), _viewport)) {
		QDialog::reject();
	} else if (isVisible()) {
		_model->preview(); // still open: catch up on any edit the stopped timer was waiting on
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

void NameplateDialog::on_textEdit_textChanged(const QString& text)
{
	_model->setText(text.toUtf8().constData());
}

void NameplateDialog::on_fontCombo_currentIndexChanged(int index)
{
	if (index < 0)
		return;
	const QString name = ui->fontCombo->itemData(index).toString();
	_model->setFontName(name.toUtf8().constData());
}

void NameplateDialog::on_fontScaleSpin_valueChanged(double value)
{
	_model->setFontScale(static_cast<float>(value));
}

void NameplateDialog::on_letterSpacingSpin_valueChanged(double value)
{
	_model->setLetterSpacing(static_cast<float>(value));
}

void NameplateDialog::on_colorRSpin_valueChanged(int value)
{
	_model->setColorR(value);
}

void NameplateDialog::on_colorGSpin_valueChanged(int value)
{
	_model->setColorG(value);
}

void NameplateDialog::on_colorBSpin_valueChanged(int value)
{
	_model->setColorB(value);
}

void NameplateDialog::on_offsetXSpin_valueChanged(int value)
{
	_model->setOffsetX(value);
}

void NameplateDialog::on_offsetYSpin_valueChanged(int value)
{
	_model->setOffsetY(value);
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


int NameplateDialog::proportionalHeight(int width) const
{
	return qRound(static_cast<double>(width) * _model->getModelHeight() / _model->getModelWidth());
}

int NameplateDialog::proportionalWidth(int height) const
{
	return qRound(static_cast<double>(height) * _model->getModelWidth() / _model->getModelHeight());
}

void NameplateDialog::on_widthSpin_valueChanged(int value)
{
	_model->setSize(value, ui->keepProportionsCheck->isChecked() ? proportionalHeight(value) : _model->getHeight());
}

void NameplateDialog::on_heightSpin_valueChanged(int value)
{
	_model->setSize(ui->keepProportionsCheck->isChecked() ? proportionalWidth(value) : _model->getWidth(), value);
}

void NameplateDialog::on_keepProportionsCheck_toggled(bool checked)
{
	// locking snaps the height back to the model's proportions for the current width
	if (checked)
		_model->setSize(_model->getWidth(), proportionalHeight(_model->getWidth()));
}

void NameplateDialog::on_resetSizeButton_clicked()
{
	_model->setSize(_model->getModelWidth(), _model->getModelHeight());
	ui->keepProportionsCheck->setChecked(true);
}

void NameplateDialog::updateUi()
{
	util::SignalBlockers blockers(this);

	const bool enabled = _model->getEnabled();
	const bool useFile = _model->getUseFile();

	ui->enabledCheck->setChecked(enabled);

	ui->modeGenerateRadio->setChecked(!useFile);
	ui->modeFileRadio->setChecked(useFile);

	// only when it differs, so typing doesn't have its cursor moved to the end
	if (ui->textEdit->text() != QString::fromUtf8(_model->getText().c_str()))
		ui->textEdit->setText(QString::fromUtf8(_model->getText().c_str()));

	// select the font entry matching the stored name (fall back to "(default font)"); an early test build
	// saved a filename, so show the font it resolves to
	{
		SCP_string name = _model->getFontName();
		const int fontIndex = nameplate_font_index(name);
		if (nameplate_font_usable(fontIndex))
			name = font::FontManager::getFont(fontIndex)->getName();
		int idx = ui->fontCombo->findData(QString(name.c_str()), Qt::UserRole, Qt::MatchFixedString);
		if (idx < 0)
			idx = 0;
		ui->fontCombo->setCurrentIndex(idx);
	}

	ui->fontScaleSpin->setValue(_model->getFontScale());
	ui->letterSpacingSpin->setValue(_model->getLetterSpacing());

	ui->colorRSpin->setValue(_model->getColorR());
	ui->colorGSpin->setValue(_model->getColorG());
	ui->colorBSpin->setValue(_model->getColorB());
	updateColorSwatch();

	ui->offsetXSpin->setValue(_model->getOffsetX());
	ui->offsetYSpin->setValue(_model->getOffsetY());

	ui->fileEdit->setText(_model->getTextureFile().c_str());

	ui->widthSpin->setValue(_model->getWidth());
	ui->heightSpin->setValue(_model->getHeight());
	ui->resetSizeButton->setEnabled(!_model->isModelSize());


	// enable/disable the mode-specific groups based on the overall state
	ui->modeBox->setEnabled(enabled);
	ui->generateBox->setEnabled(enabled && !useFile);
	ui->fileBox->setEnabled(enabled && useFile);
	ui->sizeBox->setEnabled(enabled && !useFile); // a texture file is used at its own size
}

void NameplateDialog::updateColorSwatch()
{
	ui->colorPreview->setStyleSheet(QString("background: rgb(%1,%2,%3);"
											"border: 1px solid #444; border-radius: 3px;")
			.arg(_model->getColorR())
			.arg(_model->getColorG())
			.arg(_model->getColorB()));
}

} // namespace fso::fred::dialogs
