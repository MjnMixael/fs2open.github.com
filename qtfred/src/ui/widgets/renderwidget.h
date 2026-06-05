#pragma once

#include <memory>

#include <QWindow>
#include <QWidget>
#include <mission/EditorViewport.h>

#include "osapi/osapi.h"

namespace fso::fred {

class Editor;
class RenderWidget;

class RenderWindow: public QWindow {
 Q_OBJECT
 public:
	explicit RenderWindow(RenderWidget* parent = nullptr);
	~RenderWindow() override;

	void setEditor(Editor* editor, FredRenderer* renderer);

	void initializeGL(const QSurfaceFormat& surfaceFmt);

	void startRendering();

 protected:
	void paintGL();

	bool event(QEvent* evt) override;

	void resizeEvent(QResizeEvent* event) override;

	void exposeEvent(QExposeEvent* event) override;

 private:
	RenderWidget* _renderWidget = nullptr;

	Editor* fred = nullptr;
	FredRenderer* _renderer = nullptr;
};

class RenderWidget: public QWidget {
 Q_OBJECT

	RenderWindow* _window = nullptr;

	std::unique_ptr<QCursor> _standardCursor;
	std::unique_ptr<QCursor> _moveCursor;
	std::unique_ptr<QCursor> _rotateCursor;

	Editor* fred = nullptr;
	EditorViewport* _viewport = nullptr;

	CursorMode _cursorMode = CursorMode::Selecting;

	bool _usingMarkingBox = false;
	Marking_box _markingBox;

	// Set when a viewport handle was grabbed on the most recent left-press
	// pre-pass; consumed in mouseMoveEvent / mouseReleaseEvent. While true,
	// normal object selection and dragging are bypassed entirely.
	bool _handleGrabbed = false;

	// True iff the cursor is hovering a pickable viewport handle (and the
	// editor is in Moving mode). updateCursor() consults this to show the
	// move cursor, matching the affordance for hovering a real object.
	bool _hoveringHandle = false;

	QPoint _lastMouse;

	// Orbit camera drag state
	bool _orbitDragging = false;
	bool _rbuttonDown = false;
	bool _rbuttonMoved = false;
	QPoint _orbitLastMouse;
	QPoint _rbuttonDownPoint;

	void handleOrbitDrag(QPoint point, Qt::KeyboardModifiers modifiers);

 public:
	explicit RenderWidget(QWidget* parent);

	void setSurfaceFormat(const QSurfaceFormat& fmt);
	QSurface* getRenderSurface() const;

	void setEditor(Editor* editor, EditorViewport* viewport);

	void setCursorMode(CursorMode mode);

	void renderFrame();
 protected:
	void keyPressEvent(QKeyEvent*) override;
	void keyReleaseEvent(QKeyEvent*) override;
	bool event(QEvent* evt) override;

	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;

	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent*) override;

	void wheelEvent(QWheelEvent* event) override;

	void contextMenuEvent(QContextMenuEvent* event) override;
	void updateCursor() const;
};

} // namespace fso::fred
