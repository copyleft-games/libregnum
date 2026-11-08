/* lrg-project-config.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Extra project.ryaml blocks that #LrgProject does not own: autoloads,
 * layered settings (a default map plus per-platform overrides) and a
 * Steam page (app id, achievements).
 *
 * A platform lookup returns the override when that platform sets the key,
 * otherwise the default `settings:` value.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_PROJECT_CONFIG (lrg_project_config_get_type ())
#define LRG_PROJECT_CONFIG_ERROR (lrg_project_config_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProjectConfig, lrg_project_config, LRG, PROJECT_CONFIG, GObject)

typedef enum
{
	LRG_PROJECT_CONFIG_ERROR_FAILED
} LrgProjectConfigError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_project_config_error_quark (void);

/**
 * lrg_project_config_new_from_file:
 * @path: (type filename): a project.ryaml
 * @error: (nullable): return location for a #GError
 *
 * Returns: (transfer full) (nullable): the config, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgProjectConfig * lrg_project_config_new_from_file (const gchar  *path,
                                                     GError      **error);

/**
 * lrg_project_config_get_autoloads:
 *
 * Returns: (transfer full) (array zero-terminated=1): script paths loaded at startup
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_project_config_get_autoloads (LrgProjectConfig *self);

/**
 * lrg_project_config_get_platform_setting:
 * @self: a config
 * @platform: (nullable): "linux", "windows", or %NULL for the default layer only
 * @key: setting name
 *
 * Returns: (transfer none) (nullable): the value, or %NULL if unset
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_project_config_get_platform_setting (LrgProjectConfig *self,
                                                       const gchar      *platform,
                                                       const gchar      *key);

LRG_AVAILABLE_IN_ALL
gint64 lrg_project_config_get_steam_app_id (LrgProjectConfig *self);

LRG_AVAILABLE_IN_ALL
gboolean lrg_project_config_get_steam_achievements (LrgProjectConfig *self);

G_END_DECLS
