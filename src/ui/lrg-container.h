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
 * lrg_container_insert_child_at:
 * @self: an #LrgContainer
 * @child: a widget without a parent
 * @index: position among the children; past the end (e.g. -1 cast to
 *   guint, or G_MAXUINT) appends
 *
 * Like lrg_container_add_child(), but at @index. Children are laid out
 * and painted in order, so this also decides stacking (later on top).
 */
LRG_AVAILABLE_IN_ALL
void lrg_container_insert_child_at (LrgContainer *self,
                                    LrgWidget    *child,
                                    guint         index);

/**
 * lrg_container_reorder_child:
 * @self: an #LrgContainer
 * @child: a child of @self
 * @index: its new position; past the end moves it last (on top)
 *
 * Moves @child to @index without removing it (it keeps its state,
 * focus and pointer capture) and lays the container out again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_container_reorder_child (LrgContainer *self,
                                  LrgWidget    *child,
                                  guint         index);

/**
 * lrg_container_get_overflow_hit_test:
 * @self: an #LrgContainer
 *
 * Returns: %TRUE if pointer hit-testing looks for children outside
 *   the container's own rectangle
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_container_get_overflow_hit_test (LrgContainer *self);

/**
 * lrg_container_set_overflow_hit_test:
 * @self: an #LrgContainer
 * @overflow: whether children placed outside the container's
 *   rectangle can be hit
 *
 * Normally the canvas only looks for children under the pointer when
 * the pointer is inside the container. Layouts that place children
 * beyond their own bounds (anchor layouts, callouts, badges hanging off
 * an edge) turn this on so those children still receive clicks and
 * hover. The container itself is only hit inside its rectangle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_container_set_overflow_hit_test (LrgContainer *self,
                                          gboolean      overflow);

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
