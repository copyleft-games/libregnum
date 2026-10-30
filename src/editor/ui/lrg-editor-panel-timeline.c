/* lrg-editor-panel-timeline.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The timeline panel: pick or create a level timeline, scrub and play it,
 * insert keyframes for the selection, select/drag/delete keys and change
 * their easing. Edits go through the undoable lrg_editor_timeline_* API.
 *
 * Preview: scrubbing or playing writes sampled values straight onto the
 * nodes (outside the undo history, as in other editors' animation mode).
 * The value each animated channel had before the preview started is kept
 * in timeline_rest and written back when the preview ends (Stop, saving,
 * play-in-editor, switching timelines), so previewing never changes the
 * saved document.
 *
 * Play-in-editor plays every timeline marked autoplay on the running
 * level (editor_timeline_play_begin / _apply, called from
 * lrg-editor-play.c).
 */

#include "lrg-editor-app-private.h"
#include <math.h>
#include <string.h>

#define RULER_H       (20.0f)
#define LABEL_W_RATIO (0.32f)
#define KEY_HALF      (5.0f)

void
editor_timeline_init (LrgEditorApp *self)
{
	self->timeline_rest = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->timeline_play = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->timeline_new_name = g_string_new (NULL);
	self->timeline_easing = LRG_EASING_LINEAR;
}

void
editor_timeline_dispose (LrgEditorApp *self)
{
	g_clear_pointer (&self->timeline_rest, g_hash_table_unref);
	g_clear_pointer (&self->timeline_play, g_hash_table_unref);
	if (self->timeline_new_name != NULL)
	{
		g_string_free (self->timeline_new_name, TRUE);
		self->timeline_new_name = NULL;
	}
	g_clear_pointer (&self->timeline_name, g_free);
	g_clear_pointer (&self->timeline_key_guid, g_free);
	g_clear_pointer (&self->timeline_key_channel, g_free);
}

/* The panel's timeline: the chosen one, else the level's first. */
LrgTimeline *
editor_timeline_current (LrgEditorApp *self)
{
	LrgLevel *level = lrg_editor_get_level (self->editor);
	gint      idx = lrg_level_find_timeline (level, self->timeline_name);

	if (idx < 0)
	{
		if (lrg_level_get_n_timelines (level) == 0)
			return NULL;
		idx = 0;
		g_free (self->timeline_name);
		self->timeline_name = g_strdup (lrg_timeline_get_name (lrg_level_get_timeline (level, 0)));
	}
	return lrg_level_get_timeline (level, (guint) idx);
}

static gchar *
rest_key (const gchar *guid,
          const gchar *channel)
{
	return g_strconcat (guid, "\n", channel, NULL);
}

/* Remembers the pre-preview value of every channel @tl animates. */
static void
record_rest (LrgEditorApp *self,
             LrgTimeline  *tl)
{
	LrgNode *root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	guint    i;

	for (i = 0; i < lrg_timeline_get_n_tracks (tl); i++)
	{
		LrgTimelineTrack *t = lrg_timeline_get_track (tl, i);
		gchar            *key = rest_key (lrg_timeline_track_get_node_guid (t), lrg_timeline_track_get_channel (t));
		LrgNode          *node;
		gfloat            value;

		if (g_hash_table_contains (self->timeline_rest, key) ||
		    (node = lrg_node_find_by_guid (root, lrg_timeline_track_get_node_guid (t))) == NULL ||
		    !lrg_timeline_channel_read (node, lrg_timeline_track_get_channel (t), &value))
		{
			g_free (key);
			continue;
		}
		g_hash_table_insert (self->timeline_rest, key, g_memdup2 (&value, sizeof value));
	}
}

void
editor_timeline_end_preview (LrgEditorApp *self)
{
	LrgNode       *root;
	GHashTableIter iter;
	gpointer       key, value;

	self->timeline_playing = FALSE;
	if (!self->timeline_previewing)
		return;
	self->timeline_previewing = FALSE;

	/* Put every previewed channel back to its rest value. */
	root = lrg_level_get_root (lrg_editor_get_level (self->editor));
	g_hash_table_iter_init (&iter, self->timeline_rest);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		g_auto(GStrv) parts = g_strsplit (key, "\n", 2);
		LrgNode      *node = (parts[0] != NULL && parts[1] != NULL) ? lrg_node_find_by_guid (root, parts[0]) : NULL;

		if (node != NULL)
			lrg_timeline_channel_write (node, parts[1], *(gfloat *) value);
	}
	g_hash_table_remove_all (self->timeline_rest);
}

