/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 



#include "asteroid/asteroid.h"
#include "cmdline/cmdline.h"
#include "debris/debris.h"
#include "hud/hudbrackets.h"
#include "hud/hudtarget.h"
#include "iff_defs/iff_defs.h"
#include "jumpnode/jumpnode.h"
#include "mission/missionparse.h"
#include "object/object.h"
#include "parse/parselo.h"
#include "playerman/player.h"
#include "render/3d.h"
#include "ship/ship.h"
#include "tracing/tracing.h"
#include "weapon/emp.h"
#include "weapon/weapon.h"

#define FADE_FACTOR	2			// how much the bounding brackets get faded
#define LOWEST_RED	50			// lowest r value for bounding bracket
#define LOWEST_GREEN	50			// lowest g value for bounding bracket
#define LOWEST_BLUE	50			// lowest b value for bounding bracket

char Ships_attack_fname[GR_NUM_RESOLUTIONS][MAX_FILENAME_LEN] = {
	"attacker",
	"attacker"
};

// resolution adjust factors for the above defines
int Min_target_box_width[GR_NUM_RESOLUTIONS] = { 20, 30 };
int Min_target_box_height[GR_NUM_RESOLUTIONS] = { 20, 30 };
int Min_subtarget_box_width[GR_NUM_RESOLUTIONS] = { 12, 24 };
int Min_subtarget_box_height[GR_NUM_RESOLUTIONS] = { 12, 24 };

void hud_init_brackets()
{
}

void draw_brackets_square(graphics::line_draw_list* draw_list, int x1, int y1, int x2, int y2, int resize_mode)
{
	int	width, height;

	if(resize_mode != GR_RESIZE_NONE || gr_screen.rendering_to_texture != -1)
	{
		gr_resize_screen_pos(&x1, &y1, NULL, NULL, resize_mode);
		gr_resize_screen_pos(&x2, &y2, NULL, NULL, resize_mode);
	}
	
	width = x2 - x1;
	height = y2 - y1;
	if (width < 1 || height < 1)
		return;

	// make the brackets extend 25% of the way along the width or height
	int bracket_width = width/4;
	int bracket_height = height/4;

	// horizontal lines
	if ( (x1 + bracket_width > 0) && (x1 < gr_screen.clip_width) ){
		draw_list->add_gradient(x1,y1,x1+bracket_width-1,y1,GR_RESIZE_NONE);	// top left
		draw_list->add_gradient(x1,y2,x1+bracket_width-1,y2,GR_RESIZE_NONE);	// bottom left
	}

	if ( (x2 - bracket_width < gr_screen.clip_width) && (x2 > 0) )	{
		draw_list->add_gradient(x2, y1, x2-bracket_width+1,y1,GR_RESIZE_NONE);	// top right
		draw_list->add_gradient(x2, y2, x2-bracket_width+1,y2,GR_RESIZE_NONE);	// bottom right
	}

	// vertical lines
	if ( (y1 + bracket_height > 0) && (y1 < gr_screen.clip_height) ) {
		draw_list->add_gradient(x1,y1,x1,y1+bracket_height-1,GR_RESIZE_NONE);		// top left
		draw_list->add_gradient(x2,y1,x2,y1+bracket_height-1,GR_RESIZE_NONE);		// top right
	}

	if ( (y2 - bracket_height < gr_screen.clip_height) && (y2 > 0) )	{
		draw_list->add_gradient(x1,y2,x1,y2-bracket_height+1,GR_RESIZE_NONE);	// bottom left
		draw_list->add_gradient(x2,y2,x2,y2-bracket_height+1,GR_RESIZE_NONE);	// bottom right
	}
}

