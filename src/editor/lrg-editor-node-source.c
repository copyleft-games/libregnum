/* lrg-editor-node-source.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorNodeSource: one section of one LrgNode as an LrgPropertySource.
 *
 * How it works, in short:
 *
 * - The property list ("schema") of a section is a list of Entry records:
 *   a slot (which value of the node it is), a GParamSpec giving type,
 *   range and default, the LrgPropertyInfo handed to the grid, and the
 *   last value/state seen (for coalesced change emission). Header and
 *   transform schemas are fixed; visual, component and scripts schemas
 *   follow the node (visual kind, param bag keys, stored component keys,
 *   number of scripts) and are rebuilt when their signature changes.
 *
 * - Every write mutates the node in place. Outside a gesture the write is
 *   wrapped in snapshot + lrg_editor_commit_node_state(), so it is one
 *   undoable NODE_STATE command and marks the prefab overrides it touches
 *   (the editor's execute path diffs the snapshot). Inside a gesture the
 *   write is only a preview; end_gesture() commits the whole difference
 *   once or restores the snapshot exactly.
 *
 * - The grid link (connect_grid) maps the grid's DELEGATE signals onto
 *   gestures of all its sources, wrapping the commits in one editor
 *   group so a drag over N nodes is one undo step.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <graylib.h>

#include "lrg-editor-node-source.h"
#include "lrg-editor.h"
#include "lrg-editor-math.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"
#include "lrg-prefab.h"
#include "lrg-timeline.h"
#include "../lrg-enums.h"
#include "../scene/lrg-material3d.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"

#define RAD2DEG (180.0 / G_PI)
#define DEG2RAD (G_PI / 180.0)

/* ==========================================================================
 * LrgEditorNodeSection
 * ========================================================================== */

GType
lrg_editor_node_section_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_NODE_SECTION_HEADER, "LRG_EDITOR_NODE_SECTION_HEADER", "header" },
			{ LRG_EDITOR_NODE_SECTION_TRANSFORM, "LRG_EDITOR_NODE_SECTION_TRANSFORM", "transform" },
			{ LRG_EDITOR_NODE_SECTION_VISUAL, "LRG_EDITOR_NODE_SECTION_VISUAL", "visual" },
			{ LRG_EDITOR_NODE_SECTION_COMPONENT, "LRG_EDITOR_NODE_SECTION_COMPONENT", "component" },
			{ LRG_EDITOR_NODE_SECTION_SCRIPTS, "LRG_EDITOR_NODE_SECTION_SCRIPTS", "scripts" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorNodeSection"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Entries
 * ========================================================================== */

/* Which value of the node an entry reads and writes */
typedef enum
{
	SLOT_NAME,
	SLOT_VISIBLE,
	SLOT_LOCKED,
	SLOT_IS_2D,
	SLOT_PATH,
	SLOT_GUID,
	SLOT_LOCATION,
	SLOT_ROTATION,
	SLOT_SCALE,
	SLOT_WORLD,
	SLOT_KIND,
	SLOT_PRIMITIVE,
	SLOT_ASSET,
	SLOT_COLOR,
	SLOT_ROUGHNESS,
	SLOT_METALLIC,
	SLOT_EMISSION,
	SLOT_PARAM,
	SLOT_COMP_TYPE,
	SLOT_COMP_PROP,
	SLOT_COMP_RAW,
	SLOT_SCRIPT_LANGUAGE,
	SLOT_SCRIPT_PATH,
	SLOT_SCRIPT_ENABLED
} Slot;

typedef struct
{
	Slot              slot;
	gchar            *key;          /* param or component key */
	guint             index;        /* script index */
	GParamSpec       *pspec;        /* owned reference */
	LrgPropertyInfo  *info;         /* owned reference */
	gboolean          has_default;
	GValue            def;          /* the default, when has_default */
	GValue            last;         /* last value seen (coalescing) */
	LrgPropertyState  last_state;
} Entry;

static void
entry_free (gpointer data)
{
	Entry *e = data;

	g_free (e->key);
	g_clear_pointer (&e->info, lrg_property_info_unref);
	g_clear_pointer (&e->pspec, g_param_spec_unref);
	if (G_IS_VALUE (&e->def))
		g_value_unset (&e->def);
	if (G_IS_VALUE (&e->last))
		g_value_unset (&e->last);
	g_free (e);
}

static const gchar *
entry_name (const Entry *e)
{
	return e->pspec->name;
}

/* ==========================================================================
 * Visual tables (ported from the 0.8.0 inspector)
 * ========================================================================== */

/* Parameters every node of a kind usually has: listed even when unset,
 * with their default and an editor hint. */
static const struct
{
	LrgNodeVisualKind  kind;
	const gchar       *param;
	gdouble            value;
	const gchar       *hint;
	const gchar       *tooltip;
} suggested_params[] = {
	{ LRG_NODE_VISUAL_LIGHT, "intensity", 1.0, "range:0,100,0.01", "Light intensity" },
	{ LRG_NODE_VISUAL_LIGHT, "range", 10.0, "range:0,1000,0.1", "Reach of a point or spot light, in units" },
	{ LRG_NODE_VISUAL_LIGHT, "spot_angle", 0.0, "angle:degrees", "Spot cone angle; 0 for a point light" },
	{ LRG_NODE_VISUAL_CAMERA, "fov", 60.0, "range:1,179,0.5", "Vertical field of view in degrees" },
	{ LRG_NODE_VISUAL_CAMERA, "near", 0.1, "range:0.001,100,0.01", "Near clip plane" },
	{ LRG_NODE_VISUAL_CAMERA, "far", 1000.0, "range:1,100000,1", "Far clip plane" },
	{ LRG_NODE_VISUAL_AUDIO_EMITTER, "volume", 1.0, "range:0,4,0.01", "Playback volume" },
	{ LRG_NODE_VISUAL_AUDIO_EMITTER, "radius", 10.0, "range:0,1000,0.1", "Audible radius" },
	{ LRG_NODE_VISUAL_SPRITE, "frame", 0.0, "range:0,10000,1", "Animation frame" },
	{ LRG_NODE_VISUAL_SPRITE, "pixels_per_unit", 32.0, "range:1,4096,1", "Texture pixels per world unit" },
	{ LRG_NODE_VISUAL_TILEMAP, "tile_size", 1.0, "range:0.01,100,0.01", "Tile size in world units" },
};

static gint
find_suggested (LrgNodeVisualKind  kind,
                const gchar       *param)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (suggested_params); i++)
		if (suggested_params[i].kind == kind && g_strcmp0 (suggested_params[i].param, param) == 0)
			return (gint) i;
	return -1;
}

static gboolean
kind_has_asset (LrgNodeVisualKind kind)
{
	return kind == LRG_NODE_VISUAL_MESH_ASSET || kind == LRG_NODE_VISUAL_SPRITE ||
	       kind == LRG_NODE_VISUAL_TILEMAP || kind == LRG_NODE_VISUAL_PREFAB_INSTANCE ||
	       kind == LRG_NODE_VISUAL_CAD_PART || kind == LRG_NODE_VISUAL_AUDIO_EMITTER;
}

/* The asset kind of the "asset:<kind>" hint (the asset database's
 * importer names) */
static const gchar *
asset_hint_for_kind (LrgNodeVisualKind kind)
{
	switch (kind)
	{
	case LRG_NODE_VISUAL_MESH_ASSET:      return "asset:model";
	case LRG_NODE_VISUAL_SPRITE:          return "asset:texture";
	case LRG_NODE_VISUAL_TILEMAP:         return "asset:tileset";
	case LRG_NODE_VISUAL_PREFAB_INSTANCE: return "asset:prefab";
	case LRG_NODE_VISUAL_CAD_PART:        return "asset:cad";
	case LRG_NODE_VISUAL_AUDIO_EMITTER:   return "asset:audio";
	default:                              return "asset";
	}
}

static gboolean
kind_has_material (LrgNodeVisualKind kind)
{
	return kind == LRG_NODE_VISUAL_PRIMITIVE || kind == LRG_NODE_VISUAL_MESH_ASSET ||
	       kind == LRG_NODE_VISUAL_SPRITE || kind == LRG_NODE_VISUAL_LIGHT;
}

/* ==========================================================================
 * Value helpers
 * ========================================================================== */

/*
 * make_pspec_for_type:
 *
 * A synthetic pspec for a raw value of @type (visual params, component
 * keys without a registered property). NULL for types the grid cannot
 * show.
 */
static GParamSpec *
make_pspec_for_type (const gchar *name,
                     GType        type,
                     gboolean     read_only)
{
	GParamFlags flags = read_only ? G_PARAM_READABLE : G_PARAM_READWRITE;

	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_BOOLEAN:
		return g_param_spec_boolean (name, NULL, NULL, FALSE, flags);
	case G_TYPE_INT:
		return g_param_spec_int (name, NULL, NULL, G_MININT, G_MAXINT, 0, flags);
	case G_TYPE_UINT:
		return g_param_spec_uint (name, NULL, NULL, 0, G_MAXUINT, 0, flags);
	case G_TYPE_INT64:
		return g_param_spec_int64 (name, NULL, NULL, G_MININT64, G_MAXINT64, 0, flags);
	case G_TYPE_UINT64:
		return g_param_spec_uint64 (name, NULL, NULL, 0, G_MAXUINT64, 0, flags);
	case G_TYPE_LONG:
		return g_param_spec_long (name, NULL, NULL, G_MINLONG, G_MAXLONG, 0, flags);
	case G_TYPE_ULONG:
		return g_param_spec_ulong (name, NULL, NULL, 0, G_MAXULONG, 0, flags);
	case G_TYPE_FLOAT:
		return g_param_spec_float (name, NULL, NULL, -G_MAXFLOAT, G_MAXFLOAT, 0.0f, flags);
	case G_TYPE_DOUBLE:
		return g_param_spec_double (name, NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, flags);
	case G_TYPE_STRING:
		return g_param_spec_string (name, NULL, NULL, NULL, flags);
	case G_TYPE_ENUM:
		{
			g_autoptr(GEnumClass) klass = g_type_class_ref (type);

			if (klass->n_values == 0)
				return NULL;
			return g_param_spec_enum (name, NULL, NULL, type, klass->values[0].value, flags);
		}
	case G_TYPE_FLAGS:
		return g_param_spec_flags (name, NULL, NULL, type, 0, flags);
	case G_TYPE_BOXED:
		return g_param_spec_boxed (name, NULL, NULL, type, flags);
	default:
		return NULL;
	}
}

