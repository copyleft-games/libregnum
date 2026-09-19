/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-realm.h"
#include <math.h>

typedef struct
{
    guint capacity;
    guint occupants;
} Zone;

typedef struct
{
    gchar *account;
    gchar *zone;
    gint64 last_seen;
    gint64 refill_time;
    guint64 sequence;
    gdouble tokens;
} Session;

struct _LrgMmoRealm
{
    GObject parent_instance;
    GHashTable *sessions;
    GHashTable *accounts;
    GHashTable *zones;
    guint capacity;
    gint64 timeout;
    gdouble accumulator;
    guint64 tick;
    gboolean advancing;
};

G_DEFINE_TYPE (LrgMmoRealm, lrg_mmo_realm, G_TYPE_OBJECT)

static guint session_ended_signal;
static guint tick_signal;

static gboolean
fail (GError      **error,
      GIOErrorEnum code,
      const gchar *message)
{
    g_set_error_literal (error, G_IO_ERROR, code, message);
    return FALSE;
}

static gboolean
valid_id (const gchar *id)
{
    return id != NULL && *id != '\0' && strlen (id) <= 128 && g_utf8_validate (id, -1, NULL);
}

static void
session_free (gpointer data)
{
    Session *session = data;
    g_free (session->account);
    g_free (session->zone);
    g_free (session);
}

static void
lrg_mmo_realm_finalize (GObject *object)
{
    LrgMmoRealm *self = LRG_MMO_REALM (object);
    g_hash_table_unref (self->accounts);
    g_hash_table_unref (self->sessions);
    g_hash_table_unref (self->zones);
    G_OBJECT_CLASS (lrg_mmo_realm_parent_class)->finalize (object);
}

static void
lrg_mmo_realm_class_init (LrgMmoRealmClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_realm_finalize;
    /**
     * LrgMmoRealm::session-ended:
     * @self: the realm
     * @peer_id: removed transport peer ID
     *
     * Emitted after logout or idle expiration releases the session and zone slot.
     */
    session_ended_signal = g_signal_new ("session-ended", G_TYPE_FROM_CLASS (klass),
                                       G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                       G_TYPE_NONE, 1, G_TYPE_UINT);
    /**
     * LrgMmoRealm::tick:
     * @self: the realm
     * @number: monotonically increasing tick number
     * @step: simulation step in seconds
     *
     * Emitted for each fixed simulation step.
     */
    tick_signal = g_signal_new ("tick", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                               0, NULL, NULL, NULL, G_TYPE_NONE, 2,
                               G_TYPE_UINT64, G_TYPE_DOUBLE);
}

static void
lrg_mmo_realm_init (LrgMmoRealm *self)
{
    self->sessions = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, session_free);
    self->accounts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    self->zones = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

LrgMmoRealm *
lrg_mmo_realm_new (guint  capacity,
                   gint64 timeout_us)
{
    LrgMmoRealm *self;
    g_return_val_if_fail (capacity > 0, NULL);
    g_return_val_if_fail (timeout_us > 0, NULL);
    self = g_object_new (LRG_TYPE_MMO_REALM, NULL);
    self->capacity = capacity;
    self->timeout = timeout_us;
    return self;
}

gboolean
lrg_mmo_realm_add_zone (LrgMmoRealm  *self,
                        const gchar  *zone,
                        guint         capacity,
                        GError      **error)
{
    Zone *entry;
    g_return_val_if_fail (LRG_IS_MMO_REALM (self), FALSE);
    if (!valid_id (zone) || capacity == 0)
        return fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid zone or capacity");
    if (g_hash_table_contains (self->zones, zone))
        return fail (error, G_IO_ERROR_EXISTS, "Zone already registered");
    entry = g_new0 (Zone, 1);
    entry->capacity = capacity;
    g_hash_table_insert (self->zones, g_strdup (zone), entry);
    return TRUE;
}

