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
 * (mission_checkpoint_apply, called from the GS_EVENT_ENTER_GAME handler once the loadout has
 * been committed; see its declaration below for why there and not game_post_level_init).
 * Reloading rather than restoring in place means the engine is always in a known-clean state;
 * it is the same approach the red alert code takes.
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
 *   A ship caught part-way through warping in or out.  The Arriving_stage and Depart_warp flags
 *   are not stored, so such a ship comes back simply present: the warp effect it was inside is a
 *   fireball object that does not survive either, and resuming the animation without it would
 *   leave a ship sliding out of nothing.  A ship that had begun to depart will be told to depart
 *   again by whatever ordered it the first time.
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

	// Turrets have their own weapon banks, and their own AI class (change-ai-class can retarget a
	// single turret).  By name, since Ai_classes comes from ai.tbl.
	weapon_state weapons;
	bool has_weapons = false;
	SCP_string ai_class;

	// set-armor-type, by name; empty means the class default.
	SCP_string armor_type;

	// Turret targeting as the mission set it: turret-set-target-order (fixed order-type ids),
	// turret-set-target-priorities (Ai_tp_list, by name), turret-set-forced-subsys-target (the
	// subsystem on turret_target, by lookup key) and a script's targeting override.
	SCP_vector<int> targeting_order;
	SCP_vector<SCP_string> target_priorities;
	SCP_string forced_target_subsys;
	bool scripting_target_override = false;

	// The submodel the subsystem drives: its pose and, for a rotating or translating one, how
	// fast it is going and was told to go.  A turret's base is submodel 1 and its barrels
	// submodel 2, so restoring both poses puts a turret back where it was aiming.  has_rotation
	// is false for a subsystem with no submodel of its own.
	bool has_rotation = false;
	float cur_angle = 0.0f;
	float cur_offset = 0.0f;
	float current_turn_rate = 0.0f;
	float desired_turn_rate = 0.0f;
	float turn_accel = 0.0f;
	float current_shift_rate = 0.0f;
	float desired_shift_rate = 0.0f;
	matrix canonical_orient = vmd_identity_matrix;
	vec3d canonical_offset = vmd_zero_vector;
	bool has_gun_orient = false;
	matrix gun_canonical_orient = vmd_identity_matrix;
};

// One guard-range clamp from set-guard-range: this ship will not chase further than range from
// the named ship.
struct guard_range_state {
	SCP_string ship;
	float range = -1.0f;
};

// One change-iff-color override: how observer sees observed on this ship, as a colour rather
// than the colour-table index iff_init_color() hands out at runtime.
struct iff_color_state {
	SCP_string observer;
	SCP_string observed;
	int r = 0;
	int g = 0;
	int b = 0;
};

// Damage dealt to this ship by a named attacker, which is what decides kill and assist credit.
struct damage_credit_state {
	SCP_string ship;
	float damage = 0.0f;
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
// the thing that would otherwise be the key.  That list is built solely by the mission parse and
// nothing extends it at runtime, which the fingerprint check makes identical across runs -- the
// same reasoning that lets Wings[] and the waypoint lists go by index.
struct jump_node_state {
	int index = 0;

	SCP_string name;
	SCP_string display_name;
	SCP_string model;              // filename; empty means the default model
	bool show_polys = false;       // set-jumpnode-model's second argument
	bool hidden = false;
	bool colored = false;
	int color[4] = {0, 0, 0, 0};

	// A script can move a jump node's object like any other.  has_pos is false in a checkpoint
	// written before this was captured; the node then stays where the mission file put it.
	bool has_pos = false;
	vec3d pos = vmd_zero_vector;
};

// A nebula poof type: whether it is on, and a fade in progress.  Poof_info comes from the
// tables and the fade fields on it are runtime state (neb2_fade_poof), so they go by name.
struct poof_state {
	SCP_string name;
	bool enabled = false;
	int fade_start = -1;          // engine timestamp; shifted on restore
	int fade_duration = -1;       // -1 means no fade in progress
	bool fade_in = true;
	float fade_multiplier = -1.0f;
};

// A post-processing effect's live setting (set-post-effect).
struct post_effect_state {
	SCP_string name;
	float intensity = 0.0f;
	vec3d rgb = vmd_zero_vector;
};

// A class-level damage type override (weapon-set-damage-type, ship-set-shockwave-damage-type,
// field-set-damage-type): the table entry it applies to, and the damage type by name, empty
// meaning none.  Only entries that differ from their table default are stored.
struct damage_type_override_state {
	SCP_string subject;
	SCP_string damage_type;
};

// A coordinate point.  Everything a SEXP or a script can change on one, plus the point itself:
// scripts can create and delete these mid-mission, so the set is restored, not just the fields.
struct coordinate_point_state {
	SCP_string name;
	SCP_string group;
	vec3d pos = vmd_zero_vector;   // a point has no orientation (Dont_change_orientation)
	int escort_priority = 0;
	int multi_team = -1;
	bool visible = false;
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