// NOTE: all values should be in unscaled range
void draw_brackets_square_quick(graphics::line_draw_list* draw_list, int x1, int y1, int x2, int y2, int thick)
{
	int	width, height;

	width = x2 - x1;
	height = y2 - y1;
	
	// make the brackets extend 25% of the way along the width or height
	int bracket_width = width/4;
	int bracket_height = height/4;

	// top line
	draw_list->add_line(x1,y1,x1+bracket_width,y1);
	draw_list->add_line(x2,y1,x2-bracket_width,y1);
	if ( thick ) {
		draw_list->add_line(x1,y1+1,x1+bracket_width,y1+1);
		draw_list->add_line(x2,y1+1,x2-bracket_width,y1+1);
	}

	// bottom line
	draw_list->add_line(x1,y2,x1+bracket_width,y2);
	draw_list->add_line(x2,y2,x2-bracket_width,y2);
	if ( thick ) {
		draw_list->add_line(x1,y2-1,x1+bracket_width,y2-1);
		draw_list->add_line(x2,y2-1,x2-bracket_width,y2-1);
	}

	// left line
	if ( thick ) {
		draw_list->add_line(x1,y1+2,x1,y1+bracket_height);
		draw_list->add_line(x1,y2-2,x1,y2-bracket_height);
		draw_list->add_line(x1+1,y1+2,x1+1,y1+bracket_height);
		draw_list->add_line(x1+1,y2-2,x1+1,y2-bracket_height);
	} else {
		draw_list->add_line(x1,y1+1,x1,y1+bracket_height);
		draw_list->add_line(x1,y2-1,x1,y2-bracket_height);
	}

	// right line
	if ( thick ) {
		draw_list->add_line(x2,y1+2,x2,y1+bracket_height);
		draw_list->add_line(x2,y2-2,x2,y2-bracket_height);
		draw_list->add_line(x2-1,y1+2,x2-1,y1+bracket_height);
		draw_list->add_line(x2-1,y2-2,x2-1,y2-bracket_height);
	} else {
		draw_list->add_line(x2,y1+1,x2,y1+bracket_height);
		draw_list->add_line(x2,y2-1,x2,y2-bracket_height);
	}
}

void draw_brackets_diamond_quick(graphics::line_draw_list* draw_list, int x1, int y1, int x2, int y2)
{
	int width, height, half_width, half_height;
	int center_x, center_y;
	int x_delta, y_delta;

	float side_len, bracket_len;

	width = x2 - x1;
	height = y2 - y1;

	half_width = (int)std::lround( width/2.0f);
	half_height = (int)std::lround( height/2.0f);

	side_len = (float)_hypot(half_width, half_height);
	bracket_len = side_len / 8;
	
	x_delta = (int)std::lround(bracket_len * width / side_len);
	y_delta = (int)std::lround(bracket_len * height / side_len);

	center_x = x1 + half_width;
	center_y = y1 + half_height;

	// top left line
	draw_list->add_line(center_x - x_delta, y1 + y_delta,center_x, y1);
	draw_list->add_line(x1 + x_delta, center_y - y_delta, x1, center_y);

	// top right line
	draw_list->add_line(center_x + x_delta, y1 + y_delta,center_x, y1);
	draw_list->add_line(x2 - x_delta, center_y - y_delta, x2, center_y);

	// bottom left line
	draw_list->add_line(x1 + x_delta, center_y + y_delta, x1, center_y);
	draw_list->add_line(center_x - x_delta, y2 - y_delta, center_x, y2);

	// bottom right line
	draw_list->add_line(x2 - x_delta, center_y + y_delta, x2, center_y);
	draw_list->add_line(center_x + x_delta, y2 - y_delta, center_x, y2);

	// draw an 'X' in the middle of the brackets
	draw_list->add_line(center_x-x_delta, center_y-y_delta, center_x+x_delta, center_y+y_delta);
	draw_list->add_line(center_x-x_delta, center_y+y_delta, center_x+x_delta, center_y-y_delta);
}

extern int HUD_drew_selection_bracket_on_target;
//Do we want to show the ship & class name?
extern bool Extra_target_info;

// Display the current target distance, right justified at (x,y)
void hud_target_show_dist_on_bracket(int x, int y, float distance, int font_num)
{
	char	text_dist[64];
	int	w,h;
	float displayed_distance;

	if ( y < 0 || y > gr_screen.clip_height ) {
		return;
	}

	if ( x < 0 || x > gr_screen.clip_width ) {
		return;
	}

	// scale by distance modifier from hud_guages.tbl for display purposes
	displayed_distance = distance * Hud_unit_multiplier;

	sprintf(text_dist, "%d", (int)std::lround(displayed_distance));
	hud_num_make_mono(text_dist, font_num);
	gr_get_string_size(&w,&h,text_dist);

	int y_delta = 4;
	if ( HUD_drew_selection_bracket_on_target ) {
		y += 4;
	}

	gr_string(x - w+2, y+y_delta, text_dist);
}


