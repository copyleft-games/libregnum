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

#define LRG_TYPE_COMPONENT (lrg_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgComponent, lrg_component, LRG, COMPONENT, GObject)

/**
 * LrgComponentClass:
 * @parent_class: The parent class
 * @attached: Virtual method called when the component is attached to a game object
 * @detached: Virtual method called when the component is detached from a game object
 * @update: Virtual method called each frame to update the component
 *
 * The class structure for #LrgComponent.
 *
 * Subclasses can override these virtual methods to customize behavior.
 * The @attached method is called after the component is attached to a
 * game object, and @detached is called before it is removed.
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

    /*< private >*/
    gpointer _reserved[8];
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

G_END_DECLS
