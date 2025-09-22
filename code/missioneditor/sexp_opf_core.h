#pragma once

#include "sexp_tree_core.h"

class SexpOpfListBuilder final {
  public:
	explicit SexpOpfListBuilder(const SCP_vector<SexpNode>& nodes, const ISexpEnvironment* env)
		: tree_nodes(nodes), environment(env)
	{
	}

	// Single public entrypoint: dispatch to the correct OPF lister.
	SexpListItem* buildListing(int opf, int parent_node, int arg_index);

  private:
	const SCP_vector<SexpNode>& tree_nodes; // model's nodes
	const ISexpEnvironment* environment;    // may be null if not needed

	bool is_node_eligible_for_special_argument(int parent_node) const;
	int find_ancestral_argument_number(int parent_op, int child_node) const;
	int find_argument_number(int parent_node, int child_node) const;

	static SexpListItem* get_listing_opf_null();
	SexpListItem* get_listing_opf_bool(int parent_node = -1);
	static SexpListItem* get_listing_opf_number();
	SexpListItem* get_listing_opf_ship(int parent_node = -1);
	static SexpListItem* get_listing_opf_wing();
	SexpListItem* get_listing_opf_subsystem(int parent_node, int arg_index);
	SexpListItem* get_listing_opf_subsystem_type(int parent_node);
	static SexpListItem* get_listing_opf_point();
	static SexpListItem* get_listing_opf_iff();
	static SexpListItem* get_listing_opf_ai_class();
	static SexpListItem* get_listing_opf_support_ship_class();
	static SexpListItem* get_listing_opf_ssm_class();
	static SexpListItem* get_listing_opf_arrival_location();
	static SexpListItem* get_listing_opf_departure_location();
	static SexpListItem* get_listing_opf_arrival_anchor_all();
	static SexpListItem* get_listing_opf_ship_with_bay();
	static SexpListItem* get_listing_opf_soundtrack_name();
	SexpListItem* get_listing_opf_ai_goal(int parent_node);
	static SexpListItem* get_listing_opf_flexible_argument();
	SexpListItem* get_listing_opf_docker_point(int parent_node, int arg_index);
	SexpListItem* get_listing_opf_dockee_point(int parent_node);
	SexpListItem* get_listing_opf_message();
	static SexpListItem* get_listing_opf_who_from();
	static SexpListItem* get_listing_opf_priority();
	static SexpListItem* get_listing_opf_waypoint_path();
	static SexpListItem* get_listing_opf_positive();
	SexpListItem* get_listing_opf_mission_name();
	SexpListItem* get_listing_opf_ship_point();
	SexpListItem* get_listing_opf_goal_name(int parent_node);
	SexpListItem* get_listing_opf_ship_wing();
	SexpListItem* get_listing_opf_ship_wing_wholeteam();
	SexpListItem* get_listing_opf_ship_wing_shiponteam_point();
	SexpListItem* get_listing_opf_ship_wing_point();
	SexpListItem* get_listing_opf_ship_wing_point_or_none();
	SexpListItem* get_listing_opf_order_recipient();
	static SexpListItem* get_listing_opf_ship_type();
	static SexpListItem* get_listing_opf_keypress();
	SexpListItem* get_listing_opf_event_name(int parent_node);
	static SexpListItem* get_listing_opf_ai_order();
	static SexpListItem* get_listing_opf_skill_level();
	static SexpListItem* get_listing_opf_cargo();
	static SexpListItem* get_listing_opf_string();
	static SexpListItem* get_listing_opf_medal_name();
	static SexpListItem* get_listing_opf_weapon_name();
	static SexpListItem* get_listing_opf_intel_name();
	static SexpListItem* get_listing_opf_ship_class_name();
	static SexpListItem* get_listing_opf_huge_weapon();
	static SexpListItem* get_listing_opf_ship_not_player();
	SexpListItem* get_listing_opf_ship_or_none();
	SexpListItem* get_listing_opf_subsystem_or_none(int parent_node, int arg_index);
	SexpListItem* get_listing_opf_subsys_or_generic(int parent_node, int arg_index);
	static SexpListItem* get_listing_opf_jump_nodes();
	static SexpListItem* get_listing_opf_variable_names();
	static SexpListItem* get_listing_opf_skybox_model();
	static SexpListItem* get_listing_opf_skybox_flags();
	static SexpListItem* get_listing_opf_background_bitmap();
	static SexpListItem* get_listing_opf_sun_bitmap();
	static SexpListItem* get_listing_opf_nebula_storm_type();
	static SexpListItem* get_listing_opf_nebula_poof();
	static SexpListItem* get_listing_opf_turret_target_order();
	static SexpListItem* get_listing_opf_turret_types();
	static SexpListItem* get_listing_opf_turret_target_priorities();
	static SexpListItem* get_listing_opf_armor_type();
	static SexpListItem* get_listing_opf_damage_type();
	static SexpListItem* get_listing_opf_animation_type();
	static SexpListItem* get_listing_opf_persona();
	static SexpListItem* get_listing_opf_post_effect();
	static SexpListItem* get_listing_opf_font();
	static SexpListItem* get_listing_opf_hud_elements();
	static SexpListItem* get_listing_opf_sound_environment();
	static SexpListItem* get_listing_opf_sound_environment_option();
	static SexpListItem* get_listing_opf_adjust_audio_volume();
	static SexpListItem* get_listing_opf_explosion_option();
	static SexpListItem* get_listing_opf_weapon_banks();
	static SexpListItem* get_listing_opf_builtin_hud_gauge();
	static SexpListItem* get_listing_opf_custom_hud_gauge();
	static SexpListItem* get_listing_opf_any_hud_gauge();
	static SexpListItem* get_listing_opf_ship_effect();
	static SexpListItem* get_listing_opf_mission_moods();
	static SexpListItem* get_listing_opf_ship_flags();
	static SexpListItem* get_listing_opf_wing_flags();
	static SexpListItem* get_listing_opf_team_colors();
	static SexpListItem* get_listing_opf_nebula_patterns();
	static SexpListItem* get_listing_opf_game_snds();
	static SexpListItem* get_listing_opf_fireball();
	static SexpListItem* get_listing_opf_species();
	static SexpListItem* get_listing_opf_language();
	static SexpListItem* get_listing_opf_functional_when_eval_type();
	SexpListItem* get_listing_opf_animation_name(int parent_node);
	static SexpListItem* get_listing_opf_sexp_containers(ContainerType con_type);
	static SexpListItem* get_listing_opf_asteroid_types();
	static SexpListItem* get_listing_opf_debris_types();
	static SexpListItem* get_listing_opf_wing_formation();
	static SexpListItem* get_listing_opf_motion_debris();
	static SexpListItem* get_listing_opf_bolt_types();
	static SexpListItem* get_listing_opf_traitor_overrides();
	static SexpListItem* get_listing_opf_lua_general_orders();
	static SexpListItem* get_listing_opf_message_types();
	SexpListItem* get_listing_opf_lua_enum(int parent_node, int arg_index);
	static SexpListItem* get_listing_opf_mission_custom_strings();

	static SexpListItem* check_for_dynamic_sexp_enum(int opf);
};