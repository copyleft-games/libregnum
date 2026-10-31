/* test-particles.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <libregnum.h>

/* ============================================================================
 * LrgParticle Tests
 * ============================================================================ */

static void
test_particle_new (void)
{
    LrgParticle *particle;

    particle = lrg_particle_new ();
    g_assert_nonnull (particle);

    /* Check default values - particle is dead by default */
    g_assert_cmpfloat (particle->position_x, ==, 0.0f);
    g_assert_cmpfloat (particle->position_y, ==, 0.0f);
    g_assert_cmpfloat (particle->position_z, ==, 0.0f);
    g_assert_false (lrg_particle_is_alive (particle));

    lrg_particle_free (particle);
}

static void
test_particle_new_at (void)
{
    LrgParticle *particle;

    particle = lrg_particle_new_at (10.0f, 20.0f, 30.0f, 5.0f);
    g_assert_nonnull (particle);

    g_assert_cmpfloat (particle->position_x, ==, 10.0f);
    g_assert_cmpfloat (particle->position_y, ==, 20.0f);
    g_assert_cmpfloat (particle->position_z, ==, 30.0f);
    g_assert_cmpfloat (particle->life, ==, 5.0f);
    g_assert_cmpfloat (particle->max_life, ==, 5.0f);
    g_assert_true (lrg_particle_is_alive (particle));

    lrg_particle_free (particle);
}

static void
test_particle_copy (void)
{
    LrgParticle *particle;
    LrgParticle *copy;

    particle = lrg_particle_new_at (10.0f, 20.0f, 30.0f, 2.0f);
    lrg_particle_set_velocity (particle, 1.0f, 2.0f, 3.0f);

    copy = lrg_particle_copy (particle);
    g_assert_nonnull (copy);
    g_assert_true (copy != particle);

    g_assert_cmpfloat (copy->position_x, ==, 10.0f);
    g_assert_cmpfloat (copy->position_y, ==, 20.0f);
    g_assert_cmpfloat (copy->position_z, ==, 30.0f);
    g_assert_cmpfloat (copy->velocity_x, ==, 1.0f);
    g_assert_cmpfloat (copy->velocity_y, ==, 2.0f);
    g_assert_cmpfloat (copy->velocity_z, ==, 3.0f);
    g_assert_cmpfloat (copy->life, ==, 2.0f);

    lrg_particle_free (particle);
    lrg_particle_free (copy);
}

static void
test_particle_reset (void)
{
    LrgParticle *particle;

    particle = lrg_particle_new_at (10.0f, 20.0f, 30.0f, 5.0f);
    g_assert_true (lrg_particle_is_alive (particle));

    lrg_particle_reset (particle);

    g_assert_cmpfloat (particle->position_x, ==, 0.0f);
    g_assert_cmpfloat (particle->position_y, ==, 0.0f);
    g_assert_cmpfloat (particle->position_z, ==, 0.0f);
    g_assert_false (lrg_particle_is_alive (particle));

    lrg_particle_free (particle);
}

static void
test_particle_spawn (void)
{
    LrgParticle *particle;

    particle = lrg_particle_new ();
    g_assert_false (lrg_particle_is_alive (particle));

    lrg_particle_spawn (particle, 100.0f, 200.0f, 300.0f, 3.0f);

    g_assert_cmpfloat (particle->position_x, ==, 100.0f);
    g_assert_cmpfloat (particle->position_y, ==, 200.0f);
    g_assert_cmpfloat (particle->position_z, ==, 300.0f);
    g_assert_cmpfloat (particle->life, ==, 3.0f);
    g_assert_true (lrg_particle_is_alive (particle));

    lrg_particle_free (particle);
}

static void
test_particle_update (void)
{
    LrgParticle *particle;
    gboolean alive;

    particle = lrg_particle_new_at (0.0f, 0.0f, 0.0f, 1.0f);
    lrg_particle_set_velocity (particle, 10.0f, 20.0f, 0.0f);

    /* Update for 0.5 seconds */
    alive = lrg_particle_update (particle, 0.5f);
    g_assert_true (alive);
    g_assert_cmpfloat (particle->position_x, ==, 5.0f);
    g_assert_cmpfloat (particle->position_y, ==, 10.0f);
    g_assert_cmpfloat (particle->life, ==, 0.5f);

    /* Update past lifetime */
    alive = lrg_particle_update (particle, 1.0f);
    g_assert_false (alive);
    g_assert_false (lrg_particle_is_alive (particle));

    lrg_particle_free (particle);
}

