/* lrg-tree-mesh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Procedural low-poly trees. Every part is built from a handful of
 * primitives (prisms, frustums, jittered icosahedra, two-sided leaf
 * strips) emitted as unshared, flat-shaded triangles; every random
 * choice comes from one xorshift stream seeded by the tree's seed and
 * shape, so a tree is fully reproducible.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-tree-mesh.h"
#include "graphics/lrg-hash-noise-private.h"

/* the foliage of every shape stays within this share of the height
 * from the axis (documented in lrg_tree_mesh_new()) */
#define TREE_REACH_TENTHS (6)
#define TREE_REACH ((gfloat)TREE_REACH_TENTHS / 10.0f)
#define TREE_SIDES (6)

G_DEFINE_ENUM_TYPE (LrgTreeShape, lrg_tree_shape,
    G_DEFINE_ENUM_VALUE (LRG_TREE_SHAPE_CONIFER, "conifer"),
    G_DEFINE_ENUM_VALUE (LRG_TREE_SHAPE_BROADLEAF, "broadleaf"),
    G_DEFINE_ENUM_VALUE (LRG_TREE_SHAPE_PALM, "palm"),
    G_DEFINE_ENUM_VALUE (LRG_TREE_SHAPE_DEAD, "dead"),
    G_DEFINE_ENUM_VALUE (LRG_TREE_SHAPE_SHRUB, "shrub"))

struct _LrgTreeMesh
{
    GObject       parent_instance;

    LrgTreeShape  shape;
    guint32       seed;
    gfloat        height;
    guint8        trunk[4];
    guint8        foliage[4];

    GArray       *positions;   /* gfloat x3 */
    GArray       *normals;     /* gfloat x3 */
    GArray       *colors;      /* guint8 x4 */
    GArray       *indices;     /* guint32 */
    gfloat        min[3];
    gfloat        max[3];

    guint32       rng;         /* generation stream */
};

G_DEFINE_FINAL_TYPE (LrgTreeMesh, lrg_tree_mesh, G_TYPE_OBJECT)

/* default colours: bark, and the leaves of each shape */
static const guint8 default_trunk[4] = { 92, 64, 42, 255 };
static const guint8 default_foliage[5][4] = {
    { 40, 82, 46, 255 },     /* conifer */
    { 66, 114, 50, 255 },    /* broadleaf */
    { 78, 126, 56, 255 },    /* palm */
    { 104, 92, 80, 255 },    /* dead (its branches) */
    { 62, 98, 46, 255 }      /* shrub */
};

static void
lrg_tree_mesh_finalize (GObject *object)
{
    LrgTreeMesh *self = LRG_TREE_MESH (object);

    g_clear_pointer (&self->positions, g_array_unref);
    g_clear_pointer (&self->normals, g_array_unref);
    g_clear_pointer (&self->colors, g_array_unref);
    g_clear_pointer (&self->indices, g_array_unref);

    G_OBJECT_CLASS (lrg_tree_mesh_parent_class)->finalize (object);
}

static void
lrg_tree_mesh_class_init (LrgTreeMeshClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_tree_mesh_finalize;
}

static void
lrg_tree_mesh_init (LrgTreeMesh *self)
{
    self->positions = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->normals = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->colors = g_array_new (FALSE, FALSE, sizeof (guint8));
    self->indices = g_array_new (FALSE, FALSE, sizeof (guint32));
}

/* --- emitting --- */

static gfloat
rnd (LrgTreeMesh *self,
     gfloat       lo,
     gfloat       hi)
{
    return lo + (hi - lo) * lrg_hash_next_unit (&self->rng);
}

