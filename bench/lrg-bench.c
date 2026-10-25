/* lrg-bench.c - Frame-time benchmarks for libregnum and graylib
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Runs a set of CPU-bound rendering/update scenarios with the frame cap
 * off and reports the mean, median and 95th percentile frame time of each.
 * The window is small on purpose so that fill rate (which is the GPU's or
 * llvmpipe's problem, not ours) stays out of the numbers and the cost of
 * the engine's own per-frame work dominates.
 *
 * Usage:
 *   lrg-bench                     run every scenario
 *   lrg-bench -s ecs-sprites      run one scenario (repeatable)
 *   lrg-bench -f 600 -o yaml      600 measured frames, YAML output
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <glib.h>
#include <raylib.h>
#include <graylib.h>
#include <libregnum.h>

#include "gui/lrg-gui-raylib.h"

#define BENCH_WIDTH          (320)
#define BENCH_HEIGHT         (240)
#define BENCH_WARMUP_FRAMES  (30)

/* ==========================================================================
 * Scenario plumbing
 * ========================================================================== */

typedef struct _BenchScenario BenchScenario;

/* setup() returns opaque state, frame() runs one frame's update + draw
 * work (inside begin/end drawing), teardown() frees the state. */
struct _BenchScenario
{
	const gchar *name;
	const gchar *description;
	gpointer   (*setup)    (void);
	void       (*frame)    (gpointer state, guint frame_index);
	void       (*teardown) (gpointer state);
};

static gint    opt_frames = 300;
static gchar **opt_scenarios = NULL;
static gchar  *opt_output = NULL;
static gboolean opt_list = FALSE;
static gboolean opt_license = FALSE;

/* A tiny solid texture shared by the sprite, tilemap and particle cases. */
static GrlTexture *
bench_make_texture (gint size)
{
	g_autoptr(GrlColor) white = NULL;
	g_autoptr(GrlImage) image = NULL;

	white = grl_color_new (255, 255, 255, 255);
	image = grl_image_new_color (size, size, white);
	return grl_texture_new_from_image (image);
}

/* ==========================================================================
 * ecs-sprites: LrgWorld of game objects with sprite components
 * ========================================================================== */

#define ECS_OBJECTS (4000)

typedef struct
{
	LrgWorld   *world;
	GrlTexture *texture;
} EcsState;

static gpointer
ecs_setup (void)
{
	EcsState *st;
	guint     i;

	st = g_new0 (EcsState, 1);
	st->world = lrg_world_new ();
	st->texture = bench_make_texture (4);

	for (i = 0; i < ECS_OBJECTS; i++)
	{
		g_autoptr(LrgGameObject) obj = NULL;
		g_autoptr(LrgSpriteComponent) sprite = NULL;

		obj = lrg_game_object_new_at ((gfloat) (i % 64) * 5.0f,
		                              (gfloat) (i / 64) * 4.0f);
		sprite = lrg_sprite_component_new_with_texture (st->texture);
		lrg_game_object_add_component (obj, LRG_COMPONENT (sprite));
		grl_entity_set_z_index (GRL_ENTITY (obj), (gint) (i % 7));
		lrg_world_add_object (st->world, obj);
	}

	return st;
}

static void
ecs_frame (gpointer state,
           guint    frame_index)
{
	EcsState *st = state;

	lrg_world_update (st->world, 1.0f / 60.0f);
	lrg_world_draw (st->world);
}

static void
ecs_teardown (gpointer state)
{
	EcsState *st = state;

	g_clear_object (&st->world);
	g_clear_object (&st->texture);
	g_free (st);
}

/* ==========================================================================
 * ui-labels: a retained-mode canvas full of labels in a vbox
 * ========================================================================== */

#define UI_LABELS (600)

typedef struct
{
	LrgCanvas *canvas;
	LrgLabel  *counter;
} UiState;

static gpointer
ui_setup (void)
{
	UiState *st;
	LrgVBox *box;
	guint    i;

	st = g_new0 (UiState, 1);
	st->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (st->canvas), BENCH_WIDTH, BENCH_HEIGHT);

	box = lrg_vbox_new ();
	lrg_widget_set_position (LRG_WIDGET (box), 4.0f, 4.0f);
	lrg_widget_set_size (LRG_WIDGET (box), 300.0f, 20000.0f);
	lrg_container_add_child (LRG_CONTAINER (st->canvas), LRG_WIDGET (box));

	for (i = 0; i < UI_LABELS; i++)
	{
		g_autofree gchar *text = NULL;
		LrgLabel         *label;

		text = g_strdup_printf ("Label number %u with some text", i);
		label = lrg_label_new (text);
		lrg_label_set_font_size (label, 10.0f);
		lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (label));
		if (i == 0)
			st->counter = label;
	}

	return st;
}