	// Named, like every other flag set in this file.  A bit position in a FLAG_LIST is an
	// implementation detail that moves whenever somebody inserts an entry, so raw bits would
	// quietly corrupt every checkpoint on the next engine update.
	SCP_vector<SCP_string> instance_flags;
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

	// Not here: ai_info::lua_ai_target, the argument list a Lua-driven AI order carries.  It is a
	// LuaValueList, which has no serializable form, so Lua orders are dropped on restore and a
	// ship running one comes back idle (load_ai()).  A script that needs its orders to survive
	// should stage what it needs through mission.setCheckpointData() and re-issue them from the On Checkpoint Restore hook.

	SCP_map<SCP_string, int> ints;
	SCP_map<SCP_string, float> floats;
	SCP_map<SCP_string, int> mission_times;
	SCP_map<SCP_string, int> stamps;
	SCP_map<SCP_string, vec3d> vecs;
	SCP_map<SCP_string, float> override_floats;   // ai_info::ai_override_ci

	// Everything the AI points at, by name.  Empty means "nothing".
	SCP_string target_ship;
	SCP_string previous_target_ship;
	// The targeted subsystem, and the ship that owns it.  Those are two different things:
	// targeted_subsys_parent is its own objnum and need not be the current target.
	SCP_string target_subsystem;   // name + ordinal key, as elsewhere
	SCP_string target_subsystem_ship;
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

	// The name the mission file gives this ship, when a script has renamed it since (ship.Name
	// from Lua renames the ship and its registry entry but not its parse object).  A fresh load
	// only knows the ship by this name; the restore finds it by this and renames it back to
	// `name`.  Empty when the two agree, which is nearly always, and always empty for wing ships,
	// whose waves share one parse object and so have no parse name of their own.
	SCP_string parse_name;

	// --- only meaningful when disposition == Present ---

	SCP_string ship_class;
	SCP_string team;
	SCP_string display_name;
	SCP_string wing_name;
	SCP_string cargo_title;
	SCP_string countermeasure_class;
	SCP_string persona;          // Personas is built from messages.tbl, so the index is a table index

	// alt_type_index and callsign_index look like parse-only indices, but change-alt-name and
	// change-callsign append to Mission_alt_types and Mission_callsigns at runtime, so an index
	// from the saved run can name a different entry, or none, in the fresh parse.  By name, and
	// re-added on apply if the fresh parse does not have them.  Empty means none.
	SCP_string alt_name;
	SCP_string callsign;

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

	// set-departure-info rewrites the route on a present ship as it does on a wing; the anchor by
	// name.  (The path mask and the delay are in the registries already.)
	int departure_location = 0;
	SCP_string departure_anchor;

	// set-armor-type and ship-set-damage-type, all by name; empty means the class default.
	SCP_string armor_type;
	SCP_string shield_armor_type;
	SCP_string collision_damage_type;
	SCP_string debris_damage_type;

	// set-explosion-option: the numbers are in the int registry; these are its two switches.
	bool use_special_explosion = false;
	bool use_shockwave = false;

	// set-player-orders and set-order-allowed-for-target, as Player_orders parse names.  An empty
	// set is a real value (every order removed), so orders_present says whether the file had
	// them at all.
	bool orders_present = false;
	SCP_vector<SCP_string> orders_accepted;
	SCP_vector<SCP_string> orders_allowed_against;

	SCP_vector<guard_range_state> guard_ranges;
	SCP_string special_warpout_ship;    // set-special-warpout-name: the Knossos, by name