void
editor_timeline_seek (LrgEditorApp *self,
                      gfloat        time)
{
	LrgTimeline *tl = editor_timeline_current (self);

	if (tl == NULL || self->playing)
		return;
	if (!isfinite (time))
		time = 0.0f;
	self->timeline_time = CLAMP (time, 0.0f, lrg_timeline_get_length (tl));
	record_rest (self, tl);
	self->timeline_previewing = TRUE;
	lrg_timeline_apply (tl, lrg_level_get_root (lrg_editor_get_level (self->editor)), self->timeline_time);
}

void
editor_timeline_set_playing (LrgEditorApp *self,
                             gboolean      playing)
{
	LrgTimeline *tl = editor_timeline_current (self);

	if (tl == NULL || self->playing)
		return;
	if (playing && !lrg_timeline_get_loop (tl) && self->timeline_time >= lrg_timeline_get_length (tl))
		self->timeline_time = 0.0f;
	self->timeline_playing = playing;
	editor_timeline_seek (self, self->timeline_time);
}

void
editor_timeline_tick (LrgEditorApp *self,
                      gfloat        delta)
{
	LrgTimeline *tl;
	gfloat       t;

	if (!self->timeline_playing || (tl = editor_timeline_current (self)) == NULL)
	{
		self->timeline_playing = FALSE;
		return;
	}
	t = self->timeline_time + CLAMP (delta, 0.0f, 0.1f);
	if (t >= lrg_timeline_get_length (tl))
	{
		if (lrg_timeline_get_loop (tl))
			t = lrg_timeline_wrap_time (tl, t);
		else
		{
			t = lrg_timeline_get_length (tl);
			self->timeline_playing = FALSE;
		}
	}
	editor_timeline_seek (self, t);
}

/* Inserts transform keys for every selected node at the playhead. */
guint
editor_timeline_key_selection (LrgEditorApp *self)
{
	GPtrArray   *nodes = lrg_editor_selection_get_nodes (lrg_editor_get_selection (self->editor));
	LrgTimeline *tl = editor_timeline_current (self);
	const gchar *name;
	guint        i, keys = 0;

	if (nodes->len == 0)
		return 0;
	name = (tl != NULL) ? lrg_timeline_get_name (tl) : "Timeline";
	if (tl == NULL)
	{
		g_free (self->timeline_name);
		self->timeline_name = g_strdup (name);
	}

	lrg_editor_begin_group (self->editor, "Insert Keyframe");
	for (i = 0; i < nodes->len; i++)
		keys += lrg_editor_timeline_key_node (self->editor, name, g_ptr_array_index (nodes, i), self->timeline_time,
		                                      NULL, (LrgEasingType) self->timeline_easing);
	lrg_editor_end_group (self->editor);
	editor_app_set_status (self, "Keyed %u channel(s) at %.2f s", keys, self->timeline_time);
	return keys;
}

/* ==========================================================================
 * Play-in-editor
 * ========================================================================== */

void
editor_timeline_play_begin (LrgEditorApp *self)
{
	LrgLevel *level = lrg_editor_get_level (self->editor);
	guint     i;

	editor_timeline_end_preview (self);
	g_hash_table_remove_all (self->timeline_play);
	for (i = 0; i < lrg_level_get_n_timelines (level); i++)
	{
		LrgTimeline *tl = lrg_level_get_timeline (level, i);

		if (lrg_timeline_get_autoplay (tl))
			g_hash_table_add (self->timeline_play, g_strdup (lrg_timeline_get_name (tl)));
	}
}

void
editor_timeline_play_apply (LrgEditorApp *self,
                            gdouble       play_time)
{
	LrgLevel      *level = lrg_editor_get_level (self->editor);
	GHashTableIter iter;
	gpointer       name;

	/* Nodes are restored from the play snapshot when play stops. */
	g_hash_table_iter_init (&iter, self->timeline_play);
	while (g_hash_table_iter_next (&iter, &name, NULL))
	{
		gint idx = lrg_level_find_timeline (level, name);

		if (idx >= 0)
		{
			LrgTimeline *tl = lrg_level_get_timeline (level, (guint) idx);

			lrg_timeline_apply (tl, lrg_level_get_root (level), lrg_timeline_wrap_time (tl, (gfloat) play_time));
		}
	}
}

