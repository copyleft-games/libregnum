/* test-editor-app-assets.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's Assets panel (LrgEditorAssetsPanel), driven headless
 * through the canvas (clicks on widget rects, keys, typed text, drags
 * through the canvas DnD session) and the IPC methods it registers:
 * breadcrumb navigation, name and type filters, the grid / list toggle,
 * git badges, inline rename carrying the .meta GUID, duplicate with a
 * fresh GUID, trash with confirmation, drag onto a folder, the Import
 * Settings dialog writing the sidecar, asset.* IPC and the live code
 * tile of scripts.
 *
 * HOME and the XDG dirs point into a private temp root (set before any
 * GLib call caches them), so g_file_trash() uses a trash on the same
 * file system and never touches the user's trash, and git sees no user
 * configuration.
 */

#include "editor-app-fixture.h"

static gchar *test_root = NULL;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void
write_text (const gchar *path,
            const gchar *text)
{
	g_autofree gchar *dir = g_path_get_dirname (path);

	g_mkdir_with_parents (dir, 0755);
	g_assert_true (g_file_set_contents (path, text, -1, NULL));
}

static void
write_png (const gchar *path,
           guint8       r)
{
	GrlColor            c = { r, 80, 200, 255 };
	g_autoptr(GrlImage) img = grl_image_new_color (32, 24, &c);
	g_autofree gchar   *dir = g_path_get_dirname (path);

	g_mkdir_with_parents (dir, 0755);
	g_assert_true (grl_image_export (img, path));
}

static gchar *
proj (EditorAppFixture *f,
      const gchar      *rel)
{
	return g_build_filename (f->project, rel, NULL);
}

static LrgEditorAssetsPanel *
panel (EditorAppFixture *f)
{
	LrgWidget *w = efx_find (f, "assets-panel");

	g_assert_nonnull (w);
	g_assert_true (LRG_IS_EDITOR_ASSETS_PANEL (w));
	return LRG_EDITOR_ASSETS_PANEL (w);
}

static LrgTileView *
grid (EditorAppFixture *f)
{
	return LRG_TILE_VIEW (efx_find (f, "assets-grid"));
}

/* A project with a few files, opened in the app. */
static void
assets_setup (EditorAppFixture *f,
              gconstpointer     data)
{
	g_autoptr(GError) error = NULL;
	g_autofree gchar *hero = NULL;
	g_autofree gchar *crate = NULL;
	g_autofree gchar *lua = NULL;
	g_autofree gchar *sprites = NULL;

	efx_setup (f, data);
	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Demo", &error));
	g_assert_no_error (error);

	hero = proj (f, "assets/hero.png");
	crate = proj (f, "assets/crate.png");
	lua = proj (f, "scripts/player.lua");
	sprites = proj (f, "assets/sprites");
	write_png (hero, 200);
	write_png (crate, 40);
	g_mkdir_with_parents (sprites, 0755);
	write_text (lua, "local speed = 4\nfunction update (self, dt)\n  return speed * dt -- move\nend\n");

	/* Rescan so the new files have sidecars and GUIDs */
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.refresh-assets"));
	efx_frames (f, 3);
}

/* Shows @dir (relative) in the panel and lays it out. */
static void
show_dir (EditorAppFixture *f,
          const gchar      *dir)
{
	g_assert_true (lrg_editor_assets_panel_set_directory (panel (f), dir));
	efx_frames (f, 3);
}

static gint
index_of (EditorAppFixture *f,
          const gchar      *rel)
{
	return lrg_editor_assets_panel_find_entry (panel (f), rel);
}

/* The centre of the grid tile of @rel (it must be visible). */
static void
tile_center (EditorAppFixture *f,
             const gchar      *rel,
             gfloat           *x,
             gfloat           *y)
{
	gint   p = index_of (f, rel);
	gfloat tx, ty, tw, th;

	g_assert_cmpint (p, >=, 0);
	g_assert_true (lrg_tile_view_get_tile_rect (grid (f), (guint) p, &tx, &ty, &tw, &th));
	*x = tx + tw * 0.5f;
	*y = ty + th * 0.4f;
}

static void
click_tile (EditorAppFixture *f,
            const gchar      *rel)
{
	gfloat x, y;

	tile_center (f, rel, &x, &y);
	efx_click (f, x, y);
}

static gchar *
guid_of (const gchar *asset)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgAssetMeta) meta = lrg_asset_meta_load_for_asset (asset, &error);

	g_assert_no_error (error);
	return g_strdup (lrg_asset_meta_get_guid (meta));
}

