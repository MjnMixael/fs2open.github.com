#pragma once

#include <mission/dialogs/ShipEditor/NameplateDialogModel.h>

#include <QtWidgets/QDialog>

namespace fso::fred::dialogs {

namespace Ui {
class NameplateDialog;
}

class NameplateDialog : public QDialog {
	Q_OBJECT

  public:
	explicit NameplateDialog(QDialog* parent, EditorViewport* viewport);
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
	void on_textEdit_editingFinished();
	void on_fontCombo_currentIndexChanged(int index);
	void on_fontScaleSpin_valueChanged(double value);
	void on_fileEdit_editingFinished();
	void on_browseButton_clicked();
	void on_widthSpin_valueChanged(int value);
	void on_heightSpin_valueChanged(int value);

  private: // NOLINT(readability-redundant-access-specifiers)
	void updateUi();

	std::unique_ptr<Ui::NameplateDialog> ui;
	std::unique_ptr<NameplateDialogModel> _model;
	EditorViewport* _viewport;
};

} // namespace fso::fred::dialogs
