/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell
 * or otherwise commercially exploit the source or things you created based on the
 * source.
 *
*/



#include <cmath>

#include "cfile/cfile.h"
#include "debugconsole/console.h"
#include "def_files/def_files.h"
#include "graphics/2d.h"
#include "graphics/material.h"
#include "math/vecmat.h"
#include "mission/missionparse.h"
#include "nebula/neb.h"
#include "parse/parselo.h"
#include "render/3d.h"
#include "starfield/nebula.h"

// distance the background nebula sphere sits at; large so it reads as a faraway backdrop
#define NEBULA_RADIUS 1000.0f

// ----------------------------------------------------------------------------------------------------
// data-driven pattern / color registries
// ----------------------------------------------------------------------------------------------------

SCP_vector<old_nebula_pattern> Old_nebula_patterns;
SCP_vector<old_nebula_color>   Old_nebula_colors;

int old_nebula_pattern_lookup(const char *name)
{
	for (int i = 0; i < static_cast<int>(Old_nebula_patterns.size()); i++) {
		if (!stricmp(Old_nebula_patterns[i].name.c_str(), name))
			return i;
	}
	return -1;
}

int old_nebula_color_lookup(const char *name)
{
	for (int i = 0; i < static_cast<int>(Old_nebula_colors.size()); i++) {
		if (!stricmp(Old_nebula_colors[i].name.c_str(), name))
			return i;
	}
	return -1;
}

const char *old_nebula_pattern_name(int index)
{
	if (index < 0 || index >= static_cast<int>(Old_nebula_patterns.size()))
		return "";
	return Old_nebula_patterns[index].name.c_str();
}

const char *old_nebula_color_name(int index)
{
	if (index < 0 || index >= static_cast<int>(Old_nebula_colors.size()))
		return "";
	return Old_nebula_colors[index].name.c_str();
}

// parse the #Old Nebula Patterns / #Old Nebula Colors sections out of whatever table text is
// currently loaded into the parse buffer.  Entries are matched by name, so a later table can
// either override an existing entry in place or append a brand-new one.
static void old_nebula_parse_buffer()
{
	// patterns
	reset_parse();
	if (skip_to_string("#Old Nebula Patterns") == 1) {
		while (optional_string("$Name:")) {
			SCP_string nm;
			stuff_string(nm, F_NAME);

			int idx = old_nebula_pattern_lookup(nm.c_str());
			old_nebula_pattern *p;
			if (idx < 0) {
				Old_nebula_patterns.emplace_back();
				p = &Old_nebula_patterns.back();
				p->name = nm;
			} else {
				p = &Old_nebula_patterns[idx];
			}

			if (optional_string("+Density:"))
				stuff_float(&p->density);
			if (optional_string("+Frequency:")) {
				stuff_float(&p->freq_u);
				stuff_float(&p->freq_v);
			}
			if (optional_string("+Warp:"))
				stuff_float(&p->warp);
			if (optional_string("+Contrast:"))
				stuff_float(&p->contrast);
			if (optional_string("+Seed:"))
				stuff_int(&p->seed);
			if (optional_string("+Resolution:")) {
				stuff_int(&p->res_lon);
				stuff_int(&p->res_lat);
			}
			if (optional_string("+Band:")) {
				stuff_float(&p->band_min);
				stuff_float(&p->band_max);
			}
		}
	}

	// colors
	reset_parse();
	if (skip_to_string("#Old Nebula Colors") == 1) {
		while (optional_string("$Name:")) {
			SCP_string nm;
			stuff_string(nm, F_NAME);

			int idx = old_nebula_color_lookup(nm.c_str());
			old_nebula_color *c;
			if (idx < 0) {
				Old_nebula_colors.emplace_back();
				c = &Old_nebula_colors.back();
				c->name = nm;
			} else {
				c = &Old_nebula_colors[idx];
			}

			if (optional_string("+RGB:")) {
				int rgb[3];
				stuff_int(&rgb[0]);
				stuff_int(&rgb[1]);
				stuff_int(&rgb[2]);
				CLAMP(rgb[0], 0, 255);
				CLAMP(rgb[1], 0, 255);
				CLAMP(rgb[2], 0, 255);
				c->r = static_cast<ubyte>(rgb[0]);
				c->g = static_cast<ubyte>(rgb[1]);
				c->b = static_cast<ubyte>(rgb[2]);
			}
		}
	}
}

