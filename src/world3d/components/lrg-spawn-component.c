/* lrg-spawn-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpawnComponent: spawn-point data on an entity, plus world lookups.
 */

#include "config.h"

#include "lrg-spawn-component.h"
#include "../lrg-entity3d.h"
#include "../../core/lrg-math3d.h"

struct _LrgSpawnComponent
{
	LrgComponent  parent_instance;

	LrgSpawnType  spawn_type;
	gint          team;
	gchar        *entity_type;
	GPtrArray    *tags;     /* gchar*, NULL terminated */
};

G_DEFINE_FINAL_TYPE (LrgSpawnComponent, lrg_spawn_component, LRG_TYPE_COMPONENT)

enum
{
	PROP_0,
	PROP_SPAWN_TYPE,
	PROP_TEAM,
	PROP_ENTITY_TYPE,
	PROP_TAGS,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Resets the tag array to just its NULL terminator. */
static void
clear_tags (LrgSpawnComponent *self)
{
	g_ptr_array_set_size (self->tags, 0);
	g_ptr_array_add (self->tags, NULL);
}

/* Appends @tag before the terminator unless it is already present. */
static gboolean
append_tag (LrgSpawnComponent *self,
            const gchar       *tag)
{
	if (tag == NULL || *tag == '\0' || lrg_spawn_component_has_tag (self, tag))
		return FALSE;
	g_ptr_array_insert (self->tags, (gint) self->tags->len - 1, g_strdup (tag));
	return TRUE;
}

static void
lrg_spawn_component_finalize (GObject *object)
{
	LrgSpawnComponent *self = LRG_SPAWN_COMPONENT (object);

	g_free (self->entity_type);
	g_ptr_array_unref (self->tags);

	G_OBJECT_CLASS (lrg_spawn_component_parent_class)->finalize (object);
}

static void
lrg_spawn_component_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgSpawnComponent *self = LRG_SPAWN_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_SPAWN_TYPE:  g_value_set_enum (value, (gint) self->spawn_type); break;
	case PROP_TEAM:        g_value_set_int (value, self->team); break;
	case PROP_ENTITY_TYPE: g_value_set_string (value, self->entity_type); break;
	case PROP_TAGS:        g_value_set_boxed (value, self->tags->pdata); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_spawn_component_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgSpawnComponent *self = LRG_SPAWN_COMPONENT (object);

