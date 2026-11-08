/* lrg-editor-physics-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The lrgeditor Physics panel: the `physics:` block of the primary
 * selection.
 *
 * Layout (top to bottom): a header (title, the node's name, Add Body /
 * Remove Body), then in a scroll box the body grid, the shape list with an
 * "add shape" kind picker, the Fit row and the selected shape's grid, the
 * joint list with its kind picker and the selected joint's grid, and while
 * playing a "Live" readout. Without a node, or for a node without a body,
 * an empty state explains what to do.
 *
 * Editing model.  The three grids edit through one private
 * LrgPropertySource (EditorPhysSource) per part (body, shape, joint) that
 * resolves the live object on every read: the selected node's current
 * LrgNodePhysics and the selected shape / joint index.  Every write goes
 * through edit_*(): snapshot the node (lrg_editor_snapshot_node()), work on
 * a deep copy of the block, install the copy and record the change with
 * lrg_editor_commit_node_state().  Undo and redo restore deep copies, so
 * nothing the panel holds can alias a history entry; that is also why the
 * sources never keep the objects themselves.  A grid gesture (a drag, a
 * typing session) merges into one history step: its first committed value
 * makes a new step, later ones merge into it (same node, same label).
 *
 * The same functions back the physics.* IPC methods, so the panel, the
 * viewport gizmo handles (lrg-editor-viewport-panel.c) and scripts all
 * produce identical history entries.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL

#include "lrg-editor-physics-panel.h"
#include "lrg-editor-app-private.h"
#include "lrg-editor-console-panel.h"
#include "../lrg-editor-math.h"
#include "../../ui/lrg-vbox.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-scroll-box.h"
#include "../../ui/lrg-combo-box.h"
#include "../../ui/lrg-expander.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-property-source.h"
#include "../../paint/lrg-paint-icons.h"
#include "../../physics/lrg-physics-layers.h"
#include "../../world3d/lrg-entity3d.h"
#include "../../world3d/components/lrg-rigid-body-component.h"
#include "../../world3d/components/lrg-collider-component3d.h"
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>
#include <raylib.h>

#define PANEL_KEY     "lrg-editor-physics-panel"
#define LIST_ROW_H    (24.0f)
#define LIST_BTN_W    (22.0f)
#define LIVE_LINES    (4)
#define BODY_LAYER_NONE "(body)"

typedef enum
{
	PART_BODY,
	PART_SHAPE,
	PART_JOINT
} Part;

/* ==========================================================================
 * Names
 * ========================================================================== */

static const gchar *
shape_kind_label (LrgCollider3DKind kind)
{
	switch (kind)
	{
	case LRG_COLLIDER3D_SPHERE:   return "Sphere";
	case LRG_COLLIDER3D_CAPSULE:  return "Capsule";
	case LRG_COLLIDER3D_CYLINDER: return "Cylinder";
	case LRG_COLLIDER3D_BOX:      return "Box";
	case LRG_COLLIDER3D_CONVEX:   return "Convex";
	case LRG_COLLIDER3D_MESH:     return "Mesh";
	case LRG_COLLIDER3D_PLANE:    return "Plane";
	default:                      return "Shape";
	}
}

static const gchar *
joint_kind_label (LrgJointKind kind)
{
	switch (kind)
	{
	case LRG_JOINT_FIXED:       return "Fixed";
	case LRG_JOINT_POINT:       return "Point";
	case LRG_JOINT_HINGE:       return "Hinge";
	case LRG_JOINT_SLIDER:      return "Slider";
	case LRG_JOINT_DISTANCE:    return "Distance";
	case LRG_JOINT_CONE:        return "Cone";
	case LRG_JOINT_SWING_TWIST: return "Swing-twist";
	case LRG_JOINT_SIX_DOF:     return "Six-DOF";
	default:                    return "Joint";
	}
}

/* "half_extent_x" -> "half-extent-x" (newly allocated). */
static gchar *
canon_name (const gchar *name)
{
	return g_strdelimit (g_strdup (name), "_", '-');
}

/* ==========================================================================
 * EditorPhysSource: one grid's view of the live block
 * ========================================================================== */

#define EDITOR_TYPE_PHYS_SOURCE (editor_phys_source_get_type ())
G_DECLARE_FINAL_TYPE (EditorPhysSource, editor_phys_source, EDITOR, PHYS_SOURCE, GObject)

struct _EditorPhysSource
{
	GObject                parent_instance;
	LrgEditorPhysicsPanel *panel;  /* weak */
	Part                   part;
};

static void editor_phys_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorPhysSource, editor_phys_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE, editor_phys_source_iface_init))

/* ==========================================================================
 * The panel
 * ========================================================================== */

struct _LrgEditorPhysicsPanel
{
	LrgContainer          parent_instance;

	LrgEditorApp         *app;          /* weak */
	LrgEditor            *editor;       /* owned */
	LrgEditorSelection   *selection;    /* owned */
	LrgEditorPlaySession *play;         /* owned */
	LrgNode              *node;         /* weak: the node shown */
	gint                  sel_shape;
	gint                  sel_joint;
	gchar                *signature;    /* what the grids were built for */

	/* widgets (owned by the container tree) */
	LrgWidget            *top;
	LrgWidget            *header_row;
	LrgWidget            *title;
	LrgLabel             *node_label;
	LrgWidget            *add_body_chip;
	LrgWidget            *remove_body_chip;
	LrgWidget            *scroll;
	LrgWidget            *content;
	LrgWidget            *empty;
	LrgWidget            *hint_none;
	LrgWidget            *hint_2d;
	LrgWidget            *hint_body;
	LrgWidget            *empty_add;
	LrgComboBox          *empty_kind;
	LrgPropertyGrid      *body_grid;
	LrgComboBox          *shape_kind;
	LrgWidget            *shape_list;
	LrgWidget            *fit_row;
	LrgComboBox          *fit_kind;
	LrgPropertyGrid      *shape_grid;
	LrgComboBox          *joint_kind;
	LrgWidget            *joint_list;
	LrgPropertyGrid      *joint_grid;
	LrgExpander          *live_section;
	LrgWidget            *live;

	/* the grids' sources */
	EditorPhysSource     *src[3];

	/* gestures */
	gboolean              in_gesture;
	gboolean              gesture_first;
	gboolean              applying;
};

G_DEFINE_FINAL_TYPE (LrgEditorPhysicsPanel, lrg_editor_physics_panel, LRG_TYPE_CONTAINER)

static void sync_widgets (LrgEditorPhysicsPanel *self);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static LrgLevel *
current_level (LrgEditorPhysicsPanel *self)
{
	return (self->editor != NULL) ? lrg_editor_get_level (self->editor) : NULL;
}

static LrgNode *
resolve_node (LrgEditorPhysicsPanel *self,
              LrgNode               *node)
{
	return (node != NULL) ? node : self->node;
}

static LrgNodePhysicsShape *
shape_at (LrgNodePhysics *phys,
          gint            index)
{
	GPtrArray *shapes = (phys != NULL) ? lrg_node_physics_get_shapes (phys) : NULL;

	return (shapes != NULL && index >= 0 && (guint) index < shapes->len) ? g_ptr_array_index (shapes, index) : NULL;
}

static LrgNodePhysicsJoint *
joint_at (LrgNodePhysics *phys,
          gint            index)
{
	GPtrArray *joints = (phys != NULL) ? lrg_node_physics_get_joints (phys) : NULL;

	return (joints != NULL && index >= 0 && (guint) index < joints->len) ? g_ptr_array_index (joints, index) : NULL;
}

/* The project's layer table, or the engine defaults without a project. */
static LrgPhysicsLayers *
layers_of (LrgEditorPhysicsPanel *self,
           LrgPhysicsLayers     **owned)
{
	*owned = NULL;
	if (self->app != NULL && self->app->project != NULL)
		return lrg_project_get_physics_layers (self->app->project);
	*owned = lrg_physics_layers_new_default ();
	return *owned;
}

/* The named layers, in slot order. */
static GPtrArray *
layer_names (LrgEditorPhysicsPanel *self)
{
	LrgPhysicsLayers *owned = NULL;
	LrgPhysicsLayers *layers = layers_of (self, &owned);
	GPtrArray        *names = g_ptr_array_new_with_free_func (g_free);
	guint             i;

	for (i = 0; i < LRG_PHYSICS_LAYERS_MAX; i++)
	{
		const gchar *name = lrg_physics_layers_get_name (layers, i);

		if (name != NULL && *name != '\0')
			g_ptr_array_add (names, g_strdup (name));
	}
	g_clear_object (&owned);
	return names;
}

/* A node's display path (for joint targets). */
static gchar *
node_path_for_guid (LrgEditorPhysicsPanel *self,
                    const gchar           *guid)
{
	LrgLevel *level = current_level (self);
	LrgNode  *node = (level != NULL && guid != NULL) ? lrg_level_find_node (level, guid) : NULL;

	if (guid == NULL || *guid == '\0')
		return g_strdup ("");
	if (node == NULL)
		return g_strdup (guid);
	return lrg_node_get_path (node);
}

/* ==========================================================================
 * Field access (shared by the grids and IPC)
 * ========================================================================== */

/* Reads a GrlVector3 from a value: the boxed type, or "x,y,z". */
static gboolean
value_to_vec3 (const GValue *value,
               gfloat       *out)
{
	if (G_VALUE_HOLDS (value, GRL_TYPE_VECTOR3))
	{
		const GrlVector3 *v = g_value_get_boxed (value);

		if (v == NULL)
			return FALSE;
		out[0] = v->x;
		out[1] = v->y;
		out[2] = v->z;
		return TRUE;
	}
	if (G_VALUE_HOLDS_STRING (value) && g_value_get_string (value) != NULL)
	{
		g_auto(GStrv) parts = g_strsplit_set (g_value_get_string (value), ", ", -1);
		guint         n = 0, i;

		for (i = 0; parts[i] != NULL && n < 3; i++)
			if (*parts[i] != '\0')
				out[n++] = (gfloat) g_ascii_strtod (parts[i], NULL);
		return n == 3;
	}
	return FALSE;
}

static gboolean
is_vector_field (Part         part,
                 const gchar *name)
{
	if (part == PART_SHAPE)
		return g_strcmp0 (name, "half-extents") == 0 || g_strcmp0 (name, "offset") == 0 ||
		       g_strcmp0 (name, "rotation") == 0;
	if (part == PART_JOINT)
		return g_strcmp0 (name, "anchor") == 0 || g_strcmp0 (name, "axis") == 0;
	return FALSE;
}

/* Reads field @name of @object (a body, shape or joint) for display. */
static gboolean
field_get (LrgEditorPhysicsPanel *self,
           Part                   part,
           GObject               *object,
           const gchar           *name,
           GValue                *out)
{
	GParamSpec *pspec;

	if (is_vector_field (part, name))
	{
		gfloat v[3];

		if (part == PART_SHAPE && g_strcmp0 (name, "half-extents") == 0)
			lrg_node_physics_shape_get_half_extents (LRG_NODE_PHYSICS_SHAPE (object), v);
		else if (part == PART_SHAPE && g_strcmp0 (name, "offset") == 0)
			lrg_node_physics_shape_get_offset (LRG_NODE_PHYSICS_SHAPE (object), v);
		else if (part == PART_SHAPE)
			lrg_node_physics_shape_get_rotation (LRG_NODE_PHYSICS_SHAPE (object), v);
		else if (g_strcmp0 (name, "anchor") == 0)
			lrg_node_physics_joint_get_anchor (LRG_NODE_PHYSICS_JOINT (object), v);
		else
			lrg_node_physics_joint_get_axis (LRG_NODE_PHYSICS_JOINT (object), v);
		g_value_init (out, GRL_TYPE_VECTOR3);
		g_value_take_boxed (out, grl_vector3_new (v[0], v[1], v[2]));
		return TRUE;
	}
	if (part == PART_JOINT && g_strcmp0 (name, "target") == 0)
	{
		g_value_init (out, G_TYPE_STRING);
		g_value_take_string (out, node_path_for_guid (self, lrg_node_physics_joint_get_target (LRG_NODE_PHYSICS_JOINT (object))));
		return TRUE;
	}
	if (g_strcmp0 (name, "layer") == 0)
	{
		const gchar *layer = (part == PART_SHAPE) ? lrg_node_physics_shape_get_layer (LRG_NODE_PHYSICS_SHAPE (object))
		                                          : lrg_node_physics_get_layer (LRG_NODE_PHYSICS (object));

		g_value_init (out, G_TYPE_STRING);
		if (layer != NULL && *layer != '\0')
			g_value_set_string (out, layer);
		else if (part == PART_SHAPE)
			g_value_set_string (out, BODY_LAYER_NONE);
		else
		{
			/* no name: layer slot 0 */
			g_autoptr(GPtrArray) names = layer_names (self);

			g_value_set_string (out, names->len > 0 ? g_ptr_array_index (names, 0) : "default");
		}
		return TRUE;
	}
	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);
	if (pspec == NULL)
		return FALSE;
	g_value_init (out, pspec->value_type);
	g_object_get_property (object, name, out);
	return TRUE;
}

/* Writes field @name of @object (a detached copy); FALSE with @error for
 * unknown fields or values that do not fit. */
