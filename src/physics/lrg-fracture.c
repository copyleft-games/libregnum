/* lrg-fracture.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Voronoi pre-fracture.  The Voronoi cell of seed i is the set of points
 * closer to i than to any other seed: the intersection of the half
 * spaces behind the bisector planes of (i, j).  Starting from the convex
 * source polyhedron (its faces as polygons), each bisector clips every
 * face with Sutherland-Hodgman and closes the hole with a cap polygon
 * made from the new points (convex, so they are sorted by angle round
 * their centroid).  Exterior faces keep the source material and
 * interpolated texture coordinates; caps get the interior material.  The
 * other seeds are visited nearest first and the loop stops once a seed
 * is further than twice the cell's radius, which no bisector can cut.
 * Mass properties use Eberly's polyhedral integrals ("Polyhedral Mass
 * Properties (Revisited)", 2002).
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <float.h>
#include <gio/gio.h>
#include <raylib.h>
#include <yaml-glib.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-fracture.h"
#include "core/lrg-math3d.h"

#define FRACTURE_MAX_SOURCE_VERTS (65535u)
#define FRACTURE_MESH_MAX_VERTS   (65535u)

/* ==========================================================================
 * Chunk
 * ========================================================================== */

struct _LrgFractureChunk
{
	gint      ref_count;
	guint     index;
	gfloat    seed[3];
	guint     n_vertices;
	gfloat   *positions;
	gfloat   *normals;
	gfloat   *texcoords;
	guint     n_indices;
	guint32  *indices;
	guint32  *materials;      /* per triangle */
	guint     n_hull;
	gfloat   *hull;           /* distinct corners */
	guint     n_neighbors;
	guint32  *neighbors;
	gfloat    volume;
	gfloat    com[3];
	gfloat    inertia[9];     /* unit density, about the COM */
	gfloat    bmin[3];
	gfloat    bmax[3];
};

G_DEFINE_BOXED_TYPE (LrgFractureChunk, lrg_fracture_chunk, lrg_fracture_chunk_ref, lrg_fracture_chunk_unref)

static LrgFractureChunk *
chunk_new (void)
{
	LrgFractureChunk *c = g_new0 (LrgFractureChunk, 1);

	c->ref_count = 1;
	return c;
}

LrgFractureChunk *
lrg_fracture_chunk_ref (LrgFractureChunk *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	g_atomic_int_inc (&self->ref_count);
	return self;
}

void
lrg_fracture_chunk_unref (LrgFractureChunk *self)
{
	g_return_if_fail (self != NULL);
	if (!g_atomic_int_dec_and_test (&self->ref_count))
		return;
	g_free (self->positions);
	g_free (self->normals);
	g_free (self->texcoords);
	g_free (self->indices);
	g_free (self->materials);
	g_free (self->hull);
	g_free (self->neighbors);
	g_free (self);
}

guint
lrg_fracture_chunk_get_index (const LrgFractureChunk *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->index;
}

void
lrg_fracture_chunk_get_seed (const LrgFractureChunk *self,
                             gfloat                 *out)
{
	g_return_if_fail (self != NULL && out != NULL);
	memcpy (out, self->seed, 3 * sizeof (gfloat));
}

guint
lrg_fracture_chunk_get_vertex_count (const LrgFractureChunk *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->n_vertices;
}

const gfloat *
lrg_fracture_chunk_get_positions (const LrgFractureChunk *self,
                                  guint                  *n_floats)
{
	g_return_val_if_fail (self != NULL, NULL);
	if (n_floats != NULL)
		*n_floats = 3 * self->n_vertices;
	return self->positions;
}

const gfloat *
lrg_fracture_chunk_get_normals (const LrgFractureChunk *self,
                                guint                  *n_floats)
{
	g_return_val_if_fail (self != NULL, NULL);
	if (n_floats != NULL)
		*n_floats = 3 * self->n_vertices;
	return self->normals;
}

const gfloat *
lrg_fracture_chunk_get_texcoords (const LrgFractureChunk *self,
                                  guint                  *n_floats)
{
	g_return_val_if_fail (self != NULL, NULL);
	if (n_floats != NULL)
		*n_floats = 2 * self->n_vertices;
	return self->texcoords;
}

const guint32 *
lrg_fracture_chunk_get_indices (const LrgFractureChunk *self,
                                guint                  *n_indices)
{
	g_return_val_if_fail (self != NULL, NULL);
	if (n_indices != NULL)
		*n_indices = self->n_indices;
	return self->indices;
}

const guint32 *
lrg_fracture_chunk_get_triangle_materials (const LrgFractureChunk *self,
                                           guint                  *n_triangles)
{
	g_return_val_if_fail (self != NULL, NULL);
	if (n_triangles != NULL)
		*n_triangles = self->n_indices / 3;
	return self->materials;
}

guint
lrg_fracture_chunk_get_interior_triangle_count (const LrgFractureChunk *self,
                                                guint32                 interior_material)
{
	guint i;
	guint n = 0;

	g_return_val_if_fail (self != NULL, 0);
	for (i = 0; i < self->n_indices / 3; i++)
		if (self->materials[i] == interior_material)
			n++;
	return n;
}

const gfloat *
lrg_fracture_chunk_get_hull_points (const LrgFractureChunk *self,
                                    guint                  *n_floats)
{
	g_return_val_if_fail (self != NULL, NULL);
	if (n_floats != NULL)
		*n_floats = 3 * self->n_hull;
	return self->hull;
}

const guint32 *
lrg_fracture_chunk_get_neighbors (const LrgFractureChunk *self,
                                  guint                  *n_neighbors)
{
	g_return_val_if_fail (self != NULL, NULL);
	if (n_neighbors != NULL)
		*n_neighbors = self->n_neighbors;
	return self->neighbors;
}

gfloat
lrg_fracture_chunk_get_volume (const LrgFractureChunk *self)
{
	g_return_val_if_fail (self != NULL, 0.0f);
	return self->volume;
}

void
lrg_fracture_chunk_get_center_of_mass (const LrgFractureChunk *self,
                                       gfloat                 *out)
{
	g_return_if_fail (self != NULL && out != NULL);
	memcpy (out, self->com, 3 * sizeof (gfloat));
}

gboolean
lrg_fracture_chunk_get_mass_properties (const LrgFractureChunk *self,
                                        gfloat                  density,
                                        gfloat                 *mass,
                                        gfloat                 *com,
                                        gfloat                 *inertia)
{
	guint i;

	g_return_val_if_fail (self != NULL, FALSE);
	if (!(density > 0.0f) || !isfinite (density))
		return FALSE;
	if (mass != NULL)
		*mass = density * self->volume;
	if (com != NULL)
		memcpy (com, self->com, 3 * sizeof (gfloat));
	if (inertia != NULL)
		for (i = 0; i < 9; i++)
			inertia[i] = density * self->inertia[i];
	return TRUE;
}

void
lrg_fracture_chunk_get_bounds (const LrgFractureChunk *self,
                               gfloat                 *out_min,
                               gfloat                 *out_max)
{
	g_return_if_fail (self != NULL && out_min != NULL && out_max != NULL);
	memcpy (out_min, self->bmin, 3 * sizeof (gfloat));
	memcpy (out_max, self->bmax, 3 * sizeof (gfloat));
}

LrgPhysicsShape *
lrg_fracture_chunk_create_shape (const LrgFractureChunk  *self,
                                 GError                 **error)
{
	g_autofree gfloat *pts = NULL;
	guint              i;

	g_return_val_if_fail (self != NULL, NULL);
	pts = g_new (gfloat, 3 * (gsize) self->n_hull);
	for (i = 0; i < self->n_hull; i++)
	{
		pts[3 * i] = self->hull[3 * i] - self->com[0];
		pts[3 * i + 1] = self->hull[3 * i + 1] - self->com[1];
		pts[3 * i + 2] = self->hull[3 * i + 2] - self->com[2];
	}
	return lrg_physics_shape_new_convex_hull (pts, 3 * self->n_hull, 0.0f, error);
}

GrlMesh *
lrg_fracture_chunk_create_mesh (const LrgFractureChunk  *self,
                                gboolean                 centered,
                                GError                 **error)
{
	static const gfloat  tri_vertices[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
	static const guint16 tri_indices[3] = { 0, 1, 2 };
	GrlMesh             *wrapper;
	Mesh                *handle;
	Mesh                 m;
	guint                i;

	g_return_val_if_fail (self != NULL, NULL);
	if (!IsWindowReady ())
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_GPU,
		                     "A graphics context is required to create a chunk mesh");
		return NULL;
	}
	if (self->n_vertices == 0 || self->n_vertices > FRACTURE_MESH_MAX_VERTS)
	{
		g_set_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID,
		             "A chunk mesh needs 1 .. %u corners (has %u)", FRACTURE_MESH_MAX_VERTS,
		             self->n_vertices);
		return NULL;
	}

	memset (&m, 0, sizeof (m));
	m.vertexCount = (int) self->n_vertices;
	m.triangleCount = (int) (self->n_indices / 3);
	m.vertices = RL_MALLOC (3 * sizeof (float) * self->n_vertices);
	m.normals = RL_MALLOC (3 * sizeof (float) * self->n_vertices);
	m.texcoords = RL_MALLOC (2 * sizeof (float) * self->n_vertices);
	m.indices = RL_MALLOC (sizeof (unsigned short) * self->n_indices);
	for (i = 0; i < self->n_vertices; i++)
	{
		m.vertices[3 * i] = self->positions[3 * i] - (centered ? self->com[0] : 0.0f);
		m.vertices[3 * i + 1] = self->positions[3 * i + 1] - (centered ? self->com[1] : 0.0f);
		m.vertices[3 * i + 2] = self->positions[3 * i + 2] - (centered ? self->com[2] : 0.0f);
	}
	memcpy (m.normals, self->normals, 3 * sizeof (float) * self->n_vertices);
	memcpy (m.texcoords, self->texcoords, 2 * sizeof (float) * self->n_vertices);
	for (i = 0; i < self->n_indices; i++)
		m.indices[i] = (unsigned short) self->indices[i];

	/* Adopt the raylib mesh through a placeholder GrlMesh (see lrg-mesh-simplify.c). */
	wrapper = grl_mesh_new_custom (tri_vertices, 3, NULL, tri_indices, 3);
	if (wrapper == NULL)
	{
		UnloadMesh (m);
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_GPU,
		                     "Could not create a mesh");
		return NULL;
	}
	handle = (Mesh *) grl_mesh_get_handle (wrapper);
	UnloadMesh (*handle);
	*handle = m;
	UploadMesh (handle, false);
	return wrapper;
}

/* ==========================================================================
 * Fracture object
 * ========================================================================== */

struct _LrgFracture
{
	GObject parent_instance;

	/* source */
	guint      n_vertices;
	gfloat    *positions;
	gfloat    *texcoords;
	guint      n_indices;
	guint32   *indices;
	guint32   *materials;
	gfloat     source_volume;
	gfloat     bmin[3];
	gfloat     bmax[3];

	/* parameters */
	guint      chunk_count;
	guint      seed;
	LrgFractureSeedMode seed_mode;
	guint      interior_material;
	gfloat     impact_radius;
	gfloat     grid_jitter;
	gfloat     impulse_threshold;
	gfloat     density;
	gboolean   has_impact;
	gfloat     impact[3];

	/* result */
	GArray    *seeds;           /* gfloat triples */
	GPtrArray *chunks;
};

