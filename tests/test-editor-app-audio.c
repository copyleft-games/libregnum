/* test-editor-app-audio.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Audio tools in lrgeditor (0.9.0 M7): the .rmixer data editor (channel
 * strips driven through the canvas, the bus / effect / snapshot / ducking
 * grids, one undo step per edit, byte-stable saves), the offline preview
 * with meters, snapshot previews and ducking, the mixer.* IPC methods,
 * the docked Mixer panel, the attenuation gizmo of audio emitters (volume
 * handles, a canvas drag in the viewport), the inspector's Audio Source
 * section (component rows, custom rolloff curve, bus picker) and the
 * audio.* IPC methods. Everything is headless; no audio device is used.
 */

#include "editor-app-fixture.h"
#include <math.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* A project with audio/main.rmixer (music, sfx, dialogue) as its
 * default mixer, created and opened through mixer.create. */
static LrgEditorMixerEditor *
audio_project (EditorAppFixture *f)
{
	g_autoptr(GError)     error = NULL;
	g_autoptr(JsonNode)   r = NULL;
	g_autofree gchar     *path = NULL;
	LrgEditorMixerEditor *ed;

	lrg_editor_mixer_editor_install (f->app);
	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Audio", &error));
	g_assert_no_error (error);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.create','params':{'path':'audio/main.rmixer',"
	                       "'buses':['music','sfx','dialogue'],'set_default':true}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "buses")), ==,
	                  4);
	path = lrg_editor_mixer_editor_project_mixer_path (f->app);
	g_assert_nonnull (path);
	g_assert_true (g_str_has_suffix (path, "audio/main.rmixer"));
	ed = lrg_editor_mixer_editor_find_for_path (f->app, path, FALSE, NULL);
	g_assert_nonnull (ed);
	efx_frames (f, 2);
	return ed;
}

/* Moves the editor's tab over the viewport (more room than the bottom
 * stack) and paints a frame so the strips know their part rectangles. */
static void
present_editor (EditorAppFixture     *f,
                LrgEditorMixerEditor *ed)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *p = lrg_dock_area_get_panel (dock, lrg_editor_data_editor_get_panel_id (LRG_EDITOR_DATA_EDITOR (ed)));
	LrgDockPanel *vp = lrg_dock_area_get_panel (dock, "viewport");

	g_assert_nonnull (p);
	if (vp != NULL)
		lrg_dock_area_move_panel (dock, p, vp, LRG_DOCK_POSITION_CENTER);
	lrg_dock_area_present_panel (dock, p);
	efx_frames (f, 3);
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
}

static void
part_center (LrgWidget               *strips,
             const gchar             *bus,
             LrgEditorMixerStripPart  part,
             guint                    index,
             gfloat                  *cx,
             gfloat                  *cy)
{
	gfloat x, y, w, h;

	g_assert_true (lrg_editor_mixer_strips_get_part_rect (strips, bus, part, index, &x, &y, &w, &h));
	g_assert_cmpfloat (w, >, 0.0f);
	*cx = x + w * 0.5f;
	*cy = y + h * 0.5f;
}

/* Scrolls every scroll box above @name so the widget is in view, then
 * clicks it. */
static void
click_named (EditorAppFixture *f,
             const gchar      *name)
{
	LrgWidget    *w = efx_find (f, name);
	LrgContainer *p;

	g_assert_nonnull (w);
	for (p = lrg_widget_get_parent (w); p != NULL; p = lrg_widget_get_parent (LRG_WIDGET (p)))
		if (LRG_IS_SCROLL_BOX (p))
			lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (p), w);
	efx_frames (f, 40);
	efx_click_widget (f, w);
}

static LrgNode *
create_node (EditorAppFixture *f,
             const gchar      *json_params)
{
	g_autofree gchar   *req = g_strdup_printf ("{'id':1,'method':'node.create','params':%s}", json_params);
	g_autoptr(JsonNode) r = efx_ipc_result (f, req);

	return lrg_editor_find_node (f->editor, json_object_get_string_member (json_node_get_object (r), "guid"));
}

/* The JSON bus object @name of a mixer.get result. */
static JsonObject *
bus_json (JsonNode    *mixer,
          const gchar *name)
{
	JsonArray *a = json_object_get_array_member (json_node_get_object (mixer), "buses");
	guint      i;

	for (i = 0; i < json_array_get_length (a); i++)
	{
		JsonObject *b = json_array_get_object_element (a, i);

		if (g_strcmp0 (json_object_get_string_member (b, "name"), name) == 0)
			return b;
	}
	return NULL;
}

/* Peak (linear) of @bus from mixer.meters, -1 when not listed. */
static gdouble
meter_peak (EditorAppFixture *f,
            const gchar      *bus,
            gdouble          *duck_gain)
{
	g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'mixer.meters'}");
	JsonArray          *a = json_object_get_array_member (json_node_get_object (r), "buses");
	guint               i;

	for (i = 0; i < json_array_get_length (a); i++)
	{
		JsonObject *b = json_array_get_object_element (a, i);

		if (g_strcmp0 (json_object_get_string_member (b, "name"), bus) == 0)
		{
			if (duck_gain != NULL)
				*duck_gain = json_object_get_double_member (b, "duck_gain");
			return json_object_get_double_member (b, "peak");
		}
	}
	return -1.0;
}

/* ==========================================================================
 * Fader law
 * ========================================================================== */

static void
test_fader_law (void)
{
	gdouble db;

	g_assert_cmpfloat_with_epsilon (lrg_editor_mixer_fader_db_to_position (0.0), 0.75, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_editor_mixer_fader_db_to_position (-12.0), 0.45, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_editor_mixer_fader_db_to_position (LRG_EDITOR_MIXER_FADER_MIN_DB), 0.05, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_editor_mixer_fader_db_to_position (LRG_AUDIO_MIXER_MAX_GAIN_DB), 1.0, 1e-9);
	g_assert_cmpfloat (lrg_editor_mixer_fader_db_to_position (LRG_AUDIO_SILENCE_DB), ==, 0.0);
	for (db = -60.0; db <= 12.0; db += 1.5)
		g_assert_cmpfloat_with_epsilon (lrg_editor_mixer_fader_position_to_db (
			lrg_editor_mixer_fader_db_to_position (db)), db, 1e-6);
	g_assert_cmpfloat (lrg_editor_mixer_fader_position_to_db (0.01), ==, LRG_AUDIO_SILENCE_DB);
	g_assert_true (g_enum_get_value_by_nick (g_type_class_ref (LRG_TYPE_EDITOR_MIXER_STRIP_PART), "fader") != NULL);
}

