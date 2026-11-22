/* lrg-world-adapter.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorldAdapter: the small contract an application uses to put its
 * own actors into a world it does not own -- load a world, find a named
 * place in it, spawn an actor, walk it somewhere, remove it, choose a
 * camera.  An office view, a game's town or a 3D level can each
 * implement it, and the application drives them all the same way.
 * LrgTiledWorld is the implementation shipped here, for Tiled maps.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LrgWorldCameraMode:
 * @LRG_WORLD_CAMERA_OVERVIEW: the whole world in view
 * @LRG_WORLD_CAMERA_FOLLOW: following the focused actor
 *   (lrg_world_adapter_set_camera_focus())
 * @LRG_WORLD_CAMERA_FREE: the application moves the camera itself
 *
 * How a world adapter points its camera.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_WORLD_CAMERA_OVERVIEW,
	LRG_WORLD_CAMERA_FOLLOW,
	LRG_WORLD_CAMERA_FREE
} LrgWorldCameraMode;

LRG_AVAILABLE_IN_ALL
GType lrg_world_camera_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WORLD_CAMERA_MODE (lrg_world_camera_mode_get_type ())

/**
 * LRG_WORLD_ADAPTER_ERROR:
 *
 * Error domain of world adapters.
 *
 * Since: 0.11.0
 */
#define LRG_WORLD_ADAPTER_ERROR (lrg_world_adapter_error_quark ())

LRG_AVAILABLE_IN_ALL
GQuark lrg_world_adapter_error_quark (void);

/**
 * LrgWorldAdapterError:
 * @LRG_WORLD_ADAPTER_ERROR_NOT_SUPPORTED: the adapter does not implement
 *   this call
 * @LRG_WORLD_ADAPTER_ERROR_NOT_LOADED: no world is loaded
 * @LRG_WORLD_ADAPTER_ERROR_LOAD: the world could not be loaded
 * @LRG_WORLD_ADAPTER_ERROR_UNKNOWN_PLACE: no place has that name
 * @LRG_WORLD_ADAPTER_ERROR_UNKNOWN_ACTOR: no actor has that handle
 * @LRG_WORLD_ADAPTER_ERROR_DUPLICATE_ACTOR: an actor with that id exists
 * @LRG_WORLD_ADAPTER_ERROR_UNREACHABLE: the actor cannot get there
 *
 * Errors of world adapters.
 *
 * Since: 0.11.0
 */
typedef enum
{
	LRG_WORLD_ADAPTER_ERROR_NOT_SUPPORTED,
	LRG_WORLD_ADAPTER_ERROR_NOT_LOADED,
	LRG_WORLD_ADAPTER_ERROR_LOAD,
	LRG_WORLD_ADAPTER_ERROR_UNKNOWN_PLACE,
	LRG_WORLD_ADAPTER_ERROR_UNKNOWN_ACTOR,
	LRG_WORLD_ADAPTER_ERROR_DUPLICATE_ACTOR,
	LRG_WORLD_ADAPTER_ERROR_UNREACHABLE
} LrgWorldAdapterError;

LRG_AVAILABLE_IN_ALL
GType lrg_world_adapter_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_WORLD_ADAPTER_ERROR (lrg_world_adapter_error_get_type ())

#define LRG_TYPE_WORLD_ADAPTER (lrg_world_adapter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgWorldAdapter, lrg_world_adapter, LRG, WORLD_ADAPTER, GObject)

/**
 * LrgWorldAdapterInterface:
 * @parent_iface: the parent interface
 * @load: loads a world from a path (a map, a level, a directory)
 * @resolve_place: finds a named place; FALSE when there is none
 * @spawn_actor: adds an actor and returns its handle (> 0), 0 on error
 * @move_actor: starts walking an actor to a point
 * @remove_actor: removes an actor; FALSE for an unknown handle
 * @set_camera: points the camera
 * @set_camera_focus: chooses the actor %LRG_WORLD_CAMERA_FOLLOW follows
 *   (optional)
 * @get_actor_position: reads where an actor is now (optional)
 * @update: advances the world by a time step (optional)
 * @draw: draws the world (optional; needs a GL context)
 *
 * The world adapter interface.  @load, @resolve_place, @spawn_actor,
 * @move_actor, @remove_actor and @set_camera are required; an adapter
 * that leaves one unset reports %LRG_WORLD_ADAPTER_ERROR_NOT_SUPPORTED
 * rather than pretending it worked.  Positions are world coordinates
 * of the adapter's own world (map pixels for a 2D map, units for 3D).
 * An adapter emits #LrgWorldAdapter::actor-arrived when an actor
 * finishes a move.
 *
 * Since: 0.11.0
 */
struct _LrgWorldAdapterInterface
{
	GTypeInterface parent_iface;

