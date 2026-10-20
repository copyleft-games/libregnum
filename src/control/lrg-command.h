/* lrg-command.h - Named, typed commands for agents, scripts and tools
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * A command is one thing a game can be asked to do or tell from outside:
 * "status", "walk-to", "attack".  It has a name, a group, help text,
 * typed parameters (described as a JSON schema), and a handler that
 * completes an #LrgCommandCall with a JSON result or a #GError, either
 * at once or later (a walk that ends when the character arrives).
 * Commands live in an #LrgCommandRegistry; the control socket
 * (#LrgControlServer), an MCP bridge or a script call them the same way.
 */

#ifndef LRG_COMMAND_H
#define LRG_COMMAND_H

#include <glib-object.h>
#include <json-glib/json-glib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgCommandParamType:
 * @LRG_COMMAND_PARAM_STRING: a string
 * @LRG_COMMAND_PARAM_INT: an integer (a whole number given as 3 or 3.0)
 * @LRG_COMMAND_PARAM_NUMBER: a number
 * @LRG_COMMAND_PARAM_BOOLEAN: true or false (text: true/false, yes/no,
 *   on/off, 1/0)
 * @LRG_COMMAND_PARAM_OBJECT: a JSON object (text: JSON)
 * @LRG_COMMAND_PARAM_ARRAY: a JSON array (text: JSON, or the rest of
 *   the words with %LRG_COMMAND_PARAM_FLAG_REST)
 * @LRG_COMMAND_PARAM_ANY: any JSON value (text: JSON when it parses,
 *   else the word as a string)
 *
 * The type of a command parameter.  Values are checked (and text
 * arguments converted) before the handler runs.
 */
typedef enum
{
    LRG_COMMAND_PARAM_STRING,
    LRG_COMMAND_PARAM_INT,
    LRG_COMMAND_PARAM_NUMBER,
    LRG_COMMAND_PARAM_BOOLEAN,
    LRG_COMMAND_PARAM_OBJECT,
    LRG_COMMAND_PARAM_ARRAY,
    LRG_COMMAND_PARAM_ANY
} LrgCommandParamType;

LRG_AVAILABLE_IN_ALL
GType lrg_command_param_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COMMAND_PARAM_TYPE (lrg_command_param_type_get_type ())

/**
 * LrgCommandParamFlags:
 * @LRG_COMMAND_PARAM_FLAG_NONE: an optional parameter
 * @LRG_COMMAND_PARAM_FLAG_REQUIRED: the call is refused without it
 * @LRG_COMMAND_PARAM_FLAG_REST: in the text form, the last positional
 *   parameter takes every remaining word (a string joins them with
 *   spaces, an array keeps them as strings)
 *
 * How a parameter is given.
 */
typedef enum /*< flags >*/
{
    LRG_COMMAND_PARAM_FLAG_NONE     = 0,
    LRG_COMMAND_PARAM_FLAG_REQUIRED = 1 << 0,
    LRG_COMMAND_PARAM_FLAG_REST     = 1 << 1
} LrgCommandParamFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_command_param_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COMMAND_PARAM_FLAGS (lrg_command_param_flags_get_type ())

/**
 * LrgCommandFlags:
 * @LRG_COMMAND_FLAG_NONE: nothing special
 * @LRG_COMMAND_FLAG_READ_ONLY: only observes; never changes the game
 *   (an MCP bridge may mark it read-only, a token-less reader may call it)
 * @LRG_COMMAND_FLAG_ASYNC: may complete after the call returns (the
 *   caller waits for #LrgCommandCall::completed)
 * @LRG_COMMAND_FLAG_HIDDEN: left out of listings and generated
 *   references (internal or deprecated commands)
 * @LRG_COMMAND_FLAG_DESTRUCTIVE: ends or overwrites something the user
 *   may want to keep (quit, load over the running game)
 *
 * What kind of command it is.
 */
