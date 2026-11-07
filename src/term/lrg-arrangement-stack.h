/* lrg-arrangement-stack.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "stack" arrangement (a Rolodex): the slot ordinal is depth.  Ordinal 0 is
 * the front panel at the origin; ordinals 1, 2, ... recede behind it and off to
 * the left, and negative ordinals recede to the right, each step dimmer than
 * the last.  Built for history (back entries to the left, forward entries to
 * the right) and revisions; anything with a "current" and a sequence around
 * it.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ARRANGEMENT_STACK (lrg_arrangement_stack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgArrangementStack, lrg_arrangement_stack, LRG, ARRANGEMENT_STACK, GObject)

LRG_AVAILABLE_IN_ALL
LrgArrangementStack * lrg_arrangement_stack_new (void);

/**
 * lrg_arrangement_stack_set_depth_step:
 * @self: a #LrgArrangementStack
 * @step: world units each depth level recedes (clamped to [0.2, 10]);
 *   defaults to 1.6
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_arrangement_stack_set_depth_step (LrgArrangementStack *self,
                                           gfloat               step);

LRG_AVAILABLE_IN_ALL
gfloat lrg_arrangement_stack_get_depth_step (LrgArrangementStack *self);

G_END_DECLS
