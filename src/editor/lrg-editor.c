/* lrg-editor.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Host-agnostic editor runtime for an #LrgLevel.
 *
 * Overview of the moving parts:
 *
 *  - Every mutation is an #LrgEditorCommand.  execute_command() applies it
 *    and records it either on the undo stack or, while a group is open, in
 *    the pending group (begin_group / end_group).
 *
 *  - Dirty tracking compares the serial of the command on top of the undo
 *    stack with the serial recorded when the document was last loaded or
 *    saved.  Serials are unique and refreshed on merge, so a merge into the
 *    saved command, an undo past the save point, or history truncation all
 *    correctly read as "dirty" without extra bookkeeping.
 *
 *  - The level's node-removed signal prunes the selection, so undoing an add
 *    or deleting a parent never leaves a dangling selected node.
 */

#include "lrg-editor.h"
#include "lrg-editor-command.h"
#include "lrg-editor-selection.h"
#include "lrg-editor-math.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"
#include "lrg-level-serializer.h"
#include "lrg-prefab.h"
#include "../lrg-enums.h"
#include <graylib.h>
#include <gio/gio.h>
#include <string.h>

#define DEFAULT_HISTORY_LIMIT (1000)

struct _LrgEditor
{
	GObject parent_instance;

	LrgLevel           *level;
	gulong              level_removed_id;
	gchar              *path;
	LrgEditorSelection *selection;
	GPtrArray          *undo_stack;   /* owned LrgEditorCommand* */
	GPtrArray          *redo_stack;   /* owned LrgEditorCommand* */
	guint               history_limit;

	/* Grouping: the outermost open group collects executed commands. */
	LrgEditorCommand   *open_group;
	guint               group_depth;

	/* Dirty tracking (see file comment). */
	guint64             clean_serial;
	gboolean            last_dirty;

	gchar              *clipboard;

	LrgEditorTool       tool;
	LrgEditorSpace      space;
	gboolean            snap_enabled;
	gdouble             snap_translate;
	gdouble             snap_rotate;
	gdouble             snap_scale;
};

