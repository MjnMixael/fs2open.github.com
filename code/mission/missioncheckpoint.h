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
	SCP_string cargo;            // subsys_cargo_name, by name -- see the note on ship_state::cargo
	SCP_string turret_target;    // ship name this turret was firing on, if any
	bool cargo_no_deplete = false;

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
	SCP_string persona;          // Personas is built from messages.tbl, so the index is a table index

	// ship::cargo1 packs an index into Cargo_names with the "do not deplete" bit.  set-cargo
	// appends to Cargo_names at runtime, so an index saved in one run can point past the end of
	// the freshly parsed list in the next; the name is stored instead and re-resolved on apply.
	SCP_string cargo;
	bool cargo_no_deplete = false;

	vec3d pos = vmd_zero_vector;
	matrix orient = vmd_identity_matrix;

	float hull = 0.0f;
	float max_hull = 0.0f;
	// Sized to match object::shield_quadrant, which is not fixed at four for every model.
	SCP_vector<float> shield_quadrants;

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

// One AI order.
//
// ai_goal is already mostly name-based -- target_name, docker.name, dockee.name -- which is why an
// order survives this trip almost unchanged.  The two things that need care are the mode and type
// enums, which go out by name so that inserting a value into either enum cannot silently turn an
// attack order into a dock order, and the dockpoint unions, which hold either a name or an index
// depending on two of the flags.  The names are always stored and the two index-valid flags are
// dropped on the way out, so the goal re-resolves its dockpoints the next time it is evaluated.
struct ai_goal_state {
	SCP_string mode;          // see Ai_goal_mode_table in missioncheckpoint.cpp
	SCP_string type;          // see Ai_goal_type_table
	SCP_vector<SCP_string> flags;

	SCP_string target_name;
	SCP_string docker_point;
	SCP_string dockee_point;

	int signature = 0;
	int submode = -1;
	int priority = 0;
	fix time = 0;             // Missiontime the order was issued, not an engine timestamp
	int wp_list_index = -1;
	int int_data = 0;
	float float_data = 0.0f;
};

// What a ship's AI was doing.  Keyed by ship name; the file never mentions ai_index.
//
// Without this a restored mission puts every ship back where it was and then has it forget its
// orders: the wing told to guard the transport goes back to its mission-file goals, the bomber
// ordered onto a subsystem picks a new target, the support ship forgets who called it.
struct ai_state {
	SCP_string ship;

	SCP_string ai_class;      // an index into Ai_classes, which comes from ai.tbl, so by name

	SCP_vector<SCP_string> flags;
	SCP_vector<SCP_string> override_flags;

	SCP_map<SCP_string, int> ints;
	SCP_map<SCP_string, float> floats;
	SCP_map<SCP_string, vec3d> vecs;
	SCP_map<SCP_string, float> override_floats;

	// Object references.  Every one of these is an objnum at runtime and a ship name here; the
	// paired signatures are not stored, because they are recomputed from whatever the name
	// resolves to.  ignore is either a ship name or "wing:<name>", matching the dual encoding
	// ignore_objnum uses at runtime.
	SCP_string target;
	SCP_string previous_target;
	SCP_string goal_ship;
	SCP_string guard_ship;
	SCP_string ignore;
	SCP_string support_ship;
	SCP_string hitter;
	SCP_string attacker;
	SCP_string artillery_target;
	SCP_vector<SCP_string> ignore_new;

	// Targeted subsystems, by owning ship plus the same name-and-ordinal key the subsystem
	// restore uses.
	SCP_string targeted_subsys_ship;
	SCP_string targeted_subsys;
	int targeted_subsys_ordinal = 0;
	SCP_string last_subsys_target_ship;
	SCP_string last_subsys_target;
	int last_subsys_target_ordinal = 0;

	SCP_vector<ai_goal_state> goals;
};

struct wing_state {
	SCP_string name;
	SCP_map<SCP_string, int> ints;
	SCP_vector<SCP_string> ship_names;   // wing::ship_index, resolved to names
	fix time_gone = 0;
	int wave_delay_timestamp = 0;

