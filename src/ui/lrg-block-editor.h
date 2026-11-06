/* lrg-block-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBlockEditor: a Scratch-style visual programming editor. Programs
 * are scripts of blocks that snap together: a palette of block kinds
 * on one side, a pannable and zoomable workspace on the other.
 *
 * The model is two small GObjects defined here:
 *
 *  - #LrgBlockKind: what a block is (id, category, colour, shape and
 *    a label template with typed input slots such as
 *    "move {steps:number=10} steps");
 *  - #LrgBlock: one block in a program: its kind, its slot values,
 *    reporter/boolean blocks nested in its slots, the block below it
 *    and the blocks inside its mouths (C and E shapes).
 *
 * The editor owns the scripts (top-level blocks); every block owns
 * what hangs under it. Applications run programs by walking that tree
 * (see the documentation for a worked interpreter).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations and errors
 * ========================================================================== */

/**
 * LrgBlockShape:
 * @LRG_BLOCK_SHAPE_HAT: starts a script (round top, tab below), e.g.
 *   "when flag clicked"
 * @LRG_BLOCK_SHAPE_STATEMENT: a command (notch on top, tab below)
 * @LRG_BLOCK_SHAPE_C: a command with one mouth holding a stack, e.g.
 *   "repeat", "if"
 * @LRG_BLOCK_SHAPE_E: a command with two mouths, e.g. "if / else"
 * @LRG_BLOCK_SHAPE_CAP: ends a stack (notch on top, nothing below),
 *   e.g. "stop"
 * @LRG_BLOCK_SHAPE_REPORTER: a rounded value block that fits in
 *   number and text slots
 * @LRG_BLOCK_SHAPE_BOOLEAN: a hexagonal true/false block that fits in
 *   every slot except dropdowns
 *
 * The silhouette of a block, which decides where it can connect.
 */
typedef enum
{
	LRG_BLOCK_SHAPE_HAT,
	LRG_BLOCK_SHAPE_STATEMENT,
	LRG_BLOCK_SHAPE_C,
	LRG_BLOCK_SHAPE_E,
	LRG_BLOCK_SHAPE_CAP,
	LRG_BLOCK_SHAPE_REPORTER,
	LRG_BLOCK_SHAPE_BOOLEAN
} LrgBlockShape;

LRG_AVAILABLE_IN_ALL
GType lrg_block_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BLOCK_SHAPE (lrg_block_shape_get_type ())

/**
 * LrgBlockSlotType:
 * @LRG_BLOCK_SLOT_NUMBER: a round number field; takes reporters and
 *   booleans
 * @LRG_BLOCK_SLOT_TEXT: a text field; takes reporters and booleans
 * @LRG_BLOCK_SLOT_BOOLEAN: a hexagonal hole; takes boolean blocks only
 * @LRG_BLOCK_SLOT_DROPDOWN: a menu of fixed choices; takes no blocks
 *
 * The type of an input slot in a block label.
 */
typedef enum
{
	LRG_BLOCK_SLOT_NUMBER,
	LRG_BLOCK_SLOT_TEXT,
	LRG_BLOCK_SLOT_BOOLEAN,
	LRG_BLOCK_SLOT_DROPDOWN
} LrgBlockSlotType;

LRG_AVAILABLE_IN_ALL
GType lrg_block_slot_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BLOCK_SLOT_TYPE (lrg_block_slot_type_get_type ())

/**
 * LrgBlockPlace:
 * @LRG_BLOCK_PLACE_NONE: a script of its own on the workspace (or not
 *   in an editor)
 * @LRG_BLOCK_PLACE_NEXT: below its parent
 * @LRG_BLOCK_PLACE_MOUTH: first block in a mouth of its parent (the
 *   index is the mouth)
 * @LRG_BLOCK_PLACE_INPUT: in an input slot of its parent (the index
 *   is the slot)
 *
 * Where a block is attached.
 */
typedef enum
{
	LRG_BLOCK_PLACE_NONE,
	LRG_BLOCK_PLACE_NEXT,
	LRG_BLOCK_PLACE_MOUTH,
	LRG_BLOCK_PLACE_INPUT
} LrgBlockPlace;

LRG_AVAILABLE_IN_ALL
GType lrg_block_place_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BLOCK_PLACE (lrg_block_place_get_type ())

/**
 * LrgBlockYamlParts:
 * @LRG_BLOCK_YAML_PALETTE: the categories and block kinds
 * @LRG_BLOCK_YAML_PROGRAM: the view and the scripts
 * @LRG_BLOCK_YAML_ALL: both
 *
 * Which sections lrg_block_editor_save_yaml() writes.
 */
typedef enum /*< flags >*/
{
	LRG_BLOCK_YAML_PALETTE = 1 << 0,
	LRG_BLOCK_YAML_PROGRAM = 1 << 1,
	LRG_BLOCK_YAML_ALL     = LRG_BLOCK_YAML_PALETTE | LRG_BLOCK_YAML_PROGRAM
} LrgBlockYamlParts;

LRG_AVAILABLE_IN_ALL
GType lrg_block_yaml_parts_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BLOCK_YAML_PARTS (lrg_block_yaml_parts_get_type ())

/**
 * LrgBlockEditorError:
 * @LRG_BLOCK_EDITOR_ERROR_LABEL: a label template is malformed
 * @LRG_BLOCK_EDITOR_ERROR_PARSE: a YAML document is malformed
 * @LRG_BLOCK_EDITOR_ERROR_UNKNOWN_KIND: a block names a kind the
 *   palette does not have
 * @LRG_BLOCK_EDITOR_ERROR_INVALID: blocks are put where their shapes
 *   or slot types do not allow
 * @LRG_BLOCK_EDITOR_ERROR_IO: a file could not be read or written
 *
 * Errors of #LrgBlockKind and #LrgBlockEditor.
 */
