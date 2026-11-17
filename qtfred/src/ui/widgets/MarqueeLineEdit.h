#pragma once
#include <QElapsedTimer>
#include <QLineEdit>
#include <QTimer>

namespace fso::fred {

// A read-only line edit for text that may not fit, such as the sexp mini-help. Text that overflows
// scrolls left in a loop, pausing at the start of each pass; hovering pauses it so it can be read.
// With focus (clicked, to select and copy) it is an ordinary line edit.
class MarqueeLineEdit : public QLineEdit {
	Q_OBJECT
  public:
	explicit MarqueeLineEdit(QWidget* parent = nullptr);

  protected:
	void paintEvent(QPaintEvent* e) override;
	void resizeEvent(QResizeEvent* e) override;
	void showEvent(QShowEvent* e) override;
	void hideEvent(QHideEvent* e) override;
	void enterEvent(QEnterEvent* e) override;
	void leaveEvent(QEvent* e) override;
	void focusInEvent(QFocusEvent* e) override;
	void focusOutEvent(QFocusEvent* e) override;
	void changeEvent(QEvent* e) override;

  private:
	QRect textRect() const;
	bool marquee() const; // overflowing and not focused, so this class paints the text
	void restart();       // back to the start of a pass, and run or stop the timer to suit
	void updateTimer();
	void tick();

	QTimer _timer;
	QElapsedTimer _sinceTick;
	qreal _offset = 0;  // how far the text has scrolled left, in pixels
	int _pauseLeft = 0; // milliseconds still to wait before this pass scrolls
	bool _hovered = false;
};

} // namespace fso::fred