static gboolean
trash_works (EditorAppFixture *f)
{
	g_autofree gchar *probe = proj (f, "probe.tmp");
	g_autoptr(GFile)  file = NULL;
	g_autoptr(GError) error = NULL;

	write_text (probe, "x");
	file = g_file_new_for_path (probe);
	if (g_file_trash (file, NULL, &error))
		return TRUE;
	g_unlink (probe);
	return FALSE;
}

static gboolean
run_git (const gchar *dir,
         const gchar *args)
{
	g_autofree gchar *cmd = g_strdup_printf ("git -c user.name=t -c user.email=t@example.com -c init.defaultBranch=main %s",
	                                         args);
	g_auto(GStrv)     argv = NULL;
	gint              status = 0;

	g_assert_true (g_shell_parse_argv (cmd, NULL, &argv, NULL));
	if (!g_spawn_sync (dir, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
	                   NULL, NULL, NULL, NULL, &status, NULL))
		return FALSE;
	return g_spawn_check_wait_status (status, NULL);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Breadcrumb, double click into a folder, Backspace up. */
static void
test_navigation (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	LrgBreadcrumb        *crumb = LRG_BREADCRUMB (efx_find (f, "assets-breadcrumb"));
	g_autofree gchar     *assets = proj (f, "assets");
	GrlRectangle          r;
	gfloat                x, y;

	/* The project root first, folders first */
	g_assert_cmpstr (lrg_editor_assets_panel_get_directory (p), ==, f->project);
	g_assert_cmpuint (lrg_breadcrumb_get_n_segments (crumb), ==, 1);
	g_assert_cmpint (index_of (f, "assets"), >=, 0);
	g_assert_cmpint (index_of (f, "assets"), <, index_of (f, "project.ryaml"));

	/* Double click the folder tile */
	tile_center (f, "assets", &x, &y);
	efx_click (f, x, y);
	efx_click (f, x, y);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_assets_panel_get_directory (p), ==, assets);
	g_assert_cmpuint (lrg_breadcrumb_get_n_segments (crumb), ==, 2);
	g_assert_cmpstr (lrg_breadcrumb_get_segment_label (crumb, 1), ==, "assets");
	g_assert_cmpint (index_of (f, "assets/sprites"), ==, 0);   /* the folder first */

	/* The root segment of the breadcrumb goes back */
	g_assert_true (lrg_breadcrumb_get_segment_bounds (crumb, 0, &r));
	efx_click (f, r.x + r.width * 0.5f, r.y + r.height * 0.5f);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_assets_panel_get_directory (p), ==, f->project);

	/* Into assets again, focus the grid, Backspace goes up and selects
	 * the folder we came from */
	show_dir (f, "assets");
	click_tile (f, "assets/hero.png");
	efx_key (f, GRL_KEY_BACKSPACE, 0);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_assets_panel_get_directory (p), ==, f->project);
	{
		g_auto(GStrv) sel = lrg_editor_assets_panel_get_selected_paths (p);

		g_assert_cmpuint (g_strv_length (sel), ==, 1);
		g_assert_cmpstr (sel[0], ==, assets);
	}

	/* Folders outside the project are refused */
	g_assert_false (lrg_editor_assets_panel_set_directory (p, "/"));
}

/* Typed name filter and the type combo. */
static void
test_filter (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	LrgWidget            *search = efx_find (f, "assets-filter");

	show_dir (f, "assets");
	g_assert_cmpuint (lrg_editor_assets_panel_get_n_entries (p), ==, 3);

	/* Typing into the filter field */
	efx_click_widget (f, search);
	efx_type (f, "her");
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_assets_panel_get_filter (p), ==, "her");
	g_assert_cmpuint (lrg_editor_assets_panel_get_n_entries (p), ==, 1);
	g_assert_cmpint (index_of (f, "assets/hero.png"), ==, 0);

	/* Escape in the field clears it */
	efx_key (f, GRL_KEY_ESCAPE, 0);
	efx_frames (f, 1);
	g_assert_cmpstr (lrg_editor_assets_panel_get_filter (p), ==, "");
	g_assert_cmpuint (lrg_editor_assets_panel_get_n_entries (p), ==, 3);

	/* Type filter: scripts only (folders stay), then textures */
	lrg_combo_box_set_selected (LRG_COMBO_BOX (efx_find (f, "assets-type")), LRG_ASSET_TYPE_SCRIPT);
	efx_frames (f, 1);
	g_assert_cmpint (lrg_editor_assets_panel_get_type_filter (p), ==, LRG_ASSET_TYPE_SCRIPT);
	g_assert_cmpuint (lrg_editor_assets_panel_get_n_entries (p), ==, 1);
	g_assert_cmpint (index_of (f, "assets/sprites"), ==, 0);
	lrg_editor_assets_panel_set_type_filter (p, LRG_ASSET_TYPE_TEXTURE);
	g_assert_cmpuint (lrg_editor_assets_panel_get_n_entries (p), ==, 3);
	lrg_editor_assets_panel_set_type_filter (p, LRG_ASSET_TYPE_UNKNOWN);
}

