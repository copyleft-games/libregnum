/* lrg-entity3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEntity3D: the runtime 3D scene entity.
 *
 * Transform cache design
 * ----------------------
 * Every entity keeps:
 *  - local_version: bumped by every local TRS change and by reparenting;
 *  - a cached local matrix tagged with the local_version it was built for;
 *  - a cached world matrix tagged with (local_version, parent pointer,
 *    parent world_version) it was built from, plus its own world_version
 *    which takes a fresh value from a global counter whenever the cache is
 *    rebuilt;
 *  - world_epoch: the value of the global transform epoch when the cache
 *    was last validated.
 *
 * Any local change anywhere bumps the global epoch.  A world matrix
 * validated in the current epoch is therefore still valid without looking
 * at the ancestors (the common case in a frame where nothing moved, or
 * when many reads happen between two writes).  Otherwise the entity
 * validates its parent first (recursively, stopping at the first ancestor
 * validated in this epoch) and rebuilds only when its own inputs changed.
 * Children are never walked on a write, so moving a parent with ten
 * thousand descendants is O(1) and the descendants are fixed up lazily on
 * their next read.
 *
 * The epoch and counters are plain integers: an #LrgEntity3D hierarchy is
 * single-threaded, like the rest of a world.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_WORLD3D

#include <math.h>
#include <string.h>

#include "lrg-entity3d.h"
#include "lrg-world3d.h"
#include "../core/lrg-math3d.h"
#include "../ecs/lrg-component.h"
#include "../graphics/lrg-camera.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Internal cross-file hooks
 *
 * Defined in lrg-component.c and lrg-world3d.c.  They are library-internal
 * (hidden visibility) and declared here rather than in a private header.
 * ========================================================================== */

void _lrg_component_set_entity        (LrgComponent *self,
                                       LrgEntity3D  *entity);
void _lrg_world3d_entity_joined       (LrgWorld3D   *world,
                                       LrgEntity3D  *entity);
void _lrg_world3d_entity_leaving      (LrgWorld3D   *world,
                                       LrgEntity3D  *entity);
void _lrg_world3d_guid_changed        (LrgWorld3D   *world,
                                       LrgEntity3D  *entity,
                                       const gchar  *old_guid);
void _lrg_world3d_queue_transform     (LrgWorld3D   *world,
                                       LrgEntity3D  *entity,
                                       gboolean      queue_signal);

/* Exported to lrg-world3d.c (declared there the same way). */
void     _lrg_entity3d_set_world_recursive  (LrgEntity3D *self,
                                             LrgWorld3D  *world);
void     _lrg_entity3d_emit_transform_changed (LrgEntity3D *self);

/* ==========================================================================
 * Private data
 * ========================================================================== */

typedef struct
{
	/* Identity and flags */
	gchar       *guid;
	gchar       *name;
	gboolean     active;
	gboolean     visible;
	gboolean     is_static;
	guint        layer;
	GHashTable  *tags;              /* gchar* -> NULL set, lazily created */

	/* Local TRS + authored Euler */
	gfloat       position[3];
	gfloat       rotation[4];
	gfloat       scale[3];
	gfloat       euler[3];
	gboolean     euler_valid;

	/* Hierarchy (parent and world are weak links) */
	LrgEntity3D *parent;
	LrgWorld3D  *world;
	GPtrArray   *children;          /* owns a ref on each child */
	GPtrArray   *components;        /* owns a ref on each component */

	/* Transform cache (see the file comment) */
	guint64      local_version;
	guint64      local_matrix_version;
	gfloat       local_matrix[16];
	gboolean     world_valid;
	guint64      world_version;
	guint64      world_local_version;
	guint64      world_parent_version;
	LrgEntity3D *world_parent;
	guint64      world_epoch;
	gfloat       world_matrix[16];

	/* Coalesced transform-changed bookkeeping */
	gboolean     transform_pending;
} LrgEntity3DPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgEntity3D, lrg_entity3d, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_GUID,
	PROP_NAME,
	PROP_ACTIVE,
	PROP_VISIBLE,
	PROP_STATIC,
	PROP_LAYER,
	PROP_PARENT,
	PROP_WORLD,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_TRANSFORM_CHANGED,
	SIGNAL_CHILD_ADDED,
	SIGNAL_CHILD_REMOVED,
	SIGNAL_COMPONENT_ADDED,
	SIGNAL_COMPONENT_REMOVED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Global transform epoch and world-version counter (see file comment). */
static guint64 transform_epoch = 1;
static guint64 world_version_counter = 0;

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static LrgEntity3DPrivate *
get_priv (LrgEntity3D *self)
{
	return (LrgEntity3DPrivate *)lrg_entity3d_get_instance_private (self);
}

/*
 * transform_mark_changed:
 *
 * Invalidates the entity's local/world caches (and, through the epoch,
 * every descendant's), then queues or emits transform-changed: entities in
 * a world are queued and emitted once at the next phase flush; entities
 * outside a world emit immediately.
 */
static void
transform_mark_changed (LrgEntity3D *self)
{
	LrgEntity3DPrivate *priv;

	priv = get_priv (self);
	priv->local_version++;
	transform_epoch++;

	if (priv->world != NULL)
	{
		/* Always mark the spatial index dirty; queue the signal once. */
		_lrg_world3d_queue_transform (priv->world, self, !priv->transform_pending);
		priv->transform_pending = TRUE;
	}
	else
	{
		g_signal_emit (self, signals[SIGNAL_TRANSFORM_CHANGED], 0);
	}
}

/*
 * ensure_local:
 *
 * Rebuilds the cached local matrix T * R * S if the TRS changed.
 */
static void
ensure_local (LrgEntity3DPrivate *priv)
{
	if (priv->local_matrix_version == priv->local_version)
		return;
	lrg_math3d_compose_quat (priv->position, priv->rotation, priv->scale,
	                         priv->local_matrix);
	priv->local_matrix_version = priv->local_version;
}

/*
 * ensure_world:
 *
 * Validates the cached world matrix (see the file comment).
 */