typedef enum /*< flags >*/
{
    LRG_COMMAND_FLAG_NONE        = 0,
    LRG_COMMAND_FLAG_READ_ONLY   = 1 << 0,
    LRG_COMMAND_FLAG_ASYNC       = 1 << 1,
    LRG_COMMAND_FLAG_HIDDEN      = 1 << 2,
    LRG_COMMAND_FLAG_DESTRUCTIVE = 1 << 3
} LrgCommandFlags;

LRG_AVAILABLE_IN_ALL
GType lrg_command_flags_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COMMAND_FLAGS (lrg_command_flags_get_type ())

/**
 * LrgCommandError:
 * @LRG_COMMAND_ERROR_FAILED: the command ran and failed
 * @LRG_COMMAND_ERROR_UNKNOWN_COMMAND: no command has that name
 * @LRG_COMMAND_ERROR_INVALID_PARAMS: a parameter is missing, unknown or
 *   of the wrong type (the message names it)
 * @LRG_COMMAND_ERROR_NOT_FOUND: what the command names does not exist
 *   (an entity id, an item, a save slot)
 * @LRG_COMMAND_ERROR_REFUSED: the game refuses it now (dead, in a
 *   menu, no game running, not permitted)
 * @LRG_COMMAND_ERROR_UNSUPPORTED: not available in this build or mode
 *   (a screenshot without a window)
 * @LRG_COMMAND_ERROR_CANCELLED: the call was cancelled before it ended
 * @LRG_COMMAND_ERROR_UNAUTHORIZED: the connection has not given the
 *   control token
 *
 * Errors in the %LRG_COMMAND_ERROR domain.  Each maps to a JSON-RPC
 * error code with lrg_command_error_to_jsonrpc_code().
 */
typedef enum
{
    LRG_COMMAND_ERROR_FAILED,
    LRG_COMMAND_ERROR_UNKNOWN_COMMAND,
    LRG_COMMAND_ERROR_INVALID_PARAMS,
    LRG_COMMAND_ERROR_NOT_FOUND,
    LRG_COMMAND_ERROR_REFUSED,
    LRG_COMMAND_ERROR_UNSUPPORTED,
    LRG_COMMAND_ERROR_CANCELLED,
    LRG_COMMAND_ERROR_UNAUTHORIZED
} LrgCommandError;

LRG_AVAILABLE_IN_ALL
GType lrg_command_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COMMAND_ERROR (lrg_command_error_get_type ())

/**
 * LRG_COMMAND_ERROR:
 *
 * The error domain of commands.
 */
#define LRG_COMMAND_ERROR (lrg_command_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_command_error_quark (void);

/**
 * lrg_command_error_to_jsonrpc_code:
 * @error: a #GError
 *
 * The JSON-RPC 2.0 error code for @error: -32601 unknown command,
 * -32602 invalid parameters, -32000 failed (and every error outside the
 * %LRG_COMMAND_ERROR domain), -32001 not found, -32002 refused, -32003
 * unsupported, -32004 cancelled, -32005 unauthorized.
 *
 * Returns: the code
 */
LRG_AVAILABLE_IN_ALL
gint lrg_command_error_to_jsonrpc_code (const GError *error);

/**
 * lrg_command_error_from_jsonrpc_code:
 * @code: a JSON-RPC error code
 *
 * The inverse of lrg_command_error_to_jsonrpc_code() (-32700 and -32600
 * become %LRG_COMMAND_ERROR_INVALID_PARAMS; unknown codes
 * %LRG_COMMAND_ERROR_FAILED).
 *
 * Returns: the #LrgCommandError
 */
LRG_AVAILABLE_IN_ALL
LrgCommandError lrg_command_error_from_jsonrpc_code (gint code);

/* ------------------------------------------------------------------ */
/* LrgCommandParam                                                       */

