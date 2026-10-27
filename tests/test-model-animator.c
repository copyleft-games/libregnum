/* test-model-animator.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgModelAnimator / LrgModelAnimState. Clip lookup,
 * aliases, suffix matching, truncated-name repair, loop wrap / clamp,
 * crossfade weights and the apply() pose-key dedupe run headlessly; a
 * hidden-window test poses and draws a real skinned GLB fixture, and the
 * skinning tests render a multi-bone tube on the GPU and on the CPU and
 * compare the images.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-test-glb.h"

#define EPS (1e-9)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0'))
        return FALSE;

    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (8, 8, "lrg-model-animator-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

/* Clip table used by most tests:
 *   0 "Armature|Idle"      60 frames
 *   1 "Armature|Walk"      30 frames
 *   2 "Rat_Walk"           10 frames
 *   3 "Sidewalk"           10 frames
 *   4 "Jump_Idle"          20 frames
 *   5 "Attack"             12 frames
 *   6 "Empty"               0 frames (invalid)
 */
static LrgModelAnimator *
make_animator (void)
{
    static const gchar *const names[] = {
        "Armature|Idle", "Armature|Walk", "Rat_Walk", "Sidewalk", "Jump_Idle", "Attack", "Empty"
    };
    static const gint frames[] = { 60, 30, 10, 10, 20, 12, 0 };

    return lrg_model_animator_new_headless (names, frames, G_N_ELEMENTS (names));
}

/* ========================================================================== */
/*                                  State                                     */
/* ========================================================================== */

static void
test_state_boxed (void)
{
    g_autoptr(LrgModelAnimState) state = lrg_model_anim_state_new ();
    g_autoptr(LrgModelAnimState) copy = NULL;

    g_assert_cmpint (state->clip, ==, -1);
    g_assert_cmpint (state->previous_clip, ==, -1);
    g_assert_cmpfloat (state->rate, ==, 1.0);
    g_assert_true (state->loop);
    g_assert_cmpfloat (lrg_model_anim_state_get_blend (state), ==, 1.0);

    state->clip = 3;
    state->time = 1.25;
    copy = lrg_model_anim_state_copy (state);
    g_assert_cmpint (copy->clip, ==, 3);
    g_assert_cmpfloat (copy->time, ==, 1.25);
    g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_MODEL_ANIM_STATE));

    lrg_model_anim_state_reset (copy);
    g_assert_cmpint (copy->clip, ==, -1);
    g_assert_cmpfloat (copy->time, ==, 0.0);
}

/* ========================================================================== */
/*                                  Lookup                                    */
/* ========================================================================== */

static void
test_clip_table (void)
{
    g_autoptr(LrgModelAnimator) animator = make_animator ();

    g_assert_null (lrg_model_animator_get_model (animator));
    g_assert_cmpuint (lrg_model_animator_get_clip_count (animator), ==, 7);
    g_assert_cmpstr (lrg_model_animator_get_clip_name (animator, 1), ==, "Armature|Walk");
    g_assert_null (lrg_model_animator_get_clip_name (animator, 7));
    g_assert_null (lrg_model_animator_get_clip_name (animator, -1));
    g_assert_cmpint (lrg_model_animator_get_clip_frame_count (animator, 1), ==, 30);
    g_assert_cmpfloat_with_epsilon (lrg_model_animator_get_clip_duration (animator, 1), 0.5, EPS);
    g_assert_cmpfloat (lrg_model_animator_get_clip_duration (animator, 99), ==, 0.0);
    g_assert_true (lrg_model_animator_clip_is_valid (animator, 0));
    g_assert_false (lrg_model_animator_clip_is_valid (animator, 6));
    g_assert_false (lrg_model_animator_clip_is_valid (animator, -1));
}

static void
test_find_clip (void)
{
    g_autoptr(LrgModelAnimator) animator = make_animator ();

    /* Exact. */
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "Sidewalk"), ==, 3);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "Attack"), ==, 5);

    /* Suffix, case-insensitive, at a word boundary; shortest name wins. */
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "idle"), ==, 0);
    /* '|' ranks above '_': "Armature|Walk" beats the shorter "Rat_Walk". */
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "WALK"), ==, 1);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "rat_walk"), ==, 2);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "jump_idle"), ==, 4);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "attack"), ==, 5);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "alk"), ==, -1);  /* no boundary */
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "Run"), ==, -1);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, ""), ==, -1);

    /* Alias to an exact name, then to a suffix. */
    lrg_model_animator_set_alias (animator, "walk", "Armature|Walk");
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "walk"), ==, 1);
    lrg_model_animator_set_alias (animator, "stroll", "sidewalk");
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "stroll"), ==, 3);
    /* An exact name still beats an alias with the same text. */
    lrg_model_animator_set_alias (animator, "Attack", "Armature|Idle");
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "Attack"), ==, 5);
    /* An alias to nothing falls back to suffix matching of the alias. */
    lrg_model_animator_set_alias (animator, "jump_idle", "Nope");
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "jump_idle"), ==, 4);

    /* Removal. */
    lrg_model_animator_set_alias (animator, "walk", "rat_walk");
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "walk"), ==, 2);
    lrg_model_animator_set_alias (animator, "walk", NULL);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "walk"), ==, 1);
    lrg_model_animator_clear_aliases (animator);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "stroll"), ==, -1);
}

