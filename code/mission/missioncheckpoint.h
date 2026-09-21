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

#include <cstdint>

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
 *
 * WHAT IS DELIBERATELY NOT CAPTURED
 *
 * These are decisions rather than omissions, and they are written down here so that the next
 * reader does not spend an afternoon working out whether each one was forgotten.  Individual
 * structs below carry their own narrower notes; this is the list of whole categories.
 *
 *   Shockwaves.  shockwave::obj_sig_hitlist holds object signatures, which are regenerated on
 *   every load and can name weapons or debris that do not survive at all; a bad remap double-
 *   damages a ship.  total_time is about a second, so a checkpoint almost never lands inside one.
 *
 *   Small debris.  It expires in seconds, it is pure decoration, and debris_create_only() culls
 *   it by distance anyway.  Hull debris is captured, because a capital ship wreck is permanent,
 *   collidable and targetable -- battlefield terrain rather than an effect.
 *
 *   Particles, decals, trails, sparks, sound handles and RNG state.  None of these are ever worth
 *   doing: they are either regenerated within a frame or two of the restore, or they are handles
 *   into subsystems that were torn down with the level.
 *
 *   The camera, cutscene bars, fades and subtitles.  A checkpoint taken mid-cutscene is
 *   pathological, the state lasts seconds, and a half-restored camera is worse than none.  The
 *   bars and the fade are force-cleared on restore so a checkpoint taken mid-fade cannot resume
 *   into a black screen.
 *
 *   A ship in the middle of its death roll is recorded as already destroyed, and contributes no
 *   debris, because it had not produced any yet.  There is no way to resume a death roll on a
 *   fresh load.
 *
 *   Autopilot engagement.  Half of what the autopilot needs is the flight path it had worked out,
 *   which is not stored; dropping the player into a half-engaged autopilot flying nowhere is
 *   worse than handing the controls back.  The nav points themselves are captured.
 *
 * Two of these used to be longer.  Weapons in flight and beams are now captured -- see
 * projectile_state and beam_shot_state -- as is the asteroid field.  What made those tractable
 * was that each is recreated through the engine's own entry point (weapon_create(), beam_fire(),
 * asteroid_create()) rather than being reconstructed field by field, so the caches, trails and
 * sound handles that made them look impossible are built by the engine as it would for a live
 * shot, and only the state that describes where the thing is and what it is doing comes from the
 * file.
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

// A nav point's mutable state.  Nav points themselves are created by the mission, but whether
// one has been visited, hidden or locked is all SEXP-driven and changes during play.
// One autopilot navpoint.
//
// A whole family of operators adds, deletes, hides, restricts and marks these visited while the
// mission runs, so the array is runtime state rather than mission-file state.  The array goes out
// whole, unused slots included, because a nav is identified by its slot.  target_index is an
// object index unless the nav is bound to a waypoint, so it goes out as a ship name, or as a
// waypoint list name plus the node within it.
struct navpoint_state {
	SCP_string name;               // empty for an unused slot
	int flags = 0;

	SCP_string target;             // ship name, or waypoint list name for a waypoint nav
	int waypoint_num = -1;

	int normal_color[3] = {0, 0, 0};
	int visited_color[3] = {0, 0, 0};
};

// One jump node, as the mission has left it.
//
// Identified by its position in Jump_nodes rather than by name, because set-jumpnode-name renames
// the thing that would otherwise be the key.  That list is built solely by the mission parse,
// which the fingerprint check makes identical across runs -- the same reasoning that lets
// alt_type_index stay an index.
struct jump_node_state {
	int index = 0;

	SCP_string name;
	SCP_string display_name;
	SCP_string model;              // filename; empty means the default model
	bool hidden = false;
	bool colored = false;
	int color[4] = {0, 0, 0, 0};
};

// One live sun or background bitmap.  These are instances rather than the definition of the
// background set they came from, and once a SEXP has been at them the two are no longer the same.
struct starfield_entry_state {
	SCP_string name;
	bool is_sun = false;

	float scale_x = 1.0f;
	float scale_y = 1.0f;
	int div_x = 1;
	int div_y = 1;
	angles ang = {0.0f, 0.0f, 0.0f};
};

