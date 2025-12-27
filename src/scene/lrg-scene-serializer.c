/* lrg-scene-serializer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Interface for scene serialization/deserialization.
 */

#include "lrg-scene-serializer.h"

G_DEFINE_INTERFACE (LrgSceneSerializer, lrg_scene_serializer, G_TYPE_OBJECT)

static void
lrg_scene_serializer_default_init (LrgSceneSerializerInterface *iface)
{
	/* Default implementations are NULL; subclasses must override */
}

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
LrgScene *
lrg_scene_serializer_load_from_file (LrgSceneSerializer  *self,
                                     const gchar         *path,
                                     GError             **error)
{
	LrgSceneSerializerInterface *iface;

	g_return_val_if_fail (LRG_IS_SCENE_SERIALIZER (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	iface = LRG_SCENE_SERIALIZER_GET_IFACE (self);

	if (iface->load_from_file == NULL)
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_FAILED,
		             "load_from_file not implemented");
		return NULL;
	}

	return iface->load_from_file (self, path, error);
}

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
LrgScene *
lrg_scene_serializer_load_from_data (LrgSceneSerializer  *self,
                                     const gchar         *data,
                                     gssize               length,
                                     GError             **error)
{
	LrgSceneSerializerInterface *iface;

	g_return_val_if_fail (LRG_IS_SCENE_SERIALIZER (self), NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	iface = LRG_SCENE_SERIALIZER_GET_IFACE (self);

	if (iface->load_from_data == NULL)
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_FAILED,
		             "load_from_data not implemented");
		return NULL;
	}

	return iface->load_from_data (self, data, length, error);
}

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
gboolean
lrg_scene_serializer_save_to_file (LrgSceneSerializer  *self,
                                   LrgScene            *scene,
                                   const gchar         *path,
                                   GError             **error)
{
	LrgSceneSerializerInterface *iface;

	g_return_val_if_fail (LRG_IS_SCENE_SERIALIZER (self), FALSE);
	g_return_val_if_fail (LRG_IS_SCENE (scene), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	iface = LRG_SCENE_SERIALIZER_GET_IFACE (self);

	if (iface->save_to_file == NULL)
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_FAILED,
		             "save_to_file not implemented");
		return FALSE;
	}

	return iface->save_to_file (self, scene, path, error);
}

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
gchar *
lrg_scene_serializer_save_to_data (LrgSceneSerializer *self,
                                   LrgScene           *scene,
                                   gsize              *length)
{
	LrgSceneSerializerInterface *iface;

	g_return_val_if_fail (LRG_IS_SCENE_SERIALIZER (self), NULL);
	g_return_val_if_fail (LRG_IS_SCENE (scene), NULL);

	iface = LRG_SCENE_SERIALIZER_GET_IFACE (self);

	if (iface->save_to_data == NULL)
		return NULL;

	return iface->save_to_data (self, scene, length);
}
