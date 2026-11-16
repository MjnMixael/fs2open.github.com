#pragma once

#include <QDialog>
#include <QListWidgetItem>

#include <mission/MusicPlayer.h>
#include <ui/FredView.h>

namespace fso::fred::dialogs {

namespace Ui {
class MusicPlayerDialog;
}

// A view of the main window's MusicPlayer. Closing it leaves the music playing while the toolbar
// has the music controls (a preference); with them hidden, closing it stops the music.
class MusicPlayerDialog final : public QDialog {
	Q_OBJECT
  public:
	MusicPlayerDialog(FredView* parent, EditorViewport* viewport);
	~MusicPlayerDialog() override;

	void accept() override; // not used

  private slots:
	void on_musicList_itemSelectionChanged();
	void on_musicList_itemDoubleClicked(QListWidgetItem* item);
	void on_playButton_clicked();
	void on_stopButton_clicked();
	void on_nextButton_clicked();
	void on_prevButton_clicked();
	void on_autoplayCheck_toggled(bool on);
	void on_musicTblButton_clicked();

  private: // NOLINT(readability-redundant-access-specifiers)
	void populateList();
	void updateUi();

	EditorViewport* _viewport = nullptr;
	std::unique_ptr<Ui::MusicPlayerDialog> ui;
	MusicPlayer* _player = nullptr;
};

} // namespace fso::fred::dialogs
