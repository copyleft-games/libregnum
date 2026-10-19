/* lrg-controller-rumble.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Controller vibration through Linux force feedback (evdev).  Raylib and
 * GLFW have no rumble API, so a game that wants vibration on Linux talks
 * to the kernel's input device of the pad directly.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_CONTROLLER_RUMBLE (lrg_controller_rumble_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgControllerRumble, lrg_controller_rumble, LRG, CONTROLLER_RUMBLE, GObject)

/**
 * LrgControllerRumble:
 *
 * Plays rumble effects on one controller.  The kernel's input device is
 * found by the controller's name (the one raylib reports) under
 * class/input in sysfs, then opened under /dev/input; it is opened on the
 * first lrg_controller_rumble_play() and closed on dispose.  Everything
 * fails soft: without a device, without force feedback support, without
 * permission or off Linux, play returns %FALSE and the game goes on
 * without vibration.
 *
 * Pads Steam Input owns (a Steam Deck or Steam Frame in Steam's game
 * mode) take their rumble from Steam, not from here.
 */

/**
 * lrg_controller_rumble_new:
 * @name: the controller name (see lrg_input_get_gamepad_name())
 *
 * Returns: (transfer full): a new #LrgControllerRumble
 */
LRG_AVAILABLE_IN_ALL
LrgControllerRumble * lrg_controller_rumble_new (const gchar *name);

/**
 * lrg_controller_rumble_new_with_roots:
 * @name: the controller name
 * @sysfs_root: (nullable): the sysfs mount (default "/sys")
 * @dev_root: (nullable): the directory with the eventN nodes (default
 *   "/dev/input")
 *
 * lrg_controller_rumble_new() against other trees (tests).
 *
 * Returns: (transfer full): a new #LrgControllerRumble
 */
LRG_AVAILABLE_IN_ALL
LrgControllerRumble * lrg_controller_rumble_new_with_roots (const gchar *name,
                                                            const gchar *sysfs_root,
                                                            const gchar *dev_root);

/**
 * lrg_controller_rumble_find_node:
 * @self: an #LrgControllerRumble
 *
 * Finds the event node of the pad without opening it.
 *
 * Returns: (transfer full) (nullable): the node path such as
 *   "/dev/input/event12", %NULL when no input device carries the name
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_controller_rumble_find_node (LrgControllerRumble *self);

/**
 * lrg_controller_rumble_play:
 * @self: an #LrgControllerRumble
 * @strong: the strong (low frequency) motor, 0.0 to 1.0
 * @weak: the weak (high frequency) motor, 0.0 to 1.0
 * @duration_ms: how long to run, in milliseconds (at most 65535)
 * @error: return location for a #GError
 *
 * Starts a rumble effect, replacing the one running.
 *
 * Returns: %TRUE when the effect was started
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_rumble_play (LrgControllerRumble  *self,
                                     gdouble               strong,
                                     gdouble               weak,
                                     guint                 duration_ms,
                                     GError              **error);

/**
 * lrg_controller_rumble_stop:
 * @self: an #LrgControllerRumble
 *
 * Stops the running effect (a no-op when none runs).
 */
LRG_AVAILABLE_IN_ALL
void lrg_controller_rumble_stop (LrgControllerRumble *self);

/**
 * lrg_controller_rumble_is_open:
 * @self: an #LrgControllerRumble
 *
 * Returns: %TRUE when the device is open and supports rumble
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_rumble_is_open (LrgControllerRumble *self);

G_END_DECLS
