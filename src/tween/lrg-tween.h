/* lrg-tween.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Single property tween that animates a GObject property.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-tween-base.h"

G_BEGIN_DECLS

#define LRG_TYPE_TWEEN (lrg_tween_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTween, lrg_tween, LRG, TWEEN, LrgTweenBase)

/**
 * LrgTweenFloatSetter:
 * @target: (transfer none): The tween's target object
 * @value: The interpolated value for this update
 * @user_data: (closure): Data given to lrg_tween_set_float_setter()
 *
 * Writes an interpolated float to the target without going through
 * the GObject property system. Installed with
 * lrg_tween_set_float_setter().
 *
 * Since: 0.7
 */
typedef void (*LrgTweenFloatSetter) (GObject  *target,
                                     gfloat    value,
                                     gpointer  user_data);

/**
 * lrg_tween_new:
 * @target: (transfer none): The target #GObject to animate
 * @property_name: The name of the property to animate
 * @duration: Duration in seconds
 *
 * Creates a new tween that animates a property on the target object.
 * The property must be readable, writable, and of a numeric type
 * (int, uint, float, double) or a type that can be interpolated.
 *
 * Returns: (transfer full): A new #LrgTween
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTween *      lrg_tween_new               (GObject         *target,
                                             const gchar     *property_name,
                                             gfloat           duration);

/**
 * lrg_tween_new_full:
 * @target: (transfer none): The target #GObject to animate
 * @property_name: The name of the property to animate
 * @duration: Duration in seconds
 * @from: (nullable): Start value (or %NULL to use current value)
 * @to: End value
 *
 * Creates a new tween with explicit start and end values.
 *
 * Returns: (transfer full): A new #LrgTween
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTween *      lrg_tween_new_full          (GObject         *target,
                                             const gchar     *property_name,
                                             gfloat           duration,
                                             const GValue    *from,
                                             const GValue    *to);

/* Target */

/**
 * lrg_tween_get_target:
 * @self: A #LrgTween
 *
 * Gets the target object being animated.
 *
 * Returns: (transfer none) (nullable): The target #GObject
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GObject *       lrg_tween_get_target        (LrgTween        *self);

/**
 * lrg_tween_get_property_name:
 * @self: A #LrgTween
 *
 * Gets the name of the property being animated.
 *
 * Returns: (transfer none): The property name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_tween_get_property_name (LrgTween        *self);

/* Value setters - Float */

/**
 * lrg_tween_set_from_float:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value for a float property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_from_float    (LrgTween        *self,
                                             gfloat           value);

/**
 * lrg_tween_set_to_float:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value for a float property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_to_float      (LrgTween        *self,
                                             gfloat           value);

/* Value setters - Double */

/**
 * lrg_tween_set_from_double:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value for a double property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_from_double   (LrgTween        *self,
                                             gdouble          value);

/**
 * lrg_tween_set_to_double:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value for a double property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_to_double     (LrgTween        *self,
                                             gdouble          value);

/* Value setters - Int */

/**
 * lrg_tween_set_from_int:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value for an integer property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_from_int      (LrgTween        *self,
                                             gint             value);

/**
 * lrg_tween_set_to_int:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value for an integer property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_to_int        (LrgTween        *self,
                                             gint             value);

/* Value setters - UInt */

/**
 * lrg_tween_set_from_uint:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value for an unsigned integer property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_from_uint     (LrgTween        *self,
                                             guint            value);

/**
 * lrg_tween_set_to_uint:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value for an unsigned integer property.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_to_uint       (LrgTween        *self,
                                             guint            value);

/* Generic value setters */

/**
 * lrg_tween_set_from_value:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value using a #GValue.
 * The value type must be compatible with the property type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_from_value    (LrgTween        *self,
                                             const GValue    *value);

/**
 * lrg_tween_set_to_value:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value using a #GValue.
 * The value type must be compatible with the property type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_to_value      (LrgTween        *self,
                                             const GValue    *value);

/* Relative mode */

/**
 * lrg_tween_get_relative:
 * @self: A #LrgTween
 *
 * Gets whether the end value is relative to the start value.
 *
 * Returns: %TRUE if relative mode is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_tween_get_relative      (LrgTween        *self);

/**
 * lrg_tween_set_relative:
 * @self: A #LrgTween
 * @relative: Whether to use relative mode
 *
 * Sets whether the end value is relative to the start value.
 * When enabled, the end value is added to the start value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_relative      (LrgTween        *self,
                                             gboolean         relative);

/**
 * lrg_tween_by_float:
 * @self: A #LrgTween
 * @delta: The amount to change
 *
 * Convenience function to animate by a relative amount.
 * Sets the end value as current + delta.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_by_float          (LrgTween        *self,
                                             gfloat           delta);

/**
 * lrg_tween_by_int:
 * @self: A #LrgTween
 * @delta: The amount to change
 *
 * Convenience function to animate by a relative amount.
 * Sets the end value as current + delta.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_by_int            (LrgTween        *self,
                                             gint             delta);

/* Use from value */

/**
 * lrg_tween_get_use_current_as_from:
 * @self: A #LrgTween
 *
 * Gets whether to use the current property value as the start value.
 *
 * Returns: %TRUE if using current value as start
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_tween_get_use_current_as_from (LrgTween  *self);

/**
 * lrg_tween_set_use_current_as_from:
 * @self: A #LrgTween
 * @use_current: Whether to use current value
 *
 * Sets whether to capture the current property value as the start
 * value when the tween starts.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_use_current_as_from (LrgTween  *self,
                                                   gboolean   use_current);

/* Direct setter */

/**
 * lrg_tween_set_float_setter:
 * @self: A #LrgTween
 * @setter: (nullable) (scope notified) (closure user_data) (destroy destroy): Function
 *   that writes each value, or %NULL to write the property again
 * @user_data: Data passed to @setter
 * @destroy: (nullable): Frees @user_data when the setter is replaced,
 *   removed, or the tween is disposed
 *
 * Makes the tween hand its values to @setter instead of writing the
 * property through GObject. Use it for hot engine targets that have a
 * plain C setter (for example lrg_transform_component_set_local_x()):
 * the write then costs one indirect call.
 *
 * The setter is used whenever both the start and the end value are
 * floats; updates and lrg_tween_base_reset() pass it the same value
 * the property write would have received. Everything else about the
 * tween is unchanged: it still needs a live target and a valid
 * property name (the target's lifetime ends the tween, and
 * #LrgTween:use-current-as-from still reads the property on start).
 * GObject is bypassed entirely, so the tween emits no "notify" for the
 * property; a setter that should notify must do so itself (a C setter
 * such as lrg_transform_component_set_local_x() does).
 *
 * Passing %NULL for @setter removes the current setter (calling its
 * destroy notify) and frees @user_data with @destroy at once.
 *
 * Since: 0.7
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_set_float_setter  (LrgTween            *self,
                                             LrgTweenFloatSetter  setter,
                                             gpointer             user_data,
                                             GDestroyNotify       destroy);

/**
 * lrg_tween_has_float_setter:
 * @self: A #LrgTween
 *
 * Gets whether a float setter is installed with
 * lrg_tween_set_float_setter().
 *
 * Returns: %TRUE if a float setter is installed
 *
 * Since: 0.7
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_tween_has_float_setter  (LrgTween            *self);

G_END_DECLS
