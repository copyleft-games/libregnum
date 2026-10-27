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
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <raylib.h>
#include <graylib.h>
#include <libregnum.h>

#include "gui/lrg-gui-raylib.h"
#include "../tests/lrg-test-glb.h"
#include "../tests/lrg-test-midi.h"

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
static gint    opt_fps = 0;
static gboolean opt_no_pin = FALSE;
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
 * ecs-moving: ecs-sprites with every object moving in 60 Hz fixed steps,
 * drawn at 144 Hz with render interpolation (ecs-moving-raw: without)
 * ========================================================================== */

#define MOVING_STEP  (1.0 / 60.0)
#define MOVING_FRAME (1.0 / 144.0)

typedef struct
{
	EcsState        ecs;
	LrgGameObject **objects;     /* borrowed: the world holds the references */
	gdouble         accumulator;
	gboolean        interpolate;
} MovingState;

static gpointer
moving_setup_common (gboolean interpolate)
{
	MovingState *st;
	EcsState    *ecs;
	GList       *objects;
	GList       *l;
	guint        i;

	ecs = ecs_setup ();
	st = g_new0 (MovingState, 1);
	st->ecs = *ecs;
	g_free (ecs);
	st->interpolate = interpolate;
	st->objects = g_new0 (LrgGameObject *, ECS_OBJECTS);

	objects = lrg_world_get_objects (st->ecs.world);
	for (l = objects, i = 0; l != NULL && i < ECS_OBJECTS; l = l->next, i++)
		st->objects[i] = l->data;
	g_list_free (objects);

	/* What LrgGameTemplate does: the clock is on with a fixed timestep
	 * and interpolation, off otherwise */
	grl_interpolation_set_enabled (interpolate);
	return st;
}

static gpointer
moving_setup (void)
{
	return moving_setup_common (TRUE);
}

static gpointer
moving_raw_setup (void)
{
	return moving_setup_common (FALSE);
}

static void
moving_frame (gpointer state,
              guint    frame_index)
{
	MovingState *st = state;
	guint        i;

	/* A 144 Hz frame: 0 or 1 fixed steps, each moving every object (a
	 * slow circle, so they stay on screen), as LrgGameTemplate runs them */
	st->accumulator += MOVING_FRAME;
	while (st->accumulator >= MOVING_STEP)
	{
		grl_interpolation_begin_step ();
		for (i = 0; i < ECS_OBJECTS; i++)
		{
			gfloat phase = (gfloat) (i + frame_index) * 0.01f;

			grl_entity_move_xy (GRL_ENTITY (st->objects[i]),
			                    cosf (phase) * 30.0f, sinf (phase) * 30.0f,
			                    (gfloat) MOVING_STEP);
		}
		grl_interpolation_end_step ();
		st->accumulator -= MOVING_STEP;
	}
	grl_interpolation_set_alpha ((gfloat) (st->accumulator / MOVING_STEP));

	/* Sprite components draw at the render pose */
	lrg_world_draw (st->ecs.world);
}

