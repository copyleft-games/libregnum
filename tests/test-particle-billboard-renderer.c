/* test-particle-billboard-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests of LrgParticleBillboardRenderer's geometry (build()) and
 * lrg_particle_system_foreach_active().  draw() needs a window and is
 * presentation only.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

#define FPV  LRG_PARTICLE_BILLBOARD_FLOATS_PER_VERTEX
#define FPQ  LRG_PARTICLE_BILLBOARD_FLOATS_PER_QUAD
#define EPS  (1e-4)

/* ------------------------------------------------------------------------ */
/* A system subclass that sets the rotation on spawn                        */
/* ------------------------------------------------------------------------ */

#define TEST_TYPE_ROTATING_SYSTEM (test_rotating_system_get_type ())
G_DECLARE_FINAL_TYPE (TestRotatingSystem, test_rotating_system, TEST, ROTATING_SYSTEM, LrgParticleSystem)

struct _TestRotatingSystem
{
    LrgParticleSystem parent_instance;
    gfloat            rotation;
};

G_DEFINE_TYPE (TestRotatingSystem, test_rotating_system, LRG_TYPE_PARTICLE_SYSTEM)

static void
test_rotating_system_spawn (LrgParticleSystem *system,
                            LrgParticle       *particle)
{
    particle->rotation = TEST_ROTATING_SYSTEM (system)->rotation;
}

static void
test_rotating_system_class_init (TestRotatingSystemClass *klass)
{
    LRG_PARTICLE_SYSTEM_CLASS (klass)->on_particle_spawn = test_rotating_system_spawn;
}