G_DEFINE_FINAL_TYPE (LrgEditor, lrg_editor, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_LEVEL,
	PROP_PATH,
	PROP_DIRTY,
	PROP_TOOL,
	PROP_SPACE,
	PROP_SNAP_ENABLED,
	PROP_HISTORY_LIMIT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_CHANGED,
	SIGNAL_LEVEL_REPLACED,
	SIGNAL_SAVED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void attach_level (LrgEditor *self, LrgLevel *level);

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_dispose (GObject *object)
{
	LrgEditor *self = LRG_EDITOR (object);

	if (self->level != NULL && self->level_removed_id != 0)
		g_signal_handler_disconnect (self->level, self->level_removed_id);
	self->level_removed_id = 0;

	G_OBJECT_CLASS (lrg_editor_parent_class)->dispose (object);
}

static void
lrg_editor_finalize (GObject *object)
{
	LrgEditor *self = LRG_EDITOR (object);

	g_clear_object (&self->level);
	g_clear_pointer (&self->path, g_free);
	g_clear_object (&self->selection);
	g_clear_pointer (&self->undo_stack, g_ptr_array_unref);
	g_clear_pointer (&self->redo_stack, g_ptr_array_unref);
	g_clear_object (&self->open_group);
	g_clear_pointer (&self->clipboard, g_free);

	G_OBJECT_CLASS (lrg_editor_parent_class)->finalize (object);
}

static void
lrg_editor_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgEditor *self = LRG_EDITOR (object);

	switch (prop_id)
	{
	case PROP_LEVEL:
		g_value_set_object (value, self->level);
		break;
	case PROP_PATH:
		g_value_set_string (value, self->path);
		break;
	case PROP_DIRTY:
		g_value_set_boolean (value, lrg_editor_is_dirty (self));
		break;
	case PROP_TOOL:
		g_value_set_enum (value, self->tool);
		break;
	case PROP_SPACE:
		g_value_set_enum (value, self->space);
		break;
	case PROP_SNAP_ENABLED:
		g_value_set_boolean (value, self->snap_enabled);
		break;
	case PROP_HISTORY_LIMIT:
		g_value_set_uint (value, self->history_limit);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgEditor *self = LRG_EDITOR (object);

	switch (prop_id)
	{
	case PROP_PATH:
		lrg_editor_set_path (self, g_value_get_string (value));
		break;
	case PROP_TOOL:
		lrg_editor_set_tool (self, g_value_get_enum (value));
		break;
	case PROP_SPACE:
		lrg_editor_set_space (self, g_value_get_enum (value));
		break;
	case PROP_SNAP_ENABLED:
		lrg_editor_set_snap_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_HISTORY_LIMIT:
		lrg_editor_set_history_limit (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_class_init (LrgEditorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose      = lrg_editor_dispose;
	object_class->finalize     = lrg_editor_finalize;
	object_class->get_property = lrg_editor_get_property;
	object_class->set_property = lrg_editor_set_property;

	/**
	 * LrgEditor:level:
	 *
	 * The level being edited. Replaced wholesale by lrg_editor_set_level(),
	 * lrg_editor_load_level() and lrg_editor_new_level().
	 */
	properties[PROP_LEVEL] =
		g_param_spec_object ("level", "Level", "The edited level",
		                     LRG_TYPE_LEVEL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditor:path:
	 *
	 * The file the document was loaded from or last saved to.
	 */
	properties[PROP_PATH] =
		g_param_spec_string ("path", "Path", "Document file path",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditor:dirty:
	 *
	 * Whether the document has unsaved changes. Notified when it flips.
	 */
	properties[PROP_DIRTY] =
		g_param_spec_boolean ("dirty", "Dirty", "Unsaved changes",
		                      FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditor:tool:
	 *
	 * The active manipulation tool.
	 */
	properties[PROP_TOOL] =
		g_param_spec_enum ("tool", "Tool", "Active manipulation tool",
		                   LRG_TYPE_EDITOR_TOOL, LRG_EDITOR_TOOL_SELECT,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                   G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditor:space:
	 *
	 * The gizmo coordinate space (local or world).
	 */
	properties[PROP_SPACE] =
		g_param_spec_enum ("space", "Space", "Gizmo coordinate space",
		                   LRG_TYPE_EDITOR_SPACE, LRG_EDITOR_SPACE_WORLD,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                   G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditor:snap-enabled:
	 *
	 * Whether gizmo snapping is active.
	 */
	properties[PROP_SNAP_ENABLED] =
		g_param_spec_boolean ("snap-enabled", "Snap Enabled",
		                      "Whether gizmo snapping is active",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEditor:history-limit:
	 *
	 * Maximum number of undo steps kept; 0 means unlimited.
	 */
	properties[PROP_HISTORY_LIMIT] =
		g_param_spec_uint ("history-limit", "History Limit",
		                   "Maximum undo steps (0 = unlimited)",
		                   0, G_MAXUINT, DEFAULT_HISTORY_LIMIT,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                   G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgEditor::changed:
	 * @self: the editor
	 *
	 * Emitted after any operation that mutates the document or history, so a
	 * host can refresh derived views.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgEditor::level-replaced:
	 * @self: the editor
	 * @level: the new #LrgLevel
	 *
	 * Emitted when the whole document is replaced (new, open, reload).
	 * Views holding node pointers must drop them.
	 */
	signals[SIGNAL_LEVEL_REPLACED] =
		g_signal_new ("level-replaced",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_LEVEL);

	/**
	 * LrgEditor::saved:
	 * @self: the editor
	 * @path: the file written
	 *
	 * Emitted after the document was written to disk.
	 */
	signals[SIGNAL_SAVED] =
		g_signal_new ("saved",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_editor_init (LrgEditor *self)
{
	self->selection      = lrg_editor_selection_new ();
	self->undo_stack     = g_ptr_array_new_with_free_func (g_object_unref);
	self->redo_stack     = g_ptr_array_new_with_free_func (g_object_unref);
	self->history_limit  = DEFAULT_HISTORY_LIMIT;
	self->tool           = LRG_EDITOR_TOOL_SELECT;
	self->space          = LRG_EDITOR_SPACE_WORLD;
	self->snap_translate = 1.0;
	self->snap_rotate    = G_PI / 12.0;   /* 15 degrees */
	self->snap_scale     = 0.1;

	attach_level (self, NULL);
}

LrgEditor *
lrg_editor_new (void)
{
	return g_object_new (LRG_TYPE_EDITOR, NULL);
}

/* ==========================================================================
 * Internal: dirty state, history, selection pruning
 * ========================================================================== */

static guint64
current_serial (LrgEditor *self)
{
	if (self->undo_stack->len == 0)
		return 0;

	return lrg_editor_command_get_serial (
		g_ptr_array_index (self->undo_stack, self->undo_stack->len - 1));
}

/* Re-evaluates the dirty flag and notifies when it flips. */
static void
update_dirty (LrgEditor *self)
{
	gboolean dirty = lrg_editor_is_dirty (self);

	if (dirty != self->last_dirty)
	{
		self->last_dirty = dirty;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRTY]);
	}
}

static void
emit_changed (LrgEditor *self)
{
	update_dirty (self);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* Drops the oldest undo steps beyond the history limit. */
static void
enforce_history_limit (LrgEditor *self)
{
	guint excess;

	if (self->history_limit == 0 || self->undo_stack->len <= self->history_limit)
		return;

	excess = self->undo_stack->len - self->history_limit;
	g_ptr_array_remove_range (self->undo_stack, 0, excess);
}

/*
 * record_command:
 *
 * Records an already-applied command: into the open group if any,
 * otherwise onto the undo stack (clearing redo).
 */
static void
record_command (LrgEditor        *self,
                LrgEditorCommand *cmd)
{
	if (self->open_group != NULL)
	{
		lrg_editor_command_group_append (self->open_group, cmd);
		return;
	}

	g_ptr_array_set_size (self->redo_stack, 0);
	g_ptr_array_add (self->undo_stack, cmd);  /* transfer full */
	enforce_history_limit (self);
	emit_changed (self);
}

static void
execute_command (LrgEditor        *self,
                 LrgEditorCommand *cmd)
{
	lrg_editor_command_apply (cmd);
	record_command (self, cmd);

	/* Inside a group the document still changed; let views refresh. */
	if (self->open_group != NULL)
		g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/*
 * try_merge:
 *
 * Folds @cmd into the most recent compatible command (top of the open
 * group or of the undo stack). On success the merged command is re-applied
 * and @cmd is consumed.
 */
static gboolean
try_merge (LrgEditor        *self,
           LrgEditorCommand *cmd)
{
	LrgEditorCommand *top = NULL;

	if (self->open_group != NULL)
	{
		guint n = lrg_editor_command_group_get_n_children (self->open_group);

		if (n > 0)
			top = lrg_editor_command_group_get_child (self->open_group, n - 1);
	}
	else if (self->undo_stack->len > 0 && self->redo_stack->len == 0)
	{
		top = g_ptr_array_index (self->undo_stack, self->undo_stack->len - 1);
	}

	if (top == NULL || !lrg_editor_command_merge (top, cmd))
		return FALSE;

	lrg_editor_command_apply (top);
	g_object_unref (cmd);
	emit_changed (self);
	return TRUE;
}

static void
on_level_node_removed (LrgLevel *level,
                       LrgNode  *node,
                       gpointer  user_data)
{
	LrgEditor *self = LRG_EDITOR (user_data);
	GPtrArray *selected;
	gint       i;

	/* Deselect the removed node and anything below it. */
	selected = lrg_editor_selection_get_nodes (self->selection);
	for (i = (gint) selected->len - 1; i >= 0; i--)
	{
		LrgNode *sel = g_ptr_array_index (selected, i);

		if (lrg_node_is_ancestor_of (node, sel))
			lrg_editor_selection_remove (self->selection, sel);
	}
}

/*
 * attach_level:
 *
 * Swaps in @level (or a fresh empty level when %NULL), moving the
 * node-removed handler, and resets selection, history and dirty state.
 */
static void
attach_level (LrgEditor *self,
              LrgLevel  *level)
{
	if (self->level != NULL && self->level_removed_id != 0)
		g_signal_handler_disconnect (self->level, self->level_removed_id);
	self->level_removed_id = 0;

	lrg_editor_selection_clear (self->selection);
	g_ptr_array_set_size (self->undo_stack, 0);
	g_ptr_array_set_size (self->redo_stack, 0);
	g_clear_object (&self->open_group);
	self->group_depth = 0;

	if (level != NULL)
		g_set_object (&self->level, level);
	else
	{
		g_clear_object (&self->level);
		self->level = lrg_level_new (NULL);
	}

	self->level_removed_id =
		g_signal_connect (self->level, "node-removed",
		                  G_CALLBACK (on_level_node_removed), self);

	self->clean_serial = 0;
}

static void
replace_level (LrgEditor *self,
               LrgLevel  *level)
{
	attach_level (self, level);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LEVEL]);
	g_signal_emit (self, signals[SIGNAL_LEVEL_REPLACED], 0, self->level);
	emit_changed (self);
}

/* ==========================================================================
 * Document
 * ========================================================================== */

LrgLevel *
lrg_editor_get_level (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	return self->level;
}

void
lrg_editor_set_level (LrgEditor *self,
                      LrgLevel  *level)
{
	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_LEVEL (level));

	replace_level (self, level);
}

void
lrg_editor_new_level (LrgEditor   *self,
                      const gchar *name)
{
	g_autoptr(LrgLevel) level = NULL;

	g_return_if_fail (LRG_IS_EDITOR (self));

	level = lrg_level_new (name);
	lrg_editor_set_path (self, NULL);
	replace_level (self, level);
}

gboolean
lrg_editor_load_level (LrgEditor    *self,
                       const gchar  *path,
                       GError      **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(LrgLevel)           loaded = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	ser = lrg_level_serializer_new ();
	loaded = lrg_level_serializer_load_from_file (ser, path, error);
	if (loaded == NULL)
		return FALSE;

	lrg_editor_set_path (self, path);
	replace_level (self, loaded);
	return TRUE;
}

gboolean
lrg_editor_save_level (LrgEditor    *self,
                       const gchar  *path,
                       GError      **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	ser = lrg_level_serializer_new ();
	if (!lrg_level_serializer_save_to_file (ser, self->level, path, error))
		return FALSE;

	lrg_editor_set_path (self, path);
	lrg_editor_mark_clean (self);
	g_signal_emit (self, signals[SIGNAL_SAVED], 0, path);
	return TRUE;
}

gboolean
lrg_editor_save (LrgEditor  *self,
                 GError    **error)
{
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	if (self->path == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
		                     "The level has no file path yet; use Save As");
		return FALSE;
	}

	/* save_level() resets the path; keep our own copy alive meanwhile. */
	path = g_strdup (self->path);
	return lrg_editor_save_level (self, path, error);
}

gboolean
lrg_editor_reload (LrgEditor  *self,
                   GError    **error)
{
	g_autoptr(GPtrArray)  guids = NULL;
	g_autofree gchar     *path = NULL;
	GPtrArray            *selected;
	guint                 i;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	if (self->path == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
		                     "The level has no file to reload from");
		return FALSE;
	}

	/* Remember the selection by GUID; node objects are replaced. */
	guids = g_ptr_array_new_with_free_func (g_free);
	selected = lrg_editor_selection_get_nodes (self->selection);
	for (i = 0; i < selected->len; i++)
		g_ptr_array_add (guids, g_strdup (lrg_node_get_guid (g_ptr_array_index (selected, i))));

	path = g_strdup (self->path);
	if (!lrg_editor_load_level (self, path, error))
		return FALSE;

	for (i = 0; i < guids->len; i++)
	{
		LrgNode *node = lrg_level_find_node (self->level, g_ptr_array_index (guids, i));

		if (node != NULL)
			lrg_editor_selection_add (self->selection, node);
	}

	return TRUE;
}

const gchar *
lrg_editor_get_path (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	return self->path;
}

void
lrg_editor_set_path (LrgEditor   *self,
                     const gchar *path)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	if (g_strcmp0 (self->path, path) == 0)
		return;

	g_free (self->path);
	self->path = g_strdup (path);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PATH]);
}

gboolean
lrg_editor_is_dirty (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	if (self->open_group != NULL &&
	    lrg_editor_command_group_get_n_children (self->open_group) > 0)
		return TRUE;

	return current_serial (self) != self->clean_serial;
}

void
lrg_editor_mark_clean (LrgEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	self->clean_serial = current_serial (self);
	update_dirty (self);
}

void
lrg_editor_mark_modified (LrgEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	/* No command ever has this serial: dirty until the next save. */
	self->clean_serial = G_MAXUINT64;
	update_dirty (self);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

gchar *
lrg_editor_serialize (LrgEditor *self)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	ser = lrg_level_serializer_new ();
	return lrg_level_serializer_save_to_data (ser, self->level, NULL);
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

LrgEditorSelection *
lrg_editor_get_selection (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	return self->selection;
}

void
lrg_editor_select (LrgEditor *self,
                   LrgNode   *node,
                   gboolean   additive)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	if (node == NULL)
		lrg_editor_selection_clear (self->selection);
	else if (additive)
		lrg_editor_selection_add (self->selection, node);
	else
		lrg_editor_selection_set (self->selection, node);
}

void
lrg_editor_toggle_selected (LrgEditor *self,
                            LrgNode   *node)
{
	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));

	if (lrg_editor_selection_contains (self->selection, node))
		lrg_editor_selection_remove (self->selection, node);
	else
		lrg_editor_selection_add (self->selection, node);
}

void
lrg_editor_select_nodes (LrgEditor *self,
                         GPtrArray *nodes,
                         gboolean   additive)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (nodes != NULL);

	if (!additive)
		lrg_editor_selection_clear (self->selection);
	for (i = 0; i < nodes->len; i++)
		lrg_editor_selection_add (self->selection, g_ptr_array_index (nodes, i));
}

static gboolean
collect_all (LrgNode  *node,
             gpointer  user_data)
{
	g_ptr_array_add (user_data, node);
	return TRUE;
}

void
lrg_editor_select_all (LrgEditor *self)
{
	g_autoptr(GPtrArray) all = NULL;
	GPtrArray           *top;
	guint                i;

	g_return_if_fail (LRG_IS_EDITOR (self));

	all = g_ptr_array_new ();
	top = lrg_node_get_children (lrg_level_get_root (self->level));
	for (i = 0; i < top->len; i++)
		lrg_node_foreach (g_ptr_array_index (top, i), collect_all, all);

	lrg_editor_select_nodes (self, all, FALSE);
}

typedef struct
{
	LrgEditorSelection *selection;
	GPtrArray          *out;
} RootsData;

/* Pre-order walk: a selected node is a root; its subtree is skipped. */
static void
collect_roots (LrgNode   *node,
               RootsData *data)
{
	GPtrArray *children;
	guint      i;

	if (lrg_editor_selection_contains (data->selection, node))
	{
		g_ptr_array_add (data->out, node);
		return;
	}

	children = lrg_node_get_children (node);
	for (i = 0; i < children->len; i++)
		collect_roots (g_ptr_array_index (children, i), data);
}

GPtrArray *
lrg_editor_get_selected_roots (LrgEditor *self)
{
	RootsData  data;
	GPtrArray *top;
	guint      i;

	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	data.selection = self->selection;
	data.out = g_ptr_array_new ();

	top = lrg_node_get_children (lrg_level_get_root (self->level));
	for (i = 0; i < top->len; i++)
		collect_roots (g_ptr_array_index (top, i), &data);

	return data.out;
}

typedef struct
{
	gchar     *needle;   /* casefolded */
	GPtrArray *out;
} FindData;

static gboolean
find_match (LrgNode  *node,
            gpointer  user_data)
{
	FindData         *data = user_data;
	const gchar      *name = lrg_node_get_name (node);
	g_autofree gchar *folded = NULL;

	if (g_strcmp0 (lrg_node_get_guid (node), data->needle) == 0)
	{
		g_ptr_array_add (data->out, node);
		return TRUE;
	}

	if (name == NULL)
		return TRUE;

	folded = g_utf8_casefold (name, -1);
	if (strstr (folded, data->needle) != NULL)
		g_ptr_array_add (data->out, node);

	return TRUE;
}

GPtrArray *
lrg_editor_find_nodes (LrgEditor   *self,
                       const gchar *query)
{
	FindData   data;
	GPtrArray *top;
	guint      i;

	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);
	g_return_val_if_fail (query != NULL, NULL);

	data.needle = g_utf8_casefold (query, -1);
	data.out = g_ptr_array_new ();

	top = lrg_node_get_children (lrg_level_get_root (self->level));
	for (i = 0; i < top->len; i++)
		lrg_node_foreach (g_ptr_array_index (top, i), find_match, &data);

	g_free (data.needle);
	return data.out;
}

LrgNode *
lrg_editor_find_node (LrgEditor   *self,
                      const gchar *ref)
{
	LrgNode *node;

	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	if (ref == NULL || *ref == '\0')
		return NULL;

	node = lrg_level_find_node (self->level, ref);
	if (node != NULL)
		return node;

	return lrg_node_find_by_path (lrg_level_get_root (self->level), ref);
}

/* ==========================================================================
 * Grouping
 * ========================================================================== */

void
lrg_editor_begin_group (LrgEditor   *self,
                        const gchar *label)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	if (self->group_depth++ == 0)
		self->open_group = lrg_editor_command_new_group (label);
}

