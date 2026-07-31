/*
 * Copyright (C) Freespace Open 2013.  All rights reserved.
 *
 * All source code herein is the property of Freespace Open. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 */

#ifndef _MISSIONCHECKPOINT_H
#define _MISSIONCHECKPOINT_H

#include "globalincs/pstypes.h"
#include "globalincs/vmallocator.h"
#include "math/vecmat.h"

/*
 * Mission checkpoints: save the state of a mission in progress and restore it later.
 *
 * A checkpoint is captured live (mission_checkpoint_store) and restored by reloading the
 * mission from scratch and bashing the saved state on top of the freshly created objects
 * (mission_checkpoint_apply, called from game_post_level_init).  Reloading rather than
 * restoring in place means the engine is always in a known-clean state; it is the same
 * approach the red alert code takes.
 *
 * Everything that crosses the file is keyed by name -- ship names, class names, subsystem
 * names -- never by a runtime index, so a checkpoint survives table changes and engine
 * updates.  See checkpointfields.h for how the per-struct field lists work.
 */

class object;
class ship;
class ship_subsys;
class ship_weapon;
struct wing;

namespace checkpoint {

// Options for a load, chosen by the mission designer on the load-checkpoint SEXP.
enum class LoadFlags : uint32_t {
	None = 0,

	// Leave the player's ship class and weapon banks as the fresh mission load produced them
	// instead of restoring what the checkpoint recorded.  Lets a player retry with a
	// different fit.
	KeepPlayerLoadout = 1 << 0,

	// As above, but for the rest of the player's wing.
	KeepWingLoadout = 1 << 1,

	// Go through briefing and ship/weapon select before resuming, rather than dropping
	// straight back into the mission.
	ReopenLoadout = 1 << 2,

