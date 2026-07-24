#pragma once

// Events editor graph view — a relationship visualizer over the working sexps.
// Milestone 1 renders the "Radial" mode: pick a ship/wing and see every event
// that references it. Modeled on CampaignMissionGraph (the existing QGraphicsView
// node editor). Read-only for now; no graph editing.

#include "mission/dialogs/EventReferenceIndex.h"

#include <QGraphicsView>
#include <QVector>
#include <QString>
#include <QColor>
#include <QSet>

class QGraphicsScene;
class QComboBox;
class QButtonGroup;
class QWidget;
class QMenu;

namespace fso::fred {

namespace graphdetail {
class CardItem;
class ObjectNodeItem;
class EventNodeItem;
class SexpNodeItem;
class RefEdgeItem;
class MinimapWidget;
class LegendWidget;
} // namespace graphdetail

// Theme-adaptive visual constants shared by the view and its items. Card fills
// are subtle whole-card tints (from the design wireframe), with both light and
// dark values set by makeStyle().
struct EventGraphStyle {
	QColor bgColor{251, 249, 244};
	QColor gridMinor{231, 226, 212};

	// Card fills by role/kind.
	QColor objectFill{242, 239, 230};
	QColor eventFill{255, 246, 232};   // cream
	QColor condFill{233, 242, 255};    // light blue
	QColor actionFill{247, 236, 246};  // light pink
	QColor cardBorder{207, 201, 187};

	QColor nodeText{26, 26, 26};
	QColor nodeSubText{74, 74, 74};

	// Chip (small kind/role label) colors.
	QColor condChip{47, 111, 179};
	QColor actionChip{124, 74, 168};
	QColor eventChip{120, 120, 120};

	// Event badge (the small event-name box on cond/action cards).
	QColor eventBadge{150, 120, 70};

	// Object-kind accent colors (chip on the center object card).
	QColor entity{217, 131, 42};    // "objects": ship / wing / waypoint path / jump node / prop / coord point
	QColor message{198, 54, 47};    // red
	QColor dataColor{179, 74, 124}; // "data": any other OPF data kind (goal, team, event name, ...)
	QColor variable{138, 106, 42};
	QColor container{38, 145, 130};  // teal, distinct from the variable brown
	QColor eventAccent{90, 90, 90};

	// Tier ring guide (dashed circles).
	QColor ringColor{170, 165, 152};

	qreal edgeWidth{1.8};
	qreal nodeRadius{7.0};

	static EventGraphStyle makeStyle(bool dark);
	QColor colorFor(RefObjectKind kind) const;
};

// A pickable object in the radial selector.
struct GraphObject {
	RefObjectKind kind = RefObjectKind::Unknown;
	QString       name;
};

class EventGraphView final : public QGraphicsView {
	Q_OBJECT
  public:
	explicit EventGraphView(QWidget* parent = nullptr);
	~EventGraphView() override;

	bool eventFilter(QObject* watched, QEvent* event) override;

	// Data feeds (owned by the dialog). Call reload() after any of these change.
	void setReferenceIndex(const EventReferenceIndex* index) { m_index = index; }
	void setEventNames(QVector<QString> names) { m_eventNames = std::move(names); }
	void setObjects(QVector<GraphObject> objects) { m_objects = std::move(objects); }
	// The whole-mission dataflow graph for the Basic view, with node positions
	// already resolved from saved annotations by the dialog.
	void setBasicGraph(BasicGraph graph) { m_basicGraph = std::move(graph); }

	// Repopulate the selector (preserving the current object where possible) and
	// rebuild the current view's scene from the current data.
	void reload();

	void zoomToFitAll(qreal margin = 40.0);
	// Zoom one step in/out, centered on the current view center (used by the
	// minimap wheel, which is away from the content).
	void zoomStep(bool zoomIn);

  signals:
	// Single-click (select) an event or node card → sync-select that event
	// elsewhere (e.g. the tree view) without switching views.
	void eventSelected(int eventIndex);
	// Double-click an event card → request a jump to that event in the tree view.
	void eventActivated(int eventIndex);
	// Double-click a condition/action node card → jump to that specific tree node.
	void nodeActivated(int treeNode);
	// Basic view: a node card was dragged to a new position. `key` is the
	// annotation key to persist the position under; (x, y) is the scene position.
	void nodeMoved(int key, double x, double y);

