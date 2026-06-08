/* lrg-reel-dissolve-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelDissolveTransition - deterministic per-pixel dissolve between frames.
 */

#include "config.h"
#include "lrg-reel-dissolve-transition.h"
#include "lrg-reel-transition.h"
#include "../graphics/lrg-image-canvas.h"

struct _LrgReelDissolveTransition
{
    LrgReelTransition parent_instance;
    guint             seed;
};

G_DEFINE_FINAL_TYPE (LrgReelDissolveTransition, lrg_reel_dissolve_transition,
                     LRG_TYPE_REEL_TRANSITION)

enum
{
    PROP_0,
    PROP_SEED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Helper: draw src fully onto dst with REPLACE blend
 * -------------------------------------------------------------------------- */

static void
draw_full (GrlImage *dst,
           GrlImage *src)
{
    GrlRectangle dst_rect;

    dst_rect.x = 0.0f;
    dst_rect.y = 0.0f;
    dst_rect.width = (gfloat) grl_image_get_width (dst);
    dst_rect.height = (gfloat) grl_image_get_height (dst);

    grl_image_draw_image (dst, src, NULL, &dst_rect, NULL);
}

/* --------------------------------------------------------------------------
 * Composite vfunc
 * -------------------------------------------------------------------------- */

static void
lrg_reel_dissolve_transition_composite (LrgReelTransition *base,
                                         LrgImageCanvas    *canvas,
                                         GrlImage          *from,
                                         GrlImage          *to,
                                         gdouble            progress)
{
    LrgReelDissolveTransition *self;
    GrlImage                  *canvas_img;
    gint                       w;
    gint                       h;
    gint                       x;
    gint                       y;
    guint32                    seed32;

    self       = LRG_REEL_DISSOLVE_TRANSITION (base);
    canvas_img = lrg_image_canvas_get_image (canvas);
    w          = grl_image_get_width (canvas_img);
    h          = grl_image_get_height (canvas_img);
    seed32     = (guint32) self->seed;

    /* Draw the outgoing frame as the base. */
    draw_full (canvas_img, from);

    /* Per-pixel reveal: copy pixels from @to whose hash threshold < progress. */
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            guint32             hval;
            gdouble             frac;
            g_autoptr(GrlColor) c = NULL;

            hval = (((guint32) x) * 73856093u) ^
                   (((guint32) y) * 19349663u) ^
                   seed32;
            frac = (gdouble) (hval % 100000u) / 100000.0;

            if (frac < progress)
            {
                c = grl_image_get_pixel (to, x, y);
                grl_image_draw_pixel (canvas_img, x, y, c);
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_dissolve_transition_get_property (GObject    *object,
                                            guint       prop_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
    LrgReelDissolveTransition *self = LRG_REEL_DISSOLVE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_SEED:
        g_value_set_uint (value, self->seed);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_dissolve_transition_set_property (GObject      *object,
                                            guint         prop_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
    LrgReelDissolveTransition *self = LRG_REEL_DISSOLVE_TRANSITION (object);

    switch (prop_id)
    {
    case PROP_SEED:
        lrg_reel_dissolve_transition_set_seed (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_dissolve_transition_class_init (LrgReelDissolveTransitionClass *klass)
{
    GObjectClass           *object_class     = G_OBJECT_CLASS (klass);
    LrgReelTransitionClass *transition_class = LRG_REEL_TRANSITION_CLASS (klass);

    object_class->get_property = lrg_reel_dissolve_transition_get_property;
    object_class->set_property = lrg_reel_dissolve_transition_set_property;

    transition_class->composite = lrg_reel_dissolve_transition_composite;

    /**
     * LrgReelDissolveTransition:seed:
     *
     * Hash seed used to determine the per-pixel reveal order.  Two transitions
     * with the same seed produce identical dissolve patterns.  The default
     * value is 1337.
     *
     * Since: 1.0
     */
    properties[PROP_SEED] =
        g_param_spec_uint ("seed",
                           "Seed",
                           "Hash seed for deterministic pixel reveal order",
                           0, G_MAXUINT, 1337,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_dissolve_transition_init (LrgReelDissolveTransition *self)
{
    self->seed = 1337u;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_dissolve_transition_new:
 *
 * Creates a new #LrgReelDissolveTransition with the default seed (1337).
 *
 * Returns: (transfer full): a new #LrgReelDissolveTransition
 *
 * Since: 1.0
 */
LrgReelDissolveTransition *
lrg_reel_dissolve_transition_new (void)
{
    return g_object_new (LRG_TYPE_REEL_DISSOLVE_TRANSITION, NULL);
}

/**
 * lrg_reel_dissolve_transition_get_seed:
 * @self: an #LrgReelDissolveTransition
 *
 * Returns the hash seed.
 *
 * Returns: the seed value
 *
 * Since: 1.0
 */
guint
lrg_reel_dissolve_transition_get_seed (LrgReelDissolveTransition *self)
{
    g_return_val_if_fail (LRG_IS_REEL_DISSOLVE_TRANSITION (self), 0);
    return self->seed;
}

/**
 * lrg_reel_dissolve_transition_set_seed:
 * @self: an #LrgReelDissolveTransition
 * @seed: the new seed value
 *
 * Sets the hash seed.
 *
 * Since: 1.0
 */
void
lrg_reel_dissolve_transition_set_seed (LrgReelDissolveTransition *self,
                                        guint                      seed)
{
    g_return_if_fail (LRG_IS_REEL_DISSOLVE_TRANSITION (self));

    if (self->seed == seed)
        return;

    self->seed = seed;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SEED]);
}
