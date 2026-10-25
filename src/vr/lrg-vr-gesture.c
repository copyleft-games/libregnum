/* lrg-vr-gesture.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gestures drawn with a controller.  See lrg-vr-gesture.h.
 *
 * The matching is the $1 unistroke recognizer's (Wobbrock, Wilson and Li,
 * 2007) without its rotation search: a drawing and every template are
 * resampled to the same number of points evenly spaced along their
 * length, moved so their centroid is at 0 and scaled uniformly so their
 * larger side is 1 (uniformly, so a straight line stays a line); the
 * score falls with the mean distance between corresponding points.
 * Orientation and direction therefore count: a line up is not a line
 * down, a Z is not an N.  A closed template (a circle) is also tried
 * starting at sixteen places along it, so a circle begun at the bottom
 * matches one begun at the top.
 */

#include "lrg-vr-gesture.h"

#include <math.h>
#include <string.h>

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_VR
#include "../lrg-log.h"

/**
 * SECTION:lrg-vr-gesture
 * @title: LrgVRGestureRecognizer
 * @short_description: Shapes drawn in the air with a controller
 *
 * #LrgVRGestureRecognizer turns a hand's path into a named shape.  Call
 * lrg_vr_gesture_recognizer_begin() with the head's pose when a button
 * goes down, lrg_vr_gesture_recognizer_add_point() every frame with the
 * hand's position, and lrg_vr_gesture_recognizer_end() when it comes up:
 * the #LrgVRGesture names the template drawn, a push or a pull, or
 * nothing.
 *
 * |[<!-- language="C" -->
 * g_autoptr(LrgVRGestureRecognizer) gestures = lrg_vr_gesture_recognizer_new ();
 * g_autoptr(LrgVRGesture) drawn = NULL;
 *
 * lrg_vr_gesture_recognizer_add_builtin_templates (gestures);
 * lrg_vr_gesture_recognizer_begin (gestures, head_pose);
 * ... every frame while the button is held:
 * lrg_vr_gesture_recognizer_add_point (gestures, &hand_pose[12], now);
 * ... when it lets go:
 * drawn = lrg_vr_gesture_recognizer_end (gestures);
 * if (drawn->name != NULL)
 *     cast_the_spell_for (drawn->name);
 * ]|
 *
 * Since: 1.0
 */

/* points every drawing and template is resampled to */
#define N_SAMPLES (48)
/* the starts a closed template is also tried from */
#define N_SHIFTS (16)
/* the mean distance (of shapes 1 across) at which the score reaches 0 */
#define SCORE_SPAN (0.4f)
/* a drawing this close to two different shapes is neither (a wrong
   shape costs more than none) */
#define AMBIGUITY (0.04f)
/* a push: its depth this many times its spread in the view plane */
#define THRUST_RATIO (1.3f)
/* and at least this many times the minimum size */
#define THRUST_MIN (1.5f)

typedef struct
{
    gchar    *name;
    gboolean  closed;
    gfloat    points[N_SAMPLES * 2];
} Template;

struct _LrgVRGestureRecognizer
{
    GObject parent_instance;

    GPtrArray *templates;   /* Template */
    gfloat     threshold;
    gfloat     min_size;

    gboolean   drawing;
    gfloat     right[3];    /* the view plane: sideways, up, away */
    gfloat     up[3];
    gfloat     forward[3];
    GArray    *points;      /* gfloat x, y, z */
    gdouble    first_time;
    gdouble    last_time;
};

G_DEFINE_TYPE (LrgVRGestureRecognizer, lrg_vr_gesture_recognizer, G_TYPE_OBJECT)

G_DEFINE_BOXED_TYPE (LrgVRGesture, lrg_vr_gesture, lrg_vr_gesture_copy, lrg_vr_gesture_free)

/* ==========================================================================
 * LrgVRGesture
 * ========================================================================== */

LrgVRGesture *
lrg_vr_gesture_copy (const LrgVRGesture *self)
{
    LrgVRGesture *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new (LrgVRGesture, 1);
    *copy = *self;
    copy->name = g_strdup (self->name);
    return copy;
}