	SCP_vector<bool> glow_banks;
	// Instance texture replacements (old, new), in parallel, recovered from the model instance
	// by texture name; "invisible" is the engine's own spelling for a texture switched off.
	SCP_vector<SCP_string> texture_old;
	SCP_vector<SCP_string> texture_new;
	int collision_group_id = 0;

	// change-team-color: the colour set and, mid-fade, the one being faded from; the fade's start
	// time and length are in the registries.
	SCP_string team_color;
	SCP_string secondary_team_color;
	SCP_vector<iff_color_state> iff_colors;

	float sim_hull = 0.0f;              // training-weapon damage, which is kept apart from the real hull
	SCP_vector<damage_credit_state> damage_credits;

	// --- only meaningful when the ship had already left ---
	//
	// ship_class, team, display_name, cargo and cargo_no_deplete above are filled in for a gone
	// ship too, from its exited record: SEXPs keep asking a dead ship its class, IFF and cargo,
	// and the fresh load's record is built from the mission file, not from what the ship had
	// become by the time it died.
	fix exit_time = 0;
	SCP_vector<SCP_string> exit_flags;   // Exit_Flags by name; see Exit_flag_table
	fix time_cargo_revealed = 0;         // mission time, verbatim
	int exit_hull_strength = 0;
};

struct wing_state {
	SCP_string name;
	SCP_vector<SCP_string> flags;
	SCP_map<SCP_string, int> ints;
	SCP_vector<SCP_string> ship_names;   // wing::ship_index, resolved to names
	// wing::special_ship is an index into ship_index[], which ship_wing_cleanup() compacts as
	// members leave, so the leader's slot moves during a mission; by name, and re-derived
	// against the rebuilt list on apply.  Empty means "the first ship".
	SCP_string special_ship;
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

	// set-wing-formation: the formation by name (empty for the default) and its scale.
	SCP_string formation;
	float formation_scale = 1.0f;
};

struct variable_state {
	SCP_string name;
	bool is_number = false;
	SCP_string value;
	// The full SEXP_VARIABLE_* type word: a script can create a variable mid-mission and can
	// change its persistence.  Zero in a file from before this was stored.
	int type = 0;
};

// A prop: a piece of the scene with no crew and no AI, created by the mission file, by prop-create
// or by a script, and moved, re-textured, hidden or removed by any of them.  Mirrors ship_state in
// miniature.  A parsed prop carries a disposition the same way a ship does: Present, NotYetHere
// (its spawn cue had not fired) or Vanished; a prop with no parsed_prop behind it is recreated
// from scratch.
struct prop_state {
	SCP_string name;
	ShipDisposition disposition = ShipDisposition::Present;
	bool no_parse_prop = false;

	SCP_string prop_class;       // Prop_info entry, by name
	vec3d pos = vmd_zero_vector;
	matrix orient = vmd_identity_matrix;
	vec3d vel = vmd_zero_vector;
	vec3d rotvel = vmd_zero_vector;

	float alpha_mult = 1.0f;
	SCP_vector<SCP_string> flags;          // Prop_Flags, by name
	SCP_vector<SCP_string> object_flags;   // Object_Flags, by name
	SCP_vector<bool> glow_banks;
	// Instance texture replacements (old, new), in parallel; class-level ones come from the table.
	SCP_vector<SCP_string> texture_old;
	SCP_vector<SCP_string> texture_new;
	int collision_group_id = 0;
	int despawn_delay = 0;       // dual-encoded like a ship's departure delay; translated
};

// A waypoint list, whole.  checkpointfields.h once said these "come only from the mission
// file"; scripts can create, rename and move them (mission.createWaypointList, the list's Name
// setter, Object.Position on a waypoint), and both AI orders and nav points refer to them by
// name, so a list the fresh parse does not have leaves every one of those references dangling.
struct waypoint_list_state {
	SCP_string name;
	SCP_vector<vec3d> points;
};

struct scoring_state {
	SCP_map<SCP_string, int> ints;
	// Per-ship-class kills, keyed by class name so a table change cannot misattribute them.
	SCP_map<SCP_string, int> class_kills;
	// grant-medal, by name (Medals comes from a table); empty means none.
	SCP_string medal_earned;
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

// Goals have two pieces of runtime state: whether they were met, and whether they still count.
// invalidate-goal and validate-goal flip the INVALID_GOAL bit in mission_goal::type mid-mission,
// and an invalidated objective that came back valid would show in the objectives screen, be
// counted by the goals-incomplete queries and reach the debrief.
struct goal_state {
	SCP_string name;
	int satisfied = 0;
	bool invalid = false;
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