static void
test_names_from_info (void)
{
    static const gchar *const full[] = {
        "AnimalArmature|AnimalArmature|AnimalArmature|Idle",
        "AnimalArmature|AnimalArmature|AnimalArmature|Walk"
    };
    g_autoptr(GError)           error = NULL;
    g_autoptr(LrgGltfInfo)      info = NULL;
    g_autoptr(LrgModelAnimator) animator = NULL;
    g_autoptr(LrgModelAnimator) mismatched = NULL;
    g_autofree gchar           *path = g_build_filename (g_get_tmp_dir (), "lrg-animator-names.glb", NULL);
    g_autofree gchar           *truncated0 = NULL;
    g_autofree gchar           *truncated1 = NULL;
    const gchar                *names[2];
    const gchar                *other[2] = { "Idle", "Walk" };
    const gint                  frames[2] = { 31, 31 };

    test_glb_write_skinned_triangle (path, full, 2);
    info = lrg_gltf_info_new_from_file (path, &error);
    g_assert_no_error (error);
    g_remove (path);

    /* What raylib reports: both names identical after truncation. */
    truncated0 = lrg_gltf_info_get_animation_raylib_name (info, 0);
    truncated1 = lrg_gltf_info_get_animation_raylib_name (info, 1);
    g_assert_cmpstr (truncated0, ==, truncated1);
    names[0] = truncated0;
    names[1] = truncated1;

    animator = lrg_model_animator_new_headless (names, frames, 2);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "walk"), ==, -1);
    g_assert_true (lrg_model_animator_set_clip_names_from_info (animator, info));
    g_assert_cmpstr (lrg_model_animator_get_clip_name (animator, 1), ==, full[1]);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "walk"), ==, 1);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "Idle"), ==, 0);

    /* Names that are not prefixes: rejected, nothing changes. */
    mismatched = lrg_model_animator_new_headless (other, frames, 2);
    g_assert_false (lrg_model_animator_set_clip_names_from_info (mismatched, info));
    g_assert_cmpstr (lrg_model_animator_get_clip_name (mismatched, 0), ==, "Idle");
}

/* ========================================================================== */
/*                                 Playback                                   */
/* ========================================================================== */

static void
test_loop_wrap (void)
{
    g_autoptr(LrgModelAnimator) animator = make_animator ();
    LrgModelAnimState state;

    lrg_model_anim_state_reset (&state);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, -1);

    g_assert_true (lrg_model_animator_play (animator, &state, 1, TRUE, 0));
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 0);

    /* 30 frames = 0.5 s. 0.6 s wraps to 0.1 s = frame 6. */
    lrg_model_animator_advance (animator, &state, 0.6);
    g_assert_cmpfloat_with_epsilon (state.time, 0.1, 1e-9);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 6);
    g_assert_false (lrg_model_animator_is_finished (animator, &state));

    /* Many small steps stay bounded. */
    {
        gint i;

        for (i = 0; i < 1000; i++)
            lrg_model_animator_advance (animator, &state, 1.0 / 60.0);
        g_assert_cmpfloat (state.time, >=, 0.0);
        g_assert_cmpfloat (state.time, <, 0.5);
    }

    /* Rate scales time; negative rate wraps backwards. */
    state.time = 0.0;
    state.rate = 2.0;
    lrg_model_animator_advance (animator, &state, 0.1);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 12);
    state.rate = -1.0;
    lrg_model_animator_advance (animator, &state, 0.25);
    g_assert_cmpfloat_with_epsilon (state.time, 0.45, 1e-9);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 27);

    /* Invalid dt is ignored. */
    lrg_model_animator_advance (animator, &state, -1.0);
    lrg_model_animator_advance (animator, &state, NAN);
    lrg_model_animator_advance (animator, &state, INFINITY);
    g_assert_cmpfloat_with_epsilon (state.time, 0.45, 1e-9);

    /* frame_of also wraps a time set by hand. */
    state.time = 1.2;
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 72 % 30);
}

