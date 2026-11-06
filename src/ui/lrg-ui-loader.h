/* lrg-ui-loader.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUILoader: Builds widget trees from YAML documents with components and hot reload.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_UI_LOADER (lrg_ui_loader_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUILoader, lrg_ui_loader, LRG, UI_LOADER, GObject)

/**
 * lrg_ui_loader_new:
 *
 * Returns: (transfer full): a new #LrgUILoader
 */
LRG_AVAILABLE_IN_ALL
LrgUILoader * lrg_ui_loader_new (void);

G_END_DECLS
