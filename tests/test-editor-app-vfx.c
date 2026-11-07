/* test-editor-app-vfx.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 VFX tools of lrgeditor (EDFX), in a headless LrgEditorApp
 * (tests/editor-app-fixture.h):
 *
 *  - the particle editor (.rparticles): create from the templates, open
 *    through the data editor registry, emitters (add, duplicate, remove,
 *    reorder, mute), the module stack (add in stage order, reorder,
 *    enable, remove; sub-emitter targets), set_value through the strict
 *    loader (bad values fail with the key path and change nothing, renames
 *    carry sub-emitter references, a missing renderer block is created),
 *    a grid drag is one undo step and the parsed asset survives it, the
 *    preview (play, pause, deterministic seek, speed, seed override,
 *    solo, the GPU toggle and its subset messages), unknown keys become
 *    warnings, the canvas buttons;
 *  - the weather editor (.rweather): create, presets, layers, intensity
 *    and enable, kind parameters (typed and file syntax, unset), the
 *    parameter grid generated from the kind schema, transitions, the
 *    preview (preset sync after edits, crossfade with the rule's
 *    duration), validation;
 *  - every particles.* and weather.* IPC method and its errors;
 *  - /gpu/: the previews render (only under the isolated Xvfb of
 *    build-aux/xrun.sh); with LRG_TEST_SHOT_DIR set the frames are saved.
 */

#include "editor-app-fixture.h"
#include <raylib.h>
#include <math.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* Opens a project (once) and returns the absolute path of @rel. */
static gchar *
project_path (EditorAppFixture *f,
              const gchar      *rel)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = NULL;
	gchar            *path;

	if (lrg_editor_app_get_project_dir (f->app) == NULL)
	{
		g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Demo", &error));
		g_assert_no_error (error);
		efx_frames (f, 2);
	}
	path = g_build_filename (f->project, rel, NULL);
	dir = g_path_get_dirname (path);
	g_mkdir_with_parents (dir, 0755);
	return path;
}

static gboolean
loader_warnings_ok (const gchar    *domain,
                    GLogLevelFlags  level,
                    const gchar    *message,
                    gpointer        user_data)
{
	return !(domain != NULL && g_str_has_prefix (domain, "Libregnum-") && (level & G_LOG_LEVEL_WARNING) &&
	         g_strcmp0 (domain, "Libregnum-Editor") != 0);
}

static LrgEditorDocument *
pdoc (LrgEditorParticleEditor *ed)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed));
}

static gchar *
ptext (LrgEditorParticleEditor *ed)
{
	return lrg_editor_document_to_data (pdoc (ed), NULL);
}

static LrgEditorDocument *
wdoc (LrgEditorWeatherEditor *ed)
{
	return lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed));
}

static gchar *
wtext (LrgEditorWeatherEditor *ed)
{
	return lrg_editor_document_to_data (wdoc (ed), NULL);
}

static LrgWidget *
find_in (gpointer     editor,
         const gchar *name)
{
	return lrg_widget_find (LRG_WIDGET (editor), name);
}

