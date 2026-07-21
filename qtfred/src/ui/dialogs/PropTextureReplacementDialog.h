#pragma once

#include <mission/dialogs/PropTextureReplacementDialogModel.h>

#include <QAbstractListModel>
#include <QCheckBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QtWidgets/QDialog>

namespace fso::fred::dialogs {

namespace Ui {
class PropTextureReplacementDialog;
}

// Model for mapping data to the listview in the prop Texture Replace dialog
class PropMapModel : public QAbstractListModel {
	Q_OBJECT
  public:
	PropMapModel(PropTextureReplacementDialogModel*, QObject* parent);
	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	PropTextureReplacementDialogModel* _model;
};

class PropTextureReplacementDialog : public QDialog {
	Q_OBJECT

  public:
	explicit PropTextureReplacementDialog(QDialog* parent, EditorViewport* viewport, int propObjNum);
	~PropTextureReplacementDialog() override;

	void accept() override;
	void reject() override;

  protected:
	void closeEvent(QCloseEvent*) override;
  private slots:
	void on_buttonBox_accepted();
	void on_buttonBox_rejected();
	void on_newTextureLineEdit_editingFinished();

  private: // NOLINT(readability-redundant-access-specifiers)
	struct TextureTypeRow {
		QLabel*    label;
		QCheckBox* useCheckbox;
		QCheckBox* inheritCheckbox;
		QLineEdit* lineEdit;
	};

	std::unique_ptr<Ui::PropTextureReplacementDialog> ui;
	std::unique_ptr<PropTextureReplacementDialogModel> _model;
	EditorViewport* _viewport;
	int _selectedRow = 0;
	PropMapModel* _listModel;
	SCP_map<SCP_string, TextureTypeRow> _textureRows;
	void updateUi();
	void updateUiFull();
};
} // namespace fso::fred::dialogs
