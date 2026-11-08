/* test-editor-sources.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Asset sources (LrgAssetSource) and their editor integration:
 *
 *   paths      virtual path normalisation, LrgAssetSourceEntry
 *   list       listing, find_entry, the fallback search and a source's
 *              own search
 *   cache      lrg_asset_source_export_cached(): one export, reused in
 *              process and across instances (marker), invalidation
 *   registry   lrg_editor_app_add / lookup / get / remove_asset_source
 *   panel      the Sources panel driven headless: hint, listing,
 *              navigation (activate, Up chip), filter (typed), search,
 *              preview export, source chooser
 *   import     into <project>/assets/<id>/<folder>/, refused without a
 *              project, place in scene, export to a folder
 *   ipc        source.list / ls / search / export / import / place and
 *              their errors
 *   module     the game module editor hook: lrg_loaded_game_lookup_symbol
 *              and lrg_editor_app_init_module on the fixture module
 *              (tests/fixtures/test-editor-module.c), its failure path,
 *              and a module without a hook
 *
 * HOME and the XDG dirs point into a private temp root (set before any
 * GLib call caches them), so the export cache
 * ($XDG_CACHE_HOME/libregnum/editor/sources) is private to the run.
 */

#include "editor-app-fixture.h"

static gchar *test_root = NULL;

/* A 1x1 RGBA PNG, for the texture exports */
static const gchar *png_1x1 =
	"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";

/* ==========================================================================
 * The test source
 *
 *   Props/                folder
 *   Props/crate.mdl       model    (exports crate.obj + crate.mtl)
 *   Props/Nested/         folder
 *   Props/Nested/barrel.mdl  model
 *   Textures/             folder
 *   Textures/grass.tex    texture  (exports grass.png)
 *   Textures/stone.tex    texture
 *   broken.mdl            model    (export fails)
 * ========================================================================== */

typedef struct
{
	const gchar  *path;
	gboolean      folder;
	LrgAssetType  type;
	guint64       size;
} TestNode;

static const TestNode tree[] = {
	{ "Props", TRUE, LRG_ASSET_TYPE_UNKNOWN, 0 },
	{ "Props/crate.mdl", FALSE, LRG_ASSET_TYPE_MODEL, 1234 },
	{ "Props/Nested", TRUE, LRG_ASSET_TYPE_UNKNOWN, 0 },
	{ "Props/Nested/barrel.mdl", FALSE, LRG_ASSET_TYPE_MODEL, 2000 },
	{ "Textures", TRUE, LRG_ASSET_TYPE_UNKNOWN, 0 },
	{ "Textures/grass.tex", FALSE, LRG_ASSET_TYPE_TEXTURE, 4096 },
	{ "Textures/stone.tex", FALSE, LRG_ASSET_TYPE_TEXTURE, 0 },
	{ "broken.mdl", FALSE, LRG_ASSET_TYPE_MODEL, 10 },
};

#define TEST_TYPE_SOURCE (test_source_get_type ())
G_DECLARE_DERIVABLE_TYPE (TestSource, test_source, TEST, SOURCE, GObject)

struct _TestSourceClass
{
	GObjectClass parent_class;
};

typedef struct
{
	gchar      *id;
	guint       exports;    /* export_entry calls */
	guint       searches;   /* search calls (TestSearchSource) */
	GHashTable *per_path;   /* path -> export count */
} TestSourcePrivate;

static void test_source_iface_init (LrgAssetSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestSource, test_source, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (TestSource)
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_ASSET_SOURCE, test_source_iface_init))

static TestSourcePrivate *
priv_of (gpointer source)
{
	return test_source_get_instance_private (TEST_SOURCE (source));
}

static const gchar *
ts_get_id (LrgAssetSource *self)
{
	return priv_of (self)->id;
}

static const gchar *
ts_get_title (LrgAssetSource *self)
{
	return "Test Source";
}

/* The direct children of @folder in the static tree. */
static GPtrArray *
ts_list (LrgAssetSource  *self,
         const gchar     *folder,
         GError         **error)
{
	GPtrArray *entries = g_ptr_array_new_with_free_func (g_object_unref);
	gboolean   exists = (*folder == '\0');
	gsize      flen = strlen (folder);
	guint      i;

	for (i = 0; i < G_N_ELEMENTS (tree); i++)
	{
		const gchar *rest;

		if (tree[i].folder && g_strcmp0 (tree[i].path, folder) == 0)
			exists = TRUE;
		if (flen > 0 && (!g_str_has_prefix (tree[i].path, folder) || tree[i].path[flen] != '/'))
			continue;
		rest = tree[i].path + (flen > 0 ? flen + 1 : 0);
		if (strchr (rest, '/') != NULL)
			continue;
		{
			LrgAssetSourceEntry *e = lrg_asset_source_entry_new (tree[i].path, tree[i].folder, tree[i].type,
			                                                     tree[i].size);

			if (g_strcmp0 (tree[i].path, "Props/crate.mdl") == 0)
				lrg_asset_source_entry_set_description (e, "A wooden crate");
			g_ptr_array_add (entries, e);
		}
	}
	if (!exists)
	{
		g_ptr_array_unref (entries);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No folder '%s'", folder);
		return NULL;
	}
	return entries;
}

