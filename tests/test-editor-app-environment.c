/* test-editor-app-environment.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The lrgeditor environment panel and the .rpost post-stack editor:
 * live, undoable editing of the level's environment block (grid rows,
 * gesture merging, rows by sky source), environment.* IPC, the sky
 * preview, level round trip, and the post-stack editor (effects add /
 * move / remove / params, undo, save, live viewport preview, save current
 * look, assign to the level) with the post.* IPC. GPU parts (a bake with
 * progress) run only under a display (xrun.sh).
 */

#include "editor-app-fixture.h"
#include <raylib.h>
#include <math.h>

static gboolean gpu_ready = FALSE;

/* The panel docked as a tab (the window docks it once the shell does;
 * until then the test docks it itself). */
static LrgEditorEnvironmentPanel *
env_panel (EditorAppFixture *f)
{
	LrgEditorEnvironmentPanel *panel = lrg_editor_environment_panel_find (f->app);

	if (panel == NULL)
	{
		LrgDockPanel *dock = lrg_dock_panel_new (LRG_EDITOR_ENVIRONMENT_PANEL_ID, "Environment");

		panel = lrg_editor_environment_panel_new (f->app);
		lrg_dock_panel_set_child (dock, LRG_WIDGET (panel));
		g_object_unref (panel);
		lrg_editor_app_add_panel (f->app, dock, "tab:inspector");
	}
	efx_frames (f, 2);
	return panel;
}

/* Brings the environment tab (when docked) to the front and lays the
 * window out. */
static void
present_env (EditorAppFixture *f)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *p = lrg_dock_area_get_panel (dock, LRG_EDITOR_ENVIRONMENT_PANEL_ID);

	if (p != NULL)
		lrg_dock_area_present_panel (dock, p);
	efx_frames (f, 3);
}

static LrgLevel *
level_of (EditorAppFixture *f)
{
	return lrg_editor_get_level (f->editor);
}

static void
set_double (LrgEditorEnvironmentPanel *panel,
            const gchar               *name,
            gdouble                    v)
{
	g_auto(GValue) value = G_VALUE_INIT;

	g_value_init (&value, G_TYPE_DOUBLE);
	g_value_set_double (&value, v);
	g_assert_true (lrg_editor_environment_panel_set_field (panel, name, &value));
}

/* ==========================================================================
 * Environment panel
 * ========================================================================== */

/* Add / remove are commands; the empty state and grid follow. */
static void
test_add_remove (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorEnvironmentPanel *panel;
	LrgWidget                 *empty;

	lrg_editor_new_level (f->editor, "env");
	panel = env_panel (f);
	g_assert_null (lrg_editor_environment_panel_get_environment (panel));
	empty = efx_find (f, "environment-empty");
	g_assert_nonnull (empty);
	g_assert_true (lrg_widget_get_visible (empty));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "environment-scroll")));

	/* The empty state's chip adds a block (one undo step) */
	present_env (f);
	efx_click_widget (f, efx_find (f, "environment-add"));
	g_assert_nonnull (lrg_level_get_environment (level_of (f)));
	g_assert_true (lrg_widget_get_visible (efx_find (f, "environment-scroll")));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "environment-empty")));
	lrg_editor_undo (f->editor);
	g_assert_null (lrg_level_get_environment (level_of (f)));
	lrg_editor_redo (f->editor);
	g_assert_nonnull (lrg_level_get_environment (level_of (f)));

	/* Remove, undo brings the same settings back */
	set_double (panel, "exposure", 1.5);
	g_assert_true (lrg_editor_environment_panel_remove_environment (panel));
	g_assert_null (lrg_level_get_environment (level_of (f)));
	lrg_editor_undo (f->editor);
	g_assert_nonnull (lrg_level_get_environment (level_of (f)));
	g_assert_cmpfloat_with_epsilon (lrg_environment_get_exposure (lrg_level_get_environment (level_of (f))), 1.5f,
	                                1e-4f);
	g_assert_false (lrg_editor_environment_panel_add_environment (panel));
}

