/* lrg-level-stream.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Streaming cells and level instances.
 *
 * A cell is an axis-aligned box plus the level file that should be
 * loaded while a point (the camera, plus a margin) is inside it.
 * lrg_level_stream_update() only decides; lrg_level_stream_load_async()
 * reads the file off the calling thread. A level instance is a node
 * whose `level:` path is another .rlevel; lrg_level_instance_mount()
 * copies that level's nodes under the host for editing, and play
 * instantiates the file when the host has no children yet.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_LEVEL_STREAM (lrg_level_stream_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLevelStream, lrg_level_stream, LRG, LEVEL_STREAM, GObject)

/**
 * lrg_level_stream_new:
 *
 * Returns: (transfer full): an empty stream
 */
LRG_AVAILABLE_IN_ALL
LrgLevelStream * lrg_level_stream_new (void);

/**
 * lrg_level_stream_add_cell:
 * @self: a stream
 * @id: stable cell id
 * @path: (type filename): level file to load
 * @min_x: box minimum
 * @min_y: box minimum
 * @min_z: box minimum
 * @max_x: box maximum
 * @max_y: box maximum
 * @max_z: box maximum
 *
 * Replaces a cell that already uses @id.
 *
 * Returns: %TRUE when the cell was stored
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_stream_add_cell (LrgLevelStream *self,
                                    const gchar    *id,
                                    const gchar    *path,
                                    gfloat          min_x,
                                    gfloat          min_y,
                                    gfloat          min_z,
                                    gfloat          max_x,
                                    gfloat          max_y,
                                    gfloat          max_z);

/**
 * lrg_level_stream_update:
 * @self: a stream
 * @x: query point
 * @y: query point
 * @z: query point
 * @margin: how far outside a box still counts as inside
 * @load: (out) (transfer full) (array zero-terminated=1) (optional):
 *   cell ids that became wanted
 * @unload: (out) (transfer full) (array zero-terminated=1) (optional):
 *   cell ids that are no longer wanted
 *
 * Compares the cells that contain the point with the set remembered
 * from the previous call.
 *
 * Returns: %TRUE
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_stream_update (LrgLevelStream  *self,
                                  gfloat           x,
                                  gfloat           y,
                                  gfloat           z,
                                  gfloat           margin,
                                  GStrv           *load,
                                  GStrv           *unload);

/**
 * lrg_level_stream_load_async:
 * @self: a stream
 * @path: (type filename): a `.rlevel` file
 * @callback: (scope async) (nullable): called on the thread-default context
 * @user_data: data for @callback
 *
 * Loads @path on a worker thread.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_stream_load_async (LrgLevelStream      *self,
                                  const gchar         *path,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data);

/**
 * lrg_level_stream_load_finish:
 * @self: a stream
 * @result: the result passed to the callback
 * @error: (nullable): return location for a #GError
 *
 * Returns: (transfer full) (nullable): the loaded level
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_level_stream_load_finish (LrgLevelStream  *self,
                                         GAsyncResult    *result,
                                         GError         **error);

/**
 * lrg_level_instance_mount:
 * @host: the node that stands for the instance
 * @path: (type filename): the `.rlevel` to copy under @host
 * @error: (nullable): return location for a #GError
 *
 * Copies the level's nodes under @host (same GUIDs) and records @path
 * as the node's level path. Mounting twice duplicates GUIDs; call it
 * once per host.
 *
 * Returns: how many top-level nodes were mounted, or 0 on error
 */
LRG_AVAILABLE_IN_ALL
guint lrg_level_instance_mount (LrgNode      *host,
                                const gchar  *path,
                                GError      **error);

G_END_DECLS
