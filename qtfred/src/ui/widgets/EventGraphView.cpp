#include "EventGraphView.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsPathItem>
#include <QFontMetricsF>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QTextOption>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace fso::fred {

// Distance between adjacent radial rings; a graph setting that persists for the
// session (across object and view changes), adjustable via the settings gear.
static qreal s_ringSpacing = 210.0;

// Stable identity key for a swimlanes object row (kind + case-insensitive name).
static QString swimRowKey(RefObjectKind kind, const QString& name)
{
	return QString::number(static_cast<int>(kind)) + QLatin1Char('|') + name.toLower();
}

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------

EventGraphStyle EventGraphStyle::makeStyle(bool dark)
{
	EventGraphStyle s; // light defaults are the struct initializers
	if (dark) {
		s.bgColor     = QColor(31, 29, 26);
		s.gridMinor   = QColor(51, 47, 40);
		s.objectFill  = QColor(42, 40, 36);
		s.eventFill   = QColor(59, 42, 24);
		s.condFill    = QColor(29, 42, 61);
		s.actionFill  = QColor(45, 29, 43);
		s.cardBorder  = QColor(74, 70, 62);
		s.nodeText    = QColor(233, 230, 222);
		s.nodeSubText = QColor(180, 175, 163);
		s.eventChip   = QColor(150, 150, 150);
		s.eventBadge  = QColor(190, 160, 110);
		s.ringColor   = QColor(74, 70, 62);
	}
	// Chip accent colors (blue/purple/orange) are vivid enough for both themes.
	return s;
}

QColor EventGraphStyle::colorFor(RefObjectKind kind) const
{
	switch (kind) {
	case RefObjectKind::Ship:
	case RefObjectKind::Wing:
	case RefObjectKind::Prop:
	case RefObjectKind::Waypoint:
	case RefObjectKind::JumpNode:
	case RefObjectKind::CoordinatePoint:
	case RefObjectKind::Team:
		return entity;
	case RefObjectKind::Message:  return message;
	case RefObjectKind::Goal:     return goal;
	case RefObjectKind::Variable: return variable;
	case RefObjectKind::Event:    return eventAccent;
	default:                      return eventAccent;
	}
}

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------

namespace graphdetail {

static constexpr qreal kNodeW = 224.0;
static constexpr qreal kEventW = 158.0;
static constexpr qreal kObjectW = 224.0;

// Running Z so a clicked card jumps above all others (overlaps are fine).
static qreal s_cardTopZ = 100.0;

// Layout metrics (item pixels).
namespace metric {
constexpr qreal padX = 12.0, padTop = 7.0, padBottom = 8.0;
constexpr qreal chipH = 14.0, titleH = 20.0, lineH = 15.0, subH = 14.0, toggleH = 15.0;
} // namespace metric

// A card: a role/kind chip (top-left), an optional event-name badge (top-right),
// a bold title, an optional bulleted body (the operator's arguments), an
// optional subtitle, and — when the body is long — an expand/collapse toggle.
class CardItem : public QGraphicsItem {
  public:
	CardItem(qreal width, QColor fill, QColor chipColor, QString chip, QString title, QVector<QString> lines,
		QString subtitle, QString cornerText, QColor cornerColor, bool expandable, int collapsedMax,
		const EventGraphStyle& style)
		: m_width(width), m_fill(fill), m_chipColor(chipColor), m_chip(std::move(chip)), m_title(std::move(title)),
		  m_lines(std::move(lines)), m_subtitle(std::move(subtitle)), m_cornerText(std::move(cornerText)),
		  m_cornerColor(cornerColor), m_expandable(expandable), m_collapsedMax(collapsedMax), m_style(style)
	{
		setFlag(ItemIsSelectable, true);
		// Show the normal pointer over cards instead of the pan hand.
		setCursor(Qt::ArrowCursor);
		setAcceptHoverEvents(true);
	}

	QRectF boundingRect() const override
	{
		const qreal h = contentHeight();
		return QRectF(-m_width / 2, -h / 2, m_width, h);
	}

	// Raise above every other card; called on any click.
	void bringToFront() { setZValue(s_cardTopZ += 1.0); }

	QString titleText() const { return m_title; }

	// Distinct highlight for a swimlanes filter item (independent of selection).
	void setFocusHighlight(bool on) { m_focusHighlight = on; update(); }

	// If the click landed on the expand/collapse toggle, flip it. Returns true
	// if it consumed the click.
	bool handleToggleClick(const QPointF& scenePos)
	{
		if (!hasToggle())
			return false;
		if (m_toggleRect.contains(mapFromScene(scenePos))) {
			prepareGeometryChange();
			m_expanded = !m_expanded;
			update();
			return true;
		}
		return false;
	}