gboolean
lrg_mmo_realm_login (LrgMmoRealm  *self,
                     guint32       peer_id,
                     const gchar  *account,
                     gint64        now_us,
                     GError      **error)
{
    Session *session;
    g_return_val_if_fail (LRG_IS_MMO_REALM (self), FALSE);
    if (peer_id == 0 || !valid_id (account) || now_us < 0)
        return fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid admission parameters");
    if (g_hash_table_contains (self->sessions, GUINT_TO_POINTER (peer_id)) ||
        g_hash_table_contains (self->accounts, account))
        return fail (error, G_IO_ERROR_EXISTS, "Account or peer already admitted");
    if (g_hash_table_size (self->sessions) >= self->capacity)
        return fail (error, G_IO_ERROR_NO_SPACE, "Realm is full");
    session = g_new0 (Session, 1);
    session->account = g_strdup (account);
    session->last_seen = now_us;
    session->refill_time = now_us;
    session->tokens = 100;
    g_hash_table_insert (self->sessions, GUINT_TO_POINTER (peer_id), session);
    g_hash_table_insert (self->accounts, g_strdup (account), GUINT_TO_POINTER (peer_id));
    return TRUE;
}

void
lrg_mmo_realm_logout (LrgMmoRealm *self,
                      guint32      peer_id)
{
    Session *session;
    g_return_if_fail (LRG_IS_MMO_REALM (self));
    session = g_hash_table_lookup (self->sessions, GUINT_TO_POINTER (peer_id));
    if (session == NULL)
        return;
    if (session->zone != NULL)
    {
        Zone *zone = g_hash_table_lookup (self->zones, session->zone);
        zone->occupants--;
    }
    g_hash_table_remove (self->accounts, session->account);
    g_hash_table_remove (self->sessions, GUINT_TO_POINTER (peer_id));
    g_signal_emit (self, session_ended_signal, 0, peer_id);
}

gboolean
lrg_mmo_realm_enter_zone (LrgMmoRealm  *self,
                          guint32       peer_id,
                          const gchar  *zone,
                          GError      **error)
{
    Session *session;
    Zone *destination;
    gchar *zone_copy;
    g_return_val_if_fail (LRG_IS_MMO_REALM (self), FALSE);
    if (!valid_id (zone))
        return fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid zone");
    session = g_hash_table_lookup (self->sessions, GUINT_TO_POINTER (peer_id));
    destination = g_hash_table_lookup (self->zones, zone);
    if (session == NULL || destination == NULL)
        return fail (error, G_IO_ERROR_NOT_FOUND, "Session or zone not found");
    if (g_strcmp0 (session->zone, zone) == 0)
        return TRUE;
    if (destination->occupants >= destination->capacity)
        return fail (error, G_IO_ERROR_NO_SPACE, "Destination zone is full");
    zone_copy = g_strdup (zone);
    if (session->zone != NULL)
    {
        Zone *previous = g_hash_table_lookup (self->zones, session->zone);
        previous->occupants--;
    }
    destination->occupants++;
    g_free (session->zone);
    session->zone = zone_copy;
    return TRUE;
}

const gchar *
lrg_mmo_realm_get_zone (LrgMmoRealm *self,
                        guint32      peer_id)
{
    Session *session;
    g_return_val_if_fail (LRG_IS_MMO_REALM (self), NULL);
    session = g_hash_table_lookup (self->sessions, GUINT_TO_POINTER (peer_id));
    return session != NULL ? session->zone : NULL;
}

const gchar *
lrg_mmo_realm_get_account (LrgMmoRealm *self,
                           guint32      peer_id)
{
    Session *session;
    g_return_val_if_fail (LRG_IS_MMO_REALM (self), NULL);
    session = g_hash_table_lookup (self->sessions, GUINT_TO_POINTER (peer_id));
    return session != NULL ? session->account : NULL;
}