static void
ensure_world (LrgEntity3D *self)
{
	LrgEntity3DPrivate *priv;
	guint64 parent_version;

	priv = get_priv (self);

	/* Fast path: validated since the last change anywhere. */
	if (priv->world_valid && priv->world_epoch == transform_epoch)
		return;

	/* Validate the parent chain first. */
	parent_version = 0;
	if (priv->parent != NULL)
	{
		ensure_world (priv->parent);
		parent_version = get_priv (priv->parent)->world_version;
	}

	/* Rebuild only if one of our inputs changed. */
	if (!priv->world_valid
	    || priv->world_local_version != priv->local_version
	    || priv->world_parent != priv->parent
	    || priv->world_parent_version != parent_version)
	{
		ensure_local (priv);
		if (priv->parent != NULL)
			lrg_math3d_multiply (get_priv (priv->parent)->world_matrix,
			                     priv->local_matrix, priv->world_matrix);
		else
			memcpy (priv->world_matrix, priv->local_matrix,
			        sizeof (priv->world_matrix));

		priv->world_valid = TRUE;
		priv->world_local_version = priv->local_version;
		priv->world_parent = priv->parent;
		priv->world_parent_version = parent_version;
		priv->world_version = ++world_version_counter;
	}

	priv->world_epoch = transform_epoch;
}

/*
 * world_matrix_of:
 *
 * Internal non-NULL variant of lrg_entity3d_peek_world_matrix().
 */
static const gfloat *
world_matrix_of (LrgEntity3D *self)
{
	ensure_world (self);
	return get_priv (self)->world_matrix;
}

static void
vec3_normalize (gfloat *v)
{
	gfloat len;

	len = sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (len > 1e-12f)
	{
		v[0] /= len;
		v[1] /= len;
		v[2] /= len;
	}
}

/*
 * normalize_quat_or_identity:
 *
 * Normalises @q into @out, turning a (near) zero quaternion into the
 * identity instead of producing NaNs.
 */
static void
normalize_quat_or_identity (const gfloat *q,
                            gfloat       *out)
{
	gfloat len2;

	len2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
	if (len2 < 1e-12f)
	{
		lrg_math3d_quat_identity (out);
		return;
	}
	lrg_math3d_quat_normalize (q, out);
}

/* ==========================================================================
 * World membership
 * ========================================================================== */

/*
 * leave_world_post_order / join_world_pre_order:
 *
 * Walk a subtree telling the old world each entity is leaving (children
 * first, so a parent's components are still present while its children
 * unregister) and the new world each entity joined (parents first).
 */
static void
leave_world_post_order (LrgEntity3D *self)
{
	LrgEntity3DPrivate *priv;
	LrgWorld3D *world;
	guint i;

	priv = get_priv (self);
	for (i = 0; i < priv->children->len; i++)
		leave_world_post_order ((LrgEntity3D *)g_ptr_array_index (priv->children, i));

	world = priv->world;
	if (world == NULL)
		return;

	_lrg_world3d_entity_leaving (world, self);
	priv->world = NULL;
	priv->transform_pending = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WORLD]);
}

static void
join_world_pre_order (LrgEntity3D *self,
                      LrgWorld3D  *world)
{
	LrgEntity3DPrivate *priv;
	guint i;

	priv = get_priv (self);
	priv->world = world;
	_lrg_world3d_entity_joined (world, self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WORLD]);

	for (i = 0; i < priv->children->len; i++)
		join_world_pre_order ((LrgEntity3D *)g_ptr_array_index (priv->children, i),
		                      world);
}

/*
 * _lrg_entity3d_set_world_recursive:
 *
 * Moves a subtree from its current world (if any) to @world (if any).
 * Used by the hierarchy operations below and by LrgWorld3D for its root.
 */
void
_lrg_entity3d_set_world_recursive (LrgEntity3D *self,
                                   LrgWorld3D  *world)
{
	LrgEntity3DPrivate *priv;

	priv = get_priv (self);
	if (priv->world == world)
		return;

	g_object_ref (self);
	if (priv->world != NULL)
		leave_world_post_order (self);
	if (world != NULL)
		join_world_pre_order (self, world);
	g_object_unref (self);
}

/*
 * _lrg_entity3d_emit_transform_changed:
 *
 * Called by the world when it flushes its queue of moved entities.
 */
void
_lrg_entity3d_emit_transform_changed (LrgEntity3D *self)
{
	LrgEntity3DPrivate *priv;

	priv = get_priv (self);
	if (!priv->transform_pending)
		return;
	priv->transform_pending = FALSE;
	g_signal_emit (self, signals[SIGNAL_TRANSFORM_CHANGED], 0);
}

/* ==========================================================================
 * Hierarchy link / unlink
 * ========================================================================== */

/*
 * hierarchy_unlink:
 * @parent: the current parent
 * @child: a child of @parent
 * @next_world: the world @child will be in afterwards (it only leaves its
 *   current world if this differs, so moves inside one world do not churn
 *   the GUID map or the components' world hooks)
 *
 * Removes @child from @parent.  The parent's reference is transferred to
 * the caller.
 */
static void
hierarchy_unlink (LrgEntity3D *parent,
                  LrgEntity3D *child,
                  LrgWorld3D  *next_world)
{
	LrgEntity3DPrivate *ppriv;
	LrgEntity3DPrivate *cpriv;
	guint index;

	ppriv = get_priv (parent);
	cpriv = get_priv (child);

	if (cpriv->world != next_world)
		_lrg_entity3d_set_world_recursive (child, NULL);

	if (g_ptr_array_find (ppriv->children, child, &index))
		(void)g_ptr_array_steal_index (ppriv->children, index);

	cpriv->parent = NULL;
	transform_mark_changed (child);

	g_object_notify_by_pspec (G_OBJECT (child), properties[PROP_PARENT]);
	g_signal_emit (parent, signals[SIGNAL_CHILD_REMOVED], 0, child);
}

/*
 * hierarchy_link:
 *
 * Inserts @child (whose reference the caller transfers) at @index (-1 or
 * past the end appends) and moves it into @parent's world.
 */
