/* test-ui-file-browser.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFileBrowser, LrgFileFilter and LrgFileDialog.
 *
 * Every browser test gets a fresh temporary folder (g_dir_make_tmp)
 * holding folders, files with known sizes and modification times and a
 * hidden file; it is removed afterwards. Folder loads are asynchronous
 * GIO jobs, so tests iterate the main context until a condition holds
 * (guarded by a timeout source, never a sleep). Widgets are driven
 * through lrg_canvas_process_input() and stepped with
 * lrg_canvas_update() at 60 Hz. Dirs are isolated per test
 * (G_TEST_OPTION_ISOLATE_DIRS), so HOME and the GTK bookmarks file are
 * the test's own. The portal tests run a fake
 * org.freedesktop.portal.FileChooser on a private GTestDBus bus.
 * One GPU case renders the browser and probes pixels; it skips without
 * a display.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

static GTestDBus *test_bus = NULL;

/* ==========================================================================
 * Waiting on the main context
 * ========================================================================== */

static gboolean
flag_cb (gpointer user_data)
{
	*(gboolean *) user_data = TRUE;
	return G_SOURCE_REMOVE;
}

/* WAIT_UNTIL: iterate the default main context until @cond (10 s cap) */
#define WAIT_UNTIL(cond) \
	G_STMT_START { \
		gboolean timed_out_ = FALSE; \
		guint    id_ = g_timeout_add (10000, flag_cb, &timed_out_); \
		while (!(cond) && !timed_out_) \
			g_main_context_iteration (NULL, TRUE); \
		if (!timed_out_) \
			g_source_remove (id_); \
		g_assert_true (cond); \
	} G_STMT_END

/* drain: run whatever is ready without blocking */
static void
drain (void)
{
	guint n = 0;

	while (n < 1000 && g_main_context_iteration (NULL, FALSE))
		n++;
}

/* ==========================================================================
 * Canvas and input helpers
 * ========================================================================== */

static LrgCanvas *
canvas_new (void)
{
	LrgCanvas *canvas = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), 1024.0f, 768.0f);
	lrg_canvas_set_paint_background (canvas, FALSE);
	return canvas;
}

static void
frame_pointer (LrgCanvasInput *input,
               gfloat          x,
               gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = DT;
}

static void
move_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	lrg_canvas_process_input (canvas, &input);
}

/* click_at_mods: move, press and release with modifiers held */
static void
click_at_mods (LrgCanvas         *canvas,
               gfloat             x,
               gfloat             y,
               LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	move_to (canvas, x, y);
	frame_pointer (&input, x, y);
	input.modifiers = mods;
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
	frame_pointer (&input, x, y);
	input.modifiers = mods;
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	click_at_mods (canvas, x, y, LRG_INPUT_MODIFIER_NONE);
}

/* key_mods: one frame with a key press and modifiers held */
static void
key_mods (LrgCanvas         *canvas,
          GrlKey             k,
          LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	input.modifiers = mods;
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

static void
key (LrgCanvas *canvas,
     GrlKey     k)
{
	key_mods (canvas, k, LRG_INPUT_MODIFIER_NONE);
}

/* type_text: one frame per character */
static void
type_text (LrgCanvas   *canvas,
           const gchar *text)
{
	const gchar *p;

	for (p = text; *p != '\0'; p = g_utf8_next_char (p))
	{
		LrgCanvasInput input;

		frame_pointer (&input, -10.0f, -10.0f);
		lrg_canvas_input_add_char (&input, g_utf8_get_char (p));
		lrg_canvas_process_input (canvas, &input);
	}
}

/* settle: run frames until nothing animates */
static void
settle (LrgCanvas *canvas)
{
	guint n = 0;

	while (n < 600 && lrg_canvas_update (canvas, DT))
	{
		lrg_canvas_record (canvas);
		n++;
	}
	lrg_canvas_record (canvas);
	g_assert_cmpuint (n, <, 600);
}

/* center_of: world centre of a widget */
static void
center_of (LrgWidget *w,
           gfloat    *x,
           gfloat    *y)
{
	*x = lrg_widget_get_world_x (w) + lrg_widget_get_width (w) * 0.5f;
	*y = lrg_widget_get_world_y (w) + lrg_widget_get_height (w) * 0.5f;
}

static void
click_widget (LrgCanvas *canvas,
              LrgWidget *w)
{
	gfloat x;
	gfloat y;

	g_assert_nonnull (w);
	center_of (w, &x, &y);
	click_at (canvas, x, y);
}

/* row_point: a point on row @row of @browser's list, away from text */
static void
row_point (LrgFileBrowser *browser,
           guint           row,
           gfloat         *x,
           gfloat         *y)
{
	LrgWidget *list = lrg_widget_find (LRG_WIDGET (browser), "list");
	gfloat     rh = lrg_theme_get_metric (lrg_widget_get_theme (list), LRG_THEME_METRIC_ROW_HEIGHT);

	*x = lrg_widget_get_world_x (list) + 60.0f;
	*y = lrg_widget_get_world_y (list) + rh + ((gfloat) row + 0.5f) * rh;
}

static void
click_row (LrgCanvas      *canvas,
           LrgFileBrowser *browser,
           guint           row)
{
	gfloat x;
	gfloat y;

	row_point (browser, row, &x, &y);
	click_at (canvas, x, y);
}

/* header_point: centre of column header @col (0 name, 1 size, 2 date) */
static void
header_point (LrgFileBrowser *browser,
              guint           col,
              gfloat         *x,
              gfloat         *y)
{
	LrgWidget *list = lrg_widget_find (LRG_WIDGET (browser), "list");
	gfloat     rh = lrg_theme_get_metric (lrg_widget_get_theme (list), LRG_THEME_METRIC_ROW_HEIGHT);
	gfloat     lw = lrg_widget_get_width (list);
	gfloat     date_w = 132.0f;
	gfloat     size_w = 88.0f;
	gfloat     name_w = lw - date_w - size_w;
	gfloat     rel = col == 0 ? name_w * 0.5f : (col == 1 ? name_w + size_w * 0.5f : name_w + size_w + date_w * 0.5f);

	*x = lrg_widget_get_world_x (list) + rel;
	*y = lrg_widget_get_world_y (list) + rh * 0.5f;
}

/* ==========================================================================
 * Temporary folder fixture
 * ========================================================================== */

typedef struct
{
	gchar          *root;
	GFile          *root_file;
	LrgCanvas      *canvas;
	LrgFileBrowser *browser;
	guint           activated;
	GFile          *last_activated;
	guint           selection_changes;
	guint           location_changes;
} Fixture;

static void
write_file (const gchar *dir,
            const gchar *name,
            gsize        size,
            guint64      mtime)
{
	g_autofree gchar *path = g_build_filename (dir, name, NULL);
	g_autofree gchar *data = g_malloc0 (size + 1);
	g_autoptr(GFile)  file = g_file_new_for_path (path);
	g_autoptr(GError) error = NULL;

	memset (data, 'x', size);
	g_assert_true (g_file_set_contents (path, data, (gssize) size, &error));
	g_assert_no_error (error);
	if (mtime > 0)
	{
		g_file_set_attribute_uint64 (file, G_FILE_ATTRIBUTE_TIME_MODIFIED, mtime, G_FILE_QUERY_INFO_NONE,
		                             NULL, &error);
		g_assert_no_error (error);
	}
}

static void
make_dir (const gchar *dir,
          const gchar *name)
{
	g_autofree gchar *path = g_build_filename (dir, name, NULL);

	g_assert_cmpint (g_mkdir (path, 0755), ==, 0);
}

static void
rm_rf (const gchar *path)
{
	GDir        *dir;
	const gchar *name;

	if (g_file_test (path, G_FILE_TEST_IS_DIR) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
	{
		g_chmod (path, 0755);
		dir = g_dir_open (path, 0, NULL);
		while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
		{
			g_autofree gchar *child = g_build_filename (path, name, NULL);

			rm_rf (child);
		}
		if (dir != NULL)
			g_dir_close (dir);
		g_rmdir (path);
	}
	else
		g_unlink (path);
}

static void
on_activated (LrgFileBrowser *browser,
              GFile          *file,
              Fixture        *f)
{
	f->activated++;
	g_set_object (&f->last_activated, file);
}

static void
on_selection_changed (LrgFileBrowser *browser,
                      Fixture        *f)
{
	f->selection_changes++;
}

static void
on_location_changed (LrgFileBrowser *browser,
                     Fixture        *f)
{
	f->location_changes++;
}

static gboolean
browser_idle (LrgFileBrowser *browser)
{
	return !lrg_file_browser_get_loading (browser);
}

/*
 * fixture_setup:
 *
 *   root/
 *     sub/inner.txt
 *     zeta/
 *     alpha.txt   5 bytes, t=1000
 *     beta.png    100 bytes, t=3000
 *     Gamma.dat   50 bytes, t=2000
 *     .hidden.txt
 *
 * and a browser on a canvas showing root, loaded.
 */
static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *sub = NULL;

	f->root = g_dir_make_tmp ("lrg-fb-XXXXXX", &error);
	g_assert_no_error (error);
	f->root_file = g_file_new_for_path (f->root);
	make_dir (f->root, "sub");
	make_dir (f->root, "zeta");
	sub = g_build_filename (f->root, "sub", NULL);
	write_file (sub, "inner.txt", 3, 0);
	write_file (f->root, "alpha.txt", 5, 1000);
	write_file (f->root, "beta.png", 100, 3000);
	write_file (f->root, "Gamma.dat", 50, 2000);
	write_file (f->root, ".hidden.txt", 1, 500);

	f->canvas = canvas_new ();
	f->browser = g_object_new (LRG_TYPE_FILE_BROWSER,
	                           "show-system-places", FALSE,
	                           "location", f->root_file,
	                           NULL);
	g_signal_connect (f->browser, "file-activated", G_CALLBACK (on_activated), f);
	g_signal_connect (f->browser, "selection-changed", G_CALLBACK (on_selection_changed), f);
	g_signal_connect (f->browser, "location-changed", G_CALLBACK (on_location_changed), f);
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->browser));
	g_object_unref (f->browser);
	lrg_widget_allocate (LRG_WIDGET (f->browser), 0.0f, 0.0f, 800.0f, 480.0f);
	WAIT_UNTIL (browser_idle (f->browser));
	lrg_canvas_record (f->canvas);
	settle (f->canvas);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->canvas);
	drain ();
	g_clear_object (&f->last_activated);
	rm_rf (f->root);
	g_clear_object (&f->root_file);
	g_free (f->root);
}