/**
 * LrgCommandParam:
 *
 * One parameter of a command: a name, a type, flags, a description and
 * optionally a default, a list of allowed strings and a numeric range.
 * Reference counted.
 */
typedef struct _LrgCommandParam LrgCommandParam;

#define LRG_TYPE_COMMAND_PARAM (lrg_command_param_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_command_param_get_type (void) G_GNUC_CONST;

/**
 * lrg_command_param_new:
 * @name: the parameter's name (lower case, `a-z0-9_-`)
 * @type: its type
 * @flags: #LrgCommandParamFlags
 * @description: (nullable): one line for help and the schema
 *
 * Returns: (transfer full): a new parameter
 */
LRG_AVAILABLE_IN_ALL
LrgCommandParam *lrg_command_param_new (const gchar          *name,
                                        LrgCommandParamType   type,
                                        LrgCommandParamFlags  flags,
                                        const gchar          *description);

/**
 * lrg_command_param_ref:
 * @self: an #LrgCommandParam
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgCommandParam *lrg_command_param_ref (LrgCommandParam *self);

/**
 * lrg_command_param_unref:
 * @self: (transfer full): an #LrgCommandParam
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_param_unref (LrgCommandParam *self);

/**
 * lrg_command_param_get_name:
 * @self: an #LrgCommandParam
 *
 * Returns: (transfer none): the name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_param_get_name (LrgCommandParam *self);

/**
 * lrg_command_param_get_param_type:
 * @self: an #LrgCommandParam
 *
 * Returns: the type
 */
LRG_AVAILABLE_IN_ALL
LrgCommandParamType lrg_command_param_get_param_type (LrgCommandParam *self);

/**
 * lrg_command_param_get_flags:
 * @self: an #LrgCommandParam
 *
 * Returns: the flags
 */
LRG_AVAILABLE_IN_ALL
LrgCommandParamFlags lrg_command_param_get_flags (LrgCommandParam *self);

/**
 * lrg_command_param_is_required:
 * @self: an #LrgCommandParam
 *
 * Returns: whether %LRG_COMMAND_PARAM_FLAG_REQUIRED is set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_param_is_required (LrgCommandParam *self);

/**
 * lrg_command_param_get_description:
 * @self: an #LrgCommandParam
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_param_get_description (LrgCommandParam *self);

/**
 * lrg_command_param_set_default:
 * @self: an #LrgCommandParam
 * @value: (nullable): the value used when the parameter is absent
 *   (copied); %NULL for none
 *
 * Handlers see the default as if the caller had given it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_param_set_default (LrgCommandParam *self,
                                    JsonNode        *value);

/**
 * lrg_command_param_get_default:
 * @self: an #LrgCommandParam
 *
 * Returns: (transfer none) (nullable): the default
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_command_param_get_default (LrgCommandParam *self);

/**
 * lrg_command_param_set_choices:
 * @self: an #LrgCommandParam
 * @choices: (nullable) (array zero-terminated=1): the allowed strings
 *   (a string parameter only); %NULL allows any
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_param_set_choices (LrgCommandParam    *self,
                                    const gchar *const *choices);

/**
 * lrg_command_param_get_choices:
 * @self: an #LrgCommandParam
 *
 * Returns: (transfer none) (nullable) (array zero-terminated=1): the
 *   allowed strings
 */
LRG_AVAILABLE_IN_ALL
const gchar *const *lrg_command_param_get_choices (LrgCommandParam *self);

