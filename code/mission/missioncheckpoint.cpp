/*
 * Copyright (C) Freespace Open 2013.  All rights reserved.
 *
 * All source code herein is the property of Freespace Open. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 */

#include "mission/missioncheckpoint.h"

#include "debris/debris.h"
#include "gamesequence/gamesequence.h"
#include "globalincs/systemvars.h"
#include "hud/hudescort.h"
#include "hud/hudtarget.h"
#include "model/model.h"
#include "species_defs/species_defs.h"
#include "io/timer.h"
#include "iff_defs/iff_defs.h"
#include "mission/checkpointfields.h"
#include "mission/checkpointfile.h"
#include "mission/missioncampaign.h"
#include "mission/missiongoals.h"
#include "mission/missionlog.h"
#include "mission/missionparse.h"
#include "mod_table/mod_table.h"
#include "object/object.h"
#include "object/objectdock.h"
#include "parse/parselo.h"
#include "parse/sexp.h"
#include "parse/sexp_container.h"
#include "playerman/player.h"
#include "popup/popup.h"
#include "ship/ship.h"
#include "ship/shipfx.h"
#include "stats/scoring.h"
#include "weapon/weapon.h"

#include <algorithm>

extern char Game_current_mission_filename[];

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
		swp.primary_bank_rearm_time[i] = bank.rearm_time;
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
		swp.secondary_bank_rearm_time[i] = bank.rearm_time;
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

		// Never leave a subsystem above its (possibly changed) maximum.
		if (subsys->current_hits > subsys->max_hits) {
			subsys->current_hits = subsys->max_hits;
		}

		if (state.has_weapons) {
			load_weapons(subsys->weapons, state.weapons, true);
		}

		// Turret targets are resolved in a second pass, once every ship exists.
	}
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
		state.ship_class = ship_class_name(shipp->ship_info_index);
		state.team = team_name(shipp->team);
		state.display_name = shipp->display_name;
		state.cargo_title = shipp->cargo_title;
		state.cargo1 = shipp->cargo1;
		state.countermeasure_class = weapon_class_name(shipp->current_cmeasure);

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
		for (int i = 0; i < static_cast<int>(Ship_info.size()) && i < MAX_SHIP_CLASSES; i++) {
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
		state.primary_team = entry.primary_team;
		state.secondary_team = entry.secondary_team;
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
					  (Sexp_nodes[i].value == SEXP_NAN_FOREVER);

		if (!sticky && Sexp_nodes[i].flags == SNF_DEFAULT_VALUE) {
			continue;
		}

		sexp_node_state state;
		state.index = i;
		state.value = Sexp_nodes[i].value;
		state.flags = Sexp_nodes[i].flags;
		data.sexp_nodes.push_back(state);
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
		state.cargo1 = p_objp->cargo1;

		collect_def_flags(p_objp->flags, Parse_object_flags, Num_parse_object_flags, state.flags);

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

void mission_checkpoint_request_load(const SCP_string& slot, LoadFlags flags)
{
	if (!mission_checkpoint_allowed()) {
		mprintf(("CHECKPOINT => Checkpoints are switched off for this mission; not loading.\n"));
		return;
	}

	Pending_load.queued = true;
	Pending_load.slot = slot;
	Pending_load.flags = flags;
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
// KNOWN GAP: ship_cleanup() runs the On Ship Depart hook for departing and vanishing ships, so a
// script will see a departure it already saw in the run that was saved.  Suppressing that needs a
// general "a restore is in progress" notion, which belongs with the rest of the side-effect
// handling in milestone 2.
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
	shipp->cargo1 = state.cargo1;

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

// KNOWN GAP: wing_state carries no flags, so Wing_Flags::Gone and Departing are not restored --
// they are left at whatever replaying the waves produced.  Directives that key off a wing being
// wiped out can therefore read wrong.  Belongs with the rest of the mission logic state in
// milestone 2, alongside the goals and events those directives are made of.
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

	for (int i = 0; i < MAX_SHIP_CLASSES; i++) {
		Player->stats.m_okKills[i] = 0;
	}

	for (const auto& entry : data.scoring.class_kills) {
		int ship_class = ship_info_lookup(entry.first.c_str());
		if (ship_class < 0) {
			mprintf(("CHECKPOINT => Dropping kills for retired ship class '%s'.\n", entry.first.c_str()));
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
		p_objp->arrival_delay = state.arrival_delay;
		p_objp->departure_delay = state.departure_delay;
		p_objp->escort_priority = state.escort_priority;
		p_objp->respawn_priority = state.respawn_priority;
		p_objp->alt_type_index = state.alt_type_index;
		p_objp->callsign_index = state.callsign_index;
		p_objp->cargo1 = state.cargo1;

		apply_def_flags(state.flags, Parse_object_flags, Num_parse_object_flags, p_objp->flags);
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

	int escorts = 0;
	int flagged_in_file = 0;

	for (const auto& state : data.ships) {
		if (state.disposition != ShipDisposition::Present) {
			continue;
		}

		// What the file says, so a mismatch between this and the live flag points straight at the
		// ship restore rather than at the rebuild below.
		if (std::find(state.flags.begin(), state.flags.end(), SCP_string("escort")) != state.flags.end()) {
			flagged_in_file++;
		}

		auto entry = ship_registry_get(state.name);
		if (entry == nullptr || !entry->has_shipp() || !entry->has_objp()) {
			continue;
		}

		if (Ships[entry->shipnum].flags[Ship::Ship_Flags::Escort]) {
			hud_add_remove_ship_escort(entry->objnum, 1);
			escorts++;
		}
	}

	mprintf(("CHECKPOINT => Escort list: %d ship(s) flagged in the checkpoint, %d re-added.\n",
	         flagged_in_file,
	         escorts));

	if (Player == nullptr) {
		return;
	}

	for (int set = 0; set < MAX_KEYED_TARGETS; set++) {
		hud_target_hotkey_clear(set);
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

	// The log is replayed wholesale rather than merged: mission_log_add_entry() has been suppressed
	// throughout the restore (see Game_restoring), so whatever is here now is only what the fresh
	// load produced, and the saved log is the truth.
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
		entry.primary_team = state.primary_team;
		entry.secondary_team = state.secondary_team;
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
	// the mission instead.  "Reopen loadout" is deliberately not available here: the player has
	// only just come through the loadout screen.
	LoadFlags flags = LoadFlags::None;
	if (The_mission.flags[Mission::Mission_Flags::Checkpoint_keep_player_loadout]) {
		flags |= LoadFlags::KeepPlayerLoadout;
	}
	if (The_mission.flags[Mission::Mission_Flags::Checkpoint_keep_wing_loadout]) {
		flags |= LoadFlags::KeepWingLoadout;
	}

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
		}
	}

	apply_wings(data);
	apply_variables(data);
	apply_scoring(data);

	// Debris is independent of everything else; it just needs the ship classes paged in, which the
	// mission load has already done.
	apply_debris(data);

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
}
