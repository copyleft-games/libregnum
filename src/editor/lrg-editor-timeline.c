/* lrg-editor-timeline.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Undoable timeline editing. Each entry point copies the live timeline,
 * edits the copy, and records a before/after LRG_EDITOR_CMD_TIMELINE
 * command (which installs the copy). Timelines are small (a few tracks of
 * a few keys), so whole-timeline snapshots keep the command trivial and
 * make every kind of edit (keys, easing, tracks, settings) uniform.
 */

#include "lrg-editor-timeline.h"
#include "lrg-editor.h"
#include "lrg-editor-command.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-timeline.h"
#include <math.h>
#include <string.h>

#define MATCH_TOLERANCE (0.001f)

static const gchar * const transform_channels[] = {
	"location.x", "location.y", "location.z",
	"rotation.x", "rotation.y", "rotation.z",
	"scale.x", "scale.y", "scale.z",
	NULL
};

static LrgTimeline *
find_live (LrgEditor   *self,
           const gchar *name)
{
	LrgLevel *level = lrg_editor_get_level (self);
	gint      idx = lrg_level_find_timeline (level, name);

	return (idx >= 0) ? lrg_level_get_timeline (level, (guint) idx) : NULL;
}

/* Records before -> after; NULL before adds, NULL after removes. */
static void
commit (LrgEditor   *self,
        LrgTimeline *before,
        LrgTimeline *after,
        const gchar *label,
        gboolean     merge)
{
	LrgEditorCommand *cmd = lrg_editor_command_new_timeline (lrg_editor_get_level (self), before, after, label);

	if (merge)
		lrg_editor_execute_merged (self, cmd);
	else
		lrg_editor_execute (self, cmd);
}

gboolean
lrg_editor_timeline_add (LrgEditor   *self,
                         const gchar *name,
                         gfloat       length,
                         gboolean     loop)
{
	g_autoptr(LrgTimeline) fresh = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (name != NULL && *name != '\0', FALSE);

	if (find_live (self, name) != NULL)
		return FALSE;
	fresh = lrg_timeline_new (name);
	if (length > 0.0f && isfinite (length))
		lrg_timeline_set_length (fresh, length);
	lrg_timeline_set_loop (fresh, loop);
	commit (self, NULL, fresh, "Add Timeline", FALSE);
	return TRUE;
}

gboolean
lrg_editor_timeline_remove (LrgEditor   *self,
                            const gchar *name)
{
	LrgTimeline *live;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	if ((live = find_live (self, name)) == NULL)
		return FALSE;
	commit (self, live, NULL, "Remove Timeline", FALSE);
	return TRUE;
}

gboolean
lrg_editor_timeline_configure (LrgEditor   *self,
                               const gchar *name,
                               const gchar *new_name,
                               gfloat       length,
                               gint         loop,
                               gint         autoplay)
{
	LrgTimeline           *live;
	g_autoptr(LrgTimeline) edit = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);

	if ((live = find_live (self, name)) == NULL)
		return FALSE;
	if (new_name != NULL && g_strcmp0 (new_name, name) != 0 && (*new_name == '\0' || find_live (self, new_name) != NULL))
		return FALSE;

	edit = lrg_timeline_copy (live);
	if (new_name != NULL)
		lrg_timeline_set_name (edit, new_name);
	if (length > 0.0f && isfinite (length))
		lrg_timeline_set_length (edit, length);
	if (loop >= 0)
		lrg_timeline_set_loop (edit, loop != 0);
	if (autoplay >= 0)
		lrg_timeline_set_autoplay (edit, autoplay != 0);
	if (lrg_timeline_equal (live, edit))
		return TRUE;
	commit (self, live, edit, "Timeline Settings", FALSE);
	return TRUE;
}

/* Working copy of @name, or a fresh timeline when it does not exist yet
 * (*live is then NULL, so the commit adds it). */
