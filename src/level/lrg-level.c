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
#include "lrg-timeline.h"

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
	GPtrArray *timelines;  /* LrgTimeline* (owned) */
	LrgEnvironment *environment;  /* nullable, owned */

	/* the `lighting:` block (0.9.0 M3) */
	gchar      *lightmap_ref;     /* nullable */
	GPtrArray  *probe_refs;       /* gchar *, owned */
	GHashTable *bake_settings;    /* gchar * -> gdouble * */
};

G_DEFINE_FINAL_TYPE (LrgLevel, lrg_level, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_DEFAULT_2D,
	PROP_ENVIRONMENT,
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
	g_clear_pointer (&self->timelines, g_ptr_array_unref);
	g_clear_object (&self->environment);
	g_clear_pointer (&self->lightmap_ref, g_free);
	g_clear_pointer (&self->probe_refs, g_ptr_array_unref);
	g_clear_pointer (&self->bake_settings, g_hash_table_unref);

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
	case PROP_ENVIRONMENT:
		g_value_set_object (value, self->environment);
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
	case PROP_ENVIRONMENT:
		lrg_level_set_environment (self, g_value_get_object (value));
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

	/**
	 * LrgLevel:environment:
	 *
	 * The level's environment block (#LrgEnvironment), %NULL when the
	 * level has none.
	 */
	properties[PROP_ENVIRONMENT] =
		g_param_spec_object ("environment",
		                     "Environment",
		                     "Sky, lighting and fog of the level",
		                     LRG_TYPE_ENVIRONMENT,
		                     G_PARAM_READWRITE |
		                     G_PARAM_EXPLICIT_NOTIFY |
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
	self->timelines  = g_ptr_array_new_with_free_func (g_object_unref);
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/*
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

	lrg_level_insert_node (self, node, parent, -1);
}

void
lrg_level_insert_node (LrgLevel *self,
                       LrgNode  *node,
                       LrgNode  *parent,
                       gint      index)
{
	g_return_if_fail (LRG_IS_LEVEL (self));
	g_return_if_fail (LRG_IS_NODE (node));
	g_return_if_fail (parent == NULL || LRG_IS_NODE (parent));

	if (parent == NULL)
		parent = self->root;

	lrg_node_insert_child (parent, node, index);
	g_signal_emit (self, signals[SIGNAL_NODE_ADDED], 0, node);
}

static gboolean
count_node (LrgNode  *node,
            gpointer  user_data)
{
	(*(guint *) user_data)++;
	return TRUE;
}

guint
lrg_level_count_nodes (LrgLevel *self)
{
	guint count = 0;

	g_return_val_if_fail (LRG_IS_LEVEL (self), 0);

	lrg_node_foreach (self->root, count_node, &count);
	return count - 1;  /* the walk includes the implicit root */
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

/* ==========================================================================
 * Timelines
 * ========================================================================== */

guint
lrg_level_get_n_timelines (LrgLevel *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), 0);
	return self->timelines->len;
}

LrgTimeline *
lrg_level_get_timeline (LrgLevel *self,
                        guint     index)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), NULL);
	return (index < self->timelines->len) ? g_ptr_array_index (self->timelines, index) : NULL;
}

gint
lrg_level_find_timeline (LrgLevel    *self,
                         const gchar *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_LEVEL (self), -1);

	for (i = 0; name != NULL && i < self->timelines->len; i++)
		if (g_strcmp0 (lrg_timeline_get_name (g_ptr_array_index (self->timelines, i)), name) == 0)
			return (gint) i;
	return -1;
}

void
lrg_level_insert_timeline (LrgLevel    *self,
                           LrgTimeline *timeline,
                           gint         index)
{
	g_return_if_fail (LRG_IS_LEVEL (self));
	g_return_if_fail (LRG_IS_TIMELINE (timeline));

	if (index < 0 || (guint) index > self->timelines->len)
		index = (gint) self->timelines->len;
	g_ptr_array_insert (self->timelines, index, g_object_ref (timeline));
}

gboolean
lrg_level_remove_timeline (LrgLevel *self,
                           guint     index)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), FALSE);

	if (index >= self->timelines->len)
		return FALSE;
	g_ptr_array_remove_index (self->timelines, index);
	return TRUE;
}

/* ==========================================================================
 * Environment
 * ========================================================================== */

LrgEnvironment *
lrg_level_get_environment (LrgLevel *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), NULL);
	return self->environment;
}

void
lrg_level_set_environment (LrgLevel       *self,
                           LrgEnvironment *environment)
{
	g_return_if_fail (LRG_IS_LEVEL (self));
	g_return_if_fail (environment == NULL || LRG_IS_ENVIRONMENT (environment));

	if (g_set_object (&self->environment, environment))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENVIRONMENT]);
}