static void
test_particle_color (void)
{
    LrgParticle *particle;

    particle = lrg_particle_new ();

    lrg_particle_set_color (particle, 1.0f, 0.5f, 0.25f, 0.75f);
    g_assert_cmpfloat (particle->color_r, ==, 1.0f);
    g_assert_cmpfloat (particle->color_g, ==, 0.5f);
    g_assert_cmpfloat (particle->color_b, ==, 0.25f);
    g_assert_cmpfloat (particle->color_a, ==, 0.75f);

    lrg_particle_free (particle);
}

static void
test_particle_normalized_age (void)
{
    LrgParticle *particle;
    gfloat age;

    particle = lrg_particle_new_at (0.0f, 0.0f, 0.0f, 2.0f);

    age = lrg_particle_get_normalized_age (particle);
    g_assert_cmpfloat (age, ==, 0.0f);

    lrg_particle_update (particle, 1.0f);
    age = lrg_particle_get_normalized_age (particle);
    g_assert_cmpfloat (age, ==, 0.5f);

    lrg_particle_free (particle);
}

/* ============================================================================
 * LrgParticlePool Tests
 * ============================================================================ */

typedef struct
{
    LrgParticlePool *pool;
} PoolFixture;

static void
pool_fixture_set_up (PoolFixture   *fixture,
                     gconstpointer  user_data)
{
    (void) user_data;
    fixture->pool = lrg_particle_pool_new (100);
    g_assert_nonnull (fixture->pool);
}

static void
pool_fixture_tear_down (PoolFixture   *fixture,
                        gconstpointer  user_data)
{
    (void) user_data;
    g_clear_object (&fixture->pool);
}

static void
test_pool_new (PoolFixture   *fixture,
               gconstpointer  user_data)
{
    (void) user_data;

    g_assert_cmpuint (lrg_particle_pool_get_capacity (fixture->pool), ==, 100);
    g_assert_cmpuint (lrg_particle_pool_get_alive_count (fixture->pool), ==, 0);
    g_assert_true (lrg_particle_pool_is_empty (fixture->pool));
}

static void
test_pool_acquire (PoolFixture   *fixture,
                   gconstpointer  user_data)
{
    LrgParticle *p1;
    LrgParticle *p2;

    (void) user_data;

    p1 = lrg_particle_pool_acquire (fixture->pool);
    g_assert_nonnull (p1);
    g_assert_cmpuint (lrg_particle_pool_get_alive_count (fixture->pool), ==, 1);

    p2 = lrg_particle_pool_acquire (fixture->pool);
    g_assert_nonnull (p2);
    g_assert_true (p1 != p2);
    g_assert_cmpuint (lrg_particle_pool_get_alive_count (fixture->pool), ==, 2);
}

static void
test_pool_release (PoolFixture   *fixture,
                   gconstpointer  user_data)
{
    LrgParticle *particle;

    (void) user_data;

    particle = lrg_particle_pool_acquire (fixture->pool);
    g_assert_cmpuint (lrg_particle_pool_get_alive_count (fixture->pool), ==, 1);

    lrg_particle_pool_release (fixture->pool, particle);
    g_assert_cmpuint (lrg_particle_pool_get_alive_count (fixture->pool), ==, 0);
}

static void
test_pool_clear (PoolFixture   *fixture,
                 gconstpointer  user_data)
{
    guint i;

    (void) user_data;

    for (i = 0; i < 50; i++)
        lrg_particle_pool_acquire (fixture->pool);

    g_assert_cmpuint (lrg_particle_pool_get_alive_count (fixture->pool), ==, 50);

    lrg_particle_pool_clear (fixture->pool);
    g_assert_cmpuint (lrg_particle_pool_get_alive_count (fixture->pool), ==, 0);
    g_assert_true (lrg_particle_pool_is_empty (fixture->pool));
}

/* ============================================================================
 * LrgParticleEmitter Tests
 * ============================================================================ */

typedef struct
{
    LrgParticleEmitter *emitter;
} EmitterFixture;

static void
emitter_fixture_set_up (EmitterFixture *fixture,
                        gconstpointer   user_data)
{
    (void) user_data;
    fixture->emitter = lrg_particle_emitter_new ();
    g_assert_nonnull (fixture->emitter);
}

static void
emitter_fixture_tear_down (EmitterFixture *fixture,
                           gconstpointer   user_data)
{
    (void) user_data;
    g_clear_object (&fixture->emitter);
}

static void
test_emitter_new (EmitterFixture *fixture,
                  gconstpointer   user_data)
{
    (void) user_data;

    g_assert_true (LRG_IS_PARTICLE_EMITTER (fixture->emitter));
    g_assert_cmpfloat (lrg_particle_emitter_get_emission_rate (fixture->emitter), >, 0.0f);
}