/*
 * convert_into:
 *
 * Stores @src into the initialised @dst of the property's type. Besides
 * GLib's transforms this parses strings (enum and flags nicks, numbers,
 * as the YAML loader keeps unknown scalars) and maps integers onto enums
 * and flags. Returns FALSE (leaving @dst alone) when nothing fits.
 */
static gboolean
convert_into (const GValue *src,
              GValue       *dst)
{
	GType dtype = G_VALUE_TYPE (dst);
	GType stype = G_VALUE_TYPE (src);

	if (stype == dtype)
	{
		g_value_copy (src, dst);
		return TRUE;
	}

	/* Integers onto enums and flags: GLib only transforms the other way */
	if ((G_TYPE_FUNDAMENTAL (dtype) == G_TYPE_ENUM || G_TYPE_FUNDAMENTAL (dtype) == G_TYPE_FLAGS) &&
	    g_value_type_transformable (stype, G_TYPE_INT64) && G_TYPE_FUNDAMENTAL (stype) != G_TYPE_STRING)
	{
		g_auto(GValue) as_int = G_VALUE_INIT;

		g_value_init (&as_int, G_TYPE_INT64);
		if (!g_value_transform (src, &as_int))
			return FALSE;
		if (G_TYPE_FUNDAMENTAL (dtype) == G_TYPE_ENUM)
			g_value_set_enum (dst, (gint) g_value_get_int64 (&as_int));
		else
			g_value_set_flags (dst, (guint) g_value_get_int64 (&as_int));
		return TRUE;
	}

	/* Strings: the grid's text format (enum nicks, "a|b" flags, numbers) */
	if (G_VALUE_HOLDS_STRING (src) && dtype != G_TYPE_STRING)
		return g_value_get_string (src) != NULL &&
		       lrg_property_value_from_string (dst, g_value_get_string (src));

	if (g_value_type_transformable (stype, dtype))
		return g_value_transform (src, dst);

	return FALSE;
}

static GrlVector3 *
vec3_copy (const GrlVector3 *v,
           gdouble           scale)
{
	return grl_vector3_new ((gfloat) (v->x * scale), (gfloat) (v->y * scale), (gfloat) (v->z * scale));
}

static guint8
unit_to_byte (gfloat f)
{
	return (guint8) (CLAMP (f, 0.0f, 1.0f) * 255.0f + 0.5f);
}

/* ==========================================================================
 * LrgEditorNodeSource
 * ========================================================================== */

struct _LrgEditorNodeSource
{
	GObject               parent_instance;

	LrgEditor            *editor;
	LrgNode              *node;
	LrgEditorNodeSection  section;
	gchar                *component_type;
	gchar                *asset_root;

	/* Schema: Entry records in display order plus a name index */
	GPtrArray            *entries;
	GHashTable           *by_name;
	gchar                *signature;

	/* Change tracking */
	LrgLevel             *level;
	gulong                changed_id;
	gulong                replaced_id;
	gulong                node_changed_id;
	gboolean              refreshing;

	/* Gesture */
	guint                 gesture_depth;
	LrgNode              *snapshot;
};

enum
{
	PROP_0,
	PROP_EDITOR,
	PROP_NODE,
	PROP_SECTION,
	PROP_COMPONENT_TYPE,
	PROP_ASSET_ROOT,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* While > 0, change notifications from previews are batched by the grid
 * link and the per-source handlers stay quiet. */
static guint batch_depth = 0;

static void lrg_editor_node_source_iface_init (LrgPropertySourceInterface *iface);
static void refresh (LrgEditorNodeSource *self);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgEditorNodeSource, lrg_editor_node_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE,
                                                      lrg_editor_node_source_iface_init))

/* ==========================================================================
 * Component lookup
 * ========================================================================== */

/*
 * find_component:
 *
 * The component description this source edits: the Nth component of
 * the type ("Type#N", 1-based; plain "Type" is the first).
 */
static LrgComponentDesc *
find_component (LrgEditorNodeSource *self)
{
	g_autofree gchar *type = NULL;
	const gchar      *hash;
	guint             wanted = 1;
	guint             seen = 0;
	GPtrArray        *components;
	guint             i;

	if (self->component_type == NULL)
		return NULL;

	hash = strrchr (self->component_type, '#');
	if (hash != NULL && hash[1] != '\0' && g_ascii_isdigit (hash[1]))
	{
		type = g_strndup (self->component_type, (gsize) (hash - self->component_type));
		wanted = (guint) MAX (1, g_ascii_strtoull (hash + 1, NULL, 10));
	}
	else
		type = g_strdup (self->component_type);

	components = lrg_node_get_components (self->node);
	for (i = 0; components != NULL && i < components->len; i++)
	{
		LrgComponentDesc *desc = g_ptr_array_index (components, i);

		if (g_strcmp0 (lrg_component_desc_get_type_name (desc), type) == 0 && ++seen == wanted)
			return desc;
	}
	return NULL;
}

/* The GType of a component type name: the engine registry first, then
 * the GType system. */
static GType
lookup_component_type (const gchar *type_name)
{
	LrgEngine   *engine;
	LrgRegistry *reg;
	GType        type = G_TYPE_INVALID;

	if (type_name == NULL)
		return G_TYPE_INVALID;

	engine = lrg_engine_get_default ();
	reg = (engine != NULL) ? lrg_engine_get_registry (engine) : NULL;
	if (reg != NULL)
		type = lrg_registry_lookup (reg, type_name);
	if (type == G_TYPE_INVALID)
		type = g_type_from_name (type_name);
	return type;
}

static LrgScriptBinding *
script_at (LrgEditorNodeSource *self,
           guint                index)
{
	GPtrArray *scripts = lrg_node_get_scripts (self->node);

	return (scripts != NULL && index < scripts->len) ? g_ptr_array_index (scripts, index) : NULL;
}

/* ==========================================================================
 * Reading values
 * ========================================================================== */

static LrgNodeVisual *
node_visual (LrgEditorNodeSource *self)
{
	return lrg_node_get_visual (self->node);
}

/*
 * read_entry:
 *
 * Initialises @out to the entry's type and stores the node's current
 * value. Unset values (a suggested visual param, a component property
 * without a stored value) read as the default.
 */
static void
read_entry (LrgEditorNodeSource *self,
            Entry               *e,
            GValue              *out)
{
	LrgNodeVisual *visual = node_visual (self);
	LrgMaterial3D *material = (visual != NULL) ? lrg_node_visual_get_material (visual) : NULL;

	g_value_init (out, e->pspec->value_type);

	switch (e->slot)
	{
	case SLOT_NAME:
		g_value_set_string (out, lrg_node_get_name (self->node));
		break;
	case SLOT_VISIBLE:
		g_value_set_boolean (out, lrg_node_get_visible (self->node));
		break;
	case SLOT_LOCKED:
		g_value_set_boolean (out, lrg_node_get_locked (self->node));
		break;
	case SLOT_IS_2D:
		g_value_set_boolean (out, lrg_node_get_is_2d (self->node));
		break;
	case SLOT_PATH:
		g_value_take_string (out, lrg_node_get_path (self->node));
		break;
	case SLOT_GUID:
		g_value_set_string (out, lrg_node_get_guid (self->node));
		break;
	case SLOT_LOCATION:
		g_value_take_boxed (out, vec3_copy (lrg_node_get_location (self->node), 1.0));
		break;
	case SLOT_ROTATION:
		g_value_take_boxed (out, vec3_copy (lrg_node_get_rotation (self->node), RAD2DEG));
		break;
	case SLOT_SCALE:
		g_value_take_boxed (out, vec3_copy (lrg_node_get_scale (self->node), 1.0));
		break;
	case SLOT_WORLD:
		{
			gfloat w[3];

			lrg_editor_node_get_world_position (self->node, w);
			g_value_take_boxed (out, grl_vector3_new (w[0], w[1], w[2]));
		}
		break;
	case SLOT_KIND:
		g_value_set_enum (out, visual != NULL ? (gint) lrg_node_visual_get_kind (visual) : LRG_NODE_VISUAL_NONE);
		break;
	case SLOT_PRIMITIVE:
		g_value_set_enum (out, visual != NULL ? (gint) lrg_node_visual_get_primitive (visual) : 0);
		break;
	case SLOT_ASSET:
		g_value_set_string (out, visual != NULL ? lrg_node_visual_get_asset (visual) : NULL);
		break;
	case SLOT_COLOR:
		{
			gfloat r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

			if (material != NULL)
				lrg_material3d_get_color (material, &r, &g, &b, &a);
			g_value_take_boxed (out, grl_color_new (unit_to_byte (r), unit_to_byte (g),
			                                        unit_to_byte (b), unit_to_byte (a)));
		}
		break;
	case SLOT_ROUGHNESS:
		g_value_set_double (out, material != NULL ? (gdouble) lrg_material3d_get_roughness (material) : 0.5);
		break;
	case SLOT_METALLIC:
		g_value_set_double (out, material != NULL ? (gdouble) lrg_material3d_get_metallic (material) : 0.0);
		break;
	case SLOT_EMISSION:
		g_value_set_double (out, material != NULL ? (gdouble) lrg_material3d_get_emission_strength (material) : 0.0);
		break;
	case SLOT_PARAM:
		{
			const GValue *stored = (visual != NULL) ? lrg_node_visual_get_param (visual, e->key) : NULL;

			if (stored == NULL || !convert_into (stored, out))
			{
				if (e->has_default)
					g_value_copy (&e->def, out);
			}
		}
		break;
	case SLOT_COMP_TYPE:
		{
			LrgComponentDesc *desc = find_component (self);

			g_value_set_string (out, desc != NULL ? lrg_component_desc_get_type_name (desc) : NULL);
		}
		break;
	case SLOT_COMP_PROP:
	case SLOT_COMP_RAW:
		{
			LrgComponentDesc *desc = find_component (self);
			const GValue     *stored = (desc != NULL) ? lrg_component_desc_get_value (desc, e->key) : NULL;

			if (stored == NULL || !convert_into (stored, out))
				g_param_value_set_default (e->pspec, out);
		}
		break;
	case SLOT_SCRIPT_LANGUAGE:
		{
			LrgScriptBinding *s = script_at (self, e->index);

			g_value_set_enum (out, s != NULL ? (gint) lrg_script_binding_get_language (s) : 0);
		}
		break;
	case SLOT_SCRIPT_PATH:
		{
			LrgScriptBinding *s = script_at (self, e->index);

			g_value_set_string (out, s != NULL ? lrg_script_binding_get_script (s) : NULL);
		}
		break;
	case SLOT_SCRIPT_ENABLED:
		{
			LrgScriptBinding *s = script_at (self, e->index);

			g_value_set_boolean (out, s != NULL ? lrg_script_binding_get_enabled (s) : TRUE);
		}
		break;
	default:
		break;
	}
}

