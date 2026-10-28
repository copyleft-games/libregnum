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
 *
 * Inside a shape batch (lrg_shape3d_begin_batch()) opaque solid draws are
 * not drawn one DrawMesh() at a time: each appends its model-view-
 * projection matrix (computed exactly as DrawMesh() would) and colour to
 * its mesh's instance list, and a flush draws every list with one
 * instanced call through a small program that does what the default
 * shader does for these meshes (position times the matrix, the colour as
 * is).
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
#include "graphics/lrg-draw-cull-private.h"

/* raylib's DEG2RAD (raymath.h / raylib.h), spelled the same way so the
 * constant folds to the same float */
#define SHAPE_PI      (3.14159265358979323846f)
#define SHAPE_DEG2RAD (SHAPE_PI / 180.0f)

/* DrawMesh() walks raylib's MAX_MATERIAL_MAPS (12) maps; keep headroom */
#define SHAPE_MATERIAL_MAPS (16)

/* the instanced program's per-instance attributes: raylib binds these
 * names to these locations in every program it links */
#define SHAPE_LOC_COLOR    (RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR)
#define SHAPE_LOC_MVP      (RL_DEFAULT_SHADER_ATTRIB_LOCATION_INSTANCETRANSFORM)

/* the fewest instances an instance buffer is made for */
#define SHAPE_MIN_INSTANCES (64u)

typedef enum
{
	SHAPE_MESH_SPHERE,
	SHAPE_MESH_CYLINDER,
	SHAPE_MESH_CUBE
} ShapeMeshKind;

/* one queued draw: the column-major MVP DrawMesh() would have sent, and
 * the colour the default shader would have multiplied in (colDiffuse) */
typedef struct
{
	gfloat mvp[16];
	gfloat color[4];
} ShapeInstance;

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

	/* the batch: the queued instances and the vertex array that draws
	 * them (the mesh's positions and indices plus the instance buffer) */
	GArray      *instances;         /* ShapeInstance */
	guint        instance_vao;
	guint        instance_vbo;
	guint        instance_capacity; /* instances the buffer holds */
};

static GHashTable *mesh_table = NULL;   /* ShapeMeshKey* -> LrgShape3DMesh* */

/* the batch: how deep the begin/end nesting is, the meshes with queued
 * instances in the order they were first queued (each holds a reference
 * while queued), and the instanced program of the context it was made in */
static guint      batch_depth = 0;
static GPtrArray *batch_pending = NULL;
static guint      batch_program = 0;
static gpointer   batch_program_window = NULL;
static gboolean   batch_program_failed = FALSE;

/* the instanced program: what raylib's default shader computes for an
 * untextured mesh without vertex colours (the 1x1 white texel, times
 * colDiffuse, times the default vertex colour of 1), with the MVP and
 * the colour per instance; the colour is flat so it is not interpolated */
static const gchar batch_vs[] =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec4 vertexColor;\n"
	"in mat4 instanceTransform;\n"
	"flat out vec4 fragColor;\n"
	"void main ()\n"
	"{\n"
	"    fragColor = vertexColor;\n"
	"    gl_Position = instanceTransform*vec4(vertexPosition, 1.0);\n"
	"}\n";

static const gchar batch_fs[] =
	"#version 330\n"
	"flat in vec4 fragColor;\n"
	"out vec4 finalColor;\n"
	"void main ()\n"
	"{\n"
	"    finalColor = fragColor;\n"
	"}\n";

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

/*
 * build_cube:
 * DrawCube()'s triangles of the unit cube at the origin (width, height
 * and length 1, scaled by the shape's size when drawing), in its order.
 */
static gfloat *
build_cube (guint *out_count)
{
	/* the corners DrawCube() names, as x, y, z signs of the half size */
	static const gint8 corners[36][3] = {
		/* front */
		{ -1, -1,  1 }, {  1, -1,  1 }, { -1,  1,  1 },
		{  1,  1,  1 }, { -1,  1,  1 }, {  1, -1,  1 },
		/* back */
		{ -1, -1, -1 }, { -1,  1, -1 }, {  1, -1, -1 },
		{  1,  1, -1 }, {  1, -1, -1 }, { -1,  1, -1 },
		/* top */
		{ -1,  1, -1 }, { -1,  1,  1 }, {  1,  1,  1 },
		{  1,  1, -1 }, { -1,  1, -1 }, {  1,  1,  1 },
		/* bottom */
		{ -1, -1, -1 }, {  1, -1,  1 }, { -1, -1,  1 },
		{  1, -1, -1 }, {  1, -1,  1 }, { -1, -1, -1 },
		/* right */
		{  1, -1, -1 }, {  1,  1, -1 }, {  1,  1,  1 },
		{  1, -1,  1 }, {  1, -1, -1 }, {  1,  1,  1 },
		/* left */
		{ -1, -1, -1 }, { -1,  1,  1 }, { -1,  1, -1 },
		{ -1, -1,  1 }, { -1,  1,  1 }, { -1, -1, -1 }
	};
	gfloat *v;
	guint   n = 0;
	guint   i;

	*out_count = 36;
	v = g_new (gfloat, 36 * 3);
	for (i = 0; i < 36; i++)
		put_vertex (v, &n, 0.5f * corners[i][0], 0.5f * corners[i][1],
		            0.5f * corners[i][2]);
	return v;
}

