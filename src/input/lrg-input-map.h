/* lrg-input-map.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Container for input actions with YAML serialization.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-input-action.h"

G_BEGIN_DECLS

#define LRG_TYPE_INPUT_MAP (lrg_input_map_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgInputMap, lrg_input_map, LRG, INPUT_MAP, GObject)

/**
 * LrgInputMapClass:
 * @parent_class: The parent class
 *
 * The class structure for #LrgInputMap.
 */
struct _LrgInputMapClass
{
    GObjectClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_input_map_new:
 *
 * Creates a new empty input map.
 *
 * Returns: (transfer full): A new #LrgInputMap
 */
LRG_AVAILABLE_IN_ALL
LrgInputMap * lrg_input_map_new (void);

/* ==========================================================================
 * Action Management
 * ========================================================================== */

/**
 * lrg_input_map_add_action:
 * @self: an #LrgInputMap
 * @action: the action to add
 *
 * Adds an action to this map.
 *
 * The map takes ownership of the action.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_map_add_action (LrgInputMap    *self,
                               LrgInputAction *action);

/**
 * lrg_input_map_remove_action:
 * @self: an #LrgInputMap
 * @name: the action name to remove
 *
 * Removes an action from this map by name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_map_remove_action (LrgInputMap *self,
                                  const gchar *name);

/**
 * lrg_input_map_get_action:
 * @self: an #LrgInputMap
 * @name: the action name
 *
 * Gets an action by name.
 *
 * Returns: (transfer none) (nullable): The action, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgInputAction * lrg_input_map_get_action (LrgInputMap *self,
                                           const gchar *name);

/**
 * lrg_input_map_has_action:
 * @self: an #LrgInputMap
 * @name: the action name
 *
 * Checks if an action exists in this map.
 *
 * Returns: %TRUE if the action exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_map_has_action (LrgInputMap *self,
                                   const gchar *name);

/**
 * lrg_input_map_get_actions:
 * @self: an #LrgInputMap
 *
 * Gets a list of all actions in this map, in the order they were added
 * (the file order for a loaded map; a replaced action keeps its place).
 *
 * Returns: (transfer container) (element-type LrgInputAction): A list of actions
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_input_map_get_actions (LrgInputMap *self);

/**
 * lrg_input_map_get_action_count:
 * @self: an #LrgInputMap
 *
 * Gets the number of actions in this map.
 *
 * Returns: The action count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_input_map_get_action_count (LrgInputMap *self);

/**
 * lrg_input_map_clear:
 * @self: an #LrgInputMap
 *
 * Removes all actions from this map.
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_map_clear (LrgInputMap *self);

/**
 * lrg_input_map_clear_overrides:
 * @self: an #LrgInputMap
 *
 * Clears the injected state (lrg_input_action_set_override()) of every
 * action, so they read their bindings again.
 *
 * Since: 0.4
 */
LRG_AVAILABLE_IN_ALL
void lrg_input_map_clear_overrides (LrgInputMap *self);

/* ==========================================================================
 * Convenience State Query
 * ========================================================================== */

/**
 * lrg_input_map_is_pressed:
 * @self: an #LrgInputMap
 * @action_name: the action name
 *
 * Checks if an action was just pressed this frame.
 *
 * Returns: %TRUE if the action was just pressed, %FALSE otherwise or if not found
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_map_is_pressed (LrgInputMap *self,
                                   const gchar *action_name);

/**
 * lrg_input_map_is_down:
 * @self: an #LrgInputMap
 * @action_name: the action name
 *
 * Checks if an action is currently held down.
 *
 * Returns: %TRUE if the action is held down, %FALSE otherwise or if not found
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_map_is_down (LrgInputMap *self,
                                const gchar *action_name);

/**
 * lrg_input_map_is_released:
 * @self: an #LrgInputMap
 * @action_name: the action name
 *
 * Checks if an action was just released this frame.
 *
 * Returns: %TRUE if the action was just released, %FALSE otherwise or if not found
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_map_is_released (LrgInputMap *self,
                                    const gchar *action_name);

/**
 * lrg_input_map_get_value:
 * @self: an #LrgInputMap
 * @action_name: the action name
 *
 * Gets the axis value for an action.
 *
 * Returns: The axis value (0.0 to 1.0), or 0.0 if not found
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_input_map_get_value (LrgInputMap *self,
                                const gchar *action_name);

/**
 * lrg_input_map_get_axis:
 * @self: an #LrgInputMap
 * @action_name: the action name
 *
 * Gets the *signed* analog axis value for an action (see
 * lrg_input_action_get_axis()), or 0.0 if the action is not found.
 *
 * Returns: the signed axis value (typically -1.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_input_map_get_axis (LrgInputMap *self,
                               const gchar *action_name);

/* ==========================================================================
 * YAML Serialization
 * ========================================================================== */

/**
 * lrg_input_map_load_from_file:
 * @self: an #LrgInputMap
 * @path: (type filename): path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads input mappings from a YAML file.
 *
 * This clears any existing actions before loading.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_map_load_from_file (LrgInputMap  *self,
                                       const gchar  *path,
                                       GError      **error);

/**
 * lrg_input_map_save_to_file:
 * @self: an #LrgInputMap
 * @path: (type filename): path to save the YAML file
 * @error: (nullable): return location for error
 *
 * Saves input mappings to a YAML file; see lrg_input_map_save_to_data().
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_map_save_to_file (LrgInputMap  *self,
                                     const gchar  *path,
                                     GError      **error);

/**
 * lrg_input_map_load_from_data:
 * @self: an #LrgInputMap
 * @data: (array length=length) (element-type guint8): the YAML text
 * @length: length of @data in bytes, or -1 when it is nul-terminated
 * @error: (nullable): return location for error
 *
 * Loads input mappings from YAML text in the format of
 * lrg_input_map_load_from_file(): an `actions` mapping of action name to
 * a mapping with a `bindings` list. Each binding has a `type`
 * (`keyboard`, `mouse_button`, `gamepad_button`, `gamepad_axis`) and its
 * fields (`key`/`button`/`axis`, `modifiers`, `gamepad`, `threshold`,
 * `positive`).
 *
 * This clears any existing actions before loading. Keys the engine does
 * not know (at the root, in an action, in a binding) are kept and written
 * back by lrg_input_map_save_to_data().
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_input_map_load_from_data (LrgInputMap  *self,
                                       const gchar  *data,
                                       gssize        length,
                                       GError      **error);

/**
 * lrg_input_map_save_to_data:
 * @self: an #LrgInputMap
 * @length: (out) (optional): return location for the text length
 * @error: (nullable): return location for error
 *
 * Writes the input mappings as YAML: the actions in the order they were
 * added (file order for a loaded map), each binding with its known keys.
 * Keys the engine does not know, from the last loaded document, are
 * written back where they were: root keys around `actions`, action keys
 * around `bindings`, and a binding's extra keys after its known ones (when
 * the binding at that index still has the same type). Comments are not
 * kept. Saving a map that was loaded from saved text gives the same bytes.
 *
 * Returns: (transfer full) (nullable): the YAML text, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_input_map_save_to_data (LrgInputMap  *self,
                                    gsize        *length,
                                    GError      **error);

G_END_DECLS
