/* lrg-post-stack.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPostStack: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_POST_STACK (lrg_post_stack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPostStack, lrg_post_stack, LRG, POST_STACK, GObject)

/**
 * lrg_post_stack_new:
 *
 * Returns: (transfer full): a new #LrgPostStack
 */
LRG_AVAILABLE_IN_ALL
LrgPostStack * lrg_post_stack_new (void);

G_END_DECLS
