/* lrg-vr-gesture.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shapes drawn in the air with a controller: a circle, a zigzag, a flick
 * up, a thrust forward.  #LrgVRGestureRecognizer records a hand's path
 * between a begin and an end (a button held), flattens it onto the plane
 * the head faced when it began, and matches it against named templates
 * the way the $1 unistroke recognizer does (resampled, centred, scaled;
 * the drawing's direction and orientation count).  Thrusts towards and
 * away from the head are told apart by their depth.
 *
 * Nothing here touches a runtime: positions go in, a name comes out, so
 * every backend and the tests share it.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_VR_GESTURE_PUSH:
 *
 * The name of a thrust away from the head (lrg_vr_gesture_recognizer_end()).
 */
#define LRG_VR_GESTURE_PUSH "push"

/**
 * LRG_VR_GESTURE_PULL:
 *
 * The name of a pull back towards the head.
 */
#define LRG_VR_GESTURE_PULL "pull"

/**
 * LRG_VR_GESTURE_SAMPLES:
 *
 * The points a drawing and every template are resampled to: the length
 * of lrg_vr_gesture_recognizer_get_template()'s pairs and
 * lrg_vr_gesture_recognizer_fit_template()'s triples.
 */
#define LRG_VR_GESTURE_SAMPLES 48

/**
 * LrgVRGesture:
 * @name: (nullable): the template matched, %LRG_VR_GESTURE_PUSH or
 *   %LRG_VR_GESTURE_PULL; %NULL when nothing was recognised
 * @score: how well it matched, 0 (not at all) to 1 (exactly)
 * @length: metres the hand travelled
 * @duration: seconds from the first sample to the last
 * @width: metres the drawing spans sideways (in the view plane)
 * @height: metres it spans up and down
 * @depth: metres it moved away from the head (negative: towards)
 * @nearest: (nullable): the closest template's name, whether or not it
 *   reached the threshold (%NULL without templates or a drawing to
 *   match; a push or a pull names itself): what a drawing was most like,
 *   for a hint
 *
 * One recognised (or unrecognised) drawing.
 *
 * Since: 1.0
 */
typedef struct
{
    gchar  *name;
    gfloat  score;
    gfloat  length;
    gfloat  duration;
    gfloat  width;
    gfloat  height;
    gfloat  depth;
    gchar  *nearest;
} LrgVRGesture;

#define LRG_TYPE_VR_GESTURE (lrg_vr_gesture_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_vr_gesture_get_type (void) G_GNUC_CONST;