/* The toggle button, the view.asset-grid action and the selection. */
static void
test_grid_list_toggle (EditorAppFixture *f,
                       gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	LrgEditorActions     *actions = lrg_editor_app_get_actions (f->app);
	LrgWidget            *table = efx_find (f, "assets-list");

	show_dir (f, "assets");
	g_assert_true (lrg_editor_assets_panel_get_grid_view (p));
	g_assert_true (lrg_editor_actions_is_checked (actions, "view.asset-grid"));
	click_tile (f, "assets/hero.png");

	efx_click_widget (f, efx_find (f, "assets-view-toggle"));
	efx_frames (f, 2);
	g_assert_false (lrg_editor_assets_panel_get_grid_view (p));
	g_assert_true (lrg_widget_get_visible (table));
	g_assert_false (lrg_widget_get_visible (LRG_WIDGET (grid (f))));
	g_assert_false (lrg_editor_actions_is_checked (actions, "view.asset-grid"));

	/* The selection follows into the list (name, type, size, modified, git) */
	g_assert_cmpuint (lrg_table_get_n_columns (LRG_TABLE (table)), ==, 5);
	g_assert_true (lrg_table_is_selected (LRG_TABLE (table), (guint) index_of (f, "assets/hero.png")));
	g_assert_cmpstr (lrg_table_get_cell_text (LRG_TABLE (table), (guint) index_of (f, "assets/hero.png"), 1), ==,
	                 "Texture");

	/* The registry action toggles back */
	g_assert_true (lrg_editor_app_activate_action (f->app, "view.asset-grid"));
	efx_frames (f, 1);
	g_assert_true (lrg_editor_assets_panel_get_grid_view (p));
	g_assert_true (lrg_tile_view_is_selected (grid (f), (guint) index_of (f, "assets/hero.png")));
}

/* Git badges: a modified file and its folder. */
static void
test_git_badge (EditorAppFixture *f,
                gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	g_autofree gchar     *hero = proj (f, "assets/hero.png");
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *git = g_find_program_in_path ("git");
	gint                  i;

	if (git == NULL)
	{
		g_test_skip ("git is not installed");
		return;
	}
	g_assert_true (run_git (f->project, "init -q"));
	g_assert_true (run_git (f->project, "add -A"));
	g_assert_true (run_git (f->project, "commit -qm init"));

	/* Reopen: a fresh LrgEditorVcs finds the repository */
	g_assert_true (lrg_editor_app_open_project (f->app, f->project, &error));
	g_assert_no_error (error);
	write_png (hero, 10);
	show_dir (f, "assets");
	g_assert_true (lrg_editor_app_activate_action (f->app, "tools.refresh-assets"));
	for (i = 0; i < 400; i++)
	{
		efx_frame (f);
		if (lrg_editor_assets_panel_get_entry_vcs_status (p, (guint) index_of (f, "assets/hero.png")) ==
		    LRG_EDITOR_VCS_STATUS_MODIFIED)
			break;
		g_usleep (5000);
	}
	g_assert_cmpint (i, <, 400);
	g_assert_cmpint (lrg_editor_assets_panel_get_entry_vcs_status (p, (guint) index_of (f, "assets/crate.png")), ==,
	                 LRG_EDITOR_VCS_STATUS_UNMODIFIED);

	/* The folder shows the aggregate at the root */
	show_dir (f, "");
	g_assert_cmpint (lrg_editor_assets_panel_get_entry_vcs_status (p, (guint) index_of (f, "assets")), ==,
	                 LRG_EDITOR_VCS_STATUS_MODIFIED);
}

