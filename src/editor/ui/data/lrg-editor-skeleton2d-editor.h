/* lrg-editor-skeleton2d-editor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorSkeleton2DEditor: the lrgeditor 2D rigging and animation
 * editor for ".rskel2d" files (#LrgSkeleton2D).
 *
 * A data editor (#LrgEditorDataEditor) whose content is a canvas with the
 * textured rig, a side bar (outline of bones, slots in draw order, skins,
 * attachments and clips, plus a property grid of the selection) and, in
 * animation mode, a dope sheet (#LrgTimelineView) with a curve editor
 * (#LrgCurveEdit) for the selected timeline. Every edit is one undo step
 * of the edited #LrgEditorDocument: the editor changes a working
 * #LrgSkeleton2D (or a scratch copy of the YAML), serialises it and
 * applies only the YAML nodes that changed, so comments elsewhere in the
 * file survive.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../../lrg-version.h"
#include "../../../animation/lrg-skeleton2d.h"
#include "lrg-editor-data-editor.h"

G_BEGIN_DECLS

/**
 * LrgEditorSkeleton2DMode:
 * @LRG_EDITOR_SKELETON2D_MODE_SETUP: rigging: edits change the setup pose,
 *   the slots, skins and meshes
 * @LRG_EDITOR_SKELETON2D_MODE_ANIMATE: animating: the selected clip poses
 *   the rig at the playhead, bone edits on the canvas key the clip
 *
 * The two modes of the 2D rigging editor.
 */
typedef enum
{
	LRG_EDITOR_SKELETON2D_MODE_SETUP,
	LRG_EDITOR_SKELETON2D_MODE_ANIMATE
} LrgEditorSkeleton2DMode;

/**
 * lrg_editor_skeleton2d_mode_get_type:
 *
 * Returns: the #GType of #LrgEditorSkeleton2DMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_skeleton2d_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_SKELETON2D_MODE (lrg_editor_skeleton2d_mode_get_type ())

/**
 * LrgEditorSkeleton2DTool:
 * @LRG_EDITOR_SKELETON2D_TOOL_SELECT: pick bones and slots; drag a bone's
 *   joint to move it, its body to rotate it
 * @LRG_EDITOR_SKELETON2D_TOOL_BONE: drag on the canvas to create a bone,
 *   parented to the selected bone (setup mode only)
 * @LRG_EDITOR_SKELETON2D_TOOL_MESH: edit the vertices of the selected
 *   attachment: click to add (the mesh is triangulated again), drag to
 *   move, Delete removes (setup mode only)
 * @LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS: paint the selected bone's weights on
 *   the selected attachment; Shift erases (setup mode only)
 *
 * The canvas tools of the 2D rigging editor.
 */
typedef enum
{
	LRG_EDITOR_SKELETON2D_TOOL_SELECT,
	LRG_EDITOR_SKELETON2D_TOOL_BONE,
	LRG_EDITOR_SKELETON2D_TOOL_MESH,
	LRG_EDITOR_SKELETON2D_TOOL_WEIGHTS
} LrgEditorSkeleton2DTool;

/**
 * lrg_editor_skeleton2d_tool_get_type:
 *
 * Returns: the #GType of #LrgEditorSkeleton2DTool
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_skeleton2d_tool_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_SKELETON2D_TOOL (lrg_editor_skeleton2d_tool_get_type ())

#define LRG_TYPE_EDITOR_SKELETON2D_EDITOR (lrg_editor_skeleton2d_editor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSkeleton2DEditor, lrg_editor_skeleton2d_editor, LRG, EDITOR_SKELETON2D_EDITOR,
                      LrgEditorDataEditor)

/* ==========================================================================
 * Construction, opening, IPC
 * ========================================================================== */

