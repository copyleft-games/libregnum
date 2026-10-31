/* lrg-node.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Editable node in an #LrgLevel authoring tree.
 */

#include "lrg-node.h"
#include "lrg-node-private.h"
#include "lrg-node-visual.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"

typedef struct
{
	gchar         *name;
	gchar         *guid;

	GrlVector3    *location;
	GrlVector3    *rotation;
	GrlVector3    *scale;

	LrgNode       *parent;     /* borrowed back-pointer, not owned */
	GPtrArray     *children;   /* owned LrgNode* */

	gboolean       visible;
	gboolean       locked;
	gboolean       is_2d;

	LrgNodeVisual *visual;     /* owned, nullable */
	GPtrArray     *components; /* owned LrgComponentDesc* */
	GPtrArray     *scripts;    /* owned LrgScriptBinding* */
} LrgNodePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgNode, lrg_node, G_TYPE_OBJECT)

#define NODE_PRIV(self) ((LrgNodePrivate *) lrg_node_get_instance_private (self))

enum
{
	PROP_0,
	PROP_NAME,
	PROP_GUID,
	PROP_VISIBLE,
	PROP_LOCKED,
	PROP_IS_2D,
	PROP_VISUAL,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* See lrg_node_get_tree_serial() (lrg-node-private.h). */
static guint tree_serial = 1;

static void
tree_changed (void)
{
	if (++tree_serial == 0)
		tree_serial = 1;
}

guint
lrg_node_get_tree_serial (void)
{
	return tree_serial;
}

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_node_finalize (GObject *object)
{
	LrgNode        *self = LRG_NODE (object);
	LrgNodePrivate *priv = lrg_node_get_instance_private (self);

	g_clear_pointer (&priv->name, g_free);
	g_clear_pointer (&priv->guid, g_free);
	g_clear_pointer (&priv->location, grl_vector3_free);
	g_clear_pointer (&priv->rotation, grl_vector3_free);
	g_clear_pointer (&priv->scale, grl_vector3_free);
	g_clear_pointer (&priv->children, g_ptr_array_unref);
	g_clear_object (&priv->visual);
	g_clear_pointer (&priv->components, g_ptr_array_unref);
	g_clear_pointer (&priv->scripts, g_ptr_array_unref);
	tree_changed ();

	G_OBJECT_CLASS (lrg_node_parent_class)->finalize (object);
}

static void
lrg_node_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
	LrgNode        *self = LRG_NODE (object);
	LrgNodePrivate *priv = lrg_node_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_GUID:
		g_value_set_string (value, priv->guid);
		break;
	case PROP_VISIBLE:
		g_value_set_boolean (value, priv->visible);
		break;
	case PROP_LOCKED:
		g_value_set_boolean (value, priv->locked);
		break;
	case PROP_IS_2D:
		g_value_set_boolean (value, priv->is_2d);
		break;
	case PROP_VISUAL:
		g_value_set_object (value, priv->visual);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_node_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
	LrgNode        *self = LRG_NODE (object);
	LrgNodePrivate *priv = lrg_node_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_NAME:
		g_clear_pointer (&priv->name, g_free);
		priv->name = g_value_dup_string (value);
		tree_changed ();
		break;
	case PROP_GUID:
		if (g_value_get_string (value) != NULL)
		{
			g_clear_pointer (&priv->guid, g_free);
			priv->guid = g_value_dup_string (value);
			tree_changed ();
		}
		break;
	case PROP_VISIBLE:
		priv->visible = g_value_get_boolean (value);
		break;
	case PROP_LOCKED:
		priv->locked = g_value_get_boolean (value);
		break;
	case PROP_IS_2D:
		priv->is_2d = g_value_get_boolean (value);
		break;
	case PROP_VISUAL:
		g_set_object (&priv->visual, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_node_class_init (LrgNodeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_node_finalize;
	object_class->get_property = lrg_node_get_property;
	object_class->set_property = lrg_node_set_property;

	/* Default instantiate vfunc is a no-op; lrg_level_instantiate() applies
	 * component descriptions and script bindings via the engine registry. */
	klass->instantiate = NULL;

	/**
	 * LrgNode:name:
	 *
	 * The display name of the node.
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name",
		                     "Name",
		                     "Node name",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgNode:guid:
	 *
	 * The stable unique identifier of the node. Generated automatically on
	 * construction; may be overridden when loading a serialized level.
	 */
	properties[PROP_GUID] =
		g_param_spec_string ("guid",
		                     "GUID",
		                     "Stable unique identifier",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgNode:visible:
	 *
	 * Whether the node is visible in the viewport.
	 */
	properties[PROP_VISIBLE] =
		g_param_spec_boolean ("visible",
		                      "Visible",
		                      "Whether the node is visible",
		                      TRUE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	/**
	 * LrgNode:locked:
	 *
	 * Whether the node is locked against editing.
	 */
	properties[PROP_LOCKED] =
		g_param_spec_boolean ("locked",
		                      "Locked",
		                      "Whether the node is locked against editing",
		                      FALSE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	/**
	 * LrgNode:is-2d:
	 *
	 * Whether the node is authored as 2D (a gizmo/camera mode hint).
	 */
	properties[PROP_IS_2D] =
		g_param_spec_boolean ("is-2d",
		                      "Is 2D",
		                      "Whether the node is authored as 2D",
		                      FALSE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	/**
	 * LrgNode:visual:
	 *
	 * The tagged visual/content payload, or %NULL for an empty/group node.
	 */
	properties[PROP_VISUAL] =
		g_param_spec_object ("visual",
		                     "Visual",
		                     "Tagged visual payload",
		                     LRG_TYPE_NODE_VISUAL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_node_init (LrgNode *self)
{
	LrgNodePrivate *priv = lrg_node_get_instance_private (self);

	priv->name       = NULL;
	priv->guid       = g_uuid_string_random ();
	priv->location   = grl_vector3_new (0.0f, 0.0f, 0.0f);
	priv->rotation   = grl_vector3_new (0.0f, 0.0f, 0.0f);
	priv->scale      = grl_vector3_new (1.0f, 1.0f, 1.0f);
	priv->parent     = NULL;
	priv->children   = g_ptr_array_new_with_free_func (g_object_unref);
	priv->visible    = TRUE;
	priv->locked     = FALSE;
	priv->is_2d      = FALSE;
	tree_changed ();
	priv->visual     = NULL;
	priv->components = g_ptr_array_new_with_free_func (g_object_unref);
	priv->scripts    = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/*
 * lrg_node_new:
 * @name: (nullable): the node name
 *
 * Creates a new #LrgNode with an automatically generated guid, identity
 * transform, and no visual.
 *
 * Returns: (transfer full): a new #LrgNode
 */
LrgNode *
lrg_node_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_NODE,
	                     "name", name,
	                     NULL);
}

/* ==========================================================================
 * Identity
 * ========================================================================== */

const gchar *
lrg_node_get_name (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->name;
}

void
lrg_node_set_name (LrgNode     *self,
                   const gchar *name)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));

	priv = lrg_node_get_instance_private (self);
	g_clear_pointer (&priv->name, g_free);
	priv->name = g_strdup (name);
	tree_changed ();
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

const gchar *
lrg_node_get_guid (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->guid;
}

void
lrg_node_set_guid (LrgNode     *self,
                   const gchar *guid)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));
	g_return_if_fail (guid != NULL);

	priv = lrg_node_get_instance_private (self);
	g_clear_pointer (&priv->guid, g_free);
	priv->guid = g_strdup (guid);
	tree_changed ();
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GUID]);
}

