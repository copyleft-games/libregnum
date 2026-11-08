/* lrg-physics-shape.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe:
 * the active-edge rule (ActiveEdges::IsEdgeActive) and the supporting-face
 * conventions of the convex shapes.
 *
 * LrgPhysicsShape: immutable collision shapes for the 3D dynamics world.
 * See lrg-physics-shape.h for the public contract and
 * lrg-physics-shape-private.h for the layout shared with lrg-collide.c.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "lrg-physics-shape.h"
#include "lrg-physics-shape-private.h"
#include "../core/lrg-math3d.h"

/* ==========================================================================
 * Small vector helpers (gfloat)
 * ========================================================================== */

static inline void
v3_set (gfloat *o, gfloat x, gfloat y, gfloat z)
{
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

static inline void
v3_copy (gfloat *o, const gfloat *a)
{
	o[0] = a[0];
	o[1] = a[1];
	o[2] = a[2];
}

static inline void
v3_sub (gfloat *o, const gfloat *a, const gfloat *b)
{
	o[0] = a[0] - b[0];
	o[1] = a[1] - b[1];
	o[2] = a[2] - b[2];
}

static inline void
v3_add (gfloat *o, const gfloat *a, const gfloat *b)
{
	o[0] = a[0] + b[0];
	o[1] = a[1] + b[1];
	o[2] = a[2] + b[2];
}

static inline void
v3_madd (gfloat *o, const gfloat *a, const gfloat *b, gfloat s)
{
	o[0] = a[0] + b[0] * s;
	o[1] = a[1] + b[1] * s;
	o[2] = a[2] + b[2] * s;
}

static inline gfloat
v3_dot (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
v3_cross (gfloat *o, const gfloat *a, const gfloat *b)
{
	gfloat x;
	gfloat y;
	gfloat z;

	x = a[1] * b[2] - a[2] * b[1];
	y = a[2] * b[0] - a[0] * b[2];
	z = a[0] * b[1] - a[1] * b[0];
	o[0] = x;
	o[1] = y;
	o[2] = z;
}

static inline gfloat
v3_len (const gfloat *a)
{
	return sqrtf (v3_dot (a, a));
}

/* Normalises @a into @o; a (near) zero vector becomes @fallback. */
static inline gfloat
v3_normalize (gfloat *o, const gfloat *a, const gfloat *fallback)
{
	gfloat l;

	l = v3_len (a);
	if (l < 1e-20f)
	{
		v3_copy (o, fallback);
		return 0.0f;
	}
	o[0] = a[0] / l;
	o[1] = a[1] / l;
	o[2] = a[2] / l;
	return l;
}

static inline gboolean
shape_finite (gfloat v)
{
	return isfinite (v) ? TRUE : FALSE;
}

static const gfloat axis_y[3] = { 0.0f, 1.0f, 0.0f };

/* ==========================================================================
 * Enums, errors, LrgRayHit
 * ========================================================================== */

GType
lrg_physics_shape_kind_get_type (void)
{
	static gsize type_id = 0;
	static const GEnumValue values[] = {
		{ LRG_PHYSICS_SHAPE_KIND_SPHERE, "LRG_PHYSICS_SHAPE_KIND_SPHERE", "sphere" },
		{ LRG_PHYSICS_SHAPE_KIND_CAPSULE, "LRG_PHYSICS_SHAPE_KIND_CAPSULE", "capsule" },
		{ LRG_PHYSICS_SHAPE_KIND_CYLINDER, "LRG_PHYSICS_SHAPE_KIND_CYLINDER", "cylinder" },
		{ LRG_PHYSICS_SHAPE_KIND_BOX, "LRG_PHYSICS_SHAPE_KIND_BOX", "box" },
		{ LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL, "LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL", "convex-hull" },
		{ LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH, "LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH", "triangle-mesh" },
		{ LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD, "LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD", "heightfield" },
		{ LRG_PHYSICS_SHAPE_KIND_COMPOUND, "LRG_PHYSICS_SHAPE_KIND_COMPOUND", "compound" },
		{ LRG_PHYSICS_SHAPE_KIND_PLANE, "LRG_PHYSICS_SHAPE_KIND_PLANE", "plane" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type_id))
	{
		GType t;

		t = g_enum_register_static (g_intern_static_string ("LrgPhysicsShapeKind"), values);
		g_once_init_leave (&type_id, t);
	}
	return (GType)type_id;
}

GType
lrg_physics_shape_error_get_type (void)
{
	static gsize type_id = 0;
	static const GEnumValue values[] = {
		{ LRG_PHYSICS_SHAPE_ERROR_INVALID, "LRG_PHYSICS_SHAPE_ERROR_INVALID", "invalid" },
		{ LRG_PHYSICS_SHAPE_ERROR_DEGENERATE, "LRG_PHYSICS_SHAPE_ERROR_DEGENERATE", "degenerate" },
		{ 0, NULL, NULL }
	};

	if (g_once_init_enter (&type_id))
	{
		GType t;

		t = g_enum_register_static (g_intern_static_string ("LrgPhysicsShapeError"), values);
		g_once_init_leave (&type_id, t);
	}
	return (GType)type_id;
}

G_DEFINE_QUARK (lrg-physics-shape-error-quark, lrg_physics_shape_error)

G_DEFINE_BOXED_TYPE (LrgRayHit, lrg_ray_hit, lrg_ray_hit_copy, lrg_ray_hit_free)

LrgRayHit *
lrg_ray_hit_new (void)
{
	LrgRayHit *hit;

	hit = g_new0 (LrgRayHit, 1);
	lrg_ray_hit_reset (hit);
	return hit;
}

LrgRayHit *
lrg_ray_hit_copy (const LrgRayHit *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return (LrgRayHit *)g_memdup2 (self, sizeof (LrgRayHit));
}

void
lrg_ray_hit_free (LrgRayHit *self)
{
	g_free (self);
}

void
lrg_ray_hit_reset (LrgRayHit *self)
{
	g_return_if_fail (self != NULL);
	memset (self, 0, sizeof (LrgRayHit));
	self->fraction = G_MAXFLOAT;
	self->body = LRG_PHYSICS_NO_BODY;
	self->child = -1;
	self->sub_shape = LRG_PHYSICS_NO_SUB_SHAPE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

G_DEFINE_FINAL_TYPE (LrgPhysicsShape, lrg_physics_shape, G_TYPE_OBJECT)

static void
child_clear (gpointer data)
{
	LrgShapeChild *c;

	c = (LrgShapeChild *)data;
	g_clear_object (&c->shape);
}

static void
lrg_physics_shape_finalize (GObject *object)
{
	LrgPhysicsShape *self;

	self = LRG_PHYSICS_SHAPE (object);
	g_free (self->hull_verts);
	g_free (self->face_indices);
	g_free (self->face_start);
	g_free (self->face_count);
	g_free (self->face_planes);
	g_free (self->mesh_verts);
	g_free (self->tris);
	g_free (self->tri_material);
	g_free (self->tri_user);
	g_free (self->tri_active);
	g_free (self->nodes);
	g_free (self->tri_order);
	g_free (self->heights);
	g_free (self->hf_active);
	g_clear_pointer (&self->children, g_array_unref);

	G_OBJECT_CLASS (lrg_physics_shape_parent_class)->finalize (object);
}

static void
lrg_physics_shape_class_init (LrgPhysicsShapeClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = lrg_physics_shape_finalize;
}

static void
lrg_physics_shape_init (LrgPhysicsShape *self)
{
	self->kind = LRG_PHYSICS_SHAPE_KIND_SPHERE;
	self->radius = 0.5f;
}

static LrgPhysicsShape *
shape_alloc (LrgPhysicsShapeKind kind)
{
	LrgPhysicsShape *s;

	s = (LrgPhysicsShape *)g_object_new (LRG_TYPE_PHYSICS_SHAPE, NULL);
	s->kind = kind;
	return s;
}

/* Clamps a size argument: non-finite or below @min becomes @min. */
static gfloat
clamp_size (gfloat v, gfloat min)
{
	if (!shape_finite (v) || v < min)
		return min;
	return v;
}

/* ==========================================================================
 * Primitive constructors
 * ========================================================================== */

LrgPhysicsShape *
lrg_physics_shape_new_sphere (gfloat radius)
{
	LrgPhysicsShape *s;

	s = shape_alloc (LRG_PHYSICS_SHAPE_KIND_SPHERE);
	s->radius = clamp_size (radius, 0.001f);
	v3_set (s->bmin, -s->radius, -s->radius, -s->radius);
	v3_set (s->bmax, s->radius, s->radius, s->radius);
	s->max_extent = s->radius;
	return s;
}

LrgPhysicsShape *
lrg_physics_shape_new_capsule (gfloat radius,
                               gfloat half_height)
{
	LrgPhysicsShape *s;

	s = shape_alloc (LRG_PHYSICS_SHAPE_KIND_CAPSULE);
	s->radius = clamp_size (radius, 0.001f);
	s->half_height = clamp_size (half_height, 0.0f);
	v3_set (s->bmin, -s->radius, -s->half_height - s->radius, -s->radius);
	v3_set (s->bmax, s->radius, s->half_height + s->radius, s->radius);
	s->max_extent = s->half_height + s->radius;
	return s;
}

LrgPhysicsShape *
lrg_physics_shape_new_cylinder (gfloat radius,
                                gfloat half_height)
{
	LrgPhysicsShape *s;

	s = shape_alloc (LRG_PHYSICS_SHAPE_KIND_CYLINDER);
	s->radius = clamp_size (radius, 0.001f);
	s->half_height = clamp_size (half_height, 0.001f);
	v3_set (s->bmin, -s->radius, -s->half_height, -s->radius);
	v3_set (s->bmax, s->radius, s->half_height, s->radius);
	s->max_extent = sqrtf (s->radius * s->radius + s->half_height * s->half_height);
	return s;
}

LrgPhysicsShape *
lrg_physics_shape_new_box (gfloat half_x,
                           gfloat half_y,
                           gfloat half_z)
{
	LrgPhysicsShape *s;

	s = shape_alloc (LRG_PHYSICS_SHAPE_KIND_BOX);
	s->radius = 0.0f;
	v3_set (s->half, clamp_size (half_x, 0.001f), clamp_size (half_y, 0.001f),
	        clamp_size (half_z, 0.001f));
	v3_set (s->bmin, -s->half[0], -s->half[1], -s->half[2]);
	v3_copy (s->bmax, s->half);
	s->max_extent = v3_len (s->half);
	return s;
}

LrgPhysicsShape *
lrg_physics_shape_new_plane (const gfloat *normal,
                             gfloat        distance)
{
	LrgPhysicsShape *s;
	gfloat n[3];

	g_return_val_if_fail (normal != NULL, NULL);

	s = shape_alloc (LRG_PHYSICS_SHAPE_KIND_PLANE);
	s->radius = 0.0f;
	if (!shape_finite (normal[0]) || !shape_finite (normal[1]) || !shape_finite (normal[2]))
		v3_copy (n, axis_y);
	else
		v3_normalize (n, normal, axis_y);
	v3_copy (s->plane_n, n);
	s->plane_d = shape_finite (distance) ? distance : 0.0f;
	v3_set (s->bmin, -LRG_PHYSICS_PLANE_HALF_EXTENT, -LRG_PHYSICS_PLANE_HALF_EXTENT,
	        -LRG_PHYSICS_PLANE_HALF_EXTENT);
	v3_set (s->bmax, LRG_PHYSICS_PLANE_HALF_EXTENT, LRG_PHYSICS_PLANE_HALF_EXTENT,
	        LRG_PHYSICS_PLANE_HALF_EXTENT);
	s->max_extent = G_MAXFLOAT;
	return s;
}

/* ==========================================================================
 * Convex hull: quickhull (double precision) + coplanar face merging
 * ========================================================================== */

typedef struct
{
	guint    v[3];
	gdouble  n[3];
	gdouble  d;
	gboolean alive;
	GArray  *outside;   /* guint point indices above this face */
} QhFace;

typedef struct
{
	const gdouble *p;   /* points, 3 per point */
	guint          n_points;
	gdouble        eps;
	GArray        *faces;  /* QhFace */
} QhState;

static inline const gdouble *
qh_pt (QhState *st, guint i)
{
	return &st->p[i * 3];
}

static gdouble
qh_dist (const QhFace *f, const gdouble *p)
{
	return f->n[0] * p[0] + f->n[1] * p[1] + f->n[2] * p[2] - f->d;
}

/* Appends face (a, b, c) wound counter-clockwise about its outward normal. */
static guint
qh_add_face (QhState *st,
             guint    a,
             guint    b,
             guint    c)
{
	QhFace f;
	const gdouble *pa;
	const gdouble *pb;
	const gdouble *pc;
	gdouble e1[3];
	gdouble e2[3];
	gdouble l;
	guint i;

	memset (&f, 0, sizeof (f));
	f.v[0] = a;
	f.v[1] = b;
	f.v[2] = c;
	pa = qh_pt (st, a);
	pb = qh_pt (st, b);
	pc = qh_pt (st, c);
	for (i = 0; i < 3; i++)
	{
		e1[i] = pb[i] - pa[i];
		e2[i] = pc[i] - pa[i];
	}
	f.n[0] = e1[1] * e2[2] - e1[2] * e2[1];
	f.n[1] = e1[2] * e2[0] - e1[0] * e2[2];
	f.n[2] = e1[0] * e2[1] - e1[1] * e2[0];
	l = sqrt (f.n[0] * f.n[0] + f.n[1] * f.n[1] + f.n[2] * f.n[2]);
	if (l > 0.0)
	{
		f.n[0] /= l;
		f.n[1] /= l;
		f.n[2] /= l;
	}
	f.d = f.n[0] * pa[0] + f.n[1] * pa[1] + f.n[2] * pa[2];
	f.alive = TRUE;
	f.outside = g_array_new (FALSE, FALSE, sizeof (guint));
	g_array_append_val (st->faces, f);
	return st->faces->len - 1;
}

/* Gives point @pi to the face among [@first, faces->len) that it is
 * furthest above (by more than eps).  Points above no face are inside. */
static void
qh_assign (QhState *st,
           guint    pi,
           guint    first)
{
	guint i;
	guint best;
	gdouble best_d;

	best = G_MAXUINT;
	best_d = st->eps;
	for (i = first; i < st->faces->len; i++)
	{
		QhFace *f;
		gdouble d;

		f = &g_array_index (st->faces, QhFace, i);
		if (!f->alive)
			continue;
		d = qh_dist (f, qh_pt (st, pi));
		if (d > best_d)
		{
			best_d = d;
			best = i;
		}
	}
	if (best != G_MAXUINT)
		g_array_append_val (g_array_index (st->faces, QhFace, best).outside, pi);
}

/* TRUE when the directed edge a->b belongs to a visible face. */
static gboolean
qh_has_edge (QhState      *st,
             const GArray *visible,
             guint         a,
             guint         b)
{
	guint i;
	guint k;

	for (i = 0; i < visible->len; i++)
	{
		const QhFace *f;

		f = &g_array_index (st->faces, QhFace, g_array_index (visible, guint, i));
		for (k = 0; k < 3; k++)
			if (f->v[k] == a && f->v[(k + 1) % 3] == b)
				return TRUE;
	}
	return FALSE;
}

/* Builds the triangulated hull of the points.  Returns FALSE when the
 * points span no volume. */
static gboolean
qh_build (QhState *st)
{
	guint i;
	guint i0;
	guint i1;
	guint i2;
	guint i3;
	guint ext[6];
	gdouble best;
	gdouble c[3];
	guint iter;
	guint cursor;

	/* Extreme points on the axes, then the most distant pair of them. */
	for (i = 0; i < 6; i++)
		ext[i] = 0;
	for (i = 1; i < st->n_points; i++)
	{
		guint ax;

		for (ax = 0; ax < 3; ax++)
		{
			if (qh_pt (st, i)[ax] < qh_pt (st, ext[ax * 2])[ax])
				ext[ax * 2] = i;
			if (qh_pt (st, i)[ax] > qh_pt (st, ext[ax * 2 + 1])[ax])
				ext[ax * 2 + 1] = i;
		}
	}
	i0 = 0;
	i1 = 0;
	best = -1.0;
	for (i = 0; i < 6; i++)
	{
		guint j;

		for (j = i + 1; j < 6; j++)
		{
			const gdouble *a;
			const gdouble *b;
			gdouble d;

			a = qh_pt (st, ext[i]);
			b = qh_pt (st, ext[j]);
			d = (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1])
			    + (a[2] - b[2]) * (a[2] - b[2]);
			if (d > best)
			{
				best = d;
				i0 = ext[i];
				i1 = ext[j];
			}
		}
	}
	if (best <= st->eps * st->eps)
		return FALSE;

	/* Point furthest from the line i0-i1. */
	i2 = G_MAXUINT;
	best = st->eps * st->eps;
	for (i = 0; i < st->n_points; i++)
	{
		const gdouble *a;
		const gdouble *b;
		const gdouble *p;
		gdouble ab[3];
		gdouble ap[3];
		gdouble cr[3];
		gdouble d;
		guint k;

		a = qh_pt (st, i0);
		b = qh_pt (st, i1);
		p = qh_pt (st, i);
		for (k = 0; k < 3; k++)
		{
			ab[k] = b[k] - a[k];
			ap[k] = p[k] - a[k];
		}
		cr[0] = ab[1] * ap[2] - ab[2] * ap[1];
		cr[1] = ab[2] * ap[0] - ab[0] * ap[2];
		cr[2] = ab[0] * ap[1] - ab[1] * ap[0];
		d = (cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2])
		    / (ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]);
		if (d > best)
		{
			best = d;
			i2 = i;
		}
	}
	if (i2 == G_MAXUINT)
		return FALSE;

	/* Point furthest from the plane i0 i1 i2. */
	{
		guint tmp;
		QhFace *f;

		tmp = qh_add_face (st, i0, i1, i2);
		f = &g_array_index (st->faces, QhFace, tmp);
		i3 = G_MAXUINT;
		best = st->eps;
		for (i = 0; i < st->n_points; i++)
		{
			gdouble d;

			d = fabs (qh_dist (f, qh_pt (st, i)));
			if (d > best)
			{
				best = d;
				i3 = i;
			}
		}
		g_array_unref (f->outside);
		g_array_set_size (st->faces, 0);
		if (i3 == G_MAXUINT)
			return FALSE;
	}

	/* Initial tetrahedron, every face oriented away from the centroid. */
	for (i = 0; i < 3; i++)
		c[i] = (qh_pt (st, i0)[i] + qh_pt (st, i1)[i] + qh_pt (st, i2)[i]
		        + qh_pt (st, i3)[i]) * 0.25;
	{
		guint tet[4][3];
		guint t;

		tet[0][0] = i0; tet[0][1] = i1; tet[0][2] = i2;
		tet[1][0] = i0; tet[1][1] = i3; tet[1][2] = i1;
		tet[2][0] = i1; tet[2][1] = i3; tet[2][2] = i2;
		tet[3][0] = i2; tet[3][1] = i3; tet[3][2] = i0;
		for (t = 0; t < 4; t++)
		{
			guint fi;
			QhFace *f;

			fi = qh_add_face (st, tet[t][0], tet[t][1], tet[t][2]);
			f = &g_array_index (st->faces, QhFace, fi);
			if (qh_dist (f, c) > 0.0)
			{
				/* Wrong winding: flip. */
				g_array_unref (f->outside);
				g_array_set_size (st->faces, fi);
				qh_add_face (st, tet[t][0], tet[t][2], tet[t][1]);
			}
		}
	}
	for (i = 0; i < st->n_points; i++)
	{
		if (i == i0 || i == i1 || i == i2 || i == i3)
			continue;
		qh_assign (st, i, 0);
	}

	/* Quickhull iterations: always expand the lowest-index face that still
	 * has outside points (deterministic), towards its furthest point. */
	cursor = 0;
	for (iter = 0; iter < st->n_points * 4 + 16; iter++)
	{
		guint fi;
		QhFace *f;
		guint eye;
		gdouble eye_d;
		g_autoptr(GArray) visible = NULL;
		g_autoptr(GArray) orphans = NULL;
		g_autoptr(GArray) horizon = NULL;
		guint first_new;

		/* Faces before the cursor never regain outside points (orphans go
		 * to new faces only), so the scan resumes where it stopped. */
		fi = G_MAXUINT;
		for (i = cursor; i < st->faces->len; i++)
		{
			f = &g_array_index (st->faces, QhFace, i);
			if (f->alive && f->outside->len > 0)
			{
				fi = i;
				break;
			}
		}
		cursor = i;
		if (fi == G_MAXUINT)
			break;

		f = &g_array_index (st->faces, QhFace, fi);
		eye = g_array_index (f->outside, guint, 0);
		eye_d = qh_dist (f, qh_pt (st, eye));
		for (i = 1; i < f->outside->len; i++)
		{
			guint pi;
			gdouble d;

			pi = g_array_index (f->outside, guint, i);
			d = qh_dist (f, qh_pt (st, pi));
			if (d > eye_d)
			{
				eye_d = d;
				eye = pi;
			}
		}

		/* Every face the eye point sees. */
		visible = g_array_new (FALSE, FALSE, sizeof (guint));
		for (i = 0; i < st->faces->len; i++)
		{
			QhFace *g;

			g = &g_array_index (st->faces, QhFace, i);
			if (g->alive && qh_dist (g, qh_pt (st, eye)) > st->eps)
				g_array_append_val (visible, i);
		}
		if (visible->len == 0)
			g_array_append_val (visible, fi);

		/* Horizon: directed edges of visible faces whose twin is not
		 * visible, kept in face order for determinism. */
		horizon = g_array_new (FALSE, FALSE, sizeof (guint) * 2);
		for (i = 0; i < visible->len; i++)
		{
			QhFace *g;
			guint k;

			g = &g_array_index (st->faces, QhFace, g_array_index (visible, guint, i));
			for (k = 0; k < 3; k++)
			{
				guint e[2];

				e[0] = g->v[k];
				e[1] = g->v[(k + 1) % 3];
				if (!qh_has_edge (st, visible, e[1], e[0]))
					g_array_append_vals (horizon, e, 1);
			}
		}

		/* Collect orphaned points, kill visible faces. */
		orphans = g_array_new (FALSE, FALSE, sizeof (guint));
		for (i = 0; i < visible->len; i++)
		{
			QhFace *g;
			guint k;

			g = &g_array_index (st->faces, QhFace, g_array_index (visible, guint, i));
			for (k = 0; k < g->outside->len; k++)
			{
				guint pi;

				pi = g_array_index (g->outside, guint, k);
				if (pi != eye)
					g_array_append_val (orphans, pi);
			}
			g_array_set_size (g->outside, 0);
			g->alive = FALSE;
		}

		/* Fan from the eye to the horizon. */
		first_new = st->faces->len;
		for (i = 0; i < horizon->len; i++)
		{
			const guint *e;

			e = (const guint *)(horizon->data + i * sizeof (guint) * 2);
			qh_add_face (st, e[0], e[1], eye);
		}
		for (i = 0; i < orphans->len; i++)
			qh_assign (st, g_array_index (orphans, guint, i), first_new);
	}
	return TRUE;
}

