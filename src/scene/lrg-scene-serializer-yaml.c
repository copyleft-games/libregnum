/* lrg-scene-serializer-yaml.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base YAML implementation of the scene serializer.
 * Subclasses can override coordinate conversion for different source formats.
 */

#include "lrg-scene-serializer-yaml.h"
#include "lrg-mesh-data.h"
#include <yaml-glib.h>
#include <gio/gio.h>

/**
 * LrgSceneSerializerYamlPrivate:
 *
 * Private data for YAML serializer instances.
 */
typedef struct
{
	gpointer _reserved;
} LrgSceneSerializerYamlPrivate;

static void lrg_scene_serializer_iface_init (LrgSceneSerializerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgSceneSerializerYaml, lrg_scene_serializer_yaml,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (LrgSceneSerializerYaml)
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_SCENE_SERIALIZER,
                                                lrg_scene_serializer_iface_init))

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

/*
 * Default convert_position: identity (no conversion).
 */
static GrlVector3 *
lrg_scene_serializer_yaml_real_convert_position (LrgSceneSerializerYaml *self,
                                                  gfloat                  x,
                                                  gfloat                  y,
                                                  gfloat                  z)
{
	return grl_vector3_new (x, y, z);
}

/*
 * Default convert_rotation: identity (no conversion).
 */
static GrlVector3 *
lrg_scene_serializer_yaml_real_convert_rotation (LrgSceneSerializerYaml *self,
                                                  gfloat                  x,
                                                  gfloat                  y,
                                                  gfloat                  z)
{
	return grl_vector3_new (x, y, z);
}

/*
 * Default convert_scale: identity (no conversion).
 */
static GrlVector3 *
lrg_scene_serializer_yaml_real_convert_scale (LrgSceneSerializerYaml *self,
                                               gfloat                  x,
                                               gfloat                  y,
                                               gfloat                  z)
{
	return grl_vector3_new (x, y, z);
}

/*
 * Default should_reverse_face_winding: no reversal needed.
 * Subclasses that mirror geometry during coordinate conversion
 * should override this to return TRUE.
 */
static gboolean
lrg_scene_serializer_yaml_real_should_reverse_face_winding (LrgSceneSerializerYaml *self)
{
	return FALSE;
}

/* ==========================================================================
 * Helper Functions - Parsing
 * ========================================================================== */

/*
 * Convert primitive string to enum value.
 * Returns LRG_PRIMITIVE_CUBE if unknown.
 */
static LrgPrimitiveType
parse_primitive_type (const gchar *str)
{
	if (str == NULL)
		return LRG_PRIMITIVE_CUBE;

	if (g_str_equal (str, "primitive_plane"))
		return LRG_PRIMITIVE_PLANE;
	if (g_str_equal (str, "primitive_cube"))
		return LRG_PRIMITIVE_CUBE;
	if (g_str_equal (str, "primitive_circle"))
		return LRG_PRIMITIVE_CIRCLE;
	if (g_str_equal (str, "primitive_uv_sphere") ||
	    g_str_equal (str, "primitive_sphere"))
		return LRG_PRIMITIVE_UV_SPHERE;
	if (g_str_equal (str, "primitive_ico_sphere"))
		return LRG_PRIMITIVE_ICO_SPHERE;
	if (g_str_equal (str, "primitive_cylinder"))
		return LRG_PRIMITIVE_CYLINDER;
	if (g_str_equal (str, "primitive_cone"))
		return LRG_PRIMITIVE_CONE;
	if (g_str_equal (str, "primitive_torus"))
		return LRG_PRIMITIVE_TORUS;
	if (g_str_equal (str, "primitive_grid"))
		return LRG_PRIMITIVE_GRID;
	if (g_str_equal (str, "primitive_mesh"))
		return LRG_PRIMITIVE_MESH;
	if (g_str_equal (str, "primitive_rectangle_2d"))
		return LRG_PRIMITIVE_RECTANGLE_2D;
	if (g_str_equal (str, "primitive_circle_2d"))
		return LRG_PRIMITIVE_CIRCLE_2D;

	return LRG_PRIMITIVE_CUBE;
}

/*
 * Convert primitive enum to string.
 */