void
lrg_vr_gesture_free (LrgVRGesture *self)
{
    if (self == NULL)
        return;
    g_free (self->name);
    g_free (self);
}

/* ==========================================================================
 * The shape arithmetic
 * ========================================================================== */

static void
template_free (gpointer data)
{
    Template *t = data;

    g_free (t->name);
    g_free (t);
}

/* the length of a path of @n x, y pairs */
static gfloat
path_length (const gfloat *points,
             guint         n)
{
    gfloat length = 0.0f;
    guint i;

    for (i = 1; i < n; i++)
        length += hypotf (points[i * 2] - points[(i - 1) * 2],
                          points[i * 2 + 1] - points[(i - 1) * 2 + 1]);
    return length;
}

/*
 * resample:
 * @points: @n x, y pairs
 * @out: N_SAMPLES x, y pairs, evenly spaced along the path
 *
 * Returns: %FALSE when the path has no length
 */
static gboolean
resample (const gfloat *points,
          guint         n,
          gfloat       *out)
{
    gfloat interval;
    gfloat walked;
    gfloat px;
    gfloat py;
    guint made;
    guint i;

    interval = path_length (points, n) / (gfloat)(N_SAMPLES - 1);
    if (n < 2 || !(interval > 1e-7f))
        return FALSE;

    /* walk the path, dropping a sample every @interval; a segment longer
       than the interval yields several */
    out[0] = px = points[0];
    out[1] = py = points[1];
    made = 1;
    walked = 0.0f;
    for (i = 1; i < n && made < N_SAMPLES; i++)
    {
        gfloat qx = points[i * 2];
        gfloat qy = points[i * 2 + 1];
        gfloat d = hypotf (qx - px, qy - py);

        while (walked + d >= interval && made < N_SAMPLES)
        {
            gfloat f = d > 0.0f ? (interval - walked) / d : 0.0f;

            px = px + f * (qx - px);
            py = py + f * (qy - py);
            out[made * 2] = px;
            out[made * 2 + 1] = py;
            made++;
            d = hypotf (qx - px, qy - py);
            walked = 0.0f;
        }
        walked += d;
        px = qx;
        py = qy;
    }
    /* rounding can leave the last sample short: the path's end */
    for (; made < N_SAMPLES; made++)
    {
        out[made * 2] = points[(n - 1) * 2];
        out[made * 2 + 1] = points[(n - 1) * 2 + 1];
    }
    return TRUE;
}

/*
 * normalize:
 * Centres N_SAMPLES pairs on their centroid and scales them so the
 * larger side of their box is 1.
 *
 * Returns: %FALSE when they span nothing
 */
static gboolean
normalize (gfloat *points)
{
    gfloat cx = 0.0f;
    gfloat cy = 0.0f;
    gfloat min_x = G_MAXFLOAT;
    gfloat max_x = -G_MAXFLOAT;
    gfloat min_y = G_MAXFLOAT;
    gfloat max_y = -G_MAXFLOAT;
    gfloat side;
    guint i;

    for (i = 0; i < N_SAMPLES; i++)
    {
        cx += points[i * 2];
        cy += points[i * 2 + 1];
        min_x = MIN (min_x, points[i * 2]);
        max_x = MAX (max_x, points[i * 2]);
        min_y = MIN (min_y, points[i * 2 + 1]);
        max_y = MAX (max_y, points[i * 2 + 1]);
    }
    side = MAX (max_x - min_x, max_y - min_y);
    if (!(side > 1e-7f))
        return FALSE;
    cx /= (gfloat)N_SAMPLES;
    cy /= (gfloat)N_SAMPLES;
    for (i = 0; i < N_SAMPLES; i++)
    {
        points[i * 2] = (points[i * 2] - cx) / side;
        points[i * 2 + 1] = (points[i * 2 + 1] - cy) / side;
    }
    return TRUE;
}

/* the mean distance between a drawing and a template begun @shift
   samples along */
static gfloat
mean_distance (const gfloat *drawing,
               const gfloat *template,
               guint         shift)
{
    gfloat sum = 0.0f;
    guint i;

    for (i = 0; i < N_SAMPLES; i++)
    {
        guint j = (i + shift) % N_SAMPLES;

        sum += hypotf (drawing[i * 2] - template[j * 2],
                       drawing[i * 2 + 1] - template[j * 2 + 1]);
    }
    return sum / (gfloat)N_SAMPLES;
}