/* Union-find root with path halving. */
static guint
uf_find (guint *parent,
         guint  i)
{
	while (parent[i] != i)
	{
		parent[i] = parent[parent[i]];
		i = parent[i];
	}
	return i;
}

/* Integrates mass properties (density 1) of a closed, outward-wound
 * triangle set: volume, centre of mass and inertia about the COM.  Uses the
 * second-moment form of each tetrahedron (ref, a, b, c):
 * S = V / 20 * (aa' + bb' + cc' + (a+b+c)(a+b+c)'), relative to ref. */
static void
integrate_tris (const gdouble *verts,
                const guint   *tris,
                guint          n_tris,
                const gdouble *ref,
                gdouble       *out_volume,
                gdouble       *out_com,
                gdouble       *out_inertia)
{
	gdouble vol;
	gdouble com[3];
	gdouble S[9];
	guint t;
	guint i;
	guint j;
	gdouble tr;

	vol = 0.0;
	memset (com, 0, sizeof (com));
	memset (S, 0, sizeof (S));
	for (t = 0; t < n_tris; t++)
	{
		gdouble a[3];
		gdouble b[3];
		gdouble c[3];
		gdouble s[3];
		gdouble v;

		for (i = 0; i < 3; i++)
		{
			a[i] = verts[tris[t * 3 + 0] * 3 + i] - ref[i];
			b[i] = verts[tris[t * 3 + 1] * 3 + i] - ref[i];
			c[i] = verts[tris[t * 3 + 2] * 3 + i] - ref[i];
			s[i] = a[i] + b[i] + c[i];
		}
		v = (a[0] * (b[1] * c[2] - b[2] * c[1])
		     - a[1] * (b[0] * c[2] - b[2] * c[0])
		     + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0;
		vol += v;
		for (i = 0; i < 3; i++)
			com[i] += v * s[i] / 4.0;
		for (i = 0; i < 3; i++)
			for (j = 0; j < 3; j++)
				S[i * 3 + j] += v / 20.0 * (a[i] * a[j] + b[i] * b[j] + c[i] * c[j]
				                            + s[i] * s[j]);
	}
	if (vol <= 0.0)
	{
		*out_volume = 0.0;
		memcpy (out_com, ref, sizeof (gdouble) * 3);
		memset (out_inertia, 0, sizeof (gdouble) * 9);
		return;
	}
	for (i = 0; i < 3; i++)
		com[i] /= vol;
	/* Shift the second moment to the centre of mass. */
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			S[i * 3 + j] -= vol * com[i] * com[j];
	tr = S[0] + S[4] + S[8];
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			out_inertia[i * 3 + j] = (i == j ? tr : 0.0) - S[i * 3 + j];
	*out_volume = vol;
	for (i = 0; i < 3; i++)
		out_com[i] = com[i] + ref[i];
}

LrgPhysicsShape *
lrg_physics_shape_new_convex_hull (const gfloat  *points,
                                   guint          n_floats,
                                   gfloat         radius,
                                   GError       **error)
{
	g_autoptr(LrgPhysicsShape) s = NULL;
	g_autofree gdouble *p = NULL;
	QhState st;
	guint n;
	guint i;
	gdouble scale;
	g_autoptr(GArray) tris = NULL;
	g_autoptr(GArray) normals = NULL;
	g_autofree guint *parent = NULL;
	g_autofree guint *remap = NULL;
	g_autoptr(GArray) face_idx = NULL;
	g_autoptr(GArray) face_start = NULL;
	g_autoptr(GArray) face_cnt = NULL;
	g_autoptr(GArray) face_pl = NULL;
	g_autoptr(GArray) out_verts = NULL;
	guint n_tris;
	gdouble merge_tol;
	g_autoptr(GHashTable) edge_tri = NULL;
	g_autofree guint *group_head = NULL;
	g_autofree guint *group_tail = NULL;
	g_autofree guint *group_next = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* Validate input. */
	if (points == NULL || n_floats % 3 != 0 || n_floats < 12)
	{
		g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
		             "convex hull needs at least 4 points (3 floats each), got %u floats",
		             n_floats);
		return NULL;
	}
	if (!shape_finite (radius) || radius < 0.0f)
	{
		g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
		             "convex radius must be finite and >= 0");
		return NULL;
	}
	n = n_floats / 3;
	p = g_new (gdouble, n_floats);
	scale = 0.0;
	for (i = 0; i < n_floats; i++)
	{
		if (!shape_finite (points[i]))
		{
			g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
			             "convex hull point %u is not finite", i / 3);
			return NULL;
		}
		p[i] = (gdouble)points[i];
		scale = MAX (scale, fabs (p[i]));
	}

	/* Quickhull. */
	st.p = p;
	st.n_points = n;
	st.eps = MAX (scale, 1e-6) * 1e-6;
	st.faces = g_array_new (FALSE, FALSE, sizeof (QhFace));
	if (!qh_build (&st))
	{
		for (i = 0; i < st.faces->len; i++)
			g_array_unref (g_array_index (st.faces, QhFace, i).outside);
		g_array_unref (st.faces);
		g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_DEGENERATE,
		             "convex hull points are coincident, collinear or coplanar");
		return NULL;
	}

	/* Live triangles and their normals. */
	tris = g_array_new (FALSE, FALSE, sizeof (guint));
	normals = g_array_new (FALSE, FALSE, sizeof (gdouble));
	for (i = 0; i < st.faces->len; i++)
	{
		QhFace *f;

		f = &g_array_index (st.faces, QhFace, i);
		if (f->alive)
		{
			g_array_append_vals (tris, f->v, 3);
			g_array_append_vals (normals, f->n, 3);
		}
		g_array_unref (f->outside);
	}
	g_array_unref (st.faces);
	n_tris = tris->len / 3;

	/* Merge coplanar neighbours: union triangles that share an edge and
	 * whose normals agree within ~0.25 degrees. */
	merge_tol = st.eps * 10.0;
	parent = g_new (guint, n_tris);
	for (i = 0; i < n_tris; i++)
		parent[i] = i;
	/* Neighbours through a directed-edge map: the twin of edge (a, b)
	 * of triangle i is edge (b, a) of its neighbour. */
	edge_tri = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
	for (i = 0; i < n_tris; i++)
	{
		guint k;

		for (k = 0; k < 3; k++)
		{
			gint64 *key;

			key = g_new (gint64, 1);
			*key = ((gint64)g_array_index (tris, guint, i * 3 + k) << 32)
			       | (gint64)g_array_index (tris, guint, i * 3 + (k + 1) % 3);
			g_hash_table_insert (edge_tri, key, GUINT_TO_POINTER (i + 1));
		}
	}
	for (i = 0; i < n_tris; i++)
	{
		guint k;

		for (k = 0; k < 3; k++)
		{
			const guint *ti;
			const guint *tj;
			const gdouble *ni;
			const gdouble *nj;
			guint b;
			guint j;
			gdouble off;
			gint64 twin;

			ti = &g_array_index (tris, guint, i * 3);
			twin = ((gint64)ti[(k + 1) % 3] << 32) | (gint64)ti[k];
			j = GPOINTER_TO_UINT (g_hash_table_lookup (edge_tri, &twin));
			if (j == 0 || j - 1 <= i)
				continue;
			j--;
			tj = &g_array_index (tris, guint, j * 3);
			ni = &g_array_index (normals, gdouble, i * 3);
			nj = &g_array_index (normals, gdouble, j * 3);
			/* The vertex of j that is not shared must lie on i's plane. */
			off = 0.0;
			for (b = 0; b < 3; b++)
			{
				const gdouble *q;
				const gdouble *o;

				q = &p[tj[b] * 3];
				o = &p[ti[0] * 3];
				off = MAX (off, fabs (ni[0] * (q[0] - o[0]) + ni[1] * (q[1] - o[1])
				                      + ni[2] * (q[2] - o[2])));
			}
			if (ni[0] * nj[0] + ni[1] * nj[1] + ni[2] * nj[2] > 1.0 - 1e-6
			    && off <= merge_tol)
			{
				guint ri;
				guint rj;

				ri = uf_find (parent, i);
				rj = uf_find (parent, j);
				if (ri != rj)
					parent[MAX (ri, rj)] = MIN (ri, rj);
			}
		}
	}

	/* Group lists: members of each root in triangle order. */
	group_head = g_new (guint, n_tris);
	group_tail = g_new (guint, n_tris);
	group_next = g_new (guint, n_tris);
	for (i = 0; i < n_tris; i++)
	{
		group_head[i] = G_MAXUINT;
		group_next[i] = G_MAXUINT;
	}
	for (i = 0; i < n_tris; i++)
	{
		guint r;

		r = uf_find (parent, i);
		if (group_head[r] == G_MAXUINT)
			group_head[r] = i;
		else
			group_next[group_tail[r]] = i;
		group_tail[r] = i;
	}

	/* Faces: for each group, walk the boundary loop (directed edges whose
	 * twin is not in the group).  A group whose boundary is not one loop
	 * falls back to its separate triangles. */
	face_idx = g_array_new (FALSE, FALSE, sizeof (guint));
	face_start = g_array_new (FALSE, FALSE, sizeof (guint));
	face_cnt = g_array_new (FALSE, FALSE, sizeof (guint));
	face_pl = g_array_new (FALSE, FALSE, sizeof (gfloat));
	for (i = 0; i < n_tris; i++)
	{
		g_autoptr(GArray) members = NULL;
		g_autoptr(GArray) edges = NULL;
		g_autoptr(GArray) loop = NULL;
		gdouble nsum[3];
		gdouble nl;
		gdouble dsum;
		guint j;
		guint k;
		gboolean ok;

		if (uf_find (parent, i) != i)
			continue;
		members = g_array_new (FALSE, FALSE, sizeof (guint));
		for (j = group_head[i]; j != G_MAXUINT; j = group_next[j])
			g_array_append_val (members, j);

		/* Boundary edges of the group. */
		edges = g_array_new (FALSE, FALSE, sizeof (guint) * 2);
		for (j = 0; j < members->len; j++)
		{
			const guint *t;

			t = &g_array_index (tris, guint, g_array_index (members, guint, j) * 3);
			for (k = 0; k < 3; k++)
			{
				guint a;
				guint b;
				guint m;
				gboolean twin;

				a = t[k];
				b = t[(k + 1) % 3];
				twin = FALSE;
				for (m = 0; m < members->len && !twin; m++)
				{
					const guint *u;
					guint q;

					u = &g_array_index (tris, guint, g_array_index (members, guint, m) * 3);
					for (q = 0; q < 3; q++)
						if (u[q] == b && u[(q + 1) % 3] == a)
							twin = TRUE;
				}
				if (!twin)
				{
					guint e[2];

					e[0] = a;
					e[1] = b;
					g_array_append_vals (edges, e, 1);
				}
			}
		}

		/* Walk the loop. */
		loop = g_array_new (FALSE, FALSE, sizeof (guint));
		ok = edges->len >= 3;
		if (ok)
		{
			guint start;
			guint cur;

			start = ((const guint *)edges->data)[0];
			cur = start;
			for (k = 0; k <= edges->len; k++)
			{
				guint m;
				guint next;
				guint found;

				g_array_append_val (loop, cur);
				found = 0;
				next = G_MAXUINT;
				for (m = 0; m < edges->len; m++)
				{
					const guint *e;

					e = (const guint *)(edges->data + m * sizeof (guint) * 2);
					if (e[0] == cur)
					{
						next = e[1];
						found++;
					}
				}
				if (found != 1)
				{
					ok = FALSE;
					break;
				}
				cur = next;
				if (cur == start)
					break;
			}
			if (ok && (cur != start || loop->len != edges->len))
				ok = FALSE;
		}

		/* Plane of the group: area-weighted normal. */
		memset (nsum, 0, sizeof (nsum));
		for (j = 0; j < members->len; j++)
		{
			const gdouble *nn;

			nn = &g_array_index (normals, gdouble, g_array_index (members, guint, j) * 3);
			for (k = 0; k < 3; k++)
				nsum[k] += nn[k];
		}
		nl = sqrt (nsum[0] * nsum[0] + nsum[1] * nsum[1] + nsum[2] * nsum[2]);
		for (k = 0; k < 3; k++)
			nsum[k] /= nl;

		/* The merged plane must hold every loop vertex within tolerance. */
		if (ok && members->len > 1)
		{
			gdouble dmin;
			gdouble dmax;

			dmin = G_MAXDOUBLE;
			dmax = -G_MAXDOUBLE;
			for (j = 0; j < loop->len; j++)
			{
				const gdouble *v;
				gdouble dd;

				v = &p[g_array_index (loop, guint, j) * 3];
				dd = nsum[0] * v[0] + nsum[1] * v[1] + nsum[2] * v[2];
				dmin = MIN (dmin, dd);
				dmax = MAX (dmax, dd);
			}
			if (dmax - dmin > merge_tol)
				ok = FALSE;
		}

		if (ok)
		{
			guint start;
			gfloat pl[4];

			start = face_idx->len;
			dsum = 0.0;
			for (j = 0; j < loop->len; j++)
			{
				const gdouble *v;

				v = &p[g_array_index (loop, guint, j) * 3];
				dsum += nsum[0] * v[0] + nsum[1] * v[1] + nsum[2] * v[2];
			}
			g_array_append_vals (face_idx, loop->data, loop->len);
			g_array_append_val (face_start, start);
			g_array_append_val (face_cnt, loop->len);
			pl[0] = (gfloat)nsum[0];
			pl[1] = (gfloat)nsum[1];
			pl[2] = (gfloat)nsum[2];
			pl[3] = (gfloat)(dsum / (gdouble)loop->len);
			g_array_append_vals (face_pl, pl, 4);
		}
		else
		{
			for (j = 0; j < members->len; j++)
			{
				guint ti;
				guint start;
				guint three;
				const gdouble *nn;
				const gdouble *v;
				gfloat pl[4];

				ti = g_array_index (members, guint, j);
				start = face_idx->len;
				three = 3;
				g_array_append_vals (face_idx, &g_array_index (tris, guint, ti * 3), 3);
				g_array_append_val (face_start, start);
				g_array_append_val (face_cnt, three);
				nn = &g_array_index (normals, gdouble, ti * 3);
				v = &p[g_array_index (tris, guint, ti * 3) * 3];
				pl[0] = (gfloat)nn[0];
				pl[1] = (gfloat)nn[1];
				pl[2] = (gfloat)nn[2];
				pl[3] = (gfloat)(nn[0] * v[0] + nn[1] * v[1] + nn[2] * v[2]);
				g_array_append_vals (face_pl, pl, 4);
			}
		}
	}

	/* Keep only referenced vertices, in first-use order. */
	remap = g_new (guint, n);
	for (i = 0; i < n; i++)
		remap[i] = G_MAXUINT;
	out_verts = g_array_new (FALSE, FALSE, sizeof (gfloat));
	for (i = 0; i < face_idx->len; i++)
	{
		guint *vi;

		vi = &g_array_index (face_idx, guint, i);
		if (remap[*vi] == G_MAXUINT)
		{
			remap[*vi] = out_verts->len / 3;
			g_array_append_vals (out_verts, &points[*vi * 3], 3);
		}
		*vi = remap[*vi];
	}

	s = shape_alloc (LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL);
	s->radius = radius;
	s->n_hull_verts = out_verts->len / 3;
	s->hull_verts = (gfloat *)g_array_free (g_steal_pointer (&out_verts), FALSE);
	s->n_faces = face_cnt->len;
	s->face_indices = (guint *)g_array_free (g_steal_pointer (&face_idx), FALSE);
	s->face_start = (guint *)g_array_free (g_steal_pointer (&face_start), FALSE);
	s->face_count = (guint *)g_array_free (g_steal_pointer (&face_cnt), FALSE);
	s->face_planes = (gfloat *)g_array_free (g_steal_pointer (&face_pl), FALSE);

	/* Bounds and extent. */
	v3_copy (s->bmin, s->hull_verts);
	v3_copy (s->bmax, s->hull_verts);
	s->max_extent = 0.0f;
	for (i = 0; i < s->n_hull_verts; i++)
	{
		const gfloat *v;
		guint k;

		v = &s->hull_verts[i * 3];
		for (k = 0; k < 3; k++)
		{
			s->bmin[k] = MIN (s->bmin[k], v[k]);
			s->bmax[k] = MAX (s->bmax[k], v[k]);
		}
		s->max_extent = MAX (s->max_extent, v3_len (v));
	}
	for (i = 0; i < 3; i++)
	{
		s->bmin[i] -= radius;
		s->bmax[i] += radius;
	}
	s->max_extent += radius;

	/* Mass properties (density 1) from a fan triangulation of the faces,
	 * integrated about the vertex centroid. */
	{
		g_autofree gdouble *dv = NULL;
		g_autoptr(GArray) ft = NULL;
		gdouble ref[3];
		gdouble vol;
		gdouble com[3];
		gdouble inertia[9];
		guint f;

		dv = g_new (gdouble, s->n_hull_verts * 3);
		memset (ref, 0, sizeof (ref));
		for (i = 0; i < s->n_hull_verts * 3; i++)
		{
			dv[i] = (gdouble)s->hull_verts[i];
			ref[i % 3] += dv[i] / (gdouble)s->n_hull_verts;
		}
		ft = g_array_new (FALSE, FALSE, sizeof (guint));
		for (f = 0; f < s->n_faces; f++)
		{
			guint k;
			const guint *fi;

			fi = &s->face_indices[s->face_start[f]];
			for (k = 1; k + 1 < s->face_count[f]; k++)
			{
				g_array_append_val (ft, fi[0]);
				g_array_append_val (ft, fi[k]);
				g_array_append_val (ft, fi[k + 1]);
			}
		}
		integrate_tris (dv, (const guint *)ft->data, ft->len / 3, ref, &vol, com, inertia);
		s->hull_volume = (gfloat)vol;
		for (i = 0; i < 3; i++)
			s->hull_com[i] = (gfloat)com[i];
		for (i = 0; i < 9; i++)
			s->hull_inertia[i] = (gfloat)inertia[i];
		if (vol <= 0.0)
		{
			g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_DEGENERATE,
			             "convex hull has no volume");
			return NULL;
		}
	}

	/* Inner radius: distance from the centre of mass to the closest face. */
	s->hull_inner_radius = G_MAXFLOAT;
	for (i = 0; i < s->n_faces; i++)
	{
		const gfloat *pl;
		gfloat d;

		pl = &s->face_planes[i * 4];
		d = pl[3] - v3_dot (pl, s->hull_com);
		s->hull_inner_radius = MIN (s->hull_inner_radius, d);
	}
	s->hull_inner_radius = MAX (0.0f, s->hull_inner_radius) + radius;

	return (LrgPhysicsShape *)g_steal_pointer (&s);
}

