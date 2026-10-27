/* test-interpolation.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Render interpolation through LrgGameTemplate: smoothness of motion
 * simulated at 60 steps a second and drawn at 144 Hz, the 60 Hz case,
 * teleports, opt-outs, transforms, shapes and cameras.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

#define EPSILON  (0.0001f)
#define SPEED    (60.0f)   /* units per second: one unit per 1/60 s step */

/* ==========================================================================
 * A minimal game host and a template that moves things in fixed_update
 * ========================================================================== */

typedef GObject InterpHost;
typedef GObjectClass InterpHostClass;

static void interp_host_iface_init (LrgGameHostInterface *iface);
GType interp_host_get_type (void);
G_DEFINE_TYPE_WITH_CODE (InterpHost, interp_host, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_GAME_HOST,
                                                interp_host_iface_init))

static LrgEngine *
interp_host_get_engine (LrgGameHost *host)
{
    return lrg_engine_get_default ();
}

static void
interp_host_iface_init (LrgGameHostInterface *iface)
{
    iface->get_engine = interp_host_get_engine;
}

static void
interp_host_class_init (InterpHostClass *klass)
{
}

static void
interp_host_init (InterpHost *self)
{
}

/* Each fixed step moves every registered thing right by SPEED * step */
typedef struct
{
    LrgGameTemplate        parent_instance;
    GrlEntity             *entity;     /* moved with grl_entity_move_xy */
    LrgTransformComponent *transform;  /* moved with translate */
    LrgShape2D            *shape2d;
    LrgShape3D            *shape3d;
    LrgCamera2D           *camera2d;   /* follows the entity */
    LrgCamera3D           *camera3d;   /* follows the entity */
    gboolean               follow;     /* template camera follow target */
    gboolean               teleport_next;
} InterpTemplate;

typedef LrgGameTemplateClass InterpTemplateClass;

GType interp_template_get_type (void);
G_DEFINE_TYPE (InterpTemplate, interp_template, LRG_TYPE_GAME_TEMPLATE)

static void
interp_template_fixed_update (LrgGameTemplate *template,
                              gdouble          delta)
{
    InterpTemplate *self = (InterpTemplate *) template;
    gfloat          dx = SPEED * (gfloat) delta;
    gfloat          x;

    if (self->entity != NULL)
    {
        if (self->teleport_next)
        {
            grl_entity_teleport (self->entity, 1000.0f, 0.0f);
            self->teleport_next = FALSE;
        }
        grl_entity_move_xy (self->entity, SPEED, 0.0f, (gfloat) delta);
        x = grl_entity_get_x (self->entity);

        if (self->camera2d != NULL)
            lrg_camera2d_set_target_xy (self->camera2d, x, 0.0f);
        if (self->camera3d != NULL)
        {
            lrg_camera3d_set_position_xyz (self->camera3d, x, 5.0f, 10.0f);
            lrg_camera3d_set_target_xyz (self->camera3d, x, 0.0f, 0.0f);
        }
        if (self->follow)
            lrg_game_template_update_camera_follow_target (template, x, 0.0f);
    }

    if (self->transform != NULL)
    {
        g_autoptr(GrlVector2) offset = grl_vector2_new (dx, 0.0f);

        lrg_transform_component_translate (self->transform, offset);
        lrg_transform_component_rotate (self->transform, 2.0f);
    }

    if (self->shape2d != NULL)
        lrg_shape2d_set_x (self->shape2d, lrg_shape2d_get_x (self->shape2d) + dx);

    if (self->shape3d != NULL)
        lrg_shape3d_set_position_xyz (self->shape3d,
                                      lrg_shape3d_get_x (self->shape3d) + dx,
                                      0.0f, 0.0f);
}

static void
interp_template_class_init (InterpTemplateClass *klass)
{
    klass->fixed_update = interp_template_fixed_update;
    klass->create_initial_state = NULL;
}