static void
hierarchy_link (LrgEntity3D *parent,
                LrgEntity3D *child,
                gint         index)
{
	LrgEntity3DPrivate *ppriv;
	LrgEntity3DPrivate *cpriv;

	ppriv = get_priv (parent);
	cpriv = get_priv (child);

	if (index < 0 || (guint)index >= ppriv->children->len)
		g_ptr_array_add (ppriv->children, child);
	else
		g_ptr_array_insert (ppriv->children, index, child);

	cpriv->parent = parent;
	transform_mark_changed (child);

	if (cpriv->world != ppriv->world)
		_lrg_entity3d_set_world_recursive (child, ppriv->world);

	g_object_notify_by_pspec (G_OBJECT (child), properties[PROP_PARENT]);
	g_signal_emit (parent, signals[SIGNAL_CHILD_ADDED], 0, child);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_entity3d_dispose (GObject *object)
{
	LrgEntity3D *self;
	LrgEntity3DPrivate *priv;

	self = LRG_ENTITY3D (object);
	priv = get_priv (self);

	/* Components: detach in reverse order so later components (which may
	 * depend on earlier ones) go first. */
	while (priv->components->len > 0)
	{
		g_autoptr(LrgComponent) component = NULL;

		component = (LrgComponent *)g_ptr_array_steal_index (
			priv->components, priv->components->len - 1);
		_lrg_component_set_entity (component, NULL);
	}

	/* Children: drop the parent link before releasing them. */
	while (priv->children->len > 0)
	{
		LrgEntity3D *child;

		child = (LrgEntity3D *)g_ptr_array_steal_index (
			priv->children, priv->children->len - 1);
		get_priv (child)->parent = NULL;
		g_object_unref (child);
	}

	G_OBJECT_CLASS (lrg_entity3d_parent_class)->dispose (object);
}

static void
lrg_entity3d_finalize (GObject *object)
{
	LrgEntity3DPrivate *priv;

	priv = get_priv (LRG_ENTITY3D (object));

	g_clear_pointer (&priv->guid, g_free);
	g_clear_pointer (&priv->name, g_free);
	g_clear_pointer (&priv->tags, g_hash_table_unref);
	g_clear_pointer (&priv->children, g_ptr_array_unref);
	g_clear_pointer (&priv->components, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_entity3d_parent_class)->finalize (object);
}

static void
lrg_entity3d_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgEntity3D *self;
	LrgEntity3DPrivate *priv;

	self = LRG_ENTITY3D (object);
	priv = get_priv (self);

	switch (prop_id)
	{
	case PROP_GUID:
		g_value_set_string (value, priv->guid);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_ACTIVE:
		g_value_set_boolean (value, priv->active);
		break;
	case PROP_VISIBLE:
		g_value_set_boolean (value, priv->visible);
		break;
	case PROP_STATIC:
		g_value_set_boolean (value, priv->is_static);
		break;
	case PROP_LAYER:
		g_value_set_uint (value, priv->layer);
		break;
	case PROP_PARENT:
		g_value_set_object (value, priv->parent);
		break;
	case PROP_WORLD:
		g_value_set_object (value, priv->world);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_entity3d_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgEntity3D *self;

	self = LRG_ENTITY3D (object);

	switch (prop_id)
	{
	case PROP_GUID:
		if (g_value_get_string (value) != NULL)
			lrg_entity3d_set_guid (self, g_value_get_string (value));
		break;
	case PROP_NAME:
		lrg_entity3d_set_name (self, g_value_get_string (value));
		break;
	case PROP_ACTIVE:
		lrg_entity3d_set_active (self, g_value_get_boolean (value));
		break;
	case PROP_VISIBLE:
		lrg_entity3d_set_visible (self, g_value_get_boolean (value));
		break;
	case PROP_STATIC:
		lrg_entity3d_set_static (self, g_value_get_boolean (value));
		break;
	case PROP_LAYER:
		lrg_entity3d_set_layer (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_entity3d_class_init (LrgEntity3DClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = lrg_entity3d_dispose;
	object_class->finalize = lrg_entity3d_finalize;
	object_class->get_property = lrg_entity3d_get_property;
	object_class->set_property = lrg_entity3d_set_property;

	/**
	 * LrgEntity3D:guid:
	 *
	 * Stable identifier; can be set once (see lrg_entity3d_set_guid()).
	 */
	properties[PROP_GUID] =
		g_param_spec_string ("guid", "GUID", "Stable identifier", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEntity3D:name:
	 *
	 * Display and lookup name (not unique).
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name", "Name", "Entity name", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEntity3D:active:
	 *
	 * Whether the entity (and its subtree) takes part in world phases.
	 */
	properties[PROP_ACTIVE] =
		g_param_spec_boolean ("active", "Active", "Takes part in updates",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEntity3D:visible:
	 *
	 * Whether the entity (and its subtree) is collected for rendering.
	 */
	properties[PROP_VISIBLE] =
		g_param_spec_boolean ("visible", "Visible", "Collected for rendering",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEntity3D:static:
	 *
	 * Hint that the entity never moves at runtime.
	 */
	properties[PROP_STATIC] =
		g_param_spec_boolean ("static", "Static", "Never moves at runtime",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                      G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEntity3D:layer:
	 *
	 * Render layer index, 0..31.
	 */
	properties[PROP_LAYER] =
		g_param_spec_uint ("layer", "Layer", "Render layer index",
		                   0, LRG_RENDER_LAYERS_COUNT - 1,
		                   LRG_RENDER_LAYERS_DEFAULT,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
		                   G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEntity3D:parent:
	 *
	 * The parent entity (read-only; use lrg_entity3d_set_parent()).
	 */
	properties[PROP_PARENT] =
		g_param_spec_object ("parent", "Parent", "Parent entity",
		                     LRG_TYPE_ENTITY3D,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgEntity3D:world:
	 *
	 * The #LrgWorld3D the entity is in (read-only).
	 */
	properties[PROP_WORLD] =
		g_param_spec_object ("world", "World", "World the entity is in",
		                     LRG_TYPE_WORLD3D,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
		                     G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgEntity3D::transform-changed:
	 * @self: the entity
	 *
	 * Emitted when the entity's own local transform changed or it was
	 * reparented.  Coalesced: inside an #LrgWorld3D it is emitted at most
	 * once per phase flush (after update, each fixed substep, late update,
	 * and before render collection), however many setters ran; outside a
	 * world it is emitted synchronously by each setter.  Descendants whose
	 * world matrix changed as a consequence do not emit; compare
	 * lrg_entity3d_get_world_version() if you need that.
	 */
	signals[SIGNAL_TRANSFORM_CHANGED] =
		g_signal_new ("transform-changed", G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (LrgEntity3DClass, transform_changed),
		              NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgEntity3D::child-added:
	 * @self: the parent
	 * @child: the new child
	 *
	 * Emitted after @child was linked under @self.
	 */
	signals[SIGNAL_CHILD_ADDED] =
		g_signal_new ("child-added", G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (LrgEntity3DClass, child_added),
		              NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);

	/**
	 * LrgEntity3D::child-removed:
	 * @self: the former parent
	 * @child: the removed child (still alive during emission)
	 *
	 * Emitted after @child was unlinked from @self.
	 */
	signals[SIGNAL_CHILD_REMOVED] =
		g_signal_new ("child-removed", G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (LrgEntity3DClass, child_removed),
		              NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_ENTITY3D);

	/**
	 * LrgEntity3D::component-added:
	 * @self: the entity
	 * @component: the component
	 *
	 * Emitted after a component was attached (after its attached_entity
	 * and world_entered hooks ran).
	 */
	signals[SIGNAL_COMPONENT_ADDED] =
		g_signal_new ("component-added", G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_COMPONENT);

	/**
	 * LrgEntity3D::component-removed:
	 * @self: the entity
	 * @component: the component (still alive during emission)
	 *
	 * Emitted after a component was detached.
	 */
	signals[SIGNAL_COMPONENT_REMOVED] =
		g_signal_new ("component-removed", G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, LRG_TYPE_COMPONENT);
}

static void
lrg_entity3d_init (LrgEntity3D *self)
{
	LrgEntity3DPrivate *priv;

	priv = get_priv (self);
	priv->active = TRUE;
	priv->visible = TRUE;
	priv->is_static = FALSE;
	priv->layer = LRG_RENDER_LAYERS_DEFAULT;

	priv->position[0] = priv->position[1] = priv->position[2] = 0.0f;
	lrg_math3d_quat_identity (priv->rotation);
	priv->scale[0] = priv->scale[1] = priv->scale[2] = 1.0f;
	priv->euler[0] = priv->euler[1] = priv->euler[2] = 0.0f;
	priv->euler_valid = TRUE;

	priv->children = g_ptr_array_new_with_free_func (g_object_unref);
	priv->components = g_ptr_array_new_with_free_func (g_object_unref);

	/* local_matrix_version 0 != local_version 1 forces the first build. */
	priv->local_version = 1;
	priv->local_matrix_version = 0;
	priv->world_valid = FALSE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgEntity3D *
lrg_entity3d_new (void)
{
	return (LrgEntity3D *)g_object_new (LRG_TYPE_ENTITY3D, NULL);
}

LrgEntity3D *
lrg_entity3d_new_with_name (const gchar *name)
{
	return (LrgEntity3D *)g_object_new (LRG_TYPE_ENTITY3D, "name", name, NULL);
}

/* ==========================================================================
 * Identity and flags
 * ========================================================================== */

const gchar *
lrg_entity3d_get_guid (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);
	return get_priv (self)->guid;
}

gboolean
lrg_entity3d_set_guid (LrgEntity3D *self,
                       const gchar *guid)
{
	LrgEntity3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (guid != NULL && guid[0] != '\0', FALSE);

	priv = get_priv (self);

	if (priv->guid != NULL)
	{
		if (g_strcmp0 (priv->guid, guid) == 0)
			return TRUE;
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D,
		             "Entity '%s' already has GUID %s; refusing to change it to %s",
		             priv->name != NULL ? priv->name : "(unnamed)",
		             priv->guid, guid);
		return FALSE;
	}

	priv->guid = g_strdup (guid);
	if (priv->world != NULL)
		_lrg_world3d_guid_changed (priv->world, self, NULL);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GUID]);
	return TRUE;
}

const gchar *
lrg_entity3d_ensure_guid (LrgEntity3D *self)
{
	LrgEntity3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);

	priv = get_priv (self);
	if (priv->guid == NULL)
	{
		g_autofree gchar *uuid = g_uuid_string_random ();

		lrg_entity3d_set_guid (self, uuid);
	}
	return priv->guid;
}

const gchar *
lrg_entity3d_get_name (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);
	return get_priv (self)->name;
}

void
lrg_entity3d_set_name (LrgEntity3D *self,
                       const gchar *name)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));

	priv = get_priv (self);
	if (g_strcmp0 (priv->name, name) == 0)
		return;
	g_free (priv->name);
	priv->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

gboolean
lrg_entity3d_get_active (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	return get_priv (self)->active;
}

void
lrg_entity3d_set_active (LrgEntity3D *self,
                         gboolean     active)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));

	priv = get_priv (self);
	active = !!active;
	if (priv->active == active)
		return;
	priv->active = active;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
}

gboolean
lrg_entity3d_is_active_in_hierarchy (LrgEntity3D *self)
{
	LrgEntity3D *e;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);

	for (e = self; e != NULL; e = get_priv (e)->parent)
	{
		if (!get_priv (e)->active)
			return FALSE;
	}
	return TRUE;
}

gboolean
lrg_entity3d_get_visible (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	return get_priv (self)->visible;
}

void
lrg_entity3d_set_visible (LrgEntity3D *self,
                          gboolean     visible)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));

	priv = get_priv (self);
	visible = !!visible;
	if (priv->visible == visible)
		return;
	priv->visible = visible;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
}

