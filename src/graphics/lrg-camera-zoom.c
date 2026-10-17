/* lrg-camera-zoom.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCameraZoom: three states, first person, third person, and third
 * person crossing (easing to the near end, then first person).  The
 * easing is frame-rate independent: the gap shrinks by e^(-speed * dt)
 * each update.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "graphics/lrg-camera-zoom.h"

#include <math.h>

#define DEFAULT_STEP  (0.75f)
#define DEFAULT_SPEED (10.0f)

struct _LrgCameraZoom
{
    GObject  parent_instance;

    gfloat   min_distance;
    gfloat   max_distance;
    gfloat   step;
    gfloat   speed;
    gboolean first_person;
    gboolean crossing;
    gfloat   distance;   /* third person: the arm now */
    gfloat   target;     /* third person: where it eases to */
    gfloat   resume;     /* where leaving first person by a toggle goes */
};

G_DEFINE_TYPE (LrgCameraZoom, lrg_camera_zoom, G_TYPE_OBJECT)

enum
{
    SIGNAL_CROSSED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_camera_zoom_class_init (LrgCameraZoomClass *klass)
{
    signals[SIGNAL_CROSSED] = g_signal_new ("crossed", G_TYPE_FROM_CLASS (klass),
                                            G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                            G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_camera_zoom_init (LrgCameraZoom *self)
{
    self->min_distance = 1.0f;
    self->max_distance = 8.0f;
    self->step = DEFAULT_STEP;
    self->speed = DEFAULT_SPEED;
    self->distance = self->max_distance;
    self->target = self->max_distance;
    self->resume = self->max_distance;
}

static gfloat
clamp_distance (LrgCameraZoom *self,
                gfloat         distance)
{
    return CLAMP (distance, self->min_distance, self->max_distance);
}

LrgCameraZoom *
lrg_camera_zoom_new (gfloat min_distance,
                     gfloat max_distance)
{
    LrgCameraZoom *self;

    self = g_object_new (LRG_TYPE_CAMERA_ZOOM, NULL);
    lrg_camera_zoom_set_limits (self, min_distance, max_distance);
    self->distance = self->max_distance;
    self->target = self->max_distance;
    self->resume = self->max_distance;
    return self;
}

void
lrg_camera_zoom_set_limits (LrgCameraZoom *self,
                            gfloat         min_distance,
                            gfloat         max_distance)
{
    g_return_if_fail (LRG_IS_CAMERA_ZOOM (self));

    if (min_distance > 0.0f && isfinite (min_distance))
        self->min_distance = min_distance;
    if (isfinite (max_distance))
        self->max_distance = MAX (max_distance, self->min_distance);
    else
        self->max_distance = MAX (self->max_distance, self->min_distance);
    self->distance = clamp_distance (self, self->distance);
    self->target = clamp_distance (self, self->target);
    self->resume = clamp_distance (self, self->resume);
}

gfloat
lrg_camera_zoom_get_min_distance (LrgCameraZoom *self)
{
    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), 0.0f);
    return self->min_distance;
}

gfloat
lrg_camera_zoom_get_max_distance (LrgCameraZoom *self)
{
    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), 0.0f);
    return self->max_distance;
}

void
lrg_camera_zoom_set_step (LrgCameraZoom *self,
                          gfloat         step)
{
    g_return_if_fail (LRG_IS_CAMERA_ZOOM (self));
    if (step > 0.0f && isfinite (step))
        self->step = step;
}

gfloat
lrg_camera_zoom_get_step (LrgCameraZoom *self)
{
    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), 0.0f);
    return self->step;
}

void
lrg_camera_zoom_set_speed (LrgCameraZoom *self,
                           gfloat         speed)
{
    g_return_if_fail (LRG_IS_CAMERA_ZOOM (self));
    self->speed = isfinite (speed) ? MAX (speed, 0.0f) : 0.0f;
}

gfloat
lrg_camera_zoom_get_speed (LrgCameraZoom *self)
{
    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), 0.0f);
    return self->speed;
}

static void
enter_first_person (LrgCameraZoom *self)
{
    self->first_person = TRUE;
    self->crossing = FALSE;
    self->distance = self->min_distance;
    self->target = self->min_distance;
    g_signal_emit (self, signals[SIGNAL_CROSSED], 0, TRUE);
}

static void
leave_first_person (LrgCameraZoom *self,
                    gfloat         distance,
                    gfloat         target)
{
    self->first_person = FALSE;
    self->crossing = FALSE;
    self->distance = clamp_distance (self, distance);
    self->target = clamp_distance (self, target);
    g_signal_emit (self, signals[SIGNAL_CROSSED], 0, FALSE);
}

