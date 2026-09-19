/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-shard-directory.h"
#include "lrg-mmo-store-private.h"
#include "lrg-mmo-service-private.h"

struct _LrgMmoShardDirectory
{
    GObject parent_instance;
    LrgMmoStore *store;
};
G_DEFINE_TYPE (LrgMmoShardDirectory, lrg_mmo_shard_directory, G_TYPE_OBJECT)

static void
lrg_mmo_shard_directory_dispose (GObject *object)
{
    g_clear_object (&LRG_MMO_SHARD_DIRECTORY (object)->store);
    G_OBJECT_CLASS (lrg_mmo_shard_directory_parent_class)->dispose (object);
}
static void
lrg_mmo_shard_directory_class_init (LrgMmoShardDirectoryClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = lrg_mmo_shard_directory_dispose;
}
static void
lrg_mmo_shard_directory_init (LrgMmoShardDirectory *self)
{
}

LrgMmoShardDirectory *
lrg_mmo_shard_directory_new (LrgMmoStore *store, GError **error)
{
    LrgMmoShardDirectory *self;
    sqlite3 *db;
    g_return_val_if_fail (LRG_IS_MMO_STORE (store), NULL);
    db = _lrg_mmo_store_database (store);
    if (sqlite3_exec (db, "CREATE TABLE IF NOT EXISTS lrg_leases (zone TEXT PRIMARY KEY,"
                          "owner TEXT NOT NULL,endpoint TEXT NOT NULL,fence INTEGER NOT NULL,"
                          "expires INTEGER NOT NULL,state BLOB NOT NULL)", NULL, NULL, NULL) != SQLITE_OK)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_FAILED, sqlite3_errmsg (db));
        return NULL;
    }
    self = g_object_new (LRG_TYPE_MMO_SHARD_DIRECTORY, NULL);
    self->store = g_object_ref (store);
    return self;
}

static gboolean
valid_endpoint (const gchar *endpoint)
{
    return endpoint != NULL && *endpoint != '\0' && strlen (endpoint) <= 512 &&
           g_utf8_validate (endpoint, -1, NULL);
}

static guint64
mutate_lease (LrgMmoShardDirectory *self, const gchar *zone, const gchar *owner,
              guint64 fence, const gchar *destination, const gchar *endpoint,
              GBytes *state, guint ttl, guint operation, GError **error)
{
    sqlite3 *db = _lrg_mmo_store_database (self->store);
    sqlite3_stmt *statement = NULL;
    gint result;
    guint64 current = 0;
    gint64 now, expires = 0;
    gboolean found;
    g_autofree gchar *current_owner = NULL;
    g_autofree gchar *current_endpoint = NULL;
    g_autoptr(GBytes) current_state = NULL;
    guint64 next = 0;
    gsize size;
    gconstpointer data;

    if (!_lrg_mmo_id_valid (zone) || !_lrg_mmo_id_valid (owner) ||
        (operation != 2 && (ttl == 0 || ttl > 86400)) ||
        ((operation == 0 || operation == 3) && !valid_endpoint (endpoint)) ||
        (operation == 3 && (!_lrg_mmo_id_valid (destination) || state == NULL ||
                            g_bytes_get_size (state) > 1024 * 1024)))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid lease parameters");
        return 0;
    }
    if (sqlite3_exec (db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK)
        goto sql_failure;
    if (sqlite3_prepare_v2 (db, "SELECT CAST(strftime('%s','now') AS INTEGER)", -1,
                           &statement, NULL) != SQLITE_OK || sqlite3_step (statement) != SQLITE_ROW)
        goto rollback_sql;
    now = sqlite3_column_int64 (statement, 0);
    sqlite3_finalize (statement);
    statement = NULL;
    if (sqlite3_prepare_v2 (db, "SELECT owner,endpoint,fence,expires,state FROM lrg_leases WHERE zone=?1",
                           -1, &statement, NULL) != SQLITE_OK)
        goto rollback_sql;
    sqlite3_bind_text (statement, 1, zone, -1, SQLITE_TRANSIENT);
    result = sqlite3_step (statement);
    if (result != SQLITE_ROW && result != SQLITE_DONE)
        goto rollback_sql;
    found = result == SQLITE_ROW;
    if (found)
    {
        current_owner = g_strdup ((const gchar *) sqlite3_column_text (statement, 0));
        current_endpoint = g_strdup ((const gchar *) sqlite3_column_text (statement, 1));
        current = sqlite3_column_int64 (statement, 2);
        expires = sqlite3_column_int64 (statement, 3);
        current_state = g_bytes_new (sqlite3_column_blob (statement, 4), sqlite3_column_bytes (statement, 4));
    }
    sqlite3_finalize (statement);
    statement = NULL;
    if ((operation == 0 && found && expires > now) ||
        (operation != 0 && (!found || current != fence || expires <= now ||
                            g_strcmp0 (owner, current_owner) != 0)))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Zone lease is held, stale or expired");
        goto rollback;
    }
    if ((operation == 0 || operation == 3) && current >= G_MAXINT64)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Fencing token exhausted");
        goto rollback;
    }
    next = (operation == 0 || operation == 3) ? current + 1 : current;
    if (sqlite3_prepare_v2 (db, "INSERT OR REPLACE INTO lrg_leases VALUES (?1,?2,?3,?4,?5,?6)",
                           -1, &statement, NULL) != SQLITE_OK)
        goto rollback_sql;
    sqlite3_bind_text (statement, 1, zone, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 2, operation == 3 ? destination : owner, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (statement, 3, endpoint != NULL ? endpoint : current_endpoint, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64 (statement, 4, next);
    sqlite3_bind_int64 (statement, 5, operation == 2 ? 0 : now + ttl);
    if (state == NULL)
        state = current_state;
    data = state != NULL ? g_bytes_get_data (state, &size) : NULL;
    if (state == NULL)
        size = 0;
    sqlite3_bind_blob (statement, 6, size > 0 ? data : "", size, SQLITE_TRANSIENT);
    if (sqlite3_step (statement) != SQLITE_DONE)
        goto rollback_sql;
    sqlite3_finalize (statement);
    statement = NULL;
    if (sqlite3_exec (db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK)
        goto rollback_sql;
    return next;
rollback_sql:
    _lrg_mmo_fail (error, G_IO_ERROR_FAILED, sqlite3_errmsg (db));
rollback:
    sqlite3_finalize (statement);
    sqlite3_exec (db, "ROLLBACK", NULL, NULL, NULL);
    return 0;
sql_failure:
    _lrg_mmo_fail (error, G_IO_ERROR_FAILED, sqlite3_errmsg (db));
    return 0;
}

guint64
lrg_mmo_shard_directory_acquire (LrgMmoShardDirectory *self, const gchar *zone, const gchar *owner,
                                 const gchar *endpoint, guint ttl, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_SHARD_DIRECTORY (self), 0);
    return mutate_lease (self, zone, owner, 0, NULL, endpoint, NULL, ttl, 0, error);
}

gboolean
lrg_mmo_shard_directory_renew (LrgMmoShardDirectory *self, const gchar *zone, const gchar *owner,
                               guint64 fence, guint ttl, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_SHARD_DIRECTORY (self), FALSE);
    return mutate_lease (self, zone, owner, fence, NULL, NULL, NULL, ttl, 1, error) != 0;
}

