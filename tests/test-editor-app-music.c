/* test-editor-app-music.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Music and sound creation in lrgeditor (0.10.0 M7-M9): the Foley
 * designer (.rfoley) and the song editor (.rsong) through their IPC
 * methods (foley.*, song.*, audio.formats) and the canvas (arrangement
 * clicks, step grid, piano roll), undo, renders, exports and the
 * headless live preview.  No audio device is used.
 */

#include "editor-app-fixture.h"
#include <math.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
music_project (EditorAppFixture *f)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Music", &error));
	g_assert_no_error (error);
	lrg_editor_foley_editor_install (f->app);
	lrg_editor_song_editor_install (f->app);
}

/* Brings a data editor's tab over the viewport and lays it out. */
static void
present (EditorAppFixture    *f,
         LrgEditorDataEditor *ed)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *p = lrg_dock_area_get_panel (dock, lrg_editor_data_editor_get_panel_id (ed));
	LrgDockPanel *vp = lrg_dock_area_get_panel (dock, "viewport");

	g_assert_nonnull (p);
	if (vp != NULL)
		lrg_dock_area_move_panel (dock, p, vp, LRG_DOCK_POSITION_CENTER);
	lrg_dock_area_present_panel (dock, p);
	efx_frames (f, 3);
}

static JsonObject *
obj_of (JsonNode *n)
{
	g_assert_true (JSON_NODE_HOLDS_OBJECT (n));
	return json_node_get_object (n);
}

static gchar *
project_file (EditorAppFixture *f,
              const gchar      *rel)
{
	return g_build_filename (f->project, rel, NULL);
}

/* ==========================================================================
 * Foley
 * ========================================================================== */

static void
test_foley_ipc (EditorAppFixture *f,
                gconstpointer     data)
{
	g_autoptr(JsonNode)    r = NULL;
	g_autofree gchar      *wav = NULL;
	LrgEditorFoleyEditor  *ed;
	guint                  layers;

	music_project (f);
	r = efx_ipc_result (f, "{'id':1,'method':'foley.new','params':{'path':'sfx/step.rfoley','kind':'footstep-stone'}}");
	g_clear_pointer (&r, json_node_unref);
	ed = LRG_EDITOR_FOLEY_EDITOR (lrg_editor_data_editor_find (f->app, NULL));
	g_assert_true (LRG_IS_EDITOR_FOLEY_EDITOR (ed));
	efx_frames (f, 2);

	r = efx_ipc_result (f, "{'id':2,'method':'foley.get'}");
	layers = json_array_get_length (json_object_get_array_member (obj_of (r), "layers"));
	g_assert_cmpuint (layers, >, 0);
	g_clear_pointer (&r, json_node_unref);

	/* a tone layer with parameters: one undo step */
	r = efx_ipc_result (f, "{'id':3,'method':'foley.add_layer','params':{'type':'tone','params':{'freq':440,"
	                       "'freq-end':220}}}");
	g_assert_cmpint (json_object_get_int_member (obj_of (r), "index"), ==, (gint64) layers);
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpuint (lrg_foley_patch_get_n_layers (lrg_editor_foley_editor_get_patch (ed)), ==, layers + 1);
	g_assert_cmpfloat_with_epsilon (lrg_foley_patch_get_layer_param (lrg_editor_foley_editor_get_patch (ed), layers,
	                                                                 "freq"), 440.0, 1e-9);
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_cmpuint (lrg_foley_patch_get_n_layers (lrg_editor_foley_editor_get_patch (ed)), ==, layers);

	/* a bad parameter changes nothing */
	g_free (efx_ipc (f, "{'id':4,'method':'foley.set_layer','params':{'index':0,'params':{'nope':1}}}"));
	g_assert_cmpuint (lrg_foley_patch_get_n_layers (lrg_editor_foley_editor_get_patch (ed)), ==, layers);

	/* render: an analysis with sound in it */
	r = efx_ipc_result (f, "{'id':5,'method':'foley.render','params':{'bins':16}}");
	g_assert_cmpfloat (json_object_get_double_member (obj_of (r), "peak_db"), >, -60.0);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (obj_of (r), "envelope")), ==, 16);
	g_clear_pointer (&r, json_node_unref);

	/* export */
	r = efx_ipc_result (f, "{'id':6,'method':'foley.export','params':{'file':'sfx/step.wav'}}");
	g_clear_pointer (&r, json_node_unref);
	wav = project_file (f, "sfx/step.wav");
	g_assert_true (g_file_test (wav, G_FILE_TEST_IS_REGULAR));

	/* the catalogue */
	r = efx_ipc_result (f, "{'id':7,'method':'foley.layer_types'}");
	g_assert_true (json_object_has_member (json_object_get_object_member (obj_of (r), "layer_types"), "grain"));
	g_clear_pointer (&r, json_node_unref);

	/* the designer shows its views */
	present (f, LRG_EDITOR_DATA_EDITOR (ed));
	g_assert_nonnull (efx_find (f, "foley-waveform"));
	g_assert_nonnull (efx_find (f, "foley-layer-list"));
}

