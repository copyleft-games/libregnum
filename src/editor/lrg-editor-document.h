/* lrg-editor-document.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorDocument: the headless model behind the editor's data editors.
 *
 * A document holds one engine data file (input map, locale, sound bank,
 * HUD layout, UI layout, dialog tree, procgen dungeon recipe or theme) as
 * a YAML tree.  Every edit goes through a YAML path ("/actions/jump/
 * bindings/0/key") and is recorded as a command on the document's own
 * undo stack, so the eight data editors, the IPC methods and scripts all
 * share one model with one undo history per file.  Unknown keys, key
 * order and comments survive a save; validation runs the matching engine
 * loader on the serialized text plus structural checks per format.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-editor-validate.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LrgEditorDocumentFormat:
 * @LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN: not recognised; plain YAML editing only
 * @LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP: an #LrgInputMap file (`actions:`)
 * @LRG_EDITOR_DOCUMENT_FORMAT_LOCALE: an #LrgLocale file (`code:`, `name:`, `strings:`)
 * @LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK: an #LrgSoundBank manifest (`name:`, `sounds:`)
 * @LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT: an #LrgHudLayout state file (`version:`, `pieces:`)
 * @LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT: an #LrgUILoader document (`type:`, `children:`)
 * @LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE: an #LrgDialogTree definition (`id:`, `nodes:`)
 * @LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON: an #LrgProcgenDungeon recipe (`generator:`)
 * @LRG_EDITOR_DOCUMENT_FORMAT_THEME: an #LrgTheme file (`preset:`, `colors:`, ...)
 * @LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL: an #LrgMaterial asset (a `.rmat`
 *   file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK: an #LrgPostStack (a `.rpost`
 *   file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH: an #LrgShaderGraph (a
 *   `.rshadergraph` file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH: an #LrgAnimGraph (a `.ranim`
 *   file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP: an #LrgBoneMap (a `.rbonemap`
 *   file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE: an #LrgSequence (a `.rseq` file,
 *   or a mapping with `type: sequence`). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D: an #LrgSkeleton2D (a `.rskel2d`
 *   file, or a mapping with an `rskel2d:` version key). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET: an #LrgPhysicsAsset (a
 *   `.rphysasset` ragdoll description, or a mapping whose `parts:` is a
 *   sequence of mappings with a `bone:` key). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_BT: an #LrgBtAsset behaviour tree (a `.rbt`
 *   file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_FSM: an #LrgLogicMachine state machine (a
 *   `.rfsm` file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES: an #LrgParticleAsset effect (a
 *   `.rparticles` file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_WEATHER: an #LrgWeatherAsset (a `.rweather`
 *   file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_MIXER: an #LrgMixerAsset bus graph (a
 *   `.rmixer` file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_TILESET: an #LrgTileset asset (a `.rtileset`
 *   file; recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_SPRITE: an #LrgSpriteSheet slicing and
 *   animation file (a `.rsprite` file; recognised by its extension only).
 *   Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_WATER: an #LrgWaterBody (a `.rwater` file;
 *   recognised by its extension only). Since 0.9.0.
 * @LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS: an #LrgNavBake navigation bake
 *   settings file (a `.rnav` file; recognised by its extension only).
 *   Since 0.9.0.
 *
 * The engine format a document holds. The formats from
 * %LRG_EDITOR_DOCUMENT_FORMAT_BT on are engine-backed: the document can
 * hand out the engine object (lrg_editor_document_dup_object()), take
 * one back as a single undo step (lrg_editor_document_commit_object()),
 * validates through the engine and saves the engine writer's text, so a
 * file the engine wrote is saved back byte for byte.
 *
 * It picks the validator, the
 * skeleton of a new document and the editor panel that opens the file.
 */
typedef enum
{
	LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN,
	LRG_EDITOR_DOCUMENT_FORMAT_INPUT_MAP,
	LRG_EDITOR_DOCUMENT_FORMAT_LOCALE,
	LRG_EDITOR_DOCUMENT_FORMAT_SOUND_BANK,
	LRG_EDITOR_DOCUMENT_FORMAT_HUD_LAYOUT,
	LRG_EDITOR_DOCUMENT_FORMAT_UI_LAYOUT,
	LRG_EDITOR_DOCUMENT_FORMAT_DIALOG_TREE,
	LRG_EDITOR_DOCUMENT_FORMAT_DUNGEON,
	LRG_EDITOR_DOCUMENT_FORMAT_THEME,
	LRG_EDITOR_DOCUMENT_FORMAT_MATERIAL,
	LRG_EDITOR_DOCUMENT_FORMAT_POST_STACK,
	LRG_EDITOR_DOCUMENT_FORMAT_SHADER_GRAPH,
	LRG_EDITOR_DOCUMENT_FORMAT_ANIM_GRAPH,
	LRG_EDITOR_DOCUMENT_FORMAT_BONE_MAP,
	LRG_EDITOR_DOCUMENT_FORMAT_SEQUENCE,
	LRG_EDITOR_DOCUMENT_FORMAT_SKELETON2D,
	LRG_EDITOR_DOCUMENT_FORMAT_PHYSICS_ASSET,
	LRG_EDITOR_DOCUMENT_FORMAT_BT,
	LRG_EDITOR_DOCUMENT_FORMAT_FSM,
	LRG_EDITOR_DOCUMENT_FORMAT_PARTICLES,
	LRG_EDITOR_DOCUMENT_FORMAT_WEATHER,
	LRG_EDITOR_DOCUMENT_FORMAT_MIXER,
	LRG_EDITOR_DOCUMENT_FORMAT_TILESET,
	LRG_EDITOR_DOCUMENT_FORMAT_SPRITE,
	LRG_EDITOR_DOCUMENT_FORMAT_WATER,
	LRG_EDITOR_DOCUMENT_FORMAT_NAV_SETTINGS
} LrgEditorDocumentFormat;