/* fx/fire.rparticles from the default template, opened. */
static LrgEditorParticleEditor *
fire_effect (EditorAppFixture *f)
{
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *path = project_path (f, "fx/fire.rparticles");
	LrgEditorParticleEditor *ed;

	if (!g_file_test (path, G_FILE_TEST_EXISTS))
	{
		g_assert_true (lrg_editor_particle_editor_create (f->app, "fx/fire.rparticles", NULL, &error));
		g_assert_no_error (error);
	}
	ed = lrg_editor_particle_editor_open (f->app, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (ed);
	efx_frames (f, 3);
	return ed;
}

/* data/sky.rweather from the default template, opened. */
static LrgEditorWeatherEditor *
sky_weather (EditorAppFixture *f)
{
	g_autoptr(GError)       error = NULL;
	g_autofree gchar       *path = project_path (f, "data/sky.rweather");
	LrgEditorWeatherEditor *ed;

	if (!g_file_test (path, G_FILE_TEST_EXISTS))
	{
		g_assert_true (lrg_editor_weather_editor_create (f->app, "data/sky.rweather", NULL, &error));
		g_assert_no_error (error);
	}
	ed = lrg_editor_weather_editor_open (f->app, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (ed);
	efx_frames (f, 3);
	return ed;
}

static guint
undo_all (LrgEditorDataEditor *ed)
{
	guint n = 0;

	while (lrg_editor_data_editor_undo (ed))
		n++;
	return n;
}

/* An IPC call expected to fail with @code (the reply's code nick). */
static void
ipc_fails (EditorAppFixture *f,
           const gchar      *request,
           const gchar      *code)
{
	g_autofree gchar     *reply = efx_ipc (f, request);
	g_autoptr(JsonParser) parser = json_parser_new ();
	JsonObject           *o, *err;

	g_assert_true (json_parser_load_from_data (parser, reply, -1, NULL));
	o = json_node_get_object (json_parser_get_root (parser));
	if (!json_object_has_member (o, "error"))
		g_error ("expected an error: %s -> %s", request, reply);
	err = json_object_get_object_member (o, "error");
	if (g_strcmp0 (json_object_get_string_member (err, "code"), code) != 0)
		g_error ("%s -> %s (wanted code %s)", request, reply, code);
}

/* ==========================================================================
 * Particles
 * ========================================================================== */

static void
test_particles_formats (EditorAppFixture *f,
                        gconstpointer     data)
{
	g_autofree gchar *p = project_path (f, "fx/a.rparticles");
	g_autofree gchar *w = project_path (f, "data/a.rweather");
	g_autoptr(GError) error = NULL;

	g_assert_true (lrg_editor_data_editor_lookup_type (LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES) ==
	               LRG_TYPE_EDITOR_PARTICLE_EDITOR);
	g_assert_true (lrg_editor_data_editor_lookup_type (LRG_EDITOR_DOCUMENT_FORMAT_WEATHER) ==
	               LRG_TYPE_EDITOR_WEATHER_EDITOR);
	g_assert_true (lrg_editor_particle_editor_create (f->app, p, "empty", &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_weather_editor_create (f->app, w, "empty", &error));
	g_assert_no_error (error);
	g_assert_true (lrg_editor_data_editor_can_open (p));
	g_assert_true (lrg_editor_data_editor_can_open (w));
	/* existing files and unknown templates are refused */
	g_assert_false (lrg_editor_particle_editor_create (f->app, p, NULL, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
	g_clear_error (&error);
	g_assert_false (lrg_editor_particle_editor_create (f->app, "fx/b.rparticles", "nope", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_false (lrg_editor_weather_editor_create (f->app, "data/b.rweather", "nope", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	/* a particle file is not a weather file */
	g_assert_null (lrg_editor_weather_editor_open (f->app, p, &error));
	g_assert_nonnull (error);
}

static void
test_particles_open (EditorAppFixture *f,
                     gconstpointer     data)
{
	LrgEditorParticleEditor *ed = fire_effect (f);
	LrgParticleAsset        *a = lrg_editor_particle_editor_get_asset (ed);
	LrgParticleAssetEmitter *e;
	g_autofree gchar        *state = NULL;

	g_assert_nonnull (a);
	g_assert_null (lrg_editor_particle_editor_get_load_error (ed));
	g_assert_cmpstr (lrg_particle_asset_get_name (a), ==, "fire");
	g_assert_cmpuint (lrg_particle_asset_get_n_emitters (a), ==, 1);
	e = lrg_particle_asset_get_emitter (a, 0);
	g_assert_cmpstr (lrg_particle_asset_emitter_get_name (e), ==, "main");
	g_assert_cmpuint (lrg_particle_asset_emitter_get_n_modules (e), ==, 9);
	/* the first emitter is selected, the preview plays */
	g_assert_cmpint (lrg_editor_particle_editor_get_selected_emitter (ed), ==, 0);
	g_assert_cmpint (lrg_editor_particle_editor_get_selected_module (ed), ==, -1);
	g_assert_nonnull (lrg_editor_particle_editor_get_effect (ed));
	g_assert_true (lrg_editor_particle_editor_get_playing (ed));
	efx_frames (f, 30);
	g_assert_cmpuint (lrg_editor_particle_editor_get_active_count (ed, -1), >, 0);
	/* the widgets exist and the module stack shows the 9 modules by stage */
	g_assert_nonnull (find_in (ed, "particles-emitter-0"));
	g_assert_nonnull (find_in (ed, "particles-module-8"));
	g_assert_nonnull (find_in (ed, "particles-preview-viewport"));
	state = lrg_editor_data_editor_get_state (LRG_EDITOR_DATA_EDITOR (ed));
	g_assert_nonnull (strstr (state, "emitters=1"));
	/* opening again presents the same tab */
	g_assert_true (lrg_editor_particle_editor_open (f->app, "fx/fire.rparticles", NULL) == ed);
}

static void
test_particles_emitters (EditorAppFixture *f,
                         gconstpointer     data)
{
	LrgEditorParticleEditor *ed = fire_effect (f);
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *start = ptext (ed);
	LrgParticleAsset        *a;
	gint                     i;

	/* add: numbered names, selected, one undo step each */
	i = lrg_editor_particle_editor_add_emitter (ed, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (i, ==, 1);
	i = lrg_editor_particle_editor_add_emitter (ed, "smoke", &error);
	g_assert_no_error (error);
	g_assert_cmpint (i, ==, 2);
	g_assert_cmpint (lrg_editor_particle_editor_get_selected_emitter (ed), ==, 2);
	g_assert_cmpint (lrg_editor_particle_editor_add_emitter (ed, "smoke", &error), ==, -1);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
	g_clear_error (&error);
	a = lrg_editor_particle_editor_get_asset (ed);
	g_assert_cmpstr (lrg_particle_asset_emitter_get_name (lrg_particle_asset_get_emitter (a, 1)), ==, "emitter");
	g_assert_cmpuint (lrg_particle_asset_emitter_get_n_modules (lrg_particle_asset_get_emitter (a, 2)), ==, 3);

	/* duplicate inserts a copy after the original */
	i = lrg_editor_particle_editor_duplicate_emitter (ed, 0, &error);
	g_assert_no_error (error);
	g_assert_cmpint (i, ==, 1);
	a = lrg_editor_particle_editor_get_asset (ed);
	g_assert_cmpstr (lrg_particle_asset_emitter_get_name (lrg_particle_asset_get_emitter (a, 1)), ==, "main copy");
	g_assert_cmpuint (lrg_particle_asset_emitter_get_n_modules (lrg_particle_asset_get_emitter (a, 1)), ==, 9);

	/* move and remove */
	g_assert_true (lrg_editor_particle_editor_move_emitter (ed, 3, 0, &error));
	g_assert_no_error (error);
	a = lrg_editor_particle_editor_get_asset (ed);
	g_assert_cmpstr (lrg_particle_asset_emitter_get_name (lrg_particle_asset_get_emitter (a, 0)), ==, "smoke");
	g_assert_true (lrg_editor_particle_editor_remove_emitter (ed, 0, &error));
	g_assert_no_error (error);
	a = lrg_editor_particle_editor_get_asset (ed);
	g_assert_cmpuint (lrg_particle_asset_get_n_emitters (a), ==, 3);
	g_assert_false (lrg_editor_particle_editor_remove_emitter (ed, 7, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);

	/* mute: the emitter's enabled key */
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "/emitters/0", "enabled", "false", 0, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_particle_asset_emitter_get_enabled (
	                    lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0)));
	efx_frames (f, 2);

	/* six steps, six undos back to the start */
	g_assert_cmpuint (undo_all (LRG_EDITOR_DATA_EDITOR (ed)), ==, 6);
	{
		g_autofree gchar *now = ptext (ed);

		g_assert_cmpstr (now, ==, start);
	}
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_particle_asset_get_n_emitters (lrg_editor_particle_editor_get_asset (ed)), ==, 1);
}

static void
test_particles_modules (EditorAppFixture *f,
                        gconstpointer     data)
{
	LrgEditorParticleEditor *ed = fire_effect (f);
	g_autoptr(GError)        error = NULL;
	LrgParticleAssetEmitter *e;
	LrgParticleModule       *m;
	g_autofree gchar        *target = NULL;
	gint                     i;

	/* drag goes after the last force module (gravity, index 6) */
	i = lrg_editor_particle_editor_add_module (ed, 0, "drag", -1, &error);
	g_assert_no_error (error);
	g_assert_cmpint (i, ==, 7);
	e = lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0);
	g_assert_cmpstr (lrg_particle_module_get_kind (lrg_particle_asset_emitter_get_module (e, 7)), ==, "drag");
	g_assert_cmpint (lrg_editor_particle_editor_get_selected_module (ed), ==, 7);
	/* a burst goes into the spawn stage at the top */
	i = lrg_editor_particle_editor_add_module (ed, 0, "burst", -1, &error);
	g_assert_cmpint (i, ==, 1);
	g_assert_cmpint (lrg_editor_particle_editor_add_module (ed, 0, "warp-drive", -1, &error), ==, -1);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);

	/* a sub-emitter gets a target, which then cannot be removed */
	g_assert_cmpint (lrg_editor_particle_editor_add_emitter (ed, "sparks", &error), ==, 1);
	i = lrg_editor_particle_editor_add_module (ed, 0, "sub-emitter", -1, &error);
	g_assert_no_error (error);
	e = lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0);
	m = lrg_particle_asset_emitter_get_module (e, (guint) i);
	g_object_get (m, "emitter", &target, NULL);
	g_assert_cmpstr (target, ==, "sparks");
	g_assert_false (lrg_editor_particle_editor_remove_emitter (ed, 1, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	/* renaming the target carries the reference along */
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "/emitters/1", "name", "glints", 0, &error));
	g_assert_no_error (error);
	e = lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0);
	g_clear_pointer (&target, g_free);
	g_object_get (lrg_particle_asset_emitter_get_module (e, (guint) i), "emitter", &target, NULL);
	g_assert_cmpstr (target, ==, "glints");

	/* enable, move, remove */
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "/emitters/0/modules/2", "enabled", "false", 0, &error));
	e = lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0);
	g_assert_false (lrg_particle_module_get_enabled (lrg_particle_asset_emitter_get_module (e, 2)));
	g_assert_true (lrg_editor_particle_editor_move_module (ed, 0, 0, 1, &error));
	e = lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0);
	g_assert_cmpstr (lrg_particle_module_get_kind (lrg_particle_asset_emitter_get_module (e, 0)), ==, "burst");
	g_assert_true (lrg_editor_particle_editor_remove_module (ed, 0, 0, &error));
	e = lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0);
	g_assert_cmpstr (lrg_particle_module_get_kind (lrg_particle_asset_emitter_get_module (e, 0)), ==, "spawn-rate");
	g_assert_false (lrg_editor_particle_editor_remove_module (ed, 0, 99, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);

	/* canvas: the stack rows and their buttons exist; clicking a module selects it */
	efx_frames (f, 3);
	efx_click_widget (f, find_in (ed, "particles-module-2"));
	g_assert_cmpint (lrg_editor_particle_editor_get_selected_module (ed), ==, 2);
	efx_frames (f, 2);
	g_assert_nonnull (lrg_property_grid_get_row (LRG_PROPERTY_GRID (find_in (ed, "particles-grid")), "speed"));
}

