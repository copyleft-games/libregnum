/* lrg-feed.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFeed - a scrolling notification feed (the "you received" corner of
 * a HUD: items and gold picked up, experience, reputation): short rows
 * that stay a few seconds and fade, newest last, with rows of the same
 * key merged into one counted row while it is still shown.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>

#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_FEED_DEFAULT_SECONDS:
 *
 * How long a row stays by default (merging restarts it).
 */
#define LRG_FEED_DEFAULT_SECONDS (5.0f)

/**
 * LRG_FEED_FADE_SECONDS:
 *
 * The last seconds of a row's time over which it fades.
 */
#define LRG_FEED_FADE_SECONDS (1.0f)

#define LRG_TYPE_FEED (lrg_feed_get_type ())

#pragma GCC visibility push(default)
G_DECLARE_FINAL_TYPE (LrgFeed, lrg_feed, LRG, FEED, GObject)
#pragma GCC visibility pop

/**
 * LrgFeed:
 *
 * At most #LrgFeed:max-rows rows, oldest first; a new row pushes the
 * oldest out of a full feed.
 *
 * Signals: #LrgFeed::changed after rows were added, merged or dropped.
 */

/**
 * LrgFeed::changed:
 * @self: the feed
 */

/**
 * lrg_feed_new:
 * @max_rows: the most rows shown (clamped to 1 .. 64)
 * @seconds: how long a row stays, 0 or less for %LRG_FEED_DEFAULT_SECONDS
 *
 * Returns: (transfer full): an empty feed
 */
LRG_AVAILABLE_IN_ALL
LrgFeed *
lrg_feed_new (guint  max_rows,
              gfloat seconds);

/**
 * lrg_feed_push:
 * @self: an #LrgFeed
 * @key: (nullable): rows of the same key merge while shown; %NULL never
 *   merges
 * @label: what the row shows (an item's name)
 * @count: how many (added to a merged row's count)
 *
 * Adds a row, or adds @count to the shown row of @key, moves it to the
 * end and restarts its time.
 *
 * Returns: the row's index after the call, -1 for an empty label
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_feed_push (LrgFeed     *self,
               const gchar *key,
               const gchar *label,
               gint64       count);

/**
 * lrg_feed_update:
 * @self: an #LrgFeed
 * @delta: seconds passed
 *
 * Ages the rows and drops those whose time is up.
 *
 * Returns: the rows dropped
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_feed_update (LrgFeed *self,
                 gfloat   delta);

/**
 * lrg_feed_get_count:
 * @self: an #LrgFeed
 *
 * Returns: the rows shown
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_feed_get_count (LrgFeed *self);

/**
 * lrg_feed_get_label:
 * @self: an #LrgFeed
 * @index: 0 for the oldest
 *
 * Returns: (transfer none) (nullable): the row's label
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_feed_get_label (LrgFeed *self,
                    guint    index);

/**
 * lrg_feed_get_key:
 * @self: an #LrgFeed
 * @index: 0 for the oldest
 *
 * Returns: (transfer none) (nullable): the row's key
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_feed_get_key (LrgFeed *self,
                  guint    index);

/**
 * lrg_feed_get_row_count:
 * @self: an #LrgFeed
 * @index: 0 for the oldest
 *
 * Returns: the row's count, 0 out of range
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_feed_get_row_count (LrgFeed *self,
                        guint    index);

/**
 * lrg_feed_get_alpha:
 * @self: an #LrgFeed
 * @index: 0 for the oldest
 *
 * Returns: the row's opacity: 1, then down to 0 over the last
 *   %LRG_FEED_FADE_SECONDS; 0 out of range
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_feed_get_alpha (LrgFeed *self,
                    guint    index);

/**
 * lrg_feed_clear:
 * @self: an #LrgFeed
 */
LRG_AVAILABLE_IN_ALL
void
lrg_feed_clear (LrgFeed *self);

G_END_DECLS
