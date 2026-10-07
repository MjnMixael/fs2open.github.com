#pragma once

#include "globalincs/pstypes.h"

// The one music track a mission plays from a file with play-music-from-file, stop-music-from-file and
// pause-music-from-file. It loops until stopped or replaced, and stops when the mission ends.

#define MISSION_MUSIC_NONE "<none>"

struct mission_music_state {
	SCP_string filename;
	int volume = 100;		// percent of the player's music volume
	bool paused = false;
	int position_ms = 0;
};

// Starts a track, replacing the current one (faded out when fade is set). MISSION_MUSIC_NONE stops the current
// track instead, over 5 seconds when fade is set.
void mission_music_play(const char* filename, bool fade, int volume = 100, int start_ms = 0);

// Stops the track, at once when fade_ms is 0 or less, otherwise ramping its volume down over fade_ms first
void mission_music_stop(int fade_ms = 0);

void mission_music_toggle_pause();

// Drives a timed stop
void mission_music_do_frame();

// Called when the mission closes
void mission_music_close();

// Every file in data/music that the editors offer, minus $Ignore Music Files In Music Player:
void mission_music_get_file_list(SCP_vector<SCP_string>& list);

// Whether a music file exists, named with or without its extension
bool mission_music_file_exists(const char* filename);

// What is playing, for a checkpoint. False when nothing is playing or the track is being stopped.
bool mission_music_get_state(mission_music_state& state);

// Starts the track a checkpoint stored, where it left off
void mission_music_restore(const mission_music_state& state);
