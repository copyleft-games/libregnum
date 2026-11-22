/* lrg-tiled-world.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTiledWorld: LrgWorldAdapter over LrgTiledMap, LrgNavGrid,
 * LrgPathfinder and LrgActorMover, with a 2D camera and a plain
 * renderer.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TILEMAP

#include "lrg-tiled-world.h"
#include "../pathfinding/lrg-pathfinder.h"
#include "../graphics/lrg-camera.h"
#include "../lrg-log.h"

#include <math.h>
#include <raylib.h>
#include <string.h>

typedef struct _LrgTiledWorldActor LrgTiledWorldActor;

struct _LrgTiledWorld
{
	GObject             parent_instance;

	LrgTiledMap        *map;
	LrgNavGrid         *grid;
	LrgPathfinder      *pathfinder;
	GPtrArray          *tilemaps;      /* LrgTilemap, one per image tileset */
	gboolean            textures_tried;
	gboolean            textures_ok;

	GHashTable         *actors;        /* GUINT_TO_POINTER (handle) -> actor */
	guint               next_handle;
	gfloat              actor_speed;

	LrgCamera2D        *camera;
	LrgWorldCameraMode  camera_mode;
	guint               focus;
	gint                viewport_w;
	gint                viewport_h;
};

struct _LrgTiledWorldActor
{
	LrgTiledWorld *world;          /* not a reference: the world owns the actor */
	guint          handle;
	gchar         *id;
	gchar         *appearance;
	GrlColor       color;
	LrgActorMover *mover;
	gulong         arrived_id;
	gboolean       seat_has_facing;
	gfloat         seat_facing;
};

static void lrg_tiled_world_adapter_init (LrgWorldAdapterInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgTiledWorld, lrg_tiled_world, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_WORLD_ADAPTER,
                                                      lrg_tiled_world_adapter_init))

static void
actor_free (gpointer data)
{
	LrgTiledWorldActor *actor = data;

	g_clear_signal_handler (&actor->arrived_id, actor->mover);
	g_clear_object (&actor->mover);
	g_free (actor->id);
	g_free (actor->appearance);
	g_free (actor);
}

static void
lrg_tiled_world_finalize (GObject *object)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (object);

	g_hash_table_unref (self->actors);
	g_clear_pointer (&self->tilemaps, g_ptr_array_unref);
	g_clear_object (&self->pathfinder);
	g_clear_object (&self->grid);
	g_clear_object (&self->map);
	g_clear_object (&self->camera);
	G_OBJECT_CLASS (lrg_tiled_world_parent_class)->finalize (object);
}

static void
lrg_tiled_world_class_init (LrgTiledWorldClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_tiled_world_finalize;
}

static void
lrg_tiled_world_init (LrgTiledWorld *self)
{
	self->actors = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, actor_free);
	self->next_handle = 1;
	self->actor_speed = 96.0f;
	self->camera = lrg_camera2d_new ();
	lrg_camera2d_set_interpolate (self->camera, FALSE);
	self->viewport_w = 1280;
	self->viewport_h = 720;
}

LrgTiledWorld *
lrg_tiled_world_new (void)
{
	return g_object_new (LRG_TYPE_TILED_WORLD, NULL);
}

/* ==========================================================================
 * Building the walkable grid
 * ========================================================================== */

static gboolean
layer_is_wall (LrgTiledLayer *layer)
{
	const gchar *name = lrg_tiled_layer_get_name (layer);
	const gchar *slash = strrchr (name, '/');
	const gchar *leaf = (slash != NULL) ? slash + 1 : name;
	LrgTiledProperties *props = lrg_tiled_layer_get_properties (layer);

	return lrg_tiled_properties_get_boolean (props, "collision", FALSE) ||
	       lrg_tiled_properties_get_boolean (props, "solid", FALSE) ||
	       g_ascii_strcasecmp (leaf, "collision") == 0 ||
	       g_ascii_strcasecmp (leaf, "walls") == 0;
}