/* the layer list: a click selects, the trash button removes */
static void
test_foley_canvas (EditorAppFixture *f,
                   gconstpointer     data)
{
	g_autoptr(JsonNode)   r = NULL;
	LrgEditorFoleyEditor *ed;
	LrgWidget            *list;
	gfloat                x, y, w, h;
	guint                 n;

	music_project (f);
	r = efx_ipc_result (f, "{'id':1,'method':'foley.new','params':{'path':'sfx/hit.rfoley','kind':'footstep-water'}}");
	ed = LRG_EDITOR_FOLEY_EDITOR (lrg_editor_data_editor_find (f->app, NULL));
	present (f, LRG_EDITOR_DATA_EDITOR (ed));
	list = efx_find (f, "foley-layer-list");
	g_assert_nonnull (list);
	efx_rect_of (list, &x, &y, &w, &h);
	n = lrg_foley_patch_get_n_layers (lrg_editor_foley_editor_get_patch (ed));
	g_assert_cmpuint (n, >=, 2);

	efx_click (f, x + 20.0f, y + 1.0f + 24.0f * 1.5f);
	g_assert_cmpint (lrg_editor_foley_editor_get_selected_layer (ed), ==, 1);
	efx_click (f, x + w - 12.0f, y + 1.0f + 24.0f * 0.5f);
	g_assert_cmpuint (lrg_foley_patch_get_n_layers (lrg_editor_foley_editor_get_patch (ed)), ==, n - 1);
}

/* ==========================================================================
 * Songs
 * ========================================================================== */

static LrgEditorSongEditor *
new_song (EditorAppFixture *f,
          const gchar      *path)
{
	g_autofree gchar    *req = g_strdup_printf ("{'id':1,'method':'song.new','params':{'path':'%s'}}", path);
	g_autoptr(JsonNode)  r = efx_ipc_result (f, req);
	LrgEditorSongEditor *ed = LRG_EDITOR_SONG_EDITOR (lrg_editor_data_editor_find (f->app, NULL));

	g_assert_true (LRG_IS_EDITOR_SONG_EDITOR (ed));
	g_assert_nonnull (lrg_editor_song_editor_get_song (ed));
	efx_frames (f, 2);
	return ed;
}

static guint
track_count (LrgEditorSongEditor *ed)
{
	g_auto(GStrv) ids = lrg_song_list_tracks (lrg_editor_song_editor_get_song (ed));

	return g_strv_length (ids);
}

