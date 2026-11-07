/* test-editor-app-sequencer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The sequencer panel (.rseq) and the level timeline polish, driven
 * headless through the canvas and IPC:
 *
 *   ipc        sequence.* end to end: create, bind, tracks, keys with
 *              tangents, sections, events, scrub preview on level nodes,
 *              play/stop restore, undo/redo, save + reload byte stable
 *   canvas     curve editor handle and key drags (one undo step each),
 *              view key drag, Add Track menu, Key, ruler scrub, play,
 *              ping-pong, snapping, pilot camera, Delete
 *   files      open_file, from_timeline (Convert to Sequence)
 *   timeline   level timeline panel: tangent picker, Bézier keys keep
 *              their handles when dragged, Key Bone, timeline.tangent
 */

#include "editor-app-fixture.h"
#include <math.h>

#define EPS (1e-3)

/* The app's sequencer panel, docked and presented (docked here until
 * the shell creates it). */
static LrgEditorSequencerPanel *
seq_panel (EditorAppFixture *f)
{
	LrgDockArea             *dock = lrg_editor_app_get_dock (f->app);
	LrgEditorSequencerPanel *panel = lrg_editor_sequencer_panel_find (f->app);
	LrgDockPanel            *dp = lrg_dock_area_get_panel (dock, LRG_EDITOR_SEQUENCER_PANEL_ID);

	if (panel == NULL)
	{
		panel = lrg_editor_sequencer_panel_new (f->app);
		dp = lrg_dock_panel_new (LRG_EDITOR_SEQUENCER_PANEL_ID, "Sequencer");
		lrg_dock_panel_set_child (dp, LRG_WIDGET (panel));
		g_object_unref (panel);
		lrg_editor_app_add_panel (f->app, dp, NULL);
	}
	g_assert_nonnull (dp);
	lrg_dock_area_present_panel (dock, dp);
	/* room for the tracks and the curve editor (the bottom stack is 30 %) */
	lrg_dock_area_set_panel_share (dock, dp, 0.6f);
	efx_frames (f, 3);
	return panel;
}

static LrgWidget *
find (EditorAppFixture *f,
      const gchar      *name)
{
	LrgWidget *w = efx_find (f, name);

	if (w == NULL)
		g_error ("no widget named %s", name);
	return w;
}

static gchar *
create_node (EditorAppFixture *f,
             const gchar      *params)
{
	g_autofree gchar   *req = g_strdup_printf ("{'id':1,'method':'node.create','params':%s}", params);
	g_autoptr(JsonNode) r = efx_ipc_result (f, req);

	return g_strdup (json_object_get_string_member (json_node_get_object (r), "guid"));
}

static void
select_guid (EditorAppFixture *f,
             const gchar      *guid)
{
	LrgEditor *editor = lrg_editor_app_get_editor (f->app);

	lrg_editor_select (editor, lrg_editor_find_node (editor, guid), FALSE);
	efx_frame (f);
}

/* An IPC call formatted with printf; returns the result object. */
static JsonNode *
call (EditorAppFixture *f,
      const gchar      *format,
      ...) G_GNUC_PRINTF (2, 3);

static JsonNode *
call (EditorAppFixture *f,
      const gchar      *format,
      ...)
{
	va_list           args;
	g_autofree gchar *req = NULL;

	va_start (args, format);
	req = g_strdup_vprintf (format, args);
	va_end (args);
	return efx_ipc_result (f, req);
}

static gint64
result_int (JsonNode    *r,
            const gchar *member)
{
	return json_object_get_int_member (json_node_get_object (r), member);
}

static gfloat
node_x (EditorAppFixture *f,
        const gchar      *guid)
{
	LrgNode *node = lrg_editor_find_node (lrg_editor_app_get_editor (f->app), guid);

	return lrg_node_get_location (node)->x;
}

/* The world centre of a view keyframe. */
static void
view_key_point (LrgEditorSequencerPanel *panel,
                guint                    track,
                const gchar             *channel,
                gfloat                   time,
                gfloat                  *x,
                gfloat                  *y)
{
	LrgTimelineView      *view = lrg_editor_sequencer_panel_get_view (panel);
	LrgTimelineViewTrack *row = lrg_editor_sequencer_panel_find_track (panel, track, channel);
	gfloat                ty = 0.0f, th = 0.0f;

	g_assert_nonnull (row);
	g_assert_true (lrg_timeline_view_get_track_row (view, row, &ty, &th));
	*x = lrg_timeline_view_time_to_x (view, time);
	*y = ty + th * 0.5f;
}

/* A Hero cube, a camera, and a sequence with a transform track on Hero:
 * location.x 0 -> 4 over 1 s. */