static LrgTimeline *
edit_copy (LrgEditor    *self,
           const gchar  *name,
           LrgTimeline **live)
{
	*live = find_live (self, name);
	return (*live != NULL) ? lrg_timeline_copy (*live) : lrg_timeline_new (name);
}

gboolean
lrg_editor_timeline_set_key (LrgEditor     *self,
                             const gchar   *name,
                             LrgNode       *node,
                             const gchar   *channel,
                             gfloat         time,
                             gfloat         value,
                             LrgEasingType  easing,
                             gboolean       merge)
{
	LrgTimeline           *live;
	g_autoptr(LrgTimeline) edit = NULL;

	g_return_val_if_fail (LRG_IS_EDITOR (self), FALSE);
	g_return_val_if_fail (name != NULL && *name != '\0', FALSE);
	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	if (!lrg_timeline_channel_is_valid (channel) || lrg_node_get_guid (node) == NULL ||
	    !isfinite (time) || !isfinite (value))
		return FALSE;

	edit = edit_copy (self, name, &live);
	lrg_timeline_track_set_key (lrg_timeline_ensure_track (edit, lrg_node_get_guid (node), channel),
	                            MAX (time, 0.0f), value, easing);
	commit (self, live, edit, "Set Keyframe", merge && live != NULL);
	return TRUE;
}

guint
lrg_editor_timeline_key_node (LrgEditor           *self,
                              const gchar         *name,
                              LrgNode             *node,
                              gfloat               time,
                              const gchar * const *channels,
                              LrgEasingType        easing)
{
	LrgTimeline           *live;
	g_autoptr(LrgTimeline) edit = NULL;
	guint                  i, keyed = 0;

	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);
	g_return_val_if_fail (name != NULL && *name != '\0', 0);
	g_return_val_if_fail (LRG_IS_NODE (node), 0);

	if (lrg_node_get_guid (node) == NULL || !isfinite (time))
		return 0;
	if (channels == NULL)
		channels = transform_channels;

	edit = edit_copy (self, name, &live);
	for (i = 0; channels[i] != NULL; i++)
	{
		gfloat value;

		if (!lrg_timeline_channel_read (node, channels[i], &value))
			continue;
		lrg_timeline_track_set_key (lrg_timeline_ensure_track (edit, lrg_node_get_guid (node), channels[i]),
		                            MAX (time, 0.0f), value, easing);
		keyed++;
	}
	if (keyed > 0)
		commit (self, live, edit, "Insert Keyframe", FALSE);
	return keyed;
}

/* Calls @func on every track of @node (optionally one channel) in @edit. */
typedef guint (*TrackFunc) (LrgTimelineTrack *track, gpointer data);

static guint
foreach_track (LrgTimeline *edit,
               LrgNode     *node,
               const gchar *channel,
               TrackFunc    func,
               gpointer     data)
{
	guint i, total = 0;

	for (i = 0; i < lrg_timeline_get_n_tracks (edit); i++)
	{
		LrgTimelineTrack *t = lrg_timeline_get_track (edit, i);

		if (g_strcmp0 (lrg_timeline_track_get_node_guid (t), lrg_node_get_guid (node)) != 0)
			continue;
		if (channel != NULL && g_strcmp0 (lrg_timeline_track_get_channel (t), channel) != 0)
			continue;
		total += func (t, data);
	}
	return total;
}

static guint
remove_key_func (LrgTimelineTrack *track,
                 gpointer          data)
{
	gint idx = lrg_timeline_track_find_key (track, *(gfloat *) data, MATCH_TOLERANCE);

	return (idx >= 0 && lrg_timeline_track_remove_key (track, (guint) idx)) ? 1 : 0;
}

typedef struct
{
	gfloat                 from;
	gfloat                 to;
	LrgEasingType          easing;
	LrgSequenceTangentMode mode;
	gfloat                 in_tangent;
	gfloat                 out_tangent;
} KeyEdit;

