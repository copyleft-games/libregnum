/* test-reel.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the Reel programmatic-video module.  The spine (animation math,
 * frame/sequence stack, clips, composition, headless renderer) is exercised
 * here; exporter/transition/audio/player coverage is added alongside those
 * modules.
 */

#include <libregnum.h>
#include <graylib.h>
#include <glib/gstdio.h>
#include <math.h>

/* ==========================================================================
 * interpolate
 * ========================================================================== */

static void
test_interpolate_endpoints_and_midpoint (void)
{
    gdouble in[2] = { 0.0, 10.0 };
    gdouble out[2] = { 0.0, 100.0 };

    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (0.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP),
        0.0, 1e-6);
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (10.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP),
        100.0, 1e-6);
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (5.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP),
        50.0, 1e-6);
}

static void
test_interpolate_extrapolate_modes (void)
{
    gdouble in[2] = { 0.0, 10.0 };
    gdouble out[2] = { 0.0, 100.0 };

    /* CLAMP */
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (-5.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP),
        0.0, 1e-6);
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (15.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP),
        100.0, 1e-6);

    /* IDENTITY returns the input unchanged. */
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (-5.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_IDENTITY, LRG_REEL_EXTRAPOLATE_IDENTITY),
        -5.0, 1e-6);
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (15.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_IDENTITY, LRG_REEL_EXTRAPOLATE_IDENTITY),
        15.0, 1e-6);

    /* EXTEND linearly continues the edge segment. */
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (-5.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_EXTEND, LRG_REEL_EXTRAPOLATE_EXTEND),
        -50.0, 1e-6);
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (15.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_EXTEND, LRG_REEL_EXTRAPOLATE_EXTEND),
        150.0, 1e-6);

    /* WRAP folds the input back into the range. */
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (15.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_WRAP, LRG_REEL_EXTRAPOLATE_WRAP),
        50.0, 1e-6);
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (-5.0, in, 2, out, 2, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_WRAP, LRG_REEL_EXTRAPOLATE_WRAP),
        50.0, 1e-6);
}

static void
test_interpolate_multi_segment (void)
{
    gdouble in[3] = { 0.0, 10.0, 20.0 };
    gdouble out[3] = { 0.0, 100.0, 0.0 };

    /* In the second segment [10,20] -> [100,0], input 15 is the midpoint. */
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (15.0, in, 3, out, 3, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP),
        50.0, 1e-6);
    /* On the interior breakpoint. */
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (10.0, in, 3, out, 3, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP),
        100.0, 1e-6);
}

static void
test_interpolate_zero_width_segment (void)
{
    /* A zero-width segment [10,10] introduces a step without dividing by 0. */
    gdouble in[4] = { 0.0, 10.0, 10.0, 20.0 };
    gdouble out[4] = { 0.0, 1.0, 5.0, 6.0 };

    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (10.0, in, 4, out, 4, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP),
        1.0, 1e-6);
    g_assert_cmpfloat_with_epsilon (
        lrg_reel_interpolate (15.0, in, 4, out, 4, LRG_EASING_LINEAR,
                              LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP),
        5.5, 1e-6);
}

static void
test_interpolate_easing_changes_midpoint (void)
{
    gdouble in[2] = { 0.0, 10.0 };
    gdouble out[2] = { 0.0, 100.0 };
    gdouble linear;
    gdouble eased;

    linear = lrg_reel_interpolate (5.0, in, 2, out, 2, LRG_EASING_LINEAR,
                                   LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP);
    eased = lrg_reel_interpolate (5.0, in, 2, out, 2, LRG_EASING_EASE_IN_QUAD,
                                  LRG_REEL_EXTRAPOLATE_CLAMP, LRG_REEL_EXTRAPOLATE_CLAMP);

    g_assert_cmpfloat_with_epsilon (linear, 50.0, 1e-6);
    /* ease-in-quad at t=0.5 is 0.25 -> 25. */
    g_assert_cmpfloat_with_epsilon (eased, 25.0, 1e-4);
}

/* ==========================================================================
 * spring
 * ========================================================================== */

