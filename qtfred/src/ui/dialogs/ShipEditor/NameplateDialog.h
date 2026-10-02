#pragma once

#include <mission/dialogs/ShipEditor/NameplateDialogModel.h>

#include <QtWidgets/QDialog>
#include <QTimer>

namespace fso::fred::dialogs {

namespace Ui {
class NameplateDialog;
}

class NameplateDialog : public QDialog {
	Q_OBJECT

  public:
	NameplateDialog(QDialog* parent, EditorViewport* viewport, int shipnum);
	~NameplateDialog() override;

	void accept() override;
	void reject() override;

  protected:
	void closeEvent(QCloseEvent*) override;

  private slots:
	void on_buttonBox_accepted();
	void on_buttonBox_rejected();
	void on_enabledCheck_toggled(bool state);
	void on_modeGenerateRadio_toggled(bool state);
	void on_textEdit_textChanged(const QString& text);
	void on_fontCombo_currentIndexChanged(int index);
	void on_fontScaleSpin_valueChanged(double value);
	void on_letterSpacingSpin_valueChanged(double value);
	void on_colorRSpin_valueChanged(int value);
	void on_colorGSpin_valueChanged(int value);
	void on_colorBSpin_valueChanged(int value);
	void on_offsetXSpin_valueChanged(int value);
	void on_offsetYSpin_valueChanged(int value);
	void on_fileEdit_editingFinished();
	void on_browseButton_clicked();
	void on_widthSpin_valueChanged(int value);
	void on_heightSpin_valueChanged(int value);
	void on_keepProportionsCheck_toggled(bool checked);
	void on_resetSizeButton_clicked();

  private: // NOLINT(readability-redundant-access-specifiers)
	void updateUi();
	void updateColorSwatch();
	// the other side of a size with the model's proportions
	int proportionalHeight(int width) const;
	int proportionalWidth(int height) const;

	std::unique_ptr<Ui::NameplateDialog> ui;
	std::unique_ptr<NameplateDialogModel> _model;
	EditorViewport* _viewport;

	// coalesces edits so a held spinbox or typing builds one preview texture, not one per step
	QTimer _previewTimer;
};

} // namespace fso::fred::dialogs