static gboolean
field_set (LrgEditorPhysicsPanel  *self,
           Part                    part,
           GObject                *object,
           const gchar            *raw_name,
           const GValue           *value,
           GError                **error)
{
	g_autofree gchar *name = canon_name (raw_name);
	GParamSpec       *pspec;
	g_auto(GValue)    v = G_VALUE_INIT;

	/* Vectors */
	if (is_vector_field (part, name))
	{
		gfloat vec[3];

		if (!value_to_vec3 (value, vec))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "%s takes three numbers", name);
			return FALSE;
		}
		if (part == PART_SHAPE && g_strcmp0 (name, "half-extents") == 0)
			lrg_node_physics_shape_set_half_extents (LRG_NODE_PHYSICS_SHAPE (object), vec[0], vec[1], vec[2]);
		else if (part == PART_SHAPE && g_strcmp0 (name, "offset") == 0)
			lrg_node_physics_shape_set_offset (LRG_NODE_PHYSICS_SHAPE (object), vec[0], vec[1], vec[2]);
		else if (part == PART_SHAPE)
			lrg_node_physics_shape_set_rotation (LRG_NODE_PHYSICS_SHAPE (object), vec[0], vec[1], vec[2]);
		else if (g_strcmp0 (name, "anchor") == 0)
			lrg_node_physics_joint_set_anchor (LRG_NODE_PHYSICS_JOINT (object), vec[0], vec[1], vec[2]);
		else
		{
			if (vec[0] == 0.0f && vec[1] == 0.0f && vec[2] == 0.0f)
			{
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "axis must not be zero");
				return FALSE;
			}
			lrg_node_physics_joint_set_axis (LRG_NODE_PHYSICS_JOINT (object), vec[0], vec[1], vec[2]);
		}
		return TRUE;
	}

	/* Joint target: a node path or GUID, "" for the world */
	if (part == PART_JOINT && g_strcmp0 (name, "target") == 0)
	{
		const gchar *ref = G_VALUE_HOLDS_STRING (value) ? g_value_get_string (value) : NULL;
		LrgNode     *target;

		if (ref == NULL || *ref == '\0')
		{
			lrg_node_physics_joint_set_target (LRG_NODE_PHYSICS_JOINT (object), NULL);
			return TRUE;
		}
		target = lrg_editor_find_node (self->editor, ref);
		if (target == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No node '%s'", ref);
			return FALSE;
		}
		lrg_node_physics_joint_set_target (LRG_NODE_PHYSICS_JOINT (object), lrg_node_get_guid (target));
		return TRUE;
	}

	/* Layers: "(body)" / "" mean the body's (shapes) or slot 0 (body) */
	if (g_strcmp0 (name, "layer") == 0)
	{
		const gchar *layer = G_VALUE_HOLDS_STRING (value) ? g_value_get_string (value) : NULL;

		if (layer != NULL && (*layer == '\0' || g_strcmp0 (layer, BODY_LAYER_NONE) == 0))
			layer = NULL;
		if (part == PART_SHAPE)
			lrg_node_physics_shape_set_layer (LRG_NODE_PHYSICS_SHAPE (object), layer);
		else
			lrg_node_physics_set_layer (LRG_NODE_PHYSICS (object), layer);
		return TRUE;
	}

	pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);
	if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No field '%s'", name);
		return FALSE;
	}
	g_value_init (&v, pspec->value_type);

	/* Enums by nick or name */
	if (G_IS_PARAM_SPEC_ENUM (pspec) && G_VALUE_HOLDS_STRING (value))
	{
		GEnumClass *klass = G_PARAM_SPEC_ENUM (pspec)->enum_class;
		const gchar *s = g_value_get_string (value);
		GEnumValue *ev = (s != NULL) ? g_enum_get_value_by_nick (klass, s) : NULL;

		if (ev == NULL && s != NULL)
			ev = g_enum_get_value_by_name (klass, s);
		if (ev == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown %s '%s'", name,
			             s != NULL ? s : "(null)");
			return FALSE;
		}
		g_value_set_enum (&v, ev->value);
	}
	else if (G_IS_PARAM_SPEC_ENUM (pspec) && (G_VALUE_HOLDS_INT64 (value) || G_VALUE_HOLDS_INT (value) ||
	                                          G_VALUE_HOLDS_DOUBLE (value)))
	{
		g_auto(GValue) i = G_VALUE_INIT;

		g_value_init (&i, G_TYPE_INT);
		g_value_transform (value, &i);
		if (g_enum_get_value (G_PARAM_SPEC_ENUM (pspec)->enum_class, g_value_get_int (&i)) == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Bad %s %d", name, g_value_get_int (&i));
			return FALSE;
		}
		g_value_set_enum (&v, g_value_get_int (&i));
	}
	else if (!g_value_type_transformable (G_VALUE_TYPE (value), pspec->value_type) || !g_value_transform (value, &v))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Bad value type for %s", name);
		return FALSE;
	}
	g_object_set_property (object, name, &v);
	return TRUE;
}

/* ==========================================================================
 * Undoable edits
 *
 * Edit = snapshot, deep copy, mutate the copy, install it, commit. A copy
 * keeps the old object (which a history entry may share via the snapshot)
 * untouched.
 * ========================================================================== */

typedef struct
{
	LrgNode        *node;     /* borrowed */
	LrgNode        *before;   /* owned snapshot */
	LrgNodePhysics *phys;     /* owned working copy, nullable */
} Edit;

static void
edit_begin (Edit    *e,
            LrgNode *node)
{
	LrgNodePhysics *old = lrg_node_get_physics (node);

	e->node = node;
	e->before = lrg_editor_snapshot_node (node);
	e->phys = (old != NULL) ? lrg_node_physics_copy (old) : NULL;
}

static void
edit_abort (Edit *e)
{
	g_clear_object (&e->before);
	g_clear_object (&e->phys);
}

/* Installs the copy and records the step; @merge folds it into the
 * previous step of the same node and label. */
static gboolean
edit_commit (LrgEditorPhysicsPanel *self,
             Edit                  *e,
             const gchar           *label,
             gboolean               merge)
{
	gboolean done;

	self->applying = TRUE;
	lrg_node_set_physics (e->node, e->phys);
	if (current_level (self) != NULL)
		lrg_level_notify_node_changed (current_level (self), e->node);
	done = lrg_editor_commit_node_state (self->editor, e->node, e->before, label, merge);
	self->applying = FALSE;
	edit_abort (e);
	return done;
}

/* Commits as part of the running grid gesture (merging after its first
 * real step) or as a step of its own. */
static gboolean
edit_commit_gesture (LrgEditorPhysicsPanel *self,
                     Edit                  *e,
                     const gchar           *label)
{
	gboolean merge = self->in_gesture && !self->gesture_first;
	gboolean done = edit_commit (self, e, label, merge);

	if (done && self->in_gesture)
		self->gesture_first = FALSE;
	if (!self->in_gesture)
		sync_widgets (self);
	return done;
}

/* A fitting first shape for @node: by primitive or asset. */
static LrgCollider3DKind
auto_shape_kind (LrgNode             *node,
                 LrgDynamicsBodyKind  body)
{
	LrgNodeVisual *visual = lrg_node_get_visual (node);

	if (visual == NULL)
		return LRG_COLLIDER3D_BOX;
	switch (lrg_node_visual_get_kind (visual))
	{
	case LRG_NODE_VISUAL_PRIMITIVE:
		switch (lrg_node_visual_get_primitive (visual))
		{
		case LRG_PRIMITIVE_UV_SPHERE:
		case LRG_PRIMITIVE_ICO_SPHERE:
			return LRG_COLLIDER3D_SPHERE;
		case LRG_PRIMITIVE_CYLINDER:
		case LRG_PRIMITIVE_CONE:
			return LRG_COLLIDER3D_CYLINDER;
		default:
			return LRG_COLLIDER3D_BOX;
		}
	case LRG_NODE_VISUAL_MESH_ASSET:
		return body == LRG_DYNAMICS_BODY_DYNAMIC ? LRG_COLLIDER3D_CONVEX : LRG_COLLIDER3D_MESH;
	default:
		return LRG_COLLIDER3D_BOX;
	}
}

/* The node's local geometry bounds: the model file's when it can be read
 * (needs a window), the editor's pick bounds otherwise. */
static void
node_bounds (LrgEditorPhysicsPanel *self,
             LrgNode               *node,
             gfloat                *mn,
             gfloat                *mx)
{
	LrgNodeVisual *visual = lrg_node_get_visual (node);

	lrg_editor_node_get_local_bounds (node, mn, mx);
	if (visual != NULL && lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_MESH_ASSET &&
	    lrg_node_visual_get_asset (visual) != NULL && self->app != NULL && IsWindowReady ())
	{
		g_autofree gchar   *path = editor_app_project_path (self->app, lrg_node_visual_get_asset (visual));
		g_autoptr(GrlModel) model = (path != NULL && g_file_test (path, G_FILE_TEST_EXISTS))
		                            ? grl_model_new_from_file (path, NULL) : NULL;
		GrlBoundingBox     *box = (model != NULL) ? grl_model_get_bounding_box (model) : NULL;

		if (box != NULL)
		{
			mn[0] = box->min.x;
			mn[1] = box->min.y;
			mn[2] = box->min.z;
			mx[0] = box->max.x;
			mx[1] = box->max.y;
			mx[2] = box->max.z;
			g_boxed_free (GRL_TYPE_BOUNDING_BOX, box);
		}
	}
}

void
lrg_editor_physics_fit_shape_to_bounds (LrgNodePhysicsShape *shape,
                                        LrgCollider3DKind    kind,
                                        const gfloat        *min,
                                        const gfloat        *max)
{
	gfloat c[3], h[3], radial;
	gint   k;

	g_return_if_fail (LRG_IS_NODE_PHYSICS_SHAPE (shape));
	g_return_if_fail (min != NULL && max != NULL);

	for (k = 0; k < 3; k++)
	{
		c[k] = (min[k] + max[k]) * 0.5f;
		h[k] = MAX ((max[k] - min[k]) * 0.5f, 0.001f);
	}
	radial = MAX (h[0], h[2]);
	lrg_node_physics_shape_set_kind (shape, kind);
	lrg_node_physics_shape_set_rotation (shape, 0.0f, 0.0f, 0.0f);
	switch (kind)
	{
	case LRG_COLLIDER3D_BOX:
		lrg_node_physics_shape_set_half_extents (shape, h[0], h[1], h[2]);
		break;
	case LRG_COLLIDER3D_SPHERE:
		lrg_node_physics_shape_set_radius (shape, MAX (radial, h[1]));
		break;
	case LRG_COLLIDER3D_CAPSULE:
		/* half the distance between the cap centres: the caps fill the rest */
		lrg_node_physics_shape_set_radius (shape, radial);
		lrg_node_physics_shape_set_half_height (shape, MAX (h[1] - radial, 0.0f));
		break;
	case LRG_COLLIDER3D_CYLINDER:
		lrg_node_physics_shape_set_radius (shape, radial);
		lrg_node_physics_shape_set_half_height (shape, h[1]);
		break;
	case LRG_COLLIDER3D_CONVEX:
	case LRG_COLLIDER3D_MESH:
	case LRG_COLLIDER3D_PLANE:
	default:
		/* the node's own model, where it is */
		lrg_node_physics_shape_set_mesh (shape, NULL);
		lrg_node_physics_shape_set_offset (shape, 0.0f, 0.0f, 0.0f);
		return;
	}
	lrg_node_physics_shape_set_offset (shape, c[0], c[1], c[2]);
}

/* A new shape of @kind, fitted to @node or with the component defaults. */
static LrgNodePhysicsShape *
make_shape (LrgEditorPhysicsPanel *self,
            LrgNode               *node,
            LrgCollider3DKind      kind,
            gboolean               fit)
{
	LrgNodePhysicsShape *shape = lrg_node_physics_shape_new (kind);

	if (fit)
	{
		gfloat mn[3], mx[3];

		node_bounds (self, node, mn, mx);
		lrg_editor_physics_fit_shape_to_bounds (shape, kind, mn, mx);
	}
	return shape;
}

/* ==========================================================================
 * Public API: editing
 * ========================================================================== */

LrgNode *
lrg_editor_physics_panel_get_node (LrgEditorPhysicsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), NULL);
	return self->node;
}

LrgPropertyGrid *
lrg_editor_physics_panel_get_body_grid (LrgEditorPhysicsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), NULL);
	return self->body_grid;
}

LrgPropertyGrid *
lrg_editor_physics_panel_get_shape_grid (LrgEditorPhysicsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), NULL);
	return self->shape_grid;
}

LrgPropertyGrid *
lrg_editor_physics_panel_get_joint_grid (LrgEditorPhysicsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), NULL);
	return self->joint_grid;
}

gint
lrg_editor_physics_panel_get_selected_shape (LrgEditorPhysicsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), -1);
	return self->sel_shape;
}

void
lrg_editor_physics_panel_set_selected_shape (LrgEditorPhysicsPanel *self,
                                             gint                   index)
{
	LrgNodePhysics *phys;

	g_return_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self));

	phys = (self->node != NULL) ? lrg_node_get_physics (self->node) : NULL;
	self->sel_shape = (shape_at (phys, index) != NULL) ? index : -1;
	sync_widgets (self);
	if (self->app != NULL && self->app->viewport_panel != NULL)
		lrg_editor_viewport_panel_queue_render (self->app->viewport_panel);
}

gint
lrg_editor_physics_panel_get_selected_joint (LrgEditorPhysicsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), -1);
	return self->sel_joint;
}

void
lrg_editor_physics_panel_set_selected_joint (LrgEditorPhysicsPanel *self,
                                             gint                   index)
{
	LrgNodePhysics *phys;

	g_return_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self));

	phys = (self->node != NULL) ? lrg_node_get_physics (self->node) : NULL;
	self->sel_joint = (joint_at (phys, index) != NULL) ? index : -1;
	sync_widgets (self);
}

gboolean
lrg_editor_physics_panel_add_body (LrgEditorPhysicsPanel *self,
                                   LrgNode               *node,
                                   LrgDynamicsBodyKind    kind,
                                   gboolean               fit)
{
	Edit e;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), FALSE);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), FALSE);

	node = resolve_node (self, node);
	if (node == NULL || lrg_node_get_physics (node) != NULL)
		return FALSE;
	edit_begin (&e, node);
	e.phys = lrg_node_physics_new (kind);
	if (fit)
	{
		g_autoptr(LrgNodePhysicsShape) shape = make_shape (self, node, auto_shape_kind (node, kind), TRUE);

		lrg_node_physics_add_shape (e.phys, shape);
	}
	edit_commit (self, &e, "Add Physics Body", FALSE);
	if (node == self->node)
		self->sel_shape = fit ? 0 : -1;
	if (self->app != NULL)
		editor_app_set_status (self->app, "Added a physics body to %s", lrg_node_get_name (node));
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_physics_panel_remove_body (LrgEditorPhysicsPanel *self,
                                      LrgNode               *node)
{
	Edit e;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), FALSE);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), FALSE);

	node = resolve_node (self, node);
	if (node == NULL || lrg_node_get_physics (node) == NULL)
		return FALSE;
	edit_begin (&e, node);
	g_clear_object (&e.phys);
	edit_commit (self, &e, "Remove Physics Body", FALSE);
	if (self->app != NULL)
		editor_app_set_status (self->app, "Removed the physics body of %s", lrg_node_get_name (node));
	sync_widgets (self);
	return TRUE;
}