typedef enum
{
	LRG_BLOCK_EDITOR_ERROR_LABEL,
	LRG_BLOCK_EDITOR_ERROR_PARSE,
	LRG_BLOCK_EDITOR_ERROR_UNKNOWN_KIND,
	LRG_BLOCK_EDITOR_ERROR_INVALID,
	LRG_BLOCK_EDITOR_ERROR_IO
} LrgBlockEditorError;

LRG_AVAILABLE_IN_ALL
GType lrg_block_editor_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BLOCK_EDITOR_ERROR (lrg_block_editor_error_get_type ())

/**
 * LRG_BLOCK_EDITOR_ERROR:
 *
 * Error domain of #LrgBlockEditorError.
 */
#define LRG_BLOCK_EDITOR_ERROR (lrg_block_editor_error_quark ())

/**
 * lrg_block_editor_error_quark:
 *
 * Returns: the #LrgBlockEditorError domain
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_block_editor_error_quark (void);

/* ==========================================================================
 * Types
 * ========================================================================== */

#define LRG_TYPE_BLOCK_KIND (lrg_block_kind_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBlockKind, lrg_block_kind, LRG, BLOCK_KIND, GObject)

#define LRG_TYPE_BLOCK (lrg_block_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBlock, lrg_block, LRG, BLOCK, GObject)

#define LRG_TYPE_BLOCK_EDITOR (lrg_block_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBlockEditor, lrg_block_editor, LRG, BLOCK_EDITOR, LrgWidget)

/* ==========================================================================
 * LrgBlockKind
 * ========================================================================== */

/**
 * lrg_block_kind_new:
 * @id: unique id of the kind, e.g. "move"
 * @shape: the block shape
 * @category: (nullable): palette category, e.g. "motion"
 * @label: the label template: words with input slots written
 *   `{name:type}` or `{name:type=default}` where type is `number`,
 *   `text`, `boolean` or `dropdown` (choices separated by `|`, the
 *   first is the default: `{key:dropdown=space|left|right}`). In an
 *   %LRG_BLOCK_SHAPE_E label, a `|` outside braces starts the second
 *   row ("if {c:boolean} then|else"). `\` escapes the next character.
 * @error: return location for an %LRG_BLOCK_EDITOR_ERROR_LABEL
 *
 * Creates a block kind. Kinds are immutable apart from their category,
 * colour and dropdown choices.
 *
 * Returns: (transfer full) (nullable): the kind, %NULL if @label is
 *   malformed
 */
LRG_AVAILABLE_IN_ALL
LrgBlockKind * lrg_block_kind_new (const gchar    *id,
                                   LrgBlockShape   shape,
                                   const gchar    *category,
                                   const gchar    *label,
                                   GError        **error);

/**
 * lrg_block_kind_get_id:
 * @self: an #LrgBlockKind
 *
 * Returns: (transfer none): the id
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_block_kind_get_id (LrgBlockKind *self);

/**
 * lrg_block_kind_get_shape:
 * @self: an #LrgBlockKind
 *
 * Returns: the shape
 */
LRG_AVAILABLE_IN_ALL
LrgBlockShape lrg_block_kind_get_shape (LrgBlockKind *self);

/**
 * lrg_block_kind_get_label:
 * @self: an #LrgBlockKind
 *
 * Returns: (transfer none): the label template as given
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_block_kind_get_label (LrgBlockKind *self);

/**
 * lrg_block_kind_get_category:
 * @self: an #LrgBlockKind
 *
 * Returns: (transfer none) (nullable): the palette category
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_block_kind_get_category (LrgBlockKind *self);

/**
 * lrg_block_kind_set_category:
 * @self: an #LrgBlockKind
 * @category: (nullable): the palette category
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_kind_set_category (LrgBlockKind *self,
                                  const gchar  *category);

/**
 * lrg_block_kind_get_color:
 * @self: an #LrgBlockKind
 * @color: (out caller-allocates) (optional): the explicit colour
 *
 * Returns: %TRUE if the kind has its own colour (otherwise its
 *   category's colour is used)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_kind_get_color (LrgBlockKind *self,
                                   GrlColor     *color);

/**
 * lrg_block_kind_set_color:
 * @self: an #LrgBlockKind
 * @color: (nullable): a colour overriding the category's, %NULL to
 *   follow the category again
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_kind_set_color (LrgBlockKind   *self,
                               const GrlColor *color);

/**
 * lrg_block_kind_get_n_slots:
 * @self: an #LrgBlockKind
 *
 * Returns: the number of input slots in the label
 */
LRG_AVAILABLE_IN_ALL
guint lrg_block_kind_get_n_slots (LrgBlockKind *self);

/**
 * lrg_block_kind_get_n_mouths:
 * @self: an #LrgBlockKind
 *
 * Returns: 1 for %LRG_BLOCK_SHAPE_C, 2 for %LRG_BLOCK_SHAPE_E, else 0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_block_kind_get_n_mouths (LrgBlockKind *self);

/**
 * lrg_block_kind_find_slot:
 * @self: an #LrgBlockKind
 * @name: a slot name
 *
 * Returns: the index of the slot called @name, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_block_kind_find_slot (LrgBlockKind *self,
                               const gchar  *name);

/**
 * lrg_block_kind_get_slot_name:
 * @self: an #LrgBlockKind
 * @slot: a slot index
 *
 * Returns: (transfer none): the slot's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_block_kind_get_slot_name (LrgBlockKind *self,
                                            guint         slot);

/**
 * lrg_block_kind_get_slot_type:
 * @self: an #LrgBlockKind
 * @slot: a slot index
 *
 * Returns: the slot's type
 */
