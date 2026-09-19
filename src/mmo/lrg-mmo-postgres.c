/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-postgres-private.h"
#include "lrg-mmo-service-private.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PGresult, PQclear)

static PGresult *
query (PGconn *db, const gchar *sql, guint count, const gchar **values, GError **error)
{
    PGresult *result = PQexecParams (db, sql, count, NULL, values, NULL, NULL, 0);
    ExecStatusType status = PQresultStatus (result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
    {
        const gchar *state = PQresultErrorField (result, PG_DIAG_SQLSTATE);
        GIOErrorEnum code = g_strcmp0 (state, "23505") == 0 ? G_IO_ERROR_WRONG_ETAG : G_IO_ERROR_FAILED;
        /* Server diagnostics may include credentials or record contents. */
        _lrg_mmo_fail (error, code, "PostgreSQL operation failed; transaction may need retry");
        PQclear (result);
        return NULL;
    }
    return result;
}

static gboolean
command (PGconn *db, const gchar *sql, GError **error)
{
    g_autoptr(PGresult) result = query (db, sql, 0, NULL, error);
    return result != NULL;
}

static gboolean
begin (PGconn *db, GError **error)
{
    if (!command (db, "BEGIN", error))
        return FALSE;
    /* One shared ordering boundary for leases, receipt checks and record CAS.
     * Deliberately coarse until application-specific partitioning is measured. */
    if (!command (db, "SELECT pg_advisory_xact_lock(1280460621)", error))
    {
        command (db, "ROLLBACK", NULL);
        return FALSE;
    }
    return TRUE;
}

PGconn *
_lrg_mmo_pg_open (const gchar *connection, GError **error)
{
    const gchar *keys[] = { "connect_timeout", "sslmode", "target_session_attrs", "dbname", "client_encoding", NULL };
    const gchar *values[] = { "5", "verify-full", "read-write", connection, "UTF8", NULL };
    PGconn *db = PQconnectdbParams (keys, values, 1);
    g_autoptr(PGresult) result = NULL;
    if (PQstatus (db) != CONNECTION_OK)
        goto fail;
    if (!command (db, "SET client_min_messages=warning", error) ||
        !command (db, "SET statement_timeout='5s'", error) ||
        !command (db, "SET lock_timeout='1s'", error) ||
        !command (db, "SET search_path=public,pg_catalog", error) ||
        !begin (db, error))
        goto fail;
    if (!command (db, "CREATE TABLE IF NOT EXISTS lrg_records (key TEXT PRIMARY KEY,"
                       "revision BIGINT NOT NULL CHECK(revision>0),data BYTEA NOT NULL)", error) ||
        !command (db, "CREATE TABLE IF NOT EXISTS lrg_operations (id TEXT PRIMARY KEY,digest TEXT NOT NULL)", error) ||
        !command (db, "CREATE TABLE IF NOT EXISTS lrg_audit (sequence BIGSERIAL PRIMARY KEY,"
                       "operation TEXT,digest TEXT NOT NULL,created BIGINT NOT NULL)", error) ||
        !command (db, "CREATE TABLE IF NOT EXISTS lrg_leases (zone TEXT PRIMARY KEY,owner TEXT NOT NULL,"
                       "endpoint TEXT NOT NULL,fence BIGINT NOT NULL,expires BIGINT NOT NULL,state BYTEA NOT NULL)", error) ||
        !command (db, "COMMIT", error))
        goto fail;
    return db;
fail:
    if (error != NULL && *error == NULL)
        _lrg_mmo_fail (error, G_IO_ERROR_FAILED, "PostgreSQL connection unavailable");
    PQfinish (db);
    return NULL;
}

static GBytes *
read_bytes (PGresult *result, guint row, guint column, GError **error)
{
    size_t size;
    unsigned char *data = PQunescapeBytea ((unsigned char *) PQgetvalue (result, row, column), &size);
    GBytes *bytes;
    if (data == NULL || size > 1024 * 1024)
    {
        PQfreemem (data);
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Invalid PostgreSQL record payload");
        return NULL;
    }
    bytes = g_bytes_new (data, size);
    PQfreemem (data);
    return bytes;
}

GBytes *
_lrg_mmo_pg_read (PGconn *db, const gchar *key, guint64 *revision, GError **error)
{
    const gchar *values[] = { key };
    g_autoptr(PGresult) result = query (db, "SELECT revision,data FROM lrg_records WHERE key=$1", 1, values, error);
    GBytes *bytes;
    if (result == NULL)
        return NULL;
    if (PQntuples (result) != 1)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_NOT_FOUND, "Record not found");
        return NULL;
    }
    bytes = read_bytes (result, 0, 1, error);
    if (bytes != NULL)
        *revision = g_ascii_strtoull (PQgetvalue (result, 0, 0), NULL, 10);
    return bytes;
}