static void
test_spring_starts_at_from (void)
{
    g_assert_cmpfloat_with_epsilon (lrg_reel_spring (0, 30.0, NULL, 0.0, 1.0),
                                    0.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (lrg_reel_spring (0, 30.0, NULL, 3.0, 9.0),
                                    3.0, 1e-9);
}

static void
test_spring_settles_to_target (void)
{
    /* After plenty of time the default spring is essentially at rest. */
    gdouble v = lrg_reel_spring (600, 30.0, NULL, 0.0, 1.0);

    g_assert_cmpfloat (fabs (v - 1.0), <, 0.01);
}

static void
test_spring_underdamped_overshoots (void)
{
    gint i;
    gdouble maxv = 0.0;

    /* Default config (damping 10, stiffness 100, mass 1) is underdamped. */
    for (i = 0; i < 60; i++)
    {
        gdouble v = lrg_reel_spring (i, 30.0, NULL, 0.0, 1.0);
        if (v > maxv)
            maxv = v;
    }

    g_assert_cmpfloat (maxv, >, 1.0);
}

static void
test_spring_overshoot_clamping (void)
{
    LrgReelSpringConfig cfg;
    gint i;
    gdouble maxv = 0.0;

    cfg.mass = 1.0;
    cfg.stiffness = 100.0;
    cfg.damping = 10.0;
    cfg.initial_velocity = 0.0;
    cfg.overshoot_clamping = TRUE;

    for (i = 0; i < 120; i++)
    {
        gdouble v = lrg_reel_spring (i, 30.0, &cfg, 0.0, 1.0);
        if (v > maxv)
            maxv = v;
    }

    g_assert_cmpfloat (maxv, <=, 1.0 + 1e-6);
}

static void
test_spring_duration_is_finite (void)
{
    guint d = lrg_reel_spring_duration_in_frames (30.0, NULL);

    g_assert_cmpuint (d, >=, 1);
    g_assert_cmpuint (d, <, (guint) (30.0 * 20.0));
}

static void
test_spring_config_boxed_roundtrip (void)
{
    g_autoptr(LrgReelSpringConfig) cfg = lrg_reel_spring_config_new ();
    LrgReelSpringConfig *copy;

    g_assert_cmpfloat_with_epsilon (cfg->mass, 1.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (cfg->stiffness, 100.0, 1e-9);

    copy = lrg_reel_spring_config_copy (cfg);
    g_assert_cmpfloat_with_epsilon (copy->damping, cfg->damping, 1e-9);
    lrg_reel_spring_config_free (copy);
}

/* ==========================================================================
 * context (offset stack)
 * ========================================================================== */

static void
test_context_empty_stack (void)
{
    g_autoptr(LrgReelContext) ctx = lrg_reel_context_new (42, 30.0, 320, 240, 100);

    g_assert_cmpint (lrg_reel_context_get_frame (ctx), ==, 42);
    g_assert_cmpint (lrg_reel_context_get_absolute_frame (ctx), ==, 42);
    g_assert_true (lrg_reel_context_is_active (ctx));
    g_assert_cmpint (lrg_reel_context_get_width (ctx), ==, 320);
    g_assert_cmpint (lrg_reel_context_get_height (ctx), ==, 240);
}

static void
test_context_push_pop_offsets (void)
{
    g_autoptr(LrgReelContext) ctx = lrg_reel_context_new (20, 30.0, 320, 240, 100);

    lrg_reel_context_push_offset (ctx, 10, 50);
    g_assert_cmpint (lrg_reel_context_get_frame (ctx), ==, 10);

    /* Nested offsets accumulate. */
    lrg_reel_context_push_offset (ctx, 5, 50);
    g_assert_cmpint (lrg_reel_context_get_frame (ctx), ==, 5);

    lrg_reel_context_pop_offset (ctx);
    g_assert_cmpint (lrg_reel_context_get_frame (ctx), ==, 10);

    lrg_reel_context_pop_offset (ctx);
    g_assert_cmpint (lrg_reel_context_get_frame (ctx), ==, 20);
}

static void
test_context_is_active_boundaries (void)
{
    g_autoptr(LrgReelContext) ctx = lrg_reel_context_new (0, 30.0, 320, 240, 100);

    /* Window [10, 10+5) = [10,15). */
    lrg_reel_context_set_absolute_frame (ctx, 9);
    lrg_reel_context_push_offset (ctx, 10, 5);
    g_assert_false (lrg_reel_context_is_active (ctx)); /* rel = -1 */
    lrg_reel_context_pop_offset (ctx);

    lrg_reel_context_set_absolute_frame (ctx, 10);
    lrg_reel_context_push_offset (ctx, 10, 5);
    g_assert_true (lrg_reel_context_is_active (ctx));  /* rel = 0 */
    lrg_reel_context_pop_offset (ctx);

    lrg_reel_context_set_absolute_frame (ctx, 14);
    lrg_reel_context_push_offset (ctx, 10, 5);
    g_assert_true (lrg_reel_context_is_active (ctx));  /* rel = 4 (last active) */
    lrg_reel_context_pop_offset (ctx);

    lrg_reel_context_set_absolute_frame (ctx, 15);
    lrg_reel_context_push_offset (ctx, 10, 5);
    g_assert_false (lrg_reel_context_is_active (ctx)); /* rel = 5 (== duration) */
    lrg_reel_context_pop_offset (ctx);
}

static void
test_context_infinite_window_no_overflow (void)
{
    g_autoptr(LrgReelContext) ctx = lrg_reel_context_new (1000000, 30.0, 320, 240, 100);

    lrg_reel_context_push_offset (ctx, 0, LRG_REEL_DURATION_INFINITE);
    g_assert_true (lrg_reel_context_is_active (ctx));
    lrg_reel_context_pop_offset (ctx);
}

/* ==========================================================================
 * clip / reel
 * ========================================================================== */

static void
noop_render (LrgReelClip    *clip,
             LrgReelContext *ctx,
             LrgImageCanvas *canvas,
             gpointer        user_data)
{
}

static gboolean destroy_flag;

static void
flag_destroy (gpointer data)
{
    destroy_flag = TRUE;
}

static void
test_clip_properties_and_destroy (void)
{
    LrgReelClip *clip = lrg_reel_clip_new_with_func (noop_render, "data", flag_destroy);

    g_assert_cmpint (lrg_reel_clip_get_from_frame (clip), ==, 0);
    g_assert_true (lrg_reel_clip_get_visible (clip));
    g_assert_cmpfloat_with_epsilon (lrg_reel_clip_get_opacity (clip), 1.0, 1e-9);

    lrg_reel_clip_set_from_frame (clip, 7);
    lrg_reel_clip_set_duration_in_frames (clip, 12);
    lrg_reel_clip_set_opacity (clip, 2.0);  /* clamps to 1.0 */
    lrg_reel_clip_set_opacity (clip, -1.0); /* clamps to 0.0 */

    g_assert_cmpint (lrg_reel_clip_get_from_frame (clip), ==, 7);
    g_assert_cmpint (lrg_reel_clip_get_duration_in_frames (clip), ==, 12);
    g_assert_cmpfloat_with_epsilon (lrg_reel_clip_get_opacity (clip), 0.0, 1e-9);

    /* is_active_at honours from/duration window [7, 19). */
    g_assert_false (lrg_reel_clip_is_active_at (clip, 6));
    g_assert_true (lrg_reel_clip_is_active_at (clip, 7));
    g_assert_true (lrg_reel_clip_is_active_at (clip, 18));
    g_assert_false (lrg_reel_clip_is_active_at (clip, 19));

    destroy_flag = FALSE;
    g_object_unref (clip);
    g_assert_true (destroy_flag);
}

static void
test_reel_clip_ordering (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 32, 32, 30.0, 100);
    g_autoptr(LrgReelClip) a = lrg_reel_clip_new_with_func (noop_render, NULL, NULL);
    g_autoptr(LrgReelClip) b = lrg_reel_clip_new_with_func (noop_render, NULL, NULL);
    g_autoptr(LrgReelClip) c = lrg_reel_clip_new_with_func (noop_render, NULL, NULL);

    lrg_reel_add_clip (reel, a);
    lrg_reel_add_clip (reel, b);
    lrg_reel_insert_clip (reel, c, 0); /* insert at bottom */

    g_assert_cmpuint (lrg_reel_get_n_clips (reel), ==, 3);
    g_assert_true (lrg_reel_get_clip (reel, 0) == c);
    g_assert_true (lrg_reel_get_clip (reel, 1) == a);
    g_assert_true (lrg_reel_get_clip (reel, 2) == b);

    g_assert_true (lrg_reel_remove_clip (reel, a));
    g_assert_cmpuint (lrg_reel_get_n_clips (reel), ==, 2);
}

static void
test_reel_props (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 32, 32, 30.0, 100);

    lrg_reel_set_prop_double (reel, "gravity", 9.8);
    lrg_reel_set_prop_int (reel, "lives", 3);
    lrg_reel_set_prop_string (reel, "title", "Hi");
    lrg_reel_set_prop_boolean (reel, "hard", TRUE);

    g_assert_true (lrg_reel_has_prop (reel, "gravity"));
    g_assert_cmpfloat_with_epsilon (lrg_reel_get_prop_double (reel, "gravity", 0.0), 9.8, 1e-6);
    g_assert_cmpint (lrg_reel_get_prop_int (reel, "lives", 0), ==, 3);
    g_assert_cmpstr (lrg_reel_get_prop_string (reel, "title"), ==, "Hi");
    g_assert_true (lrg_reel_get_prop_boolean (reel, "hard", FALSE));
    /* Fallbacks for missing / mistyped. */
    g_assert_cmpint (lrg_reel_get_prop_int (reel, "missing", 99), ==, 99);
    g_assert_cmpint (lrg_reel_get_prop_int (reel, "gravity", 99), ==, 99);
}

