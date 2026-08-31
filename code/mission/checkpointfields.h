/*
 * Copyright (C) Freespace Open 2013.  All rights reserved.
 *
 * All source code herein is the property of Freespace Open. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 */

#ifndef _CHECKPOINTFIELDS_H
#define _CHECKPOINTFIELDS_H

/*
 * Field registries for the mission checkpoint system.
 *
 * Each list below names the runtime-mutable fields of one struct that a checkpoint captures.
 * The lists are expanded twice -- once to write and once to read -- so adding a saved field
 * is a one-line change in exactly one place, and the write and read sides can never drift
 * apart.
 *
 * The reader always supplies the freshly-created value as the default, so a field that is
 * absent from the file (because it was added to the engine after the checkpoint was written)
 * simply keeps whatever the mission load produced.  Likewise a field the file still carries
 * but the engine no longer has just disappears from the list and is ignored on read.  That is
 * what lets a checkpoint survive an engine update without any version gate.
 *
 * Only add a field here if it is genuinely mutable during a mission.  Anything derived from
 * the ship class, the model, or the mission file is reproduced by the mission load itself and
 * must not be captured -- writing it back would defeat table changes and waste space.
 *
 * Do NOT add pointers, handles, or indices into runtime arrays (objnum, model_instance_num,
 * sound handles, subsystem pointers).  Those are meaningless across a mission reload and are
 * handled by name elsewhere in missioncheckpoint.cpp.
 *
 * A TIMESTAMP FIELD BELONGS IN THE ..._STAMPS LIST, NEVER THE ..._INTS ONE.  Engine timestamps
 * are absolute values in a clock that runs from game launch, not from mission start, so a stamp
 * saved in one run means nothing in the next.  The stamps lists are the ones the restore shifts
 * into the current clock; see translate_stamp() in missioncheckpoint.cpp.  Put a timestamp in
 * an ints list and it will be restored verbatim, i.e. already long elapsed.  Both lists are
 * written to and read from the same map, so moving a field between them is not a file format
 * change and needs no version bump.
 */

// ------------------------------------------------------------------
// physics_info -- see code/physics/physics.h
// ------------------------------------------------------------------

// Scalars that describe how the object is currently moving.  The limits (max_vel, rotdamp,
// the various time constants) are all re-derived from the ship class on load and so are
// deliberately absent.
#define CKPT_PHYSICS_FLOATS(F)                                                                 \
	F(speed)                                                                                   \
	F(fspeed)                                                                                  \
	F(heading)                                                                                 \
	F(cur_glide_cap)

#define CKPT_PHYSICS_VECS(F)                                                                   \
	F(vel)                                                                                     \
	F(rotvel)                                                                                  \
	F(desired_vel)                                                                             \
	F(desired_rotvel)                                                                          \
	F(prev_ramp_vel)                                                                           \
	F(linear_thrust)                                                                           \
	F(rotational_thrust)                                                                       \
	F(acceleration)

// ------------------------------------------------------------------
// ship -- see code/ship/ship.h
// ------------------------------------------------------------------

// Consumables and other simple per-ship state.  Hull and shields live on the object, not the
// ship, and are handled separately.
#define CKPT_SHIP_FLOATS(F)                                                                    \
	F(ship_max_hull_strength)                                                                  \
	F(ship_max_shield_strength)                                                                \
	F(afterburner_fuel)                                                                        \
	F(weapon_energy)                                                                           \
	F(target_shields_delta)                                                                    \
	F(target_weapon_energy_delta)                                                              \
	F(total_damage_received)                                                                   \
	F(emp_intensity)                                                                           \
	F(emp_decr)                                                                                \
	F(tag_total)                                                                               \
	F(tag_left)                                                                                \
	F(level2_tag_total)                                                                        \
	F(level2_tag_left)

// current_cmeasure (a weapon class index), persona_index (an index into Personas, built from
// messages.tbl) and subsys_cargo_name / cargo1 (indices into Cargo_names, which set-cargo extends
// at runtime) are all table or runtime indices and are captured by name instead; see the
// "never write a runtime index" rule above.  hotkey is a hotkey *set* number, not an index into
// anything, so it stays.
#define CKPT_SHIP_INTS(F)                                                                      \
	F(cmeasure_count)                                                                          \
	F(shield_recharge_index)                                                                   \
	F(weapon_recharge_index)                                                                   \
	F(engine_recharge_index)                                                                   \
	F(escort_priority)                                                                         \
	F(respawn_priority)                                                                        \
	F(score)                                                                                   \
	F(hotkey)                                                                                  \
	F(alt_type_index)                                                                          \
	F(callsign_index)                                                                          \
	F(ship_guardian_threshold)                                                                 \
	F(subsys_disrupted_flags)                                                                  \
	F(num_swarm_missiles_to_fire)                                                              \
	F(swarm_missile_bank)                                                                      \
	F(num_corkscrew_to_fire)                                                                   \
	F(corkscrew_missile_bank)                                                                  \
	F(primitive_sensor_range)                                                                  \
	F(current_viewpoint)                                                                       \
	F(arrival_distance)                                                                        \
	F(arrival_path_mask)                                                                       \
	F(departure_path_mask)