/* Field edits: one step each, the live object kept, a gesture merged. */
static void
test_field_edits (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorEnvironmentPanel *panel;
	LrgEnvironment            *env;
	LrgPropertyGrid           *grid;
	g_auto(GValue)             v = G_VALUE_INIT;
	gfloat                     rgba[4];
	guint                      gesture = 4242;

	lrg_editor_new_level (f->editor, "env");
	panel = env_panel (f);

	/* The first edit creates the block in the same step */
	set_double (panel, "exposure", 2.0);
	env = lrg_level_get_environment (level_of (f));
	g_assert_nonnull (env);
	g_assert_cmpfloat_with_epsilon (lrg_environment_get_exposure (env), 2.0f, 1e-4f);
	lrg_editor_undo (f->editor);
	g_assert_null (lrg_level_get_environment (level_of (f)));
	lrg_editor_redo (f->editor);
	env = lrg_level_get_environment (level_of (f));

	/* Undo of a fog tweak keeps the object (and its GPU maps) */
	set_double (panel, "fog-density", 0.07);
	lrg_editor_undo (f->editor);
	g_assert_true (lrg_level_get_environment (level_of (f)) == env);
	lrg_environment_get_fog (env, NULL, rgba, NULL, NULL, NULL);

	/* A grid gesture (a slider drag) is one undo step */
	grid = lrg_editor_environment_panel_get_grid (panel);
	g_signal_emit_by_name (grid, "edit-begin", "rotation", gesture);
	set_double (panel, "rotation", 10.0);
	set_double (panel, "rotation", 20.0);
	set_double (panel, "rotation", 30.0);
	g_signal_emit_by_name (grid, "edit-end", "rotation", gesture, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_environment_get_rotation (env), 30.0f, 1e-4f);
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (lrg_environment_get_rotation (env), 0.0f, 1e-4f);

	/* Enums by nick, colours as GrlColor */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "color");
	g_assert_true (lrg_editor_environment_panel_set_field (panel, "sky-source", &v));
	g_assert_cmpint (lrg_environment_get_sky_source (env), ==, LRG_ENVIRONMENT_SKY_COLOR);
	g_value_unset (&v);
	g_value_init (&v, GRL_TYPE_COLOR);
	g_value_take_boxed (&v, grl_color_new (255, 0, 0, 255));
	g_assert_true (lrg_editor_environment_panel_set_field (panel, "sky-color", &v));
	{
		gfloat rgb[3];

		lrg_environment_get_sky_color (env, rgb);
		g_assert_cmpfloat_with_epsilon (rgb[0], 1.0f, 1e-3f);
		g_assert_cmpfloat_with_epsilon (rgb[1], 0.0f, 1e-3f);
	}
	g_assert_false (lrg_editor_environment_panel_set_field (panel, "no-such-field", &v));
}

/* Rows follow the sky source and the fog / volumetric switches. */
static void
test_rows_by_source (EditorAppFixture *f,
                     gconstpointer     data)
{
	LrgEditorEnvironmentPanel *panel;
	LrgPropertyGrid           *grid;
	g_auto(GValue)             v = G_VALUE_INIT;

	lrg_editor_new_level (f->editor, "env");
	panel = env_panel (f);
	g_assert_true (lrg_editor_environment_panel_add_environment (panel));
	efx_frames (f, 2);
	grid = lrg_editor_environment_panel_get_grid (panel);

	/* gradient: zenith/horizon/ground, no flat colour or image */
	g_assert_nonnull (lrg_property_grid_get_row (grid, "zenith"));
	g_assert_null (lrg_property_grid_get_row (grid, "sky-color"));
	g_assert_null (lrg_property_grid_get_row (grid, "sky-path"));
	g_assert_null (lrg_property_grid_get_row (grid, "fog-density"));
	g_assert_null (lrg_property_grid_get_row (grid, "volumetric-steps"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "post-stack"));

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "equirect");
	g_assert_true (lrg_editor_environment_panel_set_field (panel, "sky-source", &v));
	/* rows are rebuilt at once; no frame runs with an imageless
	 * equirect sky (a GPU viewport would warn while baking it) */
	g_assert_nonnull (lrg_property_grid_get_row (grid, "sky-path"));
	g_assert_null (lrg_property_grid_get_row (grid, "zenith"));
	g_value_set_string (&v, "gradient");
	g_assert_true (lrg_editor_environment_panel_set_field (panel, "sky-source", &v));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "zenith"));

	g_value_unset (&v);
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, TRUE);
	g_assert_true (lrg_editor_environment_panel_set_field (panel, "fog-enabled", &v));
	g_assert_true (lrg_editor_environment_panel_set_field (panel, "volumetric-enabled", &v));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "fog-density"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "volumetric-steps"));
}

