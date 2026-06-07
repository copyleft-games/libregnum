/* lrg-scripting-crispy.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Crispy (compiled-C) scripting backend.
 *
 * LrgScriptingCrispy is an #LrgScripting backend over the Crispy embedded
 * C-scripting library. Unlike the interpreter backends (Lua/Python/Gjs),
 * Crispy compiles a C source to a shared object and runs its entry point, so
 * this backend maps load_file()/load_string() to "compile and run". Per-function
 * calls and global get/set are not part of Crispy's model and are reported as
 * unsupported. This backend is only built when libregnum is configured with
 * Crispy support (HAS_CRISPY=1 / -DLRG_HAS_CRISPY).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-scripting.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_CRISPY (lrg_scripting_crispy_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptingCrispy, lrg_scripting_crispy, LRG, SCRIPTING_CRISPY, LrgScripting)

/**
 * lrg_scripting_crispy_new:
 *
 * Creates a new Crispy scripting context.
 *
 * Returns: (transfer full): a new #LrgScriptingCrispy
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingCrispy * lrg_scripting_crispy_new (void);

G_END_DECLS