static void
test_clamp_one_shot (void)
{
    g_autoptr(LrgModelAnimator) animator = make_animator ();
    LrgModelAnimState state;

    lrg_model_anim_state_reset (&state);
    g_assert_true (lrg_model_animator_play (animator, &state, 5, FALSE, 0));  /* 12 frames */
    lrg_model_animator_advance (animator, &state, 0.1);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 6);
    g_assert_false (lrg_model_animator_is_finished (animator, &state));

    /* Holds the last frame (11 at 11/60 s). */
    lrg_model_animator_advance (animator, &state, 5.0);
    g_assert_cmpfloat_with_epsilon (state.time, 11.0 / 60.0, 1e-12);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 11);
    g_assert_true (lrg_model_animator_is_finished (animator, &state));

    /* Replaying the same clip does not restart it; restart() does. */
    g_assert_true (lrg_model_animator_play (animator, &state, 5, FALSE, 0));
    g_assert_true (lrg_model_animator_is_finished (animator, &state));
    lrg_model_animator_restart (animator, &state);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 0);
    g_assert_false (lrg_model_animator_is_finished (animator, &state));

    /* Switching to looping keeps the time. */
    lrg_model_animator_advance (animator, &state, 0.05);
    g_assert_true (lrg_model_animator_play (animator, &state, 5, TRUE, 0));
    g_assert_true (state.loop);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 3);

    /* Backwards one-shot finishes at time 0. */
    g_assert_true (lrg_model_animator_play (animator, &state, 5, FALSE, 0));
    state.rate = -1.0;
    lrg_model_animator_advance (animator, &state, 1.0);
    g_assert_cmpfloat (state.time, ==, 0.0);
    g_assert_true (lrg_model_animator_is_finished (animator, &state));

    /* Out-of-range clips are refused; -1 stops. */
    g_assert_false (lrg_model_animator_play (animator, &state, 7, TRUE, 0));
    g_assert_false (lrg_model_animator_play (animator, &state, -2, TRUE, 0));
    g_assert_cmpint (state.clip, ==, 5);
    g_assert_true (lrg_model_animator_play (animator, &state, -1, TRUE, 0.3));
    g_assert_cmpint (state.clip, ==, -1);
    g_assert_cmpint (state.previous_clip, ==, -1);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, -1);
}

static void
test_crossfade (void)
{
    g_autoptr(LrgModelAnimator) animator = make_animator ();
    LrgModelAnimState state;

    lrg_model_anim_state_reset (&state);

    /* No fade from "no clip". */
    lrg_model_animator_play (animator, &state, 0, TRUE, 0.5);
    g_assert_cmpint (state.previous_clip, ==, -1);
    lrg_model_animator_advance (animator, &state, 0.25);

    /* Idle -> Walk over 0.2 s. */
    lrg_model_animator_play (animator, &state, 1, TRUE, 0.2);
    g_assert_cmpint (state.clip, ==, 1);
    g_assert_cmpint (state.previous_clip, ==, 0);
    g_assert_cmpfloat_with_epsilon (state.previous_time, 0.25, EPS);
    g_assert_cmpfloat (state.time, ==, 0.0);
    g_assert_cmpfloat (lrg_model_anim_state_get_blend (&state), ==, 0.0);
    g_assert_cmpint (lrg_model_animator_previous_frame_of (animator, &state), ==, 15);

    lrg_model_animator_advance (animator, &state, 0.05);
    g_assert_cmpfloat_with_epsilon (lrg_model_anim_state_get_blend (&state), 0.25, 1e-9);
    lrg_model_animator_advance (animator, &state, 0.05);
    g_assert_cmpfloat_with_epsilon (lrg_model_anim_state_get_blend (&state), 0.5, 1e-9);
    /* The outgoing clip keeps running. */
    g_assert_cmpfloat_with_epsilon (state.previous_time, 0.35, 1e-9);
    g_assert_cmpint (lrg_model_animator_previous_frame_of (animator, &state), ==, 21);

    /* Fade completes: previous dropped, full weight. */
    lrg_model_animator_advance (animator, &state, 0.15);
    g_assert_cmpint (state.previous_clip, ==, -1);
    g_assert_cmpfloat (lrg_model_anim_state_get_blend (&state), ==, 1.0);
    g_assert_cmpint (lrg_model_animator_previous_frame_of (animator, &state), ==, -1);

    /* No fade into an invalid clip's slot: fading from an invalid clip
     * cuts instead. */
    state.clip = 6;
    lrg_model_animator_play (animator, &state, 1, TRUE, 0.3);
    g_assert_cmpint (state.previous_clip, ==, -1);

    /* Zero, negative and NaN fades cut. */
    lrg_model_animator_play (animator, &state, 0, TRUE, 0);
    g_assert_cmpint (state.previous_clip, ==, -1);
    lrg_model_animator_play (animator, &state, 1, TRUE, -1);
    g_assert_cmpint (state.previous_clip, ==, -1);
    lrg_model_animator_play (animator, &state, 0, TRUE, NAN);
    g_assert_cmpint (state.previous_clip, ==, -1);
}