int	Ships_attacking_bitmap = -1;

int num_ships_attacking(int target_objnum);

int draw_subsys_brackets(graphics::line_draw_list* draw_list, ship_subsys* subsys, int min_width, int min_height, bool draw, bool set_color, int* draw_coords)
{
	Assertion(subsys != NULL, "Invalid subsystem pointer passed to draw_subsys_brackets!");

	int		target_objnum;
	object* targetp;
	vertex subobj_vertex;
	vec3d	subobj_pos;
	int x1,x2,y1,y2;

	target_objnum = subsys->parent_objnum;
	Assert(target_objnum != -1);
	targetp = &Objects[target_objnum];
	Assert( targetp->type == OBJ_SHIP );

	get_subsystem_world_pos(targetp, subsys, &subobj_pos);

	g3_rotate_vertex(&subobj_vertex,&subobj_pos);

	g3_project_vertex(&subobj_vertex);
	if (subobj_vertex.flags & PF_OVERFLOW)  // if overflow, no point in drawing brackets
		return -1;

	int subobj_x = (int)std::lround(subobj_vertex.screen.xyw.x);
	int subobj_y = (int)std::lround(subobj_vertex.screen.xyw.y);
	int hud_subtarget_w, hud_subtarget_h, bound_rc;

	bound_rc = subobj_find_2d_bound(subsys->system_info->radius, &targetp->orient, &subobj_pos, &x1,&y1,&x2,&y2);
	if ( bound_rc != 0 )
		return -1;

	hud_subtarget_w = x2-x1+1;
	if ( hud_subtarget_w > gr_screen.clip_width ) {
		hud_subtarget_w = gr_screen.clip_width;
	}

	hud_subtarget_h = y2-y1+1;
	if ( hud_subtarget_h > gr_screen.clip_height ) {
		hud_subtarget_h = gr_screen.clip_height;
	}

	if ( hud_subtarget_w > gr_screen.max_w ) {
		x1 = subobj_x - (gr_screen.max_w>>1);
		x2 = subobj_x + (gr_screen.max_w>>1);
	}
	if ( hud_subtarget_h > gr_screen.max_h ) {
		y1 = subobj_y - (gr_screen.max_h>>1);
		y2 = subobj_y + (gr_screen.max_h>>1);
	}

	// *** these unsize take care of everything below ***
	gr_unsize_screen_pos( &hud_subtarget_w, &hud_subtarget_h );
	gr_unsize_screen_pos( &subobj_x, &subobj_y );
	gr_unsize_screen_pos( &x1, &y1 );
	gr_unsize_screen_pos( &x2, &y2 );

	if ( hud_subtarget_w < min_width ) {
		x1 = subobj_x - (min_width>>1);
		x2 = subobj_x + (min_width>>1);
	}
	if ( hud_subtarget_h < min_height ) {
		y1 = subobj_y - (min_height>>1);
		y2 = subobj_y + (min_height>>1);
	}

	// determine if subsystem is on far or near side of the ship
	bool in_sight = ship_subsystem_in_sight(targetp, subsys, &View_position, &subobj_pos, false);
	
	if (draw)
	{

		if (set_color)
		{
			// AL 29-3-98: If subsystem is destroyed, draw gray brackets
			// Goober5000: this will now execute for fighterbays if the bay has been given a
			// percentage subsystem strength in ships.tbl
			// Goober5000: this will now execute for any subsys that takes damage and will not
			// execute for any subsys that doesn't take damage
			if ( (subsys->current_hits <= 0) && ( ship_subsys_takes_damage(subsys) ) ) {
				gr_set_color_fast(iff_get_color(IFF_COLOR_MESSAGE, 1));
			} else {
				hud_set_iff_color( targetp, 1 );
			}
		}

		if ( in_sight ) {
			draw_brackets_square_quick(draw_list, x1, y1, x2, y2);
		} else {
			draw_brackets_diamond_quick(draw_list, x1, y1, x2, y2);
		}
	}
	
	if (draw_coords != nullptr)
	{
		// Positions are unsized, we need to resize them to get the actual screen coordinates
		gr_resize_screen_pos(&x1, &y1);
		gr_resize_screen_pos(&x2, &y2);

		draw_coords[0] = x1;
		draw_coords[1] = y1;
		draw_coords[2] = x2;
		draw_coords[3] = y2;
	}
	// mprintf(("Drawing subobject brackets at %4i, %4i\n", sx, sy));
	
	return in_sight;
}

