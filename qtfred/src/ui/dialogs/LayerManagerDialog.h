#pragma once

#include <QDialog>
#include <QVector>

#include <functional>

#include "mission/dialogs/LayerManagerDialogModel.h"

class QCheckBox;
class QListWidgetItem;

namespace fso::fred::dialogs {

namespace Ui {
class LayerManagerDialog;
}

class LayerManagerDialog final : public QDialog {
	Q_OBJECT

public:
	explicit LayerManagerDialog(EditorViewport* viewport, QWidget* parent = nullptr);
	~LayerManagerDialog() override;

private slots:
	void on_addLayerButton_clicked();
	void on_renameLayerButton_clicked();
	void on_deleteLayerButton_clicked();
	void on_layerList_currentRowChanged(int row);
	void on_layerList_itemChanged(QListWidgetItem* item);
	void on_showShipsCheck_toggled(bool checked);
	void on_showStartsCheck_toggled(bool checked);
	void on_showWaypointsCheck_toggled(bool checked);
	void on_showPropsCheck_toggled(bool checked);
	void on_showJumpNodesCheck_toggled(bool checked);
	void on_showCoordinatePointsCheck_toggled(bool checked);

private: // NOLINT(readability-redundant-access-specifiers)
	void initializeUi();
	void updateUi();

	// Runs a layer add/delete/rename wrapped in a LayerStructureCommand: the
	// command captures the before-state, op() applies the change, and the command
	// is only pushed if op() succeeded. Returns what op() returned.
	bool runStructureOp(const QString& text, const std::function<bool()>& op);

	EditorViewport* _viewport = nullptr;
	bool _refreshing = false;
	QVector<QCheckBox*> _iffChecks;
	std::unique_ptr<Ui::LayerManagerDialog> ui;
	std::unique_ptr<LayerManagerDialogModel> _model;
};

} // namespace fso::fred::dialogs