/* ==========================================================================
 * Writing values (in place)
 * ========================================================================== */

/* The visual's material, created when it has none */
static LrgMaterial3D *
ensure_material (LrgNodeVisual *visual)
{
	LrgMaterial3D *m = lrg_node_visual_get_material (visual);

	if (m == NULL)
	{
		g_autoptr(LrgMaterial3D) fresh = lrg_material3d_new ();

		lrg_node_visual_set_material (visual, fresh);
		m = lrg_node_visual_get_material (visual);
	}
	return m;
}

/* Changes the visual kind the way the 0.8.0 inspector did: NONE drops the
 * visual, other kinds keep the existing payload (params, asset) and a
 * fresh primitive gets a light grey material. */
static void
apply_kind (LrgEditorNodeSource *self,
            LrgNodeVisualKind    kind)
{
	LrgNodeVisual            *visual = node_visual (self);
	g_autoptr(LrgNodeVisual)  v = NULL;

	if (kind == LRG_NODE_VISUAL_NONE)
	{
		lrg_node_set_visual (self->node, NULL);
		return;
	}
	if (visual != NULL && lrg_node_visual_get_kind (visual) == kind)
		return;

	v = (visual != NULL) ? lrg_node_visual_copy (visual) : lrg_node_visual_new (kind);
	lrg_node_visual_set_kind (v, kind);
	if (kind == LRG_NODE_VISUAL_PRIMITIVE && lrg_node_visual_get_material (v) == NULL)
	{
		g_autoptr(LrgMaterial3D) m = lrg_material3d_new_with_color (0.75f, 0.75f, 0.78f, 1.0f);

		lrg_node_visual_set_material (v, m);
	}
	lrg_node_set_visual (self->node, v);
}

/*
 * apply_entry:
 *
 * Writes @value (already of the entry's type) into the node, in place.
 * Returns FALSE when the value has nowhere to go (no visual, the
 * component or script is gone).
 */