/* Models: <stem>.obj + <stem>.mtl; textures: <stem>.png. */
static gboolean
ts_export (LrgAssetSource  *self,
           const gchar     *path,
           const gchar     *dest_dir,
           gchar          **out_file,
           GError         **error)
{
	TestSourcePrivate *priv = priv_of (self);
	g_autofree gchar  *base = g_path_get_basename (path);
	gchar             *dot = strrchr (base, '.');
	g_autofree gchar  *file = NULL;
	guint              i;

	priv->exports++;
	g_hash_table_insert (priv->per_path, g_strdup (path),
	                     GUINT_TO_POINTER (GPOINTER_TO_UINT (g_hash_table_lookup (priv->per_path, path)) + 1));
	if (dot != NULL)
		*dot = '\0';
	for (i = 0; i < G_N_ELEMENTS (tree); i++)
	{
		if (g_strcmp0 (tree[i].path, path) != 0 || tree[i].folder)
			continue;
		if (g_strcmp0 (path, "broken.mdl") == 0)
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "broken on purpose");
			return FALSE;
		}
		if (tree[i].type == LRG_ASSET_TYPE_MODEL)
		{
			g_autofree gchar *obj = g_strconcat (base, ".obj", NULL);
			g_autofree gchar *mtl = g_strconcat (base, ".mtl", NULL);
			g_autofree gchar *mtl_path = g_build_filename (dest_dir, mtl, NULL);
			g_autofree gchar *text = g_strdup_printf ("mtllib %s\no %s\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", mtl,
			                                          base);

			file = g_build_filename (dest_dir, obj, NULL);
			if (!g_file_set_contents (file, text, -1, error) ||
			    !g_file_set_contents (mtl_path, "newmtl wood\nKd 0.6 0.4 0.2\n", -1, error))
				return FALSE;
		}
		else
		{
			g_autofree gchar  *png = g_strconcat (base, ".png", NULL);
			gsize              len = 0;
			g_autofree guchar *bytes = g_base64_decode (png_1x1, &len);

			file = g_build_filename (dest_dir, png, NULL);
			if (!g_file_set_contents (file, (const gchar *) bytes, (gssize) len, error))
				return FALSE;
		}
		*out_file = g_steal_pointer (&file);
		return TRUE;
	}
	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No entry '%s'", path);
	return FALSE;
}

static void
test_source_iface_init (LrgAssetSourceInterface *iface)
{
	iface->get_id = ts_get_id;
	iface->get_title = ts_get_title;
	iface->list = ts_list;
	iface->export_entry = ts_export;
}

static void
test_source_finalize (GObject *object)
{
	TestSourcePrivate *priv = priv_of (object);

	g_free (priv->id);
	g_hash_table_unref (priv->per_path);
	G_OBJECT_CLASS (test_source_parent_class)->finalize (object);
}

static void
test_source_class_init (TestSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = test_source_finalize;
}

static void
test_source_init (TestSource *self)
{
	TestSourcePrivate *priv = test_source_get_instance_private (self);

	priv->id = g_strdup ("test");
	priv->per_path = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static TestSource *
test_source_new (const gchar *id)
{
	TestSource *self = g_object_new (TEST_TYPE_SOURCE, NULL);

	g_free (priv_of (self)->id);
	priv_of (self)->id = g_strdup (id);
	return self;
}

static guint
exports_of (TestSource  *self,
            const gchar *path)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (priv_of (self)->per_path, path));
}

/* A subclass with its own search (re-implements the interface): it
 * only finds textures, so a test can tell it from the fallback walk. */
#define TEST_TYPE_SEARCH_SOURCE (test_search_source_get_type ())
G_DECLARE_FINAL_TYPE (TestSearchSource, test_search_source, TEST, SEARCH_SOURCE, TestSource)

struct _TestSearchSource
{
	TestSource parent_instance;
};

static void test_search_source_iface_init (LrgAssetSourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (TestSearchSource, test_search_source, TEST_TYPE_SOURCE,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_ASSET_SOURCE, test_search_source_iface_init))

static GPtrArray *
tss_search (LrgAssetSource  *self,
            const gchar     *pattern,
            guint            limit,
            GError         **error)
{
	GPtrArray *out = g_ptr_array_new_with_free_func (g_object_unref);
	guint      i;

	priv_of (self)->searches++;
	for (i = 0; i < G_N_ELEMENTS (tree); i++)
		if (tree[i].type == LRG_ASSET_TYPE_TEXTURE && strstr (tree[i].path, pattern) != NULL)
			g_ptr_array_add (out, lrg_asset_source_entry_new (tree[i].path, FALSE, tree[i].type, tree[i].size));
	return out;
}

static void
test_search_source_iface_init (LrgAssetSourceInterface *iface)
{
	iface->get_id = ts_get_id;
	iface->get_title = ts_get_title;
	iface->list = ts_list;
	iface->export_entry = ts_export;
	iface->search = tss_search;
}

static void
test_search_source_class_init (TestSearchSourceClass *klass)
{
}