/**
 * lrg_editor_skeleton2d_editor_new:
 * @app: (nullable): the editor application
 * @document: a ".rskel2d" document
 *
 * Creates the editor without docking it (lrg_editor_skeleton2d_editor_open()
 * and lrg_editor_data_editor_open() dock it as a tab).
 *
 * Returns: (transfer full): a new #LrgEditorSkeleton2DEditor
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSkeleton2DEditor * lrg_editor_skeleton2d_editor_new (LrgEditorApp      *app,
                                                              LrgEditorDocument *document);

/**
 * lrg_editor_skeleton2d_editor_open:
 * @app: the editor application
 * @path: a ".rskel2d" file, absolute or relative to the project
 * @error: return location for a #GError
 *
 * Opens @path in a 2D rigging editor tab (or presents the open one).
 *
 * Returns: (transfer none) (nullable): the editor, or %NULL with @error set
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSkeleton2DEditor * lrg_editor_skeleton2d_editor_open (LrgEditorApp  *app,
                                                               const gchar   *path,
                                                               GError       **error);

/**
 * lrg_editor_skeleton2d_editor_create:
 * @app: the editor application
 * @path: the new file, absolute or relative to the project
 * @name: (nullable): the rig's `name:` (defaults to the file's base name)
 * @error: return location for a #GError
 *
 * Writes a new rig with one root bone (the New 2D Skeleton template). An
 * existing file is not overwritten (%G_IO_ERROR_EXISTS).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_create (LrgEditorApp  *app,
                                              const gchar   *path,
                                              const gchar   *name,
                                              GError       **error);

/**
 * lrg_editor_skeleton2d_editor_install:
 * @app: the editor application
 *
 * Registers the skel2d.* IPC methods on @app's server (and the data.*
 * methods through lrg_editor_data_editor_install()). Safe to call more
 * than once; the editor calls it when it is constructed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_install (LrgEditorApp *app);

/* ==========================================================================
 * Model and view state
 * ========================================================================== */