/* ==========================================================================
 * Active edges
 * ========================================================================== */

gboolean
lrg_physics_shape_edge_is_active (const gfloat *n1,
                                  const gfloat *p1,
                                  const gfloat *n2,
                                  const gfloat *q,
                                  gfloat        cos_threshold)
{
	gfloat cos_angle;
	gfloat d[3];

	/* Back-to-back triangles: the edge is a real (thin) edge. */
	cos_angle = v3_dot (n1, n2);
	if (cos_angle < -0.999848f)
		return TRUE;

	/* Concave edge (the neighbour rises above our plane): never active. */
	v3_sub (d, q, p1);
	if (v3_dot (n1, d) > 1e-6f * (1.0f + v3_len (d)))
		return FALSE;

	/* Convex edge: active when sharper than the threshold. */
	return cos_angle < cos_threshold;
}

/* Unit normal of triangle (a, b, c); FALSE if degenerate. */
static gboolean
tri_normal (const gfloat *a,
            const gfloat *b,
            const gfloat *c,
            gfloat       *n)
{
	gfloat e1[3];
	gfloat e2[3];
	gfloat cr[3];
	gfloat l;

	v3_sub (e1, b, a);
	v3_sub (e2, c, a);
	v3_cross (cr, e1, e2);
	l = v3_len (cr);
	if (l <= 1e-12f)
	{
		v3_copy (n, axis_y);
		return FALSE;
	}
	n[0] = cr[0] / l;
	n[1] = cr[1] / l;
	n[2] = cr[2] / l;
	return TRUE;
}

/* ==========================================================================
 * Triangle mesh
 * ========================================================================== */

typedef struct
{
	const gfloat *verts;
	const guint  *tris;
	gfloat       *centroids;
	guint        *order;
	GArray       *nodes;
} BvhBuild;

static const gfloat *bvh_sort_centroids;
static guint bvh_sort_axis;

/* qsort comparator over triangle indices by centroid on one axis, ties by
 * index so the tree is deterministic.  Uses file-static context: builds
 * run on one thread at a time per call chain only through this sort, so
 * the context is set immediately before qsort under a lock. */
static int
bvh_cmp (const void *pa,
         const void *pb)
{
	guint a;
	guint b;
	gfloat ca;
	gfloat cb;

	a = *(const guint *)pa;
	b = *(const guint *)pb;
	ca = bvh_sort_centroids[a * 3 + bvh_sort_axis];
	cb = bvh_sort_centroids[b * 3 + bvh_sort_axis];
	if (ca < cb)
		return -1;
	if (ca > cb)
		return 1;
	return (a < b) ? -1 : (a > b ? 1 : 0);
}

G_LOCK_DEFINE_STATIC (bvh_sort);

static void
tri_bounds (const gfloat *verts,
            const guint  *t,
            gfloat       *mn,
            gfloat       *mx)
{
	guint k;
	guint i;

	v3_copy (mn, &verts[t[0] * 3]);
	v3_copy (mx, &verts[t[0] * 3]);
	for (k = 1; k < 3; k++)
		for (i = 0; i < 3; i++)
		{
			mn[i] = MIN (mn[i], verts[t[k] * 3 + i]);
			mx[i] = MAX (mx[i], verts[t[k] * 3 + i]);
		}
}