void
lrg_editor_end_group (LrgEditor *self)
{
	LrgEditorCommand *group;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (self->group_depth > 0);

	if (--self->group_depth > 0)
		return;

	group = g_steal_pointer (&self->open_group);
	if (lrg_editor_command_group_get_n_children (group) == 0)
	{
		g_object_unref (group);
		update_dirty (self);
		return;
	}

	/* Children are already applied; record the group as one step. */
	record_command (self, group);
}

/* ==========================================================================
 * Mutating operations
 * ========================================================================== */

void
lrg_editor_execute (LrgEditor        *self,
                    LrgEditorCommand *command)
{
	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_EDITOR_COMMAND (command));

	execute_command (self, command);
}

void
lrg_editor_add_node (LrgEditor *self,
                     LrgNode   *node,
                     LrgNode   *parent)
{
	lrg_editor_insert_node (self, node, parent, -1);
}

void
lrg_editor_insert_node (LrgEditor *self,
                        LrgNode   *node,
                        LrgNode   *parent,
                        gint       index)
{
	LrgEditorCommand *cmd;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (parent == NULL || LRG_IS_NODE (parent));

	cmd = lrg_editor_command_new_insert_node (self->level, node, parent, index);
	execute_command (self, cmd);
	lrg_editor_selection_set (self->selection, node);
}