static void
ui_frame (gpointer state,
          guint    frame_index)
{
	UiState *st = state;
	gchar    buf[64];

	/* One label changes every frame, as a score or FPS readout would. */
	g_snprintf (buf, sizeof buf, "Frame %u", frame_index);
	lrg_label_set_text (st->counter, buf);
	lrg_canvas_render (st->canvas);
}

static void
ui_teardown (gpointer state)
{
	UiState *st = state;

	g_clear_object (&st->canvas);
	g_free (st);
}

/* ==========================================================================
 * particles: a 2D particle system kept saturated by emitters
 * ========================================================================== */

#define PARTICLES_MAX (20000)

typedef struct
{
	LrgParticleSystem *system;
} ParticleState;

static gpointer
particles_setup (void)
{
	ParticleState *st;
	guint          i;

	st = g_new0 (ParticleState, 1);
	st->system = lrg_particle_system_new (PARTICLES_MAX);

	for (i = 0; i < 4; i++)
	{
		g_autoptr(LrgParticleEmitter) emitter = NULL;

		emitter = lrg_particle_emitter_new ();
		lrg_particle_emitter_set_emission_rate (emitter, 4000.0f);
		lrg_particle_emitter_set_initial_lifetime (emitter, 1.2f, 1.8f);
		lrg_particle_emitter_set_initial_speed (emitter, 30.0f, 50.0f);
		lrg_particle_emitter_set_initial_size (emitter, 1.0f, 3.0f);
		lrg_particle_emitter_set_position (emitter,
		                                   80.0f + 50.0f * (gfloat) i,
		                                   120.0f, 0.0f);
		lrg_particle_system_add_emitter (st->system, emitter);
	}
	lrg_particle_system_play (st->system);

	/* Fill the pool before measuring. */
	for (i = 0; i < 120; i++)
		lrg_particle_system_update (st->system, 1.0f / 60.0f);

	return st;
}

static void
particles_frame (gpointer state,
                 guint    frame_index)
{
	ParticleState *st = state;

	lrg_particle_system_update (st->system, 1.0f / 60.0f);
	lrg_particle_system_draw (st->system);
}

static void
particles_teardown (gpointer state)
{
	ParticleState *st = state;

	g_clear_object (&st->system);
	g_free (st);
}

/* ==========================================================================
 * tilemap: a large two-layer map drawn through a 2D camera
 * ========================================================================== */

#define TILEMAP_SIZE (512)

typedef struct
{
	GrlTexture  *texture;
	LrgTileset  *tileset;
	LrgTilemap  *map;
	GrlCamera2D *camera;
} TilemapState;

static gpointer
tilemap_setup (void)
{
	TilemapState *st;
	guint         l;

	st = g_new0 (TilemapState, 1);
	st->texture = bench_make_texture (64);
	st->tileset = lrg_tileset_new (st->texture, 8, 8);
	st->map = lrg_tilemap_new (st->tileset);

	for (l = 0; l < 2; l++)
	{
		LrgTilemapLayer *layer;
		guint            x;
		guint            y;

		layer = lrg_tilemap_layer_new (TILEMAP_SIZE, TILEMAP_SIZE);
		for (y = 0; y < TILEMAP_SIZE; y++)
			for (x = 0; x < TILEMAP_SIZE; x++)
				lrg_tilemap_layer_set_tile (layer, x, y,
				                            1 + ((x + y + l) % 60));
		lrg_tilemap_add_layer (st->map, layer);
		g_object_unref (layer);
	}

	st->camera = grl_camera2d_new ();
	return st;
}

static void
tilemap_frame (gpointer state,
               guint    frame_index)
{
	TilemapState *st = state;

	/* Pan slowly so the visible window moves every frame. */
	grl_camera2d_set_target_xy (st->camera,
	                            (gfloat) (frame_index % 2000),
	                            (gfloat) (frame_index % 1000));
	grl_camera2d_begin (st->camera);
	lrg_tilemap_draw_with_camera (st->map, st->camera);
	grl_camera2d_end (st->camera);
}

static void
tilemap_teardown (gpointer state)
{
	TilemapState *st = state;

	g_clear_object (&st->camera);
	g_clear_object (&st->map);
	g_clear_object (&st->tileset);
	g_clear_object (&st->texture);
	g_free (st);
}

/* ==========================================================================
 * shapes3d: many LrgCube3D drawn through a 3D camera
 * ========================================================================== */

#define SHAPES_COUNT (3000)

typedef struct
{
	GPtrArray   *shapes;
	GrlCamera3D *camera;
} ShapesState;

