/* test-vfs.c - LrgVfs, LrgVfsSource and LrgVfsDirSource
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * The VFS is exercised through two test-only sources: TestMemSource
 * (an in-memory table implementing every vfunc, with switches for
 * case-insensitive matching and for failing reads and listings) and
 * TestBareSource (only the required vfuncs, to check how the optional
 * ones are handled).  LrgVfsDirSource is tested on a temporary tree
 * inside the isolated directories GTest provides.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* TestMemSource                                                      */
/* ------------------------------------------------------------------ */

#define TEST_TYPE_MEM_SOURCE (test_mem_source_get_type ())
G_DECLARE_FINAL_TYPE (TestMemSource, test_mem_source, TEST, MEM_SOURCE, GObject)

struct _TestMemSource
{
    GObject     parent_instance;

    gchar      *name;
    gboolean    case_insensitive;
    GHashTable *files;      /* key -> GBytes, or NULL for a broken file */
    GHashTable *spellings;  /* key -> path as added */
    gboolean    fail_list;
    gchar      *last_path;  /* the last path a vfunc was asked about */
};

static void test_mem_source_iface_init (LrgVfsSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestMemSource, test_mem_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_VFS_SOURCE, test_mem_source_iface_init))

static gchar *
mem_key (TestMemSource *self,
         const gchar   *path)
{
    return self->case_insensitive ? g_ascii_strdown (path, -1) : g_strdup (path);
}

static void
mem_seen (TestMemSource *self,
          const gchar   *path)
{
    g_free (self->last_path);
    self->last_path = g_strdup (path);
}

static const gchar *
mem_get_name (LrgVfsSource *source)
{
    return TEST_MEM_SOURCE (source)->name;
}

static gboolean
mem_has_file (LrgVfsSource *source,
              const gchar  *path)
{
    TestMemSource *self = TEST_MEM_SOURCE (source);
    g_autofree gchar *key = mem_key (self, path);

    mem_seen (self, path);
    return g_hash_table_contains (self->files, key);
}

static GBytes *
mem_read_file (LrgVfsSource  *source,
               const gchar   *path,
               GError       **error)
{
    TestMemSource *self = TEST_MEM_SOURCE (source);
    g_autofree gchar *key = mem_key (self, path);
    gpointer bytes;

    mem_seen (self, path);
    if (!g_hash_table_lookup_extended (self->files, key, NULL, &bytes))
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_FOUND, "no %s", path);
        return NULL;
    }
    if (bytes == NULL)
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_IO, "%s is broken", path);
        return NULL;
    }
    return g_bytes_ref (bytes);
}

static GPtrArray *
mem_list_files (LrgVfsSource  *source,
                const gchar   *prefix,
                GError       **error)
{
    TestMemSource *self = TEST_MEM_SOURCE (source);
    GPtrArray *out;
    GHashTableIter iter;
    gpointer value;

    if (self->fail_list)
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_IO, "listing failed");
        return NULL;
    }

    /* returns everything regardless of @prefix: the VFS filters */
    out = g_ptr_array_new_with_free_func (g_free);
    g_hash_table_iter_init (&iter, self->spellings);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        g_ptr_array_add (out, g_strdup (value));
    return out;
}

static gboolean
mem_get_file_size (LrgVfsSource  *source,
                   const gchar   *path,
                   guint64       *out_size,
                   GError       **error)
{
    g_autoptr(GBytes) bytes = mem_read_file (source, path, error);

    if (bytes == NULL)
        return FALSE;
    *out_size = g_bytes_get_size (bytes);
    return TRUE;
}

static void
test_mem_source_iface_init (LrgVfsSourceInterface *iface)
{
    iface->get_name = mem_get_name;
    iface->has_file = mem_has_file;
    iface->read_file = mem_read_file;
    iface->list_files = mem_list_files;
    iface->get_file_size = mem_get_file_size;
}

static void
test_mem_source_finalize (GObject *object)
{
    TestMemSource *self = TEST_MEM_SOURCE (object);

    g_free (self->name);
    g_free (self->last_path);
    g_hash_table_unref (self->files);
    g_hash_table_unref (self->spellings);
    G_OBJECT_CLASS (test_mem_source_parent_class)->finalize (object);
}

static void
test_mem_source_class_init (TestMemSourceClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = test_mem_source_finalize;
}

