/* lrg-script-debugger.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptDebugger: backend-neutral breakpoints, stepping and inspection.
 * The script thread blocks in a wait loop; other threads post requests
 * that the stopped thread serves.  The debugger mutex is never held across
 * a backend call (those may take the Python GIL).
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SCRIPTING
#define LRG_SCRIPTING_PRIVATE

#include "lrg-script-debugger.h"
#include "../lrg-log.h"

#include <string.h>

#define LRG_SCRIPT_DEBUGGER_TIMEOUT_US ((gint64) 5 * G_TIME_SPAN_SECOND)
#define LRG_SCRIPT_DEBUGGER_IDLE_US    ((gint64) 10 * G_TIME_SPAN_MILLISECOND)

#define LRG_DBG_FLAG_PAUSE      (1)
#define LRG_DBG_FLAG_STEP       (2)
#define LRG_DBG_FLAG_BREAK_NEXT (4)
#define LRG_DBG_FLAG_BPS        (8)

typedef enum
{
	STEP_NONE = 0,
	STEP_IN,
	STEP_OVER,
	STEP_OUT
} StepMode;

typedef enum
{
	REQ_NONE = 0,
	REQ_FRAMES,
	REQ_SCOPES,
	REQ_VARS,
	REQ_EVAL
} ReqKind;

typedef struct
{
	LrgScripting *scripting; /* not ref'd; a weak notify unlinks it */
	guint         thread_id;
} Attached;

typedef struct
{
	gpointer       handle;
	GDestroyNotify destroy;
} VarRef;

typedef struct
{
	guint   id;
	gchar  *condition;
	gchar  *hit_condition;
	gchar  *log_message;
	guint   hit_count;
} BpSnap;

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

G_DEFINE_QUARK (lrg-script-debugger-error-quark, lrg_script_debugger_error)

static GType
register_enum (const gchar *name,
               const GEnumValue *values)
{
	return g_enum_register_static (g_intern_static_string (name), values);
}

GType
lrg_script_debugger_state_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SCRIPT_DEBUGGER_STATE_RUNNING, "LRG_SCRIPT_DEBUGGER_STATE_RUNNING", "running" },
			{ LRG_SCRIPT_DEBUGGER_STATE_PAUSED, "LRG_SCRIPT_DEBUGGER_STATE_PAUSED", "paused" },
			{ 0, NULL, NULL }
		};
		GType id;

		id = register_enum ("LrgScriptDebuggerState", values);
		g_once_init_leave (&type_id, (gsize) id);
	}
	return (GType) type_id;
}

GType
lrg_script_stop_reason_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SCRIPT_STOP_REASON_NONE, "LRG_SCRIPT_STOP_REASON_NONE", "none" },
			{ LRG_SCRIPT_STOP_REASON_BREAKPOINT, "LRG_SCRIPT_STOP_REASON_BREAKPOINT", "breakpoint" },
			{ LRG_SCRIPT_STOP_REASON_STEP, "LRG_SCRIPT_STOP_REASON_STEP", "step" },
			{ LRG_SCRIPT_STOP_REASON_PAUSE, "LRG_SCRIPT_STOP_REASON_PAUSE", "pause" },
			{ LRG_SCRIPT_STOP_REASON_EXCEPTION, "LRG_SCRIPT_STOP_REASON_EXCEPTION", "exception" },
			{ 0, NULL, NULL }
		};
		GType id;

		id = register_enum ("LrgScriptStopReason", values);
		g_once_init_leave (&type_id, (gsize) id);
	}
	return (GType) type_id;
}

const gchar *
lrg_script_stop_reason_to_string (LrgScriptStopReason reason)
{
	switch (reason)
	{
	case LRG_SCRIPT_STOP_REASON_BREAKPOINT:
		return "breakpoint";
	case LRG_SCRIPT_STOP_REASON_STEP:
		return "step";
	case LRG_SCRIPT_STOP_REASON_PAUSE:
		return "pause";
	case LRG_SCRIPT_STOP_REASON_EXCEPTION:
		return "exception";
	case LRG_SCRIPT_STOP_REASON_NONE:
	default:
		return "none";
	}
}

GType
lrg_script_exception_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SCRIPT_EXCEPTION_MODE_NEVER, "LRG_SCRIPT_EXCEPTION_MODE_NEVER", "never" },
			{ LRG_SCRIPT_EXCEPTION_MODE_UNCAUGHT, "LRG_SCRIPT_EXCEPTION_MODE_UNCAUGHT", "uncaught" },
			{ LRG_SCRIPT_EXCEPTION_MODE_ALL, "LRG_SCRIPT_EXCEPTION_MODE_ALL", "all" },
			{ 0, NULL, NULL }
		};
		GType id;

		id = register_enum ("LrgScriptExceptionMode", values);
		g_once_init_leave (&type_id, (gsize) id);
	}
	return (GType) type_id;
}

GType
lrg_script_debugger_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SCRIPT_DEBUGGER_ERROR_FAILED, "LRG_SCRIPT_DEBUGGER_ERROR_FAILED", "failed" },
			{ LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED, "LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED", "unsupported" },
			{ LRG_SCRIPT_DEBUGGER_ERROR_ALREADY_ATTACHED, "LRG_SCRIPT_DEBUGGER_ERROR_ALREADY_ATTACHED", "already-attached" },
			{ LRG_SCRIPT_DEBUGGER_ERROR_NOT_PAUSED, "LRG_SCRIPT_DEBUGGER_ERROR_NOT_PAUSED", "not-paused" },
			{ LRG_SCRIPT_DEBUGGER_ERROR_INVALID_REFERENCE, "LRG_SCRIPT_DEBUGGER_ERROR_INVALID_REFERENCE", "invalid-reference" },
			{ LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE, "LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE", "evaluate" },
			{ LRG_SCRIPT_DEBUGGER_ERROR_TIMEOUT, "LRG_SCRIPT_DEBUGGER_ERROR_TIMEOUT", "timeout" },
			{ 0, NULL, NULL }
		};
		GType id;

		id = register_enum ("LrgScriptDebuggerError", values);
		g_once_init_leave (&type_id, (gsize) id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * LrgScriptBreakpoint
 * ========================================================================== */

struct _LrgScriptBreakpoint
{
	GObject parent_instance;

	GMutex             mutex;
	LrgScriptDebugger *debugger; /* not ref'd; cleared before the debugger drops us */
	guint              id;
	gchar             *file;
	gint               line;
	gchar             *node_id;
	gchar             *condition;
	gchar             *hit_condition;
	gchar             *log_message;
	gboolean           enabled;
	gboolean           verified;
	guint              hit_count;
};

G_DEFINE_FINAL_TYPE (LrgScriptBreakpoint, lrg_script_breakpoint, G_TYPE_OBJECT)

static void lrg_script_debugger_breakpoints_changed (LrgScriptDebugger *self);

static void
breakpoint_notify (LrgScriptBreakpoint *self)
{
	LrgScriptDebugger *debugger;

	g_mutex_lock (&self->mutex);
	debugger = self->debugger;
	g_mutex_unlock (&self->mutex);

	/* The breakpoint mutex is released before the debugger mutex. */
	if (debugger != NULL)
		lrg_script_debugger_breakpoints_changed (debugger);
}

static void
lrg_script_breakpoint_finalize (GObject *object)
{
	LrgScriptBreakpoint *self = LRG_SCRIPT_BREAKPOINT (object);

	g_free (self->file);
	g_free (self->node_id);
	g_free (self->condition);
	g_free (self->hit_condition);
	g_free (self->log_message);
	g_mutex_clear (&self->mutex);

	G_OBJECT_CLASS (lrg_script_breakpoint_parent_class)->finalize (object);
}

static void
lrg_script_breakpoint_class_init (LrgScriptBreakpointClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_script_breakpoint_finalize;
}

static void
lrg_script_breakpoint_init (LrgScriptBreakpoint *self)
{
	g_mutex_init (&self->mutex);
	self->enabled = TRUE;
	self->verified = TRUE;
}

static LrgScriptBreakpoint *
breakpoint_new (LrgScriptDebugger *debugger,
                guint              id,
                const gchar       *file,
                gint               line,
                const gchar       *node_id)
{
	LrgScriptBreakpoint *self = g_object_new (LRG_TYPE_SCRIPT_BREAKPOINT, NULL);

	self->debugger = debugger;
	self->id = id;
	self->file = g_strdup (file);
	self->line = line;
	self->node_id = g_strdup (node_id);
	return self;
}

guint
lrg_script_breakpoint_get_id (LrgScriptBreakpoint *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), 0);
	return self->id;
}

const gchar *
lrg_script_breakpoint_get_file (LrgScriptBreakpoint *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), NULL);
	return self->file;
}

gint
lrg_script_breakpoint_get_line (LrgScriptBreakpoint *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), 0);
	return self->line;
}

const gchar *
lrg_script_breakpoint_get_node_id (LrgScriptBreakpoint *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), NULL);
	return self->node_id;
}

const gchar *
lrg_script_breakpoint_get_condition (LrgScriptBreakpoint *self)
{
	const gchar *value;

	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), NULL);
	g_mutex_lock (&self->mutex);
	value = self->condition;
	g_mutex_unlock (&self->mutex);
	return value;
}

void
lrg_script_breakpoint_set_condition (LrgScriptBreakpoint *self,
                                     const gchar         *condition)
{
	g_return_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self));
	g_mutex_lock (&self->mutex);
	g_free (self->condition);
	self->condition = g_strdup (condition);
	g_mutex_unlock (&self->mutex);
	breakpoint_notify (self);
}

