/* lrg-editor-console-panel.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Console, History and Problems panels, plus the small helpers every
 * utility panel shares (toggle chips, hints, column layout).
 *
 * Console: the LrgEditorLog ring is mirrored into rows (formatted once,
 * when a record first appears) and the rows passing the level / text
 * filter live in a GListStore shown by a virtualized LrgListView, so a
 * frame formats nothing and the list only builds the visible rows.
 * Records only ever leave the ring from the front and arrive at the
 * back with increasing serials, so mirroring is incremental; any
 * mismatch (a clear, a race with a logging thread) rebuilds. The
 * command line runs:
 *
 *   help                      list commands and IPC methods
 *   clear                     clear the log
 *   :action-id                run an action ("::" lists them)
 *   method key=value ...      call an IPC method with simple parameters
 *   {"method": ...}           send a raw JSON request
 *
 * exactly as the 0.8.0 console did, so everything the IPC server offers
 * is also available interactively. Up / Down walk the command history.
 *
 * History: one row per undo step and per redo step, rebuilt when the
 * editor changes; a click jumps with lrg_editor_jump_to_history().
 *
 * Problems: the app's validation results (re-run debounced after every
 * edit by the app), a click selects the node, a double click opens a
 * script problem's file (code.open when the code panel registered it)
 * or frames the node.
 */

#include "config.h"

#define LRG_EDITOR_PANEL_INTERNAL
#include "lrg-editor-console-panel.h"
#include "lrg-editor-app-private.h"
#include "../../ui/lrg-label.h"
#include "../../ui/lrg-hbox.h"
#include "../../ui/lrg-list-view.h"
#include "../../ui/lrg-text-input.h"
#include "../../ui/lrg-custom-widget.h"
#include "../../ui/lrg-ui-event.h"
#include "../../paint/lrg-paint-list.h"
#include "../../paint/lrg-paint-types.h"
#include "../../text/lrg-font-manager.h"
#include <json-glib/json-glib.h>
#include <raylib.h>
#include <string.h>

/* Shown above the most recent command echo when the history is empty */
#define CONSOLE_PLACEHOLDER "Command: help, :action, method key=value, {json}"

/* Commands remembered for Up / Down */
#define CONSOLE_HISTORY_MAX (200)

/* ==========================================================================
 * Shared helpers: chips
 *
 * A chip is an LrgCustomWidget: an optional icon, a label and an
 * optional badge (a count) on a rounded pill. Checked chips (toggles)
 * are filled with their tint; hover and press blend in through the
 * widget's springs. Clicking (or Enter / Space while focused) calls the
 * chip's function.
 * ========================================================================== */

typedef struct
{
	LrgIcon         icon;
	gchar          *label;
	gchar          *badge;
	LrgThemeColor   tint;
	gboolean        checked;
	gboolean        compact;   /* label hidden (icon and badge only) */
	EditorChipFunc  func;
	gpointer        user_data;
} EditorChip;

static void
editor_chip_free (gpointer data)
{
	EditorChip *c = data;

	g_free (c->label);
	g_free (c->badge);
	g_free (c);
}

static EditorChip *
chip_data (LrgWidget *chip)
{
	return g_object_get_data (G_OBJECT (chip), "lrg-editor-chip");
}

/* Sizes from the theme: icon box, horizontal padding, gap, font size */
static void
chip_metrics (LrgWidget *widget,
              gfloat    *icon,
              gfloat    *pad,
              gfloat    *gap,
              gfloat    *font)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	*font = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	*icon = MAX (*font + 2.0f, 12.0f);
	*pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X) * 0.75f;
	*gap = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING) * 0.75f);
}

static void
chip_measure (LrgCustomWidget     *widget,
              gfloat               avail_width,
              gfloat               avail_height,
              LrgCustomWidgetSize *size,
              gpointer             user_data)
{
	EditorChip *c = user_data;
	LrgTheme   *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont    *font = lrg_theme_get_ui_font (theme);
	gfloat      icon, pad, gap, fs, tw = 0.0f, th = 0.0f, w;

	chip_metrics (LRG_WIDGET (widget), &icon, &pad, &gap, &fs);
	w = pad * 2.0f;
	if (c->icon != LRG_ICON_NONE)
		w += icon;
	if (c->label != NULL && *c->label != '\0' && !(c->compact && c->icon != LRG_ICON_NONE))
	{
		lrg_paint_measure_text (font, c->label, fs, 0.0f, &tw, &th);
		w += tw + (c->icon != LRG_ICON_NONE ? gap : 0.0f);
	}
	if (c->badge != NULL && *c->badge != '\0')
	{
		lrg_paint_measure_text (font, c->badge, fs * 0.9f, 0.0f, &tw, &th);
		w += gap + MAX (tw + 8.0f, fs + 4.0f);
	}
	size->width = w;
	size->height = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT) * 0.85f;
}

static void
chip_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	EditorChip     *c = user_data;
	LrgWidget      *w = LRG_WIDGET (widget);
	LrgTheme       *theme = lrg_widget_get_theme (w);
	GrlFont        *font = lrg_theme_get_ui_font (theme);
	const GrlColor *tint = lrg_theme_get_color (theme, c->tint);
	gfloat          hover = lrg_widget_get_hover_progress (w);
	gfloat          press = lrg_widget_get_press_progress (w);
	gfloat          radius = height * 0.5f;
	gfloat          icon, pad, gap, fs, cx, tw = 0.0f, th = 0.0f;
	GrlColor        fill, border, fg;
	gboolean        enabled = lrg_widget_get_enabled (w);

	chip_metrics (w, &icon, &pad, &gap, &fs);

	/* Pill: tinted when checked, a soft control shade on hover */
	if (c->checked)
	{
		lrg_paint_color_with_alpha (tint, 0.20f + 0.10f * hover + 0.08f * press, &fill);
		lrg_paint_color_with_alpha (tint, 0.65f, &border);
	}
	else
	{
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER),
		                            0.25f + 0.55f * hover + 0.2f * press, &fill);
		lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER), 0.55f + 0.3f * hover,
		                            &border);
	}
	lrg_paint_list_rect (list, x, y, width, height, radius, &fill);
	lrg_paint_list_border (list, x, y, width, height, radius, 1.0f, &border);

	/* Foreground: tinted icon, text in the normal colour (muted when off) */
	fg = *lrg_theme_get_color (theme, c->checked || hover > 0.5f ? LRG_THEME_COLOR_TEXT : LRG_THEME_COLOR_TEXT_MUTED);
	if (!enabled)
		fg = *lrg_theme_get_color (theme, LRG_THEME_COLOR_DISABLED);
	cx = x + pad;
	if (c->icon != LRG_ICON_NONE)
	{
		lrg_paint_list_icon (list, c->icon, cx, y + (height - icon) * 0.5f, icon, 0.0f,
		                     (c->tint == LRG_THEME_COLOR_TEXT || !enabled) ? &fg : tint);
		cx += icon + gap;
	}
	if (c->label != NULL && *c->label != '\0' && !(c->compact && c->icon != LRG_ICON_NONE))
	{
		lrg_paint_measure_text (font, c->label, fs, 0.0f, &tw, &th);
		lrg_paint_list_text (list, font, c->label, cx, y + (height - fs) * 0.5f, fs, 0.0f, &fg);
		cx += tw + gap;
	}

	/* Badge: a small pill with the count */
	if (c->badge != NULL && *c->badge != '\0')
	{
		gfloat   bfs = fs * 0.9f;
		gfloat   bw, bh = bfs + 4.0f;
		GrlColor bfill;

		lrg_paint_measure_text (font, c->badge, bfs, 0.0f, &tw, &th);
		bw = MAX (tw + 8.0f, bh);
		lrg_paint_color_with_alpha (tint, c->checked ? 0.85f : 0.35f, &bfill);
		lrg_paint_list_rect (list, cx, y + (height - bh) * 0.5f, bw, bh, bh * 0.5f, &bfill);
		lrg_paint_list_text (list, font, c->badge, cx + (bw - tw) * 0.5f, y + (height - bfs) * 0.5f, bfs, 0.0f,
		                     lrg_theme_get_color (theme, c->checked ? LRG_THEME_COLOR_TEXT_ON_ACCENT
		                                                            : LRG_THEME_COLOR_TEXT));
	}
}

static gboolean
chip_event (LrgCustomWidget  *widget,
            const LrgUIEvent *event,
            gpointer          user_data)
{
	LrgWidget *w = LRG_WIDGET (widget);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return lrg_ui_event_get_button (event) == 0;
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (lrg_widget_contains_point (w, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)))
			editor_chip_activate (w);
		return TRUE;
	case LRG_UI_EVENT_ACTIVATE:
		editor_chip_activate (w);
		return TRUE;
	default:
		return FALSE;
	}
}

LrgWidget *
editor_chip_new (const gchar    *name,
                 LrgIcon         icon,
                 const gchar    *label,
                 EditorChipFunc  func,
                 gpointer        user_data)
{
	static const LrgCustomWidgetFuncs funcs = { chip_measure, chip_paint, NULL, chip_event, { NULL } };
	LrgCustomWidget *widget = lrg_custom_widget_new ();
	EditorChip      *c = g_new0 (EditorChip, 1);

	c->icon = icon;
	c->label = g_strdup (label);
	c->tint = LRG_THEME_COLOR_ACCENT;
	c->func = func;
	c->user_data = user_data;
	g_object_set_data (G_OBJECT (widget), "lrg-editor-chip", c);
	lrg_custom_widget_set_funcs (widget, &funcs, c, editor_chip_free);
	lrg_widget_set_name (LRG_WIDGET (widget), name);
	lrg_widget_set_focusable (LRG_WIDGET (widget), TRUE);
	lrg_widget_set_valign (LRG_WIDGET (widget), LRG_ALIGN_CENTER);
	return LRG_WIDGET (widget);
}

void
editor_chip_set_label (LrgWidget   *chip,
                       const gchar *label)
{
	EditorChip *c = chip_data (chip);

	if (c == NULL || g_strcmp0 (c->label, label) == 0)
		return;
	g_free (c->label);
	c->label = g_strdup (label);
	lrg_widget_queue_layout (chip);
}

void
editor_chip_set_badge (LrgWidget   *chip,
                       const gchar *badge)
{
	EditorChip *c = chip_data (chip);

	if (c == NULL || g_strcmp0 (c->badge, badge) == 0)
		return;
	g_free (c->badge);
	c->badge = g_strdup (badge);
	lrg_widget_queue_layout (chip);
}

void
editor_chip_set_icon (LrgWidget *chip,
                      LrgIcon    icon)
{
	EditorChip *c = chip_data (chip);

	if (c != NULL && c->icon != icon)
	{
		c->icon = icon;
		lrg_widget_queue_layout (chip);
	}
}