/* ==========================================================================
 * Flags
 * ========================================================================== */

gboolean
lrg_node_get_visible (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), FALSE);

	return NODE_PRIV (self)->visible;
}

void
lrg_node_set_visible (LrgNode  *self,
                      gboolean  visible)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));

	priv = lrg_node_get_instance_private (self);
	priv->visible = visible;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
}

gboolean
lrg_node_get_locked (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), FALSE);

	return NODE_PRIV (self)->locked;
}

void
lrg_node_set_locked (LrgNode  *self,
                     gboolean  locked)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));

	priv = lrg_node_get_instance_private (self);
	priv->locked = locked;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCKED]);
}

gboolean
lrg_node_get_is_2d (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), FALSE);

	return NODE_PRIV (self)->is_2d;
}

void
lrg_node_set_is_2d (LrgNode  *self,
                    gboolean  is_2d)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));

	priv = lrg_node_get_instance_private (self);
	priv->is_2d = is_2d;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_2D]);
}

/* ==========================================================================
 * Transform (local TRS)
 * ========================================================================== */

GrlVector3 *
lrg_node_get_location (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->location;
}

void
lrg_node_set_location_xyz (LrgNode *self,
                           gfloat   x,
                           gfloat   y,
                           gfloat   z)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));

	priv = lrg_node_get_instance_private (self);
	g_clear_pointer (&priv->location, grl_vector3_free);
	priv->location = grl_vector3_new (x, y, z);
}

