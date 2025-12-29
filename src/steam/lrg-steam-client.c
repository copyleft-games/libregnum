/* lrg-steam-client.c - Steam client implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-steam-client.h"
#include "lrg-steam-types.h"

/**
 * SECTION:lrg-steam-client
 * @title: LrgSteamClient
 * @short_description: Steam client and initialization
 *
 * #LrgSteamClient provides Steam client initialization and basic
 * user information. It implements the #LrgSteamService interface.
 *
 * When built with STEAM=1, it uses the actual Steam SDK flat API.
 * When built without Steam support, it provides stub implementations
 * that allow games to run without Steam.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * g_autoptr(LrgSteamClient) client = lrg_steam_client_new ();
 * g_autoptr(GError) error = NULL;
 *
 * if (!lrg_steam_service_init (LRG_STEAM_SERVICE (client), 480, &error))
 * {
 *     g_warning ("Steam init failed: %s", error->message);
 * }
 *
 * // In game loop
 * lrg_steam_service_run_callbacks (LRG_STEAM_SERVICE (client));
 *
 * // On shutdown
 * lrg_steam_service_shutdown (LRG_STEAM_SERVICE (client));
 * ]|
 */

struct _LrgSteamClient
{
    GObject  parent_instance;

    guint32  app_id;
    gboolean initialized;

#ifdef LRG_ENABLE_STEAM
    ISteamUser        *steam_user;
    ISteamFriends     *steam_friends;
    ISteamUtils       *steam_utils;
    ISteamUserStats   *steam_user_stats;
    ISteamRemoteStorage *steam_remote_storage;
#endif
};

static void lrg_steam_client_steam_service_init (LrgSteamServiceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgSteamClient, lrg_steam_client, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_STEAM_SERVICE,
                                                lrg_steam_client_steam_service_init))

G_DEFINE_QUARK (lrg-steam-client-error-quark, lrg_steam_client_error)

static gboolean
lrg_steam_client_is_available (LrgSteamService *service)
{
#ifdef LRG_ENABLE_STEAM
    LrgSteamClient *self = LRG_STEAM_CLIENT (service);
    return self->initialized && self->steam_user != NULL;
#else
    return FALSE;
#endif
}

static gboolean
lrg_steam_client_init_impl (LrgSteamService  *service,
                            guint32           app_id,
                            GError          **error)
{
    LrgSteamClient *self = LRG_STEAM_CLIENT (service);

#ifdef LRG_ENABLE_STEAM
    SteamErrMsg err_msg;
    ESteamAPIInitResult result;

    result = SteamAPI_InitFlat (&err_msg);

    if (result != k_ESteamAPIInitResult_OK)
    {
        switch (result)
        {
        case k_ESteamAPIInitResult_NoSteamClient:
            g_set_error (error,
                         LRG_STEAM_CLIENT_ERROR,
                         LRG_STEAM_CLIENT_ERROR_NO_STEAM_CLIENT,
                         "Steam client is not running: %s", err_msg);
            break;
        case k_ESteamAPIInitResult_VersionMismatch:
            g_set_error (error,
                         LRG_STEAM_CLIENT_ERROR,
                         LRG_STEAM_CLIENT_ERROR_VERSION_MISMATCH,
                         "Steam SDK version mismatch: %s", err_msg);
            break;
        default:
            g_set_error (error,
                         LRG_STEAM_CLIENT_ERROR,
                         LRG_STEAM_CLIENT_ERROR_INIT_FAILED,
                         "Steam initialization failed: %s", err_msg);
            break;
        }
        return FALSE;
    }

    self->app_id = app_id;
    self->initialized = TRUE;

    /* Get interface pointers */
    self->steam_user = SteamAPI_SteamUser_v023 ();
    self->steam_friends = SteamAPI_SteamFriends_v018 ();
    self->steam_utils = SteamAPI_SteamUtils_v010 ();
    self->steam_user_stats = SteamAPI_SteamUserStats_v013 ();
    self->steam_remote_storage = SteamAPI_SteamRemoteStorage_v016 ();

    /* Request current stats so we can access achievements */
    if (self->steam_user_stats != NULL)
        SteamAPI_ISteamUserStats_RequestCurrentStats (self->steam_user_stats);

    g_debug ("Steam initialized successfully for app ID %u", app_id);
    return TRUE;
#else
    (void)self;
    (void)app_id;
    g_set_error (error,
                 LRG_STEAM_CLIENT_ERROR,
                 LRG_STEAM_CLIENT_ERROR_NOT_SUPPORTED,
                 "Steam support not compiled (build with STEAM=1)");
    return FALSE;
#endif
}