void
editor_chip_set_tint (LrgWidget     *chip,
                      LrgThemeColor  role)
{
	EditorChip *c = chip_data (chip);

	if (c != NULL)
		c->tint = role;
}

void
editor_chip_set_checked (LrgWidget *chip,
                         gboolean   checked)
{
	EditorChip *c = chip_data (chip);

	if (c == NULL)
		return;
	c->checked = !!checked;
	if (c->checked)
		lrg_widget_set_state_flags (chip, LRG_WIDGET_STATE_CHECKED);
	else
		lrg_widget_unset_state_flags (chip, LRG_WIDGET_STATE_CHECKED);
}

gboolean
editor_chip_get_checked (LrgWidget *chip)
{
	EditorChip *c = chip_data (chip);

	return c != NULL && c->checked;
}

void
editor_chip_activate (LrgWidget *chip)
{
	EditorChip *c = chip_data (chip);

	if (c != NULL && c->func != NULL && lrg_widget_get_enabled (chip))
		c->func (chip, c->user_data);
}

void
editor_chip_set_compact (LrgWidget *chip,
                         gboolean   compact)
{
	EditorChip *c = chip_data (chip);

	if (c != NULL && c->compact != !!compact)
	{
		c->compact = !!compact;
		lrg_widget_queue_layout (chip);
	}
}

/* Every chip directly or indirectly inside @widget. */
static void
collect_chips (LrgWidget *widget,
               GPtrArray *out)
{
	GList *l;

	if (chip_data (widget) != NULL)
	{
		g_ptr_array_add (out, widget);
		return;
	}
	if (!LRG_IS_CONTAINER (widget))
		return;
	for (l = lrg_container_get_children (LRG_CONTAINER (widget)); l != NULL; l = l->next)
		collect_chips (LRG_WIDGET (l->data), out);
}

/*
 * editor_panel_fit_toolbar:
 *
 * Narrow panels: when @toolbar would be wider than @width with every
 * chip labelled, the chips that have an icon drop their labels (their
 * badges stay); wide enough again, the labels come back.
 */
void
editor_panel_fit_toolbar (LrgWidget *toolbar,
                          gfloat     width)
{
	g_autoptr(GPtrArray) chips = g_ptr_array_new ();
	gfloat               pw = 0.0f, ph = 0.0f, extra = 0.0f;
	gboolean             compact = FALSE;
	guint                i;

	if (toolbar == NULL || width <= 0.0f)
		return;
	collect_chips (toolbar, chips);

	/* The width the bar would need with every label shown: its current
	 * preferred width plus the labels compact chips hide */
	for (i = 0; i < chips->len; i++)
	{
		LrgWidget  *chip = g_ptr_array_index (chips, i);
		EditorChip *c = chip_data (chip);
		gfloat      icon, pad, gap, fs, tw = 0.0f, th = 0.0f;

		if (!c->compact || c->label == NULL || *c->label == '\0' || c->icon == LRG_ICON_NONE)
			continue;
		compact = TRUE;
		chip_metrics (chip, &icon, &pad, &gap, &fs);
		lrg_paint_measure_text (lrg_theme_get_ui_font (lrg_widget_get_theme (chip)), c->label, fs, 0.0f, &tw, &th);
		extra += tw + gap;
	}
	lrg_widget_get_preferred_size (toolbar, &pw, &ph);

	/* Only flip when the answer changes, so layouts settle */
	if (!compact && pw > width)
	{
		for (i = 0; i < chips->len; i++)
			editor_chip_set_compact (g_ptr_array_index (chips, i), TRUE);
	}
	else if (compact && pw + extra <= width)
	{
		for (i = 0; i < chips->len; i++)
			editor_chip_set_compact (g_ptr_array_index (chips, i), FALSE);
	}
}

/* --------------------------------------------------------------------------
 * Hints: an icon over word-wrapped muted text, centred (empty states)
 * -------------------------------------------------------------------------- */

static void
hint_paint (LrgCustomWidget *widget,
            LrgPaintList    *list,
            gfloat           x,
            gfloat           y,
            gfloat           width,
            gfloat           height,
            gpointer         user_data)
{
	const gchar         *text = user_data;
	LrgTheme            *theme = lrg_widget_get_theme (LRG_WIDGET (widget));
	GrlFont             *font = lrg_theme_get_ui_font (theme);
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat               icon = fs * 2.2f;
	gfloat               line_h = fs * 1.35f;
	gfloat               max_w = MAX (60.0f, width - 24.0f);
	g_auto(GStrv)        words = g_strsplit (text, " ", -1);
	g_autoptr(GPtrArray) lines = g_ptr_array_new_with_free_func (g_free);
	GString             *cur = g_string_new (NULL);
	GrlColor             tint;
	gfloat               ty;
	guint                i;

	/* Greedy word wrap */
	for (i = 0; words[i] != NULL; i++)
	{
		g_autofree gchar *probe = cur->len > 0 ? g_strconcat (cur->str, " ", words[i], NULL) : g_strdup (words[i]);
		gfloat            tw = 0.0f, th = 0.0f;

		lrg_paint_measure_text (font, probe, fs, 0.0f, &tw, &th);
		if (tw > max_w && cur->len > 0)
		{
			g_ptr_array_add (lines, g_string_free (cur, FALSE));
			cur = g_string_new (words[i]);
		}
		else
			g_string_assign (cur, probe);
	}
	g_ptr_array_add (lines, g_string_free (cur, FALSE));

	/* Icon above the block, all centred */
	ty = y + (height - icon - 10.0f - line_h * (gfloat) lines->len) * 0.5f;
	lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.55f, &tint);
	lrg_paint_list_icon (list, LRG_ICON_INFO, x + (width - icon) * 0.5f, ty, icon, 0.0f, &tint);
	ty += icon + 10.0f;
	for (i = 0; i < lines->len; i++)
	{
		const gchar *line = g_ptr_array_index (lines, i);
		gfloat       tw = 0.0f, th = 0.0f;

		lrg_paint_measure_text (font, line, fs, 0.0f, &tw, &th);
		lrg_paint_list_text (list, font, line, x + (width - tw) * 0.5f, ty, fs, 0.0f,
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		ty += line_h;
	}
}

LrgWidget *
editor_panel_hint_new (const gchar *name,
                       const gchar *text)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, hint_paint, NULL, NULL, { NULL } };
	LrgCustomWidget *hint = lrg_custom_widget_new ();

	lrg_widget_set_name (LRG_WIDGET (hint), name);
	lrg_custom_widget_set_preferred_width (hint, 200.0f);
	lrg_custom_widget_set_preferred_height (hint, 80.0f);
	lrg_custom_widget_set_funcs (hint, &funcs, g_strdup (text), g_free);
	return LRG_WIDGET (hint);
}

/* The "ui-mono" face when something registered it, else the UI font. */
GrlFont *
editor_panel_mono_font (LrgWidget *widget)
{
	GrlFont *font = lrg_font_manager_get_font (lrg_font_manager_get_default (), "ui-mono");

	return font != NULL ? font : lrg_theme_get_ui_font (lrg_widget_get_theme (widget));
}

/*
 * editor_panel_place_column:
 *
 * The utility panels' layout: @top (a toolbar) at its preferred height,
 * @bottom (a command line, nullable) at its preferred height, and @fill
 * taking what is left, all inset by the theme's padding.
 */
void
editor_panel_place_column (LrgContainer *self,
                           LrgWidget    *top,
                           LrgWidget    *fill,
                           LrgWidget    *bottom)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat    w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat    h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat    pad = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_Y));
	gfloat    gap = MAX (4.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING) * 0.75f);
	gfloat    pw, ph, y = pad, bottom_h = 0.0f;

	if (top != NULL && lrg_widget_get_visible (top))
	{
		lrg_widget_get_preferred_size (top, &pw, &ph);
		lrg_widget_place_in_slot (top, pad, y, MAX (0.0f, w - pad * 2.0f), ph);
		y += ph + gap;
	}
	if (bottom != NULL && lrg_widget_get_visible (bottom))
	{
		lrg_widget_get_preferred_size (bottom, &pw, &ph);
		bottom_h = ph;
		lrg_widget_place_in_slot (bottom, pad, MAX (y, h - pad - ph), MAX (0.0f, w - pad * 2.0f), ph);
		bottom_h += gap;
	}
	if (fill != NULL)
		lrg_widget_place_in_slot (fill, pad, y, MAX (0.0f, w - pad * 2.0f),
		                          MAX (0.0f, h - y - pad - bottom_h));
}

/* ==========================================================================
 * Rows (internal list item)
 * ========================================================================== */

typedef enum
{
	ROW_LOG,
	ROW_HISTORY,
	ROW_PROBLEM
} RowKind;

#define LRG_TYPE_EDITOR_CONSOLE_ROW (lrg_editor_console_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgEditorConsoleRow, lrg_editor_console_row, LRG, EDITOR_CONSOLE_ROW, GObject)

/*
 * One list row. Log rows: serial, GLib level, stamp, domain, message,
 * file / line. History rows: index (the undo count a click jumps to),
 * level 0 applied / 1 undone, message = description. Problem rows:
 * index into the app's problem array, level = severity, domain = node
 * path, message, code (in stamp), guid, file.
 */
struct _LrgEditorConsoleRow
{
	GObject  parent_instance;

	RowKind  kind;
	guint64  serial;
	gint     level;
	guint    index;
	gchar   *text;     /* the whole row, as copied */
	gchar   *stamp;
	gchar   *domain;
	gchar   *message;
	gchar   *folded;   /* casefolded message + domain, for the search */
	gchar   *file;
	gint     line;
	gchar   *guid;
};

G_DEFINE_FINAL_TYPE (LrgEditorConsoleRow, lrg_editor_console_row, G_TYPE_OBJECT)

static void
lrg_editor_console_row_finalize (GObject *object)
{
	LrgEditorConsoleRow *self = LRG_EDITOR_CONSOLE_ROW (object);

	g_free (self->text);
	g_free (self->stamp);
	g_free (self->domain);
	g_free (self->message);
	g_free (self->folded);
	g_free (self->file);
	g_free (self->guid);
	G_OBJECT_CLASS (lrg_editor_console_row_parent_class)->finalize (object);
}

static void
lrg_editor_console_row_class_init (LrgEditorConsoleRowClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_console_row_finalize;
}

static void
lrg_editor_console_row_init (LrgEditorConsoleRow *self)
{
}

