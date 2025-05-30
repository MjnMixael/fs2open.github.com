/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 


#define REDALERT_INTERNAL
#include "ai/aigoals.h"
#include "cfile/cfile.h"
#include "freespace.h"
#include "gamesequence/gamesequence.h"
#include "gamesnd/gamesnd.h"
#include "globalincs/alphacolors.h"
#include "globalincs/linklist.h"
#include "graphics/font.h"
#include "hud/hudwingmanstatus.h"
#include "io/key.h"
#include "io/mouse.h"
#include "io/timer.h"
#include "mission/missionbriefcommon.h"
#include "mission/missioncampaign.h"
#include "mission/missiongoals.h"
#include "missionui/missionscreencommon.h"
#include "missionui/missionweaponchoice.h"
#include "missionui/redalert.h"
#include "network/multi.h"
#include "network/multiteamselect.h"
#include "network/multi_endgame.h"
#include "mod_table/mod_table.h"
#include "model/model.h"
#include "object/deadobjectdock.h"
#include "object/objectdock.h"
#include "ship/ship.h"
#include "sound/audiostr.h"
#include "sound/fsspeech.h"
#include "weapon/weapon.h"

#include <stdexcept>


/////////////////////////////////////////////////////////////////////////////
// Red Alert Mission-Level
/////////////////////////////////////////////////////////////////////////////

static TIMESTAMP Red_alert_new_mission_timestamp;		// timestamp used to give user a little warning for red alerts
static int Red_alert_voice_started;

SCP_vector<red_alert_ship_status> Red_alert_ship_status;
SCP_vector<red_alert_wing_status> Red_alert_wing_status;
SCP_string Red_alert_precursor_mission;

/////////////////////////////////////////////////////////////////////////////
// Red Alert Interface
/////////////////////////////////////////////////////////////////////////////

const char *Red_alert_fname[GR_NUM_RESOLUTIONS] = {
	"RedAlert",
	"2_RedAlert"
};

const char *Red_alert_mask[GR_NUM_RESOLUTIONS] = {
	"RedAlert-m",
	"2_RedAlert-m"
};

// font to use for "incoming transmission"
int Ra_flash_font[GR_NUM_RESOLUTIONS] = {
	font::FONT2,
	font::FONT2
};

int Ra_flash_y[GR_NUM_RESOLUTIONS] = {
	140,
	200
};

/*
static int Ra_flash_coords[GR_NUM_RESOLUTIONS][2] = {
	{
		61, 108			// GR_640
	},
	{
		61, 108			// GR_1024
	}
};
*/

#define NUM_BUTTONS						2

#define RA_REPLAY_MISSION				0
#define RA_CONTINUE						1

static ui_button_info Buttons[GR_NUM_RESOLUTIONS][NUM_BUTTONS] = {
	{	// GR_640
		ui_button_info("RAB_00",	2,		445,	-1,	-1, 0),
		ui_button_info("RAB_01",	575,	432,	-1,	-1, 1),
	},	
	{	// GR_1024
		ui_button_info("2_RAB_00",	4,		712,	-1,	-1, 0),
		ui_button_info("2_RAB_01",	920,	691,	-1,	-1, 1),
	}
};

#define RED_ALERT_NUM_TEXT		3
UI_XSTR Red_alert_text[GR_NUM_RESOLUTIONS][RED_ALERT_NUM_TEXT] = {
	{ // GR_640
		{ "Replay",		1405,	46,	451,	UI_XSTR_COLOR_PINK,	-1, &Buttons[0][RA_REPLAY_MISSION].button },
		{ "Previous Mission",	1452,	46,	462,	UI_XSTR_COLOR_PINK,	-1, &Buttons[0][RA_REPLAY_MISSION].button },
		{ "Continue",	1069,	564,	413,	UI_XSTR_COLOR_PINK,	-1, &Buttons[0][RA_CONTINUE].button },
	},
	{ // GR_1024
		{ "Replay",		1405,	75,	722,	UI_XSTR_COLOR_PINK,	-1, &Buttons[1][RA_REPLAY_MISSION].button },
		{ "Previous Mission",	1452,	75,	733,	UI_XSTR_COLOR_PINK,	-1, &Buttons[1][RA_REPLAY_MISSION].button },
		{ "Continue",	1069,	902,	661,	UI_XSTR_COLOR_PINK,	-1, &Buttons[1][RA_CONTINUE].button },
	}
};

// indicies for coordinates
#define RA_X_COORD 0
#define RA_Y_COORD 1
#define RA_W_COORD 2
#define RA_H_COORD 3


static UI_TIMESTAMP Text_delay;

int Ra_brief_text_wnd_coords[GR_NUM_RESOLUTIONS][4] = {
	{
		14, 151, 522, 289
	},
	{
		52, 241, 785, 463
	}
};

static UI_WINDOW Ui_window;
// static hud_anim Flash_anim;
static int Background_bitmap;
static int Red_alert_inited = 0;

static int Red_alert_voice;

// open and pre-load the stream buffers for the different voice streams
void red_alert_voice_load()
{
	Assert( Briefing != NULL );
	if ( strnicmp(Briefing->stages[0].voice, NOX("none"), 4) != 0 && (Briefing->stages[0].voice[0] != '\0') ) {
		Red_alert_voice = audiostream_open( Briefing->stages[0].voice, ASF_VOICE );
	}
}

// close all the briefing voice streams
void red_alert_voice_unload()
{
	if ( Red_alert_voice != -1 ) {
		audiostream_close_file(Red_alert_voice, 0);
		Red_alert_voice = -1;
	}
}