/* one flat-shaded triangle in @rgba, its shade varied a little */
static void
emit_tri (LrgTreeMesh  *self,
          const gfloat *a,
          const gfloat *b,
          const gfloat *c,
          const guint8 *rgba)
{
    gfloat u[3];
    gfloat v[3];
    gfloat n[3];
    gfloat len;
    gfloat shade;
    guint8 col[4];
    guint32 base;
    guint k;

    for (k = 0; k < 3; k++)
    {
        u[k] = b[k] - a[k];
        v[k] = c[k] - a[k];
    }
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
    len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(len > 1e-12f))
        return;
    for (k = 0; k < 3; k++)
        n[k] /= len;

    /* +-12 % per face: the facets read without a texture */
    shade = rnd (self, 0.88f, 1.12f);
    for (k = 0; k < 3; k++)
        col[k] = (guint8)CLAMP ((gfloat)rgba[k] * shade, 0.0f, 255.0f);
    col[3] = rgba[3];

    base = self->positions->len / 3;
    g_array_append_vals (self->positions, a, 3);
    g_array_append_vals (self->positions, b, 3);
    g_array_append_vals (self->positions, c, 3);
    for (k = 0; k < 3; k++)
    {
        guint32 index;

        g_array_append_vals (self->normals, n, 3);
        g_array_append_vals (self->colors, col, 4);
        index = base + k;
        g_array_append_val (self->indices, index);
    }
}

/* a triangle wound so that its normal points along @out */
static void
emit_tri_out (LrgTreeMesh  *self,
              const gfloat *a,
              const gfloat *b,
              const gfloat *c,
              const gfloat *out,
              const guint8 *rgba)
{
    gfloat n[3];

    n[0] = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
    n[1] = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
    n[2] = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    if (n[0] * out[0] + n[1] * out[1] + n[2] * out[2] < 0.0f)
        emit_tri (self, a, c, b, rgba);
    else
        emit_tri (self, a, b, c, rgba);
}

static void
emit_quad (LrgTreeMesh  *self,
           const gfloat *a,
           const gfloat *b,
           const gfloat *c,
           const gfloat *d,
           const guint8 *rgba)
{
    emit_tri (self, a, b, c, rgba);
    emit_tri (self, a, c, d, rgba);
}

/* a ring point: centre (cx, y, cz), radius r, angle a */
static void
ring_point (gfloat  cx,
            gfloat  y,
            gfloat  cz,
            gfloat  r,
            gfloat  a,
            gfloat *out)
{
    out[0] = cx + r * cosf (a);
    out[1] = y;
    out[2] = cz + r * sinf (a);
}

/*
 * emit_frustum:
 * A closed-sided frustum from the ring (b, r0) to (t, r1), r1 may be 0
 * (a cone); with @cap the bottom is closed too (cones of foliage seen
 * from below).  @twist rotates the ring.
 */
static void
emit_frustum (LrgTreeMesh  *self,
              const gfloat *b,
              gfloat        r0,
              const gfloat *t,
              gfloat        r1,
              guint         sides,
              gfloat        twist,
              gboolean      cap,
              const guint8 *rgba)
{
    guint i;

    for (i = 0; i < sides; i++)
    {
        gfloat a0;
        gfloat a1;
        gfloat p0[3];
        gfloat p1[3];
        gfloat q0[3];
        gfloat q1[3];

        a0 = twist + (gfloat)(2.0 * G_PI) * (gfloat)i / (gfloat)sides;
        a1 = twist + (gfloat)(2.0 * G_PI) * (gfloat)(i + 1) / (gfloat)sides;
        ring_point (b[0], b[1], b[2], r0, a0, p0);
        ring_point (b[0], b[1], b[2], r0, a1, p1);
        ring_point (t[0], t[1], t[2], r1, a0, q0);
        ring_point (t[0], t[1], t[2], r1, a1, q1);
        /* wound to face away from the axis, the cap downwards */
        {
            gfloat am;
            gfloat out[3];
            static const gfloat down[3] = { 0.0f, -1.0f, 0.0f };

            am = (a0 + a1) * 0.5f;
            out[0] = cosf (am);
            out[1] = MAX (r0 - r1, 0.0f) / MAX (t[1] - b[1], 1e-6f);
            out[2] = sinf (am);
            if (r1 > 0.0f)
            {
                emit_tri_out (self, p0, q0, q1, out, rgba);
                emit_tri_out (self, p0, q1, p1, out, rgba);
            }
            else
            {
                emit_tri_out (self, p0, t, p1, out, rgba);
            }
            if (cap)
                emit_tri_out (self, b, p0, p1, down, rgba);
        }
    }
}

