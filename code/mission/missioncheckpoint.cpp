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
#include "nebula/neblightning.h"
#include "starfield/starfield.h"
#include "starfield/supernova.h"
#include "camera/photomode.h"
#include "controlconfig/controlsconfig.h"
#include "graphics/grinternal.h"
#include "hud/hudmessage.h"
#include "io/keycontrol.h"
#include "math/bitarray.h"
#include "object/objcollide.h"
#include "ship/subsysdamage.h"
#include "sound/ds.h"
#include "sound/sound.h"
#include "freespace.h"
#include "ai/ai.h"
#include "asteroid/asteroid.h"
#include "coordinate_points/coordinate_point.h"
#include "gamesnd/eventmusic.h"
#include "autopilot/autopilot.h"
#include "ai/aigoals.h"
#include "object/waypoint.h"
#include "gamesequence/gamesequence.h"
#include "globalincs/linklist.h"
#include "globalincs/memory/utils.h"
#include "globalincs/systemvars.h"
#include "hud/hudescort.h"
#include "hud/hudsquadmsg.h"
#include "menuui/techmenu.h"
#include "stats/medals.h"
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
#include "object/parseobjectdock.h"
#include "parse/parselo.h"
#include "parse/sexp.h"
#include "parse/sexp_container.h"
#include "playerman/player.h"
#include "popup/popup.h"
#include "prop/prop.h"
#include "scripting/global_hooks.h"
#include "scripting/hook_api.h"
#include "ship/ship.h"
#include "ship/shipfx.h"
#include "ship/shiphit.h"
#include "stats/scoring.h"
#include "weapon/beam.h"
#include "weapon/weapon.h"

#include <algorithm>
#include <array>

extern char Game_current_mission_filename[];
// These live in freespace.cpp with no header of their own; the graphics back ends and sexp.cpp
// declare them the same way.
extern int Game_subspace_effect;
extern void game_start_subspace_ambient_sound();
extern void game_stop_subspace_ambient_sound();

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
	// Has_display_name has to travel with ship_state::display_name.  set-display-name and the
	// Lua setter move the two together, so restoring the string without the flag leaves the name
	// set but unused, and restoring neither leaves a name the mission had cleared still showing.
	{Ship::Ship_Flags::Has_display_name, "has_display_name"},
	// Set the first time a ship screams, so it does not scream again.  Same class of state as the
	// player's built-in message budget, which is restored for the same reason.
	{Ship::Ship_Flags::Ship_has_screamed, "ship_has_screamed"},
	// The warp options read like parse-time settings, but alter-ship-flag, set-arrival-info and
	// set-departure-info all rewrite them mid-mission.
	{Ship::Ship_Flags::No_arrival_warp, "no_arrival_warp"},
	{Ship::Ship_Flags::No_departure_warp, "no_departure_warp"},
	{Ship::Ship_Flags::Same_arrival_warp_when_docked, "same_arrival_warp_when_docked"},
	{Ship::Ship_Flags::Same_departure_warp_when_docked, "same_departure_warp_when_docked"},
	// Set once ship_process_post() has recorded each bank's starting ammo.  Without it the first
	// frame after a restore records the *current* ammo as the starting ammo, and the rearm cap
	// silently drops to whatever the player had left when the checkpoint was written.
	{Ship::Ship_Flags::Ammo_count_recorded, "ammo_count_recorded"},
	// alter-ship-flag can set this mid-mission, and it decides whether the ship's death counts
	// towards the ship-type kill totals.
	{Ship::Ship_Flags::Ignore_count, "ignore_count"},
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
// checkpoint has to come back still wiped out rather than merely empty.  The genuinely parse-time
// flags (Ignore_count, Reinforcement) are reproduced by the mission load and are deliberately
// absent; the warp options are not parse-time, since alter-wing-flag can set every one of them.
// Has_display_name travels with wing_state::display_name for the same reason as the ship flag.
struct prop_flag_entry {
	Prop::Prop_Flags flag;
	const char* name;
};

// All of a prop's instance flags are render settings the SEXPs and scripts can set.
const prop_flag_entry Prop_flag_table[] = {
	{Prop::Prop_Flags::Glowmaps_disabled, "glowmaps_disabled"},
	{Prop::Prop_Flags::Draw_as_wireframe, "draw_as_wireframe"},
	{Prop::Prop_Flags::Render_full_detail, "render_full_detail"},
	{Prop::Prop_Flags::Render_without_light, "render_without_light"},
	{Prop::Prop_Flags::Render_without_diffuse, "render_without_diffuse"},
	{Prop::Prop_Flags::Render_without_glowmap, "render_without_glowmap"},
	{Prop::Prop_Flags::Render_without_normalmap, "render_without_normalmap"},
	{Prop::Prop_Flags::Render_without_heightmap, "render_without_heightmap"},
	{Prop::Prop_Flags::Render_without_ambientmap, "render_without_ambientmap"},
	{Prop::Prop_Flags::Render_without_specmap, "render_without_specmap"},
	{Prop::Prop_Flags::Render_without_reflectmap, "render_without_reflectmap"},
	{Prop::Prop_Flags::Render_with_alpha_mult, "render_with_alpha_mult"},
};

struct exit_flag_entry {
	Ship::Exit_Flags flag;
	const char* name;
};

// What an exited record remembers about how the ship left, beyond Destroyed/Departed (which the
// disposition carries) and Player_deleted (which the restore corrects itself).
const exit_flag_entry Exit_flag_table[] = {
	{Ship::Exit_Flags::Cargo_known, "cargo_known"},
	{Ship::Exit_Flags::Been_tagged, "been_tagged"},
	{Ship::Exit_Flags::Red_alert_carry, "red_alert_carry"},
	{Ship::Exit_Flags::From_player_wing, "from_player_wing"},
};

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
	{Ship::Wing_Flags::No_arrival_warp, "no_arrival_warp"},
	{Ship::Wing_Flags::No_departure_warp, "no_departure_warp"},
	{Ship::Wing_Flags::Same_arrival_warp_when_docked, "same_arrival_warp_when_docked"},
	{Ship::Wing_Flags::Same_departure_warp_when_docked, "same_departure_warp_when_docked"},
	{Ship::Wing_Flags::Waypoints_no_formation, "waypoints_no_formation"},
	{Ship::Wing_Flags::Has_display_name, "has_display_name"},
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

struct animation_instance_flag_entry {
	animation::Animation_Instance_Flags flag;
	const char* name;
};

// Where a running animation had got to in its own loop handling.  By name like every other flag
// set: these are positions in a FLAG_LIST, and the list gains entries.
const animation_instance_flag_entry Animation_instance_flag_table[] = {
	{animation::Animation_Instance_Flags::Stop_after_next_loop, "stop_after_next_loop"},
	{animation::Animation_Instance_Flags::Seamless_loop_shutdown, "seamless_loop_shutdown"},
	{animation::Animation_Instance_Flags::Seamless_fully_started, "seamless_fully_started"},
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

// Alt names and callsigns.  Mission_alt_types and Mission_callsigns are seeded by the mission
// parse, but change-alt-name and change-callsign append to them at runtime, so an index is only
// good within one run.  On the way back in a name the fresh parse does not have is added, which
// is exactly what the SEXP did the first time.
SCP_string alt_name_for_index(int index)
{
	return (index >= 0) ? SCP_string(mission_parse_lookup_alt_index(index)) : SCP_string();
}

int alt_index_for_name(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}

	int index = mission_parse_lookup_alt(name.c_str());
	if (index < 0) {
		index = mission_parse_add_alt(name.c_str());
	}
	if (index < 0) {
		mprintf(("CHECKPOINT => No room for alt name '%s'.\n", name.c_str()));
	}
	return index;
}

SCP_string callsign_for_index(int index)
{
	return (index >= 0) ? SCP_string(mission_parse_lookup_callsign_index(index)) : SCP_string();
}

int callsign_index_for_name(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}

	int index = mission_parse_lookup_callsign(name.c_str());
	if (index < 0) {
		index = mission_parse_add_callsign(name.c_str());
	}
	if (index < 0) {
		mprintf(("CHECKPOINT => No room for callsign '%s'.\n", name.c_str()));
	}
	return index;
}

// Armor and damage types come from tables, so both go by name.  Empty means "none" (-1), which
// for a ship means the class default.
SCP_string armor_type_name(int armor_type)
{
	if (armor_type < 0 || armor_type >= static_cast<int>(Armor_types.size())) {
		return SCP_string();
	}
	return Armor_types[armor_type].GetNamePtr();
}

int lookup_armor_type(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}
	for (int i = 0; i < static_cast<int>(Armor_types.size()); i++) {
		if (Armor_types[i].IsName(name.c_str())) {
			return i;
		}
	}
	mprintf(("CHECKPOINT => Armor type '%s' no longer exists.\n", name.c_str()));
	return -1;
}

SCP_string damage_type_name(int damage_type)
{
	if (damage_type < 0 || damage_type >= static_cast<int>(Damage_types.size())) {
		return SCP_string();
	}
	return Damage_types[damage_type].name;
}

int lookup_damage_type(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}
	for (int i = 0; i < static_cast<int>(Damage_types.size()); i++) {
		if (!stricmp(Damage_types[i].name, name.c_str())) {
			return i;
		}
	}
	mprintf(("CHECKPOINT => Damage type '%s' no longer exists.\n", name.c_str()));
	return -1;
}

// The texture replacements applied to a model instance, recovered as (old, new) texture names:
// the instance only holds bitmap ids, but the model still knows each slot's original texture and
// bmpman knows every loaded bitmap's filename.  "invisible" is the engine's own spelling for a
// texture switched off, and is what the replace-texture SEXP accepts back.
void capture_instance_textures(int model_instance_num, SCP_vector<SCP_string>& out_old, SCP_vector<SCP_string>& out_new)
{
	polymodel_instance* pmi = (model_instance_num >= 0) ? model_get_instance(model_instance_num) : nullptr;
	if (pmi == nullptr || pmi->texture_replace == nullptr) {
		return;
	}
	polymodel* pm = model_get(pmi->model_num);
	if (pm == nullptr) {
		return;
	}

	for (int j = 0; j < pm->n_textures; j++) {
		for (int t = 0; t < TM_NUM_TYPES; t++) {
			int replacement = (*pmi->texture_replace)[j * TM_NUM_TYPES + t];
			if (replacement == -1) {
				continue;
			}
			int original = pm->maps[j].textures[t].GetOriginalTexture();
			if (original < 0) {
				continue;
			}

			char old_name[MAX_FILENAME_LEN];
			bm_get_filename(original, old_name);
			out_old.emplace_back(old_name);

			if (replacement == REPLACE_WITH_INVISIBLE) {
				out_new.emplace_back("invisible");
			} else {
				char new_name[MAX_FILENAME_LEN];
				bm_get_filename(replacement, new_name);
				out_new.emplace_back(new_name);
			}
		}
	}
}

// change-iff-color stores a colour-table index that iff_init_color() hands out at runtime, so the
// override goes out as the colour itself and is re-registered on the way back.
void store_iff_colors(const SCP_map<std::pair<int, int>, int>& in, SCP_vector<iff_color_state>& out)
{
	for (const auto& entry : in) {
		iff_color_state state;
		state.observer = team_name(entry.first.first);
		state.observed = team_name(entry.first.second);
		const color* c = iff_get_color(entry.second, 0);
		if (c != nullptr) {
			state.r = c->red;
			state.g = c->green;
			state.b = c->blue;
		}
		out.push_back(std::move(state));
	}
}

void load_iff_colors(const SCP_vector<iff_color_state>& in, SCP_map<std::pair<int, int>, int>& out)
{
	for (const auto& state : in) {
		int observer = lookup_team(state.observer);
		int observed = lookup_team(state.observed);
		if (observer < 0 || observed < 0) {
			continue;
		}
		out[{observer, observed}] = iff_init_color(state.r, state.g, state.b);
	}
}

// Ai_classes comes from ai.tbl, so an AI class is a table index and goes by name.
SCP_string ai_class_name(int ai_class)
{
	if (ai_class < 0 || ai_class >= static_cast<int>(Ai_class_names.size())) {
		return SCP_string();
	}
	return Ai_class_names[ai_class];
}

int lookup_ai_class(const SCP_string& name)
{
	if (name.empty()) {
		return -1;
	}

	for (size_t i = 0; i < Ai_class_names.size(); i++) {
		if (!stricmp(Ai_class_names[i], name.c_str())) {
			return static_cast<int>(i);
		}
	}

	mprintf(("CHECKPOINT => AI class '%s' no longer exists.\n", name.c_str()));
	return -1;
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

// How many entries Messages[] had once the mission was parsed, noted on every entry into
// gameplay.  Anything past it at store time was added by a script and has to travel with the
// checkpoint, since a reload puts the parse's count back.
int Parsed_message_count = 0;

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
		CKPT_SHIP_MISSION_TIMES(CKPT_STORE_INT)
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
		CKPT_SHIP_MISSION_TIMES(CKPT_LOAD_INT)
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
		CKPT_SUBSYS_MISSION_TIMES(CKPT_STORE_INT)
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
		CKPT_SUBSYS_MISSION_TIMES(CKPT_LOAD_INT)
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
	// Keeping the loadout means keeping all of it.  The player may have just picked a different
	// ship, or different weapons in the same banks, and the checkpoint's ammo counts, capacities
	// and bank selections describe weapons that are no longer there: a saved
	// current_secondary_bank of 1 on a ship that now has one bank is an out-of-range index the
	// firing code asserts on.  So only the flags and the tertiary/detonation scalars come back,
	// and the bank selection is put back to what the fresh loadout gave it.
	if (!restore_classes) {
		int current_primary = swp.current_primary_bank;
		int current_secondary = swp.current_secondary_bank;
		int previous_primary = swp.previous_primary_bank;
		int previous_secondary = swp.previous_secondary_bank;

		apply_flags(in.flags, Weapon_flag_table, swp.flags);
		load_weapon_scalars(swp, in.scalars);

		swp.current_primary_bank = current_primary;
		swp.current_secondary_bank = current_secondary;
		swp.previous_primary_bank = previous_primary;
		swp.previous_secondary_bank = previous_secondary;
		return;
	}

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
	{
		auto& out = state.vecs;
		CKPT_AI_VECS(CKPT_STORE_VEC)
	}
}

// ai_override_ci is a control_info rather than part of ai_info, so it gets its own pair.
void store_ai_override(const control_info& obj, SCP_map<SCP_string, float>& out)
{
	CKPT_AI_OVERRIDE_FLOATS(CKPT_STORE_FLOAT)
}

