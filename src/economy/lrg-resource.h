/* lrg-resource.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgResource - Definition of a resource type.
 *
 * This is a derivable class that defines the properties and behavior
 * of a resource type (currency, material, food, energy, etc.).
 * Actual resource quantities are stored in LrgResourcePool.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_RESOURCE (lrg_resource_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgResource, lrg_resource, LRG, RESOURCE, GObject)

/**
 * LrgResourceClass:
 * @parent_class: the parent class
 * @format_value: virtual function to format a resource amount for display
 * @validate_amount: virtual function to validate a resource amount
 *
 * The class structure for #LrgResource.
 *
 * Since: 1.0
 */
struct _LrgResourceClass
{
    GObjectClass parent_class;

    /**
     * LrgResourceClass::format_value:
     * @self: the resource definition
     * @amount: the amount to format
     *
     * Formats a resource amount for display. Override this to provide
     * custom formatting (e.g., currency symbols, abbreviations).
     *
     * Returns: (transfer full): formatted string representation
     */
    gchar * (* format_value)     (LrgResource *self,
                                  gdouble      amount);

    /**
     * LrgResourceClass::validate_amount:
     * @self: the resource definition
     * @amount: the amount to validate
     *
     * Validates whether an amount is valid for this resource.
     * Default implementation checks against min-value and max-value.
     *
     * Returns: %TRUE if the amount is valid
     */
    gboolean (* validate_amount) (LrgResource *self,
                                  gdouble      amount);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_resource_new:
 * @id: unique identifier for this resource type
 *
 * Creates a new resource definition.
 *
 * Returns: (transfer full): A new #LrgResource
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgResource *
lrg_resource_new (const gchar *id);

/* Properties */

/**
 * lrg_resource_get_id:
 * @self: an #LrgResource
 *
 * Gets the unique identifier for this resource type.
 *
 * Returns: (transfer none): the resource ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_resource_get_id (LrgResource *self);

/**
 * lrg_resource_get_name:
 * @self: an #LrgResource
 *
 * Gets the display name.
 *
 * Returns: (transfer none) (nullable): the display name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_resource_get_name (LrgResource *self);

/**
 * lrg_resource_set_name:
 * @self: an #LrgResource
 * @name: (nullable): the display name
 *
 * Sets the display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_set_name (LrgResource *self,
                       const gchar *name);

/**
 * lrg_resource_get_description:
 * @self: an #LrgResource
 *
 * Gets the resource description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_resource_get_description (LrgResource *self);

/**
 * lrg_resource_set_description:
 * @self: an #LrgResource
 * @description: (nullable): the description
 *
 * Sets the resource description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_set_description (LrgResource  *self,
                              const gchar  *description);

/**
 * lrg_resource_get_icon:
 * @self: an #LrgResource
 *
 * Gets the icon path or identifier.
 *
 * Returns: (transfer none) (nullable): the icon path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_resource_get_icon (LrgResource *self);

/**
 * lrg_resource_set_icon:
 * @self: an #LrgResource
 * @icon: (nullable): the icon path or identifier
 *
 * Sets the icon path or identifier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_set_icon (LrgResource *self,
                       const gchar *icon);

/**
 * lrg_resource_get_category:
 * @self: an #LrgResource
 *
 * Gets the resource category.
 *
 * Returns: the #LrgResourceCategory
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgResourceCategory
lrg_resource_get_category (LrgResource *self);

/**
 * lrg_resource_set_category:
 * @self: an #LrgResource
 * @category: the #LrgResourceCategory
 *
 * Sets the resource category.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_set_category (LrgResource         *self,
                           LrgResourceCategory  category);

/**
 * lrg_resource_get_min_value:
 * @self: an #LrgResource
 *
 * Gets the minimum allowed value for this resource.
 * A value of -G_MAXDOUBLE indicates no minimum (can go negative).
 *
 * Returns: the minimum value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_get_min_value (LrgResource *self);

/**
 * lrg_resource_set_min_value:
 * @self: an #LrgResource
 * @min_value: the minimum value
 *
 * Sets the minimum allowed value for this resource.
 * Use -G_MAXDOUBLE to allow negative values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_set_min_value (LrgResource *self,
                            gdouble      min_value);

/**
 * lrg_resource_get_max_value:
 * @self: an #LrgResource
 *
 * Gets the maximum allowed value for this resource.
 * A value of G_MAXDOUBLE indicates no maximum (unlimited).
 *
 * Returns: the maximum value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_get_max_value (LrgResource *self);

/**
 * lrg_resource_set_max_value:
 * @self: an #LrgResource
 * @max_value: the maximum value
 *
 * Sets the maximum allowed value for this resource.
 * Use G_MAXDOUBLE for unlimited.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_set_max_value (LrgResource *self,
                            gdouble      max_value);

/**
 * lrg_resource_get_decimal_places:
 * @self: an #LrgResource
 *
 * Gets the number of decimal places for display.
 *
 * Returns: number of decimal places (0 for integer display)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_resource_get_decimal_places (LrgResource *self);

/**
 * lrg_resource_set_decimal_places:
 * @self: an #LrgResource
 * @decimal_places: number of decimal places (0-6)
 *
 * Sets the number of decimal places for display.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_set_decimal_places (LrgResource *self,
                                 guint        decimal_places);

/**
 * lrg_resource_get_hidden:
 * @self: an #LrgResource
 *
 * Gets whether this resource is hidden from the player UI.
 *
 * Returns: %TRUE if hidden
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_resource_get_hidden (LrgResource *self);

/**
 * lrg_resource_set_hidden:
 * @self: an #LrgResource
 * @hidden: whether to hide from UI
 *
 * Sets whether this resource is hidden from the player UI.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_resource_set_hidden (LrgResource *self,
                         gboolean     hidden);

/* Virtual Function Wrappers */

/**
 * lrg_resource_format_value:
 * @self: an #LrgResource
 * @amount: the amount to format
 *
 * Formats a resource amount for display.
 *
 * Returns: (transfer full): formatted string representation
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_resource_format_value (LrgResource *self,
                           gdouble      amount);

/**
 * lrg_resource_validate_amount:
 * @self: an #LrgResource
 * @amount: the amount to validate
 *
 * Validates whether an amount is valid for this resource.
 *
 * Returns: %TRUE if the amount is valid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_resource_validate_amount (LrgResource *self,
                              gdouble      amount);

/**
 * lrg_resource_clamp_amount:
 * @self: an #LrgResource
 * @amount: the amount to clamp
 *
 * Clamps an amount to the valid range for this resource.
 *
 * Returns: the clamped amount
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_resource_clamp_amount (LrgResource *self,
                           gdouble      amount);

G_END_DECLS