enum
{
	PROP_0,
	PROP_CHUNK_COUNT,
	PROP_SEED,
	PROP_SEED_MODE,
	PROP_INTERIOR_MATERIAL,
	PROP_IMPACT_RADIUS,
	PROP_GRID_JITTER,
	PROP_IMPULSE_THRESHOLD,
	PROP_DENSITY,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgFracture, lrg_fracture, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-fracture-error-quark, lrg_fracture_error)

GType
lrg_fracture_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FRACTURE_ERROR_INVALID, "LRG_FRACTURE_ERROR_INVALID", "invalid" },
			{ LRG_FRACTURE_ERROR_NOT_CLOSED, "LRG_FRACTURE_ERROR_NOT_CLOSED", "not-closed" },
			{ LRG_FRACTURE_ERROR_NOT_CONVEX, "LRG_FRACTURE_ERROR_NOT_CONVEX", "not-convex" },
			{ LRG_FRACTURE_ERROR_NO_SOURCE, "LRG_FRACTURE_ERROR_NO_SOURCE", "no-source" },
			{ LRG_FRACTURE_ERROR_PARSE, "LRG_FRACTURE_ERROR_PARSE", "parse" },
			{ LRG_FRACTURE_ERROR_NO_GPU, "LRG_FRACTURE_ERROR_NO_GPU", "no-gpu" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgFractureError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_fracture_seed_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_FRACTURE_SEED_RANDOM, "LRG_FRACTURE_SEED_RANDOM", "random" },
			{ LRG_FRACTURE_SEED_IMPACT, "LRG_FRACTURE_SEED_IMPACT", "impact" },
			{ LRG_FRACTURE_SEED_GRID, "LRG_FRACTURE_SEED_GRID", "grid" },
			{ LRG_FRACTURE_SEED_CUSTOM, "LRG_FRACTURE_SEED_CUSTOM", "custom" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgFractureSeedMode"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

static void
lrg_fracture_finalize (GObject *object)
{
	LrgFracture *self = LRG_FRACTURE (object);

	g_free (self->positions);
	g_free (self->texcoords);
	g_free (self->indices);
	g_free (self->materials);
	g_array_unref (self->seeds);
	g_ptr_array_unref (self->chunks);

	G_OBJECT_CLASS (lrg_fracture_parent_class)->finalize (object);
}

static void
lrg_fracture_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgFracture *self = LRG_FRACTURE (object);

	switch (prop_id)
	{
	case PROP_CHUNK_COUNT:
		if (self->chunk_count != g_value_get_uint (value))
		{
			self->chunk_count = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_SEED:
		if (self->seed != g_value_get_uint (value))
		{
			self->seed = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_SEED_MODE:
		if (self->seed_mode != (LrgFractureSeedMode) g_value_get_enum (value))
		{
			self->seed_mode = (LrgFractureSeedMode) g_value_get_enum (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_INTERIOR_MATERIAL:
		if (self->interior_material != g_value_get_uint (value))
		{
			self->interior_material = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_IMPACT_RADIUS:
		if (self->impact_radius != g_value_get_float (value))
		{
			self->impact_radius = g_value_get_float (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_GRID_JITTER:
		if (self->grid_jitter != g_value_get_float (value))
		{
			self->grid_jitter = g_value_get_float (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_IMPULSE_THRESHOLD:
		if (self->impulse_threshold != g_value_get_float (value))
		{
			self->impulse_threshold = g_value_get_float (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_DENSITY:
		if (self->density != g_value_get_float (value))
		{
			self->density = g_value_get_float (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_fracture_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgFracture *self = LRG_FRACTURE (object);

	switch (prop_id)
	{
	case PROP_CHUNK_COUNT:
		g_value_set_uint (value, self->chunk_count);
		break;
	case PROP_SEED:
		g_value_set_uint (value, self->seed);
		break;
	case PROP_SEED_MODE:
		g_value_set_enum (value, (gint) self->seed_mode);
		break;
	case PROP_INTERIOR_MATERIAL:
		g_value_set_uint (value, self->interior_material);
		break;
	case PROP_IMPACT_RADIUS:
		g_value_set_float (value, self->impact_radius);
		break;
	case PROP_GRID_JITTER:
		g_value_set_float (value, self->grid_jitter);
		break;
	case PROP_IMPULSE_THRESHOLD:
		g_value_set_float (value, self->impulse_threshold);
		break;
	case PROP_DENSITY:
		g_value_set_float (value, self->density);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_fracture_class_init (LrgFractureClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->finalize = lrg_fracture_finalize;
	object_class->set_property = lrg_fracture_set_property;
	object_class->get_property = lrg_fracture_get_property;

	/**
	 * LrgFracture:chunk-count:
	 *
	 * Seed points to place (the chunk count when every seed lands inside).
	 */
	properties[PROP_CHUNK_COUNT] =
		g_param_spec_uint ("chunk-count", NULL, NULL, 1, LRG_FRACTURE_MAX_CHUNKS, 8, rw);

	/**
	 * LrgFracture:seed:
	 *
	 * Random seed of the seed placement (same seed, same pieces).
	 */
	properties[PROP_SEED] =
		g_param_spec_uint ("seed", NULL, NULL, 0, G_MAXUINT, 1, rw);

	/**
	 * LrgFracture:seed-mode:
	 *
	 * How seeds are placed (#LrgFractureSeedMode).
	 */
	properties[PROP_SEED_MODE] =
		g_param_spec_enum ("seed-mode", NULL, NULL, LRG_TYPE_FRACTURE_SEED_MODE,
		                   LRG_FRACTURE_SEED_RANDOM, rw);

	/**
	 * LrgFracture:interior-material:
	 *
	 * Material id of the cut faces.
	 */
	properties[PROP_INTERIOR_MATERIAL] =
		g_param_spec_uint ("interior-material", NULL, NULL, 0, G_MAXUINT32, 1, rw);

	/**
	 * LrgFracture:impact-radius:
	 *
	 * Spread of %LRG_FRACTURE_SEED_IMPACT seeds round the impact point in
	 * metres; 0 uses half the bounds diagonal.
	 */
	properties[PROP_IMPACT_RADIUS] =
		g_param_spec_float ("impact-radius", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgFracture:grid-jitter:
	 *
	 * Random offset of %LRG_FRACTURE_SEED_GRID seeds as a fraction of a
	 * grid cell (0 = regular boxes).
	 */
	properties[PROP_GRID_JITTER] =
		g_param_spec_float ("grid-jitter", NULL, NULL, 0.0f, 1.0f, 0.3f, rw);

	/**
	 * LrgFracture:impulse-threshold:
	 *
	 * Contact impulse (N·s) at which lrg_fracture_should_break() says yes.
	 */
	properties[PROP_IMPULSE_THRESHOLD] =
		g_param_spec_float ("impulse-threshold", NULL, NULL, 0.0f, G_MAXFLOAT, 50.0f, rw);

	/**
	 * LrgFracture:density:
	 *
	 * Density in kg/m³ used for chunk masses (1000 = water, 2400 =
	 * concrete).
	 */
	properties[PROP_DENSITY] =
		g_param_spec_float ("density", NULL, NULL, 0.001f, G_MAXFLOAT, 1000.0f, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_fracture_init (LrgFracture *self)
{
	self->seeds = g_array_new (FALSE, FALSE, sizeof (gfloat));
	self->chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_fracture_chunk_unref);
	self->chunk_count = 8;
	self->seed = 1;
	self->seed_mode = LRG_FRACTURE_SEED_RANDOM;
	self->interior_material = 1;
	self->grid_jitter = 0.3f;
	self->impulse_threshold = 50.0f;
	self->density = 1000.0f;
}

LrgFracture *
lrg_fracture_new (void)
{
	return g_object_new (LRG_TYPE_FRACTURE, NULL);
}

/* ==========================================================================
 * Vector helpers
 * ========================================================================== */

static inline void
v3_sub (const gfloat *a, const gfloat *b, gfloat *out)
{
	out[0] = a[0] - b[0];
	out[1] = a[1] - b[1];
	out[2] = a[2] - b[2];
}

static inline gfloat
v3_dot (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
v3_cross (const gfloat *a, const gfloat *b, gfloat *out)
{
	gfloat x = a[1] * b[2] - a[2] * b[1];
	gfloat y = a[2] * b[0] - a[0] * b[2];
	gfloat z = a[0] * b[1] - a[1] * b[0];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

static inline gfloat
v3_len (const gfloat *a)
{
	return sqrtf (v3_dot (a, a));
}

/* ==========================================================================
 * Polyhedron clipping
 * ========================================================================== */

/* A corner of a polygon: position and texture coordinate. */
typedef struct
{
	gdouble p[3];
	gdouble uv[2];
} Corner;

/* A convex polygon face of a cell. */
typedef struct
{
	GArray  *corners;     /* Corner */
	gdouble  normal[3];
	guint32  material;
	gboolean interior;
} Face;

static void
face_clear (gpointer data)
{
	Face *f = data;

	g_clear_pointer (&f->corners, g_array_unref);
}

static GArray *
faces_new (void)
{
	GArray *a = g_array_new (FALSE, TRUE, sizeof (Face));

	g_array_set_clear_func (a, face_clear);
	return a;
}

static inline gdouble
d3_dot (const gdouble *a, const gdouble *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/*
 * Clips every face of @faces to dot (n, x) <= d and closes the cut with
 * one cap face.  @eps is the snapping distance.  Returns TRUE when the
 * plane cut something (the cell changed), *out_cap_area the cap's area.
 */
static gboolean
clip_cell (GArray        *faces,
           const gdouble *n,
           gdouble        d,
           gdouble        eps,
           guint32        material,
           gdouble       *out_cap_area)
{
	g_autoptr(GArray) result = faces_new ();
	g_autoptr(GArray) cut = g_array_new (FALSE, FALSE, sizeof (Corner));
	gboolean          changed = FALSE;
	guint             f;
	guint             i;

	*out_cap_area = 0.0;

	/* Quick reject: nothing beyond the plane. */
	for (f = 0; f < faces->len && !changed; f++)
	{
		const Face *face = &g_array_index (faces, Face, f);

		for (i = 0; i < face->corners->len; i++)
			if (d3_dot (n, g_array_index (face->corners, Corner, i).p) - d > eps)
			{
				changed = TRUE;
				break;
			}
	}
	if (!changed)
		return FALSE;

	for (f = 0; f < faces->len; f++)
	{
		Face   *face = &g_array_index (faces, Face, f);
		guint   count = face->corners->len;
		GArray *out = g_array_new (FALSE, FALSE, sizeof (Corner));
		Face    nf;

		/* Sutherland-Hodgman against one plane. */
		for (i = 0; i < count; i++)
		{
			const Corner *a = &g_array_index (face->corners, Corner, i);
			const Corner *b = &g_array_index (face->corners, Corner, (i + 1) % count);
			gdouble       da = d3_dot (n, a->p) - d;
			gdouble       db = d3_dot (n, b->p) - d;
			gboolean      ina = da <= eps;

			if (ina)
			{
				g_array_append_val (out, *a);
				if (fabs (da) <= eps)
					g_array_append_val (cut, *a);
			}
			if ((da < -eps && db > eps) || (da > eps && db < -eps))
			{
				Corner  c;
				gdouble t = da / (da - db);
				guint   k;

				for (k = 0; k < 3; k++)
					c.p[k] = a->p[k] + t * (b->p[k] - a->p[k]);
				for (k = 0; k < 2; k++)
					c.uv[k] = a->uv[k] + t * (b->uv[k] - a->uv[k]);
				g_array_append_val (out, c);
				g_array_append_val (cut, c);
			}
		}

		if (out->len >= 3)
		{
			nf = *face;
			nf.corners = out;
			g_array_append_val (result, nf);
		}
		else
		{
			g_array_unref (out);
		}
	}

	/* Cap: dedupe the cut points and sort them round their centroid. */
	if (cut->len >= 3)
	{
		g_autoptr(GArray) uniq = g_array_new (FALSE, FALSE, sizeof (Corner));
		gdouble           c[3] = { 0.0, 0.0, 0.0 };
		gdouble           u[3];
		gdouble           v[3];
		gdouble           len;
		g_autofree gdouble *angle = NULL;
		guint             j;

		for (i = 0; i < cut->len; i++)
		{
			const Corner *p = &g_array_index (cut, Corner, i);
			gboolean      dup = FALSE;

			for (j = 0; j < uniq->len; j++)
			{
				const Corner *q = &g_array_index (uniq, Corner, j);
				gdouble       dx = p->p[0] - q->p[0];
				gdouble       dy = p->p[1] - q->p[1];
				gdouble       dz = p->p[2] - q->p[2];

				if (dx * dx + dy * dy + dz * dz <= 4.0 * eps * eps)
				{
					dup = TRUE;
					break;
				}
			}
			if (!dup)
				g_array_append_val (uniq, *p);
		}

		if (uniq->len >= 3)
		{
			Face face;

			for (i = 0; i < uniq->len; i++)
				for (j = 0; j < 3; j++)
					c[j] += g_array_index (uniq, Corner, i).p[j] / (gdouble) uniq->len;

			/* In-plane basis (u, v) with u x v = n. */
			if (fabs (n[0]) < 0.9)
			{
				u[0] = 0.0; u[1] = n[2]; u[2] = -n[1];
			}
			else
			{
				u[0] = -n[2]; u[1] = 0.0; u[2] = n[0];
			}
			len = sqrt (d3_dot (u, u));
			u[0] /= len; u[1] /= len; u[2] /= len;
			v[0] = n[1] * u[2] - n[2] * u[1];
			v[1] = n[2] * u[0] - n[0] * u[2];
			v[2] = n[0] * u[1] - n[1] * u[0];

			angle = g_new (gdouble, uniq->len);
			for (i = 0; i < uniq->len; i++)
			{
				Corner *p = &g_array_index (uniq, Corner, i);
				gdouble r[3] = { p->p[0] - c[0], p->p[1] - c[1], p->p[2] - c[2] };

				angle[i] = atan2 (d3_dot (r, v), d3_dot (r, u));
				/* Planar texture projection of the cut, 1 unit per metre. */
				p->uv[0] = d3_dot (p->p, u);
				p->uv[1] = d3_dot (p->p, v);
			}
			/* Insertion sort by angle (counter-clockwise seen from +n). */
			for (i = 1; i < uniq->len; i++)
			{
				Corner  key = g_array_index (uniq, Corner, i);
				gdouble ka = angle[i];
				gint    k = (gint) i - 1;

				while (k >= 0 && angle[k] > ka)
				{
					g_array_index (uniq, Corner, k + 1) = g_array_index (uniq, Corner, k);
					angle[k + 1] = angle[k];
					k--;
				}
				g_array_index (uniq, Corner, k + 1) = key;
				angle[k + 1] = ka;
			}

			/* Area of the cap (fan from corner 0). */
			for (i = 1; i + 1 < uniq->len; i++)
			{
				const gdouble *a = g_array_index (uniq, Corner, 0).p;
				const gdouble *b = g_array_index (uniq, Corner, i).p;
				const gdouble *e = g_array_index (uniq, Corner, i + 1).p;
				gdouble        ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
				gdouble        ae[3] = { e[0] - a[0], e[1] - a[1], e[2] - a[2] };
				gdouble        cr[3] = { ab[1] * ae[2] - ab[2] * ae[1], ab[2] * ae[0] - ab[0] * ae[2],
				                         ab[0] * ae[1] - ab[1] * ae[0] };

				*out_cap_area += 0.5 * sqrt (d3_dot (cr, cr));
			}

			face.corners = g_array_ref (uniq);
			face.normal[0] = n[0];
			face.normal[1] = n[1];
			face.normal[2] = n[2];
			face.material = material;
			face.interior = TRUE;
			g_array_append_val (result, face);
		}
	}

	/* Swap the result in. */
	g_array_set_size (faces, 0);
	for (f = 0; f < result->len; f++)
	{
		Face *face = &g_array_index (result, Face, f);

		g_array_append_val (faces, *face);
		face->corners = NULL;
	}
	return TRUE;
}

/* ==========================================================================
 * Mass properties (Eberly, "Polyhedral Mass Properties (Revisited)")
 * ========================================================================== */

static void
subexpressions (gdouble  w0,
                gdouble  w1,
                gdouble  w2,
                gdouble *f1,
                gdouble *f2,
                gdouble *f3,
                gdouble *g0,
                gdouble *g1,
                gdouble *g2)
{
	gdouble temp0 = w0 + w1;
	gdouble temp1 = w0 * w0;
	gdouble temp2 = temp1 + w1 * temp0;

	*f1 = temp0 + w2;
	*f2 = temp2 + w2 * (*f1);
	*f3 = w0 * temp1 + w1 * temp2 + w2 * (*f2);
	*g0 = *f2 + w0 * (*f1 + w0);
	*g1 = *f2 + w1 * (*f1 + w1);
	*g2 = *f2 + w2 * (*f1 + w2);
}

/*
 * Volume, centre of mass and inertia (unit density, about the COM,
 * column-major) of a closed, outward-wound triangle mesh.
 */
static void
mesh_mass_properties (const gfloat  *pos,
                      const guint32 *idx,
                      guint          n_idx,
                      gfloat        *out_volume,
                      gfloat        *out_com,
                      gfloat        *out_inertia)
{
	static const gdouble mult[10] = {
		1.0 / 6.0, 1.0 / 24.0, 1.0 / 24.0, 1.0 / 24.0, 1.0 / 60.0,
		1.0 / 60.0, 1.0 / 60.0, 1.0 / 120.0, 1.0 / 120.0, 1.0 / 120.0
	};
	gdouble intg[10] = { 0 };
	gdouble mass;
	gdouble cm[3];
	gdouble xx, yy, zz, xy, yz, xz;
	guint   t;
	guint   i;

	for (t = 0; t + 2 < n_idx; t += 3)
	{
		const gfloat *p0 = pos + 3 * idx[t];
		const gfloat *p1 = pos + 3 * idx[t + 1];
		const gfloat *p2 = pos + 3 * idx[t + 2];
		gdouble x0 = p0[0], y0 = p0[1], z0 = p0[2];
		gdouble x1 = p1[0], y1 = p1[1], z1 = p1[2];
		gdouble x2 = p2[0], y2 = p2[1], z2 = p2[2];
		gdouble a1 = x1 - x0, b1 = y1 - y0, c1 = z1 - z0;
		gdouble a2 = x2 - x0, b2 = y2 - y0, c2 = z2 - z0;
		gdouble d0 = b1 * c2 - b2 * c1;
		gdouble d1 = a2 * c1 - a1 * c2;
		gdouble d2 = a1 * b2 - a2 * b1;
		gdouble f1x, f2x, f3x, g0x, g1x, g2x;
		gdouble f1y, f2y, f3y, g0y, g1y, g2y;
		gdouble f1z, f2z, f3z, g0z, g1z, g2z;

		subexpressions (x0, x1, x2, &f1x, &f2x, &f3x, &g0x, &g1x, &g2x);
		subexpressions (y0, y1, y2, &f1y, &f2y, &f3y, &g0y, &g1y, &g2y);
		subexpressions (z0, z1, z2, &f1z, &f2z, &f3z, &g0z, &g1z, &g2z);

		intg[0] += d0 * f1x;
		intg[1] += d0 * f2x;
		intg[2] += d1 * f2y;
		intg[3] += d2 * f2z;
		intg[4] += d0 * f3x;
		intg[5] += d1 * f3y;
		intg[6] += d2 * f3z;
		intg[7] += d0 * (y0 * g0x + y1 * g1x + y2 * g2x);
		intg[8] += d1 * (z0 * g0y + z1 * g1y + z2 * g2y);
		intg[9] += d2 * (x0 * g0z + x1 * g1z + x2 * g2z);
	}
	for (i = 0; i < 10; i++)
		intg[i] *= mult[i];

	mass = intg[0];
	if (mass <= 0.0)
	{
		*out_volume = 0.0f;
		memset (out_com, 0, 3 * sizeof (gfloat));
		memset (out_inertia, 0, 9 * sizeof (gfloat));
		return;
	}
	cm[0] = intg[1] / mass;
	cm[1] = intg[2] / mass;
	cm[2] = intg[3] / mass;
	xx = intg[5] + intg[6] - mass * (cm[1] * cm[1] + cm[2] * cm[2]);
	yy = intg[4] + intg[6] - mass * (cm[2] * cm[2] + cm[0] * cm[0]);
	zz = intg[4] + intg[5] - mass * (cm[0] * cm[0] + cm[1] * cm[1]);
	xy = -(intg[7] - mass * cm[0] * cm[1]);
	yz = -(intg[8] - mass * cm[1] * cm[2]);
	xz = -(intg[9] - mass * cm[2] * cm[0]);

	*out_volume = (gfloat) mass;
	out_com[0] = (gfloat) cm[0];
	out_com[1] = (gfloat) cm[1];
	out_com[2] = (gfloat) cm[2];
	out_inertia[0] = (gfloat) xx; out_inertia[3] = (gfloat) xy; out_inertia[6] = (gfloat) xz;
	out_inertia[1] = (gfloat) xy; out_inertia[4] = (gfloat) yy; out_inertia[7] = (gfloat) yz;
	out_inertia[2] = (gfloat) xz; out_inertia[5] = (gfloat) yz; out_inertia[8] = (gfloat) zz;
}

/* ==========================================================================
 * Source mesh
 * ========================================================================== */

static void
clear_chunks (LrgFracture *self)
{
	g_ptr_array_set_size (self->chunks, 0);
}

/* Checks that every directed edge has exactly one opposite (a closed 2-manifold). */
static gboolean
check_closed (guint          n_vertices,
              guint          n_indices,
              const guint32 *indices)
{
	g_autoptr(GHashTable) edges = g_hash_table_new (g_int64_hash, g_int64_equal);
	g_autofree gint64    *keys = g_new (gint64, n_indices);
	guint                 i;

	for (i = 0; i < n_indices; i++)
	{
		guint32 a = indices[i];
		guint32 b = indices[i - i % 3 + (i % 3 + 1) % 3];

		keys[i] = ((gint64) a << 32) | (gint64) b;
		if (g_hash_table_contains (edges, &keys[i]))
			return FALSE;   /* the same directed edge twice: not manifold */
		g_hash_table_add (edges, &keys[i]);
	}
	for (i = 0; i < n_indices; i++)
	{
		gint64 rev = ((keys[i] & 0xffffffff) << 32) | (keys[i] >> 32);

		if (!g_hash_table_contains (edges, &rev))
			return FALSE;
	}
	(void) n_vertices;
	return TRUE;
}

gboolean
lrg_fracture_set_mesh (LrgFracture    *self,
                       guint           n_vertices,
                       const gfloat   *positions,
                       const gfloat   *texcoords,
                       guint           n_indices,
                       const guint32  *indices,
                       const guint32  *materials,
                       GError        **error)
{
	gfloat size;
	gfloat bmin[3];
	gfloat bmax[3];
	gfloat volume;
	gfloat com[3];
	gfloat inertia[9];
	guint  i;
	guint  t;
	guint  k;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);

	if (n_vertices < 4 || n_vertices > FRACTURE_MAX_SOURCE_VERTS || positions == NULL
	    || indices == NULL || n_indices < 12 || (n_indices % 3) != 0)
	{
		g_set_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID,
		             "Invalid fracture source: %u vertices, %u indices", n_vertices, n_indices);
		return FALSE;
	}
	for (i = 0; i < 3 * n_vertices; i++)
	{
		if (!isfinite (positions[i]))
		{
			g_set_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID,
			             "Vertex %u of the fracture source is not finite", i / 3);
			return FALSE;
		}
	}
	for (i = 0; i < n_indices; i++)
	{
		if (indices[i] >= n_vertices)
		{
			g_set_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID,
			             "Index %u of the fracture source is out of range", i);
			return FALSE;
		}
	}
	if (!check_closed (n_vertices, n_indices, indices))
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NOT_CLOSED,
		                     "The fracture source is not a closed, welded triangle mesh");
		return FALSE;
	}

	memcpy (bmin, positions, 3 * sizeof (gfloat));
	memcpy (bmax, positions, 3 * sizeof (gfloat));
	for (i = 1; i < n_vertices; i++)
		for (k = 0; k < 3; k++)
		{
			bmin[k] = MIN (bmin[k], positions[3 * i + k]);
			bmax[k] = MAX (bmax[k], positions[3 * i + k]);
		}
	{
		gfloat diag[3];

		v3_sub (bmax, bmin, diag);
		size = v3_len (diag);
	}

	/* Convex: every vertex on or behind every triangle's plane. */
	for (t = 0; t < n_indices; t += 3)
	{
		const gfloat *a = positions + 3 * indices[t];
		gfloat        e1[3];
		gfloat        e2[3];
		gfloat        nrm[3];
		gfloat        len;

		v3_sub (positions + 3 * indices[t + 1], a, e1);
		v3_sub (positions + 3 * indices[t + 2], a, e2);
		v3_cross (e1, e2, nrm);
		len = v3_len (nrm);
		if (len <= 1.0e-12f * size * size)
			continue;
		for (i = 0; i < n_vertices; i++)
		{
			gfloat r[3];

			v3_sub (positions + 3 * i, a, r);
			if (v3_dot (r, nrm) / len > 1.0e-4f * size)
			{
				g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NOT_CONVEX,
				                     "The fracture source is not convex (or is wound inside out)");
				return FALSE;
			}
		}
	}

	mesh_mass_properties (positions, indices, n_indices, &volume, com, inertia);
	if (!(volume > 0.0f))
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID,
		                     "The fracture source has no volume (or is wound inside out)");
		return FALSE;
	}

	g_free (self->positions);
	g_free (self->texcoords);
	g_free (self->indices);
	g_free (self->materials);
	self->n_vertices = n_vertices;
	self->n_indices = n_indices;
	self->positions = g_memdup2 (positions, 3 * sizeof (gfloat) * n_vertices);
	self->texcoords = texcoords != NULL ? g_memdup2 (texcoords, 2 * sizeof (gfloat) * n_vertices)
	                                    : g_new0 (gfloat, 2 * (gsize) n_vertices);
	self->indices = g_memdup2 (indices, sizeof (guint32) * n_indices);
	self->materials = materials != NULL ? g_memdup2 (materials, sizeof (guint32) * (n_indices / 3))
	                                    : g_new0 (guint32, n_indices / 3);
	self->source_volume = volume;
	memcpy (self->bmin, bmin, sizeof (bmin));
	memcpy (self->bmax, bmax, sizeof (bmax));
	clear_chunks (self);
	return TRUE;
}

gboolean
lrg_fracture_set_box (LrgFracture   *self,
                      const gfloat  *half_extents,
                      GError       **error)
{
	/* Face axis, then two in-plane axes giving a CCW (outward) quad. */
	static const gint faces[6][3] = {
		{ 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 }, { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 }
	};
	gfloat  pos[72];
	gfloat  uv[48];
	guint32 idx[36];
	guint   f;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	g_return_val_if_fail (half_extents != NULL, FALSE);
	if (!(half_extents[0] > 0.0f) || !(half_extents[1] > 0.0f) || !(half_extents[2] > 0.0f)
	    || !isfinite (half_extents[0]) || !isfinite (half_extents[1]) || !isfinite (half_extents[2]))
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID,
		                     "Box half extents must be positive");
		return FALSE;
	}

	for (f = 0; f < 6; f++)
	{
		/* Faces 0,2,4 are +axis, 1,3,5 -axis; the (u, v) order flips with the sign. */
		gint   axis = faces[f][0];
		gint   ua = faces[f][1];
		gint   va = faces[f][2];
		gfloat sign = (f % 2 == 0) ? 1.0f : -1.0f;
		static const gfloat cu[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
		static const gfloat cv[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
		guint  c;

		for (c = 0; c < 4; c++)
		{
			gfloat *p = pos + 3 * (4 * f + c);

			p[axis] = sign * half_extents[axis];
			p[ua] = cu[c] * half_extents[ua];
			p[va] = cv[c] * half_extents[va];
			uv[2 * (4 * f + c)] = 0.5f * (cu[c] + 1.0f);
			uv[2 * (4 * f + c) + 1] = 0.5f * (1.0f - cv[c]);
		}
		idx[6 * f] = 4 * f;
		idx[6 * f + 1] = 4 * f + 1;
		idx[6 * f + 2] = 4 * f + 2;
		idx[6 * f + 3] = 4 * f;
		idx[6 * f + 4] = 4 * f + 2;
		idx[6 * f + 5] = 4 * f + 3;
		/* Make the winding outward. */
		{
			gfloat e1[3];
			gfloat e2[3];
			gfloat n[3];

			v3_sub (pos + 3 * (4 * f + 1), pos + 3 * (4 * f), e1);
			v3_sub (pos + 3 * (4 * f + 2), pos + 3 * (4 * f), e2);
			v3_cross (e1, e2, n);
			if (n[axis] * sign < 0.0f)
			{
				idx[6 * f + 1] = 4 * f + 2;
				idx[6 * f + 2] = 4 * f + 1;
				idx[6 * f + 4] = 4 * f + 3;
				idx[6 * f + 5] = 4 * f + 2;
			}
		}
	}

	/*
	 * The box's 24 corners are split per face (texture coordinates), so
	 * weld positions for the closed-mesh check: map each corner to the
	 * first one at the same place.
	 */
	{
		guint32 weld[24];
		guint32 welded_idx[36];
		gfloat  welded_pos[24];
		gfloat  welded_uv[16];
		guint   n = 0;
		guint   i;
		guint   j;

		for (i = 0; i < 24; i++)
		{
			weld[i] = G_MAXUINT32;
			for (j = 0; j < n; j++)
				if (welded_pos[3 * j] == pos[3 * i] && welded_pos[3 * j + 1] == pos[3 * i + 1]
				    && welded_pos[3 * j + 2] == pos[3 * i + 2])
					weld[i] = j;
			if (weld[i] == G_MAXUINT32)
			{
				memcpy (welded_pos + 3 * n, pos + 3 * i, 3 * sizeof (gfloat));
				memcpy (welded_uv + 2 * n, uv + 2 * i, 2 * sizeof (gfloat));
				weld[i] = n++;
			}
		}
		for (i = 0; i < 36; i++)
			welded_idx[i] = weld[idx[i]];
		if (!lrg_fracture_set_mesh (self, n, welded_pos, NULL, 36, welded_idx, NULL, error))
			return FALSE;
	}

	/*
	 * Keep the per-face texture coordinates: the clipper interpolates
	 * UVs per polygon corner, so store the unwelded corners for faces.
	 */
	g_free (self->positions);
	g_free (self->texcoords);
	g_free (self->indices);
	self->n_vertices = 24;
	self->positions = g_memdup2 (pos, sizeof (pos));
	self->texcoords = g_memdup2 (uv, sizeof (uv));
	self->indices = g_memdup2 (idx, sizeof (idx));
	return TRUE;
}

gboolean
lrg_fracture_has_source (LrgFracture *self)
{
	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	return self->positions != NULL;
}

gfloat
lrg_fracture_get_source_volume (LrgFracture *self)
{
	g_return_val_if_fail (LRG_IS_FRACTURE (self), 0.0f);
	return self->positions != NULL ? self->source_volume : 0.0f;
}

gboolean
lrg_fracture_get_source_bounds (LrgFracture *self,
                                gfloat      *out_min,
                                gfloat      *out_max)
{
	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	g_return_val_if_fail (out_min != NULL && out_max != NULL, FALSE);
	if (self->positions == NULL)
		return FALSE;
	memcpy (out_min, self->bmin, 3 * sizeof (gfloat));
	memcpy (out_max, self->bmax, 3 * sizeof (gfloat));
	return TRUE;
}

void
lrg_fracture_set_impact_point (LrgFracture  *self,
                               const gfloat *point)
{
	g_return_if_fail (LRG_IS_FRACTURE (self));
	self->has_impact = point != NULL;
	if (point != NULL)
		memcpy (self->impact, point, sizeof (self->impact));
}

gboolean
lrg_fracture_get_impact_point (LrgFracture *self,
                               gfloat      *out)
{
	guint k;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);
	if (self->has_impact)
		memcpy (out, self->impact, sizeof (self->impact));
	else
		for (k = 0; k < 3; k++)
			out[k] = 0.5f * (self->bmin[k] + self->bmax[k]);
	return self->has_impact;
}

gboolean
lrg_fracture_set_seed_points (LrgFracture  *self,
                              const gfloat *points,
                              guint         n_floats)
{
	guint i;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	if (points == NULL || n_floats == 0)
	{
		g_array_set_size (self->seeds, 0);
		if (self->seed_mode == LRG_FRACTURE_SEED_CUSTOM)
			g_object_set (self, "seed-mode", LRG_FRACTURE_SEED_RANDOM, NULL);
		return TRUE;
	}
	if (n_floats % 3 != 0 || n_floats > 3 * LRG_FRACTURE_MAX_CHUNKS)
		return FALSE;
	for (i = 0; i < n_floats; i++)
		if (!isfinite (points[i]))
			return FALSE;
	g_array_set_size (self->seeds, 0);
	g_array_append_vals (self->seeds, points, n_floats);
	g_object_set (self, "seed-mode", LRG_FRACTURE_SEED_CUSTOM, NULL);
	return TRUE;
}

const gfloat *
lrg_fracture_get_seed_points (LrgFracture *self,
                              guint       *n_floats)
{
	g_return_val_if_fail (LRG_IS_FRACTURE (self), NULL);
	if (n_floats != NULL)
		*n_floats = self->seeds->len;
	return self->seeds->len > 0 ? (const gfloat *) self->seeds->data : NULL;
}

/* ==========================================================================
 * Seeds
 * ========================================================================== */

/* TRUE when @p is inside the convex source (behind every face plane). */
static gboolean
inside_source (LrgFracture  *self,
               const gfloat *p)
{
	guint t;

	for (t = 0; t < self->n_indices; t += 3)
	{
		const gfloat *a = self->positions + 3 * self->indices[t];
		gfloat        e1[3];
		gfloat        e2[3];
		gfloat        n[3];
		gfloat        r[3];

		v3_sub (self->positions + 3 * self->indices[t + 1], a, e1);
		v3_sub (self->positions + 3 * self->indices[t + 2], a, e2);
		v3_cross (e1, e2, n);
		v3_sub (p, a, r);
		if (v3_dot (r, n) > 0.0f)
			return FALSE;
	}
	return TRUE;
}

static void
generate_seeds (LrgFracture *self)
{
	g_autoptr(GRand) rng = g_rand_new_with_seed (self->seed);
	gfloat           ext[3];
	guint            target = self->chunk_count;
	guint            tries = 0;
	guint            k;

	g_array_set_size (self->seeds, 0);
	v3_sub (self->bmax, self->bmin, ext);

	switch (self->seed_mode)
	{
	case LRG_FRACTURE_SEED_GRID:
		{
			gfloat vol = MAX (ext[0], 1e-6f) * MAX (ext[1], 1e-6f) * MAX (ext[2], 1e-6f);
			gfloat cell = cbrtf (vol / (gfloat) target);
			guint  dims[3];
			guint  x;
			guint  y;
			guint  z;

			/*
			 * The grid whose cell count is closest to the target, ties to
			 * the one whose cells are closest to cubes (smallest summed
			 * log ratio of the per-axis counts to the ideal ones).
			 */
			{
				gfloat best_score = G_MAXFLOAT;
				guint  best_diff = G_MAXUINT;
				guint  nx;
				guint  ny;
				guint  nz;

				dims[0] = dims[1] = dims[2] = 1;
				for (nx = 1; nx <= target; nx++)
				for (ny = 1; nx * ny <= target; ny++)
				for (nz = 1; nx * ny * nz <= target + target / 2 + 1; nz++)
				{
					guint  prod = nx * ny * nz;
					guint  diff = prod > target ? prod - target : target - prod;
					gfloat score = fabsf (logf ((gfloat) nx * cell / MAX (ext[0], 1e-6f)))
					               + fabsf (logf ((gfloat) ny * cell / MAX (ext[1], 1e-6f)))
					               + fabsf (logf ((gfloat) nz * cell / MAX (ext[2], 1e-6f)));

					if (diff < best_diff || (diff == best_diff && score < best_score))
					{
						best_diff = diff;
						best_score = score;
						dims[0] = nx;
						dims[1] = ny;
						dims[2] = nz;
					}
				}
			}
			for (z = 0; z < dims[2]; z++)
			for (y = 0; y < dims[1]; y++)
			for (x = 0; x < dims[0]; x++)
			{
				guint  c[3] = { x, y, z };
				gfloat p[3];

				for (k = 0; k < 3; k++)
				{
					gfloat step = ext[k] / (gfloat) dims[k];
					gfloat jitter = self->grid_jitter * ((gfloat) g_rand_double (rng) - 0.5f);

					p[k] = self->bmin[k] + step * ((gfloat) c[k] + 0.5f + jitter);
				}
				if (inside_source (self, p))
					g_array_append_vals (self->seeds, p, 3);
			}
		}
		break;

	case LRG_FRACTURE_SEED_IMPACT:
		{
			gfloat centre[3];
			gfloat radius = self->impact_radius > 0.0f ? self->impact_radius : 0.5f * v3_len (ext);

			lrg_fracture_get_impact_point (self, centre);
			while (self->seeds->len < 3 * target && tries++ < 64 * target)
			{
				gfloat dir[3];
				gfloat len;
				gfloat r;
				gfloat p[3];

				for (k = 0; k < 3; k++)
					dir[k] = (gfloat) g_rand_double_range (rng, -1.0, 1.0);
				len = v3_len (dir);
				if (len > 1.0f || len < 1e-4f)
					continue;
				/* u^3 radial distribution: dense at the impact, sparse far away. */
				r = (gfloat) g_rand_double (rng);
				r = radius * r * r * r;
				for (k = 0; k < 3; k++)
					p[k] = centre[k] + dir[k] / len * r;
				if (inside_source (self, p))
					g_array_append_vals (self->seeds, p, 3);
			}
		}
		break;

	case LRG_FRACTURE_SEED_RANDOM:
	case LRG_FRACTURE_SEED_CUSTOM:
	default:
		while (self->seeds->len < 3 * target && tries++ < 64 * target)
		{
			gfloat p[3];

			for (k = 0; k < 3; k++)
				p[k] = self->bmin[k] + ext[k] * (gfloat) g_rand_double (rng);
			if (inside_source (self, p))
				g_array_append_vals (self->seeds, p, 3);
		}
		break;
	}
}

/* ==========================================================================
 * Build
 * ========================================================================== */

typedef struct
{
	gfloat dist2;
	guint  index;
} SeedDistance;

static gint
seed_distance_compare (gconstpointer a,
                       gconstpointer b)
{
	const SeedDistance *x = a;
	const SeedDistance *y = b;

	if (x->dist2 != y->dist2)
		return x->dist2 < y->dist2 ? -1 : 1;
	return x->index < y->index ? -1 : (x->index > y->index ? 1 : 0);
}

/* The source mesh as polygon faces (one triangle each). */
static GArray *
source_faces (LrgFracture *self)
{
	GArray *faces = faces_new ();
	guint   t;

	for (t = 0; t < self->n_indices; t += 3)
	{
		Face    f;
		gdouble e1[3];
		gdouble e2[3];
		gdouble len;
		guint   c;
		guint   k;

		f.corners = g_array_new (FALSE, FALSE, sizeof (Corner));
		for (c = 0; c < 3; c++)
		{
			Corner cr;
			guint  v = self->indices[t + c];

			for (k = 0; k < 3; k++)
				cr.p[k] = self->positions[3 * v + k];
			cr.uv[0] = self->texcoords[2 * v];
			cr.uv[1] = self->texcoords[2 * v + 1];
			g_array_append_val (f.corners, cr);
		}
		for (k = 0; k < 3; k++)
		{
			e1[k] = g_array_index (f.corners, Corner, 1).p[k] - g_array_index (f.corners, Corner, 0).p[k];
			e2[k] = g_array_index (f.corners, Corner, 2).p[k] - g_array_index (f.corners, Corner, 0).p[k];
		}
		f.normal[0] = e1[1] * e2[2] - e1[2] * e2[1];
		f.normal[1] = e1[2] * e2[0] - e1[0] * e2[2];
		f.normal[2] = e1[0] * e2[1] - e1[1] * e2[0];
		len = sqrt (d3_dot (f.normal, f.normal));
		if (len <= 0.0)
		{
			g_array_unref (f.corners);
			continue;
		}
		for (k = 0; k < 3; k++)
			f.normal[k] /= len;
		f.material = self->materials[t / 3];
		f.interior = FALSE;
		g_array_append_val (faces, f);
	}
	return faces;
}

/*
 * Removes corners lying on the segment between their neighbours (cuts
 * through a triangulated face leave such points on the cap and on the
 * clipped faces); the fan triangulation would otherwise emit slivers.
 */
static void
drop_collinear (GArray  *corners,
                gdouble  eps)
{
	gboolean again = TRUE;

	while (again && corners->len > 3)
	{
		guint i;

		again = FALSE;
		for (i = 0; i < corners->len; i++)
		{
			const gdouble *a = g_array_index (corners, Corner, (i + corners->len - 1) % corners->len).p;
			const gdouble *b = g_array_index (corners, Corner, i).p;
			const gdouble *e = g_array_index (corners, Corner, (i + 1) % corners->len).p;
			gdouble        ae[3] = { e[0] - a[0], e[1] - a[1], e[2] - a[2] };
			gdouble        ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
			gdouble        cr[3] = { ab[1] * ae[2] - ab[2] * ae[1], ab[2] * ae[0] - ab[0] * ae[2],
			                         ab[0] * ae[1] - ab[1] * ae[0] };
			gdouble        len = sqrt (d3_dot (ae, ae));

			/* Distance of b from the line a-e: |ab x ae| / |ae|. */
			if (len <= eps || sqrt (d3_dot (cr, cr)) <= eps * len)
			{
				g_array_remove_index (corners, i);
				again = TRUE;
				break;
			}
		}
	}
}

/* Triangulates a finished cell into a chunk and computes its properties. */
static LrgFractureChunk *
chunk_from_faces (GArray       *faces,
                  const gfloat *seed,
                  GArray       *neighbors,
                  gdouble       eps)
{
	LrgFractureChunk *c = chunk_new ();
	GArray           *pos = g_array_new (FALSE, FALSE, sizeof (gfloat));
	GArray           *nrm = g_array_new (FALSE, FALSE, sizeof (gfloat));
	GArray           *uv = g_array_new (FALSE, FALSE, sizeof (gfloat));
	GArray           *idx = g_array_new (FALSE, FALSE, sizeof (guint32));
	GArray           *mat = g_array_new (FALSE, FALSE, sizeof (guint32));
	GArray           *hull = g_array_new (FALSE, FALSE, sizeof (gfloat));
	guint             f;
	guint             i;
	guint             k;

	memcpy (c->seed, seed, 3 * sizeof (gfloat));
	for (f = 0; f < faces->len; f++)
	{
		Face    *face = &g_array_index (faces, Face, f);
		guint32  base = pos->len / 3;

		drop_collinear (face->corners, eps);
		if (face->corners->len < 3)
			continue;

		for (i = 0; i < face->corners->len; i++)
		{
			const Corner *cr = &g_array_index (face->corners, Corner, i);
			gfloat        p[3] = { (gfloat) cr->p[0], (gfloat) cr->p[1], (gfloat) cr->p[2] };
			gfloat        n[3] = { (gfloat) face->normal[0], (gfloat) face->normal[1], (gfloat) face->normal[2] };
			gfloat        t[2] = { (gfloat) cr->uv[0], (gfloat) cr->uv[1] };
			gboolean      dup = FALSE;
			guint         j;

			g_array_append_vals (pos, p, 3);
			g_array_append_vals (nrm, n, 3);
			g_array_append_vals (uv, t, 2);
			for (j = 0; j < hull->len; j += 3)
			{
				const gfloat *h = &g_array_index (hull, gfloat, j);
				gfloat        d[3];

				v3_sub (h, p, d);
				if (v3_dot (d, d) <= (gfloat) (4.0 * eps * eps))
				{
					dup = TRUE;
					break;
				}
			}
			if (!dup)
				g_array_append_vals (hull, p, 3);
		}
		for (i = 1; i + 1 < face->corners->len; i++)
		{
			guint32 tri[3] = { base, base + i, base + i + 1 };

			g_array_append_vals (idx, tri, 3);
			g_array_append_val (mat, face->material);
		}
	}

	c->n_vertices = pos->len / 3;
	c->n_indices = idx->len;
	c->n_hull = hull->len / 3;
	c->positions = (gfloat *) g_array_free (pos, FALSE);
	c->normals = (gfloat *) g_array_free (nrm, FALSE);
	c->texcoords = (gfloat *) g_array_free (uv, FALSE);
	c->indices = (guint32 *) g_array_free (idx, FALSE);
	c->materials = (guint32 *) g_array_free (mat, FALSE);
	c->hull = (gfloat *) g_array_free (hull, FALSE);
	c->n_neighbors = neighbors->len;
	c->neighbors = g_memdup2 (neighbors->data, sizeof (guint32) * neighbors->len);

	mesh_mass_properties (c->positions, c->indices, c->n_indices, &c->volume, c->com, c->inertia);
	memcpy (c->bmin, c->hull, 3 * sizeof (gfloat));
	memcpy (c->bmax, c->hull, 3 * sizeof (gfloat));
	for (i = 1; i < c->n_hull; i++)
		for (k = 0; k < 3; k++)
		{
			c->bmin[k] = MIN (c->bmin[k], c->hull[3 * i + k]);
			c->bmax[k] = MAX (c->bmax[k], c->hull[3 * i + k]);
		}
	return c;
}

gboolean
lrg_fracture_build (LrgFracture  *self,
                    GError      **error)
{
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GArray)    chunk_seed = g_array_new (FALSE, FALSE, sizeof (guint));
	g_autofree SeedDistance *order = NULL;
	const gfloat        *seeds;
	guint                n_seeds;
	gfloat               ext[3];
	gdouble              eps;
	guint                i;
	guint                j;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	if (self->positions == NULL)
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_SOURCE,
		                     "The fracture has no source mesh");
		return FALSE;
	}
	if (self->seed_mode != LRG_FRACTURE_SEED_CUSTOM || self->seeds->len == 0)
	{
		if (self->seed_mode == LRG_FRACTURE_SEED_CUSTOM)
		{
			g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID,
			                     "Custom seed mode without seed points");
			return FALSE;
		}
		generate_seeds (self);
	}

	v3_sub (self->bmax, self->bmin, ext);
	eps = 1.0e-5 * (gdouble) v3_len (ext);
	seeds = (const gfloat *) self->seeds->data;
	n_seeds = self->seeds->len / 3;
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_fracture_chunk_unref);
	order = g_new (SeedDistance, MAX (n_seeds, 1u));

	for (i = 0; i < n_seeds; i++)
	{
		g_autoptr(GArray) faces = NULL;
		g_autoptr(GArray) neighbors = g_array_new (FALSE, FALSE, sizeof (guint32));
		const gfloat     *si = seeds + 3 * i;
		gboolean          coincident = FALSE;

		if (!inside_source (self, si))
			continue;

		/* Other seeds, nearest first. */
		for (j = 0; j < n_seeds; j++)
		{
			gfloat d[3];

			v3_sub (seeds + 3 * j, si, d);
			order[j].dist2 = v3_dot (d, d);
			order[j].index = j;
			if (j < i && order[j].dist2 <= (gfloat) (eps * eps))
				coincident = TRUE;
		}
		if (coincident)
			continue;   /* a duplicate seed: the first copy owns the cell */
		qsort (order, n_seeds, sizeof (SeedDistance), seed_distance_compare);

		faces = source_faces (self);
		for (j = 0; j < n_seeds && faces->len > 0; j++)
		{
			const gfloat *sj = seeds + 3 * order[j].index;
			gdouble       n[3];
			gdouble       mid[3];
			gdouble       len;
			gdouble       radius2 = 0.0;
			gdouble       cap_area;
			guint         f;
			guint         k;

			if (order[j].index == i || order[j].dist2 <= (gfloat) (eps * eps))
				continue;

			/* No bisector further than the cell's furthest corner can cut it. */
			for (f = 0; f < faces->len; f++)
			{
				const Face *face = &g_array_index (faces, Face, f);

				for (k = 0; k < face->corners->len; k++)
				{
					const gdouble *p = g_array_index (face->corners, Corner, k).p;
					gdouble dx = p[0] - si[0];
					gdouble dy = p[1] - si[1];
					gdouble dz = p[2] - si[2];

					radius2 = MAX (radius2, dx * dx + dy * dy + dz * dz);
				}
			}
			if ((gdouble) order[j].dist2 > 4.0 * radius2 + eps)
				break;

			for (k = 0; k < 3; k++)
			{
				n[k] = (gdouble) sj[k] - (gdouble) si[k];
				mid[k] = 0.5 * ((gdouble) sj[k] + (gdouble) si[k]);
			}
			len = sqrt (d3_dot (n, n));
			for (k = 0; k < 3; k++)
				n[k] /= len;
			if (clip_cell (faces, n, d3_dot (n, mid), eps, self->interior_material, &cap_area)
			    && cap_area > 100.0 * eps * eps)
			{
				guint32 nb = order[j].index;

				g_array_append_val (neighbors, nb);
			}
		}

		if (faces->len >= 4)
		{
			LrgFractureChunk *c;

			/* Neighbour list ascending (it was collected nearest first). */
			{
				guint a;
				guint b;

				for (a = 1; a < neighbors->len; a++)
				{
					guint32 key = g_array_index (neighbors, guint32, a);

					b = a;
					while (b > 0 && g_array_index (neighbors, guint32, b - 1) > key)
					{
						g_array_index (neighbors, guint32, b) = g_array_index (neighbors, guint32, b - 1);
						b--;
					}
					g_array_index (neighbors, guint32, b) = key;
				}
			}
			c = chunk_from_faces (faces, si, neighbors, eps);
			if (c->volume > 0.0f)
			{
				/* Neighbours hold seed indices for now; remapped below. */
				c->index = chunks->len;
				g_ptr_array_add (chunks, c);
				g_array_append_val (chunk_seed, i);
			}
			else
			{
				lrg_fracture_chunk_unref (c);
			}
		}
	}

	if (chunks->len == 0)
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_INVALID,
		                     "No seed point lies inside the source mesh");
		return FALSE;
	}

	/* Neighbour lists hold seed indices: map them to chunk indices. */
	{
		g_autofree gint *seed_to_chunk = g_new (gint, MAX (n_seeds, 1u));

		for (j = 0; j < n_seeds; j++)
			seed_to_chunk[j] = -1;
		for (i = 0; i < chunks->len; i++)
			seed_to_chunk[g_array_index (chunk_seed, guint, i)] = (gint) i;
		for (i = 0; i < chunks->len; i++)
		{
			LrgFractureChunk *c = g_ptr_array_index (chunks, i);
			guint             w = 0;

			for (j = 0; j < c->n_neighbors; j++)
			{
				gint m = seed_to_chunk[c->neighbors[j]];

				if (m >= 0)
					c->neighbors[w++] = (guint32) m;
			}
			c->n_neighbors = w;
		}
	}

	g_ptr_array_unref (self->chunks);
	self->chunks = g_steal_pointer (&chunks);
	return TRUE;
}

gboolean
lrg_fracture_is_built (LrgFracture *self)
{
	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	return self->chunks->len > 0;
}

guint
lrg_fracture_get_chunk_count (LrgFracture *self)
{
	g_return_val_if_fail (LRG_IS_FRACTURE (self), 0);
	return self->chunks->len;
}

LrgFractureChunk *
lrg_fracture_get_chunk (LrgFracture *self,
                        guint        index)
{
	g_return_val_if_fail (LRG_IS_FRACTURE (self), NULL);
	if (index >= self->chunks->len)
		return NULL;
	return g_ptr_array_index (self->chunks, index);
}

gfloat
lrg_fracture_get_total_volume (LrgFracture *self)
{
	gdouble v = 0.0;
	guint   i;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), 0.0f);
	for (i = 0; i < self->chunks->len; i++)
		v += ((LrgFractureChunk *) g_ptr_array_index (self->chunks, i))->volume;
	return (gfloat) v;
}

/* ==========================================================================
 * Cache file
 * ========================================================================== */

gchar *
lrg_fracture_get_source_hash (LrgFracture *self)
{
	g_autoptr(GChecksum) sum = NULL;
	guint32              params[5];
	gfloat               fparams[5];

	g_return_val_if_fail (LRG_IS_FRACTURE (self), NULL);
	if (self->positions == NULL)
		return NULL;

	sum = g_checksum_new (G_CHECKSUM_SHA256);
	g_checksum_update (sum, (const guchar *) "rfracture-1", -1);
	g_checksum_update (sum, (const guchar *) self->positions, 3 * sizeof (gfloat) * self->n_vertices);
	g_checksum_update (sum, (const guchar *) self->texcoords, 2 * sizeof (gfloat) * self->n_vertices);
	g_checksum_update (sum, (const guchar *) self->indices, sizeof (guint32) * self->n_indices);
	g_checksum_update (sum, (const guchar *) self->materials, sizeof (guint32) * (self->n_indices / 3));
	params[0] = self->chunk_count;
	params[1] = self->seed;
	params[2] = (guint32) self->seed_mode;
	params[3] = self->interior_material;
	params[4] = self->has_impact ? 1u : 0u;
	fparams[0] = self->impact_radius;
	fparams[1] = self->grid_jitter;
	fparams[2] = self->has_impact ? self->impact[0] : 0.0f;
	fparams[3] = self->has_impact ? self->impact[1] : 0.0f;
	fparams[4] = self->has_impact ? self->impact[2] : 0.0f;
	g_checksum_update (sum, (const guchar *) params, sizeof (params));
	g_checksum_update (sum, (const guchar *) fparams, sizeof (fparams));
	if (self->seed_mode == LRG_FRACTURE_SEED_CUSTOM && self->seeds->len > 0)
		g_checksum_update (sum, (const guchar *) self->seeds->data, sizeof (gfloat) * self->seeds->len);
	return g_strdup (g_checksum_get_string (sum));
}

/* Appends "[a, b, c]" with nine significant digits (exact float round trip). */
static void
append_floats (GString      *s,
               const gfloat *v,
               guint         n)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
	guint i;

	g_string_append_c (s, '[');
	for (i = 0; i < n; i++)
	{
		if (i > 0)
			g_string_append (s, ", ");
		g_string_append (s, g_ascii_formatd (buf, sizeof (buf), "%.9g", (gdouble) v[i]));
	}
	g_string_append_c (s, ']');
}

static void
append_uints (GString       *s,
              const guint32 *v,
              guint          n)
{
	guint i;

	g_string_append_c (s, '[');
	for (i = 0; i < n; i++)
		g_string_append_printf (s, i > 0 ? ", %u" : "%u", v[i]);
	g_string_append_c (s, ']');
}

static const gchar *
seed_mode_nick (LrgFractureSeedMode mode)
{
	GEnumClass *klass = g_type_class_ref (LRG_TYPE_FRACTURE_SEED_MODE);
	GEnumValue *value = g_enum_get_value (klass, (gint) mode);
	const gchar *nick = value != NULL ? value->value_nick : "random";

	g_type_class_unref (klass);
	return nick;
}

gchar *
lrg_fracture_save_to_data (LrgFracture *self,
                           gsize       *length)
{
	GString        *s;
	g_autofree gchar *hash = NULL;
	guint           i;
	gchar           buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_return_val_if_fail (LRG_IS_FRACTURE (self), NULL);

	s = g_string_new ("# libregnum pre-fractured mesh (Voronoi chunks)\n");
	g_string_append (s, "format: rfracture\n");
	g_string_append_printf (s, "version: %d\n", LRG_FRACTURE_FORMAT_VERSION);
	hash = lrg_fracture_get_source_hash (self);
	if (hash != NULL)
		g_string_append_printf (s, "source_hash: \"%s\"\n", hash);
	g_string_append_printf (s, "seed_mode: %s\n", seed_mode_nick (self->seed_mode));
	g_string_append_printf (s, "chunk_count: %u\n", self->chunk_count);
	g_string_append_printf (s, "seed: %u\n", self->seed);
	g_string_append_printf (s, "interior_material: %u\n", self->interior_material);
	g_string_append_printf (s, "impact_radius: %s\n",
	                        g_ascii_formatd (buf, sizeof (buf), "%.9g", (gdouble) self->impact_radius));
	g_string_append_printf (s, "grid_jitter: %s\n",
	                        g_ascii_formatd (buf, sizeof (buf), "%.9g", (gdouble) self->grid_jitter));
	g_string_append_printf (s, "impulse_threshold: %s\n",
	                        g_ascii_formatd (buf, sizeof (buf), "%.9g", (gdouble) self->impulse_threshold));
	g_string_append_printf (s, "density: %s\n",
	                        g_ascii_formatd (buf, sizeof (buf), "%.9g", (gdouble) self->density));
	if (self->has_impact)
	{
		g_string_append (s, "impact_point: ");
		append_floats (s, self->impact, 3);
		g_string_append_c (s, '\n');
	}
	if (self->positions != NULL)
	{
		gfloat b[6] = { self->bmin[0], self->bmin[1], self->bmin[2],
		                self->bmax[0], self->bmax[1], self->bmax[2] };

		g_string_append_printf (s, "source_volume: %s\n",
		                        g_ascii_formatd (buf, sizeof (buf), "%.9g", (gdouble) self->source_volume));
		g_string_append (s, "source_bounds: ");
		append_floats (s, b, 6);
		g_string_append_c (s, '\n');
	}
	g_string_append (s, "seeds: ");
	append_floats (s, (const gfloat *) self->seeds->data, self->seeds->len);
	g_string_append (s, "\nchunks:\n");
	for (i = 0; i < self->chunks->len; i++)
	{
		const LrgFractureChunk *c = g_ptr_array_index (self->chunks, i);
		gfloat                  b[6] = { c->bmin[0], c->bmin[1], c->bmin[2], c->bmax[0], c->bmax[1], c->bmax[2] };

		g_string_append (s, "  - seed: ");
		append_floats (s, c->seed, 3);
		g_string_append_printf (s, "\n    volume: %s\n    center_of_mass: ",
		                        g_ascii_formatd (buf, sizeof (buf), "%.9g", (gdouble) c->volume));
		append_floats (s, c->com, 3);
		g_string_append (s, "\n    inertia: ");
		append_floats (s, c->inertia, 9);
		g_string_append (s, "\n    bounds: ");
		append_floats (s, b, 6);
		g_string_append (s, "\n    neighbors: ");
		append_uints (s, c->neighbors, c->n_neighbors);
		g_string_append (s, "\n    positions: ");
		append_floats (s, c->positions, 3 * c->n_vertices);
		g_string_append (s, "\n    normals: ");
		append_floats (s, c->normals, 3 * c->n_vertices);
		g_string_append (s, "\n    texcoords: ");
		append_floats (s, c->texcoords, 2 * c->n_vertices);
		g_string_append (s, "\n    indices: ");
		append_uints (s, c->indices, c->n_indices);
		g_string_append (s, "\n    materials: ");
		append_uints (s, c->materials, c->n_indices / 3);
		g_string_append (s, "\n    hull: ");
		append_floats (s, c->hull, 3 * c->n_hull);
		g_string_append_c (s, '\n');
	}
	if (length != NULL)
		*length = s->len;
	return g_string_free (s, FALSE);
}

gboolean
lrg_fracture_save_to_file (LrgFracture  *self,
                           const gchar  *path,
                           GError      **error)
{
	g_autofree gchar *data = NULL;
	gsize             len = 0;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	data = lrg_fracture_save_to_data (self, &len);
	return g_file_set_contents (path, data, (gssize) len, error);
}

/* --- parsing ------------------------------------------------------------- */

static gboolean
parse_fail (GError     **error,
            const gchar *what)
{
	g_set_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_PARSE, "Malformed .rfracture: %s", what);
	return FALSE;
}