/* entry_name: name of row @i */
static const gchar *
entry_name (LrgFileBrowser *browser,
            guint           i)
{
	GFileInfo *info = lrg_file_browser_get_entry_info (browser, i);

	g_assert_nonnull (info);
	return g_file_info_get_name (info);
}

/* assert_names: the listed names, in order, separated by spaces */
static void
assert_names (LrgFileBrowser *browser,
              const gchar    *expected)
{
	g_autoptr(GString) s = g_string_new (NULL);
	guint              i;

	for (i = 0; i < lrg_file_browser_get_n_entries (browser); i++)
	{
		if (i > 0)
			g_string_append_c (s, ' ');
		g_string_append (s, entry_name (browser, i));
	}
	g_assert_cmpstr (s->str, ==, expected);
}

/* child_of: @f->root / @name as a GFile */
static GFile *
child_of (Fixture     *f,
          const gchar *name)
{
	return g_file_get_child (f->root_file, name);
}

static gboolean
location_is (LrgFileBrowser *browser,
             GFile          *file)
{
	return g_file_equal (lrg_file_browser_get_location (browser), file);
}

/* ==========================================================================
 * LrgFileFilter
 * ========================================================================== */

static void
test_filter_match (void)
{
	g_autoptr(LrgFileFilter) all = lrg_file_filter_new ("All");
	g_autoptr(LrgFileFilter) text = lrg_file_filter_new ("Text");
	g_autoptr(LrgFileFilter) images = lrg_file_filter_new ("Images");
	g_autoptr(LrgFileFilter) saves = lrg_file_filter_new ("Saves");
	g_autofree gchar        *ext = NULL;
	g_auto(GStrv)            patterns = NULL;

	/* No rules: everything */
	g_assert_true (lrg_file_filter_match_name (all, "anything.bin", NULL));

	/* Globs ignore case */
	lrg_file_filter_add_pattern (text, "*.txt");
	g_assert_true (lrg_file_filter_match_name (text, "NOTES.TXT", NULL));
	g_assert_false (lrg_file_filter_match_name (text, "notes.md", NULL));

	/* MIME family and exact type, guessed from the name */
	lrg_file_filter_add_mime_type (images, "image/*");
	g_assert_true (lrg_file_filter_match_name (images, "shot.png", NULL));
	g_assert_true (lrg_file_filter_match_name (images, "x.bin", "image/jpeg"));
	g_assert_false (lrg_file_filter_match_name (images, "notes.txt", NULL));

	/* Suffixes; the first one is the save extension */
	lrg_file_filter_add_suffix (saves, ".DAT");
	lrg_file_filter_add_suffix (saves, "sav");
	g_assert_true (lrg_file_filter_match_name (saves, "slot1.dat", NULL));
	g_assert_true (lrg_file_filter_match_name (saves, "SLOT2.SAV", NULL));
	g_assert_false (lrg_file_filter_match_name (saves, "dat", NULL));
	ext = lrg_file_filter_get_default_extension (saves);
	g_assert_cmpstr (ext, ==, "dat");
	g_clear_pointer (&ext, g_free);
	ext = lrg_file_filter_get_default_extension (text);
	g_assert_cmpstr (ext, ==, "txt");
	g_clear_pointer (&ext, g_free);
	g_assert_null (lrg_file_filter_get_default_extension (images));

	/* Properties */
	g_assert_cmpstr (lrg_file_filter_get_name (saves), ==, "Saves");
	g_assert_cmpstr (lrg_file_filter_get_suffixes (saves)[1], ==, "sav");
	g_assert_cmpstr (lrg_file_filter_get_mime_types (images)[0], ==, "image/*");
	g_object_set (text, "patterns", (const gchar * const []) { "*.md", "*.org", NULL }, NULL);
	g_object_get (text, "patterns", &patterns, NULL);
	g_assert_cmpuint (g_strv_length (patterns), ==, 2);
	g_assert_true (lrg_file_filter_match_name (text, "readme.org", NULL));
	g_assert_false (lrg_file_filter_match_name (text, "notes.txt", NULL));
	lrg_file_filter_set_name (text, "Docs");
	g_assert_cmpstr (lrg_file_filter_get_name (text), ==, "Docs");
}

/* ==========================================================================
 * Browser
 * ========================================================================== */

/* Listing: folders first, natural case-insensitive order, no hidden */
static void
test_listing (Fixture       *f,
              gconstpointer  data)
{
	GFileInfo *info;

	g_assert_false (lrg_file_browser_get_loading (f->browser));
	g_assert_null (lrg_file_browser_get_error (f->browser));
	assert_names (f->browser, "sub zeta alpha.txt beta.png Gamma.dat");
	info = lrg_file_browser_get_entry_info (f->browser, 0);
	g_assert_cmpint (g_file_info_get_file_type (info), ==, G_FILE_TYPE_DIRECTORY);
	g_assert_null (lrg_file_browser_get_entry_info (f->browser, 99));
	g_assert_true (location_is (f->browser, f->root_file));
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, -1);
	g_assert_true (lrg_file_browser_get_folders_first (f->browser));

	/* The breadcrumb ends with the folder's name */
	{
		LrgBreadcrumb    *crumb = LRG_BREADCRUMB (lrg_widget_find (LRG_WIDGET (f->browser), "breadcrumb"));
		guint             n = lrg_breadcrumb_get_n_segments (crumb);
		g_autofree gchar *base = g_path_get_basename (f->root);

		g_assert_cmpuint (n, >=, 2);
		g_assert_cmpstr (lrg_breadcrumb_get_segment_label (crumb, n - 1), ==, base);
	}
}

static void
test_sort (Fixture       *f,
           gconstpointer  data)
{
	gfloat x;
	gfloat y;

	/* Size: folders stay first, files small to large */
	lrg_file_browser_set_sort (f->browser, LRG_FILE_BROWSER_SORT_SIZE, TRUE);
	g_assert_cmpstr (entry_name (f->browser, 2), ==, "alpha.txt");
	g_assert_cmpstr (entry_name (f->browser, 3), ==, "Gamma.dat");
	g_assert_cmpstr (entry_name (f->browser, 4), ==, "beta.png");

	/* Modified, newest first */
	lrg_file_browser_set_sort (f->browser, LRG_FILE_BROWSER_SORT_MODIFIED, FALSE);
	g_assert_cmpstr (entry_name (f->browser, 2), ==, "beta.png");
	g_assert_cmpstr (entry_name (f->browser, 3), ==, "Gamma.dat");
	g_assert_cmpstr (entry_name (f->browser, 4), ==, "alpha.txt");

	/* Without folders-first, folders mix in by name */
	lrg_file_browser_set_sort (f->browser, LRG_FILE_BROWSER_SORT_NAME, TRUE);
	lrg_file_browser_set_folders_first (f->browser, FALSE);
	assert_names (f->browser, "alpha.txt beta.png Gamma.dat sub zeta");
	lrg_file_browser_set_folders_first (f->browser, TRUE);

	/* Clicking the Name header flips to descending; Size starts ascending */
	lrg_canvas_record (f->canvas);
	header_point (f->browser, 0, &x, &y);
	click_at (f->canvas, x, y);
	g_assert_false (lrg_file_browser_get_sort_ascending (f->browser));
	assert_names (f->browser, "zeta sub Gamma.dat beta.png alpha.txt");
	header_point (f->browser, 1, &x, &y);
	click_at (f->canvas, x, y);
	g_assert_cmpint (lrg_file_browser_get_sort_column (f->browser), ==, LRG_FILE_BROWSER_SORT_SIZE);
	g_assert_true (lrg_file_browser_get_sort_ascending (f->browser));
}

static void
test_hidden (Fixture       *f,
             gconstpointer  data)
{
	lrg_file_browser_set_show_hidden (f->browser, TRUE);
	g_assert_cmpuint (lrg_file_browser_get_n_entries (f->browser), ==, 6);
	g_assert_cmpstr (entry_name (f->browser, 2), ==, ".hidden.txt");

	/* Ctrl+H toggles back */
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->browser));
	key_mods (f->canvas, GRL_KEY_H, LRG_INPUT_MODIFIER_CTRL);
	g_assert_false (lrg_file_browser_get_show_hidden (f->browser));
	g_assert_cmpuint (lrg_file_browser_get_n_entries (f->browser), ==, 5);

	/* The eye button too */
	click_widget (f->canvas, lrg_widget_find (LRG_WIDGET (f->browser), "show-hidden"));
	g_assert_true (lrg_file_browser_get_show_hidden (f->browser));
}