/* the bits of one position, the key that finds its index */
typedef struct
{
	guint32 bits[3];
} PositionKey;

static guint
position_hash (gconstpointer data)
{
	const PositionKey *key = data;

	return ((key->bits[0] * 16777619u) ^ key->bits[1]) * 16777619u ^ key->bits[2];
}

static gboolean
position_equal (gconstpointer a,
                gconstpointer b)
{
	return memcmp (a, b, sizeof (PositionKey)) == 0;
}

/*
 * index_positions:
 * @positions: @count positions, three floats each (a triangle list)
 * @count: positions
 * @out_unique: (out): the distinct positions
 *
 * The same triangle list as one copy of each distinct position (equal
 * bit for bit) and an index per corner, so the GPU shades a shared
 * corner once instead of once per triangle. The triangles, their order
 * and their corners are unchanged.
 *
 * Returns: (transfer full) (nullable): the @count indices, rewriting
 *   @positions to the @out_unique distinct ones in first-use order; %NULL
 *   (and @positions untouched) when they would not fit 16-bit indices
 */
static guint16 *
index_positions (gfloat *positions,
                 guint   count,
                 guint  *out_unique)
{
	g_autoptr(GHashTable) seen = NULL;
	g_autofree PositionKey *keys = NULL;
	guint16 *indices;
	guint    unique = 0;
	guint    i;

	*out_unique = count;
	if (count == 0)
		return NULL;

	seen = g_hash_table_new (position_hash, position_equal);
	keys = g_new (PositionKey, count);
	indices = g_new (guint16, count);
	for (i = 0; i < count; i++)
	{
		gpointer found;

		memcpy (keys[i].bits, &positions[i * 3], sizeof keys[i].bits);
		if (g_hash_table_lookup_extended (seen, &keys[i], NULL, &found))
		{
			indices[i] = (guint16)GPOINTER_TO_UINT (found);
			continue;
		}
		if (unique > G_MAXUINT16)
		{
			g_free (indices);
			return NULL;
		}
		g_hash_table_insert (seen, &keys[i], GUINT_TO_POINTER (unique));
		indices[i] = (guint16)unique;
		unique++;
	}

	/* compact: each distinct position once, in the order first used (an
	 * index is never past its corner, and the bits come from the keys) */
	for (i = 0; i < count; i++)
		memcpy (&positions[(gsize)indices[i] * 3], keys[i].bits, sizeof keys[i].bits);
	*out_unique = unique;
	return indices;
}

/* ==========================================================================
 * The shared table
 * ========================================================================== */

/* forget_instancing: the instance buffer and array are gone with
 * their context (or about to be freed by the caller) */
static void
forget_instancing (LrgShape3DMesh *entry)
{
	entry->instance_vao = 0;
	entry->instance_vbo = 0;
	entry->instance_capacity = 0;
}

/* unload: frees the GPU objects (only while the context lives) and the
 * CPU positions and indices */
static void
unload (LrgShape3DMesh *entry)
{
	if (IsWindowReady () && entry->window == GetWindowHandle ())
	{
		if (entry->instance_vao != 0)
			rlUnloadVertexArray (entry->instance_vao);
		if (entry->instance_vbo != 0)
			rlUnloadVertexBuffer (entry->instance_vbo);
		UnloadMesh (entry->mesh);
	}
	else
	{
		/* the context that owned the buffers is gone with them */
		RL_FREE (entry->mesh.vboId);
		RL_FREE (entry->mesh.vertices);
		RL_FREE (entry->mesh.indices);
	}
	forget_instancing (entry);
	memset (&entry->mesh, 0, sizeof entry->mesh);
}

/* upload: the positions (and indices, when there are any) in raylib's
 * allocator, which UnloadMesh() frees, into a fresh mesh in the current
 * context; @count is the corners, @unique the positions */
