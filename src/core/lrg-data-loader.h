/* lrg-data-loader.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Data loader for loading GObjects from YAML files.
 *
 * The data loader integrates with the type registry to enable
 * fully data-driven object creation. YAML files can specify a
 * "type" field that maps to a registered GType.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#ifdef LRG_HAS_LIBDEX
#include <libdex.h>
#endif
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_DATA_LOADER (lrg_data_loader_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDataLoader, lrg_data_loader, LRG, DATA_LOADER, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_data_loader_new:
 *
 * Creates a new data loader.
 *
 * Note: The loader needs a registry to resolve type names. Call
 * lrg_data_loader_set_registry() before loading typed objects.
 *
 * Returns: (transfer full): A new #LrgDataLoader
 */
LRG_AVAILABLE_IN_ALL
LrgDataLoader * lrg_data_loader_new (void);

/* ==========================================================================
 * Registry
 * ========================================================================== */

/**
 * lrg_data_loader_set_registry:
 * @self: an #LrgDataLoader
 * @registry: (nullable): the #LrgRegistry to use for type lookups
 *
 * Sets the registry used to resolve type names in YAML files.
 *
 * When loading a YAML file that contains a "type" field at the root
 * level, the loader uses the registry to look up the corresponding
 * GType and deserialize the object.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_registry (LrgDataLoader *self,
                                   LrgRegistry   *registry);

/**
 * lrg_data_loader_get_registry:
 * @self: an #LrgDataLoader
 *
 * Gets the registry used for type lookups.
 *
 * Returns: (transfer none) (nullable): The #LrgRegistry, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgRegistry * lrg_data_loader_get_registry (LrgDataLoader *self);

/* ==========================================================================
 * Synchronous Loading
 * ========================================================================== */

/**
 * lrg_data_loader_load_file:
 * @self: an #LrgDataLoader
 * @path: path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a YAML file.
 *
 * The YAML file must have a "type" field at the root level that
 * corresponds to a registered type name. The remaining fields
 * are used to set object properties.
 *
 * Example YAML:
 * ```yaml
 * type: player
 * name: "Hero"
 * health: 100
 * ```
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_file (LrgDataLoader  *self,
                                     const gchar    *path,
                                     GError        **error);

/**
 * lrg_data_loader_load_gfile:
 * @self: an #LrgDataLoader
 * @file: a #GFile to load
 * @cancellable: (nullable): a #GCancellable
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a #GFile.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_gfile (LrgDataLoader  *self,
                                      GFile          *file,
                                      GCancellable   *cancellable,
                                      GError        **error);

/**
 * lrg_data_loader_load_data:
 * @self: an #LrgDataLoader
 * @data: the YAML string
 * @length: length of @data, or -1 if null-terminated
 * @error: (nullable): return location for error
 *
 * Loads a GObject from a YAML string.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_data (LrgDataLoader  *self,
                                     const gchar    *data,
                                     gssize          length,
                                     GError        **error);

/**
 * lrg_data_loader_load_typed:
 * @self: an #LrgDataLoader
 * @type: the #GType to deserialize as
 * @path: path to the YAML file
 * @error: (nullable): return location for error
 *
 * Loads a GObject of a specific type from a YAML file.
 *
 * Unlike lrg_data_loader_load_file(), this does not require a
 * "type" field in the YAML. The entire file is deserialized
 * directly to the specified type.
 *
 * Returns: (transfer full) (nullable): The loaded #GObject, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_data_loader_load_typed (LrgDataLoader  *self,
                                      GType           type,
                                      const gchar    *path,
                                      GError        **error);

/* ==========================================================================
 * Batch Loading
 * ========================================================================== */

/**
 * lrg_data_loader_load_directory:
 * @self: an #LrgDataLoader
 * @directory: path to directory containing YAML files
 * @recursive: whether to recurse into subdirectories
 * @error: (nullable): return location for error
 *
 * Loads all YAML files from a directory.
 *
 * Files are loaded in alphabetical order. Files that fail to load
 * are skipped (with a warning logged), and loading continues.
 *
 * Returns: (transfer full) (element-type GObject): A #GList of loaded
 *          objects. Free with g_list_free_full(list, g_object_unref).
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_data_loader_load_directory (LrgDataLoader  *self,
                                        const gchar    *directory,
                                        gboolean        recursive,
                                        GError        **error);

/**
 * lrg_data_loader_load_files:
 * @self: an #LrgDataLoader
 * @paths: (array zero-terminated=1): NULL-terminated array of file paths
 * @error: (nullable): return location for error
 *
 * Loads multiple YAML files.
 *
 * Returns: (transfer full) (element-type GObject): A #GList of loaded
 *          objects. Free with g_list_free_full(list, g_object_unref).
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_data_loader_load_files (LrgDataLoader   *self,
                                    const gchar    **paths,
                                    GError         **error);

#ifdef LRG_HAS_LIBDEX
/* ==========================================================================
 * Asynchronous Loading (libdex futures)
 * ========================================================================== */

/**
 * lrg_data_loader_load_file_async:
 * @self: an #LrgDataLoader
 * @path: path to the YAML file
 *
 * Asynchronously loads a GObject from a YAML file.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GObject
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_data_loader_load_file_async (LrgDataLoader *self,
                                             const gchar   *path);

/**
 * lrg_data_loader_load_gfile_async:
 * @self: an #LrgDataLoader
 * @file: a #GFile to load
 *
 * Asynchronously loads a GObject from a #GFile.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GObject
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_data_loader_load_gfile_async (LrgDataLoader *self,
                                              GFile         *file);

/**
 * lrg_data_loader_load_directory_async:
 * @self: an #LrgDataLoader
 * @directory: path to directory containing YAML files
 * @recursive: whether to recurse into subdirectories
 *
 * Asynchronously loads all YAML files from a directory.
 *
 * Returns: (transfer full): A #DexFuture that resolves to a #GList
 *          of #GObject instances
 */
LRG_AVAILABLE_IN_ALL
DexFuture * lrg_data_loader_load_directory_async (LrgDataLoader *self,
                                                  const gchar   *directory,
                                                  gboolean       recursive);
#endif /* LRG_HAS_LIBDEX */

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_data_loader_get_type_field_name:
 * @self: an #LrgDataLoader
 *
 * Gets the field name used to identify object types in YAML files.
 *
 * Default is "type".
 *
 * Returns: (transfer none): The type field name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_data_loader_get_type_field_name (LrgDataLoader *self);

/**
 * lrg_data_loader_set_type_field_name:
 * @self: an #LrgDataLoader
 * @field_name: the field name to use
 *
 * Sets the field name used to identify object types in YAML files.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_type_field_name (LrgDataLoader *self,
                                          const gchar   *field_name);

/**
 * lrg_data_loader_get_file_extensions:
 * @self: an #LrgDataLoader
 *
 * Gets the file extensions recognized by directory loading.
 *
 * Default is ".yaml" and ".yml".
 *
 * Returns: (transfer none) (array zero-terminated=1): The extensions array
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_data_loader_get_file_extensions (LrgDataLoader *self);

/**
 * lrg_data_loader_set_file_extensions:
 * @self: an #LrgDataLoader
 * @extensions: (array zero-terminated=1): NULL-terminated array of extensions
 *
 * Sets the file extensions recognized by directory loading.
 */
LRG_AVAILABLE_IN_ALL
void lrg_data_loader_set_file_extensions (LrgDataLoader   *self,
                                          const gchar    **extensions);

G_END_DECLS
