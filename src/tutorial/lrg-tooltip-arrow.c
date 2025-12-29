/* lrg-tooltip-arrow.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tooltip arrow widget for tutorial system.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TUTORIAL

#include "lrg-tooltip-arrow.h"
#include "../lrg-log.h"

#include <math.h>

struct _LrgTooltipArrow
{
    LrgWidget parent_instance;

    LrgArrowDirection direction;
    LrgWidget        *target;
    GrlColor          color;

    gfloat size;
    gfloat offset;

    /* Animation */
    gboolean animated;
    gfloat   bounce_amount;
    gfloat   bounce_speed;
    gfloat   animation_time;

    /* Manual target position */
    gfloat target_x;
    gfloat target_y;
    gboolean use_manual_position;
};

G_DEFINE_TYPE (LrgTooltipArrow, lrg_tooltip_arrow, LRG_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_DIRECTION,
    PROP_TARGET,
    PROP_COLOR,
    PROP_SIZE,
    PROP_OFFSET,
    PROP_ANIMATED,
    PROP_BOUNCE_AMOUNT,
    PROP_BOUNCE_SPEED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Default yellow color */
static const GrlColor DEFAULT_COLOR = { 255, 220, 50, 255 };

static void
get_target_center (LrgTooltipArrow *self,
                   gfloat          *out_x,
                   gfloat          *out_y)
{
    if (self->use_manual_position)
    {
        *out_x = self->target_x;
        *out_y = self->target_y;
    }
    else if (self->target != NULL)
    {
        gfloat w = lrg_widget_get_width (self->target);
        gfloat h = lrg_widget_get_height (self->target);

        *out_x = lrg_widget_get_world_x (self->target) + (w / 2.0f);
        *out_y = lrg_widget_get_world_y (self->target) + (h / 2.0f);
    }
    else
    {
        /* Default to widget's own position */
        *out_x = lrg_widget_get_world_x (LRG_WIDGET (self));
        *out_y = lrg_widget_get_world_y (LRG_WIDGET (self));
    }
}

static gfloat
get_bounce_offset (LrgTooltipArrow *self)
{
    if (!self->animated)
        return 0.0f;

    return sinf (self->animation_time * self->bounce_speed * 2.0f * G_PI) * self->bounce_amount;
}

static void
draw_arrow_up (LrgTooltipArrow *self,
               gfloat           center_x,
               gfloat           center_y,
               gfloat           bounce)
{
    g_autoptr(GrlVector2) v1 = NULL;
    g_autoptr(GrlVector2) v2 = NULL;
    g_autoptr(GrlVector2) v3 = NULL;
    gfloat arrow_y;

    /* Position above the target, pointing down */
    arrow_y = center_y - self->offset - bounce;

    v1 = grl_vector2_new (center_x, arrow_y + self->size);  /* Bottom point (tip) */
    v2 = grl_vector2_new (center_x - (self->size / 2.0f), arrow_y);  /* Top left */
    v3 = grl_vector2_new (center_x + (self->size / 2.0f), arrow_y);  /* Top right */

    grl_draw_triangle (v1, v2, v3, &self->color);
}

static void
draw_arrow_down (LrgTooltipArrow *self,
                 gfloat           center_x,
                 gfloat           center_y,
                 gfloat           bounce)
{
    g_autoptr(GrlVector2) v1 = NULL;
    g_autoptr(GrlVector2) v2 = NULL;
    g_autoptr(GrlVector2) v3 = NULL;
    gfloat arrow_y;

    /* Position below the target, pointing up */
    arrow_y = center_y + self->offset + bounce;

    v1 = grl_vector2_new (center_x, arrow_y);  /* Top point (tip) */
    v2 = grl_vector2_new (center_x - (self->size / 2.0f), arrow_y + self->size);  /* Bottom left */
    v3 = grl_vector2_new (center_x + (self->size / 2.0f), arrow_y + self->size);  /* Bottom right */

    grl_draw_triangle (v1, v2, v3, &self->color);
}

static void
draw_arrow_left (LrgTooltipArrow *self,
                 gfloat           center_x,
                 gfloat           center_y,
                 gfloat           bounce)
{
    g_autoptr(GrlVector2) v1 = NULL;
    g_autoptr(GrlVector2) v2 = NULL;
    g_autoptr(GrlVector2) v3 = NULL;
    gfloat arrow_x;

    /* Position to the left of the target, pointing right */
    arrow_x = center_x - self->offset - bounce;

    v1 = grl_vector2_new (arrow_x + self->size, center_y);  /* Right point (tip) */
    v2 = grl_vector2_new (arrow_x, center_y - (self->size / 2.0f));  /* Top left */
    v3 = grl_vector2_new (arrow_x, center_y + (self->size / 2.0f));  /* Bottom left */

    grl_draw_triangle (v1, v2, v3, &self->color);
}

static void
draw_arrow_right (LrgTooltipArrow *self,
                  gfloat           center_x,
                  gfloat           center_y,
                  gfloat           bounce)
{
    g_autoptr(GrlVector2) v1 = NULL;
    g_autoptr(GrlVector2) v2 = NULL;
    g_autoptr(GrlVector2) v3 = NULL;
    gfloat arrow_x;

    /* Position to the right of the target, pointing left */
    arrow_x = center_x + self->offset + bounce;

    v1 = grl_vector2_new (arrow_x, center_y);  /* Left point (tip) */
    v2 = grl_vector2_new (arrow_x + self->size, center_y - (self->size / 2.0f));  /* Top right */
    v3 = grl_vector2_new (arrow_x + self->size, center_y + (self->size / 2.0f));  /* Bottom right */

    grl_draw_triangle (v1, v2, v3, &self->color);
}

static LrgArrowDirection
determine_auto_direction (LrgTooltipArrow *self,
                          gfloat           target_x,
                          gfloat           target_y)
{
    /*
     * Simple heuristic: prefer direction with most screen space.
     * Default to DOWN if target is in upper half of screen.
     * This could be enhanced with actual screen dimension checks.
     */
    if (target_y < 300.0f)
        return LRG_ARROW_DIRECTION_DOWN;
    if (target_y > 600.0f)
        return LRG_ARROW_DIRECTION_UP;
    if (target_x < 400.0f)
        return LRG_ARROW_DIRECTION_RIGHT;
    if (target_x > 1200.0f)
        return LRG_ARROW_DIRECTION_LEFT;

    return LRG_ARROW_DIRECTION_DOWN;
}

static void
lrg_tooltip_arrow_draw (LrgWidget *widget)
{
    LrgTooltipArrow  *self = LRG_TOOLTIP_ARROW (widget);
    gfloat            target_x, target_y;
    gfloat            bounce;
    LrgArrowDirection actual_direction;

    get_target_center (self, &target_x, &target_y);
    bounce = get_bounce_offset (self);

    /* Determine actual direction */
    if (self->direction == LRG_ARROW_DIRECTION_AUTO)
    {
        actual_direction = determine_auto_direction (self, target_x, target_y);
    }
    else
    {
        actual_direction = self->direction;
    }

    switch (actual_direction)
    {
    case LRG_ARROW_DIRECTION_UP:
        draw_arrow_up (self, target_x, target_y, bounce);
        break;
    case LRG_ARROW_DIRECTION_DOWN:
        draw_arrow_down (self, target_x, target_y, bounce);
        break;
    case LRG_ARROW_DIRECTION_LEFT:
        draw_arrow_left (self, target_x, target_y, bounce);
        break;
    case LRG_ARROW_DIRECTION_RIGHT:
        draw_arrow_right (self, target_x, target_y, bounce);
        break;
    case LRG_ARROW_DIRECTION_AUTO:
    default:
        draw_arrow_down (self, target_x, target_y, bounce);
        break;
    }
}

static void
lrg_tooltip_arrow_measure (LrgWidget *widget,
                           gfloat    *preferred_width,
                           gfloat    *preferred_height)
{
    LrgTooltipArrow *self = LRG_TOOLTIP_ARROW (widget);

    if (preferred_width != NULL)
        *preferred_width = self->size;
    if (preferred_height != NULL)
        *preferred_height = self->size;
}

static void
lrg_tooltip_arrow_finalize (GObject *object)
{
    LrgTooltipArrow *self = LRG_TOOLTIP_ARROW (object);

    g_clear_object (&self->target);

    G_OBJECT_CLASS (lrg_tooltip_arrow_parent_class)->finalize (object);
}

static void
lrg_tooltip_arrow_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgTooltipArrow *self = LRG_TOOLTIP_ARROW (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        g_value_set_enum (value, self->direction);
        break;
    case PROP_TARGET:
        g_value_set_object (value, self->target);
        break;
    case PROP_COLOR:
        g_value_set_boxed (value, &self->color);
        break;
    case PROP_SIZE:
        g_value_set_float (value, self->size);
        break;
    case PROP_OFFSET:
        g_value_set_float (value, self->offset);
        break;
    case PROP_ANIMATED:
        g_value_set_boolean (value, self->animated);
        break;
    case PROP_BOUNCE_AMOUNT:
        g_value_set_float (value, self->bounce_amount);
        break;
    case PROP_BOUNCE_SPEED:
        g_value_set_float (value, self->bounce_speed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tooltip_arrow_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgTooltipArrow *self = LRG_TOOLTIP_ARROW (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        lrg_tooltip_arrow_set_direction (self, g_value_get_enum (value));
        break;
    case PROP_TARGET:
        lrg_tooltip_arrow_set_target (self, g_value_get_object (value));
        break;
    case PROP_COLOR:
        lrg_tooltip_arrow_set_color (self, g_value_get_boxed (value));
        break;
    case PROP_SIZE:
        lrg_tooltip_arrow_set_size (self, g_value_get_float (value));
        break;
    case PROP_OFFSET:
        lrg_tooltip_arrow_set_offset (self, g_value_get_float (value));
        break;
    case PROP_ANIMATED:
        lrg_tooltip_arrow_set_animated (self, g_value_get_boolean (value));
        break;
    case PROP_BOUNCE_AMOUNT:
        lrg_tooltip_arrow_set_bounce_amount (self, g_value_get_float (value));
        break;
    case PROP_BOUNCE_SPEED:
        lrg_tooltip_arrow_set_bounce_speed (self, g_value_get_float (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tooltip_arrow_class_init (LrgTooltipArrowClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

    object_class->finalize = lrg_tooltip_arrow_finalize;
    object_class->get_property = lrg_tooltip_arrow_get_property;
    object_class->set_property = lrg_tooltip_arrow_set_property;

    widget_class->draw = lrg_tooltip_arrow_draw;
    widget_class->measure = lrg_tooltip_arrow_measure;

    /**
     * LrgTooltipArrow:direction:
     *
     * The arrow direction.
     *
     * Since: 1.0
     */
    properties[PROP_DIRECTION] =
        g_param_spec_enum ("direction", NULL, NULL,
                           LRG_TYPE_ARROW_DIRECTION,
                           LRG_ARROW_DIRECTION_DOWN,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTooltipArrow:target:
     *
     * The target widget to point at.
     *
     * Since: 1.0
     */
    properties[PROP_TARGET] =
        g_param_spec_object ("target", NULL, NULL,
                             LRG_TYPE_WIDGET,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTooltipArrow:color:
     *
     * The arrow color.
     *
     * Since: 1.0
     */
    properties[PROP_COLOR] =
        g_param_spec_boxed ("color", NULL, NULL,
                            GRL_TYPE_COLOR,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTooltipArrow:size:
     *
     * The arrow size in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_SIZE] =
        g_param_spec_float ("size", NULL, NULL,
                            8.0f, 128.0f, 24.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTooltipArrow:offset:
     *
     * The offset distance from the target.
     *
     * Since: 1.0
     */
    properties[PROP_OFFSET] =
        g_param_spec_float ("offset", NULL, NULL,
                            0.0f, 200.0f, 20.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTooltipArrow:animated:
     *
     * Whether the arrow is animated.
     *
     * Since: 1.0
     */
    properties[PROP_ANIMATED] =
        g_param_spec_boolean ("animated", NULL, NULL,
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTooltipArrow:bounce-amount:
     *
     * The bounce animation amount in pixels.
     *
     * Since: 1.0
     */
    properties[PROP_BOUNCE_AMOUNT] =
        g_param_spec_float ("bounce-amount", NULL, NULL,
                            0.0f, 50.0f, 8.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgTooltipArrow:bounce-speed:
     *
     * The bounce animation speed (cycles per second).
     *
     * Since: 1.0
     */
    properties[PROP_BOUNCE_SPEED] =
        g_param_spec_float ("bounce-speed", NULL, NULL,
                            0.1f, 10.0f, 2.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_tooltip_arrow_init (LrgTooltipArrow *self)
{
    self->direction = LRG_ARROW_DIRECTION_DOWN;
    self->target = NULL;
    self->color = DEFAULT_COLOR;
    self->size = 24.0f;
    self->offset = 20.0f;
    self->animated = TRUE;
    self->bounce_amount = 8.0f;
    self->bounce_speed = 2.0f;
    self->animation_time = 0.0f;
    self->use_manual_position = FALSE;
}

/**
 * lrg_tooltip_arrow_new:
 *
 * Creates a new tooltip arrow widget.
 *
 * Returns: (transfer full): A new #LrgTooltipArrow
 *
 * Since: 1.0
 */
LrgTooltipArrow *
lrg_tooltip_arrow_new (void)
{
    return g_object_new (LRG_TYPE_TOOLTIP_ARROW, NULL);
}

/**
 * lrg_tooltip_arrow_new_with_direction:
 * @direction: The arrow direction
 *
 * Creates a new tooltip arrow with the specified direction.
 *
 * Returns: (transfer full): A new #LrgTooltipArrow
 *
 * Since: 1.0
 */
LrgTooltipArrow *
lrg_tooltip_arrow_new_with_direction (LrgArrowDirection direction)
{
    return g_object_new (LRG_TYPE_TOOLTIP_ARROW,
                         "direction", direction,
                         NULL);
}

/**
 * lrg_tooltip_arrow_get_direction:
 * @self: An #LrgTooltipArrow
 *
 * Gets the arrow direction.
 *
 * Returns: The arrow direction
 *
 * Since: 1.0
 */
LrgArrowDirection
lrg_tooltip_arrow_get_direction (LrgTooltipArrow *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP_ARROW (self), LRG_ARROW_DIRECTION_DOWN);
    return self->direction;
}

/**
 * lrg_tooltip_arrow_set_direction:
 * @self: An #LrgTooltipArrow
 * @direction: The arrow direction
 *
 * Sets the arrow direction.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_set_direction (LrgTooltipArrow  *self,
                                 LrgArrowDirection direction)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));

    if (self->direction != direction)
    {
        self->direction = direction;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]);
    }
}

/**
 * lrg_tooltip_arrow_get_target:
 * @self: An #LrgTooltipArrow
 *
 * Gets the target widget.
 *
 * Returns: (transfer none) (nullable): The target widget
 *
 * Since: 1.0
 */
LrgWidget *
lrg_tooltip_arrow_get_target (LrgTooltipArrow *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP_ARROW (self), NULL);
    return self->target;
}

/**
 * lrg_tooltip_arrow_set_target:
 * @self: An #LrgTooltipArrow
 * @target: (nullable): The widget to point at
 *
 * Sets the target widget.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_set_target (LrgTooltipArrow *self,
                              LrgWidget       *target)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));

    if (g_set_object (&self->target, target))
    {
        self->use_manual_position = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET]);
    }
}

/**
 * lrg_tooltip_arrow_set_target_position:
 * @self: An #LrgTooltipArrow
 * @x: Target X position
 * @y: Target Y position
 *
 * Sets a manual target position.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_set_target_position (LrgTooltipArrow *self,
                                       gfloat           x,
                                       gfloat           y)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));

    self->target_x = x;
    self->target_y = y;
    self->use_manual_position = TRUE;

    g_clear_object (&self->target);
}

/**
 * lrg_tooltip_arrow_get_color:
 * @self: An #LrgTooltipArrow
 *
 * Gets the arrow color.
 *
 * Returns: (transfer none): The color
 *
 * Since: 1.0
 */
const GrlColor *
lrg_tooltip_arrow_get_color (LrgTooltipArrow *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP_ARROW (self), &DEFAULT_COLOR);
    return &self->color;
}

/**
 * lrg_tooltip_arrow_set_color:
 * @self: An #LrgTooltipArrow
 * @color: The arrow color
 *
 * Sets the arrow color.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_set_color (LrgTooltipArrow *self,
                             const GrlColor  *color)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));
    g_return_if_fail (color != NULL);

    if (self->color.r != color->r ||
        self->color.g != color->g ||
        self->color.b != color->b ||
        self->color.a != color->a)
    {
        self->color = *color;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR]);
    }
}

/**
 * lrg_tooltip_arrow_get_size:
 * @self: An #LrgTooltipArrow
 *
 * Gets the arrow size.
 *
 * Returns: The size in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_tooltip_arrow_get_size (LrgTooltipArrow *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP_ARROW (self), 24.0f);
    return self->size;
}

/**
 * lrg_tooltip_arrow_set_size:
 * @self: An #LrgTooltipArrow
 * @size: The size in pixels
 *
 * Sets the arrow size.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_set_size (LrgTooltipArrow *self,
                            gfloat           size)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));

    if (self->size != size)
    {
        self->size = size;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SIZE]);
    }
}

/**
 * lrg_tooltip_arrow_get_offset:
 * @self: An #LrgTooltipArrow
 *
 * Gets the offset distance from the target.
 *
 * Returns: The offset in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_tooltip_arrow_get_offset (LrgTooltipArrow *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP_ARROW (self), 20.0f);
    return self->offset;
}

/**
 * lrg_tooltip_arrow_set_offset:
 * @self: An #LrgTooltipArrow
 * @offset: The offset in pixels
 *
 * Sets the offset distance from the target.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_set_offset (LrgTooltipArrow *self,
                              gfloat           offset)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));

    if (self->offset != offset)
    {
        self->offset = offset;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OFFSET]);
    }
}

/**
 * lrg_tooltip_arrow_get_animated:
 * @self: An #LrgTooltipArrow
 *
 * Gets whether the arrow is animated.
 *
 * Returns: %TRUE if animated
 *
 * Since: 1.0
 */
gboolean
lrg_tooltip_arrow_get_animated (LrgTooltipArrow *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP_ARROW (self), TRUE);
    return self->animated;
}

/**
 * lrg_tooltip_arrow_set_animated:
 * @self: An #LrgTooltipArrow
 * @animated: Whether to animate
 *
 * Sets whether the arrow should animate.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_set_animated (LrgTooltipArrow *self,
                                gboolean         animated)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));

    if (self->animated != animated)
    {
        self->animated = animated;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATED]);
    }
}

/**
 * lrg_tooltip_arrow_get_bounce_amount:
 * @self: An #LrgTooltipArrow
 *
 * Gets the bounce animation amount.
 *
 * Returns: The bounce amount in pixels
 *
 * Since: 1.0
 */
gfloat
lrg_tooltip_arrow_get_bounce_amount (LrgTooltipArrow *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP_ARROW (self), 8.0f);
    return self->bounce_amount;
}

/**
 * lrg_tooltip_arrow_set_bounce_amount:
 * @self: An #LrgTooltipArrow
 * @amount: The bounce amount in pixels
 *
 * Sets the bounce animation amount.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_set_bounce_amount (LrgTooltipArrow *self,
                                     gfloat           amount)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));

    if (self->bounce_amount != amount)
    {
        self->bounce_amount = amount;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNCE_AMOUNT]);
    }
}

/**
 * lrg_tooltip_arrow_get_bounce_speed:
 * @self: An #LrgTooltipArrow
 *
 * Gets the bounce animation speed.
 *
 * Returns: The bounce speed
 *
 * Since: 1.0
 */
gfloat
lrg_tooltip_arrow_get_bounce_speed (LrgTooltipArrow *self)
{
    g_return_val_if_fail (LRG_IS_TOOLTIP_ARROW (self), 2.0f);
    return self->bounce_speed;
}

/**
 * lrg_tooltip_arrow_set_bounce_speed:
 * @self: An #LrgTooltipArrow
 * @speed: The bounce speed
 *
 * Sets the bounce animation speed.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_set_bounce_speed (LrgTooltipArrow *self,
                                    gfloat           speed)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));

    if (self->bounce_speed != speed)
    {
        self->bounce_speed = speed;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BOUNCE_SPEED]);
    }
}

/**
 * lrg_tooltip_arrow_update:
 * @self: An #LrgTooltipArrow
 * @delta_time: Time since last update in seconds
 *
 * Updates the arrow animation state.
 *
 * Since: 1.0
 */
void
lrg_tooltip_arrow_update (LrgTooltipArrow *self,
                          gfloat           delta_time)
{
    g_return_if_fail (LRG_IS_TOOLTIP_ARROW (self));

    if (self->animated)
    {
        self->animation_time += delta_time;

        /* Wrap around to avoid float overflow */
        if (self->animation_time > 1000.0f)
        {
            self->animation_time = 0.0f;
        }
    }
}