static gboolean
apply_entry (LrgEditorNodeSource *self,
             Entry               *e,
             const GValue        *value)
{
	LrgNodeVisual *visual = node_visual (self);

	switch (e->slot)
	{
	case SLOT_NAME:
		lrg_node_set_name (self->node, g_value_get_string (value));
		return TRUE;
	case SLOT_VISIBLE:
		lrg_node_set_visible (self->node, g_value_get_boolean (value));
		return TRUE;
	case SLOT_LOCKED:
		lrg_node_set_locked (self->node, g_value_get_boolean (value));
		return TRUE;
	case SLOT_IS_2D:
		lrg_node_set_is_2d (self->node, g_value_get_boolean (value));
		return TRUE;
	case SLOT_LOCATION:
	case SLOT_ROTATION:
	case SLOT_SCALE:
		{
			const GrlVector3 *v = g_value_get_boxed (value);

			if (v == NULL)
				return FALSE;
			if (e->slot == SLOT_LOCATION)
				lrg_node_set_location_xyz (self->node, v->x, v->y, v->z);
			else if (e->slot == SLOT_ROTATION)
				lrg_node_set_rotation_xyz (self->node, (gfloat) (v->x * DEG2RAD),
				                           (gfloat) (v->y * DEG2RAD), (gfloat) (v->z * DEG2RAD));
			else
				lrg_node_set_scale_xyz (self->node, v->x, v->y, v->z);
		}
		return TRUE;
	case SLOT_KIND:
		apply_kind (self, (LrgNodeVisualKind) g_value_get_enum (value));
		return TRUE;
	case SLOT_PRIMITIVE:
		if (visual == NULL)
			return FALSE;
		lrg_node_visual_set_primitive (visual, (LrgPrimitiveType) g_value_get_enum (value));
		return TRUE;
	case SLOT_ASSET:
		{
			const gchar *s = g_value_get_string (value);

			if (visual == NULL)
				return FALSE;
			lrg_node_visual_set_asset (visual, (s != NULL && *s != '\0') ? s : NULL);
		}
		return TRUE;
	case SLOT_COLOR:
		{
			const GrlColor *c = g_value_get_boxed (value);

			if (visual == NULL || c == NULL)
				return FALSE;
			lrg_material3d_set_color (ensure_material (visual), (gfloat) c->r / 255.0f,
			                          (gfloat) c->g / 255.0f, (gfloat) c->b / 255.0f,
			                          (gfloat) c->a / 255.0f);
		}
		return TRUE;
	case SLOT_ROUGHNESS:
		if (visual == NULL)
			return FALSE;
		lrg_material3d_set_roughness (ensure_material (visual), (gfloat) g_value_get_double (value));
		return TRUE;
	case SLOT_METALLIC:
		if (visual == NULL)
			return FALSE;
		lrg_material3d_set_metallic (ensure_material (visual), (gfloat) g_value_get_double (value));
		return TRUE;
	case SLOT_EMISSION:
		if (visual == NULL)
			return FALSE;
		lrg_material3d_set_emission_strength (ensure_material (visual), (gfloat) g_value_get_double (value));
		return TRUE;
	case SLOT_PARAM:
		if (visual == NULL)
			return FALSE;
		lrg_node_visual_set_param (visual, e->key, value);
		return TRUE;
	case SLOT_COMP_PROP:
	case SLOT_COMP_RAW:
		{
			LrgComponentDesc *desc = find_component (self);

			if (desc == NULL)
				return FALSE;
			lrg_component_desc_set_value (desc, e->key, value);
		}
		return TRUE;
	case SLOT_SCRIPT_LANGUAGE:
	case SLOT_SCRIPT_PATH:
	case SLOT_SCRIPT_ENABLED:
		{
			LrgScriptBinding *s = script_at (self, e->index);

			if (s == NULL)
				return FALSE;
			if (e->slot == SLOT_SCRIPT_LANGUAGE)
				lrg_script_binding_set_language (s, (LrgScriptLanguage) g_value_get_enum (value));
			else if (e->slot == SLOT_SCRIPT_PATH)
				lrg_script_binding_set_script (s, g_value_get_string (value));
			else
				lrg_script_binding_set_enabled (s, g_value_get_boolean (value));
		}
		return TRUE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * Schema building
 * ========================================================================== */

typedef struct
{
	LrgEditorNodeSource *self;
	GPtrArray           *entries;
	GHashTable          *taken;      /* names in use */
	GString             *signature;
} Builder;

/*
 * unique_name:
 *
 * A valid, unused GParamSpec name for @raw: characters outside
 * [A-Za-z0-9] (also "_", which GParamSpec canonicalises) become '-', a leading non-letter gets "p-", and clashes
 * get "-2", "-3", ...
 */
static gchar *
unique_name (Builder     *b,
             const gchar *prefix,
             const gchar *raw)
{
	g_autoptr(GString) s = g_string_new (prefix);
	g_autofree gchar  *base = NULL;
	gchar             *p;
	guint              n = 2;
	gchar             *candidate;

	g_string_append (s, raw != NULL ? raw : "");
	for (p = s->str; *p != '\0'; p++)
		if (!g_ascii_isalnum (*p))
			*p = '-';
	if (s->len == 0 || !g_ascii_isalpha (s->str[0]))
		g_string_prepend (s, "p-");

	base = g_string_free (g_steal_pointer (&s), FALSE);
	candidate = g_strdup (base);
	while (g_hash_table_contains (b->taken, candidate) || !g_param_spec_is_valid_name (candidate))
	{
		g_free (candidate);
		candidate = g_strdup_printf ("%s-%u", base, n++);
	}
	return candidate;
}

/*
 * add_entry:
 *
 * Appends an entry for @pspec (floating or not; a reference is kept).
 * @label, @group, @hint and @tooltip override what LrgPropertyInfo
 * derives from the pspec when not NULL.
 */
static Entry *
add_entry (Builder     *b,
           Slot         slot,
           GParamSpec  *pspec,
           const gchar *label,
           const gchar *group,
           const gchar *hint,
           const gchar *tooltip)
{
	Entry *e = g_new0 (Entry, 1);

	e->slot = slot;
	e->pspec = g_param_spec_ref_sink (pspec);
	e->info = lrg_property_info_new (e->pspec);
	if (label != NULL)
		lrg_property_info_set_label (e->info, label);
	if (group != NULL)
		lrg_property_info_set_group (e->info, group);
	if (hint != NULL)
		lrg_property_info_set_hint (e->info, hint);
	if (tooltip != NULL)
		lrg_property_info_set_tooltip (e->info, tooltip);

	g_hash_table_add (b->taken, g_strdup (e->pspec->name));
	g_ptr_array_add (b->entries, e);
	return e;
}

/* Uses the pspec's own default as the entry default */
static void
default_from_pspec (Entry *e)
{
	g_value_init (&e->def, e->pspec->value_type);
	g_param_value_set_default (e->pspec, &e->def);
	e->has_default = TRUE;
}

static void
default_vec3 (Entry  *e,
              gfloat  v)
{
	g_value_init (&e->def, GRL_TYPE_VECTOR3);
	g_value_take_boxed (&e->def, grl_vector3_new (v, v, v));
	e->has_default = TRUE;
}

static void
default_double (Entry   *e,
                gdouble  v)
{
	g_auto(GValue) d = G_VALUE_INIT;

	g_value_init (&d, G_TYPE_DOUBLE);
	g_value_set_double (&d, v);
	g_value_init (&e->def, e->pspec->value_type);
	if (convert_into (&d, &e->def))
		e->has_default = TRUE;
	else
		g_value_unset (&e->def);
}

#define RW (G_PARAM_READWRITE)
#define RO (G_PARAM_READABLE)

static void
build_header (Builder *b)
{
	Entry *e;

	add_entry (b, SLOT_NAME, g_param_spec_string ("name", NULL, NULL, NULL, RW),
	           "Name", NULL, NULL, "The node's name");
	e = add_entry (b, SLOT_VISIBLE, g_param_spec_boolean ("visible", NULL, NULL, TRUE, RW),
	               "Visible", NULL, NULL, "Drawn in the viewport and the game");
	default_from_pspec (e);
	e = add_entry (b, SLOT_LOCKED, g_param_spec_boolean ("locked", NULL, NULL, FALSE, RW),
	               "Locked", NULL, NULL, "Locked nodes cannot be picked in the viewport");
	default_from_pspec (e);
	e = add_entry (b, SLOT_IS_2D, g_param_spec_boolean ("is-2d", NULL, NULL, FALSE, RW),
	               "2D", NULL, NULL, "The node lives in the 2D scene");
	default_from_pspec (e);
	add_entry (b, SLOT_PATH, g_param_spec_string ("path", NULL, NULL, NULL, RO),
	           "Path", NULL, NULL, "Path from the level root");
	add_entry (b, SLOT_GUID, g_param_spec_string ("guid", NULL, NULL, NULL, RO),
	           "GUID", NULL, NULL, "Stable node identity");
}

static void
build_transform (Builder *b)
{
	Entry *e;

	e = add_entry (b, SLOT_LOCATION, g_param_spec_boxed ("location", NULL, NULL, GRL_TYPE_VECTOR3, RW),
	               "Location", NULL, "vector3", "Local position");
	default_vec3 (e, 0.0f);
	e = add_entry (b, SLOT_ROTATION, g_param_spec_boxed ("rotation", NULL, NULL, GRL_TYPE_VECTOR3, RW),
	               "Rotation", NULL, "vector3", "Local Euler rotation, in degrees");
	default_vec3 (e, 0.0f);
	e = add_entry (b, SLOT_SCALE, g_param_spec_boxed ("scale", NULL, NULL, GRL_TYPE_VECTOR3, RW),
	               "Scale", NULL, "vector3:uniform", "Local scale");
	default_vec3 (e, 1.0f);
	add_entry (b, SLOT_WORLD, g_param_spec_boxed ("world", NULL, NULL, GRL_TYPE_VECTOR3, RO),
	           "World", NULL, "vector3", "World-space position (read-only)");
}

static void
build_visual (Builder *b)
{
	LrgEditorNodeSource *self = b->self;
	LrgNodeVisual       *visual = node_visual (self);
	LrgNodeVisualKind    kind = (visual != NULL) ? lrg_node_visual_get_kind (visual) : LRG_NODE_VISUAL_NONE;
	g_autoptr(GPtrArray) keys = NULL;
	GList               *names;
	GList               *l;
	Entry               *e;
	guint                i;

	e = add_entry (b, SLOT_KIND,
	               g_param_spec_enum ("kind", NULL, NULL, LRG_TYPE_NODE_VISUAL_KIND, LRG_NODE_VISUAL_NONE, RW),
	               "Kind", NULL, NULL, "What the node shows; none makes it a group");
	default_from_pspec (e);
	if (visual == NULL)
		return;

	if (kind == LRG_NODE_VISUAL_PRIMITIVE)
		add_entry (b, SLOT_PRIMITIVE,
		           g_param_spec_enum ("primitive", NULL, NULL, LRG_TYPE_PRIMITIVE_TYPE, LRG_PRIMITIVE_CUBE, RW),
		           "Primitive", NULL, NULL, "The primitive shape");
	if (kind_has_asset (kind))
		add_entry (b, SLOT_ASSET, g_param_spec_string ("asset", NULL, NULL, NULL, RW),
		           "Asset", NULL, asset_hint_for_kind (kind), "Asset path relative to the project, or its guid");

	/* Material of drawable kinds; lights only use its colour */
	if (kind_has_material (kind))
	{
		e = add_entry (b, SLOT_COLOR, g_param_spec_boxed ("color", NULL, NULL, GRL_TYPE_COLOR, RW),
		               kind == LRG_NODE_VISUAL_LIGHT ? "Light color" : "Color", "Material", "color",
		               kind == LRG_NODE_VISUAL_LIGHT ? "Colour of the light" : "Base colour");
		g_value_init (&e->def, GRL_TYPE_COLOR);
		g_value_take_boxed (&e->def, grl_color_new (255, 255, 255, 255));
		e->has_default = TRUE;
		if (kind != LRG_NODE_VISUAL_LIGHT)
		{
			e = add_entry (b, SLOT_ROUGHNESS, g_param_spec_double ("roughness", NULL, NULL, 0.0, 1.0, 0.5, RW),
			               "Roughness", "Material", "range:0,1,0.01", NULL);
			default_from_pspec (e);
			e = add_entry (b, SLOT_METALLIC, g_param_spec_double ("metallic", NULL, NULL, 0.0, 1.0, 0.0, RW),
			               "Metallic", "Material", "range:0,1,0.01", NULL);
			default_from_pspec (e);
			e = add_entry (b, SLOT_EMISSION, g_param_spec_double ("emission", NULL, NULL, 0.0, 100.0, 0.0, RW),
			               "Emission", "Material", "range:0,100,0.01", "Emission strength");
			default_from_pspec (e);
		}
	}

	/* The parameter bag: stored keys plus the kind's usual ones, sorted;
	 * "prefab:" keys are internal markers and stay hidden */
	keys = g_ptr_array_new_with_free_func (g_free);
	names = lrg_node_visual_get_param_names (visual);
	for (l = names; l != NULL; l = l->next)
		if (!g_str_has_prefix (l->data, "prefab:"))
			g_ptr_array_add (keys, g_strdup (l->data));
	g_list_free (names);
	for (i = 0; i < G_N_ELEMENTS (suggested_params); i++)
		if (suggested_params[i].kind == kind &&
		    !g_ptr_array_find_with_equal_func (keys, suggested_params[i].param, g_str_equal, NULL))
			g_ptr_array_add (keys, g_strdup (suggested_params[i].param));
	g_ptr_array_sort_values (keys, (GCompareFunc) g_strcmp0);

	for (i = 0; i < keys->len; i++)
	{
		const gchar      *key = g_ptr_array_index (keys, i);
		const GValue     *stored = lrg_node_visual_get_param (visual, key);
		gint              sug = find_suggested (kind, key);
		GType             type = (stored != NULL) ? G_VALUE_TYPE (stored) : G_TYPE_DOUBLE;
		g_autofree gchar *name = unique_name (b, "param-", key);
		GParamSpec       *pspec = make_pspec_for_type (name, type, FALSE);

		if (pspec == NULL)
			continue;
		e = add_entry (b, SLOT_PARAM, pspec, key, "Parameters",
		               sug >= 0 ? suggested_params[sug].hint : NULL,
		               sug >= 0 ? suggested_params[sug].tooltip : NULL);
		e->key = g_strdup (key);
		if (sug >= 0)
			default_double (e, suggested_params[sug].value);
		else if (G_TYPE_FUNDAMENTAL (type) != G_TYPE_STRING && G_TYPE_FUNDAMENTAL (type) != G_TYPE_BOXED)
			default_from_pspec (e);
	}
}

/* Whether a component class property is edited in the inspector (the
 * 0.8.0 inspector's filter) */
static gboolean
component_pspec_shown (GParamSpec *pspec)
{
	return (pspec->flags & G_PARAM_WRITABLE) && !(pspec->flags & G_PARAM_CONSTRUCT_ONLY) &&
	       !G_TYPE_IS_OBJECT (pspec->value_type) && pspec->owner_type != G_TYPE_OBJECT;
}

static void
build_component (Builder *b)
{
	LrgEditorNodeSource *self = b->self;
	LrgComponentDesc    *desc = find_component (self);
	g_autoptr(GHashTable) shown = NULL;
	GType                type;
	GList               *keys;
	GList               *l;
	Entry               *e;

	if (desc == NULL)
		return;

	type = lookup_component_type (lrg_component_desc_get_type_name (desc));
	add_entry (b, SLOT_COMP_TYPE, g_param_spec_string ("component-type", NULL, NULL, NULL, RO),
	           "Type", NULL, NULL,
	           (type != G_TYPE_INVALID) ? "The component type" : "Type not registered in this editor; values are raw");

	/* Reflected properties of the registered type */
	shown = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	if (type != G_TYPE_INVALID && G_TYPE_IS_OBJECT (type))
	{
		GObjectClass  *klass = g_type_class_ref (type);
		guint          n = 0;
		guint          i;
		GParamSpec   **pspecs = g_object_class_list_properties (klass, &n);

		for (i = 0; i < n; i++)
		{
			GParamSpec *pspec = pspecs[i];

			if (!component_pspec_shown (pspec) || g_hash_table_contains (b->taken, pspec->name))
				continue;
			e = add_entry (b, SLOT_COMP_PROP, pspec, NULL, NULL, NULL, NULL);
			e->key = g_strdup (pspec->name);
			default_from_pspec (e);
			g_hash_table_add (shown, g_strdup (pspec->name));
		}
		g_free (pspecs);
		g_type_class_unref (klass);
	}

	/* Stored keys the type does not declare: raw typed values */
	keys = lrg_component_desc_get_sorted_keys (desc);
	for (l = keys; l != NULL; l = l->next)
	{
		const GValue     *cur = lrg_component_desc_get_value (desc, l->data);
		g_autofree gchar *name = NULL;
		GParamSpec       *pspec;

		if (cur == NULL || g_hash_table_contains (shown, l->data))
			continue;
		name = unique_name (b, "", l->data);
		pspec = make_pspec_for_type (name, G_VALUE_TYPE (cur), FALSE);
		if (pspec == NULL)
			continue;
		e = add_entry (b, SLOT_COMP_RAW, pspec, l->data, NULL, NULL, "Stored value");
		e->key = g_strdup (l->data);
	}
	g_list_free (keys);
}

static void
build_scripts (Builder *b)
{
	GPtrArray *scripts = lrg_node_get_scripts (b->self->node);
	guint      i;

	for (i = 0; scripts != NULL && i < scripts->len; i++)
	{
		LrgScriptBinding *s = g_ptr_array_index (scripts, i);
		const gchar      *path = lrg_script_binding_get_script (s);
		g_autofree gchar *group = (path != NULL && *path != '\0')
		                          ? g_strdup_printf ("%u: %s", i + 1, path)
		                          : g_strdup_printf ("Script %u", i + 1);
		g_autofree gchar *n_lang = g_strdup_printf ("script-%u-language", i);
		g_autofree gchar *n_path = g_strdup_printf ("script-%u-path", i);
		g_autofree gchar *n_enabled = g_strdup_printf ("script-%u-enabled", i);
		Entry            *e;

		e = add_entry (b, SLOT_SCRIPT_ENABLED, g_param_spec_boolean (n_enabled, NULL, NULL, TRUE, RW),
		               "Enabled", group, NULL, "Run this script");
		e->index = i;
		default_from_pspec (e);
		e = add_entry (b, SLOT_SCRIPT_LANGUAGE,
		               g_param_spec_enum (n_lang, NULL, NULL, LRG_TYPE_SCRIPT_LANGUAGE, LRG_SCRIPT_LANGUAGE_NONE, RW),
		               "Language", group, NULL, "Scripting backend");
		e->index = i;
		e = add_entry (b, SLOT_SCRIPT_PATH, g_param_spec_string (n_path, NULL, NULL, NULL, RW),
		               "Path", group, "asset:script", "Script file relative to the project");
		e->index = i;
	}
}

/*
 * build_schema:
 *
 * Builds the entries of the section and a signature naming every
 * entry's name, type, label, group, hint and read-only flag, so a
 * schema change (not a value change) can be detected by comparing
 * signatures.
 */
static GPtrArray *
build_schema (LrgEditorNodeSource  *self,
              gchar               **signature)
{
	Builder b;
	guint   i;

	b.self = self;
	b.entries = g_ptr_array_new_with_free_func (entry_free);
	b.taken = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	b.signature = g_string_new (NULL);

	switch (self->section)
	{
	case LRG_EDITOR_NODE_SECTION_HEADER:
		build_header (&b);
		break;
	case LRG_EDITOR_NODE_SECTION_TRANSFORM:
		build_transform (&b);
		break;
	case LRG_EDITOR_NODE_SECTION_VISUAL:
		build_visual (&b);
		break;
	case LRG_EDITOR_NODE_SECTION_COMPONENT:
		build_component (&b);
		break;
	case LRG_EDITOR_NODE_SECTION_SCRIPTS:
		build_scripts (&b);
		break;
	default:
		break;
	}

	for (i = 0; i < b.entries->len; i++)
	{
		Entry *e = g_ptr_array_index (b.entries, i);

		g_string_append_printf (b.signature, "%s|%s|%s|%s|%s|%d;", entry_name (e),
		                        g_type_name (e->pspec->value_type),
		                        lrg_property_info_get_label (e->info),
		                        lrg_property_info_get_group (e->info) != NULL ? lrg_property_info_get_group (e->info) : "",
		                        lrg_property_info_get_hint (e->info) != NULL ? lrg_property_info_get_hint (e->info) : "",
		                        lrg_property_info_get_read_only (e->info));
	}

	g_hash_table_unref (b.taken);
	*signature = g_string_free (b.signature, FALSE);
	return b.entries;
}

/* ==========================================================================
 * States
 * ========================================================================== */

/* The prefab override path of an entry (see docs/editor/prefabs.org) */
static gchar *
override_path (LrgEditorNodeSource *self,
               Entry               *e)
{
	LrgScriptBinding *s;

	switch (e->slot)
	{
	case SLOT_NAME:      return g_strdup ("name");
	case SLOT_VISIBLE:   return g_strdup ("visible");
	case SLOT_IS_2D:     return g_strdup ("is_2d");
	case SLOT_LOCATION:  return g_strdup ("transform/location");
	case SLOT_ROTATION:  return g_strdup ("transform/rotation");
	case SLOT_SCALE:     return g_strdup ("transform/scale");
	case SLOT_KIND:      return g_strdup ("visual/kind");
	case SLOT_PRIMITIVE: return g_strdup ("visual/primitive");
	case SLOT_ASSET:     return g_strdup ("visual/asset");
	case SLOT_COLOR:     return g_strdup ("visual/material/color");
	case SLOT_ROUGHNESS: return g_strdup ("visual/material/roughness");
	case SLOT_METALLIC:  return g_strdup ("visual/material/metallic");
	case SLOT_EMISSION:  return g_strdup ("visual/material/emission_strength");
	case SLOT_PARAM:     return g_strconcat ("visual/params/", e->key, NULL);
	case SLOT_COMP_TYPE: return g_strconcat ("components/+", self->component_type, NULL);
	case SLOT_COMP_PROP:
	case SLOT_COMP_RAW:  return g_strconcat ("components/", self->component_type, "/", e->key, NULL);
	case SLOT_SCRIPT_LANGUAGE:
	case SLOT_SCRIPT_ENABLED:
	case SLOT_SCRIPT_PATH:
		s = script_at (self, e->index);
		if (s == NULL || lrg_script_binding_get_script (s) == NULL)
			return NULL;
		if (e->slot == SLOT_SCRIPT_PATH)
			return g_strconcat ("scripts/+", lrg_script_binding_get_script (s), NULL);
		return g_strconcat ("scripts/", lrg_script_binding_get_script (s),
		                    e->slot == SLOT_SCRIPT_ENABLED ? "/enabled" : "/language", NULL);
	default:
		return NULL;
	}
}

/* Whether a timeline track of the level animates one of the entry's
 * channels on this node */
static gboolean
is_animated (LrgEditorNodeSource *self,
             Entry               *e)
{
	static const gchar *loc[] = { "location.x", "location.y", "location.z", NULL };
	static const gchar *rot[] = { "rotation.x", "rotation.y", "rotation.z", NULL };
	static const gchar *scl[] = { "scale.x", "scale.y", "scale.z", NULL };
	static const gchar *col[] = { "color.r", "color.g", "color.b", "color.a", NULL };
	static const gchar *rough[] = { "roughness", NULL };
	static const gchar *metal[] = { "metallic", NULL };
	const gchar        *param[2] = { NULL, NULL };
	g_autofree gchar   *param_channel = NULL;
	const gchar *const *channels;
	LrgLevel           *level = lrg_editor_get_level (self->editor);
	const gchar        *guid = lrg_node_get_guid (self->node);
	guint               t;
	guint               c;

	switch (e->slot)
	{
	case SLOT_LOCATION:  channels = loc; break;
	case SLOT_ROTATION:  channels = rot; break;
	case SLOT_SCALE:     channels = scl; break;
	case SLOT_COLOR:     channels = col; break;
	case SLOT_ROUGHNESS: channels = rough; break;
	case SLOT_METALLIC:  channels = metal; break;
	case SLOT_PARAM:
		param_channel = g_strconcat ("param.", e->key, NULL);
		param[0] = param_channel;
		channels = param;
		break;
	default:
		return FALSE;
	}

	if (level == NULL || guid == NULL)
		return FALSE;
	for (t = 0; t < lrg_level_get_n_timelines (level); t++)
	{
		LrgTimeline *timeline = lrg_level_get_timeline (level, t);

		for (c = 0; channels[c] != NULL; c++)
			if (lrg_timeline_find_track (timeline, guid, channels[c]) >= 0)
				return TRUE;
	}
	return FALSE;
}

/*
 * asset_missing:
 *
 * TRUE when @ref looks like a file path (has a '/' or a '.') and no file
 * exists at it: absolute, or relative to the asset root, or (without a
 * root) to the level file's directory or the current directory. Guids
 * and empty values are not missing.
 */
static gboolean
asset_missing (LrgEditorNodeSource *self,
               const gchar         *ref)
{
	const gchar *level_path;

	if (ref == NULL || *ref == '\0' || (strchr (ref, '/') == NULL && strchr (ref, '.') == NULL))
		return FALSE;
	if (g_path_is_absolute (ref))
		return !g_file_test (ref, G_FILE_TEST_EXISTS);

	if (self->asset_root != NULL)
	{
		g_autofree gchar *full = g_build_filename (self->asset_root, ref, NULL);

		return !g_file_test (full, G_FILE_TEST_EXISTS);
	}

	level_path = lrg_editor_get_path (self->editor);
	if (level_path != NULL)
	{
		g_autofree gchar *dir = g_path_get_dirname (level_path);
		g_autofree gchar *full = g_build_filename (dir, ref, NULL);

		if (g_file_test (full, G_FILE_TEST_EXISTS))
			return FALSE;
	}
	return !g_file_test (ref, G_FILE_TEST_EXISTS);
}

/* The state flags of @e, given its current @value */
static LrgPropertyState
entry_state (LrgEditorNodeSource *self,
             Entry               *e,
             const GValue        *value)
{
	LrgPropertyState  state = LRG_PROPERTY_STATE_NONE;
	g_autofree gchar *path = NULL;

	if (e->has_default && lrg_property_values_equal (value, &e->def))
		state |= LRG_PROPERTY_STATE_DEFAULT;

	path = override_path (self, e);
	if (path != NULL && lrg_prefab_is_overridden (self->node, path))
		state |= LRG_PROPERTY_STATE_OVERRIDDEN;

	if (is_animated (self, e))
		state |= LRG_PROPERTY_STATE_ANIMATED;

	if ((e->slot == SLOT_ASSET || e->slot == SLOT_SCRIPT_PATH) && asset_missing (self, g_value_get_string (value)))
		state |= LRG_PROPERTY_STATE_INVALID;
	if (e->slot == SLOT_COMP_TYPE && lookup_component_type (g_value_get_string (value)) == G_TYPE_INVALID)
		state |= LRG_PROPERTY_STATE_INVALID;

	return state;
}

/* ==========================================================================
 * Schema cache and change emission
 * ========================================================================== */

/* Records the current value and state of every entry as "seen" */
static void
snapshot_values (LrgEditorNodeSource *self,
                 GPtrArray           *entries)
{
	guint i;

	for (i = 0; i < entries->len; i++)
	{
		Entry *e = g_ptr_array_index (entries, i);

		if (G_IS_VALUE (&e->last))
			g_value_unset (&e->last);
		read_entry (self, e, &e->last);
		e->last_state = entry_state (self, e, &e->last);
	}
}

static void
install_schema (LrgEditorNodeSource *self,
                GPtrArray           *entries,
                gchar               *signature)
{
	guint i;

	g_clear_pointer (&self->entries, g_ptr_array_unref);
	g_free (self->signature);
	self->entries = entries;
	self->signature = signature;

	g_hash_table_remove_all (self->by_name);
	for (i = 0; i < entries->len; i++)
	{
		Entry *e = g_ptr_array_index (entries, i);

		g_hash_table_insert (self->by_name, (gpointer) entry_name (e), e);
	}
	snapshot_values (self, entries);
}

static void
ensure_schema (LrgEditorNodeSource *self)
{
	gchar     *signature = NULL;
	GPtrArray *entries;

	if (self->entries != NULL)
		return;
	entries = build_schema (self, &signature);
	install_schema (self, entries, signature);
}

static Entry *
lookup_entry (LrgEditorNodeSource *self,
              const gchar         *name)
{
	ensure_schema (self);
	return g_hash_table_lookup (self->by_name, name);
}

/*
 * refresh:
 *
 * Coalesced change emission. Re-reads the node: when the schema changed
 * (different signature) the new schema is installed and
 * property-changed is emitted once with a NULL name; otherwise it is
 * emitted once per property whose value or state differs from what was
 * last seen. Repeated notifications without a real change emit nothing.
 */
static void
refresh (LrgEditorNodeSource *self)
{
	gboolean fixed;
	guint    i;

	if (self->refreshing || self->entries == NULL)
		return;
	self->refreshing = TRUE;

	/* Visual, component and scripts schemas follow the node */
	fixed = self->section == LRG_EDITOR_NODE_SECTION_HEADER ||
	        self->section == LRG_EDITOR_NODE_SECTION_TRANSFORM;
	if (!fixed)
	{
		gchar     *signature = NULL;
		GPtrArray *entries = build_schema (self, &signature);

		if (g_strcmp0 (signature, self->signature) != 0)
		{
			install_schema (self, entries, signature);
			self->refreshing = FALSE;
			lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self), NULL);
			return;
		}
		g_ptr_array_unref (entries);
		g_free (signature);
	}

	/* Same schema: compare values and states; collect names first so
	 * handlers may re-enter the source safely */
	{
		g_autoptr(GPtrArray) changed = g_ptr_array_new_with_free_func (g_free);

		for (i = 0; i < self->entries->len; i++)
		{
			Entry            *e = g_ptr_array_index (self->entries, i);
			g_auto(GValue)    now = G_VALUE_INIT;
			LrgPropertyState  state;

			read_entry (self, e, &now);
			state = entry_state (self, e, &now);
			if (lrg_property_values_equal (&now, &e->last) && state == e->last_state)
				continue;
			g_value_unset (&e->last);
			g_value_init (&e->last, G_VALUE_TYPE (&now));
			g_value_copy (&now, &e->last);
			e->last_state = state;
			g_ptr_array_add (changed, g_strdup (entry_name (e)));
		}
		self->refreshing = FALSE;
		for (i = 0; i < changed->len; i++)
			lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self), g_ptr_array_index (changed, i));
	}
}

