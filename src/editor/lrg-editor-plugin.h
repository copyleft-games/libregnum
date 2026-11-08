/* lrg-editor-plugin.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Editor plugins.
 *
 * A plugin is a directory with plugin.yaml (`name`, `entry`) and a shared
 * object. The object exports lrg_plugin_init(), which receives the manager
 * and a function table and registers actions, panels and inspector types.
 * The editor reads those lists back and installs them; this module does
 * not touch the widget tree itself, so a plugin can be tested headless.
 *
 * The function-table field order is ABI. Do not reorder it.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_EDITOR_PLUGIN_MANAGER (lrg_editor_plugin_manager_get_type ())
#define LRG_EDITOR_PLUGIN_ERROR (lrg_editor_plugin_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorPluginManager, lrg_editor_plugin_manager, LRG, EDITOR_PLUGIN_MANAGER, GObject)

typedef enum
{
	LRG_EDITOR_PLUGIN_ERROR_FAILED,
	LRG_EDITOR_PLUGIN_ERROR_UNSUPPORTED
} LrgEditorPluginError;

/**
 * LrgEditorPluginApi:
 * @add_action: register an action id, label and category
 * @add_panel: register a dock panel id and title
 * @add_inspector: register a GType name that wants a custom inspector
 *
 * C ABI passed to lrg_plugin_init(). Field order is part of the ABI.
 */
typedef struct
{
	void (*add_action)    (gpointer manager, const gchar *id, const gchar *label, const gchar *category);
	void (*add_panel)     (gpointer manager, const gchar *id, const gchar *title);
	void (*add_inspector) (gpointer manager, const gchar *type_name);
} LrgEditorPluginApi;

/**
 * LrgEditorPluginInit:
 * @manager: the #LrgEditorPluginManager
 * @api: the registration table
 *
 * Symbol a plugin shared object exports as `lrg_plugin_init`.
 */
typedef void (*LrgEditorPluginInit) (gpointer                  manager,
                                     const LrgEditorPluginApi *api);

LRG_AVAILABLE_IN_ALL
GQuark lrg_editor_plugin_error_quark (void);

LRG_AVAILABLE_IN_ALL
LrgEditorPluginManager * lrg_editor_plugin_manager_new (void);

/**
 * lrg_editor_plugin_manager_load:
 * @self: a manager
 * @directory: (type filename): a directory containing plugin.yaml
 * @error: (nullable): return location for a #GError
 *
 * Opens the shared object named by `entry` and calls lrg_plugin_init().
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_plugin_manager_load (LrgEditorPluginManager  *self,
                                         const gchar             *directory,
                                         GError                 **error);

LRG_AVAILABLE_IN_ALL
void lrg_editor_plugin_manager_add_action (LrgEditorPluginManager *self,
                                           const gchar            *id,
                                           const gchar            *label,
                                           const gchar            *category);

LRG_AVAILABLE_IN_ALL
void lrg_editor_plugin_manager_add_panel (LrgEditorPluginManager *self,
                                          const gchar            *id,
                                          const gchar            *title);

LRG_AVAILABLE_IN_ALL
void lrg_editor_plugin_manager_add_inspector (LrgEditorPluginManager *self,
                                              const gchar            *type_name);

/**
 * lrg_editor_plugin_manager_list_actions:
 *
 * Returns: (transfer full) (array zero-terminated=1): action ids
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_plugin_manager_list_actions (LrgEditorPluginManager *self);

/**
 * lrg_editor_plugin_manager_list_panels:
 * @self: a plugin manager
 *
 * Returns: (transfer full) (array zero-terminated=1): panel ids
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_plugin_manager_list_panels (LrgEditorPluginManager *self);

/**
 * lrg_editor_plugin_manager_list_inspectors:
 * @self: a plugin manager
 *
 * Returns: (transfer full) (array zero-terminated=1): inspector type names
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_plugin_manager_list_inspectors (LrgEditorPluginManager *self);

LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_plugin_manager_action_label (LrgEditorPluginManager *self,
                                                      const gchar            *id);

/**
 * lrg_editor_plugin_manager_panel_title:
 * @self: a plugin manager
 * @id: a panel id from lrg_editor_plugin_manager_list_panels()
 *
 * Returns: (transfer none) (nullable): the title registered with @id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_plugin_manager_panel_title (LrgEditorPluginManager *self,
                                                     const gchar            *id);

G_END_DECLS
