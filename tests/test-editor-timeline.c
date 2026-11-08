/* test-editor-timeline.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for keyframe animation: channels, LrgTimelineTrack
 * sampling with easing, LrgTimeline wrap/apply, the undoable editor API
 * (lrg_editor_timeline_*) and the .rlevel round trip.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-4)

typedef struct
{
	LrgEditor *editor;
	LrgNode   *node;   /* borrowed: owned by the level */
} Fixture;

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(LrgNode)       node = lrg_node_new ("Box");
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);

	f->editor = lrg_editor_new ();
	lrg_node_set_visual (node, visual);
	lrg_editor_add_node (f->editor, node, NULL);
	f->node = node;
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->editor);
}

static LrgTimeline *
timeline_named (LrgEditor   *editor,
                const gchar *name)
{
	LrgLevel *level = lrg_editor_get_level (editor);
	gint      idx = lrg_level_find_timeline (level, name);

	return idx >= 0 ? lrg_level_get_timeline (level, (guint) idx) : NULL;
}

/* ==========================================================================
 * Channels and tracks
 * ========================================================================== */

static void
test_channels (void)
{
	g_autoptr(LrgNode)       node = lrg_node_new ("n");
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (LRG_NODE_VISUAL_SPRITE);
	gfloat                   v = -1.0f;

	g_assert_true (lrg_timeline_channel_is_valid ("location.x"));
	g_assert_true (lrg_timeline_channel_is_valid ("rotation.z"));
	g_assert_true (lrg_timeline_channel_is_valid ("color.a"));
	g_assert_true (lrg_timeline_channel_is_valid ("param.intensity"));
	g_assert_true (lrg_timeline_channel_is_valid ("metallic"));
	g_assert_false (lrg_timeline_channel_is_valid ("location.w"));
	g_assert_false (lrg_timeline_channel_is_valid ("location.xy"));
	g_assert_false (lrg_timeline_channel_is_valid ("param."));
	g_assert_false (lrg_timeline_channel_is_valid ("bogus"));
	g_assert_false (lrg_timeline_channel_is_valid (NULL));

	g_assert_true (lrg_timeline_channel_write (node, "location.y", 3.5f));
	g_assert_true (lrg_timeline_channel_read (node, "location.y", &v));
	g_assert_cmpfloat_with_epsilon (v, 3.5f, EPS);
	g_assert_true (lrg_timeline_channel_read (node, "scale.z", &v));
	g_assert_cmpfloat_with_epsilon (v, 1.0f, EPS);

	/* Material/param channels need a visual. */
	g_assert_false (lrg_timeline_channel_write (node, "color.r", 0.5f));
	lrg_node_set_visual (node, visual);
	g_assert_true (lrg_timeline_channel_read (node, "color.r", &v));
	g_assert_cmpfloat_with_epsilon (v, 1.0f, EPS);
	g_assert_true (lrg_timeline_channel_write (node, "color.g", 0.25f));
	g_assert_nonnull (lrg_node_visual_get_material (lrg_node_get_visual (node)));
	g_assert_true (lrg_timeline_channel_read (node, "color.g", &v));
	g_assert_cmpfloat_with_epsilon (v, 0.25f, EPS);
	g_assert_true (lrg_timeline_channel_write (node, "param.radius", 2.0f));
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (lrg_node_get_visual (node), "radius", 0.0),
	                                2.0, EPS);
}