	// Named flags from wing::flags; see Wing_flag_table in missioncheckpoint.cpp.  Gone and
	// Departing are the ones that matter most: directives keyed on a wing being wiped out read
	// them, and replaying the waves does not reproduce either.
	SCP_vector<SCP_string> flags;

	// Carried alongside the Has_display_name flag, since restoring one without the other would
	// leave the wing claiming a display name it does not have.
	SCP_string display_name;

	// set-arrival-info and set-departure-info rewrite all of this on a wing exactly as they do on
	// a ship that has not arrived, so it needs the same treatment parse_object_state gets: anchors
	// by name, everything else verbatim.  arrival_distance and the two delays are already in the
	// CKPT_WING_INTS and CKPT_WING_STAMPS lists.
	SCP_string arrival_anchor;
	SCP_string departure_anchor;
	int arrival_location = 0;
	int departure_location = 0;
	int arrival_path_mask = 0;
	int departure_path_mask = 0;

	// A wing carries its own ai_goals[], handed to each ship as it arrives, and a SEXP can rewrite
	// them mid-mission.  Same struct as the per-ship orders.
	SCP_vector<ai_goal_state> goals;
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
// SEXP_NUM_EVAL is sticky too, and less obviously so: `rand` rolls once and then parks both the
// marker and the rolled number on the node (rand_sexp(), sexp.cpp), so it never rolls again.  That
// is why the text comes along -- for those nodes the text *is* the value, and without it a
// restored mission re-rolls every random delay the mission had already settled.
//
// Identified by node index, which is only meaningful for an identical parse of an identical
// mission file.  That is exactly what the fingerprint check guarantees.
struct sexp_node_state {
	int index = 0;
	int value = 0;
	int flags = 0;
	SCP_string text;   // only stored when the node's text carries state, i.e. SEXP_NUM_EVAL
};

// Containers hold runtime data the same way SEXP variables do.
struct container_state {
	SCP_string name;
	SCP_vector<SCP_string> list_data;
	// Flattened key/value pairs, in the order they come out of the map.
	SCP_vector<SCP_string> map_keys;
	SCP_vector<SCP_string> map_values;
};

// One hotkey set's contents.  Ships by name; how_added distinguishes a mission-file default from
// something the player put there, which matters because the two are shown differently and the
// player's choices are the half a fresh mission load cannot reproduce.
struct hotkey_state {
	int set = 0;
	SCP_vector<SCP_string> ship_names;
	SCP_vector<int> how_added;
};

// A ship that had not arrived yet, whose parse object had been changed since the mission loaded.
//
// SEXPs can rewrite a ship long before it shows up -- change-ship-class, change-iff, hull and
// shield bashing, flag changes -- and a fresh mission load puts all of that back the way the file
// had it.  Only the fields a SEXP can actually reach are captured; the rest is reproduced by the
// parse.
struct parse_object_state {
	SCP_string name;
	SCP_string ship_class;
	SCP_string team;

	// Anchors go by name: either a ship, or one of the "<any hostile>" specials.
	SCP_string arrival_anchor;
	SCP_string departure_anchor;
	int arrival_location = 0;
	int departure_location = 0;
	int arrival_path_mask = 0;
	int departure_path_mask = 0;

	int initial_hull = 100;
	int initial_shields = 100;
	int arrival_distance = 0;
	int arrival_delay = 0;
	int departure_delay = 0;
	int escort_priority = 0;
	int respawn_priority = 0;
	// Mission_alt_types and Mission_callsigns are built solely by the mission parse and nothing
	// extends them at runtime, so these indices mean the same thing in any run of the same
	// mission file -- which the fingerprint check guarantees.  Cargo_names is not like that; see
	// the note on ship_state::cargo.
	int alt_type_index = -1;
	int callsign_index = -1;
	SCP_string cargo;
	bool cargo_no_deplete = false;

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
	int parent_alt_name = -1;   // index into Mission_alt_types, which only the mission parse builds

