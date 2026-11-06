/* lrg-dialog.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDialog: A modal sheet with a title, content and response buttons.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_DIALOG (lrg_dialog_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDialog, lrg_dialog, LRG, DIALOG, LrgContainer)

/**
 * lrg_dialog_new:
 *
 * Returns: (transfer full): a new #LrgDialog
 */
LRG_AVAILABLE_IN_ALL
LrgDialog * lrg_dialog_new (void);

G_END_DECLS
