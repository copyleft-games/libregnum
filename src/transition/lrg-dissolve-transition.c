/* lrg-dissolve-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Noise-based dissolve transition.
 */

#include "lrg-dissolve-transition.h"
#include "../tween/lrg-easing.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TRANSITION

/**
 * LrgDissolveTransition:
 *
 * A transition that dissolves the screen using a noise pattern,
 * creating a particle-like breaking effect.
 *
 * The dissolve transition uses procedural noise to determine
 * which pixels fade first, creating organic-looking transitions:
 *
 * 1. **OUT phase**: Pixels dissolve based on noise threshold
 * 2. **HOLD phase**: Screen shows solid color
 * 3. **IN phase**: New scene dissolves in
 *
 * The `edge-width` property creates a glowing border around
 * dissolving edges, which can be colored for artistic effects
 * (fire dissolve, ice dissolve, etc.).
 *
 * ## Example usage
 *
 * ```c
 * LrgDissolveTransition *dissolve = lrg_dissolve_transition_new ();
 * lrg_dissolve_transition_set_edge_width (dissolve, 0.05f);
 * lrg_dissolve_transition_set_edge_color (dissolve, 255, 128, 0); // Orange glow
 * lrg_transition_start (LRG_TRANSITION (dissolve));
 * ```
 *
 * Since: 1.0
 */

struct _LrgDissolveTransition
{
    LrgTransition parent_instance;

    /* Noise parameters */
    gfloat noise_scale;
    guint32 seed;
    guint32 active_seed;  /* Seed used for current transition */

    /* Edge effect */
    gfloat edge_width;
    guint8 edge_r;
    guint8 edge_g;
    guint8 edge_b;
};

enum
{
    PROP_0,
    PROP_NOISE_SCALE,
    PROP_EDGE_WIDTH,
    PROP_EDGE_RED,
    PROP_EDGE_GREEN,
    PROP_EDGE_BLUE,
    PROP_SEED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgDissolveTransition, lrg_dissolve_transition, LRG_TYPE_TRANSITION)

/*
 * LrgTransition virtual method implementations
 */

static gboolean
lrg_dissolve_transition_initialize (LrgTransition  *transition,
                                    guint           width,
                                    guint           height,
                                    GError        **error)
{
    (void) transition;
    (void) width;
    (void) height;
    (void) error;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Dissolve transition initialized (viewport: %ux%u)", width, height);

    return TRUE;
}

static void
lrg_dissolve_transition_shutdown (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Dissolve transition shutdown");
}

static void
lrg_dissolve_transition_start (LrgTransition *transition)
{
    LrgDissolveTransition *self;

    self = LRG_DISSOLVE_TRANSITION (transition);

    /* Generate new seed if set to random */
    if (self->seed == 0)
    {
        self->active_seed = g_random_int ();
    }
    else
    {
        self->active_seed = self->seed;
    }

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Dissolve transition started (seed: %u, scale: %.2f)",
                   self->active_seed, self->noise_scale);
}

static void
lrg_dissolve_transition_update (LrgTransition *transition,
                                gfloat         delta_time)
{
    (void) transition;
    (void) delta_time;
}

static void
lrg_dissolve_transition_render (LrgTransition *transition,
                                guint          old_scene_texture,
                                guint          new_scene_texture,
                                guint          width,
                                guint          height)
{
    LrgDissolveTransition *self;
    LrgTransitionState state;
    gfloat phase_progress;
    gfloat eased_progress;
    LrgEasingType easing;
    gfloat threshold;

    self = LRG_DISSOLVE_TRANSITION (transition);
    state = lrg_transition_get_state (transition);
    phase_progress = lrg_transition_get_phase_progress (transition);
    easing = lrg_transition_get_easing (transition);
    eased_progress = lrg_easing_apply (easing, phase_progress);

    /*
     * Calculate dissolve threshold.
     * Pixels with noise values below the threshold are dissolved.
     *
     * OUT: threshold 0 -> 1 (progressively dissolve more pixels)
     * HOLD: fully dissolved
     * IN: threshold 1 -> 0 (progressively reveal more pixels)
     */
    switch (state)
    {
    case LRG_TRANSITION_STATE_OUT:
        threshold = eased_progress;
        break;

    case LRG_TRANSITION_STATE_HOLD:
        threshold = 1.0f;
        break;

    case LRG_TRANSITION_STATE_IN:
        threshold = 1.0f - eased_progress;
        break;

    case LRG_TRANSITION_STATE_IDLE:
    case LRG_TRANSITION_STATE_COMPLETE:
    default:
        threshold = 0.0f;
        break;
    }

    (void) self;
    (void) threshold;
    (void) old_scene_texture;
    (void) new_scene_texture;
    (void) width;
    (void) height;

    /*
     * TODO: Integrate with graylib shader rendering
     *
     * The dissolve effect is typically done with a shader that:
     * 1. Samples a noise texture (or generates procedural noise)
     * 2. Compares noise value to threshold
     * 3. If noise < threshold, show dissolve color
     * 4. If noise is near threshold (within edge_width), blend edge color
     * 5. Otherwise show scene texture
     *
     * Fragment shader pseudocode:
     *
     * float noise = sample_noise(uv, seed, noise_scale);
     * if (noise < threshold - edge_width) {
     *     fragColor = dissolve_color;
     * } else if (noise < threshold) {
     *     float t = (threshold - noise) / edge_width;
     *     fragColor = mix(scene_color, edge_color, t);
     * } else {
     *     fragColor = scene_color;
     * }
     */
}