static void
test_rotating_system_init (TestRotatingSystem *self)
{
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

/* make_emitter:
 * A point emitter with fixed size, colour and lifetime and no speed, so
 * emit_at() places particles exactly. */
static LrgParticleEmitter *
make_emitter (gfloat size,
              gfloat life)
{
    LrgParticleEmitter *emitter = lrg_particle_emitter_new ();

    lrg_particle_emitter_set_emission_shape (emitter, LRG_EMISSION_SHAPE_POINT);
    lrg_particle_emitter_set_position (emitter, 0.0f, 0.0f, 0.0f);
    lrg_particle_emitter_set_initial_speed (emitter, 0.0f, 0.0f);
    lrg_particle_emitter_set_initial_size (emitter, size, size);
    lrg_particle_emitter_set_initial_lifetime (emitter, life, life);
    lrg_particle_emitter_set_start_color (emitter, 1.0f, 0.5f, 0.25f, 0.75f);
    lrg_particle_emitter_set_emission_rate (emitter, 0.0f);
    return emitter;
}

static LrgParticleSystem *
make_system (gfloat size,
             gfloat life)
{
    LrgParticleSystem *system = lrg_particle_system_new (64);
    g_autoptr(LrgParticleEmitter) emitter = make_emitter (size, life);

    lrg_particle_system_add_emitter (system, emitter);
    return system;
}

static const gfloat eye_z[3] = { 0.0f, 0.0f, 10.0f };
static const gfloat world_up[3] = { 0.0f, 1.0f, 0.0f };

static void
assert_vertex (const gfloat *v,
               gfloat        x,
               gfloat        y,
               gfloat        z,
               gfloat        u,
               gfloat        tv)
{
    g_assert_cmpfloat_with_epsilon (v[0], x, EPS);
    g_assert_cmpfloat_with_epsilon (v[1], y, EPS);
    g_assert_cmpfloat_with_epsilon (v[2], z, EPS);
    g_assert_cmpfloat_with_epsilon (v[3], u, EPS);
    g_assert_cmpfloat_with_epsilon (v[4], tv, EPS);
}

/* ------------------------------------------------------------------------ */
/* foreach_active                                                           */
/* ------------------------------------------------------------------------ */

static void
count_cb (const LrgParticle *particle,
          gpointer           user_data)
{
    guint *count = user_data;

    g_assert_true (particle->alive);
    (*count)++;
}

static void
test_foreach_active (void)
{
    g_autoptr(LrgParticleSystem) system = make_system (1.0f, 1.0f);
    guint count = 0;

    lrg_particle_system_foreach_active (system, count_cb, &count);
    g_assert_cmpuint (count, ==, 0);

    g_assert_cmpuint (lrg_particle_system_emit_at (system, 0, 0, 0, 5), ==, 5);
    lrg_particle_system_foreach_active (system, count_cb, &count);
    g_assert_cmpuint (count, ==, 5);

    /* Dead particles are dropped by the update. */
    lrg_particle_system_play (system);
    lrg_particle_system_update (system, 2.0f);
    count = 0;
    lrg_particle_system_foreach_active (system, count_cb, &count);
    g_assert_cmpuint (count, ==, 0);
}

static void
test_emit_calls_spawn_hook (void)
{
    g_autoptr(TestRotatingSystem) system = NULL;
    g_autoptr(LrgParticleEmitter) emitter = make_emitter (1.0f, 1.0f);
    gfloat vertices[FPQ];
    g_autoptr(LrgParticleBillboardRenderer) renderer = lrg_particle_billboard_renderer_new ();

    /* A subclass built with g_object_new() gets its pool from the
     * construct-only capacity. */
    system = (TestRotatingSystem *)g_object_new (TEST_TYPE_ROTATING_SYSTEM,
                                                 "max-particles", 8, NULL);
    g_assert_cmpuint (lrg_particle_system_get_max_particles (LRG_PARTICLE_SYSTEM (system)), ==, 8);
    system->rotation = 90.0f;
    lrg_particle_system_add_emitter (LRG_PARTICLE_SYSTEM (system), emitter);
    g_assert_cmpuint (lrg_particle_system_emit_at (LRG_PARTICLE_SYSTEM (system), 0, 0, 0, 1), ==, 1);

    /* A 90 degree turn: the bottom-left corner moves to bottom-right of
     * an unrotated quad, i.e. (+0.5, -0.5) of size 1. */
    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, LRG_PARTICLE_SYSTEM (system),
                                                             eye_z, world_up, vertices, 1), ==, 1);
    g_assert_cmpfloat_with_epsilon (vertices[0], 0.5f, EPS);
    g_assert_cmpfloat_with_epsilon (vertices[1], -0.5f, EPS);
}

/* ------------------------------------------------------------------------ */
/* build                                                                    */
/* ------------------------------------------------------------------------ */

static void
test_build_empty (void)
{
    g_autoptr(LrgParticleSystem) system = make_system (1.0f, 1.0f);
    g_autoptr(LrgParticleBillboardRenderer) renderer = lrg_particle_billboard_renderer_new ();
    gfloat vertices[FPQ];

    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_z, world_up,
                                                             vertices, 1), ==, 0);
    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_z, world_up,
                                                             NULL, 100), ==, 0);
}

