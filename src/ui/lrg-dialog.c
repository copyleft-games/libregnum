/* lrg-dialog.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDialog: A modal sheet with a title, content and response buttons.
 *
 * The dialog is a container shown on the canvas popup layer as a modal
 * popup. It paints its title and (word-wrapped) body text itself and
 * owns two kinds of children: an optional content widget and the
 * response buttons, laid out top to bottom:
 *
 *   padding
 *   title            (FONT_SIZE_LARGE, TEXT)
 *   body lines       (FONT_SIZE, TEXT_MUTED)
 *   content widget   (inner width, preferred height)
 *   buttons          (right-aligned row)
 *   padding
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-dialog.h"
#include "../lrg-log.h"

/* Widest the text column grows before wrapping */
#define DIALOG_MAX_TEXT_WIDTH (420.0f)

/* Narrowest inner width, so short messages still read as a sheet */
#define DIALOG_MIN_INNER_WIDTH (260.0f)

/* Smallest button width in the row */
#define DIALOG_MIN_BUTTON_WIDTH (88.0f)

/* Object data keys on the buttons */
#define DIALOG_RESPONSE_KEY "lrg-dialog-response"

struct _LrgDialog
{
	LrgContainer  parent_instance;

	gchar        *title;
	gchar        *body;
	LrgWidget    *content;       /* child, not separately owned */
	GPtrArray    *buttons;       /* LrgButton *, children, not owned */
	LrgButton    *primary;       /* one of @buttons or NULL */

	/* Wrapped body and text positions from the last layout */
	GPtrArray    *lines;         /* gchar *, owned */
	gfloat        title_y;
	gfloat        body_y;
	gfloat        line_h;

	/* Presentation */
	LrgCanvas    *canvas;        /* weak */
	gulong        dismissed_id;
	gboolean      presented;
	LrgWidget    *return_focus;  /* weak: focus before presenting */
	gboolean      return_visible;
};

G_DEFINE_FINAL_TYPE (LrgDialog, lrg_dialog, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_TITLE,
	PROP_BODY,
	PROP_CONTENT,
	PROP_PRESENTED,
	N_PROPS
};

enum
{
	SIGNAL_RESPONSE,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Layout
 * ========================================================================== */

typedef struct
{
	gfloat pad;
	gfloat spacing;
	gfloat title_size;
	gfloat body_size;
	gfloat title_h;
	gfloat line_h;
	gfloat inner_w;
	gfloat content_h;
	gfloat buttons_w;
	gfloat buttons_h;
	gfloat width;
	gfloat height;
} DialogLayout;

/* measure: width / height of @text in the dialog's UI font */
static void
measure (LrgDialog   *self,
         const gchar *text,
         gfloat       size,
         gfloat      *w,
         gfloat      *h)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));

	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, size, 1.0f, w, h);
}

/*
 * wrap_body:
 *
 * Splits the body into lines: explicit '\n' breaks are kept, and each
 * paragraph is filled greedily word by word up to @max_w. A single word
 * wider than @max_w gets a line of its own. Returns the widest line.
 */
static gfloat
wrap_body (LrgDialog *self,
           gfloat     size,
           gfloat     max_w)
{
	g_auto(GStrv) paragraphs = NULL;
	gfloat        widest = 0.0f;
	guint         p;

	g_ptr_array_set_size (self->lines, 0);
	if (self->body == NULL || self->body[0] == '\0')
		return 0.0f;

	paragraphs = g_strsplit (self->body, "\n", -1);
	for (p = 0; paragraphs[p] != NULL; p++)
	{
		g_auto(GStrv)     words = g_strsplit (paragraphs[p], " ", -1);
		g_autoptr(GString) line = g_string_new (NULL);
		guint             i;
		gfloat            w = 0.0f;

		for (i = 0; words[i] != NULL; i++)
		{
			g_autofree gchar *candidate = NULL;
			gfloat            cw = 0.0f;

			if (words[i][0] == '\0' && line->len > 0)
				continue;
			candidate = line->len > 0 ? g_strconcat (line->str, " ", words[i], NULL)
			                          : g_strdup (words[i]);
			measure (self, candidate, size, &cw, NULL);

			if (cw > max_w && line->len > 0)
			{
				/* Full: emit the line, start the next with this word */
				widest = MAX (widest, w);
				g_ptr_array_add (self->lines, g_strdup (line->str));
				g_string_assign (line, words[i]);
				measure (self, line->str, size, &w, NULL);
			}
			else
			{
				g_string_assign (line, candidate);
				w = cw;
			}
		}

		widest = MAX (widest, w);
		g_ptr_array_add (self->lines, g_strdup (line->str));
	}

	return widest;
}

