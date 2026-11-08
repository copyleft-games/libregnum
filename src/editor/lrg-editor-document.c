/* lrg-editor-document.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDocument: the headless model behind the editor's data editors.
 *
 * Design notes
 * ------------
 * One final type with a format enum, not a subclass per format: the eight
 * formats differ only in their validator, their skeleton and a handful of
 * read helpers, while everything the UI and IPC drive (paths, undo, save,
 * reload) is identical.  A single type keeps `data.*` IPC and the base
 * data-editor panel generic, and the per-format code is a switch over
 * static functions at the bottom of this file.
 *
 * The tree is a yaml-glib YamlNode.  Edits never mutate a node in place;
 * they replace, insert or remove whole child nodes of a container, so a
 * command can keep references to the exact node objects it swapped and
 * undo/redo is a matter of swapping them back in strict stack order.
 * yaml-glib has no insert-at-index for sequences or mappings, so inserts
 * detach the tail, append, and re-append the tail: the container node
 * itself (and its comments) is never replaced.
 *
 * Line numbers come from a libyaml event walk over the serialized text,
 * which yaml-glib does not keep; the same walk finds duplicate keys in
 * the source file (yaml-glib silently keeps the last one).
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <math.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <yaml.h>
#include <yaml-glib.h>
#include <graylib.h>

#include "lrg-editor-document.h"
#include "../lrg-enums.h"
#include "../lrg-log.h"
#include "../input/lrg-input-map.h"
#include "../input/lrg-input-action.h"
#include "../input/lrg-input-binding.h"
#include "../i18n/lrg-locale.h"
#include "../audio/lrg-sound-bank.h"
#include "../hud/lrg-hud-layout.h"
#include "../ui/lrg-widget.h"
#include "../ui/lrg-ui-loader.h"
#include "../ui/lrg-theme.h"
#include "../dialog/lrg-dialog-tree.h"
#include "../procgen/lrg-procgen-dungeon.h"
#include "../ai/lrg-bt-asset.h"
#include "../ai/lrg-logic-machine.h"
#include "../particles/lrg-particle-asset.h"
#include "../weather/lrg-weather-asset.h"
#include "../audio/lrg-mixer-asset.h"
#include "../tilemap/lrg-tileset.h"
#include "../atlas/lrg-sprite-sheet.h"
#include "../atlas/lrg-sprite-animation.h"
#include "../graphics/lrg-water-body.h"
#include "../pathfinding/lrg-nav-bake.h"
#include "../scripting/lrg-vscript-program.h"
#include "../audio/lrg-foley-patch.h"
#include "../music/lrg-song.h"

#define DEFAULT_UNDO_LIMIT (256)

/* ==========================================================================
 * Enum, error and boxed types
 * ========================================================================== */

GType
lrg_editor_document_format_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN, "LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN", "unknown" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP, "LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP", "input-map" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_LOCALE, "LRG_EDITOR_DOCUMENT_FORMAT_LOCALE", "locale" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK, "LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK", "sound-bank" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT, "LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT", "hud-layout" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT, "LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT", "ui-layout" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE, "LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE", "dialog-tree" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON, "LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON", "dungeon" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_THEME, "LRG_EDITOR_DOCUMENT_FORMAT_THEME", "theme" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL, "LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL", "material" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK, "LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK", "post-stack" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH, "LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH", "shader-graph" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH, "LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH", "anim-graph" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP, "LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP", "bone-map" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE, "LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE", "sequence" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D, "LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D", "skeleton2d" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET, "LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET", "physics-asset" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_BT, "LRG_EDITOR_DOCUMENT_FORMAT_BT", "bt" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_FSM, "LRG_EDITOR_DOCUMENT_FORMAT_FSM", "fsm" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES, "LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES", "particles" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_WEATHER, "LRG_EDITOR_DOCUMENT_FORMAT_WEATHER", "weather" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_MIXER, "LRG_EDITOR_DOCUMENT_FORMAT_MIXER", "mixer" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_TILESET, "LRG_EDITOR_DOCUMENT_FORMAT_TILESET", "tileset" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_SPRITE, "LRG_EDITOR_DOCUMENT_FORMAT_SPRITE", "sprite" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_WATER, "LRG_EDITOR_DOCUMENT_FORMAT_WATER", "water" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS, "LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS", "nav-settings" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT, "LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT", "vscript" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_FOLEY, "LRG_EDITOR_DOCUMENT_FORMAT_FOLEY", "foley" },
			{ LRG_EDITOR_DOCUMENT_FORMAT_SONG, "LRG_EDITOR_DOCUMENT_FORMAT_SONG", "song" },
			{ 0, NULL, NULL }
		};
		GType id;

		id = g_enum_register_static (g_intern_static_string ("LrgEditorDocumentFormat"), values);
		g_once_init_leave (&type_id, id);
	}

	return (GType) type_id;
}

const gchar *
lrg_editor_document_format_to_string (LrgEditorDocumentFormat format)
{
	GEnumClass *klass;
	GEnumValue *value;

	klass = (GEnumClass *) g_type_class_ref (LRG_TYPE_EDITOR_DOCUMENT_FORMAT);
	value = g_enum_get_value (klass, (gint) format);
	g_type_class_unref (klass);

	/* nicks are static strings, so they outlive the class reference */
	return value != NULL ? value->value_nick : "unknown";
}

LrgEditorDocumentFormat
lrg_editor_document_format_from_string (const gchar *name)
{
	GEnumClass *klass;
	GEnumValue *value;
	LrgEditorDocumentFormat format;

	if (name == NULL)
		return LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN;

	klass = (GEnumClass *) g_type_class_ref (LRG_TYPE_EDITOR_DOCUMENT_FORMAT);
	value = g_enum_get_value_by_nick (klass, name);
	if (value == NULL)
		value = g_enum_get_value_by_name (klass, name);
	format = value != NULL ? (LrgEditorDocumentFormat) value->value : LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN;
	g_type_class_unref (klass);

	return format;
}

/*
 * The engine extension of each format.  The table is also what
 * lrg_editor_document_detect_format() matches first, so a format and its
 * extension are declared in one place.
 */
static const struct
{
	LrgEditorDocumentFormat  format;
	const gchar             *extension;
} format_extensions[] = {
	{ LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL, ".rmat" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK, ".rpost" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH, ".rshadergraph" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH, ".ranim" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP, ".rbonemap" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE, ".rseq" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D, ".rskel2d" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET, ".rphysasset" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_BT, ".rbt" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_FSM, ".rfsm" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES, ".rparticles" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_WEATHER, ".rweather" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_MIXER, ".rmixer" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_TILESET, ".rtileset" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_SPRITE, ".rsprite" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_WATER, ".rwater" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS, ".rnav" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT, ".rvscript" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_FOLEY, ".rfoley" },
	{ LRG_EDITOR_DOCUMENT_FORMAT_SONG, ".rsong" }
};

const gchar *
lrg_editor_document_format_get_extension (LrgEditorDocumentFormat format)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (format_extensions); i++)
	{
		if (format_extensions[i].format == format)
			return format_extensions[i].extension;
	}

	return NULL;
}

GType
lrg_editor_document_format_get_object_type (LrgEditorDocumentFormat format)
{
	switch (format)
	{
	case LRG_EDITOR_DOCUMENT_FORMAT_BT:
		return LRG_TYPE_BT_ASSET;
	case LRG_EDITOR_DOCUMENT_FORMAT_FSM:
		return LRG_TYPE_LOGIC_MACHINE;
	case LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES:
		return LRG_TYPE_PARTICLE_ASSET;
	case LRG_EDITOR_DOCUMENT_FORMAT_WEATHER:
		return LRG_TYPE_WEATHER_ASSET;
	case LRG_EDITOR_DOCUMENT_FORMAT_MIXER:
		return LRG_TYPE_MIXER_ASSET;
	case LRG_EDITOR_DOCUMENT_FORMAT_TILESET:
		return LRG_TYPE_TILESET;
	case LRG_EDITOR_DOCUMENT_FORMAT_SPRITE:
		return LRG_TYPE_SPRITE_SHEET;
	case LRG_EDITOR_DOCUMENT_FORMAT_WATER:
		return LRG_TYPE_WATER_BODY;
	case LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS:
		return LRG_TYPE_NAV_BAKE;
	case LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT:
		return LRG_TYPE_VSCRIPT_PROGRAM;
	case LRG_EDITOR_DOCUMENT_FORMAT_FOLEY:
		return LRG_TYPE_FOLEY_PATCH;
	case LRG_EDITOR_DOCUMENT_FORMAT_SONG:
		return LRG_TYPE_SONG;
	default:
		return G_TYPE_INVALID;
	}
}

GType
lrg_editor_document_node_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_DOCUMENT_NODE_MISSING, "LRG_EDITOR_DOCUMENT_NODE_MISSING", "missing" },
			{ LRG_EDITOR_DOCUMENT_NODE_NULL, "LRG_EDITOR_DOCUMENT_NODE_NULL", "null" },
			{ LRG_EDITOR_DOCUMENT_NODE_SCALAR, "LRG_EDITOR_DOCUMENT_NODE_SCALAR", "scalar" },
			{ LRG_EDITOR_DOCUMENT_NODE_MAPPING, "LRG_EDITOR_DOCUMENT_NODE_MAPPING", "mapping" },
			{ LRG_EDITOR_DOCUMENT_NODE_SEQUENCE, "LRG_EDITOR_DOCUMENT_NODE_SEQUENCE", "sequence" },
			{ 0, NULL, NULL }
		};
		GType id;

		id = g_enum_register_static (g_intern_static_string ("LrgEditorDocumentNodeKind"), values);
		g_once_init_leave (&type_id, id);
	}

	return (GType) type_id;
}

G_DEFINE_QUARK (lrg-editor-document-error-quark, lrg_editor_document_error)

GType
lrg_editor_document_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_DOCUMENT_ERROR_PARSE, "LRG_EDITOR_DOCUMENT_ERROR_PARSE", "parse" },
			{ LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH, "LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH", "invalid-path" },
			{ LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND, "LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND", "not-found" },
			{ LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND, "LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND", "wrong-kind" },
			{ LRG_EDITOR_DOCUMENT_ERROR_EXISTS, "LRG_EDITOR_DOCUMENT_ERROR_EXISTS", "exists" },
			{ LRG_EDITOR_DOCUMENT_ERROR_READ_ONLY, "LRG_EDITOR_DOCUMENT_ERROR_READ_ONLY", "read-only" },
			{ LRG_EDITOR_DOCUMENT_ERROR_NO_PATH, "LRG_EDITOR_DOCUMENT_ERROR_NO_PATH", "no-path" },
			{ LRG_EDITOR_DOCUMENT_ERROR_GROUP_OPEN, "LRG_EDITOR_DOCUMENT_ERROR_GROUP_OPEN", "group-open" },
			{ LRG_EDITOR_DOCUMENT_ERROR_IO, "LRG_EDITOR_DOCUMENT_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType id;

		id = g_enum_register_static (g_intern_static_string ("LrgEditorDocumentError"), values);
		g_once_init_leave (&type_id, id);
	}

	return (GType) type_id;
}

G_DEFINE_BOXED_TYPE (LrgEditorDocumentIssue, lrg_editor_document_issue,
                     lrg_editor_document_issue_copy, lrg_editor_document_issue_free)

LrgEditorDocumentIssue *
lrg_editor_document_issue_new (LrgEditorProblemSeverity  severity,
                               const gchar              *path,
                               guint                     line,
                               guint                     column,
                               const gchar              *message)
{
	LrgEditorDocumentIssue *issue;

	issue = g_new0 (LrgEditorDocumentIssue, 1);
	issue->severity = severity;
	issue->path = g_strdup (path);
	issue->line = line;
	issue->column = column;
	issue->message = g_strdup (message != NULL ? message : "");

	return issue;
}

LrgEditorDocumentIssue *
lrg_editor_document_issue_copy (const LrgEditorDocumentIssue *issue)
{
	g_return_val_if_fail (issue != NULL, NULL);

	return lrg_editor_document_issue_new (issue->severity, issue->path, issue->line,
	                                      issue->column, issue->message);
}

void
lrg_editor_document_issue_free (LrgEditorDocumentIssue *issue)
{
	if (issue == NULL)
		return;

	g_free (issue->path);
	g_free (issue->message);
	g_free (issue);
}

G_DEFINE_BOXED_TYPE (LrgEditorDocumentEdge, lrg_editor_document_edge,
                     lrg_editor_document_edge_copy, lrg_editor_document_edge_free)

/* builds an edge; @path may be NULL for an implicit chain edge */
static LrgEditorDocumentEdge *
edge_new (const gchar *from,
          const gchar *to,
          gint         response,
          const gchar *path)
{
	LrgEditorDocumentEdge *edge;

	edge = g_new0 (LrgEditorDocumentEdge, 1);
	edge->from = g_strdup (from);
	edge->to = g_strdup (to);
	edge->response = response;
	edge->path = g_strdup (path);

	return edge;
}

LrgEditorDocumentEdge *
lrg_editor_document_edge_copy (const LrgEditorDocumentEdge *edge)
{
	g_return_val_if_fail (edge != NULL, NULL);

	return edge_new (edge->from, edge->to, edge->response, edge->path);
}

void
lrg_editor_document_edge_free (LrgEditorDocumentEdge *edge)
{
	if (edge == NULL)
		return;

	g_free (edge->from);
	g_free (edge->to);
	g_free (edge->path);
	g_free (edge);
}

/* ==========================================================================
 * Commands
 *
 * Every edit is a Cmd.  Commands never copy the tree: they hold
 * references to the node objects they put in and took out, and a
 * container path that is resolved again at undo/redo time (it resolves to
 * the same container because undo and redo run in strict stack order).
 * ========================================================================== */

typedef enum
{
	CMD_REPLACE,   /* swap the node at a key/index (or the root) */
	CMD_INSERT,    /* insert new_node at a key position / sequence index */
	CMD_REMOVE,    /* remove old_node from a key position / sequence index */
	CMD_MOVE,      /* move a sequence item from index to index2 */
	CMD_RENAME,    /* rename key to key2 at the same position */
	CMD_GROUP      /* children, applied in order */
} CmdKind;

typedef struct _Cmd Cmd;

struct _Cmd
{
	CmdKind    kind;
	gchar     *label;
	guint64    serial;     /* identifies the document state after this step */

	gboolean   at_root;    /* CMD_REPLACE of the root node */
	gchar     *container;  /* path of the mapping or sequence edited */
	gboolean   in_map;     /* container is a mapping (else a sequence) */
	gchar     *key;        /* mapping key (CMD_RENAME: the old key) */
	gchar     *key2;       /* CMD_RENAME: the new key */
	guint      index;      /* sequence index or mapping position */
	guint      index2;     /* CMD_MOVE: destination index */
	YamlNode  *old_node;   /* CMD_REPLACE: before; CMD_REMOVE: the removed node */
	YamlNode  *new_node;   /* CMD_REPLACE: after; CMD_INSERT: the inserted node */

	gchar     *target;     /* full path of the edited node (merge key) */
	guint      gesture;    /* merge id, 0 = never merge */

	GPtrArray *children;   /* CMD_GROUP: Cmd* */
};

static void
cmd_free (gpointer data)
{
	Cmd *cmd = (Cmd *) data;

	if (cmd == NULL)
		return;

	g_free (cmd->label);
	g_free (cmd->container);
	g_free (cmd->key);
	g_free (cmd->key2);
	g_free (cmd->target);
	g_clear_pointer (&cmd->old_node, yaml_node_unref);
	g_clear_pointer (&cmd->new_node, yaml_node_unref);
	g_clear_pointer (&cmd->children, g_ptr_array_unref);
	g_free (cmd);
}

static Cmd *
cmd_new (CmdKind      kind,
         const gchar *label)
{
	Cmd *cmd;

	cmd = g_new0 (Cmd, 1);
	cmd->kind = kind;
	cmd->label = g_strdup (label);
	if (kind == CMD_GROUP)
		cmd->children = g_ptr_array_new_with_free_func (cmd_free);

	return cmd;
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

/* one entry of the line index: where a path starts in the serialized text */
typedef struct
{
	gchar *path;
	guint  line;
	guint  column;
} LineEntry;

static void
line_entry_free (gpointer data)
{
	LineEntry *entry = (LineEntry *) data;

	g_free (entry->path);
	g_free (entry);
}

struct _LrgEditorDocument
{
	GObject                  parent_instance;

	YamlNode                *root;
	LrgEditorDocumentFormat  format;
	gchar                   *path;
	gchar                   *display_name;
	gboolean                 read_only;
	gchar                   *base_dir;
	LrgEditorDocument       *reference;

	/* undo */
	GPtrArray               *undo_stack;     /* Cmd*, oldest first */
	GPtrArray               *redo_stack;     /* Cmd*, next redo last */
	GPtrArray               *groups;         /* open CMD_GROUPs, outermost first */
	Cmd                     *merge_candidate;
	guint                    undo_limit;
	guint64                  next_serial;
	guint64                  base_serial;    /* state of an empty undo stack */
	guint64                  clean_serial;   /* state last loaded or saved */
	gboolean                 dirty;
	gboolean                 notified_can_undo;
	gboolean                 notified_can_redo;

	/* disk */
	gboolean                 monitor_file;
	GFileMonitor            *monitor;
	gint64                   disk_mtime;
	gchar                   *disk_text;
	gboolean                 external_changed;

	/* caches, dropped on every change */
	gchar                   *text_cache;
	gsize                    text_length;
	gchar                   *file_cache;     /* engine-backed: the engine writer's text */
	gsize                    file_length;
	GPtrArray               *line_index;     /* LineEntry*, document order */

	/* duplicate keys found in the source text when it was read */
	GPtrArray               *load_issues;
};

G_DEFINE_FINAL_TYPE (LrgEditorDocument, lrg_editor_document, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_FORMAT,
	PROP_PATH,
	PROP_DIRTY,
	PROP_READ_ONLY,
	PROP_CAN_UNDO,
	PROP_CAN_REDO,
	PROP_UNDO_LIMIT,
	PROP_BASE_DIR,
	PROP_REFERENCE,
	PROP_MONITOR_FILE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_CHANGED,
	SIGNAL_DIRTY_CHANGED,
	SIGNAL_SAVED,
	SIGNAL_RELOADED,
	SIGNAL_FILE_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

/* drops the serialized text and line index; called after every change */
static void
invalidate_caches (LrgEditorDocument *self)
{
	g_clear_pointer (&self->text_cache, g_free);
	self->text_length = 0;
	g_clear_pointer (&self->file_cache, g_free);
	self->file_length = 0;
	g_clear_pointer (&self->line_index, g_ptr_array_unref);
}

/*
 * Serializes @node with the comment-preserving block writer.  yaml-glib's
 * libyaml emitter would drop every comment, so emit-comments is always on.
 */
static gchar *
serialize_node (YamlNode *node,
                gsize    *length)
{
	g_autoptr(YamlGenerator) generator = NULL;
	g_autoptr(GError) error = NULL;
	gchar *text;
	gsize len;

	generator = yaml_generator_new ();
	yaml_generator_set_emit_comments (generator, TRUE);
	yaml_generator_set_root (generator, node);
	len = 0;
	text = yaml_generator_to_data (generator, &len, &error);
	if (text == NULL)
	{
		g_warning ("LrgEditorDocument: could not serialize: %s",
		           error != NULL ? error->message : "unknown error");
		text = g_strdup ("");
		len = 0;
	}
	if (length != NULL)
		*length = len;

	return text;
}

/* the cached full-document text (owned by @self) */
static const gchar *
document_text (LrgEditorDocument *self,
               gsize             *length)
{
	if (self->text_cache == NULL)
		self->text_cache = serialize_node (self->root, &self->text_length);
	if (length != NULL)
		*length = self->text_length;

	return self->text_cache;
}

/*
 * Parses YAML text into a node with comments captured.  An empty document
 * becomes a null node.  Errors carry the parser's line and column.
 */
static YamlNode *
parse_text (const gchar  *text,
            gssize        length,
            GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError) local = NULL;
	YamlNode *root;

	parser = yaml_parser_new ();
	yaml_parser_set_capture_comments (parser, TRUE);
	if (!yaml_parser_load_from_data (parser, text, length, &local))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_PARSE,
		             "%s", local != NULL ? local->message : "invalid YAML");
		return NULL;
	}

	root = yaml_parser_dup_root (parser);
	if (root == NULL)
		root = yaml_node_new_null ();

	return root;
}

