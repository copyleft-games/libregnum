/* lrg-item-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgItemDef - Definition of an item type.
 *
 * This is a derivable class that defines the properties and behavior
 * of an item type. Actual item instances are represented by LrgItemStack.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_ITEM_DEF (lrg_item_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgItemDef, lrg_item_def, LRG, ITEM_DEF, GObject)

/**
 * LrgItemDefClass:
 * @parent_class: the parent class
 * @on_use: virtual function called when item is used
 * @can_stack_with: virtual function to check if items can stack
 * @get_tooltip: virtual function to get tooltip text
 *
 * The class structure for #LrgItemDef.
 *
 * Since: 1.0
 */
struct _LrgItemDefClass
{
    GObjectClass parent_class;

    /**
     * LrgItemDefClass::on_use:
     * @self: the item definition
     * @owner: (nullable): the object using the item
     * @quantity: how many are being used
     *
     * Called when the item is used.
     *
     * Returns: %TRUE if the item was consumed
     */
    gboolean (* on_use)          (LrgItemDef *self,
                                  GObject    *owner,
                                  guint       quantity);

    /**
     * LrgItemDefClass::can_stack_with:
     * @self: the item definition
     * @other: another item definition
     *
     * Checks if items of these definitions can stack together.
     * Default implementation returns TRUE if both have the same ID.
     *
     * Returns: %TRUE if items can stack
     */
    gboolean (* can_stack_with)  (LrgItemDef *self,
                                  LrgItemDef *other);