static void
test_song_ipc (EditorAppFixture *f,
               gconstpointer     data)
{
	g_autoptr(JsonNode)  r = NULL;
	LrgEditorSongEditor *ed;
	JsonArray           *tracks;

	music_project (f);
	ed = new_song (f, "music/loop.rsong");

	/* the starter: drums and bass */
	r = efx_ipc_result (f, "{'id':2,'method':'song.get','params':{'section':'tracks'}}");
	tracks = json_object_get_array_member (obj_of (r), "tracks");
	g_assert_cmpuint (json_array_get_length (tracks), ==, 2);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (tracks, 0), "id"), ==, "drums");
	g_clear_pointer (&r, json_node_unref);

	/* one op: a marker */
	r = efx_ipc_result (f, "{'id':3,'method':'song.set_marker','params':{'name':'Verse','beat':8}}");
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpfloat (lrg_song_find_marker (lrg_editor_song_editor_get_song (ed), "Verse"), ==, 8.0);

	/* a batch is one undo step */
	r = efx_ipc_result (f, "{'id':4,'method':'song.apply','params':{'ops':["
	                       "{'op':'set_instrument','id':'lead','type':'synth','source':'synthwave-lead',"
	                       "'params':{'volume':0.5}},"
	                       "{'op':'add_pattern','id':'hook','length':4,'notes':[[0,'A4',1,0.9],'1 C5 1 0.8',[2,76,2]]},"
	                       "{'op':'add_track','id':'lead','instrument':'lead','volume':-6},"
	                       "{'op':'add_clip','track':'lead','source':'hook','start':4,'length':8}]}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (obj_of (r), "results")), ==, 4);
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpuint (track_count (ed), ==, 3);
	g_assert_cmpuint (lrg_song_pattern_get_n_notes (lrg_editor_song_editor_get_song (ed), "hook"), ==, 3);
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_cmpuint (track_count (ed), ==, 2);
	g_assert_true (lrg_editor_data_editor_redo (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_cmpuint (track_count (ed), ==, 3);

	/* a failing batch changes nothing */
	g_free (efx_ipc (f, "{'id':5,'method':'song.apply','params':{'ops':[{'op':'remove_track','id':'lead'},"
	                    "{'op':'remove_track','id':'nope'}]}}"));
	g_assert_cmpuint (track_count (ed), ==, 3);

	/* mixer: a reverb bus, a send, a duck */
	r = efx_ipc_result (f, "{'id':6,'method':'song.apply','params':{'ops':["
	                       "{'op':'set_bus','name':'verb'},"
	                       "{'op':'set_effect','bus':'verb','type':'reverb','id':'hall'},"
	                       "{'op':'set_bus','name':'synths'},{'op':'set_send','source':'synths','target':'verb','level':-12},"
	                       "{'op':'set_track','id':'bass','bus':'synths'},"
	                       "{'op':'set_bus','name':'kick'},{'op':'set_track','id':'drums','bus':'kick'},"
	                       "{'op':'set_duck','trigger':'kick','target':'synths','depth':-4}]}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':7,'method':'song.get','params':{'section':'mixer'}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (
	                      json_object_get_object_member (obj_of (r), "mixer"), "buses")), >=, 2);
	g_clear_pointer (&r, json_node_unref);

	r = efx_ipc_result (f, "{'id':8,'method':'song.validate'}");
	g_assert_true (json_object_get_boolean_member (obj_of (r), "valid"));
	g_clear_pointer (&r, json_node_unref);

	/* discovery */
	r = efx_ipc_result (f, "{'id':9,'method':'song.instruments'}");
	g_assert_true (json_object_has_member (json_object_get_object_member (obj_of (r), "instrument_types"), "synth"));
	g_assert_true (json_object_has_member (json_object_get_object_member (obj_of (r), "effects"), "compressor"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':10,'method':'audio.formats'}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (obj_of (r), "formats")), >=, 9);
	g_clear_pointer (&r, json_node_unref);
}

static void
test_song_render_export (EditorAppFixture *f,
                         gconstpointer     data)
{
	g_autoptr(JsonNode)  r = NULL;
	g_autofree gchar    *wav = NULL;
	g_autofree gchar    *mid = NULL;
	g_autofree gchar    *stem = NULL;
	LrgEditorSongEditor *ed;
	JsonObject          *tracks;

	music_project (f);
	ed = new_song (f, "music/loop.rsong");

	/* four bars, analysed with each track alone */
	r = efx_ipc_result (f, "{'id':2,'method':'song.render','params':{'end':16,'tracks':true,'bins':8}}");
	g_assert_cmpfloat (json_object_get_double_member (obj_of (r), "peak_db"), >, -40.0);
	tracks = json_object_get_object_member (obj_of (r), "tracks");
	g_assert_true (json_object_has_member (tracks, "drums"));
	g_assert_true (json_object_has_member (tracks, "bass"));
	g_clear_pointer (&r, json_node_unref);

	/* export: normalised WAV and stems */
	r = efx_ipc_result (f, "{'id':3,'method':'song.export','params':{'file':'out/loop.wav','end':8,'stems':true}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (obj_of (r), "files")), ==, 3);
	g_clear_pointer (&r, json_node_unref);
	wav = project_file (f, "out/loop.wav");
	stem = project_file (f, "out/loop.bass.wav");
	g_assert_true (g_file_test (wav, G_FILE_TEST_IS_REGULAR));
	g_assert_true (g_file_test (stem, G_FILE_TEST_IS_REGULAR));

	/* MIDI out and back in as a new song */
	r = efx_ipc_result (f, "{'id':4,'method':'song.export_midi','params':{'file':'out/loop.mid'}}");
	g_clear_pointer (&r, json_node_unref);
	mid = project_file (f, "out/loop.mid");
	g_assert_true (g_file_test (mid, G_FILE_TEST_IS_REGULAR));
	r = efx_ipc_result (f, "{'id':5,'method':'song.import_midi','params':{'file':'out/loop.mid',"
	                       "'path':'music/imported.rsong'}}");
	g_clear_pointer (&r, json_node_unref);
	g_assert_true (LRG_IS_EDITOR_SONG_EDITOR (lrg_editor_data_editor_find (f->app, NULL)));
	g_assert_true (lrg_editor_data_editor_find (f->app, NULL) != LRG_EDITOR_DATA_EDITOR (ed));
}

/* the live preview, headless: renders sound and follows edits */
static void
test_song_preview (EditorAppFixture *f,
                   gconstpointer     data)
{
	g_autoptr(GError)    error = NULL;
	g_autoptr(JsonNode)  r = NULL;
	g_autofree gfloat   *pcm = NULL;
	LrgEditorSongEditor *ed;
	gsize                n = 0, i;
	gfloat               peak = 0.0f;
	gdouble              beat;

	music_project (f);
	ed = new_song (f, "music/loop.rsong");
	pcm = lrg_editor_song_editor_preview_render (ed, 48000, &n, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (n, ==, 96000);
	for (i = 0; i < n; i++)
		peak = MAX (peak, fabsf (pcm[i]));
	g_assert_cmpfloat (peak, >, 0.01f);
	beat = lrg_editor_song_editor_get_beat (ed);
	g_assert_cmpfloat (beat, >, 1.0);

	/* an edit keeps the playhead */
	r = efx_ipc_result (f, "{'id':2,'method':'song.set_track','params':{'id':'bass','mute':true}}");
	g_clear_pointer (&r, json_node_unref);
	g_clear_pointer (&pcm, g_free);
	pcm = lrg_editor_song_editor_preview_render (ed, 1024, &n, &error);
	g_assert_no_error (error);
	g_assert_cmpfloat (lrg_editor_song_editor_get_beat (ed), >=, beat);

	r = efx_ipc_result (f, "{'id':3,'method':'song.seek','params':{'beat':4}}");
	g_assert_cmpfloat (json_object_get_double_member (obj_of (r), "beat"), ==, 4.0);
}

/* the canvas: select a clip, toggle a step, add a note, drag a clip */
static void
test_song_canvas (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorSongEditor *ed;
	LrgSong             *song;
	LrgWidget           *arrange;
	LrgWidget           *pattern;
	gfloat               ax, ay, aw, ah, px, py, pw, ph;
	const gchar         *lane;
	const gchar         *source = NULL;
	gdouble              start = -1, len = 0;
	guint                notes;

	music_project (f);
	ed = new_song (f, "music/loop.rsong");
	present (f, LRG_EDITOR_DATA_EDITOR (ed));
	arrange = efx_find (f, "song-arrangement");
	pattern = efx_find (f, "song-pattern");
	g_assert_nonnull (arrange);
	g_assert_nonnull (pattern);
	efx_rect_of (arrange, &ax, &ay, &aw, &ah);
	efx_rect_of (pattern, &px, &py, &pw, &ph);
	g_assert_cmpfloat (aw, >, 300.0f);

	/* the drums clip (beats 0-16 of the first track) */
	efx_click (f, ax + 132.0f + 12.0f * 2.0f, ay + 22.0f + 15.0f);
	g_assert_cmpstr (lrg_editor_song_editor_get_selected_track (ed), ==, "drums");
	g_assert_cmpint (lrg_editor_song_editor_get_selected_clip (ed), ==, 0);
	efx_frames (f, 2);

	/* the step grid: kick (first lane), second step goes . -> x */
	song = lrg_editor_song_editor_get_song (ed);
	lane = lrg_song_pattern_get_lane (song, "beat", "kick");
	g_assert_true (lane != NULL && lane[1] == '.');
	efx_click (f, px + 44.0f + (pw - 48.0f) / 16.0f * 1.5f, py + 4.0f + 10.0f);
	song = lrg_editor_song_editor_get_song (ed);
	lane = lrg_song_pattern_get_lane (song, "beat", "kick");
	g_assert_nonnull (lane);
	g_assert_cmpint (lane[1], ==, 'x');

	/* the bass clip: a click on an empty cell of the roll adds a note */
	efx_click (f, ax + 132.0f + 12.0f * 2.0f, ay + 22.0f + 30.0f + 15.0f);
	g_assert_cmpstr (lrg_editor_song_editor_get_selected_track (ed), ==, "bass");
	efx_frames (f, 2);
	notes = lrg_song_pattern_get_n_notes (lrg_editor_song_editor_get_song (ed), "bassline");
	efx_click (f, px + 44.0f + (pw - 48.0f) * 0.9f, py + 6.0f);
	g_assert_cmpuint (lrg_song_pattern_get_n_notes (lrg_editor_song_editor_get_song (ed), "bassline"), ==, notes + 1);

	/* drag the bass clip four beats right: one undo step */
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, ax + 132.0f + 12.0f * 2.0f, ay + 22.0f + 45.0f,
	          ax + 132.0f + 12.0f * 6.0f, ay + 22.0f + 45.0f, 4);
	g_assert_true (lrg_song_get_clip (lrg_editor_song_editor_get_song (ed), "bass", 0, &source, &start, &len));
	g_assert_cmpfloat (start, ==, 4.0);
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_true (lrg_song_get_clip (lrg_editor_song_editor_get_song (ed), "bass", 0, &source, &start, &len));
	g_assert_cmpfloat (start, ==, 0.0);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/editor-app/music/foley-ipc", EditorAppFixture, NULL, efx_setup, test_foley_ipc, efx_teardown);
	g_test_add ("/editor-app/music/foley-canvas", EditorAppFixture, NULL, efx_setup, test_foley_canvas, efx_teardown);
	g_test_add ("/editor-app/music/song-ipc", EditorAppFixture, NULL, efx_setup, test_song_ipc, efx_teardown);
	g_test_add ("/editor-app/music/song-render-export", EditorAppFixture, NULL, efx_setup, test_song_render_export,
	            efx_teardown);
	g_test_add ("/editor-app/music/song-preview", EditorAppFixture, NULL, efx_setup, test_song_preview, efx_teardown);
	g_test_add ("/editor-app/music/song-canvas", EditorAppFixture, NULL, efx_setup, test_song_canvas, efx_teardown);
	return g_test_run ();
}
