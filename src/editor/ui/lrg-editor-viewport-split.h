/* lrg-editor-viewport-split.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewportSplit: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ui/lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_VIEWPORT_SPLIT (lrg_editor_viewport_split_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorViewportSplit, lrg_editor_viewport_split, LRG, EDITOR_VIEWPORT_SPLIT, LrgContainer)

/**
 * lrg_editor_viewport_split_new:
 *
 * Returns: (transfer full): a new #LrgEditorViewportSplit
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportSplit * lrg_editor_viewport_split_new (void);

G_END_DECLS