/* The preview strip samples the engine's sky: a gradient is lighter
 * up than down. */
static void
test_preview (EditorAppFixture *f,
              gconstpointer     data)
{
	LrgEditorEnvironmentPanel *panel;
	gfloat                     top[4], bottom[4];

	lrg_editor_new_level (f->editor, "env");
	panel = env_panel (f);
	g_assert_true (lrg_editor_environment_panel_add_environment (panel));
	lrg_editor_environment_panel_sample_preview (panel, 0.5f, 0.02f, top);
	lrg_editor_environment_panel_sample_preview (panel, 0.5f, 0.98f, bottom);
	g_assert_cmpfloat (top[2], >, bottom[2]);
	g_assert_cmpfloat (top[3], ==, 1.0f);

	/* More exposure is brighter */
	set_double (panel, "exposure", 2.0);
	{
		gfloat bright[4];

		lrg_editor_environment_panel_sample_preview (panel, 0.5f, 0.02f, bright);
		g_assert_cmpfloat (bright[2] + bright[1], >, top[2] + top[1]);
	}
}

/* environment.* IPC: get, a two-key set as one step, reset, bake without
 * a window. */
static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	g_autofree gchar *reply = NULL;
	JsonObject       *o;
	JsonObject       *env;

	lrg_editor_new_level (f->editor, "env");
	env_panel (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'environment.get'}");

		g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "present"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':2,'method':'environment.set','params':{'values':{'fog.enabled':true,'fog.density':0.05,"
			   "'sky.zenith':[0.1,0.2,0.9],'post_stack':'looks/dusk.rpost'}}}");

		o = json_node_get_object (r);
		g_assert_true (json_object_get_boolean_member (o, "present"));
		env = json_object_get_object_member (o, "environment");
		g_assert_true (json_object_get_boolean_member (json_object_get_object_member (env, "fog"), "enabled"));
		g_assert_cmpfloat_with_epsilon (
			json_object_get_double_member (json_object_get_object_member (env, "fog"), "density"), 0.05, 1e-6);
		g_assert_cmpstr (json_object_get_string_member (env, "post_stack"), ==, "looks/dusk.rpost");
	}
	g_assert_cmpstr (lrg_environment_get_post_stack (lrg_level_get_environment (level_of (f))), ==,
	                 "looks/dusk.rpost");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':3,'method':'environment.set','params':{'key':'exposure','value':-1.0}}");
	}
	g_assert_cmpfloat_with_epsilon (lrg_environment_get_exposure (lrg_level_get_environment (level_of (f))), -1.0f,
	                                1e-4f);
	/* one undo per set */
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (lrg_environment_get_exposure (lrg_level_get_environment (level_of (f))), 0.0f,
	                                1e-4f);
	lrg_editor_undo (f->editor);
	g_assert_null (lrg_level_get_environment (level_of (f)));
	lrg_editor_redo (f->editor);

	/* errors */
	reply = efx_ipc (f, "{'id':4,'method':'environment.set','params':{}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);
	if (!gpu_ready)
	{
		reply = efx_ipc (f, "{'id':5,'method':'environment.bake'}");
		g_assert_nonnull (strstr (reply, "window"));
		g_clear_pointer (&reply, g_free);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':6,'method':'environment.progress'}");

		g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "baking"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':7,'method':'environment.reset'}");

		g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "present"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':8,'method':'environment.show'}");

		g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "panel"), ==, "environment");
	}
}