/* the best (smallest) distance to @t, over its starts when closed */
static gfloat
template_distance (const Template *t,
                   const gfloat   *drawing)
{
    gfloat best;
    guint s;

    best = mean_distance (drawing, t->points, 0);
    if (!t->closed)
        return best;
    for (s = 1; s < N_SHIFTS; s++)
        best = MIN (best, mean_distance (drawing, t->points, s * N_SAMPLES / N_SHIFTS));
    return best;
}

static gfloat
score_of (gfloat distance)
{
    return CLAMP (1.0f - distance / SCORE_SPAN, 0.0f, 1.0f);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_vr_gesture_recognizer_finalize (GObject *object)
{
    LrgVRGestureRecognizer *self = LRG_VR_GESTURE_RECOGNIZER (object);

    g_clear_pointer (&self->templates, g_ptr_array_unref);
    g_clear_pointer (&self->points, g_array_unref);

    G_OBJECT_CLASS (lrg_vr_gesture_recognizer_parent_class)->finalize (object);
}

static void
lrg_vr_gesture_recognizer_class_init (LrgVRGestureRecognizerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_vr_gesture_recognizer_finalize;
}

static void
lrg_vr_gesture_recognizer_init (LrgVRGestureRecognizer *self)
{
    self->templates = g_ptr_array_new_with_free_func (template_free);
    self->points = g_array_new (FALSE, FALSE, sizeof (gfloat));
    self->threshold = 0.75f;
    self->min_size = 0.1f;
}

LrgVRGestureRecognizer *
lrg_vr_gesture_recognizer_new (void)
{
    return g_object_new (LRG_TYPE_VR_GESTURE_RECOGNIZER, NULL);
}

/* ==========================================================================
 * Templates
 * ========================================================================== */

gboolean
lrg_vr_gesture_recognizer_add_template (LrgVRGestureRecognizer *self,
                                        const gchar            *name,
                                        const gfloat           *points,
                                        guint                   n_points,
                                        gboolean                closed)
{
    Template *t;

    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), FALSE);
    g_return_val_if_fail (name != NULL && *name != '\0', FALSE);
    g_return_val_if_fail (points != NULL && n_points >= 2, FALSE);

    t = g_new0 (Template, 1);
    if (!resample (points, n_points, t->points) || !normalize (t->points))
    {
        lrg_warning (LRG_LOG_DOMAIN_VR, "gesture template \"%s\" spans no distance", name);
        g_free (t);
        return FALSE;
    }
    t->name = g_strdup (name);
    t->closed = closed;
    g_ptr_array_add (self->templates, t);
    return TRUE;
}

/* adds @n pairs and, reversed, the same shape drawn the other way */
static void
add_both_ways (LrgVRGestureRecognizer *self,
               const gchar            *name,
               const gfloat           *points,
               guint                   n,
               gboolean                closed)
{
    g_autofree gfloat *reversed = g_new (gfloat, n * 2);
    guint i;

    for (i = 0; i < n; i++)
    {
        reversed[i * 2] = points[(n - 1 - i) * 2];
        reversed[i * 2 + 1] = points[(n - 1 - i) * 2 + 1];
    }
    (void)lrg_vr_gesture_recognizer_add_template (self, name, points, n, closed);
    (void)lrg_vr_gesture_recognizer_add_template (self, name, reversed, n, closed);
}