static const gchar *
scalar_of (YamlMapping *map,
           const gchar *key)
{
	YamlNode *n = yaml_mapping_get_member (map, key);

	if (n == NULL || yaml_node_get_node_type (n) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (n);
}

static gboolean
read_float (YamlMapping *map,
            const gchar *key,
            gfloat      *out)
{
	const gchar *s = scalar_of (map, key);
	gchar       *end = NULL;
	gdouble      d;

	if (s == NULL)
		return FALSE;
	d = g_ascii_strtod (s, &end);
	if (end == s || *end != '\0' || !isfinite (d))
		return FALSE;
	*out = (gfloat) d;
	return TRUE;
}

static gboolean
read_uint (YamlMapping *map,
           const gchar *key,
           guint       *out)
{
	const gchar *s = scalar_of (map, key);
	guint64      v;

	if (s == NULL || !g_ascii_string_to_unsigned (s, 10, 0, G_MAXUINT32, &v, NULL))
		return FALSE;
	*out = (guint) v;
	return TRUE;
}

/* Reads a flat float sequence; a missing key yields an empty array when @optional. */
static gfloat *
read_float_seq (YamlMapping *map,
                const gchar *key,
                guint       *n_out,
                gboolean     optional,
                gboolean    *ok)
{
	YamlNode     *node = yaml_mapping_get_member (map, key);
	YamlSequence *seq;
	gfloat       *v;
	guint         n;
	guint         i;

	*n_out = 0;
	if (node == NULL)
	{
		*ok = optional;
		return NULL;
	}
	if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
	{
		*ok = FALSE;
		return NULL;
	}
	seq = yaml_node_get_sequence (node);
	n = yaml_sequence_get_length (seq);
	v = g_new (gfloat, MAX (n, 1u));
	for (i = 0; i < n; i++)
	{
		YamlNode    *e = yaml_sequence_get_element (seq, i);
		const gchar *s;
		gchar       *end = NULL;
		gdouble      d;

		if (e == NULL || yaml_node_get_node_type (e) != YAML_NODE_SCALAR)
		{
			g_free (v);
			*ok = FALSE;
			return NULL;
		}
		s = yaml_node_get_scalar (e);
		d = g_ascii_strtod (s, &end);
		if (end == s || *end != '\0' || !isfinite (d))
		{
			g_free (v);
			*ok = FALSE;
			return NULL;
		}
		v[i] = (gfloat) d;
	}
	*n_out = n;
	*ok = TRUE;
	return v;
}

static guint32 *
read_uint_seq (YamlMapping *map,
               const gchar *key,
               guint       *n_out,
               gboolean    *ok)
{
	YamlNode     *node = yaml_mapping_get_member (map, key);
	YamlSequence *seq;
	guint32      *v;
	guint         n;
	guint         i;

	*n_out = 0;
	*ok = FALSE;
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return NULL;
	seq = yaml_node_get_sequence (node);
	n = yaml_sequence_get_length (seq);
	v = g_new (guint32, MAX (n, 1u));
	for (i = 0; i < n; i++)
	{
		YamlNode *e = yaml_sequence_get_element (seq, i);
		guint64   x;

		if (e == NULL || yaml_node_get_node_type (e) != YAML_NODE_SCALAR
		    || !g_ascii_string_to_unsigned (yaml_node_get_scalar (e), 10, 0, G_MAXUINT32, &x, NULL))
		{
			g_free (v);
			return NULL;
		}
		v[i] = (guint32) x;
	}
	*n_out = n;
	*ok = TRUE;
	return v;
}

static gboolean
read_fixed (YamlMapping *map,
            const gchar *key,
            gfloat      *out,
            guint        n)
{
	gboolean          ok;
	guint             got;
	g_autofree gfloat *v = read_float_seq (map, key, &got, FALSE, &ok);

	if (!ok || got != n)
		return FALSE;
	memcpy (out, v, n * sizeof (gfloat));
	return TRUE;
}

static LrgFractureChunk *
parse_chunk (YamlNode *node,
             guint     index,
             guint     n_chunks)
{
	g_autoptr(LrgFractureChunk) c = chunk_new ();
	YamlMapping *map;
	gboolean     ok;
	guint        n;
	guint        n2;
	guint        i;
	gfloat       b[6];

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return NULL;
	map = yaml_node_get_mapping (node);
	c->index = index;
	if (!read_fixed (map, "seed", c->seed, 3) || !read_float (map, "volume", &c->volume)
	    || !read_fixed (map, "center_of_mass", c->com, 3) || !read_fixed (map, "inertia", c->inertia, 9)
	    || !read_fixed (map, "bounds", b, 6))
		return NULL;
	memcpy (c->bmin, b, 3 * sizeof (gfloat));
	memcpy (c->bmax, b + 3, 3 * sizeof (gfloat));

	c->positions = read_float_seq (map, "positions", &n, FALSE, &ok);
	if (!ok || n == 0 || n % 3 != 0)
		return NULL;
	c->n_vertices = n / 3;
	c->normals = read_float_seq (map, "normals", &n2, FALSE, &ok);
	if (!ok || n2 != n)
		return NULL;
	c->texcoords = read_float_seq (map, "texcoords", &n2, FALSE, &ok);
	if (!ok || n2 != 2 * c->n_vertices)
		return NULL;
	c->indices = read_uint_seq (map, "indices", &c->n_indices, &ok);
	if (!ok || c->n_indices == 0 || c->n_indices % 3 != 0)
		return NULL;
	for (i = 0; i < c->n_indices; i++)
		if (c->indices[i] >= c->n_vertices)
			return NULL;
	c->materials = read_uint_seq (map, "materials", &n2, &ok);
	if (!ok || n2 != c->n_indices / 3)
		return NULL;
	c->neighbors = read_uint_seq (map, "neighbors", &c->n_neighbors, &ok);
	if (!ok)
		return NULL;
	for (i = 0; i < c->n_neighbors; i++)
		if (c->neighbors[i] >= n_chunks)
			return NULL;
	c->hull = read_float_seq (map, "hull", &n2, FALSE, &ok);
	if (!ok || n2 == 0 || n2 % 3 != 0)
		return NULL;
	c->n_hull = n2 / 3;
	return g_steal_pointer (&c);
}

gboolean
lrg_fracture_load_from_data (LrgFracture  *self,
                             const gchar  *data,
                             gssize        length,
                             GError      **error)
{
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	g_autoptr(GPtrArray)  chunks = NULL;
	g_autofree gfloat    *seeds = NULL;
	YamlNode             *root;
	YamlMapping          *map;
	YamlNode             *list;
	YamlSequence         *seq;
	const gchar          *fmt;
	const gchar          *mode_nick;
	guint                 version = 0;
	guint                 n_seeds;
	guint                 chunk_count = self->chunk_count;
	guint                 seed = self->seed;
	guint                 interior = self->interior_material;
	gfloat                impact_radius = self->impact_radius;
	gfloat                grid_jitter = self->grid_jitter;
	gfloat                threshold = self->impulse_threshold;
	gfloat                density = self->density;
	gfloat                impact[3];
	gboolean              has_impact;
	gboolean              ok;
	GEnumClass           *klass;
	GEnumValue           *mode_value;
	LrgFractureSeedMode   mode;
	guint                 i;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	if (!yaml_parser_load_from_data (parser, data, length, error))
		return FALSE;
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		return parse_fail (error, "the document is not a mapping");
	map = yaml_node_get_mapping (root);
	fmt = scalar_of (map, "format");
	if (fmt == NULL || g_strcmp0 (fmt, "rfracture") != 0)
		return parse_fail (error, "format is not rfracture");
	if (!read_uint (map, "version", &version) || version != LRG_FRACTURE_FORMAT_VERSION)
	{
		g_set_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_PARSE,
		             "Unsupported .rfracture version (want %d)", LRG_FRACTURE_FORMAT_VERSION);
		return FALSE;
	}

	/* Optional parameters keep the current values when missing. */
	mode = self->seed_mode;
	mode_nick = scalar_of (map, "seed_mode");
	if (mode_nick != NULL)
	{
		klass = g_type_class_ref (LRG_TYPE_FRACTURE_SEED_MODE);
		mode_value = g_enum_get_value_by_nick (klass, mode_nick);
		if (mode_value != NULL)
			mode = (LrgFractureSeedMode) mode_value->value;
		g_type_class_unref (klass);
		if (mode_value == NULL)
			return parse_fail (error, "unknown seed_mode");
	}
	if (yaml_mapping_has_member (map, "chunk_count") && !read_uint (map, "chunk_count", &chunk_count))
		return parse_fail (error, "chunk_count");
	if (yaml_mapping_has_member (map, "seed") && !read_uint (map, "seed", &seed))
		return parse_fail (error, "seed");
	if (yaml_mapping_has_member (map, "interior_material") && !read_uint (map, "interior_material", &interior))
		return parse_fail (error, "interior_material");
	if (yaml_mapping_has_member (map, "impact_radius") && !read_float (map, "impact_radius", &impact_radius))
		return parse_fail (error, "impact_radius");
	if (yaml_mapping_has_member (map, "grid_jitter") && !read_float (map, "grid_jitter", &grid_jitter))
		return parse_fail (error, "grid_jitter");
	if (yaml_mapping_has_member (map, "impulse_threshold") && !read_float (map, "impulse_threshold", &threshold))
		return parse_fail (error, "impulse_threshold");
	if (yaml_mapping_has_member (map, "density") && !read_float (map, "density", &density))
		return parse_fail (error, "density");
	has_impact = yaml_mapping_has_member (map, "impact_point");
	if (has_impact && !read_fixed (map, "impact_point", impact, 3))
		return parse_fail (error, "impact_point");
	seeds = read_float_seq (map, "seeds", &n_seeds, TRUE, &ok);
	if (!ok || n_seeds % 3 != 0 || n_seeds > 3 * LRG_FRACTURE_MAX_CHUNKS)
		return parse_fail (error, "seeds");
	if (chunk_count < 1 || chunk_count > LRG_FRACTURE_MAX_CHUNKS || !(density > 0.0f))
		return parse_fail (error, "parameter out of range");

	list = yaml_mapping_get_member (map, "chunks");
	if (list == NULL || yaml_node_get_node_type (list) != YAML_NODE_SEQUENCE)
		return parse_fail (error, "chunks is not a list");
	seq = yaml_node_get_sequence (list);
	chunks = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_fracture_chunk_unref);
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		LrgFractureChunk *c = parse_chunk (yaml_sequence_get_element (seq, i), i,
		                                   yaml_sequence_get_length (seq));

		if (c == NULL)
		{
			g_set_error (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_PARSE,
			             "Malformed .rfracture: chunk %u", i);
			return FALSE;
		}
		g_ptr_array_add (chunks, c);
	}

	/* Commit. */
	g_object_freeze_notify (G_OBJECT (self));
	g_object_set (self, "seed-mode", mode, "chunk-count", chunk_count, "seed", seed,
	              "interior-material", interior, "impact-radius", impact_radius,
	              "grid-jitter", CLAMP (grid_jitter, 0.0f, 1.0f), "impulse-threshold", MAX (threshold, 0.0f),
	              "density", density, NULL);
	g_object_thaw_notify (G_OBJECT (self));
	lrg_fracture_set_impact_point (self, has_impact ? impact : NULL);
	g_array_set_size (self->seeds, 0);
	if (n_seeds > 0)
		g_array_append_vals (self->seeds, seeds, n_seeds);
	g_ptr_array_unref (self->chunks);
	self->chunks = g_steal_pointer (&chunks);
	return TRUE;
}