static void
test_track_sampling (void)
{
	g_autoptr(LrgTimelineTrack) track = lrg_timeline_track_new ("g", "location.x");
	g_autoptr(LrgTimelineTrack) copy = NULL;
	gfloat                      v = 0.0f, t = 0.0f;
	LrgEasingType               e;

	g_assert_false (lrg_timeline_track_sample (track, 0.0f, &v));

	lrg_timeline_track_set_key (track, 0.0f, 0.0f, LRG_EASING_LINEAR);
	lrg_timeline_track_set_key (track, 2.0f, 10.0f, LRG_EASING_LINEAR);
	g_assert_true (lrg_timeline_track_sample (track, 1.0f, &v));
	g_assert_cmpfloat_with_epsilon (v, 5.0f, EPS);
	/* Clamped outside the keys. */
	lrg_timeline_track_sample (track, 9.0f, &v);
	g_assert_cmpfloat_with_epsilon (v, 10.0f, EPS);

	/* Easing applies to the segment after the key. */
	lrg_timeline_track_set_key (track, 0.0f, 0.0f, LRG_EASING_EASE_IN_QUAD);
	g_assert_cmpuint (lrg_timeline_track_get_n_keys (track), ==, 2);
	lrg_timeline_track_sample (track, 1.0f, &v);
	g_assert_cmpfloat_with_epsilon (v, 2.5f, EPS);

	/* Near-duplicate times replace instead of adding. */
	lrg_timeline_track_set_key (track, 2.0002f, 4.0f, LRG_EASING_LINEAR);
	g_assert_cmpuint (lrg_timeline_track_get_n_keys (track), ==, 2);
	g_assert_true (lrg_timeline_track_get_key (track, 1, &t, &v, &e));
	g_assert_cmpfloat_with_epsilon (v, 4.0f, EPS);

	g_assert_cmpint (lrg_timeline_track_find_key (track, 1.99f, 0.05f), ==, 1);
	g_assert_cmpint (lrg_timeline_track_find_key (track, 1.0f, 0.05f), ==, -1);

	copy = lrg_timeline_track_copy (track);
	g_assert_true (lrg_timeline_track_equal (track, copy));
	g_assert_true (lrg_timeline_track_remove_key (copy, 0));
	g_assert_false (lrg_timeline_track_equal (track, copy));
}

static void
test_timeline_wrap_apply (void)
{
	g_autoptr(LrgTimeline) tl = lrg_timeline_new ("t");
	g_autoptr(LrgNode)     root = lrg_node_new ("root");
	g_autoptr(LrgNode)     child = lrg_node_new ("child");
	g_autoptr(LrgTimeline) copy = NULL;
	LrgTimelineTrack      *track;

	lrg_node_add_child (root, child);
	lrg_timeline_set_length (tl, 4.0f);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_wrap_time (tl, 5.0f), 4.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_wrap_time (tl, -1.0f), 0.0f, EPS);
	lrg_timeline_set_loop (tl, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_wrap_time (tl, 5.0f), 1.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_wrap_time (tl, -1.0f), 3.0f, EPS);

	track = lrg_timeline_ensure_track (tl, lrg_node_get_guid (child), "scale.x");
	g_assert_true (track == lrg_timeline_ensure_track (tl, lrg_node_get_guid (child), "scale.x"));
	lrg_timeline_track_set_key (track, 0.0f, 1.0f, LRG_EASING_LINEAR);
	lrg_timeline_track_set_key (track, 4.0f, 3.0f, LRG_EASING_LINEAR);
	/* A track for a node that is not there is skipped. */
	lrg_timeline_track_set_key (lrg_timeline_ensure_track (tl, "missing", "location.x"), 0.0f, 1.0f,
	                            LRG_EASING_LINEAR);

	g_assert_cmpuint (lrg_timeline_apply (tl, root, 2.0f), ==, 1);
	g_assert_cmpfloat_with_epsilon (lrg_node_get_scale (child)->x, 2.0f, EPS);

	copy = lrg_timeline_copy (tl);
	g_assert_true (lrg_timeline_equal (tl, copy));
	lrg_timeline_set_autoplay (copy, TRUE);
	g_assert_false (lrg_timeline_equal (tl, copy));
	g_assert_true (lrg_timeline_remove_track (copy, 1));
	g_assert_cmpuint (lrg_timeline_get_n_tracks (copy), ==, 1);
}

/* ==========================================================================
 * Undoable editing
 * ========================================================================== */

static void
test_editor_add_configure_remove (Fixture       *f,
                                  gconstpointer  data)
{
	LrgLevel *level = lrg_editor_get_level (f->editor);

	g_assert_true (lrg_editor_timeline_add (f->editor, "intro", 3.0f, TRUE));
	g_assert_false (lrg_editor_timeline_add (f->editor, "intro", 1.0f, FALSE));
	g_assert_cmpuint (lrg_level_get_n_timelines (level), ==, 1);
	g_assert_true (lrg_timeline_get_loop (timeline_named (f->editor, "intro")));

	g_assert_true (lrg_editor_timeline_configure (f->editor, "intro", "opening", 5.0f, 0, 1));
	g_assert_null (timeline_named (f->editor, "intro"));
	g_assert_cmpfloat_with_epsilon (lrg_timeline_get_length (timeline_named (f->editor, "opening")), 5.0f, EPS);
	g_assert_false (lrg_timeline_get_loop (timeline_named (f->editor, "opening")));
	g_assert_true (lrg_timeline_get_autoplay (timeline_named (f->editor, "opening")));

	lrg_editor_undo (f->editor);
	g_assert_nonnull (timeline_named (f->editor, "intro"));
	g_assert_null (timeline_named (f->editor, "opening"));
	lrg_editor_redo (f->editor);

	g_assert_true (lrg_editor_timeline_remove (f->editor, "opening"));
	g_assert_cmpuint (lrg_level_get_n_timelines (level), ==, 0);
	g_assert_false (lrg_editor_timeline_remove (f->editor, "opening"));
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_level_get_n_timelines (level), ==, 1);
	lrg_editor_undo (f->editor);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_level_get_n_timelines (level), ==, 0);
}

