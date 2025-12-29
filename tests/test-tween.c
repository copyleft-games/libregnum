/* test-tween.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the Tween module.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

/* ==========================================================================
 * Mock Object for Testing
 *
 * A simple GObject with numeric properties for testing tweens.
 * ========================================================================== */

#define TEST_TYPE_ANIMATABLE (test_animatable_get_type ())
G_DECLARE_FINAL_TYPE (TestAnimatable, test_animatable, TEST, ANIMATABLE, GObject)

struct _TestAnimatable
{
    GObject parent_instance;

    gfloat  x;
    gfloat  y;
    gfloat  opacity;
    gint    score;
    guint   level;
};

enum
{
    PROP_ANIM_0,
    PROP_ANIM_X,
    PROP_ANIM_Y,
    PROP_ANIM_OPACITY,
    PROP_ANIM_SCORE,
    PROP_ANIM_LEVEL,
    N_ANIM_PROPS
};

static GParamSpec *anim_properties[N_ANIM_PROPS];

G_DEFINE_TYPE (TestAnimatable, test_animatable, G_TYPE_OBJECT)

static void
test_animatable_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    TestAnimatable *self = TEST_ANIMATABLE (object);

    switch (prop_id)
    {
    case PROP_ANIM_X:
        g_value_set_float (value, self->x);
        break;
    case PROP_ANIM_Y:
        g_value_set_float (value, self->y);
        break;
    case PROP_ANIM_OPACITY:
        g_value_set_float (value, self->opacity);
        break;
    case PROP_ANIM_SCORE:
        g_value_set_int (value, self->score);
        break;
    case PROP_ANIM_LEVEL:
        g_value_set_uint (value, self->level);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_animatable_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    TestAnimatable *self = TEST_ANIMATABLE (object);

    switch (prop_id)
    {
    case PROP_ANIM_X:
        self->x = g_value_get_float (value);
        break;
    case PROP_ANIM_Y:
        self->y = g_value_get_float (value);
        break;
    case PROP_ANIM_OPACITY:
        self->opacity = g_value_get_float (value);
        break;
    case PROP_ANIM_SCORE:
        self->score = g_value_get_int (value);
        break;
    case PROP_ANIM_LEVEL:
        self->level = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
test_animatable_class_init (TestAnimatableClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = test_animatable_get_property;
    object_class->set_property = test_animatable_set_property;

    anim_properties[PROP_ANIM_X] =
        g_param_spec_float ("x", "X", "X position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    anim_properties[PROP_ANIM_Y] =
        g_param_spec_float ("y", "Y", "Y position",
                            -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    anim_properties[PROP_ANIM_OPACITY] =
        g_param_spec_float ("opacity", "Opacity", "Opacity",
                            0.0f, 1.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    anim_properties[PROP_ANIM_SCORE] =
        g_param_spec_int ("score", "Score", "Score",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    anim_properties[PROP_ANIM_LEVEL] =
        g_param_spec_uint ("level", "Level", "Level",
                           0, G_MAXUINT, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_ANIM_PROPS, anim_properties);
}

static void
test_animatable_init (TestAnimatable *self)
{
    self->x = 0.0f;
    self->y = 0.0f;
    self->opacity = 1.0f;
    self->score = 0;
    self->level = 1;
}

/* ==========================================================================
 * Test Fixtures
 * ========================================================================== */

typedef struct
{
    LrgTweenManager *manager;
    TestAnimatable  *target;
} TweenFixture;

static void
tween_fixture_set_up (TweenFixture  *fixture,
                      gconstpointer  user_data)
{
    fixture->manager = lrg_tween_manager_new ();
    fixture->target = g_object_new (TEST_TYPE_ANIMATABLE, NULL);

    g_assert_nonnull (fixture->manager);
    g_assert_nonnull (fixture->target);
}

static void
tween_fixture_tear_down (TweenFixture  *fixture,
                         gconstpointer  user_data)
{
    g_clear_object (&fixture->manager);
    g_clear_object (&fixture->target);
}

/* ==========================================================================
 * Easing Function Tests
 * ========================================================================== */

static void
test_easing_linear (void)
{
    gfloat result;

    result = lrg_easing_linear (0.0f);
    g_assert_cmpfloat_with_epsilon (result, 0.0f, 0.0001f);

    result = lrg_easing_linear (0.5f);
    g_assert_cmpfloat_with_epsilon (result, 0.5f, 0.0001f);

    result = lrg_easing_linear (1.0f);
    g_assert_cmpfloat_with_epsilon (result, 1.0f, 0.0001f);
}

static void
test_easing_quad (void)
{
    gfloat result;

    /* Ease in quad: t^2 */
    result = lrg_easing_ease_in_quad (0.5f);
    g_assert_cmpfloat_with_epsilon (result, 0.25f, 0.0001f);

    /* Ease out quad: 1 - (1-t)^2 */
    result = lrg_easing_ease_out_quad (0.5f);
    g_assert_cmpfloat_with_epsilon (result, 0.75f, 0.0001f);

    /* Boundary conditions */
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_in_quad (0.0f), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_in_quad (1.0f), 1.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_out_quad (0.0f), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_out_quad (1.0f), 1.0f, 0.0001f);
}

static void
test_easing_cubic (void)
{
    gfloat result;

    /* Ease in cubic: t^3 */
    result = lrg_easing_ease_in_cubic (0.5f);
    g_assert_cmpfloat_with_epsilon (result, 0.125f, 0.0001f);

    /* Boundary conditions */
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_in_cubic (0.0f), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_in_cubic (1.0f), 1.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_out_cubic (0.0f), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_out_cubic (1.0f), 1.0f, 0.0001f);
}

static void
test_easing_apply (void)
{
    gfloat linear;
    gfloat quad;

    linear = lrg_easing_apply (LRG_EASING_LINEAR, 0.5f);
    quad = lrg_easing_apply (LRG_EASING_EASE_IN_QUAD, 0.5f);

    g_assert_cmpfloat_with_epsilon (linear, 0.5f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (quad, 0.25f, 0.0001f);
}

static void
test_easing_interpolate (void)
{
    gfloat result;

    /* Linear interpolation from 0 to 100 at t=0.5 */
    result = lrg_easing_interpolate (LRG_EASING_LINEAR, 0.0f, 100.0f, 0.5f);
    g_assert_cmpfloat_with_epsilon (result, 50.0f, 0.0001f);

    /* Ease in quad from 0 to 100 at t=0.5 (should be 25) */
    result = lrg_easing_interpolate (LRG_EASING_EASE_IN_QUAD, 0.0f, 100.0f, 0.5f);
    g_assert_cmpfloat_with_epsilon (result, 25.0f, 0.0001f);
}

static void
test_easing_bounce (void)
{
    /* Test boundary conditions for bounce */
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_out_bounce (0.0f), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_out_bounce (1.0f), 1.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_in_bounce (0.0f), 0.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (lrg_easing_ease_in_bounce (1.0f), 1.0f, 0.0001f);
}

/* ==========================================================================
 * Tween Base Tests
 * ========================================================================== */

static void
test_tween_base_new (void)
{
    g_autoptr(LrgTweenBase) tween = NULL;

    tween = g_object_new (LRG_TYPE_TWEEN,
                          "duration", 1.0f,
                          NULL);

    g_assert_nonnull (tween);
    g_assert_true (LRG_IS_TWEEN_BASE (tween));
    g_assert_cmpfloat_with_epsilon (lrg_tween_base_get_duration (tween), 1.0f, 0.0001f);
    g_assert_cmpint (lrg_tween_base_get_state (tween), ==, LRG_TWEEN_STATE_IDLE);
}

static void
test_tween_base_state_transitions (TweenFixture  *fixture,
                                   gconstpointer  user_data)
{
    g_autoptr(LrgTweenBase) tween = NULL;

    tween = g_object_new (LRG_TYPE_TWEEN,
                          "duration", 1.0f,
                          NULL);

    /* Initial state */
    g_assert_cmpint (lrg_tween_base_get_state (tween), ==, LRG_TWEEN_STATE_IDLE);
    g_assert_false (lrg_tween_base_is_running (tween));
    g_assert_false (lrg_tween_base_is_paused (tween));

    /* Start */
    lrg_tween_base_start (tween);
    g_assert_cmpint (lrg_tween_base_get_state (tween), ==, LRG_TWEEN_STATE_RUNNING);
    g_assert_true (lrg_tween_base_is_running (tween));

    /* Pause */
    lrg_tween_base_pause (tween);
    g_assert_cmpint (lrg_tween_base_get_state (tween), ==, LRG_TWEEN_STATE_PAUSED);
    g_assert_true (lrg_tween_base_is_paused (tween));
    g_assert_false (lrg_tween_base_is_running (tween));

    /* Resume */
    lrg_tween_base_resume (tween);
    g_assert_cmpint (lrg_tween_base_get_state (tween), ==, LRG_TWEEN_STATE_RUNNING);

    /* Stop */
    lrg_tween_base_stop (tween);
    g_assert_cmpint (lrg_tween_base_get_state (tween), ==, LRG_TWEEN_STATE_IDLE);
}

static void
test_tween_base_progress (TweenFixture  *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(LrgTweenBase) tween = NULL;

    tween = g_object_new (LRG_TYPE_TWEEN,
                          "duration", 1.0f,
                          NULL);

    lrg_tween_base_start (tween);

    /* Initial progress */
    g_assert_cmpfloat_with_epsilon (lrg_tween_base_get_progress (tween), 0.0f, 0.0001f);

    /* Update halfway */
    lrg_tween_base_update (tween, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_tween_base_get_progress (tween), 0.5f, 0.0001f);

    /* Update to end */
    lrg_tween_base_update (tween, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_tween_base_get_progress (tween), 1.0f, 0.0001f);
    g_assert_true (lrg_tween_base_is_finished (tween));
}

static void
test_tween_base_delay (TweenFixture  *fixture,
                       gconstpointer  user_data)
{
    g_autoptr(LrgTweenBase) tween = NULL;

    tween = g_object_new (LRG_TYPE_TWEEN,
                          "duration", 1.0f,
                          "delay", 0.5f,
                          NULL);

    lrg_tween_base_start (tween);

    /* During delay, progress should be 0 */
    lrg_tween_base_update (tween, 0.25f);
    g_assert_cmpfloat_with_epsilon (lrg_tween_base_get_progress (tween), 0.0f, 0.0001f);

    /* After delay starts, progress should advance */
    lrg_tween_base_update (tween, 0.25f);  /* Now past delay */
    g_assert_cmpfloat_with_epsilon (lrg_tween_base_get_progress (tween), 0.0f, 0.0001f);

    lrg_tween_base_update (tween, 0.5f);  /* 0.5 into duration */
    g_assert_cmpfloat_with_epsilon (lrg_tween_base_get_progress (tween), 0.5f, 0.0001f);
}

static void
test_tween_base_looping (TweenFixture  *fixture,
                         gconstpointer  user_data)
{
    g_autoptr(LrgTweenBase) tween = NULL;

    tween = g_object_new (LRG_TYPE_TWEEN,
                          "duration", 1.0f,
                          "loop-count", 2,
                          NULL);

    lrg_tween_base_start (tween);

    /* Complete first loop */
    lrg_tween_base_update (tween, 1.0f);
    g_assert_cmpint (lrg_tween_base_get_current_loop (tween), ==, 1);
    g_assert_false (lrg_tween_base_is_finished (tween));

    /* Complete second loop */
    lrg_tween_base_update (tween, 1.0f);
    g_assert_cmpint (lrg_tween_base_get_current_loop (tween), ==, 2);
    g_assert_true (lrg_tween_base_is_finished (tween));
}

static void
test_tween_base_ping_pong (TweenFixture  *fixture,
                           gconstpointer  user_data)
{
    g_autoptr(LrgTweenBase) tween = NULL;

    tween = g_object_new (LRG_TYPE_TWEEN,
                          "duration", 1.0f,
                          "loop-count", 1,
                          "loop-mode", LRG_TWEEN_LOOP_PING_PONG,
                          NULL);

    /* Verify ping-pong mode can be set */
    g_assert_cmpint (lrg_tween_base_get_loop_mode (tween), ==, LRG_TWEEN_LOOP_PING_PONG);

    lrg_tween_base_start (tween);

    /* Forward direction */
    lrg_tween_base_update (tween, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_tween_base_get_progress (tween), 0.5f, 0.0001f);

    /* Complete first direction */
    lrg_tween_base_update (tween, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_tween_base_get_progress (tween), 1.0f, 0.0001f);
}

/* ==========================================================================
 * Property Tween Tests
 * ========================================================================== */

static void
test_tween_new (TweenFixture  *fixture,
                gconstpointer  user_data)
{
    g_autoptr(LrgTween) tween = NULL;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);

    g_assert_nonnull (tween);
    g_assert_true (LRG_IS_TWEEN (tween));
    g_assert_true (lrg_tween_get_target (tween) == G_OBJECT (fixture->target));
    g_assert_cmpstr (lrg_tween_get_property_name (tween), ==, "x");
}

static void
test_tween_animate_float (TweenFixture  *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(LrgTween) tween = NULL;

    fixture->target->x = 0.0f;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 100.0f);

    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    /* Update to 50% */
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 50.0f, 0.0001f);

    /* Update to 100% */
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 100.0f, 0.0001f);
}

static void
test_tween_animate_int (TweenFixture  *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(LrgTween) tween = NULL;

    fixture->target->score = 0;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "score", 1.0f);
    lrg_tween_set_from_int (tween, 0);
    lrg_tween_set_to_int (tween, 100);

    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    /* Update to 50% */
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);
    g_assert_cmpint (fixture->target->score, ==, 50);

    /* Update to 100% */
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);
    g_assert_cmpint (fixture->target->score, ==, 100);
}