/**
 * lrg_editor_skeleton2d_editor_get_skeleton:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * The working skeleton, loaded from the document after every change. Read
 * it; change the rig through the editor functions so edits are undoable.
 *
 * Returns: (transfer none) (nullable): the skeleton, %NULL while the
 *   document does not load (see lrg_editor_skeleton2d_editor_get_parse_error())
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2D * lrg_editor_skeleton2d_editor_get_skeleton (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_get_parse_error:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: (nullable): why the document does not load as a skeleton, or
 *   %NULL when it does
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_skeleton2d_editor_get_parse_error (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_get_mode:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: the current mode
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSkeleton2DMode lrg_editor_skeleton2d_editor_get_mode (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_set_mode:
 * @self: a #LrgEditorSkeleton2DEditor
 * @mode: the mode
 *
 * Switches between rigging and animating. Animation mode shows the dope
 * sheet and selects the first clip when none is selected; the bone, mesh
 * and weight tools fall back to the select tool.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_set_mode (LrgEditorSkeleton2DEditor *self,
                                            LrgEditorSkeleton2DMode    mode);

/**
 * lrg_editor_skeleton2d_editor_get_tool:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: the active canvas tool
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSkeleton2DTool lrg_editor_skeleton2d_editor_get_tool (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_set_tool:
 * @self: a #LrgEditorSkeleton2DEditor
 * @tool: the tool
 *
 * Activates a canvas tool. The bone, mesh and weight tools only work in
 * setup mode; setting one switches to setup mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_set_tool (LrgEditorSkeleton2DEditor *self,
                                            LrgEditorSkeleton2DTool    tool);

/**
 * lrg_editor_skeleton2d_editor_get_selected_bone:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: the selected bone index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_skeleton2d_editor_get_selected_bone (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_set_selected_bone:
 * @self: a #LrgEditorSkeleton2DEditor
 * @bone: a bone index, or -1 for none
 *
 * Selects a bone (the parent of the next bone the bone tool creates, the
 * bone the weight brush paints).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_set_selected_bone (LrgEditorSkeleton2DEditor *self,
                                                     gint                       bone);

/**
 * lrg_editor_skeleton2d_editor_get_selected_slot:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: the selected slot index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_skeleton2d_editor_get_selected_slot (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_set_selected_slot:
 * @self: a #LrgEditorSkeleton2DEditor
 * @slot: a slot index, or -1 for none
 *
 * Selects a slot; its current attachment becomes the selected attachment.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_set_selected_slot (LrgEditorSkeleton2DEditor *self,
                                                     gint                       slot);

/**
 * lrg_editor_skeleton2d_editor_get_selected_attachment:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: (nullable): the name of the selected attachment (of the
 *   selected slot), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_skeleton2d_editor_get_selected_attachment (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_select_attachment:
 * @self: a #LrgEditorSkeleton2DEditor
 * @slot: a slot index
 * @name: (nullable): an attachment name of @slot in the active or default
 *   skin, or %NULL for none
 *
 * Selects a slot and one of its attachments (what the mesh and weight
 * tools edit).
 *
 * Returns: %TRUE when the attachment exists (or @name is %NULL)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_select_attachment (LrgEditorSkeleton2DEditor *self,
                                                         gint                       slot,
                                                         const gchar               *name);

/**
 * lrg_editor_skeleton2d_editor_get_skin:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: the active skin (shown on the canvas, receiving new attachments)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_skeleton2d_editor_get_skin (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_set_skin:
 * @self: a #LrgEditorSkeleton2DEditor
 * @name: (nullable): a skin name, %NULL for "default"
 *
 * Switches the active skin (a view setting, not saved).
 *
 * Returns: %TRUE when the skin exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_set_skin (LrgEditorSkeleton2DEditor *self,
                                                const gchar               *name);

/**
 * lrg_editor_skeleton2d_editor_get_view:
 * @self: a #LrgEditorSkeleton2DEditor
 * @zoom: (out) (optional): canvas pixels per setup unit
 * @pan_x: (out) (optional): offset of the setup origin from the canvas
 *   centre, in pixels
 * @pan_y: (out) (optional): vertical offset
 *
 * Reads the canvas view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_get_view (LrgEditorSkeleton2DEditor *self,
                                            gfloat                    *zoom,
                                            gfloat                    *pan_x,
                                            gfloat                    *pan_y);

/**
 * lrg_editor_skeleton2d_editor_set_view:
 * @self: a #LrgEditorSkeleton2DEditor
 * @zoom: canvas pixels per setup unit (clamped to 0.05 .. 40)
 * @pan_x: offset of the setup origin from the canvas centre
 * @pan_y: vertical offset
 *
 * Sets the canvas view (the wheel zooms about the pointer, the middle
 * button pans).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_set_view (LrgEditorSkeleton2DEditor *self,
                                            gfloat                     zoom,
                                            gfloat                     pan_x,
                                            gfloat                     pan_y);

/**
 * lrg_editor_skeleton2d_editor_frame_all:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Fits the view to the bones and attachments (the F key).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_frame_all (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_setup_to_canvas:
 * @self: a #LrgEditorSkeleton2DEditor
 * @x: setup-space x
 * @y: setup-space y
 * @canvas_x: (out): canvas x (the UI canvas's coordinates)
 * @canvas_y: (out): canvas y
 *
 * Maps a setup-space point to where the canvas shows it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_setup_to_canvas (LrgEditorSkeleton2DEditor *self,
                                                   gfloat                     x,
                                                   gfloat                     y,
                                                   gfloat                    *canvas_x,
                                                   gfloat                    *canvas_y);

/**
 * lrg_editor_skeleton2d_editor_canvas_to_setup:
 * @self: a #LrgEditorSkeleton2DEditor
 * @canvas_x: canvas x
 * @canvas_y: canvas y
 * @x: (out): setup-space x
 * @y: (out): setup-space y
 *
 * The inverse of lrg_editor_skeleton2d_editor_setup_to_canvas().
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_canvas_to_setup (LrgEditorSkeleton2DEditor *self,
                                                   gfloat                     canvas_x,
                                                   gfloat                     canvas_y,
                                                   gfloat                    *x,
                                                   gfloat                    *y);

/* ==========================================================================
 * Bones
 * ========================================================================== */

