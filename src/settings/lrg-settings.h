/* lrg-settings.h - Main settings container
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_SETTINGS_H
#define LRG_SETTINGS_H

#include <glib-object.h>
#include "lrg-settings-group.h"
#include "lrg-graphics-settings.h"
#include "lrg-audio-settings.h"
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_SETTINGS (lrg_settings_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSettings, lrg_settings, LRG, SETTINGS, GObject)

/**
 * LrgSettingsError:
 * @LRG_SETTINGS_ERROR_IO: I/O error reading/writing settings
 * @LRG_SETTINGS_ERROR_PARSE: Error parsing settings file
 * @LRG_SETTINGS_ERROR_INVALID: Invalid settings data
 *
 * Error codes for settings operations.
 */
typedef enum
{
    LRG_SETTINGS_ERROR_IO,
    LRG_SETTINGS_ERROR_PARSE,
    LRG_SETTINGS_ERROR_INVALID
} LrgSettingsError;

#define LRG_SETTINGS_ERROR (lrg_settings_error_quark ())
LRG_AVAILABLE_IN_ALL
GQuark lrg_settings_error_quark (void);

/**
 * lrg_settings_new:
 *
 * Creates a new #LrgSettings with default values for all groups.
 *
 * Returns: (transfer full): A new #LrgSettings
 */
LRG_AVAILABLE_IN_ALL
LrgSettings *
lrg_settings_new (void);

/**
 * lrg_settings_get_default:
 *
 * Gets the default settings singleton instance.
 * This is typically used by the engine.
 *
 * Returns: (transfer none): The default #LrgSettings
 */
LRG_AVAILABLE_IN_ALL
LrgSettings *
lrg_settings_get_default (void);

/* Settings Groups */

/**
 * lrg_settings_get_graphics:
 * @self: an #LrgSettings
 *
 * Gets the graphics settings group.
 *
 * Returns: (transfer none): The #LrgGraphicsSettings
 */
LRG_AVAILABLE_IN_ALL
LrgGraphicsSettings *
lrg_settings_get_graphics (LrgSettings *self);

/**
 * lrg_settings_get_audio:
 * @self: an #LrgSettings
 *
 * Gets the audio settings group.
 *
 * Returns: (transfer none): The #LrgAudioSettings
 */
LRG_AVAILABLE_IN_ALL
LrgAudioSettings *
lrg_settings_get_audio (LrgSettings *self);

/**
 * lrg_settings_get_group:
 * @self: an #LrgSettings
 * @name: the group name (e.g., "graphics", "audio")
 *
 * Gets a settings group by name.
 *
 * Returns: (transfer none) (nullable): The #LrgSettingsGroup, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgSettingsGroup *
lrg_settings_get_group (LrgSettings *self,
                        const gchar *name);

/**
 * lrg_settings_add_group:
 * @self: an #LrgSettings
 * @group: (transfer none): the #LrgSettingsGroup to add
 *
 * Adds a custom settings group. The group name is determined by
 * calling lrg_settings_group_get_group_name() on the group.
 *
 * Returns: %TRUE if added, %FALSE if a group with that name exists
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_settings_add_group (LrgSettings      *self,
                        LrgSettingsGroup *group);

/**
 * lrg_settings_list_groups:
 * @self: an #LrgSettings
 *
 * Lists all registered settings group names.
 *
 * Returns: (transfer full) (element-type utf8): Array of group names
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_settings_list_groups (LrgSettings *self);

/* Persistence */

/**
 * lrg_settings_load:
 * @self: an #LrgSettings
 * @path: path to the settings file
 * @error: (nullable): return location for error
 *
 * Loads settings from a YAML file.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_settings_load (LrgSettings  *self,
                   const gchar  *path,
                   GError      **error);

/**
 * lrg_settings_save:
 * @self: an #LrgSettings
 * @path: path to save the settings file
 * @error: (nullable): return location for error
 *
 * Saves settings to a YAML file.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_settings_save (LrgSettings  *self,
                   const gchar  *path,
                   GError      **error);

/**
 * lrg_settings_load_default_path:
 * @self: an #LrgSettings
 * @app_id: application ID for config directory
 * @error: (nullable): return location for error
 *
 * Loads settings from the default user config path.
 * The path is: $XDG_CONFIG_HOME/app_id/settings.yaml
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_settings_load_default_path (LrgSettings  *self,
                                const gchar  *app_id,
                                GError      **error);

/**
 * lrg_settings_save_default_path:
 * @self: an #LrgSettings
 * @app_id: application ID for config directory
 * @error: (nullable): return location for error
 *
 * Saves settings to the default user config path.
 * The path is: $XDG_CONFIG_HOME/app_id/settings.yaml
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_settings_save_default_path (LrgSettings  *self,
                                const gchar  *app_id,
                                GError      **error);

/* Operations */

/**
 * lrg_settings_apply_all:
 * @self: an #LrgSettings
 *
 * Applies all settings groups to the engine/system.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_settings_apply_all (LrgSettings *self);

/**
 * lrg_settings_reset_all:
 * @self: an #LrgSettings
 *
 * Resets all settings groups to their default values.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_settings_reset_all (LrgSettings *self);

/**
 * lrg_settings_is_dirty:
 * @self: an #LrgSettings
 *
 * Checks if any settings group has unsaved changes.
 *
 * Returns: %TRUE if any group has been modified
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_settings_is_dirty (LrgSettings *self);

G_END_DECLS

#endif /* LRG_SETTINGS_H */