/* What the panel edits is what the level file stores. */
static void
test_level_round_trip (EditorAppFixture *f,
                       gconstpointer     data)
{
	LrgEditorEnvironmentPanel *panel;
	g_autofree gchar          *path = g_build_filename (f->dir, "look.rlevel", NULL);
	g_autoptr(GError)          error = NULL;
	g_autoptr(LrgLevel)        loaded = NULL;
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	LrgFogType                 type;

	lrg_editor_new_level (f->editor, "env");
	panel = env_panel (f);
	set_double (panel, "exposure", 0.75);
	set_double (panel, "ambient-intensity", 0.4);
	{
		g_auto(GValue) on = G_VALUE_INIT;

		g_value_init (&on, G_TYPE_BOOLEAN);
		g_value_set_boolean (&on, TRUE);
		g_assert_true (lrg_editor_environment_panel_set_field (panel, "fog-enabled", &on));
	}
	g_assert_true (lrg_level_serializer_save_to_file (ser, level_of (f), path, &error));
	g_assert_no_error (error);
	loaded = lrg_level_serializer_load_from_file (ser, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (lrg_level_get_environment (loaded));
	g_assert_cmpfloat_with_epsilon (lrg_environment_get_exposure (lrg_level_get_environment (loaded)), 0.75f, 1e-4f);
	g_assert_true (lrg_environment_get_fog (lrg_level_get_environment (loaded), &type, NULL, NULL, NULL, NULL));
}

/* The panel's chips: Bake is insensitive without a block; Remove works. */
static void
test_chips (EditorAppFixture *f,
            gconstpointer     data)
{
	LrgEditorEnvironmentPanel *panel;

	lrg_editor_new_level (f->editor, "env");
	panel = env_panel (f);
	present_env (f);
	g_assert_false (lrg_widget_get_enabled (efx_find (f, "environment-bake")));
	g_assert_true (lrg_editor_environment_panel_add_environment (panel));
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_enabled (efx_find (f, "environment-bake")));
	g_assert_cmpstr (lrg_editor_environment_panel_get_bake_phase (panel), ==, "idle");
	efx_click_widget (f, efx_find (f, "environment-remove"));
	g_assert_null (lrg_level_get_environment (level_of (f)));

	/* actions */
	g_assert_true (lrg_editor_app_activate_action (f->app, "environment.add"));
	g_assert_nonnull (lrg_level_get_environment (level_of (f)));
	g_assert_true (lrg_editor_app_activate_action (f->app, "environment.remove"));
	g_assert_null (lrg_level_get_environment (level_of (f)));
}

/* ==========================================================================
 * Post-stack editor
 * ========================================================================== */

static gchar *
make_rpost (EditorAppFixture *f,
            const gchar      *name)
{
	g_autofree gchar *rel = g_strdup_printf ("looks/%s", name);
	g_autofree gchar *q = g_strdup_printf ("{'id':1,'method':'post.create','params':{'path':'%s'}}", rel);
	g_autoptr(JsonNode) r = efx_ipc_result (f, q);

	return g_strdup (json_object_get_string_member (json_node_get_object (r), "path"));
}

static LrgEditorPostStackEditor *
open_rpost (EditorAppFixture *f,
            const gchar      *path)
{
	g_autoptr(GError)    error = NULL;
	LrgEditorDataEditor *ed = lrg_editor_data_editor_open (f->app, path, &error);

	g_assert_no_error (error);
	g_assert_true (LRG_IS_EDITOR_POST_STACK_EDITOR (ed));
	efx_frames (f, 2);
	return LRG_EDITOR_POST_STACK_EDITOR (ed);
}

static void
project_setup (EditorAppFixture *f)
{
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Look", &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
}

