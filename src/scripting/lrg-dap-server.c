/* lrg-dap-server.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDapServer: (stub, being written)
 */

#include "config.h"

#include "lrg-dap-server.h"

struct _LrgDapServer
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgDapServer, lrg_dap_server, G_TYPE_OBJECT)

static void
lrg_dap_server_class_init (LrgDapServerClass *klass)
{
}

static void
lrg_dap_server_init (LrgDapServer *self)
{
}

LrgDapServer *
lrg_dap_server_new (void)
{
	return g_object_new (LRG_TYPE_DAP_SERVER, NULL);
}
