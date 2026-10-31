/* lrg-solid-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared primitive storage for LrgSolidGeometry and LrgSolidWorld.
 * Not installed and not part of the public API.
 */

#pragma once

#include <glib.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-solid-geometry.h"
#include "lrg-collision-model.h"

G_BEGIN_DECLS

typedef enum {
    LRG_SOLID_PRIM_SPHERE = 1,
    LRG_SOLID_PRIM_BOX = 2,
    LRG_SOLID_PRIM_SPHYL = 3,
    LRG_SOLID_PRIM_CONVEX = 4,
    LRG_SOLID_PRIM_CYLINDER = 5
} LrgSolidPrimKind;

/*
 * axis[column][component]. A sphyl uses column 1 as its +Y segment.
 * A flat cylinder uses column 2 as its +Z axis and half[2] as the
 * axial half-height. planes are nx, ny, nz, d with outward normals
 * and inside n·x <= d. faces index the convex vertices, three per
 * triangle.
 */
typedef struct
{
    LrgSolidPrimKind kind;
    gdouble          center[3];
    gdouble          axis[3][3];
    gdouble          radius;
    gdouble          half[3];
    gdouble         *points;
    guint            n_points;
    gdouble         *planes;
    guint            n_planes;
    guint           *faces;
    guint            n_faces;
} LrgSolidPrim;

struct _LrgSolidGeometry
{
    GObject              parent_instance;
    LrgSolidShape        shape;
    guint                depth;
    LrgSolidPrim        *prims;
    guint                n_prims;
    LrgSolidGeometry   **children;
    guint                n_children;
};

struct _LrgCollisionModel
{
    GObject           parent_instance;
    LrgSolidGeometry *geometry;
    gfloat            transform[16];
    gdouble           rotation[3][3];
    gdouble           translation[3];
    gdouble           scale;
    guint32           group;
    guint32           mask;
    guint64           caller_id;
};

void lrg_solid_prims_free (LrgSolidPrim *prims, guint n_prims);

gboolean
lrg_solid_transform_decompose (const gfloat *transform,
                               gdouble       rotation[3][3],
                               gdouble       translation[3],
                               gdouble      *scale,
                               GError      **error);

gboolean
lrg_solid_geometry_bake (LrgSolidGeometry *self,
                         const gdouble     rotation[3][3],
                         gdouble           scale,
                         const gdouble     translation[3],
                         LrgSolidPrim    **out_prims,
                         guint            *out_n,
                         GError          **error);

gboolean
lrg_solid_prims_classify (const LrgSolidPrim *prims,
                          guint               n_prims,
                          const gdouble       point[3],
                          LrgSolidClass      *result);

gboolean
lrg_solid_prims_overlap (const LrgSolidPrim *a,
                         guint               n_a,
                         const LrgSolidPrim *b,
                         guint               n_b,
                         LrgSolidContact    *contact,
                         gboolean           *overlapping,
                         GError            **error);

gboolean
lrg_solid_prims_contains (const LrgSolidPrim *outer,
                          guint               n_outer,
                          const LrgSolidPrim *inner,
                          guint               n_inner,
                          gboolean           *contained,
                          GError            **error);

gboolean
lrg_solid_prims_sweep_sphere (const LrgSolidPrim *static_prims,
                              guint               n_static,
                              const gdouble       center[3],
                              gdouble             radius,
                              const gdouble       delta[3],
                              LrgSolidHit        *hit,
                              gboolean           *did_hit,
                              GError            **error);

G_END_DECLS
