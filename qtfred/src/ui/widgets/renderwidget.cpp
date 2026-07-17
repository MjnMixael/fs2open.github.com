#if defined(_MSC_VER) && _MSC_VER <= 1920
	// work around MSVC 2015 and 2017 compiler bug
	// https://bugreports.qt.io/browse/QTBUG-72073
	#define QT_NO_FLOAT16_OPERATORS
#endif

#include "renderwidget.h"

#include <array>

#include <QDebug>
#include <QDir>
#include <QKeyEvent>
#include <QTimer>
#include <QtGui/QtGui>
#include <QtWidgets/QHBoxLayout>
#include <mission/object.h>
#include <ship/ship.h>
#include <globalincs/linklist.h>
#include <QtWidgets/QMessageBox>

#include "osapi/osapi.h"
#include "ui/ControlBindings.h"
#include "io/timer.h"
#include "starfield/starfield.h"

#include "mission/Editor.h"
#include "FredApplication.h"
#include "ui/FredView.h"

namespace fso::fred {

RenderWindow::RenderWindow(RenderWidget* renderWidget) : QWindow((QWindow*) nullptr), _renderWidget(renderWidget) {
	setSurfaceType(QWindow::OpenGLSurface);
}

void RenderWindow::initializeGL(const QSurfaceFormat& surfaceFmt) {
	setFormat(surfaceFmt);

	// Force creation of this window so that we can use it for OpenGL
	create();
}

void RenderWindow::startRendering() {
	_renderer->resize(size().width(), size().height());
}

void RenderWindow::paintGL() {
	if (!_renderer || !fredApp->isInitializeComplete()) {
		return;
	}

	_renderWidget->renderFrame();
}

bool RenderWindow::event(QEvent* evt) {
	switch (evt->type()) {
	case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonPress:
	case QEvent::KeyPress:
	case QEvent::KeyRelease:
	case QEvent::ShortcutOverride:
	case QEvent::MouseButtonDblClick:
	case QEvent::MouseMove:
	case QEvent::Wheel: {
		// Redirect all the events to the render widget since we want to handle them in in the QtWidget related code
		qGuiApp->sendEvent(_renderWidget, evt);
		return true;
	}
	case QEvent::UpdateRequest:
		paintGL();
		evt->accept();
		return true;
	default:
		return QWindow::event(evt);
	}
}
void RenderWindow::resizeEvent(QResizeEvent* event) {
	if (_renderer) {
		// Only send resize event if we are actually rendering
		_renderer->resize(event->size().width(), event->size().height());
	}
}
RenderWindow::~RenderWindow() = default;
void RenderWindow::exposeEvent(QExposeEvent* event) {
	if (isExposed()) {
		requestUpdate();

		event->accept();
	} else {
		QWindow::exposeEvent(event);
	}
}
void RenderWindow::setEditor(Editor* editor, FredRenderer* renderer) {
	Assertion(fred == nullptr, "Render widget currently does not support resetting the editor!");
	Assertion(_renderer == nullptr, "Render widget currently does not support resetting the renderer!");

	Assertion(editor != nullptr, "Invalid editor pointer passed!");
	Assertion(renderer != nullptr, "Invalid renderer pointer passed!");

	fred = editor;
	_renderer = renderer;

	// When the editor want to update the main window we have to do that.
	connect(_renderer, &FredRenderer::scheduleUpdate, this, &QWindow::requestUpdate);
}
RenderWidget::RenderWidget(QWidget* parent) : QWidget(parent) {
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);

	_window = new RenderWindow(this);

	auto layout = new QHBoxLayout(this);
	layout->setSpacing(0);
	layout->addWidget(QWidget::createWindowContainer(_window, this));
	layout->setContentsMargins(0, 0, 0, 0);

	setLayout(layout);


	_standardCursor.reset(new QCursor(Qt::ArrowCursor));
	_moveCursor.reset(new QCursor(Qt::SizeAllCursor));

	QPixmap rotatePixmap(":/images/cursor_rotate.png");
	_rotateCursor.reset(new QCursor(rotatePixmap, 15, 16)); // These values are from the original cursor file

