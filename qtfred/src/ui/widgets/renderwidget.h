include <mission/EditorViewport.h>#pragma once

#include <memory>

#include <QWindow>
#include <QWidget>
#include <QByteArray>
#include <mission/Editor.h>
#include <mission/EditorViewport.h>

#include "osapi/osapi.h"

namespace fso::fred {

class Editor;
class FredView;
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
	FredView* _fredView = nullptr;

	CursorMode _cursorMode = CursorMode::Selecting;

	bool _usingMarkingBox = false;
	Marking_box _markingBox;

	// Set when a viewport handle was grabbed on the most recent left-press
	// pre-pass; consumed in mouseMoveEvent / mouseReleaseEvent. While true,
	// normal object selection and dragging are bypassed entirely.
	bool _handleGrabbed = false;

	// Which environment entity the grabbed handle belongs to, and the global
	// snapshot taken at grab time. Consumed on release to push one undo command
	// per drag gesture (mirrors the background-drag tracking below).
	EnvironmentObject _handleDragEnv = EnvironmentObject::None;
	QByteArray _handleDragBefore;

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

	// Commit an in-progress background-element drag: push one undo command (or
	// revert if the dialog closed mid-drag) and clear the drag state. Shared by
	// the release handler and the "button released off-widget" recovery path.
	void finalizeBackgroundDrag();

	// Commit an in-progress viewport-handle drag: fire the handle's on_release
	// and push one undo command for the gesture. Shared by the release handler,
	// the Escape/right-click cancels and the lost-release recovery path.
	void finalizeHandleDrag();

	// Ctrl+drag clone tracking — set on press, consumed on release.
	bool            _wasDupDrag              = false;
	bool            _wasInsertDrag           = false;
	int             _preCloneCurrentObj      = -1;
	SCP_vector<int> _preCloneSourceSignatures;

	// Background-element drag tracking — set on press when a background handle
	// is grabbed, consumed on release (pushes one undo command for the drag).
	bool       _bgDragging = false;
	bool       _bgMoved    = false;
	QByteArray _bgDragBefore;
	QPoint     _bgLastMouse;

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
	void focusInEvent(QFocusEvent* event) override;

	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;

	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent*) override;

	void wheelEvent(QWheelEvent* event) override;

	void contextMenuEvent(QContextMenuEvent* event) override;
	void updateCursor() const;
};

} // namespace fso::fred