	gboolean (*load)               (LrgWorldAdapter     *self,
	                                const gchar         *world_path,
	                                GError             **error);
	gboolean (*resolve_place)      (LrgWorldAdapter     *self,
	                                const gchar         *name,
	                                gfloat              *x,
	                                gfloat              *y,
	                                gfloat              *z);
	guint    (*spawn_actor)        (LrgWorldAdapter     *self,
	                                const gchar         *id,
	                                const gchar         *appearance,
	                                GError             **error);
	gboolean (*move_actor)         (LrgWorldAdapter     *self,
	                                guint                handle,
	                                gfloat               x,
	                                gfloat               y,
	                                gfloat               z,
	                                GError             **error);
	gboolean (*remove_actor)       (LrgWorldAdapter     *self,
	                                guint                handle);
	void     (*set_camera)         (LrgWorldAdapter     *self,
	                                LrgWorldCameraMode   mode);
	void     (*set_camera_focus)   (LrgWorldAdapter     *self,
	                                guint                handle);
	gboolean (*get_actor_position) (LrgWorldAdapter     *self,
	                                guint                handle,
	                                gfloat              *x,
	                                gfloat              *y,
	                                gfloat              *z);
	void     (*update)             (LrgWorldAdapter     *self,
	                                gfloat               delta);
	void     (*draw)               (LrgWorldAdapter     *self);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_world_adapter_load:
 * @self: an #LrgWorldAdapter
 * @world_path: what to load; its meaning is the adapter's (a Tiled map
 *   file for #LrgTiledWorld)
 * @error: (nullable): return location for an error
 *
 * Loads a world, replacing any loaded before (and its actors).
 *
 * Returns: %TRUE on success
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_adapter_load (LrgWorldAdapter  *self,
                                 const gchar      *world_path,
                                 GError          **error);

/**
 * lrg_world_adapter_resolve_place:
 * @self: an #LrgWorldAdapter
 * @name: a place name (a desk, a room, a door)
 * @x: (out) (optional): x
 * @y: (out) (optional): y
 * @z: (out) (optional): z
 *
 * Finds a named place.
 *
 * Returns: %FALSE when the world has no such place (or none is loaded)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_adapter_resolve_place (LrgWorldAdapter *self,
                                          const gchar     *name,
                                          gfloat          *x,
                                          gfloat          *y,
                                          gfloat          *z);

/**
 * lrg_world_adapter_spawn_actor:
 * @self: an #LrgWorldAdapter
 * @id: the application's id for the actor, unique in the world
 * @appearance: (nullable): how it looks; the adapter's own vocabulary (a
 *   sprite sheet, a model, a colour)
 * @error: (nullable): return location for an error
 *
 * Adds an actor at the world's spawn point.
 *
 * Returns: the actor's handle (> 0), or 0 on error
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_world_adapter_spawn_actor (LrgWorldAdapter  *self,
                                     const gchar      *id,
                                     const gchar      *appearance,
                                     GError          **error);

/**
 * lrg_world_adapter_move_actor:
 * @self: an #LrgWorldAdapter
 * @handle: an actor handle
 * @x: target x
 * @y: target y
 * @z: target z
 * @error: (nullable): return location for an error
 *
 * Starts an actor walking to a point.  #LrgWorldAdapter::actor-arrived
 * is emitted when it gets there.
 *
 * Returns: %TRUE when the walk started
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_adapter_move_actor (LrgWorldAdapter  *self,
                                       guint             handle,
                                       gfloat            x,
                                       gfloat            y,
                                       gfloat            z,
                                       GError          **error);

/**
 * lrg_world_adapter_move_actor_to_place:
 * @self: an #LrgWorldAdapter
 * @handle: an actor handle
 * @place: a place name
 * @error: (nullable): return location for an error
 *
 * Resolves @place and moves the actor there.
 *
 * Returns: %TRUE when the walk started
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_adapter_move_actor_to_place (LrgWorldAdapter  *self,
                                                guint             handle,
                                                const gchar      *place,
                                                GError          **error);

/**
 * lrg_world_adapter_remove_actor:
 * @self: an #LrgWorldAdapter
 * @handle: an actor handle
 *
 * Removes an actor.
 *
 * Returns: %FALSE when there was no such actor
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_adapter_remove_actor (LrgWorldAdapter *self,
                                         guint            handle);

/**
 * lrg_world_adapter_set_camera:
 * @self: an #LrgWorldAdapter
 * @mode: how to point the camera
 *
 * Points the camera.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_adapter_set_camera (LrgWorldAdapter    *self,
                                   LrgWorldCameraMode  mode);

/**
 * lrg_world_adapter_set_camera_focus:
 * @self: an #LrgWorldAdapter
 * @handle: the actor to follow (0: none)
 *
 * Chooses who %LRG_WORLD_CAMERA_FOLLOW follows.  Does nothing on an
 * adapter without a follow camera.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_adapter_set_camera_focus (LrgWorldAdapter *self,
                                         guint            handle);

/**
 * lrg_world_adapter_get_actor_position:
 * @self: an #LrgWorldAdapter
 * @handle: an actor handle
 * @x: (out) (optional): x
 * @y: (out) (optional): y
 * @z: (out) (optional): z
 *
 * Reads where an actor is.
 *
 * Returns: %FALSE for an unknown actor, or an adapter that cannot say
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_adapter_get_actor_position (LrgWorldAdapter *self,
                                               guint            handle,
                                               gfloat          *x,
                                               gfloat          *y,
                                               gfloat          *z);

/**
 * lrg_world_adapter_update:
 * @self: an #LrgWorldAdapter
 * @delta: seconds
 *
 * Advances the world (actors walking).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_adapter_update (LrgWorldAdapter *self,
                               gfloat           delta);

/**
 * lrg_world_adapter_draw:
 * @self: an #LrgWorldAdapter
 *
 * Draws the world with its camera (inside BeginDrawing() or a texture
 * mode, for example lrg_frame_export_begin()).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_adapter_draw (LrgWorldAdapter *self);

G_END_DECLS