gboolean
lrg_entity3d_is_visible_in_hierarchy (LrgEntity3D *self)
{
	LrgEntity3D *e;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);

	for (e = self; e != NULL; e = get_priv (e)->parent)
	{
		if (!get_priv (e)->visible)
			return FALSE;
	}
	return TRUE;
}

gboolean
lrg_entity3d_get_static (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	return get_priv (self)->is_static;
}

void
lrg_entity3d_set_static (LrgEntity3D *self,
                         gboolean     is_static)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));

	priv = get_priv (self);
	is_static = !!is_static;
	if (priv->is_static == is_static)
		return;
	priv->is_static = is_static;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATIC]);
}

guint
lrg_entity3d_get_layer (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), 0);
	return get_priv (self)->layer;
}

void
lrg_entity3d_set_layer (LrgEntity3D *self,
                        guint        layer)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (layer < LRG_RENDER_LAYERS_COUNT);

	priv = get_priv (self);
	if (priv->layer == layer)
		return;
	priv->layer = layer;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYER]);
}

guint32
lrg_entity3d_get_layer_mask (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), 0);
	return LRG_RENDER_LAYERS_BIT (get_priv (self)->layer);
}

/* ==========================================================================
 * Tags
 * ========================================================================== */

void
lrg_entity3d_add_tag (LrgEntity3D *self,
                      const gchar *tag)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (tag != NULL);

	priv = get_priv (self);
	if (priv->tags == NULL)
		priv->tags = g_hash_table_new_full (g_str_hash, g_str_equal,
		                                    g_free, NULL);
	g_hash_table_add (priv->tags, g_strdup (tag));
}

gboolean
lrg_entity3d_remove_tag (LrgEntity3D *self,
                         const gchar *tag)
{
	LrgEntity3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (tag != NULL, FALSE);

	priv = get_priv (self);
	if (priv->tags == NULL)
		return FALSE;
	return g_hash_table_remove (priv->tags, tag);
}