gboolean
lrg_mmo_realm_accept_command (LrgMmoRealm  *self,
                              guint32       peer_id,
                              guint64       sequence,
                              gint64        now_us,
                              GError      **error)
{
    Session *session;
    gdouble tokens;
    g_return_val_if_fail (LRG_IS_MMO_REALM (self), FALSE);
    session = g_hash_table_lookup (self->sessions, GUINT_TO_POINTER (peer_id));
    if (session == NULL)
        return fail (error, G_IO_ERROR_PERMISSION_DENIED, "Peer is not admitted");
    if (now_us < session->last_seen || now_us < session->refill_time)
        return fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Monotonic clock moved backwards");
    if (now_us - session->last_seen >= self->timeout)
        return fail (error, G_IO_ERROR_TIMED_OUT, "Session has expired");
    if (sequence == 0 || sequence <= session->sequence)
        return fail (error, G_IO_ERROR_INVALID_DATA, "Replayed or out-of-order command");
    tokens = MIN (100.0, session->tokens + (now_us - session->refill_time) / 20000.0);
    if (tokens < 1.0)
        return fail (error, G_IO_ERROR_WOULD_BLOCK, "Command rate exceeded");
    session->tokens = tokens - 1.0;
    session->refill_time = now_us;
    session->last_seen = now_us;
    session->sequence = sequence;
    return TRUE;
}

guint
lrg_mmo_realm_expire (LrgMmoRealm *self,
                      gint64       now_us)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    g_autoptr(GArray) expired = NULL;
    g_autoptr(LrgMmoRealm) keep_alive = NULL;
    guint i;
    guint count = 0;
    g_return_val_if_fail (LRG_IS_MMO_REALM (self), 0);
    keep_alive = g_object_ref (self);
    expired = g_array_new (FALSE, FALSE, sizeof (guint32));
    g_hash_table_iter_init (&iter, self->sessions);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        Session *session = value;
        if (now_us >= session->last_seen && now_us - session->last_seen >= self->timeout)
        {
            guint32 id = GPOINTER_TO_UINT (key);
            g_array_append_val (expired, id);
        }
    }
    for (i = 0; i < expired->len; i++)
    {
        guint32 id = g_array_index (expired, guint32, i);
        Session *session = g_hash_table_lookup (self->sessions, GUINT_TO_POINTER (id));
        /* Signal handlers may refresh or replace another session. */
        if (session != NULL && now_us >= session->last_seen &&
            now_us - session->last_seen >= self->timeout)
        {
            lrg_mmo_realm_logout (self, id);
            count++;
        }
    }
    return count;
}

guint
lrg_mmo_realm_advance (LrgMmoRealm *self,
                       gdouble      elapsed)
{
    guint steps;
    guint i;
    g_autoptr(LrgMmoRealm) keep_alive = NULL;
    g_return_val_if_fail (LRG_IS_MMO_REALM (self), 0);
    if (!isfinite (elapsed) || elapsed < 0 || self->advancing)
        return 0;
    keep_alive = g_object_ref (self);
    /* Clamp before addition, keeping arithmetic finite even for DBL_MAX. */
    self->accumulator += elapsed > 0.4 ? 0.4 + fmod (elapsed, 0.05) : elapsed;
    steps = MIN (8u, (guint) floor ((self->accumulator + 1e-12) / 0.05));
    self->accumulator = fmod (MAX (0.0, self->accumulator - steps * 0.05), 0.05);
    self->advancing = TRUE;
    for (i = 0; i < steps; i++)
    {
        self->tick++;
        g_signal_emit (self, tick_signal, 0, self->tick, 0.05);
    }
    self->advancing = FALSE;
    return steps;
}

guint
lrg_mmo_realm_get_session_count (LrgMmoRealm *self)
{
    g_return_val_if_fail (LRG_IS_MMO_REALM (self), 0);
    return g_hash_table_size (self->sessions);
}
