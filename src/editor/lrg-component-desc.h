/* lrg-component-desc.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Serializable description of a component attached to an editor node.
 *
 * LrgComponentDesc is a data-only, serializable record of a component to
 * instantiate on a game object when a level is played: a registry type name
 * plus a bag of typed property values. It does NOT hold a live #LrgComponent;
 * the live component is created from this description by lrg_level_instantiate()
 * via the engine #LrgRegistry and applied to the owning #LrgGameObject.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_COMPONENT_DESC (lrg_component_desc_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgComponentDesc, lrg_component_desc, LRG, COMPONENT_DESC, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_component_desc_new:
 * @type_name: the registry type name of the component (e.g. "health")
 *
 * Creates a new, empty #LrgComponentDesc for the given registry type name.
 *
 * Returns: (transfer full): a new #LrgComponentDesc
 */
LRG_AVAILABLE_IN_ALL
LrgComponentDesc * lrg_component_desc_new (const gchar *type_name);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_component_desc_get_type_name:
 * @self: an #LrgComponentDesc
 *
 * Gets the registry type name of the component.
 *
 * Returns: (transfer none) (nullable): the type name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_component_desc_get_type_name (LrgComponentDesc *self);

/**
 * lrg_component_desc_set_type_name:
 * @self: an #LrgComponentDesc
 * @type_name: (nullable): the registry type name
 *
 * Sets the registry type name of the component.
 */
LRG_AVAILABLE_IN_ALL
void lrg_component_desc_set_type_name (LrgComponentDesc *self,
                                       const gchar      *type_name);

/* ==========================================================================
 * Property Values
 * ========================================================================== */

/**
 * lrg_component_desc_set_value:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: the value to store (copied)
 *
 * Stores a typed property value, replacing any existing value for @name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_component_desc_set_value (LrgComponentDesc *self,
                                   const gchar      *name,
                                   const GValue     *value);

/**
 * lrg_component_desc_get_value:
 * @self: an #LrgComponentDesc
 * @name: the property name
 *
 * Gets the stored value for @name, if any.
 *
 * Returns: (transfer none) (nullable): the stored #GValue, owned by @self
 */
LRG_AVAILABLE_IN_ALL
const GValue * lrg_component_desc_get_value (LrgComponentDesc *self,
                                             const gchar      *name);

/**
 * lrg_component_desc_set_string:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: (nullable): the string value
 *
 * Convenience setter for a string property value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_component_desc_set_string (LrgComponentDesc *self,
                                    const gchar      *name,
                                    const gchar      *value);

/**
 * lrg_component_desc_set_int:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: the integer value
 *
 * Convenience setter for an integer property value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_component_desc_set_int (LrgComponentDesc *self,
                                 const gchar      *name,
                                 gint              value);

/**
 * lrg_component_desc_set_double:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: the double value
 *
 * Convenience setter for a double property value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_component_desc_set_double (LrgComponentDesc *self,
                                    const gchar      *name,
                                    gdouble           value);

/**
 * lrg_component_desc_set_boolean:
 * @self: an #LrgComponentDesc
 * @name: the property name
 * @value: the boolean value
 *
 * Convenience setter for a boolean property value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_component_desc_set_boolean (LrgComponentDesc *self,
                                     const gchar      *name,
                                     gboolean          value);

/**
 * lrg_component_desc_has:
 * @self: an #LrgComponentDesc
 * @name: the property name
 *
 * Checks whether a property value is set for @name.
 *
 * Returns: %TRUE if a value exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_component_desc_has (LrgComponentDesc *self,
                                 const gchar      *name);

/**
 * lrg_component_desc_get_keys:
 * @self: an #LrgComponentDesc
 *
 * Gets the names of all set property values.
 *
 * Returns: (transfer container) (element-type utf8): list of property names
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_component_desc_get_keys (LrgComponentDesc *self);

G_END_DECLS