/* ========================================================================== */
/*                                  apply                                     */
/* ========================================================================== */

static void
test_apply_dedupe (void)
{
    g_autoptr(LrgModelAnimator) animator = make_animator ();
    LrgModelAnimState a;
    LrgModelAnimState b;

    lrg_model_anim_state_reset (&a);
    lrg_model_anim_state_reset (&b);

    /* Nothing to pose without a valid clip. */
    g_assert_false (lrg_model_animator_apply (animator, &a));
    a.clip = 6;
    g_assert_false (lrg_model_animator_apply (animator, &a));

    lrg_model_animator_play (animator, &a, 1, TRUE, 0);
    g_assert_true (lrg_model_animator_apply (animator, &a));
    g_assert_false (lrg_model_animator_apply (animator, &a));

    /* Within the same frame: no re-skin. */
    lrg_model_animator_advance (animator, &a, 0.01);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &a), ==, 0);
    g_assert_false (lrg_model_animator_apply (animator, &a));

    /* Next frame. */
    lrg_model_animator_advance (animator, &a, 0.01);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &a), ==, 1);
    g_assert_true (lrg_model_animator_apply (animator, &a));

    /* A second actor on the same clip and frame shares the pose. */
    lrg_model_animator_play (animator, &b, 1, TRUE, 0);
    b.time = a.time;
    g_assert_false (lrg_model_animator_apply (animator, &b));
    /* ... a different frame does not. */
    b.time = 0.3;
    g_assert_true (lrg_model_animator_apply (animator, &b));
    g_assert_true (lrg_model_animator_apply (animator, &a));

    /* invalidate() forces the next apply. */
    lrg_model_animator_invalidate (animator);
    g_assert_true (lrg_model_animator_apply (animator, &a));
    g_assert_false (lrg_model_animator_apply (animator, &a));

    /* Same frame index in another clip is a different pose. */
    b.clip = 0;
    b.time = a.time;
    g_assert_true (lrg_model_animator_apply (animator, &b));
}

/*
 * test_apply_non_finite:
 *
 * Regression: a non-finite time mapped to frame -1, which apply() handed
 * to raylib as keyframe -1 (an out-of-bounds read). The current clip is
 * then not posed; an unusable previous time drops the crossfade.
 */
static void
test_apply_non_finite (void)
{
    g_autoptr(LrgModelAnimator) animator = make_animator ();
    LrgModelAnimState state;

    lrg_model_anim_state_reset (&state);
    lrg_model_animator_play (animator, &state, 1, TRUE, 0);
    state.time = NAN;
    g_assert_false (lrg_model_animator_apply (animator, &state));
    state.time = INFINITY;
    g_assert_false (lrg_model_animator_apply (animator, &state));

    /* Mid-fade with a broken previous time: the plain current pose, the
     * same key as the finished fade (so the second apply is deduplicated). */
    lrg_model_anim_state_reset (&state);
    lrg_model_animator_play (animator, &state, 0, TRUE, 0);
    lrg_model_animator_play (animator, &state, 1, TRUE, 1.0);
    state.rate = 0.0;
    state.previous_time = NAN;
    g_assert_true (lrg_model_animator_apply (animator, &state));
    state.previous_clip = -1;
    g_assert_false (lrg_model_animator_apply (animator, &state));
}

static void
test_apply_crossfade_quantized (void)
{
    g_autoptr(LrgModelAnimator) animator = make_animator ();
    LrgModelAnimState state;

    lrg_model_anim_state_reset (&state);
    lrg_model_animator_play (animator, &state, 0, TRUE, 0);
    /* rate 0 freezes both clips so only the blend weight moves. */
    state.rate = 0.0;
    g_assert_true (lrg_model_animator_apply (animator, &state));

    lrg_model_animator_play (animator, &state, 1, TRUE, 1.0);
    g_assert_true (lrg_model_animator_apply (animator, &state));  /* blend level 0 */

    /* 1/64 of a second over a 1 s fade: half a level, rounds to 0 or 1. */
    lrg_model_animator_advance (animator, &state, 0.004);
    g_assert_false (lrg_model_animator_apply (animator, &state));
    lrg_model_animator_advance (animator, &state, 0.010);   /* ~0.9 levels -> 1 */
    g_assert_true (lrg_model_animator_apply (animator, &state));
    lrg_model_animator_advance (animator, &state, 0.002);
    g_assert_false (lrg_model_animator_apply (animator, &state));

    /* Near the end the weight quantizes to full: plain current pose, the
     * same key as a finished fade. */
    state.fade = 0.999;
    g_assert_true (lrg_model_animator_apply (animator, &state));
    lrg_model_animator_advance (animator, &state, 0.5);
    g_assert_cmpint (state.previous_clip, ==, -1);
    g_assert_false (lrg_model_animator_apply (animator, &state));
}

