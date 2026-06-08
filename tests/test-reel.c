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

/* ==========================================================================
 * Wave A: per-clip transform / blend / nested opacity
 * ========================================================================== */

/* Draws a 4x4 white square at the canvas origin (honours the transform). */
static void
marker_render (LrgReelClip    *clip,
               LrgReelContext *ctx,
               LrgImageCanvas *canvas,
               gpointer        user_data)
{
    GrlColor white = { 255, 255, 255, 255 };

    lrg_image_canvas_fill_rect (canvas, 0, 0, 4, 4, &white);
}

static void
test_transform_translate (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 4);
    LrgReelClip *clip;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba px;

    clip = lrg_reel_clip_new_with_func (marker_render, NULL, NULL);
    lrg_reel_clip_set_anchor (clip, 0.0, 0.0);
    lrg_reel_clip_set_x (clip, 8.0);
    lrg_reel_clip_set_y (clip, 8.0);
    lrg_reel_add_clip (reel, clip);

    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 2, 2, &px);
        g_assert_cmpint (px.a, ==, 0);    /* marker moved away from origin */
        read_px (f, 10, 10, &px);
        g_assert_cmpint (px.a, ==, 255);  /* marker now at [8,12) */
        g_assert_cmpint (px.r, ==, 255);
    }
    g_object_unref (clip);
}

static void
test_transform_scale (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 4);
    LrgReelClip *clip;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba px;

    clip = lrg_reel_clip_new_with_func (marker_render, NULL, NULL);
    lrg_reel_clip_set_anchor (clip, 0.0, 0.0);
    lrg_reel_clip_set_scale_x (clip, 2.0);
    lrg_reel_clip_set_scale_y (clip, 2.0);
    lrg_reel_add_clip (reel, clip);

    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        /* 4x4 marker scaled 2x about origin -> covers [0,8). */
        read_px (f, 6, 6, &px);
        g_assert_cmpint (px.a, ==, 255);
        read_px (f, 10, 10, &px);
        g_assert_cmpint (px.a, ==, 0);
    }
    g_object_unref (clip);
}

static void
test_clip_blend_multiply (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 4);
    LrgReelClip *bottom;
    LrgReelClip *top;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba red = { 255, 0, 0, 255 };
    Rgba gray = { 128, 128, 128, 255 };
    Rgba px;

    bottom = lrg_reel_clip_new_with_func (fill_render, &red, NULL);
    top = lrg_reel_clip_new_with_func (fill_render, &gray, NULL);
    lrg_reel_clip_set_blend_mode (top, LRG_REEL_BLEND_MULTIPLY);
    lrg_reel_add_clip (reel, bottom);
    lrg_reel_add_clip (reel, top);

    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 4, 4, &px);
        /* red * gray = (255*128/255, 0, 0) ~ (128, 0, 0). */
        g_assert_cmpint (px.r, >, 110);
        g_assert_cmpint (px.r, <, 145);
        g_assert_cmpint (px.g, ==, 0);
        g_assert_cmpint (px.b, ==, 0);
    }
    g_object_unref (bottom);
    g_object_unref (top);
}

static void
test_nested_clip_opacity (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 4);
    LrgReelClip *base;
    LrgReelSequence *seq;
    LrgReelClip *child;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba red = { 255, 0, 0, 255 };
    Rgba blue = { 0, 0, 255, 255 };
    Rgba px;

    base = lrg_reel_clip_new_with_func (fill_render, &red, NULL);
    lrg_reel_add_clip (reel, base);

    /* A blue child at 0.5 opacity INSIDE a sequence -> must blend (v1 gap). */
    seq = lrg_reel_sequence_new (0, LRG_REEL_DURATION_INFINITE);
    child = lrg_reel_clip_new_with_func (fill_render, &blue, NULL);
    lrg_reel_clip_set_opacity (child, 0.5);
    lrg_reel_sequence_add_child (seq, child);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (seq));

    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 4, 4, &px);
        g_assert_cmpint (px.r, >, 80);   /* blend of red + blue, not pure blue */
        g_assert_cmpint (px.r, <, 175);
        g_assert_cmpint (px.b, >, 80);
    }
    g_object_unref (child);
    g_object_unref (seq);
    g_object_unref (base);
}

/* ==========================================================================
 * Wave A: color interp / bezier / random / noise
 * ========================================================================== */

static void
test_color_interpolate (void)
{
    GrlColor red = { 255, 0, 0, 255 };
    GrlColor blue = { 0, 0, 255, 255 };
    g_autoptr(GrlColor) c0 = NULL;
    g_autoptr(GrlColor) c1 = NULL;
    g_autoptr(GrlColor) cmid = NULL;

    c0 = lrg_reel_interpolate_color_clamped (0.0, 0.0, 1.0, &red, &blue, LRG_EASING_LINEAR);
    g_assert_cmpint (c0->r, ==, 255);
    g_assert_cmpint (c0->b, ==, 0);

    c1 = lrg_reel_interpolate_color_clamped (1.0, 0.0, 1.0, &red, &blue, LRG_EASING_LINEAR);
    g_assert_cmpint (c1->r, ==, 0);
    g_assert_cmpint (c1->b, ==, 255);

    cmid = lrg_reel_interpolate_color_clamped (0.5, 0.0, 1.0, &red, &blue, LRG_EASING_LINEAR);
    /* OkLab midpoint of red->blue is a magenta/purple: both ends present. */
    g_assert_cmpint (cmid->r, >, 20);
    g_assert_cmpint (cmid->b, >, 20);
}

