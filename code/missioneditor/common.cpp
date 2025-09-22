// methods and members common to any mission editor FSO may have
#include "common.h"

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

// Given an object index, find the ship index for that object.
int get_ship_from_obj(int obj)
{
	if ((Objects[obj].type == OBJ_SHIP) || (Objects[obj].type == OBJ_START))
		return Objects[obj].instance;

	Int3();
	return 0;
}

int get_ship_from_obj(object* objp)
{
	if ((objp->type == OBJ_SHIP) || (objp->type == OBJ_START))
		return objp->instance;

	Assertion(false, "get_ship_from_obj: Invalid object type %d", objp->type);
	return 0;
}