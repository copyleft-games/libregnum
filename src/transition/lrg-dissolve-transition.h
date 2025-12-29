/* lrg-dissolve-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Noise-based dissolve transition.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-transition.h"

G_BEGIN_DECLS

#define LRG_TYPE_DISSOLVE_TRANSITION (lrg_dissolve_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDissolveTransition, lrg_dissolve_transition, LRG, DISSOLVE_TRANSITION, LrgTransition)

/**
 * lrg_dissolve_transition_new:
 *
 * Creates a new dissolve transition with default settings.
 *
 * Returns: (transfer full): A new #LrgDissolveTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDissolveTransition * lrg_dissolve_transition_new             (void);

/**
 * lrg_dissolve_transition_get_noise_scale:
 * @self: A #LrgDissolveTransition
 *
 * Gets the noise scale (size of dissolve pattern).
 *
 * Returns: Noise scale value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_dissolve_transition_get_noise_scale     (LrgDissolveTransition *self);

/**
 * lrg_dissolve_transition_set_noise_scale:
 * @self: A #LrgDissolveTransition
 * @scale: Noise scale (higher = larger dissolve patterns)
 *
 * Sets the noise scale.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_dissolve_transition_set_noise_scale     (LrgDissolveTransition *self,
                                                                 gfloat                 scale);

/**
 * lrg_dissolve_transition_get_edge_width:
 * @self: A #LrgDissolveTransition
 *
 * Gets the edge width (glow/color border around dissolving edges).
 *
 * Returns: Edge width value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_dissolve_transition_get_edge_width      (LrgDissolveTransition *self);

/**
 * lrg_dissolve_transition_set_edge_width:
 * @self: A #LrgDissolveTransition
 * @width: Edge width (0.0 = no edge, larger = wider edge)
 *
 * Sets the edge width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_dissolve_transition_set_edge_width      (LrgDissolveTransition *self,
                                                                 gfloat                 width);

/**
 * lrg_dissolve_transition_get_edge_color:
 * @self: A #LrgDissolveTransition
 * @r: (out) (nullable): Return location for red component
 * @g: (out) (nullable): Return location for green component
 * @b: (out) (nullable): Return location for blue component
 *
 * Gets the edge glow color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_dissolve_transition_get_edge_color      (LrgDissolveTransition *self,
                                                                 guint8                *r,
                                                                 guint8                *g,
                                                                 guint8                *b);

/**
 * lrg_dissolve_transition_set_edge_color:
 * @self: A #LrgDissolveTransition
 * @r: Red component (0-255)
 * @g: Green component (0-255)
 * @b: Blue component (0-255)
 *
 * Sets the edge glow color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_dissolve_transition_set_edge_color      (LrgDissolveTransition *self,
                                                                 guint8                 r,
                                                                 guint8                 g,
                                                                 guint8                 b);

/**
 * lrg_dissolve_transition_get_seed:
 * @self: A #LrgDissolveTransition
 *
 * Gets the noise seed for reproducible patterns.
 *
 * Returns: The seed value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint32             lrg_dissolve_transition_get_seed            (LrgDissolveTransition *self);

/**
 * lrg_dissolve_transition_set_seed:
 * @self: A #LrgDissolveTransition
 * @seed: Noise seed (0 = random on each start)
 *
 * Sets the noise seed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_dissolve_transition_set_seed            (LrgDissolveTransition *self,
                                                                 guint32                seed);

G_END_DECLS