LRG_AVAILABLE_IN_ALL
LrgBlockSlotType lrg_block_kind_get_slot_type (LrgBlockKind *self,
                                               guint         slot);

/**
 * lrg_block_kind_get_slot_default:
 * @self: an #LrgBlockKind
 * @slot: a slot index
 *
 * Returns: (transfer none): the value new blocks start with ("0" for
 *   number slots without a default, "" for text and boolean slots,
 *   the first choice for dropdowns)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_block_kind_get_slot_default (LrgBlockKind *self,
                                               guint         slot);

/**
 * lrg_block_kind_get_slot_choices:
 * @self: an #LrgBlockKind
 * @slot: a slot index
 *
 * Returns: (transfer none) (array zero-terminated=1) (nullable): the
 *   choices of a dropdown slot, %NULL for other slots
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_block_kind_get_slot_choices (LrgBlockKind *self,
                                                       guint         slot);

/**
 * lrg_block_kind_set_slot_choices:
 * @self: an #LrgBlockKind
 * @slot: a dropdown slot index
 * @choices: (array zero-terminated=1): the new choices (at least one),
 *   e.g. the sprite names of a game. The default becomes the first.
 *
 * Replaces the choices of a dropdown slot. Values of existing blocks
 * are kept even when they are no longer a choice.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_kind_set_slot_choices (LrgBlockKind        *self,
                                      guint                slot,
                                      const gchar * const *choices);

/* ==========================================================================
 * LrgBlock
 * ========================================================================== */

/**
 * lrg_block_new:
 * @kind: the block's kind
 *
 * Creates a free block holding the kind's default values. Put it in
 * an editor with lrg_block_editor_add_script().
 *
 * Returns: (transfer full): a new #LrgBlock
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_new (LrgBlockKind *kind);

/**
 * lrg_block_get_kind:
 * @self: an #LrgBlock
 *
 * Returns: (transfer none): the kind
 */
LRG_AVAILABLE_IN_ALL
LrgBlockKind * lrg_block_get_kind (LrgBlock *self);

/**
 * lrg_block_get_kind_id:
 * @self: an #LrgBlock
 *
 * Returns: (transfer none): the id of the block's kind, what an
 *   interpreter switches on
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_block_get_kind_id (LrgBlock *self);

/**
 * lrg_block_get_editor:
 * @self: an #LrgBlock
 *
 * Returns: (transfer none) (nullable): the editor holding the block
 */
LRG_AVAILABLE_IN_ALL
LrgBlockEditor * lrg_block_get_editor (LrgBlock *self);

/**
 * lrg_block_get_parent:
 * @self: an #LrgBlock
 *
 * Returns: (transfer none) (nullable): the block it is attached to;
 *   %NULL for the top block of a script
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_get_parent (LrgBlock *self);

/**
 * lrg_block_get_place:
 * @self: an #LrgBlock
 * @index: (out) (optional): the mouth or slot index
 *
 * Returns: where the block is attached to its parent
 */
LRG_AVAILABLE_IN_ALL
LrgBlockPlace lrg_block_get_place (LrgBlock *self,
                                   guint    *index);

/**
 * lrg_block_get_top:
 * @self: an #LrgBlock
 *
 * Returns: (transfer none): the top block of the script holding @self
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_get_top (LrgBlock *self);

/**
 * lrg_block_get_next:
 * @self: an #LrgBlock
 *
 * Returns: (transfer none) (nullable): the block below, run after
 *   this one
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_get_next (LrgBlock *self);

/**
 * lrg_block_get_mouth:
 * @self: an #LrgBlock
 * @mouth: 0, or 1 for the second mouth of an E block
 *
 * Returns: (transfer none) (nullable): the first block of the stack
 *   in the mouth
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_get_mouth (LrgBlock *self,
                                guint     mouth);

/**
 * lrg_block_get_input:
 * @self: an #LrgBlock
 * @slot: a slot index
 *
 * Returns: (transfer none) (nullable): the reporter or boolean block
 *   in the slot; when there is one it replaces the slot's own value
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_get_input (LrgBlock *self,
                                guint     slot);

/**
 * lrg_block_get_input_by_name:
 * @self: an #LrgBlock
 * @name: a slot name
 *
 * Returns: (transfer none) (nullable): the block in the named slot
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_get_input_by_name (LrgBlock    *self,
                                        const gchar *name);

/**
 * lrg_block_get_value:
 * @self: an #LrgBlock
 * @slot: a slot index
 *
 * Returns: (transfer none): the slot's own value as text (kept while a
 *   block sits in the slot)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_block_get_value (LrgBlock *self,
                                   guint     slot);

/**
 * lrg_block_get_value_by_name:
 * @self: an #LrgBlock
 * @name: a slot name
 *
 * Returns: (transfer none) (nullable): the named slot's value, %NULL
 *   when the kind has no such slot
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_block_get_value_by_name (LrgBlock    *self,
                                           const gchar *name);

/**
 * lrg_block_get_number:
 * @self: an #LrgBlock
 * @name: a slot name
 *
 * Returns: the named slot's value as a number (0 when it is not one)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_block_get_number (LrgBlock    *self,
                              const gchar *name);

/**
 * lrg_block_get_value_variant:
 * @self: an #LrgBlock
 * @slot: a slot index
 *
 * The slot's value typed by its slot: a double ("d") for number slots
 * (0 when not a number), a boolean ("b", "true" is %TRUE) for boolean
 * slots and a string ("s") otherwise.
 *
 * Returns: (transfer full): a new floating-free #GVariant
 */
