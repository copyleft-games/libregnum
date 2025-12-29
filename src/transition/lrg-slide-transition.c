/* lrg-slide-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Slide transition with push/cover/reveal modes.
 */

#include "lrg-slide-transition.h"
#include "../tween/lrg-easing.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRANSITION

/**
 * LrgSlideTransition:
 *
 * A transition that slides scenes in a specified direction.
 *
 * The slide transition supports three modes:
 *
 * - **LRG_SLIDE_MODE_PUSH**: Old and new scenes move together (like a carousel)
 * - **LRG_SLIDE_MODE_COVER**: New scene slides over the stationary old scene
 * - **LRG_SLIDE_MODE_REVEAL**: Old scene slides away, revealing stationary new scene
 *
 * Unlike other transitions, slide transitions typically skip the HOLD phase
 * as the scenes are both visible during the slide.
 *
 * ## Example usage
 *
 * ```c
 * LrgSlideTransition *slide = lrg_slide_transition_new_with_options (
 *     LRG_TRANSITION_DIRECTION_LEFT,
 *     LRG_SLIDE_MODE_PUSH);
 * lrg_transition_set_easing (LRG_TRANSITION (slide), LRG_EASING_EASE_IN_OUT_CUBIC);
 * lrg_transition_start (LRG_TRANSITION (slide));
 * ```
 *
 * Since: 1.0
 */

struct _LrgSlideTransition
{
    LrgTransition parent_instance;

    LrgTransitionDirection direction;
    LrgSlideMode mode;
};

enum
{
    PROP_0,
    PROP_DIRECTION,
    PROP_MODE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgSlideTransition, lrg_slide_transition, LRG_TYPE_TRANSITION)

/*
 * LrgTransition virtual method implementations
 */

static gboolean
lrg_slide_transition_initialize (LrgTransition  *transition,
                                 guint           width,
                                 guint           height,
                                 GError        **error)
{
    (void) transition;
    (void) width;
    (void) height;
    (void) error;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Slide transition initialized (viewport: %ux%u)", width, height);

    return TRUE;
}

static void
lrg_slide_transition_shutdown (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Slide transition shutdown");
}

static void
lrg_slide_transition_start (LrgTransition *transition)
{
    LrgSlideTransition *self;
    const gchar *direction_name;
    const gchar *mode_name;

    self = LRG_SLIDE_TRANSITION (transition);

    switch (self->direction)
    {
    case LRG_TRANSITION_DIRECTION_LEFT:
        direction_name = "left";
        break;
    case LRG_TRANSITION_DIRECTION_RIGHT:
        direction_name = "right";
        break;
    case LRG_TRANSITION_DIRECTION_UP:
        direction_name = "up";
        break;
    case LRG_TRANSITION_DIRECTION_DOWN:
        direction_name = "down";
        break;
    default:
        direction_name = "unknown";
        break;
    }

    switch (self->mode)
    {
    case LRG_SLIDE_MODE_PUSH:
        mode_name = "push";
        break;
    case LRG_SLIDE_MODE_COVER:
        mode_name = "cover";
        break;
    case LRG_SLIDE_MODE_REVEAL:
        mode_name = "reveal";
        break;
    default:
        mode_name = "unknown";
        break;
    }

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Slide transition started (direction: %s, mode: %s)",
                   direction_name, mode_name);
}

static void
lrg_slide_transition_update (LrgTransition *transition,
                             gfloat         delta_time)
{
    (void) transition;
    (void) delta_time;
}