	_window->setCursor(*_standardCursor);

	setContextMenuPolicy(Qt::DefaultContextMenu);

	fredApp->runAfterInit([this]() { _window->startRendering(); });
}
QSurface* RenderWidget::getRenderSurface() const {
	return _window;
}
void RenderWidget::setSurfaceFormat(const QSurfaceFormat& fmt) {
	_window->initializeGL(fmt);
}
void RenderWidget::contextMenuEvent(QContextMenuEvent* event) {
	// Suppress context menu if right-button was used for orbit dragging
	if (_rbuttonMoved) {
		event->accept();
		return;
	}

	event->accept();

	auto parentView = static_cast<FredView*>(parentWidget());

	Q_ASSERT(parentView);

	parentView->showContextMenu(event->globalPos());
}

void RenderWidget::keyPressEvent(QKeyEvent* key) {
	if (_viewport != nullptr && key->key() == Qt::Key_Escape && _handleGrabbed) {
		// Escape during a handle drag: drop the grab. The struct retains
		// whatever the last drag tick wrote (apply/reject on the owning
		// dialog handles full revert).
		_viewport->end_handle_drag();
		_handleGrabbed = false;
		_viewport->needsUpdate();
		key->accept();
		return;
	}

	if (_viewport != nullptr && key->key() == Qt::Key_Escape && _viewport->button_down) {
		_viewport->cancel_drag();
		_usingMarkingBox = false;
		key->accept();
		return;
	}

	// Escape with an environment entity selected (and nothing being dragged):
	// deselect it, mirroring Escape clearing an object selection.
	if (_viewport != nullptr && key->key() == Qt::Key_Escape &&
		fred->currentEnvironment != EnvironmentObject::None) {
		fred->clearEnvironment();
		key->accept();
		return;
	}

	if (!ControlBindings::instance().handleKeyPress(key)) {
		QWidget::keyPressEvent(key);
		return;
	}
	key->accept();
}

