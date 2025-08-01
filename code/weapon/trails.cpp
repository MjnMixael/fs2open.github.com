/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/



#include "cmdline/cmdline.h"
#include "globalincs/systemvars.h"
#include "graphics/2d.h"
#include "io/timer.h"
#include "mission/missionparse.h"
#include "nebula/neb.h"
#include "render/3d.h" 
#include "ship/ship.h"
#include "tracing/tracing.h"
#include "weapon/trails.h"
#include "render/batching.h"

static int Num_trails = 0;
static trail Trails;

void trail_info_init(trail_info* t_info) {
	t_info->pt = vmd_zero_vector;
	t_info->w_start = 0.0f;
	t_info->w_end = 0.0f;
	t_info->a_start = 0.0f;
	t_info->a_end = 0.0f;
	t_info->a_decay_exponent = 1.0f;
	t_info->max_life = 0.0f;
	t_info->spew_duration = 0;
	generic_bitmap_init(&t_info->texture);
	t_info->texture_stretch = 1.0f;
	t_info->n_fade_out_sections = 0;
	t_info->spread = 0.0f;
}

// Reset everything between levels
void trail_level_init()
{
	Num_trails = 0;
	Trails.next = &Trails;
}

void trail_level_close()
{
	if ( !Num_trails ) {
		return;
	}

	trail *nextp;
	for(trail *trailp = Trails.next; trailp != &Trails; trailp = nextp)
	{
		nextp = trailp->next;

		//Now we can delete it
		delete trailp;
	}

	Num_trails=0;
	Trails.next = &Trails;
}

//returns the number of a free trail
//returns -1 if no free trails
trail *trail_create(trail_info *info, bool const_vel)
{
	// standalone server should never create trails
	// No trails at slot 0
	if((Game_mode & GM_STANDALONE_SERVER) || !Detail.weapon_extras)
		return NULL;

	// Make a new trail
	trail *trailp = new trail;

	// increment counter
	Num_trails++;

	// Init the trail data
	trailp->info = *info;
	trailp->tail = 0;
	trailp->head = 0;	
	trailp->object_died = false;		
	trailp->single_segment = const_vel && info->a_decay_exponent == 1.0f && info->spread == 0.0f && info->n_fade_out_sections == 0;
	trailp->trail_stamp = _timestamp(trailp->info.spew_duration);

	//Add it to the front of the list
	//This is quickest since there are no prev vars
	trailp->next = Trails.next;
	Trails.next = trailp;

	return trailp;
}

// output top and bottom vectors
// fvec == forward vector (eye viewpoint basically. in world coords)
// pos == world coordinate of the point we're calculating "around"
// w == width of the diff between top and bottom around pos
void trail_calc_facing_pts( vec3d *top, vec3d *bot, vec3d *fvec, vec3d *pos, float w )
{
	vec3d uvec, rvec;

	vm_vec_sub( &rvec, &Eye_position, pos );
	if (!IS_VEC_NULL(&rvec))
		vm_vec_normalize( &rvec );

	vm_vec_cross(&uvec,fvec,&rvec);
	if (!IS_VEC_NULL(&uvec))
		vm_vec_normalize(&uvec);

	// Scale the trails so that they are always at least some configured amount of pixels across.
	w = model_render_get_diameter_clamped_to_min_pixel_size(pos, w, Min_pixel_size_trail);

	vm_vec_scale_add( top, pos, &uvec, w * 0.5f );
	vm_vec_scale_add( bot, pos, &uvec, -w * 0.5f );
}

// trail is on ship
int trail_is_on_ship(trail *trailp, ship *shipp)
{
	if(trailp == NULL)
		return 0;

	for(int idx=0; idx<MAX_SHIP_CONTRAILS; idx++){
		if(shipp->trail_ptr[idx] == trailp){
			return 1;
		}
	}

	// nope
	return 0;
}

