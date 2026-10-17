/* lrg-achievement-toasts.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementToasts - a queue of unlock toasts shown a few at a time:
 * each fades (and slides) in over the fade time, stays, and fades out
 * over the last fade time of its duration.
 */

#include "config.h"

#include "lrg-achievement-toasts.h"

struct _LrgAchievementToast
{
    gint     ref_count;
    gchar   *id;
    gchar   *name;
    gchar   *description;
    gchar   *icon;
    guint    points;
    gdouble  age;
};

G_DEFINE_BOXED_TYPE (LrgAchievementToast,
                     lrg_achievement_toast,
                     lrg_achievement_toast_ref,
                     lrg_achievement_toast_unref)

struct _LrgAchievementToasts
{
    GObject                parent_instance;

    gdouble                duration;
    gdouble                fade;
    guint                  max_visible;
    GPtrArray             *shown;     /* LrgAchievementToast, oldest first */
    GQueue                 waiting;   /* LrgAchievementToast */
    LrgAchievementManager *manager;   /* followed (owned), nullable */
    gulong                 unlocked_id;
};

enum
{
    PROP_0,
    PROP_DURATION,
    PROP_FADE,
    PROP_MAX_VISIBLE,
    N_PROPS
};

enum
{
    SIGNAL_SHOWN,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_TYPE (LrgAchievementToasts, lrg_achievement_toasts, G_TYPE_OBJECT)

/* --- the toast record --- */

LrgAchievementToast *
lrg_achievement_toast_ref (LrgAchievementToast *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    g_atomic_int_inc (&self->ref_count);
    return self;
}

void
lrg_achievement_toast_unref (LrgAchievementToast *self)
{
    if (self == NULL || !g_atomic_int_dec_and_test (&self->ref_count))
        return;
    g_free (self->id);
    g_free (self->name);
    g_free (self->description);
    g_free (self->icon);
    g_free (self);
}

const gchar *
lrg_achievement_toast_get_id (const LrgAchievementToast *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

const gchar *
lrg_achievement_toast_get_name (const LrgAchievementToast *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->name;
}

const gchar *
lrg_achievement_toast_get_description (const LrgAchievementToast *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->description;
}

const gchar *
lrg_achievement_toast_get_icon (const LrgAchievementToast *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->icon;
}

guint
lrg_achievement_toast_get_points (const LrgAchievementToast *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->points;
}

gdouble
lrg_achievement_toast_get_age (const LrgAchievementToast *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return self->age;
}

/* --- the queue --- */

/* moves waiting toasts into free places */
static void
promote (LrgAchievementToasts *self)
{
    while (self->shown->len < self->max_visible && !g_queue_is_empty (&self->waiting))
    {
        LrgAchievementToast *toast = g_queue_pop_head (&self->waiting);

        toast->age = 0.0;
        g_ptr_array_add (self->shown, toast);
        g_signal_emit (self, signals[SIGNAL_SHOWN], 0, toast);
    }
}

static void
on_unlocked (LrgAchievementManager *manager,
             LrgAchievement        *achievement,
             gpointer               user_data)
{
    (void)manager;
    lrg_achievement_toasts_push (LRG_ACHIEVEMENT_TOASTS (user_data), achievement);
}

static void
lrg_achievement_toasts_dispose (GObject *object)
{
    LrgAchievementToasts *self = LRG_ACHIEVEMENT_TOASTS (object);

    lrg_achievement_toasts_follow (self, NULL);
    G_OBJECT_CLASS (lrg_achievement_toasts_parent_class)->dispose (object);
}

static void
lrg_achievement_toasts_finalize (GObject *object)
{
    LrgAchievementToasts *self = LRG_ACHIEVEMENT_TOASTS (object);

    g_ptr_array_unref (self->shown);
    g_queue_clear_full (&self->waiting, (GDestroyNotify)lrg_achievement_toast_unref);
    G_OBJECT_CLASS (lrg_achievement_toasts_parent_class)->finalize (object);
}

static void
lrg_achievement_toasts_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgAchievementToasts *self = LRG_ACHIEVEMENT_TOASTS (object);

    switch (prop_id)
    {
    case PROP_DURATION:
        g_value_set_double (value, self->duration);
        break;
    case PROP_FADE:
        g_value_set_double (value, self->fade);
        break;
    case PROP_MAX_VISIBLE:
        g_value_set_uint (value, self->max_visible);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_achievement_toasts_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgAchievementToasts *self = LRG_ACHIEVEMENT_TOASTS (object);

    switch (prop_id)
    {
    case PROP_DURATION:
        self->duration = g_value_get_double (value);
        break;
    case PROP_FADE:
        self->fade = g_value_get_double (value);
        break;
    case PROP_MAX_VISIBLE:
        self->max_visible = g_value_get_uint (value);
        promote (self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_achievement_toasts_class_init (LrgAchievementToastsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_achievement_toasts_dispose;
    object_class->finalize = lrg_achievement_toasts_finalize;
    object_class->get_property = lrg_achievement_toasts_get_property;
    object_class->set_property = lrg_achievement_toasts_set_property;

    /**
     * LrgAchievementToasts:duration:
     *
     * Seconds a toast shows, its fades included.
     *
     * Since: 1.0
     */
    properties[PROP_DURATION] =
        g_param_spec_double ("duration", "Duration", "Seconds a toast shows",
                             0.1, 600.0, LRG_ACHIEVEMENT_TOASTS_DEFAULT_SECONDS,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievementToasts:fade:
     *
     * Seconds of the fade in and of the fade out.
     *
     * Since: 1.0
     */
    properties[PROP_FADE] =
        g_param_spec_double ("fade", "Fade", "Seconds of each fade",
                             0.0, 60.0, LRG_ACHIEVEMENT_TOASTS_FADE_SECONDS,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievementToasts:max-visible:
     *
     * Toasts shown at once.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_VISIBLE] =
        g_param_spec_uint ("max-visible", "Max visible", "Toasts shown at once",
                           1, 16, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_SHOWN] =
        g_signal_new ("shown",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_ACHIEVEMENT_TOAST);
}

static void
lrg_achievement_toasts_init (LrgAchievementToasts *self)
{
    self->duration = LRG_ACHIEVEMENT_TOASTS_DEFAULT_SECONDS;
    self->fade = LRG_ACHIEVEMENT_TOASTS_FADE_SECONDS;
    self->max_visible = 1;
    self->shown = g_ptr_array_new_with_free_func ((GDestroyNotify)lrg_achievement_toast_unref);
    g_queue_init (&self->waiting);
}

LrgAchievementToasts *
lrg_achievement_toasts_new (void)
{
    return g_object_new (LRG_TYPE_ACHIEVEMENT_TOASTS, NULL);
}

LrgAchievementToast *
lrg_achievement_toasts_push (LrgAchievementToasts *self,
                             LrgAchievement       *achievement)
{
    LrgAchievementToast *toast;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_TOASTS (self), NULL);
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT (achievement), NULL);

    toast = g_new0 (LrgAchievementToast, 1);
    toast->ref_count = 1;
    toast->id = g_strdup (lrg_achievement_get_id (achievement));
    toast->name = g_strdup (lrg_achievement_get_name (achievement) != NULL ?
                            lrg_achievement_get_name (achievement) : toast->id);
    toast->description = g_strdup (lrg_achievement_get_description (achievement));
    toast->icon = g_strdup (lrg_achievement_get_icon (achievement));
    toast->points = lrg_achievement_get_points (achievement);
    g_queue_push_tail (&self->waiting, toast);
    promote (self);
    return toast;
}

void
lrg_achievement_toasts_follow (LrgAchievementToasts  *self,
                               LrgAchievementManager *manager)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_TOASTS (self));
    g_return_if_fail (manager == NULL || LRG_IS_ACHIEVEMENT_MANAGER (manager));

    if (self->manager != NULL)
    {
        g_signal_handler_disconnect (self->manager, self->unlocked_id);
        self->unlocked_id = 0;
        g_clear_object (&self->manager);
    }
    if (manager == NULL)
        return;
    self->manager = g_object_ref (manager);
    self->unlocked_id = g_signal_connect (manager, "achievement-unlocked",
                                          G_CALLBACK (on_unlocked), self);
}

void
lrg_achievement_toasts_update (LrgAchievementToasts *self,
                               gdouble               delta)
{
    guint i;

    g_return_if_fail (LRG_IS_ACHIEVEMENT_TOASTS (self));

    if (delta < 0.0)
        delta = 0.0;
    for (i = 0; i < self->shown->len; )
    {
        LrgAchievementToast *toast = g_ptr_array_index (self->shown, i);

        toast->age += delta;
        if (toast->age >= self->duration)
            g_ptr_array_remove_index (self->shown, i);
        else
            i++;
    }
    promote (self);
}

guint
lrg_achievement_toasts_get_count (LrgAchievementToasts *self)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_TOASTS (self), 0);
    return self->shown->len;
}

