/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/




#include "debugconsole/console.h"
#include "freespace.h"
#include "gamesnd/gamesnd.h"
#include "globalincs/linklist.h"
#include "io/timer.h"
#include "mission/missionparse.h"
#include "nebula/neb.h"
#include "nebula/neblightning.h"
#include "network/multi.h"
#include "network/multimsgs.h"
#include "graphics/material.h"
#include "parse/parselo.h"
#include "render/3d.h"
#include "weapon/emp.h"

// ------------------------------------------------------------------------------------------------------
// NEBULA LIGHTNING DEFINES/VARS
//

// Lightning nodes
int Num_lnodes = 0;
l_node Nebl_nodes[MAX_LIGHTNING_NODES];

l_node Nebl_free_list;
l_node Nebl_used_list;

// nodes in a lightning bolt
#define LINK_LEFT	0
#define LINK_RIGHT	1
#define LINK_CHILD	2

// Lightning bolts
int Num_lbolts = 0;
l_bolt Nebl_bolts[MAX_LIGHTNING_BOLTS];

// Lightning bolt types
SCP_vector<bolt_type> Bolt_types;

// Lightning storm types
SCP_vector<storm_type> Storm_types;

// points on the basic cross section
vec3d Nebl_ring[3] = {	
	{ { { -1.0f, 0.0f, 0.0f } } },
	{ { { 1.0f, 0.70f, 0.0f } } },
	{ { { 1.0f, -0.70f, 0.0f } } }	
};

// pinched off cross-section
vec3d Nebl_ring_pinched[3] = {	
	{ { { -0.05f, 0.0f, 0.0f } } },
	{ { { 0.05f, 0.035f, 0.0f } } },
	{ { { 0.05f, -0.035f, 0.0f } } }	
};

// globals used for rendering and generating bolts
int Nebl_flash_count = 0;		// # of points rendered onscreen for this bolt
float Nebl_flash_x = 0.0f;		// avg x of the points rendered
float Nebl_flash_y = 0.0f;		// avg y of the points rendered
float Nebl_bang = 0.0;			// distance to the viewer object
float Nebl_alpha = 0.0f;		// alpha to use when rendering the bolt itself
float Nebl_glow_alpha = 0.0f;	// alpha to use when rendering the bolt glow
int Nebl_stamp = -1;			// random timestamp for making bolts
float Nebl_bolt_len;			// length of the current bolt being generated
bolt_type *Nebl_type;			// bolt type
matrix Nebl_bolt_dir;			// orientation matrix of the bolt being generated
vec3d Nebl_bolt_start;			// start point of the bolt being generated
vec3d Nebl_bolt_strike;			// strike point of the bolt being generated

// the type of active storm
storm_type *Storm = NULL;

// vars
DCF(b_scale, "Sets the scale factor for debug nebula bolts")
{
	dc_stuff_float(&Bolt_types[DEBUG_BOLT].b_scale);
}
DCF(b_rand, "Sets the randomness factor for debug nebula bolts")
{
	dc_stuff_float(&Bolt_types[DEBUG_BOLT].b_rand);
}
DCF(b_shrink, "Sets the shrink factor for debug nebula bolts")
{
	dc_stuff_float(&Bolt_types[DEBUG_BOLT].b_shrink);
}
DCF(b_poly_pct, "Sets b_poly_pct")
{
	dc_stuff_float(&Bolt_types[DEBUG_BOLT].b_poly_pct);
}
DCF(b_add, "Sets b_add")
{
	dc_stuff_float(&Bolt_types[DEBUG_BOLT].b_add);
}
DCF(b_strikes, "Sets num_strikes")
{
	dc_stuff_int(&Bolt_types[DEBUG_BOLT].num_strikes);
}
DCF(b_noise, "Sets noise factor")
{
	dc_stuff_float(&Bolt_types[DEBUG_BOLT].noise);
}
DCF(b_bright, "Sets brightness factor")
{
	dc_stuff_float(&Bolt_types[DEBUG_BOLT].b_bright);
}
DCF(b_lifetime, "Sets lifetime duration")
{
	dc_stuff_int(&Bolt_types[DEBUG_BOLT].lifetime);
}
DCF(b_list, "Displays status of debug lightning commands")
{
	dc_printf("Debug lightning bolt settings :\n");

	dc_printf("b_scale : %f\n", Bolt_types[DEBUG_BOLT].b_scale);
	dc_printf("b_rand : %f\n", Bolt_types[DEBUG_BOLT].b_rand);
	dc_printf("b_shrink : %f\n", Bolt_types[DEBUG_BOLT].b_shrink);
	dc_printf("b_poly_pct : %f\n", Bolt_types[DEBUG_BOLT].b_poly_pct);
	dc_printf("b_add : %f\n", Bolt_types[DEBUG_BOLT].b_add);
	dc_printf("b_strikes : %d\n", Bolt_types[DEBUG_BOLT].num_strikes);
	dc_printf("b_noise : %f\n", Bolt_types[DEBUG_BOLT].noise);
	dc_printf("b_bright : %f\n", Bolt_types[DEBUG_BOLT].b_bright);
	dc_printf("b_lifetime : %d\n", Bolt_types[DEBUG_BOLT].lifetime);
}