static LrgEditorSequencerPanel *
setup_sequence (EditorAppFixture *f,
                gchar           **hero,
                gchar           **cam,
                gchar           **path)
{
	LrgEditorSequencerPanel *panel = seq_panel (f);
	g_autoptr(JsonNode)      r = NULL;

	*hero = create_node (f, "{'name':'Hero','primitive':'cube'}");
	*cam = create_node (f, "{'name':'CamA','kind':'camera'}");
	*path = g_build_filename (f->dir, "seq", "intro.rseq", NULL);

	r = call (f, "{'id':1,'method':'sequence.create','params':{'path':'%s','length':4,'fps':30}}", *path);
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "path"), ==, *path);
	g_assert_true (g_file_test (*path, G_FILE_TEST_EXISTS));
	json_node_unref (call (f, "{'id':1,'method':'sequence.add_track','params':{'kind':'transform','binding':'%s'}}",
	                       *hero));
	json_node_unref (call (f, "{'id':1,'method':'sequence.add_key','params':{'track':0,'channel':'location.x',"
	                          "'time':0,'value':0}}"));
	json_node_unref (call (f, "{'id':1,'method':'sequence.add_key','params':{'track':0,'channel':'location.x',"
	                          "'time':1,'value':4}}"));
	efx_frames (f, 2);
	return panel;
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	g_autofree gchar        *hero = NULL, *cam = NULL, *path = NULL;
	LrgEditorSequencerPanel *panel = setup_sequence (f, &hero, &cam, &path);
	LrgEditorViewState      *vs = lrg_editor_app_get_view_state (f->app);
	g_autoptr(JsonNode)      r = NULL;
	JsonObject              *o;
	JsonArray               *tracks, *keys;
	g_autoptr(LrgSequence)   reloaded = NULL;
	g_autoptr(GError)        error = NULL;
	gfloat                   x0 = node_x (f, hero);
	guint                    undo;

	/* Regression: create with settings re-reset the document with its own
	 * path (use after free); list / get / save must report the real path */
	r = call (f, "{'id':1,'method':'sequence.list'}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "open"), ==, path);
	json_node_unref (g_steal_pointer (&r));
	g_assert_cmpstr (lrg_editor_sequencer_panel_get_path (panel), ==, path);
	r = call (f, "{'id':1,'method':'sequence.save'}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "path"), ==, path);
	json_node_unref (g_steal_pointer (&r));

	/* Bindings: add_track named the node; bind adds the camera */
	r = call (f, "{'id':1,'method':'sequence.bind','params':{'node':'%s'}}", cam);
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "name"), ==, "CamA");
	json_node_unref (g_steal_pointer (&r));

	/* Broken tangents on the 1 s key, then a camera cut, an event */
	json_node_unref (call (f, "{'id':1,'method':'sequence.set_key','params':{'track':0,'channel':'location.x',"
	                          "'at':1,'tangent':'broken','in':1.5,'out':0}}"));
	r = call (f, "{'id':1,'method':'sequence.add_track','params':{'kind':'camera-cut'}}");
	g_assert_cmpint (result_int (r, "index"), ==, 1);
	json_node_unref (g_steal_pointer (&r));
	json_node_unref (call (f, "{'id':1,'method':'sequence.add_section','params':{'track':1,'start':0,"
	                          "'name':'CamA'}}"));
	json_node_unref (call (f, "{'id':1,'method':'sequence.add_track','params':{'kind':'event','name':'Cues'}}"));
	json_node_unref (call (f, "{'id':1,'method':'sequence.add_section','params':{'track':2,'start':0.5,"
	                          "'name':'door','payload':'slow'}}"));

	/* get: the whole document */
	r = call (f, "{'id':1,'method':'sequence.get'}");
	o = json_node_get_object (r);
	g_assert_cmpstr (json_object_get_string_member (o, "path"), ==, path);
	g_assert_true (json_object_get_boolean_member (o, "dirty"));
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (o, "length"), 4.0, EPS);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "bindings")), ==, 2);
	tracks = json_object_get_array_member (o, "tracks");
	g_assert_cmpuint (json_array_get_length (tracks), ==, 3);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (tracks, 0), "binding"), ==, hero);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (tracks, 0), "binding_name"), ==,
	                 "Hero");
	keys = json_object_get_array_member (json_object_get_object_member (json_array_get_object_element (tracks, 0),
	                                                                    "channels"), "location.x");
	g_assert_cmpuint (json_array_get_length (keys), ==, 2);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (keys, 1), "tangent"), ==, "broken");
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (json_array_get_object_element (keys, 1), "in"),
	                                1.5, EPS);
	/* the camera given by name became its GUID */
	g_assert_cmpstr (json_object_get_string_member (
	                     json_array_get_object_element (json_object_get_array_member (
	                         json_array_get_object_element (tracks, 1), "sections"), 0), "name"), ==, cam);
	json_node_unref (g_steal_pointer (&r));

	/* The panel shows it: rows, keys, the event key, a clip */
	g_assert_nonnull (lrg_editor_sequencer_panel_find_track (panel, 0, "location.x"));
	g_assert_cmpuint (lrg_editor_sequencer_panel_find_keyframe (panel, 0, "location.x", 1.0f), !=,
	                  LRG_TIMELINE_VIEW_INVALID_ID);
	g_assert_cmpuint (lrg_editor_sequencer_panel_find_keyframe (panel, 2, NULL, 0.5f), !=,
	                  LRG_TIMELINE_VIEW_INVALID_ID);
	g_assert_cmpuint (lrg_editor_sequencer_panel_find_clip (panel, 1, 0), !=, LRG_TIMELINE_VIEW_INVALID_ID);
	g_assert_cmpuint (lrg_timeline_view_get_n_markers (lrg_editor_sequencer_panel_get_view (panel)), ==, 1);

	/* Retiming a broken key keeps its handles */
	json_node_unref (call (f, "{'id':1,'method':'sequence.set_key','params':{'track':0,'channel':'location.x',"
	                          "'at':1,'time':1.2}}"));
	{
		LrgSequenceCurve *c = lrg_sequence_track_get_channel_curve (
			lrg_sequence_get_track (lrg_editor_sequencer_panel_get_sequence (panel), 0), LRG_SEQUENCE_CHANNEL_LOCATION_X);
		LrgSequenceKey    key;

		g_assert_true (lrg_sequence_curve_get_key (c, 1, &key));
		g_assert_cmpfloat_with_epsilon (key.time, 1.2f, EPS);
		g_assert_cmpint (key.tangent_mode, ==, LRG_SEQUENCE_TANGENT_BROKEN);
		g_assert_cmpfloat_with_epsilon (key.in_tangent, 1.5f, EPS);
	}

	/* Scrub: the node is posed outside the history, the event fires */
	undo = lrg_editor_get_undo_count (lrg_editor_app_get_editor (f->app));
	r = call (f, "{'id':1,'method':'sequence.scrub','params':{'time':1.2}}");
	o = json_node_get_object (r);
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (o, "time"), 1.2, EPS);
	g_assert_cmpint (json_object_get_int_member (o, "frame"), ==, 36);
	g_assert_cmpstr (json_object_get_string_member (o, "camera"), ==, cam);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "events")), ==, 1);
	g_assert_cmpstr (json_object_get_string_member (
	                     json_array_get_object_element (json_object_get_array_member (o, "events"), 0), "payload"), ==,
	                 "slow");
	json_node_unref (g_steal_pointer (&r));
	g_assert_cmpfloat_with_epsilon (node_x (f, hero), 4.0f, EPS);
	g_assert_true (lrg_editor_view_state_get_timeline_previewing (vs));
	g_assert_cmpuint (lrg_editor_get_undo_count (lrg_editor_app_get_editor (f->app)), ==, undo);

	/* Frame scrub, then stop: the node is back */
	json_node_unref (call (f, "{'id':1,'method':'sequence.scrub','params':{'frame':15}}"));
	g_assert_cmpfloat (node_x (f, hero), >, 0.1f);
	g_assert_cmpfloat (node_x (f, hero), <, 4.0f);
	r = call (f, "{'id':1,'method':'sequence.play','params':{'stop':true}}");
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (json_node_get_object (r), "time"), 0.0, EPS);
	json_node_unref (g_steal_pointer (&r));
	g_assert_cmpfloat_with_epsilon (node_x (f, hero), x0, EPS);
	g_assert_false (lrg_editor_view_state_get_timeline_previewing (vs));

	/* Play in loop mode runs over frames, then pause */
	r = call (f, "{'id':1,'method':'sequence.play','params':{'mode':'loop'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "playing"));
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "mode"), ==, "loop");
	json_node_unref (g_steal_pointer (&r));
	efx_frames (f, 30);
	g_assert_cmpfloat (lrg_editor_sequencer_panel_get_time (panel), >, 0.3f);
	g_assert_cmpfloat (node_x (f, hero), >, 0.5f);
	json_node_unref (call (f, "{'id':1,'method':'sequence.play','params':{'playing':false}}"));
	g_assert_false (lrg_editor_sequencer_panel_get_playing (panel));

	/* Undo / redo inside the panel */
	r = call (f, "{'id':1,'method':'sequence.remove','params':{'track':2}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "removed"));
	json_node_unref (g_steal_pointer (&r));
	g_assert_cmpuint (lrg_sequence_get_n_tracks (lrg_editor_sequencer_panel_get_sequence (panel)), ==, 2);
	json_node_unref (call (f, "{'id':1,'method':'sequence.undo'}"));
	g_assert_cmpuint (lrg_sequence_get_n_tracks (lrg_editor_sequencer_panel_get_sequence (panel)), ==, 3);
	json_node_unref (call (f, "{'id':1,'method':'sequence.undo','params':{'redo':true}}"));
	g_assert_cmpuint (lrg_sequence_get_n_tracks (lrg_editor_sequencer_panel_get_sequence (panel)), ==, 2);

	/* Bad params are errors, not crashes */
	{
		g_autofree gchar *reply = efx_ipc (f, "{'id':1,'method':'sequence.add_track','params':{'kind':'bogus'}}");

		g_assert_nonnull (strstr (reply, "\"error\""));
		g_free (reply);
		reply = efx_ipc (f, "{'id':1,'method':'sequence.add_key','params':{'track':9,'time':0}}");
		g_assert_nonnull (strstr (reply, "\"error\""));
		g_free (reply);
		reply = efx_ipc (f, "{'id':1,'method':'sequence.add_section','params':{'track':0,'start':0,'name':'x'}}");
		g_assert_nonnull (strstr (reply, "\"error\""));
	}

	/* Save: clean, and the file reads back to the same sequence */
	r = call (f, "{'id':1,'method':'sequence.save'}");
	json_node_unref (g_steal_pointer (&r));
	g_assert_false (lrg_editor_sequencer_panel_get_dirty (panel));
	reloaded = lrg_sequence_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_sequence_equal (reloaded, lrg_editor_sequencer_panel_get_sequence (panel)));

	/* create refuses an existing file */
	{
		g_autofree gchar *req = g_strdup_printf ("{'id':1,'method':'sequence.create','params':{'path':'%s'}}", path);
		g_autofree gchar *reply = efx_ipc (f, req);

		g_assert_nonnull (strstr (reply, "\"error\""));
	}
}

