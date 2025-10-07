#pragma once

#include "globalincs/pstypes.h"

#include "parse/sexp_container.h"

class SexpTreeModel;
struct SexpNode;
struct SexpListItem;
struct ISexpEnvironment;

struct SexpListItemDeleter {
	void operator()(SexpListItem* p) const;
};

using SexpListItemPtr = std::unique_ptr<SexpListItem, SexpListItemDeleter>;

// Minimal list item used for building argument-choice lists (OPF listings)
struct SexpListItem {
	int type = 0;
	int op = 0;
	SCP_string text;
	SexpListItem* next = nullptr;

	// Construction helpers
	void set_op(int op_num);
	void set_data(const char* str);
	void set_data(const char* str, int t);

	void add_op(int op_num);
	void add_data(const char* str);
	void add_data(const char* str, int t);
	void add_list(SexpListItemPtr list);
	void destroy();
};

class SexpOpfListBuilder final {
  public:
	explicit SexpOpfListBuilder(const SCP_vector<SexpNode>& nodes, const ISexpEnvironment* env)
		: tree_nodes(nodes), environment(env)
	{
	}

	// Single public entrypoint: dispatch to the correct OPF lister.
	SexpListItemPtr buildListing(int opf, int parent_node, int arg_index);

	int find_ancestral_argument_number(int parent_op, int child_node) const;
	int find_argument_number(int parent_node, int child_node) const;
	bool is_node_eligible_for_special_argument(int parent_node) const;

  private:
	const SCP_vector<SexpNode>& tree_nodes; // model's nodes
	const ISexpEnvironment* environment;    // may be null if not needed

