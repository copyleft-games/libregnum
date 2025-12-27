/* lrg-scene-serializer-yaml.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * YAML implementation of the scene serializer.
 *
 * LrgSceneSerializerYaml loads and saves LrgScene objects using the
 * YAML format exported by Blender. It supports lossless round-trip
 * serialization with full float precision.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-scene-serializer.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCENE_SERIALIZER_YAML (lrg_scene_serializer_yaml_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSceneSerializerYaml, lrg_scene_serializer_yaml,
                      LRG, SCENE_SERIALIZER_YAML, GObject)

/**
 * lrg_scene_serializer_yaml_new:
 *
 * Creates a new #LrgSceneSerializerYaml.
 *
 * Returns: (transfer full): A new #LrgSceneSerializerYaml
 */
LRG_AVAILABLE_IN_ALL
LrgSceneSerializerYaml * lrg_scene_serializer_yaml_new (void);

G_END_DECLS