/* F2, type, Enter: the file and its sidecar move, the GUID stays. */
static void
test_rename_keeps_guid (EditorAppFixture *f,
                        gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	g_autofree gchar     *hero = proj (f, "assets/hero.png");
	g_autofree gchar     *knight = proj (f, "assets/knight.png");
	g_autofree gchar     *guid = NULL;
	g_autofree gchar     *after = NULL;
	g_autofree gchar     *knight_meta = NULL;

	show_dir (f, "assets");
	guid = guid_of (hero);
	click_tile (f, "assets/hero.png");
	efx_key (f, GRL_KEY_F2, 0);
	efx_frames (f, 1);
	g_assert_cmpstr (lrg_editor_assets_panel_get_renaming (p), ==, hero);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "assets-rename")));

	/* The stem is selected: typing replaces it, the extension stays */
	efx_type (f, "knight");
	efx_key (f, GRL_KEY_ENTER, 0);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_assets_panel_get_renaming (p));
	g_assert_false (g_file_test (hero, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (knight, G_FILE_TEST_IS_REGULAR));
	knight_meta = lrg_asset_meta_path_for_asset (knight);
	g_assert_true (g_file_test (knight_meta, G_FILE_TEST_IS_REGULAR));
	after = guid_of (knight);
	g_assert_cmpstr (after, ==, guid);

	/* The renamed entry is selected; Escape cancels a second rename */
	g_assert_cmpint (index_of (f, "assets/knight.png"), >=, 0);
	g_assert_true (lrg_editor_assets_panel_begin_rename (p, "assets/knight.png"));
	efx_frames (f, 1);
	efx_key (f, GRL_KEY_ESCAPE, 0);
	g_assert_null (lrg_editor_assets_panel_get_renaming (p));
	g_assert_true (g_file_test (knight, G_FILE_TEST_IS_REGULAR));
}

/* Duplicate from the context menu action: a copy with a fresh GUID. */
static void
test_duplicate (EditorAppFixture *f,
                gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	g_autofree gchar     *crate = proj (f, "assets/crate.png");
	g_autofree gchar     *copy = proj (f, "assets/crate copy.png");
	g_autofree gchar     *guid = NULL;
	g_autofree gchar     *copy_guid = NULL;

	show_dir (f, "assets");
	guid = guid_of (crate);
	click_tile (f, "assets/crate.png");
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (p), "assets.duplicate", NULL));
	efx_frames (f, 2);
	g_assert_true (g_file_test (copy, G_FILE_TEST_IS_REGULAR));
	copy_guid = guid_of (copy);
	g_assert_cmpstr (copy_guid, !=, guid);
	g_assert_cmpint (index_of (f, "assets/crate copy.png"), >=, 0);
	{
		g_auto(GStrv) sel = lrg_editor_assets_panel_get_selected_paths (p);

		g_assert_cmpstr (sel[0], ==, copy);
	}
}

/* Delete asks first; Enter confirms; the sidecar goes too. */
static void
test_trash (EditorAppFixture *f,
            gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	LrgCanvas            *canvas = lrg_editor_app_get_canvas (f->app);
	g_autofree gchar     *crate = proj (f, "assets/crate.png");
	g_autofree gchar     *meta = lrg_asset_meta_path_for_asset (crate);
	LrgWidget            *top;

	if (!trash_works (f))
	{
		g_test_skip ("No trash on this file system");
		return;
	}
	show_dir (f, "assets");
	g_assert_true (g_file_test (meta, G_FILE_TEST_IS_REGULAR));
	click_tile (f, "assets/crate.png");
	efx_key (f, GRL_KEY_DELETE, 0);
	efx_frames (f, 3);
	top = lrg_canvas_get_top_popup (canvas);
	g_assert_true (LRG_IS_DIALOG (top));
	g_assert_cmpstr (lrg_widget_get_name (top), ==, "assets-trash-dialog");
	g_assert_true (g_file_test (crate, G_FILE_TEST_EXISTS));

	lrg_dialog_respond (LRG_DIALOG (top), 1);
	efx_frames (f, 3);
	g_assert_false (g_file_test (crate, G_FILE_TEST_EXISTS));
	g_assert_false (g_file_test (meta, G_FILE_TEST_EXISTS));
	g_assert_cmpint (index_of (f, "assets/crate.png"), ==, -1);
	g_assert_cmpint (lrg_editor_assets_panel_find_entry (p, "assets/hero.png"), >=, 0);
}