static void
interp_template_init (InterpTemplate *self)
{
}

typedef struct
{
    GObject        *host;
    InterpTemplate *game;
} Fixture;

static void
fixture_set_up (Fixture       *fixture,
                gconstpointer  user_data)
{
    g_autoptr(GError) error = NULL;

    fixture->host = g_object_new (interp_host_get_type (), NULL);
    fixture->game = g_object_new (interp_template_get_type (),
                                  "fixed-timestep", 1.0 / 60.0, NULL);

    g_assert_true (lrg_engine_startup (lrg_engine_get_default (), &error));
    g_assert_no_error (error);
    g_test_expect_message (LRG_LOG_DOMAIN_TEMPLATE, G_LOG_LEVEL_WARNING,
                           "*No initial state created*");
    g_assert_true (lrg_game_template_startup (LRG_GAME_TEMPLATE (fixture->game),
                                              LRG_GAME_HOST (fixture->host),
                                              &error));
    g_test_assert_expected_messages ();
    g_assert_no_error (error);
}

static void
fixture_tear_down (Fixture       *fixture,
                   gconstpointer  user_data)
{
    lrg_game_template_shutdown_game (LRG_GAME_TEMPLATE (fixture->game));
    g_clear_object (&fixture->game);
    g_clear_object (&fixture->host);
    lrg_engine_shutdown (lrg_engine_get_default ());

    /* shutting the game down turns the process-wide clock off */
    g_assert_false (grl_interpolation_get_enabled ());
}

/* ==========================================================================
 * Smoothness: judder = stddev / mean of the drawn per-frame displacement
 * ========================================================================== */

/*
 * run_display:
 *
 * Drives @frames host frames of @frame_delta seconds and records where
 * the entity is drawn after each. Returns the judder of the drawn motion:
 * the standard deviation of the per-frame displacement relative to its
 * mean (0 = perfectly even motion).
 */
static gdouble
run_display (InterpTemplate *game,
             gdouble         frame_delta,
             guint           frames,
             gdouble        *out_mean)
{
    g_autofree gdouble *step = g_new0 (gdouble, frames);
    gfloat  last_x;
    gfloat  x;
    gdouble mean;
    gdouble var;
    guint   i;

    grl_entity_get_render_position_xy (game->entity, &last_x, NULL);
    for (i = 0; i < frames; i++)
    {
        lrg_game_template_update (LRG_GAME_TEMPLATE (game), frame_delta);
        grl_entity_get_render_position_xy (game->entity, &x, NULL);
        step[i] = (gdouble) (x - last_x);
        last_x = x;
    }

    mean = 0.0;
    for (i = 0; i < frames; i++)
        mean += step[i];
    mean /= (gdouble) frames;

    var = 0.0;
    for (i = 0; i < frames; i++)
        var += (step[i] - mean) * (step[i] - mean);
    var /= (gdouble) frames;

    if (out_mean != NULL)
        *out_mean = mean;
    return sqrt (var) / mean;
}

static void
test_interp_judder_144hz (Fixture       *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(GrlEntity) entity = grl_entity_new_at (0.0f, 0.0f);
    gdouble judder_on;
    gdouble judder_off;
    gdouble mean;

    fixture->game->entity = entity;

    /* settle, then measure two seconds of a 144 Hz display */
    run_display (fixture->game, 1.0 / 144.0, 30, NULL);
    judder_on = run_display (fixture->game, 1.0 / 144.0, 288, &mean);
    g_assert_cmpfloat_with_epsilon (mean, SPEED / 144.0, 0.01);

    g_object_set (fixture->game, "interpolate", FALSE, NULL);
    run_display (fixture->game, 1.0 / 144.0, 30, NULL);
    judder_off = run_display (fixture->game, 1.0 / 144.0, 288, NULL);

    g_test_message ("144 Hz display, 60 Hz steps: judder %.4f interpolated, %.4f raw",
                    judder_on, judder_off);
    if (g_test_verbose ())
        g_printerr ("judder-144hz: interpolated %.6f raw %.6f\n", judder_on, judder_off);

    /* raw: 0 or 1 step per frame -> displacement 0 or 1 unit, very uneven */
    g_assert_cmpfloat (judder_off, >, 0.5);
    /* interpolated: even motion (only float rounding left) */
    g_assert_cmpfloat (judder_on, <, 0.01);

    fixture->game->entity = NULL;
}

