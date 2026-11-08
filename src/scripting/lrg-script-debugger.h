/* lrg-script-debugger.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptDebugger: a backend-neutral source-level debugger for the
 * scripting backends (breakpoints, stepping, stack frames, scopes,
 * variables, evaluate, exception stops).  Backends plug in through the
 * #LrgScriptDebuggable interface; front ends (the DAP server, the editor's
 * debug panel, tool scripts) drive the debugger through its thread-safe
 * control API.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-scripting.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations and errors
 * ========================================================================== */

/**
 * LrgScriptDebuggerState:
 * @LRG_SCRIPT_DEBUGGER_STATE_RUNNING: scripts run normally (hooks may be
 *   armed for breakpoints)
 * @LRG_SCRIPT_DEBUGGER_STATE_PAUSED: a script thread is stopped inside the
 *   debugger; inspection calls are valid
 *
 * Execution state of an #LrgScriptDebugger.
 */
typedef enum
{
	LRG_SCRIPT_DEBUGGER_STATE_RUNNING,
	LRG_SCRIPT_DEBUGGER_STATE_PAUSED
} LrgScriptDebuggerState;

LRG_AVAILABLE_IN_ALL
GType lrg_script_debugger_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SCRIPT_DEBUGGER_STATE (lrg_script_debugger_state_get_type ())

/**
 * LrgScriptStopReason:
 * @LRG_SCRIPT_STOP_REASON_NONE: not stopped
 * @LRG_SCRIPT_STOP_REASON_BREAKPOINT: a line or node breakpoint was hit
 * @LRG_SCRIPT_STOP_REASON_STEP: a step (in, over, out) finished
 * @LRG_SCRIPT_STOP_REASON_PAUSE: lrg_script_debugger_pause() was honoured
 * @LRG_SCRIPT_STOP_REASON_EXCEPTION: a script error was raised (see
 *   #LrgScriptExceptionMode)
 *
 * Why the debugger stopped.  The names match the Debug Adapter Protocol
 * `stopped` event reasons ("breakpoint", "step", "pause", "exception");
 * lrg_script_stop_reason_to_string() returns them.
 */
typedef enum
{
	LRG_SCRIPT_STOP_REASON_NONE,
	LRG_SCRIPT_STOP_REASON_BREAKPOINT,
	LRG_SCRIPT_STOP_REASON_STEP,
	LRG_SCRIPT_STOP_REASON_PAUSE,
	LRG_SCRIPT_STOP_REASON_EXCEPTION
} LrgScriptStopReason;

LRG_AVAILABLE_IN_ALL
GType lrg_script_stop_reason_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SCRIPT_STOP_REASON (lrg_script_stop_reason_get_type ())

/**
 * lrg_script_stop_reason_to_string:
 * @reason: an #LrgScriptStopReason
 *
 * Returns the Debug Adapter Protocol name of @reason ("breakpoint",
 * "step", "pause", "exception"; "none" for %LRG_SCRIPT_STOP_REASON_NONE).
 *
 * Returns: (transfer none): a static string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_stop_reason_to_string (LrgScriptStopReason reason);

/**
 * LrgScriptExceptionMode:
 * @LRG_SCRIPT_EXCEPTION_MODE_NEVER: script errors never stop
 * @LRG_SCRIPT_EXCEPTION_MODE_UNCAUGHT: stop when an error is about to
 *   escape the script to the host (Lua: the error handler of the host's
 *   protected call; Python: an exception unwinding the outermost script
 *   frame)
 * @LRG_SCRIPT_EXCEPTION_MODE_ALL: stop at every raise (Python); Lua only
 *   sees errors that reach the host, so it behaves like UNCAUGHT
 *
 * When script errors stop the debugger.  Maps onto the DAP exception
 * breakpoint filters "uncaught" and "raised".
 */
typedef enum
{
	LRG_SCRIPT_EXCEPTION_MODE_NEVER,
	LRG_SCRIPT_EXCEPTION_MODE_UNCAUGHT,
	LRG_SCRIPT_EXCEPTION_MODE_ALL
} LrgScriptExceptionMode;

LRG_AVAILABLE_IN_ALL
GType lrg_script_exception_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SCRIPT_EXCEPTION_MODE (lrg_script_exception_mode_get_type ())

/**
 * LRG_SCRIPT_DEBUGGER_ERROR:
 *
 * Error domain for #LrgScriptDebugger.
 */
#define LRG_SCRIPT_DEBUGGER_ERROR (lrg_script_debugger_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_script_debugger_error_quark (void);

