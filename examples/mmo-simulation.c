/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include <libregnum.h>

static gboolean
simulate (GError **error)
{
    g_autoptr(LrgMmoRealm) realm = NULL;
    g_autoptr(LrgMmoReplicator) replication = NULL;
    g_autoptr(LrgMmoGroup) party = NULL;
    g_autoptr(LrgMmoStore) store = NULL;
    g_autoptr(GBytes) state = NULL;
    g_autoptr(GBytes) restored = NULL;
    g_autoptr(GVariant) delta = NULL;
    g_autoptr(GVariant) batch = NULL;
    GVariantBuilder changes;
    guint64 sequence;
    guint64 revision;
    gint64 now = g_get_monotonic_time ();

    realm = lrg_mmo_realm_new (100, 30 * G_TIME_SPAN_SECOND);
    replication = lrg_mmo_replicator_new (1000, 100, 32);
    party = lrg_mmo_group_new (1001, 5);
    store = lrg_mmo_store_new (":memory:", error);
    if (store == NULL)
        return FALSE;

    /* These identities are supplied by this local demo, not by a remote client. */
    if (!lrg_mmo_realm_add_zone (realm, "town", 100, error) ||
        !lrg_mmo_realm_login (realm, 1, "alice", now, error) ||
        !lrg_mmo_realm_login (realm, 2, "bob", now, error) ||
        !lrg_mmo_realm_enter_zone (realm, 1, "town", error) ||
        !lrg_mmo_realm_enter_zone (realm, 2, "town", error) ||
        !lrg_mmo_group_add (party, 1001, 1002, error) ||
        !lrg_mmo_realm_accept_command (realm, 1, 1, now, error))
        return FALSE;

    /* The game has already validated this movement and chosen public state. */
    state = g_bytes_new_static ("character:alice;health:100", 26);
    if (!lrg_mmo_replicator_upsert (replication, 1001, "town", 5, 0, 0, state, error))
        return FALSE;
    lrg_mmo_realm_advance (realm, 0.05);
    delta = lrg_mmo_replicator_build (replication, 2, "town", 0, 0, 0, 64, error);
    if (delta == NULL)
        return FALSE;
    g_variant_get_child (delta, 0, "t", &sequence);
    g_print ("Bob receives snapshot %" G_GUINT64_FORMAT " (%" G_GSIZE_FORMAT " bytes)\n",
             sequence, g_variant_get_size (delta));
    /* The local demo client has applied the complete snapshot. */
    if (!lrg_mmo_replicator_acknowledge (replication, 2, sequence, error))
        return FALSE;

    g_variant_builder_init (&changes, G_VARIANT_TYPE ("a(stay)"));
    g_variant_builder_add (&changes, "(st@ay)", "character/alice", (guint64) 0,
                           g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), state, TRUE));
    g_variant_builder_add (&changes, "(st@ay)", "character/bob", (guint64) 0,
                           g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, "health:100", 10, 1));
    batch = g_variant_ref_sink (g_variant_builder_end (&changes));
    if (!lrg_mmo_store_commit (store, batch, error))
        return FALSE;
    restored = lrg_mmo_store_read (store, "character/alice", &revision, error);
    if (restored == NULL)
        return FALSE;
    g_print ("Both characters committed atomically; Alice revision %" G_GUINT64_FORMAT "\n", revision);
    lrg_mmo_realm_logout (realm, 1);
    lrg_mmo_realm_logout (realm, 2);
    lrg_mmo_replicator_forget (replication, 2);
    return TRUE;
}

int
main (void)
{
    g_autoptr(GError) error = NULL;
    if (!simulate (&error))
    {
        g_printerr ("Simulation failed: %s\n", error->message);
        return 1;
    }
    return 0;
}
