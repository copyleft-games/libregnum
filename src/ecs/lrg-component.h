/* lrg-component.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for game object components.
 *
 * Components are modular pieces of functionality that can be attached
 * to game objects. Custom components should derive from this class
 * and override the virtual methods as needed.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/* Declared in world3d/lrg-entity3d.h and world3d/lrg-world3d.h; only
 * pointers are needed here, so the 2D ECS does not include the 3D headers. */
typedef struct _LrgEntity3D   LrgEntity3D;
typedef struct _LrgWorld3D    LrgWorld3D;
typedef struct _LrgRenderList LrgRenderList;

#define LRG_TYPE_COMPONENT (lrg_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgComponent, lrg_component, LRG, COMPONENT, GObject)

/**
 * LrgComponentClass:
 * @parent_class: The parent class
 * @attached: Virtual method called when the component is attached to a game object
 * @detached: Virtual method called when the component is detached from a game object
 * @update: Virtual method called each frame to update the component
 * @attached_entity: called after the component is added to an #LrgEntity3D
 * @detached_entity: called before the component leaves an #LrgEntity3D
 * @fixed_update: called at the #LrgWorld3D fixed rate
 * @late_update: called after every component's update
 * @collect_render: adds #LrgRenderItem records to an #LrgRenderList
 * @get_bounds: reports local-space bounds (optional)
 * @world_entered: the owning entity joined an #LrgWorld3D
 * @world_exited: the owning entity is leaving an #LrgWorld3D
 *
 * The class structure for #LrgComponent.
 *
 * Subclasses can override these virtual methods to customize behavior.
 * The @attached method is called after the component is attached to a
 * game object, and @detached is called before it is removed.
 *
 * Since 0.9.0 the eight former reserved pointer slots hold the 3D hooks
 * (@attached_entity .. @world_exited, in that order), so the class size
 * and the offsets of @attached, @detached and @update are unchanged and
 * existing 2D subclasses keep working without recompiling.  The 2D hooks
 * (@attached, @detached) fire only for #LrgGameObject owners; the 3D
 * hooks fire only for #LrgEntity3D owners; @update fires for both.
 */
struct _LrgComponentClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgComponentClass::attached:
     * @self: the component
     * @owner: the game object this component was attached to
     *
     * Called when the component is attached to a game object.
     * Subclasses can override this to perform initialization that
     * requires the owner to be set.
     */
    void (*attached)    (LrgComponent  *self,
                         LrgGameObject *owner);

    /**
     * LrgComponentClass::detached:
     * @self: the component
     *
     * Called when the component is about to be detached from its owner.
     * Subclasses can override this to perform cleanup.
     */
    void (*detached)    (LrgComponent  *self);

    /**
     * LrgComponentClass::update:
     * @self: the component
     * @delta: time elapsed since last frame in seconds
     *
     * Called each frame to update the component.
     * Only called if the component is enabled.
     */
    void (*update)      (LrgComponent  *self,
                         gfloat         delta);

    /* --- 0.9.0 hooks for #LrgEntity3D owners (formerly _reserved[0..7]) --- */

    /**
     * LrgComponentClass::attached_entity:
     * @self: the component
     * @entity: the #LrgEntity3D this component was attached to
     *
     * Called after the component is added to an #LrgEntity3D (the 3D
     * counterpart of @attached, which only fires for #LrgGameObject
     * owners).  Since: 0.9.0 (was _reserved[0])
     */
    void (*attached_entity) (LrgComponent *self,
                             LrgEntity3D  *entity);

    /**
     * LrgComponentClass::detached_entity:
     * @self: the component
     * @entity: the #LrgEntity3D the component is leaving
     *
     * Called before the component is removed from an #LrgEntity3D (and
     * when the entity is disposed).  Since: 0.9.0 (was _reserved[1])
     */
    void (*detached_entity) (LrgComponent *self,
                             LrgEntity3D  *entity);

    /**
     * LrgComponentClass::fixed_update:
     * @self: the component
     * @step: the fixed timestep in seconds
     *
     * Called zero or more times per frame at the #LrgWorld3D fixed rate,
     * before update.  Physics and deterministic gameplay go here.  Only
     * called if the component is enabled.  Since: 0.9.0 (was
     * _reserved[2])
     */
    void (*fixed_update)    (LrgComponent *self,
                             gfloat        step);

    /**
     * LrgComponentClass::late_update:
     * @self: the component
     * @delta: time elapsed since last frame in seconds
     *
     * Called once per frame after every component's update.  Cameras
     * following targets and light registration go here.  Only called if
     * the component is enabled.  Since: 0.9.0 (was _reserved[3])
     */
    void (*late_update)     (LrgComponent *self,
                             gfloat        delta);

    /**
     * LrgComponentClass::collect_render:
     * @self: the component
     * @list: the render list being built
     *
     * Called by lrg_world3d_collect_render() for enabled components of
     * active, visible entities whose layer the camera sees.  Add
     * #LrgRenderItem records with lrg_render_list_add(), normally after
     * lrg_render_item_init_for_entity() and filtered through
     * lrg_render_list_accepts().  Since: 0.9.0 (was _reserved[4])
     */
    void (*collect_render)  (LrgComponent  *self,
                             LrgRenderList *list);

    /**
     * LrgComponentClass::get_bounds:
     * @self: the component
     * @out_local: (out caller-allocates): bounds in the entity's local
     *   space
     *
     * Optional.  Reports what the component occupies, for the world
     * octree and spatial queries.  Since: 0.9.0 (was _reserved[5])
     *
     * Returns: %TRUE if @out_local was filled
     */
    gboolean (*get_bounds)  (LrgComponent     *self,
                             LrgBoundingBox3D *out_local);

    /**
     * LrgComponentClass::world_entered:
     * @self: the component
     * @world: the #LrgWorld3D the owning entity joined
     *
     * Called when the owning entity joins a world, or when the component
     * is added to an entity that is already in one.  Register lights,
     * cameras and listeners here.  Since: 0.9.0 (was _reserved[6])
     */
    void (*world_entered)   (LrgComponent *self,
                             LrgWorld3D   *world);

    /**
     * LrgComponentClass::world_exited:
     * @self: the component
     * @world: the #LrgWorld3D the owning entity is leaving
     *
     * Called before the owning entity leaves a world, or before the
     * component is removed from an entity in one.  Unregister here.
     * Since: 0.9.0 (was _reserved[7])
     */
    void (*world_exited)    (LrgComponent *self,
                             LrgWorld3D   *world);
};