/**
 * lrg_command_param_set_range:
 * @self: an #LrgCommandParam
 * @minimum: the smallest value allowed
 * @maximum: the largest value allowed
 *
 * Limits an int or number parameter; values outside are refused.
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_param_set_range (LrgCommandParam *self,
                                  gdouble          minimum,
                                  gdouble          maximum);

/**
 * lrg_command_param_get_range:
 * @self: an #LrgCommandParam
 * @minimum: (out) (optional): the minimum
 * @maximum: (out) (optional): the maximum
 *
 * Returns: %TRUE when a range was set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_param_get_range (LrgCommandParam *self,
                                      gdouble         *minimum,
                                      gdouble         *maximum);

/**
 * lrg_command_param_to_json_schema:
 * @self: an #LrgCommandParam
 *
 * The parameter as a JSON schema fragment (`type`, `description`,
 * `default`, `enum`, `minimum`, `maximum`).
 *
 * Returns: (transfer full): a JSON object node
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_command_param_to_json_schema (LrgCommandParam *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgCommandParam, lrg_command_param_unref)

/* ------------------------------------------------------------------ */
/* LrgCommandCall                                                        */

#define LRG_TYPE_COMMAND_CALL (lrg_command_call_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCommandCall, lrg_command_call, LRG, COMMAND_CALL, GObject)

#define LRG_TYPE_COMMAND (lrg_command_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCommand, lrg_command, LRG, COMMAND, GObject)

/**
 * lrg_command_call_new:
 * @command: (nullable): the command (referenced); %NULL for a call that
 *   failed before a command was found
 * @params: (nullable): the checked parameters (referenced); %NULL for
 *   none
 * @origin: (nullable): who calls ("socket", "mcp", "script", "test")
 *
 * Usually made by lrg_command_registry_invoke(); public for commands
 * that call other commands and for tests.
 *
 * Returns: (transfer full): a new, pending call
 */
LRG_AVAILABLE_IN_ALL
LrgCommandCall *lrg_command_call_new (LrgCommand  *command,
                                      JsonObject  *params,
                                      const gchar *origin);

/**
 * lrg_command_call_get_command:
 * @self: an #LrgCommandCall
 *
 * Returns: (transfer none) (nullable): the command
 */
LRG_AVAILABLE_IN_ALL
LrgCommand *lrg_command_call_get_command (LrgCommandCall *self);

/**
 * lrg_command_call_get_name:
 * @self: an #LrgCommandCall
 *
 * Returns: (transfer none) (nullable): the command's name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_call_get_name (LrgCommandCall *self);

/**
 * lrg_command_call_get_origin:
 * @self: an #LrgCommandCall
 *
 * Returns: (transfer none) (nullable): who called
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_call_get_origin (LrgCommandCall *self);

/**
 * lrg_command_call_get_params:
 * @self: an #LrgCommandCall
 *
 * The parameters after checking: every given one converted to its
 * type, defaults filled in.
 *
 * Returns: (transfer none): the parameters (an empty object for none)
 */
LRG_AVAILABLE_IN_ALL
JsonObject *lrg_command_call_get_params (LrgCommandCall *self);

