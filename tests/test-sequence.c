/* test-sequence.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the `.rseq` sequence asset (LrgSequence, LrgSequenceCurve,
 * LrgSequenceTrack), LrgSequencePlayer on a small LrgWorld3D,
 * LrgSequenceComponent, and the 0.9.0 level timeline additions (Bézier
 * keys, bone channels, timeline -> sequence conversion). Headless.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-4f)

/* ==========================================================================
 * Bézier curve evaluation vs reference values
 * ========================================================================== */

/* Two auto keys: both ends flat -> smoothstep 3s^2 - 2s^3. */
static void
test_curve_auto_two_keys (void)
{
	g_autoptr(LrgSequenceCurve) c = lrg_sequence_curve_new ();

	lrg_sequence_curve_set_key (c, 0.0f, 0.0f, LRG_SEQUENCE_TANGENT_AUTO);
	lrg_sequence_curve_set_key (c, 1.0f, 1.0f, LRG_SEQUENCE_TANGENT_AUTO);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_evaluate (c, 0.25f), 0.15625f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_evaluate (c, 0.5f), 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_evaluate (c, 0.75f), 0.84375f, EPS);
	/* Clamped outside the keys */
	g_assert_cmpfloat (lrg_sequence_curve_evaluate (c, -1.0f), ==, 0.0f);
	g_assert_cmpfloat (lrg_sequence_curve_evaluate (c, 9.0f), ==, 1.0f);
}

/* Auto with three monotone keys: middle slope = Catmull-Rom (2 - 0) / 2
 * = 1, below the 3 * min secant limit. */
static void
test_curve_auto_three_keys (void)
{
	g_autoptr(LrgSequenceCurve) c = lrg_sequence_curve_new ();
	gfloat                      in = 0.0f, out = 0.0f, expect;

	lrg_sequence_curve_set_key (c, 0.0f, 0.0f, LRG_SEQUENCE_TANGENT_AUTO);
	lrg_sequence_curve_set_key (c, 1.0f, 1.0f, LRG_SEQUENCE_TANGENT_AUTO);
	lrg_sequence_curve_set_key (c, 2.0f, 2.0f, LRG_SEQUENCE_TANGENT_AUTO);
	g_assert_true (lrg_sequence_curve_get_slopes (c, 1, &in, &out));
	g_assert_cmpfloat_with_epsilon (in, 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (out, 1.0f, EPS);

	/* Segment 0..1 with m0 = 0, m1 = 1 at s = 0.5:
	 * h01 = 0.5, h11 = 0.125 - 0.25 = -0.125 -> 0.5 - 0.125 = 0.375 */
	expect = 0.375f;
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_evaluate (c, 0.5f), expect, EPS);

	/* A local extremum is flat (no overshoot). */
	lrg_sequence_curve_set_key (c, 2.0f, 0.0f, LRG_SEQUENCE_TANGENT_AUTO);
	g_assert_true (lrg_sequence_curve_get_slopes (c, 1, &in, &out));
	g_assert_cmpfloat (in, ==, 0.0f);
	g_assert_cmpfloat (out, ==, 0.0f);

	/* Fritsch-Carlson limit: secants 10 and 0.1 -> CR slope 5.05 is
	 * clamped to 3 * 0.1 = 0.3. */
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_auto_slope (TRUE, 0.0f, 0.0f, 1.0f, 10.0f, TRUE, 2.0f, 10.1f),
	                                0.3f, EPS);
}

static void
test_curve_linear_constant (void)
{
	g_autoptr(LrgSequenceCurve) c = lrg_sequence_curve_new ();

	lrg_sequence_curve_set_key (c, 0.0f, 0.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_curve_set_key (c, 2.0f, 4.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_evaluate (c, 0.5f), 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_evaluate (c, 1.5f), 3.0f, EPS);

	/* Constant: hold until the next key */
	lrg_sequence_curve_set_key (c, 0.0f, 0.0f, LRG_SEQUENCE_TANGENT_CONSTANT);
	g_assert_cmpfloat (lrg_sequence_curve_evaluate (c, 1.999f), ==, 0.0f);
	g_assert_cmpfloat (lrg_sequence_curve_evaluate (c, 2.0f), ==, 4.0f);
}

static void
test_curve_user_broken (void)
{
	g_autoptr(LrgSequenceCurve) c = lrg_sequence_curve_new ();
	LrgSequenceKey              k;
	guint                       i;

	/* key0 slope 2 (user), key1 slope 0 (user), dt = 1, s = 0.5:
	 * h10 * 2 + h01 * 1 = 0.125 * 2 + 0.5 = 0.75 */
	i = lrg_sequence_curve_set_key (c, 0.0f, 0.0f, LRG_SEQUENCE_TANGENT_USER);
	lrg_sequence_curve_set_key_tangents (c, i, LRG_SEQUENCE_TANGENT_USER, 2.0f, 99.0f);
	lrg_sequence_curve_set_key (c, 1.0f, 1.0f, LRG_SEQUENCE_TANGENT_USER);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_evaluate (c, 0.5f), 0.75f, EPS);
	g_assert_true (lrg_sequence_curve_get_key (c, 0, &k));
	g_assert_cmpfloat (k.out_tangent, ==, 2.0f);   /* user: out follows in */

	/* Broken middle key at t=1: in -1, out 5. Segment 1..2 (v 1 -> 1)
	 * m0 = 5, m1 = 0 (user 0 at key 2), s = 0.5: 1 + 0.125 * 5 = 1.625.
	 * Segment 0..1: m0 = 2, m1 = -1: 0.25 + 0.5 + (-0.125)(-1) = 0.875 */
	lrg_sequence_curve_set_key_tangents (c, 1, LRG_SEQUENCE_TANGENT_BROKEN, -1.0f, 5.0f);
	i = lrg_sequence_curve_set_key (c, 2.0f, 1.0f, LRG_SEQUENCE_TANGENT_USER);
	g_assert_cmpuint (i, ==, 2);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_evaluate (c, 1.5f), 1.625f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_curve_evaluate (c, 0.5f), 0.875f, EPS);

	/* Re-keying keeps handles; moving keeps them too */
	lrg_sequence_curve_set_key (c, 1.0f, 1.0f, LRG_SEQUENCE_TANGENT_BROKEN);
	g_assert_true (lrg_sequence_curve_get_key (c, 1, &k));
	g_assert_cmpfloat (k.in_tangent, ==, -1.0f);
	g_assert_cmpfloat (k.out_tangent, ==, 5.0f);
	g_assert_cmpint (lrg_sequence_curve_move_key (c, 1, 1.5f), ==, 1);
	g_assert_true (lrg_sequence_curve_get_key (c, 1, &k));
	g_assert_cmpfloat (k.time, ==, 1.5f);
	g_assert_cmpfloat (k.out_tangent, ==, 5.0f);
	g_assert_cmpint (lrg_sequence_curve_find_key (c, 1.5f, 0.01f), ==, 1);
}

/* ==========================================================================
 * Format round trip
 * ========================================================================== */