LRG_AVAILABLE_IN_ALL
GVariant * lrg_block_get_value_variant (LrgBlock *self,
                                        guint     slot);

/**
 * lrg_block_set_value:
 * @self: an #LrgBlock
 * @slot: a slot index
 * @value: the new text
 *
 * Sets a slot's value. Inside an editor this is an undoable edit and
 * emits #LrgBlockEditor::changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_set_value (LrgBlock    *self,
                          guint        slot,
                          const gchar *value);

/**
 * lrg_block_set_value_by_name:
 * @self: an #LrgBlock
 * @name: a slot name
 * @value: the new text
 *
 * Returns: %FALSE when the kind has no slot called @name
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_set_value_by_name (LrgBlock    *self,
                                      const gchar *name,
                                      const gchar *value);

/**
 * lrg_block_get_position:
 * @self: an #LrgBlock
 * @x: (out) (optional): workspace x of a script's top block
 * @y: (out) (optional): workspace y
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_get_position (LrgBlock *self,
                             gfloat   *x,
                             gfloat   *y);

/**
 * lrg_block_get_running:
 * @self: an #LrgBlock
 *
 * Returns: %TRUE if the block is highlighted as executing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_get_running (LrgBlock *self);

/**
 * lrg_block_set_running:
 * @self: an #LrgBlock
 * @running: show the block as executing
 *
 * Highlights the block with a glow (it fades in and out), so a game
 * can show which step its interpreter is running. On the top block of
 * a script the glow surrounds the whole script. Not an edit: it is
 * neither saved nor undone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_set_running (LrgBlock *self,
                            gboolean  running);

/**
 * lrg_block_get_selected:
 * @self: an #LrgBlock
 *
 * Returns: %TRUE if the block is the editor's selected block
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_get_selected (LrgBlock *self);

/* ==========================================================================
 * LrgBlockEditor: palette
 * ========================================================================== */

/**
 * lrg_block_editor_new:
 *
 * Returns: (transfer full): a new, empty #LrgBlockEditor
 */
LRG_AVAILABLE_IN_ALL
LrgBlockEditor * lrg_block_editor_new (void);

/**
 * lrg_block_editor_add_kind:
 * @self: an #LrgBlockEditor
 * @kind: a kind (not in another editor)
 *
 * Adds a kind to the palette, after the kinds of its category (a new
 * category is appended).
 *
 * Returns: %FALSE if a kind with the same id exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_add_kind (LrgBlockEditor *self,
                                    LrgBlockKind   *kind);

/**
 * lrg_block_editor_define_kind:
 * @self: an #LrgBlockEditor
 * @id: unique id
 * @shape: the shape
 * @category: (nullable): palette category
 * @label: the label template, see lrg_block_kind_new()
 * @error: return location for a #GError
 *
 * lrg_block_kind_new() and lrg_block_editor_add_kind() in one call.
 *
 * Returns: (transfer none) (nullable): the new kind
 */
LRG_AVAILABLE_IN_ALL
LrgBlockKind * lrg_block_editor_define_kind (LrgBlockEditor  *self,
                                             const gchar     *id,
                                             LrgBlockShape    shape,
                                             const gchar     *category,
                                             const gchar     *label,
                                             GError         **error);

/**
 * lrg_block_editor_remove_kind:
 * @self: an #LrgBlockEditor
 * @id: a kind id
 *
 * Returns: %FALSE if there is no such kind or blocks of it are in the
 *   program
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_remove_kind (LrgBlockEditor *self,
                                       const gchar    *id);

/**
 * lrg_block_editor_get_kind:
 * @self: an #LrgBlockEditor
 * @id: a kind id
 *
 * Returns: (transfer none) (nullable): the kind
 */
LRG_AVAILABLE_IN_ALL
LrgBlockKind * lrg_block_editor_get_kind (LrgBlockEditor *self,
                                          const gchar    *id);

/**
 * lrg_block_editor_get_n_kinds:
 * @self: an #LrgBlockEditor
 *
 * Returns: the number of kinds in the palette
 */
LRG_AVAILABLE_IN_ALL
guint lrg_block_editor_get_n_kinds (LrgBlockEditor *self);

/**
 * lrg_block_editor_get_kinds:
 * @self: an #LrgBlockEditor
 *
 * Returns: (transfer container) (element-type LrgBlockKind): the kinds
 *   in palette order
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_block_editor_get_kinds (LrgBlockEditor *self);

/**
 * lrg_block_editor_clear_palette:
 * @self: an #LrgBlockEditor
 *
 * Removes the program, every kind and every category.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_clear_palette (LrgBlockEditor *self);

/**
 * lrg_block_editor_add_category:
 * @self: an #LrgBlockEditor
 * @name: a category name
 * @color: (nullable): its colour, %NULL for the theme-aware default
 *
 * Adds a category (palette groups follow the order categories were
 * added in), or changes the colour of an existing one.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_add_category (LrgBlockEditor *self,
                                    const gchar    *name,
                                    const GrlColor *color);

/**
 * lrg_block_editor_get_categories:
 * @self: an #LrgBlockEditor
 *
 * Returns: (transfer full) (array zero-terminated=1): the category
 *   names in palette order
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_block_editor_get_categories (LrgBlockEditor *self);

/**
 * lrg_block_editor_get_category_color:
 * @self: an #LrgBlockEditor
 * @name: (nullable): a category name
 * @color: (out caller-allocates): the colour blocks of the category use
 *
 * Resolves the explicit colour, else a default: well-known Scratch
 * category names ("motion", "looks", "sound", "events", "control",
 * "sensing", "operators", "variables", "lists", "custom") get their
 * familiar colours, others a colour picked by name. Defaults are
 * toned down slightly on dark themes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_get_category_color (LrgBlockEditor *self,
                                          const gchar    *name,
                                          GrlColor       *color);

/**
 * lrg_block_editor_set_category_color:
 * @self: an #LrgBlockEditor
 * @name: a category name (added if new)
 * @color: (nullable): the colour, %NULL for the default
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_set_category_color (LrgBlockEditor *self,
                                          const gchar    *name,
                                          const GrlColor *color);

/* ==========================================================================
 * LrgBlockEditor: program
 * ========================================================================== */

