#include "anim/packunpack.h"
#include "globalincs/globals.h"
#include "graphics/2d.h"
#include "graphics/generic.h"
#include "cutscene/Decoder.h"
#include "cutscene/player.h"
#define BMPMAN_INTERNAL
#include "bmpman/bm_internal.h"
#ifdef _WIN32
#include <windows.h>	// for MAX_PATH
#else
#define MAX_PATH	255
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
//#define TIMER
#ifdef TIMER
#include "io/timer.h"
#endif

// Movie file extensions handled by generic_anim itself (not by bm_load_animation).
// Kept local to avoid polluting bm_ani_ext_list, which is used elsewhere for
// EFF/ANI frame-name parsing.
// IMPORTANT: every extension here MUST be 3 characters. cf_find_file_location_ext
// filters its pack/loose index matches by a single precomputed length derived from
// ext_list[0] (see the "assumes that everything in ext_list[] is the same length"
// FIXME in cfilesystem.cpp), so a mixed-length list silently fails to find files
// whose extension differs in length from the first. That is why .webm (4-char) is
// not supported for movie anims -- it would break lookups of .mp4/.ogg files.
static const char* movie_ext_list[] = { ".mp4", ".ogg" };
static const int MOVIE_NUM_TYPES = sizeof(movie_ext_list) / sizeof(movie_ext_list[0]);

// Directories searched for movie-format generic anims. Deliberately excludes
// CF_TYPE_MOVIES so a tech anim referencing "intro" does not pick up
// data/movies/intro.mp4 (the FS2 cutscene), and the cutscene system's default
// search (ROOT + MOVIES) keeps cutscenes from picking up tech anims.
static const int generic_anim_movie_dirs[] = {
	CF_TYPE_INTEL_ANIMS,
	CF_TYPE_CBANIMS,
	CF_TYPE_INTERFACE,
	CF_TYPE_HUD,
	CF_TYPE_MAPS,
	CF_TYPE_EFFECTS,
};

// Look up a movie file (mp4/ogg) for a generic_anim base name across the
// anim directories in priority order. Returns the first hit. On success, *out_dir
// (if non-null) receives the CF_TYPE directory the file was found in, so the caller
// can constrain the decoder to that same directory. extension_index is filled with
// the matched movie_ext_list index.
static CFileLocationExt find_movie_for_generic_anim(const char* filename, int* out_dir = nullptr)
{
	for (int dir : generic_anim_movie_dirs) {
		auto res = cf_find_file_location_ext(filename, MOVIE_NUM_TYPES, movie_ext_list, dir);
		if (res.found) {
			if (out_dir != nullptr)
				*out_dir = dir;
			return res;
		}
	}
	return CFileLocationExt();
}

// Holds the heavyweight state for a streaming movie generic_anim. The generic_anim
// union only carries a raw pointer to this (mirroring apng's apng_ani*); the real
// state lives here so cutscene headers stay out of generic.h.
struct movie_anim_state {
	std::unique_ptr<cutscene::Decoder> decoder;
	std::unique_ptr<std::thread> decoder_thread;
	cutscene::VideoFramePtr current_frame;	// most recently displayed frame
	cutscene::VideoFramePtr next_frame;		// peeked-ahead frame whose frameTime we use to detect loop wrap
	double playback_time = 0.0;	// seconds since last loop wrap
	float fps = 0.0f;
	float duration = 0.0f;
	bool looping = true;
	bool finished = false;		// true once a NOLOOP playthrough has completed
	bool warned_backwards = false;
};
 
//we check background type to avoid messed up colours for ANI
#define ANI_BPP_CHECK		(ga->ani.bg_type == BM_TYPE_PCX) ? 16 : 32

// These two functions find if a bitmap or animation exists by filename, no extension needed.
bool generic_bitmap_exists(const char *filename)
{
	return cf_exists_full_ext(filename, CF_TYPE_ANY, BM_NUM_TYPES, bm_ext_list) != 0;
}

bool generic_anim_exists(const char *filename)
{
	if (cf_exists_full_ext(filename, CF_TYPE_ANY, BM_ANI_NUM_TYPES, bm_ani_ext_list) != 0)
		return true;
	return find_movie_for_generic_anim(filename).found;
}

// Goober5000
int generic_anim_init_and_stream(generic_anim *ga, const char *anim_filename, BM_TYPE bg_type, bool attempt_hi_res)
{
	int stream_result = -1;
	char filename[NAME_LENGTH];
	char *p;

	Assert(ga != NULL);
	Assert(anim_filename != NULL);

	// hi-res support
	if (attempt_hi_res && (gr_screen.res == GR_1024)) {
		// attempt to load a hi-res animation
		memset(filename, 0, NAME_LENGTH);
		strcpy_s(filename, "2_");
		strncat(filename, anim_filename, NAME_LENGTH - 3);

		// remove extension
		p = strchr(filename, '.');
		if(p) {
			*p = '\0';
		}

		// attempt to stream the hi-res ani
		generic_anim_init(ga, filename);
		ga->ani.bg_type = bg_type;
		stream_result = generic_anim_stream(ga);
	}

	// we failed to stream hi-res, or we aren't running in hi-res, so try low-res
	if (stream_result < 0) {
		strcpy_s(filename, anim_filename);

		// remove extension
		p = strchr(filename, '.');
		if(p) {
			*p = '\0';
		}

		// attempt to stream the low-res ani
		generic_anim_init(ga, filename);
		ga->ani.bg_type = bg_type;
		stream_result = generic_anim_stream(ga);
	}

	return stream_result;
}

