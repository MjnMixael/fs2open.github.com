#if defined(_MSC_VER) && _MSC_VER <= 1920
	// work around MSVC 2015 and 2017 compiler bug
	// https://bugreports.qt.io/browse/QTBUG-72073
	#define QT_NO_FLOAT16_OPERATORS
#endif

#include "BriefingMapWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QtGui/QOpenGLContext>
#include <QtWidgets/QHBoxLayout>

#include "FredApplication.h"
#include "mission/dialogs/BriefingEditorDialogModel.h"
#include "mission/EditorViewport.h"

#include "graphics/2d.h"
#include "render/3d.h"
#include "mission/missionbriefcommon.h"

namespace fso::fred {

// ---- BriefingMapWindow ----

BriefingMapWindow::BriefingMapWindow(QWidget* parentWidget)
	: QWindow(static_cast<QWindow*>(nullptr)), _parentWidget(parentWidget)
{
	setSurfaceType(QWindow::OpenGLSurface);
}

void BriefingMapWindow::initializeGL(const QSurfaceFormat& fmt) {
	setFormat(fmt);
	create();
}

bool BriefingMapWindow::event(QEvent* evt) {
	switch (evt->type()) {
	case QEvent::KeyPress:
	case QEvent::KeyRelease:
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseMove:
		// Forward input events to the parent widget
		qGuiApp->sendEvent(_parentWidget, evt);
		return true;
	default:
		return QWindow::event(evt);
	}
}

void BriefingMapWindow::exposeEvent(QExposeEvent* event) {
	if (isExposed()) {
		requestUpdate();
		event->accept();
	} else {
		QWindow::exposeEvent(event);
	}
}

// ---- BriefingMapWidget ----

BriefingMapWidget::BriefingMapWidget(QWidget* parent,
	dialogs::BriefingEditorDialogModel* model,
	EditorViewport* viewport)
	: QWidget(parent), _model(model), _viewport(viewport)
{
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);

	_window = new BriefingMapWindow(this);

	auto layout = new QHBoxLayout(this);
	layout->setSpacing(0);
	layout->addWidget(QWidget::createWindowContainer(_window, this));
	layout->setContentsMargins(0, 0, 0, 0);
	setLayout(layout);

	_renderTimer = new QTimer(this);
	_renderTimer->setInterval(33); // ~30 fps
	connect(_renderTimer, &QTimer::timeout, this, &BriefingMapWidget::renderFrame);

	fredApp->runAfterInit([this]() { initBriefingMap(); });
}

BriefingMapWidget::~BriefingMapWidget() {
	_renderTimer->stop();
}

void BriefingMapWidget::initBriefingMap() {
	// Cache the GL context — during init, the main renderer has it current.
	// We need this because QOpenGLContext::currentContext() may return null
	// later when the modal dialog blocks the main render loop.
	_glContext = QOpenGLContext::currentContext();
	if (_glContext) {
		_window->initializeGL(_glContext->format());
	}

	// Initialize the briefing rendering subsystem.
	// This mirrors what brief_init(true) does in the Lua API path:
	// set the Briefing pointer, init the map (camera, grid, animations),
	// set the initial camera target, and reset icon state.
	auto* briefPtr = _model->getWipBriefingPtr(_model->getCurrentTeam());
	if (briefPtr) {
		briefing* savedBriefing = Briefing;
		Briefing = briefPtr;

		brief_init_map();

		if (Briefing->num_stages > 0) {
			brief_set_new_stage(&Briefing->stages[0].camera_pos,
				&Briefing->stages[0].camera_orient, 0, 0);
			brief_reset_icons(0);
		}

		Briefing = savedBriefing;
	}

	_initialized = true;
	_renderTimer->start();
}

void BriefingMapWidget::setStage(int stageNum) {
	if (!_initialized)
		return;

	auto* briefPtr = _model->getWipBriefingPtr(_model->getCurrentTeam());
	if (!briefPtr || stageNum < 0 || stageNum >= briefPtr->num_stages)
		return;

	// Save the Briefing pointer, set it to our WIP data
	briefing* savedBriefing = Briefing;
	Briefing = briefPtr;

	auto& stage = briefPtr->stages[stageNum];
	brief_reset_last_new_stage();
	brief_set_new_stage(&stage.camera_pos, &stage.camera_orient,
		stage.camera_time, stageNum);
	brief_reset_icons(stageNum);

	_currentStage = stageNum;

	Briefing = savedBriefing;
}

int BriefingMapWidget::getCurrentStage() const {
	return _currentStage;
}

QWindow* BriefingMapWidget::getRenderWindow() const {
	return _window;
}