void
lrg_vr_gesture_recognizer_add_builtin_templates (LrgVRGestureRecognizer *self)
{
    static const gfloat up[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const gfloat down[] = { 0.0f, 1.0f, 0.0f, 0.0f };
    static const gfloat left[] = { 1.0f, 0.0f, 0.0f, 0.0f };
    static const gfloat right[] = { 0.0f, 0.0f, 1.0f, 0.0f };
    /* a Z, and a lightning bolt drawn from the top */
    static const gfloat z[] = { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    static const gfloat bolt[] = { 0.65f, 1.0f, 0.2f, 0.45f, 0.8f, 0.55f, 0.35f, 0.0f };
    /* a V, and a tick */
    static const gfloat v[] = { 0.0f, 1.0f, 0.5f, 0.0f, 1.0f, 1.0f };
    static const gfloat tick[] = { 0.0f, 0.45f, 0.3f, 0.0f, 1.0f, 1.0f };
    static const gfloat caret[] = { 0.0f, 0.0f, 0.5f, 1.0f, 1.0f, 0.0f };
    static const gfloat triangle[] = { 0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f };
    static const gfloat square[] = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    gfloat circle[33 * 2];
    guint i;

    g_return_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self));

    (void)lrg_vr_gesture_recognizer_add_template (self, "line-up", up, 2, FALSE);
    (void)lrg_vr_gesture_recognizer_add_template (self, "line-down", down, 2, FALSE);
    (void)lrg_vr_gesture_recognizer_add_template (self, "line-left", left, 2, FALSE);
    (void)lrg_vr_gesture_recognizer_add_template (self, "line-right", right, 2, FALSE);

    /* a circle from the top, clockwise; and the other way round */
    for (i = 0; i <= 32; i++)
    {
        gfloat a = (gfloat)i * (gfloat)(2.0 * G_PI / 32.0);

        circle[i * 2] = sinf (a);
        circle[i * 2 + 1] = cosf (a);
    }
    add_both_ways (self, "circle", circle, 33, TRUE);

    (void)lrg_vr_gesture_recognizer_add_template (self, "zigzag", z, 4, FALSE);
    (void)lrg_vr_gesture_recognizer_add_template (self, "zigzag", bolt, 4, FALSE);
    (void)lrg_vr_gesture_recognizer_add_template (self, "v", v, 3, FALSE);
    (void)lrg_vr_gesture_recognizer_add_template (self, "v", tick, 3, FALSE);
    (void)lrg_vr_gesture_recognizer_add_template (self, "caret", caret, 3, FALSE);
    add_both_ways (self, "triangle", triangle, 4, TRUE);
    add_both_ways (self, "square", square, 5, TRUE);
}

guint
lrg_vr_gesture_recognizer_remove_template (LrgVRGestureRecognizer *self,
                                           const gchar            *name)
{
    guint removed = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), 0);
    g_return_val_if_fail (name != NULL, 0);

    for (i = self->templates->len; i > 0; i--)
    {
        Template *t = g_ptr_array_index (self->templates, i - 1);

        if (g_strcmp0 (t->name, name) == 0)
        {
            g_ptr_array_remove_index (self->templates, i - 1);
            removed++;
        }
    }
    return removed;
}

void
lrg_vr_gesture_recognizer_clear_templates (LrgVRGestureRecognizer *self)
{
    g_return_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self));

    g_ptr_array_set_size (self->templates, 0);
}

gchar **
lrg_vr_gesture_recognizer_dup_names (LrgVRGestureRecognizer *self)
{
    g_autoptr(GPtrArray) names = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), NULL);

    names = g_ptr_array_new ();
    for (i = 0; i < self->templates->len; i++)
    {
        Template *t = g_ptr_array_index (self->templates, i);
        gboolean seen = FALSE;
        guint j;

        for (j = 0; j < names->len && !seen; j++)
            seen = g_strcmp0 (g_ptr_array_index (names, j), t->name) == 0;
        if (!seen)
            g_ptr_array_add (names, g_strdup (t->name));
    }
    g_ptr_array_add (names, NULL);
    return (gchar **)g_ptr_array_free (g_steal_pointer (&names), FALSE);
}

const gfloat *
lrg_vr_gesture_recognizer_get_template (LrgVRGestureRecognizer *self,
                                        const gchar            *name,
                                        guint                  *n_points)
{
    guint i;

    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), NULL);
    g_return_val_if_fail (name != NULL && n_points != NULL, NULL);

    *n_points = 0;
    for (i = 0; i < self->templates->len; i++)
    {
        Template *t = g_ptr_array_index (self->templates, i);

        if (g_strcmp0 (t->name, name) == 0)
        {
            *n_points = N_SAMPLES;
            return t->points;
        }
    }
    return NULL;
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