#define LRG_TYPE_EDITOR_DOCUMENT_FORMAT (lrg_editor_document_format_get_type ())

/**
 * lrg_editor_document_format_get_type:
 *
 * Returns: the #GType of #LrgEditorDocumentFormat
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_document_format_get_type (void) G_GNUC_CONST;

/**
 * lrg_editor_document_format_to_string:
 * @format: a format
 *
 * The format's nick, as used by IPC: "unknown", "input-map", "locale",
 * "sound-bank", "hud-layout", "ui-layout", "dialog-tree", "dungeon",
 * "theme", "material", "post-stack", "shader-graph", "anim-graph",
 * "bone-map", "sequence", "skeleton2d", "physics-asset", "bt", "fsm",
 * "particles", "weather", "mixer", "tileset", "sprite", "water" or
 * "nav-settings".
 *
 * Returns: (transfer none): the nick
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_document_format_to_string (LrgEditorDocumentFormat format);

/**
 * lrg_editor_document_format_from_string:
 * @name: (nullable): a nick of lrg_editor_document_format_to_string()
 *
 * Returns: the format, or %LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN for an
 *   unknown or %NULL name
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocumentFormat lrg_editor_document_format_from_string (const gchar *name);

/**
 * lrg_editor_document_format_get_extension:
 * @format: a format
 *
 * The file extension the engine uses for @format, with its dot: ".rmat",
 * ".rpost", ".rshadergraph", ".ranim", ".rbonemap", ".rseq", ".rskel2d",
 * ".rphysasset", ".rbt", ".rfsm", ".rparticles", ".rweather", ".rmixer",
 * ".rtileset", ".rsprite", ".rwater" or ".rnav". The plain YAML formats
 * (input map, locale, ...) have none.
 *
 * Returns: (transfer none) (nullable): the extension, or %NULL
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_document_format_get_extension (LrgEditorDocumentFormat format);

/**
 * lrg_editor_document_format_get_object_type:
 * @format: a format
 *
 * The engine type an engine-backed format loads into: #LrgBtAsset,
 * #LrgLogicMachine, #LrgParticleAsset, #LrgWeatherAsset, #LrgMixerAsset,
 * #LrgTileset, #LrgSpriteSheet, #LrgWaterBody or #LrgNavBake.
 *
 * Returns: the #GType, or %G_TYPE_INVALID when @format is a plain YAML
 *   format without an engine object
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_document_format_get_object_type (LrgEditorDocumentFormat format);

/**
 * LrgEditorDocumentNodeKind:
 * @LRG_EDITOR_DOCUMENT_NODE_MISSING: nothing at that path
 * @LRG_EDITOR_DOCUMENT_NODE_NULL: an explicit null (`~`, `null` or an empty value)
 * @LRG_EDITOR_DOCUMENT_NODE_SCALAR: a string, number or boolean
 * @LRG_EDITOR_DOCUMENT_NODE_MAPPING: a mapping (ordered keys)
 * @LRG_EDITOR_DOCUMENT_NODE_SEQUENCE: a sequence
 *
 * What a YAML path points at.
 */
typedef enum
{
	LRG_EDITOR_DOCUMENT_NODE_MISSING,
	LRG_EDITOR_DOCUMENT_NODE_NULL,
	LRG_EDITOR_DOCUMENT_NODE_SCALAR,
	LRG_EDITOR_DOCUMENT_NODE_MAPPING,
	LRG_EDITOR_DOCUMENT_NODE_SEQUENCE
} LrgEditorDocumentNodeKind;

#define LRG_TYPE_EDITOR_DOCUMENT_NODE_KIND (lrg_editor_document_node_kind_get_type ())

/**
 * lrg_editor_document_node_kind_get_type:
 *
 * Returns: the #GType of #LrgEditorDocumentNodeKind
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_document_node_kind_get_type (void) G_GNUC_CONST;

/**
 * LRG_EDITOR_DOCUMENT_ERROR:
 *
 * Error domain of #LrgEditorDocument. Errors in this domain are
 * #LrgEditorDocumentError values.
 */
#define LRG_EDITOR_DOCUMENT_ERROR (lrg_editor_document_error_quark ())

/**
 * LrgEditorDocumentError:
 * @LRG_EDITOR_DOCUMENT_ERROR_PARSE: the text is not valid YAML
 * @LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH: the path is malformed (no
 *   leading '/', a bad `~` escape, a non-numeric sequence index)
 * @LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND: the path, key or index does not exist
 * @LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND: the node is not of the kind the
 *   operation needs (e.g. inserting into a mapping)
 * @LRG_EDITOR_DOCUMENT_ERROR_EXISTS: the key being added or renamed to
 *   already exists
 * @LRG_EDITOR_DOCUMENT_ERROR_READ_ONLY: the document is read-only
 * @LRG_EDITOR_DOCUMENT_ERROR_NO_PATH: saving or reloading a document that
 *   has no file
 * @LRG_EDITOR_DOCUMENT_ERROR_GROUP_OPEN: undo, redo or save while an edit
 *   group is open
 * @LRG_EDITOR_DOCUMENT_ERROR_IO: reading or writing the file failed
 *
 * Error codes of %LRG_EDITOR_DOCUMENT_ERROR.
 */