HudGaugeBrackets::HudGaugeBrackets():
HudGauge3DAnchor(HUD_OBJECT_BRACKETS, HUD_OFFSCREEN_INDICATOR, false, true, VM_DEAD_VIEW, 255, 255, 255)
{
}

void HudGaugeBrackets::initMinTargetBoxSizes(int w, int h)
{
	Min_target_box_width = w;
	Min_target_box_height = h;
}

void HudGaugeBrackets::initMinSubTargetBoxSizes(int w, int h)
{
	Min_subtarget_box_width = w;
	Min_subtarget_box_height = h;
}

void HudGaugeBrackets::initBitmaps(char *fname)
{
	attacking_dot = bm_load(fname);
	if (attacking_dot < 0) {
		Warning(LOCATION,"Cannot load hud ani: %s\n", fname);
	}
}

void HudGaugeBrackets::render(float  /*frametime*/, bool config)
{
	// Brackets are do not support config settings
	if (config) {
		return;
	}
	
	// don't display brackets if we're warping out.
	if ( Player->control_mode != PCM_NORMAL ) {
		return;
	}

	bool in_frame = g3_in_frame() > 0;
	if(!in_frame)
		g3_start_frame(0);

	for(size_t i = 0; i < target_display_list.size(); i++) {
		// make sure this point is projected. Otherwise, skip.
		if( !(target_display_list[i].target_point.flags & PF_OVERFLOW) ) {
			if ( target_display_list[i].objp ) {
				renderObjectBrackets(target_display_list[i].objp, &target_display_list[i].bracket_clr, target_display_list[i].correction, 
					target_display_list[i].correction, target_display_list[i].flags);
			} else {
				// no corresponding object so this must represent a nav point.
				renderNavBrackets(&target_display_list[i].target_pos, &target_display_list[i].target_point, &target_display_list[i].bracket_clr, 
					target_display_list[i].name);
			}
		}
	}

	line_draw_list.flush();

	if(!in_frame)
		g3_end_frame();
}