static void
test_filters (Fixture       *f,
              gconstpointer  data)
{
	g_autoptr(LrgFileFilter) text = lrg_file_filter_new ("Text");
	g_autoptr(LrgFileFilter) images = lrg_file_filter_new ("Images");
	LrgWidget               *combo = lrg_widget_find (LRG_WIDGET (f->browser), "filter-combo");

	g_assert_false (lrg_widget_get_visible (combo));
	lrg_file_filter_add_pattern (text, "*.TXT");
	lrg_file_filter_add_mime_type (images, "image/*");

	/* The first filter added becomes current; folders are never filtered */
	lrg_file_browser_add_filter (f->browser, text);
	lrg_file_browser_add_filter (f->browser, images);
	g_assert_true (lrg_file_browser_get_filter (f->browser) == text);
	g_assert_cmpuint (g_list_model_get_n_items (lrg_file_browser_get_filters (f->browser)), ==, 2);
	assert_names (f->browser, "sub zeta alpha.txt");
	g_assert_true (lrg_widget_get_visible (combo));
	g_assert_cmpint (lrg_combo_box_get_selected (LRG_COMBO_BOX (combo)), ==, 0);

	/* Picking in the combo switches */
	lrg_combo_box_set_selected (LRG_COMBO_BOX (combo), 1);
	g_assert_true (lrg_file_browser_get_filter (f->browser) == images);
	assert_names (f->browser, "sub zeta beta.png");

	/* No filter: everything */
	lrg_file_browser_set_filter (f->browser, NULL);
	g_assert_cmpuint (lrg_file_browser_get_n_entries (f->browser), ==, 5);

	/* Removing the current filter falls back to the first */
	lrg_file_browser_set_filter (f->browser, images);
	lrg_file_browser_remove_filter (f->browser, images);
	g_assert_true (lrg_file_browser_get_filter (f->browser) == text);
	g_assert_cmpuint (lrg_combo_box_get_n_items (LRG_COMBO_BOX (combo)), ==, 1);

	/* A selected file hidden by a new filter is unselected */
	lrg_file_browser_set_filter (f->browser, NULL);
	{
		g_autoptr(GFile) beta = child_of (f, "beta.png");

		g_assert_true (lrg_file_browser_select_file (f->browser, beta));
	}
	lrg_file_browser_set_filter (f->browser, text);
	g_assert_null (lrg_file_browser_get_selected_file (f->browser));
}

static void
test_navigation (Fixture       *f,
                 gconstpointer  data)
{
	g_autoptr(GFile) sub = child_of (f, "sub");
	g_autoptr(GFile) sel = NULL;

	f->location_changes = 0;

	/* Double click a folder: enter it */
	click_row (f->canvas, f->browser, 0);
	click_row (f->canvas, f->browser, 0);
	g_assert_true (location_is (f->browser, sub));
	g_assert_cmpuint (f->location_changes, ==, 1);
	g_assert_cmpuint (f->activated, ==, 0);
	WAIT_UNTIL (browser_idle (f->browser));
	assert_names (f->browser, "inner.txt");
	g_assert_true (lrg_file_browser_can_go_back (f->browser));
	g_assert_false (lrg_file_browser_can_go_forward (f->browser));

	/* Back, forward through the toolbar buttons */
	lrg_canvas_record (f->canvas);
	click_widget (f->canvas, lrg_widget_find (LRG_WIDGET (f->browser), "back"));
	g_assert_true (location_is (f->browser, f->root_file));
	g_assert_true (lrg_file_browser_can_go_forward (f->browser));
	WAIT_UNTIL (browser_idle (f->browser));
	g_assert_true (lrg_file_browser_go_forward (f->browser));
	g_assert_true (location_is (f->browser, sub));
	g_assert_false (lrg_file_browser_go_forward (f->browser));
	WAIT_UNTIL (browser_idle (f->browser));

	/* Up lands on the folder we left, selected */
	lrg_canvas_record (f->canvas);
	click_widget (f->canvas, lrg_widget_find (LRG_WIDGET (f->browser), "up"));
	g_assert_true (location_is (f->browser, f->root_file));
	WAIT_UNTIL (browser_idle (f->browser));
	sel = lrg_file_browser_get_selected_file (f->browser);
	g_assert_nonnull (sel);
	g_assert_true (g_file_equal (sel, sub));
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 0);

	/* The same location again does nothing; a breadcrumb segment
	 * navigates */
	f->location_changes = 0;
	lrg_file_browser_set_location (f->browser, f->root_file);
	g_assert_cmpuint (f->location_changes, ==, 0);
	{
		LrgBreadcrumb *crumb = LRG_BREADCRUMB (lrg_widget_find (LRG_WIDGET (f->browser), "breadcrumb"));
		guint          n = lrg_breadcrumb_get_n_segments (crumb);
		GFile         *parent = lrg_breadcrumb_get_segment_data (crumb, n - 2);

		g_signal_emit_by_name (crumb, "activated", n - 2);
		g_assert_true (location_is (f->browser, parent));
	}
	WAIT_UNTIL (browser_idle (f->browser));

	/* Paths: "~" is home */
	lrg_file_browser_set_location_path (f->browser, "~");
	{
		g_autoptr(GFile) home = g_file_new_for_path (g_get_home_dir ());

		g_assert_true (location_is (f->browser, home));
	}
	WAIT_UNTIL (browser_idle (f->browser));
}

static void
test_keyboard (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(GFile) sub = child_of (f, "sub");

	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->browser));

	/* Arrows, Home / End, Page keys move the cursor and select */
	key (f->canvas, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 0);
	key (f->canvas, GRL_KEY_DOWN);
	key (f->canvas, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 2);
	key (f->canvas, GRL_KEY_END);
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 4);
	key (f->canvas, GRL_KEY_DOWN);
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 4);
	key (f->canvas, GRL_KEY_PAGE_UP);
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 0);
	key (f->canvas, GRL_KEY_PAGE_DOWN);
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 4);
	key (f->canvas, GRL_KEY_HOME);
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 0);
	g_assert_cmpuint (f->selection_changes, >=, 4);

	/* Enter on a file activates it */
	key (f->canvas, GRL_KEY_END);
	key (f->canvas, GRL_KEY_ENTER);
	g_assert_cmpuint (f->activated, ==, 1);
	{
		g_autoptr(GFile) gamma = child_of (f, "Gamma.dat");

		g_assert_true (g_file_equal (f->last_activated, gamma));
	}

	/* Enter on a folder enters it; Backspace and Alt+Up go up;
	 * Alt+Left / Alt+Right walk the history */
	key (f->canvas, GRL_KEY_HOME);
	key (f->canvas, GRL_KEY_ENTER);
	g_assert_true (location_is (f->browser, sub));
	WAIT_UNTIL (browser_idle (f->browser));
	key (f->canvas, GRL_KEY_BACKSPACE);
	g_assert_true (location_is (f->browser, f->root_file));
	WAIT_UNTIL (browser_idle (f->browser));
	key_mods (f->canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_ALT);
	g_assert_true (location_is (f->browser, sub));
	WAIT_UNTIL (browser_idle (f->browser));
	key_mods (f->canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_ALT);
	g_assert_true (location_is (f->browser, f->root_file));
	WAIT_UNTIL (browser_idle (f->browser));
	key_mods (f->canvas, GRL_KEY_UP, LRG_INPUT_MODIFIER_ALT);
	g_assert_false (location_is (f->browser, f->root_file));
	WAIT_UNTIL (browser_idle (f->browser));
}

static void
test_typeahead (Fixture       *f,
                gconstpointer  data)
{
	guint i;

	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->browser));

	/* Case-insensitive prefix */
	type_text (f->canvas, "g");
	g_assert_cmpstr (entry_name (f->browser, (guint) lrg_file_browser_get_cursor (f->browser)), ==, "Gamma.dat");

	/* The buffer runs out after a second of canvas time */
	for (i = 0; i < 90; i++)
		lrg_canvas_update (f->canvas, DT);
	type_text (f->canvas, "be");
	g_assert_cmpstr (entry_name (f->browser, (guint) lrg_file_browser_get_cursor (f->browser)), ==, "beta.png");

	/* Repeating one letter cycles through matches */
	for (i = 0; i < 90; i++)
		lrg_canvas_update (f->canvas, DT);
	type_text (f->canvas, "s");
	g_assert_cmpstr (entry_name (f->browser, (guint) lrg_file_browser_get_cursor (f->browser)), ==, "sub");
	for (i = 0; i < 90; i++)
		lrg_canvas_update (f->canvas, DT);
	type_text (f->canvas, "zz");
	g_assert_cmpstr (entry_name (f->browser, (guint) lrg_file_browser_get_cursor (f->browser)), ==, "zeta");

	/* "/" opens the path field */
	for (i = 0; i < 90; i++)
		lrg_canvas_update (f->canvas, DT);
	type_text (f->canvas, "/");
	g_assert_true (lrg_file_browser_get_editing_location (f->browser));
}

