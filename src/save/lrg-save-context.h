/* lrg-save-context.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Save/load context for serialization.
 *
 * The save context provides a high-level API for serializing and
 * deserializing game state. It wraps yaml-glib to handle the
 * low-level YAML operations.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_SAVE_CONTEXT (lrg_save_context_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSaveContext, lrg_save_context, LRG, SAVE_CONTEXT, GObject)

/**
 * LrgSaveContextMode:
 * @LRG_SAVE_CONTEXT_MODE_SAVE: Context is in save mode (writing)
 * @LRG_SAVE_CONTEXT_MODE_LOAD: Context is in load mode (reading)
 *
 * The mode of the save context.
 */
typedef enum
{
    LRG_SAVE_CONTEXT_MODE_SAVE,
    LRG_SAVE_CONTEXT_MODE_LOAD
} LrgSaveContextMode;

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_save_context_new_for_save:
 *
 * Creates a new save context for saving data.
 *
 * The context starts with an empty document. Use the write methods
 * to add data, then call lrg_save_context_to_string() or
 * lrg_save_context_to_file() to get the result.
 *
 * Returns: (transfer full): A new #LrgSaveContext in save mode
 */
LRG_AVAILABLE_IN_ALL
LrgSaveContext * lrg_save_context_new_for_save (void);

/**
 * lrg_save_context_new_for_load:
 * @data: the YAML data to load from
 * @error: (optional): return location for a #GError
 *
 * Creates a new save context for loading data from a string.
 *
 * Returns: (transfer full) (nullable): A new #LrgSaveContext in load mode,
 *          or %NULL on parse error
 */
LRG_AVAILABLE_IN_ALL
LrgSaveContext * lrg_save_context_new_for_load (const gchar  *data,
                                                 GError      **error);

/**
 * lrg_save_context_new_from_file:
 * @path: path to the YAML file to load
 * @error: (optional): return location for a #GError
 *
 * Creates a new save context for loading data from a file.
 *
 * Returns: (transfer full) (nullable): A new #LrgSaveContext in load mode,
 *          or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgSaveContext * lrg_save_context_new_from_file (const gchar  *path,
                                                  GError      **error);

/* ==========================================================================
 * Mode and Version
 * ========================================================================== */

/**
 * lrg_save_context_get_mode:
 * @self: a #LrgSaveContext
 *
 * Gets the mode of this context.
 *
 * Returns: the context mode
 */
LRG_AVAILABLE_IN_ALL
LrgSaveContextMode lrg_save_context_get_mode (LrgSaveContext *self);

/**
 * lrg_save_context_get_version:
 * @self: a #LrgSaveContext
 *
 * Gets the save format version.
 *
 * Returns: the version number
 */
LRG_AVAILABLE_IN_ALL
guint lrg_save_context_get_version (LrgSaveContext *self);

/**
 * lrg_save_context_set_version:
 * @self: a #LrgSaveContext
 * @version: the version number
 *
 * Sets the save format version.
 *
 * Only valid in save mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_context_set_version (LrgSaveContext *self,
                                   guint           version);

/* ==========================================================================
 * Section Management
 * ========================================================================== */

/**
 * lrg_save_context_begin_section:
 * @self: a #LrgSaveContext
 * @name: the section name (save ID of the saveable object)
 *
 * Begins a named section for saving.
 *
 * Each saveable object should save its data within its own section,
 * identified by its save ID. This allows data to be matched to
 * objects during loading.
 *
 * Only valid in save mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_context_begin_section (LrgSaveContext *self,
                                     const gchar    *name);

/**
 * lrg_save_context_end_section:
 * @self: a #LrgSaveContext
 *
 * Ends the current section.
 *
 * Only valid in save mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_context_end_section (LrgSaveContext *self);

/**
 * lrg_save_context_has_section:
 * @self: a #LrgSaveContext
 * @name: the section name to check
 *
 * Checks if a section exists in the loaded data.
 *
 * Only valid in load mode.
 *
 * Returns: %TRUE if the section exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_context_has_section (LrgSaveContext *self,
                                       const gchar    *name);

/**
 * lrg_save_context_enter_section:
 * @self: a #LrgSaveContext
 * @name: the section name to enter
 *
 * Enters a named section for loading.
 *
 * Only valid in load mode.
 *
 * Returns: %TRUE if the section was found and entered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_context_enter_section (LrgSaveContext *self,
                                         const gchar    *name);

/**
 * lrg_save_context_leave_section:
 * @self: a #LrgSaveContext
 *
 * Leaves the current section.
 *
 * Only valid in load mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_context_leave_section (LrgSaveContext *self);

/* ==========================================================================
 * Writing (Save Mode)
 * ========================================================================== */