// nebula lightning intensity (0.0 to 1.0)
float Nebl_intensity = 0.6667f;

DCF(lightning_intensity, "Sets lightning intensity between 0.0 and 1.0 (Default is 0.6667)")
{
	float val;
	dc_stuff_float(&val);

	CLAMP(val, 0.0f, 1.0f);

	Nebl_intensity = 1.0f - val;
}


// ------------------------------------------------------------------------------------------------------
// NEBULA LIGHTNING FUNCTIONS
//

static bolt_type* get_bolt_type_pointer(const char* bolt)
{
	for (int i = 0; i < (int)Bolt_types.size(); i++) {
		if (!stricmp(bolt, Bolt_types[i].name)) {
			return &Bolt_types[i];
		}
	}

	// Didn't find anything.
	return nullptr;
}

static storm_type* get_storm_type_pointer(const char* storm)
{
	for (int i = 0; i < (int)Storm_types.size(); i++) {
		if (!stricmp(storm, Storm_types[i].name)) {
			return &Storm_types[i];
		}
	}

	// Didn't find anything.
	return nullptr;
}

int get_bolt_type_by_name(const char* bolt)
{
	for (int i = 0; i < (int)Bolt_types.size(); i++) {
		if (!stricmp(bolt, Bolt_types[i].name)) {
			return i;
		}
	}

	// Didn't find anything.
	return -1;
}

// initialize nebula lightning at game startup
void parse_lightning_table(const char* filename)
{
	char name[MAX_FILENAME_LEN];

	try
	{
		// parse the lightning table
		read_file_text(filename, CF_TYPE_TABLES);
		reset_parse();

		// parse the individual lightning bolt types
		required_string("#Bolts begin");
		while (!optional_string("#Bolts end")){

			bool create_if_not_found = true;
			bolt_type bolt_t;
			bolt_type* bolt_p;

			// bolt title
			required_string("$Bolt:");
			stuff_string(bolt_t.name, F_NAME, NAME_LENGTH);

			if (optional_string("+nocreate")) {
				if (!Parsing_modular_table) {
					Warning(LOCATION, "+nocreate flag used for lightning bolt in non-modular table\n");
				}
				create_if_not_found = false;
			}

			// Does this bolt exist already?
			// If so, load this new info into it
			bolt_p = get_bolt_type_pointer(bolt_t.name);
			if (bolt_p != nullptr) {
				if (!Parsing_modular_table) {
					error_display(1,
						"Error:  Lightning Bolt %s already exists.  All bolt names must be unique.",
						bolt_t.name);
				}
			} else {
				// Don't create bolt if it has +nocreate and is in a modular table.
				if (!create_if_not_found && Parsing_modular_table) {
					if (!skip_to_start_of_string_either("$Bolt:", "#Bolts end")) {
						error_display(1, "Missing [#Bolts end] or [$Bolt] after lightning bolt %s", bolt_t.name);
					}
					continue;
				}

				Bolt_types.push_back(bolt_t);
				bolt_p = &Bolt_types[Bolt_types.size() - 1];
			}

			// b_scale
			if (optional_string("+b_scale:")) {
				stuff_float(&bolt_p->b_scale);
			}

			// b_shrink
			if (optional_string("+b_shrink:")) {
				stuff_float(&bolt_p->b_shrink);
			}

			// b_poly_pct
			if (optional_string("+b_poly_pct:")) {
				stuff_float(&bolt_p->b_poly_pct);
			}

			// child rand, omg not the chilren!
			if (optional_string("+b_rand:")) {
				stuff_float(&bolt_p->b_rand);
			}

			// z add
			if (optional_string("+b_add:")) {
				stuff_float(&bolt_p->b_add);
			}

			// # strikes
			if (optional_string("+b_strikes:")) {
				stuff_int(&bolt_p->num_strikes);
			}

			// lifetime
			if (optional_string("+b_lifetime:")) {
				stuff_int(&bolt_p->lifetime);
			}

			// noise
			if (optional_string("+b_noise:")) {
				stuff_float(&bolt_p->noise);
			}

			// emp effect
			if (optional_string("+b_emp:")) {
				stuff_float(&bolt_p->emp_intensity);
				stuff_float(&bolt_p->emp_time);
			}

			// texture
			if (optional_string("+b_texture:")) {
				stuff_string(name, F_NAME, sizeof(name));
				if (!Fred_running && stricmp(name, "none") && stricmp(name, "<none>")) {
					bolt_p->texture = bm_load(name);
					if (bolt_p->texture < 0)
						error_display(0, "Unable to load texture %s for bolt %s.", name, bolt_p->name);
				}
			}

			// glow
			if (optional_string("+b_glow:")) {
				stuff_string(name, F_NAME, sizeof(name));
				if (!Fred_running && stricmp(name, "none") && stricmp(name, "<none>")) {
					bolt_p->glow = bm_load(name);
					if (bolt_p->glow < 0)
						error_display(0, "Unable to load glow %s for bolt %s.", name, bolt_p->name);
				}
			}

			// brightness
			if (optional_string("+b_bright:")) {
				stuff_float(&bolt_p->b_bright);
			}

		}

		// parse lightning storm types
		required_string("#Storms begin");
		while (!optional_string("#Storms end")){

			bool create_if_not_found = true;
			storm_type storm_t;
			storm_type* storm_p;

			// bolt title
			required_string("$Storm:");
			stuff_string(storm_t.name, F_NAME, NAME_LENGTH);

			if (optional_string("+nocreate")) {
				if (!Parsing_modular_table) {
					Warning(LOCATION, "+nocreate flag used for lightning storm in non-modular table\n");
				}
				create_if_not_found = false;
			}

			// Does this storm exist already?
			// If so, load this new info into it
			storm_p = get_storm_type_pointer(storm_t.name);
			if (storm_p != nullptr) {
				if (!Parsing_modular_table) {
					error_display(1,
						"Error:  Lightning Storm %s already exists.  All bolt names must be unique.",
						storm_t.name);
				}
			} else {
				// Don't create storm if it has +nocreate and is in a modular table.
				if (!create_if_not_found && Parsing_modular_table) {
					if (!skip_to_start_of_string_either("$Storm:", "#Storms end")) {
						error_display(1, "Missing [#Storms end] or [$Storm] after lightning storm %s", storm_t.name);
					}
					continue;
				}

				Storm_types.push_back(storm_t);
				storm_p = &Storm_types[Storm_types.size() - 1];
			}

			if (optional_string("+Clear bolts")) {
				if (Parsing_modular_table) {
					mprintf(("Got argument to clear the bolts list for storm %s", storm_p->name));
					for (int i = 0; i < MAX_BOLT_TYPES_PER_STORM; i++) {
						storm_p->bolt_types[i] = -1;
					}
					storm_p->num_bolt_types = 0;
				} else {
					error_display(0,
						"Got argument to clear bolts list for storm %s in non-modular table. Ignoring!",
						storm_p->name);
				}
			}

			// bolt types
			while (optional_string("+bolt:")){
				if (storm_p->num_bolt_types < MAX_BOLT_TYPES_PER_STORM) {
					stuff_string(storm_p->bolt_types_n[storm_p->num_bolt_types], F_NAME, NAME_LENGTH);
					storm_p->num_bolt_types++;
				} else {
					error_display(0, "Storm %s already has %i bolts. Ignoring bolt %s!", storm_p->name, MAX_BOLT_TYPES_PER_STORM, name);
				}
			}

			//Check that we have at least one bolt
			if (storm_p->num_bolt_types < 1)
				error_display(1, "Storm %s has no bolts defined!", storm_p->name);

			// flavor
			if (optional_string("+flavor:")) {
				stuff_float(&storm_p->flavor.xyz.x);
				stuff_float(&storm_p->flavor.xyz.y);
				stuff_float(&storm_p->flavor.xyz.z);
			}

			// frequencies
			if (optional_string("+random_freq:")) {
				stuff_int(&storm_p->min);
				stuff_int(&storm_p->max);
			}

			// counts
			if (optional_string("+random_count:")) {
				stuff_int(&storm_p->min_count);
				stuff_int(&storm_p->max_count);
			}

		}
	}
	catch (const parse::ParseException& e)
	{
		mprintf(("TABLES: Unable to parse '%s'!  Error message = %s.\n", "lightning.tbl", e.what()));
		return;
	}
}

