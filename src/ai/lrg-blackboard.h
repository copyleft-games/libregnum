/* lrg-blackboard.h
 *
 * Copyright 2024 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Blackboard for behavior tree data sharing.
 */

#ifndef LRG_BLACKBOARD_H
#define LRG_BLACKBOARD_H

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_BLACKBOARD (lrg_blackboard_get_type ())

G_DECLARE_FINAL_TYPE (LrgBlackboard, lrg_blackboard, LRG, BLACKBOARD, GObject)

/**
 * lrg_blackboard_new:
 *
 * Creates a new blackboard for storing behavior tree data.
 *
 * Returns: (transfer full): A new #LrgBlackboard
 */
LRG_AVAILABLE_IN_ALL
LrgBlackboard *     lrg_blackboard_new               (void);

/**
 * lrg_blackboard_set_int:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: The integer value
 *
 * Sets an integer value in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_int           (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gint           value);

/**
 * lrg_blackboard_get_int:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @default_value: Default if key not found
 *
 * Gets an integer value from the blackboard.
 *
 * Returns: The integer value, or @default_value if not found
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_blackboard_get_int           (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gint           default_value);

/**
 * lrg_blackboard_set_float:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: The float value
 *
 * Sets a float value in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_float         (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gfloat         value);

/**
 * lrg_blackboard_get_float:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @default_value: Default if key not found
 *
 * Gets a float value from the blackboard.
 *
 * Returns: The float value, or @default_value if not found
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_blackboard_get_float         (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gfloat         default_value);

/**
 * lrg_blackboard_set_bool:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: The boolean value
 *
 * Sets a boolean value in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_bool          (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gboolean       value);

/**
 * lrg_blackboard_get_bool:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @default_value: Default if key not found
 *
 * Gets a boolean value from the blackboard.
 *
 * Returns: The boolean value, or @default_value if not found
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_blackboard_get_bool          (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      gboolean       default_value);

/**
 * lrg_blackboard_set_string:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @value: (nullable): The string value
 *
 * Sets a string value in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_string        (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      const gchar   *value);

/**
 * lrg_blackboard_get_string:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Gets a string value from the blackboard.
 *
 * Returns: (transfer none) (nullable): The string value, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_blackboard_get_string        (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_set_object:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @object: (nullable) (transfer none): The GObject to store
 *
 * Sets a GObject reference in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_object        (LrgBlackboard *self,
                                                      const gchar   *key,
                                                      GObject       *object);

/**
 * lrg_blackboard_get_object:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Gets a GObject from the blackboard.
 *
 * Returns: (transfer none) (nullable): The GObject, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
GObject *           lrg_blackboard_get_object        (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_set_pointer:
 * @self: an #LrgBlackboard
 * @key: The key name
 * @pointer: (nullable): The pointer to store
 * @destroy: (nullable): Destroy function for the pointer
 *
 * Sets an arbitrary pointer in the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_set_pointer       (LrgBlackboard  *self,
                                                      const gchar    *key,
                                                      gpointer        pointer,
                                                      GDestroyNotify  destroy);

/**
 * lrg_blackboard_get_pointer:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Gets a pointer from the blackboard.
 *
 * Returns: (nullable): The pointer, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
gpointer            lrg_blackboard_get_pointer       (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_has_key:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Checks if a key exists in the blackboard.
 *
 * Returns: %TRUE if key exists
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_blackboard_has_key           (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_remove:
 * @self: an #LrgBlackboard
 * @key: The key name
 *
 * Removes a key from the blackboard.
 *
 * Returns: %TRUE if key was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_blackboard_remove            (LrgBlackboard *self,
                                                      const gchar   *key);

/**
 * lrg_blackboard_clear:
 * @self: an #LrgBlackboard
 *
 * Removes all entries from the blackboard.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_blackboard_clear             (LrgBlackboard *self);

/**
 * lrg_blackboard_get_keys:
 * @self: an #LrgBlackboard
 *
 * Gets all keys in the blackboard.
 *
 * Returns: (transfer container) (element-type utf8): List of key names
 */
LRG_AVAILABLE_IN_ALL
GList *             lrg_blackboard_get_keys          (LrgBlackboard *self);

G_END_DECLS

#endif /* LRG_BLACKBOARD_H */