static void
test_particles_set_value (EditorAppFixture *f,
                          gconstpointer     data)
{
	LrgEditorParticleEditor *ed = fire_effect (f);
	g_autoptr(GError)        error = NULL;
	g_autofree gchar        *before = NULL;
	g_autofree gchar        *after = NULL;
	g_autofree gchar        *saved = NULL;
	LrgParticleAssetEmitter *e;
	LrgParticleModule       *m;
	g_autoptr(GrlVector2)    speed = NULL;

	/* a range in file syntax */
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "/emitters/0/modules/2", "speed", "[2, 4]", 0, &error));
	g_assert_no_error (error);
	e = lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0);
	m = lrg_particle_asset_emitter_get_module (e, 2);
	g_object_get (m, "speed", &speed, NULL);
	g_assert_cmpfloat (speed->x, ==, 2.0f);
	g_assert_cmpfloat (speed->y, ==, 4.0f);

	/* a bad value fails with the loader's key path, nothing changes */
	before = ptext (ed);
	g_assert_false (lrg_editor_particle_editor_set_value (ed, "/emitters/0/modules/2", "speed", "fast", 0, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_assert_nonnull (strstr (error->message, "/emitters/0/modules/2"));
	g_clear_error (&error);
	g_assert_false (lrg_editor_particle_editor_set_value (ed, "/emitters/0/modules/1", "shape", "donut", 0, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_false (lrg_editor_particle_editor_set_value (ed, "/emitters/0", "modules", "[]", 0, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_false (lrg_editor_particle_editor_set_value (ed, "/emitters/5", "enabled", "false", 0, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);
	after = ptext (ed);
	g_assert_cmpstr (before, ==, after);

	/* colours and gradients are quoted for YAML; a default is written out */
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "/emitters/0/modules/5", "color", "#40c0ff", 0, &error));
	g_assert_no_error (error);
	g_clear_pointer (&after, g_free);
	after = ptext (ed);
	g_assert_nonnull (strstr (after, "'#40c0ff'"));
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "/emitters/0/modules/5", "color", NULL, 0, &error));
	g_clear_pointer (&after, g_free);
	after = ptext (ed);
	g_assert_null (strstr (after, "40c0ff"));

	/* the renderer block and the effect settings */
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "/emitters/0/renderer", "kind", "ribbon", 0, &error));
	g_assert_no_error (error);
	e = lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0);
	g_assert_cmpint (lrg_particle_render_settings_get_kind (lrg_particle_asset_emitter_get_renderer (e)), ==,
	                 LRG_PARTICLE_RENDERER_RIBBON);
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "", "seed", "1234", 0, &error));
	g_assert_cmpuint (lrg_particle_asset_get_seed (lrg_editor_particle_editor_get_asset (ed)), ==, 1234);

	/* saved through the engine writer: it loads back */
	g_assert_true (lrg_editor_data_editor_save (LRG_EDITOR_DATA_EDITOR (ed), &error));
	g_assert_no_error (error);
	{
		g_autofree gchar           *path = project_path (f, "fx/fire.rparticles");
		g_autoptr(LrgParticleAsset) back = lrg_particle_asset_new_from_file (path, &error);

		g_assert_no_error (error);
		g_assert_cmpuint (lrg_particle_asset_get_seed (back), ==, 1234);
		g_assert_false (lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (ed)));
		saved = lrg_particle_asset_save_to_data (back, NULL, NULL);
		g_assert_nonnull (strstr (saved, "kind: ribbon"));
	}
}

static void
test_particles_grid (EditorAppFixture *f,
                     gconstpointer     data)
{
	LrgEditorParticleEditor *ed = fire_effect (f);
	LrgPropertyGrid         *grid = LRG_PROPERTY_GRID (find_in (ed, "particles-grid"));
	LrgParticleAsset        *a = lrg_editor_particle_editor_get_asset (ed);
	g_autofree gchar        *start = ptext (ed);
	LrgPropertyRow          *row;
	GValue                   v = G_VALUE_INIT;
	gfloat                   rate = 0.0f;
	guint                    k;

	/* the spawn-rate module in the grid */
	g_assert_true (lrg_editor_particle_editor_select (ed, 0, 0));
	efx_frames (f, 2);
	row = lrg_property_grid_get_row (grid, "rate");
	g_assert_nonnull (row);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "rate-curve"));
	g_value_init (&v, G_TYPE_DOUBLE);
	lrg_property_row_begin_edit (row);
	for (k = 1; k <= 5; k++)
	{
		g_value_set_double (&v, 40.0 + 10.0 * k);
		lrg_property_row_edit_value (row, &v, TRUE);
		efx_frame (f);
	}
	lrg_property_row_end_edit (row, TRUE);
	g_value_unset (&v);
	efx_frames (f, 2);
	/* edited in place: the parsed asset is the same object */
	g_assert_true (lrg_editor_particle_editor_get_asset (ed) == a);
	g_object_get (lrg_particle_asset_emitter_get_module (lrg_particle_asset_get_emitter (a, 0), 0), "rate", &rate,
	              NULL);
	g_assert_cmpfloat_with_epsilon (rate, 90.0f, 1e-4);
	{
		g_autofree gchar *now = ptext (ed);

		g_assert_nonnull (strstr (now, "rate: 90"));
	}
	/* one undo step back to the start */
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frames (f, 1);
	{
		g_autofree gchar *now = ptext (ed);

		g_assert_cmpstr (now, ==, start);
	}
	g_assert_false (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));

	/* the renderer shows the rows of its kind only */
	g_assert_true (lrg_editor_particle_editor_select (ed, 0, -2));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "blend"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "columns"));
	g_assert_null (lrg_property_grid_get_row (grid, "trail-lifetime"));
	/* the emitter hides its renderer object */
	g_assert_true (lrg_editor_particle_editor_select (ed, 0, -1));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "max-particles"));
	g_assert_null (lrg_property_grid_get_row (grid, "renderer"));
	/* an emitter rename to a taken name is refused */
	row = lrg_property_grid_get_row (grid, "name");
	g_assert_nonnull (row);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "");
	lrg_property_row_apply_value (row, &v);
	g_value_unset (&v);
	efx_frames (f, 1);
	g_assert_cmpstr (lrg_particle_asset_emitter_get_name (
	                     lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0)), ==, "main");
	/* the effect settings */
	g_assert_true (lrg_editor_particle_editor_select (ed, -1, 0));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "duration"));
	g_assert_false (lrg_editor_particle_editor_select (ed, 4, -1));
	g_assert_false (lrg_editor_particle_editor_select (ed, 0, 42));
}

static void
test_particles_preview (EditorAppFixture *f,
                        gconstpointer     data)
{
	LrgEditorParticleEditor *ed = fire_effect (f);
	g_autoptr(GError)        error = NULL;
	guint                    a, b, c;
	GrlColor                 bg = { 200, 210, 220, 9 };
	GrlColor                 got;
	g_autofree gchar        *msg = NULL;

	/* deterministic seek */
	lrg_editor_particle_editor_set_playing (ed, FALSE);
	lrg_editor_particle_editor_seek (ed, 1.0f);
	g_assert_cmpfloat_with_epsilon (lrg_editor_particle_editor_get_time (ed), 1.0f, 0.02f);
	a = lrg_editor_particle_editor_get_active_count (ed, -1);
	g_assert_cmpuint (a, >, 10);
	lrg_editor_particle_editor_seek (ed, 0.25f);
	b = lrg_editor_particle_editor_get_active_count (ed, -1);
	g_assert_cmpuint (b, <, a);
	lrg_editor_particle_editor_seek (ed, 1.0f);
	c = lrg_editor_particle_editor_get_active_count (ed, -1);
	g_assert_cmpuint (a, ==, c);
	/* paused: advancing does nothing */
	lrg_editor_particle_editor_advance (ed, 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_editor_particle_editor_get_time (ed), 1.0f, 0.02f);
	/* playing at double speed */
	lrg_editor_particle_editor_set_speed (ed, 2.0f);
	lrg_editor_particle_editor_set_playing (ed, TRUE);
	lrg_editor_particle_editor_advance (ed, 0.5f);
	g_assert_cmpfloat_with_epsilon (lrg_editor_particle_editor_get_time (ed), 2.0f, 0.05f);
	lrg_editor_particle_editor_set_speed (ed, 99.0f);
	g_assert_cmpfloat (lrg_editor_particle_editor_get_speed (ed), ==, 4.0f);
	lrg_editor_particle_editor_set_speed (ed, 1.0f);
	lrg_editor_particle_editor_restart (ed);
	g_assert_cmpfloat (lrg_editor_particle_editor_get_time (ed), <, 0.05f);

	/* the seed override changes nothing in the document */
	lrg_editor_particle_editor_set_seed_override (ed, 99);
	g_assert_cmpint (lrg_editor_particle_editor_get_seed_override (ed), ==, 99);
	g_assert_cmpuint (lrg_particle_effect_get_seed (lrg_editor_particle_editor_get_effect (ed)), ==, 99);
	g_assert_false (lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (ed)));
	lrg_editor_particle_editor_set_seed_override (ed, -1);
	g_assert_cmpuint (lrg_particle_effect_get_seed (lrg_editor_particle_editor_get_effect (ed)), ==, 0);

	/* background, ground */
	lrg_editor_particle_editor_set_background (ed, &bg);
	lrg_editor_particle_editor_get_background (ed, &got);
	g_assert_cmpuint (got.r, ==, 200);
	g_assert_cmpuint (got.a, ==, 255);
	lrg_editor_particle_editor_set_show_ground (ed, FALSE);
	g_assert_false (lrg_editor_particle_editor_get_show_ground (ed));

	/* solo: preview only */
	g_assert_cmpint (lrg_editor_particle_editor_add_emitter (ed, "smoke", &error), ==, 1);
	g_assert_true (lrg_editor_particle_editor_set_solo (ed, 1));
	g_assert_cmpint (lrg_editor_particle_editor_get_solo (ed), ==, 1);
	g_assert_false (lrg_editor_particle_editor_set_solo (ed, 5));
	g_assert_true (lrg_editor_particle_editor_set_solo (ed, -1));

	/* the GPU subset: the default emitter fits, a 2-octave turbulence does not */
	g_assert_true (lrg_editor_particle_editor_get_gpu_status (ed, 0, &msg));
	g_assert_null (msg);
	g_assert_cmpint (lrg_editor_particle_editor_add_module (ed, 0, "turbulence", -1, &error), >=, 0);
	{
		gint t = lrg_editor_particle_editor_get_selected_module (ed);
		g_autofree gchar *p = g_strdup_printf ("/emitters/0/modules/%d", t);

		g_assert_true (lrg_editor_particle_editor_set_value (ed, p, "octaves", "2", 0, &error));
		g_assert_no_error (error);
	}
	g_assert_false (lrg_editor_particle_editor_get_gpu_status (ed, 0, &msg));
	g_assert_nonnull (msg);
	g_assert_nonnull (strstr (msg, "turbulence"));
	g_clear_pointer (&msg, g_free);
	/* the toggle: emitter 1 (spawn-rate, velocity, lifetime) runs on a GPU emitter (or its CPU twin) */
	lrg_editor_particle_editor_set_gpu (ed, TRUE);
	g_assert_true (lrg_editor_particle_editor_get_gpu (ed));
	lrg_editor_particle_editor_advance (ed, 0.5f);
	g_assert_cmpuint (lrg_editor_particle_editor_get_active_count (ed, 1), >, 0);
	g_assert_cmpuint (lrg_editor_particle_editor_get_active_count (ed, 0), >, 0);
	efx_frames (f, 15);
	g_assert_nonnull (strstr (lrg_label_get_text (LRG_LABEL (find_in (ed, "particles-gpu-status"))), "CPU ("));
	lrg_editor_particle_editor_set_gpu (ed, FALSE);
	g_assert_false (lrg_editor_particle_editor_get_emitter_on_gpu (ed, 1));
}

