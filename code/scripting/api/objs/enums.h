//
//

#ifndef FS2_OPEN_ENUMS_H
#define FS2_OPEN_ENUMS_H

#include "globalincs/pstypes.h"
#include "scripting/ade_api.h"

#include <optional>

namespace scripting {
namespace api {

enum lua_enum : int32_t {
	LE_ALPHABLEND_FILTER,
	LE_ALPHABLEND_NONE,
	LE_CFILE_TYPE_NORMAL,
	LE_CFILE_TYPE_MEMORY_MAPPED,
	LE_MOUSE_LEFT_BUTTON,
	LE_MOUSE_RIGHT_BUTTON,
	LE_MOUSE_MIDDLE_BUTTON,
	LE_MOUSE_X1_BUTTON,
	LE_MOUSE_X2_BUTTON,
	LE_FLIGHTMODE_FLIGHTCURSOR,
	LE_FLIGHTMODE_SHIPLOCKED,
	LE_ORDER_ATTACK,
	LE_ORDER_ATTACK_WING,
	LE_ORDER_ATTACK_SHIP_CLASS,
	LE_ORDER_ATTACK_ANY,
	LE_ORDER_DEPART,
	LE_ORDER_DISABLE,
	LE_ORDER_DISABLE_TACTICAL,
	LE_ORDER_DISARM,
	LE_ORDER_DISARM_TACTICAL,
	LE_ORDER_DOCK,
	LE_ORDER_EVADE,
	LE_ORDER_FLY_TO,
	LE_ORDER_FORM_ON_WING,
	LE_ORDER_GUARD,
	LE_ORDER_GUARD_WING,
	LE_ORDER_IGNORE,
	LE_ORDER_IGNORE_NEW,
	LE_ORDER_KEEP_SAFE_DISTANCE,
	LE_ORDER_PLAY_DEAD,
	LE_ORDER_PLAY_DEAD_PERSISTENT,
	LE_ORDER_REARM,
	LE_ORDER_STAY_NEAR,
	LE_ORDER_STAY_STILL,
	LE_ORDER_UNDOCK,
	LE_ORDER_WAYPOINTS,
	LE_ORDER_WAYPOINTS_ONCE,
	LE_ORDER_LUA,
	LE_PARTICLE_DEBUG,
	LE_PARTICLE_BITMAP,
	LE_PARTICLE_FIRE,
	LE_PARTICLE_SMOKE,
	LE_PARTICLE_SMOKE2,
	LE_PARTICLE_PERSISTENT_BITMAP,
	LE_SEXPVAR_CAMPAIGN_PERSISTENT,
	LE_SEXPVAR_NOT_PERSISTENT,
	LE_SEXPVAR_PLAYER_PERSISTENT,
	LE_SEXPVAR_TYPE_NUMBER,
	LE_SEXPVAR_TYPE_STRING,
	LE_TEXTURE_STATIC,
	LE_TEXTURE_DYNAMIC,
	LE_LOCK,
	LE_UNLOCK,
	LE_NONE,
	LE_SHIELD_FRONT,
	LE_SHIELD_LEFT,
	LE_SHIELD_RIGHT,
	LE_SHIELD_BACK,
	LE_MISSION_REPEAT,
	LE_NORMAL_CONTROLS,
	LE_LUA_STEERING_CONTROLS,
	LE_LUA_FULL_CONTROLS,
	LE_NORMAL_BUTTON_CONTROLS,
	LE_LUA_ADDITIVE_BUTTON_CONTROL,
	LE_LUA_OVERRIDE_BUTTON_CONTROL,
	LE_VM_INTERNAL,
	LE_VM_EXTERNAL,
	LE_VM_TRACK,
	LE_VM_DEAD_VIEW,
	LE_VM_CHASE,
	LE_VM_OTHER_SHIP,
	LE_VM_EXTERNAL_CAMERA_LOCKED,
	LE_VM_CAMERA_LOCKED,
	LE_VM_WARP_CHASE,
	LE_VM_PADLOCK_UP,
	LE_VM_PADLOCK_REAR,
	LE_VM_PADLOCK_LEFT,
	LE_VM_PADLOCK_RIGHT,
	LE_VM_WARPIN_ANCHOR,
	LE_VM_TOPDOWN,
	LE_VM_FREECAMERA,
	LE_VM_CENTERING,
	LE_MESSAGE_PRIORITY_LOW,
	LE_MESSAGE_PRIORITY_NORMAL,
	LE_MESSAGE_PRIORITY_HIGH,
	LE_OPTION_TYPE_SELECTION,
	LE_OPTION_TYPE_RANGE,
	LE_ASF_EVENTMUSIC,
	LE_ASF_MENUMUSIC,
	LE_ASF_VOICE,
	LE_CONTEXT_VALID,
	LE_CONTEXT_SUSPENDED,
	LE_CONTEXT_INVALID,
	LE_FIREBALL_MEDIUM_EXPLOSION,
	LE_FIREBALL_LARGE_EXPLOSION,
	LE_FIREBALL_WARP_EFFECT,
	LE_GR_RESIZE_NONE, // the sequence and offsets of the LE_GR_* #defines should correspond to the GR_* #defines
	LE_GR_RESIZE_FULL,
	LE_GR_RESIZE_FULL_CENTER,
	LE_GR_RESIZE_MENU,
	LE_GR_RESIZE_MENU_ZOOMED,
	LE_GR_RESIZE_MENU_NO_OFFSET,
	LE_TBOX_FLASH_NAME,
	LE_TBOX_FLASH_CARGO,
	LE_TBOX_FLASH_HULL,
	LE_TBOX_FLASH_STATUS,
	LE_TBOX_FLASH_SUBSYS,
	LE_LUAAI_ACHIEVABLE,
	LE_LUAAI_NOT_YET_ACHIEVABLE,
	LE_LUAAI_UNACHIEVABLE,
	LE_OS_NONE,
	LE_OS_MAIN,
	LE_OS_ENGINE,
	LE_OS_TURRET_BASE_ROTATION,
	LE_OS_TURRET_GUN_ROTATION,
	LE_OS_SUBSYS_ALIVE,
	LE_OS_SUBSYS_DEAD,
	LE_OS_SUBSYS_DAMAGED,
	LE_OS_SUBSYS_ROTATION,
	LE_OS_PLAY_ON_PLAYER,
	LE_OS_LOOPING_DISABLED,
	LE_MOVIE_PRE_FICTION, // the sequence and offsets of the LE_MOVIE_* #defines should correspond to the MOVIE_* #defines
	LE_MOVIE_PRE_CMD_BRIEF,
	LE_MOVIE_PRE_BRIEF,
	LE_MOVIE_PRE_GAME,
	LE_MOVIE_PRE_DEBRIEF,
	LE_MOVIE_POST_DEBRIEF,
	LE_MOVIE_END_CAMPAIGN,
	LE_SCORE_BRIEFING, // the sequence and offsets of the LE_SCORE_* #defines should correspond to the SCORE_* #defines
	LE_SCORE_DEBRIEFING_SUCCESS,
	LE_SCORE_DEBRIEFING_AVERAGE,
	LE_SCORE_DEBRIEFING_FAILURE,
	LE_SCORE_FICTION_VIEWER,
	LE_INVALID, // the sequence and offsets of these five #defines should correspond to the ShipStatus enums
	LE_NOT_YET_PRESENT,
	LE_PRESENT,
	LE_DEATH_ROLL,
	LE_EXITED,
	LE_DC_IS_HULL,
	LE_DC_VAPORIZE,
	LE_DC_SET_VELOCITY,
	LE_DC_FIRE_HOOK,
	LE_RPC_SERVER,
	LE_RPC_CLIENTS,
	LE_RPC_BOTH,
	LE_RPC_RELIABLE,
	LE_RPC_ORDERED,
	LE_RPC_UNRELIABLE,
	LE_HOTKEY_LINE_NONE, // the sequence and offsets of these five #defines should correspond to the HotkeyLineType enums
	LE_HOTKEY_LINE_HEADING,
	LE_HOTKEY_LINE_WING,
	LE_HOTKEY_LINE_SHIP,
	LE_HOTKEY_LINE_SUBSHIP,
	LE_SCROLLBACK_SOURCE_COMPUTER,
	LE_SCROLLBACK_SOURCE_TRAINING,
	LE_SCROLLBACK_SOURCE_HIDDEN,
	LE_SCROLLBACK_SOURCE_IMPORTANT,
	LE_SCROLLBACK_SOURCE_FAILED,
	LE_SCROLLBACK_SOURCE_SATISFIED,
	LE_SCROLLBACK_SOURCE_COMMAND,
	LE_SCROLLBACK_SOURCE_NETPLAYER,
	LE_MULTI_TYPE_COOP,
	LE_MULTI_TYPE_TEAM,
	LE_MULTI_TYPE_DOGFIGHT,
	LE_MULTI_TYPE_SQUADWAR,
	LE_MULTI_OPTION_RANK,
	LE_MULTI_OPTION_LEAD,
	LE_MULTI_OPTION_ANY,
	LE_MULTI_OPTION_HOST,
	LE_MULTI_GAME_TYPE_OPEN,
	LE_MULTI_GAME_TYPE_PASSWORD,
	LE_MULTI_GAME_TYPE_RANK_ABOVE,
	LE_MULTI_GAME_TYPE_RANK_BELOW,
	LE_SEXP_TRUE,
	LE_SEXP_FALSE,
	LE_SEXP_KNOWN_FALSE,
	LE_SEXP_KNOWN_TRUE,
	LE_SEXP_UNKNOWN,
	LE_SEXP_NAN,
	LE_SEXP_NAN_FOREVER,
	LE_SEXP_CANT_EVAL,
	LE_COMMIT_SUCCESS,
	LE_COMMIT_FAIL,
	LE_COMMIT_PLAYER_NO_WEAPONS,
	LE_COMMIT_NO_REQUIRED_WEAPON,
	LE_COMMIT_NO_REQUIRED_WEAPON_MULTIPLE,
	LE_COMMIT_BANK_GAP_ERROR,
	LE_COMMIT_PLAYER_NO_SLOT,
	LE_COMMIT_MULTI_PLAYERS_NO_SHIPS,
	LE_COMMIT_MULTI_NOT_ALL_ASSIGNED,
	LE_COMMIT_MULTI_NO_PRIMARY,
	LE_COMMIT_MULTI_NO_SECONDARY,
	LE_SQUAD_MESSAGE_ATTACK_TARGET,
	LE_SQUAD_MESSAGE_DISABLE_TARGET,
	LE_SQUAD_MESSAGE_DISARM_TARGET,
	LE_SQUAD_MESSAGE_PROTECT_TARGET,
	LE_SQUAD_MESSAGE_IGNORE_TARGET,
	LE_SQUAD_MESSAGE_FORMATION,
	LE_SQUAD_MESSAGE_COVER_ME,
	LE_SQUAD_MESSAGE_ENGAGE_ENEMY,
	LE_SQUAD_MESSAGE_CAPTURE_TARGET,
	LE_SQUAD_MESSAGE_REARM_REPAIR_ME,
	LE_SQUAD_MESSAGE_ABORT_REARM_REPAIR,
	LE_SQUAD_MESSAGE_STAY_NEAR_ME,
	LE_SQUAD_MESSAGE_STAY_NEAR_TARGET,
	LE_SQUAD_MESSAGE_KEEP_SAFE_DIST,
	LE_SQUAD_MESSAGE_DEPART,
	LE_SQUAD_MESSAGE_DISABLE_SUBSYSTEM,
	LE_SQUAD_MESSAGE_LUA_AI,
	LE_BUILTIN_MESSAGE_ATTACK_TARGET,
	LE_BUILTIN_MESSAGE_DISABLE_TARGET,
	LE_BUILTIN_MESSAGE_DISARM_TARGET,
	LE_BUILTIN_MESSAGE_ATTACK_SUBSYSTEM,
	LE_BUILTIN_MESSAGE_PROTECT_TARGET,
	LE_BUILTIN_MESSAGE_FORM_ON_MY_WING,
	LE_BUILTIN_MESSAGE_COVER_ME,
	LE_BUILTIN_MESSAGE_IGNORE,
	LE_BUILTIN_MESSAGE_ENGAGE,
	LE_BUILTIN_MESSAGE_WARP_OUT,
	LE_BUILTIN_MESSAGE_DOCK_YES,
	LE_BUILTIN_MESSAGE_YESSIR,
	LE_BUILTIN_MESSAGE_NOSIR,
	LE_BUILTIN_MESSAGE_NO_TARGET,
	LE_BUILTIN_MESSAGE_CHECK_6,
	LE_BUILTIN_MESSAGE_PLAYER_DIED,
	LE_BUILTIN_MESSAGE_PRAISE,
	LE_BUILTIN_MESSAGE_HIGH_PRAISE,
	LE_BUILTIN_MESSAGE_BACKUP,
	LE_BUILTIN_MESSAGE_HELP,
	LE_BUILTIN_MESSAGE_WINGMAN_SCREAM,
	LE_BUILTIN_MESSAGE_PRAISE_SELF,
	LE_BUILTIN_MESSAGE_REARM_REQUEST,
	LE_BUILTIN_MESSAGE_REPAIR_REQUEST,
	LE_BUILTIN_MESSAGE_PRIMARIES_LOW,
	LE_BUILTIN_MESSAGE_REARM_PRIMARIES,
	LE_BUILTIN_MESSAGE_REARM_WARP,
	LE_BUILTIN_MESSAGE_ON_WAY,
	LE_BUILTIN_MESSAGE_ALREADY_ON_WAY,
	LE_BUILTIN_MESSAGE_REPAIR_DONE,
	LE_BUILTIN_MESSAGE_REPAIR_ABORTED,
	LE_BUILTIN_MESSAGE_SUPPORT_KILLED,
	LE_BUILTIN_MESSAGE_ALL_ALONE,
	LE_BUILTIN_MESSAGE_ARRIVE_ENEMY,
	LE_BUILTIN_MESSAGE_OOPS,
	LE_BUILTIN_MESSAGE_HAMMER_SWINE,
	LE_BUILTIN_MESSAGE_AWACS_75,
	LE_BUILTIN_MESSAGE_AWACS_25,
	LE_BUILTIN_MESSAGE_STRAY_WARNING,
	LE_BUILTIN_MESSAGE_STRAY_WARNING_FINAL,
	LE_BUILTIN_MESSAGE_INSTRUCTOR_HIT,
	LE_BUILTIN_MESSAGE_INSTRUCTOR_ATTACK,
	LE_BUILTIN_MESSAGE_ALL_CLEAR,
	LE_BUILTIN_MESSAGE_PERMISSION,
	LE_BUILTIN_MESSAGE_STRAY,
	ENUM_NEXT_INDEX,
	ENUM_COMBINATION,
	ENUM_INVALID
};

struct lua_enum_def_list : public flag_def_list_new<lua_enum> {
	std::optional<int32_t> value;
	constexpr lua_enum_def_list(const char* enum_name, lua_enum flag, bool used) : flag_def_list_new<lua_enum>{ enum_name, flag, used, false }, value(std::nullopt) {}
	constexpr lua_enum_def_list(const char* enum_name, lua_enum flag, int32_t val, bool used) : flag_def_list_new<lua_enum>{ enum_name, flag, used, false }, value(val) {}
};

extern const lua_enum_def_list Enumerations[];
extern const size_t Num_enumerations;

struct enum_h {
private:
	enum class last_combine_op { NATIVE, AND, OR };
	std::optional<SCP_string> name;
	last_combine_op last_op = last_combine_op::NATIVE;

public:
	lua_enum index;
	bool is_constant;
	std::optional<int32_t> value;

	enum_h();

	explicit enum_h(lua_enum n_index);

	SCP_string getName() const;

	bool isValid() const;

	friend enum_h operator&(const enum_h& l, const enum_h& other);
	friend enum_h operator|(const enum_h& l, const enum_h& other);

	static void serialize(lua_State* /*L*/, const scripting::ade_table_entry& /*tableEntry*/, const luacpp::LuaValue& value, ubyte* data, int& packet_size);
	static void deserialize(lua_State* /*L*/, const scripting::ade_table_entry& /*tableEntry*/, char* data_ptr, ubyte* data, int& offset);
};


DECLARE_ADE_OBJ(l_Enum, enum_h);

}
}

#endif //FS2_OPEN_ENUMS_H