// Goober5000
void generic_anim_init(generic_anim *ga)
{
	generic_anim_init(ga, NULL);
}

// Goober5000
void generic_anim_init(generic_anim *ga, const char *filename)
{
	if (filename != NULL)
		strcpy_s(ga->filename, filename);
	else
		memset(ga->filename, 0, MAX_FILENAME_LEN);
	ga->first_frame = -1;
	ga->num_frames = 0;
	ga->keyframe = 0;
	ga->keyoffset = 0;
	ga->current_frame = 0;
	ga->previous_frame = -1;
	ga->direction = GENERIC_ANIM_DIRECTION_FORWARDS;
	ga->done_playing = 0;
	ga->total_time = 0.0f;
	ga->anim_time = 0.0f;

	//we only care about the stuff below if we're streaming
	ga->ani.animation = NULL;
	ga->ani.instance = NULL;
	ga->ani.bg_type = BM_TYPE_NONE;
	ga->type = BM_TYPE_NONE;
	ga->streaming = 0;
	ga->buffer = NULL;
	ga->height = 0;
	ga->width = 0;
	ga->bitmap_id = -1;
	ga->use_hud_color = false;
}

// CommanderDJ - same as generic_anim_init, just with an SCP_string 
void generic_anim_init(generic_anim *ga, const SCP_string& filename)
{
	generic_anim_init(ga);
	filename.copy(ga->filename, MAX_FILENAME_LEN - 1);
}

// Goober5000
void generic_bitmap_init(generic_bitmap *gb, const char *filename)
{
	if (filename == NULL) {
		gb->filename[0] = '\0';
	} else {
		strncpy(gb->filename, filename, MAX_FILENAME_LEN - 1);
	}

	gb->bitmap_id = -1;
}

// Goober5000
// load a generic_anim
// return 0 is successful, otherwise return -1
int generic_anim_load(generic_anim *ga)
{
	int fps;

	if ( !VALID_FNAME(ga->filename) )
		return -1;

	ga->first_frame = bm_load_animation(ga->filename, &ga->num_frames, &fps, &ga->keyframe, &ga->total_time);
	//mprintf(("generic_anim_load: %s - keyframe = %d\n", ga->filename, ga->keyframe));

	if (ga->first_frame < 0) {
		// Movie-format anims (mp4/ogg) can only be played by generic_anim_stream,
		// which drives the decoder on a background thread. There's no way to load one
		// frame-by-frame here, so point the modder at the streaming path rather than
		// failing with an indistinguishable "not found".
		if (find_movie_for_generic_anim(ga->filename).found) {
			mprintf(("generic_anim_load: '%s' is a movie file; movie anims are only supported when streamed (generic_anim_stream), not via generic_anim_load.\n", ga->filename));
		}
		return -1;
	}

	ga->done_playing = 0;
	ga->anim_time = 0.0f;

	return 0;
}