// start playback of the red alert voice
void red_alert_voice_play()
{
	if ( !Briefing_voice_enabled ) {
		return;
	}

	if ( Red_alert_voice < 0 ) {
		// play simulated speech?
		if (fsspeech_play_from(FSSPEECH_FROM_BRIEFING)) {
			if (fsspeech_playing()) {
				return;
			}

			fsspeech_play(FSSPEECH_FROM_BRIEFING, Briefing->stages[0].text.c_str());
			Red_alert_voice_started = 1;
		}
	} else {
		if (audiostream_is_playing(Red_alert_voice)) {
			return;
		}

		audiostream_play(Red_alert_voice, Master_voice_volume, 0);
		Red_alert_voice_started = 1;
	}
}

// stop playback of the red alert voice
void red_alert_voice_stop()
{
	if ( !Red_alert_voice_started )
		return;

	if (Red_alert_voice < 0) {
		fsspeech_stop();
	} else {
		audiostream_stop(Red_alert_voice, 1, 0);	// stream is automatically rewound
	}
}

// pausing and unpausing of red alert voice
void red_alert_voice_pause()
{
	if ( !Red_alert_voice_started )
		return;

	if (Red_alert_voice < 0) {
		fsspeech_pause(true);
	} else {
		audiostream_pause(Red_alert_voice);
	}
}

void red_alert_voice_unpause()
{
	if ( !Red_alert_voice_started )
		return;

	if (Red_alert_voice < 0) {
		fsspeech_pause(false);
	} else {
		audiostream_unpause(Red_alert_voice);
	}
}

// a button was pressed, deal with it
void red_alert_button_pressed(int n)
{
	switch (n) {
	case RA_CONTINUE:		
		// warp the mouse cursor the the middle of the screen for those who control with a mouse
		mouse_set_pos( gr_screen.max_w/2, gr_screen.max_h/2 );

		if(Game_mode & GM_MULTIPLAYER){	
			multi_ts_commit_pressed();
		} else {
			gameseq_post_event(GS_EVENT_ENTER_GAME);
		}
		break;

	case RA_REPLAY_MISSION:
		if ( (Game_mode & GM_CAMPAIGN_MODE) && !(Game_mode & GM_MULTIPLAYER) ) {
			// TODO: make call to campaign code to set correct mission for loading
			// mission_campaign_play_previous_mission(Red_alert_precursor_mission);
			if ( !mission_campaign_previous_mission() ) {
				gamesnd_play_iface(InterfaceSounds::GENERAL_FAIL);
				break;
			}

			gameseq_post_event(GS_EVENT_START_GAME);
		} else {
			gamesnd_play_iface(InterfaceSounds::GENERAL_FAIL);
		}
		break;
	}
}

// blit "incoming transmission"
#define RA_FLASH_CYCLE			0.25f
float Ra_flash_time = 0.0f;
int Ra_flash_up = 0;
void red_alert_blit_title()
{
	const char *str = XSTR("Incoming Transmission", 1406);
	int w, h;

	// get the string size	
	font::set_font(Ra_flash_font[gr_screen.res]);
	gr_get_string_size(&w, &h, str);

	// set alpha color
	color flash_color;
	if(Ra_flash_up){
		gr_init_alphacolor(&flash_color, (int)(255.0f * (Ra_flash_time / RA_FLASH_CYCLE)), 0, 0, 255);
	} else {
		gr_init_alphacolor(&flash_color, (int)(255.0f * (1.0f - (Ra_flash_time / RA_FLASH_CYCLE))), 0, 0, 255);
	}

	// draw
	gr_set_color_fast(&flash_color);
	gr_string(Ra_brief_text_wnd_coords[gr_screen.res][0] + ((Ra_brief_text_wnd_coords[gr_screen.res][2] - w) / 2), Ra_flash_y[gr_screen.res] - h - 5, str, GR_RESIZE_MENU);
	gr_set_color_fast(&Color_normal);	

	// increment flash time
	Ra_flash_time += flFrametime;
	if(Ra_flash_time >= RA_FLASH_CYCLE){
		Ra_flash_time = 0.0f;
		Ra_flash_up = !Ra_flash_up;
	}

	// back to the original font
	font::set_font(font::FONT1);
}

// Called once when red alert interface is started
void red_alert_init()
{
	int i;
	ui_button_info *b;

	if ( Red_alert_inited ) {
		return;
	}

	// for multiplayer, change the state in my netplayer structure
	if (Game_mode & GM_MULTIPLAYER) {
		Net_player->state = NETPLAYER_STATE_RED_ALERT;
	}

	Ui_window.create(0, 0, gr_screen.max_w_unscaled, gr_screen.max_h_unscaled, 0);
	Ui_window.set_mask_bmap(Red_alert_mask[gr_screen.res]);

	for (i=0; i<NUM_BUTTONS; i++) {
		b = &Buttons[gr_screen.res][i];

		b->button.create(&Ui_window, "", b->x, b->y, 60, 30, 0, 1);
		// set up callback for when a mouse first goes over a button
		b->button.set_highlight_action(common_play_highlight_sound);
		b->button.set_bmaps(b->filename);
		b->button.link_hotspot(b->hotspot);
	}

	// all text
	for(i=0; i<RED_ALERT_NUM_TEXT; i++){
		Ui_window.add_XSTR(&Red_alert_text[gr_screen.res][i]);
	}

	// set up red alert hotkeys
	Buttons[gr_screen.res][RA_CONTINUE].button.set_hotkey(KEY_CTRLED | KEY_ENTER);
	
	// hud_anim_init(&Flash_anim, Ra_flash_coords[gr_screen.res][RA_X_COORD], Ra_flash_coords[gr_screen.res][RA_Y_COORD], NOX("AlertFlash"));
	// hud_anim_load(&Flash_anim);

	Red_alert_voice = -1;

	if ( !Briefing ) {
		Briefing = &Briefings[0];			
	}

	// load in background image and flashing red alert animation
	Background_bitmap = mission_ui_background_load(Briefing->background[gr_screen.res], Red_alert_fname[gr_screen.res]);

	// Make sure word wrapping and rendering use the same font
	font::set_font(font::FONT1);

	if ( Briefing->num_stages > 0 ) {
		brief_color_text_init(Briefing->stages[0].text.c_str(), Ra_brief_text_wnd_coords[gr_screen.res][RA_W_COORD], default_redalert_briefing_color, 0);
	}

	red_alert_voice_load();

	Text_delay = ui_timestamp(200);

	Red_alert_voice_started = 0;
	Red_alert_inited = 1;
}