gboolean
lrg_entity3d_has_tag (LrgEntity3D *self,
                      const gchar *tag)
{
	LrgEntity3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (tag != NULL, FALSE);

	priv = get_priv (self);
	return priv->tags != NULL && g_hash_table_contains (priv->tags, tag);
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

gchar **
lrg_entity3d_get_tags (LrgEntity3D *self)
{
	LrgEntity3DPrivate *priv;
	GPtrArray *out;
	GHashTableIter iter;
	gpointer key;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);

	priv = get_priv (self);
	out = g_ptr_array_new ();
	if (priv->tags != NULL)
	{
		g_hash_table_iter_init (&iter, priv->tags);
		while (g_hash_table_iter_next (&iter, &key, NULL))
			g_ptr_array_add (out, g_strdup ((const gchar *)key));
	}
	g_ptr_array_sort (out, compare_strings);
	g_ptr_array_add (out, NULL);
	return (gchar **)g_ptr_array_free (out, FALSE);
}

/* ==========================================================================
 * Local transform
 * ========================================================================== */

void
lrg_entity3d_get_position (LrgEntity3D *self,
                           gfloat      *out)
{
	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);
	memcpy (out, get_priv (self)->position, sizeof (gfloat) * 3);
}

void
lrg_entity3d_set_position (LrgEntity3D  *self,
                           const gfloat *position)
{
	g_return_if_fail (position != NULL);
	lrg_entity3d_set_position_xyz (self, position[0], position[1], position[2]);
}

void
lrg_entity3d_set_position_xyz (LrgEntity3D *self,
                               gfloat       x,
                               gfloat       y,
                               gfloat       z)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));

	priv = get_priv (self);
	if (priv->position[0] == x && priv->position[1] == y && priv->position[2] == z)
		return;
	priv->position[0] = x;
	priv->position[1] = y;
	priv->position[2] = z;
	transform_mark_changed (self);
}

void
lrg_entity3d_get_rotation (LrgEntity3D *self,
                           gfloat      *out)
{
	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);
	memcpy (out, get_priv (self)->rotation, sizeof (gfloat) * 4);
}

void
lrg_entity3d_set_rotation (LrgEntity3D  *self,
                           const gfloat *rotation)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (rotation != NULL);

	priv = get_priv (self);
	normalize_quat_or_identity (rotation, priv->rotation);
	priv->euler_valid = FALSE;
	transform_mark_changed (self);
}

void
lrg_entity3d_get_rotation_euler (LrgEntity3D *self,
                                 gfloat      *out)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);

	priv = get_priv (self);
	if (priv->euler_valid)
		memcpy (out, priv->euler, sizeof (gfloat) * 3);
	else
		lrg_math3d_quat_to_euler (priv->rotation, out);
}

void
lrg_entity3d_set_rotation_euler (LrgEntity3D  *self,
                                 const gfloat *euler)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (euler != NULL);

	priv = get_priv (self);
	memcpy (priv->euler, euler, sizeof (gfloat) * 3);
	priv->euler_valid = TRUE;
	lrg_math3d_euler_to_quat (euler, priv->rotation);
	transform_mark_changed (self);
}

gboolean
lrg_entity3d_has_authored_euler (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	return get_priv (self)->euler_valid;
}

void
lrg_entity3d_get_scale (LrgEntity3D *self,
                        gfloat      *out)
{
	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);
	memcpy (out, get_priv (self)->scale, sizeof (gfloat) * 3);
}

void
lrg_entity3d_set_scale (LrgEntity3D  *self,
                        const gfloat *scale)
{
	g_return_if_fail (scale != NULL);
	lrg_entity3d_set_scale_xyz (self, scale[0], scale[1], scale[2]);
}

void
lrg_entity3d_set_scale_xyz (LrgEntity3D *self,
                            gfloat       x,
                            gfloat       y,
                            gfloat       z)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));

	priv = get_priv (self);
	if (priv->scale[0] == x && priv->scale[1] == y && priv->scale[2] == z)
		return;
	priv->scale[0] = x;
	priv->scale[1] = y;
	priv->scale[2] = z;
	transform_mark_changed (self);
}

void
lrg_entity3d_set_local_trs (LrgEntity3D  *self,
                            const gfloat *position,
                            const gfloat *rotation,
                            const gfloat *scale)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (position != NULL && rotation != NULL && scale != NULL);

	priv = get_priv (self);
	memcpy (priv->position, position, sizeof (gfloat) * 3);
	normalize_quat_or_identity (rotation, priv->rotation);
	memcpy (priv->scale, scale, sizeof (gfloat) * 3);
	priv->euler_valid = FALSE;
	transform_mark_changed (self);
}

void
lrg_entity3d_get_local_matrix (LrgEntity3D *self,
                               gfloat      *out)
{
	LrgEntity3DPrivate *priv;

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);

	priv = get_priv (self);
	ensure_local (priv);
	memcpy (out, priv->local_matrix, sizeof (priv->local_matrix));
}

void
lrg_entity3d_set_local_matrix (LrgEntity3D  *self,
                               const gfloat *matrix)
{
	gfloat t[3];
	gfloat r[4];
	gfloat s[3];

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (matrix != NULL);

	lrg_math3d_decompose_quat (matrix, t, r, s);
	lrg_entity3d_set_local_trs (self, t, r, s);
}

/* ==========================================================================
 * World transform
 * ========================================================================== */

void
lrg_entity3d_get_world_matrix (LrgEntity3D *self,
                               gfloat      *out)
{
	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);

	ensure_world (self);
	memcpy (out, get_priv (self)->world_matrix, sizeof (gfloat) * 16);
}

const gfloat *
lrg_entity3d_peek_world_matrix (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);

	ensure_world (self);
	return get_priv (self)->world_matrix;
}

guint64
lrg_entity3d_get_world_version (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), 0);

	ensure_world (self);
	return get_priv (self)->world_version;
}

void
lrg_entity3d_get_world_position (LrgEntity3D *self,
                                 gfloat      *out)
{
	const gfloat *m;

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);

	m = world_matrix_of (self);
	out[0] = m[12];
	out[1] = m[13];
	out[2] = m[14];
}