/* Drag a tile onto a folder tile: moved, sidecar and GUID along. */
static void
test_drag_to_folder (EditorAppFixture *f,
                     gconstpointer     data)
{
	LrgCanvas        *canvas = lrg_editor_app_get_canvas (f->app);
	g_autofree gchar *hero = proj (f, "assets/hero.png");
	g_autofree gchar *moved = proj (f, "assets/sprites/hero.png");
	g_autofree gchar *moved_meta = NULL;
	g_autofree gchar *guid = NULL;
	g_autofree gchar *after = NULL;
	gfloat            x0, y0, x1, y1;
	gint              i;

	show_dir (f, "assets");
	guid = guid_of (hero);
	tile_center (f, "assets/hero.png", &x0, &y0);
	tile_center (f, "assets/sprites", &x1, &y1);

	/* Press, move past the threshold, over the folder: the payload is
	 * the project-relative path and the folder accepts a move */
	efx_move (f, x0, y0);
	efx_button (f, x0, y0, GRL_MOUSE_BUTTON_LEFT, TRUE);
	for (i = 1; i <= 8; i++)
		efx_move (f, x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8);
	g_assert_true (lrg_canvas_is_dragging (canvas));
	g_assert_cmpstr (lrg_drag_payload_get_content_type (lrg_canvas_get_drag_payload (canvas)), ==,
	                 "application/x-lrg-asset");
	g_assert_cmpstr (lrg_drag_payload_get_string (lrg_canvas_get_drag_payload (canvas)), ==, "assets/hero.png");
	g_assert_cmpint (lrg_canvas_get_drag_action (canvas), ==, LRG_DRAG_ACTION_MOVE);
	efx_button (f, x1, y1, GRL_MOUSE_BUTTON_LEFT, FALSE);
	efx_frames (f, 2);

	g_assert_false (g_file_test (hero, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (moved, G_FILE_TEST_IS_REGULAR));
	moved_meta = lrg_asset_meta_path_for_asset (moved);
	g_assert_true (g_file_test (moved_meta, G_FILE_TEST_IS_REGULAR));
	after = guid_of (moved);
	g_assert_cmpstr (after, ==, guid);
	g_assert_cmpint (index_of (f, "assets/hero.png"), ==, -1);

	/* Over a plain file tile a drop is refused: nothing moves */
	{
		g_autofree gchar *crate = proj (f, "assets/crate.png");
		gfloat            x2, y2;

		tile_center (f, "assets/crate.png", &x0, &y0);
		tile_center (f, "assets/sprites", &x2, &y2);
		efx_move (f, x2, y2);
		efx_button (f, x2, y2, GRL_MOUSE_BUTTON_LEFT, TRUE);
		for (i = 1; i <= 6; i++)
			efx_move (f, x2 + (x0 - x2) * i / 6, y2 + (y0 - y2) * i / 6);
		g_assert_true (lrg_canvas_is_dragging (canvas));
		g_assert_cmpint (lrg_canvas_get_drag_action (canvas), ==, LRG_DRAG_ACTION_NONE);
		efx_button (f, x0, y0, GRL_MOUSE_BUTTON_LEFT, FALSE);
		efx_frames (f, 1);
		g_assert_true (g_file_test (crate, G_FILE_TEST_IS_REGULAR));
		g_assert_cmpint (index_of (f, "assets/sprites"), >=, 0);
	}
}

/* Import Settings: the property grid over the .meta writes the file. */
static void
test_import_settings (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	LrgCanvas            *canvas = lrg_editor_app_get_canvas (f->app);
	g_autofree gchar     *hero = proj (f, "assets/hero.png");
	g_autoptr(LrgAssetMeta) meta = NULL;
	g_autoptr(GError)    error = NULL;
	LrgWidget            *top;
	LrgPropertyGrid      *pgrid;
	LrgPropertySource    *src;
	GValue                v = G_VALUE_INIT;

	show_dir (f, "assets");

	/* From the context menu action, on the selection */
	click_tile (f, "assets/hero.png");
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (p), "assets.import-settings", NULL));
	efx_frames (f, 3);
	top = lrg_canvas_get_top_popup (canvas);
	g_assert_true (LRG_IS_DIALOG (top));
	pgrid = LRG_PROPERTY_GRID (lrg_dialog_get_content (LRG_DIALOG (top)));
	g_assert_nonnull (lrg_property_grid_get_row (pgrid, "filter"));
	g_assert_nonnull (lrg_property_grid_get_row (pgrid, "max-size"));
	src = lrg_property_grid_get_source (pgrid, 0);

	/* Defaults report DEFAULT; a choice and a number go to the sidecar */
	g_assert_true (lrg_property_source_get_state (src, "filter") & LRG_PROPERTY_STATE_DEFAULT);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "point");
	g_assert_true (lrg_property_source_set_value (src, "filter", &v));
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_INT64);
	g_value_set_int64 (&v, 256);
	g_assert_true (lrg_property_source_set_value (src, "max-size", &v));
	g_value_set_int64 (&v, 99999);   /* out of range: refused */
	g_assert_false (lrg_property_source_set_value (src, "max-size", &v));
	g_value_unset (&v);

	meta = lrg_asset_meta_load_for_asset (hero, &error);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_asset_meta_get_texture_filter (meta), ==, LRG_ASSET_TEXTURE_FILTER_POINT);
	g_assert_cmpint (lrg_asset_meta_get_texture_max_size (meta), ==, 256);
	g_assert_false (lrg_property_source_get_state (src, "filter") & LRG_PROPERTY_STATE_DEFAULT);

	/* Reset puts the default back */
	g_assert_true (lrg_property_source_reset (src, "filter"));
	g_clear_object (&meta);
	meta = lrg_asset_meta_load_for_asset (hero, &error);
	g_assert_false (lrg_asset_meta_has_setting (meta, "filter"));
	lrg_dialog_close (LRG_DIALOG (top));

	/* Folders and scripts have no import settings dialog */
	g_assert_false (lrg_editor_assets_panel_show_import_settings (p, "assets/sprites"));
}

