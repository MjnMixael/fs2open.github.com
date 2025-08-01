/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/




#include <algorithm>

#include "asteroid/asteroid.h"
#include "debris/debris.h"
#include "fireball/fireballs.h"
#include "freespace.h"
#include "gamesequence/gamesequence.h"
#include "gamesnd/eventmusic.h"
#include "gamesnd/gamesnd.h"
#include "globalincs/linklist.h"
#include "hud/hud.h"
#include "hud/hudartillery.h"
#include "hud/hudets.h"
#include "hud/hudmessage.h"
#include "hud/hudtarget.h"
#include "iff_defs/iff_defs.h"
#include "io/joy_ff.h"
#include "io/timer.h"
#include "mission/missionlog.h"
#include "mod_table/mod_table.h"
#include "network/multi.h"
#include "network/multi_pmsg.h"
#include "network/multi_respawn.h"
#include "network/multimsgs.h"
#include "network/multiutil.h"
#include "object/object.h"
#include "object/objectdock.h"
#include "object/objectshield.h"
#include "object/objectsnd.h"
#include "parse/parselo.h"
#include "scripting/hook_api.h"
#include "scripting/global_hooks.h"
#include "scripting/api/objs/subsystem.h"
#include "scripting/api/objs/vecmath.h"
#include "playerman/player.h"
#include "popup/popup.h"
#include "render/3d.h"
#include "ship/ship.h"
#include "ship/shipfx.h"
#include "ship/shiphit.h"
#include "weapon/beam.h"
#include "weapon/emp.h"
#include "weapon/shockwave.h"
#include "weapon/weapon.h"
#include "tracing/Monitor.h"

//#pragma optimize("", off)
//#pragma auto_inline(off)

typedef struct spark_pair {
	int index1, index2;
	float dist;
} spark_pair;

#define MAX_SPARK_PAIRS		((MAX_SHIP_SPARKS * MAX_SHIP_SPARKS - MAX_SHIP_SPARKS) / 2)

#define	BIG_SHIP_MIN_RADIUS	80.0f	//	ship radius above which death rolls can't be shortened by excessive damage

vec3d	Dead_camera_pos;
vec3d	Original_vec_to_deader;

static bool global_damage = false;

const std::shared_ptr<scripting::Hook<>> OnPainFlashHook = scripting::Hook<>::Factory(
	"On Pain Flash", "Called when a pain flash is displayed.",
	{ 		
		{"Pain_Type", "number", "The type of pain flash displayed: shield = 0 and hull = 1."},
	});


//WMC - Camera rough draft stuff
/*
camid dead_get_camera()
{
	static camid dead_camera;
	if(!dead_camera.isValid())
		dead_camera = cam_create("Dead camera");

	return dead_camera;
}
*/

static bool is_subsys_destroyed(ship *shipp, int submodel)
{
	ship_subsys *subsys;

	if (submodel == -1) {
		return false;
	}

	for ( subsys=GET_FIRST(&shipp->subsys_list); subsys != END_OF_LIST(&shipp->subsys_list); subsys = GET_NEXT(subsys) ) {
		if (subsys->system_info->subobj_num == submodel) {
			if (subsys->current_hits > 0.0f) {
				return false;
			} else {
				return true;
			}
		}
	}

	return false;
}

// do_subobj_destroyed_stuff is called when a subobject for a ship is killed.  Separated out
// to separate function on 10/15/97 by MWA for easy multiplayer access.  It does all of the
// cool things like blowing off the model (if applicable, writing the logs, etc)
// NOTE: if this function is used with ship_recalc_subsys_strength, it MUST be called first. If
// a child subsystem needs to be destroyed, the strength calculation needs to take it into account.
void do_subobj_destroyed_stuff( ship *ship_p, ship_subsys *subsys, const vec3d* hitpos, bool no_explosion )
{
	ship_info *sip;
	object *ship_objp;
	ship_subsys *ssp;
	model_subsystem *psub;
	vec3d	g_subobj_pos;
	int type, i, log_index;

	// get some local variables
	sip = &Ship_info[ship_p->ship_info_index];
	ship_objp = &Objects[ship_p->objnum];
	psub = subsys->system_info;
	type = psub->type;
	get_subsystem_world_pos(ship_objp, subsys, &g_subobj_pos);

	// see if this subsystem is on a submodel
	if (psub->subobj_num >= 0) {
		polymodel *pm = model_get(sip->model_num);

		// see if there are any subsystems which have this submodel as a parent
		for (ssp = GET_FIRST(&ship_p->subsys_list); ssp != END_OF_LIST(&ship_p->subsys_list); ssp = GET_NEXT(ssp)) {
			// is it another subsys which has a submodel?
			if (ssp != subsys && ssp->system_info->subobj_num >= 0) {
				// is this other submodel a child of the one being destroyed?
				if (pm->submodel[ssp->system_info->subobj_num].parent == psub->subobj_num) {
					// is it not yet destroyed?  (this is a valid check because we already know there is a submodel)
					if (!ssp->submodel_instance_1->blown_off) {
						// then destroy it first
						ssp->current_hits = 0;
						do_subobj_destroyed_stuff(ship_p, ssp, nullptr, no_explosion);
					}
				}
			}
		}
	}

	// create fireballs when subsys destroy for large ships.
	if (!(subsys->flags[Ship::Subsystem_Flags::Vanished, Ship::Subsystem_Flags::No_disappear]) && !no_explosion) {
		if (ship_objp->radius > 100.0f) {
			// number of fireballs determined by radius of subsys
			int num_fireballs;
			if ( psub->radius < 3 ) {
				num_fireballs = 1;
			} else {
				 num_fireballs = 5;
			}

			vec3d temp_vec, center_to_subsys, rand_vec;
			vm_vec_sub(&center_to_subsys, &g_subobj_pos, &ship_objp->pos);
			for (i=0; i<num_fireballs; i++) {
				if (i==0) {
					// make first fireball at hitpos
					if (hitpos) {
						temp_vec = *hitpos;
					} else {
						temp_vec = g_subobj_pos;
					}
				} else {
					// make other fireballs at random positions, but try to keep on the surface
					vm_vec_rand_vec_quick(&rand_vec);
					float dot = vm_vec_dot(&center_to_subsys, &rand_vec);
					vm_vec_scale_add2(&rand_vec, &center_to_subsys, -dot/vm_vec_mag_squared(&center_to_subsys));
					vm_vec_scale_add(&temp_vec, &g_subobj_pos, &rand_vec, 0.5f*psub->radius);
				}

				// scale fireball size according to size of subsystem, but not less than 10
				float fireball_rad = psub->radius * 0.2f;
				if (fireball_rad < 10) {
					fireball_rad = 10.0f;
				}

				vec3d fb_vel;
				vm_vec_cross(&fb_vel, &ship_objp->phys_info.rotvel, &center_to_subsys);
				vm_vec_add2(&fb_vel, &ship_objp->phys_info.vel);

				int fireball_type = fireball_ship_explosion_type(sip);
				if(fireball_type < 0) {
					fireball_type = FIREBALL_EXPLOSION_MEDIUM;
				}
				fireball_create( &temp_vec, fireball_type, FIREBALL_MEDIUM_EXPLOSION, ship_p->objnum, fireball_rad, false, &fb_vel );
			}
		}
	}

	if ( MULTIPLAYER_MASTER ) {
		int index;

		index = ship_get_subsys_index(subsys);
		
		vec3d hit;
		if (hitpos) {
			hit = *hitpos;
		} else {
			hit = g_subobj_pos;
		}
		send_subsystem_destroyed_packet( ship_p, index, hit );
	}

	// next do a quick sanity check on the current hits that we are keeping for the generic subsystems
	// I think that there might be rounding problems with the floats.  This code keeps us safe.
	if ( ship_p->subsys_info[type].type_count == 1 ) {
		ship_p->subsys_info[type].aggregate_current_hits = 0.0f;
	} else {
		float hits = 0.0f;

		for ( ssp=GET_FIRST(&ship_p->subsys_list); ssp != END_OF_LIST(&ship_p->subsys_list); ssp = GET_NEXT(ssp) ) {
			// type matches?
			if ( (ssp->system_info->type == type) && !(ssp->flags[Ship::Subsystem_Flags::No_aggregate]) ) {
				hits += ssp->current_hits;
			}
		}
		ship_p->subsys_info[type].aggregate_current_hits = hits;
	}

	// store an event in the event log.  Also, determine if all turrets or all
	// engines have been destroyed (if the subsystem is a turret or engine).
	// put a disabled or disarmed entry in the log if this is the case
	// 
	// MWA -- 1/8/98  A problem was found when trying to determine (via sexpression) when some subsystems
	// were destroyed.  The bottom line is that is the psub->name and psub->subobj_name are different,
	// then direct detection doesn't work.  (This scenario happens mainly with turrets and probably with
	// engines).  So, my solution is to encode the ship_info index, and the subsystem index into one
	// integer, and pass that as the "index" parameter to add_entry.  We'll use that information to
	// print out the info in the mission log.
	Assert( ship_p->ship_info_index < 65535 );

	// get the "index" of this subsystem in the ship info structure.
	int subsystem_index;
	for (subsystem_index = 0; subsystem_index < sip->n_subsystems; ++subsystem_index ) {
		if ( &(sip->subsystems[subsystem_index]) == psub )
			break;
	}
	Assert( subsystem_index < sip->n_subsystems );
	Assert( subsystem_index < 65535 );
	log_index = ((ship_p->ship_info_index << 16) & 0xffff0000) | (subsystem_index & 0xffff);

	// Don't log, display info, or play sounds about the activation subsytem
	// FUBAR/Goober5000 - or about vanishing subsystems, per precedent with ship-vanish
	int notify = (psub->type != SUBSYSTEM_ACTIVATION) && !(subsys->flags[Ship::Subsystem_Flags::Vanished]);

	if (notify) 
	{
		mission_log_add_entry(LOG_SHIP_SUBSYS_DESTROYED, ship_p->ship_name, psub->subobj_name, log_index );
		if ( ship_objp == Player_obj )
		{
			if (!no_explosion) {
				snd_play( gamesnd_get_game_sound(GameSounds::SUBSYS_DIE_1), 0.0f );
			}
			if (strlen(psub->alt_dmg_sub_name))
				HUD_printf(XSTR( "Your %s subsystem has been destroyed", 499), psub->alt_dmg_sub_name);
			else {
				char r_name[NAME_LENGTH];
				strcpy_s(r_name, ship_subsys_get_name(subsys));
				for (i = 0; r_name[i] > 0; i++) {
					if (r_name[i] == '|')
						r_name[i] = ' ';
				}
				HUD_printf(XSTR( "Your %s subsystem has been destroyed", 499), r_name );
			}
		}
	}

	if ( psub->type == SUBSYSTEM_TURRET ) {
		if ( ship_p->subsys_info[type].aggregate_current_hits <= 0.0f ) {
			//	Don't create "disarmed" event for small ships.
			if (!(sip->is_small_ship())) {
				mission_log_add_entry(LOG_SHIP_DISARMED, ship_p->ship_name, NULL );
				// ship_p->flags |= SF_DISARMED;
			}
		}
	} else if (psub->type == SUBSYSTEM_ENGINE ) {
		// when an engine is destroyed, we must change the max velocity of the ship
		// to be some fraction of its normal maximum value

		if ( ship_p->subsys_info[type].aggregate_current_hits <= 0.0f ) {
			mission_log_add_entry(LOG_SHIP_DISABLED, ship_p->ship_name, NULL );
			ship_p->flags.set(Ship::Ship_Flags::Disabled);				// add the disabled flag
		}
	}

	// call a scripting hook for the subsystem (regardless of whether it's added to the mission log)
	if (scripting::hooks::OnSubsystemDestroyed->isActive()) {
		scripting::hooks::OnSubsystemDestroyed->run(scripting::hooks::SubsystemDeathConditions{ ship_p, subsys },
			scripting::hook_param_list(
				scripting::hook_param("Ship", 'o', ship_objp),
				scripting::hook_param("Subsystem", 'o', scripting::api::l_Subsystem.Set(scripting::api::ship_subsys_h(ship_objp, subsys)))
			));
	}

	if (!(subsys->flags[Ship::Subsystem_Flags::No_disappear])) {
		if (psub->subobj_num > -1) {
			shipfx_blow_off_subsystem(ship_objp, ship_p, subsys, &g_subobj_pos, no_explosion);
			subsys->submodel_instance_1->blown_off = true;
		}

		if ((psub->subobj_num != psub->turret_gun_sobj) && (psub->turret_gun_sobj >= 0)) {
			subsys->submodel_instance_2->blown_off = true;
		}
	}

	if (notify && !no_explosion) {
		// play sound effect when subsys gets blown up
		gamesnd_id sound_index;
		if (ship_has_sound(ship_objp, GameSounds::SUBSYS_EXPLODE)) {
			sound_index = ship_get_sound(ship_objp, GameSounds::SUBSYS_EXPLODE);
		} else {
			if ( sip->is_huge_ship() ) {
				sound_index = GameSounds::CAPSHIP_SUBSYS_EXPLODE;
			} else if ( sip->is_big_ship() ) {
				sound_index = GameSounds::SUBSYS_EXPLODE;
			}
		}
		if ( sound_index.isValid() ) {
			snd_play_3d( gamesnd_get_game_sound(sound_index), &g_subobj_pos, &View_position );
		}
	}

	// make the shipsounds work as they should...
	if(subsys->subsys_snd_flags[Ship::Subsys_Sound_Flags::Alive])
	{
		obj_snd_delete_type(ship_p->objnum, subsys->system_info->alive_snd, subsys);
        subsys->subsys_snd_flags.remove(Ship::Subsys_Sound_Flags::Alive);
	}
	if(subsys->subsys_snd_flags[Ship::Subsys_Sound_Flags::Turret_rotation])
	{
		obj_snd_delete_type(ship_p->objnum, subsys->system_info->turret_base_rotation_snd, subsys);
		obj_snd_delete_type(ship_p->objnum, subsys->system_info->turret_gun_rotation_snd, subsys);
		subsys->subsys_snd_flags.remove(Ship::Subsys_Sound_Flags::Turret_rotation);
	}
	if(subsys->subsys_snd_flags[Ship::Subsys_Sound_Flags::Rotate])
	{
		obj_snd_delete_type(ship_p->objnum, subsys->system_info->rotation_snd, subsys);
		subsys->subsys_snd_flags.remove(Ship::Subsys_Sound_Flags::Rotate);
	}
	if((subsys->system_info->dead_snd.isValid()) && !(subsys->subsys_snd_flags[Ship::Subsys_Sound_Flags::Dead]))
	{
		obj_snd_assign(ship_p->objnum, subsys->system_info->dead_snd, &subsys->system_info->pnt, OS_SUBSYS_DEAD, subsys);
		subsys->subsys_snd_flags.remove(Ship::Subsys_Sound_Flags::Dead);
	}
}

// Return weapon type that is associated with damaging_objp
// input:	damaging_objp		=>	object pointer responsible for damage
//	exit:		-1		=>	no weapon type is associated with damage object
//				>=0	=>	weapon type associated with damage object
static int shiphit_get_damage_weapon(const object *damaging_objp)
{
	int weapon_info_index = -1;

	if ( damaging_objp ) {
		switch(damaging_objp->type) {
		case OBJ_WEAPON:
			weapon_info_index = Weapons[damaging_objp->instance].weapon_info_index;
			break;
		case OBJ_SHOCKWAVE:
			weapon_info_index = shockwave_get_weapon_index(damaging_objp->instance);
			break;
		case OBJ_BEAM:
			weapon_info_index = beam_get_weapon_info_index(damaging_objp);
			break;
		default:
			weapon_info_index = -1;
			break;
		}
	}

	return weapon_info_index;
}

//	Return range at which this object can apply damage.
//	Based on object type and subsystem type.
static float subsys_get_range(const object *other_obj, const ship_subsys *subsys)
{
	float	range;

	Assert(subsys);	// Goober5000

	if ((other_obj) && (other_obj->type == OBJ_SHOCKWAVE)) {	// Goober5000 - check for NULL when via sexp
		range = shockwave_get_max_radius(other_obj->instance) * 0.75f;	//	Shockwaves were too lethal to subsystems.
	} else if ( subsys->system_info->type == SUBSYSTEM_TURRET ) {
		range = subsys->system_info->radius*3;
	} else {
		range = subsys->system_info->radius*2;
	}

	return range;
}

#define	MAX_SUBSYS_LIST	200 //DTP MAX SUBSYS LIST BUMPED FROM 32 to 200, ahmm 32???

typedef struct {
	float	dist;
	float	range;
	ship_subsys	*ptr;
} sublist;

// fundamentally similar to do_subobj_hit_stuff, but without many checks inherent to damaging instead of healing
// most notably this does NOT return "remaining healing" (healing always carries), this is will NOT subtract from hull healing