static void
test_headless_mask_and_draw (void)
{
    g_autoptr(LrgModelAnimator) animator = make_animator ();
    g_autoptr(LrgGltfInfo)      info = NULL;
    g_autoptr(GBytes)           mask = NULL;
    g_autoptr(GBytes)           input = NULL;
    static const gchar         *hidden[] = { "Hat", NULL };
    static const gchar         *json =
        "{\"nodes\":[{\"name\":\"Body\",\"mesh\":0},{\"name\":\"Hat\",\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{}}]}]}";
    GrlMatrix                   identity = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    input = test_glb_build (json, NULL, 0);
    info = lrg_gltf_info_new_from_bytes (input, NULL);
    g_assert_nonnull (info);

    mask = lrg_model_animator_build_node_mask (animator, info, hidden);
    g_assert_nonnull (mask);
    g_assert_cmpuint (g_bytes_get_size (mask), ==, 2);
    g_assert_cmpuint (((const guint8 *)g_bytes_get_data (mask, NULL))[0], ==, 1);
    g_assert_cmpuint (((const guint8 *)g_bytes_get_data (mask, NULL))[1], ==, 0);

    /* Drawing without a model is a no-op. */
    lrg_model_animator_draw_masked (animator, mask, &identity, NULL);
    lrg_model_animator_draw_masked_ex (animator, NULL, 0, 0, 0, 0, 1, NULL);
}

/* ========================================================================== */
/*                               GL (display)                                 */
/* ========================================================================== */

static void
test_real_model (void)
{
    static const gchar *const clips[] = { "Armature|Idle", "Armature|Lift" };
    g_autoptr(GError)           error = NULL;
    g_autoptr(LrgAssetManager)  manager = NULL;
    g_autoptr(LrgModelAnimator) animator = NULL;
    g_autoptr(LrgGltfInfo)      info = NULL;
    g_autoptr(GBytes)           mask = NULL;
    g_autofree gchar           *path = NULL;
    static const gchar         *hidden[] = { "Body", NULL };
    GrlModel                   *model;
    GPtrArray                  *animations;
    Model                      *raw;
    LrgModelAnimState           state;
    Camera3D                    camera;
    GrlColor                    red = { 255, 0, 0, 255 };

    if (!graphics_available)
    {
        g_test_skip ("Graphics context not available");
        return;
    }

    path = g_build_filename (g_get_tmp_dir (), "lrg-animator-real.glb", NULL);
    test_glb_write_skinned_triangle (path, clips, 2);

    manager = lrg_asset_manager_new ();
    model = lrg_asset_manager_load_model (manager, path, &error);
    g_assert_no_error (error);
    animations = lrg_asset_manager_load_model_animations (manager, path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (animations->len, ==, 2);
    info = lrg_gltf_info_new_from_file (path, &error);
    g_assert_no_error (error);

    animator = lrg_model_animator_new (model, animations);
    g_assert_true (lrg_model_animator_get_model (animator) == model);
    g_assert_cmpint (lrg_model_animator_find_clip (animator, "lift"), ==, 1);
    g_assert_true (lrg_model_animator_clip_is_valid (animator, 1));
    g_assert_cmpint (lrg_model_animator_get_clip_frame_count (animator, 1), ==, 31);

    /* The fixture lifts its bone by 1 over 0.5 s: frame 30 is fully up. */
    raw = grl_model_get_handle (model);
    g_assert_nonnull (raw->boneMatrices);
    lrg_model_anim_state_reset (&state);
    lrg_model_animator_play (animator, &state, 1, FALSE, 0);
    lrg_model_animator_advance (animator, &state, 1.0);
    g_assert_cmpint (lrg_model_animator_frame_of (animator, &state), ==, 30);
    g_assert_true (lrg_model_animator_apply (animator, &state));
    g_assert_cmpfloat_with_epsilon (raw->boneMatrices[0].m13, 1.0, 1e-4);
    g_assert_false (lrg_model_animator_apply (animator, &state));

    /* Halfway through a crossfade back to frame 0 of the same motion. */
    lrg_model_animator_play (animator, &state, 0, TRUE, 1.0);
    state.rate = 0.0;
    lrg_model_animator_advance (animator, &state, 0.5);
    g_assert_true (lrg_model_animator_apply (animator, &state));
    g_assert_cmpfloat_with_epsilon (raw->boneMatrices[0].m13, 0.5, 1e-3);

    mask = lrg_model_animator_build_node_mask (animator, info, hidden);
    g_assert_nonnull (mask);

    /* Drawing must work inside a 3D pass, masked and unmasked. */
    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3) { 0, 1, 4 };
    camera.up = (Vector3) { 0, 1, 0 };
    camera.fovy = 45;
    camera.projection = CAMERA_PERSPECTIVE;
    BeginDrawing ();
    ClearBackground (BLACK);
    BeginMode3D (camera);
    lrg_model_animator_draw_masked_ex (animator, NULL, 0, 0, 0, 0.5f, 1, &red);
    lrg_model_animator_draw_masked_ex (animator, mask, 1, 0, 0, 0, 2, NULL);
    EndMode3D ();
    EndDrawing ();

    g_clear_object (&animator);
    g_clear_object (&manager);
    g_remove (path);
}

