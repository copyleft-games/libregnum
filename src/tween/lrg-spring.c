/* lrg-spring.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Damped springs for UI motion.
 *
 * The spring obeys x'' = -w0^2 (x - target) - 2 z w0 x', with
 * w0 = 2 pi / response and z the damping ratio. Each step evaluates the
 * exact solution of that ODE over dt instead of integrating it, which
 * is why one 1/30 s step lands where two 1/60 s steps do.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TWEEN

#include <math.h>

#include "lrg-spring.h"
#include "../lrg-log.h"

/* Damping ratios this close to 1 use the critically damped formula;
 * the other two divide by ~0 there */
#define CRITICAL_BAND (1.0e-4f)

/* Longest step taken in one go; a frame hitch longer than this is
 * treated as this long so a stalled game doesn't teleport springs
 * through a whole oscillation at once */
#define MAX_STEP (0.25f)

/* Global motion state */
static gfloat   motion_speed = 1.0f;
static gboolean motion_reduced = FALSE;
static gpointer followed_settings = NULL;
static gulong   followed_handler = 0;

G_DEFINE_BOXED_TYPE (LrgSpring, lrg_spring, lrg_spring_copy, lrg_spring_free)

/* ==========================================================================
 * Presets
 * ========================================================================== */

void
lrg_motion_get_params (LrgMotion  motion,
                       gfloat    *response,
                       gfloat    *damping)
{
    gfloat r;
    gfloat d;

    switch (motion)
    {
    case LRG_MOTION_SNAPPY:
        r = 0.16f;
        d = 0.90f;
        break;
    case LRG_MOTION_BOUNCY:
        r = 0.38f;
        d = 0.52f;
        break;
    case LRG_MOTION_WOBBLY:
        r = 0.50f;
        d = 0.32f;
        break;
    case LRG_MOTION_NONE:
        r = 0.0f;
        d = 1.0f;
        break;
    case LRG_MOTION_SMOOTH:
    default:
        r = 0.30f;
        d = 1.0f;
        break;
    }

    if (response != NULL)
        *response = r;
    if (damping != NULL)
        *damping = d;
}

/* ==========================================================================
 * Boxed type
 * ========================================================================== */

LrgSpring *
lrg_spring_new (gfloat    value,
                LrgMotion motion)
{
    LrgSpring *self;

    self = g_new (LrgSpring, 1);
    lrg_spring_init (self, value, motion);

    return self;
}

LrgSpring *
lrg_spring_copy (const LrgSpring *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return (LrgSpring *) g_memdup2 (self, sizeof (LrgSpring));
}

void
lrg_spring_free (LrgSpring *self)
{
    g_free (self);
}

/* ==========================================================================
 * Setup
 * ========================================================================== */

void
lrg_spring_init (LrgSpring *self,
                 gfloat     value,
                 LrgMotion  motion)
{
    gfloat response;
    gfloat damping;

    g_return_if_fail (self != NULL);

    lrg_motion_get_params (motion, &response, &damping);
    lrg_spring_init_full (self, value, response, damping);
}

void
lrg_spring_init_full (LrgSpring *self,
                      gfloat     value,
                      gfloat     response,
                      gfloat     damping)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (response >= 0.0f);
    g_return_if_fail (damping > 0.0f);

    self->value = value;
    self->velocity = 0.0f;
    self->target = value;
    self->response = response;
    self->damping = damping;
}

void
lrg_spring_set_motion (LrgSpring *self,
                       LrgMotion  motion)
{
    g_return_if_fail (self != NULL);

    lrg_motion_get_params (motion, &self->response, &self->damping);
}

void
lrg_spring_set_target (LrgSpring *self,
                       gfloat     target)
{
    g_return_if_fail (self != NULL);

    self->target = target;
}

void
lrg_spring_snap (LrgSpring *self,
                 gfloat     value)
{
    g_return_if_fail (self != NULL);

    self->value = value;
    self->target = value;
    self->velocity = 0.0f;
}

gboolean
lrg_spring_is_settled (const LrgSpring *self)
{
    g_return_val_if_fail (self != NULL, TRUE);

    return self->value == self->target && self->velocity == 0.0f;
}

/* ==========================================================================
 * Stepping
 * ========================================================================== */

/*
 * solve:
 * @d: (inout): displacement from the target
 * @v: (inout): velocity
 * @w0: undamped angular frequency
 * @z: damping ratio
 * @t: time step
 *
 * Exact solution of d'' = -w0^2 d - 2 z w0 d' after time @t, for the
 * three damping regimes. Computed in double so long-lived springs
 * don't accumulate float drift.
 */
