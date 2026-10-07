/*
 * Copyright (C) Freespace Open 2013.  All rights reserved.
 *
 * All source code herein is the property of Freespace Open. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 */

#include "mission/missionmusic.h"

#include "cfile/cfile.h"
#include "cmdline/cmdline.h"
#include "gamesnd/eventmusic.h"
#include "globalincs/systemvars.h"
#include "mod_table/mod_table.h"
#include "sound/audiostr.h"
#include "sound/sound.h"

namespace {

// How long <none> takes to fade the music out
const int NONE_FADE_MS = 5000;

int Track_handle = -1;
SCP_string Track_filename;
int Track_volume = 100;
bool Track_paused = false;

// A timed stop in progress
bool Track_stopping = false;
fix Stop_started = 0;
int Stop_duration_ms = 0;

float track_gain(int volume)
{
	return Master_event_music_volume * aav_music_volume * (i2fl(volume) / 100.0f);
}

void forget_track()
{
	Track_handle = -1;
	Track_filename.clear();
	Track_volume = 100;
	Track_paused = false;
	Track_stopping = false;
}

void close_track(bool fade)
{
	if (Track_handle >= 0)
		nprintf(("Sound", "MUSIC => close handle %d ('%s') fade %d, playing %d, paused %d\n", Track_handle, Track_filename.c_str(),
			(int)fade, audiostream_is_playing(Track_handle), audiostream_is_paused(Track_handle)));
	if (Track_handle >= 0)
		audiostream_close_file(Track_handle, fade);

	forget_track();
}

void start_track(const char* filename, int volume, int start_ms)
{
	Track_handle = audiostream_open(filename, ASF_MENUMUSIC);
	nprintf(("Sound", "MUSIC => start '%s' volume %d start %d ms -> handle %d\n", filename, volume, start_ms, Track_handle));
	if (Track_handle < 0) {
		mprintf(("Unable to play music file %s\n", filename));
		forget_track();
		return;
	}

	Track_filename = filename;
	Track_volume = volume;
	Track_paused = false;
	Track_stopping = false;

	if (start_ms > 0)
		audiostream_seek(Track_handle, i2fl(start_ms) / 1000.0f);

	audiostream_play(Track_handle, track_gain(volume), 1);
}

}

void mission_music_play(const char* filename, bool fade, int volume, int start_ms)
{
	if (Cmdline_freespace_no_music || filename == nullptr)
		return;

	if (!stricmp(filename, MISSION_MUSIC_NONE)) {
		mission_music_stop(fade ? NONE_FADE_MS : 0);
		return;
	}

	CLAMP(volume, 0, 100);

	// A track being stopped on a timer is dropped outright; otherwise the old track fades out under the new one
	close_track(fade && !Track_stopping);

	start_track(filename, volume, start_ms);
}

void mission_music_stop(int fade_ms)
{
	if (Track_handle < 0)
		return;

	if (fade_ms <= 0) {
		close_track(false);
		return;
	}

	nprintf(("Sound", "MUSIC => timed stop of handle %d over %d ms\n", Track_handle, fade_ms));
	Track_stopping = true;
	Stop_started = Missiontime;
	Stop_duration_ms = fade_ms;
}

void mission_music_toggle_pause()
{
	if (Track_handle < 0)
		return;

	if (Track_paused)
		audiostream_unpause(Track_handle, true);
	else
		audiostream_pause(Track_handle, true);

	Track_paused = !Track_paused;
	nprintf(("Sound", "MUSIC => handle %d %s\n", Track_handle, Track_paused ? "paused" : "unpaused"));
}

void mission_music_do_frame()
{
	if (Track_handle < 0 || !Track_stopping)
		return;

	// Mission time, so the fade holds while the game is paused
	const auto elapsed_ms = fl2i(f2fl(Missiontime - Stop_started) * 1000.0f);
	if (elapsed_ms >= Stop_duration_ms) {
		close_track(false);
		return;
	}

	const auto remaining = 1.0f - (i2fl(std::max(elapsed_ms, 0)) / i2fl(Stop_duration_ms));
	audiostream_set_volume(Track_handle, track_gain(Track_volume) * remaining);
}

void mission_music_close()
{
	close_track(false);
}

void mission_music_get_file_list(SCP_vector<SCP_string>& list)
{
	list.clear();

	SCP_vector<SCP_string> files;
	cf_get_file_list(files, CF_TYPE_MUSIC, "*", CF_SORT_NAME);

	for (const auto& file : files) {
		// the same name can come from more than one extension
		if (!list.empty() && lcase_equal(list.back(), file))
			continue;

		bool ignored = false;
		for (const auto& ignore : Ignored_music_player_files) {
			if (lcase_equal(ignore, file)) {
				ignored = true;
				break;
			}
		}

		if (!ignored)
			list.push_back(file);
	}
}

bool mission_music_file_exists(const char* filename)
{
	SCP_string name = filename;

	const auto ext = name.rfind('.');
	if (ext != SCP_string::npos)
		name.erase(ext);

	return cf_find_file_location_ext(name.c_str(), NUM_AUDIO_EXT, audio_ext_list, CF_TYPE_MUSIC).found;
}

bool mission_music_get_state(mission_music_state& state)
{
	if (Track_handle < 0 || Track_stopping)
		return false;

	state.filename = Track_filename;
	state.volume = Track_volume;
	state.paused = Track_paused;
	state.position_ms = fl2i(audiostream_get_position(Track_handle) * 1000.0);
	nprintf(("Sound", "MUSIC => stored '%s' at %d ms, paused %d\n", state.filename.c_str(), state.position_ms, (int)state.paused));

	return true;
}

void mission_music_restore(const mission_music_state& state)
{
	if (Cmdline_freespace_no_music)
		return;

	nprintf(("Sound", "MUSIC => restoring '%s' at %d ms, paused %d\n", state.filename.c_str(), state.position_ms, (int)state.paused));
	close_track(false);
	start_track(state.filename.c_str(), state.volume, state.position_ms);

	if (state.paused)
		mission_music_toggle_pause();
}