static void
test_multiple (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(GListModel) files = NULL;

	lrg_file_browser_set_mode (f->browser, LRG_FILE_BROWSER_MODE_OPEN_MULTIPLE);
	lrg_canvas_record (f->canvas);

	/* Click, Ctrl+click, Shift+click */
	click_row (f->canvas, f->browser, 2);
	settle (f->canvas);
	{
		gfloat x;
		gfloat y;

		row_point (f->browser, 4, &x, &y);
		click_at_mods (f->canvas, x, y, LRG_INPUT_MODIFIER_CTRL);
		settle (f->canvas);
		files = lrg_file_browser_get_selected_files (f->browser);
		g_assert_cmpuint (g_list_model_get_n_items (files), ==, 2);
		g_clear_object (&files);

		row_point (f->browser, 1, &x, &y);
		click_at_mods (f->canvas, x, y, LRG_INPUT_MODIFIER_SHIFT);
		settle (f->canvas);
	}
	files = lrg_file_browser_get_selected_files (f->browser);
	g_assert_cmpuint (g_list_model_get_n_items (files), ==, 4);
	g_clear_object (&files);

	/* Ctrl+A selects all; leaving the mode keeps only the cursor */
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->browser));
	key_mods (f->canvas, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL);
	files = lrg_file_browser_get_selected_files (f->browser);
	g_assert_cmpuint (g_list_model_get_n_items (files), ==, 5);
	g_clear_object (&files);
	lrg_file_browser_set_mode (f->browser, LRG_FILE_BROWSER_MODE_OPEN);
	files = lrg_file_browser_get_selected_files (f->browser);
	g_assert_cmpuint (g_list_model_get_n_items (files), ==, 1);

	/* Clicking empty space under the rows clears the selection */
	{
		LrgWidget *list = lrg_widget_find (LRG_WIDGET (f->browser), "list");

		click_at (f->canvas, lrg_widget_get_world_x (list) + 50.0f,
		          lrg_widget_get_world_y (list) + lrg_widget_get_height (list) - 10.0f);
		g_assert_null (lrg_file_browser_get_selected_file (f->browser));
	}
}

static void
test_save_mode (Fixture       *f,
                gconstpointer  data)
{
	g_autoptr(LrgFileFilter) saves = lrg_file_filter_new ("Saves");
	g_autoptr(LrgFileFilter) images = lrg_file_filter_new ("Images");
	g_autoptr(GFile)         target = NULL;
	g_autoptr(GFile)         expected = NULL;
	LrgWidget               *name;

	lrg_file_filter_add_suffix (saves, "dat");
	lrg_file_filter_add_pattern (images, "*.png");
	lrg_file_browser_set_mode (f->browser, LRG_FILE_BROWSER_MODE_SAVE);
	lrg_file_browser_add_filter (f->browser, saves);
	lrg_file_browser_add_filter (f->browser, images);
	name = lrg_widget_find (LRG_WIDGET (f->browser), "name-entry");
	g_assert_true (lrg_widget_get_visible (name));

	/* Empty name: nothing to save */
	g_assert_null (lrg_file_browser_get_save_file (f->browser));

	/* A bare name gets the filter's extension; an explicit one is kept */
	lrg_file_browser_set_current_name (f->browser, "slot1");
	target = lrg_file_browser_get_save_file (f->browser);
	expected = child_of (f, "slot1.dat");
	g_assert_true (g_file_equal (target, expected));
	g_clear_object (&target);
	g_clear_object (&expected);
	lrg_file_browser_set_current_name (f->browser, "slot1.sav");
	target = lrg_file_browser_get_save_file (f->browser);
	expected = child_of (f, "slot1.sav");
	g_assert_true (g_file_equal (target, expected));
	g_clear_object (&target);
	g_clear_object (&expected);

	/* Switching the filter swaps the extension */
	lrg_file_browser_set_current_name (f->browser, "map.dat");
	lrg_file_browser_set_filter (f->browser, images);
	g_assert_cmpstr (lrg_file_browser_get_current_name (f->browser), ==, "map.png");

	/* Picking a file copies its name */
	lrg_canvas_record (f->canvas);
	click_row (f->canvas, f->browser, 2);
	g_assert_cmpstr (lrg_file_browser_get_current_name (f->browser), ==, "beta.png");

	/* Enter in the name field activates the save file */
	lrg_file_browser_set_current_name (f->browser, "fresh");
	lrg_canvas_set_focused_widget (f->canvas, name);
	key (f->canvas, GRL_KEY_ENTER);
	g_assert_cmpuint (f->activated, ==, 1);
	expected = child_of (f, "fresh.png");
	g_assert_true (g_file_equal (f->last_activated, expected));
	g_clear_object (&expected);

	/* A folder name enters the folder */
	lrg_file_browser_set_current_name (f->browser, "sub");
	key (f->canvas, GRL_KEY_ENTER);
	expected = child_of (f, "sub");
	g_assert_true (location_is (f->browser, expected));
	g_assert_cmpstr (lrg_file_browser_get_current_name (f->browser), ==, "");
	WAIT_UNTIL (browser_idle (f->browser));
}

static void
test_select_folder (Fixture       *f,
                    gconstpointer  data)
{
	lrg_file_browser_set_mode (f->browser, LRG_FILE_BROWSER_MODE_SELECT_FOLDER);
	assert_names (f->browser, "sub zeta");
	g_assert_cmpint (lrg_file_browser_get_mode (f->browser), ==, LRG_FILE_BROWSER_MODE_SELECT_FOLDER);
}

/* Double click on a file through the canvas emits file-activated */
static void
test_double_click (Fixture       *f,
                   gconstpointer  data)
{
	g_autoptr(GFile) beta = child_of (f, "beta.png");
	guint            i;

	click_row (f->canvas, f->browser, 3);
	g_assert_cmpuint (f->activated, ==, 0);
	click_row (f->canvas, f->browser, 3);
	g_assert_cmpuint (f->activated, ==, 1);
	g_assert_true (g_file_equal (f->last_activated, beta));

	/* Two clicks far apart in time are two single clicks */
	for (i = 0; i < 40; i++)
		lrg_canvas_update (f->canvas, DT);
	click_row (f->canvas, f->browser, 3);
	for (i = 0; i < 40; i++)
		lrg_canvas_update (f->canvas, DT);
	click_row (f->canvas, f->browser, 3);
	g_assert_cmpuint (f->activated, ==, 1);
}

/* The folder monitor reloads, keeping the selection */
static void
test_monitor (Fixture       *f,
              gconstpointer  data)
{
	g_autoptr(GFile) gamma = child_of (f, "Gamma.dat");
	g_autoptr(GFile) sel = NULL;
	g_autofree gchar *path = g_build_filename (f->root, "Gamma.dat", NULL);

	g_assert_true (lrg_file_browser_select_file (f->browser, gamma));
	write_file (f->root, "delta.txt", 7, 4000);
	WAIT_UNTIL (lrg_file_browser_get_n_entries (f->browser) == 6 && browser_idle (f->browser));
	assert_names (f->browser, "sub zeta alpha.txt beta.png delta.txt Gamma.dat");
	sel = lrg_file_browser_get_selected_file (f->browser);
	g_assert_true (sel != NULL && g_file_equal (sel, gamma));
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 5);

	/* Deleting the selected file drops the selection */
	f->selection_changes = 0;
	g_unlink (path);
	WAIT_UNTIL (lrg_file_browser_get_n_entries (f->browser) == 5 && browser_idle (f->browser));
	g_assert_null (lrg_file_browser_get_selected_file (f->browser));
	g_assert_cmpuint (f->selection_changes, >=, 1);
}