/* Right click: the entry menu; Copy GUID; background menu on empty space. */
static void
test_context_menu (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	LrgCanvas            *canvas = lrg_editor_app_get_canvas (f->app);
	g_autofree gchar     *hero = proj (f, "assets/hero.png");
	g_autofree gchar     *guid = NULL;
	LrgWidget            *top;
	LrgMenu              *menu;
	gfloat                x, y;
	guint                 i;
	gint                  copy_guid = -1;
	gboolean              has_import = FALSE;

	show_dir (f, "assets");
	guid = guid_of (hero);
	tile_center (f, "assets/hero.png", &x, &y);
	efx_move (f, x, y);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_RIGHT, TRUE);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_RIGHT, FALSE);
	efx_frames (f, 2);
	top = lrg_canvas_get_top_popup (canvas);
	g_assert_true (LRG_IS_MENU (top));
	menu = LRG_MENU (top);
	for (i = 0; i < lrg_menu_get_n_entries (menu); i++)
	{
		const gchar *label = lrg_menu_get_entry_label (menu, i);

		if (g_strcmp0 (label, "Copy GUID") == 0)
			copy_guid = (gint) i;
		if (label != NULL && g_str_has_prefix (label, "Import Settings"))
			has_import = TRUE;
	}
	g_assert_true (has_import);
	g_assert_cmpint (copy_guid, >=, 0);
	{
		g_auto(GStrv) sel = lrg_editor_assets_panel_get_selected_paths (p);

		g_assert_cmpstr (sel[0], ==, hero);
	}
	g_assert_true (lrg_menu_activate_entry (menu, (guint) copy_guid));
	efx_frames (f, 2);
	g_assert_cmpstr (g_object_get_data (G_OBJECT (f->app), "lrg-editor-clipboard"), ==, guid);
}

/* Double click a level: it opens in the editor. */
static void
test_open_level (EditorAppFixture *f,
                 gconstpointer     data)
{
	g_autofree gchar *level = proj (f, "levels/second.rlevel");
	gfloat            x, y;

	write_text (level, "type: level\nname: second\ndefault_view: 3d\nnodes: []\n");
	show_dir (f, "levels");
	tile_center (f, "levels/second.rlevel", &x, &y);
	efx_click (f, x, y);
	efx_click (f, x, y);
	efx_frames (f, 3);
	g_assert_cmpstr (lrg_editor_get_path (f->editor), ==, level);
}

static JsonObject *
result_object (JsonNode *n)
{
	return json_node_get_object (n);
}