gboolean
lrg_editor_delete_node (LrgEditor *self,
                        LrgNode   *node)
{
	LrgNode          *parent;
	LrgEditorCommand *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	parent = lrg_node_get_parent (node);
	if (parent == NULL)
		return FALSE;

	cmd = lrg_editor_command_new_delete_node (self->level, node, parent);
	execute_command (self, cmd);
	return TRUE;
}

guint
lrg_editor_delete_selection (LrgEditor *self)
{
	g_autoptr(GPtrArray) roots = NULL;
	guint                i;
	guint                count = 0;

	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);

	roots = lrg_editor_get_selected_roots (self);
	if (roots->len == 0)
		return 0;

	lrg_editor_begin_group (self, roots->len == 1 ? NULL : "Delete Selection");
	/* Delete back-to-front so recorded sibling indices stay valid on undo. */
	for (i = roots->len; i > 0; i--)
		if (lrg_editor_delete_node (self, g_ptr_array_index (roots, i - 1)))
			count++;
	lrg_editor_end_group (self);

	return count;
}

gboolean
lrg_editor_reparent_node (LrgEditor *self,
                          LrgNode   *node,
                          LrgNode   *new_parent)
{
	LrgNode *old_parent;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (new_parent), FALSE);

	old_parent = lrg_node_get_parent (node);
	if (old_parent == NULL || old_parent == new_parent)
		return FALSE;

	return lrg_editor_move_node (self, node, new_parent, -1, FALSE);
}

