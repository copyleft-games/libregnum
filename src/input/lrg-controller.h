/* lrg-controller.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Controller families: detection from the system's controller name and the
 * USB ids, button glyph names per family, preset input maps per family and
 * the LrgControllerMonitor that watches one gamepad slot and tells a game
 * when the family changes (a pad plugged in, swapped or removed).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-input.h"
#include "lrg-input-map.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Valve ids
 * ========================================================================== */

/**
 * LRG_CONTROLLER_VENDOR_VALVE:
 *
 * The USB vendor id of Valve (0x28de): Steam Controller, Steam Deck, the
 * Steam Virtual Gamepad and, by name, the Steam Frame's controllers.
 */
#define LRG_CONTROLLER_VENDOR_VALVE (0x28de)

/**
 * LRG_CONTROLLER_ENV_FAMILY:
 *
 * The environment variable ("LRG_CONTROLLER_FAMILY") that forces the family
 * a #LrgControllerMonitor reports, by nick (for example "steam-frame").  It
 * is the escape hatch for hardware the name and id tables do not know yet.
 */
#define LRG_CONTROLLER_ENV_FAMILY "LRG_CONTROLLER_FAMILY"

/* ==========================================================================
 * Family detection
 * ========================================================================== */

/**
 * lrg_controller_family_detect:
 * @name: (nullable): the controller name the system reports
 * @vendor: the USB vendor id, 0 when unknown
 * @product: the USB product id, 0 when unknown
 * @steam_deck_host: %TRUE when the game runs on a Steam Deck
 *
 * Works out the family of a connected controller.  The ids are tried first
 * (exact and reliable), then the name (case-insensitive substrings).  On a
 * Steam Deck host (see lrg_controller_is_steam_deck_host()) a pad that is
 * only the generic or Xbox 360 shaped virtual pad Steam Input presents for
 * every controller is reported as %LRG_CONTROLLER_FAMILY_STEAM_DECK, because
 * those are the Deck's own controls.  A pad nothing matches is
 * %LRG_CONTROLLER_FAMILY_GENERIC, never %LRG_CONTROLLER_FAMILY_UNKNOWN (that
 * means "nothing connected" and is the caller's to report).
 *
 * The Steam Frame's controllers are matched by name ("steam frame") and by
 * Valve's vendor id with a "frame" name; their product ids are not
 * documented anywhere this library could rely on.
 *
 * Returns: the family
 */
LRG_AVAILABLE_IN_ALL
LrgControllerFamily lrg_controller_family_detect (const gchar *name,
                                                  guint16      vendor,
                                                  guint16      product,
                                                  gboolean     steam_deck_host);

/**
 * lrg_controller_family_get_nick:
 * @family: a #LrgControllerFamily
 *
 * Returns: (transfer none): the stable nick ("xbox-series", "dualsense",
 *   "steam-deck", ...)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_controller_family_get_nick (LrgControllerFamily family);

/**
 * lrg_controller_family_get_display_name:
 * @family: a #LrgControllerFamily
 *
 * Returns: (transfer none): a human readable name ("Xbox Series controller")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_controller_family_get_display_name (LrgControllerFamily family);

/**
 * lrg_controller_family_from_nick:
 * @nick: a nick as lrg_controller_family_get_nick() returns it
 * @family: (out): the family
 *
 * Returns: %TRUE when @nick names a family
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_family_from_nick (const gchar         *nick,
                                          LrgControllerFamily *family);

/**
 * lrg_controller_family_get_gamepad_type:
 * @family: a #LrgControllerFamily
 *
 * The coarse #LrgGamepadType of a family (all Xbox families are
 * %LRG_GAMEPAD_TYPE_XBOX, the Steam families %LRG_GAMEPAD_TYPE_STEAM_DECK).
 *
 * Returns: the grouping
 */
LRG_AVAILABLE_IN_ALL
LrgGamepadType lrg_controller_family_get_gamepad_type (LrgControllerFamily family);