/* ==========================================================================
 * Panel
 * ========================================================================== */

static void
select_key (LrgEditorApp *self,
            const gchar  *guid,
            const gchar  *channel,
            gfloat        time)
{
	g_free (self->timeline_key_guid);
	g_free (self->timeline_key_channel);
	self->timeline_key_guid = g_strdup (guid);
	self->timeline_key_channel = g_strdup (channel);
	self->timeline_key_time = time;
	self->timeline_key_valid = (guid != NULL);
}

static void
easing_combo (LrgEditorApp *self,
              LrgTimeline  *tl)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
	g_autofree const gchar **nicks = g_new0 (const gchar *, klass->n_values + 1);
	guint                     i;
	gint                      current = self->timeline_easing;

	for (i = 0; i < klass->n_values; i++)
		nicks[i] = klass->values[i].value_nick;
	lrg_gui_set_next_item_width (self->gui, 150.0f);
	if (lrg_gui_combo (self->gui, "##easing", &current, (const gchar * const *) nicks, (gint) klass->n_values))
	{
		self->timeline_easing = current;
		/* Also re-ease the selected key(s). */
		if (tl != NULL && self->timeline_key_valid)
		{
			LrgNode *node = lrg_editor_find_node (self->editor, self->timeline_key_guid);

			if (node != NULL)
				lrg_editor_timeline_set_easing (self->editor, lrg_timeline_get_name (tl), node,
				                                self->timeline_key_channel, self->timeline_key_time,
				                                (LrgEasingType) current);
		}
	}
	lrg_gui_set_item_tooltip (self->gui, "Easing for new keys and the selected key");
}

static void
toolbar (LrgEditorApp *self,
         LrgTimeline  *tl)
{
	LrgLevel *level = lrg_editor_get_level (self->editor);
	gboolean  entered;
	guint     i;

	/* Timeline chooser. */
	lrg_gui_set_next_item_width (self->gui, 140.0f);
	if (lrg_gui_begin_combo (self->gui, "##timeline", tl != NULL ? lrg_timeline_get_name (tl) : "(none)"))
	{
		for (i = 0; i < lrg_level_get_n_timelines (level); i++)
		{
			const gchar *name = lrg_timeline_get_name (lrg_level_get_timeline (level, i));

			if (lrg_gui_selectable (self->gui, name, tl != NULL && g_strcmp0 (name, lrg_timeline_get_name (tl)) == 0, 0))
			{
				editor_timeline_end_preview (self);
				g_free (self->timeline_name);
				self->timeline_name = g_strdup (name);
				self->timeline_time = 0.0f;
			}
		}
		lrg_gui_end_combo (self->gui);
	}
	lrg_gui_same_line (self->gui, -1.0f);
	lrg_gui_set_next_item_width (self->gui, 110.0f);
	entered = lrg_gui_input_text (self->gui, "##new-timeline", "new name", self->timeline_new_name,
	                              LRG_GUI_INPUT_TEXT_ENTER_RETURNS_TRUE);
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "New") || entered)
	{
		const gchar      *base = self->timeline_new_name->len > 0 ? self->timeline_new_name->str : "Timeline";
		g_autofree gchar *name = g_strdup (base);
		guint             n = 2;

		while (lrg_level_find_timeline (level, name) >= 0)
		{
			g_free (name);
			name = g_strdup_printf ("%s %u", base, n++);
		}
		if (lrg_editor_timeline_add (self->editor, name, 2.0f, FALSE))
		{
			editor_timeline_end_preview (self);
			g_free (self->timeline_name);
			self->timeline_name = g_strdup (name);
			self->timeline_time = 0.0f;
		}
		g_string_truncate (self->timeline_new_name, 0);
	}
	if (tl == NULL)
		return;
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "Delete##timeline"))
	{
		editor_timeline_end_preview (self);
		lrg_editor_timeline_remove (self->editor, lrg_timeline_get_name (tl));
		return;
	}

	/* Transport. */
	if (lrg_gui_button (self->gui, self->timeline_playing ? "Pause" : "Play"))
		lrg_editor_actions_activate (self->actions, "timeline.play");
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "Stop"))
		lrg_editor_actions_activate (self->actions, "timeline.stop");
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "Key (I)"))
		lrg_editor_actions_activate (self->actions, "timeline.key");
	lrg_gui_same_line (self->gui, -1.0f);
	if (lrg_gui_button (self->gui, "Delete Key") && self->timeline_key_valid)
		lrg_editor_actions_activate (self->actions, "timeline.delete-key");
	lrg_gui_same_line (self->gui, -1.0f);
	easing_combo (self, tl);
	lrg_gui_same_line (self->gui, -1.0f);
	{
		gboolean loop = lrg_timeline_get_loop (tl);
		gboolean autoplay = lrg_timeline_get_autoplay (tl);
		gfloat   length = lrg_timeline_get_length (tl);

		if (lrg_gui_checkbox (self->gui, "Loop", &loop))
			lrg_editor_timeline_configure (self->editor, lrg_timeline_get_name (tl), NULL, 0.0f, loop, -1);
		lrg_gui_same_line (self->gui, -1.0f);
		if (lrg_gui_checkbox (self->gui, "Autoplay", &autoplay))
			lrg_editor_timeline_configure (self->editor, lrg_timeline_get_name (tl), NULL, 0.0f, -1, autoplay);
		lrg_gui_same_line (self->gui, -1.0f);
		lrg_gui_set_next_item_width (self->gui, 80.0f);
		if (lrg_gui_drag_float (self->gui, "s##length", &length, 0.01f, 0.05f, 3600.0f, "%.2f"))
			lrg_editor_timeline_configure (self->editor, lrg_timeline_get_name (tl), NULL, length, -1, -1);
		lrg_gui_same_line (self->gui, -1.0f);
		lrg_gui_textf (self->gui, "%.2f s", self->timeline_time);
	}
}

