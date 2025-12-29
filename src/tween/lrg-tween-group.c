/* lrg-tween-group.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for tween groups (sequences, parallel).
 */

#include "lrg-tween-group.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TWEEN

/**
 * LrgTweenGroup:
 *
 * Abstract base class for groups of tweens.
 *
 * #LrgTweenGroup provides the base functionality for managing
 * a collection of tweens. Subclasses implement specific playback
 * behavior:
 *
 * - #LrgTweenSequence plays tweens one after another
 * - #LrgTweenParallel plays tweens simultaneously
 *
 * Since: 1.0
 */

typedef struct _LrgTweenGroupPrivate
{
    GPtrArray *tweens;
} LrgTweenGroupPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgTweenGroup, lrg_tween_group, LRG_TYPE_TWEEN_BASE)

/*
 * Default virtual method implementations
 */

static void
lrg_tween_group_real_add_tween (LrgTweenGroup *self,
                                LrgTweenBase  *tween)
{
    LrgTweenGroupPrivate *priv;

    priv = lrg_tween_group_get_instance_private (self);

    g_return_if_fail (tween != NULL);

    g_ptr_array_add (priv->tweens, g_object_ref (tween));
}

static gboolean
lrg_tween_group_real_remove_tween (LrgTweenGroup *self,
                                   LrgTweenBase  *tween)
{
    LrgTweenGroupPrivate *priv;

    priv = lrg_tween_group_get_instance_private (self);

    return g_ptr_array_remove (priv->tweens, tween);
}

static void
lrg_tween_group_real_clear (LrgTweenGroup *self)
{
    LrgTweenGroupPrivate *priv;

    priv = lrg_tween_group_get_instance_private (self);

    g_ptr_array_set_size (priv->tweens, 0);
}

static GPtrArray *
lrg_tween_group_real_get_tweens (LrgTweenGroup *self)
{
    LrgTweenGroupPrivate *priv;

    priv = lrg_tween_group_get_instance_private (self);

    return priv->tweens;
}

static guint
lrg_tween_group_real_get_tween_count (LrgTweenGroup *self)
{
    LrgTweenGroupPrivate *priv;

    priv = lrg_tween_group_get_instance_private (self);

    return priv->tweens->len;
}

/*
 * LrgTweenBase virtual method overrides
 */

static void
lrg_tween_group_reset (LrgTweenBase *base)
{
    LrgTweenGroup *self;
    LrgTweenGroupPrivate *priv;
    LrgTweenBaseClass *parent_class;
    guint i;

    self = LRG_TWEEN_GROUP (base);
    priv = lrg_tween_group_get_instance_private (self);

    /* Reset all child tweens */
    for (i = 0; i < priv->tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (priv->tweens, i);
        lrg_tween_base_reset (tween);
    }

    /* Chain up to parent */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_group_parent_class);
    if (parent_class->reset != NULL)
    {
        parent_class->reset (base);
    }
}

static void
lrg_tween_group_stop (LrgTweenBase *base)
{
    LrgTweenGroup *self;
    LrgTweenGroupPrivate *priv;
    LrgTweenBaseClass *parent_class;
    guint i;

    self = LRG_TWEEN_GROUP (base);
    priv = lrg_tween_group_get_instance_private (self);

    /* Stop all child tweens */
    for (i = 0; i < priv->tweens->len; i++)
    {
        LrgTweenBase *tween;

        tween = g_ptr_array_index (priv->tweens, i);
        lrg_tween_base_stop (tween);
    }

    /* Chain up to parent */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_group_parent_class);
    if (parent_class->stop != NULL)
    {
        parent_class->stop (base);
    }
}

/*
 * GObject virtual methods
 */

static void
lrg_tween_group_dispose (GObject *object)
{
    LrgTweenGroup *self;
    LrgTweenGroupPrivate *priv;

    self = LRG_TWEEN_GROUP (object);
    priv = lrg_tween_group_get_instance_private (self);

    g_ptr_array_set_size (priv->tweens, 0);

    G_OBJECT_CLASS (lrg_tween_group_parent_class)->dispose (object);
}