void HudGaugeBrackets::renderObjectBrackets(object *targetp, color *clr, int w_correction, int h_correction, int flags)
{
	TRACE_SCOPE(tracing::RenderTargetingBracket);

	int x1,x2,y1,y2;
	bool draw_box = true;
	int bound_rc;


	bool not_player_target = (Player_ai->target_objnum < 0 || targetp != &Objects[Player_ai->target_objnum]);


	// target_is_dying is set to zero when the target is not dying, -1 when no target. Only 1, aka "TRUE", is invalid.
	if ( not_player_target || Player->target_is_dying <= 0) {
		int modelnum;

		switch ( targetp->type ) {
		case OBJ_SHIP:
			modelnum = Ship_info[Ships[targetp->instance].ship_info_index].model_num;
			bound_rc = model_find_2d_bound_min( modelnum, &targetp->orient, &targetp->pos,&x1,&y1,&x2,&y2 );
			if ( bound_rc != 0 ) {
				draw_box = false;
			}
			break;

		case OBJ_DEBRIS:
			modelnum = Debris[targetp->instance].model_num;
			bound_rc = submodel_find_2d_bound_min( modelnum, Debris[targetp->instance].submodel_num, &targetp->orient, &targetp->pos,&x1,&y1,&x2,&y2 );
			if ( bound_rc != 0 ) {
				draw_box = false;
			}
			break;

		case OBJ_WEAPON:
			modelnum = Weapon_info[Weapons[targetp->instance].weapon_info_index].model_num;
			if (modelnum != -1)
				bound_rc = model_find_2d_bound_min( modelnum, &targetp->orient, &targetp->pos,&x1,&y1,&x2,&y2 );
			else {
				vertex vtx;
				g3_rotate_vertex(&vtx,&targetp->pos);
				g3_project_vertex(&vtx);
				x1 = x2 = (int) vtx.screen.xyw.x;
				y1 = y2 = (int) vtx.screen.xyw.y;
			}

			break;

		case OBJ_ASTEROID:
			{
			int pof = 0;
			pof = Asteroids[targetp->instance].asteroid_subtype;
			modelnum = Asteroid_info[Asteroids[targetp->instance].asteroid_type].subtypes[pof].model_number;
			bound_rc = model_find_2d_bound_min( modelnum, &targetp->orient, &targetp->pos,&x1,&y1,&x2,&y2 );
			}
			break;

		case OBJ_JUMP_NODE:
			{
			auto jnp = jumpnode_get_by_objp(targetp);
			modelnum = jnp ? jnp->GetModelNumber() : -1;
			bound_rc = model_find_2d_bound_min( modelnum, &targetp->orient, &targetp->pos,&x1,&y1,&x2,&y2 );
			}
			break;

		default:
			Int3();	// should never happen
			return;
		}

		Hud_target_w = x2-x1+1;
		if ( Hud_target_w > gr_screen.clip_width ) {
			Hud_target_w = gr_screen.clip_width;
		}

		Hud_target_h = y2-y1+1;
		if ( Hud_target_h > gr_screen.clip_height ) {
			Hud_target_h = gr_screen.clip_height;
		}

		if(clr->red && clr->green && clr->blue ) {
			gr_set_color_fast(clr);
		} else {
			// if no specific color defined, use the IFF color.
			hud_set_iff_color(targetp, 1);
		}

		if ( draw_box ) {
			float distance = 0.0f; // init to 0 if we don't have to display distance
			int target_objnum = -1;

			if(flags & TARGET_DISPLAY_DIST) {
				// since we can have either the target, or rogue asteroids bracketed,
				// check which case we are in, and then do the correct distance.
				if (not_player_target) {
					distance = hud_find_target_distance(targetp, &Player_obj->pos);
				} else {
					distance = Player_ai->current_target_distance;
				}
			}

			if(flags & TARGET_DISPLAY_DOTS) {
				target_objnum = OBJ_INDEX(targetp);
			}

			renderBoundingBrackets(x1-5, y1-5, x2+5, y2+5, w_correction, h_correction, distance, target_objnum, flags);
		}

		if ( (targetp->type == OBJ_SHIP) && (flags & TARGET_DISPLAY_SUBSYS) ) {
			renderBoundingBracketsSubobject();
		}
	}
}

void HudGaugeBrackets::renderNavBrackets(vec3d* nav_pos, vertex* nav_point, color* clr, char* string)
{
	TRACE_SCOPE(tracing::RenderNavBracket);

	float dist;
	int box_scale = 15;
	int x, y;

	x = int(nav_point->screen.xyw.x);
	y = int(nav_point->screen.xyw.y);

	// draw this nav bracket based on this gauge's base resolution
	gr_set_screen_scale(base_w, base_h);

	gr_unsize_screen_pos( &x, &y );

	dist = vm_vec_dist_quick(&Objects[Player_ship->objnum].pos, nav_pos);

	if (dist < 1000)
		box_scale = int(float(dist)/1000.0f * 15);
	if (box_scale < 4)
		box_scale=4;

	gr_set_color_fast(clr);
	draw_brackets_square(&line_draw_list, x-box_scale, y-box_scale, x+box_scale, y+box_scale);

	// draw the nav name
	if(string) {
		gr_string(x-(box_scale+10), y-(box_scale+20), string);
	}

	// draw distance to target in lower right corner of box
	hud_target_show_dist_on_bracket(x+(box_scale+10),y+(box_scale+10), dist, font_num);

	// bring the scale back to normal
	gr_reset_screen_scale();
}