const gchar *
lrg_script_breakpoint_get_hit_condition (LrgScriptBreakpoint *self)
{
	const gchar *value;

	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), NULL);
	g_mutex_lock (&self->mutex);
	value = self->hit_condition;
	g_mutex_unlock (&self->mutex);
	return value;
}

void
lrg_script_breakpoint_set_hit_condition (LrgScriptBreakpoint *self,
                                         const gchar         *hit_condition)
{
	g_return_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self));
	g_mutex_lock (&self->mutex);
	g_free (self->hit_condition);
	self->hit_condition = g_strdup (hit_condition);
	g_mutex_unlock (&self->mutex);
	breakpoint_notify (self);
}

const gchar *
lrg_script_breakpoint_get_log_message (LrgScriptBreakpoint *self)
{
	const gchar *value;

	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), NULL);
	g_mutex_lock (&self->mutex);
	value = self->log_message;
	g_mutex_unlock (&self->mutex);
	return value;
}

void
lrg_script_breakpoint_set_log_message (LrgScriptBreakpoint *self,
                                       const gchar         *log_message)
{
	g_return_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self));
	g_mutex_lock (&self->mutex);
	g_free (self->log_message);
	self->log_message = g_strdup (log_message);
	g_mutex_unlock (&self->mutex);
	breakpoint_notify (self);
}

gboolean
lrg_script_breakpoint_get_enabled (LrgScriptBreakpoint *self)
{
	gboolean value;

	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), FALSE);
	g_mutex_lock (&self->mutex);
	value = self->enabled;
	g_mutex_unlock (&self->mutex);
	return value;
}

void
lrg_script_breakpoint_set_enabled (LrgScriptBreakpoint *self,
                                   gboolean             enabled)
{
	g_return_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self));
	g_mutex_lock (&self->mutex);
	self->enabled = !!enabled;
	g_mutex_unlock (&self->mutex);
	breakpoint_notify (self);
}

gboolean
lrg_script_breakpoint_get_verified (LrgScriptBreakpoint *self)
{
	gboolean value;

	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), FALSE);
	g_mutex_lock (&self->mutex);
	value = self->verified;
	g_mutex_unlock (&self->mutex);
	return value;
}

guint
lrg_script_breakpoint_get_hit_count (LrgScriptBreakpoint *self)
{
	guint value;

	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self), 0);
	g_mutex_lock (&self->mutex);
	value = self->hit_count;
	g_mutex_unlock (&self->mutex);
	return value;
}

void
lrg_script_breakpoint_reset_hit_count (LrgScriptBreakpoint *self)
{
	g_return_if_fail (LRG_IS_SCRIPT_BREAKPOINT (self));
	g_mutex_lock (&self->mutex);
	self->hit_count = 0;
	g_mutex_unlock (&self->mutex);
}

/* ==========================================================================
 * Stack frames, scopes, variables
 * ========================================================================== */

struct _LrgScriptStackFrame
{
	GObject parent_instance;
	guint   id;
	gint    level;
	gchar  *name;
	gchar  *file;
	gint    line;
};

G_DEFINE_FINAL_TYPE (LrgScriptStackFrame, lrg_script_stack_frame, G_TYPE_OBJECT)

static void
lrg_script_stack_frame_finalize (GObject *object)
{
	LrgScriptStackFrame *self = LRG_SCRIPT_STACK_FRAME (object);

	g_free (self->name);
	g_free (self->file);
	G_OBJECT_CLASS (lrg_script_stack_frame_parent_class)->finalize (object);
}

static void
lrg_script_stack_frame_class_init (LrgScriptStackFrameClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_script_stack_frame_finalize;
}

static void
lrg_script_stack_frame_init (LrgScriptStackFrame *self)
{
}

void
lrg_script_stack_frame_set_id (LrgScriptStackFrame *self,
                               guint                id)
{
	g_return_if_fail (LRG_IS_SCRIPT_STACK_FRAME (self));
	self->id = id;
}

LrgScriptStackFrame *
lrg_script_stack_frame_new (gint         level,
                            const gchar *name,
                            const gchar *file,
                            gint         line)
{
	LrgScriptStackFrame *self = g_object_new (LRG_TYPE_SCRIPT_STACK_FRAME, NULL);

	self->level = level;
	self->name = g_strdup (name != NULL ? name : "main chunk");
	self->file = file != NULL ? lrg_script_debugger_normalize_path (file) : NULL;
	self->line = line;
	return self;
}

guint
lrg_script_stack_frame_get_id (LrgScriptStackFrame *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_STACK_FRAME (self), 0);
	return self->id;
}

gint
lrg_script_stack_frame_get_level (LrgScriptStackFrame *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_STACK_FRAME (self), 0);
	return self->level;
}

const gchar *
lrg_script_stack_frame_get_name (LrgScriptStackFrame *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_STACK_FRAME (self), NULL);
	return self->name;
}

const gchar *
lrg_script_stack_frame_get_file (LrgScriptStackFrame *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_STACK_FRAME (self), NULL);
	return self->file;
}

gint
lrg_script_stack_frame_get_line (LrgScriptStackFrame *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_STACK_FRAME (self), 0);
	return self->line;
}

struct _LrgScriptScope
{
	GObject  parent_instance;
	gchar   *name;
	guint    variables_reference;
	gboolean expensive;
};

G_DEFINE_FINAL_TYPE (LrgScriptScope, lrg_script_scope, G_TYPE_OBJECT)

static void
lrg_script_scope_finalize (GObject *object)
{
	LrgScriptScope *self = LRG_SCRIPT_SCOPE (object);

	g_free (self->name);
	G_OBJECT_CLASS (lrg_script_scope_parent_class)->finalize (object);
}

static void
lrg_script_scope_class_init (LrgScriptScopeClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_script_scope_finalize;
}

static void
lrg_script_scope_init (LrgScriptScope *self)
{
}

LrgScriptScope *
lrg_script_scope_new (const gchar *name,
                      guint        variables_reference,
                      gboolean     expensive)
{
	LrgScriptScope *self = g_object_new (LRG_TYPE_SCRIPT_SCOPE, NULL);

	self->name = g_strdup (name != NULL ? name : "");
	self->variables_reference = variables_reference;
	self->expensive = !!expensive;
	return self;
}

const gchar *
lrg_script_scope_get_name (LrgScriptScope *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_SCOPE (self), NULL);
	return self->name;
}

guint
lrg_script_scope_get_variables_reference (LrgScriptScope *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_SCOPE (self), 0);
	return self->variables_reference;
}

gboolean
lrg_script_scope_get_expensive (LrgScriptScope *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_SCOPE (self), FALSE);
	return self->expensive;
}

struct _LrgScriptVariable
{
	GObject  parent_instance;
	gchar   *name;
	gchar   *value;
	gchar   *type_name;
	guint    variables_reference;
	gboolean truthy;
};

G_DEFINE_FINAL_TYPE (LrgScriptVariable, lrg_script_variable, G_TYPE_OBJECT)

static void
lrg_script_variable_finalize (GObject *object)
{
	LrgScriptVariable *self = LRG_SCRIPT_VARIABLE (object);

	g_free (self->name);
	g_free (self->value);
	g_free (self->type_name);
	G_OBJECT_CLASS (lrg_script_variable_parent_class)->finalize (object);
}

static void
lrg_script_variable_class_init (LrgScriptVariableClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_script_variable_finalize;
}

static void
lrg_script_variable_init (LrgScriptVariable *self)
{
	self->truthy = TRUE;
}

LrgScriptVariable *
lrg_script_variable_new (const gchar *name,
                         const gchar *value,
                         const gchar *type_name,
                         guint        variables_reference)
{
	LrgScriptVariable *self = g_object_new (LRG_TYPE_SCRIPT_VARIABLE, NULL);

	self->name = g_strdup (name != NULL ? name : "");
	self->value = g_strdup (value != NULL ? value : "");
	self->type_name = g_strdup (type_name != NULL ? type_name : "");
	self->variables_reference = variables_reference;
	self->truthy = TRUE;
	return self;
}

const gchar *
lrg_script_variable_get_name (LrgScriptVariable *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_VARIABLE (self), NULL);
	return self->name;
}

const gchar *
lrg_script_variable_get_value (LrgScriptVariable *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_VARIABLE (self), NULL);
	return self->value;
}

const gchar *
lrg_script_variable_get_type_name (LrgScriptVariable *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_VARIABLE (self), NULL);
	return self->type_name;
}

guint
lrg_script_variable_get_variables_reference (LrgScriptVariable *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_VARIABLE (self), 0);
	return self->variables_reference;
}

gboolean
lrg_script_variable_get_truthy (LrgScriptVariable *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_VARIABLE (self), FALSE);
	return self->truthy;
}

void
lrg_script_variable_set_truthy (LrgScriptVariable *self,
                                gboolean           truthy)
{
	g_return_if_fail (LRG_IS_SCRIPT_VARIABLE (self));
	self->truthy = !!truthy;
}

/* ==========================================================================
 * LrgScriptDebuggable
 * ========================================================================== */

G_DEFINE_INTERFACE (LrgScriptDebuggable, lrg_script_debuggable, G_TYPE_OBJECT)

static void
lrg_script_debuggable_default_init (LrgScriptDebuggableInterface *iface)
{
	(void) iface;
}

/* ==========================================================================
 * LrgScriptDebugger
 * ========================================================================== */

struct _LrgScriptDebugger
{
	GObject parent_instance;

	GMutex mutex;
	GCond  cond;

	GPtrArray *attached;     /* Attached *, not ref'd */
	gboolean   auto_attach;
	gboolean   instance_hook;
	guint      next_thread_id;