static void
test_search_source_init (TestSearchSource *self)
{
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gboolean
has_entry (GPtrArray   *entries,
           const gchar *path)
{
	guint i;

	for (i = 0; entries != NULL && i < entries->len; i++)
		if (g_strcmp0 (lrg_asset_source_entry_get_path (g_ptr_array_index (entries, i)), path) == 0)
			return TRUE;
	return FALSE;
}

/* A per-test cache root inside the fixture's temp dir */
static gchar *
cache_root (EditorAppFixture *f)
{
	return g_build_filename (f->dir, "cache", NULL);
}

static LrgEditorSourcesPanel *
present_panel (EditorAppFixture *f)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *panel = lrg_dock_area_get_panel (dock, "sources");

	g_assert_nonnull (panel);
	lrg_dock_area_present_panel (dock, panel);
	efx_frames (f, 2);
	g_assert_true (LRG_IS_EDITOR_SOURCES_PANEL (lrg_dock_panel_get_child (panel)));
	return LRG_EDITOR_SOURCES_PANEL (lrg_dock_panel_get_child (panel));
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

/* A fixture app whose default export cache starts empty, so export
 * counts do not depend on which tests ran before (the cache root is
 * process-wide: $XDG_CACHE_HOME is fixed in main()). */
static void
plain_setup (EditorAppFixture *f,
             gconstpointer     data)
{
	g_autofree gchar *sources = g_build_filename (g_get_user_cache_dir (), "libregnum", "editor", "sources", NULL);

	efx_rm_rf (sources);
	efx_setup (f, data);
}

/* An app with a project and the "test" source registered. */
static void
sources_setup (EditorAppFixture *f,
               gconstpointer     data)
{
	g_autoptr(GError)     error = NULL;
	g_autoptr(TestSource) source = test_source_new ("test");

	plain_setup (f, data);
	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Sources Test", &error));
	g_assert_no_error (error);
	lrg_editor_app_add_asset_source (f->app, LRG_ASSET_SOURCE (source));
	efx_frames (f, 2);
}

/* The JSON error code of a reply line. */
static gchar *
ipc_error_code (EditorAppFixture *f,
                const gchar      *request)
{
	g_autofree gchar     *reply = efx_ipc (f, request);
	g_autoptr(JsonParser) parser = json_parser_new ();
	JsonObject           *o;

	g_assert_true (json_parser_load_from_data (parser, reply, -1, NULL));
	o = json_node_get_object (json_parser_get_root (parser));
	if (!json_object_has_member (o, "error"))
		return NULL;
	return g_strdup (json_object_get_string_member_with_default (json_object_get_object_member (o, "error"),
	                                                             "code", ""));
}

/* ==========================================================================
 * Core: paths, entries, listing, search, cache
 * ========================================================================== */

static void
test_paths (void)
{
	g_autoptr(GError)              error = NULL;
	g_autoptr(LrgAssetSourceEntry) e = NULL;
	gchar                         *p;

	p = lrg_asset_source_normalize_path (NULL, &error);
	g_assert_cmpstr (p, ==, "");
	g_free (p);
	p = lrg_asset_source_normalize_path ("/a//b/c/", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (p, ==, "a/b/c");
	g_free (p);
	g_assert_null (lrg_asset_source_normalize_path ("a/../b", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME);
	g_clear_error (&error);
	g_assert_null (lrg_asset_source_normalize_path ("a\\b", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME);
	g_clear_error (&error);

	e = lrg_asset_source_entry_new ("World/Azeroth/tree.m2", FALSE, LRG_ASSET_TYPE_MODEL, 99);
	g_assert_cmpstr (lrg_asset_source_entry_get_path (e), ==, "World/Azeroth/tree.m2");
	g_assert_cmpstr (lrg_asset_source_entry_get_name (e), ==, "tree.m2");
	g_assert_false (lrg_asset_source_entry_is_folder (e));
	g_assert_cmpint (lrg_asset_source_entry_get_asset_type (e), ==, LRG_ASSET_TYPE_MODEL);
	g_assert_cmpuint (lrg_asset_source_entry_get_size (e), ==, 99);
	g_assert_null (lrg_asset_source_entry_get_description (e));
	lrg_asset_source_entry_set_description (e, "M2 model");
	g_assert_cmpstr (lrg_asset_source_entry_get_description (e), ==, "M2 model");
	{
		g_autofree gchar *name = NULL;

		g_object_get (e, "name", &name, NULL);
		g_assert_cmpstr (name, ==, "tree.m2");
	}
}

static void
test_list_and_search (void)
{
	g_autoptr(TestSource)          source = test_source_new ("test");
	g_autoptr(TestSource)          searcher = g_object_new (TEST_TYPE_SEARCH_SOURCE, NULL);
	LrgAssetSource                *s = LRG_ASSET_SOURCE (source);
	g_autoptr(GPtrArray)           entries = NULL;
	g_autoptr(GPtrArray)           found = NULL;
	g_autoptr(GError)              error = NULL;
	g_autoptr(LrgAssetSourceEntry) e = NULL;

	g_assert_cmpstr (lrg_asset_source_get_id (s), ==, "test");
	g_assert_cmpstr (lrg_asset_source_get_title (s), ==, "Test Source");

	/* Root and a nested folder; the path is normalised first */
	entries = lrg_asset_source_list (s, "", &error);
	g_assert_no_error (error);
	g_assert_cmpuint (entries->len, ==, 3);
	g_assert_true (has_entry (entries, "Props"));
	g_assert_true (has_entry (entries, "broken.mdl"));
	g_clear_pointer (&entries, g_ptr_array_unref);
	entries = lrg_asset_source_list (s, "/Props/", &error);
	g_assert_no_error (error);
	g_assert_cmpuint (entries->len, ==, 2);
	g_assert_true (has_entry (entries, "Props/Nested"));
	g_clear_pointer (&entries, g_ptr_array_unref);
	g_assert_null (lrg_asset_source_list (s, "Nope", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);
	g_assert_null (lrg_asset_source_list (s, "../etc", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME);
	g_clear_error (&error);

	/* find_entry lists the parent */
	e = lrg_asset_source_find_entry (s, "Props/Nested/barrel.mdl", &error);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_asset_source_entry_get_asset_type (e), ==, LRG_ASSET_TYPE_MODEL);
	g_assert_null (lrg_asset_source_find_entry (s, "Props/none.mdl", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);

	/* Fallback search: walks the tree, case-insensitive, honours limit */
	g_assert_false (lrg_asset_source_implements_search (s));
	found = lrg_asset_source_search (s, "BARREL", 0, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (found->len, ==, 1);
	g_assert_true (has_entry (found, "Props/Nested/barrel.mdl"));
	g_clear_pointer (&found, g_ptr_array_unref);
	found = lrg_asset_source_search (s, ".", 2, &error);
	g_assert_cmpuint (found->len, ==, 2);
	g_clear_pointer (&found, g_ptr_array_unref);

	/* The source's own search is used when it has one */
	g_assert_true (lrg_asset_source_implements_search (LRG_ASSET_SOURCE (searcher)));
	found = lrg_asset_source_search (LRG_ASSET_SOURCE (searcher), "s", 0, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (priv_of (searcher)->searches, ==, 1);
	g_assert_cmpuint (found->len, ==, 2);   /* grass.tex, stone.tex: textures only */
	g_assert_true (has_entry (found, "Textures/grass.tex"));
	g_clear_pointer (&found, g_ptr_array_unref);
	found = lrg_asset_source_search (LRG_ASSET_SOURCE (searcher), "s", 1, &error);
	g_assert_cmpuint (found->len, ==, 1);
}

static void
test_export_cache (EditorAppFixture *f,
                   gconstpointer     data)
{
	g_autoptr(TestSource) source = test_source_new ("cache-test");
	g_autoptr(TestSource) again = NULL;
	g_autoptr(GError)     error = NULL;
	g_autofree gchar     *root = cache_root (f);
	g_autofree gchar     *first = NULL;
	g_autofree gchar     *second = NULL;
	g_autofree gchar     *third = NULL;
	g_autofree gchar     *entry_dir = NULL;
	g_autofree gchar     *marker = NULL;
	g_autofree gchar     *mtl = NULL;
	g_autofree gchar     *def = NULL;

	/* The default root is under the user cache dir, by id */
	def = lrg_asset_source_get_cache_dir (LRG_ASSET_SOURCE (source));
	g_assert_true (g_str_has_suffix (def, "libregnum/editor/sources/cache-test"));
	g_assert_true (g_str_has_prefix (def, g_get_user_cache_dir ()));

	/* First call exports; the second reuses it */
	first = lrg_asset_source_export_cached (LRG_ASSET_SOURCE (source), "Props/crate.mdl", root, &error);
	g_assert_no_error (error);
	g_assert_true (g_file_test (first, G_FILE_TEST_IS_REGULAR));
	g_assert_true (g_str_has_suffix (first, "crate.obj"));
	entry_dir = lrg_asset_source_get_cache_entry_dir (LRG_ASSET_SOURCE (source), "Props/crate.mdl", root);
	g_assert_true (g_str_has_prefix (first, entry_dir));
	marker = g_build_filename (entry_dir, LRG_ASSET_SOURCE_CACHE_MARKER, NULL);
	g_assert_true (g_file_test (marker, G_FILE_TEST_IS_REGULAR));
	mtl = g_build_filename (entry_dir, "crate.mtl", NULL);
	g_assert_true (g_file_test (mtl, G_FILE_TEST_IS_REGULAR));
	second = lrg_asset_source_export_cached (LRG_ASSET_SOURCE (source), "/Props//crate.mdl", root, &error);
	g_assert_cmpstr (first, ==, second);
	g_assert_cmpuint (exports_of (source, "Props/crate.mdl"), ==, 1);

	/* Another instance (a later editor run) finds the marker */
	again = test_source_new ("cache-test");
	g_free (second);
	second = lrg_asset_source_export_cached (LRG_ASSET_SOURCE (again), "Props/crate.mdl", root, &error);
	g_assert_cmpstr (first, ==, second);
	g_assert_cmpuint (priv_of (again)->exports, ==, 0);

	/* Invalidation exports again */
	lrg_asset_source_invalidate_cache (LRG_ASSET_SOURCE (source), "Props/crate.mdl", root);
	g_assert_false (g_file_test (marker, G_FILE_TEST_EXISTS));
	third = lrg_asset_source_export_cached (LRG_ASSET_SOURCE (source), "Props/crate.mdl", root, &error);
	g_assert_cmpstr (third, ==, first);
	g_assert_cmpuint (exports_of (source, "Props/crate.mdl"), ==, 2);
	lrg_asset_source_invalidate_cache (LRG_ASSET_SOURCE (source), NULL, root);
	g_assert_false (g_file_test (marker, G_FILE_TEST_EXISTS));

	/* Failures are not cached, folders and the root are refused */
	g_assert_null (lrg_asset_source_export_cached (LRG_ASSET_SOURCE (source), "broken.mdl", root, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_clear_error (&error);
	g_assert_null (lrg_asset_source_export_cached (LRG_ASSET_SOURCE (source), "broken.mdl", root, &error));
	g_clear_error (&error);
	g_assert_cmpuint (exports_of (source, "broken.mdl"), ==, 2);
	g_assert_null (lrg_asset_source_export_cached (LRG_ASSET_SOURCE (source), "", root, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY);
	g_clear_error (&error);
	g_assert_null (lrg_asset_source_export_cached (LRG_ASSET_SOURCE (source), "Props", root, &error));
	g_assert_nonnull (error);
}

/* ==========================================================================
 * The app: registry, panel, import, IPC
 * ========================================================================== */

static void
test_registry (EditorAppFixture *f,
               gconstpointer     data)
{
	g_autoptr(TestSource) a = test_source_new ("alpha");
	g_autoptr(TestSource) b = test_source_new ("beta");
	g_autoptr(TestSource) a2 = test_source_new ("alpha");
	g_autoptr(GPtrArray)  list = NULL;

	list = lrg_editor_app_get_asset_sources (f->app);
	g_assert_cmpuint (list->len, ==, 0);
	g_clear_pointer (&list, g_ptr_array_unref);

	lrg_editor_app_add_asset_source (f->app, LRG_ASSET_SOURCE (a));
	lrg_editor_app_add_asset_source (f->app, LRG_ASSET_SOURCE (b));
	g_assert_true (lrg_editor_app_lookup_asset_source (f->app, "alpha") == LRG_ASSET_SOURCE (a));
	g_assert_null (lrg_editor_app_lookup_asset_source (f->app, "gamma"));

	/* Same id replaces, keeping the order */
	lrg_editor_app_add_asset_source (f->app, LRG_ASSET_SOURCE (a2));
	list = lrg_editor_app_get_asset_sources (f->app);
	g_assert_cmpuint (list->len, ==, 2);
	g_assert_true (g_ptr_array_index (list, 0) == (gpointer) a2);
	g_assert_true (g_ptr_array_index (list, 1) == (gpointer) b);
	g_clear_pointer (&list, g_ptr_array_unref);

	g_assert_true (lrg_editor_app_remove_asset_source (f->app, "beta"));
	g_assert_false (lrg_editor_app_remove_asset_source (f->app, "beta"));
	list = lrg_editor_app_get_asset_sources (f->app);
	g_assert_cmpuint (list->len, ==, 1);
}

static void
test_panel_empty (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorSourcesPanel *panel = present_panel (f);
	g_autoptr(TestSource)  source = NULL;

	/* No source: the hint, nothing listed */
	g_assert_null (lrg_editor_sources_panel_get_source (panel));
	g_assert_true (lrg_widget_get_visible (find (f, "sources-empty")));
	g_assert_false (lrg_widget_get_visible (find (f, "sources-list")));
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 0);

	/* A source registered later shows up on the next frame */
	source = test_source_new ("late");
	lrg_editor_app_add_asset_source (f->app, LRG_ASSET_SOURCE (source));
	efx_frames (f, 2);
	g_assert_false (lrg_widget_get_visible (find (f, "sources-empty")));
	g_assert_true (lrg_widget_get_visible (find (f, "sources-list")));
	g_assert_cmpstr (lrg_asset_source_get_id (lrg_editor_sources_panel_get_source (panel)), ==, "late");
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 3);

	/* Replacing it under the same id re-lists the new object */
	{
		g_autoptr(TestSource) again = test_source_new ("late");

		g_assert_true (lrg_editor_sources_panel_set_folder (panel, "Props", NULL));
		lrg_editor_app_add_asset_source (f->app, LRG_ASSET_SOURCE (again));
		efx_frames (f, 2);
		g_assert_true (lrg_editor_sources_panel_get_source (panel) == LRG_ASSET_SOURCE (again));
		g_assert_cmpstr (lrg_editor_sources_panel_get_folder (panel), ==, "");
	}

	/* And goes away again */
	g_assert_true (lrg_editor_app_remove_asset_source (f->app, "late"));
	efx_frames (f, 2);
	g_assert_null (lrg_editor_sources_panel_get_source (panel));
	g_assert_true (lrg_widget_get_visible (find (f, "sources-empty")));
}

static void
test_panel_browse (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorSourcesPanel *panel = present_panel (f);
	LrgAssetSourceEntry   *e;
	g_autoptr(GError)      error = NULL;
	LrgTable              *table = LRG_TABLE (find (f, "sources-list"));
	gint                   at;

	/* Root: folders first, then names */
	g_assert_cmpstr (lrg_editor_sources_panel_get_folder (panel), ==, "");
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 3);
	g_assert_cmpstr (lrg_asset_source_entry_get_path (lrg_editor_sources_panel_get_entry (panel, 0)), ==, "Props");
	g_assert_cmpstr (lrg_asset_source_entry_get_path (lrg_editor_sources_panel_get_entry (panel, 1)), ==, "Textures");
	g_assert_cmpstr (lrg_asset_source_entry_get_path (lrg_editor_sources_panel_get_entry (panel, 2)), ==,
	                 "broken.mdl");
	g_assert_cmpstr (lrg_table_get_cell_text (table, 0, 0), ==, "Props/");
	g_assert_cmpstr (lrg_table_get_cell_text (table, 2, 1), ==, "Model");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (find (f, "sources-count"))), ==, "3 items");
	g_assert_false (lrg_widget_get_enabled (find (f, "sources-up")));

	/* Activating a folder opens it */
	g_assert_true (lrg_editor_sources_panel_activate (panel, 0));
	efx_frame (f);
	g_assert_cmpstr (lrg_editor_sources_panel_get_folder (panel), ==, "Props");
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 2);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (find (f, "sources-path"))), ==, "Test Source / Props");
	g_assert_true (lrg_widget_get_enabled (find (f, "sources-up")));
	g_assert_true (lrg_editor_sources_panel_set_folder (panel, "Props/Nested", &error));
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 1);
	g_assert_false (lrg_editor_sources_panel_set_folder (panel, "Nope", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);
	g_assert_cmpstr (lrg_editor_sources_panel_get_folder (panel), ==, "Props/Nested");

	/* The Up chip, clicked */
	efx_frames (f, 2);
	efx_click_widget (f, find (f, "sources-up"));
	g_assert_cmpstr (lrg_editor_sources_panel_get_folder (panel), ==, "Props");
	lrg_editor_sources_panel_go_up (panel);
	g_assert_cmpstr (lrg_editor_sources_panel_get_folder (panel), ==, "");

	/* Filter typed into the field: folder-local, case-insensitive */
	efx_frames (f, 2);
	efx_click_widget (f, find (f, "sources-filter"));
	efx_type (f, "BRO");
	efx_frame (f);
	g_assert_cmpstr (lrg_editor_sources_panel_get_filter (panel), ==, "BRO");
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 1);
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (find (f, "sources-count"))), ==, "1 of 3 items");
	lrg_editor_sources_panel_set_filter (panel, NULL);
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 3);
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (find (f, "sources-filter"))), ==, "");

	/* No own search: no Search button, search() refuses */
	g_assert_false (lrg_widget_get_visible (find (f, "sources-search")));
	g_assert_false (lrg_editor_sources_panel_search (panel, "grass", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_clear_error (&error);

	/* Selecting through the table schedules the preview for the next
	 * frame: export to the cache, then the preview file is known */
	g_assert_true (lrg_editor_sources_panel_set_folder (panel, "Textures", &error));
	at = lrg_editor_sources_panel_find_entry (panel, "Textures/grass.tex");
	g_assert_cmpint (at, >=, 0);
	lrg_table_select (table, (guint) at);
	e = lrg_editor_sources_panel_get_selected (panel);
	g_assert_nonnull (e);
	g_assert_cmpstr (lrg_asset_source_entry_get_path (e), ==, "Textures/grass.tex");
	g_assert_null (lrg_editor_sources_panel_get_preview_file (panel));
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (find (f, "sources-state"))), ==, "Exporting...");
	efx_frame (f);
	g_assert_nonnull (lrg_editor_sources_panel_get_preview_file (panel));
	g_assert_true (g_str_has_suffix (lrg_editor_sources_panel_get_preview_file (panel), "grass.png"));
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (find (f, "sources-name"))), ==, "grass.tex");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (find (f, "sources-state"))), ==, "Cached: grass.png");
	g_assert_true (lrg_widget_get_enabled (find (f, "sources-import")));

	/* Re-selecting is instant (cached): still one export */
	lrg_editor_sources_panel_select (panel, "Textures/stone.tex");
	lrg_editor_sources_panel_update_preview (panel);
	lrg_editor_sources_panel_select (panel, "Textures/grass.tex");
	lrg_editor_sources_panel_update_preview (panel);
	{
		TestSource *s = TEST_SOURCE (lrg_editor_sources_panel_get_source (panel));

		g_assert_cmpuint (exports_of (s, "Textures/grass.tex"), ==, 1);
		g_assert_cmpuint (exports_of (s, "Textures/stone.tex"), ==, 1);
	}

	/* A failing export is shown, not fatal */
	g_assert_true (lrg_editor_sources_panel_set_folder (panel, "", &error));
	g_assert_true (lrg_editor_sources_panel_select (panel, "broken.mdl"));
	lrg_editor_sources_panel_update_preview (panel);
	g_assert_null (lrg_editor_sources_panel_get_preview_file (panel));
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (find (f, "sources-state"))), ==, "broken on purpose");
	g_assert_false (lrg_editor_sources_panel_select (panel, "Textures/none.tex"));
}