static gboolean
tile_is_solid (LrgTiledMap *map,
               guint        gid)
{
	LrgTiledTileset *ts;
	LrgTiledProperties *props;
	guint local = 0;

	ts = lrg_tiled_map_get_tileset_for_gid (map, gid, &local);
	if (ts == NULL)
		return FALSE;
	props = lrg_tiled_tileset_get_tile_properties (ts, local);
	return props != NULL && (lrg_tiled_properties_get_boolean (props, "solid", FALSE) ||
	                         lrg_tiled_properties_get_boolean (props, "collision", FALSE));
}

static gboolean
object_is_obstacle (LrgTiledObject *object)
{
	const gchar *type = lrg_tiled_object_get_object_type (object);

	return g_strcmp0 (type, "obstacle") == 0 || g_strcmp0 (type, "solid") == 0 ||
	       lrg_tiled_properties_get_boolean (lrg_tiled_object_get_properties (object), "solid",
	                                         FALSE);
}

static LrgNavGrid *
build_grid (LrgTiledMap *map)
{
	guint w = lrg_tiled_map_get_width (map);
	guint h = lrg_tiled_map_get_height (map);
	LrgNavGrid *grid = lrg_nav_grid_new (w, h);
	g_autoptr(GPtrArray) objects = NULL;
	guint tw;
	guint th;
	guint i;
	guint x;
	guint y;

	lrg_tiled_map_get_tile_size (map, &tw, &th);
	for (i = 0; i < lrg_tiled_map_get_n_layers (map); i++)
	{
		LrgTiledLayer *layer = lrg_tiled_map_get_layer (map, i);
		gboolean wall;

		if (lrg_tiled_layer_get_kind (layer) != LRG_TILED_LAYER_TILES)
			continue;
		wall = layer_is_wall (layer);
		for (y = 0; y < MIN (h, lrg_tiled_layer_get_height (layer)); y++)
		{
			for (x = 0; x < MIN (w, lrg_tiled_layer_get_width (layer)); x++)
			{
				guint gid = lrg_tiled_layer_get_gid (layer, x, y);

				if (gid != 0 && (wall || tile_is_solid (map, gid)))
					lrg_nav_grid_set_blocked (grid, (gint) x, (gint) y, TRUE);
			}
		}
	}

	objects = lrg_tiled_map_get_objects (map, NULL);
	for (i = 0; i < objects->len; i++)
	{
		LrgTiledObject *object = g_ptr_array_index (objects, i);

		if (!object_is_obstacle (object))
			continue;
		for (y = 0; y < h; y++)
		{
			for (x = 0; x < w; x++)
			{
				if (lrg_tiled_object_contains_point (object, (x + 0.5) * tw, (y + 0.5) * th))
					lrg_nav_grid_set_blocked (grid, (gint) x, (gint) y, TRUE);
			}
		}
	}
	return grid;
}

/* ==========================================================================
 * Camera
 * ========================================================================== */

static void
update_camera (LrgTiledWorld *self)
{
	guint tw;
	guint th;
	gfloat map_w;
	gfloat map_h;

	if (self->map == NULL || self->camera_mode == LRG_WORLD_CAMERA_FREE)
		return;
	lrg_tiled_map_get_tile_size (self->map, &tw, &th);
	map_w = (gfloat) (lrg_tiled_map_get_width (self->map) * tw);
	map_h = (gfloat) (lrg_tiled_map_get_height (self->map) * th);
	lrg_camera2d_set_offset_xy (self->camera, self->viewport_w * 0.5f, self->viewport_h * 0.5f);

	if (self->camera_mode == LRG_WORLD_CAMERA_FOLLOW)
	{
		LrgTiledWorldActor *actor = g_hash_table_lookup (self->actors,
		                                                 GUINT_TO_POINTER (self->focus));

		if (actor != NULL)
		{
			gfloat x;
			gfloat y;

			lrg_actor_mover_get_position (actor->mover, &x, &y, NULL);
			lrg_camera2d_set_target_xy (self->camera, x, y);
			/* close enough to see faces, far enough to see the room */
			lrg_camera2d_set_zoom (self->camera,
			                       2.0f * MIN (self->viewport_w / map_w, self->viewport_h / map_h));
			return;
		}
	}
	lrg_camera2d_set_target_xy (self->camera, map_w * 0.5f, map_h * 0.5f);
	lrg_camera2d_set_zoom (self->camera,
	                       MIN (self->viewport_w / map_w, self->viewport_h / map_h));
}