typedef enum
{
	LRG_EDITOR_DOCUMENT_ERROR_PARSE,
	LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH,
	LRG_EDITOR_DOCUMENT_ERROR_NOT_FOUND,
	LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND,
	LRG_EDITOR_DOCUMENT_ERROR_EXISTS,
	LRG_EDITOR_DOCUMENT_ERROR_READ_ONLY,
	LRG_EDITOR_DOCUMENT_ERROR_NO_PATH,
	LRG_EDITOR_DOCUMENT_ERROR_GROUP_OPEN,
	LRG_EDITOR_DOCUMENT_ERROR_IO
} LrgEditorDocumentError;

#define LRG_TYPE_EDITOR_DOCUMENT_ERROR (lrg_editor_document_error_get_type ())

/**
 * lrg_editor_document_error_quark:
 *
 * Returns: the error quark of #LrgEditorDocument errors
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_editor_document_error_quark (void);

/**
 * lrg_editor_document_error_get_type:
 *
 * Returns: the #GType of #LrgEditorDocumentError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_document_error_get_type (void) G_GNUC_CONST;

/* ==========================================================================
 * Boxed types
 * ========================================================================== */

#define LRG_TYPE_EDITOR_DOCUMENT_ISSUE (lrg_editor_document_issue_get_type ())

/**
 * LrgEditorDocumentIssue:
 * @severity: how serious it is (shared with the level Problems panel)
 * @path: (nullable): YAML path of the offending node, best effort
 * @line: 1-based line in the serialized document (lrg_editor_document_to_data()),
 *   or 0 when unknown
 * @column: 1-based column, or 0 when unknown
 * @message: human-readable description
 *
 * One finding of lrg_editor_document_validate().
 */
typedef struct
{
	LrgEditorProblemSeverity  severity;
	gchar                    *path;
	guint                     line;
	guint                     column;
	gchar                    *message;
} LrgEditorDocumentIssue;

/**
 * lrg_editor_document_issue_get_type:
 *
 * Returns: the #GType of #LrgEditorDocumentIssue
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_document_issue_get_type (void) G_GNUC_CONST;

/**
 * lrg_editor_document_issue_new:
 * @severity: how serious it is
 * @path: (nullable): YAML path, or %NULL
 * @line: 1-based line, or 0
 * @column: 1-based column, or 0
 * @message: the message
 *
 * Returns: (transfer full): a new issue
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocumentIssue * lrg_editor_document_issue_new (LrgEditorProblemSeverity  severity,
                                                        const gchar              *path,
                                                        guint                     line,
                                                        guint                     column,
                                                        const gchar              *message);

/**
 * lrg_editor_document_issue_copy:
 * @issue: an issue
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocumentIssue * lrg_editor_document_issue_copy (const LrgEditorDocumentIssue *issue);

/**
 * lrg_editor_document_issue_free:
 * @issue: an issue
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_issue_free (LrgEditorDocumentIssue *issue);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgEditorDocumentIssue, lrg_editor_document_issue_free)

#define LRG_TYPE_EDITOR_DOCUMENT_EDGE (lrg_editor_document_edge_get_type ())

/**
 * LrgEditorDocumentEdge:
 * @from: id of the source dialog node
 * @to: id of the target dialog node
 * @response: index of the response that leads there, or -1 for the
 *   node's own `next` (or the implicit chain to the following node)
 * @path: (nullable): YAML path of the `next` value, or %NULL for an
 *   implicit chain edge
 *
 * One edge of a dialog tree (lrg_editor_document_dialog_edges()).
 */
typedef struct
{
	gchar *from;
	gchar *to;
	gint   response;
	gchar *path;
} LrgEditorDocumentEdge;

/**
 * lrg_editor_document_edge_get_type:
 *
 * Returns: the #GType of #LrgEditorDocumentEdge
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_document_edge_get_type (void) G_GNUC_CONST;

/**
 * lrg_editor_document_edge_copy:
 * @edge: an edge
 *
 * Returns: (transfer full): a copy
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocumentEdge * lrg_editor_document_edge_copy (const LrgEditorDocumentEdge *edge);

/**
 * lrg_editor_document_edge_free:
 * @edge: an edge
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_edge_free (LrgEditorDocumentEdge *edge);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgEditorDocumentEdge, lrg_editor_document_edge_free)

/* ==========================================================================
 * LrgEditorDocument
 * ========================================================================== */

#define LRG_TYPE_EDITOR_DOCUMENT (lrg_editor_document_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorDocument, lrg_editor_document, LRG, EDITOR_DOCUMENT, GObject)

/* --- construction --- */