	bool do_not_expire = false;
};

// One animation that was playing on a ship's model instance.
//
// The id is a content hash of the animation's name combined with its ship class name, which is why
// multiplayer can send it over the wire, and which makes it a name rather than an index for the
// purposes of the rule above.  An id the current tables no longer define simply does not come
// back.
//
// Without this a restored mission snaps every model to its default pose: opened fighter bays close,
// deployed turrets retract, a submodel caught mid-swing jumps back to the start.  Note that the
// turret rotation rates and timers already survive with the subsystems, so before this the two
// disagreed.
struct animation_state {
	unsigned int id = 0;

	int state = 0;             // ModelAnimationState
	int direction = 0;         // ModelAnimationDirection
	float time = 0.0f;         // how far into the animation it had got
	float speed = 1.0f;

	SCP_vector<SCP_string> instance_flags;
};

// The animations playing on one ship, keyed by ship name rather than by model instance, since a
// model instance number is a runtime index.
struct ship_animation_state {
	SCP_string ship;
	SCP_vector<animation_state> animations;
};

// One sun or background bitmap currently hanging in the sky.
//
// These are the live instances, not the background definitions the mission file carries:
// add-background-bitmap and remove-sun-bitmap add to and take from the live lists, so a mission
// that dresses its own sky loses all of it on a reload.  By name, with the geometry that goes with
// it -- the same four numbers and three angles the mission file stores.
struct starfield_entry_state {
	SCP_string name;
	bool is_sun = false;

	float scale_x = 1.0f;
	float scale_y = 1.0f;
	int div_x = 1;
	int div_y = 1;
	angles ang = {0.0f, 0.0f, 0.0f};
};

// One weapon in the support ship rearm stockpile.  The pool is a [team][weapon class] array of
// counts, spent as the mission runs; only the non-empty entries are written, and the weapon goes by
// name because the index is a position in weapons.tbl.
struct rearm_pool_entry {
	int team = 0;
	SCP_string weapon_class;
	int count = 0;
};

// One autopilot navpoint.
//
// A whole family of operators adds, deletes, hides, restricts and marks these visited while the
// mission runs, so the array is runtime state rather than mission-file state.  target_index is an
// object index unless the nav is bound to a waypoint, so it goes out as a ship name, or as a
// waypoint list name plus the node within it.
struct navpoint_state {
	SCP_string name;
	int flags = 0;

	SCP_string target;     // ship name, or waypoint list name when the nav is a waypoint nav
	int waypoint_num = -1;

	int normal_color[3] = {0, 0, 0};
	int visited_color[3] = {0, 0, 0};
};

// One jump node, as the mission has left it.
//
// Identified by its position in Jump_nodes rather than by name, because set-jumpnode-name renames
// the thing that would otherwise be the key.  That list is built solely by the mission parse, which
// the fingerprint check makes identical across runs -- the same reasoning that lets alt_type_index
// stay an index.
struct jump_node_state {
	int index = 0;

	SCP_string name;
	SCP_string display_name;
	SCP_string model;      // filename; empty means the default model
	bool hidden = false;
	bool colored = false;
	int color[4] = {0, 0, 0, 0};
};

// The world that is not made of ships.
//
// Almost none of this lives in The_mission -- it lives in module-level globals whose only reset is
// that module's level_init, which a restore runs.  So every SEXP that dresses the mission
// (change-background, set-skybox-model, nebula-change-pattern, change-soundtrack) is undone by a
// reload unless it is written down here.
//
// Deliberately absent: the camera, cutscene bars, fades and subtitles, which last seconds and are
// worse half-restored than not restored (the same reasoning that excluded shockwaves); the
// per-gauge HUD text, coordinates and frames, which mutate table data in Ship_info; post-processing
// effects, which live only in graphics state; and the subspace ambient sound, since sound handles
// are never restored.
struct environment_state {
	// False in a checkpoint written before this section existed, in which case none of it is
	// applied -- otherwise an absent section would blank the sky rather than leave it alone.
	bool present = false;

