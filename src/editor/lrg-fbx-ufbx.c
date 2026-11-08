/* lrg-fbx-ufbx.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * FBX to OBJ conversion through vendored ufbx (MIT, Copyright (c) 2020
 * Samuli Raivio, deps/ufbx/). This file is compiled as gnu11 because
 * ufbx is C99. The rest of libregnum stays gnu89.
 */

#include "config.h"

#include "lrg-asset-importer.h"

#include <string.h>

#include "ufbx.h"

gboolean
lrg_fbx_convert_to_obj (const gchar  *source,
                        const gchar  *dest,
                        GError      **error)
{
	ufbx_load_opts  opts;
	ufbx_error      uerr;
	ufbx_scene     *scene;
	GString        *obj;
	gsize           mi;
	guint           base;
	g_autofree gchar *parent = NULL;

	memset (&opts, 0, sizeof opts);
	memset (&uerr, 0, sizeof uerr);
	scene = ufbx_load_file (source, &opts, &uerr);
	if (scene == NULL)
	{
		const char *why;

		why = (uerr.description.data != NULL) ? uerr.description.data : uerr.info;
		g_set_error (error, LRG_ASSET_IMPORTER_ERROR, LRG_ASSET_IMPORTER_ERROR_FAILED,
		             "ufbx could not read '%s': %s", source, (why != NULL) ? why : "unknown error");
		return FALSE;
	}
	if (scene->meshes.count == 0)
	{
		ufbx_free_scene (scene);
		g_set_error (error, LRG_ASSET_IMPORTER_ERROR, LRG_ASSET_IMPORTER_ERROR_FAILED,
		             "FBX '%s' has no meshes", source);
		return FALSE;
	}

	obj = g_string_new ("# libregnum ufbx import\n");
	base = 1;
	for (mi = 0; mi < scene->meshes.count; mi++)
	{
		ufbx_mesh *mesh;
		gsize      vi;
		gsize      fi;

		mesh = scene->meshes.data[mi];
		for (vi = 0; vi < mesh->vertices.count; vi++)
		{
			ufbx_vec3 p = mesh->vertices.data[vi];

			g_string_append_printf (obj, "v %.9g %.9g %.9g\n",
			                        (double) p.x, (double) p.y, (double) p.z);
		}
		for (fi = 0; fi < mesh->faces.count; fi++)
		{
			ufbx_face face;
			uint32_t  c;

			face = mesh->faces.data[fi];
			if (face.num_indices < 3)
				continue;
			g_string_append_c (obj, 'f');
			for (c = 0; c < face.num_indices; c++)
			{
				uint32_t ix;

				ix = mesh->vertex_indices.data[face.index_begin + c];
				g_string_append_printf (obj, " %u", (unsigned) (base + ix));
			}
			g_string_append_c (obj, '\n');
		}
		base += (guint) mesh->num_vertices;
	}
	ufbx_free_scene (scene);

	parent = g_path_get_dirname (dest);
	if (g_mkdir_with_parents (parent, 0755) != 0)
	{
		g_string_free (obj, TRUE);
		g_set_error (error, LRG_ASSET_IMPORTER_ERROR, LRG_ASSET_IMPORTER_ERROR_FAILED,
		             "Could not create '%s'", parent);
		return FALSE;
	}
	if (!g_file_set_contents (dest, obj->str, (gssize) obj->len, error))
	{
		g_string_free (obj, TRUE);
		return FALSE;
	}
	g_string_free (obj, TRUE);
	return TRUE;
}