/* copies the comments of the node being replaced onto its replacement */
static void
carry_comments (YamlNode *from,
                YamlNode *to)
{
	GPtrArray *leading;
	const gchar *trailing;

	if (from == NULL || to == NULL)
		return;

	leading = yaml_node_get_leading_comments (from);
	if (leading != NULL && yaml_node_get_leading_comments (to) == NULL)
		yaml_node_set_leading_comments (to, leading);
	trailing = yaml_node_get_trailing_comment (from);
	if (trailing != NULL && yaml_node_get_trailing_comment (to) == NULL)
		yaml_node_set_trailing_comment (to, trailing);
	if (yaml_node_get_blank_before (from))
		yaml_node_set_blank_before (to, TRUE);
}

/* strict decimal sequence index: no sign, no leading zeros */
static gboolean
parse_index (const gchar *token,
             guint       *out)
{
	guint64 value;
	const gchar *p;

	if (token == NULL || *token == '\0')
		return FALSE;
	if (token[0] == '0' && token[1] != '\0')
		return FALSE;
	for (p = token; *p != '\0'; p++)
	{
		if (!g_ascii_isdigit (*p))
			return FALSE;
	}
	value = g_ascii_strtoull (token, NULL, 10);
	if (value > G_MAXINT)
		return FALSE;
	*out = (guint) value;

	return TRUE;
}

/* position of @key in @map's order, or G_MAXUINT */
static guint
map_position (YamlMapping *map,
              const gchar *key)
{
	guint i;
	guint n;

	n = yaml_mapping_get_size (map);
	for (i = 0; i < n; i++)
	{
		if (g_strcmp0 (yaml_mapping_get_key (map, i), key) == 0)
			return i;
	}

	return G_MAXUINT;
}

/*
 * Inserts @key -> @node at @position of @map's key order.  yaml-glib only
 * appends, so the keys from @position on are detached (keeping references
 * to their nodes), the new key appended and the tail appended again.
 */
static void
map_insert (YamlMapping *map,
            guint        position,
            const gchar *key,
            YamlNode    *node)
{
	g_autoptr(GPtrArray) tail_keys = NULL;
	g_autoptr(GPtrArray) tail_nodes = NULL;
	guint n;
	guint i;

	n = yaml_mapping_get_size (map);
	if (position > n)
		position = n;

	tail_keys = g_ptr_array_new_with_free_func (g_free);
	tail_nodes = g_ptr_array_new_with_free_func ((GDestroyNotify) yaml_node_unref);
	for (i = position; i < n; i++)
	{
		const gchar *k = yaml_mapping_get_key (map, i);

		g_ptr_array_add (tail_keys, g_strdup (k));
		g_ptr_array_add (tail_nodes, yaml_node_ref (yaml_mapping_get_member (map, k)));
	}
	for (i = 0; i < tail_keys->len; i++)
		yaml_mapping_remove_member (map, (const gchar *) g_ptr_array_index (tail_keys, i));

	yaml_mapping_set_member (map, key, node);
	for (i = 0; i < tail_keys->len; i++)
	{
		yaml_mapping_set_member (map, (const gchar *) g_ptr_array_index (tail_keys, i),
		                         (YamlNode *) g_ptr_array_index (tail_nodes, i));
	}
}

/* inserts @node at @index of @seq (same detach-and-reappend trick) */
static void
seq_insert (YamlSequence *seq,
            guint         index,
            YamlNode     *node)
{
	g_autoptr(GPtrArray) tail = NULL;
	guint n;
	guint i;

	n = yaml_sequence_get_length (seq);
	if (index > n)
		index = n;

	tail = g_ptr_array_new_with_free_func ((GDestroyNotify) yaml_node_unref);
	for (i = index; i < n; i++)
		g_ptr_array_add (tail, yaml_node_ref (yaml_sequence_get_element (seq, i)));
	for (i = n; i > index; i--)
		yaml_sequence_remove_element (seq, i - 1);

	yaml_sequence_add_element (seq, node);
	for (i = 0; i < tail->len; i++)
		yaml_sequence_add_element (seq, (YamlNode *) g_ptr_array_index (tail, i));
}

/* removes and returns (transfer full) the item at @index */
static YamlNode *
seq_take (YamlSequence *seq,
          guint         index)
{
	YamlNode *node;

	node = yaml_sequence_dup_element (seq, index);
	if (node != NULL)
		yaml_sequence_remove_element (seq, index);

	return node;
}

/* ==========================================================================
 * Paths
 * ========================================================================== */

gchar *
lrg_editor_document_path_escape (const gchar *token)
{
	GString *out;
	const gchar *p;

	g_return_val_if_fail (token != NULL, NULL);

	out = g_string_new (NULL);
	for (p = token; *p != '\0'; p++)
	{
		if (*p == '~')
			g_string_append (out, "~0");
		else if (*p == '/')
			g_string_append (out, "~1");
		else
			g_string_append_c (out, *p);
	}

	return g_string_free (out, FALSE);
}

gchar *
lrg_editor_document_path_join (const gchar *parent,
                               const gchar *token)
{
	g_autofree gchar *escaped = NULL;

	g_return_val_if_fail (token != NULL, NULL);

	escaped = lrg_editor_document_path_escape (token);
	if (parent == NULL || *parent == '\0' || g_strcmp0 (parent, "/") == 0)
		return g_strconcat ("/", escaped, NULL);

	return g_strconcat (parent, "/", escaped, NULL);
}

GStrv
lrg_editor_document_path_split (const gchar  *path,
                                GError      **error)
{
	g_autoptr(GStrvBuilder) builder = NULL;
	g_auto(GStrv) raw = NULL;
	guint i;

	g_return_val_if_fail (path != NULL, NULL);

	builder = g_strv_builder_new ();

	/* "" and "/" are the root */
	if (*path == '\0' || g_strcmp0 (path, "/") == 0)
		return g_strv_builder_end (builder);

	if (path[0] != '/')
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		             "path '%s' does not start with '/'", path);
		return NULL;
	}

	/* split after the leading '/', then undo the ~1 / ~0 escapes */
	raw = g_strsplit (path + 1, "/", -1);
	for (i = 0; raw[i] != NULL; i++)
	{
		GString *token;
		const gchar *p;

		token = g_string_new (NULL);
		for (p = raw[i]; *p != '\0'; p++)
		{
			if (*p != '~')
			{
				g_string_append_c (token, *p);
				continue;
			}
			if (p[1] == '0')
				g_string_append_c (token, '~');
			else if (p[1] == '1')
				g_string_append_c (token, '/');
			else
			{
				g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR,
				             LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
				             "path '%s': '~' must be followed by 0 or 1", path);
				g_string_free (token, TRUE);
				return NULL;
			}
			p++;
		}
		g_strv_builder_take (builder, g_string_free (token, FALSE));
	}

	return g_strv_builder_end (builder);
}

/* joins the first @n unescaped @tokens back into a normalized path */
static gchar *
tokens_to_path (gchar **tokens,
                guint   n)
{
	GString *out;
	guint i;

	out = g_string_new (NULL);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *escaped = lrg_editor_document_path_escape (tokens[i]);

		g_string_append_c (out, '/');
		g_string_append (out, escaped);
	}

	return g_string_free (out, FALSE);
}

/* walks @n tokens down from @root */
static YamlNode *
resolve_tokens (YamlNode     *root,
                gchar       **tokens,
                guint         n,
                GError      **error)
{
	YamlNode *node;
	guint i;

	node = root;
	for (i = 0; i < n; i++)
	{
		YamlNodeType type;
		YamlNode *child;

		child = NULL;
		type = yaml_node_get_node_type (node);
		if (type == YAML_NODE_MAPPING)
		{
			child = yaml_mapping_get_member (yaml_node_get_mapping (node), tokens[i]);
		}
		else if (type == YAML_NODE_SEQUENCE)
		{
			guint index;

			if (!parse_index (tokens[i], &index))
			{
				g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR,
				             LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
				             "'%s' is not a sequence index", tokens[i]);
				return NULL;
			}
			child = yaml_sequence_get_element (yaml_node_get_sequence (node), index);
		}
		else
		{
			g_autofree gchar *where = tokens_to_path (tokens, i);

			g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
			             "'%s' is a scalar and has no '%s'",
			             *where != '\0' ? where : "/", tokens[i]);
			return NULL;
		}

		if (child == NULL)
		{
			g_autofree gchar *where = tokens_to_path (tokens, i + 1);

			g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
			             "no node at '%s'", where);
			return NULL;
		}
		node = child;
	}

	return node;
}

/* resolves a whole path string */
static YamlNode *
resolve (LrgEditorDocument  *self,
         const gchar        *path,
         GError            **error)
{
	g_auto(GStrv) tokens = NULL;

	tokens = lrg_editor_document_path_split (path, error);
	if (tokens == NULL)
		return NULL;

	return resolve_tokens (self->root, tokens, g_strv_length (tokens), error);
}

/* resolves @path to a container of the wanted type */
static YamlNode *
resolve_container (LrgEditorDocument  *self,
                   const gchar        *path,
                   YamlNodeType        type,
                   GError            **error)
{
	YamlNode *node;

	node = resolve (self, path, error);
	if (node == NULL)
		return NULL;
	if (yaml_node_get_node_type (node) != type)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "'%s' is not a %s", *path != '\0' ? path : "/",
		             type == YAML_NODE_MAPPING ? "mapping" : "sequence");
		return NULL;
	}

	return node;
}

/* quiet lookup used by the readers and the validators */
static YamlNode *
lookup (LrgEditorDocument *self,
        const gchar       *path)
{
	return resolve (self, path, NULL);
}

/* ==========================================================================
 * Line index (libyaml event walk)
 * ========================================================================== */

typedef struct
{
	gboolean    is_map;
	gchar      *path;      /* NULL inside a complex key (ignored) */
	guint       index;     /* next sequence index */
	gboolean    want_key;  /* mapping: next node is a key */
	gchar      *key;
	guint       key_line;
	guint       key_column;
	GHashTable *seen;      /* mapping: key -> GUINT line of first use */
} Frame;

static void
frame_free (gpointer data)
{
	Frame *frame = (Frame *) data;

	g_free (frame->path);
	g_free (frame->key);
	g_clear_pointer (&frame->seen, g_hash_table_unref);
	g_free (frame);
}

/*
 * Walks the first document of @text and records where every node starts.
 * A mapping value is recorded at its key's position, which is where an
 * editor wants to jump.  With @duplicates, every key that repeats within
 * one mapping adds a warning issue (yaml-glib keeps only one of them).
 */
static GPtrArray *
build_line_index (const gchar *text,
                  gsize        length,
                  GPtrArray   *duplicates)
{
	yaml_parser_t parser;
	GPtrArray *entries;
	g_autoptr(GPtrArray) stack = NULL;
	gboolean done;

	entries = g_ptr_array_new_with_free_func (line_entry_free);
	stack = g_ptr_array_new_with_free_func (frame_free);

	if (!yaml_parser_initialize (&parser))
		return entries;
	yaml_parser_set_input_string (&parser, (const unsigned char *) text, length);

	done = FALSE;
	while (!done)
	{
		yaml_event_t event;
		Frame *top;
		gboolean is_node;

		if (!yaml_parser_parse (&parser, &event))
			break;

		top = stack->len > 0 ? (Frame *) g_ptr_array_index (stack, stack->len - 1) : NULL;
		is_node = (event.type == YAML_SCALAR_EVENT || event.type == YAML_SEQUENCE_START_EVENT ||
		           event.type == YAML_MAPPING_START_EVENT || event.type == YAML_ALIAS_EVENT);

		if (event.type == YAML_DOCUMENT_END_EVENT || event.type == YAML_STREAM_END_EVENT)
		{
			done = TRUE;
		}
		else if (event.type == YAML_SEQUENCE_END_EVENT || event.type == YAML_MAPPING_END_EVENT)
		{
			if (stack->len > 0)
				g_ptr_array_remove_index (stack, stack->len - 1);
		}
		else if (is_node && top != NULL && top->is_map && top->want_key)
		{
			/* --- a mapping key --- */
			g_clear_pointer (&top->key, g_free);
			top->key_line = (guint) event.start_mark.line + 1;
			top->key_column = (guint) event.start_mark.column + 1;
			top->want_key = FALSE;

			if (event.type == YAML_SCALAR_EVENT)
			{
				const gchar *key = (const gchar *) event.data.scalar.value;
				gpointer first;

				top->key = g_strdup (key);
				if (duplicates != NULL && top->path != NULL &&
				    g_hash_table_lookup_extended (top->seen, key, NULL, &first))
				{
					g_autofree gchar *path = lrg_editor_document_path_join (top->path, key);
					g_autofree gchar *msg = NULL;

					msg = g_strdup_printf ("duplicate key '%s' (first at line %u): only the last "
					                       "value is kept, at the first key's position",
					                       key, GPOINTER_TO_UINT (first));
					g_ptr_array_add (duplicates,
					                 lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, path,
					                                                top->key_line, top->key_column, msg));
				}
				else if (top->seen != NULL)
				{
					g_hash_table_insert (top->seen, g_strdup (key), GUINT_TO_POINTER (top->key_line));
				}
			}
			else if (event.type != YAML_ALIAS_EVENT)
			{
				/* a complex key: walk it without recording anything */
				Frame *frame = g_new0 (Frame, 1);

				frame->is_map = (event.type == YAML_MAPPING_START_EVENT);
				frame->want_key = frame->is_map;
				g_ptr_array_add (stack, frame);
			}
		}
		else if (is_node)
		{
			/* --- a value: record it and descend into collections --- */
			g_autofree gchar *path = NULL;
			guint line;
			guint column;

			if (top == NULL)
			{
				path = g_strdup ("");
				line = (guint) event.start_mark.line + 1;
				column = (guint) event.start_mark.column + 1;
			}
			else if (top->path == NULL)
			{
				line = 0;
				column = 0;
			}
			else if (top->is_map)
			{
				path = lrg_editor_document_path_join (top->path, top->key != NULL ? top->key : "");
				line = top->key_line;
				column = top->key_column;
			}
			else
			{
				g_autofree gchar *idx = g_strdup_printf ("%u", top->index);

				path = lrg_editor_document_path_join (top->path, idx);
				line = (guint) event.start_mark.line + 1;
				column = (guint) event.start_mark.column + 1;
			}

			if (top != NULL)
			{
				if (top->is_map)
					top->want_key = TRUE;
				else
					top->index++;
			}

			if (path != NULL)
			{
				LineEntry *entry = g_new0 (LineEntry, 1);

				entry->path = g_strdup (path);
				entry->line = line;
				entry->column = column;
				g_ptr_array_add (entries, entry);
			}

			if (event.type == YAML_SEQUENCE_START_EVENT || event.type == YAML_MAPPING_START_EVENT)
			{
				Frame *frame = g_new0 (Frame, 1);

				frame->is_map = (event.type == YAML_MAPPING_START_EVENT);
				frame->want_key = frame->is_map;
				frame->path = g_steal_pointer (&path);
				if (frame->is_map)
					frame->seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
				g_ptr_array_add (stack, frame);
			}
		}

		yaml_event_delete (&event);
	}

	yaml_parser_delete (&parser);

	return entries;
}

static GPtrArray *
line_index (LrgEditorDocument *self)
{
	if (self->line_index == NULL)
	{
		const gchar *text;
		gsize length;

		text = document_text (self, &length);
		self->line_index = build_line_index (text, length, NULL);
	}

	return self->line_index;
}

/* ==========================================================================
 * Undo bookkeeping
 * ========================================================================== */

static guint64
current_serial (LrgEditorDocument *self)
{
	if (self->undo_stack->len == 0)
		return self->base_serial;

	return ((Cmd *) g_ptr_array_index (self->undo_stack, self->undo_stack->len - 1))->serial;
}

/* true while an open group already holds edits */
static gboolean
group_has_edits (LrgEditorDocument *self)
{
	guint i;

	for (i = 0; i < self->groups->len; i++)
	{
		if (((Cmd *) g_ptr_array_index (self->groups, i))->children->len > 0)
			return TRUE;
	}

	return FALSE;
}

/* recomputes dirty and the can-undo/can-redo notifications */
static void
update_state (LrgEditorDocument *self)
{
	gboolean dirty;
	gboolean can_undo;
	gboolean can_redo;

	dirty = group_has_edits (self) || current_serial (self) != self->clean_serial;
	if (dirty != self->dirty)
	{
		self->dirty = dirty;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRTY]);
		g_signal_emit (self, signals[SIGNAL_DIRTY_CHANGED], 0, dirty);
	}

	can_undo = lrg_editor_document_can_undo (self);
	if (can_undo != self->notified_can_undo)
	{
		self->notified_can_undo = can_undo;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAN_UNDO]);
	}
	can_redo = lrg_editor_document_can_redo (self);
	if (can_redo != self->notified_can_redo)
	{
		self->notified_can_redo = can_redo;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAN_REDO]);
	}
}

/* drops the oldest steps beyond the limit; their state becomes the base */
static void
enforce_limit (LrgEditorDocument *self)
{
	while (self->undo_limit > 0 && self->undo_stack->len > self->undo_limit)
	{
		Cmd *oldest = (Cmd *) g_ptr_array_index (self->undo_stack, 0);

		if (self->merge_candidate == oldest)
			self->merge_candidate = NULL;
		self->base_serial = oldest->serial;
		g_ptr_array_remove_index (self->undo_stack, 0);
	}
}

/* merges @cmd into @prev when both set the same path in one gesture */
static gboolean
try_merge (Cmd *prev,
           Cmd *cmd)
{
	if (prev == NULL || cmd->kind != CMD_REPLACE || cmd->gesture == 0)
		return FALSE;
	if (prev->kind != CMD_REPLACE && prev->kind != CMD_INSERT)
		return FALSE;
	if (prev->gesture != cmd->gesture || g_strcmp0 (prev->target, cmd->target) != 0)
		return FALSE;

	/* prev keeps its "before"; the "after" becomes the newest node */
	g_clear_pointer (&prev->new_node, yaml_node_unref);
	prev->new_node = g_steal_pointer (&cmd->new_node);

	return TRUE;
}

/* records an applied command (takes ownership) */
static void
record (LrgEditorDocument *self,
        Cmd               *cmd)
{
	GPtrArray *into;

	if (self->redo_stack->len > 0)
		g_ptr_array_set_size (self->redo_stack, 0);

	into = self->groups->len > 0
	       ? ((Cmd *) g_ptr_array_index (self->groups, self->groups->len - 1))->children
	       : self->undo_stack;

	if (self->merge_candidate != NULL && into->len > 0 &&
	    g_ptr_array_index (into, into->len - 1) == self->merge_candidate &&
	    try_merge (self->merge_candidate, cmd))
	{
		self->merge_candidate->serial = ++self->next_serial;
		cmd_free (cmd);
	}
	else
	{
		cmd->serial = ++self->next_serial;
		g_ptr_array_add (into, cmd);
		self->merge_candidate = cmd;
		if (into == self->undo_stack)
			enforce_limit (self);
	}

	update_state (self);
}

/* ==========================================================================
 * Applying commands
 * ========================================================================== */

/* emits ::changed for a path */
static void
emit_changed (LrgEditorDocument *self,
              const gchar       *path)
{
	invalidate_caches (self);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0, path != NULL ? path : "");
}

/* removes @key from @map and re-adds the same node as @new_key in place */
static void
map_rename (YamlMapping *map,
            const gchar *key,
            const gchar *new_key)
{
	g_autoptr(YamlNode) node = NULL;
	guint position;

	position = map_position (map, key);
	if (position == G_MAXUINT)
		return;
	node = yaml_mapping_dup_member (map, key);
	yaml_mapping_remove_member (map, key);
	map_insert (map, position, new_key, node);
}