/* Tells views that a preview changed the node (no undo command was
 * executed, so the editor would not say so itself) */
static void
notify_preview (LrgEditorNodeSource *self)
{
	LrgLevel *level = lrg_editor_get_level (self->editor);

	if (batch_depth > 0)
		return;
	if (level != NULL)
		lrg_level_notify_node_changed (level, self->node);
	g_signal_emit_by_name (self->editor, "changed");
}

/* ==========================================================================
 * Signal handlers
 * ========================================================================== */

static void
on_editor_changed (LrgEditor *editor,
                   gpointer   user_data)
{
	if (batch_depth == 0)
		refresh (LRG_EDITOR_NODE_SOURCE (user_data));
}

static void
on_level_node_changed (LrgLevel *level,
                       LrgNode  *node,
                       gpointer  user_data)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (user_data);

	if (node == self->node && batch_depth == 0)
		refresh (self);
}

static void
watch_level (LrgEditorNodeSource *self,
             LrgLevel            *level)
{
	if (self->level != NULL)
	{
		g_clear_signal_handler (&self->node_changed_id, self->level);
		g_clear_object (&self->level);
	}
	if (level != NULL)
	{
		self->level = g_object_ref (level);
		self->node_changed_id = g_signal_connect (level, "node-changed",
		                                          G_CALLBACK (on_level_node_changed), self);
	}
}