static void
test_easing_bezier (void)
{
    /* bezier(0,0,1,1) is the identity (linear). */
    g_assert_cmpfloat_with_epsilon (lrg_reel_easing_bezier (0.0, 0.0, 1.0, 1.0, 0.3),
                                    0.3, 0.01);
    g_assert_cmpfloat_with_epsilon (lrg_reel_easing_bezier (0.0, 0.0, 1.0, 1.0, 0.0),
                                    0.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (lrg_reel_easing_bezier (0.0, 0.0, 1.0, 1.0, 1.0),
                                    1.0, 1e-9);
    /* Symmetric ease-in-out passes through 0.5 at the midpoint. */
    g_assert_cmpfloat_with_epsilon (lrg_reel_easing_bezier (0.42, 0.0, 0.58, 1.0, 0.5),
                                    0.5, 0.02);
    /* Strong ease-in is below the line early. */
    g_assert_cmpfloat (lrg_reel_easing_bezier (0.7, 0.0, 1.0, 0.5, 0.3), <, 0.3);
}

static void
test_random_deterministic (void)
{
    gdouble a;
    gdouble b;
    gint    i;
    gdouble sum = 0.0;

    a = lrg_reel_random (42);
    b = lrg_reel_random (42);
    g_assert_cmpfloat (a, ==, b);                 /* same seed -> same value */
    g_assert_cmpfloat (a, >=, 0.0);
    g_assert_cmpfloat (a, <, 1.0);
    g_assert_cmpfloat (lrg_reel_random (43), !=, a);

    for (i = 0; i < 2000; i++)
        sum += lrg_reel_random ((guint64) i);
    g_assert_cmpfloat (fabs (sum / 2000.0 - 0.5), <, 0.05);  /* ~uniform */

    {
        gdouble r = lrg_reel_random_range (7, 10.0, 20.0);
        g_assert_cmpfloat (r, >=, 10.0);
        g_assert_cmpfloat (r, <, 20.0);
    }
}

static void
test_noise (void)
{
    gdouble n;
    gint    i;

    n = lrg_reel_noise_1d (3.7);
    g_assert_cmpfloat (n, ==, lrg_reel_noise_1d (3.7));   /* deterministic */
    g_assert_cmpfloat (n, >=, -1.0);
    g_assert_cmpfloat (n, <=, 1.0);
    /* Continuity: nearby inputs give nearby outputs. */
    g_assert_cmpfloat (fabs (lrg_reel_noise_1d (3.70) - lrg_reel_noise_1d (3.71)), <, 0.1);

    /* 2D/3D in range and deterministic over a grid. */
    for (i = 0; i < 50; i++)
    {
        gdouble x = i * 0.37;
        gdouble n2 = lrg_reel_noise_2d (x, x * 0.5);
        gdouble n3 = lrg_reel_noise_3d (x, x * 0.5, x * 0.25);

        g_assert_cmpfloat (n2, >=, -1.0);
        g_assert_cmpfloat (n2, <=, 1.0);
        g_assert_cmpfloat (n3, >=, -1.0);
        g_assert_cmpfloat (n3, <=, 1.0);
        g_assert_cmpfloat (n2, ==, lrg_reel_noise_2d (x, x * 0.5));
    }
}

/* ==========================================================================
 * Wave B: content clips
 * ========================================================================== */

static void
test_solid_clip (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 2);
    GrlColor green = { 0, 200, 0, 255 };
    LrgReelSolidClip *solid = lrg_reel_solid_clip_new (&green);
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba px;

    lrg_reel_add_clip (reel, LRG_REEL_CLIP (solid));
    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 4, 4, &px);
        g_assert_cmpint (px.g, ==, 200);
        g_assert_cmpint (px.a, ==, 255);
    }
    g_object_unref (solid);
}

static void
test_gradient_clip (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 16, 30.0, 2);
    GrlColor red = { 255, 0, 0, 255 };
    GrlColor blue = { 0, 0, 255, 255 };
    LrgReelGradientClip *grad =
        lrg_reel_gradient_clip_new_linear (&red, &blue, GRL_GRADIENT_AXIS_VERTICAL);
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba top;
    Rgba bottom;

    lrg_reel_add_clip (reel, LRG_REEL_CLIP (grad));
    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 4, 0, &top);
        read_px (f, 4, 15, &bottom);
        g_assert_cmpint (top.r, >, 150);     /* red at the top */
        g_assert_cmpint (bottom.b, >, 150);  /* blue at the bottom */
    }
    g_object_unref (grad);
}

static void
test_image_clip (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 2);
    GrlColor red = { 255, 0, 0, 255 };
    g_autoptr(GrlImage) src = grl_image_new_color (4, 4, &red);
    LrgReelImageClip *clip = lrg_reel_image_clip_new_from_image (src);
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba px;

    lrg_reel_image_clip_set_fit (clip, LRG_REEL_FIT_FILL);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (clip));
    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 8, 8, &px);
        g_assert_cmpint (px.r, ==, 255);  /* image stretched to fill the frame */
        g_assert_cmpint (px.a, ==, 255);
    }
    g_object_unref (clip);
}

static void
test_text_clip (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 64, 32, 30.0, 2);
    GrlColor white = { 255, 255, 255, 255 };
    LrgReelTextClip *text = lrg_reel_text_clip_new ("HI");
    g_autoptr(LrgReelRenderer) renderer = NULL;
    gint x;
    gint y;
    gint nonblank = 0;

    lrg_reel_text_clip_set_color (text, &white);
    lrg_reel_text_clip_set_text_x (text, 4);
    lrg_reel_text_clip_set_text_y (text, 8);
    lrg_reel_text_clip_set_font_size (text, 16);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (text));

    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);

        /* The bitmap glyphs leave some opaque pixels in the text area. */
        for (y = 0; y < 32; y++)
            for (x = 0; x < 64; x++)
            {
                Rgba px;
                read_px (f, x, y, &px);
                if (px.a > 0)
                    nonblank++;
            }
        g_assert_cmpint (nonblank, >, 0);
    }
    g_object_unref (text);
}

static void
test_shape_clip_rect (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 2);
    GrlColor green = { 0, 200, 0, 255 };
    LrgReelShapeClip *shape = lrg_reel_shape_clip_new_rect (2, 2, 6, 6);
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba inside;
    Rgba outside;

    lrg_reel_shape_clip_set_fill_color (shape, &green);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (shape));
    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 4, 4, &inside);
        read_px (f, 13, 13, &outside);
        g_assert_cmpint (inside.g, ==, 200);
        g_assert_cmpint (outside.a, ==, 0);
    }
    g_object_unref (shape);
}