int generic_anim_stream(generic_anim *ga, const bool cache)
{
	CFILE *img_cfp = NULL;
	int anim_fps = 0;
	int bpp;

	ga->type = BM_TYPE_NONE;

	auto res = cf_find_file_location_ext(ga->filename, BM_ANI_NUM_TYPES, bm_ani_ext_list, CF_TYPE_ANY);

	// Fall back to looking for a movie file (mp4/ogg) if no ani/eff/apng matched.
	// Order matters: ani/eff/apng take priority, so a .png is still treated as apng, not as a movie.
	// Movies are searched only in anim directories (intelanims, cbanims, interface, etc.)
	// to avoid conflicts with fullscreen cutscenes that may share a base name in data/movies.
	bool is_movie = false;
	int movie_dir = -1;
	if ( !res.found ) {
		res = find_movie_for_generic_anim(ga->filename, &movie_dir);
		if ( !res.found )
			return -1;
		is_movie = true;
	}

	//make sure we can open it
	img_cfp = cfopen_special(res, "rb", CF_TYPE_ANY);

	if (img_cfp == NULL) {
		return -1;
	}

	if (is_movie) {
		strcat_s(ga->filename, movie_ext_list[res.extension_index]);
		ga->type = BM_TYPE_MOVIE;
	} else {
		strcat_s(ga->filename, bm_ani_ext_list[res.extension_index]);
		ga->type = bm_ani_type_list[res.extension_index];
	}
	//seek to the end
	cfseek(img_cfp, 0, CF_SEEK_END);

	cfclose(img_cfp);

	if(ga->type == BM_TYPE_ANI) {
		bpp = ANI_BPP_CHECK;
		if(ga->use_hud_color)
			bpp = 8;
		if (ga->ani.animation == nullptr) {
			ga->ani.animation = anim_load(ga->filename, CF_TYPE_ANY);
		}
		if (ga->ani.instance == nullptr) {
			ga->ani.instance = init_anim_instance(ga->ani.animation, bpp);
		}

	#ifndef NDEBUG
		// for debug of ANI sizes
		strcpy_s(ga->ani.animation->name, ga->filename);
	#endif

		ga->num_frames = ga->ani.animation->total_frames;
		anim_fps = ga->ani.animation->fps;
		ga->height = ga->ani.animation->height;
		ga->width = ga->ani.animation->width;
		ga->buffer = ga->ani.instance->frame;
		ga->bitmap_id = bm_create(bpp, ga->width, ga->height, ga->buffer, (bpp==8)?BMP_AABITMAP:0);
		ga->ani.instance->last_bitmap = -1;

		ga->ani.instance->file_offset = ga->ani.animation->file_offset;
		ga->ani.instance->data = ga->ani.animation->data;

		ga->previous_frame = -1;
	}
	else if (ga->type == BM_TYPE_PNG) {
		if (ga->png.anim == nullptr) {
			try {
				ga->png.anim = new apng::apng_ani(ga->filename, cache);
			}
			catch (const apng::ApngException& e) {
				nprintf(("apng","Failed to load apng: %s\n", e.what() ));
				delete ga->png.anim;
				ga->png.anim = nullptr;
				ga->type = BM_TYPE_NONE;
				return -1;
			}
			nprintf(("apng", "apng read OK (%ix%i@%i) duration (%f)\n", ga->png.anim->w, ga->png.anim->h,
					ga->png.anim->bpp, ga->png.anim->anim_time));
		}
		ga->png.anim->goto_start();
		ga->current_frame = 0;
		ga->png.previous_frame_time = 0.0f;
		ga->num_frames = ga->png.anim->nframes;
		ga->height = ga->png.anim->h;
		ga->width = ga->png.anim->w;
		ga->previous_frame = -1;
		ga->buffer = ga->png.anim->frame.data.data();
		ga->bitmap_id = bm_create(ga->png.anim->bpp, ga->width, ga->height, ga->buffer, 0);
	}
	else if (ga->type == BM_TYPE_MOVIE) {
		cutscene::PlaybackProperties props;
		props.with_audio = false;
		props.looping = true;
		props.force_rgba = true;
		// Constrain the decoder to the exact directory we resolved the file in
		// above. Combined with movie_ext_list matching the decoder's extension
		// precedence, this guarantees the decoder opens the same file we detected
		// and never wanders into data/movies to grab a fullscreen cutscene.
		props.search_dirs.assign(1, movie_dir);

		auto decoder = cutscene::findDecoder(ga->filename, props);
		if (!decoder) {
			mprintf(("generic_anim: failed to open movie '%s'\n", ga->filename));
			ga->type = BM_TYPE_NONE;
			return -1;
		}

		const auto movie_props = decoder->getProperties();
		if (movie_props.size.width == 0 || movie_props.size.height == 0) {
			mprintf(("generic_anim: movie '%s' has zero dimensions\n", ga->filename));
			ga->type = BM_TYPE_NONE;
			return -1;
		}

		ga->width = static_cast<int>(movie_props.size.width);
		ga->height = static_cast<int>(movie_props.size.height);
		// Some containers (notably some ogg files) don't report a duration.
		// Looping playback is unaffected, but total_time then stays 0 — callers that
		// derive progress as anim_time/total_time must guard against that. Warn so a
		// modder relying on duration-based timing knows why it's unavailable.
		if (movie_props.duration <= 0.0f) {
			mprintf(("generic_anim: movie '%s' reports no duration; total_time will be 0 (looping playback is unaffected).\n", ga->filename));
		}
		ga->total_time = movie_props.duration > 0.0f ? movie_props.duration : 0.0f;
		const int est_frames = (movie_props.fps > 0.0f && movie_props.duration > 0.0f)
			? std::max(1, (int)std::lround(movie_props.fps * movie_props.duration))
			: 1;
		ga->num_frames = est_frames;
		ga->current_frame = 0;
		ga->previous_frame = -1;

		// Allocate a BGRA staging buffer for the decoded frame and create a
		// 32-bpp bitmap pointing at it. Zero-initialized so the bitmap reads as
		// fully transparent until the first frame arrives from the decoder.
		const size_t buf_bytes = static_cast<size_t>(ga->width) * static_cast<size_t>(ga->height) * 4;
		ga->buffer = (ubyte*)vm_malloc(buf_bytes);
		memset(ga->buffer, 0, buf_bytes);
		ga->bitmap_id = bm_create(32, ga->width, ga->height, ga->buffer, 0);
		if (ga->bitmap_id < 0) {
			vm_free(ga->buffer);
			ga->buffer = nullptr;
			ga->type = BM_TYPE_NONE;
			return -1;
		}

		auto state = new movie_anim_state();
		state->decoder = std::move(decoder);
		state->fps = movie_props.fps;
		state->duration = movie_props.duration;
		state->looping = props.looping;
		state->finished = false;

		// Spawn the decode loop on its own thread. startDecoding() is blocking
		// (it runs the read/decode loop until stopDecoder() flips the flag);
		// see Player::decoderThread() in cutscene/player.cpp.
		auto raw = state->decoder.get();
		state->decoder_thread.reset(new std::thread([raw]() {
			try {
				raw->startDecoding();
			} catch (const std::exception& e) {
				mprintf(("generic_anim: exception in movie decode thread: %s\n", e.what()));
			} catch (...) {
				mprintf(("generic_anim: unknown exception in movie decode thread\n"));
			}
		}));

		ga->movie.state = state;
	}
	else {
		bpp = 32;
		if(ga->use_hud_color)
			bpp = 8;
		bm_load_and_parse_eff(ga->filename, CF_TYPE_ANY, &ga->num_frames, &anim_fps, &ga->keyframe, 0);
		char *p = strrchr( ga->filename, '.' );
		if ( p )
			*p = 0;
		char frame_name[MAX_FILENAME_LEN];
		if (snprintf(frame_name, MAX_FILENAME_LEN, "%s_0000", ga->filename) >= MAX_FILENAME_LEN) {
			// Make sure the string is null terminated
			frame_name[MAX_FILENAME_LEN - 1] = '\0';
		}
		ga->bitmap_id = bm_load(frame_name);
		if(ga->bitmap_id < 0) {
			mprintf(("Cannot find first frame for eff streaming. eff Filename: %s\n", ga->filename));
			return -1;
		}
		if (snprintf(frame_name, MAX_FILENAME_LEN, "%s_0001", ga->filename) >= MAX_FILENAME_LEN) {
			// Make sure the string is null terminated
			frame_name[MAX_FILENAME_LEN - 1] = '\0';
		}
		ga->eff.next_frame = bm_load(frame_name);
		bm_get_info(ga->bitmap_id, &ga->width, &ga->height);
		ga->previous_frame = 0;
	}

	// keyframe info
	if (ga->type == BM_TYPE_ANI) {
		//we only care if there are 2 keyframes - first frame, other frame to jump to for ship/weapons
		//mainhall door anis hav every frame as keyframe, so we don't care
		//other anis only have the first frame
		if(ga->ani.animation->num_keys == 2) {
			int key1 = ga->ani.animation->keys[0].frame_num;
			int key2 = ga->ani.animation->keys[1].frame_num;

			if (key1 < 0 || key1 >= ga->num_frames) key1 = -1;
			if (key2 < 0 || key2 >= ga->num_frames) key2 = -1;

			// some retail anis have their keyframes reversed
			// and some have their keyframes out of bounds
			if (key1 >= 0 && key1 >= key2) {
				ga->keyframe = ga->ani.animation->keys[0].frame_num;
				ga->keyoffset = ga->ani.animation->keys[0].offset;
			}
			else if (key2 >= 0 && key2 >= key1) {
				ga->keyframe = ga->ani.animation->keys[1].frame_num;
				ga->keyoffset = ga->ani.animation->keys[1].offset;
			}
		}
	}

	ga->streaming = 1;

	if (ga->type == BM_TYPE_PNG) {
		ga->total_time = ga->png.anim->anim_time;
	}
	else if (ga->type == BM_TYPE_MOVIE) {
		// total_time was set from MovieProperties::duration above.
	}
	else {
		if (anim_fps == 0) {
			Error(LOCATION, "animation (%s) has invalid fps of zero, fix this!", ga->filename);
		}
		ga->total_time = ga->num_frames / (float) anim_fps;
	}
	ga->done_playing = 0;
	ga->anim_time = 0.0f;

	return 0;
}