	// is-true-for-duration keeps, per operator node, the mission time at which its condition
	// first held (Sexp_is_true_for_duration_times, sexp.cpp).  A node with such a clock is stored
	// whether or not its value is sticky, since losing the clock restarts the countdown.
	bool has_duration = false;
	fix duration_start = 0;   // mission time, verbatim
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
	// By name; Ai_classes comes from ai.tbl.  Empty means the parse's own value (usually the
	// SUBSYS_STATUS_NO_CHANGE sentinel) is left as it is.
	SCP_string ai_class;
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
	// By name, for the reason given on ship_state::alt_name.
	SCP_string alt_name;
	SCP_string callsign;
	SCP_string cargo;
	bool cargo_no_deplete = false;

	SCP_vector<SCP_string> flags;

	// The same SEXPs reach a ship that has not arrived yet; see the ship_state fields.
	int collision_group_id = 0;
	SCP_string team_color;
	SCP_vector<SCP_string> texture_old;
	SCP_vector<SCP_string> texture_new;
	SCP_vector<iff_color_state> iff_colors;
};

// A piece of debris.
//
// Hull debris is what matters: a large hull chunk has lifeleft == -1 -- it stays for the rest of
// the mission, it collides, and it can be targeted and shot -- so a capital ship wreck is part of
// the battlefield rather than an effect, and a restore that loses it is immediately obvious.  The
// generic fragments, and anything a script made from a model of its own, come along too now that
// the model goes by filename; they cost nothing extra and a script may have been counting on them.
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
	SCP_string parent_alt_name; // by name; see ship_state::alt_name.  Empty means none.

	bool do_not_expire = false;

	// Hull debris is a piece of the ship's own model; the rest is generic fragments from the
	// debris model, and a script can make debris from any model at all.  So the model goes by
	// filename, and a chunk with no ship class is fine as long as it has a model.  A checkpoint
	// written before this was captured has hull debris only and no model name; the class's model
	// is used then.
	bool is_hull = true;
	SCP_string model;
	fix time_started = 0;       // mission time, verbatim; what debris_find_oldest() sorts by
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

	vec3d rotational_velocity = vmd_zero_vector;

	// Mines: the chase in progress and the cooldown after it.  Both engine timestamps.
	int mine_chase_expires = -1;
	int mine_chase_cooldown_expires = -1;

	// Continuous child spawns, one stamp per spawn type; empty in a checkpoint written before
	// this was captured, in which case the spawn clocks start over.
	SCP_vector<int> last_spawn_times;

	// Big-ship attack point: where on the target this shot is aimed, and when to pick anew.
	int big_attack_point_stamp = 0;
	vec3d big_attack_point = vmd_zero_vector;

	// Collision groups are a bitmask a SEXP or script assigns; a shot inherits its parent's.
	int collision_group_id = 0;
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

	// The asteroid field's definition, as distinct from the rocks in it (see asteroid_state).
	// set-asteroid-field, set-debris-field and config-field-targets all rewrite this at runtime,
	// and a mission whose field a SEXP created has no field in the file at all -- in which case
	// the fresh load has no models paged in, and every saved rock would be silently dropped.
	// has_asteroid_field is false in a checkpoint written before this was captured; then the
	// mission file's field is left alone.
	bool has_asteroid_field = false;
	int asteroid_field_num_initial = 0;
	bool asteroid_field_active = true;       // FT_ACTIVE vs FT_PASSIVE
	bool asteroid_field_is_debris = false;   // DG_DEBRIS vs DG_ASTEROID
	bool asteroid_field_enhanced_checks = false;
	bool asteroid_field_has_inner_bound = false;
	float asteroid_field_speed = 0.0f;
	float asteroid_field_bound_rad = 0.0f;
	vec3d asteroid_field_vel = vmd_zero_vector;
	vec3d asteroid_field_min = vmd_zero_vector;
	vec3d asteroid_field_max = vmd_zero_vector;
	vec3d asteroid_field_inner_min = vmd_zero_vector;
	vec3d asteroid_field_inner_max = vmd_zero_vector;
	SCP_vector<SCP_string> asteroid_field_asteroid_types;  // asteroid subtype names, as the field holds them
	SCP_vector<SCP_string> asteroid_field_debris_types;    // Asteroid_info entries, by name
	SCP_vector<SCP_string> asteroid_field_targets;