/* the arm stands at its near end (no more travel before the eye) */
static gboolean
at_near_end (LrgCameraZoom *self)
{
    return self->distance - self->min_distance <= LRG_CAMERA_ZOOM_SNAP;
}

gboolean
lrg_camera_zoom_scroll (LrgCameraZoom *self,
                        gfloat         notches)
{
    gfloat wanted;

    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), FALSE);

    if (!isfinite (notches) || notches == 0.0f)
        return FALSE;

    /* --- first person: only zooming out does anything --- */
    if (self->first_person)
    {
        if (notches > 0.0f)
            return FALSE;
        leave_first_person (self, self->min_distance,
                            self->min_distance + self->step * (fabsf (notches) - 1.0f));
        return TRUE;
    }

    /* --- third person: move the target; past the near end, cross --- */
    wanted = self->target - self->step * notches;
    if (notches < 0.0f)
    {
        self->crossing = FALSE;
        self->target = clamp_distance (self, wanted);
        return TRUE;
    }
    /* past the near end: from the near end itself, or by half a step or
     * more (a fast spin); a notch that only reaches it stops there */
    if (self->target - self->min_distance <= LRG_CAMERA_ZOOM_SNAP ||
        self->min_distance - wanted >= self->step * 0.5f)
    {
        self->target = self->min_distance;
        self->crossing = TRUE;
        /* already at the near end: the eye at once */
        if (at_near_end (self))
            enter_first_person (self);
        return TRUE;
    }
    self->target = clamp_distance (self, wanted);
    return TRUE;
}

gboolean
lrg_camera_zoom_update (LrgCameraZoom *self,
                        gfloat         delta)
{
    gfloat gap;

    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), FALSE);

    if (self->first_person)
        return FALSE;
    if (!isfinite (delta) || delta < 0.0f)
        delta = 0.0f;

    /* --- ease the arm --- */
    gap = self->target - self->distance;
    if (self->speed <= 0.0f)
        self->distance = self->target;
    else
        self->distance += gap * (1.0f - expf (-self->speed * delta));
    if (fabsf (self->target - self->distance) <= LRG_CAMERA_ZOOM_SNAP)
        self->distance = self->target;

    /* --- a pending crossing at the near end enters the eye --- */
    if (self->crossing && at_near_end (self))
    {
        enter_first_person (self);
        return TRUE;
    }
    return FALSE;
}

void
lrg_camera_zoom_set_distance (LrgCameraZoom *self,
                              gfloat         distance)
{
    g_return_if_fail (LRG_IS_CAMERA_ZOOM (self));

    if (!isfinite (distance))
        return;
    if (distance <= 0.0f)
    {
        if (!self->first_person)
            enter_first_person (self);
        return;
    }
    if (self->first_person)
    {
        leave_first_person (self, distance, distance);
        return;
    }
    self->crossing = FALSE;
    self->distance = clamp_distance (self, distance);
    self->target = self->distance;
}

gfloat
lrg_camera_zoom_get_distance (LrgCameraZoom *self)
{
    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), 0.0f);
    return self->first_person ? 0.0f : self->distance;
}

gfloat
lrg_camera_zoom_get_target (LrgCameraZoom *self)
{
    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), 0.0f);
    return self->first_person ? 0.0f : self->target;
}

gboolean
lrg_camera_zoom_get_first_person (LrgCameraZoom *self)
{
    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), FALSE);
    return self->first_person;
}

gboolean
lrg_camera_zoom_is_crossing (LrgCameraZoom *self)
{
    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), FALSE);
    return self->crossing && !self->first_person;
}

void
lrg_camera_zoom_set_first_person (LrgCameraZoom *self,
                                  gboolean       first_person)
{
    g_return_if_fail (LRG_IS_CAMERA_ZOOM (self));

    first_person = first_person != FALSE;
    if (first_person == self->first_person)
        return;
    if (first_person)
    {
        /* a toggle remembers where it left; a crossing came from the near end */
        if (!self->crossing)
            self->resume = self->target;
        enter_first_person (self);
        return;
    }
    leave_first_person (self, self->resume, self->resume);
}

void
lrg_camera_zoom_set_resume_distance (LrgCameraZoom *self,
                                     gfloat         distance)
{
    g_return_if_fail (LRG_IS_CAMERA_ZOOM (self));
    if (isfinite (distance) && distance > 0.0f)
        self->resume = clamp_distance (self, distance);
}

gfloat
lrg_camera_zoom_get_resume_distance (LrgCameraZoom *self)
{
    g_return_val_if_fail (LRG_IS_CAMERA_ZOOM (self), 0.0f);
    return self->resume;
}