/**
 * lrg_save_context_write_string:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the string value
 *
 * Writes a string value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_context_write_string (LrgSaveContext *self,
                                    const gchar    *key,
                                    const gchar    *value);

/**
 * lrg_save_context_write_int:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the integer value
 *
 * Writes an integer value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_context_write_int (LrgSaveContext *self,
                                 const gchar    *key,
                                 gint64          value);

/**
 * lrg_save_context_write_uint:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the unsigned integer value
 *
 * Writes an unsigned integer value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_context_write_uint (LrgSaveContext *self,
                                  const gchar    *key,
                                  guint64         value);

/**
 * lrg_save_context_write_double:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the double value
 *
 * Writes a double value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_context_write_double (LrgSaveContext *self,
                                    const gchar    *key,
                                    gdouble         value);

/**
 * lrg_save_context_write_boolean:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @value: the boolean value
 *
 * Writes a boolean value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_save_context_write_boolean (LrgSaveContext *self,
                                     const gchar    *key,
                                     gboolean        value);

/* ==========================================================================
 * Reading (Load Mode)
 * ========================================================================== */

/**
 * lrg_save_context_read_string:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: (nullable): default if key not found
 *
 * Reads a string value.
 *
 * Returns: (transfer none) (nullable): the string value or @default_value
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_save_context_read_string (LrgSaveContext *self,
                                            const gchar    *key,
                                            const gchar    *default_value);

/**
 * lrg_save_context_read_int:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: default if key not found
 *
 * Reads an integer value.
 *
 * Returns: the integer value or @default_value
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_save_context_read_int (LrgSaveContext *self,
                                  const gchar    *key,
                                  gint64          default_value);

/**
 * lrg_save_context_read_uint:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: default if key not found
 *
 * Reads an unsigned integer value.
 *
 * Returns: the unsigned integer value or @default_value
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_save_context_read_uint (LrgSaveContext *self,
                                    const gchar    *key,
                                    guint64         default_value);

/**
 * lrg_save_context_read_double:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: default if key not found
 *
 * Reads a double value.
 *
 * Returns: the double value or @default_value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_save_context_read_double (LrgSaveContext *self,
                                      const gchar    *key,
                                      gdouble         default_value);

/**
 * lrg_save_context_read_boolean:
 * @self: a #LrgSaveContext
 * @key: the key name
 * @default_value: default if key not found
 *
 * Reads a boolean value.
 *
 * Returns: the boolean value or @default_value
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_context_read_boolean (LrgSaveContext *self,
                                        const gchar    *key,
                                        gboolean        default_value);

/**
 * lrg_save_context_has_key:
 * @self: a #LrgSaveContext
 * @key: the key name to check
 *
 * Checks if a key exists in the current section.
 *
 * Only valid in load mode.
 *
 * Returns: %TRUE if the key exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_context_has_key (LrgSaveContext *self,
                                   const gchar    *key);

/* ==========================================================================
 * Output (Save Mode)
 * ========================================================================== */

/**
 * lrg_save_context_to_string:
 * @self: a #LrgSaveContext
 * @error: (optional): return location for a #GError
 *
 * Generates the YAML string from the save context.
 *
 * Only valid in save mode.
 *
 * Returns: (transfer full) (nullable): the YAML string, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_save_context_to_string (LrgSaveContext  *self,
                                    GError         **error);

/**
 * lrg_save_context_to_file:
 * @self: a #LrgSaveContext
 * @path: the file path to write to
 * @error: (optional): return location for a #GError
 *
 * Writes the save context to a file.
 *
 * Only valid in save mode.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_save_context_to_file (LrgSaveContext  *self,
                                   const gchar     *path,
                                   GError         **error);

G_END_DECLS
