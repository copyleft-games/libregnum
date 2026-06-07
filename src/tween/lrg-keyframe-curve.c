/* lrg-keyframe-curve.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Pure, clock-free multi-keyframe value sampler for offline/headless rendering.
 */

#include "config.h"

#include "lrg-keyframe-curve.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TWEEN

/**
 * LrgKeyframeCurve:
 *
 * A pure, clock-free multi-keyframe value sampler.
 *
 * #LrgKeyframeCurve stores an ordered sequence of (time, value, easing)
 * keyframes and can evaluate the curve at any normalized time @t without
 * requiring a running game loop or #LrgTweenManager.  It is the recommended
 * primitive for headless / offline animation baking.
 *
 * Example usage — baking a 60-frame sprite animation to a GIF:
 * |[<!-- language="C" -->
 * #define N_FRAMES 60
 *
 * g_autoptr(LrgKeyframeCurve) curve = lrg_keyframe_curve_new ();
 * lrg_keyframe_curve_add_key (curve, 0.0f,  0.0f, LRG_EASING_EASE_IN_CUBIC);
 * lrg_keyframe_curve_add_key (curve, 0.5f, 80.0f, LRG_EASING_EASE_OUT_CUBIC);
 * lrg_keyframe_curve_add_key (curve, 1.0f,  0.0f, LRG_EASING_LINEAR);
 *
 * for (gint frame = 0; frame < N_FRAMES; frame++)
 * {
 *     gfloat t = (gfloat) frame / (gfloat)(N_FRAMES - 1);
 *     gfloat y = lrg_keyframe_curve_sample (curve, t);
 *     // draw sprite at y into GrlImage, then append frame to GrlGifWriter ...
 * }
 * ]|
 *
 * Since: 1.0
 */

/* Internal keyframe entry */
typedef struct
{
    gfloat        t;
    gfloat        value;
    LrgEasingType ease_to_next;
} LrgKeyframe;

struct _LrgKeyframeCurve
{
    GObject  parent_instance;

    /* Sorted (ascending by t) array of LrgKeyframe */
    GArray  *keys;
};

G_DEFINE_FINAL_TYPE (LrgKeyframeCurve, lrg_keyframe_curve, G_TYPE_OBJECT)

/* -------------------------------------------------------------------------- */
/* GObject lifecycle                                                           */
/* -------------------------------------------------------------------------- */

static void
lrg_keyframe_curve_finalize (GObject *object)
{
    LrgKeyframeCurve *self = LRG_KEYFRAME_CURVE (object);

    g_array_unref (self->keys);

    G_OBJECT_CLASS (lrg_keyframe_curve_parent_class)->finalize (object);
}

static void
lrg_keyframe_curve_class_init (LrgKeyframeCurveClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_keyframe_curve_finalize;
}

static void
lrg_keyframe_curve_init (LrgKeyframeCurve *self)
{
    self->keys = g_array_new (FALSE, FALSE, sizeof (LrgKeyframe));
}

/* -------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* -------------------------------------------------------------------------- */

/*
 * find_insert_position:
 *
 * Returns the index at which a key with time @t should be inserted to keep the
 * array sorted.  If a key with exactly the same @t already exists its index is
 * returned instead (so the caller can overwrite it).
 *
 * Returns: index in [0, self->keys->len]
 */
static guint
find_insert_position (LrgKeyframeCurve *self,
                      gfloat            t,
                      gboolean         *exact_match)
{
    guint i;

    *exact_match = FALSE;

    for (i = 0; i < self->keys->len; i++)
    {
        LrgKeyframe *key = &g_array_index (self->keys, LrgKeyframe, i);

        if (key->t == t)
        {
            *exact_match = TRUE;
            return i;
        }

        if (key->t > t)
            return i;
    }

    return self->keys->len;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * lrg_keyframe_curve_new:
 *
 * Creates a new, empty keyframe curve.
 *
 * Returns: (transfer full): A new #LrgKeyframeCurve
 *
 * Since: 1.0
 */
LrgKeyframeCurve *
lrg_keyframe_curve_new (void)
{
    return g_object_new (LRG_TYPE_KEYFRAME_CURVE, NULL);
}

/**
 * lrg_keyframe_curve_add_key:
 * @self: an #LrgKeyframeCurve
 * @t: normalized time of this keyframe
 * @value: value at this keyframe
 * @ease_to_next: easing used from this key to the next
 *
 * Adds (or replaces) the keyframe at normalized time @t.
 *
 * Since: 1.0
 */
void
lrg_keyframe_curve_add_key (LrgKeyframeCurve *self,
                             gfloat            t,
                             gfloat            value,
                             LrgEasingType     ease_to_next)
{
    LrgKeyframe  new_key;
    gboolean     exact_match;
    guint        idx;

    g_return_if_fail (LRG_IS_KEYFRAME_CURVE (self));

    new_key.t            = t;
    new_key.value        = value;
    new_key.ease_to_next = ease_to_next;

    idx = find_insert_position (self, t, &exact_match);

    if (exact_match)
    {
        /* Overwrite existing key at same t */
        g_array_index (self->keys, LrgKeyframe, idx) = new_key;
    }
    else
    {
        g_array_insert_val (self->keys, idx, new_key);
    }
}

/**
 * lrg_keyframe_curve_sample:
 * @self: an #LrgKeyframeCurve
 * @t: normalized time to evaluate
 *
 * Samples the curve at normalized time @t.
 *
 * Returns: the interpolated value
 *
 * Since: 1.0
 */
gfloat
lrg_keyframe_curve_sample (LrgKeyframeCurve *self,
                            gfloat            t)
{
    LrgKeyframe *first;
    LrgKeyframe *last;
    LrgKeyframe *lo;
    LrgKeyframe *hi;
    gfloat       seg_len;
    gfloat       seg_t;
    guint        i;

    g_return_val_if_fail (LRG_IS_KEYFRAME_CURVE (self), 0.0f);
    g_return_val_if_fail (self->keys->len > 0, 0.0f);

    first = &g_array_index (self->keys, LrgKeyframe, 0);
    last  = &g_array_index (self->keys, LrgKeyframe, self->keys->len - 1);

    /* Single-key or clamp-before-first */
    if (self->keys->len == 1 || t <= first->t)
        return first->value;

    /* Clamp-after-last */
    if (t >= last->t)
        return last->value;

    /* Find the bracketing pair [lo, hi] where lo->t <= t < hi->t */
    lo = first;
    hi = NULL;

    for (i = 1; i < self->keys->len; i++)
    {
        hi = &g_array_index (self->keys, LrgKeyframe, i);

        if (hi->t > t)
            break;

        lo = hi;
        hi = NULL;
    }

    /* hi must be non-NULL here because t < last->t */
    if (hi == NULL)
        return last->value;

    seg_len = hi->t - lo->t;

    if (seg_len <= 0.0f)
        return lo->value;

    seg_t = (t - lo->t) / seg_len;

    return lrg_easing_interpolate (lo->ease_to_next, lo->value, hi->value, seg_t);
}

/**
 * lrg_keyframe_curve_get_key_count:
 * @self: an #LrgKeyframeCurve
 *
 * Returns the number of keyframes stored in the curve.
 *
 * Returns: the number of keyframes
 *
 * Since: 1.0
 */
guint
lrg_keyframe_curve_get_key_count (LrgKeyframeCurve *self)
{
    g_return_val_if_fail (LRG_IS_KEYFRAME_CURVE (self), 0);

    return self->keys->len;
}
