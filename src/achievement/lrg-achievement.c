/* lrg-achievement.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievement - Base class for achievement definitions.
 */

#include "config.h"

#include "lrg-achievement.h"
#include "../lrg-log.h"

typedef struct
{
    gchar    *id;
    gchar    *name;
    gchar    *description;
    gchar    *icon;
    gchar    *locked_icon;
    guint     points;
    gboolean  hidden;
    gboolean  unlocked;
    GDateTime *unlock_time;
    LrgAchievementProgress *progress;
} LrgAchievementPrivate;

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_ICON,
    PROP_LOCKED_ICON,
    PROP_POINTS,
    PROP_HIDDEN,
    PROP_UNLOCKED,
    N_PROPS
};

enum
{
    SIGNAL_UNLOCKED,
    SIGNAL_PROGRESS_CHANGED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_TYPE_WITH_PRIVATE (LrgAchievement, lrg_achievement, G_TYPE_OBJECT)

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_achievement_real_check_unlock (LrgAchievement *self)
{
    LrgAchievementPrivate *priv = lrg_achievement_get_instance_private (self);

    /* Default: unlock when progress is complete, or if no progress tracking */
    if (priv->progress == NULL)
        return FALSE;

    return lrg_achievement_progress_is_complete (priv->progress);
}

static void
lrg_achievement_real_on_unlocked (LrgAchievement *self)
{
    LrgAchievementPrivate *priv = lrg_achievement_get_instance_private (self);

    lrg_info (LRG_LOG_DOMAIN_ACHIEVEMENT,
                  "Achievement unlocked: %s (%s)",
                  priv->name, priv->id);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_achievement_finalize (GObject *object)
{
    LrgAchievement *self = LRG_ACHIEVEMENT (object);
    LrgAchievementPrivate *priv = lrg_achievement_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->description, g_free);
    g_clear_pointer (&priv->icon, g_free);
    g_clear_pointer (&priv->locked_icon, g_free);
    g_clear_pointer (&priv->unlock_time, g_date_time_unref);
    g_clear_pointer (&priv->progress, lrg_achievement_progress_free);

    G_OBJECT_CLASS (lrg_achievement_parent_class)->finalize (object);
}

static void
lrg_achievement_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    LrgAchievement *self = LRG_ACHIEVEMENT (object);
    LrgAchievementPrivate *priv = lrg_achievement_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;
    case PROP_DESCRIPTION:
        g_value_set_string (value, priv->description);
        break;
    case PROP_ICON:
        g_value_set_string (value, priv->icon);
        break;
    case PROP_LOCKED_ICON:
        g_value_set_string (value, priv->locked_icon);
        break;
    case PROP_POINTS:
        g_value_set_uint (value, priv->points);
        break;
    case PROP_HIDDEN:
        g_value_set_boolean (value, priv->hidden);
        break;
    case PROP_UNLOCKED:
        g_value_set_boolean (value, priv->unlocked);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_achievement_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    LrgAchievement *self = LRG_ACHIEVEMENT (object);
    LrgAchievementPrivate *priv = lrg_achievement_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_clear_pointer (&priv->id, g_free);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_clear_pointer (&priv->name, g_free);
        priv->name = g_value_dup_string (value);
        break;
    case PROP_DESCRIPTION:
        g_clear_pointer (&priv->description, g_free);
        priv->description = g_value_dup_string (value);
        break;
    case PROP_ICON:
        g_clear_pointer (&priv->icon, g_free);
        priv->icon = g_value_dup_string (value);
        break;
    case PROP_LOCKED_ICON:
        g_clear_pointer (&priv->locked_icon, g_free);
        priv->locked_icon = g_value_dup_string (value);
        break;
    case PROP_POINTS:
        priv->points = g_value_get_uint (value);
        break;
    case PROP_HIDDEN:
        priv->hidden = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_achievement_class_init (LrgAchievementClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_achievement_finalize;
    object_class->get_property = lrg_achievement_get_property;
    object_class->set_property = lrg_achievement_set_property;

    /* Default virtual method implementations */
    klass->check_unlock = lrg_achievement_real_check_unlock;
    klass->on_unlocked = lrg_achievement_real_on_unlocked;

    /**
     * LrgAchievement:id:
     *
     * Unique identifier for this achievement.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique achievement identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievement:name:
     *
     * Display name of the achievement.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievement:description:
     *
     * Description of the achievement.
     *
     * Since: 1.0
     */
    properties[PROP_DESCRIPTION] =
        g_param_spec_string ("description",
                             "Description",
                             "Achievement description",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievement:icon:
     *
     * Path to the unlocked icon.
     *
     * Since: 1.0
     */
    properties[PROP_ICON] =
        g_param_spec_string ("icon",
                             "Icon",
                             "Path to unlocked icon",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievement:locked-icon:
     *
     * Path to the locked icon.
     *
     * Since: 1.0
     */
    properties[PROP_LOCKED_ICON] =
        g_param_spec_string ("locked-icon",
                             "Locked Icon",
                             "Path to locked icon",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievement:points:
     *
     * Point value of the achievement.
     *
     * Since: 1.0
     */
    properties[PROP_POINTS] =
        g_param_spec_uint ("points",
                           "Points",
                           "Point value",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievement:hidden:
     *
     * Whether this achievement is hidden until unlocked.
     *
     * Since: 1.0
     */
    properties[PROP_HIDDEN] =
        g_param_spec_boolean ("hidden",
                              "Hidden",
                              "Whether hidden until unlocked",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievement:unlocked:
     *
     * Whether this achievement is unlocked.
     *
     * Since: 1.0
     */
    properties[PROP_UNLOCKED] =
        g_param_spec_boolean ("unlocked",
                              "Unlocked",
                              "Whether unlocked",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgAchievement::unlocked:
     * @self: the achievement
     *
     * Emitted when the achievement is unlocked.
     *
     * Since: 1.0
     */
    signals[SIGNAL_UNLOCKED] =
        g_signal_new ("unlocked",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgAchievement::progress-changed:
     * @self: the achievement
     * @current: current progress value
     * @target: target progress value
     *
     * Emitted when progress changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PROGRESS_CHANGED] =
        g_signal_new ("progress-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_INT64, G_TYPE_INT64);
}

static void
lrg_achievement_init (LrgAchievement *self)
{
    LrgAchievementPrivate *priv = lrg_achievement_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->description = NULL;
    priv->icon = NULL;
    priv->locked_icon = NULL;
    priv->points = 0;
    priv->hidden = FALSE;
    priv->unlocked = FALSE;
    priv->unlock_time = NULL;
    priv->progress = NULL;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAchievement *
lrg_achievement_new (const gchar *id,
                     const gchar *name,
                     const gchar *description)
{
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    return g_object_new (LRG_TYPE_ACHIEVEMENT,
                         "id", id,
                         "name", name,
                         "description", description,
                         NULL);
}

LrgAchievement *
lrg_achievement_new_with_progress (const gchar *id,
                                   const gchar *name,
                                   const gchar *description,
                                   gint64       target)
{
    LrgAchievement *self;
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (target > 0, NULL);

    self = lrg_achievement_new (id, name, description);
    priv = lrg_achievement_get_instance_private (self);

    priv->progress = lrg_achievement_progress_new (0, target);

    return self;
}

const gchar *
lrg_achievement_get_id (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), NULL);

    priv = lrg_achievement_get_instance_private (self);

    return priv->id;
}

const gchar *
lrg_achievement_get_name (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), NULL);

    priv = lrg_achievement_get_instance_private (self);

    return priv->name;
}

const gchar *
lrg_achievement_get_description (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), NULL);

    priv = lrg_achievement_get_instance_private (self);

    return priv->description;
}

const gchar *
lrg_achievement_get_icon (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), NULL);

    priv = lrg_achievement_get_instance_private (self);

    return priv->icon;
}

void
lrg_achievement_set_icon (LrgAchievement *self,
                          const gchar    *icon)
{
    LrgAchievementPrivate *priv;

    g_return_if_fail (LRG_IS_ACHIEVEMENT (self));

    priv = lrg_achievement_get_instance_private (self);

    if (g_strcmp0 (priv->icon, icon) != 0)
    {
        g_clear_pointer (&priv->icon, g_free);
        priv->icon = g_strdup (icon);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ICON]);
    }
}

