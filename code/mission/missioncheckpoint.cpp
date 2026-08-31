/*
 * Copyright (C) Freespace Open 2013.  All rights reserved.
 *
 * All source code herein is the property of Freespace Open. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 */

#include "mission/missioncheckpoint.h"

#include "bmpman/bmpman.h"
#include "debris/debris.h"
#include "graphics/light.h"
#include "hud/hud.h"
#include "jumpnode/jumpnode.h"
#include "nebula/neb.h"
#include "starfield/starfield.h"
#include "ai/ai.h"
#include "asteroid/asteroid.h"
#include "gamesnd/eventmusic.h"
#include "autopilot/autopilot.h"
#include "ai/aigoals.h"
#include "object/waypoint.h"
#include "gamesequence/gamesequence.h"
#include "globalincs/systemvars.h"
#include "hud/hudescort.h"
#include "hud/hudwingmanstatus.h"
#include "hud/hudtarget.h"
#include "model/animation/modelanimation.h"
#include "model/model.h"
#include "species_defs/species_defs.h"
#include "io/timer.h"
#include "iff_defs/iff_defs.h"
#include "mission/checkpointfields.h"
#include "mission/checkpointfile.h"
#include "mission/missioncampaign.h"
#include "mission/missiongoals.h"
#include "mission/missionlog.h"
#include "mission/missionmessage.h"
#include "mission/missiontraining.h"
#include "mission/missionparse.h"
#include "mod_table/mod_table.h"
#include "network/multiutil.h"
#include "object/object.h"
#include "object/objectdock.h"
#include "parse/parselo.h"
#include "parse/sexp.h"
#include "parse/sexp_container.h"
#include "playerman/player.h"
#include "popup/popup.h"
#include "scripting/global_hooks.h"
#include "scripting/hook_api.h"
#include "ship/ship.h"
#include "ship/shipfx.h"
#include "stats/scoring.h"
#include "weapon/beam.h"
#include "weapon/weapon.h"

#include <algorithm>
#include <array>

extern char Game_current_mission_filename[];
// Lives in freespace.cpp with no header of its own; the graphics back ends declare it the same way.
extern int Game_subspace_effect;

using namespace checkpoint;

namespace {

// ------------------------------------------------------------------
// Flag name tables
// ------------------------------------------------------------------
//
// Flags are written by name, never by bit position.  A flag's position in its FLAG_LIST is an
// implementation detail that shifts whenever anybody inserts a new one, so writing the raw
// bits would quietly corrupt every checkpoint on the next engine update.
//
// Only flags that are genuinely mutable during a mission belong here.  Anything set once at
// parse time is reproduced by the mission load itself, and anything transient (dying, warping)
// is deliberately excluded -- see the note about dying ships in mission_checkpoint_store().

struct ship_flag_entry {
	Ship::Ship_Flags flag;
	const char* name;
};

const ship_flag_entry Ship_flag_table[] = {
	{Ship::Ship_Flags::Cargo_revealed, "cargo_revealed"},
	{Ship::Ship_Flags::Scannable, "scannable"},
	{Ship::Ship_Flags::No_scanned_cargo, "no_scanned_cargo"},
	{Ship::Ship_Flags::Hidden_from_sensors, "hidden_from_sensors"},
	{Ship::Ship_Flags::Stealth, "stealth"},
	{Ship::Ship_Flags::Friendly_stealth_invis, "friendly_stealth_invis"},
	{Ship::Ship_Flags::Dont_collide_invis, "dont_collide_invis"},
	{Ship::Ship_Flags::Hide_ship_name, "hide_ship_name"},
	{Ship::Ship_Flags::Primitive_sensors, "primitive_sensors"},
	{Ship::Ship_Flags::Afterburner_locked, "afterburner_locked"},
	{Ship::Ship_Flags::Primaries_locked, "primaries_locked"},
	{Ship::Ship_Flags::Secondaries_locked, "secondaries_locked"},
	{Ship::Ship_Flags::Primary_linked, "primary_linked"},
	{Ship::Ship_Flags::Secondary_dual_fire, "secondary_dual_fire"},
	{Ship::Ship_Flags::Force_primary_unlinking, "force_primary_unlinking"},
	{Ship::Ship_Flags::No_subspace_drive, "no_subspace_drive"},
	{Ship::Ship_Flags::Warp_broken, "warp_broken"},
	{Ship::Ship_Flags::Warp_never, "warp_never"},
	{Ship::Ship_Flags::Vaporize, "vaporize"},
	{Ship::Ship_Flags::Disabled, "disabled"},
	{Ship::Ship_Flags::No_ets, "no_ets"},
	{Ship::Ship_Flags::Cloaked, "cloaked"},
	{Ship::Ship_Flags::No_thrusters, "no_thrusters"},
	{Ship::Ship_Flags::No_death_scream, "no_death_scream"},
	{Ship::Ship_Flags::Always_death_scream, "always_death_scream"},
	{Ship::Ship_Flags::Escort, "escort"},
	{Ship::Ship_Flags::No_arrival_music, "no_arrival_music"},
	{Ship::Ship_Flags::Toggle_subsystem_scanning, "toggle_subsystem_scanning"},
	{Ship::Ship_Flags::Force_shields_on, "force_shields_on"},
	{Ship::Ship_Flags::Affected_by_gravity, "affected_by_gravity"},
	{Ship::Ship_Flags::Ship_locked, "ship_locked"},
	{Ship::Ship_Flags::Weapons_locked, "weapons_locked"},
	{Ship::Ship_Flags::No_secondary_lockon, "no_secondary_lockon"},
	{Ship::Ship_Flags::Aspect_immune, "aspect_immune"},
	{Ship::Ship_Flags::No_targeting_limits, "no_targeting_limits"},
	{Ship::Ship_Flags::Cannot_perform_scan_hide_cargo, "cannot_perform_scan_hide_cargo"},
	{Ship::Ship_Flags::Cannot_perform_scan_show_cargo, "cannot_perform_scan_show_cargo"},
	{Ship::Ship_Flags::No_builtin_messages, "no_builtin_messages"},
	{Ship::Ship_Flags::Scramble_messages, "scramble_messages"},
	{Ship::Ship_Flags::EMP_doesnt_scramble_messages, "emp_doesnt_scramble_messages"},
	{Ship::Ship_Flags::Hide_mission_log, "hide_mission_log"},
	{Ship::Ship_Flags::No_disabled_self_destruct, "no_disabled_self_destruct"},
	{Ship::Ship_Flags::Subsystem_movement_locked, "subsystem_movement_locked"},
	{Ship::Ship_Flags::Maneuver_despite_engines, "maneuver_despite_engines"},
	{Ship::Ship_Flags::Navpoint_carry, "navpoint_carry"},
	{Ship::Ship_Flags::Navpoint_needslink, "navpoint_needslink"},
	{Ship::Ship_Flags::Departure_ordered, "departure_ordered"},
	{Ship::Ship_Flags::Fail_sound_locked_primary, "fail_sound_locked_primary"},
	{Ship::Ship_Flags::Fail_sound_locked_secondary, "fail_sound_locked_secondary"},
	{Ship::Ship_Flags::No_passive_lightning, "no_passive_lightning"},
	{Ship::Ship_Flags::No_insignias, "no_insignias"},
	{Ship::Ship_Flags::Glowmaps_disabled, "glowmaps_disabled"},
	{Ship::Ship_Flags::Draw_as_wireframe, "draw_as_wireframe"},
	{Ship::Ship_Flags::Render_full_detail, "render_full_detail"},
	{Ship::Ship_Flags::Render_without_light, "render_without_light"},
	{Ship::Ship_Flags::Render_without_weapons, "render_without_weapons"},
	{Ship::Ship_Flags::Render_with_alpha_mult, "render_with_alpha_mult"},
};

// Several things a designer thinks of as ship state -- invulnerability, weapon protection,
// whether the ship can be moved -- actually live on the object, not the ship.
struct object_flag_entry {
	Object::Object_Flags flag;
	const char* name;
};

const object_flag_entry Object_flag_table[] = {
	{Object::Object_Flags::Invulnerable, "invulnerable"},
	{Object::Object_Flags::Protected, "protected"},
	{Object::Object_Flags::Beam_protected, "beam_protected"},
	{Object::Object_Flags::Flak_protected, "flak_protected"},
	{Object::Object_Flags::Laser_protected, "laser_protected"},
	{Object::Object_Flags::Missile_protected, "missile_protected"},
	{Object::Object_Flags::Targetable_as_bomb, "targetable_as_bomb"},
	{Object::Object_Flags::Immobile, "immobile"},
	{Object::Object_Flags::Dont_change_position, "dont_change_position"},
	{Object::Object_Flags::Dont_change_orientation, "dont_change_orientation"},
	{Object::Object_Flags::No_shields, "no_shields"},
	{Object::Object_Flags::Collides, "collides"},
	{Object::Object_Flags::Renders, "renders"},
	{Object::Object_Flags::Attackable_if_no_collide, "attackable_if_no_collide"},
	{Object::Object_Flags::Collides_with_parent, "collides_with_parent"},
};

struct subsys_flag_entry {
	Ship::Subsystem_Flags flag;
	const char* name;
};

const subsys_flag_entry Subsys_flag_table[] = {
	{Ship::Subsystem_Flags::Cargo_revealed, "cargo_revealed"},
	{Ship::Subsystem_Flags::Untargetable, "untargetable"},
	{Ship::Subsystem_Flags::No_SS_targeting, "no_ss_targeting"},
	{Ship::Subsystem_Flags::Has_fired, "has_fired"},
	{Ship::Subsystem_Flags::FOV_Required, "fov_required"},
	{Ship::Subsystem_Flags::FOV_edge_check, "fov_edge_check"},
	{Ship::Subsystem_Flags::No_replace, "no_replace"},
	{Ship::Subsystem_Flags::No_live_debris, "no_live_debris"},
	{Ship::Subsystem_Flags::Vanished, "vanished"},
	{Ship::Subsystem_Flags::Missiles_ignore_if_dead, "missiles_ignore_if_dead"},
	{Ship::Subsystem_Flags::Rotates, "rotates"},
	{Ship::Subsystem_Flags::Translates, "translates"},
	{Ship::Subsystem_Flags::Damage_as_hull, "damage_as_hull"},
	{Ship::Subsystem_Flags::No_aggregate, "no_aggregate"},
	{Ship::Subsystem_Flags::Play_sound_for_player, "play_sound_for_player"},
	{Ship::Subsystem_Flags::No_disappear, "no_disappear"},
	{Ship::Subsystem_Flags::Autorepair_if_disabled, "autorepair_if_disabled"},
	{Ship::Subsystem_Flags::No_autorepair_if_disabled, "no_autorepair_if_disabled"},
	{Ship::Subsystem_Flags::Forced_target, "forced_target"},
	{Ship::Subsystem_Flags::Forced_subsys_target, "forced_subsys_target"},
};

struct weapon_flag_entry {
	Ship::Weapon_Flags flag;
	const char* name;
};

// Trigger_down flags are deliberately absent: they describe what the pilot's finger is doing
// this instant, not anything worth carrying across a reload.
const weapon_flag_entry Weapon_flag_table[] = {
	{Ship::Weapon_Flags::Beam_Free, "beam_free"},
	{Ship::Weapon_Flags::Turret_Lock, "turret_lock"},
	{Ship::Weapon_Flags::Tagged_Only, "tagged_only"},
};

struct wing_flag_entry {
	Ship::Wing_Flags flag;
	const char* name;
};

// Wing state that changes during the mission.  Gone and Departing are the ones that matter for
// directives: is-destroyed and friends read them, so a wing that had been wiped out before the
// checkpoint has to come back still wiped out rather than merely empty.  The parse-time flags
// (Ignore_count, Reinforcement, the arrival/departure warp options) are reproduced by the
// mission load and are deliberately absent.
const wing_flag_entry Wing_flag_table[] = {
	{Ship::Wing_Flags::Gone, "gone"},
	{Ship::Wing_Flags::Departing, "departing"},
	{Ship::Wing_Flags::Departure_ordered, "departure_ordered"},
	{Ship::Wing_Flags::Never_existed, "never_existed"},
	{Ship::Wing_Flags::Reset_reinforcement, "reset_reinforcement"},
	{Ship::Wing_Flags::No_dynamic, "no_dynamic"},
	{Ship::Wing_Flags::Nav_carry, "nav_carry"},
	{Ship::Wing_Flags::No_arrival_music, "no_arrival_music"},
	{Ship::Wing_Flags::No_arrival_message, "no_arrival_message"},
	{Ship::Wing_Flags::No_first_wave_message, "no_first_wave_message"},
};

struct ai_flag_entry {
	AI::AI_Flags flag;
	const char* name;
};

// Behaviour the AI is in the middle of.  The docking/repair ones matter most: a ship that was
// awaiting or receiving repair when the checkpoint was taken has to come back still waiting,
// or the support ship it is expecting will never be reconciled with it.
const ai_flag_entry Ai_flag_table[] = {
	{AI::AI_Flags::Formation_wing, "formation_wing"},
	{AI::AI_Flags::Formation_object, "formation_object"},
	{AI::AI_Flags::Awaiting_repair, "awaiting_repair"},
	{AI::AI_Flags::Being_repaired, "being_repaired"},
	{AI::AI_Flags::Repairing, "repairing"},
	{AI::AI_Flags::Repair_obstructed, "repair_obstructed"},
	{AI::AI_Flags::Seek_lock, "seek_lock"},
	{AI::AI_Flags::Temporary_ignore, "temporary_ignore"},
	{AI::AI_Flags::Use_exit_path, "use_exit_path"},
	{AI::AI_Flags::Use_static_path, "use_static_path"},
	{AI::AI_Flags::Target_collision, "target_collision"},
	{AI::AI_Flags::Unload_secondaries, "unload_secondaries"},
	{AI::AI_Flags::Unload_primaries, "unload_primaries"},
	{AI::AI_Flags::On_subsys_path, "on_subsys_path"},
	{AI::AI_Flags::Attack_slowly, "attack_slowly"},
	{AI::AI_Flags::Kamikaze, "kamikaze"},
	{AI::AI_Flags::No_dynamic, "no_dynamic"},
	{AI::AI_Flags::Stealth_pursuit, "stealth_pursuit"},
	{AI::AI_Flags::Trying_unsuccessfully_to_warp, "trying_unsuccessfully_to_warp"},
	{AI::AI_Flags::Free_afterburner_use, "free_afterburner_use"},
	{AI::AI_Flags::Waypoints_no_formation, "waypoints_no_formation"},
};

struct ai_override_flag_entry {
	AI::Maneuver_Override_Flags flag;
	const char* name;
};

// SEXP- and script-driven maneuver overrides.  These persist until their timestamps expire, so
// dropping them would hand control back to the AI in the middle of a scripted manoeuvre.
const ai_override_flag_entry Ai_override_flag_table[] = {
	{AI::Maneuver_Override_Flags::Full_rot, "full_rot"},
	{AI::Maneuver_Override_Flags::Roll, "roll"},
	{AI::Maneuver_Override_Flags::Pitch, "pitch"},
	{AI::Maneuver_Override_Flags::Heading, "heading"},
	{AI::Maneuver_Override_Flags::Full_lat, "full_lat"},
	{AI::Maneuver_Override_Flags::Up, "up"},
	{AI::Maneuver_Override_Flags::Sideways, "sideways"},
	{AI::Maneuver_Override_Flags::Forward, "forward"},
	{AI::Maneuver_Override_Flags::Dont_bank_when_turning, "dont_bank_when_turning"},
	{AI::Maneuver_Override_Flags::Dont_clamp_max_velocity, "dont_clamp_max_velocity"},
	{AI::Maneuver_Override_Flags::Instantaneous_acceleration, "instantaneous_acceleration"},
	{AI::Maneuver_Override_Flags::Lateral_never_expire, "lateral_never_expire"},
	{AI::Maneuver_Override_Flags::Rotational_never_expire, "rotational_never_expire"},
	{AI::Maneuver_Override_Flags::Dont_override_old_maneuvers, "dont_override_old_maneuvers"},
};

struct ai_goal_flag_entry {
	AI::Goal_Flags flag;
	const char* name;
};

// Docker_index_valid / Dockee_index_valid are deliberately absent: the dock points are always
// stored as names, so the restore re-derives those two rather than trusting a saved index.
const ai_goal_flag_entry Ai_goal_flag_table[] = {
	{AI::Goal_Flags::Goal_on_hold, "on_hold"},
	{AI::Goal_Flags::Subsys_needs_fixup, "subsys_needs_fixup"},
	{AI::Goal_Flags::Goal_override, "goal_override"},
	{AI::Goal_Flags::Want_override, "want_override"},
	{AI::Goal_Flags::Purge, "purge"},
	{AI::Goal_Flags::Purge_when_new_goal_added, "purge_when_new_goal_added"},
	{AI::Goal_Flags::Goals_purged, "goals_purged"},
	{AI::Goal_Flags::Depart_sound_played, "depart_sound_played"},
	{AI::Goal_Flags::Target_own_team, "target_own_team"},
	{AI::Goal_Flags::Afterburn_hard, "afterburn_hard"},
	{AI::Goal_Flags::Waypoints_in_reverse, "waypoints_in_reverse"},
	{AI::Goal_Flags::Clear_all_goals_first, "clear_all_goals_first"},
};

// mission_event::flags is a plain int of MEF_ bits rather than a flagset, so it gets its own pair
// of helpers below.  Only the bits that change while the mission runs are listed: the rest
// (MEF_USING_TRIGGER_COUNT, MEF_USE_MSECS) come from the mission file and the mission load has
// already put them back.
struct event_flag_entry {
	int flag;
	const char* name;
};

const event_flag_entry Event_flag_table[] = {
	{MEF_CURRENT, "current"},
	{MEF_DIRECTIVE_SPECIAL, "directive_special"},
	{MEF_DIRECTIVE_TEMP_TRUE, "directive_temp_true"},
	{MEF_TIMESTAMP_HAS_INTERVAL, "timestamp_has_interval"},
	{MEF_EVENT_IS_DONE, "event_is_done"},
};

void collect_int_flags(int flags, SCP_vector<SCP_string>& out)
{
	out.clear();

	for (const auto& entry : Event_flag_table) {
		if (flags & entry.flag) {
			out.emplace_back(entry.name);
		}
	}
}

void apply_int_flags(const SCP_vector<SCP_string>& names, int& flags)
{
	// Clear only what this table covers, so the parse-time bits survive.
	for (const auto& entry : Event_flag_table) {
		flags &= ~entry.flag;
	}

	for (const auto& name : names) {
		bool found = false;
		for (const auto& entry : Event_flag_table) {
			if (!stricmp(name.c_str(), entry.name)) {
				flags |= entry.flag;
				found = true;
				break;
			}
		}
		if (!found) {
			mprintf(("CHECKPOINT => Unknown event flag '%s'; ignoring it.\n", name.c_str()));
		}
	}
}

// The engine's own flag_def_list_new tables are declared as incomplete arrays, so they come with
// an explicit count rather than being deduced like the local tables below.
template <typename FlagType>
void collect_def_flags(const flagset<FlagType>& flags,
	const flag_def_list_new<FlagType>* table,
	size_t count,
	SCP_vector<SCP_string>& out)
{
	out.clear();

	for (size_t i = 0; i < count; i++) {
		if (table[i].in_use && flags[table[i].def]) {
			out.emplace_back(table[i].name);
		}
	}
}

template <typename FlagType>
void apply_def_flags(const SCP_vector<SCP_string>& names,
	const flag_def_list_new<FlagType>* table,
	size_t count,
	flagset<FlagType>& flags)
{
	for (size_t i = 0; i < count; i++) {
		if (table[i].in_use) {
			flags.remove(table[i].def);
		}
	}

	for (const auto& name : names) {
		for (size_t i = 0; i < count; i++) {
			if (table[i].in_use && !stricmp(name.c_str(), table[i].name)) {
				flags.set(table[i].def);
				break;
			}
		}
	}
}

template <typename FlagType, typename EntryType, size_t N>
void collect_flags(const flagset<FlagType>& flags, const EntryType (&table)[N], SCP_vector<SCP_string>& out)
{
	out.clear();

	for (const auto& entry : table) {
		if (flags[entry.flag]) {
			out.emplace_back(entry.name);
		}
	}
}

template <typename FlagType, typename EntryType, size_t N>
void apply_flags(const SCP_vector<SCP_string>& names, const EntryType (&table)[N], flagset<FlagType>& flags)
{
	// Clear only the flags this table covers, so that flags outside its scope -- set by the
	// mission load and none of our business -- survive untouched.
	for (const auto& entry : table) {
		flags.set(entry.flag, false);
	}

	for (const auto& name : names) {
		bool found = false;
		for (const auto& entry : table) {
			if (name == entry.name) {
				flags.set(entry.flag, true);
				found = true;
				break;
			}
		}
		if (!found) {
			mprintf(("CHECKPOINT => Ignoring unknown flag '%s'.\n", name.c_str()));
		}
	}
}

// ------------------------------------------------------------------
// Name lookups
// ------------------------------------------------------------------

SCP_string ship_class_name(int ship_class)
{
	if (ship_class < 0 || ship_class >= static_cast<int>(Ship_info.size())) {
		return SCP_string();
	}
	return Ship_info[ship_class].name;
}

SCP_string weapon_class_name(int weapon_class)
{
	if (weapon_class < 0 || weapon_class >= weapon_info_size()) {
		return SCP_string();
	}
	return Weapon_info[weapon_class].name;
}

SCP_string team_name(int team)
{
	if (team < 0 || team >= static_cast<int>(Iff_info.size())) {
		return SCP_string();
	}
	return Iff_info[team].iff_name;
}

// Look up a name that came out of a checkpoint.  A miss is normal -- the mod may have changed
// since the file was written -- so it logs and lets the caller fall back rather than erroring.
int lookup_ship_class(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}

	int index = ship_info_lookup(name.c_str());
	if (index < 0) {
		mprintf(("CHECKPOINT => Ship class '%s' no longer exists.\n", name.c_str()));
	}
	return index;
}

int lookup_weapon_class(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}

	int index = weapon_info_lookup(name.c_str());
	if (index < 0) {
		mprintf(("CHECKPOINT => Weapon class '%s' no longer exists.\n", name.c_str()));
	}
	return index;
}

// Cargo_names starts out as whatever the mission file declared, but set-cargo appends to it as the
// mission runs, so a cargo index saved late in one run can point past the end of the list a fresh
// parse produces.  Cargo therefore travels by name.  The packed CARGO_NO_DEPLETE bit is a property
// of the assignment rather than of the name, so it is carried separately.
SCP_string cargo_name(int cargo1)
{
	int index = cargo1 & CARGO_INDEX_MASK;
	if (index < 0 || index >= Num_cargo) {
		return SCP_string();
	}
	return Cargo_names[index];
}