gboolean
lrg_editor_move_node (LrgEditor *self,
                      LrgNode   *node,
                      LrgNode   *new_parent,
                      gint       index,
                      gboolean   keep_world)
{
	LrgEditorCommand *cmd;
	LrgNode          *old_parent;
	gfloat            world[16];

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (new_parent == NULL || LRG_IS_NODE (new_parent), FALSE);

	if (new_parent == NULL)
		new_parent = lrg_level_get_root (self->level);

	old_parent = lrg_node_get_parent (node);
	if (old_parent == NULL)
		return FALSE;

	/* No-op moves are not recorded. */
	if (old_parent == new_parent)
	{
		gint cur = lrg_node_get_child_index (old_parent, node);
		gint last = (gint) lrg_node_get_n_children (old_parent) - 1;

		if (index == cur || (index < 0 && cur == last))
			return FALSE;
	}

	if (keep_world)
		lrg_editor_node_get_world_matrix (node, world);

	cmd = lrg_editor_command_new_move (node, new_parent, index);
	if (cmd == NULL)
		return FALSE;

	lrg_editor_begin_group (self, NULL);
	execute_command (self, cmd);

	/* Rewrite the local transform so the world placement is unchanged. */
	if (keep_world && old_parent != new_parent)
	{
		gfloat loc[3], rot[3], scl[3];

		if (lrg_editor_world_to_local (new_parent, world, loc, rot, scl))
			lrg_editor_transform_node (self, node, loc, rot, scl, FALSE);
	}
	lrg_editor_end_group (self);

	return TRUE;
}

/* --------------------------------------------------------------------------
 * Unique sibling names ("Crate" -> "Crate2", "Crate2" -> "Crate3")
 * -------------------------------------------------------------------------- */

static gboolean
sibling_has_name (LrgNode     *parent,
                  const gchar *name)
{
	GPtrArray *children = lrg_node_get_children (parent);
	guint      i;

	for (i = 0; i < children->len; i++)
		if (g_strcmp0 (lrg_node_get_name (g_ptr_array_index (children, i)), name) == 0)
			return TRUE;

	return FALSE;
}

static gchar *
unique_sibling_name (LrgNode     *parent,
                     const gchar *name)
{
	g_autofree gchar *stem = NULL;
	const gchar      *p;
	guint64           n = 2;

	if (name == NULL || *name == '\0')
		name = "Node";

	if (!sibling_has_name (parent, name))
		return g_strdup (name);

	/* Split a trailing number off so the counter continues from it. */
	p = name + strlen (name);
	while (p > name && g_ascii_isdigit (p[-1]))
		p--;
	if (*p != '\0' && p > name)
	{
		n = g_ascii_strtoull (p, NULL, 10) + 1;
		stem = g_strndup (name, p - name);
	}
	else
	{
		stem = g_strdup (name);
	}

	for (;; n++)
	{
		gchar *candidate = g_strdup_printf ("%s%" G_GUINT64_FORMAT, stem, n);

		if (!sibling_has_name (parent, candidate))
			return candidate;
		g_free (candidate);
	}
}

GPtrArray *
lrg_editor_duplicate_selection (LrgEditor *self)
{
	g_autoptr(GPtrArray) roots = NULL;
	GPtrArray           *created;
	guint                i;

	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	created = g_ptr_array_new ();
	roots = lrg_editor_get_selected_roots (self);
	if (roots->len == 0)
		return created;

	lrg_editor_begin_group (self, "Duplicate");
	for (i = 0; i < roots->len; i++)
	{
		LrgNode            *src = g_ptr_array_index (roots, i);
		LrgNode            *parent = lrg_node_get_parent (src);
		g_autoptr(LrgNode)  copy = NULL;
		g_autofree gchar   *name = NULL;
		gint                index;

		if (parent == NULL)
			continue;

		copy = lrg_prefab_clone (src);
		name = unique_sibling_name (parent, lrg_node_get_name (src));
		lrg_node_set_name (copy, name);

		/* Place each copy right after its original. */
		index = lrg_node_get_child_index (parent, src) + 1;
		execute_command (self, lrg_editor_command_new_insert_node (self->level, copy,
		                                                           parent, index));
		g_ptr_array_add (created, copy);
	}
	lrg_editor_end_group (self);

	lrg_editor_select_nodes (self, created, FALSE);
	return created;
}

/* --------------------------------------------------------------------------
 * Clipboard
 * -------------------------------------------------------------------------- */

gchar *
lrg_editor_copy_selection (LrgEditor *self)
{
	g_autoptr(GPtrArray)          roots = NULL;
	g_autoptr(LrgLevel)           tmp = NULL;
	g_autoptr(LrgLevelSerializer) ser = NULL;
	gchar                        *data;
	guint                         i;

	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	roots = lrg_editor_get_selected_roots (self);
	if (roots->len == 0)
		return NULL;

	/* Serialize clones in a scratch level; the document is untouched. */
	tmp = lrg_level_new ("clipboard");
	for (i = 0; i < roots->len; i++)
	{
		g_autoptr(LrgNode) clone = lrg_prefab_clone (g_ptr_array_index (roots, i));

		lrg_level_add_node (tmp, clone, NULL);
	}

	ser = lrg_level_serializer_new ();
	data = lrg_level_serializer_save_to_data (ser, tmp, NULL);
	lrg_editor_set_clipboard (self, data);

	return data;
}

gchar *
lrg_editor_cut_selection (LrgEditor *self)
{
	gchar *data;

	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	data = lrg_editor_copy_selection (self);
	if (data != NULL)
	{
		lrg_editor_begin_group (self, "Cut");
		lrg_editor_delete_selection (self);
		lrg_editor_end_group (self);
	}

	return data;
}