// arrival_delay and departure_delay are dual-purpose: in-mission they hold a real timestamp once
// the delay timer has been armed, and a non-positive value meaning "so many seconds, timer not
// set yet" before that (missionparse.cpp:3709).  translate_stamp() leaves anything non-positive
// alone, so both encodings survive.
#define CKPT_SHIP_STAMPS(F)                                                                    \
	F(cmeasure_fire_stamp)                                                                     \
	F(next_manage_ets)                                                                         \
	F(subsys_disrupted_check_timestamp)                                                        \
	F(wash_timestamp)                                                                          \
	F(lightning_stamp)                                                                         \
	F(next_swarm_fire)                                                                         \
	F(next_corkscrew_fire)                                                                     \
	F(arrival_delay)                                                                           \
	F(departure_delay)

// ------------------------------------------------------------------
// ship_subsys -- see code/ship/ship.h
// ------------------------------------------------------------------

#define CKPT_SUBSYS_FLOATS(F)                                                                  \
	F(current_hits)                                                                            \
	F(max_hits)                                                                                \
	F(awacs_intensity)                                                                         \
	F(awacs_radius)                                                                            \
	F(turret_time_enemy_in_range)                                                              \
	F(turret_inaccuracy)                                                                       \
	F(optimum_range)                                                                           \
	F(favor_current_facing)                                                                    \
	F(points_to_target)                                                                        \
	F(base_rotation_rate_pct)                                                                  \
	F(gun_rotation_rate_pct)                                                                   \
	F(rof_scaler)

#define CKPT_SUBSYS_INTS(F)                                                                    \
	F(subsys_guardian_threshold)                                                               \
	F(turret_next_fire_pos)                                                                    \
	F(turret_swarm_num)

#define CKPT_SUBSYS_STAMPS(F)                                                                  \
	F(turret_next_enemy_check_stamp)                                                           \
	F(turret_next_fire_stamp)                                                                  \
	F(turret_pick_big_attack_point_timestamp)                                                  \
	F(disruption_timestamp)                                                                    \
	F(rotation_timestamp)

// ------------------------------------------------------------------
// ship_weapon -- see code/ship/ship.h
// ------------------------------------------------------------------

// Bank contents are captured per bank (see checkpoint_weapon_bank); these are the fields that
// describe the weapon system as a whole.
#define CKPT_WEAPONS_INTS(F)                                                                   \
	F(current_primary_bank)                                                                    \
	F(current_secondary_bank)                                                                  \
	F(current_tertiary_bank)                                                                   \
	F(previous_primary_bank)                                                                   \
	F(previous_secondary_bank)                                                                 \
	F(tertiary_bank_ammo)                                                                      \
	F(tertiary_bank_start_ammo)                                                                \
	F(tertiary_bank_capacity)                                                                  \
	F(remote_detonaters_active)

// rearm_time is "timestamp which indicates when bank can get new projectile" (ship.h), not a
// duration, despite the name.  The per-bank primary and secondary equivalents live in
// checkpoint::weapon_bank and are shifted where they are applied.
#define CKPT_WEAPONS_STAMPS(F)                                                                 \
	F(next_tertiary_fire_stamp)                                                                \
	F(detonate_weapon_time)                                                                    \
	F(tertiary_bank_rearm_time)

// ------------------------------------------------------------------
// wing -- see code/ship/ship.h
// ------------------------------------------------------------------

#define CKPT_WING_INTS(F)                                                                      \
	F(current_wave)                                                                            \
	F(total_arrived_count)                                                                     \
	F(current_count)                                                                           \
	F(total_destroyed)                                                                         \
	F(total_departed)                                                                          \
	F(total_vanished)                                                                          \
	F(red_alert_skipped_ships)                                                                 \
	F(arrival_distance)                                                                        \
	F(wave_delay_min)                                                                          \
	F(wave_delay_max)

// Same dual encoding as the ship versions; wave_delay_timestamp is a real TIMESTAMP and is
// handled separately in wing_state.
#define CKPT_WING_STAMPS(F)                                                                    \
	F(arrival_delay)                                                                           \
	F(departure_delay)

// ------------------------------------------------------------------
// scoring_struct, mission-scoped fields only -- see code/stats/scoring.h
// ------------------------------------------------------------------