/* button_size: a button's slot size in the row */
static void
button_size (LrgDialog *self,
             LrgWidget *button,
             gfloat    *w,
             gfloat    *h)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat    pw;
	gfloat    ph;

	lrg_widget_get_preferred_size (button, &pw, &ph);
	*w = MAX (pw, DIALOG_MIN_BUTTON_WIDTH);
	*h = MAX (ph, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT));
}

/*
 * compute_layout:
 *
 * Measures every section and fills @out. Also re-wraps the body, so the
 * cached lines always match the reported size.
 */
static void
compute_layout (LrgDialog    *self,
                DialogLayout *out)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	gfloat    title_w = 0.0f;
	gfloat    body_w;
	gfloat    content_w = 0.0f;
	gfloat    y;
	guint     i;

	memset (out, 0, sizeof (DialogLayout));
	out->pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X) * 2.0f;
	out->spacing = lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING);
	out->title_size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_LARGE);
	out->body_size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);

	/* Text column */
	if (self->title != NULL && self->title[0] != '\0')
		measure (self, self->title, out->title_size, &title_w, &out->title_h);
	measure (self, "Ag", out->body_size, NULL, &out->line_h);
	out->line_h = ceilf (out->line_h * 1.3f);
	body_w = wrap_body (self, out->body_size, DIALOG_MAX_TEXT_WIDTH);

	/* Content */
	if (self->content != NULL && lrg_widget_get_visible (self->content))
	{
		LrgInsets m;

		lrg_widget_get_preferred_size (self->content, &content_w, &out->content_h);
		lrg_widget_get_margin (self->content, &m);
		content_w += m.left + m.right;
		out->content_h += m.top + m.bottom;
	}

	/* Button row */
	for (i = 0; i < self->buttons->len; i++)
	{
		gfloat bw;
		gfloat bh;

		button_size (self, g_ptr_array_index (self->buttons, i), &bw, &bh);
		out->buttons_w += bw + (i > 0 ? out->spacing : 0.0f);
		out->buttons_h = MAX (out->buttons_h, bh);
	}

	out->inner_w = MIN (MAX (title_w, body_w), DIALOG_MAX_TEXT_WIDTH);
	out->inner_w = MAX (out->inner_w, DIALOG_MIN_INNER_WIDTH);
	out->inner_w = ceilf (MAX (out->inner_w, MAX (content_w, out->buttons_w)));

	/* Stack the sections, with a spacing gap between those present */
	y = out->pad;
	self->title_y = y;
	if (out->title_h > 0.0f)
		y += out->title_h + (self->lines->len > 0 ? out->spacing : 0.0f);
	self->body_y = y;
	self->line_h = out->line_h;
	y += out->line_h * (gfloat) self->lines->len;
	if (out->content_h > 0.0f)
		y += (y > out->pad ? out->spacing * 1.5f : 0.0f) + out->content_h;
	if (self->buttons->len > 0)
		y += (y > out->pad ? out->spacing * 2.5f : 0.0f) + out->buttons_h;

	out->width = out->inner_w + out->pad * 2.0f;
	out->height = ceilf (y + out->pad);
}

static void
lrg_dialog_measure (LrgWidget *widget,
                    gfloat    *width,
                    gfloat    *height)
{
	DialogLayout l;

	compute_layout (LRG_DIALOG (widget), &l);
	*width = l.width;
	*height = l.height;
}

static void
lrg_dialog_layout_children (LrgContainer *container)
{
	LrgDialog    *self = LRG_DIALOG (container);
	DialogLayout  l;
	gfloat        h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat        inner_w;
	gfloat        x;
	gfloat        y;
	guint         i;

	compute_layout (self, &l);

	/* Use the allocated width (may be larger than preferred) */
	inner_w = MAX (lrg_widget_get_width (LRG_WIDGET (self)) - l.pad * 2.0f, 0.0f);

	/* Buttons sit on the bottom edge, right-aligned */
	y = h - l.pad - l.buttons_h;
	x = l.pad + inner_w - l.buttons_w;
	for (i = 0; i < self->buttons->len; i++)
	{
		LrgWidget *button = g_ptr_array_index (self->buttons, i);
		gfloat     bw;
		gfloat     bh;

		button_size (self, button, &bw, &bh);
		lrg_widget_place_in_slot (button, x, y + (l.buttons_h - bh) * 0.5f, bw, bh);
		x += bw + l.spacing;
	}

	/* Content right under the text */
	if (self->content != NULL && lrg_widget_get_visible (self->content))
	{
		gfloat cy = self->body_y + l.line_h * (gfloat) self->lines->len;

		if (cy > l.pad)
			cy += l.spacing * 1.5f;
		lrg_widget_place_in_slot (self->content, l.pad, cy, inner_w, l.content_h);
	}
}