/* Errors show as a row; the list is empty */
static void
test_errors (Fixture       *f,
             gconstpointer  data)
{
	g_autoptr(GFile)  missing = child_of (f, "no-such-folder");
	g_autofree gchar *locked = g_build_filename (f->root, "locked", NULL);
	const GError     *error;
	LrgPaintList     *list;

	lrg_file_browser_set_location (f->browser, missing);
	WAIT_UNTIL (browser_idle (f->browser));
	error = lrg_file_browser_get_error (f->browser);
	g_assert_error ((GError *) error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_cmpuint (lrg_file_browser_get_n_entries (f->browser), ==, 0);
	settle (f->canvas);
	list = lrg_canvas_record (f->canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 10);

	/* Going back clears the error */
	lrg_file_browser_go_back (f->browser);
	WAIT_UNTIL (browser_idle (f->browser));
	g_assert_null (lrg_file_browser_get_error (f->browser));

	/* A folder we may not read (skipped as root, who reads anything) */
	if (geteuid () != 0)
	{
		g_autoptr(GFile) lf = g_file_new_for_path (locked);

		g_assert_cmpint (g_mkdir (locked, 0000), ==, 0);
		lrg_file_browser_set_location (f->browser, lf);
		WAIT_UNTIL (browser_idle (f->browser));
		g_assert_error ((GError *) lrg_file_browser_get_error (f->browser), G_IO_ERROR,
		                G_IO_ERROR_PERMISSION_DENIED);
		g_chmod (locked, 0755);
	}
}

static void
test_places (Fixture       *f,
             gconstpointer  data)
{
	g_autoptr(GFile) saves = child_of (f, "zeta");
	g_autoptr(GFile) mods = child_of (f, "sub");
	LrgWidget       *zone;
	gfloat           rh;
	guint            i;
	gboolean         home = FALSE;
	gboolean         bookmark = FALSE;

	g_assert_cmpuint (lrg_file_browser_get_n_places (f->browser), ==, 0);
	lrg_file_browser_add_place (f->browser, "Saves", saves, LRG_ICON_STAR);
	lrg_file_browser_add_place (f->browser, "Mods", mods, LRG_ICON_FOLDER);
	lrg_file_browser_add_place (f->browser, "Save Slots", saves, LRG_ICON_STAR);
	g_assert_cmpuint (lrg_file_browser_get_n_places (f->browser), ==, 2);
	g_assert_cmpstr (lrg_file_browser_get_place_label (f->browser, 0), ==, "Save Slots");
	g_assert_true (g_file_equal (lrg_file_browser_get_place_location (f->browser, 1), mods));

	/* Clicking the second place opens it */
	lrg_canvas_record (f->canvas);
	settle (f->canvas);
	zone = lrg_widget_find (LRG_WIDGET (f->browser), "places");
	g_assert_true (lrg_widget_get_visible (zone));
	rh = lrg_theme_get_metric (lrg_widget_get_theme (zone), LRG_THEME_METRIC_ROW_HEIGHT);
	click_at (f->canvas, lrg_widget_get_world_x (zone) + 40.0f, lrg_widget_get_world_y (zone) + rh * 1.5f);
	g_assert_true (location_is (f->browser, mods));
	WAIT_UNTIL (browser_idle (f->browser));

	g_assert_true (lrg_file_browser_remove_place (f->browser, mods));
	g_assert_false (lrg_file_browser_remove_place (f->browser, mods));

	/* System places: home and a GTK bookmark from the isolated config */
	{
		g_autofree gchar *dir = g_build_filename (g_get_user_config_dir (), "gtk-3.0", NULL);
		g_autofree gchar *file = g_build_filename (dir, "bookmarks", NULL);
		g_autofree gchar *uri = g_file_get_uri (saves);
		g_autofree gchar *line = g_strdup_printf ("%s Game Saves\nfile:///does/not/exist Gone\n", uri);

		g_mkdir_with_parents (dir, 0755);
		g_assert_true (g_file_set_contents (file, line, -1, NULL));
	}
	lrg_file_browser_set_show_system_places (f->browser, TRUE);
	g_assert_cmpstr (lrg_file_browser_get_place_label (f->browser, 0), ==, "Save Slots");
	for (i = 0; i < lrg_file_browser_get_n_places (f->browser); i++)
	{
		home |= g_strcmp0 (lrg_file_browser_get_place_label (f->browser, i), "Home") == 0;
		bookmark |= g_strcmp0 (lrg_file_browser_get_place_label (f->browser, i), "Game Saves") == 0;
		g_assert_cmpstr (lrg_file_browser_get_place_label (f->browser, i), !=, "Gone");
	}
	g_assert_true (home);
	g_assert_true (bookmark);

	lrg_file_browser_set_show_sidebar (f->browser, FALSE);
	lrg_canvas_record (f->canvas);
	g_assert_false (lrg_widget_get_visible (zone));
}

static void
test_location_entry (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(GFile) sub = child_of (f, "sub");
	g_autoptr(GFile) inner = NULL;
	LrgTextInput    *entry = LRG_TEXT_INPUT (lrg_widget_find (LRG_WIDGET (f->browser), "location-entry"));

	/* Ctrl+L shows and focuses the field with the current path */
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->browser));
	key_mods (f->canvas, GRL_KEY_L, LRG_INPUT_MODIFIER_CTRL);
	g_assert_true (lrg_file_browser_get_editing_location (f->browser));
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (entry));
	g_assert_true (g_str_has_prefix (lrg_text_input_get_text (entry), f->root));

	/* Escape goes back to the breadcrumb */
	key (f->canvas, GRL_KEY_ESCAPE);
	g_assert_false (lrg_file_browser_get_editing_location (f->browser));

	/* A relative folder path navigates */
	lrg_file_browser_set_editing_location (f->browser, TRUE);
	lrg_text_input_set_text (entry, "sub");
	key (f->canvas, GRL_KEY_ENTER);
	g_assert_true (location_is (f->browser, sub));
	g_assert_false (lrg_file_browser_get_editing_location (f->browser));
	WAIT_UNTIL (browser_idle (f->browser));

	/* A file path opens its folder and activates the file */
	lrg_file_browser_set_editing_location (f->browser, TRUE);
	{
		g_autofree gchar *path = g_build_filename (f->root, "alpha.txt", NULL);

		lrg_text_input_set_text (entry, path);
	}
	key (f->canvas, GRL_KEY_ENTER);
	g_assert_true (location_is (f->browser, f->root_file));
	g_assert_cmpuint (f->activated, ==, 1);
	inner = child_of (f, "alpha.txt");
	g_assert_true (g_file_equal (f->last_activated, inner));
	WAIT_UNTIL (browser_idle (f->browser));
	g_assert_cmpstr (entry_name (f->browser, (guint) lrg_file_browser_get_cursor (f->browser)), ==, "alpha.txt");
}

/* find_color: does any instance carry exactly @c (RGB, alpha ignored)? */
static gboolean
find_color (LrgPaintList   *list,
            const GrlColor *c)
{
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *p = lrg_paint_list_get_instance (list, i);

		if (p->color[0] == c->r && p->color[1] == c->g && p->color[2] == c->b)
			return TRUE;
	}
	return FALSE;
}

/* Headless paint: shapes, the selection colour, RTL mirroring, presets */
static void
test_paint (Fixture       *f,
            gconstpointer  data)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgTheme)  theme = lrg_theme_new_preset ("midnight");
	LrgWidget           *places;
	LrgWidget           *list_zone;
	LrgPaintList        *list;
	guint                i;

	lrg_widget_set_theme (LRG_WIDGET (f->canvas), theme);
	lrg_file_browser_add_place (f->browser, "Saves", f->root_file, LRG_ICON_STAR);
	g_assert_true (lrg_file_browser_select_file (f->browser, f->root_file) == FALSE);
	click_row (f->canvas, f->browser, 1);
	settle (f->canvas);

	for (i = 0; presets[i] != NULL; i++)
	{
		lrg_theme_apply_preset (theme, presets[i]);
		list = lrg_canvas_record (f->canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 40);
		g_assert_true (find_color (list, lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION)));
		g_assert_true (find_color (list, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT)));
	}

	/* Right to left: the sidebar moves to the right, back is right of forward */
	places = lrg_widget_find (LRG_WIDGET (f->browser), "places");
	list_zone = lrg_widget_find (LRG_WIDGET (f->browser), "list");
	g_assert_cmpfloat (lrg_widget_get_world_x (places), <, lrg_widget_get_world_x (list_zone));
	lrg_canvas_set_default_direction (f->canvas, LRG_WIDGET_DIRECTION_RTL);
	lrg_widget_queue_layout (LRG_WIDGET (f->browser));
	lrg_canvas_record (f->canvas);
	g_assert_cmpfloat (lrg_widget_get_world_x (places), >, lrg_widget_get_world_x (list_zone));
	g_assert_cmpfloat (lrg_widget_get_world_x (lrg_widget_find (LRG_WIDGET (f->browser), "back")), >,
	                   lrg_widget_get_world_x (lrg_widget_find (LRG_WIDGET (f->browser), "forward")));

	/* Rows still hit-test (mirrored) */
	{
		gfloat x = lrg_widget_get_world_x (list_zone) + lrg_widget_get_width (list_zone) - 60.0f;
		gfloat y;
		gfloat unused;

		row_point (f->browser, 3, &unused, &y);
		click_at (f->canvas, x, y);
		g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 3);
	}
	settle (f->canvas);
}

/* Big folders are virtualized: painting does not grow with the count */
static void
test_virtualized (Fixture       *f,
                  gconstpointer  data)
{
	g_autofree gchar *big = g_build_filename (f->root, "big", NULL);
	g_autoptr(GFile)  bf = g_file_new_for_path (big);
	guint             before;
	guint             after;
	guint             i;

	g_mkdir (big, 0755);
	for (i = 0; i < 600; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("file-%04u.txt", i);

		write_file (big, name, 1, 0);
	}
	lrg_file_browser_set_location (f->browser, bf);
	WAIT_UNTIL (browser_idle (f->browser));
	g_assert_cmpuint (lrg_file_browser_get_n_entries (f->browser), ==, 600);
	g_assert_cmpstr (entry_name (f->browser, 599), ==, "file-0599.txt");
	settle (f->canvas);
	before = lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas));

	/* End scrolls to the last row; still about one screen of rows */
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->browser));
	key (f->canvas, GRL_KEY_END);
	settle (f->canvas);
	after = lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas));
	g_assert_cmpint (lrg_file_browser_get_cursor (f->browser), ==, 599);
	g_assert_cmpuint (after, <, before * 2);
}

/* ==========================================================================
 * LrgFileDialog: in-app
 * ========================================================================== */

typedef enum
{
	KIND_OPEN,
	KIND_MULTIPLE,
	KIND_SAVE,
	KIND_FOLDER
} Kind;

typedef struct
{
	Kind        kind;
	gboolean    done;
	GFile      *file;
	GListModel *files;
	GError     *error;
} Outcome;

static void
outcome_clear (Outcome *o)
{
	g_clear_object (&o->file);
	g_clear_object (&o->files);
	g_clear_error (&o->error);
	o->done = FALSE;
}

static void
on_dialog_done (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
	Outcome       *o = user_data;
	LrgFileDialog *dialog = LRG_FILE_DIALOG (source);

	switch (o->kind)
	{
	case KIND_OPEN:
		o->file = lrg_file_dialog_open_finish (dialog, result, &o->error);
		break;
	case KIND_MULTIPLE:
		o->files = lrg_file_dialog_open_multiple_finish (dialog, result, &o->error);
		break;
	case KIND_SAVE:
		o->file = lrg_file_dialog_save_finish (dialog, result, &o->error);
		break;
	case KIND_FOLDER:
	default:
		o->file = lrg_file_dialog_select_folder_finish (dialog, result, &o->error);
		break;
	}
	o->done = TRUE;
}