/* The history label of a field edit. */
static gchar *
field_label (Part         part,
             const gchar *name)
{
	g_autofree gchar *canon = canon_name (name);

	return g_strdup_printf ("Physics %s: %s", part == PART_BODY ? "Body" : part == PART_SHAPE ? "Shape" : "Joint",
	                        canon);
}

/* One field of the body / shape @index / joint @index of @node. */
static gboolean
set_field (LrgEditorPhysicsPanel  *self,
           LrgNode                *node,
           Part                    part,
           guint                   index,
           const gchar            *name,
           const GValue           *value,
           gboolean                gesture,
           GError                **error)
{
	g_autofree gchar *label = NULL;
	GObject          *target;
	Edit              e;

	node = resolve_node (self, node);
	if (node == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No node");
		return FALSE;
	}
	edit_begin (&e, node);
	if (e.phys == NULL)
	{
		if (part != PART_BODY)
		{
			edit_abort (&e);
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s has no physics body", lrg_node_get_name (node));
			return FALSE;
		}
		e.phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	}
	if (part == PART_BODY)
		target = G_OBJECT (e.phys);
	else if (part == PART_SHAPE)
		target = (GObject *) shape_at (e.phys, (gint) index);
	else
		target = (GObject *) joint_at (e.phys, (gint) index);
	if (target == NULL)
	{
		edit_abort (&e);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No %s %u", part == PART_SHAPE ? "shape" : "joint",
		             index);
		return FALSE;
	}
	if (!field_set (self, part, target, name, value, error))
	{
		edit_abort (&e);
		return FALSE;
	}
	label = field_label (part, name);
	if (gesture)
		edit_commit_gesture (self, &e, label);
	else
	{
		edit_commit (self, &e, label, FALSE);
		sync_widgets (self);
	}
	return TRUE;
}

gboolean
lrg_editor_physics_panel_set_body_field (LrgEditorPhysicsPanel *self,
                                         LrgNode               *node,
                                         const gchar           *name,
                                         const GValue          *value)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), FALSE);
	g_return_val_if_fail (name != NULL && G_IS_VALUE (value), FALSE);
	return set_field (self, node, PART_BODY, 0, name, value, FALSE, NULL);
}

gboolean
lrg_editor_physics_panel_set_shape_field (LrgEditorPhysicsPanel *self,
                                          LrgNode               *node,
                                          guint                  index,
                                          const gchar           *name,
                                          const GValue          *value)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), FALSE);
	g_return_val_if_fail (name != NULL && G_IS_VALUE (value), FALSE);
	return set_field (self, node, PART_SHAPE, index, name, value, FALSE, NULL);
}

gboolean
lrg_editor_physics_panel_set_joint_field (LrgEditorPhysicsPanel *self,
                                          LrgNode               *node,
                                          guint                  index,
                                          const gchar           *name,
                                          const GValue          *value)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), FALSE);
	g_return_val_if_fail (name != NULL && G_IS_VALUE (value), FALSE);
	return set_field (self, node, PART_JOINT, index, name, value, FALSE, NULL);
}

gint
lrg_editor_physics_panel_add_shape (LrgEditorPhysicsPanel *self,
                                    LrgNode               *node,
                                    LrgCollider3DKind      kind,
                                    gboolean               fit)
{
	g_autoptr(LrgNodePhysicsShape) shape = NULL;
	g_autofree gchar              *label = NULL;
	gint                           index;
	Edit                           e;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), -1);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), -1);

	node = resolve_node (self, node);
	if (node == NULL)
		return -1;
	edit_begin (&e, node);
	if (e.phys == NULL)
		e.phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	shape = make_shape (self, node, kind, fit);
	lrg_node_physics_add_shape (e.phys, shape);
	index = (gint) lrg_node_physics_get_shapes (e.phys)->len - 1;
	label = g_strdup_printf ("Add %s Shape", shape_kind_label (kind));
	edit_commit (self, &e, label, FALSE);
	if (node == self->node)
		self->sel_shape = index;
	sync_widgets (self);
	return index;
}

gboolean
lrg_editor_physics_panel_remove_shape (LrgEditorPhysicsPanel *self,
                                       LrgNode               *node,
                                       guint                  index)
{
	Edit e;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), FALSE);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), FALSE);

	node = resolve_node (self, node);
	if (node == NULL || shape_at (lrg_node_get_physics (node), (gint) index) == NULL)
		return FALSE;
	edit_begin (&e, node);
	lrg_node_physics_remove_shape_at (e.phys, index);
	edit_commit (self, &e, "Remove Shape", FALSE);
	if (node == self->node && self->sel_shape > (gint) index)
		self->sel_shape--;
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_physics_panel_move_shape (LrgEditorPhysicsPanel *self,
                                     LrgNode               *node,
                                     guint                  index,
                                     guint                  to)
{
	g_autoptr(GPtrArray) order = NULL;
	GPtrArray           *shapes;
	gpointer             moved;
	guint                n, i;
	Edit                 e;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), FALSE);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), FALSE);

	node = resolve_node (self, node);
	if (node == NULL || shape_at (lrg_node_get_physics (node), (gint) index) == NULL)
		return FALSE;
	n = lrg_node_physics_get_shapes (lrg_node_get_physics (node))->len;
	to = MIN (to, n - 1);
	if (to == index)
		return FALSE;

	/* Rebuild the list in the new order (the block has no insert) */
	edit_begin (&e, node);
	shapes = lrg_node_physics_get_shapes (e.phys);
	order = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < shapes->len; i++)
		g_ptr_array_add (order, g_object_ref (g_ptr_array_index (shapes, i)));
	moved = g_ptr_array_steal_index (order, index);
	g_ptr_array_insert (order, (gint) to, moved);
	while (lrg_node_physics_get_shapes (e.phys)->len > 0)
		lrg_node_physics_remove_shape_at (e.phys, 0);
	for (i = 0; i < order->len; i++)
		lrg_node_physics_add_shape (e.phys, g_ptr_array_index (order, i));
	edit_commit (self, &e, "Reorder Shapes", FALSE);
	if (node == self->node && self->sel_shape == (gint) index)
		self->sel_shape = (gint) to;
	sync_widgets (self);
	return TRUE;
}

gboolean
lrg_editor_physics_panel_fit_shape (LrgEditorPhysicsPanel *self,
                                    LrgNode               *node,
                                    guint                  index,
                                    LrgCollider3DKind      kind)
{
	g_autofree gchar *label = NULL;
	gfloat            mn[3], mx[3];
	Edit              e;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), FALSE);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), FALSE);

	node = resolve_node (self, node);
	if (node == NULL || shape_at (lrg_node_get_physics (node), (gint) index) == NULL)
		return FALSE;
	node_bounds (self, node, mn, mx);
	edit_begin (&e, node);
	lrg_editor_physics_fit_shape_to_bounds (shape_at (e.phys, (gint) index), kind, mn, mx);
	label = g_strdup_printf ("Fit %s to Mesh", shape_kind_label (kind));
	edit_commit (self, &e, label, FALSE);
	sync_widgets (self);
	return TRUE;
}

gint
lrg_editor_physics_panel_add_joint (LrgEditorPhysicsPanel *self,
                                    LrgNode               *node,
                                    LrgJointKind           kind,
                                    const gchar           *target)
{
	g_autoptr(LrgNodePhysicsJoint) joint = NULL;
	g_autofree gchar              *label = NULL;
	LrgNode                       *other = NULL;
	gint                           index;
	Edit                           e;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), -1);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), -1);

	node = resolve_node (self, node);
	if (node == NULL)
		return -1;
	if (target != NULL && *target != '\0')
	{
		other = lrg_editor_find_node (self->editor, target);
		if (other == NULL || other == node)
			return -1;
	}
	edit_begin (&e, node);
	if (e.phys == NULL)
		e.phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	joint = lrg_node_physics_joint_new (kind);
	if (other != NULL)
		lrg_node_physics_joint_set_target (joint, lrg_node_get_guid (other));
	lrg_node_physics_add_joint (e.phys, joint);
	index = (gint) lrg_node_physics_get_joints (e.phys)->len - 1;
	label = g_strdup_printf ("Add %s Joint", joint_kind_label (kind));
	edit_commit (self, &e, label, FALSE);
	if (node == self->node)
		self->sel_joint = index;
	sync_widgets (self);
	return index;
}

gboolean
lrg_editor_physics_panel_remove_joint (LrgEditorPhysicsPanel *self,
                                       LrgNode               *node,
                                       guint                  index)
{
	Edit e;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), FALSE);
	g_return_val_if_fail (node == NULL || LRG_IS_NODE (node), FALSE);

	node = resolve_node (self, node);
	if (node == NULL || joint_at (lrg_node_get_physics (node), (gint) index) == NULL)
		return FALSE;
	edit_begin (&e, node);
	lrg_node_physics_remove_joint_at (e.phys, index);
	edit_commit (self, &e, "Remove Joint", FALSE);
	if (node == self->node && self->sel_joint >= (gint) index)
		self->sel_joint--;
	sync_widgets (self);
	return TRUE;
}

/* ==========================================================================
 * Live state (play in editor)
 * ========================================================================== */

static LrgRigidBodyComponent *
live_body (LrgEditorPhysicsPanel *self,
           LrgNode               *node)
{
	GObject      *live;
	LrgComponent *rb;

	if (node == NULL || self->play == NULL || !lrg_editor_play_session_is_playing (self->play))
		return NULL;
	live = lrg_editor_play_session_get_live_for_node (self->play, node);
	if (live == NULL || !LRG_IS_ENTITY3D (live))
		return NULL;
	rb = lrg_entity3d_get_component (LRG_ENTITY3D (live), LRG_TYPE_RIGID_BODY_COMPONENT);
	return (rb != NULL) ? LRG_RIGID_BODY_COMPONENT (rb) : NULL;
}

/* Contact manifolds touching @body_id this step. */
static guint
count_contacts (LrgDynamicsWorld *world,
                guint             body_id)
{
	guint n, i, count = 0;

	if (world == NULL || body_id == 0)
		return 0;
	n = lrg_dynamics_world_get_contact_count (world);
	for (i = 0; i < n; i++)
	{
		LrgDynamicsContactKind kind;
		guint                  a = 0, b = 0;
		gfloat                 p[3], nrm[3], depth;

		if (lrg_dynamics_world_get_contact (world, i, &kind, &a, &b, p, nrm, &depth, NULL) &&
		    (a == body_id || b == body_id))
			count++;
	}
	return count;
}

/* The live state as JSON members on @o; FALSE without a live body. */
static gboolean
live_state (LrgEditorPhysicsPanel *self,
            LrgNode               *node,
            JsonObject            *o)
{
	LrgRigidBodyComponent *rb = live_body (self, node);
	gfloat                 v[3], w[3], p[3];
	guint                  id;
	JsonArray             *a;
	gint                   k;

	if (rb == NULL)
		return FALSE;
	id = lrg_rigid_body_component_get_body_id (rb);
	lrg_rigid_body_component_get_linear_velocity (rb, v);
	lrg_rigid_body_component_get_angular_velocity (rb, w);
	memset (p, 0, sizeof p);
	if (lrg_rigid_body_component_get_physics (rb) != NULL && id != 0)
		lrg_dynamics_world_get_position (lrg_rigid_body_component_get_physics (rb), id, p, NULL);
	json_object_set_int_member (o, "body-id", id);
	a = json_array_new ();
	for (k = 0; k < 3; k++)
		json_array_add_double_element (a, p[k]);
	json_object_set_array_member (o, "position", a);
	a = json_array_new ();
	for (k = 0; k < 3; k++)
		json_array_add_double_element (a, v[k]);
	json_object_set_array_member (o, "velocity", a);
	a = json_array_new ();
	for (k = 0; k < 3; k++)
		json_array_add_double_element (a, w[k]);
	json_object_set_array_member (o, "angular-velocity", a);
	json_object_set_boolean_member (o, "sleeping", lrg_rigid_body_component_is_sleeping (rb));
	json_object_set_double_member (o, "mass", lrg_rigid_body_component_get_mass (rb));
	json_object_set_int_member (o, "contacts", count_contacts (lrg_rigid_body_component_get_physics (rb), id));
	return TRUE;
}

gchar *
lrg_editor_physics_panel_describe_live (LrgEditorPhysicsPanel *self)
{
	LrgRigidBodyComponent *rb;
	gfloat                 v[3], w[3];
	guint                  id;

	g_return_val_if_fail (LRG_IS_EDITOR_PHYSICS_PANEL (self), NULL);

	rb = live_body (self, self->node);
	if (rb == NULL)
		return NULL;
	id = lrg_rigid_body_component_get_body_id (rb);
	lrg_rigid_body_component_get_linear_velocity (rb, v);
	lrg_rigid_body_component_get_angular_velocity (rb, w);
	return g_strdup_printf ("Velocity   %7.2f %7.2f %7.2f   |v| %.2f m/s\n"
	                        "Angular    %7.2f %7.2f %7.2f   rad/s\n"
	                        "State      %s   body #%u\n"
	                        "Contacts   %u",
	                        (gdouble) v[0], (gdouble) v[1], (gdouble) v[2],
	                        (gdouble) sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]),
	                        (gdouble) w[0], (gdouble) w[1], (gdouble) w[2],
	                        lrg_rigid_body_component_is_sleeping (rb) ? "sleeping" : "awake", id,
	                        count_contacts (lrg_rigid_body_component_get_physics (rb), id));
}

/* ==========================================================================
 * EditorPhysSource implementation
 * ========================================================================== */

/* The object a source shows right now (NULL when none). */
static GObject *
source_target (EditorPhysSource *src)
{
	LrgEditorPhysicsPanel *panel = src->panel;
	LrgNodePhysics        *phys = (panel != NULL && panel->node != NULL) ? lrg_node_get_physics (panel->node) : NULL;

	if (phys == NULL)
		return NULL;
	switch (src->part)
	{
	case PART_BODY:
		return G_OBJECT (phys);
	case PART_SHAPE:
		return (GObject *) shape_at (phys, panel->sel_shape);
	case PART_JOINT:
	default:
		return (GObject *) joint_at (phys, panel->sel_joint);
	}
}