	// Skybox.  The model and its texture by filename; stars_set_background_model() reloads both.
	SCP_string skybox_model;
	SCP_string skybox_texture;
	uint skybox_flags_hi = 0;   // Nmodel_flags is 64-bit, and the file writes 32 at a time
	uint skybox_flags_lo = 0;
	float skybox_alpha = 1.0f;
	matrix skybox_orient = vmd_identity_matrix;

	int ambient_light = 0;

	// Nebula.  fullneb and the range go back through stars_set_nebula(), which owns all the
	// derived state; the pattern and fog colour go through neb2_post_level_init().
	bool fullneb = false;
	float neb_range = 0.0f;
	SCP_string neb_pattern;
	bool neb_fog_color_override = false;
	int neb_fog_r = 0;
	int neb_fog_g = 0;
	int neb_fog_b = 0;

	bool subspace = false;

	// Background: which set is live, and then the live sun and bitmap instances, which are not the
	// same thing as that set's definition once a SEXP has been at them.
	int background_index = -1;
	SCP_vector<starfield_entry_state> starfield;

	bool motion_debris_override = false;
	SCP_string motion_debris_type;

	SCP_string soundtrack;      // by name; Soundtracks is built from music.tbl

	// HUD.  display_warpout is dual-purpose: 0 and 1 are off and on, anything larger is a
	// timestamp saying when to stop, so it goes in the translated set.
	bool hud_draw = true;
	bool hud_disable_except_messages = false;
	int hud_max_targeting_range = 0;
	int hud_display_warpout = 0;
	int hud_timer_padding = 0;

	// Support ships.  Everything a SEXP or the mission itself can move: which class turns up,
	// how many are left, and the rearm stockpile, which is genuinely spent as the mission runs
	// and would otherwise refill on a restore.
	SCP_string support_ship_class;
	SCP_string support_arrival_anchor;
	SCP_string support_departure_anchor;
	int support_arrival_location = 0;
	int support_departure_location = 0;
	int support_max_ships = 0;
	int support_max_concurrent = 0;
	int support_tally = 0;
	int support_available_for_species = 0;
	float support_max_hull_repair = 0.0f;
	float support_max_subsys_repair = 0.0f;
	bool support_disallow_rearm = false;
	SCP_vector<rearm_pool_entry> rearm_pool;

	bool no_traitor = false;
	SCP_string traitor_override;   // by name; nullptr means none
	SCP_string debriefing_persona; // by name, the persona_index precedent

	bool asteroids_enabled = true;

	// Autopilot navpoints.  The array is fixed length, so it goes out whole -- an unused slot is
	// an entry with an empty name.
	SCP_vector<navpoint_state> navpoints;
	int current_nav = -1;

	SCP_vector<jump_node_state> jump_nodes;
};

// One docking link between two ships.
//
// Docking that the mission file sets up is reproduced by the mission load, because the arrival path
// creates a docked group whole.  What is not reproduced is everything that happened afterwards: a
// support ship that docked to rearm, a freighter that undocked its cargo, an ai-dock order that
// completed.  A restore without this brings a docked pair back as two ships sitting in the right
// places with no link between them, which the AI, the collision code and the departure logic all
// read differently from a real dock.
//
// Emitted once per pair, with the two ends ordered by name so a pair cannot be written twice.
// Dockpoints go by name for the same reason subsystems and debris submodels do: a re-exported pof
// must not silently move a ship to a different bay.
struct dock_pair {
	SCP_string docker;
	SCP_string dockee;
	SCP_string docker_point;
	SCP_string dockee_point;
};

// A mission log entry, reproduced whole.  The timestamp here is mission time, not an engine
// timestamp, so it is restored as-is rather than shifted.
struct log_entry_state {
	int type = 0;
	int flags = 0;
	fix timestamp = 0;
	int timer_padding = 0;
	int index = 0;
	// IFF indices come from iff_defs.tbl, so they shift if a mod reorders it.
	SCP_string primary_team;
	SCP_string secondary_team;
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
	SCP_vector<dock_pair> dock_pairs;
	SCP_vector<ai_state> ai;
	SCP_vector<ship_animation_state> animations;
	SCP_vector<wing_state> wings;
	environment_state environment;
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
	SCP_vector<hotkey_state> hotkeys;
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