static void
test_mem_source_init (TestMemSource *self)
{
    self->files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                         (GDestroyNotify) g_bytes_unref);
    self->spellings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static TestMemSource *
mem_new (const gchar *name,
         gboolean     case_insensitive)
{
    TestMemSource *self = g_object_new (TEST_TYPE_MEM_SOURCE, NULL);

    self->name = g_strdup (name);
    self->case_insensitive = case_insensitive;
    return self;
}

/* adds @path holding @text; NULL @text makes a file that fails to read */
static void
mem_add (TestMemSource *self,
         const gchar   *path,
         const gchar   *text)
{
    GBytes *bytes = text != NULL ? g_bytes_new (text, strlen (text)) : NULL;

    g_hash_table_replace (self->files, mem_key (self, path), bytes);
    g_hash_table_replace (self->spellings, mem_key (self, path), g_strdup (path));
}

/* ------------------------------------------------------------------ */
/* TestBareSource: only the required vfuncs                           */
/* ------------------------------------------------------------------ */

#define TEST_TYPE_BARE_SOURCE (test_bare_source_get_type ())
G_DECLARE_FINAL_TYPE (TestBareSource, test_bare_source, TEST, BARE_SOURCE, GObject)

struct _TestBareSource
{
    GObject parent_instance;
};

static void test_bare_source_iface_init (LrgVfsSourceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestBareSource, test_bare_source, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_VFS_SOURCE, test_bare_source_iface_init))

static gboolean
bare_has_file (LrgVfsSource *source,
               const gchar  *path)
{
    return strcmp (path, "bare.txt") == 0;
}

static GBytes *
bare_read_file (LrgVfsSource  *source,
                const gchar   *path,
                GError       **error)
{
    if (strcmp (path, "bare.txt") != 0)
    {
        g_set_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_FOUND, "no %s", path);
        return NULL;
    }
    return g_bytes_new_static ("12345", 5);
}

static void
test_bare_source_iface_init (LrgVfsSourceInterface *iface)
{
    iface->has_file = bare_has_file;
    iface->read_file = bare_read_file;
}

static void
test_bare_source_class_init (TestBareSourceClass *klass)
{
}

static void
test_bare_source_init (TestBareSource *self)
{
}

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

/* asserts that @path reads as @expected */
static void
assert_reads (LrgVfs      *vfs,
              const gchar *path,
              const gchar *expected)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes = NULL;
    gsize size;
    const gchar *data;

    bytes = lrg_vfs_read_file (vfs, path, &error);
    g_assert_no_error (error);
    g_assert_nonnull (bytes);
    data = g_bytes_get_data (bytes, &size);
    g_assert_cmpmem (data, size, expected, strlen (expected));
}

/* joins a listing with ',' for compact comparisons */
static gchar *
join_list (GPtrArray *files)
{
    g_autoptr(GString) out = g_string_new (NULL);
    guint i;

    for (i = 0; i < files->len; i++)
    {
        if (i > 0)
            g_string_append_c (out, ',');
        g_string_append (out, g_ptr_array_index (files, i));
    }
    return g_string_free (g_steal_pointer (&out), FALSE);
}

static gchar *
list_joined (LrgVfs      *vfs,
             const gchar *prefix)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) files = NULL;

    files = lrg_vfs_list_files (vfs, prefix, &error);
    g_assert_no_error (error);
    g_assert_nonnull (files);
    return join_list (files);
}

typedef struct
{
    guint added;
    guint removed;
    guint last_id;
    LrgVfsSource *last_source;
} SignalLog;

static void
on_added (LrgVfs       *vfs,
          LrgVfsSource *source,
          guint         id,
          gpointer      data)
{
    SignalLog *log = data;

    log->added++;
    log->last_id = id;
    log->last_source = source;
}

static void
on_removed (LrgVfs       *vfs,
            LrgVfsSource *source,
            guint         id,
            gpointer      data)
{
    SignalLog *log = data;

    log->removed++;
    log->last_id = id;
    log->last_source = source;
}

/* ------------------------------------------------------------------ */
/* tests                                                              */
/* ------------------------------------------------------------------ */