static void
lrg_steam_client_shutdown_impl (LrgSteamService *service)
{
    LrgSteamClient *self = LRG_STEAM_CLIENT (service);

#ifdef LRG_ENABLE_STEAM
    if (self->initialized)
    {
        SteamAPI_Shutdown ();
        self->initialized = FALSE;
        self->steam_user = NULL;
        self->steam_friends = NULL;
        self->steam_utils = NULL;
        self->steam_user_stats = NULL;
        self->steam_remote_storage = NULL;
        g_debug ("Steam shutdown");
    }
#else
    self->initialized = FALSE;
#endif
}

static void
lrg_steam_client_run_callbacks_impl (LrgSteamService *service)
{
#ifdef LRG_ENABLE_STEAM
    LrgSteamClient *self = LRG_STEAM_CLIENT (service);

    if (self->initialized)
        SteamAPI_RunCallbacks ();
#endif
}

static void
lrg_steam_client_steam_service_init (LrgSteamServiceInterface *iface)
{
    iface->is_available  = lrg_steam_client_is_available;
    iface->init          = lrg_steam_client_init_impl;
    iface->shutdown      = lrg_steam_client_shutdown_impl;
    iface->run_callbacks = lrg_steam_client_run_callbacks_impl;
}

static void
lrg_steam_client_dispose (GObject *object)
{
    LrgSteamClient *self = LRG_STEAM_CLIENT (object);

    /* Ensure Steam is shut down */
    if (self->initialized)
        lrg_steam_client_shutdown_impl (LRG_STEAM_SERVICE (self));

    G_OBJECT_CLASS (lrg_steam_client_parent_class)->dispose (object);
}

static void
lrg_steam_client_class_init (LrgSteamClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_steam_client_dispose;
}

static void
lrg_steam_client_init (LrgSteamClient *self)
{
    self->app_id = 0;
    self->initialized = FALSE;

#ifdef LRG_ENABLE_STEAM
    self->steam_user = NULL;
    self->steam_friends = NULL;
    self->steam_utils = NULL;
    self->steam_user_stats = NULL;
    self->steam_remote_storage = NULL;
#endif
}

/**
 * lrg_steam_client_new:
 *
 * Creates a new Steam client.
 *
 * Returns: (transfer full): A new #LrgSteamClient
 */
LrgSteamClient *
lrg_steam_client_new (void)
{
    return g_object_new (LRG_TYPE_STEAM_CLIENT, NULL);
}

/**
 * lrg_steam_client_is_logged_on:
 * @self: an #LrgSteamClient
 *
 * Checks if the current user is logged into Steam.
 *
 * Returns: %TRUE if logged in
 */
gboolean
lrg_steam_client_is_logged_on (LrgSteamClient *self)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLIENT (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    if (self->initialized && self->steam_user != NULL)
        return SteamAPI_ISteamUser_BLoggedOn (self->steam_user);
#endif

    return FALSE;
}

/**
 * lrg_steam_client_get_steam_id:
 * @self: an #LrgSteamClient
 *
 * Gets the current user's Steam ID.
 *
 * Returns: The 64-bit Steam ID, or 0 if not available
 */
guint64
lrg_steam_client_get_steam_id (LrgSteamClient *self)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLIENT (self), 0);

#ifdef LRG_ENABLE_STEAM
    if (self->initialized && self->steam_user != NULL)
        return SteamAPI_ISteamUser_GetSteamID (self->steam_user);
#endif

    return 0;
}

/**
 * lrg_steam_client_get_persona_name:
 * @self: an #LrgSteamClient
 *
 * Gets the current user's display name.
 *
 * Returns: (transfer none) (nullable): The persona name
 */
const gchar *
lrg_steam_client_get_persona_name (LrgSteamClient *self)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLIENT (self), NULL);

#ifdef LRG_ENABLE_STEAM
    if (self->initialized && self->steam_friends != NULL)
        return SteamAPI_ISteamFriends_GetPersonaName (self->steam_friends);
#endif

    return NULL;
}

/**
 * lrg_steam_client_get_app_id:
 * @self: an #LrgSteamClient
 *
 * Gets the application's Steam App ID.
 *
 * Returns: The app ID
 */
guint32
lrg_steam_client_get_app_id (LrgSteamClient *self)
{
    g_return_val_if_fail (LRG_IS_STEAM_CLIENT (self), 0);

#ifdef LRG_ENABLE_STEAM
    if (self->initialized && self->steam_utils != NULL)
        return SteamAPI_ISteamUtils_GetAppID (self->steam_utils);
#endif

    return self->app_id;
}
