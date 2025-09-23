#include "sexp_opf_core.h"
#include "common.h"

#include "ai/ailua.h"
#include "asteroid/asteroid.h"
#include "gamesnd/eventmusic.h"
#include "hud/hudartillery.h"
#include "hud/hudsquadmsg.h"
#include "iff_defs/iff_defs.h"
#include "jumpnode/jumpnode.h"
#include "localization/localize.h"
#include "menuui/techmenu.h"
#include "mission/missioncampaign.h"
#include "mission/missiongoals.h"
#include "nebula/neb.h"
#include "nebula/neblightning.h"
#include "ship/ship.h"
#include "starfield/starfield.h"
#include "stats/medals.h"
#include "weapon/weapon.h"

// SexpListItem

void SexpListItem::set_op(int op_num)
{
	if (op_num >= FIRST_OP) { // do we have an op value instead of an op number (index)?
		for (int i = 0; i < (int)Operators.size(); i++)
			if (op_num == Operators[i].value)
				op_num = i; // convert op value to op number
	}

	op = op_num;

	Assertion(SCP_vector_inbounds(Operators, op), "Invalid operator number %d", op);

	text = Operators[op].text;
	type = (SEXPT_OPERATOR | SEXPT_VALID);
}
void SexpListItem::set_data(const char* str, int t)
{
	op = -1;
	text = str ? str : "";
	type = t;
}
void SexpListItem::add_op(int op_num)
{
	SexpListItem* tail = this;
	while (tail->next)
		tail = tail->next;

	auto* n = new SexpListItem();
	n->set_op(op_num);
	tail->next = n;
}
void SexpListItem::add_data(const char* str, int t)
{
	SexpListItem* tail = this;
	while (tail->next)
		tail = tail->next;

	auto* n = new SexpListItem();
	n->set_data(str, t);
	tail->next = n;
}
void SexpListItem::add_list(SexpListItemPtr list)
{
	if (!list)
		return;

	// Append the entire list as-is to preserve order
	SexpListItem* tail = this;
	while (tail->next)
		tail = tail->next;
	tail->next = list.release();
}
void SexpListItem::destroy()
{
	SexpListItem* p = this;
	while (p) {
		SexpListItem* next = p->next;
		delete p;
		p = next;
	}
}

extern SCP_vector<game_snd> Snds;

template <typename M, typename T, typename PTM>
static void add_flag_name_helper(M& flag_name_map, SexpListItem& head, T flag_name_array[], PTM T::* member, size_t flag_name_count)
{
	for (size_t i = 0; i < flag_name_count; i++)
	{
		auto name = flag_name_array[i].*member;
		if (flag_name_map.count(name) == 0)
		{
			head.add_data(name);
			flag_name_map.insert(name);
		}
	}
}

// given a node's parent, check if node is eligible for being used with the special argument
bool SexpOpfListBuilder::is_node_eligible_for_special_argument(int parent_node) const
{
	Assertion(parent_node != -1,
		"Attempt to access invalid parent node for special arg eligibility check. Please report!");

	const int w_arg = find_ancestral_argument_number(OP_WHEN_ARGUMENT, parent_node);
	const int e_arg = find_ancestral_argument_number(OP_EVERY_TIME_ARGUMENT, parent_node);
	return w_arg >= 1 || e_arg >= 1; /* || the same for any future _ARGUMENT sexps */
}

// Goober5000
// backtrack through parents until we find the operator matching
// parent_op, then find the argument we went through
int SexpOpfListBuilder::find_ancestral_argument_number(int parent_op, int child_node) const
{
	if (child_node == -1)
		return -1;

	int parent_node;
	int current_node;

	current_node = child_node;
	parent_node = tree_nodes[current_node].parent;

	while (parent_node >= 0) {
		// check if the parent operator is the one we're looking for
		if (get_operator_const(tree_nodes[parent_node].text.c_str()) == parent_op)
			return find_argument_number(parent_node, current_node);

		// continue iterating up the tree
		current_node = parent_node;
		parent_node = tree_nodes[current_node].parent;
	}

	// not found
	return -1;
}

// Goober5000
int SexpOpfListBuilder::find_argument_number(int parent_node, int child_node) const
{
	int arg_num, current_node;

	// code moved/adapted from match_closest_operator
	arg_num = 0;
	current_node = tree_nodes[parent_node].child;
	while (current_node >= 0) {
		// found?
		if (current_node == child_node)
			return arg_num;

		// continue iterating
		arg_num++;
		current_node = tree_nodes[current_node].next;
	}

	// not found
	return -1;
}