/* ==========================================================================
 * Mixer editor: strips through the canvas
 * ========================================================================== */

static void
test_strips (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgEditorMixerEditor *ed = audio_project (f);
	LrgEditorDocument    *doc = lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed));
	LrgWidget            *strips = lrg_editor_mixer_editor_get_strips (ed);
	gfloat                x, y, fx, fy, fw, fh;
	gdouble               vol;

	present_editor (f, ed);
	g_assert_nonnull (lrg_editor_mixer_editor_get_asset (ed));

	/* the header selects the bus */
	part_center (strips, "sfx", LRG_EDITOR_MIXER_STRIP_HEADER, 0, &x, &y);
	efx_click (f, x, y);
	g_assert_cmpstr (lrg_editor_mixer_editor_get_selected_bus (ed), ==, "sfx");
	g_assert_null (lrg_editor_mixer_editor_get_selected_effect (ed));

	/* Mute and Solo toggle, one undo step each */
	g_assert_false (lrg_editor_document_can_undo (doc));
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
	part_center (strips, "music", LRG_EDITOR_MIXER_STRIP_MUTE, 0, &x, &y);
	efx_click (f, x, y);
	g_assert_true (lrg_mixer_asset_get_bus_mute (lrg_editor_mixer_editor_get_asset (ed), "music"));
	g_assert_true (lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (ed)));
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
	part_center (strips, "dialogue", LRG_EDITOR_MIXER_STRIP_SOLO, 0, &x, &y);
	efx_click (f, x, y);
	g_assert_true (lrg_mixer_asset_get_bus_solo (lrg_editor_mixer_editor_get_asset (ed), "dialogue"));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_false (lrg_mixer_asset_get_bus_solo (lrg_editor_mixer_editor_get_asset (ed), "dialogue"));
	g_assert_true (lrg_mixer_asset_get_bus_mute (lrg_editor_mixer_editor_get_asset (ed), "music"));
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_false (lrg_mixer_asset_get_bus_mute (lrg_editor_mixer_editor_get_asset (ed), "music"));
	g_assert_false (lrg_editor_document_can_undo (doc));

	/* a fader drag from 0 dB down is ONE undo step */
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
	g_assert_true (lrg_editor_mixer_strips_get_part_rect (strips, "sfx", LRG_EDITOR_MIXER_STRIP_FADER, 0, &fx, &fy,
	                                                      &fw, &fh));
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, fx + fw * 0.5f, fy + fh * 0.25f, fx + fw * 0.5f, fy + fh * 0.6f, 5);
	vol = lrg_mixer_asset_get_bus_volume (lrg_editor_mixer_editor_get_asset (ed), "sfx");
	g_assert_cmpfloat_with_epsilon (vol, lrg_editor_mixer_fader_position_to_db (0.4), 0.6);
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpfloat (lrg_mixer_asset_get_bus_volume (lrg_editor_mixer_editor_get_asset (ed), "sfx"), ==, 0.0);
	g_assert_false (lrg_editor_document_can_undo (doc));
	g_assert_true (lrg_editor_document_redo (doc));
	g_assert_cmpfloat_with_epsilon (lrg_mixer_asset_get_bus_volume (lrg_editor_mixer_editor_get_asset (ed), "sfx"),
	                                vol, 1e-9);

	/* an effect and a send show as rows; clicking the row selects the
	 * effect, its dot bypasses it; a send drags sideways */
	{
		g_autoptr(JsonNode) r1 = efx_ipc_result (f, "{'id':1,'method':'mixer.add_effect','params':"
		                                            "{'bus':'music','type':'low-pass','id':'muffle'}}");
		g_autoptr(JsonNode) r2 = efx_ipc_result (f, "{'id':1,'method':'mixer.set_send','params':"
		                                            "{'source':'sfx','target':'dialogue','level':-24}}");
		gdouble             lvl = 0.0;
		gfloat              sx, sy, sw, sh;

		efx_frames (f, 2);
		lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
		part_center (strips, "music", LRG_EDITOR_MIXER_STRIP_EFFECT, 0, &x, &y);
		efx_click (f, x, y);
		g_assert_cmpstr (lrg_editor_mixer_editor_get_selected_bus (ed), ==, "music");
		g_assert_cmpstr (lrg_editor_mixer_editor_get_selected_effect (ed), ==, "muffle");
		lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
		part_center (strips, "music", LRG_EDITOR_MIXER_STRIP_EFFECT_TOGGLE, 0, &x, &y);
		efx_click (f, x, y);
		g_assert_false (lrg_mixer_asset_get_effect_enabled (lrg_editor_mixer_editor_get_asset (ed), "music",
		                                                    "muffle"));
		lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
		g_assert_true (lrg_editor_mixer_strips_get_part_rect (strips, "sfx", LRG_EDITOR_MIXER_STRIP_SEND, 0, &sx, &sy,
		                                                      &sw, &sh));
		efx_drag (f, GRL_MOUSE_BUTTON_LEFT, sx + 4.0f, sy + sh * 0.5f, sx + 4.0f + sw * 0.3f, sy + sh * 0.5f, 3);
		g_assert_true (lrg_mixer_asset_get_send (lrg_editor_mixer_editor_get_asset (ed), "sfx", "dialogue", &lvl));
		g_assert_cmpfloat (lvl, >, -20.0);
	}

	/* the strips keep painting with meters while previewing */
	lrg_editor_mixer_editor_set_previewing (ed, TRUE);
	efx_frames (f, 3);
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
	g_assert_true (lrg_editor_mixer_strips_get_part_rect (strips, "master", LRG_EDITOR_MIXER_STRIP_METER, 0, &x, &y,
	                                                      &fw, &fh));
	lrg_editor_mixer_editor_set_previewing (ed, FALSE);
}

