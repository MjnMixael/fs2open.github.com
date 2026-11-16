#pragma once

#include "mission/dialogs/AbstractDialogModel.h"
#include "mission/MusicPlayer.h"

namespace fso::fred::dialogs {

// The Music Player dialog's view of the main window's MusicPlayer, which owns playback so music
// can outlive the dialog. Player changes come through as modelChanged().
class MusicPlayerDialogModel final : public AbstractDialogModel {
	Q_OBJECT
  public:
	MusicPlayerDialogModel(QObject* parent, EditorViewport* viewport, MusicPlayer* player);
	~MusicPlayerDialogModel() override = default;

	bool apply() override; // nothing to apply
	// Closing the dialog: stops the music when the toolbar has no controls for it
	void reject() override;

	const SCP_vector<SCP_string>& tracks() const { return _player->tracks(); }
	int currentRow() const { return _player->currentRow(); }
	void setCurrentRow(int row) { _player->setCurrentRow(row); }

	bool hasStream() const { return _player->hasStream(); }
	bool isPlaying() const { return _player->isPlaying(); }
	void play() { _player->play(); }
	void togglePlay() { _player->togglePlay(); }
	void stop() { _player->stop(); }

	bool canSelectNext() const { return _player->canSelectNext(); }
	bool canSelectPrev() const { return _player->canSelectPrev(); }
	void selectNext() { _player->selectNext(); }
	void selectPrev() { _player->selectPrev(); }

	bool autoplay() const { return _player->autoplay(); }
	void setAutoplay(bool on) { _player->setAutoplay(on); }

  private:
	MusicPlayer* _player = nullptr;
};

} // namespace fso::fred::dialogs