static LrgSequence *
build_full_sequence (void)
{
	LrgSequence                *seq = lrg_sequence_new ();
	g_autoptr(LrgSequenceTrack) tr = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_TRANSFORM, "Hero move");
	g_autoptr(LrgSequenceTrack) bone = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_TRANSFORM, "Hero arm");
	g_autoptr(LrgSequenceTrack) prop = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_PROPERTY, "Zoom");
	g_autoptr(LrgSequenceTrack) cut = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_CAMERA_CUT, "Cuts");
	g_autoptr(LrgSequenceTrack) aud = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_AUDIO, "Music");
	g_autoptr(LrgSequenceTrack) ev = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_EVENT, "Cues");
	g_autoptr(LrgSequenceTrack) anim = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_ANIMATION, "Hero anim");
	g_autoptr(LrgSequenceTrack) sub = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_SUBSEQUENCE, "Shot B");
	LrgSequenceCurve           *c;
	guint                       i;

	lrg_sequence_set_name (seq, "intro");
	lrg_sequence_set_length (seq, 8.0f);
	lrg_sequence_set_fps (seq, 24.0f);
	lrg_sequence_set_play_mode (seq, LRG_SEQUENCE_PLAY_PING_PONG);
	lrg_sequence_add_binding (seq, "hero-guid", "Hero");
	lrg_sequence_add_binding (seq, "cam-a", "Camera A");

	lrg_sequence_track_set_binding (tr, "hero-guid");
	c = lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_LOCATION_X);
	lrg_sequence_curve_set_key (c, 0.0f, 0.1f, LRG_SEQUENCE_TANGENT_AUTO);
	i = lrg_sequence_curve_set_key (c, 1.0f / 3.0f, 2.5f, LRG_SEQUENCE_TANGENT_BROKEN);
	lrg_sequence_curve_set_key_tangents (c, i, LRG_SEQUENCE_TANGENT_BROKEN, -0.3f, 1.7f);
	lrg_sequence_curve_set_key (c, 2.0f, 3.0f, LRG_SEQUENCE_TANGENT_CONSTANT);
	c = lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_ROTATION_Y);
	i = lrg_sequence_curve_set_key (c, 0.5f, 1.25f, LRG_SEQUENCE_TANGENT_USER);
	lrg_sequence_curve_set_key_tangents (c, i, LRG_SEQUENCE_TANGENT_USER, 0.75f, 0.0f);
	lrg_sequence_curve_set_key (c, 4.0f, -1.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_add_track (seq, tr);

	lrg_sequence_track_set_binding (bone, "hero-guid");
	lrg_sequence_track_set_bone (bone, "upper.arm.L");
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (bone, LRG_SEQUENCE_CHANNEL_SCALE_Z),
	                            1.0f, 1.5f, LRG_SEQUENCE_TANGENT_AUTO);
	lrg_sequence_track_set_muted (bone, TRUE);
	lrg_sequence_add_track (seq, bone);

	lrg_sequence_track_set_binding (prop, "cam-a");
	lrg_sequence_track_set_component_type (prop, "LrgCameraComponent");
	lrg_sequence_track_set_property_name (prop, "fov");
	lrg_sequence_curve_set_key (lrg_sequence_track_get_curve (prop), 0.0f, 60.0f, LRG_SEQUENCE_TANGENT_AUTO);
	lrg_sequence_add_track (seq, prop);

	lrg_sequence_track_add_camera_cut (cut, 0.0f, "cam-a");
	lrg_sequence_track_add_camera_cut (cut, 4.0f, "cam-b");
	lrg_sequence_add_track (seq, cut);

	lrg_sequence_track_set_binding (aud, "hero-guid");
	lrg_sequence_track_add_audio (aud, 0.5f, "sfx:whoosh", 0.8f, 0.0f);
	lrg_sequence_track_add_audio (aud, 3.0f, "music/theme.ogg", 1.0f, 2.0f);
	lrg_sequence_add_track (seq, aud);

	lrg_sequence_track_add_event (ev, 1.0f, "door-open", "slow");
	lrg_sequence_track_add_event (ev, 2.0f, "door-close", NULL);
	lrg_sequence_add_track (seq, ev);

	lrg_sequence_track_set_binding (anim, "hero-guid");
	lrg_sequence_track_add_clip (anim, 0.0f, 2.0f, "walk", 0.25f, 1.5f, TRUE, 0.5f);
	lrg_sequence_track_add_clip (anim, 2.0f, 0.0f, "idle", 0.0f, 1.0f, FALSE, 1.0f);
	lrg_sequence_add_track (seq, anim);

	lrg_sequence_track_add_subsequence (sub, 5.0f, 2.0f, "shots/b.rseq", 0.5f, 2.0f, FALSE);
	lrg_sequence_add_track (seq, sub);
	return seq;
}

static void
test_format_round_trip (void)
{
	g_autoptr(LrgSequence) seq = build_full_sequence ();
	g_autoptr(LrgSequence) back = NULL;
	g_autoptr(LrgSequence) copy = NULL;
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *text = NULL;
	g_autofree gchar      *text2 = NULL;
	LrgSequenceKey         k;
	LrgSequenceCurve      *c;

	text = lrg_sequence_to_data (seq, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (text, "kind: camera-cut"));
	g_assert_nonnull (strstr (text, "tangent: broken"));
	g_assert_nonnull (strstr (text, "play_mode: ping-pong"));

	back = lrg_sequence_new_from_data (text, -1, "/tmp", &error);
	g_assert_no_error (error);
	g_assert_true (lrg_sequence_equal (seq, back));
	g_assert_cmpstr (lrg_sequence_get_base_dir (back), ==, "/tmp");

	/* Byte stable */
	text2 = lrg_sequence_to_data (back, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, text2);

	/* Floats come back exactly */
	c = lrg_sequence_track_get_channel_curve (lrg_sequence_get_track (back, 0), LRG_SEQUENCE_CHANNEL_LOCATION_X);
	g_assert_true (lrg_sequence_curve_get_key (c, 1, &k));
	g_assert_cmpfloat (k.time, ==, 1.0f / 3.0f);
	g_assert_cmpfloat (k.out_tangent, ==, 1.7f);
	g_assert_cmpstr (lrg_sequence_track_get_bone (lrg_sequence_get_track (back, 1)), ==, "upper.arm.L");
	g_assert_cmpstr (lrg_sequence_get_binding_name (back, "cam-a"), ==, "Camera A");

	copy = lrg_sequence_copy (back);
	g_assert_true (lrg_sequence_equal (copy, seq));
	lrg_sequence_track_set_muted (lrg_sequence_get_track (copy, 0), TRUE);
	g_assert_false (lrg_sequence_equal (copy, seq));
}

static void
test_format_errors (void)
{
	g_autoptr(GError)      error = NULL;
	g_autoptr(LrgSequence) s = NULL;

	s = lrg_sequence_new_from_data ("type: sequence\nversion: 99\n", -1, NULL, &error);
	g_assert_null (s);
	g_assert_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_VERSION);
	g_clear_error (&error);

	s = lrg_sequence_new_from_data ("type: level\n", -1, NULL, &error);
	g_assert_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_INVALID);
	g_clear_error (&error);

	s = lrg_sequence_new_from_data ("tracks:\n  - kind: banana\n", -1, NULL, &error);
	g_assert_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_INVALID);
	g_clear_error (&error);

	s = lrg_sequence_new_from_file ("/nonexistent/x.rseq", &error);
	g_assert_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_IO);
}

