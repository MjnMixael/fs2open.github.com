/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/

#include "weapon/weapon.h"


#ifndef _SHIPHIT_H
#define _SHIPHIT_H

struct vec3d;
class ship;
class ship_subsys;
class object;

#define NO_SPARKS			0
#define CREATE_SPARKS	1

#define MISS_SHIELDS		-1

constexpr float DEATHROLL_ROTVEL_CAP = 6.3f;    // maximum added deathroll rotvel in rad/sec (6.3 rad/sec is about 1 rev/sec)

// =====================   NOTE!! =========================
// To apply damage to a ship, call either ship_apply_local_damage or ship_apply_global_damage.
// These replace the old calls to ship_hit and ship_do_damage...
// These functions do nothing to the ship's physics; that is the responsibility
// of whoever is calling these functions.  These functions are strictly
// for damaging ship's hulls, shields, and subsystems.  Nothing more.

// function to destroy a subsystem.  Called internally and from multiplayer messaging code
extern void do_subobj_destroyed_stuff( ship *ship_p, ship_subsys *subsys, const vec3d *hitpos, bool no_explosion = false );

std::pair<std::optional<ConditionData>, float> do_subobj_hit_stuff(object *ship_obj, const object *other_obj, const vec3d *hitpos, int submodel_num, float damage, bool *hull_should_apply_armor, float hit_dot = 1.f, bool shield_hit = false);

// Goober5000
// (it might be possible to make `target` const, but that would set off another const-cascade)
extern void ship_apply_tag(ship *ship_p, int tag_level, float tag_time, object *target, const vec3d *start, int ssm_index, int ssm_team);

// This gets called to apply damage when something hits a particular point on a ship.
// This assumes that whoever called this knows if the shield got hit or not.
// hitpos is in world coordinates.
// if quadrant is not -1, then that part of the shield takes damage properly.
// (it might be possible to make `other_obj` const, but that would set off another const-cascade)
void ship_apply_local_damage(object *ship_obj, object *other_obj, const vec3d *hitpos, float damage, int damage_type_idx, int quadrant, bool create_spark=true, int submodel_num=-1, const vec3d *hit_normal=nullptr, float hit_dot = 1.f, const vec3d* local_hitpos = nullptr);

// This gets called to apply damage when a damaging force hits a ship, but at no 
// point in particular.  Like from a shockwave.   This routine will see if the
// shield got hit and if so, apply damage to it.
// You can pass force_center==NULL if you the damage doesn't come from anywhere,
// like for debug keys to damage an object or something.  It will 
// assume damage is non-directional and will apply it correctly.   
void ship_apply_global_damage(object *ship_obj, object *other_obj, const vec3d *force_center, float damage, int damage_type_idx);

// like above, but does not apply damage to shields
void ship_apply_wash_damage(object *ship_obj, object *other_obj, float damage);

// next routine needed for multiplayer
void ship_hit_kill(object *ship_obj, object *other_obj, const vec3d *hitpos, float percent_killed, bool self_destruct = false, bool always_log_other_obj = false);

void ship_self_destruct( object *objp );

// Call this instead of physics_apply_whack directly to 
// deal with two docked ships properly.
void ship_apply_whack(const vec3d *force, const vec3d *hit_pos, object *objp);

// externed for code in missionparse to create sparks on a ship with < 100% hull integrity.
void ship_hit_sparks_no_rotate(object *ship_obj, vec3d *hitpos);

// externed so that ships which self destruct have the proper things done to them in multiplayer
void ship_generic_kill_stuff( object *objp, float percent_killed );

// find the max number of sparks allowed for ship
// limited for fighter by hull % others by radius.
int get_max_sparks(const object* ship_obj);

// player pain
void ship_hit_pain(float damage, int quadrant);


#endif //_SHIPHIT_H