/* ==========================================================================
 * sequence (driven through the renderer with probe clips)
 * ========================================================================== */

typedef struct
{
    gint frame;  /* relative frame last seen */
    gint count;  /* number of times rendered */
} Probe;

static void
probe_render (LrgReelClip    *clip,
              LrgReelContext *ctx,
              LrgImageCanvas *canvas,
              gpointer        user_data)
{
    Probe *p = user_data;

    p->frame = lrg_reel_context_get_frame (ctx);
    p->count++;
}

/* Render a single frame of a reel (discarding the image). */
static void
render_at (LrgReelRenderer *renderer,
           gint             frame)
{
    g_autoptr(GrlImage) img = lrg_reel_renderer_render_frame (renderer, frame);
    g_assert_nonnull (img);
}

static void
test_sequence_shift (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 100);
    LrgReelSequence *seq = lrg_reel_sequence_new (10, 20); /* window [10,30) */
    LrgReelClip *probe_clip;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Probe probe = { -1, 0 };

    probe_clip = lrg_reel_clip_new_with_func (probe_render, &probe, NULL);
    lrg_reel_sequence_add_child (seq, probe_clip);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (seq));

    renderer = lrg_reel_renderer_new (reel);

    probe.count = 0;
    render_at (renderer, 5);
    g_assert_cmpint (probe.count, ==, 0); /* before the window */

    probe.count = 0;
    render_at (renderer, 10);
    g_assert_cmpint (probe.count, ==, 1);
    g_assert_cmpint (probe.frame, ==, 0); /* sequence-relative */

    probe.count = 0;
    render_at (renderer, 25);
    g_assert_cmpint (probe.count, ==, 1);
    g_assert_cmpint (probe.frame, ==, 15);

    probe.count = 0;
    render_at (renderer, 30);
    g_assert_cmpint (probe.count, ==, 0); /* past the window */

    g_object_unref (probe_clip);
    g_object_unref (seq);
}

