/* lrg-cad-baker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Baking parametric CAD parts (cad-glib documents) into renderable
 * geometry.  The CPU half (evaluate -> tessellate -> 16-bit-safe
 * chunking) is fully headless; GrlModel creation is deferred until a
 * GL context exists (the render path calls get_models lazily).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <cad-glib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_CAD_BAKE_RESULT (lrg_cad_bake_result_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCadBakeResult, lrg_cad_bake_result,
                      LRG, CAD_BAKE_RESULT, GObject)

/**
 * lrg_cad_bake:
 * @document: an evaluated or evaluatable #CadDocument
 * @overrides: (nullable): parameter overrides (name -> gdouble*)
 * @deflection: tessellation linear deflection in mm (<= 0 for the
 *   default display quality)
 * @part: (nullable): which part to bake, or %NULL for the first
 * @error: return location for a #GError
 *
 * Evaluates the document (when needed) and tessellates the part into
 * 16-bit-index-safe mesh chunks.  Headless-safe.
 *
 * Returns: (transfer full) (nullable): the bake result, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgCadBakeResult * lrg_cad_bake (CadDocument  *document,
                                 GHashTable   *overrides,
                                 gdouble       deflection,
                                 const gchar  *part,
                                 GError      **error);

/**
 * lrg_cad_bake_result_get_meshes:
 * @self: a bake result
 *
 * Returns: (transfer none) (element-type CadMesh): mesh chunks, each
 *   within graylib's 65535-vertex limit, with per-triangle provenance
 *   ids
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_cad_bake_result_get_meshes (LrgCadBakeResult *self);

/**
 * lrg_cad_bake_result_get_solid:
 * @self: a bake result
 *
 * Returns: (transfer none): the baked part's solid
 */
LRG_AVAILABLE_IN_ALL
CadSolid * lrg_cad_bake_result_get_solid (LrgCadBakeResult *self);

/**
 * lrg_cad_bake_result_get_tree:
 * @self: a bake result
 *
 * Returns: (transfer none) (nullable): the part's feature tree
 */
LRG_AVAILABLE_IN_ALL
CadFeatureNode * lrg_cad_bake_result_get_tree (LrgCadBakeResult *self);

/**
 * lrg_cad_bake_result_get_total_triangles:
 * @self: a bake result
 *
 * Returns: total triangles across all chunks
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cad_bake_result_get_total_triangles (LrgCadBakeResult *self);

/**
 * lrg_cad_bake_result_get_models:
 * @self: a bake result
 *
 * Lazily uploads the mesh chunks as #GrlModel instances.  REQUIRES a
 * live GL context (raylib window); call only from render paths.
 *
 * Returns: (transfer none) (element-type GrlModel): one model per
 *   chunk
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_cad_bake_result_get_models (LrgCadBakeResult *self);

/* ---- assemblies ---- */

#define LRG_TYPE_CAD_INSTANCE_BAKE (lrg_cad_instance_bake_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCadInstanceBake, lrg_cad_instance_bake,
                      LRG, CAD_INSTANCE_BAKE, GObject)

/**
 * lrg_cad_instance_bake_get_instance_id:
 * @self: an instance bake
 *
 * Returns: the source #CadAssemblyInstance id
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cad_instance_bake_get_instance_id (LrgCadInstanceBake *self);

/**
 * lrg_cad_instance_bake_get_name:
 * @self: an instance bake
 *
 * Returns: (transfer none) (nullable): the instance label
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_cad_instance_bake_get_name (LrgCadInstanceBake *self);

/**
 * lrg_cad_instance_bake_get_transform:
 * @self: an instance bake
 * @out_m12: (out caller-allocates) (array fixed-size=12): the solved
 *   column-major 3x4 placement transform
 *
 * The meshes are baked in the instance's LOCAL frame; this is the
 * model matrix the renderer applies.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cad_instance_bake_get_transform (LrgCadInstanceBake *self,
                                          gdouble            *out_m12);

/**
 * lrg_cad_instance_bake_get_meshes:
 * @self: an instance bake
 *
 * Returns: (transfer none) (element-type CadMesh): the local mesh chunks
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_cad_instance_bake_get_meshes (LrgCadInstanceBake *self);

/**
 * lrg_cad_instance_bake_get_models:
 * @self: an instance bake
 *
 * Lazily uploads the chunks as #GrlModel (needs a GL context).
 *
 * Returns: (transfer none) (element-type GrlModel): one model per chunk
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_cad_instance_bake_get_models (LrgCadInstanceBake *self);

/**
 * lrg_cad_bake_assembly_solved:
 * @assembly: an already-solved #CadAssembly
 * @deflection: tessellation deflection (<= 0 for default)
 * @error: return location for a #GError
 *
 * Bakes each instance of an already-solved assembly into local mesh
 * chunks tagged with the instance's solved transform.  Use this for
 * joint animation: drive a joint, re-solve, re-bake.  Headless-safe.
 *
 * Returns: (transfer full) (element-type LrgCadInstanceBake) (nullable):
 *   one bake per instance, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_cad_bake_assembly_solved (CadAssembly  *assembly,
                                          gdouble       deflection,
                                          GError      **error);

/**
 * lrg_cad_bake_assembly:
 * @document: a #CadDocument defining the assembly
 * @overrides: (nullable): parameter overrides (name -> gdouble*)
 * @deflection: tessellation deflection (<= 0 for default)
 * @assembly: (nullable): the assembly name, or %NULL for the first
 * @error: return location for a #GError
 *
 * Evaluates @document, solves the named assembly, and bakes its
 * instances.  Headless-safe.
 *
 * Returns: (transfer full) (element-type LrgCadInstanceBake) (nullable):
 *   one bake per instance, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_cad_bake_assembly (CadDocument  *document,
                                   GHashTable   *overrides,
                                   gdouble       deflection,
                                   const gchar  *assembly,
                                   GError      **error);

G_END_DECLS