static void
test_file_round_trip (void)
{
	g_autoptr(LrgSequence) seq = build_full_sequence ();
	g_autoptr(LrgSequence) back = NULL;
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *dir = g_dir_make_tmp ("lrg-seq-XXXXXX", NULL);
	g_autofree gchar      *path = g_build_filename (dir, "intro.rseq", NULL);

	g_assert_true (lrg_sequence_save_to_file (seq, path, &error));
	g_assert_no_error (error);
	back = lrg_sequence_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_sequence_equal (seq, back));
	g_assert_cmpstr (lrg_sequence_get_base_dir (back), ==, dir);
	g_unlink (path);
	g_rmdir (dir);
}

/* ==========================================================================
 * Playback on a world
 * ========================================================================== */

typedef struct
{
	LrgWorld3D  *world;
	LrgEntity3D *hero;
	LrgEntity3D *cam_a;
	LrgEntity3D *cam_b;
	GPtrArray   *log;     /* "name@time" or "name@time<" (reverse) */
} Fixture;

static LrgEntity3D *
add_entity (LrgWorld3D  *world,
            const gchar *name,
            const gchar *guid)
{
	LrgEntity3D *e = lrg_entity3d_new_with_name (name);

	lrg_entity3d_set_guid (e, guid);
	lrg_world3d_add_entity (world, e);
	g_object_unref (e);
	return e;
}

static void
add_camera (LrgEntity3D *e)
{
	g_autoptr(LrgCameraComponent) cc = lrg_camera_component_new ();

	lrg_entity3d_add_component (e, LRG_COMPONENT (cc));
}

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	(void) data;
	f->world = lrg_world3d_new ();
	f->hero = add_entity (f->world, "Hero", "hero-guid");
	f->cam_a = add_entity (f->world, "CamA", "cam-a");
	f->cam_b = add_entity (f->world, "CamB", "cam-b");
	add_camera (f->cam_a);
	add_camera (f->cam_b);
	f->log = g_ptr_array_new_with_free_func (g_free);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	(void) data;
	g_clear_object (&f->world);
	g_ptr_array_unref (f->log);
}

static void
on_event (LrgSequencePlayer *player,
          LrgSequenceTrack  *track,
          const gchar       *name,
          const gchar       *payload,
          gfloat             time,
          gboolean           reverse,
          gpointer           user_data)
{
	Fixture *f = user_data;

	(void) player;
	(void) track;
	(void) payload;
	g_ptr_array_add (f->log, g_strdup_printf ("%s@%g%s", name, (gdouble) time, reverse ? "<" : ""));
}

/* Joins the log and clears it. */
static gchar *
take_log (Fixture *f)
{
	gchar *s;

	g_ptr_array_add (f->log, NULL);
	s = g_strjoinv (",", (gchar **) f->log->pdata);
	g_ptr_array_set_size (f->log, 0);
	return s;
}

static LrgSequence *
event_sequence (void)
{
	LrgSequence                *seq = lrg_sequence_new ();
	g_autoptr(LrgSequenceTrack) ev = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_EVENT, "Cues");

	lrg_sequence_set_length (seq, 4.0f);
	lrg_sequence_track_add_event (ev, 0.0f, "start", NULL);
	lrg_sequence_track_add_event (ev, 1.0f, "a", "pa");
	lrg_sequence_track_add_event (ev, 2.0f, "b", NULL);
	lrg_sequence_track_add_event (ev, 4.0f, "end", NULL);
	lrg_sequence_add_track (seq, ev);
	return seq;
}

static void
test_transform_track (Fixture       *f,
                      gconstpointer  data)
{
	g_autoptr(LrgSequence)       seq = lrg_sequence_new ();
	g_autoptr(LrgSequenceTrack)  tr = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_TRANSFORM, NULL);
	g_autoptr(LrgSequencePlayer) p = NULL;
	gfloat                       pos[3], rot[3], scl[3];

	(void) data;
	lrg_entity3d_set_position_xyz (f->hero, 7.0f, 8.0f, 9.0f);
	lrg_sequence_track_set_binding (tr, "hero-guid");
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_LOCATION_X),
	                            0.0f, 0.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_LOCATION_X),
	                            2.0f, 4.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_ROTATION_Y),
	                            0.0f, 0.5f, LRG_SEQUENCE_TANGENT_AUTO);
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_SCALE_Z),
	                            0.0f, 2.0f, LRG_SEQUENCE_TANGENT_AUTO);
	lrg_sequence_add_track (seq, tr);

	p = lrg_sequence_player_new (seq);
	lrg_sequence_player_set_world (p, f->world);
	lrg_sequence_player_set_time (p, 0.5f);

	lrg_entity3d_get_position (f->hero, pos);
	lrg_entity3d_get_rotation_euler (f->hero, rot);
	lrg_entity3d_get_scale (f->hero, scl);
	g_assert_cmpfloat_with_epsilon (pos[0], 1.0f, EPS);
	g_assert_cmpfloat (pos[1], ==, 8.0f);   /* unkeyed channels untouched */
	g_assert_cmpfloat (pos[2], ==, 9.0f);
	g_assert_cmpfloat_with_epsilon (rot[1], 0.5f, EPS);
	g_assert_cmpfloat_with_epsilon (scl[2], 2.0f, EPS);
	g_assert_cmpfloat_with_epsilon (scl[0], 1.0f, EPS);

	/* Muted tracks do nothing */
	lrg_sequence_track_set_muted (tr, TRUE);
	lrg_sequence_player_set_time (p, 1.5f);
	lrg_entity3d_get_position (f->hero, pos);
	g_assert_cmpfloat_with_epsilon (pos[0], 1.0f, EPS);

	/* Binding override */
	lrg_sequence_track_set_muted (tr, FALSE);
	lrg_sequence_player_set_binding_override (p, "hero-guid", f->cam_b);
	lrg_sequence_player_set_time (p, 1.0f);
	lrg_entity3d_get_position (f->cam_b, pos);
	g_assert_cmpfloat_with_epsilon (pos[0], 2.0f, EPS);
	g_assert_true (lrg_sequence_player_resolve_binding (p, "hero-guid") == f->cam_b);
}

