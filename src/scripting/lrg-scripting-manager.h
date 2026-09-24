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
 * LrgScriptingFactoryFunc:
 * @user_data: the user data passed to lrg_scripting_manager_register_backend()
 *
 * Factory callback that constructs a fresh #LrgScripting context for a
 * dynamically-registered backend.
 *
 * This is the extension point that lets an embedding application provide a
 * scripting backend libregnum was not compiled with — for example an Emacs
 * Lisp backend supplied by cmacs — without libregnum having to link that
 * language's runtime. The interpreter lives entirely in the embedder.
 *
 * Returns: (transfer full): a new #LrgScripting context
 */
typedef LrgScripting * (*LrgScriptingFactoryFunc) (gpointer user_data);

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

/**
 * lrg_scripting_manager_register_backend:
 * @self: an #LrgScriptingManager
 * @language: the #LrgScriptLanguage this backend provides (must not collide
 *   with a compiled-in backend; typically %LRG_SCRIPT_LANGUAGE_ELISP or a
 *   value libregnum was not built with)
 * @display_name: (transfer none): a human-readable name, e.g. "Emacs Lisp"
 * @extension: (transfer none): the canonical file extension (no dot), e.g. "el"
 * @factory: (scope notified): factory that constructs a fresh context
 * @user_data: (closure factory) (nullable): user data passed to @factory
 * @destroy: (nullable): called on @user_data when the manager is finalized or
 *   the registration is replaced
 *
 * Registers a scripting backend at runtime, in addition to the backends
 * compiled into libregnum. This is the extension point an embedding
 * application uses to plug in an interpreter libregnum does not itself ship
 * (e.g. cmacs registering an Emacs Lisp backend). After registration the
 * language is reported by lrg_scripting_manager_is_available(),
 * lrg_scripting_manager_get_available(), etc., and
 * lrg_scripting_manager_create_context() creates contexts from @factory —
 * so #LrgScriptComponent and #LrgScriptBinding transparently support it.
 *
 * Registering a @language that already has a dynamic registration replaces it.
 *
 * Returns: %TRUE on success, %FALSE if @language is %LRG_SCRIPT_LANGUAGE_NONE,
 *   collides with a compiled-in backend, or @factory is %NULL.
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_manager_register_backend (LrgScriptingManager     *self,
                                                 LrgScriptLanguage        language,
                                                 const gchar             *display_name,
                                                 const gchar             *extension,
                                                 LrgScriptingFactoryFunc  factory,
                                                 gpointer                 user_data,
                                                 GDestroyNotify           destroy);

/**
 * lrg_scripting_manager_language_for_path:
 * @self: the #LrgScriptingManager
 * @path: (type filename): a script path such as `plugin.lua`
 *
 * Maps a file's extension (case-insensitively) to the backend that loads it:
 * `lua`, `py`, `js`, `c` (Crispy), the platform module suffix (native), or
 * an extension an embedder registered.  The result may name a backend this
 * build lacks; check lrg_scripting_manager_is_available() before creating a
 * context, so callers can tell "unknown file" from "backend not built".
 *
 * Returns: the language, or %LRG_SCRIPT_LANGUAGE_NONE for unknown extensions
 */
LRG_AVAILABLE_IN_ALL
LrgScriptLanguage lrg_scripting_manager_language_for_path (LrgScriptingManager *self,
                                                           const gchar         *path);

G_END_DECLS
