/* lrg-collision-mesh-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal helpers shared by LrgCollisionMesh and LrgCharacterController.
 * Not installed, not scanned for introspection. Everything here works in
 * double precision on world-space data.
 */

#pragma once

#include <glib-object.h>
#include "physics/lrg-collision-mesh.h"

G_BEGIN_DECLS

/*
 * LrgCollisionTri:
 *
 * One triangle handed to an LrgCollisionTriFunc: corners in world space
 * (instance transforms already applied), its tag, its index in the mesh
 * that owns it and the id of the outermost instance (0 for own
 * triangles).
 */
typedef struct
{
    gdouble  v[9];
    guint32  tag;
    guint    triangle;
    guint64  instance_id;
} LrgCollisionTri;

/*
 * LrgCollisionTriFunc:
 *
 * Visitor for lrg_collision_mesh_query_box(). Return FALSE to stop the
 * query early.
 */
typedef gboolean (*LrgCollisionTriFunc) (const LrgCollisionTri *tri,
                                         gpointer               user_data);

/*
 * lrg_collision_mesh_query_box:
 *
 * Visits every non-degenerate triangle (own triangles first, then
 * instances in ascending order) whose bounds overlap the world box
 * [min, max]. The visit order is deterministic. Returns FALSE when the
 * visitor stopped the query.
 */
G_GNUC_INTERNAL
gboolean
lrg_collision_mesh_query_box (LrgCollisionMesh    *self,
                              const gdouble       *min,
                              const gdouble       *max,
                              LrgCollisionTriFunc  func,
                              gpointer             user_data);

/*
 * lrg_collision_mesh_sweep_segment:
 *
 * Sweeps the swept-sphere volume of segment a-b (radius r) along motion;
 * the contact rules of lrg_collision_mesh_sweep_sphere(). On a hit
 * out_fraction receives the fraction of motion travelled (0..1) and
 * out_face_normal (nullable) the unit face normal of the triangle hit,
 * oriented to the contact normal's side. When several triangles touch
 * at the same time (an edge shared by a step's tread and riser) the
 * one whose face normal points most upward wins, so ground tests see
 * the tread.
 */
G_GNUC_INTERNAL
gboolean
lrg_collision_mesh_sweep_segment (LrgCollisionMesh *self,
                                  const gdouble    *a,
                                  const gdouble    *b,
                                  gdouble           radius,
                                  const gdouble    *motion,
                                  LrgCollisionHit  *out_hit,
                                  gdouble          *out_fraction,
                                  gdouble          *out_face_normal);

/*
 * lrg_collision_segment_triangle:
 *
 * Distance between segment a-b and triangle tri (nine doubles); the
 * closest points are written to on_segment and on_triangle. A segment
 * that crosses the triangle returns 0 with both points at the crossing.
 */
G_GNUC_INTERNAL
gdouble
lrg_collision_segment_triangle (const gdouble *a,
                                const gdouble *b,
                                const gdouble *tri,
                                gdouble       *on_segment,
                                gdouble       *on_triangle);

/*
 * lrg_collision_triangle_normal:
 *
 * Unit face normal of tri (right-hand winding); FALSE for a degenerate
 * triangle (out is then +Y).
 */
G_GNUC_INTERNAL
gboolean
lrg_collision_triangle_normal (const gdouble *tri,
                               gdouble       *out);

G_END_DECLS