// This is the only public entry point to the class and it dispatches to the correct OPF lister to return
SexpListItemPtr SexpOpfListBuilder::buildListing(int opf, int parent_node, int arg_index)
{
	SexpListItem head;
	SexpListItemPtr list = nullptr;

	switch (opf) {
	case OPF_NONE:
		list = nullptr;
		break;

	case OPF_NULL:
		list = get_listing_opf_null();
		break;

	case OPF_BOOL:
		list = get_listing_opf_bool(parent_node);
		break;

	case OPF_NUMBER:
		list = get_listing_opf_number();
		break;

	case OPF_SHIP:
		list = get_listing_opf_ship(parent_node);
		break;

	case OPF_WING:
		list = get_listing_opf_wing();
		break;

	case OPF_AWACS_SUBSYSTEM:
	case OPF_ROTATING_SUBSYSTEM:
	case OPF_TRANSLATING_SUBSYSTEM:
	case OPF_SUBSYSTEM:
		list = get_listing_opf_subsystem(parent_node, arg_index);
		break;

	case OPF_SUBSYSTEM_TYPE:
		list = get_listing_opf_subsystem_type(parent_node);
		break;

	case OPF_POINT:
		list = get_listing_opf_point();
		break;

	case OPF_IFF:
		list = get_listing_opf_iff();
		break;

	case OPF_AI_CLASS:
		list = get_listing_opf_ai_class();
		break;

	case OPF_SUPPORT_SHIP_CLASS:
		list = get_listing_opf_support_ship_class();
		break;

	case OPF_SSM_CLASS:
		list = get_listing_opf_ssm_class();
		break;

	case OPF_ARRIVAL_LOCATION:
		list = get_listing_opf_arrival_location();
		break;

	case OPF_DEPARTURE_LOCATION:
		list = get_listing_opf_departure_location();
		break;

	case OPF_ARRIVAL_ANCHOR_ALL:
		list = get_listing_opf_arrival_anchor_all();
		break;

	case OPF_SHIP_WITH_BAY:
		list = get_listing_opf_ship_with_bay();
		break;

	case OPF_SOUNDTRACK_NAME:
		list = get_listing_opf_soundtrack_name();
		break;

	case OPF_AI_GOAL:
		list = get_listing_opf_ai_goal(parent_node);
		break;

	case OPF_FLEXIBLE_ARGUMENT:
		list = get_listing_opf_flexible_argument();
		break;

	case OPF_DOCKER_POINT:
		list = get_listing_opf_docker_point(parent_node, arg_index);
		break;

	case OPF_DOCKEE_POINT:
		list = get_listing_opf_dockee_point(parent_node);
		break;

	case OPF_MESSAGE:
		list = get_listing_opf_message();
		break;

	case OPF_WHO_FROM:
		list = get_listing_opf_who_from();
		break;

	case OPF_PRIORITY:
		list = get_listing_opf_priority();
		break;

	case OPF_WAYPOINT_PATH:
		list = get_listing_opf_waypoint_path();
		break;

	case OPF_POSITIVE:
		list = get_listing_opf_positive();
		break;

	case OPF_MISSION_NAME:
		list = get_listing_opf_mission_name();
		break;

	case OPF_SHIP_POINT:
		list = get_listing_opf_ship_point();
		break;

	case OPF_GOAL_NAME:
		list = get_listing_opf_goal_name(parent_node);
		break;

	case OPF_SHIP_WING:
		list = get_listing_opf_ship_wing();
		break;

	case OPF_SHIP_WING_WHOLETEAM:
		list = get_listing_opf_ship_wing_wholeteam();
		break;

	case OPF_SHIP_WING_SHIPONTEAM_POINT:
		list = get_listing_opf_ship_wing_shiponteam_point();
		break;

	case OPF_SHIP_WING_POINT:
		list = get_listing_opf_ship_wing_point();
		break;

	case OPF_SHIP_WING_POINT_OR_NONE:
		list = get_listing_opf_ship_wing_point_or_none();
		break;

	case OPF_ORDER_RECIPIENT:
		list = get_listing_opf_order_recipient();
		break;

	case OPF_SHIP_TYPE:
		list = get_listing_opf_ship_type();
		break;

	case OPF_KEYPRESS:
		list = get_listing_opf_keypress();
		break;

	case OPF_EVENT_NAME:
		list = get_listing_opf_event_name(parent_node);
		break;

	case OPF_AI_ORDER:
		list = get_listing_opf_ai_order();
		break;

	case OPF_SKILL_LEVEL:
		list = get_listing_opf_skill_level();
		break;

	case OPF_CARGO:
		list = get_listing_opf_cargo();
		break;

	case OPF_STRING:
		list = get_listing_opf_string();
		break;

	case OPF_MEDAL_NAME:
		list = get_listing_opf_medal_name();
		break;

	case OPF_WEAPON_NAME:
		list = get_listing_opf_weapon_name();
		break;

	case OPF_INTEL_NAME:
		list = get_listing_opf_intel_name();
		break;

	case OPF_SHIP_CLASS_NAME:
		list = get_listing_opf_ship_class_name();
		break;

	case OPF_HUGE_WEAPON:
		list = get_listing_opf_huge_weapon();
		break;

	case OPF_SHIP_NOT_PLAYER:
		list = get_listing_opf_ship_not_player();
		break;

	case OPF_SHIP_OR_NONE:
		list = get_listing_opf_ship_or_none();
		break;

	case OPF_SUBSYSTEM_OR_NONE:
		list = get_listing_opf_subsystem_or_none(parent_node, arg_index);
		break;

	case OPF_SUBSYS_OR_GENERIC:
		list = get_listing_opf_subsys_or_generic(parent_node, arg_index);
		break;

	case OPF_JUMP_NODE_NAME:
		list = get_listing_opf_jump_nodes();
		break;

	case OPF_VARIABLE_NAME:
		list = get_listing_opf_variable_names();
		break;

	case OPF_AMBIGUOUS:
		list = nullptr;
		break;

	case OPF_ANYTHING:
		list = nullptr;
		break;

	case OPF_SKYBOX_MODEL_NAME:
		list = get_listing_opf_skybox_model();
		break;

	case OPF_SKYBOX_FLAGS:
		list = get_listing_opf_skybox_flags();
		break;

	case OPF_BACKGROUND_BITMAP:
		list = get_listing_opf_background_bitmap();
		break;

	case OPF_SUN_BITMAP:
		list = get_listing_opf_sun_bitmap();
		break;

	case OPF_NEBULA_STORM_TYPE:
		list = get_listing_opf_nebula_storm_type();
		break;

	case OPF_NEBULA_POOF:
		list = get_listing_opf_nebula_poof();
		break;

	case OPF_TURRET_TARGET_ORDER:
		list = get_listing_opf_turret_target_order();
		break;

	case OPF_TURRET_TYPE:
		list = get_listing_opf_turret_types();
		break;

	case OPF_TARGET_PRIORITIES:
		list = get_listing_opf_turret_target_priorities();
		break;

	case OPF_ARMOR_TYPE:
		list = get_listing_opf_armor_type();
		break;

	case OPF_DAMAGE_TYPE:
		list = get_listing_opf_damage_type();
		break;

	case OPF_ANIMATION_TYPE:
		list = get_listing_opf_animation_type();
		break;

	case OPF_PERSONA:
		list = get_listing_opf_persona();
		break;

	case OPF_POST_EFFECT:
		list = get_listing_opf_post_effect();
		break;

	case OPF_FONT:
		list = get_listing_opf_font();
		break;

	case OPF_HUD_ELEMENT:
		list = get_listing_opf_hud_elements();
		break;

	case OPF_SOUND_ENVIRONMENT:
		list = get_listing_opf_sound_environment();
		break;

	case OPF_SOUND_ENVIRONMENT_OPTION:
		list = get_listing_opf_sound_environment_option();
		break;

	case OPF_AUDIO_VOLUME_OPTION:
		list = get_listing_opf_adjust_audio_volume();
		break;

	case OPF_EXPLOSION_OPTION:
		list = get_listing_opf_explosion_option();
		break;

	case OPF_WEAPON_BANK_NUMBER:
		list = get_listing_opf_weapon_banks();
		break;

	//TODO does this version need to allow <any string>????
	case OPF_MESSAGE_OR_STRING:
		list = get_listing_opf_message();
		break;

	case OPF_BUILTIN_HUD_GAUGE:
		list = get_listing_opf_builtin_hud_gauge();
		break;

	case OPF_CUSTOM_HUD_GAUGE:
		list = get_listing_opf_custom_hud_gauge();
		break;

	case OPF_ANY_HUD_GAUGE:
		list = get_listing_opf_any_hud_gauge();
		break;

	case OPF_SHIP_EFFECT:
		list = get_listing_opf_ship_effect();
		break;

	case OPF_MISSION_MOOD:
		list = get_listing_opf_mission_moods();
		break;

	case OPF_SHIP_FLAG:
		list = get_listing_opf_ship_flags();
		break;

	case OPF_WING_FLAG:
		list = get_listing_opf_wing_flags();
		break;

	case OPF_TEAM_COLOR:
		list = get_listing_opf_team_colors();
		break;

	case OPF_NEBULA_PATTERN:
		list = get_listing_opf_nebula_patterns();
		break;

	case OPF_GAME_SND:
		list = get_listing_opf_game_snds();
		break;

	case OPF_FIREBALL:
		list = get_listing_opf_fireball();
		break;

	case OPF_SPECIES:
		list = get_listing_opf_species();
		break;

	case OPF_LANGUAGE:
		list = get_listing_opf_language();
		break;

	case OPF_FUNCTIONAL_WHEN_EVAL_TYPE:
		list = get_listing_opf_functional_when_eval_type();
		break;

	case OPF_ANIMATION_NAME:
		list = get_listing_opf_animation_name(parent_node);
		break;

	case OPF_CONTAINER_NAME:
		list = get_listing_opf_sexp_containers(ContainerType::LIST | ContainerType::MAP);
		break;

	case OPF_LIST_CONTAINER_NAME:
		list = get_listing_opf_sexp_containers(ContainerType::LIST);
		break;

	case OPF_MAP_CONTAINER_NAME:
		list = get_listing_opf_sexp_containers(ContainerType::MAP);
		break;

	case OPF_CONTAINER_VALUE:
		list = nullptr;
		break;

	case OPF_DATA_OR_STR_CONTAINER:
		list = nullptr;
		break;

	case OPF_ASTEROID_TYPES:
		list = get_listing_opf_asteroid_types();
		break;

	case OPF_DEBRIS_TYPES:
		list = get_listing_opf_debris_types();
		break;

	case OPF_WING_FORMATION:
		list = get_listing_opf_wing_formation();
		break;

	case OPF_MOTION_DEBRIS:
		list = get_listing_opf_motion_debris();
		break;

	case OPF_BOLT_TYPE:
		list = get_listing_opf_bolt_types();
		break;

	case OPF_TRAITOR_OVERRIDE:
		list = get_listing_opf_traitor_overrides();
		break;

	case OPF_LUA_GENERAL_ORDER:
		list = get_listing_opf_lua_general_orders();
		break;

	case OPF_MESSAGE_TYPE:
		list = get_listing_opf_message_types();
		break;

	case OPF_CHILD_LUA_ENUM:
		list = get_listing_opf_lua_enum(parent_node, arg_index);
		break;

	case OPF_MISSION_CUSTOM_STRING:
		list = get_listing_opf_mission_custom_strings();
		break;

	default:
		// We're at the end of the list so check for any dynamic enums
		list = check_for_dynamic_sexp_enum(opf);
		break;
	}

	// skip OPF_NONE, also skip for OPF_NULL, because it takes no data (though it can take plenty of operators)
	if (opf == OPF_NULL || opf == OPF_NONE) {
		return list;
	}

	// skip the special argument if we aren't at the right spot in when-argument or
	// every-time-argument
	if (!is_node_eligible_for_special_argument(parent_node)) {
		return list;
	}

	// the special item is a string and should not be added for numeric lists
	if (opf != OPF_NUMBER && opf != OPF_POSITIVE) {
		head.add_data(SEXP_ARGUMENT_STRING);
	}

	if (list != nullptr) {
		// append other list
		head.add_list(std::move(list));
	}

	// return listing
	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_null()
{
	SexpListItem head;

	for (int i = 0; i < static_cast<int>(Operators.size()); i++)
		if (query_operator_return_type(i) == OPR_NULL)
			head.add_op(i);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_bool(int parent_node)
{
	SexpListItem head;

	// search for the previous goal/event operators.  If found, only add the true/false
	// sexpressions to the list
	int only_basic = 0;
	if (parent_node != -1) {
		int op;

		op = get_operator_const(tree_nodes[parent_node].text.c_str());
		if ((op == OP_PREVIOUS_GOAL_TRUE) || (op == OP_PREVIOUS_GOAL_FALSE) || (op == OP_PREVIOUS_EVENT_TRUE) ||
			(op == OP_PREVIOUS_EVENT_FALSE))
			only_basic = 1;
	}

	for (int i = 0; i < static_cast<int>(Operators.size()); i++) {
		if (query_operator_return_type(i) == OPR_BOOL) {
			if (!only_basic || (only_basic && ((Operators[i].value == OP_TRUE) || (Operators[i].value == OP_FALSE)))) {
				head.add_op(i);
			}
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_number()
{
	SexpListItem head;

	for (int i = 0; i < static_cast<int>(Operators.size()); i++) {
		int z = query_operator_return_type(i);
		if ((z == OPR_NUMBER) || (z == OPR_POSITIVE))
			head.add_op(i);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship(int parent_node)
{
	SexpListItem head;
	int op = 0, dock_ship = -1, require_cap_ship = 0;

	// look at the parent node and get the operator.  Some ship lists should be filtered based
	// on what the parent operator is
	if (parent_node >= 0) {
		op = get_operator_const(tree_nodes[parent_node].text.c_str());

		// get the dock_ship number of if this goal is an ai dock goal.  used to prune out unwanted ships out
		// of the generated ship list
		dock_ship = -1;
		if (op == OP_AI_DOCK) {
			int z = tree_nodes[parent_node].parent;
			Assertion(z >= 0, "get_listing_opf_ship: OP_AI_DOCK parent has no valid ancestor (parent=%d).", parent_node);
			Assertion(lcase_equal(tree_nodes[z].text, "add-ship-goal") || lcase_equal(tree_nodes[z].text, "add-wing-goal") ||
                      lcase_equal(tree_nodes[z].text, "add-goal"),
                      "get_listing_opf_ship: unexpected grandparent operator '%s' for OP_AI_DOCK (expected add-ship-goal/add-wing-goal/add-goal).",
                      tree_nodes[z].text.c_str());

			z = tree_nodes[z].child;
			Assertion(z >= 0, "get_listing_opf_ship: expected child ship node under '%s' goal operator.", tree_nodes[z].text.c_str());

			dock_ship = ship_name_lookup(tree_nodes[z].text.c_str(), 1);
			Assertion(dock_ship != -1, "get_listing_opf_ship: dock ship '%s' not found.", tree_nodes[z].text.c_str());
		}
	}

	object* ptr = GET_FIRST(&obj_used_list);
	while (ptr != END_OF_LIST(&obj_used_list)) {
		if ((ptr->type == OBJ_SHIP) || (ptr->type == OBJ_START)) {
			if (op == OP_AI_DOCK) {
				// only include those ships in the list which the given ship can dock with.
				if ((dock_ship != ptr->instance) && ship_docking_valid(dock_ship, ptr->instance))
					head.add_data(Ships[ptr->instance].ship_name);

			} else if (op == OP_CAP_SUBSYS_CARGO_KNOWN_DELAY) {
				if (((Ship_info[Ships[ptr->instance].ship_info_index].is_huge_ship()) && // big ship
						!(Ships[ptr->instance]
								.flags[Ship::Ship_Flags::Toggle_subsystem_scanning])) ||    // which is not flagged OR
					((!(Ship_info[Ships[ptr->instance].ship_info_index].is_huge_ship())) && // small ship
						(Ships[ptr->instance]
								.flags[Ship::Ship_Flags::Toggle_subsystem_scanning]))) { // which is flagged

					head.add_data(Ships[ptr->instance].ship_name);
				}
			} else {
				if (!require_cap_ship || Ship_info[Ships[ptr->instance].ship_info_index].is_huge_ship()) {
					head.add_data(Ships[ptr->instance].ship_name);
				}
			}
		}

		ptr = GET_NEXT(ptr);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_wing()
{
	SexpListItem head;

	for (int i = 0; i < MAX_WINGS; i++) {
		if (Wings[i].wave_count) {
			head.add_data(Wings[i].name);
		}
	}

	return SexpListItemPtr(head.next);
}

// specific types of subsystems we're looking for
#define OPS_CAP_CARGO   1
#define OPS_STRENGTH    2
#define OPS_BEAM_TURRET 3
#define OPS_AWACS       4
#define OPS_ROTATE      5
#define OPS_TRANSLATE   6
#define OPS_ARMOR       7
SexpListItemPtr SexpOpfListBuilder::get_listing_opf_subsystem(int parent_node, int arg_index)
{
	SexpListItem head;

	// determine if the parent is one of the set subsystem strength items.  If so,
	// we want to append the "Hull" name onto the end of the menu
	Assertion(parent_node >= 0, "Invalid parent node passed to get_listing_opf_subsystem!");

	// get the operator type of the node
	int op = get_operator_const(tree_nodes[parent_node].text.c_str());

	// first child node
	int child = tree_nodes[parent_node].child;
	if (child < 0)
		return nullptr;

	int special_subsys = 0;

	switch (op) {
	// where we care about hull strength
	case OP_REPAIR_SUBSYSTEM:
	case OP_SABOTAGE_SUBSYSTEM:
	case OP_SET_SUBSYSTEM_STRNGTH:
		special_subsys = OPS_STRENGTH;
		break;

	// Armor types need Hull and Shields but not Simulated Hull
	case OP_SET_ARMOR_TYPE:
	case OP_HAS_ARMOR_TYPE:
		special_subsys = OPS_ARMOR;
		break;

	// awacs subsystems
	case OP_AWACS_SET_RADIUS:
		special_subsys = OPS_AWACS;
		break;

	// rotating
	case OP_LOCK_ROTATING_SUBSYSTEM:
	case OP_FREE_ROTATING_SUBSYSTEM:
	case OP_REVERSE_ROTATING_SUBSYSTEM:
	case OP_ROTATING_SUBSYS_SET_TURN_TIME:
		special_subsys = OPS_ROTATE;
		break;

	// translating
	case OP_LOCK_TRANSLATING_SUBSYSTEM:
	case OP_FREE_TRANSLATING_SUBSYSTEM:
	case OP_REVERSE_TRANSLATING_SUBSYSTEM:
	case OP_TRANSLATING_SUBSYS_SET_SPEED:
		special_subsys = OPS_TRANSLATE;
		break;

	// where we care about capital ship subsystem cargo
	case OP_CAP_SUBSYS_CARGO_KNOWN_DELAY:
		special_subsys = OPS_CAP_CARGO;

		// get the next sibling
		child = tree_nodes[child].next;
		break;

	// where we care about turrets carrying beam weapons
	case OP_BEAM_FIRE:
		special_subsys = OPS_BEAM_TURRET;

		// if this is arg index 3 (targeted ship)
		if (arg_index == 3) {
			special_subsys = OPS_STRENGTH;

			// iterate to the next field two times
			child = tree_nodes[child].next;
			if (child < 0)
				return nullptr;
			child = tree_nodes[child].next;
		} else {
			Assertion(arg_index == 1, "Invalid argument index passed to get_listing_opf_subsystem!");
		}
		break;

	case OP_BEAM_FIRE_COORDS:
		special_subsys = OPS_BEAM_TURRET;
		break;

	// these sexps check the subsystem of the *second entry* on the list, not the first
	case OP_DISTANCE_CENTER_SUBSYSTEM:
	case OP_DISTANCE_BBOX_SUBSYSTEM:
	case OP_SET_CARGO:
	case OP_IS_CARGO:
	case OP_CHANGE_AI_CLASS:
	case OP_IS_AI_CLASS:
	case OP_MISSILE_LOCKED:
	case OP_SHIP_SUBSYS_GUARDIAN_THRESHOLD:
	case OP_IS_IN_TURRET_FOV:
	case OP_TURRET_SET_FORCED_TARGET:
		// iterate to the next field
		child = tree_nodes[child].next;
		break;

	// this sexp checks the subsystem of the *fourth entry* on the list
	case OP_QUERY_ORDERS:
		// iterate to the next field three times
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		break;

	// this sexp checks the subsystem of the *seventh entry* on the list
	case OP_BEAM_FLOATING_FIRE:
		// iterate to the next field six times
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		break;

	// this sexp checks the subsystem of the *ninth entry* on the list
	case OP_WEAPON_CREATE:
		// iterate to the next field eight times
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		if (child < 0)
			return nullptr;
		child = tree_nodes[child].next;
		break;

	// this sexp checks the third entry, but only for the 4th argument
	case OP_TURRET_SET_FORCED_SUBSYS_TARGET:
		if (arg_index >= 3) {
			child = tree_nodes[child].next;
			if (child < 0)
				return nullptr;
			child = tree_nodes[child].next;
		}
		break;

	default:
		if (op < First_available_operator_id) {
			break;
		} else {
			int this_index = get_dynamic_parameter_index(tree_nodes[parent_node].text, arg_index);

			if (this_index >= 0) {
				for (int count = 0; count < this_index; count++) {
					child = tree_nodes[child].next;
				}
			} else {
				error_display(1,
					"Expected to find a dynamic lua parent parameter for node %i in operator %s but found nothing!",
					arg_index,
					tree_nodes[parent_node].text);
			}
		}
	}

	if (child < 0)
		return nullptr;

	// if one of the subsystem strength operators, append the Hull string and the Simulated Hull string
	if (special_subsys == OPS_STRENGTH) {
		head.add_data(SEXP_HULL_STRING);
		head.add_data(SEXP_SIM_HULL_STRING);
	}

	// if setting armor type we only need Hull and Shields
	if (special_subsys == OPS_ARMOR) {
		head.add_data(SEXP_HULL_STRING);
		head.add_data(SEXP_SHIELD_STRING);
	}

	// now find the ship and add all relevant subsystems
	int sh = ship_name_lookup(tree_nodes[child].text.c_str(), 1);
	if (sh >= 0) {
		ship_subsys* subsys = GET_FIRST(&Ships[sh].subsys_list);
		while (subsys != END_OF_LIST(&Ships[sh].subsys_list)) {
			// add stuff
			switch (special_subsys) {
			// subsystem cargo
			case OPS_CAP_CARGO:
				head.add_data(subsys->system_info->subobj_name);
				break;

			// beam fire
			case OPS_BEAM_TURRET:
				head.add_data(subsys->system_info->subobj_name);
				break;

			// awacs level
			case OPS_AWACS:
				if (subsys->system_info->flags[Model::Subsystem_Flags::Awacs]) {
					head.add_data(subsys->system_info->subobj_name);
				}
				break;

			// rotating
			case OPS_ROTATE:
				if (subsys->system_info->flags[Model::Subsystem_Flags::Rotates]) {
					head.add_data(subsys->system_info->subobj_name);
				}
				break;

			// translating
			case OPS_TRANSLATE:
				if (subsys->system_info->flags[Model::Subsystem_Flags::Translates]) {
					head.add_data(subsys->system_info->subobj_name);
				}
				break;

			// everything else
			default:
				head.add_data(subsys->system_info->subobj_name);
				break;
			}

			// next subsystem
			subsys = GET_NEXT(subsys);
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_subsystem_type(int parent_node)
{
	SexpListItem head;

	// first child node
	int child = tree_nodes[parent_node].child;
	if (child < 0)
		return nullptr;

	// now find the ship
	int shipnum = ship_name_lookup(tree_nodes[child].text.c_str(), 1);
	if (shipnum < 0) {
		return SexpListItemPtr(head.next);
	}

	// add all relevant subsystem types
	int num_added = 0;
	for (int i = 0; i < SUBSYSTEM_MAX; i++) {
		// don't allow these two
		if (i == SUBSYSTEM_NONE || i == SUBSYSTEM_UNKNOWN)
			continue;

		// loop through all ship subsystems
		ship_subsys* subsys = GET_FIRST(&Ships[shipnum].subsys_list);
		while (subsys != END_OF_LIST(&Ships[shipnum].subsys_list)) {
			// check if this subsystem is of this type
			if (i == subsys->system_info->type) {
				// subsystem type is applicable, so add it
				head.add_data(Subsystem_types[i]);
				num_added++;
				break;
			}

			// next subsystem
			subsys = GET_NEXT(subsys);
		}
	}

	// if no subsystem types, go ahead and add NONE (even though it won't be checked)
	if (num_added == 0) {
		head.add_data(Subsystem_types[SUBSYSTEM_NONE]);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_point()
{
	SexpListItem head;

	for (const auto& ii : Waypoint_lists) {
		for (int j = 0; j < static_cast<int>(ii.get_waypoints().size()); ++j) {
			char buf[NAME_LENGTH + 8];
			sprintf(buf, "%s:%d", ii.get_name(), j + 1);
			head.add_data(buf);
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_iff()
{
	SexpListItem head;

	for (int i = 0; i < static_cast<int>(Iff_info.size()); i++)
		head.add_data(Iff_info[i].iff_name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ai_class()
{
	SexpListItem head;

	for (int i = 0; i < Num_ai_classes; i++)
		head.add_data(Ai_class_names[i]);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_support_ship_class()
{
	SexpListItem head;

	head.add_data("<species support ship class>");

	for (auto it = Ship_info.cbegin(); it != Ship_info.cend(); ++it) {
		if (it->flags[Ship::Info_Flags::Support]) {
			head.add_data(it->name);
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ssm_class()
{
	SexpListItem head;

	for (auto it = Ssm_info.cbegin(); it != Ssm_info.cend(); ++it) {
		head.add_data(it->name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_arrival_location()
{
	SexpListItem head;

	for (int i = 0; i < MAX_ARRIVAL_NAMES; i++)
		head.add_data(Arrival_location_names[i]);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_departure_location()
{
	SexpListItem head;

	for (int i = 0; i < MAX_DEPARTURE_NAMES; i++)
		head.add_data(Departure_location_names[i]);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_arrival_anchor_all()
{
	SexpListItem head;

	for (int restrict_to_players = 0; restrict_to_players < 2; restrict_to_players++) {
		for (int i = 0; i < static_cast<int>(Iff_info.size()); i++) {
			char tmp[NAME_LENGTH + 15];
			stuff_special_arrival_anchor_name(tmp, i, restrict_to_players, 0);

			head.add_data(tmp);
		}
	}

	object* objp;
	for (objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp)) {
		if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) {
			head.add_data(Ships[objp->instance].ship_name);
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_with_bay()
{
	SexpListItem head;

	head.add_data("<no anchor>");

	object* objp;
	for (objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp)) {
		if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START)) {
			// determine if this ship has a docking bay
			if (ship_has_dock_bay(objp->instance)) {
				head.add_data(Ships[objp->instance].ship_name);
			}
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_soundtrack_name()
{
	SexpListItem head;

	head.add_data("<No Music>");

	for (auto& st : Soundtracks)
		head.add_data(st.name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ai_goal(int parent_node)
{
	SexpListItem head;

	Assertion(parent_node >= 0, "Invalid parent node passed to get_listing_opf_ai_goal!");
	int child = tree_nodes[parent_node].child;
	if (child < 0)
		return nullptr;

	int n = ship_name_lookup(tree_nodes[child].text.c_str(), 1);
	if (n >= 0) {
		// add operators if it's an ai-goal and ai-goal is allowed for that ship
		for (int i = 0; i < static_cast<int>(Operators.size()); i++) {
			if ((query_operator_return_type(i) == OPR_AI_GOAL) && query_sexp_ai_goal_valid(Operators[i].value, n))
				head.add_op(i);
		}

	} else {
		int z = wing_name_lookup(tree_nodes[child].text.c_str());
		if (z >= 0) {
			for (int w = 0; w < Wings[z].wave_count; w++) {
				n = Wings[z].ship_index[w];
				// add operators if it's an ai-goal and ai-goal is allowed for that ship
				for (int i = 0; i < static_cast<int>(Operators.size()); i++) {
					if ((query_operator_return_type(i) == OPR_AI_GOAL) &&
						query_sexp_ai_goal_valid(Operators[i].value, n))
						head.add_op(i);
				}
			}
			// when dealing with the special argument add them all. It's up to the FREDder to ensure invalid orders
			// aren't given
		} else if (tree_nodes[child].text == SEXP_ARGUMENT_STRING) {
			for (int i = 0; i < static_cast<int>(Operators.size()); i++) {
				if (query_operator_return_type(i) == OPR_AI_GOAL) {
					head.add_op(i);
				}
			}
		} else
			return nullptr; // no valid ship or wing to check against, make nothing available
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_flexible_argument()
{
	SexpListItem head;

	for (int i = 0; i < static_cast<int>(Operators.size()); i++)
		if (query_operator_return_type(i) == OPR_FLEXIBLE_ARGUMENT)
			head.add_op(i);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_docker_point(int parent_node, int arg_num)
{
	SexpListItem head;

	Assertion(parent_node >= 0, "Invalid parent node passed to get_listing_opf_docker_point!");
	Assertion(lcase_equal(tree_nodes[parent_node].text, "ai-dock") ||
				  lcase_equal(tree_nodes[parent_node].text, "set-docked") ||
				  get_operator_const(tree_nodes[parent_node].text.c_str()) >=
					  static_cast<int>(First_available_operator_id),
		"get_listing_opf_docker_point: parent operator '%s' must be 'ai-dock', 'set-docked', or a Lua operator (id >= "
		"%d).",
		tree_nodes[parent_node].text.c_str(),
		static_cast<int>(First_available_operator_id));

	int sh = -1;
	if (lcase_equal(tree_nodes[parent_node].text, "ai-dock")) {
		int z = tree_nodes[parent_node].parent;
		if (z < 0)
			return nullptr;
		Assertion(lcase_equal(tree_nodes[z].text, "add-ship-goal") ||
					  lcase_equal(tree_nodes[z].text, "add-wing-goal") || lcase_equal(tree_nodes[z].text, "add-goal"),
			"get_listing_opf_docker_point: unexpected grandparent operator '%s' for 'ai-dock' (expected "
			"add-ship-goal/add-wing-goal/add-goal).",
			tree_nodes[z].text.c_str());

		z = tree_nodes[z].child;
		if (z < 0)
			return nullptr;
		sh = ship_name_lookup(tree_nodes[z].text.c_str(), 1);
	} else if (lcase_equal(tree_nodes[parent_node].text, "set-docked")) {
		// Docker ship should be the first child node
		int z = tree_nodes[parent_node].child;
		if (z < 0)
			return nullptr;
		sh = ship_name_lookup(tree_nodes[z].text.c_str(), 1);
	}
	// for Lua sexps
	else if (get_operator_const(tree_nodes[parent_node].text.c_str()) >= static_cast<int>(First_available_operator_id)) {
		int this_index = get_dynamic_parameter_index(tree_nodes[parent_node].text, arg_num);

		if (this_index >= 0) {
			int z = tree_nodes[parent_node].child;

			for (int j = 0; j < this_index; j++) {
				z = tree_nodes[z].next;
			}

			sh = ship_name_lookup(tree_nodes[z].text.c_str(), 1);
		} else {
			error_display(1,
				"Expected to find a dynamic lua parent parameter for node %i in operator %s but found nothing!",
				arg_num,
				tree_nodes[parent_node].text);
		}
	}

	if (sh >= 0) {
		int z = get_docking_list(Ship_info[Ships[sh].ship_info_index].model_num);
		for (int i = 0; i < z; i++)
			head.add_data(Docking_bay_list[i]);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_dockee_point(int parent_node)
{
	SexpListItem head;

	Assertion(parent_node >= 0, "Invalid parent node passed to get_listing_opf_dockee_point!");
	Assertion(lcase_equal(tree_nodes[parent_node].text, "ai-dock") || lcase_equal(tree_nodes[parent_node].text, "set-docked"),
		"get_listing_opf_dockee_point: parent operator '%s' must be 'ai-dock' or 'set-docked'.",
		tree_nodes[parent_node].text.c_str());

	int sh = -1;
	if (lcase_equal(tree_nodes[parent_node].text, "ai-dock")) {
		int z = tree_nodes[parent_node].child;
		if (z < 0)
			return nullptr;

		sh = ship_name_lookup(tree_nodes[z].text.c_str(), 1);
	} else if (lcase_equal(tree_nodes[parent_node].text, "set-docked")) {
		// Dockee ship should be the third child node
		int z = tree_nodes[parent_node].child; // 1
		if (z < 0)
			return nullptr;
		z = tree_nodes[z].next; // 2
		if (z < 0)
			return nullptr;
		z = tree_nodes[z].next; // 3
		if (z < 0)
			return nullptr;

		sh = ship_name_lookup(tree_nodes[z].text.c_str(), 1);
	}

	if (sh >= 0) {
		int z = get_docking_list(Ship_info[Ships[sh].ship_info_index].model_num);
		for (int i = 0; i < z; i++)
			head.add_data(Docking_bay_list[i]);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_message()
{
	SexpListItem head;

	if (environment) {
		const auto names = environment->getMessageNames();
		for (const auto& s : names) {
			head.add_data(s.c_str());
		}
	}
	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_who_from()
{
	SexpListItem head;

	// head.add_data("<any allied>");
	head.add_data("#Command");
	head.add_data("<any wingman>");
	head.add_data("<none>");

	object* ptr = GET_FIRST(&obj_used_list);
	while (ptr != END_OF_LIST(&obj_used_list)) {
		if ((ptr->type == OBJ_SHIP) || (ptr->type == OBJ_START))
			if (Ship_info[Ships[get_ship_from_obj(ptr)].ship_info_index].is_flyable())
				head.add_data(Ships[ptr->instance].ship_name);

		ptr = GET_NEXT(ptr);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_priority()
{
	SexpListItem head;

	head.add_data("High");
	head.add_data("Normal");
	head.add_data("Low");
	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_waypoint_path()
{
	SexpListItem head;

	for (const auto& ii : Waypoint_lists)
		head.add_data(ii.get_name());

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_positive()
{
	SexpListItem head;

	for (int i = 0; i < static_cast<int>(Operators.size()); i++) {
		int z = query_operator_return_type(i);
		// Goober5000's number hack
		if ((z == OPR_NUMBER) || (z == OPR_POSITIVE))
			head.add_op(i);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_mission_name()
{
	SexpListItem head;

	// Prefer env; fall back to the same default as the base env.
	SCP_vector<SCP_string> names = environment ? environment->getMissionNames() : ISexpEnvironment{}.getMissionNames();

	for (const auto& s : names) {
		head.add_data(s.c_str());
	}
	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_point()
{
	SexpListItem head;

	head.add_list(std::move(get_listing_opf_ship()));
	head.add_list(std::move(get_listing_opf_point()));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_goal_name(int parent_node)
{
	SexpListItem head;

	const bool inCampaign = (environment && environment->isCampaignContext());

	if (inCampaign) {
		int child;

		Assertion(parent_node >= 0, "Invalid parent node passed to get_listing_opf_goal_name!");
		child = tree_nodes[parent_node].child;
		if (child < 0)
			return nullptr;

		int m;
		for (m = 0; m < Campaign.num_missions; m++)
			if (!stricmp(Campaign.missions[m].name, tree_nodes[child].text.c_str()))
				break;

		if (m < Campaign.num_missions) {
			if (Campaign.missions[m].flags & CMISSION_FLAG_FRED_LOAD_PENDING) // haven't loaded goal names yet.
			{
				read_mission_goal_list(m);
				Campaign.missions[m].flags &= ~CMISSION_FLAG_FRED_LOAD_PENDING;
			}

			for (const auto& stored_goal : Campaign.missions[m].goals)
				head.add_data(stored_goal.name);
		}
	} else {
		for (const auto& goal : Mission_goals) {
			auto temp_name = SCP_string(goal.name, 0, NAME_LENGTH - 1);
			head.add_data(temp_name.c_str());
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_wing()
{
	SexpListItem head;

	head.add_list(std::move(get_listing_opf_ship()));
	head.add_list(std::move(get_listing_opf_wing()));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_wing_wholeteam()
{
	SexpListItem head;

	for (size_t i = 0; i < Iff_info.size(); i++)
		head.add_data(Iff_info[i].iff_name);

	head.add_list(std::move(get_listing_opf_ship_wing()));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_wing_shiponteam_point()
{
	SexpListItem head;

	for (size_t i = 0; i < Iff_info.size(); i++) {
		char tmp[NAME_LENGTH + 7];
		sprintf(tmp, "<any %s>", Iff_info[i].iff_name);
		strlwr(tmp);
		head.add_data(tmp);
	}

	head.add_list(std::move(get_listing_opf_ship_wing_point()));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_wing_point()
{
	SexpListItem head;

	head.add_list(std::move(get_listing_opf_ship()));
	head.add_list(std::move(get_listing_opf_wing()));
	head.add_list(std::move(get_listing_opf_point()));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_wing_point_or_none()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);
	head.add_list(std::move(get_listing_opf_ship_wing_point()));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_order_recipient()
{
	SexpListItem head;

	head.add_data("<all fighters>");

	head.add_list(std::move(get_listing_opf_ship()));
	head.add_list(std::move(get_listing_opf_wing()));
	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_type()
{
	SexpListItem head;

	for (size_t i = 0; i < Ship_types.size(); i++) {
		head.add_data(Ship_types[i].name);
	}
	if (Fighter_bomber_valid) {
		head.add_data(Fighter_bomber_type_name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_keypress()
{
	SexpListItem head;
	const auto& Default_config = Control_config_presets[0].bindings;

	for (size_t i = 0; i < Control_config.size(); ++i) {
		auto btn = Default_config[i].get_btn(CID_KEYBOARD);

		if ((btn >= 0) && !Control_config[i].disabled) {
			head.add_data(textify_scancode_universal(btn));
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_event_name(int parent_node)
{
	SexpListItem head;

	const bool inCampaign = (environment && environment->isCampaignContext());

	if (inCampaign) {
		int child;

		Assertion(parent_node >= 0, "Invalid parent node passed to get_listing_opf_event_name!");
		child = tree_nodes[parent_node].child;
		if (child < 0)
			return nullptr;

		int m;
		for (m = 0; m < Campaign.num_missions; m++)
			if (!stricmp(Campaign.missions[m].name, tree_nodes[child].text.c_str()))
				break;

		if (m < Campaign.num_missions) {
			if (Campaign.missions[m].flags & CMISSION_FLAG_FRED_LOAD_PENDING) // haven't loaded goal names yet.
			{
				read_mission_goal_list(m);
				Campaign.missions[m].flags &= ~CMISSION_FLAG_FRED_LOAD_PENDING;
			}

			for (const auto& stored_event : Campaign.missions[m].events)
				head.add_data(stored_event.name);
		}
	} else {
		for (const auto& event : Mission_events) {
			auto temp_name = SCP_string(event.name, 0, NAME_LENGTH - 1);
			head.add_data(temp_name.c_str());
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ai_order()
{
	SexpListItem head;

	for (const auto& order : Player_orders)
		head.add_data(order.hud_name.c_str());

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_skill_level()
{
	SexpListItem head;

	for (int i = 0; i < NUM_SKILL_LEVELS; i++)
		head.add_data(Skill_level_names(i, 0));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_cargo()
{
	SexpListItem head;

	head.add_data("Nothing");
	for (int i = 0; i < Num_cargo; i++) {
		if (stricmp(Cargo_names[i], "nothing"))
			head.add_data(Cargo_names[i]);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_string()
{
	SexpListItem head;

	head.add_data(SEXP_ANY_STRING);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_medal_name()
{
	SexpListItem head;

	for (int i = 0; i < static_cast<int>(Medals.size()); i++) {
		// don't add Rank or the Ace badges
		if ((i == Rank_medal_index) || (Medals[i].kills_needed > 0))
			continue;
		head.add_data(Medals[i].name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_weapon_name()
{
	SexpListItem head;

	for (auto& wi : Weapon_info)
		head.add_data(wi.name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_intel_name()
{
	SexpListItem head;

	for (auto& ii : Intel_info)
		head.add_data(ii.name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_class_name()
{
	SexpListItem head;

	for (auto& si : Ship_info)
		head.add_data(si.name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_huge_weapon()
{
	SexpListItem head;

	for (auto& wi : Weapon_info) {
		if (wi.wi_flags[Weapon::Info_Flags::Huge])
			head.add_data(wi.name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_not_player()
{
	SexpListItem head;

	object* ptr = GET_FIRST(&obj_used_list);
	while (ptr != END_OF_LIST(&obj_used_list)) {
		if (ptr->type == OBJ_SHIP)
			head.add_data(Ships[ptr->instance].ship_name);

		ptr = GET_NEXT(ptr);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_or_none()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);
	head.add_list(std::move(get_listing_opf_ship()));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_subsystem_or_none(int parent_node, int arg_index)
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);
	head.add_list(std::move(get_listing_opf_subsystem(parent_node, arg_index)));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_subsys_or_generic(int parent_node, int arg_index)
{
	SexpListItem head;

	for (int i = 0; i < SUBSYSTEM_MAX; ++i) {
		// it's not clear what the "activator" subsystem was intended to do, so let's not display it by default
		if (i != SUBSYSTEM_NONE && i != SUBSYSTEM_UNKNOWN && i != SUBSYSTEM_ACTIVATION) {
			char buffer[NAME_LENGTH];
			sprintf(buffer, SEXP_ALL_GENERIC_SUBSYSTEM_STRING, Subsystem_types[i]);
			SCP_tolower(buffer);
			head.add_data(buffer);
		}
	}
	head.add_data(SEXP_ALL_SUBSYSTEMS_STRING);
	head.add_list(std::move(get_listing_opf_subsystem(parent_node, arg_index)));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_jump_nodes()
{
	SexpListItem head;

	SCP_list<CJumpNode>::iterator jnp;
	for (jnp = Jump_nodes.begin(); jnp != Jump_nodes.end(); ++jnp) {
		head.add_data(jnp->GetName());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_variable_names()
{
	SexpListItem head;

	for (int i = 0; i < MAX_SEXP_VARIABLES; i++) {
		if (Sexp_variables[i].type & SEXP_VARIABLE_SET) {
			head.add_data(Sexp_variables[i].variable_name);
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_skybox_model()
{

	SexpListItem head;
	head.add_data("default");
	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_skybox_flags()
{
	SexpListItem head;

	for (int i = 0; i < Num_skybox_flags; ++i) {
		head.add_data(Skybox_flags[i]);
	}
	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_background_bitmap()
{
	SexpListItem head;
	int i;

	for (i = 0; i < stars_get_num_entries(false, true); i++) {
		head.add_data(stars_get_name_FRED(i, false));
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_sun_bitmap()
{
	SexpListItem head;
	int i;

	for (i = 0; i < stars_get_num_entries(true, true); i++) {
		head.add_data(stars_get_name_FRED(i, true));
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_nebula_storm_type()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);

	for (size_t i = 0; i < Storm_types.size(); i++) {
		head.add_data(Storm_types[i].name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_nebula_poof()
{
	SexpListItem head;

	for (poof_info& pf : Poof_info) {
		head.add_data(pf.name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_turret_target_order()
{
	SexpListItem head;

	for (int i = 0; i < NUM_TURRET_ORDER_TYPES; i++)
		head.add_data(Turret_target_order_names[i]);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_turret_types()
{
	SexpListItem head;

	for (int i = 0; i < NUM_TURRET_TYPES; i++)
		head.add_data(Turret_valid_types[i]);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_turret_target_priorities()
{
	SexpListItem head;

	for (size_t t = 0; t < Ai_tp_list.size(); t++) {
		head.add_data(Ai_tp_list[t].name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_armor_type()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);
	for (size_t t = 0; t < Armor_types.size(); t++)
		head.add_data(Armor_types[t].GetNamePtr());
	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_damage_type()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);
	for (size_t t = 0; t < Damage_types.size(); t++)
		head.add_data(Damage_types[t].name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_animation_type()
{
	SexpListItem head;

	for (const auto& animation_type : animation::Animation_types) {
		head.add_data(animation_type.second.first);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_persona()
{
	SexpListItem head;

	for (const auto& persona : Personas) {
		if (persona.flags & PERSONA_FLAG_WINGMAN) {
			head.add_data(persona.name);
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_post_effect()
{
	SexpListItem head;

	SCP_vector<SCP_string> ppe_names;
	gr_get_post_process_effect_names(ppe_names);
	for (size_t i = 0; i < ppe_names.size(); i++) {
		head.add_data(ppe_names[i].c_str());
	}
	head.add_data("lightshafts");

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_font()
{
	SexpListItem head;

	for (int i = 0; i < font::FontManager::numberOfFonts(); i++) {
		head.add_data(font::FontManager::getFont(i)->getName().c_str());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_hud_elements()
{
	SexpListItem head;
	head.add_data("warpout");

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_sound_environment()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);
	for (size_t i = 0; i < EFX_presets.size(); i++) {
		head.add_data(EFX_presets[i].name.c_str());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_sound_environment_option()
{
	SexpListItem head;

	for (int i = 0; i < Num_sound_environment_options; i++)
		head.add_data(Sound_environment_option[i]);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_adjust_audio_volume()
{
	SexpListItem head;

	for (int i = 0; i < Num_adjust_audio_options; i++)
		head.add_data(Adjust_audio_options[i]);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_explosion_option()
{
	SexpListItem head;

	for (int i = 0; i < Num_explosion_options; i++)
		head.add_data(Explosion_option[i]);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_weapon_banks()
{
	SexpListItem head;
	head.add_data(SEXP_ALL_BANKS_STRING);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_builtin_hud_gauge()
{
	SexpListItem head;

	for (int i = 0; i < Num_hud_gauge_types; i++)
		head.add_data(Hud_gauge_types[i].name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_custom_hud_gauge()
{
	SexpListItem head;
	// prevent duplicate names, comparing case-insensitively
	SCP_unordered_set<SCP_string, SCP_string_lcase_hash, SCP_string_lcase_equal_to> all_gauges;

	for (auto& gauge : default_hud_gauges) {
		SCP_string name = gauge->getCustomGaugeName();
		if (!name.empty() && all_gauges.count(name) == 0) {
			head.add_data(name.c_str());
			all_gauges.insert(std::move(name));
		}
	}

	for (auto& si : Ship_info) {
		for (auto& gauge : si.hud_gauges) {
			SCP_string name = gauge->getCustomGaugeName();
			if (!name.empty() && all_gauges.count(name) == 0) {
				head.add_data(name.c_str());
				all_gauges.insert(std::move(name));
			}
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_any_hud_gauge()
{
	SexpListItem head;

	head.add_list(std::move(get_listing_opf_builtin_hud_gauge()));
	head.add_list(std::move(get_listing_opf_custom_hud_gauge()));

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_effect()
{
	SexpListItem head;

	for (SCP_vector<ship_effect>::iterator sei = Ship_effects.begin(); sei != Ship_effects.end(); ++sei) {
		head.add_data(sei->name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_mission_moods()
{
	SexpListItem head;
	for (SCP_vector<SCP_string>::iterator iter = Builtin_moods.begin(); iter != Builtin_moods.end(); ++iter) {
		head.add_data(iter->c_str());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_ship_flags()
{
	SexpListItem head;
	// prevent duplicate names, comparing case-insensitively
	SCP_unordered_set<SCP_string, SCP_string_lcase_hash, SCP_string_lcase_equal_to> all_flags;

	add_flag_name_helper(all_flags, head, Object_flag_names, &obj_flag_name::flag_name, (size_t)Num_object_flag_names);
	add_flag_name_helper(all_flags, head, Ship_flag_names, &ship_flag_name::flag_name, Num_ship_flag_names);
	add_flag_name_helper(all_flags,
		head,
		Parse_object_flags,
		&flag_def_list_new<Mission::Parse_Object_Flags>::name,
		Num_parse_object_flags);
	add_flag_name_helper(all_flags, head, Ai_flag_names, &ai_flag_name::flag_name, (size_t)Num_ai_flag_names);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_wing_flags()
{
	SexpListItem head;
	// wing flags
	for (size_t i = 0; i < Num_wing_flag_names; i++) {
		head.add_data(Wing_flag_names[i].flag_name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_team_colors()
{
	SexpListItem head;
	head.add_data("None");
	for (SCP_map<SCP_string, team_color>::iterator tcolor = Team_Colors.begin(); tcolor != Team_Colors.end();
		++tcolor) {
		head.add_data(tcolor->first.c_str());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_nebula_patterns()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);

	for (size_t i = 0; i < Neb2_bitmap_filenames.size(); i++) {
		head.add_data(Neb2_bitmap_filenames[i].c_str());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_game_snds()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);

	for (SCP_vector<game_snd>::iterator iter = Snds.begin(); iter != Snds.end(); ++iter) {
		if (!can_construe_as_integer(iter->name.c_str())) {
			head.add_data(iter->name.c_str());
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_fireball()
{
	SexpListItem head;

	for (const auto& fi : Fireball_info) {
		auto unique_id = fi.unique_id;

		if (strlen(unique_id) > 0)
			head.add_data(unique_id);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_species()
{
	SexpListItem head;

	for (auto& species : Species_info)
		head.add_data(species.species_name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_language()
{
	SexpListItem head;

	for (auto& lang : Lcl_languages)
		head.add_data(lang.lang_name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_functional_when_eval_type()
{
	SexpListItem head;

	for (int i = 0; i < Num_functional_when_eval_types; i++)
		head.add_data(Functional_when_eval_type[i]);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_animation_name(int parent_node)
{
	SexpListItem head;

	Assertion(parent_node >= 0, "Invalid parent node passed to get_listing_opf_animation_name!");

	// get the operator type of the node
	int op = get_operator_const(tree_nodes[parent_node].text.c_str());

	// first child node
	int child = tree_nodes[parent_node].child;
	if (child < 0)
		return nullptr;
	int sh = ship_name_lookup(tree_nodes[child].text.c_str(), 1);

	switch (op) {
	case OP_TRIGGER_ANIMATION_NEW:
	case OP_STOP_LOOPING_ANIMATION: {
		child = tree_nodes[child].next;
		auto triggerType = animation::anim_match_type(tree_nodes[child].text.c_str());

		for (const auto& animation : Ship_info[Ships[sh].ship_info_index].animations.getRegisteredTriggers()) {
			if (animation.type != triggerType)
				continue;

			if (animation.subtype != animation::ModelAnimationSet::SUBTYPE_DEFAULT) {
				int animationSubtype = animation.subtype;

				if (animation.type == animation::ModelAnimationTriggerType::DockBayDoor) {
					// Because of the old system, this is this weird exception. Don't explicitly suggest the NOT doors,
					// as they cannot be explicitly targeted anyways
					if (animation.subtype < 0)
						continue;

					animationSubtype--;
				}

				head.add_data(std::to_string(animationSubtype).c_str());
			} else
				head.add_data(animation.name.c_str());
		}

		break;
	}

	case OP_UPDATE_MOVEABLE:
		for (const auto& moveable : Ship_info[Ships[sh].ship_info_index].animations.getRegisteredMoveables())
			head.add_data(moveable.c_str());

		break;
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_sexp_containers(ContainerType con_type)
{
	SexpListItem head;

	for (const auto& container : get_all_sexp_containers()) {
		if (any(container.type & con_type)) {
			head.add_data(container.container_name.c_str(), (SEXPT_CONTAINER_NAME | SEXPT_STRING | SEXPT_VALID));
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_asteroid_types()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);

	auto list = get_list_valid_asteroid_subtypes();

	for (const auto& this_asteroid : list) {
		head.add_data(this_asteroid.c_str());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_debris_types()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);

	for (const auto& this_asteroid : Asteroid_info) {
		if (this_asteroid.type == ASTEROID_TYPE_DEBRIS) {
			head.add_data(this_asteroid.name);
		}
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_wing_formation()
{
	SexpListItem head;

	head.add_data("Default");
	for (const auto& formation : Wing_formations)
		head.add_data(formation.name);

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_motion_debris()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);

	for (size_t i = 0; i < Motion_debris_info.size(); i++) {
		head.add_data(Motion_debris_info[i].name.c_str());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_bolt_types()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);

	for (size_t i = 0; i < Bolt_types.size(); i++) {
		head.add_data(Bolt_types[i].name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_traitor_overrides()
{
	SexpListItem head;

	head.add_data(SEXP_NONE_STRING);

	for (size_t i = 0; i < Traitor_overrides.size(); i++) {
		head.add_data(Traitor_overrides[i].name.c_str());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_lua_general_orders()
{
	SexpListItem head;

	SCP_vector<SCP_string> orders = ai_lua_get_general_orders();

	for (const auto& val : orders) {
		head.add_data(val.c_str());
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_message_types()
{
	SexpListItem head;

	for (const auto& val : Builtin_messages) {
		head.add_data(val.name);
	}

	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_lua_enum(int parent_node, int arg_index)
{

	SexpListItem head;
	
	// first child node
	int child = tree_nodes[parent_node].child;
	if (child < 0)
		return nullptr;

	int this_index = get_dynamic_parameter_index(tree_nodes[parent_node].text, arg_index);

	if (this_index >= 0) {
		for (int count = 0; count < this_index; count++) {
			child = tree_nodes[child].next;
		}
	} else {
		error_display(1,
			"Expected to find an enum parent parameter for node %i in operator %s but found nothing!",
			arg_index,
			tree_nodes[parent_node].text);
		return nullptr;
	}

	// Append the suffix if it exists
	SCP_string enum_name = tree_nodes[child].text + get_child_enum_suffix(tree_nodes[parent_node].text, arg_index);

	int item = get_dynamic_enum_position(enum_name);

	if (item >= 0 && item < static_cast<int>(Dynamic_enums.size())) {

		for (const SCP_string& enum_item : Dynamic_enums[item].list) {
			head.add_data(enum_item.c_str());
		}
	} else {
		// else if enum is invalid do this
		mprintf(("Could not find Lua Enum %s! Using <none> instead!", enum_name.c_str()));
		head.add_data("<none>");
	}
	return SexpListItemPtr(head.next);
}

SexpListItemPtr SexpOpfListBuilder::get_listing_opf_mission_custom_strings()
{
	SexpListItem head;

	for (const auto& val : The_mission.custom_strings) {
		head.add_data(val.name.c_str());
	}

	return SexpListItemPtr(head.next);
}


// The final check if we finish the whole switch and haven't returned yet
SexpListItemPtr SexpOpfListBuilder::check_for_dynamic_sexp_enum(int opf)
{
	SexpListItem head;

	int item = opf - First_available_opf_id;

	if (item < static_cast<int>(Dynamic_enums.size())) {

		for (const SCP_string& enum_item : Dynamic_enums[item].list) {
			head.add_data(enum_item.c_str());
		}
		return SexpListItemPtr(head.next);
	} else {
		// else if opf is invalid do this
		UNREACHABLE("Unhandled SEXP argument type!"); // unknown OPF code
		return nullptr;
	}
}