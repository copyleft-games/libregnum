/* lrg-dialog.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDialog: A modal sheet with a title, content and response buttons.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-dialog.h"
#include "../lrg-log.h"

struct _LrgDialog
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgDialog, lrg_dialog, LRG_TYPE_CONTAINER)

static void
lrg_dialog_class_init (LrgDialogClass *klass)
{
}

static void
lrg_dialog_init (LrgDialog *self)
{
}

LrgDialog *
lrg_dialog_new (void)
{
	return g_object_new (LRG_TYPE_DIALOG, NULL);
}