/* Virtual pspecs: vectors and the string rows with dynamic choices. */
static GParamSpec *
vec_pspec (const gchar *name)
{
	static GHashTable *specs = NULL;
	GParamSpec        *pspec;

	if (specs == NULL)
		specs = g_hash_table_new (g_str_hash, g_str_equal);
	pspec = g_hash_table_lookup (specs, name);
	if (pspec == NULL)
	{
		pspec = g_param_spec_ref_sink (g_param_spec_boxed (g_intern_string (name), NULL, NULL, GRL_TYPE_VECTOR3,
		                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
		g_hash_table_insert (specs, (gpointer) g_intern_string (name), pspec);
	}
	return pspec;
}

static GParamSpec *
string_pspec (const gchar *name)
{
	static GHashTable *specs = NULL;
	GParamSpec        *pspec;

	if (specs == NULL)
		specs = g_hash_table_new (g_str_hash, g_str_equal);
	pspec = g_hash_table_lookup (specs, name);
	if (pspec == NULL)
	{
		pspec = g_param_spec_ref_sink (g_param_spec_string (g_intern_string (name), NULL, NULL, NULL,
		                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
		g_hash_table_insert (specs, (gpointer) g_intern_string (name), pspec);
	}
	return pspec;
}

static void
add_info (GPtrArray   *infos,
          GParamSpec  *pspec,
          const gchar *label,
          const gchar *hint,
          const gchar *tooltip)
{
	LrgPropertyInfo *info;

	if (pspec == NULL)
		return;
	info = lrg_property_info_new (pspec);
	lrg_property_info_set_label (info, label);
	if (hint != NULL)
		lrg_property_info_set_hint (info, hint);
	if (tooltip != NULL)
		lrg_property_info_set_tooltip (info, tooltip);
	g_ptr_array_add (infos, info);
}

static void
add_real (GPtrArray   *infos,
          GType        type,
          const gchar *name,
          const gchar *label,
          const gchar *hint,
          const gchar *tooltip)
{
	g_autoptr(GTypeClass) klass = g_type_class_ref (type);

	add_info (infos, g_object_class_find_property (G_OBJECT_CLASS (klass), name), label, hint, tooltip);
}

/* "enum-strings:a,b,c" of the layer names (with "(body)" first for
 * shapes). */
static gchar *
layer_hint (EditorPhysSource *src)
{
	g_autoptr(GPtrArray) names = layer_names (src->panel);
	GString             *s = g_string_new ("enum-strings:");
	guint                i;

	if (src->part == PART_SHAPE)
		g_string_append (s, BODY_LAYER_NONE);
	for (i = 0; i < names->len; i++)
	{
		if (i > 0 || src->part == PART_SHAPE)
			g_string_append_c (s, ',');
		g_string_append (s, g_ptr_array_index (names, i));
	}
	return g_string_free (s, FALSE);
}

static GPtrArray *
editor_phys_source_list_properties (LrgPropertySource *source)
{
	EditorPhysSource *src = EDITOR_PHYS_SOURCE (source);
	GPtrArray        *infos = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	GObject          *target = source_target (src);
	g_autofree gchar *layers = NULL;

	if (target == NULL || src->panel == NULL)
		return infos;
	layers = layer_hint (src);

	switch (src->part)
	{
	case PART_BODY:
		add_real (infos, LRG_TYPE_NODE_PHYSICS, "body-kind", "Body", NULL,
		          "Dynamic bodies move under forces, kinematic ones follow the node, static ones never move");
		add_real (infos, LRG_TYPE_NODE_PHYSICS, "mass", "Mass", "range:0.001,10000,0.1", "Mass in kg (dynamic bodies)");
		add_info (infos, string_pspec ("layer"), "Layer", layers, "Collision layer (Project Settings > Physics)");
		add_real (infos, LRG_TYPE_NODE_PHYSICS, "friction", "Friction", "range:0,2,0.01", NULL);
		add_real (infos, LRG_TYPE_NODE_PHYSICS, "restitution", "Bounce", "range:0,1,0.01", "Restitution");
		add_real (infos, LRG_TYPE_NODE_PHYSICS, "linear-damping", "Damping", "range:0,10,0.01", NULL);
		add_real (infos, LRG_TYPE_NODE_PHYSICS, "angular-damping", "Spin damping", "range:0,10,0.01", NULL);
		add_real (infos, LRG_TYPE_NODE_PHYSICS, "gravity-factor", "Gravity", "range:-4,4,0.05", "Gravity factor");
		add_real (infos, LRG_TYPE_NODE_PHYSICS, "ccd", "CCD", NULL, "Continuous collision detection (fast bodies)");
		break;
	case PART_SHAPE:
		{
			LrgCollider3DKind kind = lrg_node_physics_shape_get_kind (LRG_NODE_PHYSICS_SHAPE (target));

			add_real (infos, LRG_TYPE_NODE_PHYSICS_SHAPE, "kind", "Shape", NULL, NULL);
			if (kind == LRG_COLLIDER3D_BOX)
				add_info (infos, vec_pspec ("half-extents"), "Half extents", "vector3", "Half size per local axis");
			if (kind == LRG_COLLIDER3D_SPHERE || kind == LRG_COLLIDER3D_CAPSULE || kind == LRG_COLLIDER3D_CYLINDER)
				add_real (infos, LRG_TYPE_NODE_PHYSICS_SHAPE, "radius", "Radius", "range:0.001,100,0.01", NULL);
			if (kind == LRG_COLLIDER3D_CAPSULE || kind == LRG_COLLIDER3D_CYLINDER)
				add_real (infos, LRG_TYPE_NODE_PHYSICS_SHAPE, "half-height", "Half height", "range:0,100,0.01",
				          kind == LRG_COLLIDER3D_CAPSULE ? "Half the distance between the cap centres"
				                                         : "Half the cylinder's height");
			if (kind == LRG_COLLIDER3D_CONVEX || kind == LRG_COLLIDER3D_MESH)
				add_real (infos, LRG_TYPE_NODE_PHYSICS_SHAPE, "mesh", "Mesh", "asset:model",
				          "Model the shape is built from; empty: the node's own model");
			add_info (infos, vec_pspec ("offset"), "Offset", "vector3", "Centre in the node's local space");
			add_info (infos, vec_pspec ("rotation"), "Rotation", "vector3", "Euler rotation in degrees");
			add_real (infos, LRG_TYPE_NODE_PHYSICS_SHAPE, "trigger", "Trigger", NULL,
			          "Reports overlaps without colliding");
			add_info (infos, string_pspec ("layer"), "Layer", layers, "(body): the body's layer");
			add_real (infos, LRG_TYPE_NODE_PHYSICS_SHAPE, "friction", "Friction", "range:-1,2,0.01",
			          "-1: the body's friction");
			add_real (infos, LRG_TYPE_NODE_PHYSICS_SHAPE, "restitution", "Bounce", "range:-1,1,0.01",
			          "-1: the body's restitution");
		}
		break;
	case PART_JOINT:
	default:
		add_real (infos, LRG_TYPE_NODE_PHYSICS_JOINT, "kind", "Joint", NULL, NULL);
		add_info (infos, string_pspec ("target"), "Target", "node-ref",
		          "The node this one is jointed to; empty: the world");
		add_info (infos, vec_pspec ("anchor"), "Anchor", "vector3", "Pivot in the node's local space");
		add_info (infos, vec_pspec ("axis"), "Axis", "vector3", "Hinge / slider / twist axis, node space");
		add_real (infos, LRG_TYPE_NODE_PHYSICS_JOINT, "limits-enabled", "Limits", NULL, NULL);
		add_real (infos, LRG_TYPE_NODE_PHYSICS_JOINT, "limit-min", "Limit min", "range:-10,10,0.01",
		          "Radians (hinge, cone, swing-twist) or metres (slider, distance)");
		add_real (infos, LRG_TYPE_NODE_PHYSICS_JOINT, "limit-max", "Limit max", "range:-10,10,0.01",
		          "Radians (hinge, cone, swing-twist) or metres (slider, distance)");
		add_real (infos, LRG_TYPE_NODE_PHYSICS_JOINT, "spring-frequency", "Spring Hz", "range:0,60,0.1",
		          "0: rigid");
		add_real (infos, LRG_TYPE_NODE_PHYSICS_JOINT, "spring-damping", "Spring damp", "range:0,4,0.01", NULL);
		add_real (infos, LRG_TYPE_NODE_PHYSICS_JOINT, "motor-velocity", "Motor speed", "range:-50,50,0.1", NULL);
		add_real (infos, LRG_TYPE_NODE_PHYSICS_JOINT, "motor-max-force", "Motor force", "range:0,100000,1",
		          "0: no motor");
		add_real (infos, LRG_TYPE_NODE_PHYSICS_JOINT, "break-force", "Break force", "range:0,1000000,10",
		          "0: unbreakable");
		break;
	}
	return infos;
}

static gboolean
editor_phys_source_get_value (LrgPropertySource *source,
                              const gchar       *name,
                              GValue            *value)
{
	EditorPhysSource *src = EDITOR_PHYS_SOURCE (source);
	GObject          *target = source_target (src);

	if (target == NULL || src->panel == NULL)
		return FALSE;
	return field_get (src->panel, src->part, target, name, value);
}

static gboolean
editor_phys_source_set_value (LrgPropertySource *source,
                              const gchar       *name,
                              const GValue      *value)
{
	EditorPhysSource      *src = EDITOR_PHYS_SOURCE (source);
	LrgEditorPhysicsPanel *panel = src->panel;
	g_autoptr(GError)      error = NULL;
	guint                  index;

	if (panel == NULL || panel->node == NULL || source_target (src) == NULL)
		return FALSE;
	index = (src->part == PART_SHAPE) ? (guint) panel->sel_shape : (src->part == PART_JOINT) ? (guint) panel->sel_joint : 0u;
	if (!set_field (panel, NULL, src->part, index, name, value, TRUE, &error))
	{
		if (panel->app != NULL)
			editor_app_set_status (panel->app, "Physics: %s", error->message);
		return FALSE;
	}
	lrg_property_source_emit_changed (source, name);
	return TRUE;
}

static void
editor_phys_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = editor_phys_source_list_properties;
	iface->get_value = editor_phys_source_get_value;
	iface->set_value = editor_phys_source_set_value;
}

static void
editor_phys_source_finalize (GObject *object)
{
	EditorPhysSource *self = EDITOR_PHYS_SOURCE (object);

	g_clear_weak_pointer (&self->panel);
	G_OBJECT_CLASS (editor_phys_source_parent_class)->finalize (object);
}

static void
editor_phys_source_class_init (EditorPhysSourceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = editor_phys_source_finalize;
}

static void
editor_phys_source_init (EditorPhysSource *self)
{
}

static EditorPhysSource *
editor_phys_source_new (LrgEditorPhysicsPanel *panel,
                        Part                   part)
{
	EditorPhysSource *src = g_object_new (EDITOR_TYPE_PHYS_SOURCE, NULL);

	g_set_weak_pointer (&src->panel, panel);
	src->part = part;
	return src;
}

/* ==========================================================================
 * Grids: gestures and node references
 * ========================================================================== */

static void
on_edit_begin (LrgPropertyGrid *grid,
               const gchar     *name,
               guint            gesture_id,
               gpointer         user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);

	self->in_gesture = TRUE;
	self->gesture_first = TRUE;
}

static void
on_edit_end (LrgPropertyGrid *grid,
             const gchar     *name,
             guint            gesture_id,
             gboolean         commit_,
             gpointer         user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);

	self->in_gesture = FALSE;
	self->gesture_first = FALSE;
	sync_widgets (self);
}

/* Collects node paths depth first. */
static void
collect_paths (LrgNode   *node,
               LrgNode   *skip,
               GPtrArray *out)
{
	GPtrArray *kids = lrg_node_get_children (node);
	guint      i;

	if (node != skip)
		g_ptr_array_add (out, lrg_node_get_path (node));
	for (i = 0; kids != NULL && i < kids->len; i++)
		collect_paths (g_ptr_array_index (kids, i), skip, out);
}

/* The joint target drop-down: the world, then every other node. */
static gchar **
reference_func (LrgPropertyGrid *grid,
                const gchar     *name,
                const gchar     *hint,
                gpointer         user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);
	LrgLevel              *level = current_level (self);
	LrgNode               *root = (level != NULL) ? lrg_level_get_root (level) : NULL;
	GPtrArray             *roots = (root != NULL) ? lrg_node_get_children (root) : NULL;
	g_autoptr(GPtrArray)   out = g_ptr_array_new ();
	guint                  i;

	g_ptr_array_add (out, g_strdup (""));
	for (i = 0; roots != NULL && i < roots->len; i++)
		collect_paths (g_ptr_array_index (roots, i), self->node, out);
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (g_steal_pointer (&out), FALSE);
}

/* ==========================================================================
 * Shape and joint lists (painted rows with up / down / remove buttons)
 * ========================================================================== */

static guint
list_count (LrgEditorPhysicsPanel *self,
            gboolean               joints)
{
	LrgNodePhysics *phys = (self->node != NULL) ? lrg_node_get_physics (self->node) : NULL;

	if (phys == NULL)
		return 0;
	return joints ? lrg_node_physics_get_joints (phys)->len : lrg_node_physics_get_shapes (phys)->len;
}

static gchar *
shape_row_text (LrgNodePhysicsShape *shape,
                guint                index)
{
	GString     *s = g_string_new (NULL);
	gfloat       v[3];
	const gchar *layer;

	g_string_append_printf (s, "%u  %s", index, shape_kind_label (lrg_node_physics_shape_get_kind (shape)));
	switch (lrg_node_physics_shape_get_kind (shape))
	{
	case LRG_COLLIDER3D_BOX:
		lrg_node_physics_shape_get_half_extents (shape, v);
		g_string_append_printf (s, "  %.2f x %.2f x %.2f", (gdouble) v[0] * 2.0, (gdouble) v[1] * 2.0,
		                        (gdouble) v[2] * 2.0);
		break;
	case LRG_COLLIDER3D_SPHERE:
		g_string_append_printf (s, "  r %.2f", (gdouble) lrg_node_physics_shape_get_radius (shape));
		break;
	case LRG_COLLIDER3D_CAPSULE:
	case LRG_COLLIDER3D_CYLINDER:
		g_string_append_printf (s, "  r %.2f  h %.2f", (gdouble) lrg_node_physics_shape_get_radius (shape),
		                        (gdouble) lrg_node_physics_shape_get_half_height (shape) * 2.0);
		break;
	default:
		if (lrg_node_physics_shape_get_mesh (shape) != NULL)
			g_string_append_printf (s, "  %s", lrg_node_physics_shape_get_mesh (shape));
		break;
	}
	if (lrg_node_physics_shape_get_trigger (shape))
		g_string_append (s, "  trigger");
	layer = lrg_node_physics_shape_get_layer (shape);
	if (layer != NULL)
		g_string_append_printf (s, "  [%s]", layer);
	return g_string_free (s, FALSE);
}