/* ========================================================================== */
/*                     GPU vs CPU skinning (display)                          */
/* ========================================================================== */

#define SKIN_IMAGE_SIZE (160)

/* raylib's default program, as a custom shader: no skinning inputs */
static const gchar plain_vertex_source[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "uniform mat4 mvp;\n"
    "void main()\n"
    "{\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp*vec4(vertexPosition, 1.0);\n"
    "}\n";

/* raylib-convention skinning shader with %d bone matrices */
static const gchar skin_vertex_format[] =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec4 vertexColor;\n"
    "in vec4 vertexBoneIndices;\n"
    "in vec4 vertexBoneWeights;\n"
    "out vec2 fragTexCoord;\n"
    "out vec4 fragColor;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 boneMatrices[%d];\n"
    "void main()\n"
    "{\n"
    "    mat4 skin = vertexBoneWeights.x*boneMatrices[int(vertexBoneIndices.x)]\n"
    "              + vertexBoneWeights.y*boneMatrices[int(vertexBoneIndices.y)]\n"
    "              + vertexBoneWeights.z*boneMatrices[int(vertexBoneIndices.z)]\n"
    "              + vertexBoneWeights.w*boneMatrices[int(vertexBoneIndices.w)];\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = mvp*(skin*vec4(vertexPosition, 1.0));\n"
    "}\n";

static const gchar plain_fragment_source[] =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "void main()\n"
    "{\n"
    "    finalColor = texture(texture0, fragTexCoord)*colDiffuse*fragColor;\n"
    "}\n";

typedef struct
{
    gchar             *path;
    LrgAssetManager   *manager;
    GrlModel          *model;
    LrgModelAnimator  *animator;
    RenderTexture2D    target;
} SkinFixture;

static gboolean
skin_fixture_init (SkinFixture *fx)
{
    static const gchar *const clips[] = { "Curl", "Sway" };
    g_autoptr(GError)         error = NULL;
    GPtrArray                *animations;

    memset (fx, 0, sizeof *fx);
    fx->path = g_build_filename (g_get_tmp_dir (), "lrg-animator-tube.glb", NULL);
    test_glb_write_skinned_tube (fx->path, 24, 12, 8, clips, 2);

    fx->manager = lrg_asset_manager_new ();
    fx->model = lrg_asset_manager_load_model (fx->manager, fx->path, &error);
    g_assert_no_error (error);
    animations = lrg_asset_manager_load_model_animations (fx->manager, fx->path, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (animations->len, ==, 2);
    g_assert_cmpint (grl_model_get_bone_count (fx->model), ==, 8);
    fx->animator = lrg_model_animator_new (fx->model, animations);
    g_assert_cmpint (lrg_model_animator_get_clip_frame_count (fx->animator, 0), ==, 61);

    fx->target = LoadRenderTexture (SKIN_IMAGE_SIZE, SKIN_IMAGE_SIZE);
    return fx->target.id != 0;
}

static void
skin_fixture_clear (SkinFixture *fx)
{
    if (fx->target.id != 0)
        UnloadRenderTexture (fx->target);
    g_clear_object (&fx->animator);
    g_clear_object (&fx->manager);
    if (fx->path != NULL)
        g_remove (fx->path);
    g_clear_pointer (&fx->path, g_free);
}

/* skin_render:
 * Poses @state (always, the key cache is dropped first) unless @state is
 * NULL, draws two tubes and reads the pixels back. */
static Image
skin_render (SkinFixture             *fx,
             const LrgModelAnimState *state)
{
    GrlColor tint = { 255, 220, 200, 255 };
    Camera3D camera;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3) { 1.6f, 1.6f, 3.4f };
    camera.target = (Vector3) { 0.0f, 1.0f, 0.0f };
    camera.up = (Vector3) { 0, 1, 0 };
    camera.fovy = 45;
    camera.projection = CAMERA_PERSPECTIVE;

    lrg_model_animator_invalidate (fx->animator);
    if (state != NULL)
        g_assert_true (lrg_model_animator_apply (fx->animator, state));

    BeginTextureMode (fx->target);
    ClearBackground (BLACK);
    BeginMode3D (camera);
    lrg_model_animator_draw_masked_ex (fx->animator, NULL, -0.5f, 0, 0, 0, 1, NULL);
    lrg_model_animator_draw_masked_ex (fx->animator, NULL, 0.5f, 0, 0, 0.8f, 1, &tint);
    EndMode3D ();
    EndTextureMode ();
    return LoadImageFromTexture (fx->target.texture);
}

/* image_diff:
 * Counts the pixels covered (not black) in either image and those whose
 * colour differs by more than a rounding step in any channel. */
static void
image_diff (Image  a,
            Image  b,
            guint *covered,
            guint *differing)
{
    Color *pa = LoadImageColors (a);
    Color *pb = LoadImageColors (b);
    gint   i;

    *covered = 0;
    *differing = 0;
    for (i = 0; i < a.width * a.height; i++)
    {
        gint dr = ABS ((gint)pa[i].r - (gint)pb[i].r);
        gint dg = ABS ((gint)pa[i].g - (gint)pb[i].g);
        gint db = ABS ((gint)pa[i].b - (gint)pb[i].b);

        if (pa[i].r + pa[i].g + pa[i].b > 0 || pb[i].r + pb[i].g + pb[i].b > 0)
            (*covered)++;
        if (MAX (dr, MAX (dg, db)) > 4)
            (*differing)++;
    }
    UnloadImageColors (pa);
    UnloadImageColors (pb);
}

/* assert_same_picture:
 * GPU and CPU skinning round differently, which can flip a pixel on a
 * triangle edge; anything beyond a thin sliver of edge pixels means
 * misplaced geometry. */
static void
assert_same_picture (Image        a,
                     Image        b,
                     const gchar *what)
{
    guint covered;
    guint differing;

    image_diff (a, b, &covered, &differing);
    g_test_message ("%s: %u covered, %u differing pixels", what, covered, differing);
    g_assert_cmpuint (covered, >, SKIN_IMAGE_SIZE * SKIN_IMAGE_SIZE / 25);
    g_assert_cmpuint (differing * 100, <=, covered);
}

static gboolean
gl_can_skin (void)
{
    return rlGetVersion () == RL_OPENGL_33 || rlGetVersion () == RL_OPENGL_43;
}

static void
test_gpu_matches_cpu (void)
{
    SkinFixture       fx;
    LrgModelAnimState states[3];
    Image             rest;
    guint             i;

    if (!graphics_available)
    {
        g_test_skip ("Graphics context not available");
        return;
    }
    if (!skin_fixture_init (&fx))
    {
        skin_fixture_clear (&fx);
        g_test_skip ("Render textures not available");
        return;
    }

    /* a single-clip frame, another clip, and a half-way crossfade */
    lrg_model_anim_state_reset (&states[0]);
    lrg_model_animator_play (fx.animator, &states[0], 0, TRUE, 0);
    lrg_model_animator_advance (fx.animator, &states[0], 0.5);
    lrg_model_anim_state_reset (&states[1]);
    lrg_model_animator_play (fx.animator, &states[1], 1, TRUE, 0);
    lrg_model_animator_advance (fx.animator, &states[1], 0.35);
    states[2] = states[0];
    lrg_model_animator_play (fx.animator, &states[2], 1, TRUE, 1.0);
    lrg_model_animator_advance (fx.animator, &states[2], 0.5);
    g_assert_cmpint (states[2].previous_clip, ==, 0);

    rest = skin_render (&fx, NULL);
    for (i = 0; i < G_N_ELEMENTS (states); i++)
    {
        Image    gpu;
        Image    cpu;
        Image    again;
        guint    covered;
        guint    differing;
        gchar    what[32];

        lrg_model_animator_set_gpu_skinning (fx.animator, TRUE);
        gpu = skin_render (&fx, &states[i]);
        if (gl_can_skin ())
            g_assert_true (lrg_model_animator_uses_gpu_skinning (fx.animator));

        lrg_model_animator_set_gpu_skinning (fx.animator, FALSE);
        g_assert_false (lrg_model_animator_get_gpu_skinning (fx.animator));
        cpu = skin_render (&fx, &states[i]);
        g_assert_false (lrg_model_animator_uses_gpu_skinning (fx.animator));

        /* back to the GPU after the CPU wrote a pose into the buffers */
        lrg_model_animator_set_gpu_skinning (fx.animator, TRUE);
        again = skin_render (&fx, &states[i]);

        g_snprintf (what, sizeof what, "pose %u gpu/cpu", i);
        assert_same_picture (gpu, cpu, what);
        g_snprintf (what, sizeof what, "pose %u gpu/gpu", i);
        assert_same_picture (gpu, again, what);

        /* the pose really moves the tubes away from the rest pose */
        image_diff (cpu, rest, &covered, &differing);
        g_assert_cmpuint (differing * 10, >, covered);

        UnloadImage (gpu);
        UnloadImage (cpu);
        UnloadImage (again);
    }
    UnloadImage (rest);
    skin_fixture_clear (&fx);
}

/* set_shader: puts @shader on every material of @model, returning the
 * previous shader of material 0 */
static Shader
set_shader (GrlModel *model,
            Shader    shader)
{
    Model *raw = grl_model_get_handle (model);
    Shader previous = raw->materials[0].shader;
    gint   i;

    for (i = 0; i < raw->materialCount; i++)
        raw->materials[i].shader = shader;
    return previous;
}

static void
test_shader_selection (void)
{
    SkinFixture       fx;
    LrgModelAnimState state;
    g_autofree gchar *small_source = NULL;
    g_autofree gchar *large_source = NULL;
    Shader            plain;
    Shader            small;
    Shader            large;
    Shader            original;
    Image             reference;
    Image             custom;

    if (!graphics_available || !gl_can_skin ())
    {
        g_test_skip ("OpenGL 3.3 context not available");
        return;
    }
    if (!skin_fixture_init (&fx))
    {
        skin_fixture_clear (&fx);
        g_test_skip ("Render textures not available");
        return;
    }

    lrg_model_anim_state_reset (&state);
    lrg_model_animator_play (fx.animator, &state, 1, TRUE, 0);
    lrg_model_animator_advance (fx.animator, &state, 0.4);

    /* the CPU reference, drawn with raylib's default shader */
    lrg_model_animator_set_gpu_skinning (fx.animator, FALSE);
    reference = skin_render (&fx, &state);
    lrg_model_animator_set_gpu_skinning (fx.animator, TRUE);

    small_source = g_strdup_printf (skin_vertex_format, 4);
    large_source = g_strdup_printf (skin_vertex_format, 16);
    plain = LoadShaderFromMemory (plain_vertex_source, plain_fragment_source);
    small = LoadShaderFromMemory (small_source, plain_fragment_source);
    large = LoadShaderFromMemory (large_source, plain_fragment_source);
    g_assert_cmpuint (plain.id, !=, rlGetShaderIdDefault ());
    g_assert_cmpuint (small.id, !=, rlGetShaderIdDefault ());
    g_assert_cmpuint (large.id, !=, rlGetShaderIdDefault ());

    /* a custom shader without skinning inputs: CPU */
    original = set_shader (fx.model, plain);
    custom = skin_render (&fx, &state);
    g_assert_false (lrg_model_animator_uses_gpu_skinning (fx.animator));
    assert_same_picture (reference, custom, "plain shader");
    UnloadImage (custom);

    /* a skinning shader whose array holds 4 of the 8 bones: CPU */
    set_shader (fx.model, small);
    lrg_model_animator_invalidate (fx.animator);
    g_assert_true (lrg_model_animator_apply (fx.animator, &state));
    g_assert_false (lrg_model_animator_uses_gpu_skinning (fx.animator));

    /* a skinning shader with room for every bone: GPU, same picture */
    set_shader (fx.model, large);
    custom = skin_render (&fx, &state);
    g_assert_true (lrg_model_animator_uses_gpu_skinning (fx.animator));
    assert_same_picture (reference, custom, "skinning shader");
    UnloadImage (custom);

    set_shader (fx.model, original);
    UnloadShader (plain);
    UnloadShader (small);
    UnloadShader (large);
    UnloadImage (reference);
    skin_fixture_clear (&fx);
}

int
main (int   argc,
      char *argv[])
{
    int result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

    g_test_add_func ("/model-animator/state/boxed", test_state_boxed);
    g_test_add_func ("/model-animator/clips/table", test_clip_table);
    g_test_add_func ("/model-animator/clips/find", test_find_clip);
    g_test_add_func ("/model-animator/clips/names-from-info", test_names_from_info);
    g_test_add_func ("/model-animator/play/loop-wrap", test_loop_wrap);
    g_test_add_func ("/model-animator/play/clamp-one-shot", test_clamp_one_shot);
    g_test_add_func ("/model-animator/play/crossfade", test_crossfade);
    g_test_add_func ("/model-animator/apply/dedupe", test_apply_dedupe);
    g_test_add_func ("/model-animator/apply/crossfade-quantized", test_apply_crossfade_quantized);
    g_test_add_func ("/model-animator/apply/non-finite", test_apply_non_finite);
    g_test_add_func ("/model-animator/headless-mask-and-draw", test_headless_mask_and_draw);
    g_test_add_func ("/model-animator/gl/real-model", test_real_model);
    g_test_add_func ("/model-animator/gl/gpu-matches-cpu", test_gpu_matches_cpu);
    g_test_add_func ("/model-animator/gl/shader-selection", test_shader_selection);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