// Called once when the red alert interface is exited
void red_alert_close()
{
	if (Red_alert_inited) {

		red_alert_voice_stop();
		red_alert_voice_unload();

		weapon_select_close_team();

		if (Background_bitmap >= 0) {
			bm_release(Background_bitmap);
		}
		
		Ui_window.destroy();
		// bm_unload(&Flash_anim);
		common_free_interface_palette();		// restore game palette
		game_flush();
	}

	Red_alert_inited = 0;

	fsspeech_stop();
}

// called once per frame when game state is GS_STATE_RED_ALERT
void red_alert_do_frame(float frametime)
{
	int i, k;	

	// ensure that the red alert interface has been initialized
	if (!Red_alert_inited) {
		Int3();
		return;
	}

	// commit if skipping briefing, but not in multi - Goober5000
	if (!(Game_mode & GM_MULTIPLAYER)) {
		if (The_mission.flags[Mission::Mission_Flags::No_briefing])
		{
			red_alert_button_pressed(RA_CONTINUE);
			return;
		}
	}

	k = Ui_window.process() & ~KEY_DEBUGGED;
	switch (k) {
		case KEY_ESC:
//			gameseq_post_event(GS_EVENT_ENTER_GAME);
			if (Game_mode & GM_MULTIPLAYER) {
				gamesnd_play_iface(InterfaceSounds::USER_SELECT);
				multi_quit_game(PROMPT_ALL);
			} else {
				gameseq_post_event(GS_EVENT_MAIN_MENU);
			}
			break;
	}	// end switch

	for (i=0; i<NUM_BUTTONS; i++){
		if (Buttons[gr_screen.res][i].button.pressed()){
			red_alert_button_pressed(i);
		}
	}

	GR_MAYBE_CLEAR_RES(Background_bitmap);
	if (Background_bitmap >= 0) {
		gr_set_bitmap(Background_bitmap);
		gr_bitmap(0, 0, GR_RESIZE_MENU);
	} 

	Ui_window.draw();
	// hud_anim_render(&Flash_anim, frametime);

	font::set_font(font::FONT1);

	if ( ui_timestamp_elapsed(Text_delay) ) {
		int finished_wipe = 0;
		if ( Briefing->num_stages > 0 ) {
			finished_wipe = brief_render_text(0, Ra_brief_text_wnd_coords[gr_screen.res][RA_X_COORD], Ra_brief_text_wnd_coords[gr_screen.res][RA_Y_COORD], Ra_brief_text_wnd_coords[gr_screen.res][RA_H_COORD], frametime, 0);
		}

		if (finished_wipe) {
			red_alert_voice_play();
		}
	}

	// blit incoming transmission
	red_alert_blit_title();

	gr_flip();
}

// set the red alert status for the current mission
void red_alert_invalidate_timestamp()
{
	Red_alert_new_mission_timestamp = TIMESTAMP::invalid();
}

// Store a ships weapons into a wingman status structure
void red_alert_store_weapons(red_alert_ship_status *ras, ship_weapon *swp)
{
	int i;
	weapon_info *wip;
	wep_t weapons;

	// Make sure there isn't any data from the previous ship.
	ras->primary_weapons.clear();
	ras->secondary_weapons.clear();

	if (swp == NULL) {
		return;
	}

	// edited to accommodate ballistics - Goober5000
	for (i = 0; i < swp->num_primary_banks; i++) {
		weapons.index = swp->primary_bank_weapons[i];

		if (weapons.index < 0) {
			continue;
		}

		wip = &Weapon_info[weapons.index];

		if (wip->wi_flags[Weapon::Info_Flags::Ballistic]) {
			// to avoid triggering the below condition: this way, minimum ammo will be 2...
			// since the red-alert representation of a conventional primary is 0 -> not used,
			// 1 -> used, I added the representation 2 and above -> ballistic primary
			weapons.count = swp->primary_bank_ammo[i] + 2;
		} else {
			weapons.count = 1;
		}

		ras->primary_weapons.push_back( weapons );
	}

	for (i = 0; i < swp->num_secondary_banks; i++) {
		weapons.index = swp->secondary_bank_weapons[i];

		if (weapons.index < 0) {
			continue;
		}

		weapons.count = swp->secondary_bank_ammo[i];

		ras->secondary_weapons.push_back( weapons );
	}
}