	GPtrArray  *breakpoints; /* LrgScriptBreakpoint *, ref'd */
	guint       next_bp_id;
	GHashTable *break_lines; /* gint line -> dummy */
	gint        event_flags; /* atomic; lock-free wants_line_events */

	LrgScriptExceptionMode exception_mode;

	LrgScriptDebuggerState state;
	LrgScriptStopReason    reason;
	gchar                 *stop_description;
	LrgScripting          *stopped;       /* not ref'd; the paused context */
	guint                  stopped_thread_id;
	guint                  stopped_depth;
	LrgScriptBreakpoint   *stopped_bp;    /* ref'd while paused */
	GThread               *owner;
	gboolean               in_stop;
	gboolean               resume_requested;

	StepMode step;
	guint    step_depth;
	gboolean break_on_next;
	gboolean pause_requested;

	LrgScriptDebuggerIdleFunc idle_func;
	gpointer                  idle_data;
	GDestroyNotify            idle_destroy;
	gboolean                  iterate_main;

	GArray    *refs;         /* VarRef, index 0 unused */
	GPtrArray *frames;       /* LrgScriptStackFrame *, valid while paused */
	gint       suppress;

	gboolean  request_pending;
	gboolean  req_done;
	ReqKind   req_kind;
	guint     req_frame_id;
	guint     req_var_ref;
	gchar    *req_expr;
	GPtrArray *req_array;
	LrgScriptVariable *req_var;
	GError   *req_error;
};

G_DEFINE_FINAL_TYPE (LrgScriptDebugger, lrg_script_debugger, G_TYPE_OBJECT)

enum
{
	SIGNAL_OUTPUT,
	N_SIGNALS
};

static guint dbg_signals[N_SIGNALS] = { 0 };

static void debug_update_all (LrgScriptDebugger *self);
static void auto_attach_hook (LrgScripting *scripting, gpointer user_data);

gchar *
lrg_script_debugger_normalize_path (const gchar *path)
{
	const gchar *start;

	if (path == NULL)
		return NULL;
	start = path;
	if (start[0] == '@')
		start++;
	/* Chunk names ("[string ...]") are not filesystem paths. */
	if (start[0] == '[')
		return g_strdup (start);
	return g_canonicalize_filename (start, NULL);
}

static void
refresh_flags_locked (LrgScriptDebugger *self)
{
	gint flags = 0;

	if (self->pause_requested)
		flags |= LRG_DBG_FLAG_PAUSE;
	if (self->step != STEP_NONE)
		flags |= LRG_DBG_FLAG_STEP;
	if (self->break_on_next)
		flags |= LRG_DBG_FLAG_BREAK_NEXT;
	if (self->breakpoints->len > 0)
	{
		guint i;

		for (i = 0; i < self->breakpoints->len; i++)
		{
			LrgScriptBreakpoint *bp = g_ptr_array_index (self->breakpoints, i);

			if (lrg_script_breakpoint_get_enabled (bp))
			{
				flags |= LRG_DBG_FLAG_BPS;
				break;
			}
		}
	}
	g_atomic_int_set (&self->event_flags, flags);
}

static void
rebuild_lines_locked (LrgScriptDebugger *self)
{
	guint i;

	g_hash_table_remove_all (self->break_lines);
	for (i = 0; i < self->breakpoints->len; i++)
	{
		LrgScriptBreakpoint *bp = g_ptr_array_index (self->breakpoints, i);
		gboolean enabled;
		gint line;

		g_mutex_lock (&bp->mutex);
		enabled = bp->enabled;
		line = bp->line;
		g_mutex_unlock (&bp->mutex);
		if (enabled && line > 0)
			g_hash_table_add (self->break_lines, GINT_TO_POINTER (line));
	}
	refresh_flags_locked (self);
}

static void
lrg_script_debugger_breakpoints_changed (LrgScriptDebugger *self)
{
	g_mutex_lock (&self->mutex);
	rebuild_lines_locked (self);
	g_mutex_unlock (&self->mutex);
	debug_update_all (self);
}

static Attached *
find_attached_locked (LrgScriptDebugger *self,
                      LrgScripting      *scripting)
{
	guint i;

	for (i = 0; i < self->attached->len; i++)
	{
		Attached *entry = g_ptr_array_index (self->attached, i);

		if (entry->scripting == scripting)
			return entry;
	}
	return NULL;
}

static void
on_scripting_weak (gpointer data,
                   GObject *where_the_object_was)
{
	LrgScriptDebugger *self = (LrgScriptDebugger *) data;
	guint i;

	/* The object is already being disposed: unlink only.  Subclass
	 * finalize detaches hooks while the interpreter is still alive. */
	g_mutex_lock (&self->mutex);
	for (i = 0; i < self->attached->len; i++)
	{
		Attached *entry = g_ptr_array_index (self->attached, i);

		if ((GObject *) entry->scripting == where_the_object_was)
		{
			g_ptr_array_remove_index (self->attached, i);
			break;
		}
	}
	if ((GObject *) self->stopped == where_the_object_was)
		self->stopped = NULL;
	g_mutex_unlock (&self->mutex);
}

static const gchar *
unsupported_reason (LrgScripting *scripting)
{
	const gchar *name = g_type_name (G_OBJECT_TYPE (scripting));

	if (name != NULL && strstr (name, "Gjs") != NULL)
		return "Gjs does not expose SpiderMonkey's Debugger object to C; breakpoints in JavaScript are unsupported";
	if (name != NULL && strstr (name, "Crispy") != NULL)
		return "Crispy compiles C ahead of time and has no line-hook API";
	if (name != NULL && strstr (name, "Native") != NULL)
		return "native modules have no script-level line events";
	return "this backend does not implement LrgScriptDebuggable";
}

static void
debug_update_all (LrgScriptDebugger *self)
{
	GPtrArray *list;
	guint i;

	list = g_ptr_array_new ();
	g_mutex_lock (&self->mutex);
	for (i = 0; i < self->attached->len; i++)
	{
		Attached *entry = g_ptr_array_index (self->attached, i);

		g_ptr_array_add (list, entry->scripting);
	}
	g_mutex_unlock (&self->mutex);

	for (i = 0; i < list->len; i++)
	{
		LrgScripting *scripting = g_ptr_array_index (list, i);
		LrgScriptDebuggableInterface *iface;

		if (!LRG_IS_SCRIPT_DEBUGGABLE (scripting))
			continue;
		iface = LRG_SCRIPT_DEBUGGABLE_GET_IFACE (scripting);
		if (iface->debug_update != NULL)
			iface->debug_update (LRG_SCRIPT_DEBUGGABLE (scripting));
	}
	g_ptr_array_unref (list);
}

static void
drop_inspection (LrgScriptDebugger *self)
{
	GArray *refs;
	GPtrArray *frames;
	guint i;

	g_mutex_lock (&self->mutex);
	refs = self->refs;
	self->refs = NULL;
	frames = self->frames;
	self->frames = NULL;
	g_mutex_unlock (&self->mutex);

	if (refs != NULL)
	{
		for (i = 0; i < refs->len; i++)
		{
			VarRef *ref = &g_array_index (refs, VarRef, i);

			if (ref->destroy != NULL && ref->handle != NULL)
				ref->destroy (ref->handle);
		}
		g_array_free (refs, TRUE);
	}
	if (frames != NULL)
		g_ptr_array_unref (frames);
}

static LrgScriptDebuggableInterface *
debuggable_iface (LrgScripting *scripting)
{
	if (scripting == NULL || !LRG_IS_SCRIPT_DEBUGGABLE (scripting))
		return NULL;
	return LRG_SCRIPT_DEBUGGABLE_GET_IFACE (scripting);
}

static void
assign_frame_ids (GPtrArray *frames)
{
	guint i;

	if (frames == NULL)
		return;
	for (i = 0; i < frames->len; i++)
		lrg_script_stack_frame_set_id (g_ptr_array_index (frames, i), i + 1);
}

static GPtrArray *
fetch_frames (LrgScriptDebugger *self,
              LrgScripting      *scripting)
{
	LrgScriptDebuggableInterface *iface = debuggable_iface (scripting);
	GPtrArray *frames = NULL;

	if (iface != NULL && iface->debug_get_frames != NULL)
		frames = iface->debug_get_frames (LRG_SCRIPT_DEBUGGABLE (scripting));
	if (frames == NULL)
		frames = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	assign_frame_ids (frames);
	(void) self;
	return frames;
}

static GPtrArray *
cached_frames (LrgScriptDebugger *self)
{
	if (self->frames == NULL && self->stopped != NULL)
		self->frames = fetch_frames (self, self->stopped);
	return self->frames;
}

static LrgScriptStackFrame *
frame_by_id (GPtrArray *frames,
             guint      frame_id)
{
	guint i;

	if (frames == NULL || frames->len == 0)
		return NULL;
	if (frame_id == 0)
		return g_ptr_array_index (frames, 0);
	for (i = 0; i < frames->len; i++)
	{
		LrgScriptStackFrame *frame = g_ptr_array_index (frames, i);

		if (frame->id == frame_id)
			return frame;
	}
	return NULL;
}

static GPtrArray *
fetch_scopes (LrgScripting        *scripting,
              LrgScriptStackFrame *frame)
{
	LrgScriptDebuggableInterface *iface = debuggable_iface (scripting);
	GPtrArray *scopes = NULL;

	if (iface != NULL && iface->debug_get_scopes != NULL)
		scopes = iface->debug_get_scopes (LRG_SCRIPT_DEBUGGABLE (scripting), frame);
	if (scopes == NULL)
		scopes = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	return scopes;
}