/**
 * lrg_editor_document_new:
 * @format: the format of the new document
 *
 * Creates an untitled document holding the smallest valid file of
 * @format (for example `actions: {}` for an input map). It has no path
 * and is not dirty; use lrg_editor_document_save_as() to write it.
 *
 * Returns: (transfer full): a new #LrgEditorDocument
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocument * lrg_editor_document_new (LrgEditorDocumentFormat format);

/**
 * lrg_editor_document_new_from_file:
 * @path: (type filename): a YAML file
 * @error: return location for a #GError
 *
 * Opens a file. The format is detected from the file name and content
 * (lrg_editor_document_detect_format()); the document is read-only when
 * the file is not writable. Comments are captured so a save keeps them.
 *
 * Returns: (transfer full) (nullable): the document, or %NULL with
 *   @error set (%LRG_EDITOR_DOCUMENT_ERROR_IO or
 *   %LRG_EDITOR_DOCUMENT_ERROR_PARSE with the line and column)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocument * lrg_editor_document_new_from_file (const gchar  *path,
                                                       GError      **error);

/**
 * lrg_editor_document_new_from_data:
 * @data: YAML text
 * @length: length of @data in bytes, or -1 if nul-terminated
 * @format: the format, or %LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN to detect
 *   it from the content
 * @error: return location for a #GError
 *
 * Creates an untitled document from text.
 *
 * Returns: (transfer full) (nullable): the document, or %NULL with
 *   @error set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocument * lrg_editor_document_new_from_data (const gchar              *data,
                                                       gssize                    length,
                                                       LrgEditorDocumentFormat   format,
                                                       GError                  **error);

/**
 * lrg_editor_document_detect_format:
 * @filename: (nullable) (type filename): a file name, or %NULL
 * @data: (nullable): the file's YAML text, or %NULL
 * @length: length of @data, or -1 if nul-terminated
 *
 * Guesses the format. The engine asset extensions `.rmat` (material) and
 * `.rpost` (post stack) and `.rshadergraph` (shader graph) decide on
 * their own. Then a double extension wins (`*.input.yaml`,
 * `*.locale.yaml`, `*.bank.yaml`, `*.hud.yaml`, `*.ui.yaml`,
 * `*.dialog.yaml`, `*.dungeon.yaml`, `*.theme.yaml`), then the content
 * (the keys each loader requires), then the parent directory name
 * (`locales`, `themes`, `dialogs`, ...).
 *
 * Returns: the format, or %LRG_EDITOR_DOCUMENT_FORMAT_UNKNOWN
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocumentFormat lrg_editor_document_detect_format (const gchar *filename,
                                                           const gchar *data,
                                                           gssize       length);

/* --- properties --- */

/**
 * lrg_editor_document_get_format:
 * @self: an #LrgEditorDocument
 *
 * Returns: the document's format
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocumentFormat lrg_editor_document_get_format (LrgEditorDocument *self);

/**
 * lrg_editor_document_set_format:
 * @self: an #LrgEditorDocument
 * @format: the format
 *
 * Overrides the detected format (for a file detection got wrong). The
 * tree is not changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_set_format (LrgEditorDocument       *self,
                                     LrgEditorDocumentFormat  format);

/**
 * lrg_editor_document_get_path:
 * @self: an #LrgEditorDocument
 *
 * Returns: (transfer none) (nullable) (type filename): the file, or %NULL
 *   for an untitled document
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_document_get_path (LrgEditorDocument *self);

/**
 * lrg_editor_document_get_display_name:
 * @self: an #LrgEditorDocument
 *
 * Returns: (transfer none): the file's base name, or "untitled"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_document_get_display_name (LrgEditorDocument *self);

/**
 * lrg_editor_document_get_dirty:
 * @self: an #LrgEditorDocument
 *
 * Returns: whether the tree differs from the last save or load (undoing
 *   back to the saved state clears it)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_get_dirty (LrgEditorDocument *self);

/**
 * lrg_editor_document_get_read_only:
 * @self: an #LrgEditorDocument
 *
 * Returns: whether edits and saves are refused
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_get_read_only (LrgEditorDocument *self);

/**
 * lrg_editor_document_set_read_only:
 * @self: an #LrgEditorDocument
 * @read_only: whether edits and saves are refused
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_set_read_only (LrgEditorDocument *self,
                                        gboolean           read_only);

/**
 * lrg_editor_document_get_base_dir:
 * @self: an #LrgEditorDocument
 *
 * Returns: (transfer none) (nullable) (type filename): the directory
 *   relative files (sound bank entries) are resolved against, or %NULL
 *   to use the document's own directory
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_document_get_base_dir (LrgEditorDocument *self);

/**
 * lrg_editor_document_set_base_dir:
 * @self: an #LrgEditorDocument
 * @base_dir: (nullable) (type filename): a directory, or %NULL
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_set_base_dir (LrgEditorDocument *self,
                                       const gchar       *base_dir);

/**
 * lrg_editor_document_get_reference:
 * @self: an #LrgEditorDocument
 *
 * Returns: (transfer none) (nullable): the reference document (for a
 *   locale: the source language whose keys every translation must have)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocument * lrg_editor_document_get_reference (LrgEditorDocument *self);

/**
 * lrg_editor_document_set_reference:
 * @self: an #LrgEditorDocument
 * @reference: (nullable): a document of the same format, or %NULL
 *
 * Sets the document validation compares against. For locales, keys of
 * the reference missing here are warnings, extra keys are infos and a
 * different number of `%` placeholders is a warning.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_set_reference (LrgEditorDocument *self,
                                        LrgEditorDocument *reference);

/**
 * lrg_editor_document_get_monitor_file:
 * @self: an #LrgEditorDocument
 *
 * Returns: whether a #GFileMonitor watches the file
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_get_monitor_file (LrgEditorDocument *self);

/**
 * lrg_editor_document_set_monitor_file:
 * @self: an #LrgEditorDocument
 * @monitor: whether to watch the file
 *
 * With a monitor, a change on disk by another program emits
 * #LrgEditorDocument::file-changed from the main loop. Without one, call
 * lrg_editor_document_check_external_change() (for example on window
 * focus). Off by default.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_set_monitor_file (LrgEditorDocument *self,
                                           gboolean           monitor);

/* --- paths --- */