// Take the weapons stored in a ship_status struct, and bash them into the ship weapons struct
void red_alert_bash_weapons(const red_alert_ship_status *ras, ship_weapon *swp)
{
	int i, list_size = 0;

	// restore from ship_exited
	if ( (ras->ship_class == RED_ALERT_DESTROYED_SHIP_CLASS) || (ras->ship_class == RED_ALERT_PLAYER_DEL_SHIP_CLASS) ) {
		return;
	}

	if (!ras->primary_weapons.empty()) {
		// modified to accommodate ballistics - Goober5000
		list_size = static_cast<int>(ras->primary_weapons.size());
		CLAMP(list_size, 0, MAX_SHIP_PRIMARY_BANKS);
		for (i = 0; i < list_size; i++) {
			Assert( ras->primary_weapons[i].index >= 0 );

			swp->primary_bank_weapons[i] = ras->primary_weapons[i].index;
			swp->primary_bank_ammo[i] = ras->primary_weapons[i].count;

			if (Weapon_info[swp->primary_bank_weapons[i]].wi_flags[Weapon::Info_Flags::Ballistic]) {
				// adjust to correct ammo count, per red_alert_store_weapons()
				swp->primary_bank_ammo[i] -= 2;
			}
		}
		swp->num_primary_banks = list_size;
	}

	if (!ras->secondary_weapons.empty()) {
		// bash secondary weapons
		list_size = static_cast<int>(ras->secondary_weapons.size());
		CLAMP(list_size, 0, MAX_SHIP_SECONDARY_BANKS);
		for (i = 0; i < list_size; i++) {
			Assert( ras->secondary_weapons[i].index >= 0 );

			swp->secondary_bank_weapons[i] = ras->secondary_weapons[i].index;
			swp->secondary_bank_ammo[i] = ras->secondary_weapons[i].count;
		}
		swp->num_secondary_banks = list_size;
	}
}

void red_alert_bash_weapons(const red_alert_ship_status *ras, p_object *pobjp)
{
	int i, list_size = 0;
	ship_info *sip;
	subsys_status *sssp = NULL;

	// restore from ship_exited
	if ( (ras->ship_class == RED_ALERT_DESTROYED_SHIP_CLASS) || (ras->ship_class == RED_ALERT_PLAYER_DEL_SHIP_CLASS) )
		return;
	sip = &Ship_info[ras->ship_class];

	// parse objects use the "pilot" subsystem
	for (i = 0; i < pobjp->subsys_count; i++)
	{
		if (!stricmp(Subsys_status[pobjp->subsys_index + i].name, "pilot"))
		{
			sssp = &Subsys_status[pobjp->subsys_index + i];
			break;
		}
	}

	if (sssp == NULL)
	{
		Warning(LOCATION, "Parse object data for ship '%s' doesn't contain the 'Pilot' subsystem!", pobjp->name);
		return;
	}

	if (!ras->primary_weapons.empty())
	{
		// bash primary weapons
		list_size = static_cast<int>(ras->primary_weapons.size());
		CLAMP(list_size, 0, MAX_SHIP_PRIMARY_BANKS);
		for (i = 0; i < list_size; i++)
		{
			Assert( ras->primary_weapons[i].index >= 0 );
			sssp->primary_banks[i] = ras->primary_weapons[i].index;

			if (Weapon_info[sssp->primary_banks[i]].wi_flags[Weapon::Info_Flags::Ballistic])
			{
				float max_count = sip->primary_bank_ammo_capacity[i] / Weapon_info[sssp->primary_banks[i]].cargo_size;
				sssp->primary_ammo[i] = fl2i(std::lround(100.0f * (ras->primary_weapons[i].count - 2) / max_count));
			}
			else
				sssp->primary_ammo[i] = 100;
		}
	}

	if (!ras->secondary_weapons.empty())
	{
		// bash secondary weapons
		list_size = static_cast<int>(ras->secondary_weapons.size());
		CLAMP(list_size, 0, MAX_SHIP_SECONDARY_BANKS);
		for (i = 0; i < list_size; i++)
		{
			Assert( ras->secondary_weapons[i].index >= 0 );
			sssp->secondary_banks[i] = ras->secondary_weapons[i].index;

			float max_count = sip->secondary_bank_ammo_capacity[i] / Weapon_info[sssp->secondary_banks[i]].cargo_size;
			sssp->secondary_ammo[i] = fl2i(std::lround(100.0f * ras->secondary_weapons[i].count / max_count));
		}
	}
}

void red_alert_bash_subsys_status(const red_alert_ship_status *ras, ship *shipp)
{
	ship_subsys *ss;
	int i, count = 0;
	int list_size;

	// restore from ship_exited
	if ( (ras->ship_class == RED_ALERT_DESTROYED_SHIP_CLASS) || (ras->ship_class == RED_ALERT_PLAYER_DEL_SHIP_CLASS) ) {
		return;
	}

	if (!ras->subsys_current_hits.empty()) {
		ss = GET_FIRST(&shipp->subsys_list);
		while ( ss != END_OF_LIST( &shipp->subsys_list ) ) {
			// using at() here for the bounds check, although out-of-bounds should
			// probably never happen here
			try {
				ss->current_hits = ras->subsys_current_hits.at(count);
			} catch (const std::out_of_range&) {
				break;
			}

			if (ss->current_hits <= 0 && ss->submodel_instance_1 != nullptr) {
				ss->submodel_instance_1->blown_off = true;
			}

			ss = GET_NEXT( ss );
			count++;
		}
	}

	if (!ras->subsys_aggregate_current_hits.empty()) {
		list_size = static_cast<int>(ras->subsys_aggregate_current_hits.size());
		CLAMP(list_size, 0, SUBSYSTEM_MAX);
		for (i = 0; i < list_size; i++) {
			shipp->subsys_info[i].aggregate_current_hits = ras->subsys_aggregate_current_hits[i];
		}
	}
}

extern int insert_subsys_status(p_object *pobjp);

