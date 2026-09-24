/* lrg-scripting-native.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Native (shared-object) scripting backend and the compiled-script call ABI.
 *
 * LrgScriptingNative loads prebuilt shared objects (.so / .dll) with GModule
 * and gives them the same #LrgScripting surface as the interpreted backends:
 *
 *  - lrg_scripting_call_function() resolves an exported symbol and calls it
 *    through the #LrgNativeFunction ABI;
 *  - lrg_scripting_register_function() publishes a host function that the
 *    compiled code reaches with lrg_scripting_native_call_host();
 *  - globals are a host-side table the compiled code may read and write with
 *    lrg_scripting_get_global() / lrg_scripting_set_global().
 *
 * #LrgScriptingCrispy derives from this class: it compiles C sources with
 * Crispy and then hands the loaded module to the same machinery, so one C
 * source can ship either as a Crispy script or as a prebuilt shared object.
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

/**
 * LrgNativeFunction:
 * @scripting: the #LrgScripting context making the call
 * @n_args: number of arguments
 * @args: (array length=n_args) (nullable): the arguments
 * @return_value: (out caller-allocates): an uninitialized #GValue; leave it
 *   uninitialized for "no value" or g_value_init() it to return one
 * @error: (nullable): return location for an error
 *
 * The calling convention for every function a compiled script exports to its
 * host.  A script declares it with `G_MODULE_EXPORT`, for example:
 *
 * |[<!-- language="C" -->
 * G_MODULE_EXPORT gboolean
 * greet (struct _LrgScripting *scripting, guint n_args, const GValue *args,
 *        GValue *return_value, GError **error)
 * {
 *     g_value_init (return_value, G_TYPE_STRING);
 *     g_value_set_string (return_value, "hello");
 *     return TRUE;
 * }
 * ]|
 *
 * The first parameter is spelled `struct _LrgScripting *` so scripts that do
 * not include libregnum headers can still declare it.
 *
 * Returns: %TRUE on success; %FALSE with @error set on failure
 */
typedef gboolean (*LrgNativeFunction) (LrgScripting  *scripting,
                                       guint          n_args,
                                       const GValue  *args,
                                       GValue        *return_value,
                                       GError       **error);

/**
 * LrgNativeSymbolLookup:
 * @unit: a loaded unit (a #GModule, a Crispy script, ...)
 * @symbol_name: the exported symbol to resolve
 *
 * Resolves @symbol_name in @unit for #LrgScriptingNative subclasses.
 *
 * Returns: (nullable): the symbol address, or %NULL when absent
 */
typedef gpointer (*LrgNativeSymbolLookup) (gpointer     unit,
                                           const gchar *symbol_name);

#define LRG_TYPE_SCRIPTING_NATIVE (lrg_scripting_native_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgScriptingNative, lrg_scripting_native, LRG, SCRIPTING_NATIVE, LrgScripting)

/**
 * LrgScriptingNativeClass:
 * @parent_class: the parent class
 *
 * Class structure for #LrgScriptingNative.  Subclasses override
 * #LrgScriptingClass.load_file / load_string to produce loaded units and hand
 * them over with lrg_scripting_native_add_unit().
 */
struct _LrgScriptingNativeClass
{
    LrgScriptingClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_scripting_native_new:
 *
 * Creates a native scripting context that loads prebuilt shared objects.
 *
 * Returns: (transfer full): a new #LrgScriptingNative
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingNative * lrg_scripting_native_new (void);

/**
 * lrg_scripting_native_call_host:
 * @scripting: the #LrgScripting context passed to the calling function
 * @name: a function registered with lrg_scripting_register_function()
 * @n_args: number of arguments
 * @args: (array length=n_args) (nullable): the arguments
 * @return_value: (out caller-allocates) (nullable): an uninitialized #GValue
 *   that receives the result, or %NULL to discard it
 * @error: (nullable): return location for an error
 *
 * Calls a host function from compiled script code.  This is the native
 * equivalent of calling a registered global in Lua or Python.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_native_call_host (LrgScripting  *scripting,
                                         const gchar   *name,
                                         guint          n_args,
                                         const GValue  *args,
                                         GValue        *return_value,
                                         GError       **error);

/**
 * lrg_scripting_native_add_unit: (skip)
 * @self: an #LrgScriptingNative
 * @unit: (transfer full): the loaded unit
 * @lookup: resolves exported symbols in @unit
 * @destroy: (nullable): releases @unit on reset or finalize
 * @error: (nullable): return location for an error
 *
 * For subclasses: registers a loaded unit.  Later units shadow earlier ones
 * when both export the same symbol.  When the unit exports `main`, it is
 * called as `int main (int argc, char **argv)` with no arguments and must
 * return 0 (the Crispy script convention).
 *
 * Returns: %TRUE when the unit was added and its optional main() succeeded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scripting_native_add_unit (LrgScriptingNative     *self,
                                        gpointer                unit,
                                        LrgNativeSymbolLookup   lookup,
                                        GDestroyNotify          destroy,
                                        GError                **error);

/**
 * lrg_scripting_native_get_search_paths:
 * @self: an #LrgScriptingNative
 *
 * Gets the directories added with lrg_scripting_add_search_path(), for
 * subclasses that compile sources (they become include directories).
 *
 * Returns: (transfer none) (array zero-terminated=1): the directories
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_scripting_native_get_search_paths (LrgScriptingNative *self);

G_END_DECLS