GrlVector3 *
lrg_node_get_rotation (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->rotation;
}

void
lrg_node_set_rotation_xyz (LrgNode *self,
                           gfloat   rx,
                           gfloat   ry,
                           gfloat   rz)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));

	priv = lrg_node_get_instance_private (self);
	g_clear_pointer (&priv->rotation, grl_vector3_free);
	priv->rotation = grl_vector3_new (rx, ry, rz);
}

GrlVector3 *
lrg_node_get_scale (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->scale;
}

void
lrg_node_set_scale_xyz (LrgNode *self,
                        gfloat   sx,
                        gfloat   sy,
                        gfloat   sz)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));

	priv = lrg_node_get_instance_private (self);
	g_clear_pointer (&priv->scale, grl_vector3_free);
	priv->scale = grl_vector3_new (sx, sy, sz);
}

/* ==========================================================================
 * Hierarchy
 * ========================================================================== */

LrgNode *
lrg_node_get_parent (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->parent;
}

void
lrg_node_add_child (LrgNode *self,
                    LrgNode *child)
{
	LrgNodePrivate       *priv;
	LrgNodePrivate       *child_priv;

	g_return_if_fail (LRG_IS_NODE (self));
	g_return_if_fail (LRG_IS_NODE (child));
	g_return_if_fail (self != child);

	priv       = lrg_node_get_instance_private (self);
	child_priv = lrg_node_get_instance_private (child);

	child_priv->parent = self;
	g_ptr_array_add (priv->children, g_object_ref (child));
	tree_changed ();
}

gboolean
lrg_node_remove_child (LrgNode *self,
                       LrgNode *child)
{
	LrgNodePrivate *priv;
	LrgNodePrivate *child_priv;

	g_return_val_if_fail (LRG_IS_NODE (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (child), FALSE);

	priv       = lrg_node_get_instance_private (self);
	child_priv = lrg_node_get_instance_private (child);

	if (child_priv->parent == self)
		child_priv->parent = NULL;

	/* g_ptr_array_remove drops the array's reference via the free func. */
	tree_changed ();
	return g_ptr_array_remove (priv->children, child);
}

GPtrArray *
lrg_node_get_children (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->children;
}

guint
lrg_node_get_n_children (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), 0);

	return NODE_PRIV (self)->children->len;
}

LrgNode *
lrg_node_find_by_guid (LrgNode     *self,
                       const gchar *guid)
{
	LrgNodePrivate *priv;
	guint           i;

	g_return_val_if_fail (LRG_IS_NODE (self), NULL);
	g_return_val_if_fail (guid != NULL, NULL);

	priv = lrg_node_get_instance_private (self);

	if (priv->guid != NULL && g_strcmp0 (priv->guid, guid) == 0)
		return self;

	for (i = 0; i < priv->children->len; i++)
	{
		LrgNode *child = g_ptr_array_index (priv->children, i);
		LrgNode *found = lrg_node_find_by_guid (child, guid);

		if (found != NULL)
			return found;
	}

	return NULL;
}

/* ==========================================================================
 * Visual
 * ========================================================================== */

LrgNodeVisual *
lrg_node_get_visual (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->visual;
}