struct asteroid_state {
	SCP_string type_name;      // Asteroid_info entry, by name
	int subtype = 0;
	vec3d pos = vmd_zero_vector;
	matrix orient = vmd_identity_matrix;
	vec3d vel = vmd_zero_vector;
	vec3d rotvel = vmd_zero_vector;
	float hull = 0.0f;
	int flags = 0;
	SCP_string target_ship;    // asteroids do have targets
	int check_for_wrap = 0;
	int check_for_collide = 0;
	int final_death_time = 0;
};

// One running model animation, as it stood at the checkpoint.  The id is a hash of the
// animation's own name and its ship class's name, not a table index, so it survives table
// reordering and engine updates; the trigger type and name are kept alongside it purely so a
// human reading the file can tell what it was.
struct animation_state {
	unsigned int id = 0;
	int state = 0;              // ModelAnimationState
	int direction = 0;          // ModelAnimationDirection
	float time = 0.0f;
	float duration = 0.0f;
	float speed = 1.0f;
	std::uint64_t instance_flags = 0;
};

// One end of a docking connection, as seen from the ship that holds it.  Both ships record the
// link, so the restore has to guard against docking the same pair twice.
struct dock_link_state {
	SCP_string other_ship;
	SCP_string my_point;      // dock point name on this ship
	SCP_string their_point;   // dock point name on the other ship
};

// One entry from ai_info::goals.  Goal modes and flags go out by name, and every reference to
// something in the mission -- the target, the waypoint list, the dock points -- goes out as the
// name it was given rather than the index it resolved to, so a mod that reorders its tables
// cannot turn "guard the Orion" into "guard something else".
struct ai_goal_state {
	SCP_string mode;             // from Ai_goal_names
	SCP_string type;             // ai_goal_type, by name
	SCP_vector<SCP_string> flags;

	int signature = 0;
	int submode = 0;
	int priority = 0;
	fix time = 0;

	SCP_string target_name;
	SCP_string waypoint_list;    // wp_list_index resolved to a name
	SCP_string docker_point;     // always a name, even when the live goal held an index
	SCP_string dockee_point;

	// For AI_GOAL_CHASE_SHIP_CLASS the submode is a ship class index, which a table change would
	// otherwise reassign; when that is what the submode means, the class name is authoritative.
	SCP_string submode_ship_class;

	int int_data = 0;
	float float_data = 0.0f;
};

struct ai_state {
	bool present = false;

	SCP_vector<SCP_string> flags;          // AI::AI_Flags
	SCP_vector<SCP_string> override_flags; // AI::Maneuver_Override_Flags

	SCP_string ai_class;           // an index into Ai_classes, which comes from ai.tbl, so by name

	SCP_map<SCP_string, int> ints;
	SCP_map<SCP_string, float> floats;
	SCP_map<SCP_string, int> mission_times;
	SCP_map<SCP_string, int> stamps;
	SCP_map<SCP_string, vec3d> vecs;
	SCP_map<SCP_string, float> override_floats;   // ai_info::ai_override_ci

	// Everything the AI points at, by name.  Empty means "nothing".
	SCP_string target_ship;
	SCP_string previous_target_ship;
	SCP_string target_subsystem;   // name + ordinal key, as elsewhere
	SCP_string goal_ship;
	SCP_string guard_ship;
	SCP_string guard_wing;
	SCP_string ignore_ship;
	SCP_string ignore_wing;
	SCP_vector<SCP_string> ignore_new;   // the fixed-length ignore_new_objnums array, by slot
	SCP_string support_ship;
	SCP_string hitter_ship;
	SCP_string attacker_ship;
	SCP_string waypoint_list;      // wp_list_index resolved to a name
	SCP_string artillery_ship;

	// last_subsys_target carries no parent of its own at runtime, so it is looked for on the
	// current target, which is where the AI put it.
	SCP_string last_subsys_target_ship;
	SCP_string last_subsys_target;

	SCP_vector<ai_goal_state> goals;
	// Which ai_info::goals slot each entry came from.  active_goal is an index into that array,
	// so a gap in the middle has to stay a gap.
	SCP_vector<int> goal_slots;
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

	// This ship has no parse object: nothing in the mission file will recreate it, so the
	// restore has to build it from scratch rather than waiting for an arrival cue that does not
	// exist.  In practice this means a support ship, which is brought in mid-mission by
	// mission_bring_in_support_ship() rather than parsed.
	bool no_parse_object = false;

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
	ai_state ai;
	SCP_vector<dock_link_state> docks;
	SCP_vector<animation_state> animations;