static GPtrArray *
fetch_variables (LrgScriptDebugger *self,
                 LrgScripting      *scripting,
                 guint              variables_reference,
                 GError           **error)
{
	LrgScriptDebuggableInterface *iface = debuggable_iface (scripting);
	VarRef *ref;
	GPtrArray *vars = NULL;

	if (self->refs == NULL ||
	    variables_reference == 0 ||
	    variables_reference >= self->refs->len)
	{
		g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
		             LRG_SCRIPT_DEBUGGER_ERROR_INVALID_REFERENCE,
		             "Variables reference %u is not valid", variables_reference);
		return NULL;
	}
	ref = &g_array_index (self->refs, VarRef, variables_reference);
	if (iface != NULL && iface->debug_get_variables != NULL)
		vars = iface->debug_get_variables (LRG_SCRIPT_DEBUGGABLE (scripting), ref->handle);
	if (vars == NULL)
		vars = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	return vars;
}

static LrgScriptVariable *
fetch_eval (LrgScriptDebugger    *self,
            LrgScripting         *scripting,
            LrgScriptStackFrame  *frame,
            const gchar          *expression,
            GError              **error)
{
	LrgScriptDebuggableInterface *iface = debuggable_iface (scripting);
	LrgScriptVariable *var = NULL;
	GError *local = NULL;

	g_mutex_lock (&self->mutex);
	self->suppress++;
	g_mutex_unlock (&self->mutex);
	if (iface != NULL && iface->debug_evaluate != NULL)
		var = iface->debug_evaluate (LRG_SCRIPT_DEBUGGABLE (scripting),
		                             frame, expression, &local);
	g_mutex_lock (&self->mutex);
	self->suppress--;
	g_mutex_unlock (&self->mutex);
	if (var == NULL)
	{
		g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
		             LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE,
		             "%s", local != NULL ? local->message : "evaluation failed");
		g_clear_error (&local);
	}
	return var;
}

/* Runs on the stopped thread.  The debugger mutex is NOT held. */
static void
perform_request (LrgScriptDebugger *self,
                 ReqKind            kind,
                 guint              frame_id,
                 guint              var_ref,
                 const gchar       *expr,
                 GPtrArray        **out_array,
                 LrgScriptVariable **out_var,
                 GError           **error)
{
	LrgScripting *scripting = self->stopped;

	if (scripting == NULL || !self->in_stop)
	{
		g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
		             LRG_SCRIPT_DEBUGGER_ERROR_NOT_PAUSED,
		             "No script is stopped");
		return;
	}

	if (kind == REQ_FRAMES)
	{
		*out_array = cached_frames (self);
		if (*out_array != NULL)
			g_ptr_array_ref (*out_array);
		return;
	}

	if (kind == REQ_SCOPES || kind == REQ_EVAL)
	{
		GPtrArray *frames = cached_frames (self);
		LrgScriptStackFrame *frame = frame_by_id (frames, frame_id);

		if (frame == NULL && !(kind == REQ_EVAL && frame_id == 0))
		{
			g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
			             LRG_SCRIPT_DEBUGGER_ERROR_INVALID_REFERENCE,
			             "Frame %u is not valid", frame_id);
			return;
		}
		if (kind == REQ_SCOPES)
			*out_array = fetch_scopes (scripting, frame);
		else
			*out_var = fetch_eval (self, scripting, frame, expr, error);
		return;
	}

	*out_array = fetch_variables (self, scripting, var_ref, error);
}

static void
dispatch_request_locked (LrgScriptDebugger *self)
{
	ReqKind kind;
	guint frame_id;
	guint var_ref;
	gchar *expr;
	GPtrArray *array = NULL;
	LrgScriptVariable *var = NULL;
	GError *error = NULL;

	kind = self->req_kind;
	frame_id = self->req_frame_id;
	var_ref = self->req_var_ref;
	expr = g_steal_pointer (&self->req_expr);
	self->request_pending = FALSE;

	g_mutex_unlock (&self->mutex);
	perform_request (self, kind, frame_id, var_ref, expr, &array, &var, &error);
	g_free (expr);
	g_mutex_lock (&self->mutex);

	self->req_array = array;
	self->req_var = var;
	self->req_error = error;
	self->req_done = TRUE;
	g_cond_broadcast (&self->cond);
}

static void
iterate_stopped_context (void)
{
	GMainContext *context = g_main_context_get_thread_default ();

	if (context == NULL)
		return;
	if (!g_main_context_acquire (context))
		return;
	while (g_main_context_pending (context))
		g_main_context_iteration (context, FALSE);
	g_main_context_release (context);
}

/* Caller holds the mutex.  Returns with the mutex held and the state
 * running again.  Inspection refs are dropped by the caller afterwards. */
static void
stop_loop (LrgScriptDebugger *self)
{
	self->in_stop = TRUE;
	self->state = LRG_SCRIPT_DEBUGGER_STATE_PAUSED;
	self->owner = g_thread_self ();
	self->resume_requested = FALSE;

	/* A resume that races a pending inspection still serves that request. */
	while (!self->resume_requested || self->request_pending)
	{
		LrgScriptDebuggerIdleFunc idle;
		gpointer idle_data;
		gboolean iterate;
		gint64 deadline;

		if (self->request_pending)
			dispatch_request_locked (self);

		idle = self->idle_func;
		idle_data = self->idle_data;
		iterate = self->iterate_main;
		deadline = g_get_monotonic_time () + LRG_SCRIPT_DEBUGGER_IDLE_US;

		g_mutex_unlock (&self->mutex);
		if (idle != NULL)
			idle (self, idle_data);
		if (iterate)
			iterate_stopped_context ();
		g_mutex_lock (&self->mutex);

		if (self->resume_requested)
			break;
		if (!self->request_pending)
			g_cond_wait_until (&self->cond, &self->mutex, deadline);
	}

	self->state = LRG_SCRIPT_DEBUGGER_STATE_RUNNING;
	self->reason = LRG_SCRIPT_STOP_REASON_NONE;
	self->owner = NULL;
	self->stopped = NULL;
	self->stopped_thread_id = 0;
	self->stopped_depth = 0;
	g_clear_pointer (&self->stop_description, g_free);
	g_clear_object (&self->stopped_bp);
	self->in_stop = FALSE;
	g_cond_broadcast (&self->cond);
}

static gboolean
post_request (LrgScriptDebugger  *self,
              ReqKind             kind,
              guint               frame_id,
              guint               var_ref,
              const gchar        *expr,
              GPtrArray         **out_array,
              LrgScriptVariable **out_var,
              GError            **error)
{
	gint64 deadline;
	gboolean ok = TRUE;

	g_mutex_lock (&self->mutex);
	if (!self->in_stop)
	{
		g_mutex_unlock (&self->mutex);
		g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
		             LRG_SCRIPT_DEBUGGER_ERROR_NOT_PAUSED,
		             "No script is stopped");
		return FALSE;
	}

	if (self->owner == g_thread_self ())
	{
		g_mutex_unlock (&self->mutex);
		perform_request (self, kind, frame_id, var_ref, expr, out_array, out_var, error);
		return error == NULL || *error == NULL;
	}

	while (self->request_pending)
		g_cond_wait (&self->cond, &self->mutex);

	self->req_kind = kind;
	self->req_frame_id = frame_id;
	self->req_var_ref = var_ref;
	self->req_expr = g_strdup (expr);
	self->req_done = FALSE;
	self->request_pending = TRUE;
	g_clear_pointer (&self->req_array, g_ptr_array_unref);
	g_clear_object (&self->req_var);
	g_clear_error (&self->req_error);
	g_cond_broadcast (&self->cond);

	deadline = g_get_monotonic_time () + LRG_SCRIPT_DEBUGGER_TIMEOUT_US;
	while (!self->req_done)
	{
		if (!g_cond_wait_until (&self->cond, &self->mutex, deadline))
		{
			self->request_pending = FALSE;
			g_mutex_unlock (&self->mutex);
			g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
			             LRG_SCRIPT_DEBUGGER_ERROR_TIMEOUT,
			             "Stopped thread did not answer within 5 seconds");
			return FALSE;
		}
	}

	if (self->req_error != NULL)
	{
		g_propagate_error (error, g_steal_pointer (&self->req_error));
		ok = FALSE;
	}
	else if (out_array != NULL)
		*out_array = g_steal_pointer (&self->req_array);
	else if (out_var != NULL)
		*out_var = g_steal_pointer (&self->req_var);

	g_clear_pointer (&self->req_array, g_ptr_array_unref);
	g_clear_object (&self->req_var);
	g_mutex_unlock (&self->mutex);
	return ok;
}

static guint
parse_uint (const gchar *text,
            gboolean    *ok)
{
	guint n = 0;
	const gchar *p = text;

	*ok = FALSE;
	if (p == NULL)
		return 0;
	while (*p == ' ')
		p++;
	if (!g_ascii_isdigit (*p))
		return 0;
	while (g_ascii_isdigit (*p))
	{
		n = n * 10u + (guint) (*p - '0');
		p++;
	}
	while (*p == ' ')
		p++;
	*ok = *p == '\0';
	return n;
}