LrgEnvironment *
lrg_level_ensure_environment (LrgLevel *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), NULL);

	if (self->environment == NULL)
	{
		g_autoptr(LrgEnvironment) env = lrg_environment_new ();

		lrg_level_set_environment (self, env);
	}
	return self->environment;
}

/* ==========================================================================
 * Baked lighting (the `lighting:` block, 0.9.0 M3)
 * ========================================================================== */

const gchar *
lrg_level_get_lightmap_ref (LrgLevel *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), NULL);
	return self->lightmap_ref;
}

void
lrg_level_set_lightmap_ref (LrgLevel    *self,
                            const gchar *ref)
{
	g_return_if_fail (LRG_IS_LEVEL (self));

	g_free (self->lightmap_ref);
	self->lightmap_ref = (ref != NULL && *ref != '\0') ? g_strdup (ref) : NULL;
}

/* the probe reference array, created on first use */
static GPtrArray *
ensure_probe_refs (LrgLevel *self)
{
	if (self->probe_refs == NULL)
		self->probe_refs = g_ptr_array_new_with_free_func (g_free);
	return self->probe_refs;
}

void
lrg_level_add_probe_ref (LrgLevel    *self,
                         const gchar *ref)
{
	GPtrArray *refs;
	guint      i;

	g_return_if_fail (LRG_IS_LEVEL (self));
	g_return_if_fail (ref != NULL && *ref != '\0');

	refs = ensure_probe_refs (self);
	for (i = 0; i < refs->len; i++)
		if (g_strcmp0 (g_ptr_array_index (refs, i), ref) == 0)
			return;
	g_ptr_array_add (refs, g_strdup (ref));
}

gboolean
lrg_level_remove_probe_ref (LrgLevel    *self,
                            const gchar *ref)
{
	guint i;

	g_return_val_if_fail (LRG_IS_LEVEL (self), FALSE);

	if (self->probe_refs == NULL || ref == NULL)
		return FALSE;
	for (i = 0; i < self->probe_refs->len; i++)
		if (g_strcmp0 (g_ptr_array_index (self->probe_refs, i), ref) == 0)
		{
			g_ptr_array_remove_index (self->probe_refs, i);
			return TRUE;
		}
	return FALSE;
}

GPtrArray *
lrg_level_get_probe_refs (LrgLevel *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), NULL);
	return ensure_probe_refs (self);
}

void
lrg_level_set_bake_setting (LrgLevel    *self,
                            const gchar *key,
                            gdouble      value)
{
	gdouble *v;

	g_return_if_fail (LRG_IS_LEVEL (self));
	g_return_if_fail (key != NULL && *key != '\0');

	if (self->bake_settings == NULL)
		self->bake_settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	v = g_new (gdouble, 1);
	*v = value;
	g_hash_table_replace (self->bake_settings, g_strdup (key), v);
}

gboolean
lrg_level_get_bake_setting (LrgLevel    *self,
                            const gchar *key,
                            gdouble     *out_value)
{
	gdouble *v;

	g_return_val_if_fail (LRG_IS_LEVEL (self), FALSE);

	if (self->bake_settings == NULL || key == NULL)
		return FALSE;
	v = g_hash_table_lookup (self->bake_settings, key);
	if (v == NULL)
		return FALSE;
	if (out_value != NULL)
		*out_value = *v;
	return TRUE;
}

static gint
compare_keys (gconstpointer a,
              gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

GPtrArray *
lrg_level_get_bake_setting_keys (LrgLevel *self)
{
	GPtrArray      *out;
	GHashTableIter  it;
	gpointer        key;

	g_return_val_if_fail (LRG_IS_LEVEL (self), NULL);

	out = g_ptr_array_new_with_free_func (g_free);
	if (self->bake_settings == NULL)
		return out;
	g_hash_table_iter_init (&it, self->bake_settings);
	while (g_hash_table_iter_next (&it, &key, NULL))
		g_ptr_array_add (out, g_strdup (key));
	g_ptr_array_sort (out, compare_keys);
	return out;
}

gboolean
lrg_level_has_lighting (LrgLevel *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL (self), FALSE);

	return self->lightmap_ref != NULL || (self->probe_refs != NULL && self->probe_refs->len > 0)
	       || (self->bake_settings != NULL && g_hash_table_size (self->bake_settings) > 0);
}

void
lrg_level_clear_lighting (LrgLevel *self)
{
	g_return_if_fail (LRG_IS_LEVEL (self));

	g_clear_pointer (&self->lightmap_ref, g_free);
	if (self->probe_refs != NULL)
		g_ptr_array_set_size (self->probe_refs, 0);
	if (self->bake_settings != NULL)
		g_hash_table_remove_all (self->bake_settings);
}