static void
test_editor_keys (Fixture       *f,
                  gconstpointer  data)
{
	LrgTimeline      *tl;
	LrgTimelineTrack *track;
	guint             undo0 = lrg_editor_get_undo_count (f->editor);
	gfloat            t = 0.0f, v = 0.0f;
	LrgEasingType     e = LRG_EASING_LINEAR;

	/* Keying an unknown timeline creates it in the same step. */
	g_assert_true (lrg_editor_timeline_set_key (f->editor, "walk", f->node, "location.x", 0.0f, 0.0f,
	                                            LRG_EASING_LINEAR, FALSE));
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	g_assert_false (lrg_editor_timeline_set_key (f->editor, "walk", f->node, "nope", 0.0f, 0.0f,
	                                             LRG_EASING_LINEAR, FALSE));

	/* Insert Keyframe snapshots the nine transform channels. */
	lrg_node_set_location_xyz (f->node, 4.0f, 0.0f, 0.0f);
	g_assert_cmpuint (lrg_editor_timeline_key_node (f->editor, "walk", f->node, 1.0f, NULL, LRG_EASING_LINEAR), ==, 9);
	tl = timeline_named (f->editor, "walk");
	g_assert_cmpuint (lrg_timeline_get_n_tracks (tl), ==, 9);
	track = lrg_timeline_get_track (tl, (guint) lrg_timeline_find_track (tl, lrg_node_get_guid (f->node), "location.x"));
	g_assert_cmpuint (lrg_timeline_track_get_n_keys (track), ==, 2);
	lrg_timeline_track_sample (track, 0.5f, &v);
	g_assert_cmpfloat_with_epsilon (v, 2.0f, EPS);

	/* Easing change on every track of the node at t = 0. */
	g_assert_cmpuint (lrg_editor_timeline_set_easing (f->editor, "walk", f->node, NULL, 0.0f,
	                                                  LRG_EASING_EASE_OUT_QUAD), ==, 1);
	tl = timeline_named (f->editor, "walk");
	track = lrg_timeline_get_track (tl, (guint) lrg_timeline_find_track (tl, lrg_node_get_guid (f->node), "location.x"));
	lrg_timeline_track_get_key (track, 0, NULL, NULL, &e);
	g_assert_cmpint (e, ==, LRG_EASING_EASE_OUT_QUAD);

	/* A drag of the t = 1 keys is one undo step. */
	undo0 = lrg_editor_get_undo_count (f->editor);
	g_assert_cmpuint (lrg_editor_timeline_move_key (f->editor, "walk", f->node, NULL, 1.0f, 1.2f, TRUE), ==, 9);
	g_assert_cmpuint (lrg_editor_timeline_move_key (f->editor, "walk", f->node, NULL, 1.2f, 1.5f, TRUE), ==, 9);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	tl = timeline_named (f->editor, "walk");
	track = lrg_timeline_get_track (tl, (guint) lrg_timeline_find_track (tl, lrg_node_get_guid (f->node), "location.x"));
	lrg_timeline_track_get_key (track, 1, &t, &v, NULL);
	g_assert_cmpfloat_with_epsilon (t, 1.5f, EPS);
	g_assert_cmpfloat_with_epsilon (v, 4.0f, EPS);
	lrg_editor_undo (f->editor);
	tl = timeline_named (f->editor, "walk");
	track = lrg_timeline_get_track (tl, (guint) lrg_timeline_find_track (tl, lrg_node_get_guid (f->node), "location.x"));
	lrg_timeline_track_get_key (track, 1, &t, NULL, NULL);
	g_assert_cmpfloat_with_epsilon (t, 1.0f, EPS);

	/* Deleting the t = 1 keys drops the tracks that only had that key. */
	g_assert_cmpuint (lrg_editor_timeline_remove_key (f->editor, "walk", f->node, NULL, 1.0f), ==, 9);
	tl = timeline_named (f->editor, "walk");
	g_assert_cmpuint (lrg_timeline_get_n_tracks (tl), ==, 1);
	g_assert_cmpuint (lrg_editor_timeline_remove_track (f->editor, "walk", f->node, "location.x"), ==, 1);
	g_assert_cmpuint (lrg_timeline_get_n_tracks (timeline_named (f->editor, "walk")), ==, 0);
	lrg_editor_undo (f->editor);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_timeline_get_n_tracks (timeline_named (f->editor, "walk")), ==, 9);
	g_assert_true (lrg_editor_is_dirty (f->editor));
}

