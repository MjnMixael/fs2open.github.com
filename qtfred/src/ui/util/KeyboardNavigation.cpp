#include "KeyboardNavigation.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QStyle>
#include <QTextEdit>
#include <QTreeView>

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
		case QEvent::KeyPress:
			return cycleRepeatedLetter(watched, static_cast<QKeyEvent*>(event));
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
		return false; // everything but a cycled letter is passed on
	}

  private:
	// Type-ahead in lists and dropdowns: pressing F jumps to the next item starting with F. Qt joins
	// keys pressed within keyboardInputInterval() into one search, and is meant to treat a repeated
	// letter ("ff") as "next F". QTreeView does, but QAbstractItemView::keyboardSearch (list and table
	// views, and every QComboBox, whose popup is a list view) then searches for the whole "ff" and
	// finds nothing, so tapping a letter quickly stops cycling (Qt 6.8, still so in Qt's dev branch).
	// A repeated letter inside that window is handled here; everything else is left to Qt.
	QPointer<QObject> _typeAheadWidget;
	QChar _typeAheadLetter; // null once the keys in the window were not all one letter
	QElapsedTimer _typeAheadTimer;

	bool cycleRepeatedLetter(QObject* watched, const QKeyEvent* key)
	{
		// only the widget the key was sent to, not the parents it propagates through
		if (watched != QApplication::focusWidget())
			return false;

		auto* view = qobject_cast<QAbstractItemView*>(watched);
		auto* combo = qobject_cast<QComboBox*>(watched);
		if (view == nullptr && combo == nullptr)
			return false;
		if (qobject_cast<QTreeView*>(watched) != nullptr)
			return false; // QTreeView cycles correctly on its own
		if (combo != nullptr && combo->isEditable())
			return false; // typing goes to its line edit
		if (view != nullptr && (view->editTriggers() & QAbstractItemView::AnyKeyPressed))
			return false; // a key starts editing instead of searching

		const QString text = key->text();
		if (text.size() != 1 || !text.at(0).isLetterOrNumber() ||
			(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
			return false;
		const QChar letter = text.at(0).toLower();

		const bool quick = _typeAheadWidget == watched && _typeAheadTimer.isValid() &&
			_typeAheadTimer.elapsed() <= QApplication::keyboardInputInterval();
		_typeAheadTimer.restart();

		if (!quick) {
			// a new search: Qt finds the first match
			_typeAheadWidget = watched;
			_typeAheadLetter = letter;
			return false;
		}
		if (_typeAheadLetter != letter) {
			// several letters typed quickly: Qt's prefix search is right for that
			_typeAheadLetter = QChar();
			return false;
		}

		if (view != nullptr) {
			const QModelIndex current = view->currentIndex();
			if (!current.isValid())
				return false;
			const QModelIndex next = nextStartingWith(*view->model(), current, letter);
			if (next.isValid())
				view->setCurrentIndex(next);
		} else {
			const int row = combo->currentIndex();
			if (row < 0)
				return false;
			const QModelIndex current = combo->model()->index(row, combo->modelColumn(), combo->rootModelIndex());
			const QModelIndex next = nextStartingWith(*combo->model(), current, letter);
			if (next.isValid() && next.row() != row) {
				// as QComboBox's own keyboard search does
				combo->setCurrentIndex(next.row());
				Q_EMIT combo->activated(next.row());
				Q_EMIT combo->textActivated(combo->itemText(next.row()));
			}
		}
		return true;
	}

	// The next enabled item after `from` among its siblings, wrapping, whose text starts with `letter`.
	static QModelIndex nextStartingWith(const QAbstractItemModel& model, const QModelIndex& from, QChar letter)
	{
		const QModelIndex parent = from.parent();
		const int rows = model.rowCount(parent);
		for (int i = 1; i <= rows; ++i) {
			const QModelIndex index = model.index((from.row() + i) % rows, from.column(), parent);
			if (!(model.flags(index) & Qt::ItemIsEnabled))
				continue;
			const QString itemText = index.data(Qt::DisplayRole).toString();
			if (!itemText.isEmpty() && itemText.at(0).toLower() == letter)
				return index;
		}
		return {};
	}

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