/* Effects: add, move, remove, params; each a document step. */
static void
test_post_editing (EditorAppFixture *f,
                   gconstpointer     data)
{
	g_autofree gchar         *path = NULL;
	LrgEditorPostStackEditor *ed;
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgPostStack)   stack = NULL;
	g_auto(GValue)            v = G_VALUE_INIT;
	LrgEditorDocument        *doc;
	gint                      at;

	project_setup (f);
	path = make_rpost (f, "dusk.rpost");
	g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
	ed = open_rpost (f, path);
	doc = lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed));
	g_assert_cmpuint (lrg_editor_post_stack_editor_get_n_effects (ed), ==, 4);
	g_assert_cmpstr (lrg_editor_post_stack_editor_get_effect_type (ed, 0), ==, "bloom");
	g_assert_cmpint (lrg_editor_post_stack_editor_get_selected (ed), ==, 0);
	g_assert_nonnull (efx_find (f, "post-effect-3"));

	/* add at the front */
	at = lrg_editor_post_stack_editor_add_effect (ed, "vignette", 0, &error);
	g_assert_no_error (error);
	g_assert_cmpint (at, ==, 0);
	g_assert_cmpuint (lrg_editor_post_stack_editor_get_n_effects (ed), ==, 5);
	g_assert_cmpint (lrg_editor_post_stack_editor_get_selected (ed), ==, 0);
	g_assert_cmpint (lrg_editor_post_stack_editor_add_effect (ed, "no-such-effect", -1, &error), ==, -1);
	g_assert_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_UNKNOWN_EFFECT);
	g_clear_error (&error);

	/* a param, the weight and enabled */
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 0.6);
	g_assert_true (lrg_editor_post_stack_editor_set_value (ed, 0, "intensity", &v, &error));
	g_assert_no_error (error);
	g_value_set_double (&v, 0.5);
	g_assert_true (lrg_editor_post_stack_editor_set_value (ed, 0, "weight", &v, &error));
	g_assert_false (lrg_editor_post_stack_editor_set_value (ed, 0, "not-a-param", &v, &error));
	g_assert_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_UNKNOWN_PARAM);
	g_clear_error (&error);
	stack = lrg_editor_post_stack_editor_build_stack (ed, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_post_stack_get_effect_type (stack, 0), ==, "vignette");
	g_assert_cmpfloat_with_epsilon (lrg_post_stack_get_effect_weight (stack, 0), 0.5f, 1e-4f);
	{
		g_auto(GValue) p = G_VALUE_INIT;

		g_assert_true (lrg_post_stack_get_param (stack, 0, "intensity", &p));
		g_assert_cmpfloat_with_epsilon (g_value_get_float (&p), 0.6f, 1e-4f);
	}

	/* move to the end, the selection follows */
	g_assert_true (lrg_editor_post_stack_editor_move_effect (ed, 0, 4, &error));
	g_assert_cmpstr (lrg_editor_post_stack_editor_get_effect_type (ed, 4), ==, "vignette");
	g_assert_cmpint (lrg_editor_post_stack_editor_get_selected (ed), ==, 4);

	/* undo the move, then the weight */
	g_assert_true (lrg_editor_document_undo (doc));
	g_assert_cmpstr (lrg_editor_post_stack_editor_get_effect_type (ed, 0), ==, "vignette");

	/* remove through the row chip */
	present_env (f);
	efx_frames (f, 2);
	efx_click_widget (f, efx_find (f, "post-effect-1-remove"));
	g_assert_cmpuint (lrg_editor_post_stack_editor_get_n_effects (ed), ==, 4);

	/* save and reload from disk */
	g_assert_true (lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (ed), &error));
	g_assert_no_error (error);
	{
		g_autoptr(LrgPostStack) disk = lrg_post_stack_new_from_file (path, &error);

		g_assert_no_error (error);
		g_assert_cmpuint (lrg_post_stack_get_effect_count (disk), ==, 4);
		g_assert_cmpstr (lrg_post_stack_get_effect_type (disk, 0), ==, "vignette");
	}
}