static void
test_shape_clip_circle (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 2);
    GrlColor green = { 0, 200, 0, 255 };
    LrgReelShapeClip *shape = lrg_reel_shape_clip_new_circle (8, 8, 4);
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba center;
    Rgba corner;

    lrg_reel_shape_clip_set_fill_color (shape, &green);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (shape));
    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 8, 8, &center);
        read_px (f, 0, 0, &corner);
        g_assert_cmpint (center.g, ==, 200);  /* circle covers its centre */
        g_assert_cmpint (corner.a, ==, 0);    /* not the corner */
    }
    g_object_unref (shape);
}

/* ==========================================================================
 * Wave C: media-in (video decode, audio-from-file, FFT)
 * ========================================================================== */

/* Render a solid-color reel to an MP4 at @path (returns FALSE if no ffmpeg). */
static gboolean
make_test_mp4 (const gchar *path,
               Rgba        *col,
               gint         frames)
{
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    LrgReelVideoExporter *vid;
    g_autoptr(GError) error = NULL;
    gboolean ok;

    if (!lrg_reel_video_exporter_is_ffmpeg_available ())
        return FALSE;

    reel = lrg_reel_new ("src", 16, 16, 30.0, frames);
    {
        LrgReelClip *clip = lrg_reel_clip_new_with_func (fill_render, col, NULL);
        lrg_reel_add_clip (reel, clip);
        g_object_unref (clip);
    }
    renderer = lrg_reel_renderer_new (reel);
    vid = lrg_reel_video_exporter_new (path, LRG_REEL_VIDEO_CODEC_H264);
    ok = lrg_reel_renderer_render_to_exporter (renderer, LRG_REEL_EXPORTER (vid), &error);
    g_object_unref (vid);

    return ok;
}

static void
test_video_roundtrip (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *mp4 = NULL;
    g_autoptr(GError) error = NULL;
    LrgReelVideoSource *src;
    Rgba red = { 255, 0, 0, 255 };
    GrlImage *frame;
    g_autoptr(GrlColor) c = NULL;

    if (!lrg_reel_video_source_is_ffmpeg_available ())
    {
        g_test_skip ("ffmpeg/ffprobe not available");
        return;
    }

    dir = g_dir_make_tmp ("reel-vsrc-XXXXXX", &error);
    g_assert_no_error (error);
    mp4 = g_build_filename (dir, "src.mp4", NULL);

    g_assert_true (make_test_mp4 (mp4, &red, 6));

    src = lrg_reel_video_source_new_from_file (mp4, &error);
    g_assert_no_error (error);
    g_assert_nonnull (src);
    g_assert_cmpint (lrg_reel_video_source_get_width (src), ==, 16);
    g_assert_cmpint (lrg_reel_video_source_get_height (src), ==, 16);
    g_assert_cmpint (lrg_reel_video_source_get_frame_count (src), >=, 1);

    frame = lrg_reel_video_source_get_frame (src, 0, &error);
    g_assert_no_error (error);
    g_assert_nonnull (frame);
    c = grl_image_get_pixel (frame, 8, 8);
    /* h264 is lossy; the red survives as a strong red. */
    g_assert_cmpint (c->r, >, 150);
    g_assert_cmpint (c->g, <, 90);
    g_assert_cmpint (c->b, <, 90);

    g_object_unref (src);
    g_unlink (mp4);
    g_rmdir (dir);
}

static void
test_video_clip (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *mp4 = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    LrgReelVideoClip *clip;
    Rgba red = { 255, 0, 0, 255 };
    Rgba px;

    if (!lrg_reel_video_source_is_ffmpeg_available ())
    {
        g_test_skip ("ffmpeg/ffprobe not available");
        return;
    }

    dir = g_dir_make_tmp ("reel-vclip-XXXXXX", &error);
    g_assert_no_error (error);
    mp4 = g_build_filename (dir, "src.mp4", NULL);
    g_assert_true (make_test_mp4 (mp4, &red, 6));

    reel = lrg_reel_new ("t", 32, 32, 30.0, 4);
    clip = lrg_reel_video_clip_new_from_file (mp4, &error);
    g_assert_no_error (error);
    g_assert_nonnull (clip);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (clip));

    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 16, 16, &px);
        g_assert_cmpint (px.r, >, 150);  /* video frame fills the composition */
    }
    g_object_unref (clip);
    g_unlink (mp4);
    g_rmdir (dir);
}

static void
test_audio_from_file (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *wav = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgWaveData) wave = make_const_wave (8000, 1, 1.0f, 0.5f);
    g_autoptr(LrgReelAudioTrack) track = lrg_reel_audio_track_new (30.0);
    g_autoptr(LrgWaveData) mixed = NULL;
    gfloat *samples;
    gsize   count;

    dir = g_dir_make_tmp ("reel-aud-XXXXXX", &error);
    g_assert_no_error (error);
    wav = g_build_filename (dir, "tone.wav", NULL);
    g_assert_true (lrg_wave_data_export_wav (wave, wav, &error));
    g_assert_no_error (error);

    g_assert_true (lrg_reel_audio_track_add_from_file (track, wav, 0, 1.0, 0.0, 0.0, &error));
    g_assert_no_error (error);
    mixed = lrg_reel_audio_track_mix (track, 8000, 1, 30, &error);
    g_assert_no_error (error);

    samples = lrg_wave_data_get_samples (mixed, &count);
    g_assert_cmpfloat (fabs (samples[100] - 0.5f), <, 0.02);
    g_free (samples);

    g_unlink (wav);
    g_rmdir (dir);
}

/* Build a 1-second mono sine of @freq Hz at @rate. */
static LrgWaveData *
make_sine_wave (guint rate, gdouble freq)
{
    LrgWaveData *w = lrg_wave_data_new_procedural (rate, 1, 1.0f);
    guint        fc = lrg_wave_data_get_frame_count (w);
    gfloat      *s = g_new (gfloat, fc);
    guint        i;

    for (i = 0; i < fc; i++)
        s[i] = (gfloat) sin (2.0 * G_PI * freq * (gdouble) i / (gdouble) rate);
    lrg_wave_data_set_samples (w, s, fc);
    g_free (s);

    return w;
}