static void
solve (gdouble *d,
       gdouble *v,
       gdouble  w0,
       gdouble  z,
       gdouble  t)
{
    gdouble d0 = *d;
    gdouble v0 = *v;

    if (fabs (z - 1.0) < CRITICAL_BAND)
    {
        /* Critically damped: d = (d0 + c t) e^(-w0 t), c = v0 + w0 d0 */
        gdouble c = v0 + w0 * d0;
        gdouble e = exp (-w0 * t);

        *d = (d0 + c * t) * e;
        *v = (v0 - w0 * c * t) * e;
    }
    else if (z < 1.0)
    {
        /* Under-damped: decaying oscillation at wd = w0 sqrt(1 - z^2) */
        gdouble wd = w0 * sqrt (1.0 - z * z);
        gdouble e  = exp (-z * w0 * t);
        gdouble co = cos (wd * t);
        gdouble si = sin (wd * t);

        *d = e * (d0 * co + (v0 + z * w0 * d0) / wd * si);
        *v = e * (v0 * co - (z * w0 * v0 + w0 * w0 * d0) / wd * si);
    }
    else
    {
        /* Over-damped: sum of two decaying exponentials */
        gdouble s  = sqrt (z * z - 1.0);
        gdouble r1 = -w0 * (z - s);
        gdouble r2 = -w0 * (z + s);
        gdouble c2 = (v0 - r1 * d0) / (r2 - r1);
        gdouble c1 = d0 - c2;
        gdouble e1 = exp (r1 * t);
        gdouble e2 = exp (r2 * t);

        *d = c1 * e1 + c2 * e2;
        *v = r1 * c1 * e1 + r2 * c2 * e2;
    }
}

gboolean
lrg_spring_step (LrgSpring *self,
                 gfloat     delta_time)
{
    gdouble d;
    gdouble v;
    gdouble w0;
    gfloat  dt;

    g_return_val_if_fail (self != NULL, FALSE);

    if (lrg_spring_is_settled (self))
        return FALSE;

    /* No motion wanted: jump */
    if (motion_reduced || self->response <= 0.0f)
    {
        lrg_spring_snap (self, self->target);
        return FALSE;
    }

    dt = CLAMP (delta_time * motion_speed, 0.0f, MAX_STEP);
    if (dt <= 0.0f)
        return TRUE;

    d = (gdouble) self->value - (gdouble) self->target;
    v = (gdouble) self->velocity;
    w0 = 2.0 * G_PI / (gdouble) self->response;

    solve (&d, &v, w0, (gdouble) self->damping, (gdouble) dt);

    /* Close enough to rest: land exactly so callers can stop
     * requesting frames */
    if (fabs (d) < LRG_SPRING_EPSILON && fabs (v) < LRG_SPRING_EPSILON * 10.0)
    {
        lrg_spring_snap (self, self->target);
        return FALSE;
    }

    self->value = (gfloat) ((gdouble) self->target + d);
    self->velocity = (gfloat) v;

    return TRUE;
}

/* ==========================================================================
 * Global motion settings
 * ========================================================================== */

void
lrg_motion_set_speed (gfloat speed)
{
    motion_speed = CLAMP (speed, 0.01f, 10.0f);
}

gfloat
lrg_motion_get_speed (void)
{
    return motion_speed;
}

void
lrg_motion_set_reduced (gboolean reduced)
{
    motion_reduced = reduced ? TRUE : FALSE;
}

gboolean
lrg_motion_get_reduced (void)
{
    return motion_reduced;
}

static void
on_reduce_motion_changed (GObject    *settings,
                          GParamSpec *pspec,
                          gpointer    user_data)
{
    lrg_motion_set_reduced (
        lrg_accessibility_settings_get_reduce_motion (LRG_ACCESSIBILITY_SETTINGS (settings)));
}

static void
on_followed_settings_finalized (gpointer  data,
                                GObject  *where_the_object_was)
{
    followed_settings = NULL;
    followed_handler = 0;
}

void
lrg_motion_follow_accessibility (LrgAccessibilitySettings *settings)
{
    g_return_if_fail (settings == NULL || LRG_IS_ACCESSIBILITY_SETTINGS (settings));

    /* Drop the previous subscription */
    if (followed_settings != NULL)
    {
        g_signal_handler_disconnect (followed_settings, followed_handler);
        g_object_weak_unref (G_OBJECT (followed_settings),
                             on_followed_settings_finalized, NULL);
        followed_settings = NULL;
        followed_handler = 0;
    }

    if (settings == NULL)
        return;

    followed_settings = settings;
    followed_handler = g_signal_connect (settings, "notify::reduce-motion",
                                         G_CALLBACK (on_reduce_motion_changed),
                                         NULL);
    g_object_weak_ref (G_OBJECT (settings), on_followed_settings_finalized, NULL);

    on_reduce_motion_changed (G_OBJECT (settings), NULL, NULL);
}