static void
moving_teardown (gpointer state)
{
	MovingState *st = state;

	grl_interpolation_set_enabled (FALSE);
	g_clear_object (&st->ecs.world);
	g_clear_object (&st->ecs.texture);
	g_free (st->objects);
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

	/* Shapes first, then textures: alternating the two would make raylib
	 * flush its batch on every call, which measures the GL driver rather
	 * than graylib's wrappers. */
	for (i = 0; i < GRL_DRAW_CALLS / 2; i++)
	{
		GrlColor color = { (guint8) i, (guint8) (i >> 3), 200, 255 };

		grl_draw_rectangle ((gint) (i % 300), (gint) ((i / 300) % 220),
		                    3, 3, &color);
	}
	for (i = 0; i < GRL_DRAW_CALLS / 2; i++)
	{
		GrlColor     color = { (guint8) i, (guint8) (i >> 3), 200, 255 };
		GrlRectangle src = { 0.0f, 0.0f, 8.0f, 8.0f };
		GrlRectangle dst = { (gfloat) (i % 300), (gfloat) ((i / 300) % 220),
		                     4.0f, 4.0f };
		GrlVector2   origin = { 0.0f, 0.0f };

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
 * Simulation scenarios: CPU-only per-frame work (nothing is drawn). The
 * begin/end drawing around frame() is a constant, tiny cost here.
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * animator: skeletal animators on 60-bone skeletons, crossfading
 * -------------------------------------------------------------------------- */

#define ANIM_ACTORS     (16)
#define ANIM_BONES      (60)
#define ANIM_KEYFRAMES  (24)

typedef struct
{
	LrgAnimator *animators[ANIM_ACTORS];
} AnimState;

/* A clip with one track per bone and ANIM_KEYFRAMES keys over one second. */
static LrgAnimationClip *
anim_make_clip (const gchar *name,
                gfloat       phase)
{
	LrgAnimationClip *clip;
	guint             b;
	guint             k;

	clip = lrg_animation_clip_new (name);
	lrg_animation_clip_set_loop_mode (clip, LRG_ANIMATION_LOOP_REPEAT);
	for (b = 0; b < ANIM_BONES; b++)
	{
		g_autofree gchar *bone_name = g_strdup_printf ("bone%02u", b);
		guint             track;

		track = lrg_animation_clip_add_track (clip, bone_name);
		for (k = 0; k < ANIM_KEYFRAMES; k++)
		{
			LrgAnimationKeyframe kf;
			gfloat               t;

			memset (&kf, 0, sizeof kf);
			t = (gfloat) k / (gfloat) (ANIM_KEYFRAMES - 1);
			kf.time = t;
			lrg_bone_pose_set_identity (&kf.pose);
			lrg_bone_pose_set_position (&kf.pose, 0.0f, 1.0f, 0.0f);
			lrg_bone_pose_set_rotation_euler (&kf.pose,
			                                  sinf ((t + phase) * 6.28f) * 0.3f,
			                                  0.1f * (gfloat) b, 0.0f);
			lrg_animation_clip_add_keyframe (clip, track, &kf);
		}
	}
	return clip;
}

static gpointer
anim_setup (void)
{
	AnimState *st;
	g_autoptr(LrgAnimationClip) walk = NULL;
	g_autoptr(LrgAnimationClip) run = NULL;
	guint      a;
	guint      b;

	st = g_new0 (AnimState, 1);
	walk = anim_make_clip ("walk", 0.0f);
	run = anim_make_clip ("run", 0.5f);
	for (a = 0; a < ANIM_ACTORS; a++)
	{
		g_autoptr(LrgSkeleton) skeleton = lrg_skeleton_new ();

		/* A tree three bones wide, children listed after parents. */
		for (b = 0; b < ANIM_BONES; b++)
		{
			g_autofree gchar *bone_name = g_strdup_printf ("bone%02u", b);
			g_autoptr(LrgBone) bone = lrg_bone_new (bone_name, (gint) b);

			lrg_bone_set_parent_index (bone, b == 0 ? -1 : (gint) ((b - 1) / 3));
			lrg_skeleton_add_bone (skeleton, bone);
		}
		st->animators[a] = lrg_animator_new (skeleton);
		lrg_animator_add_clip (st->animators[a], "walk", walk);
		lrg_animator_add_clip (st->animators[a], "run", run);
		lrg_animator_play (st->animators[a], "walk");
	}
	return st;
}

static void
anim_frame (gpointer state,
            guint    frame_index)
{
	AnimState *st = state;
	guint      a;

	for (a = 0; a < ANIM_ACTORS; a++)
	{
		/* Crossfade every second so half the frames blend two clips. */
		if ((frame_index + a) % 60 == 0)
			lrg_animator_crossfade (st->animators[a],
			                        ((frame_index / 60) % 2) ? "walk" : "run",
			                        0.5f);
		lrg_animator_update (st->animators[a], 1.0f / 60.0f);
	}
}

static void
anim_teardown (gpointer state)
{
	AnimState *st = state;
	guint      a;

	for (a = 0; a < ANIM_ACTORS; a++)
		g_clear_object (&st->animators[a]);
	g_free (st);
}

/* --------------------------------------------------------------------------
 * skinned-models: 50 actors sharing one skinned glTF model, each playing
 * its own clip and crossfading every two seconds; skinned on the GPU, or
 * on the CPU (skinned-models-cpu, the path every model took before)
 * -------------------------------------------------------------------------- */

#define SKIN_ACTORS (50)
#define SKIN_CLIPS  (4)
#define SKIN_RINGS  (96)
#define SKIN_SIDES  (40)
#define SKIN_BONES  (32)

typedef struct
{
	gchar             *path;
	LrgAssetManager   *manager;
	LrgModelAnimator  *animator;
	GrlCamera3D       *camera;
	LrgModelAnimState  actors[SKIN_ACTORS];
} SkinState;

static gpointer
skin_setup_mode (gboolean gpu)
{
	static const gchar *const clips[SKIN_CLIPS] = { "Curl", "Sway", "Twist", "Wave" };
	g_autoptr(GError) error = NULL;
	SkinState        *st;
	GrlModel         *model;
	GPtrArray        *animations;
	guint             a;

	/* A tube of 3880 vertices on a 32-joint chain: a small character. */
	st = g_new0 (SkinState, 1);
	st->path = g_build_filename (g_get_tmp_dir (), "lrg-bench-skinned.glb", NULL);
	test_glb_write_skinned_tube (st->path, SKIN_RINGS, SKIN_SIDES, SKIN_BONES, clips, SKIN_CLIPS);
	st->manager = lrg_asset_manager_new ();
	model = lrg_asset_manager_load_model (st->manager, st->path, &error);
	if (model == NULL)
		g_error ("skinned-models: %s", error->message);
	animations = lrg_asset_manager_load_model_animations (st->manager, st->path, &error);
	if (animations == NULL)
		g_error ("skinned-models: %s", error->message);
	st->animator = lrg_model_animator_new (model, animations);
	lrg_model_animator_set_gpu_skinning (st->animator, gpu);

	for (a = 0; a < SKIN_ACTORS; a++)
	{
		lrg_model_anim_state_reset (&st->actors[a]);
		lrg_model_animator_play (st->animator, &st->actors[a], (gint) (a % SKIN_CLIPS), TRUE, 0);
		st->actors[a].rate = 0.8 + 0.1 * (gdouble) (a % 5);
		lrg_model_animator_advance (st->animator, &st->actors[a], 0.07 * (gdouble) a);
	}

	st->camera = grl_camera3d_new ();
	grl_camera3d_set_position_xyz (st->camera, 0.0f, 7.0f, 16.0f);
	grl_camera3d_set_target_xyz (st->camera, 0.0f, 1.0f, 0.0f);
	return st;
}

static gpointer
skin_setup (void)
{
	return skin_setup_mode (TRUE);
}

static gpointer
skin_cpu_setup (void)
{
	return skin_setup_mode (FALSE);
}

static void
skin_frame (gpointer state,
            guint    frame_index)
{
	SkinState *st = state;
	guint      a;

	grl_camera3d_begin (st->camera);
	for (a = 0; a < SKIN_ACTORS; a++)
	{
		LrgModelAnimState *actor = &st->actors[a];

		/* Each actor crossfades to the next clip every two seconds. */
		if ((frame_index + a * 7) % 120 == 0)
			lrg_model_animator_play (st->animator, actor,
			                         (actor->clip + 1) % SKIN_CLIPS, TRUE, 0.3);
		lrg_model_animator_advance (st->animator, actor, 1.0 / 60.0);
		lrg_model_animator_apply (st->animator, actor);
		lrg_model_animator_draw_masked_ex (st->animator, NULL,
		                                   ((gfloat) (a % 10) - 4.5f) * 1.4f, 0.0f,
		                                   ((gfloat) (a / 10) - 2.0f) * 1.4f,
		                                   (gfloat) a * 0.6f, 1.0f, NULL);
	}
	grl_camera3d_end (st->camera);
}

static void
skin_teardown (gpointer state)
{
	SkinState *st = state;

	g_clear_object (&st->animator);
	g_clear_object (&st->manager);
	g_clear_object (&st->camera);
	g_remove (st->path);
	g_free (st->path);
	g_free (st);
}

/* --------------------------------------------------------------------------
 * physics: 500 dynamic boxes drifting in a 2D world
 * -------------------------------------------------------------------------- */

#define PHYS_BODIES (500)

typedef struct
{
	LrgPhysicsWorld *world;
	guint            contacts;
} PhysState;

static void
phys_on_collision (LrgPhysicsWorld  *world,
                   LrgCollisionInfo *info,
                   gpointer          user_data)
{
	PhysState *st = user_data;

	st->contacts++;
}

static gpointer
phys_setup (void)
{
	PhysState *st;
	GRand     *rng;
	guint      i;

	st = g_new0 (PhysState, 1);
	st->world = lrg_physics_world_new ();
	lrg_physics_world_set_gravity (st->world, 0.0f, 0.0f);
	rng = g_rand_new_with_seed (1234);
	for (i = 0; i < PHYS_BODIES; i++)
	{
		g_autoptr(LrgRigidBody) body = lrg_rigid_body_new (LRG_RIGID_BODY_DYNAMIC);

		lrg_rigid_body_set_box_shape (body, 8.0f, 8.0f);
		lrg_rigid_body_set_position (body,
		                             (gfloat) g_rand_double_range (rng, 0.0, 1000.0),
		                             (gfloat) g_rand_double_range (rng, 0.0, 1000.0));
		lrg_rigid_body_set_velocity (body,
		                             (gfloat) g_rand_double_range (rng, -20.0, 20.0),
		                             (gfloat) g_rand_double_range (rng, -20.0, 20.0));
		lrg_physics_world_add_body (st->world, body);
	}
	g_rand_free (rng);
	g_signal_connect (st->world, "collision", G_CALLBACK (phys_on_collision), st);
	return st;
}

static void
phys_frame (gpointer state,
            guint    frame_index)
{
	PhysState *st = state;

	lrg_physics_world_step (st->world, 1.0f / 60.0f);
}

static void
phys_teardown (gpointer state)
{
	PhysState *st = state;

	g_clear_object (&st->world);
	g_free (st);
}

/* --------------------------------------------------------------------------
 * tweens: 2000 property tweens on transform components
 * -------------------------------------------------------------------------- */

#define TWEEN_COUNT (2000)

typedef struct
{
	LrgTweenManager       *manager;
	LrgTransformComponent *targets[TWEEN_COUNT];
} TweenState;

static gpointer
tween_setup (void)
{
	TweenState *st;
	guint       i;

	st = g_new0 (TweenState, 1);
	st->manager = lrg_tween_manager_new ();
	for (i = 0; i < TWEEN_COUNT; i++)
	{
		LrgTween *tween;

		st->targets[i] = lrg_transform_component_new ();
		tween = lrg_tween_new (G_OBJECT (st->targets[i]), "local-x",
		                       0.5f + (gfloat) (i % 7) * 0.25f);
		lrg_tween_set_from_float (tween, 0.0f);
		lrg_tween_set_to_float (tween, 100.0f);
		lrg_tween_base_set_loop_count (LRG_TWEEN_BASE (tween), -1);
		lrg_tween_base_set_loop_mode (LRG_TWEEN_BASE (tween), LRG_TWEEN_LOOP_PING_PONG);
		lrg_tween_base_set_easing (LRG_TWEEN_BASE (tween), LRG_EASING_EASE_IN_OUT_QUAD);
		lrg_tween_manager_add (st->manager, LRG_TWEEN_BASE (tween));
		lrg_tween_base_start (LRG_TWEEN_BASE (tween));
		g_object_unref (tween);
	}
	return st;
}

static void
tween_frame (gpointer state,
             guint    frame_index)
{
	TweenState *st = state;

	lrg_tween_manager_update (st->manager, 1.0f / 60.0f);
}

static void
tween_teardown (gpointer state)
{
	TweenState *st = state;
	guint       i;

	g_clear_object (&st->manager);
	for (i = 0; i < TWEEN_COUNT; i++)
		g_clear_object (&st->targets[i]);
	g_free (st);
}

/* --------------------------------------------------------------------------
 * event-bus: 256 listeners on 32 event types, 256 events a frame
 * -------------------------------------------------------------------------- */

#define BUS_LISTENERS (256)
#define BUS_EVENTS    (256)
#define BUS_TYPES     (32)

#define BENCH_TYPE_LISTENER (bench_listener_get_type ())
G_DECLARE_FINAL_TYPE (BenchListener, bench_listener, BENCH, LISTENER, GObject)

struct _BenchListener
{
	GObject parent_instance;
	guint64 mask;
	gint    priority;
	guint   hits;
};

static void bench_listener_iface_init (LrgEventListenerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (BenchListener, bench_listener, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_EVENT_LISTENER,
                                                bench_listener_iface_init))

static const gchar *
bench_listener_get_id (LrgEventListener *listener)
{
	return "bench";
}

static gint
bench_listener_get_priority (LrgEventListener *listener)
{
	return BENCH_LISTENER (listener)->priority;
}

static guint64
bench_listener_get_mask (LrgEventListener *listener)
{
	return BENCH_LISTENER (listener)->mask;
}

static gboolean
bench_listener_on_event (LrgEventListener *listener,
                         LrgEvent         *event,
                         gpointer          context)
{
	BENCH_LISTENER (listener)->hits++;
	return TRUE;
}

static void
bench_listener_iface_init (LrgEventListenerInterface *iface)
{
	iface->get_id = bench_listener_get_id;
	iface->get_priority = bench_listener_get_priority;
	iface->get_event_mask = bench_listener_get_mask;
	iface->on_event = bench_listener_on_event;
}

static void
bench_listener_class_init (BenchListenerClass *klass)
{
}

static void
bench_listener_init (BenchListener *self)
{
}

typedef struct
{
	LrgEventBus  *bus;
	LrgCardEvent *events[BUS_TYPES];
} BusState;

static gpointer
bus_setup (void)
{
	BusState *st;
	guint     i;

	st = g_new0 (BusState, 1);
	st->bus = lrg_event_bus_new ();
	for (i = 0; i < BUS_LISTENERS; i++)
	{
		g_autoptr(BenchListener) listener = g_object_new (BENCH_TYPE_LISTENER, NULL);

		/* Each listener cares about one or two of the 32 types. */
		listener->mask = ((guint64) 1 << (i % BUS_TYPES)) |
		                 ((guint64) 1 << ((i * 7) % BUS_TYPES));
		listener->priority = (gint) (i % 5);
		lrg_event_bus_register (st->bus, LRG_EVENT_LISTENER (listener));
	}
	for (i = 0; i < BUS_TYPES; i++)
		st->events[i] = lrg_card_event_new ((LrgCardEventType) i);
	return st;
}

static void
bus_frame (gpointer state,
           guint    frame_index)
{
	BusState *st = state;
	guint     i;

	for (i = 0; i < BUS_EVENTS; i++)
		lrg_event_bus_emit (st->bus, LRG_EVENT (st->events[i % BUS_TYPES]), NULL);
}

static void
bus_teardown (gpointer state)
{
	BusState *st = state;
	guint     i;

	for (i = 0; i < BUS_TYPES; i++)
		g_clear_object (&st->events[i]);
	g_clear_object (&st->bus);
	g_free (st);
}

/* --------------------------------------------------------------------------
 * transforms: world transforms of 128 transform chains 16 deep
 * -------------------------------------------------------------------------- */

#define XFORM_CHAINS (128)
#define XFORM_DEPTH  (16)

typedef struct
{
	LrgTransformComponent *nodes[XFORM_CHAINS * XFORM_DEPTH];
	gfloat                 sink;
} XformState;

static gpointer
xform_setup (void)
{
	XformState *st;
	guint       c;
	guint       d;

	st = g_new0 (XformState, 1);
	for (c = 0; c < XFORM_CHAINS; c++)
		for (d = 0; d < XFORM_DEPTH; d++)
		{
			LrgTransformComponent *node = lrg_transform_component_new ();

			lrg_transform_component_set_local_position_xy (node, 1.0f, 0.5f);
			lrg_transform_component_set_local_rotation (node, 5.0f);
			lrg_transform_component_set_local_scale_xy (node, 1.01f, 0.99f);
			if (d > 0)
				lrg_transform_component_set_parent (node,
				                                    st->nodes[c * XFORM_DEPTH + d - 1]);
			st->nodes[c * XFORM_DEPTH + d] = node;
		}
	return st;
}

static void
xform_frame (gpointer state,
             guint    frame_index)
{
	XformState *st = state;
	guint       i;

	for (i = 0; i < XFORM_CHAINS * XFORM_DEPTH; i++)
	{
		g_autoptr(GrlVector2) pos = NULL;
		g_autoptr(GrlVector2) scale = NULL;

		if (i % XFORM_DEPTH == 0)
			lrg_transform_component_set_local_rotation (st->nodes[i],
			                                            (gfloat) (frame_index % 360));
		pos = lrg_transform_component_get_world_position (st->nodes[i]);
		scale = lrg_transform_component_get_world_scale (st->nodes[i]);
		st->sink += pos->x + scale->y +
		            lrg_transform_component_get_world_rotation (st->nodes[i]);
	}
}

static void
xform_teardown (gpointer state)
{
	XformState *st = state;
	guint       i;

	/* Children first, so no parent dies under a live child. */
	for (i = XFORM_CHAINS * XFORM_DEPTH; i > 0; i--)
		g_clear_object (&st->nodes[i - 1]);
	g_free (st);
}

/* --------------------------------------------------------------------------
 * object-pool: 4000 acquisitions and releases a frame plus iteration
 * -------------------------------------------------------------------------- */

#define POOL_SIZE    (6000)
#define POOL_CHURN   (4000)

#define BENCH_TYPE_POOLED (bench_pooled_get_type ())
G_DECLARE_FINAL_TYPE (BenchPooled, bench_pooled, BENCH, POOLED, GObject)

struct _BenchPooled
{
	GObject  parent_instance;
	gboolean active;
	guint    value;
};

static void bench_pooled_iface_init (LrgPoolableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (BenchPooled, bench_pooled, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_POOLABLE,
                                                bench_pooled_iface_init))

static void
bench_pooled_reset (LrgPoolable *poolable)
{
	BENCH_POOLED (poolable)->value = 0;
}

static gboolean
bench_pooled_is_active (LrgPoolable *poolable)
{
	return BENCH_POOLED (poolable)->active;
}

static void
bench_pooled_set_active (LrgPoolable *poolable,
                         gboolean     active)
{
	BENCH_POOLED (poolable)->active = active;
}

static void
bench_pooled_iface_init (LrgPoolableInterface *iface)
{
	iface->reset = bench_pooled_reset;
	iface->is_active = bench_pooled_is_active;
	iface->set_active = bench_pooled_set_active;
}

static void
bench_pooled_class_init (BenchPooledClass *klass)
{
}

static void
bench_pooled_init (BenchPooled *self)
{
}

typedef struct
{
	LrgObjectPool *pool;
	GObject       *held[POOL_CHURN];
	guint          visited;
} PoolState;

static gboolean
pool_visit (LrgPoolable *object,
            gpointer     user_data)
{
	PoolState *st = user_data;

	st->visited++;
	return TRUE;
}

static gpointer
pool_setup (void)
{
	PoolState *st;

	st = g_new0 (PoolState, 1);
	st->pool = lrg_object_pool_new (BENCH_TYPE_POOLED, POOL_SIZE,
	                                LRG_POOL_GROWTH_FIXED);
	return st;
}

static void
pool_frame (gpointer state,
            guint    frame_index)
{
	PoolState *st = state;
	guint      i;

	/* Spawn a wave, walk it a few times, then retire it oldest first. */
	for (i = 0; i < POOL_CHURN; i++)
		st->held[i] = lrg_object_pool_acquire (st->pool);
	for (i = 0; i < 4; i++)
		lrg_object_pool_foreach_active (st->pool, pool_visit, st);
	for (i = 0; i < POOL_CHURN; i++)
		if (st->held[i] != NULL)
			lrg_object_pool_release (st->pool, LRG_POOLABLE (st->held[i]));
}

static void
pool_teardown (gpointer state)
{
	PoolState *st = state;

	g_clear_object (&st->pool);
	g_free (st);
}

/* --------------------------------------------------------------------------
 * portals: 24x24 grid of sectors, camera wandering inside one corner sector
 * -------------------------------------------------------------------------- */

#define PORTAL_GRID   (24)
#define PORTAL_CELL   (100.0f)
#define PORTAL_PASSES (64)

typedef struct
{
	LrgPortalSystem *system;
} PortalState;

/* Adds the portal between two neighbouring cells, facing the -x/-z corner
 * so the camera there sees through every portal in the grid. */
static void
portal_link (LrgPortalSystem *system,
             LrgSector       *sector_a,
             LrgSector       *sector_b,
             const gchar     *id_a,
             const gchar     *id_b,
             gfloat           min_x,
             gfloat           min_z,
             gfloat           max_x,
             gfloat           max_z)
{
	g_autofree gchar *portal_id = g_strdup_printf ("%s|%s", id_a, id_b);
	g_autoptr(LrgBoundingBox3D) bounds = NULL;
	g_autoptr(LrgPortal) portal = NULL;
	GrlVector3 normal;

	normal.x = -1.0f;
	normal.y = 0.0f;
	normal.z = -1.0f;
	bounds = lrg_bounding_box3d_new (min_x, 0.0f, min_z, max_x, 50.0f, max_z);
	portal = lrg_portal_new_with_normal (portal_id, bounds, id_a, id_b, &normal);
	lrg_sector_add_portal (sector_a, portal_id);
	lrg_sector_add_portal (sector_b, portal_id);
	lrg_portal_system_add_portal (system, portal);
}

static gpointer
portal_setup (void)
{
	PortalState *st;
	LrgSector   *cells[PORTAL_GRID * PORTAL_GRID];
	gchar       *ids[PORTAL_GRID * PORTAL_GRID];
	guint        x;
	guint        z;
	guint        i;

	st = g_new0 (PortalState, 1);
	st->system = lrg_portal_system_new ();
	lrg_portal_system_set_max_portal_depth (st->system, 8);
	for (z = 0; z < PORTAL_GRID; z++)
		for (x = 0; x < PORTAL_GRID; x++)
		{
			g_autoptr(LrgBoundingBox3D) bounds = NULL;

			i = z * PORTAL_GRID + x;
			ids[i] = g_strdup_printf ("cell-%u-%u", x, z);
			bounds = lrg_bounding_box3d_new ((gfloat) x * PORTAL_CELL, 0.0f,
			                                 (gfloat) z * PORTAL_CELL,
			                                 (gfloat) (x + 1) * PORTAL_CELL, 50.0f,
			                                 (gfloat) (z + 1) * PORTAL_CELL);
			cells[i] = lrg_sector_new (ids[i], bounds);
		}
	for (z = 0; z < PORTAL_GRID; z++)
		for (x = 0; x < PORTAL_GRID; x++)
		{
			i = z * PORTAL_GRID + x;
			if (x + 1 < PORTAL_GRID)
				portal_link (st->system, cells[i], cells[i + 1], ids[i], ids[i + 1],
				             (gfloat) (x + 1) * PORTAL_CELL, (gfloat) z * PORTAL_CELL + 40.0f,
				             (gfloat) (x + 1) * PORTAL_CELL, (gfloat) z * PORTAL_CELL + 60.0f);
			if (z + 1 < PORTAL_GRID)
				portal_link (st->system, cells[i], cells[i + PORTAL_GRID], ids[i],
				             ids[i + PORTAL_GRID],
				             (gfloat) x * PORTAL_CELL + 40.0f, (gfloat) (z + 1) * PORTAL_CELL,
				             (gfloat) x * PORTAL_CELL + 60.0f, (gfloat) (z + 1) * PORTAL_CELL);
		}
	for (i = 0; i < PORTAL_GRID * PORTAL_GRID; i++)
	{
		lrg_portal_system_add_sector (st->system, cells[i]);
		lrg_sector_free (cells[i]);
		g_free (ids[i]);
	}
	return st;
}

static void
portal_frame (gpointer state,
              guint    frame_index)
{
	PortalState *st = state;
	guint        pass;

	/* Several cameras' worth of updates, all in the corner sector. */
	for (pass = 0; pass < PORTAL_PASSES; pass++)
	{
		GrlVector3 camera;

		camera.x = 10.0f + (gfloat) ((frame_index + pass) % 30);
		camera.y = 20.0f;
		camera.z = 10.0f + (gfloat) (pass % 30);
		lrg_portal_system_update (st->system, &camera);
	}
}

static void
portal_teardown (gpointer state)
{
	PortalState *st = state;

	g_clear_object (&st->system);
	g_free (st);
}

/* ==========================================================================
 * pacing: constant light work, for measuring the frame limiter (--fps N)
 * ========================================================================== */

static gpointer
pacing_setup (void)
{
	return NULL;
}

static void
pacing_frame (gpointer state,
              guint    frame_index)
{
	guint i;

	/* About a millisecond of steady CPU work and a few quads: what is left
	 * of the frame is the limiter's wait, whose accuracy this measures */
	for (i = 0; i < 64; i++)
	{
		GrlColor color = { (guint8) (i * 4), 120, 200, 255 };

		grl_draw_rectangle ((gint) ((i * 5 + frame_index) % 300),
		                    (gint) ((i * 3) % 220), 8, 8, &color);
	}
	g_usleep (1000);
}

static void
pacing_teardown (gpointer state)
{
}

/* ==========================================================================
 * autosave: light frames with a save-manager save every 60 frames
 * ========================================================================== */

#define AUTOSAVE_KEYS     (4000)
#define AUTOSAVE_INTERVAL (60)

#define BENCH_TYPE_SAVE_STATE (bench_save_state_get_type ())
G_DECLARE_FINAL_TYPE (BenchSaveState, bench_save_state, BENCH, SAVE_STATE, GObject)

struct _BenchSaveState
{
	GObject parent_instance;
	guint   generation;
};

static void bench_save_state_iface_init (LrgSaveableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (BenchSaveState, bench_save_state, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_SAVEABLE,
                                                bench_save_state_iface_init))

static const gchar *
bench_save_state_get_save_id (LrgSaveable *saveable)
{
	return "world";
}

/* A few thousand keys: a mid-sized game's inventory, flags and stats. */
static gboolean
bench_save_state_save (LrgSaveable     *saveable,
                       LrgSaveContext  *context,
                       GError         **error)
{
	BenchSaveState *self = BENCH_SAVE_STATE (saveable);
	gchar           key[32];
	guint           i;

	self->generation++;
	for (i = 0; i < AUTOSAVE_KEYS; i++)
	{
		g_snprintf (key, sizeof key, "entry_%u", i);
		if (i % 2 == 0)
			lrg_save_context_write_int (context, key, (gint64) (i * self->generation));
		else
			lrg_save_context_write_string (context, key, "some saved string value");
	}
	return TRUE;
}

static gboolean
bench_save_state_load (LrgSaveable     *saveable,
                       LrgSaveContext  *context,
                       GError         **error)
{
	return TRUE;
}

static void
bench_save_state_iface_init (LrgSaveableInterface *iface)
{
	iface->get_save_id = bench_save_state_get_save_id;
	iface->save = bench_save_state_save;
	iface->load = bench_save_state_load;
}

static void
bench_save_state_class_init (BenchSaveStateClass *klass)
{
}

static void
bench_save_state_init (BenchSaveState *self)
{
}

typedef struct
{
	LrgSaveManager *manager;
	BenchSaveState *world;
	gchar          *dir;
	gboolean        sync;
} AutosaveState;

/* Light steady per-frame work, so a hitch stands out against it. */
static void
bench_light_frame (guint frame_index)
{
	guint i;

	for (i = 0; i < 64; i++)
	{
		GrlColor color = { (guint8) (i * 4), 120, 200, 255 };

		grl_draw_rectangle ((gint) ((i * 5 + frame_index) % 300),
		                    (gint) ((i * 3) % 220), 8, 8, &color);
	}
}

/* Saves go to a real disk (the user cache dir), not a tmpfs, so the write
 * costs what it would in a game. */
static gpointer
autosave_setup (void)
{
	AutosaveState *st;

	st = g_new0 (AutosaveState, 1);
	st->dir = g_build_filename (g_get_user_cache_dir (), "lrg-bench-saves", NULL);
	st->manager = lrg_save_manager_new ();
	lrg_save_manager_set_save_directory (st->manager, st->dir);
	st->world = g_object_new (BENCH_TYPE_SAVE_STATE, NULL);
	lrg_save_manager_register (st->manager, LRG_SAVEABLE (st->world));
	return st;
}

static void
autosave_frame (gpointer state,
                guint    frame_index)
{
	AutosaveState *st = state;

	bench_light_frame (frame_index);
	if (frame_index % AUTOSAVE_INTERVAL != AUTOSAVE_INTERVAL - 1)
		return;
	if (st->sync)
		lrg_save_manager_save (st->manager, "autosave", NULL);
	else
		lrg_save_manager_save_in_background (st->manager, "autosave", NULL, NULL);
}

/* The same save made synchronously: generation and the file write (with
 * its fsync) land on the frame. */
static gpointer
autosave_sync_setup (void)
{
	AutosaveState *st = autosave_setup ();

	st->sync = TRUE;
	return st;
}

static void
autosave_teardown (gpointer state)
{
	AutosaveState    *st = state;
	g_autofree gchar *path = NULL;

	lrg_save_manager_flush (st->manager, NULL);
	lrg_save_manager_unregister_all (st->manager);
	g_clear_object (&st->manager);
	g_clear_object (&st->world);
	path = g_build_filename (st->dir, "autosave.yaml", NULL);
	g_unlink (path);
	g_rmdir (st->dir);
	g_free (st->dir);
	g_free (st);
}

/* ==========================================================================
 * despawn: a 4000-object world where a quarter dies and respawns at once
 * ========================================================================== */

#define DESPAWN_OBJECTS  (4000)
#define DESPAWN_WAVE     (1000)
#define DESPAWN_INTERVAL (60)

typedef struct
{
	LrgWorld       *world;
	GrlTexture     *texture;
	LrgGameObject **live;     /* borrowed: the world holds the references */
	guint           next;     /* next wave starts here (oldest first) */
} DespawnState;

static LrgGameObject *
despawn_spawn (DespawnState *st,
               guint         i)
{
	g_autoptr(LrgGameObject)      obj = NULL;
	g_autoptr(LrgSpriteComponent) sprite = NULL;

	obj = lrg_game_object_new_at ((gfloat) (i % 64) * 5.0f, (gfloat) ((i / 64) % 60) * 4.0f);
	sprite = lrg_sprite_component_new_with_texture (st->texture);
	lrg_game_object_add_component (obj, LRG_COMPONENT (sprite));
	grl_entity_set_z_index (GRL_ENTITY (obj), (gint) ((i * 7919) % 7));
	lrg_world_add_object (st->world, obj);
	return obj;
}

static gpointer
despawn_setup (void)
{
	DespawnState *st;
	guint         i;

	st = g_new0 (DespawnState, 1);
	st->world = lrg_world_new ();
	st->texture = bench_make_texture (4);
	st->live = g_new0 (LrgGameObject *, DESPAWN_OBJECTS);
	for (i = 0; i < DESPAWN_OBJECTS; i++)
		st->live[i] = despawn_spawn (st, i);
	return st;
}

static void
despawn_frame (gpointer state,
               guint    frame_index)
{
	DespawnState *st = state;
	guint         i;

	/* A wave of the oldest objects dies (an explosion, a level section
	 * unloading) and as many spawn, all in one frame. */
	if (frame_index % DESPAWN_INTERVAL == DESPAWN_INTERVAL - 1)
	{
		for (i = 0; i < DESPAWN_WAVE; i++)
		{
			guint slot = (st->next + i) % DESPAWN_OBJECTS;

			lrg_world_remove_object (st->world, st->live[slot]);
			st->live[slot] = despawn_spawn (st, slot + frame_index);
		}
		st->next = (st->next + DESPAWN_WAVE) % DESPAWN_OBJECTS;
	}
	lrg_world_update (st->world, 1.0f / 60.0f);
	lrg_world_draw (st->world);
}

static void
despawn_teardown (gpointer state)
{
	DespawnState *st = state;

	g_clear_object (&st->world);
	g_clear_object (&st->texture);
	g_free (st->live);
	g_free (st);
}

/* ==========================================================================
 * analytics: an event a frame to an HTTP backend behind a 20 ms server
 * ========================================================================== */

#define ANALYTICS_SERVER_DELAY_MS (20)

/* A one-thread HTTP/1.1 server on localhost that answers every request
 * with 200 after ANALYTICS_SERVER_DELAY_MS: a stand-in for a remote
 * collector's round trip. */
typedef struct
{
	GSocketListener *listener;
	GCancellable    *cancellable;
	GThread         *thread;
	guint16          port;
} BenchHttpServer;

static void
bench_http_serve (BenchHttpServer   *server,
                  GSocketConnection *connection)
{
	static const gchar          reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
	g_autoptr(GDataInputStream) data = NULL;
	GOutputStream              *out;

	data = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (connection)));
	g_data_input_stream_set_newline_type (data, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
	out = g_io_stream_get_output_stream (G_IO_STREAM (connection));

	for (;;)
	{
		gsize    length = 0;
		gboolean any = FALSE;
		gchar   *line;

		while ((line = g_data_input_stream_read_line (data, NULL, server->cancellable,
		                                              NULL)) != NULL)
		{
			gboolean end = line[0] == '\0';

			any = TRUE;
			if (g_ascii_strncasecmp (line, "Content-Length:", 15) == 0)
				length = (gsize) g_ascii_strtoull (line + 15, NULL, 10);
			g_free (line);
			if (end)
				break;
		}
		if (!any)
			return;
		if (length > 0)
		{
			g_autofree gchar *body = g_malloc (length);

			if (!g_input_stream_read_all (G_INPUT_STREAM (data), body, length, NULL,
			                              server->cancellable, NULL))
				return;
		}
		g_usleep (ANALYTICS_SERVER_DELAY_MS * 1000);
		if (!g_output_stream_write_all (out, reply, sizeof reply - 1, NULL,
		                                server->cancellable, NULL))
			return;
	}
}

static gpointer
bench_http_thread (gpointer user_data)
{
	BenchHttpServer *server = user_data;

	for (;;)
	{
		g_autoptr(GSocketConnection) connection = NULL;

		connection = g_socket_listener_accept (server->listener, NULL,
		                                       server->cancellable, NULL);
		if (connection == NULL)
			return NULL;
		bench_http_serve (server, connection);
	}
}

typedef struct
{
	BenchHttpServer          server;
	LrgAnalyticsBackendHttp *backend;
} AnalyticsState;

static gpointer
analytics_setup (void)
{
	AnalyticsState   *st;
	g_autofree gchar *url = NULL;

	st = g_new0 (AnalyticsState, 1);
	st->server.cancellable = g_cancellable_new ();
	st->server.listener = g_socket_listener_new ();
	st->server.port = g_socket_listener_add_any_inet_port (st->server.listener, NULL, NULL);
	st->server.thread = g_thread_new ("bench-http", bench_http_thread, &st->server);

	/* Default batching (10 events) and retries; the flush timer is off
	 * (it needs a main loop, which the bench does not run). */
	url = g_strdup_printf ("http://127.0.0.1:%u/collect", st->server.port);
	st->backend = lrg_analytics_backend_http_new (url);
	lrg_analytics_backend_http_set_flush_interval (st->backend, 0);
	return st;
}

static void
analytics_frame (gpointer state,
                 guint    frame_index)
{
	AnalyticsState               *st = state;
	g_autoptr(LrgAnalyticsEvent)  event = NULL;

	bench_light_frame (frame_index);

	/* One gameplay event a frame (a pickup, a kill, a checkpoint). */
	event = lrg_analytics_event_new ("pickup");
	lrg_analytics_event_set_property_int (event, "frame", (gint64) frame_index);
	lrg_analytics_backend_send_event (LRG_ANALYTICS_BACKEND (st->backend), event, NULL);
}

static void
analytics_teardown (gpointer state)
{
	AnalyticsState *st = state;

	g_clear_object (&st->backend);
	g_cancellable_cancel (st->server.cancellable);
	g_thread_join (st->server.thread);
	g_socket_listener_close (st->server.listener);
	g_clear_object (&st->server.listener);
	g_clear_object (&st->server.cancellable);
	g_free (st);
}

/* ==========================================================================
 * first-use: features that appear mid-run (the first-use GPU work hitch)
 * ========================================================================== */

/* the measured frame the features first draw on */
#define FIRST_USE_FRAME (BENCH_WARMUP_FRAMES + 100)

typedef struct
{
	WorldState    *world;
	SkinState     *skin;
	LrgCloudLayer *clouds;
	LrgColorGrade *grade;
	LrgHeatHaze   *haze;
	LrgSharpen    *sharpen;
	GrlTexture    *screen;     /* what the post effects draw through */
} FirstUseState;

/* how first-use is warmed before it is measured */
typedef enum
{
	FIRST_USE_COLD,     /* not at all: the first draw builds everything */
	FIRST_USE_QUEUE,    /* the prewarm queue run to the end */
	FIRST_USE_HIDDEN    /* the queue, then one hidden draw of the scene */
} FirstUseWarm;

/* the hidden draw of FIRST_USE_HIDDEN draws a frame of the scenario */
static void first_use_frame (gpointer state,
                             guint    frame_index);

/*
 * first_use_setup_mode:
 * Water (64 patches), 20000 billboards, ~40000 grass blades, a cloud
 * layer, a lit static batch, 50 GPU-skinned actors and three post effects
 * (colour grade, heat haze, sharpen): all built here, none drawn before
 * FIRST_USE_FRAME. FIRST_USE_COLD leaves the shader compiles and uploads
 * to their first draw; FIRST_USE_QUEUE runs the default prewarm queue
 * (which they all joined when created) to the end here, as a loading
 * screen would; FIRST_USE_HIDDEN also draws the scene once hidden, as a
 * loading screen's prewarm-draw handler would (the driver's draw-time
 * shader variants).
 */

static gpointer
first_use_setup_mode (FirstUseWarm prewarm)
{
	FirstUseState        *st;
	WorldState           *world;
	g_autoptr(GrlColor)   sun = NULL;
	g_autoptr(GrlVector3) direction = NULL;
	g_autoptr(GArray)     blades = NULL;
	gint                  i;
	gint                  j;

	st = g_new0 (FirstUseState, 1);
	st->world = world = world_state_new ();
	world->texture = bench_make_texture (8);

	world->water = lrg_water_surface_new ();
	for (i = -4; i < 4; i++)
		for (j = -4; j < 4; j++)
			lrg_water_surface_add_grid (world->water, (gfloat) i * 16.0f, (gfloat) j * 16.0f,
			                            (gfloat) i * 16.0f + 16.0f, (gfloat) j * 16.0f + 16.0f,
			                            -0.5f, 16, 16, NULL, NULL, NULL);

	world->billboards = lrg_billboard_batch_new ();
	lrg_billboard_batch_set_material_texture (world->billboards, 0, world->texture);
	for (i = 0; i < 20000; i++)
	{
		g_autoptr(GrlVector3) p = NULL;

		p = grl_vector3_new ((gfloat) (i % 160) - 80.0f, 0.0f, (gfloat) (i / 160) - 60.0f);
		lrg_billboard_batch_add (world->billboards, 0, p, 1.0f, 2.0f);
	}

	world->grass = lrg_grass_field_new (7);
	blades = g_array_new (FALSE, FALSE, sizeof (LrgGrassBlade));
	for (i = -20; i < 20; i++)
		for (j = -20; j < 20; j++)
			lrg_grass_field_place_cell (world->grass, i, j, 0.5f, 0.0, 0.0, NULL, NULL, blades);
	lrg_grass_field_set_blades (world->grass,
	                            (const LrgGrassBlade *) (gconstpointer) blades->data, blades->len);

	world->batch = bench_static_batch ();
	world->lights = lrg_light_set_new ();
	sun = grl_color_new (230, 220, 200, 255);
	direction = grl_vector3_new (-0.4f, -1.0f, -0.3f);
	lrg_light_set_set_directional (world->lights, direction, sun);

	st->clouds = lrg_cloud_layer_new (7);
	lrg_cloud_layer_set_coverage (st->clouds, 0.6f);

	st->skin = skin_setup_mode (TRUE);

	st->grade = lrg_color_grade_new ();
	lrg_color_grade_set_exposure (st->grade, 0.3f);
	st->haze = lrg_heat_haze_new ();
	st->sharpen = lrg_sharpen_new ();
	st->screen = bench_make_texture (64);

	if (prewarm != FIRST_USE_COLD)
	{
		LrgPrewarm *queue = lrg_prewarm_get_default ();
		gint64      t0 = g_get_monotonic_time ();
		guint       pending = lrg_prewarm_get_pending (queue);

		lrg_prewarm_run_all (queue);
		g_printerr ("%s: %u queued entries warmed in %.1f ms (setup, untimed)\n",
		            prewarm == FIRST_USE_QUEUE ? "first-use-queue" : "first-use-prewarm",
		            pending, (gdouble) (g_get_monotonic_time () - t0) / 1000.0);
	}
	if (prewarm == FIRST_USE_HIDDEN)
	{
		gint64 t0 = g_get_monotonic_time ();

		/* what LrgTemplateLoadingState::prewarm-draw is for: the scene
		 * drawn once, scissored away, on a loading-screen frame */
		BeginDrawing ();
		lrg_prewarm_begin_hidden_draw ();
		first_use_frame (st, FIRST_USE_FRAME);
		lrg_prewarm_end_hidden_draw ();
		ClearBackground (BLACK);
		EndDrawing ();
		g_printerr ("first-use-prewarm: hidden draw %.1f ms (setup, untimed)\n",
		            (gdouble) (g_get_monotonic_time () - t0) / 1000.0);
	}
	return st;
}

static gpointer
first_use_setup (void)
{
	return first_use_setup_mode (FIRST_USE_COLD);
}

static gpointer
first_use_queue_setup (void)
{
	return first_use_setup_mode (FIRST_USE_QUEUE);
}

static gpointer
first_use_prewarm_setup (void)
{
	return first_use_setup_mode (FIRST_USE_HIDDEN);
}

static void
first_use_frame (gpointer state,
                 guint    frame_index)
{
	FirstUseState        *st = state;
	WorldState           *world = st->world;
	gfloat                eye[3];
	gfloat                towards_sun[3] = { 0.4f, 0.8f, 0.3f };
	GrlRectangle          rect;
	g_autoptr(GrlVector3) camera = NULL;

	bench_world_camera (world->camera, frame_index, eye);
	if (frame_index < FIRST_USE_FRAME)
	{
		/* the plain world before the features appear */
		grl_camera3d_begin (world->camera);
		lrg_static_batch_set_shader (world->batch, NULL);
		lrg_static_batch_draw (world->batch, LRG_STATIC_BATCH_ALL_LAYERS);
		grl_camera3d_end (world->camera);
		return;
	}

	camera = grl_vector3_new (eye[0], eye[1], eye[2]);
	grl_camera3d_begin (world->camera);
	lrg_light_set_apply (world->lights, eye[0], eye[1], eye[2]);
	lrg_light_set_bind (world->lights);
	lrg_static_batch_set_shader (world->batch, lrg_light_set_get_shader (world->lights));
	lrg_static_batch_draw (world->batch, LRG_STATIC_BATCH_ALL_LAYERS);
	lrg_grass_field_draw (world->grass, eye, (gfloat) frame_index / 60.0f);
	lrg_billboard_batch_draw (world->billboards, camera);
	lrg_cloud_layer_draw (st->clouds, camera, 400.0f, towards_sun);
	lrg_water_surface_draw (world->water, eye, (gfloat) frame_index / 60.0f);
	grl_camera3d_end (world->camera);
	lrg_light_set_unbind ();

	skin_frame (st->skin, frame_index);

	/* the post effects, each through a small quad of the screen */
	rect.x = 0.0f;
	rect.y = 0.0f;
	rect.width = 64.0f;
	rect.height = 64.0f;
	lrg_color_grade_draw_texture (st->grade, st->screen, &rect, &rect);
	rect.x = 64.0f;
	lrg_heat_haze_draw_texture (st->haze, st->screen, &rect, &rect);
	rect.x = 128.0f;
	lrg_sharpen_draw_texture (st->sharpen, st->screen, &rect, &rect);
}

static void
first_use_teardown (gpointer state)
{
	FirstUseState *st = state;

	skin_teardown (st->skin);
	if (st->clouds != NULL)
		lrg_cloud_layer_release_gpu (st->clouds);
	g_clear_object (&st->clouds);
	g_clear_object (&st->grade);
	g_clear_object (&st->haze);
	g_clear_object (&st->sharpen);
	g_clear_object (&st->screen);
	world_teardown (st->world);
	g_free (st);
}

 * audio: streamed synthesis and music decoding during a 3 ms frame
 *
 * Each frame spins 3 ms on the clock (a ~300 fps game's steady work, so a
 * stream buffer lasts as many frames as it would in a game) and then makes
 * the audio calls a game makes every frame.  Whatever the audio costs on
 * the game thread shows up as work above 3 ms.  The device is opened once
 * at master volume 0 (the mix still runs, silently); without a device the
 * scenarios measure nothing but the spin and say so.
 * ========================================================================== */

#define AUDIO_FRAME_SPIN_US  (3000)
#define AUDIO_RATE           (44100)
#define AUDIO_MUSIC_SECONDS  (30)

static gboolean
bench_audio_device (void)
{
	GrlAudioDevice *device;

	device = grl_audio_device_get_default ();
	if (device == NULL)
		return FALSE;
	if (!grl_audio_device_is_ready (device))
		grl_audio_device_init_audio (device);
	if (!grl_audio_device_is_ready (device))
	{
		g_printerr ("lrg-bench: no audio device, the audio scenarios measure nothing\n");
		return FALSE;
	}
	grl_audio_device_set_master_volume (device, 0.0f);
	return TRUE;
}

static void
bench_audio_spin (void)
{
	gint64 until;

	until = g_get_monotonic_time () + AUDIO_FRAME_SPIN_US;
	while (g_get_monotonic_time () < until)
		;
}

/* -- midi-synth: a busy General MIDI song (~60 voices) -------------------- */

static gpointer
midi_setup (void)
{
	LrgMidiSynth      *synth;
	g_autoptr(GBytes)  sf2 = NULL;
	g_autoptr(GBytes)  song = NULL;

	synth = lrg_midi_synth_new (TEST_MIDI_RATE);
	sf2 = test_midi_build_sf2 ();
	song = test_midi_build_busy_song (15, 64);
	if (!lrg_midi_synth_load_soundfont_bytes (synth, sf2, NULL) ||
	    !lrg_midi_synth_play (synth, song, TRUE, 0.0, NULL))
		g_printerr ("lrg-bench: midi-synth: the song did not load\n");
	if (bench_audio_device ())
		(void) lrg_midi_synth_start_stream (synth, NULL);
	return synth;
}

static void
midi_frame (gpointer state,
            guint    frame_index)
{
	bench_light_frame (frame_index);
	bench_audio_spin ();
	lrg_midi_synth_update (LRG_MIDI_SYNTH (state));
}

static void
midi_teardown (gpointer state)
{
	g_object_unref (state);
}

/* -- sound-bed: six layers, always crossfading, through a reverb ---------- */

#define BED_LAYERS     (6)
#define BED_SWAP_EVERY (60)

typedef struct
{
	LrgSoundBed *bed;
	guint        from;
} BedState;

static gpointer
bed_setup (void)
{
	BedState           *st;
	g_autoptr(GRand)    rand = NULL;
	g_autofree gfloat  *samples = NULL;
	g_autoptr(LrgReverb) reverb = NULL;
	gsize               n;
	guint               layer;

	st = g_new0 (BedState, 1);
	st->bed = lrg_sound_bed_new (AUDIO_RATE);
	rand = g_rand_new_with_seed (7);
	n = (gsize) AUDIO_RATE * 2 * 2;   /* two seconds of stereo */
	samples = g_new (gfloat, n);
	for (layer = 0; layer < BED_LAYERS; layer++)
	{
		g_autofree gchar *name = g_strdup_printf ("layer-%u", layer);
		gsize             i;

		/* filtered noise plus a tone: a wind / water / hum loop */
		for (i = 0; i < n; i++)
			samples[i] = 0.2f * (gfloat) g_rand_double_range (rand, -1.0, 1.0) +
			             0.2f * sinf ((gfloat) (i / 2) * 0.01f * (gfloat) (layer + 1));
		(void) lrg_sound_bed_add_layer_samples (st->bed, name, samples, n, 2, AUDIO_RATE, NULL);
		(void) lrg_sound_bed_set_level (st->bed, name, layer < 3 ? 1.0f : 0.0f, 0.0);
	}
	reverb = lrg_reverb_new ();
	lrg_audio_effect_chain_add (lrg_sound_bed_get_effects (st->bed), LRG_AUDIO_EFFECT (reverb));
	if (bench_audio_device ())
		(void) lrg_sound_bed_start_stream (st->bed, NULL);
	return st;
}

static void
bed_frame (gpointer state,
           guint    frame_index)
{
	BedState *st = state;

	bench_light_frame (frame_index);
	bench_audio_spin ();

	/* a crossfade starts every second while the last one still runs */
	if (frame_index % BED_SWAP_EVERY == 0)
	{
		g_autofree gchar *from = g_strdup_printf ("layer-%u", st->from % BED_LAYERS);
		g_autofree gchar *to = g_strdup_printf ("layer-%u", (st->from + 3) % BED_LAYERS);

		lrg_sound_bed_crossfade (st->bed, from, to, 1.5);
		st->from++;
	}
	lrg_sound_bed_update (st->bed);
}

static void
bed_teardown (gpointer state)
{
	BedState *st = state;

	g_clear_object (&st->bed);
	g_free (st);
}

/* -- procedural: an additive synth, 48 sine partials ---------------------- */

#define BENCH_PARTIALS (48)

#define BENCH_TYPE_ADDITIVE (bench_additive_get_type ())
G_DECLARE_FINAL_TYPE (BenchAdditive, bench_additive, BENCH, ADDITIVE, LrgProceduralAudio)

struct _BenchAdditive
{
	LrgProceduralAudio parent_instance;
	gfloat             phase[BENCH_PARTIALS];
};

G_DEFINE_TYPE (BenchAdditive, bench_additive, LRG_TYPE_PROCEDURAL_AUDIO)

static void
bench_additive_generate (LrgProceduralAudio *audio,
                         gfloat             *buffer,
                         gint                frame_count)
{
	BenchAdditive *self = BENCH_ADDITIVE (audio);
	gint           f;
	guint          p;

	for (f = 0; f < frame_count; f++)
	{
		gfloat sample = 0.0f;

		for (p = 0; p < BENCH_PARTIALS; p++)
		{
			sample += sinf (self->phase[p]) / (gfloat) (p + 1);
			self->phase[p] += 2.0f * G_PI * 110.0f * (gfloat) (p + 1) / (gfloat) AUDIO_RATE;
			if (self->phase[p] > 2.0f * G_PI)
				self->phase[p] -= 2.0f * G_PI;
		}
		buffer[2 * f] = sample * 0.1f;
		buffer[2 * f + 1] = sample * 0.1f;
	}
}

static void
bench_additive_class_init (BenchAdditiveClass *klass)
{
	LRG_PROCEDURAL_AUDIO_CLASS (klass)->generate = bench_additive_generate;
}

static void
bench_additive_init (BenchAdditive *self)
{
}

static gpointer
procedural_setup (void)
{
	LrgProceduralAudio *audio;

	(void) bench_audio_device ();
	audio = g_object_new (BENCH_TYPE_ADDITIVE, "sample-rate", AUDIO_RATE, "channels", 2, NULL);
	lrg_procedural_audio_play (audio);
	return audio;
}

static void
procedural_frame (gpointer state,
                  guint    frame_index)
{
	bench_light_frame (frame_index);
	bench_audio_spin ();
	lrg_procedural_audio_update (LRG_PROCEDURAL_AUDIO (state));
}

static void
procedural_teardown (gpointer state)
{
	lrg_procedural_audio_stop (LRG_PROCEDURAL_AUDIO (state));
	g_object_unref (state);
}

/* -- music-ogg / music-mp3: a streamed track through LrgMusicTrack -------- */

/* A 16-bit stereo WAV of AUDIO_MUSIC_SECONDS: chords and noise, so a
 * lossy encoder has real work and the decoder too. */
static gboolean
bench_write_wav (const gchar *path)
{
	g_autoptr(GByteArray) out = NULL;
	g_autoptr(GRand)      rand = NULL;
	guint32               frames;
	guint32               bytes;
	guint32               i;

	frames = AUDIO_RATE * AUDIO_MUSIC_SECONDS;
	bytes = frames * 4;
	out = g_byte_array_sized_new (bytes + 44);
	rand = g_rand_new_with_seed (11);
	{
		guint8 header[44];
		guint32 v;

		memcpy (header, "RIFF", 4);
		v = 36 + bytes;
		memcpy (header + 4, &v, 4);
		memcpy (header + 8, "WAVEfmt ", 8);
		v = 16;
		memcpy (header + 16, &v, 4);
		header[20] = 1; header[21] = 0;                 /* PCM */
		header[22] = 2; header[23] = 0;                 /* stereo */
		v = AUDIO_RATE;
		memcpy (header + 24, &v, 4);
		v = AUDIO_RATE * 4;
		memcpy (header + 28, &v, 4);
		header[32] = 4; header[33] = 0;                 /* block align */
		header[34] = 16; header[35] = 0;                /* bits */
		memcpy (header + 36, "data", 4);
		memcpy (header + 40, &bytes, 4);
		g_byte_array_append (out, header, 44);
	}
	for (i = 0; i < frames; i++)
	{
		gdouble t = (gdouble) i / AUDIO_RATE;
		gdouble chord = sin (2.0 * G_PI * 220.0 * t) + sin (2.0 * G_PI * 277.2 * t) +
		                sin (2.0 * G_PI * (329.6 + 20.0 * sin (t)) * t);
		gint16  left = (gint16) (6000.0 * chord + 3000.0 * g_rand_double_range (rand, -1.0, 1.0));
		gint16  right = (gint16) (6000.0 * chord * 0.8 + 3000.0 * g_rand_double_range (rand, -1.0, 1.0));

		g_byte_array_append (out, (const guint8 *) &left, 2);
		g_byte_array_append (out, (const guint8 *) &right, 2);
	}
	return g_file_set_contents (path, (const gchar *) out->data, out->len, NULL);
}

/* The fixture in the user cache dir: the encoded file when ffmpeg can make
 * it (made once), else the WAV itself (and a note that WAV is measured). */
static gchar *
bench_music_fixture (const gchar *extension,
                     const gchar *codec)
{
	g_autofree gchar *dir = NULL;
	g_autofree gchar *wav = NULL;
	gchar            *encoded;
	g_autofree gchar *ffmpeg = NULL;

	dir = g_build_filename (g_get_user_cache_dir (), "lrg-bench-audio", NULL);
	g_mkdir_with_parents (dir, 0755);
	wav = g_build_filename (dir, "music.wav", NULL);
	encoded = g_strdup_printf ("%s/music.%s", dir, extension);
	if (g_file_test (encoded, G_FILE_TEST_EXISTS))
		return encoded;
	if (!g_file_test (wav, G_FILE_TEST_EXISTS) && !bench_write_wav (wav))
	{
		g_free (encoded);
		return NULL;
	}
	ffmpeg = g_find_program_in_path ("ffmpeg");
	if (ffmpeg != NULL)
	{
		const gchar *argv[] = { ffmpeg, "-loglevel", "error", "-y", "-i", wav,
		                        "-c:a", codec, "-b:a", "160k", encoded, NULL };
		gint         status = 0;

		if (g_spawn_sync (NULL, (gchar **) argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
		                  NULL, NULL, &status, NULL) &&
		    g_spawn_check_wait_status (status, NULL))
			return encoded;
	}
	g_printerr ("lrg-bench: no encoder for .%s (ffmpeg -c:a %s); streaming the WAV instead\n",
	            extension, codec);
	g_free (encoded);
	return g_strdup (wav);
}

static gpointer
music_setup_format (const gchar *extension,
                    const gchar *codec)
{
	g_autofree gchar *path = NULL;
	LrgMusicTrack    *track;

	if (!bench_audio_device ())
		return NULL;
	path = bench_music_fixture (extension, codec);
	if (path == NULL)
		return NULL;
	track = lrg_music_track_new_from_file (path, NULL);
	if (track == NULL)
		return NULL;
	lrg_music_track_set_looping (track, TRUE);
	lrg_music_track_play (track);
	return track;
}

static gpointer
music_ogg_setup (void)
{
	return music_setup_format ("ogg", "libvorbis");
}

static gpointer
music_mp3_setup (void)
{
	return music_setup_format ("mp3", "libmp3lame");
}

static void
music_frame (gpointer state,
             guint    frame_index)
{
	bench_light_frame (frame_index);
	bench_audio_spin ();
	if (state != NULL)
		lrg_music_track_update (LRG_MUSIC_TRACK (state));
}

static void
music_teardown (gpointer state)
{
	if (state == NULL)
		return;
	lrg_music_track_stop (LRG_MUSIC_TRACK (state));
	g_object_unref (state);
}

/* ==========================================================================
 * Driver
 * ========================================================================== */

static const BenchScenario scenarios[] = {
	{ "ecs-sprites", "4000 game objects with sprite components (world update + draw)",
	  ecs_setup, ecs_frame, ecs_teardown },
	{ "ecs-moving", "ecs-sprites moving in 60 Hz fixed steps, drawn at 144 Hz, interpolated",
	  moving_setup, moving_frame, moving_teardown },
	{ "ecs-moving-raw", "ecs-moving with render interpolation off",
	  moving_raw_setup, moving_frame, moving_teardown },
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
	{ "pacing", "1 ms of steady work per frame; run with --fps N to measure pacing",
	  pacing_setup, pacing_frame, pacing_teardown },
	{ "animator", "16 animators on 60-bone skeletons, crossfading every second",
	  anim_setup, anim_frame, anim_teardown },
	{ "skinned-models", "50 actors on one 3880-vertex 32-bone glTF model, GPU skinning",
	  skin_setup, skin_frame, skin_teardown },
	{ "skinned-models-cpu", "skinned-models with GPU skinning off (CPU skin + VBO upload)",
	  skin_cpu_setup, skin_frame, skin_teardown },
	{ "physics", "500 dynamic boxes in a 2D physics world, one step a frame",
	  phys_setup, phys_frame, phys_teardown },
	{ "tweens", "2000 ping-pong property tweens through a tween manager",
	  tween_setup, tween_frame, tween_teardown },
	{ "event-bus", "256 events a frame to 256 listeners over 32 event types",
	  bus_setup, bus_frame, bus_teardown },
	{ "transforms", "world position/rotation/scale of 128 transform chains 16 deep",
	  xform_setup, xform_frame, xform_teardown },
	{ "object-pool", "4000 pool acquisitions, 4 active walks, 4000 releases a frame",
	  pool_setup, pool_frame, pool_teardown },
	{ "portals", "64 portal-visibility updates a frame in a 24x24 sector grid",
	  portal_setup, portal_frame, portal_teardown },
	{ "autosave", "light frames, a 4000-key background save every 60 frames",
	  autosave_setup, autosave_frame, autosave_teardown },
	{ "autosave-sync", "as autosave, saving synchronously (lrg_save_manager_save)",
	  autosave_sync_setup, autosave_frame, autosave_teardown },
	{ "analytics", "an analytics event a frame, HTTP backend, 20 ms localhost server",
	  analytics_setup, analytics_frame, analytics_teardown },
	{ "despawn", "4000 sprite objects, 1000 removed and 1000 added every 60 frames",
	  despawn_setup, despawn_frame, despawn_teardown },
	{ "first-use", "water, billboards, grass, clouds, lit batch, skinned actors, 3 post effects first drawn at frame 100",
	  first_use_setup, first_use_frame, first_use_teardown },
	{ "first-use-queue", "first-use with the prewarm queue run in setup (a loading screen)",
	  first_use_queue_setup, first_use_frame, first_use_teardown },
	{ "first-use-prewarm", "first-use-queue plus one hidden draw of the scene in setup",
	  first_use_prewarm_setup, first_use_frame, first_use_teardown },
	{ "midi-synth", "3 ms frames, a streamed busy MIDI song (~60 voices, LrgMidiSynth)",
	  midi_setup, midi_frame, midi_teardown },
	{ "sound-bed", "3 ms frames, a streamed 6-layer sound bed, always crossfading, reverb",
	  bed_setup, bed_frame, bed_teardown },
	{ "procedural", "3 ms frames, a streamed LrgProceduralAudio (48 sine partials)",
	  procedural_setup, procedural_frame, procedural_teardown },
	{ "music-ogg", "3 ms frames, a streamed Vorbis track (LrgMusicTrack)",
	  music_ogg_setup, music_frame, music_teardown },
	{ "music-mp3", "3 ms frames, a streamed MP3 track (LrgMusicTrack)",
	  music_mp3_setup, music_frame, music_teardown },
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

/* Frame statistics over one run. "Lows" follow the usual definition:
 * the frame rate of the average of the slowest 1% (5%) of frames. Jitter
 * is the standard deviation of the change in frame time from one frame to
 * the next: steady 20 ms frames have none, alternating 10/30 ms frames a
 * lot, though both average 50 fps. */
typedef struct
{
	gdouble mean_ms;
	gdouble median_ms;
	gdouble p95_ms;
	gdouble p99_ms;
	gdouble max_ms;
	gdouble fps;
	gdouble low1_fps;
	gdouble low5_fps;
	gdouble jitter_ms;
	gdouble work_median_ms;
	gdouble work_p99_ms;
	gdouble work_max_ms;
} BenchStats;

/* Mean of the slowest @fraction of @sorted (ascending), as a frame rate. */
static gdouble
low_fps (const gdouble *sorted,
         guint          n,
         gdouble        fraction)
{
	guint   count;
	guint   i;
	gdouble sum;

	count = (guint) ((gdouble) n * fraction);
	if (count < 1)
		count = 1;
	sum = 0.0;
	for (i = n - count; i < n; i++)
		sum += sorted[i];
	return 1000.0 / (sum / (gdouble) count);
}

static void
bench_stats (const gdouble *frames,
             const gdouble *work,
             guint          n,
             BenchStats    *out)
{
	g_autofree gdouble *sorted = NULL;
	g_autofree gdouble *wsorted = NULL;
	gdouble             total;
	gdouble             dsum;
	gdouble             dsq;
	guint               i;

	sorted = g_memdup2 (frames, sizeof (gdouble) * n);
	wsorted = g_memdup2 (work, sizeof (gdouble) * n);
	qsort (sorted, n, sizeof (gdouble), compare_double);
	qsort (wsorted, n, sizeof (gdouble), compare_double);

	total = 0.0;
	for (i = 0; i < n; i++)
		total += frames[i];

	/* frame-to-frame change in frame time */
	dsum = 0.0;
	dsq = 0.0;
	for (i = 1; i < n; i++)
	{
		gdouble d = frames[i] - frames[i - 1];

		dsum += d;
		dsq += d * d;
	}

	out->mean_ms = total / n;
	out->median_ms = sorted[n / 2];
	out->p95_ms = sorted[(n * 95) / 100];
	out->p99_ms = sorted[MIN (n - 1, (n * 99) / 100)];
	out->max_ms = sorted[n - 1];
	out->fps = 1000.0 / out->mean_ms;
	out->low1_fps = low_fps (sorted, n, 0.01);
	out->low5_fps = low_fps (sorted, n, 0.05);
	out->jitter_ms = n > 2
		? sqrt (MAX (0.0, dsq / (n - 1) - (dsum / (n - 1)) * (dsum / (n - 1))))
		: 0.0;
	out->work_median_ms = wsorted[n / 2];
	out->work_p99_ms = wsorted[MIN (n - 1, (n * 99) / 100)];
	out->work_max_ms = wsorted[n - 1];
}

/* Runs one scenario: warm up, then time opt_frames frames. A frame's time
 * is the interval between consecutive returns from end-drawing (so it
 * includes the buffer swap and, with --fps, the frame limiter's wait),
 * the way a player experiences it. */
static void
run_scenario (GrlWindow           *window,
              const BenchScenario *sc,
              gboolean             yaml)
{
	g_autofree gdouble *samples = NULL;
	g_autofree gdouble *work = NULL;
	gpointer            state;
	BenchStats          st;
	gint64              last_end;
	guint               i;
	guint               n;
	g_autoptr(GrlColor) bg = NULL;

	n = (guint) opt_frames;
	samples = g_new0 (gdouble, n);
	work = g_new0 (gdouble, n);
	bg = grl_color_new (20, 20, 30, 255);
	state = sc->setup ();

	last_end = 0;
	for (i = 0; i < n + BENCH_WARMUP_FRAMES; i++)
	{
		gint64 w0;
		gint64 w1;
		gint64 t1;

		/* The scenario's own CPU work (w0..w1) is also recorded: its update
		 * and draw submission without the driver's flush and swap, which
		 * dominate under a software rasterizer. */
		grl_window_begin_drawing (window);
		grl_draw_clear_background (bg);
		w0 = g_get_monotonic_time ();
		sc->frame (state, i);
		w1 = g_get_monotonic_time ();
		grl_window_end_drawing (window);
		t1 = g_get_monotonic_time ();

		if (i >= BENCH_WARMUP_FRAMES)
		{
			samples[i - BENCH_WARMUP_FRAMES] = (gdouble) (t1 - last_end) / 1000.0;
			work[i - BENCH_WARMUP_FRAMES] = (gdouble) (w1 - w0) / 1000.0;
		}
		last_end = t1;
	}

	sc->teardown (state);

	bench_stats (samples, work, n, &st);
	if (yaml)
		g_print ("- scenario: %s\n  frames: %u\n  target_fps: %d\n"
		         "  fps: %.1f\n  low1_fps: %.1f\n  low5_fps: %.1f\n"
		         "  mean_ms: %.3f\n  median_ms: %.3f\n  p95_ms: %.3f\n"
		         "  p99_ms: %.3f\n  max_ms: %.3f\n  jitter_ms: %.3f\n"
		         "  work_median_ms: %.3f\n  work_p99_ms: %.3f\n  work_max_ms: %.3f\n",
		         sc->name, n, opt_fps, st.fps, st.low1_fps, st.low5_fps,
		         st.mean_ms, st.median_ms, st.p95_ms, st.p99_ms, st.max_ms,
		         st.jitter_ms, st.work_median_ms, st.work_p99_ms, st.work_max_ms);
	else
		g_print ("%-14s %8.1f fps  1%% low %8.1f  5%% low %8.1f  p99 %7.3f ms"
		         "  max %7.3f ms  jitter %6.3f ms | work median %7.3f p99 %7.3f max %7.3f ms\n",
		         sc->name, st.fps, st.low1_fps, st.low5_fps, st.p99_ms,
		         st.max_ms, st.jitter_ms, st.work_median_ms, st.work_p99_ms, st.work_max_ms);
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
		{ "fps", 0, 0, G_OPTION_ARG_INT, &opt_fps,
		  "Cap the frame rate (default 0: uncapped); measures pacing", "N" },
		{ "no-pin", 0, 0, G_OPTION_ARG_NONE, &opt_no_pin,
		  "Do not keep the main thread on performance cores (as LRG_CPU_AFFINITY=none)", NULL },
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
		"  lrg-bench -s pacing --fps 60    frame pacing at a 60 fps cap\n"
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

	/* What LrgEngine does at startup: keep the main thread (and the GL
	 * driver threads the window starts) on a hybrid CPU's fast cores */
	if (!opt_no_pin)
		lrg_cpu_affinity_pin_to_performance_cores (NULL);

	SetTraceLogLevel (LOG_WARNING);
	window = grl_window_new (BENCH_WIDTH, BENCH_HEIGHT, "lrg-bench");
	grl_window_set_target_fps (window, opt_fps > 0 ? opt_fps : 0);

	for (i = 0; i < G_N_ELEMENTS (scenarios); i++)
		if (scenario_selected (scenarios[i].name))
			run_scenario (window, &scenarios[i], yaml);

	return 0;
}