/**
 * lrg_editor_document_path_escape:
 * @token: a mapping key or sequence index
 *
 * Escapes one path segment: `~` becomes `~0` and `/` becomes `~1`
 * (JSON pointer, RFC 6901).
 *
 * Returns: (transfer full): the escaped segment
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_document_path_escape (const gchar *token);

/**
 * lrg_editor_document_path_join:
 * @parent: a path ("" or "/" is the root)
 * @token: an unescaped key or index
 *
 * Returns: (transfer full): @parent + "/" + the escaped @token
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_document_path_join (const gchar *parent,
                                       const gchar *token);

/**
 * lrg_editor_document_path_split:
 * @path: a path
 * @error: return location for a #GError
 *
 * Splits a path into unescaped segments. "" and "/" are the root (no
 * segments).
 *
 * Returns: (transfer full) (nullable): the segments, or %NULL with
 *   %LRG_EDITOR_DOCUMENT_ERROR_INVALID_PATH
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_document_path_split (const gchar  *path,
                                      GError      **error);

/* --- reading --- */

/**
 * lrg_editor_document_get_kind:
 * @self: an #LrgEditorDocument
 * @path: a YAML path
 *
 * Returns: what @path points at (%LRG_EDITOR_DOCUMENT_NODE_MISSING for
 *   a malformed or absent path)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorDocumentNodeKind lrg_editor_document_get_kind (LrgEditorDocument *self,
                                                        const gchar       *path);

/**
 * lrg_editor_document_has_path:
 * @self: an #LrgEditorDocument
 * @path: a YAML path
 *
 * Returns: whether @path exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_has_path (LrgEditorDocument *self,
                                       const gchar       *path);

/**
 * lrg_editor_document_get_scalar:
 * @self: an #LrgEditorDocument
 * @path: a YAML path
 *
 * Returns: (transfer none) (nullable): the scalar's text as written
 *   (numbers and booleans too), or %NULL for a null, a collection or a
 *   missing path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_document_get_scalar (LrgEditorDocument *self,
                                              const gchar       *path);

/**
 * lrg_editor_document_get_value:
 * @self: an #LrgEditorDocument
 * @path: a YAML path to a scalar or null
 * @value: (out caller-allocates): an uninitialised #GValue
 *
 * Reads a scalar typed the way YAML would: a plain `true`/`false` (also
 * `yes`/`no`/`on`/`off`) is a boolean, a plain integer a #gint64, a
 * plain float a #gdouble, anything else (including every quoted scalar)
 * a string. A null gives a %G_TYPE_POINTER value holding %NULL.
 *
 * Returns: %TRUE if @value was set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_get_value (LrgEditorDocument *self,
                                        const gchar       *path,
                                        GValue            *value);

/**
 * lrg_editor_document_get_keys:
 * @self: an #LrgEditorDocument
 * @path: a YAML path to a mapping
 *
 * Returns: (transfer full) (nullable): the mapping's keys in document
 *   order, or %NULL if @path is not a mapping
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_document_get_keys (LrgEditorDocument *self,
                                    const gchar       *path);

/**
 * lrg_editor_document_get_length:
 * @self: an #LrgEditorDocument
 * @path: a YAML path
 *
 * Returns: the number of items of a sequence or keys of a mapping, or
 *   -1 for anything else
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_document_get_length (LrgEditorDocument *self,
                                     const gchar       *path);

/**
 * lrg_editor_document_get_yaml:
 * @self: an #LrgEditorDocument
 * @path: a YAML path
 *
 * Serializes a subtree (with its comments), for a "copy" action or the
 * YAML pane of an editor.
 *
 * Returns: (transfer full) (nullable): YAML text, or %NULL if @path is
 *   missing
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_document_get_yaml (LrgEditorDocument *self,
                                      const gchar       *path);

/**
 * lrg_editor_document_to_data:
 * @self: an #LrgEditorDocument
 * @length: (out) (optional): length of the result
 *
 * Serializes the whole document exactly as lrg_editor_document_save()
 * writes it.
 *
 * Returns: (transfer full): YAML text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_document_to_data (LrgEditorDocument *self,
                                     gsize             *length);

/**
 * lrg_editor_document_get_line:
 * @self: an #LrgEditorDocument
 * @path: a YAML path
 * @line: (out) (optional): 1-based line
 * @column: (out) (optional): 1-based column
 *
 * Where @path starts in lrg_editor_document_to_data(); for a value in
 * a mapping that is the line of its key. Used to jump from a preview or an issue
 * to the YAML text.
 *
 * Returns: %TRUE if @path exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_get_line (LrgEditorDocument *self,
                                       const gchar       *path,
                                       guint             *line,
                                       guint             *column);

/**
 * lrg_editor_document_get_path_at_line:
 * @self: an #LrgEditorDocument
 * @line: a 1-based line of lrg_editor_document_to_data()
 *
 * The inverse of lrg_editor_document_get_line(): the deepest node that
 * starts on or before @line.
 *
 * Returns: (transfer full): a YAML path ("" for the root)
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_document_get_path_at_line (LrgEditorDocument *self,
                                              guint              line);

/* --- editing (each call is one undoable command) --- */