static void
lrg_dissolve_transition_reset (LrgTransition *transition)
{
    (void) transition;

    lrg_debug (LRG_LOG_DOMAIN_TRANSITION, "Dissolve transition reset");
}

/*
 * GObject virtual methods
 */

static void
lrg_dissolve_transition_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
    LrgDissolveTransition *self;

    self = LRG_DISSOLVE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_NOISE_SCALE:
        g_value_set_float (value, self->noise_scale);
        break;

    case PROP_EDGE_WIDTH:
        g_value_set_float (value, self->edge_width);
        break;

    case PROP_EDGE_RED:
        g_value_set_uint (value, self->edge_r);
        break;

    case PROP_EDGE_GREEN:
        g_value_set_uint (value, self->edge_g);
        break;

    case PROP_EDGE_BLUE:
        g_value_set_uint (value, self->edge_b);
        break;

    case PROP_SEED:
        g_value_set_uint (value, self->seed);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_dissolve_transition_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    LrgDissolveTransition *self;

    self = LRG_DISSOLVE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_NOISE_SCALE:
        self->noise_scale = g_value_get_float (value);
        break;

    case PROP_EDGE_WIDTH:
        self->edge_width = g_value_get_float (value);
        break;

    case PROP_EDGE_RED:
        self->edge_r = (guint8) g_value_get_uint (value);
        break;

    case PROP_EDGE_GREEN:
        self->edge_g = (guint8) g_value_get_uint (value);
        break;

    case PROP_EDGE_BLUE:
        self->edge_b = (guint8) g_value_get_uint (value);
        break;

    case PROP_SEED:
        self->seed = g_value_get_uint (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_dissolve_transition_class_init (LrgDissolveTransitionClass *klass)
{
    GObjectClass *object_class;
    LrgTransitionClass *transition_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->get_property = lrg_dissolve_transition_get_property;
    object_class->set_property = lrg_dissolve_transition_set_property;

    transition_class = LRG_TRANSITION_CLASS (klass);
    transition_class->initialize = lrg_dissolve_transition_initialize;
    transition_class->shutdown = lrg_dissolve_transition_shutdown;
    transition_class->start = lrg_dissolve_transition_start;
    transition_class->update = lrg_dissolve_transition_update;
    transition_class->render = lrg_dissolve_transition_render;
    transition_class->reset = lrg_dissolve_transition_reset;

    /**
     * LrgDissolveTransition:noise-scale:
     *
     * Scale of the noise pattern (larger = bigger dissolve chunks).
     *
     * Since: 1.0
     */
    properties[PROP_NOISE_SCALE] =
        g_param_spec_float ("noise-scale",
                            "Noise Scale",
                            "Scale of the noise pattern",
                            0.01f,
                            100.0f,
                            8.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgDissolveTransition:edge-width:
     *
     * Width of the edge glow effect.
     *
     * Since: 1.0
     */
    properties[PROP_EDGE_WIDTH] =
        g_param_spec_float ("edge-width",
                            "Edge Width",
                            "Width of edge glow effect",
                            0.0f,
                            0.5f,
                            0.0f,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgDissolveTransition:edge-red:
     *
     * Red component of edge glow color.
     *
     * Since: 1.0
     */
    properties[PROP_EDGE_RED] =
        g_param_spec_uint ("edge-red",
                           "Edge Red",
                           "Red component of edge color",
                           0,
                           255,
                           255,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgDissolveTransition:edge-green:
     *
     * Green component of edge glow color.
     *
     * Since: 1.0
     */
    properties[PROP_EDGE_GREEN] =
        g_param_spec_uint ("edge-green",
                           "Edge Green",
                           "Green component of edge color",
                           0,
                           255,
                           255,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgDissolveTransition:edge-blue:
     *
     * Blue component of edge glow color.
     *
     * Since: 1.0
     */
    properties[PROP_EDGE_BLUE] =
        g_param_spec_uint ("edge-blue",
                           "Edge Blue",
                           "Blue component of edge color",
                           0,
                           255,
                           255,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgDissolveTransition:seed:
     *
     * Random seed for noise (0 = random each time).
     *
     * Since: 1.0
     */
    properties[PROP_SEED] =
        g_param_spec_uint ("seed",
                           "Seed",
                           "Random seed (0 = random each time)",
                           0,
                           G_MAXUINT32,
                           0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_dissolve_transition_init (LrgDissolveTransition *self)
{
    self->noise_scale = 8.0f;
    self->edge_width = 0.0f;
    self->edge_r = 255;
    self->edge_g = 255;
    self->edge_b = 255;
    self->seed = 0;
    self->active_seed = 0;
}

/*
 * Public API
 */

/**
 * lrg_dissolve_transition_new:
 *
 * Creates a new dissolve transition with default settings.
 *
 * Returns: (transfer full): A new #LrgDissolveTransition
 *
 * Since: 1.0
 */
LrgDissolveTransition *
lrg_dissolve_transition_new (void)
{
    return g_object_new (LRG_TYPE_DISSOLVE_TRANSITION, NULL);
}

/**
 * lrg_dissolve_transition_get_noise_scale:
 * @self: A #LrgDissolveTransition
 *
 * Gets the noise scale.
 *
 * Returns: Noise scale value
 *
 * Since: 1.0
 */
gfloat
lrg_dissolve_transition_get_noise_scale (LrgDissolveTransition *self)
{
    g_return_val_if_fail (LRG_IS_DISSOLVE_TRANSITION (self), 8.0f);

    return self->noise_scale;
}

/**
 * lrg_dissolve_transition_set_noise_scale:
 * @self: A #LrgDissolveTransition
 * @scale: Noise scale
 *
 * Sets the noise scale.
 *
 * Since: 1.0
 */
void
lrg_dissolve_transition_set_noise_scale (LrgDissolveTransition *self,
                                         gfloat                 scale)
{
    g_return_if_fail (LRG_IS_DISSOLVE_TRANSITION (self));

    scale = CLAMP (scale, 0.01f, 100.0f);

    if (self->noise_scale != scale)
    {
        self->noise_scale = scale;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NOISE_SCALE]);
    }
}

/**
 * lrg_dissolve_transition_get_edge_width:
 * @self: A #LrgDissolveTransition
 *
 * Gets the edge width.
 *
 * Returns: Edge width value
 *
 * Since: 1.0
 */
gfloat
lrg_dissolve_transition_get_edge_width (LrgDissolveTransition *self)
{
    g_return_val_if_fail (LRG_IS_DISSOLVE_TRANSITION (self), 0.0f);

    return self->edge_width;
}

/**
 * lrg_dissolve_transition_set_edge_width:
 * @self: A #LrgDissolveTransition
 * @width: Edge width
 *
 * Sets the edge width.
 *
 * Since: 1.0
 */
void
lrg_dissolve_transition_set_edge_width (LrgDissolveTransition *self,
                                        gfloat                 width)
{
    g_return_if_fail (LRG_IS_DISSOLVE_TRANSITION (self));

    width = CLAMP (width, 0.0f, 0.5f);

    if (self->edge_width != width)
    {
        self->edge_width = width;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDGE_WIDTH]);
    }
}

/**
 * lrg_dissolve_transition_get_edge_color:
 * @self: A #LrgDissolveTransition
 * @r: (out) (nullable): Return location for red
 * @g: (out) (nullable): Return location for green
 * @b: (out) (nullable): Return location for blue
 *
 * Gets the edge glow color.
 *
 * Since: 1.0
 */
void
lrg_dissolve_transition_get_edge_color (LrgDissolveTransition *self,
                                        guint8                *r,
                                        guint8                *g,
                                        guint8                *b)
{
    g_return_if_fail (LRG_IS_DISSOLVE_TRANSITION (self));

    if (r != NULL)
        *r = self->edge_r;
    if (g != NULL)
        *g = self->edge_g;
    if (b != NULL)
        *b = self->edge_b;
}

/**
 * lrg_dissolve_transition_set_edge_color:
 * @self: A #LrgDissolveTransition
 * @r: Red component
 * @g: Green component
 * @b: Blue component
 *
 * Sets the edge glow color.
 *
 * Since: 1.0
 */
void
lrg_dissolve_transition_set_edge_color (LrgDissolveTransition *self,
                                        guint8                 r,
                                        guint8                 g,
                                        guint8                 b)
{
    g_return_if_fail (LRG_IS_DISSOLVE_TRANSITION (self));

    g_object_freeze_notify (G_OBJECT (self));

    if (self->edge_r != r)
    {
        self->edge_r = r;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDGE_RED]);
    }

    if (self->edge_g != g)
    {
        self->edge_g = g;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDGE_GREEN]);
    }

    if (self->edge_b != b)
    {
        self->edge_b = b;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EDGE_BLUE]);
    }

    g_object_thaw_notify (G_OBJECT (self));
}

/**
 * lrg_dissolve_transition_get_seed:
 * @self: A #LrgDissolveTransition
 *
 * Gets the noise seed.
 *
 * Returns: The seed value
 *
 * Since: 1.0
 */
guint32
lrg_dissolve_transition_get_seed (LrgDissolveTransition *self)
{
    g_return_val_if_fail (LRG_IS_DISSOLVE_TRANSITION (self), 0);

    return self->seed;
}

/**
 * lrg_dissolve_transition_set_seed:
 * @self: A #LrgDissolveTransition
 * @seed: Noise seed
 *
 * Sets the noise seed.
 *
 * Since: 1.0
 */
void
lrg_dissolve_transition_set_seed (LrgDissolveTransition *self,
                                  guint32                seed)
{
    g_return_if_fail (LRG_IS_DISSOLVE_TRANSITION (self));

    if (self->seed != seed)
    {
        self->seed = seed;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEED]);
    }
}