static gchar *
joint_row_text (LrgEditorPhysicsPanel *self,
                LrgNodePhysicsJoint   *joint,
                guint                  index)
{
	g_autofree gchar *target = node_path_for_guid (self, lrg_node_physics_joint_get_target (joint));
	gfloat            mn, mx;
	GString          *s = g_string_new (NULL);

	g_string_append_printf (s, "%u  %s -> %s", index, joint_kind_label (lrg_node_physics_joint_get_kind (joint)),
	                        *target != '\0' ? target : "world");
	if (lrg_node_physics_joint_get_limits (joint, &mn, &mx))
		g_string_append_printf (s, "  [%.2f, %.2f]", (gdouble) mn, (gdouble) mx);
	if (lrg_node_physics_joint_get_break_force (joint) > 0.0f)
		g_string_append (s, "  breakable");
	return g_string_free (s, FALSE);
}

static void
list_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);
	gboolean               joints = widget == LRG_CUSTOM_WIDGET (self->joint_list);
	guint                  n = list_count (self, joints);

	size->width = 160.0f;
	size->height = (gfloat) MAX (n, 1u) * LIST_ROW_H + 2.0f;
}

/* The buttons of a row: 0 up, 1 down, 2 remove (joints: remove only). */
static guint
row_buttons (gboolean joints)
{
	return joints ? 1u : 3u;
}

static void
list_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gboolean               joints = widget == LRG_CUSTOM_WIDGET (self->joint_list);
	LrgNodePhysics        *phys = (self->node != NULL) ? lrg_node_get_physics (self->node) : NULL;
	guint                  n = list_count (self, joints), i, b;
	gint                   sel = joints ? self->sel_joint : self->sel_shape;
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat                 radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlFont               *font = lrg_theme_get_ui_font (theme);
	const GrlColor        *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	const GrlColor        *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	GrlColor               track = *lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);

	/* The well */
	track.a = (guint8) (track.a / 2);
	lrg_paint_list_rect (list, x, y, width, height, radius, &track);
	if (n == 0)
	{
		lrg_paint_list_text (list, font, joints ? "No joints" : "No shapes: the body will not collide",
		                     x + 10.0f, roundf (y + (LIST_ROW_H - fs) * 0.5f), fs, 0.0f, muted);
		return;
	}

	for (i = 0; i < n; i++)
	{
		gfloat            ry = y + 1.0f + (gfloat) i * LIST_ROW_H;
		g_autofree gchar *label = NULL;
		GrlColor          dot;

		if ((gint) i == sel)
			lrg_paint_list_rect (list, x + 2.0f, ry, width - 4.0f, LIST_ROW_H - 1.0f, radius,
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION));
		if (joints)
		{
			dot.r = 0xfa; dot.g = 0xb3; dot.b = 0x87; dot.a = 0xff;
			label = joint_row_text (self, joint_at (phys, (gint) i), i);
		}
		else
		{
			LrgNodePhysicsShape *shape = shape_at (phys, (gint) i);

			if (lrg_node_physics_shape_get_trigger (shape))
			{
				dot.r = 240; dot.g = 220; dot.b = 60; dot.a = 255;
			}
			else
				dot = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SUCCESS);
			label = shape_row_text (shape, i);
		}
		lrg_paint_list_circle (list, x + 11.0f, ry + LIST_ROW_H * 0.5f, 3.5f, &dot);
		lrg_paint_list_push_clip (list, x + 20.0f, ry, MAX (width - 20.0f - LIST_BTN_W * row_buttons (joints) - 4.0f, 0.0f),
		                          LIST_ROW_H);
		lrg_paint_list_text (list, font, label, x + 20.0f, roundf (ry + (LIST_ROW_H - fs) * 0.5f - 1.0f), fs, 0.0f,
		                     text);
		lrg_paint_list_pop_clip (list);

		/* Buttons, right aligned */
		for (b = 0; b < row_buttons (joints); b++)
		{
			gfloat  bx = x + width - LIST_BTN_W * (gfloat) (row_buttons (joints) - b) - 2.0f;
			gfloat  is = 12.0f;
			LrgIcon icon = joints ? LRG_ICON_TRASH : (b == 0 ? LRG_ICON_CHEVRON_UP : b == 1 ? LRG_ICON_CHEVRON_DOWN
			                                                                                : LRG_ICON_TRASH);
			gboolean disabled = !joints && ((b == 0 && i == 0) || (b == 1 && i + 1 == n));

			lrg_paint_list_icon (list, icon, bx + (LIST_BTN_W - is) * 0.5f, ry + (LIST_ROW_H - is) * 0.5f, is, 0.0f,
			                     disabled ? muted : (icon == LRG_ICON_TRASH ? lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER)
			                                                                : text));
		}
	}
}

static gboolean
list_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);
	gboolean               joints = widget == LRG_CUSTOM_WIDGET (self->joint_list);
	gfloat                 x = lrg_widget_get_world_x (LRG_WIDGET (widget));
	gfloat                 y = lrg_widget_get_world_y (LRG_WIDGET (widget));
	gfloat                 w = lrg_widget_get_width (LRG_WIDGET (widget));
	gfloat                 px, py;
	guint                  n, row, b;

	if (lrg_ui_event_get_event_type (event) != LRG_UI_EVENT_MOUSE_BUTTON_DOWN || lrg_ui_event_get_button (event) != 0)
		return FALSE;
	px = lrg_ui_event_get_x (event);
	py = lrg_ui_event_get_y (event);
	n = list_count (self, joints);
	if (py < y + 1.0f || n == 0)
		return FALSE;
	row = (guint) ((py - y - 1.0f) / LIST_ROW_H);
	if (row >= n)
		return TRUE;

	/* A button? */
	for (b = 0; b < row_buttons (joints); b++)
	{
		gfloat bx = x + w - LIST_BTN_W * (gfloat) (row_buttons (joints) - b) - 2.0f;

		if (px >= bx && px < bx + LIST_BTN_W)
		{
			if (joints)
				lrg_editor_physics_panel_remove_joint (self, NULL, row);
			else if (b == 0 && row > 0)
				lrg_editor_physics_panel_move_shape (self, NULL, row, row - 1);
			else if (b == 1 && row + 1 < n)
				lrg_editor_physics_panel_move_shape (self, NULL, row, row + 1);
			else if (b == 2)
				lrg_editor_physics_panel_remove_shape (self, NULL, row);
			return TRUE;
		}
	}
	if (joints)
		lrg_editor_physics_panel_set_selected_joint (self, (gint) row);
	else
		lrg_editor_physics_panel_set_selected_shape (self, (gint) row);
	return TRUE;
}

/* ==========================================================================
 * Live readout
 * ========================================================================== */

static void
live_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	gfloat    fs = (theme != NULL) ? lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL) : 12.0f;

	size->width = 160.0f;
	size->height = (fs + 6.0f) * (gfloat) LIVE_LINES + 4.0f;
}

static void
live_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);
	LrgTheme              *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	g_autofree gchar      *text = lrg_editor_physics_panel_describe_live (self);
	gfloat                 fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	GrlFont               *font = editor_panel_mono_font (LRG_WIDGET (widget));
	g_auto(GStrv)          lines = NULL;
	guint                  i;

	if (font == NULL)
		font = lrg_theme_get_ui_font (theme);
	if (text == NULL)
	{
		lrg_paint_list_text (list, font, "No live body for this node", x + 4.0f, y + 4.0f, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		return;
	}
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
		lrg_paint_list_text (list, font, lines[i], x + 4.0f, y + 2.0f + (gfloat) i * (fs + 6.0f), fs, 0.0f,
		                     lrg_theme_get_color (theme, i == 2 && strstr (lines[i], "sleeping") != NULL
		                                                 ? LRG_THEME_COLOR_TEXT_MUTED : LRG_THEME_COLOR_TEXT));
}

/* Keeps the readout moving while the game runs. */
static gboolean
live_step (LrgCustomWidget *widget,
           gfloat           delta_time,
           gpointer         user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);

	return self->play != NULL && lrg_editor_play_session_is_playing (self->play);
}

/* ==========================================================================
 * Sync
 * ========================================================================== */

static gchar *
grid_signature (LrgEditorPhysicsPanel *self)
{
	LrgNodePhysics      *phys = (self->node != NULL) ? lrg_node_get_physics (self->node) : NULL;
	LrgNodePhysicsShape *shape = shape_at (phys, self->sel_shape);
	g_autoptr(GPtrArray) names = layer_names (self);

	return g_strdup_printf ("%p/%d/%d/%d/%d/%u", (gpointer) self->node, phys != NULL, self->sel_shape,
	                        shape != NULL ? (gint) lrg_node_physics_shape_get_kind (shape) : -1, self->sel_joint,
	                        names->len);
}

static void
sync_widgets (LrgEditorPhysicsPanel *self)
{
	LrgEditorSelection *sel;
	LrgNode            *node;
	LrgNodePhysics     *phys;
	g_autofree gchar   *sig = NULL;
	gboolean            playing;
	guint               i;

	if (self->editor == NULL)
		return;
	sel = lrg_editor_get_selection (self->editor);
	node = (sel != NULL) ? lrg_editor_selection_get_primary (sel) : NULL;
	phys = (node != NULL) ? lrg_node_get_physics (node) : NULL;
	if (node != self->node)
	{
		/* a new node: its first shape and joint are selected */
		g_set_weak_pointer (&self->node, node);
		self->sel_shape = (phys != NULL && lrg_node_physics_get_shapes (phys)->len > 0) ? 0 : -1;
		self->sel_joint = (phys != NULL && lrg_node_physics_get_joints (phys)->len > 0) ? 0 : -1;
	}

	/* Clamp the list selections (undo can shorten the lists); -1 stays */
	if (self->sel_shape >= 0 && shape_at (phys, self->sel_shape) == NULL)
		self->sel_shape = (phys != NULL) ? (gint) lrg_node_physics_get_shapes (phys)->len - 1 : -1;
	if (self->sel_joint >= 0 && joint_at (phys, self->sel_joint) == NULL)
		self->sel_joint = (phys != NULL) ? (gint) lrg_node_physics_get_joints (phys)->len - 1 : -1;

	/* Header */
	lrg_label_set_text (self->node_label, node != NULL ? lrg_node_get_name (node) : "");
	lrg_widget_set_visible (self->remove_body_chip, phys != NULL);
	lrg_widget_set_visible (self->add_body_chip, node != NULL && phys == NULL);

	/* Empty state or content */
	lrg_widget_set_visible (self->scroll, phys != NULL);
	lrg_widget_set_visible (self->empty, phys == NULL);
	lrg_widget_set_visible (self->hint_none, node == NULL);
	lrg_widget_set_visible (self->hint_2d, node != NULL && lrg_node_get_is_2d (node));
	lrg_widget_set_visible (self->hint_body, node != NULL && !lrg_node_get_is_2d (node));
	lrg_widget_set_visible (self->empty_add, node != NULL);
	lrg_widget_set_visible (LRG_WIDGET (self->empty_kind), node != NULL);

	/* Grids: rebuilt when what they show changed shape, refreshed else */
	sig = grid_signature (self);
	if (g_strcmp0 (sig, self->signature) != 0)
	{
		g_free (self->signature);
		self->signature = g_steal_pointer (&sig);
		for (i = 0; i < 3; i++)
			lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self->src[i]), NULL);
		lrg_property_grid_rebuild (self->body_grid);
		lrg_property_grid_rebuild (self->shape_grid);
		lrg_property_grid_rebuild (self->joint_grid);
	}
	else if (!self->in_gesture)
	{
		lrg_property_grid_refresh (self->body_grid);
		lrg_property_grid_refresh (self->shape_grid);
		lrg_property_grid_refresh (self->joint_grid);
	}
	lrg_widget_set_visible (LRG_WIDGET (self->shape_grid), self->sel_shape >= 0);
	lrg_widget_set_visible (self->fit_row, self->sel_shape >= 0);
	lrg_widget_set_visible (LRG_WIDGET (self->joint_grid), self->sel_joint >= 0);
	lrg_widget_queue_layout (self->shape_list);
	lrg_widget_queue_layout (self->joint_list);

	/* Live readout while playing */
	playing = self->play != NULL && lrg_editor_play_session_is_playing (self->play);
	lrg_widget_set_visible (LRG_WIDGET (self->live_section), playing && phys != NULL);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_changed (LrgEditorPhysicsPanel *self)
{
	if (!self->applying)
		sync_widgets (self);
}

static void
on_level_replaced (LrgEditorPhysicsPanel *self,
                   LrgLevel              *level)
{
	g_clear_weak_pointer (&self->node);
	self->sel_shape = -1;
	self->sel_joint = -1;
	sync_widgets (self);
}

/* ==========================================================================
 * Chips
 * ========================================================================== */

static LrgDynamicsBodyKind
body_kind_from_combo (LrgComboBox *combo)
{
	switch (lrg_combo_box_get_selected (combo))
	{
	case 1:  return LRG_DYNAMICS_BODY_KINEMATIC;
	case 2:  return LRG_DYNAMICS_BODY_STATIC;
	default: return LRG_DYNAMICS_BODY_DYNAMIC;
	}
}

static void
on_add_body (LrgWidget *chip,
             gpointer   user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);

	lrg_editor_physics_panel_add_body (self, NULL, body_kind_from_combo (self->empty_kind), TRUE);
}