void
lrg_entity3d_set_world_position (LrgEntity3D  *self,
                                 const gfloat *position)
{
	LrgEntity3DPrivate *priv;
	gfloat inv[16];
	gfloat local[3];

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (position != NULL);

	priv = get_priv (self);
	if (priv->parent == NULL)
	{
		lrg_entity3d_set_position (self, position);
		return;
	}

	/* local = inverse(parent world) * world */
	if (!lrg_math3d_invert (world_matrix_of (priv->parent), inv))
	{
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D,
		             "set_world_position: parent world matrix is singular");
		return;
	}
	lrg_math3d_transform_point (inv, position, local);
	lrg_entity3d_set_position (self, local);
}

void
lrg_entity3d_get_world_rotation (LrgEntity3D *self,
                                 gfloat      *out)
{
	LrgEntity3D *e;
	gfloat q[4];

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);

	/* world = q_root * ... * q_parent * q_self */
	memcpy (q, get_priv (self)->rotation, sizeof (q));
	for (e = get_priv (self)->parent; e != NULL; e = get_priv (e)->parent)
		lrg_math3d_quat_multiply (get_priv (e)->rotation, q, q);
	normalize_quat_or_identity (q, out);
}

void
lrg_entity3d_set_world_rotation (LrgEntity3D  *self,
                                 const gfloat *rotation)
{
	LrgEntity3DPrivate *priv;
	gfloat parent_q[4];
	gfloat inv[4];
	gfloat local[4];

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (rotation != NULL);

	priv = get_priv (self);
	if (priv->parent == NULL)
	{
		lrg_entity3d_set_rotation (self, rotation);
		return;
	}

	/* local = inverse(parent world rotation) * world */
	lrg_entity3d_get_world_rotation (priv->parent, parent_q);
	lrg_math3d_quat_invert (parent_q, inv);
	lrg_math3d_quat_multiply (inv, rotation, local);
	lrg_entity3d_set_rotation (self, local);
}

void
lrg_entity3d_get_world_scale (LrgEntity3D *self,
                              gfloat      *out)
{
	gfloat t[3];
	gfloat r[4];

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);

	lrg_math3d_decompose_quat (world_matrix_of (self), t, r, out);
}

void
lrg_entity3d_set_world_matrix (LrgEntity3D  *self,
                               const gfloat *matrix)
{
	LrgEntity3DPrivate *priv;
	gfloat inv[16];
	gfloat local[16];

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (matrix != NULL);

	priv = get_priv (self);
	if (priv->parent == NULL)
	{
		lrg_entity3d_set_local_matrix (self, matrix);
		return;
	}

	if (!lrg_math3d_invert (world_matrix_of (priv->parent), inv))
	{
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D,
		             "set_world_matrix: parent world matrix is singular");
		return;
	}
	lrg_math3d_multiply (inv, matrix, local);
	lrg_entity3d_set_local_matrix (self, local);
}

void
lrg_entity3d_transform_point (LrgEntity3D  *self,
                              const gfloat *local_point,
                              gfloat       *out)
{
	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (local_point != NULL && out != NULL);

	lrg_math3d_transform_point (world_matrix_of (self),
	                            local_point, out);
}

void
lrg_entity3d_inverse_transform_point (LrgEntity3D  *self,
                                      const gfloat *world_point,
                                      gfloat       *out)
{
	gfloat inv[16];

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (world_point != NULL && out != NULL);

	if (!lrg_math3d_invert (world_matrix_of (self), inv))
	{
		memcpy (out, world_point, sizeof (gfloat) * 3);
		return;
	}
	lrg_math3d_transform_point (inv, world_point, out);
}

/*
 * world_axis:
 *
 * Transforms a local direction by the world matrix and normalises it.
 */
static void
world_axis (LrgEntity3D *self,
            gfloat       x,
            gfloat       y,
            gfloat       z,
            gfloat      *out)
{
	gfloat d[3];

	d[0] = x;
	d[1] = y;
	d[2] = z;
	lrg_math3d_transform_direction (world_matrix_of (self), d, out);
	vec3_normalize (out);
}

void
lrg_entity3d_get_forward (LrgEntity3D *self,
                          gfloat      *out)
{
	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);
	world_axis (self, 0.0f, 0.0f, -1.0f, out);
}

void
lrg_entity3d_get_up (LrgEntity3D *self,
                     gfloat      *out)
{
	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);
	world_axis (self, 0.0f, 1.0f, 0.0f, out);
}

void
lrg_entity3d_get_right (LrgEntity3D *self,
                        gfloat      *out)
{
	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (out != NULL);
	world_axis (self, 1.0f, 0.0f, 0.0f, out);
}

/* ==========================================================================
 * Hierarchy
 * ========================================================================== */

LrgEntity3D *
lrg_entity3d_get_parent (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);
	return get_priv (self)->parent;
}

LrgWorld3D *
lrg_entity3d_get_world (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);
	return get_priv (self)->world;
}

LrgEntity3D *
lrg_entity3d_get_root (LrgEntity3D *self)
{
	LrgEntity3D *e;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);

	e = self;
	while (get_priv (e)->parent != NULL)
		e = get_priv (e)->parent;
	return e;
}

guint
lrg_entity3d_get_n_children (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), 0);
	return get_priv (self)->children->len;
}

LrgEntity3D *
lrg_entity3d_get_child (LrgEntity3D *self,
                        guint        index)
{
	LrgEntity3DPrivate *priv;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);

	priv = get_priv (self);
	if (index >= priv->children->len)
		return NULL;
	return (LrgEntity3D *)g_ptr_array_index (priv->children, index);
}

GPtrArray *
lrg_entity3d_get_children (LrgEntity3D *self)
{
	LrgEntity3DPrivate *priv;
	GPtrArray *out;
	guint i;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);

	priv = get_priv (self);
	out = g_ptr_array_sized_new (priv->children->len);
	for (i = 0; i < priv->children->len; i++)
		g_ptr_array_add (out, g_ptr_array_index (priv->children, i));
	return out;
}

gint
lrg_entity3d_get_index (LrgEntity3D *self)
{
	LrgEntity3DPrivate *priv;
	guint index;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), -1);

	priv = get_priv (self);
	if (priv->parent == NULL)
		return -1;
	if (!g_ptr_array_find (get_priv (priv->parent)->children, self, &index))
		return -1;
	return (gint)index;
}

gboolean
lrg_entity3d_is_ancestor_of (LrgEntity3D *self,
                             LrgEntity3D *descendant)
{
	LrgEntity3D *e;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (LRG_IS_ENTITY3D (descendant), FALSE);

	for (e = get_priv (descendant)->parent; e != NULL; e = get_priv (e)->parent)
	{
		if (e == self)
			return TRUE;
	}
	return FALSE;
}