static void
test_particles_validate (EditorAppFixture *f,
                         gconstpointer     data)
{
	g_autofree gchar        *path = project_path (f, "fx/typo.rparticles");
	g_autofree gchar        *bad = project_path (f, "fx/bad.rparticles");
	g_autoptr(GError)        error = NULL;
	LrgEditorParticleEditor *ed;
	GPtrArray               *issues;
	gboolean                 typo = FALSE, idle = FALSE;
	guint                    i;

	g_assert_true (g_file_set_contents (path,
	                                    "version: 1\nemitters:\n- name: a\n  modules:\n  - type: gravity\n"
	                                    "    graviti: [0, 1, 0]\n",
	                                    -1, NULL));
	ed = lrg_editor_particle_editor_open (f->app, path, &error);
	g_assert_no_error (error);
	efx_frames (f, 2);
	lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed));
	issues = lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (ed));
	for (i = 0; i < issues->len; i++)
	{
		LrgEditorDocumentIssue *is = g_ptr_array_index (issues, i);

		if (strstr (is->message, "graviti") != NULL && is->severity == LRG_EDITOR_PROBLEM_WARNING)
			typo = TRUE;
		if (strstr (is->message, "never emits") != NULL)
			idle = TRUE;
	}
	g_assert_true (typo);
	g_assert_true (idle);
	g_assert_true (lrg_widget_get_visible (find_in (ed, "particles-warnings")));

	/* a file that does not load: the error is an issue, edits refuse */
	g_assert_true (g_file_set_contents (bad, "version: 1\nemitters:\n- name: a\n  modules:\n  - type: nope\n", -1,
	                                    NULL));
	ed = lrg_editor_particle_editor_open (f->app, bad, &error);
	g_assert_no_error (error);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_particle_editor_get_asset (ed));
	g_assert_nonnull (lrg_editor_particle_editor_get_load_error (ed));
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed)), >=, 1);
	g_assert_cmpint (lrg_editor_particle_editor_add_emitter (ed, NULL, &error), ==, -1);
	g_assert_nonnull (error);
}

static void
test_particles_canvas (EditorAppFixture *f,
                       gconstpointer     data)
{
	LrgEditorParticleEditor *ed = fire_effect (f);

	efx_click_widget (f, find_in (ed, "particles-add-emitter"));
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_particle_asset_get_n_emitters (lrg_editor_particle_editor_get_asset (ed)), ==, 2);
	efx_click_widget (f, find_in (ed, "particles-emitter-0"));
	efx_frames (f, 2);
	g_assert_cmpint (lrg_editor_particle_editor_get_selected_emitter (ed), ==, 0);
	efx_click_widget (f, find_in (ed, "particles-emitter-solo-1"));
	g_assert_cmpint (lrg_editor_particle_editor_get_solo (ed), ==, 1);
	efx_frames (f, 2);
	efx_click_widget (f, find_in (ed, "particles-play"));
	g_assert_false (lrg_editor_particle_editor_get_playing (ed));
	efx_frames (f, 2);
	efx_click_widget (f, find_in (ed, "particles-renderer"));
	g_assert_cmpint (lrg_editor_particle_editor_get_selected_module (ed), ==, -2);
	efx_frames (f, 2);
	efx_click_widget (f, find_in (ed, "particles-emitter-enabled-1"));
	efx_frames (f, 2);
	g_assert_false (lrg_particle_asset_emitter_get_enabled (
	                    lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 1)));
	efx_click_widget (f, find_in (ed, "particles-module-remove-0"));
	efx_frames (f, 2);
	g_assert_cmpuint (lrg_particle_asset_emitter_get_n_modules (
	                      lrg_particle_asset_get_emitter (lrg_editor_particle_editor_get_asset (ed), 0)), ==, 8);
	efx_click_widget (f, find_in (ed, "particles-settings"));
	g_assert_cmpint (lrg_editor_particle_editor_get_selected_emitter (ed), ==, -1);
}