	void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override
	{
		p->setRenderHint(QPainter::Antialiasing, true);
		const QRectF r = boundingRect();

		// Filter focus (red) wins over tree selection (orange) wins over normal.
		QColor borderColor = m_style.cardBorder;
		qreal borderW = 1.4;
		if (m_focusHighlight) {
			borderColor = QColor(214, 40, 40);
			borderW = 2.8;
		} else if (isSelected()) {
			borderColor = QColor(217, 79, 42);
			borderW = 2.4;
		}
		p->setPen(QPen(borderColor, borderW));
		p->setBrush(m_fill);
		p->drawRoundedRect(r, m_style.nodeRadius, m_style.nodeRadius);

		const QRectF inner = r.adjusted(metric::padX, metric::padTop, -10.0, -metric::padBottom);
		qreal y = inner.top();

		// Top-right event badge.
		if (!m_cornerText.isEmpty()) {
			QFont bf = p->font();
			bf.setPointSizeF(std::max(6.0, bf.pointSizeF() - 2.0));
			QFontMetricsF fm(bf);
			const qreal maxW = m_width * 0.55;
			const QString bt = fm.elidedText(m_cornerText, Qt::ElideRight, maxW - 10.0);
			const qreal bw = std::min(maxW, fm.horizontalAdvance(bt) + 10.0);
			const QRectF badge(inner.right() - bw, y - 1.0, bw, 15.0);
			QColor fillc = m_cornerColor;
			fillc.setAlpha(38);
			p->setPen(QPen(m_cornerColor, 1.1));
			p->setBrush(fillc);
			p->drawRoundedRect(badge, 3.0, 3.0);
			p->setFont(bf);
			p->setPen(m_cornerColor);
			p->drawText(badge, Qt::AlignCenter, bt);
		}

		// Chip (role/kind) top-left.
		QFont chipFont = p->font();
		chipFont.setPointSizeF(std::max(6.0, chipFont.pointSizeF() - 1.5));
		p->setFont(chipFont);
		p->setPen(m_chipColor);
		p->drawText(QRectF(inner.left(), y, inner.width() * 0.45, metric::chipH),
			Qt::AlignLeft | Qt::AlignVCenter, m_chip.toUpper());
		y += metric::chipH + 2.0;

		// Title.
		if (!m_title.isEmpty()) {
			QFont titleFont = p->font();
			titleFont.setBold(true);
			titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
			p->setFont(titleFont);
			p->setPen(m_style.nodeText);
			const QString t = QFontMetricsF(titleFont).elidedText(m_title, Qt::ElideRight, inner.width());
			p->drawText(QRectF(inner.left(), y, inner.width(), metric::titleH),
				Qt::AlignLeft | Qt::AlignVCenter, t);
			y += metric::titleH;
		}

		// Bulleted body lines.
		QFont lineFont = p->font();
		lineFont.setBold(false);
		p->setFont(lineFont);
		p->setPen(m_style.nodeSubText);
		QFontMetricsF lfm(lineFont);
		const int shown = visibleLineCount();
		for (int i = 0; i < shown; ++i) {
			const QString line = QString(QChar(0x2022)) + QLatin1Char(' ') + m_lines[i]; // bullet
			const QString el = lfm.elidedText(line, Qt::ElideRight, inner.width());
			p->drawText(QRectF(inner.left(), y, inner.width(), metric::lineH),
				Qt::AlignLeft | Qt::AlignVCenter, el);
			y += metric::lineH;
		}

		// Expand/collapse toggle.
		if (hasToggle()) {
			m_toggleRect = QRectF(inner.left(), y, inner.width(), metric::toggleH);
			const int hidden = m_lines.size() - m_collapsedMax;
			const QString label = m_expanded ? QStringLiteral("show less")
											 : QStringLiteral("+%1 more").arg(hidden);
			QFont tf = p->font();
			tf.setItalic(true);
			tf.setPointSizeF(std::max(6.0, tf.pointSizeF() - 1.5));
			p->setFont(tf);
			p->setPen(m_chipColor);
			p->drawText(m_toggleRect, Qt::AlignLeft | Qt::AlignVCenter, label);
			y += metric::toggleH;
		}

		// Subtitle.
		if (!m_subtitle.isEmpty()) {
			QFont subFont = p->font();
			subFont.setItalic(true);
			subFont.setPointSizeF(std::max(6.0, subFont.pointSizeF() - 1.5));
			p->setFont(subFont);
			p->setPen(m_style.nodeSubText);
			const QString sub = QFontMetricsF(subFont).elidedText(m_subtitle, Qt::ElideRight, inner.width());
			p->drawText(QRectF(inner.left(), y, inner.width(), metric::subH),
				Qt::AlignLeft | Qt::AlignVCenter, sub);
		}
	}

  protected:
	bool hasToggle() const { return m_expandable && m_lines.size() > m_collapsedMax; }

	int visibleLineCount() const
	{
		if (!hasToggle() || m_expanded)
			return m_lines.size();
		return m_collapsedMax;
	}

	qreal contentHeight() const
	{
		qreal h = metric::padTop + metric::chipH + 2.0;
		if (!m_title.isEmpty())
			h += metric::titleH;
		h += visibleLineCount() * metric::lineH;
		if (hasToggle())
			h += metric::toggleH;
		if (!m_subtitle.isEmpty())
			h += metric::subH;
		h += metric::padBottom;
		return h;
	}

	qreal   m_width;
	QColor  m_fill;
	QColor  m_chipColor;
	QString m_chip;
	QString m_title;
	QVector<QString> m_lines;
	QString m_subtitle;
	QString m_cornerText;
	QColor  m_cornerColor;
	bool    m_expandable;
	int     m_collapsedMax;
	bool    m_expanded = false;
	bool    m_focusHighlight = false;
	QRectF  m_toggleRect;
	EventGraphStyle m_style;
};

// The center object (ship/wing).
class ObjectNodeItem final : public CardItem {
  public:
	enum { Type = UserType + 1 };
	ObjectNodeItem(QString chip, QColor chipColor, QString title, QString subtitle, const EventGraphStyle& style)
		: CardItem(kObjectW, style.objectFill, chipColor, std::move(chip), std::move(title), {},
			  std::move(subtitle), QString(), QColor(), /*expandable=*/false, 0, style)
	{
	}
	int type() const override { return Type; }
};

// A referencing event (tier 1) — deliberately compact: just the chip + name.
class EventNodeItem final : public CardItem {
  public:
	enum { Type = UserType + 2 };
	EventNodeItem(int eventIndex, QString title, const EventGraphStyle& style)
		: CardItem(kEventW, style.eventFill, style.eventChip, QStringLiteral("event"), std::move(title), {},
			  QString(), QString(), QColor(), /*expandable=*/false, 0, style),
		  m_eventIndex(eventIndex)
	{
	}
	int type() const override { return Type; }
	int eventIndex() const { return m_eventIndex; }

  private:
	int m_eventIndex;
};

// A condition/action operator node (tier 2/3). Title = operator name; body =
// its arguments as a bulleted list; the event it belongs to is a top-right badge.
class SexpNodeItem final : public CardItem {
  public:
	enum { Type = UserType + 3 };
	SexpNodeItem(int treeNode, int eventIndex, bool isCond, QString opName, QVector<QString> args,
		QString eventName, QString fullExpr, const EventGraphStyle& style)
		: CardItem(kNodeW, isCond ? style.condFill : style.actionFill, isCond ? style.condChip : style.actionChip,
			  isCond ? QStringLiteral("cond") : QStringLiteral("action"), std::move(opName), std::move(args),
			  QString(), std::move(eventName), style.eventBadge, /*expandable=*/true, /*collapsedMax=*/3, style),
		  m_treeNode(treeNode), m_eventIndex(eventIndex), m_isCond(isCond)
	{
		if (!fullExpr.isEmpty())
			setToolTip(fullExpr);
	}
	int type() const override { return Type; }
	int treeNode() const { return m_treeNode; }
	int eventIndex() const { return m_eventIndex; }
	bool isCond() const { return m_isCond; }

  private:
	int  m_treeNode;
	int  m_eventIndex;
	bool m_isCond;
};

class RefEdgeItem final : public QGraphicsPathItem {
  public:
	RefEdgeItem(const QPointF& a, const QPointF& b, QColor color, const EventGraphStyle& style)
	{
		QPen pen(color, style.edgeWidth, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
		setPen(pen);
		setZValue(-1.0);
		setAcceptedMouseButtons(Qt::NoButton);

		QPainterPath path(a);
		const QPointF mid = (a + b) / 2.0;
		const QPointF off(-(b.y() - a.y()) * 0.10, (b.x() - a.x()) * 0.10); // gentle bow
		path.quadTo(mid + off, b);
		setPath(path);
	}
};

// Bottom-left overview of the whole graph with a current-viewport indicator.
// The scene is rendered once into a cached pixmap (regenerate()) and only that
// pixmap + the moving viewport rectangle are drawn per repaint, so panning and
// zooming stay cheap even on huge missions.
class MinimapWidget final : public QWidget {
  public:
	MinimapWidget(EventGraphView* view, QGraphicsScene* scene, const EventGraphStyle* style, QWidget* parent)
		: QWidget(parent), m_view(view), m_scene(scene), m_style(style)
	{
		setFixedSize(190, 130);
		setToolTip(QStringLiteral("Overview - drag to pan, scroll to zoom"));
		setCursor(Qt::ArrowCursor); // not the pan hand inherited from the viewport
		setMouseTracking(true);     // so hover updates the cursor without a button
	}