/* ==========================================================================
 * Paint
 * ========================================================================== */

static void
lrg_dialog_style_defaults (LrgWidget      *widget,
                           LrgUIPart       part,
                           LrgWidgetState  state,
                           LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_POPUP:
		/* A raised sheet: big, soft shadow, hairline border */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) * 2.0f,
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y) * 2.0f);
		break;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		break;

	case LRG_UI_PART_ITEM:
		/* Body text */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		break;

	default:
		LRG_WIDGET_CLASS (lrg_dialog_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_dialog_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
	LrgDialog   *self = LRG_DIALOG (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	GrlFont     *font = lrg_theme_get_ui_font (theme);
	LrgPartLook  surface;
	LrgPartLook  title;
	LrgPartLook  body;
	gfloat       x = lrg_widget_get_world_x (widget);
	gfloat       y = lrg_widget_get_world_y (widget);
	gfloat       w = lrg_widget_get_width (widget);
	gfloat       h = lrg_widget_get_height (widget);
	gfloat       pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X) * 2.0f;
	GList       *l;
	guint        i;

	/* Sheet */
	lrg_widget_resolve_look (widget, LRG_UI_PART_POPUP, &surface);
	lrg_widget_paint_box (widget, list, &surface, x, y, w, h);

	/* Text column, clipped to the inner area */
	lrg_paint_list_push_clip (list, x + pad, y, MAX (w - pad * 2.0f, 0.0f), h);
	if (self->title != NULL && self->title[0] != '\0')
	{
		lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &title);
		lrg_paint_list_text (list, font, self->title, x + pad, y + self->title_y,
		                     lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_LARGE), 1.0f,
		                     &title.foreground);
	}
	lrg_widget_resolve_look (widget, LRG_UI_PART_ITEM, &body);
	for (i = 0; i < self->lines->len; i++)
	{
		const gchar *line = g_ptr_array_index (self->lines, i);

		if (line[0] == '\0')
			continue;
		lrg_paint_list_text (list, font, line, x + pad, y + self->body_y + self->line_h * (gfloat) i,
		                     lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE), 1.0f,
		                     &body.foreground);
	}
	lrg_paint_list_pop_clip (list);

	/* Content and buttons */
	for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
		lrg_widget_paint (LRG_WIDGET (l->data), list);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* is_own_button: @widget is one of the dialog's response buttons */
static gboolean
is_own_button (LrgDialog *self,
               LrgWidget *widget)
{
	return widget != NULL && g_ptr_array_find (self->buttons, widget, NULL);
}

/* press_button: act as if @button was clicked (its handlers run too) */
static void
press_button (LrgButton *button)
{
	g_signal_emit_by_name (button, "clicked");
}

static gboolean
lrg_dialog_handle_event (LrgWidget        *widget,
                         const LrgUIEvent *event)
{
	LrgDialog *self = LRG_DIALOG (widget);
	LrgWidget *target = lrg_ui_event_get_target (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_CANCEL:
		/* Escape / gamepad B anywhere in the dialog */
		lrg_dialog_respond (self, LRG_DIALOG_RESPONSE_CANCEL);
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		/* A button that did not handle activation itself, else the
		 * dialog background or a content widget: the primary action */
		if (LRG_IS_BUTTON (target) && is_own_button (self, target))
		{
			press_button (LRG_BUTTON (target));
			return TRUE;
		}
		if (self->primary != NULL)
		{
			press_button (self->primary);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_KEY_DOWN:
		/* Enter with nothing focused reaches the dialog directly */
		if (!lrg_ui_event_get_bubbling (event) && self->primary != NULL &&
		    (lrg_ui_event_get_key (event) == GRL_KEY_ENTER ||
		     lrg_ui_event_get_key (event) == GRL_KEY_KP_ENTER))
		{
			press_button (self->primary);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		/* Clicks on the sheet itself stop here */
		return !lrg_ui_event_get_bubbling (event) &&
		       lrg_widget_contains_point (widget, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));

	default:
		return FALSE;
	}
}

/* on_button_clicked: respond with the button's id */
static void
on_button_clicked (LrgButton *button,
                   gpointer   user_data)
{
	LrgDialog *self = LRG_DIALOG (user_data);

	lrg_dialog_respond (self, GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), DIALOG_RESPONSE_KEY)));
}

