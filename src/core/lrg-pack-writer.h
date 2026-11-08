/* lrg-pack-writer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Project packer and export profiles.
 *
 * LrgPackWriter walks a project, cooks textures whose sidecar asks for a
 * max edge, and writes them as an rres file (raylib's resource format, the
 * one #LrgAssetPack reads) plus a content-addressed VFS directory. Export
 * profiles wrap that pack: a gzip ustar, a Flatpak manifest, an AppDir
 * (the input to appimagetool), and a store-only zip.
 *
 * The rres central-directory offset matches graylib's reader: after the
 * 16-byte header the reader seeks with SEEK_CUR, so the stored offset is
 * the absolute chunk position minus 16. An empty pack has no directory
 * (offset 0 means "none" on that reader). Resource ids are the CRC32 of
 * the forward-slash relative path; id 0 is rewritten to 1 because the
 * reader treats 0 as "missing".
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_PACK_WRITER (lrg_pack_writer_get_type ())
#define LRG_PACK_WRITER_ERROR (lrg_pack_writer_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPackWriter, lrg_pack_writer, LRG, PACK_WRITER, GObject)

/**
 * LrgPackWriterError:
 * @LRG_PACK_WRITER_ERROR_FAILED: the pack could not be written
 * @LRG_PACK_WRITER_ERROR_EXISTS: a destination already exists
 * @LRG_PACK_WRITER_ERROR_UNSUPPORTED: the profile or the input is not supported
 *
 * Error codes for #LrgPackWriter.
 */
typedef enum
{
	LRG_PACK_WRITER_ERROR_FAILED,
	LRG_PACK_WRITER_ERROR_EXISTS,
	LRG_PACK_WRITER_ERROR_UNSUPPORTED
} LrgPackWriterError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_pack_writer_error_quark (void);

/**
 * lrg_pack_writer_new:
 *
 * Returns: (transfer full): a new writer
 */
LRG_AVAILABLE_IN_ALL
LrgPackWriter * lrg_pack_writer_new (void);

/**
 * lrg_pack_writer_set_project:
 * @self: a writer
 * @root: (nullable): project root scanned by lrg_pack_writer_scan()
 */
LRG_AVAILABLE_IN_ALL
void lrg_pack_writer_set_project (LrgPackWriter *self,
                                  const gchar   *root);

LRG_AVAILABLE_IN_ALL
const gchar * lrg_pack_writer_get_project (LrgPackWriter *self);

/**
 * lrg_pack_writer_set_module:
 * @self: a writer
 * @path: (nullable) (type filename): game module copied into bundle profiles
 */
LRG_AVAILABLE_IN_ALL
void lrg_pack_writer_set_module (LrgPackWriter *self,
                                 const gchar   *path);

/**
 * lrg_pack_writer_set_launcher:
 * @self: a writer
 * @path: (nullable) (type filename): `lrgldr` binary copied into bundle profiles
 */
LRG_AVAILABLE_IN_ALL
void lrg_pack_writer_set_launcher (LrgPackWriter *self,
                                   const gchar   *path);

/**
 * lrg_pack_writer_set_app_id:
 * @self: a writer
 * @app_id: (nullable): Flatpak app id (default org.libregnum.Game)
 */
LRG_AVAILABLE_IN_ALL
void lrg_pack_writer_set_app_id (LrgPackWriter *self,
                                 const gchar   *app_id);

/**
 * lrg_pack_writer_set_cook:
 * @self: a writer
 * @cook: %TRUE to honour texture max-size and `compression: dxt1` sidecars
 *
 * Cooking is on by default. A max edge resizes the image. `compression`
 * set to `dxt1` (any case) replaces the bytes with a small DXT1 block
 * (`DXT1`, little-endian width and height, then 8-byte 4x4 blocks). A
 * failed decode keeps the original bytes and records a line in the log.
 */
LRG_AVAILABLE_IN_ALL
void lrg_pack_writer_set_cook (LrgPackWriter *self,
                               gboolean       cook);

/**
 * lrg_pack_writer_add_bytes:
 * @self: a writer
 * @name: pack path, forward slashes, relative
 * @data: (array length=size) (nullable): file bytes (copied)
 * @size: byte count
 * @error: (nullable): return location for a #GError
 *
 * Adds one file. Replaces an existing entry of the same name.
 * lrg_pack_writer_scan() clears entries added this way.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pack_writer_add_bytes (LrgPackWriter  *self,
                                    const gchar    *name,
                                    const guint8   *data,
                                    gsize           size,
                                    GError        **error);

/**
 * lrg_pack_writer_scan:
 * @self: a writer
 * @error: (nullable): return location for a #GError
 *
 * Replaces the file list with every regular file under the project root.
 * Skips VCS directories, `build`, symlinks and files larger than 64 MiB
 * (those are logged and left out).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pack_writer_scan (LrgPackWriter  *self,
                               GError        **error);

LRG_AVAILABLE_IN_ALL
guint lrg_pack_writer_get_count (LrgPackWriter *self);

/**
 * lrg_pack_writer_get_files:
 * @self: a writer
 *
 * Returns: (transfer full) (array zero-terminated=1): pack paths
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_pack_writer_get_files (LrgPackWriter *self);

/**
 * lrg_pack_writer_get_log:
 * @self: a writer
 *
 * Returns: (transfer none): the cook and export log, possibly empty
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_pack_writer_get_log (LrgPackWriter *self);

/**
 * lrg_pack_writer_write_rres:
 * @self: a writer
 * @path: (type filename): destination `.rres`
 * @error: (nullable): return location for a #GError
 *
 * Writes an rres with a central directory. Names are the pack paths.
 * #LrgAssetPack can open the result and load each file by that name.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pack_writer_write_rres (LrgPackWriter  *self,
                                     const gchar    *path,
                                     GError        **error);

/**
 * lrg_pack_writer_write_vfs:
 * @self: a writer
 * @dir: (type filename): destination directory (created)
 * @error: (nullable): return location for a #GError
 *
 * Writes `index.yaml` and `objects/<sha256>` (one file per distinct payload).
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pack_writer_write_vfs (LrgPackWriter  *self,
                                    const gchar    *dir,
                                    GError        **error);

/**
 * lrg_pack_writer_export:
 * @self: a writer
 * @profile: one of `rres`, `vfs`, `linux-tar`, `flatpak`, `appimage`, `windows-zip`
 * @dest: (type filename): file (rres, tar.gz, zip) or directory (vfs, flatpak, appimage)
 * @error: (nullable): return location for a #GError
 *
 * `linux-tar` is a gzip ustar. `windows-zip` is a store-only zip.
 * `flatpak` is a directory with a manifest and `pack.rres` (it does not
 * invoke flatpak-builder). `appimage` is an AppDir; when `appimagetool`
 * is on `PATH` it is also run to produce `dest.AppImage`.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_pack_writer_export (LrgPackWriter  *self,
                                 const gchar    *profile,
                                 const gchar    *dest,
                                 GError        **error);

/**
 * lrg_pack_writer_profile_ids:
 *
 * Returns: (transfer none) (array zero-terminated=1): the profile names
 *   lrg_pack_writer_export() accepts
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_pack_writer_profile_ids (void);

G_END_DECLS
