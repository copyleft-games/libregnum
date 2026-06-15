/* lrg-arrangement-per-window.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "per-window" arrangement: each Emacs window becomes its own panel, placed
 * to mirror the frame's window tree (the workshop default).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ARRANGEMENT_PER_WINDOW (lrg_arrangement_per_window_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgArrangementPerWindow, lrg_arrangement_per_window, LRG, ARRANGEMENT_PER_WINDOW, GObject)

LRG_AVAILABLE_IN_ALL
LrgArrangementPerWindow * lrg_arrangement_per_window_new (void);

G_END_DECLS