// Per-ship-class kills (m_okKills) are indexed by ship class and so are written by class
// name separately; everything else in the mission scope is a plain counter.
#define CKPT_SCORING_INTS(F)                                                                   \
	F(m_score)                                                                                 \
	F(m_kill_count)                                                                            \
	F(m_kill_count_ok)                                                                         \
	F(m_assists)                                                                               \
	F(m_bonehead_kills)                                                                        \
	F(m_player_deaths)                                                                         \
	F(mp_shots_fired)                                                                          \
	F(mp_shots_hit)                                                                            \
	F(mp_bonehead_hits)                                                                        \
	F(ms_shots_fired)                                                                          \
	F(ms_shots_hit)                                                                            \
	F(ms_bonehead_hits)

// ------------------------------------------------------------------
// ai_info -- see code/ai/ai.h
// ------------------------------------------------------------------
//
// ai_info is around two hundred fields, and most of them are scratch that the next frame
// recomputes.  Only what actually describes what a ship was doing is captured.
//
// Deliberately absent, and worth knowing why:
//
//   - Everything path-related (path_start, path_cur, path_length, path_dir, mp_index,
//     path_create_pos and friends).  These are indices into the global path point array, which is
//     rebuilt from scratch every load, and the AI recreates its path within a frame or two of
//     needing one.  Storing them would be storing a runtime index -- exactly what the rule above
//     forbids -- and would gain nothing.
//   - The ai_* tunables copied from the AI class and from ai_profiles (ai_accuracy, ai_evasion,
//     ai_turn_time_scale, ai_profile_flags, and the rest).  All re-derived on load from the class
//     the ship comes back as.
//   - Per-frame scratch: best_dot_*, previous_dot_to_enemy, prev_accel, prev_dot_to_goal, last_dist,
//     last_speed, last_target, last_secondary_index, current_target_distance and the trend
//     counters, nearest_locked_*, next_predict_pos_time, last_aim_enemy_*.
//   - stealth_*, avoid_*, big_recover_*, big_collision_normal: recovery and pursuit state that
//     lasts a second or two and re-establishes itself.
//   - abort_rearm_timestamp, which is multiplayer only, and ai_missile_locks_firing, which is
//     rebuilt every frame.
//   - lua_ai_target, which holds a luacpp::LuaValueList and is not serialisable.  A script that
//     needs its AI target back across a restore has the checkpoint script data for it.
//
// Object references (target_objnum, goal_objnum, guard_objnum and the rest) are NOT here: an
// objnum means nothing after a reload, so they travel by ship name in ai_state and are resolved in
// a second pass once every ship exists.  ai_class is an index into Ai_classes, which comes from
// ai.tbl, so it travels by name too.

// enemy_wing and guard_wingnum index Wings[], which only the mission parse ever builds -- the same
// reasoning that lets alt_type_index and callsign_index stay as indices.  wp_list_index and
// wp_index are positions in a waypoint list, and waypoint lists likewise come only from the
// mission file.
#define CKPT_AI_INTS(F)                                                                        \
	F(mode)                                                                                    \
	F(previous_mode)                                                                           \
	F(submode)                                                                                 \
	F(previous_submode)                                                                        \
	F(submode_parm0)                                                                           \
	F(submode_parm1)                                                                           \
	F(active_goal)                                                                             \
	F(enemy_wing)                                                                              \
	F(guard_wingnum)                                                                           \
	F(wp_list_index)                                                                           \
	F(wp_index)                                                                                \
	F(wp_flags)                                                                                \
	F(waypoint_speed_cap)                                                                      \
	F(form_obj_slotnum)                                                                        \
	F(kamikaze_damage)                                                                         \
	F(danger_shield_quadrant)                                                                  \
	F(rearm_first_missile)                                                                     \
	F(rearm_first_ballistic_primary)

// These are all `fix` values holding a Missiontime, not engine timestamps, so they are restored
// verbatim -- Missiontime is itself restored, which makes them mean the same thing again.  They
// ride in the same int map as everything else; the only reason they are a separate list is to say
// out loud that they must NOT be translated.  Note resume_goal_time in particular: it reads like a
// timestamp and is not one.
#define CKPT_AI_FIXES(F)                                                                       \
	F(submode_start_time)                                                                      \
	F(resume_goal_time)                                                                        \
	F(last_attack_time)                                                                        \
	F(last_hit_time)                                                                           \
	F(last_hit_target_time)                                                                    \
	F(afterburner_stop_time)

#define CKPT_AI_FLOATS(F)                                                                      \
	F(submode_float0)                                                                          \
	F(lethality)                                                                               \
	F(aspect_locked_time)                                                                      \
	F(target_time)                                                                             \
	F(artillery_lock_time)

