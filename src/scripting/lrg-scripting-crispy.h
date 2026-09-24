/* lrg-scripting-crispy.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Crispy (compiled-C) scripting backend.
 *
 * LrgScriptingCrispy compiles C sources with the Crispy embedded
 * C-scripting library and loads the result through #LrgScriptingNative, so
 * a compiled script behaves like any other backend:
 *
 *  - an exported main(), if present, runs once at load (and must return 0);
 *  - exported #LrgNativeFunction symbols are callable with
 *    lrg_scripting_call_function();
 *  - registered host functions are reached with
 *    lrg_scripting_native_call_host();
 *  - directories added with lrg_scripting_add_search_path() become `-I`
 *    include directories.
 *
 * Compilation needs gcc, pkg-config and the GLib development headers at
 * runtime.  Results are cached (by default under `~/.cache/crispy/`), so an
 * unchanged source is compiled once.  This backend is only built when
 * libregnum is configured with Crispy support (HAS_CRISPY=1 /
 * -DLRG_HAS_CRISPY).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-scripting-native.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_CRISPY (lrg_scripting_crispy_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptingCrispy, lrg_scripting_crispy, LRG, SCRIPTING_CRISPY, LrgScriptingNative)

/**
 * lrg_scripting_crispy_new:
 *
 * Creates a new Crispy scripting context.
 *
 * Returns: (transfer full): a new #LrgScriptingCrispy
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingCrispy * lrg_scripting_crispy_new (void);

/**
 * lrg_scripting_crispy_add_cflags:
 * @self: an #LrgScriptingCrispy
 * @cflags: extra compiler flags, in shell syntax
 *
 * Appends compiler flags used for every later load (for example `-DFOO=1`
 * or `$(pkg-config --cflags json-glib-1.0)` expanded by the caller).  A
 * script's own `#define CRISPY_PARAMS` still overrides them.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_crispy_add_cflags (LrgScriptingCrispy *self,
                                      const gchar        *cflags);

/**
 * lrg_scripting_crispy_set_cache_dir:
 * @self: an #LrgScriptingCrispy
 * @cache_dir: (type filename) (nullable): the compile cache directory, or
 *   %NULL for Crispy's default
 *
 * Chooses where compiled objects are cached.  Must be called before the
 * first load.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scripting_crispy_set_cache_dir (LrgScriptingCrispy *self,
                                         const gchar        *cache_dir);

G_END_DECLS