/**
 * lrg_block_editor_create_block:
 * @self: an #LrgBlockEditor
 * @kind_id: the kind of the block
 * @x: workspace x
 * @y: workspace y
 *
 * Adds a new block as a script of its own (undoable).
 *
 * Returns: (transfer none) (nullable): the block, %NULL for an unknown
 *   kind
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_editor_create_block (LrgBlockEditor *self,
                                          const gchar    *kind_id,
                                          gfloat          x,
                                          gfloat          y);

/**
 * lrg_block_editor_add_script:
 * @self: an #LrgBlockEditor
 * @block: a free block (lrg_block_new()) of a kind in the palette
 * @x: workspace x
 * @y: workspace y
 *
 * Adds a free block as a script of its own (undoable).
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_add_script (LrgBlockEditor *self,
                                  LrgBlock       *block,
                                  gfloat          x,
                                  gfloat          y);

/**
 * lrg_block_editor_can_attach:
 * @self: an #LrgBlockEditor
 * @block: a block of the program
 * @target: another block of the program (not inside @block)
 * @place: %LRG_BLOCK_PLACE_NEXT, %LRG_BLOCK_PLACE_MOUTH or
 *   %LRG_BLOCK_PLACE_INPUT
 * @index: the mouth or slot index
 *
 * The connection rules: statement-like blocks (statement, C, E, cap)
 * go below hats, statements, C and E blocks or into mouths; a hat only
 * starts a script; reporters and booleans go into number and text
 * slots and booleans also into boolean slots; nothing goes into a
 * dropdown. A stack ending with a cap cannot be inserted above other
 * blocks.
 *
 * Returns: %TRUE if lrg_block_editor_attach() would succeed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_can_attach (LrgBlockEditor *self,
                                      LrgBlock       *block,
                                      LrgBlock       *target,
                                      LrgBlockPlace   place,
                                      guint           index);

/**
 * lrg_block_editor_attach:
 * @self: an #LrgBlockEditor
 * @block: a block of the program
 * @target: the block to attach to
 * @place: where, see lrg_block_editor_can_attach()
 * @index: the mouth or slot index
 *
 * Moves @block, with the blocks below it, to @target (undoable). The
 * blocks that were at the place hang under the moved stack; a lone C
 * or E block with an empty first mouth wraps them instead. A block
 * already in an input slot is pushed out beside @target.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_attach (LrgBlockEditor *self,
                                  LrgBlock       *block,
                                  LrgBlock       *target,
                                  LrgBlockPlace   place,
                                  guint           index);

/**
 * lrg_block_editor_detach:
 * @self: an #LrgBlockEditor
 * @block: a block of the program
 * @x: workspace x
 * @y: workspace y
 *
 * Makes @block, with the blocks below it, a script of its own at
 * (@x, @y) (undoable). On a top block it just moves the script.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_detach (LrgBlockEditor *self,
                              LrgBlock       *block,
                              gfloat          x,
                              gfloat          y);

/**
 * lrg_block_editor_remove_block:
 * @self: an #LrgBlockEditor
 * @block: a block of the program
 * @with_following: also remove the blocks below @block
 *
 * Removes @block with its inputs and mouths (undoable). Without
 * @with_following the blocks below close the gap.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_remove_block (LrgBlockEditor *self,
                                    LrgBlock       *block,
                                    gboolean        with_following);

/**
 * lrg_block_editor_clear_program:
 * @self: an #LrgBlockEditor
 *
 * Removes every script (undoable).
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_clear_program (LrgBlockEditor *self);

/**
 * lrg_block_editor_get_n_scripts:
 * @self: an #LrgBlockEditor
 *
 * Returns: the number of scripts (top blocks)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_block_editor_get_n_scripts (LrgBlockEditor *self);

/**
 * lrg_block_editor_get_nth_script:
 * @self: an #LrgBlockEditor
 * @index: a script index (drawing order, the last on top)
 *
 * Returns: (transfer none) (nullable): the top block of the script
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_editor_get_nth_script (LrgBlockEditor *self,
                                            guint           index);

/**
 * lrg_block_editor_get_scripts:
 * @self: an #LrgBlockEditor
 *
 * Returns: (transfer container) (element-type LrgBlock): the top
 *   blocks of the scripts
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_block_editor_get_scripts (LrgBlockEditor *self);

/**
 * lrg_block_editor_get_n_blocks:
 * @self: an #LrgBlockEditor
 *
 * Returns: the number of blocks in the program
 */
LRG_AVAILABLE_IN_ALL
guint lrg_block_editor_get_n_blocks (LrgBlockEditor *self);