/* ==========================================================================
 * Serialization
 * ========================================================================== */

static void
test_serialize_round_trip (Fixture       *f,
                           gconstpointer  data)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autoptr(GError)             error = NULL;
	g_autofree gchar             *yaml = NULL;
	g_autofree gchar             *plain = NULL;
	gint                          idx;

	/* No timelines: the file keeps its old shape. */
	plain = lrg_level_serializer_save_to_data (ser, lrg_editor_get_level (f->editor), NULL);
	g_assert_null (strstr (plain, "timelines"));

	lrg_editor_timeline_add (f->editor, "spin", 1.5f, TRUE);
	lrg_editor_timeline_configure (f->editor, "spin", NULL, 0.0f, -1, 1);
	lrg_editor_timeline_set_key (f->editor, "spin", f->node, "rotation.y", 0.0f, 0.0f, LRG_EASING_EASE_IN_OUT_SINE,
	                             FALSE);
	lrg_editor_timeline_set_key (f->editor, "spin", f->node, "rotation.y", 1.5f, 6.25f, LRG_EASING_LINEAR, FALSE);
	lrg_editor_timeline_set_key (f->editor, "spin", f->node, "color.b", 0.75f, 0.5f, LRG_EASING_LINEAR, FALSE);

	yaml = lrg_level_serializer_save_to_data (ser, lrg_editor_get_level (f->editor), NULL);
	g_assert_nonnull (strstr (yaml, "timelines:"));
	g_assert_nonnull (strstr (yaml, "ease-in-out-sine"));

	loaded = lrg_level_serializer_load_from_data (ser, yaml, -1, &error);
	g_assert_no_error (error);
	idx = lrg_level_find_timeline (loaded, "spin");
	g_assert_cmpint (idx, ==, 0);
	g_assert_true (lrg_timeline_equal (lrg_level_get_timeline (loaded, 0), timeline_named (f->editor, "spin")));
}

