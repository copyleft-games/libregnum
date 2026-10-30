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

/* ==========================================================================
 * Default vfunc implementations
 *
 * A format only has to know how to turn bytes into a scene and back:
 * load_from_data and save_to_data.  The file entry points default to
 * reading/writing the whole file and delegating to the data vfuncs, so a
 * minimal implementation overrides two vfuncs and gets all four methods.
 * Implementations that can stream (or need the path, e.g. to resolve
 * relative asset references) still override the file vfuncs directly.
 * ========================================================================== */

/*
 * default_load_from_file:
 *
 * Reads @path fully into memory and hands the bytes to the
 * implementation's load_from_data vfunc.  Read failures are reported
 * as %LRG_SCENE_ERROR_IO with the path in the message.
 */
static LrgScene *
default_load_from_file (LrgSceneSerializer  *self,
                        const gchar         *path,
                        GError             **error)
{
	LrgSceneSerializerInterface *iface;
	g_autofree gchar *contents = NULL;
	g_autoptr(GError) io_error = NULL;
	gsize length;

	iface = LRG_SCENE_SERIALIZER_GET_IFACE (self);

	/* Without a data loader there is nothing to delegate to. */
	if (iface->load_from_data == NULL)
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_FAILED,
		             "%s cannot load scenes: it implements neither "
		             "load_from_file nor load_from_data",
		             G_OBJECT_TYPE_NAME (self));
		return NULL;
	}

	length = 0;
	if (!g_file_get_contents (path, &contents, &length, &io_error))
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_IO,
		             "Failed to read scene file '%s': %s",
		             path, io_error->message);
		return NULL;
	}

	return iface->load_from_data (self, contents, (gssize) length, error);
}

/*
 * default_load_from_data:
 *
 * There is no format-independent way to parse bytes, so the default
 * only reports which implementation is missing the vfunc.
 */
static LrgScene *
default_load_from_data (LrgSceneSerializer  *self,
                        const gchar         *data,
                        gssize               length,
                        GError             **error)
{
	g_set_error (error,
	             LRG_SCENE_ERROR,
	             LRG_SCENE_ERROR_FAILED,
	             "%s does not implement load_from_data",
	             G_OBJECT_TYPE_NAME (self));
	return NULL;
}

/*
 * default_save_to_file:
 *
 * Serializes @scene through the save_to_data vfunc and writes the
 * result atomically with g_file_set_contents().
 */
static gboolean
default_save_to_file (LrgSceneSerializer  *self,
                      LrgScene            *scene,
                      const gchar         *path,
                      GError             **error)
{
	LrgSceneSerializerInterface *iface;
	g_autofree gchar *data = NULL;
	g_autoptr(GError) io_error = NULL;
	gsize length;

	iface = LRG_SCENE_SERIALIZER_GET_IFACE (self);

	if (iface->save_to_data == NULL)
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_FAILED,
		             "%s cannot save scenes: it implements neither "
		             "save_to_file nor save_to_data",
		             G_OBJECT_TYPE_NAME (self));
		return FALSE;
	}

	length = 0;
	data = iface->save_to_data (self, scene, &length);
	if (data == NULL)
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_FAILED,
		             "%s failed to serialize the scene",
		             G_OBJECT_TYPE_NAME (self));
		return FALSE;
	}

	if (!g_file_set_contents (path, data, (gssize) length, &io_error))
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_IO,
		             "Failed to write scene file '%s': %s",
		             path, io_error->message);
		return FALSE;
	}

	return TRUE;
}

static void
lrg_scene_serializer_default_init (LrgSceneSerializerInterface *iface)
{
	/*
	 * File I/O is generic; parsing and generating are format specific.
	 * save_to_data has no sensible default and stays NULL.
	 */
	iface->load_from_file = default_load_from_file;
	iface->load_from_data = default_load_from_data;
	iface->save_to_file   = default_save_to_file;
	iface->save_to_data   = NULL;
}

/*
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
		             "%s cleared the load_from_file vfunc",
		             G_OBJECT_TYPE_NAME (self));
		return NULL;
	}

	return iface->load_from_file (self, path, error);
}

/*
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
		             "%s cleared the load_from_data vfunc",
		             G_OBJECT_TYPE_NAME (self));
		return NULL;
	}

	return iface->load_from_data (self, data, length, error);
}

/*
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
		             "%s cleared the save_to_file vfunc",
		             G_OBJECT_TYPE_NAME (self));
		return FALSE;
	}

	return iface->save_to_file (self, scene, path, error);
}

/*
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