int generic_bitmap_load(generic_bitmap *gb)
{
	if ( !VALID_FNAME(gb->filename) )
		return -1;

	gb->bitmap_id = bm_load(gb->filename);

	if (gb->bitmap_id < 0)
		return -1;

	return 0;
}

void generic_anim_unload(generic_anim *ga)
{
	if(ga->num_frames > 0) {
		if(ga->streaming) {
			if(ga->type == BM_TYPE_ANI) {
				free_anim_instance(ga->ani.instance);
				anim_free(ga->ani.animation);
			}
			if(ga->type == BM_TYPE_EFF) {
				if(ga->eff.next_frame >= 0)
					bm_release(ga->eff.next_frame);
				if(ga->bitmap_id >= 0)
					bm_release(ga->bitmap_id);
			}
			if(ga->type == BM_TYPE_PNG) {
				if(ga->bitmap_id >= 0)
					bm_release(ga->bitmap_id);
				if (ga->png.anim != nullptr) {
					delete ga->png.anim;
					ga->png.anim = nullptr;
				}
			}
			if(ga->type == BM_TYPE_MOVIE) {
				if (ga->movie.state != nullptr) {
					auto* state = ga->movie.state;
					// stopDecoder() closes the bounded queues, which unblocks any push
					// currently waiting on a full queue (push throws sync_queue_is_closed,
					// which Decoder::pushFrameData catches). The decode loop then sees
					// !isDecoding() and exits cleanly, so we can join.
					if (state->decoder) {
						state->decoder->stopDecoder();
					}
					if (state->decoder_thread && state->decoder_thread->joinable()) {
						state->decoder_thread->join();
					}
					if (ga->bitmap_id >= 0) {
						bm_release(ga->bitmap_id);
					}
					if (ga->buffer != nullptr) {
						vm_free(ga->buffer);
						ga->buffer = nullptr;
					}
					delete state;
					ga->movie.state = nullptr;
				}
			}
		}
		else {
			//trying to release the first frame will release ALL frames
			bm_release(ga->first_frame);
		}
		// Movies already released their bitmap + buffer above; skip the generic path
		// (the buffer is heap-owned and the bitmap was already released).
		if(ga->buffer && ga->type != BM_TYPE_MOVIE) {
			bm_release(ga->bitmap_id);
		}
	}
	generic_anim_init(ga, NULL);
}