static void
test_tween_use_current_as_from (TweenFixture  *fixture,
                                gconstpointer  user_data)
{
    g_autoptr(LrgTween) tween = NULL;

    /* Set initial value */
    fixture->target->x = 50.0f;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_to_float (tween, 100.0f);
    /* use_current_as_from is TRUE by default */

    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    /* Update to 50% - should go from 50 to 100, so at 50% should be 75 */
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 75.0f, 0.0001f);
}

static void
test_tween_relative_mode (TweenFixture  *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(LrgTween) tween = NULL;

    /* Set initial value */
    fixture->target->x = 50.0f;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_by_float (tween, 30.0f);  /* Animate by +30 */

    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    /* Update to 100% - should end at 80 (50 + 30) */
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 1.0f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 80.0f, 0.0001f);
}

static void
test_tween_with_easing (TweenFixture  *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(LrgTween) tween = NULL;

    fixture->target->x = 0.0f;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 100.0f);
    lrg_tween_base_set_easing (LRG_TWEEN_BASE (tween), LRG_EASING_EASE_IN_QUAD);

    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    /* At t=0.5, ease-in-quad gives 0.25, so value should be 25 */
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 25.0f, 0.0001f);
}

/* ==========================================================================
 * Sequence Tests
 * ========================================================================== */

