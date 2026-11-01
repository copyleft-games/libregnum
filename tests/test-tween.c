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
    gfloat  clamped;
    gdouble depth;
    guint   x_writes;       /* set_property() calls for "x" */
};

enum
{
    PROP_ANIM_0,
    PROP_ANIM_X,
    PROP_ANIM_Y,
    PROP_ANIM_OPACITY,
    PROP_ANIM_SCORE,
    PROP_ANIM_LEVEL,
    PROP_ANIM_CLAMPED,
    PROP_ANIM_DEPTH,
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
    case PROP_ANIM_CLAMPED:
        g_value_set_float (value, self->clamped);
        break;
    case PROP_ANIM_DEPTH:
        g_value_set_double (value, self->depth);
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
        self->x_writes++;
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
    case PROP_ANIM_CLAMPED:
        self->clamped = g_value_get_float (value);
        break;
    case PROP_ANIM_DEPTH:
        self->depth = g_value_get_double (value);
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

    /* Out-of-range writes are clamped instead of rejected */
    anim_properties[PROP_ANIM_CLAMPED] =
        g_param_spec_float ("clamped", "Clamped", "Clamped to 0..1",
                            0.0f, 1.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_LAX_VALIDATION);

    anim_properties[PROP_ANIM_DEPTH] =
        g_param_spec_double ("depth", "Depth", "Depth",
                             -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
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

/*
 * A subclass that overrides the parent's "x" with its own property ID
 * and setter: writes through the override must reach the subclass.
 */
#define TEST_TYPE_ANIMATABLE_CHILD (test_animatable_child_get_type ())
G_DECLARE_FINAL_TYPE (TestAnimatableChild, test_animatable_child, TEST, ANIMATABLE_CHILD, TestAnimatable)

struct _TestAnimatableChild
{
    TestAnimatable parent_instance;

    gfloat  child_x;
};

enum
{
    PROP_CHILD_0,
    PROP_CHILD_X
};

G_DEFINE_TYPE (TestAnimatableChild, test_animatable_child, TEST_TYPE_ANIMATABLE)

static void
test_animatable_child_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    if (prop_id == PROP_CHILD_X)
        g_value_set_float (value, TEST_ANIMATABLE_CHILD (object)->child_x);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_animatable_child_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
    if (prop_id == PROP_CHILD_X)
        TEST_ANIMATABLE_CHILD (object)->child_x = g_value_get_float (value);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
test_animatable_child_class_init (TestAnimatableChildClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = test_animatable_child_get_property;
    object_class->set_property = test_animatable_child_set_property;
    g_object_class_override_property (object_class, PROP_CHILD_X, "x");
}

static void
test_animatable_child_init (TestAnimatableChild *self)
{
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
 * Property Write Path Tests
 *
 * Tweens write through a pspec resolved once (or a float setter); these
 * pin down that the writes behave as g_object_set_property() did.
 * ========================================================================== */

static void
count_notify (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
    guint *count = user_data;

    (*count)++;
}

/* A plain property (GObject notifies after the write): one "notify::x"
 * per update, with the value already written. */
static void
test_tween_notify_plain (TweenFixture  *fixture,
                         gconstpointer  user_data)
{
    g_autoptr(LrgTween) tween = NULL;
    guint notifies = 0;
    guint i;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 100.0f);
    g_signal_connect (fixture->target, "notify::x", G_CALLBACK (count_notify), &notifies);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    for (i = 1; i <= 4; i++)
    {
        lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.25f);
        g_assert_cmpuint (notifies, ==, i);
        g_assert_cmpuint (fixture->target->x_writes, ==, i);
        g_assert_cmpfloat_with_epsilon (fixture->target->x, 25.0f * i, 0.0001f);
    }

    /* reset() writes the start value the same way */
    lrg_tween_base_reset (LRG_TWEEN_BASE (tween));
    g_assert_cmpuint (notifies, ==, 5);
    g_assert_cmpfloat (fixture->target->x, ==, 0.0f);
}

/* A G_PARAM_EXPLICIT_NOTIFY property: the setter notifies, only on a
 * change, exactly as a by-name write let it. */
static void
test_tween_notify_explicit (void)
{
    g_autoptr(LrgTransformComponent) transform = lrg_transform_component_new ();
    g_autoptr(LrgTween) tween = NULL;
    guint notifies = 0;

    tween = lrg_tween_new (G_OBJECT (transform), "local-x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 10.0f);
    g_signal_connect (transform, "notify::local-x", G_CALLBACK (count_notify), &notifies);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_x (transform), 5.0f, 0.0001f);
    g_assert_cmpuint (notifies, ==, 1);

    /* No time passes: same value, the setter does not notify */
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.0f);
    g_assert_cmpuint (notifies, ==, 1);

    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_transform_component_get_local_x (transform), 10.0f, 0.0001f);
    g_assert_cmpuint (notifies, ==, 2);
}