void
lrg_node_set_visual (LrgNode       *self,
                     LrgNodeVisual *visual)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));
	g_return_if_fail (visual == NULL || LRG_IS_NODE_VISUAL (visual));

	priv = lrg_node_get_instance_private (self);
	if (g_set_object (&priv->visual, visual))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISUAL]);
}

/* ==========================================================================
 * Components and Scripts
 * ========================================================================== */

void
lrg_node_add_component (LrgNode          *self,
                        LrgComponentDesc *desc)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));
	g_return_if_fail (LRG_IS_COMPONENT_DESC (desc));

	priv = lrg_node_get_instance_private (self);
	g_ptr_array_add (priv->components, g_object_ref (desc));
}

GPtrArray *
lrg_node_get_components (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->components;
}

void
lrg_node_add_script (LrgNode          *self,
                     LrgScriptBinding *binding)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));
	g_return_if_fail (LRG_IS_SCRIPT_BINDING (binding));

	priv = lrg_node_get_instance_private (self);
	g_ptr_array_add (priv->scripts, g_object_ref (binding));
}

GPtrArray *
lrg_node_get_scripts (LrgNode *self)
{
	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	return NODE_PRIV (self)->scripts;
}

/* ==========================================================================
 * Instantiation
 * ========================================================================== */

void
lrg_node_instantiate (LrgNode       *self,
                      LrgGameObject *object)
{
	LrgNodeClass *klass;

	g_return_if_fail (LRG_IS_NODE (self));

	klass = LRG_NODE_GET_CLASS (self);
	if (klass->instantiate != NULL)
		klass->instantiate (self, object);
}

/* ==========================================================================
 * Indexed component / script editing
 * ========================================================================== */

void
lrg_node_insert_component (LrgNode          *self,
                           LrgComponentDesc *desc,
                           gint              index)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));
	g_return_if_fail (LRG_IS_COMPONENT_DESC (desc));

	priv = lrg_node_get_instance_private (self);
	if (index < 0 || (guint) index > priv->components->len)
		index = -1;
	g_ptr_array_insert (priv->components, index, g_object_ref (desc));
}

gboolean
lrg_node_remove_component_at (LrgNode *self,
                              guint    index)
{
	LrgNodePrivate *priv;

	g_return_val_if_fail (LRG_IS_NODE (self), FALSE);

	priv = lrg_node_get_instance_private (self);
	if (index >= priv->components->len)
		return FALSE;

	g_ptr_array_remove_index (priv->components, index);
	return TRUE;
}

LrgComponentDesc *
lrg_node_find_component (LrgNode     *self,
                         const gchar *type_name)
{
	LrgNodePrivate *priv;
	guint           i;

	g_return_val_if_fail (LRG_IS_NODE (self), NULL);
	g_return_val_if_fail (type_name != NULL, NULL);

	priv = lrg_node_get_instance_private (self);
	for (i = 0; i < priv->components->len; i++)
	{
		LrgComponentDesc *desc = g_ptr_array_index (priv->components, i);

		if (g_strcmp0 (lrg_component_desc_get_type_name (desc), type_name) == 0)
			return desc;
	}

	return NULL;
}

void
lrg_node_insert_script (LrgNode          *self,
                        LrgScriptBinding *binding,
                        gint              index)
{
	LrgNodePrivate *priv;

	g_return_if_fail (LRG_IS_NODE (self));
	g_return_if_fail (LRG_IS_SCRIPT_BINDING (binding));

	priv = lrg_node_get_instance_private (self);
	if (index < 0 || (guint) index > priv->scripts->len)
		index = -1;
	g_ptr_array_insert (priv->scripts, index, g_object_ref (binding));
}

gboolean
lrg_node_remove_script_at (LrgNode *self,
                           guint    index)
{
	LrgNodePrivate *priv;

	g_return_val_if_fail (LRG_IS_NODE (self), FALSE);

	priv = lrg_node_get_instance_private (self);
	if (index >= priv->scripts->len)
		return FALSE;

	g_ptr_array_remove_index (priv->scripts, index);
	return TRUE;
}

/* ==========================================================================
 * Tree editing helpers
 * ========================================================================== */

