/* lrg-inspector.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Debug inspector for runtime entity/component browsing.
 *
 * The inspector provides runtime introspection of game objects,
 * their components, and GObject properties for debugging.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_INSPECTOR (lrg_inspector_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgInspector, lrg_inspector, LRG, INSPECTOR, GObject)

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

/**
 * lrg_inspector_get_default:
 *
 * Gets the default inspector instance.
 *
 * Returns: (transfer none): the default #LrgInspector
 */
LRG_AVAILABLE_IN_ALL
LrgInspector *    lrg_inspector_get_default      (void);

/**
 * lrg_inspector_new:
 *
 * Creates a new inspector instance.
 *
 * Returns: (transfer full): a new #LrgInspector
 */
LRG_AVAILABLE_IN_ALL
LrgInspector *    lrg_inspector_new              (void);

/* ==========================================================================
 * Visibility Control
 * ========================================================================== */

/**
 * lrg_inspector_is_visible:
 * @self: a #LrgInspector
 *
 * Checks if the inspector is visible.
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_inspector_is_visible       (LrgInspector *self);

/**
 * lrg_inspector_set_visible:
 * @self: a #LrgInspector
 * @visible: whether to show the inspector
 *
 * Shows or hides the inspector.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_inspector_set_visible      (LrgInspector *self,
                                                  gboolean      visible);

/**
 * lrg_inspector_toggle:
 * @self: a #LrgInspector
 *
 * Toggles inspector visibility.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_inspector_toggle           (LrgInspector *self);

/* ==========================================================================
 * World Management
 * ========================================================================== */

/**
 * lrg_inspector_get_world:
 * @self: a #LrgInspector
 *
 * Gets the world being inspected.
 *
 * Returns: (transfer none) (nullable): the inspected #LrgWorld
 */
LRG_AVAILABLE_IN_ALL
LrgWorld *        lrg_inspector_get_world        (LrgInspector *self);

/**
 * lrg_inspector_set_world:
 * @self: a #LrgInspector
 * @world: (nullable): the world to inspect
 *
 * Sets the world to inspect. Uses a weak reference.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_inspector_set_world        (LrgInspector *self,
                                                  LrgWorld     *world);

/* ==========================================================================
 * Entity Browsing
 * ========================================================================== */

/**
 * lrg_inspector_get_objects:
 * @self: a #LrgInspector
 *
 * Gets all game objects from the inspected world.
 *
 * Returns: (transfer container) (element-type LrgGameObject) (nullable):
 *          list of game objects, or %NULL if no world set
 */
LRG_AVAILABLE_IN_ALL
GList *           lrg_inspector_get_objects      (LrgInspector *self);

/**
 * lrg_inspector_get_object_count:
 * @self: a #LrgInspector
 *
 * Gets the number of objects in the inspected world.
 *
 * Returns: number of objects, or 0 if no world set
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_inspector_get_object_count (LrgInspector *self);

/**
 * lrg_inspector_select_object:
 * @self: a #LrgInspector
 * @object: (nullable): the object to select
 *
 * Selects an object for inspection. Clears component selection.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_inspector_select_object    (LrgInspector  *self,
                                                  LrgGameObject *object);

/**
 * lrg_inspector_select_object_at:
 * @self: a #LrgInspector
 * @index: the index of the object to select
 *
 * Selects an object by index in the world's object list.
 *
 * Returns: %TRUE if object was selected
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_inspector_select_object_at (LrgInspector *self,
                                                  guint         index);

/**
 * lrg_inspector_get_selected_object:
 * @self: a #LrgInspector
 *
 * Gets the currently selected object.
 *
 * Returns: (transfer none) (nullable): the selected #LrgGameObject
 */
LRG_AVAILABLE_IN_ALL
LrgGameObject *   lrg_inspector_get_selected_object (LrgInspector *self);

/**
 * lrg_inspector_clear_selection:
 * @self: a #LrgInspector
 *
 * Clears object and component selection.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_inspector_clear_selection  (LrgInspector *self);

/* ==========================================================================
 * Component Browsing
 * ========================================================================== */

/**
 * lrg_inspector_get_components:
 * @self: a #LrgInspector
 *
 * Gets all components from the selected object.
 *
 * Returns: (transfer container) (element-type LrgComponent) (nullable):
 *          list of components, or %NULL if no object selected
 */
LRG_AVAILABLE_IN_ALL
GList *           lrg_inspector_get_components   (LrgInspector *self);