	// Apply whatever the checkpoint contains even if the mission file has changed since it
	// was written.  Off by default because a mission edit invalidates SEXP node indices.
	IgnoreFingerprint = 1 << 3,
};

inline LoadFlags operator|(LoadFlags a, LoadFlags b)
{
	return static_cast<LoadFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline LoadFlags& operator|=(LoadFlags& a, LoadFlags b)
{
	a = a | b;
	return a;
}

inline bool any(LoadFlags value, LoadFlags test)
{
	return (static_cast<uint32_t>(value) & static_cast<uint32_t>(test)) != 0;
}

// ------------------------------------------------------------------
// Captured state
// ------------------------------------------------------------------

// One weapon bank.  weapon_class is empty when the bank holds nothing.
struct weapon_bank {
	SCP_string weapon_class;
	int ammo = 0;
	int start_ammo = 0;
	int capacity = 0;
	int next_slot = 0;
	int next_fire_stamp = 0;
	int last_fire_stamp = 0;
	int rearm_time = 0;
	int burst_counter = 0;
	int burst_seed = 0;
};

struct weapon_state {
	SCP_vector<weapon_bank> primary_banks;
	SCP_vector<weapon_bank> secondary_banks;
	SCP_string tertiary_class;

	// Named flags from ship_weapon::flags; see Weapon_flag_names in missioncheckpoint.cpp.
	SCP_vector<SCP_string> flags;

	// Scalars from CKPT_WEAPONS_INTS.
	SCP_map<SCP_string, int> scalars;
};

// A subsystem is identified by its model subobject name.  Ships routinely carry several
// subsystems with the same name (engine01, engine01, ...), so an ordinal disambiguates them
// within that name.  Matching by name rather than by list position -- which is what the red
// alert code does -- means the restore survives a model whose subsystem list has changed.
struct subsystem_state {
	SCP_string name;
	int ordinal = 0;

	SCP_string sub_name;         // WMC's per-instance name override, if any
	SCP_string cargo_title;
	SCP_string turret_target;    // ship name this turret was firing on, if any

	SCP_vector<SCP_string> flags;
	SCP_map<SCP_string, float> floats;
	SCP_map<SCP_string, int> ints;

	// Turrets have their own weapon banks.
	weapon_state weapons;
	bool has_weapons = false;
};

// What had become of a ship at the moment the checkpoint was taken.  Mirrors ShipStatus, but
// is written by name so the file does not depend on the enum's ordering.
enum class ShipDisposition {
	Present,      // in the mission, alive
	NotYetHere,   // still on the arrival list
	Destroyed,
	Departed,
	Vanished,
};

struct ship_state {
	SCP_string name;
	ShipDisposition disposition = ShipDisposition::Present;

	// --- only meaningful when disposition == Present ---

	SCP_string ship_class;
	SCP_string team;
	SCP_string display_name;
	SCP_string wing_name;
	SCP_string cargo_title;
	SCP_string countermeasure_class;

	vec3d pos = vmd_zero_vector;
	matrix orient = vmd_identity_matrix;

	float hull = 0.0f;
	float max_hull = 0.0f;
	// Sized to match object::shield_quadrant, which is not fixed at four for every model.
	SCP_vector<float> shield_quadrants;

	char cargo1 = 0;

	SCP_vector<SCP_string> flags;
	SCP_vector<SCP_string> object_flags;
	SCP_map<SCP_string, float> floats;
	SCP_map<SCP_string, int> ints;
	SCP_map<SCP_string, float> physics_floats;
	SCP_map<SCP_string, vec3d> physics_vecs;

	SCP_vector<subsystem_state> subsystems;
	weapon_state weapons;

	// --- only meaningful when the ship had already left ---
	fix exit_time = 0;
};

struct wing_state {
	SCP_string name;
	SCP_map<SCP_string, int> ints;
	SCP_vector<SCP_string> ship_names;   // wing::ship_index, resolved to names
	fix time_gone = 0;
	int wave_delay_timestamp = 0;
};

struct variable_state {
	SCP_string name;
	bool is_number = false;
	SCP_string value;
};

struct scoring_state {
	SCP_map<SCP_string, int> ints;
	// Per-ship-class kills, keyed by class name so a table change cannot misattribute them.
	SCP_map<SCP_string, int> class_kills;
};

// What an event had got up to.  Matched back by name; everything the mission file defines about
// the event -- its formula, interval, score, objective text -- is reproduced by the mission load
// and is deliberately absent.  repeat_count and trigger_count are here because they count *down*
// as the event fires.
struct event_state {
	SCP_string name;

	int result = 0;
	int previous_result = 0;
	int repeat_count = 0;
	int trigger_count = 0;
	int count = 0;
	int mission_log_flags = 0;

	// Only the bits that change while the mission runs; see Event_flag_table in
	// missioncheckpoint.cpp.  The parse-time bits are left as the mission load set them.
	SCP_vector<SCP_string> flags;

	int timestamp = 0;
	int satisfied_time = 0;
	int born_on_date = 0;

	SCP_vector<SCP_string> log_buffer;
	SCP_vector<SCP_string> log_variable_buffer;
	SCP_vector<SCP_string> log_container_buffer;
	SCP_vector<SCP_string> log_argument_buffer;
	SCP_vector<SCP_string> backup_log_buffer;
};

// Goals only really have one piece of runtime state.
struct goal_state {
	SCP_string name;
	int satisfied = 0;
};

// A SEXP node whose evaluation state has stopped being the default.
//
// Only nodes that have reached a *sticky* state are worth carrying: SEXP_KNOWN_TRUE,
// SEXP_KNOWN_FALSE and SEXP_NAN_FOREVER never change again, and they are what stops an event
// re-triggering something the ship restore has already accounted for -- an arrival that has
// happened, a ship that is known destroyed.  A plain SEXP_TRUE/SEXP_FALSE is recomputed every
// frame anyway, so storing it would just bloat the file.
//
// Identified by node index, which is only meaningful for an identical parse of an identical
// mission file.  That is exactly what the fingerprint check guarantees.
struct sexp_node_state {
	int index = 0;
	int value = 0;
	int flags = 0;
};

// Containers hold runtime data the same way SEXP variables do.
struct container_state {
	SCP_string name;
	SCP_vector<SCP_string> list_data;
	// Flattened key/value pairs, in the order they come out of the map.
	SCP_vector<SCP_string> map_keys;
	SCP_vector<SCP_string> map_values;
};

// A ship that had not arrived yet, whose parse object had been changed since the mission loaded.
//
// SEXPs can rewrite a ship long before it shows up -- change-ship-class, change-iff, hull and
// shield bashing, flag changes -- and a fresh mission load puts all of that back the way the file
// had it.  Only the fields a SEXP can actually reach are captured; the rest is reproduced by the
// parse.  Arrival and departure *anchors* are deliberately absent: they are anchor_t, which needs
// name resolution of its own, and set-arrival-info on a ship that has not arrived is rare enough
// to be worth leaving until that resolution exists.
struct parse_object_state {
	SCP_string name;
	SCP_string ship_class;
	SCP_string team;

	int initial_hull = 100;
	int initial_shields = 100;
	int arrival_distance = 0;
	int arrival_delay = 0;
	int departure_delay = 0;
	int escort_priority = 0;
	int respawn_priority = 0;
	int alt_type_index = -1;
	int callsign_index = -1;
	char cargo1 = 0;

	SCP_vector<SCP_string> flags;
};

// A piece of hull debris.
//
// Only hull debris is captured.  Small debris expires in seconds and is pure decoration, but a
// large hull chunk has lifeleft == -1 -- it stays for the rest of the mission, it collides, and
// it can be targeted and shot.  That makes a capital ship wreck part of the battlefield rather
// than an effect, and a restore that loses it is immediately obvious.
struct debris_state {
	SCP_string ship_class;    // what it broke off
	SCP_string submodel;      // by name, so a re-exported pof cannot scramble it
	SCP_string team;
	SCP_string species;
	SCP_string damage_type;

	vec3d pos = vmd_zero_vector;
	matrix orient = vmd_identity_matrix;
	vec3d velocity = vmd_zero_vector;
	vec3d rotational_velocity = vmd_zero_vector;

	float hull_strength = 0.0f;
	float max_hull = 0.0f;
	float lifeleft = -1.0f;
	float damage_mult = 1.0f;
	int parent_alt_name = -1;
	bool do_not_expire = false;
};

// A mission log entry, reproduced whole.  The timestamp here is mission time, not an engine
// timestamp, so it is restored as-is rather than shifted.
struct log_entry_state {
	int type = 0;
	int flags = 0;
	fix timestamp = 0;
	int timer_padding = 0;
	int index = 0;
	int primary_team = -1;
	int secondary_team = -1;
	SCP_string pname;
	SCP_string sname;
	SCP_string pname_display;
	SCP_string sname_display;
};

struct checkpoint_data {
	// --- identity and validity ---
	int version = 0;
	SCP_string slot;
	SCP_string mission_filename;
	SCP_string mission_modified;   // The_mission.modified
	uint mission_fingerprint = 0;  // see checkpoint_mission_fingerprint()
	SCP_string campaign;
	SCP_string pilot;
	SCP_string mod_title;

	// --- clock ---
	// Missiontime is a fix; microseconds is what the timestamp clock is actually rebased
	// with, and is stored separately so we do not lose precision through the fix conversion.
	fix mission_time = 0;
	std::uint64_t mission_time_microseconds = 0;
	int hud_timer_padding = 0;

	// timestamp() as it read when the checkpoint was taken.  Every stamp in the file is an
	// absolute value in that clock, and the clock does not restart per mission, so this is what
	// the restore needs in order to shift them into the run that is loading them.
	int saved_timestamp_ms = 0;

	// --- state ---
	SCP_vector<ship_state> ships;
	SCP_vector<wing_state> wings;
	SCP_vector<variable_state> variables;
	scoring_state scoring;

	// --- mission logic ---
	SCP_vector<event_state> events;
	SCP_vector<goal_state> goals;
	SCP_vector<log_entry_state> log_entries;
	SCP_vector<sexp_node_state> sexp_nodes;
	SCP_vector<container_state> containers;
	SCP_vector<debris_state> debris;
	SCP_vector<parse_object_state> parse_objects;
	int goal_timestamp = 0;

	bool loaded = false;
};

} // namespace checkpoint

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

// Are checkpoints usable in the mission that is loaded, played the way it is being played?
// False in multiplayer, and false when the mission carries the flag that turns checkpoints off
// for the mode it is being flown in -- a designer may want them in the simulator but not in the
// campaign, or the other way round.  Every entry point checks this, so a mission with them
// switched off behaves as though the operators were never called.
bool mission_checkpoint_allowed();

// Capture the current mission state and write it to the named slot.  Returns false (and logs)
// if the file could not be written.  Safe to call from inside SEXP evaluation.
bool mission_checkpoint_store(const SCP_string& slot);

// Does a usable checkpoint exist for this pilot, campaign, mission and slot?  A checkpoint
// whose mission fingerprint no longer matches counts as absent.
bool mission_checkpoint_exists(const SCP_string& slot);

// Remove a checkpoint.  Silently does nothing if there was none.
void mission_checkpoint_delete(const SCP_string& slot);

// Request a load.  This does NOT reload the mission itself -- doing that while SEXP
// evaluation is on the stack would tear the level down underneath the caller.  It records the
// request; mission_checkpoint_process_pending_load() acts on it at the end of the frame.
void mission_checkpoint_request_load(const SCP_string& slot, checkpoint::LoadFlags flags);

// Is a load queued?
bool mission_checkpoint_load_pending();

// Called once per frame at the end of the gameplay loop.  If a load is queued, posts the
// mission restart that will eventually land in mission_checkpoint_apply().
void mission_checkpoint_process_pending_load();

// Offer the player a checkpoint on the way into a mission, if one is worth offering.  Does
// nothing when a load is already being serviced (so the mid-mission SEXP path does not prompt
// a second time on the way back in), when the mission carries the no-resume-prompt flag, or
// when there is no usable checkpoint.  Answering yes queues the restore for
// mission_checkpoint_apply(), which must be called immediately afterwards.
void mission_checkpoint_maybe_offer_resume();

// Apply a checkpoint if one is being restored; otherwise do nothing.
//
// Called from the GS_EVENT_ENTER_GAME handler, NOT from game_post_level_init().  It has to run
// after commit_pressed() -> create_wings() and after wss_direct_restore_loadout(), both of
// which rewrite the starting wings' ship classes and weapons and would otherwise overwrite
// whatever was restored.  See the comment at the call site in freespace.cpp.
void mission_checkpoint_apply();

// Discard any queued or in-flight restore.  Called when leaving a mission by any route other
// than a checkpoint load, so a stale request cannot leak into the next mission.
void mission_checkpoint_clear_pending();

// Throw away this mission's checkpoints if it is flagged to do that once completed.  Called when
// the player finishes the mission, so that replaying it later starts clean rather than offering
// a checkpoint from the previous run.
void mission_checkpoint_mission_complete();

// Parse a designer-supplied flag name into a LoadFlags bit.  Returns false if unrecognised.
bool mission_checkpoint_parse_load_flag(const char* name, checkpoint::LoadFlags& out);

// Every name mission_checkpoint_parse_load_flag() accepts, in table order.  The editor builds
// its dropdown from this so the list it offers and the list the parser takes cannot disagree.
const SCP_vector<SCP_string>& mission_checkpoint_get_load_flag_names();

// Shift a timestamp saved in a checkpoint's clock into one that is `delta` milliseconds ahead of
// it, preserving how far in the future or past the stamp was.  Exposed only so the sentinel and
// clamping rules can be unit tested; the restore calls it with the delta it worked out from the
// checkpoint's own clock.  See the note above translate_stamp() in missioncheckpoint.cpp.
int mission_checkpoint_translate_stamp(int saved, int delta);

// Drop everything cached about the mission that was loaded.  Called from game_level_init(), so a
// mission edited and reloaded within one run of the game is looked at afresh rather than still
// being judged against the copy that was on disk the first time.  Does NOT touch a pending load:
// that has to survive the reload it asked for.
void mission_checkpoint_level_init();

#endif // _MISSIONCHECKPOINT_H