static gpointer
shapes_setup (void)
{
	ShapesState *st;
	guint        i;

	st = g_new0 (ShapesState, 1);
	st->shapes = g_ptr_array_new_with_free_func (g_object_unref);

	for (i = 0; i < SHAPES_COUNT; i++)
	{
		g_autoptr(GrlColor) color = NULL;

		color = grl_color_new ((guint8) (i * 7), (guint8) (i * 13),
		                       (guint8) (i * 29), 255);
		g_ptr_array_add (st->shapes,
		                 lrg_cube3d_new_full ((gfloat) (i % 60) - 30.0f, 0.0f,
		                                      (gfloat) (i / 60) - 25.0f,
		                                      0.5f, 0.5f, 0.5f, color));
	}

	st->camera = grl_camera3d_new ();
	grl_camera3d_set_position_xyz (st->camera, 0.0f, 30.0f, 40.0f);
	grl_camera3d_set_target_xyz (st->camera, 0.0f, 0.0f, 0.0f);
	return st;
}

static void
shapes_frame (gpointer state,
              guint    frame_index)
{
	ShapesState *st = state;
	guint        i;

	grl_camera3d_begin (st->camera);
	for (i = 0; i < st->shapes->len; i++)
		lrg_drawable_draw (LRG_DRAWABLE (g_ptr_array_index (st->shapes, i)),
		                   1.0f / 60.0f);
	grl_camera3d_end (st->camera);
}

static void
shapes_teardown (gpointer state)
{
	ShapesState *st = state;

	g_clear_object (&st->camera);
	g_ptr_array_unref (st->shapes);
	g_free (st);
}

/* ==========================================================================
 * 3D world scenarios: a camera walking low through a large world, so most
 * of it lies outside the view (as in a streamed open world)
 * ========================================================================== */

#define WORLD_HALF (120.0f)

/* bench_world_camera: a camera circling the world's middle at eye height,
 * looking along its path; *eye gets its position */
static void
bench_world_camera (GrlCamera3D *camera,
                    guint        frame_index,
                    gfloat      *eye)
{
	gfloat a = (gfloat) frame_index * 0.01f;

	eye[0] = cosf (a) * 40.0f;
	eye[1] = 6.0f;
	eye[2] = sinf (a) * 40.0f;
	grl_camera3d_set_position_xyz (camera, eye[0], eye[1], eye[2]);
	grl_camera3d_set_target_xyz (camera, eye[0] - sinf (a) * 10.0f, 4.0f,
	                             eye[2] + cosf (a) * 10.0f);
}