// the storm effect chooses a bolt by index using a rand number 
// Generator. Means we cant do any Bolt error checking at the time 
// Of parsing, And we would need to Update the array
// Of bolts, so Keys are filled. This Grouped "for" method verifies
// Bolts by name. Each bolt in all the Storms saves the index in the
// bolts array var So it meets that requirement - Mjn
static void verify_storm_bolts()
{
	for (int i = 0; i < (int)Storm_types.size(); i++) {
		//Count the number of errored bolts so we can adjust the array to leave no blank slots-Mjn
		int error_count = 0;
		for (int j = 0; j < Storm_types[i].num_bolt_types; j++) {
			int bolt_idx = nebl_get_bolt_index(Storm_types[i].bolt_types_n[j]);
			if (bolt_idx < 0) {
				Warning(LOCATION,
					"Unknown bolt %s defined in storm %s. Skipping!",
					Storm_types[i].bolt_types_n[j],
					Storm_types[i].name);
				error_count++;
			} else {
				Storm_types[i].bolt_types[j - error_count] = bolt_idx;
			}
		}
		//Correct the bolt count
		Storm_types[i].num_bolt_types = (Storm_types[i].num_bolt_types - error_count);

		//Semi duplicate error message, but relevant in case all bolts were removed
		if (Storm_types[i].num_bolt_types == 0)
			Error(LOCATION, "Storm %s has no bolts defined!", Storm_types[i].name);
	}
}

void nebl_init()
{

	// first parse the default table
	parse_lightning_table("lightning.tbl");

	// parse any modular tables
	parse_modular_table("*-ltng.tbm", parse_lightning_table);

	//Verify the bolts after all parsing is done to make sure that load order isn't a factor
	verify_storm_bolts();
}

