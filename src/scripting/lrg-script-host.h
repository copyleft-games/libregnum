/* lrg-script-host.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Introspectable bridge between C hosts and GI-bound script runtimes.
 *
 * Some runtimes (Gjs) offer no public C API for calling into or out of a
 * script with values.  LrgScriptHost fills that gap through GObject
 * Introspection: every bridged context has a numeric id, the host stashes
 * arguments and registered functions under that id, and the script side
 * calls the functions below (as `imports.gi.Libregnum.script_host_*`) to take
 * arguments, call host functions and hand back results.  Values cross as
 * #GVariant, so strings and numbers are escaped by construction and never
 * spliced into source code.
 *
 * Value mapping (both directions): booleans `b`; integers `x` (or `t` for
 * large unsigned values); floating point `d`; strings `s`; "nothing" (nil,
 * null, undefined, a %NULL pointer) is the empty maybe `ms`.
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
 * lrg_script_host_register: (skip)
 * @scripting: the context the id stands for (not referenced)
 *
 * Allocates a bridge id for @scripting.  Unregister it before @scripting is
 * finalized.
 *
 * Returns: a non-zero id
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_host_register (LrgScripting *scripting);

/**
 * lrg_script_host_enter_script: (skip)
 *
 * Marks the start of an interpreter run whose own logging (for example Gjs
 * reporting a script exception as a warning) must not abort the process:
 * CRITICAL and WARNING leave the always-fatal mask.  Host functions called
 * from that run still execute under the mask the outermost caller had, so
 * engine CRITICALs stay fatal where the caller wanted them to be.
 *
 * Returns: the mask to hand back to lrg_script_host_leave_script()
 */
LRG_AVAILABLE_IN_ALL
GLogLevelFlags lrg_script_host_enter_script (void);

/**
 * lrg_script_host_leave_script: (skip)
 * @previous: the value lrg_script_host_enter_script() returned
 *
 * Ends a run started with lrg_script_host_enter_script().
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_host_leave_script (GLogLevelFlags previous);

/**
 * lrg_script_host_unregister: (skip)
 * @id: a bridge id
 *
 * Forgets @id with its stashed arguments, result, functions and objects.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_host_unregister (guint id);

/**
 * lrg_script_host_add_function: (skip)
 * @id: a bridge id
 * @name: the script-visible name
 * @func: the host function
 * @user_data: data for @func
 *
 * Publishes a host function for lrg_script_host_call().
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_host_add_function (guint                  id,
                                   const gchar           *name,
                                   LrgScriptingCFunction  func,
                                   gpointer               user_data);

/**
 * lrg_script_host_add_object: (skip)
 * @id: a bridge id
 * @name: the script-visible name
 * @object: the object; a reference is kept until unregister
 *
 * Publishes an object for lrg_script_host_get_object().
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_host_add_object (guint        id,
                                 const gchar *name,
                                 GObject     *object);

/**
 * lrg_script_host_stash_args: (skip)
 * @id: a bridge id
 * @args: (transfer none): an `av` array for the script to take
 *
 * Stores arguments for the next lrg_script_host_take_args().
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_host_stash_args (guint     id,
                                 GVariant *args);

/**
 * lrg_script_host_steal_result: (skip)
 * @id: a bridge id
 *
 * Takes the value the script last handed back with
 * lrg_script_host_set_result().
 *
 * Returns: (transfer full) (nullable): the result, or %NULL if none was set
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_script_host_steal_result (guint id);

/**
 * lrg_script_host_take_args:
 * @id: a bridge id
 *
 * Script side: takes the arguments the host stashed for this call.
 *
 * Returns: (transfer full) (nullable): an `av` array, or %NULL when none
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_script_host_take_args (guint id);

/**
 * lrg_script_host_set_result:
 * @id: a bridge id
 * @result: (nullable): the value to hand back to the host
 *
 * Script side: hands a value back to the host.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_host_set_result (guint     id,
                                 GVariant *result);

/**
 * lrg_script_host_call:
 * @id: a bridge id
 * @name: a function published with lrg_script_host_add_function()
 * @args: an `av` array of arguments
 * @error: return location for an error
 *
 * Script side: calls a host function.
 *
 * Returns: (transfer full) (nullable): the result, or %NULL for "no value"
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_script_host_call (guint         id,
                                 const gchar  *name,
                                 GVariant     *args,
                                 GError      **error);

/**
 * lrg_script_host_get_object:
 * @id: a bridge id
 * @name: an object published with lrg_script_host_add_object()
 *
 * Script side: fetches a published object.
 *
 * Returns: (transfer none) (nullable): the object
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_script_host_get_object (guint        id,
                                      const gchar *name);

/**
 * lrg_script_host_value_to_variant: (skip)
 * @value: (nullable): a #GValue
 *
 * Converts a scalar #GValue for the bridge.  Unset values, %NULL pointers
 * and unsupported types become the empty maybe `ms`.
 *
 * Returns: (transfer full): a new floating-free variant
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_script_host_value_to_variant (const GValue *value);

/**
 * lrg_script_host_variant_to_value: (skip)
 * @variant: a bridge variant (a `v` box is unwrapped first)
 * @value: (out caller-allocates): an uninitialized #GValue
 *
 * Converts a bridge value to a #GValue.  "Nothing" becomes a %NULL
 * %G_TYPE_POINTER value.
 *
 * Returns: %TRUE on success, %FALSE for unsupported types
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_host_variant_to_value (GVariant *variant,
                                           GValue   *value);

G_END_DECLS