static void
lrg_tween_group_finalize (GObject *object)
{
    LrgTweenGroup *self;
    LrgTweenGroupPrivate *priv;

    self = LRG_TWEEN_GROUP (object);
    priv = lrg_tween_group_get_instance_private (self);

    g_clear_pointer (&priv->tweens, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_tween_group_parent_class)->finalize (object);
}

static void
lrg_tween_group_class_init (LrgTweenGroupClass *klass)
{
    GObjectClass *object_class;
    LrgTweenBaseClass *tween_base_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = lrg_tween_group_dispose;
    object_class->finalize = lrg_tween_group_finalize;

    tween_base_class = LRG_TWEEN_BASE_CLASS (klass);
    tween_base_class->reset = lrg_tween_group_reset;
    tween_base_class->stop = lrg_tween_group_stop;

    /* Default implementations */
    klass->add_tween = lrg_tween_group_real_add_tween;
    klass->remove_tween = lrg_tween_group_real_remove_tween;
    klass->clear = lrg_tween_group_real_clear;
    klass->get_tweens = lrg_tween_group_real_get_tweens;
    klass->get_tween_count = lrg_tween_group_real_get_tween_count;
}

static void
lrg_tween_group_init (LrgTweenGroup *self)
{
    LrgTweenGroupPrivate *priv;

    priv = lrg_tween_group_get_instance_private (self);

    priv->tweens = g_ptr_array_new_with_free_func (g_object_unref);
}

/*
 * Public API
 */

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
void
lrg_tween_group_add_tween (LrgTweenGroup *self,
                           LrgTweenBase  *tween)
{
    LrgTweenGroupClass *klass;

    g_return_if_fail (LRG_IS_TWEEN_GROUP (self));
    g_return_if_fail (LRG_IS_TWEEN_BASE (tween));

    klass = LRG_TWEEN_GROUP_GET_CLASS (self);
    if (klass->add_tween != NULL)
    {
        klass->add_tween (self, tween);
    }
}

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
gboolean
lrg_tween_group_remove_tween (LrgTweenGroup *self,
                              LrgTweenBase  *tween)
{
    LrgTweenGroupClass *klass;

    g_return_val_if_fail (LRG_IS_TWEEN_GROUP (self), FALSE);
    g_return_val_if_fail (LRG_IS_TWEEN_BASE (tween), FALSE);

    klass = LRG_TWEEN_GROUP_GET_CLASS (self);
    if (klass->remove_tween != NULL)
    {
        return klass->remove_tween (self, tween);
    }

    return FALSE;
}

/**
 * lrg_tween_group_clear:
 * @self: A #LrgTweenGroup
 *
 * Removes all tweens from the group.
 *
 * Since: 1.0
 */
void
lrg_tween_group_clear (LrgTweenGroup *self)
{
    LrgTweenGroupClass *klass;

    g_return_if_fail (LRG_IS_TWEEN_GROUP (self));

    klass = LRG_TWEEN_GROUP_GET_CLASS (self);
    if (klass->clear != NULL)
    {
        klass->clear (self);
    }
}

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
GPtrArray *
lrg_tween_group_get_tweens (LrgTweenGroup *self)
{
    LrgTweenGroupClass *klass;

    g_return_val_if_fail (LRG_IS_TWEEN_GROUP (self), NULL);

    klass = LRG_TWEEN_GROUP_GET_CLASS (self);
    if (klass->get_tweens != NULL)
    {
        return klass->get_tweens (self);
    }

    return NULL;
}

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
guint
lrg_tween_group_get_tween_count (LrgTweenGroup *self)
{
    LrgTweenGroupClass *klass;

    g_return_val_if_fail (LRG_IS_TWEEN_GROUP (self), 0);

    klass = LRG_TWEEN_GROUP_GET_CLASS (self);
    if (klass->get_tween_count != NULL)
    {
        return klass->get_tween_count (self);
    }

    return 0;
}

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
LrgTweenBase *
lrg_tween_group_get_tween_at (LrgTweenGroup *self,
                              guint          index)
{
    LrgTweenGroupPrivate *priv;

    g_return_val_if_fail (LRG_IS_TWEEN_GROUP (self), NULL);

    priv = lrg_tween_group_get_instance_private (self);

    if (index >= priv->tweens->len)
    {
        return NULL;
    }

    return g_ptr_array_index (priv->tweens, index);
}