static const gchar *
primitive_type_to_string (LrgPrimitiveType type)
{
	switch (type)
	{
	case LRG_PRIMITIVE_PLANE:
		return "primitive_plane";
	case LRG_PRIMITIVE_CUBE:
		return "primitive_cube";
	case LRG_PRIMITIVE_CIRCLE:
		return "primitive_circle";
	case LRG_PRIMITIVE_UV_SPHERE:
		return "primitive_sphere";
	case LRG_PRIMITIVE_ICO_SPHERE:
		return "primitive_ico_sphere";
	case LRG_PRIMITIVE_CYLINDER:
		return "primitive_cylinder";
	case LRG_PRIMITIVE_CONE:
		return "primitive_cone";
	case LRG_PRIMITIVE_TORUS:
		return "primitive_torus";
	case LRG_PRIMITIVE_GRID:
		return "primitive_grid";
	case LRG_PRIMITIVE_MESH:
		return "primitive_mesh";
	case LRG_PRIMITIVE_RECTANGLE_2D:
		return "primitive_rectangle_2d";
	case LRG_PRIMITIVE_CIRCLE_2D:
		return "primitive_circle_2d";
	default:
		return "primitive_cube";
	}
}

/*
 * Parse raw vector components from a YAML sequence [x, y, z].
 */
static void
parse_vector3_components (YamlSequence *seq,
                          gfloat       *out_x,
                          gfloat       *out_y,
                          gfloat       *out_z)
{
	*out_x = 0.0f;
	*out_y = 0.0f;
	*out_z = 0.0f;

	if (seq == NULL)
		return;

	if (yaml_sequence_get_length (seq) >= 1)
		*out_x = (gfloat)yaml_sequence_get_double_element (seq, 0);
	if (yaml_sequence_get_length (seq) >= 2)
		*out_y = (gfloat)yaml_sequence_get_double_element (seq, 1);
	if (yaml_sequence_get_length (seq) >= 3)
		*out_z = (gfloat)yaml_sequence_get_double_element (seq, 2);
}

/*
 * Parse a color from a YAML sequence [r, g, b, a].
 */
static void
parse_color4 (YamlSequence *seq,
              gfloat       *r,
              gfloat       *g,
              gfloat       *b,
              gfloat       *a)
{
	*r = *g = *b = 1.0f;
	*a = 1.0f;

	if (seq == NULL)
		return;

	if (yaml_sequence_get_length (seq) >= 1)
		*r = (gfloat)yaml_sequence_get_double_element (seq, 0);
	if (yaml_sequence_get_length (seq) >= 2)
		*g = (gfloat)yaml_sequence_get_double_element (seq, 1);
	if (yaml_sequence_get_length (seq) >= 3)
		*b = (gfloat)yaml_sequence_get_double_element (seq, 2);
	if (yaml_sequence_get_length (seq) >= 4)
		*a = (gfloat)yaml_sequence_get_double_element (seq, 3);
}

/*
 * Parse material from YAML mapping.
 */
static LrgMaterial3D *
parse_material (YamlMapping *material_map)
{
	LrgMaterial3D *material;
	YamlNode      *node;
	gfloat         r, g, b, a;

	material = lrg_material3d_new ();

	if (material_map == NULL)
		return material;

	/* Parse color */
	node = yaml_mapping_get_member (material_map, "color");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		parse_color4 (yaml_node_get_sequence (node), &r, &g, &b, &a);
		lrg_material3d_set_color (material, r, g, b, a);
	}

	/* Parse roughness */
	if (yaml_mapping_has_member (material_map, "roughness"))
	{
		gdouble roughness = yaml_mapping_get_double_member (material_map, "roughness");
		lrg_material3d_set_roughness (material, (gfloat)roughness);
	}

	/* Parse metallic */
	if (yaml_mapping_has_member (material_map, "metallic"))
	{
		gdouble metallic = yaml_mapping_get_double_member (material_map, "metallic");
		lrg_material3d_set_metallic (material, (gfloat)metallic);
	}

	/* Parse emission_color */
	node = yaml_mapping_get_member (material_map, "emission_color");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		parse_color4 (yaml_node_get_sequence (node), &r, &g, &b, &a);
		lrg_material3d_set_emission_color (material, r, g, b, a);
	}

	/* Parse emission_strength */
	if (yaml_mapping_has_member (material_map, "emission_strength"))
	{
		gdouble strength = yaml_mapping_get_double_member (material_map, "emission_strength");
		lrg_material3d_set_emission_strength (material, (gfloat)strength);
	}

	return material;
}

/*
 * Parse mesh_data from YAML mapping.
 * Converts vertex positions using the serializer's coordinate conversion.
 * Format:
 *   mesh_data:
 *     vertices: [[x, y, z], ...]
 *     faces: [[v0, v1, v2, ...], ...]  # Can be 3, 4, or n vertices
 *     smooth: boolean
 */