    /**
     * LrgItemDefClass::get_tooltip:
     * @self: the item definition
     *
     * Gets the tooltip text for this item.
     * Default implementation returns the description.
     *
     * Returns: (transfer full) (nullable): tooltip text, or %NULL
     */
    gchar *  (* get_tooltip)     (LrgItemDef *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_item_def_new:
 * @id: unique identifier for this item type
 *
 * Creates a new item definition.
 *
 * Returns: (transfer full): A new #LrgItemDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemDef *
lrg_item_def_new (const gchar *id);

/* Properties */

/**
 * lrg_item_def_get_id:
 * @self: an #LrgItemDef
 *
 * Gets the unique identifier for this item type.
 *
 * Returns: (transfer none): the item ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_item_def_get_id (LrgItemDef *self);

/**
 * lrg_item_def_get_name:
 * @self: an #LrgItemDef
 *
 * Gets the display name.
 *
 * Returns: (transfer none) (nullable): the display name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_item_def_get_name (LrgItemDef *self);

/**
 * lrg_item_def_set_name:
 * @self: an #LrgItemDef
 * @name: (nullable): the display name
 *
 * Sets the display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_name (LrgItemDef  *self,
                       const gchar *name);

/**
 * lrg_item_def_get_description:
 * @self: an #LrgItemDef
 *
 * Gets the item description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_item_def_get_description (LrgItemDef *self);

/**
 * lrg_item_def_set_description:
 * @self: an #LrgItemDef
 * @description: (nullable): the description
 *
 * Sets the item description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_description (LrgItemDef  *self,
                              const gchar *description);

/**
 * lrg_item_def_get_item_type:
 * @self: an #LrgItemDef
 *
 * Gets the item type.
 *
 * Returns: the #LrgItemType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgItemType
lrg_item_def_get_item_type (LrgItemDef *self);

/**
 * lrg_item_def_set_item_type:
 * @self: an #LrgItemDef
 * @item_type: the #LrgItemType
 *
 * Sets the item type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_item_type (LrgItemDef  *self,
                            LrgItemType  item_type);

/**
 * lrg_item_def_get_stackable:
 * @self: an #LrgItemDef
 *
 * Gets whether this item type is stackable.
 *
 * Returns: %TRUE if stackable
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_def_get_stackable (LrgItemDef *self);

/**
 * lrg_item_def_set_stackable:
 * @self: an #LrgItemDef
 * @stackable: whether stackable
 *
 * Sets whether this item type is stackable.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_stackable (LrgItemDef *self,
                            gboolean    stackable);

/**
 * lrg_item_def_get_max_stack:
 * @self: an #LrgItemDef
 *
 * Gets the maximum stack size.
 *
 * Returns: maximum stack size
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_item_def_get_max_stack (LrgItemDef *self);

/**
 * lrg_item_def_set_max_stack:
 * @self: an #LrgItemDef
 * @max_stack: maximum stack size
 *
 * Sets the maximum stack size.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_max_stack (LrgItemDef *self,
                            guint       max_stack);

/**
 * lrg_item_def_get_value:
 * @self: an #LrgItemDef
 *
 * Gets the base value/price of the item.
 *
 * Returns: the value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_item_def_get_value (LrgItemDef *self);

/**
 * lrg_item_def_set_value:
 * @self: an #LrgItemDef
 * @value: the value
 *
 * Sets the base value/price of the item.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_value (LrgItemDef *self,
                        gint        value);

/* Custom Properties */

/**
 * lrg_item_def_get_property_int:
 * @self: an #LrgItemDef
 * @key: the property key
 * @default_value: value to return if key not found
 *
 * Gets a custom integer property.
 *
 * Returns: the property value or @default_value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_item_def_get_property_int (LrgItemDef  *self,
                               const gchar *key,
                               gint         default_value);

/**
 * lrg_item_def_set_property_int:
 * @self: an #LrgItemDef
 * @key: the property key
 * @value: the value
 *
 * Sets a custom integer property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_property_int (LrgItemDef  *self,
                               const gchar *key,
                               gint         value);

/**
 * lrg_item_def_get_property_float:
 * @self: an #LrgItemDef
 * @key: the property key
 * @default_value: value to return if key not found
 *
 * Gets a custom float property.
 *
 * Returns: the property value or @default_value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_item_def_get_property_float (LrgItemDef  *self,
                                 const gchar *key,
                                 gfloat       default_value);

/**
 * lrg_item_def_set_property_float:
 * @self: an #LrgItemDef
 * @key: the property key
 * @value: the value
 *
 * Sets a custom float property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_property_float (LrgItemDef  *self,
                                 const gchar *key,
                                 gfloat       value);

/**
 * lrg_item_def_get_property_string:
 * @self: an #LrgItemDef
 * @key: the property key
 *
 * Gets a custom string property.
 *
 * Returns: (transfer none) (nullable): the property value or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_item_def_get_property_string (LrgItemDef  *self,
                                  const gchar *key);

/**
 * lrg_item_def_set_property_string:
 * @self: an #LrgItemDef
 * @key: the property key
 * @value: (nullable): the value
 *
 * Sets a custom string property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_property_string (LrgItemDef  *self,
                                  const gchar *key,
                                  const gchar *value);

/**
 * lrg_item_def_get_property_bool:
 * @self: an #LrgItemDef
 * @key: the property key
 * @default_value: value to return if key not found
 *
 * Gets a custom boolean property.
 *
 * Returns: the property value or @default_value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_def_get_property_bool (LrgItemDef  *self,
                                const gchar *key,
                                gboolean     default_value);

/**
 * lrg_item_def_set_property_bool:
 * @self: an #LrgItemDef
 * @key: the property key
 * @value: the value
 *
 * Sets a custom boolean property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_item_def_set_property_bool (LrgItemDef  *self,
                                const gchar *key,
                                gboolean     value);

/**
 * lrg_item_def_has_custom_property:
 * @self: an #LrgItemDef
 * @key: the property key
 *
 * Checks if a custom property exists.
 *
 * Returns: %TRUE if the property exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_def_has_custom_property (LrgItemDef  *self,
                                  const gchar *key);

/**
 * lrg_item_def_remove_custom_property:
 * @self: an #LrgItemDef
 * @key: the property key
 *
 * Removes a custom property.
 *
 * Returns: %TRUE if the property was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_def_remove_custom_property (LrgItemDef  *self,
                                     const gchar *key);

/* Virtual Function Wrappers */

/**
 * lrg_item_def_use:
 * @self: an #LrgItemDef
 * @owner: (nullable): the object using the item
 * @quantity: how many are being used
 *
 * Uses the item. Calls the on_use virtual function.
 *
 * Returns: %TRUE if the item was consumed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_def_use (LrgItemDef *self,
                  GObject    *owner,
                  guint       quantity);

/**
 * lrg_item_def_can_stack_with:
 * @self: an #LrgItemDef
 * @other: another #LrgItemDef
 *
 * Checks if items of these definitions can stack together.
 *
 * Returns: %TRUE if items can stack
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_item_def_can_stack_with (LrgItemDef *self,
                             LrgItemDef *other);

/**
 * lrg_item_def_get_tooltip:
 * @self: an #LrgItemDef
 *
 * Gets the tooltip text for this item.
 *
 * Returns: (transfer full) (nullable): tooltip text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_item_def_get_tooltip (LrgItemDef *self);

G_END_DECLS