	// --- only meaningful when the ship had already left ---
	fix exit_time = 0;
};

struct wing_state {
	SCP_string name;
	SCP_vector<SCP_string> flags;
	SCP_map<SCP_string, int> ints;
	SCP_vector<SCP_string> ship_names;   // wing::ship_index, resolved to names
	fix time_gone = 0;
	int wave_delay_timestamp = 0;

	// Carried alongside the Has_display_name flag, since restoring one without the other would
	// leave the wing claiming a display name it does not have.
	SCP_string display_name;

	// set-arrival-info and set-departure-info rewrite all of this on a wing exactly as they do
	// on a ship that has not arrived, so it needs the same treatment parse_object_state gets:
	// anchors by name, everything else verbatim.  arrival_distance and the two delays are
	// already in the CKPT_WING_INTS and CKPT_WING_STAMPS lists.
	SCP_string arrival_anchor;
	SCP_string departure_anchor;
	int arrival_location = 0;
	int departure_location = 0;
	int arrival_path_mask = 0;
	int departure_path_mask = 0;

	// A wing carries its own ai_goals[], handed to each ship as it arrives, and a SEXP can
	// rewrite them mid-mission.  Same struct as the per-ship orders.
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
// One entry of a not-yet-arrived ship's subsys_status list.  This is where a loadout change
// made by SEXP before a wing arrives actually lives, so losing it means a later wave arrives
// with the mission file's loadout rather than the one the mission gave it.
struct parse_subsys_state {
	SCP_string name;               // subsystem name, or the "Pilot" pseudo-subsystem
	float percent = 0.0f;
	int ai_class = -1;
	SCP_string cargo;
	SCP_string cargo_title;
	// Bank contents by weapon class name; ammo runs in parallel.  An empty name is an empty bank.
	SCP_vector<SCP_string> primary_banks;
	SCP_vector<int> primary_ammo;
	SCP_vector<SCP_string> secondary_banks;
	SCP_vector<int> secondary_ammo;
};

struct parse_object_state {
	SCP_string name;
	SCP_vector<parse_subsys_state> subsystems;
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

// A weapon in flight.  Note the name: `weapon_state` above is a ship's weapon banks, which is a
// different thing entirely.
//
// A missile already halfway to its target is part of the tactical picture the checkpoint is
// meant to preserve, so these are recreated rather than dropped.  Everything that points at
// another object -- the ship that fired it, the turret that fired it, what it is homing on -- is
// stored by name for the usual reason: object numbers are handed out in creation order and mean
// something different in the next run.
struct projectile_state {
	SCP_string weapon_class;

	vec3d pos = vmd_zero_vector;
	matrix orient = vmd_identity_matrix;
	vec3d velocity = vmd_zero_vector;
	vec3d desired_velocity = vmd_zero_vector;
	vec3d start_pos = vmd_zero_vector;

	float hull = 0.0f;
	float lifeleft = 0.0f;
	fix creation_time = 0;      // mission time, restored as-is like the log timestamps
	int group_id = -1;          // remapped on apply; the saved numbers mean nothing in a new run

	SCP_string team;
	SCP_string species;
	SCP_vector<SCP_string> flags;
	SCP_string weapon_state;    // WeaponState, by name

	// Who fired it.  A weapon whose parent is gone keeps flying, just without a parent -- which
	// is already a state the engine handles, since parents die all the time.
	SCP_string parent_ship;
	SCP_string parent_turret;   // subsystem lookup key, empty when not turret-fired

	// What it is homing on.  Only ship targets survive: a weapon homing on debris, an asteroid
	// or another weapon has nothing to find its target by, and loses the lock.
	SCP_string homing_ship;
	SCP_string homing_subsys;   // subsystem lookup key
	vec3d homing_pos = vmd_zero_vector;
	bool has_homing_pos = false;

	float det_range = 0.0f;
	float weapon_max_vel = 0.0f;
	float launch_speed = 0.0f;
	float alpha_current = 1.0f;
	bool alpha_backward = false;

	// Local SSM: the missile is mid-jump, and the stage decides whether it is here at all.
	int lssm_stage = -1;
	fix lssm_warpout_time = 0;
	fix lssm_warpin_time = 0;
	vec3d lssm_target_pos = vmd_zero_vector;