void red_alert_bash_subsys_status(const red_alert_ship_status *ras, p_object *pobjp)
{
	int i, j;
	ship_info *sip;
	model_subsystem *psub = NULL;
	subsys_status *sssp = NULL;

	// restore from ship_exited
	if ( (ras->ship_class == RED_ALERT_DESTROYED_SHIP_CLASS) || (ras->ship_class == RED_ALERT_PLAYER_DEL_SHIP_CLASS) )
		return;
	sip = &Ship_info[ras->ship_class];

	// do this differently than the other bash_subsys_status... since the p_object may not contain
	// all the subsystems, iterate on the red_alert_ship_status entries rather than the p_object's
	// and create missing subsystems where necessary
	for (i = 0; i < (int) ras->subsys_current_hits.size(); i++)
	{
		psub = &sip->subsystems[i];

		// in the p_object, subsystem 0 is the pilot, and afterwards the subsystems go in order
		j = i + 1;

		// this subsystem is in the p_object
		if (j < pobjp->subsys_count)
		{
			sssp = &Subsys_status[pobjp->subsys_index + j];
		}
		// must create subsystem (same technique as in parse_copy_damage)
		else
		{
			// jam in the new subsystem at the end of the existing list for this parse object
			int new_idx = insert_subsys_status(pobjp);
			Assert(new_idx == pobjp->subsys_index + j);
			sssp = &Subsys_status[new_idx];

			strcpy_s(sssp->name, psub->subobj_name);
		}

		float max_hits = psub->max_subsys_strength * (pobjp->ship_max_hull_strength / sip->max_hull_strength);

		float current_hits = ras->subsys_current_hits[i];

		sssp->percent = 100.0f * (max_hits - current_hits) / max_hits;
	}
}

void red_alert_store_subsys_status(red_alert_ship_status *ras, ship *shipp)
{
	ship_subsys *ss;
	int i;
	
	// Make sure there isn't any data from the previous ship.
	ras->subsys_current_hits.clear();
	ras->subsys_aggregate_current_hits.clear();
	
	if (shipp == NULL) {
		return;
	}

	ss = GET_FIRST(&shipp->subsys_list);
	while ( ss != END_OF_LIST( &shipp->subsys_list ) ) {
		ras->subsys_current_hits.push_back( ss->current_hits );

		ss = GET_NEXT( ss );
	}

	for (i = 0; i < SUBSYSTEM_MAX; i++)
		// Pyro3d - Fixes AP8 Based RA crashes
		ras->subsys_aggregate_current_hits.push_back(shipp->subsys_info[i].aggregate_current_hits);
}


/*
 * Record the current state of the players wingman & ships with the "red-alert-carry" flag
 * Wingmen without the red-alert-carry flag are only stored if they survive
 * dead wingmen must still be handled in red_alert_bash_ship_status
 */
void red_alert_store_ship_status()
{
	SCP_set<int> needed_wingnums;

	// store the mission filename for the red alert precursor mission
	Red_alert_precursor_mission = Game_current_mission_filename;

	// Pyro3d - Clear stored red alert data
	// Probably not the best solution, but it prevents an assertion in change_ship_type()
	red_alert_clear();


	// store status for all existing ships
	for (const auto so : list_range(&Ship_obj_list)) {
		auto ship_objp = &Objects[so->objnum];
		if (ship_objp->flags[Object::Object_Flags::Should_be_dead])
			continue;
		Assert(ship_objp->type == OBJ_SHIP);
		auto shipp = &Ships[ship_objp->instance];

		if ( shipp->flags[Ship::Ship_Flags::Dying] ) {
			continue;
		}

		if ( !(shipp->flags[Ship::Ship_Flags::From_player_wing]) && !(shipp->flags[Ship::Ship_Flags::Red_alert_store_status]) ) {
			continue;
		}

		red_alert_ship_status ras;
		ras.name = shipp->ship_name;
		ras.hull = Objects[shipp->objnum].hull_strength;
		ras.ship_class = shipp->ship_info_index;

		red_alert_store_weapons(&ras, &shipp->weapons);
		red_alert_store_subsys_status(&ras, shipp);

		Red_alert_ship_status.push_back( ras );
		// niffiwan: trying to track down red alert bug creating HUGE pilot files 
		Assert( (Red_alert_ship_status.size() <= MAX_SHIPS) );

		if (shipp->wingnum >= 0) {
			needed_wingnums.insert(shipp->wingnum);
		}
	}


	// store exited ships that did not die
	for (const auto &exited : Ships_exited) {
		if ( !(exited.flags[Ship::Exit_Flags::From_player_wing]) && !(exited.flags[Ship::Exit_Flags::Red_alert_carry]) ) {
			continue;
		}

		red_alert_ship_status ras;
		ras.name = exited.ship_name;
		ras.hull = float(exited.hull_strength);

		// if a ship has been destroyed or removed manually by the player, then mark it as such ...
		if ( exited.flags[Ship::Exit_Flags::Destroyed]) {
			ras.ship_class = RED_ALERT_DESTROYED_SHIP_CLASS;
		}
		else if (exited.flags[Ship::Exit_Flags::Player_deleted]) {
			ras.ship_class = RED_ALERT_PLAYER_DEL_SHIP_CLASS;
		}
		// ... otherwise we want to make sure and carry over the ship class
		else {
			Assert( exited.ship_class >= 0 );
			ras.ship_class = exited.ship_class;
		}

		red_alert_store_weapons(&ras, NULL);
		red_alert_store_subsys_status(&ras, NULL);

		Red_alert_ship_status.push_back( ras );
		// niffiwan: trying to track down red alert bug creating HUGE pilot files 
		Assert( (Red_alert_ship_status.size() <= MAX_SHIPS) );

		if (exited.wingnum >= 0) {
			needed_wingnums.insert(exited.wingnum);
		}
	}

	Assert( !Red_alert_ship_status.empty() );


	// store status for all wings related to a stored ship
	for (int wingnum : needed_wingnums) {
		auto wingp = &Wings[wingnum];

		red_alert_wing_status rws;
		rws.name = wingp->name;
		rws.latest_wave = wingp->current_wave;

		rws.wave_count = wingp->wave_count;
		rws.total_arrived_count = wingp->total_arrived_count;
		rws.total_departed = wingp->total_departed;
		rws.total_destroyed = wingp->total_destroyed;
		rws.total_vanished = wingp->total_vanished;

		Red_alert_wing_status.push_back( rws );
		// see comments from niffiwan above
		Assert( (Red_alert_wing_status.size() <= MAX_WINGS) );
	}
}

