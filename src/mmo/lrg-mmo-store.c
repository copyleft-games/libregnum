/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-store.h"
#include <sqlite3.h>
#include "lrg-mmo-store-private.h"

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
                     "data BLOB NOT NULL);"
                     "CREATE TABLE IF NOT EXISTS lrg_operations ("
                     "id TEXT PRIMARY KEY NOT NULL,digest TEXT NOT NULL);"
                     "CREATE TABLE IF NOT EXISTS lrg_audit ("
                     "sequence INTEGER PRIMARY KEY AUTOINCREMENT,operation TEXT,"
                     "digest TEXT NOT NULL,created INTEGER NOT NULL);", NULL, NULL, NULL) != SQLITE_OK)
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

static gboolean
commit_internal (LrgMmoStore  *self,
                 GVariant     *changes,
                 const gchar  *operation_id,
                 gboolean     *duplicate,
                 const gchar  *zone,
                 const gchar  *owner,
                 guint64       fence,
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
    sqlite3_stmt *receipt = NULL;
    g_autofree gchar *digest = NULL;
    g_autoptr(GVariant) canonical = NULL;

    g_return_val_if_fail (LRG_IS_MMO_STORE (self), FALSE);
    if (duplicate != NULL)
        *duplicate = FALSE;
    if (changes == NULL || !g_variant_is_of_type (changes, G_VARIANT_TYPE ("a(stay)")) ||
        !g_variant_is_normal_form (changes) ||
        g_variant_n_children (changes) == 0 || g_variant_n_children (changes) > 256)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Expected 1 to 256 record changes");
        return FALSE;
    }
    if (operation_id != NULL && !valid_key (operation_id))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid operation ID");
        return FALSE;
    }
#if G_BYTE_ORDER == G_BIG_ENDIAN
    canonical = g_variant_byteswap (changes);
#else
    canonical = g_variant_ref (changes);
