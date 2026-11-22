/* lrg-world-adapter.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorldAdapter: the interface, its signal, and wrappers that refuse
 * (rather than pretend) when an implementation leaves a required method
 * out.
 */

#include "config.h"

#include "lrg-world-adapter.h"

G_DEFINE_QUARK (lrg-world-adapter-error-quark, lrg_world_adapter_error)

GType
lrg_world_camera_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_WORLD_CAMERA_OVERVIEW, "LRG_WORLD_CAMERA_OVERVIEW", "overview" },
			{ LRG_WORLD_CAMERA_FOLLOW, "LRG_WORLD_CAMERA_FOLLOW", "follow" },
			{ LRG_WORLD_CAMERA_FREE, "LRG_WORLD_CAMERA_FREE", "free" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgWorldCameraMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_world_adapter_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_WORLD_ADAPTER_ERROR_NOT_SUPPORTED, "LRG_WORLD_ADAPTER_ERROR_NOT_SUPPORTED", "not-supported" },
			{ LRG_WORLD_ADAPTER_ERROR_NOT_LOADED, "LRG_WORLD_ADAPTER_ERROR_NOT_LOADED", "not-loaded" },
			{ LRG_WORLD_ADAPTER_ERROR_LOAD, "LRG_WORLD_ADAPTER_ERROR_LOAD", "load" },
			{ LRG_WORLD_ADAPTER_ERROR_UNKNOWN_PLACE, "LRG_WORLD_ADAPTER_ERROR_UNKNOWN_PLACE", "unknown-place" },
			{ LRG_WORLD_ADAPTER_ERROR_UNKNOWN_ACTOR, "LRG_WORLD_ADAPTER_ERROR_UNKNOWN_ACTOR", "unknown-actor" },
			{ LRG_WORLD_ADAPTER_ERROR_DUPLICATE_ACTOR, "LRG_WORLD_ADAPTER_ERROR_DUPLICATE_ACTOR", "duplicate-actor" },
			{ LRG_WORLD_ADAPTER_ERROR_UNREACHABLE, "LRG_WORLD_ADAPTER_ERROR_UNREACHABLE", "unreachable" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgWorldAdapterError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

G_DEFINE_INTERFACE (LrgWorldAdapter, lrg_world_adapter, G_TYPE_OBJECT)

static void
lrg_world_adapter_default_init (LrgWorldAdapterInterface *iface)
{
	/**
	 * LrgWorldAdapter::actor-arrived:
	 * @self: the adapter
	 * @handle: the actor that arrived
	 *
	 * Emitted by an adapter when an actor finishes a move.
	 *
	 * Since: 0.11.0
	 */
	g_signal_new ("actor-arrived", G_TYPE_FROM_INTERFACE (iface), G_SIGNAL_RUN_LAST, 0,
	              NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

static gboolean
not_supported (LrgWorldAdapter  *self,
               const gchar      *what,
               GError          **error)
{
	g_set_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_NOT_SUPPORTED,
	             "%s does not implement %s", G_OBJECT_TYPE_NAME (self), what);
	return FALSE;
}

gboolean
lrg_world_adapter_load (LrgWorldAdapter  *self,
                        const gchar      *world_path,
                        GError          **error)
{
	LrgWorldAdapterInterface *iface;

	g_return_val_if_fail (LRG_IS_WORLD_ADAPTER (self), FALSE);
	g_return_val_if_fail (world_path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->load == NULL)
		return not_supported (self, "load", error);
	return iface->load (self, world_path, error);
}

gboolean
lrg_world_adapter_resolve_place (LrgWorldAdapter *self,
                                 const gchar     *name,
                                 gfloat          *x,
                                 gfloat          *y,
                                 gfloat          *z)
{
	LrgWorldAdapterInterface *iface;
	gfloat px = 0.0f;
	gfloat py = 0.0f;
	gfloat pz = 0.0f;

	g_return_val_if_fail (LRG_IS_WORLD_ADAPTER (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->resolve_place == NULL || !iface->resolve_place (self, name, &px, &py, &pz))
		return FALSE;
	if (x != NULL)
		*x = px;
	if (y != NULL)
		*y = py;
	if (z != NULL)
		*z = pz;
	return TRUE;
}

guint
lrg_world_adapter_spawn_actor (LrgWorldAdapter  *self,
                               const gchar      *id,
                               const gchar      *appearance,
                               GError          **error)
{
	LrgWorldAdapterInterface *iface;

	g_return_val_if_fail (LRG_IS_WORLD_ADAPTER (self), 0);
	g_return_val_if_fail (id != NULL, 0);
	g_return_val_if_fail (error == NULL || *error == NULL, 0);

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->spawn_actor == NULL)
	{
		not_supported (self, "spawn_actor", error);
		return 0;
	}
	return iface->spawn_actor (self, id, appearance, error);
}

gboolean
lrg_world_adapter_move_actor (LrgWorldAdapter  *self,
                              guint             handle,
                              gfloat            x,
                              gfloat            y,
                              gfloat            z,
                              GError          **error)
{
	LrgWorldAdapterInterface *iface;

	g_return_val_if_fail (LRG_IS_WORLD_ADAPTER (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->move_actor == NULL)
		return not_supported (self, "move_actor", error);
	return iface->move_actor (self, handle, x, y, z, error);
}

gboolean
lrg_world_adapter_move_actor_to_place (LrgWorldAdapter  *self,
                                       guint             handle,
                                       const gchar      *place,
                                       GError          **error)
{
	gfloat x;
	gfloat y;
	gfloat z;

	g_return_val_if_fail (LRG_IS_WORLD_ADAPTER (self), FALSE);
	g_return_val_if_fail (place != NULL, FALSE);

	if (!lrg_world_adapter_resolve_place (self, place, &x, &y, &z))
	{
		g_set_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_UNKNOWN_PLACE,
		             "no place named '%s'", place);
		return FALSE;
	}
	return lrg_world_adapter_move_actor (self, handle, x, y, z, error);
}

gboolean
lrg_world_adapter_remove_actor (LrgWorldAdapter *self,
                                guint            handle)
{
	LrgWorldAdapterInterface *iface;

	g_return_val_if_fail (LRG_IS_WORLD_ADAPTER (self), FALSE);

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->remove_actor == NULL)
		return FALSE;
	return iface->remove_actor (self, handle);
}

void
lrg_world_adapter_set_camera (LrgWorldAdapter    *self,
                              LrgWorldCameraMode  mode)
{
	LrgWorldAdapterInterface *iface;

	g_return_if_fail (LRG_IS_WORLD_ADAPTER (self));

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->set_camera != NULL)
		iface->set_camera (self, mode);
}