// Delete a ship in a red alert mission (since it must have died/departed in the previous mission)
void red_alert_delete_ship(int shipnum, int ship_state)
{
	ship* shipp = &Ships[shipnum];
	int cleanup_mode;	// See ship.h for cleanup mode defines. SHIP_VANISHED etc.

	switch (ship_state) {
	case RED_ALERT_DESTROYED_SHIP_CLASS:
		cleanup_mode = SHIP_DESTROYED_REDALERT;
		break;
	case RED_ALERT_PLAYER_DEL_SHIP_CLASS:
		cleanup_mode = SHIP_DEPARTED_REDALERT;
		break;
	default:
		UNREACHABLE("Red-alert: Unknown delete state '%i'\n", ship_state);
		cleanup_mode = SHIP_VANISHED;
		break;
	}

	Objects[shipp->objnum].flags.set(Object::Object_Flags::Should_be_dead);
	dock_undock_all(&Objects[shipp->objnum]);
	ship_cleanup(shipnum, cleanup_mode);
}

// just mark the parse object as never going to arrive
void red_alert_delete_ship(p_object *pobjp, int ship_state)
{
	if (ship_state == RED_ALERT_DESTROYED_SHIP_CLASS || ship_state == RED_ALERT_PLAYER_DEL_SHIP_CLASS)
	{
		pobjp->flags.set(Mission::Parse_Object_Flags::Red_alert_deleted);

        if (pobjp->wingnum < 0)
            pobjp->flags.set(Mission::Parse_Object_Flags::SF_Cannot_arrive);
	}
	else
		Error(LOCATION, "Red Alert: asked to delete ship (%s) with invalid ship state (%d)", pobjp->name, ship_state);
}

/*
 * Take the red alert status information, and adjust the red alert ships accordingly
 * "red alert ships" are wingmen and any ship with the red-alert-carry flag
 * Wingmen without red alert data still need to be handled / removed
 */
