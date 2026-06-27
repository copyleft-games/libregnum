/* lrg-level-serializer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * YAML (.rlevel) serializer for #LrgLevel documents.
 */

#include "lrg-level-serializer.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"
#include "../scene/lrg-material3d.h"
#include "../lrg-enums.h"
#include <graylib.h>
#include <yaml-glib.h>

struct _LrgLevelSerializer
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgLevelSerializer, lrg_level_serializer, G_TYPE_OBJECT)

static void
lrg_level_serializer_class_init (LrgLevelSerializerClass *klass)
{
}

static void
lrg_level_serializer_init (LrgLevelSerializer *self)
{
}

/* ==========================================================================
 * Enum <-> nick helpers
 * ========================================================================== */

static const gchar *
enum_to_nick (GType enum_type, gint value)
{
	g_autoptr(GEnumClass)  klass = g_type_class_ref (enum_type);
	GEnumValue            *ev = g_enum_get_value (klass, value);

	return (ev != NULL) ? ev->value_nick : NULL;
}

static gint
nick_to_enum (GType enum_type, const gchar *nick, gint fallback)
{
	g_autoptr(GEnumClass)  klass = NULL;
	GEnumValue            *ev;

	if (nick == NULL)
		return fallback;

	klass = g_type_class_ref (enum_type);
	ev = g_enum_get_value_by_nick (klass, nick);

	return (ev != NULL) ? ev->value : fallback;
}

/* ==========================================================================
 * GValue <-> YAML scalar helpers
 * ========================================================================== */

/* Store a #GValue as a typed YAML scalar member. */
static void
set_value_member (YamlMapping  *map,
                  const gchar  *key,
                  const GValue *value)
{
	GType vtype = G_VALUE_TYPE (value);

	switch (vtype)
	{
	case G_TYPE_STRING:
		yaml_mapping_set_string_member (map, key, g_value_get_string (value));
		break;
	case G_TYPE_BOOLEAN:
		yaml_mapping_set_boolean_member (map, key, g_value_get_boolean (value));
		break;
	case G_TYPE_INT:
		yaml_mapping_set_int_member (map, key, (gint64) g_value_get_int (value));
		break;
	case G_TYPE_UINT:
		yaml_mapping_set_int_member (map, key, (gint64) g_value_get_uint (value));
		break;
	case G_TYPE_INT64:
		yaml_mapping_set_int_member (map, key, g_value_get_int64 (value));
		break;
	case G_TYPE_FLOAT:
		yaml_mapping_set_double_member (map, key, (gdouble) g_value_get_float (value));
		break;
	case G_TYPE_DOUBLE:
		yaml_mapping_set_double_member (map, key, g_value_get_double (value));
		break;
	default:
		{
			/* Fall back to a transformed string representation. */
			GValue strv = G_VALUE_INIT;

			g_value_init (&strv, G_TYPE_STRING);
			if (g_value_transform (value, &strv))
				yaml_mapping_set_string_member (map, key, g_value_get_string (&strv));
			else
			{
				g_autofree gchar *repr = g_strdup_value_contents (value);
				yaml_mapping_set_string_member (map, key, repr);
			}
			g_value_unset (&strv);
		}
		break;
	}
}

/* Heuristically decode a YAML scalar string into a typed #GValue. */
static void
value_from_scalar (const gchar *s,
                   GValue      *out)
{
	gchar  *end = NULL;
	gint64  iv;
	gdouble dv;

	if (s == NULL)
	{
		g_value_init (out, G_TYPE_STRING);
		g_value_set_string (out, NULL);
		return;
	}

	if (g_ascii_strcasecmp (s, "true") == 0)
	{
		g_value_init (out, G_TYPE_BOOLEAN);
		g_value_set_boolean (out, TRUE);
		return;
	}
	if (g_ascii_strcasecmp (s, "false") == 0)
	{
		g_value_init (out, G_TYPE_BOOLEAN);
		g_value_set_boolean (out, FALSE);
		return;
	}

	iv = g_ascii_strtoll (s, &end, 10);
	if (s[0] != '\0' && end != NULL && *end == '\0')
	{
		g_value_init (out, G_TYPE_INT64);
		g_value_set_int64 (out, iv);
		return;
	}

	dv = g_ascii_strtod (s, &end);
	if (s[0] != '\0' && end != NULL && *end == '\0')
	{
		g_value_init (out, G_TYPE_DOUBLE);
		g_value_set_double (out, dv);
		return;
	}

	g_value_init (out, G_TYPE_STRING);
	g_value_set_string (out, s);
}