/**
 * lrg_editor_skeleton2d_editor_add_bone:
 * @self: a #LrgEditorSkeleton2DEditor
 * @name: (nullable): a unique bone name, %NULL for "bone1", "bone2", ...
 * @parent: the parent bone index, or -1 for a root bone
 * @x: setup translation relative to the parent
 * @y: setup translation relative to the parent
 * @rotation: setup rotation in degrees (clockwise on screen)
 * @length: bone length (>= 0)
 * @error: return location for a #GError
 *
 * Adds a bone (one undo step) and selects it.
 *
 * Returns: the new bone's index, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_skeleton2d_editor_add_bone (LrgEditorSkeleton2DEditor  *self,
                                            const gchar                *name,
                                            gint                        parent,
                                            gfloat                      x,
                                            gfloat                      y,
                                            gfloat                      rotation,
                                            gfloat                      length,
                                            GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_set_bone:
 * @self: a #LrgEditorSkeleton2DEditor
 * @bone: a bone index
 * @x: setup x
 * @y: setup y
 * @rotation: setup rotation in degrees
 * @scale_x: setup scale x
 * @scale_y: setup scale y
 * @length: length (>= 0)
 * @error: return location for a #GError
 *
 * Sets a bone's setup pose and length (one undo step). Meshes keep their
 * place in the setup pose and deform from the new joint.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_set_bone (LrgEditorSkeleton2DEditor  *self,
                                                gint                        bone,
                                                gfloat                      x,
                                                gfloat                      y,
                                                gfloat                      rotation,
                                                gfloat                      scale_x,
                                                gfloat                      scale_y,
                                                gfloat                      length,
                                                GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_rename_bone:
 * @self: a #LrgEditorSkeleton2DEditor
 * @bone: a bone index
 * @name: the new, unique name
 * @error: return location for a #GError
 *
 * Renames a bone everywhere it is referenced (parents, slots, mesh
 * weights, clip timelines) in one undo step.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_rename_bone (LrgEditorSkeleton2DEditor  *self,
                                                   gint                        bone,
                                                   const gchar                *name,
                                                   GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_remove_bone:
 * @self: a #LrgEditorSkeleton2DEditor
 * @bone: a bone index
 * @error: return location for a #GError
 *
 * Removes a bone with its descendants, the slots on them (with their
 * attachments in every skin), their mesh weights and their clip
 * timelines, in one undo step. The last bone of a rig cannot be removed.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_remove_bone (LrgEditorSkeleton2DEditor  *self,
                                                   gint                        bone,
                                                   GError                    **error);

/* ==========================================================================
 * Slots and draw order
 * ========================================================================== */

/**
 * lrg_editor_skeleton2d_editor_add_slot:
 * @self: a #LrgEditorSkeleton2DEditor
 * @name: (nullable): a unique slot name, %NULL for "slot1", ...
 * @bone: the slot's bone index
 * @attachment: (nullable): the setup attachment
 * @error: return location for a #GError
 *
 * Appends a slot (front of the draw order) and selects it.
 *
 * Returns: the slot index, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_skeleton2d_editor_add_slot (LrgEditorSkeleton2DEditor  *self,
                                            const gchar                *name,
                                            gint                        bone,
                                            const gchar                *attachment,
                                            GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_set_slot_attachment:
 * @self: a #LrgEditorSkeleton2DEditor
 * @slot: a slot index
 * @attachment: (nullable): the setup attachment, %NULL hides the slot
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_set_slot_attachment (LrgEditorSkeleton2DEditor  *self,
                                                           gint                        slot,
                                                           const gchar                *attachment,
                                                           GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_set_slot_color:
 * @self: a #LrgEditorSkeleton2DEditor
 * @slot: a slot index
 * @rgba: (array fixed-size=4): the setup colour, 0 .. 1
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_set_slot_color (LrgEditorSkeleton2DEditor  *self,
                                                      gint                        slot,
                                                      const gfloat               *rgba,
                                                      GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_rename_slot:
 * @self: a #LrgEditorSkeleton2DEditor
 * @slot: a slot index
 * @name: the new, unique name
 * @error: return location for a #GError
 *
 * Renames a slot everywhere (attachments, clip timelines, draw-order keys).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_rename_slot (LrgEditorSkeleton2DEditor  *self,
                                                   gint                        slot,
                                                   const gchar                *name,
                                                   GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_remove_slot:
 * @self: a #LrgEditorSkeleton2DEditor
 * @slot: a slot index
 * @error: return location for a #GError
 *
 * Removes a slot, its attachments in every skin, its clip timelines and
 * its name from draw-order keys.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_remove_slot (LrgEditorSkeleton2DEditor  *self,
                                                   gint                        slot,
                                                   GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_move_slot:
 * @self: a #LrgEditorSkeleton2DEditor
 * @slot: a slot index
 * @delta: steps toward the front (positive) or the back (negative)
 * @error: return location for a #GError
 *
 * Moves a slot in the setup draw order (the order of the slots list).
 *
 * Returns: %TRUE when the slot moved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_move_slot (LrgEditorSkeleton2DEditor  *self,
                                                 gint                        slot,
                                                 gint                        delta,
                                                 GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_set_draw_order:
 * @self: a #LrgEditorSkeleton2DEditor
 * @order: (array zero-terminated=1): every slot name once, back to front
 * @error: return location for a #GError
 *
 * Reorders the slots list, which is the setup draw order.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_set_draw_order (LrgEditorSkeleton2DEditor  *self,
                                                      const gchar * const        *order,
                                                      GError                    **error);

/* ==========================================================================
 * Skins and attachments
 * ========================================================================== */