//for timer debug, #define TIMER
void generic_render_eff_stream(generic_anim *ga)
{
	if(ga->current_frame == ga->previous_frame)
		return;
	ubyte bpp = 32;
	if(ga->use_hud_color)
		bpp = 8;
	#ifdef TIMER
		int start_time = timer_get_fixed_seconds();
	#endif

	#ifdef TIMER
		mprintf(("=========================\n"));
		mprintf(("frame: %d\n", ga->current_frame));
	#endif
		char frame_name[MAX_FILENAME_LEN];
		if (snprintf(frame_name, MAX_FILENAME_LEN, "%s_%.4d", ga->filename, ga->current_frame) >= MAX_FILENAME_LEN) {
			// Make sure the string is null terminated
			frame_name[MAX_FILENAME_LEN - 1] = '\0';
		}
		if(bm_reload(ga->eff.next_frame, frame_name) == ga->eff.next_frame)
		{
			bitmap* next_frame_bmp = bm_lock(ga->eff.next_frame, bpp, (bpp==8)?BMP_AABITMAP:BMP_TEX_NONCOMP, true);
			if(next_frame_bmp->data)
				gr_update_texture(ga->bitmap_id, bpp, (ubyte*)next_frame_bmp->data, ga->width, ga->height);
			bm_unlock(ga->eff.next_frame);
			bm_unload(ga->eff.next_frame, 0, true);
			if (ga->current_frame == ga->num_frames-1)
			{
				if (snprintf(frame_name, MAX_FILENAME_LEN, "%s_0001", ga->filename) >= MAX_FILENAME_LEN) {
					// Make sure the string is null terminated
					frame_name[MAX_FILENAME_LEN - 1] = '\0';
				}
				bm_reload(ga->eff.next_frame, frame_name);
			}
		}
	#ifdef TIMER
		mprintf(("end: %d\n", timer_get_fixed_seconds() - start_time));
		mprintf(("=========================\n"));
	#endif
}

void generic_render_ani_stream(generic_anim *ga)
{
	int i;
	int bpp = ANI_BPP_CHECK;
	if(ga->use_hud_color)
		bpp = 8;
	#ifdef TIMER
		int start_time = timer_get_fixed_seconds();
	#endif

	if(ga->current_frame == ga->previous_frame)
		return;

	#ifdef TIMER
		mprintf(("=========================\n"));
		mprintf(("frame: %d\n", ga->current_frame));
	#endif

	// if we're using bitmap polys
	BM_SELECT_TEX_FORMAT();
	if(ga->direction & GENERIC_ANIM_DIRECTION_BACKWARDS) {
		//grab the keyframe - every frame is a keyframe for ANI
		if(ga->ani.animation->flags & ANF_STREAMED) {
			ga->ani.instance->file_offset = ga->ani.animation->file_offset + ga->ani.animation->keys[ga->current_frame].offset;
		} else {
			ga->ani.instance->data = ga->ani.animation->data + ga->ani.animation->keys[ga->current_frame].offset;
		}
		if(ga->ani.animation->flags & ANF_STREAMED) {
			ga->ani.instance->file_offset = unpack_frame_from_file(ga->ani.instance, ga->buffer, ga->width * ga->height, (ga->ani.instance->xlate_pal) ? ga->ani.animation->palette_translation : NULL, (bpp==8)?1:0, bpp);
		}
		else {
			ga->ani.instance->data = unpack_frame(ga->ani.instance, ga->ani.instance->data, ga->buffer, ga->width * ga->height, (ga->ani.instance->xlate_pal) ? ga->ani.animation->palette_translation : NULL, (bpp==8)?1:0, bpp);
		}
	}
	else {
		//looping back
		if((ga->current_frame == 0) || (ga->current_frame < ga->previous_frame)) {
			//go back to keyframe if there is one
			if(ga->keyframe && (ga->current_frame > 0)) {
				if(ga->ani.animation->flags & ANF_STREAMED) {
					ga->ani.instance->file_offset = ga->ani.animation->file_offset + ga->keyoffset;
				} else {
					ga->ani.instance->data = ga->ani.animation->data + ga->keyoffset;
				}
				ga->previous_frame = ga->keyframe - 1;
			}
			//go back to the start
			else {
				ga->ani.instance->file_offset = ga->ani.animation->file_offset;
				ga->ani.instance->data = ga->ani.animation->data;
				ga->previous_frame = -1;
			}
		}
		#ifdef TIMER
				mprintf(("proc: %d\n", timer_get_fixed_seconds() - start_time));
				mprintf(("previous frame: %d\n", ga->previous_frame));
		#endif
		for(i = ga->previous_frame + 1; i <= ga->current_frame; i++) {
			if(ga->ani.animation->flags & ANF_STREAMED) {
				ga->ani.instance->file_offset = unpack_frame_from_file(ga->ani.instance, ga->buffer, ga->width * ga->height, (ga->ani.instance->xlate_pal) ? ga->ani.animation->palette_translation : NULL, (bpp==8)?1:0, bpp);
			}
			else {
				ga->ani.instance->data = unpack_frame(ga->ani.instance, ga->ani.instance->data, ga->buffer, ga->width * ga->height, (ga->ani.instance->xlate_pal) ? ga->ani.animation->palette_translation : NULL, (bpp==8)?1:0, bpp);
			}
		}
	}
	// always go back to screen format
	BM_SELECT_SCREEN_FORMAT();
	//we need to use this because performance is worse if we flush the gfx card buffer
	
	gr_update_texture(ga->bitmap_id, bpp, ga->buffer, ga->width, ga->height);

	//in case we want to check that the frame is actually changing
	//mprintf(("frame crc = %08X\n", cf_add_chksum_long(0, ga->buffer, ga->width * ga->height * (bpp >> 3))));
	ga->ani.instance->last_bitmap = ga->bitmap_id;

	#ifdef TIMER
		mprintf(("end: %d\n", timer_get_fixed_seconds() - start_time));
		mprintf(("=========================\n"));
	#endif
}

