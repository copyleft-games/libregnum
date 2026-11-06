/* lrg-expander.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgExpander: A header that reveals or hides its content with a spring.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_EXPANDER (lrg_expander_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgExpander, lrg_expander, LRG, EXPANDER, LrgContainer)

/**
 * lrg_expander_new:
 *
 * Returns: (transfer full): a new #LrgExpander
 */
LRG_AVAILABLE_IN_ALL
LrgExpander * lrg_expander_new (void);

G_END_DECLS