/* Formats a log record once, as the 0.8.0 console printed it. */
static LrgEditorConsoleRow *
row_new_log (const LrgEditorLogRecord *r)
{
	LrgEditorConsoleRow  *row = g_object_new (LRG_TYPE_EDITOR_CONSOLE_ROW, NULL);
	g_autoptr(GDateTime)  when = g_date_time_new_from_unix_local_usec (r->timestamp);
	g_autofree gchar     *both = NULL;

	row->kind = ROW_LOG;
	row->serial = r->serial;
	row->level = (gint) r->level;
	row->stamp = (when != NULL) ? g_date_time_format (when, "%H:%M:%S") : g_strdup ("--:--:--");
	row->domain = g_strdup (r->domain);
	row->message = g_strdup (r->message != NULL ? r->message : "");
	row->text = g_strdup_printf ("%s %-8s %s%s%s", row->stamp, lrg_editor_log_level_to_string (r->level),
	                             r->domain != NULL ? r->domain : "", r->domain != NULL ? ": " : "", row->message);
	both = g_strconcat (row->message, "\n", r->domain != NULL ? r->domain : "", NULL);
	row->folded = g_utf8_casefold (both, -1);
	row->file = g_strdup (r->file);
	row->line = r->line;
	return row;
}

/* Maps a GLib level onto a console level group. */
static LrgEditorConsoleLevels
level_group (gint level)
{
	if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL))
		return LRG_EDITOR_CONSOLE_LEVEL_ERROR;
	if (level & G_LOG_LEVEL_WARNING)
		return LRG_EDITOR_CONSOLE_LEVEL_WARNING;
	if (level & G_LOG_LEVEL_DEBUG)
		return LRG_EDITOR_CONSOLE_LEVEL_DEBUG;
	return LRG_EDITOR_CONSOLE_LEVEL_MESSAGE;
}

/* Icon and colour role of a level group. */
static void
level_look (LrgEditorConsoleLevels  group,
            LrgIcon                *icon,
            LrgThemeColor          *role)
{
	switch (group)
	{
	case LRG_EDITOR_CONSOLE_LEVEL_ERROR:
		*icon = LRG_ICON_CROSS;
		*role = LRG_THEME_COLOR_DANGER;
		break;
	case LRG_EDITOR_CONSOLE_LEVEL_WARNING:
		*icon = LRG_ICON_WARNING;
		*role = LRG_THEME_COLOR_WARNING;
		break;
	case LRG_EDITOR_CONSOLE_LEVEL_DEBUG:
		*icon = LRG_ICON_GEAR;
		*role = LRG_THEME_COLOR_TEXT_MUTED;
		break;
	case LRG_EDITOR_CONSOLE_LEVEL_MESSAGE:
	case LRG_EDITOR_CONSOLE_LEVEL_NONE:
	case LRG_EDITOR_CONSOLE_LEVEL_ALL:
	default:
		*icon = LRG_ICON_INFO;
		*role = LRG_THEME_COLOR_LINK;
		break;
	}
}

/* --------------------------------------------------------------------------
 * Row widgets: one LrgCustomWidget per visible row, rebound on scroll
 * -------------------------------------------------------------------------- */

typedef struct
{
	LrgEditorConsoleRow *row;   /* owned ref, the bound item */
} RowView;

static void
row_view_free (gpointer data)
{
	RowView *v = data;

	g_clear_object (&v->row);
	g_free (v);
}

/* Draws @text clipped to @max_w, returning the width used. */
static gfloat
paint_text_clip (LrgPaintList   *list,
                 GrlFont        *font,
                 const gchar    *text,
                 gfloat          x,
                 gfloat          y,
                 gfloat          size,
                 gfloat          max_w,
                 const GrlColor *color)
{
	gfloat w = 0.0f, h = 0.0f;

	if (text == NULL || *text == '\0' || max_w <= 0.0f)
		return 0.0f;
	lrg_paint_measure_text (font, text, size, 0.0f, &w, &h);
	lrg_paint_list_push_clip (list, x, y - 2.0f, max_w, size + 6.0f);
	lrg_paint_list_text (list, font, text, x, y, size, 0.0f, color);
	lrg_paint_list_pop_clip (list);
	return MIN (w, max_w);
}

static void
row_paint (LrgCustomWidget *widget,
           LrgPaintList    *list,
           gfloat           x,
           gfloat           y,
           gfloat           width,
           gfloat           height,
           gpointer         user_data)
{
	RowView             *v = user_data;
	LrgEditorConsoleRow *row = v->row;
	LrgWidget           *w = LRG_WIDGET (widget);
	LrgTheme            *theme = lrg_widget_get_theme (w);
	GrlFont             *font = lrg_theme_get_ui_font (theme);
	GrlFont             *mono = editor_panel_mono_font (w);
	gfloat               fs = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	gfloat               icon = MIN (height - 6.0f, fs + 2.0f);
	gfloat               ty = y + (height - fs) * 0.5f;
	gfloat               cx = x + 6.0f;
	gfloat               right = x + width - 6.0f;
	const GrlColor      *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
	const GrlColor      *text = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT);
	LrgIcon              ic;
	LrgThemeColor        role;

	if (row == NULL)
		return;

	switch (row->kind)
	{
	case ROW_LOG:
		{
			LrgEditorConsoleLevels group = level_group (row->level);
			const GrlColor        *tint;

			/* level icon | time | domain | message (coloured by level) */
			level_look (group, &ic, &role);
			tint = lrg_theme_get_color (theme, role);
			lrg_paint_list_icon (list, ic, cx, y + (height - icon) * 0.5f, icon, 0.0f, tint);
			cx += icon + 6.0f;
			cx += paint_text_clip (list, mono, row->stamp, cx, ty, fs, right - cx, muted) + 8.0f;
			if (row->domain != NULL)
			{
				GrlColor dom;

				lrg_paint_color_mix (muted, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 0.45f, &dom);
				cx += paint_text_clip (list, mono, row->domain, cx, ty, fs, right - cx, &dom) + 8.0f;
			}
			paint_text_clip (list, mono, row->message, cx, ty, fs, right - cx,
			                 group == LRG_EDITOR_CONSOLE_LEVEL_MESSAGE ? text
			                 : group == LRG_EDITOR_CONSOLE_LEVEL_DEBUG ? muted : tint);
			if (row->file != NULL)
			{
				/* a small link mark: the row opens its source on activate */
				lrg_paint_list_icon (list, LRG_ICON_FILE, right - icon, y + (height - icon) * 0.5f, icon, 0.0f,
				                     lrg_theme_get_color (theme, LRG_THEME_COLOR_LINK));
			}
		}
		break;

	case ROW_HISTORY:
		{
			/* step number in a dot | description; undone steps are dimmed,
			 * the current state carries an accent bar and a check */
			gboolean          undone = (row->level == 1);
			gboolean          current = (row->level == 2);
			g_autofree gchar *num = g_strdup_printf ("%u", row->index);
			const GrlColor   *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
			gfloat            d = height - 8.0f, nw = 0.0f, nh = 0.0f;
			GrlColor          dot;

			if (current)
				lrg_paint_list_rect (list, x, y + 3.0f, 3.0f, height - 6.0f, 1.5f, accent);
			lrg_paint_color_with_alpha (undone ? muted : accent, undone ? 0.25f : 0.35f, &dot);
			lrg_paint_list_circle (list, cx + d * 0.5f, y + height * 0.5f, d * 0.5f, &dot);
			lrg_paint_measure_text (font, num, fs * 0.85f, 0.0f, &nw, &nh);
			lrg_paint_list_text (list, font, num, cx + (d - nw) * 0.5f, y + (height - fs * 0.85f) * 0.5f,
			                     fs * 0.85f, 0.0f, undone ? muted : text);
			cx += d + 8.0f;
			paint_text_clip (list, font, row->message, cx, ty, fs, right - cx - icon - 4.0f, undone ? muted : text);
			if (current)
				lrg_paint_list_icon (list, LRG_ICON_CHECK, right - icon, y + (height - icon) * 0.5f, icon, 0.0f,
				                     accent);
			else if (undone)
				lrg_paint_list_icon (list, LRG_ICON_REFRESH, right - icon, y + (height - icon) * 0.5f, icon, 0.0f,
				                     muted);
		}
		break;

	case ROW_PROBLEM:
		{
			/* severity icon | node path | message | code */
			const GrlColor *tint;
			gfloat          cw = 0.0f, ch = 0.0f;

			ic = row->level == LRG_EDITOR_PROBLEM_ERROR ? LRG_ICON_CROSS
			     : row->level == LRG_EDITOR_PROBLEM_WARNING ? LRG_ICON_WARNING : LRG_ICON_INFO;
			role = row->level == LRG_EDITOR_PROBLEM_ERROR ? LRG_THEME_COLOR_DANGER
			       : row->level == LRG_EDITOR_PROBLEM_WARNING ? LRG_THEME_COLOR_WARNING : LRG_THEME_COLOR_LINK;
			tint = lrg_theme_get_color (theme, role);
			lrg_paint_list_icon (list, ic, cx, y + (height - icon) * 0.5f, icon, 0.0f, tint);
			cx += icon + 8.0f;

			/* the code as a chip on the right */
			if (row->stamp != NULL)
			{
				GrlColor bg;

				lrg_paint_measure_text (mono, row->stamp, fs * 0.85f, 0.0f, &cw, &ch);
				lrg_paint_color_with_alpha (tint, 0.18f, &bg);
				lrg_paint_list_rect (list, right - cw - 10.0f, y + 4.0f, cw + 10.0f, height - 8.0f,
				                     (height - 8.0f) * 0.5f, &bg);
				lrg_paint_list_text (list, mono, row->stamp, right - cw - 5.0f, y + (height - fs * 0.85f) * 0.5f,
				                     fs * 0.85f, 0.0f, tint);
				right -= cw + 16.0f;
			}
			if (row->domain != NULL && *row->domain != '\0')
				cx += paint_text_clip (list, font, row->domain, cx, ty, fs, MIN (right - cx, width * 0.35f), text)
				      + 10.0f;
			paint_text_clip (list, font, row->message, cx, ty, fs, right - cx, muted);
		}
		break;

	default:
		break;
	}
}

static LrgWidget *
row_create (LrgListView *view,
            gpointer     user_data)
{
	static const LrgCustomWidgetFuncs funcs = { NULL, row_paint, NULL, NULL, { NULL } };
	LrgCustomWidget *widget = lrg_custom_widget_new ();
	RowView         *v = g_new0 (RowView, 1);

	lrg_custom_widget_set_funcs (widget, &funcs, v, row_view_free);
	g_object_set_data (G_OBJECT (widget), "lrg-editor-row-view", v);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (widget), TRUE);
	return LRG_WIDGET (widget);
}

static void
row_bind (LrgListView *view,
          LrgWidget   *widget,
          GObject     *item,
          guint        position,
          gpointer     user_data)
{
	RowView *v = g_object_get_data (G_OBJECT (widget), "lrg-editor-row-view");

	if (v != NULL)
		g_set_object (&v->row, LRG_EDITOR_CONSOLE_ROW (item));
}