void
lrg_entity3d_add_child (LrgEntity3D *self,
                        LrgEntity3D *child)
{
	lrg_entity3d_insert_child (self, child, -1);
}

void
lrg_entity3d_insert_child (LrgEntity3D *self,
                           LrgEntity3D *child,
                           gint         index)
{
	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (LRG_IS_ENTITY3D (child));
	g_return_if_fail (child != self);
	g_return_if_fail (get_priv (child)->parent == NULL);

	if (lrg_entity3d_is_ancestor_of (child, self))
	{
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D,
		             "insert_child: an entity cannot become its own descendant");
		return;
	}

	hierarchy_link (self, (LrgEntity3D *)g_object_ref (child), index);
}

gboolean
lrg_entity3d_remove_child (LrgEntity3D *self,
                           LrgEntity3D *child)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (LRG_IS_ENTITY3D (child), FALSE);

	if (get_priv (child)->parent != self)
		return FALSE;

	hierarchy_unlink (self, child, NULL);
	g_object_unref (child);
	return TRUE;
}

gboolean
lrg_entity3d_set_parent (LrgEntity3D *self,
                         LrgEntity3D *parent,
                         gboolean     keep_world)
{
	LrgEntity3DPrivate *priv;
	gfloat world_m[16];
	LrgWorld3D *next_world;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (parent == NULL || LRG_IS_ENTITY3D (parent), FALSE);

	priv = get_priv (self);

	if (parent == self || (parent != NULL && lrg_entity3d_is_ancestor_of (self, parent)))
	{
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D,
		             "set_parent: an entity cannot become its own descendant");
		return FALSE;
	}

	/* Capture the world matrix before the hierarchy changes. */
	if (keep_world)
		lrg_entity3d_get_world_matrix (self, world_m);

	next_world = parent != NULL ? get_priv (parent)->world : NULL;

	/* Same parent: just move to the end. */
	if (priv->parent == parent && parent != NULL)
	{
		lrg_entity3d_set_sibling_index (self, G_MAXUINT);
		return TRUE;
	}

	/* Hold our own ref across the move: unlink transfers the old parent's
	 * reference to us, link takes it again. */
	if (priv->parent != NULL)
		hierarchy_unlink (priv->parent, self, next_world);
	else
		g_object_ref (self);

	if (parent != NULL)
		hierarchy_link (parent, self, -1);
	else
		g_object_unref (self);   /* detached: the caller's refs remain */

	if (keep_world)
		lrg_entity3d_set_world_matrix (self, world_m);

	return TRUE;
}

void
lrg_entity3d_set_sibling_index (LrgEntity3D *self,
                                guint        index)
{
	LrgEntity3DPrivate *priv;
	GPtrArray *siblings;
	guint current;

	g_return_if_fail (LRG_IS_ENTITY3D (self));

	priv = get_priv (self);
	g_return_if_fail (priv->parent != NULL);

	siblings = get_priv (priv->parent)->children;
	if (!g_ptr_array_find (siblings, self, &current))
		return;

	if (index >= siblings->len)
		index = siblings->len - 1;
	if (index == current)
		return;

	(void)g_ptr_array_steal_index (siblings, current);
	g_ptr_array_insert (siblings, (gint)index, self);
}

LrgEntity3D *
lrg_entity3d_find_child (LrgEntity3D *self,
                         const gchar *name,
                         gboolean     recursive)
{
	LrgEntity3DPrivate *priv;
	guint i;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	priv = get_priv (self);
	for (i = 0; i < priv->children->len; i++)
	{
		LrgEntity3D *child;

		child = (LrgEntity3D *)g_ptr_array_index (priv->children, i);
		if (g_strcmp0 (get_priv (child)->name, name) == 0)
			return child;
		if (recursive)
		{
			LrgEntity3D *found;

			found = lrg_entity3d_find_child (child, name, TRUE);
			if (found != NULL)
				return found;
		}
	}
	return NULL;
}

LrgEntity3D *
lrg_entity3d_find_by_path (LrgEntity3D *self,
                           const gchar *path)
{
	g_auto(GStrv) parts = NULL;
	LrgEntity3D *cur;
	guint i;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	parts = g_strsplit (path, "/", -1);
	cur = self;
	for (i = 0; parts[i] != NULL && cur != NULL; i++)
	{
		const gchar *seg;

		seg = parts[i];
		if (seg[0] == '\0' || g_strcmp0 (seg, ".") == 0)
			continue;
		if (g_strcmp0 (seg, "..") == 0)
		{
			cur = get_priv (cur)->parent;
			continue;
		}
		/* "#<index>" addresses unnamed children (see get_path). */
		if (seg[0] == '#' && seg[1] != '\0')
		{
			gchar *end;
			guint64 idx;

			idx = g_ascii_strtoull (seg + 1, &end, 10);
			if (*end == '\0')
			{
				cur = lrg_entity3d_get_child (cur, (guint)idx);
				continue;
			}
		}
		cur = lrg_entity3d_find_child (cur, seg, FALSE);
	}
	return cur;
}

gchar *
lrg_entity3d_get_path (LrgEntity3D *self)
{
	GPtrArray *segs;
	GString *out;
	LrgEntity3D *e;
	gint i;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);

	/* Collect names from self up to (excluding) the topmost ancestor. */
	segs = g_ptr_array_new_with_free_func (g_free);
	for (e = self; get_priv (e)->parent != NULL; e = get_priv (e)->parent)
	{
		const gchar *name;

		name = get_priv (e)->name;
		if (name != NULL && name[0] != '\0')
			g_ptr_array_add (segs, g_strdup (name));
		else
			g_ptr_array_add (segs, g_strdup_printf ("#%d", lrg_entity3d_get_index (e)));
	}

	out = g_string_new (NULL);
	for (i = (gint)segs->len - 1; i >= 0; i--)
	{
		g_string_append (out, (const gchar *)g_ptr_array_index (segs, i));
		if (i > 0)
			g_string_append_c (out, '/');
	}
	g_ptr_array_unref (segs);
	return g_string_free (out, FALSE);
}

void
lrg_entity3d_foreach (LrgEntity3D          *self,
                      LrgEntity3DVisitFunc  func,
                      gpointer              user_data)
{
	LrgEntity3DPrivate *priv;
	guint i;

	g_return_if_fail (LRG_IS_ENTITY3D (self));
	g_return_if_fail (func != NULL);

	if (!func (self, user_data))
		return;

	priv = get_priv (self);
	for (i = 0; i < priv->children->len; i++)
		lrg_entity3d_foreach ((LrgEntity3D *)g_ptr_array_index (priv->children, i),
		                      func, user_data);
}