/* ==========================================================================
 * Canvas
 * ========================================================================== */

static void
test_canvas (EditorAppFixture *f,
             gconstpointer     data)
{
	g_autofree gchar        *hero = NULL, *cam = NULL, *path = NULL;
	LrgEditorSequencerPanel *panel = setup_sequence (f, &hero, &cam, &path);
	LrgTimelineView         *view = lrg_editor_sequencer_panel_get_view (panel);
	LrgSequenceCurve        *curve;
	LrgSequenceKey           key;
	gfloat                   x, y, x2, y2, in_s, out_s, rx, ry, rw, rh;
	guint                    n;

	/* 1. Curve editor: drag key 0's out handle up -> a user key with a
	 * positive slope, one undo step */
	g_assert_true (lrg_editor_sequencer_panel_select_curve (panel, 0, "location.x"));
	efx_frames (f, 2);
	g_assert_true (lrg_editor_sequencer_panel_get_curve_point (panel, 0, 0, &x, &y));
	efx_click (f, x, y);         /* select key 0: its handles show */
	g_assert_true (lrg_editor_sequencer_panel_get_curve_point (panel, 0, 1, &x, &y));
	g_assert_false (lrg_editor_sequencer_panel_get_curve_point (panel, 0, -1, NULL, NULL));
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x, y - 40.0f, 6);
	efx_frame (f);
	curve = lrg_sequence_track_get_channel_curve (lrg_sequence_get_track (lrg_editor_sequencer_panel_get_sequence (panel),
	                                                                      0), LRG_SEQUENCE_CHANNEL_LOCATION_X);
	g_assert_true (lrg_sequence_curve_get_key (curve, 0, &key));
	g_assert_cmpint (key.tangent_mode, ==, LRG_SEQUENCE_TANGENT_USER);
	lrg_sequence_curve_get_slopes (curve, 0, &in_s, &out_s);
	g_assert_cmpfloat (out_s, >, 1.0f);
	g_assert_true (lrg_editor_sequencer_panel_get_dirty (panel));
	g_assert_true (lrg_editor_sequencer_panel_undo (panel));
	curve = lrg_sequence_track_get_channel_curve (lrg_sequence_get_track (lrg_editor_sequencer_panel_get_sequence (panel),
	                                                                      0), LRG_SEQUENCE_CHANNEL_LOCATION_X);
	g_assert_true (lrg_sequence_curve_get_key (curve, 0, &key));
	g_assert_cmpint (key.tangent_mode, ==, LRG_SEQUENCE_TANGENT_AUTO);
	g_assert_true (lrg_editor_sequencer_panel_redo (panel));
	efx_frames (f, 2);

	/* 2. The key fields: switch key 1 to constant through the picker */
	g_assert_true (lrg_editor_sequencer_panel_get_curve_point (panel, 1, 0, &x, &y));
	efx_click (f, x, y);
	lrg_combo_box_set_selected (LRG_COMBO_BOX (find (f, "sequencer-key-tangent")), LRG_SEQUENCE_TANGENT_CONSTANT);
	efx_frame (f);
	curve = lrg_sequence_track_get_channel_curve (lrg_sequence_get_track (lrg_editor_sequencer_panel_get_sequence (panel),
	                                                                      0), LRG_SEQUENCE_CHANNEL_LOCATION_X);
	g_assert_true (lrg_sequence_curve_get_key (curve, 1, &key));
	g_assert_cmpint (key.tangent_mode, ==, LRG_SEQUENCE_TANGENT_CONSTANT);

	/* 3. Double click on empty curve space adds a key on the curve */
	n = lrg_sequence_curve_get_n_keys (curve);
	g_assert_true (lrg_editor_sequencer_panel_get_curve_point (panel, 1, 0, &x, &y));
	efx_click (f, x + 60.0f, y);
	efx_click (f, x + 60.0f, y);
	efx_frame (f);
	curve = lrg_sequence_track_get_channel_curve (lrg_sequence_get_track (lrg_editor_sequencer_panel_get_sequence (panel),
	                                                                      0), LRG_SEQUENCE_CHANNEL_LOCATION_X);
	g_assert_cmpuint (lrg_sequence_curve_get_n_keys (curve), ==, n + 1);
	/* Delete removes it again (the curve has focus) */
	efx_key (f, GRL_KEY_DELETE, LRG_INPUT_MODIFIER_NONE);
	efx_frame (f);
	curve = lrg_sequence_track_get_channel_curve (lrg_sequence_get_track (lrg_editor_sequencer_panel_get_sequence (panel),
	                                                                      0), LRG_SEQUENCE_CHANNEL_LOCATION_X);
	g_assert_cmpuint (lrg_sequence_curve_get_n_keys (curve), ==, n);

	/* 4. Drag the 1 s key in the view to 2 s: snapped to frames, keeps
	 * its constant tangent, one undo step */
	view_key_point (panel, 0, "location.x", 1.0f, &x, &y);
	x2 = lrg_timeline_view_time_to_x (view, 2.004);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x2, y, 6);
	efx_frames (f, 2);
	curve = lrg_sequence_track_get_channel_curve (lrg_sequence_get_track (lrg_editor_sequencer_panel_get_sequence (panel),
	                                                                      0), LRG_SEQUENCE_CHANNEL_LOCATION_X);
	g_assert_cmpint (lrg_sequence_curve_find_key (curve, 2.0f, 0.001f), ==, 1);
	g_assert_true (lrg_sequence_curve_get_key (curve, 1, &key));
	g_assert_cmpint (key.tangent_mode, ==, LRG_SEQUENCE_TANGENT_CONSTANT);
	g_assert_cmpfloat_with_epsilon (key.value, 4.0f, EPS);

	/* 5. Add Track menu: an event track, keyed at the playhead */
	efx_click_widget (f, find (f, "sequencer-add-track"));
	{
		LrgWidget *top = lrg_canvas_get_top_popup (lrg_editor_app_get_canvas (f->app));
		LrgMenu   *menu;
		guint      i;
		gboolean   done = FALSE;

		g_assert_true (LRG_IS_MENU (top));
		menu = LRG_MENU (top);
		for (i = 0; i < lrg_menu_get_n_entries (menu) && !done; i++)
			if (g_strcmp0 (lrg_menu_get_entry_label (menu, i), "Event") == 0)
				done = lrg_menu_activate_entry (menu, i);
		g_assert_true (done);
	}
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_sequence_get_n_tracks (lrg_editor_sequencer_panel_get_sequence (panel)), ==, 2);
	lrg_editor_sequencer_panel_scrub (panel, 0.5f);
	efx_click_widget (f, find (f, "sequencer-key"));
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_sequence_track_get_n_sections (
	                      lrg_sequence_get_track (lrg_editor_sequencer_panel_get_sequence (panel), 1)), ==, 1);
	g_assert_cmpuint (lrg_timeline_view_get_n_markers (view), ==, 1);

	/* 6. Click the ruler: scrub (snapped to a frame), the node follows */
	efx_rect_of (LRG_WIDGET (view), &rx, &ry, &rw, &rh);
	x = lrg_timeline_view_time_to_x (view, 1.0);
	efx_click (f, x, ry + lrg_timeline_view_get_ruler_height (view) * 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_editor_sequencer_panel_get_time (panel), 1.0f, 0.04f);
	g_assert_cmpfloat_with_epsilon (fmodf (lrg_editor_sequencer_panel_get_time (panel) * 30.0f, 1.0f), 0.0f, 1e-3);
	g_assert_cmpfloat (node_x (f, hero), >, 0.5f);

	/* 7. Ping-pong playback turns at the end and keeps playing */
	lrg_combo_box_set_selected (LRG_COMBO_BOX (find (f, "sequencer-mode")), LRG_SEQUENCE_PLAY_PING_PONG);
	efx_frame (f);
	g_assert_cmpint (lrg_editor_sequencer_panel_get_preview_mode (panel), ==, LRG_SEQUENCE_PLAY_PING_PONG);
	lrg_editor_sequencer_panel_scrub (panel, 3.9f);
	efx_click_widget (f, find (f, "sequencer-play"));
	g_assert_true (lrg_editor_sequencer_panel_get_playing (panel));
	efx_frames (f, 30);
	g_assert_true (lrg_editor_sequencer_panel_get_playing (panel));
	g_assert_cmpint (lrg_sequence_player_get_direction (lrg_editor_sequencer_panel_get_player (panel)), <, 0);
	g_assert_cmpfloat (lrg_editor_sequencer_panel_get_time (panel), <, 3.9f);
	efx_click_widget (f, find (f, "sequencer-stop"));
	g_assert_false (lrg_editor_sequencer_panel_get_playing (panel));
	g_assert_cmpfloat_with_epsilon (lrg_editor_sequencer_panel_get_time (panel), 0.0f, EPS);

	/* 8. Pilot: the viewport looks through the cut camera, then gets its
	 * own camera back */
	{
		LrgEditorViewState  *vs = lrg_editor_app_get_view_state (f->app);
		LrgEditorViewCamera  before = *lrg_editor_view_state_get_camera (vs, -1);
		g_autoptr(JsonNode)  r = NULL;
		LrgNode             *camnode = lrg_editor_find_node (lrg_editor_app_get_editor (f->app), cam);

		lrg_node_set_location_xyz (camnode, 3.0f, 2.0f, 9.0f);
		json_node_unref (call (f, "{'id':1,'method':'sequence.add_track','params':{'kind':'camera-cut'}}"));
		json_node_unref (call (f, "{'id':1,'method':'sequence.add_section','params':{'track':2,'start':0,"
		                          "'name':'%s'}}", cam));
		efx_click_widget (f, find (f, "sequencer-pilot"));
		g_assert_true (lrg_editor_sequencer_panel_get_pilot (panel));
		r = call (f, "{'id':1,'method':'sequence.scrub','params':{'time':0.5}}");
		json_node_unref (g_steal_pointer (&r));
		g_assert_cmpfloat_with_epsilon (lrg_editor_view_state_get_camera (vs, -1)->view.position[0], 3.0f, EPS);
		g_assert_cmpfloat_with_epsilon (lrg_editor_view_state_get_camera (vs, -1)->view.position[2], 9.0f, EPS);
		efx_click_widget (f, find (f, "sequencer-stop"));
		g_assert_true (lrg_editor_view_camera_equal (lrg_editor_view_state_get_camera (vs, -1), &before));
	}

	/* 9. Delete: select the event and press Delete in the toolbar */
	{
		guint id = lrg_editor_sequencer_panel_find_keyframe (panel, 1, NULL, 0.5f);

		g_assert_cmpuint (id, !=, LRG_TIMELINE_VIEW_INVALID_ID);
		lrg_timeline_view_unselect_all (view);
		lrg_timeline_view_select_keyframe (view, id, FALSE);
		efx_click_widget (f, find (f, "sequencer-delete"));
		efx_frames (f, 2);
		g_assert_cmpuint (lrg_sequence_track_get_n_sections (
		                      lrg_sequence_get_track (lrg_editor_sequencer_panel_get_sequence (panel), 1)), ==, 0);
	}

	/* 10. Snap off: times keep milliseconds */
	efx_click_widget (f, find (f, "sequencer-snap"));
	g_assert_false (lrg_editor_sequencer_panel_get_snap (panel));
	lrg_editor_sequencer_panel_scrub (panel, 1.0123f);
	g_assert_cmpfloat_with_epsilon (lrg_editor_sequencer_panel_get_time (panel), 1.012f, 1e-4);
	(void) y2;
}