static gboolean
fresh_guid (LrgNode  *node,
            gpointer  user_data)
{
	g_autofree gchar *guid = g_uuid_string_random ();

	lrg_node_set_guid (node, guid);
	return TRUE;
}

GPtrArray *
lrg_editor_paste (LrgEditor    *self,
                  const gchar  *data,
                  LrgNode      *parent,
                  GError      **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(LrgLevel)           tmp = NULL;
	g_autoptr(GPtrArray)          top = NULL;
	GPtrArray                    *created;
	guint                         i;

	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);
	g_return_val_if_fail (parent == NULL || LRG_IS_NODE (parent), NULL);

	if (data == NULL)
		data = self->clipboard;
	if (data == NULL || *data == '\0')
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "The clipboard is empty");
		return NULL;
	}

	ser = lrg_level_serializer_new ();
	tmp = lrg_level_serializer_load_from_data (ser, data, -1, error);
	if (tmp == NULL)
		return NULL;

	if (parent == NULL)
		parent = lrg_level_get_root (self->level);

	/* Detach the parsed nodes from the scratch level first. */
	top = g_ptr_array_copy (lrg_node_get_children (lrg_level_get_root (tmp)), NULL, NULL);
	for (i = 0; i < top->len; i++)
		g_object_ref (g_ptr_array_index (top, i));
	g_ptr_array_set_free_func (top, g_object_unref);

	created = g_ptr_array_new ();
	lrg_editor_begin_group (self, "Paste");
	for (i = 0; i < top->len; i++)
	{
		LrgNode          *node = g_ptr_array_index (top, i);
		g_autofree gchar *name = NULL;

		lrg_level_remove_node (tmp, node);
		lrg_node_foreach (node, fresh_guid, NULL);
		name = unique_sibling_name (parent, lrg_node_get_name (node));
		lrg_node_set_name (node, name);

		execute_command (self, lrg_editor_command_new_insert_node (self->level, node,
		                                                           parent, -1));
		g_ptr_array_add (created, node);
	}
	lrg_editor_end_group (self);

	lrg_editor_select_nodes (self, created, FALSE);
	return created;
}

const gchar *
lrg_editor_get_clipboard (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	return self->clipboard;
}

void
lrg_editor_set_clipboard (LrgEditor   *self,
                          const gchar *data)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	g_free (self->clipboard);
	self->clipboard = g_strdup (data);
}

/* --------------------------------------------------------------------------
 * Properties and transforms
 * -------------------------------------------------------------------------- */

void
lrg_editor_rename_node (LrgEditor   *self,
                        LrgNode     *node,
                        const gchar *name)
{
	GValue value = G_VALUE_INIT;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));

	if (g_strcmp0 (lrg_node_get_name (node), name) == 0)
		return;

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, name);
	lrg_editor_set_object_property (self, G_OBJECT (node), "name", &value, FALSE);
	g_value_unset (&value);
}

void
lrg_editor_set_node_transform (LrgEditor    *self,
                               LrgNode      *node,
                               const gfloat *location,
                               const gfloat *rotation,
                               const gfloat *scale)
{
	g_return_if_fail (location != NULL && rotation != NULL && scale != NULL);

	lrg_editor_transform_node (self, node, location, rotation, scale, FALSE);
}

void
lrg_editor_transform_node (LrgEditor    *self,
                           LrgNode      *node,
                           const gfloat *location,
                           const gfloat *rotation,
                           const gfloat *scale,
                           gboolean      merge)
{
	LrgEditorCommand *cmd;
	GrlVector3       *l, *r, *s;
	gfloat            before[9];
	gfloat            after[9];

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));

	l = lrg_node_get_location (node);
	r = lrg_node_get_rotation (node);
	s = lrg_node_get_scale (node);

	before[0] = l->x; before[1] = l->y; before[2] = l->z;
	before[3] = r->x; before[4] = r->y; before[5] = r->z;
	before[6] = s->x; before[7] = s->y; before[8] = s->z;

	memcpy (after, before, sizeof after);
	if (location != NULL)
		memcpy (after + 0, location, 3 * sizeof (gfloat));
	if (rotation != NULL)
		memcpy (after + 3, rotation, 3 * sizeof (gfloat));
	if (scale != NULL)
		memcpy (after + 6, scale, 3 * sizeof (gfloat));

	if (memcmp (before, after, sizeof before) == 0)
		return;

	cmd = lrg_editor_command_new_transform (node, before, after);
	if (merge && try_merge (self, cmd))
		return;

	execute_command (self, cmd);
}

void
lrg_editor_set_node_property (LrgEditor    *self,
                              LrgNode      *node,
                              const gchar  *prop_name,
                              const GValue *value)
{
	g_return_if_fail (LRG_IS_NODE (node));

	lrg_editor_set_object_property (self, G_OBJECT (node), prop_name, value, FALSE);
}

gboolean
lrg_editor_set_object_property (LrgEditor    *self,
                                GObject      *object,
                                const gchar  *prop_name,
                                const GValue *value,
                                gboolean      merge)
{
	GParamSpec       *pspec;
	GValue            before = G_VALUE_INIT;
	GValue            after = G_VALUE_INIT;
	LrgEditorCommand *cmd;
	gboolean          same;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
	g_return_val_if_fail (prop_name != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), prop_name);
	if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE) ||
	    (pspec->flags & G_PARAM_CONSTRUCT_ONLY))
		return FALSE;

	/* Coerce the incoming value (e.g. a double from the IPC layer for a
	 * float property). */
	g_value_init (&after, pspec->value_type);
	if (!g_value_transform (value, &after))
	{
		g_value_unset (&after);
		return FALSE;
	}
	g_param_value_validate (pspec, &after);

	g_value_init (&before, pspec->value_type);
	g_object_get_property (object, prop_name, &before);

	same = g_param_values_cmp (pspec, &before, &after) == 0;
	if (!same)
	{
		cmd = lrg_editor_command_new_set_property (object, prop_name, &before, &after);
		if (!(merge && try_merge (self, cmd)))
			execute_command (self, cmd);
	}

	g_value_unset (&before);
	g_value_unset (&after);
	return TRUE;
}