static void
test_interp_judder_60hz (Fixture       *fixture,
                         gconstpointer  user_data)
{
    g_autoptr(GrlEntity) entity = grl_entity_new_at (0.0f, 0.0f);
    gdouble judder_on;
    gdouble judder_off;
    gdouble mean;
    gfloat  x;

    fixture->game->entity = entity;

    run_display (fixture->game, 1.0 / 60.0, 10, NULL);
    judder_on = run_display (fixture->game, 1.0 / 60.0, 120, &mean);
    g_assert_cmpfloat_with_epsilon (mean, 1.0, 0.0001);

    /* one step per frame and an empty accumulator: alpha 0, so the entity
     * is drawn where the latest step began, one step behind */
    grl_entity_get_render_position_xy (entity, &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, grl_entity_get_x (entity) - 1.0f, EPSILON);

    /* switching off jumps once to the true pose; measure after that */
    g_object_set (fixture->game, "interpolate", FALSE, NULL);
    run_display (fixture->game, 1.0 / 60.0, 10, NULL);
    judder_off = run_display (fixture->game, 1.0 / 60.0, 120, NULL);
    grl_entity_get_render_position_xy (entity, &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, grl_entity_get_x (entity), EPSILON);

    if (g_test_verbose ())
        g_printerr ("judder-60hz: interpolated %.6f raw %.6f\n", judder_on, judder_off);

    g_assert_cmpfloat (judder_on, <, 0.0001);
    g_assert_cmpfloat (judder_off, <, 0.0001);

    fixture->game->entity = NULL;
}

/* With a variable timestep nothing is blended */
static void
test_interp_variable_timestep (Fixture       *fixture,
                               gconstpointer  user_data)
{
    g_autoptr(GrlEntity) entity = grl_entity_new_at (0.0f, 0.0f);
    gfloat x;

    g_object_set (fixture->game, "use-fixed-timestep", FALSE, NULL);
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 144.0);
    g_assert_false (grl_interpolation_get_enabled ());

    grl_entity_set_x (entity, 42.0f);
    grl_entity_get_render_position_xy (entity, &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, 42.0f, EPSILON);
    g_assert_cmpfloat_with_epsilon (grl_interpolation_get_alpha (), 1.0f, EPSILON);
}

/* ==========================================================================
 * Teleports, spawns and opt-outs
 * ========================================================================== */

static void
test_interp_teleport (Fixture       *fixture,
                      gconstpointer  user_data)
{
    g_autoptr(GrlEntity) entity = grl_entity_new_at (0.0f, 0.0f);
    gfloat x;

    fixture->game->entity = entity;
    run_display (fixture->game, 1.0 / 144.0, 20, NULL);

    /* the step that teleports (then moves on) draws at the new place */
    fixture->game->teleport_next = TRUE;
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 60.0);
    grl_entity_get_render_position_xy (entity, &x, NULL);
    g_assert_cmpfloat (x, >=, 1000.0f);
    g_assert_cmpfloat (x, <=, 1001.0f + EPSILON);

    fixture->game->entity = NULL;
}

static void
test_interp_world_add_resets (Fixture       *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(LrgWorld) world = lrg_world_new ();
    g_autoptr(LrgGameObject) object = lrg_game_object_new_at (0.0f, 0.0f);
    gfloat x;

    fixture->game->entity = GRL_ENTITY (object);
    run_display (fixture->game, 1.0 / 144.0, 20, NULL);
    fixture->game->entity = NULL;

    /* a pooled object moved far away and (re)added is drawn where it is */
    grl_interpolation_begin_step ();
    grl_entity_set_x (GRL_ENTITY (object), 5000.0f);
    lrg_world_add_object (world, object);
    grl_interpolation_end_step ();
    grl_entity_get_render_position_xy (GRL_ENTITY (object), &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, 5000.0f, EPSILON);
}