static void
on_level_replaced (LrgEditor *editor,
                   LrgLevel  *level,
                   gpointer   user_data)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (user_data);

	watch_level (self, level);
	refresh (self);
}

/* ==========================================================================
 * Writes
 * ========================================================================== */

/*
 * source_write:
 *
 * The one write path. @value is converted to the property's type. Inside
 * a gesture it is a preview (in place, @notify tells views); outside one
 * it is snapshot + in-place write + one undoable NODE_STATE command.
 */
static gboolean
source_write (LrgEditorNodeSource *self,
              Entry               *e,
              const GValue        *value,
              gboolean             notify)
{
	g_auto(GValue)     typed = G_VALUE_INIT;
	g_autoptr(LrgNode) before = NULL;
	g_autofree gchar  *label = NULL;

	if (lrg_property_info_get_read_only (e->info))
		return FALSE;

	g_value_init (&typed, e->pspec->value_type);
	if (!convert_into (value, &typed))
		return FALSE;
	g_param_value_validate (e->pspec, &typed);

	if (self->gesture_depth > 0)
	{
		if (!apply_entry (self, e, &typed))
			return FALSE;
		if (notify)
			notify_preview (self);
		return TRUE;
	}

	before = lrg_editor_snapshot_node (self->node);
	if (!apply_entry (self, e, &typed))
		return FALSE;
	label = g_strdup_printf ("Set %s", lrg_property_info_get_label (e->info));
	lrg_editor_commit_node_state (self->editor, self->node, before, label, FALSE);
	return TRUE;
}

/* Removes a component key (back to the pspec default), undoably or as a
 * preview like source_write() */
static gboolean
remove_component_key (LrgEditorNodeSource *self,
                      Entry               *e)
{
	LrgComponentDesc   *desc = find_component (self);
	g_autoptr(LrgNode)  before = NULL;
	g_autofree gchar   *label = NULL;

	if (desc == NULL || !lrg_component_desc_has (desc, e->key))
		return FALSE;
	if (self->gesture_depth > 0)
	{
		lrg_component_desc_remove (desc, e->key);
		notify_preview (self);
		return TRUE;
	}
	before = lrg_editor_snapshot_node (self->node);
	lrg_component_desc_remove (desc, e->key);
	label = g_strdup_printf ("Reset %s", lrg_property_info_get_label (e->info));
	lrg_editor_commit_node_state (self->editor, self->node, before, label, FALSE);
	return TRUE;
}

/* ==========================================================================
 * LrgPropertySource
 * ========================================================================== */

static GPtrArray *
node_source_list_properties (LrgPropertySource *source)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (source);
	GPtrArray           *out;
	guint                i;

	ensure_schema (self);
	out = g_ptr_array_new_full (self->entries->len, (GDestroyNotify) lrg_property_info_unref);
	for (i = 0; i < self->entries->len; i++)
		g_ptr_array_add (out, lrg_property_info_ref (((Entry *) g_ptr_array_index (self->entries, i))->info));
	return out;
}

static gboolean
node_source_get_value (LrgPropertySource *source,
                       const gchar       *name,
                       GValue            *value)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (source);
	Entry               *e = lookup_entry (self, name);

	if (e == NULL)
		return FALSE;
	read_entry (self, e, value);
	return TRUE;
}

static gboolean
node_source_set_value (LrgPropertySource *source,
                       const gchar       *name,
                       const GValue      *value)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (source);
	Entry               *e = lookup_entry (self, name);

	if (e == NULL)
		return FALSE;
	return source_write (self, e, value, TRUE);
}

static LrgPropertyState
node_source_get_state (LrgPropertySource *source,
                       const gchar       *name)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (source);
	Entry               *e = lookup_entry (self, name);
	g_auto(GValue)       value = G_VALUE_INIT;

	if (e == NULL)
		return LRG_PROPERTY_STATE_NONE;
	read_entry (self, e, &value);
	return entry_state (self, e, &value);
}

static gboolean
node_source_reset (LrgPropertySource *source,
                   const gchar       *name)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (source);
	Entry               *e = lookup_entry (self, name);

	if (e == NULL || lrg_property_info_get_read_only (e->info))
		return FALSE;

	/* Component properties go back to "not stored": the type's default */
	if (e->slot == SLOT_COMP_PROP)
		return remove_component_key (self, e);
	if (!e->has_default)
		return FALSE;
	return source_write (self, e, &e->def, TRUE);
}

static void
lrg_editor_node_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = node_source_list_properties;
	iface->get_value = node_source_get_value;
	iface->set_value = node_source_set_value;
	iface->get_state = node_source_get_state;
	iface->reset = node_source_reset;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_editor_node_source_constructed (GObject *object)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (object);

	G_OBJECT_CLASS (lrg_editor_node_source_parent_class)->constructed (object);

	g_return_if_fail (LRG_IS_EDITOR (self->editor));
	g_return_if_fail (LRG_IS_NODE (self->node));

	self->changed_id = g_signal_connect (self->editor, "changed",
	                                     G_CALLBACK (on_editor_changed), self);
	self->replaced_id = g_signal_connect (self->editor, "level-replaced",
	                                      G_CALLBACK (on_level_replaced), self);
	watch_level (self, lrg_editor_get_level (self->editor));
}