/* sheet_browser: the browser of the file dialog on top of the canvas */
static LrgFileBrowser *
sheet_browser (LrgCanvas *canvas)
{
	LrgWidget *top = lrg_canvas_get_top_popup (canvas);

	g_assert_nonnull (top);
	g_assert_true (LRG_IS_DIALOG (top));
	return LRG_FILE_BROWSER (lrg_widget_find (top, "file-browser"));
}

/* dialog_button: a named button of the top popup */
static LrgWidget *
dialog_button (LrgCanvas   *canvas,
               const gchar *name)
{
	LrgWidget *w = lrg_widget_find (lrg_canvas_get_top_popup (canvas), name);

	g_assert_nonnull (w);
	return w;
}

/* dialog_ready: wait for the sheet's folder and its entrance */
static LrgFileBrowser *
dialog_ready (LrgCanvas *canvas)
{
	LrgFileBrowser *browser = sheet_browser (canvas);

	g_assert_nonnull (browser);
	WAIT_UNTIL (browser_idle (browser));
	lrg_canvas_record (canvas);
	settle (canvas);
	return browser;
}

static LrgFileDialog *
dialog_new_for (Fixture *f)
{
	LrgFileDialog *dialog = lrg_file_dialog_new ();

	lrg_file_dialog_set_initial_folder (dialog, f->root_file);
	lrg_file_dialog_set_show_system_places (dialog, FALSE);
	return dialog;
}

static void
test_dialog_open (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(LrgFileDialog) dialog = dialog_new_for (f);
	g_autoptr(GFile)         beta = child_of (f, "beta.png");
	g_autoptr(GFile)         sub = child_of (f, "sub");
	LrgFileBrowser          *browser;
	Outcome                  o = { KIND_OPEN, FALSE, NULL, NULL, NULL };

	lrg_file_dialog_set_title (dialog, "Load Level");
	lrg_file_dialog_open (dialog, LRG_WIDGET (f->browser), NULL, on_dialog_done, &o);
	browser = dialog_ready (f->canvas);
	g_assert_cmpstr (lrg_dialog_get_title (LRG_DIALOG (lrg_canvas_get_top_popup (f->canvas))), ==, "Load Level");
	g_assert_true (location_is (browser, f->root_file));
	g_assert_cmpint (lrg_file_browser_get_mode (browser), ==, LRG_FILE_BROWSER_MODE_OPEN);

	/* Accept with a folder selected enters it */
	click_row (f->canvas, browser, 0);
	click_widget (f->canvas, dialog_button (f->canvas, "accept-button"));
	g_assert_false (o.done);
	g_assert_true (location_is (browser, sub));
	click_widget (f->canvas, dialog_button (f->canvas, "back"));
	WAIT_UNTIL (browser_idle (browser));
	settle (f->canvas);

	/* Select beta.png and accept through the canvas */
	click_row (f->canvas, browser, 3);
	click_widget (f->canvas, dialog_button (f->canvas, "accept-button"));
	WAIT_UNTIL (o.done);
	g_assert_no_error (o.error);
	g_assert_true (g_file_equal (o.file, beta));
	settle (f->canvas);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	outcome_clear (&o);

	/* Double click a file answers at once */
	lrg_file_dialog_open (dialog, LRG_WIDGET (f->canvas), NULL, on_dialog_done, &o);
	browser = dialog_ready (f->canvas);
	click_row (f->canvas, browser, 2);
	click_row (f->canvas, browser, 2);
	WAIT_UNTIL (o.done);
	g_assert_no_error (o.error);
	{
		g_autoptr(GFile) alpha = child_of (f, "alpha.txt");

		g_assert_true (g_file_equal (o.file, alpha));
	}
	outcome_clear (&o);
	settle (f->canvas);
}

static void
test_dialog_cancel (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(LrgFileDialog) dialog = dialog_new_for (f);
	g_autoptr(GCancellable)  cancellable = g_cancellable_new ();
	Outcome                  o = { KIND_OPEN, FALSE, NULL, NULL, NULL };

	/* The Cancel button */
	lrg_file_dialog_open (dialog, LRG_WIDGET (f->canvas), NULL, on_dialog_done, &o);
	dialog_ready (f->canvas);
	click_widget (f->canvas, dialog_button (f->canvas, "cancel-button"));
	WAIT_UNTIL (o.done);
	g_assert_error (o.error, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_DISMISSED);
	g_assert_null (o.file);
	outcome_clear (&o);
	settle (f->canvas);

	/* Escape */
	lrg_file_dialog_open (dialog, LRG_WIDGET (f->canvas), NULL, on_dialog_done, &o);
	dialog_ready (f->canvas);
	key (f->canvas, GRL_KEY_ESCAPE);
	WAIT_UNTIL (o.done);
	g_assert_error (o.error, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_DISMISSED);
	outcome_clear (&o);
	settle (f->canvas);

	/* The GCancellable closes the dialog */
	lrg_file_dialog_open (dialog, LRG_WIDGET (f->canvas), cancellable, on_dialog_done, &o);
	dialog_ready (f->canvas);
	g_cancellable_cancel (cancellable);
	WAIT_UNTIL (o.done);
	g_assert_error (o.error, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_CANCELLED);
	outcome_clear (&o);
	settle (f->canvas);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);

	/* Already cancelled: fails without showing anything */
	lrg_file_dialog_open (dialog, LRG_WIDGET (f->canvas), cancellable, on_dialog_done, &o);
	WAIT_UNTIL (o.done);
	g_assert_error (o.error, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_CANCELLED);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	outcome_clear (&o);

	/* No canvas at all */
	lrg_file_dialog_open (dialog, NULL, NULL, on_dialog_done, &o);
	WAIT_UNTIL (o.done);
	g_assert_error (o.error, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_FAILED);
	outcome_clear (&o);
}

static void
test_dialog_save (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(LrgFileDialog) dialog = dialog_new_for (f);
	g_autoptr(LrgFileFilter) saves = lrg_file_filter_new ("Saves");
	g_autoptr(GListStore)    filters = g_list_store_new (LRG_TYPE_FILE_FILTER);
	g_autoptr(GFile)         alpha = child_of (f, "alpha.txt");
	g_autoptr(GFile)         fresh = child_of (f, "fresh.dat");
	LrgFileBrowser          *browser;
	Outcome                  o = { KIND_SAVE, FALSE, NULL, NULL, NULL };

	lrg_file_filter_add_suffix (saves, "dat");
	g_list_store_append (filters, saves);

	/* Existing file: asks, Replace answers */
	lrg_file_dialog_set_initial_name (dialog, "alpha.txt");
	lrg_file_dialog_save (dialog, LRG_WIDGET (f->canvas), NULL, on_dialog_done, &o);
	browser = dialog_ready (f->canvas);
	g_assert_cmpstr (lrg_file_browser_get_current_name (browser), ==, "alpha.txt");
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == lrg_widget_find (LRG_WIDGET (browser), "name-entry"));
	click_widget (f->canvas, dialog_button (f->canvas, "accept-button"));
	settle (f->canvas);
	g_assert_false (o.done);
	g_assert_cmpstr (lrg_widget_get_name (lrg_canvas_get_top_popup (f->canvas)), ==, "file-dialog-confirm");

	/* Cancel the question: back to the file dialog */
	key (f->canvas, GRL_KEY_ESCAPE);
	settle (f->canvas);
	g_assert_false (o.done);
	g_assert_cmpstr (lrg_widget_get_name (lrg_canvas_get_top_popup (f->canvas)), ==, "file-dialog");

	click_widget (f->canvas, dialog_button (f->canvas, "accept-button"));
	settle (f->canvas);
	click_widget (f->canvas, dialog_button (f->canvas, "replace-button"));
	WAIT_UNTIL (o.done);
	g_assert_no_error (o.error);
	g_assert_true (g_file_equal (o.file, alpha));
	outcome_clear (&o);
	settle (f->canvas);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);

	/* New name with a filter: extension added, no question */
	lrg_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
	lrg_file_dialog_set_initial_name (dialog, "fresh");
	lrg_file_dialog_set_accept_label (dialog, "Write");
	lrg_file_dialog_save (dialog, LRG_WIDGET (f->canvas), NULL, on_dialog_done, &o);
	dialog_ready (f->canvas);
	g_assert_cmpstr (lrg_button_get_text (LRG_BUTTON (dialog_button (f->canvas, "accept-button"))), ==, "Write");
	click_widget (f->canvas, dialog_button (f->canvas, "accept-button"));
	WAIT_UNTIL (o.done);
	g_assert_no_error (o.error);
	g_assert_true (g_file_equal (o.file, fresh));
	outcome_clear (&o);
	settle (f->canvas);
}

