#include "mission/MusicPlayer.h"

#include <cfile/cfile.h>
#include <mod_table/mod_table.h>
#include <sound/audiostr.h>

namespace fso::fred {

MusicPlayer::MusicPlayer(QObject* parent) : QObject(parent)
{
	// polls for the end of a track, like FRED2's DoFrame()
	_timer.setInterval(200);
	connect(&_timer, &QTimer::timeout, this, &MusicPlayer::tick);
}

MusicPlayer::~MusicPlayer()
{
	closeStream();
}

void MusicPlayer::ensureTracksLoaded()
{
	if (_tracksLoaded)
		return;
	_tracksLoaded = true;
	loadTracks();
}

void MusicPlayer::loadTracks()
{
	_tracks.clear();

	SCP_vector<SCP_string> files;
	cf_get_file_list(files, CF_TYPE_MUSIC, "*", CF_SORT_NAME);

	for (const auto& f : files) {
		bool add = true;
		for (const auto& ignored : Ignored_music_player_files) {
			if (lcase_equal(ignored, f)) {
				add = false;
				break;
			}
		}
		if (add)
			_tracks.push_back(f);
	}

	_currentRow = -1;
	stop();
	Q_EMIT stateChanged();
}

void MusicPlayer::setCurrentRow(int row)
{
	if (row < -1 || row >= static_cast<int>(_tracks.size()) || row == _currentRow)
		return;
	_currentRow = row;
	Q_EMIT stateChanged();
}

SCP_string MusicPlayer::loadedTrack() const
{
	if (_musicId < 0 || !SCP_vector_inbounds(_tracks, _streamRow))
		return "";
	return _tracks[_streamRow];
}

int MusicPlayer::tryOpenStream(const SCP_string& baseNoExt)
{
	// The track list has no extensions, and event music opens with the extension it is given:
	// guessing a wrong one makes the engine warn that the file has another extension. So look up
	// the real file the way the engine does for an extensionless name, and open that.
	const auto res = cf_find_file_location_ext(baseNoExt.c_str(), NUM_AUDIO_EXT, audio_ext_list, CF_TYPE_MUSIC);
	if (!res.found)
		return -1;
	return audiostream_open(res.name_ext.c_str(), ASF_EVENTMUSIC);
}

bool MusicPlayer::isPlaying() const
{
	return _musicId >= 0 && audiostream_is_playing(_musicId);
}

bool MusicPlayer::isPaused() const
{
	return _musicId >= 0 && audiostream_is_paused(_musicId) == 1;
}

void MusicPlayer::play()
{
	if (!SCP_vector_inbounds(_tracks, _currentRow))
		return;

	if (isPaused() && _streamRow == _currentRow) {
		audiostream_unpause(_musicId);
		Q_EMIT stateChanged();
		return;
	}

	closeStream();
	const auto& name = _tracks[_currentRow];
	_musicId = tryOpenStream(name);
	if (_musicId >= 0) {
		_streamRow = _currentRow;
		audiostream_play(_musicId, 1.0f, 0);
		_timer.start();
	} else {
		Warning(LOCATION, "FRED failed to open music file %s in the music player\n", name.c_str());
	}
	Q_EMIT stateChanged();
}

void MusicPlayer::togglePlay()
{
	if (isPlaying()) {
		audiostream_pause(_musicId);
		Q_EMIT stateChanged();
	} else {
		play();
	}
}

void MusicPlayer::stop()
{
	if (_musicId < 0)
		return;
	closeStream();
	Q_EMIT stateChanged();
}

void MusicPlayer::closeStream()
{
	_timer.stop();
	if (_musicId >= 0) {
		audiostream_close_file(_musicId, false);
		_musicId = -1;
	}
	_streamRow = -1;
}

void MusicPlayer::togglePause()
{
	if (isPlaying())
		audiostream_pause(_musicId);
	else if (isPaused())
		audiostream_unpause(_musicId);
	else
		return;
	Q_EMIT stateChanged();
}

int MusicPlayer::stepBase() const
{
	return hasStream() ? _streamRow : _currentRow;
}

bool MusicPlayer::canSelectNext() const
{
	const int base = stepBase();
	return base >= 0 && base < static_cast<int>(_tracks.size()) - 1;
}

bool MusicPlayer::canSelectPrev() const
{
	return stepBase() > 0;
}

void MusicPlayer::selectNext()
{
	if (!canSelectNext())
		return;
	const bool loaded = hasStream();
	setCurrentRow(stepBase() + 1);
	if (loaded)
		play();
}

void MusicPlayer::selectPrev()
{
	if (!canSelectPrev())
		return;
	const bool loaded = hasStream();
	setCurrentRow(stepBase() - 1);
	if (loaded)
		play();
}

void MusicPlayer::setAutoplay(bool on)
{
	if (_autoplay == on)
		return;
	_autoplay = on;
	Q_EMIT stateChanged();
}

void MusicPlayer::tick()
{
	// a paused stream isn't playing either, so only a stream that is neither has ended
	if (_musicId < 0 || isPlaying() || isPaused())
		return;

	// autoplay moves on from the track that ended, wherever the selection is
	const int next = _streamRow + 1;
	if (_autoplay && _streamRow >= 0 && next < static_cast<int>(_tracks.size())) {
		_currentRow = next;
		play();
	} else {
		stop();
	}
}

} // namespace fso::fred