/**
 * lrg_editor_skeleton2d_editor_add_skin:
 * @self: a #LrgEditorSkeleton2DEditor
 * @name: a new skin name
 * @error: return location for a #GError
 *
 * Adds an empty skin and makes it active. An empty skin is not written
 * to the file until it holds an attachment.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_add_skin (LrgEditorSkeleton2DEditor  *self,
                                                const gchar                *name,
                                                GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_remove_skin:
 * @self: a #LrgEditorSkeleton2DEditor
 * @name: a skin other than "default"
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_remove_skin (LrgEditorSkeleton2DEditor  *self,
                                                   const gchar                *name,
                                                   GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_add_attachment:
 * @self: a #LrgEditorSkeleton2DEditor
 * @slot: a slot index
 * @name: (nullable): the attachment name, %NULL for the image's base name
 * @image: (nullable): an image path (absolute, project-relative or
 *   relative to the rig); stored relative to the rig
 * @x: left edge in setup space
 * @y: top edge in setup space
 * @width: width in setup units, 0 for the image's width
 * @height: height in setup units, 0 for the image's height
 * @columns: grid columns (1 for a quad)
 * @rows: grid rows (1 for a quad)
 * @error: return location for a #GError
 *
 * Adds a quad or grid mesh attachment to the active skin, rigid on the
 * slot's bone (weights can be computed or painted later), makes it the
 * slot's setup attachment when the slot showed none, and selects it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_add_attachment (LrgEditorSkeleton2DEditor  *self,
                                                      gint                        slot,
                                                      const gchar                *name,
                                                      const gchar                *image,
                                                      gfloat                      x,
                                                      gfloat                      y,
                                                      gfloat                      width,
                                                      gfloat                      height,
                                                      guint                       columns,
                                                      guint                       rows,
                                                      GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_drop_image:
 * @self: a #LrgEditorSkeleton2DEditor
 * @image: an image path (absolute, project-relative or relative to the rig)
 * @x: setup-space x of the image centre
 * @y: setup-space y of the image centre
 * @error: return location for a #GError
 *
 * What dropping an image from the assets panel on the canvas does: the
 * image becomes a quad attachment (named after the file) centred on
 * (@x, @y), on the selected slot, or on a new slot (named after the file)
 * of the selected bone (the first bone when none is selected). One undo
 * step.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_drop_image (LrgEditorSkeleton2DEditor  *self,
                                                  const gchar                *image,
                                                  gfloat                      x,
                                                  gfloat                      y,
                                                  GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_remove_attachment:
 * @self: a #LrgEditorSkeleton2DEditor
 * @slot: a slot index
 * @name: the attachment name (in the active skin, else the default skin)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_remove_attachment (LrgEditorSkeleton2DEditor  *self,
                                                         gint                        slot,
                                                         const gchar                *name,
                                                         GError                    **error);

/* ==========================================================================
 * Mesh and weights (of the selected attachment)
 * ========================================================================== */