/**
 * lrg_editor_document_set_string:
 * @self: an #LrgEditorDocument
 * @path: a YAML path; a missing last mapping key is created at the end,
 *   `-` or the length appends to a sequence
 * @value: the string (written quoted when it would otherwise read as
 *   another type)
 * @gesture_id: 0, or an id shared by the steps of one gesture (a drag,
 *   typing in a field): consecutive sets of the same path with the same
 *   non-zero id merge into one undo step
 * @error: return location for a #GError
 *
 * Replaces the node at @path with a string scalar. Comments attached to
 * the old node are kept.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_set_string (LrgEditorDocument  *self,
                                         const gchar        *path,
                                         const gchar        *value,
                                         guint               gesture_id,
                                         GError            **error);

/**
 * lrg_editor_document_set_int:
 * @self: an #LrgEditorDocument
 * @path: a YAML path (see lrg_editor_document_set_string())
 * @value: the integer
 * @gesture_id: merge id (see lrg_editor_document_set_string())
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_set_int (LrgEditorDocument  *self,
                                      const gchar        *path,
                                      gint64              value,
                                      guint               gesture_id,
                                      GError            **error);

/**
 * lrg_editor_document_set_double:
 * @self: an #LrgEditorDocument
 * @path: a YAML path (see lrg_editor_document_set_string())
 * @value: the number
 * @gesture_id: merge id (see lrg_editor_document_set_string())
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_set_double (LrgEditorDocument  *self,
                                         const gchar        *path,
                                         gdouble             value,
                                         guint               gesture_id,
                                         GError            **error);

/**
 * lrg_editor_document_set_boolean:
 * @self: an #LrgEditorDocument
 * @path: a YAML path (see lrg_editor_document_set_string())
 * @value: the boolean
 * @gesture_id: merge id (see lrg_editor_document_set_string())
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_set_boolean (LrgEditorDocument  *self,
                                          const gchar        *path,
                                          gboolean            value,
                                          guint               gesture_id,
                                          GError            **error);

/**
 * lrg_editor_document_set_null:
 * @self: an #LrgEditorDocument
 * @path: a YAML path (see lrg_editor_document_set_string())
 * @gesture_id: merge id (see lrg_editor_document_set_string())
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_set_null (LrgEditorDocument  *self,
                                       const gchar        *path,
                                       guint               gesture_id,
                                       GError            **error);

/**
 * lrg_editor_document_set_value:
 * @self: an #LrgEditorDocument
 * @path: a YAML path (see lrg_editor_document_set_string())
 * @value: a string, boolean, integer (any width, signed or unsigned),
 *   float, double or enum (written as its nick) value; an unset value or
 *   a %NULL string/pointer writes a null
 * @gesture_id: merge id (see lrg_editor_document_set_string())
 * @error: return location for a #GError
 *
 * The typed setter a property grid uses.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_set_value (LrgEditorDocument  *self,
                                        const gchar        *path,
                                        const GValue       *value,
                                        guint               gesture_id,
                                        GError            **error);

/**
 * lrg_editor_document_replace:
 * @self: an #LrgEditorDocument
 * @path: a YAML path (as for lrg_editor_document_set_string(); "" replaces
 *   the whole document)
 * @yaml: YAML text of the new subtree
 * @error: return location for a #GError
 *
 * Replaces a subtree with parsed text (the YAML pane, paste).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_replace (LrgEditorDocument  *self,
                                      const gchar        *path,
                                      const gchar        *yaml,
                                      GError            **error);

/**
 * lrg_editor_document_replace_gesture:
 * @self: an #LrgEditorDocument
 * @path: a YAML path (as for lrg_editor_document_replace())
 * @yaml: YAML text of the new subtree
 * @gesture_id: 0, or the id shared by the steps of one gesture:
 *   consecutive replacements of the same path with the same non-zero id
 *   merge into one undo step (see lrg_editor_document_set_string())
 * @error: return location for a #GError
 *
 * lrg_editor_document_replace() for editors that rewrite a whole subtree
 * on every step of a drag (a physics asset part written in its canonical
 * form while a size is scrubbed). Since 0.9.0.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_replace_gesture (LrgEditorDocument  *self,
                                              const gchar        *path,
                                              const gchar        *yaml,
                                              guint               gesture_id,
                                              GError            **error);

/**
 * lrg_editor_document_insert_item:
 * @self: an #LrgEditorDocument
 * @path: a YAML path to a sequence
 * @index: where the item goes; -1 or the length appends
 * @yaml: (nullable): YAML text of the item; %NULL inserts a null
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_insert_item (LrgEditorDocument  *self,
                                          const gchar        *path,
                                          gint                index,
                                          const gchar        *yaml,
                                          GError            **error);

/**
 * lrg_editor_document_move_item:
 * @self: an #LrgEditorDocument
 * @path: a YAML path to a sequence
 * @from: index of the item
 * @to: its index after the move
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_move_item (LrgEditorDocument  *self,
                                        const gchar        *path,
                                        guint               from,
                                        guint               to,
                                        GError            **error);

/**
 * lrg_editor_document_add_key:
 * @self: an #LrgEditorDocument
 * @path: a YAML path to a mapping
 * @key: the new key
 * @yaml: (nullable): YAML text of its value; %NULL adds a null
 * @position: where the key goes in the mapping's order; -1 for the end
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success; %LRG_EDITOR_DOCUMENT_ERROR_EXISTS if @key
 *   is already there
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_add_key (LrgEditorDocument  *self,
                                      const gchar        *path,
                                      const gchar        *key,
                                      const gchar        *yaml,
                                      gint                position,
                                      GError            **error);

/**
 * lrg_editor_document_rename_key:
 * @self: an #LrgEditorDocument
 * @path: a YAML path to a mapping
 * @old_key: the key to rename
 * @new_key: its new name
 * @error: return location for a #GError
 *
 * Renames a key in place: its position, value and comments are kept.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_rename_key (LrgEditorDocument  *self,
                                         const gchar        *path,
                                         const gchar        *old_key,
                                         const gchar        *new_key,
                                         GError            **error);

/**
 * lrg_editor_document_remove:
 * @self: an #LrgEditorDocument
 * @path: a YAML path to a mapping value or sequence item (not the root)
 * @error: return location for a #GError
 *
 * Removes a key (with its value) or a sequence item.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_remove (LrgEditorDocument  *self,
                                     const gchar        *path,
                                     GError            **error);

/* --- undo --- */