static void
test_dialog_folder_multiple (Fixture       *f,
                             gconstpointer  data)
{
	g_autoptr(LrgFileDialog) dialog = dialog_new_for (f);
	g_autoptr(GFile)         zeta = child_of (f, "zeta");
	LrgFileBrowser          *browser;
	Outcome                  o = { KIND_FOLDER, FALSE, NULL, NULL, NULL };
	gfloat                   x;
	gfloat                   y;

	/* Select folder: nothing selected picks the location */
	lrg_file_dialog_select_folder (dialog, LRG_WIDGET (f->canvas), NULL, on_dialog_done, &o);
	browser = dialog_ready (f->canvas);
	assert_names (browser, "sub zeta");
	click_widget (f->canvas, dialog_button (f->canvas, "accept-button"));
	WAIT_UNTIL (o.done);
	g_assert_true (g_file_equal (o.file, f->root_file));
	outcome_clear (&o);
	settle (f->canvas);

	/* ... or the selected folder */
	lrg_file_dialog_select_folder (dialog, LRG_WIDGET (f->canvas), NULL, on_dialog_done, &o);
	browser = dialog_ready (f->canvas);
	click_row (f->canvas, browser, 1);
	click_widget (f->canvas, dialog_button (f->canvas, "accept-button"));
	WAIT_UNTIL (o.done);
	g_assert_true (g_file_equal (o.file, zeta));
	outcome_clear (&o);
	settle (f->canvas);

	/* Open multiple: folders in the selection are left out */
	o.kind = KIND_MULTIPLE;
	lrg_file_dialog_open_multiple (dialog, LRG_WIDGET (f->canvas), NULL, on_dialog_done, &o);
	browser = dialog_ready (f->canvas);
	click_row (f->canvas, browser, 1);
	row_point (browser, 3, &x, &y);
	click_at_mods (f->canvas, x, y, LRG_INPUT_MODIFIER_SHIFT);
	click_widget (f->canvas, dialog_button (f->canvas, "accept-button"));
	WAIT_UNTIL (o.done);
	g_assert_no_error (o.error);
	g_assert_cmpuint (g_list_model_get_n_items (o.files), ==, 2);
	outcome_clear (&o);
	settle (f->canvas);
}

/* ==========================================================================
 * LrgFileDialog: XDG portal
 * ========================================================================== */

static const gchar portal_xml[] =
	"<node>"
	"  <interface name='org.freedesktop.portal.FileChooser'>"
	"    <method name='OpenFile'>"
	"      <arg type='s' name='parent_window' direction='in'/>"
	"      <arg type='s' name='title' direction='in'/>"
	"      <arg type='a{sv}' name='options' direction='in'/>"
	"      <arg type='o' name='handle' direction='out'/>"
	"    </method>"
	"    <method name='SaveFile'>"
	"      <arg type='s' name='parent_window' direction='in'/>"
	"      <arg type='s' name='title' direction='in'/>"
	"      <arg type='a{sv}' name='options' direction='in'/>"
	"      <arg type='o' name='handle' direction='out'/>"
	"    </method>"
	"  </interface>"
	"</node>";

/* A fake portal on its own connection to the test bus */
typedef struct
{
	GDBusConnection *conn;
	guint            owner_id;
	guint            reg_id;
	gboolean         owned;
	gchar           *method;
	gchar           *title;
	GVariant        *options;
	guint32          response;
	const gchar     *uris[3];
} FakePortal;

static void
portal_method (GDBusConnection       *conn,
               const gchar           *sender,
               const gchar           *path,
               const gchar           *iface,
               const gchar           *method,
               GVariant              *parameters,
               GDBusMethodInvocation *invocation,
               gpointer               user_data)
{
	FakePortal       *fp = user_data;
	g_autofree gchar *escaped = g_strdup (sender + 1);
	g_autofree gchar *handle = NULL;
	const gchar      *parent = NULL;
	const gchar      *title = NULL;
	const gchar      *token = "t";
	GVariant         *options = NULL;
	GVariantBuilder   results;
	GVariantBuilder   uris;
	guint             i;

	/* Record the call */
	g_variant_get (parameters, "(&s&s@a{sv})", &parent, &title, &options);
	g_free (fp->method);
	g_free (fp->title);
	g_clear_pointer (&fp->options, g_variant_unref);
	fp->method = g_strdup (method);
	fp->title = g_strdup (title);
	fp->options = options;
	g_variant_lookup (options, "handle_token", "&s", &token);

	/* Reply with the request path, then answer on it */
	g_strdelimit (escaped, ".", '_');
	handle = g_strdup_printf ("/org/freedesktop/portal/desktop/request/%s/%s", escaped, token);
	g_dbus_method_invocation_return_value (invocation, g_variant_new ("(o)", handle));

	g_variant_builder_init (&uris, G_VARIANT_TYPE_STRING_ARRAY);
	for (i = 0; fp->uris[i] != NULL; i++)
		g_variant_builder_add (&uris, "s", fp->uris[i]);
	g_variant_builder_init (&results, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&results, "{sv}", "uris", g_variant_builder_end (&uris));
	g_dbus_connection_emit_signal (conn, sender, handle, "org.freedesktop.portal.Request", "Response",
	                               g_variant_new ("(u@a{sv})", fp->response, g_variant_builder_end (&results)),
	                               NULL);
}

static const GDBusInterfaceVTable portal_vtable = { portal_method, NULL, NULL, { 0 } };

static void
on_portal_name (GDBusConnection *conn,
                const gchar     *name,
                gpointer         user_data)
{
	((FakePortal *) user_data)->owned = TRUE;
}

static void
fake_portal_start (FakePortal *fp)
{
	g_autoptr(GError)        error = NULL;
	g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml (portal_xml, &error);

	g_assert_no_error (error);
	fp->conn = g_dbus_connection_new_for_address_sync (g_test_dbus_get_bus_address (test_bus),
	                                                   G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
	                                                   G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
	                                                   NULL, NULL, &error);
	g_assert_no_error (error);
	fp->reg_id = g_dbus_connection_register_object (fp->conn, "/org/freedesktop/portal/desktop",
	                                                info->interfaces[0], &portal_vtable, fp, NULL, &error);
	g_assert_no_error (error);
	fp->owner_id = g_bus_own_name_on_connection (fp->conn, "org.freedesktop.portal.Desktop",
	                                             G_BUS_NAME_OWNER_FLAGS_NONE, on_portal_name, NULL, fp, NULL);
	WAIT_UNTIL (fp->owned);
}

static void
fake_portal_stop (FakePortal *fp)
{
	g_bus_unown_name (fp->owner_id);
	g_dbus_connection_unregister_object (fp->conn, fp->reg_id);
	g_dbus_connection_close_sync (fp->conn, NULL, NULL);
	g_clear_object (&fp->conn);
	g_free (fp->method);
	g_free (fp->title);
	g_clear_pointer (&fp->options, g_variant_unref);
	drain ();
}

static void
test_dialog_portal (Fixture       *f,
                    gconstpointer  data)
{
	g_autoptr(LrgFileDialog) dialog = dialog_new_for (f);
	g_autoptr(LrgFileFilter) images = lrg_file_filter_new ("Images");
	g_autoptr(GListStore)    filters = g_list_store_new (LRG_TYPE_FILE_FILTER);
	g_autoptr(GFile)         first = NULL;
	g_autoptr(GVariant)      list = NULL;
	FakePortal               fp;
	Outcome                  o = { KIND_MULTIPLE, FALSE, NULL, NULL, NULL };
	gboolean                 flag = FALSE;
	const gchar             *name = NULL;
	const gchar             *folder = NULL;

	memset (&fp, 0, sizeof (fp));
	fp.uris[0] = "file:///tmp/one.png";
	fp.uris[1] = "file:///tmp/two.png";
	fake_portal_start (&fp);

	lrg_file_filter_add_mime_type (images, "image/png");
	lrg_file_filter_add_suffix (images, "png");
	g_list_store_append (filters, images);
	lrg_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
	lrg_file_dialog_set_prefer_native (dialog, TRUE);
	lrg_file_dialog_set_accept_label (dialog, "Import");

	/* OpenFile, multiple: both URIs come back; nothing in-app */
	lrg_file_dialog_open_multiple (dialog, NULL, NULL, on_dialog_done, &o);
	WAIT_UNTIL (o.done);
	g_assert_no_error (o.error);
	g_assert_cmpuint (g_list_model_get_n_items (o.files), ==, 2);
	first = g_list_model_get_item (o.files, 0);
	g_assert_cmpstr (g_file_peek_path (first), ==, "/tmp/one.png");
	g_assert_cmpstr (fp.method, ==, "OpenFile");
	g_assert_cmpstr (fp.title, ==, "Open Files");
	g_assert_true (g_variant_lookup (fp.options, "multiple", "b", &flag) && flag);
	g_assert_true (g_variant_lookup (fp.options, "accept_label", "&s", &name));
	g_assert_cmpstr (name, ==, "Import");
	list = g_variant_lookup_value (fp.options, "filters", G_VARIANT_TYPE ("a(sa(us))"));
	g_assert_nonnull (list);
	g_assert_cmpuint (g_variant_n_children (list), ==, 1);
	g_assert_cmpuint (lrg_canvas_get_popup_count (f->canvas), ==, 0);
	outcome_clear (&o);

	/* SaveFile carries the name and folder */
	o.kind = KIND_SAVE;
	lrg_file_dialog_set_initial_name (dialog, "out.png");
	lrg_file_dialog_save (dialog, NULL, NULL, on_dialog_done, &o);
	WAIT_UNTIL (o.done);
	g_assert_no_error (o.error);
	g_assert_cmpstr (fp.method, ==, "SaveFile");
	g_assert_true (g_variant_lookup (fp.options, "current_name", "&s", &name));
	g_assert_cmpstr (name, ==, "out.png");
	g_assert_true (g_variant_lookup (fp.options, "current_folder", "^&ay", &folder));
	g_assert_cmpstr (folder, ==, f->root);
	outcome_clear (&o);

	/* Select folder asks for directories */
	o.kind = KIND_FOLDER;
	lrg_file_dialog_select_folder (dialog, NULL, NULL, on_dialog_done, &o);
	WAIT_UNTIL (o.done);
	g_assert_true (g_variant_lookup (fp.options, "directory", "b", &flag) && flag);
	outcome_clear (&o);

	/* The user cancelled in the portal */
	fp.response = 1;
	o.kind = KIND_OPEN;
	lrg_file_dialog_open (dialog, NULL, NULL, on_dialog_done, &o);
	WAIT_UNTIL (o.done);
	g_assert_error (o.error, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_DISMISSED);
	outcome_clear (&o);

	fake_portal_stop (&fp);
}