static void
test_fft_spectrum (void)
{
    g_autoptr(LrgWaveData) wave = make_sine_wave (8000, 1000.0);
    gfloat *bins;
    gsize   count;
    gsize   i;
    gsize   peak = 0;
    gfloat  peak_val = 0.0f;
    gint    expected_bin;

    /* bin width = rate/window = 8000/1024 = 7.8125 Hz; 1000Hz -> bin ~128. */
    bins = lrg_reel_audio_spectrum (wave, 15, 30.0, 512, 1024, &count);
    g_assert_nonnull (bins);
    g_assert_cmpuint (count, ==, 512);

    for (i = 1; i < count; i++)  /* skip DC */
        if (bins[i] > peak_val)
        {
            peak_val = bins[i];
            peak = i;
        }

    expected_bin = (gint) (1000.0 / (8000.0 / 1024.0) + 0.5);
    g_assert_cmpint (ABS ((gint) peak - expected_bin), <=, 3);
    g_free (bins);
}

static void
test_audio_level (void)
{
    g_autoptr(LrgWaveData) wave = make_const_wave (8000, 1, 1.0f, 0.5f);
    g_autoptr(LrgWaveData) silent = make_const_wave (8000, 1, 1.0f, 0.0f);
    gdouble level;

    level = lrg_reel_audio_level (wave, 5, 30.0, 0.1);
    g_assert_cmpfloat (fabs (level - 0.5), <, 0.05);  /* RMS of constant 0.5 */

    level = lrg_reel_audio_level (silent, 5, 30.0, 0.1);
    g_assert_cmpfloat (level, <, 0.01);
}

/* ==========================================================================
 * Wave D: effects, transitions, path motion
 * ========================================================================== */

static void
apply_effect (LrgReelEffect *fx,
              GrlImage      *img)
{
    g_autoptr(LrgReelContext) ctx =
        lrg_reel_context_new (0, 30.0, grl_image_get_width (img),
                              grl_image_get_height (img), 1);

    lrg_reel_effect_apply (fx, img, ctx);
}

static void
test_effect_chroma_key (void)
{
    GrlColor green = { 0, 255, 0, 255 };
    g_autoptr(GrlImage) img = grl_image_new_color (8, 8, &green);
    LrgReelChromaKeyEffect *ck = lrg_reel_chroma_key_effect_new (&green);
    g_autoptr(GrlColor) c = NULL;

    apply_effect (LRG_REEL_EFFECT (ck), img);
    c = grl_image_get_pixel (img, 4, 4);
    g_assert_cmpint (c->a, ==, 0);  /* the key colour is removed */
    g_object_unref (ck);
}

static void
test_effect_color_grade (void)
{
    GrlColor gray = { 100, 100, 100, 255 };
    g_autoptr(GrlImage) img = grl_image_new_color (4, 4, &gray);
    LrgReelColorGradeEffect *cg = lrg_reel_color_grade_effect_new ();
    g_autoptr(GrlColor) c = NULL;

    lrg_reel_color_grade_effect_set_brightness (cg, 0.4);
    apply_effect (LRG_REEL_EFFECT (cg), img);
    c = grl_image_get_pixel (img, 2, 2);
    g_assert_cmpint (c->r, >, 150);  /* brighter */
    g_object_unref (cg);
}

static void
test_effect_blur (void)
{
    GrlColor black = { 0, 0, 0, 255 };
    GrlColor white = { 255, 255, 255, 255 };
    g_autoptr(GrlImage) img = grl_image_new_color (16, 16, &black);
    LrgReelBlurEffect *blur = lrg_reel_blur_effect_new (3);
    g_autoptr(GrlColor) c = NULL;

    grl_image_draw_pixel (img, 8, 8, &white);
    apply_effect (LRG_REEL_EFFECT (blur), img);
    /* The white spreads to neighbours. */
    c = grl_image_get_pixel (img, 9, 8);
    g_assert_cmpint (c->r, >, 0);
    g_object_unref (blur);
}

static void
test_effect_vignette (void)
{
    GrlColor white = { 255, 255, 255, 255 };
    g_autoptr(GrlImage) img = grl_image_new_color (16, 16, &white);
    LrgReelVignetteEffect *vig = lrg_reel_vignette_effect_new ();
    g_autoptr(GrlColor) corner = NULL;
    g_autoptr(GrlColor) center = NULL;

    lrg_reel_vignette_effect_set_intensity (vig, 0.8);
    lrg_reel_vignette_effect_set_radius (vig, 0.2);
    apply_effect (LRG_REEL_EFFECT (vig), img);
    corner = grl_image_get_pixel (img, 0, 0);
    center = grl_image_get_pixel (img, 8, 8);
    g_assert_cmpint (corner->r, <, center->r);  /* corners darkened */
    g_object_unref (vig);
}

static void
test_effect_on_clip (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 16, 16, 30.0, 2);
    GrlColor green = { 0, 200, 0, 255 };
    LrgReelSolidClip *solid = lrg_reel_solid_clip_new (&green);
    LrgReelBlurEffect *blur = lrg_reel_blur_effect_new (2);
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba px;

    lrg_reel_clip_add_effect (LRG_REEL_CLIP (solid), LRG_REEL_EFFECT (blur));
    g_assert_cmpuint (lrg_reel_clip_get_n_effects (LRG_REEL_CLIP (solid)), ==, 1);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (solid));

    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);
        read_px (f, 8, 8, &px);
        g_assert_cmpint (px.g, >, 100);  /* clip still renders through the effect */
    }
    g_object_unref (solid);
    g_object_unref (blur);
}

/* Composite a transition and read the centre pixel at a given progress. */
static void
transition_center (LrgReelTransition *tr,
                   gdouble            progress,
                   Rgba              *out)
{
    GrlColor red = { 255, 0, 0, 255 };
    GrlColor blue = { 0, 0, 255, 255 };
    g_autoptr(GrlImage) from = grl_image_new_color (16, 16, &red);
    g_autoptr(GrlImage) to = grl_image_new_color (16, 16, &blue);
    g_autoptr(LrgImageCanvas) canvas = lrg_image_canvas_new (16, 16, NULL);

    lrg_reel_transition_composite (tr, canvas, from, to, progress);
    read_px (lrg_image_canvas_get_image (canvas), 8, 8, out);
}