	// Re-render the scene into the cached pixmap. Call when the graph content
	// changes (rebuild) or the theme changes.
	void regenerate()
	{
		QRectF src = m_scene->itemsBoundingRect();
		if (src.isEmpty()) {
			m_cache = QPixmap();
			m_src = QRectF();
			m_dst = QRectF();
			update();
			return;
		}
		src.adjust(-40, -40, 40, 40);

		const QRectF inner = QRectF(rect()).adjusted(4, 4, -4, -4);
		const qreal s = std::min(inner.width() / src.width(), inner.height() / src.height());
		QRectF dst(0, 0, src.width() * s, src.height() * s);
		dst.moveCenter(inner.center());
		m_src = src;
		m_dst = dst;

		const qreal dpr = devicePixelRatioF();
		QPixmap pm(QSize(qRound(width() * dpr), qRound(height() * dpr)));
		pm.setDevicePixelRatio(dpr);
		pm.fill(Qt::transparent);
		QPainter pp(&pm);
		pp.setRenderHint(QPainter::Antialiasing, true);
		pp.setClipRect(dst);
		m_scene->render(&pp, dst, src);
		m_cache = pm;
		update();
	}

  protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);

		const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
		p.setPen(Qt::NoPen);
		p.setBrush(m_style->bgColor);
		p.drawRoundedRect(frame, 4.0, 4.0);

		if (!m_cache.isNull())
			p.drawPixmap(0, 0, m_cache);

		m_boxRect = QRectF();
		if (!m_src.isEmpty() && m_dst.isValid()) {
			const qreal s = m_dst.width() / m_src.width();
			const QRectF viewScene = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
			QRectF vr(m_dst.left() + (viewScene.left() - m_src.left()) * s,
				m_dst.top() + (viewScene.top() - m_src.top()) * s, viewScene.width() * s,
				viewScene.height() * s);
			vr = vr.intersected(m_dst);
			m_boxRect = vr;
			p.setPen(QPen(QColor(217, 79, 42), 1.5));
			p.setBrush(Qt::NoBrush);
			p.drawRect(vr);
		}

		p.setPen(QPen(m_style->cardBorder, 1.0));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(frame, 4.0, 4.0);
	}

	void mousePressEvent(QMouseEvent* e) override
	{
		if (e->button() != Qt::LeftButton || !m_dst.isValid() || m_src.isEmpty())
			return;
		m_dragging = true;
		setCursor(Qt::ClosedHandCursor);
		recenterTo(e->pos());
	}

	void mouseMoveEvent(QMouseEvent* e) override
	{
		if (m_dragging) {
			recenterTo(e->pos());
			return;
		}
		// Hover affordance: open hand over the view box, arrow elsewhere.
		setCursor(m_boxRect.contains(QPointF(e->pos())) ? Qt::OpenHandCursor : Qt::ArrowCursor);
	}

	void mouseReleaseEvent(QMouseEvent* e) override
	{
		if (e->button() != Qt::LeftButton)
			return;
		m_dragging = false;
		setCursor(m_boxRect.contains(QPointF(e->pos())) ? Qt::OpenHandCursor : Qt::ArrowCursor);
	}

	void wheelEvent(QWheelEvent* e) override
	{
		const int dy = e->angleDelta().y();
		if (dy != 0)
			m_view->zoomStep(dy > 0);
		e->accept();
	}

  private:
	// Recenter the main view on the scene point under the given minimap position.
	void recenterTo(const QPoint& pos)
	{
		if (!m_dst.isValid() || m_src.isEmpty())
			return;
		const qreal s = m_dst.width() / m_src.width();
		const qreal x = m_src.left() + (qBound(m_dst.left(), qreal(pos.x()), m_dst.right()) - m_dst.left()) / s;
		const qreal y = m_src.top() + (qBound(m_dst.top(), qreal(pos.y()), m_dst.bottom()) - m_dst.top()) / s;
		m_view->centerOn(x, y);
	}

	EventGraphView* m_view;
	QGraphicsScene* m_scene;
	const EventGraphStyle* m_style;
	QPixmap m_cache;
	QRectF m_src, m_dst, m_boxRect;
	bool m_dragging = false;
};

} // namespace graphdetail

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------

EventGraphView::EventGraphView(QWidget* parent) : QGraphicsView(parent)
{
	const bool dark = qApp->palette().window().color().value() < 128;
	m_style = EventGraphStyle::makeStyle(dark);

	m_scene = new QGraphicsScene(this);
	setScene(m_scene);

	// Selecting an event card syncs the selection outward (e.g. to the tree).
	// Only emit for a genuine event selection so a rebuild's clear() doesn't
	// clobber the outward selection with -1.
	connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
		if (m_suppressSelectionSignal)
			return; // programmatic (re)selection during a rebuild
		for (QGraphicsItem* it : m_scene->selectedItems()) {
			if (auto* ev = qgraphicsitem_cast<graphdetail::EventNodeItem*>(it)) {
				Q_EMIT eventSelected(ev->eventIndex());
				return;
			}
			if (auto* node = qgraphicsitem_cast<graphdetail::SexpNodeItem*>(it)) {
				Q_EMIT eventSelected(node->eventIndex()); // sync-select the containing event
				return;
			}
		}
	});

	setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
	setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
	setDragMode(QGraphicsView::ScrollHandDrag);
	setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	setBackgroundBrush(m_style.bgColor);
	setFrameShape(QFrame::NoFrame);
	setAlignment(Qt::AlignCenter);
	setFocusPolicy(Qt::StrongFocus); // receive Esc to clear the swimlanes filter

	buildOverlay();

	m_minimap = new graphdetail::MinimapWidget(this, m_scene, &m_style, viewport());
	m_minimap->raise();
	positionOverlay();

	qApp->installEventFilter(this);
}

EventGraphView::~EventGraphView() = default;

bool EventGraphView::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::ApplicationPaletteChange)
		applyTheme(qApp->palette().window().color().value() < 128);
	return QGraphicsView::eventFilter(watched, event);
}