/* Preview drives the viewport's runtime renderer; Look and assign. */
static void
test_post_preview_and_look (EditorAppFixture *f,
                            gconstpointer     data)
{
	g_autofree gchar         *path = NULL;
	g_autofree gchar         *look = NULL;
	LrgEditorPostStackEditor *ed;
	g_autoptr(LrgPostStack)   shown = NULL;
	g_autoptr(GError)         error = NULL;

	project_setup (f);
	path = make_rpost (f, "warm.rpost");
	ed = open_rpost (f, path);

	/* Preview is on by default and follows edits */
	g_assert_true (lrg_editor_post_stack_editor_get_preview (ed));
	/* the viewport's look is the previewed stack */
	shown = lrg_editor_post_stack_capture_look (f->app, -1);
	g_assert_nonnull (shown);
	g_assert_cmpuint (lrg_post_stack_get_effect_count (shown), ==, 4);
	g_clear_object (&shown);
	g_assert_true (lrg_editor_view_state_has_overlay (lrg_editor_app_get_view_state (f->app), -1,
	                                                  LRG_EDITOR_VIEW_OVERLAY_POST));
	g_assert_true (lrg_editor_post_stack_editor_remove_effect (ed, 3, &error));
	shown = lrg_editor_post_stack_capture_look (f->app, -1);
	g_assert_cmpuint (lrg_post_stack_get_effect_count (shown), ==, 3);
	g_assert_cmpstr (lrg_post_stack_get_effect_type (shown, 0), ==, "bloom");

	/* post.save_look writes what the viewport shows */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':1,'method':'post.save_look','params':{'path':'looks/captured.rpost'}}");
		g_autoptr(LrgPostStack) cap = NULL;

		look = g_strdup (json_object_get_string_member (json_node_get_object (r), "path"));
		cap = lrg_post_stack_new_from_file (look, &error);
		g_assert_no_error (error);
		g_assert_cmpuint (lrg_post_stack_get_effect_count (cap), ==, 3);
	}

	/* Look replaces the document (one undo step) */
	g_assert_true (lrg_editor_post_stack_editor_add_effect (ed, "sharpen", -1, &error) >= 0 || error != NULL);
	g_clear_error (&error);
	g_assert_true (lrg_editor_post_stack_editor_save_current_look (ed, &error));
	g_assert_no_error (error);

	/* toggling the chip stops the preview */
	present_env (f);
	efx_frames (f, 2);
	efx_click_widget (f, efx_find (f, "post-preview"));
	g_assert_false (lrg_editor_post_stack_editor_get_preview (ed));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':3,'method':'post.preview'}");

		g_assert_true (json_object_get_null_member (json_node_get_object (r), "preview"));
	}

	/* Use for Level: the level environment names the file */
	env_panel (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':2,'method':'post.assign','params':{'path':'looks/warm.rpost'}}");

		g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "post_stack"), ==,
		                 "looks/warm.rpost");
	}
	g_assert_cmpstr (lrg_environment_get_post_stack (lrg_level_get_environment (lrg_editor_get_level (f->editor))),
	                 ==, "looks/warm.rpost");
}