static void
test_emitter_rate (EmitterFixture *fixture,
                   gconstpointer   user_data)
{
    (void) user_data;

    lrg_particle_emitter_set_emission_rate (fixture->emitter, 50.0f);
    g_assert_cmpfloat (lrg_particle_emitter_get_emission_rate (fixture->emitter), ==, 50.0f);
}

static void
test_emitter_shape (EmitterFixture *fixture,
                    gconstpointer   user_data)
{
    (void) user_data;

    lrg_particle_emitter_set_emission_shape (fixture->emitter, LRG_EMISSION_SHAPE_POINT);
    g_assert_cmpint (lrg_particle_emitter_get_emission_shape (fixture->emitter), ==, LRG_EMISSION_SHAPE_POINT);

    lrg_particle_emitter_set_emission_shape (fixture->emitter, LRG_EMISSION_SHAPE_CIRCLE);
    g_assert_cmpint (lrg_particle_emitter_get_emission_shape (fixture->emitter), ==, LRG_EMISSION_SHAPE_CIRCLE);

    lrg_particle_emitter_set_emission_shape (fixture->emitter, LRG_EMISSION_SHAPE_RECTANGLE);
    g_assert_cmpint (lrg_particle_emitter_get_emission_shape (fixture->emitter), ==, LRG_EMISSION_SHAPE_RECTANGLE);

    lrg_particle_emitter_set_emission_shape (fixture->emitter, LRG_EMISSION_SHAPE_CONE);
    g_assert_cmpint (lrg_particle_emitter_get_emission_shape (fixture->emitter), ==, LRG_EMISSION_SHAPE_CONE);
}

static void
test_emitter_position (EmitterFixture *fixture,
                       gconstpointer   user_data)
{
    gfloat x, y, z;

    (void) user_data;

    lrg_particle_emitter_set_position (fixture->emitter, 10.0f, 20.0f, 30.0f);
    lrg_particle_emitter_get_position (fixture->emitter, &x, &y, &z);
    g_assert_cmpfloat (x, ==, 10.0f);
    g_assert_cmpfloat (y, ==, 20.0f);
    g_assert_cmpfloat (z, ==, 30.0f);
}

static void
test_emitter_emit (EmitterFixture *fixture,
                   gconstpointer   user_data)
{
    LrgParticle particle = {0};

    (void) user_data;

    lrg_particle_emitter_set_initial_lifetime (fixture->emitter, 1.0f, 2.0f);
    lrg_particle_emitter_emit (fixture->emitter, &particle);

    /* Particle should have been initialized */
    g_assert_true (particle.alive);
    g_assert_cmpfloat (particle.life, >=, 1.0f);
    g_assert_cmpfloat (particle.life, <=, 2.0f);
}

/* ============================================================================
 * Main
 * ============================================================================ */

/* Emitting more than max-particles used to grow the pool, reallocating
 * the particles out from under the system's active list (a crash a few
 * frames later). The system's capacity is now fixed at max-particles. */
static void
test_system_respects_max_particles (void)
{
    g_autoptr(LrgParticleSystem)  system = lrg_particle_system_new (300);
    g_autoptr(LrgParticleEmitter) emitter = lrg_particle_emitter_new ();
    guint                         i;

    lrg_particle_emitter_set_emission_rate (emitter, 20000.0f);
    lrg_particle_emitter_set_initial_lifetime (emitter, 2.0f, 3.0f);
    lrg_particle_system_add_emitter (system, emitter);
    lrg_particle_system_play (system);

    for (i = 0; i < 120; i++)
    {
        lrg_particle_system_update (system, 1.0f / 60.0f);
        g_assert_cmpuint (lrg_particle_system_get_active_count (system), <=, 300);
    }
    g_assert_cmpuint (lrg_particle_system_get_active_count (system), ==, 300);

    /* Burst emission is capped too, and clear() empties the system */
    g_assert_cmpuint (lrg_particle_system_emit (system, 50), ==, 0);
    lrg_particle_system_clear (system);
    g_assert_cmpuint (lrg_particle_system_get_active_count (system), ==, 0);
    g_assert_cmpuint (lrg_particle_system_emit (system, 50), ==, 50);
}

/* Lifetime expiry clears the particle flag before its owner releases the
 * pool lease. Direct release, bulk reclamation and pool-driven updates must
 * all return that slot exactly once, including already-killed particles. */