/**
 * lrg_block_editor_clear_running:
 * @self: an #LrgBlockEditor
 *
 * Turns off every lrg_block_set_running() highlight.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_clear_running (LrgBlockEditor *self);

/**
 * lrg_block_editor_clean_up:
 * @self: an #LrgBlockEditor
 *
 * Arranges the scripts in a tidy column, top to bottom in their
 * current vertical order (undoable; they glide into place).
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_clean_up (LrgBlockEditor *self);

/* ==========================================================================
 * LrgBlockEditor: selection and clipboard
 * ========================================================================== */

/**
 * lrg_block_editor_get_selected:
 * @self: an #LrgBlockEditor
 *
 * Returns: (transfer none) (nullable): the selected block
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_editor_get_selected (LrgBlockEditor *self);

/**
 * lrg_block_editor_select_block:
 * @self: an #LrgBlockEditor
 * @block: (nullable): a block of the program, %NULL for none
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_select_block (LrgBlockEditor *self,
                                    LrgBlock       *block);

/**
 * lrg_block_editor_delete_selection:
 * @self: an #LrgBlockEditor
 *
 * Removes the selected block (the blocks below close the gap), like
 * the Delete key.
 *
 * Returns: %TRUE if a block was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_delete_selection (LrgBlockEditor *self);

/**
 * lrg_block_editor_duplicate_selection:
 * @self: an #LrgBlockEditor
 *
 * Copies the selected block with the blocks below it into a new script
 * beside it and selects the copy (Ctrl+D).
 *
 * Returns: (transfer none) (nullable): the copy's top block
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_editor_duplicate_selection (LrgBlockEditor *self);

/**
 * lrg_block_editor_copy_selection:
 * @self: an #LrgBlockEditor
 *
 * Copies the selected block with the blocks below it to the editor's
 * clipboard (Ctrl+C).
 *
 * Returns: %TRUE if something was copied
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_copy_selection (LrgBlockEditor *self);

/**
 * lrg_block_editor_paste:
 * @self: an #LrgBlockEditor
 *
 * Pastes the clipboard as a new script at the pointer (or near the
 * top-left of the view) and selects it (Ctrl+V).
 *
 * Returns: (transfer none) (nullable): the pasted top block
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_editor_paste (LrgBlockEditor *self);

/* ==========================================================================
 * LrgBlockEditor: undo
 * ========================================================================== */

/**
 * lrg_block_editor_undo:
 * @self: an #LrgBlockEditor
 *
 * Reverts the last edit (Ctrl+Z). Blocks keep their identity: a block
 * removed and brought back is the same object.
 *
 * Returns: %TRUE if there was something to undo
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_undo (LrgBlockEditor *self);

/**
 * lrg_block_editor_redo:
 * @self: an #LrgBlockEditor
 *
 * Re-applies the last undone edit (Ctrl+Shift+Z, Ctrl+Y).
 *
 * Returns: %TRUE if there was something to redo
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_redo (LrgBlockEditor *self);

/**
 * lrg_block_editor_can_undo:
 * @self: an #LrgBlockEditor
 *
 * Returns: %TRUE if lrg_block_editor_undo() would do something
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_can_undo (LrgBlockEditor *self);

/**
 * lrg_block_editor_can_redo:
 * @self: an #LrgBlockEditor
 *
 * Returns: %TRUE if lrg_block_editor_redo() would do something
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_can_redo (LrgBlockEditor *self);

/**
 * lrg_block_editor_clear_history:
 * @self: an #LrgBlockEditor
 *
 * Forgets every undo and redo step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_clear_history (LrgBlockEditor *self);

/**
 * lrg_block_editor_get_undo_limit:
 * @self: an #LrgBlockEditor
 *
 * Returns: the most undo steps kept
 */
LRG_AVAILABLE_IN_ALL
guint lrg_block_editor_get_undo_limit (LrgBlockEditor *self);

/**
 * lrg_block_editor_set_undo_limit:
 * @self: an #LrgBlockEditor
 * @limit: the most undo steps kept (default 200, at least 1)
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_set_undo_limit (LrgBlockEditor *self,
                                      guint           limit);

/* ==========================================================================
 * LrgBlockEditor: slot editing
 * ========================================================================== */

/**
 * lrg_block_editor_begin_slot_edit:
 * @self: an #LrgBlockEditor
 * @block: a block of the program
 * @slot: a number or text slot without a block in it, or a dropdown
 *
 * Starts typing into a slot (its text is selected, so typing replaces
 * it), like clicking it; a dropdown slot opens its menu instead.
 *
 * Returns: %TRUE if editing started or the menu opened
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_begin_slot_edit (LrgBlockEditor *self,
                                           LrgBlock       *block,
                                           guint           slot);

/**
 * lrg_block_editor_commit_slot_edit:
 * @self: an #LrgBlockEditor
 *
 * Keeps what was typed (a number slot keeps only valid numbers) and
 * stops editing (Enter, or clicking elsewhere).
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_commit_slot_edit (LrgBlockEditor *self);

/**
 * lrg_block_editor_cancel_slot_edit:
 * @self: an #LrgBlockEditor
 *
 * Restores the slot's value and stops editing (Escape).
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_cancel_slot_edit (LrgBlockEditor *self);

/**
 * lrg_block_editor_get_edit_slot:
 * @self: an #LrgBlockEditor
 * @block: (out) (optional) (transfer none): the block being edited
 * @slot: (out) (optional): its slot
 *
 * Returns: %TRUE while a slot is being typed into
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_get_edit_slot (LrgBlockEditor  *self,
                                         LrgBlock       **block,
                                         guint           *slot);

/**
 * lrg_block_editor_get_edit_text:
 * @self: an #LrgBlockEditor
 *
 * Returns: (transfer none) (nullable): the text typed so far
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_block_editor_get_edit_text (LrgBlockEditor *self);

/* ==========================================================================
 * LrgBlockEditor: view and geometry
 * ========================================================================== */