static void
test_particles_ipc (EditorAppFixture *f,
                    gconstpointer     data)
{
	g_autofree gchar *path = project_path (f, "fx/ipc.rparticles");
	g_autofree gchar *req = NULL;
	JsonNode         *r;
	JsonObject       *o;

#define CALL(fmt, ...) (g_free (req), req = g_strdup_printf (fmt, __VA_ARGS__), efx_ipc_result (f, req))
	r = CALL ("{'id':1,'method':'particles.create','params':{'path':'%s','template':'default'}}", "fx/ipc.rparticles");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "path"), ==, path);
	json_node_unref (r);
	ipc_fails (f, "{'id':1,'method':'particles.create','params':{'path':'fx/ipc.rparticles'}}",
	           "invalid-params");
	r = CALL ("{'id':1,'method':'particles.open','params':{'path':'%s'}}", "fx/ipc.rparticles");
	g_assert_true (g_str_has_prefix (json_object_get_string_member (json_node_get_object (r), "panel"), "data:"));
	json_node_unref (r);
	r = efx_ipc_result (f, "{'id':1,'method':'particles.kinds'}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "kinds")), >=,
	                  20);
	json_node_unref (r);

	r = efx_ipc_result (f, "{'id':1,'method':'particles.add_emitter','params':{'path':'fx/ipc.rparticles',"
	                       "'name':'sparks'}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "index"), ==, 1);
	json_node_unref (r);
	r = efx_ipc_result (f, "{'id':1,'method':'particles.duplicate_emitter','params':{'path':'fx/ipc.rparticles',"
	                       "'emitter':'sparks'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "name"), ==, "sparks copy");
	json_node_unref (r);
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'particles.move_emitter','params':{'path':"
	                                    "'fx/ipc.rparticles','from':2,'to':0}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'particles.remove_emitter','params':{'path':"
	                                    "'fx/ipc.rparticles','emitter':'sparks copy'}}"));
	ipc_fails (f, "{'id':1,'method':'particles.remove_emitter','params':{'path':'fx/ipc.rparticles','emitter':'x'}}",
	           "not-found");
	r = efx_ipc_result (f, "{'id':1,'method':'particles.add_module','params':{'path':'fx/ipc.rparticles',"
	                       "'emitter':'sparks','kind':'burst'}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "index"), ==, 1);
	json_node_unref (r);
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'particles.set','params':{'path':'fx/ipc.rparticles',"
	                                    "'emitter':'sparks','module':1,'key':'count','value':[20,30]}}"));
	/* a key the module does not have is refused (the loader would keep it) */
	ipc_fails (f, "{'id':1,'method':'particles.set','params':{'path':'fx/ipc.rparticles','emitter':1,'module':0,"
	              "'key':'count','value':3}}", "invalid-params");
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'particles.set','params':{'path':'fx/ipc.rparticles',"
	                                    "'emitter':'sparks','renderer':true,'key':'blend','value':'additive'}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'particles.set','params':{'path':'fx/ipc.rparticles',"
	                                    "'key':'duration','value':3}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'particles.set','params':{'path':'fx/ipc.rparticles',"
	                                    "'emitter':1,'key':'max-particles','value':64}}"));
	ipc_fails (f, "{'id':1,'method':'particles.set','params':{'path':'fx/ipc.rparticles','emitter':1,'module':1,"
	              "'key':'count','value':'many'}}", "invalid-params");
	ipc_fails (f, "{'id':1,'method':'particles.set','params':{'path':'fx/ipc.rparticles','key':'seed'}}",
	           "invalid-params");
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'particles.move_module','params':{'path':"
	                                    "'fx/ipc.rparticles','emitter':1,'from':0,'to':1}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'particles.remove_module','params':{'path':"
	                                    "'fx/ipc.rparticles','emitter':1,'module':1}}"));
	ipc_fails (f, "{'id':1,'method':'particles.remove_module','params':{'path':'fx/ipc.rparticles','emitter':1,"
	              "'module':40}}", "not-found");

	r = efx_ipc_result (f, "{'id':1,'method':'particles.get','params':{'path':'fx/ipc.rparticles'}}");
	o = json_node_get_object (r);
	g_assert_cmpfloat (json_object_get_double_member (o, "duration"), ==, 3.0);
	{
		JsonArray  *em = json_object_get_array_member (o, "emitters");
		JsonObject *sparks = json_array_get_object_element (em, 1);
		JsonObject *ren = json_object_get_object_member (sparks, "renderer");

		g_assert_cmpuint (json_array_get_length (em), ==, 2);
		g_assert_cmpstr (json_object_get_string_member (sparks, "name"), ==, "sparks");
		g_assert_cmpint (json_object_get_int_member (sparks, "max_particles"), ==, 64);
		g_assert_cmpstr (json_object_get_string_member (ren, "blend"), ==, "additive");
	}
	json_node_unref (r);

	r = efx_ipc_result (f, "{'id':1,'method':'particles.select','params':{'path':'fx/ipc.rparticles','emitter':1,"
	                       "'module':0}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "module"), ==, 0);
	json_node_unref (r);
	r = efx_ipc_result (f, "{'id':1,'method':'particles.solo','params':{'path':'fx/ipc.rparticles','emitter':'sparks'}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "solo"), ==, 1);
	json_node_unref (r);
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'particles.solo','params':{'path':'fx/ipc.rparticles',"
	                                    "'emitter':null}}"));
	r = efx_ipc_result (f, "{'id':1,'method':'particles.play','params':{'path':'fx/ipc.rparticles','playing':false}}");
	g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "playing"));
	json_node_unref (r);
	r = efx_ipc_result (f, "{'id':1,'method':'particles.seek','params':{'path':'fx/ipc.rparticles','time':1.5}}");
	o = json_node_get_object (r);
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (o, "time"), 1.5, 0.02);
	g_assert_cmpint (json_object_get_int_member (o, "active"), >, 0);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "emitters")), ==, 2);
	json_node_unref (r);
	ipc_fails (f, "{'id':1,'method':'particles.seek','params':{'path':'fx/ipc.rparticles','time':-1}}",
	           "invalid-params");
	r = efx_ipc_result (f, "{'id':1,'method':'particles.preview','params':{'path':'fx/ipc.rparticles','speed':0.5,"
	                       "'seed':7,'ground':false,'background':'#102030','gpu':true}}");
	o = json_node_get_object (r);
	g_assert_cmpfloat (json_object_get_double_member (o, "speed"), ==, 0.5);
	g_assert_cmpint (json_object_get_int_member (o, "seed"), ==, 7);
	g_assert_cmpstr (json_object_get_string_member (o, "background"), ==, "#102030");
	g_assert_true (json_object_get_boolean_member (o, "gpu"));
	json_node_unref (r);
	ipc_fails (f, "{'id':1,'method':'particles.preview','params':{'path':'fx/ipc.rparticles','background':'red'}}",
	           "invalid-params");
	r = efx_ipc_result (f, "{'id':1,'method':'particles.stats','params':{'path':'fx/ipc.rparticles'}}");
	{
		JsonObject *e0 = json_array_get_object_element (json_object_get_array_member (json_node_get_object (r),
		                                                                              "emitters"), 0);

		g_assert_true (json_object_get_boolean_member (e0, "gpu_ok"));
		g_assert_cmpstr (json_object_get_string_member (e0, "simulation"), ==, "gpu-emitter");
	}
	json_node_unref (r);
	r = efx_ipc_result (f, "{'id':1,'method':'particles.restart','params':{'path':'fx/ipc.rparticles'}}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "playing"));
	json_node_unref (r);
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'data.undo','params':{'path':'fx/ipc.rparticles'}}"));
	r = efx_ipc_result (f, "{'id':1,'method':'particles.save','params':{'path':'fx/ipc.rparticles'}}");
	json_node_unref (r);
	{
		g_autoptr(LrgParticleAsset) back = lrg_particle_asset_new_from_file (path, NULL);

		g_assert_nonnull (back);
		g_assert_cmpuint (lrg_particle_asset_get_n_emitters (back), ==, 2);
	}
	/* headless: no graphics context to render with */
	if (!IsWindowReady ())
		ipc_fails (f, "{'id':1,'method':'particles.render','params':{'path':'fx/ipc.rparticles','file':'x.png'}}",
		           "failed");
	ipc_fails (f, "{'id':1,'method':'particles.get','params':{'path':'fx/missing.rparticles'}}",
	           "not-found");
	ipc_fails (f, "{'id':1,'method':'particles.get','params':{}}", "invalid-params");
#undef CALL
}

/* ==========================================================================
 * Weather
 * ========================================================================== */

static void
test_weather_open (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorWeatherEditor *ed = sky_weather (f);
	LrgWeatherAsset        *a = lrg_editor_weather_editor_get_asset (ed);

	g_assert_nonnull (a);
	g_assert_null (lrg_editor_weather_editor_get_load_error (ed));
	g_assert_cmpstr (lrg_weather_asset_get_name (a), ==, "sky");
	g_assert_cmpuint (lrg_weather_asset_get_preset_count (a), ==, 2);
	g_assert_cmpuint (lrg_weather_asset_get_transition_count (a), ==, 2);
	/* the default preset is previewed and selected */
	g_assert_cmpstr (lrg_editor_weather_editor_get_preview_preset (ed), ==, "clear");
	g_assert_cmpint (lrg_editor_weather_editor_get_selected_preset (ed), ==, 0);
	g_assert_nonnull (lrg_editor_weather_editor_get_manager (ed));
	g_assert_cmpstr (lrg_weather_manager_get_active_weather_id (lrg_editor_weather_editor_get_manager (ed)), ==,
	                 "clear");
	g_assert_nonnull (find_in (ed, "weather-preset-1"));
	g_assert_nonnull (find_in (ed, "weather-layer-0"));
	/* selecting the storm previews it */
	efx_click_widget (f, find_in (ed, "weather-preset-1"));
	efx_frames (f, 2);
	g_assert_cmpint (lrg_editor_weather_editor_get_selected_preset (ed), ==, 1);
	g_assert_cmpstr (lrg_weather_manager_get_active_weather_id (lrg_editor_weather_editor_get_manager (ed)), ==,
	                 "storm");
	g_assert_nonnull (find_in (ed, "weather-layer-3"));
	lrg_editor_weather_editor_advance (ed, 0.5f);
}