static gdouble
seq_get_double (YamlSequence *seq,
                guint         index,
                gdouble       fallback)
{
	YamlNode    *node;
	const gchar *scalar;

	if (seq == NULL || index >= yaml_sequence_get_length (seq))
		return fallback;

	node = yaml_sequence_get_element (seq, index);
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return fallback;

	scalar = yaml_node_get_scalar (node);
	if (scalar == NULL)
		return fallback;

	return g_ascii_strtod (scalar, NULL);
}

/* ==========================================================================
 * Build (write) path
 * ========================================================================== */

static YamlSequence *
build_vector3_sequence (GrlVector3 *v)
{
	YamlSequence *seq = yaml_sequence_new ();

	yaml_sequence_add_double_element (seq, (gdouble) (v != NULL ? v->x : 0.0f));
	yaml_sequence_add_double_element (seq, (gdouble) (v != NULL ? v->y : 0.0f));
	yaml_sequence_add_double_element (seq, (gdouble) (v != NULL ? v->z : 0.0f));

	return seq;
}

static YamlMapping *
build_material_mapping (LrgMaterial3D *material)
{
	YamlMapping             *map = yaml_mapping_new ();
	g_autoptr(YamlSequence)  color_seq = yaml_sequence_new ();
	gfloat                   r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

	lrg_material3d_get_color (material, &r, &g, &b, &a);
	yaml_sequence_add_double_element (color_seq, (gdouble) r);
	yaml_sequence_add_double_element (color_seq, (gdouble) g);
	yaml_sequence_add_double_element (color_seq, (gdouble) b);
	yaml_sequence_add_double_element (color_seq, (gdouble) a);
	yaml_mapping_set_sequence_member (map, "color", color_seq);

	yaml_mapping_set_double_member (map, "roughness",
	                                (gdouble) lrg_material3d_get_roughness (material));
	yaml_mapping_set_double_member (map, "metallic",
	                                (gdouble) lrg_material3d_get_metallic (material));

	return map;
}

static YamlMapping *
build_visual_mapping (LrgNodeVisual *visual)
{
	YamlMapping  *map = yaml_mapping_new ();
	const gchar  *kind_nick;
	const gchar  *asset;
	LrgMaterial3D *material;
	GList        *param_names;
	GList        *iter;

	kind_nick = enum_to_nick (LRG_TYPE_NODE_VISUAL_KIND,
	                          lrg_node_visual_get_kind (visual));
	if (kind_nick != NULL)
		yaml_mapping_set_string_member (map, "kind", kind_nick);

	if (lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_PRIMITIVE)
	{
		const gchar *prim_nick = enum_to_nick (LRG_TYPE_PRIMITIVE_TYPE,
		                                        lrg_node_visual_get_primitive (visual));
		if (prim_nick != NULL)
			yaml_mapping_set_string_member (map, "primitive", prim_nick);
	}

	asset = lrg_node_visual_get_asset (visual);
	if (asset != NULL)
		yaml_mapping_set_string_member (map, "asset", asset);

	material = lrg_node_visual_get_material (visual);
	if (material != NULL)
	{
		g_autoptr(YamlMapping) mat_map = build_material_mapping (material);
		yaml_mapping_set_mapping_member (map, "material", mat_map);
	}

	param_names = lrg_node_visual_get_param_names (visual);
	if (param_names != NULL)
	{
		g_autoptr(YamlMapping) params_map = yaml_mapping_new ();

		for (iter = param_names; iter != NULL; iter = iter->next)
		{
			const gchar  *name = iter->data;
			const GValue *value = lrg_node_visual_get_param (visual, name);

			if (value != NULL)
				set_value_member (params_map, name, value);
		}
		yaml_mapping_set_mapping_member (map, "params", params_map);
	}
	g_list_free (param_names);

	return map;
}

