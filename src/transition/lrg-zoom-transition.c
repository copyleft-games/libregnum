/* lrg-zoom-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Zoom in/out transition effect.
 */

#include "lrg-zoom-transition.h"
#include "../tween/lrg-easing.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRANSITION

/**
 * LrgZoomTransition:
 *
 * A transition that zooms in or out, creating a dramatic effect.
 *
 * The zoom transition can zoom in (toward a point) or out (away from a point):
 *
 * **ZOOM_IN**:
 * 1. **OUT phase**: Old scene zooms in toward center, fading to color
 * 2. **HOLD phase**: Solid color
 * 3. **IN phase**: New scene starts zoomed in, zooms out to normal
 *
 * **ZOOM_OUT**:
 * 1. **OUT phase**: Old scene zooms out from center, fading to color
 * 2. **HOLD phase**: Solid color
 * 3. **IN phase**: New scene starts zoomed out, zooms in to normal
 *
 * The zoom center can be customized for interesting effects (e.g., zoom
 * into a doorway the player is entering).
 *
 * ## Example usage
 *
 * ```c
 * LrgZoomTransition *zoom = lrg_zoom_transition_new_with_direction (
 *     LRG_ZOOM_DIRECTION_IN);
 * lrg_zoom_transition_set_scale (zoom, 3.0f);
 * lrg_zoom_transition_set_center (zoom, 0.5f, 0.5f); // Center of screen
 * lrg_transition_start (LRG_TRANSITION (zoom));
 * ```
 *
 * Since: 1.0
 */

struct _LrgZoomTransition
{
    LrgTransition parent_instance;

    LrgZoomDirection direction;
    gfloat scale;
    gfloat center_x;
    gfloat center_y;
};

enum
{
    PROP_0,
    PROP_DIRECTION,
    PROP_SCALE,
    PROP_CENTER_X,
    PROP_CENTER_Y,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgZoomTransition, lrg_zoom_transition, LRG_TYPE_TRANSITION)

/*
 * LrgTransition virtual method implementations
 */

static gboolean
lrg_zoom_transition_initialize (LrgTransition  *transition,
                                guint           width,
                                guint           height,
                                GError        **error)
{
    (void) transition;
    (void) width;
    (void) height;
    (void) error;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Zoom transition initialized (viewport: %ux%u)", width, height);

    return TRUE;
}

static void
lrg_zoom_transition_shutdown (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Zoom transition shutdown");
}

static void
lrg_zoom_transition_start (LrgTransition *transition)
{
    LrgZoomTransition *self;

    self = LRG_ZOOM_TRANSITION (transition);

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Zoom transition started (direction: %s, scale: %.2f, center: %.2f,%.2f)",
                   self->direction == LRG_ZOOM_DIRECTION_IN ? "in" : "out",
                   self->scale, self->center_x, self->center_y);
}

static void
lrg_zoom_transition_update (LrgTransition *transition,
                            gfloat         delta_time)
{
    (void) transition;
    (void) delta_time;
}

static void
lrg_zoom_transition_render (LrgTransition *transition,
                            guint          old_scene_texture,
                            guint          new_scene_texture,
                            guint          width,
                            guint          height)
{
    LrgZoomTransition *self;
    LrgTransitionState state;
    gfloat phase_progress;
    gfloat eased_progress;
    LrgEasingType easing;
    gfloat current_scale;
    gfloat alpha;

    self = LRG_ZOOM_TRANSITION (transition);
    state = lrg_transition_get_state (transition);
    phase_progress = lrg_transition_get_phase_progress (transition);
    easing = lrg_transition_get_easing (transition);
    eased_progress = lrg_easing_apply (easing, phase_progress);

    /*
     * Calculate current zoom scale and alpha for overlay.
     *
     * ZOOM_IN: scale goes 1.0 -> max during OUT, max -> 1.0 during IN
     * ZOOM_OUT: scale goes 1.0 -> 0.0 during OUT, 0.0 -> 1.0 during IN
     */
    switch (state)
    {
    case LRG_TRANSITION_STATE_OUT:
        if (self->direction == LRG_ZOOM_DIRECTION_IN)
        {
            current_scale = 1.0f + (self->scale - 1.0f) * eased_progress;
        }
        else
        {
            current_scale = 1.0f - eased_progress;
        }
        alpha = eased_progress;
        break;

    case LRG_TRANSITION_STATE_HOLD:
        if (self->direction == LRG_ZOOM_DIRECTION_IN)
        {
            current_scale = self->scale;
        }
        else
        {
            current_scale = 0.0f;
        }
        alpha = 1.0f;
        break;

    case LRG_TRANSITION_STATE_IN:
        if (self->direction == LRG_ZOOM_DIRECTION_IN)
        {
            current_scale = self->scale - (self->scale - 1.0f) * eased_progress;
        }
        else
        {
            current_scale = eased_progress;
        }
        alpha = 1.0f - eased_progress;
        break;

    case LRG_TRANSITION_STATE_IDLE:
    case LRG_TRANSITION_STATE_COMPLETE:
    default:
        current_scale = 1.0f;
        alpha = 0.0f;
        break;
    }

    (void) current_scale;
    (void) alpha;
    (void) old_scene_texture;
    (void) new_scene_texture;
    (void) width;
    (void) height;

    /*
     * TODO: Integrate with graylib rendering
     *
     * The zoom effect involves:
     * 1. Apply scale transform centered on (center_x * width, center_y * height)
     * 2. Draw appropriate scene texture with transform
     * 3. Draw color overlay with alpha
     *
     * Matrix operations:
     *   translate(center_x * width, center_y * height)
     *   scale(current_scale, current_scale)
     *   translate(-center_x * width, -center_y * height)
     *   draw_scene_texture()
     *   draw_color_overlay(alpha)
     */
}