gfloat
lrg_vr_gesture_recognizer_get_threshold (LrgVRGestureRecognizer *self)
{
    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), 0.0f);

    return self->threshold;
}

void
lrg_vr_gesture_recognizer_set_threshold (LrgVRGestureRecognizer *self,
                                         gfloat                  threshold)
{
    g_return_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self));

    self->threshold = CLAMP (threshold, 0.0f, 1.0f);
}

gfloat
lrg_vr_gesture_recognizer_get_min_size (LrgVRGestureRecognizer *self)
{
    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), 0.0f);

    return self->min_size;
}

void
lrg_vr_gesture_recognizer_set_min_size (LrgVRGestureRecognizer *self,
                                        gfloat                  metres)
{
    g_return_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self));

    self->min_size = MAX (metres, 0.0f);
}

/* ==========================================================================
 * Drawing
 * ========================================================================== */

void
lrg_vr_gesture_recognizer_begin (LrgVRGestureRecognizer *self,
                                 const gfloat           *head)
{
    gfloat len;

    g_return_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self));

    /* the head's right, flattened (no roll, no pitch); up is the space's;
       away is up x right (OpenXR: -Z forward) */
    self->right[0] = 1.0f;
    self->right[1] = 0.0f;
    self->right[2] = 0.0f;
    if (head != NULL)
    {
        self->right[0] = head[0];
        self->right[2] = head[2];
        len = hypotf (self->right[0], self->right[2]);
        if (len > 1e-4f)
        {
            self->right[0] /= len;
            self->right[2] /= len;
        }
        else
        {
            /* the head on its side: its up flattened is still sideways */
            self->right[0] = 1.0f;
            self->right[2] = 0.0f;
        }
    }
    self->up[0] = 0.0f;
    self->up[1] = 1.0f;
    self->up[2] = 0.0f;
    self->forward[0] = self->up[1] * self->right[2] - self->up[2] * self->right[1];
    self->forward[1] = self->up[2] * self->right[0] - self->up[0] * self->right[2];
    self->forward[2] = self->up[0] * self->right[1] - self->up[1] * self->right[0];
    g_array_set_size (self->points, 0);
    self->drawing = TRUE;
}

void
lrg_vr_gesture_recognizer_add_point (LrgVRGestureRecognizer *self,
                                     const gfloat           *position,
                                     gdouble                 time)
{
    g_return_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self));
    g_return_if_fail (position != NULL);

    if (!self->drawing)
        return;
    if (self->points->len == 0)
        self->first_time = time;
    self->last_time = time;
    g_array_append_vals (self->points, position, 3);
}

gboolean
lrg_vr_gesture_recognizer_is_drawing (LrgVRGestureRecognizer *self)
{
    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), FALSE);

    return self->drawing;
}

const gfloat *
lrg_vr_gesture_recognizer_get_points (LrgVRGestureRecognizer *self,
                                      guint                  *n_points)
{
    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), NULL);
    g_return_val_if_fail (n_points != NULL, NULL);

    *n_points = self->points->len / 3;
    return *n_points > 0 ? (const gfloat *)self->points->data : NULL;
}

void
lrg_vr_gesture_recognizer_cancel (LrgVRGestureRecognizer *self)
{
    g_return_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self));

    self->drawing = FALSE;
    g_array_set_size (self->points, 0);
}

LrgVRGesture *
lrg_vr_gesture_recognizer_recognize (LrgVRGestureRecognizer *self,
                                     const gfloat           *points,
                                     guint                   n_points)
{
    LrgVRGesture *out;
    gfloat drawing[N_SAMPLES * 2];
    const Template *best;
    gfloat best_distance;
    gfloat runner_up;
    guint i;

    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), NULL);
    g_return_val_if_fail (points != NULL || n_points == 0, NULL);

    out = g_new0 (LrgVRGesture, 1);
    out->length = path_length (points, n_points);
    if (n_points < 2 || !resample (points, n_points, drawing) || !normalize (drawing))
        return out;

    /* the nearest template, and the nearest of another name */
    best = NULL;
    best_distance = G_MAXFLOAT;
    runner_up = G_MAXFLOAT;
    for (i = 0; i < self->templates->len; i++)
    {
        const Template *t = g_ptr_array_index (self->templates, i);
        gfloat d = template_distance (t, drawing);

        if (d < best_distance)
        {
            if (best != NULL && g_strcmp0 (best->name, t->name) != 0)
                runner_up = best_distance;
            best_distance = d;
            best = t;
        }
        else if (d < runner_up && g_strcmp0 (best->name, t->name) != 0)
        {
            runner_up = d;
        }
    }
    if (best == NULL)
        return out;
    out->score = score_of (best_distance);
    if (out->score >= self->threshold &&
        (runner_up == G_MAXFLOAT || score_of (runner_up) <= out->score - AMBIGUITY))
        out->name = g_strdup (best->name);
    return out;
}