void EventGraphView::buildOverlay()
{
	m_overlay = new QWidget(viewport());
	m_overlay->setAttribute(Qt::WA_StyledBackground, true);
	// Children of the pan-hand viewport inherit its cursor; force the normal
	// pointer over the control strip.
	m_overlay->setCursor(Qt::ArrowCursor);
	auto* lay = new QHBoxLayout(m_overlay);
	lay->setContentsMargins(6, 6, 6, 6);
	lay->setSpacing(4);

	m_modeGroup = new QButtonGroup(this);
	m_modeGroup->setExclusive(true);

	auto addModeButton = [&](const QString& label, Mode mode, bool enabled, const QString& tip) {
		auto* btn = new QToolButton(m_overlay);
		btn->setText(label);
		btn->setCheckable(true);
		btn->setChecked(mode == m_mode);
		btn->setEnabled(enabled);
		if (!tip.isEmpty())
			btn->setToolTip(tip);
		m_modeGroup->addButton(btn, static_cast<int>(mode));
		lay->addWidget(btn);
	};
	addModeButton(tr("Radial"), Mode::Radial, true, QString());
	addModeButton(tr("Swimlanes"), Mode::Swimlanes, true, QString());
	addModeButton(tr("Basic"), Mode::Basic, false, tr("Coming soon"));

	connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id) { setMode(static_cast<Mode>(id)); });

	auto* sep = new QLabel(QStringLiteral("  "), m_overlay);
	lay->addWidget(sep);

	m_objectCombo = new QComboBox(m_overlay);
	m_objectCombo->setMinimumWidth(180);
	m_objectCombo->setToolTip(tr("Object to inspect"));
	lay->addWidget(m_objectCombo);

	connect(m_objectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		if (m_mode == Mode::Radial) {
			rebuildRadial();
		} else if (m_mode == Mode::Swimlanes) {
			bool ok = false;
			const int k = m_objectCombo->currentData().toInt(&ok);
			m_swimKind = (ok && k >= 0) ? static_cast<RefObjectKind>(k) : RefObjectKind::Unknown;
			clearSwimFocus(); // a focused object may be filtered out by the kind change
			m_hasFramed = false;
			rebuildSwimlanes();
		}
	});

	// Settings gear — global + per-view graph settings.
	buildSettingsMenu();
	auto* gear = new QToolButton(m_overlay);
	gear->setToolTip(tr("Graph settings"));
	gear->setPopupMode(QToolButton::InstantPopup);
	gear->setMenu(m_settingsMenu);
	bindCustomIcon(gear, CustomIcon::Settings);
	lay->addWidget(gear);

	positionOverlay();
}

void EventGraphView::buildSettingsMenu()
{
	m_settingsMenu = new QMenu(this);
	m_settingsMenu->addSection(tr("Radial"));

	auto* panel = new QWidget(m_settingsMenu);
	auto* col = new QVBoxLayout(panel);
	col->setContentsMargins(10, 4, 10, 6);
	col->addWidget(new QLabel(tr("Ring spacing"), panel));

	auto* row = new QHBoxLayout;
	auto* slider = new QSlider(Qt::Horizontal, panel);
	slider->setRange(120, 420);
	slider->setValue(static_cast<int>(s_ringSpacing));
	slider->setMinimumWidth(160);
	auto* value = new QLabel(QString::number(static_cast<int>(s_ringSpacing)), panel);
	value->setMinimumWidth(28);
	row->addWidget(slider);
	row->addWidget(value);
	col->addLayout(row);

	connect(slider, &QSlider::valueChanged, this, [this, value](int v) {
		s_ringSpacing = v;
		value->setText(QString::number(v));
		if (m_mode == Mode::Radial)
			rebuildRadial();
	});

	auto* wa = new QWidgetAction(m_settingsMenu);
	wa->setDefaultWidget(panel);
	m_settingsMenu->addAction(wa);
}

// Keep the overlay hugging the top-left corner, sized to its (variable-width)
// contents, and on top of the scene. Called on build, resize, show, and after
// the selector is repopulated.
void EventGraphView::positionOverlay()
{
	if (m_overlay) {
		m_overlay->adjustSize();
		m_overlay->move(8, 8);
		m_overlay->raise();
		m_overlay->show();
	}
	if (m_minimap) {
		m_minimap->move(8, viewport()->height() - m_minimap->height() - 8);
		m_minimap->raise();
		m_minimap->show();
	}
}

int EventGraphView::selectedObjectRow() const
{
	if (!m_objectCombo || m_objectCombo->currentIndex() < 0)
		return -1;
	bool ok = false;
	const int idx = m_objectCombo->currentData().toInt(&ok); // stored m_objects index
	return (ok && idx >= 0 && idx < m_objects.size()) ? idx : -1;
}

// Rebuild the combo from m_objects, listing only objects that are actually
// referenced (with live counts) and keeping the current object selected where
// it still exists. Each item stores its m_objects index as user data, so combo
// rows need not align 1:1 with m_objects.
void EventGraphView::populateSelector()
{
	auto countOf = [this](const GraphObject& o) {
		return m_index ? m_index->eventReferenceCount(o.kind, SCP_string(o.name.toUtf8().constData())) : 0;
	};

	// Remember the current object's identity so a rebuild doesn't reset it.
	GraphObject prev;
	bool hadPrev = false;
	const int prevIdx = selectedObjectRow();
	if (prevIdx >= 0) {
		prev = m_objects[prevIdx];
		hadPrev = true;
	}

	QSignalBlocker blocker(m_objectCombo);
	m_objectCombo->clear();

	int restoreRow = -1;
	for (int i = 0; i < m_objects.size(); ++i) {
		const GraphObject& obj = m_objects[i];
		const int count = countOf(obj);
		if (count <= 0)
			continue; // an unreferenced object would just clutter the list

		const int comboRow = m_objectCombo->count();
		m_objectCombo->addItem(QStringLiteral("%1  (%2, %3 ref%4)")
			.arg(obj.name)
			.arg(QString::fromLatin1(EventReferenceIndex::kindLabel(obj.kind)))
			.arg(count)
			.arg(count == 1 ? QString() : QStringLiteral("s")), QVariant(i));

		if (hadPrev && obj.kind == prev.kind && obj.name == prev.name)
			restoreRow = comboRow;
	}

	const int target = (restoreRow >= 0) ? restoreRow : (m_objectCombo->count() > 0 ? 0 : -1);
	if (target >= 0)
		m_objectCombo->setCurrentIndex(target);
}

void EventGraphView::reload()
{
	if (m_mode == Mode::Swimlanes)
		populateKindFilter();
	else
		populateSelector();
	positionOverlay(); // the combo width changes with the labels
	rebuildCurrent();
}

void EventGraphView::setMode(Mode mode)
{
	if (mode == m_mode)
		return;
	m_mode = mode;
	// The combo drives the object (radial) or the kind filter (swimlanes).
	if (mode == Mode::Swimlanes)
		populateKindFilter();
	else
		populateSelector();
	positionOverlay();
	m_hasFramed = false; // re-fit for the new mode
	rebuildCurrent();
}