static gboolean
hit_condition_matches (const gchar *cond,
                       guint        hits)
{
	const gchar *p;
	gboolean ok = FALSE;
	guint n;

	if (cond == NULL || cond[0] == '\0')
		return TRUE;
	p = cond;
	while (*p == ' ')
		p++;
	if (p[0] == '>' && p[1] == '=')
	{
		n = parse_uint (p + 2, &ok);
		return !ok || hits >= n;
	}
	if (p[0] == '<' && p[1] == '=')
	{
		n = parse_uint (p + 2, &ok);
		return !ok || hits <= n;
	}
	if (p[0] == '=' && p[1] == '=')
	{
		n = parse_uint (p + 2, &ok);
		return !ok || hits == n;
	}
	if (p[0] == '>')
	{
		n = parse_uint (p + 1, &ok);
		return !ok || hits > n;
	}
	if (p[0] == '<')
	{
		n = parse_uint (p + 1, &ok);
		return !ok || hits < n;
	}
	if (p[0] == '%')
	{
		n = parse_uint (p + 1, &ok);
		return !ok || (n > 0 && (hits % n) == 0);
	}
	n = parse_uint (p, &ok);
	return !ok || hits == n;
}

static void
snap_free (gpointer data)
{
	BpSnap *snap = (BpSnap *) data;

	g_free (snap->condition);
	g_free (snap->hit_condition);
	g_free (snap->log_message);
	g_free (snap);
}

static gboolean
same_path (const gchar *a,
           const gchar *b)
{
	if (a == NULL || b == NULL)
		return a == b;
	return strcmp (a, b) == 0;
}

static GPtrArray *
matching_breakpoints (LrgScriptDebugger *self,
                      const gchar       *file,
                      gint               line,
                      const gchar       *node_id)
{
	GPtrArray *snaps = g_ptr_array_new_with_free_func (snap_free);
	guint i;

	g_mutex_lock (&self->mutex);
	for (i = 0; i < self->breakpoints->len; i++)
	{
		LrgScriptBreakpoint *bp = g_ptr_array_index (self->breakpoints, i);
		BpSnap *snap;

		g_mutex_lock (&bp->mutex);
		if (!bp->enabled)
		{
			g_mutex_unlock (&bp->mutex);
			continue;
		}
		if (node_id != NULL)
		{
			if (g_strcmp0 (bp->node_id, node_id) != 0 ||
			    !same_path (bp->file, file))
			{
				g_mutex_unlock (&bp->mutex);
				continue;
			}
		}
		else if (bp->node_id != NULL || bp->line != line || !same_path (bp->file, file))
		{
			g_mutex_unlock (&bp->mutex);
			continue;
		}
		snap = g_new0 (BpSnap, 1);
		snap->id = bp->id;
		snap->condition = g_strdup (bp->condition);
		snap->hit_condition = g_strdup (bp->hit_condition);
		snap->log_message = g_strdup (bp->log_message);
		snap->hit_count = bp->hit_count;
		g_mutex_unlock (&bp->mutex);
		g_ptr_array_add (snaps, snap);
	}
	g_mutex_unlock (&self->mutex);
	return snaps;
}

static LrgScriptBreakpoint *
breakpoint_by_id_locked (LrgScriptDebugger *self,
                         guint              id)
{
	guint i;

	for (i = 0; i < self->breakpoints->len; i++)
	{
		LrgScriptBreakpoint *bp = g_ptr_array_index (self->breakpoints, i);

		if (bp->id == id)
			return bp;
	}
	return NULL;
}

/* Mutex is NOT held.  Evaluates in the innermost frame. */
static LrgScriptVariable *
eval_innermost (LrgScriptDebugger *self,
                LrgScripting      *scripting,
                const gchar       *expression,
                GError           **error)
{
	LrgScriptVariable *var;

	g_mutex_lock (&self->mutex);
	self->suppress++;
	g_mutex_unlock (&self->mutex);

	var = NULL;
	{
		LrgScriptDebuggableInterface *iface = debuggable_iface (scripting);

		if (iface != NULL && iface->debug_evaluate != NULL)
			var = iface->debug_evaluate (LRG_SCRIPT_DEBUGGABLE (scripting),
			                             NULL, expression, error);
		else
			g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
			             LRG_SCRIPT_DEBUGGER_ERROR_EVALUATE,
			             "Backend cannot evaluate");
	}

	g_mutex_lock (&self->mutex);
	self->suppress--;
	g_mutex_unlock (&self->mutex);
	return var;
}

static gchar *
expand_log_message (LrgScriptDebugger *self,
                    LrgScripting      *scripting,
                    const gchar       *message)
{
	GString *out;
	const gchar *cursor;

	if (message == NULL)
		return g_strdup ("");
	out = g_string_new (NULL);
	cursor = message;
	while (*cursor != '\0')
	{
		const gchar *close;
		gchar *expr;
		g_autoptr(GError) error = NULL;
		LrgScriptVariable *var;

		if (*cursor != '{')
		{
			g_string_append_c (out, *cursor);
			cursor++;
			continue;
		}
		close = strchr (cursor + 1, '}');
		if (close == NULL)
		{
			g_string_append (out, cursor);
			break;
		}
		expr = g_strndup (cursor + 1, (gsize) (close - (cursor + 1)));
		var = eval_innermost (self, scripting, expr, &error);
		g_free (expr);
		if (var == NULL)
		{
			lrg_script_debugger_emit_output (self, "console",
			                                 error != NULL ? error->message : "log expression failed");
			g_string_append (out, "{?}");
		}
		else
		{
			g_string_append (out, lrg_script_variable_get_value (var));
			g_object_unref (var);
		}
		cursor = close + 1;
	}
	return g_string_free (out, FALSE);
}

static gboolean
consider_breakpoint (LrgScriptDebugger  *self,
                     LrgScripting       *scripting,
                     BpSnap             *snap,
                     gboolean           *stop)
{
	gboolean failed = FALSE;
	gboolean truthy = TRUE;
	guint hits;
	LrgScriptBreakpoint *bp;

	if (snap->condition != NULL && snap->condition[0] != '\0')
	{
		g_autoptr(GError) error = NULL;
		LrgScriptVariable *var = eval_innermost (self, scripting, snap->condition, &error);

		if (var == NULL)
		{
			failed = TRUE;
			lrg_script_debugger_emit_output (self, "console",
			                                 error != NULL ? error->message : "condition failed");
		}
		else
		{
			truthy = lrg_script_variable_get_truthy (var);
			g_object_unref (var);
		}
	}

	if (!failed && !truthy)
		return FALSE;

	g_mutex_lock (&self->mutex);
	bp = breakpoint_by_id_locked (self, snap->id);
	if (bp == NULL)
	{
		g_mutex_unlock (&self->mutex);
		return FALSE;
	}
	g_mutex_lock (&bp->mutex);
	if (failed)
		hits = bp->hit_count;
	else
	{
		bp->hit_count++;
		hits = bp->hit_count;
	}
	g_mutex_unlock (&bp->mutex);
	g_mutex_unlock (&self->mutex);

	if (!failed && !hit_condition_matches (snap->hit_condition, hits))
		return FALSE;

	if (!failed && snap->log_message != NULL && snap->log_message[0] != '\0')
	{
		g_autofree gchar *text = expand_log_message (self, scripting, snap->log_message);

		lrg_script_debugger_emit_output (self, "console", text);
		return FALSE;
	}

	*stop = TRUE;
	g_mutex_lock (&self->mutex);
	bp = breakpoint_by_id_locked (self, snap->id);
	if (bp != NULL)
	{
		g_clear_object (&self->stopped_bp);
		self->stopped_bp = g_object_ref (bp);
	}
	g_mutex_unlock (&self->mutex);
	return TRUE;
}

static gboolean
step_matches (LrgScriptDebugger *self,
              guint              depth)
{
	if (self->step == STEP_IN)
		return TRUE;
	if (self->step == STEP_OVER && depth <= self->step_depth)
		return TRUE;
	if (self->step == STEP_OUT && depth < self->step_depth)
		return TRUE;
	if (self->break_on_next)
		return TRUE;
	return FALSE;
}

static void
begin_stop (LrgScriptDebugger   *self,
            LrgScripting        *scripting,
            LrgScriptStopReason  reason,
            guint                depth,
            const gchar         *description)
{
	Attached *entry;

	g_mutex_lock (&self->mutex);
	while (self->in_stop && self->owner != g_thread_self ())
		g_cond_wait (&self->cond, &self->mutex);
	if (self->in_stop && self->owner == g_thread_self ())
	{
		g_mutex_unlock (&self->mutex);
		return;
	}

	self->reason = reason;
	self->stopped = scripting;
	self->stopped_depth = depth == 0 ? 1 : depth;
	entry = find_attached_locked (self, scripting);
	self->stopped_thread_id = entry != NULL ? entry->thread_id : 0;
	g_free (self->stop_description);
	self->stop_description = g_strdup (description);
	self->step = STEP_NONE;
	self->break_on_next = FALSE;
	self->pause_requested = FALSE;
	refresh_flags_locked (self);

	stop_loop (self);
	g_mutex_unlock (&self->mutex);

	drop_inspection (self);
	debug_update_all (self);
}

