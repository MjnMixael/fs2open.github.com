#pragma once

#include "mission/dialogs/MissionSpecs/CustomDataDialogModel.h"

#include <ui/FredView.h>

#include <QDialog>
#include <QStandardItemModel>

class QIntValidator;

namespace fso::fred::dialogs {

namespace Ui {
class CustomDataDialog;
}

class CustomDataDialog final : public QDialog {
	Q_OBJECT
  public:
	explicit CustomDataDialog(QWidget* parent, EditorViewport* viewport);
	~CustomDataDialog() override;

	void accept() override;
	void reject() override;

	// optional editor-defined schema for this domain (mission/campaign/ship);
	// enables type-aware value controls, help text, and reset-to-default
	void setSchema(const SCP_vector<mission_default_custom_data>& schema);

	void setInitial(const SCP_map<SCP_string, SCP_string>& items);

	const SCP_map<SCP_string, SCP_string>& items() const
	{
		return _model->items();
	}

  protected:
	void closeEvent(QCloseEvent* e) override;

  private slots:
	// Top-row buttons
	void on_addButton_clicked();
	void on_updateButton_clicked();
	void on_removeButton_clicked();
	void on_resetButton_clicked();

	// Re-evaluate the type-aware value controls when the key changes
	void on_keyLineEdit_textChanged(const QString& key);

	// Dialog buttons
	void on_okAndCancelButtons_accepted();
	void on_okAndCancelButtons_rejected();

  private: // NOLINT(readability-redundant-access-specifiers)
	void buildView();
	void refreshTable();
	void selectRow(int row);
	void loadRowIntoEditors(int row);
	void updateHelpTextForKey(const QString& key);
	// show the value control appropriate to the key's schema type (line edit vs bool combo)
	void applyTypeForKey(const QString& key);
	void setValueEditorText(const QString& value);
	QString currentValueText() const;
	std::pair<SCP_string, SCP_string> editorsToEntry() const;
	void clearEditors();

	std::unique_ptr<Ui::CustomDataDialog> ui;
	std::unique_ptr<CustomDataDialogModel> _model;
	EditorViewport* _viewport;

	QStandardItemModel* _tableModel = nullptr;
	QIntValidator* _intValidator = nullptr;
	// true when the bool combo (not the line edit) is the active value control;
	// tracked explicitly because QWidget::isVisible() is false until the dialog is shown
	bool _boolEditorActive = false;
};

} // namespace fso::fred::dialogs