void red_alert_bash_ship_status()
{
	SCP_unordered_map<int, int> Wing_pobjects_marked_for_deletion;
	SCP_unordered_map<SCP_string, const red_alert_ship_status*, SCP_string_lcase_hash, SCP_string_lcase_equal_to> red_alert_ship_map;
	SCP_unordered_map<SCP_string, const red_alert_wing_status*, SCP_string_lcase_hash, SCP_string_lcase_equal_to> red_alert_wing_map;

	if ( !(Game_mode & GM_CAMPAIGN_MODE) ) {
		return;
	}

	if ( Red_alert_ship_status.empty() ) {
		return;
	}
	// if there is no wing status, that's ok; the code will just behave as if there was one wave in the previous mission's wing


	// create maps for data lookup
	for (const auto& ras : Red_alert_ship_status) {
		red_alert_ship_map[ras.name] = &ras;
	}
	for (const auto& rws : Red_alert_wing_status) {
		red_alert_wing_map[rws.name] = &rws;
	}


	// first, go through all wings and see if we need to skip any ships
	for (int wingnum = 0; wingnum < Num_wings; ++wingnum)
	{
		auto wingp = &Wings[wingnum];
		auto rws_iter = red_alert_wing_map.find(wingp->name);

		if (rws_iter != red_alert_wing_map.end())
		{
			auto rws = rws_iter->second;

			// if this wing has been stored, we need to skip based on the previous mission's latest wave, but *this mission's* wave count
			if (rws->latest_wave > 1)
				wingp->red_alert_skipped_ships += (rws->latest_wave - 1) * wingp->wave_count;
		}
	}


	// go through all ships in the game, and see if there is red alert status data for any
	auto so = GET_FIRST(&Ship_obj_list);
	for ( ; so != END_OF_LIST(&Ship_obj_list); )
	{
		object *ship_objp = &Objects[so->objnum];
		if (ship_objp->flags[Object::Object_Flags::Should_be_dead]) {
			so = GET_NEXT(so);
			continue;
		}

		Assert(ship_objp->type == OBJ_SHIP);
		ship *shipp = &Ships[ship_objp->instance];

		if ( !(shipp->flags[Ship::Ship_Flags::From_player_wing]) && !(shipp->flags[Ship::Ship_Flags::Red_alert_store_status]) ) {
			so = GET_NEXT(so);
			continue;
		}

		// see if this ship belongs to a wing that was stored
		if (shipp->wingnum >= 0)
		{
			auto wingp = &Wings[shipp->wingnum];
			auto rws_iter = red_alert_wing_map.find(wingp->name);

			if (rws_iter != red_alert_wing_map.end())
			{
				auto rws = rws_iter->second;

				// assign the wave number to the wing
				wingp->current_wave = rws->latest_wave;
				
				// make sure that num_waves can accommodate the new value of current_wave.
				// this would happen if the mission we are entering has too few waves.
				if (wingp->num_waves < wingp->current_wave)
				{
					mprintf(("Red alert warning: wing %s in the current mission has fewer waves than in the last mission.  Bashing the number of waves from %i to %i.\n", wingp->name, wingp->num_waves, wingp->current_wave));
					wingp->num_waves = wingp->current_wave;
				}

				if (rws->latest_wave > 1)
				{
					// find this ship's position in the wing
					int ship_entry_index = ship_registry_get_index(shipp->ship_name);
					Assertion(Ship_registry[ship_entry_index].has_p_objp(), "Ship %s must have a parse object!", shipp->ship_name);
					int pos_in_wing = Ship_registry[ship_entry_index].p_objp()->pos_in_wing;

					// give the ship its name from the latest wave
					// (this will make the ship match to the correct red-alert data)
					wing_bash_ship_name(shipp->ship_name, wingp->name, ((rws->latest_wave - 1) * wingp->wave_count) + 1 + pos_in_wing);
					// need to update the ship registry too
					strcpy_s(Ship_registry[ship_entry_index].name, shipp->ship_name);
					Ship_registry_map[shipp->ship_name] = ship_entry_index;
				}
			}
		}

		bool ship_data_restored = false;
		int ship_state = RED_ALERT_DESTROYED_SHIP_CLASS;

		auto ras_iter = red_alert_ship_map.find(shipp->ship_name);

		// red-alert data matches this ship!
		if (ras_iter != red_alert_ship_map.end())
		{
			auto ras = ras_iter->second;

			// we only want to restore ships which haven't been destroyed, or were removed by the player
			if ( (ras->ship_class != RED_ALERT_DESTROYED_SHIP_CLASS) && (ras->ship_class != RED_ALERT_PLAYER_DEL_SHIP_CLASS) )
			{
				// if necessary, restore correct ship class
				if ( ras->ship_class != shipp->ship_info_index )
				{
					if (ras->ship_class >= 0 && ras->ship_class < ship_info_size())
						change_ship_type(SHIP_INDEX(shipp), ras->ship_class);
					else
						mprintf(("Invalid ship class specified in red alert data for ship %s. Using mission default class.\n", shipp->ship_name));
				}

				float max_hull;
				if (shipp->special_hitpoints)
					max_hull = shipp->ship_max_hull_strength;
				else
					max_hull = Ship_info[shipp->ship_info_index].max_hull_strength;

				// restore hull (but not shields)
				if (ras->hull >= 0.0f && ras->hull <= max_hull)
					ship_objp->hull_strength = ras->hull;
				else
					mprintf(("Invalid hull strength in red alert data for ship %s. Using mission default hull.\n", shipp->ship_name));

				// restore weapons and subsys
				red_alert_bash_weapons(ras, &shipp->weapons);
				red_alert_bash_subsys_status(ras, shipp);

				ship_data_restored = true;
			}
			// must be destroyed or deleted
			else
			{
				ship_state = ras->ship_class;
			}
		}

		// remove ship if it was destroyed, or if there's no red-alert data for it
		if ( !ship_data_restored ) {
			// we need to be a little tricky here because deletion invalidates the ship_obj
			ship_obj *next_so = GET_NEXT(so);
			red_alert_delete_ship(ship_objp->instance, ship_state);
			so = next_so;
		} else {
			so = GET_NEXT(so);
		}
	}

	// NOTE: in retail, red alert data was not loaded for ships that arrived later in the mission
	if (!Red_alert_applies_to_delayed_ships)
		return;


	// go through all ships yet to arrive, and see if there is red alert status data for any
	for (auto &pobj : Parse_objects)
	{
		auto pobjp = &pobj;

		// objects that have already arrived would have been handled in the above loop
		if ( pobjp->created_object != NULL )
			continue;

		// same condition as in ship_obj loop
		if ( !(pobjp->flags[Mission::Parse_Object_Flags::SF_From_player_wing]) && !(pobjp->flags[Mission::Parse_Object_Flags::SF_Red_alert_store_status]) ) {
			continue;
		}

		char pobjp_name_to_check[NAME_LENGTH];
		strcpy_s(pobjp_name_to_check, pobjp->name);

		// see if this parse object belongs to a wing that was stored
		if (pobjp->wingnum >= 0)
		{
			auto wingp = &Wings[pobjp->wingnum];
			auto rws_iter = red_alert_wing_map.find(wingp->name);

			if (rws_iter != red_alert_wing_map.end())
			{
				auto rws = rws_iter->second;

				// assign the wave number to the wing
				// note that since the wing hasn't arrived yet, and the wave is incremented upon arrival, we need to subtract 1
				wingp->current_wave = rws->latest_wave - 1;

				if (rws->latest_wave > 1)
				{
					// use the name from the latest wave for the purposes of matching
					// (this will make the pobjp match to the correct red-alert data)
					wing_bash_ship_name(pobjp_name_to_check, wingp->name, ((rws->latest_wave - 1) * wingp->wave_count) + 1 + pobjp->pos_in_wing);
				}
			}
		}

		bool ship_data_restored = false;
		int ship_state = RED_ALERT_DESTROYED_SHIP_CLASS;

		auto ras_iter = red_alert_ship_map.find(pobjp_name_to_check);

		// red-alert data matches this ship!
		if (ras_iter != red_alert_ship_map.end())
		{
			auto ras = ras_iter->second;

			// we only want to restore ships which haven't been destroyed, or were removed by the player
			if ( (ras->ship_class != RED_ALERT_DESTROYED_SHIP_CLASS) && (ras->ship_class != RED_ALERT_PLAYER_DEL_SHIP_CLASS) )
			{
				// if necessary, restore correct ship class
				if ( ras->ship_class != pobjp->ship_class )
				{
					if (ras->ship_class >= 0 && ras->ship_class < ship_info_size())
						swap_parse_object(pobjp, ras->ship_class);
					else
						mprintf(("Invalid ship class specified in red alert data for ship %s. Using mission default class.\n", pobjp->name));
				}

				float max_hull;
				if (pobjp->special_hitpoints)
					max_hull = pobjp->ship_max_hull_strength;
				else
					max_hull = Ship_info[pobjp->ship_class].max_hull_strength;

				// restore hull (but not shields)
				if (ras->hull >= 0.0f && ras->hull <= max_hull)
					pobjp->initial_hull = (int) (ras->hull * 100.0f / max_hull);
				else
					mprintf(("Invalid hull strength in red alert data for ship %s. Using mission default hull.\n", pobjp->name));

				// restore weapons and subsys
				red_alert_bash_weapons(ras, pobjp);
				red_alert_bash_subsys_status(ras, pobjp);

				ship_data_restored = true;
			}
			// must be destroyed or deleted
			else
			{
				ship_state = ras->ship_class;
			}
		}

		// remove ship if it was destroyed, or if there's no red-alert data for it
		if ( !ship_data_restored )
		{
			red_alert_delete_ship(pobjp, ship_state);

			if (pobjp->wingnum >= 0)
				++Wing_pobjects_marked_for_deletion[pobjp->wingnum];
		}
	}

	// if all parse objects in a wing have been removed, clear the count for that wing so the next wave can arrive, if applicable
	for (const auto &ii: Wing_pobjects_marked_for_deletion)
	{
		wing *wingp = &Wings[ii.first];

		if (wingp->num_waves > 0 && wingp->wave_count == ii.second)
		{
			// skip over this wave
			wingp->red_alert_skipped_ships += wingp->wave_count;
			wingp->current_wave++;

			bool waves_spent = wingp->current_wave >= wingp->num_waves;
            if (waves_spent)
                wingp->flags.set(Ship::Wing_Flags::Gone);

			// look through all ships yet to arrive...
			for (auto pobjp : list_range(&Ship_arrival_list))
			{
				// ...and mark the ones in this wing
				if (pobjp->wingnum == ii.first)
				{
					// no waves left to arrive, so mark ships accordingly
                    if (waves_spent)
                        pobjp->flags.set(Mission::Parse_Object_Flags::SF_Cannot_arrive);
                    // we skipped a complete wave, so clear the flag so the next wave creates all ships
                    else
                        pobjp->flags.remove(Mission::Parse_Object_Flags::Red_alert_deleted);
				}
			}
		}
	}
}