static void
test_property_track (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(LrgSequence)       seq = lrg_sequence_new ();
	g_autoptr(LrgSequenceTrack)  tr = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_PROPERTY, NULL);
	g_autoptr(LrgSequenceTrack)  pr = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_PROPERTY, NULL);
	g_autoptr(LrgSequencePlayer) p = NULL;
	LrgCameraComponent          *cc;

	(void) data;
	lrg_sequence_track_set_binding (tr, "cam-a");
	lrg_sequence_track_set_component_type (tr, "LrgCameraComponent");
	lrg_sequence_track_set_property_name (tr, "fov");
	lrg_sequence_curve_set_key (lrg_sequence_track_get_curve (tr), 0.0f, 30.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_curve_set_key (lrg_sequence_track_get_curve (tr), 1.0f, 90.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_add_track (seq, tr);

	/* An int property (priority) is rounded */
	lrg_sequence_track_set_binding (pr, "cam-a");
	lrg_sequence_track_set_component_type (pr, "LrgCameraComponent");
	lrg_sequence_track_set_property_name (pr, "priority");
	lrg_sequence_curve_set_key (lrg_sequence_track_get_curve (pr), 0.0f, 0.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_curve_set_key (lrg_sequence_track_get_curve (pr), 1.0f, 10.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_add_track (seq, pr);

	p = lrg_sequence_player_new (seq);
	lrg_sequence_player_set_world (p, f->world);
	lrg_sequence_player_set_time (p, 0.25f);
	cc = LRG_CAMERA_COMPONENT (lrg_entity3d_get_component (f->cam_a, LRG_TYPE_CAMERA_COMPONENT));
	g_assert_cmpfloat_with_epsilon (lrg_camera_component_get_fov (cc), 45.0f, 1e-3f);
	g_assert_cmpint (lrg_camera_component_get_priority (cc), ==, 3);   /* 2.5 rounds away from zero */
}

static void
on_cut (LrgSequencePlayer *player,
        LrgEntity3D       *camera,
        gpointer           user_data)
{
	Fixture *f = user_data;

	(void) player;
	g_ptr_array_add (f->log, g_strdup (camera != NULL ? lrg_entity3d_get_name (camera) : "none"));
}

static void
test_camera_cut (Fixture       *f,
                 gconstpointer  data)
{
	g_autoptr(LrgSequence)       seq = lrg_sequence_new ();
	g_autoptr(LrgSequenceTrack)  cut = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_CAMERA_CUT, NULL);
	g_autoptr(LrgSequencePlayer) p = NULL;
	g_autofree gchar            *log = NULL;
	LrgCameraComponent          *ca, *cb;

	(void) data;
	ca = LRG_CAMERA_COMPONENT (lrg_entity3d_get_component (f->cam_a, LRG_TYPE_CAMERA_COMPONENT));
	cb = LRG_CAMERA_COMPONENT (lrg_entity3d_get_component (f->cam_b, LRG_TYPE_CAMERA_COMPONENT));
	lrg_sequence_set_length (seq, 4.0f);
	lrg_sequence_track_add_camera_cut (cut, 1.0f, "cam-a");
	lrg_sequence_track_add_camera_cut (cut, 2.0f, "cam-b");
	lrg_sequence_add_track (seq, cut);

	p = lrg_sequence_player_new (seq);
	lrg_sequence_player_set_world (p, f->world);
	g_signal_connect (p, "camera-cut", G_CALLBACK (on_cut), f);

	/* Before the first cut the world's own choice stands */
	lrg_sequence_player_set_time (p, 0.5f);
	g_assert_null (lrg_sequence_player_get_camera_binding (p));

	lrg_sequence_player_play (p);
	lrg_sequence_player_advance (p, 0.75f);   /* 1.25 */
	g_assert_true (lrg_world3d_get_active_camera (f->world) == LRG_CAMERA (lrg_camera_component_get_camera (ca)));
	lrg_sequence_player_advance (p, 1.0f);    /* 2.25 */
	g_assert_true (lrg_world3d_get_active_camera (f->world) == LRG_CAMERA (lrg_camera_component_get_camera (cb)));
	g_assert_cmpstr (lrg_sequence_player_get_camera_binding (p), ==, "cam-b");

	/* Scrubbing back before the first cut hands the camera back */
	lrg_sequence_player_scrub (p, 0.1f);
	g_assert_null (lrg_sequence_player_get_camera_binding (p));
	lrg_sequence_player_scrub (p, 3.0f);
	lrg_sequence_player_stop (p);
	g_assert_null (lrg_sequence_player_get_camera_binding (p));

	log = take_log (f);
	g_assert_cmpstr (log, ==, "CamA,CamB,none,CamB,none");
}

static void
count_signal (gpointer instance,
              gpointer user_data)
{
	(void) instance;
	(*(guint *) user_data)++;
}

static void
test_events_forward_backward (Fixture       *f,
                              gconstpointer  data)
{
	g_autoptr(LrgSequence)       seq = event_sequence ();
	g_autoptr(LrgSequencePlayer) p = lrg_sequence_player_new (seq);
	g_autofree gchar            *l1 = NULL, *l2 = NULL, *l3 = NULL, *l4 = NULL;
	guint                        finished = 0;

	(void) data;
	lrg_sequence_player_set_world (p, f->world);
	g_signal_connect (p, "event", G_CALLBACK (on_event), f);
	g_signal_connect (p, "finished", G_CALLBACK (count_signal), &finished);

	/* Play from 0: the event at 0 fires, then a and b at the right time */
	lrg_sequence_player_play (p);
	lrg_sequence_player_advance (p, 0.5f);
	lrg_sequence_player_advance (p, 0.5f);   /* exactly 1.0: a fires */
	lrg_sequence_player_advance (p, 1.5f);   /* 2.5: b */
	l1 = take_log (f);
	g_assert_cmpstr (l1, ==, "start@0,a@1,b@2");

	/* Scrub backwards: reverse order, reverse flag; the point we stand on
	 * (2.5) is not an event; 1 and b are crossed again */
	lrg_sequence_player_pause (p);
	lrg_sequence_player_scrub (p, 0.5f);
	l2 = take_log (f);
	g_assert_cmpstr (l2, ==, "b@2<,a@1<");

	/* Forward again from 0.5 to exactly 2: a, b (b fires once at arrival) */
	lrg_sequence_player_scrub (p, 2.0f);
	lrg_sequence_player_scrub (p, 2.0f);
	l3 = take_log (f);
	g_assert_cmpstr (l3, ==, "a@1,b@2");

	/* Play to the end (once): end fires, player stops */
	lrg_sequence_player_play (p);
	lrg_sequence_player_advance (p, 5.0f);
	g_assert_false (lrg_sequence_player_is_playing (p));
	g_assert_cmpuint (finished, ==, 1);
	g_assert_cmpfloat (lrg_sequence_player_get_time (p), ==, 4.0f);
	l4 = take_log (f);
	g_assert_cmpstr (l4, ==, "end@4");
}

static void
test_loop_and_ping_pong (Fixture       *f,
                         gconstpointer  data)
{
	g_autoptr(LrgSequence)       seq = event_sequence ();
	g_autoptr(LrgSequencePlayer) p = lrg_sequence_player_new (seq);
	g_autofree gchar            *l1 = NULL, *l2 = NULL, *l3 = NULL;
	guint                        looped = 0;

	(void) data;
	g_signal_connect (p, "event", G_CALLBACK (on_event), f);
	g_signal_connect (p, "looped", G_CALLBACK (count_signal), &looped);

	/* Loop: 0 -> 3.5 -> wraps to 1.5 */
	lrg_sequence_player_set_play_mode (p, LRG_SEQUENCE_PLAY_LOOP);
	lrg_sequence_player_play (p);
	lrg_sequence_player_advance (p, 3.5f);
	lrg_sequence_player_advance (p, 2.0f);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_player_get_time (p), 1.5f, EPS);
	g_assert_cmpuint (looped, ==, 1);
	l1 = take_log (f);
	g_assert_cmpstr (l1, ==, "start@0,a@1,b@2,end@4,start@0,a@1");

	/* Ping-pong: 0 -> 3 -> (4, turn) -> 2.5 -> (0, turn) -> 0.5 */
	lrg_sequence_player_stop (p);
	looped = 0;
	lrg_sequence_player_set_play_mode (p, LRG_SEQUENCE_PLAY_PING_PONG);
	lrg_sequence_player_play (p);
	lrg_sequence_player_advance (p, 3.0f);
	l2 = take_log (f);
	g_assert_cmpstr (l2, ==, "start@0,a@1,b@2");
	lrg_sequence_player_advance (p, 2.5f);
	g_assert_cmpint (lrg_sequence_player_get_direction (p), ==, -1);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_player_get_time (p), 2.5f, EPS);
	lrg_sequence_player_advance (p, 3.0f);
	g_assert_cmpint (lrg_sequence_player_get_direction (p), ==, 1);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_player_get_time (p), 0.5f, EPS);
	g_assert_cmpuint (looped, ==, 2);
	l3 = take_log (f);
	g_assert_cmpstr (l3, ==, "end@4,b@2<,a@1<,start@0<");

	/* Negative rate plays backwards (once: finishes at 0) */
	lrg_sequence_player_stop (p);
	lrg_sequence_player_set_play_mode (p, LRG_SEQUENCE_PLAY_ONCE);
	lrg_sequence_player_set_rate (p, -2.0f);
	lrg_sequence_player_play (p);      /* at 0 going backwards: rewinds to the end */
	g_assert_cmpfloat (lrg_sequence_player_get_time (p), ==, 4.0f);
	lrg_sequence_player_advance (p, 1.25f);
	g_assert_cmpfloat_with_epsilon (lrg_sequence_player_get_time (p), 1.5f, EPS);
	g_ptr_array_set_size (f->log, 0);
}