/**
 * lrg_editor_skeleton2d_editor_add_vertex:
 * @self: a #LrgEditorSkeleton2DEditor
 * @x: setup-space x
 * @y: setup-space y
 * @error: return location for a #GError
 *
 * Adds a vertex to the selected attachment (its texture coordinate is
 * fitted from the existing vertices so the image stays in place) and
 * triangulates the mesh again.
 *
 * Returns: the vertex index, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_skeleton2d_editor_add_vertex (LrgEditorSkeleton2DEditor  *self,
                                              gfloat                      x,
                                              gfloat                      y,
                                              GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_move_vertex:
 * @self: a #LrgEditorSkeleton2DEditor
 * @index: a vertex index of the selected attachment
 * @x: new setup-space x
 * @y: new setup-space y
 * @error: return location for a #GError
 *
 * Moves a vertex; its texture coordinate follows so the image does not
 * slide.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_move_vertex (LrgEditorSkeleton2DEditor  *self,
                                                   guint                       index,
                                                   gfloat                      x,
                                                   gfloat                      y,
                                                   GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_remove_vertex:
 * @self: a #LrgEditorSkeleton2DEditor
 * @index: a vertex index of the selected attachment
 * @error: return location for a #GError
 *
 * Removes a vertex and triangulates the mesh again.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_remove_vertex (LrgEditorSkeleton2DEditor  *self,
                                                     guint                       index,
                                                     GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_triangulate:
 * @self: a #LrgEditorSkeleton2DEditor
 * @error: return location for a #GError
 *
 * Replaces the triangles of the selected attachment with a Delaunay
 * triangulation of its vertices, dropping triangles that lie outside the
 * image.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_triangulate (LrgEditorSkeleton2DEditor  *self,
                                                   GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_auto_weights:
 * @self: a #LrgEditorSkeleton2DEditor
 * @bones: (array length=n_bones) (nullable): the bones to weight to, %NULL
 *   for every bone
 * @n_bones: the length of @bones
 * @falloff: distance falloff exponent (2 .. 8)
 * @max_influences: influences kept per vertex (1 .. 4)
 * @smooth_iterations: smoothing passes over the mesh
 * @error: return location for a #GError
 *
 * Weights the selected attachment automatically
 * (lrg_skeleton2d_compute_auto_weights()), one undo step.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_auto_weights (LrgEditorSkeleton2DEditor  *self,
                                                    const gint                 *bones,
                                                    guint                       n_bones,
                                                    gfloat                      falloff,
                                                    guint                       max_influences,
                                                    guint                       smooth_iterations,
                                                    GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_paint_weights:
 * @self: a #LrgEditorSkeleton2DEditor
 * @bone: the bone to paint
 * @x: brush centre, setup-space x
 * @y: brush centre, setup-space y
 * @radius: brush radius in setup units
 * @strength: 0 .. 1; negative erases
 * @error: return location for a #GError
 *
 * One dab of the weight brush on the selected attachment: vertices inside
 * the radius move their @bone weight toward 1 (or 0 when erasing) by
 * @strength times a smooth falloff, the other influences scale to keep
 * the sum 1. One undo step (a brush stroke on the canvas is one step).
 *
 * Returns: the number of vertices changed, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_editor_skeleton2d_editor_paint_weights (LrgEditorSkeleton2DEditor  *self,
                                                 gint                        bone,
                                                 gfloat                      x,
                                                 gfloat                      y,
                                                 gfloat                      radius,
                                                 gfloat                      strength,
                                                 GError                    **error);

/* ==========================================================================
 * Clips and keys
 * ========================================================================== */