/* A list view of console rows with the row factory installed. */
static LrgListView *
row_list_new (const gchar *name)
{
	LrgListView *list = lrg_list_view_new ();

	lrg_widget_set_name (LRG_WIDGET (list), name);
	lrg_list_view_set_factory (list, row_create, row_bind, NULL, NULL);
	lrg_list_view_set_selection_mode (list, LRG_SELECTION_MODE_SINGLE);
	return list;
}

/* ==========================================================================
 * LrgEditorConsoleLevels
 * ========================================================================== */

GType
lrg_editor_console_levels_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_EDITOR_CONSOLE_LEVEL_NONE, "LRG_EDITOR_CONSOLE_LEVEL_NONE", "none" },
			{ LRG_EDITOR_CONSOLE_LEVEL_ERROR, "LRG_EDITOR_CONSOLE_LEVEL_ERROR", "error" },
			{ LRG_EDITOR_CONSOLE_LEVEL_WARNING, "LRG_EDITOR_CONSOLE_LEVEL_WARNING", "warning" },
			{ LRG_EDITOR_CONSOLE_LEVEL_MESSAGE, "LRG_EDITOR_CONSOLE_LEVEL_MESSAGE", "message" },
			{ LRG_EDITOR_CONSOLE_LEVEL_DEBUG, "LRG_EDITOR_CONSOLE_LEVEL_DEBUG", "debug" },
			{ LRG_EDITOR_CONSOLE_LEVEL_ALL, "LRG_EDITOR_CONSOLE_LEVEL_ALL", "all" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgEditorConsoleLevels"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * LrgEditorConsolePanel
 * ========================================================================== */

struct _LrgEditorConsolePanel
{
	LrgContainer            parent_instance;

	LrgEditorApp           *app;          /* weak */
	LrgEditorLog           *log;          /* ref */

	/* Widgets (owned by the container) */
	LrgWidget              *toolbar;
	LrgWidget              *chips[4];     /* error, warning, message, debug */
	LrgWidget              *search;
	LrgWidget              *follow;
	LrgListView            *list;
	LrgWidget              *cmdbar;
	LrgTextInput           *command;

	/* Model */
	GPtrArray              *rows;         /* LrgEditorConsoleRow*, mirrors the ring */
	GListStore             *shown;        /* rows passing the filter */
	LrgEditorConsoleLevels  levels;
	gchar                  *search_text;
	gchar                  *needle;       /* casefolded search, NULL for none */
	gboolean                autoscroll;
	guint                   counts_n;     /* ring size the counts were taken at */
	guint64                 counts_serial;

	/* Command history */
	GPtrArray              *history;
	gint                    history_pos;  /* -1: editing a new line */
};

G_DEFINE_FINAL_TYPE (LrgEditorConsolePanel, lrg_editor_console_panel, LRG_TYPE_CONTAINER)

static const LrgEditorConsoleLevels chip_levels[4] = {
	LRG_EDITOR_CONSOLE_LEVEL_ERROR, LRG_EDITOR_CONSOLE_LEVEL_WARNING,
	LRG_EDITOR_CONSOLE_LEVEL_MESSAGE, LRG_EDITOR_CONSOLE_LEVEL_DEBUG
};

static gboolean
row_passes (LrgEditorConsolePanel *self,
            LrgEditorConsoleRow   *row)
{
	if (!(level_group (row->level) & self->levels))
		return FALSE;
	return self->needle == NULL || strstr (row->folded, self->needle) != NULL;
}

/* Rebuilds the shown store from the mirrored rows (a filter changed). */
static void
console_refilter (LrgEditorConsolePanel *self)
{
	g_autoptr(GPtrArray) pass = g_ptr_array_new ();
	guint                i;

	for (i = 0; i < self->rows->len; i++)
	{
		LrgEditorConsoleRow *row = g_ptr_array_index (self->rows, i);

		if (row_passes (self, row))
			g_ptr_array_add (pass, row);
	}
	g_list_store_splice (self->shown, 0, g_list_model_get_n_items (G_LIST_MODEL (self->shown)),
	                     pass->pdata, pass->len);
	if (self->autoscroll && pass->len > 0)
		lrg_list_view_scroll_to (self->list, pass->len - 1, FALSE);
}

/* The chips show "Errors 3" style counts of the whole ring. */
static void
console_update_counts (LrgEditorConsolePanel *self)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (chip_levels); i++)
	{
		static const GLogLevelFlags masks[4] = {
			G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL, G_LOG_LEVEL_WARNING,
			G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO, G_LOG_LEVEL_DEBUG
		};
		g_autofree gchar *badge = g_strdup_printf ("%u", lrg_editor_log_count_level (self->log, masks[i]));

		editor_chip_set_badge (self->chips[i], badge);
	}
}

/*
 * console_sync:
 *
 * Brings the mirrored rows in line with the log ring (drop rows whose
 * records left it, format the new ones) and the shown store in line
 * with the rows (remove dropped rows from the front, append new rows
 * that pass the filter). A ring that does not start or end where the
 * mirror expects (cleared, or raced) rebuilds everything.
 */
static void
console_sync (LrgEditorConsolePanel *self)
{
	guint                     n = lrg_editor_log_get_n_records (self->log);
	const LrgEditorLogRecord *first = (n > 0) ? lrg_editor_log_get_record (self->log, 0) : NULL;
	const LrgEditorLogRecord *last = (n > 0) ? lrg_editor_log_get_record (self->log, n - 1) : NULL;
	guint                     drop = 0, shown_drop = 0, old_len, i;
	gboolean                  appended = FALSE;

	/* Nothing changed since the last sync: same size, same newest record */
	if (n == self->rows->len && (n == 0 || (last != NULL &&
	    ((LrgEditorConsoleRow *) g_ptr_array_index (self->rows, n - 1))->serial == last->serial)))
		return;

	/* Rows whose records left the ring (oldest first) */
	while (drop < self->rows->len &&
	       (first == NULL || ((LrgEditorConsoleRow *) g_ptr_array_index (self->rows, drop))->serial < first->serial))
		drop++;
	if (drop > 0)
	{
		guint64 keep_from = (drop < self->rows->len)
		                    ? ((LrgEditorConsoleRow *) g_ptr_array_index (self->rows, drop))->serial : G_MAXUINT64;
		guint   n_shown = g_list_model_get_n_items (G_LIST_MODEL (self->shown));

		while (shown_drop < n_shown)
		{
			g_autoptr(LrgEditorConsoleRow) row = g_list_model_get_item (G_LIST_MODEL (self->shown), shown_drop);

			if (row->serial >= keep_from)
				break;
			shown_drop++;
		}
		if (shown_drop > 0)
			g_list_store_splice (self->shown, 0, shown_drop, NULL, 0);
		g_ptr_array_remove_range (self->rows, 0, drop);
	}

	/* The remaining rows must be the ring's first records, in order */
	if (self->rows->len > n ||
	    (self->rows->len > 0 &&
	     (lrg_editor_log_get_record (self->log, self->rows->len - 1) == NULL ||
	      lrg_editor_log_get_record (self->log, self->rows->len - 1)->serial !=
	      ((LrgEditorConsoleRow *) g_ptr_array_index (self->rows, self->rows->len - 1))->serial)))
	{
		g_ptr_array_set_size (self->rows, 0);
		g_list_store_remove_all (self->shown);
	}

	/* Format the records that arrived; append those that pass */
	old_len = self->rows->len;
	for (i = old_len; i < n; i++)
	{
		const LrgEditorLogRecord *r = lrg_editor_log_get_record (self->log, i);
		LrgEditorConsoleRow      *row;

		if (r == NULL)
			break;
		row = row_new_log (r);
		g_ptr_array_add (self->rows, row);
		if (row_passes (self, row))
		{
			g_list_store_append (self->shown, row);
			appended = TRUE;
		}
	}
	if (old_len == 0 && self->rows->len > 0 && g_list_model_get_n_items (G_LIST_MODEL (self->shown)) == 0)
		console_refilter (self);

	console_update_counts (self);
	if (appended && self->autoscroll)
		lrg_list_view_scroll_to (self->list, g_list_model_get_n_items (G_LIST_MODEL (self->shown)) - 1, FALSE);
}

static void
console_set_levels_internal (LrgEditorConsolePanel  *self,
                             LrgEditorConsoleLevels  levels)
{
	guint i;

	levels &= LRG_EDITOR_CONSOLE_LEVEL_ALL;
	for (i = 0; i < G_N_ELEMENTS (chip_levels); i++)
		editor_chip_set_checked (self->chips[i], (levels & chip_levels[i]) != 0);
	if (levels == self->levels)
		return;
	self->levels = levels;
	console_sync (self);
	console_refilter (self);
}

static void
on_level_chip (LrgWidget *chip,
               gpointer   user_data)
{
	LrgEditorConsolePanel *self = LRG_EDITOR_CONSOLE_PANEL (user_data);
	guint                  i;

	for (i = 0; i < G_N_ELEMENTS (chip_levels); i++)
		if (self->chips[i] == chip)
			console_set_levels_internal (self, self->levels ^ chip_levels[i]);
}

static void
on_search_changed (LrgTextInput *input,
                   gpointer      user_data)
{
	lrg_editor_console_panel_set_search (LRG_EDITOR_CONSOLE_PANEL (user_data), lrg_text_input_get_text (input));
}

static void
on_follow_chip (LrgWidget *chip,
                gpointer   user_data)
{
	LrgEditorConsolePanel *self = LRG_EDITOR_CONSOLE_PANEL (user_data);

	lrg_editor_console_panel_set_autoscroll (self, !self->autoscroll);
}

/* The selected row's text, or NULL. */
static const gchar *
selected_text (LrgEditorConsolePanel *self)
{
	guint                          pos = lrg_list_view_get_selected (self->list);
	g_autoptr(LrgEditorConsoleRow) row = NULL;

	if (pos == LRG_LIST_VIEW_INVALID_POSITION)
		return NULL;
	row = g_list_model_get_item (G_LIST_MODEL (self->shown), pos);
	return row != NULL ? row->text : NULL;
}

static void
console_copy (LrgEditorConsolePanel *self)
{
	const gchar *text;

	if (self->app == NULL)
		return;
	console_sync (self);
	text = selected_text (self);
	if (text == NULL)
	{
		editor_app_set_status (self->app, "Select a log row to copy");
		return;
	}
	editor_app_set_clipboard (self->app, text);
	editor_app_set_status (self->app, "Copied the log row");
}