/* int, uint and double take the direct path with the same arithmetic */
static void
test_tween_numeric_types (TweenFixture  *fixture,
                          gconstpointer  user_data)
{
    g_autoptr(LrgTween) level = NULL;
    g_autoptr(LrgTween) depth = NULL;
    g_autoptr(LrgTween) score = NULL;

    level = lrg_tween_new (G_OBJECT (fixture->target), "level", 1.0f);
    lrg_tween_set_from_uint (level, 10);
    lrg_tween_set_to_uint (level, 20);
    depth = lrg_tween_new (G_OBJECT (fixture->target), "depth", 1.0f);
    lrg_tween_set_from_double (depth, -1.0);
    lrg_tween_set_to_double (depth, 1.0);
    score = lrg_tween_new (G_OBJECT (fixture->target), "score", 1.0f);
    lrg_tween_set_from_int (score, 10);
    lrg_tween_set_to_int (score, -10);

    lrg_tween_base_start (LRG_TWEEN_BASE (level));
    lrg_tween_base_start (LRG_TWEEN_BASE (depth));
    lrg_tween_base_start (LRG_TWEEN_BASE (score));
    lrg_tween_base_update (LRG_TWEEN_BASE (level), 0.25f);
    lrg_tween_base_update (LRG_TWEEN_BASE (depth), 0.25f);
    lrg_tween_base_update (LRG_TWEEN_BASE (score), 0.25f);

    g_assert_cmpuint (fixture->target->level, ==, (guint)(10u + (gfloat)(20u - 10u) * 0.25f));
    g_assert_cmpfloat (fixture->target->depth, ==, -1.0 + 2.0 * 0.25);
    g_assert_cmpint (fixture->target->score, ==, (gint)(10 + (-10 - 10) * 0.25f));
}

/* Values of another type than the property are transformed by GObject,
 * as before (a double tween on a float property). */
static void
test_tween_transformed_value (TweenFixture  *fixture,
                              gconstpointer  user_data)
{
    g_autoptr(LrgTween) tween = NULL;
    guint notifies = 0;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_double (tween, 0.0);
    lrg_tween_set_to_double (tween, 8.0);
    g_signal_connect (fixture->target, "notify::x", G_CALLBACK (count_notify), &notifies);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);

    g_assert_cmpfloat (fixture->target->x, ==, 4.0f);
    g_assert_cmpuint (notifies, ==, 1);
}

/* Out-of-range values: rejected with GObject's critical on a strict
 * pspec, clamped on a G_PARAM_LAX_VALIDATION one. */
static void
test_tween_validation (TweenFixture  *fixture,
                       gconstpointer  user_data)
{
    g_autoptr(LrgTween) strict = NULL;
    g_autoptr(LrgTween) lax = NULL;

    strict = lrg_tween_new (G_OBJECT (fixture->target), "opacity", 1.0f);
    lrg_tween_set_from_float (strict, 0.0f);
    lrg_tween_set_to_float (strict, 4.0f);
    lrg_tween_base_start (LRG_TWEEN_BASE (strict));

    /* 1.0 is still in range */
    lrg_tween_base_update (LRG_TWEEN_BASE (strict), 0.25f);
    g_assert_cmpfloat (fixture->target->opacity, ==, 1.0f);

    g_test_expect_message ("GLib-GObject", G_LOG_LEVEL_CRITICAL, "*out of range*");
    lrg_tween_base_update (LRG_TWEEN_BASE (strict), 0.25f);
    g_test_assert_expected_messages ();
    g_assert_cmpfloat (fixture->target->opacity, ==, 1.0f);

    lax = lrg_tween_new (G_OBJECT (fixture->target), "clamped", 1.0f);
    lrg_tween_set_from_float (lax, 0.0f);
    lrg_tween_set_to_float (lax, 4.0f);
    lrg_tween_base_start (LRG_TWEEN_BASE (lax));
    lrg_tween_base_update (LRG_TWEEN_BASE (lax), 0.125f);
    g_assert_cmpfloat (fixture->target->clamped, ==, 0.5f);
    lrg_tween_base_update (LRG_TWEEN_BASE (lax), 0.5f);
    g_assert_cmpfloat (fixture->target->clamped, ==, 1.0f);
}

/* An overridden property is written through the overriding class */
static void
test_tween_override_property (void)
{
    g_autoptr(TestAnimatableChild) child = g_object_new (TEST_TYPE_ANIMATABLE_CHILD, NULL);
    g_autoptr(LrgTween) tween = NULL;
    guint notifies = 0;

    tween = lrg_tween_new (G_OBJECT (child), "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 2.0f);
    g_signal_connect (child, "notify::x", G_CALLBACK (count_notify), &notifies);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);

    g_assert_cmpfloat (child->child_x, ==, 1.0f);
    g_assert_cmpfloat (TEST_ANIMATABLE (child)->x, ==, 0.0f);
    g_assert_cmpuint (notifies, ==, 1);
}