/**
 * LrgScriptDebuggerError:
 * @LRG_SCRIPT_DEBUGGER_ERROR_FAILED: generic failure
 * @LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED: the scripting backend cannot be
 *   debugged (crispy, native, Gjs: see the module docs)
 * @LRG_SCRIPT_DEBUGGER_ERROR_ALREADY_ATTACHED: another debugger owns the
 *   backend's hooks (Python's sys.monitoring tool slot is per interpreter)
 * @LRG_SCRIPT_DEBUGGER_ERROR_NOT_PAUSED: an inspection call was made while
 *   no script is stopped
 * @LRG_SCRIPT_DEBUGGER_ERROR_INVALID_REFERENCE: a frame id or variables
 *   reference is unknown or stale (references die on resume)
 * @LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE: evaluating an expression failed
 *   (syntax or runtime error; the message is the backend's)
 * @LRG_SCRIPT_DEBUGGER_ERROR_TIMEOUT: a request from another thread was not
 *   served by the stopped script thread in time
 *
 * Error codes in the %LRG_SCRIPT_DEBUGGER_ERROR domain.
 */
typedef enum
{
	LRG_SCRIPT_DEBUGGER_ERROR_FAILED,
	LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED,
	LRG_SCRIPT_DEBUGGER_ERROR_ALREADY_ATTACHED,
	LRG_SCRIPT_DEBUGGER_ERROR_NOT_PAUSED,
	LRG_SCRIPT_DEBUGGER_ERROR_INVALID_REFERENCE,
	LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE,
	LRG_SCRIPT_DEBUGGER_ERROR_TIMEOUT
} LrgScriptDebuggerError;

LRG_AVAILABLE_IN_ALL
GType lrg_script_debugger_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SCRIPT_DEBUGGER_ERROR (lrg_script_debugger_error_get_type ())

/* ==========================================================================
 * LrgScriptBreakpoint
 * ========================================================================== */

#define LRG_TYPE_SCRIPT_BREAKPOINT (lrg_script_breakpoint_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptBreakpoint, lrg_script_breakpoint, LRG, SCRIPT_BREAKPOINT, GObject)

/**
 * lrg_script_breakpoint_get_id:
 * @self: an #LrgScriptBreakpoint
 *
 * Returns: the breakpoint's id, unique within its debugger (starts at 1)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_breakpoint_get_id (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_get_file:
 * @self: an #LrgScriptBreakpoint
 *
 * Returns: (transfer none) (type filename): the canonical (absolute) path
 *   the breakpoint is set in
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_breakpoint_get_file (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_get_line:
 * @self: an #LrgScriptBreakpoint
 *
 * Returns: the 1-based line, or 0 for a node breakpoint
 */