static YamlMapping *
build_component_mapping (LrgComponentDesc *desc)
{
	YamlMapping *map = yaml_mapping_new ();
	const gchar *type_name;
	GList       *keys;
	GList       *iter;

	type_name = lrg_component_desc_get_type_name (desc);
	if (type_name != NULL)
		yaml_mapping_set_string_member (map, "type", type_name);

	keys = lrg_component_desc_get_keys (desc);
	if (keys != NULL)
	{
		g_autoptr(YamlMapping) props_map = yaml_mapping_new ();

		for (iter = keys; iter != NULL; iter = iter->next)
		{
			const gchar  *name = iter->data;
			const GValue *value = lrg_component_desc_get_value (desc, name);

			if (value != NULL)
				set_value_member (props_map, name, value);
		}
		yaml_mapping_set_mapping_member (map, "props", props_map);
	}
	g_list_free (keys);

	return map;
}

static YamlMapping *
build_script_mapping (LrgScriptBinding *binding)
{
	YamlMapping *map = yaml_mapping_new ();
	const gchar *lang_nick;
	const gchar *script;

	lang_nick = enum_to_nick (LRG_TYPE_SCRIPT_LANGUAGE,
	                          lrg_script_binding_get_language (binding));
	if (lang_nick != NULL)
		yaml_mapping_set_string_member (map, "language", lang_nick);

	script = lrg_script_binding_get_script (binding);
	if (script != NULL)
		yaml_mapping_set_string_member (map, "script", script);

	yaml_mapping_set_boolean_member (map, "enabled",
	                                 lrg_script_binding_get_enabled (binding));

	return map;
}