/* Changing the target or the property rebinds the resolved write */
static void
test_tween_rebind (TweenFixture  *fixture,
                   gconstpointer  user_data)
{
    g_autoptr(TestAnimatableChild) child = g_object_new (TEST_TYPE_ANIMATABLE_CHILD, NULL);
    g_autoptr(LrgTransformComponent) transform = lrg_transform_component_new ();
    g_autoptr(LrgTween) tween = NULL;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 1.0f);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.25f);
    g_assert_cmpfloat (fixture->target->x, ==, 0.25f);

    /* Same name, other property ID on another class */
    g_object_set (tween, "target", child, NULL);
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.25f);
    g_assert_cmpfloat (child->child_x, ==, 0.5f);
    g_assert_cmpfloat (fixture->target->x, ==, 0.25f);

    /* Other property on the same target */
    g_object_set (tween, "target", fixture->target, "property-name", "y", NULL);
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.25f);
    g_assert_cmpfloat (fixture->target->y, ==, 0.75f);
    g_assert_cmpfloat (fixture->target->x, ==, 0.25f);

    /* A target of an unrelated class, then its property: "y" does not
     * exist on it in between */
    g_test_expect_message ("Libregnum-Tween", G_LOG_LEVEL_WARNING, "*'y' not found*");
    g_object_set (tween, "target", transform, NULL);
    g_test_assert_expected_messages ();
    g_object_set (tween, "property-name", "local-x", NULL);
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.125f);
    g_assert_cmpfloat (lrg_transform_component_get_local_x (transform), ==, 0.875f);
}

static void
drop_target_on_notify (GObject    *object,
                       GParamSpec *pspec,
                       gpointer    user_data)
{
    TestAnimatable **target = user_data;

    g_clear_object (target);
}

/* A notify handler releasing the last reference ends the tween's
 * writes without touching the freed target. */
static void
test_tween_target_released_in_notify (void)
{
    TestAnimatable *target = g_object_new (TEST_TYPE_ANIMATABLE, NULL);
    g_autoptr(LrgTween) tween = NULL;

    tween = lrg_tween_new (G_OBJECT (target), "x", 1.0f);
    lrg_tween_set_from_float (tween, 0.0f);
    lrg_tween_set_to_float (tween, 1.0f);
    g_signal_connect (target, "notify::x", G_CALLBACK (drop_target_on_notify), &target);
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));

    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.25f);
    g_assert_null (target);
    g_assert_null (lrg_tween_get_target (tween));

    /* Later updates and reset() have nothing to write to */
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.25f);
    lrg_tween_base_reset (LRG_TWEEN_BASE (tween));
}

typedef struct
{
    GArray *values;
    guint   destroyed;
} SetterLog;

static void
record_float (GObject  *target,
              gfloat    value,
              gpointer  user_data)
{
    SetterLog *log = user_data;

    g_assert_true (TEST_IS_ANIMATABLE (target));
    g_array_append_val (log->values, value);
}

static void
setter_log_destroyed (gpointer user_data)
{
    SetterLog *log = user_data;

    log->destroyed++;
}

/* The float setter gets the values the property would have, replaces
 * the property write (no notify), and owns its data. */