/* Builds the node for order[first .. first + count) and returns its index. */
static guint
bvh_build_node (BvhBuild *b,
                guint     first,
                guint     count)
{
	LrgShapeBvhNode node;
	guint idx;
	guint i;
	guint k;
	gfloat ext[3];
	guint axis;

	memset (&node, 0, sizeof (node));
	tri_bounds (b->verts, &b->tris[b->order[first] * 3], node.min, node.max);
	for (i = 1; i < count; i++)
	{
		gfloat mn[3];
		gfloat mx[3];

		tri_bounds (b->verts, &b->tris[b->order[first + i] * 3], mn, mx);
		for (k = 0; k < 3; k++)
		{
			node.min[k] = MIN (node.min[k], mn[k]);
			node.max[k] = MAX (node.max[k], mx[k]);
		}
	}
	idx = b->nodes->len;
	g_array_append_val (b->nodes, node);
	if (count <= 4)
	{
		g_array_index (b->nodes, LrgShapeBvhNode, idx).first = first;
		g_array_index (b->nodes, LrgShapeBvhNode, idx).count = count;
		return idx;
	}

	/* Split at the median along the longest axis of the centroid spread. */
	for (k = 0; k < 3; k++)
		ext[k] = node.max[k] - node.min[k];
	axis = 0;
	if (ext[1] > ext[axis])
		axis = 1;
	if (ext[2] > ext[axis])
		axis = 2;
	G_LOCK (bvh_sort);
	bvh_sort_centroids = b->centroids;
	bvh_sort_axis = axis;
	qsort (&b->order[first], count, sizeof (guint), bvh_cmp);
	G_UNLOCK (bvh_sort);

	{
		guint left_count;
		guint children;
		LrgShapeBvhNode empty;

		left_count = count / 2;
		/* Reserve two consecutive child slots, then fill them. */
		memset (&empty, 0, sizeof (empty));
		children = b->nodes->len;
		g_array_append_val (b->nodes, empty);
		g_array_append_val (b->nodes, empty);
		g_array_index (b->nodes, LrgShapeBvhNode, idx).first = children;
		g_array_index (b->nodes, LrgShapeBvhNode, idx).count = 0;
		{
			guint l;
			guint r;

			l = bvh_build_node (b, first, left_count);
			r = bvh_build_node (b, first + left_count, count - left_count);
			/* Move the built subtrees' roots into the reserved slots. */
			g_array_index (b->nodes, LrgShapeBvhNode, children) =
				g_array_index (b->nodes, LrgShapeBvhNode, l);
			g_array_index (b->nodes, LrgShapeBvhNode, children + 1) =
				g_array_index (b->nodes, LrgShapeBvhNode, r);
		}
	}
	return idx;
}

/* Edge key of two welded vertex ids. */
static guint64
edge_key (guint a,
          guint b)
{
	guint lo;
	guint hi;

	lo = MIN (a, b);
	hi = MAX (a, b);
	return ((guint64)lo << 32) | (guint64)hi;
}

static guint
pos_hash (gconstpointer key)
{
	const guint32 *u;

	u = (const guint32 *)key;
	return (guint)(u[0] * 73856093u ^ u[1] * 19349663u ^ u[2] * 83492791u);
}

static gboolean
pos_equal (gconstpointer a,
           gconstpointer b)
{
	return memcmp (a, b, sizeof (gfloat) * 3) == 0;
}

LrgPhysicsShape *
lrg_physics_shape_new_triangle_mesh (const gfloat  *vertices,
                                     guint          n_floats,
                                     const guint   *indices,
                                     guint          n_indices,
                                     GError       **error)
{
	return lrg_physics_shape_new_triangle_mesh_full (vertices, n_floats, indices, n_indices,
	                                                 NULL, NULL, 0,
	                                                 LRG_PHYSICS_DEFAULT_ACTIVE_EDGE_ANGLE,
	                                                 error);
}

LrgPhysicsShape *
lrg_physics_shape_new_triangle_mesh_full (const gfloat  *vertices,
                                          guint          n_floats,
                                          const guint   *indices,
                                          guint          n_indices,
                                          const guint   *materials,
                                          const guint   *user_ids,
                                          guint          n_triangles,
                                          gfloat         active_edge_angle,
                                          GError       **error)
{
	g_autoptr(LrgPhysicsShape) s = NULL;
	guint n_verts;
	guint n_in;
	guint i;
	guint k;
	g_autoptr(GArray) kept = NULL;
	g_autofree guint *weld = NULL;
	g_autoptr(GHashTable) pos_map = NULL;
	g_autoptr(GHashTable) edge_map = NULL;
	gfloat cos_thr;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* Validate. */
	if (vertices == NULL || indices == NULL || n_floats % 3 != 0 || n_floats < 9
	    || n_indices % 3 != 0 || n_indices < 3)
	{
		g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
		             "triangle mesh needs 3 floats per vertex and 3 indices per triangle");
		return NULL;
	}
	n_verts = n_floats / 3;
	n_in = n_indices / 3;
	if ((materials != NULL || user_ids != NULL) && n_triangles != n_in)
	{
		g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
		             "per-triangle arrays have %u entries, mesh has %u triangles",
		             n_triangles, n_in);
		return NULL;
	}
	for (i = 0; i < n_floats; i++)
		if (!shape_finite (vertices[i]))
		{
			g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
			             "mesh vertex %u is not finite", i / 3);
			return NULL;
		}
	for (i = 0; i < n_indices; i++)
		if (indices[i] >= n_verts)
		{
			g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
			             "mesh index %u (%u) is out of range", i, indices[i]);
			return NULL;
		}
	if (!shape_finite (active_edge_angle))
		active_edge_angle = LRG_PHYSICS_DEFAULT_ACTIVE_EDGE_ANGLE;
	cos_thr = cosf (active_edge_angle);

	/* Keep non-degenerate triangles. */
	kept = g_array_new (FALSE, FALSE, sizeof (guint));
	for (i = 0; i < n_in; i++)
	{
		gfloat nn[3];

		if (tri_normal (&vertices[indices[i * 3] * 3], &vertices[indices[i * 3 + 1] * 3],
		                &vertices[indices[i * 3 + 2] * 3], nn))
			g_array_append_val (kept, i);
	}
	if (kept->len == 0)
	{
		g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_DEGENERATE,
		             "triangle mesh has no triangle with area");
		return NULL;
	}

	s = shape_alloc (LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH);
	s->radius = 0.0f;
	s->n_mesh_verts = n_verts;
	s->mesh_verts = (gfloat *)g_memdup2 (vertices, sizeof (gfloat) * n_floats);
	s->n_tris = kept->len;
	s->tris = g_new (guint, s->n_tris * 3);
	s->tri_material = g_new0 (guint, s->n_tris);
	s->tri_user = g_new0 (guint, s->n_tris);
	s->tri_active = g_new0 (guint8, s->n_tris);
	for (i = 0; i < s->n_tris; i++)
	{
		guint src;

		src = g_array_index (kept, guint, i);
		for (k = 0; k < 3; k++)
			s->tris[i * 3 + k] = indices[src * 3 + k];
		if (materials != NULL)
			s->tri_material[i] = materials[src];
		if (user_ids != NULL)
			s->tri_user[i] = user_ids[src];
	}

	/* Weld identical positions for adjacency. */
	weld = g_new (guint, n_verts);
	pos_map = g_hash_table_new (pos_hash, pos_equal);
	for (i = 0; i < n_verts; i++)
	{
		gpointer found;

		if (g_hash_table_lookup_extended (pos_map, &s->mesh_verts[i * 3], NULL, &found))
			weld[i] = GPOINTER_TO_UINT (found);
		else
		{
			weld[i] = i;
			g_hash_table_insert (pos_map, &s->mesh_verts[i * 3], GUINT_TO_POINTER (i));
		}
	}

	/* Edge -> list of (tri * 3 + edge) + 1, packed in a GArray per edge. */
	edge_map = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free,
	                                  (GDestroyNotify)g_array_unref);
	for (i = 0; i < s->n_tris; i++)
		for (k = 0; k < 3; k++)
		{
			guint64 key;
			GArray *list;
			guint val;

			key = edge_key (weld[s->tris[i * 3 + k]], weld[s->tris[i * 3 + (k + 1) % 3]]);
			list = (GArray *)g_hash_table_lookup (edge_map, &key);
			if (list == NULL)
			{
				gint64 *kp;

				kp = g_new (gint64, 1);
				*kp = (gint64)key;
				list = g_array_new (FALSE, FALSE, sizeof (guint));
				g_hash_table_insert (edge_map, kp, list);
			}
			val = i * 3 + k;
			g_array_append_val (list, val);
		}
	for (i = 0; i < s->n_tris; i++)
	{
		gfloat n1[3];
		const gfloat *a;

		a = &s->mesh_verts[s->tris[i * 3] * 3];
		tri_normal (a, &s->mesh_verts[s->tris[i * 3 + 1] * 3],
		            &s->mesh_verts[s->tris[i * 3 + 2] * 3], n1);
		for (k = 0; k < 3; k++)
		{
			guint64 key;
			GArray *list;
			gboolean active;

			key = edge_key (weld[s->tris[i * 3 + k]], weld[s->tris[i * 3 + (k + 1) % 3]]);
			list = (GArray *)g_hash_table_lookup (edge_map, &key);
			active = TRUE;
			if (list != NULL && list->len == 2)
			{
				guint other;
				guint ot;
				guint oe;
				gfloat n2[3];
				const gfloat *q;

				other = g_array_index (list, guint, 0);
				if (other / 3 == i)
					other = g_array_index (list, guint, 1);
				ot = other / 3;
				oe = other % 3;
				tri_normal (&s->mesh_verts[s->tris[ot * 3] * 3],
				            &s->mesh_verts[s->tris[ot * 3 + 1] * 3],
				            &s->mesh_verts[s->tris[ot * 3 + 2] * 3], n2);
				/* The neighbour's vertex that is not on the shared edge. */
				q = &s->mesh_verts[s->tris[ot * 3 + (oe + 2) % 3] * 3];
				active = lrg_physics_shape_edge_is_active (n1, a, n2, q, cos_thr);
			}
			if (active)
				s->tri_active[i] |= (guint8)(1u << k);
		}
	}

	/* AABB tree. */
	{
		BvhBuild b;
		g_autofree gfloat *centroids = NULL;

		centroids = g_new (gfloat, s->n_tris * 3);
		s->tri_order = g_new (guint, s->n_tris);
		for (i = 0; i < s->n_tris; i++)
		{
			s->tri_order[i] = i;
			for (k = 0; k < 3; k++)
				centroids[i * 3 + k] = (s->mesh_verts[s->tris[i * 3] * 3 + k]
				                        + s->mesh_verts[s->tris[i * 3 + 1] * 3 + k]
				                        + s->mesh_verts[s->tris[i * 3 + 2] * 3 + k]) / 3.0f;
		}
		b.verts = s->mesh_verts;
		b.tris = s->tris;
		b.centroids = centroids;
		b.order = s->tri_order;
		b.nodes = g_array_new (FALSE, FALSE, sizeof (LrgShapeBvhNode));
		bvh_build_node (&b, 0, s->n_tris);
		s->n_nodes = b.nodes->len;
		s->nodes = (LrgShapeBvhNode *)g_array_free (b.nodes, FALSE);
		v3_copy (s->bmin, s->nodes[0].min);
		v3_copy (s->bmax, s->nodes[0].max);
	}
	s->max_extent = MAX (v3_len (s->bmin), v3_len (s->bmax));
	return (LrgPhysicsShape *)g_steal_pointer (&s);
}

/* ==========================================================================
 * Heightfield
 * ========================================================================== */

/* Corners of heightfield triangle @half of cell (@x, @z); FALSE in a hole.
 * half 0 = (p00, p11, p10), half 1 = (p00, p01, p11): both wound so the
 * normal points up. */
static gboolean
hf_triangle (LrgPhysicsShape *s,
             guint            x,
             guint            z,
             guint            half,
             gfloat          *v)
{
	gfloat h00;
	gfloat h10;
	gfloat h01;
	gfloat h11;
	gfloat x0;
	gfloat x1;
	gfloat z0;
	gfloat z1;

	h00 = s->heights[z * s->hf_nx + x];
	h10 = s->heights[z * s->hf_nx + x + 1];
	h01 = s->heights[(z + 1) * s->hf_nx + x];
	h11 = s->heights[(z + 1) * s->hf_nx + x + 1];
	if (h00 == LRG_PHYSICS_HEIGHTFIELD_HOLE || h10 == LRG_PHYSICS_HEIGHTFIELD_HOLE
	    || h01 == LRG_PHYSICS_HEIGHTFIELD_HOLE || h11 == LRG_PHYSICS_HEIGHTFIELD_HOLE)
		return FALSE;
	x0 = (gfloat)x * s->hf_scale[0];
	x1 = (gfloat)(x + 1) * s->hf_scale[0];
	z0 = (gfloat)z * s->hf_scale[2];
	z1 = (gfloat)(z + 1) * s->hf_scale[2];
	v3_set (&v[0], x0, h00, z0);
	if (half == 0)
	{
		v3_set (&v[3], x1, h11, z1);
		v3_set (&v[6], x1, h10, z0);
	}
	else
	{
		v3_set (&v[3], x0, h01, z1);
		v3_set (&v[6], x1, h11, z1);
	}
	return TRUE;
}

LrgPhysicsShape *
lrg_physics_shape_new_heightfield (const gfloat  *heights,
                                   guint          n_heights,
                                   guint          n_x,
                                   guint          n_z,
                                   gfloat         scale_x,
                                   gfloat         scale_y,
                                   gfloat         scale_z,
                                   GError       **error)
{
	g_autoptr(LrgPhysicsShape) s = NULL;
	guint i;
	guint x;
	guint z;
	gboolean any;
	gfloat cos_thr;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* Validate. */
	if (heights == NULL || n_x < 2 || n_z < 2 || (guint64)n_x * n_z != n_heights)
	{
		g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
		             "heightfield needs n_x * n_z samples with n_x, n_z >= 2");
		return NULL;
	}
	if (!shape_finite (scale_x) || !shape_finite (scale_y) || !shape_finite (scale_z)
	    || scale_x <= 0.0f || scale_y <= 0.0f || scale_z <= 0.0f)
	{
		g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
		             "heightfield scale must be finite and > 0");
		return NULL;
	}
	for (i = 0; i < n_heights; i++)
		if (heights[i] != LRG_PHYSICS_HEIGHTFIELD_HOLE && !shape_finite (heights[i]))
		{
			g_set_error (error, LRG_PHYSICS_SHAPE_ERROR, LRG_PHYSICS_SHAPE_ERROR_INVALID,
			             "height sample %u is not finite", i);
			return NULL;
		}

	s = shape_alloc (LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD);
	s->radius = 0.0f;
	s->hf_nx = n_x;
	s->hf_nz = n_z;
	v3_set (s->hf_scale, scale_x, scale_y, scale_z);
	s->heights = g_new (gfloat, n_heights);
	any = FALSE;
	s->bmin[1] = 0.0f;
	s->bmax[1] = 0.0f;
	for (i = 0; i < n_heights; i++)
	{
		if (heights[i] == LRG_PHYSICS_HEIGHTFIELD_HOLE)
		{
			s->heights[i] = LRG_PHYSICS_HEIGHTFIELD_HOLE;
			continue;
		}
		s->heights[i] = heights[i] * scale_y;
		if (!any)
		{
			s->bmin[1] = s->heights[i];
			s->bmax[1] = s->heights[i];
			any = TRUE;
		}
		s->bmin[1] = MIN (s->bmin[1], s->heights[i]);
		s->bmax[1] = MAX (s->bmax[1], s->heights[i]);
	}
	s->bmin[0] = 0.0f;
	s->bmin[2] = 0.0f;
	s->bmax[0] = (gfloat)(n_x - 1) * scale_x;
	s->bmax[2] = (gfloat)(n_z - 1) * scale_z;
	s->max_extent = MAX (v3_len (s->bmin), v3_len (s->bmax));
	{
		gfloat c[3];

		v3_set (c, s->bmax[0], s->bmin[1], 0.0f);
		s->max_extent = MAX (s->max_extent, v3_len (c));
		v3_set (c, 0.0f, s->bmax[1], s->bmax[2]);
		s->max_extent = MAX (s->max_extent, v3_len (c));
	}

	/* Active edges of every triangle against its grid neighbour. */
	cos_thr = cosf (LRG_PHYSICS_DEFAULT_ACTIVE_EDGE_ANGLE);
	s->hf_active = g_new0 (guint8, (gsize)(n_x - 1) * (n_z - 1) * 2);
	for (z = 0; z + 1 < n_z; z++)
		for (x = 0; x + 1 < n_x; x++)
		{
			guint half;

			for (half = 0; half < 2; half++)
			{
				gfloat v[9];
				gfloat n1[3];
				guint e;
				guint8 bits;

				if (!hf_triangle (s, x, z, half, v))
					continue;
				tri_normal (&v[0], &v[3], &v[6], n1);
				bits = 0;
				for (e = 0; e < 3; e++)
				{
					/* Neighbour across edge e: (cell x, cell z, half, the
					 * neighbour's edge index whose opposite vertex is q). */
					gint nx;
					gint nz;
					guint nhalf;
					guint nedge;
					gfloat w[9];
					gfloat n2[3];
					gboolean active;

					nx = (gint)x;
					nz = (gint)z;
					if (half == 0)
					{
						if (e == 0) { nhalf = 1; nedge = 2; }
						else if (e == 1) { nx++; nhalf = 1; nedge = 0; }
						else { nz--; nhalf = 1; nedge = 1; }
					}
					else
					{
						if (e == 0) { nx--; nhalf = 0; nedge = 1; }
						else if (e == 1) { nz++; nhalf = 0; nedge = 2; }
						else { nhalf = 0; nedge = 0; }
					}
					active = TRUE;
					if (nx >= 0 && nz >= 0 && (guint)nx + 1 < n_x && (guint)nz + 1 < n_z
					    && hf_triangle (s, (guint)nx, (guint)nz, nhalf, w))
					{
						tri_normal (&w[0], &w[3], &w[6], n2);
						active = lrg_physics_shape_edge_is_active (n1, &v[0], n2,
						                                           &w[((nedge + 2) % 3) * 3],
						                                           cos_thr);
					}
					if (active)
						bits |= (guint8)(1u << e);
				}
				s->hf_active[((z * (n_x - 1)) + x) * 2 + half] = bits;
			}
		}
	return (LrgPhysicsShape *)g_steal_pointer (&s);
}

