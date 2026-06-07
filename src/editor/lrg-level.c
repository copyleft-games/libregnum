/* lrg-level.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Top-level editable level document.
 */

#include "lrg-level.h"
#include "lrg-node.h"

/**
 * LrgLevel:
 *
 * The serializable editor document: a root #LrgNode tree plus level-wide
 * settings and metadata.
 */
struct _LrgLevel
{
	GObject parent_instance;

	gchar    *name;
	LrgNode  *root;
	gboolean  default_2d;
};

G_DEFINE_FINAL_TYPE (LrgLevel, lrg_level, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_DEFAULT_2D,
	N_PROPS
};

enum
{
	SIGNAL_NODE_ADDED,
	SIGNAL_NODE_REMOVED,
	SIGNAL_NODE_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_level_finalize (GObject *object)
{
	LrgLevel *self = LRG_LEVEL (object);

	g_clear_pointer (&self->name, g_free);
	g_clear_object (&self->root);

	G_OBJECT_CLASS (lrg_level_parent_class)->finalize (object);
}

static void
lrg_level_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgLevel *self = LRG_LEVEL (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case PROP_DEFAULT_2D:
		g_value_set_boolean (value, self->default_2d);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_level_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgLevel *self = LRG_LEVEL (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_clear_pointer (&self->name, g_free);
		self->name = g_value_dup_string (value);
		break;
	case PROP_DEFAULT_2D:
		self->default_2d = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_level_class_init (LrgLevelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_level_finalize;
	object_class->get_property = lrg_level_get_property;
	object_class->set_property = lrg_level_set_property;

	/**
	 * LrgLevel:name:
	 *
	 * The level name.
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name",
		                     "Name",
		                     "Level name",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgLevel:default-2d:
	 *
	 * Whether the level defaults to a 2D editing view.
	 */
	properties[PROP_DEFAULT_2D] =
		g_param_spec_boolean ("default-2d",
		                      "Default 2D",
		                      "Whether the default view is 2D",
		                      FALSE,
		                      G_PARAM_READWRITE |
		                      G_PARAM_CONSTRUCT |
		                      G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgLevel::node-added:
	 * @self: the level
	 * @node: the node that was added
	 *
	 * Emitted after a node is added to the level tree.
	 */
	signals[SIGNAL_NODE_ADDED] =
		g_signal_new ("node-added",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_NODE);

	/**
	 * LrgLevel::node-removed:
	 * @self: the level
	 * @node: the node that was removed
	 *
	 * Emitted after a node is removed from the level tree.
	 */
	signals[SIGNAL_NODE_REMOVED] =
		g_signal_new ("node-removed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_NODE);

	/**
	 * LrgLevel::node-changed:
	 * @self: the level
	 * @node: the node that changed
	 *
	 * Emitted when an editor operation mutates a node.
	 */
	signals[SIGNAL_NODE_CHANGED] =
		g_signal_new ("node-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_NODE);
}

static void
lrg_level_init (LrgLevel *self)
{
	self->name       = NULL;
	self->root       = lrg_node_new ("Root");
	self->default_2d = FALSE;
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_level_new:
 * @name: (nullable): the level name
 *
 * Creates a new, empty #LrgLevel with a single empty root node.
 *
 * Returns: (transfer full): a new #LrgLevel
 */
LrgLevel *
lrg_level_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_LEVEL,
	                     "name", name,
	                     NULL);
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

const gchar *
lrg_level_get_name (LrgLevel *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), NULL);

	return self->name;
}

void
lrg_level_set_name (LrgLevel    *self,
                    const gchar *name)
{
	g_return_if_fail (LRG_IS_LEVEL (self));

	g_clear_pointer (&self->name, g_free);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

gboolean
lrg_level_get_default_2d (LrgLevel *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), FALSE);

	return self->default_2d;
}

void
lrg_level_set_default_2d (LrgLevel *self,
                          gboolean  default_2d)
{
	g_return_if_fail (LRG_IS_LEVEL (self));

	self->default_2d = default_2d;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEFAULT_2D]);
}

LrgNode *
lrg_level_get_root (LrgLevel *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), NULL);

	return self->root;
}

/* ==========================================================================
 * Node Management
 * ========================================================================== */

void
lrg_level_add_node (LrgLevel *self,
                    LrgNode  *node,
                    LrgNode  *parent)
{
	g_return_if_fail (LRG_IS_LEVEL (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (parent == NULL || LRG_IS_NODE (parent));

	if (parent == NULL)
		parent = self->root;

	lrg_node_add_child (parent, node);
	g_signal_emit (self, signals[SIGNAL_NODE_ADDED], 0, node);
}

gboolean
lrg_level_remove_node (LrgLevel *self,
                       LrgNode  *node)
{
	LrgNode  *parent;
	gboolean  removed;

	g_return_val_if_fail (LRG_IS_LEVEL (self), FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	parent = lrg_node_get_parent (node);
	if (parent == NULL)
		return FALSE;

	/* Keep the node alive across the removal so listeners can inspect it. */
	g_object_ref (node);
	removed = lrg_node_remove_child (parent, node);
	if (removed)
		g_signal_emit (self, signals[SIGNAL_NODE_REMOVED], 0, node);
	g_object_unref (node);

	return removed;
}

LrgNode *
lrg_level_find_node (LrgLevel    *self,
                     const gchar *guid)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), NULL);
	g_return_val_if_fail (guid != NULL, NULL);

	return lrg_node_find_by_guid (self->root, guid);
}

void
lrg_level_notify_node_changed (LrgLevel *self,
                               LrgNode  *node)
{
	g_return_if_fail (LRG_IS_LEVEL (self));
	g_return_if_fail (LRG_IS_NODE (node));

	g_signal_emit (self, signals[SIGNAL_NODE_CHANGED], 0, node);
}