/**
 * lrg_vr_gesture_copy:
 * @self: a #LrgVRGesture
 *
 * Returns: (transfer full): a copy of @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRGesture * lrg_vr_gesture_copy (const LrgVRGesture *self);

/**
 * lrg_vr_gesture_free:
 * @self: (nullable): a #LrgVRGesture
 *
 * Frees @self and its name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_gesture_free (LrgVRGesture *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgVRGesture, lrg_vr_gesture_free)

#define LRG_TYPE_VR_GESTURE_RECOGNIZER (lrg_vr_gesture_recognizer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVRGestureRecognizer, lrg_vr_gesture_recognizer, LRG, VR_GESTURE_RECOGNIZER, GObject)

/**
 * lrg_vr_gesture_recognizer_new:
 *
 * Creates a recognizer without templates (see
 * lrg_vr_gesture_recognizer_add_builtin_templates()).  Pushes and pulls
 * are always recognised.
 *
 * Returns: (transfer full): a new #LrgVRGestureRecognizer
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRGestureRecognizer * lrg_vr_gesture_recognizer_new (void);

/**
 * lrg_vr_gesture_recognizer_add_template:
 * @self: a #LrgVRGestureRecognizer
 * @name: the shape's name (several templates may share one: the ways
 *   people draw it)
 * @points: (array length=n_points): x, y pairs (y up, any unit), in the
 *   order they are drawn
 * @n_points: the number of pairs, at least 2
 * @closed: whether the shape ends where it starts (a circle, a square):
 *   then a drawing that starts anywhere along it matches
 *
 * Adds a shape to recognise.
 *
 * Returns: %FALSE (and nothing added) when the points span no distance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_gesture_recognizer_add_template (LrgVRGestureRecognizer *self,
                                                 const gchar            *name,
                                                 const gfloat           *points,
                                                 guint                   n_points,
                                                 gboolean                closed);

/**
 * lrg_vr_gesture_recognizer_add_builtin_templates:
 * @self: a #LrgVRGestureRecognizer
 *
 * Adds the shapes every app can count on: "line-up", "line-down",
 * "line-left", "line-right" (straight strokes), "circle" (either way
 * round), "zigzag" (a Z, or a lightning bolt drawn downwards), "v" (a V
 * or a tick), "caret" (an upside-down V), "triangle" and "square" (either
 * way round).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_gesture_recognizer_add_builtin_templates (LrgVRGestureRecognizer *self);

/**
 * lrg_vr_gesture_recognizer_remove_template:
 * @self: a #LrgVRGestureRecognizer
 * @name: a shape's name
 *
 * Removes every template named @name.
 *
 * Returns: how many were removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vr_gesture_recognizer_remove_template (LrgVRGestureRecognizer *self,
                                                 const gchar            *name);

/**
 * lrg_vr_gesture_recognizer_clear_templates:
 * @self: a #LrgVRGestureRecognizer
 *
 * Removes every template.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_gesture_recognizer_clear_templates (LrgVRGestureRecognizer *self);

/**
 * lrg_vr_gesture_recognizer_dup_names:
 * @self: a #LrgVRGestureRecognizer
 *
 * Returns: (transfer full) (array zero-terminated=1): the templates'
 *   names, each once, in the order they were added
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_vr_gesture_recognizer_dup_names (LrgVRGestureRecognizer *self);

/**
 * lrg_vr_gesture_recognizer_get_template:
 * @self: a #LrgVRGestureRecognizer
 * @name: a shape's name
 * @n_points: (out): the number of x, y pairs
 *
 * The first template named @name as the recognizer keeps it: resampled,
 * centred on 0, its larger side 1 long (a stroke to replay or to show).
 *
 * Returns: (transfer none) (array length=n_points) (nullable): its
 *   x, y pairs, %NULL without a template of that name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_vr_gesture_recognizer_get_template (LrgVRGestureRecognizer *self,
                                                       const gchar            *name,
                                                       guint                  *n_points);

/**
 * lrg_vr_gesture_recognizer_get_threshold:
 * @self: a #LrgVRGestureRecognizer
 *
 * Returns: the score below which a drawing is not recognised
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_gesture_recognizer_get_threshold (LrgVRGestureRecognizer *self);

/**
 * lrg_vr_gesture_recognizer_set_threshold:
 * @self: a #LrgVRGestureRecognizer
 * @threshold: 0 .. 1 (default 0.75): lower forgives sloppier drawing and
 *   confuses more shapes
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_gesture_recognizer_set_threshold (LrgVRGestureRecognizer *self,
                                              gfloat                  threshold);

/**
 * lrg_vr_gesture_recognizer_get_min_size:
 * @self: a #LrgVRGestureRecognizer
 *
 * Returns: metres a drawing must span to be a gesture
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_gesture_recognizer_get_min_size (LrgVRGestureRecognizer *self);

/**
 * lrg_vr_gesture_recognizer_set_min_size:
 * @self: a #LrgVRGestureRecognizer
 * @metres: the smallest span (its larger side, or its depth for a push)
 *   that counts (default 0.1); smaller drawings are no gesture, so a
 *   button pressed and let go is not mistaken for one
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_gesture_recognizer_set_min_size (LrgVRGestureRecognizer *self,
                                             gfloat                  metres);

/**
 * lrg_vr_gesture_recognizer_begin:
 * @self: a #LrgVRGestureRecognizer
 * @head: (array fixed-size=16) (nullable): the head's pose (column-major,
 *   in the space the points will be in, Y up, -Z forward); %NULL looks
 *   along -Z
 *
 * Starts a drawing, dropping one in progress.  The view plane is the
 * head's at this moment, turned upright (the head's roll and pitch do not
 * tilt the shapes).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_gesture_recognizer_begin (LrgVRGestureRecognizer *self,
                                      const gfloat           *head);

/**
 * lrg_vr_gesture_recognizer_add_point:
 * @self: a #LrgVRGestureRecognizer
 * @position: (array fixed-size=3): the hand, in metres
 * @time: seconds (any clock, increasing)
 *
 * Adds a sample of the drawing (ignored when none is in progress).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_gesture_recognizer_add_point (LrgVRGestureRecognizer *self,
                                          const gfloat           *position,
                                          gdouble                 time);

/**
 * lrg_vr_gesture_recognizer_is_drawing:
 * @self: a #LrgVRGestureRecognizer
 *
 * Returns: whether a drawing is in progress
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_gesture_recognizer_is_drawing (LrgVRGestureRecognizer *self);

/**
 * lrg_vr_gesture_recognizer_get_points:
 * @self: a #LrgVRGestureRecognizer
 * @n_points: (out): the number of samples
 *
 * The drawing so far (for a trail to show), as added.
 *
 * Returns: (transfer none) (array length=n_points) (nullable): x, y, z
 *   triples, valid until the next call on @self
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gfloat * lrg_vr_gesture_recognizer_get_points (LrgVRGestureRecognizer *self,
                                                     guint                  *n_points);

/**
 * lrg_vr_gesture_recognizer_end:
 * @self: a #LrgVRGestureRecognizer
 *
 * Ends the drawing and names it: a push or a pull when it went mostly
 * away from or towards the head, else the best template at or above the
 * threshold (and clearly better than the best of any other name: a
 * drawing as near a circle as a square is neither).
 *
 * Returns: (transfer full): what was drawn (its name %NULL when nothing:
 *   too small, no template close enough, or no drawing in progress)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRGesture * lrg_vr_gesture_recognizer_end (LrgVRGestureRecognizer *self);

/**
 * lrg_vr_gesture_recognizer_peek:
 * @self: a #LrgVRGestureRecognizer
 *
 * Names the drawing so far as lrg_vr_gesture_recognizer_end() would,
 * without ending it: a live guess to show while the hand still draws.
 *
 * Returns: (transfer full): what the drawing is so far (its name %NULL
 *   when it would be nothing yet; @nearest still says what it is most
 *   like)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRGesture * lrg_vr_gesture_recognizer_peek (LrgVRGestureRecognizer *self);

/**
 * lrg_vr_gesture_recognizer_get_frame:
 * @self: a #LrgVRGestureRecognizer
 * @out_centre: (out caller-allocates) (array fixed-size=3): the middle of
 *   the drawing's box in the view plane (in the points' space)
 * @out_right: (out caller-allocates) (array fixed-size=3): the plane's
 *   sideways axis, unit
 * @out_up: (out caller-allocates) (array fixed-size=3): its up axis, unit
 * @out_size: (out) (optional): the box's larger side, metres
 *
 * Where the drawing in progress is: to put something round it or over it.
 *
 * Returns: %FALSE (nothing written) without a drawing of two points
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_gesture_recognizer_get_frame (LrgVRGestureRecognizer *self,
                                              gfloat                 *out_centre,
                                              gfloat                 *out_right,
                                              gfloat                 *out_up,
                                              gfloat                 *out_size);

/**
 * lrg_vr_gesture_recognizer_fit_template:
 * @self: a #LrgVRGestureRecognizer
 * @name: a shape's name
 * @out_points: (out caller-allocates) (array fixed-size=144): room for
 *   %LRG_VR_GESTURE_SAMPLES x, y, z triples
 *
 * The shape @name as the drawing in progress would match it, laid over
 * the drawing: of its templates the nearest (the way round, the start of
 * a closed one), centred and scaled as the drawing is, in the drawing's
 * plane and space.  A guide to show what the hand is being read as.
 *
 * Returns: %FALSE (nothing written) without a drawing to fit to or a
 *   template of that name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vr_gesture_recognizer_fit_template (LrgVRGestureRecognizer *self,
                                                 const gchar            *name,
                                                 gfloat                 *out_points);

/**
 * lrg_vr_gesture_recognizer_score_template:
 * @self: a #LrgVRGestureRecognizer
 * @name: a shape's name, or %LRG_VR_GESTURE_PUSH / %LRG_VR_GESTURE_PULL
 *
 * How well the drawing in progress matches the shape @name, whatever it
 * is nearest: the score lrg_vr_gesture_recognizer_end() would give it
 * were @name the only shape known (a thrust: how straight it goes, the
 * right way, 0 the wrong way or not deep enough to be one).  For a
 * practice of a shape: how close the hand came to the one asked for,
 * even when it read as another.
 *
 * Returns: 0 .. 1, or -1 without a drawing of two points or a shape of
 *   that name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vr_gesture_recognizer_score_template (LrgVRGestureRecognizer *self,
                                                 const gchar            *name);

/**
 * lrg_vr_gesture_recognizer_cancel:
 * @self: a #LrgVRGestureRecognizer
 *
 * Drops the drawing in progress.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_vr_gesture_recognizer_cancel (LrgVRGestureRecognizer *self);

/**
 * lrg_vr_gesture_recognizer_recognize:
 * @self: a #LrgVRGestureRecognizer
 * @points: (array length=n_points): x, y pairs (y up) of a flat drawing
 * @n_points: the number of pairs
 *
 * Matches a flat drawing against the templates (no push or pull, no
 * minimum size: the shape alone).
 *
 * Returns: (transfer full): the best match (its name %NULL below the
 *   threshold)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgVRGesture * lrg_vr_gesture_recognizer_recognize (LrgVRGestureRecognizer *self,
                                                    const gfloat           *points,
                                                    guint                   n_points);

G_END_DECLS