void load_ai_override(control_info& obj, const SCP_map<SCP_string, float>& in)
{
	CKPT_AI_OVERRIDE_FLOATS(CKPT_LOAD_FLOAT)
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
	{
		const auto& in = state.vecs;
		CKPT_AI_VECS(CKPT_LOAD_VEC)
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
	// shipp is null for a wing's goals, which belong to no ship yet; a docker index needs that
	// ship's model to resolve, so in that case only a name can be written.
	if (goal.flags[AI::Goal_Flags::Docker_index_valid] && shipp != nullptr) {
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
		// ai_class_name() returns empty for the SUBSYS_STATUS_NO_CHANGE sentinel and any other
		// out-of-range value, which is what "leave the parse's value alone" means on the way back.
		state.ai_class = ai_class_name(sssp->ai_class);
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
		if (!state.ai_class.empty()) {
			int ai_class = lookup_ai_class(state.ai_class);
			if (ai_class >= 0) {
				sssp->ai_class = ai_class;
			}
		}
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
// The gauge set the HUD is drawing: the player's ship class has its own if the table gave it
// one, otherwise the defaults.  hud_get_gauge() searches in the same order.
const SCP_vector<std::unique_ptr<HudGauge>>& live_hud_gauges()
{
	if (Player_ship != nullptr && Player_ship->ship_info_index >= 0 &&
	    !Ship_info[Player_ship->ship_info_index].hud_gauges.empty()) {
		return Ship_info[Player_ship->ship_info_index].hud_gauges;
	}
	return default_hud_gauges;
}

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

	out.player_use_ai = Player_use_ai;
	out.perspective_locked = Perspective_locked;
	out.slew_locked = Slew_locked;
	out.viewer_mode = Viewer_mode;

	for (const auto& info : ai_get_preferred_primary_info()) {
		preferred_primary_state state;
		state.subject = info.subject.object_name;
		state.target = info.target.object_name;
		state.weapon = weapon_class_name(info.weapon_index);
		out.preferred_primaries.push_back(std::move(state));
	}
	for (const auto& info : ai_get_huge_fire_info()) {
		huge_fire_state state;
		state.team = team_name(info.team);
		state.weapon = weapon_class_name(info.weapon_index);
		if (info.ship_registry_index >= 0 && info.ship_registry_index < static_cast<int>(Ship_registry.size())) {
			state.ship = Ship_registry[info.ship_registry_index].name;
		}
		state.max_fire_count = info.max_fire_count;
		out.huge_fire.push_back(std::move(state));
	}

	if (Player != nullptr) {
		out.player_throttle = Player->ci.forward_cruise_percent;
		out.auto_targeting = (Player->flags & PLAYER_FLAGS_AUTO_TARGETING) != 0;
		out.auto_match_speed = (Player->flags & PLAYER_FLAGS_AUTO_MATCH_SPEED) != 0;
		out.match_target = (Player->flags & PLAYER_FLAGS_MATCH_TARGET) != 0;
		out.death_message = Player->death_message;
		out.friendly_hits = Player->friendly_hits;
		out.friendly_damage = Player->friendly_damage;
		out.friendly_last_hit_time = Player->friendly_last_hit_time;
		out.last_warning_message_time = Player->last_warning_message_time;
	}

	if (Player != nullptr) {
		out.promoted = (Player->flags & PLAYER_FLAGS_PROMOTED) != 0;
		out.no_check_all_alone_msg = (Player->flags & PLAYER_FLAGS_NO_CHECK_ALL_ALONE_MSG) != 0;
	}

	// The message queue, entry by entry; see message_queue_state.
	for (int i = 0; i < MessageQ_num && i < static_cast<int>(MessageQ.size()); i++) {
		const auto& entry = MessageQ[i];
		if (entry.message_num < 0 || entry.message_num >= static_cast<int>(Messages.size())) {
			continue;
		}

		message_queue_state state;
		state.message = Messages[entry.message_num].name;
		if (entry.special_message != nullptr) {
			state.special_message = entry.special_message.get();
		}
		state.who_from = entry.who_from;
		state.source = entry.source;
		state.builtin_type = entry.builtin_type;
		state.flags = entry.flags;
		state.group = entry.group;
		state.priority = entry.priority;
		state.time_added = entry.time_added;
		state.window_timestamp = entry.window_timestamp.value();
		state.min_delay_stamp = entry.min_delay_stamp.value();
		if (entry.event_num_to_cancel >= 0 && entry.event_num_to_cancel < static_cast<int>(Mission_events.size())) {
			state.event_to_cancel = Mission_events[entry.event_num_to_cancel].name;
		}
		out.message_queue.push_back(std::move(state));
	}

	// The order history; see squadmsg_history_state.
	for (const auto& entry : Squadmsg_history) {
		squadmsg_history_state state;
		if (entry.order < 0 || entry.order >= static_cast<int>(Player_orders.size())) {
			continue;
		}
		state.order = Player_orders[entry.order].parse_name;
		state.to_all_fighters = (entry.order_to < 0);
		if (entry.order_to >= 0 && entry.order_to < static_cast<int>(Parse_names.size())) {
			state.order_to = Parse_names[entry.order_to];
		}
		if (entry.target >= 0 && entry.target < static_cast<int>(Parse_names.size())) {
			state.target = Parse_names[entry.target];
		}
		if (entry.order_from >= 0 && entry.order_from < MAX_SHIPS) {
			state.order_from = Ships[entry.order_from].ship_name;
		}
		// The subsystem index is into the target ship's list; only a live target can name it, and
		// only if it still has that many subsystems (the target's class may have changed since).
		if (entry.special_index >= 0 && !state.target.empty()) {
			int target_shipnum = ship_name_lookup(state.target.c_str());
			if (target_shipnum >= 0 && entry.special_index < Ship_info[Ships[target_shipnum].ship_info_index].n_subsystems) {
				ship_subsys* subsys = ship_get_indexed_subsys(&Ships[target_shipnum], entry.special_index);
				if (subsys != nullptr && subsys->system_info != nullptr) {
					state.special_subsys = subsys->system_info->subobj_name;
				}
			}
		}
		state.order_time = entry.order_time;
		out.squad_history.push_back(std::move(state));
	}

	// Messages a script added since the parse; see message_state and Parsed_message_count.
	for (int i = MAX(Parsed_message_count, Num_builtin_messages); i < Num_messages && i < static_cast<int>(Messages.size()); i++) {
		message_state state;
		state.name = Messages[i].name;
		state.text = Messages[i].message;
		state.persona = persona_name(Messages[i].persona_index);
		state.multi_team = Messages[i].multi_team;
		out.added_messages.push_back(std::move(state));
	}

	// What the mission has granted so far; see the field comments.
	{
		SCP_vector<int> ships;
		SCP_vector<int> weapons;
		mission_campaign_get_granted(ships, weapons);
		for (int ship_class : ships) {
			out.granted_ships.push_back(ship_class_name(ship_class));
		}
		for (int weapon_class : weapons) {
			out.granted_weapons.push_back(weapon_class_name(weapon_class));
		}
	}
	for (const auto& sip : Ship_info) {
		if (sip.flags[Ship::Info_Flags::In_tech_database]) {
			out.tech_ships.emplace_back(sip.name);
		}
	}
	for (const auto& wip : Weapon_info) {
		if (wip.wi_flags[Weapon::Info_Flags::In_tech_database]) {
			out.tech_weapons.emplace_back(wip.name);
		}
	}
	for (const auto& intel : Intel_info) {
		if (intel.flags & IIF_IN_TECH_DATABASE) {
			out.tech_intel.emplace_back(intel.name);
		}
	}

	for (const auto& reinforcement : Reinforcements) {
		reinforcement_state state;
		state.name = reinforcement.name;
		state.num_uses = reinforcement.num_uses;
		state.available = (reinforcement.flags & RF_IS_AVAILABLE) != 0;
		out.reinforcements.push_back(std::move(state));
	}

	// --- HUD and input ---
	out.hud_present = true;

	// Only gauges a SEXP has touched: hidden, overridden or recoloured.  The rest are in whatever
	// state the player's HUD configuration puts them, which the restore leaves alone.
	for (const auto& gauge : live_hud_gauges()) {
		hud_gauge_state state;
		state.custom = gauge->isCustom();
		state.name = state.custom ? gauge->getCustomGaugeName() : gauge->getConfigName();
		state.active = gauge->isActiveIgnoringOverride();
		state.sexp_override = gauge->isSexpOverridden();
		state.sexp_color = gauge->isSexpColorLocked();

		if (state.active && !state.sexp_override && !state.sexp_color) {
			continue;
		}
		if (state.sexp_color) {
			const color& c = gauge->getColor();
			state.color[0] = c.red;
			state.color[1] = c.green;
			state.color[2] = c.blue;
			state.color[3] = c.alpha;
		}
		out.hud_gauges.push_back(std::move(state));
	}

	out.hud_high_contrast = HUD_high_contrast;
	out.disable_cockpits = Disable_cockpits;
	out.disable_cockpit_sway = Disable_cockpit_sway;
	out.sensor_static_forced = Sensor_static_forced;

	// Ignored_keys is CCFG_MAX long, so actions a script added past that cannot be ignored and
	// are not looked at.
	for (int i = 0; i < CCFG_MAX && i < static_cast<int>(Control_config.size()); i++) {
		if (Ignored_keys[i] != 0) {
			ignored_key_state state;
			state.action = Control_config[i].text;
			state.count = Ignored_keys[i];
			out.ignored_keys.push_back(std::move(state));
		}
	}

	for (const auto& line : Msg_scrollback_vec) {
		scrollback_line_state state;
		state.time = line.time;
		state.source = line.source;
		state.text = line.text;
		out.scrollback.push_back(std::move(state));
	}

	SCP_vector<sexp_music_entry> playing;
	sexp_music_get_playing(playing);
	for (const auto& entry : playing) {
		file_sound_state state;
		state.filename = entry.filename;
		state.type = entry.type;
		state.loop = entry.loop;
		state.paused = entry.paused;
		state.variable = entry.variable;
		out.file_sounds.push_back(std::move(state));
	}
}

// The streams play-sound-from-file had going.  After the variables, since a stream started
// through a variable is identified by it, and the load writes the new handle into that variable.
void apply_file_sounds(const checkpoint_data& data)
{
	if (!data.mission.hud_present) {
		return;
	}

	for (const auto& state : data.mission.file_sounds) {
		sexp_music_entry entry;
		entry.filename = state.filename;
		entry.type = state.type;
		entry.loop = state.loop;
		entry.paused = state.paused;
		entry.variable = state.variable;
		sexp_music_restore(entry);
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

	// The fresh load starts with the player in control; handing over is all that is needed here,
	// since the AI mode, goals and override the sequence had given the player's ship come back
	// with its AI state.  (Taking control back would go through sexp_player_use_ai(false), which
	// clears those, but a fresh load has nothing to clear.)
	if (state.player_use_ai) {
		Player_use_ai = true;
	}
	Perspective_locked = state.perspective_locked;
	Slew_locked = state.slew_locked;
	Viewer_mode = state.viewer_mode;

	// Both tables are rebuilt.  The subject and target of a preferred primary are whatever text
	// the SEXP named, and the engine's own evaluator turns that back into a reference; a huge-fire
	// permission goes back in through the same call good-secondary-time makes.
	{
		auto& preferred = ai_get_preferred_primary_info();
		preferred.clear();
		for (const auto& saved : state.preferred_primaries) {
			int weapon_class = lookup_weapon_class(saved.weapon);
			if (weapon_class < 0 || saved.subject.empty() || saved.target.empty()) {
				continue;
			}
			primary_fire_info info;
			eval_object_ship_wing_point_team(&info.subject, -1, saved.subject.c_str());
			eval_object_ship_wing_point_team(&info.target, -1, saved.target.c_str());
			info.weapon_index = weapon_class;
			preferred.push_back(std::move(info));
		}

		ai_get_huge_fire_info().clear();
		for (const auto& saved : state.huge_fire) {
			int team = lookup_team(saved.team);
			int weapon_class = lookup_weapon_class(saved.weapon);
			if (team < 0 || weapon_class < 0 || saved.ship.empty()) {
				continue;
			}
			ai_good_secondary_time(team, weapon_class, saved.max_fire_count, saved.ship.c_str());
		}
	}

	if (Player != nullptr) {
		Player->ci.forward_cruise_percent = state.player_throttle;
		if (state.auto_targeting) {
			Player->flags |= PLAYER_FLAGS_AUTO_TARGETING;
		}
		if (state.auto_match_speed) {
			Player->flags |= PLAYER_FLAGS_AUTO_MATCH_SPEED;
		}
		if (state.match_target) {
			Player->flags |= PLAYER_FLAGS_MATCH_TARGET;
		}
		Player->death_message = state.death_message;
		Player->friendly_hits = state.friendly_hits;
		Player->friendly_damage = state.friendly_damage;
		Player->friendly_last_hit_time = state.friendly_last_hit_time;
		Player->last_warning_message_time = state.last_warning_message_time;
	}

	if (Player != nullptr) {
		if (state.promoted) {
			Player->flags |= PLAYER_FLAGS_PROMOTED;
		}
		if (state.no_check_all_alone_msg) {
			Player->flags |= PLAYER_FLAGS_NO_CHECK_ALL_ALONE_MSG;
		}
	}

	// The grants are rebuilt rather than merged, so a grant made after the checkpoint in this
	// session is rolled back with everything else.  The tech flags only ever go on: nothing in a
	// mission takes an entry out of the tech room, so re-setting what was set is complete.
	mission_campaign_clear_granted();
	for (const auto& name : state.granted_ships) {
		int ship_class = lookup_ship_class(name);
		if (ship_class >= 0) {
			mission_campaign_save_persistent(CAMPAIGN_PERSISTENT_SHIP, ship_class);
		}
	}
	for (const auto& name : state.granted_weapons) {
		int weapon_class = lookup_weapon_class(name);
		if (weapon_class >= 0) {
			mission_campaign_save_persistent(CAMPAIGN_PERSISTENT_WEAPON, weapon_class);
		}
	}
	// The lists are the complete set of what was in the tech room, so the room is emptied first:
	// tech-reset-to-default can take entries out, and an entry a previous session added is still
	// flagged in this one.
	for (auto& sip : Ship_info) {
		sip.flags.remove(Ship::Info_Flags::In_tech_database);
	}
	for (auto& wip : Weapon_info) {
		wip.wi_flags.remove(Weapon::Info_Flags::In_tech_database);
	}
	for (auto& intel : Intel_info) {
		intel.flags &= ~IIF_IN_TECH_DATABASE;
	}
	for (const auto& name : state.tech_ships) {
		int ship_class = lookup_ship_class(name);
		if (ship_class >= 0) {
			Ship_info[ship_class].flags.set(Ship::Info_Flags::In_tech_database);
		}
	}
	for (const auto& name : state.tech_weapons) {
		int weapon_class = lookup_weapon_class(name);
		if (weapon_class >= 0) {
			Weapon_info[weapon_class].wi_flags.set(Weapon::Info_Flags::In_tech_database);
		}
	}
	for (const auto& name : state.tech_intel) {
		int intel = intel_info_lookup(name.c_str());
		if (intel >= 0) {
			Intel_info[intel].flags |= IIF_IN_TECH_DATABASE;
		}
	}
}

// The mission's own record of what has been said and ordered: script-added messages, the message
// queue and the order history.  After the ships and the mission logic, since the queue refers to
// events and the history to ships.  Nothing here writes to the mission log.
void apply_mission_history(const checkpoint_data& data)
{
	const auto& state = data.mission;

	if (!state.present) {
		return;
	}

	// Script-added messages first, since the queue may name one.
	for (const auto& added : state.added_messages) {
		bool exists = false;
		for (int i = 0; i < Num_messages && i < static_cast<int>(Messages.size()); i++) {
			if (!stricmp(Messages[i].name, added.name.c_str())) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			add_message(added.name.c_str(), added.text.c_str(), lookup_persona(added.persona), added.multi_team);
		}
	}

	MessageQ.clear();
	MessageQ_num = 0;
	for (const auto& saved : state.message_queue) {
		// A built-in message and a mission message can share a name; the engine itself keeps
		// them apart by searching only the built-ins for one and only the mission's for the
		// other (message_send_builtin / change_message), so the search here does the same.
		int first = (saved.builtin_type >= 0) ? 0 : Num_builtin_messages;
		int last = (saved.builtin_type >= 0) ? Num_builtin_messages : Num_messages;

		int message_num = -1;
		for (int i = first; i < last && i < static_cast<int>(Messages.size()); i++) {
			if (!stricmp(Messages[i].name, saved.message.c_str())) {
				message_num = i;
				break;
			}
		}
		if (message_num < 0) {
			mprintf(("CHECKPOINT => Queued message '%s' no longer exists; dropping it.\n", saved.message.c_str()));
			continue;
		}

		MessageQ.emplace_back();
		auto& entry = MessageQ.back();
		entry.time_added = saved.time_added;
		entry.window_timestamp = TIMESTAMP(translate_stamp(saved.window_timestamp));
		entry.priority = saved.priority;
		entry.message_num = message_num;
		if (!saved.special_message.empty()) {
			entry.special_message.reset(vm_strdup(saved.special_message.c_str()));
		}
		strcpy_s(entry.who_from, saved.who_from.c_str());
		entry.source = saved.source;
		entry.builtin_type = saved.builtin_type;
		entry.flags = saved.flags;
		entry.min_delay_stamp = TIMESTAMP(translate_stamp(saved.min_delay_stamp));
		entry.group = saved.group;
		entry.event_num_to_cancel = saved.event_to_cancel.empty() ? -1 : mission_event_lookup(saved.event_to_cancel.c_str());
		MessageQ_num++;
	}

	Squadmsg_history.clear();
	for (const auto& saved : state.squad_history) {
		squadmsg_history entry;
		for (int i = 0; i < static_cast<int>(Player_orders.size()); i++) {
			if (!stricmp(Player_orders[i].parse_name.c_str(), saved.order.c_str())) {
				entry.order = i;
				break;
			}
		}
		if (entry.order < 0) {
			continue;
		}
		entry.order_to = saved.to_all_fighters ? -1 : get_parse_name_index(saved.order_to.c_str());
		entry.target = saved.target.empty() ? -1 : get_parse_name_index(saved.target.c_str());
		entry.order_from = saved.order_from.empty() ? -1 : ship_name_lookup(saved.order_from.c_str());
		if (!saved.special_subsys.empty() && !saved.target.empty()) {
			int target_shipnum = ship_name_lookup(saved.target.c_str());
			if (target_shipnum >= 0) {
				entry.special_index = ship_find_subsys(&Ships[target_shipnum], saved.special_subsys.c_str());
			}
		}
		entry.order_time = saved.order_time;
		Squadmsg_history.push_back(entry);
	}
}

// Separate from the rest of the extras because of when it has to run.  Bringing a reinforcement
// ship back in goes through mission_did_ship_arrive(), which counts a use and marks the
// reinforcement available exactly as if the player had just called it -- so this has to come
// after the arrival reconciliation, or the saved count is incremented on top of.
void apply_reinforcements(const checkpoint_data& data)
{
	const auto& state = data.mission;

	if (!state.present) {
		return;
	}

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
	out.support_arrival_anchor = anchor_name(support.arrival_anchor);
	out.support_departure_anchor = anchor_name(support.departure_anchor);

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

	// Only overwrite an anchor the checkpoint actually resolved; a name that no longer names
	// anything leaves the mission's own anchor in place rather than clearing it.
	auto support_arrival = lookup_anchor(in.support_arrival_anchor);
	if (support_arrival.isValid()) {
		support.arrival_anchor = support_arrival;
	}
	auto support_departure = lookup_anchor(in.support_departure_anchor);
	if (support_departure.isValid()) {
		support.departure_anchor = support_departure;
	}

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

	out.supernova_stage = static_cast<int>(supernova_stage());
	out.supernova_total = supernova_time_total();
	out.supernova_left = supernova_seconds_left();

	out.time_compression = f2fl(Game_time_compression);
	out.time_compression_locked = Time_compression_locked;

	// The field definition, since set-asteroid-field and friends rewrite it and a field a SEXP
	// created is not in the mission file at all.  The rocks go separately; see store_asteroids().
	out.has_asteroid_field = true;
	out.asteroid_field_num_initial = Asteroid_field.num_initial_asteroids;
	out.asteroid_field_active = (Asteroid_field.field_type == FT_ACTIVE);
	out.asteroid_field_is_debris = (Asteroid_field.debris_genre == DG_DEBRIS);
	out.asteroid_field_enhanced_checks = Asteroid_field.enhanced_visibility_checks;
	out.asteroid_field_has_inner_bound = Asteroid_field.has_inner_bound;
	out.asteroid_field_speed = Asteroid_field.speed;
	out.asteroid_field_bound_rad = Asteroid_field.bound_rad;
	out.asteroid_field_vel = Asteroid_field.vel;
	out.asteroid_field_min = Asteroid_field.min_bound;
	out.asteroid_field_max = Asteroid_field.max_bound;
	out.asteroid_field_inner_min = Asteroid_field.inner_min_bound;
	out.asteroid_field_inner_max = Asteroid_field.inner_max_bound;
	out.asteroid_field_asteroid_types = Asteroid_field.field_asteroid_type;
	out.asteroid_field_targets = Asteroid_field.target_names;
	// Debris types are Asteroid_info indices; -1 marks one the parse already invalidated.
	for (int debris_type : Asteroid_field.field_debris_type) {
		if (debris_type >= 0) {
			out.asteroid_field_debris_types.push_back(asteroid_type_name(debris_type));
		}
	}

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
		state.show_polys = node.IsShowingPolys();

		int objnum = node.GetSCPObjectNumber();
		if (objnum >= 0 && objnum < MAX_OBJECTS) {
			state.has_pos = true;
			state.pos = Objects[objnum].pos;
		}

		out.jump_nodes.push_back(std::move(state));
	}

	for (int i = 0; i < MAX_SQUADRON_WINGS; i++) {
		out.squadron_wings.emplace_back(Squadron_wings[i] >= 0 ? Wings[Squadron_wings[i]].name : "");
	}

	// --- Effects and the rest of the world ---
	out.effects_present = true;

	out.gravity = The_mission.gravity;
	if (Storm != nullptr) {
		out.storm = Storm->name;
	}

	// Every poof type, on or off, so the restore can put the set back exactly.  The flag array
	// is only allocated once the poof table has been read.
	if (Neb2_poof_flags != nullptr) {
		for (size_t i = 0; i < Poof_info.size(); i++) {
			const poof_info& info = Poof_info[i];

			poof_state state;
			state.name = info.name;
			state.enabled = (get_bit(Neb2_poof_flags.get(), i) != 0);
			state.fade_start = info.fade_start.value();
			state.fade_duration = info.fade_duration;
			state.fade_in = info.fade_in;
			state.fade_multiplier = info.fade_multiplier;
			out.poofs.push_back(std::move(state));
		}
	}

	out.has_volumetrics = The_mission.volumetrics.has_value();
	if (out.has_volumetrics) {
		out.volumetrics_enabled = The_mission.volumetrics->get_enabled();
	}

	out.fog_near_distance = Neb2_fog_near_distance;
	out.fog_1000m_visibility = Neb2_fog_1000m_visibility;
	out.fog_skybox_clip_distance = Neb2_fog_skybox_clip_distance;
	out.fog_clip_distance = Neb2_fog_clip_distance;

	if (graphics::Post_processing_manager != nullptr) {
		for (const auto& effect : graphics::Post_processing_manager->getPostEffects()) {
			post_effect_state state;
			state.name = effect.name;
			state.intensity = effect.intensity;
			state.rgb = effect.rgb;
			out.post_effects.push_back(std::move(state));
		}

		const auto& lightshafts = graphics::Post_processing_manager->getLightshaftParams();
		out.lightshafts_on = lightshafts.on;
		out.lightshafts_intensity = lightshafts.intensity;
	}

	// The preset by name; -1 (the <none> case of set-sound-environment) is an empty name.
	if (Game_sound_env.id >= 0 && Game_sound_env.id < static_cast<int>(EFX_presets.size())) {
		out.sound_env_preset = EFX_presets[Game_sound_env.id].name;
	}
	out.sound_env_volume = Game_sound_env.volume;
	out.sound_env_damping = Game_sound_env.damping;
	out.sound_env_decay = Game_sound_env.decay;

	if (The_mission.ai_profile != nullptr) {
		out.beam_friendly_damage_cap = The_mission.ai_profile->beam_friendly_damage_cap[Game_skill_level];
		out.weapon_friendly_damage_cap = The_mission.ai_profile->weapon_friendly_damage_cap[Game_skill_level];
		out.weapon_self_damage_cap = The_mission.ai_profile->weapon_self_damage_cap[Game_skill_level];
	}

	// Only the entries a SEXP has moved off their table default; see damage_type_override_state.
	for (const auto& wip : Weapon_info) {
		if (wip.damage_type_idx != wip.damage_type_idx_sav) {
			out.weapon_damage_types.push_back({wip.name, damage_type_name(wip.damage_type_idx)});
		}
		if (wip.shockwave.damage_type_idx != wip.shockwave.damage_type_idx_sav) {
			out.weapon_shockwave_damage_types.push_back({wip.name, damage_type_name(wip.shockwave.damage_type_idx)});
		}
	}
	for (const auto& sip : Ship_info) {
		if (sip.shockwave.damage_type_idx != sip.shockwave.damage_type_idx_sav) {
			out.ship_shockwave_damage_types.push_back({sip.name, damage_type_name(sip.shockwave.damage_type_idx)});
		}
	}
	for (const auto& info : Asteroid_info) {
		if (info.damage_type_idx != info.damage_type_idx_sav) {
			out.asteroid_damage_types.push_back({info.name, damage_type_name(info.damage_type_idx)});
		}
	}

	for (int i = 0; i < NUM_SCORES; i++) {
		int index = Mission_music[i];
		bool valid = (index >= 0 && index < static_cast<int>(Spooled_music.size()));
		out.mission_music.emplace_back(valid ? Spooled_music[index].name : "");
	}

	for (const auto& cp : Coordinate_points) {
		if (cp.objnum < 0) {
			continue;
		}

		coordinate_point_state state;
		state.name = cp.name;
		state.group = cp.group;
		state.pos = Objects[cp.objnum].pos;
		state.escort_priority = cp.escort_priority;
		state.multi_team = cp.multi_team;
		state.visible = cp.flags[CoordinatePoint::Flags::Visible_in_mission];
		out.coordinate_points.push_back(std::move(state));
	}

	out.shudder_perpetual = Game_shudder_perpetual;
	out.shudder_everywhere = Game_shudder_everywhere;
	out.shudder_time = Game_shudder_time.value();
	out.shudder_total = Game_shudder_total;
	out.shudder_intensity = Game_shudder_intensity;

	out.photo_mode_allowed = game_get_photo_mode_allowed();

	out.toggle_debriefing = The_mission.flags[Mission::Mission_Flags::Toggle_debriefing];
	out.deactivate_autopilot = The_mission.flags[Mission::Mission_Flags::Deactivate_ap];
	out.use_autopilot_cinematics = The_mission.flags[Mission::Mission_Flags::Use_ap_cinematics];
}

// Split from apply_environment() because of when it can run: a nav bound to a ship resolves that
// ship by object, and the environment is applied before the ships are reconciled -- so any nav on
// a ship that arrived after time zero, or on a support ship, would find nothing and be dropped.
// This runs once every ship the checkpoint knows about exists.
// Props.  Every parsed prop gets a disposition, so one whose spawn cue had not fired stays
// pending and one that had vanished stays gone; every live prop gets its state; a live prop with
// no parsed_prop behind it (prop-create, mission.createProp) is marked as such so the apply
// builds it from scratch.
void store_props(checkpoint_data& data)
{
	auto capture_live = [](const prop& live, prop_state& state) {
		const object* objp = &Objects[live.objnum];

		state.prop_class = (live.prop_info_index >= 0 && live.prop_info_index < prop_info_size())
			? Prop_info[live.prop_info_index].name
			: SCP_string();
		state.pos = objp->pos;
		state.orient = objp->orient;
		state.vel = objp->phys_info.vel;
		state.rotvel = objp->phys_info.rotvel;
		state.alpha_mult = live.alpha_mult;
		collect_flags(live.flags, Prop_flag_table, state.flags);
		collect_flags(objp->flags, Object_flag_table, state.object_flags);
		state.glow_banks.assign(live.glow_point_bank_active.begin(), live.glow_point_bank_active.end());
		for (const auto& tr : live.replacement_textures) {
			if (!tr.from_table) {
				state.texture_old.emplace_back(tr.old_texture);
				state.texture_new.emplace_back(tr.new_texture);
			}
		}
		state.collision_group_id = objp->collision_group_id;
		state.despawn_delay = live.despawn_delay;
	};

	SCP_vector<bool> live_matched(Props.size(), false);

	for (const auto& parsed : Parse_props) {
		prop_state state;
		state.name = parsed.name;

		int id = prop_name_lookup(parsed.name);
		const prop* live = (id >= 0) ? prop_id_lookup(id) : nullptr;

		if (live != nullptr && live->objnum >= 0 && !Objects[live->objnum].flags[Object::Object_Flags::Should_be_dead]) {
			live_matched[id] = true;
			capture_live(*live, state);
		} else {
			state.disposition = parsed.spawned ? ShipDisposition::Vanished : ShipDisposition::NotYetHere;
		}
		data.props.push_back(std::move(state));
	}

	for (size_t i = 0; i < Props.size(); i++) {
		if (live_matched[i] || !Props[i].has_value()) {
			continue;
		}
		const prop& live = Props[i].value();
		if (live.objnum < 0 || Objects[live.objnum].type != OBJ_PROP ||
		    Objects[live.objnum].flags[Object::Object_Flags::Should_be_dead]) {
			continue;
		}

		prop_state state;
		state.name = live.prop_name;
		state.no_parse_prop = true;
		capture_live(live, state);
		data.props.push_back(std::move(state));
	}
}

void apply_props(const checkpoint_data& data)
{
	for (const auto& state : data.props) {
		int id = prop_name_lookup(state.name.c_str());
		prop* live = (id >= 0) ? prop_id_lookup(id) : nullptr;

		parsed_prop* parsed = nullptr;
		for (auto& candidate : Parse_props) {
			if (!stricmp(candidate.name, state.name.c_str())) {
				parsed = &candidate;
				break;
			}
		}

		if (state.disposition != ShipDisposition::Present) {
			// Not here at the checkpoint.  Take out what the fresh load made, and leave the
			// parsed prop pending or spent to match.
			if (live != nullptr && live->objnum >= 0) {
				Objects[live->objnum].flags.set(Object::Object_Flags::Should_be_dead);
			}
			if (parsed != nullptr) {
				parsed->spawned = (state.disposition == ShipDisposition::Vanished);
			}
			continue;
		}

		int prop_class = prop_info_lookup(state.prop_class.c_str());

		if (live == nullptr) {
			int objnum = -1;
			if (parsed != nullptr) {
				// Its spawn cue had fired; the same creation the cue would have run.
				objnum = create_prop_from_parsed(*parsed);
			} else if (prop_class >= 0) {
				objnum = prop_create(&state.orient, &state.pos, prop_class, state.name.c_str());
			}
			if (objnum < 0) {
				mprintf(("CHECKPOINT => Cannot recreate prop '%s' (class '%s'); dropping it.\n",
				         state.name.c_str(),
				         state.prop_class.c_str()));
				continue;
			}
			id = Objects[objnum].instance;
			live = prop_id_lookup(id);
			if (live == nullptr) {
				continue;
			}
		}

		if (prop_class >= 0 && prop_class != live->prop_info_index) {
			change_prop_type(id, prop_class);
			live = prop_id_lookup(id);
		}

		object* objp = &Objects[live->objnum];
		objp->pos = state.pos;
		objp->last_pos = state.pos;
		objp->orient = state.orient;
		objp->last_orient = state.orient;
		objp->phys_info.vel = state.vel;
		objp->phys_info.desired_vel = state.vel;
		objp->phys_info.rotvel = state.rotvel;
		objp->collision_group_id = state.collision_group_id;

		live->alpha_mult = state.alpha_mult;
		apply_flags(state.flags, Prop_flag_table, live->flags);
		{
			auto object_flags = objp->flags;
			apply_flags(state.object_flags, Object_flag_table, object_flags);
			obj_set_flags(objp, object_flags);
		}

		for (size_t i = 0; i < state.glow_banks.size() && i < live->glow_point_bank_active.size(); i++) {
			live->glow_point_bank_active[i] = state.glow_banks[i];
		}

		// Instance replacements on top of whatever the class and the parse already put there,
		// resolved the way the parse's own are.
		if (!state.texture_old.empty()) {
			for (size_t i = 0; i < state.texture_old.size() && i < state.texture_new.size(); i++) {
				texture_replace tr;
				memset(&tr, 0, sizeof(tr));
				strcpy_s(tr.ship_name, state.name.c_str());
				strcpy_s(tr.old_texture, state.texture_old[i].c_str());
				strcpy_s(tr.new_texture, state.texture_new[i].c_str());
				tr.new_texture_id = -1;
				tr.from_table = false;
				live->replacement_textures.push_back(tr);
			}
			prop_apply_replacement_textures(live);
		}

		live->despawn_delay = translate_stamp(state.despawn_delay);
	}

	// The props taken out above go now, not at the end of the frame, so nothing later in the
	// apply can find them by name.
	obj_delete_all_that_should_be_dead();
}

// Put the waypoint lists back the way the mission had left them: positions a script moved, and
// whole lists a script created, which the fresh parse does not have.  Before the ships, since the
// AI orders and nav points restored with them refer to lists by name.
void apply_waypoint_lists(const checkpoint_data& data)
{
	// waypoint_add() addresses a waypoint by instance, list index and index packed the way the
	// waypoint objects themselves carry it (waypoint.cpp, calc_waypoint_instance()).
	auto instance_of = [](int list_index, int wp_index) { return list_index * 0x10000 + wp_index; };

	for (const auto& state : data.waypoint_lists) {
		if (state.points.empty()) {
			continue;
		}

		int list_index = find_matching_waypoint_list_index(state.name.c_str());

		if (list_index < 0) {
			// A list the parse does not have.  waypoint_add() with no instance starts a new list
			// under a generated name, and creates the waypoint object as it goes; the name is
			// put right afterwards.
			waypoint_add(&state.points[0], -1, true);
			list_index = static_cast<int>(Waypoint_lists.size()) - 1;
			Waypoint_lists[list_index].set_name(state.name.c_str());

			for (size_t i = 1; i < state.points.size(); i++) {
				waypoint_add(&state.points[i], instance_of(list_index, static_cast<int>(i) - 1), false);
			}
			continue;
		}

		auto& waypoints = Waypoint_lists[list_index].get_waypoints();

		size_t common = MIN(waypoints.size(), state.points.size());
		for (size_t i = 0; i < common; i++) {
			waypoints[i].set_pos(&state.points[i]);
		}

		// Extra saved points were appended by a script; extra live points were removed by one.
		for (size_t i = common; i < state.points.size(); i++) {
			waypoint_add(&state.points[i], instance_of(list_index, static_cast<int>(i) - 1), false);
		}
		while (Waypoint_lists[list_index].get_waypoints().size() > state.points.size()) {
			waypoint_remove(&Waypoint_lists[list_index].get_waypoints().back());
		}
	}
}

void apply_navpoints(const checkpoint_data& data)
{
	const auto& env = data.environment;

	if (!env.present) {
		return;
	}

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

	// Subspace: the flag, the visual and the ambient sound, the same three things the
	// mission-set-subspace SEXP moves together.  The sound is not a handle that is carried across;
	// it is started and stopped here so it cannot be left out of step with the effect -- the
	// simulation frame starts it whenever the effect is on, but nothing but this would stop it
	// when a checkpoint turns the effect off.
	if (env.subspace && !Game_subspace_effect) {
		game_start_subspace_ambient_sound();
	} else if (!env.subspace && Game_subspace_effect) {
		game_stop_subspace_ambient_sound();
	}
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

	// The opening pattern has already been chosen by now -- event_music_first_pattern() ran when
	// the gameplay state was entered, before this apply -- and nothing has begun playing yet,
	// which is exactly the condition under which first_pattern() is willing to choose again.
	// With the battle flag set it chooses SONG_BTTL_1 outright.
	//
	// The flag is set directly rather than through event_music_battle_start(): that would queue
	// the battle song behind the opening one as a forced next pattern, which the re-pick then
	// never consumes and which would fire a spurious switch when the restored battle ends; and
	// it refuses unless hostiles are present, which at this point in the apply -- before the
	// ships are reconciled -- only holds for hostiles the mission file placed at time zero.  The
	// battle-over check that clears the flag runs off a timer the level init has already armed,
	// so a flag restored with no hostiles left simply clears itself after the usual interval.
	if (env.music_battle_started) {
		Event_Music_battle_started = 1;
		event_music_first_pattern();
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

	// Only the stages the player is still flying through; the store refuses anything later.
	if (env.supernova_stage == static_cast<int>(SUPERNOVA_STAGE::STARTED) ||
	    env.supernova_stage == static_cast<int>(SUPERNOVA_STAGE::CLOSE)) {
		supernova_restore(env.supernova_total, env.supernova_left);
	}

	// Through the same two calls the SEXPs make, so the ramp and the lock behave as they did.
	set_time_compression(env.time_compression);
	lock_time_compression(env.time_compression_locked);

	// The nav points are not here: a nav bound to a ship needs that ship's object, and this runs
	// before the ships are reconciled.  See apply_navpoints().

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
		} else if (node.IsColored()) {
			// The mission file coloured it and a SEXP put it back to the default since.  Passing
			// the default colour is how SetAlphaColor() clears the flag (jumpnode.cpp).
			node.SetAlphaColor(0, 255, 0, 255);
		}

		if (state.model.empty()) {
			node.ResetToDefaultModel();
		} else {
			node.SetModel(state.model.c_str(), state.show_polys);
		}

		int objnum = node.GetSCPObjectNumber();
		if (state.has_pos && objnum >= 0 && objnum < MAX_OBJECTS) {
			Objects[objnum].pos = state.pos;
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

	// --- Effects and the rest of the world ---
	// A checkpoint from before these were captured says nothing about them.
	if (!env.effects_present) {
		return;
	}

	// Gravity.  Whether a weapon is subject to it is a per-weapon flag worked out from whether
	// there is any, so crossing zero in either direction has to redo that, as the SEXP does.
	{
		bool had_gravity = !IS_VEC_NULL(&The_mission.gravity);
		The_mission.gravity = env.gravity;
		if (had_gravity != !IS_VEC_NULL(&The_mission.gravity)) {
			collide_apply_gravity_flags_weapons();
		}
	}

	// A name this build does not know leaves no storm, which is also what the empty name means.
	nebl_set_storm(env.storm.c_str());

	// The poof set and any fade in progress.  The toggles only flip bits; finalize() is what
	// rebuilds the poofs from them, and it is called once for the lot, as the SEXP does.
	if (!env.poofs.empty() && Neb2_poof_flags != nullptr) {
		for (const auto& state : env.poofs) {
			for (size_t i = 0; i < Poof_info.size(); i++) {
				if (stricmp(Poof_info[i].name, state.name.c_str()) != 0) {
					continue;
				}

				neb2_toggle_poof(static_cast<int>(i), state.enabled);
				Poof_info[i].fade_start = TIMESTAMP(translate_stamp(state.fade_start));
				Poof_info[i].fade_duration = state.fade_duration;
				Poof_info[i].fade_in = state.fade_in;
				Poof_info[i].fade_multiplier = state.fade_multiplier;
				break;
			}
		}
		neb2_toggle_poof_finalize();
	}

	if (env.has_volumetrics && The_mission.volumetrics) {
		The_mission.volumetrics->set_enabled(env.volumetrics_enabled);
	}

	Neb2_fog_near_distance = env.fog_near_distance;
	Neb2_fog_1000m_visibility = env.fog_1000m_visibility;
	Neb2_fog_skybox_clip_distance = env.fog_skybox_clip_distance;
	Neb2_fog_clip_distance = env.fog_clip_distance;

	// Post-processing.  gr_post_process_set_effect() takes the 0-100 value the SEXP takes and
	// scales it into the effect's own range (value / div + add), so the stored intensity is
	// unscaled first.  A zero colour means "keep", which is what an effect with no colour has.
	if (graphics::Post_processing_manager != nullptr) {
		for (const auto& state : env.post_effects) {
			for (const auto& effect : graphics::Post_processing_manager->getPostEffects()) {
				if (stricmp(effect.name.c_str(), state.name.c_str()) != 0) {
					continue;
				}

				int value = fl2ir((state.intensity - effect.add) * effect.div);
				vec3d rgb = state.rgb;
				gr_post_process_set_effect(state.name.c_str(), value, &rgb);
				break;
			}
		}

		// Lightshafts are not in the effect list; the setter special-cases the name.
		int lightshafts = env.lightshafts_on ? fl2ir(env.lightshafts_intensity * 100.0f) : 0;
		gr_post_process_set_effect("lightshafts", lightshafts, nullptr);
	}

	// The sound environment, through the same calls the SEXPs make.  Game_sound_env is what the
	// engine re-applies whenever the gameplay state is entered, so it is kept in step.
	if (env.sound_env_preset.empty()) {
		sound_env_disable();
		Game_sound_env.id = -1;
	} else {
		int preset = ds_eax_get_preset_id(env.sound_env_preset.c_str());
		if (preset >= 0) {
			Game_sound_env.id = preset;
			Game_sound_env.volume = env.sound_env_volume;
			Game_sound_env.damping = env.sound_env_damping;
			Game_sound_env.decay = env.sound_env_decay;
			sound_env_set(&Game_sound_env);
		}
	}

	if (The_mission.ai_profile != nullptr) {
		The_mission.ai_profile->beam_friendly_damage_cap[Game_skill_level] = env.beam_friendly_damage_cap;
		The_mission.ai_profile->weapon_friendly_damage_cap[Game_skill_level] = env.weapon_friendly_damage_cap;
		The_mission.ai_profile->weapon_self_damage_cap[Game_skill_level] = env.weapon_self_damage_cap;
	}

	// Damage-type overrides live on the tables, so one that a previous run set is still there in
	// a fresh session.  Everything goes back to its table default first, then the overrides the
	// checkpoint recorded go on -- which is the complete set, since the store keeps every entry
	// that differs from its default.
	for (auto& wip : Weapon_info) {
		wip.damage_type_idx = wip.damage_type_idx_sav;
		wip.shockwave.damage_type_idx = wip.shockwave.damage_type_idx_sav;
	}
	for (auto& sip : Ship_info) {
		sip.shockwave.damage_type_idx = sip.shockwave.damage_type_idx_sav;
	}
	for (auto& info : Asteroid_info) {
		info.damage_type_idx = info.damage_type_idx_sav;
	}
	for (const auto& entry : env.weapon_damage_types) {
		int weapon_class = lookup_weapon_class(entry.subject);
		if (weapon_class >= 0) {
			Weapon_info[weapon_class].damage_type_idx = lookup_damage_type(entry.damage_type);
		}
	}
	for (const auto& entry : env.weapon_shockwave_damage_types) {
		int weapon_class = lookup_weapon_class(entry.subject);
		if (weapon_class >= 0) {
			Weapon_info[weapon_class].shockwave.damage_type_idx = lookup_damage_type(entry.damage_type);
		}
	}
	for (const auto& entry : env.ship_shockwave_damage_types) {
		int ship_class = lookup_ship_class(entry.subject);
		if (ship_class >= 0) {
			Ship_info[ship_class].shockwave.damage_type_idx = lookup_damage_type(entry.damage_type);
		}
	}
	for (const auto& entry : env.asteroid_damage_types) {
		int asteroid_type = lookup_asteroid_type(entry.subject);
		if (asteroid_type >= 0) {
			Asteroid_info[asteroid_type].damage_type_idx = lookup_damage_type(entry.damage_type);
		}
	}

	// The scores, through the same setter the parse and the script API use; an empty or unknown
	// name gives -1, which is what an empty name stored.
	for (int i = 0; i < NUM_SCORES && i < static_cast<int>(env.mission_music.size()); i++) {
		event_music_set_score(i, env.mission_music[i].c_str());
	}

	// Coordinate points: the set first, then the fields.  A point the checkpoint does not have was
	// deleted by a script; obj_delete() takes it out of Coordinate_points too.  One the fresh load
	// does not have was created by a script, and is created again under its saved name.
	{
		SCP_vector<int> stale;
		for (const auto& cp : Coordinate_points) {
			if (cp.objnum < 0) {
				continue;
			}
			bool kept = std::any_of(env.coordinate_points.begin(), env.coordinate_points.end(),
				[&cp](const coordinate_point_state& state) { return !stricmp(state.name.c_str(), cp.name.c_str()); });
			if (!kept) {
				stale.push_back(cp.objnum);
			}
		}
		for (int objnum : stale) {
			obj_delete(objnum);
		}

		for (const auto& state : env.coordinate_points) {
			auto cp = find_coordinate_point_by_name(state.name.c_str());
			if (cp == nullptr || cp->objnum < 0) {
				int objnum = coordinate_point_create(&state.pos, state.name.c_str());
				if (objnum < 0) {
					continue;
				}
				cp = find_coordinate_point_by_objnum(objnum);
				if (cp == nullptr) {
					continue;
				}
			}

			cp->group = state.group;
			cp->escort_priority = state.escort_priority;
			cp->multi_team = state.multi_team;
			cp->flags.set(CoordinatePoint::Flags::Visible_in_mission, state.visible);
			Objects[cp->objnum].pos = state.pos;
		}
	}

	// The shudder, exactly as game_shudder_apply() left it.  The level init has already cleared
	// it, so a checkpoint with none in progress restores none.
	Game_shudder_perpetual = env.shudder_perpetual;
	Game_shudder_everywhere = env.shudder_everywhere;
	Game_shudder_time = TIMESTAMP(translate_stamp(env.shudder_time));
	Game_shudder_total = env.shudder_total;
	Game_shudder_intensity = env.shudder_intensity;

	game_set_photo_mode_allowed(env.photo_mode_allowed);

	The_mission.flags.set(Mission::Mission_Flags::Toggle_debriefing, env.toggle_debriefing);
	The_mission.flags.set(Mission::Mission_Flags::Deactivate_ap, env.deactivate_autopilot);
	The_mission.flags.set(Mission::Mission_Flags::Use_ap_cinematics, env.use_autopilot_cinematics);
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

		state.rotational_velocity = objp->phys_info.rotvel;
		state.mine_chase_expires = wp->mine_chase_expires.value();
		state.mine_chase_cooldown_expires = wp->mine_chase_cooldown_expires.value();
		for (const auto& stamp : wp->last_spawn_time) {
			state.last_spawn_times.push_back(stamp.value());
		}
		state.big_attack_point_stamp = wp->pick_big_attack_point_timestamp;
		state.big_attack_point = wp->big_attack_point;
		state.collision_group_id = objp->collision_group_id;

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
	const auto& env = data.environment;
	bool field_known = env.present && env.has_asteroid_field;

	// The field itself first.  This is the definition set-asteroid-field, set-debris-field and
	// config-field-targets rewrite, and it decides where rocks wrap, what they are thrown at and
	// which models are loaded -- a field the mission file never declared has none paged in, and
	// asteroid_create() would quietly refuse every saved rock.  So the models are paged in here
	// the same way asteroid_create_all() does it, and then only the saved rocks are created.
	if (field_known) {
		Asteroid_field.num_initial_asteroids = env.asteroid_field_num_initial;
		Asteroid_field.field_type = env.asteroid_field_active ? FT_ACTIVE : FT_PASSIVE;
		Asteroid_field.debris_genre = env.asteroid_field_is_debris ? DG_DEBRIS : DG_ASTEROID;
		Asteroid_field.enhanced_visibility_checks = env.asteroid_field_enhanced_checks;
		Asteroid_field.has_inner_bound = env.asteroid_field_has_inner_bound;
		Asteroid_field.speed = env.asteroid_field_speed;
		Asteroid_field.bound_rad = env.asteroid_field_bound_rad;
		Asteroid_field.vel = env.asteroid_field_vel;
		Asteroid_field.min_bound = env.asteroid_field_min;
		Asteroid_field.max_bound = env.asteroid_field_max;
		Asteroid_field.inner_min_bound = env.asteroid_field_inner_min;
		Asteroid_field.inner_max_bound = env.asteroid_field_inner_max;
		Asteroid_field.field_asteroid_type = env.asteroid_field_asteroid_types;
		Asteroid_field.target_names = env.asteroid_field_targets;

		Asteroid_field.field_debris_type.clear();
		for (const auto& name : env.asteroid_field_debris_types) {
			int debris_type = lookup_asteroid_type(name);
			if (debris_type >= 0) {
				Asteroid_field.field_debris_type.push_back(debris_type);
			}
		}

		if (Asteroid_field.debris_genre == DG_DEBRIS) {
			for (int debris_type : Asteroid_field.field_debris_type) {
				asteroid_load(debris_type, 0);
			}
		} else {
			for (const auto& subtype : Asteroid_field.field_asteroid_type) {
				asteroid_load(ASTEROID_TYPE_SMALL, get_asteroid_subtype_index_by_name(subtype, ASTEROID_TYPE_SMALL));
				asteroid_load(ASTEROID_TYPE_MEDIUM, get_asteroid_subtype_index_by_name(subtype, ASTEROID_TYPE_MEDIUM));
				asteroid_load(ASTEROID_TYPE_LARGE, get_asteroid_subtype_index_by_name(subtype, ASTEROID_TYPE_LARGE));
			}
		}
	}

	// A checkpoint from before the field was captured says nothing about it, and if it captured
	// no rocks either then the mission either has no field or the file predates rock capture.
	// Either way, leave the freshly created field alone rather than wiping it.  Once the field is
	// known, an empty rock list is real -- the player cleared it -- and the fresh rocks go.
	if (!field_known && data.asteroids.empty()) {
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

	// The throw targets last, once the rocks exist: the live target list was built from the
	// mission file's names as those ships were created, so it is rebuilt from the names just
	// restored (a target config-field-targets removed stays removed), and asteroid_add_target()
	// counts the rocks already heading for each target as it adds it -- which only comes out
	// right if the restored rocks are already there to be counted.
	if (field_known) {
		asteroid_clear_targets();
		for (const auto& name : Asteroid_field.target_names) {
			int objnum = objnum_for_ship_name(name);
			if (objnum >= 0) {
				asteroid_add_target(&Objects[objnum]);
			}
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
		collect_flags(entry.second.instance_flags, Animation_instance_flag_table, state.instance_flags);

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
		apply_flags(state.instance_flags, Animation_instance_flag_table, data.instance_flags);

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

// Does the checkpoint have this exact link -- these two ships, at these two bays?
//
// Links are stored from both ends, so looking under either ship finds it; the docker's entry is
// checked first and the dockee's second only because a ship the checkpoint never mentions has no
// entry to look under at all.
bool checkpoint_has_dock_link(const checkpoint_data& data,
	const SCP_string& docker,
	const SCP_string& dockee,
	const SCP_string& docker_point,
	const SCP_string& dockee_point)
{
	for (const auto& ship_data : data.ships) {
		bool forward = lcase_equal(ship_data.name, docker);
		if (!forward && !lcase_equal(ship_data.name, dockee)) {
			continue;
		}

		// Read from this ship's side: "my" is whichever end we are standing on.
		const SCP_string& other = forward ? dockee : docker;
		const SCP_string& mine = forward ? docker_point : dockee_point;
		const SCP_string& theirs = forward ? dockee_point : docker_point;

		for (const auto& link : ship_data.docks) {
			if (lcase_equal(link.other_ship, other) && lcase_equal(link.my_point, mine) &&
				lcase_equal(link.their_point, theirs)) {
				return true;
			}
		}
	}

	return false;
}

// Break every live link the checkpoint does not have, before any of them are rebuilt.
//
// The mission load has just recreated the docking the mission file describes, and the checkpoint
// is the authority on what was still docked -- so a freighter that had released its cargo, or a
// support ship that had finished and pulled away, otherwise comes back still attached.  Restoring
// only adds links, so without this pass there is nothing that can ever take one away.
//
// Same ships at different bays counts as stale too: undocking here lets restore_docks() rebuild
// the link properly rather than leaving the mission file's arrangement in place.
//
// The links to break are collected before any of them is broken, because undocking rewrites the
// lists being walked.
void undock_stale_links(const checkpoint_data& data)
{
	SCP_vector<std::pair<object*, object*>> to_undock;

	for (const auto& entry : Ship_registry) {
		if (!entry.has_shipp() || !entry.has_objp()) {
			continue;
		}

		ship* shipp = &Ships[entry.shipnum];
		object* objp = &Objects[entry.objnum];

		for (dock_instance* dock_ptr = objp->dock_list; dock_ptr != nullptr; dock_ptr = dock_ptr->next) {
			object* other = dock_ptr->docked_objp;
			if (other == nullptr || other->type != OBJ_SHIP || other->instance < 0) {
				continue;
			}

			ship* other_shipp = &Ships[other->instance];

			// Once per pair: the dock lists are symmetric, so only the end whose name sorts first
			// gets to decide.
			if (stricmp(shipp->ship_name, other_shipp->ship_name) >= 0) {
				continue;
			}

			SCP_string my_point = dock_point_name(shipp, dock_ptr->dockpoint_used);
			SCP_string their_point =
				dock_point_name(other_shipp, dock_find_dockpoint_used_by_object(other, objp));

			if (!checkpoint_has_dock_link(data, shipp->ship_name, other_shipp->ship_name, my_point, their_point)) {
				to_undock.emplace_back(objp, other);
			}
		}
	}

	for (const auto& pair : to_undock) {
		ai_do_objects_undocked_stuff(pair.first, pair.second);
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
	store_ai_override(aip->ai_override_ci, out.override_floats);

	// ai_class is an index into Ai_classes, which comes from ai.tbl, so it goes by name like
	// every other table index.
	out.ai_class = ai_class_name(aip->ai_class);

	out.target_ship = ship_name_for_objnum(aip->target_objnum);
	out.previous_target_ship = ship_name_for_objnum(aip->previous_target_objnum);
	out.goal_ship = ship_name_for_objnum(aip->goal_objnum);
	out.guard_ship = ship_name_for_objnum(aip->guard_objnum);
	out.guard_wing = wing_name_for_wingnum(aip->guard_wingnum);
	out.support_ship = ship_name_for_objnum(aip->support_ship_objnum);
	out.hitter_ship = ship_name_for_objnum(aip->hitter_objnum);
	out.attacker_ship = ship_name_for_objnum(aip->attacker_objnum);
	out.artillery_ship = ship_name_for_objnum(aip->artillery_objnum);
	out.waypoint_list = waypoint_list_name(aip->wp_list_index);

	// Fixed length, so it goes out whole; an empty entry is a slot with nothing in it.
	for (int i = 0; i < MAX_IGNORE_NEW_OBJECTS; i++) {
		out.ignore_new.push_back(ship_name_for_objnum(aip->ignore_new_objnums[i]));
	}

	// ignore_objnum doubles as a wing reference: -(wingnum + 1) means "ignore this whole wing".
	if (aip->ignore_objnum < -1) {
		out.ignore_wing = wing_name_for_wingnum(-(aip->ignore_objnum + 1));
	} else {
		out.ignore_ship = ship_name_for_objnum(aip->ignore_objnum);
	}

	// The targeted subsystem is a pointer into its owner's subsystem list, so it is stored the
	// same way subsystems are stored everywhere else: by name plus ordinal within that name.  The
	// owner is targeted_subsys_parent, which is its own objnum and need not be the current target.
	if (aip->targeted_subsys != nullptr) {
		SCP_string parent = ship_name_for_objnum(aip->targeted_subsys_parent);
		if (!parent.empty()) {
			auto parent_entry = ship_registry_get(parent);
			if (parent_entry != nullptr && parent_entry->has_shipp()) {
				out.target_subsystem = subsys_key_for(parent_entry->shipp(), aip->targeted_subsys);
				if (!out.target_subsystem.empty()) {
					out.target_subsystem_ship = parent;
				}
			}
		}
	}

	// last_subsys_target carries no parent of its own, so it is looked for on the current target,
	// which is where the AI put it.
	if (aip->last_subsys_target != nullptr && !out.target_ship.empty()) {
		auto target_entry = ship_registry_get(out.target_ship);
		if (target_entry != nullptr && target_entry->has_shipp()) {
			out.last_subsys_target = subsys_key_for(target_entry->shipp(), aip->last_subsys_target);
			if (!out.last_subsys_target.empty()) {
				out.last_subsys_target_ship = out.target_ship;
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

// The highest goal signature any restored goal carried.  Ai_goal_signature restarts at zero on
// every level load, and ai_process_mission_orders() tells "the active goal changed" from "it did
// not" purely by comparing signatures, so a new goal handed a number a restored goal already holds
// can be mistaken for it.  The apply pushes the counter past this once every goal is back.
int Max_restored_goal_signature = -1;

void load_ai_goal(const ai_goal_state& in, ai_goal& goal)
{
	ai_goal_reset(&goal);

	// Lua AI orders are not captured: Ai_goal_names has no entry for AI_GOAL_LUA, so they are
	// written with an empty mode and land here, which is what we want.  Their target and
	// arguments are live Lua values (lua_ai_target) and their submode is a dynamic SEXP operator
	// id that need not mean the same thing in this run.  See the AIM_LUA note in load_ai().
	ai_goal_mode mode;
	if (in.mode.empty()) {
		return;
	}
	if (!ai_goal_mode_value(in.mode, mode)) {
		mprintf(("CHECKPOINT => AI goal '%s' no longer exists; dropping it.\n", in.mode.c_str()));
		return;
	}

	// A chase-weapon goal is keyed on a weapon slot (target_instance), and a weapon in flight has
	// no name to find it by again: the projectile restore recreates it in whatever slot is free.
	// Restoring the goal without its slot would leave target_instance at -1, which the goal
	// validation asserts on and then indexes Weapons[] with.  The AI picks a new bomb to chase
	// within a frame, so dropping the goal costs nothing.
	if (mode == AI_GOAL_CHASE_WEAPON) {
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

	if (in.signature > Max_restored_goal_signature) {
		Max_restored_goal_signature = in.signature;
	}

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
	// is what tells the AI to read them as names.  That only holds for AI_GOAL_DOCK, which the
	// engine re-resolves lazily; a rearm goal's dispatch reads the indices straight out of the
	// union, so those are resolved in resolve_ai_goal_dockpoints() once every ship is in its
	// final state.
	goal.flags.remove(AI::Goal_Flags::Docker_index_valid);
	goal.flags.remove(AI::Goal_Flags::Dockee_index_valid);
	if (!in.docker_point.empty()) {
		goal.docker.name = ai_add_dock_name(in.docker_point.c_str());
	}
	if (!in.dockee_point.empty()) {
		goal.dockee.name = ai_add_dock_name(in.dockee_point.c_str());
	}
}

// Turn the dock point names a restored goal carries back into indices, for the goal modes whose
// execution reads the indices without checking the flags first.  ai_rearm_repair() is handed
// docker.index and dockee.index directly (aigoals.cpp, AI_GOAL_REARM_REPAIR dispatch), and both
// ai_get_dock_goal_indexes() and ai_path_0() assert the index-valid flags, so a rearm goal left
// holding names would be executed with the low bits of a pointer as a bay number.
//
// Second pass only: it needs the model of the ship holding the goal and of the target, and the
// first pass may not have changed either ship's class yet.  A goal whose dock points no longer
// resolve is dropped, which is what happens to a rearm goal whose support ship has gone.
void resolve_ai_goal_dockpoints(const ship* shipp, ai_goal& goal)
{
	if (goal.ai_mode != AI_GOAL_REARM_REPAIR) {
		return;
	}
	if (goal.flags[AI::Goal_Flags::Docker_index_valid] && goal.flags[AI::Goal_Flags::Dockee_index_valid]) {
		return;
	}

	int target_shipnum = (goal.target_name != nullptr) ? ship_name_lookup(goal.target_name) : -1;
	int docker_model = Ship_info[shipp->ship_info_index].model_num;
	int dockee_model = (target_shipnum >= 0) ? Ship_info[Ships[target_shipnum].ship_info_index].model_num : -1;

	int docker_index = -1;
	int dockee_index = -1;
	if (docker_model >= 0 && goal.docker.name != nullptr) {
		docker_index = model_find_dock_name_index(docker_model, goal.docker.name);
	}
	if (dockee_model >= 0 && goal.dockee.name != nullptr) {
		dockee_index = model_find_dock_name_index(dockee_model, goal.dockee.name);
	}

	if (docker_index < 0 || dockee_index < 0) {
		mprintf(("CHECKPOINT => Rearm goal on '%s' has no usable dock points any more; dropping it.\n",
		         shipp->ship_name));
		ai_goal_reset(&goal);
		return;
	}

	goal.docker.index = docker_index;
	goal.dockee.index = dockee_index;
	goal.flags.set(AI::Goal_Flags::Docker_index_valid);
	goal.flags.set(AI::Goal_Flags::Dockee_index_valid);
}

void load_ai(ship* shipp, const ai_state& in)
{
	if (!in.present || shipp->ai_index < 0 || shipp->ai_index >= MAX_AI_INFO) {
		return;
	}

	ai_info* aip = &Ai_info[shipp->ai_index];

	apply_flags(in.flags, Ai_flag_table, aip->ai_flags);
	apply_flags(in.override_flags, Ai_override_flag_table, aip->ai_override_flags);

	// The class before the scalars: ship_set_new_ai_class() is what turns a class into the
	// accuracy, evasion, courage and the rest that the AI actually flies by, and it also writes
	// the defaults for a few values (ai_aburn_use_factor) that the scalars then overwrite with
	// what the mission had set.  Writing only ai_class would leave the ship reporting one class
	// and flying another.
	if (!in.ai_class.empty()) {
		int ai_class = lookup_ai_class(in.ai_class);
		if (ai_class >= 0 && ai_class != aip->ai_class) {
			ship_set_new_ai_class(shipp, ai_class);
		}
	}

	load_ai_scalars(*aip, in);
	load_ai_override(aip->ai_override_ci, in.override_floats);

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

	// A waypoint mode with no list to fly dereferences null on the first frame.  The list is
	// gone -- renamed, or created by a script and not in this parse -- so the ship idles instead;
	// its goals are restored and will re-issue the order if one still names a list that exists.
	if (aip->mode == AIM_WAYPOINTS && aip->wp_list_index < 0) {
		mprintf(("CHECKPOINT => '%s' was flying waypoint list '%s', which no longer exists; idling it.\n",
		         shipp->ship_name,
		         in.waypoint_list.c_str()));
		aip->mode = AIM_NONE;
	}

	// The path_* fields are indices into a global path array that is rebuilt from the models
	// on every load, so they are deliberately not captured (checkpointfields.h) and every ship
	// comes back with no path.  Most modes recover: ai_path() builds one when path_start is -1,
	// and bay emerge/depart fall back to AIM_NONE.  Two docking legs do not -- the approach
	// (DOCK_2/3) and the first undock legs (UNDOCK_1/2) index Path_points[] straight off
	// path_cur, which is -1.  Put those back one leg, to the submode that builds the path.
	if (aip->mode == AIM_DOCK) {
		if (aip->submode == AIS_DOCK_2 || aip->submode == AIS_DOCK_3) {
			aip->submode = AIS_DOCK_1;
		} else if (aip->submode == AIS_UNDOCK_1 || aip->submode == AIS_UNDOCK_2) {
			aip->submode = AIS_UNDOCK_0;
		}
	}

	// A Lua AI mode keeps its target and arguments in lua_ai_target, which holds live Lua values
	// and is not captured, and its submode is the id of a dynamic SEXP operator, which is handed
	// out at script registration and so need not name the same mode (or any) in this run.  Its
	// goal is dropped for the same reasons (load_ai_goal()), so idle the ship: ai_lua() would
	// otherwise run the action every frame with nothing to act on, or throw on an unknown id.
	if (aip->mode == AIM_LUA) {
		mprintf(("CHECKPOINT => '%s' was running a Lua AI order, which is not restored; idling it.\n", shipp->ship_name));
		aip->mode = AIM_NONE;
		aip->submode = 0;
	}
}

// The ship-level references to other ships, resolved once every ship in the mission exists:
// guard-range clamps, the Knossos a ship departs through, and who has damaged it.
void resolve_ship_references(ship* shipp, const ship_state& state)
{
	for (const auto& guard : state.guard_ranges) {
		int target = ship_name_lookup(guard.ship.c_str());
		if (target >= 0) {
			set_guard_range_ship(guard.range, target, shipp);
		}
	}

	shipp->special_warpout_objnum = objnum_for_ship_name(state.special_warpout_ship);

	for (int i = 0; i < MAX_DAMAGE_SLOTS; i++) {
		shipp->damage_ship_id[i] = 0;
		shipp->damage_ship[i] = 0.0f;
	}
	int slot = 0;
	for (const auto& credit : state.damage_credits) {
		if (slot >= MAX_DAMAGE_SLOTS) {
			break;
		}
		int objnum = objnum_for_ship_name(credit.ship);
		if (objnum < 0) {
			continue;
		}
		shipp->damage_ship_id[slot] = Objects[objnum].signature;
		shipp->damage_ship[slot] = credit.damage;
		slot++;
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
	aip->previous_target_objnum = objnum_for_ship_name(in.previous_target_ship);
	aip->attacker_objnum = objnum_for_ship_name(in.attacker_ship);
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

	// Fixed-length array, restored by slot; a name that no longer resolves leaves its slot empty.
	for (int i = 0; i < MAX_IGNORE_NEW_OBJECTS; i++) {
		int objnum = (i < static_cast<int>(in.ignore_new.size())) ? objnum_for_ship_name(in.ignore_new[i]) : -1;
		aip->ignore_new_objnums[i] = (objnum >= 0) ? objnum : UNUSED_OBJNUM;
		aip->ignore_new_signatures[i] = (objnum >= 0) ? Objects[objnum].signature : -1;
	}

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

	aip->targeted_subsys = find_subsys_by_key(in.target_subsystem_ship, in.target_subsystem);
	aip->targeted_subsys_parent = (aip->targeted_subsys != nullptr)
		? objnum_for_ship_name(in.target_subsystem_ship)
		: -1;

	aip->last_subsys_target = find_subsys_by_key(in.last_subsys_target_ship, in.last_subsys_target);

	for (int i = 0; i < MAX_AI_GOALS; i++) {
		resolve_ai_goal_dockpoints(shipp, aip->goals[i]);
	}

	// A goal the loader dropped (an unknown mode, a chase-weapon goal, a rearm goal with no dock
	// points) leaves its slot empty; an active_goal that pointed there has nothing to point at.
	if (aip->active_goal >= 0 && aip->active_goal < MAX_AI_GOALS &&
	    aip->goals[aip->active_goal].ai_mode == AI_GOAL_NONE) {
		aip->active_goal = -1;
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

		state.armor_type = armor_type_name(subsys->armor_type_idx);
		state.targeting_order.assign(subsys->turret_targeting_order, subsys->turret_targeting_order + NUM_TURRET_ORDER_TYPES);
		for (int i = 0; i < subsys->num_target_priorities && i < 32; i++) {
			int priority = subsys->target_priority[i];
			if (priority >= 0 && priority < static_cast<int>(Ai_tp_list.size())) {
				state.target_priorities.emplace_back(Ai_tp_list[priority].name);
			}
		}
		state.scripting_target_override = subsys->scripting_target_override;

		// The forced subsystem target lives on the turret's target ship; it is only meaningful
		// while that target is a ship, which is also the only case turret_target names.
		if (subsys->flags[Ship::Subsystem_Flags::Forced_subsys_target] && subsys->targeted_subsys != nullptr &&
		    subsys->turret_enemy_objnum >= 0 && subsys->turret_enemy_objnum < MAX_OBJECTS) {
			const object* target = &Objects[subsys->turret_enemy_objnum];
			if (target->type == OBJ_SHIP && target->instance >= 0) {
				state.forced_target_subsys = subsys_key_for(&Ships[target->instance], subsys->targeted_subsys);
			}
		}

		if (subsys->submodel_instance_1 != nullptr) {
			const submodel_instance* smi = subsys->submodel_instance_1;
			state.has_rotation = true;
			state.cur_angle = smi->cur_angle;
			state.cur_offset = smi->cur_offset;
			state.current_turn_rate = smi->current_turn_rate;
			state.desired_turn_rate = smi->desired_turn_rate;
			state.turn_accel = smi->turn_accel;
			state.current_shift_rate = smi->current_shift_rate;
			state.desired_shift_rate = smi->desired_shift_rate;
			state.canonical_orient = smi->canonical_orient;
			state.canonical_offset = smi->canonical_offset;
		}
		if (subsys->submodel_instance_2 != nullptr && subsys->submodel_instance_2 != subsys->submodel_instance_1) {
			state.has_gun_orient = true;
			state.gun_canonical_orient = subsys->submodel_instance_2->canonical_orient;
		}

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
			state.ai_class = ai_class_name(subsys->weapons.ai_class);
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

		subsys->armor_type_idx = lookup_armor_type(state.armor_type);
		for (size_t i = 0; i < state.targeting_order.size() && i < NUM_TURRET_ORDER_TYPES; i++) {
			subsys->turret_targeting_order[i] = state.targeting_order[i];
		}
		if (!state.target_priorities.empty()) {
			subsys->num_target_priorities = 0;
			for (const auto& name : state.target_priorities) {
				if (subsys->num_target_priorities >= 32) {
					break;
				}
				for (int i = 0; i < static_cast<int>(Ai_tp_list.size()); i++) {
					if (!stricmp(Ai_tp_list[i].name, name.c_str())) {
						subsys->target_priority[subsys->num_target_priorities++] = i;
						break;
					}
				}
			}
		}
		subsys->scripting_target_override = state.scripting_target_override;

		if (state.has_rotation && subsys->submodel_instance_1 != nullptr) {
			submodel_instance* smi = subsys->submodel_instance_1;
			smi->cur_angle = state.cur_angle;
			smi->cur_offset = state.cur_offset;
			smi->current_turn_rate = state.current_turn_rate;
			smi->desired_turn_rate = state.desired_turn_rate;
			smi->turn_accel = state.turn_accel;
			smi->current_shift_rate = state.current_shift_rate;
			smi->desired_shift_rate = state.desired_shift_rate;
			smi->canonical_orient = state.canonical_orient;
			smi->canonical_offset = state.canonical_offset;
		}
		if (state.has_gun_orient && subsys->submodel_instance_2 != nullptr) {
			subsys->submodel_instance_2->canonical_orient = state.gun_canonical_orient;
		}

		// Never leave a subsystem above its (possibly changed) maximum.
		if (subsys->current_hits > subsys->max_hits) {
			subsys->current_hits = subsys->max_hits;
		}

		// A destroyed subsystem is more than zero hits.  do_subobj_destroyed_stuff() also marks
		// the submodel (and a turret's barrel) blown off, which is what stops it rendering and
		// colliding, and links any subsystem that has no submodel of its own.  That function is
		// not called here because it also logs, prints to the HUD, spawns the explosion and fires
		// the On Subsystem Destroyed hook, all of which already happened in the run that was
		// saved; this is just the lasting part of it.
		if (subsys->current_hits <= 0.0f && subsys->max_hits > 0.0f &&
		    !subsys->flags[Ship::Subsystem_Flags::No_disappear] && subsys->system_info != nullptr) {
			const model_subsystem* psub = subsys->system_info;
			if (psub->subobj_num > -1 && subsys->submodel_instance_1 != nullptr) {
				subsys->submodel_instance_1->blown_off = true;
			}
			if (psub->subobj_num != psub->turret_gun_sobj && psub->turret_gun_sobj >= 0 &&
			    subsys->submodel_instance_2 != nullptr) {
				subsys->submodel_instance_2->blown_off = true;
			}
			check_subsystem_submodel_link(shipp, subsys, true);
		}

		if (state.has_weapons) {
			load_weapons(subsys->weapons, state.weapons, true);

			if (!state.ai_class.empty()) {
				int ai_class = lookup_ai_class(state.ai_class);
				if (ai_class >= 0) {
					ship_subsystem_set_new_ai_class(subsys, ai_class);
				}
			}
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

		// A forced subsystem target is on that ship.  The turret code treats a null pointer with
		// the flag set as "clear the flag", so leaving this unresolved would silently drop it.
		if (!state.forced_target_subsys.empty()) {
			it->second->targeted_subsys = find_subsys_by_key(state.turret_target, state.forced_target_subsys);
		}
	}
}

// ------------------------------------------------------------------
// Pending load state
// ------------------------------------------------------------------

struct pending_load_state {
	bool queued = false;      // a SEXP asked for a load; act on it at end of frame
	bool in_progress = false; // the mission restart has been posted; apply on the way back in
	bool awaiting_reload = false; // the restart is posted but the level has not been rebuilt yet
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

// Slots name files, and the filename folds case (see checkpoint_filename()), so "Alpha" and
// "alpha" are the same checkpoint and must be the same cache entry.
SCP_string existence_cache_key(const SCP_string& slot)
{
	SCP_string key = slot;
	SCP_tolower(key);
	return key;
}

void invalidate_existence_cache(const SCP_string& slot)
{
	Existence_cache.erase(existence_cache_key(slot));
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

// Raised for the whole of a store.  The On Checkpoint Save hook runs inside it, and a script that
// stores from that hook (mission.storeCheckpoint) would otherwise recurse until the stack ran out.
static bool Checkpoint_storing = false;

bool mission_checkpoint_store(const SCP_string& slot)
{
	if (!(Game_mode & GM_IN_MISSION)) {
		mprintf(("CHECKPOINT => store called outside a mission; ignoring.\n"));
		return false;
	}

	if (Checkpoint_storing) {
		mprintf(("CHECKPOINT => A store was asked for while another is being written; ignoring it.\n"));
		return false;
	}

	// While a checkpoint is being applied the mission is half old and half new (the clock has
	// jumped, the mission logic has not been put back yet), and scripts still run -- the arrival
	// hook fires for the ships the restore brings back.  A store from there would write that
	// in-between state over the slot.
	if (Game_restoring) {
		mprintf(("CHECKPOINT => A store was asked for during a restore; ignoring it.\n"));
		return false;
	}

	struct storing_scope {
		storing_scope() { Checkpoint_storing = true; }
		~storing_scope() { Checkpoint_storing = false; }
	} storing;

	if (!mission_checkpoint_allowed()) {
		mprintf(("CHECKPOINT => Checkpoints are switched off for this mission; not storing.\n"));
		return false;
	}

	// SEXPs keep evaluating while the player's ship is in its death roll, so a store can be asked
	// for then.  A ship mid-death-roll is recorded as destroyed, and a restore would take the
	// player's ship out of the mission with no death sequence to follow -- a checkpoint that can
	// never be resumed.  There is nothing worth saving at that point anyway.
	if (Player_ship != nullptr && Player_ship->flags[Ship::Ship_Flags::Dying]) {
		mprintf(("CHECKPOINT => The player's ship is dying; not storing.\n"));
		return false;
	}

	// Likewise once a supernova has hit: from there on the player's controls are locked and the
	// mission is on rails to its end, with nothing a restore could put back.
	if (supernova_stage() >= SUPERNOVA_STAGE::HIT) {
		mprintf(("CHECKPOINT => The supernova has hit; not storing.\n"));
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

		// A script may have renamed the ship; the fresh load will only know the parse name.  Not
		// for wing ships: every wave shares one parse object whose name is rewritten as each ship
		// is created, so for a wing ship it names whichever member came last and says nothing
		// about this one.  Taking it anyway made the restore rename a live later-wave ship to a
		// dead earlier one.  A script rename of a wing ship is therefore not followed.
		if (entry.has_p_objp() && entry.p_objp()->wingnum < 0 && stricmp(entry.p_objp()->name, entry.name) != 0) {
			state.parse_name = entry.p_objp()->name;
		}

		switch (entry.status) {
		case ShipStatus::PRESENT:
			break;

		case ShipStatus::NOT_YET_PRESENT:
			state.disposition = ShipDisposition::NotYetHere;
			data.ships.push_back(std::move(state));
			continue;

		case ShipStatus::DEATH_ROLL: {
			// A ship part-way through its death roll is going to be gone in a moment and
			// there is no way to resume a death roll on a fresh load.  Record it as already
			// destroyed; that is the state the mission is about to reach anyway.  The exited
			// record it is about to leave is built here from the live ship, the way
			// ship_add_exited_ship() will build it.
			state.disposition = ShipDisposition::Destroyed;
			state.exit_time = Missiontime;

			const ship* shipp = entry.shipp();
			const object* objp = entry.objp();
			if (shipp != nullptr && objp != nullptr) {
				state.ship_class = ship_class_name(shipp->ship_info_index);
				state.team = team_name(shipp->team);
				state.display_name = shipp->get_display_name();
				state.cargo = cargo_name(shipp->cargo1);
				state.cargo_no_deplete = (shipp->cargo1 & CARGO_NO_DEPLETE) != 0;
				state.exit_hull_strength = static_cast<int>(objp->hull_strength);
				if (shipp->flags[Ship::Ship_Flags::Cargo_revealed]) {
					state.exit_flags.emplace_back("cargo_known");
					state.time_cargo_revealed = shipp->time_cargo_revealed;
				}
				if (shipp->time_first_tagged > 0) {
					state.exit_flags.emplace_back("been_tagged");
				}
				if (shipp->flags[Ship::Ship_Flags::Red_alert_store_status]) {
					state.exit_flags.emplace_back("red_alert_carry");
				}
				if (shipp->flags[Ship::Ship_Flags::From_player_wing]) {
					state.exit_flags.emplace_back("from_player_wing");
				}
			}
			data.ships.push_back(std::move(state));
			continue;
		}

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

				// What the ship had become by the time it left; see ship_state::exit_time.
				state.ship_class = ship_class_name(exited.ship_class);
				state.team = team_name(exited.team);
				state.display_name = exited.display_name;
				state.cargo = cargo_name(exited.cargo1);
				state.cargo_no_deplete = (exited.cargo1 & CARGO_NO_DEPLETE) != 0;
				state.time_cargo_revealed = exited.time_cargo_revealed;
				state.exit_hull_strength = exited.hull_strength;
				collect_flags(exited.flags, Exit_flag_table, state.exit_flags);
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
		state.alt_name = alt_name_for_index(shipp->alt_type_index);
		state.callsign = callsign_for_index(shipp->callsign_index);
		state.countermeasure_class = weapon_class_name(shipp->current_cmeasure);
		state.persona = persona_name(shipp->persona_index);

		state.departure_location = static_cast<int>(shipp->departure_location);
		state.departure_anchor = anchor_name(shipp->departure_anchor);
		state.armor_type = armor_type_name(shipp->armor_type_idx);
		state.shield_armor_type = armor_type_name(shipp->shield_armor_type_idx);
		state.collision_damage_type = damage_type_name(shipp->collision_damage_type_idx);
		state.debris_damage_type = damage_type_name(shipp->debris_damage_type_idx);
		state.use_special_explosion = shipp->use_special_explosion;
		state.use_shockwave = shipp->use_shockwave;

		state.orders_present = true;
		for (size_t order : shipp->orders_accepted) {
			if (order < Player_orders.size()) {
				state.orders_accepted.push_back(Player_orders[order].parse_name);
			}
		}
		for (size_t order : shipp->orders_allowed_against) {
			if (order < Player_orders.size()) {
				state.orders_allowed_against.push_back(Player_orders[order].parse_name);
			}
		}

		for (const auto& entry : shipp->max_guard_ranges) {
			if (entry.shipnum >= 0 && entry.shipnum < MAX_SHIPS && entry.range > 0.0f) {
				guard_range_state guard;
				guard.ship = Ships[entry.shipnum].ship_name;
				guard.range = entry.range;
				state.guard_ranges.push_back(std::move(guard));
			}
		}
		state.special_warpout_ship = ship_name_for_objnum(shipp->special_warpout_objnum);

		state.glow_banks.assign(shipp->glow_point_bank_active.begin(), shipp->glow_point_bank_active.end());
		capture_instance_textures(shipp->model_instance_num, state.texture_old, state.texture_new);
		state.collision_group_id = objp->collision_group_id;

		state.team_color = shipp->team_name;
		state.secondary_team_color = shipp->secondary_team_name;
		store_iff_colors(shipp->ship_iff_color, state.iff_colors);

		state.sim_hull = objp->sim_hull_strength;

		// Who has damaged it, by signature at runtime; only a live attacker can be named, and only
		// a live one could still be credited.
		for (int i = 0; i < MAX_DAMAGE_SLOTS; i++) {
			if (shipp->damage_ship_id[i] <= 0 || shipp->damage_ship[i] <= 0.0f) {
				continue;
			}
			for (auto so : list_range(&Ship_obj_list)) {
				const object* attacker = &Objects[so->objnum];
				if (attacker->signature == shipp->damage_ship_id[i] && attacker->type == OBJ_SHIP) {
					damage_credit_state credit;
					credit.ship = Ships[attacker->instance].ship_name;
					credit.damage = shipp->damage_ship[i];
					state.damage_credits.push_back(std::move(credit));
					break;
				}
			}
		}

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

		if (wingp->has_display_name()) {
			state.display_name = wingp->display_name;
		}

		if (wingp->formation >= 0 && wingp->formation < static_cast<int>(Wing_formations.size())) {
			state.formation = Wing_formations[wingp->formation].name;
		}
		state.formation_scale = wingp->formation_scale;

		// set-arrival-info and set-departure-info rewrite all of this on a wing exactly as they
		// do on a ship that has not arrived.
		state.arrival_anchor = anchor_name(wingp->arrival_anchor);
		state.departure_anchor = anchor_name(wingp->departure_anchor);
		state.arrival_location = static_cast<int>(wingp->arrival_location);
		state.departure_location = static_cast<int>(wingp->departure_location);
		state.arrival_path_mask = wingp->arrival_path_mask;
		state.departure_path_mask = wingp->departure_path_mask;

		// The wing's own goal list, handed to each ship as it arrives.  No ship owns these yet,
		// hence the null.
		for (const auto& goal : wingp->ai_goals) {
			if (goal.ai_mode == AI_GOAL_NONE) {
				continue;
			}
			ai_goal_state goal_state;
			store_ai_goal(nullptr, goal, goal_state);
			state.goals.push_back(std::move(goal_state));
		}

		for (int j = 0; j < wingp->current_count && j < MAX_SHIPS_PER_WING; j++) {
			int shipnum = wingp->ship_index[j];
			if (shipnum >= 0 && shipnum < MAX_SHIPS) {
				state.ship_names.emplace_back(Ships[shipnum].ship_name);
			} else {
				state.ship_names.emplace_back();
			}
		}

		if (wingp->special_ship >= 0 && wingp->special_ship < wingp->current_count &&
		    wingp->ship_index[wingp->special_ship] >= 0) {
			state.special_ship = Ships[wingp->ship_index[wingp->special_ship]].ship_name;
		}

		data.wings.push_back(std::move(state));
	}

	// --- props ---
	store_props(data);

	// --- waypoint lists ---
	// Whole, since a script can have created, renamed or moved any of them; see
	// waypoint_list_state.
	for (const auto& list : Waypoint_lists) {
		waypoint_list_state state;
		state.name = list.get_name();
		for (const auto& point : list.get_waypoints()) {
			state.points.push_back(*point.get_pos());
		}
		data.waypoint_lists.push_back(std::move(state));
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
		state.type = Sexp_variables[i].type;

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

		if (Player->stats.m_medal_earned >= 0 && Player->stats.m_medal_earned < static_cast<int>(Medals.size())) {
			data.scoring.medal_earned = Medals[Player->stats.m_medal_earned].name;
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
		state.invalid = (goal.type & INVALID_GOAL) != 0;
		data.goals.push_back(std::move(state));
	}

	for (const auto& entry : Log_entries) {
		log_entry_state state;

		state.type = static_cast<int>(entry.type);
		state.flags = entry.flags;
		state.timestamp = entry.timestamp;
		state.timer_padding = entry.timer_padding;
		// See log_entry_state::index for which types carry an index that has to go out by name.
		if (entry.type == LOG_CARGO_REVEALED || entry.type == LOG_CAP_SUBSYS_CARGO_REVEALED) {
			state.index_name = cargo_name(entry.index);
		} else if (entry.type == LOG_SHIP_SUBSYS_DESTROYED) {
			int ship_class = (entry.index >> 16) & 0xffff;
			int subsys_index = entry.index & 0xffff;
			state.index_class = ship_class_name(ship_class);
			if (ship_class >= 0 && ship_class < ship_info_size() && subsys_index >= 0 &&
			    subsys_index < Ship_info[ship_class].n_subsystems) {
				state.index_name = Ship_info[ship_class].subsystems[subsys_index].subobj_name;
			}
		} else if (entry.type == LOG_WING_DESTROYED || entry.type == LOG_WING_DEPARTED) {
			// The wing's team, an IFF index; nothing reads it back, but it goes by name like every
			// other IFF index in the file.
			state.index_name = team_name(entry.index);
		} else {
			state.index = entry.index;
		}
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

		// An is-true-for-duration node that has started its clock is state too, sticky or not.
		int duration_index = Sexp_nodes[i].duration_index;
		bool has_duration = (duration_index >= 0) && (duration_index < static_cast<int>(Sexp_is_true_for_duration_times.size()));

		if (!sticky && Sexp_nodes[i].flags == SNF_DEFAULT_VALUE && !has_duration) {
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

		if (has_duration) {
			state.has_duration = true;
			state.duration_start = Sexp_is_true_for_duration_times[duration_index];
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
		state.alt_name = alt_name_for_index(p_objp->alt_type_index);
		state.callsign = callsign_for_index(p_objp->callsign_index);
		state.cargo = cargo_name(p_objp->cargo1);
		state.cargo_no_deplete = (p_objp->cargo1 & CARGO_NO_DEPLETE) != 0;

		state.collision_group_id = p_objp->collision_group_id;
		state.team_color = p_objp->team_color_setting;
		for (const auto& tr : p_objp->replacement_textures) {
			state.texture_old.emplace_back(tr.old_texture);
			state.texture_new.emplace_back(tr.new_texture);
		}
		store_iff_colors(p_objp->alt_iff_color, state.iff_colors);

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

	// Coordinate points on the escort list.  Ships are rebuilt from their escort flag; these
	// have no flag, so the list itself is walked for them.
	for (int i = 0; i < hud_escort_num_ships_on_list(); i++) {
		int objnum = hud_escort_return_objnum(i);
		if (objnum < 0 || objnum >= MAX_OBJECTS || Objects[objnum].type != OBJ_COORDINATE_POINT) {
			continue;
		}
		const mission_coordinate_point* point = find_coordinate_point_by_objnum(objnum);
		if (point != nullptr) {
			data.escort_points.push_back(point->name);
		}
	}

	store_asteroids(data);
	store_environment(data.environment);
	store_mission_extras(data.mission);
	store_projectiles(data);
	store_beams(data);

	// Every piece of debris: hull chunks, the generic fragments, and anything a script made.
	for (const auto& db : Debris) {
		if (!db.flags[Debris_Flags::Used] || db.objnum < 0) {
			continue;
		}

		const object* objp = &Objects[db.objnum];
		debris_state state;

		state.ship_class = ship_class_name(db.ship_info_index);
		state.team = team_name(db.team);
		state.is_hull = db.is_hull;
		state.time_started = db.time_started;

		auto pm = model_get(db.model_num);
		if (pm != nullptr) {
			state.model = pm->filename;
			if (db.submodel_num >= 0 && db.submodel_num < pm->n_models) {
				state.submodel = pm->submodel[db.submodel_num].name;
			}
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
		state.parent_alt_name = alt_name_for_index(db.parent_alt_name);
		state.do_not_expire = db.flags[Debris_Flags::DoNotExpire];

		// A chunk needs a submodel and something to find the model by: hull debris always has
		// its class, and anything else has the model's filename.
		if (!state.submodel.empty() && (!state.ship_class.empty() || !state.model.empty())) {
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

	auto key = existence_cache_key(slot);

	auto cached = Existence_cache.find(key);
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

	Existence_cache[key] = {SCP_string(Game_current_mission_filename), exists};

	return exists;
}

void mission_checkpoint_delete(const SCP_string& slot)
{
	// Every entry point is a no-op in multiplayer, deletion included: a multiplayer mission has
	// no checkpoints of its own, so the only files it could reach are somebody's single-player
	// ones.
	if (Game_mode & GM_MULTIPLAYER) {
		return;
	}

	checkpoint_delete_file(slot);
	invalidate_existence_cache(slot);
}

int mission_checkpoint_delete_all(const SCP_string& mission_name)
{
	if (Game_mode & GM_MULTIPLAYER) {
		return 0;
	}

	int deleted = checkpoint_delete_all(mission_name);

	// Whichever mission that was, nothing cached about any slot can be trusted now.
	Existence_cache.clear();

	return deleted;
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

	// Only these two states rebuild the level when the restart comes back round as ENTER_GAME (see
	// game_enter_state()).  Leaving the death roll's first state, DEATH_DIED, stops the mission
	// without loading it again, so a load asked for while the player is dying -- the obvious
	// "is-destroyed Alpha 1 -> load-checkpoint" -- waits, still queued, until the death roll
	// reaches DEATH_BLEW_UP, which restarts the way the death popup's own Restart does.
	int state = gameseq_get_state();
	if (state != GS_STATE_GAME_PLAY && state != GS_STATE_DEATH_BLEW_UP) {
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
	Pending_load.awaiting_reload = true;

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

	// The quiet red-alert cleanup does not count the kill towards the ship-type totals the way
	// SHIP_DESTROYED does, and percent-ships-destroyed reads those totals, so count it here with
	// the same test ship_cleanup() applies -- against the class the ship had when it died.
	if (state.disposition == ShipDisposition::Destroyed) {
		ship* shipp = &Ships[shipnum];
		int killed_class = lookup_ship_class(state.ship_class);
		if (killed_class < 0) {
			killed_class = shipp->ship_info_index;
		}
		if (!(shipp->flags[Ship::Ship_Flags::Ignore_count]) ||
		    ((shipp->wingnum != -1) && !(Wings[shipp->wingnum].flags[Ship::Wing_Flags::Ignore_count]))) {
			ship_add_ship_type_kill_count(killed_class);
		}
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

	// The record was just built from the ship the mission file describes; put back what the ship
	// had become by the time it left, which is what the SEXPs that ask about dead ships read.
	int exited_class = lookup_ship_class(state.ship_class);
	if (exited_class >= 0) {
		exited.ship_class = exited_class;
	}
	int exited_team = lookup_team(state.team);
	if (exited_team >= 0) {
		exited.team = exited_team;
	}
	if (!state.display_name.empty()) {
		exited.display_name = state.display_name;
	}
	if (!state.cargo.empty()) {
		int cargo = lookup_cargo(state.cargo);
		if (state.cargo_no_deplete) {
			cargo |= CARGO_NO_DEPLETE;
		}
		exited.cargo1 = static_cast<char>(cargo);
	}
	exited.time_cargo_revealed = state.time_cargo_revealed;
	exited.hull_strength = state.exit_hull_strength;
	apply_flags(state.exit_flags, Exit_flag_table, exited.flags);
}

// Take out every ship the checkpoint says had already gone but which is standing here alive.
// Safe to call more than once: a ship that has already been removed is EXITED and is skipped.
// The registry entry a ship state describes.  Until apply_ship_renames() has run, a ship a script
// renamed is only in the registry under its parse name.
const ship_registry_entry* registry_entry_for(const ship_state& state)
{
	auto entry = ship_registry_get(state.name);
	if (entry == nullptr && !state.parse_name.empty()) {
		entry = ship_registry_get(state.parse_name);
	}
	return entry;
}

// Put back the names scripts had given ships.  ship.Name (Lua) renames the ship and its registry
// entry but not its parse object, so the fresh load created the ship under the mission file's
// name; after this, every ship that exists is findable by the name the checkpoint stores.  A ship
// not created yet keeps its parse name, since that is what its parse object will create it as.
void apply_ship_renames(const checkpoint_data& data)
{
	for (const auto& state : data.ships) {
		if (state.parse_name.empty()) {
			continue;
		}

		int entry_index = ship_registry_get_index(state.parse_name);
		if (entry_index < 0 || !Ship_registry[entry_index].has_shipp()) {
			continue;
		}

		ship* shipp = Ship_registry[entry_index].shipp();
		strcpy_s(shipp->ship_name, state.name.c_str());
		ship_registry_rename(entry_index, shipp->ship_name, true);

		mprintf(("CHECKPOINT => Ship '%s' is named '%s' again.\n", state.parse_name.c_str(), state.name.c_str()));
	}
}

void remove_gone_ships(const checkpoint_data& data)
{
	for (const auto& state : data.ships) {
		if (state.disposition == ShipDisposition::Present) {
			continue;
		}

		auto entry = registry_entry_for(state);
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

		auto entry = registry_entry_for(state);
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
// dock_evaluate_all_docked_objects() callback: remember the member of a parse-object dock group
// that carries the leader flag.
void find_dock_leader_helper(p_object* pobjp, p_dock_function_info* infop)
{
	if (pobjp->flags[Mission::Parse_Object_Flags::SF_Dock_leader]) {
		infop->maintained_variables.objp_value = pobjp;
		infop->early_return_condition = true;
	}
}

void restore_loose_arrivals(const checkpoint_data& data)
{
	for (const auto& state : data.ships) {
		if (state.disposition != ShipDisposition::Present) {
			continue;
		}

		auto entry = registry_entry_for(state);
		if (entry == nullptr || entry->status != ShipStatus::NOT_YET_PRESENT || !entry->has_p_objp()) {
			continue;
		}

		auto p_objp = entry->p_objp();

		// Wing members arrive with their wing, and a ship whose docked group has already been
		// created came in as somebody else's cargo.
		if (p_objp->wingnum >= 0 || p_objp->created_object != nullptr) {
			continue;
		}

		// A docked group is created whole by its leader, and only its leader: parse_create_object()
		// asserts as much.  Which member leads is decided by class, not by registry order, so a
		// non-leader can come up here before its leader has -- and the leader may not be coming
		// at all, if it was destroyed or departed before the checkpoint while this one survived.
		// Either way the answer is the same: bring the leader in, which creates the whole group.
		// A leader the checkpoint says is gone is taken out again by remove_gone_ships(), which
		// runs after this and undocks what it removes.
		if (object_is_docked(p_objp) && !p_objp->flags[Mission::Parse_Object_Flags::SF_Dock_leader]) {
			p_dock_function_info dfi;
			dock_evaluate_all_docked_objects(p_objp, &dfi, find_dock_leader_helper);

			// A leader in a wing arrives with its wing (restore_wing_arrivals() has already run).
			p_object* leader = dfi.maintained_variables.objp_value;
			if (leader != nullptr && leader->created_object == nullptr && leader->wingnum < 0) {
				mission_maybe_make_ship_arrive(leader, true);
			}
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
	// Every ship that is going to exist does now; give the renamed ones their names back before
	// anything looks them up by those names.
	apply_ship_renames(data);
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

	shipp->alt_type_index = alt_index_for_name(state.alt_name);
	shipp->callsign_index = callsign_index_for_name(state.callsign);

	shipp->departure_location = static_cast<DepartureLocation>(state.departure_location);
	{
		auto departure_anchor = lookup_anchor(state.departure_anchor);
		if (departure_anchor.isValid()) {
			shipp->departure_anchor = departure_anchor;
		}
	}

	shipp->armor_type_idx = lookup_armor_type(state.armor_type);
	shipp->shield_armor_type_idx = lookup_armor_type(state.shield_armor_type);
	shipp->collision_damage_type_idx = lookup_damage_type(state.collision_damage_type);
	shipp->debris_damage_type_idx = lookup_damage_type(state.debris_damage_type);
	shipp->use_special_explosion = state.use_special_explosion;
	shipp->use_shockwave = state.use_shockwave;

	if (state.orders_present) {
		shipp->orders_accepted.clear();
		shipp->orders_allowed_against.clear();
		for (const auto& name : state.orders_accepted) {
			for (size_t i = 0; i < Player_orders.size(); i++) {
				if (!stricmp(Player_orders[i].parse_name.c_str(), name.c_str())) {
					shipp->orders_accepted.insert(i);
					break;
				}
			}
		}
		for (const auto& name : state.orders_allowed_against) {
			for (size_t i = 0; i < Player_orders.size(); i++) {
				if (!stricmp(Player_orders[i].parse_name.c_str(), name.c_str())) {
					shipp->orders_allowed_against.insert(i);
					break;
				}
			}
		}
	}

	for (size_t i = 0; i < state.glow_banks.size() && i < shipp->glow_point_bank_active.size(); i++) {
		shipp->glow_point_bank_active[i] = state.glow_banks[i];
	}
	for (size_t i = 0; i < state.texture_old.size() && i < state.texture_new.size(); i++) {
		ship_replace_active_texture(entry->shipnum, state.texture_old[i].c_str(), state.texture_new[i].c_str());
	}
	objp->collision_group_id = state.collision_group_id;

	shipp->team_name = state.team_color;
	shipp->secondary_team_name = state.secondary_team_color;
	load_iff_colors(state.iff_colors, shipp->ship_iff_color);

	objp->sim_hull_strength = state.sim_hull;

	// Guard ranges, the Knossos and the damage credits name other ships and are resolved in the
	// second pass; see resolve_ship_references().

	int persona = lookup_persona(state.persona);
	if (persona >= 0) {
		shipp->persona_index = persona;
	}

	apply_flags(state.flags, Ship_flag_table, shipp->flags);

	// Object flags go through obj_set_flags() rather than straight onto the object, because
	// Collides is not just a bit: membership of the collision pair list only changes through
	// obj_add_collider()/obj_remove_collider(), which obj_set_flags() drives.  Bashing the bit on
	// a ship the mission file created with no-collide leaves it a ghost for the rest of the
	// mission.
	{
		auto object_flags = objp->flags;
		apply_flags(state.object_flags, Object_flag_table, object_flags);
		obj_set_flags(objp, object_flags);
	}

	// After the flags, because Has_display_name decides whether the name is used at all: a ship
	// the mission had cleared the display name on has to come back cleared, not with whatever
	// the mission file gave it.
	shipp->display_name = shipp->flags[Ship::Ship_Flags::Has_display_name] ? state.display_name : SCP_string();

	load_ship_scalars(*shipp, state.floats, state.ints);
	load_physics(objp->phys_info, state.physics_floats, state.physics_vecs);

	// current_viewpoint indexes the model's eye points, and the model may not be the one the
	// checkpoint was written against -- a kept loadout, or a mod that re-exported the pof.  The
	// big-ship AI reads view_positions[current_viewpoint] unchecked.
	{
		polymodel* pm = model_get(Ship_info[shipp->ship_info_index].model_num);
		if (pm == nullptr || shipp->current_viewpoint < 0 || shipp->current_viewpoint >= pm->n_view_positions) {
			shipp->current_viewpoint = 0;
		}
	}

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

		wingp->formation = -1;
		for (int i = 0; i < static_cast<int>(Wing_formations.size()); i++) {
			if (!state.formation.empty() && !stricmp(Wing_formations[i].name, state.formation.c_str())) {
				wingp->formation = i;
				break;
			}
		}
		wingp->formation_scale = state.formation_scale;

		// The flag was restored above; the string has to follow it or the wing claims a display
		// name it does not have.
		if (wingp->flags[Ship::Wing_Flags::Has_display_name]) {
			wingp->display_name = state.display_name;
		}

		wingp->arrival_location = static_cast<ArrivalLocation>(state.arrival_location);
		wingp->departure_location = static_cast<DepartureLocation>(state.departure_location);
		wingp->arrival_path_mask = state.arrival_path_mask;
		wingp->departure_path_mask = state.departure_path_mask;

		// A name that no longer names anything leaves the mission's own anchor in place rather
		// than clearing it.
		auto arrival_anchor = lookup_anchor(state.arrival_anchor);
		if (arrival_anchor.isValid()) {
			wingp->arrival_anchor = arrival_anchor;
		}
		auto departure_anchor = lookup_anchor(state.departure_anchor);
		if (departure_anchor.isValid()) {
			wingp->departure_anchor = departure_anchor;
		}

		for (auto& goal : wingp->ai_goals) {
			ai_goal_reset(&goal);
		}
		for (size_t i = 0; i < state.goals.size() && i < MAX_AI_GOALS; i++) {
			load_ai_goal(state.goals[i], wingp->ai_goals[i]);
		}

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

		// The leader's slot in the list just rebuilt.  Left at the mission file's value it can
		// point past the end of a wing that has lost ships, and the squad message code indexes
		// Ships[] with ship_index[special_ship] unchecked.
		wingp->special_ship = 0;
		for (int i = 0; i < count; i++) {
			if (!state.special_ship.empty() && lcase_equal(Ships[wingp->ship_index[i]].ship_name, state.special_ship)) {
				wingp->special_ship = i;
				break;
			}
		}
		if (count > 0) {
			wingp->special_ship_ship_info_index = Ships[wingp->ship_index[wingp->special_ship]].ship_info_index;
		}
	}
}

// Tell a ship that was on its way out to leave again.
//
// A ship caught departing comes back with its AI mode restored but nothing behind it: the bay
// departure path is not captured (ai_bay_depart() drops to AIM_NONE when it finds none) and the
// Departing ship flags are not restored.  For a lone ship the departure cue simply fires again.
// For a wing member it does not -- the wing's own Departing flag is restored, and
// mission_eval_departures() skips a wing already flagged, so nothing ever re-issues the order and
// the wing idles for the rest of the mission with every event waiting on it.  So the order is
// re-issued here, through the same call the cue would have made, for every ship whose restored
// mode says it was leaving.  Runs after apply_wings(), which is where the wing's departure info
// that mission_do_departure() copies onto the ship comes back.
void reissue_departures(const checkpoint_data& data)
{
	for (const auto& state : data.ships) {
		if (state.disposition != ShipDisposition::Present || !state.ai.present) {
			continue;
		}

		auto entry = ship_registry_get(state.name);
		if (entry == nullptr || !entry->has_shipp() || !entry->has_objp()) {
			continue;
		}

		ship* shipp = &Ships[entry->shipnum];
		if (shipp->ai_index < 0 || shipp->ai_index >= MAX_AI_INFO) {
			continue;
		}

		ai_info* aip = &Ai_info[shipp->ai_index];
		if (aip->mode != AIM_BAY_DEPART && aip->mode != AIM_WARP_OUT) {
			continue;
		}

		// From the top: mission_do_departure() treats a ship already in AIM_WARP_OUT as mid-way
		// and skips the setup a wing member needs.
		aip->mode = AIM_NONE;
		mission_do_departure(&Objects[entry->objnum]);
	}
}

void apply_variables(const checkpoint_data& data)
{
	for (const auto& state : data.variables) {
		int index = get_index_sexp_variable_name(state.name.c_str());
		if (index < 0) {
			// Not in this parse: a script created it during the mission (mission.SEXPVariables
			// accepts new names).  Recreate it, which needs the type word; a file from before
			// that was stored can only fall back to the number/string bit.
			int type = (state.type != 0) ? state.type : (state.is_number ? SEXP_VARIABLE_NUMBER : SEXP_VARIABLE_STRING);
			if (sexp_add_variable(state.value.c_str(), state.name.c_str(), type) < 0) {
				mprintf(("CHECKPOINT => No room to recreate SEXP variable '%s'; skipping it.\n", state.name.c_str()));
			}
			continue;
		}

		bool is_number = (Sexp_variables[index].type & SEXP_VARIABLE_NUMBER) != 0;
		if (is_number != state.is_number) {
			mprintf(("CHECKPOINT => SEXP variable '%s' has changed type; skipping it.\n", state.name.c_str()));
			continue;
		}

		strcpy_s(Sexp_variables[index].text, state.value.c_str());
		// The persistence bits can be changed by a script mid-mission, so the whole type word
		// comes back when the file has it.
		if (state.type != 0) {
			Sexp_variables[index].type = state.type;
		}
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

	Player->stats.m_medal_earned = -1;
	if (!data.scoring.medal_earned.empty()) {
		for (int i = 0; i < static_cast<int>(Medals.size()); i++) {
			if (!stricmp(Medals[i].name, data.scoring.medal_earned.c_str())) {
				Player->stats.m_medal_earned = i;
				break;
			}
		}
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
		p_objp->alt_type_index = alt_index_for_name(state.alt_name);
		p_objp->callsign_index = callsign_index_for_name(state.callsign);

		p_objp->collision_group_id = state.collision_group_id;
		p_objp->team_color_setting = state.team_color;
		// Replaced wholesale: the saved list already holds what the mission file gave it.
		p_objp->replacement_textures.clear();
		for (size_t i = 0; i < state.texture_old.size() && i < state.texture_new.size(); i++) {
			texture_replace tr;
			memset(&tr, 0, sizeof(tr));
			strcpy_s(tr.ship_name, p_objp->name);
			strcpy_s(tr.old_texture, state.texture_old[i].c_str());
			strcpy_s(tr.new_texture, state.texture_new[i].c_str());
			tr.new_texture_id = -1;
			tr.from_table = false;
			p_objp->replacement_textures.push_back(tr);
		}
		load_iff_colors(state.iff_colors, p_objp->alt_iff_color);
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

	// Coordinate points come back by name.  A point the mission no longer has is simply not
	// re-added.  The points themselves, with their escort priorities, were restored with the
	// environment, so a point a script created or promoted and then escorted is back too.
	for (const auto& name : data.escort_points) {
		const mission_coordinate_point* point = find_coordinate_point_by_name(name.c_str());
		if (point != nullptr && point->objnum >= 0) {
			hud_add_ship_to_escort(point->objnum, 1);
		}
	}

	// The gauge switches, the HUD toggles, the ignored keys and the message log.  After the
	// ships, since the gauge set depends on the player's ship class.
	const auto& extras = data.mission;
	if (extras.hud_present) {
		// Every gauge goes back to its default first: `active` is not reset by the level init,
		// so a gauge a SEXP hid in an earlier run would otherwise stay hidden.
		for (const auto& gauge : live_hud_gauges()) {
			bool custom = gauge->isCustom();
			SCP_string name = custom ? SCP_string(gauge->getCustomGaugeName()) : gauge->getConfigName();

			const hud_gauge_state* saved = nullptr;
			for (const auto& state : extras.hud_gauges) {
				if (state.custom == custom && !stricmp(state.name.c_str(), name.c_str())) {
					saved = &state;
					break;
				}
			}

			gauge->updateActive(saved == nullptr || saved->active);
			gauge->updateSexpOverride(saved != nullptr && saved->sexp_override);

			if (saved != nullptr && saved->sexp_color) {
				// The same dance hud-set-color does: the SEXP lock is what keeps the HUD
				// configuration from overwriting the colour, so it has to be lifted to set it.
				gauge->sexpLockConfigColor(false);
				gauge->updateColor(saved->color[0], saved->color[1], saved->color[2], saved->color[3]);
				gauge->sexpLockConfigColor(true);
			}
		}

		hud_set_contrast(extras.hud_high_contrast);
		Disable_cockpits = extras.disable_cockpits;
		Disable_cockpit_sway = extras.disable_cockpit_sway;
		Sensor_static_forced = extras.sensor_static_forced;

		for (int i = 0; i < CCFG_MAX; i++) {
			Ignored_keys[i] = 0;
		}
		for (const auto& key : extras.ignored_keys) {
			for (int i = 0; i < CCFG_MAX && i < static_cast<int>(Control_config.size()); i++) {
				if (!stricmp(Control_config[i].text.c_str(), key.action.c_str())) {
					Ignored_keys[i] = key.count;
					break;
				}
			}
		}

		// The log is rebuilt whole: whatever the fresh load has logged so far belongs to the run
		// being replaced.  Each line carries its own mission time.
		Msg_scrollback_vec.clear();
		for (const auto& line : extras.scrollback) {
			hud_add_msg_to_scrollback(line.text.c_str(), line.source, line.time);
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

		// A local SSM in stages 2 and 3 is inside the warp-out effect or in subspace: stage 2 reads
		// Objects[lssm_warp_idx] unguarded, and that fireball did not survive the reload, while a
		// stage 3 missile had its Renders and Collides flags cleared, which weapon_create() has
		// just put back.  Neither is recoverable in place, so the missile is put back at stage 1
		// with the warp-out due now: the engine opens a fresh warphole from where it stands and
		// carries on.  Stage 4 (warping in) guards the index and falls through to stage 5 on its
		// own when it finds none.
		if (wp->lssm_stage == 2 || wp->lssm_stage == 3) {
			wp->lssm_stage = 1;
			wp->lssm_warpout_time = timestamp(0);
		} else if (wp->lssm_stage == 4) {
			// Warping in, next to its target, with the warp-in effect gone: stage 4 only waits on
			// that effect before handing over, so hand over now the way it would.
			wp->lssm_stage = 5;
			vm_vec_copy_scale(&objp->phys_info.desired_vel, &objp->orient.vec.fvec, Weapon_info[weapon_class].lssm_stage5_vel);
		}

		wp->cmeasure_timer = translate_stamp(state.cmeasure_timer);

		objp->phys_info.rotvel = state.rotational_velocity;
		wp->mine_chase_expires = TIMESTAMP(translate_stamp(state.mine_chase_expires));
		wp->mine_chase_cooldown_expires = TIMESTAMP(translate_stamp(state.mine_chase_cooldown_expires));
		// Empty in a checkpoint from before the spawn clocks were stored; weapon_create() has
		// already started them over in that case.
		for (size_t i = 0; i < state.last_spawn_times.size() && i < MAX_SPAWN_TYPES_PER_WEAPON; i++) {
			wp->last_spawn_time[i] = TIMESTAMP(translate_stamp(state.last_spawn_times[i]));
		}
		// 0 and 1 are the "pick now" encodings and pass through translate_stamp() unchanged.
		wp->pick_big_attack_point_timestamp = translate_stamp(state.big_attack_point_stamp);
		wp->big_attack_point = state.big_attack_point;
		objp->collision_group_id = state.collision_group_id;

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

		// A fighter beam fires from a stand-in turret (ship::fighter_beam_turret_data) that is not
		// in the subsystem list, so it has no key and turret is null here, and the beam is
		// dropped below.  That is deliberate: the stand-in is only initialised by
		// ship_fire_primary() the first time the fighter fires, from data private to ship.cpp,
		// and re-firing through it before that dereferences null.  The player is still holding
		// the trigger, so the beam is back within a frame anyway.

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

		// The beam's own flags are BF_*; what beam_fire() reads from the fire info are BFIF_*, and
		// the two are different bit layouts despite the matching names.  Translate one by one.
		{
			int saved_flags = lookup_beam_flags(state.flags);
			info.bfi_flags = 0;
			if (saved_flags & BF_FORCE_FIRING) {
				info.bfi_flags |= BFIF_FORCE_FIRING;
			}
			if (saved_flags & BF_IS_FIGHTER_BEAM) {
				info.bfi_flags |= BFIF_IS_FIGHTER_BEAM;
			}
			if (saved_flags & BF_TARGETING_COORDS) {
				info.bfi_flags |= BFIF_TARGETING_COORDS;
			}
			if (saved_flags & BF_FLOATING_BEAM) {
				info.bfi_flags |= BFIF_FLOATING_BEAM;
			}
		}

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
		// beam_fire() ignores the point it is handed and takes the turret's next firing point,
		// which the subsystem restore has already advanced past the barrel this beam came from.
		// beam_aim() places the beam from b->firingpoint every frame, so put it back.
		b->firingpoint = state.firingpoint;
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
		// Only a stamp the file carried is in the checkpoint's clock.  If beam_start_firing() chose
		// to warm down on its own, the stamp it set is already in this run's clock and shifting it
		// would either end the warmdown next frame or stretch it for minutes.
		if (b->warmdown_stamp != -1 && state.warmdown_stamp >= 0) {
			b->warmdown_stamp = translate_stamp(state.warmdown_stamp);
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
		if (ship_class < 0 && state.is_hull) {
			// Hull debris is a piece of that class's model; without the class there is nothing
			// to make it from.
			continue;
		}

		// The model by filename when the file has one; a checkpoint from before that was stored
		// has hull debris only, whose model is the class's.  model_load() hands back a model that
		// is already loaded, which is the usual case -- the class's model, or the generic debris
		// model the level init pages in.
		int model_num = -1;
		if (!state.model.empty()) {
			model_num = model_load(state.model.c_str(), nullptr, ErrorType::WARNING);
		} else if (ship_class >= 0) {
			model_num = Ship_info[ship_class].model_num;
		}
		if (model_num < 0) {
			mprintf(("CHECKPOINT => No model for debris of '%s'; dropping it.\n",
			         state.model.empty() ? state.ship_class.c_str() : state.model.c_str()));
			continue;
		}

		int submodel_num = model_find_submodel_index(model_num, state.submodel.c_str());
		if (submodel_num < 0) {
			mprintf(("CHECKPOINT => '%s' has no submodel '%s' any more; dropping that debris.\n",
			         state.model.empty() ? state.ship_class.c_str() : state.model.c_str(),
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
			alt_index_for_name(state.parent_alt_name),
			lookup_team(state.team),
			state.hull_strength,
			0,
			model_num,
			submodel_num,
			&state.pos,
			&state.orient,
			state.is_hull,
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
		db->time_started = state.time_started;
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
		mprintf(("CHECKPOINT => Restored %d piece(s) of debris.\n", created));
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
		if (state.invalid) {
			it->type |= INVALID_GOAL;
		} else {
			it->type &= ~INVALID_GOAL;
		}
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
		// The two index meanings that went out by name; see log_entry_state::index.
		if (entry.type == LOG_CARGO_REVEALED || entry.type == LOG_CAP_SUBSYS_CARGO_REVEALED) {
			entry.index = lookup_cargo(state.index_name);
		} else if (entry.type == LOG_SHIP_SUBSYS_DESTROYED) {
			int ship_class = lookup_ship_class(state.index_class);
			int subsys_index = -1;
			if (ship_class >= 0) {
				for (int i = 0; i < Ship_info[ship_class].n_subsystems; i++) {
					if (!subsystem_stricmp(Ship_info[ship_class].subsystems[i].subobj_name, state.index_name.c_str())) {
						subsys_index = i;
						break;
					}
				}
			}
			// The log display indexes Ship_info[].subsystems[] with this unchecked, so an entry
			// that cannot be resolved is left out rather than pointed at something else.
			if (subsys_index < 0) {
				mprintf(("CHECKPOINT => Log entry for subsystem '%s' of class '%s' cannot be resolved; dropping it.\n",
				         state.index_name.c_str(),
				         state.index_class.c_str()));
				continue;
			}
			entry.index = ((ship_class << 16) & 0xffff0000) | (subsys_index & 0xffff);
		} else if (entry.type == LOG_WING_DESTROYED || entry.type == LOG_WING_DEPARTED) {
			entry.index = lookup_team(state.index_name);
		}
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

		// The duration clocks are handed out in evaluation order and cleared by the level init,
		// so the node simply gets the next slot, holding the mission time it started at.
		if (state.has_duration) {
			Sexp_nodes[state.index].duration_index = static_cast<int>(Sexp_is_true_for_duration_times.size());
			Sexp_is_true_for_duration_times.push_back(state.duration_start);
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

	// Not timestamp_get_mission_time(): that reads the same frame-start snapshot timestamp() does,
	// which the jump above did not refresh, so it would answer roughly zero until the first frame
	// of the restored mission recomputes it.  The saved value is what everything created during
	// the apply that stamps Missiontime -- debris, freshly initialised AI -- should see.
	Missiontime = data.mission_time;
	The_mission.HUD_timer_padding = data.hud_timer_padding;

	mprintf(("CHECKPOINT => Clock restored to mission time %d; shifting saved timestamps by %d ms.\n",
	         f2i(Missiontime),
	         Stamp_delta));
}

} // namespace

void mission_checkpoint_maybe_offer_resume()
{
	// This runs on every entry into gameplay, with the mission freshly parsed, which makes it the
	// one place to note how many messages the parse produced: everything a script adds after
	// this is what the store has to carry.  Before any early return, deliberately.
	Parsed_message_count = Num_messages;

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
	// debrief_accept() calls this before it branches on multiplayer, and a multiplayer mission
	// never has checkpoints to clean up.
	if (Game_mode & GM_MULTIPLAYER) {
		return;
	}

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

	// The restore works by bashing saved state onto a freshly loaded mission.  If the level was not
	// rebuilt on the way here, applying it would pile the saved state on top of the live one
	// (duplicate ships from the reconciliation, doubled scoring), so refuse outright.
	if (Pending_load.awaiting_reload) {
		mprintf(("CHECKPOINT => The mission was not reloaded for checkpoint '%s'; discarding it.\n", Pending_load.slot.c_str()));
		mission_checkpoint_clear_pending();
		return;
	}

	// If we somehow arrived in a different mission -- the restart failed and dropped the
	// player back to the main hall, say, and they then started something else -- the saved
	// state belongs to a mission that is not loaded and must not be applied to this one.
	if (!checkpoint_same_mission(data.mission_filename.c_str(), Game_current_mission_filename)) {
		mprintf(("CHECKPOINT => Checkpoint '%s' is for '%s' but '%s' is loaded; discarding it.\n",
		         data.slot.c_str(),
		         data.mission_filename.c_str(),
		         Game_current_mission_filename));
		mission_checkpoint_clear_pending();
		return;
	}

	// The store refuses to write while the player is dying, but a file from an older build, or
	// one written from a script that got at the store some other way, could still say the player's
	// ship is gone.  Applying that would remove the player's ship and leave a ghost with no death
	// sequence, so the mission starts fresh instead.
	if (Player_ship != nullptr) {
		for (const auto& state : data.ships) {
			if (state.disposition != ShipDisposition::Present && !stricmp(state.name.c_str(), Player_ship->ship_name)) {
				mprintf(("CHECKPOINT => Checkpoint '%s' records the player's ship '%s' as gone; discarding it.\n",
				         data.slot.c_str(),
				         Player_ship->ship_name));
				mission_checkpoint_clear_pending();
				return;
			}
		}
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
	apply_waypoint_lists(data);
	apply_mission_extras(data);

	reconcile_ship_existence(data);

	// After the reconciliation, which counts a use for every reinforcement it brings back.
	apply_reinforcements(data);

	Max_restored_goal_signature = -1;

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

	// Before anything is re-docked, since restore_docks() only ever adds links and the mission
	// load has just rebuilt the file's own docking.
	undock_stale_links(data);

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
			resolve_ship_references(&Ships[entry->shipnum], state);
		}
	}

	// Now that every ship a nav could point at exists.
	apply_navpoints(data);

	apply_asteroids(data);
	apply_props(data);
	apply_wings(data);

	// Every restored goal, ship and wing alike, is back now; keep new goals from reusing a
	// signature one of them holds.  See Max_restored_goal_signature.
	if (Ai_goal_signature <= Max_restored_goal_signature) {
		Ai_goal_signature = Max_restored_goal_signature + 1;
	}

	// After the wings, whose departure info a departing wing member is handed on the way out.
	reissue_departures(data);

	apply_variables(data);
	apply_file_sounds(data);
	apply_scoring(data);

	// Debris is independent of everything else; it just needs the ship classes paged in, which the
	// mission load has already done.
	apply_debris(data);
	apply_projectiles(data);
	apply_beams(data);

	// After the world, because a restored event's state describes ships that now exist.
	apply_mission_logic(data);

	// After the mission logic, whose events the queued messages refer to.
	apply_mission_history(data);

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

	// The level a posted checkpoint restart was waiting for.
	Pending_load.awaiting_reload = false;

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