/* ==========================================================================
 * Components
 * ========================================================================== */

gboolean
lrg_entity3d_add_component (LrgEntity3D  *self,
                            LrgComponent *component)
{
	LrgEntity3DPrivate *priv;
	LrgComponentClass *klass;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (LRG_IS_COMPONENT (component), FALSE);

	if (lrg_component_get_owner_object (component) != NULL)
	{
		lrg_warning (LRG_LOG_DOMAIN_WORLD3D,
		             "add_component: %s already has an owner",
		             G_OBJECT_TYPE_NAME (component));
		return FALSE;
	}

	priv = get_priv (self);
	g_ptr_array_add (priv->components, g_object_ref (component));
	_lrg_component_set_entity (component, self);

	klass = LRG_COMPONENT_GET_CLASS (component);
	if (priv->world != NULL && klass->world_entered != NULL)
		klass->world_entered (component, priv->world);

	g_signal_emit (self, signals[SIGNAL_COMPONENT_ADDED], 0, component);
	return TRUE;
}

gboolean
lrg_entity3d_remove_component (LrgEntity3D  *self,
                               LrgComponent *component)
{
	LrgEntity3DPrivate *priv;
	LrgComponentClass *klass;
	guint index;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (LRG_IS_COMPONENT (component), FALSE);

	priv = get_priv (self);
	if (!g_ptr_array_find (priv->components, component, &index))
		return FALSE;

	klass = LRG_COMPONENT_GET_CLASS (component);
	if (priv->world != NULL && klass->world_exited != NULL)
		klass->world_exited (component, priv->world);
	_lrg_component_set_entity (component, NULL);

	/* The stolen reference keeps the component alive during emission. */
	(void)g_ptr_array_steal_index (priv->components, index);
	g_signal_emit (self, signals[SIGNAL_COMPONENT_REMOVED], 0, component);
	g_object_unref (component);
	return TRUE;
}

LrgComponent *
lrg_entity3d_get_component (LrgEntity3D *self,
                            GType        component_type)
{
	LrgEntity3DPrivate *priv;
	guint i;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);

	priv = get_priv (self);
	for (i = 0; i < priv->components->len; i++)
	{
		gpointer c;

		c = g_ptr_array_index (priv->components, i);
		if (G_TYPE_CHECK_INSTANCE_TYPE (c, component_type))
			return (LrgComponent *)c;
	}
	return NULL;
}

GPtrArray *
lrg_entity3d_get_components_of_type (LrgEntity3D *self,
                                     GType        component_type)
{
	LrgEntity3DPrivate *priv;
	GPtrArray *out;
	guint i;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), NULL);

	priv = get_priv (self);
	out = g_ptr_array_new ();
	for (i = 0; i < priv->components->len; i++)
	{
		gpointer c;

		c = g_ptr_array_index (priv->components, i);
		if (G_TYPE_CHECK_INSTANCE_TYPE (c, component_type))
			g_ptr_array_add (out, c);
	}
	return out;
}

gboolean
lrg_entity3d_has_component (LrgEntity3D *self,
                            GType        component_type)
{
	return lrg_entity3d_get_component (self, component_type) != NULL;
}

GPtrArray *
lrg_entity3d_get_components (LrgEntity3D *self)
{
	return lrg_entity3d_get_components_of_type (self, LRG_TYPE_COMPONENT);
}

guint
lrg_entity3d_get_n_components (LrgEntity3D *self)
{
	g_return_val_if_fail (LRG_IS_ENTITY3D (self), 0);
	return get_priv (self)->components->len;
}

/* ==========================================================================
 * Bounds
 * ========================================================================== */

gboolean
lrg_entity3d_get_local_bounds (LrgEntity3D      *self,
                               LrgBoundingBox3D *out)
{
	LrgEntity3DPrivate *priv;
	gboolean any;
	guint i;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	priv = get_priv (self);
	any = FALSE;
	memset (out, 0, sizeof (*out));

	for (i = 0; i < priv->components->len; i++)
	{
		LrgComponent *c;
		LrgBoundingBox3D b;

		c = (LrgComponent *)g_ptr_array_index (priv->components, i);
		if (!lrg_component_get_enabled (c))
			continue;
		if (!lrg_component_get_bounds (c, &b))
			continue;

		if (!any)
		{
			*out = b;
			any = TRUE;
			continue;
		}
		out->min.x = MIN (out->min.x, b.min.x);
		out->min.y = MIN (out->min.y, b.min.y);
		out->min.z = MIN (out->min.z, b.min.z);
		out->max.x = MAX (out->max.x, b.max.x);
		out->max.y = MAX (out->max.y, b.max.y);
		out->max.z = MAX (out->max.z, b.max.z);
	}
	return any;
}

gboolean
lrg_entity3d_get_world_bounds (LrgEntity3D      *self,
                               LrgBoundingBox3D *out)
{
	LrgBoundingBox3D local;
	const gfloat *m;
	guint corner;

	g_return_val_if_fail (LRG_IS_ENTITY3D (self), FALSE);
	g_return_val_if_fail (out != NULL, FALSE);

	m = world_matrix_of (self);

	if (!lrg_entity3d_get_local_bounds (self, &local))
	{
		/* No bounds: a point at the world position. */
		out->min.x = out->max.x = m[12];
		out->min.y = out->max.y = m[13];
		out->min.z = out->max.z = m[14];
		return FALSE;
	}

	/* World AABB of the eight transformed corners. */
	for (corner = 0; corner < 8; corner++)
	{
		gfloat p[3];
		gfloat w[3];

		p[0] = (corner & 1) ? local.max.x : local.min.x;
		p[1] = (corner & 2) ? local.max.y : local.min.y;
		p[2] = (corner & 4) ? local.max.z : local.min.z;
		lrg_math3d_transform_point (m, p, w);

		if (corner == 0)
		{
			out->min.x = out->max.x = w[0];
			out->min.y = out->max.y = w[1];
			out->min.z = out->max.z = w[2];
			continue;
		}
		out->min.x = MIN (out->min.x, w[0]);
		out->min.y = MIN (out->min.y, w[1]);
		out->min.z = MIN (out->min.z, w[2]);
		out->max.x = MAX (out->max.x, w[0]);
		out->max.y = MAX (out->max.y, w[1]);
		out->max.z = MAX (out->max.z, w[2]);
	}
	return TRUE;
}