/* ==========================================================================
 * Grids, controls, save
 * ========================================================================== */

static void
test_grids (EditorAppFixture *f,
            gconstpointer     data)
{
	LrgEditorMixerEditor *ed = audio_project (f);
	LrgEditorDocument    *doc = lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed));
	LrgPropertySource    *src;
	g_auto(GValue)        v = G_VALUE_INIT;
	g_auto(GValue)        out = G_VALUE_INIT;
	gdouble               d = 0.0;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *saved = NULL;
	g_autofree gchar     *text = NULL;
	g_autoptr(LrgMixerAsset) reread = NULL;

	present_editor (f, ed);

	/* bus grid: volume and parent rows of the selected bus */
	g_assert_true (lrg_editor_mixer_editor_select (ed, "dialogue", NULL));
	efx_frames (f, 2);
	src = lrg_property_grid_get_source (lrg_editor_mixer_editor_get_bus_grid (ed), 0);
	g_assert_nonnull (lrg_property_source_find_property (src, "volume"));
	g_assert_nonnull (lrg_property_source_find_property (src, "parent"));
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, -3.5);
	g_assert_true (lrg_property_source_set_value (src, "volume", &v));
	g_assert_cmpfloat (lrg_mixer_asset_get_bus_volume (lrg_editor_mixer_editor_get_asset (ed), "dialogue"), ==, -3.5);
	g_assert_true (lrg_property_source_get_value (src, "volume", &out));
	g_assert_cmpfloat (g_value_get_double (&out), ==, -3.5);
	g_value_unset (&v);

	/* add a bus as a child of the selected one, reroute it */
	lrg_text_input_set_text (LRG_TEXT_INPUT (efx_find (f, "mixer-bus-name")), "voice");
	click_named (f, "mixer-add-bus");
	g_assert_true (lrg_mixer_asset_has_bus (lrg_editor_mixer_editor_get_asset (ed), "voice"));
	g_assert_cmpstr (lrg_mixer_asset_get_bus_parent (lrg_editor_mixer_editor_get_asset (ed), "voice"), ==, "dialogue");
	g_assert_cmpstr (lrg_editor_mixer_editor_get_selected_bus (ed), ==, "voice");
	efx_frames (f, 2);
	src = lrg_property_grid_get_source (lrg_editor_mixer_editor_get_bus_grid (ed), 0);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "music");
	g_assert_true (lrg_property_source_set_value (src, "parent", &v));
	g_assert_cmpstr (lrg_mixer_asset_get_bus_parent (lrg_editor_mixer_editor_get_asset (ed), "voice"), ==, "music");
	g_value_unset (&v);

	/* effect grid: an effect's parameters are its properties */
	g_assert_true (lrg_editor_mixer_editor_select (ed, "music", NULL));
	lrg_combo_box_set_selected (LRG_COMBO_BOX (efx_find (f, "mixer-add-effect")), 1);
	g_assert_nonnull (lrg_editor_mixer_editor_get_selected_effect (ed));
	efx_frames (f, 2);
	src = lrg_property_grid_get_source (lrg_editor_mixer_editor_get_bus_grid (ed), 0);
	g_assert_nonnull (lrg_property_source_find_property (src, "enabled"));

	/* the remove control removes the selected effect */
	click_named (f, "mixer-remove");
	{
		g_auto(GStrv) ids = lrg_mixer_asset_get_effect_ids (lrg_editor_mixer_editor_get_asset (ed), "music");

		g_assert_true (ids == NULL || ids[0] == NULL);
	}

	/* snapshots: add from the controls, override a volume in the grid
	 * (OVERRIDDEN state), reset it */
	lrg_text_input_set_text (LRG_TEXT_INPUT (efx_find (f, "mixer-snapshot-name")), "underwater");
	click_named (f, "mixer-add-snapshot");
	g_assert_cmpstr (lrg_editor_mixer_editor_get_selected_snapshot (ed), ==, "underwater");
	efx_frames (f, 2);
	src = lrg_property_grid_get_source (lrg_editor_mixer_editor_get_snapshot_grid (ed), 0);
	{
		g_autoptr(GPtrArray) props = lrg_property_source_list_properties (src);
		const gchar         *music_row = NULL;
		guint                i;

		for (i = 0; i < props->len; i++)
			if (g_strcmp0 (lrg_property_info_get_label (g_ptr_array_index (props, i)), "music") == 0)
				music_row = lrg_property_info_get_name (g_ptr_array_index (props, i));
		g_assert_nonnull (music_row);
		g_assert_cmpint (lrg_property_source_get_state (src, music_row), ==, LRG_PROPERTY_STATE_DEFAULT);
		g_value_init (&v, G_TYPE_DOUBLE);
		g_value_set_double (&v, -18.0);
		g_assert_true (lrg_property_source_set_value (src, music_row, &v));
		g_assert_true (lrg_mixer_asset_get_snapshot_volume (lrg_editor_mixer_editor_get_asset (ed), "underwater",
		                                                    "music", &d));
		g_assert_cmpfloat (d, ==, -18.0);
		g_assert_cmpint (lrg_property_source_get_state (src, music_row), ==, LRG_PROPERTY_STATE_OVERRIDDEN);
		g_assert_true (lrg_property_source_reset (src, music_row));
		g_assert_false (lrg_mixer_asset_get_snapshot_volume (lrg_editor_mixer_editor_get_asset (ed), "underwater",
		                                                     "music", &d));
		g_value_unset (&v);
	}

	/* capture: the current faders become the snapshot */
	g_assert_true (lrg_editor_mixer_editor_capture_snapshot (ed, "underwater", &error));
	g_assert_no_error (error);
	g_assert_true (lrg_mixer_asset_get_snapshot_volume (lrg_editor_mixer_editor_get_asset (ed), "underwater",
	                                                    "dialogue", &d));
	g_assert_cmpfloat (d, ==, -3.5);

	/* ducking from the pickers, then the rule grid */
	lrg_combo_box_set_selected (LRG_COMBO_BOX (efx_find (f, "mixer-duck-trigger")), 3);  /* dialogue */
	lrg_combo_box_set_selected (LRG_COMBO_BOX (efx_find (f, "mixer-duck-target")), 1);   /* music */
	click_named (f, "mixer-add-duck");
	g_assert_cmpuint (lrg_mixer_asset_get_duck_count (lrg_editor_mixer_editor_get_asset (ed)), ==, 1);
	efx_frames (f, 2);
	src = lrg_property_grid_get_source (lrg_editor_mixer_editor_get_duck_grid (ed), 0);
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, -20.0);
	g_assert_true (lrg_property_source_set_value (src, "depth", &v));
	{
		const gchar *tr, *tg;
		gdouble      th, dp, at, rl;

		g_assert_true (lrg_mixer_asset_get_duck (lrg_editor_mixer_editor_get_asset (ed), 0, &tr, &tg, &th, &dp, &at,
		                                         &rl));
		g_assert_cmpstr (tr, ==, "dialogue");
		g_assert_cmpstr (tg, ==, "music");
		g_assert_cmpfloat (dp, ==, -20.0);
	}

	/* validation is clean; save writes exactly the engine's bytes */
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed)), ==, 0);
	g_assert_true (lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (ed), &error));
	g_assert_no_error (error);
	g_assert_false (lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_true (g_file_get_contents (lrg_editor_data_editor_get_path (LRG_EDITOR_DATA_EDITOR (ed)), &saved, NULL,
	                                    NULL));
	reread = lrg_mixer_asset_new_from_data (saved, -1, &error);
	g_assert_no_error (error);
	text = lrg_mixer_asset_to_data (reread, NULL, NULL);
	g_assert_cmpstr (text, ==, saved);
	g_assert_true (lrg_editor_document_can_undo (doc));
}