static void
test_interp_opt_out (Fixture       *fixture,
                     gconstpointer  user_data)
{
    g_autoptr(GrlEntity) entity = grl_entity_new_at (0.0f, 0.0f);
    gdouble judder;
    gfloat  x;

    grl_entity_set_interpolate (entity, FALSE);
    fixture->game->entity = entity;

    /* opted out: drawn at the true pose, so the raw cadence is back */
    judder = run_display (fixture->game, 1.0 / 144.0, 144, NULL);
    grl_entity_get_render_position_xy (entity, &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, grl_entity_get_x (entity), EPSILON);
    g_assert_cmpfloat (judder, >, 0.5);

    fixture->game->entity = NULL;
}

/* ==========================================================================
 * Transforms and shapes
 * ========================================================================== */

static void
test_interp_transform (Fixture       *fixture,
                       gconstpointer  user_data)
{
    g_autoptr(LrgTransformComponent) parent = lrg_transform_component_new ();
    g_autoptr(LrgTransformComponent) child = lrg_transform_component_new_at (10.0f, 0.0f);
    g_autoptr(GrlVector2) world = NULL;
    gfloat alpha;
    gfloat x;
    gfloat y;
    gfloat rotation;

    lrg_transform_component_set_parent (child, parent);
    fixture->game->transform = parent;

    /* settle on a frame that leaves part of a step in the accumulator */
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 60.0);
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 144.0);
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 144.0);
    alpha = grl_interpolation_get_alpha ();
    g_assert_cmpfloat (alpha, >, 0.0f);
    g_assert_cmpfloat (alpha, <, 1.0f);

    /* the parent is drawn a fraction of the latest step behind */
    lrg_transform_component_get_render_transform (parent, &x, NULL, &rotation, NULL, NULL);
    g_assert_cmpfloat_with_epsilon (x, lrg_transform_component_get_local_x (parent) - (1.0f - alpha),
                                    EPSILON);
    g_assert_cmpfloat_with_epsilon (rotation,
                                    lrg_transform_component_get_local_rotation (parent) - 2.0f * (1.0f - alpha),
                                    EPSILON);

    /* the child follows the drawn parent (rotated around it) */
    lrg_transform_component_get_render_position (child, &x, &y);
    {
        gfloat rad = rotation * (gfloat) (G_PI / 180.0);
        gfloat px;

        lrg_transform_component_get_render_position (parent, &px, NULL);
        g_assert_cmpfloat_with_epsilon (x, px + 10.0f * cosf (rad), 0.001f);
        g_assert_cmpfloat_with_epsilon (y, 10.0f * sinf (rad), 0.001f);
    }

    /* true state unchanged */
    world = lrg_transform_component_get_world_position (parent);
    g_assert_cmpfloat_with_epsilon (world->x, lrg_transform_component_get_local_x (parent), EPSILON);

    /* teleport: drawn at the new place at once */
    lrg_transform_component_teleport (parent, 300.0f, 0.0f);
    lrg_transform_component_get_render_position (parent, &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, 300.0f, EPSILON);

    /* opt-out: drawn at the true transform */
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 60.0);
    lrg_transform_component_set_interpolate (parent, FALSE);
    g_assert_false (lrg_transform_component_get_interpolate (parent));
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 60.0);
    lrg_transform_component_get_render_position (parent, &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, lrg_transform_component_get_local_x (parent), EPSILON);

    fixture->game->transform = NULL;
}