static guint
move_key_func (LrgTimelineTrack *track,
               gpointer          data)
{
	KeyEdit *ke = data;
	gint     idx = lrg_timeline_track_find_key (track, ke->from, MATCH_TOLERANCE);

	/* move_key keeps the value, the easing and the Bézier handles; a
	 * remove + set_key pair would drop the handles */
	if (idx < 0)
		return 0;
	return lrg_timeline_track_move_key (track, (guint) idx, ke->to) >= 0 ? 1 : 0;
}

static guint
easing_func (LrgTimelineTrack *track,
             gpointer          data)
{
	KeyEdit *ke = data;
	gint     idx = lrg_timeline_track_find_key (track, ke->from, MATCH_TOLERANCE);
	gfloat   t, value;

	if (idx < 0 || !lrg_timeline_track_get_key (track, (guint) idx, &t, &value, NULL))
		return 0;
	lrg_timeline_track_set_key (track, t, value, ke->easing);
	/* Picking an easing makes a Bézier key an eased key again */
	idx = lrg_timeline_track_find_key (track, t, MATCH_TOLERANCE);
	if (idx >= 0)
		lrg_timeline_track_clear_key_tangents (track, (guint) idx);
	return 1;
}

static guint
tangents_func (LrgTimelineTrack *track,
               gpointer          data)
{
	KeyEdit *ke = data;
	gint     idx = lrg_timeline_track_find_key (track, ke->from, MATCH_TOLERANCE);

	if (idx < 0)
		return 0;
	return lrg_timeline_track_set_key_tangents (track, (guint) idx, ke->mode, ke->in_tangent, ke->out_tangent)
	       ? 1 : 0;
}

static guint
clear_tangents_func (LrgTimelineTrack *track,
                     gpointer          data)
{
	KeyEdit *ke = data;
	gint     idx = lrg_timeline_track_find_key (track, ke->from, MATCH_TOLERANCE);

	if (idx < 0)
		return 0;
	return lrg_timeline_track_clear_key_tangents (track, (guint) idx) ? 1 : 0;
}

/* Drops tracks a removal left empty. */
static void
prune_empty (LrgTimeline *edit)
{
	guint i = lrg_timeline_get_n_tracks (edit);

	while (i-- > 0)
		if (lrg_timeline_track_get_n_keys (lrg_timeline_get_track (edit, i)) == 0)
			lrg_timeline_remove_track (edit, i);
}

guint
lrg_editor_timeline_remove_key (LrgEditor   *self,
                                const gchar *name,
                                LrgNode     *node,
                                const gchar *channel,
                                gfloat       time)
{
	LrgTimeline           *live;
	g_autoptr(LrgTimeline) edit = NULL;
	guint                  n;

	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);
	g_return_val_if_fail (LRG_IS_NODE (node), 0);

	if ((live = find_live (self, name)) == NULL)
		return 0;
	edit = lrg_timeline_copy (live);
	n = foreach_track (edit, node, channel, remove_key_func, &time);
	if (n == 0)
		return 0;
	prune_empty (edit);
	commit (self, live, edit, "Delete Keyframe", FALSE);
	return n;
}

guint
lrg_editor_timeline_move_key (LrgEditor   *self,
                              const gchar *name,
                              LrgNode     *node,
                              const gchar *channel,
                              gfloat       from_time,
                              gfloat       to_time,
                              gboolean     merge)
{
	LrgTimeline           *live;
	g_autoptr(LrgTimeline) edit = NULL;
	KeyEdit                ke;
	guint                  n;

	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);
	g_return_val_if_fail (LRG_IS_NODE (node), 0);

	if ((live = find_live (self, name)) == NULL || !isfinite (to_time))
		return 0;
	memset (&ke, 0, sizeof ke);
	ke.from = from_time;
	ke.to = MAX (to_time, 0.0f);
	ke.easing = LRG_EASING_LINEAR;
	edit = lrg_timeline_copy (live);
	n = foreach_track (edit, node, channel, move_key_func, &ke);
	if (n > 0)
		commit (self, live, edit, "Move Keyframe", merge);
	return n;
}