void BriefingMapWidget::renderFrame() {
	if (!_initialized || !_window->isExposed())
		return;

	// Guard against re-entrancy: swapBuffers() can pump the Qt event loop on Windows,
	// which may fire our timer again while we're still mid-render.
	if (_rendering)
		return;

	// Skip if a 3D frame is already open (e.g. main renderer still active during init).
	// The timer will try again on the next tick.
	if (g3_in_frame())
		return;

	_rendering = true;

	if (!_glContext) {
		_rendering = false;
		return;
	}

	// Switch the shared GL context to render onto our surface
	_glContext->makeCurrent(_window);

	int w = _window->width();
	int h = _window->height();

	// Save and set bscreen to fill our widget
	brief_screen savedBscreen = bscreen;
	bscreen.map_x1 = 0;
	bscreen.map_y1 = 0;
	bscreen.map_x2 = w;
	bscreen.map_y2 = h;
	bscreen.resize = GR_RESIZE_NONE;

	// Point Briefing at our WIP data
	briefing* savedBriefing = Briefing;
	Briefing = _model->getWipBriefingPtr(_model->getCurrentTeam());

	// Resize the graphics state for our surface
	gr_screen_resize(w, h);

	// Clear the surface
	gr_clear();

	// Update camera animation
	float frametime = 0.033f; // fixed timestep matching our timer
	brief_camera_move(frametime, _currentStage);

	// Render the briefing map
	brief_render_map(_currentStage, frametime);

	// Emit camera position for any listeners
	cameraChanged(brief_get_current_cam_pos(), brief_get_current_cam_orient());

	// Swap buffers on our window
	_glContext->swapBuffers(_window);

	// Restore state
	Briefing = savedBriefing;
	bscreen = savedBscreen;

	_rendering = false;
}

void BriefingMapWidget::keyPressEvent(QKeyEvent* event) {
	if (!_initialized)
		return;

	// Temporary keyboard camera controls
	vec3d camPos = brief_get_current_cam_pos();
	matrix camOrient = brief_get_current_cam_orient();
	bool moved = false;
	const float PAN_SPEED = 50.0f;
	const float ZOOM_SPEED = 100.0f;

	switch (event->key()) {
	case Qt::Key_Left:
		vm_vec_scale_add2(&camPos, &camOrient.vec.rvec, -PAN_SPEED);
		moved = true;
		break;
	case Qt::Key_Right:
		vm_vec_scale_add2(&camPos, &camOrient.vec.rvec, PAN_SPEED);
		moved = true;
		break;
	case Qt::Key_Up:
		vm_vec_scale_add2(&camPos, &camOrient.vec.uvec, PAN_SPEED);
		moved = true;
		break;
	case Qt::Key_Down:
		vm_vec_scale_add2(&camPos, &camOrient.vec.uvec, -PAN_SPEED);
		moved = true;
		break;
	case Qt::Key_Plus:
	case Qt::Key_Equal:
		vm_vec_scale_add2(&camPos, &camOrient.vec.fvec, ZOOM_SPEED);
		moved = true;
		break;
	case Qt::Key_Minus:
		vm_vec_scale_add2(&camPos, &camOrient.vec.fvec, -ZOOM_SPEED);
		moved = true;
		break;
	case Qt::Key_PageUp:
		camPos.xyz.y += PAN_SPEED;
		moved = true;
		break;
	case Qt::Key_PageDown:
		camPos.xyz.y -= PAN_SPEED;
		moved = true;
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}

	if (moved) {
		// Apply instant camera move by setting new stage with time=0
		auto* briefPtr = _model->getWipBriefingPtr(_model->getCurrentTeam());
		if (briefPtr && _currentStage >= 0 && _currentStage < briefPtr->num_stages) {
			auto& stage = briefPtr->stages[_currentStage];
			stage.camera_pos = camPos;

			briefing* savedBriefing = Briefing;
			Briefing = briefPtr;
			// Reset Last_new_stage so brief_set_new_stage accepts the same stage
			brief_reset_last_new_stage();
			brief_set_new_stage(&camPos, &camOrient, 0, _currentStage);
			Briefing = savedBriefing;

			_model->setCameraPosition(camPos);
			cameraChanged(camPos, camOrient);
		}
	}
}

void BriefingMapWidget::mousePressEvent(QMouseEvent* event) {
	if (!_initialized || event->button() != Qt::LeftButton)
		return;

	_lastMousePos = event->pos();

	// Hit-test icons: check which icon (if any) is under the cursor
	// For now, just store the click position for potential drag operations
	// Full icon hit-testing will be implemented once we can retrieve projected icon positions
	_draggingIcon = false;
	_dragIconIndex = -1;
}

void BriefingMapWidget::mouseMoveEvent(QMouseEvent* event) {
	if (!_initialized)
		return;

	_lastMousePos = event->pos();

	// Icon dragging will be implemented once we have icon screen positions from the renderer
}

void BriefingMapWidget::mouseReleaseEvent(QMouseEvent* event) {
	if (!_initialized || event->button() != Qt::LeftButton)
		return;

	_draggingIcon = false;
	_dragIconIndex = -1;
}

} // namespace fso::fred