// initialize lightning before entering a level
void nebl_level_init()
{
	size_t idx;	

	// zero all lightning bolts
	for(idx=0; idx<MAX_LIGHTNING_BOLTS; idx++){
		Nebl_bolts[idx].head = NULL;
		Nebl_bolts[idx].bolt_life = -1;
		Nebl_bolts[idx].used = 0;
	}	
	
	// initialize node list
	Num_lnodes = 0;
	list_init( &Nebl_free_list );
	list_init( &Nebl_used_list );

	// Link all object slots into the free list
	for (idx=0; idx<MAX_LIGHTNING_NODES; idx++)	{
		list_append(&Nebl_free_list, &Nebl_nodes[idx] );
	}

	// zero the random timestamp
	Nebl_stamp = -1;		

	// null the storm. let mission parsing set it up
	Storm = NULL;
}

// set the storm (call from mission parse)
void nebl_set_storm(const char *name)
{
	// sanity
	Storm = NULL;
	
	int index = nebl_get_storm_index(name);
	
	if(index == -1)
		return;
	
	Storm = &Storm_types[index];
}

// render all lightning bolts
void nebl_render_all()
{
	GR_DEBUG_SCOPE("Nebula render all");

	l_bolt *b;
	bolt_type *bi;

	// traverse the list
	for(size_t idx=0; idx<MAX_LIGHTNING_BOLTS; idx++){
		b = &Nebl_bolts[idx];		

		// if this is being used
		if(b->used){
			Assert(b->head != NULL);

			// bogus bolt
			if(b->head == NULL){
				b->used = 0;
				continue;
			}
			if( b->type >= Bolt_types.size() ){
				b->used = 0;
				continue;
			}
			bi = &Bolt_types[b->type];

			// if this guy is still on a delay
			if(b->delay != -1){
				if(timestamp_elapsed(b->delay)){
					b->delay = -1;
				} else {
					continue;
				}
			}

			// if the timestamp on this guy has expired
			if((b->bolt_life < 0) || timestamp_elapsed(b->bolt_life)){
				// if this is a multiple strike bolt, jitter it and reset
				if(b->strikes_left-1 > 0){
					b->bolt_life = timestamp(bi->lifetime / bi->num_strikes);
					b->first_frame = 1;
					b->strikes_left--;
					nebl_jitter(b);

					// by continuing here we skip rendering for one frame, which makes it look more like real lightning
					continue;
				}
				// otherwise he's completely done, so release him
				else {
					// maybe free up node data
					if(b->head != NULL){
						nebl_release(b->head);
						b->head = NULL;

						Num_lbolts--;

						nprintf(("lightning", "Released bolt. %d used nodes!\n", Num_lnodes));
					}

					b->used = 0;
				}
			}

			// pick some cool alpha values
			Nebl_alpha = frand();
			Nebl_glow_alpha = frand();

			// otherwise render him
			Nebl_flash_count = 0;
			Nebl_flash_x = 0.0f;
			Nebl_flash_y = 0.0f;
			Nebl_bang = 10000000.0f;
			nebl_render(bi, b->head, b->width);

			// if this is the first frame he has been rendered, determine if we need to make a flash and sound effect
			if(b->first_frame){
				float flash = 0.0f;				

				b->first_frame = 0;

				// if we rendered any points
				if(Nebl_flash_count){
					Nebl_flash_x /= (float)Nebl_flash_count;
					Nebl_flash_y /= (float)Nebl_flash_count;

					// quick distance from the center of the screen			
					float x = Nebl_flash_x - (gr_screen.max_w / 2.0f);
					float y = Nebl_flash_y - (gr_screen.max_h / 2.0f);
					float dist = fl_sqrt((x * x) + (y * y));		
					if(dist / (gr_screen.max_w / 2.0f) < 1.0f){
						flash = 1.0f - (dist / (gr_screen.max_w / 2.0f));										

						// scale the flash by bolt type
						flash *= bi->b_bright;

						game_flash(flash, flash, flash);										
					}					

					// do some special stuff on the very first strike of the bolt
					if (b->strikes_left == bi->num_strikes) {					
						// play a sound
						if (b->play_sound) {
							float bang;
							if (Nebl_bang < 40.0f) {
								bang = 1.0f;
							} else if (Nebl_bang > 400.0f) {
								bang = 0.0f;
							} else {
								bang = 1.0f - (Nebl_bang / 400.0f);
							}
							if (frand_range(0.0f, 1.0f) < 0.5f) {
								snd_play(gamesnd_get_game_sound(GameSounds::LIGHTNING_2),
									0.0f,
									bang,
									SND_PRIORITY_DOUBLE_INSTANCE);
							} else {
								snd_play(gamesnd_get_game_sound(GameSounds::LIGHTNING_1),
									0.0f,
									bang,
									SND_PRIORITY_DOUBLE_INSTANCE);
							}
						}

						// apply em pulse
						if(bi->emp_intensity > 0.0f){
							emp_apply(&b->midpoint, 0.0f, vm_vec_dist(&b->start, &b->strike), bi->emp_intensity, bi->emp_time);
						}
					}
				}				
			}
		}
	}	
}