	int cmeasure_timer = 0;     // engine timestamp, shifted on restore
};

// A beam that is mid-fire.
//
// Unlike a weapon, a beam is anchored to the turret firing it and the thing it is firing at, so
// it is put back by re-firing it through beam_fire() with the saved aim data and then winding it
// forward to where it was.  Everything beam_fire() derives from the weapon table -- the beam
// type, its range, its widths, its total life -- is deliberately not stored: it is reproduced by
// the same derivation, and storing it would only create a way for the two to disagree.
struct beam_shot_state {
	SCP_string weapon_class;

	SCP_string shooter_ship;    // by name; a beam whose shooter is gone is not restored
	SCP_string turret;          // subsystem lookup key
	SCP_string target_ship;     // by name; only ship targets survive
	SCP_string target_subsys;   // subsystem lookup key
	SCP_string team;
	SCP_string weapon_state;    // WeaponState, by name: warmup, firing, paused or warmdown

	// BF_* bits, by name.  These are plain #defines rather than a flagset, so they get their own
	// small table instead of going through collect_flags().
	SCP_vector<SCP_string> flags;

	vec3d target_pos1 = vmd_zero_vector;
	vec3d target_pos2 = vmd_zero_vector;
	vec3d last_start = vmd_zero_vector;
	vec3d last_shot = vmd_zero_vector;
	vec3d local_fire_position = vmd_zero_vector;

	float life_left = 0.0f;
	float current_width_factor = 1.0f;
	float u_offset_local = 0.0f;
	float beam_glow_frame = 0.0f;
	int framecount = 0;
	int shot_index = 0;
	int bank = -1;
	int firingpoint = -1;
	int warmup_stamp = -1;
	int warmdown_stamp = -1;

	// beam_info: the aim vectors that decide exactly how the beam sweeps over its life.  The
	// multiplayer path hands these from server to client wholesale so that every machine sees
	// the same beam; a restore needs them for the same reason.
	vec3d dir_a = vmd_zero_vector;
	vec3d dir_b = vmd_zero_vector;
	vec3d rot_axis = vmd_zero_vector;
	int shot_count = 0;
	SCP_vector<float> shot_aim;
};

// The world that is not made of ships.
//
// Almost none of this lives in The_mission -- it lives in module-level globals whose only reset is
// that module's level_init, which a restore runs.  So every SEXP that dresses the mission
// (change-background, set-skybox-model, nebula-change-pattern, change-soundtrack, the
// set-support-ship family, the nav operators) is undone by a reload unless it is written down
// here.
//
// Deliberately absent, and each worth naming: the camera, cutscene bars, fades and subtitles,
// which last seconds and are worse half-restored than not restored; the per-gauge HUD text,
// coordinates and frames, which mutate table data in Ship_info; post-processing effects, which
// live only in graphics state; and the subspace ambient sound, since sound handles are never
// restored.
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
	// same thing as that set's definition once a SEXP has been at them.  The index is allowed to
	// be an index because Backgrounds[] is built solely by the mission parse, which the fingerprint
	// check makes identical across runs.
	int background_index = -1;
	SCP_vector<starfield_entry_state> starfield;

	bool motion_debris_override = false;
	SCP_string motion_debris_type;

	// Which soundtrack the mission is on and whether combat music has already kicked in.  By
	// name, because the index is into music.tbl and is not stable across builds or mod loads.
	SCP_string soundtrack;
	bool music_battle_started = false;

	// HUD.  display_warpout is dual-purpose: 0 and 1 are off and on, anything larger is a
	// timestamp saying when to stop, so it goes in the translated set.
	bool hud_draw = true;
	bool hud_disable_except_messages = false;
	int hud_max_targeting_range = 0;
	int hud_display_warpout = 0;

	// Support ships.  Everything a SEXP or the mission itself can move: which class turns up,
	// how many are left, and the rearm stockpile, which is genuinely spent as the mission runs
	// and would otherwise refill on a restore.
	SCP_string support_ship_class;     // empty means "work it out from the requester's species"
	SCP_string support_arrival_location;    // by name, from Arrival_location_names
	SCP_string support_departure_location;  // by name, from Departure_location_names

	// Anchors go by name, the same way the parse objects' do: either a ship, or one of the
	// "<any hostile>" specials.  At runtime an anchor is a ship registry index, and support ships
	// append to that registry as they are called in, so the number is not stable across a reload.
	SCP_string support_arrival_anchor;
	SCP_string support_departure_anchor;

