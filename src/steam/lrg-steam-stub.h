/* lrg-steam-stub.h - Stub Steam implementation
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_STEAM_STUB_H
#define LRG_STEAM_STUB_H

#include <glib-object.h>
#include "lrg-steam-service.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_STEAM_STUB (lrg_steam_stub_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSteamStub, lrg_steam_stub, LRG, STEAM_STUB, GObject)

/**
 * lrg_steam_stub_new:
 *
 * Creates a new stub Steam service that provides no-op implementations
 * of all Steam functionality. This is used when Steam is not available
 * or when building without Steam support.
 *
 * The stub will:
 * - Return %FALSE for is_available()
 * - Return %TRUE for init() (allowing games to run without Steam)
 * - Do nothing for shutdown() and run_callbacks()
 *
 * Returns: (transfer full): A new #LrgSteamStub
 */
LRG_AVAILABLE_IN_ALL
LrgSteamStub *
lrg_steam_stub_new (void);

G_END_DECLS

#endif /* LRG_STEAM_STUB_H */