static void
test_weather_edits (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorWeatherEditor *ed = sky_weather (f);
	g_autoptr(GError)       error = NULL;
	g_autofree gchar       *start = wtext (ed);
	LrgWeatherAsset        *a = lrg_editor_weather_editor_get_asset (ed);
	g_auto(GValue)          v = G_VALUE_INIT;
	GrlColor                c = { 10, 20, 30, 255 }, got;
	gfloat                  br = 0.0f;
	gint                    p, l;
	guint                   steps = 0;

	/* presets */
	p = lrg_editor_weather_editor_add_preset (ed, "blizzard", "Blizzard", &error);
	g_assert_no_error (error);
	g_assert_cmpint (p, ==, 2);
	steps++;
	g_assert_cmpint (lrg_editor_weather_editor_add_preset (ed, "blizzard", NULL, &error), ==, -1);
	g_assert_nonnull (error);
	g_clear_error (&error);
	g_assert_true (lrg_editor_weather_editor_get_asset (ed) == a);
	/* the new preset is registered on the preview manager */
	g_assert_nonnull (lrg_weather_manager_get_weather (lrg_editor_weather_editor_get_manager (ed), "blizzard"));

	/* layers */
	l = lrg_editor_weather_editor_add_layer (ed, (guint) p, "snow", NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (l, ==, 0);
	steps++;
	g_assert_cmpint (lrg_editor_weather_editor_add_layer (ed, (guint) p, "snow", NULL, &error), ==, 1);
	g_assert_cmpstr (lrg_weather_asset_get_layer_id (a, (guint) p, 1), ==, "snow-2");
	steps++;
	g_assert_cmpint (lrg_editor_weather_editor_add_layer (ed, (guint) p, "hail", NULL, &error), ==, -1);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_true (lrg_editor_weather_editor_move_layer (ed, (guint) p, 1, 0, &error));
	g_assert_cmpstr (lrg_weather_asset_get_layer_id (a, (guint) p, 0), ==, "snow-2");
	steps++;
	g_assert_true (lrg_editor_weather_editor_remove_layer (ed, (guint) p, 0, &error));
	steps++;
	g_assert_cmpuint (lrg_weather_asset_get_layer_count (a, (guint) p), ==, 1);

	/* intensity, enabled, params */
	g_assert_true (lrg_editor_weather_editor_set_layer (ed, (guint) p, 0, 0.4f, -1, 0, &error));
	g_assert_cmpfloat_with_epsilon (lrg_weather_asset_get_layer_intensity (a, (guint) p, 0), 0.4f, 1e-5);
	steps++;
	g_assert_true (lrg_editor_weather_editor_set_layer (ed, (guint) p, 0, -1.0f, 0, 0, &error));
	g_assert_false (lrg_weather_asset_get_layer_enabled (a, (guint) p, 0));
	steps++;
	g_assert_true (lrg_editor_weather_editor_set_param (ed, (guint) p, 0, "flake-count", "6000", 0, &error));
	g_assert_no_error (error);
	steps++;
	g_assert_true (lrg_weather_asset_get_layer_param (a, (guint) p, 0, "flake-count", &v));
	g_value_unset (&v);
	g_assert_false (lrg_editor_weather_editor_set_param (ed, (guint) p, 0, "flakes", "1", 0, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_true (lrg_editor_weather_editor_set_param (ed, (guint) p, 0, "flake-count", NULL, 0, &error));
	g_assert_false (lrg_weather_asset_get_layer_param (a, (guint) p, 0, "flake-count", &v));
	steps++;
	g_assert_true (lrg_editor_weather_editor_set_param (ed, (guint) p, 0, "flake-count", "5000", 0, &error));
	steps++;

	/* preset name and ambient */
	g_assert_true (lrg_editor_weather_editor_set_preset (ed, (guint) p, "White-out", &c, 0.7f, 0, &error));
	g_assert_cmpstr (lrg_weather_asset_get_preset_name (a, (guint) p), ==, "White-out");
	lrg_weather_asset_get_preset_ambient (a, (guint) p, &got, &br);
	g_assert_cmpuint (got.g, ==, 20);
	g_assert_cmpfloat_with_epsilon (br, 0.7f, 1e-5);
	steps++;

	/* settings and transitions */
	g_assert_true (lrg_editor_weather_editor_set_setting (ed, "default-preset", "blizzard", &error));
	steps++;
	g_assert_false (lrg_editor_weather_editor_set_setting (ed, "default-preset", "fog", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);
	g_assert_false (lrg_editor_weather_editor_set_setting (ed, "default-transition", "-3", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_true (lrg_editor_weather_editor_set_transition (ed, "storm", "blizzard", 20.0f, &error));
	steps++;
	g_assert_cmpfloat (lrg_weather_asset_resolve_transition (a, "storm", "blizzard"), ==, 20.0f);
	g_assert_false (lrg_editor_weather_editor_set_transition (ed, "storm", "hail", 1.0f, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);
	g_assert_true (lrg_editor_weather_editor_remove_transition (ed, "clear", "storm", &error));
	steps++;
	g_assert_false (lrg_editor_weather_editor_remove_transition (ed, "clear", "storm", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);

	/* removing a preset takes its rules and the default with it */
	g_assert_true (lrg_editor_weather_editor_remove_preset (ed, (guint) p, &error));
	steps++;
	g_assert_null (lrg_weather_asset_get_default_preset (a));
	g_assert_cmpfloat (lrg_weather_asset_resolve_transition (a, "storm", "blizzard"), ==,
	                   lrg_weather_asset_get_default_transition (a));

	/* every edit was one step; undoing them all restores the file */
	efx_frames (f, 2);
	g_assert_cmpuint (undo_all (LRG_EDITOR_DATA_EDITOR (ed)), ==, steps);
	efx_frames (f, 2);
	{
		g_autofree gchar *now = wtext (ed);

		g_assert_cmpstr (now, ==, start);
	}
	g_assert_cmpuint (lrg_weather_asset_get_preset_count (lrg_editor_weather_editor_get_asset (ed)), ==, 2);
}

static void
test_weather_grid (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorWeatherEditor *ed = sky_weather (f);
	LrgPropertyGrid        *grid = LRG_PROPERTY_GRID (find_in (ed, "weather-grid"));
	LrgWeatherAsset        *a;
	LrgPropertyRow         *row;
	g_autofree gchar       *start = NULL;
	GValue                  v = G_VALUE_INIT;
	guint                   k;

	/* the rain layer of the storm: rows from the kind schema */
	g_assert_true (lrg_editor_weather_editor_select (ed, 1, 0));
	efx_frames (f, 2);
	start = wtext (ed);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "drop-count"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "volume-radius"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "space"));
	row = lrg_property_grid_get_row (grid, "drop-speed");
	g_assert_nonnull (row);
	g_value_init (&v, G_TYPE_DOUBLE);
	lrg_property_row_begin_edit (row);
	for (k = 1; k <= 4; k++)
	{
		g_value_set_double (&v, 12.0 + k);
		lrg_property_row_edit_value (row, &v, TRUE);
		efx_frame (f);
	}
	lrg_property_row_end_edit (row, TRUE);
	g_value_unset (&v);
	efx_frames (f, 2);
	a = lrg_editor_weather_editor_get_asset (ed);
	{
		g_auto(GValue)    got = G_VALUE_INIT;
		g_autofree gchar *now = wtext (ed);

		g_assert_true (lrg_weather_asset_get_layer_param (a, 1, 0, "drop-speed", &got));
		g_assert_nonnull (strstr (now, "drop-speed: 16"));
	}
	/* one step */
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frames (f, 2);
	{
		g_autofree gchar *now = wtext (ed);

		g_assert_cmpstr (now, ==, start);
	}

	/* the preset rows */
	g_assert_true (lrg_editor_weather_editor_select (ed, 1, -1));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "ambient-color"));
	row = lrg_property_grid_get_row (grid, "ambient-brightness");
	g_assert_nonnull (row);
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 1.5);
	lrg_property_row_apply_value (row, &v);
	g_value_unset (&v);
	efx_frames (f, 2);
	{
		gfloat br = 0.0f;

		lrg_weather_asset_get_preset_ambient (lrg_editor_weather_editor_get_asset (ed), 1, NULL, &br);
		g_assert_cmpfloat_with_epsilon (br, 1.5f, 1e-5);
	}
	/* the file settings */
	g_assert_true (lrg_editor_weather_editor_select (ed, -1, -1));
	efx_frames (f, 2);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "default-transition"));
	g_assert_false (lrg_editor_weather_editor_select (ed, 7, -1));
	g_assert_false (lrg_editor_weather_editor_select (ed, 1, 40));
}

static void
test_weather_preview (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorWeatherEditor *ed = sky_weather (f);
	LrgWeatherManager      *m;
	g_autoptr(GError)       error = NULL;
	gfloat                  sec;

	g_assert_true (lrg_editor_weather_editor_preview_preset (ed, "clear", &error));
	m = lrg_editor_weather_editor_get_manager (ed);   /* an instant preview starts a fresh manager */
	lrg_editor_weather_editor_advance (ed, 0.2f);
	/* the rule clear -> storm says 8 s */
	sec = lrg_editor_weather_editor_transition_to (ed, "storm", &error);
	g_assert_no_error (error);
	g_assert_cmpfloat (sec, ==, 8.0f);
	g_assert_true (lrg_weather_manager_is_transitioning (m));
	g_assert_cmpstr (lrg_editor_weather_editor_get_preview_preset (ed), ==, "storm");
	lrg_editor_weather_editor_advance (ed, 9.0f);
	g_assert_false (lrg_weather_manager_is_transitioning (m));
	g_assert_cmpstr (lrg_weather_manager_get_active_weather_id (m), ==, "storm");
	/* storm -> clear: the "*" rule, 12 s */
	sec = lrg_editor_weather_editor_transition_to (ed, "clear", &error);
	g_assert_cmpfloat (sec, ==, 12.0f);
	g_assert_cmpfloat (lrg_editor_weather_editor_transition_to (ed, "nope", &error), <, 0.0f);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);
	g_assert_false (lrg_editor_weather_editor_preview_preset (ed, "nope", &error));
	g_clear_error (&error);
	g_assert_true (lrg_editor_weather_editor_preview_preset (ed, NULL, &error));
	g_assert_null (lrg_editor_weather_editor_get_preview_preset (ed));

	/* an edit of the previewed preset syncs the live weather */
	g_assert_true (lrg_editor_weather_editor_preview_preset (ed, "storm", &error));
	m = lrg_editor_weather_editor_get_manager (ed);   /* an instant preview starts a fresh manager */
	g_assert_true (lrg_editor_weather_editor_set_param (ed, 1, 3, "speed-x", "9", 0, &error));
	g_assert_no_error (error);
	lrg_editor_weather_editor_advance (ed, 0.5f);
	{
		gfloat wx = 0.0f, wy = 0.0f;

		lrg_weather_get_effective_wind (lrg_weather_manager_get_active_weather (m), &wx, &wy);
		g_assert_cmpfloat (wx, >, 3.0f);
	}
	/* an outside change (undo) rebuilds the manager on the re-parsed asset */
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	efx_frames (f, 1);
	g_assert_nonnull (lrg_editor_weather_editor_get_manager (ed));
	g_assert_cmpstr (lrg_weather_manager_get_active_weather_id (lrg_editor_weather_editor_get_manager (ed)), ==,
	                 "storm");
}