static void
test_normalize (void)
{
    static const struct { const gchar *in; const gchar *out; } ok[] = {
        { "a/b/c", "a/b/c" },
        { "/a/b", "a/b" },
        { "\\World\\Maps\\x.adt", "World/Maps/x.adt" },
        { "a//b///c/", "a/b/c" },
        { "./a/./b", "a/b" },
        { "Mixed\\/Case", "Mixed/Case" },
        { "", "" },
        { "///", "" },
        { "a/..b/c..", "a/..b/c.." },
    };
    static const gchar *bad[] = { "..", "../a", "a/../b", "a\\..\\b", "a/.." };
    g_autoptr(GError) error = NULL;
    gchar *norm;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (ok); i++)
    {
        norm = lrg_vfs_normalize_path (ok[i].in, -1, &error);
        g_assert_no_error (error);
        g_assert_cmpstr (norm, ==, ok[i].out);
        g_free (norm);
    }
    for (i = 0; i < G_N_ELEMENTS (bad); i++)
    {
        g_assert_null (lrg_vfs_normalize_path (bad[i], -1, &error));
        g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_INVALID_PATH);
        g_clear_error (&error);
    }

    /* an explicit length may hold a NUL, which is rejected */
    g_assert_null (lrg_vfs_normalize_path ("ab\0cd", 5, &error));
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_INVALID_PATH);
    g_clear_error (&error);
    norm = lrg_vfs_normalize_path ("abcdef", 3, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (norm, ==, "abc");
    g_free (norm);
}

/* paths reach sources normalized; bad paths never reach them */
static void
test_invalid_paths (void)
{
    g_autoptr(LrgVfs) vfs = lrg_vfs_new (FALSE);
    g_autoptr(TestMemSource) mem = mem_new ("mem", FALSE);
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes = NULL;

    mem_add (mem, "dir/file.txt", "x");
    lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (mem), 0, NULL);

    assert_reads (vfs, "\\dir\\\\file.txt", "x");
    g_assert_cmpstr (mem->last_path, ==, "dir/file.txt");

    g_clear_pointer (&mem->last_path, g_free);
    bytes = lrg_vfs_read_file (vfs, "dir/../dir/file.txt", &error);
    g_assert_null (bytes);
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_INVALID_PATH);
    g_assert_null (mem->last_path);
    g_clear_error (&error);

    g_assert_false (lrg_vfs_has_file (vfs, "../file.txt"));
    g_assert_false (lrg_vfs_has_file (vfs, ""));
    g_assert_null (lrg_vfs_read_file (vfs, "/", &error));
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_INVALID_PATH);
    g_clear_error (&error);
    g_assert_null (lrg_vfs_find_source (vfs, "a/..", NULL, &error));
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_INVALID_PATH);
    g_clear_error (&error);
    g_assert_null (lrg_vfs_list_files (vfs, "../x", &error));
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_INVALID_PATH);
    g_clear_error (&error);

    g_assert_null (lrg_vfs_read_file (vfs, "dir/missing.txt", &error));
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_FOUND);
}

static void
test_priority (void)
{
    g_autoptr(LrgVfs) vfs = lrg_vfs_new (FALSE);
    g_autoptr(TestMemSource) base = mem_new ("base", FALSE);
    g_autoptr(TestMemSource) patch = mem_new ("patch", FALSE);
    g_autoptr(TestMemSource) low = mem_new ("low", FALSE);
    g_autoptr(LrgVfsSource) winner = NULL;
    g_autoptr(GArray) ids = NULL;
    g_autoptr(GError) error = NULL;
    guint base_id, patch_id, low_id, id;

    mem_add (base, "a.txt", "base");
    mem_add (base, "only-base.txt", "base");
    mem_add (patch, "a.txt", "patch");
    mem_add (low, "a.txt", "low");

    /* added out of priority order on purpose */
    base_id = lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (base), 0, NULL);
    patch_id = lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (patch), 10, NULL);
    low_id = lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (low), -5, NULL);
    g_assert_cmpuint (base_id, !=, 0);
    g_assert_cmpuint (patch_id, !=, base_id);
    g_assert_cmpuint (lrg_vfs_get_n_sources (vfs), ==, 3);

    ids = lrg_vfs_get_mount_ids (vfs);
    g_assert_cmpuint (ids->len, ==, 3);
    g_assert_cmpuint (g_array_index (ids, guint, 0), ==, patch_id);
    g_assert_cmpuint (g_array_index (ids, guint, 1), ==, base_id);
    g_assert_cmpuint (g_array_index (ids, guint, 2), ==, low_id);

    assert_reads (vfs, "a.txt", "patch");
    assert_reads (vfs, "only-base.txt", "base");

    winner = lrg_vfs_find_source (vfs, "a.txt", &id, &error);
    g_assert_no_error (error);
    g_assert_true (winner == LRG_VFS_SOURCE (patch));
    g_assert_cmpuint (id, ==, patch_id);
    g_clear_object (&winner);

    g_assert_true (lrg_vfs_remove_source (vfs, patch_id));
    assert_reads (vfs, "a.txt", "base");
    g_assert_true (lrg_vfs_remove_source (vfs, base_id));
    assert_reads (vfs, "a.txt", "low");

    winner = lrg_vfs_get_source (vfs, low_id);
    g_assert_true (winner == LRG_VFS_SOURCE (low));
    g_assert_null (lrg_vfs_get_source (vfs, patch_id));
}