LrgVRGesture *
lrg_vr_gesture_recognizer_end (LrgVRGestureRecognizer *self)
{
    g_autofree gfloat *flat = NULL;
    LrgVRGesture *out;
    const gfloat *p;
    gfloat min_x = G_MAXFLOAT;
    gfloat max_x = -G_MAXFLOAT;
    gfloat min_y = G_MAXFLOAT;
    gfloat max_y = -G_MAXFLOAT;
    gfloat length = 0.0f;
    gfloat spread;
    gfloat depth;
    guint n;
    guint i;

    g_return_val_if_fail (LRG_IS_VR_GESTURE_RECOGNIZER (self), NULL);

    n = self->points->len / 3;
    if (!self->drawing || n < 2)
    {
        lrg_vr_gesture_recognizer_cancel (self);
        return g_new0 (LrgVRGesture, 1);
    }

    /* the drawing in the view plane (from its first point) and its depth */
    p = (const gfloat *)self->points->data;
    flat = g_new (gfloat, n * 2);
    for (i = 0; i < n; i++)
    {
        gfloat d[3];
        gint k;

        for (k = 0; k < 3; k++)
            d[k] = p[i * 3 + k] - p[k];
        flat[i * 2] = d[0] * self->right[0] + d[1] * self->right[1] + d[2] * self->right[2];
        flat[i * 2 + 1] = d[0] * self->up[0] + d[1] * self->up[1] + d[2] * self->up[2];
        min_x = MIN (min_x, flat[i * 2]);
        max_x = MAX (max_x, flat[i * 2]);
        min_y = MIN (min_y, flat[i * 2 + 1]);
        max_y = MAX (max_y, flat[i * 2 + 1]);
        if (i > 0)
            length += sqrtf ((p[i * 3] - p[(i - 1) * 3]) * (p[i * 3] - p[(i - 1) * 3]) +
                             (p[i * 3 + 1] - p[(i - 1) * 3 + 1]) * (p[i * 3 + 1] - p[(i - 1) * 3 + 1]) +
                             (p[i * 3 + 2] - p[(i - 1) * 3 + 2]) * (p[i * 3 + 2] - p[(i - 1) * 3 + 2]));
    }
    depth = 0.0f;
    for (i = 0; i < 3; i++)
        depth += (p[(n - 1) * 3 + i] - p[i]) * self->forward[i];
    spread = MAX (max_x - min_x, max_y - min_y);

    /* a thrust: mostly away from (or towards) the head; otherwise the
       shape in the plane, when it is large enough to be one */
    if (fabsf (depth) >= self->min_size * THRUST_MIN && fabsf (depth) >= spread * THRUST_RATIO)
    {
        out = g_new0 (LrgVRGesture, 1);
        out->name = g_strdup (depth > 0.0f ? LRG_VR_GESTURE_PUSH : LRG_VR_GESTURE_PULL);
        out->score = CLAMP (1.0f - spread / fabsf (depth), 0.0f, 1.0f);
    }
    else if (spread >= self->min_size)
    {
        out = lrg_vr_gesture_recognizer_recognize (self, flat, n);
    }
    else
    {
        out = g_new0 (LrgVRGesture, 1);
    }
    out->length = length;
    out->duration = (gfloat)(self->last_time - self->first_time);
    out->width = max_x - min_x;
    out->height = max_y - min_y;
    out->depth = depth;
    lrg_vr_gesture_recognizer_cancel (self);
    return out;
}
