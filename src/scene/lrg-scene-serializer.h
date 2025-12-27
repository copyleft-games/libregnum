/* lrg-scene-serializer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for scene serialization/deserialization.
 *
 * LrgSceneSerializer defines the interface for loading and saving
 * LrgScene objects. Implementations provide format-specific handling
 * (e.g., YAML, JSON, binary).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-scene.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCENE_SERIALIZER (lrg_scene_serializer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgSceneSerializer, lrg_scene_serializer, LRG, SCENE_SERIALIZER, GObject)

/**
 * LrgSceneSerializerInterface:
 * @g_iface: Parent interface
 * @load_from_file: Load a scene from a file path
 * @load_from_data: Load a scene from string data
 * @save_to_file: Save a scene to a file path
 * @save_to_data: Save a scene to a string
 *
 * Interface for scene serialization/deserialization.
 */
struct _LrgSceneSerializerInterface
{
	GTypeInterface g_iface;

	LrgScene * (*load_from_file) (LrgSceneSerializer  *self,
	                              const gchar         *path,
	                              GError             **error);

	LrgScene * (*load_from_data) (LrgSceneSerializer  *self,
	                              const gchar         *data,
	                              gssize               length,
	                              GError             **error);

	gboolean   (*save_to_file)   (LrgSceneSerializer  *self,
	                              LrgScene            *scene,
	                              const gchar         *path,
	                              GError             **error);

	gchar *    (*save_to_data)   (LrgSceneSerializer  *self,
	                              LrgScene            *scene,
	                              gsize               *length);

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_scene_serializer_load_from_file:
 * @self: an #LrgSceneSerializer
 * @path: Path to the file
 * @error: (nullable): Return location for error
 *
 * Loads a scene from a file.
 *
 * Returns: (transfer full) (nullable): The loaded scene or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgScene * lrg_scene_serializer_load_from_file (LrgSceneSerializer  *self,
                                                const gchar         *path,
                                                GError             **error);

/**
 * lrg_scene_serializer_load_from_data:
 * @self: an #LrgSceneSerializer
 * @data: The serialized data
 * @length: Length of data, or -1 if null-terminated
 * @error: (nullable): Return location for error
 *
 * Loads a scene from string data.
 *
 * Returns: (transfer full) (nullable): The loaded scene or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgScene * lrg_scene_serializer_load_from_data (LrgSceneSerializer  *self,
                                                const gchar         *data,
                                                gssize               length,
                                                GError             **error);

/**
 * lrg_scene_serializer_save_to_file:
 * @self: an #LrgSceneSerializer
 * @scene: The scene to save
 * @path: Path to the output file
 * @error: (nullable): Return location for error
 *
 * Saves a scene to a file.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_serializer_save_to_file (LrgSceneSerializer  *self,
                                            LrgScene            *scene,
                                            const gchar         *path,
                                            GError             **error);

/**
 * lrg_scene_serializer_save_to_data:
 * @self: an #LrgSceneSerializer
 * @scene: The scene to save
 * @length: (out) (optional): Return location for data length
 *
 * Saves a scene to a string.
 *
 * Returns: (transfer full) (nullable): The serialized data or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_scene_serializer_save_to_data (LrgSceneSerializer *self,
                                           LrgScene           *scene,
                                           gsize              *length);

G_END_DECLS
