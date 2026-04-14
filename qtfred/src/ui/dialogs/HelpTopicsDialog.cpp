#include "HelpTopicsDialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHelpContentWidget>
#include <QHelpEngine>
#include <QHelpIndexWidget>
#include <QMessageBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace fso::fred::dialogs {

class HelpTopicsDialog::HelpBrowser : public QTextBrowser {
 public:
	explicit HelpBrowser(QHelpEngine* helpEngine, QWidget* parent = nullptr) : QTextBrowser(parent), _helpEngine(helpEngine) {
		setOpenLinks(false);
	}

 protected:
	QVariant loadResource(int type, const QUrl& name) override {
		if (name.scheme() == QStringLiteral("qthelp") && _helpEngine != nullptr) {
			return _helpEngine->fileData(name);
		}
		return QTextBrowser::loadResource(type, name);
	}

 private:
	QHelpEngine* _helpEngine = nullptr;
};

HelpTopicsDialog::HelpTopicsDialog(QWidget* parent) : QDialog(parent) {
	setWindowTitle(tr("QtFRED Help"));
	resize(1000, 700);

	auto* rootLayout = new QVBoxLayout(this);
	_splitter = new QSplitter(this);
	rootLayout->addWidget(_splitter);

	if (!initializeHelpEngine()) {
		_splitter->setDisabled(true);
		return;
	}

	_navigationTabs = new QTabWidget(_splitter);
	_navigationTabs->addTab(_helpEngine->contentWidget(), tr("Contents"));
	_navigationTabs->addTab(_helpEngine->indexWidget(), tr("Index"));

	_helpBrowser = new HelpBrowser(_helpEngine.get(), _splitter);
	_helpBrowser->setOpenExternalLinks(true);

	_splitter->addWidget(_navigationTabs);
	_splitter->addWidget(_helpBrowser);
	_splitter->setStretchFactor(0, 1);
	_splitter->setStretchFactor(1, 3);

	connect(_helpEngine->contentWidget(), &QHelpContentWidget::linkActivated, this, &HelpTopicsDialog::loadHelpPage);
	connect(_helpEngine->indexWidget(), &QHelpIndexWidget::linkActivated, this, &HelpTopicsDialog::loadHelpPage);

	const auto registeredDocuments = _helpEngine->registeredDocumentations();
	if (!registeredDocuments.isEmpty()) {
		const auto homePage = _helpEngine->findFile(
			QUrl(QStringLiteral("qthelp://%1/doc/index.html").arg(registeredDocuments.front())));
		if (homePage.isValid()) {
			loadHelpPage(homePage);
		}
	}
}

HelpTopicsDialog::~HelpTopicsDialog() = default;

bool HelpTopicsDialog::initializeHelpEngine() {
	const auto collectionFile = resolveCollectionFile();
	if (collectionFile.isEmpty()) {
		QMessageBox::information(this,
			tr("Help is not installed"),
			tr("QtFRED help was not found.\n\nExpected file: help/qtfred.qhc next to the QtFRED executable."));
		return false;
	}

	_helpEngine = std::make_unique<QHelpEngine>(collectionFile);
	if (!_helpEngine->setupData()) {
		QMessageBox::warning(this,
			tr("Failed to load help"),
			tr("Could not initialize Qt help data:\n%1").arg(_helpEngine->error()));
		_helpEngine.reset();
		return false;
	}

	return true;
}

QString HelpTopicsDialog::resolveCollectionFile() const {
	const QDir appDir(QCoreApplication::applicationDirPath());
	const QString candidate = appDir.filePath(QStringLiteral("help/qtfred.qhc"));
	if (QFileInfo::exists(candidate)) {
		return candidate;
	}

	return {};
}

void HelpTopicsDialog::loadHelpPage(const QUrl& url) {
	if (_helpBrowser == nullptr || !url.isValid()) {
		return;
	}

	_helpBrowser->setSource(url);
}

} // namespace fso::fred::dialogs