// Swimlanes: fill the combo with "All object types" + each kind that is
// actually referenced. Selecting a kind filters the rows to it.
void EventGraphView::populateKindFilter()
{
	if (!m_objectCombo)
		return;
	QSignalBlocker blocker(m_objectCombo);
	m_objectCombo->clear();
	m_objectCombo->addItem(tr("All object types"), -1);

	// Distinct kinds present, in enum order.
	QVector<int> present;
	if (m_index) {
		for (const EventObjectRef& r : m_index->allReferences()) {
			const int k = static_cast<int>(r.kind);
			if (!present.contains(k))
				present.push_back(k);
		}
	}
	std::sort(present.begin(), present.end());
	for (int k : present)
		m_objectCombo->addItem(QString::fromLatin1(EventReferenceIndex::kindLabel(static_cast<RefObjectKind>(k))), k);

	// Restore the current kind filter if still present.
	int row = 0;
	if (m_swimKind != RefObjectKind::Unknown) {
		const int idx = m_objectCombo->findData(static_cast<int>(m_swimKind));
		if (idx >= 0)
			row = idx;
		else
			m_swimKind = RefObjectKind::Unknown;
	}
	m_objectCombo->setCurrentIndex(row);
}

void EventGraphView::toggleSwimObjectFocus(const QString& objectKey, bool add)
{
	if (add) {
		if (m_focusObjects.contains(objectKey))
			m_focusObjects.remove(objectKey);
		else
			m_focusObjects.insert(objectKey);
	} else {
		const bool wasSole = m_focusEvents.isEmpty() && m_focusObjects.size() == 1
			&& m_focusObjects.contains(objectKey);
		m_focusObjects.clear();
		m_focusEvents.clear();
		if (!wasSole)
			m_focusObjects.insert(objectKey);
	}
}

void EventGraphView::toggleSwimEventFocus(int eventIndex, bool add)
{
	if (add) {
		if (m_focusEvents.contains(eventIndex))
			m_focusEvents.remove(eventIndex);
		else
			m_focusEvents.insert(eventIndex);
	} else {
		const bool wasSole = m_focusObjects.isEmpty() && m_focusEvents.size() == 1
			&& m_focusEvents.contains(eventIndex);
		m_focusObjects.clear();
		m_focusEvents.clear();
		if (!wasSole)
			m_focusEvents.insert(eventIndex);
	}
}

void EventGraphView::clearSwimFocus()
{
	m_focusObjects.clear();
	m_focusEvents.clear();
}

void EventGraphView::rebuildCurrent()
{
	if (m_mode == Mode::Swimlanes)
		rebuildSwimlanes();
	else
		rebuildRadial();
}

void EventGraphView::showEmptyMessage(const QString& text)
{
	auto* label = m_scene->addSimpleText(text);
	label->setBrush(m_style.nodeSubText);
	label->setPos(-label->boundingRect().width() / 2, 60.0);
	zoomToFitAll();
	if (m_minimap)
		m_minimap->regenerate(); // content changed → re-render the cached overview
}

void EventGraphView::rebuildRadial()
{
	// Capture the current viewport and selection so a rebuild (after an edit,
	// or on return from another view) can resume where we left off instead of
	// re-fitting the whole graph.
	const QTransform savedTransform = transform();
	const QPointF savedCenter = mapToScene(viewport()->rect().center());
	int selKind = 0; // 0 none, 1 event, 2 node
	int selEvent = -1;
	QString selOp;
	bool selCond = false;
	for (QGraphicsItem* gi : m_scene->selectedItems()) {
		if (auto* nd = qgraphicsitem_cast<graphdetail::SexpNodeItem*>(gi)) {
			selKind = 2; selEvent = nd->eventIndex(); selOp = nd->titleText(); selCond = nd->isCond();
			break;
		}
		if (auto* ev = qgraphicsitem_cast<graphdetail::EventNodeItem*>(gi)) {
			selKind = 1; selEvent = ev->eventIndex();
			break;
		}
	}

	m_suppressSelectionSignal = true; // programmatic selection below shouldn't echo out
	m_scene->clear();

	const int row = selectedObjectRow();
	if (row < 0 || !m_index) {
		m_suppressSelectionSignal = false;
		showEmptyMessage(tr("No object selected."));
		return;
	}

	const GraphObject& obj = m_objects[row];
	const SCP_string objName(obj.name.toUtf8().constData());
	const SCP_vector<EventObjectRef> sites = m_index->referenceSites(obj.kind, objName);

	// Group the referencing sites by event, split into condition / action.
	QVector<int> eventOrder; // distinct events in first-seen order
	struct Bucket { QVector<const EventObjectRef*> conds, actions; };
	QHash<int, Bucket> buckets;
	for (const EventObjectRef& s : sites) {
		if (!buckets.contains(s.eventIndex))
			eventOrder.push_back(s.eventIndex);
		Bucket& b = buckets[s.eventIndex];
		if (s.role == RefRole::Condition)
			b.conds.push_back(&s);
		else
			b.actions.push_back(&s); // actions + anything outside a when-structure
	}

	// Center object card.
	auto* center = new graphdetail::ObjectNodeItem(
		QString::fromLatin1(EventReferenceIndex::kindLabel(obj.kind)), m_style.colorFor(obj.kind), obj.name,
		tr("%1 references in %2 events").arg(static_cast<int>(sites.size())).arg(eventOrder.size()), m_style);
	center->setPos(0, 0);
	center->setZValue(2.0);
	m_scene->addItem(center);

	if (eventOrder.isEmpty()) {
		m_suppressSelectionSignal = false;
		showEmptyMessage(tr("No events reference %1.").arg(obj.name));
		return;
	}

	const int n = eventOrder.size();
	// Give each event enough arc length on the inner ring for its card.
	const qreal rEvent = std::max(250.0, n * (graphdetail::kNodeW + 70.0) / (2.0 * M_PI));
	const qreal rCond = rEvent + s_ringSpacing;
	const qreal rAction = rCond + s_ringSpacing;
	const qreal wedge = 2.0 * M_PI / n;

	auto polar = [](qreal radius, qreal angle) {
		return QPointF(radius * std::cos(angle), radius * std::sin(angle));
	};

	// Place a group of node cards on a ring, spread within their event's wedge.
	auto placeRing = [&](const QVector<const EventObjectRef*>& group, qreal centerAngle, qreal radius,
						 const QPointF& from, const QColor& edgeColor, bool isCond) {
		const int m = group.size();
		for (int j = 0; j < m; ++j) {
			const EventObjectRef* s = group[j];
			const qreal spread = wedge * 0.8;
			const qreal off = (m == 1) ? 0.0 : (j - (m - 1) / 2.0) * (spread / m);
			const QPointF pos = polar(radius, centerAngle + off);

			const QString evName = (s->eventIndex >= 0 && s->eventIndex < m_eventNames.size())
				? m_eventNames[s->eventIndex] : tr("<event %1>").arg(s->eventIndex);
			QVector<QString> argList;
			argList.reserve(static_cast<int>(s->args.size()));
			for (const auto& a : s->args)
				argList.push_back(QString::fromStdString(a));
			auto* card = new graphdetail::SexpNodeItem(s->operatorNode, s->eventIndex, isCond,
				QString::fromStdString(s->operatorText), argList, evName,
				QString::fromStdString(s->expression), m_style);
			card->setPos(pos);
			card->setZValue(1.0);
			if (selKind == 2 && s->eventIndex == selEvent && isCond == selCond
				&& QString::fromStdString(s->operatorText) == selOp)
				card->setSelected(true);
			m_scene->addItem(card);
			m_scene->addItem(new graphdetail::RefEdgeItem(from, pos, edgeColor, m_style));
		}
	};

	for (int i = 0; i < n; ++i) {
		const int ev = eventOrder[i];
		const qreal angle = -M_PI / 2.0 + i * wedge;
		const QPointF evPos = polar(rEvent, angle);

		const QString name = (ev >= 0 && ev < m_eventNames.size())
			? m_eventNames[ev] : tr("<event %1>").arg(ev);
		auto* evCard = new graphdetail::EventNodeItem(ev, name, m_style);
		evCard->setPos(evPos);
		evCard->setZValue(1.0);
		if (selKind == 1 && ev == selEvent)
			evCard->setSelected(true);
		m_scene->addItem(evCard);
		m_scene->addItem(new graphdetail::RefEdgeItem(center->pos(), evPos, m_style.entity, m_style));

		const Bucket& b = buckets[ev];
		placeRing(b.conds, angle, rCond, evPos, m_style.condChip, /*isCond=*/true);
		placeRing(b.actions, angle, rAction, evPos, m_style.actionChip, /*isCond=*/false);
	}

	// Dashed tier guide rings.
	for (qreal r : {rEvent, rCond, rAction}) {
		auto* ring = m_scene->addEllipse(QRectF(-r, -r, 2 * r, 2 * r),
			QPen(m_style.ringColor, 1.0, Qt::DashLine));
		ring->setZValue(-2.0);
	}

	// If we're still on the same object, resume the prior zoom/center; otherwise
	// frame the whole graph.
	const QString curKey = QStringLiteral("%1|%2").arg(static_cast<int>(obj.kind)).arg(obj.name);
	if (m_hasFramed && curKey == m_framedKey) {
		setTransform(savedTransform);
		centerOn(savedCenter);
		m_currentScale = transform().m11();
	} else {
		zoomToFitAll();
		m_framedKey = curKey;
		m_hasFramed = true;
	}

	m_suppressSelectionSignal = false;
	if (m_minimap)
		m_minimap->regenerate(); // content changed → re-render the cached overview
}

