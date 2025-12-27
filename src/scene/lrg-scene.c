/* lrg-scene.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Top-level scene container for Blender-exported 3D scenes.
 */

#include "lrg-scene.h"

/**
 * LrgScene:
 *
 * The root container for a Blender scene export.
 * Holds metadata about the export (source, date) and
 * a collection of entities by name.
 */
struct _LrgScene
{
	GObject parent_instance;

	gchar      *name;
	gchar      *exported_from;
	GDateTime  *export_date;
	GHashTable *entities;    /* gchar* -> LrgSceneEntity* */
};

G_DEFINE_FINAL_TYPE (LrgScene, lrg_scene, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_EXPORTED_FROM,
	PROP_EXPORT_DATE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Overrides
 * ========================================================================== */

static void
lrg_scene_finalize (GObject *object)
{
	LrgScene *self = LRG_SCENE (object);

	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->exported_from, g_free);
	g_clear_pointer (&self->export_date, g_date_time_unref);
	g_clear_pointer (&self->entities, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_scene_parent_class)->finalize (object);
}

static void
lrg_scene_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgScene *self = LRG_SCENE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case PROP_EXPORTED_FROM:
		g_value_set_string (value, self->exported_from);
		break;
	case PROP_EXPORT_DATE:
		g_value_set_boxed (value, self->export_date);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_scene_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgScene *self = LRG_SCENE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_clear_pointer (&self->name, g_free);
		self->name = g_value_dup_string (value);
		break;
	case PROP_EXPORTED_FROM:
		g_clear_pointer (&self->exported_from, g_free);
		self->exported_from = g_value_dup_string (value);
		break;
	case PROP_EXPORT_DATE:
		g_clear_pointer (&self->export_date, g_date_time_unref);
		if (g_value_get_boxed (value) != NULL)
			self->export_date = g_date_time_ref (g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_scene_class_init (LrgSceneClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = lrg_scene_finalize;
	object_class->get_property = lrg_scene_get_property;
	object_class->set_property = lrg_scene_set_property;

	/**
	 * LrgScene:name:
	 *
	 * The name of this scene.
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name",
		                     "Name",
		                     "Scene name",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_CONSTRUCT |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgScene:exported-from:
	 *
	 * The application that exported this scene.
	 */
	properties[PROP_EXPORTED_FROM] =
		g_param_spec_string ("exported-from",
		                     "Exported From",
		                     "Source application",
		                     NULL,
		                     G_PARAM_READWRITE |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgScene:export-date:
	 *
	 * The date/time when this scene was exported.
	 */
	properties[PROP_EXPORT_DATE] =
		g_param_spec_boxed ("export-date",
		                    "Export Date",
		                    "Export timestamp",
		                    G_TYPE_DATE_TIME,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_scene_init (LrgScene *self)
{
	self->name          = NULL;
	self->exported_from = NULL;
	self->export_date   = NULL;
	self->entities      = g_hash_table_new_full (g_str_hash, g_str_equal,
	                                             g_free, g_object_unref);
}

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_scene_new:
 * @name: The scene name
 *
 * Creates a new #LrgScene.
 *
 * Returns: (transfer full): A new #LrgScene
 */
LrgScene *
lrg_scene_new (const gchar *name)
{
	return g_object_new (LRG_TYPE_SCENE,
	                     "name", name,
	                     NULL);
}

/* ==========================================================================
 * Property Accessors
 * ========================================================================== */

/**
 * lrg_scene_get_name:
 * @self: an #LrgScene
 *
 * Gets the name of the scene.
 *
 * Returns: (transfer none): The scene name
 */
const gchar *
lrg_scene_get_name (LrgScene *self)
{
	g_return_val_if_fail (LRG_IS_SCENE (self), NULL);

	return self->name;
}

/**
 * lrg_scene_set_name:
 * @self: an #LrgScene
 * @name: The new name
 *
 * Sets the name of the scene.
 */
void
lrg_scene_set_name (LrgScene    *self,
                    const gchar *name)
{
	g_return_if_fail (LRG_IS_SCENE (self));

	g_clear_pointer (&self->name, g_free);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

/**
 * lrg_scene_get_exported_from:
 * @self: an #LrgScene
 *
 * Gets the application that exported the scene.
 *
 * Returns: (transfer none) (nullable): The exporter name
 */
const gchar *
lrg_scene_get_exported_from (LrgScene *self)
{
	g_return_val_if_fail (LRG_IS_SCENE (self), NULL);

	return self->exported_from;
}

/**
 * lrg_scene_set_exported_from:
 * @self: an #LrgScene
 * @exported_from: (nullable): The exporter name
 *
 * Sets the application that exported the scene.
 */
void
lrg_scene_set_exported_from (LrgScene    *self,
                             const gchar *exported_from)
{
	g_return_if_fail (LRG_IS_SCENE (self));

	g_clear_pointer (&self->exported_from, g_free);
	self->exported_from = g_strdup (exported_from);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPORTED_FROM]);
}

/**
 * lrg_scene_get_export_date:
 * @self: an #LrgScene
 *
 * Gets the date/time when the scene was exported.
 *
 * Returns: (transfer none) (nullable): The export date
 */
GDateTime *
lrg_scene_get_export_date (LrgScene *self)
{
	g_return_val_if_fail (LRG_IS_SCENE (self), NULL);

	return self->export_date;
}

/**
 * lrg_scene_set_export_date:
 * @self: an #LrgScene
 * @export_date: (nullable) (transfer none): The export date
 *
 * Sets the export date.
 */
void
lrg_scene_set_export_date (LrgScene  *self,
                           GDateTime *export_date)
{
	g_return_if_fail (LRG_IS_SCENE (self));

	g_clear_pointer (&self->export_date, g_date_time_unref);
	if (export_date != NULL)
		self->export_date = g_date_time_ref (export_date);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPORT_DATE]);
}

/**
 * lrg_scene_set_export_date_iso:
 * @self: an #LrgScene
 * @iso_string: An ISO 8601 date string
 *
 * Sets the export date from an ISO 8601 string.
 *
 * Returns: %TRUE if the string was parsed successfully
 */
gboolean
lrg_scene_set_export_date_iso (LrgScene    *self,
                               const gchar *iso_string)
{
	g_autoptr(GDateTime) dt = NULL;

	g_return_val_if_fail (LRG_IS_SCENE (self), FALSE);
	g_return_val_if_fail (iso_string != NULL, FALSE);

	dt = g_date_time_new_from_iso8601 (iso_string, NULL);
	if (dt == NULL)
		return FALSE;

	lrg_scene_set_export_date (self, dt);
	return TRUE;
}

/* ==========================================================================
 * Entity Management
 * ========================================================================== */

/**
 * lrg_scene_add_entity:
 * @self: an #LrgScene
 * @entity: (transfer none): The entity to add
 *
 * Adds an entity to the scene. If an entity with the same name
 * already exists, it will be replaced.
 */
void
lrg_scene_add_entity (LrgScene       *self,
                      LrgSceneEntity *entity)
{
	const gchar *name;

	g_return_if_fail (LRG_IS_SCENE (self));
	g_return_if_fail (LRG_IS_SCENE_ENTITY (entity));

	name = lrg_scene_entity_get_name (entity);
	g_return_if_fail (name != NULL);

	g_hash_table_insert (self->entities,
	                     g_strdup (name),
	                     g_object_ref (entity));
}

/**
 * lrg_scene_remove_entity:
 * @self: an #LrgScene
 * @name: The entity name to remove
 *
 * Removes an entity from the scene by name.
 *
 * Returns: %TRUE if the entity was found and removed
 */
gboolean
lrg_scene_remove_entity (LrgScene    *self,
                         const gchar *name)
{
	g_return_val_if_fail (LRG_IS_SCENE (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	return g_hash_table_remove (self->entities, name);
}

/**
 * lrg_scene_get_entity:
 * @self: an #LrgScene
 * @name: The entity name
 *
 * Gets an entity by name.
 *
 * Returns: (transfer none) (nullable): The entity or %NULL if not found
 */
LrgSceneEntity *
lrg_scene_get_entity (LrgScene    *self,
                      const gchar *name)
{
	g_return_val_if_fail (LRG_IS_SCENE (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	return g_hash_table_lookup (self->entities, name);
}

/**
 * lrg_scene_get_entity_names:
 * @self: an #LrgScene
 *
 * Gets a list of all entity names.
 *
 * Returns: (transfer container) (element-type utf8): List of entity names
 */
GList *
lrg_scene_get_entity_names (LrgScene *self)
{
	g_return_val_if_fail (LRG_IS_SCENE (self), NULL);

	return g_hash_table_get_keys (self->entities);
}

/**
 * lrg_scene_get_entity_count:
 * @self: an #LrgScene
 *
 * Gets the number of entities in the scene.
 *
 * Returns: The entity count
 */
guint
lrg_scene_get_entity_count (LrgScene *self)
{
	g_return_val_if_fail (LRG_IS_SCENE (self), 0);

	return g_hash_table_size (self->entities);
}

/* Internal callback for foreach */
typedef struct
{
	LrgSceneForeachFunc func;
	gpointer            user_data;
} ForeachData;

static void
foreach_wrapper (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
	ForeachData    *data   = user_data;
	const gchar    *name   = key;
	LrgSceneEntity *entity = value;

	data->func (name, entity, data->user_data);
}

/**
 * lrg_scene_foreach_entity:
 * @self: an #LrgScene
 * @func: (scope call): The function to call for each entity
 * @user_data: User data to pass to the function
 *
 * Iterates over all entities in the scene.
 */
void
lrg_scene_foreach_entity (LrgScene            *self,
                          LrgSceneForeachFunc  func,
                          gpointer             user_data)
{
	ForeachData data;

	g_return_if_fail (LRG_IS_SCENE (self));
	g_return_if_fail (func != NULL);

	data.func      = func;
	data.user_data = user_data;

	g_hash_table_foreach (self->entities, foreach_wrapper, &data);
}
