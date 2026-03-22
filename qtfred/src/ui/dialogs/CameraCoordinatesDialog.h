#pragma once

#include <QDialog>
#include <QDoubleSpinBox>

#include "globalincs/pstypes.h"

namespace fso::fred {
class BriefingMapWidget;
}

namespace fso::fred::dialogs {

class BriefingEditorDialogModel;

class CameraCoordinatesDialog : public QDialog {
	Q_OBJECT

public:
	CameraCoordinatesDialog(QWidget* parent,
		BriefingEditorDialogModel* model,
		fso::fred::BriefingMapWidget* mapWidget);

private slots:
	void onApplyClicked();

private:
	void setupUi();
	void populateFromCurrentCamera();

	BriefingEditorDialogModel* _model;
	fso::fred::BriefingMapWidget* _mapWidget;

	QDoubleSpinBox* _posX;
	QDoubleSpinBox* _posY;
	QDoubleSpinBox* _posZ;
	QDoubleSpinBox* _heading;
	QDoubleSpinBox* _pitch;
	QDoubleSpinBox* _bank;

};

} // namespace fso::fred::dialogs