/* post.* IPC end to end. */
static void
test_post_ipc (EditorAppFixture *f,
               gconstpointer     data)
{
	g_autofree gchar *reply = NULL;
	JsonArray        *effects;
	JsonArray        *types;
	guint             i;
	gboolean          bloom = FALSE;

	project_setup (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'post.effect_types'}");

		types = json_object_get_array_member (json_node_get_object (r), "types");
		for (i = 0; i < json_array_get_length (types); i++)
		{
			JsonObject *t = json_array_get_object_element (types, i);

			if (g_strcmp0 (json_object_get_string_member (t, "type"), "bloom") == 0)
			{
				bloom = TRUE;
				g_assert_cmpuint (json_array_get_length (json_object_get_array_member (t, "params")), >, 2);
			}
		}
		g_assert_true (bloom);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':2,'method':'post.create','params':{'path':'looks/empty','preset':'empty'}}");

		g_assert_true (g_str_has_suffix (json_object_get_string_member (json_node_get_object (r), "path"),
		                                 "looks/empty.rpost"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':3,'method':'post.add','params':{'path':'looks/empty.rpost','type':'tonemap'}}");

		g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "index"), ==, 0);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':4,'method':'post.add','params':{'path':'looks/empty.rpost','type':'vignette','index':0}}");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':5,'method':'post.set','params':{'path':'looks/empty.rpost','index':1,'key':'exposure',"
			   "'value':0.5}}");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':6,'method':'post.move','params':{'path':'looks/empty.rpost','from':1,'to':0}}");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':7,'method':'post.get','params':{'path':'looks/empty.rpost'}}");
		JsonObject *first;

		effects = json_object_get_array_member (json_node_get_object (r), "effects");
		g_assert_cmpuint (json_array_get_length (effects), ==, 2);
		first = json_array_get_object_element (effects, 0);
		g_assert_cmpstr (json_object_get_string_member (first, "type"), ==, "tonemap");
		g_assert_cmpfloat_with_epsilon (
			json_object_get_double_member (json_object_get_object_member (first, "params"), "exposure"), 0.5, 1e-4);
		g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "dirty"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':8,'method':'post.remove','params':{'path':'looks/empty.rpost','index':1}}");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':9,'method':'post.save','params':{'path':'looks/empty.rpost'}}");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':10,'method':'post.preview','params':{'enabled':false}}");

		g_assert_true (json_object_get_null_member (json_node_get_object (r), "preview"));
	}
	reply = efx_ipc (f, "{'id':11,'method':'post.add','params':{'path':'looks/empty.rpost','type':'nope'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);
	reply = efx_ipc (f, "{'id':12,'method':'post.get','params':{'path':'looks/missing.rpost'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
}

/* ==========================================================================
 * GPU: bake with progress (xrun.sh only)
 * ========================================================================== */

static void
test_gpu_bake (EditorAppFixture *f,
               gconstpointer     data)
{
	LrgEditorEnvironmentPanel *panel;
	LrgEnvironment            *env;
	g_autoptr(GError)          error = NULL;
	gdouble                    last = 0.0;
	gint                       guard = 0;

	if (!gpu_ready)
	{
		g_test_skip ("no display (run under xrun.sh)");
		return;
	}
	lrg_editor_new_level (f->editor, "env");
	panel = env_panel (f);
	g_assert_true (lrg_editor_environment_panel_add_environment (panel));
	env = lrg_editor_environment_panel_get_environment (panel);
	g_assert_false (lrg_environment_is_baked (env));
	lrg_environment_set_resolution (env, 32);

	g_assert_true (lrg_editor_environment_panel_bake (panel, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_environment_panel_get_baking (panel));
	g_assert_false (lrg_editor_environment_panel_bake (panel, &error));
	g_clear_error (&error);
	while (lrg_editor_environment_panel_step_bake (panel) && guard++ < 10)
	{
		g_assert_cmpfloat (lrg_editor_environment_panel_get_bake_progress (panel), >=, last);
		last = lrg_editor_environment_panel_get_bake_progress (panel);
	}
	g_assert_false (lrg_editor_environment_panel_get_baking (panel));
	g_assert_cmpstr (lrg_editor_environment_panel_get_bake_phase (panel), ==, "done");
	g_assert_null (lrg_editor_environment_panel_get_bake_error (panel));
	g_assert_cmpfloat (lrg_editor_environment_panel_get_bake_progress (panel), ==, 1.0);
	g_assert_true (lrg_environment_is_baked (env));
	g_assert_false (lrg_environment_needs_bake (env));

	/* A sky change needs a bake again; a fog change does not */
	set_double (panel, "fog-density", 0.1);
	g_assert_false (lrg_environment_needs_bake (env));
	set_double (panel, "exposure", 1.0);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'environment.progress'}");

		g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "baked"));
	}
	lrg_environment_release_gpu (env);
}

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-editor-app-environment");
	return IsWindowReady ();
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	gpu_ready = gpu_init ();

	g_test_add ("/editor-app/environment/add-remove", EditorAppFixture, NULL, efx_setup, test_add_remove,
	            efx_teardown);
	g_test_add ("/editor-app/environment/field-edits", EditorAppFixture, NULL, efx_setup, test_field_edits,
	            efx_teardown);
	g_test_add ("/editor-app/environment/rows-by-source", EditorAppFixture, NULL, efx_setup, test_rows_by_source,
	            efx_teardown);
	g_test_add ("/editor-app/environment/preview", EditorAppFixture, NULL, efx_setup, test_preview, efx_teardown);
	g_test_add ("/editor-app/environment/ipc", EditorAppFixture, NULL, efx_setup, test_ipc, efx_teardown);
	g_test_add ("/editor-app/environment/level-round-trip", EditorAppFixture, NULL, efx_setup,
	            test_level_round_trip, efx_teardown);
	g_test_add ("/editor-app/environment/chips", EditorAppFixture, NULL, efx_setup, test_chips, efx_teardown);
	g_test_add ("/editor-app/post-stack/editing", EditorAppFixture, NULL, efx_setup, test_post_editing,
	            efx_teardown);
	g_test_add ("/editor-app/post-stack/preview-look", EditorAppFixture, NULL, efx_setup,
	            test_post_preview_and_look, efx_teardown);
	g_test_add ("/editor-app/post-stack/ipc", EditorAppFixture, NULL, efx_setup, test_post_ipc, efx_teardown);
	g_test_add ("/editor-app/environment/gpu-bake", EditorAppFixture, NULL, efx_setup, test_gpu_bake, efx_teardown);

	result = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return result;
}
