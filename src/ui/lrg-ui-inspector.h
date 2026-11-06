/* lrg-ui-inspector.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUIInspector: Inspects a canvas: widget tree, geometry, state, looks and timings, with pick mode.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_UI_INSPECTOR (lrg_ui_inspector_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUIInspector, lrg_ui_inspector, LRG, UI_INSPECTOR, GObject)

/**
 * lrg_ui_inspector_new:
 *
 * Returns: (transfer full): a new #LrgUIInspector
 */
LRG_AVAILABLE_IN_ALL
LrgUIInspector * lrg_ui_inspector_new (void);

G_END_DECLS