	// A supernova countdown in progress.  Only the two stages the player is still flying through
	// are restorable (STARTED and CLOSE); the store refuses once the shockwave has hit.  Without
	// this the countdown vanished, and the event that started it was restored as already fired,
	// so a mission that ends by supernova could no longer end that way.
	int supernova_stage = 0;
	float supernova_total = 0.0f;
	float supernova_left = 0.0f;

	// set-time-compression and lock-time-compression, both reset by the level init.
	float time_compression = 1.0f;
	bool time_compression_locked = false;

	SCP_vector<navpoint_state> navpoints;
	int current_nav = -1;

	SCP_vector<jump_node_state> jump_nodes;

	// Which wings the wingman-status gauge is showing, by name, one entry per squadron slot with
	// an empty string for an unused slot.  The set-squadron-wings SEXP can change this
	// mid-mission, and a restart puts the mission's original wings back.
	SCP_vector<SCP_string> squadron_wings;

	// --- Effects and the rest of the world ---
	// Added after the section above; effects_present is false in a checkpoint written before
	// then, and none of what follows is applied from such a file.

	bool effects_present = false;

	// set-gravity-accel.  Turning gravity on or off also changes which weapons are subject to it,
	// so the restore goes through the same recalculation the SEXP does.
	vec3d gravity = vmd_zero_vector;

	SCP_string storm;   // nebula-change-storm; by name, empty for none

	SCP_vector<poof_state> poofs;

	bool has_volumetrics = false;
	bool volumetrics_enabled = true;

	// The fog distances a script can move (mission.NebulaNearDistance and friends).
	float fog_near_distance = 0.0f;
	float fog_1000m_visibility = 0.0f;
	float fog_skybox_clip_distance = 0.0f;
	float fog_clip_distance = 0.0f;

	SCP_vector<post_effect_state> post_effects;
	bool lightshafts_on = true;
	float lightshafts_intensity = 0.0f;

	// The sound environment (set-sound-environment, update-sound-environment).  The preset goes
	// by name; the EFX preset list is a table.  An empty preset means the environment is off.
	SCP_string sound_env_preset;
	float sound_env_volume = 0.0f;
	float sound_env_damping = 0.0f;
	float sound_env_decay = 0.0f;

	// set-friendly-damage-caps writes into the AI profile for the current skill level.
	float beam_friendly_damage_cap = 0.0f;
	float weapon_friendly_damage_cap = 0.0f;
	float weapon_self_damage_cap = 0.0f;

	SCP_vector<damage_type_override_state> weapon_damage_types;
	SCP_vector<damage_type_override_state> weapon_shockwave_damage_types;
	SCP_vector<damage_type_override_state> ship_shockwave_damage_types;
	SCP_vector<damage_type_override_state> asteroid_damage_types;

	// The briefing, debriefing and fiction scores (Mission_music), by spooled-music name, one
	// entry per score with an empty string for none.  A script can change them mid-mission.
	SCP_vector<SCP_string> mission_music;

	SCP_vector<coordinate_point_state> coordinate_points;

	// set-camera-shudder.  A perpetual shudder has no end; a timed one is a stamp.
	bool shudder_perpetual = false;
	bool shudder_everywhere = false;
	int shudder_time = -1;      // engine timestamp; shifted on restore
	int shudder_total = 0;
	float shudder_intensity = 0.0f;

	bool photo_mode_allowed = false;