/*
 * @brief apng specific animation rendering
 *
 * @param [in] ga  pointer to generic_anim struct
 */
void generic_render_png_stream(generic_anim* ga)
{
	if(ga->current_frame == ga->previous_frame) {
		return;
	}

	try {
		if ((ga->direction & GENERIC_ANIM_DIRECTION_BACKWARDS) && (ga->previous_frame != -1)) {
			// mainhall door anims start backwards to ensure they stay shut
			// in that case (i.e. previous_frame is -1) we actually want to call
			// next_frame, in order to retrieve the 1st frame of the animation
			ga->png.anim->prev_frame();
		}
		else {
			ga->png.anim->next_frame();
		}
	}
	catch (const apng::ApngException& e) {
		nprintf(("apng", "Unable to get next/prev apng frame: %s\n", e.what()));
		return;
	}

	bm_lock(ga->bitmap_id, ga->png.anim->bpp, BMP_TEX_NONCOMP, true);  // lock in 32 bpp for png
	int bpp = ga->png.anim->bpp;
	if (ga->use_hud_color) {
		bpp = 8;
	}
	gr_update_texture(ga->bitmap_id, bpp, ga->buffer, ga->width, ga->height);  // this will convert to 8 bpp if required
	bm_unlock(ga->bitmap_id);
}

/*
 * @brief calculate current frame for fixed frame delay animation formats (ani & eff)
 *
 * @param [in] *ga  animation data
 * @param [in] frametime  how long this frame took
 */
void generic_anim_render_fixed_frame_delay(generic_anim* ga, float frametime, float alpha = 1.0f)
{
	float keytime = 0.0;

	if(ga->keyframe)
		keytime = (ga->total_time * ((float)ga->keyframe / (float)ga->num_frames));
	//don't mess with the frame time if we're paused
	if((ga->direction & GENERIC_ANIM_DIRECTION_PAUSED) == 0) {
		if(ga->direction & GENERIC_ANIM_DIRECTION_BACKWARDS) {
			//keep going forwards if we're in a keyframe loop
			if(ga->keyframe && (ga->anim_time >= keytime)) {
				ga->anim_time += frametime;
				if(ga->anim_time >= ga->total_time) {
					ga->anim_time = keytime - 0.001f;
					ga->done_playing = 0;
				}
			}
			else {
				//playing backwards
				ga->anim_time -= frametime;
				if((ga->direction & GENERIC_ANIM_DIRECTION_NOLOOP) && ga->anim_time <= 0.0) {
					ga->anim_time = 0;		//stop on first frame when playing in reverse
				}
				else {
					while(ga->anim_time <= 0.0)
						ga->anim_time += ga->total_time;	//make sure we're always positive, so we can go back to the end
				}
			}
		}
		else {
			ga->anim_time += frametime;
			if(ga->anim_time >= ga->total_time) {
				if(ga->direction & GENERIC_ANIM_DIRECTION_NOLOOP) {
					ga->anim_time = ga->total_time - 0.001f;		//stop on last frame when playing - if it's equal we jump to the first frame
				}
				if(!ga->done_playing){
					//we've played this at least once
					ga->done_playing = 1;
				}
			}
		}
	}
	if(ga->num_frames > 0)
	{
		ga->current_frame = 0;
		if(ga->done_playing && ga->keyframe) {
			ga->anim_time = fmod(ga->anim_time - keytime, ga->total_time - keytime) + keytime;
		}
		else {
			ga->anim_time = fmod(ga->anim_time, ga->total_time);
		}
		ga->current_frame += fl2i(ga->anim_time * ga->num_frames / ga->total_time);
		//sanity check
		CLAMP(ga->current_frame, 0, ga->num_frames - 1);
		if(ga->streaming) {
			//handle streaming - render one frame
			if(ga->type == BM_TYPE_ANI) {
				generic_render_ani_stream(ga);
			} else {
				generic_render_eff_stream(ga);
			}

			if (alpha == 1.0f) {
				gr_set_bitmap(ga->bitmap_id);
			} else {
				gr_set_bitmap(ga->bitmap_id, GR_ALPHABLEND_FILTER, GR_BITBLT_MODE_NORMAL, alpha);
			}
		}
		else {
			if (alpha == 1.0f) {
				gr_set_bitmap(ga->first_frame + ga->current_frame);
			} else {
				gr_set_bitmap(ga->first_frame + ga->current_frame, GR_ALPHABLEND_FILTER, GR_BITBLT_MODE_NORMAL, alpha);
			}
		}
	}
}

/*
 * @brief calculate current frame for variable frame delay animation formats (e.g. apng)
 *
 * @param [in] *ga  animation data
 * @param [in] frametime  how long this frame took
 * @param [in] alpha  transparency to draw frame with (0.0 - 1.0)
 *
 * @note
 * uses both time & frame counts to determine end state; so that if the anims playing
 * can't be processed fast enough, all frames will still play, rather than the end
 * frames being skipped
 */