static void
test_sequence_new (void)
{
    g_autoptr(LrgTweenSequence) seq = NULL;

    seq = lrg_tween_sequence_new ();

    g_assert_nonnull (seq);
    g_assert_true (LRG_IS_TWEEN_SEQUENCE (seq));
    g_assert_cmpuint (lrg_tween_group_get_tween_count (LRG_TWEEN_GROUP (seq)), ==, 0);
}

static void
test_sequence_order (TweenFixture  *fixture,
                     gconstpointer  user_data)
{
    g_autoptr(LrgTweenSequence) seq = NULL;
    g_autoptr(LrgTween) tween1 = NULL;
    g_autoptr(LrgTween) tween2 = NULL;

    fixture->target->x = 0.0f;
    fixture->target->y = 0.0f;

    tween1 = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_float (tween1, 0.0f);
    lrg_tween_set_to_float (tween1, 100.0f);

    tween2 = lrg_tween_new (G_OBJECT (fixture->target), "y", 1.0f);
    lrg_tween_set_from_float (tween2, 0.0f);
    lrg_tween_set_to_float (tween2, 100.0f);

    seq = lrg_tween_sequence_new ();
    lrg_tween_sequence_append (seq, LRG_TWEEN_BASE (tween1));
    lrg_tween_sequence_append (seq, LRG_TWEEN_BASE (tween2));

    /* Verify sequence has correct tween count */
    g_assert_cmpuint (lrg_tween_group_get_tween_count (LRG_TWEEN_GROUP (seq)), ==, 2);

    lrg_tween_base_start (LRG_TWEEN_BASE (seq));

    /* First tween should run, y should stay at 0 */
    lrg_tween_base_update (LRG_TWEEN_BASE (seq), 1.0f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 100.0f, 0.0001f);
}

