/* lrg-steam-stub.c - Stub Steam implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-steam-stub.h"

/**
 * SECTION:lrg-steam-stub
 * @title: LrgSteamStub
 * @short_description: Stub Steam implementation
 *
 * #LrgSteamStub provides a no-op implementation of #LrgSteamService
 * for use when Steam is not available or when building without
 * Steam support (STEAM=0).
 *
 * This allows games to run without Steam by providing stub
 * implementations that return success but perform no actual
 * Steam operations.
 *
 * All achievement, cloud save, stats, and other Steam operations
 * will succeed but have no effect when using the stub.
 */

struct _LrgSteamStub
{
    GObject  parent_instance;
    guint32  app_id;
    gboolean initialized;
};

static void lrg_steam_stub_steam_service_init (LrgSteamServiceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgSteamStub, lrg_steam_stub, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_STEAM_SERVICE,
                                                lrg_steam_stub_steam_service_init))

static gboolean
lrg_steam_stub_is_available (LrgSteamService *service)
{
    /* Stub is never "available" in the Steam sense */
    return FALSE;
}

static gboolean
lrg_steam_stub_init_impl (LrgSteamService  *service,
                          guint32           app_id,
                          GError          **error)
{
    LrgSteamStub *self = LRG_STEAM_STUB (service);

    self->app_id = app_id;
    self->initialized = TRUE;

    g_debug ("Steam stub initialized with app ID %u (no Steam support)", app_id);

    return TRUE;
}

static void
lrg_steam_stub_shutdown_impl (LrgSteamService *service)
{
    LrgSteamStub *self = LRG_STEAM_STUB (service);

    self->initialized = FALSE;

    g_debug ("Steam stub shutdown");
}

static void
lrg_steam_stub_run_callbacks_impl (LrgSteamService *service)
{
    /* No-op for stub */
}

static void
lrg_steam_stub_steam_service_init (LrgSteamServiceInterface *iface)
{
    iface->is_available  = lrg_steam_stub_is_available;
    iface->init          = lrg_steam_stub_init_impl;
    iface->shutdown      = lrg_steam_stub_shutdown_impl;
    iface->run_callbacks = lrg_steam_stub_run_callbacks_impl;
}

static void
lrg_steam_stub_class_init (LrgSteamStubClass *klass)
{
    /* No additional class initialization needed */
}

static void
lrg_steam_stub_init (LrgSteamStub *self)
{
    self->app_id = 0;
    self->initialized = FALSE;
}

/**
 * lrg_steam_stub_new:
 *
 * Creates a new stub Steam service.
 *
 * Returns: (transfer full): A new #LrgSteamStub
 */
LrgSteamStub *
lrg_steam_stub_new (void)
{
    return g_object_new (LRG_TYPE_STEAM_STUB, NULL);
}