/* ==========================================================================
 * Preview: meters, solo, snapshots, ducking
 * ========================================================================== */

static void
test_preview (EditorAppFixture *f,
              gconstpointer     data)
{
	LrgEditorMixerEditor *ed = audio_project (f);
	g_autoptr(GError)     error = NULL;
	gdouble               peak, duck = 1.0;
	g_autoptr(JsonNode)   r = NULL;

	/* not previewing: no meter source */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.meters'}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "source"), ==, "none");
	g_clear_pointer (&r, json_node_unref);

	r = efx_ipc_result (f, "{'id':1,'method':'mixer.preview','params':{'playing':true}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "playing"));
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "source"), ==, "preview");
	g_clear_pointer (&r, json_node_unref);
	g_assert_nonnull (lrg_editor_mixer_editor_get_preview_mixer (ed));
	lrg_editor_mixer_editor_preview_render (ed, 0.25);
	peak = meter_peak (f, "music", NULL);
	g_assert_cmpfloat (peak, >, 0.01);
	g_assert_cmpfloat (meter_peak (f, "master", NULL), >, 0.01);

	/* mute: the bus meter goes silent (the preview follows edits) */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_bus','params':{'name':'music','mute':true}}");
	g_clear_pointer (&r, json_node_unref);
	lrg_editor_mixer_editor_preview_render (ed, 0.1);
	g_assert_cmpfloat (meter_peak (f, "music", NULL), ==, 0.0);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_bus','params':{'name':'music','mute':false}}");
	g_clear_pointer (&r, json_node_unref);

	/* a snapshot at -144 dB for music: previewing it silences the bus;
	 * back to the base layout brings it back */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.add_snapshot','params':{'name':'quiet','blend':0}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_snapshot','params':{'name':'quiet','volumes':{'music':-144}}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.preview','params':{'snapshot':'quiet','blend':0}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "snapshot"), ==, "quiet");
	g_clear_pointer (&r, json_node_unref);
	lrg_editor_mixer_editor_preview_render (ed, 0.1);
	g_assert_cmpfloat (meter_peak (f, "music", NULL), <, 1e-6);
	g_assert_cmpstr (lrg_editor_mixer_editor_get_preview_snapshot (ed), ==, "quiet");
	g_assert_true (lrg_editor_mixer_editor_preview_snapshot (ed, NULL, 0.0, &error));
	lrg_editor_mixer_editor_preview_render (ed, 0.25);
	g_assert_cmpfloat (meter_peak (f, "music", NULL), >, 0.01);

	/* blend preview at half weight */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.preview','params':{'snapshots':['quiet'],'weights':[1],"
	                       "'blend':0}}");
	g_clear_pointer (&r, json_node_unref);
	lrg_editor_mixer_editor_preview_render (ed, 0.1);
	g_assert_cmpfloat (meter_peak (f, "music", NULL), <, 1e-6);
	g_assert_true (lrg_editor_mixer_editor_preview_snapshot (ed, NULL, 0.0, &error));

	/* ducking: dialogue (pulsing test tone) ducks music; the gain
	 * reduction shows on music's meter */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_duck','params':{'trigger':'dialogue','target':'music',"
	                       "'threshold':-40,'depth':-20,'attack':0,'release':0}}");
	g_clear_pointer (&r, json_node_unref);
	{
		gdouble lowest = 1.0;
		gint    i;

		/* over one period of the dialogue tone the duck engages */
		for (i = 0; i < 25; i++)
		{
			lrg_editor_mixer_editor_preview_render (ed, 0.05);
			meter_peak (f, "music", &duck);
			lowest = MIN (lowest, duck);
		}
		g_assert_cmpfloat (lowest, <, 0.5);
	}

	/* stop */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.preview','params':{'playing':false}}");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "playing"));
	g_assert_null (lrg_editor_mixer_editor_get_preview_mixer (ed));
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	LrgEditorMixerEditor *ed = audio_project (f);
	g_autoptr(JsonNode)   r = NULL;
	g_autofree gchar     *err = NULL;
	JsonObject           *b;
	JsonArray            *a;
	guint                 i;
	gboolean              found = FALSE;

	/* effect types list the engine's effects and their parameters */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.effect_types'}");
	a = json_object_get_array_member (json_node_get_object (r), "types");
	for (i = 0; i < json_array_get_length (a); i++)
		if (g_strcmp0 (json_object_get_string_member (json_array_get_object_element (a, i), "name"), "reverb") == 0)
			found = TRUE;
	g_assert_true (found);
	g_clear_pointer (&r, json_node_unref);

	/* buses, effects with parameters, effect order */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.add_bus','params':{'name':'verb'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.add_effect','params':{'bus':'verb','type':'reverb'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.add_effect','params':{'bus':'verb','type':'low-pass'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_effect','params':{'bus':'verb','id':'reverb',"
	                       "'params':{'preset':'hall','wet':0.4}}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.move_effect','params':{'bus':'verb','id':'low-pass','index':0}}");
	b = bus_json (r, "verb");
	g_assert_nonnull (b);
	a = json_object_get_array_member (b, "effects");
	g_assert_cmpuint (json_array_get_length (a), ==, 2);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (a, 0), "id"), ==, "low-pass");
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (
		json_array_get_object_element (a, 1), "params"), "preset"), ==, "hall");
	g_clear_pointer (&r, json_node_unref);

	/* bus settings and sends */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_bus','params':{'name':'music','volume':-6,'solo':true}}");
	b = bus_json (r, "music");
	g_assert_cmpfloat (json_object_get_double_member (b, "volume"), ==, -6.0);
	g_assert_true (json_object_get_boolean_member (b, "solo"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_send','params':{'source':'sfx','target':'verb','level':-12}}");
	b = bus_json (r, "sfx");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (b, "sends")), ==, 1);
	g_clear_pointer (&r, json_node_unref);

	/* routing errors come back as errors, nothing changes */
	err = efx_ipc (f, "{'id':1,'method':'mixer.set_send','params':{'source':'verb','target':'sfx','level':-12}}");
	g_assert_nonnull (strstr (err, "error"));
	g_clear_pointer (&err, g_free);
	err = efx_ipc (f, "{'id':1,'method':'mixer.set_bus','params':{'name':'nope','volume':0}}");
	g_assert_nonnull (strstr (err, "No such bus"));
	g_clear_pointer (&err, g_free);
	err = efx_ipc (f, "{'id':1,'method':'mixer.add_effect','params':{'bus':'sfx','type':'wobble'}}");
	g_assert_nonnull (strstr (err, "error"));
	g_clear_pointer (&err, g_free);

	/* snapshots: capture, params override, default, remove */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.add_snapshot','params':{'name':'combat','blend':0.5,'capture':true}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_snapshot','params':{'name':'combat','default':true,"
	                       "'params':{'verb/reverb/wet':0.1},'sends':[{'source':'dialogue','target':'verb','level':-6}]}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "default_snapshot"), ==, "combat");
	{
		JsonObject *s = json_array_get_object_element (json_object_get_array_member (json_node_get_object (r),
		                                                                             "snapshots"), 0);

		g_assert_cmpfloat (json_object_get_double_member (json_object_get_object_member (s, "volumes"), "music"), ==,
		                   -6.0);
		g_assert_cmpfloat (json_object_get_double_member (json_object_get_object_member (s, "params"),
		                                                  "verb/reverb/wet"), ==, 0.1);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (s, "sends")), >=, 2);
	}
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.capture_snapshot','params':{'name':'calm'}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "snapshots")),
	                  ==, 2);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.remove_snapshot','params':{'name':'calm'}}");
	g_clear_pointer (&r, json_node_unref);

	/* ducking */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_duck','params':{'trigger':'dialogue','target':'music',"
	                       "'depth':-9}}");
	a = json_object_get_array_member (json_node_get_object (r), "ducking");
	g_assert_cmpuint (json_array_get_length (a), ==, 1);
	g_assert_cmpfloat (json_object_get_double_member (json_array_get_object_element (a, 0), "depth"), ==, -9.0);
	g_assert_cmpfloat (json_object_get_double_member (json_array_get_object_element (a, 0), "threshold"), ==, -30.0);
	g_clear_pointer (&r, json_node_unref);
	err = efx_ipc (f, "{'id':1,'method':'mixer.set_duck','params':{'trigger':'music','target':'music'}}");
	g_assert_nonnull (strstr (err, "error"));
	g_clear_pointer (&err, g_free);

	/* every edit was one undo step in the document */
	g_assert_cmpstr (lrg_editor_document_get_undo_label (lrg_editor_data_editor_get_document (
		LRG_EDITOR_DATA_EDITOR (ed))), ==, "Edit ducking rule");

	/* remove what was added */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.remove_duck','params':{'trigger':'dialogue','target':'music'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.remove_send','params':{'source':'sfx','target':'verb'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.remove_effect','params':{'bus':'verb','id':'low-pass'}}");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.remove_bus','params':{'name':'verb'}}");
	g_assert_null (bus_json (r, "verb"));
	g_clear_pointer (&r, json_node_unref);
	err = efx_ipc (f, "{'id':1,'method':'mixer.remove_bus','params':{'name':'master'}}");
	g_assert_nonnull (strstr (err, "error"));
	g_clear_pointer (&err, g_free);

	/* save, get (from disk state), default mixer */
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.save'}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "saved"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.get','params':{'path':'audio/main.rmixer'}}");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "dirty"));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_default','params':{'path':null}}");
	g_assert_null (lrg_editor_mixer_editor_project_mixer_path (f->app));
	g_clear_pointer (&r, json_node_unref);
	err = efx_ipc (f, "{'id':1,'method':'mixer.get'}");
	g_assert_nonnull (strstr (err, "No mixer"));
	g_clear_pointer (&err, g_free);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.set_default','params':{'path':'audio/main.rmixer'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "mixer"), ==, "audio/main.rmixer");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'mixer.open'}");
	g_assert_nonnull (json_object_get_string_member (json_node_get_object (r), "panel"));
}