	switch (prop_id)
	{
	case PROP_SPAWN_TYPE:
		lrg_spawn_component_set_spawn_type (self, (LrgSpawnType) g_value_get_enum (value));
		break;
	case PROP_TEAM:
		lrg_spawn_component_set_team (self, g_value_get_int (value));
		break;
	case PROP_ENTITY_TYPE:
		lrg_spawn_component_set_entity_type (self, g_value_get_string (value));
		break;
	case PROP_TAGS:
		lrg_spawn_component_set_tags (self, (const gchar * const *) g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_spawn_component_class_init (LrgSpawnComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	const GParamFlags  rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->finalize = lrg_spawn_component_finalize;
	object_class->get_property = lrg_spawn_component_get_property;
	object_class->set_property = lrg_spawn_component_set_property;

	/**
	 * LrgSpawnComponent:spawn-type:
	 *
	 * What spawns here (player, enemy, NPC, item, generic).
	 */
	properties[PROP_SPAWN_TYPE] =
		g_param_spec_enum ("spawn-type", NULL, "Spawn type", LRG_TYPE_SPAWN_TYPE,
		                   LRG_SPAWN_TYPE_GENERIC, rw);

	/**
	 * LrgSpawnComponent:team:
	 *
	 * The team number (0 for none).
	 */
	properties[PROP_TEAM] = g_param_spec_int ("team", NULL, "Team", 0, G_MAXINT, 0, rw);

	/**
	 * LrgSpawnComponent:entity-type:
	 *
	 * What to spawn: a prefab path or a type name the game understands.
	 */
	properties[PROP_ENTITY_TYPE] =
		g_param_spec_string ("entity-type", NULL, "What to spawn", NULL, rw);

	/**
	 * LrgSpawnComponent:tags:
	 *
	 * Free-form tags games filter spawn points by.
	 */
	properties[PROP_TAGS] = g_param_spec_boxed ("tags", NULL, "Tags", G_TYPE_STRV, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_spawn_component_init (LrgSpawnComponent *self)
{
	self->spawn_type = LRG_SPAWN_TYPE_GENERIC;
	self->tags = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (self->tags, NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgSpawnComponent *
lrg_spawn_component_new (void)
{
	return (LrgSpawnComponent *) g_object_new (LRG_TYPE_SPAWN_COMPONENT, NULL);
}

LrgSpawnComponent *
lrg_spawn_component_new_with_type (LrgSpawnType spawn_type,
                                   gint         team)
{
	return (LrgSpawnComponent *) g_object_new (LRG_TYPE_SPAWN_COMPONENT, "spawn-type", spawn_type,
	                                           "team", MAX (team, 0), NULL);
}

LrgSpawnType
lrg_spawn_component_get_spawn_type (LrgSpawnComponent *self)
{
	g_return_val_if_fail (LRG_IS_SPAWN_COMPONENT (self), LRG_SPAWN_TYPE_GENERIC);

	return self->spawn_type;
}

void
lrg_spawn_component_set_spawn_type (LrgSpawnComponent *self,
                                    LrgSpawnType       spawn_type)
{
	g_return_if_fail (LRG_IS_SPAWN_COMPONENT (self));

	if (self->spawn_type == spawn_type)
		return;
	self->spawn_type = spawn_type;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPAWN_TYPE]);
}

gint
lrg_spawn_component_get_team (LrgSpawnComponent *self)
{
	g_return_val_if_fail (LRG_IS_SPAWN_COMPONENT (self), 0);

	return self->team;
}

void
lrg_spawn_component_set_team (LrgSpawnComponent *self,
                              gint               team)
{
	g_return_if_fail (LRG_IS_SPAWN_COMPONENT (self));

	team = MAX (team, 0);
	if (self->team == team)
		return;
	self->team = team;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEAM]);
}

const gchar *
lrg_spawn_component_get_entity_type (LrgSpawnComponent *self)
{
	g_return_val_if_fail (LRG_IS_SPAWN_COMPONENT (self), NULL);

	return self->entity_type;
}

void
lrg_spawn_component_set_entity_type (LrgSpawnComponent *self,
                                     const gchar       *entity_type)
{
	g_return_if_fail (LRG_IS_SPAWN_COMPONENT (self));

	if (g_strcmp0 (self->entity_type, entity_type) == 0)
		return;
	g_free (self->entity_type);
	self->entity_type = g_strdup (entity_type);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENTITY_TYPE]);
}

const gchar * const *
lrg_spawn_component_get_tags (LrgSpawnComponent *self)
{
	g_return_val_if_fail (LRG_IS_SPAWN_COMPONENT (self), NULL);

	return (const gchar * const *) self->tags->pdata;
}

void
lrg_spawn_component_set_tags (LrgSpawnComponent   *self,
                              const gchar * const *tags)
{
	guint i;

	g_return_if_fail (LRG_IS_SPAWN_COMPONENT (self));

	clear_tags (self);
	for (i = 0; tags != NULL && tags[i] != NULL; i++)
		append_tag (self, tags[i]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TAGS]);
}

void
lrg_spawn_component_add_tag (LrgSpawnComponent *self,
                             const gchar       *tag)
{
	g_return_if_fail (LRG_IS_SPAWN_COMPONENT (self));
	g_return_if_fail (tag != NULL);

	if (append_tag (self, tag))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TAGS]);
}

