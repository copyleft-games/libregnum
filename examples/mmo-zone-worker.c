/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>

typedef struct
{
    LrgMmoStore *store;
    LrgMmoShardDirectory *directory;
    LrgMmoSimulation *simulation;
    GMainLoop *loop;
    const gchar *zone, *owner, *endpoint;
    guint64 fence, revision;
    gchar *record;
    gboolean failed;
} Worker;

static gboolean
checkpoint (Worker *worker, GError **error)
{
    g_autoptr(GVariant) snapshot = lrg_mmo_simulation_snapshot (worker->simulation);
    g_autoptr(GVariant) batch = NULL;
    g_autoptr(GBytes) bytes = NULL;
    GVariantBuilder changes;
#if G_BYTE_ORDER == G_BIG_ENDIAN
    g_autoptr(GVariant) swapped = g_variant_byteswap (snapshot);
    bytes = g_variant_get_data_as_bytes (swapped);
#else
    bytes = g_variant_get_data_as_bytes (snapshot);
#endif
    g_variant_builder_init (&changes, G_VARIANT_TYPE ("a(stay)"));
    g_variant_builder_add (&changes, "(st@ay)", worker->record, worker->revision,
                           g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), bytes, TRUE));
    batch = g_variant_ref_sink (g_variant_builder_end (&changes));
    if (!lrg_mmo_store_commit_fenced (worker->store, worker->zone, worker->owner,
                                      worker->fence, batch, error))
        return FALSE;
    worker->revision++;
    return TRUE;
}

static gboolean
step (gpointer data)
{
    Worker *worker = data;
    g_autoptr(GError) error = NULL;
    if (worker->fence == 0)
    {
        g_autoptr(GBytes) bytes = NULL;
        g_autoptr(GVariant) snapshot = NULL;
        worker->fence = lrg_mmo_shard_directory_acquire (worker->directory, worker->zone,
                                                       worker->owner, worker->endpoint, 3, &error);
        if (worker->fence == 0)
        {
            if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
                return G_SOURCE_CONTINUE;
            goto fail;
        }
        bytes = lrg_mmo_store_read (worker->store, worker->record, &worker->revision, &error);
        if (bytes == NULL)
        {
            if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
                goto fail;
            g_clear_error (&error);
        }
        else
        {
            snapshot = g_variant_ref_sink (g_variant_new_from_bytes (
                        G_VARIANT_TYPE ("(ta(tssdddduududtttt))"), bytes, FALSE));
#if G_BYTE_ORDER == G_BIG_ENDIAN
            {
                GVariant *swapped = g_variant_byteswap (snapshot);
                g_variant_unref (snapshot);
                snapshot = swapped;
            }
#endif
            if (!lrg_mmo_simulation_restore (worker->simulation, snapshot, &error))
                goto fail;
        }
        g_print ("OWNER %s fence=%" G_GUINT64_FORMAT " revision=%" G_GUINT64_FORMAT "\n",
                 worker->owner, worker->fence, worker->revision);
        fflush (stdout);
    }
    if (!lrg_mmo_shard_directory_renew (worker->directory, worker->zone, worker->owner,
                                       worker->fence, 3, &error) ||
        !lrg_mmo_simulation_advance (worker->simulation, 20, &error) || !checkpoint (worker, &error))
        goto fail;
    return G_SOURCE_CONTINUE;
fail:
    worker->failed = TRUE;
    g_printerr ("Zone worker lost authority or storage: %s\n", error->message);
    g_main_loop_quit (worker->loop);
    return G_SOURCE_REMOVE;
}

static gboolean
stop (gpointer data)
{
    g_main_loop_quit (((Worker *) data)->loop);
    return G_SOURCE_CONTINUE;
}

int
main (int argc, char **argv)
{
    g_autoptr(GOptionContext) options = g_option_context_new ("- fenced zone checkpoint/failover reference");
    g_autofree gchar *database = NULL, *postgres_file = NULL, *connection = NULL;
    g_autofree gchar *zone = NULL, *owner = NULL, *endpoint = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgMmoStore) store = NULL;
    g_autoptr(LrgMmoShardDirectory) directory = NULL;
    g_autoptr(LrgMmoSimulation) simulation = NULL;
    g_autoptr(GMainLoop) loop = NULL;
    gboolean version = FALSE;
    guint timer, interrupt, terminate;
    Worker worker = { 0 };
    GOptionEntry entries[] = {
        { "database", 0, 0, G_OPTION_ARG_FILENAME, &database, "SQLite database", "PATH" },
        { "postgres-file", 0, 0, G_OPTION_ARG_FILENAME, &postgres_file, "Private libpq connection file", "PATH" },
        { "zone", 0, 0, G_OPTION_ARG_STRING, &zone, "Zone identifier", "ID" },
        { "owner", 0, 0, G_OPTION_ARG_STRING, &owner, "Unique worker incarnation", "ID" },
        { "endpoint", 0, 0, G_OPTION_ARG_STRING, &endpoint, "Advertised gameplay endpoint", "URI" },
        { "version", 0, 0, G_OPTION_ARG_NONE, &version, "Print version and license", NULL },
        { NULL }
    };
    g_option_context_add_main_entries (options, entries, NULL);
    g_option_context_set_summary (options, "Example: mmo-zone-worker --database world.db --zone forest --owner unique-worker --endpoint tls://localhost:7777");
    if (!g_option_context_parse (options, &argc, &argv, &error))
        goto fail;
    if (version)
    {
        g_print ("Libregnum zone worker %s\nAGPL-3.0-or-later\n", LRG_VERSION_STRING);
        return 0;
    }
    if ((database == NULL) == (postgres_file == NULL) || zone == NULL || owner == NULL || endpoint == NULL)
    {
        g_printerr ("Specify one database mode, zone, unique owner and endpoint\n");
        return 1;
    }
    if (postgres_file != NULL && !g_file_get_contents (postgres_file, &connection, NULL, &error))
        goto fail;
    store = connection != NULL ? lrg_mmo_store_new_postgres (connection, &error) : lrg_mmo_store_new (database, &error);
    if (store == NULL)
        goto fail;
    directory = lrg_mmo_shard_directory_new (store, &error);
    if (directory == NULL)
        goto fail;
    simulation = lrg_mmo_simulation_new (1024);
    loop = g_main_loop_new (NULL, FALSE);
    worker.store = store;
    worker.directory = directory;
    worker.simulation = simulation;
    worker.loop = loop;
    worker.zone = zone;
    worker.owner = owner;
    worker.endpoint = endpoint;
    worker.record = g_strconcat ("world/zone/", zone, NULL);
    timer = g_timeout_add_seconds (1, step, &worker);
    interrupt = g_unix_signal_add (SIGINT, stop, &worker);
    terminate = g_unix_signal_add (SIGTERM, stop, &worker);
    g_main_loop_run (loop);
    if (!worker.failed)
    {
        g_source_remove (timer);
        if (worker.fence != 0)
            lrg_mmo_shard_directory_release (directory, zone, owner, worker.fence, NULL);
    }
    g_source_remove (interrupt);
    g_source_remove (terminate);
    g_free (worker.record);
    return worker.failed ? 1 : 0;
fail:
    g_printerr ("Zone worker: %s\n", error->message);
    return 1;
}