void
lrg_node_insert_child (LrgNode *self,
                       LrgNode *child,
                       gint     index)
{
	LrgNodePrivate *priv;
	LrgNodePrivate *child_priv;

	g_return_if_fail (LRG_IS_NODE (self));
	g_return_if_fail (LRG_IS_NODE (child));
	g_return_if_fail (!lrg_node_is_ancestor_of (child, self));

	priv       = lrg_node_get_instance_private (self);
	child_priv = lrg_node_get_instance_private (child);

	if (index < 0 || (guint) index > priv->children->len)
		index = -1;

	child_priv->parent = self;
	g_ptr_array_insert (priv->children, index, g_object_ref (child));
	tree_changed ();
}

gint
lrg_node_get_child_index (LrgNode *self,
                          LrgNode *child)
{
	guint index;

	g_return_val_if_fail (LRG_IS_NODE (self), -1);
	g_return_val_if_fail (LRG_IS_NODE (child), -1);

	if (!g_ptr_array_find (NODE_PRIV (self)->children, child, &index))
		return -1;

	return (gint) index;
}

gboolean
lrg_node_is_ancestor_of (LrgNode *self,
                         LrgNode *node)
{
	g_return_val_if_fail (LRG_IS_NODE (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	/* Walk up from @node; @self counts as its own ancestor. */
	while (node != NULL)
	{
		if (node == self)
			return TRUE;
		node = NODE_PRIV (node)->parent;
	}

	return FALSE;
}

guint
lrg_node_get_depth (LrgNode *self)
{
	guint    depth = 0;
	LrgNode *p;

	g_return_val_if_fail (LRG_IS_NODE (self), 0);

	for (p = NODE_PRIV (self)->parent; p != NULL; p = NODE_PRIV (p)->parent)
		depth++;

	return depth;
}

gchar *
lrg_node_get_path (LrgNode *self)
{
	GPtrArray *parts;
	GString   *path;
	LrgNode   *n;
	gint       i;

	g_return_val_if_fail (LRG_IS_NODE (self), NULL);

	/* Collect names leaf-to-root, skipping the implicit (parentless)
	 * level root unless @self is that root. */
	parts = g_ptr_array_new ();
	for (n = self; n != NULL; n = NODE_PRIV (n)->parent)
	{
		if (NODE_PRIV (n)->parent == NULL && n != self)
			break;
		g_ptr_array_add (parts, NODE_PRIV (n)->name != NULL
		                        ? NODE_PRIV (n)->name : "(node)");
	}

	path = g_string_new (NULL);
	for (i = (gint) parts->len - 1; i >= 0; i--)
	{
		g_string_append (path, g_ptr_array_index (parts, i));
		if (i > 0)
			g_string_append_c (path, '/');
	}
	g_ptr_array_unref (parts);

	return g_string_free (path, FALSE);
}

LrgNode *
lrg_node_find_by_path (LrgNode     *self,
                       const gchar *path)
{
	g_auto(GStrv) parts = NULL;
	LrgNode      *cur;
	guint         i;

	g_return_val_if_fail (LRG_IS_NODE (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	parts = g_strsplit (path, "/", -1);
	cur = self;
	for (i = 0; parts[i] != NULL; i++)
	{
		GPtrArray *children;
		LrgNode   *next = NULL;
		guint      j;

		/* Tolerate leading, trailing and doubled slashes. */
		if (parts[i][0] == '\0')
			continue;

		children = NODE_PRIV (cur)->children;
		for (j = 0; j < children->len; j++)
		{
			LrgNode *c = g_ptr_array_index (children, j);

			if (g_strcmp0 (NODE_PRIV (c)->name, parts[i]) == 0)
			{
				next = c;
				break;
			}
		}
		if (next == NULL)
			return NULL;
		cur = next;
	}

	return cur;
}

/* ==========================================================================
 * State copy / compare (snapshot undo)
 * ========================================================================== */

void
lrg_node_copy_state (LrgNode *self,
                     LrgNode *source)
{
	LrgNodePrivate *dst;
	LrgNodePrivate *src;
	guint           i;

	g_return_if_fail (LRG_IS_NODE (self));
	g_return_if_fail (LRG_IS_NODE (source));

	if (self == source)
		return;

	dst = lrg_node_get_instance_private (self);
	src = lrg_node_get_instance_private (source);

	/* Identity and flags (not the GUID: a snapshot restores into the live
	 * node, which keeps its own identity). */
	g_free (dst->name);
	dst->name    = g_strdup (src->name);
	tree_changed ();
	dst->visible = src->visible;
	dst->locked  = src->locked;
	dst->is_2d   = src->is_2d;

	/* Transform. */
	lrg_node_set_location_xyz (self, src->location->x, src->location->y, src->location->z);
	lrg_node_set_rotation_xyz (self, src->rotation->x, src->rotation->y, src->rotation->z);
	lrg_node_set_scale_xyz (self, src->scale->x, src->scale->y, src->scale->z);

	/* Visual payload: deep copy so later edits never alias the snapshot. */
	g_clear_object (&dst->visual);
	if (src->visual != NULL)
		dst->visual = lrg_node_visual_copy (src->visual);

	g_ptr_array_set_size (dst->components, 0);
	for (i = 0; i < src->components->len; i++)
		g_ptr_array_add (dst->components,
		                 lrg_component_desc_copy (g_ptr_array_index (src->components, i)));

	g_ptr_array_set_size (dst->scripts, 0);
	for (i = 0; i < src->scripts->len; i++)
		g_ptr_array_add (dst->scripts,
		                 lrg_script_binding_copy (g_ptr_array_index (src->scripts, i)));

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCKED]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_2D]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISUAL]);
}