static void
test_transition_iris (void)
{
    g_autoptr(LrgReelIrisTransition) iris = lrg_reel_iris_transition_new ();
    Rgba a;
    Rgba b;

    transition_center (LRG_REEL_TRANSITION (iris), 0.0, &a);
    transition_center (LRG_REEL_TRANSITION (iris), 1.0, &b);
    g_assert_cmpint (a.r, >, 200);  /* progress 0 -> from (red) */
    g_assert_cmpint (b.b, >, 200);  /* progress 1 -> to (blue) at centre */
}

static void
test_transition_push (void)
{
    g_autoptr(LrgReelPushTransition) push =
        lrg_reel_push_transition_new (LRG_REEL_TRANSITION_DIRECTION_LEFT);
    Rgba a;
    Rgba b;

    transition_center (LRG_REEL_TRANSITION (push), 0.0, &a);
    transition_center (LRG_REEL_TRANSITION (push), 1.0, &b);
    g_assert_cmpint (a.r, >, 200);
    g_assert_cmpint (b.b, >, 200);
}

static void
test_transition_zoom (void)
{
    g_autoptr(LrgReelZoomTransition) zoom = lrg_reel_zoom_transition_new ();
    Rgba a;
    Rgba b;

    transition_center (LRG_REEL_TRANSITION (zoom), 0.0, &a);
    transition_center (LRG_REEL_TRANSITION (zoom), 1.0, &b);
    g_assert_cmpint (a.r, >, 200);   /* from at progress 0 */
    g_assert_cmpint (b.b, >, 150);   /* to at progress 1 (zoomed full) */
}

static void
test_path_motion (void)
{
    g_autoptr(GrlPath) path = grl_path_new ();
    gdouble len;
    gdouble x = 0.0;
    gdouble y = 0.0;
    gdouble angle = 0.0;
    g_autoptr(GrlPath) half = NULL;

    grl_path_move_to (path, 0.0f, 0.0f);
    grl_path_line_to (path, 100.0f, 0.0f);

    len = lrg_reel_path_length (path);
    g_assert_cmpfloat (fabs (len - 100.0), <, 1.0);

    g_assert_true (lrg_reel_path_point_at (path, 0.5, &x, &y, &angle));
    g_assert_cmpfloat (fabs (x - 50.0), <, 2.0);
    g_assert_cmpfloat (fabs (y), <, 1.0);
    g_assert_cmpfloat (fabs (angle), <, 0.05);  /* horizontal tangent */

    g_assert_true (lrg_reel_path_point_at (path, 1.0, &x, &y, NULL));
    g_assert_cmpfloat (fabs (x - 100.0), <, 2.0);

    half = lrg_reel_path_evolve (path, 0.5);
    g_assert_cmpfloat (fabs (lrg_reel_path_length (half) - 50.0), <, 2.0);
}

/* Draws a 2px-wide marker whose X position tracks time (sub-frame sensitive). */
static void
moving_marker_render (LrgReelClip *clip, LrgReelContext *ctx, LrgImageCanvas *canvas, gpointer data)
{
    GrlColor white = { 255, 255, 255, 255 };
    gint     x = (gint) (lrg_reel_context_get_seconds (ctx) * 120.0);

    lrg_image_canvas_fill_rect (canvas, x, 0, 2, 8, &white);
}

static void
test_motion_blur (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 32, 8, 30.0, 4);
    LrgReelClip *clip;
    g_autoptr(LrgReelRenderer) sharp_r = NULL;
    g_autoptr(LrgReelRenderer) blur_r = NULL;
    Rgba sharp;
    Rgba blurred;

    clip = lrg_reel_clip_new_with_func (moving_marker_render, NULL, NULL);
    lrg_reel_add_clip (reel, clip);
    g_object_unref (clip);

    /* Without blur the marker sits at x=0 on frame 0; pixel 3 is empty. */
    sharp_r = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (sharp_r, 0);
        read_px (f, 3, 4, &sharp);
        g_assert_cmpint (sharp.a, ==, 0);
    }

    /* With blur the marker smears forward across the exposure window. */
    blur_r = lrg_reel_renderer_new (reel);
    lrg_reel_renderer_set_motion_blur (blur_r, 8);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (blur_r, 0);
        read_px (f, 3, 4, &blurred);
        g_assert_cmpint (blurred.a, >, 0);    /* smeared into pixel 3 */
        g_assert_cmpint (blurred.a, <, 255);  /* only partial coverage */
    }
}

/* ==========================================================================
 * Wave E: output completeness (codecs / audio-only / still)
 * ========================================================================== */