/*
 * Property Accessors
 */

/**
 * lrg_component_get_owner:
 * @self: an #LrgComponent
 *
 * Gets the game object that owns this component.
 *
 * Returns: (transfer none) (nullable): The owning #LrgGameObject, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgGameObject * lrg_component_get_owner     (LrgComponent *self);

/**
 * lrg_component_get_enabled:
 * @self: an #LrgComponent
 *
 * Gets whether this component is enabled.
 *
 * Disabled components do not receive update() calls.
 *
 * Returns: %TRUE if the component is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_component_get_enabled   (LrgComponent *self);

/**
 * lrg_component_set_enabled:
 * @self: an #LrgComponent
 * @enabled: whether to enable the component
 *
 * Sets whether this component is enabled.
 *
 * Disabled components do not receive update() calls.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_component_set_enabled   (LrgComponent *self,
                                             gboolean      enabled);

/**
 * lrg_component_get_entity:
 * @self: an #LrgComponent
 *
 * Gets the #LrgEntity3D that owns this component.  A component has at
 * most one owner: an #LrgGameObject (lrg_component_get_owner()) or an
 * #LrgEntity3D (this), never both.
 *
 * Returns: (transfer none) (nullable): the owning entity, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D *   lrg_component_get_entity    (LrgComponent *self);

/**
 * lrg_component_get_owner_object:
 * @self: an #LrgComponent
 *
 * Gets the owner whatever its kind: an #LrgGameObject, an #LrgEntity3D,
 * or %NULL.
 *
 * Returns: (transfer none) (nullable): the owner
 */
LRG_AVAILABLE_IN_ALL
GObject *       lrg_component_get_owner_object (LrgComponent *self);

/**
 * lrg_component_get_world3d:
 * @self: an #LrgComponent
 *
 * Shortcut for lrg_entity3d_get_world() on lrg_component_get_entity().
 *
 * Returns: (transfer none) (nullable): the world the owning entity is in
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3D *    lrg_component_get_world3d   (LrgComponent *self);

/*
 * Methods
 */

/**
 * lrg_component_update:
 * @self: an #LrgComponent
 * @delta: time elapsed since last frame in seconds
 *
 * Updates the component for the current frame.
 *
 * This calls the virtual update() method if the component is enabled.
 * Typically called by the owning game object, not directly.
 */
LRG_AVAILABLE_IN_ALL
void            lrg_component_update        (LrgComponent *self,
                                             gfloat        delta);

/**
 * lrg_component_fixed_update:
 * @self: an #LrgComponent
 * @step: the fixed timestep in seconds
 *
 * Calls the fixed_update() vfunc if the component is enabled.  Normally
 * called by lrg_world3d_fixed_update().
 */
LRG_AVAILABLE_IN_ALL
void            lrg_component_fixed_update  (LrgComponent *self,
                                             gfloat        step);

/**
 * lrg_component_late_update:
 * @self: an #LrgComponent
 * @delta: time elapsed since last frame in seconds
 *
 * Calls the late_update() vfunc if the component is enabled.  Normally
 * called by lrg_world3d_late_update().
 */
LRG_AVAILABLE_IN_ALL
void            lrg_component_late_update   (LrgComponent *self,
                                             gfloat        delta);

/**
 * lrg_component_collect_render:
 * @self: an #LrgComponent
 * @list: the render list being built
 *
 * Calls the collect_render() vfunc if the component is enabled.
 * Normally called by lrg_world3d_collect_render().
 */
LRG_AVAILABLE_IN_ALL
void            lrg_component_collect_render (LrgComponent  *self,
                                              LrgRenderList *list);

/**
 * lrg_component_get_bounds:
 * @self: an #LrgComponent
 * @out_local: (out caller-allocates): local-space bounds
 *
 * Calls the get_bounds() vfunc.
 *
 * Returns: %TRUE if the component reported bounds
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_component_get_bounds    (LrgComponent     *self,
                                             LrgBoundingBox3D *out_local);

G_END_DECLS