static void
test_panel_search_and_chooser (EditorAppFixture *f,
                               gconstpointer     data)
{
	LrgEditorSourcesPanel *panel = present_panel (f);
	g_autoptr(TestSource)  searcher = g_object_new (TEST_TYPE_SEARCH_SOURCE, NULL);
	g_autoptr(GError)      error = NULL;

	/* A second source: the chooser appears; switch to it */
	g_free (priv_of (searcher)->id);
	priv_of (searcher)->id = g_strdup ("searchable");
	lrg_editor_app_add_asset_source (f->app, LRG_ASSET_SOURCE (searcher));
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_visible (find (f, "sources-source")));
	g_assert_cmpuint (lrg_combo_box_get_n_items (LRG_COMBO_BOX (find (f, "sources-source"))), ==, 2);
	g_assert_cmpstr (lrg_asset_source_get_id (lrg_editor_sources_panel_get_source (panel)), ==, "test");
	lrg_combo_box_set_selected (LRG_COMBO_BOX (find (f, "sources-source")), 1);
	efx_frame (f);
	g_assert_cmpstr (lrg_asset_source_get_id (lrg_editor_sources_panel_get_source (panel)), ==, "searchable");
	g_assert_true (lrg_widget_get_visible (find (f, "sources-search")));
	g_assert_false (lrg_editor_sources_panel_set_source (panel, "nope"));

	/* Enter in the filter searches the whole source */
	efx_frames (f, 2);
	efx_click_widget (f, find (f, "sources-filter"));
	efx_type (f, "grass");
	efx_key (f, GRL_KEY_ENTER, 0);
	efx_frame (f);
	g_assert_true (lrg_editor_sources_panel_is_searching (panel));
	g_assert_cmpuint (priv_of (searcher)->searches, ==, 1);
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 1);
	g_assert_cmpstr (lrg_asset_source_entry_get_path (lrg_editor_sources_panel_get_entry (panel, 0)), ==,
	                 "Textures/grass.tex");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (find (f, "sources-count"))), ==, "1 result");

	/* Up leaves the results for the folder the search started in */
	g_assert_true (lrg_editor_sources_panel_search (panel, "s", &error));
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 2);
	lrg_editor_sources_panel_go_up (panel);
	g_assert_false (lrg_editor_sources_panel_is_searching (panel));
	g_assert_cmpstr (lrg_editor_sources_panel_get_folder (panel), ==, "");
	g_assert_cmpuint (lrg_editor_sources_panel_get_n_entries (panel), ==, 3);
}

