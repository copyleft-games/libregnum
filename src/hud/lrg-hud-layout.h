/* lrg-hud-layout.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHudLayout - where the pieces of a HUD sit, as the player arranged
 * them: every piece has the game's default placement (an anchor point
 * of the canvas and an offset from it) and may carry the player's own
 * placement, a lock and a hidden flag.  The layout is the model of a
 * HUD layout editor (move, lock, reset) and of its persistence in a
 * small YAML file; drawing the pieces is the game's.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_HUD_LAYOUT_FORMAT_VERSION:
 *
 * The version lrg_hud_layout_save_to_file() writes; files of a newer
 * version are refused.
 */
#define LRG_HUD_LAYOUT_FORMAT_VERSION (1)

#define LRG_TYPE_HUD_LAYOUT (lrg_hud_layout_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgHudLayout, lrg_hud_layout, LRG, HUD_LAYOUT, GObject)
#pragma GCC visibility pop

/**
 * LrgHudLayout:
 *
 * The pieces of a HUD by string id, in the order they were added.  A
 * placement is an #LrgHudAnchor and an offset in canvas units (the
 * game's virtual units or pixels): the piece's own anchor point sits at
 * the canvas' anchor point moved by the offset (+X right, +Y down).
 *
 * The player's placement, lock and hidden flag may be known before the
 * game adds the piece (a layout loaded at start-up, a mod's piece added
 * later): such entries wait and apply when the piece is added, and are
 * saved again meanwhile.
 *
 * Signals: #LrgHudLayout::changed.
 */

/**
 * LrgHudLayout::changed:
 * @self: the layout
 * @piece_id: (nullable): the piece that changed, %NULL when many did
 *   (reset all, a load)
 */

/**
 * lrg_hud_layout_new:
 *
 * Returns: (transfer full): an empty layout
 */
LRG_AVAILABLE_IN_ALL
LrgHudLayout *
lrg_hud_layout_new (void);

/**
 * lrg_hud_layout_add_piece:
 * @self: an #LrgHudLayout
 * @piece_id: the piece's id (non-empty, without whitespace)
 * @label: (nullable): the name the editor shows, %NULL for the id
 * @anchor: the default anchor
 * @x: the default horizontal offset
 * @y: the default vertical offset
 *
 * Adds a piece with its default placement.  A placement, lock or
 * hidden flag already known for @piece_id (loaded earlier) applies.
 *
 * Returns: %TRUE when added; %FALSE for a duplicate or an invalid id
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_add_piece (LrgHudLayout *self,
                          const gchar  *piece_id,
                          const gchar  *label,
                          LrgHudAnchor  anchor,
                          gfloat        x,
                          gfloat        y);

/**
 * lrg_hud_layout_remove_piece:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 *
 * Removes the piece; the player's placement of it is kept (and saved)
 * for when it is added again.
 *
 * Returns: %TRUE when it was there
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_remove_piece (LrgHudLayout *self,
                             const gchar  *piece_id);

/**
 * lrg_hud_layout_has_piece:
 * @self: an #LrgHudLayout
 * @piece_id: a piece id
 *
 * Returns: %TRUE when the piece was added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_has_piece (LrgHudLayout *self,
                          const gchar  *piece_id);

/**
 * lrg_hud_layout_get_pieces:
 * @self: an #LrgHudLayout
 *
 * Returns: (transfer full) (array zero-terminated=1): the ids of the
 *   pieces added, in order
 */
LRG_AVAILABLE_IN_ALL
gchar **
lrg_hud_layout_get_pieces (LrgHudLayout *self);