void generic_anim_render_variable_frame_delay(generic_anim* ga, float frametime, float alpha)
{
	Assertion(ga->type == BM_TYPE_PNG, "only valid for apngs (currently); get a coder!");
	if (ga->keyframe != 0) {
		Warning(LOCATION, "apngs don't support keyframes");
		return;
	}

	// don't change the frame time if we're paused
	if((ga->direction & GENERIC_ANIM_DIRECTION_PAUSED) == 0) {
		if(ga->direction & GENERIC_ANIM_DIRECTION_BACKWARDS) {
			// playing backwards
			ga->anim_time -= frametime;
			if (ga->anim_time <= 0.0 && ga->png.anim->current_frame <= 0) {
				if(ga->direction & GENERIC_ANIM_DIRECTION_NOLOOP) {
					ga->anim_time = 0;  //stop on first frame when playing in reverse
				}
				else {
					// loop back to end
					ga->anim_time = ga->total_time;
					ga->png.previous_frame_time = ga->total_time;
					ga->png.anim->current_frame = ga->num_frames-1;
					ga->current_frame = ga->num_frames-1;
				}
			}
		}
		else {
			// playing forwards
			ga->anim_time += frametime;
			if(ga->anim_time >= ga->total_time && ga->png.anim->current_frame >= ga->png.anim->nframes) {
				if(ga->direction & GENERIC_ANIM_DIRECTION_NOLOOP) {
					ga->anim_time = ga->total_time;  // stop on last frame when playing
				}
				else {
					// loop back to start
					ga->anim_time = 0.0f;
					ga->png.previous_frame_time = 0.0f;
					ga->current_frame = 0;
					ga->png.anim->goto_start();
				}
				ga->done_playing = 1;
			}
		}
	}

	if (ga->num_frames > 0) {

		// just increment or decrement the frame by one
		// jumping forwards multiple frames will just exacerbate slowdowns as multiple frames
		// would need to be composed
		if (ga->direction & GENERIC_ANIM_DIRECTION_BACKWARDS) {
			if (ga->anim_time <= ga->png.previous_frame_time - ga->png.anim->frame.delay &&
					ga->png.anim->current_frame > 0) {
				ga->png.previous_frame_time -= ga->png.anim->frame.delay;
				ga->current_frame--;
			}
		}
		else {
			if (ga->anim_time >= ga->png.previous_frame_time + ga->png.anim->frame.delay &&
					ga->png.anim->current_frame < ga->png.anim->nframes) {
				ga->png.previous_frame_time += ga->png.anim->frame.delay;
				ga->current_frame++;
			}
		}

		// verbose debug; but quite useful
		nprintf(("apng", "apng generic render timings/frames: %04f %04f %04f %04f | %03i %03i %03i\n",
				frametime, ga->anim_time, ga->png.anim->frame.delay, ga->png.previous_frame_time,
				ga->previous_frame, ga->current_frame, ga->png.anim->current_frame));

		Assertion(ga->streaming != 0, "non-streaming apngs not implemented yet");
		// note: generic anims are not currently ever non-streaming in FSO
		// I'm not even sure that the existing ani/eff code would allow non-streaming generic anims
		generic_render_png_stream(ga);
		gr_set_bitmap(ga->bitmap_id, GR_ALPHABLEND_FILTER, GR_BITBLT_MODE_NORMAL, alpha);
	}
}


// Copy the latest decoded video frame (plane 0, BGRA) into the staging buffer
// and push it to the bitmap. Respects sws stride padding.
static void generic_render_movie_upload(generic_anim* ga, cutscene::VideoFrame* frame)
{
	const auto plane_size = frame->getPlaneSize(0);
	const auto* src = reinterpret_cast<const ubyte*>(frame->getPlaneData(0));
	const size_t row_bytes = static_cast<size_t>(ga->width) * 4;
	const size_t src_stride = plane_size.stride;
	if (src_stride == row_bytes) {
		memcpy(ga->buffer, src, row_bytes * static_cast<size_t>(ga->height));
	} else {
		ubyte* dst = ga->buffer;
		for (int row = 0; row < ga->height; ++row) {
			memcpy(dst, src, row_bytes);
			dst += row_bytes;
			src += src_stride;
		}
	}
	gr_update_texture(ga->bitmap_id, 32, ga->buffer, ga->width, ga->height);
}

/*
 * @brief render path for streaming movie files (mp4/ogg)
 *
 * Movies decode on a background thread; here we pull due frames from the queue
 * and blit the latest one into the generic_anim bitmap. Backwards play and
 * keyframes are not supported — they fall back to forward play with a
 * one-time warning per anim.
 */