// Real timestamp() values.  See the all-caps rule at the top of this file.
#define CKPT_AI_STAMPS(F)                                                                      \
	F(mode_time)                                                                               \
	F(goal_check_time)                                                                         \
	F(warp_out_timestamp)                                                                      \
	F(next_rearm_request_timestamp)                                                            \
	F(rearm_release_delay)                                                                     \
	F(ignore_expire_timestamp)                                                                 \
	F(self_destruct_timestamp)                                                                 \
	F(force_warp_time)                                                                         \
	F(ok_to_target_timestamp)                                                                  \
	F(choose_enemy_timestamp)                                                                  \
	F(scan_for_enemy_timestamp)                                                                \
	F(primary_select_timestamp)                                                                \
	F(secondary_select_timestamp)                                                              \
	F(shield_manage_timestamp)                                                                 \
	F(pick_big_attack_point_timestamp)                                                         \
	F(multilock_check_timestamp)                                                               \
	F(ai_override_lat_timestamp)                                                               \
	F(ai_override_rot_timestamp)

#define CKPT_AI_VECS(F)                                                                        \
	F(goal_point)                                                                              \
	F(prev_goal_point)                                                                         \
	F(guard_vec)                                                                               \
	F(big_attack_point)                                                                        \
	F(artillery_lock_pos)

// ------------------------------------------------------------------
// player, mission-scoped fields only -- see code/playerman/player.h
// ------------------------------------------------------------------
//
// The player struct is mostly per-frame HUD bookkeeping and per-pilot career data, neither of which
// belongs here.  What does belong is the built-in message budget: how many times the player has
// been warned, praised, screamed at or asked for help this mission.  Those counters are what
// Builtin_messages[].max_count is compared against, so losing them hands the player a fresh
// allowance of every built-in message and a mission that had gone quiet starts chattering again.

#define CKPT_PLAYER_INTS(F)                                                                    \
	F(warn_count)                                                                              \
	F(praise_count)                                                                            \
	F(ask_help_count)                                                                          \
	F(scream_count)                                                                            \
	F(low_ammo_complaint_count)                                                                \
	F(praise_self_count)                                                                       \
	F(distance_warning_count)

// The matching "not before" stamps.  Without these a restore lets every throttled message fire at
// once, which is the same failure the mission events had before their timestamps were translated.
#define CKPT_PLAYER_STAMPS(F)                                                                   \
	F(check_warn_timestamp)                                                                    \
	F(allow_warn_timestamp)                                                                    \
	F(allow_praise_timestamp)                                                                  \
	F(praise_delay_timestamp)                                                                  \
	F(allow_ask_help_timestamp)                                                                \
	F(allow_scream_timestamp)                                                                  \
	F(allow_ammo_timestamp)                                                                    \
	F(praise_self_timestamp)                                                                   \
	F(request_repair_timestamp)                                                                \
	F(check_for_all_alone_msg)

// ------------------------------------------------------------------
// Training state -- see code/mission/missiontraining.h
// ------------------------------------------------------------------
//
// The context is what a training mission checks its directives against: which waypoint path the
// player is meant to be flying, which node of it they have reached, and what speed they are
// supposed to hold.  The directives themselves are mission events and come back with the events
// section, so this is only the frame around them.
//
// Training_message_method and Training_failure are here because both are set by the mission as it
// runs.  The message queue and the message currently on screen are not: both live in file statics
// in missiontraining.cpp, and they hold at most a few seconds of pending text.

#define CKPT_TRAINING_INTS(F)                                                                  \
	F(Training_message_method)                                                                 \
	F(Training_failure)                                                                        \
	F(Training_context)                                                                        \
	F(Training_context_speed_set)                                                              \
	F(Training_context_speed_min)                                                              \
	F(Training_context_speed_max)                                                              \
	F(Training_context_waypoint_path)                                                          \
	F(Training_context_goal_waypoint)                                                          \
	F(Training_context_at_waypoint)

// ------------------------------------------------------------------
// control_info, as used for ai_info::ai_override_ci -- see code/physics/physics.h
// ------------------------------------------------------------------

// Only the six axes and the cruise percentage: a SEXP or script maneuver override sets those and
// nothing else, and the firing counts belong to the player's controls rather than to the override.
// Without these an override with Lateral_never_expire set comes back still flagged but with the
// ship told to hold still.
#define CKPT_AI_OVERRIDE_FLOATS(F)                                                             \
	F(pitch)                                                                                   \
	F(bank)                                                                                    \
	F(heading)                                                                                 \
	F(forward)                                                                                 \
	F(sideways)                                                                                \
	F(vertical)                                                                                \
	F(forward_cruise_percent)

#endif // _CHECKPOINTFIELDS_H