void trail_render( trail * trailp )
{
	int sections[NUM_TRAIL_SECTIONS];
	int num_sections = 0;

	if (trailp->tail == trailp->head)
		return;

	trail_info *ti	= &trailp->info;

	if (ti->texture.bitmap_id <= 0) {
		return;
	}

	auto batchp = batching_find_batch(ti->texture.bitmap_id, batch_info::FLAT_EMISSIVE);

	if (trailp->single_segment) {
		Assertion(trailp->tail == 2, "Single segment trail with more than two values!");
		// confusing, i know
		int front = trailp->tail - 1;
		int back = trailp->head;

		float speed = vm_vec_mag(&trailp->vel[front]);

		float total_len = speed * ti->max_life;
		float f_alpha, b_alpha, f_width, b_width;

		float t_front = trailp->val[front] - trailp->val[back];
		float t_back = MAX(0.0f, -trailp->val[back]);

		f_alpha = t_front * (ti->a_start - ti->a_end) + ti->a_end;
		b_alpha = t_back * (ti->a_start - ti->a_end) + ti->a_end;
		f_width = t_front * (ti->w_start - ti->w_end) + ti->w_end;
		b_width = t_back * (ti->w_start - ti->w_end) + ti->w_end;

		vec3d trail_direction, ftop, fbot, btop, bbot;
		vm_vec_normalized_dir(&trail_direction, &trailp->pos[back], &trailp->pos[front]);
		trail_calc_facing_pts(&ftop, &fbot, &trail_direction, &trailp->pos[front], f_width);
		trail_calc_facing_pts(&btop, &bbot, &trail_direction, &trailp->pos[back], b_width);

		vertex verts[4];
		verts[0].r = verts[0].g = verts[0].b = verts[0].a = (ubyte)fl2i(f_alpha * 255.0f);
		verts[1].r = verts[1].g = verts[1].b = verts[1].a = (ubyte)fl2i(f_alpha * 255.0f);
		verts[2].r = verts[2].g = verts[2].b = verts[2].a = (ubyte)fl2i(b_alpha * 255.0f);
		verts[3].r = verts[3].g = verts[3].b = verts[3].a = (ubyte)fl2i(b_alpha * 255.0f);

		verts[0].world = ftop;
		verts[1].world = fbot;
		verts[2].world = bbot;
		verts[3].world = btop;

		float uv_scale = (total_len / speed / (i2fl(ti->spew_duration) / MILLISECONDS_PER_SECOND)) / ti->texture_stretch;

		verts[0].texture_position.u = trailp->val[front] * uv_scale;
		verts[1].texture_position.u = trailp->val[front] * uv_scale;
		verts[2].texture_position.u = MAX(trailp->val[back], 0.0f) * uv_scale;
		verts[3].texture_position.u = MAX(trailp->val[back], 0.0f) * uv_scale;

		verts[0].texture_position.v = verts[3].texture_position.v = 0.0f;
		verts[1].texture_position.v = verts[2].texture_position.v = 1.0f;

		float ratio = b_width / f_width;
		if (f_width <= 0.0f)
			ratio = 999.0f;

		batching_add_quad(ti->texture.bitmap_id, verts, batchp, ratio);
		return;
	}

	int n = trailp->tail;

	do	{
		n--;

		if (n < 0)
			n = NUM_TRAIL_SECTIONS-1;

		if (trailp->val[n] > 1.0f)
			break;

		sections[num_sections++] = n;
	} while ( n != trailp->head );

	if (num_sections <= 1)
		return;

	float w_size = (ti->w_end - ti->w_start);
	float a_size = (ti->a_end - ti->a_start);
	int num_faded_sections = ti->n_fade_out_sections;

	vec3d prev_top, prev_bot; vm_vec_zero(&prev_top); vm_vec_zero(&prev_bot);
	float prev_U = 0;
	ubyte prev_alpha = 0;
	for (int i = 0; i < num_sections; i++) {
		n = sections[i];

		// first get the alpha
		float w = trailp->val[n] * w_size + ti->w_start;

		float fade = trailp->val[n];
		
		if (trailp->info.a_decay_exponent != 1.0f)
			fade = powf(trailp->val[n], trailp->info.a_decay_exponent);

		ubyte current_alpha = 0;
		if ((num_faded_sections > 0) && (i < num_faded_sections)) {
			float init_fade_out = ((float)i) / (float)num_faded_sections;
			current_alpha = (ubyte)fl2i((fade * a_size + ti->a_start) * 255.0f * init_fade_out * init_fade_out);
		} else {
			current_alpha = (ubyte)fl2i((fade * a_size + ti->a_start) * 255.0f);
		}

		if (Neb_affects_weapons) {
			float nebalpha = 1.0f;
			if(nebula_handle_alpha(nebalpha, &trailp->pos[n], Neb2_fog_visibility_trail))
				current_alpha = (ubyte)(current_alpha * nebalpha);
		}

		// get the direction of the trail
		vec3d trail_direction;
		if (i == 0) {
			// first point, direction is directly to the next trail point
			vm_vec_sub(&trail_direction, &trailp->pos[n], &trailp->pos[sections[i+1]]);
		} else if (i == num_sections - 1) {
			// last point, direction is directly to the previous trail point
			vm_vec_sub(&trail_direction, &trailp->pos[sections[i-1]], &trailp->pos[n]);
		} else {
			// direction is the average between the next and previous directions
			vec3d forward, backward;
			vm_vec_sub(&backward, &trailp->pos[sections[i-1]], &trailp->pos[n]);
			vm_vec_sub(&forward, &trailp->pos[n], &trailp->pos[sections[i+1]]);
			vm_vec_normalize(&backward);
			if (!vm_maybe_normalize(&forward, &forward)) {
				// ok weird edge case that can happen
				// we are likely the the 2nd trail point but the first trail point is right on top of us
				// so just use the backward direction to avoid that degenerate forward direction
				trail_direction = backward;
			} else {
				vm_vec_avg(&trail_direction, &forward, &backward);
			}
		}
		vm_vec_normalize_safe(&trail_direction);

		
		float current_U = i2fl(n) / trailp->info.texture_stretch;
		vec3d current_top, current_bot;
		trail_calc_facing_pts(&current_top, &current_bot, &trail_direction, &trailp->pos[n], w);
		

		if (i > 0) {
			if (i == num_sections-1) {
				// Last one...
				vertex verts[3];
				verts[0].r = verts[0].g = verts[0].b = verts[0].a = prev_alpha;
				verts[1].r = verts[1].g = verts[1].b = verts[1].a = prev_alpha;
				verts[2].r = verts[2].g = verts[2].b = verts[2].a = current_alpha;

				vec3d center;
				vm_vec_avg(&center, &current_top, &current_bot);

				verts[0].world = prev_top;
				verts[1].world = prev_bot;
				verts[2].world = center;

				verts[0].texture_position.u = prev_U;
				verts[1].texture_position.u = prev_U;
				verts[2].texture_position.u = current_U;

				verts[0].texture_position.v = 0.0f;
				verts[1].texture_position.v = 1.0f;
				verts[2].texture_position.v = 0.5f;

				batching_add_tri(ti->texture.bitmap_id, verts, batchp);
			} else {
				vertex verts[4];
				verts[0].r = verts[0].g = verts[0].b = verts[0].a = prev_alpha;
				verts[1].r = verts[1].g = verts[1].b = verts[1].a = prev_alpha;
				verts[2].r = verts[2].g = verts[2].b = verts[2].a = current_alpha;
				verts[3].r = verts[3].g = verts[3].b = verts[3].a = current_alpha;

				verts[0].world = prev_top;
				verts[1].world = prev_bot;
				verts[2].world = current_bot;
				verts[3].world = current_top;

				verts[0].texture_position.u = prev_U;
				verts[1].texture_position.u = prev_U;
				verts[2].texture_position.u = current_U;
				verts[3].texture_position.u = current_U;

				verts[0].texture_position.v = verts[3].texture_position.v = 0.0f;
				verts[1].texture_position.v = verts[2].texture_position.v = 1.0f;

				batching_add_quad(ti->texture.bitmap_id, verts, batchp);
			}
		}

		prev_top = current_top;
		prev_bot = current_bot;
		prev_U = current_U;
		prev_alpha = current_alpha;
	}
}