	// Mission flags a SEXP flips at runtime.
	bool toggle_debriefing = false;
	bool deactivate_autopilot = false;
	bool use_autopilot_cinematics = false;
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

// One message sent but not yet played.  send-message-list pushes a whole conversation onto the
// queue at once, with cumulative delays, so a checkpoint taken mid-way would otherwise drop the
// rest of it -- possibly a minute of dialogue -- with the event that sent it restored as done.
struct message_queue_state {
	SCP_string message;          // Messages[].name
	SCP_string special_message;  // the text with variables substituted, if that was done
	SCP_string who_from;
	int source = 0;
	int builtin_type = -1;
	int flags = 0;               // MQF_*
	int group = 0;
	int priority = 0;
	fix time_added = 0;          // mission time, verbatim
	int window_timestamp = 0;    // translated
	int min_delay_stamp = 0;     // translated
	SCP_string event_to_cancel;  // by event name; empty for none
};

// One order the player gave.  query-orders reads this history, so mission logic keyed on "did the
// player order X to do Y" could never fire after a restore without it.  The engine encodes the
// recipient and target as indices into Parse_names, which get_parse_name_index() extends at
// runtime, so both go by name; "all fighters" is its own case.
struct squadmsg_history_state {
	bool to_all_fighters = false;
	SCP_string order_to;
	SCP_string order;            // Player_orders[].parse_name
	SCP_string target;
	SCP_string order_from;       // ship name
	SCP_string special_subsys;   // for a subsystem order: the subsystem's name on the target
	fix order_time = 0;          // mission time, verbatim
};

// good-primary-time: which primary an AI subject should use against a target.  Subject and
// target are whatever text the SEXP named -- a ship, a wing, a team -- and the engine rebuilds
// the reference from that text.
struct preferred_primary_state {
	SCP_string subject;
	SCP_string target;
	SCP_string weapon;
};

// good-secondary-time: permission for a team to fire a big secondary at a ship, so many at once.
struct huge_fire_state {
	SCP_string team;
	SCP_string weapon;
	SCP_string ship;
	int max_fire_count = 0;
};

// A message a script added during the mission.  Those are appended to Messages[] and are gone
// after a reload, so a send-message still to come that names one would find nothing.
struct message_state {
	SCP_string name;
	SCP_string text;
	SCP_string persona;          // by name; empty for none
	int multi_team = -1;
};

// Mission state that belongs to no ship: the built-in message budget, the personas already spoken
// for, the mission mood, the training context, the reinforcement allowances, the message queue,
// the order history and the player's mission-scoped awards and grants.
//
// Deliberately absent, and each worth naming: the training message queue, which lives in file
// statics in missiontraining.cpp and holds at most a few seconds of pending text; Players_target
// and the lock tracking beside it, which the training update recomputes every frame.
// A HUD gauge's runtime switches: hud-set-active / hud-activate-gauge-type, and hud-set-color.
// Built-in gauges go by their config name, custom gauges by their custom name; the two are
// separate namespaces, hence the flag.
struct hud_gauge_state {
	SCP_string name;
	bool custom = false;
	bool active = true;
	bool sexp_override = false;

	// The colour is only stored when a SEXP set it (sexp_lock_color); otherwise the gauge has
	// whatever the player's HUD configuration gives it, which is not mission state.
	bool sexp_color = false;
	int color[4] = {0, 0, 0, 0};
};

// ignore-key: the action, by the text controlsconfig gives it, and how many more presses to
// swallow (negative means for good).
struct ignored_key_state {
	SCP_string action;
	int count = 0;
};

// A line of the message log.
struct scrollback_line_state {
	fix time = 0;      // mission time, verbatim
	int source = 0;
	SCP_string text;
};

// A stream started by play-sound-from-file.  See sexp_music_entry; the variable is the one
// the SEXP was given, which holds the stream handle and is rewritten on restore.
struct file_sound_state {
	SCP_string filename;
	int type = -1;
	bool loop = false;
	bool paused = false;
	SCP_string variable;
};

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

	// player-use-ai hands the player's ship to the AI for a scripted sequence; lock-perspective
	// fixes the view.  All three are reset by player_level_init().
	bool player_use_ai = false;
	bool perspective_locked = false;
	bool slew_locked = false;
	int viewer_mode = 0;

	SCP_vector<message_queue_state> message_queue;
	SCP_vector<squadmsg_history_state> squad_history;
	SCP_vector<message_state> added_messages;

	// grant-promotion, and the "all alone" message having played; both are Player->flags bits
	// that player_level_init() resets.
	bool promoted = false;
	bool no_check_all_alone_msg = false;

	// allow-ship / allow-weapon are folded into the campaign only when the mission ends, and
	// tech-add-* sets flags on the tables; both are lost on a resume from a fresh launch, with
	// the granting event restored as already fired.  All by name.
	SCP_vector<SCP_string> granted_ships;
	SCP_vector<SCP_string> granted_weapons;
	SCP_vector<SCP_string> tech_ships;
	SCP_vector<SCP_string> tech_weapons;
	SCP_vector<SCP_string> tech_intel;