/* ==========================================================================
 * Compound
 * ========================================================================== */

LrgPhysicsShape *
lrg_physics_shape_new_compound (void)
{
	LrgPhysicsShape *s;

	s = shape_alloc (LRG_PHYSICS_SHAPE_KIND_COMPOUND);
	s->radius = 0.0f;
	s->children = g_array_new (FALSE, TRUE, sizeof (LrgShapeChild));
	g_array_set_clear_func (s->children, child_clear);
	return s;
}

gint
lrg_physics_shape_compound_add_child (LrgPhysicsShape *self,
                                      LrgPhysicsShape *child,
                                      const gfloat    *position,
                                      const gfloat    *rotation)
{
	LrgShapeChild c;
	gfloat one[3];
	guint i;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), -1);
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (child), -1);
	g_return_val_if_fail (position != NULL, -1);

	if (self->kind != LRG_PHYSICS_SHAPE_KIND_COMPOUND || child == self
	    || child->kind == LRG_PHYSICS_SHAPE_KIND_PLANE)
		return -1;
	for (i = 0; i < 3; i++)
		if (!shape_finite (position[i]))
			return -1;
	if (rotation != NULL)
		for (i = 0; i < 4; i++)
			if (!shape_finite (rotation[i]))
				return -1;

	memset (&c, 0, sizeof (c));
	c.shape = (LrgPhysicsShape *)g_object_ref (child);
	v3_copy (c.position, position);
	if (rotation != NULL)
		lrg_math3d_quat_normalize (rotation, c.rotation);
	else
		lrg_math3d_quat_identity (c.rotation);
	v3_set (one, 1.0f, 1.0f, 1.0f);
	lrg_math3d_compose_quat (c.position, c.rotation, one, c.xf);
	lrg_physics_shape_get_world_bounds (child, c.xf, c.bmin, c.bmax);
	g_array_append_val (self->children, c);

	/* Grow the compound bounds and extent. */
	if (self->children->len == 1)
	{
		v3_copy (self->bmin, c.bmin);
		v3_copy (self->bmax, c.bmax);
	}
	else
		for (i = 0; i < 3; i++)
		{
			self->bmin[i] = MIN (self->bmin[i], c.bmin[i]);
			self->bmax[i] = MAX (self->bmax[i], c.bmax[i]);
		}
	self->max_extent = MAX (self->max_extent, v3_len (c.position) + child->max_extent);
	return (gint)self->children->len - 1;
}

const LrgShapeChild *
lrg_physics_shape_get_child_internal (LrgPhysicsShape *self,
                                      guint            index)
{
	if (self->kind != LRG_PHYSICS_SHAPE_KIND_COMPOUND || index >= self->children->len)
		return NULL;
	return &g_array_index (self->children, LrgShapeChild, index);
}

/* ==========================================================================
 * Description
 * ========================================================================== */

LrgPhysicsShapeKind
lrg_physics_shape_get_kind (LrgPhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), LRG_PHYSICS_SHAPE_KIND_SPHERE);
	return self->kind;
}

gboolean
lrg_physics_shape_is_convex (LrgPhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), FALSE);
	return self->kind <= LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL;
}

gfloat
lrg_physics_shape_get_radius (LrgPhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), 0.0f);
	switch (self->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
	case LRG_PHYSICS_SHAPE_KIND_CYLINDER:
	case LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL:
		return self->radius;
	default:
		return 0.0f;
	}
}

gfloat
lrg_physics_shape_get_half_height (LrgPhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), 0.0f);
	if (self->kind == LRG_PHYSICS_SHAPE_KIND_CAPSULE || self->kind == LRG_PHYSICS_SHAPE_KIND_CYLINDER)
		return self->half_height;
	return 0.0f;
}

void
lrg_physics_shape_get_half_extents (LrgPhysicsShape *self,
                                    gfloat          *out)
{
	guint i;

	g_return_if_fail (LRG_IS_PHYSICS_SHAPE (self));
	g_return_if_fail (out != NULL);
	if (self->kind == LRG_PHYSICS_SHAPE_KIND_BOX)
	{
		v3_copy (out, self->half);
		return;
	}
	for (i = 0; i < 3; i++)
		out[i] = (self->bmax[i] - self->bmin[i]) * 0.5f;
}

gboolean
lrg_physics_shape_get_plane (LrgPhysicsShape *self,
                             gfloat          *normal,
                             gfloat          *distance)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), FALSE);
	if (self->kind != LRG_PHYSICS_SHAPE_KIND_PLANE)
		return FALSE;
	if (normal != NULL)
		v3_copy (normal, self->plane_n);
	if (distance != NULL)
		*distance = self->plane_d;
	return TRUE;
}

const gfloat *
lrg_physics_shape_get_hull_vertices (LrgPhysicsShape *self,
                                     guint           *n_floats)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), NULL);
	g_return_val_if_fail (n_floats != NULL, NULL);
	if (self->kind != LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL)
	{
		*n_floats = 0;
		return NULL;
	}
	*n_floats = self->n_hull_verts * 3;
	return self->hull_verts;
}

guint
lrg_physics_shape_get_hull_face_count (LrgPhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), 0);
	return self->kind == LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL ? self->n_faces : 0;
}

const guint *
lrg_physics_shape_get_hull_face (LrgPhysicsShape *self,
                                 guint            index,
                                 guint           *n_indices,
                                 gfloat          *plane)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), NULL);
	g_return_val_if_fail (n_indices != NULL, NULL);
	if (self->kind != LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL || index >= self->n_faces)
	{
		*n_indices = 0;
		return NULL;
	}
	*n_indices = self->face_count[index];
	if (plane != NULL)
		memcpy (plane, &self->face_planes[index * 4], sizeof (gfloat) * 4);
	return &self->face_indices[self->face_start[index]];
}

guint
lrg_physics_shape_get_triangle_count (LrgPhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), 0);
	if (self->kind == LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH)
		return self->n_tris;
	if (self->kind == LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD)
		return (self->hf_nx - 1) * (self->hf_nz - 1) * 2;
	return 0;
}

gboolean
lrg_physics_shape_get_triangle (LrgPhysicsShape *self,
                                guint            index,
                                gfloat          *vertices,
                                guint           *material,
                                guint           *user_id,
                                guint           *active_edges)
{
	guint k;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), FALSE);
	g_return_val_if_fail (vertices != NULL, FALSE);

	if (self->kind == LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH)
	{
		if (index >= self->n_tris)
			return FALSE;
		for (k = 0; k < 3; k++)
			v3_copy (&vertices[k * 3], &self->mesh_verts[self->tris[index * 3 + k] * 3]);
		if (material != NULL)
			*material = self->tri_material[index];
		if (user_id != NULL)
			*user_id = self->tri_user[index];
		if (active_edges != NULL)
			*active_edges = self->tri_active[index];
		return TRUE;
	}
	if (self->kind == LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD)
	{
		guint cell;
		guint cx;
		guint cz;

		if (index >= (self->hf_nx - 1) * (self->hf_nz - 1) * 2)
			return FALSE;
		cell = index / 2;
		cx = cell % (self->hf_nx - 1);
		cz = cell / (self->hf_nx - 1);
		if (!hf_triangle (self, cx, cz, index % 2, vertices))
			return FALSE;
		if (material != NULL)
			*material = 0;
		if (user_id != NULL)
			*user_id = 0;
		if (active_edges != NULL)
			*active_edges = self->hf_active[index];
		return TRUE;
	}
	return FALSE;
}

gboolean
lrg_physics_shape_get_heightfield_size (LrgPhysicsShape *self,
                                        guint           *n_x,
                                        guint           *n_z,
                                        gfloat          *scale)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), FALSE);
	if (self->kind != LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD)
		return FALSE;
	if (n_x != NULL)
		*n_x = self->hf_nx;
	if (n_z != NULL)
		*n_z = self->hf_nz;
	if (scale != NULL)
		v3_copy (scale, self->hf_scale);
	return TRUE;
}

gfloat
lrg_physics_shape_get_heightfield_height (LrgPhysicsShape *self,
                                          guint            x,
                                          guint            z)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), LRG_PHYSICS_HEIGHTFIELD_HOLE);
	if (self->kind != LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD || x >= self->hf_nx || z >= self->hf_nz)
		return LRG_PHYSICS_HEIGHTFIELD_HOLE;
	return self->heights[z * self->hf_nx + x];
}

guint
lrg_physics_shape_get_child_count (LrgPhysicsShape *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), 0);
	return self->kind == LRG_PHYSICS_SHAPE_KIND_COMPOUND ? self->children->len : 0;
}

LrgPhysicsShape *
lrg_physics_shape_get_child (LrgPhysicsShape *self,
                             guint            index,
                             gfloat          *position,
                             gfloat          *rotation)
{
	const LrgShapeChild *c;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), NULL);
	c = lrg_physics_shape_get_child_internal (self, index);
	if (c == NULL)
		return NULL;
	if (position != NULL)
		v3_copy (position, c->position);
	if (rotation != NULL)
		memcpy (rotation, c->rotation, sizeof (gfloat) * 4);
	return c->shape;
}

/* ==========================================================================
 * Mass properties
 * ========================================================================== */

gfloat
lrg_physics_shape_get_volume (LrgPhysicsShape *self)
{
	gfloat r;
	gfloat h;
	guint i;
	gfloat v;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), 0.0f);
	r = self->radius;
	h = self->half_height;
	switch (self->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
		return 4.0f / 3.0f * G_PI * r * r * r;
	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
		return G_PI * r * r * 2.0f * h + 4.0f / 3.0f * G_PI * r * r * r;
	case LRG_PHYSICS_SHAPE_KIND_CYLINDER:
		return G_PI * r * r * 2.0f * h;
	case LRG_PHYSICS_SHAPE_KIND_BOX:
		return 8.0f * self->half[0] * self->half[1] * self->half[2];
	case LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL:
		return self->hull_volume;
	case LRG_PHYSICS_SHAPE_KIND_COMPOUND:
		v = 0.0f;
		for (i = 0; i < self->children->len; i++)
			v += lrg_physics_shape_get_volume (g_array_index (self->children, LrgShapeChild, i).shape);
		return v;
	default:
		return 0.0f;
	}
}

/* Diagonal inertia helper. */
static void
diag3 (gfloat *I,
       gfloat  x,
       gfloat  y,
       gfloat  z)
{
	memset (I, 0, sizeof (gfloat) * 9);
	I[0] = x;
	I[4] = y;
	I[8] = z;
}