/*
 * Applies a command forward (do/redo) or backward (undo).  Commands are
 * only ever applied to the state they were created in, so the container
 * always resolves; a failure here means the stack is corrupt and is
 * reported loudly rather than ignored.
 */
static void
cmd_apply (LrgEditorDocument *self,
           Cmd               *cmd,
           gboolean           forward)
{
	YamlNode *container;
	YamlMapping *map;
	YamlSequence *seq;

	if (cmd->kind == CMD_GROUP)
	{
		guint i;

		for (i = 0; i < cmd->children->len; i++)
		{
			guint at = forward ? i : cmd->children->len - 1 - i;

			cmd_apply (self, (Cmd *) g_ptr_array_index (cmd->children, at), forward);
		}
		return;
	}

	if (cmd->kind == CMD_REPLACE && cmd->at_root)
	{
		YamlNode *node = forward ? cmd->new_node : cmd->old_node;

		g_clear_pointer (&self->root, yaml_node_unref);
		self->root = yaml_node_ref (node);
		emit_changed (self, "");
		return;
	}

	container = lookup (self, cmd->container);
	if (container == NULL)
	{
		g_critical ("LrgEditorDocument: undo stack out of sync at '%s'", cmd->container);
		return;
	}
	map = cmd->in_map ? yaml_node_get_mapping (container) : NULL;
	seq = cmd->in_map ? NULL : yaml_node_get_sequence (container);
	if ((cmd->in_map && map == NULL) || (!cmd->in_map && seq == NULL))
	{
		g_critical ("LrgEditorDocument: undo stack out of sync at '%s'", cmd->container);
		return;
	}

	switch (cmd->kind)
	{
	case CMD_REPLACE:
		{
			YamlNode *node = forward ? cmd->new_node : cmd->old_node;

			if (map != NULL)
			{
				yaml_mapping_set_member (map, cmd->key, node);
			}
			else
			{
				g_autoptr(YamlNode) gone = seq_take (seq, cmd->index);

				seq_insert (seq, cmd->index, node);
			}
			emit_changed (self, cmd->target);
		}
		break;

	case CMD_INSERT:
	case CMD_REMOVE:
		{
			/* an INSERT done forward is a REMOVE done backward */
			gboolean add = (cmd->kind == CMD_INSERT) == forward;
			YamlNode *node = cmd->kind == CMD_INSERT ? cmd->new_node : cmd->old_node;

			if (add && map != NULL)
				map_insert (map, cmd->index, cmd->key, node);
			else if (add)
				seq_insert (seq, cmd->index, node);
			else if (map != NULL)
				yaml_mapping_remove_member (map, cmd->key);
			else
				yaml_sequence_remove_element (seq, cmd->index);
			emit_changed (self, cmd->container);
		}
		break;

	case CMD_MOVE:
		{
			g_autoptr(YamlNode) node = NULL;

			node = seq_take (seq, forward ? cmd->index : cmd->index2);
			if (node != NULL)
				seq_insert (seq, forward ? cmd->index2 : cmd->index, node);
			emit_changed (self, cmd->container);
		}
		break;

	case CMD_RENAME:
		if (forward)
			map_rename (map, cmd->key, cmd->key2);
		else
			map_rename (map, cmd->key2, cmd->key);
		emit_changed (self, cmd->container);
		break;

	case CMD_GROUP:
	default:
		break;
	}
}

/* the edit preamble: refuse edits on a read-only document */
static gboolean
check_writable (LrgEditorDocument  *self,
                GError            **error)
{
	if (self->read_only)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_READ_ONLY,
		             "%s is read-only", self->display_name);
		return FALSE;
	}

	return TRUE;
}

/*
 * Puts @node (transfer full) at @path: replaces an existing node, creates
 * a missing last mapping key at the end, or appends to a sequence for
 * "-" / the length.  The common core of every setter and of replace().
 */
static gboolean
put_node (LrgEditorDocument  *self,
          const gchar        *path,
          YamlNode           *node,
          guint               gesture_id,
          const gchar        *verb,
          GError            **error)
{
	g_autoptr(YamlNode) owned = node;
	g_auto(GStrv) tokens = NULL;
	YamlNode *parent;
	const gchar *last;
	guint n;
	Cmd *cmd;

	if (!check_writable (self, error))
		return FALSE;
	tokens = lrg_editor_document_path_split (path, error);
	if (tokens == NULL)
		return FALSE;
	n = g_strv_length (tokens);

	/* --- the root itself --- */
	if (n == 0)
	{
		cmd = cmd_new (CMD_REPLACE, NULL);
		cmd->label = g_strdup_printf ("%s /", verb);
		cmd->at_root = TRUE;
		cmd->target = g_strdup ("");
		cmd->gesture = gesture_id;
		cmd->old_node = yaml_node_ref (self->root);
		carry_comments (cmd->old_node, owned);
		cmd->new_node = g_steal_pointer (&owned);
		cmd_apply (self, cmd, TRUE);
		record (self, cmd);
		return TRUE;
	}

	/* --- a child of a container --- */
	parent = resolve_tokens (self->root, tokens, n - 1, error);
	if (parent == NULL)
		return FALSE;
	last = tokens[n - 1];

	cmd = NULL;
	if (yaml_node_get_node_type (parent) == YAML_NODE_MAPPING)
	{
		YamlMapping *map = yaml_node_get_mapping (parent);
		YamlNode *old = yaml_mapping_get_member (map, last);

		cmd = cmd_new (old != NULL ? CMD_REPLACE : CMD_INSERT, NULL);
		cmd->in_map = TRUE;
		cmd->key = g_strdup (last);
		if (old != NULL)
		{
			cmd->old_node = yaml_node_ref (old);
			carry_comments (old, owned);
		}
		else
		{
			cmd->index = yaml_mapping_get_size (map);
		}
	}
	else if (yaml_node_get_node_type (parent) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (parent);
		guint length = yaml_sequence_get_length (seq);
		guint index;

		if (g_strcmp0 (last, "-") == 0)
			index = length;
		else if (!parse_index (last, &index))
		{
			g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
			             "'%s' is not a sequence index", last);
			return FALSE;
		}
		if (index > length)
		{
			g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
			             "index %u is past the end of a %u-item sequence", index, length);
			return FALSE;
		}

		cmd = cmd_new (index < length ? CMD_REPLACE : CMD_INSERT, NULL);
		cmd->index = index;
		if (index < length)
		{
			YamlNode *old = yaml_sequence_get_element (seq, index);

			cmd->old_node = yaml_node_ref (old);
			carry_comments (old, owned);
		}
	}
	else
	{
		g_autofree gchar *where = tokens_to_path (tokens, n - 1);

		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "'%s' is a scalar", *where != '\0' ? where : "/");
		return FALSE;
	}

	cmd->container = tokens_to_path (tokens, n - 1);
	if (cmd->in_map)
	{
		cmd->target = lrg_editor_document_path_join (cmd->container, last);
	}
	else
	{
		g_autofree gchar *idx = g_strdup_printf ("%u", cmd->index);

		cmd->target = lrg_editor_document_path_join (cmd->container, idx);
	}
	cmd->label = g_strdup_printf ("%s %s", verb, cmd->target);
	cmd->gesture = gesture_id;
	cmd->new_node = g_steal_pointer (&owned);

	cmd_apply (self, cmd, TRUE);
	record (self, cmd);

	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_document_finalize (GObject *object)
{
	LrgEditorDocument *self = LRG_EDITOR_DOCUMENT (object);

	g_clear_pointer (&self->root, yaml_node_unref);
	g_clear_pointer (&self->path, g_free);
	g_clear_pointer (&self->display_name, g_free);
	g_clear_pointer (&self->base_dir, g_free);
	g_clear_object (&self->reference);
	g_clear_pointer (&self->groups, g_ptr_array_unref);
	g_clear_pointer (&self->undo_stack, g_ptr_array_unref);
	g_clear_pointer (&self->redo_stack, g_ptr_array_unref);
	if (self->monitor != NULL)
		g_file_monitor_cancel (self->monitor);
	g_clear_object (&self->monitor);
	g_clear_pointer (&self->disk_text, g_free);
	invalidate_caches (self);
	g_clear_pointer (&self->load_issues, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_editor_document_parent_class)->finalize (object);
}

static void
lrg_editor_document_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgEditorDocument *self = LRG_EDITOR_DOCUMENT (object);

	switch (prop_id)
	{
	case PROP_FORMAT:
		g_value_set_enum (value, (gint) self->format);
		break;
	case PROP_PATH:
		g_value_set_string (value, self->path);
		break;
	case PROP_DIRTY:
		g_value_set_boolean (value, self->dirty);
		break;
	case PROP_READ_ONLY:
		g_value_set_boolean (value, self->read_only);
		break;
	case PROP_CAN_UNDO:
		g_value_set_boolean (value, lrg_editor_document_can_undo (self));
		break;
	case PROP_CAN_REDO:
		g_value_set_boolean (value, lrg_editor_document_can_redo (self));
		break;
	case PROP_UNDO_LIMIT:
		g_value_set_uint (value, self->undo_limit);
		break;
	case PROP_BASE_DIR:
		g_value_set_string (value, self->base_dir);
		break;
	case PROP_REFERENCE:
		g_value_set_object (value, self->reference);
		break;
	case PROP_MONITOR_FILE:
		g_value_set_boolean (value, self->monitor_file);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_document_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgEditorDocument *self = LRG_EDITOR_DOCUMENT (object);

	switch (prop_id)
	{
	case PROP_FORMAT:
		lrg_editor_document_set_format (self, (LrgEditorDocumentFormat) g_value_get_enum (value));
		break;
	case PROP_READ_ONLY:
		lrg_editor_document_set_read_only (self, g_value_get_boolean (value));
		break;
	case PROP_UNDO_LIMIT:
		lrg_editor_document_set_undo_limit (self, g_value_get_uint (value));
		break;
	case PROP_BASE_DIR:
		lrg_editor_document_set_base_dir (self, g_value_get_string (value));
		break;
	case PROP_REFERENCE:
		lrg_editor_document_set_reference (self, g_value_get_object (value));
		break;
	case PROP_MONITOR_FILE:
		lrg_editor_document_set_monitor_file (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_document_class_init (LrgEditorDocumentClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_editor_document_finalize;
	object_class->get_property = lrg_editor_document_get_property;
	object_class->set_property = lrg_editor_document_set_property;

	/**
	 * LrgEditorDocument:format:
	 *
	 * The engine format of the document; picks the validator.
	 */
	properties[PROP_FORMAT] =
		g_param_spec_enum ("format", NULL, NULL,
		                   LRG_TYPE_EDITOR_DOCUMENT_FORMAT, LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDocument:path:
	 *
	 * The document's file, or %NULL while untitled. Changed by
	 * lrg_editor_document_save_as().
	 */
	properties[PROP_PATH] =
		g_param_spec_string ("path", NULL, NULL, NULL,
		                     G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDocument:dirty:
	 *
	 * Whether the tree differs from the last load or save.
	 */
	properties[PROP_DIRTY] =
		g_param_spec_boolean ("dirty", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDocument:read-only:
	 *
	 * Whether edits and saves are refused. Set on open when the file is
	 * not writable.
	 */
	properties[PROP_READ_ONLY] =
		g_param_spec_boolean ("read-only", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDocument:can-undo:
	 *
	 * Whether lrg_editor_document_undo() would do something.
	 */
	properties[PROP_CAN_UNDO] =
		g_param_spec_boolean ("can-undo", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDocument:can-redo:
	 *
	 * Whether lrg_editor_document_redo() would do something.
	 */
	properties[PROP_CAN_REDO] =
		g_param_spec_boolean ("can-redo", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDocument:undo-limit:
	 *
	 * The maximum number of undo steps kept; 0 means unlimited.
	 */
	properties[PROP_UNDO_LIMIT] =
		g_param_spec_uint ("undo-limit", NULL, NULL, 0, G_MAXUINT, DEFAULT_UNDO_LIMIT,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDocument:base-dir:
	 *
	 * Directory relative files (sound bank entries) are resolved
	 * against; %NULL uses the document's own directory.
	 */
	properties[PROP_BASE_DIR] =
		g_param_spec_string ("base-dir", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDocument:reference:
	 *
	 * Document validation compares against (the source locale).
	 */
	properties[PROP_REFERENCE] =
		g_param_spec_object ("reference", NULL, NULL, LRG_TYPE_EDITOR_DOCUMENT,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorDocument:monitor-file:
	 *
	 * Whether a #GFileMonitor watches the file for external changes.
	 */
	properties[PROP_MONITOR_FILE] =
		g_param_spec_boolean ("monitor-file", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgEditorDocument::changed:
	 * @self: the document
	 * @path: the YAML path whose subtree changed ("" for the root); for
	 *   inserts, removes, moves and renames the container's path
	 *
	 * Emitted after every edit, undo and redo step (several times for a
	 * group) and with "" after a reload.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgEditorDocument::dirty-changed:
	 * @self: the document
	 * @dirty: the new value of #LrgEditorDocument:dirty
	 *
	 * Emitted when the document becomes dirty or clean.
	 */
	signals[SIGNAL_DIRTY_CHANGED] =
		g_signal_new ("dirty-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	/**
	 * LrgEditorDocument::saved:
	 * @self: the document
	 * @path: the file written
	 *
	 * Emitted after a successful save.
	 */
	signals[SIGNAL_SAVED] =
		g_signal_new ("saved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgEditorDocument::reloaded:
	 * @self: the document
	 *
	 * Emitted after lrg_editor_document_reload() replaced the tree.
	 */
	signals[SIGNAL_RELOADED] =
		g_signal_new ("reloaded", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgEditorDocument::file-changed:
	 * @self: the document
	 *
	 * Emitted once when another program changed or deleted the file since
	 * the last load or save; the editor offers "reload" or "keep mine".
	 */
	signals[SIGNAL_FILE_CHANGED] =
		g_signal_new ("file-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_editor_document_init (LrgEditorDocument *self)
{
	self->root = yaml_node_new_null ();
	self->display_name = g_strdup ("untitled");
	self->undo_stack = g_ptr_array_new_with_free_func (cmd_free);
	self->redo_stack = g_ptr_array_new_with_free_func (cmd_free);
	self->groups = g_ptr_array_new_with_free_func (cmd_free);
	self->undo_limit = DEFAULT_UNDO_LIMIT;
	self->load_issues = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_document_issue_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/* engine-backed formats (defined with the object API below) */
static gchar *engine_skeleton (LrgEditorDocumentFormat format);
static const gchar *file_text (LrgEditorDocument *self,
                               gsize             *length);

/* the smallest document each loader accepts */
static const gchar *
skeleton_for (LrgEditorDocumentFormat format)
{
	switch (format)
	{
	case LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP:
		return "actions: {}\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_LOCALE:
		return "code: en\nname: English\nstrings: {}\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK:
		return "name: sounds\nsounds: {}\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT:
		return "version: 1\npieces: {}\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT:
		return "type: vbox\nchildren: []\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE:
		return "id: dialog\nstart: start\nnodes:\n  - id: start\n    text: ''\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON:
		return "version: 1\ngenerator: bsp\nwidth: 64\nheight: 48\nseed: 0\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_THEME:
		return "preset: midnight\nname: untitled\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL:
		return "version: 1\nname: material\nshading_model: lit\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK:
		return "version: 1\neffects: []\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH:
		return "version: 1\nname: graph\nnodes: []\nlinks: []\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH:
		/* one base layer with an empty idle state: loads, plays the bind pose */
		return "version: 1\nname: graph\nlayers:\n  - name: base\n    default: idle\n"
		       "    states:\n      - name: idle\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP:
		/* the default rules written out so the form shows them */
		return "version: 1\nrules:\n  ignore_case: true\n  normalize_separators: true\n"
		       "  normalize_sides: true\nbones: {}\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE:
		return "type: sequence\nversion: 1\nname: sequence\nlength: 5\nfps: 30\ntracks: []\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D:
		/* a single root bone to hang the first slot on */
		return "rskel2d: 1\nname: skeleton\nbones:\n  - name: root\n    length: 50\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET:
		/* a ragdoll without parts: the editor's Generate fills it */
		return "version: 1\nparts: []\n";
	case LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN:
	default:
		return "{}\n";
	}
}

/* resets the undo state so the current tree is the clean state */
static void
reset_history (LrgEditorDocument *self)
{
	g_ptr_array_set_size (self->undo_stack, 0);
	g_ptr_array_set_size (self->redo_stack, 0);
	g_ptr_array_set_size (self->groups, 0);
	self->merge_candidate = NULL;
	self->base_serial = ++self->next_serial;
	self->clean_serial = self->base_serial;
	update_state (self);
}

/* modification time of @path in microseconds, or -1 */
static gint64
file_mtime (const gchar *path)
{
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(GDateTime) when = NULL;

	file = g_file_new_for_path (path);
	info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED "," G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
	                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (info == NULL)
		return -1;
	when = g_file_info_get_modification_date_time (info);
	if (when == NULL)
		return -1;

	return g_date_time_to_unix_usec (when);
}

/* remembers what is on disk now (after a load or save) */
static void
remember_disk (LrgEditorDocument *self,
               const gchar       *text)
{
	g_free (self->disk_text);
	self->disk_text = g_strdup (text);
	self->disk_mtime = self->path != NULL ? file_mtime (self->path) : -1;
	self->external_changed = FALSE;
}

/* sets the path and the display name */
static void
set_path_internal (LrgEditorDocument *self,
                   const gchar       *path)
{
	if (g_strcmp0 (self->path, path) == 0)
		return;

	g_free (self->path);
	self->path = g_strdup (path);
	g_free (self->display_name);
	self->display_name = path != NULL ? g_path_get_basename (path) : g_strdup ("untitled");
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PATH]);

	/* a monitor follows the file */
	if (self->monitor_file)
	{
		lrg_editor_document_set_monitor_file (self, FALSE);
		lrg_editor_document_set_monitor_file (self, TRUE);
	}
}

/* parses @text into a fresh tree and records duplicate keys */
static gboolean
load_text (LrgEditorDocument  *self,
           const gchar        *text,
           gsize               length,
           GError            **error)
{
	g_autoptr(GPtrArray) dups = NULL;
	g_autoptr(GPtrArray) entries = NULL;
	YamlNode *root;

	root = parse_text (text, (gssize) length, error);
	if (root == NULL)
		return FALSE;

	dups = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_document_issue_free);
	entries = build_line_index (text, length, dups);

	g_clear_pointer (&self->root, yaml_node_unref);
	self->root = root;
	g_clear_pointer (&self->load_issues, g_ptr_array_unref);
	self->load_issues = g_steal_pointer (&dups);
	invalidate_caches (self);

	return TRUE;
}

LrgEditorDocument *
lrg_editor_document_new (LrgEditorDocumentFormat format)
{
	LrgEditorDocument *self;
	g_autofree gchar *engine_text = NULL;
	const gchar *text;

	self = g_object_new (LRG_TYPE_EDITOR_DOCUMENT, NULL);
	self->format = format;

	/* an engine-backed format starts as the engine's own empty object */
	engine_text = engine_skeleton (format);
	text = engine_text != NULL ? engine_text : skeleton_for (format);
	if (!load_text (self, text, strlen (text), NULL))
		g_assert_not_reached ();
	reset_history (self);

	return self;
}

LrgEditorDocument *
lrg_editor_document_new_from_data (const gchar              *data,
                                   gssize                    length,
                                   LrgEditorDocumentFormat   format,
                                   GError                  **error)
{
	g_autoptr(LrgEditorDocument) self = NULL;
	gsize len;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	len = length < 0 ? strlen (data) : (gsize) length;
	self = g_object_new (LRG_TYPE_EDITOR_DOCUMENT, NULL);
	if (!load_text (self, data, len, error))
		return NULL;
	self->format = format != LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN
	               ? format
	               : lrg_editor_document_detect_format (NULL, data, (gssize) len);
	reset_history (self);

	return g_steal_pointer (&self);
}

LrgEditorDocument *
lrg_editor_document_new_from_file (const gchar  *path,
                                   GError      **error)
{
	g_autoptr(LrgEditorDocument) self = NULL;
	g_autoptr(GError) local = NULL;
	g_autofree gchar *text = NULL;
	gsize length;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &length, &local))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_IO,
		             "%s", local->message);
		return NULL;
	}

	self = g_object_new (LRG_TYPE_EDITOR_DOCUMENT, NULL);
	if (!load_text (self, text, length, &local))
	{
		g_autofree gchar *base = g_path_get_basename (path);

		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_PARSE,
		             "%s: %s", base, local->message);
		return NULL;
	}

	set_path_internal (self, path);
	self->format = lrg_editor_document_detect_format (path, text, (gssize) length);
	self->read_only = g_access (path, W_OK) != 0;
	remember_disk (self, text);
	reset_history (self);

	return g_steal_pointer (&self);
}

/* the root mapping's members, for detection */
static gboolean
root_has (YamlMapping *map,
          const gchar *key)
{
	return map != NULL && yaml_mapping_has_member (map, key);
}

/* content sniffing: the keys each loader requires or is known by */
static LrgEditorDocumentFormat
detect_from_tree (YamlNode *root)
{
	YamlMapping *map;
	YamlNode *node;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		return LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN;
	map = yaml_node_get_mapping (root);

	node = yaml_mapping_get_member (map, "actions");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
		return LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP;
	if (root_has (map, "sounds"))
		return LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK;
	if (root_has (map, "pieces"))
		return LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT;
	node = yaml_mapping_get_member (map, "nodes");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
		return LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE;
	if (root_has (map, "strings") || (root_has (map, "code") && root_has (map, "name")))
		return LRG_EDITOR_DOCUMENT_FORMAT_LOCALE;
	if (root_has (map, "generator"))
		return LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON;
	if (root_has (map, "colors") || root_has (map, "metrics") || root_has (map, "preset") ||
	    root_has (map, "accent") || root_has (map, "styles") || root_has (map, "motion"))
		return LRG_EDITOR_DOCUMENT_FORMAT_THEME;
	/* the 2D skeleton format's version key names the format */
	if (root_has (map, "rskel2d"))
		return LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D;
	/* a physics asset: `parts:` is a sequence of mappings naming bones */
	node = yaml_mapping_get_member (map, "parts");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE && root_has (map, "version"))
	{
		YamlSequence *seq = yaml_node_get_sequence (node);
		YamlNode     *first = yaml_sequence_get_length (seq) > 0 ? yaml_sequence_get_element (seq, 0) : NULL;

		if (first == NULL || (yaml_node_get_node_type (first) == YAML_NODE_MAPPING &&
		                      yaml_mapping_has_member (yaml_node_get_mapping (first), "bone")))
			return LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET;
	}
	/* `type: sequence` is the sequence format's own marker; any other
	 * `type:` is a UI layout node */
	node = yaml_mapping_get_member (map, "type");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR &&
	    g_strcmp0 (yaml_node_get_scalar (node), "sequence") == 0)
		return LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE;
	if (root_has (map, "type"))
		return LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT;

	return LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN;
}

LrgEditorDocumentFormat
lrg_editor_document_detect_format (const gchar *filename,
                                   const gchar *data,
                                   gssize       length)
{
	static const struct { const gchar *suffix; LrgEditorDocumentFormat format; } suffixes[] = {
		{ ".input", LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP },
		{ ".inputmap", LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP },
		{ ".locale", LRG_EDITOR_DOCUMENT_FORMAT_LOCALE },
		{ ".bank", LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK },
		{ ".soundbank", LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK },
		{ ".hud", LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT },
		{ ".ui", LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT },
		{ ".dialog", LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE },
		{ ".dungeon", LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON },
		{ ".theme", LRG_EDITOR_DOCUMENT_FORMAT_THEME }
	};
	static const struct { const gchar *dir; LrgEditorDocumentFormat format; } dirs[] = {
		{ "locales", LRG_EDITOR_DOCUMENT_FORMAT_LOCALE },
		{ "locale", LRG_EDITOR_DOCUMENT_FORMAT_LOCALE },
		{ "i18n", LRG_EDITOR_DOCUMENT_FORMAT_LOCALE },
		{ "lang", LRG_EDITOR_DOCUMENT_FORMAT_LOCALE },
		{ "themes", LRG_EDITOR_DOCUMENT_FORMAT_THEME },
		{ "dialogs", LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE },
		{ "dialog", LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE },
		{ "dialogue", LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE },
		{ "input", LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP },
		{ "dungeons", LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON },
		{ "ui", LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT }
	};
	guint i;

	/* --- 0. the engine asset extensions decide on their own --- */
	if (filename != NULL)
	{
		g_autofree gchar *lower = g_ascii_strdown (filename, -1);

		for (i = 0; i < G_N_ELEMENTS (format_extensions); i++)
		{
			if (g_str_has_suffix (lower, format_extensions[i].extension))
				return format_extensions[i].format;
		}
	}

	/* --- 1. a double extension is an explicit statement --- */
	if (filename != NULL)
	{
		g_autofree gchar *base = g_path_get_basename (filename);
		g_autofree gchar *lower = g_ascii_strdown (base, -1);
		gchar *dot;

		dot = strrchr (lower, '.');
		if (dot != NULL && (g_strcmp0 (dot, ".yaml") == 0 || g_strcmp0 (dot, ".yml") == 0))
		{
			*dot = '\0';
			for (i = 0; i < G_N_ELEMENTS (suffixes); i++)
			{
				if (g_str_has_suffix (lower, suffixes[i].suffix))
					return suffixes[i].format;
			}
			if (g_strcmp0 (lower, "dungeon") == 0)
				return LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON;
			if (g_strcmp0 (lower, "theme") == 0)
				return LRG_EDITOR_DOCUMENT_FORMAT_THEME;
		}
	}

	/* --- 2. the content --- */
	if (data != NULL)
	{
		g_autoptr(YamlParser) parser = yaml_parser_new ();

		if (yaml_parser_load_from_data (parser, data, length, NULL))
		{
			LrgEditorDocumentFormat format = detect_from_tree (yaml_parser_get_root (parser));

			if (format != LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN)
				return format;
		}
	}

	/* --- 3. the directory it lives in --- */
	if (filename != NULL)
	{
		g_autofree gchar *dir = g_path_get_dirname (filename);
		g_autofree gchar *name = g_path_get_basename (dir);

		for (i = 0; i < G_N_ELEMENTS (dirs); i++)
		{
			if (g_ascii_strcasecmp (name, dirs[i].dir) == 0)
				return dirs[i].format;
		}
	}

	return LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN;
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

LrgEditorDocumentFormat
lrg_editor_document_get_format (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN);

	return self->format;
}

void
lrg_editor_document_set_format (LrgEditorDocument       *self,
                                LrgEditorDocumentFormat  format)
{
	g_return_if_fail (LRG_IS_EDITOR_DOCUMENT (self));

	if (self->format == format)
		return;
	self->format = format;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FORMAT]);
}

const gchar *
lrg_editor_document_get_path (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	return self->path;
}

const gchar *
lrg_editor_document_get_display_name (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	return self->display_name;
}

gboolean
lrg_editor_document_get_dirty (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	return self->dirty;
}

gboolean
lrg_editor_document_get_read_only (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	return self->read_only;
}

void
lrg_editor_document_set_read_only (LrgEditorDocument *self,
                                   gboolean           read_only)
{
	g_return_if_fail (LRG_IS_EDITOR_DOCUMENT (self));

	read_only = !!read_only;
	if (self->read_only == read_only)
		return;
	self->read_only = read_only;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_READ_ONLY]);
}

const gchar *
lrg_editor_document_get_base_dir (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	return self->base_dir;
}

void
lrg_editor_document_set_base_dir (LrgEditorDocument *self,
                                  const gchar       *base_dir)
{
	g_return_if_fail (LRG_IS_EDITOR_DOCUMENT (self));

	if (g_strcmp0 (self->base_dir, base_dir) == 0)
		return;
	g_free (self->base_dir);
	self->base_dir = g_strdup (base_dir);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BASE_DIR]);
}

LrgEditorDocument *
lrg_editor_document_get_reference (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	return self->reference;
}

void
lrg_editor_document_set_reference (LrgEditorDocument *self,
                                   LrgEditorDocument *reference)
{
	g_return_if_fail (LRG_IS_EDITOR_DOCUMENT (self));
	g_return_if_fail (reference == NULL || LRG_IS_EDITOR_DOCUMENT (reference));
	g_return_if_fail (reference != self);

	if (g_set_object (&self->reference, reference))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REFERENCE]);
}

gboolean
lrg_editor_document_get_monitor_file (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	return self->monitor_file;
}

/* GFileMonitor callback: any event re-runs the mtime/content check */
static void
on_monitor_changed (GFileMonitor      *monitor,
                    GFile             *file,
                    GFile             *other_file,
                    GFileMonitorEvent  event_type,
                    gpointer           user_data)
{
	LrgEditorDocument *self = LRG_EDITOR_DOCUMENT (user_data);

	(void) monitor;
	(void) file;
	(void) other_file;

	switch (event_type)
	{
	case G_FILE_MONITOR_EVENT_CHANGED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_DELETED:
	case G_FILE_MONITOR_EVENT_RENAMED:
	case G_FILE_MONITOR_EVENT_MOVED_IN:
	case G_FILE_MONITOR_EVENT_MOVED_OUT:
		lrg_editor_document_check_external_change (self);
		break;
	case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
	case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
	case G_FILE_MONITOR_EVENT_UNMOUNTED:
	case G_FILE_MONITOR_EVENT_MOVED:
	default:
		break;
	}
}

void
lrg_editor_document_set_monitor_file (LrgEditorDocument *self,
                                      gboolean           monitor)
{
	g_return_if_fail (LRG_IS_EDITOR_DOCUMENT (self));

	monitor = !!monitor;

	/* tear down any existing monitor first */
	if (self->monitor != NULL)
	{
		g_signal_handlers_disconnect_by_func (self->monitor, on_monitor_changed, self);
		g_file_monitor_cancel (self->monitor);
		g_clear_object (&self->monitor);
	}

	if (monitor && self->path != NULL)
	{
		g_autoptr(GFile) file = g_file_new_for_path (self->path);
		g_autoptr(GError) error = NULL;

		self->monitor = g_file_monitor_file (file, G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
		if (self->monitor != NULL)
			g_signal_connect (self->monitor, "changed", G_CALLBACK (on_monitor_changed), self);
		else
			g_debug ("LrgEditorDocument: cannot monitor %s: %s", self->path, error->message);
	}

	if (self->monitor_file != monitor)
	{
		self->monitor_file = monitor;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MONITOR_FILE]);
	}
}