static LrgMeshData *
parse_mesh_data (LrgSceneSerializerYaml *self,
                 YamlMapping            *mesh_data_map)
{
	LrgSceneSerializerYamlClass *klass;
	LrgMeshData  *mesh_data;
	YamlNode     *node;
	YamlSequence *vertices_seq;
	YamlSequence *faces_seq;
	guint         i, j;
	guint         n_verts, n_faces;
	gfloat       *vertices;
	GArray       *face_array;
	gboolean      smooth;

	if (mesh_data_map == NULL)
		return NULL;

	klass = LRG_SCENE_SERIALIZER_YAML_GET_CLASS (self);
	mesh_data = lrg_mesh_data_new ();

	/* Parse vertices array */
	node = yaml_mapping_get_member (mesh_data_map, "vertices");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		vertices_seq = yaml_node_get_sequence (node);
		n_verts = yaml_sequence_get_length (vertices_seq);
		vertices = g_new (gfloat, n_verts * 3);

		for (i = 0; i < n_verts; i++)
		{
			YamlNode     *vert_node;
			YamlSequence *vert_seq;
			gfloat        x, y, z;
			g_autoptr(GrlVector3) converted = NULL;

			vert_node = yaml_sequence_get_element (vertices_seq, i);
			if (yaml_node_get_node_type (vert_node) != YAML_NODE_SEQUENCE)
				continue;

			vert_seq = yaml_node_get_sequence (vert_node);
			parse_vector3_components (vert_seq, &x, &y, &z);

			/* Apply coordinate conversion via virtual method */
			converted = klass->convert_position (self, x, y, z);

			vertices[i * 3 + 0] = converted->x;
			vertices[i * 3 + 1] = converted->y;
			vertices[i * 3 + 2] = converted->z;
		}

		lrg_mesh_data_set_vertices (mesh_data, vertices, n_verts);
		g_free (vertices);
	}

	/* Parse faces array */
	node = yaml_mapping_get_member (mesh_data_map, "faces");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		faces_seq = yaml_node_get_sequence (node);
		n_faces = yaml_sequence_get_length (faces_seq);
		face_array = g_array_new (FALSE, FALSE, sizeof (gint));

		for (i = 0; i < n_faces; i++)
		{
			YamlNode     *face_node;
			YamlSequence *face_seq;
			guint         face_len;
			gint          vertex_count;

			face_node = yaml_sequence_get_element (faces_seq, i);
			if (yaml_node_get_node_type (face_node) != YAML_NODE_SEQUENCE)
				continue;

			face_seq = yaml_node_get_sequence (face_node);
			face_len = yaml_sequence_get_length (face_seq);

			if (face_len < 3)
				continue;  /* Skip degenerate faces */

			/* Store face vertex count first, then indices */
			vertex_count = (gint)face_len;
			g_array_append_val (face_array, vertex_count);

			/* Read indices in normal order */
			for (j = 0; j < face_len; j++)
			{
				gint idx = (gint)yaml_sequence_get_int_element (face_seq, j);
				g_array_append_val (face_array, idx);
			}
		}

		lrg_mesh_data_set_faces (mesh_data,
		                          (gint *)face_array->data,
		                          n_faces,
		                          face_array->len);
		g_array_free (face_array, TRUE);
	}

	/* Parse smooth flag */
	smooth = FALSE;
	if (yaml_mapping_has_member (mesh_data_map, "smooth"))
		smooth = yaml_mapping_get_boolean_member (mesh_data_map, "smooth");
	lrg_mesh_data_set_smooth (mesh_data, smooth);

	/*
	 * Set reverse winding flag based on serializer's coordinate conversion.
	 * When coordinate conversion mirrors geometry (e.g., Blender Z-up to
	 * raylib Y-up with Y-negation), face winding must be reversed during
	 * triangulation to maintain correct face orientation.
	 */
	lrg_mesh_data_set_reverse_winding (mesh_data,
	                                    klass->should_reverse_face_winding (self));

	return mesh_data;
}

/*
 * Parse primitive parameters from YAML mapping.
 * Determines type by attempting to parse as each type.
 */