gboolean
lrg_fracture_load_from_file (LrgFracture  *self,
                             const gchar  *path,
                             GError      **error)
{
	g_autofree gchar *data = NULL;
	gsize             len = 0;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	if (!g_file_get_contents (path, &data, &len, error))
		return FALSE;
	return lrg_fracture_load_from_data (self, data, (gssize) len, error);
}

LrgFracture *
lrg_fracture_new_from_file (const gchar  *path,
                            GError      **error)
{
	g_autoptr(LrgFracture) self = lrg_fracture_new ();

	g_return_val_if_fail (path != NULL, NULL);
	if (!lrg_fracture_load_from_file (self, path, error))
		return NULL;
	return g_steal_pointer (&self);
}

/* The `source_hash` value of a document, or NULL. */
static gchar *
peek_hash (const gchar *data,
           gsize        len)
{
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	YamlNode             *root;
	const gchar          *h;

	if (!yaml_parser_load_from_data (parser, data, (gssize) len, NULL))
		return NULL;
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		return NULL;
	h = scalar_of (yaml_node_get_mapping (root), "source_hash");
	return g_strdup (h);
}

gboolean
lrg_fracture_load_or_build (LrgFracture  *self,
                            const gchar  *path,
                            GError      **error)
{
	g_autofree gchar *want = NULL;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	want = lrg_fracture_get_source_hash (self);
	if (want == NULL)
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_SOURCE,
		                     "The fracture has no source mesh");
		return FALSE;
	}
	if (path != NULL && g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		g_autofree gchar *data = NULL;
		g_autofree gchar *have = NULL;
		gsize             len = 0;

		if (g_file_get_contents (path, &data, &len, NULL))
		{
			have = peek_hash (data, len);
			if (have != NULL && g_strcmp0 (have, want) == 0
			    && lrg_fracture_load_from_data (self, data, (gssize) len, NULL))
				return TRUE;
		}
	}
	if (!lrg_fracture_build (self, error))
		return FALSE;
	if (path != NULL)
		return lrg_fracture_save_to_file (self, path, error);
	return TRUE;
}