/* ==========================================================================
 * Reading
 * ========================================================================== */

LrgEditorDocumentNodeKind
lrg_editor_document_get_kind (LrgEditorDocument *self,
                              const gchar       *path)
{
	YamlNode *node;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), LRG_EDITOR_DOCUMENT_NODE_MISSING);
	g_return_val_if_fail (path != NULL, LRG_EDITOR_DOCUMENT_NODE_MISSING);

	node = lookup (self, path);
	if (node == NULL)
		return LRG_EDITOR_DOCUMENT_NODE_MISSING;

	switch (yaml_node_get_node_type (node))
	{
	case YAML_NODE_MAPPING:
		return LRG_EDITOR_DOCUMENT_NODE_MAPPING;
	case YAML_NODE_SEQUENCE:
		return LRG_EDITOR_DOCUMENT_NODE_SEQUENCE;
	case YAML_NODE_SCALAR:
		return LRG_EDITOR_DOCUMENT_NODE_SCALAR;
	case YAML_NODE_NULL:
	default:
		return LRG_EDITOR_DOCUMENT_NODE_NULL;
	}
}

gboolean
lrg_editor_document_has_path (LrgEditorDocument *self,
                              const gchar       *path)
{
	return lrg_editor_document_get_kind (self, path) != LRG_EDITOR_DOCUMENT_NODE_MISSING;
}

const gchar *
lrg_editor_document_get_scalar (LrgEditorDocument *self,
                                const gchar       *path)
{
	YamlNode *node;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	node = lookup (self, path);
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;

	return yaml_node_get_scalar (node);
}

/* YAML 1.1/1.2 boolean words the engine loaders accept */
static gboolean
parse_bool_word (const gchar *text,
                 gboolean    *out)
{
	static const gchar * const yes[] = { "true", "True", "TRUE", "yes", "Yes", "YES", "on", "On", "ON", NULL };
	static const gchar * const no[] = { "false", "False", "FALSE", "no", "No", "NO", "off", "Off", "OFF", NULL };

	if (g_strv_contains (yes, text))
	{
		*out = TRUE;
		return TRUE;
	}
	if (g_strv_contains (no, text))
	{
		*out = FALSE;
		return TRUE;
	}

	return FALSE;
}

/* a plain number the way YAML reads one */
static gboolean
parse_number (const gchar *text,
              gint64      *as_int,
              gdouble     *as_double,
              gboolean    *is_int)
{
	gchar *end;
	gint64 i;
	gdouble d;

	if (text == NULL || *text == '\0')
		return FALSE;

	errno = 0;
	i = g_ascii_strtoll (text, &end, 10);
	if (*end == '\0' && errno == 0)
	{
		*as_int = i;
		*as_double = (gdouble) i;
		*is_int = TRUE;
		return TRUE;
	}

	if (g_strcmp0 (text, ".inf") == 0 || g_strcmp0 (text, "+.inf") == 0)
		d = INFINITY;
	else if (g_strcmp0 (text, "-.inf") == 0)
		d = -INFINITY;
	else if (g_strcmp0 (text, ".nan") == 0)
		d = NAN;
	else
	{
		/* reject what strtod reads but YAML does not (hex, "inf", "nan") */
		if (strpbrk (text, "xXiInN") != NULL)
			return FALSE;
		d = g_ascii_strtod (text, &end);
		if (*end != '\0')
			return FALSE;
	}
	*as_double = d;
	*as_int = (gint64) d;
	*is_int = FALSE;

	return TRUE;
}

gboolean
lrg_editor_document_get_value (LrgEditorDocument *self,
                               const gchar       *path,
                               GValue            *value)
{
	YamlNode *node;
	const gchar *text;
	YamlScalarStyle style;
	gboolean b;
	gint64 i;
	gdouble d;
	gboolean is_int;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	node = lookup (self, path);
	if (node == NULL)
		return FALSE;

	if (yaml_node_get_node_type (node) == YAML_NODE_NULL)
	{
		g_value_init (value, G_TYPE_POINTER);
		g_value_set_pointer (value, NULL);
		return TRUE;
	}
	if (yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;

	text = yaml_node_get_scalar (node);
	style = yaml_node_get_scalar_style (node);

	/* only plain (or programmatic, style-less) scalars carry a type */
	if (style == YAML_SCALAR_STYLE_PLAIN || style == YAML_SCALAR_STYLE_ANY)
	{
		if (parse_bool_word (text, &b))
		{
			g_value_init (value, G_TYPE_BOOLEAN);
			g_value_set_boolean (value, b);
			return TRUE;
		}
		if (parse_number (text, &i, &d, &is_int))
		{
			if (is_int)
			{
				g_value_init (value, G_TYPE_INT64);
				g_value_set_int64 (value, i);
			}
			else
			{
				g_value_init (value, G_TYPE_DOUBLE);
				g_value_set_double (value, d);
			}
			return TRUE;
		}
	}

	g_value_init (value, G_TYPE_STRING);
	g_value_set_string (value, text);

	return TRUE;
}

GStrv
lrg_editor_document_get_keys (LrgEditorDocument *self,
                              const gchar       *path)
{
	g_autoptr(GStrvBuilder) builder = NULL;
	YamlNode *node;
	YamlMapping *map;
	guint i;
	guint n;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	node = lookup (self, path);
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return NULL;

	map = yaml_node_get_mapping (node);
	builder = g_strv_builder_new ();
	n = yaml_mapping_get_size (map);
	for (i = 0; i < n; i++)
		g_strv_builder_add (builder, yaml_mapping_get_key (map, i));

	return g_strv_builder_end (builder);
}

gint
lrg_editor_document_get_length (LrgEditorDocument *self,
                                const gchar       *path)
{
	YamlNode *node;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), -1);
	g_return_val_if_fail (path != NULL, -1);

	node = lookup (self, path);
	if (node == NULL)
		return -1;
	if (yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
		return (gint) yaml_sequence_get_length (yaml_node_get_sequence (node));
	if (yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
		return (gint) yaml_mapping_get_size (yaml_node_get_mapping (node));

	return -1;
}

gchar *
lrg_editor_document_get_yaml (LrgEditorDocument *self,
                              const gchar       *path)
{
	YamlNode *node;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	node = lookup (self, path);
	if (node == NULL)
		return NULL;

	return serialize_node (node, NULL);
}

gchar *
lrg_editor_document_to_data (LrgEditorDocument *self,
                             gsize             *length)
{
	const gchar *text;
	gsize len;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	text = document_text (self, &len);
	if (length != NULL)
		*length = len;

	return g_strndup (text, len);
}

gboolean
lrg_editor_document_get_line (LrgEditorDocument *self,
                              const gchar       *path,
                              guint             *line,
                              guint             *column)
{
	g_auto(GStrv) tokens = NULL;
	g_autofree gchar *normal = NULL;
	GPtrArray *entries;
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	/* normalize ("/" -> "") so lookups match the recorded paths */
	tokens = lrg_editor_document_path_split (path, NULL);
	if (tokens == NULL)
		return FALSE;
	normal = tokens_to_path (tokens, g_strv_length (tokens));

	entries = line_index (self);
	for (i = 0; i < entries->len; i++)
	{
		LineEntry *entry = (LineEntry *) g_ptr_array_index (entries, i);

		if (g_strcmp0 (entry->path, normal) == 0)
		{
			if (line != NULL)
				*line = entry->line;
			if (column != NULL)
				*column = entry->column;
			return TRUE;
		}
	}

	return FALSE;
}

gchar *
lrg_editor_document_get_path_at_line (LrgEditorDocument *self,
                                      guint              line)
{
	GPtrArray *entries;
	const gchar *best;
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	/* entries are in document order: the last one starting at or before
	 * @line is the deepest node covering it */
	entries = line_index (self);
	best = "";
	for (i = 0; i < entries->len; i++)
	{
		LineEntry *entry = (LineEntry *) g_ptr_array_index (entries, i);

		if (entry->line > line)
			break;
		best = entry->path;
	}

	return g_strdup (best);
}

/* ==========================================================================
 * Editing
 * ========================================================================== */

gboolean
lrg_editor_document_set_string (LrgEditorDocument  *self,
                                const gchar        *path,
                                const gchar        *value,
                                guint               gesture_id,
                                GError            **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	/* style ANY: the writer quotes it when plain would change its type */
	return put_node (self, path, yaml_node_new_string (value), gesture_id, "Set", error);
}

gboolean
lrg_editor_document_set_int (LrgEditorDocument  *self,
                             const gchar        *path,
                             gint64              value,
                             guint               gesture_id,
                             GError            **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	return put_node (self, path, yaml_node_new_int (value), gesture_id, "Set", error);
}

/*
 * Formats a double that reads back as the same double *and* as a float
 * (yaml-glib's own "%g" keeps six digits and is locale dependent).
 */
static gchar *
format_double (gdouble value)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
	gint precision;

	if (isnan (value))
		return g_strdup (".nan");
	if (isinf (value))
		return g_strdup (value > 0 ? ".inf" : "-.inf");

	for (precision = 15; precision <= 17; precision++)
	{
		gchar format[8];

		g_snprintf (format, sizeof format, "%%.%dg", precision);
		g_ascii_formatd (buf, sizeof buf, format, value);
		if (g_ascii_strtod (buf, NULL) == value)
			break;
	}
	if (strpbrk (buf, ".eE") == NULL)
		return g_strconcat (buf, ".0", NULL);

	return g_strdup (buf);
}

gboolean
lrg_editor_document_set_double (LrgEditorDocument  *self,
                                const gchar        *path,
                                gdouble             value,
                                guint               gesture_id,
                                GError            **error)
{
	g_autofree gchar *text = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = format_double (value);

	return put_node (self, path, yaml_node_new_scalar (text, YAML_SCALAR_STYLE_PLAIN),
	                 gesture_id, "Set", error);
}