static void
parse_params (LrgSceneSerializerYaml *self,
              LrgSceneObject         *obj,
              YamlMapping            *params_map)
{
	GList       *members;
	GList       *iter;
	const gchar *str_val;

	if (params_map == NULL)
		return;

	members = yaml_mapping_get_members (params_map);

	for (iter = members; iter != NULL; iter = iter->next)
	{
		const gchar *name  = iter->data;
		YamlNode    *node  = yaml_mapping_get_member (params_map, name);
		YamlNodeType ntype = yaml_node_get_node_type (node);

		if (ntype == YAML_NODE_SCALAR)
		{
			/*
			 * yaml-glib doesn't have a scalar type enum, so we determine
			 * the type by examining the string value.
			 */
			str_val = yaml_node_get_string (node);

			if (str_val == NULL)
				continue;

			/* Check for boolean */
			if (g_ascii_strcasecmp (str_val, "true") == 0 ||
			    g_ascii_strcasecmp (str_val, "false") == 0 ||
			    g_ascii_strcasecmp (str_val, "yes") == 0 ||
			    g_ascii_strcasecmp (str_val, "no") == 0)
			{
				lrg_scene_object_set_param_bool (obj, name,
				                                 yaml_node_get_boolean (node));
			}
			/* Check for integer (no decimal point) */
			else if (g_regex_match_simple ("^-?[0-9]+$", str_val, 0, 0))
			{
				lrg_scene_object_set_param_int (obj, name,
				                                (gint)yaml_node_get_int (node));
			}
			/* Check for float (has decimal point or scientific notation) */
			else if (g_regex_match_simple ("^-?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?$", str_val, 0, 0))
			{
				lrg_scene_object_set_param_float (obj, name,
				                                  (gfloat)yaml_node_get_double (node));
			}
			/* Skip string values for now */
		}
		else if (ntype == YAML_NODE_MAPPING && g_str_equal (name, "mesh_data"))
		{
			/* Handle mesh_data as a special case */
			g_autoptr(LrgMeshData) mesh_data = NULL;

			mesh_data = parse_mesh_data (self, yaml_node_get_mapping (node));
			if (mesh_data != NULL)
				lrg_scene_object_set_mesh_data (obj, mesh_data);
		}
	}

	g_list_free (members);
}

/*
 * Parse a single scene object from YAML mapping.
 * Uses the serializer's virtual methods for coordinate conversion.
 */
static LrgSceneObject *
parse_scene_object (LrgSceneSerializerYaml *self,
                    YamlMapping            *obj_map)
{
	LrgSceneSerializerYamlClass *klass;
	LrgSceneObject              *obj;
	const gchar                 *name;
	const gchar                 *primitive_str;
	LrgPrimitiveType             primitive;
	YamlNode                    *node;
	YamlMapping                 *transform_map;
	YamlMapping                 *material_map;
	YamlMapping                 *params_map;
	g_autoptr(GrlVector3)        location = NULL;
	g_autoptr(GrlVector3)        rotation = NULL;
	g_autoptr(GrlVector3)        scale = NULL;
	g_autoptr(LrgMaterial3D)     material = NULL;
	gfloat                       x, y, z;

	if (obj_map == NULL)
		return NULL;

	klass = LRG_SCENE_SERIALIZER_YAML_GET_CLASS (self);

	/* Get name */
	name = yaml_mapping_get_string_member (obj_map, "name");

	/* Get primitive type */
	primitive_str = yaml_mapping_get_string_member (obj_map, "primitive");
	primitive = parse_primitive_type (primitive_str);

	obj = lrg_scene_object_new (name, primitive);

	/* Parse transform */
	node = yaml_mapping_get_member (obj_map, "transform");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
	{
		transform_map = yaml_node_get_mapping (node);

		/* Location - use virtual method for conversion */
		node = yaml_mapping_get_member (transform_map, "location");
		if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
		{
			parse_vector3_components (yaml_node_get_sequence (node), &x, &y, &z);
			location = klass->convert_position (self, x, y, z);
			lrg_scene_object_set_location (obj, location);
		}

		/* Rotation - use virtual method for conversion */
		node = yaml_mapping_get_member (transform_map, "rotation");
		if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
		{
			parse_vector3_components (yaml_node_get_sequence (node), &x, &y, &z);
			rotation = klass->convert_rotation (self, x, y, z);
			lrg_scene_object_set_rotation (obj, rotation);
		}

		/* Scale - use virtual method for conversion */
		node = yaml_mapping_get_member (transform_map, "scale");
		if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
		{
			parse_vector3_components (yaml_node_get_sequence (node), &x, &y, &z);
			scale = klass->convert_scale (self, x, y, z);
			lrg_scene_object_set_scale (obj, scale);
		}
	}

	/* Parse material */
	node = yaml_mapping_get_member (obj_map, "material");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
	{
		material_map = yaml_node_get_mapping (node);
		material = parse_material (material_map);
		lrg_scene_object_set_material (obj, material);
	}

	/* Parse params */
	node = yaml_mapping_get_member (obj_map, "params");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
	{
		params_map = yaml_node_get_mapping (node);
		parse_params (self, obj, params_map);
	}

	return obj;
}