void RenderWidget::keyReleaseEvent(QKeyEvent* key) {
	if (!ControlBindings::instance().handleKeyRelease(key)) {
		QWidget::keyReleaseEvent(key);
		return;
	}
	key->accept();
}
bool RenderWidget::event(QEvent* evt) {
	if (evt->type() == QEvent::ShortcutOverride) {
		auto* keyEvent = static_cast<QKeyEvent*>(evt);
		if (ControlBindings::instance().matches(keyEvent)) {
			evt->accept();
			return true;
		}
	}

	return QWidget::event(evt);
}
void RenderWidget::mouseDoubleClickEvent(QMouseEvent* event) {
	// Double-clicking the volumetric gizmo opens its editor, like double-clicking
	// a real object opens its dialog. The first click of the sequence already
	// selected it via mousePressEvent.
	if (_viewport != nullptr && event->button() == Qt::LeftButton) {
		auto pick = _viewport->pick_handle(event->position().x() * _window->devicePixelRatio(),
			event->position().y() * _window->devicePixelRatio());
		const auto env = _viewport->handleEnvironment(pick);
		if (env != EnvironmentObject::None) {
			auto parentView = static_cast<FredView*>(parentWidget());
			Q_ASSERT(parentView);
			if (env == EnvironmentObject::VolumetricNebula) {
				parentView->editVolumetricNebula();
			} else if (env == EnvironmentObject::AsteroidField) {
				parentView->editAsteroidField();
			}
			event->accept();
			return;
		}
	}
	event->ignore();
}
void RenderWidget::mousePressEvent(QMouseEvent* event) {
	// Orbit camera: middle button
	if (event->button() == Qt::MiddleButton) {
		if (_viewport != nullptr && _viewport->camera.getViewpoint() == 0 && _viewport->camera.getControlMode() == 0) {
			vec3d pivot = _viewport->orbitCameraGetPivot();
			auto grid_orient = _viewport->The_grid ? &_viewport->The_grid->gmatrix : nullptr;
			_viewport->camera.orbitCameraInitFromCurrentView(&pivot, grid_orient);
			_orbitDragging = true;
			_orbitLastMouse = event->pos();
		}
		return;
	}

	// Orbit camera: right button
	if (event->button() == Qt::RightButton) {
		if (_viewport != nullptr && _handleGrabbed) {
			_viewport->end_handle_drag();
			_handleGrabbed = false;
			_viewport->needsUpdate();
			event->accept();
			return;
		}

		if (_viewport != nullptr && _viewport->button_down) {
			_viewport->cancel_drag();
			_usingMarkingBox = false;
			event->accept();
			return;
		}

		_rbuttonDown = true;
		_rbuttonMoved = false;
		_rbuttonDownPoint = event->pos();
		_orbitLastMouse = event->pos();

		if (_viewport != nullptr && _viewport->camera.getViewpoint() == 0 && _viewport->camera.getControlMode() == 0) {
			vec3d pivot = _viewport->orbitCameraGetPivot();
			auto grid_orient = _viewport->The_grid ? &_viewport->The_grid->gmatrix : nullptr;
			_viewport->camera.orbitCameraInitFromCurrentView(&pivot, grid_orient);
		}
		return;
	}

	if (event->button() != Qt::LeftButton) {
		// Ignore everything that has nothing to do with the left button
		return QWidget::mousePressEvent(event);
	}

	// Viewport handle pre-pass. If the click landed on a handle owned by an
	// open dialog (asteroid bounds, volumetric center, etc.), consume the click
	// and route subsequent moves/release to the handle drag path. Normal
	// object selection is bypassed entirely for this click, which keeps the
	// marquee-select and selection-lock behaviors fully untouched.
	//
	// Only active in Moving mode — handles are about positioning, so the
	// pointer/rotate modes leave them inert (matching the mode's affordance
	// of "not for translation"). The handles still render so the user can
	// see them; they just won't grab.
	if (_viewport != nullptr && _viewport->Editing_mode == CursorMode::Moving) {
		const int px = event->position().x() * _window->devicePixelRatio();
		const int py = event->position().y() * _window->devicePixelRatio();
		auto pick = _viewport->pick_handle(px, py);
		if (pick.group_index >= 0) {
			// Clicking a viewport handle selects its environment entity (mutually
			// exclusive with object selection), records it as the spinbox target,
			// and arms a drag if one can start.
			const auto env = _viewport->handleEnvironment(pick);
			if (env != EnvironmentObject::None) {
				fred->selectEnvironment(env);
				_viewport->setSelectedHandle(pick);
			}
			if (_viewport->begin_handle_drag(pick, px, py)) {
				_handleGrabbed = true;
				_usingMarkingBox = false;
			}
			event->accept();
			return;
		}
	}

	// A left-click that missed every handle deselects any environment entity;
	// the normal object-selection path below is mutually exclusive with it.
	if (_viewport != nullptr && fred->currentEnvironment != EnvironmentObject::None) {
		fred->clearEnvironment();
	}

	int waypoint_instance = -1;
	if (fred->cur_waypoint != nullptr) {
		Assertion(fred->cur_waypoint_list != nullptr, "cur_waypoint is set but cur_waypoint_list is null!");
		waypoint_instance = Objects[fred->cur_waypoint->get_objnum()].instance;
	}

	_markingBox.x1 = event->position().x() * _window->devicePixelRatio();
	_markingBox.y1 = event->position().y() * _window->devicePixelRatio();
	_viewport->Dup_drag = 0;

	_viewport->on_object = _viewport->select_object(event->position().x() * _window->devicePixelRatio(),
		event->position().y() * _window->devicePixelRatio());
	_viewport->button_down = 1;

	if (event->modifiers().testFlag(Qt::ControlModifier)) {  // add a new object
		if (_viewport->on_object == -1) {
			_viewport->Selection_lock = false;  // force off selection lock
			const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
			const bool alt   = event->modifiers().testFlag(Qt::AltModifier);
			auto kind = CreateKind::Ship;
			if (alt && !shift) {
				kind = CreateKind::Other;
			} else if (shift && !alt) {
				kind = CreateKind::Prop;
			}
			_viewport->on_object = _viewport->create_object_on_grid(event->position().x() * _window->devicePixelRatio(), event->position().y() * _window->devicePixelRatio(), waypoint_instance, kind);

		} else {
			// Ctrl+drag: duplicate marked objects (waypoints start a new path).
			// Ctrl+Shift+drag: same, except marked waypoints insert into their
			// source path instead of starting a new one.
			_viewport->Dup_drag = event->modifiers().testFlag(Qt::ShiftModifier)
				? EditorViewport::DUP_DRAG_INSERT
				: 1;
		}

	} else if (!_viewport->Selection_lock) {
		if ((event->modifiers().testFlag(Qt::ShiftModifier)) || (_viewport->on_object == -1)
			|| !(Objects[_viewport->on_object].flags[Object::Object_Flags::Marked])) {
			if (!event->modifiers().testFlag(Qt::ShiftModifier)) {
				fred->unmark_all();
			}

			if (_viewport->on_object != -1) {
				if (Objects[_viewport->on_object].flags[Object::Object_Flags::Marked]) {
					fred->unmarkObject(_viewport->on_object);
				} else {
					fred->markObject(_viewport->on_object);
				}
			}
		}
	}

	// Save drag/rotate backup after selection state is finalized for this click.
	_viewport->drag_rotate_save_backup();

	if (query_valid_object(fred->currentObject)) {
		_viewport->original_pos = Objects[fred->currentObject].pos;
	}

	_viewport->moved = 0;
	if (_viewport->Selection_lock) {
		if (_viewport->Editing_mode == CursorMode::Moving) {
			_viewport->drag_objects(event->position().x() * _window->devicePixelRatio(),
				event->position().y() * _window->devicePixelRatio());
		} else if (_viewport->Editing_mode == CursorMode::Rotating) {
			_viewport->drag_rotate_objects(0, 0);
		}

		_viewport->needsUpdate();
	}
}
void RenderWidget::handleOrbitDrag(QPoint point, Qt::KeyboardModifiers modifiers)
{
	if (_viewport == nullptr || _viewport->camera.getViewpoint() != 0 || _viewport->camera.getControlMode() != 0)
		return;

	int dx = point.x() - _orbitLastMouse.x();
	int dy = point.y() - _orbitLastMouse.y();
	_orbitLastMouse = point;

	if (modifiers & Qt::ShiftModifier)
		_viewport->camera.orbitCameraPan(dx, dy);
	else
		_viewport->camera.orbitCameraRotate(dx, dy);
	_viewport->needsUpdate();
}

