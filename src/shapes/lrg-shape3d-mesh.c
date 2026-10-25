/* lrg-shape3d-mesh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared GPU meshes for the immediate-mode 3D shapes (see
 * lrg-shape3d-mesh-private.h). Meshes are kept in a process-wide table
 * keyed by kind and parameters and reference counted by the shapes that
 * draw them; drawing is single threaded, like rlgl itself.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "shapes/lrg-shape3d-mesh-private.h"

/* raylib's DEG2RAD (raymath.h / raylib.h), spelled the same way so the
 * constant folds to the same float */
#define SHAPE_PI      (3.14159265358979323846f)
#define SHAPE_DEG2RAD (SHAPE_PI / 180.0f)

/* DrawMesh() walks raylib's MAX_MATERIAL_MAPS (12) maps; keep headroom */
#define SHAPE_MATERIAL_MAPS (16)

typedef enum
{
	SHAPE_MESH_SPHERE,
	SHAPE_MESH_CYLINDER
} ShapeMeshKind;

/* the parameters a mesh is built from: floats compared by bits */
typedef struct
{
	gint    kind;
	gint    a;          /* sphere: rings; cylinder: sides */
	gint    b;          /* sphere: slices */
	guint32 f[3];       /* cylinder: top, bottom, height bits */
} ShapeMeshKey;

struct _LrgShape3DMesh
{
	ShapeMeshKey key;
	guint        refs;
	Mesh         mesh;
	gpointer     window;   /* the context the mesh was uploaded in */
};

static GHashTable *mesh_table = NULL;   /* ShapeMeshKey* -> LrgShape3DMesh* */

static guint
key_hash (gconstpointer data)
{
	const ShapeMeshKey *key = data;
	guint               h;

	h = (guint)key->kind * 2654435761u;
	h = (h ^ (guint)key->a) * 16777619u;
	h = (h ^ (guint)key->b) * 16777619u;
	h = (h ^ key->f[0]) * 16777619u;
	h = (h ^ key->f[1]) * 16777619u;
	h = (h ^ key->f[2]) * 16777619u;
	return h;
}

static gboolean
key_equal (gconstpointer a,
           gconstpointer b)
{
	const ShapeMeshKey *ka = a;
	const ShapeMeshKey *kb = b;

	return ka->kind == kb->kind && ka->a == kb->a && ka->b == kb->b
	       && ka->f[0] == kb->f[0] && ka->f[1] == kb->f[1] && ka->f[2] == kb->f[2];
}

static guint32
float_bits (gfloat value)
{
	guint32 bits;

	memcpy (&bits, &value, sizeof bits);
	return bits;
}

/* ==========================================================================
 * Geometry: raylib's immediate-mode vertex streams, captured
 * ========================================================================== */

/* put_vertex: appends (x, y, z) to @out at *@n */
static void
put_vertex (gfloat *out,
            guint  *n,
            gfloat  x,
            gfloat  y,
            gfloat  z)
{
	out[*n * 3 + 0] = x;
	out[*n * 3 + 1] = y;
	out[*n * 3 + 2] = z;
	(*n)++;
}

/*
 * build_sphere:
 * DrawSphereEx()'s triangles of the unit sphere: a ring/slice recurrence
 * of rotations (the same arithmetic, in the same order, as raylib's).
 */
static gfloat *
build_sphere (gint   rings,
              gint   slices,
              guint *out_count)
{
	gfloat *v;
	guint   n = 0;
	gint    i;
	gint    j;
	gfloat  ringangle;
	gfloat  sliceangle;
	gfloat  cosring;
	gfloat  sinring;
	gfloat  cosslice;
	gfloat  sinslice;
	gfloat  vx[4];
	gfloat  vy[4];
	gfloat  vz[4];

	*out_count = (guint)(rings + 1) * (guint)slices * 6u;
	v = g_new (gfloat, (gsize)*out_count * 3);

	ringangle = SHAPE_DEG2RAD * (180.0f / (rings + 1));
	sliceangle = SHAPE_DEG2RAD * (360.0f / slices);
	cosring = cosf (ringangle);
	sinring = sinf (ringangle);
	cosslice = cosf (sliceangle);
	sinslice = sinf (sliceangle);

	memset (vx, 0, sizeof vx);
	memset (vy, 0, sizeof vy);
	memset (vz, 0, sizeof vz);
	vx[2] = 0.0f;    vy[2] = 1.0f;    vz[2] = 0.0f;
	vx[3] = sinring; vy[3] = cosring; vz[3] = 0.0f;

	for (i = 0; i < rings + 1; i++)
	{
		for (j = 0; j < slices; j++)
		{
			gfloat x2;
			gfloat z2;
			gfloat x3;
			gfloat z3;

			/* rotate around y to set up the next face */
			vx[0] = vx[2]; vy[0] = vy[2]; vz[0] = vz[2];
			vx[1] = vx[3]; vy[1] = vy[3]; vz[1] = vz[3];
			x2 = cosslice * vx[2] - sinslice * vz[2];
			z2 = sinslice * vx[2] + cosslice * vz[2];
			x3 = cosslice * vx[3] - sinslice * vz[3];
			z3 = sinslice * vx[3] + cosslice * vz[3];
			vx[2] = x2; vz[2] = z2;
			vx[3] = x3; vz[3] = z3;

			put_vertex (v, &n, vx[0], vy[0], vz[0]);
			put_vertex (v, &n, vx[3], vy[3], vz[3]);
			put_vertex (v, &n, vx[1], vy[1], vz[1]);

			put_vertex (v, &n, vx[0], vy[0], vz[0]);
			put_vertex (v, &n, vx[2], vy[2], vz[2]);
			put_vertex (v, &n, vx[3], vy[3], vz[3]);
		}

		/* rotate around z to set up the next ring's first vertices */
		{
			gfloat x3 = cosring * vx[3] + sinring * vy[3];
			gfloat y3 = -sinring * vx[3] + cosring * vy[3];

			vx[2] = vx[3]; vy[2] = vy[3]; vz[2] = vz[3];
			vx[3] = x3;
			vy[3] = y3;
		}
	}
	return v;
}