/**
 * lrg_command_call_has_param:
 * @self: an #LrgCommandCall
 * @name: a parameter name
 *
 * Returns: whether @name was given (or has a default) and is not null
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_call_has_param (LrgCommandCall *self,
                                     const gchar    *name);

/**
 * lrg_command_call_get_string:
 * @self: an #LrgCommandCall
 * @name: a parameter name
 *
 * Returns: (transfer none) (nullable): the string, or %NULL when absent
 *   or not a string
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_call_get_string (LrgCommandCall *self,
                                          const gchar    *name);

/**
 * lrg_command_call_get_int:
 * @self: an #LrgCommandCall
 * @name: a parameter name
 * @fallback: returned when absent
 *
 * Returns: the integer
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_command_call_get_int (LrgCommandCall *self,
                                 const gchar    *name,
                                 gint64          fallback);

/**
 * lrg_command_call_get_double:
 * @self: an #LrgCommandCall
 * @name: a parameter name
 * @fallback: returned when absent
 *
 * Returns: the number
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_command_call_get_double (LrgCommandCall *self,
                                     const gchar    *name,
                                     gdouble         fallback);

/**
 * lrg_command_call_get_boolean:
 * @self: an #LrgCommandCall
 * @name: a parameter name
 * @fallback: returned when absent
 *
 * Returns: the boolean
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_call_get_boolean (LrgCommandCall *self,
                                       const gchar    *name,
                                       gboolean        fallback);

/**
 * lrg_command_call_get_node:
 * @self: an #LrgCommandCall
 * @name: a parameter name
 *
 * Returns: (transfer none) (nullable): the raw value
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_command_call_get_node (LrgCommandCall *self,
                                     const gchar    *name);

/**
 * lrg_command_call_return_value:
 * @self: an #LrgCommandCall
 * @result: (transfer full) (nullable): the result; %NULL for JSON null
 *
 * Completes the call successfully and emits
 * #LrgCommandCall::completed.  A call completes once; later returns
 * (for instance after a cancel) are ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_call_return_value (LrgCommandCall *self,
                                    JsonNode       *result);

/**
 * lrg_command_call_return_object:
 * @self: an #LrgCommandCall
 * @result: (transfer full): the result object
 *
 * lrg_command_call_return_value() with an object.
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_call_return_object (LrgCommandCall *self,
                                     JsonObject     *result);

/**
 * lrg_command_call_return_error:
 * @self: an #LrgCommandCall
 * @error: (transfer full): why it failed
 *
 * Completes the call with an error and emits #LrgCommandCall::completed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_call_return_error (LrgCommandCall *self,
                                    GError         *error);

/**
 * lrg_command_call_return_new_error:
 * @self: an #LrgCommandCall
 * @code: an #LrgCommandError
 * @format: printf-style message
 * @...: arguments
 *
 * Completes the call with a new %LRG_COMMAND_ERROR error.
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_call_return_new_error (LrgCommandCall  *self,
                                        LrgCommandError  code,
                                        const gchar     *format,
                                        ...) G_GNUC_PRINTF (3, 4);

/**
 * lrg_command_call_is_done:
 * @self: an #LrgCommandCall
 *
 * Returns: whether the call completed (with a result or an error)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_call_is_done (LrgCommandCall *self);

/**
 * lrg_command_call_get_result:
 * @self: an #LrgCommandCall
 *
 * Returns: (transfer none) (nullable): the result of a successful call
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_command_call_get_result (LrgCommandCall *self);

/**
 * lrg_command_call_get_error:
 * @self: an #LrgCommandCall
 *
 * Returns: (transfer none) (nullable): the error of a failed call
 */
LRG_AVAILABLE_IN_ALL
const GError *lrg_command_call_get_error (LrgCommandCall *self);

/**
 * lrg_command_call_propagate:
 * @self: a completed #LrgCommandCall
 * @error: return location for a #GError
 *
 * The outcome as a return value: the result (copied) or the error.  A
 * pending call fails with %G_IO_ERROR_PENDING.
 *
 * Returns: (transfer full) (nullable): a copy of the result, %NULL on
 *   error
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_command_call_propagate (LrgCommandCall  *self,
                                      GError         **error);

/**
 * lrg_command_call_cancel:
 * @self: an #LrgCommandCall
 *
 * Asks a pending call to stop: emits #LrgCommandCall::cancelled (the
 * handler stops what it started) and, when the handler has not
 * completed it from there, completes it with
 * %LRG_COMMAND_ERROR_CANCELLED.  Does nothing to a completed call.
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_call_cancel (LrgCommandCall *self);

/**
 * lrg_command_call_is_cancelled:
 * @self: an #LrgCommandCall
 *
 * Returns: whether lrg_command_call_cancel() was called
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_call_is_cancelled (LrgCommandCall *self);

/* ------------------------------------------------------------------ */
/* LrgCommand                                                            */

/**
 * LrgCommandHandler:
 * @command: the command
 * @call: the call to complete (now or later)
 * @user_data: the handler's data
 *
 * Runs a command.  The handler completes @call with
 * lrg_command_call_return_value() or lrg_command_call_return_error(),
 * before returning or later (keep a reference to @call); an
 * %LRG_COMMAND_FLAG_ASYNC command that finishes later should watch
 * #LrgCommandCall::cancelled.
 */