// Adds a new segment to trailp at pos
// In order for trailp's 'spread' field to have any effect, it must be nonzero and the orient must be non-null
// If so, the orient's fvec is the treated as the direction of the trail, and the 
// new trail point is given a random velocity orthogonal to the fvec (scaled by spread speed)
void trail_add_segment( trail *trailp, vec3d *pos , const matrix* orient, vec3d* velocity)
{
	int next = trailp->tail;

	if (!trailp->single_segment || trailp->tail < 2)
		trailp->tail++;
	else
		return;

	if ( trailp->tail >= NUM_TRAIL_SECTIONS )
		trailp->tail = 0;

	if ( trailp->head == trailp->tail )	{
		// wrapped!!
		trailp->head++;
		if ( trailp->head >= NUM_TRAIL_SECTIONS )
			trailp->head = 0;
	}
	
	trailp->pos[next] = *pos;
	trailp->val[next] = 0.0f;

	if (trailp->single_segment && velocity) {
		trailp->vel[next] = *velocity;
		if (next == 0) {
			trailp->val[next] = -1.0;
		}
	} else if (orient != nullptr && trailp->info.spread > 0.0f) {
		vm_vec_random_in_circle(&trailp->vel[next], &vmd_zero_vector, orient, trailp->info.spread, false, true);
	} else 
		vm_vec_zero(&trailp->vel[next]);
}		