/**
 * lrg_controller_family_is_steam:
 * @family: a #LrgControllerFamily
 *
 * Returns: %TRUE for the Steam Controller, Deck and Frame families (whose
 *   back grips and on-screen keyboard hint a game may want to treat alike)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_family_is_steam (LrgControllerFamily family);

/**
 * lrg_controller_is_steam_deck_host:
 *
 * Whether this machine is a Steam Deck: the SteamDeck environment variable
 * is "1" (Steam sets it for games on the Deck), or the DMI product name is
 * "Jupiter" or "Galileo" (the LCD and OLED Deck) with board vendor Valve.
 *
 * Returns: %TRUE on a Steam Deck
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_is_steam_deck_host (void);

/**
 * lrg_controller_is_steam_deck_host_at:
 * @sysfs_root: (nullable): the directory holding class/input and
 *   devices/virtual/dmi (default "/sys")
 *
 * lrg_controller_is_steam_deck_host() against another sysfs tree (tests).
 *
 * Returns: %TRUE on a Steam Deck
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_is_steam_deck_host_at (const gchar *sysfs_root);

/**
 * lrg_controller_probe_usb_ids:
 * @name: the controller name the system reports
 * @sysfs_root: (nullable): the sysfs mount (default "/sys")
 * @vendor: (out) (optional): the USB vendor id
 * @product: (out) (optional): the USB product id
 *
 * Finds the Linux input device called @name under
 * @sysfs_root/class/input/input*, and reads its id/vendor and id/product.
 * Works for USB, Bluetooth and Steam Input pads alike, whatever index
 * raylib gave them.  Elsewhere (no such directory) it fails.
 *
 * Returns: %TRUE when a device of that name with ids was found
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_probe_usb_ids (const gchar *name,
                                       const gchar *sysfs_root,
                                       guint16     *vendor,
                                       guint16     *product);

/* ==========================================================================
 * Glyphs
 * ========================================================================== */

/**
 * lrg_controller_has_control:
 * @family: a #LrgControllerFamily
 * @control: a #LrgControllerControl
 *
 * Returns: %TRUE when pads of @family have @control (every family has the
 *   standard buttons; only some have back paddles, a touchpad, ...)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_has_control (LrgControllerFamily  family,
                                     LrgControllerControl control);

/**
 * lrg_controller_get_glyph:
 * @family: a #LrgControllerFamily
 * @control: a #LrgControllerControl
 *
 * The label printed on @control for @family: "A", "Cross", "L4", "Share".
 *
 * Returns: (transfer none) (nullable): the label, %NULL when @family lacks
 *   @control
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_controller_get_glyph (LrgControllerFamily  family,
                                        LrgControllerControl control);

/**
 * lrg_controller_get_button_glyph:
 * @family: a #LrgControllerFamily
 * @button: a #GrlGamepadButton
 *
 * lrg_controller_get_glyph() for a standard button.  An unknown family
 * uses the generic (Xbox) labels.
 *
 * Returns: (transfer none): the label ("Unknown" for no button)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_controller_get_button_glyph (LrgControllerFamily family,
                                               GrlGamepadButton    button);

/**
 * lrg_controller_get_axis_glyph:
 * @family: a #LrgControllerFamily
 * @axis: a #GrlGamepadAxis
 *
 * Returns: (transfer none): the label of a stick or trigger axis
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_controller_get_axis_glyph (LrgControllerFamily family,
                                             GrlGamepadAxis      axis);

/* ==========================================================================
 * Presets
 * ========================================================================== */

/**
 * lrg_controller_preset_get_roles:
 *
 * The roles every family's preset defines, as a %NULL-terminated array:
 * "move-up", "move-down", "move-left", "move-right" (left stick),
 * "look-up", "look-down", "look-left", "look-right" (right stick),
 * "confirm", "cancel", "face-west", "face-north", "page-previous",
 * "page-next", "trigger-left", "trigger-right", "view", "menu", "guide",
 * "stick-left-click", "stick-right-click", "dpad-up", "dpad-down",
 * "dpad-left", "dpad-right".
 *
 * Returns: (transfer none): the role names
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_controller_preset_get_roles (void);

/**
 * lrg_controller_preset_lookup:
 * @family: a #LrgControllerFamily
 * @role: a role from lrg_controller_preset_get_roles()
 * @is_axis: (out): %TRUE when the role is a stick direction
 * @code: (out): the #GrlGamepadButton, or the #GrlGamepadAxis for an axis
 * @positive: (out): for an axis, whether the role is the positive direction
 *
 * Looks up what a family's preset binds a role to.  The families share the
 * physical layout except that the Switch Pro controller confirms with its
 * right face button (its A) and cancels with the bottom one (its B), the
 * Nintendo convention, so a "confirm" press always lands on the button
 * whose glyph the family calls A, Cross or A.
 *
 * Returns: %TRUE when @role is known
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_preset_lookup (LrgControllerFamily  family,
                                       const gchar         *role,
                                       gboolean            *is_axis,
                                       gint                *code,
                                       gboolean            *positive);

/**
 * lrg_controller_preset_new_map:
 * @family: a #LrgControllerFamily
 * @gamepad: the gamepad index (0-3) the bindings read
 *
 * Builds an #LrgInputMap of the family's preset: one action per role
 * (named like the role) bound to its button, or to its stick direction at
 * a 0.5 threshold.
 *
 * Returns: (transfer full): a new #LrgInputMap
 */
