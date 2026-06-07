/* lrg-node-visual.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tagged visual/content payload for an editor node.
 *
 * LrgNodeVisual describes what an #LrgNode renders or represents. It is a
 * tagged record discriminated by #LrgNodeVisualKind:
 *
 *  - PRIMITIVE: a built-in #LrgPrimitiveType with an optional #LrgMaterial3D
 *  - MESH_ASSET / SPRITE / TILEMAP / AUDIO_EMITTER / PREFAB_INSTANCE: an asset
 *    reference (path or guid) plus kind-specific params
 *  - LIGHT / CAMERA: kind-specific params only
 *
 * Kind-specific scalar settings (light intensity, camera fov, audio volume,
 * sprite source rectangle, etc.) live in a typed parameter bag, mirroring the
 * approach used by #LrgSceneObject so that primitive rendering can be reused.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "../scene/lrg-material3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_NODE_VISUAL (lrg_node_visual_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNodeVisual, lrg_node_visual, LRG, NODE_VISUAL, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_node_visual_new:
 * @kind: the visual kind
 *
 * Creates a new #LrgNodeVisual of the given kind.
 *
 * Returns: (transfer full): a new #LrgNodeVisual
 */
LRG_AVAILABLE_IN_ALL
LrgNodeVisual * lrg_node_visual_new (LrgNodeVisualKind kind);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_node_visual_get_kind:
 * @self: an #LrgNodeVisual
 *
 * Gets the visual kind.
 *
 * Returns: the #LrgNodeVisualKind
 */
LRG_AVAILABLE_IN_ALL
LrgNodeVisualKind lrg_node_visual_get_kind (LrgNodeVisual *self);

/**
 * lrg_node_visual_set_kind:
 * @self: an #LrgNodeVisual
 * @kind: the visual kind
 *
 * Sets the visual kind.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_visual_set_kind (LrgNodeVisual     *self,
                               LrgNodeVisualKind  kind);

/**
 * lrg_node_visual_get_primitive:
 * @self: an #LrgNodeVisual
 *
 * Gets the primitive type (meaningful for %LRG_NODE_VISUAL_PRIMITIVE).
 *
 * Returns: the #LrgPrimitiveType
 */
LRG_AVAILABLE_IN_ALL
LrgPrimitiveType lrg_node_visual_get_primitive (LrgNodeVisual *self);

/**
 * lrg_node_visual_set_primitive:
 * @self: an #LrgNodeVisual
 * @primitive: the primitive type
 *
 * Sets the primitive type.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_visual_set_primitive (LrgNodeVisual    *self,
                                    LrgPrimitiveType  primitive);

/**
 * lrg_node_visual_get_asset:
 * @self: an #LrgNodeVisual
 *
 * Gets the asset reference (path or guid) for asset-backed kinds.
 *
 * Returns: (transfer none) (nullable): the asset reference
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_node_visual_get_asset (LrgNodeVisual *self);

/**
 * lrg_node_visual_set_asset:
 * @self: an #LrgNodeVisual
 * @asset: (nullable): the asset reference (path or guid)
 *
 * Sets the asset reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_visual_set_asset (LrgNodeVisual *self,
                                const gchar   *asset);

/**
 * lrg_node_visual_get_material:
 * @self: an #LrgNodeVisual
 *
 * Gets the material, if any (used by primitive and mesh kinds).
 *
 * Returns: (transfer none) (nullable): the #LrgMaterial3D
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial3D * lrg_node_visual_get_material (LrgNodeVisual *self);

/**
 * lrg_node_visual_set_material:
 * @self: an #LrgNodeVisual
 * @material: (transfer none) (nullable): the material
 *
 * Sets the material. A reference is taken on @material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_visual_set_material (LrgNodeVisual *self,
                                   LrgMaterial3D *material);

/* ==========================================================================
 * Kind-specific Parameters
 * ========================================================================== */

/**
 * lrg_node_visual_set_param:
 * @self: an #LrgNodeVisual
 * @name: the parameter name
 * @value: the value to store (copied)
 *
 * Stores a typed kind-specific parameter, replacing any existing value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_visual_set_param (LrgNodeVisual *self,
                                const gchar   *name,
                                const GValue  *value);

/**
 * lrg_node_visual_get_param:
 * @self: an #LrgNodeVisual
 * @name: the parameter name
 *
 * Gets a stored kind-specific parameter.
 *
 * Returns: (transfer none) (nullable): the stored #GValue, owned by @self
 */
LRG_AVAILABLE_IN_ALL
const GValue * lrg_node_visual_get_param (LrgNodeVisual *self,
                                          const gchar   *name);

/**
 * lrg_node_visual_set_param_double:
 * @self: an #LrgNodeVisual
 * @name: the parameter name
 * @value: the value
 *
 * Convenience setter for a double parameter.
 */
LRG_AVAILABLE_IN_ALL
void lrg_node_visual_set_param_double (LrgNodeVisual *self,
                                       const gchar   *name,
                                       gdouble        value);

/**
 * lrg_node_visual_get_param_double:
 * @self: an #LrgNodeVisual
 * @name: the parameter name
 * @default_value: value to return if not set or wrong type
 *
 * Convenience getter for a double parameter.
 *
 * Returns: the parameter value, or @default_value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_node_visual_get_param_double (LrgNodeVisual *self,
                                          const gchar   *name,
                                          gdouble        default_value);

/**
 * lrg_node_visual_get_param_names:
 * @self: an #LrgNodeVisual
 *
 * Gets the names of all set parameters.
 *
 * Returns: (transfer container) (element-type utf8): list of parameter names
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_node_visual_get_param_names (LrgNodeVisual *self);

G_END_DECLS