/* ==========================================================================
 * Files
 * ========================================================================== */

static void
test_files (EditorAppFixture *f,
            gconstpointer     data)
{
	LrgEditorSequencerPanel *panel = seq_panel (f);
	g_autofree gchar        *guid = create_node (f, "{'name':'Mover','primitive':'cube'}");
	g_autofree gchar        *dest = g_build_filename (f->dir, "conv", "bounce.rseq", NULL);
	g_autoptr(LrgSequence)   seq = lrg_sequence_new ();
	g_autofree gchar        *file = g_build_filename (f->dir, "plain.rseq", NULL);
	g_autoptr(GError)        error = NULL;
	g_autoptr(JsonNode)      r = NULL;

	/* open_file: what double clicking a .rseq asset does */
	lrg_sequence_set_name (seq, "plain");
	lrg_sequence_add_track (seq, lrg_sequence_track_new (LRG_SEQUENCE_TRACK_EVENT, "Cues"));
	g_object_unref (lrg_sequence_get_track (seq, 0));   /* add_track took its own ref */
	g_assert_true (lrg_sequence_save_to_file (seq, file, &error));
	g_assert_true (lrg_editor_sequencer_panel_open_file (f->app, file, &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_editor_sequencer_panel_get_path (panel), ==, file);
	g_assert_false (lrg_editor_sequencer_panel_get_dirty (panel));
	efx_frames (f, 2);
	g_assert_false (lrg_widget_get_visible (find (f, "sequencer-empty")));
	g_assert_false (lrg_editor_sequencer_panel_open_file (f->app, "/nope/missing.rseq", &error));
	g_assert_nonnull (error);
	g_clear_error (&error);

	/* Convert a level timeline */
	json_node_unref (call (f, "{'id':1,'method':'timeline.key','params':{'name':'Bounce','node':'%s','time':0,"
	                          "'channel':'location.y','value':0}}", guid));
	json_node_unref (call (f, "{'id':1,'method':'timeline.key','params':{'name':'Bounce','node':'%s','time':1,"
	                          "'channel':'location.y','value':2}}", guid));
	r = call (f, "{'id':1,'method':'sequence.from_timeline','params':{'timeline':'Bounce','path':'%s'}}", dest);
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "path"), ==, dest);
	g_assert_true (g_file_test (dest, G_FILE_TEST_EXISTS));
	g_assert_cmpstr (lrg_editor_sequencer_panel_get_path (panel), ==, dest);
	g_assert_cmpuint (lrg_sequence_get_n_tracks (lrg_editor_sequencer_panel_get_sequence (panel)), ==, 1);
	g_assert_cmpstr (lrg_sequence_get_binding_name (lrg_editor_sequencer_panel_get_sequence (panel), guid), ==,
	                 "Mover");
	{
		g_autofree gchar *reply = efx_ipc (f, "{'id':1,'method':'sequence.from_timeline','params':"
		                                      "{'timeline':'Nope','path':'x.rseq'}}");

		g_assert_nonnull (strstr (reply, "not-found"));
	}
}