/*
 * Parse a scene entity from YAML mapping.
 */
static LrgSceneEntity *
parse_scene_entity (LrgSceneSerializerYaml *self,
                    const gchar            *entity_name,
                    YamlMapping            *entity_map)
{
	LrgSceneEntity *entity;
	YamlNode       *objects_node;
	YamlSequence   *objects_seq;
	guint           i;
	guint           n_objects;

	entity = lrg_scene_entity_new (entity_name);

	if (entity_map == NULL)
		return entity;

	/* Parse objects array */
	objects_node = yaml_mapping_get_member (entity_map, "objects");
	if (objects_node == NULL || yaml_node_get_node_type (objects_node) != YAML_NODE_SEQUENCE)
		return entity;

	objects_seq = yaml_node_get_sequence (objects_node);
	n_objects = yaml_sequence_get_length (objects_seq);

	for (i = 0; i < n_objects; i++)
	{
		YamlNode                 *obj_node = yaml_sequence_get_element (objects_seq, i);
		g_autoptr(LrgSceneObject) obj = NULL;

		if (yaml_node_get_node_type (obj_node) != YAML_NODE_MAPPING)
			continue;

		obj = parse_scene_object (self, yaml_node_get_mapping (obj_node));
		if (obj != NULL)
			lrg_scene_entity_add_object (entity, obj);
	}

	return entity;
}

/*
 * Parse scene from YAML root node.
 */
static LrgScene *
parse_scene_from_root (LrgSceneSerializerYaml *self,
                       YamlNode               *root,
                       GError                **error)
{
	LrgScene    *scene = NULL;
	YamlMapping *root_map;
	YamlNode    *scene_node;
	YamlMapping *scene_map;
	YamlNode    *entities_node;
	YamlMapping *entities_map;
	const gchar *scene_name = NULL;
	const gchar *exported_from = NULL;
	const gchar *export_date_str = NULL;
	GList       *entity_names;
	GList       *iter;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_INVALID_FORMAT,
		             "Root node is not a mapping");
		return NULL;
	}

	root_map = yaml_node_get_mapping (root);

	/* Parse scene metadata */
	scene_node = yaml_mapping_get_member (root_map, "scene");
	if (scene_node != NULL && yaml_node_get_node_type (scene_node) == YAML_NODE_MAPPING)
	{
		scene_map = yaml_node_get_mapping (scene_node);
		scene_name = yaml_mapping_get_string_member (scene_map, "name");
		exported_from = yaml_mapping_get_string_member (scene_map, "exported_from");
		export_date_str = yaml_mapping_get_string_member (scene_map, "export_date");
	}

	scene = lrg_scene_new (scene_name);

	if (exported_from != NULL)
		lrg_scene_set_exported_from (scene, exported_from);

	if (export_date_str != NULL)
		lrg_scene_set_export_date_iso (scene, export_date_str);

	/* Parse entities */
	entities_node = yaml_mapping_get_member (root_map, "entities");
	if (entities_node == NULL || yaml_node_get_node_type (entities_node) != YAML_NODE_MAPPING)
		return scene;

	entities_map = yaml_node_get_mapping (entities_node);
	entity_names = yaml_mapping_get_members (entities_map);

	for (iter = entity_names; iter != NULL; iter = iter->next)
	{
		const gchar                *entity_name = iter->data;
		YamlNode                   *entity_node = yaml_mapping_get_member (entities_map, entity_name);
		g_autoptr(LrgSceneEntity)   entity = NULL;

		if (yaml_node_get_node_type (entity_node) != YAML_NODE_MAPPING)
			continue;

		entity = parse_scene_entity (self, entity_name, yaml_node_get_mapping (entity_node));
		lrg_scene_add_entity (scene, entity);
	}

	g_list_free (entity_names);

	return scene;
}

/* ==========================================================================
 * Helper Functions - Generation
 * ========================================================================== */

/*
 * Build a YAML sequence from a vector.
 * Uses %.17g format for full precision.
 */
static YamlSequence *
build_vector3_sequence (GrlVector3 *vec)
{
	YamlSequence *seq = yaml_sequence_new ();

	yaml_sequence_add_double_element (seq, (gdouble)vec->x);
	yaml_sequence_add_double_element (seq, (gdouble)vec->y);
	yaml_sequence_add_double_element (seq, (gdouble)vec->z);

	return seq;
}

/*
 * Build a YAML sequence from color components.
 */