gboolean
lrg_editor_document_set_boolean (LrgEditorDocument  *self,
                                 const gchar        *path,
                                 gboolean            value,
                                 guint               gesture_id,
                                 GError            **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	return put_node (self, path, yaml_node_new_scalar (value ? "true" : "false", YAML_SCALAR_STYLE_PLAIN),
	                 gesture_id, "Set", error);
}

gboolean
lrg_editor_document_set_null (LrgEditorDocument  *self,
                              const gchar        *path,
                              guint               gesture_id,
                              GError            **error)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	return put_node (self, path, yaml_node_new_null (), gesture_id, "Clear", error);
}

gboolean
lrg_editor_document_set_value (LrgEditorDocument  *self,
                               const gchar        *path,
                               const GValue       *value,
                               guint               gesture_id,
                               GError            **error)
{
	GType type;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (value == NULL || !G_IS_VALUE (value))
		return lrg_editor_document_set_null (self, path, gesture_id, error);

	type = G_VALUE_TYPE (value);
	if (G_VALUE_HOLDS_ENUM (value))
	{
		GEnumClass *klass = (GEnumClass *) g_type_class_ref (type);
		GEnumValue *ev = g_enum_get_value (klass, g_value_get_enum (value));
		gboolean ok;

		if (ev != NULL)
			ok = lrg_editor_document_set_string (self, path, ev->value_nick, gesture_id, error);
		else
			ok = lrg_editor_document_set_int (self, path, g_value_get_enum (value), gesture_id, error);
		g_type_class_unref (klass);
		return ok;
	}

	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_STRING:
		if (g_value_get_string (value) == NULL)
			return lrg_editor_document_set_null (self, path, gesture_id, error);
		return lrg_editor_document_set_string (self, path, g_value_get_string (value), gesture_id, error);
	case G_TYPE_BOOLEAN:
		return lrg_editor_document_set_boolean (self, path, g_value_get_boolean (value), gesture_id, error);
	case G_TYPE_CHAR:
		return lrg_editor_document_set_int (self, path, g_value_get_schar (value), gesture_id, error);
	case G_TYPE_UCHAR:
		return lrg_editor_document_set_int (self, path, g_value_get_uchar (value), gesture_id, error);
	case G_TYPE_INT:
		return lrg_editor_document_set_int (self, path, g_value_get_int (value), gesture_id, error);
	case G_TYPE_UINT:
		return lrg_editor_document_set_int (self, path, (gint64) g_value_get_uint (value), gesture_id, error);
	case G_TYPE_LONG:
		return lrg_editor_document_set_int (self, path, g_value_get_long (value), gesture_id, error);
	case G_TYPE_ULONG:
		return lrg_editor_document_set_int (self, path, (gint64) g_value_get_ulong (value), gesture_id, error);
	case G_TYPE_INT64:
		return lrg_editor_document_set_int (self, path, g_value_get_int64 (value), gesture_id, error);
	case G_TYPE_UINT64:
		return lrg_editor_document_set_int (self, path, (gint64) g_value_get_uint64 (value), gesture_id, error);
	case G_TYPE_FLOAT:
		return lrg_editor_document_set_double (self, path, (gdouble) g_value_get_float (value), gesture_id, error);
	case G_TYPE_DOUBLE:
		return lrg_editor_document_set_double (self, path, g_value_get_double (value), gesture_id, error);
	case G_TYPE_POINTER:
		if (g_value_get_pointer (value) == NULL)
			return lrg_editor_document_set_null (self, path, gesture_id, error);
		break;
	default:
		break;
	}

	g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
	             "cannot write a %s value as YAML", g_type_name (type));

	return FALSE;
}

gboolean
lrg_editor_document_replace (LrgEditorDocument  *self,
                             const gchar        *path,
                             const gchar        *yaml,
                             GError            **error)
{
	YamlNode *node;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (yaml != NULL, FALSE);

	if (!check_writable (self, error))
		return FALSE;
	node = parse_text (yaml, -1, error);
	if (node == NULL)
		return FALSE;

	return put_node (self, path, node, 0, "Replace", error);
}

gboolean
lrg_editor_document_replace_gesture (LrgEditorDocument  *self,
                                     const gchar        *path,
                                     const gchar        *yaml,
                                     guint               gesture_id,
                                     GError            **error)
{
	YamlNode *node;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (yaml != NULL, FALSE);

	if (!check_writable (self, error))
		return FALSE;
	node = parse_text (yaml, -1, error);
	if (node == NULL)
		return FALSE;

	/* put_node merges a replace of the same path in the same gesture */
	return put_node (self, path, node, gesture_id, "Replace", error);
}

gboolean
lrg_editor_document_insert_item (LrgEditorDocument  *self,
                                 const gchar        *path,
                                 gint                index,
                                 const gchar        *yaml,
                                 GError            **error)
{
	g_autoptr(YamlNode) node = NULL;
	YamlNode *container;
	guint length;
	guint at;
	Cmd *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (!check_writable (self, error))
		return FALSE;
	container = resolve_container (self, path, YAML_NODE_SEQUENCE, error);
	if (container == NULL)
		return FALSE;
	node = yaml != NULL ? parse_text (yaml, -1, error) : yaml_node_new_null ();
	if (node == NULL)
		return FALSE;

	length = yaml_sequence_get_length (yaml_node_get_sequence (container));
	at = (index < 0 || (guint) index > length) ? length : (guint) index;
	if (index >= 0 && (guint) index > length)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		             "index %d is past the end of a %u-item sequence", index, length);
		return FALSE;
	}

	cmd = cmd_new (CMD_INSERT, NULL);
	{
		g_auto(GStrv) tokens = lrg_editor_document_path_split (path, NULL);
		g_autofree gchar *idx = g_strdup_printf ("%u", at);

		cmd->container = tokens_to_path (tokens, g_strv_length (tokens));
		cmd->target = lrg_editor_document_path_join (cmd->container, idx);
	}
	cmd->index = at;
	cmd->new_node = g_steal_pointer (&node);
	cmd->label = g_strdup_printf ("Insert %s", cmd->target);
	cmd_apply (self, cmd, TRUE);
	record (self, cmd);

	return TRUE;
}

gboolean
lrg_editor_document_move_item (LrgEditorDocument  *self,
                               const gchar        *path,
                               guint               from,
                               guint               to,
                               GError            **error)
{
	YamlNode *container;
	guint length;
	Cmd *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (!check_writable (self, error))
		return FALSE;
	container = resolve_container (self, path, YAML_NODE_SEQUENCE, error);
	if (container == NULL)
		return FALSE;
	length = yaml_sequence_get_length (yaml_node_get_sequence (container));
	if (from >= length || to >= length)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		             "cannot move item %u to %u in a %u-item sequence", from, to, length);
		return FALSE;
	}
	if (from == to)
		return TRUE;

	cmd = cmd_new (CMD_MOVE, NULL);
	{
		g_auto(GStrv) tokens = lrg_editor_document_path_split (path, NULL);

		cmd->container = tokens_to_path (tokens, g_strv_length (tokens));
	}
	cmd->target = g_strdup (cmd->container);
	cmd->index = from;
	cmd->index2 = to;
	cmd->label = g_strdup_printf ("Move %s/%u", cmd->container, from);
	cmd_apply (self, cmd, TRUE);
	record (self, cmd);

	return TRUE;
}

gboolean
lrg_editor_document_add_key (LrgEditorDocument  *self,
                             const gchar        *path,
                             const gchar        *key,
                             const gchar        *yaml,
                             gint                position,
                             GError            **error)
{
	g_autoptr(YamlNode) node = NULL;
	YamlNode *container;
	YamlMapping *map;
	guint size;
	Cmd *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	if (!check_writable (self, error))
		return FALSE;
	container = resolve_container (self, path, YAML_NODE_MAPPING, error);
	if (container == NULL)
		return FALSE;
	map = yaml_node_get_mapping (container);
	if (yaml_mapping_has_member (map, key))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS,
		             "key '%s' already exists", key);
		return FALSE;
	}
	node = yaml != NULL ? parse_text (yaml, -1, error) : yaml_node_new_null ();
	if (node == NULL)
		return FALSE;

	size = yaml_mapping_get_size (map);
	cmd = cmd_new (CMD_INSERT, NULL);
	{
		g_auto(GStrv) tokens = lrg_editor_document_path_split (path, NULL);

		cmd->container = tokens_to_path (tokens, g_strv_length (tokens));
	}
	cmd->in_map = TRUE;
	cmd->key = g_strdup (key);
	cmd->index = (position < 0 || (guint) position > size) ? size : (guint) position;
	cmd->target = lrg_editor_document_path_join (cmd->container, key);
	cmd->new_node = g_steal_pointer (&node);
	cmd->label = g_strdup_printf ("Add %s", cmd->target);
	cmd_apply (self, cmd, TRUE);
	record (self, cmd);

	return TRUE;
}

gboolean
lrg_editor_document_rename_key (LrgEditorDocument  *self,
                                const gchar        *path,
                                const gchar        *old_key,
                                const gchar        *new_key,
                                GError            **error)
{
	YamlNode *container;
	YamlMapping *map;
	Cmd *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (old_key != NULL, FALSE);
	g_return_val_if_fail (new_key != NULL, FALSE);

	if (!check_writable (self, error))
		return FALSE;
	container = resolve_container (self, path, YAML_NODE_MAPPING, error);
	if (container == NULL)
		return FALSE;
	map = yaml_node_get_mapping (container);
	if (!yaml_mapping_has_member (map, old_key))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
		             "no key '%s'", old_key);
		return FALSE;
	}
	if (g_strcmp0 (old_key, new_key) == 0)
		return TRUE;
	if (yaml_mapping_has_member (map, new_key))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_EXISTS,
		             "key '%s' already exists", new_key);
		return FALSE;
	}

	cmd = cmd_new (CMD_RENAME, NULL);
	{
		g_auto(GStrv) tokens = lrg_editor_document_path_split (path, NULL);

		cmd->container = tokens_to_path (tokens, g_strv_length (tokens));
	}
	cmd->in_map = TRUE;
	cmd->key = g_strdup (old_key);
	cmd->key2 = g_strdup (new_key);
	cmd->target = lrg_editor_document_path_join (cmd->container, new_key);
	cmd->label = g_strdup_printf ("Rename %s", cmd->target);
	cmd_apply (self, cmd, TRUE);
	record (self, cmd);

	return TRUE;
}

gboolean
lrg_editor_document_remove (LrgEditorDocument  *self,
                            const gchar        *path,
                            GError            **error)
{
	g_auto(GStrv) tokens = NULL;
	YamlNode *parent;
	const gchar *last;
	guint n;
	Cmd *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (!check_writable (self, error))
		return FALSE;
	tokens = lrg_editor_document_path_split (path, error);
	if (tokens == NULL)
		return FALSE;
	n = g_strv_length (tokens);
	if (n == 0)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
		             "the root cannot be removed; replace it instead");
		return FALSE;
	}
	parent = resolve_tokens (self->root, tokens, n - 1, error);
	if (parent == NULL)
		return FALSE;
	last = tokens[n - 1];

	cmd = cmd_new (CMD_REMOVE, NULL);
	if (yaml_node_get_node_type (parent) == YAML_NODE_MAPPING)
	{
		YamlMapping *map = yaml_node_get_mapping (parent);
		YamlNode *node = yaml_mapping_get_member (map, last);

		if (node == NULL)
		{
			g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
			             "no node at '%s'", path);
			cmd_free (cmd);
			return FALSE;
		}
		cmd->in_map = TRUE;
		cmd->key = g_strdup (last);
		cmd->index = map_position (map, last);
		cmd->old_node = yaml_node_ref (node);
	}
	else if (yaml_node_get_node_type (parent) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (parent);
		guint index;

		if (!parse_index (last, &index) || index >= yaml_sequence_get_length (seq))
		{
			g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
			             "no node at '%s'", path);
			cmd_free (cmd);
			return FALSE;
		}
		cmd->index = index;
		cmd->old_node = yaml_node_ref (yaml_sequence_get_element (seq, index));
	}
	else
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "no node at '%s': its parent is a scalar", path);
		cmd_free (cmd);
		return FALSE;
	}

	cmd->container = tokens_to_path (tokens, n - 1);
	cmd->target = tokens_to_path (tokens, n);
	cmd->label = g_strdup_printf ("Remove %s", cmd->target);
	cmd_apply (self, cmd, TRUE);
	record (self, cmd);

	return TRUE;
}

/* ==========================================================================
 * Undo
 * ========================================================================== */

void
lrg_editor_document_begin_group (LrgEditorDocument *self,
                                 const gchar       *label)
{
	Cmd *group;

	g_return_if_fail (LRG_IS_EDITOR_DOCUMENT (self));

	group = cmd_new (CMD_GROUP, label != NULL ? label : "Edit");
	g_ptr_array_add (self->groups, group);

	/* a gesture never merges across a group boundary */
	self->merge_candidate = NULL;
}

void
lrg_editor_document_end_group (LrgEditorDocument *self)
{
	Cmd *group;

	g_return_if_fail (LRG_IS_EDITOR_DOCUMENT (self));
	g_return_if_fail (self->groups->len > 0);

	group = (Cmd *) g_ptr_array_steal_index (self->groups, self->groups->len - 1);
	self->merge_candidate = NULL;

	if (group->children->len == 0)
	{
		cmd_free (group);
		update_state (self);
		return;
	}

	if (self->groups->len > 0)
	{
		/* nested: becomes one child of the enclosing group */
		Cmd *outer = (Cmd *) g_ptr_array_index (self->groups, self->groups->len - 1);

		g_ptr_array_add (outer->children, group);
	}
	else
	{
		group->serial = ++self->next_serial;
		g_ptr_array_add (self->undo_stack, group);
		enforce_limit (self);
	}
	update_state (self);
}

gboolean
lrg_editor_document_can_undo (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	return self->groups->len == 0 && self->undo_stack->len > 0;
}

gboolean
lrg_editor_document_can_redo (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	return self->groups->len == 0 && self->redo_stack->len > 0;
}

gboolean
lrg_editor_document_undo (LrgEditorDocument *self)
{
	Cmd *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	if (!lrg_editor_document_can_undo (self))
		return FALSE;

	cmd = (Cmd *) g_ptr_array_steal_index (self->undo_stack, self->undo_stack->len - 1);
	self->merge_candidate = NULL;
	cmd_apply (self, cmd, FALSE);
	g_ptr_array_add (self->redo_stack, cmd);
	update_state (self);

	return TRUE;
}

gboolean
lrg_editor_document_redo (LrgEditorDocument *self)
{
	Cmd *cmd;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	if (!lrg_editor_document_can_redo (self))
		return FALSE;

	cmd = (Cmd *) g_ptr_array_steal_index (self->redo_stack, self->redo_stack->len - 1);
	self->merge_candidate = NULL;
	cmd_apply (self, cmd, TRUE);
	g_ptr_array_add (self->undo_stack, cmd);
	enforce_limit (self);
	update_state (self);

	return TRUE;
}

const gchar *
lrg_editor_document_get_undo_label (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	if (!lrg_editor_document_can_undo (self))
		return NULL;

	return ((Cmd *) g_ptr_array_index (self->undo_stack, self->undo_stack->len - 1))->label;
}

const gchar *
lrg_editor_document_get_redo_label (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	if (!lrg_editor_document_can_redo (self))
		return NULL;

	return ((Cmd *) g_ptr_array_index (self->redo_stack, self->redo_stack->len - 1))->label;
}

void
lrg_editor_document_clear_undo (LrgEditorDocument *self)
{
	g_return_if_fail (LRG_IS_EDITOR_DOCUMENT (self));

	/* the current state becomes the empty-stack state, so dirty is kept */
	self->base_serial = current_serial (self);
	g_ptr_array_set_size (self->undo_stack, 0);
	g_ptr_array_set_size (self->redo_stack, 0);
	self->merge_candidate = NULL;
	update_state (self);
}

guint
lrg_editor_document_get_undo_limit (LrgEditorDocument *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), 0);

	return self->undo_limit;
}

void
lrg_editor_document_set_undo_limit (LrgEditorDocument *self,
                                    guint              limit)
{
	g_return_if_fail (LRG_IS_EDITOR_DOCUMENT (self));

	if (self->undo_limit == limit)
		return;
	self->undo_limit = limit;
	enforce_limit (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNDO_LIMIT]);
	update_state (self);
}

/* ==========================================================================
 * Files
 * ========================================================================== */

gboolean
lrg_editor_document_save_as (LrgEditorDocument  *self,
                             const gchar        *path,
                             GError            **error)
{
	g_autoptr(GError) local = NULL;
	g_autofree gchar *dir = NULL;
	const gchar *text;
	gsize length;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (!check_writable (self, error))
		return FALSE;
	if (self->groups->len > 0)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_GROUP_OPEN,
		             "cannot save while an edit group is open");
		return FALSE;
	}

	text = file_text (self, &length);
	dir = g_path_get_dirname (path);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_IO,
		             "cannot create %s: %s", dir, g_strerror (errno));
		return FALSE;
	}
	/* atomic: a crash mid-write never leaves a truncated data file */
	if (!g_file_set_contents (path, text, (gssize) length, &local))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_IO,
		             "%s", local->message);
		return FALSE;
	}

	set_path_internal (self, path);
	remember_disk (self, text);

	/* the saved state is clean and an undo point: no merging across it */
	self->clean_serial = current_serial (self);
	self->merge_candidate = NULL;

	/* duplicates found at load time are gone from the file now */
	g_ptr_array_set_size (self->load_issues, 0);
	update_state (self);
	g_signal_emit (self, signals[SIGNAL_SAVED], 0, self->path);

	return TRUE;
}

gboolean
lrg_editor_document_save (LrgEditorDocument  *self,
                          GError            **error)
{
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	if (self->path == NULL)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NO_PATH,
		             "%s has no file yet; use save-as", self->display_name);
		return FALSE;
	}
	path = g_strdup (self->path);

	return lrg_editor_document_save_as (self, path, error);
}

gboolean
lrg_editor_document_reload (LrgEditorDocument  *self,
                            GError            **error)
{
	g_autoptr(GError) local = NULL;
	g_autofree gchar *text = NULL;
	gsize length;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	if (self->path == NULL)
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_NO_PATH,
		             "%s has no file to reload", self->display_name);
		return FALSE;
	}
	if (!g_file_get_contents (self->path, &text, &length, &local))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_IO,
		             "%s", local->message);
		return FALSE;
	}
	if (!load_text (self, text, length, &local))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_PARSE,
		             "%s: %s", self->display_name, local->message);
		return FALSE;
	}

	remember_disk (self, text);
	reset_history (self);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0, "");
	g_signal_emit (self, signals[SIGNAL_RELOADED], 0);

	return TRUE;
}

gboolean
lrg_editor_document_check_external_change (LrgEditorDocument *self)
{
	g_autofree gchar *text = NULL;
	gint64 mtime;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	if (self->path == NULL)
		return FALSE;
	if (self->external_changed)
		return TRUE;

	mtime = file_mtime (self->path);
	if (mtime == self->disk_mtime && mtime != -1)
		return FALSE;

	/*
	 * The time moved (or the file went away): compare the content, so a
	 * touch or our own atomic rename does not count as a change.
	 */
	if (mtime != -1 && g_file_get_contents (self->path, &text, NULL, NULL) &&
	    g_strcmp0 (text, self->disk_text) == 0)
	{
		self->disk_mtime = mtime;
		return FALSE;
	}

	self->external_changed = TRUE;
	g_signal_emit (self, signals[SIGNAL_FILE_CHANGED], 0);

	return TRUE;
}

/* ==========================================================================
 * Validation: shared helpers
 * ========================================================================== */

typedef struct
{
	LrgEditorDocument *self;
	GPtrArray         *issues;
	const gchar       *text;     /* the serialized text being validated */
	gsize              length;
} Validation;

/* adds an issue; fills the line from the index when the path is known */
static void add_issue (Validation               *v,
                       LrgEditorProblemSeverity  severity,
                       const gchar              *path,
                       const gchar              *format,
                       ...) G_GNUC_PRINTF (4, 5);

