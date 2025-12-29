/* lrg-steam-presence.c - Steam Rich Presence wrapper
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-steam-presence.h"
#include "lrg-steam-types.h"

/**
 * SECTION:lrg-steam-presence
 * @title: LrgSteamPresence
 * @short_description: Steam Rich Presence wrapper
 *
 * #LrgSteamPresence provides access to Steam Rich Presence for
 * showing game status to friends. Rich presence strings must be
 * configured in the Steamworks app configuration.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * g_autoptr(LrgSteamPresence) presence;
 *
 * presence = lrg_steam_presence_new (client);
 * lrg_steam_presence_set_status (presence, "In Main Menu");
 *
 * // Later, update status
 * lrg_steam_presence_set_status (presence, "Playing Level 5");
 *
 * // On shutdown
 * lrg_steam_presence_clear (presence);
 * ]|
 */

struct _LrgSteamPresence
{
    GObject         parent_instance;
    LrgSteamClient *client;
};

enum
{
    PROP_0,
    PROP_CLIENT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgSteamPresence, lrg_steam_presence, G_TYPE_OBJECT)

static void
lrg_steam_presence_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgSteamPresence *self = LRG_STEAM_PRESENCE (object);

    switch (prop_id)
    {
    case PROP_CLIENT:
        g_clear_object (&self->client);
        self->client = g_value_dup_object (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_steam_presence_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgSteamPresence *self = LRG_STEAM_PRESENCE (object);

    switch (prop_id)
    {
    case PROP_CLIENT:
        g_value_set_object (value, self->client);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_steam_presence_dispose (GObject *object)
{
    LrgSteamPresence *self = LRG_STEAM_PRESENCE (object);

    /* Clear rich presence on dispose */
    lrg_steam_presence_clear (self);

    g_clear_object (&self->client);

    G_OBJECT_CLASS (lrg_steam_presence_parent_class)->dispose (object);
}

static void
lrg_steam_presence_class_init (LrgSteamPresenceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lrg_steam_presence_set_property;
    object_class->get_property = lrg_steam_presence_get_property;
    object_class->dispose = lrg_steam_presence_dispose;

    properties[PROP_CLIENT] =
        g_param_spec_object ("client",
                             "Client",
                             "The Steam client",
                             LRG_TYPE_STEAM_CLIENT,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_steam_presence_init (LrgSteamPresence *self)
{
    self->client = NULL;
}

/**
 * lrg_steam_presence_new:
 * @client: an #LrgSteamClient
 *
 * Creates a new Steam presence manager.
 *
 * Returns: (transfer full): A new #LrgSteamPresence
 */
LrgSteamPresence *
lrg_steam_presence_new (LrgSteamClient *client)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLIENT (client), NULL);

    return g_object_new (LRG_TYPE_STEAM_PRESENCE,
                         "client", client,
                         NULL);
}

/**
 * lrg_steam_presence_set:
 * @self: an #LrgSteamPresence
 * @key: the rich presence key
 * @value: the value (or %NULL to clear)
 *
 * Sets a rich presence key-value pair.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_presence_set (LrgSteamPresence *self,
                        const gchar      *key,
                        const gchar      *value)
{
    g_return_val_if_fail (LRG_IS_STEAM_PRESENCE (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamFriends *friends;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return FALSE;

    friends = SteamAPI_SteamFriends_v018 ();
    if (friends == NULL)
        return FALSE;

    return SteamAPI_ISteamFriends_SetRichPresence (friends, key, value ? value : "");
#else
    g_debug ("Steam stub: set presence %s = %s (no-op)", key, value ? value : "(null)");
    return TRUE;
#endif
}

/**
 * lrg_steam_presence_set_status:
 * @self: an #LrgSteamPresence
 * @status: the status string
 *
 * Sets the "status" rich presence key.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_steam_presence_set_status (LrgSteamPresence *self,
                               const gchar      *status)
{
    return lrg_steam_presence_set (self, "status", status);
}

/**
 * lrg_steam_presence_clear:
 * @self: an #LrgSteamPresence
 *
 * Clears all rich presence data.
 */
void
lrg_steam_presence_clear (LrgSteamPresence *self)
{
    g_return_if_fail (LRG_IS_STEAM_PRESENCE (self));

#ifdef LRG_ENABLE_STEAM
    ISteamFriends *friends;

    if (self->client == NULL ||
        !lrg_steam_service_is_available (LRG_STEAM_SERVICE (self->client)))
        return;

    friends = SteamAPI_SteamFriends_v018 ();
    if (friends != NULL)
        SteamAPI_ISteamFriends_ClearRichPresence (friends);
#else
    g_debug ("Steam stub: clear presence (no-op)");
#endif
}