std::pair<std::optional<ConditionData>, float> do_subobj_heal_stuff(const object* ship_objp, const object* other_obj, const vec3d* hitpos, int submodel_num, float healing)
{
	vec3d			g_subobj_pos;
	float			healing_left;
	int				weapon_info_index;
	ship* ship_p;
	sublist			subsys_list[MAX_SUBSYS_LIST];
	int				subsys_hit_first = -1; // the subsys which should be hit first and take most of the healing; index into subsys_list
	vec3d			hitpos2;

	Assertion(ship_objp, "do_subobj_heal_stuff wasn't given an object to heal!");
	Assertion(hitpos, "do_subobj_heal_stuff wasn't given a hit position!");	
	Assertion(other_obj, "do_subobj_heal_stuff wasn't given a healing object! (weapon/beam/shockwave)");

	ship_p = &Ships[ship_objp->instance];

	std::optional<ConditionData> subsys_impact = std::nullopt;

	if (other_obj->type == OBJ_SHOCKWAVE)
	{
		healing_left = shockwave_get_damage(other_obj->instance) / 2.0f;
		hitpos2 = other_obj->pos;
	}
	else {
		healing_left = healing;
		hitpos2 = *hitpos;
	}

	//	First, create a list of the N subsystems within range.
	//	Then, one at a time, process them in order.
	int	count = 0;
	for (auto subsys = GET_FIRST(&ship_p->subsys_list); subsys != END_OF_LIST(&ship_p->subsys_list); subsys = GET_NEXT(subsys))
	{
		model_subsystem* mss = subsys->system_info;

		if (subsys->current_hits > 0.0f) {
			float	dist, range;

			if (Fixed_turret_collisions && submodel_num != -1 && submodel_num == mss->turret_gun_sobj) {
				// Special case:
				// if the subsystem is a turret and the hit submodel is its barrel,
				// get the distance between the hit and the turret barrel center
				auto pmi = model_get_instance(ship_p->model_instance_num);
				auto pm = model_get(pmi->model_num);
				model_instance_local_to_global_point(&g_subobj_pos, &vmd_zero_vector, pm, pmi, submodel_num, &ship_objp->orient, &ship_objp->pos);
				dist = vm_vec_dist_quick(&hitpos2, &g_subobj_pos);

				// Healing attenuation range of barrel radius * 2 makes full healing
				// be taken regardless of where the barrel is hit
				range = submodel_get_radius(Ship_info[ship_p->ship_info_index].model_num, submodel_num) * 2;
			}
			else {
				// Default behavior:
				// get the distance between the hit and the subsystem center
				get_subsystem_world_pos(ship_objp, subsys, &g_subobj_pos);
				dist = vm_vec_dist_quick(&hitpos2, &g_subobj_pos);

				range = subsys_get_range(other_obj, subsys);
			}

			if (dist < range) {
				if (Damage_impacted_subsystem_first && submodel_num != -1 && (submodel_num == mss->subobj_num || submodel_num == mss->turret_gun_sobj)) {
					// If the hit impacted this subsystem's submodel, then make sure this subsys
					// gets healed first, even if another subsystem is closer to the hit location
					subsys_hit_first = count;
				}

				if (mss->flags[Model::Subsystem_Flags::Collide_submodel]) {
					if (submodel_num != -1 && submodel_num != mss->subobj_num && submodel_num != mss->turret_gun_sobj) {
						// If this subsystem only wants to take healing when its submodel receives
						// a direct hit and the current hit did not do so, skip it.
						continue;
					}
				}

				subsys_list[count].dist = dist;
				subsys_list[count].range = range;
				subsys_list[count].ptr = subsys;
				count++;

				if (count >= MAX_SUBSYS_LIST) {
					break;
				}
			}
		}
	}

	int dmg_type_idx = -1;
	int parent_armor_flags = 0;

	if (ship_p->armor_type_idx > -1)
		parent_armor_flags = Armor_types[ship_p->armor_type_idx].flags;

	if (other_obj)
	{
		if (other_obj->type == OBJ_SHOCKWAVE)
			dmg_type_idx = shockwave_get_damage_type_idx(other_obj->instance);
		else if (other_obj->type == OBJ_WEAPON) 
			dmg_type_idx = Weapon_info[Weapons[other_obj->instance].weapon_info_index].damage_type_idx;
		else if (other_obj->type == OBJ_BEAM) 
			dmg_type_idx = Weapon_info[beam_get_weapon_info_index(other_obj)].damage_type_idx;
	}

	//	Now scan the sorted list of subsystems in range.
	//	Apply healing to the nearest one first (exception: subsys_hit_first),
	//	subtracting off healing as we go.
	int	i, j;
	for (j = 0; j < count; j++)
	{
		float	dist, range;
		ship_subsys* subsystem;

		int	min_index = -1;
		{
			float	min_dist = 9999999.9f;

			// find the closest subsystem
			for (i=0; i<count; i++) {
				if (subsys_list[i].dist < min_dist) {
					min_dist = subsys_list[i].dist;
					min_index = i;
				}
			}
			Assert(min_index != -1);
		}

		// if the closest system does *not* override a submodel impact, and we have a submodel impact, use it instead
		if (Damage_impacted_subsystem_first && subsys_hit_first >= 0 && !subsys_list[min_index].ptr->system_info->flags[Model::Subsystem_Flags::Override_submodel_impact]) {
			min_index = subsys_hit_first;
			subsys_hit_first = -1;	// prevent the submodel impact from taking priority on the next loop iteration
		}

		subsystem = subsys_list[min_index].ptr;
		range = subsys_list[min_index].range;
		dist = subsys_list[min_index].dist;
		subsys_list[min_index].dist = 9999999.9f;	//	Make sure we don't use this one again.

		Assert(range > 0.0f);	// Goober5000 - avoid div-0 below

		// Make sure this subsystem still has hitpoints.
		if (subsystem->current_hits <= 0.0f) {
			continue;
		}

		// only do this for the closest affected subsystem
		if ((j == 0) && (!(parent_armor_flags & SAF_IGNORE_SS_ARMOR))) {
			if (subsystem->armor_type_idx > -1)
			{
				healing_left = Armor_types[subsystem->armor_type_idx].GetDamage(healing_left, dmg_type_idx, 1.0f, other_obj->type == OBJ_BEAM);
			}
		}

		// scale subsystem healing if appropriate
		float ss_factor = 1.0f;
		float hull_factor = 1.0f;
		weapon_info_index = shiphit_get_damage_weapon(other_obj);
		if ((weapon_info_index >= 0) && ((other_obj->type == OBJ_WEAPON) ||
			(Beams_use_damage_factors && (other_obj->type == OBJ_BEAM)))) {
			ss_factor = Weapon_info[weapon_info_index].subsystem_factor;
			hull_factor = Weapon_info[weapon_info_index].armor_factor;
		}

		float heal_to_apply = 0.0f;
		if (dist < range / 2.0f) {
			if (subsystem->flags[Ship::Subsystem_Flags::Damage_as_hull])
				heal_to_apply = healing_left * hull_factor;
			else
				heal_to_apply = healing_left * ss_factor;
		}
		else if (dist < range) {
			if (subsystem->flags[Ship::Subsystem_Flags::Damage_as_hull])
				heal_to_apply = healing_left * hull_factor * (1.0f - dist / range);
			else
				heal_to_apply = healing_left * ss_factor * (1.0f - dist / range);
		}

		// if we're not in CLIENT_NODAMAGE multiplayer mode (which is a the NEW way of doing things)
		if ((heal_to_apply > 0.1f) && !(MULTIPLAYER_CLIENT))
		{
			healing_left -= (heal_to_apply);

			//Apply armor to healing
			if (subsystem->armor_type_idx >= 0)
				// Nuke: this will finally factor it in to heal_to_apply and i wont need to factor it in anywhere after this
				heal_to_apply = Armor_types[subsystem->armor_type_idx].GetDamage(heal_to_apply, dmg_type_idx, 1.0f, other_obj->type == OBJ_BEAM);

			if (j == 0) {
				subsys_impact = ConditionData {
					ImpactCondition(subsystem->armor_type_idx),
					HitType::SUBSYS,
					heal_to_apply,
					subsystem->current_hits,
					subsystem->max_hits,
				};
			}

			subsystem->current_hits += heal_to_apply;

			float* agg_hits = &ship_p->subsys_info[subsystem->system_info->type].aggregate_current_hits;
			float agg_max_hits = ship_p->subsys_info[subsystem->system_info->type].aggregate_max_hits;
			if (!(subsystem->flags[Ship::Subsystem_Flags::No_aggregate])) {
				*agg_hits += heal_to_apply;
			}

			if (subsystem->current_hits > subsystem->max_hits) {
				healing_left += subsystem->current_hits - subsystem->max_hits;
				if (!(subsystem->flags[Ship::Subsystem_Flags::No_aggregate])) {
					*agg_hits += subsystem->current_hits - subsystem->max_hits;
				}
				subsystem->current_hits = subsystem->max_hits;					
			}
			
			if (*agg_hits > agg_max_hits)
				*agg_hits = agg_max_hits;


			if (healing_left <= 0)  // no more healing to distribute, so stop checking
				break;
		}
	}
	return std::make_pair(subsys_impact, healing);
}

// do_subobj_hit_stuff() is called when a collision is detected between a ship and something
// else.  This is where we see if any sub-objects on the ship should take damage.
//
//	Depending on where the collision occurs, the sub-system and surrounding hull will take 
// different amounts of damage.  The amount of damage a sub-object takes depending on how
// close the colliding object is to the center of the sub-object.  The remaining hull damage
// will be returned to the caller via the damage parameter.
//
//
// 0   -> 0.5 radius   : 100% subobject    0%  hull
// 0.5 -> 1.0 radius   :  50% subobject   50%  hull
// 1.0 -> 2.0 radius   :  25% subobject   75%  hull
//     >  2.0 radius   :   0% subobject  100%  hull
//
//
// The weapon damage is not neccesarily distributed evently between sub-systems when more than
// one sub-system is to take damage.  Whenever damage is to be assigned to a sub-system, the above
// percentages are used.  So, if more than one sub-object is taking damage, the second sub-system
// to be assigned damage will take less damage.  Eg. weapon hits in the 25% damage range of two
// subsytems, and the weapon damage is 12.  First subsystem takes 3 points damage.  Second subsystem
// will take 0.25*9 = 2.25 damage.  Should be close enough for most cases, and hull would receive 
// 0.75 * 9 = 6.75 damage.
//
//	Used to use the following constants, but now damage is linearly scaled up to 2x the subsystem
//	radius.  Same damage applied as defined by constants below.
//
//	Returns unapplied damage, which will probably be applied to the hull.
//
// Shockwave damage is handled here.  If other_obj->type == OBJ_SHOCKWAVE, it's a shockwave.
// apply the same damage to all subsystems.
//	Note: A negative damage number means to destroy the corresponding subsystem.  For example, call with -SUBSYSTEM_ENGINE to destroy engine.
//
//WMC - hull_should_apply armor means that the initial subsystem had no armor, so the hull should apply armor instead.