/* Records audio cues. */
static void
on_audio (LrgSequencePlayer *player,
          LrgSequenceTrack  *track,
          LrgEntity3D       *entity,
          const gchar       *sound,
          gfloat             volume,
          gpointer           user_data)
{
	Fixture *f = user_data;

	(void) player;
	(void) track;
	g_ptr_array_add (f->log, g_strdup_printf ("%s:%s:%g", entity != NULL ? lrg_entity3d_get_name (entity) : "-",
	                                          sound, (gdouble) volume));
}

static void
test_audio_track (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(LrgSequence)       seq = lrg_sequence_new ();
	g_autoptr(LrgSequenceTrack)  aud = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_AUDIO, NULL);
	g_autoptr(LrgSequencePlayer) p = NULL;
	g_autofree gchar            *l1 = NULL, *l2 = NULL;

	(void) data;
	lrg_sequence_set_length (seq, 4.0f);
	lrg_sequence_track_set_binding (aud, "hero-guid");
	lrg_sequence_track_add_audio (aud, 1.0f, "sfx:step", 0.5f, 0.0f);
	lrg_sequence_track_add_audio (aud, 2.0f, "music/a.ogg", 1.0f, 1.0f);
	lrg_sequence_add_track (seq, aud);

	p = lrg_sequence_player_new (seq);
	lrg_sequence_player_set_world (p, f->world);
	g_signal_connect (p, "audio-cue", G_CALLBACK (on_audio), f);

	/* Scrubbing never starts sounds */
	lrg_sequence_player_scrub (p, 3.0f);
	l1 = take_log (f);
	g_assert_cmpstr (l1, ==, "");

	lrg_sequence_player_stop (p);
	lrg_sequence_player_play (p);
	lrg_sequence_player_advance (p, 2.5f);
	l2 = take_log (f);
	g_assert_cmpstr (l2, ==, "Hero:sfx:step:0.5,Hero:music/a.ogg:1");
}

/* Builds a one-bone skeleton and a 1 s clip moving the bone 0 -> 2 in X. */
static void
make_rig (LrgSkeleton      **out_skel,
          LrgAnimationClip **out_clip)
{
	g_autoptr(LrgBone)   bone = lrg_bone_new ("arm", 0);
	LrgAnimationKeyframe *k0 = lrg_animation_keyframe_new (0.0f);
	LrgAnimationKeyframe *k1 = lrg_animation_keyframe_new (1.0f);
	guint                 tr;

	*out_skel = lrg_skeleton_new ();
	lrg_skeleton_add_bone (*out_skel, bone);

	*out_clip = lrg_animation_clip_new ("wave");
	lrg_animation_clip_set_duration (*out_clip, 1.0f);
	tr = lrg_animation_clip_add_track (*out_clip, "arm");
	lrg_bone_pose_set_identity (&k0->pose);
	lrg_bone_pose_set_identity (&k1->pose);
	k1->pose.position_x = 2.0f;
	lrg_animation_keyframe_set_linear_tangents (k0);
	lrg_animation_keyframe_set_linear_tangents (k1);
	lrg_animation_clip_add_keyframe (*out_clip, tr, k0);
	lrg_animation_clip_add_keyframe (*out_clip, tr, k1);
	lrg_animation_keyframe_free (k0);
	lrg_animation_keyframe_free (k1);
}

static gboolean
on_sample (LrgSequencePlayer *player,
           LrgEntity3D       *entity,
           const gchar       *binding,
           const gchar       *clip,
           gfloat             clip_time,
           gfloat             weight,
           gpointer           user_data)
{
	Fixture *f = user_data;

	(void) player;
	(void) entity;
	(void) weight;
	g_ptr_array_add (f->log, g_strdup_printf ("%s:%s@%g", binding, clip, (gdouble) clip_time));
	return FALSE;
}