static void
on_remove_body (LrgWidget *chip,
                gpointer   user_data)
{
	lrg_editor_physics_panel_remove_body (LRG_EDITOR_PHYSICS_PANEL (user_data), NULL);
}

static void
on_add_shape (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);
	gint                   kind = lrg_combo_box_get_selected (self->shape_kind);

	lrg_editor_physics_panel_add_shape (self, NULL, (LrgCollider3DKind) MAX (kind, 0), TRUE);
}

static void
on_add_joint (LrgWidget *chip,
              gpointer   user_data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);
	gint                   kind = lrg_combo_box_get_selected (self->joint_kind);

	lrg_editor_physics_panel_add_joint (self, NULL, (LrgJointKind) MAX (kind, 0), NULL);
}

static void
on_fit (LrgWidget *chip,
        gpointer   user_data)
{
	static const LrgCollider3DKind kinds[] = {
		LRG_COLLIDER3D_BOX, LRG_COLLIDER3D_SPHERE, LRG_COLLIDER3D_CAPSULE, LRG_COLLIDER3D_CYLINDER,
		LRG_COLLIDER3D_CONVEX, LRG_COLLIDER3D_MESH
	};
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (user_data);
	gint                   i = lrg_combo_box_get_selected (self->fit_kind);
	LrgCollider3DKind      kind = kinds[CLAMP (i, 0, (gint) G_N_ELEMENTS (kinds) - 1)];

	if (self->sel_shape >= 0)
		lrg_editor_physics_panel_fit_shape (self, NULL, (guint) self->sel_shape, kind);
}

/* ==========================================================================
 * IPC: physics.*
 * ========================================================================== */

static JsonNode *
obj_node (JsonObject *o)
{
	JsonNode *n = json_node_new (JSON_NODE_OBJECT);

	json_node_take_object (n, o);
	return n;
}

static LrgEditorPhysicsPanel *
ipc_panel (gpointer   user_data,
           GError   **error)
{
	LrgEditorPhysicsPanel *panel = lrg_editor_physics_panel_find (LRG_EDITOR_APP (user_data));

	if (panel == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "No physics panel");
	return panel;
}

/* "node": GUID or path; default the primary selection. */
static LrgNode *
ipc_node (LrgEditorPhysicsPanel  *self,
          JsonObject             *params,
          GError                **error)
{
	LrgNode *node = NULL;

	if (params != NULL && json_object_has_member (params, "node"))
	{
		const gchar *ref = json_object_get_string_member (params, "node");

		node = (ref != NULL) ? lrg_editor_find_node (self->editor, ref) : NULL;
		if (node == NULL)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No node '%s'",
			             ref != NULL ? ref : "(not a string)");
		return node;
	}
	sync_widgets (self);
	node = self->node;
	if (node == NULL)
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "No node: give \"node\" or select one");
	return node;
}

static JsonArray *
vec_array (const gfloat *v)
{
	JsonArray *a = json_array_new ();
	gint       k;

	for (k = 0; k < 3; k++)
		json_array_add_double_element (a, v[k]);
	return a;
}

static const gchar *
enum_nick (GType type,
           gint  value)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (type);
	GEnumValue           *ev = g_enum_get_value (klass, value);

	return ev != NULL ? ev->value_nick : "unknown";
}

static void
set_nullable_string (JsonObject  *o,
                     const gchar *name,
                     const gchar *value)
{
	if (value != NULL)
		json_object_set_string_member (o, name, value);
	else
		json_object_set_null_member (o, name);
}

/* The physics.get result for @node. */
static JsonNode *
physics_json (LrgEditorPhysicsPanel *self,
              LrgNode               *node)
{
	JsonObject          *o = json_object_new ();
	LrgNodePhysics      *phys = lrg_node_get_physics (node);
	g_autoptr(GPtrArray) names = layer_names (self);
	JsonArray           *a;
	guint                i;

	json_object_set_string_member (o, "node", lrg_node_get_guid (node));
	json_object_set_string_member (o, "name", lrg_node_get_name (node));
	if (phys == NULL)
	{
		json_object_set_null_member (o, "body");
		json_object_set_array_member (o, "shapes", json_array_new ());
		json_object_set_array_member (o, "joints", json_array_new ());
	}
	else
	{
		JsonObject *b = json_object_new ();
		gfloat      lin, ang;

		json_object_set_string_member (b, "body-kind",
		                               enum_nick (LRG_TYPE_DYNAMICS_BODY_KIND, (gint) lrg_node_physics_get_body_kind (phys)));
		json_object_set_double_member (b, "mass", lrg_node_physics_get_mass (phys));
		set_nullable_string (b, "layer", lrg_node_physics_get_layer (phys));
		json_object_set_double_member (b, "friction", lrg_node_physics_get_friction (phys));
		json_object_set_double_member (b, "restitution", lrg_node_physics_get_restitution (phys));
		lrg_node_physics_get_damping (phys, &lin, &ang);
		json_object_set_double_member (b, "linear-damping", lin);
		json_object_set_double_member (b, "angular-damping", ang);
		json_object_set_double_member (b, "gravity-factor", lrg_node_physics_get_gravity_factor (phys));
		json_object_set_boolean_member (b, "ccd", lrg_node_physics_get_ccd (phys));
		json_object_set_object_member (o, "body", b);

		a = json_array_new ();
		for (i = 0; i < lrg_node_physics_get_shapes (phys)->len; i++)
		{
			LrgNodePhysicsShape *shape = shape_at (phys, (gint) i);
			JsonObject          *s = json_object_new ();
			gfloat               v[3];

			json_object_set_int_member (s, "index", i);
			json_object_set_string_member (s, "kind",
			                               enum_nick (LRG_TYPE_COLLIDER3D_KIND, (gint) lrg_node_physics_shape_get_kind (shape)));
			lrg_node_physics_shape_get_half_extents (shape, v);
			json_object_set_array_member (s, "half-extents", vec_array (v));
			json_object_set_double_member (s, "radius", lrg_node_physics_shape_get_radius (shape));
			json_object_set_double_member (s, "half-height", lrg_node_physics_shape_get_half_height (shape));
			lrg_node_physics_shape_get_offset (shape, v);
			json_object_set_array_member (s, "offset", vec_array (v));
			lrg_node_physics_shape_get_rotation (shape, v);
			json_object_set_array_member (s, "rotation", vec_array (v));
			json_object_set_boolean_member (s, "trigger", lrg_node_physics_shape_get_trigger (shape));
			json_object_set_double_member (s, "friction", lrg_node_physics_shape_get_friction (shape));
			json_object_set_double_member (s, "restitution", lrg_node_physics_shape_get_restitution (shape));
			set_nullable_string (s, "layer", lrg_node_physics_shape_get_layer (shape));
			set_nullable_string (s, "mesh", lrg_node_physics_shape_get_mesh (shape));
			json_array_add_object_element (a, s);
		}
		json_object_set_array_member (o, "shapes", a);

		a = json_array_new ();
		for (i = 0; i < lrg_node_physics_get_joints (phys)->len; i++)
		{
			LrgNodePhysicsJoint *joint = joint_at (phys, (gint) i);
			JsonObject          *j = json_object_new ();
			g_autofree gchar    *path = node_path_for_guid (self, lrg_node_physics_joint_get_target (joint));
			gfloat               v[3], f1, f2;
			gboolean             on;

			json_object_set_int_member (j, "index", i);
			json_object_set_string_member (j, "kind",
			                               enum_nick (LRG_TYPE_JOINT_KIND, (gint) lrg_node_physics_joint_get_kind (joint)));
			set_nullable_string (j, "target", lrg_node_physics_joint_get_target (joint));
			json_object_set_string_member (j, "target-name", path);
			lrg_node_physics_joint_get_anchor (joint, v);
			json_object_set_array_member (j, "anchor", vec_array (v));
			lrg_node_physics_joint_get_axis (joint, v);
			json_object_set_array_member (j, "axis", vec_array (v));
			on = lrg_node_physics_joint_get_limits (joint, &f1, &f2);
			json_object_set_boolean_member (j, "limits-enabled", on);
			json_object_set_double_member (j, "limit-min", f1);
			json_object_set_double_member (j, "limit-max", f2);
			lrg_node_physics_joint_get_spring (joint, &f1, &f2);
			json_object_set_double_member (j, "spring-frequency", f1);
			json_object_set_double_member (j, "spring-damping", f2);
			lrg_node_physics_joint_get_motor (joint, &f1, &f2);
			json_object_set_double_member (j, "motor-velocity", f1);
			json_object_set_double_member (j, "motor-max-force", f2);
			json_object_set_double_member (j, "break-force", lrg_node_physics_joint_get_break_force (joint));
			json_array_add_object_element (a, j);
		}
		json_object_set_array_member (o, "joints", a);
	}
	a = json_array_new ();
	for (i = 0; i < names->len; i++)
		json_array_add_string_element (a, g_ptr_array_index (names, i));
	json_object_set_array_member (o, "layers", a);
	return obj_node (o);
}

/* A JSON value as a GValue: arrays of three numbers become GrlVector3. */
static gboolean
json_to_value (JsonNode *node,
               GValue   *out)
{
	if (JSON_NODE_HOLDS_ARRAY (node))
	{
		JsonArray *a = json_node_get_array (node);

		if (json_array_get_length (a) != 3)
			return FALSE;
		g_value_init (out, GRL_TYPE_VECTOR3);
		g_value_take_boxed (out, grl_vector3_new ((gfloat) json_array_get_double_element (a, 0),
		                                          (gfloat) json_array_get_double_element (a, 1),
		                                          (gfloat) json_array_get_double_element (a, 2)));
		return TRUE;
	}
	if (JSON_NODE_HOLDS_NULL (node))
	{
		g_value_init (out, G_TYPE_STRING);
		g_value_set_string (out, "");
		return TRUE;
	}
	if (!JSON_NODE_HOLDS_VALUE (node))
		return FALSE;
	json_node_get_value (node, out);
	return TRUE;
}

/* Applies "values" to one part as ONE history step: all fields go into
 * the same working copy. */
static gboolean
ipc_apply_values (LrgEditorPhysicsPanel  *self,
                  LrgNode                *node,
                  Part                    part,
                  guint                   index,
                  JsonObject             *params,
                  GError                **error)
{
	JsonObject *values;
	GList      *keys, *l;
	GObject    *target;
	Edit        e;
	gboolean    ok = TRUE;

	if (params == NULL || !json_object_has_member (params, "values") ||
	    !JSON_NODE_HOLDS_OBJECT (json_object_get_member (params, "values")))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "Give \"values\": {field: value}");
		return FALSE;
	}
	values = json_object_get_object_member (params, "values");
	edit_begin (&e, node);
	if (e.phys == NULL)
	{
		if (part != PART_BODY)
		{
			edit_abort (&e);
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s has no physics body",
			             lrg_node_get_name (node));
			return FALSE;
		}
		e.phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	}
	target = (part == PART_BODY) ? G_OBJECT (e.phys)
	         : (part == PART_SHAPE) ? (GObject *) shape_at (e.phys, (gint) index)
	                                : (GObject *) joint_at (e.phys, (gint) index);
	if (target == NULL)
	{
		edit_abort (&e);
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No %s %u",
		             part == PART_SHAPE ? "shape" : "joint", index);
		return FALSE;
	}
	keys = json_object_get_members (values);
	for (l = keys; l != NULL && ok; l = l->next)
	{
		g_auto(GValue)    v = G_VALUE_INIT;
		g_autoptr(GError) local = NULL;

		if (!json_to_value (json_object_get_member (values, l->data), &v) ||
		    !field_set (self, part, target, l->data, &v, &local))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "%s: %s",
			             (const gchar *) l->data, local != NULL ? local->message : "bad value");
			ok = FALSE;
		}
	}
	g_list_free (keys);
	if (!ok)
	{
		edit_abort (&e);
		return FALSE;
	}
	edit_commit (self, &e, part == PART_BODY ? "Edit Physics Body" : part == PART_SHAPE ? "Edit Shape" : "Edit Joint",
	             FALSE);
	sync_widgets (self);
	return TRUE;
}

static gboolean
ipc_enum (JsonObject   *params,
          const gchar  *member,
          GType         type,
          gint          fallback,
          gint         *out,
          GError      **error)
{
	const gchar          *s;
	g_autoptr(GEnumClass) klass = NULL;
	GEnumValue           *ev;

	*out = fallback;
	if (params == NULL || !json_object_has_member (params, member))
		return TRUE;
	if (json_node_get_value_type (json_object_get_member (params, member)) == G_TYPE_INT64)
	{
		*out = (gint) json_object_get_int_member (params, member);
		return TRUE;
	}
	s = json_object_get_string_member (params, member);
	klass = g_type_class_ref (type);
	ev = (s != NULL) ? g_enum_get_value_by_nick (klass, s) : NULL;
	if (ev == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Unknown %s '%s'", member,
		             s != NULL ? s : "(not a string)");
		return FALSE;
	}
	*out = ev->value;
	return TRUE;
}

static gboolean
ipc_index (JsonObject  *params,
           const gchar *member,
           guint       *out,
           GError     **error)
{
	if (params == NULL || !json_object_has_member (params, member))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "Give \"%s\"", member);
		return FALSE;
	}
	*out = (guint) MAX (json_object_get_int_member (params, member), 0);
	return TRUE;
}

static gboolean
ipc_bool (JsonObject  *params,
          const gchar *member,
          gboolean     fallback)
{
	if (params == NULL || !json_object_has_member (params, member))
		return fallback;
	return json_object_get_boolean_member (params, member);
}

#define IPC_PROLOGUE                                                          \
	LrgEditorPhysicsPanel *self = ipc_panel (user_data, error);              \
	LrgNode               *node;                                             \
	if (self == NULL)                                                        \
		return NULL;                                                         \
	node = ipc_node (self, params, error);                                   \
	if (node == NULL)                                                        \
		return NULL

