/* lrg-mode-registry.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-mode-registry.h"
#include "lrg-arrangement-single.h"
#include "lrg-arrangement-per-window.h"
#include "lrg-arrangement-free.h"
#include "lrg-environment-solid.h"
#include "lrg-environment-workshop.h"
#include "lrg-environment-cockpit.h"

struct _LrgModeRegistry
{
	GObject parent_instance;

	/* id (owned gchar*) -> GType (via GSIZE_TO_POINTER) */
	GHashTable *arrangements;
	GHashTable *environments;
};

G_DEFINE_FINAL_TYPE (LrgModeRegistry, lrg_mode_registry, G_TYPE_OBJECT)

static GType
lookup_type (GHashTable  *table,
			 const gchar *id)
{
	gpointer v;

	if (id == NULL)
		return G_TYPE_INVALID;

	v = g_hash_table_lookup (table, id);
	return v != NULL ? (GType) GPOINTER_TO_SIZE (v) : G_TYPE_INVALID;
}

LrgModeRegistry *
lrg_mode_registry_new (void)
{
	return g_object_new (LRG_TYPE_MODE_REGISTRY, NULL);
}

LrgModeRegistry *
lrg_mode_registry_get_default (void)
{
	static LrgModeRegistry *def = NULL;

	if (def == NULL)
	{
		def = lrg_mode_registry_new ();

		lrg_mode_registry_register_arrangement (def, "single-panel",
												LRG_TYPE_ARRANGEMENT_SINGLE);
		lrg_mode_registry_register_arrangement (def, "per-window",
												LRG_TYPE_ARRANGEMENT_PER_WINDOW);
		lrg_mode_registry_register_arrangement (def, "free",
												LRG_TYPE_ARRANGEMENT_FREE);
		lrg_mode_registry_register_environment (def, "void",
												LRG_TYPE_ENVIRONMENT_SOLID);
		lrg_mode_registry_register_environment (def, "workshop",
												LRG_TYPE_ENVIRONMENT_WORKSHOP);
		lrg_mode_registry_register_environment (def, "cockpit",
		                                        LRG_TYPE_ENVIRONMENT_COCKPIT);
	}

	return def;
}

void
lrg_mode_registry_register_arrangement (LrgModeRegistry *self,
										const gchar     *id,
										GType            type)
{
	g_return_if_fail (LRG_IS_MODE_REGISTRY (self));
	g_return_if_fail (id != NULL);
	g_return_if_fail (g_type_is_a (type, LRG_TYPE_SCENE_ARRANGEMENT));

	g_hash_table_replace (self->arrangements, g_strdup (id),
						  GSIZE_TO_POINTER ((gsize) type));
}

void
lrg_mode_registry_register_environment (LrgModeRegistry *self,
										const gchar     *id,
										GType            type)
{
	g_return_if_fail (LRG_IS_MODE_REGISTRY (self));
	g_return_if_fail (id != NULL);
	g_return_if_fail (g_type_is_a (type, LRG_TYPE_PANEL_ENVIRONMENT));

	g_hash_table_replace (self->environments, g_strdup (id),
						  GSIZE_TO_POINTER ((gsize) type));
}

LrgSceneArrangement *
lrg_mode_registry_create_arrangement (LrgModeRegistry *self,
									  const gchar     *id)
{
	GType type;

	g_return_val_if_fail (LRG_IS_MODE_REGISTRY (self), NULL);

	type = lookup_type (self->arrangements, id);
	if (type == G_TYPE_INVALID)
		return NULL;

	return g_object_new (type, NULL);
}

LrgPanelEnvironment *
lrg_mode_registry_create_environment (LrgModeRegistry *self,
									  const gchar     *id)
{
	GType type;

	g_return_val_if_fail (LRG_IS_MODE_REGISTRY (self), NULL);

	type = lookup_type (self->environments, id);
	if (type == G_TYPE_INVALID)
		return NULL;

	return g_object_new (type, NULL);
}

gboolean
lrg_mode_registry_has_arrangement (LrgModeRegistry *self,
								   const gchar     *id)
{
	g_return_val_if_fail (LRG_IS_MODE_REGISTRY (self), FALSE);
	return id != NULL && g_hash_table_contains (self->arrangements, id);
}

gboolean
lrg_mode_registry_has_environment (LrgModeRegistry *self,
								   const gchar     *id)
{
	g_return_val_if_fail (LRG_IS_MODE_REGISTRY (self), FALSE);
	return id != NULL && g_hash_table_contains (self->environments, id);
}

static void
lrg_mode_registry_finalize (GObject *object)
{
	LrgModeRegistry *self = LRG_MODE_REGISTRY (object);

	g_clear_pointer (&self->arrangements, g_hash_table_unref);
	g_clear_pointer (&self->environments, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_mode_registry_parent_class)->finalize (object);
}

static void
lrg_mode_registry_class_init (LrgModeRegistryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_mode_registry_finalize;
}

static void
lrg_mode_registry_init (LrgModeRegistry *self)
{
	self->arrangements = g_hash_table_new_full (g_str_hash, g_str_equal,
												g_free, NULL);
	self->environments = g_hash_table_new_full (g_str_hash, g_str_equal,
												g_free, NULL);
}