std::pair<std::optional<ConditionData>, float> do_subobj_hit_stuff(object *ship_objp, const object *other_obj, const vec3d *hitpos, int submodel_num, float damage, bool *hull_should_apply_armor, float hit_dot, bool shield_hit)
{
	vec3d			g_subobj_pos;
	float				damage_left, damage_if_hull;
	int				weapon_info_index;
	ship				*ship_p;
	sublist			subsys_list[MAX_SUBSYS_LIST];
	int				subsys_hit_first = -1; // the subsys which should be hit first and take most of the damage; index into subsys_list
	vec3d			hitpos2;
	float			ss_dif_scale = 1.0f; // Nuke: Set a base dificulty scale for compatibility
	
	const bool other_obj_is_weapon = other_obj && other_obj->type == OBJ_WEAPON;
	const bool other_obj_is_shockwave = other_obj && other_obj->type == OBJ_SHOCKWAVE;
	const bool other_obj_is_beam = other_obj && other_obj->type == OBJ_BEAM;

	//WMC - first, set this to damage if it isn't NULL, in case we want to return with no damage to subsystems
	if(hull_should_apply_armor != NULL) {
		*hull_should_apply_armor = true;
	}

	Assert(ship_objp);	// Goober5000 (but other_obj might be NULL via sexp)
	Assert(hitpos);		// Goober5000

	ship_p = &Ships[ship_objp->instance];

	std::optional<ConditionData> subsys_impact = std::nullopt;

	//	Don't damage player subsystems in a training mission.
	if ( The_mission.game_type & MISSION_TYPE_TRAINING ) {
		if (ship_objp == Player_obj){
			return std::make_pair(subsys_impact, damage);
		}
	}

	//	Shockwave damage is applied like weapon damage.  It gets consumed.
	if (other_obj_is_shockwave)
	{
		//	MK, 9/2/99.  Shockwaves do zero subsystem damage on small ships.
		// Goober5000 - added back in via flag
		if ((Ship_info[ship_p->ship_info_index].is_small_ship()) && !(The_mission.ai_profile->flags[AI::Profile_Flags::Shockwaves_damage_small_ship_subsystems]))
			return std::make_pair(subsys_impact, damage);
		else {
			damage_left = shockwave_get_damage(other_obj->instance) / 4.0f;
			damage_if_hull = damage_left;
		}
		hitpos2 = other_obj->pos;
	} else {
		damage_left = damage;
		damage_if_hull = damage;
		hitpos2 = *hitpos;
	}

	// scale subsystem damage if appropriate
	weapon_info_index = shiphit_get_damage_weapon(other_obj);	// Goober5000 - a NULL other_obj returns -1
	if ((weapon_info_index >= 0) && other_obj_is_weapon) {
		weapon* wp = &Weapons[other_obj->instance];
		weapon_info* wip = &Weapon_info[weapon_info_index];

		if ( wip->wi_flags[Weapon::Info_Flags::Training] ) {
			return std::make_pair(subsys_impact, damage_left);
		}

		damage_left *= wip->subsystem_factor;
		damage_left *= wip->weapon_hit_curves.get_output(weapon_info::WeaponHitCurveOutputs::SUBSYS_DAMAGE_MULT, std::forward_as_tuple(*wp, *ship_objp, hit_dot), &wp->modular_curves_instance);
		damage_if_hull *= wip->armor_factor;
		damage_if_hull *= wip->weapon_hit_curves.get_output(weapon_info::WeaponHitCurveOutputs::HULL_DAMAGE_MULT, std::forward_as_tuple(*wp, *ship_objp, hit_dot), &wp->modular_curves_instance);
	} else if ((weapon_info_index >= 0) && other_obj_is_beam) {
		weapon_info* wip = &Weapon_info[weapon_info_index];

		if (Beams_use_damage_factors) {
			if ( wip->wi_flags[Weapon::Info_Flags::Training] ) {
				return std::make_pair(subsys_impact, damage_left);
			}
			damage_left *= wip->subsystem_factor;
			damage_if_hull *= wip->armor_factor;
		}

		beam* b = &Beams[other_obj->instance];
		damage_left *= wip->beam_hit_curves.get_output(weapon_info::BeamHitCurveOutputs::SUBSYS_DAMAGE_MULT, std::forward_as_tuple(*b, *other_obj), &b->modular_curves_instance);
		damage_if_hull *= wip->beam_hit_curves.get_output(weapon_info::BeamHitCurveOutputs::HULL_DAMAGE_MULT, std::forward_as_tuple(*b, *other_obj), &b->modular_curves_instance);
	}


#ifndef NDEBUG
	float hitpos_dist = vm_vec_dist( hitpos, &ship_objp->pos );
	if ( hitpos_dist > ship_objp->radius * 2.0f )	{
		mprintf(( "BOGUS HITPOS PASSED TO DO_SUBOBJ_HIT_STUFF (%.1f > %.1f)!\nInvestigate ship %s (%s), a hit was registered on this ship outside this ship's radius.\n", hitpos_dist, ship_objp->radius * 2.0f, ship_p->ship_name, Ship_info[ship_p->ship_info_index].name ));
		// Get John ASAP!!!!  Someone passed a local coordinate instead of world for hitpos probably.
	}
#endif

	if (!global_damage) {
		auto subsys = ship_get_subsys_for_submodel(ship_p, submodel_num);

		if ( !(Ship_info[ship_p->ship_info_index].flags[Ship::Info_Flags::No_impact_debris]) && 
			( subsys == nullptr || !(subsys->system_info->flags[Model::Subsystem_Flags::No_impact_debris]) ) ) {
			create_generic_debris(ship_objp, hitpos, 1.0f, 5.0f, 1.0f, false);
		}
	}

	polymodel_instance *pmi = nullptr;
	polymodel *pm = nullptr;

	//	First, create a list of the N subsystems within range.
	//	Then, one at a time, process them in order.
	int	count = 0;
	for ( auto subsys=GET_FIRST(&ship_p->subsys_list); subsys != END_OF_LIST(&ship_p->subsys_list); subsys = GET_NEXT(subsys) )
	{
		model_subsystem *mss = subsys->system_info;

		//Deal with cheat correctly. If damage is the negative of the subsystem type, then we'll just kill the subsystem
		//See process_debug_keys() in keycontrol.cpp for details. 
		if (damage < 0.0f) {
			// single player or multiplayer
			Assert(Player_ai->targeted_subsys != NULL);
			if ( (subsys == Player_ai->targeted_subsys) && (subsys->current_hits > 0.0f) ) {
				Assert(mss->type == (int) -damage);
				if (!(subsys->flags[Ship::Subsystem_Flags::No_aggregate])) {
					ship_p->subsys_info[mss->type].aggregate_current_hits -= subsys->current_hits;
					if (ship_p->subsys_info[mss->type].aggregate_current_hits < 0.0f) {
						ship_p->subsys_info[mss->type].aggregate_current_hits = 0.0f;
					}
				}
				subsys->current_hits = 0.0f;
				do_subobj_destroyed_stuff( ship_p, subsys, global_damage ? nullptr : hitpos );
				continue;
			} else {
				continue;
			}
		}

		if (subsys->current_hits > 0.0f) {
			float	dist, range;

			if (Fixed_turret_collisions && submodel_num != -1 && submodel_num == mss->turret_gun_sobj) {
				// Special case:
				// if the subsystem is a turret and the hit submodel is its barrel,
				// get the distance between the hit and the turret barrel center
				if (pmi == nullptr) {
					pmi = model_get_instance(ship_p->model_instance_num);
					pm = model_get(pmi->model_num);
				}
				model_instance_local_to_global_point(&g_subobj_pos, &vmd_zero_vector, pm, pmi, submodel_num, &ship_objp->orient, &ship_objp->pos);
				dist = vm_vec_dist_quick(&hitpos2, &g_subobj_pos);

				// Damage attenuation range of barrel radius * 2 makes full damage
				// be taken regardless of where the barrel is hit
				range = submodel_get_radius(Ship_info[ship_p->ship_info_index].model_num, submodel_num) * 2;
			} else {
				// Default behavior:
				// get the distance between the hit and the subsystem center
				get_subsystem_world_pos(ship_objp, subsys, &g_subobj_pos);
				dist = vm_vec_dist_quick(&hitpos2, &g_subobj_pos);

				range = subsys_get_range(other_obj, subsys);
			}

			if ( dist < range) {
				if (Damage_impacted_subsystem_first && submodel_num != -1 && (submodel_num == mss->subobj_num || submodel_num == mss->turret_gun_sobj)) {
					// If the hit impacted this subsystem's submodel, then make sure this subsys
					// gets dealt damage first, even if another subsystem is closer to the hit location
					subsys_hit_first = count;
				}

				if (mss->flags[Model::Subsystem_Flags::Collide_submodel]) {
					if (submodel_num != -1 && submodel_num != mss->subobj_num && submodel_num != mss->turret_gun_sobj) {
						// If this subsystem only wants to take damage when its submodel receives
						// a direct hit and the current hit did not do so, skip it.
						continue;
					}
				}

				subsys_list[count].dist = dist;
				subsys_list[count].range = range;
				subsys_list[count].ptr = subsys;
				count++;

				if (count >= MAX_SUBSYS_LIST){
					break;
				}
			}
		}
	}

	int dmg_type_idx = -1;
	int parent_armor_flags = 0;

	if(ship_p->armor_type_idx > -1)
		parent_armor_flags = Armor_types[ship_p->armor_type_idx].flags;

	if (other_obj)
	{
		if(other_obj->type == OBJ_SHOCKWAVE) {
			dmg_type_idx = shockwave_get_damage_type_idx(other_obj->instance);
		} else if(other_obj->type == OBJ_WEAPON) {
			dmg_type_idx = Weapon_info[Weapons[other_obj->instance].weapon_info_index].damage_type_idx;
		} else if(other_obj->type == OBJ_BEAM) {
			dmg_type_idx = Weapon_info[beam_get_weapon_info_index(other_obj)].damage_type_idx;
		} else if(other_obj->type == OBJ_ASTEROID) {
			dmg_type_idx = Asteroid_info[Asteroids[other_obj->instance].asteroid_type].damage_type_idx;
		} else if(other_obj->type == OBJ_DEBRIS) {
			dmg_type_idx = Debris[other_obj->instance].damage_type_idx;
		} else if(other_obj->type == OBJ_SHIP) {
			dmg_type_idx = Ships[other_obj->instance].collision_damage_type_idx;
		}
	}

	//	Now scan the sorted list of subsystems in range.
	//	Apply damage to the nearest one first (exception: subsys_hit_first),
	//	subtracting off damage as we go.
	int	i, j;
	for (j=0; j<count; j++)
	{
		float	dist, range;
		ship_subsys	*subsystem;

		int	min_index = -1;
		{
			float	min_dist = 9999999.9f;

			// find the closest subsystem
			for (i=0; i<count; i++) {
				if (subsys_list[i].dist < min_dist) {
					min_dist = subsys_list[i].dist;
					min_index = i;
				}
			}
			Assert(min_index != -1);
		}

		// if the closest system does *not* override a submodel impact, and we have a submodel impact, use it instead
		if (Damage_impacted_subsystem_first && subsys_hit_first >= 0 && !subsys_list[min_index].ptr->system_info->flags[Model::Subsystem_Flags::Override_submodel_impact]) {
			min_index = subsys_hit_first;
			subsys_hit_first = -1;	// prevent the submodel impact from taking priority on the next loop iteration
		}

		float	damage_to_apply = 0.0f;
		subsystem = subsys_list[min_index].ptr;
		range = subsys_list[min_index].range;
		dist = subsys_list[min_index].dist;
		subsys_list[min_index].dist = 9999999.9f;	//	Make sure we don't use this one again.

		Assert(range > 0.0f);	// Goober5000 - avoid div-0 below

		// Make sure this subsystem still has hitpoints.  If it's a child of a parent that was destroyed, it will have been destroyed already.
		if (subsystem->current_hits <= 0.0f) {
			continue;
		}

		// only do this for the closest affected subsystem
		if ( (j == 0) && (!(parent_armor_flags & SAF_IGNORE_SS_ARMOR))) {
			if(subsystem->armor_type_idx > -1)
			{
				damage = Armor_types[subsystem->armor_type_idx].GetDamage(damage, dmg_type_idx, 1.0f, other_obj_is_beam); // Nuke: I don't think we need to apply damage sacaling to this one, using 1.0f
				if(hull_should_apply_armor) {
					*hull_should_apply_armor = false;
				}
			}
		}

		//	HORRIBLE HACK!
		//	MK, 9/4/99
		//	When Helios bombs are dual fired against the Juggernaut in sm3-01 (FS2), they often
		//	miss their target.  There is code dating to FS1 in the collision code to detect that a bomb or
		//	missile has somehow missed its target.  It gets its lifeleft set to 0.1 and then it detonates.
		//	Unfortunately, the shockwave damage was cut by 4 above.  So boost it back up here.
		if ((weapon_info_index >= 0) && (dist < 10.0f) && other_obj_is_shockwave) {	// Goober5000 check for NULL
			damage_left *= 4.0f * Weapon_info[weapon_info_index].subsystem_factor;
			damage_if_hull *= 4.0f * Weapon_info[weapon_info_index].armor_factor;			
		}

		if ( dist < range/2.0f ) {
			if (subsystem->flags[Ship::Subsystem_Flags::Damage_as_hull])
				damage_to_apply = damage_if_hull;
			else
				damage_to_apply = damage_left;
		} else if ( dist < range ) {
			if (subsystem->flags[Ship::Subsystem_Flags::Damage_as_hull])
				damage_to_apply = damage_if_hull * (1.0f - dist/range);
			else
				damage_to_apply = damage_left * (1.0f - dist/range);
		}

		// if we're not in CLIENT_NODAMAGE multiplayer mode (which is a the NEW way of doing things)
		if ( (damage_to_apply > 0.1f) && !(MULTIPLAYER_CLIENT) )
		{
			//	Decrease damage to subsystems to player ships.
			if (ship_objp->flags[Object::Object_Flags::Player_ship]){
				ss_dif_scale = The_mission.ai_profile->subsys_damage_scale[Game_skill_level];
			}

			// maybe modify damage FROM player ships
			if (other_obj && other_obj->parent >= 0 && Objects[other_obj->parent].signature == other_obj->parent_sig) {
				if (Objects[other_obj->parent].flags[Object::Object_Flags::Player_ship])
					ss_dif_scale *= The_mission.ai_profile->player_damage_inflicted_scale[Game_skill_level];
			}
		
			// Goober5000 - subsys guardian
			if (subsystem->subsys_guardian_threshold > 0)
			{
				float min_subsys_strength = 0.01f * subsystem->subsys_guardian_threshold * subsystem->max_hits;
				if ( (subsystem->current_hits - (damage_to_apply * ss_dif_scale)) < min_subsys_strength ) {
					// find damage needed to take object to min subsys strength
					damage_to_apply = subsystem->current_hits - min_subsys_strength;

					// make sure damage is positive
					damage_to_apply = MAX(0.0f, damage_to_apply);
				}
			}

			// decrease the damage left to apply to the ship subsystems
			// WMC - since armor aborbs damage, subtract the amount of damage before we apply armor
			damage_left -= (damage_to_apply * ss_dif_scale);

			// if this subsystem doesn't carry damage then subtract it off of our total return
			if (subsystem->system_info->flags[Model::Subsystem_Flags::Carry_no_damage]) {
				if (!other_obj_is_shockwave || !(subsystem->system_info->flags[Model::Subsystem_Flags::Carry_shockwave])) {
					float subsystem_factor = 0.0f;
					if ((weapon_info_index >= 0) && (other_obj_is_weapon || other_obj_is_shockwave)) {
						if (subsystem->flags[Ship::Subsystem_Flags::Damage_as_hull]) {
							subsystem_factor = Weapon_info[weapon_info_index].armor_factor;
						} else {
							subsystem_factor = Weapon_info[weapon_info_index].subsystem_factor;
						}
					}
					if (subsystem_factor > 0.0f) {
						damage -= ((MIN(subsystem->current_hits, (damage_to_apply * ss_dif_scale))) / subsystem_factor);
					} else {
						damage -= MIN(subsystem->current_hits, (damage_to_apply * ss_dif_scale));
					}
				}
			}

			//Apply armor to damage
			if (subsystem->armor_type_idx >= 0) {
				// Nuke: this will finally factor it in to damage_to_apply and i wont need to factor it in anywhere after this
				damage_to_apply = Armor_types[subsystem->armor_type_idx].GetDamage(damage_to_apply, dmg_type_idx, ss_dif_scale, other_obj_is_beam);
			} else { // Nuke: no get damage call to apply difficulty scaling, so factor it in now
				damage_to_apply *= ss_dif_scale;
			}

			if (j == 0 && !shield_hit) {
				subsys_impact = ConditionData {
					ImpactCondition(subsystem->armor_type_idx),
					HitType::SUBSYS,
					damage_to_apply,
					subsystem->current_hits,
					subsystem->max_hits,
				};
			}

			subsystem->current_hits -= damage_to_apply;
			if (!(subsystem->flags[Ship::Subsystem_Flags::No_aggregate])) {
				ship_p->subsys_info[subsystem->system_info->type].aggregate_current_hits -= damage_to_apply;
			}

			if (subsystem->current_hits < 0.0f) {
				damage_left -= subsystem->current_hits;
				if (!(subsystem->flags[Ship::Subsystem_Flags::No_aggregate])) {
					ship_p->subsys_info[subsystem->system_info->type].aggregate_current_hits -= subsystem->current_hits;
				}
				subsystem->current_hits = 0.0f;					// set to 0 so repair on subsystem takes immediate effect
			}

			if ( ship_p->subsys_info[subsystem->system_info->type].aggregate_current_hits < 0.0f ){
				ship_p->subsys_info[subsystem->system_info->type].aggregate_current_hits = 0.0f;
			}

			// multiplayer clients never blow up subobj stuff on their own
			if ( (subsystem->current_hits <= 0.0f) && !MULTIPLAYER_CLIENT) {
				do_subobj_destroyed_stuff( ship_p, subsystem, global_damage ? nullptr : hitpos );
			}

			if (damage_left <= 0)	{ // no more damage to distribute, so stop checking
				damage_left = 0.0f;
				break;
			}
		}
	}

	if (damage < 0.0f) {
		damage = 0.0f;
	}

	//	Note: I changed this to return damage_left and it completely screwed up balance.
	//	It had taken a few MX-50s to destory an Anubis (with 40% hull), then it took maybe ten.
	//	So, I left it alone. -- MK, 4/15/98

	return std::make_pair(subsys_impact, damage);
}

// Store who/what killed the player, so we can tell the player how he died
static void shiphit_record_player_killer(const object *killer_objp, player *p)
{
	switch (killer_objp->type) {

	case OBJ_WEAPON:
		p->killer_objtype=OBJ_WEAPON;
		p->killer_weapon_index=Weapons[killer_objp->instance].weapon_info_index;
		if (killer_objp->parent >= 0 && killer_objp->parent < MAX_OBJECTS) {
			p->killer_species = Ship_info[Ships[Objects[killer_objp->parent].instance].ship_info_index].species;

			if ( &Objects[killer_objp->parent] == Player_obj ) {
				// killed by a missile?
				if(Weapon_info[p->killer_weapon_index].subtype == WP_MISSILE){
					p->flags |= PLAYER_FLAGS_KILLED_SELF_MISSILES;
				} else {
					p->flags |= PLAYER_FLAGS_KILLED_SELF_UNKNOWN;
				}
			}

			// in multiplayer, record callsign of killer if killed by another player
			if ( (Game_mode & GM_MULTIPLAYER) && ( Objects[killer_objp->parent].flags[Object::Object_Flags::Player_ship]) ) {
				int pnum;

				pnum = multi_find_player_by_object( &Objects[killer_objp->parent] );
				if ( pnum != -1 ) {
					strcpy_s(p->killer_parent_name, Net_players[pnum].m_player->callsign);
				} else {
					nprintf(("Network", "Couldn't find player object of weapon for killer of %s\n", p->callsign));
				}
			} else {
				strcpy_s(p->killer_parent_name, Ships[Objects[killer_objp->parent].instance].ship_name);
			}
		} else {
			p->killer_species = -1;
			strcpy_s(p->killer_parent_name, "");
		}

		break;

	case OBJ_SHOCKWAVE:
		p->killer_objtype=OBJ_SHOCKWAVE;
		p->killer_weapon_index = shockwave_get_weapon_index(killer_objp->instance);
		p->killer_species = Ship_info[Ships[Objects[killer_objp->parent].instance].ship_info_index].species;

		if ( &Objects[killer_objp->parent] == Player_obj ) {
			p->flags |= PLAYER_FLAGS_KILLED_SELF_SHOCKWAVE;
		}

		if ( (Game_mode & GM_MULTIPLAYER) && ( Objects[killer_objp->parent].flags[Object::Object_Flags::Player_ship]) ) {
			int pnum;

			pnum = multi_find_player_by_object( &Objects[killer_objp->parent] );
			if ( pnum != -1 ) {
				strcpy_s(p->killer_parent_name, Net_players[pnum].m_player->callsign);
			} else {
				nprintf(("Network", "Couldn't find player object of shockwave for killer of %s\n", p->callsign));
			}
		} else {
			strcpy_s(p->killer_parent_name, Ships[Objects[killer_objp->parent].instance].ship_name);
		}
		break;

	case OBJ_SHIP:
		p->killer_objtype=OBJ_SHIP;
		p->killer_weapon_index=-1;
		p->killer_species = Ship_info[Ships[killer_objp->instance].ship_info_index].species;

		if ( Ships[killer_objp->instance].flags[Ship::Ship_Flags::Exploded] ) {
			p->flags |= PLAYER_FLAGS_KILLED_BY_EXPLOSION;
		}

		if ( Ships[Objects[p->objnum].instance].wash_killed ) {
			p->flags |= PLAYER_FLAGS_KILLED_BY_ENGINE_WASH;
		}

		// in multiplayer, record callsign of killer if killed by another player
		if ( (Game_mode & GM_MULTIPLAYER) && (killer_objp->flags[Object::Object_Flags::Player_ship]) ) {
			int pnum;

			pnum = multi_find_player_by_object( killer_objp );
			if ( pnum != -1 ) {
				strcpy_s(p->killer_parent_name, Net_players[pnum].m_player->callsign);
			} else {
				nprintf(("Network", "Couldn't find player object for killer of %s\n", p->callsign));
			}
		} else {
			strcpy_s(p->killer_parent_name, Ships[killer_objp->instance].ship_name);
		}
		break;
	
	case OBJ_DEBRIS:
	case OBJ_ASTEROID:
		if ( killer_objp->type == OBJ_DEBRIS ) {
			p->killer_objtype = OBJ_DEBRIS;
		} else {
			p->killer_objtype = OBJ_ASTEROID;
		}
		p->killer_weapon_index=-1;
		p->killer_species = -1;
		p->killer_parent_name[0] = '\0';
		break;

	case OBJ_BEAM:
		int beam_obj;
		beam_obj = beam_get_parent(killer_objp);
		p->killer_species = -1;		
		p->killer_objtype = OBJ_BEAM;
		if(beam_obj != -1){			
			if((Objects[beam_obj].type == OBJ_SHIP) && (Objects[beam_obj].instance >= 0)){
				strcpy_s(p->killer_parent_name, Ships[Objects[beam_obj].instance].ship_name);
			}
			p->killer_species = Ship_info[Ships[Objects[beam_obj].instance].ship_info_index].species;
		} else {			
			strcpy_s(p->killer_parent_name, "");
		}
		break;
	
	case OBJ_NONE:
		if ( Game_mode & GM_MULTIPLAYER ) {
			Int3();
		}
		p->killer_objtype=-1;
		p->killer_weapon_index=-1;
		p->killer_parent_name[0]=0;
		p->killer_species = -1;
		break;

	default:
		Int3();
		break;
	}
}