// process lightning (randomly generate bolts, etc, etc);
void nebl_process()
{		
	uint num_bolts, idx;

	// non-nebula mission
	if(!(The_mission.flags[Mission::Mission_Flags::Fullneb])){
		return;
	}		
	
	// non servers in multiplayer don't do this
	if((Game_mode & GM_MULTIPLAYER) && !MULTIPLAYER_MASTER){
		return;
	}

	// standalones shouldn't be doing this either
	if (Is_standalone) {
		return;
	}

	// if there's no chosen storm
	if(Storm == NULL){
		return;
	}

	// don't process lightning bolts unless we're a few seconds in
	if(f2fl(Missiontime) < 3.0f){
		return;
	}
		
	// random stamp
	if(Nebl_stamp == -1){
		Nebl_stamp = timestamp(Random::next(Storm->min, Storm->max));
		return;
	}	

	// maybe make a bolt
	if(timestamp_elapsed(Nebl_stamp)){
		// determine how many bolts to spew
		num_bolts = Random::next(Storm->min_count, Storm->max_count);
		for(idx=0; idx<num_bolts; idx++){
			vec3d start;
			do {
				vm_vec_random_in_sphere(&start, &Eye_position, 800.0f, false);
			} while (vm_vec_dist(&start, &Eye_position) > 200.0f);

			vec3d strike;
			do {
				vm_vec_random_in_sphere(&strike, &Eye_position, 800.0f, false);
			} while (vm_vec_dist(&strike, &Eye_position) > 200.0f && vm_vec_dist(&start, &strike) > 200.0f);

			// add some flavor to the bolt. mmmmmmmm, lightning
			if(!IS_VEC_NULL_SQ_SAFE(&Storm->flavor)){
				// start with your basic hot sauce. measure how much you have			
				vec3d your_basic_hot_sauce;
				vm_vec_sub(&your_basic_hot_sauce, &strike, &start);
				float how_much_hot_sauce = vm_vec_normalize(&your_basic_hot_sauce);

				// now figure out how much of that good wing sauce to add
				vec3d wing_sauce = Storm->flavor;
				if(frand_range(0.0, 1.0f) < 0.5f){
					vm_vec_scale(&wing_sauce, -1.0f);
				}
				float how_much_of_that_good_wing_sauce_to_add = vm_vec_normalize(&wing_sauce);

				// mix the two together, taking care not to add too much
				vec3d the_mixture;
				if(how_much_of_that_good_wing_sauce_to_add > 1000.0f){
					how_much_of_that_good_wing_sauce_to_add = 1000.0f;
				}
				vm_vec_interp_constant(&the_mixture, &your_basic_hot_sauce, &wing_sauce, how_much_of_that_good_wing_sauce_to_add / 1000.0f);

				// take the final sauce and store it in the proper container
				vm_vec_scale(&the_mixture, how_much_hot_sauce);

				// make sure to put it on everything! whee!			
				vm_vec_add(&strike, &start, &the_mixture);
			}

			int type = Random::next(Storm->num_bolt_types);
			nebl_bolt(Storm->bolt_types[type], &start, &strike);
		}

		// reset the timestamp
		Nebl_stamp = timestamp(Random::next(Storm->min, Storm->max));
	}	
}

// create a lightning bolt
bool nebl_bolt(int type, vec3d *start, vec3d *strike, bool play_sound)
{
	vec3d dir;
	l_bolt *bolt;
	l_node *tail;
	int idx;
	bool found;		
	bolt_type *bi;
	float bolt_len;

	// find a free bolt
	found = 0;
	for(idx=0; idx<MAX_LIGHTNING_BOLTS; idx++){
		if(!Nebl_bolts[idx].used){
			found = 1;
			break;
		}
	}
	if(!found){
		return false;
	}

	if( type >= (int)Bolt_types.size() ){
		return false;
	}
	bi = &Bolt_types[type];	

	// get a pointer to the bolt
	bolt = &Nebl_bolts[idx];	

	// setup bolt into
	bolt->start = *start;
	bolt->strike = *strike;
	bolt->strikes_left = bi->num_strikes;
	bolt->delay = -1;
	bolt->type = (char)type;
	bolt->first_frame = 1;
	bolt->bolt_life = timestamp(bi->lifetime / bi->num_strikes);	
	bolt->play_sound = play_sound;

	Nebl_bolt_start = *start;
	Nebl_bolt_strike = *strike;

	// setup fire delay
	if(bolt->delay != -1){
		bolt->delay = timestamp(bolt->delay);
	}

	// setup the rest of the important bolt data
	if(vm_vec_same(&Nebl_bolt_start, &Nebl_bolt_strike)){
		Nebl_bolt_strike.xyz.z += 150.0f;
	}
	Nebl_bolt_len = vm_vec_dist(&Nebl_bolt_start, &Nebl_bolt_strike);	
	vm_vec_sub(&dir, &Nebl_bolt_strike, &Nebl_bolt_start);

	// setup midpoint
	vm_vec_scale_add(&bolt->midpoint, &Nebl_bolt_start, &dir, 0.5f);

	bolt_len = vm_vec_normalize(&dir);
	vm_vector_2_matrix_norm(&Nebl_bolt_dir, &dir, nullptr, nullptr);

	// global type for generating the bolt
	Nebl_type = bi;

	// try and make the bolt
	if(!nebl_gen(&Nebl_bolt_start, &Nebl_bolt_strike, 0, 4, 0, &bolt->head, &tail)){
		if(bolt->head != NULL){
			nebl_release(bolt->head);
		}

		return false;
	}

	Num_lbolts++;	
	
	// setup the rest of the data	
	bolt->used = 1;	
	bolt->width = bi->b_poly_pct * bolt_len;

	// if i'm a multiplayer master, send a bolt packet
	if(MULTIPLAYER_MASTER){
		send_lightning_packet(type, start, strike);
	}

	return true;
}

