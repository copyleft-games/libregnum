/* lrg-uv-unwrap.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgUvMesh (a headless CPU mesh) and LrgUvUnwrap (the lightmap chart
 * unwrapper).  See the header for the overview.  The algorithm is our
 * own; the concepts follow xatlas (MIT) and Thekla.
 *
 * Unwrap pipeline (lrg_uv_unwrap_run):
 *   1. weld     positions are quantized so triangles that share a corner
 *               position share an id even when the mesh split the vertex
 *               (hard normals, uv seams)
 *   2. adjacency manifold edges (exactly two triangles) link triangles
 *   3. charts   seeds in decreasing area; a breadth-first flood adds a
 *               neighbour when its normal is within max-chart-angle of
 *               the seed normal and its projection onto the seed plane
 *               does not overlap the chart (SAT test through a 2D grid)
 *   4. rotate   each chart turns to its minimum-area bounding rectangle
 *               (rotating calipers on the convex hull), wide side on X
 *   5. pack     a binary search for the largest uniform scale at which
 *               the padded chart rectangles fit a resolution^2 map
 *               (skyline bottom-left, tallest first)
 *   6. output   one vertex per (chart, input vertex) pair, in first-use
 *               order, so the result is deterministic
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <raylib.h>

#include "lrg-uv-unwrap.h"
#include "../core/lrg-math3d.h"

/* ==========================================================================
 * Errors
 * ========================================================================== */

GType
lrg_uv_unwrap_error_get_type (void)
{
	static gsize type_id = 0;
	static const GEnumValue values[] = {
		{ LRG_UV_UNWRAP_ERROR_EMPTY, "LRG_UV_UNWRAP_ERROR_EMPTY", "empty" },
		{ LRG_UV_UNWRAP_ERROR_INVALID, "LRG_UV_UNWRAP_ERROR_INVALID", "invalid" },
		{ LRG_UV_UNWRAP_ERROR_NO_FIT, "LRG_UV_UNWRAP_ERROR_NO_FIT", "no-fit" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type_id))
	{
		GType id = g_enum_register_static (g_intern_static_string ("LrgUvUnwrapError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

G_DEFINE_QUARK (lrg-uv-unwrap-error-quark, lrg_uv_unwrap_error)

/* ==========================================================================
 * LrgUvMesh
 * ========================================================================== */

struct _LrgUvMesh
{
	GObject  parent_instance;

	guint    n_vertices;
	gfloat  *positions;   /* 3 per vertex */
	gfloat  *normals;     /* 3 per vertex or NULL */
	gfloat  *uvs;         /* 2 per vertex or NULL */
	gfloat  *uv2;         /* 2 per vertex or NULL */
	guint    n_indices;
	guint32 *indices;
};

G_DEFINE_FINAL_TYPE (LrgUvMesh, lrg_uv_mesh, G_TYPE_OBJECT)

static void
lrg_uv_mesh_finalize (GObject *object)
{
	LrgUvMesh *self = LRG_UV_MESH (object);

	g_free (self->positions);
	g_free (self->normals);
	g_free (self->uvs);
	g_free (self->uv2);
	g_free (self->indices);
	G_OBJECT_CLASS (lrg_uv_mesh_parent_class)->finalize (object);
}

static void
lrg_uv_mesh_class_init (LrgUvMeshClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_uv_mesh_finalize;
}

static void
lrg_uv_mesh_init (LrgUvMesh *self)
{
}

/* Adopts the arrays (no copies). */
static LrgUvMesh *
uv_mesh_adopt (gfloat  *positions,
               guint    n_vertices,
               guint32 *indices,
               guint    n_indices)
{
	LrgUvMesh *self = g_object_new (LRG_TYPE_UV_MESH, NULL);

	self->positions = positions;
	self->n_vertices = n_vertices;
	self->indices = indices;
	self->n_indices = n_indices;
	return self;
}

LrgUvMesh *
lrg_uv_mesh_new (const gfloat  *positions,
                 gsize          n_floats,
                 const guint32 *indices,
                 gsize          n_indices)
{
	gsize    n_vertices = n_floats / 3u;
	guint32 *idx;
	gsize    i;

	g_return_val_if_fail (positions != NULL || n_floats == 0, NULL);

	if (n_vertices > (gsize)G_MAXUINT32 || (indices != NULL && n_indices % 3u != 0))
		return NULL;
	if (indices == NULL)
		n_indices = n_vertices - n_vertices % 3u;
	if (n_indices > (gsize)G_MAXUINT32)
		return NULL;

	idx = g_new (guint32, MAX (n_indices, 1u));
	for (i = 0; i < n_indices; i++)
	{
		idx[i] = indices != NULL ? indices[i] : (guint32)i;
		if ((gsize)idx[i] >= n_vertices)
		{
			g_free (idx);
			return NULL;
		}
	}
	return uv_mesh_adopt (g_memdup2 (positions, MAX (n_vertices, 1u) * 3u * sizeof (gfloat)),
	                      (guint)n_vertices, idx, (guint)n_indices);
}

guint
lrg_uv_mesh_get_n_vertices (LrgUvMesh *self)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), 0);
	return self->n_vertices;
}

guint
lrg_uv_mesh_get_n_triangles (LrgUvMesh *self)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), 0);
	return self->n_indices / 3u;
}

const gfloat *
lrg_uv_mesh_get_positions (LrgUvMesh *self,
                           gsize     *n_floats)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), NULL);
	if (n_floats != NULL)
		*n_floats = (gsize)self->n_vertices * 3u;
	return self->positions;
}

const guint32 *
lrg_uv_mesh_get_indices (LrgUvMesh *self,
                         gsize     *n_indices)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), NULL);
	if (n_indices != NULL)
		*n_indices = self->n_indices;
	return self->indices;
}

const gfloat *
lrg_uv_mesh_get_normals (LrgUvMesh *self,
                         gsize     *n_floats)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), NULL);
	if (n_floats != NULL)
		*n_floats = self->normals != NULL ? (gsize)self->n_vertices * 3u : 0u;
	return self->normals;
}

/* shared setter: copies @n_floats = per * vertices floats into *slot */
static gboolean
set_attr (LrgUvMesh     *self,
          gfloat       **slot,
          const gfloat  *data,
          gsize          n_floats,
          guint          per)
{
	if (data == NULL)
	{
		g_clear_pointer (slot, g_free);
		return TRUE;
	}
	if (n_floats != (gsize)self->n_vertices * per || n_floats == 0)
		return FALSE;
	g_free (*slot);
	*slot = g_memdup2 (data, n_floats * sizeof (gfloat));
	return TRUE;
}

gboolean
lrg_uv_mesh_set_normals (LrgUvMesh    *self,
                         const gfloat *normals,
                         gsize         n_floats)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), FALSE);
	return set_attr (self, &self->normals, normals, n_floats, 3u);
}

const gfloat *
lrg_uv_mesh_get_uvs (LrgUvMesh *self,
                     gsize     *n_floats)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), NULL);
	if (n_floats != NULL)
		*n_floats = self->uvs != NULL ? (gsize)self->n_vertices * 2u : 0u;
	return self->uvs;
}

gboolean
lrg_uv_mesh_set_uvs (LrgUvMesh    *self,
                     const gfloat *uvs,
                     gsize         n_floats)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), FALSE);
	return set_attr (self, &self->uvs, uvs, n_floats, 2u);
}

const gfloat *
lrg_uv_mesh_get_uv2 (LrgUvMesh *self,
                     gsize     *n_floats)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), NULL);
	if (n_floats != NULL)
		*n_floats = self->uv2 != NULL ? (gsize)self->n_vertices * 2u : 0u;
	return self->uv2;
}

gboolean
lrg_uv_mesh_set_uv2 (LrgUvMesh    *self,
                     const gfloat *uv2,
                     gsize         n_floats)
{
	g_return_val_if_fail (LRG_IS_UV_MESH (self), FALSE);
	return set_attr (self, &self->uv2, uv2, n_floats, 2u);
}

/* small vector helpers */
static void
v3_sub (const gfloat *a,
        const gfloat *b,
        gfloat       *out)
{
	out[0] = a[0] - b[0];
	out[1] = a[1] - b[1];
	out[2] = a[2] - b[2];
}