static YamlMapping *
build_node_mapping (LrgNode *node)
{
	YamlMapping             *map = yaml_mapping_new ();
	g_autoptr(YamlMapping)   transform_map = yaml_mapping_new ();
	g_autoptr(YamlSequence)  loc_seq = NULL;
	g_autoptr(YamlSequence)  rot_seq = NULL;
	g_autoptr(YamlSequence)  scl_seq = NULL;
	LrgNodeVisual           *visual;
	GPtrArray               *components;
	GPtrArray               *scripts;
	GPtrArray               *children;
	guint                    i;

	if (lrg_node_get_name (node) != NULL)
		yaml_mapping_set_string_member (map, "name", lrg_node_get_name (node));
	if (lrg_node_get_guid (node) != NULL)
		yaml_mapping_set_string_member (map, "guid", lrg_node_get_guid (node));

	yaml_mapping_set_boolean_member (map, "is_2d", lrg_node_get_is_2d (node));
	yaml_mapping_set_boolean_member (map, "visible", lrg_node_get_visible (node));
	yaml_mapping_set_boolean_member (map, "locked", lrg_node_get_locked (node));

	loc_seq = build_vector3_sequence (lrg_node_get_location (node));
	rot_seq = build_vector3_sequence (lrg_node_get_rotation (node));
	scl_seq = build_vector3_sequence (lrg_node_get_scale (node));
	yaml_mapping_set_sequence_member (transform_map, "location", loc_seq);
	yaml_mapping_set_sequence_member (transform_map, "rotation", rot_seq);
	yaml_mapping_set_sequence_member (transform_map, "scale", scl_seq);
	yaml_mapping_set_mapping_member (map, "transform", transform_map);

	visual = lrg_node_get_visual (node);
	if (visual != NULL)
	{
		g_autoptr(YamlMapping) visual_map = build_visual_mapping (visual);
		yaml_mapping_set_mapping_member (map, "visual", visual_map);
	}

	components = lrg_node_get_components (node);
	if (components != NULL && components->len > 0)
	{
		g_autoptr(YamlSequence) comp_seq = yaml_sequence_new ();

		for (i = 0; i < components->len; i++)
		{
			LrgComponentDesc       *desc = g_ptr_array_index (components, i);
			g_autoptr(YamlMapping)  comp_map = build_component_mapping (desc);

			yaml_sequence_add_mapping_element (comp_seq, comp_map);
		}
		yaml_mapping_set_sequence_member (map, "components", comp_seq);
	}

	scripts = lrg_node_get_scripts (node);
	if (scripts != NULL && scripts->len > 0)
	{
		g_autoptr(YamlSequence) script_seq = yaml_sequence_new ();

		for (i = 0; i < scripts->len; i++)
		{
			LrgScriptBinding       *binding = g_ptr_array_index (scripts, i);
			g_autoptr(YamlMapping)  script_map = build_script_mapping (binding);

			yaml_sequence_add_mapping_element (script_seq, script_map);
		}
		yaml_mapping_set_sequence_member (map, "scripts", script_seq);
	}

	children = lrg_node_get_children (node);
	if (children != NULL && children->len > 0)
	{
		g_autoptr(YamlSequence) child_seq = yaml_sequence_new ();

		for (i = 0; i < children->len; i++)
		{
			LrgNode                *child = g_ptr_array_index (children, i);
			g_autoptr(YamlMapping)  child_map = build_node_mapping (child);

			yaml_sequence_add_mapping_element (child_seq, child_map);
		}
		yaml_mapping_set_sequence_member (map, "children", child_seq);
	}

	return map;
}

static YamlNode *
build_level_yaml (LrgLevel *level)
{
	g_autoptr(YamlMapping)   root_map = yaml_mapping_new ();
	g_autoptr(YamlSequence)  nodes_seq = yaml_sequence_new ();
	LrgNode                 *root;
	GPtrArray               *top;
	guint                    i;

	yaml_mapping_set_string_member (root_map, "type", "level");
	if (lrg_level_get_name (level) != NULL)
		yaml_mapping_set_string_member (root_map, "name", lrg_level_get_name (level));
	yaml_mapping_set_string_member (root_map, "default_view",
	                                lrg_level_get_default_2d (level) ? "2d" : "3d");

	root = lrg_level_get_root (level);
	top = lrg_node_get_children (root);
	for (i = 0; top != NULL && i < top->len; i++)
	{
		LrgNode                *node = g_ptr_array_index (top, i);
		g_autoptr(YamlMapping)  node_map = build_node_mapping (node);

		yaml_sequence_add_mapping_element (nodes_seq, node_map);
	}

	yaml_mapping_set_sequence_member (root_map, "nodes", nodes_seq);

	/* yaml_node_new_mapping refs the mapping (transfer none); let the autoptr
	 * drop our reference so the node is its sole owner. g_steal_pointer here
	 * would leak our reference and thus the entire tree. */
	return yaml_node_new_mapping (root_map);
}

/* ==========================================================================
 * Parse (read) path
 * ========================================================================== */