/* bench_box: a unit box (8 corners, 12 triangles) scaled by @s */
static void
bench_box (gfloat  *pos,
           gfloat  *nrm,
           guint32 *idx,
           gfloat   s)
{
	static const gfloat  c[8][3] = {
		{ -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
		{ -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };
	static const guint32 t[36] = {
		0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
		2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3 };
	guint i;

	for (i = 0; i < 8; i++)
	{
		pos[i * 3 + 0] = c[i][0] * s;
		pos[i * 3 + 1] = c[i][1] * s;
		pos[i * 3 + 2] = c[i][2] * s;
		nrm[i * 3 + 0] = c[i][0] * 0.577f;
		nrm[i * 3 + 1] = c[i][1] * 0.577f;
		nrm[i * 3 + 2] = c[i][2] * 0.577f;
	}
	memcpy (idx, t, sizeof t);
}

/* bench_static_batch: 3600 boxes over the world, added tile by tile (4 x
 * 10 boxes, one chunk each per material, as a streamed world adds them) */
static LrgStaticBatch *
bench_static_batch (void)
{
	LrgStaticBatch *batch;
	gfloat          pos[24];
	gfloat          nrm[24];
	guint32         idx[36];
	guint           n;

	batch = lrg_static_batch_new ();
	lrg_static_batch_set_max_chunk_vertices (batch, 8 * 40);
	bench_box (pos, nrm, idx, 1.0f);
	for (n = 0; n < 3600; n++)
	{
		guint     tile = n / 40;
		guint     i = (tile % 15) * 4 + (n % 40) % 4;
		guint     j = (tile / 15) * 10 + (n % 40) / 4;
		GrlMatrix m;

		memset (&m, 0, sizeof m);
		m.m0 = 1.0f;
		m.m5 = 1.0f + (gfloat) ((i * 7 + j) % 5) * 0.5f;
		m.m10 = 1.0f;
		m.m15 = 1.0f;
		m.m12 = (gfloat) i * 4.0f - WORLD_HALF;
		m.m13 = m.m5;
		m.m14 = (gfloat) j * 4.0f - WORLD_HALF;
		lrg_static_batch_add_mesh (batch, pos, nrm, NULL, NULL, 8, idx, 36, &m,
		                           tile % 3, 0, NULL);
	}
	lrg_static_batch_upload (batch, NULL);
	return batch;
}

/* bench_terrain: 16 x 16 chunks of 16 x 16 cells, rolling hills */
static LrgTerrain *
bench_terrain (void)
{
	LrgTerrain *terrain;
	gint        i;
	gint        j;

	terrain = lrg_terrain_new (16, 16.0f);
	for (i = -8; i < 8; i++)
	{
		for (j = -8; j < 8; j++)
		{
			gfloat heights[17 * 17];
			guint  k;

			for (k = 0; k < 17 * 17; k++)
			{
				gfloat x = (gfloat) i * 16.0f + (gfloat) (k % 17);
				gfloat z = (gfloat) j * 16.0f + (gfloat) (k / 17);

				heights[k] = 2.0f * sinf (x * 0.13f) * cosf (z * 0.11f) - 1.0f;
			}
			lrg_terrain_set_chunk (terrain, i, j, heights, 17 * 17, NULL, NULL, NULL);
		}
	}
	lrg_terrain_upload (terrain, NULL);
	return terrain;
}

/* bench_billboards: 60000 swaying, alpha-tested billboards, added in
 * 16 x 16 tiles (as a streamed landscape adds them) */
static LrgBillboardBatch *
bench_billboards (GrlTexture *texture)
{
	LrgBillboardBatch *batch;
	guint              i;

	batch = lrg_billboard_batch_new ();
	lrg_billboard_batch_set_material_texture (batch, 0, texture);
	lrg_billboard_batch_set_wind (batch, 0.8f, 0.6f, 0.08f, 0.05f, 1.3f);
	for (i = 0; i < 60000; i++)
	{
		g_autoptr(GrlVector3) p = NULL;
		guint                 id;

		p = grl_vector3_new ((gfloat) ((i / 256) % 15 * 16 + i % 16) - WORLD_HALF, 0.0f,
		                     (gfloat) ((i / 256) / 15 * 16 + (i % 256) / 16) * 0.96f
		                     - WORLD_HALF);
		id = lrg_billboard_batch_add (batch, 0, p, 1.0f, 2.0f);
		lrg_billboard_batch_set_sway (batch, id, 0.5f);
	}
	return batch;
}

typedef struct
{
	GrlCamera3D       *camera;
	GrlTexture        *texture;
	LrgStaticBatch    *batch;
	LrgTerrain        *terrain;
	LrgWaterSurface   *water;
	LrgBillboardBatch *billboards;
	LrgGrassField     *grass;
	LrgLightSet       *lights;
	LrgShadowRenderer *shadows;
} WorldState;

static WorldState *
world_state_new (void)
{
	WorldState *st;

	st = g_new0 (WorldState, 1);
	st->camera = grl_camera3d_new ();
	return st;
}

static void
world_teardown (gpointer state)
{
	WorldState *st = state;

	if (st->grass != NULL)
		lrg_grass_field_release_gpu (st->grass);
	if (st->water != NULL)
		lrg_water_surface_release_gpu (st->water);
	if (st->shadows != NULL)
		lrg_shadow_renderer_release_gpu (st->shadows);
	g_clear_object (&st->grass);
	g_clear_object (&st->water);
	g_clear_object (&st->billboards);
	g_clear_object (&st->terrain);
	g_clear_object (&st->batch);
	g_clear_object (&st->shadows);
	g_clear_object (&st->lights);
	g_clear_object (&st->texture);
	g_clear_object (&st->camera);
	g_free (st);
}

/* --- static-batch: 3600 boxes in 450 chunks */

static gpointer
static_batch_setup (void)
{
	WorldState *st = world_state_new ();

	st->batch = bench_static_batch ();
	return st;
}

static void
static_batch_frame (gpointer state,
                    guint    frame_index)
{
	WorldState *st = state;
	gfloat      eye[3];

	bench_world_camera (st->camera, frame_index, eye);
	grl_camera3d_begin (st->camera);
	lrg_static_batch_draw (st->batch, LRG_STATIC_BATCH_ALL_LAYERS);
	grl_camera3d_end (st->camera);
}

/* --- terrain: 256 chunks, plus 256 water patches */

static gpointer
terrain_setup (void)
{
	WorldState *st = world_state_new ();
	gint        i;
	gint        j;

	st->terrain = bench_terrain ();
	st->water = lrg_water_surface_new ();
	for (i = -8; i < 8; i++)
		for (j = -8; j < 8; j++)
			lrg_water_surface_add_grid (st->water, (gfloat) i * 16.0f, (gfloat) j * 16.0f,
			                            (gfloat) i * 16.0f + 16.0f, (gfloat) j * 16.0f + 16.0f,
			                            -0.5f, 16, 16, NULL, NULL, NULL);
	return st;
}

static void
terrain_frame (gpointer state,
               guint    frame_index)
{
	WorldState *st = state;
	gfloat      eye[3];

	bench_world_camera (st->camera, frame_index, eye);
	grl_camera3d_begin (st->camera);
	lrg_terrain_draw (st->terrain);
	lrg_water_surface_draw (st->water, eye, (gfloat) frame_index / 60.0f);
	grl_camera3d_end (st->camera);
}

/* --- billboards: 60000 settled (GPU) billboards */

static gpointer
billboards_setup (void)
{
	WorldState *st = world_state_new ();

	st->texture = bench_make_texture (8);
	st->billboards = bench_billboards (st->texture);
	return st;
}

static void
billboards_frame (gpointer state,
                  guint    frame_index)
{
	WorldState           *st = state;
	gfloat                eye[3];
	g_autoptr(GrlVector3) camera = NULL;

	bench_world_camera (st->camera, frame_index, eye);
	camera = grl_vector3_new (eye[0], eye[1], eye[2]);
	grl_camera3d_begin (st->camera);
	lrg_billboard_batch_set_wind_time (st->billboards, (gdouble) frame_index / 60.0);
	lrg_billboard_batch_draw (st->billboards, camera);
	grl_camera3d_end (st->camera);
}

/* --- grass: ~150000 blades over the world */

static gpointer
grass_setup (void)
{
	WorldState       *st = world_state_new ();
	g_autoptr(GArray) blades = NULL;
	gint              i;
	gint              j;

	st->grass = lrg_grass_field_new (7);
	blades = g_array_new (FALSE, FALSE, sizeof (LrgGrassBlade));
	for (i = -60; i < 60; i++)
		for (j = -60; j < 60; j++)
			lrg_grass_field_place_cell (st->grass, i, j, 0.5f, 0.0, 0.0, NULL, NULL, blades);
	lrg_grass_field_set_blades (st->grass, (const LrgGrassBlade *) (gconstpointer) blades->data,
	                            blades->len);
	return st;
}

static void
grass_frame (gpointer state,
             guint    frame_index)
{
	WorldState *st = state;
	gfloat      eye[3];

	bench_world_camera (st->camera, frame_index, eye);
	grl_camera3d_begin (st->camera);
	lrg_grass_field_draw (st->grass, eye, (gfloat) frame_index / 60.0f);
	grl_camera3d_end (st->camera);
}

/* --- world-shadows: the lit world with a 2-cascade sun and 2 point light
 * cubes (14 shadow passes), a 16 x 16 light grid and 64 point lights */

static gpointer
world_shadows_setup (void)
{
	WorldState          *st = world_state_new ();
	g_autoptr(GrlColor)  ambient = NULL;
	g_autoptr(GrlColor)  sun = NULL;
	g_autoptr(GrlVector3) direction = NULL;
	guint                i;

	st->batch = bench_static_batch ();
	st->terrain = bench_terrain ();
	st->texture = bench_make_texture (8);
	st->billboards = bench_billboards (st->texture);
	st->lights = lrg_light_set_new ();
	ambient = grl_color_new (60, 60, 70, 255);
	sun = grl_color_new (230, 220, 200, 255);
	direction = grl_vector3_new (-0.4f, -1.0f, -0.3f);
	lrg_light_set_set_ambient (st->lights, ambient);
	lrg_light_set_set_directional (st->lights, direction, sun);
	for (i = 0; i < 64; i++)
	{
		g_autoptr(GrlColor) color = NULL;
		guint               id;

		color = grl_color_new ((guint8) (i * 37), (guint8) (i * 59), 200, 255);
		id = lrg_light_set_add_point_light (st->lights, (gdouble) (i % 8) * 30.0 - 105.0, 3.0,
		                                    (gdouble) (i / 8) * 30.0 - 105.0, color, 14.0f);
		if (i == 27 || i == 36)
			lrg_light_set_set_point_light_shadow (st->lights, id, 1);
	}
	lrg_light_set_set_grid (st->lights, 8.0f, 16);
	st->shadows = lrg_shadow_renderer_new ();
	lrg_shadow_renderer_set_sun (st->shadows, 1024, 2, 80.0f, 0.5f);
	lrg_shadow_renderer_set_points (st->shadows, 256, 2, 0.0f);
	return st;
}

static void
world_shadows_frame (gpointer state,
                     guint    frame_index)
{
	WorldState           *st = state;
	gfloat                eye[3];
	gfloat                forward[3];
	gfloat                up[3] = { 0.0f, 1.0f, 0.0f };
	gfloat                a = (gfloat) frame_index * 0.01f;
	guint                 n_passes;
	guint                 p;
	GrlShader            *caster;
	g_autoptr(GrlVector3) camera = NULL;

	bench_world_camera (st->camera, frame_index, eye);
	camera = grl_vector3_new (eye[0], eye[1], eye[2]);
	forward[0] = -sinf (a);
	forward[1] = -0.196f;
	forward[2] = cosf (a);

	/* the shadow passes first (they leave the screen's 2D state behind) */
	lrg_light_set_set_shadow_renderer (st->lights, NULL);
	n_passes = lrg_shadow_renderer_update (st->shadows, st->lights, eye, forward, up,
	                                       45.0f, (gfloat) BENCH_WIDTH / BENCH_HEIGHT, 0.1f);
	caster = lrg_shadow_renderer_get_caster_shader (st->shadows);
	for (p = 0; p < n_passes; p++)
	{
		if (!lrg_shadow_renderer_begin_pass (st->shadows, p))
			continue;
		lrg_static_batch_set_shader (st->batch, caster);
		lrg_static_batch_draw (st->batch, LRG_STATIC_BATCH_ALL_LAYERS);
		lrg_terrain_set_shader (st->terrain, caster);
		lrg_terrain_draw (st->terrain);
		lrg_billboard_batch_draw_shadow (st->billboards, st->shadows, camera);
		lrg_shadow_renderer_end_pass (st->shadows);
	}
	lrg_light_set_set_shadow_renderer (st->lights, st->shadows);

	grl_camera3d_begin (st->camera);
	lrg_light_set_apply (st->lights, eye[0], eye[1], eye[2]);
	lrg_light_set_bind (st->lights);
	lrg_static_batch_set_shader (st->batch, lrg_light_set_get_shader (st->lights));
	lrg_terrain_set_shader (st->terrain, lrg_light_set_get_shader (st->lights));
	lrg_static_batch_draw (st->batch, LRG_STATIC_BATCH_ALL_LAYERS);
	lrg_terrain_draw (st->terrain);
	lrg_billboard_batch_set_wind_time (st->billboards, (gdouble) frame_index / 60.0);
	lrg_billboard_batch_draw (st->billboards, camera);
	grl_camera3d_end (st->camera);
	lrg_light_set_unbind ();
}

/* --- light-grid: lrg_light_set_apply() of a still camera, 256 lights in a
 * 32 x 32 grid of 4 layers (the per-frame cost of the lit shader's lists) */

static gpointer
light_grid_setup (void)
{
	WorldState *st = world_state_new ();
	guint       i;

	st->lights = lrg_light_set_new ();
	for (i = 0; i < 256; i++)
	{
		g_autoptr(GrlColor) color = NULL;

		color = grl_color_new ((guint8) (i * 37), (guint8) (i * 59), 200, 255);
		lrg_light_set_add_point_light (st->lights, (gdouble) (i % 16) * 8.0 - 64.0,
		                               (gdouble) (i % 3), (gdouble) (i / 16) * 8.0 - 64.0,
		                               color, 10.0f);
	}
	lrg_light_set_set_grid (st->lights, 4.0f, 32);
	lrg_light_set_set_grid_layers (st->lights, 4, 4.0f);
	return st;
}

static void
light_grid_frame (gpointer state,
                  guint    frame_index)
{
	WorldState *st = state;

	lrg_light_set_apply (st->lights, 1.0, 2.0, 3.0);
}

/* --- shapes-round: 600 spheres, 300 cylinders and 300 cones */

#define ROUND_SHAPES (1200)

static gpointer
shapes_round_setup (void)
{
	ShapesState *st;
	guint        i;

	st = g_new0 (ShapesState, 1);
	st->shapes = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < ROUND_SHAPES; i++)
	{
		g_autoptr(GrlColor) color = NULL;
		gfloat              x = (gfloat) (i % 40) - 20.0f;
		gfloat              z = (gfloat) (i / 40) - 15.0f;

		color = grl_color_new ((guint8) (i * 7), (guint8) (i * 13), (guint8) (i * 29), 255);
		if (i % 2 == 0)
			g_ptr_array_add (st->shapes, lrg_sphere3d_new_full (x, 0.0f, z, 0.4f, color));
		else if (i % 4 == 1)
			g_ptr_array_add (st->shapes,
			                 lrg_cylinder3d_new_full (x, 0.0f, z, 0.3f, 0.8f, 32, color));
		else
			g_ptr_array_add (st->shapes,
			                 lrg_cone3d_new_full (x, 0.0f, z, 0.4f, 0.0f, 0.8f, 32, color));
	}

	st->camera = grl_camera3d_new ();
	grl_camera3d_set_position_xyz (st->camera, 0.0f, 20.0f, 30.0f);
	grl_camera3d_set_target_xyz (st->camera, 0.0f, 0.0f, 0.0f);
	return st;
}

/* ==========================================================================
 * graylib-draw: raw graylib 2D draw calls with boxed colours and rects
 * ========================================================================== */

#define GRL_DRAW_CALLS (20000)

static gpointer
grl_draw_setup (void)
{
	return bench_make_texture (8);
}

static void
grl_draw_frame (gpointer state,
                guint    frame_index)
{
	GrlTexture *texture = state;
	guint       i;

	for (i = 0; i < GRL_DRAW_CALLS; i++)
	{
		GrlColor     color = { (guint8) i, (guint8) (i >> 3), 200, 255 };
		GrlRectangle src = { 0.0f, 0.0f, 8.0f, 8.0f };
		GrlRectangle dst = { (gfloat) (i % 300), (gfloat) ((i / 300) % 220),
		                     4.0f, 4.0f };
		GrlVector2   origin = { 0.0f, 0.0f };

		if ((i & 1) == 0)
			grl_draw_rectangle ((gint) (i % 300), (gint) ((i / 300) % 220),
			                    3, 3, &color);
		else
			grl_draw_texture_pro (texture, &src, &dst, &origin, 0.0f, &color);
	}
	for (i = 0; i < 200; i++)
	{
		GrlColor color = { 255, 255, 255, 255 };

		grl_draw_text ("graylib text", (gint) (i % 10) * 30,
		               (gint) (i / 10) * 11, 10, &color);
	}
}

static void
grl_draw_teardown (gpointer state)
{
	g_object_unref (state);
}

/* ==========================================================================
 * gui: the editor's immediate-mode GUI, a busy property panel
 * ========================================================================== */

typedef struct
{
	LrgGuiContext *ctx;
	LrgGuiRaylib  *renderer;
	gfloat         values[64];
	gboolean       flags[64];
} GuiState;

static gpointer
gui_setup (void)
{
	GuiState *st;

	st = g_new0 (GuiState, 1);
	st->ctx = lrg_gui_context_new ();
	st->renderer = lrg_gui_raylib_new ();
	lrg_gui_raylib_attach (st->renderer, st->ctx);
	return st;
}

static void
gui_frame (gpointer state,
           guint    frame_index)
{
	GuiState   *st = state;
	LrgGuiInput input;
	LrgGuiRect  rect = { 0.0f, 0.0f, BENCH_WIDTH, BENCH_HEIGHT };
	guint       i;

	lrg_gui_input_init (&input);
	lrg_gui_context_begin_frame (st->ctx, &input, BENCH_WIDTH, BENCH_HEIGHT);
	lrg_gui_begin_region (st->ctx, "panel", &rect, 0);
	for (i = 0; i < 64; i++)
	{
		gchar label[32];

		lrg_gui_push_id_int (st->ctx, (gint) i);
		g_snprintf (label, sizeof label, "Property %u", i);
		if (lrg_gui_tree_node (st->ctx, label, 0))
			lrg_gui_tree_pop (st->ctx);
		lrg_gui_slider_float (st->ctx, "value", &st->values[i],
		                      0.0f, 1.0f, "%.3f");
		lrg_gui_checkbox (st->ctx, "enabled", &st->flags[i]);
		lrg_gui_textf (st->ctx, "frame %u item %u", frame_index, i);
		lrg_gui_button (st->ctx, "Apply");
		lrg_gui_pop_id (st->ctx);
	}
	lrg_gui_end_region (st->ctx);
	lrg_gui_context_end_frame (st->ctx);
	lrg_gui_raylib_render (st->renderer, lrg_gui_context_get_draw_list (st->ctx));
}

static void
gui_teardown (gpointer state)
{
	GuiState *st = state;

	g_clear_object (&st->renderer);
	g_clear_object (&st->ctx);
	g_free (st);
}

/* ==========================================================================
 * Driver
 * ========================================================================== */

static const BenchScenario scenarios[] = {
	{ "ecs-sprites", "4000 game objects with sprite components (world update + draw)",
	  ecs_setup, ecs_frame, ecs_teardown },
	{ "ui-labels", "retained UI canvas with 600 labels, one changing per frame",
	  ui_setup, ui_frame, ui_teardown },
	{ "particles", "20000-particle 2D system with 4 emitters",
	  particles_setup, particles_frame, particles_teardown },
	{ "tilemap", "512x512 two-layer tilemap drawn through a panning camera",
	  tilemap_setup, tilemap_frame, tilemap_teardown },
	{ "shapes3d", "3000 LrgCube3D shapes in a 3D camera",
	  shapes_setup, shapes_frame, shapes_teardown },
	{ "shapes-round", "600 spheres, 300 cylinders, 300 cones in a 3D camera",
	  shapes_round_setup, shapes_frame, shapes_teardown },
	{ "static-batch", "3600 boxes in 90 static batch chunks (tiles), walking camera",
	  static_batch_setup, static_batch_frame, world_teardown },
	{ "terrain", "256 terrain chunks and 256 water patches, walking camera",
	  terrain_setup, terrain_frame, world_teardown },
	{ "billboards", "60000 settled swaying billboards, walking camera",
	  billboards_setup, billboards_frame, world_teardown },
	{ "grass", "~150000 grass blades, walking camera",
	  grass_setup, grass_frame, world_teardown },
	{ "world-shadows", "lit batch + terrain + billboards, 14 shadow passes, light grid",
	  world_shadows_setup, world_shadows_frame, world_teardown },
	{ "light-grid", "light set apply, 256 lights in a 32x32x4 grid, still camera",
	  light_grid_setup, light_grid_frame, world_teardown },
	{ "graylib-draw", "20000 graylib rectangle/texture draws + 200 texts",
	  grl_draw_setup, grl_draw_frame, grl_draw_teardown },
	{ "gui", "immediate-mode GUI panel, 64 rows of widgets",
	  gui_setup, gui_frame, gui_teardown },
};

static gint
compare_double (gconstpointer a,
                gconstpointer b)
{
	gdouble da = *(const gdouble *) a;
	gdouble db = *(const gdouble *) b;

	return (da > db) - (da < db);
}

static gboolean
scenario_selected (const gchar *name)
{
	guint i;

	if (opt_scenarios == NULL)
		return TRUE;
	for (i = 0; opt_scenarios[i] != NULL; i++)
		if (g_strcmp0 (opt_scenarios[i], name) == 0)
			return TRUE;
	return FALSE;
}

/* Runs one scenario: warm up, then time opt_frames whole frames (begin
 * drawing, scenario work, end drawing including the buffer swap). */
static void
run_scenario (GrlWindow           *window,
              const BenchScenario *sc,
              gboolean             yaml)
{
	g_autofree gdouble *samples = NULL;
	gpointer            state;
	gdouble             total;
	guint               i;
	guint               n;
	g_autoptr(GrlColor) bg = NULL;

	n = (guint) opt_frames;
	samples = g_new0 (gdouble, n);
	bg = grl_color_new (20, 20, 30, 255);
	state = sc->setup ();

	total = 0.0;
	for (i = 0; i < n + BENCH_WARMUP_FRAMES; i++)
	{
		gint64 t0;
		gint64 t1;

		t0 = g_get_monotonic_time ();
		grl_window_begin_drawing (window);
		grl_draw_clear_background (bg);
		sc->frame (state, i);
		grl_window_end_drawing (window);
		t1 = g_get_monotonic_time ();

		if (i >= BENCH_WARMUP_FRAMES)
		{
			samples[i - BENCH_WARMUP_FRAMES] = (gdouble) (t1 - t0) / 1000.0;
			total += samples[i - BENCH_WARMUP_FRAMES];
		}
	}

	sc->teardown (state);

	qsort (samples, n, sizeof (gdouble), compare_double);
	if (yaml)
		g_print ("- scenario: %s\n  frames: %u\n  mean_ms: %.3f\n"
		         "  median_ms: %.3f\n  p95_ms: %.3f\n  fps: %.1f\n",
		         sc->name, n, total / n, samples[n / 2],
		         samples[(n * 95) / 100], 1000.0 / (total / n));
	else
		g_print ("%-14s mean %8.3f ms  median %8.3f ms  p95 %8.3f ms  %8.1f fps\n",
		         sc->name, total / n, samples[n / 2],
		         samples[(n * 95) / 100], 1000.0 / (total / n));
}

static const gchar *license_text =
	"lrg-bench - frame-time benchmarks for libregnum\n"
	"Copyright (C) 2026 Zach Podbielniak\n\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n";

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext) octx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(GrlWindow)      window = NULL;
	gboolean                  yaml;
	guint                     i;
	GOptionEntry              entries[] = {
		{ "scenario", 's', 0, G_OPTION_ARG_STRING_ARRAY, &opt_scenarios,
		  "Run only this scenario (repeatable)", "NAME" },
		{ "frames", 'f', 0, G_OPTION_ARG_INT, &opt_frames,
		  "Measured frames per scenario (default 300)", "N" },
		{ "output", 'o', 0, G_OPTION_ARG_STRING, &opt_output,
		  "Output format: text (default) or yaml", "FORMAT" },
		{ "list", 'l', 0, G_OPTION_ARG_NONE, &opt_list,
		  "List scenarios and exit", NULL },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license,
		  "Show the license and exit", NULL },
		{ NULL }
	};

	octx = g_option_context_new ("- libregnum frame-time benchmarks");
	g_option_context_add_main_entries (octx, entries, NULL);
	g_option_context_set_description (octx,
		"Examples:\n"
		"  lrg-bench                       run every scenario\n"
		"  lrg-bench -s ecs-sprites -s gui run two scenarios\n"
		"  lrg-bench -f 1000 -o yaml       longer run, YAML results\n"
		"\nRuns uncapped in a small window; needs a display (make run-bench\n"
		"starts Xvfb when DISPLAY is unset).\n");
	if (!g_option_context_parse (octx, &argc, &argv, &error))
	{
		g_printerr ("lrg-bench: %s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("%s", license_text);
		return 0;
	}
	if (opt_list)
	{
		for (i = 0; i < G_N_ELEMENTS (scenarios); i++)
			g_print ("%-14s %s\n", scenarios[i].name, scenarios[i].description);
		return 0;
	}
	if (opt_frames < 10)
		opt_frames = 10;
	yaml = g_strcmp0 (opt_output, "yaml") == 0;

	SetTraceLogLevel (LOG_WARNING);
	window = grl_window_new (BENCH_WIDTH, BENCH_HEIGHT, "lrg-bench");
	grl_window_set_target_fps (window, 0);

	for (i = 0; i < G_N_ELEMENTS (scenarios); i++)
		if (scenario_selected (scenarios[i].name))
			run_scenario (window, &scenarios[i], yaml);

	return 0;
}