/**
 * lrg_editor_document_begin_group:
 * @self: an #LrgEditorDocument
 * @label: (nullable): label of the compound step ("Add binding"), or %NULL
 *
 * Starts a compound edit: every edit until the matching
 * lrg_editor_document_end_group() becomes one undo step. Groups nest;
 * only the outermost one makes a step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_begin_group (LrgEditorDocument *self,
                                      const gchar       *label);

/**
 * lrg_editor_document_end_group:
 * @self: an #LrgEditorDocument
 *
 * Ends the innermost group. An empty outermost group adds no step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_end_group (LrgEditorDocument *self);

/**
 * lrg_editor_document_undo:
 * @self: an #LrgEditorDocument
 *
 * Returns: %TRUE if a step was undone (%FALSE with nothing to undo or a
 *   group open)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_undo (LrgEditorDocument *self);

/**
 * lrg_editor_document_redo:
 * @self: an #LrgEditorDocument
 *
 * Returns: %TRUE if a step was redone
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_redo (LrgEditorDocument *self);

/**
 * lrg_editor_document_can_undo:
 * @self: an #LrgEditorDocument
 *
 * Returns: whether lrg_editor_document_undo() would do something
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_can_undo (LrgEditorDocument *self);

/**
 * lrg_editor_document_can_redo:
 * @self: an #LrgEditorDocument
 *
 * Returns: whether lrg_editor_document_redo() would do something
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_can_redo (LrgEditorDocument *self);

/**
 * lrg_editor_document_get_undo_label:
 * @self: an #LrgEditorDocument
 *
 * Returns: (transfer none) (nullable): label of the step undo would
 *   revert ("Set /actions/jump/bindings/0/key"), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_document_get_undo_label (LrgEditorDocument *self);

/**
 * lrg_editor_document_get_redo_label:
 * @self: an #LrgEditorDocument
 *
 * Returns: (transfer none) (nullable): label of the step redo would
 *   apply, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_document_get_redo_label (LrgEditorDocument *self);

/**
 * lrg_editor_document_clear_undo:
 * @self: an #LrgEditorDocument
 *
 * Forgets the history. The dirty flag is kept as it is.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_clear_undo (LrgEditorDocument *self);

/**
 * lrg_editor_document_get_undo_limit:
 * @self: an #LrgEditorDocument
 *
 * Returns: the maximum number of undo steps kept (0 = unlimited)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_document_get_undo_limit (LrgEditorDocument *self);

/**
 * lrg_editor_document_set_undo_limit:
 * @self: an #LrgEditorDocument
 * @limit: the maximum number of undo steps (0 = unlimited); the oldest
 *   steps are dropped first
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_document_set_undo_limit (LrgEditorDocument *self,
                                         guint              limit);

/* --- files --- */

/**
 * lrg_editor_document_save:
 * @self: an #LrgEditorDocument
 * @error: return location for a #GError
 *
 * Writes lrg_editor_document_to_data() to the document's file atomically
 * (g_file_set_contents()), clears the dirty flag and emits
 * #LrgEditorDocument::saved.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_save (LrgEditorDocument  *self,
                                   GError            **error);

/**
 * lrg_editor_document_save_as:
 * @self: an #LrgEditorDocument
 * @path: (type filename): the new file
 * @error: return location for a #GError
 *
 * Saves to @path, which becomes the document's file.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_save_as (LrgEditorDocument  *self,
                                      const gchar        *path,
                                      GError            **error);

/**
 * lrg_editor_document_reload:
 * @self: an #LrgEditorDocument
 * @error: return location for a #GError
 *
 * Re-reads the file, dropping unsaved edits and the undo history, and
 * emits #LrgEditorDocument::reloaded. On failure the document is left
 * unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_reload (LrgEditorDocument  *self,
                                     GError            **error);

/**
 * lrg_editor_document_check_external_change:
 * @self: an #LrgEditorDocument
 *
 * Compares the file's modification time (and, when it moved, its
 * content) with the last load or save, and emits
 * #LrgEditorDocument::file-changed once per new external change.
 *
 * Returns: whether the file changed on disk since the last load or save
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_check_external_change (LrgEditorDocument *self);

/* --- validation --- */