gboolean
lrg_physics_shape_get_mass_properties (LrgPhysicsShape *self,
                                       gfloat           density,
                                       gfloat          *mass,
                                       gfloat          *com,
                                       gfloat          *inertia)
{
	gfloat m;
	gfloat c[3];
	gfloat I[9];
	gfloat r;
	gfloat h;
	guint i;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), FALSE);
	if (!shape_finite (density) || density <= 0.0f)
		return FALSE;

	r = self->radius;
	h = self->half_height;
	v3_set (c, 0.0f, 0.0f, 0.0f);
	switch (self->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
		m = density * lrg_physics_shape_get_volume (self);
		diag3 (I, 0.4f * m * r * r, 0.4f * m * r * r, 0.4f * m * r * r);
		break;

	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
		{
			/* Cylinder of height H plus two hemispheres whose centres of
			 * mass sit 3r/8 beyond the cylinder ends. */
			gfloat H;
			gfloat mc;
			gfloat ms;
			gfloat iy;
			gfloat ix;

			H = 2.0f * h;
			mc = density * G_PI * r * r * H;
			ms = density * 4.0f / 3.0f * G_PI * r * r * r;
			m = mc + ms;
			iy = mc * r * r * 0.5f + ms * 0.4f * r * r;
			ix = mc * (H * H / 12.0f + r * r / 4.0f)
			     + ms * (0.4f * r * r + H * H / 4.0f + 3.0f * H * r / 8.0f);
			diag3 (I, ix, iy, ix);
		}
		break;

	case LRG_PHYSICS_SHAPE_KIND_CYLINDER:
		{
			gfloat H;
			gfloat ix;

			H = 2.0f * h;
			m = density * lrg_physics_shape_get_volume (self);
			ix = m * (3.0f * r * r + H * H) / 12.0f;
			diag3 (I, ix, 0.5f * m * r * r, ix);
		}
		break;

	case LRG_PHYSICS_SHAPE_KIND_BOX:
		{
			gfloat x2;
			gfloat y2;
			gfloat z2;

			m = density * lrg_physics_shape_get_volume (self);
			x2 = self->half[0] * self->half[0];
			y2 = self->half[1] * self->half[1];
			z2 = self->half[2] * self->half[2];
			diag3 (I, m / 3.0f * (y2 + z2), m / 3.0f * (x2 + z2), m / 3.0f * (x2 + y2));
		}
		break;

	case LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL:
		m = density * self->hull_volume;
		v3_copy (c, self->hull_com);
		for (i = 0; i < 9; i++)
			I[i] = density * self->hull_inertia[i];
		break;

	case LRG_PHYSICS_SHAPE_KIND_COMPOUND:
		{
			/* Sum the children (rotated inertia), then move every child's
			 * tensor to the common centre (parallel axis theorem). */
			guint n;
			gboolean any;
			g_autofree gfloat *child_m = NULL;
			g_autofree gfloat *child_c = NULL;
			g_autofree gfloat *child_I = NULL;

			n = self->children->len;
			child_m = g_new0 (gfloat, MAX (n, 1));
			child_c = g_new0 (gfloat, MAX (n, 1) * 3);
			child_I = g_new0 (gfloat, MAX (n, 1) * 9);
			m = 0.0f;
			any = FALSE;
			for (i = 0; i < n; i++)
			{
				const LrgShapeChild *ch;
				gfloat lm;
				gfloat lc[3];
				gfloat lI[9];
				gfloat R[9];
				gfloat RI[9];
				guint a;
				guint b;
				guint k;

				ch = &g_array_index (self->children, LrgShapeChild, i);
				if (!lrg_physics_shape_get_mass_properties (ch->shape, density, &lm, lc, lI))
					continue;
				any = TRUE;
				child_m[i] = lm;
				lrg_math3d_transform_point (ch->xf, lc, &child_c[i * 3]);
				/* R from the column-major child matrix: R[row*3+col]. */
				for (a = 0; a < 3; a++)
					for (b = 0; b < 3; b++)
						R[a * 3 + b] = ch->xf[b * 4 + a];
				/* I' = R I R^T */
				for (a = 0; a < 3; a++)
					for (b = 0; b < 3; b++)
					{
						RI[a * 3 + b] = 0.0f;
						for (k = 0; k < 3; k++)
							RI[a * 3 + b] += R[a * 3 + k] * lI[k * 3 + b];
					}
				for (a = 0; a < 3; a++)
					for (b = 0; b < 3; b++)
					{
						child_I[i * 9 + a * 3 + b] = 0.0f;
						for (k = 0; k < 3; k++)
							child_I[i * 9 + a * 3 + b] += RI[a * 3 + k] * R[b * 3 + k];
					}
				m += lm;
				v3_madd (c, c, &child_c[i * 3], lm);
			}
			if (!any || m <= 0.0f)
				return FALSE;
			for (i = 0; i < 3; i++)
				c[i] /= m;
			memset (I, 0, sizeof (I));
			for (i = 0; i < n; i++)
			{
				gfloat d[3];
				gfloat d2;
				guint a;
				guint b;

				if (child_m[i] <= 0.0f)
					continue;
				v3_sub (d, &child_c[i * 3], c);
				d2 = v3_dot (d, d);
				for (a = 0; a < 3; a++)
					for (b = 0; b < 3; b++)
						I[a * 3 + b] += child_I[i * 9 + a * 3 + b]
						                + child_m[i] * ((a == b ? d2 : 0.0f) - d[a] * d[b]);
			}
		}
		break;

	default:
		return FALSE;
	}

	if (mass != NULL)
		*mass = m;
	if (com != NULL)
		v3_copy (com, c);
	if (inertia != NULL)
		memcpy (inertia, I, sizeof (I));
	return TRUE;
}

/* ==========================================================================
 * Bounds
 * ========================================================================== */

void
lrg_physics_shape_get_local_bounds (LrgPhysicsShape *self,
                                    gfloat          *min,
                                    gfloat          *max)
{
	g_return_if_fail (LRG_IS_PHYSICS_SHAPE (self));
	g_return_if_fail (min != NULL && max != NULL);
	v3_copy (min, self->bmin);
	v3_copy (max, self->bmax);
}

void
lrg_physics_shape_get_world_bounds (LrgPhysicsShape *self,
                                    const gfloat    *xf,
                                    gfloat          *min,
                                    gfloat          *max)
{
	guint i;

	g_return_if_fail (LRG_IS_PHYSICS_SHAPE (self));
	g_return_if_fail (xf != NULL && min != NULL && max != NULL);

	if (self->kind == LRG_PHYSICS_SHAPE_KIND_SPHERE)
	{
		for (i = 0; i < 3; i++)
		{
			min[i] = xf[12 + i] - self->radius;
			max[i] = xf[12 + i] + self->radius;
		}
		return;
	}
	if (self->kind == LRG_PHYSICS_SHAPE_KIND_CAPSULE)
	{
		/* Segment ends +-h * column 1, expanded by the radius. */
		for (i = 0; i < 3; i++)
		{
			gfloat e;

			e = fabsf (xf[4 + i] * self->half_height);
			min[i] = xf[12 + i] - e - self->radius;
			max[i] = xf[12 + i] + e + self->radius;
		}
		return;
	}

	/* Rotated local box: centre transformed, half size through |R|. */
	{
		gfloat c[3];
		gfloat e[3];
		gfloat wc[3];

		for (i = 0; i < 3; i++)
		{
			c[i] = (self->bmin[i] + self->bmax[i]) * 0.5f;
			e[i] = (self->bmax[i] - self->bmin[i]) * 0.5f;
		}
		lrg_math3d_transform_point (xf, c, wc);
		for (i = 0; i < 3; i++)
		{
			gfloat we;

			we = fabsf (xf[0 + i]) * e[0] + fabsf (xf[4 + i]) * e[1] + fabsf (xf[8 + i]) * e[2];
			min[i] = wc[i] - we;
			max[i] = wc[i] + we;
		}
	}
}

gfloat
lrg_physics_shape_get_inner_radius (LrgPhysicsShape *self)
{
	guint i;
	gfloat r;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), 0.0f);
	switch (self->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
		return self->radius;
	case LRG_PHYSICS_SHAPE_KIND_CYLINDER:
		return MIN (self->radius, self->half_height);
	case LRG_PHYSICS_SHAPE_KIND_BOX:
		return MIN (self->half[0], MIN (self->half[1], self->half[2]));
	case LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL:
		return self->hull_inner_radius;
	case LRG_PHYSICS_SHAPE_KIND_COMPOUND:
		r = 0.0f;
		for (i = 0; i < self->children->len; i++)
			r = MAX (r, lrg_physics_shape_get_inner_radius (
			                g_array_index (self->children, LrgShapeChild, i).shape));
		return r;
	default:
		return 0.0f;
	}
}

/* ==========================================================================
 * Support functions and supporting faces
 * ========================================================================== */

gfloat
lrg_physics_shape_core_radius (LrgPhysicsShape *self)
{
	switch (self->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
	case LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL:
		return self->radius;
	default:
		return 0.0f;
	}
}

void
lrg_physics_shape_support_core (LrgPhysicsShape *self,
                                const gfloat    *dir,
                                gfloat          *out)
{
	guint i;

	switch (self->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
		v3_set (out, 0.0f, 0.0f, 0.0f);
		return;
	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
		v3_set (out, 0.0f, dir[1] >= 0.0f ? self->half_height : -self->half_height, 0.0f);
		return;
	case LRG_PHYSICS_SHAPE_KIND_CYLINDER:
		{
			gfloat l;

			l = sqrtf (dir[0] * dir[0] + dir[2] * dir[2]);
			if (l > 1e-12f)
				v3_set (out, dir[0] / l * self->radius, 0.0f, dir[2] / l * self->radius);
			else
				v3_set (out, 0.0f, 0.0f, 0.0f);
			out[1] = dir[1] >= 0.0f ? self->half_height : -self->half_height;
		}
		return;
	case LRG_PHYSICS_SHAPE_KIND_BOX:
		for (i = 0; i < 3; i++)
			out[i] = dir[i] >= 0.0f ? self->half[i] : -self->half[i];
		return;
	case LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL:
		{
			guint best;
			gfloat best_d;

			best = 0;
			best_d = v3_dot (&self->hull_verts[0], dir);
			for (i = 1; i < self->n_hull_verts; i++)
			{
				gfloat d;

				d = v3_dot (&self->hull_verts[i * 3], dir);
				if (d > best_d)
				{
					best_d = d;
					best = i;
				}
			}
			v3_copy (out, &self->hull_verts[best * 3]);
		}
		return;
	default:
		v3_set (out, 0.0f, 0.0f, 0.0f);
		return;
	}
}

gboolean
lrg_physics_shape_support (LrgPhysicsShape *self,
                           const gfloat    *dir,
                           gfloat          *out)
{
	gfloat r;
	gfloat n[3];

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), FALSE);
	g_return_val_if_fail (dir != NULL && out != NULL, FALSE);
	if (!lrg_physics_shape_is_convex (self))
	{
		v3_set (out, 0.0f, 0.0f, 0.0f);
		return FALSE;
	}
	lrg_physics_shape_support_core (self, dir, out);
	r = lrg_physics_shape_core_radius (self);
	if (r > 0.0f)
	{
		v3_normalize (n, dir, axis_y);
		v3_madd (out, out, n, r);
	}
	return TRUE;
}

