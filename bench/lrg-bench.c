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
	{ "graylib-draw", "20000 graylib rectangle/texture draws + 200 texts",
	  grl_draw_setup, grl_draw_frame, grl_draw_teardown },
	{ "gui", "immediate-mode GUI panel, 64 rows of widgets",
	  gui_setup, gui_frame, gui_teardown },
	{ "animator", "16 animators on 60-bone skeletons, crossfading every second",
	  anim_setup, anim_frame, anim_teardown },
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