static void
test_weather_validate (EditorAppFixture *f,
                       gconstpointer     data)
{
	g_autofree gchar       *bad = project_path (f, "data/bad.rweather");
	g_autoptr(GError)       error = NULL;
	LrgEditorWeatherEditor *ed;
	GPtrArray              *issues;
	LrgEditorDocumentIssue *is;
	guint                   k;

	g_assert_true (g_file_set_contents (bad,
	                                    "version: 1\npresets:\n- id: a\n  layers:\n  - kind: rain\n    params:\n"
	                                    "      drops: 3\n",
	                                    -1, NULL));
	ed = lrg_editor_weather_editor_open (f->app, bad, &error);
	g_assert_no_error (error);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_weather_editor_get_asset (ed));
	g_assert_nonnull (strstr (lrg_editor_weather_editor_get_load_error (ed), "drops"));
	g_assert_cmpuint (lrg_editor_data_editor_validate (LRG_EDITOR_DATA_EDITOR (ed)), >=, 1);
	issues = lrg_editor_data_editor_get_issues (LRG_EDITOR_DATA_EDITOR (ed));
	is = NULL;
	for (k = 0; k < issues->len && is == NULL; k++)
		if (g_strcmp0 (((LrgEditorDocumentIssue *) g_ptr_array_index (issues, k))->path,
		               "/presets/0/layers/0/params/drops") == 0)
			is = g_ptr_array_index (issues, k);
	g_assert_nonnull (is);
	g_assert_cmpint (is->severity, ==, LRG_EDITOR_PROBLEM_ERROR);
	g_assert_cmpint (lrg_editor_weather_editor_add_preset (ed, NULL, NULL, &error), ==, -1);
	g_assert_nonnull (error);
}

static void
test_weather_ipc (EditorAppFixture *f,
                  gconstpointer     data)
{
	g_autofree gchar *path = project_path (f, "data/ipc.rweather");
	JsonNode         *r;
	JsonObject       *o;

	r = efx_ipc_result (f, "{'id':1,'method':'weather.create','params':{'path':'data/ipc.rweather'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "path"), ==, path);
	json_node_unref (r);
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.open','params':{'path':'data/ipc.rweather'}}"));
	r = efx_ipc_result (f, "{'id':1,'method':'weather.kinds','params':{'kind':'rain'}}");
	{
		JsonArray  *k = json_object_get_array_member (json_node_get_object (r), "kinds");
		JsonObject *rain = json_array_get_object_element (k, 0);

		g_assert_cmpuint (json_array_get_length (k), ==, 1);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (rain, "params")), >, 5);
	}
	json_node_unref (r);
	ipc_fails (f, "{'id':1,'method':'weather.kinds','params':{'kind':'hail'}}", "not-found");

	r = efx_ipc_result (f, "{'id':1,'method':'weather.add_preset','params':{'path':'data/ipc.rweather','id':'ash',"
	                       "'name':'Ash fall'}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "index"), ==, 2);
	json_node_unref (r);
	ipc_fails (f, "{'id':1,'method':'weather.add_preset','params':{'path':'data/ipc.rweather','id':'ash'}}",
	           "invalid-params");
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.set_preset','params':{'path':'data/ipc.rweather',"
	                                    "'preset':'ash','ambient_color':'#806050','ambient_brightness':0.6}}"));
	r = efx_ipc_result (f, "{'id':1,'method':'weather.add_layer','params':{'path':'data/ipc.rweather','preset':'ash',"
	                       "'kind':'fog'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "id"), ==, "fog");
	json_node_unref (r);
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.add_layer','params':{'path':'data/ipc.rweather',"
	                                    "'preset':'ash','kind':'wind','id':'breeze'}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.move_layer','params':{'path':'data/ipc.rweather',"
	                                    "'preset':'ash','from':'breeze','to':0}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.set_layer','params':{'path':'data/ipc.rweather',"
	                                    "'preset':'ash','layer':'fog','intensity':0.5,'enabled':true}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.set_param','params':{'path':'data/ipc.rweather',"
	                                    "'preset':'ash','layer':'fog','name':'density','value':0.05}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.set_param','params':{'path':'data/ipc.rweather',"
	                                    "'preset':'ash','layer':'fog','name':'color','value':'#808080ff'}}"));
	ipc_fails (f, "{'id':1,'method':'weather.set_param','params':{'path':'data/ipc.rweather','preset':'ash',"
	              "'layer':'fog','name':'thickness','value':1}}", "invalid-params");
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.set_param','params':{'path':'data/ipc.rweather',"
	                                    "'preset':'ash','layer':'fog','name':'color','value':null}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.remove_layer','params':{'path':"
	                                    "'data/ipc.rweather','preset':'ash','layer':'breeze'}}"));
	ipc_fails (f, "{'id':1,'method':'weather.remove_layer','params':{'path':'data/ipc.rweather','preset':'ash',"
	              "'layer':'breeze'}}", "not-found");
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.set_transition','params':{'path':"
	                                    "'data/ipc.rweather','from':'*','to':'ash','duration':30}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.remove_transition','params':{'path':"
	                                    "'data/ipc.rweather','from':'clear','to':'storm'}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.set','params':{'path':'data/ipc.rweather',"
	                                    "'key':'default-transition','value':4}}"));
	ipc_fails (f, "{'id':1,'method':'weather.set','params':{'path':'data/ipc.rweather','key':'colour','value':4}}",
	           "invalid-params");

	r = efx_ipc_result (f, "{'id':1,'method':'weather.get','params':{'path':'data/ipc.rweather'}}");
	o = json_node_get_object (r);
	g_assert_cmpfloat (json_object_get_double_member (o, "default_transition"), ==, 4.0);
	{
		JsonArray  *ps = json_object_get_array_member (o, "presets");
		JsonObject *ash = json_array_get_object_element (ps, 2);
		JsonArray  *ls = json_object_get_array_member (ash, "layers");
		JsonObject *fog = json_array_get_object_element (ls, 0);

		g_assert_cmpstr (json_object_get_string_member (ash, "ambient_color"), ==, "#806050");
		g_assert_cmpuint (json_array_get_length (ls), ==, 1);
		g_assert_cmpfloat_with_epsilon (json_object_get_double_member (fog, "intensity"), 0.5, 1e-6);
		g_assert_true (json_object_has_member (json_object_get_object_member (fog, "params"), "density"));
		g_assert_false (json_object_has_member (json_object_get_object_member (fog, "params"), "color"));
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "transitions")), ==, 2);
	}
	json_node_unref (r);

	r = efx_ipc_result (f, "{'id':1,'method':'weather.select','params':{'path':'data/ipc.rweather','preset':'ash',"
	                       "'layer':0}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "preset"), ==, 2);
	json_node_unref (r);
	r = efx_ipc_result (f, "{'id':1,'method':'weather.preview','params':{'path':'data/ipc.rweather','preset':'clear',"
	                       "'transition_to':'ash','advance':1}}");
	o = json_node_get_object (r);
	g_assert_cmpstr (json_object_get_string_member (o, "active"), ==, "ash");
	g_assert_cmpfloat (json_object_get_double_member (o, "duration"), ==, 30.0);
	g_assert_true (json_object_get_boolean_member (o, "transitioning"));
	json_node_unref (r);
	ipc_fails (f, "{'id':1,'method':'weather.preview','params':{'path':'data/ipc.rweather','preset':'hail'}}",
	           "not-found");
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.remove_preset','params':{'path':"
	                                    "'data/ipc.rweather','preset':'ash'}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'weather.save','params':{'path':'data/ipc.rweather'}}"));
	{
		g_autoptr(LrgWeatherAsset) back = lrg_weather_asset_new_from_file (path, NULL);

		g_assert_nonnull (back);
		g_assert_cmpuint (lrg_weather_asset_get_preset_count (back), ==, 2);
		g_assert_cmpuint (lrg_weather_asset_get_transition_count (back), ==, 1);
	}
	if (!IsWindowReady ())
		ipc_fails (f, "{'id':1,'method':'weather.render','params':{'path':'data/ipc.rweather','file':'x.png'}}",
		           "failed");
	ipc_fails (f, "{'id':1,'method':'weather.get','params':{'path':'data/none.rweather'}}",
	           "not-found");
}