/* ==========================================================================
 * LrgWorldAdapter
 * ========================================================================== */

static gboolean
world_load (LrgWorldAdapter  *adapter,
            const gchar      *world_path,
            GError          **error)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);
	g_autoptr(GError) local = NULL;
	g_autoptr(LrgTiledMap) map = NULL;
	guint i;

	map = lrg_tiled_map_new_from_file (world_path, &local);
	if (map == NULL)
	{
		g_set_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_LOAD, "%s",
		             local->message);
		return FALSE;
	}

	/* a new world: the old one's actors and drawing go */
	g_hash_table_remove_all (self->actors);
	self->focus = 0;
	g_clear_object (&self->pathfinder);
	g_clear_object (&self->grid);
	g_clear_pointer (&self->tilemaps, g_ptr_array_unref);
	g_set_object (&self->map, map);
	self->textures_tried = FALSE;
	self->textures_ok = FALSE;

	self->grid = build_grid (map);
	self->pathfinder = lrg_pathfinder_new (self->grid);
	self->tilemaps = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < lrg_tiled_map_get_n_tilesets (map); i++)
	{
		LrgTilemap *tilemap;

		if (lrg_tiled_tileset_get_tileset (lrg_tiled_map_get_tileset (map, i)) == NULL)
			continue;
		tilemap = lrg_tiled_map_build_tilemap (map, i, NULL);
		if (tilemap != NULL)
			g_ptr_array_add (self->tilemaps, tilemap);
	}
	update_camera (self);
	return TRUE;
}

static gboolean
world_resolve_place (LrgWorldAdapter *adapter,
                     const gchar     *name,
                     gfloat          *x,
                     gfloat          *y,
                     gfloat          *z)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);
	LrgTiledObject *object;
	gdouble cx;
	gdouble cy;

	if (self->map == NULL)
		return FALSE;
	object = lrg_tiled_map_find_object (self->map, name);
	if (object == NULL)
		return FALSE;
	lrg_tiled_object_get_center (object, &cx, &cy);
	*x = (gfloat) cx;
	*y = (gfloat) cy;
	*z = 0.0f;
	return TRUE;
}

/* "spawn" by name, else the first object of type "spawn", else the centre */
static void
spawn_point (LrgTiledWorld *self,
             gfloat        *x,
             gfloat        *y)
{
	LrgTiledObject *object = lrg_tiled_map_find_object (self->map, "spawn");
	gdouble cx;
	gdouble cy;
	guint tw;
	guint th;

	if (object == NULL)
	{
		g_autoptr(GPtrArray) spawns = lrg_tiled_map_get_objects (self->map, "spawn");

		if (spawns->len > 0)
			object = g_ptr_array_index (spawns, 0);
	}
	if (object != NULL)
	{
		lrg_tiled_object_get_center (object, &cx, &cy);
		*x = (gfloat) cx;
		*y = (gfloat) cy;
		return;
	}
	lrg_tiled_map_get_tile_size (self->map, &tw, &th);
	*x = (gfloat) (lrg_tiled_map_get_width (self->map) * tw) * 0.5f;
	*y = (gfloat) (lrg_tiled_map_get_height (self->map) * th) * 0.5f;
}