static void
add_issue (Validation               *v,
           LrgEditorProblemSeverity  severity,
           const gchar              *path,
           const gchar              *format,
           ...)
{
	g_autofree gchar *msg = NULL;
	guint line;
	guint column;
	va_list args;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);

	line = 0;
	column = 0;
	if (path != NULL)
		lrg_editor_document_get_line (v->self, path, &line, &column);

	g_ptr_array_add (v->issues, lrg_editor_document_issue_new (severity, path, line, column, msg));
}

/* number of error-severity issues so far */
static guint
count_errors (Validation *v)
{
	guint i;
	guint n;

	n = 0;
	for (i = 0; i < v->issues->len; i++)
	{
		if (((LrgEditorDocumentIssue *) g_ptr_array_index (v->issues, i))->severity ==
		    LRG_EDITOR_PROBLEM_ERROR)
			n++;
	}

	return n;
}

/* replaces every @needle in @haystack */
static gchar *
replace_all (const gchar *haystack,
             const gchar *needle,
             const gchar *replacement)
{
	g_auto(GStrv) parts = NULL;

	if (needle == NULL || *needle == '\0')
		return g_strdup (haystack);
	parts = g_strsplit (haystack, needle, -1);

	return g_strjoinv (replacement, parts);
}

/*
 * Turns a loader's location prefix into a YAML path: "children[2].text"
 * (UI loader), "params.fill-percent" (dungeon), "(root)".  Returns NULL
 * when the prefix is not a location or does not exist in the document.
 */
static gchar *
message_path (LrgEditorDocument *self,
              const gchar       *message)
{
	g_autofree gchar *prefix = NULL;
	g_auto(GStrv) parts = NULL;
	const gchar *colon;
	GString *path;
	guint i;

	colon = strstr (message, ": ");
	if (colon == NULL)
		return NULL;
	prefix = g_strndup (message, (gsize) (colon - message));
	if (g_strcmp0 (prefix, "(root)") == 0)
		return g_strdup ("");
	if (strpbrk (prefix, " '\"") != NULL)
		return NULL;

	/* "a.b[2].c" -> "/a/b/2/c" */
	path = g_string_new (NULL);
	parts = g_strsplit (prefix, ".", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		gchar *bracket = strchr (parts[i], '[');
		g_autofree gchar *name = g_strndup (parts[i], bracket != NULL ? (gsize) (bracket - parts[i])
		                                                              : strlen (parts[i]));
		g_autofree gchar *escaped = lrg_editor_document_path_escape (name);

		if (*name != '\0')
		{
			g_string_append_c (path, '/');
			g_string_append (path, escaped);
		}
		while (bracket != NULL)
		{
			gchar *close = strchr (bracket, ']');

			if (close == NULL)
				break;
			g_string_append_c (path, '/');
			g_string_append_len (path, bracket + 1, close - bracket - 1);
			bracket = strchr (close, '[');
		}
	}

	if (!lrg_editor_document_has_path (self, path->str))
	{
		g_string_free (path, TRUE);
		return NULL;
	}

	return g_string_free (path, FALSE);
}

/* pulls "line N, column M" or ":N:M)" out of a loader message */
static gboolean
message_position (const gchar *message,
                  guint       *line,
                  guint       *column)
{
	static const gchar * const patterns[] = {
		"line (\\d+), column (\\d+)",
		":(\\d+):(\\d+)\\)",
		NULL
	};
	guint i;

	for (i = 0; patterns[i] != NULL; i++)
	{
		g_autoptr(GRegex) regex = g_regex_new (patterns[i], G_REGEX_DEFAULT, G_REGEX_MATCH_DEFAULT, NULL);
		g_autoptr(GMatchInfo) match = NULL;

		if (regex != NULL && g_regex_match (regex, message, G_REGEX_MATCH_DEFAULT, &match))
		{
			g_autofree gchar *l = g_match_info_fetch (match, 1);
			g_autofree gchar *c = g_match_info_fetch (match, 2);

			*line = (guint) g_ascii_strtoull (l, NULL, 10);
			*column = (guint) g_ascii_strtoull (c, NULL, 10);
			return TRUE;
		}
	}

	return FALSE;
}

/*
 * The source name the engine's from-data loaders (input map, locale, sound
 * bank, HUD layout, dialog tree) put in their messages: replaced by the
 * document's display name, as a temp file's path used to be.
 */
#define DATA_SOURCE "<data>"

/* a loader's GError, as an error issue placed as well as possible */
static void
add_loader_error (Validation   *v,
                  const GError *error,
                  const gchar  *temp_path)
{
	g_autofree gchar *msg = NULL;
	g_autofree gchar *path = NULL;
	guint line;
	guint column;

	/* the temp file name means nothing to the user: show ours */
	msg = temp_path != NULL ? replace_all (error->message, temp_path, v->self->display_name)
	                        : g_strdup (error->message);
	path = message_path (v->self, msg);

	line = 0;
	column = 0;
	if (message_position (msg, &line, &column))
	{
		if (path == NULL)
			path = lrg_editor_document_get_path_at_line (v->self, line);
	}
	else if (path != NULL)
	{
		lrg_editor_document_get_line (v->self, path, &line, &column);
	}

	g_ptr_array_add (v->issues,
	                 lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_ERROR, path, line, column, msg));
}

/* log capture: loader warnings become warning issues while a loader runs */
typedef struct
{
	Validation *v;
	gboolean    keep;
	const gchar *temp_path;
} Capture;

static void
capture_log (const gchar    *domain,
             GLogLevelFlags  level,
             const gchar    *message,
             gpointer        user_data)
{
	Capture *capture = (Capture *) user_data;
	g_autofree gchar *msg = NULL;

	(void) domain;
	(void) level;

	if (!capture->keep)
		return;
	msg = capture->temp_path != NULL
	      ? replace_all (message, capture->temp_path, capture->v->self->display_name)
	      : g_strdup (message);
	g_ptr_array_add (capture->v->issues,
	                 lrg_editor_document_issue_new (LRG_EDITOR_PROBLEM_WARNING, NULL, 0, 0, msg));
}

/* the mapping at @path, or NULL */
static YamlMapping *
mapping_at (LrgEditorDocument *self,
            const gchar       *path)
{
	YamlNode *node = lookup (self, path);

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return NULL;

	return yaml_node_get_mapping (node);
}

/* the sequence at @path, or NULL */
static YamlSequence *
sequence_at (LrgEditorDocument *self,
             const gchar       *path)
{
	YamlNode *node = lookup (self, path);

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return NULL;

	return yaml_node_get_sequence (node);
}

/* the scalar text of @key in @map, or NULL */
static const gchar *
member_text (YamlMapping *map,
             const gchar *key)
{
	YamlNode *node;

	if (map == NULL)
		return NULL;
	node = yaml_mapping_get_member (map, key);
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;

	return yaml_node_get_scalar (node);
}

/* ==========================================================================
 * Validation: input maps
 * ========================================================================== */

static const gchar * const binding_types[] = {
	"keyboard", "mouse_button", "gamepad_button", "gamepad_axis", NULL
};

/* whether the loader turns this YAML binding into a binding */
static gboolean
binding_is_parsed (YamlNode *node)
{
	const gchar *type;
	g_autofree gchar *lower = NULL;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return FALSE;
	type = member_text (yaml_node_get_mapping (node), "type");
	if (type == NULL)
		return FALSE;
	lower = g_ascii_strdown (type, -1);

	return g_strv_contains (binding_types, lower);
}

/* a canonical text for a binding, to find the same input bound twice */
static gchar *
binding_signature (YamlMapping *binding)
{
	g_autofree gchar *type = g_ascii_strdown (member_text (binding, "type"), -1);
	g_autofree gchar *mods = NULL;
	YamlNode *mods_node;
	const gchar *gamepad;

	/* modifiers, upper-cased and sorted, so [ctrl, shift] == [SHIFT, CTRL] */
	mods_node = yaml_mapping_get_member (binding, "modifiers");
	if (mods_node != NULL && yaml_node_get_node_type (mods_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (mods_node);
		g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func (g_free);
		guint i;

		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			const gchar *m = yaml_node_get_scalar (yaml_sequence_get_element (seq, i));

			if (m != NULL)
				g_ptr_array_add (names, g_ascii_strup (g_strcmp0 (m, "control") == 0 ? "ctrl" : m, -1));
		}
		g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);
		g_ptr_array_add (names, NULL);
		mods = g_strjoinv ("+", (gchar **) names->pdata);
	}

	gamepad = member_text (binding, "gamepad");
	if (g_strcmp0 (type, "keyboard") == 0)
		return g_strdup_printf ("key %s%s%s", mods != NULL && *mods != '\0' ? mods : "",
		                        mods != NULL && *mods != '\0' ? "+" : "",
		                        member_text (binding, "key") != NULL ? member_text (binding, "key") : "?");
	if (g_strcmp0 (type, "mouse_button") == 0)
		return g_strdup_printf ("mouse %s%s%s", mods != NULL && *mods != '\0' ? mods : "",
		                        mods != NULL && *mods != '\0' ? "+" : "",
		                        member_text (binding, "button") != NULL ? member_text (binding, "button") : "?");
	if (g_strcmp0 (type, "gamepad_button") == 0)
		return g_strdup_printf ("gamepad %s button %s", gamepad != NULL ? gamepad : "0",
		                        member_text (binding, "button") != NULL ? member_text (binding, "button") : "?");

	return g_strdup_printf ("gamepad %s axis %s %s", gamepad != NULL ? gamepad : "0",
	                        member_text (binding, "axis") != NULL ? member_text (binding, "axis") : "?",
	                        g_strcmp0 (member_text (binding, "positive"), "true") == 0 ? "+" : "-");
}

static void
validate_input_map (Validation *v)
{
	g_autoptr(LrgInputMap) map = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) seen = NULL;
	YamlMapping *actions;
	Capture capture;
	guint handler;
	guint i;

	/* --- the engine loader, on the text itself --- */
	map = lrg_input_map_new ();

	/* the structural checks below report everything the loader warns
	 * about, with paths, so its warnings are swallowed */
	capture.v = v;
	capture.keep = FALSE;
	capture.temp_path = DATA_SOURCE;
	handler = g_log_set_handler (LRG_LOG_DOMAIN_INPUT, G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
	                             capture_log, &capture);
	if (!lrg_input_map_load_from_data (map, v->text, (gssize) v->length, &error))
	{
		add_loader_error (v, error, DATA_SOURCE);
		g_clear_object (&map);
	}
	g_log_remove_handler (LRG_LOG_DOMAIN_INPUT, handler);

	/* --- structure --- */
	actions = mapping_at (v->self, "/actions");
	if (actions == NULL)
		return;

	seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	for (i = 0; i < yaml_mapping_get_size (actions); i++)
	{
		const gchar *name = yaml_mapping_get_key (actions, i);
		YamlNode *action = yaml_mapping_get_value (actions, i);
		g_autofree gchar *action_path = lrg_editor_document_path_join ("/actions", name);
		g_autofree gchar *bindings_path = NULL;
		LrgInputAction *loaded;
		YamlSequence *bindings;
		guint parsed;
		guint j;

		if (yaml_node_get_node_type (action) != YAML_NODE_MAPPING)
		{
			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, action_path,
			           "action '%s' is not a mapping; the loader skips it", name);
			continue;
		}
		bindings_path = lrg_editor_document_path_join (action_path, "bindings");
		bindings = sequence_at (v->self, bindings_path);
		if (bindings == NULL || yaml_sequence_get_length (bindings) == 0)
		{
			add_issue (v, LRG_EDITOR_PROBLEM_WARNING, action_path, "action '%s' has no bindings", name);
			continue;
		}

		loaded = map != NULL ? lrg_input_map_get_action (map, name) : NULL;
		parsed = 0;
		for (j = 0; j < yaml_sequence_get_length (bindings); j++)
		{
			YamlNode *node = yaml_sequence_get_element (bindings, j);
			g_autofree gchar *idx = g_strdup_printf ("%u", j);
			g_autofree gchar *bpath = lrg_editor_document_path_join (bindings_path, idx);
			g_autofree gchar *lower = NULL;
			g_autofree gchar *signature = NULL;
			YamlMapping *binding;
			const gchar *type;
			const gchar *first;

			if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
			{
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, bpath, "binding %u of '%s' is not a mapping", j, name);
				continue;
			}
			binding = yaml_node_get_mapping (node);
			type = member_text (binding, "type");
			if (type == NULL)
			{
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, bpath, "binding %u of '%s' has no type", j, name);
				continue;
			}
			lower = g_ascii_strdown (type, -1);
			if (!g_strv_contains (binding_types, lower))
			{
				g_autofree gchar *tpath = lrg_editor_document_path_join (bpath, "type");

				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, tpath,
				           "unknown binding type '%s' (keyboard, mouse_button, gamepad_button, gamepad_axis)",
				           type);
				continue;
			}

			/* --- required fields per type --- */
			if (g_strcmp0 (lower, "keyboard") == 0 && member_text (binding, "key") == NULL)
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, bpath, "keyboard binding without a key");
			else if ((g_strcmp0 (lower, "mouse_button") == 0 || g_strcmp0 (lower, "gamepad_button") == 0) &&
			         member_text (binding, "button") == NULL)
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, bpath, "%s binding without a button", lower);
			else if (g_strcmp0 (lower, "gamepad_axis") == 0 && member_text (binding, "axis") == NULL)
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, bpath, "gamepad_axis binding without an axis");

			/* --- the loader resolved key names: GRL_KEY_NULL means unknown --- */
			if (loaded != NULL && parsed < lrg_input_action_get_binding_count (loaded))
			{
				const LrgInputBinding *lb = lrg_input_action_get_binding (loaded, parsed);

				if (g_strcmp0 (lower, "keyboard") == 0 && member_text (binding, "key") != NULL &&
				    lrg_input_binding_get_key (lb) == GRL_KEY_NULL)
				{
					g_autofree gchar *kpath = lrg_editor_document_path_join (bpath, "key");

					add_issue (v, LRG_EDITOR_PROBLEM_ERROR, kpath, "unknown key name '%s'",
					           member_text (binding, "key"));
				}
			}
			if (binding_is_parsed (node))
				parsed++;

			/* --- the same input bound twice --- */
			signature = binding_signature (binding);
			first = g_hash_table_lookup (seen, signature);
			if (first != NULL)
			{
				add_issue (v, LRG_EDITOR_PROBLEM_WARNING, bpath, "%s is also bound at %s", signature, first);
			}
			else
			{
				g_hash_table_insert (seen, g_steal_pointer (&signature), g_strdup (bpath));
			}
		}
	}
}

/* ==========================================================================
 * Validation: locales
 * ========================================================================== */

static const gchar * const plural_forms[] = { "zero", "one", "two", "few", "many", "other", NULL };

/* the number of printf conversions in a translation ("%%" excluded) */
static guint
count_placeholders (const gchar *text)
{
	const gchar *p;
	guint n;

	n = 0;
	for (p = text; p != NULL && *p != '\0'; p++)
	{
		if (*p != '%')
			continue;
		if (p[1] == '%')
		{
			p++;
			continue;
		}
		if (p[1] != '\0')
			n++;
	}

	return n;
}

/* the first scalar of a string entry: itself, or its "other"/first form */
static const gchar *
entry_text (YamlNode *node)
{
	YamlMapping *map;
	const gchar *text;

	if (node == NULL)
		return NULL;
	if (yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
		return yaml_node_get_scalar (node);
	if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return NULL;
	map = yaml_node_get_mapping (node);
	text = member_text (map, "other");
	if (text == NULL && yaml_mapping_get_size (map) > 0)
		text = yaml_node_get_scalar (yaml_mapping_get_value (map, 0));

	return text;
}

static void
validate_locale (Validation *v)
{
	g_autoptr(GError) error = NULL;
	YamlNode *strings_node;
	YamlMapping *strings;
	LrgEditorDocument *ref;
	guint i;

	/* --- the engine loader, on the text itself --- */
	{
		g_autoptr(LrgLocale) locale = NULL;
		Capture capture = { v, TRUE, DATA_SOURCE };
		guint handler;

		handler = g_log_set_handler (LRG_LOG_DOMAIN_I18N, G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
		                             capture_log, &capture);
		locale = lrg_locale_new_from_data (v->text, (gssize) v->length, &error);
		g_log_remove_handler (LRG_LOG_DOMAIN_I18N, handler);
		if (locale == NULL)
			add_loader_error (v, error, DATA_SOURCE);
	}

	/* --- structure of strings --- */
	strings_node = lookup (v->self, "/strings");
	if (strings_node == NULL || yaml_node_get_node_type (strings_node) == YAML_NODE_NULL)
		return;
	if (yaml_node_get_node_type (strings_node) != YAML_NODE_MAPPING)
	{
		add_issue (v, LRG_EDITOR_PROBLEM_ERROR, "/strings",
		           "strings must be a mapping of key to text; the loader ignores it");
		return;
	}
	strings = yaml_node_get_mapping (strings_node);
	for (i = 0; i < yaml_mapping_get_size (strings); i++)
	{
		const gchar *key = yaml_mapping_get_key (strings, i);
		YamlNode *node = yaml_mapping_get_value (strings, i);
		g_autofree gchar *path = lrg_editor_document_path_join ("/strings", key);

		switch (yaml_node_get_node_type (node))
		{
		case YAML_NODE_NULL:
			add_issue (v, LRG_EDITOR_PROBLEM_WARNING, path, "'%s' has no text", key);
			break;
		case YAML_NODE_SEQUENCE:
			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, path,
			           "'%s' must be a text or a mapping of plural forms", key);
			break;
		case YAML_NODE_MAPPING:
			{
				YamlMapping *forms = yaml_node_get_mapping (node);
				guint j;

				for (j = 0; j < yaml_mapping_get_size (forms); j++)
				{
					const gchar *form = yaml_mapping_get_key (forms, j);
					g_autofree gchar *fpath = lrg_editor_document_path_join (path, form);

					if (!g_strv_contains (plural_forms, form))
						add_issue (v, LRG_EDITOR_PROBLEM_WARNING, fpath,
						           "unknown plural form '%s' (zero, one, two, few, many, other)", form);
					if (yaml_node_get_node_type (yaml_mapping_get_value (forms, j)) != YAML_NODE_SCALAR)
						add_issue (v, LRG_EDITOR_PROBLEM_WARNING, fpath,
						           "plural form '%s' of '%s' has no text", form, key);
				}
			}
			break;
		case YAML_NODE_SCALAR:
		default:
			break;
		}
	}

	/* --- against the reference locale --- */
	ref = v->self->reference;
	if (ref == NULL)
		return;
	{
		g_auto(GStrv) missing = lrg_editor_document_locale_missing_keys (v->self, ref);
		YamlMapping *ref_strings = mapping_at (ref, "/strings");

		for (i = 0; missing != NULL && missing[i] != NULL; i++)
			add_issue (v, LRG_EDITOR_PROBLEM_WARNING, "/strings", "missing key '%s' (present in %s)",
			           missing[i], ref->display_name);

		for (i = 0; i < yaml_mapping_get_size (strings); i++)
		{
			const gchar *key = yaml_mapping_get_key (strings, i);
			g_autofree gchar *path = lrg_editor_document_path_join ("/strings", key);
			YamlNode *ref_node = ref_strings != NULL ? yaml_mapping_get_member (ref_strings, key) : NULL;
			const gchar *mine;
			const gchar *theirs;

			if (ref_node == NULL)
			{
				add_issue (v, LRG_EDITOR_PROBLEM_INFO, path, "'%s' is not in %s", key, ref->display_name);
				continue;
			}
			mine = entry_text (yaml_mapping_get_value (strings, i));
			theirs = entry_text (ref_node);
			if (mine != NULL && theirs != NULL && count_placeholders (mine) != count_placeholders (theirs))
				add_issue (v, LRG_EDITOR_PROBLEM_WARNING, path,
				           "'%s' has %u placeholder(s) but %s has %u", key, count_placeholders (mine),
				           ref->display_name, count_placeholders (theirs));
		}
	}
}

/* ==========================================================================
 * Validation: sound banks
 * ========================================================================== */