static YamlSequence *
build_color4_sequence (gfloat r,
                       gfloat g,
                       gfloat b,
                       gfloat a)
{
	YamlSequence *seq = yaml_sequence_new ();

	yaml_sequence_add_double_element (seq, (gdouble)r);
	yaml_sequence_add_double_element (seq, (gdouble)g);
	yaml_sequence_add_double_element (seq, (gdouble)b);
	yaml_sequence_add_double_element (seq, (gdouble)a);

	return seq;
}

/*
 * Build transform mapping.
 */
static YamlMapping *
build_transform_mapping (GrlVector3 *location,
                         GrlVector3 *rotation,
                         GrlVector3 *scale)
{
	YamlMapping             *map = yaml_mapping_new ();
	g_autoptr(YamlSequence)  loc_seq = NULL;
	g_autoptr(YamlSequence)  rot_seq = NULL;
	g_autoptr(YamlSequence)  scl_seq = NULL;

	loc_seq = build_vector3_sequence (location);
	rot_seq = build_vector3_sequence (rotation);
	scl_seq = build_vector3_sequence (scale);

	yaml_mapping_set_sequence_member (map, "location", loc_seq);
	yaml_mapping_set_sequence_member (map, "rotation", rot_seq);
	yaml_mapping_set_sequence_member (map, "scale", scl_seq);

	return map;
}

/*
 * Build material mapping.
 */
static YamlMapping *
build_material_mapping (LrgMaterial3D *material)
{
	YamlMapping             *map = yaml_mapping_new ();
	gfloat                   r, g, b, a;
	g_autoptr(YamlSequence)  color_seq = NULL;
	g_autoptr(YamlSequence)  emission_seq = NULL;

	lrg_material3d_get_color (material, &r, &g, &b, &a);
	color_seq = build_color4_sequence (r, g, b, a);
	yaml_mapping_set_sequence_member (map, "color", color_seq);

	yaml_mapping_set_double_member (map, "roughness",
	                                (gdouble)lrg_material3d_get_roughness (material));
	yaml_mapping_set_double_member (map, "metallic",
	                                (gdouble)lrg_material3d_get_metallic (material));

	lrg_material3d_get_emission_color (material, &r, &g, &b, &a);
	emission_seq = build_color4_sequence (r, g, b, a);
	yaml_mapping_set_sequence_member (map, "emission_color", emission_seq);

	yaml_mapping_set_double_member (map, "emission_strength",
	                                (gdouble)lrg_material3d_get_emission_strength (material));

	return map;
}

/*
 * Build params mapping from scene object.
 */
static YamlMapping *
build_params_mapping (LrgSceneObject *obj)
{
	YamlMapping *map = yaml_mapping_new ();
	GList       *names;
	GList       *iter;
	gfloat       fval;
	gint         ival;
	gboolean     bval;
	const gchar *name;

	names = lrg_scene_object_get_param_names (obj);

	for (iter = names; iter != NULL; iter = iter->next)
	{
		name = iter->data;

		/* Try float first, then int, then bool */
		if (lrg_scene_object_has_param (obj, name))
		{
			/* We stored as specific types, retrieve appropriately */
			/* For simplicity, try float first since that's most common */
			fval = lrg_scene_object_get_param_float (obj, name, G_MAXFLOAT);
			if (fval != G_MAXFLOAT)
			{
				yaml_mapping_set_double_member (map, name, (gdouble)fval);
				continue;
			}

			ival = lrg_scene_object_get_param_int (obj, name, G_MAXINT);
			if (ival != G_MAXINT)
			{
				yaml_mapping_set_int_member (map, name, (gint64)ival);
				continue;
			}

			/* Bool */
			bval = lrg_scene_object_get_param_bool (obj, name, FALSE);
			yaml_mapping_set_boolean_member (map, name, bval);
		}
	}

	g_list_free (names);

	/* Handle mesh_data if present */
	{
		LrgMeshData *mesh_data = lrg_scene_object_get_mesh_data (obj);

		if (mesh_data != NULL && !lrg_mesh_data_is_empty (mesh_data))
		{
			g_autoptr(YamlMapping)  mesh_data_map = yaml_mapping_new ();
			g_autoptr(YamlSequence) vertices_seq = yaml_sequence_new ();
			g_autoptr(YamlSequence) faces_seq = yaml_sequence_new ();
			const gfloat           *vertices;
			const gint             *faces;
			guint                   n_vertices, n_faces, total_indices;
			guint                   i, pos;

			/* Build vertices array */
			vertices = lrg_mesh_data_get_vertices (mesh_data, &n_vertices);
			for (i = 0; i < n_vertices; i++)
			{
				g_autoptr(YamlSequence) vert_seq = yaml_sequence_new ();

				yaml_sequence_add_double_element (vert_seq,
				                                   (gdouble)vertices[i * 3 + 0]);
				yaml_sequence_add_double_element (vert_seq,
				                                   (gdouble)vertices[i * 3 + 1]);
				yaml_sequence_add_double_element (vert_seq,
				                                   (gdouble)vertices[i * 3 + 2]);
				yaml_sequence_add_sequence_element (vertices_seq, vert_seq);
			}

			/* Build faces array */
			faces = lrg_mesh_data_get_faces (mesh_data, &n_faces, &total_indices);
			pos = 0;
			for (i = 0; i < n_faces && pos < total_indices; i++)
			{
				g_autoptr(YamlSequence) face_seq = yaml_sequence_new ();
				gint                    vert_count = faces[pos++];
				gint                    j;

				for (j = 0; j < vert_count && pos < total_indices; j++)
				{
					yaml_sequence_add_int_element (face_seq, (gint64)faces[pos++]);
				}
				yaml_sequence_add_sequence_element (faces_seq, face_seq);
			}

			yaml_mapping_set_sequence_member (mesh_data_map, "vertices", vertices_seq);
			yaml_mapping_set_sequence_member (mesh_data_map, "faces", faces_seq);
			yaml_mapping_set_boolean_member (mesh_data_map, "smooth",
			                                  lrg_mesh_data_get_smooth (mesh_data));

			yaml_mapping_set_mapping_member (map, "mesh_data", mesh_data_map);
		}
	}

	return map;
}