/**
 * lrg_inspector_get_component_count:
 * @self: a #LrgInspector
 *
 * Gets the number of components on the selected object.
 *
 * Returns: number of components, or 0 if no object selected
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_inspector_get_component_count (LrgInspector *self);

/**
 * lrg_inspector_select_component:
 * @self: a #LrgInspector
 * @component: (nullable): the component to select
 *
 * Selects a component for inspection.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_inspector_select_component (LrgInspector *self,
                                                  LrgComponent *component);

/**
 * lrg_inspector_select_component_at:
 * @self: a #LrgInspector
 * @index: the index of the component to select
 *
 * Selects a component by index in the selected object's component list.
 *
 * Returns: %TRUE if component was selected
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_inspector_select_component_at (LrgInspector *self,
                                                     guint         index);

/**
 * lrg_inspector_get_selected_component:
 * @self: a #LrgInspector
 *
 * Gets the currently selected component.
 *
 * Returns: (transfer none) (nullable): the selected #LrgComponent
 */
LRG_AVAILABLE_IN_ALL
LrgComponent *    lrg_inspector_get_selected_component (LrgInspector *self);

/* ==========================================================================
 * Property Introspection
 * ========================================================================== */

/**
 * lrg_inspector_get_properties:
 * @self: a #LrgInspector
 * @object: a #GObject to inspect
 * @n_properties: (out): return location for property count
 *
 * Gets the property specs for a GObject.
 *
 * Returns: (array length=n_properties) (transfer container):
 *          array of #GParamSpec, free with g_free()
 */
LRG_AVAILABLE_IN_ALL
GParamSpec **     lrg_inspector_get_properties   (LrgInspector *self,
                                                  GObject      *object,
                                                  guint        *n_properties);

/**
 * lrg_inspector_get_property_value:
 * @self: a #LrgInspector
 * @object: a #GObject
 * @property_name: the property name
 * @value: (out caller-allocates): return location for value
 *
 * Gets a property value from an object.
 *
 * Returns: %TRUE if property was read successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_inspector_get_property_value (LrgInspector *self,
                                                    GObject      *object,
                                                    const gchar  *property_name,
                                                    GValue       *value);

/**
 * lrg_inspector_set_property_value:
 * @self: a #LrgInspector
 * @object: a #GObject
 * @property_name: the property name
 * @value: the value to set
 *
 * Sets a property value on an object.
 *
 * Returns: %TRUE if property was set successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_inspector_set_property_value (LrgInspector *self,
                                                    GObject      *object,
                                                    const gchar  *property_name,
                                                    const GValue *value);

/**
 * lrg_inspector_get_property_string:
 * @self: a #LrgInspector
 * @object: a #GObject
 * @property_name: the property name
 *
 * Gets a property value formatted as a string.
 *
 * Returns: (transfer full) (nullable): formatted value string
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_inspector_get_property_string (LrgInspector *self,
                                                     GObject      *object,
                                                     const gchar  *property_name);

/* ==========================================================================
 * Text Output (for console/overlay integration)
 * ========================================================================== */

/**
 * lrg_inspector_get_world_info:
 * @self: a #LrgInspector
 *
 * Gets formatted information about the inspected world.
 *
 * Returns: (transfer full) (nullable): formatted world info string
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_inspector_get_world_info   (LrgInspector *self);

/**
 * lrg_inspector_get_object_info:
 * @self: a #LrgInspector
 *
 * Gets formatted information about the selected object.
 *
 * Returns: (transfer full) (nullable): formatted object info string
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_inspector_get_object_info  (LrgInspector *self);

/**
 * lrg_inspector_get_component_info:
 * @self: a #LrgInspector
 *
 * Gets formatted information about the selected component.
 *
 * Returns: (transfer full) (nullable): formatted component info string
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_inspector_get_component_info (LrgInspector *self);

/**
 * lrg_inspector_get_object_list:
 * @self: a #LrgInspector
 *
 * Gets a formatted list of all objects in the world.
 *
 * Returns: (transfer full) (nullable): formatted object list string
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_inspector_get_object_list  (LrgInspector *self);

/**
 * lrg_inspector_get_component_list:
 * @self: a #LrgInspector
 *
 * Gets a formatted list of components on the selected object.
 *
 * Returns: (transfer full) (nullable): formatted component list string
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_inspector_get_component_list (LrgInspector *self);

/**
 * lrg_inspector_get_property_list:
 * @self: a #LrgInspector
 * @object: (nullable): the object to inspect, or selected object/component if NULL
 *
 * Gets a formatted list of properties on an object.
 *
 * Returns: (transfer full) (nullable): formatted property list string
 */
LRG_AVAILABLE_IN_ALL
gchar *           lrg_inspector_get_property_list (LrgInspector *self,
                                                   GObject      *object);

G_END_DECLS
