/* lrg-scene-serializer-blender.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Blender-specific YAML scene serializer.
 *
 * Converts Blender's Z-up coordinate system to raylib's Y-up system:
 *   Position: (X, Y, Z) -> (X, Z, -Y)
 *   Rotation: (X, Y, Z) -> (X, Z, -Y)
 *   Scale:    (X, Y, Z) -> (X, Z, Y)
 */

#include "lrg-scene-serializer-blender.h"

/**
 * LrgSceneSerializerBlender:
 *
 * A YAML scene serializer for Blender-exported scenes.
 *
 * This serializer extends #LrgSceneSerializerYaml with coordinate
 * conversion from Blender's Z-up right-handed system to raylib's
 * Y-up right-handed system.
 */
struct _LrgSceneSerializerBlender
{
	LrgSceneSerializerYaml parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSceneSerializerBlender, lrg_scene_serializer_blender,
                     LRG_TYPE_SCENE_SERIALIZER_YAML)

/* =============================================================================
 * Coordinate Conversion Virtual Methods
 *
 * Blender uses Z-up right-handed coordinate system:
 *   X = right, Y = forward, Z = up
 *
 * raylib uses Y-up right-handed coordinate system:
 *   X = right, Y = up, Z = forward (towards viewer)
 *
 * Conversion: Blender (X, Y, Z) -> raylib (X, Z, -Y)
 * ========================================================================== */

/**
 * lrg_scene_serializer_blender_convert_position:
 * @self: the serializer
 * @x: Blender X coordinate
 * @y: Blender Y coordinate
 * @z: Blender Z coordinate
 *
 * Converts a position from Blender Z-up to raylib Y-up.
 * Blender (X, Y, Z) -> raylib (X, Z, -Y)
 *
 * Returns: (transfer full): converted position vector
 */
static GrlVector3 *
lrg_scene_serializer_blender_convert_position (LrgSceneSerializerYaml *yaml_self,
                                               gfloat                  x,
                                               gfloat                  y,
                                               gfloat                  z)
{
	/* Blender Z-up to raylib Y-up: (X, Y, Z) -> (X, Z, -Y) */
	return grl_vector3_new (x, z, -y);
}

/**
 * lrg_scene_serializer_blender_convert_rotation:
 * @self: the serializer
 * @x: Blender X rotation (radians)
 * @y: Blender Y rotation (radians)
 * @z: Blender Z rotation (radians)
 *
 * Converts a rotation from Blender Z-up to raylib Y-up.
 * Blender (X, Y, Z) -> raylib (X, Z, -Y)
 *
 * Returns: (transfer full): converted rotation vector
 */
static GrlVector3 *
lrg_scene_serializer_blender_convert_rotation (LrgSceneSerializerYaml *yaml_self,
                                               gfloat                  x,
                                               gfloat                  y,
                                               gfloat                  z)
{
	/* Blender Z-up to raylib Y-up: (X, Y, Z) -> (X, Z, -Y) */
	return grl_vector3_new (x, z, -y);
}

/**
 * lrg_scene_serializer_blender_convert_scale:
 * @self: the serializer
 * @x: Blender X scale
 * @y: Blender Y scale
 * @z: Blender Z scale
 *
 * Converts a scale from Blender Z-up to raylib Y-up.
 * Scale values are positive, so no negation needed.
 * Blender (X, Y, Z) -> raylib (X, Z, Y)
 *
 * Returns: (transfer full): converted scale vector
 */
static GrlVector3 *
lrg_scene_serializer_blender_convert_scale (LrgSceneSerializerYaml *yaml_self,
                                            gfloat                  x,
                                            gfloat                  y,
                                            gfloat                  z)
{
	/* Scale: swap Y and Z (no negation for scale) */
	return grl_vector3_new (x, z, y);
}

/**
 * lrg_scene_serializer_blender_should_reverse_face_winding:
 * @self: the serializer
 *
 * Returns TRUE because the Blender coordinate conversion includes
 * a negation (Y -> -Y) which mirrors geometry and requires face
 * winding reversal for correct rendering.
 *
 * Returns: %TRUE
 */
static gboolean
lrg_scene_serializer_blender_should_reverse_face_winding (LrgSceneSerializerYaml *yaml_self)
{
	/*
	 * The coordinate conversion (X, Y, Z) -> (X, Z, -Y) mirrors
	 * geometry due to the negation. This inverts face winding,
	 * so we reverse indices to compensate.
	 */
	return TRUE;
}

/* =============================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_scene_serializer_blender_class_init (LrgSceneSerializerBlenderClass *klass)
{
	LrgSceneSerializerYamlClass *yaml_class;

	yaml_class = LRG_SCENE_SERIALIZER_YAML_CLASS (klass);

	/* Override coordinate conversion virtual methods */
	yaml_class->convert_position = lrg_scene_serializer_blender_convert_position;
	yaml_class->convert_rotation = lrg_scene_serializer_blender_convert_rotation;
	yaml_class->convert_scale    = lrg_scene_serializer_blender_convert_scale;

	/* Enable face winding reversal to compensate for mirrored geometry */
	yaml_class->should_reverse_face_winding = lrg_scene_serializer_blender_should_reverse_face_winding;
}

static void
lrg_scene_serializer_blender_init (LrgSceneSerializerBlender *self)
{
	/* No instance-specific initialization needed */
}

/* =============================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_scene_serializer_blender_new:
 *
 * Creates a new #LrgSceneSerializerBlender.
 *
 * Returns: (transfer full): A new #LrgSceneSerializerBlender
 */
LrgSceneSerializerBlender *
lrg_scene_serializer_blender_new (void)
{
	return g_object_new (LRG_TYPE_SCENE_SERIALIZER_BLENDER, NULL);
}