/**
 * lrg_hud_layout_get_label:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 *
 * Returns: (transfer none) (nullable): the label, %NULL for an unknown
 *   piece
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_hud_layout_get_label (LrgHudLayout *self,
                          const gchar  *piece_id);

/**
 * lrg_hud_layout_get_position:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 * @anchor: (out) (optional): the anchor
 * @x: (out) (optional): the horizontal offset
 * @y: (out) (optional): the vertical offset
 *
 * The placement in effect: the player's when there is one, else the
 * default.
 *
 * Returns: %TRUE for a piece that was added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_get_position (LrgHudLayout *self,
                             const gchar  *piece_id,
                             LrgHudAnchor *anchor,
                             gfloat       *x,
                             gfloat       *y);

/**
 * lrg_hud_layout_get_default:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 * @anchor: (out) (optional): the default anchor
 * @x: (out) (optional): the default horizontal offset
 * @y: (out) (optional): the default vertical offset
 *
 * Returns: %TRUE for a piece that was added
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_get_default (LrgHudLayout *self,
                            const gchar  *piece_id,
                            LrgHudAnchor *anchor,
                            gfloat       *x,
                            gfloat       *y);

/**
 * lrg_hud_layout_set_position:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 * @anchor: the anchor
 * @x: the horizontal offset
 * @y: the vertical offset
 *
 * The player's placement.  The default placement itself counts as no
 * placement of the player's.  Non-finite offsets are refused.
 *
 * Returns: %TRUE when placed; %FALSE for an unknown or locked piece
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_set_position (LrgHudLayout *self,
                             const gchar  *piece_id,
                             LrgHudAnchor  anchor,
                             gfloat        x,
                             gfloat        y);

/**
 * lrg_hud_layout_move_by:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 * @dx: canvas units to the right
 * @dy: canvas units down
 *
 * Moves the piece by an offset, keeping its anchor.
 *
 * Returns: %TRUE when moved; %FALSE for an unknown or locked piece
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_move_by (LrgHudLayout *self,
                        const gchar  *piece_id,
                        gfloat        dx,
                        gfloat        dy);

/**
 * lrg_hud_layout_place_rect:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 * @left: the piece's new left edge
 * @top: the piece's new top edge
 * @width: the piece's width
 * @height: the piece's height
 * @canvas_width: the canvas' width
 * @canvas_height: the canvas' height
 *
 * Places a piece dragged to a rectangle: the anchor becomes the one of
 * the canvas' thirds the piece's centre lies in (so it keeps its place
 * from that edge when the canvas changes size), the offset follows.
 * The rectangle is first kept inside the canvas.
 *
 * Returns: %TRUE when placed; %FALSE for an unknown or locked piece or
 *   a canvas without area
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_place_rect (LrgHudLayout *self,
                           const gchar  *piece_id,
                           gfloat        left,
                           gfloat        top,
                           gfloat        width,
                           gfloat        height,
                           gfloat        canvas_width,
                           gfloat        canvas_height);

/**
 * lrg_hud_layout_resolve:
 * @anchor: an anchor
 * @x: the horizontal offset
 * @y: the vertical offset
 * @width: the piece's width
 * @height: the piece's height
 * @canvas_width: the canvas' width
 * @canvas_height: the canvas' height
 * @out_left: (out): the piece's left edge
 * @out_top: (out): the piece's top edge
 *
 * Where a placement puts a piece of a size on a canvas.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hud_layout_resolve (LrgHudAnchor  anchor,
                        gfloat        x,
                        gfloat        y,
                        gfloat        width,
                        gfloat        height,
                        gfloat        canvas_width,
                        gfloat        canvas_height,
                        gfloat       *out_left,
                        gfloat       *out_top);

/**
 * lrg_hud_layout_is_modified:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 *
 * Returns: %TRUE when the piece has the player's placement
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_is_modified (LrgHudLayout *self,
                            const gchar  *piece_id);

/**
 * lrg_hud_layout_set_locked:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 * @locked: %TRUE to lock it in place
 *
 * A locked piece refuses to move (set_position, move_by, place_rect)
 * until it is unlocked or reset.
 *
 * Returns: %TRUE for a known piece
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_set_locked (LrgHudLayout *self,
                           const gchar  *piece_id,
                           gboolean      locked);

/**
 * lrg_hud_layout_get_locked:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 *
 * Returns: %TRUE when the piece is locked
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_get_locked (LrgHudLayout *self,
                           const gchar  *piece_id);

/**
 * lrg_hud_layout_set_hidden:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 * @hidden: %TRUE to hide the piece
 *
 * The player's (or an addon's) choice to hide a piece the game would
 * show; the game honours it when drawing.
 *
 * Returns: %TRUE for a known piece
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_set_hidden (LrgHudLayout *self,
                           const gchar  *piece_id,
                           gboolean      hidden);

/**
 * lrg_hud_layout_get_hidden:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 *
 * Returns: %TRUE when the piece is hidden
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_get_hidden (LrgHudLayout *self,
                           const gchar  *piece_id);

/**
 * lrg_hud_layout_reset:
 * @self: an #LrgHudLayout
 * @piece_id: a piece
 *
 * Back to the default: the player's placement, the lock and the hidden
 * flag are dropped.
 *
 * Returns: %TRUE for a known piece
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_reset (LrgHudLayout *self,
                      const gchar  *piece_id);

/**
 * lrg_hud_layout_reset_all:
 * @self: an #LrgHudLayout
 *
 * Resets every piece and forgets the entries of pieces not added.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_hud_layout_reset_all (LrgHudLayout *self);

/**
 * lrg_hud_layout_to_variant:
 * @self: an #LrgHudLayout
 *
 * The player's state of every piece that has any (also of pieces not
 * added yet), as `a{s(bsddbb)}`: id to (placed, anchor nick, x, y,
 * locked, hidden).
 *
 * Returns: (transfer floating): the state
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_hud_layout_to_variant (LrgHudLayout *self);

/**
 * lrg_hud_layout_load_variant:
 * @self: an #LrgHudLayout
 * @state: a state of lrg_hud_layout_to_variant()
 * @error: return location for a #GError
 *
 * Replaces the player's state with @state.  A wrong type, an unknown
 * anchor or a non-finite offset fails with %G_IO_ERROR_INVALID_DATA and
 * changes nothing.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_load_variant (LrgHudLayout  *self,
                             GVariant      *state,
                             GError       **error);

/**
 * lrg_hud_layout_save_to_file:
 * @self: an #LrgHudLayout
 * @path: the file to write (its directory is created)
 * @error: return location for a #GError
 *
 * Writes the player's state as YAML: `version` and a `pieces` mapping of
 * id to `anchor`, `x`, `y` (when placed), `locked`, `hidden`.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_save_to_file (LrgHudLayout  *self,
                             const gchar   *path,
                             GError       **error);

/**
 * lrg_hud_layout_load_from_file:
 * @self: an #LrgHudLayout
 * @path: a file of lrg_hud_layout_save_to_file()
 * @error: return location for a #GError
 *
 * Replaces the player's state with the file's.  A missing file is an
 * empty layout (every piece at its default).  Bad YAML, unknown keys or
 * anchors, non-numbers and a newer version fail with
 * %G_IO_ERROR_INVALID_DATA (%G_IO_ERROR_NOT_SUPPORTED for the version)
 * and change nothing.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_hud_layout_load_from_file (LrgHudLayout  *self,
                               const gchar   *path,
                               GError       **error);

G_END_DECLS