void EventGraphView::rebuildSwimlanes()
{
	// Viewport + selection continuity (same pattern as radial).
	const QTransform savedTransform = transform();
	const QPointF savedCenter = mapToScene(viewport()->rect().center());
	int selEvent = -1;
	for (QGraphicsItem* gi : m_scene->selectedItems()) {
		if (auto* ev = qgraphicsitem_cast<graphdetail::EventNodeItem*>(gi)) { selEvent = ev->eventIndex(); break; }
		if (auto* nd = qgraphicsitem_cast<graphdetail::SexpNodeItem*>(gi)) { selEvent = nd->eventIndex(); break; }
	}

	m_suppressSelectionSignal = true;
	m_scene->clear();

	if (!m_index) {
		m_suppressSelectionSignal = false;
		showEmptyMessage(tr("No data."));
		return;
	}
	const SCP_vector<EventObjectRef>& refs = m_index->allReferences();
	if (refs.empty()) {
		m_suppressSelectionSignal = false;
		showEmptyMessage(tr("No object references in events."));
		return;
	}

	const bool allKinds = (m_swimKind == RefObjectKind::Unknown);

	// Distinct rows (objects) and columns (events), over the kind-allowed refs.
	QVector<std::pair<RefObjectKind, QString>> rows;
	QHash<QString, int> rowSeen;
	QVector<int> cols;
	QHash<int, int> colSeen;
	for (const EventObjectRef& r : refs) {
		if (!allKinds && r.kind != m_swimKind)
			continue;
		const QString name = QString::fromStdString(r.name);
		const QString rk = swimRowKey(r.kind, name);
		if (!rowSeen.contains(rk)) { rowSeen.insert(rk, 0); rows.push_back({r.kind, name}); }
		if (!colSeen.contains(r.eventIndex)) { colSeen.insert(r.eventIndex, 0); cols.push_back(r.eventIndex); }
	}
	std::sort(rows.begin(), rows.end(), [](const std::pair<RefObjectKind, QString>& a,
											 const std::pair<RefObjectKind, QString>& b) {
		if (a.first != b.first)
			return static_cast<int>(a.first) < static_cast<int>(b.first);
		return a.second.toLower() < b.second.toLower();
	});
	std::sort(cols.begin(), cols.end());

	QVector<QString> rowKeyOf(rows.size());
	QHash<QString, int> rowIndex;
	for (int i = 0; i < rows.size(); ++i) {
		rowKeyOf[i] = swimRowKey(rows[i].first, rows[i].second);
		rowIndex.insert(rowKeyOf[i], i);
	}
	QHash<int, int> colIndex;
	for (int j = 0; j < cols.size(); ++j)
		colIndex.insert(cols[j], j);

	// Gather the referencing operator nodes per (object row, event column),
	// deduped by node. A node that names several objects appears in each object's
	// row (option #1); the column connector ties the copies together.
	struct Site {
		int treeNode = -1;
		int eventIndex = -1;
		bool isCond = false;
		QString opName;
		QVector<QString> args;
		QString expr;
	};
	QHash<qint64, QVector<Site>> cellSites;
	for (const EventObjectRef& r : refs) {
		if (!allKinds && r.kind != m_swimKind)
			continue;
		const int ri = rowIndex.value(swimRowKey(r.kind, QString::fromStdString(r.name)));
		const int ci = colIndex.value(r.eventIndex);
		QVector<Site>& sites = cellSites[static_cast<qint64>(ri) * 1000000 + ci];
		bool dup = false;
		for (const Site& s : sites)
			if (s.treeNode == r.operatorNode) { dup = true; break; }
		if (dup)
			continue;
		Site s;
		s.treeNode = r.operatorNode;
		s.eventIndex = r.eventIndex;
		s.isCond = (r.role == RefRole::Condition);
		s.opName = QString::fromStdString(r.operatorText);
		s.expr = QString::fromStdString(r.expression);
		for (const auto& a : r.args)
			s.args.push_back(QString::fromStdString(a));
		sites.push_back(std::move(s));
	}

	// Cross-filter: a focused object restricts the visible columns to its events;
	// a focused event restricts the visible rows to its objects. The two compose,
	// and rows/columns left empty are dropped.
	const bool objFocus = !m_focusObjects.isEmpty();
	const bool evFocus = !m_focusEvents.isEmpty();
	QSet<int> eventsOfFocusObj;
	QSet<QString> objsOfFocusEv;
	if (objFocus || evFocus) {
		for (auto it = cellSites.constBegin(); it != cellSites.constEnd(); ++it) {
			const int ri = static_cast<int>(it.key() / 1000000);
			const int ci = static_cast<int>(it.key() % 1000000);
			if (objFocus && m_focusObjects.contains(rowKeyOf[ri]))
				eventsOfFocusObj.insert(cols[ci]);
			if (evFocus && m_focusEvents.contains(cols[ci]))
				objsOfFocusEv.insert(rowKeyOf[ri]);
		}
	}
	auto colPasses = [&](int ci) { return !objFocus || eventsOfFocusObj.contains(cols[ci]); };
	auto rowPasses = [&](int ri) { return !evFocus || objsOfFocusEv.contains(rowKeyOf[ri]); };

	QSet<int> rowsWithCell, colsWithCell;
	for (auto it = cellSites.constBegin(); it != cellSites.constEnd(); ++it) {
		const int ri = static_cast<int>(it.key() / 1000000);
		const int ci = static_cast<int>(it.key() % 1000000);
		if (rowPasses(ri) && colPasses(ci)) { rowsWithCell.insert(ri); colsWithCell.insert(ci); }
	}
	QVector<int> keepRows, keepCols;
	for (int i = 0; i < rows.size(); ++i)
		if (rowPasses(i) && rowsWithCell.contains(i)) keepRows.push_back(i);
	for (int j = 0; j < cols.size(); ++j)
		if (colPasses(j) && colsWithCell.contains(j)) keepCols.push_back(j);

	if (keepRows.isEmpty() || keepCols.isEmpty()) {
		m_suppressSelectionSignal = false;
		showEmptyMessage(tr("No references match the current filter (Esc to clear)."));
		return;
	}
	QHash<int, int> rowPos, colPos;
	for (int p = 0; p < keepRows.size(); ++p) rowPos.insert(keepRows[p], p);
	for (int p = 0; p < keepCols.size(); ++p) colPos.insert(keepCols[p], p);

	// Create the node cards per kept cell and measure their stacked height so each
	// row can grow to fit. No event badge in swimlanes -- the column identifies it.
	const qreal leftGutter = 244.0, topGutter = 66.0, colW = 252.0, cardGap = 10.0, rowPad = 16.0, minRowH = 60.0;
	QHash<qint64, QVector<graphdetail::SexpNodeItem*>> cellCards;
	QHash<qint64, qreal> cellHeight;
	QVector<qreal> rowHeight(keepRows.size(), minRowH);
	for (auto it = cellSites.constBegin(); it != cellSites.constEnd(); ++it) {
		const int ri = static_cast<int>(it.key() / 1000000);
		const int ci = static_cast<int>(it.key() % 1000000);
		if (!rowPos.contains(ri) || !colPos.contains(ci))
			continue;
		QVector<graphdetail::SexpNodeItem*> cards;
		qreal h = 0.0;
		for (const Site& s : it.value()) {
			auto* card = new graphdetail::SexpNodeItem(s.treeNode, s.eventIndex, s.isCond, s.opName, s.args,
				QString(), s.expr, m_style);
			cards.push_back(card);
			h += card->boundingRect().height();
		}
		if (cards.size() > 1)
			h += cardGap * (cards.size() - 1);
		cellCards.insert(it.key(), cards);
		cellHeight.insert(it.key(), h);
		rowHeight[rowPos[ri]] = std::max(rowHeight[rowPos[ri]], h + rowPad);
	}

	// Cumulative row tops (variable heights).
	QVector<qreal> rowTop(keepRows.size());
	qreal yCursor = topGutter;
	for (int p = 0; p < keepRows.size(); ++p) { rowTop[p] = yCursor; yCursor += rowHeight[p]; }
	const qreal contentW = leftGutter + keepCols.size() * colW;
	auto colXc = [&](int pos) { return leftGutter + pos * colW + colW / 2.0; };
	auto rowCenter = [&](int pos) { return rowTop[pos] + rowHeight[pos] / 2.0; };

	// Alternating lane stripes.
	for (int p = 0; p < keepRows.size(); ++p) {
		QColor tint = m_style.cardBorder;
		tint.setAlpha(p % 2 ? 40 : 0);
		auto* stripe = m_scene->addRect(QRectF(0, rowTop[p], contentW, rowHeight[p]), QPen(Qt::NoPen), QBrush(tint));
		stripe->setZValue(-3.0);
	}

	// Position node cards; stack them centered in each row band; track the lowest
	// card per column for the connector line.
	QHash<int, qreal> colBottom;
	for (auto it = cellCards.constBegin(); it != cellCards.constEnd(); ++it) {
		const int ri = static_cast<int>(it.key() / 1000000);
		const int ci = static_cast<int>(it.key() % 1000000);
		const int rp = rowPos[ri], cp = colPos[ci];
		const qreal stackH = cellHeight.value(it.key());
		qreal top = rowCenter(rp) - stackH / 2.0;
		for (graphdetail::SexpNodeItem* card : it.value()) {
			const qreal ch = card->boundingRect().height();
			card->setPos(colXc(cp), top + ch / 2.0);
			card->setZValue(1.0);
			if (card->eventIndex() == selEvent)
				card->setSelected(true);
			m_scene->addItem(card);
			top += ch + cardGap;
		}
		const qreal bottom = rowCenter(rp) + stackH / 2.0;
		if (!colBottom.contains(cp) || bottom > colBottom[cp]) colBottom[cp] = bottom;
	}

	// Vertical connector per column: header down to the lowest card.
	for (auto it = colBottom.constBegin(); it != colBottom.constEnd(); ++it) {
		auto* line = m_scene->addLine(colXc(it.key()), topGutter / 2.0, colXc(it.key()), it.value(),
			QPen(m_style.entity, 1.6, Qt::DashLine));
		line->setZValue(-1.0);
	}

	// Column headers (events).
	for (int p = 0; p < keepCols.size(); ++p) {
		const int ev = cols[keepCols[p]];
		const QString name = (ev >= 0 && ev < m_eventNames.size()) ? m_eventNames[ev] : tr("<event %1>").arg(ev);
		auto* hdr = new graphdetail::EventNodeItem(ev, name, m_style);
		hdr->setPos(colXc(p), topGutter / 2.0);
		hdr->setZValue(1.0);
		if (m_focusEvents.contains(ev))
			hdr->setFocusHighlight(true); // filter item (red)
		else if (ev == selEvent)
			hdr->setSelected(true); // tree-synced selection (orange)
		m_scene->addItem(hdr);
	}

	// Row headers (objects); carry their key for the cross-filter click handler.
	for (int p = 0; p < keepRows.size(); ++p) {
		const int ri = keepRows[p];
		auto* hdr = new graphdetail::ObjectNodeItem(
			QString::fromLatin1(EventReferenceIndex::kindLabel(rows[ri].first)), m_style.colorFor(rows[ri].first),
			rows[ri].second, QString(), m_style);
		hdr->setPos(leftGutter / 2.0, rowCenter(p));
		hdr->setZValue(1.0);
		hdr->setData(0, rowKeyOf[ri]);
		if (m_focusObjects.contains(rowKeyOf[ri]))
			hdr->setFocusHighlight(true); // filter item (red)
		m_scene->addItem(hdr);
	}

	const QString curKey = QStringLiteral("swim");
	if (m_hasFramed && curKey == m_framedKey) {
		setTransform(savedTransform);
		centerOn(savedCenter);
		m_currentScale = transform().m11();
	} else {
		zoomToFitAll();
		m_framedKey = curKey;
		m_hasFramed = true;
	}

	m_suppressSelectionSignal = false;
	if (m_minimap)
		m_minimap->regenerate();
}