static void
lrg_slide_transition_render (LrgTransition *transition,
                             guint          old_scene_texture,
                             guint          new_scene_texture,
                             guint          width,
                             guint          height)
{
    LrgSlideTransition *self;
    LrgTransitionState state;
    gfloat overall_progress;
    gfloat eased_progress;
    LrgEasingType easing;
    gfloat slide_offset;
    gint old_x, old_y;
    gint new_x, new_y;

    self = LRG_SLIDE_TRANSITION (transition);
    state = lrg_transition_get_state (transition);
    overall_progress = lrg_transition_get_progress (transition);
    easing = lrg_transition_get_easing (transition);
    eased_progress = lrg_easing_apply (easing, overall_progress);

    /*
     * Calculate slide offset (0.0 to 1.0 means full slide)
     * For slide transitions, we use overall progress (not phase progress)
     * since we want a single continuous motion.
     */
    switch (state)
    {
    case LRG_TRANSITION_STATE_IDLE:
        slide_offset = 0.0f;
        break;

    case LRG_TRANSITION_STATE_COMPLETE:
        slide_offset = 1.0f;
        break;

    default:
        slide_offset = eased_progress;
        break;
    }

    /*
     * Calculate positions for old and new scenes based on direction and mode.
     * Start positions -> End positions:
     *
     * PUSH mode (both scenes move):
     *   Old: (0,0) -> offset in direction
     *   New: offset opposite direction -> (0,0)
     *
     * COVER mode (new scene slides over old):
     *   Old: (0,0) -> (0,0) (stationary)
     *   New: offset opposite direction -> (0,0)
     *
     * REVEAL mode (old scene slides away):
     *   Old: (0,0) -> offset in direction
     *   New: (0,0) -> (0,0) (stationary)
     */

    old_x = 0;
    old_y = 0;
    new_x = 0;
    new_y = 0;

    switch (self->direction)
    {
    case LRG_TRANSITION_DIRECTION_LEFT:
        /* Slide left means new scene comes from right */
        if (self->mode == LRG_SLIDE_MODE_PUSH || self->mode == LRG_SLIDE_MODE_REVEAL)
        {
            old_x = (gint) (-slide_offset * width);
        }
        if (self->mode == LRG_SLIDE_MODE_PUSH || self->mode == LRG_SLIDE_MODE_COVER)
        {
            new_x = (gint) ((1.0f - slide_offset) * width);
        }
        break;

    case LRG_TRANSITION_DIRECTION_RIGHT:
        /* Slide right means new scene comes from left */
        if (self->mode == LRG_SLIDE_MODE_PUSH || self->mode == LRG_SLIDE_MODE_REVEAL)
        {
            old_x = (gint) (slide_offset * width);
        }
        if (self->mode == LRG_SLIDE_MODE_PUSH || self->mode == LRG_SLIDE_MODE_COVER)
        {
            new_x = (gint) (-(1.0f - slide_offset) * width);
        }
        break;

    case LRG_TRANSITION_DIRECTION_UP:
        /* Slide up means new scene comes from bottom */
        if (self->mode == LRG_SLIDE_MODE_PUSH || self->mode == LRG_SLIDE_MODE_REVEAL)
        {
            old_y = (gint) (-slide_offset * height);
        }
        if (self->mode == LRG_SLIDE_MODE_PUSH || self->mode == LRG_SLIDE_MODE_COVER)
        {
            new_y = (gint) ((1.0f - slide_offset) * height);
        }
        break;

    case LRG_TRANSITION_DIRECTION_DOWN:
        /* Slide down means new scene comes from top */
        if (self->mode == LRG_SLIDE_MODE_PUSH || self->mode == LRG_SLIDE_MODE_REVEAL)
        {
            old_y = (gint) (slide_offset * height);
        }
        if (self->mode == LRG_SLIDE_MODE_PUSH || self->mode == LRG_SLIDE_MODE_COVER)
        {
            new_y = (gint) (-(1.0f - slide_offset) * height);
        }
        break;

    default:
        break;
    }

    (void) old_x;
    (void) old_y;
    (void) new_x;
    (void) new_y;
    (void) old_scene_texture;
    (void) new_scene_texture;

    /*
     * TODO: Integrate with graylib rendering
     *
     * For REVEAL mode, draw new scene first (behind):
     *   grl_draw_texture_ex (new_scene_texture, new_x, new_y, ...);
     *   grl_draw_texture_ex (old_scene_texture, old_x, old_y, ...);
     *
     * For COVER mode, draw old scene first (behind):
     *   grl_draw_texture_ex (old_scene_texture, old_x, old_y, ...);
     *   grl_draw_texture_ex (new_scene_texture, new_x, new_y, ...);
     *
     * For PUSH mode, order doesn't matter (no overlap):
     *   grl_draw_texture_ex (old_scene_texture, old_x, old_y, ...);
     *   grl_draw_texture_ex (new_scene_texture, new_x, new_y, ...);
     */
}

static void
lrg_slide_transition_reset (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Slide transition reset");
}

/*
 * GObject virtual methods
 */

