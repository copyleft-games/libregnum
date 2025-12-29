/* lrg-settings-group.c - Abstract base class for settings groups
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-settings-group.h"
#include <gio/gio.h>

/**
 * SECTION:lrg-settings-group
 * @title: LrgSettingsGroup
 * @short_description: Abstract base class for settings groups
 *
 * #LrgSettingsGroup is an abstract base class that defines the interface
 * for all settings groups in Libregnum. Each settings group represents
 * a category of related settings (e.g., graphics, audio, controls).
 *
 * Subclasses must implement:
 * - apply() - Apply settings to the engine/system
 * - reset() - Reset to default values
 * - get_group_name() - Return serialization key
 * - serialize() - Convert to GVariant
 * - deserialize() - Load from GVariant
 *
 * The base class provides dirty tracking to know when settings need saving.
 */

typedef struct
{
    gboolean dirty;
} LrgSettingsGroupPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgSettingsGroup, lrg_settings_group, G_TYPE_OBJECT)

enum
{
    SIGNAL_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Default implementations - subclasses must override */

static void
lrg_settings_group_default_apply (LrgSettingsGroup *self)
{
    g_warning ("LrgSettingsGroup::apply not implemented for %s",
               G_OBJECT_TYPE_NAME (self));
}

static void
lrg_settings_group_default_reset (LrgSettingsGroup *self)
{
    g_warning ("LrgSettingsGroup::reset not implemented for %s",
               G_OBJECT_TYPE_NAME (self));
}

static const gchar *
lrg_settings_group_default_get_group_name (LrgSettingsGroup *self)
{
    g_warning ("LrgSettingsGroup::get_group_name not implemented for %s",
               G_OBJECT_TYPE_NAME (self));
    return "unknown";
}

static GVariant *
lrg_settings_group_default_serialize (LrgSettingsGroup  *self,
                                      GError           **error)
{
    g_set_error (error,
                 G_IO_ERROR,
                 G_IO_ERROR_NOT_SUPPORTED,
                 "LrgSettingsGroup::serialize not implemented for %s",
                 G_OBJECT_TYPE_NAME (self));
    return NULL;
}

static gboolean
lrg_settings_group_default_deserialize (LrgSettingsGroup  *self,
                                        GVariant          *data,
                                        GError           **error)
{
    g_set_error (error,
                 G_IO_ERROR,
                 G_IO_ERROR_NOT_SUPPORTED,
                 "LrgSettingsGroup::deserialize not implemented for %s",
                 G_OBJECT_TYPE_NAME (self));
    return FALSE;
}

static void
lrg_settings_group_class_init (LrgSettingsGroupClass *klass)
{
    /* Set default virtual method implementations */
    klass->apply = lrg_settings_group_default_apply;
    klass->reset = lrg_settings_group_default_reset;
    klass->get_group_name = lrg_settings_group_default_get_group_name;
    klass->serialize = lrg_settings_group_default_serialize;
    klass->deserialize = lrg_settings_group_default_deserialize;

    /**
     * LrgSettingsGroup::changed:
     * @self: the #LrgSettingsGroup that changed
     * @property_name: (nullable): name of the property that changed, or %NULL
     *
     * Emitted when a setting in the group is modified.
     */
    signals[SIGNAL_CHANGED] =
        g_signal_new ("changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

static void
lrg_settings_group_init (LrgSettingsGroup *self)
{
    LrgSettingsGroupPrivate *priv;

    priv = lrg_settings_group_get_instance_private (self);
    priv->dirty = FALSE;
}

/**
 * lrg_settings_group_apply:
 * @self: an #LrgSettingsGroup
 *
 * Applies the current settings to the engine or system.
 * This calls the virtual apply() method on the subclass.
 */
void
lrg_settings_group_apply (LrgSettingsGroup *self)
{
    LrgSettingsGroupClass *klass;

    g_return_if_fail (LRG_IS_SETTINGS_GROUP (self));

    klass = LRG_SETTINGS_GROUP_GET_CLASS (self);
    g_return_if_fail (klass->apply != NULL);

    klass->apply (self);
}

/**
 * lrg_settings_group_reset:
 * @self: an #LrgSettingsGroup
 *
 * Resets all settings in this group to their default values.
 * This calls the virtual reset() method on the subclass and
 * marks the group as dirty.
 */
void
lrg_settings_group_reset (LrgSettingsGroup *self)
{
    LrgSettingsGroupClass *klass;

    g_return_if_fail (LRG_IS_SETTINGS_GROUP (self));

    klass = LRG_SETTINGS_GROUP_GET_CLASS (self);
    g_return_if_fail (klass->reset != NULL);

    klass->reset (self);
    lrg_settings_group_mark_dirty (self);
}

/**
 * lrg_settings_group_get_group_name:
 * @self: an #LrgSettingsGroup
 *
 * Gets the group name used for serialization (e.g., "graphics", "audio").
 *
 * Returns: (transfer none): The group name string
 */
const gchar *
lrg_settings_group_get_group_name (LrgSettingsGroup *self)
{
    LrgSettingsGroupClass *klass;

    g_return_val_if_fail (LRG_IS_SETTINGS_GROUP (self), NULL);

    klass = LRG_SETTINGS_GROUP_GET_CLASS (self);
    g_return_val_if_fail (klass->get_group_name != NULL, NULL);

    return klass->get_group_name (self);
}

/**
 * lrg_settings_group_serialize:
 * @self: an #LrgSettingsGroup
 * @error: (nullable): return location for error
 *
 * Serializes the settings group to a #GVariant dictionary.
 *
 * Returns: (transfer full) (nullable): A new floating #GVariant, or %NULL on error
 */
GVariant *
lrg_settings_group_serialize (LrgSettingsGroup  *self,
                              GError           **error)
{
    LrgSettingsGroupClass *klass;

    g_return_val_if_fail (LRG_IS_SETTINGS_GROUP (self), NULL);

    klass = LRG_SETTINGS_GROUP_GET_CLASS (self);
    g_return_val_if_fail (klass->serialize != NULL, NULL);

    return klass->serialize (self, error);
}

/**
 * lrg_settings_group_deserialize:
 * @self: an #LrgSettingsGroup
 * @data: the #GVariant containing settings data
 * @error: (nullable): return location for error
 *
 * Deserializes settings from a #GVariant dictionary.
 * After successful deserialization, the group is marked clean.
 *
 * Returns: %TRUE on success, %FALSE on error
 */
gboolean
lrg_settings_group_deserialize (LrgSettingsGroup  *self,
                                GVariant          *data,
                                GError           **error)
{
    LrgSettingsGroupClass *klass;
    gboolean result;

    g_return_val_if_fail (LRG_IS_SETTINGS_GROUP (self), FALSE);
    g_return_val_if_fail (data != NULL, FALSE);

    klass = LRG_SETTINGS_GROUP_GET_CLASS (self);
    g_return_val_if_fail (klass->deserialize != NULL, FALSE);

    result = klass->deserialize (self, data, error);

    if (result)
    {
        lrg_settings_group_mark_clean (self);
    }

    return result;
}

/**
 * lrg_settings_group_is_dirty:
 * @self: an #LrgSettingsGroup
 *
 * Checks if the settings have been modified since last save/load.
 *
 * Returns: %TRUE if settings have been modified
 */
gboolean
lrg_settings_group_is_dirty (LrgSettingsGroup *self)
{
    LrgSettingsGroupPrivate *priv;

    g_return_val_if_fail (LRG_IS_SETTINGS_GROUP (self), FALSE);

    priv = lrg_settings_group_get_instance_private (self);
    return priv->dirty;
}

/**
 * lrg_settings_group_mark_dirty:
 * @self: an #LrgSettingsGroup
 *
 * Marks the settings group as modified.
 * This is automatically called when settings change.
 */
void
lrg_settings_group_mark_dirty (LrgSettingsGroup *self)
{
    LrgSettingsGroupPrivate *priv;

    g_return_if_fail (LRG_IS_SETTINGS_GROUP (self));

    priv = lrg_settings_group_get_instance_private (self);
    priv->dirty = TRUE;
}

/**
 * lrg_settings_group_mark_clean:
 * @self: an #LrgSettingsGroup
 *
 * Marks the settings group as unmodified (saved).
 * Called after successful serialization.
 */
void
lrg_settings_group_mark_clean (LrgSettingsGroup *self)
{
    LrgSettingsGroupPrivate *priv;

    g_return_if_fail (LRG_IS_SETTINGS_GROUP (self));

    priv = lrg_settings_group_get_instance_private (self);
    priv->dirty = FALSE;
}