// "new" a lightning node
l_node *nebl_new()
{
	l_node *lp;

	// if we're out of nodes
	if(Num_lnodes >= MAX_LIGHTNING_NODES){
		nprintf(("lightning", "Out of lightning nodes!\n"));
		return NULL;
	}

	// get a new node off the freelist
	lp = GET_FIRST(&Nebl_free_list);
	Assert( lp != &Nebl_free_list );		// shouldn't have the dummy element

	// remove trailp from the free list
	list_remove( &Nebl_free_list, lp );
	
	// insert trailp onto the end of used list
	list_append( &Nebl_used_list, lp );

	// increment counter
	Num_lnodes++;

	lp->links[0] = NULL;
	lp->links[1] = NULL;
	lp->links[2] = NULL;	

	// return the pointer
	return lp;
}

// "delete" a lightning node
void nebl_delete(l_node *lp)
{
	// remove objp from the used list
	list_remove( &Nebl_used_list, lp );

	// add objp to the end of the free
	list_append( &Nebl_free_list, lp );

	// decrement counter
	Num_lnodes--;
}

// free a lightning bolt
void nebl_release(l_node *whee)
{
	// if we're invalid
	if(whee == NULL){
		return;
	}

	// release all of our children
	if(whee->links[LINK_RIGHT] != NULL){
		nebl_release(whee->links[LINK_RIGHT]);
	}	
	if(whee->links[LINK_CHILD] != NULL){
		nebl_release(whee->links[LINK_CHILD]);
	}	

	// delete this node
	nebl_delete(whee);
}

int nebl_gen(vec3d *left, vec3d *right, float depth, float max_depth, int child, l_node **l_left, l_node **l_right)
{
	l_node *child_node = NULL;
	float d = vm_vec_dist_quick( left, right );		

	// if we've reached the critical point
	if ( d < 0.30f || (depth > max_depth) ){
		// generate ne items
		l_node *new_left = nebl_new();
		if(new_left == NULL){
			return 0;
		}		
		new_left->links[0] = NULL; new_left->links[1] = NULL; new_left->links[2] = NULL;
		new_left->pos = vmd_zero_vector;
		l_node *new_right = nebl_new();
		if(new_right == NULL){
			nebl_delete(new_left);			
			return 0;
		}		
		new_right->links[0] = NULL; new_right->links[1] = NULL; new_right->links[2] = NULL;
		new_right->pos = vmd_zero_vector;

		// left side
		new_left->pos = *left;		
		new_left->links[LINK_RIGHT] = new_right;		
		*l_left = new_left;
		
		// right side
		new_right->pos = *right;
		new_right->links[LINK_LEFT] = new_left;
		*l_right = new_right;

		// done
		return 1;
	}  

	// divide in half
	vec3d tmp;
	vm_vec_avg( &tmp, left, right );

	// sometimes generate children
	if(!child && (frand() <= Nebl_type->b_rand)){
		// get a point on the plane of the strike
		vec3d tmp2;
		vm_vec_random_in_circle(&tmp2, &Nebl_bolt_strike, &Nebl_bolt_dir, Nebl_bolt_len * Nebl_type->b_scale, false);

		// maybe move away from the plane
		vec3d dir;
		vm_vec_sub(&dir, &tmp2, &tmp);		
		vm_vec_scale_add(&tmp2, &tmp, &dir, Nebl_type->b_shrink);

		// child
		l_node *argh;		
		if(!nebl_gen(&tmp, &tmp2, 0, 2, 1, &child_node, &argh)){
			if(child_node != NULL){
				nebl_release(child_node);
			}
			return 0;
		}
	}
	
	float scaler = 0.30f;
	tmp.xyz.x += (frand()-0.5f)*d*scaler;
	tmp.xyz.y += (frand()-0.5f)*d*scaler;
	tmp.xyz.z += (frand()-0.5f)*d*scaler;

	// generate left half
	l_node *ll = NULL;
	l_node *lr = NULL;
	if(!nebl_gen( left, &tmp, depth+1, max_depth, child, &ll, &lr )){
		if(child_node != NULL){
			nebl_release(child_node);
		}
		if(ll != NULL){
			nebl_release(ll);
		}
		return 0;
	}

	// generate right half
	l_node *rl = NULL;
	l_node *rr = NULL;
	if(!nebl_gen( &tmp, right, depth+1, max_depth, child, &rl, &rr )){
		if(child_node != NULL){
			nebl_release(child_node);
		}
		if(ll != NULL){
			nebl_release(ll);
		}
		if(rl != NULL){
			nebl_release(rl);
		}
		return 0;
	}
	
	// splice the two together
	lr->links[LINK_RIGHT] = rl->links[LINK_RIGHT];
	lr->links[LINK_RIGHT]->links[LINK_LEFT] = lr;
	nebl_delete(rl);

	// if we generated a child, stick him on
	if(child_node != NULL){
		lr->links[LINK_CHILD] = child_node;
	}

	// return these
	*l_left = ll;
	*l_right = rr;

	return 1;
}


