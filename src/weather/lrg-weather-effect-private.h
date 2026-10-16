/* lrg-weather-effect-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal helpers shared by the weather effects: wrapping particles
 * into the camera volume and emitting camera-facing ribbons and sprites
 * through rlgl's immediate batch. Not installed, not scanned for
 * introspection.
 */

#pragma once

#include <glib-object.h>
#include "weather/lrg-weather-effect.h"

G_BEGIN_DECLS

/* Default %LRG_WEATHER_SPACE_WORLD volume: half width and full height,
 * world units (metres in most games). */
#define LRG_WEATHER_DEFAULT_VOLUME_RADIUS (15.0f)
#define LRG_WEATHER_DEFAULT_VOLUME_HEIGHT (12.0f)

/*
 * lrg_weather_wrap:
 *
 * Wraps @value into [centre - extent / 2, centre + extent / 2), the
 * repeat of a particle in the camera's box. Non-finite input or a
 * non-positive @extent returns @centre.
 */
G_GNUC_INTERNAL
gfloat
lrg_weather_wrap (gfloat value,
                  gfloat centre,
                  gfloat extent);

/*
 * lrg_weather_can_draw:
 *
 * TRUE when a raylib window (GL context) exists. Every render path
 * checks it so headless programs and tests can call render functions.
 */
G_GNUC_INTERNAL
gboolean
lrg_weather_can_draw (void);

/*
 * lrg_weather_begin_particles:
 *
 * Flushes pending geometry, then sets the state for translucent
 * particles: no face culling, depth test on, depth writes off, the
 * default texture; opens an RL_TRIANGLES batch. End with
 * lrg_weather_end_particles(), which flushes and restores culling and
 * depth writes (raylib's 3D defaults).
 */
G_GNUC_INTERNAL
void
lrg_weather_begin_particles (void);

G_GNUC_INTERNAL
void
lrg_weather_end_particles (void);

/*
 * lrg_weather_camera_basis:
 *
 * The camera's right and up vectors in world space, read from the
 * current modelview matrix (valid inside 3D mode).
 */
G_GNUC_INTERNAL
void
lrg_weather_camera_basis (gfloat *right,
                          gfloat *up);

/*
 * lrg_weather_emit_ribbon:
 *
 * Emits a segment @a -> @b of @width as a quad turned toward @eye (two
 * triangles), coloured @color_a at @a and @color_b at @b, so a streak
 * can fade along its length. Between begin/end_particles().
 */
G_GNUC_INTERNAL
void
lrg_weather_emit_ribbon (const gfloat *a,
                         const gfloat *b,
                         gfloat        width,
                         const gfloat *eye,
                         const guint8 *color_a,
                         const guint8 *color_b);

/*
 * lrg_weather_emit_sprite:
 *
 * Emits a square of side @size centred on @centre spanned by the camera
 * basis (lrg_weather_camera_basis()). Between begin/end_particles().
 */
G_GNUC_INTERNAL
void
lrg_weather_emit_sprite (const gfloat *centre,
                         gfloat        size,
                         const gfloat *right,
                         const gfloat *up,
                         const guint8 *color);

G_END_DECLS