static void
test_animation_and_bone_tracks (Fixture       *f,
                                gconstpointer  data)
{
	g_autoptr(LrgSequence)       seq = lrg_sequence_new ();
	g_autoptr(LrgSequenceTrack)  anim = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_ANIMATION, NULL);
	g_autoptr(LrgSequenceTrack)  bone = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_TRANSFORM, NULL);
	g_autoptr(LrgSequencePlayer) p = NULL;
	g_autoptr(LrgSkeleton)       skel = NULL;
	g_autoptr(LrgAnimationClip)  clip = NULL;
	g_autofree gchar            *log = NULL;
	const LrgBonePose           *pose;
	LrgBonePose                  expect;

	(void) data;
	make_rig (&skel, &clip);
	lrg_sequence_set_length (seq, 4.0f);
	lrg_sequence_track_set_binding (anim, "hero-guid");
	/* clip time = 0.25 + (t - 1) * 0.5 */
	lrg_sequence_track_add_clip (anim, 1.0f, 2.0f, "wave", 0.25f, 0.5f, FALSE, 1.0f);
	lrg_sequence_add_track (seq, anim);

	/* Bone track: rotation-only offset, layered after the clip */
	lrg_sequence_track_set_binding (bone, "hero-guid");
	lrg_sequence_track_set_bone (bone, "arm");
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (bone, LRG_SEQUENCE_CHANNEL_ROTATION_Y),
	                            0.0f, 0.5f, LRG_SEQUENCE_TANGENT_AUTO);
	lrg_sequence_add_track (seq, bone);

	p = lrg_sequence_player_new (seq);
	lrg_sequence_player_set_world (p, f->world);
	lrg_sequence_player_bind_skeleton (p, "hero-guid", skel);
	lrg_sequence_player_add_clip (p, clip);
	g_assert_true (lrg_sequence_player_get_clip (p, "wave") == clip);
	g_assert_true (lrg_sequence_player_get_skeleton (p, "hero-guid") == skel);
	g_signal_connect (p, "animation-sample", G_CALLBACK (on_sample), f);

	lrg_sequence_player_set_time (p, 2.0f);   /* clip time 0.25 + 1 * 0.5 = 0.75 */
	pose = lrg_bone_get_local_pose (lrg_skeleton_get_bone_by_name (skel, "arm"));
	memset (&expect, 0, sizeof expect);
	lrg_animation_clip_sample_track (clip, 0, 0.75f, &expect);
	g_assert_cmpfloat (expect.position_x, >, 0.5f);
	g_assert_cmpfloat_with_epsilon (pose->position_x, expect.position_x, 1e-5f);
	/* euler (0, 0.5, 0) -> quat y = sin(0.25), w = cos(0.25) */
	g_assert_cmpfloat_with_epsilon (pose->rotation_y, sinf (0.25f), 1e-4f);
	g_assert_cmpfloat_with_epsilon (pose->rotation_w, cosf (0.25f), 1e-4f);

	log = take_log (f);
	g_assert_cmpstr (log, ==, "hero-guid:wave@0.75");
}

static void
test_subsequence (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(LrgSequence)       parent = lrg_sequence_new ();
	g_autoptr(LrgSequence)       child = event_sequence ();
	g_autoptr(LrgSequenceTrack)  sub = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_SUBSEQUENCE, NULL);
	g_autoptr(LrgSequenceTrack)  tr = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_TRANSFORM, NULL);
	g_autoptr(LrgSequencePlayer) p = NULL;
	g_autofree gchar            *log = NULL;
	gfloat                       pos[3];

	(void) data;
	/* Child: events at 0,1,2,4 plus hero X = local time */
	lrg_sequence_track_set_binding (tr, "hero-guid");
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_LOCATION_X),
	                            0.0f, 0.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_LOCATION_X),
	                            4.0f, 4.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_add_track (child, tr);

	/* Parent: the child at 2 s, from its 1 s mark, twice as fast */
	lrg_sequence_set_length (parent, 6.0f);
	lrg_sequence_track_add_subsequence (sub, 2.0f, 0.0f, "child", 1.0f, 2.0f, FALSE);
	lrg_sequence_add_track (parent, sub);

	p = lrg_sequence_player_new (parent);
	lrg_sequence_player_set_world (p, f->world);
	lrg_sequence_player_add_sequence (p, "child", child);
	g_signal_connect (p, "event", G_CALLBACK (on_event), f);

	lrg_sequence_player_play (p);
	lrg_sequence_player_advance (p, 3.0f);    /* parent 3 -> child 1 + 2 = 3 */
	lrg_entity3d_get_position (f->hero, pos);
	g_assert_cmpfloat_with_epsilon (pos[0], 3.0f, EPS);
	/* child a@1 is at parent 2 (section start, included on entry);
	 * child b@2 at parent 2.5 */
	lrg_sequence_player_advance (p, 2.0f);    /* parent 5 -> child 7, clamped at 4 */
	log = take_log (f);
	g_assert_cmpstr (log, ==, "a@2,b@2.5,end@3.5");
}

static void
test_component (Fixture       *f,
                gconstpointer  data)
{
	g_autoptr(LrgSequence)          seq = lrg_sequence_new ();
	g_autoptr(LrgSequenceTrack)     tr = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_TRANSFORM, NULL);
	g_autoptr(LrgSequenceComponent) comp = NULL;
	g_autoptr(LrgEntity3D)          actor = lrg_entity3d_new_with_name ("Actor");
	g_autoptr(GError)               error = NULL;
	g_autofree gchar               *dir = g_dir_make_tmp ("lrg-seqc-XXXXXX", NULL);
	g_autofree gchar               *path = g_build_filename (dir, "bob.rseq", NULL);
	g_autoptr(LrgSequenceComponent) by_path = NULL;
	gfloat                          pos[3];

	(void) data;
	/* "self" binding: the sequence moves whichever entity carries it */
	lrg_sequence_set_length (seq, 2.0f);
	lrg_sequence_set_play_mode (seq, LRG_SEQUENCE_PLAY_LOOP);
	lrg_sequence_track_set_binding (tr, "self");
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_LOCATION_Y),
	                            0.0f, 0.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_curve_set_key (lrg_sequence_track_ensure_channel_curve (tr, LRG_SEQUENCE_CHANNEL_LOCATION_Y),
	                            2.0f, 2.0f, LRG_SEQUENCE_TANGENT_LINEAR);
	lrg_sequence_add_track (seq, tr);

	comp = lrg_sequence_component_new_with_sequence (seq);
	g_assert_true (lrg_sequence_component_get_autoplay (comp));
	lrg_entity3d_add_component (actor, LRG_COMPONENT (comp));
	lrg_world3d_add_entity (f->world, actor);
	g_assert_true (lrg_sequence_player_is_playing (lrg_sequence_component_get_player (comp)));

	lrg_world3d_update (f->world, 0.5f);
	lrg_world3d_update (f->world, 2.0f);    /* 2.5 -> loops to 0.5 */
	lrg_entity3d_get_position (actor, pos);
	g_assert_cmpfloat_with_epsilon (pos[1], 0.5f, EPS);

	/* Leaving the world stops and rewinds */
	lrg_world3d_remove_entity (f->world, actor);
	g_assert_false (lrg_sequence_player_is_playing (lrg_sequence_component_get_player (comp)));

	/* Loaded from a path, with a play-mode override and the registry
	 * name ("sequence") */
	g_assert_true (lrg_sequence_save_to_file (seq, path, &error));
	by_path = g_object_new (LRG_TYPE_SEQUENCE_COMPONENT, "sequence-path", path, "play-mode",
	                        LRG_SEQUENCE_PLAY_PING_PONG, "autoplay", FALSE, NULL);
	g_assert_null (lrg_sequence_component_get_sequence (by_path));
	lrg_entity3d_add_component (f->hero, LRG_COMPONENT (by_path));
	g_assert_nonnull (lrg_sequence_component_get_sequence (by_path));
	g_assert_cmpstr (lrg_sequence_component_get_sequence_path (by_path), ==, path);
	g_assert_cmpint (lrg_sequence_player_get_play_mode (lrg_sequence_component_get_player (by_path)), ==,
	                 LRG_SEQUENCE_PLAY_PING_PONG);
	g_assert_false (lrg_sequence_player_is_playing (lrg_sequence_component_get_player (by_path)));
	lrg_sequence_component_play (by_path);
	g_assert_true (lrg_sequence_player_is_playing (lrg_sequence_component_get_player (by_path)));
	lrg_sequence_component_stop (by_path);
	lrg_sequence_component_set_autoplay (by_path, TRUE);
	g_assert_true (lrg_sequence_component_get_autoplay (by_path));
	lrg_sequence_component_set_sequence (by_path, NULL);
	g_assert_null (lrg_sequence_component_get_sequence (by_path));

	g_unlink (path);
	g_rmdir (dir);
}