static void
test_render_still (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *png = NULL;
    g_autoptr(GError) error = NULL;
    Rgba red = { 255, 0, 0, 255 };
    g_autoptr(LrgReel) reel = make_fill_reel (&red, 3);
    g_autoptr(LrgReelRenderer) renderer = NULL;
    g_autoptr(GrlImage) loaded = NULL;
    g_autoptr(GrlColor) c = NULL;

    dir = g_dir_make_tmp ("reel-still-XXXXXX", &error);
    g_assert_no_error (error);
    png = g_build_filename (dir, "frame.png", NULL);

    renderer = lrg_reel_renderer_new (reel);
    g_assert_true (lrg_reel_renderer_render_still (renderer, 0, png, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_test (png, G_FILE_TEST_EXISTS));

    loaded = grl_image_new_from_file (png);
    g_assert_nonnull (loaded);
    g_assert_cmpint (grl_image_get_width (loaded), ==, 8);
    c = grl_image_get_pixel (loaded, 4, 4);
    g_assert_cmpint (c->r, ==, 255);

    g_unlink (png);
    g_rmdir (dir);
}

static void
test_audio_exporter_wav (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *wav = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgWaveData) wave = make_const_wave (8000, 1, 0.5f, 0.4f);
    g_autoptr(LrgReelAudioExporter) exp = NULL;
    g_autoptr(LrgWaveData) loaded = NULL;

    dir = g_dir_make_tmp ("reel-aexp-XXXXXX", &error);
    g_assert_no_error (error);
    wav = g_build_filename (dir, "out.wav", NULL);

    exp = lrg_reel_audio_exporter_new (wav, LRG_REEL_AUDIO_FORMAT_WAV);
    g_assert_true (lrg_reel_audio_exporter_export (exp, wave, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_test (wav, G_FILE_TEST_EXISTS));

    loaded = lrg_wave_data_new_from_file (wav, &error);
    g_assert_no_error (error);
    g_assert_nonnull (loaded);
    g_assert_cmpuint (lrg_wave_data_get_frame_count (loaded), >, 0);

    g_unlink (wav);
    g_rmdir (dir);
}

static void
test_video_codec_h265 (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *mp4 = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    LrgReelVideoExporter *vid;
    Rgba red = { 255, 0, 0, 255 };
    LrgReelClip *clip;
    gboolean ok;

    if (!lrg_reel_video_source_is_ffmpeg_available ())
    {
        g_test_skip ("ffmpeg/ffprobe not available");
        return;
    }

    dir = g_dir_make_tmp ("reel-h265-XXXXXX", &error);
    g_assert_no_error (error);
    mp4 = g_build_filename (dir, "out.mp4", NULL);

    reel = lrg_reel_new ("t", 16, 16, 30.0, 6);
    clip = lrg_reel_clip_new_with_func (fill_render, &red, NULL);
    lrg_reel_add_clip (reel, clip);
    g_object_unref (clip);

    renderer = lrg_reel_renderer_new (reel);
    vid = lrg_reel_video_exporter_new (mp4, LRG_REEL_VIDEO_CODEC_H265);
    ok = lrg_reel_renderer_render_to_exporter (renderer, LRG_REEL_EXPORTER (vid), &error);
    g_object_unref (vid);

    if (!ok)
    {
        g_test_skip ("libx265 encoder unavailable");
        g_clear_error (&error);
    }
    else
    {
        LrgReelVideoSource *src = lrg_reel_video_source_new_from_file (mp4, &error);
        g_assert_no_error (error);
        g_assert_nonnull (src);
        g_assert_cmpint (lrg_reel_video_source_get_width (src), ==, 16);
        g_object_unref (src);
    }

    g_unlink (mp4);
    g_rmdir (dir);
}

static void
test_video_alpha_roundtrip (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *png = NULL;
    g_autofree gchar *webm = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    LrgReelSeqExporter *seq;
    LrgReelVideoExporter *vid;
    LrgReelShapeClip *shape;
    GrlColor green = { 0, 200, 0, 255 };
    g_autoptr(GrlImage) loaded = NULL;
    g_autoptr(GrlColor) corner = NULL;
    g_autoptr(GrlColor) center = NULL;
    gboolean ok;

    dir = g_dir_make_tmp ("reel-alpha-XXXXXX", &error);
    g_assert_no_error (error);

    /* A reel with no background: corners stay transparent. */
    reel = lrg_reel_new ("t", 16, 16, 30.0, 4);
    shape = lrg_reel_shape_clip_new_rect (4, 4, 8, 8);
    lrg_reel_shape_clip_set_fill_color (shape, &green);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (shape));
    g_object_unref (shape);
    renderer = lrg_reel_renderer_new (reel);

    /* Deterministic guarantee: the PNG path preserves the composition alpha. */
    seq = lrg_reel_seq_exporter_new (dir, "a_%03d", LRG_REEL_IMAGE_FORMAT_PNG);
    g_assert_true (lrg_reel_renderer_render_to_exporter (renderer, LRG_REEL_EXPORTER (seq), &error));
    g_assert_no_error (error);
    g_object_unref (seq);

    png = g_build_filename (dir, "a_000.png", NULL);
    loaded = grl_image_new_from_file (png);
    g_assert_nonnull (loaded);
    corner = grl_image_get_pixel (loaded, 0, 0);
    center = grl_image_get_pixel (loaded, 8, 8);
    g_assert_cmpint (corner->a, ==, 0);      /* transparent corner preserved */
    g_assert_cmpint (center->a, ==, 255);    /* opaque shape centre */
    g_unlink (png);

    /* Best-effort: an alpha video codec round-trip (codec support varies). */
    if (lrg_reel_video_source_is_ffmpeg_available ())
    {
        webm = g_build_filename (dir, "out.webm", NULL);
        vid = lrg_reel_video_exporter_new (webm, LRG_REEL_VIDEO_CODEC_VP9_ALPHA);
        ok = lrg_reel_renderer_render_to_exporter (renderer, LRG_REEL_EXPORTER (vid), &error);
        g_object_unref (vid);
        g_clear_error (&error);

        if (ok)
        {
            LrgReelVideoSource *src = lrg_reel_video_source_new_from_file (webm, NULL);

            if (src != NULL)
            {
                GrlImage *frame = lrg_reel_video_source_get_frame (src, 0, NULL);

                if (frame != NULL)
                {
                    g_autoptr(GrlColor) c = grl_image_get_pixel (frame, 0, 0);

                    /* If this build's vp9 kept alpha, the corner is transparent. */
                    if (c->a >= 128)
                        g_test_message ("vp9-alpha not preserved by this ffmpeg build");
                }
                g_object_unref (src);
            }
            g_unlink (webm);
        }
    }

    g_rmdir (dir);
}

/* ==========================================================================
 * Wave G: parallel rendering + ranges
 * ==========================================================================
 * A minimal collecting exporter, and a frame-dependent clip, so we can prove
 * parallel output is pixel-identical to sequential.
 */

#define TEST_TYPE_MOCK_EXPORTER (test_mock_exporter_get_type ())
G_DECLARE_FINAL_TYPE (TestMockExporter, test_mock_exporter, TEST, MOCK_EXPORTER, LrgReelExporter)

struct _TestMockExporter
{
    LrgReelExporter parent_instance;
    GPtrArray      *frames;  /* of GrlImage* copies */
};

G_DEFINE_FINAL_TYPE (TestMockExporter, test_mock_exporter, LRG_TYPE_REEL_EXPORTER)

static gboolean
mock_exporter_begin (LrgReelExporter *e, gint w, gint h, gdouble fps, GError **error)
{
    return TRUE;
}

static gboolean
mock_exporter_add_frame (LrgReelExporter *e, GrlImage *image, GError **error)
{
    TestMockExporter *self = TEST_MOCK_EXPORTER (e);

    g_ptr_array_add (self->frames, grl_image_copy (image));
    return TRUE;
}

static gboolean
mock_exporter_finish (LrgReelExporter *e, GError **error)
{
    return TRUE;
}