/* ==========================================================================
 * Swap-in helpers
 * ========================================================================== */

gboolean
lrg_fracture_should_break (LrgFracture *self,
                           gfloat       impulse)
{
	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	return isfinite (impulse) && fabsf (impulse) >= self->impulse_threshold;
}

static void
transform_point (const gfloat *m,
                 const gfloat *p,
                 gfloat       *out)
{
	gfloat x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
	gfloat y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
	gfloat z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

gboolean
lrg_fracture_get_chunk_transform (LrgFracture  *self,
                                  guint         index,
                                  const gfloat *object_matrix,
                                  gfloat       *out_matrix)
{
	const LrgFractureChunk *c;
	static const gfloat     identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const gfloat           *m = object_matrix != NULL ? object_matrix : identity;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	g_return_val_if_fail (out_matrix != NULL, FALSE);
	c = lrg_fracture_get_chunk (self, index);
	if (c == NULL)
		return FALSE;
	memcpy (out_matrix, m, 16 * sizeof (gfloat));
	transform_point (m, c->com, out_matrix + 12);
	return TRUE;
}

gboolean
lrg_fracture_get_chunk_velocity (LrgFracture  *self,
                                 guint         index,
                                 const gfloat *object_matrix,
                                 const gfloat *impact_point,
                                 const gfloat *impulse,
                                 gfloat       *out_linear)
{
	static const gfloat     identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const gfloat           *m = object_matrix != NULL ? object_matrix : identity;
	const LrgFractureChunk *c;
	gfloat                  centre[3];
	gfloat                  total = 0.0f;
	gfloat                  radius = 0.0f;
	gfloat                  base[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                  speed;
	gfloat                  weight = 1.0f;
	guint                   i;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), FALSE);
	g_return_val_if_fail (out_linear != NULL, FALSE);
	c = lrg_fracture_get_chunk (self, index);
	if (c == NULL)
		return FALSE;
	memset (out_linear, 0, 3 * sizeof (gfloat));
	if (impulse == NULL)
		return TRUE;

	for (i = 0; i < self->chunks->len; i++)
	{
		const LrgFractureChunk *o = g_ptr_array_index (self->chunks, i);
		gfloat                  d[3];

		total += o->volume * self->density;
		v3_sub (o->bmax, o->bmin, d);
		radius = MAX (radius, 0.5f * v3_len (d));
	}
	{
		gfloat lo[3] = { G_MAXFLOAT, G_MAXFLOAT, G_MAXFLOAT };
		gfloat hi[3] = { -G_MAXFLOAT, -G_MAXFLOAT, -G_MAXFLOAT };
		gfloat d[3];
		guint  k;

		for (i = 0; i < self->chunks->len; i++)
		{
			const LrgFractureChunk *o = g_ptr_array_index (self->chunks, i);

			for (k = 0; k < 3; k++)
			{
				lo[k] = MIN (lo[k], o->bmin[k]);
				hi[k] = MAX (hi[k], o->bmax[k]);
			}
		}
		v3_sub (hi, lo, d);
		radius = MAX (radius, 0.5f * v3_len (d));
	}
	if (!(total > 0.0f))
		return TRUE;

	for (i = 0; i < 3; i++)
		base[i] = impulse[i] / total;
	transform_point (m, c->com, centre);
	if (impact_point != NULL)
	{
		gfloat d[3];
		gfloat dist;
		gfloat len;

		v3_sub (centre, impact_point, d);
		dist = v3_len (d);
		weight = 2.0f * radius / (radius + dist);
		speed = v3_len (base) * weight;
		len = dist > 1.0e-6f ? dist : 1.0f;
		for (i = 0; i < 3; i++)
			out_linear[i] = base[i] * weight + 0.1f * speed * d[i] / len;
	}
	else
	{
		memcpy (out_linear, base, 3 * sizeof (gfloat));
	}
	return TRUE;
}