void trail_set_segment( trail *trailp, vec3d *pos )
{
	int next = trailp->tail-1;
	if ( next < 0 )	{
		next = NUM_TRAIL_SECTIONS-1;
	}
	
	trailp->pos[next] = *pos;
}

void trail_move_all(float frametime)
{
	TRACE_SCOPE(tracing::TrailsMoveAll);

	int num_alive_segments,n;
	float time_delta;
	trail *next_trail;
	trail *prev_trail = &Trails;

	for (trail *trailp = Trails.next; trailp != &Trails; trailp = next_trail) {
		next_trail = trailp->next;

		num_alive_segments = 0;
		time_delta = frametime / trailp->info.max_life;

		if (trailp->single_segment) {
			trailp->val[0] += time_delta;

			if (trailp->val[0] > 0.0f) {
				// finished 'unfurling'
				// back end moves too now
				trailp->pos[0] += trailp->vel[0] * frametime;
			}

			if (trailp->object_died) {
				if (trailp->val[0] >= trailp->val[1])
					num_alive_segments = 0; // back has caught up to front and were dead
				else
					num_alive_segments = 2;
			} else {
				trailp->pos[1] += trailp->vel[1] * frametime;
				trailp->val[1] += time_delta;

				num_alive_segments = 2;
			}
		} else if ( trailp->tail != trailp->head )	{
			n = trailp->tail;			
			do	{
				n--;
				if ( n < 0 ) n = NUM_TRAIL_SECTIONS-1;

				trailp->val[n] += time_delta;

				if ( trailp->val[n] <= 1.0f ) {
					num_alive_segments++;	// Record how many still alive.
				}

				trailp->pos[n] += trailp->vel[n] * frametime; 

			} while ( n != trailp->head );
		}		
	
		if ( (num_alive_segments < 1) && trailp->object_died)
		{
			prev_trail->next = trailp->next;
			delete trailp;

			// decrement counter
			Num_trails--;
		}
		else
		{
			prev_trail = trailp;
		}
	}
}

void trail_object_died( trail *trailp )
{
	trailp->object_died = true;
}

void trail_render_all()
{
	GR_DEBUG_SCOPE("Render trails");
	TRACE_SCOPE(tracing::RenderTrails);

	// No trails at slot 0
	if ( !Detail.weapon_extras )
		return;

	for(trail *trailp = Trails.next; trailp!=&Trails; trailp = trailp->next )
	{
		trail_render(trailp);
	}
}

int trail_stamp_elapsed(trail *trailp)
{
	return timestamp_elapsed(trailp->trail_stamp);
}

void trail_set_stamp(trail *trailp)
{
	trailp->trail_stamp = _timestamp(trailp->info.spew_duration);
}