void EventGraphView::zoomStep(bool zoomIn)
{
	const qreal step = zoomIn ? 1.10 : (1.0 / 1.10);
	const qreal next = qBound(kMinScale, m_currentScale * step, kMaxScale);
	const qreal factor = next / m_currentScale;
	if (qFuzzyCompare(factor, 1.0))
		return;
	const QPointF center = mapToScene(viewport()->rect().center());
	scale(factor, factor);
	m_currentScale = next;
	centerOn(center); // keep the view centered (the wheel came from the minimap)
}

void EventGraphView::zoomToFitAll(qreal margin)
{
	if (!m_scene || m_scene->items().isEmpty())
		return;
	const QRectF rect = m_scene->itemsBoundingRect().adjusted(-margin, -margin, margin, margin);
	if (!rect.isEmpty()) {
		fitInView(rect, Qt::KeepAspectRatio);
		m_currentScale = transform().m11();
	}
}

void EventGraphView::wheelEvent(QWheelEvent* e)
{
	const QPoint deg = e->angleDelta();
	if (deg.isNull()) {
		e->ignore();
		return;
	}
	const qreal step = (deg.y() > 0) ? 1.10 : (1.0 / 1.10);
	const qreal next = qBound(kMinScale, m_currentScale * step, kMaxScale);
	const qreal factor = next / m_currentScale;
	if (!qFuzzyCompare(factor, 1.0)) {
		scale(factor, factor);
		m_currentScale = next;
		if (m_minimap)
			m_minimap->update();
	}
	e->accept();
}