GArray *
lrg_fracture_swap_in (LrgFracture       *self,
                      LrgDynamicsWorld  *world,
                      const gfloat      *object_matrix,
                      const gfloat      *impact_point,
                      const gfloat      *impulse,
                      guint              layer,
                      GError           **error)
{
	static const gfloat identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const gfloat       *m = object_matrix != NULL ? object_matrix : identity;
	g_autoptr(GArray)   ids = NULL;
	gfloat              quat[4];
	guint               i;

	g_return_val_if_fail (LRG_IS_FRACTURE (self), NULL);
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (world), NULL);
	if (self->chunks->len == 0)
	{
		g_set_error_literal (error, LRG_FRACTURE_ERROR, LRG_FRACTURE_ERROR_NO_SOURCE,
		                     "The fracture has no chunks (build or load it first)");
		return NULL;
	}

	lrg_math3d_matrix_to_quat (m, quat);
	ids = g_array_sized_new (FALSE, FALSE, sizeof (guint), self->chunks->len);
	for (i = 0; i < self->chunks->len; i++)
	{
		const LrgFractureChunk     *c = g_ptr_array_index (self->chunks, i);
		g_autoptr(LrgPhysicsShape)  shape = NULL;
		gfloat                      xf[16];
		gfloat                      vel[3];
		guint                       id = 0;
		gboolean                    ok;

		/* The hull is centred on the centre of mass, so the body origin sits there. */
		shape = lrg_fracture_chunk_create_shape (c, error);
		ok = shape != NULL;
		if (ok)
		{
			lrg_fracture_get_chunk_transform (self, i, m, xf);
			ok = lrg_dynamics_world_add_body (world, shape, LRG_DYNAMICS_BODY_DYNAMIC,
			                                  MAX (self->density * c->volume, 1.0e-6f),
			                                  xf + 12, quat, &id, error);
		}
		if (ok)
		{
			g_array_append_val (ids, id);
			lrg_fracture_get_chunk_velocity (self, i, m, impact_point, impulse, vel);
			ok = lrg_dynamics_world_set_linear_velocity (world, id, vel, error)
			     && lrg_dynamics_world_set_body_layer (world, id, layer, G_MAXUINT32, error);
		}
		if (!ok)
		{
			guint k;

			for (k = 0; k < ids->len; k++)
				lrg_dynamics_world_remove_body (world, g_array_index (ids, guint, k), NULL);
			return NULL;
		}
	}
	return g_steal_pointer (&ids);
}