/**
 * lrg_editor_skeleton2d_editor_add_clip:
 * @self: a #LrgEditorSkeleton2DEditor
 * @name: a new clip name
 * @duration: length in seconds (>= 0)
 * @loop: the clip's default loop flag
 * @error: return location for a #GError
 *
 * Adds an empty clip and selects it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_add_clip (LrgEditorSkeleton2DEditor  *self,
                                                const gchar                *name,
                                                gfloat                      duration,
                                                gboolean                    loop,
                                                GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_remove_clip:
 * @self: a #LrgEditorSkeleton2DEditor
 * @name: a clip name
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_remove_clip (LrgEditorSkeleton2DEditor  *self,
                                                   const gchar                *name,
                                                   GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_set_clip_props:
 * @self: a #LrgEditorSkeleton2DEditor
 * @name: a clip name
 * @duration: the new length in seconds (>= 0)
 * @loop: the default loop flag
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_set_clip_props (LrgEditorSkeleton2DEditor  *self,
                                                      const gchar                *name,
                                                      gfloat                      duration,
                                                      gboolean                    loop,
                                                      GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_get_clip:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: (nullable): the selected clip's name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_skeleton2d_editor_get_clip (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_set_clip:
 * @self: a #LrgEditorSkeleton2DEditor
 * @name: (nullable): a clip name, %NULL for none
 *
 * Selects the clip the dope sheet shows and animation mode plays.
 *
 * Returns: %TRUE when the clip exists (or @name is %NULL)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_set_clip (LrgEditorSkeleton2DEditor *self,
                                                const gchar               *name);

/**
 * lrg_editor_skeleton2d_editor_set_key:
 * @self: a #LrgEditorSkeleton2DEditor
 * @target: a bone name (translate, rotate, scale) or slot name (color,
 *   attachment)
 * @channel: the channel
 * @time: key time in seconds (>= 0; a later time extends the clip)
 * @values: (array length=n_values) (nullable): the values: x, y for
 *   translate and scale, degrees for rotate, r, g, b, a for color
 * @n_values: the length of @values
 * @attachment: (nullable): the attachment name of an attachment key
 *   (%NULL hides the slot)
 * @curve: the curve toward the next key
 * @bezier: (array fixed-size=4) (nullable): control points x1, y1, x2, y2
 *   when @curve is %LRG_SKELETON2D_CURVE_BEZIER
 * @error: return location for a #GError
 *
 * Adds a key to the selected clip, or replaces the key at the same time.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_set_key (LrgEditorSkeleton2DEditor  *self,
                                               const gchar                *target,
                                               LrgSkeleton2DChannel        channel,
                                               gfloat                      time,
                                               const gfloat               *values,
                                               guint                       n_values,
                                               const gchar                *attachment,
                                               LrgSkeleton2DCurve          curve,
                                               const gfloat               *bezier,
                                               GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_remove_key:
 * @self: a #LrgEditorSkeleton2DEditor
 * @target: a bone or slot name
 * @channel: the channel
 * @index: the key index in time order
 * @error: return location for a #GError
 *
 * Removes a key of the selected clip.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_remove_key (LrgEditorSkeleton2DEditor  *self,
                                                  const gchar                *target,
                                                  LrgSkeleton2DChannel        channel,
                                                  guint                       index,
                                                  GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_move_key:
 * @self: a #LrgEditorSkeleton2DEditor
 * @target: a bone or slot name
 * @channel: the channel
 * @index: the key index in time order
 * @time: the new time (>= 0)
 * @error: return location for a #GError
 *
 * Moves a key of the selected clip in time, keeping its values and curve
 * (Bézier control points included). A key landing on another key's time
 * replaces it.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_move_key (LrgEditorSkeleton2DEditor  *self,
                                                const gchar                *target,
                                                LrgSkeleton2DChannel        channel,
                                                guint                       index,
                                                gfloat                      time,
                                                GError                    **error);

/**
 * lrg_editor_skeleton2d_editor_key_pose:
 * @self: a #LrgEditorSkeleton2DEditor
 * @bone: a bone index
 * @error: return location for a #GError
 *
 * Keys the bone's current pose (translate, rotate and scale offsets from
 * the setup pose) into the selected clip at the playhead (the K key).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_key_pose (LrgEditorSkeleton2DEditor  *self,
                                                gint                        bone,
                                                GError                    **error);

/* ==========================================================================
 * Preview playback
 * ========================================================================== */

/**
 * lrg_editor_skeleton2d_editor_get_time:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: the playhead in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_skeleton2d_editor_get_time (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_set_time:
 * @self: a #LrgEditorSkeleton2DEditor
 * @time: the playhead in seconds (clamped to the clip)
 *
 * Scrubs the preview.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_set_time (LrgEditorSkeleton2DEditor *self,
                                            gfloat                     time);

/**
 * lrg_editor_skeleton2d_editor_get_playing:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: whether the preview plays
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_get_playing (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_set_playing:
 * @self: a #LrgEditorSkeleton2DEditor
 * @playing: whether to play
 *
 * Plays or pauses the selected clip (switching to animation mode). The
 * preview advances with the canvas frame time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_set_playing (LrgEditorSkeleton2DEditor *self,
                                               gboolean                   playing);

/**
 * lrg_editor_skeleton2d_editor_get_loop:
 * @self: a #LrgEditorSkeleton2DEditor
 *
 * Returns: whether the preview loops
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_skeleton2d_editor_get_loop (LrgEditorSkeleton2DEditor *self);

/**
 * lrg_editor_skeleton2d_editor_set_loop:
 * @self: a #LrgEditorSkeleton2DEditor
 * @loop: whether the preview wraps at the end of the clip (otherwise it
 *   stops there)
 *
 * Sets the preview's loop toggle (a view setting, not the clip's flag).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_skeleton2d_editor_set_loop (LrgEditorSkeleton2DEditor *self,
                                            gboolean                   loop);

/**
 * lrg_editor_skeleton2d_editor_render_image:
 * @self: a #LrgEditorSkeleton2DEditor
 * @width: image width in pixels
 * @height: image height in pixels
 *
 * Renders the rig as the canvas shows it (current mode, pose, skin and
 * overlays fitted to the image) into an image. Needs a window.
 *
 * Returns: (transfer full) (nullable): the image, or %NULL without a window
 */
LRG_AVAILABLE_IN_ALL
GrlImage * lrg_editor_skeleton2d_editor_render_image (LrgEditorSkeleton2DEditor *self,
                                                      guint                      width,
                                                      guint                      height);

G_END_DECLS