/*
 * Build scene object mapping.
 */
static YamlMapping *
build_scene_object_mapping (LrgSceneObject *obj)
{
	YamlMapping            *map = yaml_mapping_new ();
	g_autoptr(YamlMapping)  transform_map = NULL;
	g_autoptr(YamlMapping)  material_map = NULL;
	g_autoptr(YamlMapping)  params_map = NULL;

	yaml_mapping_set_string_member (map, "name", lrg_scene_object_get_name (obj));
	yaml_mapping_set_string_member (map, "primitive",
	                                primitive_type_to_string (lrg_scene_object_get_primitive (obj)));

	transform_map = build_transform_mapping (lrg_scene_object_get_location (obj),
	                                         lrg_scene_object_get_rotation (obj),
	                                         lrg_scene_object_get_scale (obj));
	yaml_mapping_set_mapping_member (map, "transform", transform_map);

	material_map = build_material_mapping (lrg_scene_object_get_material (obj));
	yaml_mapping_set_mapping_member (map, "material", material_map);

	params_map = build_params_mapping (obj);
	yaml_mapping_set_mapping_member (map, "params", params_map);

	return map;
}

/*
 * Build entity mapping.
 */
static YamlMapping *
build_scene_entity_mapping (LrgSceneEntity *entity)
{
	YamlMapping             *map = yaml_mapping_new ();
	g_autoptr(YamlSequence)  objects_seq = yaml_sequence_new ();
	GPtrArray               *objects;
	guint                    i;

	objects = lrg_scene_entity_get_objects (entity);

	for (i = 0; i < objects->len; i++)
	{
		LrgSceneObject         *obj = g_ptr_array_index (objects, i);
		g_autoptr(YamlMapping)  obj_map = build_scene_object_mapping (obj);

		yaml_sequence_add_mapping_element (objects_seq, obj_map);
	}

	yaml_mapping_set_sequence_member (map, "objects", objects_seq);

	return map;
}

/*
 * Build root YAML node from scene.
 */
static YamlNode *
build_scene_yaml (LrgScene *scene)
{
	g_autoptr(YamlMapping)  root_map = yaml_mapping_new ();
	g_autoptr(YamlMapping)  scene_map = yaml_mapping_new ();
	g_autoptr(YamlMapping)  entities_map = yaml_mapping_new ();
	GList                  *entity_names;
	GList                  *iter;
	GDateTime              *export_date;

	/* Build scene metadata */
	if (lrg_scene_get_name (scene) != NULL)
		yaml_mapping_set_string_member (scene_map, "name", lrg_scene_get_name (scene));

	if (lrg_scene_get_exported_from (scene) != NULL)
		yaml_mapping_set_string_member (scene_map, "exported_from", lrg_scene_get_exported_from (scene));

	export_date = lrg_scene_get_export_date (scene);
	if (export_date != NULL)
	{
		g_autofree gchar *date_str = g_date_time_format_iso8601 (export_date);
		yaml_mapping_set_string_member (scene_map, "export_date", date_str);
	}

	yaml_mapping_set_mapping_member (root_map, "scene", scene_map);

	/* Build entities */
	entity_names = lrg_scene_get_entity_names (scene);

	for (iter = entity_names; iter != NULL; iter = iter->next)
	{
		const gchar            *name = iter->data;
		LrgSceneEntity         *entity = lrg_scene_get_entity (scene, name);
		g_autoptr(YamlMapping)  entity_map = build_scene_entity_mapping (entity);

		yaml_mapping_set_mapping_member (entities_map, name, entity_map);
	}

	g_list_free (entity_names);

	yaml_mapping_set_mapping_member (root_map, "entities", entities_map);

	return yaml_node_new_mapping (g_steal_pointer (&root_map));
}

