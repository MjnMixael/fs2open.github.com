#pragma once

#include <QList>

class QObject;
class QWidget;

namespace fso::fred::util {

// Application-wide keyboard navigation fixes, installed once as an event filter:
//
// Visible Tab focus on buttons. The theme's stylesheet styles push and tool buttons, and a
// stylesheet-styled button no longer gets the style's focus rectangle, so a button reached
// with Tab showed no sign of focus. Stylesheets have no :focus-visible, and a plain :focus
// rule would also ring every button the user clicks, so a button is marked (the
// "keyboardFocus" property) only while it holds focus it got from the keyboard: Tab,
// Shift+Tab or a shortcut. The theme rings buttons with [keyboardFocus="true"].
//
// Tab leaves multi-line text boxes. QPlainTextEdit and QTextEdit type a tab character on
// Tab by default, so Tab could never move past a description or message box (only Ctrl+Tab
// could). Every such box is switched to tabChangesFocus when it is first shown, unless it
// opted out with allowTabInput().
//
// Tapping a letter cycles lists and dropdowns. Pressing F jumps to the next item starting
// with F, but in list views and combo boxes Qt stopped cycling when F was tapped quickly
// (within keyboardInputInterval()): it searched for "FF" instead of the next F. A repeated
// letter is handled here; tree views already get it right and are left alone.

// Install once at startup; the filter is parented to `owner`.
void installKeyboardNavigation(QObject* owner);

// Keep Tab typing a tab character in this text box (a code or format editor). Call before
// the widget is first shown.
void allowTabInput(QWidget* textBox);

// Tab order for widgets created in code: a dialog's .ui tab order is applied in setupUi(),
// and widgets added after it land at the end of the chain, after OK/Cancel. This chains
// the given widgets in order (null entries are skipped); anchor the list on a .ui widget
// before them and the one after them to slot them in place.
void setTabChain(const QList<QWidget*>& widgets);

} // namespace fso::fred::util