void
lrg_world_adapter_set_camera_focus (LrgWorldAdapter *self,
                                    guint            handle)
{
	LrgWorldAdapterInterface *iface;

	g_return_if_fail (LRG_IS_WORLD_ADAPTER (self));

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->set_camera_focus != NULL)
		iface->set_camera_focus (self, handle);
}

gboolean
lrg_world_adapter_get_actor_position (LrgWorldAdapter *self,
                                      guint            handle,
                                      gfloat          *x,
                                      gfloat          *y,
                                      gfloat          *z)
{
	LrgWorldAdapterInterface *iface;
	gfloat px = 0.0f;
	gfloat py = 0.0f;
	gfloat pz = 0.0f;

	g_return_val_if_fail (LRG_IS_WORLD_ADAPTER (self), FALSE);

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->get_actor_position == NULL ||
	    !iface->get_actor_position (self, handle, &px, &py, &pz))
		return FALSE;
	if (x != NULL)
		*x = px;
	if (y != NULL)
		*y = py;
	if (z != NULL)
		*z = pz;
	return TRUE;
}

void
lrg_world_adapter_update (LrgWorldAdapter *self,
                          gfloat           delta)
{
	LrgWorldAdapterInterface *iface;

	g_return_if_fail (LRG_IS_WORLD_ADAPTER (self));

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->update != NULL)
		iface->update (self, delta);
}

void
lrg_world_adapter_draw (LrgWorldAdapter *self)
{
	LrgWorldAdapterInterface *iface;

	g_return_if_fail (LRG_IS_WORLD_ADAPTER (self));

	iface = LRG_WORLD_ADAPTER_GET_IFACE (self);
	if (iface->draw != NULL)
		iface->draw (self);
}