void RenderWidget::wheelEvent(QWheelEvent* event)
{
	if (_viewport == nullptr || _viewport->camera.getViewpoint() != 0 || _viewport->camera.getControlMode() != 0) {
		QWidget::wheelEvent(event);
		return;
	}

	if (!_viewport->camera.isOrbitActive()) {
		vec3d pivot = _viewport->orbitCameraGetPivot();
		auto grid_orient = _viewport->The_grid ? &_viewport->The_grid->gmatrix : nullptr;
		_viewport->camera.orbitCameraInitFromCurrentView(&pivot, grid_orient);
	}

	_viewport->camera.orbitCameraZoom(event->angleDelta().y() / -200.0f);
	_viewport->needsUpdate();
	event->accept();
}

void RenderWidget::mouseMoveEvent(QMouseEvent* event) {
	auto mouseDX = event->pos() - _lastMouse;
	_lastMouse = event->pos();

	// Orbit camera: middle button drag
	if (_orbitDragging && event->buttons().testFlag(Qt::MiddleButton)) {
		handleOrbitDrag(event->pos(), event->modifiers());
		return;
	}

	// Orbit camera: right button drag
	if (_rbuttonDown && event->buttons().testFlag(Qt::RightButton) && _viewport != nullptr
		&& _viewport->camera.getViewpoint() == 0 && _viewport->camera.getControlMode() == 0) {
		if (!_rbuttonMoved) {
			if (abs(event->pos().x() - _rbuttonDownPoint.x()) > 2
				|| abs(event->pos().y() - _rbuttonDownPoint.y()) > 2)
				_rbuttonMoved = true;
		}
		if (_rbuttonMoved) {
			handleOrbitDrag(event->pos(), event->modifiers());
			return;
		}
	}

// Update marking box
	_markingBox.x2 = event->position().x() * _window->devicePixelRatio();
	_markingBox.y2 = event->position().y() * _window->devicePixelRatio();

	// If a viewport handle is currently being dragged, route every move to
	// drag_handle and skip the rest of the mission-object-drag path.
	if (_handleGrabbed) {
		if (event->buttons().testFlag(Qt::LeftButton)) {
			_viewport->drag_handle(event->position().x() * _window->devicePixelRatio(),
				event->position().y() * _window->devicePixelRatio());
		}
		return;
	}

	// Hand the hovered handle to the viewport so the renderer can draw its hover
	// balloon. Computed in every mode, matching how a real object's info shows
	// on hover regardless of editing mode. The move cursor, though, is only
	// shown in Moving mode since that's the only mode that can grab a handle
	// (see the press-time pre-pass above).
	EditorViewport::HandlePick hoverPick = _viewport->pick_handle(event->position().x() * _window->devicePixelRatio(),
		event->position().y() * _window->devicePixelRatio());
	_viewport->setHoveredHandle(hoverPick);
	_hoveringHandle = (hoverPick.group_index >= 0) && (_viewport->Editing_mode == CursorMode::Moving);

	// RT point

	_viewport->Cursor_over = _viewport->select_object(event->position().x() * _window->devicePixelRatio(),
		event->position().y() * _window->devicePixelRatio());
	updateCursor();

	if (!event->buttons().testFlag(Qt::LeftButton)) {
		_viewport->button_down = false;
	}

	if (_viewport->button_down) {
		if (abs(_markingBox.x1 - _markingBox.x2) > 1 || abs(_markingBox.y1 - _markingBox.y2) > 1) {
			_viewport->moved = true;
		}

		if (_viewport->moved) {
			if (_viewport->on_object != -1 || _viewport->Selection_lock) {
				if (_viewport->Editing_mode == CursorMode::Moving) {
					_viewport->drag_objects(event->position().x() * _window->devicePixelRatio(),
						event->position().y() * _window->devicePixelRatio());
				} else if (_viewport->Editing_mode == CursorMode::Rotating) {
					_viewport->drag_rotate_objects(mouseDX.x(), mouseDX.y());
				}

			} else {
				_usingMarkingBox = true;
			}

			if (mouseDX.x() || mouseDX.y()) {
				_viewport->needsUpdate();
			}
		}
	}
}
void RenderWidget::mouseReleaseEvent(QMouseEvent* event) {
	if (event->button() == Qt::MiddleButton) {
		_orbitDragging = false;
		return;
	}

	if (event->button() == Qt::RightButton) {
		bool wasDragging = _rbuttonMoved;
		_rbuttonDown = false;
		_rbuttonMoved = false;

		if (!wasDragging) {
			// No drag occurred — show context menu
			auto parentView = static_cast<FredView*>(parentWidget());
			Q_ASSERT(parentView);
			parentView->showContextMenu(event->globalPosition().toPoint());
		}
		return;
	}

	if (event->button() != Qt::LeftButton) {
		// Ignore everything that has nothing to do with the left button
		return QWidget::mouseReleaseEvent(event);
	}

	// End any active viewport-handle drag via the commit path, which fires the
	// handle's on_release. Dialog-owned handles leave on_release unset and mark
	// their own dirty state; the direct-edit volumetric handle uses it to call
	// missionChanged() exactly once for the drag.
	if (_handleGrabbed) {
		_viewport->commit_handle_drag();
		_handleGrabbed = false;
		_viewport->needsUpdate();
		event->accept();
		return;
	}

	_markingBox.x2 = event->position().x() * _window->devicePixelRatio();
	_markingBox.y2 = event->position().y() * _window->devicePixelRatio();

	if (_viewport->button_down) {
		if (abs(_markingBox.x1 - _markingBox.x2) > 1 || abs(_markingBox.y1 - _markingBox.y2) > 1) {
			_viewport->moved = true;
		}

		if (_viewport->moved) {
			if ((_viewport->on_object != -1) || _viewport->Selection_lock) {
				if (_viewport->Editing_mode == CursorMode::Moving) {
					_viewport->drag_objects(event->position().x() * _window->devicePixelRatio(),
						event->position().y() * _window->devicePixelRatio());
				} else if (_viewport->Editing_mode == CursorMode::Rotating) {
					_viewport->drag_rotate_objects(0, 0);
				}

				fred->missionChanged();

			} else {
				_usingMarkingBox = true;
			}
		}

		if (_usingMarkingBox) {
			_viewport->select_objects(_markingBox);
			_usingMarkingBox = false;

		} else if ((!_viewport->moved && _viewport->on_object != -1) && !_viewport->Selection_lock
			&& !event->modifiers().testFlag(Qt::ShiftModifier)) {
			fred->unmark_all();
			fred->markObject(_viewport->on_object);
		}

		_viewport->button_down = false;
		_viewport->needsUpdate();

		if (_viewport->Dup_drag == EditorViewport::DUP_DRAG_OF_WING) {
			int ship;
			object* objp;

			QString msg = tr("Add cloned ships to wing %1?").arg(Wings[_viewport->Duped_wing].name);
			if (QMessageBox::question(this, tr("Query"), msg) == QMessageBox::Yes) {
				objp = GET_FIRST(&obj_used_list);
				while (objp != END_OF_LIST(&obj_used_list)) {
					if (objp->flags[Object::Object_Flags::Marked]) {
						if (Wings[_viewport->Duped_wing].wave_count >= MAX_SHIPS_PER_WING) {
							QMessageBox::information(this, "Warning", "Max ships per wing limit reached");
							break;
						}

						// Can't do player starts, since only player 1 is currently allowed to be in a wing
						Assert(objp->type == OBJ_SHIP);
						ship = objp->instance;
						Assert(Ships[ship].wingnum == -1);
						wing_bash_ship_name(&Ships[ship], &Wings[_viewport->Duped_wing], Wings[_viewport->Duped_wing].wave_count + 1, true);

						Wings[_viewport->Duped_wing].ship_index[Wings[_viewport->Duped_wing].wave_count] = ship;
						Ships[ship].wingnum = _viewport->Duped_wing;

						fred->wing_objects[_viewport->Duped_wing][Wings[_viewport->Duped_wing].wave_count] =
							OBJ_INDEX(objp);
						Wings[_viewport->Duped_wing].wave_count++;
					}

					objp = GET_NEXT(objp);
				}
			}
		}
	}
}
void RenderWidget::updateCursor() const {
	// Hovering a viewport handle: always show the move cursor (handles only
	// translate, and the press-time pre-pass already gated this on Moving
	// mode, so we know clicking would actually grab).
	if (_hoveringHandle) {
		_window->setCursor(*_moveCursor);
		return;
	}

	if (_viewport->Cursor_over >= 0) {
		switch (_cursorMode) {
		case CursorMode::Selecting:
			_window->setCursor(*_standardCursor);
			break;
		case CursorMode::Moving:
			_window->setCursor(*_moveCursor);
			break;
		case CursorMode::Rotating:
			_window->setCursor(*_rotateCursor);
			break;
		}
	} else {
		_window->setCursor(*_standardCursor);
	}
}
void RenderWidget::setEditor(Editor* editor, EditorViewport* viewport) {
	Assertion(fred == nullptr, "Render widget currently does not support resetting the editor!");
	Assertion(_viewport == nullptr, "Render widget currently does not support resetting the viewport!");

	Assertion(editor != nullptr, "Invalid editor pointer passed!");
	Assertion(viewport != nullptr, "Invalid viewport pointer passed!");

	fred = editor;
	_viewport = viewport;

	_window->setEditor(editor, _viewport->renderer);
}
void RenderWidget::setCursorMode(CursorMode mode) {
	_cursorMode = mode;
}
void RenderWidget::renderFrame() {
	qreal scale = _window->devicePixelRatio();
	_viewport->renderer->render_frame(fred->currentObject, fred->Render_subsys, _usingMarkingBox, _markingBox, scale);
}
} // namespace fso::fred