// Resolves back to an index, re-adding the name the way sexp_set_cargo does if this run's mission
// has not declared it.  Index 0 is "Nothing", which is what the parse guarantees is always there
// and so is the safe fallback.
int lookup_cargo(const SCP_string& name)
{
	if (name.empty()) {
		return 0;
	}

	for (int i = 0; i < Num_cargo; i++) {
		if (!stricmp(Cargo_names[i], name.c_str())) {
			return i;
		}
	}

	if (Num_cargo + 1 >= MAX_CARGO || name.length() >= NAME_LENGTH) {
		mprintf(("CHECKPOINT => Cannot restore cargo '%s'; using none.\n", name.c_str()));
		return 0;
	}

	int index = Num_cargo++;
	strcpy(Cargo_names[index], name.c_str());
	return index;
}

// Personas are numbered by the order messages.tbl and its tbms happen to be parsed in, so the
// index means nothing once a mod changes.
SCP_string persona_name(int persona_index)
{
	if (persona_index < 0 || persona_index >= static_cast<int>(Personas.size())) {
		return SCP_string();
	}
	return Personas[persona_index].name;
}

int lookup_persona(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}

	int index = message_persona_name_lookup(name.c_str());
	if (index < 0) {
		mprintf(("CHECKPOINT => Persona '%s' no longer exists.\n", name.c_str()));
	}
	return index;
}

// Anchors are an int that is either a ship registry index or a bitfield naming an IFF, so they go
// out as text the same way the mission file stores them.
SCP_string anchor_name(anchor_t anchor)
{
	int value = anchor.value();
	if (value < 0) {
		return SCP_string();
	}

	if (value & ANCHOR_SPECIAL_ARRIVAL) {
		int iff = value & ~(ANCHOR_SPECIAL_ARRIVAL | ANCHOR_SPECIAL_ARRIVAL_PLAYER);
		if (iff < 0 || iff >= static_cast<int>(Iff_info.size())) {
			return SCP_string();
		}

		SCP_string out("<any ");
		out += Iff_info[iff].iff_name;
		if (value & ANCHOR_SPECIAL_ARRIVAL_PLAYER) {
			out += " player";
		}
		out += ">";
		return out;
	}

	auto entry = ship_registry_get(value);
	return entry != nullptr ? SCP_string(entry->name) : SCP_string();
}

anchor_t lookup_anchor(const SCP_string& name)
{
	if (name.empty()) {
		return anchor_t::invalid();
	}

	auto special = get_special_anchor(name.c_str());
	if (special.isValid()) {
		return special;
	}

	// At runtime an anchor is a ship registry index, not the parse-names index get_anchor() would
	// hand back, so resolve it that way.
	int index = ship_registry_get_index(name.c_str());
	return index >= 0 ? anchor_t(index) : anchor_t::invalid();
}

int lookup_team(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}

	int index = iff_lookup(name.c_str());
	if (index < 0) {
		mprintf(("CHECKPOINT => IFF '%s' no longer exists.\n", name.c_str()));
	}
	return index;
}

// ------------------------------------------------------------------
// Timestamp translation
// ------------------------------------------------------------------
//
// Engine timestamps are absolute values in a clock that runs from game launch, not from mission
// start -- Timestamp_offset_from_counter (timer.cpp) is established once on the first unpause
// and only ever adjusted for pauses after that, and timestamp_start_mission() merely records a
// mark into it.  So a stamp saved in one run is meaningless in the next: by the time the mission
// is reloaded the clock has moved on by however long the player was playing, and every saved
// stamp would read as long since elapsed.  Turret and countermeasure timers would merely be
// eager, but a wing's pending wave would arrive instantly and, once mission events are captured,
// every -delay event would fire at once.
//
// Rebasing the global clock to meet the saved stamps is not an option either: everything
// game_post_level_init() and the HUD stamped during the load would then sit minutes in the
// future.  So the stamps come to the clock instead -- each one is shifted by the difference
// between the two runs' clocks, which preserves how far in the future (or past) it was.

int Stamp_delta = 0;

int translate_stamp(int saved)
{
	return mission_checkpoint_translate_stamp(saved, Stamp_delta);
}

// ------------------------------------------------------------------
// Field registry expansion
// ------------------------------------------------------------------

#define CKPT_STORE_FLOAT(field) out[#field] = obj.field;
#define CKPT_STORE_INT(field) out[#field] = static_cast<int>(obj.field);
#define CKPT_STORE_VEC(field) out[#field] = obj.field;