static void
test_build_camera_facing (void)
{
    g_autoptr(LrgParticleSystem) system = make_system (2.0f, 1.0f);
    g_autoptr(LrgParticleBillboardRenderer) renderer = lrg_particle_billboard_renderer_new ();
    gfloat vertices[FPQ];
    const gfloat eye_x[3] = { 10.0f, 1.0f, 0.0f };
    const gfloat eye_front[3] = { 0.0f, 1.0f, 10.0f };
    guint i;

    lrg_particle_system_emit_at (system, 0.0f, 1.0f, 0.0f, 1);

    /* Eye on +Z: the quad lies in the XY plane, 2 x 2, centred. */
    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_front, world_up,
                                                             vertices, 1), ==, 1);
    assert_vertex (vertices + 0 * FPV, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    assert_vertex (vertices + 1 * FPV,  1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    assert_vertex (vertices + 2 * FPV,  1.0f, 2.0f, 0.0f, 1.0f, 0.0f);
    assert_vertex (vertices + 3 * FPV, -1.0f, 2.0f, 0.0f, 0.0f, 0.0f);

    /* Colour of the particle on every vertex. */
    for (i = 0; i < 4; i++)
    {
        const gfloat *v = vertices + i * FPV;

        g_assert_cmpfloat_with_epsilon (v[5], 1.0f, EPS);
        g_assert_cmpfloat_with_epsilon (v[6], 0.5f, EPS);
        g_assert_cmpfloat_with_epsilon (v[7], 0.25f, EPS);
        g_assert_cmpfloat_with_epsilon (v[8], 0.75f, EPS);
    }

    /* Eye on +X: the quad turns into the ZY plane, its right is -Z. */
    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_x, world_up,
                                                             vertices, 1), ==, 1);
    assert_vertex (vertices + 0 * FPV, 0.0f, 0.0f,  1.0f, 0.0f, 1.0f);
    assert_vertex (vertices + 1 * FPV, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f);
    assert_vertex (vertices + 2 * FPV, 0.0f, 2.0f, -1.0f, 1.0f, 0.0f);
}

static void
test_build_eye_above (void)
{
    g_autoptr(LrgParticleSystem) system = make_system (1.0f, 1.0f);
    g_autoptr(LrgParticleBillboardRenderer) renderer = lrg_particle_billboard_renderer_new ();
    gfloat vertices[FPQ];
    const gfloat eye_up[3] = { 0.0f, 10.0f, 0.0f };
    gfloat ex;
    gfloat ez;
    guint i;

    /* Up parallel to the facing direction must still give a square quad
     * of the particle's size, finite and lying flat. */
    lrg_particle_system_emit_at (system, 0.0f, 0.0f, 0.0f, 1);
    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_up, world_up,
                                                             vertices, 1), ==, 1);
    for (i = 0; i < 4; i++)
    {
        g_assert_true (isfinite (vertices[i * FPV]));
        g_assert_cmpfloat_with_epsilon (vertices[i * FPV + 1], 0.0f, EPS);
    }
    ex = vertices[FPV] - vertices[0];
    ez = vertices[FPV + 2] - vertices[2];
    g_assert_cmpfloat_with_epsilon (sqrtf (ex * ex + ez * ez), 1.0f, EPS);
}

static void
test_build_flipbook (void)
{
    g_autoptr(LrgParticleSystem) system = make_system (1.0f, 10.0f);
    g_autoptr(LrgParticleBillboardRenderer) renderer = lrg_particle_billboard_renderer_new ();
    gfloat vertices[FPQ];
    guint columns;
    guint rows;
    gfloat fps;

    lrg_particle_billboard_renderer_get_frames (renderer, &columns, &rows, &fps);
    g_assert_cmpuint (columns, ==, 1);
    g_assert_cmpuint (rows, ==, 1);

    /* 4 x 2 atlas at 2 fps: age 2.6 s is frame 5 -> column 1, row 1. */
    lrg_particle_billboard_renderer_set_frames (renderer, 4, 2, 2.0f);
    lrg_particle_system_emit_at (system, 0.0f, 0.0f, 0.0f, 1);
    lrg_particle_system_play (system);
    lrg_particle_system_update (system, 2.6f);
    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_z, world_up,
                                                             vertices, 1), ==, 1);
    g_assert_cmpfloat_with_epsilon (vertices[0 * FPV + 3], 0.25f, EPS);  /* u0 */
    g_assert_cmpfloat_with_epsilon (vertices[0 * FPV + 4], 1.0f, EPS);   /* v1 (bottom) */
    g_assert_cmpfloat_with_epsilon (vertices[2 * FPV + 3], 0.5f, EPS);   /* u1 */
    g_assert_cmpfloat_with_epsilon (vertices[2 * FPV + 4], 0.5f, EPS);   /* v0 (top) */

    /* Wraps: 4.6 s total = frame 9 % 8 = 1. */
    lrg_particle_system_update (system, 2.0f);
    lrg_particle_billboard_renderer_build (renderer, system, eye_z, world_up, vertices, 1);
    g_assert_cmpfloat_with_epsilon (vertices[3], 0.25f, EPS);
    g_assert_cmpfloat_with_epsilon (vertices[2 * FPV + 4], 0.0f, EPS);

    /* fps 0: spread over the lifetime; 4.6 of 10 s of 8 frames = frame 3. */
    lrg_particle_billboard_renderer_set_frames (renderer, 4, 2, 0.0f);
    lrg_particle_billboard_renderer_build (renderer, system, eye_z, world_up, vertices, 1);
    g_assert_cmpfloat_with_epsilon (vertices[3], 0.75f, EPS);
    g_assert_cmpfloat_with_epsilon (vertices[2 * FPV + 4], 0.0f, EPS);
}