//	Say dead stuff.
static void show_dead_message(const object *ship_objp, const object *other_obj)
{
	player *player_p;

	// not doing anything when a non player dies.
	if ( !(ship_objp->flags[Object::Object_Flags::Player_ship]) ){
		return;
	}

	if(other_obj == NULL){
		return;
	}

	// Get a pointer to the player (we are assured a player ship was killed)
	if ( Game_mode & GM_NORMAL ) {
		player_p = Player;
	} else {
		// in multiplayer, get a pointer to the player that died.
		int pnum = multi_find_player_by_object( ship_objp );
		if ( pnum == -1 ) {
			return;
		}
		player_p = Net_players[pnum].m_player;
	}

	// multiplayer clients should already have this information.
	if ( !MULTIPLAYER_CLIENT ){
		shiphit_record_player_killer( other_obj, player_p );
	}

	// display a hud message is the guy killed isn't me (multiplayer only)
	/*
	if ( (Game_mode & GM_MULTIPLAYER) && (ship_obj != Player_obj) ) {
		char death_text[256];

		player_generate_death_text( player_p, death_text );
		HUD_sourced_printf(HUD_SOURCE_HIDDEN, death_text);
	}
	*/
}

/* JAS: THIS DOESN'T SEEM TO BE USED, SO I COMMENTED IT OUT
//	Apply damage to a ship, destroying if necessary, etc.
//	Returns portion of damage that exceeds ship shields, ie the "unused" portion of the damage.
//	Note: This system does not use the mesh shield.  It applies damage to the overall ship shield.
float apply_damage_to_ship(object *objp, float damage)
{
	float	_ss;

	add_shield_strength(objp, -damage);

	// check if shields are below 0%, if so take leftover damage and apply to ship integrity
	if ((_ss = get_shield_strength(objp)) < 0.0f ) {
		damage = -_ss;
		set_shield_strength(objp, 0.0f);
	} else
		damage = 0.0f;

	return damage;
}
*/

//	Do music processing for a ship hit.
static void ship_hit_music(object *ship_objp, object *other_obj)
{
	Assert(ship_objp);	// Goober5000
	Assert(other_obj);	// Goober5000

	ship* ship_p = &Ships[ship_objp->instance];
	object *parent;

	// Switch to battle track when a ship is hit by fire 
	//
	// If the ship hit has an AI class of none, it is a Cargo, NavBuoy or other non-aggressive
	// ship, so don't start the battle music	
	if (!stricmp(Ai_class_names[Ai_info[ship_p->ai_index].ai_class], NOX("none")))
		return;

	// Only start if ship hit and firing ship are from different teams
	int attackee_team, attacker_team;

	attackee_team = Ships[ship_objp->instance].team;

	// avoid uninitialized value by matching them
	attacker_team = attackee_team;

	switch ( other_obj->type )
	{
		case OBJ_SHIP:
			attacker_team = Ships[other_obj->instance].team;

			// Nonthreatening ship collided with ship, no big deal
			if ( !stricmp(Ai_class_names[Ai_info[Ships[other_obj->instance].ai_index].ai_class], NOX("none")) )
				return;

			break;

		case OBJ_WEAPON:
			// parent of weapon is object num of ship that fired it
			parent = &Objects[other_obj->parent];
			if (parent->signature == other_obj->parent_sig)
				attacker_team = Ships[parent->instance].team;
			break;

		default:
			// Unexpected object type collided with ship, no big deal.
			return;
	}

	// start the music if it was an attacking ship
	if (iff_x_attacks_y(attacker_team, attackee_team))
		event_music_battle_start();
}

//	Make sparks fly off a ship.
// Currently used in misison_parse to create partially damaged ships.
// NOTE: hitpos is in model coordinates on the detail[0] submodel (highest detail hull)
// WILL NOT WORK RIGHT IF ON A ROTATING SUBMODEL
void ship_hit_sparks_no_rotate(object *ship_objp, vec3d *hitpos)
{
	ship		*ship_p = &Ships[ship_objp->instance];

	int n = ship_p->num_sparks;
	if (n >= MAX_SHIP_SPARKS)	{
		n = Random::next(MAX_SHIP_SPARKS);
	} else {
		ship_p->num_sparks++;
	}

	// No rotation.  Just make the spark
	ship_p->sparks[n].pos = *hitpos;
	ship_p->sparks[n].submodel_num = -1;

	shipfx_emit_spark(ship_objp->instance, n);		// Create the first wave of sparks

	if ( n == 0 )	{
		ship_p->next_hit_spark = timestamp(0);		// when a hit spot will spark
	}
}

// find the max number of sparks allowed for ship
// limited for fighter by hull % others by radius.
int get_max_sparks(const object* ship_objp)
{
	Assert(ship_objp->type == OBJ_SHIP);
	Assert((ship_objp->instance >= 0) && (ship_objp->instance < MAX_SHIPS));
	if(ship_objp->type != OBJ_SHIP){
		return 1;
	}
	if((ship_objp->instance < 0) || (ship_objp->instance >= MAX_SHIPS)){
		return 1;
	}

	ship *ship_p = &Ships[ship_objp->instance];
	ship_info* si = &Ship_info[ship_p->ship_info_index];
	if (si->flags[Ship::Info_Flags::Fighter]) {
		float hull_percent = ship_objp->hull_strength / ship_p->ship_max_hull_strength;

		if (hull_percent > 0.8f) {
			return 1;
		} else if (hull_percent > 0.3f) {
			return 2;
		} else {
			return 3;
		}
	} else {
		int num_sparks = fl2i(ship_objp->radius * 0.08f);
		if (num_sparks > MAX_SHIP_SPARKS) {
			return MAX_SHIP_SPARKS;
		} else if (num_sparks < 3) {
			return 3;
		} else {
			return num_sparks;
		}
	}
}


// helper function to std::sort, sorting spark pairs by distance
static int spark_compare(const spark_pair &pair1, const spark_pair &pair2)
{
	Assert(pair1.dist >= 0);
	Assert(pair2.dist >= 0);

	return (pair1.dist < pair2.dist);
}

// for big ships, when all spark slots are filled, make intelligent choice of one to be recycled
static int choose_next_spark(const object *ship_objp, const vec3d *hitpos)
{
	int i, j, count, num_sparks, num_spark_pairs, spark_num;
	vec3d world_hitpos[MAX_SHIP_SPARKS];
	spark_pair spark_pairs[MAX_SPARK_PAIRS];
	ship *shipp = &Ships[ship_objp->instance];
	auto pmi = model_get_instance(shipp->model_instance_num);
	auto pm = model_get(pmi->model_num);

	// only choose next spark when all slots are full
	Assert(get_max_sparks(ship_objp) == Ships[ship_objp->instance].num_sparks);

	// get num_sparks
	num_sparks = shipp->num_sparks;
	Assert(num_sparks <= MAX_SHIP_SPARKS);

	// get num_spark_pairs -- only sort these
	num_spark_pairs = (num_sparks * num_sparks - num_sparks) / 2;

	// get the world hitpos for all sparks
	for (spark_num=0; spark_num<num_sparks; spark_num++) {
		if (shipp->sparks[spark_num].submodel_num != -1) {
			model_instance_local_to_global_point(&world_hitpos[spark_num], &shipp->sparks[spark_num].pos, pm, pmi, shipp->sparks[spark_num].submodel_num, &ship_objp->orient, &ship_objp->pos);
		} else {
			// rotate sparks correctly with current ship orient
			vm_vec_unrotate(&world_hitpos[spark_num], &shipp->sparks[spark_num].pos, &ship_objp->orient);
			vm_vec_add2(&world_hitpos[spark_num], &ship_objp->pos);
		}
	}

	// check we're not making a spark in the same location as a current one
	for (i=0; i<num_sparks; i++) {
		float dist = vm_vec_dist_squared(&world_hitpos[i], hitpos);
		if (dist < 1) {
			return i;
		}
	}

	// not same location, so maybe do random recyling
	if (frand() > 0.5f) {
		return Random::next(num_sparks);
	}

	// initialize spark pairs
	// (we must always initialize at least one, because the default is the pair at index 0)
	for (i=0; i<std::max(1,num_spark_pairs); i++) {
		spark_pairs[i].index1 = 0;
		spark_pairs[i].index2 = 0;
		spark_pairs[i].dist = FLT_MAX;
	}

	// set spark pairs
	count = 0;
	for (i=1; i<num_sparks; i++) {
		for (j=0; j<i; j++) {
			spark_pairs[count].index1 = i;
			spark_pairs[count].index2 = j;
			spark_pairs[count++].dist = vm_vec_dist_squared(&world_hitpos[i], &world_hitpos[j]);
		}
	}
	Assert(count == num_spark_pairs);

	// sort pairs
	std::sort(spark_pairs, spark_pairs + count, spark_compare);
	//mprintf(("Min spark pair dist %.1f\n", spark_pairs[0].dist));

	// look through the first few sorted pairs, counting number of indices of closest pair
	int index1 = spark_pairs[0].index1;
	int index2 = spark_pairs[0].index2;
	int count1 = 0;
	int count2 = 0;

	for (i=1; i<num_spark_pairs; i++) {
		if (spark_pairs[i].index1 == index1) {
			count1++;
		}
		if (spark_pairs[i].index2 == index1) {
			count1++;
		}
		if (spark_pairs[i].index1 == index2) {
			count2++;
		}
		if (spark_pairs[i].index2 == index2) {
			count2++;
		}
	}

	// recycle spark which has most indices in sorted list of pairs
	if (count1 > count2) {
		return index1;
	} else {
		return index2;
	}
}


//	Make sparks fly off a ship.
static void ship_hit_create_sparks(const object *ship_objp, const vec3d *hitpos, int submodel_num)
{
	vec3d	tempv;
	ship	*shipp = &Ships[ship_objp->instance];
	ship_info	*sip = &Ship_info[shipp->ship_info_index];
	polymodel *pm = nullptr;
	polymodel_instance *pmi = nullptr;

	int n, max_sparks;

	n = shipp->num_sparks;
	max_sparks = get_max_sparks(ship_objp);

	if (n >= max_sparks)	{
		if (sip->is_big_or_huge()) {
			// large ship, choose intelligently
			n = choose_next_spark(ship_objp, hitpos);
		} else {
			// otherwise, normal choice
			n = Random::next(max_sparks);
		}
	} else {
		shipp->num_sparks++;
	}

	bool instancing = false;
	// decide whether to do instancing
	if (submodel_num != -1) {
		pm = model_get(sip->model_num);
		if (pm->detail[0] != submodel_num) {
			// submodel is not hull
			// OPTIMIZE ... check if submodel can not rotate
			instancing = true;
			pmi = model_get_instance(shipp->model_instance_num);
		}
	}

	if (instancing) {
		// get the hit position in the subobject RF
		vec3d temp_zero, temp_x, temp_y, temp_z;
		model_instance_local_to_global_point(&temp_zero, &vmd_zero_vector, pm, pmi, submodel_num, &ship_objp->orient, &ship_objp->pos);
		model_instance_local_to_global_point(&temp_x, &vmd_x_vector, pm, pmi, submodel_num, &ship_objp->orient, &ship_objp->pos);
		model_instance_local_to_global_point(&temp_y, &vmd_y_vector, pm, pmi, submodel_num, &ship_objp->orient, &ship_objp->pos);
		model_instance_local_to_global_point(&temp_z, &vmd_z_vector, pm, pmi, submodel_num, &ship_objp->orient, &ship_objp->pos);

		// find submodel x,y,z axes
		vm_vec_sub2(&temp_x, &temp_zero);
		vm_vec_sub2(&temp_y, &temp_zero);
		vm_vec_sub2(&temp_z, &temp_zero);

		// find displacement from submodel origin
		vec3d diff;
		vm_vec_sub(&diff, hitpos, &temp_zero);

		// find displacement from submodel origin in submodel RF
		shipp->sparks[n].pos.xyz.x = vm_vec_dot(&diff, &temp_x);
		shipp->sparks[n].pos.xyz.y = vm_vec_dot(&diff, &temp_y);
		shipp->sparks[n].pos.xyz.z = vm_vec_dot(&diff, &temp_z);
		shipp->sparks[n].submodel_num = submodel_num;
		shipp->sparks[n].end_time = timestamp(-1);
	} else {
		// Rotate hitpos into ship_objp's frame of reference.
		vm_vec_sub(&tempv, hitpos, &ship_objp->pos);
		vm_vec_rotate(&shipp->sparks[n].pos, &tempv, &ship_objp->orient);
		shipp->sparks[n].submodel_num = -1;
		shipp->sparks[n].end_time = timestamp(-1);
	}

	// Create the first wave of sparks
	shipfx_emit_spark(ship_objp->instance, n);

	if ( n == 0 )	{
		shipp->next_hit_spark = timestamp(0);		// when a hit spot will spark
	}
}

//	Called from ship_hit_kill() when we detect the player has been killed.
static void player_died_start(const object *killer_objp)
{
	nprintf(("Network", "starting my player death\n"));
	gameseq_post_event(GS_EVENT_DEATH_DIED);	
	
/*	vm_vec_scale_add(&Dead_camera_pos, &Player_obj->pos, &Player_obj->orient.fvec, -10.0f);
	vm_vec_scale_add2(&Dead_camera_pos, &Player_obj->orient.uvec, 3.0f);
	vm_vec_scale_add2(&Dead_camera_pos, &Player_obj->orient.rvec, 5.0f);
*/

	//	Create a good vector for the camera to move along during death sequence.
	const object	*other_objp = nullptr;

	// on multiplayer clients, there have been occasions where we haven't been able to determine
	// the killer of a ship (due to bogus/mismatched/timed-out signatures on the client side).  If
	// we don't know the killer, use the Player_obj as the other_objp for camera position.
	if ( killer_objp ) {
		switch (killer_objp->type) {
		case OBJ_WEAPON:
		case OBJ_SHOCKWAVE:
			other_objp = &Objects[killer_objp->parent];
			break;
		case OBJ_SHIP:
		case OBJ_DEBRIS:
		case OBJ_ASTEROID:
		case OBJ_NONE:	//	Something that just got deleted due to also dying -- it happened to me! --MK.		
			other_objp = killer_objp;
			break;

		case OBJ_BEAM:
			int beam_obj_parent;
			beam_obj_parent = beam_get_parent(killer_objp);
			if(beam_obj_parent == -1){
				other_objp = killer_objp;
			} else {
				other_objp = &Objects[beam_obj_parent];
			}
			break;

		default:
			UNREACHABLE("Unhandled object type %d in player_died_start()", killer_objp->type);		//	Killed by an object of a peculiar type.  What is it?
			other_objp = killer_objp;	//	Enable to continue, just in case we shipped it with this bug...
		}
	} else {
		other_objp = Player_obj;
	}

	vm_vec_add(&Original_vec_to_deader, &Player_obj->orient.vec.fvec, &Player_obj->orient.vec.rvec);
	vm_vec_scale(&Original_vec_to_deader, 2.0f);
	vm_vec_add2(&Original_vec_to_deader, &Player_obj->orient.vec.uvec);
	vm_vec_normalize(&Original_vec_to_deader);

	vec3d	vec_from_killer;
	vec3d	*side_vec;
	float		dist;

	Assert(other_objp != nullptr);

	if (Player_obj == other_objp) {
		dist = 50.0f;
		vec_from_killer = Player_obj->orient.vec.fvec;
	} else {
		dist = vm_vec_normalized_dir(&vec_from_killer, &Player_obj->pos, &other_objp->pos);
	}

	if (dist > 100.0f)
		dist = 100.0f;
	vm_vec_scale_add(&Dead_camera_pos, &Player_obj->pos, &vec_from_killer, dist);

	float	dot = vm_vec_dot(&Player_obj->orient.vec.rvec, &vec_from_killer);
	if (fl_abs(dot) > 0.8f)
		side_vec = &Player_obj->orient.vec.fvec;
	else
		side_vec = &Player_obj->orient.vec.rvec;
	
	vm_vec_scale_add2(&Dead_camera_pos, side_vec, 10.0f);

	Player_ai->target_objnum = -1;		//	Clear targeting.  Otherwise, camera pulls away from player as soon as he blows up.

	// stop any playing emp effect
	emp_stop_local();
}