LRG_AVAILABLE_IN_ALL
gint lrg_script_breakpoint_get_line (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_get_node_id:
 * @self: an #LrgScriptBreakpoint
 *
 * Returns: (transfer none) (nullable): the visual-script node id of a node
 *   breakpoint, or %NULL for a line breakpoint
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_breakpoint_get_node_id (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_get_condition:
 * @self: an #LrgScriptBreakpoint
 *
 * Returns: (transfer none) (nullable): the condition expression, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_breakpoint_get_condition (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_set_condition:
 * @self: an #LrgScriptBreakpoint
 * @condition: (nullable): an expression in the script's language; the
 *   breakpoint only fires when it evaluates truthy in the stopping frame.
 *   %NULL or "" clears it.  A condition that fails to evaluate fires the
 *   breakpoint and reports the error through #LrgScriptDebugger::output.
 *
 * Sets the breakpoint condition.  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_breakpoint_set_condition (LrgScriptBreakpoint *self,
                                          const gchar         *condition);

/**
 * lrg_script_breakpoint_get_hit_condition:
 * @self: an #LrgScriptBreakpoint
 *
 * Returns: (transfer none) (nullable): the hit-count condition, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_breakpoint_get_hit_condition (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_set_hit_condition:
 * @self: an #LrgScriptBreakpoint
 * @hit_condition: (nullable): "N" or "==N" (the Nth hit), ">N", ">=N",
 *   "<N", "<=N", or "%N" (every Nth hit); %NULL or "" clears it
 *
 * Sets the hit-count condition, tested after the condition (the hit count
 * counts hits whose condition passed).  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_breakpoint_set_hit_condition (LrgScriptBreakpoint *self,
                                              const gchar         *hit_condition);

/**
 * lrg_script_breakpoint_get_log_message:
 * @self: an #LrgScriptBreakpoint
 *
 * Returns: (transfer none) (nullable): the log message, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_breakpoint_get_log_message (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_set_log_message:
 * @self: an #LrgScriptBreakpoint
 * @log_message: (nullable): a message with `{expression}` placeholders;
 *   when set the breakpoint is a log point: it emits
 *   #LrgScriptDebugger::output with the interpolated text and never stops.
 *   %NULL or "" makes it a normal breakpoint again.
 *
 * Turns the breakpoint into a log point.  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_breakpoint_set_log_message (LrgScriptBreakpoint *self,
                                            const gchar         *log_message);

/**
 * lrg_script_breakpoint_get_enabled:
 * @self: an #LrgScriptBreakpoint
 *
 * Returns: whether the breakpoint is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_breakpoint_get_enabled (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_set_enabled:
 * @self: an #LrgScriptBreakpoint
 * @enabled: whether the breakpoint fires
 *
 * Enables or disables the breakpoint without removing it.  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_breakpoint_set_enabled (LrgScriptBreakpoint *self,
                                        gboolean             enabled);

/**
 * lrg_script_breakpoint_get_verified:
 * @self: an #LrgScriptBreakpoint
 *
 * A breakpoint is verified when its file exists (or is a script loaded by
 * an attached backend, for string chunks).  Unverified breakpoints still
 * fire if a matching location ever runs.
 *
 * Returns: whether the breakpoint location was verified
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_breakpoint_get_verified (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_get_hit_count:
 * @self: an #LrgScriptBreakpoint
 *
 * Returns: how many times the location ran with the condition passing
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_breakpoint_get_hit_count (LrgScriptBreakpoint *self);

/**
 * lrg_script_breakpoint_reset_hit_count:
 * @self: an #LrgScriptBreakpoint
 *
 * Resets the hit count to 0.  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_breakpoint_reset_hit_count (LrgScriptBreakpoint *self);

/* ==========================================================================
 * LrgScriptStackFrame, LrgScriptScope, LrgScriptVariable
 * ========================================================================== */

#define LRG_TYPE_SCRIPT_STACK_FRAME (lrg_script_stack_frame_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptStackFrame, lrg_script_stack_frame, LRG, SCRIPT_STACK_FRAME, GObject)

/**
 * lrg_script_stack_frame_new:
 * @level: the backend's frame level (0 = innermost)
 * @name: (nullable): the function name ("main chunk" / "<module>" when
 *   %NULL)
 * @file: (nullable) (type filename): the source path or chunk name
 * @line: the current 1-based line (0 when unknown)
 *
 * Creates a stack frame.  Backends call this from
 * #LrgScriptDebuggableInterface.debug_get_frames; the debugger assigns the
 * frame id.
 *
 * Returns: (transfer full): a new #LrgScriptStackFrame
 */
LRG_AVAILABLE_IN_ALL
LrgScriptStackFrame * lrg_script_stack_frame_new (gint         level,
                                                  const gchar *name,
                                                  const gchar *file,
                                                  gint         line);

/**
 * lrg_script_stack_frame_get_id:
 * @self: an #LrgScriptStackFrame
 *
 * Returns: the frame id (unique per stop, starts at 1; valid until resume)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_stack_frame_get_id (LrgScriptStackFrame *self);

/**
 * lrg_script_stack_frame_get_level:
 * @self: an #LrgScriptStackFrame
 *
 * Returns: the backend frame level (0 = innermost)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_script_stack_frame_get_level (LrgScriptStackFrame *self);

/**
 * lrg_script_stack_frame_get_name:
 * @self: an #LrgScriptStackFrame
 *
 * Returns: (transfer none): the function name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_stack_frame_get_name (LrgScriptStackFrame *self);

/**
 * lrg_script_stack_frame_get_file:
 * @self: an #LrgScriptStackFrame
 *
 * Returns: (transfer none) (nullable) (type filename): the canonical
 *   source path, or %NULL for frames without a source (C functions)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_stack_frame_get_file (LrgScriptStackFrame *self);

/**
 * lrg_script_stack_frame_get_line:
 * @self: an #LrgScriptStackFrame
 *
 * Returns: the current 1-based line, or 0 when unknown
 */
LRG_AVAILABLE_IN_ALL
gint lrg_script_stack_frame_get_line (LrgScriptStackFrame *self);

#define LRG_TYPE_SCRIPT_SCOPE (lrg_script_scope_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptScope, lrg_script_scope, LRG, SCRIPT_SCOPE, GObject)

/**
 * lrg_script_scope_new:
 * @name: the scope name ("Locals", "Upvalues", "Globals")
 * @variables_reference: a reference from
 *   lrg_script_debugger_alloc_reference()
 * @expensive: whether expanding the scope is costly (globals)
 *
 * Creates a scope.  Backends call this from
 * #LrgScriptDebuggableInterface.debug_get_scopes.
 *
 * Returns: (transfer full): a new #LrgScriptScope
 */
LRG_AVAILABLE_IN_ALL
LrgScriptScope * lrg_script_scope_new (const gchar *name,
                                       guint        variables_reference,
                                       gboolean     expensive);

/**
 * lrg_script_scope_get_name:
 * @self: an #LrgScriptScope
 *
 * Returns: (transfer none): the scope name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_scope_get_name (LrgScriptScope *self);

/**
 * lrg_script_scope_get_variables_reference:
 * @self: an #LrgScriptScope
 *
 * Returns: the reference to pass to lrg_script_debugger_get_variables()
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_scope_get_variables_reference (LrgScriptScope *self);

/**
 * lrg_script_scope_get_expensive:
 * @self: an #LrgScriptScope
 *
 * Returns: whether the scope is costly to expand (clients expand it lazily)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_scope_get_expensive (LrgScriptScope *self);

#define LRG_TYPE_SCRIPT_VARIABLE (lrg_script_variable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptVariable, lrg_script_variable, LRG, SCRIPT_VARIABLE, GObject)

/**
 * lrg_script_variable_new:
 * @name: the variable name (a key or "[index]" for children)
 * @value: the display value (a short repr; long values are elided)
 * @type_name: (nullable): the type name in the script's language
 * @variables_reference: 0 for a leaf, else a reference from
 *   lrg_script_debugger_alloc_reference() that expands the value's
 *   children (tables, dicts, lists, objects, GObjects)
 *
 * Creates a variable.  Backends call this from
 * #LrgScriptDebuggableInterface.debug_get_variables and
 * #LrgScriptDebuggableInterface.debug_evaluate.
 *
 * Returns: (transfer full): a new #LrgScriptVariable
 */
LRG_AVAILABLE_IN_ALL
LrgScriptVariable * lrg_script_variable_new (const gchar *name,
                                             const gchar *value,
                                             const gchar *type_name,
                                             guint        variables_reference);

/**
 * lrg_script_variable_get_name:
 * @self: an #LrgScriptVariable
 *
 * Returns: (transfer none): the name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_variable_get_name (LrgScriptVariable *self);

/**
 * lrg_script_variable_get_value:
 * @self: an #LrgScriptVariable
 *
 * Returns: (transfer none): the display value
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_variable_get_value (LrgScriptVariable *self);

/**
 * lrg_script_variable_get_type_name:
 * @self: an #LrgScriptVariable
 *
 * Returns: (transfer none) (nullable): the type name, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_script_variable_get_type_name (LrgScriptVariable *self);

/**
 * lrg_script_variable_get_variables_reference:
 * @self: an #LrgScriptVariable
 *
 * Returns: 0 for a leaf, else the reference that lists its children
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_variable_get_variables_reference (LrgScriptVariable *self);

/**
 * lrg_script_variable_get_truthy:
 * @self: an #LrgScriptVariable
 *
 * Returns: whether the value is truthy in its language (used for
 *   breakpoint conditions; Lua: not nil/false; Python: bool(value))
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_variable_get_truthy (LrgScriptVariable *self);

/**
 * lrg_script_variable_set_truthy:
 * @self: an #LrgScriptVariable
 * @truthy: the language truthiness of the value
 *
 * Backends set this on evaluate results (it defaults to %TRUE).
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_variable_set_truthy (LrgScriptVariable *self,
                                     gboolean           truthy);

/* ==========================================================================
 * LrgScriptDebugger
 * ========================================================================== */

#define LRG_TYPE_SCRIPT_DEBUGGER (lrg_script_debugger_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptDebugger, lrg_script_debugger, LRG, SCRIPT_DEBUGGER, GObject)

/* ==========================================================================
 * LrgScriptDebuggable: the backend side
 * ========================================================================== */

#define LRG_TYPE_SCRIPT_DEBUGGABLE (lrg_script_debuggable_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgScriptDebuggable, lrg_script_debuggable, LRG, SCRIPT_DEBUGGABLE, GObject)

/**
 * LrgScriptDebuggableInterface:
 * @g_iface: the parent interface
 * @debug_attach: install the backend's hooks (Lua lua_sethook, Python
 *   sys.monitoring) so they report to @debugger through
 *   lrg_script_debugger_report_line() and friends.  Return %FALSE with an
 *   %LRG_SCRIPT_DEBUGGER_ERROR error when the backend cannot be debugged.
 * @debug_detach: remove the hooks
 * @debug_update: the breakpoints, step or pause state changed; re-arm the
 *   hooks (backends keep line hooks off while
 *   lrg_script_debugger_wants_line_events() is %FALSE so a debugger with no
 *   breakpoints costs nothing)
 * @debug_get_frames: called on the stopped thread; return the call stack,
 *   innermost first, as #LrgScriptStackFrame objects
 * @debug_get_scopes: called on the stopped thread; return the scopes of
 *   @frame as #LrgScriptScope objects
 * @debug_get_variables: called on the stopped thread; list the children of
 *   @handle (the pointer given to lrg_script_debugger_alloc_reference())
 * @debug_evaluate: called on the stopped thread; evaluate @expression in
 *   @frame (%NULL means the innermost frame: breakpoint conditions and log
 *   messages are evaluated that way, from inside the line hook) and return the result as a variable (expandable when it has
 *   children; lrg_script_variable_set_truthy() set)
 *
 * The interface a scripting backend implements to be debuggable.  The Lua
 * and Python backends implement it; the visual-script backend can too
 * (reporting nodes through lrg_script_debugger_report_node()).  Every
 * method runs on the thread that executes the backend's scripts, except
 * debug_update, which may be called from any thread and must only flag the
 * change (or use an async-safe call such as lua_sethook()).
 */
struct _LrgScriptDebuggableInterface
{
	GTypeInterface g_iface;

	/*< public >*/
	gboolean            (*debug_attach)        (LrgScriptDebuggable  *self,
	                                            LrgScriptDebugger    *debugger,
	                                            GError              **error);
	void                (*debug_detach)        (LrgScriptDebuggable  *self,
	                                            LrgScriptDebugger    *debugger);
	void                (*debug_update)        (LrgScriptDebuggable  *self);
	GPtrArray *         (*debug_get_frames)    (LrgScriptDebuggable  *self);
	GPtrArray *         (*debug_get_scopes)    (LrgScriptDebuggable  *self,
	                                            LrgScriptStackFrame  *frame);
	GPtrArray *         (*debug_get_variables) (LrgScriptDebuggable  *self,
	                                            gpointer              handle);
	LrgScriptVariable * (*debug_evaluate)      (LrgScriptDebuggable  *self,
	                                            LrgScriptStackFrame  *frame,
	                                            const gchar          *expression,
	                                            GError              **error);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * LrgScriptDebuggerIdleFunc:
 * @debugger: the stopped #LrgScriptDebugger
 * @user_data: user data
 *
 * Called repeatedly on the stopped script thread while it waits for a
 * resume (roughly every 10 ms).  An application whose UI shares the
 * script thread (the editor's play-in-editor) draws a frame here so its
 * debug UI stays live; it may call any debugger function, including
 * lrg_script_debugger_resume().
 */
typedef void (*LrgScriptDebuggerIdleFunc) (LrgScriptDebugger *debugger,
                                           gpointer           user_data);

/* --------------------------------------------------------------------------
 * Construction, attach
 * -------------------------------------------------------------------------- */

/**
 * lrg_script_debugger_new:
 *
 * Creates a debugger with no attached backends.
 *
 * Returns: (transfer full): a new #LrgScriptDebugger
 */
LRG_AVAILABLE_IN_ALL
LrgScriptDebugger * lrg_script_debugger_new (void);

/**
 * lrg_script_debugger_get_default:
 *
 * Returns the process-wide debugger the editor, the DAP server and tool
 * scripts share.  It is created on first use and lives until exit.
 *
 * Returns: (transfer none): the default #LrgScriptDebugger
 */
LRG_AVAILABLE_IN_ALL
LrgScriptDebugger * lrg_script_debugger_get_default (void);

/**
 * lrg_script_debugger_supports:
 * @scripting: an #LrgScripting
 *
 * Returns: %TRUE when @scripting implements #LrgScriptDebuggable (Lua,
 *   Python); %FALSE for crispy, native and Gjs
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_debugger_supports (LrgScripting *scripting);

/**
 * lrg_script_debugger_attach:
 * @self: an #LrgScriptDebugger
 * @scripting: the #LrgScripting to debug
 * @error: (nullable): return location for an error
 *
 * Attaches the debugger to @scripting.  Attaching twice is a no-op.  A
 * backend that cannot be debugged fails with
 * %LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED and a message naming the reason.
 * Must be called on the thread that runs @scripting.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_debugger_attach (LrgScriptDebugger  *self,
                                     LrgScripting       *scripting,
                                     GError            **error);

/**
 * lrg_script_debugger_detach:
 * @self: an #LrgScriptDebugger
 * @scripting: an attached #LrgScripting
 *
 * Detaches from @scripting and removes its hooks.  Detaching an unattached
 * context is a no-op.  A finalized context detaches itself.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_detach (LrgScriptDebugger *self,
                                 LrgScripting      *scripting);

/**
 * lrg_script_debugger_detach_all:
 * @self: an #LrgScriptDebugger
 *
 * Detaches from every context, resumes a stopped script and turns off
 * auto-attach.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_detach_all (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_is_attached:
 * @self: an #LrgScriptDebugger
 * @scripting: an #LrgScripting
 *
 * Returns: whether @scripting is attached
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_debugger_is_attached (LrgScriptDebugger *self,
                                          LrgScripting      *scripting);

/**
 * lrg_script_debugger_get_attached:
 * @self: an #LrgScriptDebugger
 *
 * Returns: (transfer full) (element-type LrgScripting): the attached
 *   contexts, in attach order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_script_debugger_get_attached (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_get_thread_id:
 * @self: an #LrgScriptDebugger
 * @scripting: an attached #LrgScripting
 *
 * Each attached context is one debug "thread" (the DAP `threads` request);
 * its id is stable while it stays attached.
 *
 * Returns: the thread id (>= 1), or 0 when @scripting is not attached
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_debugger_get_thread_id (LrgScriptDebugger *self,
                                         LrgScripting      *scripting);

/**
 * lrg_script_debugger_set_auto_attach:
 * @self: an #LrgScriptDebugger
 * @auto_attach: whether to attach to contexts automatically
 *
 * With auto-attach on, the debugger attaches to every debuggable
 * #LrgScripting that exists now or is created later on any thread (script
 * components, AI scripts, mods).  Contexts that cannot be debugged are
 * skipped silently.  This is how play-in-editor debugging works.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_set_auto_attach (LrgScriptDebugger *self,
                                          gboolean           auto_attach);

/**
 * lrg_script_debugger_get_auto_attach:
 * @self: an #LrgScriptDebugger
 *
 * Returns: whether auto-attach is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_debugger_get_auto_attach (LrgScriptDebugger *self);

/* --------------------------------------------------------------------------
 * Breakpoints
 * -------------------------------------------------------------------------- */

/**
 * lrg_script_debugger_add_breakpoint:
 * @self: an #LrgScriptDebugger
 * @file: (type filename): the script path (made absolute and canonical) or
 *   the chunk name given to lrg_scripting_load_string()
 * @line: the 1-based line
 *
 * Adds a line breakpoint.  Thread-safe.  Adding the same file:line twice
 * returns the existing breakpoint.
 *
 * Returns: (transfer none): the breakpoint (owned by the debugger until
 *   removed)
 */
LRG_AVAILABLE_IN_ALL
LrgScriptBreakpoint * lrg_script_debugger_add_breakpoint (LrgScriptDebugger *self,
                                                          const gchar       *file,
                                                          gint               line);

/**
 * lrg_script_debugger_add_node_breakpoint:
 * @self: an #LrgScriptDebugger
 * @file: (type filename): the visual-script program path
 * @node_id: the node id
 *
 * Adds a node breakpoint for backends that report nodes
 * (lrg_script_debugger_report_node()).  Thread-safe.
 *
 * Returns: (transfer none): the breakpoint
 */
LRG_AVAILABLE_IN_ALL
LrgScriptBreakpoint * lrg_script_debugger_add_node_breakpoint (LrgScriptDebugger *self,
                                                               const gchar       *file,
                                                               const gchar       *node_id);

/**
 * lrg_script_debugger_remove_breakpoint:
 * @self: an #LrgScriptDebugger
 * @breakpoint: a breakpoint of @self
 *
 * Removes @breakpoint.  Thread-safe.
 *
 * Returns: %TRUE when it was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_debugger_remove_breakpoint (LrgScriptDebugger   *self,
                                                LrgScriptBreakpoint *breakpoint);

/**
 * lrg_script_debugger_clear_breakpoints:
 * @self: an #LrgScriptDebugger
 * @file: (nullable) (type filename): only clear breakpoints in this file;
 *   %NULL clears all
 *
 * Removes breakpoints.  The DAP `setBreakpoints` request maps onto
 * clear_breakpoints(file) followed by add_breakpoint() per line.
 * Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_clear_breakpoints (LrgScriptDebugger *self,
                                            const gchar       *file);

/**
 * lrg_script_debugger_get_breakpoints:
 * @self: an #LrgScriptDebugger
 *
 * Returns: (transfer full) (element-type LrgScriptBreakpoint): every
 *   breakpoint, ordered by id
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_script_debugger_get_breakpoints (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_find_breakpoint:
 * @self: an #LrgScriptDebugger
 * @id: a breakpoint id
 *
 * Returns: (transfer none) (nullable): the breakpoint, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgScriptBreakpoint * lrg_script_debugger_find_breakpoint (LrgScriptDebugger *self,
                                                           guint              id);

/**
 * lrg_script_debugger_set_exception_mode:
 * @self: an #LrgScriptDebugger
 * @mode: when script errors stop
 *
 * Sets the exception stop mode (default %LRG_SCRIPT_EXCEPTION_MODE_NEVER).
 * Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_set_exception_mode (LrgScriptDebugger      *self,
                                             LrgScriptExceptionMode  mode);

/**
 * lrg_script_debugger_get_exception_mode:
 * @self: an #LrgScriptDebugger
 *
 * Returns: the exception stop mode
 */
LRG_AVAILABLE_IN_ALL
LrgScriptExceptionMode lrg_script_debugger_get_exception_mode (LrgScriptDebugger *self);

/* --------------------------------------------------------------------------
 * Execution control (thread-safe, any thread)
 * -------------------------------------------------------------------------- */

/**
 * lrg_script_debugger_pause:
 * @self: an #LrgScriptDebugger
 *
 * Requests a stop at the next script line of any attached context (stop
 * on entry: call before running the script).  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_pause (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_resume:
 * @self: an #LrgScriptDebugger
 *
 * Resumes a stopped script (DAP `continue`).  Cancels a pending pause when
 * not stopped.  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_resume (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_step_in:
 * @self: an #LrgScriptDebugger
 *
 * Resumes and stops at the next line executed, entering calls.  No-op when
 * not stopped.  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_step_in (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_step_over:
 * @self: an #LrgScriptDebugger
 *
 * Resumes and stops at the next line in the same or an outer frame.  No-op
 * when not stopped.  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_step_over (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_step_out:
 * @self: an #LrgScriptDebugger
 *
 * Resumes and stops at the next line of an outer frame.  When the
 * function returns to the host, the step completes at the next script line
 * that runs.  No-op when not stopped.  Thread-safe.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_step_out (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_get_state:
 * @self: an #LrgScriptDebugger
 *
 * Returns: the execution state
 */
LRG_AVAILABLE_IN_ALL
LrgScriptDebuggerState lrg_script_debugger_get_state (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_is_paused:
 * @self: an #LrgScriptDebugger
 *
 * Returns: %TRUE while a script thread is stopped
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_debugger_is_paused (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_get_stop_reason:
 * @self: an #LrgScriptDebugger
 *
 * Returns: why the debugger is stopped (%LRG_SCRIPT_STOP_REASON_NONE when
 *   running)
 */
LRG_AVAILABLE_IN_ALL
LrgScriptStopReason lrg_script_debugger_get_stop_reason (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_dup_stop_description:
 * @self: an #LrgScriptDebugger
 *
 * Returns: (transfer full) (nullable): a human-readable stop description
 *   (the exception message for exception stops), or %NULL when running
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_script_debugger_dup_stop_description (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_dup_stopped_scripting:
 * @self: an #LrgScriptDebugger
 *
 * Returns: (transfer full) (nullable): the stopped context, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgScripting * lrg_script_debugger_dup_stopped_scripting (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_get_stopped_thread_id:
 * @self: an #LrgScriptDebugger
 *
 * Returns: the thread id of the stopped context, or 0 when running
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_debugger_get_stopped_thread_id (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_dup_stopped_breakpoint:
 * @self: an #LrgScriptDebugger
 *
 * Returns: (transfer full) (nullable): the breakpoint that caused the stop,
 *   or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgScriptBreakpoint * lrg_script_debugger_dup_stopped_breakpoint (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_set_stopped_idle_func:
 * @self: an #LrgScriptDebugger
 * @func: (nullable) (scope notified) (closure user_data): called while
 *   stopped
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Sets the function the stopped script thread calls while it waits (see
 * #LrgScriptDebuggerIdleFunc).
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_set_stopped_idle_func (LrgScriptDebugger         *self,
                                                LrgScriptDebuggerIdleFunc  func,
                                                gpointer                   user_data,
                                                GDestroyNotify             destroy);

/**
 * lrg_script_debugger_set_iterate_main_context:
 * @self: an #LrgScriptDebugger
 * @iterate: whether the stop loop dispatches the thread-default main
 *   context
 *
 * While stopped, the debugger dispatches pending sources of the stopped
 * thread's default #GMainContext (when it can acquire it) so IPC servers
 * on that context keep answering (the editor's `debug.*` methods).  Script
 * code run from those sources while stopped executes without stopping.
 * Default %TRUE.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_set_iterate_main_context (LrgScriptDebugger *self,
                                                   gboolean           iterate);

/* --------------------------------------------------------------------------
 * Inspection (while paused; any thread)
 * -------------------------------------------------------------------------- */

/**
 * lrg_script_debugger_get_stack_frames:
 * @self: an #LrgScriptDebugger
 * @error: (nullable): return location for an error
 *
 * Returns the call stack of the stopped context, innermost first.  Called
 * from another thread, the request is served by the stopped thread and the
 * caller blocks until it is (%LRG_SCRIPT_DEBUGGER_ERROR_TIMEOUT after 5 s).
 *
 * Returns: (transfer full) (element-type LrgScriptStackFrame) (nullable):
 *   the frames, or %NULL with %LRG_SCRIPT_DEBUGGER_ERROR_NOT_PAUSED
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_script_debugger_get_stack_frames (LrgScriptDebugger  *self,
                                                  GError            **error);

/**
 * lrg_script_debugger_get_scopes:
 * @self: an #LrgScriptDebugger
 * @frame_id: a frame id from lrg_script_debugger_get_stack_frames(), or 0
 *   for the innermost frame
 * @error: (nullable): return location for an error
 *
 * Returns: (transfer full) (element-type LrgScriptScope) (nullable): the
 *   scopes of the frame (Lua: Locals, Upvalues, Globals; Python: Locals,
 *   Globals)
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_script_debugger_get_scopes (LrgScriptDebugger  *self,
                                            guint               frame_id,
                                            GError            **error);

/**
 * lrg_script_debugger_get_variables:
 * @self: an #LrgScriptDebugger
 * @variables_reference: a reference from a scope or an expandable variable
 * @error: (nullable): return location for an error
 *
 * Lists the children of a scope or a structured value (lazy expansion:
 * each child that has children carries its own reference).  References are
 * valid until the script resumes.
 *
 * Returns: (transfer full) (element-type LrgScriptVariable) (nullable): the
 *   children
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_script_debugger_get_variables (LrgScriptDebugger  *self,
                                               guint               variables_reference,
                                               GError            **error);

/**
 * lrg_script_debugger_evaluate:
 * @self: an #LrgScriptDebugger
 * @frame_id: a frame id, or 0 for the innermost frame
 * @expression: an expression in the script's language (Lua: an
 *   expression or a statement; Python: an expression, else a statement)
 * @error: (nullable): return location for an error
 *
 * Evaluates @expression with the frame's locals and globals in scope.  Lua
 * locals are visible read-only (assigning to them does not write back);
 * Python locals write through (3.13+).
 *
 * Returns: (transfer full) (nullable): the result, or %NULL with
 *   %LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE
 */
LRG_AVAILABLE_IN_ALL
LrgScriptVariable * lrg_script_debugger_evaluate (LrgScriptDebugger  *self,
                                                  guint               frame_id,
                                                  const gchar        *expression,
                                                  GError            **error);

/* --------------------------------------------------------------------------
 * Backend side (called by LrgScriptDebuggable implementations)
 * -------------------------------------------------------------------------- */

/**
 * lrg_script_debugger_wants_line_events:
 * @self: an #LrgScriptDebugger
 *
 * Backends arm their per-line hook only while this is %TRUE (breakpoints
 * exist, a step or a pause is pending).
 *
 * Returns: whether line events are needed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_debugger_wants_line_events (LrgScriptDebugger *self);

/**
 * lrg_script_debugger_line_may_break:
 * @self: an #LrgScriptDebugger
 * @line: a 1-based line
 *
 * A cheap pre-filter for line hooks: %FALSE means reporting @line cannot
 * stop (no breakpoint on that line number in any file, no step or pause
 * pending), so the backend can skip computing the file and depth.
 *
 * Returns: whether lrg_script_debugger_report_line() might stop at @line
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_script_debugger_line_may_break (LrgScriptDebugger *self,
                                             gint               line);

/**
 * lrg_script_debugger_report_line:
 * @self: an #LrgScriptDebugger
 * @source: the #LrgScriptDebuggable running the line
 * @file: (type filename): the source path or chunk name
 * @line: the 1-based line about to run
 * @depth: the script call depth (1 = outermost script frame)
 *
 * Reports that a line is about to run.  When a breakpoint, step or pause
 * applies, the call blocks in the stop loop until the debugger resumes.
 * Call on the script thread only.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_report_line (LrgScriptDebugger   *self,
                                      LrgScriptDebuggable *source,
                                      const gchar         *file,
                                      gint                 line,
                                      guint                depth);

/**
 * lrg_script_debugger_report_node:
 * @self: an #LrgScriptDebugger
 * @source: the #LrgScriptDebuggable running the node
 * @file: (type filename): the program path
 * @node_id: the node about to run
 * @depth: the call depth (1 = outermost)
 *
 * Node-level twin of lrg_script_debugger_report_line() for visual-script
 * backends: matches node breakpoints; steps treat each node as a line.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_report_node (LrgScriptDebugger   *self,
                                      LrgScriptDebuggable *source,
                                      const gchar         *file,
                                      const gchar         *node_id,
                                      guint                depth);

/**
 * lrg_script_debugger_report_exception:
 * @self: an #LrgScriptDebugger
 * @source: the #LrgScriptDebuggable raising
 * @file: (nullable) (type filename): the raising source
 * @line: the raising line (0 when unknown)
 * @message: the error message
 * @uncaught: %TRUE when the error is about to reach the host
 *
 * Reports a script error with the raising frame still on the stack.  Stops
 * when the exception mode asks for it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_report_exception (LrgScriptDebugger   *self,
                                           LrgScriptDebuggable *source,
                                           const gchar         *file,
                                           gint                 line,
                                           const gchar         *message,
                                           gboolean             uncaught);

/**
 * lrg_script_debugger_alloc_reference:
 * @self: an #LrgScriptDebugger
 * @handle: (nullable): backend data identifying a scope or value
 * @destroy: (nullable): frees @handle when references are dropped (on
 *   resume, on the stopped thread)
 *
 * Allocates a variables reference for a scope or expandable value.  The
 * debugger hands @handle back to
 * #LrgScriptDebuggableInterface.debug_get_variables.
 *
 * Returns: the reference (>= 1)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_script_debugger_alloc_reference (LrgScriptDebugger *self,
                                           gpointer           handle,
                                           GDestroyNotify     destroy);

/**
 * lrg_script_debugger_emit_output:
 * @self: an #LrgScriptDebugger
 * @category: "console", "stdout", "stderr" or "important"
 * @text: the text (a trailing newline is not added)
 *
 * Emits #LrgScriptDebugger::output.  Backends use it for log points and
 * condition errors; applications may forward script prints.
 */
LRG_AVAILABLE_IN_ALL
void lrg_script_debugger_emit_output (LrgScriptDebugger *self,
                                      const gchar       *category,
                                      const gchar       *text);

/**
 * lrg_script_debugger_normalize_path:
 * @path: (type filename): a path or chunk name
 *
 * Canonicalizes a path the way breakpoints and frames store it: a leading
 * "@" (Lua chunk names) is stripped, relative paths resolve against the
 * current directory, "." and ".." are removed.
 *
 * Returns: (transfer full) (type filename): the canonical path
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_script_debugger_normalize_path (const gchar *path);

#ifdef LRG_SCRIPTING_PRIVATE

/* A step-over/out whose frame returned to C stops at the next script line. */
G_GNUC_INTERNAL void lrg_script_debugger_note_host_return (LrgScriptDebugger *self);

/* The debugger assigns frame ids after debug_get_frames() returns. */
G_GNUC_INTERNAL void lrg_script_stack_frame_set_id (LrgScriptStackFrame *self,
                                                    guint                id);

#endif /* LRG_SCRIPTING_PRIVATE */

G_END_DECLS