static void
lrg_zoom_transition_reset (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Zoom transition reset");
}

/*
 * GObject virtual methods
 */

static void
lrg_zoom_transition_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgZoomTransition *self;

    self = LRG_ZOOM_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        g_value_set_enum (value, self->direction);
        break;

    case PROP_SCALE:
        g_value_set_float (value, self->scale);
        break;

    case PROP_CENTER_X:
        g_value_set_float (value, self->center_x);
        break;

    case PROP_CENTER_Y:
        g_value_set_float (value, self->center_y);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_zoom_transition_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgZoomTransition *self;

    self = LRG_ZOOM_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        self->direction = g_value_get_enum (value);
        break;

    case PROP_SCALE:
        self->scale = g_value_get_float (value);
        break;

    case PROP_CENTER_X:
        self->center_x = g_value_get_float (value);
        break;

    case PROP_CENTER_Y:
        self->center_y = g_value_get_float (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_zoom_transition_class_init (LrgZoomTransitionClass *klass)
{
    GObjectClass *object_class;
    LrgTransitionClass *transition_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->get_property = lrg_zoom_transition_get_property;
    object_class->set_property = lrg_zoom_transition_set_property;

    transition_class = LRG_TRANSITION_CLASS (klass);
    transition_class->initialize = lrg_zoom_transition_initialize;
    transition_class->shutdown = lrg_zoom_transition_shutdown;
    transition_class->start = lrg_zoom_transition_start;
    transition_class->update = lrg_zoom_transition_update;
    transition_class->render = lrg_zoom_transition_render;
    transition_class->reset = lrg_zoom_transition_reset;

    /**
     * LrgZoomTransition:direction:
     *
     * The zoom direction (in or out).
     *
     * Since: 1.0
     */
    properties[PROP_DIRECTION] =
        g_param_spec_enum ("direction",
                           "Direction",
                           "The zoom direction",
                           LRG_TYPE_ZOOM_DIRECTION,
                           LRG_ZOOM_DIRECTION_IN,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgZoomTransition:scale:
     *
     * Maximum zoom scale.
     *
     * Since: 1.0
     */
    properties[PROP_SCALE] =
        g_param_spec_float ("scale",
                            "Scale",
                            "Maximum zoom scale",
                            0.1f,
                            10.0f,
                            2.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgZoomTransition:center-x:
     *
     * Zoom center X coordinate (0.0-1.0, normalized).
     *
     * Since: 1.0
     */
    properties[PROP_CENTER_X] =
        g_param_spec_float ("center-x",
                            "Center X",
                            "Zoom center X (normalized)",
                            0.0f,
                            1.0f,
                            0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgZoomTransition:center-y:
     *
     * Zoom center Y coordinate (0.0-1.0, normalized).
     *
     * Since: 1.0
     */
    properties[PROP_CENTER_Y] =
        g_param_spec_float ("center-y",
                            "Center Y",
                            "Zoom center Y (normalized)",
                            0.0f,
                            1.0f,
                            0.5f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_zoom_transition_init (LrgZoomTransition *self)
{
    self->direction = LRG_ZOOM_DIRECTION_IN;
    self->scale = 2.0f;
    self->center_x = 0.5f;
    self->center_y = 0.5f;
}

/*
 * Public API
 */

/**
 * lrg_zoom_transition_new:
 *
 * Creates a new zoom transition with default settings.
 *
 * Returns: (transfer full): A new #LrgZoomTransition
 *
 * Since: 1.0
 */
LrgZoomTransition *
lrg_zoom_transition_new (void)
{
    return g_object_new (LRG_TYPE_ZOOM_TRANSITION, NULL);
}

/**
 * lrg_zoom_transition_new_with_direction:
 * @direction: The zoom direction
 *
 * Creates a new zoom transition with the specified direction.
 *
 * Returns: (transfer full): A new #LrgZoomTransition
 *
 * Since: 1.0
 */
LrgZoomTransition *
lrg_zoom_transition_new_with_direction (LrgZoomDirection direction)
{
    return g_object_new (LRG_TYPE_ZOOM_TRANSITION,
                         "direction", direction,
                         NULL);
}

/**
 * lrg_zoom_transition_get_direction:
 * @self: A #LrgZoomTransition
 *
 * Gets the zoom direction.
 *
 * Returns: The #LrgZoomDirection
 *
 * Since: 1.0
 */
LrgZoomDirection
lrg_zoom_transition_get_direction (LrgZoomTransition *self)
{
    g_return_val_if_fail (LRG_IS_ZOOM_TRANSITION (self), LRG_ZOOM_DIRECTION_IN);

    return self->direction;
}

/**
 * lrg_zoom_transition_set_direction:
 * @self: A #LrgZoomTransition
 * @direction: The zoom direction
 *
 * Sets the zoom direction.
 *
 * Since: 1.0
 */
void
lrg_zoom_transition_set_direction (LrgZoomTransition *self,
                                   LrgZoomDirection   direction)
{
    g_return_if_fail (LRG_IS_ZOOM_TRANSITION (self));

    if (self->direction != direction)
    {
        self->direction = direction;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]);
    }
}

/**
 * lrg_zoom_transition_get_scale:
 * @self: A #LrgZoomTransition
 *
 * Gets the maximum zoom scale.
 *
 * Returns: The scale value
 *
 * Since: 1.0
 */
gfloat
lrg_zoom_transition_get_scale (LrgZoomTransition *self)
{
    g_return_val_if_fail (LRG_IS_ZOOM_TRANSITION (self), 2.0f);

    return self->scale;
}

/**
 * lrg_zoom_transition_set_scale:
 * @self: A #LrgZoomTransition
 * @scale: Maximum zoom scale
 *
 * Sets the maximum zoom scale.
 *
 * Since: 1.0
 */
void
lrg_zoom_transition_set_scale (LrgZoomTransition *self,
                               gfloat             scale)
{
    g_return_if_fail (LRG_IS_ZOOM_TRANSITION (self));

    scale = CLAMP (scale, 0.1f, 10.0f);

    if (self->scale != scale)
    {
        self->scale = scale;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALE]);
    }
}

/**
 * lrg_zoom_transition_get_center_x:
 * @self: A #LrgZoomTransition
 *
 * Gets the zoom center X coordinate.
 *
 * Returns: The center X coordinate
 *
 * Since: 1.0
 */
gfloat
lrg_zoom_transition_get_center_x (LrgZoomTransition *self)
{
    g_return_val_if_fail (LRG_IS_ZOOM_TRANSITION (self), 0.5f);

    return self->center_x;
}

/**
 * lrg_zoom_transition_get_center_y:
 * @self: A #LrgZoomTransition
 *
 * Gets the zoom center Y coordinate.
 *
 * Returns: The center Y coordinate
 *
 * Since: 1.0
 */
gfloat
lrg_zoom_transition_get_center_y (LrgZoomTransition *self)
{
    g_return_val_if_fail (LRG_IS_ZOOM_TRANSITION (self), 0.5f);

    return self->center_y;
}

/**
 * lrg_zoom_transition_set_center:
 * @self: A #LrgZoomTransition
 * @x: Center X (0.0-1.0)
 * @y: Center Y (0.0-1.0)
 *
 * Sets the zoom center point.
 *
 * Since: 1.0
 */
void
lrg_zoom_transition_set_center (LrgZoomTransition *self,
                                gfloat             x,
                                gfloat             y)
{
    g_return_if_fail (LRG_IS_ZOOM_TRANSITION (self));

    x = CLAMP (x, 0.0f, 1.0f);
    y = CLAMP (y, 0.0f, 1.0f);

    g_object_freeze_notify (G_OBJECT (self));

    if (self->center_x != x)
    {
        self->center_x = x;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CENTER_X]);
    }

    if (self->center_y != y)
    {
        self->center_y = y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CENTER_Y]);
    }

    g_object_thaw_notify (G_OBJECT (self));
}
