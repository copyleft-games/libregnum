/* lrg-scene-serializer-blender.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Blender-specific YAML scene serializer.
 *
 * LrgSceneSerializerBlender handles YAML scene files exported from Blender,
 * converting from Blender's Z-up coordinate system to raylib's Y-up system.
 *
 * Coordinate Conversion:
 *   Position: (X, Y, Z) -> (X, Z, -Y)
 *   Rotation: (X, Y, Z) -> (X, Z, -Y)
 *   Scale:    (X, Y, Z) -> (X, Z, Y)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-scene-serializer-yaml.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCENE_SERIALIZER_BLENDER (lrg_scene_serializer_blender_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSceneSerializerBlender, lrg_scene_serializer_blender,
                      LRG, SCENE_SERIALIZER_BLENDER, LrgSceneSerializerYaml)

/**
 * lrg_scene_serializer_blender_new:
 *
 * Creates a new #LrgSceneSerializerBlender for loading Blender-exported
 * YAML scene files.
 *
 * This serializer automatically converts coordinates from Blender's Z-up
 * coordinate system to raylib's Y-up coordinate system.
 *
 * Returns: (transfer full): A new #LrgSceneSerializerBlender
 */
LRG_AVAILABLE_IN_ALL
LrgSceneSerializerBlender * lrg_scene_serializer_blender_new (void);

G_END_DECLS
