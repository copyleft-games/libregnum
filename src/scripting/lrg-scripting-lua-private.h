/* lrg-scripting-lua-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private types and functions for LuaJIT scripting backend.
 */

#pragma once

#include <glib-object.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "lrg-scripting-lua.h"

G_BEGIN_DECLS

/*
 * RegisteredCFunction:
 *
 * Internal structure to track C functions registered with Lua.
 * This is stored as userdata so we can access it from the Lua closure.
 */
typedef struct
{
    LrgScriptingLua       *scripting;   /* Weak reference to scripting context */
    LrgScriptingCFunction  func;        /* The C function to call */
    gpointer               user_data;   /* User data to pass to the function */
} RegisteredCFunction;

/*
 * SignalConnection:
 *
 * Tracks a Lua callback connected to a GObject signal.
 */
typedef struct
{
    GObject     *object;        /* The object the signal is connected to */
    gulong       handler_id;    /* GLib signal handler ID */
    gint         lua_ref;       /* Reference to Lua callback in registry */
    lua_State   *L;             /* Lua state (for callback invocation) */
} SignalConnection;

/*
 * lrg_scripting_lua_get_state:
 * @self: an #LrgScriptingLua
 *
 * Gets the internal Lua state.
 *
 * Returns: (transfer none): the lua_State
 */
lua_State * lrg_scripting_lua_get_state (LrgScriptingLua *self);

G_END_DECLS