static void
upload (LrgShape3DMesh *entry,
        const gfloat   *positions,
        guint           unique,
        const guint16  *indices,
        guint           count)
{
	memset (&entry->mesh, 0, sizeof entry->mesh);
	entry->mesh.vertexCount = (gint)unique;
	entry->mesh.triangleCount = (gint)(count / 3);
	entry->mesh.vertices = RL_MALLOC ((gsize)unique * 3 * sizeof (gfloat));
	memcpy (entry->mesh.vertices, positions, (gsize)unique * 3 * sizeof (gfloat));
	if (indices != NULL)
	{
		entry->mesh.indices = RL_MALLOC ((gsize)count * sizeof (guint16));
		memcpy (entry->mesh.indices, indices, (gsize)count * sizeof (guint16));
	}
	UploadMesh (&entry->mesh, false);
	entry->window = GetWindowHandle ();
	forget_instancing (entry);
}

/* build_and_upload: (re)makes @entry's mesh from its key in the
 * current context, indexed when its positions fit 16-bit indices;
 * FALSE when there is nothing to draw or it failed */
static gboolean
build_and_upload (LrgShape3DMesh *entry)
{
	g_autofree gfloat  *positions = NULL;
	g_autofree guint16 *indices = NULL;
	guint               count = 0;
	guint               unique = 0;

	if (entry->key.kind == SHAPE_MESH_SPHERE)
	{
		positions = build_sphere (entry->key.a, entry->key.b, &count);
	}
	else if (entry->key.kind == SHAPE_MESH_CUBE)
	{
		positions = build_cube (&count);
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
	indices = index_positions (positions, count, &unique);
	upload (entry, positions, unique, indices, count);
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
	entry->instances = g_array_new (FALSE, FALSE, sizeof (ShapeInstance));
	if (!build_and_upload (entry))
	{
		unload (entry);
		g_array_unref (entry->instances);
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

LrgShape3DMesh *
lrg_shape3d_mesh_acquire_cube (void)
{
	ShapeMeshKey key;

	memset (&key, 0, sizeof key);
	key.kind = SHAPE_MESH_CUBE;
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
	g_array_unref (mesh->instances);
	g_free (mesh);
}

gboolean
lrg_shape3d_mesh_prepare (LrgShape3DMesh *mesh)
{
	g_return_val_if_fail (mesh != NULL, FALSE);

	if (!IsWindowReady ())
		return FALSE;
	if (mesh->window != GetWindowHandle () || mesh->mesh.vaoId == 0)
	{
		/* a new context: the old buffers died with the old one (and
		 * what was queued for them was meant for it) */
		g_array_set_size (mesh->instances, 0);
		unload (mesh);
		if (!build_and_upload (mesh))
			return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * The batch
 * ========================================================================== */

/* batch_program_ready: the instanced program of the current context,
 * made on first use (FALSE without GLSL 330, in stereo, or when it does
 * not build: the shapes then draw one by one) */
static gboolean
batch_program_ready (void)
{
	gint version;

	if (!IsWindowReady () || rlIsStereoRenderEnabled ())
		return FALSE;
	if (batch_program_window != GetWindowHandle ())
	{
		/* a new context: the old program died with the old one */
		batch_program = 0;
		batch_program_failed = FALSE;
		batch_program_window = GetWindowHandle ();
	}
	if (batch_program != 0)
		return TRUE;
	if (batch_program_failed)
		return FALSE;

	version = rlGetVersion ();
	if (version != RL_OPENGL_33 && version != RL_OPENGL_43)
	{
		batch_program_failed = TRUE;
		return FALSE;
	}
	batch_program = rlLoadShaderProgram (batch_vs, batch_fs);
	if (batch_program == 0 || batch_program == rlGetShaderIdDefault ())
	{
		/* rlgl hands back the default program when a build fails */
		batch_program = 0;
		batch_program_failed = TRUE;
		return FALSE;
	}
	return TRUE;
}

/* ensure_instancing: @entry's instance buffer, big enough for @count
 * instances, and the vertex array that reads it next to the mesh's
 * positions and indices */
static gboolean
ensure_instancing (LrgShape3DMesh *entry,
                   guint           count)
{
	guint capacity;
	guint k;

	if (entry->instance_vao != 0 && entry->instance_capacity >= count)
		return TRUE;

	if (entry->instance_vao != 0)
		rlUnloadVertexArray (entry->instance_vao);
	if (entry->instance_vbo != 0)
		rlUnloadVertexBuffer (entry->instance_vbo);
	forget_instancing (entry);

	capacity = SHAPE_MIN_INSTANCES;
	while (capacity < count && capacity < G_MAXUINT / 2)
		capacity *= 2;

	entry->instance_vao = rlLoadVertexArray ();
	if (entry->instance_vao == 0 || !rlEnableVertexArray (entry->instance_vao))
	{
		forget_instancing (entry);
		return FALSE;
	}

	/* the mesh's positions, per vertex */
	rlEnableVertexBuffer (entry->mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION]);
	rlSetVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, 3, RL_FLOAT, false, 0, 0);
	rlEnableVertexAttribute (RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION);

	/* the instances: four matrix columns and the colour, per instance */
	entry->instance_vbo = rlLoadVertexBuffer (NULL, (gint)(capacity * sizeof (ShapeInstance)), true);
	for (k = 0; k < 4; k++)
	{
		rlSetVertexAttribute (SHAPE_LOC_MVP + k, 4, RL_FLOAT, false, (gint)sizeof (ShapeInstance),
		                      (gint)(k * 4 * sizeof (gfloat)));
		rlEnableVertexAttribute (SHAPE_LOC_MVP + k);
		rlSetVertexAttributeDivisor (SHAPE_LOC_MVP + k, 1);
	}
	rlSetVertexAttribute (SHAPE_LOC_COLOR, 4, RL_FLOAT, false, (gint)sizeof (ShapeInstance),
	                      (gint)G_STRUCT_OFFSET (ShapeInstance, color));
	rlEnableVertexAttribute (SHAPE_LOC_COLOR);
	rlSetVertexAttributeDivisor (SHAPE_LOC_COLOR, 1);

	if (entry->mesh.indices != NULL)
		rlEnableVertexBufferElement (entry->mesh.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_INDICES]);
	rlDisableVertexArray ();
	rlDisableVertexBuffer ();
	rlDisableVertexBufferElement ();

	if (entry->instance_vbo == 0)
	{
		rlUnloadVertexArray (entry->instance_vao);
		forget_instancing (entry);
		return FALSE;
	}
	entry->instance_capacity = capacity;
	return TRUE;
}

/* draw_instances: one instanced call for what @entry has queued */
static void
draw_instances (LrgShape3DMesh *entry)
{
	guint count = entry->instances->len;

	if (count == 0)
		return;
	if (!lrg_shape3d_mesh_prepare (entry) || !ensure_instancing (entry, count))
	{
		g_array_set_size (entry->instances, 0);
		return;
	}
	rlUpdateVertexBuffer (entry->instance_vbo, entry->instances->data,
	                      (gint)(count * sizeof (ShapeInstance)), 0);
	rlEnableShader (batch_program);
	rlEnableVertexArray (entry->instance_vao);
	if (entry->mesh.indices != NULL)
		rlDrawVertexArrayElementsInstanced (0, entry->mesh.triangleCount * 3, NULL, (gint)count);
	else
		rlDrawVertexArrayInstanced (0, entry->mesh.vertexCount, (gint)count);
	rlDisableVertexArray ();
	rlDisableShader ();
	g_array_set_size (entry->instances, 0);
}

void
lrg_shape3d_mesh_batch_begin (void)
{
	batch_depth++;
	if (batch_depth == 1)
	{
		if (batch_pending == NULL)
			batch_pending = g_ptr_array_new ();
		/* make the program now, not at the first shape */
		batch_program_ready ();
	}
}

void
lrg_shape3d_mesh_batch_flush (void)
{
	g_autoptr(GPtrArray) drawn = NULL;
	guint                i;

	if (batch_pending == NULL || batch_pending->len == 0)
		return;

	/* the pending list is swapped out first: releasing a mesh may free it */
	drawn = batch_pending;
	batch_pending = g_ptr_array_new ();

	/* what rlgl queued before the flush lands before the shapes, as it
	 * did before each DrawMesh() */
	rlDrawRenderBatchActive ();
	if (batch_program_ready ())
	{
		for (i = 0; i < drawn->len; i++)
			draw_instances (g_ptr_array_index (drawn, i));
	}
	for (i = 0; i < drawn->len; i++)
	{
		LrgShape3DMesh *entry = g_ptr_array_index (drawn, i);

		g_array_set_size (entry->instances, 0);
		lrg_shape3d_mesh_release (entry);
	}
}

void
lrg_shape3d_mesh_batch_end (void)
{
	g_return_if_fail (batch_depth > 0);

	if (--batch_depth == 0)
		lrg_shape3d_mesh_batch_flush ();
}

gboolean
lrg_shape3d_mesh_batching (void)
{
	return batch_depth > 0 && batch_program_ready ();
}

/* batch_queue: appends the draw DrawMesh() would make of @mesh under
 * the current rlgl matrices to its instance list */
static void
batch_queue (LrgShape3DMesh *mesh,
             const GrlColor *color,
             gfloat          sx,
             gfloat          sy,
             gfloat          sz)
{
	ShapeInstance instance;
	Matrix        model;
	Matrix        mvp;

	/* DrawMesh(): matModel = MatrixMultiply (transform, rlGetMatrixTransform ()),
	 * matModelView = MatrixMultiply (matModel, view),
	 * mvp = MatrixMultiply (matModelView, projection); raylib's
	 * MatrixMultiply (a, b) is the column-major product b * a */
	memset (&model, 0, sizeof model);
	model.m0 = sx;
	model.m5 = sy;
	model.m10 = sz;
	model.m15 = 1.0f;
	mvp = lrg_draw_cull_mul (rlGetMatrixProjection (),
	                         lrg_draw_cull_mul (rlGetMatrixModelview (),
	                                            lrg_draw_cull_mul (rlGetMatrixTransform (),
	                                                               model)));
	instance.mvp[0] = mvp.m0;   instance.mvp[1] = mvp.m1;
	instance.mvp[2] = mvp.m2;   instance.mvp[3] = mvp.m3;
	instance.mvp[4] = mvp.m4;   instance.mvp[5] = mvp.m5;
	instance.mvp[6] = mvp.m6;   instance.mvp[7] = mvp.m7;
	instance.mvp[8] = mvp.m8;   instance.mvp[9] = mvp.m9;
	instance.mvp[10] = mvp.m10; instance.mvp[11] = mvp.m11;
	instance.mvp[12] = mvp.m12; instance.mvp[13] = mvp.m13;
	instance.mvp[14] = mvp.m14; instance.mvp[15] = mvp.m15;
	/* DrawMesh()'s colDiffuse, to the bit */
	instance.color[0] = (gfloat)color->r / 255.0f;
	instance.color[1] = (gfloat)color->g / 255.0f;
	instance.color[2] = (gfloat)color->b / 255.0f;
	instance.color[3] = (gfloat)color->a / 255.0f;

	if (mesh->instances->len == 0)
	{
		/* held while queued, in case its last shape goes first */
		mesh->refs++;
		g_ptr_array_add (batch_pending, mesh);
	}
	g_array_append_val (mesh->instances, instance);
}

gboolean
lrg_shape3d_mesh_draw (LrgShape3DMesh *mesh,
                       const GrlColor *color,
                       gfloat          sx,
                       gfloat          sy,
                       gfloat          sz)
{
	MaterialMap maps[SHAPE_MATERIAL_MAPS];
	Material    material;
	Matrix      model;

	g_return_val_if_fail (mesh != NULL, FALSE);
	g_return_val_if_fail (color != NULL, FALSE);

	if (!lrg_shape3d_mesh_prepare (mesh))
		return FALSE;

	if (batch_depth > 0)
	{
		/* an opaque shape joins its mesh's instances; a translucent one
		 * must see everything drawn before it, so the batch goes first */
		if (color->a == 255 && batch_program_ready ())
		{
			batch_queue (mesh, color, sx, sy, sz);
			return TRUE;
		}
		lrg_shape3d_mesh_batch_flush ();
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
	model.m0 = sx;
	model.m5 = sy;
	model.m10 = sz;
	model.m15 = 1.0f;
	DrawMesh (mesh->mesh, material, model);
	return TRUE;
}

void
lrg_shape3d_mesh_push_transform (const GrlVector3 *position,
                                 const GrlVector3 *rotation,
                                 const GrlVector3 *scale)
{
	rlPushMatrix ();

	/* translate, rotate (XYZ order), scale; a rotation by zero and a
	 * scale by one multiply by the identity, which changes no bit, so
	 * they are skipped */
	rlTranslatef (position->x, position->y, position->z);
	if (rotation->x != 0.0f)
		rlRotatef (rotation->x * RAD2DEG, 1.0f, 0.0f, 0.0f);
	if (rotation->y != 0.0f)
		rlRotatef (rotation->y * RAD2DEG, 0.0f, 1.0f, 0.0f);
	if (rotation->z != 0.0f)
		rlRotatef (rotation->z * RAD2DEG, 0.0f, 0.0f, 1.0f);
	if (scale->x != 1.0f || scale->y != 1.0f || scale->z != 1.0f)
		rlScalef (scale->x, scale->y, scale->z);
}