	static SexpListItemPtr get_listing_opf_null();
	SexpListItemPtr get_listing_opf_bool(int parent_node = -1);
	static SexpListItemPtr get_listing_opf_number();
	SexpListItemPtr get_listing_opf_ship(int parent_node = -1);
	static SexpListItemPtr get_listing_opf_wing();
	SexpListItemPtr get_listing_opf_subsystem(int parent_node, int arg_index);
	SexpListItemPtr get_listing_opf_subsystem_type(int parent_node);
	static SexpListItemPtr get_listing_opf_point();
	static SexpListItemPtr get_listing_opf_iff();
	static SexpListItemPtr get_listing_opf_ai_class();
	static SexpListItemPtr get_listing_opf_support_ship_class();
	static SexpListItemPtr get_listing_opf_ssm_class();
	static SexpListItemPtr get_listing_opf_arrival_location();
	static SexpListItemPtr get_listing_opf_departure_location();
	static SexpListItemPtr get_listing_opf_arrival_anchor_all();
	static SexpListItemPtr get_listing_opf_ship_with_bay();
	static SexpListItemPtr get_listing_opf_soundtrack_name();
	SexpListItemPtr get_listing_opf_ai_goal(int parent_node);
	static SexpListItemPtr get_listing_opf_flexible_argument();
	SexpListItemPtr get_listing_opf_docker_point(int parent_node, int arg_index);
	SexpListItemPtr get_listing_opf_dockee_point(int parent_node);
	SexpListItemPtr get_listing_opf_message();
	static SexpListItemPtr get_listing_opf_who_from();
	static SexpListItemPtr get_listing_opf_priority();
	static SexpListItemPtr get_listing_opf_waypoint_path();
	static SexpListItemPtr get_listing_opf_positive();
	SexpListItemPtr get_listing_opf_mission_name();
	SexpListItemPtr get_listing_opf_ship_point();
	SexpListItemPtr get_listing_opf_goal_name(int parent_node);
	SexpListItemPtr get_listing_opf_ship_wing();
	SexpListItemPtr get_listing_opf_ship_wing_wholeteam();
	SexpListItemPtr get_listing_opf_ship_wing_shiponteam_point();
	SexpListItemPtr get_listing_opf_ship_wing_point();
	SexpListItemPtr get_listing_opf_ship_wing_point_or_none();
	SexpListItemPtr get_listing_opf_order_recipient();
	static SexpListItemPtr get_listing_opf_ship_type();
	static SexpListItemPtr get_listing_opf_keypress();
	SexpListItemPtr get_listing_opf_event_name(int parent_node);
	static SexpListItemPtr get_listing_opf_ai_order();
	static SexpListItemPtr get_listing_opf_skill_level();
	static SexpListItemPtr get_listing_opf_cargo();
	static SexpListItemPtr get_listing_opf_string();
	static SexpListItemPtr get_listing_opf_medal_name();
	static SexpListItemPtr get_listing_opf_weapon_name();
	static SexpListItemPtr get_listing_opf_intel_name();
	static SexpListItemPtr get_listing_opf_ship_class_name();
	static SexpListItemPtr get_listing_opf_huge_weapon();
	static SexpListItemPtr get_listing_opf_ship_not_player();
	SexpListItemPtr get_listing_opf_ship_or_none();
	SexpListItemPtr get_listing_opf_subsystem_or_none(int parent_node, int arg_index);
	SexpListItemPtr get_listing_opf_subsys_or_generic(int parent_node, int arg_index);
	static SexpListItemPtr get_listing_opf_jump_nodes();
	static SexpListItemPtr get_listing_opf_variable_names();
	static SexpListItemPtr get_listing_opf_skybox_model();
	static SexpListItemPtr get_listing_opf_skybox_flags();
	static SexpListItemPtr get_listing_opf_background_bitmap();
	static SexpListItemPtr get_listing_opf_sun_bitmap();
	static SexpListItemPtr get_listing_opf_nebula_storm_type();
	static SexpListItemPtr get_listing_opf_nebula_poof();
	static SexpListItemPtr get_listing_opf_turret_target_order();
	static SexpListItemPtr get_listing_opf_turret_types();
	static SexpListItemPtr get_listing_opf_turret_target_priorities();
	static SexpListItemPtr get_listing_opf_armor_type();
	static SexpListItemPtr get_listing_opf_damage_type();
	static SexpListItemPtr get_listing_opf_animation_type();
	static SexpListItemPtr get_listing_opf_persona();
	static SexpListItemPtr get_listing_opf_post_effect();
	static SexpListItemPtr get_listing_opf_font();
	static SexpListItemPtr get_listing_opf_hud_elements();
	static SexpListItemPtr get_listing_opf_sound_environment();
	static SexpListItemPtr get_listing_opf_sound_environment_option();
	static SexpListItemPtr get_listing_opf_adjust_audio_volume();
	static SexpListItemPtr get_listing_opf_explosion_option();
	static SexpListItemPtr get_listing_opf_weapon_banks();
	static SexpListItemPtr get_listing_opf_builtin_hud_gauge();
	static SexpListItemPtr get_listing_opf_custom_hud_gauge();
	static SexpListItemPtr get_listing_opf_any_hud_gauge();
	static SexpListItemPtr get_listing_opf_ship_effect();
	static SexpListItemPtr get_listing_opf_mission_moods();
	static SexpListItemPtr get_listing_opf_ship_flags();
	static SexpListItemPtr get_listing_opf_wing_flags();
	static SexpListItemPtr get_listing_opf_team_colors();
	static SexpListItemPtr get_listing_opf_nebula_patterns();
	static SexpListItemPtr get_listing_opf_game_snds();
	static SexpListItemPtr get_listing_opf_fireball();
	static SexpListItemPtr get_listing_opf_species();
	static SexpListItemPtr get_listing_opf_language();
	static SexpListItemPtr get_listing_opf_functional_when_eval_type();
	SexpListItemPtr get_listing_opf_animation_name(int parent_node);
	static SexpListItemPtr get_listing_opf_sexp_containers(ContainerType con_type);
	static SexpListItemPtr get_listing_opf_asteroid_types();
	static SexpListItemPtr get_listing_opf_debris_types();
	static SexpListItemPtr get_listing_opf_wing_formation();
	static SexpListItemPtr get_listing_opf_motion_debris();
	static SexpListItemPtr get_listing_opf_bolt_types();
	static SexpListItemPtr get_listing_opf_traitor_overrides();
	static SexpListItemPtr get_listing_opf_lua_general_orders();
	static SexpListItemPtr get_listing_opf_message_types();
	SexpListItemPtr get_listing_opf_lua_enum(int parent_node, int arg_index);
	static SexpListItemPtr get_listing_opf_mission_custom_strings();

	static SexpListItemPtr check_for_dynamic_sexp_enum(int opf);
};