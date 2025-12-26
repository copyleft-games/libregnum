/* lrg-container.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract container widget that can hold child widgets.
 *
 * Containers manage a list of child widgets and are responsible
 * for laying them out. Subclasses implement the layout_children()
 * virtual method to position their children.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_CONTAINER (lrg_container_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgContainer, lrg_container, LRG, CONTAINER, LrgWidget)

/**
 * LrgContainerClass:
 * @parent_class: The parent class
 * @layout_children: Virtual method to position child widgets
 *
 * The class structure for #LrgContainer.
 *
 * Subclasses should override @layout_children to implement
 * their specific layout algorithm (vertical, horizontal, grid, etc.).
 */
struct _LrgContainerClass
{
    LrgWidgetClass parent_class;

    /* Virtual methods */

    /**
     * LrgContainerClass::layout_children:
     * @self: the container
     *
     * Positions all child widgets according to the container's
     * layout algorithm. Called automatically when children are
     * added/removed or when the container's size changes.
     */
    void (*layout_children) (LrgContainer *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Child Management
 * ========================================================================== */

/**
 * lrg_container_add_child:
 * @self: an #LrgContainer
 * @child: the widget to add
 *
 * Adds a child widget to this container.
 * The container takes a reference to the child.
 */
LRG_AVAILABLE_IN_ALL
void lrg_container_add_child (LrgContainer *self,
                              LrgWidget    *child);

/**
 * lrg_container_remove_child:
 * @self: an #LrgContainer
 * @child: the widget to remove
 *
 * Removes a child widget from this container.
 * The child's reference is released.
 */
LRG_AVAILABLE_IN_ALL
void lrg_container_remove_child (LrgContainer *self,
                                 LrgWidget    *child);

/**
 * lrg_container_remove_all:
 * @self: an #LrgContainer
 *
 * Removes all child widgets from this container.
 */
LRG_AVAILABLE_IN_ALL
void lrg_container_remove_all (LrgContainer *self);

/**
 * lrg_container_get_child_count:
 * @self: an #LrgContainer
 *
 * Gets the number of children in this container.
 *
 * Returns: The child count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_container_get_child_count (LrgContainer *self);

/**
 * lrg_container_get_child:
 * @self: an #LrgContainer
 * @index: the child index
 *
 * Gets a child widget by index.
 *
 * Returns: (transfer none) (nullable): The child at @index, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_container_get_child (LrgContainer *self,
                                     guint         index);

/**
 * lrg_container_get_children:
 * @self: an #LrgContainer
 *
 * Gets the list of all children.
 *
 * Returns: (transfer none) (element-type LrgWidget): The children list
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_container_get_children (LrgContainer *self);

/* ==========================================================================
 * Layout Properties
 * ========================================================================== */

/**
 * lrg_container_get_spacing:
 * @self: an #LrgContainer
 *
 * Gets the spacing between child widgets.
 *
 * Returns: The spacing in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_container_get_spacing (LrgContainer *self);

/**
 * lrg_container_set_spacing:
 * @self: an #LrgContainer
 * @spacing: the spacing in pixels
 *
 * Sets the spacing between child widgets.
 */
LRG_AVAILABLE_IN_ALL
void lrg_container_set_spacing (LrgContainer *self,
                                gfloat        spacing);

/**
 * lrg_container_get_padding:
 * @self: an #LrgContainer
 *
 * Gets the padding around the container's content.
 *
 * Returns: The padding in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_container_get_padding (LrgContainer *self);

/**
 * lrg_container_set_padding:
 * @self: an #LrgContainer
 * @padding: the padding in pixels
 *
 * Sets the padding around the container's content.
 */
LRG_AVAILABLE_IN_ALL
void lrg_container_set_padding (LrgContainer *self,
                                gfloat        padding);

/* ==========================================================================
 * Layout
 * ========================================================================== */

/**
 * lrg_container_layout_children:
 * @self: an #LrgContainer
 *
 * Triggers the layout_children() virtual method.
 * Call this after changing layout-affecting properties.
 */
LRG_AVAILABLE_IN_ALL
void lrg_container_layout_children (LrgContainer *self);

G_END_DECLS
