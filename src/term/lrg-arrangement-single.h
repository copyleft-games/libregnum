/* lrg-arrangement-single.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "single-panel" arrangement: the whole captured frame on one floating
 * panel, centred and facing the camera.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ARRANGEMENT_SINGLE (lrg_arrangement_single_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgArrangementSingle, lrg_arrangement_single, LRG, ARRANGEMENT_SINGLE, GObject)

LRG_AVAILABLE_IN_ALL
LrgArrangementSingle * lrg_arrangement_single_new (void);

G_END_DECLS