static void
lrg_editor_node_source_dispose (GObject *object)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (object);

	/* An open gesture keeps its edit, like the grid does */
	if (self->gesture_depth > 0 && self->editor != NULL && self->node != NULL)
	{
		self->gesture_depth = 1;
		lrg_editor_node_source_end_gesture (self, TRUE, NULL);
	}
	g_clear_object (&self->snapshot);

	watch_level (self, NULL);
	if (self->editor != NULL)
	{
		g_clear_signal_handler (&self->changed_id, self->editor);
		g_clear_signal_handler (&self->replaced_id, self->editor);
	}
	g_clear_object (&self->editor);
	g_clear_object (&self->node);
	g_clear_pointer (&self->entries, g_ptr_array_unref);
	if (self->by_name != NULL)
		g_hash_table_remove_all (self->by_name);

	G_OBJECT_CLASS (lrg_editor_node_source_parent_class)->dispose (object);
}

static void
lrg_editor_node_source_finalize (GObject *object)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (object);

	g_free (self->component_type);
	g_free (self->asset_root);
	g_free (self->signature);
	g_clear_pointer (&self->by_name, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_editor_node_source_parent_class)->finalize (object);
}

static void
lrg_editor_node_source_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (object);

	switch (prop_id)
	{
	case PROP_EDITOR:
		g_value_set_object (value, self->editor);
		break;
	case PROP_NODE:
		g_value_set_object (value, self->node);
		break;
	case PROP_SECTION:
		g_value_set_enum (value, self->section);
		break;
	case PROP_COMPONENT_TYPE:
		g_value_set_string (value, self->component_type);
		break;
	case PROP_ASSET_ROOT:
		g_value_set_string (value, self->asset_root);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_node_source_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgEditorNodeSource *self = LRG_EDITOR_NODE_SOURCE (object);

	switch (prop_id)
	{
	case PROP_EDITOR:
		self->editor = g_value_dup_object (value);
		break;
	case PROP_NODE:
		self->node = g_value_dup_object (value);
		break;
	case PROP_SECTION:
		self->section = (LrgEditorNodeSection) g_value_get_enum (value);
		break;
	case PROP_COMPONENT_TYPE:
		self->component_type = g_value_dup_string (value);
		break;
	case PROP_ASSET_ROOT:
		lrg_editor_node_source_set_asset_root (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_editor_node_source_class_init (LrgEditorNodeSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = lrg_editor_node_source_constructed;
	object_class->dispose = lrg_editor_node_source_dispose;
	object_class->finalize = lrg_editor_node_source_finalize;
	object_class->get_property = lrg_editor_node_source_get_property;
	object_class->set_property = lrg_editor_node_source_set_property;

	/**
	 * LrgEditorNodeSource:editor:
	 *
	 * The #LrgEditor whose undo stack records the edits.
	 */
	properties[PROP_EDITOR] =
		g_param_spec_object ("editor", NULL, NULL, LRG_TYPE_EDITOR,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorNodeSource:node:
	 *
	 * The #LrgNode the source exposes.
	 */
	properties[PROP_NODE] =
		g_param_spec_object ("node", NULL, NULL, LRG_TYPE_NODE,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorNodeSource:section:
	 *
	 * Which part of the node the source exposes.
	 */
	properties[PROP_SECTION] =
		g_param_spec_enum ("section", NULL, NULL, LRG_TYPE_EDITOR_NODE_SECTION,
		                   LRG_EDITOR_NODE_SECTION_HEADER,
		                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorNodeSource:component-type:
	 *
	 * The component type of a component section ("Type" or "Type#N").
	 */
	properties[PROP_COMPONENT_TYPE] =
		g_param_spec_string ("component-type", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorNodeSource:asset-root:
	 *
	 * The directory relative asset and script paths are checked against
	 * for the invalid state, or %NULL.
	 */
	properties[PROP_ASSET_ROOT] =
		g_param_spec_string ("asset-root", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_editor_node_source_init (LrgEditorNodeSource *self)
{
	self->by_name = g_hash_table_new (g_str_hash, g_str_equal);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgEditorNodeSource *
lrg_editor_node_source_new (LrgEditor            *editor,
                            LrgNode              *node,
                            LrgEditorNodeSection  section,
                            const gchar          *component_type)
{
	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);
	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	return g_object_new (LRG_TYPE_EDITOR_NODE_SOURCE,
	                     "editor", editor,
	                     "node", node,
	                     "section", section,
	                     "component-type", section == LRG_EDITOR_NODE_SECTION_COMPONENT ? component_type : NULL,
	                     NULL);
}

GPtrArray *
lrg_editor_node_source_new_for_nodes (LrgEditor            *editor,
                                      GPtrArray            *nodes,
                                      LrgEditorNodeSection  section,
                                      const gchar          *component_type)
{
	GPtrArray *out;
	guint      i;

	g_return_val_if_fail (LRG_IS_EDITOR (editor), NULL);
	g_return_val_if_fail (nodes != NULL, NULL);

	out = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < nodes->len; i++)
	{
		LrgEditorNodeSource *src = lrg_editor_node_source_new (editor, g_ptr_array_index (nodes, i),
		                                                       section, component_type);

		if (section == LRG_EDITOR_NODE_SECTION_COMPONENT && find_component (src) == NULL)
		{
			g_object_unref (src);
			continue;
		}
		g_ptr_array_add (out, src);
	}
	return out;
}

LrgEditor *
lrg_editor_node_source_get_editor (LrgEditorNodeSource *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), NULL);

	return self->editor;
}

LrgNode *
lrg_editor_node_source_get_node (LrgEditorNodeSource *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), NULL);

	return self->node;
}

LrgEditorNodeSection
lrg_editor_node_source_get_section (LrgEditorNodeSource *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), LRG_EDITOR_NODE_SECTION_HEADER);

	return self->section;
}

const gchar *
lrg_editor_node_source_get_component_type (LrgEditorNodeSource *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), NULL);

	return self->component_type;
}

const gchar *
lrg_editor_node_source_get_asset_root (LrgEditorNodeSource *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), NULL);

	return self->asset_root;
}

void
lrg_editor_node_source_set_asset_root (LrgEditorNodeSource *self,
                                       const gchar         *root)
{
	g_return_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self));

	if (!g_set_str (&self->asset_root, root))
		return;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_ROOT]);
	refresh (self);
}

gchar *
lrg_editor_node_source_get_override_path (LrgEditorNodeSource *self,
                                          const gchar         *name)
{
	Entry *e;

	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	e = lookup_entry (self, name);
	return (e != NULL) ? override_path (self, e) : NULL;
}

/*
 * covering_key:
 *
 * The override key of the owning instance that covers @path on the
 * source's node: the exact key, or the nearest parent path key.
 */
static gchar *
covering_key (LrgEditorNodeSource  *self,
              const gchar          *path,
              LrgNode             **out_owner)
{
	LrgNode          *owner = lrg_prefab_find_owner (self->node);
	LrgPrefabLink    *link;
	g_autofree gchar *candidate = NULL;
	gchar            *slash;

	*out_owner = owner;
	if (owner == NULL || (link = lrg_node_get_prefab_link (owner)) == NULL)
		return NULL;

	candidate = g_strdup (path);
	for (;;)
	{
		g_autofree gchar *key = lrg_prefab_make_override_key (owner, self->node, candidate);

		if (lrg_prefab_link_contains_override (link, key))
			return g_steal_pointer (&key);
		slash = strrchr (candidate, '/');
		if (slash == NULL)
			return NULL;
		*slash = '\0';
	}
}

gboolean
lrg_editor_node_source_revert_override (LrgEditorNodeSource  *self,
                                        const gchar          *name,
                                        GError              **error)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *key = NULL;
	LrgNode          *owner = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	path = lrg_editor_node_source_get_override_path (self, name);
	if (path != NULL)
		key = covering_key (self, path, &owner);
	if (key == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "Property “%s” is not an overridden prefab property", name);
		return FALSE;
	}
	return lrg_editor_prefab_revert (self->editor, owner, key, error);
}

gboolean
lrg_editor_node_source_apply_override (LrgEditorNodeSource  *self,
                                       const gchar          *name,
                                       GError              **error)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *key = NULL;
	LrgNode          *owner = NULL;
	const gchar      *keys[2] = { NULL, NULL };

	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	path = lrg_editor_node_source_get_override_path (self, name);
	if (path != NULL)
		key = covering_key (self, path, &owner);
	if (key == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "Property “%s” is not an overridden prefab property", name);
		return FALSE;
	}
	keys[0] = key;
	return lrg_editor_prefab_apply (self->editor, owner, keys, error);
}

void
lrg_editor_node_source_begin_gesture (LrgEditorNodeSource *self)
{
	g_return_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self));

	if (self->gesture_depth++ == 0)
	{
		g_clear_object (&self->snapshot);
		self->snapshot = lrg_editor_snapshot_node (self->node);
	}
}

gboolean
lrg_editor_node_source_end_gesture (LrgEditorNodeSource *self,
                                    gboolean             commit,
                                    const gchar         *label)
{
	g_autoptr(LrgNode) snap = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), FALSE);
	g_return_val_if_fail (self->gesture_depth > 0, FALSE);

	if (--self->gesture_depth > 0)
		return FALSE;

	snap = g_steal_pointer (&self->snapshot);
	if (commit)
		return lrg_editor_commit_node_state (self->editor, self->node, snap,
		                                     label != NULL ? label : "Edit Properties", FALSE);

	/* Cancel: put back exactly what the snapshot holds */
	if (!lrg_node_state_equal (self->node, snap))
	{
		lrg_node_copy_state (self->node, snap);
		notify_preview (self);
	}
	return FALSE;
}

gboolean
lrg_editor_node_source_in_gesture (LrgEditorNodeSource *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self), FALSE);

	return self->gesture_depth > 0;
}