static void
test_interp_transform_sync_teleport (Fixture       *fixture,
                                     gconstpointer  user_data)
{
    g_autoptr(LrgGameObject) object = lrg_game_object_new ();
    g_autoptr(LrgTransformComponent) transform = lrg_transform_component_new ();
    gfloat x;

    lrg_game_object_add_component (object, LRG_COMPONENT (transform));
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 60.0);

    /* a teleported transform synced in the same step teleports the entity */
    grl_interpolation_begin_step ();
    lrg_transform_component_teleport (transform, 800.0f, 0.0f);
    lrg_transform_component_sync_to_entity (transform);
    grl_interpolation_end_step ();
    grl_interpolation_set_alpha (0.0f);
    grl_entity_get_render_position_xy (GRL_ENTITY (object), &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, 800.0f, EPSILON);
}

static void
test_interp_shapes (Fixture       *fixture,
                    gconstpointer  user_data)
{
    g_autoptr(LrgRectangle2D) rect = lrg_rectangle2d_new ();
    g_autoptr(LrgCube3D) cube = lrg_cube3d_new ();
    GrlVector3 position;
    GrlVector3 rotation;
    gfloat     alpha;
    gfloat     x;

    fixture->game->shape2d = LRG_SHAPE2D (rect);
    fixture->game->shape3d = LRG_SHAPE3D (cube);

    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 60.0);
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 144.0);
    alpha = grl_interpolation_get_alpha ();

    lrg_shape2d_get_render_position (LRG_SHAPE2D (rect), &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, lrg_shape2d_get_x (LRG_SHAPE2D (rect)) - (1.0f - alpha),
                                    EPSILON);
    lrg_shape3d_get_render_transform (LRG_SHAPE3D (cube), &position, NULL, NULL);
    g_assert_cmpfloat_with_epsilon (position.x, lrg_shape3d_get_x (LRG_SHAPE3D (cube)) - (1.0f - alpha),
                                    EPSILON);

    /* Euler angles take the shorter arc across +-pi */
    fixture->game->shape3d = NULL;
    lrg_shape3d_set_rotation_xyz (LRG_SHAPE3D (cube), 0.0f, 3.0f, 0.0f);
    lrg_shape3d_reset_interpolation (LRG_SHAPE3D (cube));
    grl_interpolation_begin_step ();
    lrg_shape3d_set_rotation_xyz (LRG_SHAPE3D (cube), 0.0f, -3.0f, 0.0f);
    grl_interpolation_end_step ();
    grl_interpolation_set_alpha (0.5f);
    lrg_shape3d_get_render_transform (LRG_SHAPE3D (cube), NULL, &rotation, NULL);
    g_assert_cmpfloat_with_epsilon (fabsf (rotation.y), (gfloat) G_PI, 0.001f);

    /* opted out */
    lrg_shape2d_set_interpolate (LRG_SHAPE2D (rect), FALSE);
    lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 144.0);
    lrg_shape2d_get_render_position (LRG_SHAPE2D (rect), &x, NULL);
    g_assert_cmpfloat_with_epsilon (x, lrg_shape2d_get_x (LRG_SHAPE2D (rect)), EPSILON);

    fixture->game->shape2d = NULL;
}

/* ==========================================================================
 * Cameras follow the drawn target
 * ========================================================================== */