#endif
    digest = g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                          g_variant_get_data (canonical), g_variant_get_size (canonical));
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
    if (operation_id != NULL)
    {
        gint result;
        if (sqlite3_prepare_v2 (self->db, "SELECT digest FROM lrg_operations WHERE id=?1",
                               -1, &receipt, NULL) != SQLITE_OK)
        {
            sql_error (self, error);
            goto out;
        }
        sqlite3_bind_text (receipt, 1, operation_id, -1, SQLITE_TRANSIENT);
        result = sqlite3_step (receipt);
        if (result == SQLITE_ROW)
        {
            if (g_strcmp0 ((const gchar *) sqlite3_column_text (receipt, 0), digest) != 0)
                g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                     "Operation ID reused with different changes");
            else
            {
                if (duplicate != NULL)
                    *duplicate = TRUE;
                success = TRUE;
            }
            sqlite3_exec (self->db, "ROLLBACK", NULL, NULL, NULL);
            goto out;
        }
        if (result != SQLITE_DONE)
        {
            sql_error (self, error);
            goto out;
        }
        sqlite3_finalize (receipt);
        receipt = NULL;
    }
    if (zone != NULL)
    {
        if (fence == 0 || fence > G_MAXINT64 || owner == NULL ||
            sqlite3_prepare_v2 (self->db, "SELECT 1 FROM lrg_leases WHERE zone=?1 AND owner=?2 AND fence=?3 "
                               "AND expires>CAST(strftime('%s','now') AS INTEGER)", -1, &receipt, NULL) != SQLITE_OK)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Invalid fencing authority");
            goto out;
        }
        sqlite3_bind_text (receipt, 1, zone, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (receipt, 2, owner, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64 (receipt, 3, fence);
        if (sqlite3_step (receipt) != SQLITE_ROW)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Stale or expired fencing token");
            goto out;
        }
        sqlite3_finalize (receipt);
        receipt = NULL;
    }
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
    if (operation_id != NULL)
    {
        if (sqlite3_prepare_v2 (self->db, "INSERT INTO lrg_operations VALUES (?1,?2)",
                               -1, &receipt, NULL) != SQLITE_OK)
        {
            sql_error (self, error);
            goto out;
        }
        sqlite3_bind_text (receipt, 1, operation_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text (receipt, 2, digest, -1, SQLITE_TRANSIENT);
        if (sqlite3_step (receipt) != SQLITE_DONE)
        {
            sql_error (self, error);
            goto out;
        }
        sqlite3_finalize (receipt);
        receipt = NULL;
    }
    if (sqlite3_prepare_v2 (self->db,
                           "INSERT INTO lrg_audit(operation,digest,created) VALUES (?1,?2,CAST(strftime('%s','now') AS INTEGER))",
                           -1, &receipt, NULL) != SQLITE_OK)
    {
        sql_error (self, error);
        goto out;
    }
    if (operation_id != NULL)
        sqlite3_bind_text (receipt, 1, operation_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (receipt, 2, digest, -1, SQLITE_TRANSIENT);
    if (sqlite3_step (receipt) != SQLITE_DONE)
    {
        sql_error (self, error);
        goto out;
    }
    if (sqlite3_exec (self->db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK)
        sql_error (self, error);
    else
        success = TRUE;
out:
    sqlite3_finalize (receipt);
    sqlite3_finalize (insert);
    sqlite3_finalize (update);
    if (!success)
        sqlite3_exec (self->db, "ROLLBACK", NULL, NULL, NULL);
    return success;
}

gboolean
lrg_mmo_store_commit (LrgMmoStore *self, GVariant *changes, GError **error)
{
    return commit_internal (self, changes, NULL, NULL, NULL, NULL, 0, error);
}

gboolean
lrg_mmo_store_commit_once (LrgMmoStore *self, const gchar *operation_id,
                           GVariant *changes, gboolean *duplicate, GError **error)
{
    g_return_val_if_fail (operation_id != NULL, FALSE);
    return commit_internal (self, changes, operation_id, duplicate, NULL, NULL, 0, error);
}

sqlite3 *
_lrg_mmo_store_database (LrgMmoStore *self)
{
    return self->db;
}

gboolean
lrg_mmo_store_backup (LrgMmoStore *self, const gchar *path, GError **error)
{
    sqlite3 *destination = NULL;
    sqlite3_backup *backup;
    gint result;
    g_autoptr(GFile) file = NULL;
    g_autoptr(GFileOutputStream) reservation = NULL;
    g_return_val_if_fail (LRG_IS_MMO_STORE (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    /* Exclusive reservation prevents overwriting a live database or old backup. */
    file = g_file_new_for_path (path);
    reservation = g_file_create (file, G_FILE_CREATE_PRIVATE, NULL, error);
    if (reservation == NULL)
        return FALSE;
    if (!g_output_stream_close (G_OUTPUT_STREAM (reservation), NULL, error))
        return FALSE;
    result = sqlite3_open_v2 (path, &destination, SQLITE_OPEN_READWRITE, NULL);
    if (result == SQLITE_OK)
    {
        backup = sqlite3_backup_init (destination, "main", self->db, "main");
        if (backup == NULL)
            result = sqlite3_errcode (destination);
        else
        {
            result = sqlite3_backup_step (backup, -1);
            if (sqlite3_backup_finish (backup) != SQLITE_OK)
                result = SQLITE_ERROR;
        }
    }
    if (result != SQLITE_DONE)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Backup failed: %s",
                     sqlite3_errmsg (destination));
    sqlite3_close (destination);
    if (result != SQLITE_DONE)
        g_file_delete (file, NULL, NULL);
    return result == SQLITE_DONE;
}

GVariant *
lrg_mmo_store_read_audit (LrgMmoStore *self, guint64 after, guint limit, GError **error)
{
    sqlite3_stmt *statement = NULL;
    GVariantBuilder builder;
    gint result;
    g_return_val_if_fail (LRG_IS_MMO_STORE (self), NULL);
    if (after > G_MAXINT64 || limit == 0 || limit > 1000)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid audit cursor or limit");
        return NULL;
    }
    if (sqlite3_prepare_v2 (self->db,
                           "SELECT sequence,coalesce(operation,''),digest,created FROM lrg_audit "
                           "WHERE sequence>?1 ORDER BY sequence LIMIT ?2", -1, &statement, NULL) != SQLITE_OK)
    {
        sql_error (self, error);
        return NULL;
    }
    sqlite3_bind_int64 (statement, 1, after);
    sqlite3_bind_int (statement, 2, limit);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(tssx)"));
    while ((result = sqlite3_step (statement)) == SQLITE_ROW)
        g_variant_builder_add (&builder, "(tssx)", (guint64) sqlite3_column_int64 (statement, 0),
                               sqlite3_column_text (statement, 1), sqlite3_column_text (statement, 2),
                               (gint64) sqlite3_column_int64 (statement, 3));
    sqlite3_finalize (statement);
    if (result != SQLITE_DONE)
    {
        g_variant_builder_clear (&builder);
        sql_error (self, error);
        return NULL;
    }
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

gboolean
lrg_mmo_store_commit_fenced (LrgMmoStore *self, const gchar *zone, const gchar *owner,
                             guint64 fence, GVariant *changes, GError **error)
{
    g_return_val_if_fail (zone != NULL && owner != NULL, FALSE);
    return commit_internal (self, changes, NULL, NULL, zone, owner, fence, error);
}