/* equal priorities: the most recently added mount wins */
static void
test_tie_order (void)
{
    g_autoptr(LrgVfs) vfs = lrg_vfs_new (FALSE);
    g_autoptr(TestMemSource) first = mem_new ("first", FALSE);
    g_autoptr(TestMemSource) second = mem_new ("second", FALSE);
    g_autoptr(GArray) ids = NULL;
    guint first_id, second_id, again_id;

    mem_add (first, "f", "first");
    mem_add (second, "f", "second");

    first_id = lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (first), 3, NULL);
    second_id = lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (second), 3, NULL);
    assert_reads (vfs, "f", "second");

    /* the same source mounted again on top wins again */
    again_id = lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (first), 3, NULL);
    assert_reads (vfs, "f", "first");

    ids = lrg_vfs_get_mount_ids (vfs);
    g_assert_cmpuint (ids->len, ==, 3);
    g_assert_cmpuint (g_array_index (ids, guint, 0), ==, again_id);
    g_assert_cmpuint (g_array_index (ids, guint, 1), ==, second_id);
    g_assert_cmpuint (g_array_index (ids, guint, 2), ==, first_id);

    g_assert_true (lrg_vfs_remove_source (vfs, again_id));
    assert_reads (vfs, "f", "second");
}

static void
test_remove_and_signals (void)
{
    g_autoptr(LrgVfs) vfs = lrg_vfs_new (FALSE);
    g_autoptr(TestMemSource) base = mem_new ("base", FALSE);
    g_autoptr(TestMemSource) mod_a = mem_new ("mod-a", FALSE);
    g_autoptr(TestMemSource) mod_b = mem_new ("mod-b", FALSE);
    SignalLog log = { 0 };
    guint base_id, a_id;

    g_signal_connect (vfs, "source-added", G_CALLBACK (on_added), &log);
    g_signal_connect (vfs, "source-removed", G_CALLBACK (on_removed), &log);

    mem_add (base, "x", "base");
    mem_add (mod_a, "x", "mod");
    mem_add (mod_b, "y", "mod");

    base_id = lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (base), 0, "game");
    g_assert_cmpuint (log.added, ==, 1);
    g_assert_cmpuint (log.last_id, ==, base_id);
    g_assert_true (log.last_source == LRG_VFS_SOURCE (base));

    a_id = lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (mod_a), 100, "mod:example");
    lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (mod_b), 100, "mod:example");
    g_assert_cmpuint (log.added, ==, 3);
    assert_reads (vfs, "x", "mod");
    g_assert_true (lrg_vfs_has_file (vfs, "y"));

    /* unknown ids and tags remove nothing and say nothing */
    g_assert_false (lrg_vfs_remove_source (vfs, 0));
    g_assert_false (lrg_vfs_remove_source (vfs, 9999));
    g_assert_cmpuint (lrg_vfs_remove_owner (vfs, "mod:other"), ==, 0);
    g_assert_cmpuint (log.removed, ==, 0);

    /* the whole mod goes at once */
    g_assert_cmpuint (lrg_vfs_remove_owner (vfs, "mod:example"), ==, 2);
    g_assert_cmpuint (log.removed, ==, 2);
    g_assert_cmpuint (lrg_vfs_get_n_sources (vfs), ==, 1);
    assert_reads (vfs, "x", "base");
    g_assert_false (lrg_vfs_has_file (vfs, "y"));
    g_assert_false (lrg_vfs_remove_source (vfs, a_id));

    g_assert_true (lrg_vfs_remove_source (vfs, base_id));
    g_assert_cmpuint (log.removed, ==, 3);
    g_assert_cmpuint (log.last_id, ==, base_id);
    g_assert_true (log.last_source == LRG_VFS_SOURCE (base));
    g_assert_cmpuint (lrg_vfs_get_n_sources (vfs), ==, 0);

    /* ids are not reused */
    g_assert_cmpuint (lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (base), 0, NULL), >, a_id);
}

