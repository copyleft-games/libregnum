/* lrg-lua-bridge.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * GObject <-> Lua type conversion bridge.
 *
 * This file provides utilities for converting between GLib/GObject types
 * and Lua values. It handles pushing C values to the Lua stack and
 * extracting C values from Lua stack positions.
 */

#pragma once

#include <glib-object.h>
#include <lua.h>

G_BEGIN_DECLS

/* ==========================================================================
 * GValue <-> Lua Conversion
 * ========================================================================== */

/**
 * lrg_lua_push_gvalue:
 * @L: the Lua state
 * @value: the GValue to push
 *
 * Pushes a GValue onto the Lua stack, converting it to the
 * appropriate Lua type.
 *
 * Type mappings:
 * - G_TYPE_BOOLEAN -> boolean
 * - G_TYPE_INT, G_TYPE_UINT, G_TYPE_INT64, G_TYPE_UINT64 -> number
 * - G_TYPE_FLOAT, G_TYPE_DOUBLE -> number
 * - G_TYPE_STRING -> string
 * - G_TYPE_OBJECT -> userdata with metatable
 * - G_TYPE_NONE, G_TYPE_INVALID -> nil
 *
 * Returns: 1 on success, 0 on unsupported type (nil pushed)
 */
gint lrg_lua_push_gvalue (lua_State    *L,
                          const GValue *value);

/**
 * lrg_lua_to_gvalue:
 * @L: the Lua state
 * @index: stack index to read from
 * @value: (out): GValue to initialize and fill
 *
 * Reads a value from the Lua stack and converts it to a GValue.
 * The GValue is initialized with the appropriate type.
 *
 * Type mappings:
 * - nil -> G_TYPE_NONE
 * - boolean -> G_TYPE_BOOLEAN
 * - number -> G_TYPE_DOUBLE
 * - string -> G_TYPE_STRING
 * - userdata (GObject) -> G_TYPE_OBJECT
 *
 * Returns: %TRUE on success, %FALSE on unsupported type
 */
gboolean lrg_lua_to_gvalue (lua_State *L,
                            gint       index,
                            GValue    *value);

/**
 * lrg_lua_to_gvalue_with_type:
 * @L: the Lua state
 * @index: stack index to read from
 * @type: expected GType
 * @value: (out): GValue to initialize and fill
 *
 * Reads a value from the Lua stack and converts it to a GValue
 * of the specified type, performing type coercion if possible.
 *
 * Returns: %TRUE on success, %FALSE on type mismatch or unsupported type
 */
gboolean lrg_lua_to_gvalue_with_type (lua_State *L,
                                      gint       index,
                                      GType      type,
                                      GValue    *value);

/* ==========================================================================
 * GObject Handling
 * ========================================================================== */

/**
 * lrg_lua_push_gobject:
 * @L: the Lua state
 * @object: (nullable): the GObject to push
 *
 * Pushes a GObject onto the Lua stack as a userdata with a metatable
 * that provides property access and method calls.
 *
 * The object is referenced (g_object_ref) when pushed and will be
 * unreferenced when the Lua userdata is garbage collected.
 *
 * If @object is %NULL, pushes nil.
 *
 * Duplicate pushes of the same object return the same userdata
 * (tracked via a weak table).
 */
void lrg_lua_push_gobject (lua_State *L,
                           GObject   *object);

/**
 * lrg_lua_to_gobject:
 * @L: the Lua state
 * @index: stack index to read from
 *
 * Extracts a GObject from a Lua userdata at the given stack position.
 *
 * Returns: (transfer none) (nullable): the GObject, or %NULL if not a GObject
 */
GObject * lrg_lua_to_gobject (lua_State *L,
                              gint       index);

/**
 * lrg_lua_is_gobject:
 * @L: the Lua state
 * @index: stack index to check
 *
 * Checks if the value at the given stack position is a GObject userdata.
 *
 * Returns: %TRUE if the value is a GObject userdata
 */
gboolean lrg_lua_is_gobject (lua_State *L,
                             gint       index);

/* ==========================================================================
 * Metatable Registration
 * ========================================================================== */

/**
 * lrg_lua_register_gobject_metatable:
 * @L: the Lua state
 *
 * Registers the GObject metatable in the Lua registry.
 * This must be called once before any GObjects are pushed.
 *
 * The metatable provides:
 * - __index: property get and method lookup
 * - __newindex: property set
 * - __gc: release GObject reference
 * - __tostring: GObject type name and pointer
 */
void lrg_lua_register_gobject_metatable (lua_State *L);

/**
 * lrg_lua_register_weak_table:
 * @L: the Lua state
 *
 * Registers the weak table used to track GObject -> userdata mappings.
 * This must be called once before any GObjects are pushed.
 */
void lrg_lua_register_weak_table (lua_State *L);

/* ==========================================================================
 * Signal Connection Support
 * ========================================================================== */

/**
 * lrg_lua_connect_signal:
 * @L: the Lua state
 * @object: the GObject to connect to
 * @signal_name: name of the signal
 * @callback_ref: reference to the Lua callback function (in registry)
 *
 * Connects a Lua callback to a GObject signal.
 *
 * Returns: the signal handler ID, or 0 on failure
 */
gulong lrg_lua_connect_signal (lua_State   *L,
                               GObject     *object,
                               const gchar *signal_name,
                               gint         callback_ref);

/**
 * lrg_lua_disconnect_signal:
 * @L: the Lua state
 * @object: the GObject to disconnect from
 * @handler_id: the signal handler ID returned by lrg_lua_connect_signal
 *
 * Disconnects a Lua callback from a GObject signal.
 */
void lrg_lua_disconnect_signal (lua_State *L,
                                GObject   *object,
                                gulong     handler_id);

G_END_DECLS
