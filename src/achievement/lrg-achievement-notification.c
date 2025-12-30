/* lrg-achievement-notification.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementNotification - UI popup for achievement unlocks.
 */

#include "config.h"

#include "lrg-achievement-notification.h"
#include "../ui/lrg-label.h"
#include "../ui/lrg-image.h"
#include "../ui/lrg-hbox.h"
#include "../ui/lrg-vbox.h"
#include "../lrg-log.h"

typedef enum
{
    NOTIFICATION_STATE_HIDDEN,
    NOTIFICATION_STATE_FADE_IN,
    NOTIFICATION_STATE_VISIBLE,
    NOTIFICATION_STATE_FADE_OUT
} NotificationState;

struct _LrgAchievementNotification
{
    LrgContainer parent_instance;

    /* Child widgets */
    LrgImage *icon;
    LrgLabel *title_label;
    LrgLabel *name_label;
    LrgLabel *description_label;

    /* Configuration */
    gfloat duration;
    gfloat fade_duration;
    gint   margin;
    LrgNotificationPosition position;

    /* State */
    NotificationState state;
    gfloat timer;
    gfloat alpha;
    LrgAchievement *current_achievement;
};

enum
{
    PROP_0,
    PROP_DURATION,
    PROP_FADE_DURATION,
    PROP_POSITION,
    PROP_MARGIN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgAchievementNotification, lrg_achievement_notification, LRG_TYPE_CONTAINER)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_achievement_notification_finalize (GObject *object)
{
    LrgAchievementNotification *self = LRG_ACHIEVEMENT_NOTIFICATION (object);

    g_clear_object (&self->current_achievement);

    G_OBJECT_CLASS (lrg_achievement_notification_parent_class)->finalize (object);
}

static void
lrg_achievement_notification_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
    LrgAchievementNotification *self = LRG_ACHIEVEMENT_NOTIFICATION (object);

    switch (prop_id)
    {
    case PROP_DURATION:
        g_value_set_float (value, self->duration);
        break;
    case PROP_FADE_DURATION:
        g_value_set_float (value, self->fade_duration);
        break;
    case PROP_POSITION:
        g_value_set_int (value, self->position);
        break;
    case PROP_MARGIN:
        g_value_set_int (value, self->margin);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_achievement_notification_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
    LrgAchievementNotification *self = LRG_ACHIEVEMENT_NOTIFICATION (object);

    switch (prop_id)
    {
    case PROP_DURATION:
        self->duration = g_value_get_float (value);
        break;
    case PROP_FADE_DURATION:
        self->fade_duration = g_value_get_float (value);
        break;
    case PROP_POSITION:
        self->position = g_value_get_int (value);
        break;
    case PROP_MARGIN:
        self->margin = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_achievement_notification_class_init (LrgAchievementNotificationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_achievement_notification_finalize;
    object_class->get_property = lrg_achievement_notification_get_property;
    object_class->set_property = lrg_achievement_notification_set_property;

    /**
     * LrgAchievementNotification:duration:
     *
     * Display duration in seconds.
     *
     * Since: 1.0
     */
    properties[PROP_DURATION] =
        g_param_spec_float ("duration",
                            "Duration",
                            "Display duration in seconds",
                            0.0f, 60.0f, 5.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievementNotification:fade-duration:
     *
     * Fade in/out duration in seconds.
     *
     * Since: 1.0
     */
    properties[PROP_FADE_DURATION] =
        g_param_spec_float ("fade-duration",
                            "Fade Duration",
                            "Fade in/out duration in seconds",
                            0.0f, 5.0f, 0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievementNotification:position:
     *
     * Screen position for the notification.
     *
     * Since: 1.0
     */
    properties[PROP_POSITION] =
        g_param_spec_int ("position",
                          "Position",
                          "Screen position",
                          LRG_NOTIFICATION_POSITION_TOP_LEFT,
                          LRG_NOTIFICATION_POSITION_BOTTOM_RIGHT,
                          LRG_NOTIFICATION_POSITION_TOP_RIGHT,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    /**
     * LrgAchievementNotification:margin:
     *
     * Margin from screen edge in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_MARGIN] =
        g_param_spec_int ("margin",
                          "Margin",
                          "Margin from screen edge",
                          0, 200, 20,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_achievement_notification_init (LrgAchievementNotification *self)
{
    /* Default configuration */
    self->duration = 5.0f;
    self->fade_duration = 0.5f;
    self->margin = 20;
    self->position = LRG_NOTIFICATION_POSITION_TOP_RIGHT;

    /* Initial state */
    self->state = NOTIFICATION_STATE_HIDDEN;
    self->timer = 0.0f;
    self->alpha = 0.0f;
    self->current_achievement = NULL;

    /* Set panel properties */
    lrg_widget_set_visible (LRG_WIDGET (self), FALSE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAchievementNotification *
lrg_achievement_notification_new (void)
{
    return g_object_new (LRG_TYPE_ACHIEVEMENT_NOTIFICATION, NULL);
}

void
lrg_achievement_notification_show (LrgAchievementNotification *self,
                                   LrgAchievement             *achievement)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self));
    g_return_if_fail (LRG_IS_ACHIEVEMENT (achievement));

    /* Store reference to achievement */
    g_clear_object (&self->current_achievement);
    self->current_achievement = g_object_ref (achievement);

    /* Update display content */
    if (self->name_label != NULL)
        lrg_label_set_text (self->name_label, lrg_achievement_get_name (achievement));

    if (self->description_label != NULL)
        lrg_label_set_text (self->description_label, lrg_achievement_get_description (achievement));

    /* Start fade in */
    self->state = NOTIFICATION_STATE_FADE_IN;
    self->timer = 0.0f;
    self->alpha = 0.0f;

    lrg_widget_set_visible (LRG_WIDGET (self), TRUE);
}

void
lrg_achievement_notification_hide (LrgAchievementNotification *self)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self));

    self->state = NOTIFICATION_STATE_HIDDEN;
    self->alpha = 0.0f;
    lrg_widget_set_visible (LRG_WIDGET (self), FALSE);
}

gboolean
lrg_achievement_notification_is_visible (LrgAchievementNotification *self)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self), FALSE);

    return self->state != NOTIFICATION_STATE_HIDDEN;
}