/* "#RRGGBB" as written, anything else a stable colour from its hash */
static GrlColor
appearance_color (const gchar *appearance)
{
	GrlColor c = { 200, 200, 210, 255 };
	Color hsv;
	guint hash;

	if (appearance != NULL && appearance[0] == '#' && strlen (appearance) == 7)
	{
		guint64 v = g_ascii_strtoull (appearance + 1, NULL, 16);

		c.r = (guint8) ((v >> 16) & 0xFF);
		c.g = (guint8) ((v >> 8) & 0xFF);
		c.b = (guint8) (v & 0xFF);
		return c;
	}
	if (appearance == NULL || *appearance == '\0')
		return c;
	hash = g_str_hash (appearance);
	hsv = ColorFromHSV ((gfloat) (hash % 360), 0.55f, 0.9f);
	c.r = hsv.r;
	c.g = hsv.g;
	c.b = hsv.b;
	return c;
}

static void
on_actor_arrived (LrgActorMover *mover,
                  gpointer       user_data)
{
	LrgTiledWorldActor *actor = user_data;

	if (actor->seat_has_facing && lrg_actor_mover_get_state (mover) == LRG_ACTOR_MOVER_SITTING)
		lrg_actor_mover_set_facing (mover, actor->seat_facing);
	g_signal_emit_by_name (actor->world, "actor-arrived", actor->handle);
}

static guint
world_spawn_actor (LrgWorldAdapter  *adapter,
                   const gchar      *id,
                   const gchar      *appearance,
                   GError          **error)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);
	LrgTiledWorldActor *actor;
	guint tw;
	guint th;
	gfloat x;
	gfloat y;

	if (self->map == NULL)
	{
		g_set_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_NOT_LOADED,
		             "no map is loaded");
		return 0;
	}
	if (lrg_tiled_world_find_actor (self, id) != 0)
	{
		g_set_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_DUPLICATE_ACTOR,
		             "an actor '%s' already exists", id);
		return 0;
	}

	actor = g_new0 (LrgTiledWorldActor, 1);
	actor->world = self;
	actor->handle = self->next_handle++;
	actor->id = g_strdup (id);
	actor->appearance = g_strdup (appearance);
	actor->color = appearance_color (appearance);
	actor->mover = lrg_actor_mover_new ();
	lrg_tiled_map_get_tile_size (self->map, &tw, &th);
	lrg_actor_mover_set_grid (actor->mover, self->pathfinder, (gfloat) tw, (gfloat) th, 0.0f, 0.0f);
	lrg_actor_mover_set_speed (actor->mover, self->actor_speed);
	spawn_point (self, &x, &y);
	lrg_actor_mover_set_position (actor->mover, x, y, 0.0f);
	actor->arrived_id = g_signal_connect (actor->mover, "arrived",
	                                      G_CALLBACK (on_actor_arrived), actor);
	g_hash_table_insert (self->actors, GUINT_TO_POINTER (actor->handle), actor);
	return actor->handle;
}

/* A target on a "seat" point object sits, facing its "facing" degrees. */
static gboolean
seat_at (LrgTiledWorld *self,
         gfloat         x,
         gfloat         y,
         gfloat        *facing,
         gboolean      *has_facing)
{
	g_autoptr(GPtrArray) seats = lrg_tiled_map_get_objects (self->map, "seat");
	guint i;

	for (i = 0; i < seats->len; i++)
	{
		LrgTiledObject *seat = g_ptr_array_index (seats, i);
		LrgTiledProperties *props = lrg_tiled_object_get_properties (seat);
		gdouble cx;
		gdouble cy;

		lrg_tiled_object_get_center (seat, &cx, &cy);
		if (fabs (cx - x) <= 0.5 && fabs (cy - y) <= 0.5)
		{
			*has_facing = lrg_tiled_properties_has (props, "facing");
			*facing = (gfloat) (lrg_tiled_properties_get_double (props, "facing", 0.0) * G_PI / 180.0);
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean
world_move_actor (LrgWorldAdapter  *adapter,
                  guint             handle,
                  gfloat            x,
                  gfloat            y,
                  gfloat            z,
                  GError          **error)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);
	LrgTiledWorldActor *actor = g_hash_table_lookup (self->actors, GUINT_TO_POINTER (handle));
	gboolean sit;

	if (actor == NULL)
	{
		g_set_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_UNKNOWN_ACTOR,
		             "no actor with handle %u", handle);
		return FALSE;
	}
	sit = seat_at (self, x, y, &actor->seat_facing, &actor->seat_has_facing);
	if (!sit)
		actor->seat_has_facing = FALSE;
	if (!lrg_actor_mover_move_to (actor->mover, x, y, 0.0f, sit))
	{
		g_set_error (error, LRG_WORLD_ADAPTER_ERROR, LRG_WORLD_ADAPTER_ERROR_UNREACHABLE,
		             "'%s' cannot reach (%.1f, %.1f)", actor->id, x, y);
		return FALSE;
	}
	return TRUE;
}