/* The followed entity stays put on screen, frame after frame, at 144 Hz */
static void
test_interp_camera2d_follow (Fixture       *fixture,
                             gconstpointer  user_data)
{
    g_autoptr(GrlEntity) entity = grl_entity_new_at (0.0f, 0.0f);
    g_autoptr(LrgCamera2D) camera = lrg_camera2d_new ();
    GrlVector3 world;
    GrlVector2 screen;
    gfloat     x;
    gfloat     fx;
    gfloat     fy;
    guint      i;

    lrg_camera2d_set_offset_xy (camera, 160.0f, 120.0f);
    fixture->game->entity = entity;
    fixture->game->camera2d = camera;
    fixture->game->follow = TRUE;
    lrg_game_template_set_camera_follow (LRG_GAME_TEMPLATE (fixture->game), TRUE, 0.0f);

    for (i = 0; i < 100; i++)
    {
        lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 144.0);
        if (i < 10)
            continue;

        grl_entity_get_render_position_xy (entity, &x, NULL);
        world.x = x;
        world.y = 0.0f;
        world.z = 0.0f;
        lrg_camera_world_to_screen (LRG_CAMERA (camera), &world, &screen);
        g_assert_cmpfloat_with_epsilon (screen.x, 160.0f, 0.001f);

        /* the template's follow target is the drawn one too */
        lrg_game_template_get_camera_position (LRG_GAME_TEMPLATE (fixture->game), &fx, &fy);
        g_assert_cmpfloat_with_epsilon (fx, x, EPSILON);
    }

    /* the true camera target is the true entity position */
    {
        g_autoptr(GrlVector2) target = lrg_camera2d_get_target (camera);

        g_assert_cmpfloat_with_epsilon (target->x, grl_entity_get_x (entity), EPSILON);
    }

    /* a cut */
    lrg_camera2d_reset_interpolation (camera);
    world.x = grl_entity_get_x (entity);
    lrg_camera_world_to_screen (LRG_CAMERA (camera), &world, &screen);
    g_assert_cmpfloat_with_epsilon (screen.x, 160.0f, 0.001f);

    fixture->game->entity = NULL;
    fixture->game->camera2d = NULL;
}

static void
test_interp_camera3d_follow (Fixture       *fixture,
                             gconstpointer  user_data)
{
    g_autoptr(GrlEntity) entity = grl_entity_new_at (0.0f, 0.0f);
    g_autoptr(LrgCamera3D) camera = lrg_camera3d_new ();
    GrlVector3 eye;
    GrlVector3 target;
    gfloat     x;
    guint      i;

    fixture->game->entity = entity;
    fixture->game->camera3d = camera;

    for (i = 0; i < 50; i++)
    {
        lrg_game_template_update (LRG_GAME_TEMPLATE (fixture->game), 1.0 / 144.0);
        if (i < 10)
            continue;

        grl_entity_get_render_position_xy (entity, &x, NULL);
        lrg_camera3d_get_render_pose (camera, &eye, &target);
        g_assert_cmpfloat_with_epsilon (eye.x, x, EPSILON);
        g_assert_cmpfloat_with_epsilon (target.x, x, EPSILON);
        g_assert_cmpfloat_with_epsilon (eye.z, 10.0f, EPSILON);
    }

    lrg_camera3d_set_interpolate (camera, FALSE);
    lrg_camera3d_get_render_pose (camera, &eye, NULL);
    g_assert_cmpfloat_with_epsilon (eye.x, grl_entity_get_x (entity), EPSILON);

    fixture->game->entity = NULL;
    fixture->game->camera3d = NULL;
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

#define ADD(path, func) \
    g_test_add (path, Fixture, NULL, fixture_set_up, func, fixture_tear_down)

    ADD ("/interpolation/judder-144hz", test_interp_judder_144hz);
    ADD ("/interpolation/judder-60hz", test_interp_judder_60hz);
    ADD ("/interpolation/variable-timestep", test_interp_variable_timestep);
    ADD ("/interpolation/teleport", test_interp_teleport);
    ADD ("/interpolation/world-add-resets", test_interp_world_add_resets);
    ADD ("/interpolation/opt-out", test_interp_opt_out);
    ADD ("/interpolation/transform", test_interp_transform);
    ADD ("/interpolation/transform-sync-teleport", test_interp_transform_sync_teleport);
    ADD ("/interpolation/shapes", test_interp_shapes);
    ADD ("/interpolation/camera2d-follow", test_interp_camera2d_follow);
    ADD ("/interpolation/camera3d-follow", test_interp_camera3d_follow);

#undef ADD

    return g_test_run ();
}