gboolean
lrg_mmo_shard_directory_release (LrgMmoShardDirectory *self, const gchar *zone, const gchar *owner,
                                 guint64 fence, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_SHARD_DIRECTORY (self), FALSE);
    return mutate_lease (self, zone, owner, fence, NULL, NULL, NULL, 0, 2, error) != 0;
}

guint64
lrg_mmo_shard_directory_handoff (LrgMmoShardDirectory *self, const gchar *zone, const gchar *owner,
                                 guint64 fence, const gchar *destination, const gchar *endpoint,
                                 GBytes *state, guint ttl, GError **error)
{
    g_return_val_if_fail (LRG_IS_MMO_SHARD_DIRECTORY (self), 0);
    return mutate_lease (self, zone, owner, fence, destination, endpoint, state, ttl, 3, error);
}

GVariant *
lrg_mmo_shard_directory_lookup (LrgMmoShardDirectory *self, const gchar *zone, GError **error)
{
    sqlite3 *db;
    sqlite3_stmt *statement = NULL;
    gint result;
    GVariant *lease = NULL;
    g_return_val_if_fail (LRG_IS_MMO_SHARD_DIRECTORY (self), NULL);
    if (!_lrg_mmo_id_valid (zone))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid zone ID");
        return NULL;
    }
    db = _lrg_mmo_store_database (self->store);
    if (sqlite3_prepare_v2 (db, "SELECT fence,owner,endpoint,expires,state FROM lrg_leases WHERE zone=?1 "
                           "AND expires>CAST(strftime('%s','now') AS INTEGER)", -1, &statement, NULL) != SQLITE_OK)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_FAILED, sqlite3_errmsg (db));
        return NULL;
    }
    sqlite3_bind_text (statement, 1, zone, -1, SQLITE_TRANSIENT);
    result = sqlite3_step (statement);
    if (result == SQLITE_ROW)
        lease = g_variant_ref_sink (g_variant_new ("(tssx@ay)", (guint64) sqlite3_column_int64 (statement, 0),
                     sqlite3_column_text (statement, 1), sqlite3_column_text (statement, 2),
                     (gint64) sqlite3_column_int64 (statement, 3),
                     g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, sqlite3_column_blob (statement, 4),
                                                sqlite3_column_bytes (statement, 4), 1)));
    else
        _lrg_mmo_fail (error, result == SQLITE_DONE ? G_IO_ERROR_NOT_FOUND : G_IO_ERROR_FAILED,
                       "No live zone lease");
    sqlite3_finalize (statement);
    return lease;
}