LRG_AVAILABLE_IN_ALL
LrgInputMap * lrg_controller_preset_new_map (LrgControllerFamily family,
                                             gint                gamepad);

/* ==========================================================================
 * LrgControllerMonitor
 * ========================================================================== */

#define LRG_TYPE_CONTROLLER_MONITOR (lrg_controller_monitor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgControllerMonitor, lrg_controller_monitor, LRG, CONTROLLER_MONITOR, GObject)

/**
 * LrgControllerMonitor:
 *
 * Watches one gamepad slot of an #LrgInput.  Call lrg_controller_monitor_poll()
 * once per frame: when the slot's controller appears, changes or goes away
 * the family is detected again and #LrgControllerMonitor::family-changed is
 * emitted.  Detection is re-run only when the connection or the reported
 * name changes, so polling is cheap.
 *
 * The family can be forced by the %LRG_CONTROLLER_ENV_FAMILY environment
 * variable or lrg_controller_monitor_set_family_override().
 */

/**
 * lrg_controller_monitor_new:
 * @input: (transfer none): the #LrgInput to watch (the real gamepad source or
 *   an #LrgInputMock)
 * @gamepad: the gamepad index (0-3)
 *
 * Returns: (transfer full): a new #LrgControllerMonitor
 */
LRG_AVAILABLE_IN_ALL
LrgControllerMonitor * lrg_controller_monitor_new (LrgInput *input,
                                                   gint      gamepad);

/**
 * lrg_controller_monitor_poll:
 * @self: an #LrgControllerMonitor
 *
 * Reads the slot and emits #LrgControllerMonitor::family-changed when the
 * family differs from the last poll.
 *
 * Returns: %TRUE when the family changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_monitor_poll (LrgControllerMonitor *self);

/**
 * lrg_controller_monitor_get_family:
 * @self: an #LrgControllerMonitor
 *
 * Returns: the family as of the last poll, %LRG_CONTROLLER_FAMILY_UNKNOWN
 *   while nothing is connected
 */
LRG_AVAILABLE_IN_ALL
LrgControllerFamily lrg_controller_monitor_get_family (LrgControllerMonitor *self);

/**
 * lrg_controller_monitor_is_connected:
 * @self: an #LrgControllerMonitor
 *
 * Returns: %TRUE when a controller was connected at the last poll
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_monitor_is_connected (LrgControllerMonitor *self);

/**
 * lrg_controller_monitor_get_name:
 * @self: an #LrgControllerMonitor
 *
 * Returns: (transfer none) (nullable): the name the controller reports
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_controller_monitor_get_name (LrgControllerMonitor *self);

/**
 * lrg_controller_monitor_get_ids:
 * @self: an #LrgControllerMonitor
 * @vendor: (out) (optional): the USB vendor id
 * @product: (out) (optional): the USB product id
 *
 * Returns: %TRUE when the ids of the connected controller are known
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_monitor_get_ids (LrgControllerMonitor *self,
                                         guint16              *vendor,
                                         guint16              *product);

/**
 * lrg_controller_monitor_get_steam_deck_host:
 * @self: an #LrgControllerMonitor
 *
 * Returns: whether the monitor treats this machine as a Steam Deck
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_controller_monitor_get_steam_deck_host (LrgControllerMonitor *self);

/**
 * lrg_controller_monitor_set_steam_deck_host:
 * @self: an #LrgControllerMonitor
 * @steam_deck_host: whether this machine is a Steam Deck
 *
 * Overrides the host detection (the default asks
 * lrg_controller_is_steam_deck_host() once).  The family is detected again
 * at the next poll.
 */
LRG_AVAILABLE_IN_ALL
void lrg_controller_monitor_set_steam_deck_host (LrgControllerMonitor *self,
                                                 gboolean              steam_deck_host);

/**
 * lrg_controller_monitor_set_family_override:
 * @self: an #LrgControllerMonitor
 * @family: the family to report while a controller is connected, or
 *   %LRG_CONTROLLER_FAMILY_UNKNOWN to detect again
 *
 * Forces the family (a user's choice in an options menu).
 */
LRG_AVAILABLE_IN_ALL
void lrg_controller_monitor_set_family_override (LrgControllerMonitor *self,
                                                 LrgControllerFamily   family);

/**
 * lrg_controller_monitor_get_family_override:
 * @self: an #LrgControllerMonitor
 *
 * Returns: the forced family, %LRG_CONTROLLER_FAMILY_UNKNOWN for none
 */
LRG_AVAILABLE_IN_ALL
LrgControllerFamily lrg_controller_monitor_get_family_override (LrgControllerMonitor *self);

G_END_DECLS