/* ==========================================================================
 * Level timeline: Bézier keys, bone channels, conversion, round trip
 * ========================================================================== */

static void
test_timeline_tangents (void)
{
	g_autoptr(LrgTimelineTrack) t = lrg_timeline_track_new ("n", "location.x");
	g_autoptr(LrgTimelineTrack) copy = NULL;
	LrgSequenceTangentMode      mode;
	gfloat                      v = 0.0f, in = 0.0f, out = 0.0f;

	lrg_timeline_track_set_key (t, 0.0f, 0.0f, LRG_EASING_LINEAR);
	lrg_timeline_track_set_key (t, 1.0f, 1.0f, LRG_EASING_LINEAR);
	g_assert_true (lrg_timeline_track_sample (t, 0.25f, &v));
	g_assert_cmpfloat_with_epsilon (v, 0.25f, EPS);

	/* Auto on key 0 (flat at the end), key 1 eased -> secant in-slope 1:
	 * h11 * 1 at s=.25 = (0.015625 - 0.0625) + h01 0.15625 = 0.109375 */
	g_assert_true (lrg_timeline_track_set_key_tangents (t, 0, LRG_SEQUENCE_TANGENT_AUTO, 0.0f, 0.0f));
	g_assert_true (lrg_timeline_track_sample (t, 0.25f, &v));
	g_assert_cmpfloat_with_epsilon (v, 0.109375f, EPS);

	/* Both auto -> smoothstep */
	lrg_timeline_track_set_key_tangents (t, 1, LRG_SEQUENCE_TANGENT_AUTO, 0.0f, 0.0f);
	g_assert_true (lrg_timeline_track_sample (t, 0.25f, &v));
	g_assert_cmpfloat_with_epsilon (v, 0.15625f, EPS);

	/* Constant */
	lrg_timeline_track_set_key_tangents (t, 0, LRG_SEQUENCE_TANGENT_CONSTANT, 0.0f, 0.0f);
	g_assert_true (lrg_timeline_track_sample (t, 0.9f, &v));
	g_assert_cmpfloat (v, ==, 0.0f);

	/* Broken handles survive re-keying the value and copying */
	lrg_timeline_track_set_key_tangents (t, 0, LRG_SEQUENCE_TANGENT_BROKEN, 3.0f, 2.0f);
	lrg_timeline_track_set_key (t, 0.0f, 0.0f, LRG_EASING_EASE_IN_QUAD);
	g_assert_true (lrg_timeline_track_get_key_tangents (t, 0, &mode, &in, &out));
	g_assert_cmpint (mode, ==, LRG_SEQUENCE_TANGENT_BROKEN);
	g_assert_cmpfloat (in, ==, 3.0f);
	g_assert_cmpfloat (out, ==, 2.0f);
	copy = lrg_timeline_track_copy (t);
	g_assert_true (lrg_timeline_track_equal (t, copy));
	lrg_timeline_track_clear_key_tangents (copy, 0);
	g_assert_false (lrg_timeline_track_equal (t, copy));
	g_assert_false (lrg_timeline_track_get_key_tangents (copy, 0, NULL, NULL, NULL));

	/* move_key keeps value, easing and handles */
	g_assert_cmpint (lrg_timeline_track_move_key (t, 0, 0.5f), ==, 0);
	g_assert_true (lrg_timeline_track_get_key_tangents (t, 0, &mode, NULL, &out));
	g_assert_cmpfloat (out, ==, 2.0f);

	/* Removing a key drops its handles; clearing the curve directly too */
	lrg_timeline_track_remove_key (t, 0);
	g_assert_cmpuint (lrg_timeline_track_get_n_keys (t), ==, 1);
	lrg_keyframe_curve_clear (lrg_timeline_track_get_curve (t));
	lrg_timeline_track_set_key (t, 1.0f, 5.0f, LRG_EASING_LINEAR);
	g_assert_false (lrg_timeline_track_get_key_tangents (t, 0, NULL, NULL, NULL));
}

static void
test_timeline_bone_channels (void)
{
	g_autoptr(LrgNode)          node = lrg_node_new ("Rig");
	g_autofree gchar           *bone = NULL;
	LrgSequenceTransformChannel ch;
	gfloat                      off[9], v = 0.0f;
	LrgBonePose                 bind, out;

	g_assert_true (lrg_timeline_channel_is_valid ("bone.spine.rotation.y"));
	g_assert_true (lrg_timeline_channel_is_valid ("bone.upper.arm.L.scale.x"));
	g_assert_false (lrg_timeline_channel_is_valid ("bone..rotation.y"));
	g_assert_false (lrg_timeline_channel_is_valid ("bone.spine.rotation.w"));
	g_assert_false (lrg_timeline_channel_is_valid ("bone.rotation.y"));
	g_assert_true (lrg_timeline_channel_get_bone ("bone.upper.arm.L.scale.x", &bone, &ch));
	g_assert_cmpstr (bone, ==, "upper.arm.L");
	g_assert_cmpint (ch, ==, LRG_SEQUENCE_CHANNEL_SCALE_X);

	/* Default: identity offset; writing goes to the side table only */
	g_assert_false (lrg_timeline_get_bone_offset (node, "spine", off));
	g_assert_cmpfloat (off[6], ==, 1.0f);
	g_assert_true (lrg_timeline_channel_read (node, "bone.spine.scale.y", &v));
	g_assert_cmpfloat (v, ==, 1.0f);
	g_assert_true (lrg_timeline_channel_write (node, "bone.spine.location.z", 0.25f));
	g_assert_true (lrg_timeline_get_bone_offset (node, "spine", off));
	g_assert_cmpfloat (off[2], ==, 0.25f);
	g_assert_true (lrg_timeline_channel_read (node, "bone.spine.location.z", &v));
	g_assert_cmpfloat (v, ==, 0.25f);
	lrg_timeline_clear_bone_offsets (node);
	g_assert_false (lrg_timeline_get_bone_offset (node, "spine", off));

	/* Composition against a bind pose */
	lrg_bone_pose_set_identity (&bind);
	bind.position_x = 1.0f;
	bind.scale_y = 2.0f;
	off[0] = 0.5f; off[1] = 0.0f; off[2] = 0.0f;
	off[3] = 0.0f; off[4] = 0.0f; off[5] = (gfloat) G_PI;
	off[6] = 1.0f; off[7] = 3.0f; off[8] = 1.0f;
	lrg_sequence_compose_bone_offset (&bind, off, &out);
	g_assert_cmpfloat_with_epsilon (out.position_x, 1.5f, EPS);
	g_assert_cmpfloat_with_epsilon (out.scale_y, 6.0f, EPS);
	g_assert_cmpfloat_with_epsilon (fabsf (out.rotation_z), 1.0f, EPS);
}