static void
test_mock_exporter_finalize (GObject *object)
{
    TestMockExporter *self = TEST_MOCK_EXPORTER (object);

    g_ptr_array_unref (self->frames);
    G_OBJECT_CLASS (test_mock_exporter_parent_class)->finalize (object);
}

static void
test_mock_exporter_class_init (TestMockExporterClass *klass)
{
    LrgReelExporterClass *ec = LRG_REEL_EXPORTER_CLASS (klass);

    G_OBJECT_CLASS (klass)->finalize = test_mock_exporter_finalize;
    ec->begin = mock_exporter_begin;
    ec->add_frame = mock_exporter_add_frame;
    ec->finish = mock_exporter_finish;
}

static void
test_mock_exporter_init (TestMockExporter *self)
{
    self->frames = g_ptr_array_new_with_free_func (g_object_unref);
}

/* Fills the frame with a colour that depends on the frame number. */
static void
frame_color_render (LrgReelClip *clip, LrgReelContext *ctx, LrgImageCanvas *canvas, gpointer data)
{
    gint     f = lrg_reel_context_get_frame (ctx);
    GrlColor c;

    c.r = (guint8) ((f * 37) % 256);
    c.g = (guint8) ((f * 91) % 256);
    c.b = (guint8) ((f * 13 + 7) % 256);
    c.a = 255;
    lrg_image_canvas_clear (canvas, &c);
}

static void
test_render_range (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 10);
    LrgReelClip *clip;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    TestMockExporter *mock = g_object_new (TEST_TYPE_MOCK_EXPORTER, NULL);
    g_autoptr(GError) error = NULL;

    clip = lrg_reel_clip_new_with_func (frame_color_render, NULL, NULL);
    lrg_reel_add_clip (reel, clip);
    g_object_unref (clip);

    renderer = lrg_reel_renderer_new (reel);
    g_assert_true (lrg_reel_renderer_render_range (renderer, 3, 7,
                                                   LRG_REEL_EXPORTER (mock), &error));
    g_assert_no_error (error);
    g_assert_cmpuint (mock->frames->len, ==, 4);  /* frames [3,7) */

    g_object_unref (mock);
}

static void
test_parallel_determinism (void)
{
    g_autoptr(LrgReel) reel = lrg_reel_new ("t", 8, 8, 30.0, 16);
    LrgReelClip *clip;
    g_autoptr(LrgReelRenderer) seq_r = NULL;
    g_autoptr(LrgReelRenderer) par_r = NULL;
    TestMockExporter *seq_m = g_object_new (TEST_TYPE_MOCK_EXPORTER, NULL);
    TestMockExporter *par_m = g_object_new (TEST_TYPE_MOCK_EXPORTER, NULL);
    g_autoptr(GError) error = NULL;
    guint f;

    clip = lrg_reel_clip_new_with_func (frame_color_render, NULL, NULL);
    lrg_reel_add_clip (reel, clip);
    g_object_unref (clip);

    seq_r = lrg_reel_renderer_new (reel);
    par_r = lrg_reel_renderer_new (reel);

    g_assert_true (lrg_reel_renderer_render_range (seq_r, 0, 16,
                                                   LRG_REEL_EXPORTER (seq_m), &error));
    g_assert_no_error (error);
    g_assert_true (lrg_reel_renderer_render_parallel (par_r, 4,
                                                      LRG_REEL_EXPORTER (par_m), &error));
    g_assert_no_error (error);

    g_assert_cmpuint (seq_m->frames->len, ==, 16);
    g_assert_cmpuint (par_m->frames->len, ==, 16);

    /* Parallel output must be pixel-identical to sequential. */
    for (f = 0; f < 16; f++)
    {
        Rgba a;
        Rgba b;

        read_px (g_ptr_array_index (seq_m->frames, f), 4, 4, &a);
        read_px (g_ptr_array_index (par_m->frames, f), 4, 4, &b);
        g_assert_cmpint (a.r, ==, b.r);
        g_assert_cmpint (a.g, ==, b.g);
        g_assert_cmpint (a.b, ==, b.b);
    }

    g_object_unref (seq_m);
    g_object_unref (par_m);
}

/* ==========================================================================
 * Wave F: data-driven YAML + captions
 * ========================================================================== */

static void
test_yaml_load (void)
{
    const gchar *yaml =
        "id: t\n"
        "width: 16\n"
        "height: 16\n"
        "fps: 30\n"
        "duration-in-frames: 4\n"
        "clips:\n"
        "  - type: solid-clip\n"
        "    color: \"#3050a0ff\"\n"
        "  - type: shape-clip\n"
        "    kind: circle\n"
        "    shape-x: 8\n"
        "    shape-y: 8\n"
        "    shape-radius: 4\n"
        "    fill-color: \"#ff8800ff\"\n";
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = lrg_reel_load_yaml_string (yaml, NULL, &error);
    g_autoptr(LrgReelRenderer) renderer = NULL;
    Rgba corner;
    Rgba center;

    g_assert_no_error (error);
    g_assert_nonnull (reel);
    g_assert_cmpint (lrg_reel_get_width (reel), ==, 16);
    g_assert_cmpint (lrg_reel_get_height (reel), ==, 16);
    g_assert_cmpint (lrg_reel_get_duration_in_frames (reel), ==, 4);

    renderer = lrg_reel_renderer_new (reel);
    {
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 0);

        /* The solid fill colour (#3050a0) shows where the circle isn't. */
        read_px (f, 0, 0, &corner);
        g_assert_cmpint (corner.r, ==, 0x30);
        g_assert_cmpint (corner.g, ==, 0x50);
        g_assert_cmpint (corner.b, ==, 0xa0);

        /* The orange circle (#ff8800) covers the centre. */
        read_px (f, 8, 8, &center);
        g_assert_cmpint (center.r, ==, 0xff);
        g_assert_cmpint (center.g, ==, 0x88);
    }
}

static void
test_yaml_unknown_type (void)
{
    const gchar *yaml =
        "id: t\nwidth: 8\nheight: 8\nfps: 30\nduration-in-frames: 1\n"
        "clips:\n  - type: not-a-real-clip\n";
    g_autoptr(GError) error = NULL;
    LrgReel *reel = lrg_reel_load_yaml_string (yaml, NULL, &error);

    g_assert_null (reel);
    g_assert_nonnull (error);  /* unknown clip type is reported */
}