//#define	DEATHROLL_TIME						3000			//	generic deathroll is 3 seconds (3 * 1000 milliseconds) - Moved to ships.tbl
#define	MIN_PLAYER_DEATHROLL_TIME		1000			// at least one second deathroll for a player
#define	DEATHROLL_ROTVEL_MIN				0.8f			// minimum added deathroll rotvel in rad/sec (about 1 rev / 12 sec)
#define	DEATHROLL_MASS_STANDARD			50				// approximate mass of lightest ship
#define	DEATHROLL_VELOCITY_STANDARD	70				// deathroll rotvel is scaled according to ship velocity
#define	DEATHROLL_ROTVEL_SCALE			4				// constant determines how quickly deathroll rotvel is ramped up  (smaller is faster)

static void saturate_fabs(float *f, float max)
{
	if ( fl_abs(*f) > max) {
		if (*f > 0.0f)
			*f = max;
		else
			*f = -max;
	}
}

// function to do generic things when a ship explodes
void ship_generic_kill_stuff( object *objp, float percent_killed )
{
	float rotvel_mag;
	int	delta_time;
	ship	*sp;

	Assert(objp->type == OBJ_SHIP);
	Assert(objp->instance >= 0 && objp->instance < MAX_SHIPS );
	if((objp->type != OBJ_SHIP) || (objp->instance < 0) || (objp->instance >= MAX_SHIPS)){
		return;
	}
	sp = &Ships[objp->instance];
	ship_info *sip = &Ship_info[sp->ship_info_index];

	ai_announce_ship_dying(objp);

	ship_stop_fire_primary(objp);	//mostly for stopping fighter beam looping sounds -Bobboau

    sp->flags.set(Ship::Ship_Flags::Dying);
    objp->phys_info.flags |= (PF_DEAD_DAMP | PF_REDUCED_DAMP);
	objp->phys_info.gravity_const = sip->dying_gravity_const;
	delta_time = (int) (sip->death_roll_base_time);

	//	For smaller ships, subtract off time proportional to excess damage delivered.
	if (objp->radius < BIG_SHIP_MIN_RADIUS)
		delta_time -=  (int) (1.01f - 4*percent_killed);

	//	Cut down cargo death rolls.  Looks a little silly. -- MK, 3/30/98.
	if (sip->flags[Ship::Info_Flags::Cargo]) {
		delta_time /= 4;
	}
	
	//	Prevent bogus timestamps.
	if (delta_time < 2)
		delta_time = 2;
	
	if (objp->flags[Object::Object_Flags::Player_ship]) {
		//	Note: Kamikaze ships have no minimum death time.
		if (!(Ai_info[Ships[objp->instance].ai_index].ai_flags[AI::AI_Flags::Kamikaze]) && (delta_time < MIN_PLAYER_DEATHROLL_TIME))
			delta_time = MIN_PLAYER_DEATHROLL_TIME;
	}

	//nprintf(("AI", "ShipHit.cpp: Frame %i, Gametime = %7.3f, Ship %s will die in %7.3f seconds.\n", Framecount, f2fl(Missiontime), Ships[objp->instance].ship_name, (float) delta_time/1000.0f));

	//	Make big ships have longer deathrolls.
	//	This is debug code by MK to increase the deathroll time so ships have time to evade the shockwave.
	//	Perhaps deathroll time should be specified in ships.tbl.
	float damage = ship_get_exp_damage(objp);

	if (damage >= 250.0f)
		delta_time += 3000 + (int)(damage*4.0f + 4.0f*objp->radius);

	if (Ai_info[sp->ai_index].ai_flags[AI::AI_Flags::Kamikaze])
		delta_time = 2;

	// Knossos gets 7-10 sec to die, time for "little" explosions
	if (Ship_info[sp->ship_info_index].flags[Ship::Info_Flags::Knossos_device]) {
		delta_time = 7000 + (int)(frand() * 3000.0f);
		Ship_info[sp->ship_info_index].explosion_propagates = 0;
	}

	// Goober5000 - ship-specific deathroll time, woot
	if (sp->special_exp_deathroll_time > 0)
	{
		delta_time = sp->special_exp_deathroll_time;

		// prevent bogus timestamps, per comment several lines up
		if (delta_time < 2)
			delta_time = 2;
	}

	sp->death_time = sp->final_death_time = timestamp(delta_time);	// Give him 3 secs to explode

	//SUSHI: What are the chances of an instant explosion? Check the ship type (objecttypes.tbl) as well as the ship (ships.tbl)
	float skipChance;
	if (sp->flags[Ship::Ship_Flags::Vaporize])
		skipChance = 1.0f;
	else if (sip->skip_deathroll_chance > 0.0f)
		skipChance = sip->skip_deathroll_chance;
	else if (sip->class_type >= 0)
		skipChance = Ship_types[sip->class_type].skip_deathroll_chance;
	else
		skipChance = 0.0f;

	if (frand() < skipChance)
	{
		// LIVE FOR 100 MS
		sp->final_death_time = timestamp(100);
	}

	sp->pre_death_explosion_happened = 0;				// The little fireballs haven't came in yet.

	sp->next_fireball = timestamp(0);	//start one right away

	ai_deathroll_start(objp);

	// play death roll begin sound
	auto snd_id = ship_get_sound(objp, GameSounds::DEATH_ROLL);
	if (snd_id.isValid())
		sp->death_roll_snd = snd_play_3d( gamesnd_get_game_sound(snd_id), &objp->pos, &View_position, objp->radius );
	if (objp == Player_obj)
		joy_ff_deathroll();

	// apply a whack
	//	rotational velocity proportional to original translational velocity, with a bit added in.
	//	Also, preserve half of original rotational velocity.

	// At standard speed (70) and standard mass (50), deathroll rotvel should be capped at DEATHROLL_ROTVEL_CAP
	// Minimum deathroll velocity is set	DEATHROLL_ROTVEL_MIN
	// At lower speed, lower death rotvel (scaled linearly)
	// At higher mass, lower death rotvel (scaled logarithmically)
	// variable scale calculates the deathroll rotational velocity magnitude
	float logval = (float) log10(objp->phys_info.mass / (0.05f*DEATHROLL_MASS_STANDARD));
	float velval = ((vm_vec_mag_quick(&objp->phys_info.vel) + 3.0f) / DEATHROLL_VELOCITY_STANDARD);
	float	p1 = (float) (DEATHROLL_ROTVEL_CAP - DEATHROLL_ROTVEL_MIN);

	rotvel_mag = (float) DEATHROLL_ROTVEL_MIN * 2.0f/(logval + 2.0f);
	rotvel_mag += (float) (p1 * velval/logval) * 0.75f;

	// set so maximum velocity from rotation is less than 200
	if (rotvel_mag*objp->radius > 150) {
		rotvel_mag = 150.0f / objp->radius;
	}

	rotvel_mag *= sip->death_roll_rotation_mult;

	if (object_is_dead_docked(objp)) {
		// don't change current rotvel
		sp->deathroll_rotvel = objp->phys_info.rotvel;
	} else {
		// if added rotvel is too random, we should decrease the random component, putting a const in front of the rotvel.
		sp->deathroll_rotvel = objp->phys_info.rotvel;
		sp->deathroll_rotvel.xyz.x += (frand() - 0.5f) * 2.0f * rotvel_mag;
		saturate_fabs(&sp->deathroll_rotvel.xyz.x, sip->death_roll_xrotation_cap);
		sp->deathroll_rotvel.xyz.y += (frand() - 0.5f) * 3.0f * rotvel_mag;
		saturate_fabs(&sp->deathroll_rotvel.xyz.y, sip->death_roll_yrotation_cap);
		sp->deathroll_rotvel.xyz.z += (frand() - 0.5f) * 6.0f * rotvel_mag;
		// make z component  2x larger than larger of x,y
		float largest_mag = MAX(fl_abs(sp->deathroll_rotvel.xyz.x), fl_abs(sp->deathroll_rotvel.xyz.y));
		if (fl_abs(sp->deathroll_rotvel.xyz.z) < 2.0f*largest_mag) {
			sp->deathroll_rotvel.xyz.z *= (2.0f * largest_mag / fl_abs(sp->deathroll_rotvel.xyz.z));
		}
		saturate_fabs(&sp->deathroll_rotvel.xyz.z, sip->death_roll_zrotation_cap);
		// nprintf(("Physics", "Frame: %i rotvel_mag: %5.2f, rotvel: (%4.2f, %4.2f, %4.2f)\n", Framecount, rotvel_mag, sp->deathroll_rotvel.x, sp->deathroll_rotvel.y, sp->deathroll_rotvel.z));
	}

	
	// blow out his reverse thrusters. Or drag, same thing.
	objp->phys_info.rotdamp = (float) DEATHROLL_ROTVEL_SCALE / rotvel_mag;
	objp->phys_info.side_slip_time_const = 10000.0f;

	vm_vec_zero(&objp->phys_info.max_vel);		// make so he can't turn on his own VOLITION anymore.

	vm_vec_zero(&objp->phys_info.max_rotvel);	// make so he can't change speed on his own VOLITION anymore.
}

// called from ship_hit_kill if the ship is vaporized
static void ship_vaporize(ship *shipp)
{
	object *ship_objp;

	// sanity
	Assert(shipp != NULL);
	if(shipp == NULL){
		return;
	}
	Assert((shipp->objnum >= 0) && (shipp->objnum < MAX_OBJECTS));
	if((shipp->objnum < 0) || (shipp->objnum >= MAX_OBJECTS)){
		return;
	}
	ship_objp = &Objects[shipp->objnum];
	ship_info* sip = &Ship_info[shipp->ship_info_index];

	// create debris shards
	create_generic_debris(ship_objp, &ship_objp->pos, (float)sip->generic_debris_spew_num, sip->generic_debris_spew_num * 2.0f, 1.4f, true);
}

//	*ship_objp was hit and we've determined he's been killed!  By *other_obj!
void ship_hit_kill(object *ship_objp, object *other_obj, const vec3d *hitpos, float percent_killed, bool self_destruct, bool always_log_other_obj)
{
	Assert(ship_objp);	// Goober5000 - but not other_obj, not only for sexp but also for self-destruct
	ship *sp = &Ships[ship_objp->instance];

	// don't kill the ship if it's already dying
	if (sp->flags[Ship::Ship_Flags::Dying])
		return;

	if (scripting::hooks::OnShipDeathStarted->isActive())
	{
		// add scripting hook for 'On Ship Death Started' -- Goober5000
		// hook is placed at the beginning of this function to allow the scripter to
		// actually have access to the ship before any death routines (such as mission logging) are executed
		scripting::hooks::OnShipDeathStarted->run(scripting::hooks::ShipDeathConditions{ sp },
			scripting::hook_param_list(
			scripting::hook_param("Ship", 'o', ship_objp),
			scripting::hook_param("Killer", 'o', other_obj),
			scripting::hook_param("Hitpos",
				'o',
				scripting::api::l_Vector.Set(hitpos ? *hitpos : vmd_zero_vector),
				hitpos != nullptr)));
	}

	// if the OnDeath override is enabled, run the hook and then exit
	if (scripting::hooks::OnDeath->isActive())
	{
		auto onDeathParamList = scripting::hook_param_list(scripting::hook_param("Self", 'o', ship_objp),
			scripting::hook_param("Ship", 'o', ship_objp),
			scripting::hook_param("Killer", 'o', other_obj),
			scripting::hook_param("Hitpos",
				'o',
				scripting::api::l_Vector.Set(hitpos ? *hitpos : vmd_zero_vector),
				hitpos != nullptr));

		if (scripting::hooks::OnDeath->isOverride(scripting::hooks::ObjectDeathConditions{ ship_objp }, onDeathParamList)) {
			scripting::hooks::OnDeath->run(scripting::hooks::ObjectDeathConditions{ ship_objp }, std::move(onDeathParamList));
			return;
		}
	}

	// if the OnShipDeath override is enabled, run the hook and then exit
	if (scripting::hooks::OnShipDeath->isActive())
	{
		auto onDeathParamList = scripting::hook_param_list(
			scripting::hook_param("Ship", 'o', ship_objp),
			scripting::hook_param("Killer", 'o', other_obj),
			scripting::hook_param("Hitpos",
				'o',
				scripting::api::l_Vector.Set(hitpos ? *hitpos : vmd_zero_vector),
				hitpos != nullptr));

		if (scripting::hooks::OnShipDeath->isOverride(scripting::hooks::ShipDeathConditions{ sp }, onDeathParamList)) {
			scripting::hooks::OnShipDeath->run(scripting::hooks::ShipDeathConditions{ sp }, onDeathParamList);
			return;
		}
	}

	char *killer_ship_name;
	int killer_damage_percent = 0;
	int killer_index = -1;
	object *killer_objp = NULL;

	show_dead_message(ship_objp, other_obj);

	if (ship_objp == Player_obj) {
		player_died_start(other_obj);
	}

	// maybe vaporize him
	if(sp->flags[Ship::Ship_Flags::Vaporize]){
		ship_vaporize(sp);
	}

	// hehe
	extern void game_tst_mark(const object *objp, const ship *shipp);
	game_tst_mark(ship_objp, sp);

	// single player and multiplayer masters evaluate the scoring and kill stuff
	if ( !MULTIPLAYER_CLIENT ) {
		killer_index = scoring_eval_kill( ship_objp );

		// ship is destroyed -- send this event to the mission log stuff to record this event.  Try to find who
		// killed this ship.  scoring_eval_kill above should leave the obj signature of the ship who killed
		// this guy (or a -1 if no one got the kill).
		killer_ship_name = NULL;
		killer_damage_percent = -1;
		if ( killer_index >= 0 ) {
			object *objp;
			int sig;

			sig = sp->damage_ship_id[killer_index];
			for ( objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) ) {
				// don't skip should-be-dead objects here
				if ( objp->signature == sig ){
					break;
				}
			}
			// if the object isn't around, the try to find the object in the list of ships which has exited			
			if ( objp != END_OF_LIST(&obj_used_list) ) {
				Assert ( (objp->type == OBJ_SHIP ) || (objp->type == OBJ_GHOST) );					// I suppose that this should be true
				killer_ship_name = Ships[objp->instance].ship_name;

				killer_objp = objp;
			} else {
				int ei;

				ei = ship_find_exited_ship_by_signature( sig );
				if ( ei != -1 ){
					killer_ship_name = Ships_exited[ei].ship_name;
				}
			}
			killer_damage_percent = (int)(sp->damage_ship[killer_index]/sp->total_damage_received * 100.0f);
		}

		// are we going to insist that other_obj was the killer?
		if (always_log_other_obj && other_obj) {
			auto named_objp = other_obj;

			if (named_objp->type != OBJ_SHIP && named_objp->parent >= 0) {
				named_objp = &Objects[other_obj->parent];
			}
			if (named_objp->type == OBJ_SHIP) {
				killer_objp = nullptr;
				killer_ship_name = Ships[named_objp->instance].ship_name;
				killer_damage_percent = (int)(percent_killed * 100.0f);
			}
		}

		if (self_destruct) {
			// try and find a player
			if(Game_mode & GM_MULTIPLAYER){
				int np_index = multi_find_player_by_object(ship_objp);
				if((np_index >= 0) && (np_index < MAX_PLAYERS) && (Net_players[np_index].m_player != NULL)){
					mission_log_add_entry(LOG_SELF_DESTRUCTED, Net_players[np_index].m_player->callsign, NULL );
				} else {
					mission_log_add_entry(LOG_SELF_DESTRUCTED, Ships[ship_objp->instance].ship_name, NULL );
				}
			} else {
				mission_log_add_entry(LOG_SELF_DESTRUCTED, Ships[ship_objp->instance].ship_name, NULL );
			}
		} else {
			// multiplayer
			if(Game_mode & GM_MULTIPLAYER){
				char name1[256] = "";
				char name2[256] = "";
				int np_index;

				// get first name				
				np_index = multi_find_player_by_object(ship_objp);
				if((np_index >= 0) && (np_index < MAX_PLAYERS) && (Net_players[np_index].m_player != NULL)){
					strcpy_s(name1, Net_players[np_index].m_player->callsign);
				} else {
					strcpy_s(name1, sp->ship_name);
				}

				// argh
				if((killer_objp != NULL) || (killer_ship_name != NULL)){

					// second name
					if(killer_objp == NULL){
						strcpy_s(name2, killer_ship_name);
					} else {
						np_index = multi_find_player_by_object(killer_objp);
						if((np_index >= 0) && (np_index < MAX_PLAYERS) && (Net_players[np_index].m_player != NULL)){
							strcpy_s(name2, Net_players[np_index].m_player->callsign);
						} else {
							strcpy_s(name2, killer_ship_name);
						}
					}					
				}

				mission_log_add_entry(LOG_SHIP_DESTROYED, name1, name2, killer_damage_percent);
			} else {
				// DKA: 8/23/99 allow message log in single player with no killer name
				//if(killer_ship_name != NULL){
				mission_log_add_entry(LOG_SHIP_DESTROYED, sp->ship_name, killer_ship_name, killer_damage_percent);
				//}
			}
		}

		// maybe praise the player for this kill
		if ( (killer_damage_percent > 10) && (other_obj != nullptr) && (other_obj->parent >= 0) ) {
			if (other_obj->parent_sig == Player_obj->signature) {
				ship_maybe_praise_player(sp);
			} else if (Objects[other_obj->parent].type == OBJ_SHIP) {
				ship_maybe_praise_self(sp, &Ships[Objects[other_obj->parent].instance]);
			}
		}
	}

	// Goober5000 - since we added a mission log entry above, immediately set the status.  For destruction, ship_cleanup isn't called until a little bit later
	auto entry = &Ship_registry[Ship_registry_map[sp->ship_name]];
	entry->status = ShipStatus::DEATH_ROLL;

	ship_generic_kill_stuff( ship_objp, percent_killed );

	// mwa -- removed 2/25/98 -- why is this here?  ship_objp->flags &= ~(OF_PLAYER_SHIP);
	// if it is for observers, must deal with it a separate way!!!!
	if ( MULTIPLAYER_MASTER ) {
		// check to see if this ship needs to be respawned
		multi_respawn_check(ship_objp);
			
		// send the kill packet to all players
		// maybe send vaporize packet to all players
		send_ship_kill_packet( ship_objp, other_obj, percent_killed, self_destruct );
	}

	// if a non-player is dying, play a scream
	if ( !(ship_objp->flags[Object::Object_Flags::Player_ship]) ) {
		ship_maybe_scream(sp);
	}

	// if the player is dying, have wingman lament
	if ( ship_objp == Player_obj ) {
		ship_maybe_lament();
	}

	if (scripting::hooks::OnDeath->isActive()) {
		auto onDeathParamList = scripting::hook_param_list(scripting::hook_param("Self", 'o', ship_objp),
			scripting::hook_param("Ship", 'o', ship_objp),
			scripting::hook_param("Killer", 'o', other_obj),
			scripting::hook_param("Hitpos",
				'o',
				scripting::api::l_Vector.Set(hitpos ? *hitpos : vmd_zero_vector),
				hitpos != nullptr));

		scripting::hooks::OnDeath->run(scripting::hooks::ObjectDeathConditions{ ship_objp }, onDeathParamList);
	}
	if (scripting::hooks::OnShipDeath->isActive()) {
		auto onDeathParamList = scripting::hook_param_list(
			scripting::hook_param("Ship", 'o', ship_objp),
			scripting::hook_param("Killer", 'o', other_obj),
			scripting::hook_param("Hitpos",
				'o',
				scripting::api::l_Vector.Set(hitpos ? *hitpos : vmd_zero_vector),
				hitpos != nullptr));

		scripting::hooks::OnShipDeath->run(scripting::hooks::ShipDeathConditions{ sp }, onDeathParamList);
	}
}

