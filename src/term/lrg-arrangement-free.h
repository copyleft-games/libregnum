/* lrg-arrangement-free.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "free" arrangement: panels are spread in a loose row as an initial
 * placement and then governed entirely by the user (grab/move + pin via
 * #Lrg3DSurface).  Pinned panels keep their place; un-pinned panels fall back to
 * this default spread.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ARRANGEMENT_FREE (lrg_arrangement_free_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgArrangementFree, lrg_arrangement_free, LRG, ARRANGEMENT_FREE, GObject)

LRG_AVAILABLE_IN_ALL
LrgArrangementFree * lrg_arrangement_free_new (void);

G_END_DECLS
