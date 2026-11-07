/* lrg-skeleton2d-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2DComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_SKELETON2D_COMPONENT (lrg_skeleton2d_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkeleton2DComponent, lrg_skeleton2d_component, LRG, SKELETON2D_COMPONENT, LrgComponent)

/**
 * lrg_skeleton2d_component_new:
 *
 * Returns: (transfer full): a new #LrgSkeleton2DComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2DComponent * lrg_skeleton2d_component_new (void);

G_END_DECLS
