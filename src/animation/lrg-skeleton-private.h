/* lrg-skeleton-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal LrgSkeleton API. Not installed, not introspected.
 */

#pragma once

#include "lrg-skeleton.h"

G_BEGIN_DECLS

/*
 * lrg_skeleton_get_generation:
 * @self: an #LrgSkeleton
 *
 * A counter bumped whenever a bone is added or removed. Callers that cache
 * bone indices resolved from names (LrgAnimator's track bindings) compare
 * it to know when to resolve again.
 *
 * Returns: the current generation
 */
G_GNUC_INTERNAL
guint lrg_skeleton_get_generation (LrgSkeleton *self);

G_END_DECLS