static void
console_copy_all (LrgEditorConsolePanel *self)
{
	GString *all;
	guint    i, n;

	if (self->app == NULL)
		return;
	console_sync (self);
	all = g_string_new (NULL);
	n = g_list_model_get_n_items (G_LIST_MODEL (self->shown));
	for (i = 0; i < n; i++)
	{
		g_autoptr(LrgEditorConsoleRow) row = g_list_model_get_item (G_LIST_MODEL (self->shown), i);

		g_string_append (all, row->text);
		g_string_append_c (all, '\n');
	}
	editor_app_set_clipboard (self->app, all->str);
	editor_app_set_status (self->app, "Copied %u log row(s)", n);
	g_string_free (all, TRUE);
}

static void
console_clear (LrgEditorConsolePanel *self)
{
	lrg_editor_log_clear (self->log);
	console_sync (self);
}

static void on_copy_chip (LrgWidget *c, gpointer d)     { console_copy (LRG_EDITOR_CONSOLE_PANEL (d)); }
static void on_copy_all_chip (LrgWidget *c, gpointer d) { console_copy_all (LRG_EDITOR_CONSOLE_PANEL (d)); }
static void on_clear_chip (LrgWidget *c, gpointer d)    { console_clear (LRG_EDITOR_CONSOLE_PANEL (d)); }

/* Double click / Enter on a row with a source location opens it. */
static void
on_row_activated (LrgListView *list,
                  guint        position,
                  gpointer     user_data)
{
	LrgEditorConsolePanel         *self = LRG_EDITOR_CONSOLE_PANEL (user_data);
	g_autoptr(LrgEditorConsoleRow) row = g_list_model_get_item (G_LIST_MODEL (self->shown), position);

	if (row != NULL && row->file != NULL && self->app != NULL)
		editor_app_open_file (self->app, row->file, MAX (1, row->line), 1);
}

/* --------------------------------------------------------------------------
 * Commands (0.8.0 editor_console_execute)
 * -------------------------------------------------------------------------- */

/* Turns `key=value key2="two words"` into a JSON params object. */
static gchar *
params_from_words (gchar **words)
{
	g_autoptr(JsonBuilder)   b = json_builder_new ();
	g_autoptr(JsonGenerator) gen = json_generator_new ();
	g_autoptr(JsonNode)      root = NULL;
	guint                    i;

	json_builder_begin_object (b);
	for (i = 0; words[i] != NULL; i++)
	{
		gchar       *eq = strchr (words[i], '=');
		const gchar *v;
		gchar       *end = NULL;
		gdouble      num;

		if (eq == NULL)
			continue;
		*eq = '\0';
		v = eq + 1;
		json_builder_set_member_name (b, words[i]);

		/* true / false, numbers, [x,y,z], else strings */
		if (g_strcmp0 (v, "true") == 0 || g_strcmp0 (v, "false") == 0)
			json_builder_add_boolean_value (b, v[0] == 't');
		else if (v[0] == '[')
		{
			g_auto(GStrv) parts = g_strsplit_set (v, "[], ", -1);
			guint         k;

			json_builder_begin_array (b);
			for (k = 0; parts[k] != NULL; k++)
				if (*parts[k] != '\0')
					json_builder_add_double_value (b, g_ascii_strtod (parts[k], NULL));
			json_builder_end_array (b);
		}
		else if ((num = g_ascii_strtod (v, &end)), end != v && *end == '\0')
		{
			if (strchr (v, '.') == NULL && strchr (v, 'e') == NULL)
				json_builder_add_int_value (b, (gint64) num);
			else
				json_builder_add_double_value (b, num);
		}
		else
			json_builder_add_string_value (b, v);
	}
	json_builder_end_object (b);

	root = json_builder_get_root (b);
	json_generator_set_root (gen, root);
	return json_generator_to_data (gen, NULL);
}

static void
console_execute (LrgEditorConsolePanel *self,
                 const gchar           *line)
{
	LrgEditorApp     *app = self->app;
	g_autofree gchar *trimmed = g_strstrip (g_strdup (line));
	g_autofree gchar *echo = NULL;

	if (*trimmed == '\0' || app == NULL)
		return;

	/* History: drop an identical previous entry, cap the length */
	if (self->history->len == 0 ||
	    g_strcmp0 (g_ptr_array_index (self->history, self->history->len - 1), trimmed) != 0)
		g_ptr_array_add (self->history, g_strdup (trimmed));
	if (self->history->len > CONSOLE_HISTORY_MAX)
		g_ptr_array_remove_index (self->history, 0);
	self->history_pos = -1;

	echo = g_strdup_printf ("> %s", trimmed);
	lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "console", echo);

	if (g_strcmp0 (trimmed, "clear") == 0)
	{
		console_clear (self);
		return;
	}

	if (g_strcmp0 (trimmed, "help") == 0)
	{
		GList   *methods = lrg_editor_server_list_methods (app->server);
		GString *out = g_string_new ("Commands: help, clear, :action, method key=value..., raw JSON.\nMethods:");
		GList   *l;

		for (l = methods; l != NULL; l = l->next)
			g_string_append_printf (out, " %s", (const gchar *) l->data);
		g_list_free (methods);
		lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "console", out->str);
		g_string_free (out, TRUE);
		return;
	}

	if (trimmed[0] == ':')
	{
		if (trimmed[1] == ':' || trimmed[1] == '\0')
		{
			GList   *ids = lrg_editor_actions_list (app->actions);
			GString *out = g_string_new ("Actions:");
			GList   *l;

			for (l = ids; l != NULL; l = l->next)
				g_string_append_printf (out, " %s", (const gchar *) l->data);
			g_list_free (ids);
			lrg_editor_log_append (self->log, G_LOG_LEVEL_MESSAGE, "console", out->str);
			g_string_free (out, TRUE);
		}
		else if (!lrg_editor_actions_activate (app->actions, trimmed + 1))
		{
			g_autofree gchar *msg = g_strdup_printf ("No enabled action '%s' (try ::)", trimmed + 1);

			lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "console", msg);
		}
		return;
	}

	/* An IPC request, raw or in the short form */
	{
		g_autofree gchar *request = NULL;
		g_autofree gchar *response = NULL;

		if (trimmed[0] == '{')
			request = g_strdup (trimmed);
		else
		{
			g_auto(GStrv)     words = NULL;
			g_autofree gchar *params = NULL;
			g_autoptr(GError) error = NULL;

			if (!g_shell_parse_argv (trimmed, NULL, &words, &error))
			{
				lrg_editor_log_append (self->log, G_LOG_LEVEL_WARNING, "console", error->message);
				return;
			}
			params = params_from_words (words + 1);
			request = g_strdup_printf ("{\"id\":0,\"method\":\"%s\",\"params\":%s}", words[0], params);
		}

		response = lrg_editor_server_dispatch (app->server, request);
		if (response != NULL)
			lrg_editor_log_append (self->log,
			                       strstr (response, "\"error\"") != NULL ? G_LOG_LEVEL_WARNING : G_LOG_LEVEL_MESSAGE,
			                       "console", response);
	}
}

static void
on_command_submitted (LrgTextInput *input,
                      gpointer      user_data)
{
	LrgEditorConsolePanel *self = LRG_EDITOR_CONSOLE_PANEL (user_data);
	g_autofree gchar      *line = g_strdup (lrg_text_input_get_text (input));

	lrg_text_input_set_text (input, "");
	console_execute (self, line);
	console_sync (self);
}

static void
on_run_chip (LrgWidget *chip,
             gpointer   user_data)
{
	on_command_submitted (LRG_EDITOR_CONSOLE_PANEL (user_data)->command, user_data);
}

/* Up / Down in the command line walk the history (newest first). */
static gboolean
console_history_step (LrgEditorConsolePanel *self,
                      gint                   dir)
{
	gint len = (gint) self->history->len;

	if (len == 0)
		return TRUE;
	if (self->history_pos < 0)
		self->history_pos = (dir < 0) ? len - 1 : -1;
	else
		self->history_pos = CLAMP (self->history_pos + dir, -1, len);
	if (self->history_pos >= len)
		self->history_pos = -1;
	lrg_text_input_set_text (self->command,
	                         self->history_pos >= 0 ? g_ptr_array_index (self->history, self->history_pos) : "");
	lrg_text_input_set_cursor_position (self->command, (gint) g_utf8_strlen (lrg_text_input_get_text (self->command), -1));
	return TRUE;
}

/* --------------------------------------------------------------------------
 * Widget
 * -------------------------------------------------------------------------- */

static gboolean
lrg_editor_console_panel_handle_event (LrgWidget        *widget,
                                       const LrgUIEvent *event)
{
	LrgEditorConsolePanel *self = LRG_EDITOR_CONSOLE_PANEL (widget);

	/* Up / Down bubble here from the command line (it does not use them) */
	if (lrg_ui_event_get_event_type (event) == LRG_UI_EVENT_KEY_DOWN &&
	    lrg_ui_event_get_target (event) == LRG_WIDGET (self->command))
	{
		GrlKey key = lrg_ui_event_get_key (event);

		if (key == GRL_KEY_UP)
			return console_history_step (self, -1);
		if (key == GRL_KEY_DOWN)
			return console_history_step (self, 1);
	}
	return LRG_WIDGET_CLASS (lrg_editor_console_panel_parent_class)->handle_event (widget, event);
}

static gboolean
lrg_editor_console_panel_step (LrgWidget *widget,
                               gfloat     delta_time)
{
	/* Visible panels poll the ring each frame (records arrive from any
	 * thread; polling needs no main-loop iteration) */
	console_sync (LRG_EDITOR_CONSOLE_PANEL (widget));
	return FALSE;
}

static void
lrg_editor_console_panel_layout_children (LrgContainer *container)
{
	LrgEditorConsolePanel *self = LRG_EDITOR_CONSOLE_PANEL (container);

	editor_panel_fit_toolbar (self->toolbar, lrg_widget_get_width (LRG_WIDGET (self)) - 16.0f);
	editor_panel_place_column (container, self->toolbar, LRG_WIDGET (self->list), self->cmdbar);
}

static void
lrg_editor_console_panel_dispose (GObject *object)
{
	LrgEditorConsolePanel *self = LRG_EDITOR_CONSOLE_PANEL (object);

	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_console_panel_parent_class)->dispose (object);
}

static void
lrg_editor_console_panel_finalize (GObject *object)
{
	LrgEditorConsolePanel *self = LRG_EDITOR_CONSOLE_PANEL (object);

	g_clear_object (&self->log);
	g_clear_object (&self->shown);
	g_clear_pointer (&self->rows, g_ptr_array_unref);
	g_clear_pointer (&self->history, g_ptr_array_unref);
	g_free (self->search_text);
	g_free (self->needle);
	G_OBJECT_CLASS (lrg_editor_console_panel_parent_class)->finalize (object);
}