const gchar *
lrg_achievement_get_locked_icon (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), NULL);

    priv = lrg_achievement_get_instance_private (self);

    return priv->locked_icon;
}

void
lrg_achievement_set_locked_icon (LrgAchievement *self,
                                 const gchar    *icon)
{
    LrgAchievementPrivate *priv;

    g_return_if_fail (LRG_IS_ACHIEVEMENT (self));

    priv = lrg_achievement_get_instance_private (self);

    if (g_strcmp0 (priv->locked_icon, icon) != 0)
    {
        g_clear_pointer (&priv->locked_icon, g_free);
        priv->locked_icon = g_strdup (icon);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOCKED_ICON]);
    }
}

gboolean
lrg_achievement_is_hidden (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), FALSE);

    priv = lrg_achievement_get_instance_private (self);

    return priv->hidden;
}

void
lrg_achievement_set_hidden (LrgAchievement *self,
                            gboolean        hidden)
{
    LrgAchievementPrivate *priv;

    g_return_if_fail (LRG_IS_ACHIEVEMENT (self));

    priv = lrg_achievement_get_instance_private (self);

    if (priv->hidden != hidden)
    {
        priv->hidden = hidden;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HIDDEN]);
    }
}

guint
lrg_achievement_get_points (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), 0);

    priv = lrg_achievement_get_instance_private (self);

    return priv->points;
}