static void
test_sequence_series (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 100);
    LrgReelSequence *seq = lrg_reel_sequence_new_series ();
    LrgReelClip *a;
    LrgReelClip *b;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Probe pa = { -1, 0 };
    Probe pb = { -1, 0 };

    a = lrg_reel_clip_new_with_func (probe_render, &pa, NULL);
    b = lrg_reel_clip_new_with_func (probe_render, &pb, NULL);
    lrg_reel_clip_set_duration_in_frames (a, 10);
    lrg_reel_clip_set_duration_in_frames (b, 10);
    lrg_reel_sequence_add_child (seq, a);
    lrg_reel_sequence_add_child (seq, b);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (seq));

    renderer = lrg_reel_renderer_new (reel);

    /* frame 5 lands in slot A [0,10). */
    pa.count = pb.count = 0;
    render_at (renderer, 5);
    g_assert_cmpint (pa.count, ==, 1);
    g_assert_cmpint (pa.frame, ==, 5);
    g_assert_cmpint (pb.count, ==, 0);

    /* frame 12 lands in slot B [10,20) -> B sees 2. */
    pa.count = pb.count = 0;
    render_at (renderer, 12);
    g_assert_cmpint (pa.count, ==, 0);
    g_assert_cmpint (pb.count, ==, 1);
    g_assert_cmpint (pb.frame, ==, 2);

    g_object_unref (a);
    g_object_unref (b);
    g_object_unref (seq);
}

static void
test_sequence_loop (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 100);
    LrgReelSequence *seq = lrg_reel_sequence_new_loop (10, 3); /* 3 * 10 = 30 frames */
    LrgReelClip *probe_clip;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Probe probe = { -1, 0 };

    probe_clip = lrg_reel_clip_new_with_func (probe_render, &probe, NULL);
    lrg_reel_sequence_add_child (seq, probe_clip);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (seq));

    renderer = lrg_reel_renderer_new (reel);

    probe.count = 0;
    render_at (renderer, 3);
    g_assert_cmpint (probe.frame, ==, 3);

    probe.count = 0;
    render_at (renderer, 13); /* second iteration */
    g_assert_cmpint (probe.frame, ==, 3);

    probe.count = 0;
    render_at (renderer, 23); /* third iteration */
    g_assert_cmpint (probe.frame, ==, 3);

    probe.count = 0;
    render_at (renderer, 30); /* loop finished (3*10) */
    g_assert_cmpint (probe.count, ==, 0);

    g_object_unref (probe_clip);
    g_object_unref (seq);
}

static void
test_sequence_freeze (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 100);
    LrgReelSequence *seq = lrg_reel_sequence_new_freeze (7);
    LrgReelClip *probe_clip;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Probe probe = { -1, 0 };

    probe_clip = lrg_reel_clip_new_with_func (probe_render, &probe, NULL);
    lrg_reel_sequence_add_child (seq, probe_clip);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (seq));

    renderer = lrg_reel_renderer_new (reel);

    probe.count = 0;
    render_at (renderer, 0);
    g_assert_cmpint (probe.frame, ==, 7);

    probe.count = 0;
    render_at (renderer, 55);
    g_assert_cmpint (probe.frame, ==, 7); /* always frozen */

    g_object_unref (probe_clip);
    g_object_unref (seq);
}

static void
test_sequence_nested_offsets (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 100);
    LrgReelSequence *outer = lrg_reel_sequence_new (10, 50);
    LrgReelSequence *inner = lrg_reel_sequence_new (5, 50);
    LrgReelClip *probe_clip;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Probe probe = { -1, 0 };

    probe_clip = lrg_reel_clip_new_with_func (probe_render, &probe, NULL);
    lrg_reel_sequence_add_child (inner, probe_clip);
    lrg_reel_sequence_add_child (outer, LRG_REEL_CLIP (inner));
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (outer));

    renderer = lrg_reel_renderer_new (reel);

    /* absolute 20 -> outer rel 10 -> inner rel 5. */
    probe.count = 0;
    render_at (renderer, 20);
    g_assert_cmpint (probe.count, ==, 1);
    g_assert_cmpint (probe.frame, ==, 5);

    g_object_unref (probe_clip);
    g_object_unref (inner);
    g_object_unref (outer);
}

/* ==========================================================================
 * renderer pixel behaviour (ghosting, z-order, opacity)
 * ========================================================================== */

typedef struct
{
    guint8 r;
    guint8 g;
    guint8 b;
    guint8 a;
} Rgba;