gboolean
_lrg_mmo_pg_commit (PGconn *db, GVariant *changes, const gchar *operation, const gchar *digest,
                    gboolean *duplicate, const gchar *zone, const gchar *owner, guint64 fence, GError **error)
{
    g_autoptr(PGresult) result = NULL;
    GVariantIter iter;
    const gchar *key;
    guint64 revision;
    GVariant *payload;
    const gchar *receipt[] = { operation, digest };
    if (!begin (db, error))
        return FALSE;
    if (operation != NULL)
    {
        result = query (db, "SELECT digest FROM lrg_operations WHERE id=$1", 1, receipt, error);
        if (result == NULL)
            goto rollback;
        if (PQntuples (result) != 0)
        {
            gboolean same = g_str_equal (digest, PQgetvalue (result, 0, 0));
            command (db, "ROLLBACK", NULL);
            if (!same)
                return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Operation ID reused with different changes");
            if (duplicate != NULL)
                *duplicate = TRUE;
            return TRUE;
        }
        g_clear_pointer (&result, PQclear);
    }
    if (zone != NULL)
    {
        g_autofree gchar *serial = g_strdup_printf ("%" G_GUINT64_FORMAT, fence);
        const gchar *values[] = { zone, owner, serial };
        result = query (db, "SELECT 1 FROM lrg_leases WHERE zone=$1 AND owner=$2 AND fence=$3::bigint "
                            "AND expires>floor(extract(epoch FROM clock_timestamp()))", 3, values, error);
        if (result == NULL)
            goto rollback;
        if (PQntuples (result) != 1)
        {
            _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Stale or expired fencing token");
            goto rollback;
        }
        g_clear_pointer (&result, PQclear);
    }
    g_variant_iter_init (&iter, changes);
    while (g_variant_iter_next (&iter, "(&st@ay)", &key, &revision, &payload))
    {
        gsize size;
        const guint8 *data = g_variant_get_fixed_array (payload, &size, 1);
        g_autofree gchar *encoded = g_base64_encode (data, size);
        g_autofree gchar *serial = g_strdup_printf ("%" G_GUINT64_FORMAT, revision);
        const gchar *values[] = { key, encoded, serial };
        const gchar *sql = revision == 0 ?
            "INSERT INTO lrg_records VALUES ($1,1,decode($2,'base64'))" :
            "UPDATE lrg_records SET data=decode($2,'base64'),revision=revision+1 WHERE key=$1 AND revision=$3::bigint";
        result = query (db, sql, revision == 0 ? 2 : 3, values, error);
        g_variant_unref (payload);
        if (result == NULL)
            goto rollback;
        if (!g_str_equal (PQcmdTuples (result), "1"))
        {
            _lrg_mmo_fail (error, G_IO_ERROR_WRONG_ETAG, "Record revision conflict");
            goto rollback;
        }
        g_clear_pointer (&result, PQclear);
    }
    if (operation != NULL)
    {
        result = query (db, "INSERT INTO lrg_operations VALUES ($1,$2)", 2, receipt, error);
        if (result == NULL)
            goto rollback;
        g_clear_pointer (&result, PQclear);
    }
    result = query (db, "INSERT INTO lrg_audit(operation,digest,created) VALUES "
                        "($1,$2,floor(extract(epoch FROM clock_timestamp())))", 2, receipt, error);
    if (result != NULL && command (db, "COMMIT", error))
        return TRUE;
rollback:
    command (db, "ROLLBACK", NULL);
    return FALSE;
}