void
lrg_editor_set_visual_param (LrgEditor   *self,
                             LrgNode     *node,
                             const gchar *param,
                             gdouble      value,
                             gboolean     merge)
{
	LrgNodeVisual    *visual;
	gdouble           before;
	LrgEditorCommand *cmd;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (param != NULL);

	visual = lrg_node_get_visual (node);
	if (visual == NULL)
		return;

	before = lrg_node_visual_get_param_double (visual, param, 0.0);
	cmd = lrg_editor_command_new_set_visual_param (node, param, before, value);

	/* A continuing slider drag coalesces onto the top undo command so
	 * the whole drag is one undo step. */
	if (merge && try_merge (self, cmd))
		return;

	execute_command (self, cmd);
}

/* --------------------------------------------------------------------------
 * Snapshot edits (visual, components, scripts, anything else)
 * -------------------------------------------------------------------------- */

LrgNode *
lrg_editor_snapshot_node (LrgNode *node)
{
	LrgNode *snap;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	snap = lrg_node_new (NULL);
	lrg_node_copy_state (snap, node);
	return snap;
}

gboolean
lrg_editor_commit_node_state (LrgEditor   *self,
                              LrgNode     *node,
                              LrgNode     *before,
                              const gchar *label,
                              gboolean     merge)
{
	LrgEditorCommand *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (before), FALSE);

	if (lrg_node_state_equal (node, before))
		return FALSE;

	/* The node already holds the "after" state; applying it again is a
	 * no-op, so the normal execute path can be used. */
	cmd = lrg_editor_command_new_node_state (node, before, node, label);
	if (!(merge && try_merge (self, cmd)))
		execute_command (self, cmd);

	return TRUE;
}

void
lrg_editor_set_visual (LrgEditor     *self,
                       LrgNode       *node,
                       LrgNodeVisual *visual)
{
	g_autoptr(LrgNode)       before = NULL;
	g_autoptr(LrgNodeVisual) copy = NULL;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (visual == NULL || LRG_IS_NODE_VISUAL (visual));

	before = lrg_editor_snapshot_node (node);
	if (visual != NULL)
		copy = lrg_node_visual_copy (visual);
	lrg_node_set_visual (node, copy);
	lrg_editor_commit_node_state (self, node, before, "Change Visual", FALSE);
}

void
lrg_editor_add_component (LrgEditor        *self,
                          LrgNode          *node,
                          LrgComponentDesc *desc)
{
	g_autoptr(LrgNode)          before = NULL;
	g_autoptr(LrgComponentDesc) copy = NULL;
	g_autofree gchar           *label = NULL;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (LRG_IS_COMPONENT_DESC (desc));

	before = lrg_editor_snapshot_node (node);
	copy = lrg_component_desc_copy (desc);
	lrg_node_add_component (node, copy);
	label = g_strdup_printf ("Add Component %s",
	                         lrg_component_desc_get_type_name (desc) != NULL
	                         ? lrg_component_desc_get_type_name (desc) : "");
	lrg_editor_commit_node_state (self, node, before, label, FALSE);
}

gboolean
lrg_editor_remove_component (LrgEditor *self,
                             LrgNode   *node,
                             guint      index)
{
	g_autoptr(LrgNode) before = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	before = lrg_editor_snapshot_node (node);
	if (!lrg_node_remove_component_at (node, index))
		return FALSE;

	return lrg_editor_commit_node_state (self, node, before, "Remove Component", FALSE);
}

gboolean
lrg_editor_set_component_value (LrgEditor    *self,
                                LrgNode      *node,
                                guint         index,
                                const gchar  *key,
                                const GValue *value,
                                gboolean      merge)
{
	g_autoptr(LrgNode) before = NULL;
	g_autofree gchar  *label = NULL;
	GPtrArray         *components;
	LrgComponentDesc  *desc;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	components = lrg_node_get_components (node);
	if (index >= components->len)
		return FALSE;

	before = lrg_editor_snapshot_node (node);
	desc = g_ptr_array_index (components, index);
	if (value != NULL)
		lrg_component_desc_set_value (desc, key, value);
	else
		lrg_component_desc_remove (desc, key);

	/* The label carries index and key so drags of one field merge but a
	 * drag of another field starts a new step. */
	label = g_strdup_printf ("Set %s.%s",
	                         lrg_component_desc_get_type_name (desc) != NULL
	                         ? lrg_component_desc_get_type_name (desc) : "component",
	                         key);
	return lrg_editor_commit_node_state (self, node, before, label, merge);
}

void
lrg_editor_add_script (LrgEditor        *self,
                       LrgNode          *node,
                       LrgScriptBinding *binding)
{
	g_autoptr(LrgNode)          before = NULL;
	g_autoptr(LrgScriptBinding) copy = NULL;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (LRG_IS_SCRIPT_BINDING (binding));

	before = lrg_editor_snapshot_node (node);
	copy = lrg_script_binding_copy (binding);
	lrg_node_add_script (node, copy);
	lrg_editor_commit_node_state (self, node, before, "Attach Script", FALSE);
}

gboolean
lrg_editor_remove_script (LrgEditor *self,
                          LrgNode   *node,
                          guint      index)
{
	g_autoptr(LrgNode) before = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	before = lrg_editor_snapshot_node (node);
	if (!lrg_node_remove_script_at (node, index))
		return FALSE;

	return lrg_editor_commit_node_state (self, node, before, "Detach Script", FALSE);
}