static void
report_location (LrgScriptDebugger   *self,
                 LrgScriptDebuggable *source,
                 const gchar         *file,
                 gint                 line,
                 const gchar         *node_id,
                 guint                depth)
{
	LrgScripting *scripting = LRG_SCRIPTING (source);
	g_autofree gchar *canon = NULL;
	g_autoptr(GPtrArray) snaps = NULL;
	gboolean stop = FALSE;
	LrgScriptStopReason reason = LRG_SCRIPT_STOP_REASON_NONE;
	gboolean pause = FALSE;
	guint i;

	if (depth == 0)
		depth = 1;
	canon = lrg_script_debugger_normalize_path (file);

	g_object_ref (self);
	g_mutex_lock (&self->mutex);
	if (self->suppress > 0 ||
	    (self->in_stop && self->owner == g_thread_self ()))
	{
		g_mutex_unlock (&self->mutex);
		g_object_unref (self);
		return;
	}
	pause = self->pause_requested;
	g_mutex_unlock (&self->mutex);

	snaps = matching_breakpoints (self, canon, line, node_id);
	for (i = 0; i < snaps->len && !stop; i++)
	{
		if (consider_breakpoint (self, scripting, g_ptr_array_index (snaps, i), &stop))
			reason = LRG_SCRIPT_STOP_REASON_BREAKPOINT;
	}

	g_mutex_lock (&self->mutex);
	if (!stop && step_matches (self, depth))
	{
		stop = TRUE;
		reason = LRG_SCRIPT_STOP_REASON_STEP;
	}
	else if (!stop && pause)
	{
		stop = TRUE;
		reason = LRG_SCRIPT_STOP_REASON_PAUSE;
	}
	g_mutex_unlock (&self->mutex);

	if (stop)
	{
		gchar *description;

		if (node_id != NULL)
			description = g_strdup_printf ("%s at %s node %s",
			                               lrg_script_stop_reason_to_string (reason),
			                               canon != NULL ? canon : "",
			                               node_id);
		else
			description = g_strdup_printf ("%s at %s:%d",
			                               lrg_script_stop_reason_to_string (reason),
			                               canon != NULL ? canon : "",
			                               line);
		begin_stop (self, scripting, reason, depth, description);
		g_free (description);
	}
	g_object_unref (self);
}

void
lrg_script_debugger_note_host_return (LrgScriptDebugger *self)
{
	gboolean update = FALSE;

	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));

	g_mutex_lock (&self->mutex);
	if (self->state == LRG_SCRIPT_DEBUGGER_STATE_RUNNING &&
	    (self->step == STEP_OVER || self->step == STEP_OUT))
	{
		self->step = STEP_NONE;
		self->break_on_next = TRUE;
		refresh_flags_locked (self);
		update = TRUE;
	}
	g_mutex_unlock (&self->mutex);
	if (update)
		debug_update_all (self);
}

static void
auto_attach_hook (LrgScripting *scripting,
                  gpointer      user_data)
{
	LrgScriptDebugger *self = (LrgScriptDebugger *) user_data;
	g_autoptr(GError) error = NULL;

	if (!lrg_script_debugger_supports (scripting))
		return;
	if (!lrg_script_debugger_attach (self, scripting, &error))
		lrg_debug (LRG_LOG_DOMAIN_SCRIPTING, "auto-attach: %s", error->message);
}

static void
request_step (LrgScriptDebugger *self,
              StepMode           mode)
{
	gboolean paused;

	g_mutex_lock (&self->mutex);
	paused = self->in_stop;
	if (!paused)
	{
		g_mutex_unlock (&self->mutex);
		return;
	}
	self->step = mode;
	if (mode == STEP_OVER || mode == STEP_OUT)
		self->step_depth = self->stopped_depth > 0 ? self->stopped_depth : 1;
	self->break_on_next = FALSE;
	refresh_flags_locked (self);
	self->resume_requested = TRUE;
	g_cond_broadcast (&self->cond);
	g_mutex_unlock (&self->mutex);
}

static void
lrg_script_debugger_finalize (GObject *object)
{
	LrgScriptDebugger *self = LRG_SCRIPT_DEBUGGER (object);

	lrg_script_debugger_detach_all (self);

	if (self->idle_destroy != NULL && self->idle_data != NULL)
		self->idle_destroy (self->idle_data);
	self->idle_func = NULL;
	self->idle_destroy = NULL;

	g_clear_pointer (&self->attached, g_ptr_array_unref);
	g_clear_pointer (&self->breakpoints, g_ptr_array_unref);
	g_clear_pointer (&self->break_lines, g_hash_table_unref);
	g_clear_pointer (&self->stop_description, g_free);
	g_clear_object (&self->stopped_bp);
	g_clear_pointer (&self->req_expr, g_free);
	g_clear_pointer (&self->req_array, g_ptr_array_unref);
	g_clear_object (&self->req_var);
	g_clear_error (&self->req_error);
	drop_inspection (self);
	g_cond_clear (&self->cond);
	g_mutex_clear (&self->mutex);

	G_OBJECT_CLASS (lrg_script_debugger_parent_class)->finalize (object);
}

static void
lrg_script_debugger_class_init (LrgScriptDebuggerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_script_debugger_finalize;

	/**
	 * LrgScriptDebugger::output:
	 * @debugger: the debugger
	 * @category: "console", "stdout", "stderr" or "important"
	 * @text: the text
	 *
	 * Emitted for log points, condition errors and forwarded script prints.
	 */
	dbg_signals[SIGNAL_OUTPUT] =
		g_signal_new ("output",
		              LRG_TYPE_SCRIPT_DEBUGGER,
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
attached_free (gpointer data)
{
	g_free (data);
}

static void
lrg_script_debugger_init (LrgScriptDebugger *self)
{
	g_mutex_init (&self->mutex);
	g_cond_init (&self->cond);
	self->attached = g_ptr_array_new_with_free_func (attached_free);
	self->breakpoints = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	self->break_lines = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->state = LRG_SCRIPT_DEBUGGER_STATE_RUNNING;
	self->exception_mode = LRG_SCRIPT_EXCEPTION_MODE_NEVER;
	self->iterate_main = TRUE;
	self->next_bp_id = 1;
	self->next_thread_id = 1;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgScriptDebugger *
lrg_script_debugger_new (void)
{
	return g_object_new (LRG_TYPE_SCRIPT_DEBUGGER, NULL);
}

LrgScriptDebugger *
lrg_script_debugger_get_default (void)
{
	static gsize instance_bits = 0;

	if (g_once_init_enter (&instance_bits))
	{
		LrgScriptDebugger *created = lrg_script_debugger_new ();

		g_once_init_leave (&instance_bits, (gsize) created);
	}
	return (LrgScriptDebugger *) instance_bits;
}

gboolean
lrg_script_debugger_supports (LrgScripting *scripting)
{
	g_return_val_if_fail (LRG_IS_SCRIPTING (scripting), FALSE);
	return LRG_IS_SCRIPT_DEBUGGABLE (scripting);
}

gboolean
lrg_script_debugger_attach (LrgScriptDebugger  *self,
                            LrgScripting       *scripting,
                            GError            **error)
{
	LrgScriptDebuggableInterface *iface;
	Attached *entry;
	gboolean ok;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), FALSE);
	g_return_val_if_fail (LRG_IS_SCRIPTING (scripting), FALSE);

	if (!LRG_IS_SCRIPT_DEBUGGABLE (scripting))
	{
		g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
		             LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED,
		             "%s", unsupported_reason (scripting));
		return FALSE;
	}

	g_mutex_lock (&self->mutex);
	if (find_attached_locked (self, scripting) != NULL)
	{
		g_mutex_unlock (&self->mutex);
		return TRUE;
	}
	g_mutex_unlock (&self->mutex);

	iface = LRG_SCRIPT_DEBUGGABLE_GET_IFACE (scripting);
	if (iface->debug_attach == NULL)
	{
		g_set_error (error, LRG_SCRIPT_DEBUGGER_ERROR,
		             LRG_SCRIPT_DEBUGGER_ERROR_UNSUPPORTED,
		             "%s", unsupported_reason (scripting));
		return FALSE;
	}

	ok = iface->debug_attach (LRG_SCRIPT_DEBUGGABLE (scripting), self, error);
	if (!ok)
		return FALSE;

	entry = g_new0 (Attached, 1);
	entry->scripting = scripting;
	g_mutex_lock (&self->mutex);
	entry->thread_id = self->next_thread_id++;
	g_ptr_array_add (self->attached, entry);
	g_mutex_unlock (&self->mutex);
	g_object_weak_ref (G_OBJECT (scripting), on_scripting_weak, self);
	return TRUE;
}

void
lrg_script_debugger_detach (LrgScriptDebugger *self,
                            LrgScripting      *scripting)
{
	LrgScriptDebuggableInterface *iface;
	gboolean found = FALSE;

	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_return_if_fail (LRG_IS_SCRIPTING (scripting));

	g_mutex_lock (&self->mutex);
	if (find_attached_locked (self, scripting) != NULL)
	{
		guint i;

		for (i = 0; i < self->attached->len; i++)
		{
			Attached *entry = g_ptr_array_index (self->attached, i);

			if (entry->scripting == scripting)
			{
				g_ptr_array_remove_index (self->attached, i);
				found = TRUE;
				break;
			}
		}
	}
	if ((GObject *) self->stopped == (GObject *) scripting)
		self->resume_requested = TRUE;
	g_cond_broadcast (&self->cond);
	g_mutex_unlock (&self->mutex);

	if (!found)
		return;

	g_object_weak_unref (G_OBJECT (scripting), on_scripting_weak, self);
	iface = debuggable_iface (scripting);
	if (iface != NULL && iface->debug_detach != NULL)
		iface->debug_detach (LRG_SCRIPT_DEBUGGABLE (scripting), self);
}