static void
test_caption_clip (void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *srt = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    LrgReelCaptionClip *cap;
    const gchar *srt_data =
        "1\n00:00:00,000 --> 00:00:02,000\nHELLO\n\n";
    gint x;
    gint y;
    gint nonblank = 0;

    dir = g_dir_make_tmp ("reel-cap-XXXXXX", &error);
    g_assert_no_error (error);
    srt = g_build_filename (dir, "c.srt", NULL);
    g_assert_true (g_file_set_contents (srt, srt_data, -1, &error));
    g_assert_no_error (error);

    cap = lrg_reel_caption_clip_new_from_srt (srt, &error);
    g_assert_no_error (error);
    g_assert_nonnull (cap);

    reel = lrg_reel_new ("t", 128, 48, 30.0, 60);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (cap));

    renderer = lrg_reel_renderer_new (reel);
    {
        /* Frame 15 == 0.5s, inside the [0,2s) cue: caption text is drawn. */
        g_autoptr(GrlImage) f = lrg_reel_renderer_render_frame (renderer, 15);

        for (y = 0; y < 48; y++)
            for (x = 0; x < 128; x++)
            {
                Rgba px;
                read_px (f, x, y, &px);
                if (px.a > 0)
                    nonblank++;
            }
        g_assert_cmpint (nonblank, >, 0);
    }

    g_object_unref (cap);
    g_unlink (srt);
    g_rmdir (dir);
}

/* ==========================================================================
 * Wave H: engine-native GPU capture (display-gated)
 * ========================================================================== */

static void
test_gpu_capture (void)
{
    GrlColor red = { 200, 30, 30, 255 };
    LrgReelGpuRenderer *gpu;
    g_autoptr(GError) error = NULL;
    g_autoptr(GrlImage) a = NULL;
    g_autoptr(GrlImage) b = NULL;
    g_autoptr(GrlColor) ca = NULL;
    g_autoptr(GrlColor) cb = NULL;

    if (!lrg_reel_gpu_renderer_is_available ())
    {
        g_test_skip ("no display available for GPU capture");
        return;
    }

    gpu = lrg_reel_gpu_renderer_new (64, 48, 30.0, 4, &error);
    if (gpu == NULL)
    {
        g_test_skip ("GPU renderer could not initialize");
        g_clear_error (&error);
        return;
    }

    lrg_reel_gpu_renderer_set_clear_color (gpu, &red);
    a = lrg_reel_gpu_renderer_capture_frame (gpu, 0);
    b = lrg_reel_gpu_renderer_capture_frame (gpu, 0);

    g_assert_nonnull (a);
    g_assert_cmpint (grl_image_get_width (a), ==, 64);
    g_assert_cmpint (grl_image_get_height (a), ==, 48);

    ca = grl_image_get_pixel (a, 32, 24);
    cb = grl_image_get_pixel (b, 32, 24);
    g_assert_cmpint (ca->r, >, 150);        /* framebuffer cleared to red */
    g_assert_cmpint (ca->r, ==, cb->r);     /* same frame twice -> identical */

    g_object_unref (gpu);
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

    g_test_add_func ("/reel/transform/translate", test_transform_translate);
    g_test_add_func ("/reel/transform/scale", test_transform_scale);
    g_test_add_func ("/reel/transform/blend-multiply", test_clip_blend_multiply);
    g_test_add_func ("/reel/transform/nested-opacity", test_nested_clip_opacity);

    g_test_add_func ("/reel/anim/color", test_color_interpolate);
    g_test_add_func ("/reel/anim/bezier", test_easing_bezier);
    g_test_add_func ("/reel/anim/random", test_random_deterministic);
    g_test_add_func ("/reel/anim/noise", test_noise);

    g_test_add_func ("/reel/clip/solid", test_solid_clip);
    g_test_add_func ("/reel/clip/gradient", test_gradient_clip);
    g_test_add_func ("/reel/clip/image", test_image_clip);
    g_test_add_func ("/reel/clip/text", test_text_clip);
    g_test_add_func ("/reel/clip/shape-rect", test_shape_clip_rect);
    g_test_add_func ("/reel/clip/shape-circle", test_shape_clip_circle);

    g_test_add_func ("/reel/video/roundtrip", test_video_roundtrip);
    g_test_add_func ("/reel/video/clip", test_video_clip);
    g_test_add_func ("/reel/audio/from-file", test_audio_from_file);
    g_test_add_func ("/reel/audio/fft-spectrum", test_fft_spectrum);
    g_test_add_func ("/reel/audio/level", test_audio_level);

    g_test_add_func ("/reel/effect/chroma-key", test_effect_chroma_key);
    g_test_add_func ("/reel/effect/color-grade", test_effect_color_grade);
    g_test_add_func ("/reel/effect/blur", test_effect_blur);
    g_test_add_func ("/reel/effect/vignette", test_effect_vignette);
    g_test_add_func ("/reel/effect/on-clip", test_effect_on_clip);

    g_test_add_func ("/reel/transition/iris", test_transition_iris);
    g_test_add_func ("/reel/transition/push", test_transition_push);
    g_test_add_func ("/reel/transition/zoom", test_transition_zoom);

    g_test_add_func ("/reel/path/motion", test_path_motion);
    g_test_add_func ("/reel/motionblur/smear", test_motion_blur);

    g_test_add_func ("/reel/output/still", test_render_still);
    g_test_add_func ("/reel/output/audio-wav", test_audio_exporter_wav);
    g_test_add_func ("/reel/output/codec-h265", test_video_codec_h265);
    g_test_add_func ("/reel/output/alpha-roundtrip", test_video_alpha_roundtrip);

    g_test_add_func ("/reel/parallel/range", test_render_range);
    g_test_add_func ("/reel/parallel/determinism", test_parallel_determinism);

    g_test_add_func ("/reel/yaml/load", test_yaml_load);
    g_test_add_func ("/reel/yaml/unknown-type", test_yaml_unknown_type);
    g_test_add_func ("/reel/caption/srt", test_caption_clip);

    g_test_add_func ("/reel/gpu/capture", test_gpu_capture);

    return g_test_run ();
}