/* ==========================================================================
 * Level timeline polish
 * ========================================================================== */

static void
test_timeline_panel (EditorAppFixture *f,
                     gconstpointer     data)
{
	LrgDockArea            *dock = lrg_editor_app_get_dock (f->app);
	LrgEditor              *editor = lrg_editor_app_get_editor (f->app);
	LrgEditorTimelinePanel *panel;
	LrgTimelineView        *view;
	g_autofree gchar       *guid = create_node (f, "{'name':'Mover','primitive':'cube'}");
	g_autoptr(JsonNode)     r = NULL;
	LrgTimelineTrack       *track;
	LrgSequenceTangentMode  mode;
	gfloat                  in_s = 0.0f, out_s = 0.0f, x, y, x2, ty, th;
	LrgTimeline            *tl;
	guint                   id;

	lrg_dock_area_present_panel (dock, lrg_dock_area_get_panel (dock, "timeline"));
	efx_frames (f, 2);
	panel = LRG_EDITOR_TIMELINE_PANEL (lrg_dock_panel_get_child (lrg_dock_area_get_panel (dock, "timeline")));
	view = lrg_editor_timeline_panel_get_view (panel);

	json_node_unref (call (f, "{'id':1,'method':'timeline.key','params':{'name':'T','node':'%s','time':0,"
	                          "'channel':'location.x','value':0}}", guid));
	json_node_unref (call (f, "{'id':1,'method':'timeline.key','params':{'name':'T','node':'%s','time':1,"
	                          "'channel':'location.x','value':2}}", guid));
	efx_frames (f, 2);

	/* Tangent picker on the selected 1 s key: Broken keeps the drawn slopes */
	id = lrg_editor_timeline_panel_find_keyframe (panel, guid, "location.x", 1.0f);
	g_assert_cmpuint (id, !=, LRG_EDITOR_TIMELINE_PANEL_INVALID_KEY);
	lrg_timeline_view_unselect_all (view);
	lrg_timeline_view_select_keyframe (view, id, FALSE);
	efx_frame (f);
	g_assert_cmpint (lrg_combo_box_get_selected (LRG_COMBO_BOX (find (f, "timeline-tangent"))), ==, 0);
	lrg_combo_box_set_selected (LRG_COMBO_BOX (find (f, "timeline-tangent")), 1 + LRG_SEQUENCE_TANGENT_BROKEN);
	efx_frames (f, 2);
	tl = lrg_level_get_timeline (lrg_editor_get_level (editor), 0);
	track = lrg_timeline_get_track (tl, 0);
	g_assert_true (lrg_timeline_track_get_key_tangents (track, 1, &mode, &in_s, &out_s));
	g_assert_cmpint (mode, ==, LRG_SEQUENCE_TANGENT_BROKEN);
	/* the secant slope a linear neighbour sees: 2 per second */
	g_assert_cmpfloat_with_epsilon (in_s, 2.0f, EPS);

	/* IPC timeline.tangent: user with an explicit slope */
	r = call (f, "{'id':1,'method':'timeline.tangent','params':{'node':'%s','time':1,'tangent':'user','slope':0.5}}",
	          guid);
	g_assert_cmpint (result_int (r, "changed"), ==, 1);
	json_node_unref (g_steal_pointer (&r));
	tl = lrg_level_get_timeline (lrg_editor_get_level (editor), 0);
	track = lrg_timeline_get_track (tl, 0);
	g_assert_true (lrg_timeline_track_get_key_tangents (track, 1, &mode, &in_s, NULL));
	g_assert_cmpint (mode, ==, LRG_SEQUENCE_TANGENT_USER);
	g_assert_cmpfloat_with_epsilon (in_s, 0.5f, EPS);

	/* Dragging the Bézier key keeps its handles */
	efx_frames (f, 2);
	g_assert_true (lrg_timeline_view_get_track_row (view, lrg_editor_timeline_panel_find_track (panel, guid,
	                                                                                            "location.x"),
	                                                &ty, &th));
	x = lrg_timeline_view_time_to_x (view, 1.0);
	y = ty + th * 0.5f;
	x2 = lrg_timeline_view_time_to_x (view, 1.5);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x2, y, 6);
	efx_frames (f, 2);
	tl = lrg_level_get_timeline (lrg_editor_get_level (editor), 0);
	track = lrg_timeline_get_track (tl, 0);
	{
		gint k = lrg_timeline_track_find_key (track, 1.5f, 0.02f);

		g_assert_cmpint (k, >=, 0);
		g_assert_true (lrg_timeline_track_get_key_tangents (track, (guint) k, &mode, &in_s, NULL));
		g_assert_cmpint (mode, ==, LRG_SEQUENCE_TANGENT_USER);
		g_assert_cmpfloat_with_epsilon (in_s, 0.5f, EPS);
	}

	/* "Eased" through IPC makes it an eased key again */
	r = call (f, "{'id':1,'method':'timeline.tangent','params':{'node':'%s','time':1.5,'tangent':'eased'}}", guid);
	json_node_unref (g_steal_pointer (&r));
	tl = lrg_level_get_timeline (lrg_editor_get_level (editor), 0);
	g_assert_false (lrg_timeline_track_get_key_tangents (lrg_timeline_get_track (tl, 0), 1, NULL, NULL, NULL));

	/* Key Bone: nine offset channels of "head" on the selection */
	select_guid (f, guid);
	lrg_text_input_set_text (LRG_TEXT_INPUT (find (f, "timeline-bone")), "head");
	{
		gfloat bx, by, bw, bh, px, py, pw, ph;

		/* the chip is inside the panel (the toolbars fit) */
		efx_rect_of (find (f, "timeline-key-bone"), &bx, &by, &bw, &bh);
		efx_rect_of (LRG_WIDGET (panel), &px, &py, &pw, &ph);
		g_assert_cmpfloat (bx + bw, <=, px + pw);
	}
	efx_click_widget (f, find (f, "timeline-key-bone"));
	efx_frames (f, 2);
	tl = lrg_level_get_timeline (lrg_editor_get_level (editor), 0);
	g_assert_cmpint (lrg_timeline_find_track (tl, guid, "bone.head.rotation.y"), >=, 0);
	g_assert_nonnull (lrg_editor_timeline_panel_find_track (panel, guid, "bone.head.rotation.y"));
	g_assert_cmpstr (lrg_timeline_view_track_get_name (
	                     lrg_editor_timeline_panel_find_track (panel, guid, "bone.head.rotation.y")), ==,
	                 "head  rotation.y");

	/* Curves chip: channel rows become curve lanes */
	efx_click_widget (f, find (f, "timeline-curves"));
	efx_frames (f, 2);
	g_assert_true (lrg_timeline_view_track_get_curve_lane (lrg_editor_timeline_panel_find_track (panel, guid,
	                                                                                             "location.x")));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#define ADD(path, fn) g_test_add (path, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("/editor-app/sequencer/ipc", test_ipc);
	ADD ("/editor-app/sequencer/canvas", test_canvas);
	ADD ("/editor-app/sequencer/files", test_files);
	ADD ("/editor-app/sequencer/timeline-panel", test_timeline_panel);
#undef ADD

	return g_test_run ();
}
