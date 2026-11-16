#pragma once

#include <globalincs/pstypes.h>

#include <QObject>
#include <QTimer>

namespace fso::fred {

// The editor's music player. It belongs to the main window, so music keeps playing with the Music
// Player dialog closed; the dialog and the toolbar controls are both views of it.
class MusicPlayer final : public QObject {
	Q_OBJECT
  public:
	explicit MusicPlayer(QObject* parent = nullptr);
	~MusicPlayer() override;

	// Fills the track list from the music folder, minus editor.tbl's ignored files
	void loadTracks();
	const SCP_vector<SCP_string>& tracks() const { return _tracks; }

	// The selected track, or -1
	int currentRow() const { return _currentRow; }
	void setCurrentRow(int row);

	// The track that is playing or paused, or an empty string / -1
	SCP_string loadedTrack() const;
	int loadedRow() const { return _musicId >= 0 ? _streamRow : -1; }

	bool hasStream() const { return _musicId >= 0; }
	bool isPlaying() const;
	bool isPaused() const;

	// Resumes the selected track if it's the paused one, otherwise starts it from the beginning
	void play();
	// The dialog's Play / Pause button: pauses while playing, otherwise plays the selection
	void togglePlay();
	// The toolbar's Play / Pause button: pauses or resumes the loaded track, never starts one
	void togglePause();
	void stop();

	// Previous / Next step from the loaded track, or from the selection when nothing is loaded
	bool canSelectNext() const;
	bool canSelectPrev() const;
	// Move the selection; when a track is loaded, the new one plays
	void selectNext();
	void selectPrev();

	bool autoplay() const { return _autoplay; }
	void setAutoplay(bool on);

  signals:
	void stateChanged();

  private:
	void tick();
	void closeStream();
	int stepBase() const;
	static int tryOpenStream(const SCP_string& baseNoExt);

	SCP_vector<SCP_string> _tracks;
	int _currentRow = -1;
	int _streamRow = -1; // the track _musicId was opened from
	int _musicId = -1;   // audiostream id or -1 when none
	bool _autoplay = false;
	QTimer _timer;
};

} // namespace fso::fred