guint
lrg_achievement_toasts_get_waiting (LrgAchievementToasts *self)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_TOASTS (self), 0);
    return g_queue_get_length (&self->waiting);
}

LrgAchievementToast *
lrg_achievement_toasts_get_toast (LrgAchievementToasts *self,
                                  guint                 index)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_TOASTS (self), NULL);

    if (index >= self->shown->len)
        return NULL;
    return g_ptr_array_index (self->shown, index);
}

gdouble
lrg_achievement_toasts_get_alpha (LrgAchievementToasts *self,
                                  guint                 index)
{
    LrgAchievementToast *toast;
    gdouble fade;
    gdouble left;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_TOASTS (self), 0.0);

    toast = lrg_achievement_toasts_get_toast (self, index);
    if (toast == NULL)
        return 0.0;
    /* fades never overlap: a short toast splits its time between them */
    fade = MIN (self->fade, self->duration / 2.0);
    if (fade <= 0.0)
        return 1.0;
    left = self->duration - toast->age;
    return CLAMP (MIN (toast->age / fade, left / fade), 0.0, 1.0);
}

gdouble
lrg_achievement_toasts_get_slide (LrgAchievementToasts *self,
                                  guint                 index)
{
    LrgAchievementToast *toast;
    gdouble fade;
    gdouble t;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_TOASTS (self), 0.0);

    toast = lrg_achievement_toasts_get_toast (self, index);
    if (toast == NULL)
        return 0.0;
    fade = MIN (self->fade, self->duration / 2.0);
    if (fade <= 0.0)
        return 1.0;
    t = CLAMP (toast->age / fade, 0.0, 1.0);
    /* ease out: fast at first, settling into place */
    return 1.0 - (1.0 - t) * (1.0 - t);
}

void
lrg_achievement_toasts_clear (LrgAchievementToasts *self)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_TOASTS (self));

    g_ptr_array_set_size (self->shown, 0);
    g_queue_clear_full (&self->waiting, (GDestroyNotify)lrg_achievement_toast_unref);
    g_queue_init (&self->waiting);
}