LrgNode *
lrg_editor_instantiate_prefab (LrgEditor    *self,
                               const gchar  *path,
                               LrgNode      *parent,
                               GError      **error)
{
	g_autoptr(LrgNode) prefab = NULL;
	g_autoptr(LrgNode) instance = NULL;
	LrgNodeVisual     *visual;

	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	prefab = lrg_prefab_load (path, error);
	if (prefab == NULL)
		return NULL;

	instance = lrg_prefab_instantiate (prefab);

	/* Record where the instance came from so tools can find the source. */
	visual = lrg_node_get_visual (instance);
	if (visual == NULL)
	{
		g_autoptr(LrgNodeVisual) v = lrg_node_visual_new (LRG_NODE_VISUAL_PREFAB_INSTANCE);

		lrg_node_visual_set_asset (v, path);
		lrg_node_set_visual (instance, v);
	}
	else
	{
		GValue src = G_VALUE_INIT;

		g_value_init (&src, G_TYPE_STRING);
		g_value_set_string (&src, path);
		lrg_node_visual_set_param (visual, "prefab:source", &src);
		g_value_unset (&src);
	}

	if (parent == NULL)
		parent = lrg_level_get_root (self->level);

	{
		g_autofree gchar *name = unique_sibling_name (parent, lrg_node_get_name (instance));

		lrg_node_set_name (instance, name);
	}

	lrg_editor_insert_node (self, instance, parent, -1);
	return instance;
}

/* ==========================================================================
 * Undo / redo
 * ========================================================================== */

gboolean
lrg_editor_can_undo (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	return self->undo_stack->len > 0;
}

gboolean
lrg_editor_can_redo (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	return self->redo_stack->len > 0;
}

/* Moves one command between stacks without emitting. */
static gboolean
undo_one (LrgEditor *self)
{
	LrgEditorCommand *cmd;

	if (self->undo_stack->len == 0)
		return FALSE;

	cmd = g_ptr_array_steal_index (self->undo_stack, self->undo_stack->len - 1);
	lrg_editor_command_undo (cmd);
	g_ptr_array_add (self->redo_stack, cmd);  /* transfer the ref */
	return TRUE;
}

static gboolean
redo_one (LrgEditor *self)
{
	LrgEditorCommand *cmd;

	if (self->redo_stack->len == 0)
		return FALSE;

	cmd = g_ptr_array_steal_index (self->redo_stack, self->redo_stack->len - 1);
	lrg_editor_command_apply (cmd);
	g_ptr_array_add (self->undo_stack, cmd);  /* transfer the ref */
	return TRUE;
}

void
lrg_editor_undo (LrgEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (self->open_group == NULL);

	if (undo_one (self))
		emit_changed (self);
}

void
lrg_editor_redo (LrgEditor *self)
{
	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (self->open_group == NULL);

	if (redo_one (self))
		emit_changed (self);
}

guint
lrg_editor_get_undo_count (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);

	return self->undo_stack->len;
}

guint
lrg_editor_get_redo_count (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);

	return self->redo_stack->len;
}

LrgEditorCommand *
lrg_editor_get_undo_command (LrgEditor *self,
                             guint      index)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	if (index >= self->undo_stack->len)
		return NULL;

	return g_ptr_array_index (self->undo_stack, index);
}

LrgEditorCommand *
lrg_editor_get_redo_command (LrgEditor *self,
                             guint      index)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), NULL);

	if (index >= self->redo_stack->len)
		return NULL;

	/* The redo stack's top (last element) is the next redo. */
	return g_ptr_array_index (self->redo_stack, self->redo_stack->len - 1 - index);
}

void
lrg_editor_jump_to_history (LrgEditor *self,
                            guint      undo_count)
{
	gboolean moved = FALSE;

	g_return_if_fail (LRG_IS_EDITOR (self));
	g_return_if_fail (self->open_group == NULL);

	while (self->undo_stack->len > undo_count && undo_one (self))
		moved = TRUE;
	while (self->undo_stack->len < undo_count && redo_one (self))
		moved = TRUE;

	if (moved)
		emit_changed (self);
}

void
lrg_editor_clear_history (LrgEditor *self)
{
	gboolean was_dirty;

	g_return_if_fail (LRG_IS_EDITOR (self));

	was_dirty = lrg_editor_is_dirty (self);
	g_ptr_array_set_size (self->undo_stack, 0);
	g_ptr_array_set_size (self->redo_stack, 0);

	/* With no history the current state is serial 0; keep the dirty flag
	 * meaningful by moving the clean marker accordingly. */
	self->clean_serial = was_dirty ? G_MAXUINT64 : 0;
	emit_changed (self);
}

void
lrg_editor_set_history_limit (LrgEditor *self,
                              guint      limit)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	if (self->history_limit == limit)
		return;

	self->history_limit = limit;
	enforce_history_limit (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HISTORY_LIMIT]);
}

guint
lrg_editor_get_history_limit (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);

	return self->history_limit;
}

/* ==========================================================================
 * Tool / snap / space
 * ========================================================================== */

LrgEditorTool
lrg_editor_get_tool (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), LRG_EDITOR_TOOL_SELECT);

	return self->tool;
}

void
lrg_editor_set_tool (LrgEditor     *self,
                     LrgEditorTool  tool)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	if (self->tool == tool)
		return;

	self->tool = tool;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOOL]);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

void
lrg_editor_set_snap (LrgEditor *self,
                     gdouble    translate,
                     gdouble    rotate,
                     gdouble    scale)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	self->snap_translate = MAX (translate, 0.0);
	self->snap_rotate = MAX (rotate, 0.0);
	self->snap_scale = MAX (scale, 0.0);
}

void
lrg_editor_get_snap (LrgEditor *self,
                     gdouble   *translate,
                     gdouble   *rotate,
                     gdouble   *scale)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	if (translate != NULL)
		*translate = self->snap_translate;
	if (rotate != NULL)
		*rotate = self->snap_rotate;
	if (scale != NULL)
		*scale = self->snap_scale;
}

gboolean
lrg_editor_get_snap_enabled (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	return self->snap_enabled;
}

void
lrg_editor_set_snap_enabled (LrgEditor *self,
                             gboolean   enabled)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	enabled = !!enabled;
	if (self->snap_enabled == enabled)
		return;

	self->snap_enabled = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAP_ENABLED]);
}

LrgEditorSpace
lrg_editor_get_space (LrgEditor *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR (self), LRG_EDITOR_SPACE_WORLD);

	return self->space;
}

void
lrg_editor_set_space (LrgEditor      *self,
                      LrgEditorSpace  space)
{
	g_return_if_fail (LRG_IS_EDITOR (self));

	if (self->space == space)
		return;

	self->space = space;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPACE]);
}