/* Adds a shape or joint and applies "values" to it, as one step. */
static gint
ipc_add_with_values (LrgEditorPhysicsPanel  *self,
                     LrgNode                *node,
                     Part                    part,
                     gint                    kind,
                     const gchar            *target,
                     JsonObject             *params,
                     GError                **error)
{
	JsonObject *values = json_object_get_object_member (params, "values");
	GList      *keys, *l;
	GObject    *item;
	gint        index;
	Edit        e;

	if (values == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS,
		             "\"values\" must be an object");
		return -1;
	}
	edit_begin (&e, node);
	if (e.phys == NULL)
		e.phys = lrg_node_physics_new (LRG_DYNAMICS_BODY_DYNAMIC);
	if (part == PART_SHAPE)
	{
		LrgNodePhysicsShape *shape = make_shape (self, node, (LrgCollider3DKind) kind, ipc_bool (params, "fit", FALSE));

		lrg_node_physics_add_shape (e.phys, shape);
		item = G_OBJECT (shape);
		g_object_unref (shape);
		index = (gint) lrg_node_physics_get_shapes (e.phys)->len - 1;
	}
	else
	{
		LrgNodePhysicsJoint *joint = lrg_node_physics_joint_new ((LrgJointKind) kind);
		g_auto(GValue)       tv = G_VALUE_INIT;

		lrg_node_physics_add_joint (e.phys, joint);
		item = G_OBJECT (joint);
		g_object_unref (joint);
		index = (gint) lrg_node_physics_get_joints (e.phys)->len - 1;
		g_value_init (&tv, G_TYPE_STRING);
		g_value_set_string (&tv, target != NULL ? target : "");
		if (!field_set (self, PART_JOINT, item, "target", &tv, NULL) ||
		    (target != NULL && lrg_editor_find_node (self->editor, target) == node))
		{
			edit_abort (&e);
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No target node '%s'",
			             target != NULL ? target : "");
			return -1;
		}
	}
	keys = json_object_get_members (values);
	for (l = keys; l != NULL; l = l->next)
	{
		g_auto(GValue)    v = G_VALUE_INIT;
		g_autoptr(GError) local = NULL;

		if (!json_to_value (json_object_get_member (values, l->data), &v) ||
		    !field_set (self, part, item, l->data, &v, &local))
		{
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_INVALID_PARAMS, "%s: %s",
			             (const gchar *) l->data, local != NULL ? local->message : "bad value");
			g_list_free (keys);
			edit_abort (&e);
			return -1;
		}
	}
	g_list_free (keys);
	edit_commit (self, &e, part == PART_SHAPE ? "Add Shape" : "Add Joint", FALSE);
	if (node == self->node)
	{
		if (part == PART_SHAPE)
			self->sel_shape = index;
		else
			self->sel_joint = index;
	}
	sync_widgets (self);
	return index;
}

static JsonNode *
m_get (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	IPC_PROLOGUE;
	return physics_json (self, node);
}

static JsonNode *
m_add_body (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	const gchar *shape = (params != NULL && json_object_has_member (params, "shape"))
	                     ? json_object_get_string_member (params, "shape") : "auto";
	gint         kind;
	IPC_PROLOGUE;

	if (!ipc_enum (params, "kind", LRG_TYPE_DYNAMICS_BODY_KIND, LRG_DYNAMICS_BODY_DYNAMIC, &kind, error))
		return NULL;
	if (lrg_node_get_physics (node) != NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "%s already has a body",
		             lrg_node_get_name (node));
		return NULL;
	}
	if (g_strcmp0 (shape, "auto") == 0)
		lrg_editor_physics_panel_add_body (self, node, (LrgDynamicsBodyKind) kind, TRUE);
	else
	{
		gint skind = -1;

		if (g_strcmp0 (shape, "none") != 0 &&
		    !ipc_enum (params, "shape", LRG_TYPE_COLLIDER3D_KIND, LRG_COLLIDER3D_BOX, &skind, error))
			return NULL;
		/* body and fitted shape in one step */
		{
			Edit e;

			edit_begin (&e, node);
			e.phys = lrg_node_physics_new ((LrgDynamicsBodyKind) kind);
			if (skind >= 0)
			{
				g_autoptr(LrgNodePhysicsShape) s = make_shape (self, node, (LrgCollider3DKind) skind, TRUE);

				lrg_node_physics_add_shape (e.phys, s);
			}
			edit_commit (self, &e, "Add Physics Body", FALSE);
			sync_widgets (self);
		}
	}
	return physics_json (self, node);
}

static JsonNode *
m_remove_body (LrgEditorServer  *server,
               JsonObject       *params,
               gpointer          user_data,
               GError          **error)
{
	IPC_PROLOGUE;
	lrg_editor_physics_panel_remove_body (self, node);
	return physics_json (self, node);
}

static JsonNode *
m_set_body (LrgEditorServer  *server,
            JsonObject       *params,
            gpointer          user_data,
            GError          **error)
{
	IPC_PROLOGUE;
	if (!ipc_apply_values (self, node, PART_BODY, 0, params, error))
		return NULL;
	return physics_json (self, node);
}

static JsonNode *
with_index (JsonNode *result,
            gint      index)
{
	json_object_set_int_member (json_node_get_object (result), "index", index);
	return result;
}

static JsonNode *
m_add_shape (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	gint index, kind;
	IPC_PROLOGUE;

	if (!ipc_enum (params, "kind", LRG_TYPE_COLLIDER3D_KIND, LRG_COLLIDER3D_BOX, &kind, error))
		return NULL;
	if (params != NULL && json_object_has_member (params, "values"))
	{
		/* the shape and its values as one step: add, then edit the same
		 * working copy through ipc_apply_values() would make two */
		index = ipc_add_with_values (self, node, PART_SHAPE, kind, NULL, params, error);
		if (index < 0)
			return NULL;
	}
	else
		index = lrg_editor_physics_panel_add_shape (self, node, (LrgCollider3DKind) kind, ipc_bool (params, "fit", FALSE));
	if (index < 0)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_FAILED, "Could not add a shape");
		return NULL;
	}
	return with_index (physics_json (self, node), index);
}

static JsonNode *
m_remove_shape (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	guint index;
	IPC_PROLOGUE;

	if (!ipc_index (params, "index", &index, error))
		return NULL;
	if (!lrg_editor_physics_panel_remove_shape (self, node, index))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No shape %u", index);
		return NULL;
	}
	return physics_json (self, node);
}

static JsonNode *
m_set_shape (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	guint index;
	IPC_PROLOGUE;

	if (!ipc_index (params, "index", &index, error) || !ipc_apply_values (self, node, PART_SHAPE, index, params, error))
		return NULL;
	return physics_json (self, node);
}

static JsonNode *
m_move_shape (LrgEditorServer  *server,
              JsonObject       *params,
              gpointer          user_data,
              GError          **error)
{
	guint index, to;
	IPC_PROLOGUE;

	if (!ipc_index (params, "index", &index, error) || !ipc_index (params, "to", &to, error))
		return NULL;
	if (shape_at (lrg_node_get_physics (node), (gint) index) == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No shape %u", index);
		return NULL;
	}
	lrg_editor_physics_panel_move_shape (self, node, index, to);
	return physics_json (self, node);
}

static JsonNode *
m_add_joint (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	const gchar *target = (params != NULL && json_object_has_member (params, "target"))
	                      ? json_object_get_string_member (params, "target") : NULL;
	gint         index, kind;
	IPC_PROLOGUE;

	if (!ipc_enum (params, "kind", LRG_TYPE_JOINT_KIND, LRG_JOINT_FIXED, &kind, error))
		return NULL;
	if (params != NULL && json_object_has_member (params, "values"))
		index = ipc_add_with_values (self, node, PART_JOINT, kind, target, params, error);
	else
	{
		index = lrg_editor_physics_panel_add_joint (self, node, (LrgJointKind) kind, target);
		if (index < 0)
			g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No target node '%s'",
			             target != NULL ? target : "");
	}
	if (index < 0)
		return NULL;
	return with_index (physics_json (self, node), index);
}

static JsonNode *
m_remove_joint (LrgEditorServer  *server,
                JsonObject       *params,
                gpointer          user_data,
                GError          **error)
{
	guint index;
	IPC_PROLOGUE;

	if (!ipc_index (params, "index", &index, error))
		return NULL;
	if (!lrg_editor_physics_panel_remove_joint (self, node, index))
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No joint %u", index);
		return NULL;
	}
	return physics_json (self, node);
}

static JsonNode *
m_set_joint (LrgEditorServer  *server,
             JsonObject       *params,
             gpointer          user_data,
             GError          **error)
{
	guint index;
	IPC_PROLOGUE;

	if (!ipc_index (params, "index", &index, error) || !ipc_apply_values (self, node, PART_JOINT, index, params, error))
		return NULL;
	return physics_json (self, node);
}

static JsonNode *
m_fit (LrgEditorServer  *server,
       JsonObject       *params,
       gpointer          user_data,
       GError          **error)
{
	LrgNodePhysicsShape *shape;
	guint                index = 0;
	gint                 kind;
	IPC_PROLOGUE;

	if (params != NULL && json_object_has_member (params, "index"))
		index = (guint) MAX (json_object_get_int_member (params, "index"), 0);
	shape = shape_at (lrg_node_get_physics (node), (gint) index);
	if (shape == NULL)
	{
		g_set_error (error, LRG_EDITOR_SERVER_ERROR, LRG_EDITOR_SERVER_ERROR_NOT_FOUND, "No shape %u", index);
		return NULL;
	}
	if (!ipc_enum (params, "kind", LRG_TYPE_COLLIDER3D_KIND, (gint) lrg_node_physics_shape_get_kind (shape), &kind,
	               error))
		return NULL;
	lrg_editor_physics_panel_fit_shape (self, node, index, (LrgCollider3DKind) kind);
	return physics_json (self, node);
}

static JsonNode *
m_state (LrgEditorServer  *server,
         JsonObject       *params,
         gpointer          user_data,
         GError          **error)
{
	JsonObject *o, *b;
	IPC_PROLOGUE;

	o = json_object_new ();
	json_object_set_string_member (o, "node", lrg_node_get_guid (node));
	json_object_set_boolean_member (o, "playing", self->play != NULL && lrg_editor_play_session_is_playing (self->play));
	json_object_set_boolean_member (o, "has-body", lrg_node_get_physics (node) != NULL);
	b = json_object_new ();
	if (live_state (self, node, b))
		json_object_set_object_member (o, "body", b);
	else
	{
		json_object_unref (b);
		json_object_set_null_member (o, "body");
	}
	return obj_node (o);
}

static JsonNode *
m_show (LrgEditorServer  *server,
        JsonObject       *params,
        gpointer          user_data,
        GError          **error)
{
	LrgEditorApp *app = LRG_EDITOR_APP (user_data);
	JsonObject   *o;

	editor_app_show_panel (app, LRG_EDITOR_PHYSICS_PANEL_ID);
	o = json_object_new ();
	json_object_set_string_member (o, "panel", LRG_EDITOR_PHYSICS_PANEL_ID);
	json_object_set_boolean_member (o, "shown", editor_app_panel_is_shown (app, LRG_EDITOR_PHYSICS_PANEL_ID));
	return obj_node (o);
}

/* ==========================================================================
 * Actions
 * ========================================================================== */

static void
a_show (gpointer data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (data);

	if (self->app != NULL)
		editor_app_show_panel (self->app, LRG_EDITOR_PHYSICS_PANEL_ID);
}

static void
a_add_body (gpointer data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (data);

	sync_widgets (self);
	lrg_editor_physics_panel_add_body (self, NULL, LRG_DYNAMICS_BODY_DYNAMIC, TRUE);
	a_show (data);
}

static void
a_remove_body (gpointer data)
{
	lrg_editor_physics_panel_remove_body (LRG_EDITOR_PHYSICS_PANEL (data), NULL);
}

static void
a_add_shape (gpointer data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (data);

	sync_widgets (self);
	lrg_editor_physics_panel_add_shape (self, NULL, LRG_COLLIDER3D_BOX, TRUE);
}

static void
a_fit (gpointer data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (data);
	LrgNodePhysicsShape   *shape;

	sync_widgets (self);
	shape = shape_at (self->node != NULL ? lrg_node_get_physics (self->node) : NULL, self->sel_shape);
	if (shape != NULL)
		lrg_editor_physics_panel_fit_shape (self, NULL, (guint) self->sel_shape, lrg_node_physics_shape_get_kind (shape));
}

static gboolean
has_node (gpointer data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (data);
	LrgEditorSelection    *sel = (self->editor != NULL) ? lrg_editor_get_selection (self->editor) : NULL;

	return sel != NULL && lrg_editor_selection_get_primary (sel) != NULL;
}

static gboolean
can_add_body (gpointer data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (data);
	LrgEditorSelection    *sel = (self->editor != NULL) ? lrg_editor_get_selection (self->editor) : NULL;
	LrgNode               *node = (sel != NULL) ? lrg_editor_selection_get_primary (sel) : NULL;

	return node != NULL && lrg_node_get_physics (node) == NULL;
}

static gboolean
has_body (gpointer data)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (data);
	LrgEditorSelection    *sel = (self->editor != NULL) ? lrg_editor_get_selection (self->editor) : NULL;
	LrgNode               *node = (sel != NULL) ? lrg_editor_selection_get_primary (sel) : NULL;

	return node != NULL && lrg_node_get_physics (node) != NULL;
}

/* ==========================================================================
 * GObject / widget
 * ========================================================================== */

static void
lrg_editor_physics_panel_layout_children (LrgContainer *container)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (container);

	editor_panel_fit_toolbar (self->header_row, lrg_widget_get_width (LRG_WIDGET (self)) - 20.0f);
	editor_panel_place_column (container, self->top, self->scroll, NULL);
	editor_panel_place_column (container, self->top, self->empty, NULL);
}

static void
lrg_editor_physics_panel_dispose (GObject *object)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (object);
	guint                  i;

	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	if (self->selection != NULL)
		g_signal_handlers_disconnect_by_data (self->selection, self);
	if (self->play != NULL)
		g_signal_handlers_disconnect_by_data (self->play, self);
	if (self->app != NULL && g_object_get_data (G_OBJECT (self->app), PANEL_KEY) == self)
		g_object_set_data (G_OBJECT (self->app), PANEL_KEY, NULL);
	g_clear_object (&self->editor);
	g_clear_object (&self->selection);
	g_clear_object (&self->play);
	g_clear_weak_pointer (&self->app);
	g_clear_weak_pointer (&self->node);
	for (i = 0; i < 3; i++)
		g_clear_object (&self->src[i]);
	G_OBJECT_CLASS (lrg_editor_physics_panel_parent_class)->dispose (object);
}