static void
test_import_place_export (EditorAppFixture *f,
                          gconstpointer     data)
{
	LrgEditorSourcesPanel *panel = present_panel (f);
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *rel = NULL;
	g_autofree gchar      *abs = NULL;
	g_autofree gchar      *mtl = NULL;
	g_autofree gchar      *marker = NULL;
	g_autofree gchar      *out = NULL;
	g_autofree gchar      *dest = NULL;
	LrgNode               *node;
	TestSource            *source = TEST_SOURCE (lrg_editor_sources_panel_get_source (panel));

	/* Import: assets/<id>/<folder>/, side files along, no marker */
	g_assert_true (lrg_editor_sources_panel_set_folder (panel, "Props", &error));
	g_assert_true (lrg_editor_sources_panel_select (panel, "Props/crate.mdl"));
	rel = lrg_editor_sources_panel_import_selected (panel, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (rel, ==, "assets/test/Props/crate.obj");
	abs = g_build_filename (f->project, rel, NULL);
	g_assert_true (g_file_test (abs, G_FILE_TEST_IS_REGULAR));
	mtl = g_build_filename (f->project, "assets", "test", "Props", "crate.mtl", NULL);
	g_assert_true (g_file_test (mtl, G_FILE_TEST_IS_REGULAR));
	marker = g_build_filename (f->project, "assets", "test", "Props", LRG_ASSET_SOURCE_CACHE_MARKER, NULL);
	g_assert_false (g_file_test (marker, G_FILE_TEST_EXISTS));
	g_assert_cmpstr (lrg_editor_app_get_status (f->app), ==, "Imported assets/test/Props/crate.obj");

	/* Place: imports (cached: no new export) and adds a mesh node */
	node = lrg_editor_sources_panel_place_selected (panel, &error);
	g_assert_no_error (error);
	g_assert_nonnull (node);
	g_assert_cmpstr (lrg_node_get_name (node), ==, "crate");
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (node)), ==, LRG_NODE_VISUAL_MESH_ASSET);
	g_assert_cmpstr (lrg_node_visual_get_asset (lrg_node_get_visual (node)), ==, "assets/test/Props/crate.obj");
	g_assert_true (lrg_editor_selection_get_primary (lrg_editor_get_selection (f->editor)) == node);
	g_assert_cmpuint (exports_of (source, "Props/crate.mdl"), ==, 1);
	lrg_editor_undo (f->editor);
	g_assert_null (lrg_editor_find_node (f->editor, lrg_node_get_guid (node)));

	/* Export to a folder */
	dest = g_build_filename (f->dir, "out", NULL);
	out = lrg_editor_sources_panel_export_selected (panel, dest, &error);
	g_assert_no_error (error);
	g_assert_true (g_str_has_prefix (out, dest));
	g_assert_true (g_file_test (out, G_FILE_TEST_IS_REGULAR));

	/* A folder is not an asset */
	g_assert_true (lrg_editor_sources_panel_select (panel, "Props/Nested"));
	g_assert_false (lrg_widget_get_enabled (find (f, "sources-import")));
	g_assert_null (lrg_editor_sources_panel_import_selected (panel, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

static void
test_import_needs_project (EditorAppFixture *f,
                          gconstpointer     data)
{
	g_autoptr(TestSource) source = test_source_new ("test");
	g_autoptr(GError)     error = NULL;
	LrgEditorSourcesPanel *panel;
	g_autofree gchar      *cached = NULL;

	lrg_editor_app_add_asset_source (f->app, LRG_ASSET_SOURCE (source));
	panel = present_panel (f);
	g_assert_null (lrg_editor_app_import_source_entry (f->app, "test", "Props/crate.mdl", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_clear_error (&error);

	/* The panel says so in the status bar */
	g_assert_true (lrg_editor_sources_panel_set_folder (panel, "Props", &error));
	g_assert_true (lrg_editor_sources_panel_select (panel, "Props/crate.mdl"));
	g_assert_null (lrg_editor_sources_panel_import_selected (panel, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
	g_clear_error (&error);
	g_assert_true (g_str_has_prefix (lrg_editor_app_get_status (f->app), "Open a project first"));

	/* Exporting still works (into the cache); unknown sources do not */
	cached = lrg_editor_app_export_source_entry (f->app, "test", "Props/crate.mdl", NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (cached);
	g_assert_null (lrg_editor_app_export_source_entry (f->app, "nope", "Props/crate.mdl", NULL, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	g_autoptr(JsonNode) r = NULL;
	g_autofree gchar   *code = NULL;
	g_autofree gchar   *dest = NULL;
	g_autofree gchar   *req = NULL;
	JsonArray          *a;
	JsonObject         *o;

	/* source.list */
	r = efx_ipc_result (f, "{'id':1,'method':'source.list'}");
	a = json_node_get_array (r);
	g_assert_cmpuint (json_array_get_length (a), ==, 1);
	o = json_array_get_object_element (a, 0);
	g_assert_cmpstr (json_object_get_string_member (o, "id"), ==, "test");
	g_assert_cmpstr (json_object_get_string_member (o, "title"), ==, "Test Source");
	g_assert_false (json_object_get_boolean_member (o, "search"));
	g_clear_pointer (&r, json_node_unref);

	/* source.ls */
	r = efx_ipc_result (f, "{'id':2,'method':'source.ls','params':{'source':'test','folder':'Props'}}");
	o = json_node_get_object (r);
	g_assert_cmpstr (json_object_get_string_member (o, "folder"), ==, "Props");
	a = json_object_get_array_member (o, "entries");
	g_assert_cmpuint (json_array_get_length (a), ==, 2);
	o = json_array_get_object_element (a, 0);
	g_assert_cmpstr (json_object_get_string_member (o, "path"), ==, "Props/crate.mdl");
	g_assert_cmpstr (json_object_get_string_member (o, "type"), ==, "model");
	g_assert_cmpint (json_object_get_int_member (o, "size"), ==, 1234);
	g_assert_cmpstr (json_object_get_string_member (o, "description"), ==, "A wooden crate");
	o = json_array_get_object_element (a, 1);
	g_assert_true (json_object_get_boolean_member (o, "folder"));
	g_assert_cmpstr (json_object_get_string_member (o, "type"), ==, "folder");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':3,'method':'source.ls','params':{'source':'test'}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "entries")), ==,
	                  3);
	g_clear_pointer (&r, json_node_unref);

	/* source.search (fallback walk), with a limit */
	r = efx_ipc_result (f, "{'id':4,'method':'source.search','params':{'source':'test','pattern':'tex'}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "entries")), ==,
	                  3);   /* Textures/, grass.tex, stone.tex */
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f,
	                    "{'id':5,'method':'source.search','params':{'source':'test','pattern':'tex','limit':1}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "entries")), ==,
	                  1);
	g_clear_pointer (&r, json_node_unref);

	/* source.export: cached file, and copied to dest */
	r = efx_ipc_result (f, "{'id':6,'method':'source.export','params':{'source':'test','path':'Textures/grass.tex'}}");
	g_assert_true (g_str_has_suffix (json_object_get_string_member (json_node_get_object (r), "file"), "grass.png"));
	g_assert_true (g_str_has_prefix (json_object_get_string_member (json_node_get_object (r), "file"),
	                                 g_get_user_cache_dir ()));
	g_clear_pointer (&r, json_node_unref);
	dest = g_build_filename (f->dir, "exported", NULL);
	req = g_strdup_printf ("{'id':7,'method':'source.export','params':{'source':'test','path':'Textures/grass.tex',"
	                       "'dest':'%s'}}", dest);
	r = efx_ipc_result (f, req);
	g_assert_true (g_str_has_prefix (json_object_get_string_member (json_node_get_object (r), "file"), dest));
	g_assert_true (g_file_test (json_object_get_string_member (json_node_get_object (r), "file"),
	                            G_FILE_TEST_IS_REGULAR));
	g_clear_pointer (&r, json_node_unref);

	/* source.import and source.place */
	r = efx_ipc_result (f, "{'id':8,'method':'source.import','params':{'source':'test','path':'Textures/grass.tex'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "asset"), ==,
	                 "assets/test/Textures/grass.png");
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':9,'method':'source.place','params':{'source':'test','path':'Textures/grass.tex'}}");
	o = json_node_get_object (r);
	g_assert_cmpstr (json_object_get_string_member (o, "asset"), ==, "assets/test/Textures/grass.png");
	g_assert_cmpstr (json_object_get_string_member (o, "name"), ==, "grass");
	g_assert_nonnull (lrg_editor_find_node (f->editor, json_object_get_string_member (o, "node")));
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (
	                     lrg_editor_find_node (f->editor, json_object_get_string_member (o, "node")))), ==,
	                 LRG_NODE_VISUAL_SPRITE);
	g_clear_pointer (&r, json_node_unref);

	/* Errors */
	code = ipc_error_code (f, "{'id':10,'method':'source.ls','params':{'source':'nope'}}");
	g_assert_cmpstr (code, ==, "not-found");
	g_free (code);
	code = ipc_error_code (f, "{'id':11,'method':'source.ls','params':{}}");
	g_assert_cmpstr (code, ==, "invalid-params");
	g_free (code);
	code = ipc_error_code (f, "{'id':12,'method':'source.ls','params':{'source':'test','folder':'Nope'}}");
	g_assert_cmpstr (code, ==, "not-found");
	g_free (code);
	code = ipc_error_code (f, "{'id':13,'method':'source.ls','params':{'source':'test','folder':'../x'}}");
	g_assert_cmpstr (code, ==, "invalid-params");
	g_free (code);
	code = ipc_error_code (f, "{'id':14,'method':'source.export','params':{'source':'test','path':'broken.mdl'}}");
	g_assert_cmpstr (code, ==, "failed");
	g_free (code);
	code = ipc_error_code (f, "{'id':15,'method':'source.search','params':{'source':'test'}}");
	g_assert_cmpstr (code, ==, "invalid-params");
	g_free (code);
	code = ipc_error_code (f, "{'id':16,'method':'source.import','params':{'source':'test','path':'Props/none.mdl'}}");
	g_assert_cmpstr (code, ==, "not-found");
	g_free (code);
	code = NULL;
}

/* ==========================================================================
 * Game module editor hook
 * ========================================================================== */

static void
test_module_hook (EditorAppFixture *f,
                  gconstpointer     data)
{
	g_autoptr(LrgLoadedGame) game = NULL;
	g_autoptr(LrgLoadedGame) plain = NULL;
	g_autoptr(GError)        error = NULL;
	g_autoptr(GPtrArray)     list = NULL;
	LrgAssetSource          *fixture;
	g_autofree gchar        *file = NULL;

	game = lrg_loaded_game_load (TEST_EDITOR_MODULE_PATH, &error);
	g_assert_no_error (error);
	g_assert_nonnull (game);

	/* The optional symbols */
	g_assert_nonnull (lrg_loaded_game_lookup_symbol (game, LRG_GAME_MODULE_EDITOR_SYMBOL));
	g_assert_nonnull (lrg_loaded_game_lookup_symbol (game, LRG_GAME_MODULE_ENTRY_SYMBOL));
	g_assert_null (lrg_loaded_game_lookup_symbol (game, "no_such_symbol_here"));

	/* A failing hook reports its error and registers nothing */
	g_object_set_data (G_OBJECT (f->app), "lrg-test-editor-hook-fail", GINT_TO_POINTER (1));
	g_assert_false (lrg_editor_app_init_module (f->app, game, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_assert_cmpstr (error->message, ==, "fixture hook asked to fail");
	g_clear_error (&error);
	g_object_set_data (G_OBJECT (f->app), "lrg-test-editor-hook-fail", NULL);
	list = lrg_editor_app_get_asset_sources (f->app);
	g_assert_cmpuint (list->len, ==, 0);
	g_clear_pointer (&list, g_ptr_array_unref);

	/* The hook registers the module's source; it works end to end */
	g_assert_true (lrg_editor_app_init_module (f->app, game, &error));
	g_assert_no_error (error);
	fixture = lrg_editor_app_lookup_asset_source (f->app, "fixture");
	g_assert_nonnull (fixture);
	g_assert_cmpstr (lrg_asset_source_get_title (fixture), ==, "Fixture Source");
	file = lrg_editor_app_export_source_entry (f->app, "fixture", "Props/crate.mdl", NULL, &error);
	g_assert_no_error (error);
	g_assert_true (g_str_has_suffix (file, "crate.obj"));

	/* A module without a hook needs nothing: success */
	plain = lrg_loaded_game_load (TEST_ARGS_MODULE_PATH, &error);
	g_assert_no_error (error);
	g_assert_null (lrg_loaded_game_lookup_symbol (plain, LRG_GAME_MODULE_EDITOR_SYMBOL));
	g_assert_true (lrg_editor_app_init_module (f->app, plain, &error));
	g_assert_no_error (error);

	/* After unload there is nothing to look up */
	lrg_loaded_game_unload (plain);
	g_assert_null (lrg_loaded_game_lookup_symbol (plain, LRG_GAME_MODULE_ENTRY_SYMBOL));
}

int
main (int    argc,
      char **argv)
{
	g_autofree gchar *data_home = NULL;
	g_autofree gchar *cache_home = NULL;
	g_autofree gchar *config_home = NULL;
	int               status;

	/* Before any GLib call caches them: private HOME and XDG dirs */
	test_root = g_dir_make_tmp ("lrg-app-sources-XXXXXX", NULL);
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

	g_test_add_func ("/editor/sources/paths", test_paths);
	g_test_add_func ("/editor/sources/list-and-search", test_list_and_search);
	g_test_add ("/editor/sources/export-cache", EditorAppFixture, NULL, efx_setup, test_export_cache, efx_teardown);
	g_test_add ("/editor/sources/registry", EditorAppFixture, NULL, efx_setup, test_registry, efx_teardown);
	g_test_add ("/editor/sources/panel-empty", EditorAppFixture, NULL, plain_setup, test_panel_empty, efx_teardown);
	g_test_add ("/editor/sources/panel-browse", EditorAppFixture, NULL, sources_setup, test_panel_browse,
	            efx_teardown);
	g_test_add ("/editor/sources/panel-search-and-chooser", EditorAppFixture, NULL, sources_setup,
	            test_panel_search_and_chooser, efx_teardown);
	g_test_add ("/editor/sources/import-place-export", EditorAppFixture, NULL, sources_setup,
	            test_import_place_export, efx_teardown);
	g_test_add ("/editor/sources/import-needs-project", EditorAppFixture, NULL, plain_setup,
	            test_import_needs_project, efx_teardown);
	g_test_add ("/editor/sources/ipc", EditorAppFixture, NULL, sources_setup, test_ipc, efx_teardown);
	g_test_add ("/editor/sources/module-hook", EditorAppFixture, NULL, plain_setup, test_module_hook, efx_teardown);

	status = g_test_run ();
	efx_rm_rf (test_root);
	g_free (test_root);
	return status;
}
