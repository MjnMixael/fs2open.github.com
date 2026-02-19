// methods and members common to any mission editor FSO may have
#include "common.h"
#include "mission/missionparse.h"
#include "iff_defs/iff_defs.h"
#include "ship/ship.h"

#include "iff_defs/iff_defs.h"
#include "mission/missionparse.h"

// to keep track of data
char Voice_abbrev_briefing[NAME_LENGTH];
char Voice_abbrev_campaign[NAME_LENGTH];
char Voice_abbrev_command_briefing[NAME_LENGTH];
char Voice_abbrev_debriefing[NAME_LENGTH];
char Voice_abbrev_message[NAME_LENGTH];
char Voice_abbrev_mission[NAME_LENGTH];
bool Voice_no_replace_filenames;
char Voice_script_entry_format[NOTES_LENGTH];
int Voice_export_selection; // 0=everything, 1=cmd brief, 2=brief, 3=debrief, 4=messages
bool Voice_group_messages;

SCP_string Voice_script_default_string = "Sender: $sender\r\nPersona: $persona\r\nFile: $filename\r\nMessage: $message";
SCP_string Voice_script_instructions_string = "$name - name of the message\r\n"
                                              "$filename - name of the message file\r\n"
                                              "$message - text of the message\r\n"
                                              "$persona - persona of the sender\r\n"
                                              "$sender - name of the sender\r\n"
                                              "$note - message notes\r\n\r\n"
                                              "Note that $persona and $sender will only appear for the Message section.";

void time_to_mission_info_string(const std::tm* src, char* dest, size_t dest_max_len)
{
	std::strftime(dest, dest_max_len, "%x at %X", src);
}

void generate_weaponry_usage_list_team(int team, int* arr)
{
	int i;

	for (i = 0; i < MAX_WEAPON_TYPES; i++) {
		arr[i] = 0;
	}

	if (The_mission.game_type & MISSION_TYPE_MULTI_TEAMS) {
		Assert(team >= 0 && team < MAX_TVT_TEAMS);

		for (i = 0; i < MAX_TVT_WINGS_PER_TEAM; i++) {
			generate_weaponry_usage_list_wing(TVT_wings[(team * MAX_TVT_WINGS_PER_TEAM) + i], arr);
		}
	} else {
		for (i = 0; i < MAX_STARTING_WINGS; i++) {
			generate_weaponry_usage_list_wing(Starting_wings[i], arr);
		}
	}
}

void generate_weaponry_usage_list_wing(int wing_num, int* arr)
{
	int i, j;
	ship_weapon* swp;

	if (wing_num < 0) {
		return;
	}

	i = Wings[wing_num].wave_count;
	while (i--) {
		swp = &Ships[Wings[wing_num].ship_index[i]].weapons;
		j = swp->num_primary_banks;
		while (j--) {
			if (swp->primary_bank_weapons[j] >= 0 &&
				swp->primary_bank_weapons[j] < static_cast<int>(Weapon_info.size())) {
				arr[swp->primary_bank_weapons[j]]++;
			}
		}

		j = swp->num_secondary_banks;
		while (j--) {
			if (swp->secondary_bank_weapons[j] >= 0 &&
				swp->secondary_bank_weapons[j] < static_cast<int>(Weapon_info.size())) {
				arr[swp->secondary_bank_weapons[j]] +=
					(int)floor((swp->secondary_bank_ammo[j] * swp->secondary_bank_capacity[j] / 100.0f /
								   Weapon_info[swp->secondary_bank_weapons[j]].cargo_size) +
							   0.5f);
			}
		}
	}
}

// Goober5000
void stuff_special_arrival_anchor_name(char* buf, int iff_index, int restrict_to_players, int retail_format)
{
	char* iff_name = Iff_info[iff_index].iff_name;

	// stupid retail hack
	if (retail_format && !stricmp(iff_name, "hostile") && !restrict_to_players)
		iff_name = "enemy";

	if (restrict_to_players)
		sprintf(buf, "<any %s player>", iff_name);
	else
		sprintf(buf, "<any %s>", iff_name);

	strlwr(buf);
}

// Goober5000
void stuff_special_arrival_anchor_name(char* buf, int anchor_num, int retail_format)
{
	// filter out iff
	int iff_index = anchor_num;
	iff_index &= ~SPECIAL_ARRIVAL_ANCHOR_FLAG;
	iff_index &= ~SPECIAL_ARRIVAL_ANCHOR_PLAYER_FLAG;

	// filter players
	int restrict_to_players = (anchor_num & SPECIAL_ARRIVAL_ANCHOR_PLAYER_FLAG);

	// get name
	stuff_special_arrival_anchor_name(buf, iff_index, restrict_to_players, retail_format);
}

char* Docking_bay_list[MAX_DOCKS];

int get_docking_list(int model_index)
{
	int i;
	polymodel* pm;

	pm = model_get(model_index);
	Assert(pm->n_docks <= MAX_DOCKS);
	for (i = 0; i < pm->n_docks; i++)
		Docking_bay_list[i] = pm->docking_bays[i].name;

	return pm->n_docks;
}

bool query_valid_object(int index)
{
	bool obj_found = false;
	object* ptr;

	if (index < 0 || index >= MAX_OBJECTS || Objects[index].type == OBJ_NONE)
		return false;

	ptr = GET_FIRST(&obj_used_list);
	while (ptr != END_OF_LIST(&obj_used_list)) {
		Assert(ptr->type != OBJ_NONE);
		if (OBJ_INDEX(ptr) == index)
			obj_found = true;

		ptr = GET_NEXT(ptr);
	}

	Assert(obj_found); // just to make sure it's in the list like it should be.
	return true;
}

// Given an object index, find the ship index for that object.
int get_ship_from_obj(int obj)
{
	Assertion(query_valid_object(obj), "Invalid object index detected!");

	return get_ship_from_obj(&Objects[obj]);
}

int get_ship_from_obj(object* objp)
{
	if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START))
		return objp->instance;

	Assertion(false, "get_ship_from_obj: Invalid object type %d", objp->type);
	return 0;
}