#pragma once

#include <QDialog>

#include <memory>

class QHelpEngine;
class QSplitter;
class QTabWidget;
class QTextBrowser;

namespace fso::fred::dialogs {

class HelpTopicsDialog : public QDialog {
	Q_OBJECT

 public:
	explicit HelpTopicsDialog(QWidget* parent = nullptr);
	~HelpTopicsDialog() override;

 private:
	class HelpBrowser;

	bool initializeHelpEngine();
	QString resolveCollectionFile() const;
	void loadHelpPage(const QUrl& url);

	std::unique_ptr<QHelpEngine> _helpEngine;
	QTabWidget* _navigationTabs = nullptr;
	QSplitter* _splitter = nullptr;
	HelpBrowser* _helpBrowser = nullptr;
};

} // namespace fso::fred::dialogs
