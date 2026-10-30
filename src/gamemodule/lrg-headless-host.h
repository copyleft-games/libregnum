/* lrg-headless-host.h - LrgGameHost with no window, for headless runs
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_HEADLESS_HOST (lrg_headless_host_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHeadlessHost, lrg_headless_host, LRG, HEADLESS_HOST, GObject)

/**
 * lrg_headless_host_new:
 * @engine: a started #LrgEngine
 * @frame_delta: the delta lrg_game_host_get_frame_delta() reports, in seconds
 *
 * Creates a game host with no window and no render target: frames have a
 * fixed delta and drawing is a no-op. Headless replay playback, golden
 * tests and dedicated simulation servers drive a template through it.
 *
 * Returns: (transfer full): a new #LrgHeadlessHost
 *
 * Since: 0.4
 */
LRG_AVAILABLE_IN_ALL
LrgHeadlessHost *lrg_headless_host_new (LrgEngine *engine,
                                        gdouble    frame_delta);

G_END_DECLS