guint
lrg_physics_shape_supporting_face (LrgPhysicsShape *self,
                                   const gfloat    *dir,
                                   gfloat          *out,
                                   guint           *face_id)
{
	gfloat d[3];
	guint i;

	v3_normalize (d, dir, axis_y);
	*face_id = 0;
	switch (self->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
		v3_set (out, d[0] * self->radius, d[1] * self->radius, d[2] * self->radius);
		return 1;

	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
		if (fabsf (d[1]) < 0.05f)
		{
			/* Side: the segment pushed out by the radius (2 points). */
			gfloat hd[3];

			v3_set (hd, d[0], 0.0f, d[2]);
			v3_normalize (hd, hd, d);
			v3_set (&out[0], hd[0] * self->radius, self->half_height, hd[2] * self->radius);
			v3_set (&out[3], hd[0] * self->radius, -self->half_height, hd[2] * self->radius);
			*face_id = 1;
			return 2;
		}
		v3_set (out, d[0] * self->radius,
		        (d[1] >= 0.0f ? self->half_height : -self->half_height) + d[1] * self->radius,
		        d[2] * self->radius);
		*face_id = d[1] >= 0.0f ? 2 : 3;
		return 1;

	case LRG_PHYSICS_SHAPE_KIND_CYLINDER:
		if (fabsf (d[1]) > 0.995f)
		{
			/* Cap polygon, counter-clockwise about the cap normal. */
			gfloat y;
			gfloat sgn;

			sgn = d[1] > 0.0f ? 1.0f : -1.0f;
			y = sgn * self->half_height;
			for (i = 0; i < LRG_SHAPE_CYLINDER_CAP_POINTS; i++)
			{
				gfloat a;

				a = 2.0f * G_PI * (gfloat)i / (gfloat)LRG_SHAPE_CYLINDER_CAP_POINTS;
				v3_set (&out[i * 3], self->radius * cosf (a), y,
				        -sgn * self->radius * sinf (a));
			}
			*face_id = d[1] > 0.0f ? 1 : 2;
			return LRG_SHAPE_CYLINDER_CAP_POINTS;
		}
		if (fabsf (d[1]) < 0.05f)
		{
			gfloat hd[3];

			v3_set (hd, d[0], 0.0f, d[2]);
			v3_normalize (hd, hd, d);
			v3_set (&out[0], hd[0] * self->radius, self->half_height, hd[2] * self->radius);
			v3_set (&out[3], hd[0] * self->radius, -self->half_height, hd[2] * self->radius);
			*face_id = 3;
			return 2;
		}
		lrg_physics_shape_support_core (self, d, out);
		*face_id = 4;
		return 1;

	case LRG_PHYSICS_SHAPE_KIND_BOX:
		{
			/* Face whose normal is most aligned with d, corners
			 * (-,-) (+,-) (+,+) (-,+) in (u, v) which is CCW about +axis. */
			guint ax;
			guint u;
			guint v;
			gfloat sgn;
			static const gfloat cu[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
			static const gfloat cv[4] = { -1.0f, -1.0f, 1.0f, 1.0f };

			ax = 0;
			if (fabsf (d[1]) > fabsf (d[ax]))
				ax = 1;
			if (fabsf (d[2]) > fabsf (d[ax]))
				ax = 2;
			sgn = d[ax] >= 0.0f ? 1.0f : -1.0f;
			u = (ax + 1) % 3;
			v = (ax + 2) % 3;
			for (i = 0; i < 4; i++)
			{
				guint j;
				gfloat *p;

				j = sgn > 0.0f ? i : 3 - i;
				p = &out[i * 3];
				p[ax] = sgn * self->half[ax];
				p[u] = cu[j] * self->half[u];
				p[v] = cv[j] * self->half[v];
			}
			*face_id = ax * 2 + (sgn > 0.0f ? 0 : 1);
			return 4;
		}

	case LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL:
		{
			guint best;
			gfloat best_d;
			guint n;
			const gfloat *pl;

			best = 0;
			best_d = v3_dot (&self->face_planes[0], d);
			for (i = 1; i < self->n_faces; i++)
			{
				gfloat dd;

				dd = v3_dot (&self->face_planes[i * 4], d);
				if (dd > best_d)
				{
					best_d = dd;
					best = i;
				}
			}
			pl = &self->face_planes[best * 4];
			n = MIN (self->face_count[best], (guint)LRG_SHAPE_MAX_FACE_POINTS);
			for (i = 0; i < n; i++)
				v3_madd (&out[i * 3],
				         &self->hull_verts[self->face_indices[self->face_start[best] + i] * 3],
				         pl, self->radius);
			*face_id = best;
			return n;
		}

	default:
		v3_set (out, 0.0f, 0.0f, 0.0f);
		return 1;
	}
}

/* ==========================================================================
 * Triangle iteration
 * ========================================================================== */

static gboolean
box_overlap (const gfloat *amin,
             const gfloat *amax,
             const gfloat *bmin,
             const gfloat *bmax)
{
	return !(amin[0] > bmax[0] || amax[0] < bmin[0]
	         || amin[1] > bmax[1] || amax[1] < bmin[1]
	         || amin[2] > bmax[2] || amax[2] < bmin[2]);
}

void
lrg_physics_shape_foreach_triangle (LrgPhysicsShape      *self,
                                    const gfloat         *bmin,
                                    const gfloat         *bmax,
                                    LrgShapeTriangleFunc  func,
                                    gpointer              user_data)
{
	if (self->kind == LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH)
	{
		guint stack[64];
		guint sp;

		sp = 0;
		stack[sp++] = 0;
		while (sp > 0)
		{
			const LrgShapeBvhNode *node;

			node = &self->nodes[stack[--sp]];
			if (!box_overlap (node->min, node->max, bmin, bmax))
				continue;
			if (node->count > 0)
			{
				guint i;

				for (i = 0; i < node->count; i++)
				{
					guint t;
					gfloat v[9];
					gfloat mn[3];
					gfloat mx[3];
					guint k;

					t = self->tri_order[node->first + i];
					tri_bounds (self->mesh_verts, &self->tris[t * 3], mn, mx);
					if (!box_overlap (mn, mx, bmin, bmax))
						continue;
					for (k = 0; k < 3; k++)
						v3_copy (&v[k * 3], &self->mesh_verts[self->tris[t * 3 + k] * 3]);
					if (!func (t, v, self->tri_material[t], self->tri_user[t],
					           self->tri_active[t], user_data))
						return;
				}
			}
			else if (sp + 2 <= G_N_ELEMENTS (stack))
			{
				/* Push the right child first so the left is visited first. */
				stack[sp++] = node->first + 1;
				stack[sp++] = node->first;
			}
		}
		return;
	}

	if (self->kind == LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD)
	{
		gint x0;
		gint x1;
		gint z0;
		gint z1;
		gint x;
		gint z;

		if (bmax[0] < 0.0f || bmax[2] < 0.0f || bmin[0] > self->bmax[0]
		    || bmin[2] > self->bmax[2] || bmin[1] > self->bmax[1] || bmax[1] < self->bmin[1])
			return;
		x0 = CLAMP ((gint)floorf (bmin[0] / self->hf_scale[0]), 0, (gint)self->hf_nx - 2);
		x1 = CLAMP ((gint)floorf (bmax[0] / self->hf_scale[0]), 0, (gint)self->hf_nx - 2);
		z0 = CLAMP ((gint)floorf (bmin[2] / self->hf_scale[2]), 0, (gint)self->hf_nz - 2);
		z1 = CLAMP ((gint)floorf (bmax[2] / self->hf_scale[2]), 0, (gint)self->hf_nz - 2);
		for (z = z0; z <= z1; z++)
			for (x = x0; x <= x1; x++)
			{
				guint half;

				for (half = 0; half < 2; half++)
				{
					gfloat v[9];
					gfloat mn[3];
					gfloat mx[3];
					guint k;
					guint id;

					if (!hf_triangle (self, (guint)x, (guint)z, half, v))
						continue;
					v3_copy (mn, &v[0]);
					v3_copy (mx, &v[0]);
					for (k = 1; k < 3; k++)
					{
						guint i;

						for (i = 0; i < 3; i++)
						{
							mn[i] = MIN (mn[i], v[k * 3 + i]);
							mx[i] = MAX (mx[i], v[k * 3 + i]);
						}
					}
					if (!box_overlap (mn, mx, bmin, bmax))
						continue;
					id = ((guint)z * (self->hf_nx - 1) + (guint)x) * 2 + half;
					if (!func (id, v, 0, 0, self->hf_active[id], user_data))
						return;
				}
			}
	}
}

/* ==========================================================================
 * Ray casts (local space)
 * ========================================================================== */

/* Fills @hit (if non-NULL) for a local hit at t with normal @n. */
static void
set_hit (LrgRayHit    *hit,
         const gfloat *o,
         const gfloat *d,
         gfloat        t,
         const gfloat *n)
{
	gfloat nn[3];

	if (hit == NULL)
		return;
	lrg_ray_hit_reset (hit);
	hit->fraction = t;
	v3_madd (hit->point, o, d, t);
	v3_normalize (nn, n, axis_y);
	v3_copy (hit->normal, nn);
}

/* Ray vs sphere at @c of radius @r: smallest t >= 0, or < 0 for a miss. */
static gfloat
ray_sphere (const gfloat *o,
            const gfloat *d,
            const gfloat *c,
            gfloat        r)
{
	gfloat m[3];
	gfloat a;
	gfloat b;
	gfloat cc;
	gfloat disc;

	v3_sub (m, o, c);
	a = v3_dot (d, d);
	b = v3_dot (m, d);
	cc = v3_dot (m, m) - r * r;
	if (a <= 0.0f)
		return -1.0f;
	if (cc > 0.0f && b > 0.0f)
		return -1.0f;
	disc = b * b - a * cc;
	if (disc < 0.0f)
		return -1.0f;
	return MAX (0.0f, (-b - sqrtf (disc)) / a);
}

/* Ray vs infinite Y cylinder of radius r around the axis, limited to
 * |y| <= h: entry t >= 0, or -1. */
static gfloat
ray_cyl_side (const gfloat *o,
              const gfloat *d,
              gfloat        r,
              gfloat        h)
{
	gfloat a;
	gfloat b;
	gfloat c;
	gfloat disc;
	gfloat t;
	gfloat y;

	a = d[0] * d[0] + d[2] * d[2];
	if (a < 1e-12f)
		return -1.0f;
	b = o[0] * d[0] + o[2] * d[2];
	c = o[0] * o[0] + o[2] * o[2] - r * r;
	disc = b * b - a * c;
	if (disc < 0.0f)
		return -1.0f;
	t = (-b - sqrtf (disc)) / a;
	if (t < 0.0f)
		return -1.0f;
	y = o[1] + d[1] * t;
	if (y < -h || y > h)
		return -1.0f;
	return t;
}

typedef struct
{
	const gfloat *o;
	const gfloat *d;
	gfloat        best;
	guint         tri;
	gfloat        n[3];
} RayTriCtx;

/* Two-sided Moller-Trumbore; on a closer hit stores t, the triangle and
 * the normal facing the ray. */
static gboolean
ray_triangle (RayTriCtx    *ctx,
              guint         id,
              const gfloat *v)
{
	gfloat e1[3];
	gfloat e2[3];
	gfloat p[3];
	gfloat q[3];
	gfloat s[3];
	gfloat det;
	gfloat inv;
	gfloat u;
	gfloat w;
	gfloat t;

	v3_sub (e1, &v[3], &v[0]);
	v3_sub (e2, &v[6], &v[0]);
	v3_cross (p, ctx->d, e2);
	det = v3_dot (e1, p);
	if (fabsf (det) < 1e-12f)
		return FALSE;
	inv = 1.0f / det;
	v3_sub (s, ctx->o, &v[0]);
	u = v3_dot (s, p) * inv;
	if (u < 0.0f || u > 1.0f)
		return FALSE;
	v3_cross (q, s, e1);
	w = v3_dot (ctx->d, q) * inv;
	if (w < 0.0f || u + w > 1.0f)
		return FALSE;
	t = v3_dot (e2, q) * inv;
	if (t < 0.0f || t >= ctx->best)
		return FALSE;
	ctx->best = t;
	ctx->tri = id;
	v3_cross (ctx->n, e1, e2);
	if (v3_dot (ctx->n, ctx->d) > 0.0f)
	{
		ctx->n[0] = -ctx->n[0];
		ctx->n[1] = -ctx->n[1];
		ctx->n[2] = -ctx->n[2];
	}
	return TRUE;
}

/* Slab test of a ray against an AABB: entry/exit t, FALSE on a miss. */
static gboolean
ray_aabb (const gfloat *o,
          const gfloat *d,
          const gfloat *mn,
          const gfloat *mx,
          gfloat       *t_in,
          gfloat       *t_out,
          gint         *axis_in)
{
	gfloat t0;
	gfloat t1;
	guint i;

	t0 = -G_MAXFLOAT;
	t1 = G_MAXFLOAT;
	*axis_in = -1;
	for (i = 0; i < 3; i++)
	{
		if (fabsf (d[i]) < 1e-20f)
		{
			if (o[i] < mn[i] || o[i] > mx[i])
				return FALSE;
			continue;
		}
		{
			gfloat a;
			gfloat b;
			gint sgn;

			a = (mn[i] - o[i]) / d[i];
			b = (mx[i] - o[i]) / d[i];
			sgn = -1;
			if (a > b)
			{
				gfloat tmp;

				tmp = a;
				a = b;
				b = tmp;
				sgn = 1;
			}
			if (a > t0)
			{
				t0 = a;
				*axis_in = (gint)(i * 2) + (sgn > 0 ? 1 : 0);
			}
			t1 = MIN (t1, b);
			if (t0 > t1)
				return FALSE;
		}
	}
	*t_in = t0;
	*t_out = t1;
	return TRUE;
}

/* Ray vs the triangle mesh's tree. */
static gboolean
ray_mesh (LrgPhysicsShape *self,
          RayTriCtx       *ctx)
{
	guint stack[64];
	guint sp;
	gboolean any;

	any = FALSE;
	sp = 0;
	stack[sp++] = 0;
	while (sp > 0)
	{
		const LrgShapeBvhNode *node;
		gfloat t0;
		gfloat t1;
		gint ax;

		node = &self->nodes[stack[--sp]];
		if (!ray_aabb (ctx->o, ctx->d, node->min, node->max, &t0, &t1, &ax))
			continue;
		if (t1 < 0.0f || t0 > ctx->best)
			continue;
		if (node->count > 0)
		{
			guint i;

			for (i = 0; i < node->count; i++)
			{
				guint t;
				gfloat v[9];
				guint k;

				t = self->tri_order[node->first + i];
				for (k = 0; k < 3; k++)
					v3_copy (&v[k * 3], &self->mesh_verts[self->tris[t * 3 + k] * 3]);
				if (ray_triangle (ctx, t, v))
					any = TRUE;
			}
		}
		else if (sp + 2 <= G_N_ELEMENTS (stack))
		{
			stack[sp++] = node->first + 1;
			stack[sp++] = node->first;
		}
	}
	return any;
}

/* Ray vs heightfield: walk the cells under the ray with a 2D DDA and test
 * their two triangles; the first cell with a hit holds the closest hit. */
static gboolean
ray_heightfield (LrgPhysicsShape *self,
                 RayTriCtx       *ctx)
{
	gfloat t0;
	gfloat t1;
	gint ax;
	gfloat mn[3];
	gfloat mx[3];
	gint cx;
	gint cz;
	gint stepx;
	gint stepz;
	gfloat tmaxx;
	gfloat tmaxz;
	gfloat tdx;
	gfloat tdz;
	gfloat p[3];
	guint guard;
	gfloat sx;
	gfloat sz;

	v3_copy (mn, self->bmin);
	v3_copy (mx, self->bmax);
	mn[1] -= 1e-3f;
	mx[1] += 1e-3f;
	if (!ray_aabb (ctx->o, ctx->d, mn, mx, &t0, &t1, &ax))
		return FALSE;
	t0 = MAX (t0, 0.0f);
	if (t0 > t1 || t0 > ctx->best)
		return FALSE;
	sx = self->hf_scale[0];
	sz = self->hf_scale[2];
	v3_madd (p, ctx->o, ctx->d, t0);
	cx = CLAMP ((gint)floorf (p[0] / sx), 0, (gint)self->hf_nx - 2);
	cz = CLAMP ((gint)floorf (p[2] / sz), 0, (gint)self->hf_nz - 2);
	stepx = ctx->d[0] > 0.0f ? 1 : (ctx->d[0] < 0.0f ? -1 : 0);
	stepz = ctx->d[2] > 0.0f ? 1 : (ctx->d[2] < 0.0f ? -1 : 0);
	tdx = stepx != 0 ? sx / fabsf (ctx->d[0]) : G_MAXFLOAT;
	tdz = stepz != 0 ? sz / fabsf (ctx->d[2]) : G_MAXFLOAT;
	tmaxx = stepx != 0
	        ? ((gfloat)(cx + (stepx > 0 ? 1 : 0)) * sx - ctx->o[0]) / ctx->d[0] : G_MAXFLOAT;
	tmaxz = stepz != 0
	        ? ((gfloat)(cz + (stepz > 0 ? 1 : 0)) * sz - ctx->o[2]) / ctx->d[2] : G_MAXFLOAT;

	for (guard = 0; guard < (self->hf_nx + self->hf_nz) * 2 + 4; guard++)
	{
		guint half;
		gboolean hit;

		hit = FALSE;
		for (half = 0; half < 2; half++)
		{
			gfloat v[9];

			if (hf_triangle (self, (guint)cx, (guint)cz, half, v)
			    && ray_triangle (ctx, ((guint)cz * (self->hf_nx - 1) + (guint)cx) * 2 + half, v))
				hit = TRUE;
		}
		if (hit)
			return TRUE;
		if (tmaxx < tmaxz)
		{
			if (tmaxx > t1 || tmaxx > ctx->best)
				break;
			cx += stepx;
			tmaxx += tdx;
		}
		else
		{
			if (tmaxz > t1 || tmaxz > ctx->best || stepz == 0)
				break;
			cz += stepz;
			tmaxz += tdz;
		}
		if (cx < 0 || cz < 0 || cx > (gint)self->hf_nx - 2 || cz > (gint)self->hf_nz - 2)
			break;
	}
	return FALSE;
}

gboolean
lrg_physics_shape_cast_ray (LrgPhysicsShape *self,
                            const gfloat    *origin,
                            const gfloat    *dir,
                            gfloat           max_fraction,
                            LrgRayHit       *hit)
{
	gfloat t;
	gfloat n[3];
	gfloat neg[3];
	gfloat p[3];
	const gfloat *o;
	const gfloat *d;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), FALSE);
	g_return_val_if_fail (origin != NULL && dir != NULL, FALSE);

	o = origin;
	d = dir;
	if (!(max_fraction >= 0.0f) || v3_dot (d, d) <= 0.0f)
		return FALSE;
	v3_set (neg, -d[0], -d[1], -d[2]);

	switch (self->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
		{
			gfloat zero[3];

			v3_set (zero, 0.0f, 0.0f, 0.0f);
			if (v3_dot (o, o) <= self->radius * self->radius)
			{
				set_hit (hit, o, d, 0.0f, neg);
				return TRUE;
			}
			t = ray_sphere (o, d, zero, self->radius);
			if (t < 0.0f || t > max_fraction)
				return FALSE;
			v3_madd (p, o, d, t);
			set_hit (hit, o, d, t, p);
			return TRUE;
		}

	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
		{
			gfloat cy;
			gfloat best;
			gfloat c[3];
			gfloat ts;
			gint which;

			cy = CLAMP (o[1], -self->half_height, self->half_height);
			if (o[0] * o[0] + (o[1] - cy) * (o[1] - cy) + o[2] * o[2]
			    <= self->radius * self->radius)
			{
				set_hit (hit, o, d, 0.0f, neg);
				return TRUE;
			}
			best = G_MAXFLOAT;
			which = -1;
			ts = ray_cyl_side (o, d, self->radius, self->half_height);
			if (ts >= 0.0f)
			{
				best = ts;
				which = 0;
			}
			v3_set (c, 0.0f, self->half_height, 0.0f);
			ts = ray_sphere (o, d, c, self->radius);
			if (ts >= 0.0f && ts < best)
			{
				best = ts;
				which = 1;
			}
			v3_set (c, 0.0f, -self->half_height, 0.0f);
			ts = ray_sphere (o, d, c, self->radius);
			if (ts >= 0.0f && ts < best)
			{
				best = ts;
				which = 2;
			}
			if (which < 0 || best > max_fraction)
				return FALSE;
			v3_madd (p, o, d, best);
			if (which == 0)
				v3_set (n, p[0], 0.0f, p[2]);
			else
				v3_set (n, p[0], p[1] - (which == 1 ? self->half_height : -self->half_height), p[2]);
			set_hit (hit, o, d, best, n);
			return TRUE;
		}

	case LRG_PHYSICS_SHAPE_KIND_CYLINDER:
		{
			gfloat best;
			gfloat ts;
			gint which;
			gint s;

			if (fabsf (o[1]) <= self->half_height
			    && o[0] * o[0] + o[2] * o[2] <= self->radius * self->radius)
			{
				set_hit (hit, o, d, 0.0f, neg);
				return TRUE;
			}
			best = G_MAXFLOAT;
			which = -1;
			ts = ray_cyl_side (o, d, self->radius, self->half_height);
			if (ts >= 0.0f)
			{
				best = ts;
				which = 0;
			}
			for (s = -1; s <= 1; s += 2)
			{
				gfloat y;

				if (fabsf (d[1]) < 1e-20f)
					break;
				y = (gfloat)s * self->half_height;
				ts = (y - o[1]) / d[1];
				if (ts >= 0.0f && ts < best)
				{
					gfloat q[3];

					v3_madd (q, o, d, ts);
					if (q[0] * q[0] + q[2] * q[2] <= self->radius * self->radius)
					{
						best = ts;
						which = s > 0 ? 1 : 2;
					}
				}
			}
			if (which < 0 || best > max_fraction)
				return FALSE;
			v3_madd (p, o, d, best);
			if (which == 0)
				v3_set (n, p[0], 0.0f, p[2]);
			else
				v3_set (n, 0.0f, which == 1 ? 1.0f : -1.0f, 0.0f);
			set_hit (hit, o, d, best, n);
			return TRUE;
		}

	case LRG_PHYSICS_SHAPE_KIND_BOX:
		{
			gfloat t0;
			gfloat t1;
			gint ax;

			if (fabsf (o[0]) <= self->half[0] && fabsf (o[1]) <= self->half[1]
			    && fabsf (o[2]) <= self->half[2])
			{
				set_hit (hit, o, d, 0.0f, neg);
				return TRUE;
			}
			if (!ray_aabb (o, d, self->bmin, self->bmax, &t0, &t1, &ax))
				return FALSE;
			if (t0 < 0.0f || t0 > max_fraction || ax < 0)
				return FALSE;
			v3_set (n, 0.0f, 0.0f, 0.0f);
			/* ax = axis * 2 + (entered through the max face ? 1 : 0) */
			n[ax / 2] = (ax % 2) ? 1.0f : -1.0f;
			set_hit (hit, o, d, t0, n);
			return TRUE;
		}

	case LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL:
		{
			/* Cyrus-Beck against the face planes pushed out by the radius;
			 * that polytope contains the rounded hull, so its entry is a
			 * safe start for sphere-tracing the rounded surface. */
			gfloat t_in;
			gfloat t_out;
			gint face;
			guint i;
			gboolean inside;

			t_in = 0.0f;
			t_out = max_fraction;
			face = -1;
			inside = TRUE;
			for (i = 0; i < self->n_faces; i++)
			{
				const gfloat *pl;
				gfloat dist;
				gfloat den;

				pl = &self->face_planes[i * 4];
				dist = v3_dot (pl, o) - (pl[3] + self->radius);
				den = v3_dot (pl, d);
				if (dist > 0.0f)
					inside = FALSE;
				if (fabsf (den) < 1e-20f)
				{
					if (dist > 0.0f)
						return FALSE;
					continue;
				}
				{
					gfloat tt;

					tt = -dist / den;
					if (den < 0.0f)
					{
						if (tt > t_in)
						{
							t_in = tt;
							face = (gint)i;
						}
					}
					else
						t_out = MIN (t_out, tt);
				}
				if (t_in > t_out)
					return FALSE;
			}
			if (self->radius <= 0.0f)
			{
				if (inside)
				{
					set_hit (hit, o, d, 0.0f, neg);
					return TRUE;
				}
				if (face < 0)
					return FALSE;
				set_hit (hit, o, d, t_in, &self->face_planes[face * 4]);
				return TRUE;
			}

			/* Rounded hull: sphere-trace the distance to the core. */
			{
				gfloat dl;
				gfloat closest[3];
				guint it;

				dl = v3_len (d);
				t = t_in;
				for (it = 0; it < 64; it++)
				{
					gfloat dist;

					v3_madd (p, o, d, t);
					dist = lrg_collide_internal_core_distance (self, p, closest) - self->radius;
					if (dist <= 1e-5f)
					{
						if (t == 0.0f && dist < 0.0f)
						{
							set_hit (hit, o, d, 0.0f, neg);
							return TRUE;
						}
						v3_sub (n, p, closest);
						set_hit (hit, o, d, t, n);
						return TRUE;
					}
					t += dist / dl;
					if (t > t_out)
						return FALSE;
				}
				return FALSE;
			}
		}

	case LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH:
	case LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD:
		{
			RayTriCtx ctx;
			gboolean any;

			ctx.o = o;
			ctx.d = d;
			ctx.best = max_fraction;
			ctx.tri = LRG_PHYSICS_NO_SUB_SHAPE;
			v3_set (ctx.n, 0.0f, 1.0f, 0.0f);
			if (self->kind == LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH)
				any = ray_mesh (self, &ctx);
			else
				any = ray_heightfield (self, &ctx);
			if (!any)
				return FALSE;
			set_hit (hit, o, d, ctx.best, ctx.n);
			if (hit != NULL)
			{
				hit->sub_shape = ctx.tri;
				if (self->kind == LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH)
				{
					hit->material = self->tri_material[ctx.tri];
					hit->user_id = self->tri_user[ctx.tri];
				}
			}
			return TRUE;
		}

	case LRG_PHYSICS_SHAPE_KIND_COMPOUND:
		{
			guint i;
			gfloat best;
			gboolean any;
			LrgRayHit tmp;
			LrgRayHit best_hit;

			best = max_fraction;
			any = FALSE;
			lrg_ray_hit_reset (&best_hit);
			for (i = 0; i < self->children->len; i++)
			{
				const LrgShapeChild *c;
				gfloat inv[16];
				gfloat lo[3];
				gfloat ld[3];

				c = &g_array_index (self->children, LrgShapeChild, i);
				lrg_math3d_invert (c->xf, inv);
				lrg_math3d_transform_point (inv, o, lo);
				lrg_math3d_transform_direction (inv, d, ld);
				if (lrg_physics_shape_cast_ray (c->shape, lo, ld, best, &tmp))
				{
					best = tmp.fraction;
					best_hit = tmp;
					best_hit.child = (gint)i;
					lrg_math3d_transform_direction (c->xf, tmp.normal, best_hit.normal);
					any = TRUE;
				}
			}
			if (!any)
				return FALSE;
			if (hit != NULL)
			{
				*hit = best_hit;
				v3_madd (hit->point, o, d, best_hit.fraction);
			}
			return TRUE;
		}

	case LRG_PHYSICS_SHAPE_KIND_PLANE:
		{
			gfloat dist;
			gfloat den;

			dist = v3_dot (self->plane_n, o) - self->plane_d;
			if (dist <= 0.0f)
			{
				set_hit (hit, o, d, 0.0f, neg);
				return TRUE;
			}
			den = v3_dot (self->plane_n, d);
			if (den >= 0.0f)
				return FALSE;
			t = -dist / den;
			if (t > max_fraction)
				return FALSE;
			set_hit (hit, o, d, t, self->plane_n);
			return TRUE;
		}

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * Debug mesh
 * ========================================================================== */

static void
push_tri (GArray       *a,
          const gfloat *p0,
          const gfloat *p1,
          const gfloat *p2)
{
	g_array_append_vals (a, p0, 3);
	g_array_append_vals (a, p1, 3);
	g_array_append_vals (a, p2, 3);
}

static void
push_quad (GArray       *a,
           const gfloat *p0,
           const gfloat *p1,
           const gfloat *p2,
           const gfloat *p3)
{
	push_tri (a, p0, p1, p2);
	push_tri (a, p0, p2, p3);
}

/* A latitude/longitude sphere (or capsule when @h > 0: the hemispheres
 * are split apart by +-h with a band between them). */
static void
debug_rounded (GArray *a,
               gfloat  r,
               gfloat  h,
               guint   seg)
{
	guint rings;
	guint i;
	guint j;

	rings = MAX (seg / 2, 2u);
	if (rings % 2 == 1)
		rings++;
	for (i = 0; i < rings; i++)
	{
		gfloat la0;
		gfloat la1;
		gfloat y0;
		gfloat y1;

		la0 = G_PI * ((gfloat)i / (gfloat)rings - 0.5f);
		la1 = G_PI * ((gfloat)(i + 1) / (gfloat)rings - 0.5f);
		y0 = r * sinf (la0) + (i < rings / 2 ? -h : h);
		y1 = r * sinf (la1) + (i + 1 <= rings / 2 ? -h : h);
		for (j = 0; j < seg; j++)
		{
			gfloat lo0;
			gfloat lo1;
			gfloat p00[3];
			gfloat p01[3];
			gfloat p10[3];
			gfloat p11[3];

			lo0 = 2.0f * G_PI * (gfloat)j / (gfloat)seg;
			lo1 = 2.0f * G_PI * (gfloat)(j + 1) / (gfloat)seg;
			v3_set (p00, r * cosf (la0) * cosf (lo0), y0, -r * cosf (la0) * sinf (lo0));
			v3_set (p01, r * cosf (la0) * cosf (lo1), y0, -r * cosf (la0) * sinf (lo1));
			v3_set (p10, r * cosf (la1) * cosf (lo0), y1, -r * cosf (la1) * sinf (lo0));
			v3_set (p11, r * cosf (la1) * cosf (lo1), y1, -r * cosf (la1) * sinf (lo1));
			push_quad (a, p00, p01, p11, p10);
		}
		/* Band between the hemispheres of a capsule. */
		if (h > 0.0f && i + 1 == rings / 2)
			for (j = 0; j < seg; j++)
			{
				gfloat lo0;
				gfloat lo1;
				gfloat p0[3];
				gfloat p1[3];
				gfloat q0[3];
				gfloat q1[3];

				lo0 = 2.0f * G_PI * (gfloat)j / (gfloat)seg;
				lo1 = 2.0f * G_PI * (gfloat)(j + 1) / (gfloat)seg;
				v3_set (p0, r * cosf (lo0), -h, -r * sinf (lo0));
				v3_set (p1, r * cosf (lo1), -h, -r * sinf (lo1));
				v3_set (q0, r * cosf (lo0), h, -r * sinf (lo0));
				v3_set (q1, r * cosf (lo1), h, -r * sinf (lo1));
				push_quad (a, p0, p1, q1, q0);
			}
	}
}

static gboolean
debug_tri_cb (guint         index,
              const gfloat *v,
              guint         material,
              guint         user_id,
              guint         active_edges,
              gpointer      user_data)
{
	(void)index;
	(void)material;
	(void)user_id;
	(void)active_edges;
	push_tri ((GArray *)user_data, &v[0], &v[3], &v[6]);
	return TRUE;
}

GArray *
lrg_physics_shape_build_debug_mesh (LrgPhysicsShape *self,
                                    guint            segments)
{
	GArray *a;
	guint i;
	guint j;

	g_return_val_if_fail (LRG_IS_PHYSICS_SHAPE (self), NULL);
	a = g_array_new (FALSE, FALSE, sizeof (gfloat));
	segments = MAX (segments, 3u);

	switch (self->kind)
	{
	case LRG_PHYSICS_SHAPE_KIND_SPHERE:
		debug_rounded (a, self->radius, 0.0f, segments);
		break;
	case LRG_PHYSICS_SHAPE_KIND_CAPSULE:
		debug_rounded (a, self->radius, self->half_height, segments);
		break;
	case LRG_PHYSICS_SHAPE_KIND_CYLINDER:
		for (j = 0; j < segments; j++)
		{
			gfloat a0;
			gfloat a1;
			gfloat b0[3];
			gfloat b1[3];
			gfloat t0[3];
			gfloat t1[3];
			gfloat ct[3];
			gfloat cb[3];

			a0 = 2.0f * G_PI * (gfloat)j / (gfloat)segments;
			a1 = 2.0f * G_PI * (gfloat)(j + 1) / (gfloat)segments;
			v3_set (b0, self->radius * cosf (a0), -self->half_height, -self->radius * sinf (a0));
			v3_set (b1, self->radius * cosf (a1), -self->half_height, -self->radius * sinf (a1));
			v3_set (t0, b0[0], self->half_height, b0[2]);
			v3_set (t1, b1[0], self->half_height, b1[2]);
			v3_set (ct, 0.0f, self->half_height, 0.0f);
			v3_set (cb, 0.0f, -self->half_height, 0.0f);
			push_quad (a, b0, b1, t1, t0);
			push_tri (a, ct, t0, t1);
			push_tri (a, cb, b1, b0);
		}
		break;
	case LRG_PHYSICS_SHAPE_KIND_BOX:
		{
			static const gfloat dirs[6][3] = {
				{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
			};

			for (i = 0; i < 6; i++)
			{
				gfloat f[LRG_SHAPE_MAX_FACE_POINTS * 3];
				guint id;

				lrg_physics_shape_supporting_face (self, dirs[i], f, &id);
				push_quad (a, &f[0], &f[3], &f[6], &f[9]);
			}
		}
		break;
	case LRG_PHYSICS_SHAPE_KIND_CONVEX_HULL:
		for (i = 0; i < self->n_faces; i++)
		{
			const guint *fi;
			const gfloat *pl;

			fi = &self->face_indices[self->face_start[i]];
			pl = &self->face_planes[i * 4];
			for (j = 1; j + 1 < self->face_count[i]; j++)
			{
				gfloat p0[3];
				gfloat p1[3];
				gfloat p2[3];

				v3_madd (p0, &self->hull_verts[fi[0] * 3], pl, self->radius);
				v3_madd (p1, &self->hull_verts[fi[j] * 3], pl, self->radius);
				v3_madd (p2, &self->hull_verts[fi[j + 1] * 3], pl, self->radius);
				push_tri (a, p0, p1, p2);
			}
		}
		break;
	case LRG_PHYSICS_SHAPE_KIND_TRIANGLE_MESH:
	case LRG_PHYSICS_SHAPE_KIND_HEIGHTFIELD:
		lrg_physics_shape_foreach_triangle (self, self->bmin, self->bmax, debug_tri_cb, a);
		break;
	case LRG_PHYSICS_SHAPE_KIND_COMPOUND:
		for (i = 0; i < self->children->len; i++)
		{
			const LrgShapeChild *c;
			g_autoptr(GArray) sub = NULL;

			c = &g_array_index (self->children, LrgShapeChild, i);
			sub = lrg_physics_shape_build_debug_mesh (c->shape, segments);
			for (j = 0; j + 3 <= sub->len; j += 3)
			{
				gfloat p[3];

				lrg_math3d_transform_point (c->xf, &g_array_index (sub, gfloat, j), p);
				g_array_append_vals (a, p, 3);
			}
		}
		break;
	case LRG_PHYSICS_SHAPE_KIND_PLANE:
		{
			/* Square on the plane around the point closest to the origin. */
			gfloat c[3];
			gfloat u[3];
			gfloat v[3];
			gfloat tmp[3];
			gfloat p[4][3];
			gfloat e;

			e = LRG_PHYSICS_PLANE_HALF_EXTENT;
			v3_set (c, self->plane_n[0] * self->plane_d, self->plane_n[1] * self->plane_d,
			        self->plane_n[2] * self->plane_d);
			if (fabsf (self->plane_n[0]) < 0.9f)
				v3_set (tmp, 1.0f, 0.0f, 0.0f);
			else
				v3_set (tmp, 0.0f, 0.0f, 1.0f);
			v3_cross (u, tmp, self->plane_n);
			v3_normalize (u, u, tmp);
			v3_cross (v, self->plane_n, u);
			for (i = 0; i < 4; i++)
			{
				gfloat su;
				gfloat sv;

				su = (i == 0 || i == 3) ? -e : e;
				sv = (i < 2) ? -e : e;
				p[i][0] = c[0] + u[0] * su + v[0] * sv;
				p[i][1] = c[1] + u[1] * su + v[1] * sv;
				p[i][2] = c[2] + u[2] * su + v[2] * sv;
			}
			push_quad (a, p[0], p[1], p[2], p[3]);
		}
		break;
	default:
		break;
	}
	return a;
}