// output top and bottom vectors
// fvec == forward vector (eye viewpoint basically. in world coords)
// pos == world coordinate of the point we're calculating "around"
// w == width of the diff between top and bottom around pos
void nebl_calc_facing_pts_smart( vec3d *top, vec3d *bot, vec3d *fvec, vec3d *pos, float w, float z_add )
{
	vec3d uvec, rvec;
	vec3d temp;	

	temp = *pos;

	vm_vec_sub( &rvec, &Eye_position, &temp );
	vm_vec_normalize( &rvec );	

	vm_vec_cross(&uvec,fvec,&rvec);
	vm_vec_normalize(&uvec);

	vm_vec_scale_add( top, &temp, &uvec, w/2.0f );
	vm_vec_scale_add( bot, &temp, &uvec, -w/2.0f );	
	
	vm_vec_scale_add2( top, &rvec, z_add );
	vm_vec_scale_add2( bot, &rvec, z_add );
}

// render a section of the bolt
void nebl_render_section(bolt_type *bi, l_section *a, l_section *b)
{		
	vertex v[4];
	vertex *verts[4] = {&v[0], &v[1], &v[2], &v[3]};

	material material_params;

	material_set_unlit_emissive(&material_params, bi->texture, Nebl_alpha, 2.0f);
	
	// draw some stuff
	for(size_t idx=0; idx<2; idx++){		
		v[0] = a->vex[idx];		
		v[0].texture_position.u = 0.0f; v[0].texture_position.v = 0.0f;

		v[1] = a->vex[idx+1];		
		v[1].texture_position.u = 1.0f; v[1].texture_position.v = 0.0f;

		v[2] = b->vex[idx+1];		
		v[2].texture_position.u = 1.0f; v[2].texture_position.v = 1.0f;

		v[3] = b->vex[idx];		
		v[3].texture_position.u = 0.0f; v[3].texture_position.v = 1.0f;

		// draw
		g3_render_primitives_textured(&material_params, v, 4, PRIM_TYPE_TRIFAN, false);
	}

	// draw
	v[0] = a->vex[2];		
	v[0].texture_position.u = 0.0f; v[0].texture_position.v = 0.0f;

	v[1] = a->vex[0];		
	v[1].texture_position.u = 1.0f; v[1].texture_position.v = 0.0f;

	v[2] = b->vex[0];		
	v[2].texture_position.u = 1.0f; v[2].texture_position.v = 1.0f;

	v[3] = b->vex[2];		
	v[3].texture_position.u = 0.0f; v[3].texture_position.v = 1.0f;

	g3_render_primitives_textured(&material_params, v, 4, PRIM_TYPE_TRIFAN, false);

	// draw the glow beam	
	verts[0] = &a->glow_vex[0];
	verts[0]->texture_position.v = 0.0f; verts[0]->texture_position.u = 0.0f;

	verts[1] = &a->glow_vex[1];
	verts[1]->texture_position.v = 1.0f; verts[1]->texture_position.u = 0.0f;

	verts[2] = &b->glow_vex[1];
	verts[2]->texture_position.v = 1.0f; verts[2]->texture_position.u = 1.0f;

	verts[3] = &b->glow_vex[0];
	verts[3]->texture_position.v = 0.0f; verts[3]->texture_position.u = 1.0f;

	g3_render_primitives_textured(&material_params, v, 4, PRIM_TYPE_TRIFAN, false);
}

