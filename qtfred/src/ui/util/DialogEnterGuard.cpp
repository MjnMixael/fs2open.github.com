#include "DialogEnterGuard.h"

#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QFontDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QProgressDialog>

namespace fso::fred::util {

namespace {

constexpr auto ALLOW_ENTER_PROPERTY = "fred_allow_enter_to_accept";

class DialogEnterGuard : public QObject {
  public:
	using QObject::QObject;

  protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (event->type() != QEvent::KeyPress)
			return false;

		// Key events propagate from the focus widget up through its parents, and an
		// application filter sees each step. Only act at the dialog itself, i.e. once
		// every widget below it has passed on the key.
		auto* dialog = qobject_cast<QDialog*>(watched);
		if (dialog == nullptr)
			return false;

		// The same keys QDialog::keyPressEvent treats as "press the default button".
		const auto* key = static_cast<QKeyEvent*>(event);
		const bool isEnter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
		const bool plain = key->modifiers() == Qt::NoModifier ||
			(key->modifiers() == Qt::KeypadModifier && key->key() == Qt::Key_Enter);
		if (!isEnter || !plain)
			return false;

		if (dialog->property(ALLOW_ENTER_PROPERTY).toBool())
			return false;
		if (qobject_cast<QInputDialog*>(dialog) || qobject_cast<QMessageBox*>(dialog) ||
			qobject_cast<QFileDialog*>(dialog) || qobject_cast<QColorDialog*>(dialog) ||
			qobject_cast<QFontDialog*>(dialog) || qobject_cast<QProgressDialog*>(dialog)) {
			return false;
		}

		return true; // swallow it: no default button fires
	}
};

} // namespace

void installDialogEnterGuard(QObject* owner)
{
	qApp->installEventFilter(new DialogEnterGuard(owner));
}

void allowEnterToAccept(QDialog* dialog)
{
	dialog->setProperty(ALLOW_ENTER_PROPERTY, true);
}

} // namespace fso::fred::util
