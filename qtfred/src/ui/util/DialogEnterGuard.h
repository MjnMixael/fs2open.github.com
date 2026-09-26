#pragma once

class QDialog;
class QObject;

namespace fso {
namespace fred {
namespace util {

// QDialog presses its default button when Return/Enter reaches it, and nearly
// every field passes the key on: a line edit after emitting returnPressed, a
// spin box after committing its value, and checkboxes, combos and lists
// untouched. With no OK button the "default" is the first auto-default push
// button in focus order, so Enter in a field could click Prev, Add or Reset.
//
// The guard swallows Return/Enter when it reaches a dialog unhandled, so typing
// in a field never closes an editor or clicks an unrelated button. Widgets still
// see the key first (find-next hooks and spin box commits keep working), and a
// focused push button still clicks on Enter because it handles the key itself.
// Qt's own prompt dialogs (QInputDialog, QMessageBox, file/color/font pickers)
// keep their normal Enter-to-accept behavior.

// Install once at startup; the guard is parented to `owner`.
void installDialogEnterGuard(QObject* owner);

// Opt a small picker/prompt dialog back in to Enter pressing its default
// (OK) button from any field, which is what users expect there.
void allowEnterToAccept(QDialog* dialog);

}
}
}