// function to simply explode a ship where it is currently at
void ship_self_destruct( object *objp )
{	
	Assert ( objp->type == OBJ_SHIP );

	// don't self-destruct if this ship is already dying
	if (Ships[objp->instance].flags[Ship::Ship_Flags::Dying])
		return;

	// check to see if this ship needs to be respawned
	if(MULTIPLAYER_MASTER){
		// player ship?
		int np_index = multi_find_player_by_object(objp);
		if((np_index >= 0) && (np_index < MAX_PLAYERS) && MULTI_CONNECTED(Net_players[np_index]) && (Net_players[np_index].m_player != NULL)){
			char msg[512] = "";
			sprintf(msg, "%s %s", Net_players[np_index].m_player->callsign, XSTR("Self destructed", 1476));

			// send a message
			send_game_chat_packet(Net_player, msg, MULTI_MSG_ALL, NULL, NULL, 2);

			// printf
			if(!(Game_mode & GM_STANDALONE_SERVER)){
				HUD_printf("%s", msg);
			}
		}
	}

	// self destruct
	ship_hit_kill(objp, nullptr, nullptr, 1.0f, true);
}

// Call this instead of physics_apply_whack directly to 
// deal with two docked ships properly.
// Goober5000 - note... hit_pos is in *world* coordinates
void ship_apply_whack(const vec3d *force, const vec3d *hit_pos, object *objp)
{
	Assertion((objp != nullptr) && (force != nullptr) && (hit_pos != nullptr), "ship_apply_whack invalid argument(s)");

	if (objp == Player_obj) {
		nprintf(("Sandeep", "Playing stupid joystick effect\n"));
		vec3d test;
		vm_vec_unrotate(&test, force, &objp->orient);

		game_whack_apply( -test.xyz.x, -test.xyz.y );
	}


	if (object_is_docked(objp))
	{
		dock_calculate_and_apply_whack_docked_object(force, hit_pos, objp);
	}
	else
	{
		// this one needs local position but since it doesn't have the objp its easier to just do this now
		vec3d rel_hit_pos;
		vm_vec_sub(&rel_hit_pos, hit_pos, &objp->pos);
		physics_calculate_and_apply_whack(force, &rel_hit_pos, &objp->phys_info, &objp->orient, &objp->phys_info.I_body_inv);
	}					
}

// If a ship is dying and it gets hit, shorten its deathroll.
//	But, if it's a player, don't decrease below MIN_PLAYER_DEATHROLL_TIME
static void shiphit_hit_after_death(object *ship_objp, float damage)
{
	float	percent_killed;
	int	delta_time, time_remaining;
	ship	*shipp = &Ships[ship_objp->instance];
	ship_info *sip = &Ship_info[shipp->ship_info_index];

	// Since the explosion has two phases (final_death_time and really_final_death_time)
	// we should only shorten the deathroll time if that is the phase we're in.
	// And you can tell by seeing if the timestamp is valid, since it gets set to
	// invalid after it does the first large explosion.
	if ( !timestamp_valid(shipp->final_death_time) )	{
		return;
	}

	// Don't adjust vaporized ship
	if (shipp->flags[Ship::Ship_Flags::Vaporize]) {
		return;
	}

	//	Don't shorten deathroll on very large ships.
	if (ship_objp->radius > BIG_SHIP_MIN_RADIUS)
		return;

	percent_killed = damage/shipp->ship_max_hull_strength;
	if (percent_killed > 1.0f)
		percent_killed = 1.0f;

	delta_time = (int) (4 * sip->death_roll_base_time * percent_killed);
	time_remaining = timestamp_until(shipp->final_death_time);

	//nprintf(("AI", "Gametime = %7.3f, Time until %s dies = %7.3f, delta = %7.3f\n", f2fl(Missiontime), Ships[ship_objp->instance].ship_name, (float)time_remaining/1000.0f, delta_time));
	if (ship_objp->flags[Object::Object_Flags::Player_ship])
		if (time_remaining < MIN_PLAYER_DEATHROLL_TIME)
			return;

	// nprintf(("AI", "Subtracting off %7.3f seconds from deathroll, reducing to %7.3f\n", (float) delta_time/1000.0f, (float) (time_remaining - delta_time)/1000.0f));

	delta_time = time_remaining - delta_time;
	if (ship_objp->flags[Object::Object_Flags::Player_ship])
		if (delta_time < MIN_PLAYER_DEATHROLL_TIME)
			delta_time = MIN_PLAYER_DEATHROLL_TIME;

	//	Prevent bogus timestamp.
	if (delta_time < 2)
		delta_time = 2;

	shipp->final_death_time = timestamp(delta_time);	// Adjust time until explosion.
}

MONITOR( ShipHits )
MONITOR( ShipNumDied )

static int maybe_shockwave_damage_adjust(const object *ship_objp, const object *other_obj, float *damage)
{
	ship_subsys *subsys;
	ship *shipp;
	weapon_info *wip;
	ship_info *sip;
	float dist, nearest_dist = FLT_MAX;
	vec3d g_subobj_pos;
	float max_damage;
	float inner_radius, outer_radius;

	Assert(ship_objp);	// Goober5000 (but not other_obj in case of sexp)
	Assert(damage);		// Goober5000

	Assert(ship_objp->type == OBJ_SHIP);

	if (!other_obj) {
		return 0;
	}

	if (other_obj->type != OBJ_SHOCKWAVE) {
		return 0;
	}

	shipp = &Ships[ship_objp->instance];
	sip = &Ship_info[shipp->ship_info_index];

	if (!sip->is_huge_ship()) {
		return 0;
	}

	// find closest subsystem distance to shockwave origin
	for (subsys=GET_FIRST(&shipp->subsys_list); subsys != END_OF_LIST(&shipp->subsys_list); subsys = GET_NEXT(subsys) ) {
		get_subsystem_world_pos(ship_objp, subsys, &g_subobj_pos);
		dist = vm_vec_dist_quick(&g_subobj_pos, &other_obj->pos);

		if (dist < nearest_dist) {
			nearest_dist = dist;
		}
	}

	// get max damage and adjust if needed to account for shockwave created from destroyed weapon
	max_damage = shockwave_get_damage(other_obj->instance);
	if (shockwave_get_flags(other_obj->instance) & SW_WEAPON_KILL) {
		max_damage *= 4.0f;
	}

	// If the shockwave was caused by a weapon, then check if the weapon can deal lethal damage to the ship.
	// If it cannot, then neither should the shockwave caused by the weapon be able to do the same.
	// The code for this is copied from part of weapon_get_damage_scale.
	int wp_index = shockwave_get_weapon_index(other_obj->instance);
	if ((wp_index >= 0) && Weapon_shockwaves_respect_huge) {
		wip = &Weapon_info[wp_index];

		if (!(wip->wi_flags[Weapon::Info_Flags::Heals])) {
			float hull_pct = get_hull_pct(ship_objp);

			// First handle Supercap ships.
			if ((sip->flags[Ship::Info_Flags::Supercap]) && !(wip->wi_flags[Weapon::Info_Flags::Supercap])) {
				if (hull_pct <= 0.75f) {
					*damage = 0.0f;
					return 1;
				}
				else {
					// If hull isn't below 3/4, then allow damage to be applied just like in weapon_get_damage_scale.
					// SUPERCAP_DAMAGE_SCALE is defined in weapon.h.
					max_damage *= SUPERCAP_DAMAGE_SCALE;
				}
			}

			// Next handle big damage ships.
			bool is_big_damage_ship = (sip->flags[Ship::Info_Flags::Big_damage]);
			if (is_big_damage_ship && !(wip->hurts_big_ships())) {
				if (hull_pct > 0.1f) {
					max_damage *= hull_pct;
				}
				else {
					*damage = 0.0f;
					return 1;
				}
			}
		}
	}

	outer_radius = shockwave_get_max_radius(other_obj->instance);
	inner_radius = shockwave_get_min_radius(other_obj->instance);

	// scale damage
	// floor of 25%, max if within inner_radius, linear between
	if (nearest_dist > outer_radius) {
		*damage = max_damage / 4.0f;
	} else if (nearest_dist < inner_radius) {
		*damage = max_damage;
	} else {
		*damage = max_damage * (1.0f - 0.75f * (nearest_dist - inner_radius) / (outer_radius - inner_radius));
	}

	return 1;
}