// callback for parse_modular_table() / the base nebula.tbl
static void old_nebula_parse_table(const char *filename)
{
	try {
		read_file_text(filename, CF_TYPE_TABLES);
		old_nebula_parse_buffer();
	} catch (const parse::ParseException &e) {
		mprintf(("TABLES: Unable to parse old nebula data in '%s'!  Error message = %s.\n", filename, e.what()));
	}
}

void old_nebula_init()
{
	Old_nebula_patterns.clear();
	Old_nebula_colors.clear();

	// built-in defaults, embedded in the executable
	try {
		read_file_text_from_default(defaults_get_file("old_nebula.tbm"));
		old_nebula_parse_buffer();
	} catch (const parse::ParseException &e) {
		mprintf(("TABLES: Unable to parse default old_nebula.tbm!  Error message = %s.\n", e.what()));
	}

	// allow the game data to override or extend the defaults
	if (cf_exists_full("nebula.tbl", CF_TYPE_TABLES))
		old_nebula_parse_table("nebula.tbl");

	parse_modular_table("*-neb.tbm", old_nebula_parse_table);
}

// ----------------------------------------------------------------------------------------------------
// procedural mesh generation + rendering
// ----------------------------------------------------------------------------------------------------

static vertex *Nebula_verts = nullptr;
static int Nebula_n_verts = 0;

static int Nebula_loaded = 0;
static angles Nebula_pbh;
static matrix Nebula_orient;

int Nebula_pitch;
int Nebula_bank;
int Nebula_heading;

void nebula_close()
{
	delete[] Nebula_verts;
	Nebula_verts = nullptr;
	Nebula_n_verts = 0;

	if (!Nebula_loaded)
		return;

	Nebula_loaded = 0;
}

// classic 2d -> sphere projection.  u,v in range 0..1.
static void project_2d_onto_sphere(vec3d *pnt, float u, float v)
{
	float a, x, y, z, s;

	a = PI * (2.0f * u - 1.0f);
	z = 2.0f * v - 1.0f;
	s = fl_sqrt(1.0f - z * z);
	x = s * cosf(a);
	y = s * sinf(a);
	pnt->xyz.x = x;
	pnt->xyz.y = y;
	pnt->xyz.z = z;
}

// --- small, deterministic value-noise field (no general noise utility exists in the engine) ---

static uint32_t neb_hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

static float neb_hash2(int ix, int iy, int seed)
{
	uint32_t h = neb_hash(static_cast<uint32_t>(ix) * 73856093U ^ static_cast<uint32_t>(iy) * 19349663U ^
						   static_cast<uint32_t>(seed) * 83492791U);
	return (h & 0xffffffU) / static_cast<float>(0x1000000);
}