/* ==========================================================================
 * Interface Implementation
 * ========================================================================== */

static LrgScene *
lrg_scene_serializer_yaml_load_from_file_impl (LrgSceneSerializer  *serializer,
                                               const gchar         *path,
                                               GError             **error)
{
	LrgSceneSerializerYaml *self = LRG_SCENE_SERIALIZER_YAML (serializer);
	g_autoptr(YamlParser)   parser = NULL;
	YamlNode               *root;
	LrgScene               *scene;

	parser = yaml_parser_new ();

	if (!yaml_parser_load_from_file (parser, path, error))
		return NULL;

	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_PARSE,
		             "No YAML document found in file");
		return NULL;
	}

	scene = parse_scene_from_root (self, root, error);
	return scene;
}

static LrgScene *
lrg_scene_serializer_yaml_load_from_data_impl (LrgSceneSerializer  *serializer,
                                               const gchar         *data,
                                               gssize               length,
                                               GError             **error)
{
	LrgSceneSerializerYaml *self = LRG_SCENE_SERIALIZER_YAML (serializer);
	g_autoptr(YamlParser)   parser = NULL;
	YamlNode               *root;
	LrgScene               *scene;

	parser = yaml_parser_new ();

	if (!yaml_parser_load_from_data (parser, data, length, error))
		return NULL;

	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error (error,
		             LRG_SCENE_ERROR,
		             LRG_SCENE_ERROR_PARSE,
		             "No YAML document found in data");
		return NULL;
	}

	scene = parse_scene_from_root (self, root, error);
	return scene;
}

static gboolean
lrg_scene_serializer_yaml_save_to_file_impl (LrgSceneSerializer  *serializer,
                                             LrgScene            *scene,
                                             const gchar         *path,
                                             GError             **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	g_autofree gchar        *yaml_str = NULL;

	root = build_scene_yaml (scene);
	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();

	yaml_generator_set_document (gen, doc);
	yaml_str = yaml_generator_to_data (gen, NULL, error);
	if (yaml_str == NULL)
		return FALSE;

	if (!g_file_set_contents (path, yaml_str, -1, error))
		return FALSE;

	return TRUE;
}

static gchar *
lrg_scene_serializer_yaml_save_to_data_impl (LrgSceneSerializer *serializer,
                                             LrgScene           *scene,
                                             gsize              *length)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;

	root = build_scene_yaml (scene);
	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();

	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, length, NULL);
}

static void
lrg_scene_serializer_iface_init (LrgSceneSerializerInterface *iface)
{
	iface->load_from_file = lrg_scene_serializer_yaml_load_from_file_impl;
	iface->load_from_data = lrg_scene_serializer_yaml_load_from_data_impl;
	iface->save_to_file   = lrg_scene_serializer_yaml_save_to_file_impl;
	iface->save_to_data   = lrg_scene_serializer_yaml_save_to_data_impl;
}

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_scene_serializer_yaml_class_init (LrgSceneSerializerYamlClass *klass)
{
	/* Set default virtual method implementations */
	klass->convert_position = lrg_scene_serializer_yaml_real_convert_position;
	klass->convert_rotation = lrg_scene_serializer_yaml_real_convert_rotation;
	klass->convert_scale    = lrg_scene_serializer_yaml_real_convert_scale;
	klass->should_reverse_face_winding = lrg_scene_serializer_yaml_real_should_reverse_face_winding;
}

static void
lrg_scene_serializer_yaml_init (LrgSceneSerializerYaml *self)
{
	/* Nothing to initialize */
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_scene_serializer_yaml_new:
 *
 * Creates a new #LrgSceneSerializerYaml with no coordinate conversion.
 *
 * For Blender scenes, use #LrgSceneSerializerBlender instead.
 *
 * Returns: (transfer full): A new #LrgSceneSerializerYaml
 */
LrgSceneSerializerYaml *
lrg_scene_serializer_yaml_new (void)
{
	return g_object_new (LRG_TYPE_SCENE_SERIALIZER_YAML, NULL);
}
