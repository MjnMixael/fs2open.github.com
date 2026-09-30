#include "KeyboardNavigation.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTextEdit>

namespace fso::fred::util {

namespace {

constexpr auto KEYBOARD_FOCUS_PROPERTY = "keyboardFocus";
constexpr auto ALLOW_TAB_INPUT_PROPERTY = "fred_allow_tab_input";

class KeyboardNavigation : public QObject {
  public:
	using QObject::QObject;

  protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		switch (event->type()) {
		case QEvent::FocusIn:
		case QEvent::FocusOut:
			if (auto* button = qobject_cast<QAbstractButton*>(watched))
				markKeyboardFocus(button, event);
			break;
		case QEvent::Polish:
			// Sent once, before a widget is first shown
			if (!watched->property(ALLOW_TAB_INPUT_PROPERTY).toBool()) {
				if (auto* plain = qobject_cast<QPlainTextEdit*>(watched))
					plain->setTabChangesFocus(true);
				else if (auto* rich = qobject_cast<QTextEdit*>(watched))
					rich->setTabChangesFocus(true);
			}
			break;
		default:
			break;
		}
		return false; // observe only
	}

  private:
	static void markKeyboardFocus(QAbstractButton* button, QEvent* event)
	{
		bool keyboard = false;
		if (event->type() == QEvent::FocusIn) {
			const auto reason = static_cast<QFocusEvent*>(event)->reason();
			keyboard = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
				reason == Qt::ShortcutFocusReason;
		}

		if (button->property(KEYBOARD_FOCUS_PROPERTY).toBool() != keyboard) {
			button->setProperty(KEYBOARD_FOCUS_PROPERTY, keyboard);
			// Property selectors are only re-evaluated on a re-polish
			button->style()->unpolish(button);
			button->style()->polish(button);
			button->update();
		}
	}
};

} // namespace

void installKeyboardNavigation(QObject* owner)
{
	qApp->installEventFilter(new KeyboardNavigation(owner));
}

void allowTabInput(QWidget* textBox)
{
	textBox->setProperty(ALLOW_TAB_INPUT_PROPERTY, true);
}

void setTabChain(const QList<QWidget*>& widgets)
{
	QWidget* prev = nullptr;
	for (auto* w : widgets) {
		if (w == nullptr)
			continue;
		if (prev != nullptr)
			QWidget::setTabOrder(prev, w);
		prev = w;
	}
}

} // namespace fso::fred::util