/* ==========================================================================
 * The Mixer panel
 * ========================================================================== */

static LrgEditorMixerPanel *
mixer_panel (EditorAppFixture *f)
{
	LrgEditorMixerPanel *panel = lrg_editor_mixer_panel_find (f->app);

	if (panel == NULL)
	{
		LrgDockPanel *dock = lrg_dock_panel_new (LRG_EDITOR_MIXER_PANEL_ID, "Mixer");

		panel = lrg_editor_mixer_panel_new (f->app);
		lrg_dock_panel_set_child (dock, LRG_WIDGET (panel));
		g_object_unref (panel);
		lrg_editor_app_add_panel (f->app, dock, "tab:viewport");
	}
	else
	{
		/* The shell docks the Mixer in the short bottom area; the strip
		 * clicks below need the tall centre area, so move it there */
		LrgDockArea *area = lrg_editor_app_get_dock (f->app);

		lrg_dock_area_move_panel (area, lrg_dock_area_get_panel (area, LRG_EDITOR_MIXER_PANEL_ID),
		                          lrg_dock_area_get_panel (area, "viewport"), LRG_DOCK_POSITION_CENTER);
	}
	efx_frames (f, 2);
	return panel;
}

static void
present_mixer_panel (EditorAppFixture *f)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *p = lrg_dock_area_get_panel (dock, LRG_EDITOR_MIXER_PANEL_ID);

	g_assert_nonnull (p);
	lrg_dock_area_present_panel (dock, p);
	efx_frames (f, 3);
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
}

