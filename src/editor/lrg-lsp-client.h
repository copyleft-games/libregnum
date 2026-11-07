/* lrg-lsp-client.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLspClient: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_LSP_CLIENT (lrg_lsp_client_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLspClient, lrg_lsp_client, LRG, LSP_CLIENT, GObject)

/**
 * lrg_lsp_client_new:
 *
 * Returns: (transfer full): a new #LrgLspClient
 */
LRG_AVAILABLE_IN_ALL
LrgLspClient * lrg_lsp_client_new (void);

G_END_DECLS