/* a jittered icosahedron: a rounded clump of foliage */
static void
emit_blob (LrgTreeMesh  *self,
           const gfloat *centre,
           gfloat        rx,
           gfloat        ry,
           gfloat        rz,
           const guint8 *rgba)
{
    static const gint faces[20][3] = {
        { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
        { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
        { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
        { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 }
    };
    gfloat t;
    gfloat base[12][3];
    gfloat v[12][3];
    guint i;

    t = (gfloat)((1.0 + sqrt (5.0)) / 2.0);
    base[0][0] = -1; base[0][1] = t; base[0][2] = 0;
    base[1][0] = 1; base[1][1] = t; base[1][2] = 0;
    base[2][0] = -1; base[2][1] = -t; base[2][2] = 0;
    base[3][0] = 1; base[3][1] = -t; base[3][2] = 0;
    base[4][0] = 0; base[4][1] = -1; base[4][2] = t;
    base[5][0] = 0; base[5][1] = 1; base[5][2] = t;
    base[6][0] = 0; base[6][1] = -1; base[6][2] = -t;
    base[7][0] = 0; base[7][1] = 1; base[7][2] = -t;
    base[8][0] = t; base[8][1] = 0; base[8][2] = -1;
    base[9][0] = t; base[9][1] = 0; base[9][2] = 1;
    base[10][0] = -t; base[10][1] = 0; base[10][2] = -1;
    base[11][0] = -t; base[11][1] = 0; base[11][2] = 1;
    for (i = 0; i < 12; i++)
    {
        gfloat len;
        gfloat j;

        len = sqrtf (base[i][0] * base[i][0] + base[i][1] * base[i][1] + base[i][2] * base[i][2]);
        /* shrink only, so the clump stays inside its radii */
        j = rnd (self, 0.8f, 1.0f);
        v[i][0] = centre[0] + base[i][0] / len * rx * j;
        v[i][1] = centre[1] + base[i][1] / len * ry * j;
        v[i][2] = centre[2] + base[i][2] / len * rz * j;
    }
    for (i = 0; i < 20; i++)
    {
        gfloat out[3];
        guint k;

        /* outward: from the centre through the face's middle */
        for (k = 0; k < 3; k++)
            out[k] = (v[faces[i][0]][k] + v[faces[i][1]][k] + v[faces[i][2]][k]) / 3.0f - centre[k];
        emit_tri_out (self, v[faces[i][0]], v[faces[i][1]], v[faces[i][2]], out, rgba);
    }
}

/* --- shapes --- */

static void
build_conifer (LrgTreeMesh *self)
{
    gfloat h;
    gfloat b[3] = { 0.0f, 0.0f, 0.0f };
    gfloat t[3] = { 0.0f, 0.0f, 0.0f };
    gfloat y;
    guint tier;

    h = self->height;
    t[1] = h * 0.35f;
    emit_frustum (self, b, h * 0.045f, t, h * 0.03f, TREE_SIDES - 1, 0.0f, FALSE, self->trunk);

    /* three tiers, each narrower and overlapping the one below */
    y = h * rnd (self, 0.18f, 0.24f);
    for (tier = 0; tier < 3; tier++)
    {
        gfloat radius;
        gfloat top;
        gfloat cb[3];
        gfloat ct[3];

        radius = h * (0.32f - 0.08f * (gfloat)tier) * rnd (self, 0.85f, 1.0f);
        top = tier == 2 ? h : y + (h - y) * rnd (self, 0.55f, 0.65f);
        cb[0] = 0.0f; cb[1] = y; cb[2] = 0.0f;
        ct[0] = 0.0f; ct[1] = top; ct[2] = 0.0f;
        emit_frustum (self, cb, radius, ct, 0.0f, TREE_SIDES + 1, rnd (self, 0.0f, 1.0f), TRUE,
                      self->foliage);
        y = y + (top - y) * 0.45f;
    }
}

static void
build_broadleaf (LrgTreeMesh *self)
{
    gfloat h;
    gfloat b[3] = { 0.0f, 0.0f, 0.0f };
    gfloat t[3];
    guint blobs;
    guint i;

    h = self->height;
    t[0] = h * rnd (self, -0.03f, 0.03f);
    t[1] = h * 0.5f;
    t[2] = h * rnd (self, -0.03f, 0.03f);
    emit_frustum (self, b, h * 0.05f, t, h * 0.035f, TREE_SIDES - 1, 0.0f, FALSE, self->trunk);

    /* the crown: one big clump reaching the top, one or two beside it */
    {
        gfloat c[3];

        c[0] = t[0];
        c[1] = h * 0.7f;
        c[2] = t[2];
        emit_blob (self, c, h * 0.36f, h * 0.3f, h * 0.36f, self->foliage);
    }
    blobs = 1 + (lrg_hash_next (&self->rng) % 2u);
    for (i = 0; i < blobs; i++)
    {
        gfloat a;
        gfloat c[3];

        a = rnd (self, 0.0f, (gfloat)(2.0 * G_PI));
        c[0] = t[0] + cosf (a) * h * 0.16f;
        c[1] = h * rnd (self, 0.55f, 0.65f);
        c[2] = t[2] + sinf (a) * h * 0.16f;
        emit_blob (self, c, h * 0.24f, h * 0.2f, h * 0.24f, self->foliage);
    }
}

static void
build_palm (LrgTreeMesh *self)
{
    gfloat h;
    gfloat lean;
    gfloat heading;
    gfloat prev[3] = { 0.0f, 0.0f, 0.0f };
    gfloat top[3];
    guint fronds;
    guint seg;
    guint i;

    h = self->height;
    lean = h * rnd (self, 0.04f, 0.1f);
    heading = rnd (self, 0.0f, (gfloat)(2.0 * G_PI));

    /* four segments bending further from the axis as they rise */
    for (seg = 1; seg <= 4; seg++)
    {
        gfloat s;
        gfloat p[3];

        s = (gfloat)seg / 4.0f;
        p[0] = cosf (heading) * lean * s * s;
        p[1] = h * 0.88f * s;
        p[2] = sinf (heading) * lean * s * s;
        emit_frustum (self, prev, h * (0.035f - 0.006f * (gfloat)seg), p,
                      h * (0.035f - 0.006f * (gfloat)(seg + 1)), TREE_SIDES - 1, 0.0f, FALSE,
                      self->trunk);
        memcpy (prev, p, sizeof (prev));
    }
    memcpy (top, prev, sizeof (top));

    /* fronds: two-sided strips arching out and down */
    fronds = 6 + (lrg_hash_next (&self->rng) % 3u);
    for (i = 0; i < fronds; i++)
    {
        gfloat a;
        gfloat len;
        gfloat width;
        gfloat dir[2];
        gfloat side[2];
        gfloat last_l[3];
        gfloat last_r[3];
        guint k;

        a = (gfloat)(2.0 * G_PI) * (gfloat)i / (gfloat)fronds + rnd (self, -0.2f, 0.2f);
        len = h * rnd (self, 0.32f, 0.42f);
        width = h * 0.06f;
        dir[0] = cosf (a);
        dir[1] = sinf (a);
        side[0] = -dir[1];
        side[1] = dir[0];
        memcpy (last_l, top, sizeof (last_l));
        memcpy (last_r, top, sizeof (last_r));
        for (k = 1; k <= 3; k++)
        {
            gfloat s;
            gfloat w;
            gfloat c[3];
            gfloat l[3];
            gfloat r[3];

            s = (gfloat)k / 3.0f;
            /* half width in the middle, a point at the tip */
            w = width * (k == 3 ? 0.0f : (k == 1 ? 1.0f : 0.7f));
            c[0] = top[0] + dir[0] * len * s;
            c[1] = top[1] + h * (0.06f * s - 0.2f * s * s);
            c[2] = top[2] + dir[1] * len * s;
            l[0] = c[0] + side[0] * w; l[1] = c[1]; l[2] = c[2] + side[1] * w;
            r[0] = c[0] - side[0] * w; r[1] = c[1]; r[2] = c[2] - side[1] * w;
            /* both faces, so the frond shows from below */
            emit_quad (self, last_l, l, r, last_r, self->foliage);
            emit_quad (self, last_r, r, l, last_l, self->foliage);
            memcpy (last_l, l, sizeof (last_l));
            memcpy (last_r, r, sizeof (last_r));
        }
    }
}

static void
build_dead (LrgTreeMesh *self)
{
    gfloat h;
    gfloat b[3] = { 0.0f, 0.0f, 0.0f };
    gfloat t[3];
    guint branches;
    guint i;

    h = self->height;
    t[0] = h * rnd (self, -0.05f, 0.05f);
    t[1] = h;
    t[2] = h * rnd (self, -0.05f, 0.05f);
    emit_frustum (self, b, h * 0.05f, t, h * 0.008f, TREE_SIDES - 1, 0.0f, FALSE, self->trunk);

    branches = 3 + (lrg_hash_next (&self->rng) % 3u);
    for (i = 0; i < branches; i++)
    {
        gfloat s;
        gfloat a;
        gfloat len;
        gfloat from[3];
        gfloat to[3];

        s = rnd (self, 0.35f, 0.8f);
        a = rnd (self, 0.0f, (gfloat)(2.0 * G_PI));
        len = h * rnd (self, 0.18f, 0.32f);
        from[0] = t[0] * s;
        from[1] = h * s;
        from[2] = t[2] * s;
        to[0] = from[0] + cosf (a) * len;
        to[1] = MIN (from[1] + len * rnd (self, 0.3f, 0.8f), h);
        to[2] = from[2] + sinf (a) * len;
        emit_frustum (self, from, h * 0.018f, to, h * 0.004f, 4, 0.0f, FALSE, self->foliage);
    }
}

static void
build_shrub (LrgTreeMesh *self)
{
    gfloat h;
    guint clumps;
    guint i;

    h = self->height;
    clumps = 2 + (lrg_hash_next (&self->rng) % 2u);
    for (i = 0; i < clumps; i++)
    {
        gfloat a;
        gfloat r;
        gfloat c[3];
        gfloat ry;

        a = rnd (self, 0.0f, (gfloat)(2.0 * G_PI));
        r = i == 0 ? 0.0f : h * rnd (self, 0.12f, 0.22f);
        ry = h * (i == 0 ? 0.5f : 0.35f);
        c[0] = cosf (a) * r;
        c[1] = ry;
        c[2] = sinf (a) * r;
        emit_blob (self, c, h * 0.34f, ry, h * 0.34f, self->foliage);
    }
}

/* regenerates the geometry and its bounds from the stored parameters */
static void
build (LrgTreeMesh *self)
{
    guint n;
    guint i;
    guint k;

    g_array_set_size (self->positions, 0);
    g_array_set_size (self->normals, 0);
    g_array_set_size (self->colors, 0);
    g_array_set_size (self->indices, 0);
    self->rng = lrg_hash_mix (self->seed * 0x9e3779b1U + (guint32)self->shape * 0x85ebca77U + 1u);

    switch (self->shape)
    {
    case LRG_TREE_SHAPE_BROADLEAF:
        build_broadleaf (self);
        break;
    case LRG_TREE_SHAPE_PALM:
        build_palm (self);
        break;
    case LRG_TREE_SHAPE_DEAD:
        build_dead (self);
        break;
    case LRG_TREE_SHAPE_SHRUB:
        build_shrub (self);
        break;
    case LRG_TREE_SHAPE_CONIFER:
    default:
        build_conifer (self);
        break;
    }

    n = self->positions->len / 3;
    for (k = 0; k < 3; k++)
    {
        self->min[k] = n > 0 ? G_MAXFLOAT : 0.0f;
        self->max[k] = n > 0 ? -G_MAXFLOAT : 0.0f;
    }
    for (i = 0; i < n; i++)
    {
        const gfloat *p = &g_array_index (self->positions, gfloat, i * 3);

        for (k = 0; k < 3; k++)
        {
            self->min[k] = MIN (self->min[k], p[k]);
            self->max[k] = MAX (self->max[k], p[k]);
        }
    }
}

LrgTreeMesh *
lrg_tree_mesh_new (LrgTreeShape shape,
                   guint32      seed,
                   gfloat       height)
{
    LrgTreeMesh *self;

    self = g_object_new (LRG_TYPE_TREE_MESH, NULL);
    self->shape = (gint)shape >= (gint)LRG_TREE_SHAPE_CONIFER && (gint)shape <= (gint)LRG_TREE_SHAPE_SHRUB ?
                  shape : LRG_TREE_SHAPE_CONIFER;
    self->seed = seed;
    self->height = isfinite (height) && height > 0.0f ? height : 1.0f;
    memcpy (self->trunk, default_trunk, sizeof (self->trunk));
    memcpy (self->foliage, default_foliage[self->shape], sizeof (self->foliage));
    build (self);
    return self;
}

void
lrg_tree_mesh_set_colors (LrgTreeMesh    *self,
                          const GrlColor *trunk,
                          const GrlColor *foliage)
{
    g_return_if_fail (LRG_IS_TREE_MESH (self));

    if (trunk != NULL)
    {
        self->trunk[0] = trunk->r;
        self->trunk[1] = trunk->g;
        self->trunk[2] = trunk->b;
        self->trunk[3] = trunk->a;
    }
    else
    {
        memcpy (self->trunk, default_trunk, sizeof (self->trunk));
    }
    if (foliage != NULL)
    {
        self->foliage[0] = foliage->r;
        self->foliage[1] = foliage->g;
        self->foliage[2] = foliage->b;
        self->foliage[3] = foliage->a;
    }
    else
    {
        memcpy (self->foliage, default_foliage[self->shape], sizeof (self->foliage));
    }
    build (self);
}

LrgTreeShape
lrg_tree_mesh_get_shape (LrgTreeMesh *self)
{
    g_return_val_if_fail (LRG_IS_TREE_MESH (self), LRG_TREE_SHAPE_CONIFER);
    return self->shape;
}

gfloat
lrg_tree_mesh_get_height (LrgTreeMesh *self)
{
    g_return_val_if_fail (LRG_IS_TREE_MESH (self), 0.0f);
    return self->height;
}

guint
lrg_tree_mesh_get_vertex_count (LrgTreeMesh *self)
{
    g_return_val_if_fail (LRG_IS_TREE_MESH (self), 0);
    return self->positions->len / 3;
}

guint
lrg_tree_mesh_get_triangle_count (LrgTreeMesh *self)
{
    g_return_val_if_fail (LRG_IS_TREE_MESH (self), 0);
    return self->indices->len / 3;
}

const gfloat *
lrg_tree_mesh_get_positions (LrgTreeMesh *self,
                             guint       *n_floats)
{
    g_return_val_if_fail (LRG_IS_TREE_MESH (self), NULL);
    if (n_floats != NULL)
        *n_floats = self->positions->len;
    return (const gfloat *)self->positions->data;
}

const gfloat *
lrg_tree_mesh_get_normals (LrgTreeMesh *self,
                           guint       *n_floats)
{
    g_return_val_if_fail (LRG_IS_TREE_MESH (self), NULL);
    if (n_floats != NULL)
        *n_floats = self->normals->len;
    return (const gfloat *)self->normals->data;
}

const guint8 *
lrg_tree_mesh_get_colors (LrgTreeMesh *self,
                          guint       *n_bytes)
{
    g_return_val_if_fail (LRG_IS_TREE_MESH (self), NULL);
    if (n_bytes != NULL)
        *n_bytes = self->colors->len;
    return (const guint8 *)self->colors->data;
}

const guint32 *
lrg_tree_mesh_get_indices (LrgTreeMesh *self,
                           guint       *n_indices)
{
    g_return_val_if_fail (LRG_IS_TREE_MESH (self), NULL);
    if (n_indices != NULL)
        *n_indices = self->indices->len;
    return (const guint32 *)self->indices->data;
}

void
lrg_tree_mesh_get_bounds (LrgTreeMesh *self,
                          gfloat      *out_min,
                          gfloat      *out_max)
{
    g_return_if_fail (LRG_IS_TREE_MESH (self));
    g_return_if_fail (out_min != NULL && out_max != NULL);
    memcpy (out_min, self->min, sizeof (self->min));
    memcpy (out_max, self->max, sizeof (self->max));
}

gboolean
lrg_tree_mesh_add_to_batch (LrgTreeMesh     *self,
                            LrgStaticBatch  *batch,
                            const GrlMatrix *transform,
                            guint            material,
                            guint            layer,
                            GError         **error)
{
    g_return_val_if_fail (LRG_IS_TREE_MESH (self), FALSE);
    g_return_val_if_fail (LRG_IS_STATIC_BATCH (batch), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    return lrg_static_batch_add_mesh (batch, (const gfloat *)self->positions->data,
                                      (const gfloat *)self->normals->data, NULL,
                                      (const guint8 *)self->colors->data,
                                      self->positions->len / 3,
                                      (const guint32 *)self->indices->data, self->indices->len,
                                      transform, material, layer, error);
}

/* the reach is part of the documented contract (tests check it); asserted
 * on the integer tenths: a floating comparison is not an integer constant
 * expression, which older GCCs (13) reject in G_STATIC_ASSERT under gnu89 */
G_STATIC_ASSERT (TREE_REACH_TENTHS > 5);