static void
test_panel (EditorAppFixture *f,
            gconstpointer     data)
{
	g_autoptr(GError)     error = NULL;
	LrgEditorMixerPanel  *panel;
	LrgEditorMixerEditor *ed;
	LrgWidget            *strips;
	g_autofree gchar     *path = NULL;
	gfloat                x, y;

	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Audio", &error));
	panel = mixer_panel (f);

	/* no project mixer: the empty state, New Mixer creates one */
	g_assert_null (lrg_editor_mixer_panel_get_path (panel));
	present_mixer_panel (f);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "mixer-panel-empty")));
	efx_click_widget (f, efx_find (f, "mixer-panel-empty-new"));
	lrg_editor_mixer_panel_reload (panel);
	g_assert_nonnull (lrg_editor_mixer_panel_get_path (panel));
	g_assert_true (g_str_has_suffix (lrg_editor_mixer_panel_get_path (panel), "audio/main.rmixer"));
	path = g_strdup (lrg_editor_mixer_panel_get_path (panel));
	ed = lrg_editor_mixer_editor_find_for_path (f->app, path, FALSE, NULL);
	g_assert_nonnull (ed);
	g_assert_true (lrg_mixer_asset_has_bus (lrg_editor_mixer_panel_get_asset (panel), "dialogue"));

	/* the editor closes: the panel shows the file */
	g_assert_true (lrg_editor_data_editor_close (LRG_EDITOR_DATA_EDITOR (ed), TRUE));
	efx_frames (f, 2);
	g_assert_null (lrg_editor_mixer_editor_find_for_path (f->app, path, FALSE, NULL));
	g_assert_true (lrg_mixer_asset_has_bus (lrg_editor_mixer_panel_get_asset (panel), "music"));

	/* a strip edit in the panel opens the editor and lands there */
	mixer_panel (f);
	present_mixer_panel (f);
	strips = lrg_editor_mixer_panel_get_strips (panel);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "mixer-panel-scroll")));
	part_center (strips, "sfx", LRG_EDITOR_MIXER_STRIP_MUTE, 0, &x, &y);
	efx_click (f, x, y);
	ed = lrg_editor_mixer_editor_find_for_path (f->app, path, FALSE, NULL);
	g_assert_nonnull (ed);
	g_assert_true (lrg_mixer_asset_get_bus_mute (lrg_editor_mixer_editor_get_asset (ed), "sfx"));
	g_assert_true (lrg_mixer_asset_get_bus_mute (lrg_editor_mixer_panel_get_asset (panel), "sfx"));

	/* Preview from the panel; the meters follow */
	present_mixer_panel (f);
	efx_click_widget (f, efx_find (f, "mixer-panel-preview"));
	g_assert_true (lrg_editor_mixer_editor_get_previewing (ed));
	efx_frames (f, 6);
	g_assert_cmpfloat (meter_peak (f, "music", NULL), >, 0.0);
	{
		const gchar *src = NULL;

		g_assert_nonnull (lrg_editor_mixer_editor_get_meter_mixer (f->app, ed, &src));
		g_assert_cmpstr (src, ==, "preview");
	}

	/* the action is registered; mixer.show answers */
	g_assert_true (lrg_editor_actions_has (lrg_editor_app_get_actions (f->app), "mixer.show"));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'mixer.show'}");

		g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "panel"), ==, "mixer");
	}
}

/* ==========================================================================
 * Attenuation gizmo
 * ========================================================================== */

/* The shell's viewport split (the viewport panel's 3D view). */
static void
frame_front (EditorAppFixture *f)
{
	LrgEditorViewState  *vs = lrg_editor_app_get_view_state (f->app);
	LrgEditorViewCamera *cam = lrg_editor_view_state_get_camera (vs, 0);

	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_FRONT);
	lrg_editor_view_camera_frame_all (cam, lrg_editor_get_level (f->editor), 50.0f);
	lrg_editor_view_state_camera_changed (vs, 0);
	efx_frames (f, 2);
}

static void
vp_to_canvas (EditorAppFixture *f,
              gfloat            u,
              gfloat            v,
              gfloat           *x,
              gfloat           *y)
{
	LrgEditorView view;
	gfloat        rx, ry, rw, rh;

	efx_viewport_rect (f, &rx, &ry, &rw, &rh);
	lrg_editor_view_state_get_view (lrg_editor_app_get_view_state (f->app), 0, &view);
	*x = rx + u * rw / view.width;
	*y = ry + v * rh / view.height;
}