void
lrg_editor_node_source_refresh (LrgEditorNodeSource *self)
{
	g_return_if_fail (LRG_IS_EDITOR_NODE_SOURCE (self));

	if (self->entries == NULL)
		return;
	refresh (self);
}

/* ==========================================================================
 * Grid link
 * ========================================================================== */

typedef struct
{
	LrgPropertyGrid *grid;      /* not owned: the link lives on the grid */
	LrgEditor       *editor;    /* owned */
	gboolean         open;
	gchar           *name;
	GPtrArray       *sources;   /* the grid's sources at edit-begin */
	GPtrArray       *saved;     /* GValue* per non-node source, else NULL */
} GridLink;

static GQuark
grid_link_quark (void)
{
	return g_quark_from_static_string ("lrg-editor-node-source-grid-link");
}

static void
free_saved_value (gpointer data)
{
	GValue *v = data;

	if (v != NULL)
	{
		g_value_unset (v);
		g_free (v);
	}
}

static void
grid_link_free (gpointer data)
{
	GridLink *link = data;

	g_clear_object (&link->editor);
	g_free (link->name);
	g_clear_pointer (&link->sources, g_ptr_array_unref);
	g_clear_pointer (&link->saved, g_ptr_array_unref);
	g_free (link);
}

/* "Set Location": the undo label of an edit of @name */
static gchar *
link_label (GridLink    *link,
            const gchar *verb,
            const gchar *name)
{
	LrgPropertyRow  *row = lrg_property_grid_get_row (link->grid, name);
	LrgPropertyInfo *info = (row != NULL) ? lrg_property_row_get_info (row) : NULL;

	return g_strdup_printf ("%s %s", verb, info != NULL ? lrg_property_info_get_label (info) : name);
}

/* One notification for all node sources after a batch of previews */
static void
link_notify (GridLink *link)
{
	LrgLevel *level = lrg_editor_get_level (link->editor);
	guint     i;

	batch_depth++;
	for (i = 0; level != NULL && i < link->sources->len; i++)
	{
		gpointer src = g_ptr_array_index (link->sources, i);

		if (LRG_IS_EDITOR_NODE_SOURCE (src))
			lrg_level_notify_node_changed (level, LRG_EDITOR_NODE_SOURCE (src)->node);
	}
	batch_depth--;
	g_signal_emit_by_name (link->editor, "changed");
}

/* Re-reads every node source (after commands executed in a batch) */
static void
link_refresh (GridLink *link)
{
	guint i;

	for (i = 0; i < link->sources->len; i++)
	{
		gpointer src = g_ptr_array_index (link->sources, i);

		if (LRG_IS_EDITOR_NODE_SOURCE (src))
			lrg_editor_node_source_refresh (LRG_EDITOR_NODE_SOURCE (src));
	}
}

/* Captures the grid's sources and opens a gesture on each */
static void
link_begin (GridLink    *link,
            const gchar *name)
{
	guint n = lrg_property_grid_get_n_sources (link->grid);
	guint i;

	g_free (link->name);
	link->name = g_strdup (name);
	link->sources = g_ptr_array_new_with_free_func (g_object_unref);
	link->saved = g_ptr_array_new_with_free_func (free_saved_value);
	for (i = 0; i < n; i++)
	{
		LrgPropertySource *src = lrg_property_grid_get_source (link->grid, i);
		GValue            *saved = NULL;

		g_ptr_array_add (link->sources, g_object_ref (src));
		if (LRG_IS_EDITOR_NODE_SOURCE (src))
			lrg_editor_node_source_begin_gesture (LRG_EDITOR_NODE_SOURCE (src));
		else
		{
			saved = g_new0 (GValue, 1);
			if (!lrg_property_source_get_value (src, name, saved))
				g_clear_pointer (&saved, g_free);
		}
		g_ptr_array_add (link->saved, saved);
	}
	link->open = TRUE;
}

/* Closes the gesture: one undo group on commit, exact restore on cancel */
static void
link_finish (GridLink *link,
             gboolean  commit)
{
	g_autofree gchar *label = NULL;
	guint             i;

	if (!link->open)
		return;
	link->open = FALSE;
	label = link_label (link, "Set", link->name);

	if (commit)
	{
		/* Children's "changed" emissions are batched: one refresh after */
		lrg_editor_begin_group (link->editor, label);
		batch_depth++;
		for (i = 0; i < link->sources->len; i++)
		{
			gpointer src = g_ptr_array_index (link->sources, i);

			if (LRG_IS_EDITOR_NODE_SOURCE (src))
				lrg_editor_node_source_end_gesture (LRG_EDITOR_NODE_SOURCE (src), TRUE, label);
		}
		batch_depth--;
		lrg_editor_end_group (link->editor);
		link_refresh (link);
	}
	else
	{
		batch_depth++;
		for (i = 0; i < link->sources->len; i++)
		{
			gpointer  src = g_ptr_array_index (link->sources, i);
			GValue   *saved = g_ptr_array_index (link->saved, i);

			if (LRG_IS_EDITOR_NODE_SOURCE (src))
				lrg_editor_node_source_end_gesture (LRG_EDITOR_NODE_SOURCE (src), FALSE, NULL);
			else if (saved != NULL)
				lrg_property_source_set_value (LRG_PROPERTY_SOURCE (src), link->name, saved);
		}
		batch_depth--;
		link_notify (link);
	}

	g_clear_pointer (&link->sources, g_ptr_array_unref);
	g_clear_pointer (&link->saved, g_ptr_array_unref);
}

static void
on_grid_edit_begin (LrgPropertyGrid *grid,
                    const gchar     *name,
                    guint            gesture_id,
                    gpointer         user_data)
{
	GridLink *link = user_data;

	link_finish (link, TRUE);
	link_begin (link, name);
}

static void
on_grid_edit_value (LrgPropertyGrid *grid,
                         const gchar     *name,
                         const GValue    *value,
                         guint            gesture_id,
                         gboolean         interactive,
                         gpointer         user_data)
{
	GridLink *link = user_data;
	guint     i;

	if (!link->open || g_strcmp0 (link->name, name) != 0)
	{
		link_finish (link, TRUE);
		link_begin (link, name);
	}

	/* Every source gets its own merged value (vectors keep the source's
	 * other axes); node sources preview in place, the rest are written */
	batch_depth++;
	for (i = 0; i < link->sources->len; i++)
	{
		gpointer       src = g_ptr_array_index (link->sources, i);
		g_auto(GValue) per = G_VALUE_INIT;

		if (!lrg_property_grid_get_value_for_source (grid, name, i, value, &per))
			continue;
		if (LRG_IS_EDITOR_NODE_SOURCE (src))
		{
			LrgEditorNodeSource *ns = LRG_EDITOR_NODE_SOURCE (src);
			Entry               *e = lookup_entry (ns, name);

			if (e != NULL)
				source_write (ns, e, &per, FALSE);
		}
		else
			lrg_property_source_set_value (LRG_PROPERTY_SOURCE (src), name, &per);
	}
	batch_depth--;
	link_notify (link);
}

static void
on_grid_edit_end (LrgPropertyGrid *grid,
                  const gchar     *name,
                  guint            gesture_id,
                  gboolean         commit,
                  gpointer         user_data)
{
	link_finish ((GridLink *) user_data, commit);
}

static void
on_grid_reset_requested (LrgPropertyGrid *grid,
                         const gchar     *name,
                         gpointer         user_data)
{
	GridLink         *link = user_data;
	g_autofree gchar *label = NULL;
	guint             n;
	guint             i;

	link_finish (link, TRUE);
	label = link_label (link, "Reset", name);
	n = lrg_property_grid_get_n_sources (grid);

	/* Overridden prefab properties go back to the prefab's value, the
	 * rest to their default; all of it one undo step */
	link->sources = g_ptr_array_new_with_free_func (g_object_unref);
	lrg_editor_begin_group (link->editor, label);
	batch_depth++;
	for (i = 0; i < n; i++)
	{
		LrgPropertySource *src = lrg_property_grid_get_source (grid, i);

		g_ptr_array_add (link->sources, g_object_ref (src));
		if (LRG_IS_EDITOR_NODE_SOURCE (src) &&
		    (lrg_property_source_get_state (src, name) & LRG_PROPERTY_STATE_OVERRIDDEN))
			lrg_editor_node_source_revert_override (LRG_EDITOR_NODE_SOURCE (src), name, NULL);
		else
			lrg_property_source_reset (src, name);
	}
	batch_depth--;
	lrg_editor_end_group (link->editor);
	link_refresh (link);
	g_clear_pointer (&link->sources, g_ptr_array_unref);
}

void
lrg_editor_node_source_connect_grid (LrgPropertyGrid *grid,
                                     LrgEditor       *editor)
{
	GridLink *link;

	g_return_if_fail (LRG_IS_PROPERTY_GRID (grid));
	g_return_if_fail (LRG_IS_EDITOR (editor));

	if (g_object_get_qdata (G_OBJECT (grid), grid_link_quark ()) != NULL)
		return;

	link = g_new0 (GridLink, 1);
	link->grid = grid;
	link->editor = g_object_ref (editor);
	g_object_set_qdata_full (G_OBJECT (grid), grid_link_quark (), link, grid_link_free);

	lrg_property_grid_set_apply_mode (grid, LRG_PROPERTY_APPLY_DELEGATE);
	g_signal_connect (grid, "edit-begin", G_CALLBACK (on_grid_edit_begin), link);
	g_signal_connect (grid, "edit-value", G_CALLBACK (on_grid_edit_value), link);
	g_signal_connect (grid, "edit-end", G_CALLBACK (on_grid_edit_end), link);
	g_signal_connect (grid, "reset-requested", G_CALLBACK (on_grid_reset_requested), link);
}

void
lrg_editor_node_source_disconnect_grid (LrgPropertyGrid *grid)
{
	GridLink *link;

	g_return_if_fail (LRG_IS_PROPERTY_GRID (grid));

	link = g_object_get_qdata (G_OBJECT (grid), grid_link_quark ());
	if (link == NULL)
		return;
	link_finish (link, TRUE);
	g_signal_handlers_disconnect_by_data (grid, link);
	g_object_set_qdata (G_OBJECT (grid), grid_link_quark (), NULL);
}