/*
 * build_cylinder:
 * DrawCylinder()'s triangles: the body and top cap (or the cone's sides
 * when the top radius is not positive), then the base.
 */
static gfloat *
build_cylinder (gfloat  radius_top,
                gfloat  radius_bottom,
                gfloat  height,
                gint    sides,
                guint  *out_count)
{
	gfloat *v;
	guint   n = 0;
	gint    i;
	gfloat  step;

	if (sides < 3)
		sides = 3;
	step = 360.0f / sides;
	*out_count = (guint)sides * (radius_top > 0 ? 12u : 6u);
	v = g_new (gfloat, (gsize)*out_count * 3);

	if (radius_top > 0)
	{
		/* body */
		for (i = 0; i < sides; i++)
		{
			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * i * step) * radius_bottom, 0,
			            cosf (SHAPE_DEG2RAD * i * step) * radius_bottom);
			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * (i + 1) * step) * radius_bottom, 0,
			            cosf (SHAPE_DEG2RAD * (i + 1) * step) * radius_bottom);
			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * (i + 1) * step) * radius_top, height,
			            cosf (SHAPE_DEG2RAD * (i + 1) * step) * radius_top);

			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * i * step) * radius_top, height,
			            cosf (SHAPE_DEG2RAD * i * step) * radius_top);
			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * i * step) * radius_bottom, 0,
			            cosf (SHAPE_DEG2RAD * i * step) * radius_bottom);
			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * (i + 1) * step) * radius_top, height,
			            cosf (SHAPE_DEG2RAD * (i + 1) * step) * radius_top);
		}

		/* cap */
		for (i = 0; i < sides; i++)
		{
			put_vertex (v, &n, 0, height, 0);
			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * i * step) * radius_top, height,
			            cosf (SHAPE_DEG2RAD * i * step) * radius_top);
			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * (i + 1) * step) * radius_top, height,
			            cosf (SHAPE_DEG2RAD * (i + 1) * step) * radius_top);
		}
	}
	else
	{
		/* cone */
		for (i = 0; i < sides; i++)
		{
			put_vertex (v, &n, 0, height, 0);
			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * i * step) * radius_bottom, 0,
			            cosf (SHAPE_DEG2RAD * i * step) * radius_bottom);
			put_vertex (v, &n, sinf (SHAPE_DEG2RAD * (i + 1) * step) * radius_bottom, 0,
			            cosf (SHAPE_DEG2RAD * (i + 1) * step) * radius_bottom);
		}
	}

	/* base */
	for (i = 0; i < sides; i++)
	{
		put_vertex (v, &n, 0, 0, 0);
		put_vertex (v, &n, sinf (SHAPE_DEG2RAD * (i + 1) * step) * radius_bottom, 0,
		            cosf (SHAPE_DEG2RAD * (i + 1) * step) * radius_bottom);
		put_vertex (v, &n, sinf (SHAPE_DEG2RAD * i * step) * radius_bottom, 0,
		            cosf (SHAPE_DEG2RAD * i * step) * radius_bottom);
	}
	return v;
}

/* ==========================================================================
 * The shared table
 * ========================================================================== */

/* unload: frees the GPU objects (only while the context lives) and the
 * CPU positions */
static void
unload (LrgShape3DMesh *entry)
{
	if (IsWindowReady () && entry->window == GetWindowHandle ())
	{
		UnloadMesh (entry->mesh);
	}
	else
	{
		/* the context that owned the buffers is gone with them */
		RL_FREE (entry->mesh.vboId);
		RL_FREE (entry->mesh.vertices);
	}
	memset (&entry->mesh, 0, sizeof entry->mesh);
}

/* upload: the positions (raylib's allocator, UnloadMesh() frees them)
 * into a fresh mesh in the current context */
static void
upload (LrgShape3DMesh *entry,
        gfloat         *positions,
        guint           count)
{
	memset (&entry->mesh, 0, sizeof entry->mesh);
	entry->mesh.vertexCount = (gint)count;
	entry->mesh.triangleCount = (gint)(count / 3);
	entry->mesh.vertices = RL_MALLOC ((gsize)count * 3 * sizeof (gfloat));
	memcpy (entry->mesh.vertices, positions, (gsize)count * 3 * sizeof (gfloat));
	UploadMesh (&entry->mesh, false);
	entry->window = GetWindowHandle ();
}