void HudGaugeBrackets::renderBoundingBrackets(int x1, int y1, int x2, int y2, int w_correction, int h_correction, float distance, int target_objnum, int flags)
{
	int width, height;

	if ( ( x1 < 0 && x2 < 0 ) || ( y1 < 0 && y2 < 0 ) )
		return;

	if ( ( x1 > gr_screen.clip_width && x2 > gr_screen.clip_width ) ||
		  ( y1 > gr_screen.clip_height && y2 > gr_screen.clip_height ) )
		return;

	// draw the all bracket components based on this gauge's screen scale
	gr_set_screen_scale(base_w, base_h);

	// *** everything below is taken care of with this unsize ***
	gr_unsize_screen_pos(&x1, &y1);
	gr_unsize_screen_pos(&x2, &y2);

	width = x2-x1;
	Assert(width>=0);

	height = y2-y1;
	Assert(height>=0);

	if ( (width >= (base_w)) && (height >= (base_h)) ) {
		return;
	}

	if (width < Min_target_box_width) {
		x1 = x1 - (Min_target_box_width-width)/2;
		x2 = x2 + (Min_target_box_width-width)/2;
	}

	if (height < Min_target_box_height) {
		y1 = y1 - (Min_target_box_height-height)/2;
		y2 = y2 + (Min_target_box_height-height)/2;
	}
	
	draw_brackets_square(&line_draw_list, x1-w_correction, y1-h_correction, x2+w_correction, y2+h_correction);

	// draw distance to target in lower right corner of box
	if ( distance > 0 ) {
		hud_target_show_dist_on_bracket(x2+w_correction,y2+h_correction,distance,font_num);
	}

	//	Maybe show + for each additional fighter or bomber attacking target.
	if ( (target_objnum != -1) ) {
		int num_attacking = num_ships_attacking(target_objnum);

		//	If a ship is attacked by player, show one fewer plus
		int	k=0;
		if (Objects[target_objnum].type == OBJ_SHIP) {
			if (iff_x_attacks_y(Player_ship->team, Ships[Objects[target_objnum].instance].team)) {
				k = 1;
			}
		} else {
			k = 1;
		}

		if (num_attacking > k) {
			int	i, w, h, num_blips;
			
			num_blips = num_attacking-k;
			if (num_blips > 4) {
				num_blips = 4;
			}

			//int bitmap = get_blip_bitmap();

			bm_get_info(attacking_dot, &w, &h);

			if (attacking_dot >= 0) {
				if (num_blips > 3)
					y1 -= 3;

				for (i=0; i<num_blips; i++) {
					GR_AABITMAP(attacking_dot, x2+3, y1+i*(h+2));
				}
			}

			// Increment for the position of ship name/class.
			// DEPENDANT ON ATTACKER SIZE (X)
			x2 += w + 2;
		}
	}

	if( (target_objnum != -1) && (flags & (TARGET_DISPLAY_NAME | TARGET_DISPLAY_CLASS)) ) {
		object* t_objp = &Objects[target_objnum];
		const char* tinfo_name = NULL;
		const char* tinfo_class = NULL;
		char temp_name[NAME_LENGTH*2+3];
		char temp_class[NAME_LENGTH];

		switch(t_objp->type) {
			case OBJ_SHIP:
				hud_stuff_ship_name(temp_name, &Ships[t_objp->instance]);
				hud_stuff_ship_class(temp_class, &Ships[t_objp->instance]);
				tinfo_name = temp_name;
				tinfo_class = temp_class;

				// maybe concatenate the callsign
				if (*temp_name) {
					char temp_callsign[NAME_LENGTH];

					hud_stuff_ship_callsign(temp_callsign, &Ships[t_objp->instance]);
					if (*temp_callsign)
						sprintf(&temp_name[strlen(temp_name)], " (%s)", temp_callsign);
				} else { // maybe substitute the callsign
					hud_stuff_ship_callsign(temp_name, &Ships[t_objp->instance]);
				}
				break;

			case OBJ_DEBRIS:
				tinfo_name = XSTR("debris", 348);
				break;
			case OBJ_WEAPON:
				tinfo_name = Weapon_info[Weapons[t_objp->instance].weapon_info_index].get_display_name();
				break;
			case OBJ_ASTEROID:
				switch(Asteroids[t_objp->instance].asteroid_type) {
					case ASTEROID_TYPE_SMALL:
					case ASTEROID_TYPE_MEDIUM:
					case ASTEROID_TYPE_LARGE:
						tinfo_name = XSTR("asteroid", 431);
						break;
					default:
						tinfo_name = XSTR("debris", 348);
				}
				break;
			case OBJ_JUMP_NODE:
				auto jnp = jumpnode_get_by_objnum(target_objnum);
				tinfo_name = jnp ? jnp->GetDisplayName() : "";
				break;
		}

		if(tinfo_name && (flags & TARGET_DISPLAY_NAME)) {
			gr_string(x2+3, y1, tinfo_name);
		} 
		if(tinfo_class && (flags & TARGET_DISPLAY_CLASS)) {
			gr_string(x2+3, y1+gr_get_font_height(), tinfo_class);
		}
	}

	// we're done, so bring the scale back to normal
	gr_reset_screen_scale();
}