void
editor_panel_timeline (LrgEditorApp *self)
{
	LrgGuiStyle    *style = lrg_gui_context_get_style (self->gui);
	LrgTimeline    *tl = editor_timeline_current (self);
	LrgGuiDrawList *dl;
	gfloat          x0, y0, w, h, label_w, track_x, track_w, row_h, length, mx, my;
	guint           i, k, n;
	gboolean        hit_key = FALSE;

	toolbar (self, tl);
	tl = editor_timeline_current (self);
	if (tl == NULL)
	{
		lrg_gui_text_wrapped (self->gui, "No timeline. Type a name and press New, or select nodes and press "
		                                 "I to key their transform.");
		return;
	}

	/* Layout: labels | ruler + key lanes. */
	lrg_gui_get_cursor (self->gui, &x0, &y0);
	lrg_gui_get_content_avail (self->gui, &w, &h);
	n = lrg_timeline_get_n_tracks (tl);
	row_h = lrg_gui_get_frame_height (self->gui);
	h = MAX (h, RULER_H + row_h * MAX (n, 1u));
	label_w = MAX (120.0f, w * LABEL_W_RATIO);
	track_x = x0 + label_w;
	track_w = MAX (40.0f, w - label_w - 8.0f);
	length = MAX (lrg_timeline_get_length (tl), 0.01f);

	lrg_gui_invisible_button (self->gui, "##timeline-area", w, RULER_H + row_h * MAX (n, 1u));
	dl = lrg_gui_get_draw_list (self->gui);
	lrg_gui_get_mouse (self->gui, &mx, &my);

	/* Ruler with a tick per 10% of the clip. */
	{
		LrgGuiRect ruler = { track_x, y0, track_w, RULER_H };

		lrg_gui_draw_list_rect_filled (dl, &ruler, style->colors[LRG_GUI_COL_FRAME_BG], 0.0f);
		for (i = 0; i <= 10; i++)
		{
			gfloat           tx = track_x + track_w * i / 10.0f;
			g_autofree gchar *label = g_strdup_printf ("%.1f", length * i / 10.0f);

			lrg_gui_draw_list_line (dl, tx, y0 + RULER_H * 0.5f, tx, y0 + RULER_H, style->colors[LRG_GUI_COL_BORDER],
			                        1.0f);
			if (i < 10)
				lrg_gui_draw_list_text (dl, tx + 2.0f, y0 + 1.0f, label, -1, style->font_size * 0.75f,
				                        style->colors[LRG_GUI_COL_TEXT_DISABLED], FALSE);
		}
	}

	/* Track rows: label + keys. */
	for (i = 0; i < n; i++)
	{
		LrgTimelineTrack *t = lrg_timeline_get_track (tl, i);
		const gchar      *guid = lrg_timeline_track_get_node_guid (t);
		LrgNode          *node = lrg_editor_find_node (self->editor, guid);
		gfloat            ry = y0 + RULER_H + row_h * i;
		g_autofree gchar *label = g_strdup_printf ("%s  %s", node != NULL ? lrg_node_get_name (node) : "(missing)",
		                                           lrg_timeline_track_get_channel (t));
		LrgGuiRect        lane = { track_x, ry, track_w, row_h };

		if (i % 2 == 0)
			lrg_gui_draw_list_rect_filled (dl, &lane, (style->colors[LRG_GUI_COL_FRAME_BG] & 0xffffff00u) | 0x60u, 0.0f);
		lrg_gui_draw_list_text (dl, x0 + 4.0f, ry + style->frame_padding_y, label, -1, style->font_size * 0.9f,
		                        node != NULL ? style->colors[LRG_GUI_COL_TEXT] : style->colors[LRG_GUI_COL_ERROR],
		                        FALSE);

		for (k = 0; k < lrg_timeline_track_get_n_keys (t); k++)
		{
			gfloat   kt = 0.0f;
			gfloat   kx, ky = ry + row_h * 0.5f;
			gboolean selected;

			lrg_timeline_track_get_key (t, k, &kt, NULL, NULL);
			kx = track_x + track_w * CLAMP (kt / length, 0.0f, 1.0f);
			selected = self->timeline_key_valid && g_strcmp0 (self->timeline_key_guid, guid) == 0 &&
			           (self->timeline_key_channel == NULL ||
			            g_strcmp0 (self->timeline_key_channel, lrg_timeline_track_get_channel (t)) == 0) &&
			           fabsf (kt - self->timeline_key_time) < 0.001f;
			lrg_gui_draw_list_triangle (dl, kx, ky - KEY_HALF, kx - KEY_HALF, ky, kx + KEY_HALF, ky,
			                            selected ? style->colors[LRG_GUI_COL_ACCENT] : 0xf9e2afffu);
			lrg_gui_draw_list_triangle (dl, kx - KEY_HALF, ky, kx, ky + KEY_HALF, kx + KEY_HALF, ky,
			                            selected ? style->colors[LRG_GUI_COL_ACCENT] : 0xf9e2afffu);

			/* Press on a key: select it and start a drag. */
			if (!hit_key && lrg_gui_is_item_clicked (self->gui, LRG_GUI_MOUSE_LEFT) &&
			    fabsf (mx - kx) <= KEY_HALF + 2.0f && fabsf (my - ky) <= row_h * 0.5f)
			{
				select_key (self, guid, lrg_timeline_track_get_channel (t), kt);
				self->timeline_key_drag = TRUE;
				self->timeline_key_drag_from = kt;
				hit_key = TRUE;
			}
		}
	}

	/* Press elsewhere scrubs (and clears the key selection). */
	if (!hit_key && lrg_gui_is_item_clicked (self->gui, LRG_GUI_MOUSE_LEFT) && mx >= track_x)
	{
		self->timeline_scrubbing = TRUE;
		select_key (self, NULL, NULL, 0.0f);
	}
	if (self->timeline_scrubbing)
	{
		if (lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_LEFT))
			editor_timeline_seek (self, (mx - track_x) / track_w * length);
		else
			self->timeline_scrubbing = FALSE;
	}

	/* Key drag: retime (one undo step for the whole drag). */
	if (self->timeline_key_drag)
	{
		if (lrg_gui_is_mouse_down (self->gui, LRG_GUI_MOUSE_LEFT))
		{
			gfloat   to = CLAMP ((mx - track_x) / track_w * length, 0.0f, length);
			LrgNode *node = lrg_editor_find_node (self->editor, self->timeline_key_guid);

			to = roundf (to * 100.0f) / 100.0f;   /* snap to 10 ms */
			if (node != NULL && fabsf (to - self->timeline_key_time) >= 0.005f &&
			    lrg_editor_timeline_move_key (self->editor, lrg_timeline_get_name (tl), node,
			                                  self->timeline_key_channel, self->timeline_key_time, to,
			                                  fabsf (self->timeline_key_time - self->timeline_key_drag_from) > 0.0f) > 0)
				self->timeline_key_time = to;
		}
		else
			self->timeline_key_drag = FALSE;
	}

	/* Playhead. */
	{
		gfloat px = track_x + track_w * CLAMP (self->timeline_time / length, 0.0f, 1.0f);

		lrg_gui_draw_list_line (dl, px, y0, px, y0 + RULER_H + row_h * MAX (n, 1u),
		                        style->colors[LRG_GUI_COL_ERROR], 2.0f);
	}
}