/**
 * lrg_editor_document_validate:
 * @self: an #LrgEditorDocument
 * @issues: (out) (optional) (transfer container) (element-type LrgEditorDocumentIssue):
 *   the findings, errors first
 *
 * Runs the engine loader of the document's format on the serialized
 * text (from memory where the loader can, otherwise through a temporary
 * file) and the format's structural checks: duplicate keys found when
 * the file was read, unknown or duplicate bindings, missing locale keys
 * against the reference, dangling dialog references, missing sound files
 * and so on.
 *
 * Returns: %TRUE when there is no error-severity issue
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_validate (LrgEditorDocument  *self,
                                       GPtrArray         **issues);

/* --- engine-backed formats --- */

/**
 * lrg_editor_document_dup_object:
 * @self: a document of an engine-backed format
 *   (lrg_editor_document_format_get_object_type() is not %G_TYPE_INVALID)
 * @error: return location for a #GError
 *
 * Parses the current text with the engine's own loader (the base
 * directory, or the file's directory, resolves relative paths; textures
 * are not loaded) and returns a fresh object the caller may change and
 * hand back with lrg_editor_document_commit_object().
 *
 * Returns: (transfer full) (nullable): a new engine object of the
 *   format's object type, or %NULL with @error set (the loader's error,
 *   or %LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND for a plain YAML format)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_editor_document_dup_object (LrgEditorDocument  *self,
                                          GError            **error);

/**
 * lrg_editor_document_commit_object:
 * @self: a document of an engine-backed format
 * @object: an object of the format's object type
 * @label: (nullable): the undo label, or %NULL for "Edit"
 * @gesture: 0 for one undo step of its own, or a gesture id: consecutive
 *   commits with the same non-zero id (a drag, a slider) merge into one
 *   undo step
 * @error: return location for a #GError
 *
 * Writes @object with the engine's own writer and makes that text the
 * whole document, as one undoable command (the undo step restores the
 * previous text, so undo is a snapshot of the engine's output). Nothing
 * is recorded when the text would not change.
 *
 * Returns: %TRUE on success (also when nothing changed); %FALSE with
 *   @error set when the writer fails, the document is read-only or
 *   @object has the wrong type (%LRG_EDITOR_DOCUMENT_ERROR_WRONG_KIND)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_document_commit_object (LrgEditorDocument  *self,
                                            GObject            *object,
                                            const gchar        *label,
                                            guint               gesture,
                                            GError            **error);

/**
 * lrg_editor_document_to_file_data:
 * @self: a document
 * @length: (out) (optional): length of the text in bytes
 *
 * The text lrg_editor_document_save() writes. For a plain YAML format
 * this is lrg_editor_document_to_data(); for an engine-backed format it
 * is the engine writer's output for the parsed document (so a file the
 * engine wrote round-trips byte for byte), or the YAML text when the
 * engine cannot parse the document (the save still succeeds, validation
 * reports the error).
 *
 * Returns: (transfer full): the text
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_document_to_file_data (LrgEditorDocument *self,
                                          gsize             *length);

/* --- format helpers --- */

/**
 * lrg_editor_document_input_actions:
 * @self: an input map document
 *
 * Returns: (transfer full): the action names in document order (empty
 *   if there are none)
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_document_input_actions (LrgEditorDocument *self);

/**
 * lrg_editor_document_locale_keys:
 * @self: a locale document
 *
 * Returns: (transfer full): the keys of `strings` in document order (a
 *   plural entry is one key)
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_document_locale_keys (LrgEditorDocument *self);

/**
 * lrg_editor_document_locale_missing_keys:
 * @self: a locale document
 * @reference: the reference locale document
 *
 * Returns: (transfer full): keys of @reference that @self lacks, in the
 *   reference's order
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_document_locale_missing_keys (LrgEditorDocument *self,
                                               LrgEditorDocument *reference);

/**
 * lrg_editor_document_dialog_nodes:
 * @self: a dialog tree document
 *
 * Returns: (transfer full): the node ids in document order
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_editor_document_dialog_nodes (LrgEditorDocument *self);

/**
 * lrg_editor_document_dialog_edges:
 * @self: a dialog tree document
 *
 * Every `next` of every node and response, plus the implicit chain from
 * a node with neither `next` nor `responses` to the following node (the
 * loader's linear-scene rule). Targets may be missing ids.
 *
 * Returns: (transfer container) (element-type LrgEditorDocumentEdge): the edges
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_document_dialog_edges (LrgEditorDocument *self);

/**
 * lrg_editor_document_sound_bank_resolve:
 * @self: a sound bank document
 * @name: a key of `sounds`
 *
 * Resolves a sound's file the way #LrgSoundBank does: relative to
 * `base_path`, which is itself relative to the base directory (or the
 * document's directory, or the current directory).
 *
 * Returns: (transfer full) (nullable) (type filename): the file, or %NULL
 *   if @name is not a sound of the bank
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_document_sound_bank_resolve (LrgEditorDocument *self,
                                                const gchar       *name);

G_END_DECLS
