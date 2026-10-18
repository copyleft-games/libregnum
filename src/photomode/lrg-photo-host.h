/* lrg-photo-host.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhotoHost - what photo mode needs from the game: freeze the
 * world, hide the HUD, override the time of day and the weather.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_PHOTO_HOST (lrg_photo_host_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgPhotoHost, lrg_photo_host, LRG, PHOTO_HOST, GObject)

/**
 * LrgPhotoHostInterface:
 * @parent_iface: the parent interface
 * @freeze_world: stops (%TRUE) or resumes (%FALSE) the simulation; return
 *   whether the world really is frozen afterwards
 * @set_hud_visible: shows or hides the game's HUD
 * @get_time_of_day: reads the game's hour of day (0 to 24); return %FALSE
 *   when the game has no clock
 * @set_time_of_day: overrides the hour of day
 * @clear_time_of_day: puts the clock back as it was
 * @get_weather: the current weather's id, or %NULL
 * @list_weathers: the ids photo mode may choose from, %NULL terminated, or
 *   %NULL when the weather cannot be overridden
 * @set_weather: overrides the weather
 * @clear_weather: puts the weather back as it was
 * @get_anchor: the point the camera should stay near (the player), as
 *   x, y, z; return %FALSE when there is none
 * @describe: adds the game's own text entries (title, version, location)
 *   to the table that becomes the photo's metadata
 *
 * The game's side of photo mode.  Every method is optional: photo
 * mode skips what the host does not implement, so a tiny game
 * implements only freeze_world.
 *
 * Since: 1.0
 */
struct _LrgPhotoHostInterface
{
    GTypeInterface parent_iface;

    gboolean            (*freeze_world)     (LrgPhotoHost *self,
                                             gboolean      freeze);
    void                (*set_hud_visible)  (LrgPhotoHost *self,
                                             gboolean      visible);
    gboolean            (*get_time_of_day)  (LrgPhotoHost *self,
                                             gfloat       *hour);
    void                (*set_time_of_day)  (LrgPhotoHost *self,
                                             gfloat        hour);
    void                (*clear_time_of_day)(LrgPhotoHost *self);
    const gchar *       (*get_weather)      (LrgPhotoHost *self);
    const gchar * const * (*list_weathers)  (LrgPhotoHost *self);
    void                (*set_weather)      (LrgPhotoHost *self,
                                             const gchar  *weather);
    void                (*clear_weather)    (LrgPhotoHost *self);
    gboolean            (*get_anchor)       (LrgPhotoHost *self,
                                             gfloat       *xyz);
    void                (*describe)         (LrgPhotoHost *self,
                                             GHashTable   *metadata);
};

/**
 * lrg_photo_host_freeze_world:
 * @self: an #LrgPhotoHost
 * @freeze: %TRUE to stop the world, %FALSE to resume it
 *
 * Returns: whether the world is frozen now (%FALSE when the host cannot)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_host_freeze_world (LrgPhotoHost *self,
                             gboolean      freeze);

/**
 * lrg_photo_host_set_hud_visible:
 * @self: an #LrgPhotoHost
 * @visible: whether the game's HUD is drawn
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_host_set_hud_visible (LrgPhotoHost *self,
                                gboolean      visible);

/**
 * lrg_photo_host_get_time_of_day:
 * @self: an #LrgPhotoHost
 * @hour: (out): the hour, 0.0 to 24.0
 *
 * Returns: %FALSE when the game has no clock
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_host_get_time_of_day (LrgPhotoHost *self,
                                gfloat       *hour);

/**
 * lrg_photo_host_set_time_of_day:
 * @self: an #LrgPhotoHost
 * @hour: the hour, 0.0 to 24.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_host_set_time_of_day (LrgPhotoHost *self,
                                gfloat        hour);

/**
 * lrg_photo_host_clear_time_of_day:
 * @self: an #LrgPhotoHost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_host_clear_time_of_day (LrgPhotoHost *self);

/**
 * lrg_photo_host_get_weather:
 * @self: an #LrgPhotoHost
 *
 * Returns: (transfer none) (nullable): the current weather id
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_photo_host_get_weather (LrgPhotoHost *self);

/**
 * lrg_photo_host_list_weathers:
 * @self: an #LrgPhotoHost
 *
 * Returns: (transfer none) (nullable) (array zero-terminated=1): the
 *   weather ids, or %NULL when the host cannot override the weather
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * const *
lrg_photo_host_list_weathers (LrgPhotoHost *self);

/**
 * lrg_photo_host_set_weather:
 * @self: an #LrgPhotoHost
 * @weather: a weather id
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_host_set_weather (LrgPhotoHost *self,
                            const gchar  *weather);

/**
 * lrg_photo_host_clear_weather:
 * @self: an #LrgPhotoHost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_host_clear_weather (LrgPhotoHost *self);

/**
 * lrg_photo_host_get_anchor:
 * @self: an #LrgPhotoHost
 * @xyz: (out caller-allocates) (array fixed-size=3): the point
 *
 * Returns: %FALSE when the host has no anchor point
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_photo_host_get_anchor (LrgPhotoHost *self,
                           gfloat       *xyz);

/**
 * lrg_photo_host_describe:
 * @self: an #LrgPhotoHost
 * @metadata: (element-type utf8 utf8): the table to add entries to
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_photo_host_describe (LrgPhotoHost *self,
                         GHashTable   *metadata);

G_END_DECLS