// Reading uses the object's current value as the default, so a field the file does not carry
// simply keeps whatever the fresh mission load produced.
#define CKPT_LOAD_FLOAT(field)                                                                 \
	{                                                                                          \
		auto it = in.find(#field);                                                             \
		if (it != in.end())                                                                    \
			obj.field = it->second;                                                            \
	}
#define CKPT_LOAD_INT(field)                                                                   \
	{                                                                                          \
		auto it = in.find(#field);                                                             \
		if (it != in.end())                                                                    \
			obj.field = static_cast<decltype(obj.field)>(it->second);                           \
	}
#define CKPT_LOAD_VEC(field)                                                                   \
	{                                                                                          \
		auto it = in.find(#field);                                                             \
		if (it != in.end())                                                                    \
			obj.field = it->second;                                                            \
	}
// Stamps ride in the same int map as everything else -- the only difference is that they are
// shifted into the current clock on the way back in.
#define CKPT_LOAD_STAMP(field)                                                                 \
	{                                                                                          \
		auto it = in.find(#field);                                                             \
		if (it != in.end())                                                                    \
			obj.field = static_cast<decltype(obj.field)>(translate_stamp(it->second));          \
	}

void store_physics(const physics_info& obj, SCP_map<SCP_string, float>& out_floats, SCP_map<SCP_string, vec3d>& out_vecs)
{
	{
		auto& out = out_floats;
		CKPT_PHYSICS_FLOATS(CKPT_STORE_FLOAT)
	}
	{
		auto& out = out_vecs;
		CKPT_PHYSICS_VECS(CKPT_STORE_VEC)
	}
}

void load_physics(physics_info& obj, const SCP_map<SCP_string, float>& in_floats,
                  const SCP_map<SCP_string, vec3d>& in_vecs)
{
	{
		const auto& in = in_floats;
		CKPT_PHYSICS_FLOATS(CKPT_LOAD_FLOAT)
	}
	{
		const auto& in = in_vecs;
		CKPT_PHYSICS_VECS(CKPT_LOAD_VEC)
	}
}

void store_ship_scalars(const ship& obj, SCP_map<SCP_string, float>& out_floats, SCP_map<SCP_string, int>& out_ints)
{
	{
		auto& out = out_floats;
		CKPT_SHIP_FLOATS(CKPT_STORE_FLOAT)
	}
	{
		auto& out = out_ints;
		CKPT_SHIP_INTS(CKPT_STORE_INT)
		CKPT_SHIP_STAMPS(CKPT_STORE_INT)
	}
}

void load_ship_scalars(ship& obj, const SCP_map<SCP_string, float>& in_floats, const SCP_map<SCP_string, int>& in_ints)
{
	{
		const auto& in = in_floats;
		CKPT_SHIP_FLOATS(CKPT_LOAD_FLOAT)
	}
	{
		const auto& in = in_ints;
		CKPT_SHIP_INTS(CKPT_LOAD_INT)
		CKPT_SHIP_STAMPS(CKPT_LOAD_STAMP)
	}
}

void store_subsys_scalars(const ship_subsys& obj, SCP_map<SCP_string, float>& out_floats,
                          SCP_map<SCP_string, int>& out_ints)
{
	{
		auto& out = out_floats;
		CKPT_SUBSYS_FLOATS(CKPT_STORE_FLOAT)
	}
	{
		auto& out = out_ints;
		CKPT_SUBSYS_INTS(CKPT_STORE_INT)
		CKPT_SUBSYS_STAMPS(CKPT_STORE_INT)
	}
}

void load_subsys_scalars(ship_subsys& obj, const SCP_map<SCP_string, float>& in_floats,
                         const SCP_map<SCP_string, int>& in_ints)
{
	{
		const auto& in = in_floats;
		CKPT_SUBSYS_FLOATS(CKPT_LOAD_FLOAT)
	}
	{
		const auto& in = in_ints;
		CKPT_SUBSYS_INTS(CKPT_LOAD_INT)
		CKPT_SUBSYS_STAMPS(CKPT_LOAD_STAMP)
	}
}

void store_weapon_scalars(const ship_weapon& obj, SCP_map<SCP_string, int>& out_ints)
{
	auto& out = out_ints;
	CKPT_WEAPONS_INTS(CKPT_STORE_INT)
	CKPT_WEAPONS_STAMPS(CKPT_STORE_INT)
}

void load_weapon_scalars(ship_weapon& obj, const SCP_map<SCP_string, int>& in_ints)
{
	const auto& in = in_ints;
	CKPT_WEAPONS_INTS(CKPT_LOAD_INT)
	CKPT_WEAPONS_STAMPS(CKPT_LOAD_STAMP)
}

void store_wing_scalars(const wing& obj, SCP_map<SCP_string, int>& out_ints)
{
	auto& out = out_ints;
	CKPT_WING_INTS(CKPT_STORE_INT)
	CKPT_WING_STAMPS(CKPT_STORE_INT)
}

void load_wing_scalars(wing& obj, const SCP_map<SCP_string, int>& in_ints)
{
	const auto& in = in_ints;
	CKPT_WING_INTS(CKPT_LOAD_INT)
	CKPT_WING_STAMPS(CKPT_LOAD_STAMP)
}

void store_scoring_scalars(const scoring_struct& obj, SCP_map<SCP_string, int>& out_ints)
{
	auto& out = out_ints;
	CKPT_SCORING_INTS(CKPT_STORE_INT)
}

void load_scoring_scalars(scoring_struct& obj, const SCP_map<SCP_string, int>& in_ints)
{
	const auto& in = in_ints;
	CKPT_SCORING_INTS(CKPT_LOAD_INT)
}

// ------------------------------------------------------------------
// Weapon banks
// ------------------------------------------------------------------

void store_weapons(const ship_weapon& swp, weapon_state& out)
{
	out.primary_banks.clear();
	for (int i = 0; i < swp.num_primary_banks && i < MAX_SHIP_PRIMARY_BANKS; i++) {
		weapon_bank bank;
		bank.weapon_class = weapon_class_name(swp.primary_bank_weapons[i]);
		bank.ammo = swp.primary_bank_ammo[i];
		bank.start_ammo = swp.primary_bank_start_ammo[i];
		bank.capacity = swp.primary_bank_capacity[i];
		bank.next_slot = swp.primary_next_slot[i];
		bank.next_fire_stamp = swp.next_primary_fire_stamp[i];
		bank.last_fire_stamp = swp.last_primary_fire_stamp[i];
		bank.rearm_time = swp.primary_bank_rearm_time[i];
		bank.burst_counter = swp.burst_counter[i];
		bank.burst_seed = swp.burst_seed[i];
		out.primary_banks.push_back(std::move(bank));
	}

	out.secondary_banks.clear();
	for (int i = 0; i < swp.num_secondary_banks && i < MAX_SHIP_SECONDARY_BANKS; i++) {
		weapon_bank bank;
		bank.weapon_class = weapon_class_name(swp.secondary_bank_weapons[i]);
		bank.ammo = swp.secondary_bank_ammo[i];
		bank.start_ammo = swp.secondary_bank_start_ammo[i];
		bank.capacity = swp.secondary_bank_capacity[i];
		bank.next_slot = swp.secondary_next_slot[i];
		bank.next_fire_stamp = swp.next_secondary_fire_stamp[i];
		bank.last_fire_stamp = swp.last_secondary_fire_stamp[i];
		bank.rearm_time = swp.secondary_bank_rearm_time[i];
		bank.burst_counter = swp.burst_counter[MAX_SHIP_PRIMARY_BANKS + i];
		bank.burst_seed = swp.burst_seed[MAX_SHIP_PRIMARY_BANKS + i];
		out.secondary_banks.push_back(std::move(bank));
	}

	// Tertiary banks carry no weapon class of their own in the current engine, so there is
	// nothing to record beyond the ammo counts, which ride along in the scalars below.
	out.tertiary_class.clear();

	collect_flags(swp.flags, Weapon_flag_table, out.flags);
	store_weapon_scalars(swp, out.scalars);
}

// Apply saved bank contents.  Bank count comes from the ship class, not the file: if the class
// has fewer banks than the checkpoint recorded (because the mod changed, or because the player
// is retrying in a different ship) the extra banks are simply dropped.
void load_weapons(ship_weapon& swp, const weapon_state& in, bool restore_classes)
{
	int num_primaries = MIN(swp.num_primary_banks, static_cast<int>(in.primary_banks.size()));
	for (int i = 0; i < num_primaries; i++) {
		const auto& bank = in.primary_banks[i];

		if (restore_classes) {
			int weapon_class = lookup_weapon_class(bank.weapon_class);
			if (weapon_class >= 0) {
				swp.primary_bank_weapons[i] = weapon_class;
			}
		}

		swp.primary_bank_capacity[i] = bank.capacity;
		swp.primary_bank_start_ammo[i] = bank.start_ammo;
		// Clamp rather than trust the file: the bank may be smaller now.
		swp.primary_bank_ammo[i] = MIN(bank.ammo, bank.capacity > 0 ? bank.capacity : bank.ammo);
		swp.primary_next_slot[i] = bank.next_slot;
		swp.next_primary_fire_stamp[i] = translate_stamp(bank.next_fire_stamp);
		swp.last_primary_fire_stamp[i] = translate_stamp(bank.last_fire_stamp);
		swp.primary_bank_rearm_time[i] = translate_stamp(bank.rearm_time);
		swp.burst_counter[i] = bank.burst_counter;
		swp.burst_seed[i] = bank.burst_seed;
	}

	int num_secondaries = MIN(swp.num_secondary_banks, static_cast<int>(in.secondary_banks.size()));
	for (int i = 0; i < num_secondaries; i++) {
		const auto& bank = in.secondary_banks[i];

		if (restore_classes) {
			int weapon_class = lookup_weapon_class(bank.weapon_class);
			if (weapon_class >= 0) {
				swp.secondary_bank_weapons[i] = weapon_class;
			}
		}

		swp.secondary_bank_capacity[i] = bank.capacity;
		swp.secondary_bank_start_ammo[i] = bank.start_ammo;
		swp.secondary_bank_ammo[i] = MIN(bank.ammo, bank.capacity > 0 ? bank.capacity : bank.ammo);
		swp.secondary_next_slot[i] = bank.next_slot;
		swp.next_secondary_fire_stamp[i] = translate_stamp(bank.next_fire_stamp);
		swp.last_secondary_fire_stamp[i] = translate_stamp(bank.last_fire_stamp);
		swp.secondary_bank_rearm_time[i] = translate_stamp(bank.rearm_time);
		swp.burst_counter[MAX_SHIP_PRIMARY_BANKS + i] = bank.burst_counter;
		swp.burst_seed[MAX_SHIP_PRIMARY_BANKS + i] = bank.burst_seed;
	}

	apply_flags(in.flags, Weapon_flag_table, swp.flags);
	load_weapon_scalars(swp, in.scalars);
}

// ------------------------------------------------------------------
// Subsystems
// ------------------------------------------------------------------

const char* subsys_key(const ship_subsys* subsys)
{
	if (subsys->system_info == nullptr) {
		return "";
	}
	return subsys->system_info->subobj_name;
}

// Ships routinely carry several subsystems with the same subobject name, so each one is
// identified by its name plus an ordinal within that name.  Matching that way survives a model
// whose subsystem list order or length has changed, which matching on list position -- what
// the red alert code does -- does not.
SCP_string subsys_lookup_key(const SCP_string& name, int ordinal)
{
	SCP_string key;
	sprintf(key, "%s#%d", name.c_str(), ordinal);
	return key;
}

// Build the name+ordinal index for a ship's live subsystems.  Both the apply pass and the
// turret-target pass need it, and they must agree, so it is built the same way for both.
SCP_map<SCP_string, ship_subsys*> index_subsystems(ship* shipp)
{
	SCP_map<SCP_string, int> ordinals;
	SCP_map<SCP_string, ship_subsys*> live;

	for (auto subsys = GET_FIRST(&shipp->subsys_list); subsys != END_OF_LIST(&shipp->subsys_list);
	     subsys = GET_NEXT(subsys)) {
		SCP_string name = subsys_key(subsys);
		live[subsys_lookup_key(name, ordinals[name]++)] = subsys;
	}

	return live;
}

// ------------------------------------------------------------------
// AI state
// ------------------------------------------------------------------

// An objnum resolved to the ship it points at, or empty for "nothing".  Every AI reference goes
// through this: objnums are handed out in creation order, so the same number means a different
// ship in the next run.
SCP_string ship_name_for_objnum(int objnum)
{
	if (objnum < 0 || objnum >= MAX_OBJECTS) {
		return SCP_string();
	}

	const object* objp = &Objects[objnum];
	if (objp->type != OBJ_SHIP || objp->instance < 0 || objp->instance >= MAX_SHIPS) {
		return SCP_string();
	}

	return SCP_string(Ships[objp->instance].ship_name);
}

// The reverse.  Returns -1 when the ship is gone, which every caller treats as "no reference".
int objnum_for_ship_name(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}

	auto entry = ship_registry_get(name);
	if (entry == nullptr || !entry->has_objp()) {
		return -1;
	}

	return entry->objnum;
}

SCP_string wing_name_for_wingnum(int wingnum)
{
	if (wingnum < 0 || wingnum >= MAX_WINGS) {
		return SCP_string();
	}
	return SCP_string(Wings[wingnum].name);
}

SCP_string waypoint_list_name(int wl_index)
{
	if (wl_index < 0 || !Waypoint_lists.in_bounds(wl_index)) {
		return SCP_string();
	}
	return SCP_string(Waypoint_lists[wl_index].get_name());
}

const char* ai_goal_mode_name(ai_goal_mode mode)
{
	for (int i = 0; i < Num_ai_goals; i++) {
		if (Ai_goal_names[i].def == mode) {
			return Ai_goal_names[i].name;
		}
	}
	return "";
}

bool ai_goal_mode_value(const SCP_string& name, ai_goal_mode& out)
{
	for (int i = 0; i < Num_ai_goals; i++) {
		if (name == Ai_goal_names[i].name) {
			out = Ai_goal_names[i].def;
			return true;
		}
	}
	return false;
}

const char* ai_goal_type_name(ai_goal_type type)
{
	switch (type) {
	case ai_goal_type::EVENT_SHIP:  return "event_ship";
	case ai_goal_type::EVENT_WING:  return "event_wing";
	case ai_goal_type::PLAYER_SHIP: return "player_ship";
	case ai_goal_type::PLAYER_WING: return "player_wing";
	case ai_goal_type::DYNAMIC:     return "dynamic";
	case ai_goal_type::INVALID:
	default:                        return "invalid";
	}
}

ai_goal_type ai_goal_type_value(const SCP_string& name)
{
	if (name == "event_ship")  return ai_goal_type::EVENT_SHIP;
	if (name == "event_wing")  return ai_goal_type::EVENT_WING;
	if (name == "player_ship") return ai_goal_type::PLAYER_SHIP;
	if (name == "player_wing") return ai_goal_type::PLAYER_WING;
	if (name == "dynamic")     return ai_goal_type::DYNAMIC;
	return ai_goal_type::INVALID;
}

// The field macros below expand to references to `obj` and to a map called `out`, so each
// block rebinds `out` to the map it is filling.  The parameter is named `state` so that
// rebinding does not shadow it.
void store_ai_scalars(const ai_info& obj, ai_state& state)
{
	{
		auto& out = state.ints;
		CKPT_AI_INTS(CKPT_STORE_INT)
	}
	{
		auto& out = state.floats;
		CKPT_AI_FLOATS(CKPT_STORE_FLOAT)
	}
	{
		auto& out = state.mission_times;
		CKPT_AI_MISSION_TIMES(CKPT_STORE_INT)
	}
	{
		auto& out = state.stamps;
		CKPT_AI_STAMPS(CKPT_STORE_INT)
	}
}

void load_ai_scalars(ai_info& obj, const ai_state& state)
{
	{
		const auto& in = state.ints;
		CKPT_AI_INTS(CKPT_LOAD_INT)
	}
	{
		const auto& in = state.floats;
		CKPT_AI_FLOATS(CKPT_LOAD_FLOAT)
	}
	{
		const auto& in = state.mission_times;
		CKPT_AI_MISSION_TIMES(CKPT_LOAD_INT)
	}
	{
		const auto& in = state.stamps;
		CKPT_AI_STAMPS(CKPT_LOAD_STAMP)
	}
}

void store_ai_goal(const ship* shipp, const ai_goal& goal, ai_goal_state& out)
{
	out.mode = ai_goal_mode_name(goal.ai_mode);
	out.type = ai_goal_type_name(goal.type);
	collect_flags(goal.flags, Ai_goal_flag_table, out.flags);

	out.signature = goal.signature;
	out.submode = goal.ai_submode;
	out.priority = goal.priority;
	out.time = goal.time;
	out.int_data = goal.int_data;
	out.float_data = goal.float_data;

	if (goal.target_name != nullptr) {
		out.target_name = goal.target_name;
	}

	out.waypoint_list = waypoint_list_name(goal.wp_list_index);

	// For this one mode the submode is a ship class index rather than an AIS_* constant, so the
	// number on its own would survive a table change pointing at the wrong class.
	if (goal.ai_mode == AI_GOAL_CHASE_SHIP_CLASS) {
		out.submode_ship_class = ship_class_name(goal.ai_submode);
	}

	// Dock points are held as either a name or an index into the model's dock point list,
	// depending on the goal flags.  Write the name either way, so the restore never has to trust
	// an index into a model that may have changed underneath it.  The docker index indexes the
	// model of the ship holding the goal; the dockee index indexes the target's model.
	if (goal.flags[AI::Goal_Flags::Docker_index_valid]) {
		int modelnum = Ship_info[shipp->ship_info_index].model_num;
		const char* name = (modelnum >= 0) ? model_get_dock_name(modelnum, goal.docker.index) : nullptr;
		if (name != nullptr) {
			out.docker_point = name;
		}
	} else if (goal.docker.name != nullptr) {
		out.docker_point = goal.docker.name;
	}

	if (goal.flags[AI::Goal_Flags::Dockee_index_valid]) {
		int target_shipnum = (goal.target_name != nullptr) ? ship_name_lookup(goal.target_name) : -1;
		if (target_shipnum >= 0) {
			int modelnum = Ship_info[Ships[target_shipnum].ship_info_index].model_num;
			const char* name = (modelnum >= 0) ? model_get_dock_name(modelnum, goal.dockee.index) : nullptr;
			if (name != nullptr) {
				out.dockee_point = name;
			}
		}
	} else if (goal.dockee.name != nullptr) {
		out.dockee_point = goal.dockee.name;
	}
}

// The subsys_status list a parse object carries is where a not-yet-arrived ship's per-subsystem
// damage and weapon loadout live.  wl_update_parse_object_weapons() writes into it when a
// loadout is committed, and SEXPs that alter an unarrived wing's loadout write into it too, so
// it is genuine runtime state and not just a copy of the mission file.
void store_parse_subsystems(const p_object* p_objp, SCP_vector<parse_subsys_state>& out)
{
	out.clear();

	if (p_objp->subsys_index < 0 || p_objp->subsys_count <= 0) {
		return;
	}

	for (int i = 0; i < p_objp->subsys_count; i++) {
		const subsys_status* sssp = &Subsys_status[p_objp->subsys_index + i];

		parse_subsys_state state;
		state.name = sssp->name;
		state.percent = sssp->percent;
		state.ai_class = sssp->ai_class;
		state.cargo = cargo_name(sssp->subsys_cargo_name);
		state.cargo_title = sssp->subsys_cargo_title;

		for (int j = 0; j < MAX_SHIP_PRIMARY_BANKS; j++) {
			state.primary_banks.push_back(weapon_class_name(sssp->primary_banks[j]));
			state.primary_ammo.push_back(sssp->primary_ammo[j]);
		}
		for (int j = 0; j < MAX_SHIP_SECONDARY_BANKS; j++) {
			state.secondary_banks.push_back(weapon_class_name(sssp->secondary_banks[j]));
			state.secondary_ammo.push_back(sssp->secondary_ammo[j]);
		}

		out.push_back(std::move(state));
	}
}

// Matched by name rather than by position: the fresh load builds its own subsys_status list, and
// a mod change can alter how many entries a ship gets.
void load_parse_subsystems(p_object* p_objp, const SCP_vector<parse_subsys_state>& in)
{
	if (p_objp->subsys_index < 0 || p_objp->subsys_count <= 0) {
		return;
	}

	for (const auto& state : in) {
		subsys_status* sssp = nullptr;
		for (int i = 0; i < p_objp->subsys_count; i++) {
			if (!subsystem_stricmp(Subsys_status[p_objp->subsys_index + i].name, state.name.c_str())) {
				sssp = &Subsys_status[p_objp->subsys_index + i];
				break;
			}
		}

		if (sssp == nullptr) {
			mprintf(("CHECKPOINT => '%s' has no parse subsystem '%s' any more; skipping it.\n",
			         p_objp->name,
			         state.name.c_str()));
			continue;
		}

		sssp->percent = state.percent;
		sssp->ai_class = state.ai_class;
		if (!state.cargo.empty()) {
			sssp->subsys_cargo_name = lookup_cargo(state.cargo);
		}
		if (!state.cargo_title.empty()) {
			strcpy_s(sssp->subsys_cargo_title, state.cargo_title.c_str());
		}

		for (int j = 0; j < MAX_SHIP_PRIMARY_BANKS && j < static_cast<int>(state.primary_banks.size()); j++) {
			// An empty name means the bank was empty, which is -1 rather than a lookup failure.
			sssp->primary_banks[j] =
				state.primary_banks[j].empty() ? -1 : lookup_weapon_class(state.primary_banks[j]);
			sssp->primary_ammo[j] = state.primary_ammo[j];
		}
		for (int j = 0; j < MAX_SHIP_SECONDARY_BANKS && j < static_cast<int>(state.secondary_banks.size()); j++) {
			sssp->secondary_banks[j] =
				state.secondary_banks[j].empty() ? -1 : lookup_weapon_class(state.secondary_banks[j]);
			sssp->secondary_ammo[j] = state.secondary_ammo[j];
		}
	}
}

// The asteroid field is regenerated by asteroid_create_all() on every mission load, at random
// positions and always intact.  A restored mission therefore got a differently shaped field with
// every asteroid the player had already destroyed put back in it.
SCP_string asteroid_type_name(int asteroid_type)
{
	if (asteroid_type < 0 || asteroid_type >= static_cast<int>(Asteroid_info.size())) {
		return SCP_string();
	}
	return SCP_string(Asteroid_info[asteroid_type].name);
}

int lookup_asteroid_type(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}

	for (int i = 0; i < static_cast<int>(Asteroid_info.size()); i++) {
		if (!stricmp(Asteroid_info[i].name, name.c_str())) {
			return i;
		}
	}

	mprintf(("CHECKPOINT => Asteroid type '%s' no longer exists.\n", name.c_str()));
	return -1;
}

// ------------------------------------------------------------------
// Support ships
// ------------------------------------------------------------------

// An anchor is either an index into the ship registry or one of the ANCHOR_SPECIAL_* flag
// values.  The index is not stable across a reload -- support ships append to the registry as
// they are called in -- so a ship anchor is stored by name and only the flag values as numbers.
void store_anchor(anchor_t anchor, SCP_string& out_ship, int& out_special)
{
	out_ship.clear();
	out_special = -1;

	if (!anchor.isValid()) {
		return;
	}

	int value = anchor.value();
	if (value & (ANCHOR_SPECIAL_ARRIVAL | ANCHOR_SPECIAL_ARRIVAL_PLAYER | ANCHOR_IS_PARSE_NAMES_INDEX)) {
		out_special = value;
		return;
	}

	if (value >= 0 && value < static_cast<int>(Ship_registry.size())) {
		out_ship = Ship_registry[value].name;
	}
}

anchor_t load_anchor(const SCP_string& ship, int special)
{
	if (special != -1) {
		return anchor_t(special);
	}
	if (ship.empty()) {
		return anchor_t::invalid();
	}

	int index = ship_registry_get_index(ship.c_str());
	return (index < 0) ? anchor_t::invalid() : anchor_t(index);
}

SCP_string arrival_location_name(ArrivalLocation location)
{
	int index = static_cast<int>(location);
	if (index < 0 || index >= MAX_ARRIVAL_NAMES) {
		return SCP_string();
	}
	return SCP_string(Arrival_location_names[index]);
}

SCP_string departure_location_name(DepartureLocation location)
{
	int index = static_cast<int>(location);
	if (index < 0 || index >= MAX_DEPARTURE_NAMES) {
		return SCP_string();
	}
	return SCP_string(Departure_location_names[index]);
}

// ------------------------------------------------------------------
// Mission state
// ------------------------------------------------------------------

// The training registry names globals rather than members of a struct, so it needs its own pair of
// expansions; the list itself works exactly like the others.
#define CKPT_STORE_GLOBAL_INT(field) out[#field] = static_cast<int>(field);
#define CKPT_LOAD_GLOBAL_INT(field)                                                            \
	{                                                                                          \
		auto it = in.find(#field);                                                             \
		if (it != in.end())                                                                    \
			field = static_cast<decltype(field)>(it->second);                                  \
	}

void store_player_scalars(const player& obj, SCP_map<SCP_string, int>& out)
{
	CKPT_PLAYER_INTS(CKPT_STORE_INT)
	CKPT_PLAYER_STAMPS(CKPT_STORE_INT)
}

void load_player_scalars(player& obj, const SCP_map<SCP_string, int>& in)
{
	CKPT_PLAYER_INTS(CKPT_LOAD_INT)
	CKPT_PLAYER_STAMPS(CKPT_LOAD_STAMP)
}

void store_training_scalars(SCP_map<SCP_string, int>& out)
{
	CKPT_TRAINING_INTS(CKPT_STORE_GLOBAL_INT)
}

void load_training_scalars(const SCP_map<SCP_string, int>& in)
{
	CKPT_TRAINING_INTS(CKPT_LOAD_GLOBAL_INT)
}

// Mission state that belongs to no ship: the built-in message budget, the personas already spoken
// for, the mission mood, the training context and the reinforcement allowances.
void store_mission_extras(mission_extra_state& out)
{
	out.present = true;

	if (Player != nullptr) {
		store_player_scalars(*Player, out.player_ints);
	}

	store_training_scalars(out.training_ints);
	out.training_context_speed_timestamp = Training_context_speed_timestamp.value();

	// Personas are marked used as the mission hands them out, and cleared on load, so without
	// this a restored mission can give the same voice to a second ship.
	for (const auto& persona : Personas) {
		if (persona.flags & PERSONA_FLAG_USED) {
			out.used_personas.emplace_back(persona.name);
		}
	}

	out.mission_mood = Current_mission_mood;
	out.no_builtin_msgs = The_mission.flags[Mission::Mission_Flags::No_builtin_msgs];
	out.no_builtin_command = The_mission.flags[Mission::Mission_Flags::No_builtin_command];

	for (const auto& reinforcement : Reinforcements) {
		reinforcement_state state;
		state.name = reinforcement.name;
		state.num_uses = reinforcement.num_uses;
		state.available = (reinforcement.flags & RF_IS_AVAILABLE) != 0;
		out.reinforcements.push_back(std::move(state));
	}
}

void apply_mission_extras(const checkpoint_data& data)
{
	const auto& state = data.mission;

	// As with the environment, an absent section has to mean "says nothing" rather than "all
	// zeroes" -- zero built-in messages used is a real value.
	if (!state.present) {
		return;
	}

	if (Player != nullptr) {
		load_player_scalars(*Player, state.player_ints);
	}

	load_training_scalars(state.training_ints);
	Training_context_speed_timestamp = TIMESTAMP(translate_stamp(state.training_context_speed_timestamp));

	// Rebuilt rather than merged: the file lists every persona that had been spoken for, so one
	// that is absent has to come back unused.
	for (auto& persona : Personas) {
		persona.flags &= ~PERSONA_FLAG_USED;
	}
	for (const auto& name : state.used_personas) {
		int index = lookup_persona(name);
		if (index >= 0) {
			Personas[index].flags |= PERSONA_FLAG_USED;
		}
	}

	Current_mission_mood = state.mission_mood;
	The_mission.flags.set(Mission::Mission_Flags::No_builtin_msgs, state.no_builtin_msgs);
	The_mission.flags.set(Mission::Mission_Flags::No_builtin_command, state.no_builtin_command);

	// Matched by name, because Reinforcements is built by the mission parse in the order the file
	// lists them and a reinforcement that has been renamed is a different one.
	for (const auto& saved : state.reinforcements) {
		auto it = std::find_if(Reinforcements.begin(),
			Reinforcements.end(),
			[&saved](const reinforcements& live) { return lcase_equal(live.name, saved.name); });

		if (it == Reinforcements.end()) {
			mprintf(("CHECKPOINT => Reinforcement '%s' is no longer in this mission.\n", saved.name.c_str()));
			continue;
		}

		it->num_uses = saved.num_uses;
		if (saved.available) {
			it->flags |= RF_IS_AVAILABLE;
		} else {
			it->flags &= ~RF_IS_AVAILABLE;
		}
	}
}

// Motion_debris_ptr points into one of the Motion_debris_info entries rather than naming it, so
// the name is recovered by finding the entry it points into.
SCP_string motion_debris_name()
{
	if (Motion_debris_ptr == nullptr) {
		return SCP_string();
	}

	for (const auto& info : Motion_debris_info) {
		if (info.bitmaps == Motion_debris_ptr) {
			return info.name;
		}
	}

	return SCP_string();
}

void store_support(environment_state& out)
{
	const auto& support = The_mission.support_ships;

	out.support_ship_class = ship_class_name(support.ship_class);
	out.support_arrival_location = arrival_location_name(support.arrival_location);
	out.support_departure_location = departure_location_name(support.departure_location);
	store_anchor(support.arrival_anchor, out.support_arrival_anchor_ship, out.support_arrival_anchor_special);
	store_anchor(support.departure_anchor, out.support_departure_anchor_ship, out.support_departure_anchor_special);

	out.support_max_ships = support.max_support_ships;
	out.support_max_concurrent = support.max_concurrent_ships;
	out.support_tally = support.tally;
	out.support_available_for_species = support.support_available_for_species;
	out.support_max_hull_repair = support.max_hull_repair_val;
	out.support_max_subsys_repair = support.max_subsys_repair_val;
	out.support_disallow_rearm = support.disallow_rearm;

	for (const auto& pool : support.rearm_weapon_pool) {
		SCP_map<SCP_string, int> named;
		for (const auto& item : pool) {
			SCP_string name = weapon_class_name(item.first);
			if (!name.empty()) {
				named[name] = item.second;
			}
		}
		out.rearm_pools.push_back(std::move(named));
	}
}

void apply_support(const environment_state& in)
{
	auto& support = The_mission.support_ships;

	// An empty class name means "work it out from the requester's species", which is what -1
	// spells; that is also what a class the mod no longer has should fall back to.
	support.ship_class = in.support_ship_class.empty() ? -1 : lookup_ship_class(in.support_ship_class);

	for (int i = 0; i < MAX_ARRIVAL_NAMES; i++) {
		if (in.support_arrival_location == Arrival_location_names[i]) {
			support.arrival_location = static_cast<ArrivalLocation>(i);
			break;
		}
	}
	for (int i = 0; i < MAX_DEPARTURE_NAMES; i++) {
		if (in.support_departure_location == Departure_location_names[i]) {
			support.departure_location = static_cast<DepartureLocation>(i);
			break;
		}
	}

	support.arrival_anchor = load_anchor(in.support_arrival_anchor_ship, in.support_arrival_anchor_special);
	support.departure_anchor = load_anchor(in.support_departure_anchor_ship, in.support_departure_anchor_special);

	support.max_support_ships = in.support_max_ships;
	support.max_concurrent_ships = in.support_max_concurrent;
	support.tally = in.support_tally;
	support.support_available_for_species = in.support_available_for_species;
	support.max_hull_repair_val = in.support_max_hull_repair;
	support.max_subsys_repair_val = in.support_max_subsys_repair;
	support.disallow_rearm = in.support_disallow_rearm;

	// Rebuilt rather than merged, because the file lists everything that was left in the pool:
	// a weapon that has been spent to nothing has to come back spent rather than keeping
	// whatever the mission file stocked.  Only teams the checkpoint actually has are touched, so
	// a file written before the pools were stored leaves the mission's own pools alone.
	for (size_t team = 0; team < in.rearm_pools.size() && team < support.rearm_weapon_pool.size(); team++) {
		auto& pool = support.rearm_weapon_pool[team];
		pool.clear();

		for (const auto& item : in.rearm_pools[team]) {
			int weapon_class = lookup_weapon_class(item.first);
			if (weapon_class >= 0) {
				pool[weapon_class] = item.second;
			}
		}
	}
}

// The world that is not made of ships: the sky, the nebula, the music, the HUD toggles, the
// support ship settings, the nav points and the jump nodes.
//
// Almost none of this lives in The_mission.  It lives in module-level globals whose only reset is
// that module's level_init, which a restore runs -- so every SEXP that dresses the mission is
// undone by the reload unless it is written down here.
void store_environment(environment_state& out)
{
	out.present = true;

	if (Nmodel_num >= 0) {
		auto pm = model_get(Nmodel_num);
		if (pm != nullptr) {
			out.skybox_model = pm->filename;
		}
	}
	if (Nmodel_bitmap >= 0) {
		const char* texture = bm_get_filename(Nmodel_bitmap);
		if (texture != nullptr) {
			out.skybox_texture = texture;
		}
	}
	// Nmodel_flags is 64-bit and the handler writes 32 at a time.
	out.skybox_flags_hi = static_cast<uint>(Nmodel_flags >> 32);
	out.skybox_flags_lo = static_cast<uint>(Nmodel_flags & 0xffffffffu);
	out.skybox_alpha = Nmodel_alpha;
	out.skybox_orient = Nmodel_orient;

	out.ambient_light = The_mission.ambient_light_level;

	out.fullneb = The_mission.flags[Mission::Mission_Flags::Fullneb];
	out.neb_range = Neb2_awacs;
	out.neb_pattern = Neb2_texture_name;
	out.neb_fog_color_override = The_mission.flags[Mission::Mission_Flags::Neb2_fog_color_override];
	out.neb_fog_r = Neb2_fog_color[0];
	out.neb_fog_g = Neb2_fog_color[1];
	out.neb_fog_b = Neb2_fog_color[2];

	out.subspace = The_mission.flags[Mission::Mission_Flags::Subspace];

	out.background_index = Cur_background;

	// Suns first, then bitmaps, which is the order stars_load_background() adds them in and so
	// the order the restore puts them back in.
	for (int pass = 0; pass < 2; pass++) {
		bool is_sun = (pass == 0);
		int count = stars_get_num_entries(is_sun, false);

		for (int i = 0; i < count; i++) {
			const char* name = stars_get_name_from_instance(i, is_sun);
			if (name == nullptr || *name == '\0') {
				// An instance marked unused keeps its slot but loses its name; it is not in the
				// sky any more, so it does not go in the file either.
				continue;
			}

			starfield_list_entry sle;
			stars_get_data(is_sun, i, sle);

			starfield_entry_state entry;
			entry.name = name;
			entry.is_sun = is_sun;
			entry.scale_x = sle.scale_x;
			entry.scale_y = sle.scale_y;
			entry.div_x = sle.div_x;
			entry.div_y = sle.div_y;
			entry.ang = sle.ang;

			out.starfield.push_back(std::move(entry));
		}
	}

	out.motion_debris_override = Motion_debris_override;
	out.motion_debris_type = motion_debris_name();

	if (Current_soundtrack_num >= 0 && Current_soundtrack_num < static_cast<int>(Soundtracks.size())) {
		out.soundtrack = Soundtracks[Current_soundtrack_num].name;
	}
	out.music_battle_started = (Event_Music_battle_started != 0);

	out.hud_draw = (HUD_draw != 0);
	out.hud_disable_except_messages = (hud_disabled_except_messages() != 0);
	out.hud_max_targeting_range = Hud_max_targeting_range;
	out.hud_display_warpout = Sexp_hud_display_warpout;

	store_support(out);

	out.no_traitor = The_mission.flags[Mission::Mission_Flags::No_traitor];
	if (The_mission.traitor_override != nullptr) {
		out.traitor_override = The_mission.traitor_override->name;
	}
	out.debriefing_persona = persona_name(The_mission.debriefing_persona);

	out.asteroids_enabled = (Asteroids_enabled != 0);

	// Navpoints go out whole, unused slots included, since a nav is identified by its slot.
	for (int i = 0; i < MAX_NAVPOINTS; i++) {
		const NavPoint& nav = Navs[i];

		navpoint_state state;
		state.name = nav.m_NavName;
		state.flags = nav.flags;

		// target_index means different things depending on the type, and neither meaning
		// survives a reload as a number.
		if (nav.flags & NP_WAYPOINT) {
			auto wp_list = find_waypoint_list_at_index(nav.target_index);
			if (wp_list != nullptr) {
				state.target = wp_list->get_name();
			}
			state.waypoint_num = nav.waypoint_num;
		} else if (nav.flags & NP_SHIP) {
			state.target = ship_name_for_objnum(nav.target_index);
		}

		for (int c = 0; c < 3; c++) {
			state.normal_color[c] = nav.normal_color[c];
			state.visited_color[c] = nav.visited_color[c];
		}

		out.navpoints.push_back(std::move(state));
	}
	out.current_nav = CurrentNav;

	for (size_t i = 0; i < Jump_nodes.size(); i++) {
		const auto& node = Jump_nodes[i];

		jump_node_state state;
		state.index = static_cast<int>(i);
		state.name = node.GetName();
		state.hidden = node.IsHidden();
		state.colored = node.IsColored();

		if (node.HasDisplayName()) {
			state.display_name = node.GetDisplayName();
		}
		if (node.IsSpecialModel()) {
			state.model = node.GetModelFilename();
		}
		if (state.colored) {
			const color& c = node.GetColor();
			state.color[0] = c.red;
			state.color[1] = c.green;
			state.color[2] = c.blue;
			state.color[3] = c.alpha;
		}

		out.jump_nodes.push_back(std::move(state));
	}

	for (int i = 0; i < MAX_SQUADRON_WINGS; i++) {
		out.squadron_wings.emplace_back(Squadron_wings[i] >= 0 ? Wings[Squadron_wings[i]].name : "");
	}
}

// Put the sky, the nebula, the music and the rest of the mission's dressing back.
//
// Everything here goes through the engine's own setters rather than by bashing the globals,
// because most of them own derived state: stars_set_background_model() reloads the model and
// rebuilds the environment map, stars_set_nebula() moves the render mode and the HUD contrast
// with it, and neb2_post_level_init() rebuilds the fog.
void apply_environment(const checkpoint_data& data)
{
	const auto& env = data.environment;

	// A checkpoint written before this section existed says nothing about the sky, and saying
	// nothing must leave it alone rather than blank it.
	if (!env.present) {
		return;
	}

	// The nebula first, because turning it on rewrites the render mode and the background model
	// that the skybox settings below then override.
	stars_set_nebula(env.fullneb, env.neb_range);

	if (env.fullneb) {
		strcpy_s(Neb2_texture_name, env.neb_pattern.c_str());
		The_mission.flags.set(Mission::Mission_Flags::Neb2_fog_color_override, env.neb_fog_color_override);
		if (env.neb_fog_color_override) {
			Neb2_fog_color[0] = static_cast<ubyte>(env.neb_fog_r);
			Neb2_fog_color[1] = static_cast<ubyte>(env.neb_fog_g);
			Neb2_fog_color[2] = static_cast<ubyte>(env.neb_fog_b);
		}
		neb2_post_level_init(env.neb_fog_color_override);
	}

	// Subspace: the flag and the visual, but not the ambient sound, which is a sound handle and
	// so on the list of things this system does not carry.
	Game_subspace_effect = env.subspace ? 1 : 0;
	The_mission.flags.set(Mission::Mission_Flags::Subspace, env.subspace);
	stars_set_dynamic_environment(env.subspace);

	std::uint64_t skybox_flags =
		(static_cast<std::uint64_t>(env.skybox_flags_hi) << 32) | static_cast<std::uint64_t>(env.skybox_flags_lo);

	stars_set_background_model(env.skybox_model.empty() ? nullptr : env.skybox_model.c_str(),
		env.skybox_texture.empty() ? nullptr : env.skybox_texture.c_str(),
		skybox_flags,
		env.skybox_alpha);
	stars_set_background_orientation(&env.skybox_orient);

	The_mission.ambient_light_level = env.ambient_light;
	gr_set_ambient_light((env.ambient_light & 0x0000ff),
		(env.ambient_light & 0x00ff00) >> 8,
		(env.ambient_light & 0xff0000) >> 16);

	// Clearing and rebuilding, rather than diffing: stars_load_background(-1) is the public way
	// to empty the live sun and bitmap lists, and every entry that should be in the sky is in
	// the file.
	stars_load_background(-1);
	Cur_background = env.background_index;

	for (const auto& entry : env.starfield) {
		starfield_list_entry sle;
		strcpy_s(sle.filename, entry.name.c_str());
		sle.scale_x = entry.scale_x;
		sle.scale_y = entry.scale_y;
		sle.div_x = entry.div_x;
		sle.div_y = entry.div_y;
		sle.ang = entry.ang;

		int added = entry.is_sun ? stars_add_sun_entry(&sle) : stars_add_bitmap_entry(&sle);
		if (added < 0) {
			mprintf(("CHECKPOINT => The sky had %s '%s', which this build does not have; leaving "
			         "it out.\n",
			         entry.is_sun ? "a sun" : "a bitmap",
			         entry.name.c_str()));
		}
	}

	if (!env.motion_debris_type.empty()) {
		stars_load_debris(The_mission.flags[Mission::Mission_Flags::Fullneb] ? 1 : 0, env.motion_debris_type);
	}
	Motion_debris_override = env.motion_debris_override;

	// The mission parse has already put its own soundtrack in place, so there is only work to do
	// if a SEXP had changed it.  Go through the same entry point the change-soundtrack SEXP uses:
	// assigning Current_soundtrack_num by hand would leave the previous track's patterns open and
	// still playing.
	int soundtrack_index = env.soundtrack.empty() ? -1 : event_music_get_soundtrack_index(env.soundtrack.c_str());
	if (soundtrack_index >= 0 && soundtrack_index != Current_soundtrack_num) {
		event_sexp_change_soundtrack(env.soundtrack.c_str());

		// If the music level was not inited yet, the call above bailed out after clearing the
		// index.  Put it back so whatever starts the level later picks up the right track.
		if (Current_soundtrack_num < 0) {
			Current_soundtrack_num = soundtrack_index;
		}
	}

	// Ask for the battle song rather than setting the flag, which only means "already kicked
	// off": forcing it on over the opening track would keep combat music from ever coming in.
	if (env.music_battle_started) {
		event_music_battle_start();
	}

	hud_set_draw(env.hud_draw ? 1 : 0);
	hud_disable_except_messages(env.hud_disable_except_messages ? 1 : 0);
	Hud_max_targeting_range = env.hud_max_targeting_range;
	// Anything above 1 is a stamp saying when the warpout gauge stops being forced on; 0 and 1
	// are plain off and on, and translate_stamp() leaves them alone.
	Sexp_hud_display_warpout = translate_stamp(env.hud_display_warpout);

	apply_support(env);

	The_mission.flags.set(Mission::Mission_Flags::No_traitor, env.no_traitor);
	The_mission.traitor_override =
		env.traitor_override.empty() ? nullptr : get_traitor_override_pointer(env.traitor_override);
	int debrief_persona = lookup_persona(env.debriefing_persona);
	if (debrief_persona >= 0) {
		The_mission.debriefing_persona = debrief_persona;
	}

	// Only the toggle.  The rocks themselves are restored separately; see apply_asteroids().
	Asteroids_enabled = env.asteroids_enabled ? 1 : 0;

	for (size_t i = 0; i < env.navpoints.size() && i < static_cast<size_t>(MAX_NAVPOINTS); i++) {
		const auto& state = env.navpoints[i];
		NavPoint& nav = Navs[i];

		if (state.name.empty()) {
			nav.clear();
			continue;
		}

		strcpy_s(nav.m_NavName, state.name.c_str());
		nav.flags = state.flags;
		nav.waypoint_num = state.waypoint_num;
		nav.target_index = -1;

		if (state.flags & NP_WAYPOINT) {
			nav.target_index = find_matching_waypoint_list_index(state.target.c_str());
		} else if (state.flags & NP_SHIP) {
			nav.target_index = objnum_for_ship_name(state.target);
		}

		// A nav whose target has gone is cleared rather than left pointing at nothing, since the
		// autopilot code dereferences it.
		if ((state.flags & NP_VALIDTYPE) && nav.target_index < 0) {
			mprintf(("CHECKPOINT => Navpoint '%s' has lost what it pointed at; dropping it.\n",
			         state.name.c_str()));
			nav.clear();
			continue;
		}

		for (int c = 0; c < 3; c++) {
			nav.normal_color[c] = static_cast<ubyte>(state.normal_color[c]);
			nav.visited_color[c] = static_cast<ubyte>(state.visited_color[c]);
		}
	}
	// Only point at a nav that survived; the autopilot indexes Navs[CurrentNav] without checking.
	CurrentNav = -1;
	if (env.current_nav >= 0 && env.current_nav < MAX_NAVPOINTS && Navs[env.current_nav].m_NavName[0] != '\0') {
		CurrentNav = env.current_nav;
	}

	// Autopilot is deliberately not resumed.  Half of what it needs is the flight path it had
	// worked out, which is not stored, and a restore that drops the player into a half-engaged
	// autopilot flying nowhere is worse than one that hands the controls back.
	AutoPilotEngaged = false;

	for (const auto& state : env.jump_nodes) {
		if (state.index < 0 || state.index >= static_cast<int>(Jump_nodes.size())) {
			continue;
		}

		auto& node = Jump_nodes[state.index];

		node.SetName(state.name.c_str());
		// Passing the node's own name is how the display name is cleared, and an empty string is
		// not the same thing -- that would leave the node flagged as having a blank display name.
		node.SetDisplayName(state.display_name.empty() ? state.name.c_str() : state.display_name.c_str());
		node.SetVisibility(!state.hidden);

		if (state.colored) {
			node.SetAlphaColor(state.color[0], state.color[1], state.color[2], state.color[3]);
		}

		if (state.model.empty()) {
			node.ResetToDefaultModel();
		} else {
			node.SetModel(state.model.c_str());
		}
	}

	// Go through the same call the set-squadron-wings SEXP makes rather than assigning
	// Squadron_wings directly: it also moves each ship's wing_status_wing_index and carries the
	// per-slot gauge state over to the slot its wing has moved to.
	if (!env.squadron_wings.empty()) {
		std::array<int, MAX_SQUADRON_WINGS> wingnums;
		bool changed = false;

		for (int i = 0; i < MAX_SQUADRON_WINGS; i++) {
			const char* name = i < static_cast<int>(env.squadron_wings.size()) ? env.squadron_wings[i].c_str() : "";
			wingnums[i] = (*name == '\0') ? -1 : wing_name_lookup(name);

			if (wingnums[i] != Squadron_wings[i]) {
				changed = true;
			}
		}

		if (changed) {
			hud_set_new_squadron_wings(wingnums);
		}
	}
}

// Create the ships that the mission file will not recreate for us.
//
// A support ship is not parsed: mission_bring_in_support_ship() builds a p_object on the fly,
// hands it to the arrival code and throws it away afterwards.  After a restart there is no parse
// object for it, no arrival cue that will ever come true and no registry entry -- so a support
// ship the player had called in simply would not be there, and every reference to it by name,
// including the rearm goal of the ship it was repairing, would come up empty.
//
// So the p_object is built here the same way mission_bring_in_support_ship() builds it, but with
// the saved name and the saved position instead of a generated name and a warp-in point, and
// handed straight to parse_create_object().  Support_ship_pobj and Arriving_support_ship are the
// globals the engine itself uses for this, and parse_create_object_sub() recognises that pairing
// as the one case where a ship legitimately has no entry in Parse_objects.
//
// parse_create_object() skips the warp-in effect while Game_restoring is set, which is what we
// want: the ship was already in the mission when the checkpoint was taken.
//
// A support ship that had already been destroyed or had departed is not recreated.  There is
// nothing left of it to restore onto, and what the mission can still ask about it -- the log,
// and any event that keyed off it -- is restored in its own right.
void restore_dynamic_ships(const checkpoint_data& data)
{
	int created = 0;

	for (const auto& state : data.ships) {
		if (!state.no_parse_object || state.disposition != ShipDisposition::Present) {
			continue;
		}
		if (ship_registry_get(state.name) != nullptr) {
			// Already here, which should not happen for a ship with no parse object, but if it
			// is then apply_ship() will handle it like any other.
			continue;
		}

		int ship_class = lookup_ship_class(state.ship_class);
		if (ship_class < 0) {
			mprintf(("CHECKPOINT => No ship class '%s' any more; cannot recreate '%s'.\n",
			         state.ship_class.c_str(),
			         state.name.c_str()));
			continue;
		}

		auto sip = &Ship_info[ship_class];

		Support_ship_pobj = p_object();
		Arriving_support_ship = &Support_ship_pobj;
		Num_arriving_repair_targets = 0;

		p_object* pobj = Arriving_support_ship;
		strcpy_s(pobj->name, state.name.c_str());
		pobj->ship_class = ship_class;
		pobj->ai_class = sip->ai_class;
		pobj->warpin_params_index = sip->warpin_params_index;
		pobj->warpout_params_index = sip->warpout_params_index;
		pobj->ship_max_shield_strength = sip->max_shield_strength;
		pobj->ship_max_hull_strength = sip->max_hull_strength;
		pobj->max_shield_recharge = sip->max_shield_recharge;
		pobj->replacement_textures = sip->replacement_textures;
		pobj->score = sip->score;

		pobj->pos = state.pos;
		pobj->orient = state.orient;

		int team = lookup_team(state.team);
		pobj->team = (team >= 0) ? team : 0;

		pobj->arrival_location = The_mission.support_ships.arrival_location;
		pobj->arrival_anchor = The_mission.support_ships.arrival_anchor;
		pobj->departure_location = The_mission.support_ships.departure_location;
		pobj->departure_anchor = The_mission.support_ships.departure_anchor;
		pobj->arrival_delay = 0;
		pobj->arrival_cue = Locked_sexp_true;
		pobj->departure_cue = Locked_sexp_false;
		pobj->initial_velocity = 100;
		pobj->net_signature = multi_assign_network_signature(MULTI_SIG_SHIP);

		if (Player_obj != nullptr && Player_obj->flags[Object::Object_Flags::No_shields]) {
			pobj->flags.set(Mission::Parse_Object_Flags::OF_No_shields);
		}

		{
			ship_registry_entry entry(pobj->name);
			entry.status = ShipStatus::NOT_YET_PRESENT;
			entry.pobj_num = -1; // not in Parse_objects, which is the whole point

			Ship_registry.push_back(entry);
			Ship_registry_map[pobj->name] = static_cast<int>(Ship_registry.size() - 1);
		}

		int objnum = parse_create_object(pobj);
		if (objnum < 0) {
			mprintf(("CHECKPOINT => Could not recreate support ship '%s'.\n", state.name.c_str()));
			Arriving_support_ship = nullptr;
			continue;
		}

		// The engine's own tidy-up: it sets Warped_support and clears the arriving-support
		// globals.  The repair goals it would normally issue here are left to the AI restore,
		// which has the real ones, hence the empty target list above.
		mission_parse_support_arrived(objnum);

		created++;
	}

	if (created > 0) {
		mprintf(("CHECKPOINT => Recreated %d support ship(s).\n", created));
	}
}

// ------------------------------------------------------------------
// Weapons in flight
// ------------------------------------------------------------------

struct weapon_in_flight_flag_entry {
	Weapon::Weapon_Flags flag;
	const char* name;
};

// Only the flags that say something durable about a weapon already in the air.  The render
// overrides are here because scripts set them per weapon instance and nothing else would put
// them back; the multiplayer bookkeeping flags are deliberately absent, since a restored
// mission is single player and those describe packets that were sent in another run.
const weapon_in_flight_flag_entry Weapon_in_flight_flag_table[] = {
	{Weapon::Weapon_Flags::Lock_warning_played,       "lock_warning_played"},
	{Weapon::Weapon_Flags::Played_flyby_sound,        "played_flyby_sound"},
	{Weapon::Weapon_Flags::Consider_for_flyby_sound,  "consider_for_flyby_sound"},
	{Weapon::Weapon_Flags::Dead_in_water,             "dead_in_water"},
	{Weapon::Weapon_Flags::Locked_when_fired,         "locked_when_fired"},
	{Weapon::Weapon_Flags::Spawned,                   "spawned"},
	{Weapon::Weapon_Flags::No_homing,                 "no_homing"},
	{Weapon::Weapon_Flags::Overridden_homing,         "overridden_homing"},
	{Weapon::Weapon_Flags::Begun_detonation,          "begun_detonation"},
	{Weapon::Weapon_Flags::No_thruster,               "no_thruster"},
	{Weapon::Weapon_Flags::Glowmaps_disabled,         "glowmaps_disabled"},
	{Weapon::Weapon_Flags::Draw_as_wireframe,         "draw_as_wireframe"},
	{Weapon::Weapon_Flags::Render_full_detail,        "render_full_detail"},
	{Weapon::Weapon_Flags::Render_without_light,      "render_without_light"},
	{Weapon::Weapon_Flags::Render_without_diffuse,    "render_without_diffuse"},
	{Weapon::Weapon_Flags::Render_without_glowmap,    "render_without_glowmap"},
	{Weapon::Weapon_Flags::Render_without_normalmap,  "render_without_normalmap"},
	{Weapon::Weapon_Flags::Render_without_heightmap,  "render_without_heightmap"},
	{Weapon::Weapon_Flags::Render_without_ambientmap, "render_without_ambientmap"},
	{Weapon::Weapon_Flags::Render_without_specmap,    "render_without_specmap"},
	{Weapon::Weapon_Flags::Render_without_reflectmap, "render_without_reflectmap"},
};

struct weapon_state_entry {
	WeaponState state;
	const char* name;
};

const weapon_state_entry Weapon_state_table[] = {
	{WeaponState::INVALID,        "invalid"},
	{WeaponState::NORMAL,         "normal"},
	{WeaponState::FREEFLIGHT,     "freeflight"},
	{WeaponState::IGNITION,       "ignition"},
	{WeaponState::HOMED_FLIGHT,   "homed_flight"},
	{WeaponState::UNHOMED_FLIGHT, "unhomed_flight"},
	{WeaponState::WARMUP,         "warmup"},
	{WeaponState::FIRING,         "firing"},
	{WeaponState::PAUSED,         "paused"},
	{WeaponState::WARMDOWN,       "warmdown"},
};

SCP_string weapon_state_name(WeaponState state)
{
	for (const auto& entry : Weapon_state_table) {
		if (entry.state == state) {
			return SCP_string(entry.name);
		}
	}
	return SCP_string("invalid");
}

WeaponState lookup_weapon_state(const SCP_string& name)
{
	for (const auto& entry : Weapon_state_table) {
		if (name == entry.name) {
			return entry.state;
		}
	}
	return WeaponState::INVALID;
}

// A subsystem pointer turned into the name+ordinal key everything else in here uses.  Empty when
// the subsystem does not belong to that ship, which is how a stale pointer shows up.
SCP_string subsys_key_for(const ship* shipp, const ship_subsys* target)
{
	if (shipp == nullptr || target == nullptr) {
		return SCP_string();
	}

	SCP_map<SCP_string, int> ordinals;
	for (auto subsys = GET_FIRST(&shipp->subsys_list); subsys != END_OF_LIST(&shipp->subsys_list);
	     subsys = GET_NEXT(subsys)) {
		SCP_string name = subsys_key(subsys);
		int ordinal = ordinals[name]++;
		if (subsys == target) {
			return subsys_lookup_key(name, ordinal);
		}
	}

	return SCP_string();
}

// The same key resolved against whatever ship now carries that name.
ship_subsys* find_subsys_by_key(const SCP_string& ship_name, const SCP_string& key)
{
	if (ship_name.empty() || key.empty()) {
		return nullptr;
	}

	auto entry = ship_registry_get(ship_name);
	if (entry == nullptr || !entry->has_shipp()) {
		return nullptr;
	}

	auto live = index_subsystems(entry->shipp());
	auto it = live.find(key);
	return (it == live.end()) ? nullptr : it->second;
}

// Every weapon currently in the air.  Beams are objects of their own type and are not here.
void store_projectiles(checkpoint_data& data)
{
	for (int i = 0; i < MAX_WEAPONS; i++) {
		const weapon* wp = &Weapons[i];
		if (wp->weapon_info_index < 0 || wp->objnum < 0 || wp->objnum >= MAX_OBJECTS) {
			continue;
		}

		const object* objp = &Objects[wp->objnum];
		if (objp->type != OBJ_WEAPON || objp->flags[Object::Object_Flags::Should_be_dead]) {
			continue;
		}

		projectile_state state;
		state.weapon_class = weapon_class_name(wp->weapon_info_index);
		if (state.weapon_class.empty()) {
			continue;
		}

		state.pos = objp->pos;
		state.orient = objp->orient;
		state.velocity = objp->phys_info.vel;
		state.desired_velocity = objp->phys_info.desired_vel;
		state.start_pos = wp->start_pos;
		state.hull = objp->hull_strength;

		state.lifeleft = wp->lifeleft;
		state.creation_time = wp->creation_time;
		state.group_id = wp->group_id;

		state.team = team_name(wp->team);
		if (wp->species >= 0 && wp->species < static_cast<int>(Species_info.size())) {
			state.species = Species_info[wp->species].species_name;
		}
		collect_flags(wp->weapon_flags, Weapon_in_flight_flag_table, state.flags);
		state.weapon_state = weapon_state_name(wp->weapon_state);

		state.parent_ship = ship_name_for_objnum(objp->parent);
		if (wp->turret_subsys != nullptr && !state.parent_ship.empty()) {
			auto parent_entry = ship_registry_get(state.parent_ship);
			if (parent_entry != nullptr && parent_entry->has_shipp()) {
				state.parent_turret = subsys_key_for(parent_entry->shipp(), wp->turret_subsys);
			}
		}

		// homing_object is set to &obj_used_list -- the list head, not a real object -- to mean
		// "not homing", so it has to be range-checked rather than null-checked.
		if (wp->homing_object != nullptr && wp->homing_object != &obj_used_list) {
			int homing_objnum = OBJ_INDEX(wp->homing_object);
			state.homing_ship = ship_name_for_objnum(homing_objnum);
			if (!state.homing_ship.empty() && wp->homing_subsys != nullptr) {
				state.homing_subsys = subsys_key_for(&Ships[wp->homing_object->instance], wp->homing_subsys);
			}
		}
		if (!IS_VEC_NULL(&wp->homing_pos)) {
			state.homing_pos = wp->homing_pos;
			state.has_homing_pos = true;
		}

		state.det_range = wp->det_range;
		state.weapon_max_vel = wp->weapon_max_vel;
		state.launch_speed = wp->launch_speed;
		state.alpha_current = wp->alpha_current;
		state.alpha_backward = (wp->alpha_backward != 0);

		state.lssm_stage = wp->lssm_stage;
		state.lssm_warpout_time = wp->lssm_warpout_time;
		state.lssm_warpin_time = wp->lssm_warpin_time;
		state.lssm_target_pos = wp->lssm_target_pos;

		state.cmeasure_timer = wp->cmeasure_timer;

		data.projectiles.push_back(std::move(state));
	}
}

// ------------------------------------------------------------------
// Beams
// ------------------------------------------------------------------

struct beam_flag_entry {
	int flag;
	const char* name;
};

// beam::flags is a plain int of BF_* bits rather than a flagset, so it needs its own table.
// BF_SAFETY is per-frame and is recomputed before it is next read, so it is not here.
const beam_flag_entry Beam_flag_table[] = {
	{BF_SHRINK,           "shrink"},
	{BF_FORCE_FIRING,     "force_firing"},
	{BF_IS_FIGHTER_BEAM,  "fighter_beam"},
	{BF_TARGETING_COORDS, "targeting_coords"},
	{BF_FLOATING_BEAM,    "floating_beam"},
	{BF_GROW,             "grow"},
	{BF_FINISHED_GROWING, "finished_growing"},
};

void collect_beam_flags(int flags, SCP_vector<SCP_string>& out)
{
	out.clear();
	for (const auto& entry : Beam_flag_table) {
		if (flags & entry.flag) {
			out.emplace_back(entry.name);
		}
	}
}

int lookup_beam_flags(const SCP_vector<SCP_string>& names)
{
	int flags = 0;
	for (const auto& name : names) {
		for (const auto& entry : Beam_flag_table) {
			if (name == entry.name) {
				flags |= entry.flag;
				break;
			}
		}
	}
	return flags;
}

bool has_flag_name(const SCP_vector<SCP_string>& names, const char* name)
{
	return std::find(names.begin(), names.end(), name) != names.end();
}

void store_beams(checkpoint_data& data)
{
	// Walked through the object list rather than the beam free/used lists, which beam.cpp keeps
	// to itself.
	for (auto objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp)) {
		if (objp->type != OBJ_BEAM || objp->instance < 0 || objp->instance >= MAX_BEAMS) {
			continue;
		}
		if (objp->flags[Object::Object_Flags::Should_be_dead]) {
			continue;
		}

		const beam* b = &Beams[objp->instance];
		if (b->weapon_info_index < 0 || b->objnum < 0) {
			continue;
		}

		beam_shot_state state;
		state.weapon_class = weapon_class_name(b->weapon_info_index);
		if (state.weapon_class.empty()) {
			continue;
		}

		if (b->objp != nullptr) {
			state.shooter_ship = ship_name_for_objnum(OBJ_INDEX(b->objp));
			if (b->subsys != nullptr && !state.shooter_ship.empty()) {
				state.turret = subsys_key_for(&Ships[b->objp->instance], b->subsys);
			}
		}

		if (b->target != nullptr) {
			state.target_ship = ship_name_for_objnum(OBJ_INDEX(b->target));
			if (!state.target_ship.empty() && b->target_subsys != nullptr) {
				state.target_subsys = subsys_key_for(&Ships[b->target->instance], b->target_subsys);
			}
		}

		state.team = team_name(b->team);
		state.weapon_state = weapon_state_name(b->weapon_state);
		collect_beam_flags(b->flags, state.flags);

		state.target_pos1 = b->target_pos1;
		state.target_pos2 = b->target_pos2;
		state.last_start = b->last_start;
		state.last_shot = b->last_shot;
		state.local_fire_position = b->local_fire_postion;

		state.life_left = b->life_left;
		state.current_width_factor = b->current_width_factor;
		state.u_offset_local = b->u_offset_local;
		state.beam_glow_frame = b->beam_glow_frame;
		state.framecount = b->framecount;
		state.shot_index = b->shot_index;
		state.bank = b->bank;
		state.firingpoint = b->firingpoint;
		state.warmup_stamp = b->warmup_stamp;
		state.warmdown_stamp = b->warmdown_stamp;

		state.dir_a = b->binfo.dir_a;
		state.dir_b = b->binfo.dir_b;
		state.rot_axis = b->binfo.rot_axis;
		state.shot_count = b->binfo.shot_count;
		for (int i = 0; i < b->binfo.shot_count && i < MAX_BEAM_SHOTS; i++) {
			state.shot_aim.push_back(b->binfo.shot_aim[i]);
		}

		data.beams.push_back(std::move(state));
	}
}

void store_asteroids(checkpoint_data& data)
{
	for (const auto& ast : Asteroids) {
		if (ast.objnum < 0 || ast.objnum >= MAX_OBJECTS) {
			continue;
		}

		const object* objp = &Objects[ast.objnum];
		if (objp->type != OBJ_ASTEROID) {
			continue;
		}

		asteroid_state state;
		state.type_name = asteroid_type_name(ast.asteroid_type);
		state.subtype = ast.asteroid_subtype;
		state.pos = objp->pos;
		state.orient = objp->orient;
		state.vel = objp->phys_info.vel;
		state.rotvel = objp->phys_info.rotvel;
		state.hull = objp->hull_strength;
		state.flags = ast.flags;
		state.target_ship = ship_name_for_objnum(ast.target_objnum);
		state.check_for_wrap = ast.check_for_wrap.value();
		state.check_for_collide = ast.check_for_collide.value();
		state.final_death_time = ast.final_death_time.value();

		data.asteroids.push_back(std::move(state));
	}
}

void apply_asteroids(const checkpoint_data& data)
{
	// Asteroids_enabled itself is restored with the rest of the world; see apply_environment().
	//
	// Nothing was captured, which means either the mission has no field or the checkpoint
	// predates this being saved.  Either way, leave the freshly created field alone rather than
	// wiping it.
	if (data.asteroids.empty()) {
		return;
	}

	// Clear what the mission load made, then rebuild exactly what was saved.  Rebuilding rather
	// than matching up is far simpler here than it would be for ships: asteroids have no names
	// and nothing in the mission refers to an individual one.
	for (const auto& ast : Asteroids) {
		if (ast.objnum >= 0 && ast.objnum < MAX_OBJECTS && Objects[ast.objnum].type == OBJ_ASTEROID) {
			Objects[ast.objnum].flags.set(Object::Object_Flags::Should_be_dead);
		}
	}
	obj_delete_all_that_should_be_dead();

	for (const auto& state : data.asteroids) {
		int asteroid_type = lookup_asteroid_type(state.type_name);
		if (asteroid_type < 0) {
			continue;
		}

		object* objp = asteroid_create(&Asteroid_field, asteroid_type, state.subtype);
		if (objp == nullptr) {
			continue;
		}

		objp->pos = state.pos;
		objp->last_pos = state.pos;
		objp->orient = state.orient;
		objp->phys_info.vel = state.vel;
		objp->phys_info.desired_vel = state.vel;
		objp->phys_info.rotvel = state.rotvel;
		objp->hull_strength = state.hull;

		if (objp->instance >= 0 && objp->instance < MAX_ASTEROIDS) {
			asteroid* ast = &Asteroids[objp->instance];
			ast->flags = state.flags;
			ast->target_objnum = objnum_for_ship_name(state.target_ship);
			ast->collide_objnum = -1;
			ast->collide_objsig = -1;
			ast->check_for_wrap = TIMESTAMP(translate_stamp(state.check_for_wrap));
			ast->check_for_collide = TIMESTAMP(translate_stamp(state.check_for_collide));
			ast->final_death_time = TIMESTAMP(translate_stamp(state.final_death_time));
		}
	}
}

// Model animations are runtime state that lives nowhere in the mission file: a Scripted
// animation set going by a SEXP, a fighter bay left open, a dock arm part-way through its
// travel.  Without this a restored mission snaps every one of them back to its rest pose,
// which for a bay door or a dock arm is not merely cosmetic -- the ship is left in a pose its
// own logic thinks it has already moved out of.
//
// The engine already knows how to put an animation at an arbitrary point in its timeline: that
// is what multiplayer does to sync them, via ModelAnimation::start()'s time override.  This
// reuses that.
void store_animations(const object* objp, SCP_vector<animation_state>& out)
{
	out.clear();

	int model_instance_num = object_get_model_instance_num(objp);
	if (model_instance_num < 0) {
		return;
	}

	polymodel_instance* pmi = model_get_instance(model_instance_num);
	if (pmi == nullptr) {
		return;
	}

	for (const auto& entry : animation::ModelAnimationSet::getAnimationStates(pmi->id)) {
		animation_state state;
		state.id = entry.first;
		state.state = static_cast<int>(entry.second.state);
		state.direction = static_cast<int>(entry.second.canonicalDirection);
		state.time = entry.second.time;
		state.duration = entry.second.duration;
		state.speed = entry.second.speed;
		state.instance_flags = entry.second.instance_flags.to_u64();

		out.push_back(std::move(state));
	}
}

void restore_animations(const object* objp, const SCP_vector<animation_state>& in)
{
	if (in.empty()) {
		return;
	}

	int model_instance_num = object_get_model_instance_num(objp);
	if (model_instance_num < 0) {
		return;
	}

	polymodel_instance* pmi = model_get_instance(model_instance_num);
	if (pmi == nullptr) {
		return;
	}

	for (const auto& state : in) {
		animation::ModelAnimation::instance_data data;
		data.state = static_cast<animation::ModelAnimationState>(state.state);
		data.canonicalDirection = static_cast<animation::ModelAnimationDirection>(state.direction);
		data.time = state.time;
		data.duration = state.duration;
		data.speed = state.speed;
		data.instance_flags.from_u64(state.instance_flags);

		// A miss means the animation was renamed or removed from the table since the checkpoint
		// was written, which is a mod change rather than an error.
		if (!animation::ModelAnimationSet::applyAnimationState(pmi, state.id, data)) {
			mprintf(("CHECKPOINT => Animation %u is no longer present; leaving it at rest.\n", state.id));
		}
	}
}

// A dock point index resolved against the model it belongs to.  Same reasoning as everywhere
// else: an index is only meaningful for the exact model that produced it.
SCP_string dock_point_name(const ship* shipp, int dockpoint)
{
	if (shipp == nullptr || dockpoint < 0) {
		return SCP_string();
	}

	int modelnum = Ship_info[shipp->ship_info_index].model_num;
	if (modelnum < 0) {
		return SCP_string();
	}

	const char* name = model_get_dock_name(modelnum, dockpoint);
	return (name != nullptr) ? SCP_string(name) : SCP_string();
}

int dock_point_index(const ship* shipp, const SCP_string& name)
{
	if (shipp == nullptr || name.empty()) {
		return -1;
	}

	int modelnum = Ship_info[shipp->ship_info_index].model_num;
	if (modelnum < 0) {
		return -1;
	}

	return model_find_dock_name_index(modelnum, name.c_str());
}

// Docking established during the mission -- by a dock goal, by a support ship, by a SEXP -- is
// runtime state and lives nowhere in the mission file, so without this a restored mission has
// only whatever docking the parse data set up at mission start.
void store_docks(const ship* shipp, const object* objp, SCP_vector<dock_link_state>& out)
{
	out.clear();

	for (dock_instance* dock_ptr = objp->dock_list; dock_ptr != nullptr; dock_ptr = dock_ptr->next) {
		const object* other = dock_ptr->docked_objp;
		if (other == nullptr || other->type != OBJ_SHIP || other->instance < 0) {
			continue;
		}

		const ship* other_shipp = &Ships[other->instance];

		dock_link_state link;
		link.other_ship = other_shipp->ship_name;
		link.my_point = dock_point_name(shipp, dock_ptr->dockpoint_used);
		// dock_find_dockpoint_used_by_object() only reads, but predates const-correctness in this
		// area and takes object* -- casting here keeps store_docks() honest about not mutating.
		link.their_point = dock_point_name(other_shipp,
		                                   dock_find_dockpoint_used_by_object(const_cast<object*>(other),
		                                                                      const_cast<object*>(objp)));

		out.push_back(std::move(link));
	}
}

// Run once every ship exists.  Both ends of a link are stored, so the pair is checked first --
// docking an already-docked pair a second time would corrupt the dock list.
void restore_docks(ship* shipp, object* objp, const SCP_vector<dock_link_state>& in)
{
	for (const auto& link : in) {
		auto entry = ship_registry_get(link.other_ship);
		if (entry == nullptr || !entry->has_objp() || !entry->has_shipp()) {
			mprintf(("CHECKPOINT => '%s' was docked to '%s', which is not here; leaving it undocked.\n",
			         shipp->ship_name,
			         link.other_ship.c_str()));
			continue;
		}

		object* other = &Objects[entry->objnum];
		if (dock_check_find_direct_docked_object(objp, other)) {
			// The other end of this link already did it.
			continue;
		}

		int my_point = dock_point_index(shipp, link.my_point);
		int their_point = dock_point_index(&Ships[entry->shipnum], link.their_point);
		if (my_point < 0 || their_point < 0) {
			mprintf(("CHECKPOINT => Dock point '%s'/'%s' no longer exists on '%s'/'%s'; leaving them undocked.\n",
			         link.my_point.c_str(),
			         link.their_point.c_str(),
			         shipp->ship_name,
			         link.other_ship.c_str()));
			continue;
		}

		// The ai_ version rather than dock_dock_objects() directly, because it also sets the
		// docked-with bookkeeping the AI reads.
		ai_do_objects_docked_stuff(objp, my_point, other, their_point, false);
	}
}

void store_ai(const ship* shipp, ai_state& out)
{
	out = ai_state();

	if (shipp->ai_index < 0 || shipp->ai_index >= MAX_AI_INFO) {
		return;
	}

	const ai_info* aip = &Ai_info[shipp->ai_index];
	out.present = true;

	collect_flags(aip->ai_flags, Ai_flag_table, out.flags);
	collect_flags(aip->ai_override_flags, Ai_override_flag_table, out.override_flags);
	store_ai_scalars(*aip, out);

	out.target_ship = ship_name_for_objnum(aip->target_objnum);
	out.goal_ship = ship_name_for_objnum(aip->goal_objnum);
	out.guard_ship = ship_name_for_objnum(aip->guard_objnum);
	out.guard_wing = wing_name_for_wingnum(aip->guard_wingnum);
	out.support_ship = ship_name_for_objnum(aip->support_ship_objnum);
	out.hitter_ship = ship_name_for_objnum(aip->hitter_objnum);
	out.artillery_ship = ship_name_for_objnum(aip->artillery_objnum);
	out.waypoint_list = waypoint_list_name(aip->wp_list_index);

	// ignore_objnum doubles as a wing reference: -(wingnum + 1) means "ignore this whole wing".
	if (aip->ignore_objnum < -1) {
		out.ignore_wing = wing_name_for_wingnum(-(aip->ignore_objnum + 1));
	} else {
		out.ignore_ship = ship_name_for_objnum(aip->ignore_objnum);
	}

	// The targeted subsystem is a pointer into the target's subsystem list, so it is stored the
	// same way subsystems are stored everywhere else: by name plus ordinal within that name.
	if (aip->targeted_subsys != nullptr && aip->target_objnum >= 0 && aip->target_objnum < MAX_OBJECTS) {
		const object* target = &Objects[aip->target_objnum];
		if (target->type == OBJ_SHIP && target->instance >= 0) {
			const ship* target_shipp = &Ships[target->instance];
			SCP_map<SCP_string, int> ordinals;
			for (auto subsys = GET_FIRST(&target_shipp->subsys_list); subsys != END_OF_LIST(&target_shipp->subsys_list);
			     subsys = GET_NEXT(subsys)) {
				SCP_string name = subsys_key(subsys);
				int ordinal = ordinals[name]++;
				if (subsys == aip->targeted_subsys) {
					out.target_subsystem = subsys_lookup_key(name, ordinal);
					break;
				}
			}
		}
	}

	for (int i = 0; i < MAX_AI_GOALS; i++) {
		if (aip->goals[i].ai_mode == AI_GOAL_NONE) {
			continue;
		}

		ai_goal_state goal_state;
		store_ai_goal(shipp, aip->goals[i], goal_state);
		// The slot index matters: active_goal indexes into ai_info::goals, so a gap in the
		// middle of that array has to survive the round trip.
		out.goals.push_back(std::move(goal_state));
		out.goal_slots.push_back(i);
	}
}

void load_ai_goal(const ai_goal_state& in, ai_goal& goal)
{
	ai_goal_reset(&goal);

	ai_goal_mode mode;
	if (!ai_goal_mode_value(in.mode, mode)) {
		mprintf(("CHECKPOINT => AI goal '%s' no longer exists; dropping it.\n", in.mode.c_str()));
		return;
	}

	goal.ai_mode = mode;
	goal.type = ai_goal_type_value(in.type);
	apply_flags(in.flags, Ai_goal_flag_table, goal.flags);

	goal.signature = in.signature;
	goal.ai_submode = in.submode;
	goal.priority = in.priority;
	goal.time = in.time;
	goal.int_data = in.int_data;
	goal.float_data = in.float_data;

	// A chase-ship-class submode is a class index, so re-resolve it rather than trusting the
	// number, which a table change would have reassigned.
	if (mode == AI_GOAL_CHASE_SHIP_CLASS && !in.submode_ship_class.empty()) {
		int ship_class = lookup_ship_class(in.submode_ship_class);
		if (ship_class >= 0) {
			goal.ai_submode = ship_class;
		}
	}

	if (!in.target_name.empty()) {
		goal.target_name = ai_get_goal_target_name(in.target_name.c_str(), &goal.target_name_index);
	}

	if (!in.waypoint_list.empty()) {
		goal.wp_list_index = find_matching_waypoint_list_index(in.waypoint_list.c_str());
	}

	// Always names, never indices -- see store_ai_goal().  Clearing the two "index valid" flags
	// is what tells the AI to read them as names.
	goal.flags.remove(AI::Goal_Flags::Docker_index_valid);
	goal.flags.remove(AI::Goal_Flags::Dockee_index_valid);
	if (!in.docker_point.empty()) {
		goal.docker.name = ai_add_dock_name(in.docker_point.c_str());
	}
	if (!in.dockee_point.empty()) {
		goal.dockee.name = ai_add_dock_name(in.dockee_point.c_str());
	}
}

void load_ai(ship* shipp, const ai_state& in)
{
	if (!in.present || shipp->ai_index < 0 || shipp->ai_index >= MAX_AI_INFO) {
		return;
	}

	ai_info* aip = &Ai_info[shipp->ai_index];

	apply_flags(in.flags, Ai_flag_table, aip->ai_flags);
	apply_flags(in.override_flags, Ai_override_flag_table, aip->ai_override_flags);
	load_ai_scalars(*aip, in);

	// Object references are resolved in a second pass, once every ship exists -- see
	// resolve_ai_references().  Only the goals, which carry names rather than objnums, can be
	// rebuilt here.
	for (int i = 0; i < MAX_AI_GOALS; i++) {
		ai_goal_reset(&aip->goals[i]);
	}

	for (size_t i = 0; i < in.goals.size() && i < in.goal_slots.size(); i++) {
		int slot = in.goal_slots[i];
		if (slot < 0 || slot >= MAX_AI_GOALS) {
			continue;
		}
		load_ai_goal(in.goals[i], aip->goals[slot]);
	}

	if (!in.waypoint_list.empty()) {
		aip->wp_list_index = find_matching_waypoint_list_index(in.waypoint_list.c_str());
	}
}

// Everything the AI points at, resolved once every ship in the mission exists.  Split out from
// load_ai() because a ship's target is very often a ship that has not been restored yet.
void resolve_ai_references(ship* shipp, const ai_state& in)
{
	if (!in.present || shipp->ai_index < 0 || shipp->ai_index >= MAX_AI_INFO) {
		return;
	}

	ai_info* aip = &Ai_info[shipp->ai_index];

	aip->target_objnum = objnum_for_ship_name(in.target_ship);
	aip->goal_objnum = objnum_for_ship_name(in.goal_ship);
	aip->guard_objnum = objnum_for_ship_name(in.guard_ship);
	aip->support_ship_objnum = objnum_for_ship_name(in.support_ship);
	aip->hitter_objnum = objnum_for_ship_name(in.hitter_ship);
	aip->artillery_objnum = objnum_for_ship_name(in.artillery_ship);

	// Signatures are paired with the objnums so the AI can tell that the thing it was chasing
	// has been replaced by something else reusing the slot; they have to be re-derived from the
	// objects we just resolved rather than restored, since signatures are handed out afresh.
	aip->target_signature = (aip->target_objnum >= 0) ? Objects[aip->target_objnum].signature : -1;
	aip->goal_signature = (aip->goal_objnum >= 0) ? Objects[aip->goal_objnum].signature : -1;
	aip->guard_signature = (aip->guard_objnum >= 0) ? Objects[aip->guard_objnum].signature : -1;
	aip->support_ship_signature = (aip->support_ship_objnum >= 0) ? Objects[aip->support_ship_objnum].signature : -1;
	aip->hitter_signature = (aip->hitter_objnum >= 0) ? Objects[aip->hitter_objnum].signature : -1;
	aip->artillery_sig = (aip->artillery_objnum >= 0) ? Objects[aip->artillery_objnum].signature : -1;

	aip->guard_wingnum = in.guard_wing.empty() ? -1 : wing_lookup(in.guard_wing.c_str());

	if (!in.ignore_wing.empty()) {
		int wingnum = wing_lookup(in.ignore_wing.c_str());
		aip->ignore_objnum = (wingnum >= 0) ? -(wingnum + 1) : UNUSED_OBJNUM;
		aip->ignore_signature = -1;
	} else {
		aip->ignore_objnum = objnum_for_ship_name(in.ignore_ship);
		if (aip->ignore_objnum < 0) {
			aip->ignore_objnum = UNUSED_OBJNUM;
			aip->ignore_signature = -1;
		} else {
			aip->ignore_signature = Objects[aip->ignore_objnum].signature;
		}
	}

	aip->targeted_subsys = nullptr;
	if (!in.target_subsystem.empty() && aip->target_objnum >= 0) {
		const object* target = &Objects[aip->target_objnum];
		if (target->type == OBJ_SHIP && target->instance >= 0) {
			auto live = index_subsystems(&Ships[target->instance]);
			auto it = live.find(in.target_subsystem);
			if (it != live.end()) {
				aip->targeted_subsys = it->second;
				aip->targeted_subsys_parent = aip->target_objnum;
			}
		}
	}
}

void store_subsystems(const ship* shipp, SCP_vector<subsystem_state>& out)
{
	out.clear();

	SCP_map<SCP_string, int> ordinals;

	for (auto subsys = GET_FIRST(&shipp->subsys_list); subsys != END_OF_LIST(&shipp->subsys_list);
	     subsys = GET_NEXT(subsys)) {
		subsystem_state state;

		state.name = subsys_key(subsys);
		state.ordinal = ordinals[state.name]++;
		state.sub_name = subsys->sub_name;
		state.cargo_title = subsys->subsys_cargo_title;
		state.cargo = cargo_name(subsys->subsys_cargo_name);
		state.cargo_no_deplete = (subsys->subsys_cargo_name & CARGO_NO_DEPLETE) != 0;

		collect_flags(subsys->flags, Subsys_flag_table, state.flags);
		store_subsys_scalars(*subsys, state.floats, state.ints);

		// A turret's target is stored by ship name; the objnum it holds is meaningless once
		// the mission is reloaded.
		if (subsys->turret_enemy_objnum >= 0 && subsys->turret_enemy_objnum < MAX_OBJECTS) {
			const object* target = &Objects[subsys->turret_enemy_objnum];
			if (target->type == OBJ_SHIP && target->instance >= 0) {
				state.turret_target = Ships[target->instance].ship_name;
			}
		}

		if (subsys->system_info != nullptr && subsys->system_info->type == SUBSYSTEM_TURRET) {
			state.has_weapons = true;
			store_weapons(subsys->weapons, state.weapons);
		}

		out.push_back(std::move(state));
	}
}

void load_subsystems(ship* shipp, const SCP_vector<subsystem_state>& in)
{
	auto live = index_subsystems(shipp);

	for (const auto& state : in) {
		auto it = live.find(subsys_lookup_key(state.name, state.ordinal));
		if (it == live.end()) {
			mprintf(("CHECKPOINT => Ship '%s' has no subsystem '%s' (ordinal %d) any more; skipping it.\n",
			         shipp->ship_name,
			         state.name.c_str(),
			         state.ordinal));
			continue;
		}

		ship_subsys* subsys = it->second;

		apply_flags(state.flags, Subsys_flag_table, subsys->flags);
		load_subsys_scalars(*subsys, state.floats, state.ints);

		if (!state.sub_name.empty()) {
			strcpy_s(subsys->sub_name, state.sub_name.c_str());
		}
		if (!state.cargo_title.empty()) {
			strcpy_s(subsys->subsys_cargo_title, state.cargo_title.c_str());
		}
		if (!state.cargo.empty()) {
			subsys->subsys_cargo_name = lookup_cargo(state.cargo);
			if (state.cargo_no_deplete) {
				subsys->subsys_cargo_name |= CARGO_NO_DEPLETE;
			}
		}

		// Never leave a subsystem above its (possibly changed) maximum.
		if (subsys->current_hits > subsys->max_hits) {
			subsys->current_hits = subsys->max_hits;
		}

		if (state.has_weapons) {
			load_weapons(subsys->weapons, state.weapons, true);
		}

		// Turret targets are resolved in a second pass, once every ship exists.
	}

	// The per-subsystem hits above are only half the picture.  ship_get_subsystem_strength(),
	// which is what engine speed, sensor range, weapon function and comms all actually read,
	// works off ship::subsys_info[].aggregate_current_hits -- and nothing above touches those,
	// so without this a restored ship shows its damage on the HUD while flying, targeting and
	// shooting as though it were untouched.
	ship_recalc_subsys_strength(shipp);
}

void resolve_turret_targets(ship* shipp, const SCP_vector<subsystem_state>& in)
{
	auto live = index_subsystems(shipp);

	for (const auto& state : in) {
		if (state.turret_target.empty()) {
			continue;
		}

		auto it = live.find(subsys_lookup_key(state.name, state.ordinal));
		if (it == live.end()) {
			continue;
		}

		auto entry = ship_registry_get(state.turret_target);
		if (entry == nullptr || !entry->has_objp()) {
			continue;
		}

		it->second->turret_enemy_objnum = entry->objnum;
		it->second->turret_enemy_sig = Objects[entry->objnum].signature;
	}
}

// ------------------------------------------------------------------
// Pending load state
// ------------------------------------------------------------------

struct pending_load_state {
	bool queued = false;      // a SEXP asked for a load; act on it at end of frame
	bool in_progress = false; // the mission restart has been posted; apply on the way back in
	SCP_string slot;
	LoadFlags flags = LoadFlags::None;
	checkpoint_data data;
};

pending_load_state Pending_load;

// checkpoint-exists and prompt-user-checkpoint-load are typically sat inside a `when`, so they
// get evaluated every frame until they come true.  Reading and parsing the checkpoint each
// time would mean a file read per frame, so the answer is cached.  The cache is keyed by
// mission as well as slot, and is dropped whenever we write or delete a checkpoint, which are
// the only ways the answer can change while the game is running.
struct existence_cache_entry {
	SCP_string mission;
	bool exists;
};

SCP_map<SCP_string, existence_cache_entry> Existence_cache;

// What scripts have asked to have remembered.  Lives across the reload a checkpoint load performs,
// because the store side fills it before the write and the apply side fills it from the file; only
// the level teardown empties it.
SCP_map<SCP_string, SCP_string> Script_data;

void invalidate_existence_cache(const SCP_string& slot)
{
	Existence_cache.erase(slot);
}

} // namespace

// ------------------------------------------------------------------
// Availability
// ------------------------------------------------------------------

bool mission_checkpoint_allowed()
{
	if (Game_mode & GM_MULTIPLAYER) {
		return false;
	}

	// A designer may well want checkpoints while the mission is being replayed on its own and
	// not during the campaign proper, or the other way round, so the two are separate switches.
	if (Game_mode & GM_CAMPAIGN_MODE) {
		if (The_mission.flags[Mission::Mission_Flags::No_checkpoints_in_campaign]) {
			return false;
		}
	} else if (The_mission.flags[Mission::Mission_Flags::No_checkpoints_in_simulator]) {
		return false;
	}

	return true;
}

// ------------------------------------------------------------------
// Store
// ------------------------------------------------------------------

bool mission_checkpoint_store(const SCP_string& slot)
{
	if (!(Game_mode & GM_IN_MISSION)) {
		mprintf(("CHECKPOINT => store called outside a mission; ignoring.\n"));
		return false;
	}

	if (!mission_checkpoint_allowed()) {
		mprintf(("CHECKPOINT => Checkpoints are switched off for this mission; not storing.\n"));
		return false;
	}

	// Before anything is gathered, so a script can stage whatever it wants remembered.
	if (scripting::hooks::OnCheckpointSave->isActive()) {
		scripting::hooks::OnCheckpointSave->run(
			scripting::hook_param_list(scripting::hook_param("Slot", 's', slot)));
	}

	checkpoint_data data;

	data.version = static_cast<int>(CHECKPOINT_VERSION);
	data.slot = slot;
	data.mission_filename = Game_current_mission_filename;
	data.mission_modified = The_mission.modified;
	data.mission_fingerprint = checkpoint_mission_fingerprint(SCP_string());
	data.campaign = Campaign.filename;
	data.pilot = (Player != nullptr) ? Player->callsign : "";
	data.mod_title = Mod_title;

	data.mission_time = Missiontime;
	data.mission_time_microseconds = timestamp_get_mission_time_in_microseconds();
	data.hud_timer_padding = The_mission.HUD_timer_padding;
	data.saved_timestamp_ms = timestamp();

	// --- ships ---
	// Walk the registry rather than the object list so that ships which have not arrived, and
	// ships which have already left, are captured too.
	for (const auto& entry : Ship_registry) {
		ship_state state;
		state.name = entry.name;

		switch (entry.status) {
		case ShipStatus::PRESENT:
			break;

		case ShipStatus::NOT_YET_PRESENT:
			state.disposition = ShipDisposition::NotYetHere;
			data.ships.push_back(std::move(state));
			continue;

		case ShipStatus::DEATH_ROLL:
			// A ship part-way through its death roll is going to be gone in a moment and
			// there is no way to resume a death roll on a fresh load.  Record it as already
			// destroyed; that is the state the mission is about to reach anyway.
			state.disposition = ShipDisposition::Destroyed;
			state.exit_time = Missiontime;
			data.ships.push_back(std::move(state));
			continue;

		case ShipStatus::EXITED: {
			state.disposition = ShipDisposition::Vanished;
			if (entry.exited_index >= 0 && entry.exited_index < static_cast<int>(Ships_exited.size())) {
				const auto& exited = Ships_exited[entry.exited_index];
				state.exit_time = exited.time;
				if (exited.flags[Ship::Exit_Flags::Destroyed]) {
					state.disposition = ShipDisposition::Destroyed;
				} else if (exited.flags[Ship::Exit_Flags::Departed]) {
					state.disposition = ShipDisposition::Departed;
				}
			}
			data.ships.push_back(std::move(state));
			continue;
		}

		case ShipStatus::INVALID:
		default:
			continue;
		}

		// --- from here on the ship is present and alive ---
		const ship* shipp = entry.shipp();
		const object* objp = entry.objp();
		if (shipp == nullptr || objp == nullptr) {
			continue;
		}

		state.disposition = ShipDisposition::Present;

		// Nothing in the mission file will bring this one back, so the restore has to create it
		// itself.  See restore_dynamic_ships().
		state.no_parse_object = (entry.pobj_num < 0);
		state.ship_class = ship_class_name(shipp->ship_info_index);
		state.team = team_name(shipp->team);
		state.display_name = shipp->display_name;
		state.cargo_title = shipp->cargo_title;
		state.cargo = cargo_name(shipp->cargo1);
		state.cargo_no_deplete = (shipp->cargo1 & CARGO_NO_DEPLETE) != 0;
		state.countermeasure_class = weapon_class_name(shipp->current_cmeasure);
		state.persona = persona_name(shipp->persona_index);

		if (shipp->wingnum >= 0 && shipp->wingnum < MAX_WINGS) {
			state.wing_name = Wings[shipp->wingnum].name;
		}

		state.pos = objp->pos;
		state.orient = objp->orient;

		state.hull = objp->hull_strength;
		state.max_hull = shipp->ship_max_hull_strength;
		state.shield_quadrants.assign(objp->shield_quadrant.begin(), objp->shield_quadrant.end());

		collect_flags(shipp->flags, Ship_flag_table, state.flags);
		collect_flags(objp->flags, Object_flag_table, state.object_flags);
		store_ship_scalars(*shipp, state.floats, state.ints);
		store_physics(objp->phys_info, state.physics_floats, state.physics_vecs);

		store_subsystems(shipp, state.subsystems);
		store_weapons(shipp->weapons, state.weapons);
		store_ai(shipp, state.ai);
		store_docks(shipp, objp, state.docks);
		store_animations(objp, state.animations);

		data.ships.push_back(std::move(state));
	}

	// --- wings ---
	for (int i = 0; i < Num_wings; i++) {
		const wing* wingp = &Wings[i];
		if (wingp->name[0] == '\0') {
			continue;
		}

		wing_state state;
		state.name = wingp->name;
		state.time_gone = wingp->time_gone;
		state.wave_delay_timestamp = wingp->wave_delay_timestamp.value();
		collect_flags(wingp->flags, Wing_flag_table, state.flags);
		store_wing_scalars(*wingp, state.ints);

		for (int j = 0; j < wingp->current_count && j < MAX_SHIPS_PER_WING; j++) {
			int shipnum = wingp->ship_index[j];
			if (shipnum >= 0 && shipnum < MAX_SHIPS) {
				state.ship_names.emplace_back(Ships[shipnum].ship_name);
			} else {
				state.ship_names.emplace_back();
			}
		}

		data.wings.push_back(std::move(state));
	}

	// --- SEXP variables ---
	for (int i = 0; i < MAX_SEXP_VARIABLES; i++) {
		if (!(Sexp_variables[i].type & SEXP_VARIABLE_SET)) {
			continue;
		}

		variable_state state;
		state.name = Sexp_variables[i].variable_name;
		state.is_number = (Sexp_variables[i].type & SEXP_VARIABLE_NUMBER) != 0;
		state.value = Sexp_variables[i].text;

		data.variables.push_back(std::move(state));
	}

	// --- scoring ---
	if (Player != nullptr) {
		store_scoring_scalars(Player->stats, data.scoring.ints);

		// Per-class kills go out by class name so that a table change cannot silently
		// reattribute them to a different ship.
		for (int i = 0; i < static_cast<int>(Player->stats.m_okKills.size()) && i < static_cast<int>(Ship_info.size());
		     i++) {
			if (Player->stats.m_okKills[i] != 0) {
				data.scoring.class_kills[Ship_info[i].name] = Player->stats.m_okKills[i];
			}
		}
	}

	// --- mission logic ---
	// This is what stops a restored mission replaying itself: without it every `when` whose
	// condition still holds fires again, satisfied directives re-announce, and the log restarts.
	for (const auto& event : Mission_events) {
		event_state state;

		state.name = event.name;
		state.result = event.result;
		state.previous_result = event.previous_result;
		state.repeat_count = event.repeat_count;
		state.trigger_count = event.trigger_count;
		state.count = event.count;
		state.mission_log_flags = event.mission_log_flags;
		collect_int_flags(event.flags, state.flags);

		state.timestamp = event.timestamp.value();
		state.satisfied_time = event.satisfied_time.value();
		state.born_on_date = event.born_on_date.value();

		state.log_buffer = event.event_log_buffer;
		state.log_variable_buffer = event.event_log_variable_buffer;
		state.log_container_buffer = event.event_log_container_buffer;
		state.log_argument_buffer = event.event_log_argument_buffer;
		state.backup_log_buffer = event.backup_log_buffer;

		data.events.push_back(std::move(state));
	}

	for (const auto& goal : Mission_goals) {
		goal_state state;
		state.name = goal.name;
		state.satisfied = goal.satisfied;
		data.goals.push_back(std::move(state));
	}

	for (const auto& entry : Log_entries) {
		log_entry_state state;

		state.type = static_cast<int>(entry.type);
		state.flags = entry.flags;
		state.timestamp = entry.timestamp;
		state.timer_padding = entry.timer_padding;
		state.index = entry.index;
		state.primary_team = team_name(entry.primary_team);
		state.secondary_team = team_name(entry.secondary_team);
		state.pname = entry.pname;
		state.sname = entry.sname;
		state.pname_display = entry.pname_display;
		state.sname_display = entry.sname_display;

		data.log_entries.push_back(std::move(state));
	}

	// Sticky SEXP node states.  Without these an event that has already fired can re-trigger the
	// arrival or destruction the ship restore has just accounted for.
	for (int i = 0; i < Num_sexp_nodes; i++) {
		if (Sexp_nodes[i].type == SEXP_NOT_USED) {
			continue;
		}

		bool sticky = (Sexp_nodes[i].value == SEXP_KNOWN_TRUE) || (Sexp_nodes[i].value == SEXP_KNOWN_FALSE) ||
					  (Sexp_nodes[i].value == SEXP_NAN_FOREVER) || (Sexp_nodes[i].value == SEXP_NUM_EVAL);

		if (!sticky && Sexp_nodes[i].flags == SNF_DEFAULT_VALUE) {
			continue;
		}

		sexp_node_state state;
		state.index = i;
		state.value = Sexp_nodes[i].value;
		state.flags = Sexp_nodes[i].flags;

		// For a rolled `rand` the text is the number it settled on, so it has to travel with it.
		if (Sexp_nodes[i].value == SEXP_NUM_EVAL) {
			state.text = Sexp_nodes[i].text;
		}

		data.sexp_nodes.push_back(std::move(state));
	}

	for (const auto& container : get_all_sexp_containers()) {
		container_state state;
		state.name = container.container_name;

		for (const auto& value : container.list_data) {
			state.list_data.push_back(value);
		}
		for (const auto& entry : container.map_data) {
			state.map_keys.push_back(entry.first);
			state.map_values.push_back(entry.second);
		}

		data.containers.push_back(std::move(state));
	}

	// Ships that have not arrived, but whose parse object a SEXP has already rewritten.
	for (const auto& entry : Ship_registry) {
		if (entry.status != ShipStatus::NOT_YET_PRESENT || !entry.has_p_objp()) {
			continue;
		}

		const p_object* p_objp = entry.p_objp();
		parse_object_state state;

		state.name = p_objp->name;
		state.ship_class = ship_class_name(p_objp->ship_class);
		state.team = team_name(p_objp->team);

		state.arrival_anchor = anchor_name(p_objp->arrival_anchor);
		state.departure_anchor = anchor_name(p_objp->departure_anchor);
		state.arrival_location = static_cast<int>(p_objp->arrival_location);
		state.departure_location = static_cast<int>(p_objp->departure_location);
		state.arrival_path_mask = p_objp->arrival_path_mask;
		state.departure_path_mask = p_objp->departure_path_mask;

		state.initial_hull = p_objp->initial_hull;
		state.initial_shields = p_objp->initial_shields;
		state.arrival_distance = p_objp->arrival_distance;
		state.arrival_delay = p_objp->arrival_delay;
		state.departure_delay = p_objp->departure_delay;
		state.escort_priority = p_objp->escort_priority;
		state.respawn_priority = p_objp->respawn_priority;
		state.alt_type_index = p_objp->alt_type_index;
		state.callsign_index = p_objp->callsign_index;
		state.cargo = cargo_name(p_objp->cargo1);
		state.cargo_no_deplete = (p_objp->cargo1 & CARGO_NO_DEPLETE) != 0;

		collect_def_flags(p_objp->flags, Parse_object_flags, Num_parse_object_flags, state.flags);
		store_parse_subsystems(p_objp, state.subsystems);

		data.parse_objects.push_back(std::move(state));
	}

	// Hotkey sets.  The mission-file assignments come back with ship::hotkey, but anything the
	// player bound during the mission exists only here.
	if (Player != nullptr) {
		for (int set = 0; set < MAX_KEYED_TARGETS; set++) {
			hotkey_state state;
			state.set = set;

			auto plist = &Player->keyed_targets[set];
			for (auto hitem = GET_FIRST(plist); hitem != END_OF_LIST(plist); hitem = GET_NEXT(hitem)) {
				if (hitem->objp == nullptr || hitem->objp->type != OBJ_SHIP) {
					continue;
				}

				state.ship_names.emplace_back(Ships[hitem->objp->instance].ship_name);
				state.how_added.push_back(hitem->how_added);
			}

			if (!state.ship_names.empty()) {
				data.hotkeys.push_back(std::move(state));
			}
		}
	}

	if (Player != nullptr) {
		data.current_hotkey_set = Player->current_hotkey_set;
	}

	store_asteroids(data);
	store_environment(data.environment);
	store_mission_extras(data.mission);
	store_projectiles(data);
	store_beams(data);

	// Hull debris only -- see the note on debris_state.
	for (const auto& db : Debris) {
		if (!db.flags[Debris_Flags::Used] || !db.is_hull || db.objnum < 0) {
			continue;
		}

		const object* objp = &Objects[db.objnum];
		debris_state state;

		state.ship_class = ship_class_name(db.ship_info_index);
		state.team = team_name(db.team);

		auto pm = model_get(db.model_num);
		if (pm != nullptr && db.submodel_num >= 0 && db.submodel_num < pm->n_models) {
			state.submodel = pm->submodel[db.submodel_num].name;
		}

		if (db.species >= 0 && db.species < static_cast<int>(Species_info.size())) {
			state.species = Species_info[db.species].species_name;
		}
		if (db.damage_type_idx >= 0 && db.damage_type_idx < static_cast<int>(Damage_types.size())) {
			state.damage_type = Damage_types[db.damage_type_idx].name;
		}

		state.pos = objp->pos;
		state.orient = objp->orient;
		state.velocity = objp->phys_info.vel;
		state.rotational_velocity = objp->phys_info.rotvel;

		state.hull_strength = objp->hull_strength;
		state.max_hull = db.max_hull;
		state.lifeleft = db.lifeleft;
		state.damage_mult = db.damage_mult;
		state.parent_alt_name = db.parent_alt_name;
		state.do_not_expire = db.flags[Debris_Flags::DoNotExpire];

		// A chunk with no class or submodel cannot be recreated, and hull debris always has both.
		if (!state.ship_class.empty() && !state.submodel.empty()) {
			data.debris.push_back(std::move(state));
		}
	}

	data.goal_timestamp = Mission_goal_timestamp.value();

	// Whatever the On Checkpoint Save hook staged, plus anything a script set earlier.
	data.script_data = Script_data;

	bool written = checkpoint_write(data);
	invalidate_existence_cache(slot);

	return written;
}

// ------------------------------------------------------------------
// Existence / deletion
// ------------------------------------------------------------------

bool mission_checkpoint_exists(const SCP_string& slot)
{
	// A checkpoint that cannot be loaded in this mode may as well not be there.
	if (!mission_checkpoint_allowed()) {
		return false;
	}

	auto cached = Existence_cache.find(slot);
	if (cached != Existence_cache.end() && cached->second.mission == Game_current_mission_filename) {
		return cached->second.exists;
	}

	bool exists = false;
	checkpoint_data data;

	if (checkpoint_read(slot, data)) {
		exists = checkpoint_matches_current_mission(data);
		if (!exists) {
			mprintf(("CHECKPOINT => Checkpoint '%s' does not match the current mission; treating it as absent.\n",
			         slot.c_str()));
		}
	}

	Existence_cache[slot] = {SCP_string(Game_current_mission_filename), exists};

	return exists;
}

void mission_checkpoint_delete(const SCP_string& slot)
{
	checkpoint_delete_file(slot);
	invalidate_existence_cache(slot);
}

// ------------------------------------------------------------------
// Load request handling
// ------------------------------------------------------------------

// The loadout choices a mission makes in its settings.  These apply to every load of this
// mission, however it was triggered -- a designer who ticks "keep player loadout" in FRED means
// it for load-checkpoint just as much as for the automatic entry prompt, and having it silently
// apply to only one of them is a trap.  Operator options are added on top of these, never
// subtracted, so a SEXP can ask for more than the mission settings but the mission's own choice
// always holds.
static LoadFlags mission_load_flag_defaults()
{
	LoadFlags flags = LoadFlags::None;

	if (The_mission.flags[Mission::Mission_Flags::Checkpoint_keep_player_loadout]) {
		flags |= LoadFlags::KeepPlayerLoadout;
	}
	if (The_mission.flags[Mission::Mission_Flags::Checkpoint_keep_wing_loadout]) {
		flags |= LoadFlags::KeepWingLoadout;
	}

	return flags;
}

void mission_checkpoint_request_load(const SCP_string& slot, LoadFlags flags)
{
	if (!mission_checkpoint_allowed()) {
		mprintf(("CHECKPOINT => Checkpoints are switched off for this mission; not loading.\n"));
		return;
	}

	Pending_load.queued = true;
	Pending_load.slot = slot;
	Pending_load.flags = flags | mission_load_flag_defaults();
}

bool mission_checkpoint_load_pending()
{
	return Pending_load.queued;
}

void mission_checkpoint_clear_pending()
{
	Pending_load = pending_load_state();
	Existence_cache.clear();

	// Only meaningful while a restore is being applied, and mission_checkpoint_apply() calls this
	// once it has finished.  Clearing it keeps a stale offset from reaching anything else.
	Stamp_delta = 0;
}

void mission_checkpoint_process_pending_load()
{
	if (!Pending_load.queued) {
		return;
	}

	Pending_load.queued = false;

	// Read the file now, while the old mission is still loaded, so that a missing or
	// unusable checkpoint costs nothing -- we simply carry on with the mission in progress
	// rather than restarting it and then discovering there is nothing to restore.
	if (!checkpoint_read(Pending_load.slot, Pending_load.data)) {
		mprintf(("CHECKPOINT => Cannot load '%s'; staying in the current mission.\n", Pending_load.slot.c_str()));
		mission_checkpoint_clear_pending();
		return;
	}

	if (!checkpoint_matches_current_mission(Pending_load.data) &&
	    !any(Pending_load.flags, LoadFlags::IgnoreFingerprint)) {
		mprintf(("CHECKPOINT => Checkpoint '%s' was written for a different version of this mission; "
		         "staying in the current mission.\n",
		         Pending_load.slot.c_str()));
		mission_checkpoint_clear_pending();
		return;
	}

	Pending_load.in_progress = true;

	// Restarting the mission is what actually performs the load: the level is torn down and
	// rebuilt from the mission file, and mission_checkpoint_apply() then bashes the saved
	// state on top before the first frame runs.
	if (any(Pending_load.flags, LoadFlags::ReopenLoadout)) {
		gameseq_post_event(GS_EVENT_START_GAME);
	} else {
		gameseq_post_event(GS_EVENT_START_GAME_QUICK);
	}
}

// ------------------------------------------------------------------
// Apply
// ------------------------------------------------------------------

namespace {

// Take a ship out of the mission because the checkpoint says it was already gone.
//
// This goes through ship_cleanup() rather than deleting the object directly, because that is what
// keeps the ship registry, the exited-ship list, the wing bookkeeping, the docking lists and the
// AI consistent.  The REDALERT cleanup modes are the quiet ones -- no mission log entry, no kill
// counted, no arrival/destruction music -- which is what we want, since the log and the score are
// restored from the checkpoint in their own right.  The exited-ship entry it leaves behind says
// "player deleted", so we correct that afterwards to what actually happened.
//
// Game_restoring is raised for the whole of the apply, which is what stops ship_cleanup() writing a
// departure to the mission log or re-running the On Ship Depart hook for a departure the script
// already saw in the run that was saved.  Both guards live at the call sites in ship.cpp.
void remove_ship_for_restore(const ship_registry_entry* entry, const ship_state& state)
{
	int shipnum = entry->shipnum;
	object* objp = &Objects[entry->objnum];

	int cleanup_mode = SHIP_VANISHED;
	int registry_mode = SHIP_VANISHED;
	auto exit_reason = Ship::Exit_Flags::Destroyed;
	bool leaves_record = false;

	switch (state.disposition) {
	case ShipDisposition::Destroyed:
		cleanup_mode = SHIP_DESTROYED_REDALERT;
		registry_mode = SHIP_DESTROYED;
		exit_reason = Ship::Exit_Flags::Destroyed;
		leaves_record = true;
		break;

	case ShipDisposition::Departed:
		cleanup_mode = SHIP_DEPARTED_REDALERT;
		registry_mode = SHIP_DEPARTED;
		exit_reason = Ship::Exit_Flags::Departed;
		leaves_record = true;
		break;

	default:
		// Vanished, or a ship the checkpoint says had not arrived yet but which this run created
		// anyway.  Either way it leaves no trace, which is exactly what SHIP_VANISHED does.
		break;
	}

	objp->flags.set(Object::Object_Flags::Should_be_dead);
	dock_undock_all(objp);
	ship_cleanup(shipnum, cleanup_mode);

	if (!leaves_record) {
		return;
	}

	// Re-read the entry: ship_cleanup() has just rewritten it.
	auto updated = ship_registry_get(state.name);
	if (updated == nullptr) {
		return;
	}

	// So that anything reading the registry sees how the ship really left, not the red-alert
	// stand-in we borrowed to get the quiet cleanup.
	const_cast<ship_registry_entry*>(updated)->cleanup_mode = registry_mode;

	if (updated->exited_index < 0 || updated->exited_index >= static_cast<int>(Ships_exited.size())) {
		return;
	}

	auto& exited = Ships_exited[updated->exited_index];
	exited.flags.remove(Ship::Exit_Flags::Player_deleted);
	exited.flags.set(exit_reason);
	exited.time = state.exit_time;
}

// Take out every ship the checkpoint says had already gone but which is standing here alive.
// Safe to call more than once: a ship that has already been removed is EXITED and is skipped.
void remove_gone_ships(const checkpoint_data& data)
{
	for (const auto& state : data.ships) {
		if (state.disposition == ShipDisposition::Present) {
			continue;
		}

		auto entry = ship_registry_get(state.name);
		if (entry == nullptr) {
			// The mission no longer has this ship at all; nothing to reconcile.
			continue;
		}

		if (entry->status != ShipStatus::PRESENT && entry->status != ShipStatus::DEATH_ROLL) {
			continue;
		}

		if (entry->has_shipp() && entry->has_objp()) {
			remove_ship_for_restore(entry, state);
		}
	}
}

// Stop anything the checkpoint says had already gone from arriving later.  The clock has just
// been wound forward, so a ship still on the arrival list would otherwise find its cue true
// within a frame or two and walk into a mission it had already left.
void block_gone_arrivals(const checkpoint_data& data)
{
	for (const auto& state : data.ships) {
		// A ship that had not arrived yet when the checkpoint was taken is still due to arrive;
		// only ships that had already been and gone are blocked.
		if (state.disposition == ShipDisposition::Present || state.disposition == ShipDisposition::NotYetHere) {
			continue;
		}

		auto entry = ship_registry_get(state.name);
		if (entry == nullptr || entry->status != ShipStatus::NOT_YET_PRESENT || !entry->has_p_objp()) {
			continue;
		}

		// This is all mission_parse_mark_non_arrival() does; the flag is set directly because
		// that function has no header declaration.
		entry->p_objp()->flags.set(Mission::Parse_Object_Flags::SF_Cannot_arrive);
	}
}

// Replay the waves each wing had been through, so its ships exist to be restored onto.
//
// Waves have to be created one at a time, with the ships that had already gone taken out in
// between: a wing will not produce its next wave while the previous one is still above its
// threshold, and MAX_SHIPS_PER_WING is an assert, not a soft limit.  Creating and then removing a
// wave the player had already wiped out is not wasted work -- it is what gives the later waves
// their correct names, which is how the saved state finds them again.
//
// parse_wing_create_ships() is called with force_create so that the arrival cue, the arrival
// delay and the docking-bay anchor check are all skipped.  We already know the wing arrived,
// because the checkpoint watched it happen, and the anchor it launched from may well not have
// been created yet at this point in the restore.
void restore_wing_arrivals(const checkpoint_data& data)
{
	for (const auto& state : data.wings) {
		int wingnum = wing_lookup(state.name.c_str());
		if (wingnum < 0) {
			mprintf(("CHECKPOINT => Wing '%s' is no longer in this mission.\n", state.name.c_str()));
			continue;
		}

		auto wave = state.ints.find("current_wave");
		if (wave == state.ints.end() || wave->second <= 0) {
			continue;
		}

		auto wingp = &Wings[wingnum];
		int target_wave = wave->second;

		// The mission having fewer waves than the checkpoint recorded should be impossible given
		// the fingerprint check, but red alert handles the same case rather than asserting.
		if (wingp->num_waves < target_wave) {
			mprintf(("CHECKPOINT => Wing '%s' now has %d waves but the checkpoint reached wave %d; "
			         "raising the wave count.\n",
			         wingp->name,
			         wingp->num_waves,
			         target_wave));
			wingp->num_waves = target_wave;
		}

		while (wingp->current_wave < target_wave) {
			int before = wingp->current_wave;
			parse_wing_create_ships(wingp, wingp->wave_count, true, true);

			if (wingp->current_wave == before) {
				mprintf(("CHECKPOINT => Wing '%s' stopped arriving at wave %d of %d.\n",
				         wingp->name,
				         wingp->current_wave,
				         target_wave));
				break;
			}

			// Make room for the next wave.
			remove_gone_ships(data);
		}
	}
}

// Bring in the ships that are not in a wing and had arrived by the time the checkpoint was taken.
// These go through the engine's own arrival path with the cue forced, rather than being created
// behind its back, so the arrival list, support-ship housekeeping and docked groups are all
// handled the way they normally would be.
void restore_loose_arrivals(const checkpoint_data& data)
{
	for (const auto& state : data.ships) {
		if (state.disposition != ShipDisposition::Present) {
			continue;
		}

		auto entry = ship_registry_get(state.name);
		if (entry == nullptr || entry->status != ShipStatus::NOT_YET_PRESENT || !entry->has_p_objp()) {
			continue;
		}

		auto p_objp = entry->p_objp();

		// Wing members arrive with their wing, and a ship whose docked group has already been
		// created came in as somebody else's cargo.
		if (p_objp->wingnum >= 0 || p_objp->created_object != nullptr) {
			continue;
		}

		mission_maybe_make_ship_arrive(p_objp, true);
	}
}

// Make the set of ships that exist match the checkpoint, before any per-ship state is bashed on
// top of them.
//
// A freshly loaded mission contains the ships that are present at time zero and nothing else, so
// two things are out of place: every ship the player had already destroyed or seen depart is
// standing there alive again, and everything that arrived during the saved run is missing.
//
// Arrivals come first.  Replaying a wing's waves is what removals depend on -- there is no
// wave 3 to leave standing until waves 1 and 2 have been created and cleared away.
void reconcile_ship_existence(const checkpoint_data& data)
{
	restore_wing_arrivals(data);
	restore_loose_arrivals(data);
	restore_dynamic_ships(data);
	remove_gone_ships(data);
	block_gone_arrivals(data);
}

// Bring a ship that the checkpoint says was alive into the state it was in.
void apply_ship(const ship_state& state, bool skip_loadout)
{
	auto entry = ship_registry_get(state.name);
	if (entry == nullptr || !entry->has_shipp() || !entry->has_objp()) {
		return;
	}

	ship* shipp = &Ships[entry->shipnum];
	object* objp = &Objects[entry->objnum];

	// Class first: changing it reallocates the subsystem list and the weapon banks, so
	// everything else has to happen afterwards.
	if (!skip_loadout) {
		int ship_class = lookup_ship_class(state.ship_class);
		if (ship_class >= 0 && ship_class != shipp->ship_info_index) {
			change_ship_type(entry->shipnum, ship_class, 1);
		}
	}

	int team = lookup_team(state.team);
	if (team >= 0) {
		shipp->team = team;
	}

	if (!state.display_name.empty()) {
		shipp->display_name = state.display_name;
	}
	if (!state.cargo_title.empty()) {
		strcpy_s(shipp->cargo_title, state.cargo_title.c_str());
	}
	if (!state.cargo.empty()) {
		int cargo = lookup_cargo(state.cargo);
		if (state.cargo_no_deplete) {
			cargo |= CARGO_NO_DEPLETE;
		}
		shipp->cargo1 = static_cast<char>(cargo);
	}

	int persona = lookup_persona(state.persona);
	if (persona >= 0) {
		shipp->persona_index = persona;
	}

	apply_flags(state.flags, Ship_flag_table, shipp->flags);
	apply_flags(state.object_flags, Object_flag_table, objp->flags);
	load_ship_scalars(*shipp, state.floats, state.ints);
	load_physics(objp->phys_info, state.physics_floats, state.physics_vecs);

	objp->pos = state.pos;
	objp->orient = state.orient;

	// Clamp hull and shields to the current maxima; the ship class may grant different values
	// now than it did when the checkpoint was written.
	objp->hull_strength = MIN(state.hull, shipp->ship_max_hull_strength);

	size_t quadrants = MIN(state.shield_quadrants.size(), objp->shield_quadrant.size());
	for (size_t i = 0; i < quadrants; i++) {
		objp->shield_quadrant[i] = state.shield_quadrants[i];
	}

	int cmeasure = lookup_weapon_class(state.countermeasure_class);
	if (cmeasure >= 0) {
		shipp->current_cmeasure = cmeasure;
	}

	load_subsystems(shipp, state.subsystems);
	load_ai(shipp, state.ai);
	restore_animations(objp, state.animations);
	load_weapons(shipp->weapons, state.weapons, !skip_loadout);
}

// Was this ship part of the player's wing?  Used to honour the keep-loadout flags.
bool is_player_wing_ship(const SCP_string& name)
{
	auto entry = ship_registry_get(name);
	if (entry == nullptr || !entry->has_shipp()) {
		return false;
	}

	return Ships[entry->shipnum].flags[Ship::Ship_Flags::From_player_wing];
}

void apply_wings(const checkpoint_data& data)
{
	for (const auto& state : data.wings) {
		int wingnum = wing_lookup(state.name.c_str());
		if (wingnum < 0) {
			mprintf(("CHECKPOINT => Wing '%s' no longer exists; skipping it.\n", state.name.c_str()));
			continue;
		}

		wing* wingp = &Wings[wingnum];

		load_wing_scalars(*wingp, state.ints);
		apply_flags(state.flags, Wing_flag_table, wingp->flags);
		wingp->time_gone = state.time_gone;
		wingp->wave_delay_timestamp = TIMESTAMP(translate_stamp(state.wave_delay_timestamp));

		// Rebuild ship_index from names.  current_count is corrected to whatever we could
		// actually resolve, so a ship the mod no longer has cannot leave a dangling index.
		int count = 0;
		for (const auto& ship_name : state.ship_names) {
			if (count >= MAX_SHIPS_PER_WING) {
				break;
			}
			auto entry = ship_registry_get(ship_name);
			if (entry != nullptr && entry->has_shipp()) {
				wingp->ship_index[count++] = entry->shipnum;
			}
		}
		for (int i = count; i < MAX_SHIPS_PER_WING; i++) {
			wingp->ship_index[i] = -1;
		}
		wingp->current_count = count;
	}
}

void apply_variables(const checkpoint_data& data)
{
	for (const auto& state : data.variables) {
		int index = get_index_sexp_variable_name(state.name.c_str());
		if (index < 0) {
			mprintf(("CHECKPOINT => SEXP variable '%s' no longer exists; skipping it.\n", state.name.c_str()));
			continue;
		}

		bool is_number = (Sexp_variables[index].type & SEXP_VARIABLE_NUMBER) != 0;
		if (is_number != state.is_number) {
			mprintf(("CHECKPOINT => SEXP variable '%s' has changed type; skipping it.\n", state.name.c_str()));
			continue;
		}

		strcpy_s(Sexp_variables[index].text, state.value.c_str());
		Sexp_variables[index].type |= SEXP_VARIABLE_MODIFIED;
	}
}

void apply_scoring(const checkpoint_data& data)
{
	if (Player == nullptr) {
		return;
	}

	load_scoring_scalars(Player->stats, data.scoring.ints);

	// m_okKills is sized to ship_info_size() rather than a fixed maximum, so clear and index it
	// through its own size -- a mod with fewer classes than the one that wrote the checkpoint
	// would otherwise be written past the end.
	std::fill(Player->stats.m_okKills.begin(), Player->stats.m_okKills.end(), 0);

	for (const auto& entry : data.scoring.class_kills) {
		int ship_class = ship_info_lookup(entry.first.c_str());
		if (ship_class < 0) {
			mprintf(("CHECKPOINT => Dropping kills for retired ship class '%s'.\n", entry.first.c_str()));
			continue;
		}
		if (ship_class >= static_cast<int>(Player->stats.m_okKills.size())) {
			continue;
		}
		Player->stats.m_okKills[ship_class] = entry.second;
	}
}

// Put the mission clock back where it was when the checkpoint was taken, and work out how far
// every saved stamp has to move to follow it.
//
// The forward jump is what makes Missiontime read correctly again; it is the same mechanism the
// pre-player-entry skip in freespace.cpp uses.  It does NOT make the saved stamps correct on its
// own -- see the note above translate_stamp() for why -- so we also compute the offset between
// the checkpoint's clock and this run's, which every restored stamp is then shifted by.
// Put back the changes SEXPs had made to ships that had not arrived yet.
//
// Runs before the arrival replay, so a ship that is about to be brought in is created from the
// parse object the checkpoint recorded rather than the one the mission file describes.
void apply_parse_objects(const checkpoint_data& data)
{
	for (const auto& state : data.parse_objects) {
		auto entry = ship_registry_get(state.name);
		if (entry == nullptr || !entry->has_p_objp()) {
			continue;
		}

		// Only worth applying to something still waiting to arrive; anything already in the
		// mission is restored properly by apply_ship().
		if (entry->status != ShipStatus::NOT_YET_PRESENT) {
			continue;
		}

		p_object* p_objp = entry->p_objp();

		int ship_class = lookup_ship_class(state.ship_class);
		if (ship_class >= 0 && ship_class != p_objp->ship_class) {
			swap_parse_object(p_objp, ship_class);
		}

		int team = lookup_team(state.team);
		if (team >= 0) {
			p_objp->team = team;
		}

		// An anchor that no longer resolves is left as the mission file had it rather than being
		// blanked, which would break the arrival outright.
		auto arrival_anchor = lookup_anchor(state.arrival_anchor);
		if (arrival_anchor.isValid()) {
			p_objp->arrival_anchor = arrival_anchor;
		}
		auto departure_anchor = lookup_anchor(state.departure_anchor);
		if (departure_anchor.isValid()) {
			p_objp->departure_anchor = departure_anchor;
		}

		p_objp->arrival_location = static_cast<ArrivalLocation>(state.arrival_location);
		p_objp->departure_location = static_cast<DepartureLocation>(state.departure_location);
		p_objp->arrival_path_mask = state.arrival_path_mask;
		p_objp->departure_path_mask = state.departure_path_mask;

		p_objp->initial_hull = state.initial_hull;
		p_objp->initial_shields = state.initial_shields;
		p_objp->arrival_distance = state.arrival_distance;

		// Same dual encoding as the ship versions: once the arrival cue goes true the delay stops
		// being "so many seconds" and becomes a real timestamp (missionparse.cpp:8723), so it has
		// to be shifted into this run's clock like any other.  Miss this and a ship with a long
		// armed delay -- a capital ship due in three minutes, say -- arrives the instant the
		// mission resumes.
		p_objp->arrival_delay = translate_stamp(state.arrival_delay);
		p_objp->departure_delay = translate_stamp(state.departure_delay);
		p_objp->escort_priority = state.escort_priority;
		p_objp->respawn_priority = state.respawn_priority;
		p_objp->alt_type_index = state.alt_type_index;
		p_objp->callsign_index = state.callsign_index;
		if (!state.cargo.empty()) {
			int cargo = lookup_cargo(state.cargo);
			if (state.cargo_no_deplete) {
				cargo |= CARGO_NO_DEPLETE;
			}
			p_objp->cargo1 = static_cast<char>(cargo);
		}

		apply_def_flags(state.flags, Parse_object_flags, Num_parse_object_flags, p_objp->flags);
		load_parse_subsystems(p_objp, state.subsystems);
	}
}

// The two bits of HUD state that game_post_level_init() builds from the pristine mission, well
// before the apply point, and which therefore have to be put back by hand.
void apply_hud_state(const checkpoint_data& data)
{
	// The escort list needs nothing stored: hud_add_remove_ship_escort() keeps Ship_Flags::Escort
	// in step with the list, and that flag is restored with the rest of the ship.  So rebuild the
	// list from the flags -- clearing without clearing the flags, then re-adding each flagged
	// ship, which the toggle treats as an add because the list is empty by then.
	hud_escort_clear_all(false);

	for (const auto& state : data.ships) {
		if (state.disposition != ShipDisposition::Present) {
			continue;
		}

		auto entry = ship_registry_get(state.name);
		if (entry == nullptr || !entry->has_shipp() || !entry->has_objp()) {
			continue;
		}

		if (Ships[entry->shipnum].flags[Ship::Ship_Flags::Escort]) {
			hud_add_remove_ship_escort(entry->objnum, 1);
		}
	}

	if (Player == nullptr) {
		return;
	}

	for (int set = 0; set < MAX_KEYED_TARGETS; set++) {
		hud_target_hotkey_clear(set);
	}

	if (Player != nullptr) {
		Player->current_hotkey_set = data.current_hotkey_set;
	}

	for (const auto& state : data.hotkeys) {
		if (state.set < 0 || state.set >= MAX_KEYED_TARGETS) {
			continue;
		}

		for (size_t i = 0; i < state.ship_names.size(); i++) {
			auto entry = ship_registry_get(state.ship_names[i]);
			if (entry == nullptr || !entry->has_objp()) {
				continue;
			}

			int how_added = (i < state.how_added.size()) ? state.how_added[i] : HOTKEY_USER_ADDED;
			hud_target_hotkey_add_remove(state.set, &Objects[entry->objnum], how_added);
		}
	}
}

// Put the weapons that were in the air back in the air.
//
// weapon_create() is the only sane way in -- it does the model loading, the trail, the swarm and
// corkscrew setup, the missile list and a dozen other things -- but it is built for firing a
// weapon, not for reinstating one, so two of the things it does for a live shot have to be
// worked around.
//
// It applies the weapon's field of fire, which randomises the orientation.  Since the saved
// orientation is overwritten immediately afterwards, that costs nothing but a wasted random
// draw.
//
// It also applies substitution patterns and the failure rate, either of which can hand back a
// different weapon class or none at all.  Both only happen when the shot has a parent, so the
// weapon is created parentless and the parent is attached afterwards; the class is still
// checked, because failure_sub can fire without one.
void apply_projectiles(const checkpoint_data& data)
{
	int created = 0;

	// Saved group ids are indices handed out by weapon_create_group_id() in the run that was
	// saved, so they mean nothing here.  Weapons that shared one still have to share one,
	// though -- that is what makes a linked volley behave as a volley -- so each distinct saved
	// id is mapped to one freshly allocated id.
	SCP_map<int, int> group_ids;

	for (const auto& state : data.projectiles) {
		int weapon_class = lookup_weapon_class(state.weapon_class);
		if (weapon_class < 0) {
			mprintf(("CHECKPOINT => No weapon class '%s' any more; dropping that shot.\n",
			         state.weapon_class.c_str()));
			continue;
		}

		int group_id = -1;
		if (state.group_id >= 0) {
			auto it = group_ids.find(state.group_id);
			if (it == group_ids.end()) {
				group_id = weapon_create_group_id();
				group_ids[state.group_id] = group_id;
			} else {
				group_id = it->second;
			}
		}

		bool locked = std::find(state.flags.begin(), state.flags.end(), "locked_when_fired") != state.flags.end();
		bool spawned = std::find(state.flags.begin(), state.flags.end(), "spawned") != state.flags.end();

		int objnum = weapon_create(&state.pos, &state.orient, weapon_class, -1, group_id, locked, spawned);
		if (objnum < 0) {
			continue;
		}

		object* objp = &Objects[objnum];
		weapon* wp = &Weapons[objp->instance];

		if (wp->weapon_info_index != weapon_class) {
			// Substitution or a failure_sub gave us something else; a wrong weapon in the air is
			// worse than a missing one.
			objp->flags.set(Object::Object_Flags::Should_be_dead);
			continue;
		}

		// The parent, attached by hand because passing it to weapon_create() would have opened
		// the door to substitution.  A weapon whose parent has since been destroyed simply has
		// none, which the engine already copes with.
		int parent_objnum = objnum_for_ship_name(state.parent_ship);
		if (parent_objnum >= 0) {
			objp->parent = parent_objnum;
			objp->parent_sig = Objects[parent_objnum].signature;
			wp->turret_subsys = find_subsys_by_key(state.parent_ship, state.parent_turret);
		} else {
			objp->parent = -1;
			objp->parent_sig = -1;
			wp->turret_subsys = nullptr;
		}

		objp->orient = state.orient;
		objp->phys_info.vel = state.velocity;
		objp->phys_info.desired_vel = state.desired_velocity;
		objp->phys_info.speed = vm_vec_mag(&state.velocity);
		objp->hull_strength = state.hull;

		wp->start_pos = state.start_pos;
		wp->lifeleft = state.lifeleft;
		wp->creation_time = state.creation_time;
		wp->det_range = state.det_range;
		wp->weapon_max_vel = state.weapon_max_vel;
		wp->launch_speed = state.launch_speed;
		wp->alpha_current = state.alpha_current;
		wp->alpha_backward = state.alpha_backward ? (ubyte)1 : (ubyte)0;
		wp->weapon_state = lookup_weapon_state(state.weapon_state);

		wp->lssm_stage = state.lssm_stage;
		wp->lssm_warpout_time = translate_stamp(static_cast<int>(state.lssm_warpout_time));
		wp->lssm_warpin_time = translate_stamp(static_cast<int>(state.lssm_warpin_time));
		wp->lssm_target_pos = state.lssm_target_pos;

		wp->cmeasure_timer = translate_stamp(state.cmeasure_timer);

		// Set after weapon_create(), which resets the flagset and then sets Played_flyby_sound
		// itself for player shots.
		apply_flags(state.flags, Weapon_in_flight_flag_table, wp->weapon_flags);

		int team = lookup_team(state.team);
		if (team >= 0) {
			wp->team = team;
		}
		if (!state.species.empty()) {
			int species = species_info_lookup(state.species.c_str());
			if (species >= 0) {
				wp->species = species;
			}
		}

		// Homing.  target_num and target_sig are what the homing code checks each frame to see
		// whether its target is still the one it locked onto, so both have to agree with the
		// object we are pointing it at.
		int homing_objnum = objnum_for_ship_name(state.homing_ship);
		if (homing_objnum >= 0) {
			wp->homing_object = &Objects[homing_objnum];
			wp->target_num = homing_objnum;
			wp->target_sig = Objects[homing_objnum].signature;
			wp->homing_subsys = find_subsys_by_key(state.homing_ship, state.homing_subsys);
		} else {
			// The list head, which is how weapon_create() spells "homing on nothing".
			wp->homing_object = &obj_used_list;
			wp->homing_subsys = nullptr;
			wp->target_num = -1;
			wp->target_sig = -1;
		}
		if (state.has_homing_pos) {
			wp->homing_pos = state.homing_pos;
		}

		created++;
	}

	if (created > 0) {
		mprintf(("CHECKPOINT => Restored %d weapon(s) in flight.\n", created));
	}
}

// Put the beams that were mid-fire back.
//
// A beam is re-fired through beam_fire() and then wound forward to where it was.  beam_fire() is
// the only way in -- it allocates the beam, creates the object, derives the beam type and the
// widths from the weapon table and runs beam_aim() -- and it takes the saved aim vectors
// directly through beam_info_override, the same door multiplayer uses to make every machine see
// the same beam.
//
// beam_fire() ends in the warmup phase, so a beam that was past warmup is moved on with
// beam_start_firing() and, if it was warming down, beam_start_warmdown().  Those calls would
// ordinarily fire the beam's launch sound, the On Beam Warmup and On Beam Fired hooks, and
// deduct a round from a ballistic fighter beam; all four are suppressed while Game_restoring is
// set (beam.cpp), since they belong to the shot that was fired in the run that was saved.
void apply_beams(const checkpoint_data& data)
{
	int created = 0;

	for (const auto& state : data.beams) {
		int weapon_class = lookup_weapon_class(state.weapon_class);
		if (weapon_class < 0) {
			mprintf(("CHECKPOINT => No weapon class '%s' any more; dropping that beam.\n",
			         state.weapon_class.c_str()));
			continue;
		}

		bool floating = has_flag_name(state.flags, "floating_beam");
		bool targeting_coords = has_flag_name(state.flags, "targeting_coords");

		int shooter_objnum = objnum_for_ship_name(state.shooter_ship);
		ship_subsys* turret = find_subsys_by_key(state.shooter_ship, state.turret);

		// A beam is anchored to the turret that is firing it, so if either the shooter or its
		// turret is gone there is nothing to re-fire.  The turret will simply acquire and fire
		// again on its own within a second or two, which is what happens anyway.
		if (!floating && (shooter_objnum < 0 || turret == nullptr)) {
			continue;
		}

		int target_objnum = objnum_for_ship_name(state.target_ship);
		if (target_objnum < 0 && !targeting_coords) {
			// The target was a non-ship -- an asteroid, a chunk of debris, another weapon --
			// and none of those has a name to find it by again.
			continue;
		}

		beam_fire_info info;
		memset(&info, 0, sizeof(info));

		info.beam_info_index = weapon_class;
		info.shooter = (shooter_objnum >= 0) ? &Objects[shooter_objnum] : nullptr;
		info.turret = turret;
		info.target = (target_objnum >= 0) ? &Objects[target_objnum] : nullptr;
		info.target_subsys = find_subsys_by_key(state.target_ship, state.target_subsys);
		info.target_pos1 = state.target_pos1;
		info.target_pos2 = state.target_pos2;
		info.starting_pos = state.last_start;
		info.local_fire_postion = state.local_fire_position;
		info.accuracy = 1.0f;
		info.num_shots = state.shot_count;
		info.bank = state.bank;
		info.point = state.firingpoint;
		info.team = static_cast<char>(lookup_team(state.team));
		info.burst_seed = 0;
		info.per_burst_rotation = 0.0f;
		info.burst_index = 0;

		info.bfi_flags = lookup_beam_flags(state.flags) &
		                 (BF_FORCE_FIRING | BF_IS_FIGHTER_BEAM | BF_TARGETING_COORDS | BF_FLOATING_BEAM);

		// The fire method is not kept on the beam, but it is fully determined by the flags that
		// are, and all beam_has_valid_params() does with it is decide which of shooter, turret
		// and target must be present.
		if (floating) {
			info.fire_method = BFM_SEXP_FLOATING_FIRED;
		} else if (has_flag_name(state.flags, "fighter_beam")) {
			info.fire_method = BFM_FIGHTER_FIRED;
		} else if (has_flag_name(state.flags, "force_firing")) {
			info.fire_method = BFM_TURRET_FORCE_FIRED;
		} else {
			info.fire_method = BFM_TURRET_FIRED;
		}

		// The aim vectors, so the restored beam sweeps exactly where the saved one was sweeping
		// rather than picking a fresh random spread.
		beam_info binfo;
		memset(&binfo, 0, sizeof(binfo));
		binfo.dir_a = state.dir_a;
		binfo.dir_b = state.dir_b;
		binfo.rot_axis = state.rot_axis;
		binfo.shot_count = static_cast<ubyte>(state.shot_count);
		for (int i = 0; i < state.shot_count && i < MAX_BEAM_SHOTS; i++) {
			binfo.shot_aim[i] = (i < static_cast<int>(state.shot_aim.size())) ? state.shot_aim[i] : 1.0f;
		}
		info.beam_info_override = &binfo;

		int objnum = beam_fire(&info);
		if (objnum < 0) {
			// beam_fire() turns down a shot it considers illegal -- the turret can no longer
			// see the target, say.  That is the same answer it would give the turret code, so
			// there is nothing to correct.
			continue;
		}

		beam* b = &Beams[Objects[objnum].instance];

		b->flags = lookup_beam_flags(state.flags);
		b->life_left = state.life_left;
		b->current_width_factor = state.current_width_factor;
		b->u_offset_local = state.u_offset_local;
		b->beam_glow_frame = state.beam_glow_frame;
		b->framecount = state.framecount;
		b->shot_index = state.shot_index;
		b->last_start = state.last_start;
		b->last_shot = state.last_shot;

		auto saved_state = lookup_weapon_state(state.weapon_state);

		if (saved_state == WeaponState::WARMUP) {
			b->warmup_stamp = translate_stamp(state.warmup_stamp);
			b->warmdown_stamp = -1;
			created++;
			continue;
		}

		// Past warmup.  beam_start_firing() clears the warmup stamp itself, and can still
		// refuse, in which case the beam is dropped the same way beam_move_all_post() would.
		if (!beam_start_firing(b)) {
			Objects[objnum].flags.set(Object::Object_Flags::Should_be_dead);
			continue;
		}

		// beam_start_firing() may have gone straight to warmdown on its own if the shot is no
		// longer legal; in that case leave it there rather than dragging it back.
		if (b->warmdown_stamp == -1 && saved_state == WeaponState::WARMDOWN) {
			beam_start_warmdown(b);
		}
		if (b->warmdown_stamp != -1) {
			b->warmdown_stamp = translate_stamp(state.warmdown_stamp >= 0 ? state.warmdown_stamp : b->warmdown_stamp);
		}

		// Re-assert what beam_start_firing() overwrote.
		b->life_left = state.life_left;
		b->framecount = state.framecount;
		b->shot_index = state.shot_index;

		created++;
	}

	if (created > 0) {
		mprintf(("CHECKPOINT => Restored %d beam(s).\n", created));
	}
}

// Put back the hull debris that was floating around.
//
// The ships these came off are destroyed and gone, so there is no source object to create them
// from; debris_create_only() takes explicit position and orientation for exactly that reason, and
// deduces nothing it is given a real value for.
void apply_debris(const checkpoint_data& data)
{
	int created = 0;

	for (const auto& state : data.debris) {
		int ship_class = lookup_ship_class(state.ship_class);
		if (ship_class < 0) {
			continue;
		}

		int model_num = Ship_info[ship_class].model_num;
		if (model_num < 0) {
			mprintf(("CHECKPOINT => No model loaded for '%s'; dropping its debris.\n", state.ship_class.c_str()));
			continue;
		}

		int submodel_num = model_find_submodel_index(model_num, state.submodel.c_str());
		if (submodel_num < 0) {
			mprintf(("CHECKPOINT => '%s' has no submodel '%s' any more; dropping that debris.\n",
			         state.ship_class.c_str(),
			         state.submodel.c_str()));
			continue;
		}

		int damage_type = -1;
		if (!state.damage_type.empty()) {
			for (int i = 0; i < static_cast<int>(Damage_types.size()); i++) {
				if (!stricmp(Damage_types[i].name, state.damage_type.c_str())) {
					damage_type = i;
					break;
				}
			}
		}

		auto objp = debris_create_only(-1,
			ship_class,
			state.parent_alt_name,
			lookup_team(state.team),
			state.hull_strength,
			0,
			model_num,
			submodel_num,
			&state.pos,
			&state.orient,
			true,
			false,
			damage_type);

		if (objp == nullptr) {
			continue;
		}

		objp->phys_info.vel = state.velocity;
		objp->phys_info.rotvel = state.rotational_velocity;
		objp->hull_strength = state.hull_strength;

		auto db = &Debris[objp->instance];
		// lifeleft is re-rolled from the ship class on creation, so put the saved one back --
		// including the -1 that makes a large chunk permanent.
		db->lifeleft = state.lifeleft;
		db->max_hull = state.max_hull;
		db->damage_mult = state.damage_mult;
		if (!state.species.empty()) {
			db->species = species_info_lookup(state.species.c_str());
		}
		if (state.do_not_expire) {
			db->flags.set(Debris_Flags::DoNotExpire);
		}

		created++;
	}

	if (created > 0) {
		mprintf(("CHECKPOINT => Restored %d piece(s) of hull debris.\n", created));
	}
}

// Events, goals and the log -- the mission's memory of what has already happened.
//
// Matched by name rather than by index, so an event moved around in FRED still finds its state.
// The fingerprint check makes that mostly academic, but it costs nothing and it means the failure
// mode for a mismatched file is "this event starts fresh" rather than "this event gets some other
// event's state".
void apply_mission_logic(const checkpoint_data& data)
{
	for (const auto& state : data.events) {
		auto it = std::find_if(Mission_events.begin(), Mission_events.end(), [&state](const mission_event& e) {
			return lcase_equal(e.name, state.name);
		});

		if (it == Mission_events.end()) {
			mprintf(("CHECKPOINT => Event '%s' is no longer in this mission.\n", state.name.c_str()));
			continue;
		}

		it->result = state.result;
		it->previous_result = state.previous_result;
		it->repeat_count = state.repeat_count;
		it->trigger_count = state.trigger_count;
		it->count = state.count;
		it->mission_log_flags = state.mission_log_flags;
		apply_int_flags(state.flags, it->flags);

		it->timestamp = TIMESTAMP(translate_stamp(state.timestamp));
		it->satisfied_time = TIMESTAMP(translate_stamp(state.satisfied_time));
		it->born_on_date = TIMESTAMP(translate_stamp(state.born_on_date));

		it->event_log_buffer = state.log_buffer;
		it->event_log_variable_buffer = state.log_variable_buffer;
		it->event_log_container_buffer = state.log_container_buffer;
		it->event_log_argument_buffer = state.log_argument_buffer;
		it->backup_log_buffer = state.backup_log_buffer;
	}

	for (const auto& state : data.goals) {
		auto it = std::find_if(Mission_goals.begin(), Mission_goals.end(), [&state](const mission_goal& g) {
			return lcase_equal(g.name, state.name);
		});

		if (it == Mission_goals.end()) {
			mprintf(("CHECKPOINT => Goal '%s' is no longer in this mission.\n", state.name.c_str()));
			continue;
		}

		it->satisfied = state.satisfied;
	}

	// The log is replayed wholesale rather than merged, so whatever is here now is discarded and
	// the saved log is the truth.  mission_log_add_entry() itself is not suppressed -- only the
	// departure call site in ship.cpp checks Game_restoring -- so this clear is what actually keeps
	// the restored log clean, and it only works because everything that can write to the log during
	// an apply runs before this point.  Anything added to the apply after it must either run earlier
	// or suppress its own logging.
	Log_entries.clear();
	for (const auto& state : data.log_entries) {
		log_entry entry;

		entry.type = static_cast<LogType>(state.type);
		entry.flags = state.flags;
		// Mission time, not an engine timestamp -- the clock has already been put back to match,
		// so this is restored as written.
		entry.timestamp = state.timestamp;
		entry.timer_padding = state.timer_padding;
		entry.index = state.index;
		entry.primary_team = lookup_team(state.primary_team);
		entry.secondary_team = lookup_team(state.secondary_team);
		strcpy_s(entry.pname, state.pname.c_str());
		strcpy_s(entry.sname, state.sname.c_str());
		entry.pname_display = state.pname_display;
		entry.sname_display = state.sname_display;

		Log_entries.push_back(std::move(entry));
	}

	// Sticky node states last, so nothing above can re-dirty them.  These are what stop an event
	// whose formula has already resolved from resolving it a second time and re-triggering an
	// arrival or a destruction that the ship restore has already put back the way it was.
	for (const auto& state : data.sexp_nodes) {
		if (state.index < 0 || state.index >= Num_sexp_nodes) {
			mprintf(("CHECKPOINT => SEXP node %d is out of range for this mission; skipping it.\n", state.index));
			continue;
		}
		if (Sexp_nodes[state.index].type == SEXP_NOT_USED) {
			continue;
		}

		Sexp_nodes[state.index].value = state.value;
		Sexp_nodes[state.index].flags = state.flags;

		if (state.value == SEXP_NUM_EVAL && !state.text.empty()) {
			strcpy_s(Sexp_nodes[state.index].text, state.text.c_str());
		}
	}

	for (const auto& state : data.containers) {
		auto container = get_sexp_container(state.name.c_str());
		if (container == nullptr) {
			mprintf(("CHECKPOINT => Container '%s' is no longer in this mission.\n", state.name.c_str()));
			continue;
		}

		container->list_data.clear();
		for (const auto& value : state.list_data) {
			container->list_data.push_back(value);
		}

		container->map_data.clear();
		for (size_t i = 0; i < state.map_keys.size() && i < state.map_values.size(); i++) {
			container->map_data.emplace(state.map_keys[i], state.map_values[i]);
		}
	}

	Mission_goal_timestamp = TIMESTAMP(translate_stamp(data.goal_timestamp));

	mprintf(("CHECKPOINT => Restored %d event(s), %d goal(s), %d log entr%s, %d SEXP node(s) and %d container(s).\n",
	         static_cast<int>(data.events.size()),
	         static_cast<int>(data.goals.size()),
	         static_cast<int>(data.log_entries.size()),
	         data.log_entries.size() == 1 ? "y" : "ies",
	         static_cast<int>(data.sexp_nodes.size()),
	         static_cast<int>(data.containers.size())));
}

void apply_clock(const checkpoint_data& data)
{
	// timestamp() reads a value snapshotted at the start of the frame, and adjusting the clock
	// does not refresh that snapshot, so ask before jumping and add the jump ourselves.  The
	// first frame of the restored mission will see exactly this value.
	auto mission_time_ms = static_cast<int>(data.mission_time_microseconds / 1000);
	int now_after_jump = timestamp() + mission_time_ms;

	if (data.mission_time_microseconds > 0) {
		timestamp_adjust_microseconds(data.mission_time_microseconds, TIMER_DIRECTION::FORWARD);
	}

	// A checkpoint written before stamps were translated has no clock recorded.  Leaving the
	// delta at zero restores those stamps verbatim, which is what such a file used to get.
	Stamp_delta = (data.saved_timestamp_ms != 0) ? (now_after_jump - data.saved_timestamp_ms) : 0;

	Missiontime = timestamp_get_mission_time();
	The_mission.HUD_timer_padding = data.hud_timer_padding;

	mprintf(("CHECKPOINT => Clock restored to mission time %d; shifting saved timestamps by %d ms.\n",
	         f2i(Missiontime),
	         Stamp_delta));
}

} // namespace

void mission_checkpoint_maybe_offer_resume()
{
	// A mid-mission load is already being serviced.  That path comes back through this same
	// event, so without this check the player would be asked a second time for the restore
	// they just asked for.
	if (Pending_load.in_progress || Pending_load.queued) {
		return;
	}

	if (!mission_checkpoint_allowed()) {
		return;
	}

	// The designer can suppress the offer for missions where a scripted mid-mission prompt is
	// the intended flow and an entry prompt would just be confusing.
	if (The_mission.flags[Mission::Mission_Flags::No_checkpoint_resume_prompt]) {
		return;
	}

	const SCP_string slot("default");

	// Nothing worth offering -- either no checkpoint at all, or one written for a version of
	// this mission that no longer matches.  Either way, say nothing and start normally.
	if (!mission_checkpoint_exists(slot)) {
		return;
	}

	checkpoint_data data;
	if (!checkpoint_read(slot, data)) {
		return;
	}

	SCP_string prompt;
	sprintf(prompt,
	        "%s\n\n%s",
	        XSTR("Resume from checkpoint?", 2002),
	        XSTR("You have a saved checkpoint for this mission.", 2003));

	int choice = popup(PF_USE_AFFIRMATIVE_ICON | PF_USE_NEGATIVE_ICON, 2, POPUP_NO, POPUP_YES, prompt.c_str());

	if (choice != 1) {
		mprintf(("CHECKPOINT => Player declined the checkpoint; starting the mission normally.\n"));
		return;
	}

	// The entry prompt has no arguments to take options from, so the loadout choices come from
	// the mission alone.  "Reopen loadout" is deliberately not available here: the player has
	// only just come through the loadout screen.
	LoadFlags flags = mission_load_flag_defaults();

	// No mission reload needed.  The reload the SEXP path performs exists only to get back to
	// a freshly parsed mission, and entering a mission is already exactly that -- so all that
	// is left is the bash, which mission_checkpoint_apply() does immediately after this.
	Pending_load.slot = slot;
	Pending_load.flags = flags;
	Pending_load.data = std::move(data);
	Pending_load.in_progress = true;
}

void mission_checkpoint_mission_complete()
{
	if (!The_mission.flags[Mission::Mission_Flags::Checkpoint_delete_on_completion]) {
		return;
	}

	int deleted = checkpoint_delete_all(SCP_string());
	if (deleted > 0) {
		mprintf(("CHECKPOINT => Mission complete; discarded %d checkpoint(s).\n", deleted));
	}

	Existence_cache.clear();
}

void mission_checkpoint_apply()
{
	if (!Pending_load.in_progress) {
		return;
	}

	const checkpoint_data& data = Pending_load.data;
	LoadFlags flags = Pending_load.flags;

	// Consume the request up front, so that a failure part-way through cannot leave us trying
	// to apply the same checkpoint again on the next mission load.
	Pending_load.in_progress = false;

	// If we somehow arrived in a different mission -- the restart failed and dropped the
	// player back to the main hall, say, and they then started something else -- the saved
	// state belongs to a mission that is not loaded and must not be applied to this one.
	if (stricmp(data.mission_filename.c_str(), Game_current_mission_filename) != 0) {
		mprintf(("CHECKPOINT => Checkpoint '%s' is for '%s' but '%s' is loaded; discarding it.\n",
		         data.slot.c_str(),
		         data.mission_filename.c_str(),
		         Game_current_mission_filename));
		mission_checkpoint_clear_pending();
		return;
	}

	mprintf(("CHECKPOINT => Applying checkpoint '%s' to '%s'.\n", data.slot.c_str(), Game_current_mission_filename));

	// Tell the rest of the engine that what follows is a restore rather than the mission actually
	// happening.  Ships put back into the mission skip the warp effect and the arrival
	// repositioning that would otherwise override the saved position, arrival music does not play,
	// nothing is written to the mission log, and the departure hook does not fire for departures a
	// script already saw in the run that was saved.  Game_restoring is a leftover from the original
	// savegame code and means exactly this; nothing else sets it.
	Game_restoring = 1;

	// The clock goes first: everything restored after this point stores timestamps that are
	// only meaningful relative to it.
	apply_clock(data);

	// Parse objects first: a ship the reconciliation is about to bring in should be created from
	// the parse object the checkpoint recorded, not the one the mission file describes.
	apply_parse_objects(data);

	// Then decide which ships should exist at all, before bashing state onto the ones that do.
	// Before the ships, because restore_dynamic_ships() builds a support ship's parse object
	// out of the mission's support settings, and the environment is where those live.
	apply_environment(data);
	apply_mission_extras(data);

	reconcile_ship_existence(data);

	for (const auto& state : data.ships) {
		if (state.disposition != ShipDisposition::Present) {
			continue;
		}

		bool skip_loadout = false;
		if (any(flags, LoadFlags::ReopenLoadout)) {
			// The player has just picked a loadout on the way back in; leave it alone.
			skip_loadout = is_player_wing_ship(state.name);
		} else if (any(flags, LoadFlags::KeepPlayerLoadout) || any(flags, LoadFlags::KeepWingLoadout)) {
			auto entry = ship_registry_get(state.name);
			bool is_player = (entry != nullptr && entry->has_objp() && &Objects[entry->objnum] == Player_obj);

			if (is_player) {
				skip_loadout = any(flags, LoadFlags::KeepPlayerLoadout);
			} else {
				skip_loadout = any(flags, LoadFlags::KeepWingLoadout) && is_player_wing_ship(state.name);
			}
		}

		apply_ship(state, skip_loadout);
	}

	// Turret targets reference other ships, so they can only be resolved once every ship has
	// been through apply_ship().
	for (const auto& state : data.ships) {
		if (state.disposition != ShipDisposition::Present) {
			continue;
		}
		auto entry = ship_registry_get(state.name);
		if (entry != nullptr && entry->has_shipp()) {
			resolve_turret_targets(&Ships[entry->shipnum], state.subsystems);
			restore_docks(&Ships[entry->shipnum], &Objects[entry->objnum], state.docks);
			resolve_ai_references(&Ships[entry->shipnum], state.ai);
		}
	}

	apply_asteroids(data);
	apply_wings(data);
	apply_variables(data);
	apply_scoring(data);

	// Debris is independent of everything else; it just needs the ship classes paged in, which the
	// mission load has already done.
	apply_debris(data);
	apply_projectiles(data);
	apply_beams(data);

	// After the world, because a restored event's state describes ships that now exist.
	apply_mission_logic(data);

	// HUD state last: the escort list is rebuilt from the ship flags, so every ship has to be in
	// its final state first.
	apply_hud_state(data);

	// Player_obj and friends still point at whatever the fresh load created.  If the player's
	// ship had its class changed above, change_ship_type() has already fixed the ship and
	// object up; the player pointers themselves are unchanged by that, so there is nothing
	// further to do here.

	Game_restoring = 0;

	mprintf(("CHECKPOINT => Applied checkpoint at mission time %d.\n", f2i(Missiontime)));

	// The script data comes back before the hook that reads it, and the hook runs after
	// Game_restoring has been cleared so that a script sees a mission in its final state rather
	// than one that is still being assembled.
	Script_data = data.script_data;

	if (scripting::hooks::OnCheckpointRestore->isActive()) {
		scripting::hooks::OnCheckpointRestore->run(
			scripting::hook_param_list(scripting::hook_param("Slot", 's', data.slot)));
	}

	// Release the snapshot.  A large mission's checkpoint is not small, and there is no reason
	// to hold it for the rest of the mission.
	mission_checkpoint_clear_pending();
}

// ------------------------------------------------------------------
// Designer-facing flag names
// ------------------------------------------------------------------

// The one place a load option's designer-facing name is written down.  Both the SEXP parser and
// the editor's dropdown read this, so they cannot drift apart.
namespace {

struct load_flag_entry {
	const char* name;
	LoadFlags flag;
};

const load_flag_entry Load_flag_table[] = {
	{"keep player loadout", LoadFlags::KeepPlayerLoadout},
	{"keep wing loadout", LoadFlags::KeepWingLoadout},
	{"reopen loadout", LoadFlags::ReopenLoadout},
	{"ignore mission changes", LoadFlags::IgnoreFingerprint},
};

} // namespace

bool mission_checkpoint_parse_load_flag(const char* name, LoadFlags& out)
{
	for (const auto& entry : Load_flag_table) {
		if (!stricmp(name, entry.name)) {
			out = entry.flag;
			return true;
		}
	}

	return false;
}

int mission_checkpoint_translate_stamp(int saved, int delta)
{
	// -1 (invalid), 0 (never) and 1 (immediate) are not points in time.  TIMESTAMP uses them as
	// sentinels (timer.h) and the legacy int stamps follow the same convention, as do the
	// dual-purpose arrival/departure delays, where a non-positive value is a delay in seconds
	// whose timer has not been armed yet.  Shifting any of those would invent a deadline.
	if (saved <= 1) {
		return saved;
	}

	auto shifted = static_cast<std::int64_t>(saved) + delta;

	// A stamp that had already elapsed must still have elapsed.  Restoring in a fresh session
	// gives a large negative delta, which could otherwise push one down onto a sentinel and turn
	// "long since past" into "never".
	if (shifted < 2) {
		return 2;
	}

	return static_cast<int>(shifted);
}

const SCP_vector<SCP_string>& mission_checkpoint_get_load_flag_names()
{
	static const SCP_vector<SCP_string> names = []() {
		SCP_vector<SCP_string> list;
		for (const auto& entry : Load_flag_table) {
			list.emplace_back(entry.name);
		}
		return list;
	}();

	return names;
}

void mission_checkpoint_level_init()
{
	// Both of these are keyed on the mission filename, which does not change when the mission
	// behind it does.  Edit a mission in FRED and re-enter it without restarting the game, and a
	// stale entry would go on reporting the checkpoint as valid, because nothing would re-read
	// either the checkpoint or the mission file.
	Existence_cache.clear();
	checkpoint_invalidate_fingerprint();

	// Not cleared by mission_checkpoint_clear_pending(), which has to survive the very reload a
	// checkpoint load asks for -- but the level teardown is exactly where one mission's script
	// data stops being about the mission that is loading.  A restore refills it immediately
	// afterwards, from the file.
	Script_data.clear();
}

void mission_checkpoint_set_script_data(const SCP_string& key, const SCP_string& value)
{
	if (key.empty()) {
		return;
	}
	Script_data[key] = value;
}

bool mission_checkpoint_get_script_data(const SCP_string& key, SCP_string& out_value)
{
	auto it = Script_data.find(key);
	if (it == Script_data.end()) {
		return false;
	}

	out_value = it->second;
	return true;
}