void HudGaugeBrackets::renderBoundingBracketsSubobject()
{
	if (Player_ai->targeted_subsys_parent == Player_ai->target_objnum)
		if (Player_ai->targeted_subsys != NULL) {
			ship_subsys	*subsys;
			int		target_objnum;
			object* targetp;
			vertex subobj_vertex;
			vec3d	subobj_pos;
			int x1,x2,y1,y2;

			subsys = Player_ai->targeted_subsys;
			target_objnum = Player_ai->target_objnum;
			Assert(target_objnum != -1);
			targetp = &Objects[target_objnum];
			Assert( targetp->type == OBJ_SHIP );

			gr_set_screen_scale(base_w, base_h);

			get_subsystem_world_pos(targetp, subsys, &subobj_pos);

			g3_rotate_vertex(&subobj_vertex,&subobj_pos);

			g3_project_vertex(&subobj_vertex);
			if (subobj_vertex.flags & PF_OVERFLOW)  // if overflow, no point in drawing brackets
				return;

			int subobj_x = (int)std::lround(subobj_vertex.screen.xyw.x);
			int subobj_y = (int)std::lround(subobj_vertex.screen.xyw.y);
			int hud_subtarget_w, hud_subtarget_h, bound_rc;

			bound_rc = subobj_find_2d_bound(subsys->system_info->radius, &targetp->orient, &subobj_pos, &x1,&y1,&x2,&y2);
			if ( bound_rc != 0 )
				return;

			hud_subtarget_w = x2-x1+1;
			if ( hud_subtarget_w > gr_screen.clip_width ) {
				hud_subtarget_w = gr_screen.clip_width;
			}

			hud_subtarget_h = y2-y1+1;
			if ( hud_subtarget_h > gr_screen.clip_height ) {
				hud_subtarget_h = gr_screen.clip_height;
			}

			if ( hud_subtarget_w > gr_screen.max_w ) {
				x1 = subobj_x - (gr_screen.max_w>>1);
				x2 = subobj_x + (gr_screen.max_w>>1);
			}
			if ( hud_subtarget_h > gr_screen.max_h ) {
				y1 = subobj_y - (gr_screen.max_h>>1);
				y2 = subobj_y + (gr_screen.max_h>>1);
			}

			// *** these unsize take care of everything below ***
			gr_unsize_screen_pos( &hud_subtarget_w, &hud_subtarget_h );
			gr_unsize_screen_pos( &subobj_x, &subobj_y );
			gr_unsize_screen_pos( &x1, &y1 );
			gr_unsize_screen_pos( &x2, &y2 );

			if ( hud_subtarget_w < Min_subtarget_box_width ) {
				x1 = subobj_x - (Min_subtarget_box_width>>1);
				x2 = subobj_x + (Min_subtarget_box_width>>1);
			}
			if ( hud_subtarget_h < Min_subtarget_box_height ) {
				y1 = subobj_y - (Min_subtarget_box_height>>1);
				y2 = subobj_y + (Min_subtarget_box_height>>1);
			}

			// determine if subsystem is on far or near side of the ship
			Player->subsys_in_view = ship_subsystem_in_sight(targetp, subsys, &View_position, &subobj_pos, false) ? 1 : 0;

			// AL 29-3-98: If subsystem is destroyed, draw gray brackets
			// Goober5000: this will now execute for fighterbays if the bay has been given a
			// percentage subsystem strength in ships.tbl
			// Goober5000: this will now execute for any subsys that takes damage and will not
			// execute for any subsys that doesn't take damage
			if ( (Player_ai->targeted_subsys->current_hits <= 0) && ( ship_subsys_takes_damage(Player_ai->targeted_subsys) ) ) {
				gr_set_color_fast(iff_get_color(IFF_COLOR_MESSAGE, 1));
			} else {
				hud_set_iff_color( targetp, 1 );
			}

			if ( Player->subsys_in_view ) {
				draw_brackets_square_quick(&line_draw_list, x1, y1, x2, y2);
			} else {
				draw_brackets_diamond_quick(&line_draw_list, x1, y1, x2, y2);
			}
			// mprintf(("Drawing subobject brackets at %4i, %4i\n", sx, sy));

			// reset the scale to normal
			gr_reset_screen_scale();
		}
}