/* asset.reveal / rename / move / duplicate / trash / orphans */
static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	g_autoptr(JsonNode)   r = NULL;
	g_autofree gchar     *crate = proj (f, "assets/crate.png");
	g_autofree gchar     *guid = guid_of (crate);
	g_autofree gchar     *reply = NULL;
	g_autofree gchar     *orphan = proj (f, "assets/ghost.png.meta");

	/* reveal: navigates and selects */
	r = efx_ipc_result (f, "{'id':1,'method':'asset.reveal','params':{'path':'assets/crate.png'}}");
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "path"), ==, crate);
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "guid"), ==, guid);
	g_clear_pointer (&r, json_node_unref);
	efx_frames (f, 2);
	{
		g_autofree gchar *assets = proj (f, "assets");
		g_auto(GStrv)     sel = lrg_editor_assets_panel_get_selected_paths (p);

		g_assert_cmpstr (lrg_editor_assets_panel_get_directory (p), ==, assets);
		g_assert_cmpstr (sel[0], ==, crate);
	}

	/* rename keeps the guid */
	r = efx_ipc_result (f, "{'id':2,'method':'asset.rename','params':{'path':'assets/crate.png','name':'box.png'}}");
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "relative"), ==, "assets/box.png");
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "guid"), ==, guid);
	g_clear_pointer (&r, json_node_unref);

	/* move into a folder */
	r = efx_ipc_result (f, "{'id':3,'method':'asset.move','params':{'path':'assets/box.png','dest':'assets/sprites'}}");
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "relative"), ==, "assets/sprites/box.png");
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "guid"), ==, guid);
	g_clear_pointer (&r, json_node_unref);

	/* duplicate: fresh guid */
	r = efx_ipc_result (f, "{'id':4,'method':'asset.duplicate','params':{'path':'assets/sprites/box.png'}}");
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "relative"), ==,
	                 "assets/sprites/box copy.png");
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "guid"), !=, guid);
	g_clear_pointer (&r, json_node_unref);

	/* errors: unknown file, rename onto an existing name, missing name */
	reply = efx_ipc (f, "{'id':5,'method':'asset.rename','params':{'path':'nope.png','name':'x.png'}}");
	g_assert_nonnull (strstr (reply, "not-found"));
	g_clear_pointer (&reply, g_free);
	reply = efx_ipc (f, "{'id':6,'method':'asset.rename','params':{'path':'assets/sprites/box.png','name':'box copy.png'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);
	reply = efx_ipc (f, "{'id':7,'method':'asset.rename','params':{'path':'assets/hero.png'}}");
	g_assert_nonnull (strstr (reply, "invalid-params"));
	g_clear_pointer (&reply, g_free);

	/* orphans: a sidecar without its asset */
	write_text (orphan, "version: 1\nguid: 00000000-0000-4000-8000-000000000000\nimporter: texture\n");
	r = efx_ipc_result (f, "{'id':8,'method':'asset.orphans'}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (result_object (r), "orphans")), ==, 1);
	g_assert_cmpstr (json_array_get_string_element (json_object_get_array_member (result_object (r), "orphans"), 0),
	                 ==, "assets/ghost.png.meta");
	g_clear_pointer (&r, json_node_unref);

	/* trash (no confirmation over IPC) */
	if (trash_works (f))
	{
		g_autofree gchar *copy = proj (f, "assets/sprites/box copy.png");

		r = efx_ipc_result (f, "{'id':9,'method':'asset.trash','params':{'path':'assets/sprites/box copy.png'}}");
		g_assert_true (json_object_get_boolean_member (result_object (r), "trashed"));
		g_clear_pointer (&r, json_node_unref);
		g_assert_false (g_file_test (copy, G_FILE_TEST_EXISTS));

		r = efx_ipc_result (f, "{'id':10,'method':'asset.orphans','params':{'trash':true}}");
		g_assert_cmpint (json_object_get_int_member (result_object (r), "trashed"), ==, 1);
		g_clear_pointer (&r, json_node_unref);
		g_assert_false (g_file_test (orphan, G_FILE_TEST_EXISTS));
	}
}

/* The script tile is painted live with the language's token colours. */
static void
test_script_tile (EditorAppFixture *f,
                  gconstpointer     data)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	LrgWidget              *tile;
	LrgSyntax              *syntax = lrg_syntax_get_for_filename ("player.lua");
	GrlColor                keyword;
	GrlColor                comment;
	gboolean                has_keyword = FALSE;
	gboolean                has_comment = FALSE;
	guint                   i;

	g_assert_nonnull (syntax);
	if (lrg_paint_font_get_system_default () == NULL)
	{
		g_test_skip ("no system font: headless text records no glyphs without one");
		return;
	}
	show_dir (f, "scripts");
	tile = lrg_tile_view_get_tile_widget (grid (f), (guint) index_of (f, "scripts/player.lua"));
	g_assert_nonnull (tile);
	lrg_syntax_get_token_color (syntax, lrg_widget_get_theme (tile), LRG_SYNTAX_TOKEN_KEYWORD, &keyword);
	lrg_syntax_get_token_color (syntax, lrg_widget_get_theme (tile), LRG_SYNTAX_TOKEN_COMMENT, &comment);

	/* Glyphs are recorded headless through a default SDF font */
	lrg_paint_font_set_default (lrg_paint_font_get_system_default ());
	lrg_widget_paint (tile, list);
	lrg_paint_font_set_default (NULL);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 30);
	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *in = lrg_paint_list_get_instance (list, i);

		if (in->color[0] == keyword.r && in->color[1] == keyword.g && in->color[2] == keyword.b)
			has_keyword = TRUE;
		if (in->color[0] == comment.r && in->color[1] == comment.g && in->color[2] == comment.b)
			has_comment = TRUE;
	}
	g_assert_true (has_keyword);
	g_assert_true (has_comment);
}