static void
test_build_back_to_front (void)
{
    g_autoptr(LrgParticleSystem) system = make_system (1.0f, 1.0f);
    g_autoptr(LrgParticleBillboardRenderer) renderer = lrg_particle_billboard_renderer_new ();
    gfloat vertices[3 * FPQ];
    gfloat near_two[2 * FPQ];
    gfloat centre[3];
    guint i;

    /* Emitted in the order middle, near, far. */
    lrg_particle_system_emit_at (system, 0.0f, 0.0f, 0.0f, 1);
    lrg_particle_system_emit_at (system, 0.0f, 0.0f, 5.0f, 1);
    lrg_particle_system_emit_at (system, 0.0f, 0.0f, -5.0f, 1);

    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_z, world_up,
                                                             NULL, 10), ==, 3);
    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_z, world_up,
                                                             vertices, 3), ==, 3);
    for (i = 0; i < 3; i++)
    {
        centre[i] = vertices[i * FPQ + 2];
    }
    g_assert_cmpfloat_with_epsilon (centre[0], -5.0f, EPS);
    g_assert_cmpfloat_with_epsilon (centre[1], 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (centre[2], 5.0f, EPS);

    /* max_quads 2 keeps the nearest two, still back to front; the buffer
     * past them is never touched (ASAN would catch an overrun). */
    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_z, world_up,
                                                             near_two, 2), ==, 2);
    g_assert_cmpfloat_with_epsilon (near_two[2], 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (near_two[FPQ + 2], 5.0f, EPS);
    g_assert_cmpuint (lrg_particle_billboard_renderer_build (renderer, system, eye_z, world_up,
                                                             near_two, 0), ==, 0);
}

static void
test_texture_and_release (void)
{
    g_autoptr(LrgParticleBillboardRenderer) renderer = lrg_particle_billboard_renderer_new ();

    /* No texture by default (the dot), release_gpu safe headless. */
    g_assert_null (lrg_particle_billboard_renderer_get_texture (renderer));
    lrg_particle_billboard_renderer_set_texture (renderer, NULL);
    lrg_particle_billboard_renderer_release_gpu (renderer);
    lrg_particle_billboard_renderer_release_gpu (renderer);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/particles/system/foreach-active", test_foreach_active);
    g_test_add_func ("/particles/system/emit-spawn-hook", test_emit_calls_spawn_hook);
    g_test_add_func ("/particles/billboard-renderer/empty", test_build_empty);
    g_test_add_func ("/particles/billboard-renderer/camera-facing", test_build_camera_facing);
    g_test_add_func ("/particles/billboard-renderer/eye-above", test_build_eye_above);
    g_test_add_func ("/particles/billboard-renderer/flipbook", test_build_flipbook);
    g_test_add_func ("/particles/billboard-renderer/back-to-front", test_build_back_to_front);
    g_test_add_func ("/particles/billboard-renderer/texture", test_texture_and_release);

    return g_test_run ();
}