/* the directory relative paths start from: base-dir, the file's dir, cwd */
static gchar *
document_base_dir (LrgEditorDocument *self)
{
	if (self->base_dir != NULL)
		return g_strdup (self->base_dir);
	if (self->path != NULL)
		return g_path_get_dirname (self->path);

	return g_get_current_dir ();
}

static void
validate_sound_bank (Validation *v)
{
	static const gchar * const extensions[] = { "wav", "ogg", "mp3", "flac", "qoa", "xm", "mod", NULL };
	g_autoptr(GError) error = NULL;
	YamlNode *sounds_node;
	YamlMapping *sounds;
	gboolean check_files;
	guint i;

	/*
	 * --- the engine loader ---
	 * Manifest only: it reads the whole manifest the engine way (root,
	 * `name`, `base_path`, `volume`, `sounds`) without decoding any audio,
	 * so it needs neither the sound files nor the audio device. The files
	 * themselves are checked below.
	 */
	if (yaml_node_get_node_type (v->self->root) == YAML_NODE_MAPPING)
	{
		g_autofree gchar *base_dir = document_base_dir (v->self);
		g_autoptr(LrgSoundBank) bank = NULL;

		bank = lrg_sound_bank_new_from_data (v->text, (gssize) v->length, base_dir, TRUE, &error);
		if (bank == NULL)
			add_loader_error (v, error, DATA_SOURCE);
	}
	else
	{
		add_issue (v, LRG_EDITOR_PROBLEM_ERROR, "", "a sound bank must be a mapping");
		return;
	}

	/* --- sounds --- */
	sounds_node = lookup (v->self, "/sounds");
	if (sounds_node == NULL || yaml_node_get_node_type (sounds_node) == YAML_NODE_NULL)
	{
		add_issue (v, LRG_EDITOR_PROBLEM_INFO, NULL, "the bank has no sounds");
		return;
	}
	if (yaml_node_get_node_type (sounds_node) != YAML_NODE_MAPPING)
	{
		add_issue (v, LRG_EDITOR_PROBLEM_ERROR, "/sounds", "sounds must be a mapping of name to file");
		return;
	}

	/* an untitled document without a base dir has nothing to resolve against */
	check_files = v->self->base_dir != NULL || v->self->path != NULL;
	sounds = yaml_node_get_mapping (sounds_node);
	for (i = 0; i < yaml_mapping_get_size (sounds); i++)
	{
		const gchar *name = yaml_mapping_get_key (sounds, i);
		YamlNode *node = yaml_mapping_get_value (sounds, i);
		g_autofree gchar *path = lrg_editor_document_path_join ("/sounds", name);
		const gchar *file;
		const gchar *dot;

		if (yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		{
			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, path, "sound '%s' must be a file name", name);
			continue;
		}
		file = yaml_node_get_scalar (node);
		dot = strrchr (file, '.');
		if (dot == NULL || !g_strv_contains (extensions, dot + 1))
			add_issue (v, LRG_EDITOR_PROBLEM_WARNING, path,
			           "'%s' is not a known audio type (wav, ogg, mp3, flac, qoa, xm, mod)", file);
		if (check_files)
		{
			g_autofree gchar *resolved = lrg_editor_document_sound_bank_resolve (v->self, name);

			if (resolved != NULL && !g_file_test (resolved, G_FILE_TEST_IS_REGULAR))
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, path, "sound file not found: %s", resolved);
		}
	}
}

/* ==========================================================================
 * Validation: HUD layouts
 * ========================================================================== */

/* a scalar that reads as a number */
static gboolean
node_is_number (YamlNode *node)
{
	gint64 i;
	gdouble d;
	gboolean is_int;

	return node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR &&
	       parse_number (yaml_node_get_scalar (node), &i, &d, &is_int) && isfinite (d);
}

static void
validate_hud_layout (Validation *v)
{
	static const gchar * const root_keys[] = { "version", "pieces", NULL };
	static const gchar * const piece_keys[] = { "anchor", "x", "y", "locked", "hidden", NULL };
	g_autoptr(GError) error = NULL;
	YamlMapping *root;
	YamlNode *pieces_node;
	YamlMapping *pieces;
	GEnumClass *anchors;
	guint errors_before;
	guint i;

	errors_before = count_errors (v);

	/* --- structure: every problem, with paths (the loader stops at one) --- */
	root = mapping_at (v->self, "");
	if (root != NULL)
	{
		for (i = 0; i < yaml_mapping_get_size (root); i++)
		{
			const gchar *key = yaml_mapping_get_key (root, i);
			g_autofree gchar *path = lrg_editor_document_path_join ("", key);

			if (!g_strv_contains (root_keys, key))
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, path,
				           "unknown key '%s': HUD layouts only have version and pieces", key);
		}
	}

	pieces_node = lookup (v->self, "/pieces");
	pieces = pieces_node != NULL && yaml_node_get_node_type (pieces_node) == YAML_NODE_MAPPING
	         ? yaml_node_get_mapping (pieces_node) : NULL;
	anchors = (GEnumClass *) g_type_class_ref (LRG_TYPE_HUD_ANCHOR);
	for (i = 0; pieces != NULL && i < yaml_mapping_get_size (pieces); i++)
	{
		const gchar *id = yaml_mapping_get_key (pieces, i);
		YamlNode *node = yaml_mapping_get_value (pieces, i);
		g_autofree gchar *path = lrg_editor_document_path_join ("/pieces", id);
		YamlMapping *piece;
		const gchar *anchor;
		guint j;

		if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		{
			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, path, "piece '%s' is not a mapping", id);
			continue;
		}
		piece = yaml_node_get_mapping (node);
		for (j = 0; j < yaml_mapping_get_size (piece); j++)
		{
			const gchar *key = yaml_mapping_get_key (piece, j);
			g_autofree gchar *kpath = lrg_editor_document_path_join (path, key);

			if (!g_strv_contains (piece_keys, key))
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, kpath,
				           "piece '%s': unknown key '%s' (anchor, x, y, locked, hidden)", id, key);
		}

		anchor = member_text (piece, "anchor");
		if (anchor != NULL && g_enum_get_value_by_nick (anchors, anchor) == NULL)
		{
			g_autofree gchar *apath = lrg_editor_document_path_join (path, "anchor");

			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, apath, "piece '%s': unknown anchor '%s'", id, anchor);
		}
		for (j = 0; j < 2; j++)
		{
			const gchar *axis = j == 0 ? "x" : "y";
			YamlNode *value = yaml_mapping_get_member (piece, axis);
			g_autofree gchar *vpath = lrg_editor_document_path_join (path, axis);

			if (value == NULL)
				continue;
			if (!node_is_number (value))
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, vpath, "piece '%s': %s must be a number", id, axis);
			else if (!yaml_mapping_has_member (piece, "anchor"))
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, vpath,
				           "piece '%s': %s needs an anchor beside it", id, axis);
		}
		for (j = 0; j < 2; j++)
		{
			const gchar *flag = j == 0 ? "locked" : "hidden";
			const gchar *text = member_text (piece, flag);
			g_autofree gchar *fpath = lrg_editor_document_path_join (path, flag);

			if (yaml_mapping_has_member (piece, flag) &&
			    !g_strv_contains ((const gchar * const[]) { "true", "false", "yes", "no", NULL }, text != NULL ? text : ""))
				add_issue (v, LRG_EDITOR_PROBLEM_ERROR, fpath,
				           "piece '%s': %s must be true or false", id, flag);
		}
	}
	g_type_class_unref (anchors);

	/* --- the engine loader; its first error only when ours found none --- */
	{
		g_autoptr(LrgHudLayout) layout = lrg_hud_layout_new ();

		if (!lrg_hud_layout_load_from_data (layout, v->text, (gssize) v->length, &error) &&
		    count_errors (v) == errors_before)
			add_loader_error (v, error, DATA_SOURCE);
	}
}

/* ==========================================================================
 * Validation: dialog trees
 * ========================================================================== */

static void
validate_dialog_tree (Validation *v)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) ids = NULL;
	g_autoptr(GHashTable) reached = NULL;
	g_autoptr(GPtrArray) edges = NULL;
	g_autoptr(GQueue) queue = NULL;
	YamlSequence *nodes;
	const gchar *start;
	guint errors_before;
	guint i;

	errors_before = count_errors (v);

	/* --- ids: present, unique --- */
	nodes = sequence_at (v->self, "/nodes");
	if (nodes == NULL || yaml_sequence_get_length (nodes) == 0)
		add_issue (v, LRG_EDITOR_PROBLEM_ERROR, lrg_editor_document_has_path (v->self, "/nodes") ? "/nodes" : NULL,
		           "a dialog tree needs a non-empty nodes sequence");
	if (member_text (mapping_at (v->self, ""), "id") == NULL)
		add_issue (v, LRG_EDITOR_PROBLEM_ERROR, NULL, "a dialog tree needs an id");

	ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; nodes != NULL && i < yaml_sequence_get_length (nodes); i++)
	{
		YamlNode *node = yaml_sequence_get_element (nodes, i);
		g_autofree gchar *idx = g_strdup_printf ("%u", i);
		g_autofree gchar *path = lrg_editor_document_path_join ("/nodes", idx);
		YamlMapping *map;
		const gchar *id;
		guint j;

		if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		{
			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, path, "dialog node %u is not a mapping", i);
			continue;
		}
		map = yaml_node_get_mapping (node);
		id = member_text (map, "id");
		if (id == NULL)
		{
			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, path, "dialog node %u has no id", i);
		}
		else if (g_hash_table_contains (ids, id))
		{
			g_autofree gchar *ipath = lrg_editor_document_path_join (path, "id");

			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, ipath, "duplicate node id '%s' (also node %u)", id,
			           GPOINTER_TO_UINT (g_hash_table_lookup (ids, id)) - 1);
		}
		else
		{
			g_hash_table_insert (ids, g_strdup (id), GUINT_TO_POINTER (i + 1));
		}
		if (member_text (map, "text") == NULL)
			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, path, "dialog node '%s' has no text",
			           id != NULL ? id : idx);

		{
			g_autofree gchar *rpath = lrg_editor_document_path_join (path, "responses");
			YamlSequence *rs = sequence_at (v->self, rpath);

			for (j = 0; rs != NULL && j < yaml_sequence_get_length (rs); j++)
			{
				YamlNode *r = yaml_sequence_get_element (rs, j);
				g_autofree gchar *jdx = g_strdup_printf ("%u", j);
				g_autofree gchar *path_j = lrg_editor_document_path_join (rpath, jdx);

				if (yaml_node_get_node_type (r) != YAML_NODE_MAPPING ||
				    member_text (yaml_node_get_mapping (r), "text") == NULL ||
				    member_text (yaml_node_get_mapping (r), "next") == NULL)
					add_issue (v, LRG_EDITOR_PROBLEM_ERROR, path_j,
					           "response %u of '%s' needs text and next", j, id != NULL ? id : idx);
			}
		}
	}

	/* --- references --- */
	edges = lrg_editor_document_dialog_edges (v->self);
	for (i = 0; i < edges->len; i++)
	{
		LrgEditorDocumentEdge *edge = (LrgEditorDocumentEdge *) g_ptr_array_index (edges, i);

		if (!g_hash_table_contains (ids, edge->to))
			add_issue (v, LRG_EDITOR_PROBLEM_ERROR, edge->path, "'%s' leads to missing node '%s'",
			           edge->from, edge->to);
	}
	start = member_text (mapping_at (v->self, ""), "start");
	if (start != NULL && !g_hash_table_contains (ids, start))
		add_issue (v, LRG_EDITOR_PROBLEM_ERROR, "/start", "start node '%s' does not exist", start);

	/* --- reachability from the start (or first) node --- */
	if (start == NULL && nodes != NULL && yaml_sequence_get_length (nodes) > 0)
	{
		YamlNode *first = yaml_sequence_get_element (nodes, 0);

		if (yaml_node_get_node_type (first) == YAML_NODE_MAPPING)
			start = member_text (yaml_node_get_mapping (first), "id");
	}
	reached = g_hash_table_new (g_str_hash, g_str_equal);
	queue = g_queue_new ();
	if (start != NULL && g_hash_table_contains (ids, start))
	{
		g_hash_table_add (reached, (gpointer) start);
		g_queue_push_tail (queue, (gpointer) start);
	}
	while (!g_queue_is_empty (queue))
	{
		const gchar *from = (const gchar *) g_queue_pop_head (queue);

		for (i = 0; i < edges->len; i++)
		{
			LrgEditorDocumentEdge *edge = (LrgEditorDocumentEdge *) g_ptr_array_index (edges, i);

			if (g_strcmp0 (edge->from, from) == 0 && g_hash_table_contains (ids, edge->to) &&
			    !g_hash_table_contains (reached, edge->to))
			{
				g_hash_table_add (reached, edge->to);
				g_queue_push_tail (queue, edge->to);
			}
		}
	}
	if (start != NULL && g_hash_table_contains (ids, start))
	{
		for (i = 0; nodes != NULL && i < yaml_sequence_get_length (nodes); i++)
		{
			YamlNode *node = yaml_sequence_get_element (nodes, i);
			const gchar *id;
			g_autofree gchar *idx = g_strdup_printf ("%u", i);
			g_autofree gchar *path = lrg_editor_document_path_join ("/nodes", idx);

			if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
				continue;
			id = member_text (yaml_node_get_mapping (node), "id");
			if (id != NULL && !g_hash_table_contains (reached, id))
				add_issue (v, LRG_EDITOR_PROBLEM_WARNING, path, "node '%s' cannot be reached from '%s'",
				           id, start);
		}
	}

	/* --- the engine loader (from memory); its first error only when ours found none --- */
	{
		g_autoptr(LrgDialogTree) tree = NULL;
		Capture capture = { v, TRUE, DATA_SOURCE };
		guint handler;

		handler = g_log_set_handler (LRG_LOG_DOMAIN_DIALOG, G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
		                             capture_log, &capture);
		tree = lrg_dialog_tree_new_from_data (v->text, (gssize) v->length, &error);
		g_log_remove_handler (LRG_LOG_DOMAIN_DIALOG, handler);
		if (tree == NULL && count_errors (v) == errors_before)
			add_loader_error (v, error, DATA_SOURCE);
	}
}

/* ==========================================================================
 * Validation: formats checked by their (strict) loaders alone
 * ========================================================================== */

/* A stand-in for game handlers the editor cannot resolve */
static void
ui_layout_stub_handler (void)
{
}

/*
 * validate_ui_layout:
 *
 * Runs LrgUILoader on the document. Signal handlers name functions of
 * the game, which the editor does not have: an unknown handler is a
 * warning, not an error. The loader stops at the first one, so each is
 * registered as a no-op stub and the load retried (bounded).
 */
static void
validate_ui_layout (Validation *v)
{
	g_autoptr(LrgUILoader) loader = lrg_ui_loader_new ();
	guint                  attempt;

	for (attempt = 0; attempt < 64; attempt++)
	{
		g_autoptr(GError) error = NULL;
		LrgWidget        *widget;
		const gchar      *start;
		const gchar      *end;
		g_autofree gchar *name = NULL;

		widget = lrg_ui_loader_load_from_data (loader, v->text, (gssize) v->length, &error);
		if (widget != NULL)
		{
			g_object_unref (widget);
			return;
		}

		/* "unknown handler 'NAME' for signal ..." */
		start = g_error_matches (error, LRG_UI_LOADER_ERROR, LRG_UI_LOADER_ERROR_HANDLER)
		        ? strstr (error->message, "unknown handler '") : NULL;
		end = start != NULL ? strchr (start + strlen ("unknown handler '"), '\'') : NULL;
		if (end == NULL)
		{
			add_loader_error (v, error, NULL);
			return;
		}
		start += strlen ("unknown handler '");
		name = g_strndup (start, (gsize) (end - start));
		add_issue (v, LRG_EDITOR_PROBLEM_WARNING, NULL,
		           "handler '%s' is provided by the game, not the editor", name);
		lrg_ui_loader_add_handler (loader, name, G_CALLBACK (ui_layout_stub_handler), NULL);
	}
}

static void
validate_dungeon (Validation *v)
{
	g_autoptr(LrgProcgenDungeon) dungeon = NULL;
	g_autoptr(GError) error = NULL;

	dungeon = lrg_procgen_dungeon_new_from_data (v->text, (gssize) v->length, &error);
	if (dungeon == NULL)
		add_loader_error (v, error, NULL);
}

static void
validate_theme (Validation *v)
{
	g_autoptr(LrgTheme) theme = NULL;
	g_autoptr(GError) error = NULL;

	theme = lrg_theme_new_from_data (v->text, (gssize) v->length, &error);
	if (theme == NULL)
		add_loader_error (v, error, NULL);
}

/* ==========================================================================
 * Engine-backed formats (BT, FSM, particles, weather, mixer, tileset,
 * sprite, water, nav settings)
 *
 * These documents are still YAML trees (so path edits, the generic IPC
 * methods and undo work as for every format), but the engine object is
 * the authority on the file: the loader parses the tree's text, the
 * writer produces what is saved, and editors commit whole objects.
 * ========================================================================== */

/*
 * engine_parse:
 * Runs @format's engine loader on @text.  Relative paths (weather particle
 * effects, tileset images) resolve from @base_dir; no texture is loaded,
 * so this works without a GL context.
 */
