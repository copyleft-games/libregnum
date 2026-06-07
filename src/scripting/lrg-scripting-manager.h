/* lrg-scripting-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Registry of available scripting-language backends.
 *
 * LrgScriptingManager enumerates the scripting backends compiled into this
 * build (#LrgScriptLanguage values: Lua, Python, Gjs, Crispy) and creates a
 * fresh #LrgScripting context for a chosen language. It lets the editor and
 * #LrgScriptComponent offer "attach a script in language X" data-drivenly,
 * matching the engine's pluggable scripting design.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_MANAGER (lrg_scripting_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptingManager, lrg_scripting_manager, LRG, SCRIPTING_MANAGER, GObject)

/**
 * lrg_scripting_manager_get_default:
 *
 * Gets the process-wide scripting manager singleton.
 *
 * Returns: (transfer none): the default #LrgScriptingManager
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingManager * lrg_scripting_manager_get_default (void);

/**
 * lrg_scripting_manager_is_available:
 * @self: an #LrgScriptingManager
 * @language: a scripting language
 *
 * Returns: %TRUE if a backend for @language is compiled into this build
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_manager_is_available (LrgScriptingManager *self,
                                             LrgScriptLanguage    language);

/**
 * lrg_scripting_manager_create_context:
 * @self: an #LrgScriptingManager
 * @language: a scripting language
 *
 * Creates a fresh scripting context for @language.
 *
 * Returns: (transfer full) (nullable): a new #LrgScripting, or %NULL if the
 *   language is unavailable
 */
LRG_AVAILABLE_IN_ALL
LrgScripting * lrg_scripting_manager_create_context (LrgScriptingManager *self,
                                                     LrgScriptLanguage    language);

/**
 * lrg_scripting_manager_get_display_name:
 * @self: an #LrgScriptingManager
 * @language: a scripting language
 *
 * Returns: (transfer none) (nullable): a human-readable name for @language
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_scripting_manager_get_display_name (LrgScriptingManager *self,
                                                      LrgScriptLanguage    language);

/**
 * lrg_scripting_manager_get_extension:
 * @self: an #LrgScriptingManager
 * @language: a scripting language
 *
 * Returns: (transfer none) (nullable): the canonical file extension (no dot)
 *   for @language, e.g. "lua", "py", "js", "c"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_scripting_manager_get_extension (LrgScriptingManager *self,
                                                   LrgScriptLanguage    language);

/**
 * lrg_scripting_manager_get_available_count:
 * @self: an #LrgScriptingManager
 *
 * Returns: the number of available (compiled-in) language backends
 */
LRG_AVAILABLE_IN_ALL
guint lrg_scripting_manager_get_available_count (LrgScriptingManager *self);

/**
 * lrg_scripting_manager_get_available:
 * @self: an #LrgScriptingManager
 * @n_languages: (out) (optional): return location for the array length
 *
 * Gets the available language backends.
 *
 * Returns: (transfer full) (array length=n_languages): a newly-allocated array
 *   of available #LrgScriptLanguage values (free with g_free())
 */
LRG_AVAILABLE_IN_ALL
LrgScriptLanguage * lrg_scripting_manager_get_available (LrgScriptingManager *self,
                                                         guint               *n_languages);

G_END_DECLS