static void
lrg_slide_transition_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgSlideTransition *self;

    self = LRG_SLIDE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        g_value_set_enum (value, self->direction);
        break;

    case PROP_MODE:
        g_value_set_enum (value, self->mode);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_slide_transition_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgSlideTransition *self;

    self = LRG_SLIDE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_DIRECTION:
        self->direction = g_value_get_enum (value);
        break;

    case PROP_MODE:
        self->mode = g_value_get_enum (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_slide_transition_class_init (LrgSlideTransitionClass *klass)
{
    GObjectClass *object_class;
    LrgTransitionClass *transition_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->get_property = lrg_slide_transition_get_property;
    object_class->set_property = lrg_slide_transition_set_property;

    transition_class = LRG_TRANSITION_CLASS (klass);
    transition_class->initialize = lrg_slide_transition_initialize;
    transition_class->shutdown = lrg_slide_transition_shutdown;
    transition_class->start = lrg_slide_transition_start;
    transition_class->update = lrg_slide_transition_update;
    transition_class->render = lrg_slide_transition_render;
    transition_class->reset = lrg_slide_transition_reset;

    /**
     * LrgSlideTransition:direction:
     *
     * The direction of the slide effect.
     *
     * Since: 1.0
     */
    properties[PROP_DIRECTION] =
        g_param_spec_enum ("direction",
                           "Direction",
                           "The slide direction",
                           LRG_TYPE_TRANSITION_DIRECTION,
                           LRG_TRANSITION_DIRECTION_LEFT,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgSlideTransition:mode:
     *
     * The slide mode (push, cover, or reveal).
     *
     * Since: 1.0
     */
    properties[PROP_MODE] =
        g_param_spec_enum ("mode",
                           "Mode",
                           "The slide mode",
                           LRG_TYPE_SLIDE_MODE,
                           LRG_SLIDE_MODE_PUSH,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_slide_transition_init (LrgSlideTransition *self)
{
    self->direction = LRG_TRANSITION_DIRECTION_LEFT;
    self->mode = LRG_SLIDE_MODE_PUSH;

    /*
     * Slide transitions typically don't need a hold phase since
     * both scenes are visible during the slide.
     */
    lrg_transition_set_hold_duration (LRG_TRANSITION (self), 0.0f);
}

/*
 * Public API
 */

/**
 * lrg_slide_transition_new:
 *
 * Creates a new slide transition with default settings.
 *
 * Returns: (transfer full): A new #LrgSlideTransition
 *
 * Since: 1.0
 */
LrgSlideTransition *
lrg_slide_transition_new (void)
{
    return g_object_new (LRG_TYPE_SLIDE_TRANSITION, NULL);
}

/**
 * lrg_slide_transition_new_with_options:
 * @direction: The slide direction
 * @mode: The slide mode
 *
 * Creates a new slide transition with the specified options.
 *
 * Returns: (transfer full): A new #LrgSlideTransition
 *
 * Since: 1.0
 */
LrgSlideTransition *
lrg_slide_transition_new_with_options (LrgTransitionDirection direction,
                                       LrgSlideMode           mode)
{
    return g_object_new (LRG_TYPE_SLIDE_TRANSITION,
                         "direction", direction,
                         "mode", mode,
                         NULL);
}

/**
 * lrg_slide_transition_get_direction:
 * @self: A #LrgSlideTransition
 *
 * Gets the slide direction.
 *
 * Returns: The #LrgTransitionDirection
 *
 * Since: 1.0
 */
LrgTransitionDirection
lrg_slide_transition_get_direction (LrgSlideTransition *self)
{
    g_return_val_if_fail (LRG_IS_SLIDE_TRANSITION (self), LRG_TRANSITION_DIRECTION_LEFT);

    return self->direction;
}

/**
 * lrg_slide_transition_set_direction:
 * @self: A #LrgSlideTransition
 * @direction: The slide direction
 *
 * Sets the slide direction.
 *
 * Since: 1.0
 */
void
lrg_slide_transition_set_direction (LrgSlideTransition     *self,
                                    LrgTransitionDirection  direction)
{
    g_return_if_fail (LRG_IS_SLIDE_TRANSITION (self));

    if (self->direction != direction)
    {
        self->direction = direction;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIRECTION]);
    }
}

/**
 * lrg_slide_transition_get_mode:
 * @self: A #LrgSlideTransition
 *
 * Gets the slide mode.
 *
 * Returns: The #LrgSlideMode
 *
 * Since: 1.0
 */
LrgSlideMode
lrg_slide_transition_get_mode (LrgSlideTransition *self)
{
    g_return_val_if_fail (LRG_IS_SLIDE_TRANSITION (self), LRG_SLIDE_MODE_PUSH);

    return self->mode;
}

/**
 * lrg_slide_transition_set_mode:
 * @self: A #LrgSlideTransition
 * @mode: The slide mode
 *
 * Sets the slide mode.
 *
 * Since: 1.0
 */
void
lrg_slide_transition_set_mode (LrgSlideTransition *self,
                               LrgSlideMode        mode)
{
    g_return_if_fail (LRG_IS_SLIDE_TRANSITION (self));

    if (self->mode != mode)
    {
        self->mode = mode;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
    }
}
