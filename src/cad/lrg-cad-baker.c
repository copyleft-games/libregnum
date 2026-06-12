/* lrg-cad-baker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Baking parametric CAD parts into renderable geometry.
 */

#include "lrg-cad-baker.h"

#include <graylib.h>

struct _LrgCadBakeResult
{
	GObject parent_instance;

	CadSolid       *solid;       /* ref */
	CadFeatureNode *tree;        /* ref, nullable */
	GPtrArray      *meshes;      /* CadMesh*, owned */
	GPtrArray      *models;      /* GrlModel*, lazily built, owned */
	guint           total_triangles;
};

G_DEFINE_FINAL_TYPE (LrgCadBakeResult, lrg_cad_bake_result, G_TYPE_OBJECT)

static void
lrg_cad_bake_result_finalize (GObject *object)
{
	LrgCadBakeResult *self = LRG_CAD_BAKE_RESULT (object);

	g_clear_object (&self->solid);
	g_clear_object (&self->tree);
	g_clear_pointer (&self->meshes, g_ptr_array_unref);
	g_clear_pointer (&self->models, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_cad_bake_result_parent_class)->finalize (object);
}

static void
lrg_cad_bake_result_class_init (LrgCadBakeResultClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_cad_bake_result_finalize;
}

static void
lrg_cad_bake_result_init (LrgCadBakeResult *self)
{
	self->meshes = g_ptr_array_new_with_free_func ((GDestroyNotify) cad_mesh_free);
}

LrgCadBakeResult *
lrg_cad_bake (CadDocument  *document,
              GHashTable   *overrides,
              gdouble       deflection,
              const gchar  *part,
              GError      **error)
{
	LrgCadBakeResult *self;
	CadSolid *solid;
	CadTessOptions *opts = NULL;
	CadMesh *mesh;
	GPtrArray *chunks;
	guint i;

	g_return_val_if_fail (CAD_IS_DOCUMENT (document), NULL);

	if (!cad_document_eval (document, overrides, NULL, error))
		return NULL;

	solid = cad_document_get_solid (document, part);
	if (solid == NULL)
	{
		g_set_error (error, CAD_ERROR, CAD_ERROR_FAILED,
		             "the document defines no part%s%s",
		             part != NULL ? " named " : "",
		             part != NULL ? part : "");
		return NULL;
	}

	if (deflection > 0.0)
		opts = cad_tess_options_new (deflection, 20.0, FALSE);
	mesh = cad_solid_tessellate (solid, opts, error);
	g_clear_pointer (&opts, cad_tess_options_free);
	if (mesh == NULL)
		return NULL;

	self = g_object_new (LRG_TYPE_CAD_BAKE_RESULT, NULL);
	self->solid = g_object_ref (solid);
	if (cad_document_get_feature_tree (document, part) != NULL)
		self->tree = g_object_ref (cad_document_get_feature_tree (document,
		                                                          part));

	/* graylib meshes index with guint16: chunk to 65535 vertices. */
	chunks = cad_mesh_split (mesh, 65535);
	cad_mesh_free (mesh);
	for (i = 0; i < chunks->len; i++)
	{
		CadMesh *chunk = cad_mesh_copy (g_ptr_array_index (chunks, i));

		self->total_triangles += chunk->n_triangles;
		g_ptr_array_add (self->meshes, chunk);
	}
	g_ptr_array_unref (chunks);

	return self;
}

GPtrArray *
lrg_cad_bake_result_get_meshes (LrgCadBakeResult *self)
{
	g_return_val_if_fail (LRG_IS_CAD_BAKE_RESULT (self), NULL);

	return self->meshes;
}

CadSolid *
lrg_cad_bake_result_get_solid (LrgCadBakeResult *self)
{
	g_return_val_if_fail (LRG_IS_CAD_BAKE_RESULT (self), NULL);

	return self->solid;
}

CadFeatureNode *
lrg_cad_bake_result_get_tree (LrgCadBakeResult *self)
{
	g_return_val_if_fail (LRG_IS_CAD_BAKE_RESULT (self), NULL);

	return self->tree;
}

guint
lrg_cad_bake_result_get_total_triangles (LrgCadBakeResult *self)
{
	g_return_val_if_fail (LRG_IS_CAD_BAKE_RESULT (self), 0);

	return self->total_triangles;
}

GPtrArray *
lrg_cad_bake_result_get_models (LrgCadBakeResult *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_CAD_BAKE_RESULT (self), NULL);

	if (self->models != NULL)
		return self->models;

	self->models = g_ptr_array_new_with_free_func (g_object_unref);

	for (i = 0; i < self->meshes->len; i++)
	{
		CadMesh *chunk = g_ptr_array_index (self->meshes, i);
		guint16 *indices16;
		GrlMesh *gmesh;
		GrlModel *model;
		guint t;

		indices16 = g_new (guint16, (gsize) chunk->n_triangles * 3);
		for (t = 0; t < chunk->n_triangles * 3; t++)
			indices16[t] = (guint16) chunk->indices[t];

		gmesh = grl_mesh_new_custom (chunk->vertices,
		                             chunk->n_vertices,
		                             chunk->normals,
		                             indices16,
		                             chunk->n_triangles * 3);
		g_free (indices16);
		if (gmesh == NULL)
			continue;

		model = grl_model_new_from_mesh (gmesh);
		g_object_unref (gmesh);
		if (model != NULL)
			g_ptr_array_add (self->models, model);
	}

	return self->models;
}