static void
v3_cross (const gfloat *a,
          const gfloat *b,
          gfloat       *out)
{
	gfloat x = a[1] * b[2] - a[2] * b[1];
	gfloat y = a[2] * b[0] - a[0] * b[2];
	gfloat z = a[0] * b[1] - a[1] * b[0];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

static gfloat
v3_dot (const gfloat *a,
        const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static gfloat
v3_normalize (gfloat *v)
{
	gfloat len = sqrtf (v3_dot (v, v));

	if (len > 1e-30f)
	{
		v[0] /= len;
		v[1] /= len;
		v[2] /= len;
	}
	return len;
}

/* unnormalized face normal (length = 2 x area) of triangle @t */
static void
face_normal (const gfloat  *pos,
             const guint32 *idx,
             guint          t,
             gfloat        *out)
{
	gfloat e1[3];
	gfloat e2[3];

	v3_sub (pos + (gsize)idx[t * 3u + 1u] * 3u, pos + (gsize)idx[t * 3u] * 3u, e1);
	v3_sub (pos + (gsize)idx[t * 3u + 2u] * 3u, pos + (gsize)idx[t * 3u] * 3u, e2);
	v3_cross (e1, e2, out);
}

void
lrg_uv_mesh_compute_normals (LrgUvMesh *self)
{
	guint t;
	guint v;

	g_return_if_fail (LRG_IS_UV_MESH (self));

	if (self->n_vertices == 0)
		return;
	g_free (self->normals);
	self->normals = g_new0 (gfloat, (gsize)self->n_vertices * 3u);
	for (t = 0; t < self->n_indices / 3u; t++)
	{
		gfloat n[3];
		gint   j;

		/* area weighting comes from the cross product's length */
		face_normal (self->positions, self->indices, t, n);
		for (j = 0; j < 3; j++)
		{
			gfloat *dst = self->normals + (gsize)self->indices[t * 3u + (guint)j] * 3u;

			dst[0] += n[0];
			dst[1] += n[1];
			dst[2] += n[2];
		}
	}
	for (v = 0; v < self->n_vertices; v++)
	{
		gfloat *n = self->normals + (gsize)v * 3u;

		if (v3_normalize (n) <= 1e-30f)
		{
			n[0] = 0.0f;
			n[1] = 1.0f;
			n[2] = 0.0f;
		}
	}
}

gdouble
lrg_uv_mesh_get_surface_area (LrgUvMesh    *self,
                              const gfloat *matrix)
{
	gdouble area = 0.0;
	guint   t;

	g_return_val_if_fail (LRG_IS_UV_MESH (self), 0.0);

	for (t = 0; t < self->n_indices / 3u; t++)
	{
		gfloat p[3][3];
		gfloat e1[3];
		gfloat e2[3];
		gfloat n[3];
		gint   j;

		for (j = 0; j < 3; j++)
		{
			const gfloat *src = self->positions + (gsize)self->indices[t * 3u + (guint)j] * 3u;

			if (matrix != NULL)
				lrg_math3d_transform_point (matrix, src, p[j]);
			else
				memcpy (p[j], src, sizeof p[j]);
		}
		v3_sub (p[1], p[0], e1);
		v3_sub (p[2], p[0], e2);
		v3_cross (e1, e2, n);
		area += 0.5 * sqrt ((gdouble)v3_dot (n, n));
	}
	return area;
}

gdouble
lrg_uv_mesh_get_uv2_area (LrgUvMesh *self)
{
	gdouble area = 0.0;
	guint   t;

	g_return_val_if_fail (LRG_IS_UV_MESH (self), 0.0);

	if (self->uv2 == NULL)
		return 0.0;
	for (t = 0; t < self->n_indices / 3u; t++)
	{
		const gfloat *a = self->uv2 + (gsize)self->indices[t * 3u] * 2u;
		const gfloat *b = self->uv2 + (gsize)self->indices[t * 3u + 1u] * 2u;
		const gfloat *c = self->uv2 + (gsize)self->indices[t * 3u + 2u] * 2u;

		area += 0.5 * fabs ((gdouble)(b[0] - a[0]) * (c[1] - a[1])
		                    - (gdouble)(c[0] - a[0]) * (b[1] - a[1]));
	}
	return area;
}

/* --- primitives ------------------------------------------------------- */

/* A growing vertex/index builder for the primitive generators. */
typedef struct
{
	GArray *pos;
	GArray *nrm;
	GArray *uv;
	GArray *idx;
} Builder;

static void
builder_init (Builder *b)
{
	b->pos = g_array_new (FALSE, FALSE, sizeof (gfloat));
	b->nrm = g_array_new (FALSE, FALSE, sizeof (gfloat));
	b->uv = g_array_new (FALSE, FALSE, sizeof (gfloat));
	b->idx = g_array_new (FALSE, FALSE, sizeof (guint32));
}

static guint32
builder_vertex (Builder *b,
                gfloat   x,
                gfloat   y,
                gfloat   z,
                gfloat   nx,
                gfloat   ny,
                gfloat   nz,
                gfloat   u,
                gfloat   v)
{
	gfloat  p[3];
	gfloat  n[3];
	gfloat  t[2];
	guint32 i = (guint32)(b->pos->len / 3u);

	p[0] = x;
	p[1] = y;
	p[2] = z;
	n[0] = nx;
	n[1] = ny;
	n[2] = nz;
	v3_normalize (n);
	t[0] = u;
	t[1] = v;
	g_array_append_vals (b->pos, p, 3);
	g_array_append_vals (b->nrm, n, 3);
	g_array_append_vals (b->uv, t, 2);
	return i;
}

/*
 * Adds a triangle and makes it face the way its vertex normals point
 * (the generators then need not care about winding).
 */
static void
builder_tri (Builder *b,
             guint32  i0,
             guint32  i1,
             guint32  i2)
{
	const gfloat *pos = (const gfloat *)b->pos->data;
	const gfloat *nrm = (const gfloat *)b->nrm->data;
	gfloat        e1[3];
	gfloat        e2[3];
	gfloat        fn[3];
	gfloat        vn[3];
	guint32       tri[3];

	v3_sub (pos + (gsize)i1 * 3u, pos + (gsize)i0 * 3u, e1);
	v3_sub (pos + (gsize)i2 * 3u, pos + (gsize)i0 * 3u, e2);
	v3_cross (e1, e2, fn);
	if (v3_dot (fn, fn) <= 1e-24f)
		return;   /* drop slivers at poles and apexes */
	vn[0] = nrm[i0 * 3u] + nrm[i1 * 3u] + nrm[i2 * 3u];
	vn[1] = nrm[i0 * 3u + 1u] + nrm[i1 * 3u + 1u] + nrm[i2 * 3u + 1u];
	vn[2] = nrm[i0 * 3u + 2u] + nrm[i1 * 3u + 2u] + nrm[i2 * 3u + 2u];
	tri[0] = i0;
	if (v3_dot (fn, vn) >= 0.0f)
	{
		tri[1] = i1;
		tri[2] = i2;
	}
	else
	{
		tri[1] = i2;
		tri[2] = i1;
	}
	g_array_append_vals (b->idx, tri, 3);
}

static LrgUvMesh *
builder_finish (Builder *b)
{
	LrgUvMesh *mesh;
	guint      n_vertices = b->pos->len / 3u;
	guint      n_indices = b->idx->len;

	mesh = uv_mesh_adopt ((gfloat *)(gpointer)g_array_free (b->pos, FALSE), n_vertices,
	                      (guint32 *)(gpointer)g_array_free (b->idx, FALSE), n_indices);
	mesh->normals = (gfloat *)(gpointer)g_array_free (b->nrm, FALSE);
	mesh->uvs = (gfloat *)(gpointer)g_array_free (b->uv, FALSE);
	return mesh;
}

/* a quad (a, b, c, d around) with one normal */
static void
builder_quad (Builder      *b,
              const gfloat *a,
              const gfloat *bb,
              const gfloat *c,
              const gfloat *d,
              const gfloat *n)
{
	guint32 i0 = builder_vertex (b, a[0], a[1], a[2], n[0], n[1], n[2], 0.0f, 0.0f);
	guint32 i1 = builder_vertex (b, bb[0], bb[1], bb[2], n[0], n[1], n[2], 1.0f, 0.0f);
	guint32 i2 = builder_vertex (b, c[0], c[1], c[2], n[0], n[1], n[2], 1.0f, 1.0f);
	guint32 i3 = builder_vertex (b, d[0], d[1], d[2], n[0], n[1], n[2], 0.0f, 1.0f);

	builder_tri (b, i0, i1, i2);
	builder_tri (b, i0, i2, i3);
}

static void
gen_box (Builder *b,
         gfloat   hx,
         gfloat   hy,
         gfloat   hz)
{
	/* each face: normal axis, sign; corners walk around the face */
	gint axis;
	gint s;

	for (axis = 0; axis < 3; axis++)
	{
		for (s = -1; s <= 1; s += 2)
		{
			gfloat h[3];
			gfloat n[3] = { 0.0f, 0.0f, 0.0f };
			gfloat c[4][3];
			gint   ua = (axis + 1) % 3;
			gint   va = (axis + 2) % 3;
			gint   k;

			h[0] = hx;
			h[1] = hy;
			h[2] = hz;
			n[axis] = (gfloat)s;
			for (k = 0; k < 4; k++)
			{
				gfloat su = (k == 1 || k == 2) ? 1.0f : -1.0f;
				gfloat sv = (k >= 2) ? 1.0f : -1.0f;

				c[k][axis] = (gfloat)s * h[axis];
				c[k][ua] = su * h[ua];
				c[k][va] = sv * h[va];
			}
			builder_quad (b, c[0], c[1], c[2], c[3], n);
		}
	}
}

static void
gen_sphere (Builder *b,
            gfloat   r,
            gint     rings,
            gint     slices)
{
	gint i;
	gint j;

	for (i = 0; i <= rings; i++)
	{
		gfloat th = (gfloat)G_PI * (gfloat)i / (gfloat)rings;   /* 0 at +Y */

		for (j = 0; j <= slices; j++)
		{
			gfloat ph = 2.0f * (gfloat)G_PI * (gfloat)j / (gfloat)slices;
			gfloat nx = sinf (th) * cosf (ph);
			gfloat ny = cosf (th);
			gfloat nz = sinf (th) * sinf (ph);

			builder_vertex (b, nx * r, ny * r, nz * r, nx, ny, nz,
			                (gfloat)j / (gfloat)slices, (gfloat)i / (gfloat)rings);
		}
	}
	for (i = 0; i < rings; i++)
	{
		for (j = 0; j < slices; j++)
		{
			guint32 a = (guint32)(i * (slices + 1) + j);
			guint32 c = (guint32)((i + 1) * (slices + 1) + j);

			builder_tri (b, a, c, c + 1u);
			builder_tri (b, a, c + 1u, a + 1u);
		}
	}
}

/* a flat disc in the plane of @axis_n (y = @y), facing @ny (+1 / -1) */
static void
gen_disc (Builder *b,
          gfloat   r,
          gfloat   y,
          gfloat   ny,
          gint     sides)
{
	guint32 centre = builder_vertex (b, 0.0f, y, 0.0f, 0.0f, ny, 0.0f, 0.5f, 0.5f);
	guint32 first = (guint32)(b->pos->len / 3u);
	gint    i;

	for (i = 0; i <= sides; i++)
	{
		gfloat a = 2.0f * (gfloat)G_PI * (gfloat)i / (gfloat)sides;

		builder_vertex (b, cosf (a) * r, y, sinf (a) * r, 0.0f, ny, 0.0f,
		                0.5f + 0.5f * cosf (a), 0.5f + 0.5f * sinf (a));
	}
	for (i = 0; i < sides; i++)
		builder_tri (b, centre, first + (guint32)i, first + (guint32)i + 1u);
}

/* cylinder (r_top = r_bottom) or cone (r_top = 0) centred on the origin */
static void
gen_tube (Builder *b,
          gfloat   r_bottom,
          gfloat   r_top,
          gfloat   h,
          gint     sides)
{
	gfloat  y0 = -0.5f * h;
	gfloat  y1 = 0.5f * h;
	gfloat  slope = (r_bottom - r_top) / MAX (h, 1e-6f);   /* normal y */
	guint32 first = (guint32)(b->pos->len / 3u);
	gint    i;

	for (i = 0; i <= sides; i++)
	{
		gfloat a = 2.0f * (gfloat)G_PI * (gfloat)i / (gfloat)sides;
		gfloat c = cosf (a);
		gfloat s = sinf (a);

		builder_vertex (b, c * r_bottom, y0, s * r_bottom, c, slope, s, (gfloat)i / (gfloat)sides, 1.0f);
		builder_vertex (b, c * r_top, y1, s * r_top, c, slope, s, (gfloat)i / (gfloat)sides, 0.0f);
	}
	for (i = 0; i < sides; i++)
	{
		guint32 a0 = first + (guint32)i * 2u;
		guint32 a1 = a0 + 1u;
		guint32 b0 = a0 + 2u;
		guint32 b1 = a0 + 3u;

		builder_tri (b, a0, a1, b1);
		builder_tri (b, a0, b1, b0);
	}
	gen_disc (b, r_bottom, y0, -1.0f, sides);
	if (r_top > 0.0f)
		gen_disc (b, r_top, y1, 1.0f, sides);
}

static void
gen_torus (Builder *b,
           gfloat   major,
           gfloat   minor,
           gint     major_segments,
           gint     minor_segments)
{
	gint i;
	gint j;

	for (i = 0; i <= major_segments; i++)
	{
		gfloat u = (gfloat)i / (gfloat)major_segments * 2.0f * (gfloat)G_PI;

		for (j = 0; j <= minor_segments; j++)
		{
			gfloat w = (gfloat)j / (gfloat)minor_segments * 2.0f * (gfloat)G_PI;
			gfloat nx = cosf (w) * cosf (u);
			gfloat ny = sinf (w);
			gfloat nz = cosf (w) * sinf (u);

			builder_vertex (b, (major + minor * cosf (w)) * cosf (u), minor * ny,
			                (major + minor * cosf (w)) * sinf (u), nx, ny, nz,
			                (gfloat)i / (gfloat)major_segments, (gfloat)j / (gfloat)minor_segments);
		}
	}
	for (i = 0; i < major_segments; i++)
	{
		for (j = 0; j < minor_segments; j++)
		{
			guint32 a = (guint32)(i * (minor_segments + 1) + j);
			guint32 c = (guint32)((i + 1) * (minor_segments + 1) + j);

			builder_tri (b, a, c, c + 1u);
			builder_tri (b, a, c + 1u, a + 1u);
		}
	}
}

/* turns +Y-facing geometry to face +Z (the 2D shapes): (x, y, z) -> (x, -z, y) */
static void
builder_rotate_x90 (Builder *b)
{
	gfloat *p = (gfloat *)b->pos->data;
	gfloat *n = (gfloat *)b->nrm->data;
	guint   i;

	for (i = 0; i < b->pos->len / 3u; i++)
	{
		gfloat y = p[i * 3u + 1u];
		gfloat ny = n[i * 3u + 1u];

		p[i * 3u + 1u] = -p[i * 3u + 2u];
		p[i * 3u + 2u] = y;
		n[i * 3u + 1u] = -n[i * 3u + 2u];
		n[i * 3u + 2u] = ny;
	}
}

static gfloat
param_or (const gfloat *params,
          guint         i,
          gfloat        fallback)
{
	if (params == NULL || !isfinite (params[i]) || params[i] <= 0.0f)
		return fallback;
	return params[i];
}

LrgUvMesh *
lrg_uv_mesh_new_primitive (LrgPrimitiveType  primitive,
                           const gfloat     *params)
{
	Builder b;
	gfloat  r;

	switch (primitive)
	{
	case LRG_PRIMITIVE_CUBE:
		builder_init (&b);
		gen_box (&b, 0.5f * param_or (params, 0, 1.0f), 0.5f * param_or (params, 1, 1.0f),
		         0.5f * param_or (params, 2, 1.0f));
		break;
	case LRG_PRIMITIVE_UV_SPHERE:
	case LRG_PRIMITIVE_ICO_SPHERE:
		builder_init (&b);
		gen_sphere (&b, param_or (params, 0, 0.5f), 16, 24);
		break;
	case LRG_PRIMITIVE_PLANE:
	case LRG_PRIMITIVE_RECTANGLE_2D:
		{
			gfloat hx = 0.5f * param_or (params, 0, 1.0f);
			gfloat hz = 0.5f * param_or (params, 1, 1.0f);
			gfloat a[3] = { -hx, 0.0f, -hz };
			gfloat bb[3] = { -hx, 0.0f, hz };
			gfloat c[3] = { hx, 0.0f, hz };
			gfloat d[3] = { hx, 0.0f, -hz };
			gfloat n[3] = { 0.0f, 1.0f, 0.0f };

			builder_init (&b);
			builder_quad (&b, a, bb, c, d, n);
			if (primitive == LRG_PRIMITIVE_RECTANGLE_2D)
				builder_rotate_x90 (&b);
		}
		break;
	case LRG_PRIMITIVE_CIRCLE:
	case LRG_PRIMITIVE_CIRCLE_2D:
		builder_init (&b);
		gen_disc (&b, param_or (params, 0, 0.5f), 0.0f, 1.0f, 32);
		if (primitive == LRG_PRIMITIVE_CIRCLE_2D)
			builder_rotate_x90 (&b);
		break;
	case LRG_PRIMITIVE_CYLINDER:
		builder_init (&b);
		r = param_or (params, 0, 0.5f);
		gen_tube (&b, r, r, param_or (params, 1, 1.0f), 24);
		break;
	case LRG_PRIMITIVE_CONE:
		builder_init (&b);
		gen_tube (&b, param_or (params, 0, 0.5f), 0.0f, param_or (params, 1, 1.0f), 24);
		break;
	case LRG_PRIMITIVE_TORUS:
		/* the renderer scales a 0.375 / 0.125 torus uniformly */
		builder_init (&b);
		r = param_or (params, 0, 0.375f);
		gen_torus (&b, r, r * (0.125f / 0.375f), 32, 12);
		break;
	default:
		return NULL;
	}
	return builder_finish (&b);
}

LrgUvMesh *
lrg_uv_mesh_new_from_raylib (gconstpointer mesh)
{
	const Mesh *m = (const Mesh *)mesh;
	LrgUvMesh  *self;
	g_autofree guint32 *idx = NULL;
	gsize       n_indices;
	gsize       i;

	g_return_val_if_fail (mesh != NULL, NULL);

	if (m->vertices == NULL || m->vertexCount <= 0)
		return NULL;
	if (m->indices != NULL)
	{
		n_indices = (gsize)MAX (m->triangleCount, 0) * 3u;
		idx = g_new (guint32, MAX (n_indices, 1u));
		for (i = 0; i < n_indices; i++)
			idx[i] = (guint32)m->indices[i];
	}
	else
		n_indices = 0;

	self = lrg_uv_mesh_new (m->vertices, (gsize)m->vertexCount * 3u, idx, n_indices);
	if (self == NULL)
		return NULL;
	if (m->normals != NULL)
		lrg_uv_mesh_set_normals (self, m->normals, (gsize)m->vertexCount * 3u);
	if (m->texcoords != NULL)
		lrg_uv_mesh_set_uvs (self, m->texcoords, (gsize)m->vertexCount * 2u);
	if (m->texcoords2 != NULL)
		lrg_uv_mesh_set_uv2 (self, m->texcoords2, (gsize)m->vertexCount * 2u);
	return self;
}

/* MemAlloc'd copy of @n floats (UnloadMesh frees it with RL_FREE) */
static gfloat *
rl_floats (const gfloat *src,
           gsize         n)
{
	gfloat *dst = (gfloat *)MemAlloc ((guint)(n * sizeof (gfloat)));

	memcpy (dst, src, n * sizeof (gfloat));
	return dst;
}

void
lrg_uv_mesh_to_raylib (LrgUvMesh *self,
                       gpointer   out_mesh)
{
	Mesh   *m = (Mesh *)out_mesh;
	guint   t;
	gint    j;
	gsize   i;

	g_return_if_fail (LRG_IS_UV_MESH (self));
	g_return_if_fail (out_mesh != NULL);

	memset (m, 0, sizeof (Mesh));
	m->triangleCount = (gint)(self->n_indices / 3u);
	if (self->n_vertices <= 65535u)
	{
		/* indexed: the arrays as they are */
		m->vertexCount = (gint)self->n_vertices;
		m->vertices = rl_floats (self->positions, (gsize)self->n_vertices * 3u);
		if (self->normals != NULL)
			m->normals = rl_floats (self->normals, (gsize)self->n_vertices * 3u);
		if (self->uvs != NULL)
			m->texcoords = rl_floats (self->uvs, (gsize)self->n_vertices * 2u);
		if (self->uv2 != NULL)
			m->texcoords2 = rl_floats (self->uv2, (gsize)self->n_vertices * 2u);
		m->indices = (unsigned short *)MemAlloc ((guint)(MAX (self->n_indices, 1u) * sizeof (unsigned short)));
		for (i = 0; i < self->n_indices; i++)
			m->indices[i] = (unsigned short)self->indices[i];
		return;
	}

	/* too many vertices for 16-bit indices: one vertex per corner */
	m->vertexCount = (gint)self->n_indices;
	m->vertices = (gfloat *)MemAlloc ((guint)((gsize)self->n_indices * 3u * sizeof (gfloat)));
	if (self->normals != NULL)
		m->normals = (gfloat *)MemAlloc ((guint)((gsize)self->n_indices * 3u * sizeof (gfloat)));
	if (self->uvs != NULL)
		m->texcoords = (gfloat *)MemAlloc ((guint)((gsize)self->n_indices * 2u * sizeof (gfloat)));
	if (self->uv2 != NULL)
		m->texcoords2 = (gfloat *)MemAlloc ((guint)((gsize)self->n_indices * 2u * sizeof (gfloat)));
	for (t = 0; t < self->n_indices / 3u; t++)
	{
		for (j = 0; j < 3; j++)
		{
			gsize c = (gsize)t * 3u + (gsize)j;
			gsize v = self->indices[c];

			memcpy (m->vertices + c * 3u, self->positions + v * 3u, 3 * sizeof (gfloat));
			if (m->normals != NULL)
				memcpy (m->normals + c * 3u, self->normals + v * 3u, 3 * sizeof (gfloat));
			if (m->texcoords != NULL)
				memcpy (m->texcoords + c * 2u, self->uvs + v * 2u, 2 * sizeof (gfloat));
			if (m->texcoords2 != NULL)
				memcpy (m->texcoords2 + c * 2u, self->uv2 + v * 2u, 2 * sizeof (gfloat));
		}
	}
}

/* ==========================================================================
 * LrgUvUnwrap
 * ========================================================================== */

struct _LrgUvUnwrap
{
	GObject  parent_instance;

	guint    resolution;
	guint    padding;
	gfloat   max_chart_angle;

	/* statistics of the last run */
	guint    n_charts;
	guint32 *xrefs;
	guint    n_xrefs;
	guint32 *chart_ids;
	guint    n_chart_ids;
	gdouble  utilization;
};

G_DEFINE_FINAL_TYPE (LrgUvUnwrap, lrg_uv_unwrap, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_RESOLUTION,
	PROP_PADDING,
	PROP_MAX_CHART_ANGLE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_uv_unwrap_finalize (GObject *object)
{
	LrgUvUnwrap *self = LRG_UV_UNWRAP (object);

	g_free (self->xrefs);
	g_free (self->chart_ids);
	G_OBJECT_CLASS (lrg_uv_unwrap_parent_class)->finalize (object);
}

static void
lrg_uv_unwrap_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgUvUnwrap *self = LRG_UV_UNWRAP (object);

	switch (prop_id)
	{
	case PROP_RESOLUTION:
		g_value_set_uint (value, self->resolution);
		break;
	case PROP_PADDING:
		g_value_set_uint (value, self->padding);
		break;
	case PROP_MAX_CHART_ANGLE:
		g_value_set_float (value, self->max_chart_angle);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_uv_unwrap_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgUvUnwrap *self = LRG_UV_UNWRAP (object);

	switch (prop_id)
	{
	case PROP_RESOLUTION:
		lrg_uv_unwrap_set_resolution (self, g_value_get_uint (value));
		break;
	case PROP_PADDING:
		lrg_uv_unwrap_set_padding (self, g_value_get_uint (value));
		break;
	case PROP_MAX_CHART_ANGLE:
		lrg_uv_unwrap_set_max_chart_angle (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_uv_unwrap_class_init (LrgUvUnwrapClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_uv_unwrap_finalize;
	object_class->get_property = lrg_uv_unwrap_get_property;
	object_class->set_property = lrg_uv_unwrap_set_property;

	/**
	 * LrgUvUnwrap:resolution:
	 *
	 * The map size, in texels, #LrgUvUnwrap:padding is measured in.
	 */
	properties[PROP_RESOLUTION] =
		g_param_spec_uint ("resolution", "Resolution", "Map size the padding is measured in",
		                   8, 8192, LRG_UV_UNWRAP_DEFAULT_RESOLUTION,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgUvUnwrap:padding:
	 *
	 * Texels left free around every chart.
	 */
	properties[PROP_PADDING] =
		g_param_spec_uint ("padding", "Padding", "Texels free around every chart",
		                   0, 16, LRG_UV_UNWRAP_DEFAULT_PADDING,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgUvUnwrap:max-chart-angle:
	 *
	 * Largest angle, in degrees, between a triangle's normal and its
	 * chart's seed normal.
	 */
	properties[PROP_MAX_CHART_ANGLE] =
		g_param_spec_float ("max-chart-angle", "Max chart angle", "Normal deviation limit of a chart",
		                    1.0f, 85.0f, LRG_UV_UNWRAP_DEFAULT_MAX_CHART_ANGLE,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_uv_unwrap_init (LrgUvUnwrap *self)
{
	self->resolution = LRG_UV_UNWRAP_DEFAULT_RESOLUTION;
	self->padding = LRG_UV_UNWRAP_DEFAULT_PADDING;
	self->max_chart_angle = LRG_UV_UNWRAP_DEFAULT_MAX_CHART_ANGLE;
}

LrgUvUnwrap *
lrg_uv_unwrap_new (void)
{
	return g_object_new (LRG_TYPE_UV_UNWRAP, NULL);
}

guint
lrg_uv_unwrap_get_resolution (LrgUvUnwrap *self)
{
	g_return_val_if_fail (LRG_IS_UV_UNWRAP (self), 0);
	return self->resolution;
}

void
lrg_uv_unwrap_set_resolution (LrgUvUnwrap *self,
                              guint        resolution)
{
	g_return_if_fail (LRG_IS_UV_UNWRAP (self));

	resolution = CLAMP (resolution, 8u, 8192u);
	if (self->resolution == resolution)
		return;
	self->resolution = resolution;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESOLUTION]);
}

guint
lrg_uv_unwrap_get_padding (LrgUvUnwrap *self)
{
	g_return_val_if_fail (LRG_IS_UV_UNWRAP (self), 0);
	return self->padding;
}

void
lrg_uv_unwrap_set_padding (LrgUvUnwrap *self,
                           guint        padding)
{
	g_return_if_fail (LRG_IS_UV_UNWRAP (self));

	padding = MIN (padding, 16u);
	if (self->padding == padding)
		return;
	self->padding = padding;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PADDING]);
}

gfloat
lrg_uv_unwrap_get_max_chart_angle (LrgUvUnwrap *self)
{
	g_return_val_if_fail (LRG_IS_UV_UNWRAP (self), 0.0f);
	return self->max_chart_angle;
}

void
lrg_uv_unwrap_set_max_chart_angle (LrgUvUnwrap *self,
                                   gfloat       degrees)
{
	g_return_if_fail (LRG_IS_UV_UNWRAP (self));

	if (!isfinite (degrees))
		return;
	degrees = CLAMP (degrees, 1.0f, 85.0f);
	if (self->max_chart_angle == degrees)
		return;
	self->max_chart_angle = degrees;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_CHART_ANGLE]);
}

guint
lrg_uv_unwrap_get_n_charts (LrgUvUnwrap *self)
{
	g_return_val_if_fail (LRG_IS_UV_UNWRAP (self), 0);
	return self->n_charts;
}

const guint32 *
lrg_uv_unwrap_get_xrefs (LrgUvUnwrap *self,
                         guint       *n_vertices)
{
	g_return_val_if_fail (LRG_IS_UV_UNWRAP (self), NULL);
	if (n_vertices != NULL)
		*n_vertices = self->n_xrefs;
	return self->xrefs;
}

const guint32 *
lrg_uv_unwrap_get_chart_ids (LrgUvUnwrap *self,
                             guint       *n_triangles)
{
	g_return_val_if_fail (LRG_IS_UV_UNWRAP (self), NULL);
	if (n_triangles != NULL)
		*n_triangles = self->n_chart_ids;
	return self->chart_ids;
}

gdouble
lrg_uv_unwrap_get_utilization (LrgUvUnwrap *self)
{
	g_return_val_if_fail (LRG_IS_UV_UNWRAP (self), 0.0);
	return self->utilization;
}

/* --- skyline packer ---------------------------------------------------- */

typedef struct
{
	guint x;
	guint y;
	guint w;
} SkySeg;

/*
 * Finds the bottom-left position for a w x h rectangle on the skyline:
 * lowest resulting top, then leftmost.  Returns FALSE when it does not
 * fit the page.
 */
static gboolean
skyline_find (const GArray *sky,
              guint         page,
              guint         w,
              guint         h,
              guint        *out_i,
              guint        *out_x,
              guint        *out_y)
{
	guint best_top = G_MAXUINT;
	guint i;
	gboolean found = FALSE;

	for (i = 0; i < sky->len; i++)
	{
		const SkySeg *s = &g_array_index (sky, SkySeg, i);
		guint x = s->x;
		guint y = 0;
		guint covered = 0;
		guint j = i;

		if (x + w > page)
			break;
		/* the rectangle rests on the highest segment it spans */
		while (covered < w && j < sky->len)
		{
			const SkySeg *t = &g_array_index (sky, SkySeg, j);

			y = MAX (y, t->y);
			covered += t->w;
			j++;
		}
		if (covered < w || y + h > page)
			continue;
		if (y + h < best_top)
		{
			best_top = y + h;
			*out_i = i;
			*out_x = x;
			*out_y = y;
			found = TRUE;
		}
	}
	return found;
}

/* Raises the skyline under a placed rectangle. */
static void
skyline_add (GArray *sky,
             guint   i,
             guint   x,
             guint   y,
             guint   w,
             guint   h)
{
	SkySeg seg;
	guint  end = x + w;

	seg.x = x;
	seg.y = y + h;
	seg.w = w;
	g_array_insert_val (sky, i, seg);
	/* trim or drop the segments the new one covers */
	i++;
	while (i < sky->len)
	{
		SkySeg *s = &g_array_index (sky, SkySeg, i);

		if (s->x >= end)
			break;
		if (s->x + s->w <= end)
		{
			g_array_remove_index (sky, i);
			continue;
		}
		s->w = s->x + s->w - end;
		s->x = end;
		break;
	}
	/* merge neighbours of equal height */
	for (i = 0; i + 1u < sky->len; )
	{
		SkySeg *a = &g_array_index (sky, SkySeg, i);
		SkySeg *b = &g_array_index (sky, SkySeg, i + 1u);

		if (a->y == b->y)
		{
			a->w += b->w;
			g_array_remove_index (sky, i + 1u);
		}
		else
			i++;
	}
}

typedef struct
{
	guint w;
	guint h;
	guint index;
} PackRect;

static gint
pack_rect_cmp (gconstpointer a,
               gconstpointer b)
{
	const PackRect *ra = a;
	const PackRect *rb = b;

	if (ra->h != rb->h)
		return ra->h > rb->h ? -1 : 1;
	if (ra->w != rb->w)
		return ra->w > rb->w ? -1 : 1;
	return ra->index < rb->index ? -1 : (ra->index > rb->index ? 1 : 0);
}

gboolean
lrg_uv_unwrap_pack_rects (const guint *sizes,
                          gsize        n_sizes,
                          guint        page_size,
                          guint        max_pages,
                          guint       *out_placements,
                          guint       *out_n_pages)
{
	g_autofree PackRect *rects = NULL;
	g_autoptr(GPtrArray) skies = NULL;
	gsize                n = n_sizes / 2u;
	gsize                i;

	g_return_val_if_fail (sizes != NULL || n_sizes == 0, FALSE);
	g_return_val_if_fail (out_placements != NULL || n_sizes == 0, FALSE);
	g_return_val_if_fail (page_size > 0, FALSE);

	if (out_n_pages != NULL)
		*out_n_pages = 0;
	rects = g_new (PackRect, MAX (n, 1u));
	for (i = 0; i < n; i++)
	{
		rects[i].w = sizes[i * 2u];
		rects[i].h = sizes[i * 2u + 1u];
		rects[i].index = (guint)i;
		if (rects[i].w > page_size || rects[i].h > page_size)
			return FALSE;
	}
	qsort (rects, n, sizeof (PackRect), pack_rect_cmp);

	/* one skyline per page; a rectangle goes to the first page it fits */
	skies = g_ptr_array_new_with_free_func ((GDestroyNotify)g_array_unref);
	for (i = 0; i < n; i++)
	{
		guint p;
		gboolean placed = FALSE;

		for (p = 0; !placed; p++)
		{
			GArray *sky;
			guint   si = 0;
			guint   x = 0;
			guint   y = 0;

			if (p == skies->len)
			{
				SkySeg base;

				if (max_pages > 0 && p >= max_pages)
					return FALSE;
				sky = g_array_new (FALSE, FALSE, sizeof (SkySeg));
				base.x = 0;
				base.y = 0;
				base.w = page_size;
				g_array_append_val (sky, base);
				g_ptr_array_add (skies, sky);
			}
			sky = g_ptr_array_index (skies, p);
			if (rects[i].w == 0 || rects[i].h == 0)
			{
				x = y = 0;
				placed = TRUE;
			}
			else if (skyline_find (sky, page_size, rects[i].w, rects[i].h, &si, &x, &y))
			{
				skyline_add (sky, si, x, y, rects[i].w, rects[i].h);
				placed = TRUE;
			}
			if (placed)
			{
				out_placements[rects[i].index * 3u] = x;
				out_placements[rects[i].index * 3u + 1u] = y;
				out_placements[rects[i].index * 3u + 2u] = p;
			}
		}
	}
	if (out_n_pages != NULL)
		*out_n_pages = skies->len;
	return TRUE;
}

/* --- the unwrap -------------------------------------------------------- */

/* One chart under construction / packed. */
typedef struct
{
	GArray *tris;        /* guint triangle indices in insertion order */
	gfloat  normal[3];   /* the seed normal (projection axis) */
	gfloat  axis_u[3];
	gfloat  axis_v[3];
	gfloat  rot_c;       /* rotation to the minimum-area rectangle */
	gfloat  rot_s;
	gfloat  min_u;       /* rotated bounds (world units) */
	gfloat  min_v;
	gfloat  max_u;
	gfloat  max_v;
	guint   px;          /* packed rectangle (texels) */
	guint   py;
} Chart;

/* The working state of one run. */
typedef struct
{
	const gfloat  *pos;
	const guint32 *idx;
	guint          n_tris;
	guint32       *canon;      /* welded id per input vertex */
	gfloat        *fn;         /* unit face normal per triangle */
	gfloat        *area;       /* triangle area */
	gint32        *nbr;        /* neighbour across each edge, -1 none */
	gint32        *chart_of;   /* chart per triangle, -1 unassigned */
	GPtrArray     *charts;     /* Chart * */
} Unwrap;

static void
chart_free (gpointer data)
{
	Chart *c = data;

	g_array_unref (c->tris);
	g_free (c);
}

/* position welding: quantized coordinates -> first vertex with them */
static void
weld (Unwrap *u,
      guint   n_vertices)
{
	g_autoptr(GHashTable) map = g_hash_table_new_full (g_bytes_hash, g_bytes_equal,
	                                                   (GDestroyNotify)g_bytes_unref, NULL);
	gfloat bmin[3] = { G_MAXFLOAT, G_MAXFLOAT, G_MAXFLOAT };
	gfloat bmax[3] = { -G_MAXFLOAT, -G_MAXFLOAT, -G_MAXFLOAT };
	gfloat cell;
	guint  v;
	gint   k;

	for (v = 0; v < n_vertices; v++)
	{
		for (k = 0; k < 3; k++)
		{
			bmin[k] = MIN (bmin[k], u->pos[v * 3u + (guint)k]);
			bmax[k] = MAX (bmax[k], u->pos[v * 3u + (guint)k]);
		}
	}
	cell = MAX (MAX (bmax[0] - bmin[0], bmax[1] - bmin[1]), bmax[2] - bmin[2]) * 1e-6f;
	cell = MAX (cell, 1e-12f);

	u->canon = g_new (guint32, MAX (n_vertices, 1u));
	for (v = 0; v < n_vertices; v++)
	{
		gint64   q[3];
		GBytes  *key;
		gpointer found;

		for (k = 0; k < 3; k++)
			q[k] = (gint64)floor ((gdouble)(u->pos[v * 3u + (guint)k] - bmin[k]) / cell + 0.5);
		key = g_bytes_new (q, sizeof q);
		if (g_hash_table_lookup_extended (map, key, NULL, &found))
		{
			u->canon[v] = GPOINTER_TO_UINT (found);
			g_bytes_unref (key);
		}
		else
		{
			u->canon[v] = v;
			g_hash_table_insert (map, key, GUINT_TO_POINTER (v));
		}
	}
}

/* manifold edge adjacency through the welded ids */
static void
build_adjacency (Unwrap *u)
{
	g_autoptr(GHashTable) edges = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
	guint t;
	gint  e;

	u->nbr = g_new (gint32, (gsize)MAX (u->n_tris, 1u) * 3u);
	for (t = 0; t < u->n_tris * 3u; t++)
		u->nbr[t] = -1;

	/* value: (first triangle * 3 + edge) + 1, or 0 once non-manifold */
	for (t = 0; t < u->n_tris; t++)
	{
		for (e = 0; e < 3; e++)
		{
			guint32  a = u->canon[u->idx[t * 3u + (guint)e]];
			guint32  b = u->canon[u->idx[t * 3u + (guint)((e + 1) % 3)]];
			gint64   key;
			gpointer found;

			if (a == b)
				continue;
			key = ((gint64)MIN (a, b) << 32) | (gint64)MAX (a, b);
			if (g_hash_table_lookup_extended (edges, &key, NULL, &found))
			{
				guint packed = GPOINTER_TO_UINT (found);

				if (packed != 0)
				{
					guint other = (packed - 1u) / 3u;
					guint oe = (packed - 1u) % 3u;

					if (u->nbr[other * 3u + oe] == -1 && other != t)
					{
						u->nbr[other * 3u + oe] = (gint32)t;
						u->nbr[t * 3u + (guint)e] = (gint32)other;
					}
					else
					{
						/* a third triangle: the edge is a boundary for all */
						if (u->nbr[other * 3u + oe] >= 0)
						{
							gint32 third = u->nbr[other * 3u + oe];
							gint   k;

							for (k = 0; k < 3; k++)
								if (u->nbr[(guint)third * 3u + (guint)k] == (gint32)other)
									u->nbr[(guint)third * 3u + (guint)k] = -1;
						}
						u->nbr[other * 3u + oe] = -1;
						g_hash_table_replace (edges, g_memdup2 (&key, sizeof key), GUINT_TO_POINTER (0u));
					}
				}
			}
			else
				g_hash_table_insert (edges, g_memdup2 (&key, sizeof key),
				                     GUINT_TO_POINTER (t * 3u + (guint)e + 1u));
		}
	}
}

/* projected 2D corners of triangle @t in @c's plane */
static void
project_tri (const Unwrap *u,
             const Chart  *c,
             guint         t,
             gfloat       *out)
{
	gint j;

	for (j = 0; j < 3; j++)
	{
		const gfloat *p = u->pos + (gsize)u->idx[t * 3u + (guint)j] * 3u;

		out[j * 2] = v3_dot (p, c->axis_u);
		out[j * 2 + 1] = v3_dot (p, c->axis_v);
	}
}

/*
 * Whether two counter-clockwise 2D triangles overlap with positive area
 * (separating-axis test over the six edge normals; touching along an
 * edge or at a corner is not an overlap).
 */
static gboolean
tris_overlap (const gfloat *a,
              const gfloat *b,
              gfloat        eps)
{
	const gfloat *tri[2];
	gint          s;

	tri[0] = a;
	tri[1] = b;
	for (s = 0; s < 2; s++)
	{
		const gfloat *p = tri[s];
		const gfloat *q = tri[1 - s];
		gint          e;

		for (e = 0; e < 3; e++)
		{
			gfloat x0 = p[e * 2];
			gfloat y0 = p[e * 2 + 1];
			gfloat dx = p[((e + 1) % 3) * 2] - x0;
			gfloat dy = p[((e + 1) % 3) * 2 + 1] - y0;
			gfloat len = sqrtf (dx * dx + dy * dy);
			gfloat nx;
			gfloat ny;
			gfloat mn = G_MAXFLOAT;
			gint   k;

			if (len <= 1e-30f)
				continue;
			/* outward normal of a CCW edge */
			nx = dy / len;
			ny = -dx / len;
			for (k = 0; k < 3; k++)
				mn = MIN (mn, (q[k * 2] - x0) * nx + (q[k * 2 + 1] - y0) * ny);
			if (mn >= -eps)
				return FALSE;
		}
	}
	return TRUE;
}

/* --- 2D spatial grid of a chart's projected triangles ------------------- */

typedef struct
{
	GHashTable *cells;   /* gint64 cell key -> GArray of guint triangles */
	gfloat      size;
	GArray     *big;     /* triangles covering too many cells */
} Grid2D;

static void
grid_init (Grid2D *g,
           gfloat  size)
{
	g->cells = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, (GDestroyNotify)g_array_unref);
	g->size = MAX (size, 1e-12f);
	g->big = g_array_new (FALSE, FALSE, sizeof (guint));
}

static void
grid_clear (Grid2D *g)
{
	g_hash_table_unref (g->cells);
	g_array_unref (g->big);
}

static void
tri_cells (const Grid2D *g,
           const gfloat *p,
           gint64       *x0,
           gint64       *y0,
           gint64       *x1,
           gint64       *y1)
{
	gfloat mnx = MIN (p[0], MIN (p[2], p[4]));
	gfloat mny = MIN (p[1], MIN (p[3], p[5]));
	gfloat mxx = MAX (p[0], MAX (p[2], p[4]));
	gfloat mxy = MAX (p[1], MAX (p[3], p[5]));

	*x0 = (gint64)floorf (mnx / g->size);
	*y0 = (gint64)floorf (mny / g->size);
	*x1 = (gint64)floorf (mxx / g->size);
	*y1 = (gint64)floorf (mxy / g->size);
}

static void
grid_insert (Grid2D       *g,
             guint         t,
             const gfloat *p)
{
	gint64 x0, y0, x1, y1, x, y;

	tri_cells (g, p, &x0, &y0, &x1, &y1);
	if ((x1 - x0 + 1) * (y1 - y0 + 1) > 256)
	{
		g_array_append_val (g->big, t);
		return;
	}
	for (y = y0; y <= y1; y++)
	{
		for (x = x0; x <= x1; x++)
		{
			gint64  key = (x << 32) ^ (y & 0xffffffff);
			GArray *cell = g_hash_table_lookup (g->cells, &key);

			if (cell == NULL)
			{
				cell = g_array_new (FALSE, FALSE, sizeof (guint));
				g_hash_table_insert (g->cells, g_memdup2 (&key, sizeof key), cell);
			}
			g_array_append_val (cell, t);
		}
	}
}

/* whether projected triangle @p overlaps any triangle in the grid */
static gboolean
grid_overlaps (const Grid2D *g,
               const Unwrap *u,
               const Chart  *c,
               const gfloat *p,
               gfloat        eps)
{
	gint64 x0, y0, x1, y1, x, y;
	gfloat q[6];
	guint  i;

	for (i = 0; i < g->big->len; i++)
	{
		project_tri (u, c, g_array_index (g->big, guint, i), q);
		if (tris_overlap (p, q, eps))
			return TRUE;
	}
	tri_cells (g, p, &x0, &y0, &x1, &y1);
	if ((x1 - x0 + 1) * (y1 - y0 + 1) > 256)
	{
		/* a huge candidate: test every triangle of the chart */
		for (i = 0; i < c->tris->len; i++)
		{
			project_tri (u, c, g_array_index (c->tris, guint, i), q);
			if (tris_overlap (p, q, eps))
				return TRUE;
		}
		return FALSE;
	}
	for (y = y0; y <= y1; y++)
	{
		for (x = x0; x <= x1; x++)
		{
			gint64  key = (x << 32) ^ (y & 0xffffffff);
			GArray *cell = g_hash_table_lookup (g->cells, &key);

			if (cell == NULL)
				continue;
			for (i = 0; i < cell->len; i++)
			{
				project_tri (u, c, g_array_index (cell, guint, i), q);
				if (tris_overlap (p, q, eps))
					return TRUE;
			}
		}
	}
	return FALSE;
}

/* seed order: larger triangles first, index breaks ties */
typedef struct
{
	gfloat area;
	guint  t;
} SeedRec;

static gint
seed_cmp (gconstpointer a,
          gconstpointer b)
{
	const SeedRec *sa = a;
	const SeedRec *sb = b;

	if (sa->area != sb->area)
		return sa->area > sb->area ? -1 : 1;
	return sa->t < sb->t ? -1 : (sa->t > sb->t ? 1 : 0);
}

/* Grows every chart (step 3). */
static void
grow_charts (Unwrap *u,
             gfloat  max_angle_deg)
{
	g_autofree SeedRec *seeds = g_new (SeedRec, MAX (u->n_tris, 1u));
	g_autoptr(GArray)   queue = g_array_new (FALSE, FALSE, sizeof (guint));
	gfloat              cos_max = cosf (max_angle_deg * (gfloat)G_PI / 180.0f);
	guint               i;

	for (i = 0; i < u->n_tris; i++)
	{
		seeds[i].area = u->area[i];
		seeds[i].t = i;
	}
	qsort (seeds, u->n_tris, sizeof (SeedRec), seed_cmp);

	for (i = 0; i < u->n_tris; i++)
	{
		guint   seed = seeds[i].t;
		Chart  *c;
		Grid2D  grid;
		gfloat  p[6];
		gfloat  up[3];
		gfloat  edge;
		gfloat  eps;
		guint   head = 0;
		gint32  ci;

		if (u->chart_of[seed] >= 0)
			continue;

		/* a new chart projecting onto the seed's plane */
		c = g_new0 (Chart, 1);
		c->tris = g_array_new (FALSE, FALSE, sizeof (guint));
		memcpy (c->normal, u->fn + (gsize)seed * 3u, sizeof c->normal);
		if (fabsf (c->normal[1]) < 0.99f)
		{
			up[0] = 0.0f;
			up[1] = 1.0f;
			up[2] = 0.0f;
		}
		else
		{
			up[0] = 1.0f;
			up[1] = 0.0f;
			up[2] = 0.0f;
		}
		/* u x v = normal keeps counter-clockwise triangles counter-clockwise */
		v3_cross (up, c->normal, c->axis_u);
		v3_normalize (c->axis_u);
		v3_cross (c->normal, c->axis_u, c->axis_v);
		ci = (gint32)u->charts->len;
		g_ptr_array_add (u->charts, c);

		edge = sqrtf (MAX (u->area[seed], 1e-20f) * 2.0f);
		eps = edge * 1e-4f;
		grid_init (&grid, edge * 2.0f);

		g_array_set_size (queue, 0);
		g_array_append_val (queue, seed);
		u->chart_of[seed] = ci;
		g_array_append_val (c->tris, seed);
		project_tri (u, c, seed, p);
		grid_insert (&grid, seed, p);

		/* breadth-first across manifold edges */
		while (head < queue->len)
		{
			guint t = g_array_index (queue, guint, head++);
			gint  e;

			for (e = 0; e < 3; e++)
			{
				gint32 nb = u->nbr[t * 3u + (guint)e];
				guint  n;

				if (nb < 0 || u->chart_of[nb] >= 0)
					continue;
				n = (guint)nb;
				if (u->area[n] > 0.0f && v3_dot (u->fn + (gsize)n * 3u, c->normal) < cos_max)
					continue;
				project_tri (u, c, n, p);
				if (u->area[n] > 0.0f && grid_overlaps (&grid, u, c, p, eps))
					continue;
				u->chart_of[n] = ci;
				g_array_append_val (c->tris, n);
				g_array_append_val (queue, n);
				grid_insert (&grid, n, p);
			}
		}
		grid_clear (&grid);
	}
}

/* --- minimum-area rotation (step 4) ------------------------------------ */

static gint
pt_cmp (gconstpointer a,
        gconstpointer b)
{
	const gfloat *pa = a;
	const gfloat *pb = b;

	if (pa[0] != pb[0])
		return pa[0] < pb[0] ? -1 : 1;
	if (pa[1] != pb[1])
		return pa[1] < pb[1] ? -1 : 1;
	return 0;
}

static gfloat
cross2 (const gfloat *o,
        const gfloat *a,
        const gfloat *b)
{
	return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

/* Picks the rotation and bounds of @c's minimum-area rectangle. */
static void
fit_chart (Unwrap *u,
           Chart  *c)
{
	g_autofree gfloat *pts = g_new (gfloat, (gsize)c->tris->len * 6u);
	g_autofree gfloat *hull = NULL;
	guint  n = c->tris->len * 3u;
	guint  h = 0;
	guint  i;
	gfloat best_area = G_MAXFLOAT;
	gfloat best_c = 1.0f;
	gfloat best_s = 0.0f;

	for (i = 0; i < c->tris->len; i++)
		project_tri (u, c, g_array_index (c->tris, guint, i), pts + (gsize)i * 6u);
	qsort (pts, n, 2 * sizeof (gfloat), pt_cmp);

	/* Andrew's monotone chain */
	hull = g_new (gfloat, (gsize)n * 4u + 4u);
	for (i = 0; i < n; i++)
	{
		while (h >= 2 && cross2 (hull + (h - 2) * 2, hull + (h - 1) * 2, pts + i * 2) <= 0.0f)
			h--;
		hull[h * 2] = pts[i * 2];
		hull[h * 2 + 1] = pts[i * 2 + 1];
		h++;
	}
	{
		guint lower = h + 1;

		for (i = n - 1; i-- > 0; )
		{
			while (h >= lower && cross2 (hull + (h - 2) * 2, hull + (h - 1) * 2, pts + i * 2) <= 0.0f)
				h--;
			hull[h * 2] = pts[i * 2];
			hull[h * 2 + 1] = pts[i * 2 + 1];
			h++;
		}
	}
	if (h > 1)
		h--;

	/* rotating calipers: try every hull edge as the rectangle's side */
	for (i = 0; i < MAX (h, 1u); i++)
	{
		const gfloat *a = hull + i * 2;
		const gfloat *b = hull + ((i + 1) % MAX (h, 1u)) * 2;
		gfloat dx = b[0] - a[0];
		gfloat dy = b[1] - a[1];
		gfloat len = sqrtf (dx * dx + dy * dy);
		gfloat cs = 1.0f;
		gfloat sn = 0.0f;
		gfloat mnu = G_MAXFLOAT, mnv = G_MAXFLOAT, mxu = -G_MAXFLOAT, mxv = -G_MAXFLOAT;
		gfloat area;
		guint  k;

		if (len > 1e-30f)
		{
			cs = dx / len;
			sn = dy / len;
		}
		for (k = 0; k < h; k++)
		{
			/* rotate by -angle so the edge lies on +X */
			gfloat x = hull[k * 2] * cs + hull[k * 2 + 1] * sn;
			gfloat y = -hull[k * 2] * sn + hull[k * 2 + 1] * cs;

			mnu = MIN (mnu, x);
			mxu = MAX (mxu, x);
			mnv = MIN (mnv, y);
			mxv = MAX (mxv, y);
		}
		area = (mxu - mnu) * (mxv - mnv);
		/* strict improvement keeps the first edge on ties (deterministic) */
		if (area < best_area * (1.0f - 1e-6f))
		{
			best_area = area;
			best_c = cs;
			best_s = sn;
			c->min_u = mnu;
			c->max_u = mxu;
			c->min_v = mnv;
			c->max_v = mxv;
		}
	}
	if (best_area == G_MAXFLOAT)
	{
		c->min_u = c->max_u = c->min_v = c->max_v = 0.0f;
		best_c = 1.0f;
		best_s = 0.0f;
	}
	/* wide side along X: turn a further 90 degrees when taller */
	if (c->max_v - c->min_v > c->max_u - c->min_u)
	{
		gfloat mnu = c->min_v;
		gfloat mxu = c->max_v;
		gfloat mnv = -c->max_u;
		gfloat mxv = -c->min_u;
		gfloat cs = -best_s;
		gfloat sn = best_c;

		/* rotating (x, y) to (y, -x) composes as angle + 90 degrees */
		best_c = cs;
		best_s = sn;
		c->min_u = mnu;
		c->max_u = mxu;
		c->min_v = mnv;
		c->max_v = mxv;
	}
	c->rot_c = best_c;
	c->rot_s = best_s;
}

/* chart rectangle size in texels at @scale texels per unit */
static void
chart_texels (const Chart *c,
              gdouble      scale,
              guint        pad,
              guint       *w,
              guint       *h)
{
	*w = (guint)ceil ((gdouble)(c->max_u - c->min_u) * scale - 1e-6) + 2u * pad;
	*h = (guint)ceil ((gdouble)(c->max_v - c->min_v) * scale - 1e-6) + 2u * pad;
	*w = MAX (*w, 1u + 2u * pad);
	*h = MAX (*h, 1u + 2u * pad);
}

static gboolean
try_pack (Unwrap  *u,
          gdouble  scale,
          guint    res,
          guint    pad,
          guint   *sizes,
          guint   *place)
{
	guint i;

	for (i = 0; i < u->charts->len; i++)
		chart_texels (g_ptr_array_index (u->charts, i), scale, pad, &sizes[i * 2u], &sizes[i * 2u + 1u]);
	return lrg_uv_unwrap_pack_rects (sizes, (gsize)u->charts->len * 2u, res, 1, place, NULL);
}

LrgUvMesh *
lrg_uv_unwrap_run (LrgUvUnwrap  *self,
                   LrgUvMesh    *mesh,
                   GError      **error)
{
	Unwrap              u;
	g_autofree guint   *sizes = NULL;
	g_autofree guint   *place = NULL;
	g_autoptr(GHashTable) vmap = NULL;
	GArray             *xref;
	GArray             *uv2;
	guint32            *out_idx;
	LrgUvMesh          *out;
	gdouble             lo;
	gdouble             hi;
	gdouble             max_extent = 0.0;
	guint               res;
	guint               pad;
	guint               i;
	gint                it;

	g_return_val_if_fail (LRG_IS_UV_UNWRAP (self), NULL);
	g_return_val_if_fail (LRG_IS_UV_MESH (mesh), NULL);

	/* reset the statistics */
	g_clear_pointer (&self->xrefs, g_free);
	g_clear_pointer (&self->chart_ids, g_free);
	self->n_xrefs = 0;
	self->n_chart_ids = 0;
	self->n_charts = 0;
	self->utilization = 0.0;

	if (mesh->n_indices < 3u)
	{
		g_set_error (error, LRG_UV_UNWRAP_ERROR, LRG_UV_UNWRAP_ERROR_EMPTY, "the mesh has no triangles");
		return NULL;
	}
	for (i = 0; i < mesh->n_vertices * 3u; i++)
	{
		if (!isfinite (mesh->positions[i]))
		{
			g_set_error (error, LRG_UV_UNWRAP_ERROR, LRG_UV_UNWRAP_ERROR_INVALID,
			             "vertex %u is not finite", i / 3u);
			return NULL;
		}
	}

	/* steps 1 and 2 */
	memset (&u, 0, sizeof u);
	u.pos = mesh->positions;
	u.idx = mesh->indices;
	u.n_tris = mesh->n_indices / 3u;
	weld (&u, mesh->n_vertices);
	u.fn = g_new (gfloat, (gsize)u.n_tris * 3u);
	u.area = g_new (gfloat, u.n_tris);
	u.chart_of = g_new (gint32, u.n_tris);
	for (i = 0; i < u.n_tris; i++)
	{
		gfloat *n = u.fn + (gsize)i * 3u;

		face_normal (u.pos, u.idx, i, n);
		u.area[i] = 0.5f * v3_normalize (n);
		if (u.area[i] <= 1e-20f)
		{
			u.area[i] = 0.0f;
			n[0] = 0.0f;
			n[1] = 1.0f;
			n[2] = 0.0f;
		}
		u.chart_of[i] = -1;
	}
	build_adjacency (&u);

	/* steps 3 and 4 */
	u.charts = g_ptr_array_new_with_free_func (chart_free);
	grow_charts (&u, self->max_chart_angle);
	for (i = 0; i < u.charts->len; i++)
	{
		Chart *c = g_ptr_array_index (u.charts, i);

		fit_chart (&u, c);
		max_extent = MAX (max_extent, (gdouble)MAX (c->max_u - c->min_u, c->max_v - c->min_v));
	}

	/* step 5: the largest scale that packs, by bisection */
	res = self->resolution;
	pad = self->padding;
	sizes = g_new (guint, (gsize)u.charts->len * 2u);
	place = g_new (guint, (gsize)u.charts->len * 3u);
	lo = 0.0;
	hi = max_extent > 0.0 ? (gdouble)(res - MIN (2u * pad, res - 1u)) / max_extent : 1.0;
	if (!try_pack (&u, lo, res, pad, sizes, place))
	{
		g_set_error (error, LRG_UV_UNWRAP_ERROR, LRG_UV_UNWRAP_ERROR_NO_FIT,
		             "%u charts do not fit a %u texel map with %u texels of padding",
		             u.charts->len, res, pad);
		goto fail;
	}
	if (try_pack (&u, hi, res, pad, sizes, place))
		lo = hi;
	else
	{
		for (it = 0; it < 24; it++)
		{
			gdouble mid = 0.5 * (lo + hi);

			if (try_pack (&u, mid, res, pad, sizes, place))
				lo = mid;
			else
				hi = mid;
		}
	}
	(void)try_pack (&u, lo, res, pad, sizes, place);
	for (i = 0; i < u.charts->len; i++)
	{
		Chart *c = g_ptr_array_index (u.charts, i);

		c->px = place[i * 3u];
		c->py = place[i * 3u + 1u];
	}

	/* step 6: one output vertex per (chart, input vertex) */
	vmap = g_hash_table_new (g_int64_hash, g_int64_equal);
	{
		g_autofree gint64 *keys = g_new (gint64, mesh->n_indices);

		xref = g_array_new (FALSE, FALSE, sizeof (guint32));
		uv2 = g_array_new (FALSE, FALSE, sizeof (gfloat));
		out_idx = g_new (guint32, mesh->n_indices);
		for (i = 0; i < mesh->n_indices; i++)
		{
			guint    t = i / 3u;
			guint32  v = mesh->indices[i];
			Chart   *c = g_ptr_array_index (u.charts, (guint)u.chart_of[t]);
			gpointer found;

			keys[i] = ((gint64)u.chart_of[t] << 32) | (gint64)v;
			if (g_hash_table_lookup_extended (vmap, &keys[i], NULL, &found))
				out_idx[i] = GPOINTER_TO_UINT (found);
			else
			{
				const gfloat *p = mesh->positions + (gsize)v * 3u;
				gfloat pu = v3_dot (p, c->axis_u);
				gfloat pv = v3_dot (p, c->axis_v);
				gfloat ru = pu * c->rot_c + pv * c->rot_s;
				gfloat rv = -pu * c->rot_s + pv * c->rot_c;
				gfloat t2[2];

				t2[0] = (gfloat)(((gdouble)c->px + pad + (gdouble)(ru - c->min_u) * lo) / res);
				t2[1] = (gfloat)(((gdouble)c->py + pad + (gdouble)(rv - c->min_v) * lo) / res);
				out_idx[i] = xref->len;
				g_hash_table_insert (vmap, &keys[i], GUINT_TO_POINTER (xref->len));
				g_array_append_val (xref, v);
				g_array_append_vals (uv2, t2, 2);
			}
		}

		/* the new mesh: attributes copied through the cross-references */
		{
			guint    nv = xref->len;
			gfloat  *pos = g_new (gfloat, (gsize)MAX (nv, 1u) * 3u);
			guint32 *xr = (guint32 *)xref->data;
			guint    k;

			for (k = 0; k < nv; k++)
				memcpy (pos + (gsize)k * 3u, mesh->positions + (gsize)xr[k] * 3u, 3 * sizeof (gfloat));
			out = uv_mesh_adopt (pos, nv, out_idx, mesh->n_indices);
			if (mesh->normals != NULL)
			{
				out->normals = g_new (gfloat, (gsize)nv * 3u);
				for (k = 0; k < nv; k++)
					memcpy (out->normals + (gsize)k * 3u, mesh->normals + (gsize)xr[k] * 3u,
					        3 * sizeof (gfloat));
			}
			if (mesh->uvs != NULL)
			{
				out->uvs = g_new (gfloat, (gsize)nv * 2u);
				for (k = 0; k < nv; k++)
					memcpy (out->uvs + (gsize)k * 2u, mesh->uvs + (gsize)xr[k] * 2u, 2 * sizeof (gfloat));
			}
			out->uv2 = (gfloat *)(gpointer)g_array_free (uv2, FALSE);
			self->n_xrefs = nv;
			self->xrefs = (guint32 *)(gpointer)g_array_free (xref, FALSE);
		}
		/* the table points into keys[]: drop it before keys goes */
		g_hash_table_remove_all (vmap);
	}

	self->n_charts = u.charts->len;
	self->n_chart_ids = u.n_tris;
	self->chart_ids = g_new (guint32, MAX (u.n_tris, 1u));
	for (i = 0; i < u.n_tris; i++)
		self->chart_ids[i] = (guint32)u.chart_of[i];
	self->utilization = lrg_uv_mesh_get_uv2_area (out);

	g_free (u.canon);
	g_free (u.fn);
	g_free (u.area);
	g_free (u.nbr);
	g_free (u.chart_of);
	g_ptr_array_unref (u.charts);
	return out;

fail:
	g_free (u.canon);
	g_free (u.fn);
	g_free (u.area);
	g_free (u.nbr);
	g_free (u.chart_of);
	g_ptr_array_unref (u.charts);
	return NULL;
}

LrgUvMesh *
lrg_uv_unwrap_new_lightmap_primitive (LrgPrimitiveType  primitive,
                                      const gfloat     *params)
{
	g_autoptr(LrgUvMesh)   mesh = lrg_uv_mesh_new_primitive (primitive, params);
	g_autoptr(LrgUvUnwrap) unwrap = NULL;

	if (mesh == NULL)
		return NULL;
	unwrap = lrg_uv_unwrap_new ();
	return lrg_uv_unwrap_run (unwrap, mesh, NULL);
}