GVariant *
_lrg_mmo_pg_audit (PGconn *db, guint64 after, guint limit, GError **error)
{
    g_autofree gchar *cursor = g_strdup_printf ("%" G_GUINT64_FORMAT, after);
    g_autofree gchar *count = g_strdup_printf ("%u", limit);
    const gchar *values[] = { cursor, count };
    g_autoptr(PGresult) result = query (db, "SELECT sequence,coalesce(operation,''),digest,created "
                  "FROM lrg_audit WHERE sequence>$1::bigint ORDER BY sequence LIMIT $2::int", 2, values, error);
    GVariantBuilder builder;
    gint i;
    if (result == NULL)
        return NULL;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(tssx)"));
    for (i = 0; i < PQntuples (result); i++)
        g_variant_builder_add (&builder, "(tssx)", g_ascii_strtoull (PQgetvalue (result, i, 0), NULL, 10),
                               PQgetvalue (result, i, 1), PQgetvalue (result, i, 2),
                               g_ascii_strtoll (PQgetvalue (result, i, 3), NULL, 10));
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

guint64
_lrg_mmo_pg_lease (PGconn *db, const gchar *zone, const gchar *owner, guint64 fence,
                   const gchar *destination, const gchar *endpoint, GBytes *state,
                   guint ttl, guint operation, GError **error)
{
    const gchar *zone_value[] = { zone };
    g_autoptr(PGresult) result = NULL;
    g_autoptr(GBytes) previous = NULL;
    g_autofree gchar *old_endpoint = NULL;
    g_autofree gchar *serial = NULL;
    g_autofree gchar *deadline = NULL;
    g_autofree gchar *encoded = NULL;
    guint64 current = 0, next;
    gint64 now;
    gsize size;
    gconstpointer bytes;
    const gchar *values[6];
    if (!begin (db, error))
        return 0;
    result = query (db, "SELECT floor(extract(epoch FROM clock_timestamp()))::bigint", 0, NULL, error);
    if (result == NULL)
        goto rollback;
    now = g_ascii_strtoll (PQgetvalue (result, 0, 0), NULL, 10);
    g_clear_pointer (&result, PQclear);
    result = query (db, "SELECT owner,endpoint,fence,expires,state FROM lrg_leases WHERE zone=$1", 1, zone_value, error);
    if (result == NULL)
        goto rollback;
    if (PQntuples (result) == 1)
    {
        gint64 expiry = g_ascii_strtoll (PQgetvalue (result, 0, 3), NULL, 10);
        current = g_ascii_strtoull (PQgetvalue (result, 0, 2), NULL, 10);
        if ((operation == 0 && expiry > now) || (operation != 0 &&
            (expiry <= now || current != fence || !g_str_equal (owner, PQgetvalue (result, 0, 0)))))
            goto denied;
        old_endpoint = g_strdup (PQgetvalue (result, 0, 1));
        previous = read_bytes (result, 0, 4, error);
        if (previous == NULL)
            goto rollback;
    }
    else if (operation != 0)
        goto denied;
    if ((operation == 0 || operation == 3) && current >= G_MAXINT64)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_NO_SPACE, "Fencing token exhausted");
        goto rollback;
    }
    next = current + (operation == 0 || operation == 3);
    serial = g_strdup_printf ("%" G_GUINT64_FORMAT, next);
    deadline = g_strdup_printf ("%" G_GINT64_FORMAT, operation == 2 ? (gint64) 0 : now + ttl);
    if (state == NULL)
        state = previous;
    size = 0;
    bytes = state != NULL ? g_bytes_get_data (state, &size) : NULL;
    encoded = g_base64_encode (bytes, size);
    values[0] = zone;
    values[1] = operation == 3 ? destination : owner;
    values[2] = endpoint != NULL ? endpoint : old_endpoint;
    values[3] = serial;
    values[4] = deadline;
    values[5] = encoded;
    g_clear_pointer (&result, PQclear);
    result = query (db, "INSERT INTO lrg_leases VALUES ($1,$2,$3,$4::bigint,$5::bigint,decode($6,'base64')) "
                        "ON CONFLICT(zone) DO UPDATE SET owner=excluded.owner,endpoint=excluded.endpoint,"
                        "fence=excluded.fence,expires=excluded.expires,state=excluded.state", 6, values, error);
    if (result != NULL && command (db, "COMMIT", error))
        return next;
    goto rollback;
denied:
    _lrg_mmo_fail (error, G_IO_ERROR_PERMISSION_DENIED, "Zone lease is held, stale or expired");
rollback:
    command (db, "ROLLBACK", NULL);
    return 0;
}

GVariant *
_lrg_mmo_pg_lookup (PGconn *db, const gchar *zone, GError **error)
{
    const gchar *values[] = { zone };
    g_autoptr(PGresult) result = query (db, "SELECT fence,owner,endpoint,expires,state FROM lrg_leases "
                  "WHERE zone=$1 AND expires>floor(extract(epoch FROM clock_timestamp()))", 1, values, error);
    g_autoptr(GBytes) bytes = NULL;
    if (result == NULL)
        return NULL;
    if (PQntuples (result) != 1)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_NOT_FOUND, "No live zone lease");
        return NULL;
    }
    bytes = read_bytes (result, 0, 4, error);
    if (bytes == NULL)
        return NULL;
    return g_variant_ref_sink (g_variant_new ("(tssx@ay)",
               g_ascii_strtoull (PQgetvalue (result, 0, 0), NULL, 10),
               PQgetvalue (result, 0, 1), PQgetvalue (result, 0, 2),
               g_ascii_strtoll (PQgetvalue (result, 0, 3), NULL, 10),
               g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), bytes, TRUE)));
}