typedef void (*LrgCommandHandler) (LrgCommand     *command,
                                   LrgCommandCall *call,
                                   gpointer        user_data);

/**
 * LrgCommandClass:
 * @parent_class: the parent class
 * @invoke: runs the command for a checked call; the default calls the
 *   handler given to lrg_command_new() (and fails the call without one)
 *
 * Subclass to implement a command as a type instead of a handler.
 */
struct _LrgCommandClass
{
    GObjectClass parent_class;

    void (*invoke) (LrgCommand     *self,
                    LrgCommandCall *call);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_command_new:
 * @name: the command's name (lower case words joined by `-` or `.`:
 *   "status", "walk-to", "window.focus")
 * @group: (nullable): its group for listings ("observe", "act")
 * @summary: one line of help
 * @handler: (scope notified) (closure user_data) (destroy destroy) (nullable): what runs it
 * @user_data: data for @handler
 * @destroy: (nullable): frees @user_data
 *
 * Returns: (transfer full): a new command without parameters
 */
LRG_AVAILABLE_IN_ALL
LrgCommand *lrg_command_new (const gchar       *name,
                             const gchar       *group,
                             const gchar       *summary,
                             LrgCommandHandler  handler,
                             gpointer           user_data,
                             GDestroyNotify     destroy);

/**
 * lrg_command_get_name:
 * @self: an #LrgCommand
 *
 * Returns: (transfer none): the name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_get_name (LrgCommand *self);

/**
 * lrg_command_get_group:
 * @self: an #LrgCommand
 *
 * Returns: (transfer none) (nullable): the group
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_get_group (LrgCommand *self);

/**
 * lrg_command_get_summary:
 * @self: an #LrgCommand
 *
 * Returns: (transfer none) (nullable): the one-line help
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_get_summary (LrgCommand *self);

/**
 * lrg_command_set_description:
 * @self: an #LrgCommand
 * @description: (nullable): longer help: what it does, what it returns,
 *   when it fails
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_set_description (LrgCommand  *self,
                                  const gchar *description);

/**
 * lrg_command_get_description:
 * @self: an #LrgCommand
 *
 * Returns: (transfer none) (nullable): the longer help
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_get_description (LrgCommand *self);

/**
 * lrg_command_set_flags:
 * @self: an #LrgCommand
 * @flags: #LrgCommandFlags
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_set_flags (LrgCommand      *self,
                            LrgCommandFlags  flags);

/**
 * lrg_command_get_flags:
 * @self: an #LrgCommand
 *
 * Returns: the flags
 */
LRG_AVAILABLE_IN_ALL
LrgCommandFlags lrg_command_get_flags (LrgCommand *self);

/**
 * lrg_command_set_example:
 * @self: an #LrgCommand
 * @example: (nullable): a text-form example ("walk-to 12 0 4.5")
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_set_example (LrgCommand  *self,
                              const gchar *example);

/**
 * lrg_command_get_example:
 * @self: an #LrgCommand
 *
 * Returns: (transfer none) (nullable): the example
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_command_get_example (LrgCommand *self);

/**
 * lrg_command_add_param:
 * @self: an #LrgCommand
 * @param: (transfer none): the next parameter (referenced); positional
 *   text arguments fill parameters in the order they were added
 *
 * Returns: %FALSE when a parameter of that name exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_command_add_param (LrgCommand      *self,
                                LrgCommandParam *param);

/**
 * lrg_command_add_param_full:
 * @self: an #LrgCommand
 * @name: the parameter's name
 * @type: its type
 * @flags: #LrgCommandParamFlags
 * @description: (nullable): one line of help
 *
 * Makes and adds a parameter; set its default, choices or range on the
 * returned parameter.
 *
 * Returns: (transfer none): the new parameter
 */