/**
 * lrg_block_editor_get_zoom:
 * @self: an #LrgBlockEditor
 *
 * Returns: screen units per workspace unit
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_block_editor_get_zoom (LrgBlockEditor *self);

/**
 * lrg_block_editor_set_zoom:
 * @self: an #LrgBlockEditor
 * @zoom: the zoom, clamped to the zoom range; keeps the workspace's
 *   top-left corner in place
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_set_zoom (LrgBlockEditor *self,
                                gfloat          zoom);

/**
 * lrg_block_editor_zoom_at:
 * @self: an #LrgBlockEditor
 * @x: screen (world) x of the fixed point
 * @y: screen (world) y of the fixed point
 * @zoom: the new zoom
 *
 * Zooms keeping the workspace point under (@x, @y) in place, like
 * Ctrl+wheel and pinch.
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_zoom_at (LrgBlockEditor *self,
                               gfloat          x,
                               gfloat          y,
                               gfloat          zoom);

/**
 * lrg_block_editor_get_scroll:
 * @self: an #LrgBlockEditor
 * @x: (out) (optional): workspace x at the workspace's left edge
 * @y: (out) (optional): workspace y at its top edge
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_get_scroll (LrgBlockEditor *self,
                                  gfloat         *x,
                                  gfloat         *y);

/**
 * lrg_block_editor_set_scroll:
 * @self: an #LrgBlockEditor
 * @x: workspace x to show at the left edge
 * @y: workspace y to show at the top edge
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_set_scroll (LrgBlockEditor *self,
                                  gfloat          x,
                                  gfloat          y);

/**
 * lrg_block_editor_pan_by:
 * @self: an #LrgBlockEditor
 * @dx: screen units to move the content right
 * @dy: screen units to move the content down
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_pan_by (LrgBlockEditor *self,
                              gfloat          dx,
                              gfloat          dy);

/**
 * lrg_block_editor_set_zoom_range:
 * @self: an #LrgBlockEditor
 * @min_zoom: smallest zoom (default 0.3)
 * @max_zoom: largest zoom (default 3)
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_set_zoom_range (LrgBlockEditor *self,
                                      gfloat          min_zoom,
                                      gfloat          max_zoom);

/**
 * lrg_block_editor_get_min_zoom:
 * @self: an #LrgBlockEditor
 *
 * Returns: the smallest zoom
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_block_editor_get_min_zoom (LrgBlockEditor *self);

/**
 * lrg_block_editor_get_max_zoom:
 * @self: an #LrgBlockEditor
 *
 * Returns: the largest zoom
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_block_editor_get_max_zoom (LrgBlockEditor *self);

/**
 * lrg_block_editor_workspace_to_screen:
 * @self: an #LrgBlockEditor
 * @wx: workspace x
 * @wy: workspace y
 * @sx: (out) (optional): screen (world) x
 * @sy: (out) (optional): screen (world) y
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_workspace_to_screen (LrgBlockEditor *self,
                                           gfloat          wx,
                                           gfloat          wy,
                                           gfloat         *sx,
                                           gfloat         *sy);

/**
 * lrg_block_editor_screen_to_workspace:
 * @self: an #LrgBlockEditor
 * @sx: screen (world) x
 * @sy: screen (world) y
 * @wx: (out) (optional): workspace x
 * @wy: (out) (optional): workspace y
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_screen_to_workspace (LrgBlockEditor *self,
                                           gfloat          sx,
                                           gfloat          sy,
                                           gfloat         *wx,
                                           gfloat         *wy);

/**
 * lrg_block_editor_get_workspace_bounds:
 * @self: an #LrgBlockEditor
 * @x: (out) (optional): screen x of the workspace area
 * @y: (out) (optional): screen y
 * @width: (out) (optional): width
 * @height: (out) (optional): height
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_get_workspace_bounds (LrgBlockEditor *self,
                                            gfloat         *x,
                                            gfloat         *y,
                                            gfloat         *width,
                                            gfloat         *height);

/**
 * lrg_block_editor_get_palette_bounds:
 * @self: an #LrgBlockEditor
 * @x: (out) (optional): screen x of the palette (with its category
 *   strip)
 * @y: (out) (optional): screen y
 * @width: (out) (optional): width (0 when hidden)
 * @height: (out) (optional): height
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_get_palette_bounds (LrgBlockEditor *self,
                                          gfloat         *x,
                                          gfloat         *y,
                                          gfloat         *width,
                                          gfloat         *height);

/**
 * lrg_block_editor_get_block_bounds:
 * @self: an #LrgBlockEditor
 * @block: a block of the program
 * @x: (out) (optional): screen x where the block is shown
 * @y: (out) (optional): screen y
 * @width: (out) (optional): width of the block itself (not the blocks
 *   below it)
 * @height: (out) (optional): height (with its mouths for C and E)
 *
 * Returns: %FALSE if @block is not in this editor
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_get_block_bounds (LrgBlockEditor *self,
                                            LrgBlock       *block,
                                            gfloat         *x,
                                            gfloat         *y,
                                            gfloat         *width,
                                            gfloat         *height);

/**
 * lrg_block_editor_get_slot_bounds:
 * @self: an #LrgBlockEditor
 * @block: a block of the program
 * @slot: a slot index
 * @x: (out) (optional): screen x of the slot (or of the block in it)
 * @y: (out) (optional): screen y
 * @width: (out) (optional): width
 * @height: (out) (optional): height
 *
 * Returns: %FALSE if @block is not in this editor
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_get_slot_bounds (LrgBlockEditor *self,
                                           LrgBlock       *block,
                                           guint           slot,
                                           gfloat         *x,
                                           gfloat         *y,
                                           gfloat         *width,
                                           gfloat         *height);

/**
 * lrg_block_editor_get_palette_block_bounds:
 * @self: an #LrgBlockEditor
 * @kind_id: a kind id
 * @x: (out) (optional): screen x of the kind's palette block
 * @y: (out) (optional): screen y
 * @width: (out) (optional): width
 * @height: (out) (optional): height
 *
 * Returns: %FALSE if there is no such kind or the palette is hidden
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_get_palette_block_bounds (LrgBlockEditor *self,
                                                    const gchar    *kind_id,
                                                    gfloat         *x,
                                                    gfloat         *y,
                                                    gfloat         *width,
                                                    gfloat         *height);

/**
 * lrg_block_editor_block_at:
 * @self: an #LrgBlockEditor
 * @x: screen (world) x
 * @y: screen (world) y
 * @slot: (out) (optional): the empty slot under the point, or -1
 *
 * Returns: (transfer none) (nullable): the innermost program block
 *   under the point
 */