// ------------------------------------------------------------------------
// ship_do_damage()
//
// Do damage assessment on a ship.    This should only be called
// internally by ship_apply_global_damage and ship_apply_local_damage
//
// 
//	input:	ship_objp		=>		object pointer for ship receiving damage
//				other_obj	=>		object pointer to object causing damage
//				hitpos		=>		impact world pos on the ship 
//				TODO:	get a better value for hitpos
//				damage		=>		damage to apply to the ship
//				quadrant	=> which part of shield takes damage, -1 if not shield hit
//				submodel_num=> which submodel was hit, -1 if none in particular
//				wash_damage	=>		1 if damage is done by engine wash
// Goober5000 - sanity checked this whole function in the case that other_obj is NULL, which
// will happen with the explosion-effect sexp
void ai_update_lethality(const object *ship_objp, const object *weapon_obj, float damage);
static void ship_do_damage(object *ship_objp, object *other_obj, const vec3d *hitpos, float damage, int quadrant, int submodel_num, int damage_type_idx = -1, bool wash_damage = false, float hit_dot = 1.f, const vec3d* hit_normal = nullptr, const vec3d* local_hitpos = nullptr)
{
//	mprintf(("doing damage\n"));

	ship *shipp;	
	bool other_obj_is_weapon;
	bool other_obj_is_beam;
	bool other_obj_is_shockwave;
	float difficulty_scale_factor = 1.0f;

	Assertion(ship_objp, "No ship_objp in ship_do_damage!");
	Assertion(hitpos, "No hitpos in ship_do_damage!");

	Assertion(ship_objp->instance >= 0 && ship_objp->type == OBJ_SHIP, "invalid ship target in ship_do_damage");
	shipp = &Ships[ship_objp->instance];

	// maybe adjust damage done by shockwave for BIG|HUGE
	maybe_shockwave_damage_adjust(ship_objp, other_obj, &damage);

	// Goober5000 - check to see what other_obj is
	if (other_obj)
	{
		other_obj_is_weapon = ((other_obj->type == OBJ_WEAPON) && (other_obj->instance >= 0) && (other_obj->instance < MAX_WEAPONS));
		other_obj_is_beam = ((other_obj->type == OBJ_BEAM) && (other_obj->instance >= 0) && (other_obj->instance < MAX_BEAMS));
		other_obj_is_shockwave = ((other_obj->type == OBJ_SHOCKWAVE) && (other_obj->instance >= 0) && (other_obj->instance < MAX_SHOCKWAVES));
	}
	else
	{
		other_obj_is_weapon = 0;
		other_obj_is_beam = 0;
		other_obj_is_shockwave = 0;
	}

	// update lethality of ship doing damage - modified by Goober5000
	if (other_obj_is_weapon || other_obj_is_shockwave) {
		ai_update_lethality(ship_objp, other_obj, damage);
	}

	// damage scaling due to big damage, supercap, etc
	float damage_scale = 1.0f; 
	// if this is a weapon
	if (other_obj_is_weapon){
		// Cyborg - Coverity 1523515, this was the only place in ship_do_damage that we weren't checking weapon_info_index
		Assertion(Weapons[other_obj->instance].weapon_info_index > -1, "Weapon info index in ship_do_damage was found to be a negative value of %d.  Please report this to an SCP coder!", Weapons[other_obj->instance].weapon_info_index);
		if (Weapons[other_obj->instance].weapon_info_index > -1){
			damage_scale = weapon_get_damage_scale(&Weapon_info[Weapons[other_obj->instance].weapon_info_index], other_obj, ship_objp);
		}
	}

	if (other_obj && other_obj->parent >= 0 && Objects[other_obj->parent].signature == other_obj->parent_sig) {
		if(Objects[other_obj->parent].flags[Object::Object_Flags::Player_ship])
			difficulty_scale_factor *= The_mission.ai_profile->player_damage_inflicted_scale[Game_skill_level];
	}

	MONITOR_INC( ShipHits, 1 );

	std::array<std::optional<ConditionData>, NumHitTypes> impact_data = {};

	//	Don't damage player ship in the process of warping out.
	if ( Player->control_mode >= PCM_WARPOUT_STAGE2 )	{
		if ( ship_objp == Player_obj ){
			if (!global_damage) {
				maybe_play_conditional_impacts(impact_data, other_obj, ship_objp, true, submodel_num, hitpos, local_hitpos, hit_normal);
			}
			return;
		}
	}

	if ( other_obj_is_weapon ) {		
		// for tvt and dogfight missions, don't scale damage
		if( (Game_mode & GM_MULTIPLAYER) && ((Netgame.type_flags & NG_TYPE_TEAM) || (Netgame.type_flags & NG_TYPE_DOGFIGHT)) ){
		} 
		else {
			// Do a little "skill" balancing for the player in single player and coop multiplayer
			if (ship_objp->flags[Object::Object_Flags::Player_ship])	{
				// Nuke - store it in a couple factor and we will apply it where needed
				difficulty_scale_factor *= The_mission.ai_profile->player_damage_scale[Game_skill_level];
			}		
		}
	}

	// if this is not a laser, or i'm not a multiplayer client
	// apply pain to me

	// check for nulls, check that the player is the one being hit, and check that we're actually doing the pain flash
	if ((other_obj != nullptr) && (ship_objp == Player_obj) && !(Ship_info[Ships[Player_obj->instance].ship_info_index].flags[Ship::Info_Flags::No_pain_flash]))
	{
		// For the record, ship_hit_pain seems to simply be the red flash that appears
		// on the screen when you're hit.
		int special_check = !MULTIPLAYER_CLIENT;

		// now the actual checks
		if (other_obj->type == OBJ_BEAM)
		{
			Assert((beam_get_weapon_info_index(other_obj) >= 0) && (beam_get_weapon_info_index(other_obj) < weapon_info_size()));
			if (((Weapon_info[beam_get_weapon_info_index(other_obj)].subtype != WP_LASER) || special_check))
			{
				ship_hit_pain(damage * difficulty_scale_factor, quadrant);
			}	
		}
		if (other_obj_is_weapon)
		{
			Assert((Weapons[other_obj->instance].weapon_info_index > -1) && (Weapons[other_obj->instance].weapon_info_index < weapon_info_size()));
			if (((Weapon_info[Weapons[other_obj->instance].weapon_info_index].subtype != WP_LASER) || special_check))
			{
				ship_hit_pain(damage * difficulty_scale_factor, quadrant);
			}
		}
	}


	// If the ship is invulnerable, do nothing
	if (ship_objp->flags[Object::Object_Flags::Invulnerable])	{
		if (!global_damage) {
			maybe_play_conditional_impacts(impact_data, other_obj, ship_objp, true, submodel_num, hitpos, local_hitpos, hit_normal);
		}
		return;
	}

	//	if ship is already dying, shorten deathroll.
	if (shipp->flags[Ship::Ship_Flags::Dying]) {
		if (quadrant >= 0 && !(ship_objp->flags[Object::Object_Flags::No_shields])) {
			impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::SHIELD)] = ConditionData {
				ImpactCondition(shipp->shield_armor_type_idx),
				HitType::SHIELD,
				0.0f,
				// we have to do this annoying thing where we reduce the shield health a bit because it turns out the last X percent of a shield doesn't matter
				MAX(0.0f, ship_objp->shield_quadrant[quadrant] - ship_shield_hitpoint_threshold(ship_objp)),
				shield_get_max_quad(ship_objp) - ship_shield_hitpoint_threshold(ship_objp),
			};
		} else {
			impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::HULL)] = ConditionData {
				ImpactCondition(shipp->armor_type_idx),
				HitType::HULL,
				0.0f,
				ship_objp->hull_strength,
				shipp->ship_max_hull_strength,
			};
		}
		if (!global_damage) {
			maybe_play_conditional_impacts(impact_data, other_obj, ship_objp, true, submodel_num, hitpos, local_hitpos, hit_normal);
		}

		shiphit_hit_after_death(ship_objp, (damage * difficulty_scale_factor));
		return;
	}

	int weapon_info_index = shiphit_get_damage_weapon(other_obj);

	//	If we hit the shield, reduce it's strength and found
	// out how much damage is left over.
	if ( quadrant >= 0 && !(ship_objp->flags[Object::Object_Flags::No_shields]) )	{
//		mprintf(("applying damage ge to shield\n"));
		float shield_damage = damage * damage_scale;

		auto shield_impact = ConditionData {
			ImpactCondition(shipp->shield_armor_type_idx),
			HitType::SHIELD,
			0.0f,
			// we have to do this annoying thing where we reduce the shield health a bit because it turns out the last X percent of a shield doesn't matter
			MAX(0.0f, ship_objp->shield_quadrant[quadrant] - ship_shield_hitpoint_threshold(ship_objp)),
			shield_get_max_quad(ship_objp) - ship_shield_hitpoint_threshold(ship_objp),
		};

		if ( damage > 0.0f ) {
			float piercing_pct = 0.0f;

			// apply any armor types and the difficulty scaling
			if(shipp->shield_armor_type_idx != -1) {
				piercing_pct = Armor_types[shipp->shield_armor_type_idx].GetShieldPiercePCT(damage_type_idx);
				// reduce shield damage by the piercing percent
				shield_damage = shield_damage * (1.0f - piercing_pct);

				// Nuke: this call will decide when to use the damage factor, but it will get used, unless the modder is dumb (like setting +Difficulty Scale Type: to 'manual' and not manually applying it in their calculations)
				shield_damage = Armor_types[shipp->shield_armor_type_idx].GetDamage(shield_damage, damage_type_idx, difficulty_scale_factor, other_obj_is_beam);
			} else {
				shield_damage *= difficulty_scale_factor;
			}

			float shield_factor = 1.0f;
			if (weapon_info_index >= 0 && (!other_obj_is_beam || Beams_use_damage_factors))
				shield_factor = Weapon_info[weapon_info_index].shield_factor;
			
			shield_impact.damage = shield_damage * shield_factor;
			// apply shield damage
			float remaining_damage = shield_apply_damage(ship_objp, quadrant, shield_damage * shield_factor);
			// remove the shield factor, since the overflow will no longer be thrown at shields
			remaining_damage /= shield_factor;
			
			// Unless the backwards compatible flag is on, remove difficulty scaling as well
			// The hull/subsystem code below will re-add it where necessary
			if (!The_mission.ai_profile->flags[AI::Profile_Flags::Carry_shield_difficulty_scaling_bug])
			remaining_damage /= difficulty_scale_factor;
			
			// the rest of the damage is what overflowed from the shield damage and pierced
			damage = remaining_damage + (damage * piercing_pct);
		}

		impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::SHIELD)] = shield_impact;
	}
			
	// Apply leftover damage to the ship's subsystem and hull.
	if ( (damage > 0.0f) )	{
		bool apply_hull_armor = true;

		bool shield_hit = quadrant >= 0;

		// apply damage to subsystems, and get back any remaining damage that needs to go to the hull
		auto damage_pair = do_subobj_hit_stuff(ship_objp, other_obj, hitpos, submodel_num, damage, &apply_hull_armor, hit_dot, shield_hit);

		damage = damage_pair.second;

		impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::SUBSYS)] = damage_pair.first;

		// damage scaling doesn't apply to subsystems, but it does to the hull
		damage *= damage_scale;
		
		// Do armor stuff
		if (apply_hull_armor && shipp->armor_type_idx != -1)		{			
			damage = Armor_types[shipp->armor_type_idx].GetDamage(damage, damage_type_idx, difficulty_scale_factor, other_obj_is_beam);
		} else {
			damage *= difficulty_scale_factor;
		}

		// if weapon is vampiric, slap healing on shooter instead of target
		if (weapon_info_index >= 0) {
			weapon_info* wip = &Weapon_info[weapon_info_index];

			if ((wip->wi_flags[Weapon::Info_Flags::Vampiric]) && (other_obj->parent > 0)) {
				object* parent = &Objects[other_obj->parent];

				if ((parent->type == OBJ_SHIP) && (parent->signature == other_obj->parent_sig)) {
					ship* shipp_parent = &Ships[parent->instance];

					if (!parent->flags[Object::Object_Flags::Should_be_dead]) {
						parent->hull_strength += damage * wip->vamp_regen;

						if (parent->hull_strength > shipp_parent->ship_max_hull_strength) {
							parent->hull_strength = shipp_parent->ship_max_hull_strength;
						}
					}
				}
			}
		}

		// continue with damage?
		if (damage > 0.0f) {
			if ( weapon_info_index >= 0 && (!other_obj_is_beam || Beams_use_damage_factors)) {
				if (Weapon_info[weapon_info_index].wi_flags[Weapon::Info_Flags::Puncture]) {
					damage /= 4;
				}

				damage *= Weapon_info[weapon_info_index].armor_factor;
			}

			// if ship is flagged as can not die, don't let it die
			if (shipp->ship_guardian_threshold > 0) {
				float min_hull_strength = 0.01f * shipp->ship_guardian_threshold * shipp->ship_max_hull_strength;
				if ( (ship_objp->hull_strength - damage) < min_hull_strength ) {
					// find damage needed to take object to min hull strength
					damage = ship_objp->hull_strength - min_hull_strength;

					// make sure damage is positive
					damage = MAX(0.0f, damage);
				}
			}

			if (!shield_hit) {
				impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::HULL)] = ConditionData {
					ImpactCondition(shipp->armor_type_idx),
					HitType::HULL,
					damage,
					ship_objp->hull_strength,
					shipp->ship_max_hull_strength,
				};
			}

			// multiplayer clients don't do damage
			if (((Game_mode & GM_MULTIPLAYER) && MULTIPLAYER_CLIENT)) {
			} else {
				// Check if this is simulated damage.
				if ( weapon_info_index >= 0 ) {
					if (Weapon_info[weapon_info_index].wi_flags[Weapon::Info_Flags::Training]) {
//						diag_printf2("Simulated Hull for Ship %s hit, dropping from %.32f to %d.\n", shipp->ship_name, (int) ( ship_objp->sim_hull_strength * 100 ), (int) ( ( ship_objp->sim_hull_strength - damage ) * 100 ) );
						ship_objp->sim_hull_strength -= damage;
						ship_objp->sim_hull_strength = MAX(0.0f, ship_objp->sim_hull_strength);
						return;
					}
				}
				ship_objp->hull_strength -= damage;
			}

			// let damage gauge know that player ship just took damage
			if ( Player_obj == ship_objp ) {
				hud_gauge_popup_start(HUD_DAMAGE_GAUGE, 5000);
			}
		
			// DB - removed 1/12/99 - scoring code properly bails if MULTIPLAYER_CLIENT
			// in multiplayer, if I am not the host, get out of this function here!!
			//if ( MULTIPLAYER_CLIENT ) {
				//return;
			//}		

			if (other_obj)
			{
				switch (other_obj->type)
				{
					case OBJ_SHOCKWAVE:
						scoring_add_damage(ship_objp,other_obj,damage);
						break;
					case OBJ_ASTEROID:
						// don't call scoring for asteroids
						break;
					case OBJ_WEAPON:
						if((other_obj->parent < 0) || (other_obj->parent >= MAX_OBJECTS)){
							scoring_add_damage(ship_objp, NULL, damage);
						} else {
							scoring_add_damage(ship_objp, &Objects[other_obj->parent], damage);
						}
						break;
					case OBJ_BEAM://give kills for fighter beams-Bobboau
					{
						int bobjn = beam_get_parent(other_obj);

						// Goober5000 - only count beams fired by fighters or bombers unless the ai profile says different
						if (bobjn >= 0)
						{
							if ( !(The_mission.ai_profile->flags[AI::Profile_Flags::Include_beams_in_stat_calcs]) && 
								 !(Ship_info[Ships[Objects[bobjn].instance].ship_info_index].is_fighter_bomber()) &&
								 !(Objects[bobjn].flags[Object::Object_Flags::Player_ship]) ) {
								bobjn = -1;
							}
						}

						if(bobjn == -1){
							scoring_add_damage(ship_objp, NULL, damage);
						} else {
							scoring_add_damage(ship_objp, &Objects[bobjn], damage);
						}
						break;
					  }
					default:
						break;
				}
			}	// other_obj

			if (ship_objp->hull_strength <= 0.0f) {
				MONITOR_INC( ShipNumDied, 1 );

				ship_info	*sip = &Ship_info[shipp->ship_info_index];

				// If massive beam hitting small ship, vaporize  otherwise normal damage pipeline
				// Only vaporize once
				// multiplayer clients should skip this
				if(!MULTIPLAYER_CLIENT) {
					if ( !(shipp->flags[Ship::Ship_Flags::Vaporize]) ) {
						// Only small ships can be vaporized
						if (sip->is_small_ship()) {
							if (other_obj) {	// Goober5000 check for NULL
								if (other_obj->type == OBJ_BEAM)
								{
									int beam_weapon_info_index = beam_get_weapon_info_index(other_obj);
									if ( (beam_weapon_info_index > -1) && (Weapon_info[beam_weapon_info_index].wi_flags[Weapon::Info_Flags::Huge]) ) {
										// Flag as vaporized
										shipp->flags.set(Ship::Ship_Flags::Vaporize);
									}
								}
							}
						}
					}
				}
				
				// maybe engine wash death
				if (wash_damage) {
					shipp->wash_killed = 1;
				}

				float percent_killed = -get_hull_pct(ship_objp, true);
				if (percent_killed > 1.0f){
					percent_killed = 1.0f;
				}

				if ( !(shipp->flags[Ship::Ship_Flags::Dying]) && !MULTIPLAYER_CLIENT) {  // if not killed, then kill
					ship_hit_kill(ship_objp, other_obj, hitpos, percent_killed);
				}
			}
		}
	}

	if (!global_damage) {
		maybe_play_conditional_impacts(impact_data, other_obj, ship_objp, true, submodel_num, hitpos, local_hitpos, hit_normal);
	}

	// handle weapon and afterburner leeching here
	if(other_obj_is_weapon || other_obj_is_beam) {		
		Assert(weapon_info_index >= 0);
		weapon_info* wip = &Weapon_info[weapon_info_index];

		float mult = 1.0f;
		if (other_obj_is_beam)
			mult = flFrametime;

		// if its a leech weapon - NOTE - unknownplayer: Perhaps we should do something interesting like direct the leeched energy into the attacker ?
		if (wip->wi_flags[Weapon::Info_Flags::Energy_suck]) {
			// reduce afterburner fuel
			shipp->afterburner_fuel -= wip->afterburner_reduce * mult;
			shipp->afterburner_fuel = (shipp->afterburner_fuel < 0.0f) ? 0.0f : shipp->afterburner_fuel;

			// reduce weapon energy
			shipp->weapon_energy -= wip->weapon_reduce * mult;
			shipp->weapon_energy = (shipp->weapon_energy < 0.0f) ? 0.0f : shipp->weapon_energy;
		}
	}
}

static void ship_do_healing(object* ship_objp, const object* other_obj, const vec3d* hitpos, float healing, int quadrant, int submodel_num, int damage_type_idx = -1, const vec3d* hit_normal = nullptr, const vec3d* local_hitpos = nullptr)
{
	// multiplayer clients dont do healing
	if (MULTIPLAYER_CLIENT)
		return;

	ship* shipp;
	bool other_obj_is_weapon, other_obj_is_beam, other_obj_is_shockwave;

	Assertion(ship_objp, "No ship_objp in ship_do_healing!");	
	Assertion(other_obj, "No other_obj in ship_do_healing!");
	Assertion(hitpos, "No hitpos in ship_do_healing!");

	Assertion(ship_objp->instance >= 0 && ship_objp->type == OBJ_SHIP, "invalid ship target in ship_do_healing");
	shipp = &Ships[ship_objp->instance];

	Assert(other_obj->type == OBJ_WEAPON || other_obj->type == OBJ_BEAM || other_obj->type == OBJ_SHOCKWAVE);

	// maybe adjust "damage" done by shockwave for BIG|HUGE
	maybe_shockwave_damage_adjust(ship_objp, other_obj, &healing);

	other_obj_is_weapon = ((other_obj->type == OBJ_WEAPON) && (other_obj->instance >= 0) && (other_obj->instance < MAX_WEAPONS));
	other_obj_is_beam = ((other_obj->type == OBJ_BEAM) && (other_obj->instance >= 0) && (other_obj->instance < MAX_BEAMS));
	other_obj_is_shockwave = ((other_obj->type == OBJ_SHOCKWAVE) && (other_obj->instance >= 0) && (other_obj->instance < MAX_SHOCKWAVES));
	
	MONITOR_INC(ShipHits, 1);

	std::array<std::optional<ConditionData>, NumHitTypes> impact_data = {};

	//	Don't heal player ship in the process of warping out.
	if ((Player->control_mode >= PCM_WARPOUT_STAGE2) && (ship_objp == Player_obj)) {
		if (!global_damage) {
			maybe_play_conditional_impacts(impact_data, other_obj, ship_objp, true, submodel_num, hitpos, local_hitpos, hit_normal);
		}
		return;
	}

	int wip_index = -1;
	if (other_obj_is_weapon)
		wip_index = Weapons[other_obj->instance].weapon_info_index;
	else if (other_obj_is_beam)
		wip_index = Beams[other_obj->instance].weapon_info_index;
	else if (other_obj_is_shockwave)
		wip_index = shockwave_get_weapon_index(other_obj->instance);

	if (wip_index < 0)
		return;
	weapon_info* wip = &Weapon_info[wip_index];

	float shield_health;
	if (quadrant >= 0) {
		shield_health = MAX(0.0f, ship_objp->shield_quadrant[quadrant] - ship_shield_hitpoint_threshold(ship_objp, false));
	} else {
		//if we haven't hit a shield, assume the shield is fully depleted, because we have no way of knowing what the relevant quadrant would be
		shield_health = 0.f;
	}

	// handle shield healing
	if (!(ship_objp->flags[Object::Object_Flags::No_shields])) {
		auto shield_impact = ConditionData {
			ImpactCondition(shipp->shield_armor_type_idx),
			HitType::SHIELD,
			0.0f,
			shield_health,
			shield_get_max_quad(ship_objp) - ship_shield_hitpoint_threshold(ship_objp, false),
		};

		float shield_healing = healing * wip->shield_factor;

		if (shield_healing > 0.0f) {
			if (shipp->shield_armor_type_idx != -1)
				shield_healing = Armor_types[shipp->shield_armor_type_idx].GetDamage(shield_healing, damage_type_idx, 1.0f, other_obj_is_beam);

			shield_impact.damage = shield_healing;
			shield_apply_healing(ship_objp, shield_healing);
		}
		impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::SHIELD)] = shield_impact;
	}

	// now for subsystems and hull
	if ((healing > 0.0f)) {

		auto healing_pair = do_subobj_heal_stuff(ship_objp, other_obj, hitpos, submodel_num, healing);

		healing = healing_pair.second;

		impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::SUBSYS)] = healing_pair.first;

		//Do armor stuff
		if (shipp->armor_type_idx != -1)
			healing = Armor_types[shipp->armor_type_idx].GetDamage(healing, damage_type_idx, 1.0f, other_obj_is_beam);
		
		if (wip->wi_flags[Weapon::Info_Flags::Puncture])
			healing /= 4;

		healing *= wip->armor_factor;

		impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::HULL)] = ConditionData {
			ImpactCondition(shipp->armor_type_idx),
			HitType::HULL,
			healing,
			ship_objp->hull_strength,
			shipp->ship_max_hull_strength,
		};

		ship_objp->hull_strength += healing;
		if (ship_objp->hull_strength > shipp->ship_max_hull_strength)
			ship_objp->hull_strength = shipp->ship_max_hull_strength;
	}

	maybe_play_conditional_impacts(impact_data, other_obj, ship_objp, true, submodel_num, hitpos, local_hitpos, hit_normal);

	// fix up the ship's sparks :)
	// turn off a random spark, if its a beam, do this on average twice a second
	if(!other_obj_is_beam || frand() > flFrametime * 2.0f )
		shipp->sparks[Random::next(MAX_SHIP_SPARKS)].end_time = timestamp(0);

	// if we brought it to full health, fix ALL sparks
	if (ship_objp->hull_strength == shipp->ship_max_hull_strength) {
		for(auto & spark : shipp->sparks )
			spark.end_time = timestamp(0);
	}

	// handle weapon and afterburner leeching here
	if (other_obj_is_weapon || other_obj_is_beam) {
		float mult = 1.0f;
		if (other_obj_is_beam)
			mult = flFrametime;

		// if its a leech weapon - NOTE - unknownplayer: Perhaps we should do something interesting like direct the leeched energy into the attacker ?
		if (wip->wi_flags[Weapon::Info_Flags::Energy_suck]) {
			// reduce afterburner fuel
			shipp->afterburner_fuel -= wip->afterburner_reduce * mult;
			shipp->afterburner_fuel = (shipp->afterburner_fuel < 0.0f) ? 0.0f : shipp->afterburner_fuel;

			// reduce weapon energy
			shipp->weapon_energy -= wip->weapon_reduce * mult;
			shipp->weapon_energy = (shipp->weapon_energy < 0.0f) ? 0.0f : shipp->weapon_energy;
		}
	}
}