static gboolean
world_remove_actor (LrgWorldAdapter *adapter,
                    guint            handle)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);

	if (self->focus == handle)
		self->focus = 0;
	return g_hash_table_remove (self->actors, GUINT_TO_POINTER (handle));
}

static void
world_set_camera (LrgWorldAdapter    *adapter,
                  LrgWorldCameraMode  mode)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);

	self->camera_mode = mode;
	update_camera (self);
}

static void
world_set_camera_focus (LrgWorldAdapter *adapter,
                        guint            handle)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);

	self->focus = handle;
	update_camera (self);
}

static gboolean
world_get_actor_position (LrgWorldAdapter *adapter,
                          guint            handle,
                          gfloat          *x,
                          gfloat          *y,
                          gfloat          *z)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);
	LrgTiledWorldActor *actor = g_hash_table_lookup (self->actors, GUINT_TO_POINTER (handle));

	if (actor == NULL)
		return FALSE;
	lrg_actor_mover_get_position (actor->mover, x, y, z);
	return TRUE;
}

static gint
compare_handles (gconstpointer a,
                 gconstpointer b)
{
	guint ha = GPOINTER_TO_UINT (a);
	guint hb = GPOINTER_TO_UINT (b);

	return (ha < hb) ? -1 : (ha > hb);
}

static void
world_update (LrgWorldAdapter *adapter,
              gfloat           delta)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);
	g_autoptr(GList) handles = NULL;
	GList *l;

	/* in handle order, so a run is the same whatever the hash order */
	handles = g_hash_table_get_keys (self->actors);
	handles = g_list_sort (handles, compare_handles);
	for (l = handles; l != NULL; l = l->next)
	{
		LrgTiledWorldActor *actor = g_hash_table_lookup (self->actors, l->data);
		g_autoptr(LrgActorMover) mover = NULL;

		/* an arrived handler may remove actors, this one included: keep
		 * its mover alive until its update returns */
		if (actor == NULL)
			continue;
		mover = g_object_ref (actor->mover);
		lrg_actor_mover_update (mover, delta);
	}
	update_camera (self);
}

static void
world_draw (LrgWorldAdapter *adapter)
{
	LrgTiledWorld *self = LRG_TILED_WORLD (adapter);
	GHashTableIter iter;
	gpointer value;
	guint tw;
	guint th;
	guint i;

	if (self->map == NULL || !IsWindowReady ())
		return;

	if (!self->textures_tried)
	{
		g_autoptr(GError) error = NULL;

		self->textures_tried = TRUE;
		self->textures_ok = lrg_tiled_map_load_textures (self->map, &error);
		if (!self->textures_ok)
			lrg_debug (LRG_LOG_DOMAIN_TILEMAP, "tiled world: drawing without tiles: %s",
			           error->message);
	}

	lrg_tiled_map_get_tile_size (self->map, &tw, &th);
	update_camera (self);
	lrg_camera_begin (LRG_CAMERA (self->camera));

	if (self->textures_ok)
	{
		for (i = 0; i < self->tilemaps->len; i++)
			lrg_tilemap_draw (g_ptr_array_index (self->tilemaps, i));
	}
	else
	{
		/* no tile images: walls as blocks, so the room is still readable */
		guint x;
		guint y;

		for (y = 0; y < lrg_nav_grid_get_height (self->grid); y++)
			for (x = 0; x < lrg_nav_grid_get_width (self->grid); x++)
				if (!lrg_nav_grid_is_walkable (self->grid, (gint) x, (gint) y))
					DrawRectangle ((gint) (x * tw), (gint) (y * th), (gint) tw, (gint) th,
					               (Color) { 70, 74, 90, 255 });
	}

	g_hash_table_iter_init (&iter, self->actors);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		LrgTiledWorldActor *actor = value;
		Color c = { actor->color.r, actor->color.g, actor->color.b, actor->color.a };
		Vector2 at;

		lrg_actor_mover_get_position (actor->mover, &at.x, &at.y, NULL);
		DrawCircleV (at, MIN (tw, th) * 0.4f, c);
	}
	lrg_camera_end (LRG_CAMERA (self->camera));
}