/* move_key keeps Bézier handles; set/clear tangents and easing are undoable. */
static void
test_editor_tangents (Fixture       *f,
                      gconstpointer  data)
{
	LrgTimelineTrack       *track;
	LrgSequenceTangentMode  mode = LRG_SEQUENCE_TANGENT_AUTO;
	gfloat                  in_s = 0.0f, out_s = 0.0f;
	guint                   undo;
	gint                    k;

	lrg_editor_timeline_set_key (f->editor, "bez", f->node, "location.x", 0.0f, 0.0f, LRG_EASING_LINEAR, FALSE);
	lrg_editor_timeline_set_key (f->editor, "bez", f->node, "location.x", 1.0f, 2.0f, LRG_EASING_LINEAR, FALSE);
	lrg_editor_timeline_set_key (f->editor, "bez", f->node, "location.x", 2.0f, 0.0f, LRG_EASING_LINEAR, FALSE);

	/* Bézier key at 1 s, one undo step; same values again record nothing */
	undo = lrg_editor_get_undo_count (f->editor);
	g_assert_cmpuint (lrg_editor_timeline_set_tangents (f->editor, "bez", f->node, "location.x", 1.0f,
	                                                    LRG_SEQUENCE_TANGENT_BROKEN, 1.5f, -0.5f), ==, 1);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	g_assert_cmpuint (lrg_editor_timeline_set_tangents (f->editor, "bez", f->node, "location.x", 1.0f,
	                                                    LRG_SEQUENCE_TANGENT_BROKEN, 1.5f, -0.5f), ==, 1);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	track = lrg_timeline_get_track (timeline_named (f->editor, "bez"), 0);
	g_assert_true (lrg_timeline_track_get_key_tangents (track, 1, &mode, &in_s, &out_s));
	g_assert_cmpint (mode, ==, LRG_SEQUENCE_TANGENT_BROKEN);
	g_assert_cmpfloat_with_epsilon (in_s, 1.5f, EPS);
	g_assert_cmpfloat_with_epsilon (out_s, -0.5f, EPS);

	/* moving it keeps the handles (0.8.0 lost them: remove + set_key) */
	g_assert_cmpuint (lrg_editor_timeline_move_key (f->editor, "bez", f->node, "location.x", 1.0f, 1.25f, FALSE), ==, 1);
	track = lrg_timeline_get_track (timeline_named (f->editor, "bez"), 0);
	k = lrg_timeline_track_find_key (track, 1.25f, 0.001f);
	g_assert_cmpint (k, ==, 1);
	g_assert_true (lrg_timeline_track_get_key_tangents (track, (guint) k, &mode, &in_s, &out_s));
	g_assert_cmpint (mode, ==, LRG_SEQUENCE_TANGENT_BROKEN);
	g_assert_cmpfloat_with_epsilon (in_s, 1.5f, EPS);
	g_assert_cmpfloat_with_epsilon (out_s, -0.5f, EPS);

	/* undo the move, then the tangents */
	lrg_editor_undo (f->editor);
	track = lrg_timeline_get_track (timeline_named (f->editor, "bez"), 0);
	g_assert_cmpint (lrg_timeline_track_find_key (track, 1.0f, 0.001f), ==, 1);
	lrg_editor_undo (f->editor);
	track = lrg_timeline_get_track (timeline_named (f->editor, "bez"), 0);
	g_assert_false (lrg_timeline_track_get_key_tangents (track, 1, NULL, NULL, NULL));
	lrg_editor_redo (f->editor);

	/* clear_tangents: eased again */
	g_assert_cmpuint (lrg_editor_timeline_clear_tangents (f->editor, "bez", f->node, NULL, 1.0f), ==, 1);
	track = lrg_timeline_get_track (timeline_named (f->editor, "bez"), 0);
	g_assert_false (lrg_timeline_track_get_key_tangents (track, 1, NULL, NULL, NULL));

	/* picking an easing clears Bézier handles */
	lrg_editor_timeline_set_tangents (f->editor, "bez", f->node, "location.x", 0.0f, LRG_SEQUENCE_TANGENT_USER, 3.0f,
	                                  3.0f);
	track = lrg_timeline_get_track (timeline_named (f->editor, "bez"), 0);
	g_assert_true (lrg_timeline_track_get_key_tangents (track, 0, NULL, NULL, NULL));
	g_assert_cmpuint (lrg_editor_timeline_set_easing (f->editor, "bez", f->node, "location.x", 0.0f,
	                                                  LRG_EASING_EASE_IN_QUAD), ==, 1);
	track = lrg_timeline_get_track (timeline_named (f->editor, "bez"), 0);
	g_assert_false (lrg_timeline_track_get_key_tangents (track, 0, NULL, NULL, NULL));

	/* missing timeline / key */
	g_assert_cmpuint (lrg_editor_timeline_set_tangents (f->editor, "nope", f->node, NULL, 0.0f,
	                                                    LRG_SEQUENCE_TANGENT_AUTO, 0.0f, 0.0f), ==, 0);
	g_assert_cmpuint (lrg_editor_timeline_set_tangents (f->editor, "bez", f->node, NULL, 9.0f,
	                                                    LRG_SEQUENCE_TANGENT_AUTO, 0.0f, 0.0f), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor-timeline/channels", test_channels);
	g_test_add_func ("/editor-timeline/track-sampling", test_track_sampling);
	g_test_add_func ("/editor-timeline/wrap-apply", test_timeline_wrap_apply);
#define ADD(path, fn) g_test_add (path, Fixture, NULL, fixture_setup, fn, fixture_teardown)
	ADD ("/editor-timeline/editor/add-configure-remove", test_editor_add_configure_remove);
	ADD ("/editor-timeline/editor/keys", test_editor_keys);
	ADD ("/editor-timeline/serialize/round-trip", test_serialize_round_trip);
	ADD ("/editor-timeline/editor/tangents", test_editor_tangents);
#undef ADD

	return g_test_run ();
}