// return !0 if this is a red alert mission, otherwise return 0
int red_alert_mission()
{
	return (The_mission.flags[Mission::Mission_Flags::Red_alert]);
}

// called from sexpression code to start a red alert mission
void red_alert_start_mission()
{
	// if we are not in campaign mode, go to debriefing
//	if ( !(Game_mode & GM_CAMPAIGN_MODE) ) {
//		gameseq_post_event( GS_EVENT_DEBRIEF );	// proceed to debriefing
//		return;
//	}

	// check player health here, but only in single-player
	// if we're dead (or about to die), don't start red alert mission.
	if ( (Game_mode & GM_MULTIPLAYER) ||
		 ((Player_obj->type == OBJ_SHIP) && !Player_ship->flags[Ship::Ship_Flags::Dying] && (Player_obj->hull_strength > 0)) )
	{
		if (Player_obj->type == OBJ_SHIP)
		{
			// make sure we don't die
			Player_ship->ship_guardian_threshold = SHIP_GUARDIAN_THRESHOLD_DEFAULT;
		}

		// do normal red alert stuff
		Red_alert_new_mission_timestamp = _timestamp(RED_ALERT_WARN_TIME);

		// throw down a sound here to make the warning seem ultra-important
		// gamesnd_play_iface(SND_USER_SELECT);
		snd_play(gamesnd_get_game_sound(GameSounds::DIRECTIVE_COMPLETE));
	}
}

// called from HUD code to see if we're red-alerting
int red_alert_in_progress()
{
	// it is specifically a question of whether the timestamp is running
	return Red_alert_new_mission_timestamp.isValid();
}

// called from the game loop to check if we should actually do the red-alert
void red_alert_maybe_move_to_next_mission()
{
	// if the timestamp is invalid, do nothing.
	if ( !Red_alert_new_mission_timestamp.isValid() )
		return;

	// return if the timestamp hasn't elapsed yet
	if ( !timestamp_elapsed(Red_alert_new_mission_timestamp) )
		return;

	// basic premise here is to stop the current mission, and then set the next mission in the campaign
	// which better be a red alert mission
	if ( Game_mode & GM_CAMPAIGN_MODE ) {
		red_alert_store_ship_status();
		mission_goal_fail_incomplete();

		if (Game_mode & GM_MULTIPLAYER) {
			// this should handle close-out and moving to next mission
			gameseq_post_event(GS_EVENT_DEBRIEF);
		} else {
			mission_campaign_store_goals_and_events_and_variables();
			scoring_level_close();
			mission_campaign_eval_next_mission();
			mission_campaign_mission_over();

			// CD CHECK
			gameseq_post_event(GS_EVENT_START_GAME);
		}
	} else {
		if (Game_mode & GM_MULTIPLAYER) {
			gameseq_post_event(GS_EVENT_DEBRIEF);
		} else {
			gameseq_post_event(GS_EVENT_END_GAME);
		}
	}
}

/*
 * red_alert_clear()
 *
 * clear all red alert "wingman" data
 * Allows data to be cleared from outside REDALERT_INTERNAL code
 */
void red_alert_clear()
{
	Red_alert_ship_status.clear();
	Red_alert_wing_status.clear();
}