static void
test_case_sensitivity (void)
{
    g_autoptr(LrgVfs) ci = lrg_vfs_new (TRUE);
    g_autoptr(LrgVfs) cs = lrg_vfs_new (FALSE);
    g_autoptr(TestMemSource) upper = mem_new ("upper", TRUE);
    g_autoptr(TestMemSource) lower = mem_new ("lower", TRUE);
    g_autofree gchar *listing = NULL;
    gboolean prop = FALSE;

    g_assert_true (lrg_vfs_get_case_insensitive (ci));
    g_object_get (cs, "case-insensitive", &prop, NULL);
    g_assert_false (prop);

    mem_add (upper, "Data/World/File.TXT", "upper");
    mem_add (lower, "data/world/file.txt", "lower");
    mem_add (lower, "data/world/other.txt", "lower");

    lrg_vfs_add_source (ci, LRG_VFS_SOURCE (upper), 10, NULL);
    lrg_vfs_add_source (ci, LRG_VFS_SOURCE (lower), 0, NULL);
    lrg_vfs_add_source (cs, LRG_VFS_SOURCE (upper), 10, NULL);
    lrg_vfs_add_source (cs, LRG_VFS_SOURCE (lower), 0, NULL);

    /* lookups go through each source's own matching */
    assert_reads (ci, "DATA\\world\\FILE.txt", "upper");

    /* the case-insensitive VFS merges the two spellings, keeping the
     * winner's, and matches the prefix in any case */
    listing = list_joined (ci, "DATA/WORLD");
    g_assert_cmpstr (listing, ==, "Data/World/File.TXT,data/world/other.txt");
    g_clear_pointer (&listing, g_free);

    /* the case-sensitive one keeps both spellings and an exact prefix */
    listing = list_joined (cs, NULL);
    g_assert_cmpstr (listing, ==, "Data/World/File.TXT,data/world/file.txt,data/world/other.txt");
    g_clear_pointer (&listing, g_free);
    listing = list_joined (cs, "data");
    g_assert_cmpstr (listing, ==, "data/world/file.txt,data/world/other.txt");
}

static void
test_list_merge (void)
{
    g_autoptr(LrgVfs) vfs = lrg_vfs_new (FALSE);
    g_autoptr(TestMemSource) a = mem_new ("a", FALSE);
    g_autoptr(TestMemSource) b = mem_new ("b", FALSE);
    g_autoptr(TestMemSource) broken = mem_new ("broken", FALSE);
    g_autoptr(TestBareSource) bare = g_object_new (TEST_TYPE_BARE_SOURCE, NULL);
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) files = NULL;
    g_autofree gchar *listing = NULL;
    guint broken_id;

    mem_add (a, "World/b.txt", "a");
    mem_add (a, "World/a.txt", "a");
    mem_add (a, "WorldX/no.txt", "a");
    mem_add (a, "World", "a file named like the prefix");
    mem_add (b, "/World//a.txt", "b");          /* sloppy spelling, normalized */
    mem_add (b, "World/sub/c.txt", "b");
    mem_add (b, "zzz.txt", "b");
    mem_add (b, "bad/../escape", "b");          /* unreachable: dropped */

    lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (a), 0, NULL);
    lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (b), 5, NULL);
    lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (bare), 1, NULL);   /* cannot list: skipped */

    listing = list_joined (vfs, "World");
    g_assert_cmpstr (listing, ==, "World/a.txt,World/b.txt,World/sub/c.txt");
    g_clear_pointer (&listing, g_free);

    listing = list_joined (vfs, "\\World\\sub\\");
    g_assert_cmpstr (listing, ==, "World/sub/c.txt");
    g_clear_pointer (&listing, g_free);

    listing = list_joined (vfs, "");
    g_assert_cmpstr (listing, ==,
                     "World,World/a.txt,World/b.txt,World/sub/c.txt,WorldX/no.txt,zzz.txt");
    g_clear_pointer (&listing, g_free);

    listing = list_joined (vfs, "nothing/here");
    g_assert_cmpstr (listing, ==, "");
    g_clear_pointer (&listing, g_free);

    /* a source whose listing fails fails the whole listing */
    broken->fail_list = TRUE;
    broken_id = lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (broken), 0, NULL);
    files = lrg_vfs_list_files (vfs, NULL, &error);
    g_assert_null (files);
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_IO);
    g_assert_nonnull (strstr (error->message, "broken"));
    g_clear_error (&error);
    lrg_vfs_remove_source (vfs, broken_id);
}

