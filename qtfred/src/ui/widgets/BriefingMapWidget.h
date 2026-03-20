#pragma once

#include <QWidget>
#include <QWindow>
#include <QTimer>

class QOpenGLContext;

#include "globalincs/pstypes.h"

namespace fso::fred::dialogs {
class BriefingEditorDialogModel;
}

namespace fso::fred {

class EditorViewport;

class BriefingMapWindow : public QWindow {
	Q_OBJECT
public:
	explicit BriefingMapWindow(QWidget* parentWidget);

	void initializeGL(const QSurfaceFormat& fmt);

protected:
	bool event(QEvent* evt) override;
	void exposeEvent(QExposeEvent* event) override;

private:
	QWidget* _parentWidget = nullptr;
};

class BriefingMapWidget : public QWidget {
	Q_OBJECT

public:
	explicit BriefingMapWidget(QWidget* parent,
		dialogs::BriefingEditorDialogModel* model,
		EditorViewport* viewport);
	~BriefingMapWidget() override;

	void setStage(int stageNum);
	int getCurrentStage() const;

	QWindow* getRenderWindow() const;

signals:
	void iconSelected(int index);
	void cameraChanged(vec3d pos, matrix orient);

protected:
	void keyPressEvent(QKeyEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	void renderFrame();
	void initBriefingMap();

	BriefingMapWindow* _window = nullptr;
	QTimer* _renderTimer = nullptr;
	QOpenGLContext* _glContext = nullptr; // cached from init, since currentContext() may be null in modal dialog

	dialogs::BriefingEditorDialogModel* _model = nullptr;
	EditorViewport* _viewport = nullptr;

	int _currentStage = 0;
	bool _initialized = false;
	bool _rendering = false; // re-entrancy guard

	// Mouse drag state
	bool _draggingIcon = false;
	int _dragIconIndex = -1;
	QPoint _lastMousePos;
};

} // namespace fso::fred