static LrgMaterial3D *
parse_material (YamlMapping *map)
{
	LrgMaterial3D *material = lrg_material3d_new ();
	YamlNode      *color_node;

	color_node = yaml_mapping_get_member (map, "color");
	if (color_node != NULL && yaml_node_get_node_type (color_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (color_node);

		lrg_material3d_set_color (material,
		                          (gfloat) seq_get_double (seq, 0, 1.0),
		                          (gfloat) seq_get_double (seq, 1, 1.0),
		                          (gfloat) seq_get_double (seq, 2, 1.0),
		                          (gfloat) seq_get_double (seq, 3, 1.0));
	}

	if (yaml_mapping_has_member (map, "roughness"))
		lrg_material3d_set_roughness (material,
		                              (gfloat) yaml_mapping_get_double_member (map, "roughness"));
	if (yaml_mapping_has_member (map, "metallic"))
		lrg_material3d_set_metallic (material,
		                             (gfloat) yaml_mapping_get_double_member (map, "metallic"));

	return material;
}

static void
parse_param_bag (YamlMapping *map,
                 void (*setter) (gpointer ctx, const gchar *name, const GValue *value),
                 gpointer ctx)
{
	GList *keys;
	GList *iter;

	keys = yaml_mapping_get_members (map);
	for (iter = keys; iter != NULL; iter = iter->next)
	{
		const gchar *name = iter->data;
		YamlNode    *node = yaml_mapping_get_member (map, name);
		GValue       value = G_VALUE_INIT;

		if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
			continue;

		value_from_scalar (yaml_node_get_scalar (node), &value);
		setter (ctx, name, &value);
		g_value_unset (&value);
	}
	g_list_free (keys);
}

static void
visual_param_setter (gpointer ctx, const gchar *name, const GValue *value)
{
	lrg_node_visual_set_param (LRG_NODE_VISUAL (ctx), name, value);
}

static void
component_prop_setter (gpointer ctx, const gchar *name, const GValue *value)
{
	lrg_component_desc_set_value (LRG_COMPONENT_DESC (ctx), name, value);
}

static LrgNodeVisual *
parse_visual (YamlMapping *map)
{
	LrgNodeVisual     *visual;
	LrgNodeVisualKind  kind;
	const gchar       *asset;
	YamlNode          *material_node;
	YamlNode          *params_node;

	kind = nick_to_enum (LRG_TYPE_NODE_VISUAL_KIND,
	                     yaml_mapping_get_string_member (map, "kind"),
	                     LRG_NODE_VISUAL_NONE);
	visual = lrg_node_visual_new (kind);

	if (yaml_mapping_has_member (map, "primitive"))
		lrg_node_visual_set_primitive (visual,
		    nick_to_enum (LRG_TYPE_PRIMITIVE_TYPE,
		                  yaml_mapping_get_string_member (map, "primitive"),
		                  LRG_PRIMITIVE_CUBE));

	asset = yaml_mapping_get_string_member (map, "asset");
	if (asset != NULL)
		lrg_node_visual_set_asset (visual, asset);

	material_node = yaml_mapping_get_member (map, "material");
	if (material_node != NULL && yaml_node_get_node_type (material_node) == YAML_NODE_MAPPING)
	{
		g_autoptr(LrgMaterial3D) material = parse_material (yaml_node_get_mapping (material_node));
		lrg_node_visual_set_material (visual, material);
	}

	params_node = yaml_mapping_get_member (map, "params");
	if (params_node != NULL && yaml_node_get_node_type (params_node) == YAML_NODE_MAPPING)
		parse_param_bag (yaml_node_get_mapping (params_node), visual_param_setter, visual);

	return visual;
}

static LrgComponentDesc *
parse_component (YamlMapping *map)
{
	LrgComponentDesc *desc;
	YamlNode         *props_node;

	desc = lrg_component_desc_new (yaml_mapping_get_string_member (map, "type"));

	props_node = yaml_mapping_get_member (map, "props");
	if (props_node != NULL && yaml_node_get_node_type (props_node) == YAML_NODE_MAPPING)
		parse_param_bag (yaml_node_get_mapping (props_node), component_prop_setter, desc);

	return desc;
}

static LrgScriptBinding *
parse_script (YamlMapping *map)
{
	LrgScriptBinding  *binding;
	LrgScriptLanguage  lang;

	lang = nick_to_enum (LRG_TYPE_SCRIPT_LANGUAGE,
	                     yaml_mapping_get_string_member (map, "language"),
	                     LRG_SCRIPT_LANGUAGE_NONE);
	binding = lrg_script_binding_new (lang,
	                                  yaml_mapping_get_string_member (map, "script"));

	if (yaml_mapping_has_member (map, "enabled"))
		lrg_script_binding_set_enabled (binding,
		                                yaml_mapping_get_boolean_member (map, "enabled"));

	return binding;
}

static LrgNode *
parse_node (YamlMapping *map)
{
	LrgNode  *node;
	const gchar *guid;
	YamlNode *transform_node;
	YamlNode *visual_node;
	YamlNode *components_node;
	YamlNode *scripts_node;
	YamlNode *children_node;

	node = lrg_node_new (yaml_mapping_get_string_member (map, "name"));

	guid = yaml_mapping_get_string_member (map, "guid");
	if (guid != NULL)
		lrg_node_set_guid (node, guid);

	if (yaml_mapping_has_member (map, "is_2d"))
		lrg_node_set_is_2d (node, yaml_mapping_get_boolean_member (map, "is_2d"));
	if (yaml_mapping_has_member (map, "visible"))
		lrg_node_set_visible (node, yaml_mapping_get_boolean_member (map, "visible"));
	if (yaml_mapping_has_member (map, "locked"))
		lrg_node_set_locked (node, yaml_mapping_get_boolean_member (map, "locked"));

	transform_node = yaml_mapping_get_member (map, "transform");
	if (transform_node != NULL && yaml_node_get_node_type (transform_node) == YAML_NODE_MAPPING)
	{
		YamlMapping *tmap = yaml_node_get_mapping (transform_node);
		YamlNode    *loc = yaml_mapping_get_member (tmap, "location");
		YamlNode    *rot = yaml_mapping_get_member (tmap, "rotation");
		YamlNode    *scl = yaml_mapping_get_member (tmap, "scale");

		if (loc != NULL && yaml_node_get_node_type (loc) == YAML_NODE_SEQUENCE)
		{
			YamlSequence *s = yaml_node_get_sequence (loc);
			lrg_node_set_location_xyz (node,
			                           (gfloat) seq_get_double (s, 0, 0.0),
			                           (gfloat) seq_get_double (s, 1, 0.0),
			                           (gfloat) seq_get_double (s, 2, 0.0));
		}
		if (rot != NULL && yaml_node_get_node_type (rot) == YAML_NODE_SEQUENCE)
		{
			YamlSequence *s = yaml_node_get_sequence (rot);
			lrg_node_set_rotation_xyz (node,
			                           (gfloat) seq_get_double (s, 0, 0.0),
			                           (gfloat) seq_get_double (s, 1, 0.0),
			                           (gfloat) seq_get_double (s, 2, 0.0));
		}
		if (scl != NULL && yaml_node_get_node_type (scl) == YAML_NODE_SEQUENCE)
		{
			YamlSequence *s = yaml_node_get_sequence (scl);
			lrg_node_set_scale_xyz (node,
			                        (gfloat) seq_get_double (s, 0, 1.0),
			                        (gfloat) seq_get_double (s, 1, 1.0),
			                        (gfloat) seq_get_double (s, 2, 1.0));
		}
	}

	visual_node = yaml_mapping_get_member (map, "visual");
	if (visual_node != NULL && yaml_node_get_node_type (visual_node) == YAML_NODE_MAPPING)
	{
		g_autoptr(LrgNodeVisual) visual = parse_visual (yaml_node_get_mapping (visual_node));
		lrg_node_set_visual (node, visual);
	}

	components_node = yaml_mapping_get_member (map, "components");
	if (components_node != NULL && yaml_node_get_node_type (components_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (components_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *cn = yaml_sequence_get_element (seq, i);

			if (cn != NULL && yaml_node_get_node_type (cn) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgComponentDesc) desc = parse_component (yaml_node_get_mapping (cn));
				lrg_node_add_component (node, desc);
			}
		}
	}

	scripts_node = yaml_mapping_get_member (map, "scripts");
	if (scripts_node != NULL && yaml_node_get_node_type (scripts_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (scripts_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *sn = yaml_sequence_get_element (seq, i);

			if (sn != NULL && yaml_node_get_node_type (sn) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgScriptBinding) binding = parse_script (yaml_node_get_mapping (sn));
				lrg_node_add_script (node, binding);
			}
		}
	}

	children_node = yaml_mapping_get_member (map, "children");
	if (children_node != NULL && yaml_node_get_node_type (children_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (children_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *cn = yaml_sequence_get_element (seq, i);

			if (cn != NULL && yaml_node_get_node_type (cn) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgNode) child = parse_node (yaml_node_get_mapping (cn));
				lrg_node_add_child (node, child);
			}
		}
	}

	return node;
}

static LrgLevel *
parse_level_from_root (YamlNode  *root,
                       GError   **error)
{
	YamlMapping *root_map;
	LrgLevel    *level;
	const gchar *name;
	const gchar *view;
	YamlNode    *nodes_node;

	if (yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "Level root is not a mapping");
		return NULL;
	}

	root_map = yaml_node_get_mapping (root);
	name = yaml_mapping_get_string_member (root_map, "name");
	level = lrg_level_new (name);

	view = yaml_mapping_get_string_member (root_map, "default_view");
	if (view != NULL && g_ascii_strcasecmp (view, "2d") == 0)
		lrg_level_set_default_2d (level, TRUE);

	nodes_node = yaml_mapping_get_member (root_map, "nodes");
	if (nodes_node != NULL && yaml_node_get_node_type (nodes_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (nodes_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *cn = yaml_sequence_get_element (seq, i);

			if (cn != NULL && yaml_node_get_node_type (cn) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgNode) node = parse_node (yaml_node_get_mapping (cn));
				lrg_level_add_node (level, node, NULL);
			}
		}
	}

	return level;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgLevelSerializer *
lrg_level_serializer_new (void)
{
	return g_object_new (LRG_TYPE_LEVEL_SERIALIZER, NULL);
}

LrgLevel *
lrg_level_serializer_load_from_file (LrgLevelSerializer  *self,
                                     const gchar         *path,
                                     GError             **error)
{
	g_autoptr(YamlParser) parser = NULL;
	YamlNode             *root;

	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, error))
		return NULL;

	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "No YAML document found in file");
		return NULL;
	}

	return parse_level_from_root (root, error);
}