gfloat
lrg_achievement_notification_get_duration (LrgAchievementNotification *self)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self), 0.0f);

    return self->duration;
}

void
lrg_achievement_notification_set_duration (LrgAchievementNotification *self,
                                           gfloat                      duration)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self));
    g_return_if_fail (duration >= 0.0f);

    if (self->duration != duration)
    {
        self->duration = duration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
    }
}

LrgNotificationPosition
lrg_achievement_notification_get_position (LrgAchievementNotification *self)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self),
                          LRG_NOTIFICATION_POSITION_TOP_RIGHT);

    return self->position;
}

void
lrg_achievement_notification_set_position (LrgAchievementNotification *self,
                                           LrgNotificationPosition     position)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self));

    if (self->position != position)
    {
        self->position = position;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POSITION]);
    }
}

gfloat
lrg_achievement_notification_get_fade_duration (LrgAchievementNotification *self)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self), 0.0f);

    return self->fade_duration;
}

void
lrg_achievement_notification_set_fade_duration (LrgAchievementNotification *self,
                                                gfloat                      duration)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self));
    g_return_if_fail (duration >= 0.0f);

    if (self->fade_duration != duration)
    {
        self->fade_duration = duration;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FADE_DURATION]);
    }
}

void
lrg_achievement_notification_set_margin (LrgAchievementNotification *self,
                                         gint                        margin)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self));
    g_return_if_fail (margin >= 0);

    if (self->margin != margin)
    {
        self->margin = margin;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARGIN]);
    }
}

void
lrg_achievement_notification_update (LrgAchievementNotification *self,
                                     gfloat                      delta)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_NOTIFICATION (self));

    if (self->state == NOTIFICATION_STATE_HIDDEN)
        return;

    self->timer += delta;

    switch (self->state)
    {
    case NOTIFICATION_STATE_FADE_IN:
        if (self->fade_duration > 0.0f)
            self->alpha = self->timer / self->fade_duration;
        else
            self->alpha = 1.0f;

        if (self->alpha >= 1.0f)
        {
            self->alpha = 1.0f;
            self->state = NOTIFICATION_STATE_VISIBLE;
            self->timer = 0.0f;
        }
        break;

    case NOTIFICATION_STATE_VISIBLE:
        if (self->timer >= self->duration)
        {
            self->state = NOTIFICATION_STATE_FADE_OUT;
            self->timer = 0.0f;
        }
        break;

    case NOTIFICATION_STATE_FADE_OUT:
        if (self->fade_duration > 0.0f)
            self->alpha = 1.0f - (self->timer / self->fade_duration);
        else
            self->alpha = 0.0f;

        if (self->alpha <= 0.0f)
        {
            self->alpha = 0.0f;
            self->state = NOTIFICATION_STATE_HIDDEN;
            lrg_widget_set_visible (LRG_WIDGET (self), FALSE);
        }
        break;

    case NOTIFICATION_STATE_HIDDEN:
        break;
    }
}