LRG_AVAILABLE_IN_ALL
LrgCommandParam *lrg_command_add_param_full (LrgCommand           *self,
                                             const gchar          *name,
                                             LrgCommandParamType   type,
                                             LrgCommandParamFlags  flags,
                                             const gchar          *description);

/**
 * lrg_command_get_params:
 * @self: an #LrgCommand
 *
 * Returns: (transfer none) (element-type LrgCommandParam): the
 *   parameters in order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_command_get_params (LrgCommand *self);

/**
 * lrg_command_find_param:
 * @self: an #LrgCommand
 * @name: a parameter name
 *
 * Returns: (transfer none) (nullable): the parameter
 */
LRG_AVAILABLE_IN_ALL
LrgCommandParam *lrg_command_find_param (LrgCommand  *self,
                                         const gchar *name);

/**
 * lrg_command_get_input_schema:
 * @self: an #LrgCommand
 *
 * The parameters as a JSON schema object (`type: object`,
 * `properties`, `required`, `additionalProperties: false`): what an
 * MCP tool's `inputSchema` holds.
 *
 * Returns: (transfer full): a JSON object node
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_command_get_input_schema (LrgCommand *self);

/**
 * lrg_command_describe:
 * @self: an #LrgCommand
 *
 * Everything about the command as JSON: `name`, `group`, `summary`,
 * `description`, `example`, `read_only`, `async`, `destructive`,
 * `params` (array of `{name, type, required, description, default?,
 * enum?, minimum?, maximum?}`) and `schema` (the input schema).
 *
 * Returns: (transfer full): a JSON object node
 */
LRG_AVAILABLE_IN_ALL
JsonNode *lrg_command_describe (LrgCommand *self);

/**
 * lrg_command_check_params:
 * @self: an #LrgCommand
 * @params: (nullable): the given parameters
 * @error: return location for a #GError
 *
 * Checks @params against the declaration: unknown names, missing
 * required ones, types (an int given as 3.0 is accepted, a number as
 * an int is converted), choices and ranges.  Defaults fill the absent
 * ones; null members count as absent.
 *
 * Returns: (transfer full) (nullable): the checked parameters, %NULL
 *   with %LRG_COMMAND_ERROR_INVALID_PARAMS
 */
LRG_AVAILABLE_IN_ALL
JsonObject *lrg_command_check_params (LrgCommand  *self,
                                      JsonObject  *params,
                                      GError     **error);

/**
 * lrg_command_parse_arguments:
 * @self: an #LrgCommand
 * @argv: (array zero-terminated=1): the words after the name
 * @error: return location for a #GError
 *
 * Text arguments to parameters: `name=value` sets a named parameter;
 * other words fill the parameters in order (a word that starts with
 * `name=` for an unknown name is positional).  Values are converted to
 * the parameter's type; the result still goes through
 * lrg_command_check_params().
 *
 * Returns: (transfer full) (nullable): the parameters, %NULL with
 *   %LRG_COMMAND_ERROR_INVALID_PARAMS
 */
LRG_AVAILABLE_IN_ALL
JsonObject *lrg_command_parse_arguments (LrgCommand         *self,
                                         const gchar *const *argv,
                                         GError            **error);

/**
 * lrg_command_invoke:
 * @self: an #LrgCommand
 * @call: a pending call whose parameters were checked
 *
 * Runs the command (the #LrgCommandClass.invoke vfunc).  A handler that
 * throws nothing and does not complete a call of a command without
 * %LRG_COMMAND_FLAG_ASYNC leaves it pending; the registry then completes
 * it with a null result.
 */
LRG_AVAILABLE_IN_ALL
void lrg_command_invoke (LrgCommand     *self,
                         LrgCommandCall *call);

G_END_DECLS

#endif /* LRG_COMMAND_H */