LRG_AVAILABLE_IN_ALL
LrgBlock * lrg_block_editor_block_at (LrgBlockEditor *self,
                                      gfloat          x,
                                      gfloat          y,
                                      gint           *slot);

/**
 * lrg_block_editor_scroll_palette_to:
 * @self: an #LrgBlockEditor
 * @category: a category name
 *
 * Glides the palette to the category's group, like clicking it in the
 * category strip.
 *
 * Returns: %FALSE for an unknown or empty category
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_scroll_palette_to (LrgBlockEditor *self,
                                             const gchar    *category);

/* ==========================================================================
 * LrgBlockEditor: settings
 * ========================================================================== */

/**
 * lrg_block_editor_get_grid_size:
 * @self: an #LrgBlockEditor
 *
 * Returns: the spacing of the background dots in workspace units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_block_editor_get_grid_size (LrgBlockEditor *self);

/**
 * lrg_block_editor_set_grid_size:
 * @self: an #LrgBlockEditor
 * @size: dot spacing (default 24)
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_set_grid_size (LrgBlockEditor *self,
                                     gfloat          size);

/**
 * lrg_block_editor_get_show_grid:
 * @self: an #LrgBlockEditor
 *
 * Returns: %TRUE if the dot grid is drawn
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_get_show_grid (LrgBlockEditor *self);

/**
 * lrg_block_editor_set_show_grid:
 * @self: an #LrgBlockEditor
 * @show: draw the dot grid
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_set_show_grid (LrgBlockEditor *self,
                                     gboolean        show);

/**
 * lrg_block_editor_get_show_palette:
 * @self: an #LrgBlockEditor
 *
 * Returns: %TRUE if the palette is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_get_show_palette (LrgBlockEditor *self);

/**
 * lrg_block_editor_set_show_palette:
 * @self: an #LrgBlockEditor
 * @show: show the palette and its category strip (a hidden palette
 *   gives a read-only-ish program view: blocks still move)
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_set_show_palette (LrgBlockEditor *self,
                                        gboolean        show);

/**
 * lrg_block_editor_get_palette_width:
 * @self: an #LrgBlockEditor
 *
 * Returns: the width of the block list, without the category strip
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_block_editor_get_palette_width (LrgBlockEditor *self);

/**
 * lrg_block_editor_set_palette_width:
 * @self: an #LrgBlockEditor
 * @width: width of the block list (default 220)
 */
LRG_AVAILABLE_IN_ALL
void lrg_block_editor_set_palette_width (LrgBlockEditor *self,
                                         gfloat          width);

/* ==========================================================================
 * LrgBlockEditor: YAML
 * ========================================================================== */

/**
 * lrg_block_editor_save_yaml:
 * @self: an #LrgBlockEditor
 * @parts: what to write
 * @length: (out) (optional): length of the result
 * @error: return location for a #GError
 *
 * Writes the palette (categories, kinds) and/or the program (view and
 * scripts with every slot value and nested block). Loading the result
 * and saving again gives the same text.
 *
 * Returns: (transfer full) (nullable): the YAML document
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_block_editor_save_yaml (LrgBlockEditor     *self,
                                    LrgBlockYamlParts   parts,
                                    gsize              *length,
                                    GError            **error);

/**
 * lrg_block_editor_save_yaml_to_file:
 * @self: an #LrgBlockEditor
 * @parts: what to write
 * @path: the file to write
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_save_yaml_to_file (LrgBlockEditor     *self,
                                             LrgBlockYamlParts   parts,
                                             const gchar        *path,
                                             GError            **error);

/**
 * lrg_block_editor_load_yaml:
 * @self: an #LrgBlockEditor
 * @data: a document from lrg_block_editor_save_yaml() or written by hand
 * @length: length of @data, or -1 if NUL-terminated
 * @error: return location for a #GError
 *
 * Loads the sections the document has: `categories`/`kinds` replace
 * the palette, `scripts` replace the program (`view` sets the view).
 * A palette-only document keeps the current program when every block
 * of it still fits the new kinds. Everything is validated before
 * anything changes, so a bad document leaves the editor untouched.
 * The undo history is cleared.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_load_yaml (LrgBlockEditor  *self,
                                     const gchar     *data,
                                     gssize           length,
                                     GError         **error);

/**
 * lrg_block_editor_load_yaml_from_file:
 * @self: an #LrgBlockEditor
 * @path: the file to read
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_block_editor_load_yaml_from_file (LrgBlockEditor  *self,
                                               const gchar     *path,
                                               GError         **error);

G_END_DECLS
