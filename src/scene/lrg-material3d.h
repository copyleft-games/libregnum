/* lrg-material3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * PBR material for 3D scene objects.
 *
 * LrgMaterial3D represents a physically-based rendering (PBR) material
 * with support for base color, roughness, metallic, and emission properties.
 * This material format is compatible with Blender's Principled BSDF shader
 * and the exported YAML scene format.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_MATERIAL3D (lrg_material3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMaterial3D, lrg_material3d, LRG, MATERIAL3D, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_material3d_new:
 *
 * Creates a new #LrgMaterial3D with default values.
 * Default is white color (1,1,1,1), roughness 0.5, metallic 0.0,
 * no emission.
 *
 * Returns: (transfer full): A new #LrgMaterial3D
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial3D * lrg_material3d_new (void);

/**
 * lrg_material3d_new_with_color:
 * @r: Red component (0.0-1.0)
 * @g: Green component (0.0-1.0)
 * @b: Blue component (0.0-1.0)
 * @a: Alpha component (0.0-1.0)
 *
 * Creates a new #LrgMaterial3D with the specified color.
 *
 * Returns: (transfer full): A new #LrgMaterial3D
 */
LRG_AVAILABLE_IN_ALL
LrgMaterial3D * lrg_material3d_new_with_color (gfloat r,
                                               gfloat g,
                                               gfloat b,
                                               gfloat a);

/* ==========================================================================
 * Color Properties
 * ========================================================================== */

/**
 * lrg_material3d_set_color:
 * @self: an #LrgMaterial3D
 * @r: Red component (0.0-1.0)
 * @g: Green component (0.0-1.0)
 * @b: Blue component (0.0-1.0)
 * @a: Alpha component (0.0-1.0)
 *
 * Sets the base color of the material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material3d_set_color (LrgMaterial3D *self,
                               gfloat         r,
                               gfloat         g,
                               gfloat         b,
                               gfloat         a);

/**
 * lrg_material3d_get_color:
 * @self: an #LrgMaterial3D
 * @r: (out) (optional): Location for red component
 * @g: (out) (optional): Location for green component
 * @b: (out) (optional): Location for blue component
 * @a: (out) (optional): Location for alpha component
 *
 * Gets the base color of the material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material3d_get_color (LrgMaterial3D *self,
                               gfloat        *r,
                               gfloat        *g,
                               gfloat        *b,
                               gfloat        *a);

/**
 * lrg_material3d_get_color_grl:
 * @self: an #LrgMaterial3D
 *
 * Gets the base color as a GrlColor for use with graylib rendering.
 *
 * Returns: (transfer full): A new #GrlColor
 */
LRG_AVAILABLE_IN_ALL
GrlColor * lrg_material3d_get_color_grl (LrgMaterial3D *self);

/* ==========================================================================
 * PBR Properties
 * ========================================================================== */

/**
 * lrg_material3d_get_roughness:
 * @self: an #LrgMaterial3D
 *
 * Gets the roughness value of the material.
 * 0.0 = perfectly smooth (mirror), 1.0 = fully rough (diffuse).
 *
 * Returns: The roughness value (0.0-1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material3d_get_roughness (LrgMaterial3D *self);

/**
 * lrg_material3d_set_roughness:
 * @self: an #LrgMaterial3D
 * @roughness: The roughness value (0.0-1.0)
 *
 * Sets the roughness value of the material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material3d_set_roughness (LrgMaterial3D *self,
                                   gfloat         roughness);

/**
 * lrg_material3d_get_metallic:
 * @self: an #LrgMaterial3D
 *
 * Gets the metallic value of the material.
 * 0.0 = dielectric (plastic, wood), 1.0 = fully metallic (metal).
 *
 * Returns: The metallic value (0.0-1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material3d_get_metallic (LrgMaterial3D *self);

/**
 * lrg_material3d_set_metallic:
 * @self: an #LrgMaterial3D
 * @metallic: The metallic value (0.0-1.0)
 *
 * Sets the metallic value of the material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material3d_set_metallic (LrgMaterial3D *self,
                                  gfloat         metallic);

/* ==========================================================================
 * Emission Properties
 * ========================================================================== */

/**
 * lrg_material3d_set_emission_color:
 * @self: an #LrgMaterial3D
 * @r: Red component (0.0-1.0)
 * @g: Green component (0.0-1.0)
 * @b: Blue component (0.0-1.0)
 * @a: Alpha component (0.0-1.0)
 *
 * Sets the emission color of the material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material3d_set_emission_color (LrgMaterial3D *self,
                                        gfloat         r,
                                        gfloat         g,
                                        gfloat         b,
                                        gfloat         a);

/**
 * lrg_material3d_get_emission_color:
 * @self: an #LrgMaterial3D
 * @r: (out) (optional): Location for red component
 * @g: (out) (optional): Location for green component
 * @b: (out) (optional): Location for blue component
 * @a: (out) (optional): Location for alpha component
 *
 * Gets the emission color of the material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material3d_get_emission_color (LrgMaterial3D *self,
                                        gfloat        *r,
                                        gfloat        *g,
                                        gfloat        *b,
                                        gfloat        *a);

/**
 * lrg_material3d_get_emission_strength:
 * @self: an #LrgMaterial3D
 *
 * Gets the emission strength of the material.
 * 0.0 = no emission, higher values = brighter glow.
 *
 * Returns: The emission strength (0.0+)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_material3d_get_emission_strength (LrgMaterial3D *self);

/**
 * lrg_material3d_set_emission_strength:
 * @self: an #LrgMaterial3D
 * @strength: The emission strength (0.0+)
 *
 * Sets the emission strength of the material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_material3d_set_emission_strength (LrgMaterial3D *self,
                                           gfloat         strength);

G_END_DECLS