static void
test_sequence_interval (TweenFixture  *fixture,
                        gconstpointer  user_data)
{
    g_autoptr(LrgTweenSequence) seq = NULL;
    g_autoptr(LrgTween) tween = NULL;

    fixture->target->x = 0.0f;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 100.0f);

    seq = lrg_tween_sequence_new ();
    lrg_tween_sequence_append_interval (seq, 0.5f);  /* Wait 0.5 seconds */
    lrg_tween_sequence_append (seq, LRG_TWEEN_BASE (tween));

    lrg_tween_base_start (LRG_TWEEN_BASE (seq));

    /* During interval, x should not change */
    lrg_tween_base_update (LRG_TWEEN_BASE (seq), 0.5f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 0.0f, 0.0001f);

    /* After interval, tween should start */
    lrg_tween_base_update (LRG_TWEEN_BASE (seq), 0.5f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 50.0f, 0.0001f);
}

/* ==========================================================================
 * Parallel Tests
 * ========================================================================== */

static void
test_parallel_new (void)
{
    g_autoptr(LrgTweenParallel) parallel = NULL;

    parallel = lrg_tween_parallel_new ();

    g_assert_nonnull (parallel);
    g_assert_true (LRG_IS_TWEEN_PARALLEL (parallel));
    g_assert_cmpuint (lrg_tween_group_get_tween_count (LRG_TWEEN_GROUP (parallel)), ==, 0);
}