// Goober5000
// (it might be possible to make `target` const, but that would set off another const-cascade)
void ship_apply_tag(ship *shipp, int tag_level, float tag_time, object *target, const vec3d *start, int ssm_index, int ssm_team)
{
	// set time first tagged
	if (shipp->time_first_tagged == 0)
		shipp->time_first_tagged = Missiontime;

	// do tag effect
	if (tag_level == 1)
	{
//		mprintf(("TAGGED %s for %f seconds\n", shipp->ship_name, tag_time));
		shipp->tag_left = tag_time;
		shipp->tag_total = tag_time;
	}
	else if (tag_level == 2)
	{
//		mprintf(("Level 2 TAGGED %s for %f seconds\n", shipp->ship_name, tag_time));
		shipp->level2_tag_left = tag_time;
		shipp->level2_tag_total = tag_time;
	}
	else if (tag_level == 3)
	{
		// tag C creates an SSM strike, yay -Bobboau
		Assert(target);
		Assert(start);
		if (ssm_index < 0)	// TAG-C? Is that you? -MageKing17
			return;

		ssm_create(target, start, ssm_index, NULL, ssm_team);
	}
}

// This gets called to apply damage when something hits a particular point on a ship.
// This assumes that whoever called this knows if the shield got hit or not.
// hitpos is in world coordinates.
// if quadrant is not -1, then that part of the shield takes damage properly.
// (it might be possible to make `other_obj` const, but that would set off another const-cascade)
void ship_apply_local_damage(object *ship_objp, object *other_obj, const vec3d *hitpos, float damage, int damage_type_idx, int quadrant, bool create_spark, int submodel_num, const vec3d *hit_normal, float hit_dot, const vec3d* local_hitpos)
{
	Assert(ship_objp);	// Goober5000
	Assert(other_obj);	// Goober5000

	ship *ship_p = &Ships[ship_objp->instance];
    weapon *wp = &Weapons[other_obj->instance];
	bool create_sparks = true;

	//	If got hit by a weapon, tell the AI so it can react.  Only do this line in single player,
	// or if I am the master in a multiplayer game
	if ((other_obj->type == OBJ_WEAPON) && ( !(Game_mode & GM_MULTIPLAYER) || MULTIPLAYER_MASTER )) {
		//	If weapon hits ship on same team and that ship not targeted and parent of weapon not player,
		//	don't do damage.
		//	Ie, player can always do damage.  AI can only damage team if that ship is targeted.
		if (wp->target_num != OBJ_INDEX(ship_objp)) {
			if ((ship_p->team == wp->team) && !(Objects[other_obj->parent].flags[Object::Object_Flags::Player_ship]) ) {
				// need to play the impact effect(s) for the weapon if we have one, since we won't get the chance to do it later
				// we won't account for subsystems; that's a lot of extra logic for little benefit in this edge case
				std::array<std::optional<ConditionData>, NumHitTypes> impact_data = {};
				if (quadrant >= 0 && !(ship_objp->flags[Object::Object_Flags::No_shields])) {
					impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::SHIELD)] = ConditionData {
						ImpactCondition(ship_p->shield_armor_type_idx),
						HitType::SHIELD,
						0.0f,
						// we have to do this annoying thing where we reduce the shield health a bit because it turns out the last X percent of a shield doesn't matter
						MAX(0.0f, ship_objp->shield_quadrant[quadrant] - ship_shield_hitpoint_threshold(ship_objp)),
						shield_get_max_quad(ship_objp) - ship_shield_hitpoint_threshold(ship_objp),
					};
				} else {
					impact_data[static_cast<std::underlying_type_t<HitType>>(HitType::HULL)] = ConditionData {
						ImpactCondition(ship_p->armor_type_idx),
						HitType::HULL,
						0.0f,
						ship_objp->hull_strength,
						ship_p->ship_max_hull_strength,
					};
				}
				maybe_play_conditional_impacts(impact_data, other_obj, ship_objp, true, submodel_num, hitpos, local_hitpos, hit_normal);
				return;
			}
		}
	}

	// only want to check the following in single player or if I am the multiplayer game server
	// Added OBJ_BEAM for traitor detection - FUBAR
	if ( !MULTIPLAYER_CLIENT && ((other_obj->type == OBJ_SHIP) || (other_obj->type == OBJ_WEAPON) || (other_obj->type == OBJ_BEAM)) ) {
		ai_ship_hit(ship_objp, other_obj, hit_normal);
	}

	//	Cut damage done on the player by 4x in training missions, but do full accredidation
	if ( The_mission.game_type & MISSION_TYPE_TRAINING ){
		if (ship_objp == Player_obj){
			damage /= 4.0f;
		}
	}

	// maybe tag the ship
	if(!MULTIPLAYER_CLIENT && (other_obj->type == OBJ_WEAPON || other_obj->type == OBJ_BEAM)) {
		weapon_info *wip = NULL;

		if (other_obj->type == OBJ_WEAPON)
			wip = &Weapon_info[Weapons[other_obj->instance].weapon_info_index];
		else if (other_obj->type == OBJ_BEAM)
			wip = &Weapon_info[Beams[other_obj->instance].weapon_info_index];

		Assert(wip != NULL);

		if (wip->wi_flags[Weapon::Info_Flags::Tag]) {
			ship_apply_tag(ship_p, wip->tag_level, wip->tag_time, ship_objp, hitpos, wip->SSM_index, wp->team);
		}
	}

	if ( Event_Music_battle_started == 0 )	{
		ship_hit_music(ship_objp, other_obj);
	}

	if (damage < 0.0f){
		damage = 0.0f;
	}

	// evaluate any possible player stats implications
	scoring_eval_hit(ship_objp,other_obj);

	int wip_index = -1;
	if (other_obj->type == OBJ_WEAPON)
		wip_index = Weapons[other_obj->instance].weapon_info_index;
	else if (other_obj->type == OBJ_BEAM)
		wip_index = Beams[other_obj->instance].weapon_info_index;
	else if (other_obj->type == OBJ_SHOCKWAVE)
		wip_index = shockwave_get_weapon_index(other_obj->instance);

	global_damage = false;
	if (wip_index >= 0 && Weapon_info[wip_index].wi_flags[Weapon::Info_Flags::Heals]) {
		ship_do_healing(ship_objp, other_obj, hitpos, damage, quadrant, submodel_num, -1, hit_normal, local_hitpos);
		create_sparks = false;
	} else {
		ship_do_damage(ship_objp, other_obj, hitpos, damage, quadrant, submodel_num, damage_type_idx, false, hit_dot, hit_normal, local_hitpos);
	}

	// DA 5/5/98: move ship_hit_create_sparks() after do_damage() since number of sparks depends on hull strength
	// doesn't hit shield and we want sparks
	if ((quadrant == MISS_SHIELDS) && create_spark)	{
		// check if subsys destroyed
		if ( !is_subsys_destroyed(ship_p, submodel_num) ) {
			// Simulated weapons don't cause sparks
			if(other_obj->type == OBJ_WEAPON || other_obj->type == OBJ_BEAM) {
				weapon_info *wip = NULL;

				if (other_obj->type == OBJ_WEAPON)
					wip = &Weapon_info[Weapons[other_obj->instance].weapon_info_index];
				else if (other_obj->type == OBJ_BEAM)
					wip = &Weapon_info[Beams[other_obj->instance].weapon_info_index];

				Assert(wip != NULL);

				if (wip->wi_flags[Weapon::Info_Flags::Training] || wip->wi_flags[Weapon::Info_Flags::No_impact_spew]) {
					create_sparks = false;
				}
			}

			if (create_sparks) {
				auto subsys = ship_get_subsys_for_submodel(ship_p, submodel_num);

				if (subsys != nullptr && subsys->system_info->flags[Model::Subsystem_Flags::No_sparks]) {
					// Spark creation was explicitly disabled for this subsystem
					create_sparks = false;
				}
			}

			if (create_sparks) {
				ship_hit_create_sparks(ship_objp, hitpos, submodel_num);
			}
		}
		//fireball_create( hitpos, FIREBALL_SHIP_EXPLODE1, OBJ_INDEX(ship_objp), 0.25f );
	}
}

// This gets called to apply damage when a damaging force hits a ship, but at no 
// point in particular.  Like from a shockwave.   This routine will see if the
// shield got hit and if so, apply damage to it.
// You can pass force_center==NULL if you the damage doesn't come from anywhere,
// like for debug keys to damage an object or something.  It will 
// assume damage is non-directional and will apply it correctly.   
void ship_apply_global_damage(object *ship_objp, object *other_obj, const vec3d *force_center, float damage, int damage_type_idx)
{
	Assert(ship_objp);	// Goober5000 (but not other_obj in case of sexp)

	vec3d tmp, world_hitpos;
	global_damage = true;

	if ( force_center )	{
		int shield_quad;
		vec3d local_hitpos;

		// find world hitpos
		vm_vec_sub( &tmp, force_center, &ship_objp->pos );
		vm_vec_normalize_safe( &tmp );
		vm_vec_scale_add( &world_hitpos, &ship_objp->pos, &tmp, ship_objp->radius );

		// Rotate world_hitpos into local coordinates (local_hitpos)
		vm_vec_sub(&tmp, &world_hitpos, &ship_objp->pos );
		vm_vec_rotate( &local_hitpos, &tmp, &ship_objp->orient );

		// shield_quad = quadrant facing the force_center
		shield_quad = get_quadrant(&local_hitpos, ship_objp);

		int wip_index = -1;
		if(other_obj != nullptr && other_obj->type == OBJ_SHOCKWAVE)
			wip_index = shockwave_get_weapon_index(other_obj->instance);

		// the healing case should only ever be true for shockwaves
		if (wip_index >= 0 && Weapon_info[wip_index].wi_flags[Weapon::Info_Flags::Heals])
			ship_do_healing(ship_objp, other_obj, &world_hitpos, damage, -1, damage_type_idx);
		else
			// Do damage on local point		
			ship_do_damage(ship_objp, other_obj, &world_hitpos, damage, shield_quad, -1 , damage_type_idx);
	} else {
		// Since an force_center wasn't specified, this is probably just a debug key
		// to kill an object.   So pick a shield quadrant and a point on the
		// radius of the object.   
		vm_vec_scale_add( &world_hitpos, &ship_objp->pos, &ship_objp->orient.vec.fvec, ship_objp->radius );

		int n_quadrants = static_cast<int>(ship_objp->shield_quadrant.size());
		for (int i=0; i<n_quadrants; i++){
			ship_do_damage(ship_objp, other_obj, &world_hitpos, damage/n_quadrants, i, -1, damage_type_idx);
		}
	}

	// AL 3-30-98: Show flashing blast icon if player ship has taken blast damage
	if ( ship_objp == Player_obj ) {
		// only show blast icon if playing on medium skill or lower -> unknownplayer: why? I think this should be changed.
		// Goober5000 - agreed; commented out
		//if ( Game_skill_level <= 2 ) {
			hud_start_text_flash(XSTR("Blast", 1428), 2000);
		//}
	}

	// evaluate any player stats scoring conditions (specifically, blasts from remotely detonated secondary weapons)
	scoring_eval_hit(ship_objp,other_obj,1);
}

void ship_apply_wash_damage(object *ship_objp, object *other_obj, float damage)
{
	vec3d world_hitpos, direction_vec, rand_vec;

	// Since an force_center wasn't specified, this is probably just a debug key
	// to kill an object.   So pick a shield quadrant and a point on the
	// radius of the object
	vm_vec_rand_vec_quick(&rand_vec);
	vm_vec_scale_add(&direction_vec, &ship_objp->orient.vec.fvec, &rand_vec, 0.5f);
	vm_vec_normalize_quick(&direction_vec);
	vm_vec_scale_add( &world_hitpos, &ship_objp->pos, &direction_vec, ship_objp->radius );

	// Do damage to hull and not to shields
	global_damage = true;
	ship_do_damage(ship_objp, other_obj, &world_hitpos, damage, -1, -1, -1, true);

	// AL 3-30-98: Show flashing blast icon if player ship has taken blast damage
	if ( ship_objp == Player_obj ) {
		// only show blast icon if playing on medium skill or lower
		// Goober5000 - commented out
		//if ( Game_skill_level <= 2 ) {
			hud_start_text_flash(XSTR("Engine Wash", 1429), 2000);
		//}
	}

	// evaluate any player stats scoring conditions (specifically, blasts from remotely detonated secondary weapons)
	scoring_eval_hit(ship_objp,other_obj,1);
}

// player pain
void ship_hit_pain(float damage, int quadrant)
{

	ship *shipp = &Ships[Player_obj->instance];
	ship_info *sip = &Ship_info[shipp->ship_info_index];

    if (!(Player_obj->flags[Object::Object_Flags::Invulnerable]))
    {
		int pain_type;
		if (Shield_pain_flash_factor != 0.0f && quadrant >= 0)
		{
			float effect = (Shield_pain_flash_factor * Player_obj->shield_quadrant[quadrant] * static_cast<int>(Player_obj->shield_quadrant.size())) / shield_get_max_strength(Player_obj);

			if (Shield_pain_flash_factor < 0.0f)
				effect -= Shield_pain_flash_factor;
			
			game_flash((sip->shield_color[0] * effect) / 255.0f, (sip->shield_color[1] * effect) / 255.0f, (sip->shield_color[2] * effect) / 255.0f);
			pain_type = 0;
		}
		else
		{
			game_flash(damage * Generic_pain_flash_factor / 15.0f, -damage * Generic_pain_flash_factor / 30.0f, -damage * Generic_pain_flash_factor / 30.0f);
			pain_type = 1;
		}

		if (OnPainFlashHook->isActive())
		{
			// add scripting hook for 'On Pain Flash' --wookieejedi
			OnPainFlashHook->run(scripting::hook_param_list(scripting::hook_param("Pain_Type", 'i', pain_type)));
		}
    }

	// kill any active popups when you get hit.
	if ( Game_mode & GM_MULTIPLAYER ){
		popup_kill_any_active();
	}
}