gboolean
lrg_spawn_component_has_tag (LrgSpawnComponent *self,
                             const gchar       *tag)
{
	guint i;

	g_return_val_if_fail (LRG_IS_SPAWN_COMPONENT (self), FALSE);
	g_return_val_if_fail (tag != NULL, FALSE);

	for (i = 0; i + 1 < self->tags->len; i++)
		if (g_strcmp0 ((const gchar *) g_ptr_array_index (self->tags, i), tag) == 0)
			return TRUE;
	return FALSE;
}

gboolean
lrg_spawn_component_matches (LrgSpawnComponent *self,
                             const gchar       *tag,
                             gint               team)
{
	g_return_val_if_fail (LRG_IS_SPAWN_COMPONENT (self), FALSE);

	if (!lrg_component_get_enabled (LRG_COMPONENT (self)))
		return FALSE;
	if (team != LRG_SPAWN_COMPONENT_ANY_TEAM && team != self->team)
		return FALSE;
	return tag == NULL || lrg_spawn_component_has_tag (self, tag);
}

LrgSpawnPoint3D *
lrg_spawn_component_to_spawn_point (LrgSpawnComponent *self)
{
	LrgEntity3D     *entity;
	LrgSpawnPoint3D *point;
	const gchar     *id = NULL;
	gfloat           pos[3] = { 0.0f, 0.0f, 0.0f };
	gfloat           euler[3] = { 0.0f, 0.0f, 0.0f };
	GValue           team = G_VALUE_INIT;

	g_return_val_if_fail (LRG_IS_SPAWN_COMPONENT (self), NULL);

	/* Placement from the entity's world transform */
	entity = lrg_component_get_entity (LRG_COMPONENT (self));
	if (entity != NULL)
	{
		gfloat quat[4];

		lrg_entity3d_get_world_position (entity, pos);
		lrg_entity3d_get_world_rotation (entity, quat);
		lrg_math3d_quat_to_euler (quat, euler);
		id = lrg_entity3d_get_name (entity);
		if (id == NULL || *id == '\0')
			id = lrg_entity3d_ensure_guid (entity);
	}

	point = lrg_spawn_point3d_new (id != NULL ? id : "spawn", pos[0], pos[1], pos[2], self->spawn_type);
	{
		GrlVector3 rot;

		rot.x = euler[0] * 180.0f / (gfloat) G_PI;
		rot.y = euler[1] * 180.0f / (gfloat) G_PI;
		rot.z = euler[2] * 180.0f / (gfloat) G_PI;
		lrg_spawn_point3d_set_rotation (point, &rot);
	}
	if (self->entity_type != NULL)
		lrg_spawn_point3d_set_entity_type (point, self->entity_type);
	g_value_init (&team, G_TYPE_INT);
	g_value_set_int (&team, self->team);
	lrg_spawn_point3d_set_property (point, "team", &team);
	g_value_unset (&team);
	return point;
}

GPtrArray *
lrg_spawn_component_find (LrgWorld3D  *world,
                          const gchar *tag,
                          gint         team)
{
	g_autoptr(GPtrArray) all = NULL;
	GPtrArray           *out;
	guint                i;

	g_return_val_if_fail (LRG_IS_WORLD3D (world), NULL);

	out = g_ptr_array_new ();
	all = lrg_world3d_get_components_of_type (world, LRG_TYPE_SPAWN_COMPONENT);
	for (i = 0; all != NULL && i < all->len; i++)
	{
		LrgSpawnComponent *spawn = LRG_SPAWN_COMPONENT (g_ptr_array_index (all, i));
		LrgEntity3D       *entity = lrg_component_get_entity (LRG_COMPONENT (spawn));

		if (entity != NULL && lrg_entity3d_is_active_in_hierarchy (entity) &&
		    lrg_spawn_component_matches (spawn, tag, team))
			g_ptr_array_add (out, spawn);
	}
	return out;
}