guint
lrg_editor_timeline_set_easing (LrgEditor     *self,
                                const gchar   *name,
                                LrgNode       *node,
                                const gchar   *channel,
                                gfloat         time,
                                LrgEasingType  easing)
{
	LrgTimeline           *live;
	g_autoptr(LrgTimeline) edit = NULL;
	KeyEdit                ke;
	guint                  n;

	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);
	g_return_val_if_fail (LRG_IS_NODE (node), 0);

	if ((live = find_live (self, name)) == NULL)
		return 0;
	memset (&ke, 0, sizeof ke);
	ke.from = time;
	ke.to = time;
	ke.easing = easing;
	edit = lrg_timeline_copy (live);
	n = foreach_track (edit, node, channel, easing_func, &ke);
	if (n > 0 && !lrg_timeline_equal (live, edit))
		commit (self, live, edit, "Set Easing", FALSE);
	return n;
}

guint
lrg_editor_timeline_set_tangents (LrgEditor              *self,
                                  const gchar            *name,
                                  LrgNode                *node,
                                  const gchar            *channel,
                                  gfloat                  time,
                                  LrgSequenceTangentMode  mode,
                                  gfloat                  in_tangent,
                                  gfloat                  out_tangent)
{
	LrgTimeline           *live;
	g_autoptr(LrgTimeline) edit = NULL;
	KeyEdit                ke;
	guint                  n;

	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);
	g_return_val_if_fail (LRG_IS_NODE (node), 0);

	if ((live = find_live (self, name)) == NULL || !isfinite (in_tangent) || !isfinite (out_tangent))
		return 0;
	memset (&ke, 0, sizeof ke);
	ke.from = time;
	ke.mode = mode;
	ke.in_tangent = in_tangent;
	ke.out_tangent = out_tangent;
	edit = lrg_timeline_copy (live);
	n = foreach_track (edit, node, channel, tangents_func, &ke);
	if (n > 0 && !lrg_timeline_equal (live, edit))
		commit (self, live, edit, "Set Tangents", FALSE);
	return n;
}

guint
lrg_editor_timeline_clear_tangents (LrgEditor   *self,
                                    const gchar *name,
                                    LrgNode     *node,
                                    const gchar *channel,
                                    gfloat       time)
{
	LrgTimeline           *live;
	g_autoptr(LrgTimeline) edit = NULL;
	KeyEdit                ke;
	guint                  n;

	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);
	g_return_val_if_fail (LRG_IS_NODE (node), 0);

	if ((live = find_live (self, name)) == NULL)
		return 0;
	memset (&ke, 0, sizeof ke);
	ke.from = time;
	edit = lrg_timeline_copy (live);
	n = foreach_track (edit, node, channel, clear_tangents_func, &ke);
	if (n > 0)
		commit (self, live, edit, "Clear Tangents", FALSE);
	return n;
}

static guint
mark_track_func (LrgTimelineTrack *track,
                 gpointer          data)
{
	/* Empty the curve; prune_empty() then drops the track. */
	lrg_keyframe_curve_clear (lrg_timeline_track_get_curve (track));
	return 1;
}

guint
lrg_editor_timeline_remove_track (LrgEditor   *self,
                                  const gchar *name,
                                  LrgNode     *node,
                                  const gchar *channel)
{
	LrgTimeline           *live;
	g_autoptr(LrgTimeline) edit = NULL;
	guint                  n;

	g_return_val_if_fail (LRG_IS_EDITOR (self), 0);
	g_return_val_if_fail (LRG_IS_NODE (node), 0);

	if ((live = find_live (self, name)) == NULL)
		return 0;
	edit = lrg_timeline_copy (live);
	n = foreach_track (edit, node, channel, mark_track_func, NULL);
	if (n == 0)
		return 0;
	prune_empty (edit);
	commit (self, live, edit, "Delete Track", FALSE);
	return n;
}
