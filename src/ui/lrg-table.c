/* lrg-table.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTable: A virtualized data table with resizable, sortable columns.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-table.h"
#include "../lrg-log.h"

struct _LrgTable
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTable, lrg_table, LRG_TYPE_CONTAINER)

static void
lrg_table_class_init (LrgTableClass *klass)
{
}

static void
lrg_table_init (LrgTable *self)
{
}

LrgTable *
lrg_table_new (void)
{
	return g_object_new (LRG_TYPE_TABLE, NULL);
}