static void
test_pool_expired_leases(void)
{
	g_autoptr(LrgParticlePool) pool=lrg_particle_pool_new_with_policy(4,LRG_POOL_GROW_NONE,4);
	LrgParticle *first,*second;
	first=lrg_particle_pool_acquire(pool);
	lrg_particle_spawn(first,0,0,0,.1f);
	g_assert_false(lrg_particle_update(first,.2f));
	lrg_particle_pool_release(pool,first); lrg_particle_pool_release(pool,first);
	g_assert_cmpuint(lrg_particle_pool_get_free_count(pool),==,4);
	first=lrg_particle_pool_acquire(pool); second=lrg_particle_pool_acquire(pool);
	g_assert_true(first!=second);
	lrg_particle_spawn(first,0,0,0,.1f); lrg_particle_spawn(second,0,0,0,.1f);
	g_assert_false(lrg_particle_update(second,.2f));
	g_assert_cmpuint(lrg_particle_pool_release_dead(pool),==,1);
	g_assert_cmpuint(lrg_particle_pool_release_dead(pool),==,0);
	lrg_particle_pool_release(pool,second);
	g_assert_cmpuint(lrg_particle_pool_update_all(pool,.2f),==,0);
	g_assert_cmpuint(lrg_particle_pool_get_free_count(pool),==,4);
	g_assert_cmpuint(lrg_particle_pool_get_alive_count(pool),==,0);
	first=lrg_particle_pool_acquire(pool); lrg_particle_kill(first);
	g_assert_cmpuint(lrg_particle_pool_update_all(pool,0),==,0);
	g_assert_cmpuint(lrg_particle_pool_get_free_count(pool),==,4);
}

static void
test_system_reuses_expired_cohorts(void)
{
	g_autoptr(LrgParticleSystem) system=lrg_particle_system_new(7);
	g_autoptr(LrgParticleEmitter) emitter=lrg_particle_emitter_new();
	guint i;
	lrg_particle_emitter_set_emission_rate(emitter,0);
	lrg_particle_emitter_set_initial_lifetime(emitter,.1f,.1f);
	lrg_particle_system_add_emitter(system,emitter);
	lrg_particle_system_set_loop(system,TRUE); lrg_particle_system_play(system);
	for (i=0;i<8;i++) {
		g_assert_cmpuint(lrg_particle_system_emit(system,8),==,7);
		lrg_particle_system_update(system,.2f);
		g_assert_cmpuint(lrg_particle_system_get_active_count(system),==,0);
	}
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Particle tests */
    g_test_add_func ("/particles/particle/new", test_particle_new);
    g_test_add_func ("/particles/particle/new-at", test_particle_new_at);
    g_test_add_func ("/particles/particle/copy", test_particle_copy);
    g_test_add_func ("/particles/particle/reset", test_particle_reset);
    g_test_add_func ("/particles/particle/spawn", test_particle_spawn);
    g_test_add_func ("/particles/particle/update", test_particle_update);
    g_test_add_func ("/particles/particle/color", test_particle_color);
    g_test_add_func ("/particles/particle/normalized-age", test_particle_normalized_age);

    /* Pool tests */
    g_test_add ("/particles/pool/new", PoolFixture, NULL,
                pool_fixture_set_up, test_pool_new, pool_fixture_tear_down);
    g_test_add ("/particles/pool/acquire", PoolFixture, NULL,
                pool_fixture_set_up, test_pool_acquire, pool_fixture_tear_down);
    g_test_add ("/particles/pool/release", PoolFixture, NULL,
                pool_fixture_set_up, test_pool_release, pool_fixture_tear_down);
    g_test_add ("/particles/pool/clear", PoolFixture, NULL,
                pool_fixture_set_up, test_pool_clear, pool_fixture_tear_down);

    /* Emitter tests */
    g_test_add ("/particles/emitter/new", EmitterFixture, NULL,
                emitter_fixture_set_up, test_emitter_new, emitter_fixture_tear_down);
    g_test_add ("/particles/emitter/rate", EmitterFixture, NULL,
                emitter_fixture_set_up, test_emitter_rate, emitter_fixture_tear_down);
    g_test_add ("/particles/emitter/shape", EmitterFixture, NULL,
                emitter_fixture_set_up, test_emitter_shape, emitter_fixture_tear_down);
    g_test_add ("/particles/emitter/position", EmitterFixture, NULL,
                emitter_fixture_set_up, test_emitter_position, emitter_fixture_tear_down);
    g_test_add ("/particles/emitter/emit", EmitterFixture, NULL,
                emitter_fixture_set_up, test_emitter_emit, emitter_fixture_tear_down);

    g_test_add_func ("/particles/system/respects-max-particles",
                     test_system_respects_max_particles);
	g_test_add_func("/particles/pool/expired-leases",test_pool_expired_leases);
	g_test_add_func("/particles/system/expired-cohorts",test_system_reuses_expired_cohorts);

    return g_test_run ();
}