static void
test_timeline_to_sequence (Fixture       *f,
                           gconstpointer  data)
{
	g_autoptr(LrgTimeline)       tl = lrg_timeline_new ("walk");
	g_autoptr(LrgSequence)       seq = NULL;
	g_autoptr(LrgSequencePlayer) p = NULL;
	g_autoptr(LrgSkeleton)       skel = NULL;
	g_autoptr(LrgAnimationClip)  clip = NULL;
	LrgTimelineTrack            *t;
	LrgSequenceTrack            *bt;
	gfloat                       pos[3], v = 0.0f;

	(void) data;
	lrg_timeline_set_length (tl, 2.0f);
	lrg_timeline_set_loop (tl, TRUE);
	t = lrg_timeline_ensure_track (tl, "hero-guid", "location.x");
	lrg_timeline_track_set_key (t, 0.0f, 0.0f, LRG_EASING_EASE_IN_QUAD);
	lrg_timeline_track_set_key (t, 1.0f, 1.0f, LRG_EASING_LINEAR);
	t = lrg_timeline_ensure_track (tl, "hero-guid", "bone.arm.location.y");
	lrg_timeline_track_set_key (t, 0.0f, 0.0f, LRG_EASING_LINEAR);
	lrg_timeline_track_set_key (t, 1.0f, 2.0f, LRG_EASING_LINEAR);
	lrg_timeline_track_set_key_tangents (t, 0, LRG_SEQUENCE_TANGENT_AUTO, 0.0f, 0.0f);
	lrg_timeline_ensure_track (tl, "hero-guid", "roughness");

	seq = lrg_sequence_new_from_timeline (tl, 10.0f);
	g_assert_cmpstr (lrg_sequence_get_name (seq), ==, "walk");
	g_assert_cmpint (lrg_sequence_get_play_mode (seq), ==, LRG_SEQUENCE_PLAY_LOOP);
	g_assert_cmpuint (lrg_sequence_get_n_tracks (seq), ==, 2);
	bt = lrg_sequence_get_track (seq, 1);
	g_assert_cmpstr (lrg_sequence_track_get_bone (bt), ==, "arm");

	p = lrg_sequence_player_new (seq);
	lrg_sequence_player_set_world (p, f->world);
	make_rig (&skel, &clip);
	lrg_sequence_player_bind_skeleton (p, "hero-guid", skel);

	/* The eased segment is baked at 10 fps: matches the timeline at
	 * frame times */
	lrg_sequence_player_set_time (p, 0.5f);
	lrg_entity3d_get_position (f->hero, pos);
	lrg_timeline_track_sample (lrg_timeline_get_track (tl, 0), 0.5f, &v);
	g_assert_cmpfloat_with_epsilon (pos[0], v, 1e-4f);
	/* Bone: auto key 0 then linear-eased key 1 (secant 2): 0.5 * 2 *
	 * (0.5 - 0.25) ... = h01 * 2 + h11 * 2 = 1 - 0.25 = 0.75 */
	g_assert_cmpfloat_with_epsilon (lrg_bone_get_local_pose (lrg_skeleton_get_bone_by_name (skel, "arm"))->position_y,
	                                0.75f, 1e-4f);
}

static void
test_timeline_level_round_trip (void)
{
	g_autoptr(LrgLevel)           level = lrg_level_new ("L");
	g_autoptr(LrgLevel)           back = NULL;
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgTimeline)        tl = lrg_timeline_new ("cine");
	g_autoptr(GError)             error = NULL;
	g_autofree gchar             *text = NULL;
	g_autofree gchar             *text2 = NULL;
	LrgTimelineTrack             *t;

	t = lrg_timeline_ensure_track (tl, "abc", "bone.spine.rotation.z");
	lrg_timeline_track_set_key (t, 0.0f, 0.0f, LRG_EASING_LINEAR);
	lrg_timeline_track_set_key (t, 0.5f, 1.5f, LRG_EASING_EASE_OUT_QUAD);
	lrg_timeline_track_set_key (t, 1.0f, -1.0f, LRG_EASING_LINEAR);
	lrg_timeline_track_set_key_tangents (t, 0, LRG_SEQUENCE_TANGENT_USER, 2.0f, 0.0f);
	lrg_timeline_track_set_key_tangents (t, 1, LRG_SEQUENCE_TANGENT_BROKEN, -1.0f, 4.0f);
	lrg_timeline_track_set_key_tangents (t, 2, LRG_SEQUENCE_TANGENT_CONSTANT, 0.0f, 0.0f);
	lrg_level_insert_timeline (level, tl, -1);

	text = lrg_level_serializer_save_to_data (ser, level, NULL);
	g_assert_nonnull (strstr (text, "bone.spine.rotation.z"));
	g_assert_nonnull (strstr (text, "tangent: broken"));
	back = lrg_level_serializer_load_from_data (ser, text, -1, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_timeline_equal (lrg_level_get_timeline (back, 0), tl));
	text2 = lrg_level_serializer_save_to_data (ser, back, NULL);
	g_assert_cmpstr (text, ==, text2);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/sequence/curve/auto-two-keys", test_curve_auto_two_keys);
	g_test_add_func ("/sequence/curve/auto-three-keys", test_curve_auto_three_keys);
	g_test_add_func ("/sequence/curve/linear-constant", test_curve_linear_constant);
	g_test_add_func ("/sequence/curve/user-broken", test_curve_user_broken);
	g_test_add_func ("/sequence/format/round-trip", test_format_round_trip);
	g_test_add_func ("/sequence/format/errors", test_format_errors);
	g_test_add_func ("/sequence/format/file", test_file_round_trip);

	g_test_add ("/sequence/player/transform", Fixture, NULL, fixture_setup, test_transform_track, fixture_teardown);
	g_test_add ("/sequence/player/property", Fixture, NULL, fixture_setup, test_property_track, fixture_teardown);
	g_test_add ("/sequence/player/camera-cut", Fixture, NULL, fixture_setup, test_camera_cut, fixture_teardown);
	g_test_add ("/sequence/player/events", Fixture, NULL, fixture_setup, test_events_forward_backward,
	            fixture_teardown);
	g_test_add ("/sequence/player/loop-ping-pong", Fixture, NULL, fixture_setup, test_loop_and_ping_pong,
	            fixture_teardown);
	g_test_add ("/sequence/player/audio", Fixture, NULL, fixture_setup, test_audio_track, fixture_teardown);
	g_test_add ("/sequence/player/animation-bone", Fixture, NULL, fixture_setup, test_animation_and_bone_tracks,
	            fixture_teardown);
	g_test_add ("/sequence/player/subsequence", Fixture, NULL, fixture_setup, test_subsequence, fixture_teardown);
	g_test_add ("/sequence/component", Fixture, NULL, fixture_setup, test_component, fixture_teardown);

	g_test_add_func ("/sequence/timeline/tangents", test_timeline_tangents);
	g_test_add_func ("/sequence/timeline/bone-channels", test_timeline_bone_channels);
	g_test_add ("/sequence/timeline/to-sequence", Fixture, NULL, fixture_setup, test_timeline_to_sequence,
	            fixture_teardown);
	g_test_add_func ("/sequence/timeline/level-round-trip", test_timeline_level_round_trip);

	return g_test_run ();
}