void
lrg_script_debugger_detach_all (LrgScriptDebugger *self)
{
	GPtrArray *copy;
	guint i;
	gboolean wait = FALSE;
	gint64 deadline;

	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));

	g_mutex_lock (&self->mutex);
	self->auto_attach = FALSE;
	if (self->instance_hook)
	{
		self->instance_hook = FALSE;
		g_mutex_unlock (&self->mutex);
		lrg_scripting_priv_remove_instance_hook (auto_attach_hook, self);
		g_mutex_lock (&self->mutex);
	}
	self->resume_requested = TRUE;
	self->step = STEP_NONE;
	self->break_on_next = FALSE;
	self->pause_requested = FALSE;
	refresh_flags_locked (self);
	g_cond_broadcast (&self->cond);
	wait = self->in_stop && self->owner != g_thread_self ();
	deadline = g_get_monotonic_time () + LRG_SCRIPT_DEBUGGER_TIMEOUT_US;
	while (wait && self->in_stop)
	{
		if (!g_cond_wait_until (&self->cond, &self->mutex, deadline))
			break;
	}

	copy = g_ptr_array_new ();
	for (i = 0; i < self->attached->len; i++)
	{
		Attached *entry = g_ptr_array_index (self->attached, i);

		g_ptr_array_add (copy, entry->scripting);
	}
	g_ptr_array_set_size (self->attached, 0);
	g_mutex_unlock (&self->mutex);

	for (i = 0; i < copy->len; i++)
	{
		LrgScripting *scripting = g_ptr_array_index (copy, i);
		LrgScriptDebuggableInterface *iface;

		if (G_OBJECT (scripting)->ref_count == 0)
			continue;
		g_object_weak_unref (G_OBJECT (scripting), on_scripting_weak, self);
		iface = debuggable_iface (scripting);
		if (iface != NULL && iface->debug_detach != NULL)
			iface->debug_detach (LRG_SCRIPT_DEBUGGABLE (scripting), self);
	}
	g_ptr_array_unref (copy);
}

gboolean
lrg_script_debugger_is_attached (LrgScriptDebugger *self,
                                 LrgScripting      *scripting)
{
	gboolean found;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), FALSE);
	g_return_val_if_fail (LRG_IS_SCRIPTING (scripting), FALSE);
	g_mutex_lock (&self->mutex);
	found = find_attached_locked (self, scripting) != NULL;
	g_mutex_unlock (&self->mutex);
	return found;
}

GPtrArray *
lrg_script_debugger_get_attached (LrgScriptDebugger *self)
{
	GPtrArray *out;
	guint i;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	out = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_mutex_lock (&self->mutex);
	for (i = 0; i < self->attached->len; i++)
	{
		Attached *entry = g_ptr_array_index (self->attached, i);

		if (G_OBJECT (entry->scripting)->ref_count == 0)
			continue;
		g_ptr_array_add (out, g_object_ref (entry->scripting));
	}
	g_mutex_unlock (&self->mutex);
	return out;
}

guint
lrg_script_debugger_get_thread_id (LrgScriptDebugger *self,
                                   LrgScripting      *scripting)
{
	Attached *entry;
	guint id = 0;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), 0);
	g_return_val_if_fail (LRG_IS_SCRIPTING (scripting), 0);
	g_mutex_lock (&self->mutex);
	entry = find_attached_locked (self, scripting);
	if (entry != NULL)
		id = entry->thread_id;
	g_mutex_unlock (&self->mutex);
	return id;
}

void
lrg_script_debugger_set_auto_attach (LrgScriptDebugger *self,
                                     gboolean           auto_attach)
{
	g_autoptr(GPtrArray) instances = NULL;
	guint i;

	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	auto_attach = !!auto_attach;

	g_mutex_lock (&self->mutex);
	if (self->auto_attach == auto_attach)
	{
		g_mutex_unlock (&self->mutex);
		return;
	}
	self->auto_attach = auto_attach;
	if (auto_attach && !self->instance_hook)
	{
		self->instance_hook = TRUE;
		g_mutex_unlock (&self->mutex);
		lrg_scripting_priv_add_instance_hook (auto_attach_hook, self);
	}
	else if (!auto_attach && self->instance_hook)
	{
		self->instance_hook = FALSE;
		g_mutex_unlock (&self->mutex);
		lrg_scripting_priv_remove_instance_hook (auto_attach_hook, self);
	}
	else
		g_mutex_unlock (&self->mutex);

	if (!auto_attach)
		return;

	instances = lrg_scripting_list_instances ();
	for (i = 0; i < instances->len; i++)
		auto_attach_hook (g_ptr_array_index (instances, i), self);
}

gboolean
lrg_script_debugger_get_auto_attach (LrgScriptDebugger *self)
{
	gboolean value;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), FALSE);
	g_mutex_lock (&self->mutex);
	value = self->auto_attach;
	g_mutex_unlock (&self->mutex);
	return value;
}

static LrgScriptBreakpoint *
add_breakpoint_internal (LrgScriptDebugger *self,
                         const gchar       *file,
                         gint               line,
                         const gchar       *node_id)
{
	g_autofree gchar *canon = lrg_script_debugger_normalize_path (file);
	LrgScriptBreakpoint *created;
	guint i;

	g_mutex_lock (&self->mutex);
	for (i = 0; i < self->breakpoints->len; i++)
	{
		LrgScriptBreakpoint *bp = g_ptr_array_index (self->breakpoints, i);
		gboolean same_line;
		gboolean same_node;

		same_node = node_id != NULL && g_strcmp0 (bp->node_id, node_id) == 0 &&
		            same_path (bp->file, canon);
		same_line = node_id == NULL && bp->node_id == NULL &&
		            bp->line == line && same_path (bp->file, canon);
		if (same_node || same_line)
		{
			g_object_ref (bp);
			g_mutex_unlock (&self->mutex);
			return bp;
		}
	}
	created = breakpoint_new (self, self->next_bp_id++, canon, line, node_id);
	g_ptr_array_add (self->breakpoints, g_object_ref (created));
	rebuild_lines_locked (self);
	g_mutex_unlock (&self->mutex);
	debug_update_all (self);
	return created;
}

LrgScriptBreakpoint *
lrg_script_debugger_add_breakpoint (LrgScriptDebugger *self,
                                    const gchar       *file,
                                    gint               line)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	g_return_val_if_fail (file != NULL, NULL);
	return add_breakpoint_internal (self, file, line, NULL);
}

LrgScriptBreakpoint *
lrg_script_debugger_add_node_breakpoint (LrgScriptDebugger *self,
                                         const gchar       *file,
                                         const gchar       *node_id)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	g_return_val_if_fail (node_id != NULL, NULL);
	return add_breakpoint_internal (self, file, 0, node_id);
}

gboolean
lrg_script_debugger_remove_breakpoint (LrgScriptDebugger   *self,
                                       LrgScriptBreakpoint *breakpoint)
{
	gboolean removed = FALSE;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), FALSE);
	g_return_val_if_fail (LRG_IS_SCRIPT_BREAKPOINT (breakpoint), FALSE);

	g_mutex_lock (&breakpoint->mutex);
	if (breakpoint->debugger == self)
		breakpoint->debugger = NULL;
	g_mutex_unlock (&breakpoint->mutex);

	g_mutex_lock (&self->mutex);
	if (g_ptr_array_remove (self->breakpoints, breakpoint))
	{
		removed = TRUE;
		rebuild_lines_locked (self);
	}
	g_mutex_unlock (&self->mutex);
	if (removed)
		debug_update_all (self);
	return removed;
}

void
lrg_script_debugger_clear_breakpoints (LrgScriptDebugger *self,
                                       const gchar       *file)
{
	g_autofree gchar *canon = NULL;
	guint i;
	gboolean changed = FALSE;

	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	if (file != NULL)
		canon = lrg_script_debugger_normalize_path (file);

	g_mutex_lock (&self->mutex);
	for (i = self->breakpoints->len; i > 0; i--)
	{
		LrgScriptBreakpoint *bp = g_ptr_array_index (self->breakpoints, i - 1);

		if (canon != NULL && !same_path (bp->file, canon))
			continue;
		g_mutex_lock (&bp->mutex);
		bp->debugger = NULL;
		g_mutex_unlock (&bp->mutex);
		g_ptr_array_remove_index (self->breakpoints, i - 1);
		changed = TRUE;
	}
	if (changed)
		rebuild_lines_locked (self);
	g_mutex_unlock (&self->mutex);
	if (changed)
		debug_update_all (self);
}

GPtrArray *
lrg_script_debugger_get_breakpoints (LrgScriptDebugger *self)
{
	GPtrArray *out;
	guint i;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	out = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_mutex_lock (&self->mutex);
	for (i = 0; i < self->breakpoints->len; i++)
		g_ptr_array_add (out, g_object_ref (g_ptr_array_index (self->breakpoints, i)));
	g_mutex_unlock (&self->mutex);
	return out;
}

LrgScriptBreakpoint *
lrg_script_debugger_find_breakpoint (LrgScriptDebugger *self,
                                     guint              id)
{
	LrgScriptBreakpoint *found = NULL;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	g_mutex_lock (&self->mutex);
	found = breakpoint_by_id_locked (self, id);
	g_mutex_unlock (&self->mutex);
	return found;
}

void
lrg_script_debugger_set_exception_mode (LrgScriptDebugger      *self,
                                        LrgScriptExceptionMode  mode)
{
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_mutex_lock (&self->mutex);
	self->exception_mode = mode;
	g_mutex_unlock (&self->mutex);
	debug_update_all (self);
}

LrgScriptExceptionMode
lrg_script_debugger_get_exception_mode (LrgScriptDebugger *self)
{
	LrgScriptExceptionMode mode;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), LRG_SCRIPT_EXCEPTION_MODE_NEVER);
	g_mutex_lock (&self->mutex);
	mode = self->exception_mode;
	g_mutex_unlock (&self->mutex);
	return mode;
}

void
lrg_script_debugger_pause (LrgScriptDebugger *self)
{
	gboolean already;

	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_mutex_lock (&self->mutex);
	already = self->in_stop;
	if (!already)
	{
		self->pause_requested = TRUE;
		refresh_flags_locked (self);
	}
	g_mutex_unlock (&self->mutex);
	if (!already)
		debug_update_all (self);
}