/* No portal on the bus: the in-app dialog shows instead */
static void
test_dialog_portal_fallback (Fixture       *f,
                             gconstpointer  data)
{
	g_autoptr(LrgFileDialog) dialog = dialog_new_for (f);
	g_autoptr(GCancellable)  cancellable = g_cancellable_new ();
	Outcome                  o = { KIND_OPEN, FALSE, NULL, NULL, NULL };

	lrg_file_dialog_set_prefer_native (dialog, TRUE);
	lrg_file_dialog_open (dialog, LRG_WIDGET (f->canvas), cancellable, on_dialog_done, &o);
	WAIT_UNTIL (lrg_canvas_get_popup_count (f->canvas) == 1);
	g_assert_false (o.done);
	dialog_ready (f->canvas);
	g_cancellable_cancel (cancellable);
	WAIT_UNTIL (o.done);
	g_assert_error (o.error, LRG_FILE_DIALOG_ERROR, LRG_FILE_DIALOG_ERROR_CANCELLED);
	outcome_clear (&o);
	settle (f->canvas);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define GPU_W (480)
#define GPU_H (320)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);

	/* No usable display is a skip, not a failure: graylib warns when GLFW
	 * cannot start, and g_test_init () made warnings fatal */
	{
		GLogLevelFlags fatal = g_log_set_always_fatal (G_LOG_FATAL_MASK);

		test_window = grl_window_new (8, 8, "lrg-ui-file-browser-test");
		g_log_set_always_fatal (fatal);
	}
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

static Image
render_canvas (LrgCanvas *canvas)
{
	RenderTexture2D target = LoadRenderTexture (GPU_W, GPU_H);
	Image           image;

	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	lrg_canvas_render (canvas);
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);
	return image;
}

static gboolean
pixel_near (Image          *image,
            gint            x,
            gint            y,
            const GrlColor *c,
            gint            tol)
{
	Color p = GetImageColor (*image, x, y);

	return ABS ((gint) p.r - (gint) c->r) <= tol &&
	       ABS ((gint) p.g - (gint) c->g) <= tol &&
	       ABS ((gint) p.b - (gint) c->b) <= tol;
}

/* GPU: the list well in INPUT, a selected row in SELECTION over it */
static void
test_gpu (Fixture       *f,
          gconstpointer  data)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgTheme)  theme = NULL;
	LrgWidget           *list_zone;
	gfloat               lx;
	gfloat               ly;
	gfloat               lw;
	gfloat               lh;
	gfloat               rh;
	guint                i;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	theme = lrg_theme_new_preset ("midnight");
	lrg_widget_set_theme (LRG_WIDGET (f->canvas), theme);
	lrg_file_browser_set_show_sidebar (f->browser, FALSE);
	lrg_widget_allocate (LRG_WIDGET (f->browser), 0.0f, 0.0f, (gfloat) GPU_W, (gfloat) GPU_H);
	click_row (f->canvas, f->browser, 0);
	move_to (f->canvas, -50.0f, -50.0f);
	settle (f->canvas);
	list_zone = lrg_widget_find (LRG_WIDGET (f->browser), "list");

	for (i = 0; presets[i] != NULL; i++)
	{
		const GrlColor *input;
		const GrlColor *sel;
		GrlColor        expect;
		Image           img;

		lrg_theme_apply_preset (theme, presets[i]);
		settle (f->canvas);
		lx = lrg_widget_get_world_x (list_zone);
		ly = lrg_widget_get_world_y (list_zone);
		lw = lrg_widget_get_width (list_zone);
		lh = lrg_widget_get_height (list_zone);
		rh = lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT);
		input = lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT);
		sel = lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
		lrg_paint_color_mix (input, sel, (gfloat) sel->a / 255.0f, &expect);
		img = render_canvas (f->canvas);

		/* Empty well under the rows, and the middle of the selected row */
		g_assert_true (pixel_near (&img, (gint) (lx + lw * 0.5f), (gint) (ly + lh - 8.0f), input, 3));
		g_assert_true (pixel_near (&img, (gint) (lx + lw * 0.45f), (gint) (ly + rh * 1.5f), &expect, 5));
		UnloadImage (img);
	}
}

/* ==========================================================================
 * Main
 * ========================================================================== */

#define ADD(path, func) g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

int
main (int   argc,
      char *argv[])
{
	gint              result;
	g_autofree gchar *runtime_dir = g_strdup (g_getenv ("XDG_RUNTIME_DIR"));
	const gchar      *wayland;

	/* Local files only, no gvfs or remote volume monitors */
	g_setenv ("GIO_USE_VFS", "local", TRUE);
	g_setenv ("GIO_USE_VOLUME_MONITOR", "unix", TRUE);
	/* MIME data from the system, everything else isolated per test */
	g_content_type_set_mime_dirs (NULL);
	g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

	/* ISOLATE_DIRS points XDG_RUNTIME_DIR at a temporary directory, where
	 * a relative WAYLAND_DISPLAY ("wayland-0") no longer resolves: give
	 * the compositor socket by its absolute path instead */
	wayland = g_getenv ("WAYLAND_DISPLAY");
	if (wayland != NULL && *wayland != '\0' && !g_path_is_absolute (wayland) && runtime_dir != NULL)
	{
		g_autofree gchar *socket = g_build_filename (runtime_dir, wayland, NULL);

		g_setenv ("WAYLAND_DISPLAY", socket, TRUE);
	}

	/* GTestDBus forks a watcher process on the first up. A watcher forked
	 * after Mesa started its threads deadlocks in Mesa's atexit handler
	 * when it exits, leaving a stuck process behind; so fork it now with
	 * a throwaway bus (down unsets the display variables: keep them). */
	{
		static const gchar *keep[] = { "DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR" };
		g_autoptr(GTestDBus) warm = g_test_dbus_new (G_TEST_DBUS_NONE);
		gchar               *saved[G_N_ELEMENTS (keep)];
		guint                i;

		for (i = 0; i < G_N_ELEMENTS (keep); i++)
			saved[i] = g_strdup (g_getenv (keep[i]));
		g_test_dbus_up (warm);
		g_test_dbus_down (warm);
		for (i = 0; i < G_N_ELEMENTS (keep); i++)
		{
			if (saved[i] != NULL)
				g_setenv (keep[i], saved[i], TRUE);
			g_free (saved[i]);
		}
	}

	/* The window's decorations (libdecor's GTK plugin) open the session
	 * bus for accessibility and never let it go. Give them none: a bus
	 * held that way would keep the private test bus from shutting down. */
	g_setenv ("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/lrg-test-no-session-bus", TRUE);
	g_setenv ("NO_AT_BRIDGE", "1", TRUE);
	graphics_available = init_graphics_context ();

	/* The private session bus for the portal tests */
	test_bus = g_test_dbus_new (G_TEST_DBUS_NONE);
	g_test_dbus_up (test_bus);

	g_test_add_func ("/ui/file-filter/match", test_filter_match);
	ADD ("/ui/file-browser/listing", test_listing);
	ADD ("/ui/file-browser/sort", test_sort);
	ADD ("/ui/file-browser/hidden", test_hidden);
	ADD ("/ui/file-browser/filters", test_filters);
	ADD ("/ui/file-browser/navigation", test_navigation);
	ADD ("/ui/file-browser/keyboard", test_keyboard);
	ADD ("/ui/file-browser/typeahead", test_typeahead);
	ADD ("/ui/file-browser/multiple", test_multiple);
	ADD ("/ui/file-browser/save-mode", test_save_mode);
	ADD ("/ui/file-browser/select-folder", test_select_folder);
	ADD ("/ui/file-browser/double-click", test_double_click);
	ADD ("/ui/file-browser/monitor", test_monitor);
	ADD ("/ui/file-browser/errors", test_errors);
	ADD ("/ui/file-browser/places", test_places);
	ADD ("/ui/file-browser/location-entry", test_location_entry);
	ADD ("/ui/file-browser/paint", test_paint);
	ADD ("/ui/file-browser/virtualized", test_virtualized);
	ADD ("/ui/file-dialog/open", test_dialog_open);
	ADD ("/ui/file-dialog/cancel", test_dialog_cancel);
	ADD ("/ui/file-dialog/save", test_dialog_save);
	ADD ("/ui/file-dialog/folder-multiple", test_dialog_folder_multiple);
	ADD ("/ui/file-dialog/portal", test_dialog_portal);
	ADD ("/ui/file-dialog/portal-fallback", test_dialog_portal_fallback);
	ADD ("/ui/gpu/file-browser", test_gpu);

	result = g_test_run ();

	/* Pending D-Bus replies and the deferred cleanup of unsubscribed
	 * signals hold the session connection until the main context runs
	 * them; g_test_dbus_down () waits for that connection to be released */
	{
		gint64 deadline = g_get_monotonic_time () + G_USEC_PER_SEC;

		while (g_get_monotonic_time () < deadline)
		{
			if (!g_main_context_iteration (NULL, FALSE))
				g_usleep (1000);
		}
	}
	g_test_dbus_down (test_bus);
	g_clear_object (&test_bus);
	g_clear_object (&test_window);
	return result;
}