void
lrg_achievement_set_points (LrgAchievement *self,
                            guint           points)
{
    LrgAchievementPrivate *priv;

    g_return_if_fail (LRG_IS_ACHIEVEMENT (self));

    priv = lrg_achievement_get_instance_private (self);

    if (priv->points != points)
    {
        priv->points = points;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINTS]);
    }
}

gboolean
lrg_achievement_is_unlocked (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), FALSE);

    priv = lrg_achievement_get_instance_private (self);

    return priv->unlocked;
}

GDateTime *
lrg_achievement_get_unlock_time (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), NULL);

    priv = lrg_achievement_get_instance_private (self);

    return priv->unlock_time;
}

gboolean
lrg_achievement_unlock (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;
    LrgAchievementClass *klass;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), FALSE);

    priv = lrg_achievement_get_instance_private (self);

    if (priv->unlocked)
        return FALSE;

    priv->unlocked = TRUE;
    priv->unlock_time = g_date_time_new_now_utc ();

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNLOCKED]);

    /* Call the virtual on_unlocked method */
    klass = LRG_ACHIEVEMENT_GET_CLASS (self);
    if (klass->on_unlocked != NULL)
        klass->on_unlocked (self);

    g_signal_emit (self, signals[SIGNAL_UNLOCKED], 0);

    return TRUE;
}

void
lrg_achievement_lock (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_if_fail (LRG_IS_ACHIEVEMENT (self));

    priv = lrg_achievement_get_instance_private (self);

    if (priv->unlocked)
    {
        priv->unlocked = FALSE;
        g_clear_pointer (&priv->unlock_time, g_date_time_unref);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNLOCKED]);
    }
}

LrgAchievementProgress *
lrg_achievement_get_progress (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), NULL);

    priv = lrg_achievement_get_instance_private (self);

    return priv->progress;
}

gboolean
lrg_achievement_has_progress (LrgAchievement *self)
{
    LrgAchievementPrivate *priv;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), FALSE);

    priv = lrg_achievement_get_instance_private (self);

    return priv->progress != NULL;
}

void
lrg_achievement_set_progress_value (LrgAchievement *self,
                                    gint64          value)
{
    LrgAchievementPrivate *priv;

    g_return_if_fail (LRG_IS_ACHIEVEMENT (self));

    priv = lrg_achievement_get_instance_private (self);

    if (priv->progress == NULL)
        return;

    if (priv->unlocked)
        return;

    lrg_achievement_progress_set_current (priv->progress, value);

    g_signal_emit (self, signals[SIGNAL_PROGRESS_CHANGED], 0,
                   lrg_achievement_progress_get_current (priv->progress),
                   lrg_achievement_progress_get_target (priv->progress));

    /* Check for unlock */
    if (lrg_achievement_check_unlock (self))
        lrg_achievement_unlock (self);
}

void
lrg_achievement_increment_progress (LrgAchievement *self,
                                    gint64          amount)
{
    LrgAchievementPrivate *priv;

    g_return_if_fail (LRG_IS_ACHIEVEMENT (self));

    priv = lrg_achievement_get_instance_private (self);

    if (priv->progress == NULL)
        return;

    if (priv->unlocked)
        return;

    lrg_achievement_progress_increment (priv->progress, amount);

    g_signal_emit (self, signals[SIGNAL_PROGRESS_CHANGED], 0,
                   lrg_achievement_progress_get_current (priv->progress),
                   lrg_achievement_progress_get_target (priv->progress));

    /* Check for unlock */
    if (lrg_achievement_check_unlock (self))
        lrg_achievement_unlock (self);
}

gboolean
lrg_achievement_check_unlock (LrgAchievement *self)
{
    LrgAchievementClass *klass;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (self), FALSE);

    klass = LRG_ACHIEVEMENT_GET_CLASS (self);

    if (klass->check_unlock != NULL)
        return klass->check_unlock (self);

    return FALSE;
}
