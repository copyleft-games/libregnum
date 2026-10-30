/* lrg-editor-timeline.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Undoable timeline (keyframe) editing on an #LrgEditor.
 *
 * Every function here edits a copy of the named #LrgTimeline and records
 * the change as one %LRG_EDITOR_CMD_TIMELINE command, so keyframing
 * undoes, redoes and marks the document dirty like any other edit. The
 * timeline panel, the IPC methods (timeline.*) and the tests share these
 * entry points.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * lrg_editor_timeline_add:
 * @self: an #LrgEditor
 * @name: the new timeline's name
 * @length: length in seconds (<= 0 uses 2 s)
 * @loop: whether it loops
 *
 * Adds an empty timeline to the level.
 *
 * Returns: %FALSE if a timeline called @name already exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_timeline_add (LrgEditor   *self,
                                  const gchar *name,
                                  gfloat       length,
                                  gboolean     loop);

/**
 * lrg_editor_timeline_remove:
 * @self: an #LrgEditor
 * @name: a timeline name
 *
 * Returns: %FALSE if there is no such timeline
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_timeline_remove (LrgEditor   *self,
                                     const gchar *name);

/**
 * lrg_editor_timeline_configure:
 * @self: an #LrgEditor
 * @name: a timeline name
 * @new_name: (nullable): rename to this, or %NULL to keep
 * @length: new length in seconds, or <= 0 to keep
 * @loop: 1/0 to set looping, -1 to keep
 * @autoplay: 1/0 to set autoplay, -1 to keep
 *
 * Changes timeline settings as one undo step.
 *
 * Returns: %FALSE if there is no such timeline or @new_name is taken
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_timeline_configure (LrgEditor   *self,
                                        const gchar *name,
                                        const gchar *new_name,
                                        gfloat       length,
                                        gint         loop,
                                        gint         autoplay);

/**
 * lrg_editor_timeline_set_key:
 * @self: an #LrgEditor
 * @name: a timeline name (created when missing)
 * @node: the animated node
 * @channel: a channel (see lrg_timeline_channel_is_valid())
 * @time: key time in seconds
 * @value: key value
 * @easing: easing to the next key
 * @merge: %TRUE to coalesce with the previous identical-label edit (drags)
 *
 * Adds or replaces one key.
 *
 * Returns: %FALSE on an invalid channel or a node without a GUID
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_timeline_set_key (LrgEditor     *self,
                                      const gchar   *name,
                                      LrgNode       *node,
                                      const gchar   *channel,
                                      gfloat         time,
                                      gfloat         value,
                                      LrgEasingType  easing,
                                      gboolean       merge);

/**
 * lrg_editor_timeline_key_node:
 * @self: an #LrgEditor
 * @name: a timeline name (created when missing)
 * @node: the node to key
 * @time: key time in seconds
 * @channels: (nullable) (array zero-terminated=1): channels to key, or
 *   %NULL for the nine transform channels
 * @easing: easing for the new keys
 *
 * Keys the node's current values of @channels at @time, as one undo step
 * ("Insert Keyframe").
 *
 * Returns: the number of keys written
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_timeline_key_node (LrgEditor           *self,
                                    const gchar         *name,
                                    LrgNode             *node,
                                    gfloat               time,
                                    const gchar * const *channels,
                                    LrgEasingType        easing);

/**
 * lrg_editor_timeline_remove_key:
 * @self: an #LrgEditor
 * @name: a timeline name
 * @node: the animated node
 * @channel: (nullable): a channel, or %NULL for every track of @node
 * @time: the key time (keys within 1 ms match)
 *
 * Removes matching keys; tracks left without keys are dropped.
 *
 * Returns: the number of keys removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_timeline_remove_key (LrgEditor   *self,
                                      const gchar *name,
                                      LrgNode     *node,
                                      const gchar *channel,
                                      gfloat       time);

/**
 * lrg_editor_timeline_move_key:
 * @self: an #LrgEditor
 * @name: a timeline name
 * @node: the animated node
 * @channel: (nullable): a channel, or %NULL for every track of @node
 * @from_time: the key's current time (keys within 1 ms match)
 * @to_time: the new time (>= 0)
 * @merge: %TRUE while dragging, so the drag is one undo step
 *
 * Retimes matching keys, keeping value and easing.
 *
 * Returns: the number of keys moved
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_timeline_move_key (LrgEditor   *self,
                                    const gchar *name,
                                    LrgNode     *node,
                                    const gchar *channel,
                                    gfloat       from_time,
                                    gfloat       to_time,
                                    gboolean     merge);

/**
 * lrg_editor_timeline_set_easing:
 * @self: an #LrgEditor
 * @name: a timeline name
 * @node: the animated node
 * @channel: (nullable): a channel, or %NULL for every track of @node
 * @time: the key time (keys within 1 ms match)
 * @easing: the new easing to the next key
 *
 * Returns: the number of keys changed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_timeline_set_easing (LrgEditor     *self,
                                      const gchar   *name,
                                      LrgNode       *node,
                                      const gchar   *channel,
                                      gfloat         time,
                                      LrgEasingType  easing);

/**
 * lrg_editor_timeline_remove_track:
 * @self: an #LrgEditor
 * @name: a timeline name
 * @node: the animated node
 * @channel: (nullable): a channel, or %NULL for every track of @node
 *
 * Returns: the number of tracks removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_timeline_remove_track (LrgEditor   *self,
                                        const gchar *name,
                                        LrgNode     *node,
                                        const gchar *channel);

G_END_DECLS
