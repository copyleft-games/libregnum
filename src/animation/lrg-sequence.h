/* lrg-sequence.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequence: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SEQUENCE (lrg_sequence_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSequence, lrg_sequence, LRG, SEQUENCE, GObject)

/**
 * lrg_sequence_new:
 *
 * Returns: (transfer full): a new #LrgSequence
 */
LRG_AVAILABLE_IN_ALL
LrgSequence * lrg_sequence_new (void);

G_END_DECLS
