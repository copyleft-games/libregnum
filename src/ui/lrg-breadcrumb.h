/* lrg-breadcrumb.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBreadcrumb: A path of clickable segments that collapses to an ellipsis when narrow.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_BREADCRUMB (lrg_breadcrumb_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBreadcrumb, lrg_breadcrumb, LRG, BREADCRUMB, LrgWidget)

/**
 * lrg_breadcrumb_new:
 *
 * Returns: (transfer full): a new #LrgBreadcrumb
 */
LRG_AVAILABLE_IN_ALL
LrgBreadcrumb * lrg_breadcrumb_new (void);

G_END_DECLS