static void
test_gizmo (EditorAppFixture *f,
            gconstpointer     data)
{
	LrgEditorViewState *vs = lrg_editor_app_get_view_state (f->app);
	LrgEditorView       view;
	LrgNode            *emitter, *anchor;
	LrgNodeVisual      *visual;
	gfloat              p[3], u, v, x, y;
	gdouble             val;
	guint               undo;

	lrg_editor_new_level (f->editor, "audio");
	emitter = create_node (f, "{'name':'Radio','kind':'audio-emitter'}");
	anchor = create_node (f, "{'name':'Far','kind':'primitive','primitive':'cube'}");
	g_assert_nonnull (emitter);
	lrg_node_set_location_xyz (emitter, 0.0f, 0.0f, 0.0f);
	lrg_node_set_location_xyz (anchor, 0.0f, 12.0f, 0.0f);
	visual = lrg_node_get_visual (emitter);
	lrg_node_visual_set_param_double (visual, "min_distance", 2.0);
	lrg_node_visual_set_param_double (visual, "radius", 8.0);
	lrg_editor_select (f->editor, emitter, FALSE);
	g_assert_true (lrg_editor_volume_has_gizmo (emitter));
	frame_front (f);
	lrg_editor_view_state_get_view (vs, 0, &view);

	/* min on the camera's right, max on its up; no cone handles yet */
	g_assert_true (lrg_editor_volume_get_handle_position (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_MIN_DISTANCE, p));
	g_assert_cmpfloat_with_epsilon (p[0], 2.0f, 1e-3f);
	g_assert_true (lrg_editor_volume_get_handle_position (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_MAX_DISTANCE, p));
	g_assert_cmpfloat_with_epsilon (p[1], 8.0f, 1e-3f);
	g_assert_false (lrg_editor_volume_get_handle_position (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_AUDIO_CONE, p));
	g_assert_false (lrg_editor_volume_get_handle_position (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_RANGE, p));
	g_assert_cmpstr (lrg_editor_volume_handle_get_param (LRG_EDITOR_VOLUME_HANDLE_MAX_DISTANCE), ==, "radius");
	g_assert_cmpstr (lrg_editor_volume_handle_get_param (LRG_EDITOR_VOLUME_HANDLE_MIN_DISTANCE), ==, "min_distance");

	/* hit test and the value under the handle */
	g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
	g_assert_cmpint (lrg_editor_volume_hit_test (&view, emitter, u, v), ==, LRG_EDITOR_VOLUME_HANDLE_MAX_DISTANCE);
	g_assert_true (lrg_editor_volume_drag_value (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_MAX_DISTANCE, u, v, &val));
	g_assert_cmpfloat_with_epsilon (val, 8.0, 0.05);

	/* drag the max handle up on the canvas: one undo step */
	undo = lrg_editor_get_undo_count (f->editor);
	vp_to_canvas (f, u, v, &x, &y);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x, y - 40.0f, 4);
	g_assert_cmpfloat (lrg_node_visual_get_param_double (visual, "radius", 0.0), >, 8.5);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	lrg_editor_undo (f->editor);
	/* undo restores the node's state: the visual may be a new object */
	visual = lrg_node_get_visual (emitter);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (visual, "radius", 0.0), 8.0, 1e-6);

	/* min never passes max */
	g_assert_true (lrg_editor_volume_get_handle_position (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_MAX_DISTANCE, p));
	p[1] += 30.0f;
	g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
	g_assert_true (lrg_editor_volume_drag_value (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_MIN_DISTANCE, u, v, &val));
	g_assert_cmpfloat_with_epsilon (val, 8.0, 1e-4);

	/* a cone: handles appear; the outer angle under its handle */
	lrg_node_visual_set_param_double (visual, "cone_outer_angle", 120.0);
	lrg_node_visual_set_param_double (visual, "cone_inner_angle", 60.0);
	lrg_node_set_rotation_xyz (emitter, 0.0f, (gfloat) G_PI_2, 0.0f);   /* radians: the cone along -X */
	g_assert_true (lrg_editor_volume_get_handle_position (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_AUDIO_CONE, p));
	g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
	g_assert_true (lrg_editor_volume_drag_value (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_AUDIO_CONE, u, v, &val));
	g_assert_cmpfloat_with_epsilon (val, 120.0, 1.5);
	g_assert_true (lrg_editor_volume_get_handle_position (&view, emitter, LRG_EDITOR_VOLUME_HANDLE_AUDIO_INNER_CONE,
	                                                      p));

	/* painting into a list */
	{
		g_autoptr(LrgPaintList) list = lrg_paint_list_new ();

		lrg_editor_gizmo_paint_volume (list, NULL, &view, emitter, LRG_EDITOR_VOLUME_HANDLE_NONE, FALSE, 0.0f, 0.0f,
		                               1.0f);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 100);
	}
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
}

/* ==========================================================================
 * Inspector: Audio Source section, audio.* IPC
 * ========================================================================== */

