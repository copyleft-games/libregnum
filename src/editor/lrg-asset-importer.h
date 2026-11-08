/* lrg-asset-importer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Custom asset importers.
 *
 * An importer turns a source file into a cooked file the runtime can load.
 * The default registry ships "copy" (byte copy, the generic importer) and
 * "fbx" (vendored ufbx writes an OBJ raylib can load). ".ply" is not a
 * model: the extension table leaves it unknown, and
 * lrg_asset_importer_model_loadable() is false for it and for ".fbx"
 * until the importer has written the OBJ.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_ASSET_IMPORTER (lrg_asset_importer_get_type ())
#define LRG_ASSET_IMPORTER_ERROR (lrg_asset_importer_error_quark ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgAssetImporter, lrg_asset_importer, LRG, ASSET_IMPORTER, GObject)

/**
 * LrgAssetImporterError:
 * @LRG_ASSET_IMPORTER_ERROR_FAILED: the import failed
 * @LRG_ASSET_IMPORTER_ERROR_UNSUPPORTED: this build cannot import the file
 */
typedef enum
{
	LRG_ASSET_IMPORTER_ERROR_FAILED,
	LRG_ASSET_IMPORTER_ERROR_UNSUPPORTED
} LrgAssetImporterError;

struct _LrgAssetImporterClass
{
	GObjectClass parent_class;

	gboolean (*import) (LrgAssetImporter *self,
	                    const gchar      *source,
	                    const gchar      *dest,
	                    LrgAssetMeta     *meta,
	                    GError          **error);

	gpointer _reserved[8];
};

LRG_AVAILABLE_IN_ALL
GQuark lrg_asset_importer_error_quark (void);

/**
 * lrg_asset_importer_import:
 * @self: an importer
 * @source: (type filename): source file
 * @dest: (type filename): cooked output
 * @meta: (nullable): sidecar settings, or %NULL
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_importer_import (LrgAssetImporter  *self,
                                    const gchar       *source,
                                    const gchar       *dest,
                                    LrgAssetMeta      *meta,
                                    GError           **error);

/**
 * lrg_asset_importer_model_loadable:
 * @path: a filesystem path
 *
 * %TRUE when raylib can load this model path. ".ply" and ".fbx" are false:
 * PLY has no loader here, and FBX is only loadable after an importer has
 * written a glTF or OBJ next to it.
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_importer_model_loadable (const gchar *path);

#define LRG_TYPE_ASSET_IMPORTER_REGISTRY (lrg_asset_importer_registry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAssetImporterRegistry, lrg_asset_importer_registry, LRG, ASSET_IMPORTER_REGISTRY, GObject)

/**
 * lrg_asset_importer_registry_get_default:
 *
 * The process-wide importer registry (copy, generic and fbx).
 *
 * Returns: (transfer none): the default registry
 */
LRG_AVAILABLE_IN_ALL
LrgAssetImporterRegistry * lrg_asset_importer_registry_get_default (void);

LRG_AVAILABLE_IN_ALL
LrgAssetImporterRegistry * lrg_asset_importer_registry_new (void);

/**
 * lrg_asset_importer_registry_register:
 * @self: a registry
 * @name: importer name ("copy", "texture", a custom name)
 * @importer: (transfer full): the importer
 */
LRG_AVAILABLE_IN_ALL
void lrg_asset_importer_registry_register (LrgAssetImporterRegistry *self,
                                           const gchar              *name,
                                           LrgAssetImporter         *importer);

/**
 * lrg_asset_importer_registry_lookup:
 * @self: a registry
 * @name: importer name
 *
 * Returns: (transfer none) (nullable): the importer
 */
LRG_AVAILABLE_IN_ALL
LrgAssetImporter * lrg_asset_importer_registry_lookup (LrgAssetImporterRegistry *self,
                                                       const gchar              *name);

/**
 * lrg_asset_importer_registry_names:
 * @self: a registry
 *
 * Returns: (transfer full) (array zero-terminated=1): registered names
 */
LRG_AVAILABLE_IN_ALL
GStrv lrg_asset_importer_registry_names (LrgAssetImporterRegistry *self);

/**
 * lrg_asset_importer_registry_import:
 * @self: a registry
 * @name: importer name
 * @source: (type filename): source file
 * @dest: (type filename): cooked output
 * @meta: (nullable): sidecar settings
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_importer_registry_import (LrgAssetImporterRegistry  *self,
                                             const gchar               *name,
                                             const gchar               *source,
                                             const gchar               *dest,
                                             LrgAssetMeta              *meta,
                                             GError                   **error);

/**
 * lrg_asset_importer_registry_reimport_if_changed:
 * @self: a registry
 * @name: importer name
 * @source: (type filename): source file
 * @dest: (type filename): cooked output
 * @meta: (nullable): sidecar settings
 * @changed: (out): %TRUE when the cooked file was rewritten
 * @error: (nullable): return location for a #GError
 *
 * Rewrites @dest when @source is missing, newer than the stamp beside
 * @dest (`dest` + `.stamp`, the source mtime in seconds), or the stamp
 * does not match. A matching stamp leaves the cooked file alone.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_asset_importer_registry_reimport_if_changed (LrgAssetImporterRegistry  *self,
                                                          const gchar               *name,
                                                          const gchar               *source,
                                                          const gchar               *dest,
                                                          LrgAssetMeta              *meta,
                                                          gboolean                  *changed,
                                                          GError                   **error);

G_END_DECLS
