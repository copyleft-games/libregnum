/* lrg-lsp-client.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLspClient: (stub, being written)
 */

#include "config.h"

#include "lrg-lsp-client.h"

struct _LrgLspClient
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgLspClient, lrg_lsp_client, G_TYPE_OBJECT)

static void
lrg_lsp_client_class_init (LrgLspClientClass *klass)
{
}

static void
lrg_lsp_client_init (LrgLspClient *self)
{
}

LrgLspClient *
lrg_lsp_client_new (void)
{
	return g_object_new (LRG_TYPE_LSP_CLIENT, NULL);
}
