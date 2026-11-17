#include "MarqueeLineEdit.h"

#include <QEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFrame>

namespace fso::fred {

namespace {
constexpr int TickMs = 16;        // about 60 steps a second, for a smooth scroll
constexpr int StartPauseMs = 1500; // time to read the start before each pass
constexpr qreal PixelsPerSecond = 40.0;
constexpr int LoopGap = 48;        // space between the end of the text and its next copy
constexpr int TextMargin = 2;      // QLineEdit's own horizontal text margin
} // namespace

MarqueeLineEdit::MarqueeLineEdit(QWidget* parent) : QLineEdit(parent)
{
	_timer.setInterval(TickMs);
	connect(&_timer, &QTimer::timeout, this, &MarqueeLineEdit::tick);
	connect(this, &QLineEdit::textChanged, this, &MarqueeLineEdit::restart);
}

QRect MarqueeLineEdit::textRect() const
{
	QStyleOptionFrame opt;
	initStyleOption(&opt);
	QRect r = style()->subElementRect(QStyle::SE_LineEditContents, &opt, this);
	const QMargins m = textMargins();
	return r.adjusted(m.left() + TextMargin, m.top(), -(m.right() + TextMargin), -m.bottom());
}

bool MarqueeLineEdit::marquee() const
{
	return !hasFocus() && fontMetrics().horizontalAdvance(text()) > textRect().width();
}

void MarqueeLineEdit::restart()
{
	_offset = 0;
	_pauseLeft = StartPauseMs;
	// the whole text on hover, for when the scroll is too slow
	setToolTip(marquee() ? text() : QString());
	updateTimer();
	update();
}

void MarqueeLineEdit::updateTimer()
{
	if (marquee() && isVisible() && !_hovered) {
		if (!_timer.isActive()) {
			_sinceTick.start();
			_timer.start();
		}
	} else {
		_timer.stop();
	}
}

void MarqueeLineEdit::tick()
{
	const int elapsed = static_cast<int>(_sinceTick.restart());

	if (_pauseLeft > 0) {
		_pauseLeft -= elapsed;
		return;
	}

	_offset += PixelsPerSecond * elapsed / 1000.0;
	const int cycle = fontMetrics().horizontalAdvance(text()) + LoopGap;
	if (_offset >= cycle) {
		// the next copy has slid into the start position: a new pass, from its pause
		_offset = 0;
		_pauseLeft = StartPauseMs;
	}
	update();
}

void MarqueeLineEdit::paintEvent(QPaintEvent* e)
{
	if (!marquee()) {
		QLineEdit::paintEvent(e);
		return;
	}

	QPainter p(this);
	QStyleOptionFrame opt;
	initStyleOption(&opt);
	style()->drawPrimitive(QStyle::PE_PanelLineEdit, &opt, &p, this);

	const QRect r = textRect();
	p.setClipRect(r);
	p.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Text));

	// the text and the copy that follows it, so the loop reads as one continuous ribbon
	const int width = fontMetrics().horizontalAdvance(text());
	const qreal x = r.left() - _offset;
	const QRectF first(x, r.top(), width, r.height());
	p.drawText(first, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text());
	p.drawText(first.translated(width + LoopGap, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text());
}

void MarqueeLineEdit::resizeEvent(QResizeEvent* e)
{
	QLineEdit::resizeEvent(e);
	restart(); // whether it overflows depends on the width
}

void MarqueeLineEdit::showEvent(QShowEvent* e)
{
	QLineEdit::showEvent(e);
	restart();
}

void MarqueeLineEdit::hideEvent(QHideEvent* e)
{
	QLineEdit::hideEvent(e);
	_timer.stop();
}

void MarqueeLineEdit::enterEvent(QEnterEvent* e)
{
	QLineEdit::enterEvent(e);
	_hovered = true;
	updateTimer();
}

void MarqueeLineEdit::leaveEvent(QEvent* e)
{
	QLineEdit::leaveEvent(e);
	_hovered = false;
	updateTimer();
}

void MarqueeLineEdit::focusInEvent(QFocusEvent* e)
{
	QLineEdit::focusInEvent(e);
	restart(); // an ordinary line edit while focused
}

void MarqueeLineEdit::focusOutEvent(QFocusEvent* e)
{
	QLineEdit::focusOutEvent(e);
	restart();
}

void MarqueeLineEdit::changeEvent(QEvent* e)
{
	QLineEdit::changeEvent(e);
	if (e->type() == QEvent::FontChange || e->type() == QEvent::StyleChange)
		restart();
}

} // namespace fso::fred