static gboolean
vec3_equal (const GrlVector3 *a,
            const GrlVector3 *b)
{
	return a->x == b->x && a->y == b->y && a->z == b->z;
}

gboolean
lrg_node_state_equal (LrgNode *self,
                      LrgNode *other)
{
	LrgNodePrivate *a;
	LrgNodePrivate *b;
	guint           i;

	g_return_val_if_fail (LRG_IS_NODE (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (other), FALSE);

	a = lrg_node_get_instance_private (self);
	b = lrg_node_get_instance_private (other);

	if (g_strcmp0 (a->name, b->name) != 0 ||
	    !a->visible != !b->visible ||
	    !a->locked != !b->locked ||
	    !a->is_2d != !b->is_2d)
		return FALSE;

	if (!vec3_equal (a->location, b->location) ||
	    !vec3_equal (a->rotation, b->rotation) ||
	    !vec3_equal (a->scale, b->scale))
		return FALSE;

	if (!lrg_node_visual_equal (a->visual, b->visual))
		return FALSE;

	if (a->components->len != b->components->len ||
	    a->scripts->len != b->scripts->len)
		return FALSE;

	for (i = 0; i < a->components->len; i++)
		if (!lrg_component_desc_equal (g_ptr_array_index (a->components, i),
		                               g_ptr_array_index (b->components, i)))
			return FALSE;

	for (i = 0; i < a->scripts->len; i++)
		if (!lrg_script_binding_equal (g_ptr_array_index (a->scripts, i),
		                               g_ptr_array_index (b->scripts, i)))
			return FALSE;

	return TRUE;
}

static gpointer
ref_node_copy (gconstpointer src,
               gpointer      data)
{
	return g_object_ref ((gpointer) src);
}

gboolean
lrg_node_foreach (LrgNode            *self,
                  LrgNodeForeachFunc  func,
                  gpointer            user_data)
{
	GPtrArray *children;
	guint      i;

	g_return_val_if_fail (LRG_IS_NODE (self), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	if (!func (self, user_data))
		return FALSE;

	/* Iterate over a snapshot so @func may safely restructure the tree. */
	children = g_ptr_array_copy (NODE_PRIV (self)->children, ref_node_copy, NULL);
	g_ptr_array_set_free_func (children, g_object_unref);
	for (i = 0; i < children->len; i++)
	{
		if (!lrg_node_foreach (g_ptr_array_index (children, i), func, user_data))
		{
			g_ptr_array_unref (children);
			return FALSE;
		}
	}
	g_ptr_array_unref (children);

	return TRUE;
}