static void
fill_render (LrgReelClip    *clip,
             LrgReelContext *ctx,
             LrgImageCanvas *canvas,
             gpointer        user_data)
{
    Rgba    *col = user_data;
    GrlColor color;

    color.r = col->r;
    color.g = col->g;
    color.b = col->b;
    color.a = col->a;
    lrg_image_canvas_clear (canvas, &color);
}

static void
read_px (GrlImage *img,
         gint      x,
         gint      y,
         Rgba     *out)
{
    g_autoptr(GrlColor) c = grl_image_get_pixel (img, x, y);

    out->r = c->r;
    out->g = c->g;
    out->b = c->b;
    out->a = c->a;
}

static void
test_renderer_no_ghosting (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 10);
    LrgReelClip *clip;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba red = { 255, 0, 0, 255 };
    Rgba px;

    clip = lrg_reel_clip_new_with_func (fill_render, &red, NULL);
    lrg_reel_clip_set_from_frame (clip, 0);
    lrg_reel_clip_set_duration_in_frames (clip, 1); /* only frame 0 */
    lrg_reel_add_clip (reel, clip);

    renderer = lrg_reel_renderer_new (reel);

    {
        g_autoptr(GrlImage) f0 = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f0, 4, 4, &px);
        g_assert_cmpint (px.r, ==, 255);
        g_assert_cmpint (px.a, ==, 255);
    }

    {
        /* Frame 1: clip inactive, canvas must be cleared (no ghost). */
        g_autoptr(GrlImage) f1 = lrg_reel_renderer_render_frame (renderer, 1);
        read_px (f1, 4, 4, &px);
        g_assert_cmpint (px.a, ==, 0);
    }

    g_object_unref (clip);
}

static void
test_renderer_z_order (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 10);
    LrgReelClip *bottom;
    LrgReelClip *top;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba red = { 255, 0, 0, 255 };
    Rgba green = { 0, 255, 0, 255 };
    Rgba px;

    bottom = lrg_reel_clip_new_with_func (fill_render, &red, NULL);
    top = lrg_reel_clip_new_with_func (fill_render, &green, NULL);
    lrg_reel_add_clip (reel, bottom);
    lrg_reel_add_clip (reel, top);

    renderer = lrg_reel_renderer_new (reel);

    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 4, 4, &px);
        g_assert_cmpint (px.g, ==, 255); /* top (green) wins */
        g_assert_cmpint (px.r, ==, 0);
    }

    g_object_unref (bottom);
    g_object_unref (top);
}

static void
test_renderer_opacity_blend (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 10);
    LrgReelClip *bottom;
    LrgReelClip *top;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba red = { 255, 0, 0, 255 };
    Rgba blue = { 0, 0, 255, 255 };
    Rgba px;

    bottom = lrg_reel_clip_new_with_func (fill_render, &red, NULL);
    top = lrg_reel_clip_new_with_func (fill_render, &blue, NULL);
    lrg_reel_clip_set_opacity (top, 0.5);
    lrg_reel_add_clip (reel, bottom);
    lrg_reel_add_clip (reel, top);

    renderer = lrg_reel_renderer_new (reel);

    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 4, 4, &px);
        /* 50% blue over red ~ (128, 0, 128). Allow tolerance for blend space. */
        g_assert_cmpint (px.r, >, 80);
        g_assert_cmpint (px.r, <, 175);
        g_assert_cmpint (px.b, >, 80);
        g_assert_cmpint (px.b, <, 175);
        g_assert_cmpint (px.a, ==, 255);
    }

    g_object_unref (bottom);
    g_object_unref (top);
}

static void
test_renderer_empty_reel (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 3);
    g_autoptr(LrgReelRenderer) renderer = lrg_reel_renderer_new (reel);
    g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
    Rgba px;

    g_assert_nonnull (f);
    g_assert_cmpint (grl_image_get_width (f), ==, 8);
    g_assert_cmpint (grl_image_get_height (f), ==, 8);
    read_px (f, 0, 0, &px);
    g_assert_cmpint (px.a, ==, 0); /* transparent */
}

static void
test_renderer_background (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 3);
    g_autoptr(LrgReelRenderer) renderer = lrg_reel_renderer_new (reel);
    GrlColor bg;
    Rgba px;

    bg.r = 10;
    bg.g = 20;
    bg.b = 30;
    bg.a = 255;
    lrg_reel_renderer_set_background (renderer, &bg);

    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 0, 0, &px);
        g_assert_cmpint (px.r, ==, 10);
        g_assert_cmpint (px.g, ==, 20);
        g_assert_cmpint (px.b, ==, 30);
        g_assert_cmpint (px.a, ==, 255);
    }
}

/* ==========================================================================
 * exporters
 * ========================================================================== */

/* Build a tiny reel whose single clip fills every frame with @col. */
static LrgReel *
make_fill_reel (Rgba *col,
                gint  frames)
{
    LrgReel     *reel = lrg_reel_new ("t", 8, 8, 30.0, frames);
    LrgReelClip *clip = lrg_reel_clip_new_with_func (fill_render, col, NULL);

    lrg_reel_add_clip (reel, clip);
    g_object_unref (clip);

    return reel;
}

