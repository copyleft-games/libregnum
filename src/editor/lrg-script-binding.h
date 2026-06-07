/* lrg-script-binding.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Serializable binding of a script to an editor node.
 *
 * LrgScriptBinding records a script (by file path or asset guid) authored in
 * a given #LrgScriptLanguage that should be attached to the owning node's game
 * object at play time as an LrgScriptComponent. It is data-only and carries no
 * live interpreter state.
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

#define LRG_TYPE_SCRIPT_BINDING (lrg_script_binding_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptBinding, lrg_script_binding, LRG, SCRIPT_BINDING, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_script_binding_new:
 * @language: the scripting language
 * @script: (nullable): the script path or asset guid
 *
 * Creates a new #LrgScriptBinding, enabled by default.
 *
 * Returns: (transfer full): a new #LrgScriptBinding
 */
LRG_AVAILABLE_IN_ALL
LrgScriptBinding * lrg_script_binding_new (LrgScriptLanguage  language,
                                           const gchar       *script);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_script_binding_get_language:
 * @self: an #LrgScriptBinding
 *
 * Gets the scripting language of the binding.
 *
 * Returns: the #LrgScriptLanguage
 */
LRG_AVAILABLE_IN_ALL
LrgScriptLanguage lrg_script_binding_get_language (LrgScriptBinding *self);

/**
 * lrg_script_binding_set_language:
 * @self: an #LrgScriptBinding
 * @language: the scripting language
 *
 * Sets the scripting language of the binding.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_binding_set_language (LrgScriptBinding  *self,
                                      LrgScriptLanguage  language);

/**
 * lrg_script_binding_get_script:
 * @self: an #LrgScriptBinding
 *
 * Gets the script path or asset guid.
 *
 * Returns: (transfer none) (nullable): the script reference
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_binding_get_script (LrgScriptBinding *self);

/**
 * lrg_script_binding_set_script:
 * @self: an #LrgScriptBinding
 * @script: (nullable): the script path or asset guid
 *
 * Sets the script path or asset guid.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_binding_set_script (LrgScriptBinding *self,
                                    const gchar      *script);

/**
 * lrg_script_binding_get_enabled:
 * @self: an #LrgScriptBinding
 *
 * Gets whether the binding is enabled.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_binding_get_enabled (LrgScriptBinding *self);

/**
 * lrg_script_binding_set_enabled:
 * @self: an #LrgScriptBinding
 * @enabled: whether the binding is enabled
 *
 * Sets whether the binding is enabled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_binding_set_enabled (LrgScriptBinding *self,
                                     gboolean          enabled);

G_END_DECLS