/* ==========================================================================
 * Presentation
 * ========================================================================== */

/* forget_canvas: drop the canvas hook */
static void
forget_canvas (LrgDialog *self)
{
	if (self->canvas != NULL && self->dismissed_id != 0)
		g_signal_handler_disconnect (self->canvas, self->dismissed_id);
	self->dismissed_id = 0;
	g_clear_weak_pointer (&self->canvas);
}

/* on_popup_dismissed: the dialog left the popup layer (any way) */
static void
on_popup_dismissed (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	LrgDialog            *self = LRG_DIALOG (user_data);
	g_autoptr(LrgWidget)  back = NULL;

	if (popup != LRG_WIDGET (self))
		return;

	back = self->return_focus != NULL ? g_object_ref (self->return_focus) : NULL;
	g_clear_weak_pointer (&self->return_focus);
	forget_canvas (self);
	self->presented = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRESENTED]);

	/* Give focus back to where it was, if that is still reachable */
	if (back != NULL && lrg_widget_get_root (back) == LRG_WIDGET (canvas) &&
	    lrg_widget_get_visible (back) && lrg_widget_get_enabled (back) &&
	    lrg_canvas_get_focused_widget (canvas) == NULL)
	{
		lrg_canvas_set_focused_widget (canvas, back);
		if (self->return_visible)
			lrg_widget_set_state_flags (back, LRG_WIDGET_STATE_FOCUS_VISIBLE);
	}
}