static void
test_parallel_simultaneous (TweenFixture  *fixture,
                            gconstpointer  user_data)
{
    g_autoptr(LrgTweenParallel) parallel = NULL;
    g_autoptr(LrgTween) tween1 = NULL;
    g_autoptr(LrgTween) tween2 = NULL;

    fixture->target->x = 0.0f;
    fixture->target->y = 0.0f;

    tween1 = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_float (tween1, 0.0f);
    lrg_tween_set_to_float (tween1, 100.0f);

    tween2 = lrg_tween_new (G_OBJECT (fixture->target), "y", 1.0f);
    lrg_tween_set_from_float (tween2, 0.0f);
    lrg_tween_set_to_float (tween2, 200.0f);

    parallel = lrg_tween_parallel_new ();
    lrg_tween_parallel_add (parallel, LRG_TWEEN_BASE (tween1));
    lrg_tween_parallel_add (parallel, LRG_TWEEN_BASE (tween2));

    lrg_tween_base_start (LRG_TWEEN_BASE (parallel));

    /* Both should update simultaneously */
    lrg_tween_base_update (LRG_TWEEN_BASE (parallel), 0.5f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 50.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (fixture->target->y, 100.0f, 0.0001f);
}

static void
test_parallel_different_durations (TweenFixture  *fixture,
                                   gconstpointer  user_data)
{
    g_autoptr(LrgTweenParallel) parallel = NULL;
    g_autoptr(LrgTween) tween1 = NULL;
    g_autoptr(LrgTween) tween2 = NULL;

    fixture->target->x = 0.0f;
    fixture->target->y = 0.0f;

    /* Short tween: 1 second */
    tween1 = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_float (tween1, 0.0f);
    lrg_tween_set_to_float (tween1, 100.0f);

    /* Long tween: 2 seconds */
    tween2 = lrg_tween_new (G_OBJECT (fixture->target), "y", 2.0f);
    lrg_tween_set_from_float (tween2, 0.0f);
    lrg_tween_set_to_float (tween2, 100.0f);

    parallel = lrg_tween_parallel_new ();
    lrg_tween_parallel_add (parallel, LRG_TWEEN_BASE (tween1));
    lrg_tween_parallel_add (parallel, LRG_TWEEN_BASE (tween2));

    /* Verify both tweens are added */
    g_assert_cmpuint (lrg_tween_group_get_tween_count (LRG_TWEEN_GROUP (parallel)), ==, 2);

    lrg_tween_base_start (LRG_TWEEN_BASE (parallel));

    /* After 1 second, tween1 should be done, tween2 at 50% */
    lrg_tween_base_update (LRG_TWEEN_BASE (parallel), 1.0f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 100.0f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (fixture->target->y, 50.0f, 0.0001f);
}

/* ==========================================================================
 * Manager Tests
 * ========================================================================== */

static void
test_manager_new (void)
{
    g_autoptr(LrgTweenManager) manager = NULL;

    manager = lrg_tween_manager_new ();

    g_assert_nonnull (manager);
    g_assert_true (LRG_IS_TWEEN_MANAGER (manager));
    g_assert_cmpuint (lrg_tween_manager_get_tween_count (manager), ==, 0);
}

static void
test_manager_add_remove (TweenFixture  *fixture,
                         gconstpointer  user_data)
{
    g_autoptr(LrgTween) tween = NULL;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_base_set_auto_start (LRG_TWEEN_BASE (tween), FALSE);

    lrg_tween_manager_add (fixture->manager, LRG_TWEEN_BASE (tween));
    g_assert_cmpuint (lrg_tween_manager_get_tween_count (fixture->manager), ==, 1);

    lrg_tween_manager_remove (fixture->manager, LRG_TWEEN_BASE (tween));
    g_assert_cmpuint (lrg_tween_manager_get_tween_count (fixture->manager), ==, 0);
}

static void
test_manager_update (TweenFixture  *fixture,
                     gconstpointer  user_data)
{
    LrgTween *tween;

    fixture->target->x = 0.0f;

    tween = lrg_tween_manager_create_tween (fixture->manager,
                                            G_OBJECT (fixture->target),
                                            "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 100.0f);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    lrg_tween_manager_update (fixture->manager, 0.5f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 50.0f, 0.0001f);
}

static void
test_manager_auto_remove (TweenFixture  *fixture,
                          gconstpointer  user_data)
{
    LrgTween *tween;

    fixture->target->x = 0.0f;

    tween = lrg_tween_manager_create_tween (fixture->manager,
                                            G_OBJECT (fixture->target),
                                            "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 100.0f);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    g_assert_cmpuint (lrg_tween_manager_get_tween_count (fixture->manager), ==, 1);

    /* Complete the tween */
    lrg_tween_manager_update (fixture->manager, 1.0f);

    /* Should be auto-removed */
    g_assert_cmpuint (lrg_tween_manager_get_tween_count (fixture->manager), ==, 0);
}

static void
test_manager_time_scale (TweenFixture  *fixture,
                         gconstpointer  user_data)
{
    LrgTween *tween;

    fixture->target->x = 0.0f;

    tween = lrg_tween_manager_create_tween (fixture->manager,
                                            G_OBJECT (fixture->target),
                                            "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 100.0f);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    /* Set 2x time scale */
    lrg_tween_manager_set_time_scale (fixture->manager, 2.0f);

    /* Update with 0.25 seconds, should act like 0.5 seconds */
    lrg_tween_manager_update (fixture->manager, 0.25f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 50.0f, 0.0001f);
}

static void
test_manager_pause_resume_all (TweenFixture  *fixture,
                               gconstpointer  user_data)
{
    LrgTween *tween;

    fixture->target->x = 0.0f;

    tween = lrg_tween_manager_create_tween (fixture->manager,
                                            G_OBJECT (fixture->target),
                                            "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 100.0f);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    lrg_tween_manager_update (fixture->manager, 0.25f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 25.0f, 0.0001f);

    /* Pause all */
    lrg_tween_manager_pause_all (fixture->manager);

    /* Update should not change value */
    lrg_tween_manager_update (fixture->manager, 0.25f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 25.0f, 0.0001f);

    /* Resume all */
    lrg_tween_manager_resume_all (fixture->manager);

    /* Now should continue */
    lrg_tween_manager_update (fixture->manager, 0.25f);
    g_assert_cmpfloat_with_epsilon (fixture->target->x, 50.0f, 0.0001f);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Easing functions */
    g_test_add_func ("/tween/easing/linear", test_easing_linear);
    g_test_add_func ("/tween/easing/quad", test_easing_quad);
    g_test_add_func ("/tween/easing/cubic", test_easing_cubic);
    g_test_add_func ("/tween/easing/apply", test_easing_apply);
    g_test_add_func ("/tween/easing/interpolate", test_easing_interpolate);
    g_test_add_func ("/tween/easing/bounce", test_easing_bounce);

    /* Tween base */
    g_test_add_func ("/tween/base/new", test_tween_base_new);

    g_test_add ("/tween/base/state-transitions",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_base_state_transitions,
                tween_fixture_tear_down);

    g_test_add ("/tween/base/progress",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_base_progress,
                tween_fixture_tear_down);

    g_test_add ("/tween/base/delay",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_base_delay,
                tween_fixture_tear_down);

    g_test_add ("/tween/base/looping",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_base_looping,
                tween_fixture_tear_down);

    g_test_add ("/tween/base/ping-pong",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_base_ping_pong,
                tween_fixture_tear_down);

    /* Property tweens */
    g_test_add ("/tween/property/new",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_new,
                tween_fixture_tear_down);

    g_test_add ("/tween/property/animate-float",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_animate_float,
                tween_fixture_tear_down);

    g_test_add ("/tween/property/animate-int",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_animate_int,
                tween_fixture_tear_down);

    g_test_add ("/tween/property/use-current-as-from",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_use_current_as_from,
                tween_fixture_tear_down);

    g_test_add ("/tween/property/relative-mode",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_relative_mode,
                tween_fixture_tear_down);

    g_test_add ("/tween/property/with-easing",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_with_easing,
                tween_fixture_tear_down);

    /* Sequence */
    g_test_add_func ("/tween/sequence/new", test_sequence_new);

    g_test_add ("/tween/sequence/order",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_sequence_order,
                tween_fixture_tear_down);

    g_test_add ("/tween/sequence/interval",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_sequence_interval,
                tween_fixture_tear_down);

    /* Parallel */
    g_test_add_func ("/tween/parallel/new", test_parallel_new);

    g_test_add ("/tween/parallel/simultaneous",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_parallel_simultaneous,
                tween_fixture_tear_down);

    g_test_add ("/tween/parallel/different-durations",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_parallel_different_durations,
                tween_fixture_tear_down);

    /* Manager */
    g_test_add_func ("/tween/manager/new", test_manager_new);

    g_test_add ("/tween/manager/add-remove",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_manager_add_remove,
                tween_fixture_tear_down);

    g_test_add ("/tween/manager/update",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_manager_update,
                tween_fixture_tear_down);

    g_test_add ("/tween/manager/auto-remove",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_manager_auto_remove,
                tween_fixture_tear_down);

    g_test_add ("/tween/manager/time-scale",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_manager_time_scale,
                tween_fixture_tear_down);

    g_test_add ("/tween/manager/pause-resume-all",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_manager_pause_resume_all,
                tween_fixture_tear_down);

    return g_test_run ();
}
