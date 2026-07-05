/* lrg-image-document.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgImageDocument - a layered raster image (the model behind a 2D image /
 * sprite editor).  Holds an ordered stack of #LrgImageLayer (composited
 * bottom-to-top), a cached flattened image, a pixel-snapshot undo ring, and
 * file export.  Pure CPU / headless: no GL context required.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-image-layer.h"

G_BEGIN_DECLS

#define LRG_TYPE_IMAGE_DOCUMENT (lrg_image_document_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgImageDocument, lrg_image_document, LRG, IMAGE_DOCUMENT, GObject)

/**
 * lrg_image_document_new:
 * @width: document width in pixels (> 0)
 * @height: document height in pixels (> 0)
 *
 * Creates a document with a single transparent layer.
 *
 * Returns: (transfer full): a new #LrgImageDocument
 */
LRG_AVAILABLE_IN_ALL
LrgImageDocument *lrg_image_document_new (gint width, gint height);

/**
 * lrg_image_document_new_from_image:
 * @image: (transfer none): an image to seed the first layer (copied, RGBA8)
 *
 * Creates a document sized to @image with one layer holding it.
 *
 * Returns: (transfer full): a new #LrgImageDocument
 */
LRG_AVAILABLE_IN_ALL
LrgImageDocument *lrg_image_document_new_from_image (GrlImage *image);

LRG_AVAILABLE_IN_ALL
gint lrg_image_document_get_width (LrgImageDocument *self);
LRG_AVAILABLE_IN_ALL
gint lrg_image_document_get_height (LrgImageDocument *self);

/* ── Layers ──────────────────────────────────────────────────────────── */

LRG_AVAILABLE_IN_ALL
guint lrg_image_document_get_n_layers (LrgImageDocument *self);

/**
 * lrg_image_document_get_layer:
 * Returns: (transfer none) (nullable): the layer at @index (0 == bottom)
 */
LRG_AVAILABLE_IN_ALL
LrgImageLayer *lrg_image_document_get_layer (LrgImageDocument *self,
                                             guint             index);

/**
 * lrg_image_document_add_layer:
 * @name: (nullable): layer name
 *
 * Appends a new transparent layer on top and makes it active.
 *
 * Returns: the new layer's index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_image_document_add_layer (LrgImageDocument *self,
                                    const gchar      *name);

/**
 * lrg_image_document_add_layer_for_image:
 * @image: (transfer none): image to place in the new layer (copied, RGBA8)
 * @name: (nullable): layer name
 *
 * Appends a new layer holding @image on top and makes it active.
 *
 * Returns: the new layer's index
 */
LRG_AVAILABLE_IN_ALL
guint lrg_image_document_add_layer_for_image (LrgImageDocument *self,
                                              GrlImage         *image,
                                              const gchar      *name);

/* Remove the layer at @index (a document always keeps >= 1 layer; removing
 * the last one is refused).  Returns %TRUE if removed. */
LRG_AVAILABLE_IN_ALL
gboolean lrg_image_document_remove_layer (LrgImageDocument *self,
                                          guint             index);

/* Move the layer at @from to @to (reorder).  Returns %TRUE on success. */
LRG_AVAILABLE_IN_ALL
gboolean lrg_image_document_move_layer (LrgImageDocument *self,
                                        guint             from,
                                        guint             to);

/* Duplicate the layer at @index (inserted just above it, made active).
 * Returns the new layer's index, or -1 on failure. */
LRG_AVAILABLE_IN_ALL
gint lrg_image_document_duplicate_layer (LrgImageDocument *self,
                                         guint             index);

LRG_AVAILABLE_IN_ALL
guint lrg_image_document_get_active_index (LrgImageDocument *self);
LRG_AVAILABLE_IN_ALL
void lrg_image_document_set_active_index (LrgImageDocument *self, guint index);

/**
 * lrg_image_document_get_active_layer:
 * Returns: (transfer none) (nullable): the active layer
 */
LRG_AVAILABLE_IN_ALL
LrgImageLayer *lrg_image_document_get_active_layer (LrgImageDocument *self);

/* ── Compositing ─────────────────────────────────────────────────────── */

/**
 * lrg_image_document_flatten:
 * @self: a #LrgImageDocument
 *
 * Composites all visible layers (bottom-to-top, honouring opacity, blend
 * mode and offset) into a cached RGBA8 image, recomputing only when dirty.
 *
 * Returns: (transfer none): the flattened image
 */
LRG_AVAILABLE_IN_ALL
GrlImage *lrg_image_document_flatten (LrgImageDocument *self);

/* Mark the cached flattened image stale (call after drawing into a layer). */
LRG_AVAILABLE_IN_ALL
void lrg_image_document_mark_dirty (LrgImageDocument *self);

/* Whole-document geometric transforms (all layers + canvas size). */
LRG_AVAILABLE_IN_ALL
void lrg_image_document_resize (LrgImageDocument *self, gint w, gint h,
                               gboolean nearest);
LRG_AVAILABLE_IN_ALL
void lrg_image_document_crop (LrgImageDocument *self, gint x, gint y,
                             gint w, gint h);
LRG_AVAILABLE_IN_ALL
void lrg_image_document_rotate (LrgImageDocument *self, gboolean clockwise);

/**
 * lrg_image_document_get_pixel:
 * @out_color: (out caller-allocates): receives the flattened pixel
 *
 * Reads the composited pixel at (@x, @y).  Returns %FALSE if out of bounds.
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_image_document_get_pixel (LrgImageDocument *self,
                                       gint              x,
                                       gint              y,
                                       GrlColor         *out_color);

/* Clear the active layer to @color (transparent if %NULL).  Caller should
 * push_undo() first if undoable. */
LRG_AVAILABLE_IN_ALL
void lrg_image_document_fill_active (LrgImageDocument *self,
                                     const GrlColor   *color);

/* ── I/O ─────────────────────────────────────────────────────────────── */

/* Flatten and write to PATH (format inferred from extension: .png/.jpg/.bmp). */
LRG_AVAILABLE_IN_ALL
gboolean lrg_image_document_export (LrgImageDocument *self,
                                    const gchar      *path,
                                    GError          **error);

/* ── Undo / redo (active-layer pixel snapshots) ──────────────────────── */

/* Snapshot the active layer BEFORE an edit.  Clears the redo stack. */
LRG_AVAILABLE_IN_ALL
void lrg_image_document_push_undo (LrgImageDocument *self);

LRG_AVAILABLE_IN_ALL
gboolean lrg_image_document_undo (LrgImageDocument *self);
LRG_AVAILABLE_IN_ALL
gboolean lrg_image_document_redo (LrgImageDocument *self);
LRG_AVAILABLE_IN_ALL
gboolean lrg_image_document_can_undo (LrgImageDocument *self);
LRG_AVAILABLE_IN_ALL
gboolean lrg_image_document_can_redo (LrgImageDocument *self);

G_END_DECLS
