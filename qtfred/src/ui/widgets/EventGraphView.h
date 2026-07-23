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
	QColor entity{217, 131, 42};  // ship / wing / waypoint
	QColor message{124, 74, 168};
	QColor goal{179, 74, 124};
	QColor variable{138, 106, 42};
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

	// Repopulate the selector (preserving the current object where possible) and
	// rebuild the current view's scene from the current data.
	void reload();

	void zoomToFitAll(qreal margin = 40.0);

  signals:
	// Single-click (select) an event or node card → sync-select that event
	// elsewhere (e.g. the tree view) without switching views.
	void eventSelected(int eventIndex);
	// Double-click an event card → request a jump to that event in the tree view.
	void eventActivated(int eventIndex);
	// Double-click a condition/action node card → jump to that specific tree node.
	void nodeActivated(int treeNode);

  protected:
	void wheelEvent(QWheelEvent* e) override;
	void drawBackground(QPainter* painter, const QRectF& rect) override;
	void mousePressEvent(QMouseEvent* e) override;
	void mouseDoubleClickEvent(QMouseEvent* e) override;
	void resizeEvent(QResizeEvent* e) override;
	void showEvent(QShowEvent* e) override;
	void scrollContentsBy(int dx, int dy) override;

  private:
	enum class Mode { Radial, Swimlanes, Basic };

	void buildOverlay();
	void buildSettingsMenu();
	void positionOverlay();
	void applyTheme(bool dark);
	void populateSelector();
	void rebuildRadial();
	void showEmptyMessage(const QString& text);
	int  selectedObjectRow() const;

	QGraphicsScene* m_scene = nullptr;
	const EventReferenceIndex* m_index = nullptr;
	QVector<QString> m_eventNames;
	QVector<GraphObject> m_objects;

	EventGraphStyle m_style;
	Mode m_mode = Mode::Radial;

	// Top-left overlay controls.
	QWidget*      m_overlay = nullptr;
	QButtonGroup* m_modeGroup = nullptr;
	QComboBox*    m_objectCombo = nullptr;
	QMenu*        m_settingsMenu = nullptr;

	// Bottom-left minimap.
	graphdetail::MinimapWidget* m_minimap = nullptr;

	qreal m_currentScale = 1.0;
	const qreal kMinScale = 0.2;
	const qreal kMaxScale = 3.0;

	// View continuity across rebuilds: keep zoom/center (and re-select) when the
	// same object is still shown, instead of re-fitting to the whole graph.
	QString m_framedKey;
	bool    m_hasFramed = false;
	bool    m_suppressSelectionSignal = false;
};

} // namespace fso::fred