static void
lrg_editor_console_panel_class_init (LrgEditorConsolePanelClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_editor_console_panel_dispose;
	object_class->finalize = lrg_editor_console_panel_finalize;
	widget_class->handle_event = lrg_editor_console_panel_handle_event;
	widget_class->step = lrg_editor_console_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_console_panel_layout_children;
}

static void
add_to (LrgWidget *box,
        LrgWidget *child)
{
	lrg_container_add_child (LRG_CONTAINER (box), child);
	g_object_unref (child);
}

static void
lrg_editor_console_panel_init (LrgEditorConsolePanel *self)
{
	static const struct { const gchar *name; const gchar *label; LrgIcon icon; LrgThemeColor tint; } chips[4] = {
		{ "console-errors", "Errors", LRG_ICON_CROSS, LRG_THEME_COLOR_DANGER },
		{ "console-warnings", "Warnings", LRG_ICON_WARNING, LRG_THEME_COLOR_WARNING },
		{ "console-messages", "Messages", LRG_ICON_INFO, LRG_THEME_COLOR_LINK },
		{ "console-debug", "Debug", LRG_ICON_GEAR, LRG_THEME_COLOR_TEXT_MUTED },
	};
	LrgWidget *chip;
	guint      i;

	self->rows = g_ptr_array_new_with_free_func (g_object_unref);
	self->shown = g_list_store_new (LRG_TYPE_EDITOR_CONSOLE_ROW);
	self->history = g_ptr_array_new_with_free_func (g_free);
	self->history_pos = -1;
	self->levels = LRG_EDITOR_CONSOLE_LEVEL_ERROR | LRG_EDITOR_CONSOLE_LEVEL_WARNING |
	               LRG_EDITOR_CONSOLE_LEVEL_MESSAGE;
	self->autoscroll = TRUE;
	self->search_text = g_strdup ("");

	/* Toolbar: level chips with counts | search | follow, copy, clear */
	self->toolbar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->toolbar, "console-toolbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 6.0f);
	for (i = 0; i < G_N_ELEMENTS (chips); i++)
	{
		self->chips[i] = editor_chip_new (chips[i].name, chips[i].icon, chips[i].label, on_level_chip, self);
		editor_chip_set_tint (self->chips[i], chips[i].tint);
		editor_chip_set_checked (self->chips[i], (self->levels & chip_levels[i]) != 0);
		editor_chip_set_badge (self->chips[i], "0");
		add_to (self->toolbar, self->chips[i]);
	}
	self->search = LRG_WIDGET (lrg_text_input_new_with_placeholder ("Search log"));
	lrg_widget_set_name (self->search, "console-search");
	lrg_widget_set_fill_weight (self->search, 1.0f);
	lrg_widget_set_min_size (self->search, 100.0f, 0.0f);
	g_signal_connect (self->search, "text-changed", G_CALLBACK (on_search_changed), self);
	add_to (self->toolbar, self->search);
	self->follow = editor_chip_new ("console-follow", LRG_ICON_CHEVRON_DOWN, "Follow", on_follow_chip, self);
	editor_chip_set_checked (self->follow, TRUE);
	add_to (self->toolbar, self->follow);
	/* Copy / Copy All / Clear are icon-only to keep the bar narrow (they
	 * are also in the log's context menu with their names) */
	add_to (self->toolbar, editor_chip_new ("console-copy", LRG_ICON_FILE, NULL, on_copy_chip, self));
	add_to (self->toolbar, editor_chip_new ("console-copy-all", LRG_ICON_DOWNLOAD, NULL, on_copy_all_chip, self));
	chip = editor_chip_new ("console-clear", LRG_ICON_TRASH, NULL, on_clear_chip, self);
	editor_chip_set_tint (chip, LRG_THEME_COLOR_DANGER);
	add_to (self->toolbar, chip);
	lrg_container_add_child (LRG_CONTAINER (self), self->toolbar);
	g_object_unref (self->toolbar);

	/* The log */
	self->list = row_list_new ("console-list");
	lrg_list_view_set_model (self->list, G_LIST_MODEL (self->shown));
	g_signal_connect (self->list, "row-activated", G_CALLBACK (on_row_activated), self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->list));
	g_object_unref (self->list);

	/* Command line: a chevron prompt, the input, Run */
	self->cmdbar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->cmdbar, "console-cmdbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->cmdbar), 6.0f);
	self->command = lrg_text_input_new_with_placeholder (CONSOLE_PLACEHOLDER);
	lrg_widget_set_name (LRG_WIDGET (self->command), "console-command");
	lrg_widget_set_fill_weight (LRG_WIDGET (self->command), 1.0f);
	g_signal_connect (self->command, "submitted", G_CALLBACK (on_command_submitted), self);
	add_to (self->cmdbar, LRG_WIDGET (self->command));
	add_to (self->cmdbar, editor_chip_new ("console-run", LRG_ICON_CHEVRON_RIGHT, "Run", on_run_chip, self));
	lrg_container_add_child (LRG_CONTAINER (self), self->cmdbar);
	g_object_unref (self->cmdbar);
}

/* --------------------------------------------------------------------------
 * Actions (registered on the app's registry)
 * -------------------------------------------------------------------------- */

static void a_console_copy (gpointer d)     { console_copy (LRG_EDITOR_CONSOLE_PANEL (d)); }
static void a_console_copy_all (gpointer d) { console_copy_all (LRG_EDITOR_CONSOLE_PANEL (d)); }
static void a_console_clear (gpointer d)    { console_clear (LRG_EDITOR_CONSOLE_PANEL (d)); }

static void
a_console_focus (gpointer data)
{
	LrgEditorConsolePanel *self = LRG_EDITOR_CONSOLE_PANEL (data);
	LrgWidget             *root;

	if (self->app == NULL)
		return;
	editor_app_show_panel (self->app, "console");
	root = lrg_widget_get_root (LRG_WIDGET (self));
	if (LRG_IS_CANVAS (root))
		lrg_canvas_set_focused_widget (LRG_CANVAS (root), LRG_WIDGET (self->command));
}

LrgEditorConsolePanel *
lrg_editor_console_panel_new (LrgEditorApp *app)
{
	LrgEditorConsolePanel *self;
	LrgEditorActions      *actions;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_CONSOLE_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	self->log = g_object_ref (app->log);

	/* The panel lives as long as the app (it owns the dock panel), so
	 * the actions can point at it */
	actions = lrg_editor_app_get_actions (app);
	lrg_editor_actions_add (actions, "console.copy", "Copy Log Row", "Console", NULL, a_console_copy, NULL, NULL, self);
	lrg_editor_actions_add (actions, "console.copy-all", "Copy Log", "Console", NULL, a_console_copy_all, NULL, NULL,
	                        self);
	lrg_editor_actions_add (actions, "console.clear", "Clear Log", "Console", NULL, a_console_clear, NULL, NULL, self);
	lrg_editor_actions_add (actions, "console.focus", "Focus Command Line", "Console", NULL, a_console_focus,
	                        NULL, NULL, self);

	/* Right-click menu on the log */
	{
		g_autoptr(GMenu) menu = g_menu_new ();

		g_menu_append (menu, "Copy Row", "editor.console.copy");
		g_menu_append (menu, "Copy All", "editor.console.copy-all");
		g_menu_append (menu, "Clear", "editor.console.clear");
		lrg_widget_set_context_menu (LRG_WIDGET (self->list), G_MENU_MODEL (menu));
	}

	console_sync (self);
	return self;
}

LrgEditorApp *
lrg_editor_console_panel_get_app (LrgEditorConsolePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self), NULL);
	return self->app;
}

void
lrg_editor_console_panel_execute (LrgEditorConsolePanel *self,
                                  const gchar           *line)
{
	g_return_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self));
	g_return_if_fail (line != NULL);

	console_execute (self, line);
	console_sync (self);
}

LrgEditorConsoleLevels
lrg_editor_console_panel_get_levels (LrgEditorConsolePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self), LRG_EDITOR_CONSOLE_LEVEL_NONE);
	return self->levels;
}

void
lrg_editor_console_panel_set_levels (LrgEditorConsolePanel  *self,
                                     LrgEditorConsoleLevels  levels)
{
	g_return_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self));
	console_set_levels_internal (self, levels);
}

const gchar *
lrg_editor_console_panel_get_search (LrgEditorConsolePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self), NULL);
	return self->search_text;
}

void
lrg_editor_console_panel_set_search (LrgEditorConsolePanel *self,
                                     const gchar           *search)
{
	g_return_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self));

	if (search == NULL)
		search = "";
	if (g_strcmp0 (search, self->search_text) == 0)
		return;
	g_free (self->search_text);
	self->search_text = g_strdup (search);
	g_clear_pointer (&self->needle, g_free);
	if (*search != '\0')
		self->needle = g_utf8_casefold (search, -1);
	if (g_strcmp0 (lrg_text_input_get_text (LRG_TEXT_INPUT (self->search)), search) != 0)
		lrg_text_input_set_text (LRG_TEXT_INPUT (self->search), search);
	console_sync (self);
	console_refilter (self);
}

gboolean
lrg_editor_console_panel_get_autoscroll (LrgEditorConsolePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self), FALSE);
	return self->autoscroll;
}

void
lrg_editor_console_panel_set_autoscroll (LrgEditorConsolePanel *self,
                                         gboolean               autoscroll)
{
	guint n;

	g_return_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self));

	self->autoscroll = !!autoscroll;
	editor_chip_set_checked (self->follow, self->autoscroll);
	n = g_list_model_get_n_items (G_LIST_MODEL (self->shown));
	if (self->autoscroll && n > 0)
		lrg_list_view_scroll_to (self->list, n - 1, TRUE);
}

guint
lrg_editor_console_panel_get_n_shown (LrgEditorConsolePanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self), 0);
	console_sync (self);
	return g_list_model_get_n_items (G_LIST_MODEL (self->shown));
}

const gchar *
lrg_editor_console_panel_get_row_text (LrgEditorConsolePanel *self,
                                       guint                  index)
{
	LrgEditorConsoleRow *row;

	g_return_val_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self), NULL);
	console_sync (self);
	if (index >= g_list_model_get_n_items (G_LIST_MODEL (self->shown)))
		return NULL;
	/* The store holds a reference, so the borrowed text stays valid */
	row = g_list_model_get_item (G_LIST_MODEL (self->shown), index);
	g_object_unref (row);
	return row->text;
}

guint
lrg_editor_console_panel_get_level_count (LrgEditorConsolePanel  *self,
                                          LrgEditorConsoleLevels  level)
{
	GLogLevelFlags mask = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_CONSOLE_PANEL (self), 0);

	if (level & LRG_EDITOR_CONSOLE_LEVEL_ERROR)
		mask |= G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL;
	if (level & LRG_EDITOR_CONSOLE_LEVEL_WARNING)
		mask |= G_LOG_LEVEL_WARNING;
	if (level & LRG_EDITOR_CONSOLE_LEVEL_MESSAGE)
		mask |= G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO;
	if (level & LRG_EDITOR_CONSOLE_LEVEL_DEBUG)
		mask |= G_LOG_LEVEL_DEBUG;
	return lrg_editor_log_count_level (self->log, mask);
}