/* a source without the optional vfuncs */
static void
test_bare_source (void)
{
    g_autoptr(TestBareSource) bare = g_object_new (TEST_TYPE_BARE_SOURCE, NULL);
    LrgVfsSource *source = LRG_VFS_SOURCE (bare);
    g_autoptr(LrgVfs) vfs = lrg_vfs_new (FALSE);
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) files = NULL;
    guint64 size = 0;

    g_assert_cmpstr (lrg_vfs_source_get_name (source), ==, "TestBareSource");

    files = lrg_vfs_source_list_files (source, NULL, &error);
    g_assert_null (files);
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_SUPPORTED);
    g_clear_error (&error);

    /* sizes fall back to reading the file */
    g_assert_true (lrg_vfs_source_get_file_size (source, "bare.txt", &size, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (size, ==, 5);
    g_assert_false (lrg_vfs_source_get_file_size (source, "nope", &size, &error));
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_FOUND);
    g_clear_error (&error);

    /* through a VFS: readable and sized, listed as nothing */
    lrg_vfs_add_source (vfs, source, 0, NULL);
    size = 0;
    g_assert_true (lrg_vfs_get_file_size (vfs, "/bare.txt", &size, &error));
    g_assert_cmpuint (size, ==, 5);
    g_clear_pointer (&files, g_ptr_array_unref);
    files = lrg_vfs_list_files (vfs, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (files->len, ==, 0);
}

/* the winner's read error is returned; lower sources are not tried */
static void
test_read_errors (void)
{
    g_autoptr(LrgVfs) vfs = lrg_vfs_new (FALSE);
    g_autoptr(TestMemSource) top = mem_new ("top", FALSE);
    g_autoptr(TestMemSource) bottom = mem_new ("bottom", FALSE);
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes = NULL;
    guint64 size;

    mem_add (top, "file.bin", NULL);
    mem_add (bottom, "file.bin", "fine");
    mem_add (bottom, "sized.bin", "123");
    lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (bottom), 0, NULL);
    lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (top), 1, NULL);

    bytes = lrg_vfs_read_file (vfs, "file.bin", &error);
    g_assert_null (bytes);
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_IO);
    g_assert_nonnull (strstr (error->message, "top"));
    g_clear_error (&error);

    g_assert_false (lrg_vfs_get_file_size (vfs, "file.bin", &size, &error));
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_IO);
    g_clear_error (&error);

    g_assert_true (lrg_vfs_get_file_size (vfs, "sized.bin", &size, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (size, ==, 3);
}

/* writes @text to @root/@relative, creating the directories */
static void
write_file (const gchar *root,
            const gchar *relative,
            const gchar *text)
{
    g_autofree gchar *full = g_build_filename (root, relative, NULL);
    g_autofree gchar *dir = g_path_get_dirname (full);
    g_autoptr(GError) error = NULL;

    g_assert_cmpint (g_mkdir_with_parents (dir, 0755), ==, 0);
    g_file_set_contents (full, text, -1, &error);
    g_assert_no_error (error);
}

static void
test_dir_source (void)
{
    g_autofree gchar *base = NULL;
    g_autofree gchar *root = NULL;
    g_autofree gchar *outside = NULL;
    g_autofree gchar *link_out = NULL;
    g_autofree gchar *link_in = NULL;
    g_autofree gchar *resolved = NULL;
    g_autofree gchar *listing = NULL;
    g_autoptr(LrgVfsDirSource) ci_dir = NULL;
    g_autoptr(LrgVfsDirSource) cs_dir = NULL;
    g_autoptr(LrgVfs) vfs = NULL;
    g_autoptr(GPtrArray) files = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) bytes = NULL;
    guint64 size = 0;

    /* GTest isolates (and removes) the user directories */
    base = g_build_filename (g_get_user_cache_dir (), "vfs", NULL);
    root = g_build_filename (base, "root", NULL);
    write_file (root, "World/Maps/Azeroth/tile_32_48.adt", "adt");
    write_file (root, "Interface/Icons/Inv_Sword.blp", "blp");
    write_file (root, "top.txt", "top");
    write_file (root, "Dup/Foo", "Foo");
    write_file (root, "Dup/FOO", "FOO");
    write_file (base, "secret.txt", "secret");

    /* a link escaping the root, and one staying inside */
    outside = g_build_filename (base, "secret.txt", NULL);
    link_out = g_build_filename (root, "escape.txt", NULL);
    g_assert_cmpint (symlink (outside, link_out), ==, 0);
    link_in = g_build_filename (root, "alias.txt", NULL);
    g_assert_cmpint (symlink ("top.txt", link_in), ==, 0);

    ci_dir = lrg_vfs_dir_source_new (root, TRUE);
    cs_dir = lrg_vfs_dir_source_new (root, FALSE);
    g_assert_cmpstr (lrg_vfs_dir_source_get_root (ci_dir), ==, root);
    g_assert_true (lrg_vfs_dir_source_get_case_insensitive (ci_dir));
    g_assert_cmpstr (lrg_vfs_source_get_name (LRG_VFS_SOURCE (ci_dir)), ==, root);

    /* exact and folded lookups */
    g_assert_true (lrg_vfs_source_has_file (LRG_VFS_SOURCE (cs_dir), "World/Maps/Azeroth/tile_32_48.adt"));
    g_assert_false (lrg_vfs_source_has_file (LRG_VFS_SOURCE (cs_dir), "world/maps/azeroth/TILE_32_48.ADT"));
    g_assert_true (lrg_vfs_source_has_file (LRG_VFS_SOURCE (ci_dir), "world/maps/azeroth/TILE_32_48.ADT"));
    resolved = lrg_vfs_dir_source_resolve (ci_dir, "INTERFACE\\icons\\inv_sword.BLP");
    g_assert_nonnull (resolved);
    g_assert_true (g_str_has_suffix (resolved, "Interface/Icons/Inv_Sword.blp"));
    g_clear_pointer (&resolved, g_free);

    /* directories are not files */
    g_assert_false (lrg_vfs_source_has_file (LRG_VFS_SOURCE (ci_dir), "world/maps"));

    /* exact match first; otherwise the bytewise-smallest folded match */
    resolved = lrg_vfs_dir_source_resolve (ci_dir, "Dup/FOO");
    g_assert_true (g_str_has_suffix (resolved, "Dup/FOO"));
    g_clear_pointer (&resolved, g_free);
    resolved = lrg_vfs_dir_source_resolve (ci_dir, "dup/foo");
    g_assert_true (g_str_has_suffix (resolved, "Dup/FOO"));
    g_clear_pointer (&resolved, g_free);

    /* never outside the root */
    g_assert_null (lrg_vfs_dir_source_resolve (ci_dir, "../secret.txt"));
    g_assert_null (lrg_vfs_dir_source_resolve (ci_dir, "escape.txt"));
    g_assert_false (lrg_vfs_source_has_file (LRG_VFS_SOURCE (ci_dir), "escape.txt"));
    g_assert_true (lrg_vfs_source_has_file (LRG_VFS_SOURCE (ci_dir), "alias.txt"));

    /* reads and sizes */
    bytes = lrg_vfs_source_read_file (LRG_VFS_SOURCE (ci_dir), "TOP.TXT", &error);
    g_assert_no_error (error);
    g_assert_cmpmem (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes), "top", 3);
    g_assert_true (lrg_vfs_source_get_file_size (LRG_VFS_SOURCE (ci_dir), "world/maps/azeroth/tile_32_48.adt",
                                                 &size, &error));
    g_assert_cmpuint (size, ==, 3);
    g_assert_null (lrg_vfs_source_read_file (LRG_VFS_SOURCE (cs_dir), "TOP.TXT", &error));
    g_assert_error (error, LRG_VFS_ERROR, LRG_VFS_ERROR_NOT_FOUND);
    g_clear_error (&error);

    /* listing walks recursively in on-disk spelling, without the escape */
    {
        g_autoptr(LrgVfs) cs_vfs = lrg_vfs_new (FALSE);

        lrg_vfs_add_source (cs_vfs, LRG_VFS_SOURCE (cs_dir), 0, NULL);
        listing = list_joined (cs_vfs, NULL);
        g_assert_cmpstr (listing, ==,
                         "Dup/FOO,Dup/Foo,Interface/Icons/Inv_Sword.blp,"
                         "World/Maps/Azeroth/tile_32_48.adt,alias.txt,top.txt");
        g_clear_pointer (&listing, g_free);
    }

    /* a case-insensitive VFS folds Dup/FOO and Dup/Foo into one entry */
    vfs = lrg_vfs_new (TRUE);
    lrg_vfs_add_source (vfs, LRG_VFS_SOURCE (ci_dir), 0, "data");
    g_clear_pointer (&files, g_ptr_array_unref);
    files = lrg_vfs_list_files (vfs, "DUP", &error);
    g_assert_no_error (error);
    g_assert_cmpuint (files->len, ==, 1);
    g_clear_pointer (&files, g_ptr_array_unref);
    listing = list_joined (vfs, "world/MAPS");
    g_assert_cmpstr (listing, ==, "World/Maps/Azeroth/tile_32_48.adt");
    g_clear_pointer (&listing, g_free);

    /* the source alone: a missing prefix directory is simply empty */
    files = lrg_vfs_source_list_files (LRG_VFS_SOURCE (cs_dir), "world", &error);
    g_assert_no_error (error);
    g_assert_cmpuint (files->len, ==, 0);
    g_clear_pointer (&files, g_ptr_array_unref);
    files = lrg_vfs_source_list_files (LRG_VFS_SOURCE (ci_dir), "world", &error);
    g_assert_no_error (error);
    g_assert_cmpuint (files->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (files, 0), ==, "World/Maps/Azeroth/tile_32_48.adt");

    /* through the VFS */
    assert_reads (vfs, "/world/maps/azeroth/tile_32_48.ADT", "adt");
    g_assert_false (lrg_vfs_has_file (vfs, "escape.txt"));

    /* a root that does not exist has no files */
    {
        g_autoptr(LrgVfsDirSource) none = NULL;
        g_autofree gchar *missing = g_build_filename (base, "missing", NULL);

        none = lrg_vfs_dir_source_new (missing, TRUE);
        g_assert_false (lrg_vfs_source_has_file (LRG_VFS_SOURCE (none), "top.txt"));
        g_clear_pointer (&files, g_ptr_array_unref);
        files = lrg_vfs_source_list_files (LRG_VFS_SOURCE (none), NULL, &error);
        g_assert_no_error (error);
        g_assert_cmpuint (files->len, ==, 0);
    }
}