static float neb_smooth(float t)
{
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// value noise that is periodic in x with period xperiod (so the longitude seam matches)
static float neb_vnoise(float x, float y, int seed, int xperiod)
{
	int ix = static_cast<int>(floorf(x));
	int iy = static_cast<int>(floorf(y));
	float fx = x - ix;
	float fy = y - iy;

	auto wrap = [xperiod](int i) {
		if (xperiod > 0) {
			i %= xperiod;
			if (i < 0)
				i += xperiod;
		}
		return i;
	};

	float h00 = neb_hash2(wrap(ix), iy, seed);
	float h10 = neb_hash2(wrap(ix + 1), iy, seed);
	float h01 = neb_hash2(wrap(ix), iy + 1, seed);
	float h11 = neb_hash2(wrap(ix + 1), iy + 1, seed);

	float u = neb_smooth(fx);
	float v = neb_smooth(fy);

	return (h00 * (1.0f - u) + h10 * u) * (1.0f - v) + (h01 * (1.0f - u) + h11 * u) * v;
}

// multi-octave, anisotropic, longitude-seamless fbm in 0..1
static float neb_fbm(float lon, float lat, const old_nebula_pattern &p)
{
	int base = (int)std::lround(p.freq_u);
	if (base < 1)
		base = 1;

	// low-frequency directional warp to stretch the wisps
	float w = (neb_vnoise(lon * base, lat * p.freq_v, p.seed + 777, base) - 0.5f) * p.warp;

	float sum = 0.0f, amp = 0.5f, norm = 0.0f;
	for (int o = 0; o < 4; o++) {
		int per = base << o;
		float n = neb_vnoise((lon + w) * per, lat * p.freq_v * static_cast<float>(1 << o), p.seed + o, per);
		sum += amp * n;
		norm += amp;
		amp *= 0.5f;
	}

	return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

// brightness 0..1 for a vertex: mostly black, a sparse scatter of bright "knots"
static float nebula_brightness(float lon, float lat, const old_nebula_pattern &p)
{
	float n = neb_fbm(lon, lat, p);

	float density = p.density;
	CLAMP(density, 0.0f, 1.0f);
	float thr = 1.0f - density;

	if (n <= thr || density <= 0.0f)
		return 0.0f;

	float b = (n - thr) / (1.0f - thr);
	return powf(b, p.contrast);
}

// build the procedural gouraud sphere into Nebula_verts
static void nebula_generate_mesh(const old_nebula_pattern &p, const old_nebula_color &col, const matrix *orient)
{
	delete[] Nebula_verts;
	Nebula_verts = nullptr;
	Nebula_n_verts = 0;

	int rlon = p.res_lon;
	int rlat = p.res_lat;
	CLAMP(rlon, 2, 200);
	CLAMP(rlat, 1, 100);

	float band_min = p.band_min;
	float band_max = p.band_max;
	CLAMP(band_min, 0.0f, 1.0f);
	CLAMP(band_max, 0.0f, 1.0f);
	if (band_max < band_min)
		std::swap(band_max, band_min);

	int n_grid = (rlon + 1) * (rlat + 1);
	SCP_vector<vec3d> gpt(n_grid);
	SCP_vector<float> gbright(n_grid);

	for (int i = 0; i <= rlon; i++) {
		float u = static_cast<float>(i) / static_cast<float>(rlon); // 0..1 longitude
		for (int j = 0; j <= rlat; j++) {
			float t = static_cast<float>(j) / static_cast<float>(rlat);
			float v = band_min + (band_max - band_min) * t; // sphere v inside the band

			vec3d pt;
			project_2d_onto_sphere(&pt, u, v);
			vm_vec_scale(&pt, NEBULA_RADIUS);
			vm_vec_unrotate(&pt, &pt, orient);

			int gi = i * (rlat + 1) + j;
			gpt[gi] = pt;
			gbright[gi] = nebula_brightness(u, v, p);
		}
	}

	Nebula_n_verts = rlon * rlat * 6;
	Nebula_verts = new vertex[Nebula_n_verts];

	auto set_vert = [&](vertex *vt, int gi) {
		g3_transfer_vertex(vt, &gpt[gi]);
		float b = gbright[gi];
		vt->r = static_cast<ubyte>(col.r * b);
		vt->g = static_cast<ubyte>(col.g * b);
		vt->b = static_cast<ubyte>(col.b * b);
		vt->a = 255;
	};

	int k = 0;
	for (int i = 0; i < rlon; i++) {
		for (int j = 0; j < rlat; j++) {
			int g00 = i * (rlat + 1) + j;
			int g10 = (i + 1) * (rlat + 1) + j;
			int g11 = (i + 1) * (rlat + 1) + (j + 1);
			int g01 = i * (rlat + 1) + (j + 1);

			set_vert(&Nebula_verts[k++], g00);
			set_vert(&Nebula_verts[k++], g10);
			set_vert(&Nebula_verts[k++], g11);

			set_vert(&Nebula_verts[k++], g00);
			set_vert(&Nebula_verts[k++], g11);
			set_vert(&Nebula_verts[k++], g01);
		}
	}

	Assertion(k == Nebula_n_verts, "old nebula mesh vertex count mismatch (%d != %d)", k, Nebula_n_verts);
}

void nebula_init(int index, int pitch, int bank, int heading)
{
	nebula_close();

	Nebula_pbh.p = fl_radians(pitch);
	Nebula_pbh.b = fl_radians(bank);
	Nebula_pbh.h = fl_radians(heading);
	vm_angles_2_matrix(&Nebula_orient, &Nebula_pbh);

	if (index < 0 || index >= static_cast<int>(Old_nebula_patterns.size()))
		return;

	// pick the tint color (fall back to white if the palette index is out of range)
	old_nebula_color col;
	if (Mission_palette >= 0 && Mission_palette < static_cast<int>(Old_nebula_colors.size()))
		col = Old_nebula_colors[Mission_palette];

	nebula_generate_mesh(Old_nebula_patterns[index], col, &Nebula_orient);
	Nebula_loaded = 1;
}

void nebula_render()
{
	if (Nebula_verts == nullptr || Nebula_n_verts <= 0)
		return;

	material mat;
	mat.set_depth_mode(ZBUFFER_TYPE_NONE);
	mat.set_blend_mode(ALPHA_BLEND_ADDITIVE);

	g3_start_instance_matrix(&Eye_position, &vmd_identity_matrix);
	g3_render_primitives_colored(&mat, Nebula_verts, Nebula_n_verts, PRIM_TYPE_TRIS, false);
	g3_done_instance(true);
}

// ----------------------------------------------------------------------------------------------------
// debug-only: load and render a real legacy .neb mesh, for tuning the procedural look against the
// original FS1 nebula files.  NOT used by the normal game path.
// ----------------------------------------------------------------------------------------------------

#define MAX_TRIS 200
#define MAX_POINTS 300

#define NEBULA_FILE_ID NOX("NEBU")
#define NEBULA_MAJOR_VERSION 1

static int load_nebula_sub(const char *filename)
{
	CFILE *fp;
	char id[16];
	int version, major;
	int num_pts = 0;
	int num_tris = 0;
	static vec3d nebula_vecs[MAX_POINTS];
	static int neb_light[MAX_POINTS];
	static int tri[MAX_TRIS][3];

	fp = cfopen(filename, "rb");
	if (!fp)
		return 0;

	cfread(id, 4, 1, fp);
	if (strncmp(id, NEBULA_FILE_ID, 4) != 0) {
		mprintf(("Not a valid nebula file.\n"));
		cfclose(fp);
		return 0;
	}
	cfread(&version, sizeof(int), 1, fp);
	major = version / 100;
	if (major != NEBULA_MAJOR_VERSION) {
		mprintf(("An out of date nebula file.\n"));
		cfclose(fp);
		return 0;
	}

	cfread(&num_pts, sizeof(int), 1, fp);
	cfread(&num_tris, sizeof(int), 1, fp);

	if (num_pts <= 0 || num_pts >= MAX_POINTS || num_tris <= 0 || num_tris >= MAX_TRIS) {
		cfclose(fp);
		return 0;
	}

	for (int i = 0; i < num_pts; i++) {
		float xf, yf;
		int l;
		cfread(&xf, sizeof(float), 1, fp);
		cfread(&yf, sizeof(float), 1, fp);
		cfread(&l, sizeof(int), 1, fp);
		project_2d_onto_sphere(&nebula_vecs[i], 1.0f - xf, yf);
		vm_vec_scale(&nebula_vecs[i], NEBULA_RADIUS);
		neb_light[i] = l;
	}

	for (int i = 0; i < num_tris; i++) {
		cfread(&tri[i][0], sizeof(int), 1, fp);
		cfread(&tri[i][1], sizeof(int), 1, fp);
		cfread(&tri[i][2], sizeof(int), 1, fp);
	}

	cfclose(fp);

	// build a render buffer tinted by the current mission palette, mirroring the procedural path
	nebula_close();

	old_nebula_color col;
	if (Mission_palette >= 0 && Mission_palette < static_cast<int>(Old_nebula_colors.size()))
		col = Old_nebula_colors[Mission_palette];

	Nebula_n_verts = num_tris * 3;
	Nebula_verts = new vertex[Nebula_n_verts];

	int k = 0;
	for (int i = 0; i < num_tris; i++) {
		for (int e = 0; e < 3; e++) {
			int idx = tri[i][e];
			float b = neb_light[idx] / 31.0f;
			vertex *vt = &Nebula_verts[k++];
			g3_transfer_vertex(vt, &nebula_vecs[idx]);
			vt->r = static_cast<ubyte>(col.r * b);
			vt->g = static_cast<ubyte>(col.g * b);
			vt->b = static_cast<ubyte>(col.b * b);
			vt->a = 255;
		}
	}

	Nebula_loaded = 1;
	return 1;
}

DCF(nebula, "Loads a real legacy .neb mesh for reference, or regenerates the procedural nebula")
{
	SCP_string filename;

	if (dc_optional_string_either("help", "--help")) {
		dc_printf("Usage: nebula [filename]\n");
		dc_printf("With a filename (no extension), loads the original FS1 .neb mesh for reference.\n");
		dc_printf("With no argument, restores the current mission's procedural nebula.\n");
		return;
	}

	if (dc_maybe_stuff_string_white(filename)) {
		load_nebula_sub(cf_add_ext(filename.c_str(), NOX(".neb")));
	} else {
		nebula_init(Nebula_index, Nebula_pitch, Nebula_bank, Nebula_heading);
	}
}