static void
test_tween_float_setter (TweenFixture  *fixture,
                         gconstpointer  user_data)
{
    g_autoptr(GArray) values = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgTween) tween = NULL;
    g_autoptr(LrgTween) ints = NULL;
    SetterLog log = { values, 0 };
    guint notifies = 0;

    tween = lrg_tween_new (G_OBJECT (fixture->target), "x", 1.0f);
    lrg_tween_set_from_float (tween, 2.0f);
    lrg_tween_set_to_float (tween, 6.0f);
    lrg_tween_base_set_easing (LRG_TWEEN_BASE (tween), LRG_EASING_EASE_IN_QUAD);
    g_assert_false (lrg_tween_has_float_setter (tween));
    lrg_tween_set_float_setter (tween, record_float, &log, setter_log_destroyed);
    g_assert_true (lrg_tween_has_float_setter (tween));
    g_signal_connect (fixture->target, "notify::x", G_CALLBACK (count_notify), &notifies);

    lrg_tween_base_start (LRG_TWEEN_BASE (tween));
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 0.5f);
    g_assert_cmpuint (values->len, ==, 1);
    g_assert_cmpfloat (g_array_index (values, gfloat, 0), ==,
                       2.0f + (6.0f - 2.0f) * lrg_easing_apply (LRG_EASING_EASE_IN_QUAD, 0.5f));
    g_assert_cmpfloat (fixture->target->x, ==, 0.0f);
    g_assert_cmpuint (fixture->target->x_writes, ==, 0);
    g_assert_cmpuint (notifies, ==, 0);

    lrg_tween_base_reset (LRG_TWEEN_BASE (tween));
    g_assert_cmpuint (values->len, ==, 2);
    g_assert_cmpfloat (g_array_index (values, gfloat, 1), ==, 2.0f);

    /* Replacing the setter frees the old data; removing it frees both
     * the old data and the data passed along with NULL */
    lrg_tween_set_float_setter (tween, record_float, &log, setter_log_destroyed);
    g_assert_cmpuint (log.destroyed, ==, 1);
    lrg_tween_set_float_setter (tween, NULL, &log, setter_log_destroyed);
    g_assert_cmpuint (log.destroyed, ==, 3);
    g_assert_false (lrg_tween_has_float_setter (tween));

    /* Without the setter the property is written again */
    lrg_tween_base_start (LRG_TWEEN_BASE (tween));
    lrg_tween_base_update (LRG_TWEEN_BASE (tween), 1.0f);
    g_assert_cmpfloat (fixture->target->x, ==, 6.0f);
    g_assert_cmpuint (notifies, ==, 1);

    /* Non-float values ignore a float setter */
    ints = lrg_tween_new (G_OBJECT (fixture->target), "score", 1.0f);
    lrg_tween_set_from_int (ints, 0);
    lrg_tween_set_to_int (ints, 8);
    lrg_tween_set_float_setter (ints, record_float, &log, setter_log_destroyed);
    lrg_tween_base_start (LRG_TWEEN_BASE (ints));
    lrg_tween_base_update (LRG_TWEEN_BASE (ints), 0.5f);
    g_assert_cmpint (fixture->target->score, ==, 4);
    g_assert_cmpuint (values->len, ==, 2);

    /* Disposing the tween frees the setter data */
    g_clear_object (&ints);
    g_assert_cmpuint (log.destroyed, ==, 4);
}

/* A manager update with a notify handler on every target: each handler
 * runs once per update. */
static void
test_tween_manager_notifies (TweenFixture  *fixture,
                             gconstpointer  user_data)
{
    TestAnimatable *targets[3];
    guint notifies[3] = { 0, 0, 0 };
    guint i;

    for (i = 0; i < 3; i++)
    {
        LrgTween *tween;

        targets[i] = g_object_new (TEST_TYPE_ANIMATABLE, NULL);
        g_signal_connect (targets[i], "notify::x", G_CALLBACK (count_notify), &notifies[i]);
        tween = lrg_tween_manager_create_tween (fixture->manager, G_OBJECT (targets[i]), "x", 1.0f);
        lrg_tween_set_from_float (tween, 0.0f);
        lrg_tween_set_to_float (tween, 1.0f);
        lrg_tween_base_set_loop_count (LRG_TWEEN_BASE (tween), -1);
        lrg_tween_base_set_loop_mode (LRG_TWEEN_BASE (tween), LRG_TWEEN_LOOP_PING_PONG);
        lrg_tween_base_start (LRG_TWEEN_BASE (tween));
    }

    for (i = 1; i <= 10; i++)
    {
        guint j;

        lrg_tween_manager_update (fixture->manager, 0.3f);
        for (j = 0; j < 3; j++)
            g_assert_cmpuint (notifies[j], ==, i);
    }

    lrg_tween_manager_clear (fixture->manager);
    for (i = 0; i < 3; i++)
        g_object_unref (targets[i]);
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

    /* Property write path */
    g_test_add ("/tween/property/notify-plain",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_notify_plain,
                tween_fixture_tear_down);
    g_test_add_func ("/tween/property/notify-explicit", test_tween_notify_explicit);
    g_test_add ("/tween/property/numeric-types",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_numeric_types,
                tween_fixture_tear_down);
    g_test_add ("/tween/property/transformed-value",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_transformed_value,
                tween_fixture_tear_down);
    g_test_add ("/tween/property/validation",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_validation,
                tween_fixture_tear_down);
    g_test_add_func ("/tween/property/override", test_tween_override_property);
    g_test_add ("/tween/property/rebind",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_rebind,
                tween_fixture_tear_down);
    g_test_add_func ("/tween/property/target-released-in-notify",
                     test_tween_target_released_in_notify);
    g_test_add ("/tween/property/float-setter",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_float_setter,
                tween_fixture_tear_down);
    g_test_add ("/tween/manager/notifies",
                TweenFixture, NULL,
                tween_fixture_set_up,
                test_tween_manager_notifies,
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