/* A new folder from the background menu action starts its rename. */
static void
test_new_folder (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorAssetsPanel *p = panel (f);
	g_autofree gchar     *folder = proj (f, "assets/New Folder");
	g_autofree gchar     *named = proj (f, "assets/tiles");

	show_dir (f, "assets");
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (p), "assets.new-folder", NULL));
	efx_frames (f, 2);
	g_assert_true (g_file_test (folder, G_FILE_TEST_IS_DIR));
	g_assert_cmpstr (lrg_editor_assets_panel_get_renaming (p), ==, folder);
	efx_type (f, "tiles");
	efx_key (f, GRL_KEY_ENTER, 0);
	efx_frames (f, 2);
	g_assert_true (g_file_test (named, G_FILE_TEST_IS_DIR));
}

int
main (int   argc,
      char *argv[])
{
	g_autofree gchar *data_home = NULL;
	g_autofree gchar *cache_home = NULL;
	g_autofree gchar *config_home = NULL;
	int               status;

	/* Before any GLib call caches them: a private HOME (trash device
	 * check), XDG dirs (trash location, caches) and no git config */
	test_root = g_dir_make_tmp ("lrg-app-assets-XXXXXX", NULL);
	g_assert_nonnull (test_root);
	data_home = g_build_filename (test_root, "data", NULL);
	cache_home = g_build_filename (test_root, "cache", NULL);
	config_home = g_build_filename (test_root, "config", NULL);
	g_mkdir_with_parents (data_home, 0700);
	g_mkdir_with_parents (cache_home, 0700);
	g_mkdir_with_parents (config_home, 0700);
	g_setenv ("HOME", test_root, TRUE);
	g_setenv ("XDG_DATA_HOME", data_home, TRUE);
	g_setenv ("XDG_CACHE_HOME", cache_home, TRUE);
	g_setenv ("XDG_CONFIG_HOME", config_home, TRUE);
	g_setenv ("GIT_CONFIG_NOSYSTEM", "1", TRUE);
	g_setenv ("TMPDIR", test_root, TRUE);

	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */

#define ADD(path, fn) g_test_add (path, EditorAppFixture, NULL, assets_setup, fn, efx_teardown)
	ADD ("/editor-app/assets/navigation", test_navigation);
	ADD ("/editor-app/assets/filter", test_filter);
	ADD ("/editor-app/assets/grid-list-toggle", test_grid_list_toggle);
	ADD ("/editor-app/assets/git-badge", test_git_badge);
	ADD ("/editor-app/assets/rename-keeps-guid", test_rename_keeps_guid);
	ADD ("/editor-app/assets/duplicate", test_duplicate);
	ADD ("/editor-app/assets/trash", test_trash);
	ADD ("/editor-app/assets/drag-to-folder", test_drag_to_folder);
	ADD ("/editor-app/assets/import-settings", test_import_settings);
	ADD ("/editor-app/assets/context-menu", test_context_menu);
	ADD ("/editor-app/assets/open-level", test_open_level);
	ADD ("/editor-app/assets/ipc", test_ipc);
	ADD ("/editor-app/assets/script-tile", test_script_tile);
	ADD ("/editor-app/assets/new-folder", test_new_folder);
#undef ADD

	status = g_test_run ();
	efx_rm_rf (test_root);
	g_free (test_root);
	return status;
}