/* sync_presented_size: keep an open dialog sized and centred after edits */
static void
sync_presented_size (LrgDialog *self)
{
	LrgWidget *widget = LRG_WIDGET (self);
	gfloat     w;
	gfloat     h;
	gfloat     cw;
	gfloat     ch;

	lrg_widget_queue_layout (widget);
	if (!self->presented || self->canvas == NULL)
		return;

	lrg_widget_get_preferred_size (widget, &w, &h);
	cw = lrg_widget_get_width (LRG_WIDGET (self->canvas));
	ch = lrg_widget_get_height (LRG_WIDGET (self->canvas));
	lrg_widget_allocate (widget, roundf (MAX ((cw - w) * 0.5f, 0.0f)), roundf (MAX ((ch - h) * 0.45f, 0.0f)),
	                     w, h);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_dialog_dispose (GObject *object)
{
	LrgDialog *self = LRG_DIALOG (object);

	forget_canvas (self);
	g_clear_weak_pointer (&self->return_focus);
	self->content = NULL;
	self->primary = NULL;
	if (self->buttons != NULL)
		g_ptr_array_set_size (self->buttons, 0);

	G_OBJECT_CLASS (lrg_dialog_parent_class)->dispose (object);
}

static void
lrg_dialog_finalize (GObject *object)
{
	LrgDialog *self = LRG_DIALOG (object);

	g_clear_pointer (&self->title, g_free);
	g_clear_pointer (&self->body, g_free);
	g_clear_pointer (&self->buttons, g_ptr_array_unref);
	g_clear_pointer (&self->lines, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_dialog_parent_class)->finalize (object);
}

static void
lrg_dialog_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgDialog *self = LRG_DIALOG (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		g_value_set_string (value, self->title);
		break;
	case PROP_BODY:
		g_value_set_string (value, self->body);
		break;
	case PROP_CONTENT:
		g_value_set_object (value, self->content);
		break;
	case PROP_PRESENTED:
		g_value_set_boolean (value, self->presented);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_dialog_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgDialog *self = LRG_DIALOG (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		lrg_dialog_set_title (self, g_value_get_string (value));
		break;
	case PROP_BODY:
		lrg_dialog_set_body (self, g_value_get_string (value));
		break;
	case PROP_CONTENT:
		lrg_dialog_set_content (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_dialog_class_init (LrgDialogClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_dialog_dispose;
	object_class->finalize = lrg_dialog_finalize;
	object_class->get_property = lrg_dialog_get_property;
	object_class->set_property = lrg_dialog_set_property;

	widget_class->style_defaults = lrg_dialog_style_defaults;
	widget_class->measure = lrg_dialog_measure;
	widget_class->paint = lrg_dialog_paint;
	widget_class->handle_event = lrg_dialog_handle_event;

	container_class->layout_children = lrg_dialog_layout_children;

	/**
	 * LrgDialog:title:
	 *
	 * Heading text, drawn in the theme's large font size.
	 */
	properties[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDialog:body:
	 *
	 * Message text. Explicit line breaks are kept; lines longer than
	 * about 420 units are word-wrapped.
	 */
	properties[PROP_BODY] =
		g_param_spec_string ("body", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDialog:content:
	 *
	 * Optional widget shown between the body and the buttons.
	 */
	properties[PROP_CONTENT] =
		g_param_spec_object ("content", NULL, NULL, LRG_TYPE_WIDGET,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDialog:presented:
	 *
	 * Whether the dialog is open on a canvas.
	 */
	properties[PROP_PRESENTED] =
		g_param_spec_boolean ("presented", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgDialog::response:
	 * @self: the dialog
	 * @response_id: the clicked button's id, or
	 *   %LRG_DIALOG_RESPONSE_CANCEL for Escape / gamepad B
	 *
	 * Emitted when the user answers the dialog. The dialog closes right
	 * after the signal returns.
	 */
	signals[SIGNAL_RESPONSE] =
		g_signal_new ("response", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
}

static void
lrg_dialog_init (LrgDialog *self)
{
	self->buttons = g_ptr_array_new ();
	self->lines = g_ptr_array_new_with_free_func (g_free);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgDialog *
lrg_dialog_new (const gchar *title,
                const gchar *body)
{
	return g_object_new (LRG_TYPE_DIALOG, "title", title, "body", body, NULL);
}

const gchar *
lrg_dialog_get_title (LrgDialog *self)
{
	g_return_val_if_fail (LRG_IS_DIALOG (self), NULL);

	return self->title;
}

void
lrg_dialog_set_title (LrgDialog   *self,
                      const gchar *title)
{
	g_return_if_fail (LRG_IS_DIALOG (self));

	if (g_strcmp0 (self->title, title) == 0)
		return;
	g_free (self->title);
	self->title = g_strdup (title);
	sync_presented_size (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

const gchar *
lrg_dialog_get_body (LrgDialog *self)
{
	g_return_val_if_fail (LRG_IS_DIALOG (self), NULL);

	return self->body;
}

void
lrg_dialog_set_body (LrgDialog   *self,
                     const gchar *body)
{
	g_return_if_fail (LRG_IS_DIALOG (self));

	if (g_strcmp0 (self->body, body) == 0)
		return;
	g_free (self->body);
	self->body = g_strdup (body);
	sync_presented_size (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BODY]);
}

LrgWidget *
lrg_dialog_get_content (LrgDialog *self)
{
	g_return_val_if_fail (LRG_IS_DIALOG (self), NULL);

	return self->content;
}

void
lrg_dialog_set_content (LrgDialog *self,
                        LrgWidget *content)
{
	g_autoptr(GPtrArray) held = NULL;
	guint                i;

	g_return_if_fail (LRG_IS_DIALOG (self));
	g_return_if_fail (content == NULL || LRG_IS_WIDGET (content));
	g_return_if_fail (content == NULL || lrg_widget_get_parent (content) == NULL);

	if (self->content == content)
		return;

	if (self->content != NULL)
	{
		lrg_container_remove_child (LRG_CONTAINER (self), self->content);
		self->content = NULL;
	}

	if (content != NULL)
	{
		/* Re-add the buttons after the content so Tab reaches the
		 * content first */
		held = g_ptr_array_new_with_free_func (g_object_unref);
		for (i = 0; i < self->buttons->len; i++)
		{
			LrgWidget *b = g_ptr_array_index (self->buttons, i);

			g_ptr_array_add (held, g_object_ref (b));
			lrg_container_remove_child (LRG_CONTAINER (self), b);
		}

		self->content = content;
		lrg_container_add_child (LRG_CONTAINER (self), content);
		for (i = 0; i < held->len; i++)
			lrg_container_add_child (LRG_CONTAINER (self), g_ptr_array_index (held, i));
	}

	sync_presented_size (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CONTENT]);
}

LrgButton *
lrg_dialog_add_button (LrgDialog   *self,
                       const gchar *label,
                       gint         response_id,
                       gboolean     primary)
{
	LrgButton *button;

	g_return_val_if_fail (LRG_IS_DIALOG (self), NULL);
	g_return_val_if_fail (label != NULL, NULL);

	button = lrg_button_new (label);
	g_object_set_data (G_OBJECT (button), DIALOG_RESPONSE_KEY, GINT_TO_POINTER (response_id));
	g_signal_connect_object (button, "clicked", G_CALLBACK (on_button_clicked), self, 0);

	/* One primary button: the newest one wins */
	if (primary)
	{
		if (self->primary != NULL)
			lrg_widget_remove_class (LRG_WIDGET (self->primary), "primary");
		lrg_widget_add_class (LRG_WIDGET (button), "primary");
		self->primary = button;
	}

	g_ptr_array_add (self->buttons, button);
	lrg_container_add_child (LRG_CONTAINER (self), LRG_WIDGET (button));
	g_object_unref (button);

	sync_presented_size (self);
	return button;
}

LrgButton *
lrg_dialog_get_primary_button (LrgDialog *self)
{
	g_return_val_if_fail (LRG_IS_DIALOG (self), NULL);

	return self->primary;
}

void
lrg_dialog_present (LrgDialog *self,
                    LrgCanvas *canvas)
{
	LrgWidget *widget;
	LrgWidget *previous;
	LrgWidget *focused;
	gfloat     w;
	gfloat     h;

	g_return_if_fail (LRG_IS_DIALOG (self));
	g_return_if_fail (LRG_IS_CANVAS (canvas));

	widget = LRG_WIDGET (self);
	if (self->presented)
		return;
	/* Still fading out on this canvas is fine (the canvas drops the
	 * ghost); being inside some other container is not */
	if (lrg_widget_get_parent (widget) != NULL &&
	    lrg_widget_get_parent (widget) != LRG_CONTAINER (canvas))
	{
		lrg_warning (LRG_LOG_DOMAIN_UI,
		             "lrg_dialog_present: the dialog is inside a container; dialogs are top-level");
		return;
	}

	/* Remember where focus was, to give it back on close */
	previous = lrg_canvas_get_focused_widget (canvas);
	g_set_weak_pointer (&self->return_focus, previous);
	self->return_visible = previous != NULL &&
	                       (lrg_widget_get_state_flags (previous) & LRG_WIDGET_STATE_FOCUS_VISIBLE);

	/* Fresh size every time: content may have changed since last time */
	lrg_widget_get_preferred_size (widget, &w, &h);
	lrg_widget_set_size (widget, w, h);

	g_set_weak_pointer (&self->canvas, canvas);
	self->dismissed_id = g_signal_connect_object (canvas, "popup-dismissed",
	                                              G_CALLBACK (on_popup_dismissed), self, 0);
	self->presented = TRUE;

	/* The rising sheet */
	lrg_widget_set_appear (widget, LRG_APPEAR_FLY_UP, 0.0f);
	lrg_canvas_show_popup (canvas, widget, NULL, LRG_POPUP_PLACEMENT_CENTER, TRUE);

	/* The canvas focused the first focusable inside; prefer the
	 * primary button, keeping the ring if focus came by keyboard */
	focused = lrg_canvas_get_focused_widget (canvas);
	if (self->primary != NULL && focused != LRG_WIDGET (self->primary))
	{
		gboolean keyboard = focused != NULL &&
		                    (lrg_widget_get_state_flags (focused) & LRG_WIDGET_STATE_FOCUS_VISIBLE);

		lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (self->primary));
		if (keyboard)
			lrg_widget_set_state_flags (LRG_WIDGET (self->primary), LRG_WIDGET_STATE_FOCUS_VISIBLE);
	}

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRESENTED]);
}

void
lrg_dialog_close (LrgDialog *self)
{
	g_return_if_fail (LRG_IS_DIALOG (self));

	if (!self->presented || self->canvas == NULL)
		return;

	/* popup-dismissed clears the presented state */
	lrg_canvas_dismiss_popup (self->canvas, LRG_WIDGET (self));
}

void
lrg_dialog_respond (LrgDialog *self,
                    gint       response_id)
{
	g_return_if_fail (LRG_IS_DIALOG (self));

	g_object_ref (self);
	g_signal_emit (self, signals[SIGNAL_RESPONSE], 0, response_id);
	lrg_dialog_close (self);
	g_object_unref (self);
}

gboolean
lrg_dialog_get_presented (LrgDialog *self)
{
	g_return_val_if_fail (LRG_IS_DIALOG (self), FALSE);

	return self->presented;
}