void generic_anim_render_movie(generic_anim* ga, float frametime, float alpha)
{
	auto* state = ga->movie.state;
	Assertion(state != nullptr, "movie generic_anim with null state!");

	// Backwards / keyframe requests are not supported for movies. Warn once and proceed forward.
	if ((ga->direction & GENERIC_ANIM_DIRECTION_BACKWARDS) || ga->keyframe != 0) {
		if (!state->warned_backwards) {
			Warning(LOCATION, "Movie generic_anim '%s' does not support reverse playback or keyframes; playing forward.", ga->filename);
			state->warned_backwards = true;
		}
	}

	const bool paused = (ga->direction & GENERIC_ANIM_DIRECTION_PAUSED) != 0;
	const bool noloop = (ga->direction & GENERIC_ANIM_DIRECTION_NOLOOP) != 0;

	if (!paused && !state->finished) {
		state->playback_time += frametime;
		ga->anim_time += frametime;
		if (ga->total_time > 0.0f && ga->anim_time > ga->total_time) {
			ga->anim_time = noloop ? ga->total_time : fmodf(ga->anim_time, ga->total_time);
		}
	}

	// Prime the pipeline by pulling a frame if we don't have one queued yet.
	if (!state->next_frame) {
		state->decoder->tryPopVideoFrame(state->next_frame);
	}

	// Advance through any frames whose timestamp has already passed. Track whether
	// we actually swapped in a new frame this call so we can skip the (expensive)
	// texture re-upload when the displayed frame hasn't changed. ga->buffer always
	// holds the latest frame, so a later page-in still reads correct pixels.
	bool advanced = false;
	while (state->next_frame && state->playback_time >= state->next_frame->frameTime) {
		// Detect loop wrap: a frame whose timestamp is earlier than the currently
		// displayed one means the decoder has seeked back to the start. Mirror
		// player.cpp::processVideoData. For NOLOOP, stop here on the last frame.
		if (state->current_frame && state->next_frame->frameTime < state->current_frame->frameTime) {
			if (noloop) {
				if (!state->finished) {
					state->finished = true;
					ga->done_playing = 1;
					ga->anim_time = ga->total_time;
					state->decoder->stopDecoder();
				}
				// Discard the post-wrap frame; we want to hold the last frame instead.
				state->next_frame.reset();
				break;
			}
			state->playback_time = state->next_frame->frameTime;
			ga->anim_time = 0.0f;
			ga->done_playing = 1;	// completed one cycle
		}
		state->current_frame = std::move(state->next_frame);
		advanced = true;
		if (!state->decoder->tryPopVideoFrame(state->next_frame)) {
			state->next_frame.reset();
			break;
		}
	}

	if (state->current_frame && advanced) {
		generic_render_movie_upload(ga, state->current_frame.get());
	}

	gr_set_bitmap(ga->bitmap_id, GR_ALPHABLEND_FILTER, GR_BITBLT_MODE_NORMAL, alpha);
}

/*
 * @brief render animations
 *
 * @param [in] *ga  animation data
 * @param [in] frametime  how long this frame took
 * @param [in] x  2D screen x co-ordinate to render at
 * @param [in] y  2D screen y co-ordinate to render at
 * @param [in] menu select if this is rendered in menu screen, or fullscreen
 */
void generic_anim_render(generic_anim *ga, float frametime, int x, int y, bool menu, const generic_extras *ge, float scale_factor)
{
	if ((ge != nullptr) && (ga->use_hud_color == true)) {
		Warning(LOCATION, "Monochrome generic anims can't use extra info (yet)");
		return;
	}

	float a = 1.0f;
	if (ge != nullptr) {
		a = ge->alpha;
	}
	if (ga->type == BM_TYPE_PNG) {
		generic_anim_render_variable_frame_delay(ga, frametime, a);
	}
	else if (ga->type == BM_TYPE_MOVIE) {
		generic_anim_render_movie(ga, frametime, a);
	}
	else {
		generic_anim_render_fixed_frame_delay(ga, frametime, a);
	}

	if(ga->num_frames > 0) {
		ga->previous_frame = ga->current_frame;

		if(ga->use_hud_color) {
			gr_aabitmap(x, y, (menu ? GR_RESIZE_MENU : GR_RESIZE_FULL), false, scale_factor);
		}
		else {
			if (ge == nullptr) {
				gr_bitmap(x, y, (menu ? GR_RESIZE_MENU : GR_RESIZE_FULL), false, scale_factor);
			}
			else if (ge->draw == true) {
				// currently only for lua streaminganim objects
				// and don't draw them unless requested...
				gr_bitmap_uv(x, y, ge->width, ge->height, ge->u0, ge->v0, ge->u1, ge->v1, ge->resize_mode);
			}
		}
	}
}

void generic_anim_bitmap_set(generic_anim* ga, float frametime, const generic_extras* ge)
{
	if ((ge != nullptr) && (ga->use_hud_color == true)) {
		Warning(LOCATION, "Monochrome generic anims can't use extra info (yet)");
		return;
	}

	float a = 1.0f;
	if (ge != nullptr) {
		a = ge->alpha;
	}
	if (ga->type == BM_TYPE_PNG) {
		generic_anim_render_variable_frame_delay(ga, frametime, a);
	}
	else if (ga->type == BM_TYPE_MOVIE) {
		generic_anim_render_movie(ga, frametime, a);
	}
	else {
		generic_anim_render_fixed_frame_delay(ga, frametime, a);
	}

	if (ga->num_frames > 0)
		ga->previous_frame = ga->current_frame;
}

/*
 * @brief reset an animation back to the start
 *
 * @param [in] *ga  animation data
 */
void generic_anim_reset(generic_anim *ga) {
	ga->anim_time = 0.0f;
	ga->current_frame = 0;
	if (ga->type == BM_TYPE_PNG) {
		ga->png.previous_frame_time = 0.0f;
		ga->png.anim->goto_start();
	}
	else if (ga->type == BM_TYPE_MOVIE) {
		// Intentionally a no-op for the decoder. Movies are for continuous/looping
		// playback: the decoder loops on its own, so a looping movie needs no reset.
		// We deliberately do NOT rebase state->playback_time here — doing so desyncs
		// the local clock from the frames already queued and freezes the picture for
		// seconds until it catches back up. A true seek isn't safe from this thread
		// (the decoder owns av_seek_frame inside its own loop), so the timer-driven
		// "play once then replay" pattern (e.g. intermittent main hall anims) is not
		// supported for movies; such a slot should use an ANI/EFF/APNG, or accept
		// that a looping movie simply keeps looping rather than restarting on cue.
	}
}