void
lrg_script_debugger_resume (LrgScriptDebugger *self)
{
	gboolean paused;

	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_mutex_lock (&self->mutex);
	self->step = STEP_NONE;
	self->break_on_next = FALSE;
	self->pause_requested = FALSE;
	refresh_flags_locked (self);
	paused = self->in_stop;
	if (paused)
	{
		self->resume_requested = TRUE;
		g_cond_broadcast (&self->cond);
	}
	g_mutex_unlock (&self->mutex);
	if (!paused)
		debug_update_all (self);
}

void
lrg_script_debugger_step_in (LrgScriptDebugger *self)
{
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	request_step (self, STEP_IN);
}

void
lrg_script_debugger_step_over (LrgScriptDebugger *self)
{
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	request_step (self, STEP_OVER);
}

void
lrg_script_debugger_step_out (LrgScriptDebugger *self)
{
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	request_step (self, STEP_OUT);
}

LrgScriptDebuggerState
lrg_script_debugger_get_state (LrgScriptDebugger *self)
{
	LrgScriptDebuggerState state;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), LRG_SCRIPT_DEBUGGER_STATE_RUNNING);
	g_mutex_lock (&self->mutex);
	state = self->state;
	g_mutex_unlock (&self->mutex);
	return state;
}

gboolean
lrg_script_debugger_is_paused (LrgScriptDebugger *self)
{
	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), FALSE);
	return lrg_script_debugger_get_state (self) == LRG_SCRIPT_DEBUGGER_STATE_PAUSED;
}

LrgScriptStopReason
lrg_script_debugger_get_stop_reason (LrgScriptDebugger *self)
{
	LrgScriptStopReason reason;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), LRG_SCRIPT_STOP_REASON_NONE);
	g_mutex_lock (&self->mutex);
	reason = self->reason;
	g_mutex_unlock (&self->mutex);
	return reason;
}

gchar *
lrg_script_debugger_dup_stop_description (LrgScriptDebugger *self)
{
	gchar *copy;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	g_mutex_lock (&self->mutex);
	copy = g_strdup (self->stop_description);
	g_mutex_unlock (&self->mutex);
	return copy;
}

LrgScripting *
lrg_script_debugger_dup_stopped_scripting (LrgScriptDebugger *self)
{
	LrgScripting *scripting = NULL;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	g_mutex_lock (&self->mutex);
	if (self->stopped != NULL && G_OBJECT (self->stopped)->ref_count > 0)
		scripting = g_object_ref (self->stopped);
	g_mutex_unlock (&self->mutex);
	return scripting;
}

guint
lrg_script_debugger_get_stopped_thread_id (LrgScriptDebugger *self)
{
	guint id;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), 0);
	g_mutex_lock (&self->mutex);
	id = self->stopped_thread_id;
	g_mutex_unlock (&self->mutex);
	return id;
}

LrgScriptBreakpoint *
lrg_script_debugger_dup_stopped_breakpoint (LrgScriptDebugger *self)
{
	LrgScriptBreakpoint *bp = NULL;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	g_mutex_lock (&self->mutex);
	if (self->stopped_bp != NULL)
		bp = g_object_ref (self->stopped_bp);
	g_mutex_unlock (&self->mutex);
	return bp;
}

void
lrg_script_debugger_set_stopped_idle_func (LrgScriptDebugger         *self,
                                           LrgScriptDebuggerIdleFunc  func,
                                           gpointer                   user_data,
                                           GDestroyNotify             destroy)
{
	GDestroyNotify old_destroy;
	gpointer old_data;

	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_mutex_lock (&self->mutex);
	old_destroy = self->idle_destroy;
	old_data = self->idle_data;
	self->idle_func = func;
	self->idle_data = user_data;
	self->idle_destroy = destroy;
	g_mutex_unlock (&self->mutex);
	if (old_destroy != NULL && old_data != NULL)
		old_destroy (old_data);
}

void
lrg_script_debugger_set_iterate_main_context (LrgScriptDebugger *self,
                                              gboolean           iterate)
{
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_mutex_lock (&self->mutex);
	self->iterate_main = !!iterate;
	g_mutex_unlock (&self->mutex);
}

GPtrArray *
lrg_script_debugger_get_stack_frames (LrgScriptDebugger  *self,
                                      GError            **error)
{
	GPtrArray *frames = NULL;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	if (!post_request (self, REQ_FRAMES, 0, 0, NULL, &frames, NULL, error))
		return NULL;
	return frames;
}

GPtrArray *
lrg_script_debugger_get_scopes (LrgScriptDebugger  *self,
                                guint               frame_id,
                                GError            **error)
{
	GPtrArray *scopes = NULL;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	if (!post_request (self, REQ_SCOPES, frame_id, 0, NULL, &scopes, NULL, error))
		return NULL;
	return scopes;
}

GPtrArray *
lrg_script_debugger_get_variables (LrgScriptDebugger  *self,
                                   guint               variables_reference,
                                   GError            **error)
{
	GPtrArray *vars = NULL;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	if (!post_request (self, REQ_VARS, 0, variables_reference, NULL, &vars, NULL, error))
		return NULL;
	return vars;
}

LrgScriptVariable *
lrg_script_debugger_evaluate (LrgScriptDebugger  *self,
                              guint               frame_id,
                              const gchar        *expression,
                              GError            **error)
{
	LrgScriptVariable *var = NULL;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), NULL);
	g_return_val_if_fail (expression != NULL, NULL);
	if (!post_request (self, REQ_EVAL, frame_id, 0, expression, NULL, &var, error))
		return NULL;
	return var;
}

gboolean
lrg_script_debugger_wants_line_events (LrgScriptDebugger *self)
{
	gint flags;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), FALSE);
	flags = g_atomic_int_get (&self->event_flags);
	return (flags & (LRG_DBG_FLAG_PAUSE | LRG_DBG_FLAG_STEP |
	                 LRG_DBG_FLAG_BREAK_NEXT | LRG_DBG_FLAG_BPS)) != 0;
}

gboolean
lrg_script_debugger_line_may_break (LrgScriptDebugger *self,
                                    gint               line)
{
	gint flags;
	gboolean hit;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), FALSE);
	flags = g_atomic_int_get (&self->event_flags);
	if (flags & (LRG_DBG_FLAG_PAUSE | LRG_DBG_FLAG_STEP | LRG_DBG_FLAG_BREAK_NEXT))
		return TRUE;
	g_mutex_lock (&self->mutex);
	hit = g_hash_table_contains (self->break_lines, GINT_TO_POINTER (line));
	g_mutex_unlock (&self->mutex);
	return hit;
}

void
lrg_script_debugger_report_line (LrgScriptDebugger   *self,
                                 LrgScriptDebuggable *source,
                                 const gchar         *file,
                                 gint                 line,
                                 guint                depth)
{
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGABLE (source));
	report_location (self, source, file, line, NULL, depth);
}

void
lrg_script_debugger_report_node (LrgScriptDebugger   *self,
                                 LrgScriptDebuggable *source,
                                 const gchar         *file,
                                 const gchar         *node_id,
                                 guint                depth)
{
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGABLE (source));
	g_return_if_fail (node_id != NULL);
	report_location (self, source, file, 0, node_id, depth);
}

void
lrg_script_debugger_report_exception (LrgScriptDebugger   *self,
                                      LrgScriptDebuggable *source,
                                      const gchar         *file,
                                      gint                 line,
                                      const gchar         *message,
                                      gboolean             uncaught)
{
	LrgScriptExceptionMode mode;
	gboolean suppress;
	gchar *description;

	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGABLE (source));

	g_object_ref (self);
	g_mutex_lock (&self->mutex);
	mode = self->exception_mode;
	suppress = self->suppress > 0 || (self->in_stop && self->owner == g_thread_self ());
	g_mutex_unlock (&self->mutex);

	if (suppress || mode == LRG_SCRIPT_EXCEPTION_MODE_NEVER)
	{
		g_object_unref (self);
		return;
	}
	if (mode == LRG_SCRIPT_EXCEPTION_MODE_UNCAUGHT && !uncaught)
	{
		g_object_unref (self);
		return;
	}

	description = g_strdup (message != NULL ? message : "exception");
	begin_stop (self, LRG_SCRIPTING (source), LRG_SCRIPT_STOP_REASON_EXCEPTION,
	            1, description);
	g_free (description);
	(void) file;
	(void) line;
	g_object_unref (self);
}

guint
lrg_script_debugger_alloc_reference (LrgScriptDebugger *self,
                                     gpointer           handle,
                                     GDestroyNotify     destroy)
{
	VarRef ref;
	guint id;

	g_return_val_if_fail (LRG_IS_SCRIPT_DEBUGGER (self), 0);

	g_mutex_lock (&self->mutex);
	if (self->refs == NULL)
	{
		VarRef dummy;

		self->refs = g_array_new (FALSE, TRUE, sizeof (VarRef));
		dummy.handle = NULL;
		dummy.destroy = NULL;
		g_array_append_val (self->refs, dummy);
	}
	ref.handle = handle;
	ref.destroy = destroy;
	g_array_append_val (self->refs, ref);
	id = self->refs->len - 1;
	g_mutex_unlock (&self->mutex);
	return id;
}

void
lrg_script_debugger_emit_output (LrgScriptDebugger *self,
                                 const gchar       *category,
                                 const gchar       *text)
{
	g_return_if_fail (LRG_IS_SCRIPT_DEBUGGER (self));
	g_signal_emit (self, dbg_signals[SIGNAL_OUTPUT], 0,
	               category != NULL ? category : "console",
	               text != NULL ? text : "");
}
