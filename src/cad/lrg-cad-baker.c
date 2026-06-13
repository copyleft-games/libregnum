/* lrg-cad-baker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Baking parametric CAD parts into renderable geometry.
 */

#include "lrg-cad-baker.h"

#include <string.h>

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

/* ---- assemblies ---- */

struct _LrgCadInstanceBake
{
	GObject parent_instance;

	guint      instance_id;
	gchar     *name;
	gdouble    transform[12];
	GPtrArray *meshes;    /* CadMesh*, owned */
	GPtrArray *models;    /* GrlModel*, lazily built, owned */
};

G_DEFINE_FINAL_TYPE (LrgCadInstanceBake, lrg_cad_instance_bake, G_TYPE_OBJECT)

static void
lrg_cad_instance_bake_finalize (GObject *object)
{
	LrgCadInstanceBake *self = LRG_CAD_INSTANCE_BAKE (object);

	g_free (self->name);
	g_clear_pointer (&self->meshes, g_ptr_array_unref);
	g_clear_pointer (&self->models, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_cad_instance_bake_parent_class)->finalize (object);
}

static void
lrg_cad_instance_bake_class_init (LrgCadInstanceBakeClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_cad_instance_bake_finalize;
}

static void
lrg_cad_instance_bake_init (LrgCadInstanceBake *self)
{
	self->meshes =
		g_ptr_array_new_with_free_func ((GDestroyNotify) cad_mesh_free);
}

guint
lrg_cad_instance_bake_get_instance_id (LrgCadInstanceBake *self)
{
	g_return_val_if_fail (LRG_IS_CAD_INSTANCE_BAKE (self), 0);
	return self->instance_id;
}

const gchar *
lrg_cad_instance_bake_get_name (LrgCadInstanceBake *self)
{
	g_return_val_if_fail (LRG_IS_CAD_INSTANCE_BAKE (self), NULL);
	return self->name;
}

void
lrg_cad_instance_bake_get_transform (LrgCadInstanceBake *self,
                                     gdouble            *out_m12)
{
	g_return_if_fail (LRG_IS_CAD_INSTANCE_BAKE (self));
	g_return_if_fail (out_m12 != NULL);
	memcpy (out_m12, self->transform, sizeof self->transform);
}

GPtrArray *
lrg_cad_instance_bake_get_meshes (LrgCadInstanceBake *self)
{
	g_return_val_if_fail (LRG_IS_CAD_INSTANCE_BAKE (self), NULL);
	return self->meshes;
}

GPtrArray *
lrg_cad_instance_bake_get_models (LrgCadInstanceBake *self)
{
	guint i;

	g_return_val_if_fail (LRG_IS_CAD_INSTANCE_BAKE (self), NULL);

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

		gmesh = grl_mesh_new_custom (chunk->vertices, chunk->n_vertices,
		                             chunk->normals, indices16,
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

/* Tessellate @solid and append its 16-bit-safe chunks to @out_meshes. */
static gboolean
lrg_cad_tessellate_chunks (CadSolid   *solid,
                           gdouble     deflection,
                           GPtrArray  *out_meshes,
                           GError    **error)
{
	CadTessOptions *opts = NULL;
	CadMesh *mesh;
	GPtrArray *chunks;
	guint i;

	if (deflection > 0.0)
		opts = cad_tess_options_new (deflection, 20.0, FALSE);
	mesh = cad_solid_tessellate (solid, opts, error);
	g_clear_pointer (&opts, cad_tess_options_free);
	if (mesh == NULL)
		return FALSE;

	chunks = cad_mesh_split (mesh, 65535);
	cad_mesh_free (mesh);
	for (i = 0; i < chunks->len; i++)
		g_ptr_array_add (out_meshes,
		                 cad_mesh_copy (g_ptr_array_index (chunks, i)));
	g_ptr_array_unref (chunks);
	return TRUE;
}

GPtrArray *
lrg_cad_bake_assembly_solved (CadAssembly  *assembly,
                              gdouble       deflection,
                              GError      **error)
{
	GPtrArray *instances;
	GPtrArray *out;
	guint i;

	g_return_val_if_fail (CAD_IS_ASSEMBLY (assembly), NULL);

	instances = cad_assembly_get_instances (assembly);
	out = g_ptr_array_new_with_free_func (g_object_unref);

	for (i = 0; i < instances->len; i++)
	{
		CadAssemblyInstance *inst = g_ptr_array_index (instances, i);
		CadSolid *solid = cad_assembly_instance_get_solid (inst);
		LrgCadInstanceBake *bake;

		if (solid == NULL)
			continue;

		bake = g_object_new (LRG_TYPE_CAD_INSTANCE_BAKE, NULL);
		bake->instance_id = cad_assembly_instance_get_id (inst);
		bake->name = g_strdup (cad_assembly_instance_get_name (inst));
		cad_assembly_instance_get_transform (inst, bake->transform);

		if (!lrg_cad_tessellate_chunks (solid, deflection, bake->meshes,
		                                error))
		{
			g_object_unref (bake);
			g_ptr_array_unref (out);
			return NULL;
		}
		g_ptr_array_add (out, bake);
	}

	return out;
}

GPtrArray *
lrg_cad_bake_assembly (CadDocument  *document,
                       GHashTable   *overrides,
                       gdouble       deflection,
                       const gchar  *assembly,
                       GError      **error)
{
	CadAssembly *asm_;

	g_return_val_if_fail (CAD_IS_DOCUMENT (document), NULL);

	if (!cad_document_eval (document, overrides, NULL, error))
		return NULL;

	if (assembly != NULL)
	{
		asm_ = cad_document_get_assembly (document, assembly);
	}
	else
	{
		GPtrArray *names = cad_document_get_assembly_names (document);

		asm_ = names->len > 0
		       ? cad_document_get_assembly (document,
		                                    g_ptr_array_index (names, 0))
		       : NULL;
		g_ptr_array_unref (names);
	}

	if (asm_ == NULL)
	{
		g_set_error (error, CAD_ERROR, CAD_ERROR_FAILED,
		             "the document defines no assembly%s%s",
		             assembly != NULL ? " named " : "",
		             assembly != NULL ? assembly : "");
		return NULL;
	}

	if (!cad_assembly_solve (asm_, error))
		return NULL;

	return lrg_cad_bake_assembly_solved (asm_, deflection, error);
}
