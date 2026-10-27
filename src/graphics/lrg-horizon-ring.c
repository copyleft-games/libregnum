/* lrg-horizon-ring.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The distant-terrain ring. Vertex (s, j) sits at angle 2 pi s / S and
 * radius inner * (outer / inner)^(j / rings); the sampler gives its
 * height and colour; normals come from the neighbours along both grid
 * directions. The ring wraps round (segment S is segment 0), so the
 * mesh has S * (rings + 1) vertices and 2 S rings triangles.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-horizon-ring.h"
#include "core/lrg-prewarm.h"

struct _LrgHorizonRing
{
    GObject         parent_instance;

    gfloat          inner;
    gfloat          outer;
    guint           segments;
    guint           rings;
    gfloat          sink;

    gboolean        built;
    gdouble         centre[2];
    guint           n_vertices;
    guint           n_triangles;
    LrgStaticBatch *batch;
};

static void lrg_horizon_ring_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgHorizonRing, lrg_horizon_ring, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                      lrg_horizon_ring_prewarmable_init))

static void
lrg_horizon_ring_finalize (GObject *object)
{
    LrgHorizonRing *self = LRG_HORIZON_RING (object);

    g_clear_object (&self->batch);
    G_OBJECT_CLASS (lrg_horizon_ring_parent_class)->finalize (object);
}

static void
lrg_horizon_ring_class_init (LrgHorizonRingClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_horizon_ring_finalize;
}

static void
lrg_horizon_ring_init (LrgHorizonRing *self)
{
    self->inner = 1000.0f;
    self->outer = 8000.0f;
    self->segments = 96;
    self->rings = 12;
    self->batch = lrg_static_batch_new ();

    /* build the GPU resources behind a loading screen, not on the
     * first draw (see lrg_prewarm_track()) */
    lrg_prewarm_track (LRG_PREWARMABLE (self));
}

LrgHorizonRing *
lrg_horizon_ring_new (void)
{
    return g_object_new (LRG_TYPE_HORIZON_RING, NULL);
}

void
lrg_horizon_ring_set_radii (LrgHorizonRing *self,
                            gfloat          inner,
                            gfloat          outer)
{
    g_return_if_fail (LRG_IS_HORIZON_RING (self));
    if (!(inner > 0.0f) || !(outer > inner) || !isfinite (outer))
        return;
    self->inner = inner;
    self->outer = outer;
}

void
lrg_horizon_ring_get_radii (LrgHorizonRing *self,
                            gfloat         *out_inner,
                            gfloat         *out_outer)
{
    g_return_if_fail (LRG_IS_HORIZON_RING (self));
    if (out_inner != NULL)
        *out_inner = self->inner;
    if (out_outer != NULL)
        *out_outer = self->outer;
}

void
lrg_horizon_ring_set_resolution (LrgHorizonRing *self,
                                 guint           segments,
                                 guint           rings)
{
    g_return_if_fail (LRG_IS_HORIZON_RING (self));
    self->segments = CLAMP (segments, 8u, 1024u);
    self->rings = CLAMP (rings, 1u, 64u);
}

void
lrg_horizon_ring_set_sink (LrgHorizonRing *self,
                           gfloat          sink)
{
    g_return_if_fail (LRG_IS_HORIZON_RING (self));
    self->sink = isfinite (sink) ? MAX (sink, 0.0f) : 0.0f;
}

