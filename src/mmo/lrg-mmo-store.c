/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-store.h"
#include <sqlite3.h>

struct _LrgMmoStore
{
    GObject parent_instance;
    sqlite3 *db;
};

G_DEFINE_TYPE (LrgMmoStore, lrg_mmo_store, G_TYPE_OBJECT)

static gboolean
sql_error (LrgMmoStore *self,
           GError     **error)
{
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Record store: %s",
                 sqlite3_errmsg (self->db));
    return FALSE;
}

static gboolean
valid_key (const gchar *key)
{
    return key != NULL && *key != '\0' && strlen (key) <= 256 && g_utf8_validate (key, -1, NULL);
}

static void
lrg_mmo_store_finalize (GObject *object)
{
    LrgMmoStore *self = LRG_MMO_STORE (object);
    if (self->db != NULL)
        sqlite3_close (self->db);
    G_OBJECT_CLASS (lrg_mmo_store_parent_class)->finalize (object);
}

static void
lrg_mmo_store_class_init (LrgMmoStoreClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_store_finalize;
}

static void
lrg_mmo_store_init (LrgMmoStore *self)
{
}

LrgMmoStore *
lrg_mmo_store_new (const gchar  *path,
                   GError      **error)
{
    g_autoptr(LrgMmoStore) self = NULL;
    g_return_val_if_fail (path != NULL, NULL);
    self = g_object_new (LRG_TYPE_MMO_STORE, NULL);
    if (sqlite3_open_v2 (path, &self->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                        SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK)
    {
        sql_error (self, error);
        return NULL;
    }
    sqlite3_busy_timeout (self->db, 1000);
    if (sqlite3_exec (self->db,
                     "PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;"
                     "CREATE TABLE IF NOT EXISTS lrg_records ("
                     "key TEXT PRIMARY KEY NOT NULL, revision INTEGER NOT NULL CHECK(revision > 0),"
                     "data BLOB NOT NULL);", NULL, NULL, NULL) != SQLITE_OK)
    {
        sql_error (self, error);
        return NULL;
    }
    return g_steal_pointer (&self);
}

GBytes *
lrg_mmo_store_read (LrgMmoStore  *self,
                    const gchar  *key,
                    guint64      *revision,
                    GError      **error)
{
    sqlite3_stmt *statement = NULL;
    GBytes *bytes = NULL;
    gint result;
    g_return_val_if_fail (LRG_IS_MMO_STORE (self), NULL);
    g_return_val_if_fail (revision != NULL, NULL);
    *revision = 0;
    if (!valid_key (key))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid record key");
        return NULL;
    }
    if (sqlite3_prepare_v2 (self->db, "SELECT revision, data FROM lrg_records WHERE key=?1",
                           -1, &statement, NULL) != SQLITE_OK)
    {
        sql_error (self, error);
        return NULL;
    }
    sqlite3_bind_text (statement, 1, key, -1, SQLITE_TRANSIENT);
    result = sqlite3_step (statement);
    if (result == SQLITE_ROW)
    {
        gint64 stored_revision = sqlite3_column_int64 (statement, 0);
        gint size = sqlite3_column_bytes (statement, 1);
        if (stored_revision <= 0 || size > 1024 * 1024)
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "Invalid stored revision or record size");
        else
        {
            *revision = stored_revision;
            bytes = g_bytes_new (sqlite3_column_blob (statement, 1), size);
        }
    }
    else if (result == SQLITE_DONE)
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Record not found");
    else
        sql_error (self, error);
    sqlite3_finalize (statement);
    return bytes;
}

gboolean
lrg_mmo_store_commit (LrgMmoStore  *self,
                      GVariant     *changes,
                      GError      **error)
{
    GVariantIter iter;
    const gchar *key;
    guint64 expected;
    GVariant *value;
    g_autoptr(GHashTable) keys = NULL;
    sqlite3_stmt *insert = NULL;
    sqlite3_stmt *update = NULL;
    gboolean success = FALSE;

    g_return_val_if_fail (LRG_IS_MMO_STORE (self), FALSE);
    if (changes == NULL || !g_variant_is_of_type (changes, G_VARIANT_TYPE ("a(stay)")) ||
        !g_variant_is_normal_form (changes) ||
        g_variant_n_children (changes) == 0 || g_variant_n_children (changes) > 256)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Expected 1 to 256 record changes");
        return FALSE;
    }
    /* Validate the whole batch before obtaining a write lock. */
    keys = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_variant_iter_init (&iter, changes);
    while (g_variant_iter_next (&iter, "(&st@ay)", &key, &expected, &value))
    {
        gboolean valid = valid_key (key) && expected < G_MAXINT64 &&
                         g_variant_get_size (value) <= 1024 * 1024 &&
                         !g_hash_table_contains (keys, key);
        g_variant_unref (value);
        if (!valid)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Invalid key, revision, payload size or duplicate key");
            return FALSE;
        }
        g_hash_table_add (keys, g_strdup (key));
    }
    if (sqlite3_exec (self->db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK)
        return sql_error (self, error);
    if (sqlite3_prepare_v2 (self->db, "INSERT INTO lrg_records VALUES (?1,1,?2)", -1,
                           &insert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2 (self->db, "UPDATE lrg_records SET data=?2,revision=revision+1 WHERE key=?1 AND revision=?3",
                           -1, &update, NULL) != SQLITE_OK)
    {
        sql_error (self, error);
        goto out;
    }
    g_variant_iter_init (&iter, changes);
    while (g_variant_iter_next (&iter, "(&st@ay)", &key, &expected, &value))
    {
        sqlite3_stmt *statement = expected == 0 ? insert : update;
        gsize size;
        gconstpointer data = g_variant_get_fixed_array (value, &size, 1);
        gint result;

        sqlite3_reset (statement);
        sqlite3_bind_text (statement, 1, key, -1, SQLITE_TRANSIENT);
        /* SQLite interprets a NULL blob pointer as SQL NULL even at length zero. */
        sqlite3_bind_blob (statement, 2, size > 0 ? data : "", size, SQLITE_TRANSIENT);
        if (expected != 0)
            sqlite3_bind_int64 (statement, 3, expected);
        result = sqlite3_step (statement);
        g_variant_unref (value);
        if (result == SQLITE_CONSTRAINT || (result == SQLITE_DONE && sqlite3_changes (self->db) != 1))
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG, "Revision conflict for %s", key);
            goto out;
        }
        if (result != SQLITE_DONE)
        {
            sql_error (self, error);
            goto out;
        }
    }
    if (sqlite3_exec (self->db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK)
        sql_error (self, error);
    else
        success = TRUE;
out:
    sqlite3_finalize (insert);
    sqlite3_finalize (update);
    if (!success)
        sqlite3_exec (self->db, "ROLLBACK", NULL, NULL, NULL);
    return success;
}