// generate a section
void nebl_generate_section(bolt_type *bi, float width, l_node *a, l_node *b, l_section *c, l_section *cap, int pinch_a, int pinch_b)
{
	vec3d dir_normal;
	matrix m;
	size_t idx;	
	vec3d temp, pt;
	vec3d glow_a, glow_b;
	vertex tempv;

	// direction matrix
	vm_vec_normalized_dir(&dir_normal, &a->pos, &b->pos);
	vm_vector_2_matrix_norm(&m, &dir_normal, nullptr, nullptr);

	// distance to player
	float bang_dist = vm_vec_dist_quick(&Eye_position, &a->pos);
	if(bang_dist < Nebl_bang){
		Nebl_bang = bang_dist;
	}

	// rotate the basic section into world	
	for(idx=0; idx<3; idx++){
		memset(&tempv, 0, sizeof(tempv));
        
		// rotate to world		
		if(pinch_a){			
			vm_vec_rotate(&pt, &Nebl_ring_pinched[idx], &m);
		} else {
			vm_vec_copy_scale(&temp, &Nebl_ring[idx], width);
			vm_vec_rotate(&pt, &temp, &m);
		}
		vm_vec_add2(&pt, &a->pos);
			
		g3_transfer_vertex(&c->vex[idx], &pt);
		g3_rotate_vertex(&tempv, &pt);
		g3_project_vertex(&tempv);

		// if first frame, keep track of the average screen pos
		if (tempv.codes == 0) {
			Nebl_flash_x += tempv.screen.xyw.x;
			Nebl_flash_y += tempv.screen.xyw.y;
			Nebl_flash_count++;
		}
	}
	// calculate the glow points		
	nebl_calc_facing_pts_smart(&glow_a, &glow_b, &dir_normal, &a->pos, pinch_a ? 0.5f : width * 6.0f, Nebl_type->b_add);
	g3_transfer_vertex(&c->glow_vex[0], &glow_a);
	g3_transfer_vertex(&c->glow_vex[1], &glow_b);

	// maybe do a cap
	if(cap != NULL){		
		// rotate the basic section into world
		for(idx=0; idx<3; idx++){
			// rotate to world		
			if(pinch_b){
				vm_vec_rotate(&pt, &Nebl_ring_pinched[idx], &m);
			} else {
				vm_vec_copy_scale(&temp, &Nebl_ring[idx], width);
				vm_vec_rotate(&pt, &temp, &m);		
			}
			vm_vec_add2(&pt, &b->pos);
			
			g3_transfer_vertex(&cap->vex[idx], &pt);
			g3_rotate_vertex(&tempv, &pt);
			g3_project_vertex(&tempv);

			// if first frame, keep track of the average screen pos
			if (tempv.codes == 0) {
				Nebl_flash_x += tempv.screen.xyw.x;
				Nebl_flash_y += tempv.screen.xyw.y;
				Nebl_flash_count++;
			}
		}
		
		// calculate the glow points		
		nebl_calc_facing_pts_smart(&glow_a, &glow_b, &dir_normal, &b->pos, pinch_b ? 0.5f : width * 6.0f, bi->b_add);
		g3_transfer_vertex(&cap->glow_vex[0], &glow_a);
		g3_transfer_vertex(&cap->glow_vex[1], &glow_b);
	}
}

// render the bolt
void nebl_render(bolt_type *bi, l_node *whee, float width, l_section *prev)
{		
	l_section start;
	l_section end;
	l_section child_start;

	// bad
	if(whee == NULL){
		return;
	}

	// if prev is NULL, we're just starting so we need our start point
	if(prev == NULL){
		Assert(whee->links[LINK_RIGHT] != NULL);
		nebl_generate_section(bi, width, whee, whee->links[LINK_RIGHT], &start, NULL, 1, 0);
	} else {
		start = *prev;
	}
	
	// if we have a child section	
	if(whee->links[LINK_CHILD]){		
		// generate section
		nebl_generate_section(bi, width * 0.5f, whee, whee->links[LINK_CHILD], &child_start, &end, 0, whee->links[LINK_CHILD]->links[LINK_RIGHT] == NULL ? 1 : 0);

		// render
		nebl_render_section(bi, &child_start, &end);			

		// maybe continue
		if(whee->links[LINK_CHILD]->links[LINK_RIGHT] != NULL){
			nebl_render(bi, whee->links[LINK_CHILD], width * 0.5f, &end);
		}
	}	
		
	// if the next section is an end section
	if(whee->links[LINK_RIGHT]->links[LINK_RIGHT] == NULL){
		l_section temp;

		// generate section
		nebl_generate_section(bi, width, whee, whee->links[LINK_RIGHT], &temp, &end, 0, 1);

		// render the section
		nebl_render_section(bi, &start, &end);		
	}
	// a middle section
	else if(whee->links[LINK_RIGHT]->links[LINK_RIGHT] != NULL){
		// generate section
		nebl_generate_section(bi, width, whee->links[LINK_RIGHT], whee->links[LINK_RIGHT]->links[LINK_RIGHT], &end, NULL, 0, 0);

		// render the section
		nebl_render_section(bi, &start, &end);

		// recurse through him
		nebl_render(bi, whee->links[LINK_RIGHT], width, &end);
	}
}

// given a valid, complete bolt, jitter him based upon his noise
void nebl_jitter(l_bolt *b)
{
	matrix m;
	vec3d temp;
	float length;
	l_node *moveup;
	bolt_type *bi = NULL;

	// sanity
	if(b == NULL){
		return;
	}
	if( b->type >= Bolt_types.size() ){
		return;		
	}
	bi = &Bolt_types[b->type];

	// get the bolt direction
	length = vm_vec_normalized_dir(&temp, &b->strike, &b->start);
	vm_vector_2_matrix_norm(&m, &temp, nullptr, nullptr);

	// jitter all nodes on the main trunk
	moveup = b->head;
	while(moveup != NULL){
		temp = moveup->pos;
		vm_vec_random_in_circle(&moveup->pos, &temp, &m, frand_range(0.0f, length * bi->noise), false);

		// just on the main trunk
		moveup = moveup->links[LINK_RIGHT];
	}	
}

// return the index of a given bolt type by name
int nebl_get_bolt_index(const char *name)
{
	for(int idx=0; idx<(int)Bolt_types.size(); idx++){
		if(!strcmp(name, Bolt_types[idx].name)){
			return idx;
		}
	}

	return -1;
}

// return the index of a given storm type by name
int nebl_get_storm_index(const char *name)
{
	if (name == NULL)
		return -1;

	for(int idx=0; idx<(int)Storm_types.size(); idx++){
		if(!strcmp(name, Storm_types[idx].name)){
			return idx;
		}
	}

	return -1;
}
