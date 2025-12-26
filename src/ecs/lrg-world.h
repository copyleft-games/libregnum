/* lrg-world.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * World container for game objects.
 *
 * LrgWorld provides a container for managing game objects in a scene.
 * It wraps graylib's GrlScene and provides game-object-centric APIs
 * for adding, removing, and finding objects.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>

#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-game-object.h"

G_BEGIN_DECLS

#define LRG_TYPE_WORLD (lrg_world_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWorld, lrg_world, LRG, WORLD, GObject)

/*
 * Construction
 */

/**
 * lrg_world_new:
 *
 * Creates a new empty world.
 *
 * Returns: (transfer full): A new #LrgWorld
 */
LRG_AVAILABLE_IN_ALL
LrgWorld *      lrg_world_new               (void);

/*
 * Game Object Management
 */

/**
 * lrg_world_add_object:
 * @self: an #LrgWorld
 * @object: (transfer none): the game object to add
 *
 * Adds a game object to this world.
 *
 * The world takes a reference to the object.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_world_add_object        (LrgWorld      *self,
                                             LrgGameObject *object);

/**
 * lrg_world_remove_object:
 * @self: an #LrgWorld
 * @object: the game object to remove
 *
 * Removes a game object from this world.
 *
 * The world releases its reference to the object.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_world_remove_object     (LrgWorld      *self,
                                             LrgGameObject *object);

/**
 * lrg_world_clear:
 * @self: an #LrgWorld
 *
 * Removes all game objects from this world.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_world_clear             (LrgWorld *self);

/**
 * lrg_world_get_objects:
 * @self: an #LrgWorld
 *
 * Gets a list of all game objects in this world.
 *
 * Returns: (transfer container) (element-type LrgGameObject): List of objects
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_world_get_objects       (LrgWorld *self);

/**
 * lrg_world_get_object_count:
 * @self: an #LrgWorld
 *
 * Gets the number of game objects in this world.
 *
 * Returns: The number of objects
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_world_get_object_count  (LrgWorld *self);

/*
 * Object Lookup
 */

/**
 * lrg_world_find_by_tag:
 * @self: an #LrgWorld
 * @tag: the tag to search for
 *
 * Finds the first game object with the specified tag.
 *
 * Returns: (transfer none) (nullable): The game object, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgGameObject * lrg_world_find_by_tag       (LrgWorld    *self,
                                             const gchar *tag);

/**
 * lrg_world_find_all_by_tag:
 * @self: an #LrgWorld
 * @tag: the tag to search for
 *
 * Finds all game objects with the specified tag.
 *
 * Returns: (transfer container) (element-type LrgGameObject): List of matching objects
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_world_find_all_by_tag   (LrgWorld    *self,
                                             const gchar *tag);

/*
 * Frame Processing
 */

/**
 * lrg_world_update:
 * @self: an #LrgWorld
 * @delta: time elapsed since last frame in seconds
 *
 * Updates all game objects in the world.
 *
 * Only active objects are updated. If the world is paused,
 * no objects are updated.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_world_update            (LrgWorld *self,
                                             gfloat    delta);

/**
 * lrg_world_draw:
 * @self: an #LrgWorld
 *
 * Draws all visible game objects in the world.
 *
 * Objects are drawn in z-index order (lowest first).
 */
LRG_AVAILABLE_IN_ALL
void            lrg_world_draw              (LrgWorld *self);

/*
 * graylib Integration
 */

/**
 * lrg_world_get_scene:
 * @self: an #LrgWorld
 *
 * Gets the underlying graylib scene.
 *
 * This can be used to access graylib-specific features or
 * to add non-game-object entities to the scene.
 *
 * Returns: (transfer none): The #GrlScene
 */
LRG_AVAILABLE_IN_ALL
GrlScene *      lrg_world_get_scene         (LrgWorld *self);

/*
 * Properties
 */

/**
 * lrg_world_get_active:
 * @self: an #LrgWorld
 *
 * Gets whether the world is active.
 *
 * Returns: %TRUE if the world is active
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_world_get_active        (LrgWorld *self);

/**
 * lrg_world_set_active:
 * @self: an #LrgWorld
 * @active: whether the world should be active
 *
 * Sets whether the world is active.
 *
 * Inactive worlds do not update or draw their objects.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_world_set_active        (LrgWorld *self,
                                             gboolean  active);

/**
 * lrg_world_get_paused:
 * @self: an #LrgWorld
 *
 * Gets whether the world is paused.
 *
 * Returns: %TRUE if the world is paused
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_world_get_paused        (LrgWorld *self);

/**
 * lrg_world_set_paused:
 * @self: an #LrgWorld
 * @paused: whether the world should be paused
 *
 * Sets whether the world is paused.
 *
 * Paused worlds still draw their objects but do not update them.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_world_set_paused        (LrgWorld *self,
                                             gboolean  paused);

G_END_DECLS