/* ==========================================================================
 * GPU (only under the isolated Xvfb)
 * ========================================================================== */

static gboolean gpu_ready = FALSE;

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	/* never on a live Wayland session: xrun.sh unsets WAYLAND_DISPLAY */
	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-editor-app-vfx");
	return IsWindowReady ();
}

static gboolean
gpu_skip (void)
{
	if (!gpu_ready)
		g_test_skip ("needs a display (run under xrun.sh)");
	return !gpu_ready;
}

/* Pixels that differ from @bg by more than 30 in a channel. */
static guint
lit_pixels (GrlImage    *image,
            const guint8 *bg)
{
	const guint8 *px = grl_image_get_pixels (image, NULL);
	gint          w = grl_image_get_width (image), h = grl_image_get_height (image);
	guint         n = 0;
	gint          i;

	for (i = 0; i < w * h; i++)
		if (abs ((gint) px[4 * i] - bg[0]) > 30 || abs ((gint) px[4 * i + 1] - bg[1]) > 30 ||
		    abs ((gint) px[4 * i + 2] - bg[2]) > 30)
			n++;
	return n;
}

static void
save_shot (GrlImage    *image,
           const gchar *name)
{
	const gchar *dir = g_getenv ("LRG_TEST_SHOT_DIR");

	if (dir != NULL && *dir != '\0')
	{
		g_autofree gchar *file = g_build_filename (dir, name, NULL);

		grl_image_export (image, file);
	}
}

static void
test_gpu_particles (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorParticleEditor *ed;
	g_autoptr(GrlImage)      a = NULL;
	g_autoptr(GrlImage)      b = NULL;
	g_autoptr(GrlImage)      g = NULL;
	const guint8             bg[3] = { 18, 20, 28 };
	g_autoptr(GError)        error = NULL;

	if (gpu_skip ())
		return;
	ed = fire_effect (f);
	lrg_editor_particle_editor_set_show_ground (ed, FALSE);
	lrg_editor_particle_editor_seek (ed, 1.5f);
	a = lrg_editor_particle_editor_render_preview (ed, 384);
	g_assert_nonnull (a);
	g_assert_cmpuint (lit_pixels (a, bg), >, 400);
	save_shot (a, "m67e-edfx-particles-preview.png");
	/* a ribbon renderer draws too */
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "/emitters/0/renderer", "kind", "ribbon", 0, &error));
	g_assert_no_error (error);
	lrg_editor_particle_editor_seek (ed, 1.5f);
	b = lrg_editor_particle_editor_render_preview (ed, 384);
	g_assert_nonnull (b);
	g_assert_cmpuint (lit_pixels (b, bg), >, 200);
	save_shot (b, "m67e-edfx-particles-ribbon.png");
	/* the GPU toggle: billboards again, simulated by an LrgGpuParticles */
	g_assert_true (lrg_editor_particle_editor_set_value (ed, "/emitters/0/renderer", "kind", NULL, 0, &error));
	lrg_editor_particle_editor_set_gpu (ed, TRUE);
	lrg_editor_particle_editor_set_playing (ed, TRUE);
	lrg_editor_particle_editor_advance (ed, 1.0f);
	g_assert_cmpuint (lrg_editor_particle_editor_get_active_count (ed, 0), >, 0);
	g = lrg_editor_particle_editor_render_preview (ed, 384);
	g_assert_nonnull (g);
	save_shot (g, "m67e-edfx-particles-gpu.png");
}

static void
test_gpu_weather (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorWeatherEditor *ed;
	g_autoptr(GrlImage)     a = NULL;
	g_autoptr(GrlImage)     b = NULL;
	g_autoptr(GError)       error = NULL;
	const guint8           *px;

	if (gpu_skip ())
		return;
	ed = sky_weather (f);
	g_assert_true (lrg_editor_weather_editor_preview_preset (ed, "clear", &error));
	lrg_editor_weather_editor_advance (ed, 0.5f);
	a = lrg_editor_weather_editor_render_preview (ed, 480, 270);
	g_assert_nonnull (a);
	save_shot (a, "m67e-edfx-weather-clear.png");
	g_assert_true (lrg_editor_weather_editor_preview_preset (ed, "storm", &error));
	lrg_editor_weather_editor_advance (ed, 1.0f);
	b = lrg_editor_weather_editor_render_preview (ed, 480, 270);
	g_assert_nonnull (b);
	save_shot (b, "m67e-edfx-weather-storm.png");
	/* the storm's ambient darkens the sky (a pixel high in the middle) */
	px = grl_image_get_pixels (b, NULL) + 4 * (8 * 480 + 240);
	{
		const guint8 *pa = grl_image_get_pixels (a, NULL) + 4 * (8 * 480 + 240);

		g_assert_cmpint ((gint) px[0] + px[1] + px[2], <, (gint) pa[0] + pa[1] + pa[2]);
	}
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_log_set_fatal_handler (loader_warnings_ok, NULL);
	gpu_ready = gpu_init ();

#define ADD(name, fn) g_test_add ("/editor-app-vfx/" name, EditorAppFixture, NULL, efx_setup, fn, efx_teardown)
	ADD ("formats", test_particles_formats);
	ADD ("particles/open", test_particles_open);
	ADD ("particles/emitters", test_particles_emitters);
	ADD ("particles/modules", test_particles_modules);
	ADD ("particles/set-value", test_particles_set_value);
	ADD ("particles/grid", test_particles_grid);
	ADD ("particles/preview", test_particles_preview);
	ADD ("particles/validate", test_particles_validate);
	ADD ("particles/canvas", test_particles_canvas);
	ADD ("ipc/particles", test_particles_ipc);
	ADD ("weather/open", test_weather_open);
	ADD ("weather/edits", test_weather_edits);
	ADD ("weather/grid", test_weather_grid);
	ADD ("weather/preview", test_weather_preview);
	ADD ("weather/validate", test_weather_validate);
	ADD ("ipc/weather", test_weather_ipc);
	ADD ("gpu/particles", test_gpu_particles);
	ADD ("gpu/weather", test_gpu_weather);
#undef ADD

	{
		gint rc = g_test_run ();

		if (gpu_ready)
			CloseWindow ();
		return rc;
	}
}