gboolean
lrg_horizon_ring_build (LrgHorizonRing        *self,
                        gdouble                centre_x,
                        gdouble                centre_z,
                        gdouble                offset_x,
                        gdouble                offset_y,
                        gdouble                offset_z,
                        LrgHorizonSampleFunc   func,
                        gpointer               user_data,
                        GError               **error)
{
    g_autofree gfloat *positions = NULL;
    g_autofree gfloat *normals = NULL;
    g_autofree guint8 *colors = NULL;
    g_autofree guint32 *indices = NULL;
    guint8 last[4] = { 128, 128, 128, 255 };
    guint ring_n;
    guint n;
    guint s;
    guint j;
    guint t;

    g_return_val_if_fail (LRG_IS_HORIZON_RING (self), FALSE);
    g_return_val_if_fail (func != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    ring_n = self->rings + 1;
    n = self->segments * ring_n;
    positions = g_new (gfloat, (gsize)n * 3);
    normals = g_new (gfloat, (gsize)n * 3);
    colors = g_new (guint8, (gsize)n * 4);
    indices = g_new (guint32, (gsize)self->segments * self->rings * 6);

    /* 1. positions and colours, ring by ring outwards */
    for (j = 0; j < ring_n; j++)
    {
        gdouble frac;
        gdouble radius;
        gfloat drop;

        frac = (gdouble)j / (gdouble)self->rings;
        radius = (gdouble)self->inner * pow ((gdouble)self->outer / (gdouble)self->inner, frac);
        drop = self->sink * (gfloat)MAX (0.0, 1.0 - 2.0 * frac);
        for (s = 0; s < self->segments; s++)
        {
            gdouble a;
            gdouble px;
            gdouble pz;
            gfloat h;
            gsize v;

            a = 2.0 * G_PI * (gdouble)s / (gdouble)self->segments;
            px = centre_x + radius * cos (a);
            pz = centre_z + radius * sin (a);
            v = (gsize)j * self->segments + s;
            h = 0.0f;
            if (func (px, pz, &h, &colors[v * 4], user_data))
                memcpy (last, &colors[v * 4], sizeof (last));
            else
            {
                h = (gfloat)offset_y;
                memcpy (&colors[v * 4], last, sizeof (last));
            }
            positions[v * 3 + 0] = (gfloat)(px - offset_x);
            positions[v * 3 + 1] = (gfloat)((gdouble)h - offset_y) - drop;
            positions[v * 3 + 2] = (gfloat)(pz - offset_z);
        }
    }

    /* 2. normals: cross of the around and outward differences */
    for (j = 0; j < ring_n; j++)
    {
        guint j0 = j > 0 ? j - 1 : j;
        guint j1 = j + 1 < ring_n ? j + 1 : j;

        for (s = 0; s < self->segments; s++)
        {
            const gfloat *pa;
            const gfloat *pb;
            const gfloat *po;
            const gfloat *pi;
            gfloat ta[3];
            gfloat tr[3];
            gfloat nn[3];
            gfloat len;
            gsize v;
            guint k;

            v = (gsize)j * self->segments + s;
            pa = &positions[((gsize)j * self->segments + (s + 1) % self->segments) * 3];
            pb = &positions[((gsize)j * self->segments + (s + self->segments - 1) % self->segments) * 3];
            po = &positions[((gsize)j1 * self->segments + s) * 3];
            pi = &positions[((gsize)j0 * self->segments + s) * 3];
            for (k = 0; k < 3; k++)
            {
                ta[k] = pa[k] - pb[k];
                tr[k] = po[k] - pi[k];
            }
            /* around (+X towards +Z) x outward points up */
            nn[0] = ta[1] * tr[2] - ta[2] * tr[1];
            nn[1] = ta[2] * tr[0] - ta[0] * tr[2];
            nn[2] = ta[0] * tr[1] - ta[1] * tr[0];
            len = sqrtf (nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            if (!(len > 1e-9f))
            {
                nn[0] = 0.0f;
                nn[1] = 1.0f;
                nn[2] = 0.0f;
                len = 1.0f;
            }
            if (nn[1] < 0.0f)
                len = -len;
            for (k = 0; k < 3; k++)
                normals[v * 3 + k] = nn[k] / len;
        }
    }

    /* 3. two triangles per cell */
    t = 0;
    for (j = 0; j < self->rings; j++)
    {
        for (s = 0; s < self->segments; s++)
        {
            guint32 a = j * self->segments + s;
            guint32 b = j * self->segments + (s + 1) % self->segments;
            guint32 c = (j + 1) * self->segments + s;
            guint32 d = (j + 1) * self->segments + (s + 1) % self->segments;

            /* counter-clockwise seen from above (+Y) */
            indices[t++] = a;
            indices[t++] = d;
            indices[t++] = c;
            indices[t++] = a;
            indices[t++] = b;
            indices[t++] = d;
        }
    }

    lrg_static_batch_clear (self->batch);
    if (!lrg_static_batch_add_mesh (self->batch, positions, normals, NULL, colors, n, indices, t,
                                    NULL, 0, 0, error))
    {
        self->built = FALSE;
        self->n_vertices = 0;
        self->n_triangles = 0;
        return FALSE;
    }
    self->built = TRUE;
    self->centre[0] = centre_x;
    self->centre[1] = centre_z;
    self->n_vertices = n;
    self->n_triangles = t / 3;
    return TRUE;
}

gboolean
lrg_horizon_ring_is_built (LrgHorizonRing *self)
{
    g_return_val_if_fail (LRG_IS_HORIZON_RING (self), FALSE);
    return self->built;
}

void
lrg_horizon_ring_get_centre (LrgHorizonRing *self,
                             gdouble        *out_x,
                             gdouble        *out_z)
{
    g_return_if_fail (LRG_IS_HORIZON_RING (self));
    if (out_x != NULL)
        *out_x = self->centre[0];
    if (out_z != NULL)
        *out_z = self->centre[1];
}

guint
lrg_horizon_ring_get_vertex_count (LrgHorizonRing *self)
{
    g_return_val_if_fail (LRG_IS_HORIZON_RING (self), 0);
    return self->n_vertices;
}

guint
lrg_horizon_ring_get_triangle_count (LrgHorizonRing *self)
{
    g_return_val_if_fail (LRG_IS_HORIZON_RING (self), 0);
    return self->n_triangles;
}

LrgStaticBatch *
lrg_horizon_ring_get_batch (LrgHorizonRing *self)
{
    g_return_val_if_fail (LRG_IS_HORIZON_RING (self), NULL);
    return self->batch;
}

void
lrg_horizon_ring_draw (LrgHorizonRing *self,
                       GrlShader      *shader)
{
    g_autoptr(GError) error = NULL;

    g_return_if_fail (LRG_IS_HORIZON_RING (self));

    if (!self->built)
        return;
    if (!lrg_static_batch_is_uploaded (self->batch) && !lrg_static_batch_upload (self->batch, &error))
    {
        g_warning ("horizon ring: %s", error->message);
        self->built = FALSE;
        return;
    }
    if (shader != NULL)
        lrg_static_batch_set_shader (self->batch, shader);
    lrg_static_batch_draw (self->batch, LRG_STATIC_BATCH_ALL_LAYERS);
    if (shader != NULL)
        lrg_static_batch_set_shader (self->batch, NULL);
}

void
lrg_horizon_ring_release_gpu (LrgHorizonRing *self)
{
    g_return_if_fail (LRG_IS_HORIZON_RING (self));
    lrg_static_batch_unload (self->batch);
}

/* lrg_horizon_ring_prewarm: uploads the built ring's batch now (what the
 * first draw would do) */
static gboolean
lrg_horizon_ring_prewarm (LrgPrewarmable *prewarmable)
{
    LrgHorizonRing *self = LRG_HORIZON_RING (prewarmable);

    if (self->built && !lrg_static_batch_is_uploaded (self->batch))
        lrg_static_batch_upload (self->batch, NULL);
    return TRUE;
}

static void
lrg_horizon_ring_prewarmable_init (LrgPrewarmableInterface *iface)
{
    iface->prewarm = lrg_horizon_ring_prewarm;
}
