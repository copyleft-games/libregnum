/* lrg-game-object.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Game object with component support.
 *
 * LrgGameObject extends GrlEntity to add component-based functionality.
 * It inherits all transform properties (position, rotation, scale) and
 * rendering capabilities from graylib's entity system.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>

#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_GAME_OBJECT (lrg_game_object_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgGameObject, lrg_game_object, LRG, GAME_OBJECT, GrlEntity)

/**
 * LrgGameObjectClass:
 * @parent_class: The parent class (GrlEntityClass)
 *
 * The class structure for #LrgGameObject.
 *
 * LrgGameObject is derivable to allow creating specialized game objects.
 * The component system provides most customization, but subclassing can
 * be useful for objects that need custom draw or collision behavior.
 */
struct _LrgGameObjectClass
{
    GrlEntityClass parent_class;

    /*< private >*/
    gpointer _reserved[8];
};

/*
 * Construction
 */

/**
 * lrg_game_object_new:
 *
 * Creates a new game object at position (0, 0).
 *
 * Returns: (transfer full): A new #LrgGameObject
 */
LRG_AVAILABLE_IN_ALL
LrgGameObject * lrg_game_object_new             (void);

/**
 * lrg_game_object_new_at:
 * @x: Initial X position
 * @y: Initial Y position
 *
 * Creates a new game object at the specified position.
 *
 * Returns: (transfer full): A new #LrgGameObject
 */
LRG_AVAILABLE_IN_ALL
LrgGameObject * lrg_game_object_new_at          (gfloat          x,
                                                 gfloat          y);

/*
 * Component Management
 */

/**
 * lrg_game_object_add_component:
 * @self: an #LrgGameObject
 * @component: (transfer none): the component to add
 *
 * Adds a component to this game object.
 *
 * The game object takes a reference to the component and calls
 * its attached() virtual method. A component can only be attached
 * to one game object at a time.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_game_object_add_component   (LrgGameObject *self,
                                                 LrgComponent  *component);

/**
 * lrg_game_object_remove_component:
 * @self: an #LrgGameObject
 * @component: the component to remove
 *
 * Removes a component from this game object.
 *
 * The component's detached() virtual method is called, and the
 * game object releases its reference to the component.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_game_object_remove_component (LrgGameObject *self,
                                                  LrgComponent  *component);

/**
 * lrg_game_object_get_component:
 * @self: an #LrgGameObject
 * @component_type: the #GType of the component to find
 *
 * Finds a component by type.
 *
 * If the game object has multiple components of the same type,
 * the first one found is returned.
 *
 * Returns: (transfer none) (nullable): The component, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgComponent *  lrg_game_object_get_component   (LrgGameObject *self,
                                                 GType          component_type);

/**
 * lrg_game_object_get_components:
 * @self: an #LrgGameObject
 *
 * Gets a list of all components attached to this game object.
 *
 * Returns: (transfer container) (element-type LrgComponent): List of components
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_game_object_get_components  (LrgGameObject *self);

/**
 * lrg_game_object_has_component:
 * @self: an #LrgGameObject
 * @component_type: the #GType to check for
 *
 * Checks if the game object has a component of the specified type.
 *
 * Returns: %TRUE if a component of that type is attached
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_game_object_has_component   (LrgGameObject *self,
                                                 GType          component_type);

/**
 * lrg_game_object_get_components_of_type:
 * @self: an #LrgGameObject
 * @component_type: the #GType to find
 *
 * Gets all components of the specified type.
 *
 * Returns: (transfer container) (element-type LrgComponent): List of matching components
 */
LRG_AVAILABLE_IN_ALL
GList *         lrg_game_object_get_components_of_type (LrgGameObject *self,
                                                        GType          component_type);

/**
 * lrg_game_object_remove_all_components:
 * @self: an #LrgGameObject
 *
 * Removes all components from this game object.
 *
 * Each component's detached() method is called before removal.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_game_object_remove_all_components (LrgGameObject *self);

/**
 * lrg_game_object_get_component_count:
 * @self: an #LrgGameObject
 *
 * Gets the number of components attached to this game object.
 *
 * Returns: The number of attached components
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_game_object_get_component_count (LrgGameObject *self);

/*
 * Convenience Macro
 */

/**
 * lrg_game_object_get_component_of_type:
 * @obj: an #LrgGameObject
 * @T: the C type to cast to (e.g., LrgSpriteComponent)
 * @t: the #GType of the component (e.g., LRG_TYPE_SPRITE_COMPONENT)
 *
 * Gets a component and casts it to the specified type.
 *
 * This is a convenience macro that combines lrg_game_object_get_component()
 * with a type cast.
 *
 * Returns: (nullable): The component cast to type T, or %NULL if not found
 */
#define lrg_game_object_get_component_of_type(obj, T, t) \
    ((T *)lrg_game_object_get_component ((obj), (t)))

G_END_DECLS