void EventGraphView::drawBackground(QPainter* p, const QRectF& rect)
{
	p->save();
	p->setRenderHint(QPainter::Antialiasing, false);
	p->fillRect(rect, m_style.bgColor);

	const qreal step = 28.0;
	QPen gridPen(m_style.gridMinor, 1.0);
	p->setPen(gridPen);
	const qreal left = std::floor(rect.left() / step) * step;
	const qreal top = std::floor(rect.top() / step) * step;
	for (qreal x = left; x < rect.right(); x += step)
		p->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
	for (qreal y = top; y < rect.bottom(); y += step)
		p->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
	p->restore();
}

void EventGraphView::mousePressEvent(QMouseEvent* e)
{
	const QPointF sp = mapToScene(e->pos());

	// Swimlanes cross-filter: clicking a row header (object) or column header
	// (event) drives the filter; Ctrl adds to the selection. Clicking empty
	// canvas (a click, not a pan) clears the filter — handled on release.
	m_pressOnEmpty = false;
	if (m_mode == Mode::Swimlanes && e->button() == Qt::LeftButton) {
		const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
		bool onNode = false;
		for (QGraphicsItem* it : m_scene->items(sp)) {
			if (auto* obj = qgraphicsitem_cast<graphdetail::ObjectNodeItem*>(it)) {
				toggleSwimObjectFocus(obj->data(0).toString(), ctrl);
				m_hasFramed = false;
				rebuildSwimlanes();
				e->accept();
				return;
			}
			if (auto* ev = qgraphicsitem_cast<graphdetail::EventNodeItem*>(it)) {
				toggleSwimEventFocus(ev->eventIndex(), ctrl);
				m_hasFramed = false;
				rebuildSwimlanes();
				e->accept();
				return;
			}
			if (qgraphicsitem_cast<graphdetail::SexpNodeItem*>(it)) {
				onNode = true; // node cards fall through to normal selection / jump
				break;
			}
		}
		if (!onNode) {
			m_pressOnEmpty = true;
			m_pressPos = e->pos();
		}
	}

	for (QGraphicsItem* it : m_scene->items(sp)) {
		if (auto* card = dynamic_cast<graphdetail::CardItem*>(it)) {
			card->bringToFront(); // clicked card jumps above overlaps
			if (card->handleToggleClick(sp)) {
				e->accept();
				return;
			}
			break;
		}
	}
	QGraphicsView::mousePressEvent(e);
}

void EventGraphView::mouseReleaseEvent(QMouseEvent* e)
{
	// A click (not a pan-drag) on empty canvas clears the swimlanes filter.
	const bool emptyClick = m_pressOnEmpty && e->button() == Qt::LeftButton
		&& (e->pos() - m_pressPos).manhattanLength() < 6
		&& (!m_focusObjects.isEmpty() || !m_focusEvents.isEmpty());
	m_pressOnEmpty = false;

	QGraphicsView::mouseReleaseEvent(e); // let ScrollHandDrag settle its state first

	if (emptyClick) {
		clearSwimFocus();
		m_hasFramed = false;
		rebuildSwimlanes();
	}
}

void EventGraphView::keyPressEvent(QKeyEvent* e)
{
	if (m_mode == Mode::Swimlanes && e->key() == Qt::Key_Escape
		&& (!m_focusObjects.isEmpty() || !m_focusEvents.isEmpty())) {
		clearSwimFocus();
		m_hasFramed = false;
		rebuildSwimlanes();
		e->accept();
		return;
	}
	QGraphicsView::keyPressEvent(e);
}

void EventGraphView::mouseDoubleClickEvent(QMouseEvent* e)
{
	const QPointF sp = mapToScene(e->pos());
	for (QGraphicsItem* it : m_scene->items(sp)) {
		if (auto* ev = qgraphicsitem_cast<graphdetail::EventNodeItem*>(it)) {
			Q_EMIT eventActivated(ev->eventIndex());
			e->accept();
			return;
		}
		if (auto* node = qgraphicsitem_cast<graphdetail::SexpNodeItem*>(it)) {
			Q_EMIT nodeActivated(node->treeNode());
			e->accept();
			return;
		}
	}
	QGraphicsView::mouseDoubleClickEvent(e);
}

void EventGraphView::resizeEvent(QResizeEvent* e)
{
	QGraphicsView::resizeEvent(e);
	positionOverlay();
}

void EventGraphView::showEvent(QShowEvent* e)
{
	QGraphicsView::showEvent(e);
	positionOverlay();
}

void EventGraphView::scrollContentsBy(int dx, int dy)
{
	QGraphicsView::scrollContentsBy(dx, dy);
	positionOverlay(); // keep overlays pinned to the corner, not the content
	if (m_minimap)
		m_minimap->update(); // viewport rectangle moved
}

void EventGraphView::applyTheme(bool dark)
{
	m_style = EventGraphStyle::makeStyle(dark);
	setBackgroundBrush(m_style.bgColor);
	reload();
}

} // namespace fso::fred