/* ==========================================================================
 * LrgEditorHistoryPanel
 * ========================================================================== */

struct _LrgEditorHistoryPanel
{
	LrgContainer   parent_instance;

	LrgEditorApp  *app;      /* weak */
	LrgEditor     *editor;   /* ref */
	LrgWidget     *toolbar;
	LrgWidget     *limit;    /* LrgLabel */
	LrgListView   *list;
	GListStore    *rows;
	gboolean       dirty;
	gboolean       syncing;  /* selecting the current row from code */
};

G_DEFINE_FINAL_TYPE (LrgEditorHistoryPanel, lrg_editor_history_panel, LRG_TYPE_CONTAINER)

static LrgEditorConsoleRow *
history_row (guint        index,
             const gchar *desc,
             gint         state)
{
	LrgEditorConsoleRow *row = g_object_new (LRG_TYPE_EDITOR_CONSOLE_ROW, NULL);

	row->kind = ROW_HISTORY;
	row->index = index;
	row->level = state;
	row->message = g_strdup (desc);
	row->text = g_strdup_printf ("%u. %s%s", index, desc, state == 1 ? "  (undone)" : "");
	return row;
}

/* Rebuilds the rows from the editor's undo and redo stacks. */
static void
history_rebuild (LrgEditorHistoryPanel *self)
{
	guint                n_undo = lrg_editor_get_undo_count (self->editor);
	guint                n_redo = lrg_editor_get_redo_count (self->editor);
	g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func (g_object_unref);
	g_autofree gchar    *limit = NULL;
	guint                i;

	self->dirty = FALSE;
	g_ptr_array_add (rows, history_row (0, "(initial state)", n_undo == 0 ? 2 : 0));
	for (i = 0; i < n_undo + n_redo; i++)
	{
		/* Redo index 0 is the next step to redo: list them in order */
		LrgEditorCommand *cmd = (i < n_undo) ? lrg_editor_get_undo_command (self->editor, i)
		                                     : lrg_editor_get_redo_command (self->editor, i - n_undo);
		g_autofree gchar *desc = (cmd != NULL) ? lrg_editor_command_describe (cmd) : g_strdup ("?");

		g_ptr_array_add (rows, history_row (i + 1, desc, i >= n_undo ? 1 : (i + 1 == n_undo ? 2 : 0)));
	}
	g_list_store_splice (self->rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->rows)), rows->pdata, rows->len);

	/* Keep the current state selected and in view */
	self->syncing = TRUE;
	lrg_list_view_select (self->list, n_undo);
	self->syncing = FALSE;
	lrg_list_view_scroll_to (self->list, n_undo, FALSE);

	limit = g_strdup_printf ("%u of %u steps", n_undo + n_redo, lrg_editor_get_history_limit (self->editor));
	lrg_label_set_text (LRG_LABEL (self->limit), limit);
}

static void
on_history_changed (LrgEditor *editor,
                    gpointer   user_data)
{
	LRG_EDITOR_HISTORY_PANEL (user_data)->dirty = TRUE;
}

/* ::level-replaced carries the new level before user_data */
static void
on_history_level_replaced (LrgEditor *editor,
                           LrgLevel  *level,
                           gpointer   user_data)
{
	LRG_EDITOR_HISTORY_PANEL (user_data)->dirty = TRUE;
}

static void
on_history_selection (LrgListView *list,
                      gpointer     user_data)
{
	LrgEditorHistoryPanel *self = LRG_EDITOR_HISTORY_PANEL (user_data);
	guint                  pos = lrg_list_view_get_selected (list);

	if (self->syncing || pos == LRG_LIST_VIEW_INVALID_POSITION)
		return;
	lrg_editor_history_panel_activate_row (self, pos);
}

static void
on_history_clear (LrgWidget *chip,
                  gpointer   user_data)
{
	LrgEditorHistoryPanel *self = LRG_EDITOR_HISTORY_PANEL (user_data);

	lrg_editor_clear_history (self->editor);
	history_rebuild (self);
}

static void on_history_undo (LrgWidget *c, gpointer d) { lrg_editor_undo (LRG_EDITOR_HISTORY_PANEL (d)->editor); }
static void on_history_redo (LrgWidget *c, gpointer d) { lrg_editor_redo (LRG_EDITOR_HISTORY_PANEL (d)->editor); }

static gboolean
lrg_editor_history_panel_step (LrgWidget *widget,
                               gfloat     delta_time)
{
	LrgEditorHistoryPanel *self = LRG_EDITOR_HISTORY_PANEL (widget);

	if (self->dirty)
		history_rebuild (self);
	return FALSE;
}

static void
lrg_editor_history_panel_layout_children (LrgContainer *container)
{
	LrgEditorHistoryPanel *self = LRG_EDITOR_HISTORY_PANEL (container);

	editor_panel_fit_toolbar (self->toolbar, lrg_widget_get_width (LRG_WIDGET (self)) - 16.0f);
	editor_panel_place_column (container, self->toolbar, LRG_WIDGET (self->list), NULL);
}

static void
lrg_editor_history_panel_dispose (GObject *object)
{
	LrgEditorHistoryPanel *self = LRG_EDITOR_HISTORY_PANEL (object);

	if (self->editor != NULL)
		g_signal_handlers_disconnect_by_data (self->editor, self);
	g_clear_object (&self->editor);
	g_clear_weak_pointer (&self->app);
	G_OBJECT_CLASS (lrg_editor_history_panel_parent_class)->dispose (object);
}

static void
lrg_editor_history_panel_finalize (GObject *object)
{
	g_clear_object (&LRG_EDITOR_HISTORY_PANEL (object)->rows);
	G_OBJECT_CLASS (lrg_editor_history_panel_parent_class)->finalize (object);
}

static void
lrg_editor_history_panel_class_init (LrgEditorHistoryPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_history_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_history_panel_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_history_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_history_panel_layout_children;
}

static void
lrg_editor_history_panel_init (LrgEditorHistoryPanel *self)
{
	LrgWidget *spacer;

	self->rows = g_list_store_new (LRG_TYPE_EDITOR_CONSOLE_ROW);

	self->toolbar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->toolbar, "history-toolbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 6.0f);
	add_to (self->toolbar, editor_chip_new ("history-undo", LRG_ICON_ARROW_LEFT, "Undo", on_history_undo, self));
	add_to (self->toolbar, editor_chip_new ("history-redo", LRG_ICON_ARROW_RIGHT, "Redo", on_history_redo, self));
	spacer = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_fill_weight (spacer, 1.0f);
	add_to (self->toolbar, spacer);
	self->limit = LRG_WIDGET (lrg_label_new (""));
	lrg_widget_set_name (self->limit, "history-limit");
	lrg_widget_add_class (self->limit, "dim-label");
	lrg_widget_set_valign (self->limit, LRG_ALIGN_CENTER);
	add_to (self->toolbar, self->limit);
	{
		LrgWidget *clear = editor_chip_new ("history-clear", LRG_ICON_TRASH, "Clear History", on_history_clear, self);

		editor_chip_set_tint (clear, LRG_THEME_COLOR_DANGER);
		add_to (self->toolbar, clear);
	}
	lrg_container_add_child (LRG_CONTAINER (self), self->toolbar);
	g_object_unref (self->toolbar);

	self->list = row_list_new ("history-list");
	lrg_list_view_set_model (self->list, G_LIST_MODEL (self->rows));
	g_signal_connect (self->list, "selection-changed", G_CALLBACK (on_history_selection), self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->list));
	g_object_unref (self->list);
}

LrgEditorHistoryPanel *
lrg_editor_history_panel_new (LrgEditorApp *app)
{
	LrgEditorHistoryPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_HISTORY_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	self->editor = g_object_ref (lrg_editor_app_get_editor (app));
	g_signal_connect (self->editor, "changed", G_CALLBACK (on_history_changed), self);
	g_signal_connect (self->editor, "level-replaced", G_CALLBACK (on_history_level_replaced), self);
	history_rebuild (self);
	return self;
}

guint
lrg_editor_history_panel_get_n_rows (LrgEditorHistoryPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_HISTORY_PANEL (self), 0);
	if (self->dirty)
		history_rebuild (self);
	return g_list_model_get_n_items (G_LIST_MODEL (self->rows));
}

const gchar *
lrg_editor_history_panel_get_row_label (LrgEditorHistoryPanel *self,
                                        guint                  index)
{
	LrgEditorConsoleRow *row;

	g_return_val_if_fail (LRG_IS_EDITOR_HISTORY_PANEL (self), NULL);
	if (self->dirty)
		history_rebuild (self);
	if (index >= g_list_model_get_n_items (G_LIST_MODEL (self->rows)))
		return NULL;
	row = g_list_model_get_item (G_LIST_MODEL (self->rows), index);
	g_object_unref (row);
	return row->text;
}

void
lrg_editor_history_panel_activate_row (LrgEditorHistoryPanel *self,
                                       guint                  index)
{
	g_return_if_fail (LRG_IS_EDITOR_HISTORY_PANEL (self));

	if (index > lrg_editor_get_undo_count (self->editor) + lrg_editor_get_redo_count (self->editor))
		return;
	if (index != lrg_editor_get_undo_count (self->editor))
		lrg_editor_jump_to_history (self->editor, index);
	history_rebuild (self);
}

/* ==========================================================================
 * LrgEditorProblemsPanel
 * ========================================================================== */

struct _LrgEditorProblemsPanel
{
	LrgContainer   parent_instance;

	LrgEditorApp  *app;          /* weak */
	LrgWidget     *toolbar;
	LrgWidget     *counts[3];    /* error, warning, info chips */
	LrgWidget     *hint;         /* "No problems found" */
	LrgListView   *list;
	GListStore    *rows;
	gpointer       seen;         /* app->problems the rows were built from */
	guint          seen_len;
	gboolean       syncing;
};

G_DEFINE_FINAL_TYPE (LrgEditorProblemsPanel, lrg_editor_problems_panel, LRG_TYPE_CONTAINER)