static void
lrg_tiled_world_adapter_init (LrgWorldAdapterInterface *iface)
{
	iface->load = world_load;
	iface->resolve_place = world_resolve_place;
	iface->spawn_actor = world_spawn_actor;
	iface->move_actor = world_move_actor;
	iface->remove_actor = world_remove_actor;
	iface->set_camera = world_set_camera;
	iface->set_camera_focus = world_set_camera_focus;
	iface->get_actor_position = world_get_actor_position;
	iface->update = world_update;
	iface->draw = world_draw;
}

/* ==========================================================================
 * Public
 * ========================================================================== */

LrgTiledMap *
lrg_tiled_world_get_map (LrgTiledWorld *self)
{
	g_return_val_if_fail (LRG_IS_TILED_WORLD (self), NULL);
	return self->map;
}

LrgNavGrid *
lrg_tiled_world_get_nav_grid (LrgTiledWorld *self)
{
	g_return_val_if_fail (LRG_IS_TILED_WORLD (self), NULL);
	return self->grid;
}

LrgCamera2D *
lrg_tiled_world_get_camera (LrgTiledWorld *self)
{
	g_return_val_if_fail (LRG_IS_TILED_WORLD (self), NULL);
	return self->camera;
}

void
lrg_tiled_world_set_viewport (LrgTiledWorld *self,
                              gint           width,
                              gint           height)
{
	g_return_if_fail (LRG_IS_TILED_WORLD (self));
	g_return_if_fail (width > 0 && height > 0);

	self->viewport_w = width;
	self->viewport_h = height;
	update_camera (self);
}

void
lrg_tiled_world_set_actor_speed (LrgTiledWorld *self,
                                 gfloat         speed)
{
	g_return_if_fail (LRG_IS_TILED_WORLD (self));
	g_return_if_fail (speed > 0.0f);
	self->actor_speed = speed;
}

LrgActorMover *
lrg_tiled_world_get_actor_mover (LrgTiledWorld *self,
                                 guint          handle)
{
	LrgTiledWorldActor *actor;

	g_return_val_if_fail (LRG_IS_TILED_WORLD (self), NULL);

	actor = g_hash_table_lookup (self->actors, GUINT_TO_POINTER (handle));
	return (actor != NULL) ? actor->mover : NULL;
}

guint
lrg_tiled_world_find_actor (LrgTiledWorld *self,
                            const gchar   *id)
{
	GHashTableIter iter;
	gpointer value;

	g_return_val_if_fail (LRG_IS_TILED_WORLD (self), 0);
	g_return_val_if_fail (id != NULL, 0);

	g_hash_table_iter_init (&iter, self->actors);
	while (g_hash_table_iter_next (&iter, NULL, &value))
	{
		LrgTiledWorldActor *actor = value;

		if (g_strcmp0 (actor->id, id) == 0)
			return actor->handle;
	}
	return 0;
}

guint
lrg_tiled_world_get_n_actors (LrgTiledWorld *self)
{
	g_return_val_if_fail (LRG_IS_TILED_WORLD (self), 0);
	return g_hash_table_size (self->actors);
}
