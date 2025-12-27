/* lrg-scene-serializer-yaml.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base YAML implementation of the scene serializer.
 *
 * LrgSceneSerializerYaml is a derivable class that loads and saves
 * LrgScene objects using YAML format. Subclasses can override the
 * coordinate conversion methods to handle different coordinate systems
 * (e.g., Blender Z-up vs raylib Y-up).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-scene-serializer.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCENE_SERIALIZER_YAML (lrg_scene_serializer_yaml_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgSceneSerializerYaml, lrg_scene_serializer_yaml,
                          LRG, SCENE_SERIALIZER_YAML, GObject)

/**
 * LrgSceneSerializerYamlClass:
 * @parent_class: Parent class
 * @convert_position: Virtual method to convert position coordinates
 * @convert_rotation: Virtual method to convert rotation coordinates
 * @convert_scale: Virtual method to convert scale coordinates
 *
 * Class structure for YAML scene serializers. Subclasses can override
 * the convert_* methods to handle different source coordinate systems.
 *
 * The default implementation performs no conversion (identity).
 */
struct _LrgSceneSerializerYamlClass
{
	GObjectClass parent_class;

	/**
	 * convert_position:
	 * @self: the serializer
	 * @x: source X coordinate
	 * @y: source Y coordinate
	 * @z: source Z coordinate
	 *
	 * Convert a position vector from source to target coordinate system.
	 *
	 * Returns: (transfer full): converted vector
	 */
	GrlVector3 * (*convert_position) (LrgSceneSerializerYaml *self,
	                                  gfloat                  x,
	                                  gfloat                  y,
	                                  gfloat                  z);

	/**
	 * convert_rotation:
	 * @self: the serializer
	 * @x: source X rotation (radians)
	 * @y: source Y rotation (radians)
	 * @z: source Z rotation (radians)
	 *
	 * Convert a rotation vector from source to target coordinate system.
	 *
	 * Returns: (transfer full): converted vector
	 */
	GrlVector3 * (*convert_rotation) (LrgSceneSerializerYaml *self,
	                                  gfloat                  x,
	                                  gfloat                  y,
	                                  gfloat                  z);

	/**
	 * convert_scale:
	 * @self: the serializer
	 * @x: source X scale
	 * @y: source Y scale
	 * @z: source Z scale
	 *
	 * Convert a scale vector from source to target coordinate system.
	 *
	 * Returns: (transfer full): converted vector
	 */
	GrlVector3 * (*convert_scale) (LrgSceneSerializerYaml *self,
	                               gfloat                  x,
	                               gfloat                  y,
	                               gfloat                  z);

	/**
	 * should_reverse_face_winding:
	 * @self: the serializer
	 *
	 * Returns whether face winding order should be reversed when
	 * parsing mesh data. This is needed when coordinate conversion
	 * mirrors the geometry (e.g., Blender Z-up to raylib Y-up).
	 *
	 * Returns: %TRUE if face indices should be stored in reverse order
	 */
	gboolean (*should_reverse_face_winding) (LrgSceneSerializerYaml *self);

	/*< private >*/
	gpointer _reserved[3];
};

/**
 * lrg_scene_serializer_yaml_new:
 *
 * Creates a new #LrgSceneSerializerYaml with no coordinate conversion.
 *
 * For Blender scenes, use #LrgSceneSerializerBlender instead.
 *
 * Returns: (transfer full): A new #LrgSceneSerializerYaml
 */
LRG_AVAILABLE_IN_ALL
LrgSceneSerializerYaml * lrg_scene_serializer_yaml_new (void);

G_END_DECLS