/* Rebuilds the rows when the app replaced its problem array. */
static void
problems_sync (LrgEditorProblemsPanel *self,
               gboolean                force)
{
	GPtrArray           *problems;
	g_autoptr(GPtrArray) rows = NULL;
	guint                n[3] = { 0, 0, 0 };
	guint                i;

	if (self->app == NULL)
		return;
	problems = self->app->problems;
	if (!force && problems == self->seen && (problems == NULL || problems->len == self->seen_len))
		return;
	self->seen = problems;
	self->seen_len = (problems != NULL) ? problems->len : 0;

	rows = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; problems != NULL && i < problems->len; i++)
	{
		LrgEditorProblem    *p = g_ptr_array_index (problems, i);
		LrgEditorConsoleRow *row = g_object_new (LRG_TYPE_EDITOR_CONSOLE_ROW, NULL);

		row->kind = ROW_PROBLEM;
		row->index = i;
		row->level = (gint) p->severity;
		row->stamp = g_strdup (p->code);
		row->domain = g_strdup (p->node_path);
		row->message = g_strdup (p->message);
		row->guid = g_strdup (p->node_guid);
		row->file = g_strdup (p->file);
		row->text = g_strdup_printf ("%-7s %s%s%s", lrg_editor_problem_severity_to_string (p->severity),
		                             p->node_path != NULL ? p->node_path : "", p->node_path != NULL ? ": " : "",
		                             p->message);
		if ((guint) p->severity < 3)
			n[2 - MIN ((guint) p->severity, 2u)]++;
		g_ptr_array_add (rows, row);
	}
	self->syncing = TRUE;
	g_list_store_splice (self->rows, 0, g_list_model_get_n_items (G_LIST_MODEL (self->rows)), rows->pdata, rows->len);
	self->syncing = FALSE;

	for (i = 0; i < 3; i++)
	{
		g_autofree gchar *badge = g_strdup_printf ("%u", n[i]);

		editor_chip_set_badge (self->counts[i], badge);
		editor_chip_set_checked (self->counts[i], n[i] > 0);
	}
	lrg_widget_set_visible (self->hint, rows->len == 0);
	lrg_widget_set_visible (LRG_WIDGET (self->list), rows->len > 0);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

static void
on_validate_chip (LrgWidget *chip,
                  gpointer   user_data)
{
	LrgEditorProblemsPanel *self = LRG_EDITOR_PROBLEMS_PANEL (user_data);

	if (self->app == NULL)
		return;
	editor_app_validate_now (self->app);
	problems_sync (self, TRUE);
	editor_app_set_status (self->app, "Validation: %u problem(s)", self->seen_len);
}

static void
on_problem_selection (LrgListView *list,
                      gpointer     user_data)
{
	LrgEditorProblemsPanel *self = LRG_EDITOR_PROBLEMS_PANEL (user_data);
	guint                   pos = lrg_list_view_get_selected (list);

	if (!self->syncing && pos != LRG_LIST_VIEW_INVALID_POSITION)
		lrg_editor_problems_panel_activate_row (self, pos, FALSE);
}

static void
on_problem_activated (LrgListView *list,
                      guint        position,
                      gpointer     user_data)
{
	lrg_editor_problems_panel_activate_row (LRG_EDITOR_PROBLEMS_PANEL (user_data), position, TRUE);
}

static gboolean
lrg_editor_problems_panel_step (LrgWidget *widget,
                                gfloat     delta_time)
{
	problems_sync (LRG_EDITOR_PROBLEMS_PANEL (widget), FALSE);
	return FALSE;
}

static void
lrg_editor_problems_panel_layout_children (LrgContainer *container)
{
	LrgEditorProblemsPanel *self = LRG_EDITOR_PROBLEMS_PANEL (container);

	editor_panel_fit_toolbar (self->toolbar, lrg_widget_get_width (LRG_WIDGET (self)) - 16.0f);
	editor_panel_place_column (container, self->toolbar, LRG_WIDGET (self->list), NULL);
	if (self->hint != NULL && lrg_widget_get_visible (self->hint))
		editor_panel_place_column (container, self->toolbar, self->hint, NULL);
}

static void
lrg_editor_problems_panel_dispose (GObject *object)
{
	g_clear_weak_pointer (&LRG_EDITOR_PROBLEMS_PANEL (object)->app);
	G_OBJECT_CLASS (lrg_editor_problems_panel_parent_class)->dispose (object);
}

static void
lrg_editor_problems_panel_finalize (GObject *object)
{
	g_clear_object (&LRG_EDITOR_PROBLEMS_PANEL (object)->rows);
	G_OBJECT_CLASS (lrg_editor_problems_panel_parent_class)->finalize (object);
}

static void
lrg_editor_problems_panel_class_init (LrgEditorProblemsPanelClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = lrg_editor_problems_panel_dispose;
	G_OBJECT_CLASS (klass)->finalize = lrg_editor_problems_panel_finalize;
	LRG_WIDGET_CLASS (klass)->step = lrg_editor_problems_panel_step;
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_editor_problems_panel_layout_children;
}

static void
lrg_editor_problems_panel_init (LrgEditorProblemsPanel *self)
{
	static const struct { const gchar *name; const gchar *label; LrgIcon icon; LrgThemeColor tint; } counts[3] = {
		{ "problems-errors", "Errors", LRG_ICON_CROSS, LRG_THEME_COLOR_DANGER },
		{ "problems-warnings", "Warnings", LRG_ICON_WARNING, LRG_THEME_COLOR_WARNING },
		{ "problems-info", "Info", LRG_ICON_INFO, LRG_THEME_COLOR_LINK },
	};
	LrgWidget *spacer, *validate;
	guint      i;

	self->rows = g_list_store_new (LRG_TYPE_EDITOR_CONSOLE_ROW);

	self->toolbar = LRG_WIDGET (lrg_hbox_new ());
	lrg_widget_set_name (self->toolbar, "problems-toolbar");
	lrg_container_set_spacing (LRG_CONTAINER (self->toolbar), 6.0f);
	for (i = 0; i < G_N_ELEMENTS (counts); i++)
	{
		self->counts[i] = editor_chip_new (counts[i].name, counts[i].icon, counts[i].label, NULL, NULL);
		editor_chip_set_tint (self->counts[i], counts[i].tint);
		editor_chip_set_badge (self->counts[i], "0");
		lrg_widget_set_focusable (self->counts[i], FALSE);
		add_to (self->toolbar, self->counts[i]);
	}
	spacer = LRG_WIDGET (lrg_label_new ("Checks run automatically after each change"));
	lrg_widget_add_class (spacer, "dim-label");
	lrg_widget_set_valign (spacer, LRG_ALIGN_CENTER);
	lrg_widget_set_fill_weight (spacer, 1.0f);
	add_to (self->toolbar, spacer);
	validate = editor_chip_new ("problems-validate", LRG_ICON_REFRESH, "Validate Now", on_validate_chip, self);
	editor_chip_set_checked (validate, TRUE);
	add_to (self->toolbar, validate);
	lrg_container_add_child (LRG_CONTAINER (self), self->toolbar);
	g_object_unref (self->toolbar);

	self->list = row_list_new ("problems-list");
	lrg_list_view_set_model (self->list, G_LIST_MODEL (self->rows));
	g_signal_connect (self->list, "selection-changed", G_CALLBACK (on_problem_selection), self);
	g_signal_connect (self->list, "row-activated", G_CALLBACK (on_problem_activated), self);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (self->list));
	g_object_unref (self->list);

	self->hint = editor_panel_hint_new ("problems-none", "No problems found");
	lrg_container_add_child (LRG_CONTAINER (self), self->hint);
	g_object_unref (self->hint);
}

LrgEditorProblemsPanel *
lrg_editor_problems_panel_new (LrgEditorApp *app)
{
	LrgEditorProblemsPanel *self;

	g_return_val_if_fail (LRG_IS_EDITOR_APP (app), NULL);

	self = g_object_new (LRG_TYPE_EDITOR_PROBLEMS_PANEL, NULL);
	g_set_weak_pointer (&self->app, app);
	problems_sync (self, TRUE);
	return self;
}

guint
lrg_editor_problems_panel_get_n_rows (LrgEditorProblemsPanel *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_PROBLEMS_PANEL (self), 0);
	problems_sync (self, FALSE);
	return g_list_model_get_n_items (G_LIST_MODEL (self->rows));
}

/* Opens @file at line 1: code.open when the code panel registered it. */
static void
problems_open_file (LrgEditorApp *app,
                    const gchar  *file)
{
	GList   *methods = lrg_editor_server_list_methods (app->server);
	gboolean has_code_open = g_list_find_custom (methods, "code.open", (GCompareFunc) g_strcmp0) != NULL;

	g_list_free (methods);
	if (has_code_open)
	{
		g_autoptr(JsonBuilder)   b = json_builder_new ();
		g_autoptr(JsonGenerator) gen = json_generator_new ();
		g_autoptr(JsonNode)      root = NULL;
		g_autofree gchar        *request = NULL;
		g_autofree gchar        *reply = NULL;

		json_builder_begin_object (b);
		json_builder_set_member_name (b, "id");
		json_builder_add_int_value (b, 0);
		json_builder_set_member_name (b, "method");
		json_builder_add_string_value (b, "code.open");
		json_builder_set_member_name (b, "params");
		json_builder_begin_object (b);
		json_builder_set_member_name (b, "path");
		json_builder_add_string_value (b, file);
		json_builder_set_member_name (b, "line");
		json_builder_add_int_value (b, 1);
		json_builder_end_object (b);
		json_builder_end_object (b);
		root = json_builder_get_root (b);
		json_generator_set_root (gen, root);
		request = json_generator_to_data (gen, NULL);
		reply = lrg_editor_server_dispatch (app->server, request);
		if (reply != NULL && strstr (reply, "\"error\"") == NULL)
			return;
	}
	editor_app_open_file (app, file, 1, 1);
}

void
lrg_editor_problems_panel_activate_row (LrgEditorProblemsPanel *self,
                                        guint                   index,
                                        gboolean                open)
{
	g_autoptr(LrgEditorConsoleRow) row = NULL;
	LrgEditorApp                  *app;
	LrgNode                       *node = NULL;

	g_return_if_fail (LRG_IS_EDITOR_PROBLEMS_PANEL (self));

	app = self->app;
	if (app == NULL || index >= g_list_model_get_n_items (G_LIST_MODEL (self->rows)))
		return;
	row = g_list_model_get_item (G_LIST_MODEL (self->rows), index);

	/* Click: select the node */
	if (row->guid != NULL && (node = lrg_editor_find_node (app->editor, row->guid)) != NULL)
		lrg_editor_select (app->editor, node, FALSE);

	if (!open)
		return;

	/* Double click: a script problem opens its file, else frame the node */
	if (row->file != NULL)
	{
		g_autofree gchar *abs = editor_app_project_path (app, row->file);

		if (g_file_test (abs, G_FILE_TEST_EXISTS))
		{
			problems_open_file (app, row->file);
			return;
		}
	}
	if (node != NULL)
		lrg_editor_actions_activate (app->actions, "view.focus");
}