static void
test_inspector (EditorAppFixture *f,
                gconstpointer     data)
{
	LrgEditorMixerEditor *ed = audio_project (f);
	LrgNode              *emitter;
	LrgNodeVisual        *visual;
	LrgWidget            *section;
	LrgPropertyGrid      *grid;
	LrgPropertySource    *src;
	LrgPropertyInfo      *info;
	g_auto(GValue)        v = G_VALUE_INIT;
	guint                 undo;
	g_autoptr(JsonNode)   r = NULL;
	JsonObject           *params;

	(void) ed;
	lrg_editor_new_level (f->editor, "audio");
	emitter = create_node (f, "{'name':'Radio','kind':'audio-emitter'}");
	visual = lrg_node_get_visual (emitter);
	lrg_editor_select (f->editor, emitter, FALSE);
	efx_frames (f, 3);

	section = efx_find (f, "inspector-section-audio");
	g_assert_nonnull (section);
	g_assert_true (lrg_widget_get_visible (section));
	grid = LRG_PROPERTY_GRID (efx_find (f, "inspector-audio-grid"));
	src = lrg_property_grid_get_source (grid, 0);

	/* rows are the component's properties; the bus row lists the
	 * project mixer's buses */
	info = lrg_property_source_find_property (src, "bus");
	g_assert_nonnull (info);
	g_assert_nonnull (strstr (lrg_property_info_get_hint (info), "dialogue"));
	lrg_property_info_unref (info);
	info = lrg_property_source_find_property (src, "rolloff-mode");
	g_assert_nonnull (info);
	g_assert_true (G_TYPE_IS_ENUM (lrg_property_info_get_value_type (info)));
	lrg_property_info_unref (info);
	g_assert_cmpint (lrg_property_source_get_state (src, "min-distance"), ==, LRG_PROPERTY_STATE_DEFAULT);

	/* a write is a visual param and one undo step; max is "radius" */
	undo = lrg_editor_get_undo_count (f->editor);
	g_value_init (&v, G_TYPE_FLOAT);
	g_value_set_float (&v, 25.0f);
	g_assert_true (lrg_property_source_set_value (src, "max-distance", &v));
	g_assert_cmpfloat (lrg_node_visual_get_param_double (visual, "radius", 0.0), ==, 25.0);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
	g_assert_cmpint (lrg_property_source_get_state (src, "max-distance"), ==, LRG_PROPERTY_STATE_OVERRIDDEN);
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "sfx");
	g_assert_true (lrg_property_source_set_value (src, "bus", &v));
	g_value_unset (&v);
	lrg_editor_undo (f->editor);
	visual = lrg_node_get_visual (emitter);
	g_assert_null (lrg_node_visual_get_param (visual, "bus"));
	g_assert_true (lrg_property_source_reset (src, "max-distance"));
	g_assert_null (lrg_node_visual_get_param (visual, "radius"));

	/* the custom rolloff shows the curve editor; editing it writes keys */
	g_value_init (&v, LRG_TYPE_AUDIO_ROLLOFF);
	g_value_set_enum (&v, LRG_AUDIO_ROLLOFF_CUSTOM);
	g_assert_true (lrg_property_source_set_value (src, "rolloff-mode", &v));
	g_value_unset (&v);
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "inspector-audio-curve")));
	{
		LrgCurveEdit              *curve = LRG_CURVE_EDIT (efx_find (f, "inspector-audio-curve"));
		g_autoptr(LrgKeyframeCurve) k = lrg_keyframe_curve_new ();

		lrg_keyframe_curve_add_key (k, 0.0f, 1.0f, LRG_EASING_LINEAR);
		lrg_keyframe_curve_add_key (k, 0.5f, 0.2f, LRG_EASING_LINEAR);
		lrg_keyframe_curve_add_key (k, 1.0f, 0.0f, LRG_EASING_LINEAR);
		undo = lrg_editor_get_undo_count (f->editor);
		g_signal_emit_by_name (curve, "edit-begin");
		lrg_curve_edit_set_curve (curve, k);
		g_signal_emit_by_name (curve, "curve-changed");
		g_signal_emit_by_name (curve, "edit-end", TRUE);
		g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo + 1);
		g_assert_nonnull (lrg_node_visual_get_param (visual, "rolloff_curve_keys"));
		g_assert_nonnull (strstr (g_value_get_string (lrg_node_visual_get_param (visual, "rolloff_curve_keys")),
		                          "0.5:0.2"));
	}

	/* audio.get / audio.set / audio.curve / audio.buses */
	r = efx_ipc_result (f, "{'id':1,'method':'audio.set','params':{'node':'Radio','params':{'min_distance':3,"
	                       "'max_distance':30,'rolloff_mode':'linear','cone_outer_angle':90,'occlusion':true,"
	                       "'bus':'dialogue'}}}");
	params = json_object_get_object_member (json_node_get_object (r), "params");
	g_assert_cmpfloat (json_object_get_double_member (params, "max_distance"), ==, 30.0);
	g_assert_cmpstr (json_object_get_string_member (params, "rolloff_mode"), ==, "linear");
	g_assert_cmpstr (json_object_get_string_member (params, "bus"), ==, "dialogue");
	g_assert_true (json_object_get_boolean_member (params, "occlusion"));
	g_assert_cmpfloat (lrg_node_visual_get_param_double (visual, "radius", 0.0), ==, 30.0);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'audio.curve','params':{'node':'Radio','samples':4}}");
	{
		JsonArray *pts = json_object_get_array_member (json_node_get_object (r), "points");

		g_assert_cmpuint (json_array_get_length (pts), ==, 4);
		/* linear: full at 0, silent past the max */
		g_assert_cmpfloat (json_array_get_double_element (json_array_get_array_element (pts, 0), 1), ==, 1.0);
		g_assert_cmpfloat (json_array_get_double_element (json_array_get_array_element (pts, 3), 1), ==, 0.0);
	}
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'audio.buses'}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "buses")), ==, 4);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'audio.set','params':{'node':'Radio','params':{'bus':null}}}");
	g_assert_null (lrg_node_visual_get_param (visual, "bus"));
	g_clear_pointer (&r, json_node_unref);
	{
		g_autofree gchar *err = efx_ipc (f, "{'id':1,'method':'audio.set','params':{'node':'Radio','params':"
		                                    "{'wobble':1}}}");

		g_assert_nonnull (strstr (err, "wobble"));
	}

	/* not an emitter: no section; audio.get refuses */
	{
		LrgNode          *cube = create_node (f, "{'name':'Box','kind':'primitive','primitive':'cube'}");
		g_autofree gchar *err = efx_ipc (f, "{'id':1,'method':'audio.get','params':{'node':'Box'}}");

		lrg_editor_select (f->editor, cube, FALSE);
		efx_frames (f, 2);
		g_assert_false (lrg_widget_get_visible (section));
		g_assert_nonnull (strstr (err, "not an audio emitter"));
	}
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor-app/audio/fader-law", test_fader_law);
	g_test_add ("/editor-app/audio/strips", EditorAppFixture, NULL, efx_setup, test_strips, efx_teardown);
	g_test_add ("/editor-app/audio/grids", EditorAppFixture, NULL, efx_setup, test_grids, efx_teardown);
	g_test_add ("/editor-app/audio/preview", EditorAppFixture, NULL, efx_setup, test_preview, efx_teardown);
	g_test_add ("/editor-app/audio/ipc", EditorAppFixture, NULL, efx_setup, test_ipc, efx_teardown);
	g_test_add ("/editor-app/audio/panel", EditorAppFixture, NULL, efx_setup, test_panel, efx_teardown);
	g_test_add ("/editor-app/audio/gizmo", EditorAppFixture, NULL, efx_setup, test_gizmo, efx_teardown);
	g_test_add ("/editor-app/audio/inspector", EditorAppFixture, NULL, efx_setup, test_inspector, efx_teardown);
	return g_test_run ();
}