	SCP_vector<preferred_primary_state> preferred_primaries;
	SCP_vector<huge_fire_state> huge_fire;

	// The player's own settings and standing: throttle (set-player-throttle-speed and the
	// throttle keys), the two auto-targeting toggles and speed matching, the death message
	// (set-death-message), and the friendly-fire tally the traitor logic keeps.
	float player_throttle = 0.0f;
	bool auto_targeting = false;
	bool auto_match_speed = false;
	bool match_target = false;
	SCP_string death_message;
	int friendly_hits = 0;
	float friendly_damage = 0.0f;
	fix friendly_last_hit_time = 0;      // mission time, verbatim
	fix last_warning_message_time = 0;   // mission time, verbatim

	// --- HUD and input ---
	// Added after the fields above; hud_present is false in a checkpoint written before then,
	// and none of what follows is applied from such a file.

	bool hud_present = false;

	SCP_vector<hud_gauge_state> hud_gauges;
	bool hud_high_contrast = false;
	bool disable_cockpits = false;
	bool disable_cockpit_sway = false;
	bool sensor_static_forced = false;   // hud-force-sensor-static

	SCP_vector<ignored_key_state> ignored_keys;
	SCP_vector<scrollback_line_state> scrollback;
	SCP_vector<file_sound_state> file_sounds;
};

// A mission log entry, reproduced whole.  The timestamp here is mission time, not an engine
// timestamp, so it is restored as-is rather than shifted.
struct log_entry_state {
	int type = 0;
	int flags = 0;
	fix timestamp = 0;
	int timer_padding = 0;
	// log_entry::index means something different per type, and two of those meanings are indices
	// this file must not carry: a Cargo_names index for the cargo-revealed entries (set-cargo
	// extends that list at runtime), and a Ship_info index packed with a subsystem index for
	// subsystem-destroyed entries (a table index).  Those go out as names in index_name (the cargo,
	// or the subsystem) and index_class (the ship class), and index is left at zero for them;
	// every other type's index -- a wave number, a goal number -- is mission-file order and is
	// carried as it is.
	int index = 0;
	SCP_string index_name;
	SCP_string index_class;
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
	SCP_vector<waypoint_list_state> waypoint_lists;
	SCP_vector<prop_state> props;
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

	// Coordinate points on the escort list, by name.  Ships need nothing stored (the escort flag
	// on each ship rebuilds the list), but a coordinate point carries no such flag: the list is
	// the only record that the player put it there.
	SCP_vector<SCP_string> escort_points;
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
// if the store was refused or the file could not be written.  Called from inside an event, it
// only queues the store and returns true: the event has not been marked as fired yet, so the
// store waits for the end of the frame (mission_checkpoint_process_pending_load()).
bool mission_checkpoint_store(const SCP_string& slot);

// Does a usable checkpoint exist for this pilot, campaign, mission and slot?  A checkpoint
// whose mission fingerprint no longer matches counts as absent.
bool mission_checkpoint_exists(const SCP_string& slot);

// Remove a checkpoint.  Silently does nothing if there was none.
void mission_checkpoint_delete(const SCP_string& slot);

// Remove every checkpoint this pilot has for a mission (the current one when the name is empty)
// in the current campaign, and return how many went.  The only way to delete in bulk that keeps
// the existence cache honest; scripts must come through here rather than the file layer.
int mission_checkpoint_delete_all(const SCP_string& mission_name);

// Request a load.  This does NOT reload the mission itself -- doing that while SEXP
// evaluation is on the stack would tear the level down underneath the caller.  It records the
// request, having already read and checked the file, so a missing or mismatched checkpoint is
// refused here; mission_checkpoint_process_pending_load() acts on it at the end of the frame.
void mission_checkpoint_request_load(const SCP_string& slot, checkpoint::LoadFlags flags);

// Is a load queued?
bool mission_checkpoint_load_pending();

// Called once per frame at the end of the gameplay loop.  Writes any stores events queued this
// frame, then, if a load is queued, posts the mission restart that will eventually land in
// mission_checkpoint_apply().
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