  protected:
	void wheelEvent(QWheelEvent* e) override;
	void drawBackground(QPainter* painter, const QRectF& rect) override;
	void mousePressEvent(QMouseEvent* e) override;
	void mouseMoveEvent(QMouseEvent* e) override;
	void mouseReleaseEvent(QMouseEvent* e) override;
	void mouseDoubleClickEvent(QMouseEvent* e) override;
	void keyPressEvent(QKeyEvent* e) override;
	void resizeEvent(QResizeEvent* e) override;
	void showEvent(QShowEvent* e) override;
	void scrollContentsBy(int dx, int dy) override;

  private:
	enum class Mode { Radial, Swimlanes, Basic };

	void buildOverlay();
	void rebuildSettingsMenu();
	void positionOverlay();
	void updateChromeVisibility();
	void applyTheme(bool dark);
	void populateSelector();
	void populateKindFilter();
	void populateSelectorForMode();
	// Basic: a manual node drag flips the layout dropdown to Custom.
	void setBasicLayoutToCustom();
	void setMode(Mode mode);
	void rebuildCurrent();
	void rebuildRadial();
	void rebuildSwimlanes();
	void rebuildBasic();
	// Shrink the scene rect back to the current content (+margin). The default
	// scene rect only grows, so without this a big swimlanes layout leaves radial
	// / basic pinned to a corner and out of sync with the minimap.
	void updateSceneRect();
	// Selection-driven emphasis: reference-line on-select visibility + focus fade.
	void applyEmphasis();
	// Swimlanes cross-filter: focus an object row / event column (Ctrl adds).
	void toggleSwimObjectFocus(const QString& objectKey, bool add);
	void toggleSwimEventFocus(int eventIndex, bool add);
	void clearSwimFocus();
	void showEmptyMessage(const QString& text);
	int  selectedObjectRow() const;

	QGraphicsScene* m_scene = nullptr;
	const EventReferenceIndex* m_index = nullptr;
	QVector<QString> m_eventNames;
	QVector<GraphObject> m_objects;
	BasicGraph m_basicGraph;

	// Basic view drag tracking: the card grabbed on press and its starting scene
	// position, so a real move (not a click) emits nodeMoved on release.
	graphdetail::CardItem* m_dragItem = nullptr;
	QPointF m_dragStartPos;
	// Basic view uses NoDrag (so cards drag individually), so panning on empty
	// canvas is handled manually.
	bool   m_panning = false;
	QPoint m_panLastPos;

	EventGraphStyle m_style;
	Mode m_mode = Mode::Radial;

	// Top-left overlay controls.
	QWidget*      m_overlay = nullptr;
	QButtonGroup* m_modeGroup = nullptr;
	QComboBox*    m_objectCombo = nullptr;
	QMenu*        m_settingsMenu = nullptr;

	// Bottom-left minimap.
	graphdetail::MinimapWidget* m_minimap = nullptr;
	// Top-right color-key legend.
	graphdetail::LegendWidget* m_legend = nullptr;

	qreal m_currentScale = 1.0;
	const qreal kMinScale = 0.2;
	const qreal kMaxScale = 3.0;

	// View continuity across rebuilds: keep zoom/center (and re-select) when the
	// same object is still shown, instead of re-fitting to the whole graph.
	QString m_framedKey;
	bool    m_hasFramed = false;
	bool    m_suppressSelectionSignal = false;

	// Swimlanes filters. m_swimKind == Unknown means "all kinds".
	RefObjectKind m_swimKind = RefObjectKind::Unknown;
	QSet<QString> m_focusObjects; // object row keys
	QSet<int>     m_focusEvents;
	// Track an empty-space press so a click (not a pan-drag) on empty canvas
	// clears the swimlanes filter on release.
	bool   m_pressOnEmpty = false;
	QPoint m_pressPos;
};

} // namespace fso::fred