static void
lrg_editor_physics_panel_finalize (GObject *object)
{
	LrgEditorPhysicsPanel *self = LRG_EDITOR_PHYSICS_PANEL (object);

	g_free (self->signature);
	G_OBJECT_CLASS (lrg_editor_physics_panel_parent_class)->finalize (object);
}

static void
lrg_editor_physics_panel_class_init (LrgEditorPhysicsPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_physics_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_physics_panel_finalize;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_physics_panel_layout_children;
}

static gpointer
add_to (gpointer box,
        gpointer child)
{
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (child));
	g_object_unref (child);
	return child;
}

static LrgComboBox *
make_combo (const gchar         *name,
            const gchar * const *labels,
            guint                n,
            gint                 selected)
{
	LrgComboBox *combo = lrg_combo_box_new ();
	guint        i;

	for (i = 0; i < n; i++)
		lrg_combo_box_append (combo, labels[i]);
	lrg_combo_box_set_selected (combo, selected);
	lrg_widget_set_name (LRG_WIDGET (combo), name);
	lrg_widget_set_valign (LRG_WIDGET (combo), LRG_ALIGN_CENTER);
	lrg_widget_set_max_size (LRG_WIDGET (combo), 128.0f, 0.0f);
	return combo;
}

static LrgPropertyGrid *
make_grid (LrgEditorPhysicsPanel *self,
           const gchar           *name,
           Part                   part)
{
	LrgPropertyGrid *grid = lrg_property_grid_new ();

	lrg_widget_set_name (LRG_WIDGET (grid), name);
	lrg_property_grid_set_label_width (grid, 112.0f);
	g_signal_connect (grid, "edit-begin", G_CALLBACK (on_edit_begin), self);
	g_signal_connect (grid, "edit-end", G_CALLBACK (on_edit_end), self);
	lrg_property_grid_set_source (grid, LRG_PROPERTY_SOURCE (self->src[part]));
	return grid;
}

/* A sub heading row: a label, then the given widgets right aligned. */
static LrgWidget *
heading_row (const gchar *name,
             const gchar *text)
{
	LrgWidget *row = LRG_WIDGET (lrg_hbox_new ());
	LrgWidget *label = LRG_WIDGET (lrg_label_new (text));

	lrg_widget_set_name (row, name);
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	lrg_widget_add_class (label, "heading");
	lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (label, 1.0f);
	add_to (row, label);
	return row;
}

static void
lrg_editor_physics_panel_init (LrgEditorPhysicsPanel *self)
{
	static const LrgCustomWidgetFuncs list_funcs = { list_measure, list_paint, NULL, list_event, { NULL } };
	static const LrgCustomWidgetFuncs live_funcs = { live_measure, live_paint, live_step, NULL, { NULL } };
	static const gchar * const body_kinds[] = { "Dynamic", "Kinematic", "Static" };
	static const gchar * const shape_kinds[] = { "Sphere", "Capsule", "Cylinder", "Box", "Convex", "Mesh", "Plane" };
	static const gchar * const joint_kinds[] = { "Fixed", "Point", "Hinge", "Slider", "Distance", "Cone",
	                                             "Swing-twist", "Six-DOF" };
	static const struct { const gchar *label; LrgCollider3DKind kind; } fits[] = {
		{ "Box", LRG_COLLIDER3D_BOX },
		{ "Sphere", LRG_COLLIDER3D_SPHERE },
		{ "Capsule", LRG_COLLIDER3D_CAPSULE },
		{ "Cylinder", LRG_COLLIDER3D_CYLINDER },
		{ "Convex hull", LRG_COLLIDER3D_CONVEX },
		{ "Triangle mesh", LRG_COLLIDER3D_MESH },
	};
	const LrgInsets pad = { 8.0f, 6.0f, 8.0f, 4.0f };
	LrgWidget      *row;
	LrgWidget      *chip;
	LrgWidget      *label;
	guint           i;

	self->sel_shape = -1;
	self->sel_joint = -1;
	self->src[PART_BODY] = editor_phys_source_new (self, PART_BODY);
	self->src[PART_SHAPE] = editor_phys_source_new (self, PART_SHAPE);
	self->src[PART_JOINT] = editor_phys_source_new (self, PART_JOINT);

	/* Header: title, node name, Add / Remove body */
	self->top = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->top, "physics-top");
	lrg_container_set_spacing (LRG_CONTAINER (self->top), 6.0f);
	lrg_widget_set_margin (self->top, &pad);
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "physics-header");
	self->header_row = row;
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	self->title = LRG_WIDGET (lrg_label_new ("Physics"));
	lrg_widget_set_name (self->title, "physics-title");
	lrg_widget_add_class (self->title, "heading");
	lrg_widget_set_valign (self->title, LRG_ALIGN_CENTER);
	add_to (row, self->title);
	self->node_label = lrg_label_new ("");
	lrg_widget_set_name (LRG_WIDGET (self->node_label), "physics-node");
	lrg_widget_add_class (LRG_WIDGET (self->node_label), "dim");
	lrg_widget_set_valign (LRG_WIDGET (self->node_label), LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (LRG_WIDGET (self->node_label), 1.0f);
	add_to (row, self->node_label);
	self->add_body_chip = editor_chip_new ("physics-add-body", LRG_ICON_PLUS, "Add Body", on_add_body, self);
	editor_chip_set_tint (self->add_body_chip, LRG_THEME_COLOR_ACCENT);
	add_to (row, self->add_body_chip);
	self->remove_body_chip = editor_chip_new ("physics-remove-body", LRG_ICON_TRASH, NULL, on_remove_body, self);
	editor_chip_set_tint (self->remove_body_chip, LRG_THEME_COLOR_DANGER);
	add_to (row, self->remove_body_chip);
	add_to (self->top, row);
	lrg_container_add_child (LRG_CONTAINER (self), self->top);
	g_object_unref (self->top);

	/* Content */
	self->content = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->content, "physics-content");
	lrg_container_set_spacing (LRG_CONTAINER (self->content), 6.0f);
	lrg_widget_set_margin (self->content, &pad);

	/* Live readout (playing): on top, where it is seen */
	self->live = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->live, "physics-live");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->live), &live_funcs, self, NULL);
	self->live_section = lrg_expander_new ("Live");
	lrg_widget_set_name (LRG_WIDGET (self->live_section), "physics-live-section");
	lrg_expander_set_child (self->live_section, self->live);
	g_object_unref (self->live);
	lrg_expander_set_expanded (self->live_section, TRUE);
	lrg_widget_set_visible (LRG_WIDGET (self->live_section), FALSE);
	add_to (self->content, self->live_section);

	/* Body */
	add_to (self->content, heading_row ("physics-body-heading", "Body"));
	self->body_grid = add_to (self->content, make_grid (self, "physics-body-grid", PART_BODY));

	/* Shapes: heading with the kind picker and Add */
	row = heading_row ("physics-shapes-heading", "Shapes");
	self->shape_kind = add_to (row, make_combo ("physics-shape-kind", shape_kinds, G_N_ELEMENTS (shape_kinds),
	                                            LRG_COLLIDER3D_BOX));
	chip = editor_chip_new ("physics-add-shape", LRG_ICON_PLUS, "Add", on_add_shape, self);
	add_to (row, chip);
	add_to (self->content, row);
	self->shape_list = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->shape_list, "physics-shape-list");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->shape_list), &list_funcs, self, NULL);
	add_to (self->content, self->shape_list);

	/* Fit to mesh: "Fit selected shape to mesh as [kind] [Fit]" */
	self->fit_row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->fit_row, "physics-fit-row");
	lrg_container_set_spacing (LRG_CONTAINER (self->fit_row), 6.0f);
	label = LRG_WIDGET (lrg_label_new ("Fit to mesh as"));
	lrg_widget_add_class (label, "dim");
	lrg_widget_set_valign (label, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (label, 1.0f);
	add_to (self->fit_row, label);
	{
		const gchar *labels[G_N_ELEMENTS (fits)];

		for (i = 0; i < G_N_ELEMENTS (fits); i++)
			labels[i] = fits[i].label;
		self->fit_kind = add_to (self->fit_row, make_combo ("physics-fit-kind", labels, G_N_ELEMENTS (fits), 0));
	}
	chip = editor_chip_new ("physics-fit", LRG_ICON_REFRESH, "Fit", on_fit, self);
	add_to (self->fit_row, chip);
	add_to (self->content, self->fit_row);
	self->shape_grid = add_to (self->content, make_grid (self, "physics-shape-grid", PART_SHAPE));

	/* Joints */
	row = heading_row ("physics-joints-heading", "Joints");
	self->joint_kind = add_to (row, make_combo ("physics-joint-kind", joint_kinds, G_N_ELEMENTS (joint_kinds),
	                                            LRG_JOINT_HINGE));
	chip = editor_chip_new ("physics-add-joint", LRG_ICON_PLUS, "Add", on_add_joint, self);
	add_to (row, chip);
	add_to (self->content, row);
	self->joint_list = LRG_WIDGET (lrg_custom_widget_new ());
	lrg_widget_set_name (self->joint_list, "physics-joint-list");
	lrg_custom_widget_set_funcs (LRG_CUSTOM_WIDGET (self->joint_list), &list_funcs, self, NULL);
	add_to (self->content, self->joint_list);
	self->joint_grid = add_to (self->content, make_grid (self, "physics-joint-grid", PART_JOINT));
	lrg_property_grid_set_reference_func (self->joint_grid, reference_func, self, NULL);

	self->scroll = LRG_WIDGET (lrg_scroll_box_new ());
	lrg_widget_set_name (self->scroll, "physics-scroll");
	lrg_scroll_box_set_child (LRG_SCROLL_BOX (self->scroll), self->content);
	g_object_unref (self->content);
	lrg_container_add_child (LRG_CONTAINER (self), self->scroll);
	g_object_unref (self->scroll);

	/* Empty state */
	self->empty = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (self->empty, "physics-empty");
	lrg_container_set_spacing (LRG_CONTAINER (self->empty), 10.0f);
	lrg_widget_set_margin (self->empty, &pad);
	self->hint_none = add_to (self->empty, editor_panel_hint_new (
		"physics-hint-none", "Select a node to edit its physics body: its kind, mass, collision shapes and joints."));
	self->hint_2d = add_to (self->empty, editor_panel_hint_new (
		"physics-hint-2d", "This 2D node has no 3D physics body. 2D colliders are a `collider` component (shape "
		"box or circle, width, height, radius, offset_x, offset_y) edited in the Inspector; the viewport shows "
		"them with drag handles."));
	self->hint_body = add_to (self->empty, editor_panel_hint_new (
		"physics-hint", "This node has no physics body. Add one to make it collide and move under physics when "
		"the level plays; a shape fitted to its geometry comes with it."));
	row = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (row, "physics-empty-row");
	lrg_container_set_spacing (LRG_CONTAINER (row), 6.0f);
	lrg_widget_set_halign (row, LRG_ALIGN_CENTER);
	self->empty_kind = add_to (row, make_combo ("physics-empty-kind", body_kinds, G_N_ELEMENTS (body_kinds), 0));
	self->empty_add = editor_chip_new ("physics-empty-add", LRG_ICON_PLUS, "Add Body", on_add_body, self);
	editor_chip_set_tint (self->empty_add, LRG_THEME_COLOR_ACCENT);
	add_to (row, self->empty_add);
	add_to (self->empty, row);
	lrg_container_add_child (LRG_CONTAINER (self), self->empty);
	g_object_unref (self->empty);
}

static void
on_play (LrgEditorPhysicsPanel *self)
{
	sync_widgets (self);
}

LrgEditorPhysicsPanel *
lrg_editor_physics_panel_new (LrgEditorApp *app)
{
	LrgEditorPhysicsPanel *self;
	LrgEditorActions      *actions;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_PHYSICS_PANEL, NULL);
	lrg_widget_set_name (LRG_WIDGET (self), "physics-panel");
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	self->selection = g_object_ref (lrg_editor_get_selection (self->editor));
	self->play = (app->play != NULL) ? g_object_ref (app->play) : NULL;
	g_object_set_data (G_OBJECT (app), PANEL_KEY, self);

	g_signal_connect_swapped (self->editor, "changed", G_CALLBACK (on_changed), self);
	g_signal_connect_swapped (self->editor, "level-replaced", G_CALLBACK (on_level_replaced), self);
	g_signal_connect_swapped (self->selection, "changed", G_CALLBACK (on_changed), self);
	if (self->play != NULL)
	{
		g_signal_connect_swapped (self->play, "started", G_CALLBACK (on_play), self);
		g_signal_connect_swapped (self->play, "stopped", G_CALLBACK (on_play), self);
	}

	/* Actions and IPC from this file */
	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "physics.show", "Physics Panel", "View", NULL, a_show, NULL, NULL, self);
	lrg_editor_actions_add (actions, "physics.add-body", "Add Physics Body", "Node", NULL, a_add_body, can_add_body,
	                        NULL, self);
	lrg_editor_actions_add (actions, "physics.remove-body", "Remove Physics Body", "Node", NULL, a_remove_body,
	                        has_body, NULL, self);
	lrg_editor_actions_add (actions, "physics.add-shape", "Add Collision Shape", "Node", NULL, a_add_shape, has_node,
	                        NULL, self);
	lrg_editor_actions_add (actions, "physics.fit", "Fit Shape to Mesh", "Node", NULL, a_fit, has_body, NULL, self);
	if (app->server != NULL)
	{
		lrg_editor_server_register_method (app->server, "physics.get", m_get, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.add_body", m_add_body, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.remove_body", m_remove_body, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.set_body", m_set_body, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.add_shape", m_add_shape, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.remove_shape", m_remove_shape, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.set_shape", m_set_shape, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.move_shape", m_move_shape, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.add_joint", m_add_joint, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.remove_joint", m_remove_joint, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.set_joint", m_set_joint, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.fit", m_fit, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.state", m_state, app, NULL);
		lrg_editor_server_register_method (app->server, "physics.show", m_show, app, NULL);
	}

	sync_widgets (self);
	return self;
}

LrgEditorPhysicsPanel *
lrg_editor_physics_panel_find (LrgEditorApp *app)
{
	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);
	return g_object_get_data (G_OBJECT (app), PANEL_KEY);
}
