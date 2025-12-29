/* lrg-tween-group.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for tween groups (sequences, parallel).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-tween-base.h"

G_BEGIN_DECLS

#define LRG_TYPE_TWEEN_GROUP (lrg_tween_group_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTweenGroup, lrg_tween_group, LRG, TWEEN_GROUP, LrgTweenBase)

/**
 * LrgTweenGroupClass:
 * @parent_class: Parent class
 * @add_tween: Virtual method to add a tween to the group
 * @remove_tween: Virtual method to remove a tween from the group
 * @clear: Virtual method to clear all tweens
 * @get_tweens: Virtual method to get all tweens
 * @get_tween_count: Virtual method to get the number of tweens
 *
 * The class structure for #LrgTweenGroup.
 * Subclasses implement specific playback behavior (sequential or parallel).
 */
struct _LrgTweenGroupClass
{
    LrgTweenBaseClass parent_class;

    /* Virtual methods */

    /**
     * LrgTweenGroupClass::add_tween:
     * @self: A #LrgTweenGroup
     * @tween: The tween to add
     *
     * Adds a tween to the group.
     */
    void         (*add_tween)       (LrgTweenGroup   *self,
                                     LrgTweenBase    *tween);

    /**
     * LrgTweenGroupClass::remove_tween:
     * @self: A #LrgTweenGroup
     * @tween: The tween to remove
     *
     * Removes a tween from the group.
     *
     * Returns: %TRUE if the tween was found and removed
     */
    gboolean     (*remove_tween)    (LrgTweenGroup   *self,
                                     LrgTweenBase    *tween);

    /**
     * LrgTweenGroupClass::clear:
     * @self: A #LrgTweenGroup
     *
     * Removes all tweens from the group.
     */
    void         (*clear)           (LrgTweenGroup   *self);

    /**
     * LrgTweenGroupClass::get_tweens:
     * @self: A #LrgTweenGroup
     *
     * Gets the list of tweens in the group.
     *
     * Returns: (transfer none) (element-type LrgTweenBase): The tweens
     */
    GPtrArray *  (*get_tweens)      (LrgTweenGroup   *self);

    /**
     * LrgTweenGroupClass::get_tween_count:
     * @self: A #LrgTweenGroup
     *
     * Gets the number of tweens in the group.
     *
     * Returns: The number of tweens
     */
    guint        (*get_tween_count) (LrgTweenGroup   *self);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_tween_group_add_tween:
 * @self: A #LrgTweenGroup
 * @tween: (transfer none): The tween to add
 *
 * Adds a tween to the group. The tween will be started
 * according to the group's playback behavior.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_group_add_tween       (LrgTweenGroup   *self,
                                                 LrgTweenBase    *tween);

/**
 * lrg_tween_group_remove_tween:
 * @self: A #LrgTweenGroup
 * @tween: The tween to remove
 *
 * Removes a tween from the group.
 *
 * Returns: %TRUE if the tween was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_tween_group_remove_tween    (LrgTweenGroup   *self,
                                                 LrgTweenBase    *tween);

/**
 * lrg_tween_group_clear:
 * @self: A #LrgTweenGroup
 *
 * Removes all tweens from the group.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_tween_group_clear           (LrgTweenGroup   *self);

/**
 * lrg_tween_group_get_tweens:
 * @self: A #LrgTweenGroup
 *
 * Gets the list of tweens in the group.
 *
 * Returns: (transfer none) (element-type LrgTweenBase): The tweens
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *     lrg_tween_group_get_tweens      (LrgTweenGroup   *self);

/**
 * lrg_tween_group_get_tween_count:
 * @self: A #LrgTweenGroup
 *
 * Gets the number of tweens in the group.
 *
 * Returns: The number of tweens
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint           lrg_tween_group_get_tween_count (LrgTweenGroup   *self);

/**
 * lrg_tween_group_get_tween_at:
 * @self: A #LrgTweenGroup
 * @index: The index of the tween
 *
 * Gets a tween at the specified index.
 *
 * Returns: (transfer none) (nullable): The tween at @index, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenBase *  lrg_tween_group_get_tween_at    (LrgTweenGroup   *self,
                                                 guint            index);

G_END_DECLS