/* build_and_upload: (re)makes @entry's mesh from its key in the
 * current context; FALSE when there is nothing to draw or it failed */
static gboolean
build_and_upload (LrgShape3DMesh *entry)
{
	g_autofree gfloat *positions = NULL;
	guint              count = 0;

	if (entry->key.kind == SHAPE_MESH_SPHERE)
	{
		positions = build_sphere (entry->key.a, entry->key.b, &count);
	}
	else
	{
		gfloat top;
		gfloat bottom;
		gfloat height;

		memcpy (&top, &entry->key.f[0], sizeof top);
		memcpy (&bottom, &entry->key.f[1], sizeof bottom);
		memcpy (&height, &entry->key.f[2], sizeof height);
		positions = build_cylinder (top, bottom, height, entry->key.a, &count);
	}
	if (count == 0)
		return FALSE;
	upload (entry, positions, count);
	return entry->mesh.vaoId != 0;
}

static LrgShape3DMesh *
acquire (const ShapeMeshKey *key)
{
	LrgShape3DMesh *entry;

	if (!IsWindowReady ())
		return NULL;
	if (mesh_table == NULL)
		mesh_table = g_hash_table_new (key_hash, key_equal);

	entry = g_hash_table_lookup (mesh_table, key);
	if (entry != NULL)
	{
		entry->refs++;
		return entry;
	}

	entry = g_new0 (LrgShape3DMesh, 1);
	entry->key = *key;
	entry->refs = 1;
	if (!build_and_upload (entry))
	{
		unload (entry);
		g_free (entry);
		return NULL;
	}
	g_hash_table_insert (mesh_table, &entry->key, entry);
	return entry;
}

LrgShape3DMesh *
lrg_shape3d_mesh_acquire_sphere (gint rings,
                                 gint slices)
{
	ShapeMeshKey key;

	/* raylib draws nothing (or divides by zero) below these */
	if (rings < 0 || slices <= 0)
		return NULL;
	memset (&key, 0, sizeof key);
	key.kind = SHAPE_MESH_SPHERE;
	key.a = rings;
	key.b = slices;
	return acquire (&key);
}

LrgShape3DMesh *
lrg_shape3d_mesh_acquire_cylinder (gfloat radius_top,
                                   gfloat radius_bottom,
                                   gfloat height,
                                   gint   sides)
{
	ShapeMeshKey key;

	if (!isfinite (radius_top) || !isfinite (radius_bottom) || !isfinite (height))
		return NULL;
	memset (&key, 0, sizeof key);
	key.kind = SHAPE_MESH_CYLINDER;
	key.a = sides < 3 ? 3 : sides;
	key.f[0] = float_bits (radius_top);
	key.f[1] = float_bits (radius_bottom);
	key.f[2] = float_bits (height);
	return acquire (&key);
}

void
lrg_shape3d_mesh_release (LrgShape3DMesh *mesh)
{
	if (mesh == NULL)
		return;
	g_return_if_fail (mesh->refs > 0);

	if (--mesh->refs > 0)
		return;
	g_hash_table_remove (mesh_table, &mesh->key);
	unload (mesh);
	g_free (mesh);
}

gboolean
lrg_shape3d_mesh_draw (LrgShape3DMesh *mesh,
                       const GrlColor *color,
                       gfloat          scale)
{
	MaterialMap maps[SHAPE_MATERIAL_MAPS];
	Material    material;
	Matrix      model;

	g_return_val_if_fail (mesh != NULL, FALSE);
	g_return_val_if_fail (color != NULL, FALSE);

	if (!IsWindowReady ())
		return FALSE;
	if (mesh->window != GetWindowHandle () || mesh->mesh.vaoId == 0)
	{
		/* a new context: the old buffers died with the old one */
		unload (mesh);
		if (!build_and_upload (mesh))
			return FALSE;
	}

	/* what rlgl queued before this shape is drawn before it */
	rlDrawRenderBatchActive ();

	memset (maps, 0, sizeof maps);
	memset (&material, 0, sizeof material);
	material.shader.id = rlGetShaderIdDefault ();
	material.shader.locs = rlGetShaderLocsDefault ();
	maps[MATERIAL_MAP_ALBEDO].texture.id = rlGetTextureIdDefault ();
	maps[MATERIAL_MAP_ALBEDO].texture.width = 1;
	maps[MATERIAL_MAP_ALBEDO].texture.height = 1;
	maps[MATERIAL_MAP_ALBEDO].texture.mipmaps = 1;
	maps[MATERIAL_MAP_ALBEDO].texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
	maps[MATERIAL_MAP_ALBEDO].color = (Color){ color->r, color->g, color->b, color->a };
	material.maps = maps;

	memset (&model, 0, sizeof model);
	model.m0 = scale;
	model.m5 = scale;
	model.m10 = scale;
	model.m15 = 1.0f;
	DrawMesh (mesh->mesh, material, model);
	return TRUE;
}