	int support_max_ships = 0;
	int support_max_concurrent = 0;
	int support_tally = 0;
	int support_available_for_species = 0;
	float support_max_hull_repair = 0.0f;
	float support_max_subsys_repair = 0.0f;
	bool support_disallow_rearm = false;

	// Per team, weapon class name -> rounds left in the pool.  An absent entry means the mission
	// default, so only what the map actually holds is stored.
	SCP_vector<SCP_map<SCP_string, int>> rearm_pools;

	bool no_traitor = false;
	SCP_string traitor_override;   // by name; empty means none
	SCP_string debriefing_persona; // by name, the persona_index precedent

	bool asteroids_enabled = true;

	SCP_vector<navpoint_state> navpoints;
	int current_nav = -1;

	SCP_vector<jump_node_state> jump_nodes;

	// Which wings the wingman-status gauge is showing, by name, one entry per squadron slot with
	// an empty string for an unused slot.  The set-squadron-wings SEXP can change this
	// mid-mission, and a restart puts the mission's original wings back.
	SCP_vector<SCP_string> squadron_wings;
};

// One reinforcement's remaining allowance.
//
// num_uses counts up as the player calls them in and the availability bit is set when the mission
// makes one callable, so both move as the mission runs.  Everything else about a reinforcement --
// how many uses it started with, its type, its acknowledgement messages -- comes from the mission
// file and is reproduced by the load.  Without this a player who has spent two of their three
// support calls gets them back.
struct reinforcement_state {
	SCP_string name;
	int num_uses = 0;
	bool available = false;
};

// Mission state that belongs to no ship: the built-in message budget, the personas already spoken
// for, the mission mood, the training context and the reinforcement allowances.
//
// Deliberately absent, and each worth naming: the mission message queue and the training message
// queue, both of which live in file statics in their own modules and hold at most a few seconds
// of text that has not been said yet; Squadmsg_history, which is a log of orders given rather
// than state that affects play, and whose four ship references each have their own encoding;
// Players_target and the lock tracking beside it, which the training update recomputes every
// frame.
struct mission_extra_state {
	// As with the environment, an absent section has to mean "says nothing" rather than "all
	// zeroes" -- zero built-in messages used is a real value.
	bool present = false;

	SCP_map<SCP_string, int> player_ints;
	SCP_map<SCP_string, int> training_ints;
	int training_context_speed_timestamp = 0;

	SCP_vector<SCP_string> used_personas;   // by name; Personas comes from messages.tbl
	int mission_mood = 0;
	bool no_builtin_msgs = false;
	bool no_builtin_command = false;

	SCP_vector<reinforcement_state> reinforcements;
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
	SCP_vector<hotkey_state> hotkeys;
	SCP_vector<asteroid_state> asteroids;

	SCP_vector<projectile_state> projectiles;
	SCP_vector<beam_shot_state> beams;

	environment_state environment;
	mission_extra_state mission;

	// Whatever scripts asked to have remembered.  Strings only, deliberately: serialising
	// arbitrary Lua values is a different problem, and a script that needs structure can encode
	// it itself.  See mission.setCheckpointData() and the two checkpoint hooks.
	SCP_map<SCP_string, SCP_string> script_data;

	// Which hotkey set the player currently has selected, -1 for none.  Separate from the sets
	// themselves: restoring the contents but not the selection drops the player back to no
	// selection mid-mission.
	int current_hotkey_set = -1;
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

// Script data: a string-to-string map that rides along in the checkpoint file, so a script can
// remember state the engine knows nothing about.
//
// A script stages values from the On Checkpoint Save hook and reads them back from On Checkpoint
// Restore, but neither is enforced: a write outside a save goes into the live map and is picked up
// by the next store, and a read outside a restore returns whatever the last restore loaded.  Both
// are cleared when the level is torn down, so one mission's script data cannot reach another.
void mission_checkpoint_set_script_data(const SCP_string& key, const SCP_string& value);
bool mission_checkpoint_get_script_data(const SCP_string& key, SCP_string& out_value);

// Drop everything cached about the mission that was loaded.  Called from game_level_init(), so a
// mission edited and reloaded within one run of the game is looked at afresh rather than still
// being judged against the copy that was on disk the first time.  Does NOT touch a pending load:
// that has to survive the reload it asked for.
void mission_checkpoint_level_init();

#endif // _MISSIONCHECKPOINT_H