static void
test_types (void)
{
    g_autofree gchar *name = NULL;

    g_assert_true (G_TYPE_IS_INTERFACE (LRG_TYPE_VFS_SOURCE));
    g_assert_true (g_type_is_a (LRG_TYPE_VFS_DIR_SOURCE, LRG_TYPE_VFS_SOURCE));
    name = g_enum_to_string (LRG_TYPE_VFS_ERROR, LRG_VFS_ERROR_NOT_SUPPORTED);
    g_assert_cmpstr (name, ==, "LRG_VFS_ERROR_NOT_SUPPORTED");
    g_assert_cmpuint (g_signal_lookup ("source-added", LRG_TYPE_VFS), !=, 0);
    g_assert_cmpuint (g_signal_lookup ("source-removed", LRG_TYPE_VFS), !=, 0);
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

    g_test_add_func ("/vfs/normalize", test_normalize);
    g_test_add_func ("/vfs/invalid-paths", test_invalid_paths);
    g_test_add_func ("/vfs/priority", test_priority);
    g_test_add_func ("/vfs/tie-order", test_tie_order);
    g_test_add_func ("/vfs/remove-and-signals", test_remove_and_signals);
    g_test_add_func ("/vfs/case-sensitivity", test_case_sensitivity);
    g_test_add_func ("/vfs/list-merge", test_list_merge);
    g_test_add_func ("/vfs/bare-source", test_bare_source);
    g_test_add_func ("/vfs/read-errors", test_read_errors);
    g_test_add_func ("/vfs/dir-source", test_dir_source);
    g_test_add_func ("/vfs/types", test_types);

    return g_test_run ();
}
