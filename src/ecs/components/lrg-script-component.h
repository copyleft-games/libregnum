/* lrg-script-component.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A component that binds a script to its owning game object.
 *
 * LrgScriptComponent is the runtime materialization of an editor node's script
 * binding. When attached, it asks the #LrgScriptingManager for a context in the
 * configured #LrgScriptLanguage, loads the script, and drives its lifecycle
 * hooks ("lrg_script_start" / "lrg_script_update" / "lrg_script_detach", see
 * lrg-script-module.h). If the language is unavailable in this build the
 * component is simply inert.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../lrg-enums.h"
#include "../lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPT_COMPONENT (lrg_script_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptComponent, lrg_script_component, LRG, SCRIPT_COMPONENT, LrgComponent)

/**
 * lrg_script_component_new:
 * @language: the scripting language
 * @script: (nullable): the script path
 *
 * Returns: (transfer full): a new #LrgScriptComponent
 */
LRG_AVAILABLE_IN_ALL
LrgScriptComponent * lrg_script_component_new (LrgScriptLanguage  language,
                                               const gchar       *script);

/**
 * lrg_script_component_get_language:
 * @self: an #LrgScriptComponent
 *
 * Returns: the scripting language
 */
LRG_AVAILABLE_IN_ALL
LrgScriptLanguage lrg_script_component_get_language (LrgScriptComponent *self);

/**
 * lrg_script_component_set_language:
 * @self: an #LrgScriptComponent
 * @language: the scripting language
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_component_set_language (LrgScriptComponent *self,
                                        LrgScriptLanguage   language);

/**
 * lrg_script_component_get_script:
 * @self: an #LrgScriptComponent
 *
 * Returns: (transfer none) (nullable): the script path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_component_get_script (LrgScriptComponent *self);

/**
 * lrg_script_component_set_script:
 * @self: an #LrgScriptComponent
 * @script: (nullable): the script path
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_component_set_script (LrgScriptComponent *self,
                                      const gchar        *script);

/**
 * lrg_script_component_get_context:
 * @self: an #LrgScriptComponent
 *
 * Gets the live scripting context, if the component is attached and the
 * language is available.
 *
 * Returns: (transfer none) (nullable): the #LrgScripting context, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgScripting * lrg_script_component_get_context (LrgScriptComponent *self);

G_END_DECLS