static void
test_exporter_sequence (void)
{
    g_autofree gchar *dir = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    LrgReelSeqExporter *seq;
    Rgba red = { 255, 0, 0, 255 };
    gint i;

    dir = g_dir_make_tmp ("reel-seq-XXXXXX", &error);
    g_assert_no_error (error);

    reel = make_fill_reel (&red, 3);
    renderer = lrg_reel_renderer_new (reel);
    seq = lrg_reel_seq_exporter_new (dir, "frame_%03d", LRG_REEL_IMAGE_FORMAT_PNG);

    g_assert_true (lrg_reel_renderer_render_to_exporter (
        renderer, LRG_REEL_EXPORTER (seq), &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_reel_seq_exporter_get_frame_count (seq), ==, 3);

    for (i = 0; i < 3; i++)
    {
        g_autofree gchar *name = g_strdup_printf ("frame_%03d.png", i);
        g_autofree gchar *path = g_build_filename (dir, name, NULL);
        GrlImage *loaded;

        g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
        loaded = grl_image_new_from_file (path);
        g_assert_nonnull (loaded);
        g_assert_cmpint (grl_image_get_width (loaded), ==, 8);
        g_assert_cmpint (grl_image_get_height (loaded), ==, 8);
        g_object_unref (loaded);
        g_unlink (path);
    }

    g_object_unref (seq);
    g_rmdir (dir);
}

static void
test_exporter_gif (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    LrgReelGifExporter *gif;
    Rgba green = { 0, 255, 0, 255 };
    GStatBuf st;

    dir = g_dir_make_tmp ("reel-gif-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "out.gif", NULL);

    reel = make_fill_reel (&green, 4);
    renderer = lrg_reel_renderer_new (reel);
    gif = lrg_reel_gif_exporter_new (path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (gif);

    g_assert_true (lrg_reel_renderer_render_to_exporter (
        renderer, LRG_REEL_EXPORTER (gif), &error));
    g_assert_no_error (error);

    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
    g_assert_cmpint (g_stat (path, &st), ==, 0);
    g_assert_cmpint (st.st_size, >, 0);

    g_object_unref (gif);
    g_unlink (path);
    g_rmdir (dir);
}

static void
test_exporter_video_ffmpeg (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *path = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    LrgReelVideoExporter *vid;
    Rgba blue = { 0, 0, 255, 255 };
    GStatBuf st;

    if (!lrg_reel_video_exporter_is_ffmpeg_available ())
    {
        g_test_skip ("ffmpeg not available");
        return;
    }

    dir = g_dir_make_tmp ("reel-vid-XXXXXX", &error);
    g_assert_no_error (error);
    path = g_build_filename (dir, "out.mp4", NULL);

    reel = make_fill_reel (&blue, 6);
    renderer = lrg_reel_renderer_new (reel);
    vid = lrg_reel_video_exporter_new (path, LRG_REEL_VIDEO_CODEC_H264);

    g_assert_true (lrg_reel_renderer_render_to_exporter (
        renderer, LRG_REEL_EXPORTER (vid), &error));
    g_assert_no_error (error);

    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
    g_assert_cmpint (g_stat (path, &st), ==, 0);
    g_assert_cmpint (st.st_size, >, 0);

    g_object_unref (vid);
    g_unlink (path);
    g_rmdir (dir);
}

static void
test_exporter_video_missing_ffmpeg (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    LrgReelVideoExporter *vid;
    Rgba blue = { 0, 0, 255, 255 };

    /* A bogus ffmpeg path must fail gracefully, not crash. */
    reel = make_fill_reel (&blue, 2);
    renderer = lrg_reel_renderer_new (reel);
    vid = lrg_reel_video_exporter_new ("/dev/null/out.mp4", LRG_REEL_VIDEO_CODEC_H264);
    lrg_reel_video_exporter_set_ffmpeg_path (vid, "/nonexistent/ffmpeg-xyz");

    g_assert_false (lrg_reel_renderer_render_to_exporter (
        renderer, LRG_REEL_EXPORTER (vid), &error));
    g_assert_error (error, LRG_REEL_EXPORTER_ERROR,
                    LRG_REEL_EXPORTER_ERROR_FFMPEG_NOT_FOUND);

    g_object_unref (vid);
}

/* ==========================================================================
 * transitions (CPU)
 * ========================================================================== */

static void
test_transition_fade (void)
{
    GrlColor red = { 255, 0, 0, 255 };
    GrlColor blue = { 0, 0, 255, 255 };
    g_autoptr(GrlImage) from = grl_image_new_color (8, 8, &red);
    g_autoptr(GrlImage) to = grl_image_new_color (8, 8, &blue);
    g_autoptr(LrgImageCanvas) canvas = lrg_image_canvas_new (8, 8, NULL);
    g_autoptr(LrgReelFadeTransition) fade = lrg_reel_fade_transition_new ();
    GrlImage *img = lrg_image_canvas_get_image (canvas);
    Rgba px;

    lrg_reel_transition_composite (LRG_REEL_TRANSITION (fade), canvas, from, to, 0.0);
    read_px (img, 4, 4, &px);
    g_assert_cmpint (px.r, >, 200); /* mostly red */

    lrg_reel_transition_composite (LRG_REEL_TRANSITION (fade), canvas, from, to, 1.0);
    read_px (img, 4, 4, &px);
    g_assert_cmpint (px.b, >, 200); /* mostly blue */

    lrg_reel_transition_composite (LRG_REEL_TRANSITION (fade), canvas, from, to, 0.5);
    read_px (img, 4, 4, &px);
    g_assert_cmpint (px.r, >, 40);  /* a blend of both */
    g_assert_cmpint (px.b, >, 40);
}

static void
test_transition_wipe (void)
{
    GrlColor red = { 255, 0, 0, 255 };
    GrlColor blue = { 0, 0, 255, 255 };
    g_autoptr(GrlImage) from = grl_image_new_color (16, 16, &red);
    g_autoptr(GrlImage) to = grl_image_new_color (16, 16, &blue);
    g_autoptr(LrgImageCanvas) canvas = lrg_image_canvas_new (16, 16, NULL);
    g_autoptr(LrgReelWipeTransition) wipe =
        lrg_reel_wipe_transition_new (LRG_REEL_TRANSITION_DIRECTION_RIGHT);
    GrlImage *img = lrg_image_canvas_get_image (canvas);
    Rgba left;
    Rgba right;

    /* At progress 0.5, RIGHT wipe reveals the left half as `to` (blue). */
    lrg_reel_transition_composite (LRG_REEL_TRANSITION (wipe), canvas, from, to, 0.5);
    read_px (img, 2, 8, &left);
    read_px (img, 13, 8, &right);
    g_assert_cmpint (left.b, >, 200);  /* revealed -> blue */
    g_assert_cmpint (right.r, >, 200); /* not yet -> red */
}

/* ==========================================================================
 * audio mix
 * ========================================================================== */

static LrgWaveData *
make_const_wave (guint  rate,
                 guint  channels,
                 gfloat dur,
                 gfloat value)
{
    LrgWaveData *w = lrg_wave_data_new_procedural (rate, channels, dur);
    guint        fc = lrg_wave_data_get_frame_count (w);
    gsize        n = (gsize) fc * channels;
    gfloat      *s = g_new (gfloat, n);
    gsize        i;

    for (i = 0; i < n; i++)
        s[i] = value;
    lrg_wave_data_set_samples (w, s, n);
    g_free (s);

    return w;
}

static void
test_audio_mix_single (void)
{
    g_autoptr(LrgReelAudioTrack) track = lrg_reel_audio_track_new (30.0);
    g_autoptr(LrgWaveData) wave = make_const_wave (8000, 1, 1.0f, 0.5f);
    g_autoptr(LrgWaveData) mixed = NULL;
    g_autoptr(GError) error = NULL;
    gfloat *samples;
    gsize   count;

    lrg_reel_audio_track_add (track, wave, 0, 1.0, 0.0, 0.0);
    mixed = lrg_reel_audio_track_mix (track, 8000, 1, 30, &error);
    g_assert_no_error (error);
    g_assert_nonnull (mixed);

    /* ~1 second at 8000 Hz mono. */
    g_assert_cmpuint (lrg_wave_data_get_frame_count (mixed), >=, 7900);
    samples = lrg_wave_data_get_samples (mixed, &count);
    g_assert_cmpfloat (fabs (samples[100] - 0.5f), <, 0.01);
    g_free (samples);
}

static void
test_audio_mix_sum_and_clamp (void)
{
    g_autoptr(LrgReelAudioTrack) track = lrg_reel_audio_track_new (30.0);
    g_autoptr(LrgWaveData) a = make_const_wave (8000, 1, 1.0f, 0.6f);
    g_autoptr(LrgWaveData) b = make_const_wave (8000, 1, 1.0f, 0.6f);
    g_autoptr(LrgWaveData) mixed = NULL;
    g_autoptr(GError) error = NULL;
    gfloat *samples;
    gsize   count;

    lrg_reel_audio_track_add (track, a, 0, 1.0, 0.0, 0.0);
    lrg_reel_audio_track_add (track, b, 0, 1.0, 0.0, 0.0);
    mixed = lrg_reel_audio_track_mix (track, 8000, 1, 30, &error);
    g_assert_no_error (error);

    samples = lrg_wave_data_get_samples (mixed, &count);
    /* 0.6 + 0.6 = 1.2 -> clamped to 1.0. */
    g_assert_cmpfloat (samples[100], <=, 1.0f);
    g_assert_cmpfloat (samples[100], >, 0.95f);
    g_free (samples);
}

static void
test_audio_mix_offset (void)
{
    g_autoptr(LrgReelAudioTrack) track = lrg_reel_audio_track_new (30.0);
    g_autoptr(LrgWaveData) wave = make_const_wave (8000, 1, 0.5f, 0.8f);
    g_autoptr(LrgWaveData) mixed = NULL;
    g_autoptr(GError) error = NULL;
    gfloat *samples;
    gsize   count;

    /* Place the clip at frame 15 (0.5s at 30fps). */
    lrg_reel_audio_track_add (track, wave, 15, 1.0, 0.0, 0.0);
    mixed = lrg_reel_audio_track_mix (track, 8000, 1, 30, &error);
    g_assert_no_error (error);

    samples = lrg_wave_data_get_samples (mixed, &count);
    /* Before 0.5s: silence. After 0.5s: audio present. */
    g_assert_cmpfloat (fabs (samples[100]), <, 0.01);            /* ~0.0125 s */
    g_assert_cmpfloat (fabs (samples[4000 + 100]) - 0.8f, <, 0.05); /* ~0.5 s */
    g_free (samples);
}

static void
test_audio_mix_empty (void)
{
    g_autoptr(LrgReelAudioTrack) track = lrg_reel_audio_track_new (30.0);
    g_autoptr(LrgWaveData) mixed = NULL;
    g_autoptr(GError) error = NULL;

    mixed = lrg_reel_audio_track_mix (track, 8000, 1, 30, &error);
    g_assert_no_error (error);
    g_assert_nonnull (mixed);
    g_assert_cmpuint (lrg_wave_data_get_frame_count (mixed), >=, 7900);
}

static void
test_audio_mix_invalid_args (void)
{
    g_autoptr(LrgReelAudioTrack) track = lrg_reel_audio_track_new (30.0);
    g_autoptr(LrgWaveData) mixed = NULL;
    g_autoptr(GError) error = NULL;

    mixed = lrg_reel_audio_track_mix (track, 0, 1, 30, &error);
    g_assert_null (mixed);
    g_assert_nonnull (error);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/reel/interpolate/endpoints", test_interpolate_endpoints_and_midpoint);
    g_test_add_func ("/reel/interpolate/extrapolate", test_interpolate_extrapolate_modes);
    g_test_add_func ("/reel/interpolate/multi-segment", test_interpolate_multi_segment);
    g_test_add_func ("/reel/interpolate/zero-width", test_interpolate_zero_width_segment);
    g_test_add_func ("/reel/interpolate/easing", test_interpolate_easing_changes_midpoint);

    g_test_add_func ("/reel/spring/starts-at-from", test_spring_starts_at_from);
    g_test_add_func ("/reel/spring/settles", test_spring_settles_to_target);
    g_test_add_func ("/reel/spring/overshoots", test_spring_underdamped_overshoots);
    g_test_add_func ("/reel/spring/clamping", test_spring_overshoot_clamping);
    g_test_add_func ("/reel/spring/duration", test_spring_duration_is_finite);
    g_test_add_func ("/reel/spring/config-boxed", test_spring_config_boxed_roundtrip);

    g_test_add_func ("/reel/context/empty", test_context_empty_stack);
    g_test_add_func ("/reel/context/push-pop", test_context_push_pop_offsets);
    g_test_add_func ("/reel/context/active-boundaries", test_context_is_active_boundaries);
    g_test_add_func ("/reel/context/infinite", test_context_infinite_window_no_overflow);

    g_test_add_func ("/reel/clip/properties", test_clip_properties_and_destroy);
    g_test_add_func ("/reel/reel/ordering", test_reel_clip_ordering);
    g_test_add_func ("/reel/reel/props", test_reel_props);

    g_test_add_func ("/reel/sequence/shift", test_sequence_shift);
    g_test_add_func ("/reel/sequence/series", test_sequence_series);
    g_test_add_func ("/reel/sequence/loop", test_sequence_loop);
    g_test_add_func ("/reel/sequence/freeze", test_sequence_freeze);
    g_test_add_func ("/reel/sequence/nested", test_sequence_nested_offsets);

    g_test_add_func ("/reel/renderer/no-ghosting", test_renderer_no_ghosting);
    g_test_add_func ("/reel/renderer/z-order", test_renderer_z_order);
    g_test_add_func ("/reel/renderer/opacity", test_renderer_opacity_blend);
    g_test_add_func ("/reel/renderer/empty", test_renderer_empty_reel);
    g_test_add_func ("/reel/renderer/background", test_renderer_background);

    g_test_add_func ("/reel/exporter/sequence", test_exporter_sequence);
    g_test_add_func ("/reel/exporter/gif", test_exporter_gif);
    g_test_add_func ("/reel/exporter/video", test_exporter_video_ffmpeg);
    g_test_add_func ("/reel/exporter/video-missing-ffmpeg", test_exporter_video_missing_ffmpeg);

    g_test_add_func ("/reel/transition/fade", test_transition_fade);
    g_test_add_func ("/reel/transition/wipe", test_transition_wipe);

    g_test_add_func ("/reel/audio/mix-single", test_audio_mix_single);
    g_test_add_func ("/reel/audio/mix-sum-clamp", test_audio_mix_sum_and_clamp);
    g_test_add_func ("/reel/audio/mix-offset", test_audio_mix_offset);
    g_test_add_func ("/reel/audio/mix-empty", test_audio_mix_empty);
    g_test_add_func ("/reel/audio/mix-invalid", test_audio_mix_invalid_args);

    return g_test_run ();
}
