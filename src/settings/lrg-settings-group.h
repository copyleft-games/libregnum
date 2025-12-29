/* lrg-settings-group.h - Abstract base class for settings groups
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_SETTINGS_GROUP_H
#define LRG_SETTINGS_GROUP_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_SETTINGS_GROUP (lrg_settings_group_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgSettingsGroup, lrg_settings_group, LRG, SETTINGS_GROUP, GObject)

/**
 * LrgSettingsGroupClass:
 * @parent_class: the parent class
 * @apply: Apply the current settings to the engine/system
 * @reset: Reset all settings to their default values
 * @get_group_name: Get the group name used in serialization
 * @serialize: Serialize settings to a GVariant
 * @deserialize: Deserialize settings from a GVariant
 *
 * The virtual function table for #LrgSettingsGroup.
 *
 * Subclasses must implement all virtual methods to provide
 * their specific settings behavior.
 */
struct _LrgSettingsGroupClass
{
    GObjectClass parent_class;

    /*< public >*/

    /**
     * LrgSettingsGroupClass::apply:
     * @self: an #LrgSettingsGroup
     *
     * Applies the current settings to the engine or system.
     * Subclasses should override this to apply their specific
     * settings (e.g., graphics settings apply to renderer).
     */
    void         (*apply)          (LrgSettingsGroup *self);

    /**
     * LrgSettingsGroupClass::reset:
     * @self: an #LrgSettingsGroup
     *
     * Resets all settings in this group to their default values.
     */
    void         (*reset)          (LrgSettingsGroup *self);

    /**
     * LrgSettingsGroupClass::get_group_name:
     * @self: an #LrgSettingsGroup
     *
     * Gets the group name used for serialization (e.g., "graphics", "audio").
     *
     * Returns: (transfer none): The group name string
     */
    const gchar *(*get_group_name) (LrgSettingsGroup *self);

    /**
     * LrgSettingsGroupClass::serialize:
     * @self: an #LrgSettingsGroup
     * @error: (nullable): return location for error
     *
     * Serializes the settings to a #GVariant dictionary.
     *
     * Returns: (transfer full) (nullable): A new #GVariant, or %NULL on error
     */
    GVariant    *(*serialize)      (LrgSettingsGroup  *self,
                                    GError           **error);

    /**
     * LrgSettingsGroupClass::deserialize:
     * @self: an #LrgSettingsGroup
     * @data: the #GVariant containing settings data
     * @error: (nullable): return location for error
     *
     * Deserializes settings from a #GVariant dictionary.
     *
     * Returns: %TRUE on success, %FALSE on error
     */
    gboolean     (*deserialize)    (LrgSettingsGroup  *self,
                                    GVariant          *data,
                                    GError           **error);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_settings_group_apply:
 * @self: an #LrgSettingsGroup
 *
 * Applies the current settings to the engine or system.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_settings_group_apply (LrgSettingsGroup *self);

/**
 * lrg_settings_group_reset:
 * @self: an #LrgSettingsGroup
 *
 * Resets all settings in this group to their default values.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_settings_group_reset (LrgSettingsGroup *self);

/**
 * lrg_settings_group_get_group_name:
 * @self: an #LrgSettingsGroup
 *
 * Gets the group name used for serialization.
 *
 * Returns: (transfer none): The group name string
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_settings_group_get_group_name (LrgSettingsGroup *self);

/**
 * lrg_settings_group_serialize:
 * @self: an #LrgSettingsGroup
 * @error: (nullable): return location for error
 *
 * Serializes the settings group to a #GVariant.
 *
 * Returns: (transfer full) (nullable): A new #GVariant dictionary, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_settings_group_serialize (LrgSettingsGroup  *self,
                              GError           **error);

/**
 * lrg_settings_group_deserialize:
 * @self: an #LrgSettingsGroup
 * @data: the #GVariant containing settings data
 * @error: (nullable): return location for error
 *
 * Deserializes settings from a #GVariant dictionary.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_settings_group_deserialize (LrgSettingsGroup  *self,
                                GVariant          *data,
                                GError           **error);

/**
 * lrg_settings_group_is_dirty:
 * @self: an #LrgSettingsGroup
 *
 * Checks if the settings have been modified since last save/load.
 *
 * Returns: %TRUE if settings have been modified
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_settings_group_is_dirty (LrgSettingsGroup *self);

/**
 * lrg_settings_group_mark_dirty:
 * @self: an #LrgSettingsGroup
 *
 * Marks the settings group as modified.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_settings_group_mark_dirty (LrgSettingsGroup *self);

/**
 * lrg_settings_group_mark_clean:
 * @self: an #LrgSettingsGroup
 *
 * Marks the settings group as unmodified (saved).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_settings_group_mark_clean (LrgSettingsGroup *self);

G_END_DECLS

#endif /* LRG_SETTINGS_GROUP_H */