static GObject *
engine_parse (LrgEditorDocumentFormat   format,
              const gchar              *text,
              gssize                    length,
              const gchar              *base_dir,
              GError                  **error)
{
	switch (format)
	{
	case LRG_EDITOR_DOCUMENT_FORMAT_BT:
		return (GObject *) lrg_bt_asset_new_from_data (text, length, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_FSM:
		return (GObject *) lrg_logic_machine_new_from_data (text, length, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES:
		return (GObject *) lrg_particle_asset_new_from_data (text, length, DATA_SOURCE, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_WEATHER:
		return (GObject *) lrg_weather_asset_new_from_data (text, length, base_dir, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_MIXER:
		return (GObject *) lrg_mixer_asset_new_from_data (text, length, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_TILESET:
		return (GObject *) lrg_tileset_new_from_asset_data (text, length, base_dir, DATA_SOURCE, FALSE, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_SPRITE:
		return (GObject *) lrg_sprite_sheet_new_from_rsprite_data (text, length, DATA_SOURCE, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_WATER:
		return (GObject *) lrg_water_body_new_from_data (text, length, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS:
		return (GObject *) lrg_nav_bake_new_from_data (text, length, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT:
		return (GObject *) lrg_vscript_program_new_from_data (text, length, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_FOLEY:
		return (GObject *) lrg_foley_patch_new_from_data (text, length, error);
	case LRG_EDITOR_DOCUMENT_FORMAT_SONG:
		{
			/* clips, samples and patches resolve against the song's folder */
			LrgSong *song = lrg_song_new_from_data (text, length, error);

			if (song != NULL && base_dir != NULL)
				g_object_set (song, "base-path", base_dir, NULL);
			return (GObject *) song;
		}
	default:
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "%s documents have no engine object",
		             lrg_editor_document_format_to_string (format));
		return NULL;
	}
}

/*
 * engine_write:
 * Runs the engine writer of @object's type.  The caller checked that the
 * type belongs to an engine-backed format.
 */
static gchar *
engine_write (GObject  *object,
              GError  **error)
{
	if (LRG_IS_BT_ASSET (object))
		return lrg_bt_asset_to_data (LRG_BT_ASSET (object), error);
	if (LRG_IS_LOGIC_MACHINE (object))
		return lrg_logic_machine_to_data (LRG_LOGIC_MACHINE (object), error);
	if (LRG_IS_PARTICLE_ASSET (object))
		return lrg_particle_asset_save_to_data (LRG_PARTICLE_ASSET (object), NULL, error);
	if (LRG_IS_WEATHER_ASSET (object))
		return lrg_weather_asset_to_data (LRG_WEATHER_ASSET (object), error);
	if (LRG_IS_MIXER_ASSET (object))
		return lrg_mixer_asset_to_data (LRG_MIXER_ASSET (object), NULL, error);
	if (LRG_IS_TILESET (object))
		return lrg_tileset_save_to_data (LRG_TILESET (object), NULL, error);
	if (LRG_IS_SPRITE_SHEET (object))
		return lrg_sprite_sheet_save_rsprite_to_data (LRG_SPRITE_SHEET (object), NULL, error);
	if (LRG_IS_WATER_BODY (object))
		return lrg_water_body_to_data (LRG_WATER_BODY (object), NULL, error);
	if (LRG_IS_NAV_BAKE (object))
		return lrg_nav_bake_to_data (LRG_NAV_BAKE (object));
	if (LRG_IS_VSCRIPT_PROGRAM (object))
		return lrg_vscript_program_to_data (LRG_VSCRIPT_PROGRAM (object), error);
	if (LRG_IS_FOLEY_PATCH (object))
		return lrg_foley_patch_to_data (LRG_FOLEY_PATCH (object), NULL, error);
	if (LRG_IS_SONG (object))
		return lrg_song_to_data (LRG_SONG (object), NULL, error);

	g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
	             "%s is not an engine-backed document object", G_OBJECT_TYPE_NAME (object));
	return NULL;
}

/*
 * engine_skeleton:
 * The text of a new engine-backed document: the writer's output for the
 * engine's default object, so a new file is exactly what the engine
 * would write.  %NULL for the plain YAML formats (skeleton_for() has
 * those).
 */
static gchar *
engine_skeleton (LrgEditorDocumentFormat format)
{
	g_autoptr(GObject) object = NULL;
	g_autoptr(GError) error = NULL;
	gchar *text;

	switch (format)
	{
	case LRG_EDITOR_DOCUMENT_FORMAT_BT:
		{
			/* validate wants a root: an empty `sequence` (succeeds at once) */
			g_autoptr(LrgBtAssetNode) root = lrg_bt_asset_node_new ("sequence");

			object = (GObject *) lrg_bt_asset_new ();
			lrg_bt_asset_node_set_id (root, "root");
			lrg_bt_asset_set_root (LRG_BT_ASSET (object), root);
		}
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_FSM:
		{
			/* validate wants a state: one `idle` state, the initial one */
			g_autoptr(LrgLogicState) idle = lrg_logic_state_new ("idle");
			LrgLogicState *root;

			object = (GObject *) lrg_logic_machine_new ();
			root = lrg_logic_machine_get_root (LRG_LOGIC_MACHINE (object));
			lrg_logic_state_add_state (root, idle);
			lrg_logic_state_set_initial (root, "idle");
		}
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES:
		object = (GObject *) lrg_particle_asset_new ();
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_WEATHER:
		object = (GObject *) lrg_weather_asset_new ();
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_MIXER:
		object = (GObject *) lrg_mixer_asset_new ();
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_TILESET:
		/* one 16x16 tile; the tileset editor sets the image and grid */
		object = (GObject *) lrg_tileset_new_empty (16, 16, 1, 1);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_SPRITE:
		object = (GObject *) lrg_sprite_sheet_new ("sprite");
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_WATER:
		object = (GObject *) lrg_water_body_new (LRG_WATER_BODY_PLANE);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS:
		object = (GObject *) lrg_nav_bake_new ();
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT:
		{
			LrgVscriptProgram *program = lrg_vscript_program_new ();
			LrgVscriptGraph *graph = lrg_vscript_program_get_main (program);
			LrgVscriptNode *log_node;

			lrg_vscript_program_set_name (program, "script");
			lrg_vscript_graph_create_node (graph, "event.start", "start", 40.0, 40.0, NULL);
			log_node = lrg_vscript_graph_create_node (graph, "debug.log", "log", 280.0, 40.0, NULL);
			if (log_node != NULL)
				lrg_vscript_node_set_input (log_node, "text", g_variant_new_string ("hello"));
			lrg_vscript_program_connect (program, NULL, "start", "then", "log", "exec", NULL);
			object = (GObject *) program;
		}
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_FOLEY:
		{
			/* a short blip: one tone, so a new patch already makes a sound */
			LrgFoleyPatch *patch = lrg_foley_patch_new ();
			gint           layer;

			g_object_set (patch, "name", "sound", "duration", 0.3, NULL);
			layer = lrg_foley_patch_add_layer (patch, LRG_FOLEY_LAYER_TONE);
			lrg_foley_patch_set_layer_param (patch, (guint) layer, "freq-end", 440.0);
			lrg_foley_patch_set_layer_param (patch, (guint) layer, "decay", 0.08);
			object = (GObject *) patch;
		}
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_SONG:
		{
			/* a drum machine and a bass on a bar-long loop */
			LrgSong *song = lrg_song_new ();
			guint    i;

			g_object_set (song, "title", "song", "bpm", 110.0, NULL);
			lrg_song_set_instrument (song, "drums", "drums", "808");
			lrg_song_set_instrument (song, "bass", "synth", "synthwave-bass");
			lrg_song_add_pattern (song, "beat", 4);
			lrg_song_pattern_set_lane (song, "beat", "kick", "x...x...x...x...");
			lrg_song_pattern_set_lane (song, "beat", "snare", "....x.......x...");
			lrg_song_pattern_set_lane (song, "beat", "closed-hat", "..x...x...x...x.");
			lrg_song_add_pattern (song, "bassline", 4);
			for (i = 0; i < 8; i++)
				lrg_song_pattern_add_note (song, "bassline", i * 0.5, i % 2 ? 45 : 33, 0.4, 0.85);
			lrg_song_add_track (song, "drums", LRG_SONG_TRACK_INSTRUMENT, "drums");
			lrg_song_add_track (song, "bass", LRG_SONG_TRACK_INSTRUMENT, "bass");
			lrg_song_add_clip (song, "drums", "beat", 0, 16);
			lrg_song_add_clip (song, "bass", "bassline", 0, 16);
			object = (GObject *) song;
		}
		break;
	default:
		return NULL;
	}

	text = engine_write (object, &error);
	if (text == NULL)
	{
		/* a default object always writes; keep the document usable anyway */
		g_warning ("LrgEditorDocument: cannot write a new %s: %s",
		           lrg_editor_document_format_to_string (format), error->message);
		text = g_strdup ("version: 1\n");
	}

	return text;
}

/* whether the document's format has an engine object */
static gboolean
is_engine_backed (LrgEditorDocument *self)
{
	return lrg_editor_document_format_get_object_type (self->format) != G_TYPE_INVALID;
}

/*
 * file_text:
 * What a save writes (owned by @self, cached until the next change): the
 * engine writer's output for an engine-backed document the engine can
 * parse, the YAML text otherwise.
 */
static const gchar *
file_text (LrgEditorDocument *self,
           gsize             *length)
{
	const gchar *yaml;
	gsize        yaml_length;

	yaml = document_text (self, &yaml_length);
	if (!is_engine_backed (self))
	{
		if (length != NULL)
			*length = yaml_length;
		return yaml;
	}

	if (self->file_cache == NULL)
	{
		g_autofree gchar *base_dir = document_base_dir (self);
		g_autoptr(GObject) object = NULL;
		gchar *text = NULL;

		object = engine_parse (self->format, yaml, (gssize) yaml_length, base_dir, NULL);
		if (object != NULL)
			text = engine_write (object, NULL);
		/* the engine cannot read it yet: save the YAML, validation says why */
		if (text == NULL)
			text = g_strndup (yaml, yaml_length);
		self->file_cache = text;
		self->file_length = strlen (text);
	}

	if (length != NULL)
		*length = self->file_length;
	return self->file_cache;
}

GObject *
lrg_editor_document_dup_object (LrgEditorDocument  *self,
                                GError            **error)
{
	g_autofree gchar *base_dir = NULL;
	const gchar *text;
	gsize length;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	base_dir = document_base_dir (self);
	text = document_text (self, &length);

	return engine_parse (self->format, text, (gssize) length, base_dir, error);
}

gboolean
lrg_editor_document_commit_object (LrgEditorDocument  *self,
                                   GObject            *object,
                                   const gchar        *label,
                                   guint               gesture,
                                   GError            **error)
{
	g_autofree gchar *text = NULL;
	GType type;
	gboolean ok;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);
	g_return_val_if_fail (G_IS_OBJECT (object), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* --- the object must be the format's engine type --- */
	type = lrg_editor_document_format_get_object_type (self->format);
	if (type == G_TYPE_INVALID || !g_type_is_a (G_OBJECT_TYPE (object), type))
	{
		g_set_error (error, LRG_EDITOR_DOCUMENT_ERROR, LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
		             "a %s document cannot hold a %s",
		             lrg_editor_document_format_to_string (self->format), G_OBJECT_TYPE_NAME (object));
		return FALSE;
	}
	if (!check_writable (self, error))
		return FALSE;

	/* --- the engine writer's text; no command when nothing changes --- */
	text = engine_write (object, error);
	if (text == NULL)
		return FALSE;
	if (g_strcmp0 (text, file_text (self, NULL)) == 0)
		return TRUE;

	/*
	 * One replace of the root: its undo restores the previous tree, so
	 * the undo stack holds whole snapshots of the engine's output.  A
	 * gesture merges consecutive commits (a drag) into one step.
	 */
	if (gesture != 0)
		return lrg_editor_document_replace_gesture (self, "", text, gesture, error);

	lrg_editor_document_begin_group (self, label != NULL ? label : "Edit");
	ok = lrg_editor_document_replace_gesture (self, "", text, 0, error);
	lrg_editor_document_end_group (self);

	return ok;
}

gchar *
lrg_editor_document_to_file_data (LrgEditorDocument *self,
                                  gsize             *length)
{
	const gchar *text;
	gsize len;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	text = file_text (self, &len);
	if (length != NULL)
		*length = len;

	return g_strndup (text, len);
}

/*
 * validate_engine:
 * The engine loader on the document text, then the asset's own validate
 * where it has one (references, cycles, ranges).  Loader warnings become
 * warning issues through the usual log capture.
 */
static void
validate_engine (Validation *v)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GObject) object = NULL;
	g_autofree gchar *base_dir = document_base_dir (v->self);
	gboolean ok = TRUE;

	object = engine_parse (v->self->format, v->text, (gssize) v->length, base_dir, &error);
	if (object == NULL)
	{
		add_loader_error (v, error, DATA_SOURCE);
		return;
	}

	if (LRG_IS_BT_ASSET (object))
		ok = lrg_bt_asset_validate (LRG_BT_ASSET (object), &error);
	else if (LRG_IS_LOGIC_MACHINE (object))
		ok = lrg_logic_machine_validate (LRG_LOGIC_MACHINE (object), &error);
	else if (LRG_IS_PARTICLE_ASSET (object))
		ok = lrg_particle_asset_validate (LRG_PARTICLE_ASSET (object), &error);
	else if (LRG_IS_MIXER_ASSET (object))
		ok = lrg_mixer_asset_validate (LRG_MIXER_ASSET (object), &error);
	else if (LRG_IS_VSCRIPT_PROGRAM (object))
	{
		g_autoptr(GPtrArray) found = NULL;
		guint j;

		ok = lrg_vscript_program_validate (LRG_VSCRIPT_PROGRAM (object), &found);
		if (!ok && found != NULL)
		{
			for (j = 0; j < found->len; j++)
			{
				LrgVscriptIssue *issue = (LrgVscriptIssue *) g_ptr_array_index (found, j);

				if (issue->severity != LRG_VSCRIPT_SEVERITY_ERROR)
					continue;
				g_set_error (&error, LRG_VSCRIPT_ERROR, LRG_VSCRIPT_ERROR_INVALID,
				             "%s", issue->message != NULL ? issue->message : "invalid visual script");
				break;
			}
		}
	}
	else if (LRG_IS_SONG (object))
	{
		g_autoptr(GPtrArray) found = NULL;
		guint j;

		/* the first problem that is not a warning */
		ok = lrg_song_validate (LRG_SONG (object), &found);
		for (j = 0; !ok && found != NULL && j < found->len; j++)
		{
			const gchar *text = g_ptr_array_index (found, j);

			if (g_str_has_prefix (text, "warning: "))
				continue;
			g_set_error (&error, LRG_SONG_ERROR, LRG_SONG_ERROR_INVALID_VALUE, "%s", text);
			break;
		}
	}
	if (!ok)
		add_loader_error (v, error, DATA_SOURCE);
}

gboolean
lrg_editor_document_validate (LrgEditorDocument  *self,
                              GPtrArray         **issues)
{
	g_autoptr(GPtrArray) found = NULL;
	g_autoptr(GPtrArray) sorted = NULL;
	Validation v;
	gboolean ok;
	gint severity;
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), FALSE);

	found = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_document_issue_free);
	v.self = self;
	v.issues = found;
	v.text = document_text (self, &v.length);

	/* duplicate keys seen when the file was read */
	for (i = 0; i < self->load_issues->len; i++)
		g_ptr_array_add (found, lrg_editor_document_issue_copy (g_ptr_array_index (self->load_issues, i)));

	switch (self->format)
	{
	case LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP:
		validate_input_map (&v);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_LOCALE:
		validate_locale (&v);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK:
		validate_sound_bank (&v);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT:
		validate_hud_layout (&v);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT:
		validate_ui_layout (&v);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE:
		validate_dialog_tree (&v);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON:
		validate_dungeon (&v);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_THEME:
		validate_theme (&v);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_BT:
	case LRG_EDITOR_DOCUMENT_FORMAT_FSM:
	case LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES:
	case LRG_EDITOR_DOCUMENT_FORMAT_WEATHER:
	case LRG_EDITOR_DOCUMENT_FORMAT_MIXER:
	case LRG_EDITOR_DOCUMENT_FORMAT_TILESET:
	case LRG_EDITOR_DOCUMENT_FORMAT_SPRITE:
	case LRG_EDITOR_DOCUMENT_FORMAT_WATER:
	case LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS:
	case LRG_EDITOR_DOCUMENT_FORMAT_VSCRIPT:
	case LRG_EDITOR_DOCUMENT_FORMAT_FOLEY:
	case LRG_EDITOR_DOCUMENT_FORMAT_SONG:
		validate_engine (&v);
		break;
	case LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN:
	default:
		break;
	}

	/* errors first, then warnings, then infos; stable within a severity */
	sorted = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_document_issue_free);
	ok = TRUE;
	for (severity = LRG_EDITOR_PROBLEM_ERROR; severity >= LRG_EDITOR_PROBLEM_INFO; severity--)
	{
		for (i = 0; i < found->len; i++)
		{
			LrgEditorDocumentIssue *issue = (LrgEditorDocumentIssue *) g_ptr_array_index (found, i);

			if ((gint) issue->severity != severity)
				continue;
			if (issue->severity == LRG_EDITOR_PROBLEM_ERROR)
				ok = FALSE;
			g_ptr_array_add (sorted, lrg_editor_document_issue_copy (issue));
		}
	}

	if (issues != NULL)
		*issues = g_steal_pointer (&sorted);

	return ok;
}

/* ==========================================================================
 * Format helpers
 * ========================================================================== */

GStrv
lrg_editor_document_input_actions (LrgEditorDocument *self)
{
	GStrv keys;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	keys = lrg_editor_document_get_keys (self, "/actions");

	return keys != NULL ? keys : g_new0 (gchar *, 1);
}

GStrv
lrg_editor_document_locale_keys (LrgEditorDocument *self)
{
	GStrv keys;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	keys = lrg_editor_document_get_keys (self, "/strings");

	return keys != NULL ? keys : g_new0 (gchar *, 1);
}

GStrv
lrg_editor_document_locale_missing_keys (LrgEditorDocument *self,
                                         LrgEditorDocument *reference)
{
	g_autoptr(GStrvBuilder) builder = NULL;
	g_auto(GStrv) theirs = NULL;
	YamlMapping *mine;
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);
	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (reference), NULL);

	builder = g_strv_builder_new ();
	mine = mapping_at (self, "/strings");
	theirs = lrg_editor_document_locale_keys (reference);
	for (i = 0; theirs[i] != NULL; i++)
	{
		if (mine == NULL || !yaml_mapping_has_member (mine, theirs[i]))
			g_strv_builder_add (builder, theirs[i]);
	}

	return g_strv_builder_end (builder);
}

GStrv
lrg_editor_document_dialog_nodes (LrgEditorDocument *self)
{
	g_autoptr(GStrvBuilder) builder = NULL;
	YamlSequence *nodes;
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	builder = g_strv_builder_new ();
	nodes = sequence_at (self, "/nodes");
	for (i = 0; nodes != NULL && i < yaml_sequence_get_length (nodes); i++)
	{
		YamlNode *node = yaml_sequence_get_element (nodes, i);
		const gchar *id;

		if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
			continue;
		id = member_text (yaml_node_get_mapping (node), "id");
		if (id != NULL)
			g_strv_builder_add (builder, id);
	}

	return g_strv_builder_end (builder);
}

GPtrArray *
lrg_editor_document_dialog_edges (LrgEditorDocument *self)
{
	GPtrArray *edges;
	YamlSequence *nodes;
	guint n;
	guint i;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);

	edges = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_editor_document_edge_free);
	nodes = sequence_at (self, "/nodes");
	n = nodes != NULL ? yaml_sequence_get_length (nodes) : 0;
	for (i = 0; i < n; i++)
	{
		YamlNode *node = yaml_sequence_get_element (nodes, i);
		g_autofree gchar *idx = g_strdup_printf ("%u", i);
		g_autofree gchar *path = lrg_editor_document_path_join ("/nodes", idx);
		g_autofree gchar *rpath = NULL;
		YamlMapping *map;
		YamlSequence *responses;
		const gchar *id;
		const gchar *next;
		guint j;

		if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
			continue;
		map = yaml_node_get_mapping (node);
		id = member_text (map, "id");
		if (id == NULL)
			continue;

		next = member_text (map, "next");
		if (next != NULL)
		{
			g_autofree gchar *npath = lrg_editor_document_path_join (path, "next");

			g_ptr_array_add (edges, edge_new (id, next, -1, npath));
		}

		rpath = lrg_editor_document_path_join (path, "responses");
		responses = sequence_at (self, rpath);
		for (j = 0; responses != NULL && j < yaml_sequence_get_length (responses); j++)
		{
			YamlNode *r = yaml_sequence_get_element (responses, j);
			g_autofree gchar *jdx = g_strdup_printf ("%u", j);
			g_autofree gchar *rj = lrg_editor_document_path_join (rpath, jdx);
			g_autofree gchar *npath = lrg_editor_document_path_join (rj, "next");
			const gchar *rnext;

			if (yaml_node_get_node_type (r) != YAML_NODE_MAPPING)
				continue;
			rnext = member_text (yaml_node_get_mapping (r), "next");
			if (rnext != NULL)
				g_ptr_array_add (edges, edge_new (id, rnext, (gint) j, npath));
		}

		/* the loader's linear-scene rule: no next and no responses chains
		 * to the following node */
		if (next == NULL && (responses == NULL || yaml_sequence_get_length (responses) == 0) &&
		    !yaml_mapping_has_member (map, "responses") && i + 1 < n)
		{
			YamlNode *following = yaml_sequence_get_element (nodes, i + 1);
			const gchar *to = yaml_node_get_node_type (following) == YAML_NODE_MAPPING
			                  ? member_text (yaml_node_get_mapping (following), "id") : NULL;

			if (to != NULL)
				g_ptr_array_add (edges, edge_new (id, to, -1, NULL));
		}
	}

	return edges;
}

gchar *
lrg_editor_document_sound_bank_resolve (LrgEditorDocument *self,
                                        const gchar       *name)
{
	g_autofree gchar *base = NULL;
	g_autofree gchar *dir = NULL;
	YamlMapping *sounds;
	YamlNode *node;
	const gchar *file;
	const gchar *base_path;

	g_return_val_if_fail (LRG_IS_EDITOR_DOCUMENT (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sounds = mapping_at (self, "/sounds");
	node = sounds != NULL ? yaml_mapping_get_member (sounds, name) : NULL;
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	file = yaml_node_get_scalar (node);
	if (g_path_is_absolute (file))
		return g_strdup (file);

	/* mirror LrgSoundBank: base_path relative to the manifest's directory */
	base = document_base_dir (self);
	base_path = member_text (mapping_at (self, ""), "base_path");
	if (base_path == NULL)
		dir = g_strdup (base);
	else if (g_path_is_absolute (base_path))
		dir = g_strdup (base_path);
	else
		dir = g_build_filename (base, base_path, NULL);

	return g_build_filename (dir, file, NULL);
}