LrgLevel *
lrg_level_serializer_load_from_data (LrgLevelSerializer  *self,
                                     const gchar         *data,
                                     gssize               length,
                                     GError             **error)
{
	g_autoptr(YamlParser) parser = NULL;
	YamlNode             *root;

	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), NULL);
	g_return_val_if_fail (data != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, error))
		return NULL;

	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "No YAML document found in data");
		return NULL;
	}

	return parse_level_from_root (root, error);
}

gboolean
lrg_level_serializer_save_to_file (LrgLevelSerializer  *self,
                                   LrgLevel            *level,
                                   const gchar         *path,
                                   GError             **error)
{
	g_autofree gchar *data = NULL;
	gsize             length = 0;

	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), FALSE);
	g_return_val_if_fail (LRG_IS_LEVEL (level), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	data = lrg_level_serializer_save_to_data (self, level, &length);
	if (data == NULL)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_FAILED,
		             "Failed to serialize level");
		return FALSE;
	}

	return g_file_set_contents (path, data, (gssize) length, error);
}

gchar *
lrg_level_serializer_save_to_data (LrgLevelSerializer *self,
                                   LrgLevel           *level,
                                   gsize              *length)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;

	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), NULL);
	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);

	root = build_level_yaml (level);
	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();

	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, length, NULL);
}
