/* lrg-expander.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgExpander: A header that reveals or hides its content with a spring.
 *
 * A header strip (title and a chevron that turns 90 degrees) above a
 * content child. Opening or closing drives one "reveal" spring 0..1:
 * the expander measures as header + reveal x content height, so its
 * parent re-lays out smoothly as the spring moves (step() queues a
 * layout every frame of the animation), and the content is clipped to
 * the revealed band and faded with it.
 *
 * The header is a private focusable child (a plain LrgWidget painted
 * by the expander), so clicks in the content never focus the expander;
 * a click on the header or ACTIVATE (Enter / Space / gamepad A) on the
 * focused header toggles it.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-expander.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

struct _LrgExpander
{
	LrgContainer  parent_instance;

	gchar        *title;
	LrgWidget    *child;       /* not owned (container holds it) */
	LrgWidget    *header;      /* owned */

	gboolean      expanded;
	LrgSpring     reveal;      /* 0 closed .. 1 open */
	gboolean      stepped;     /* first frame done: animate from now on */
	gboolean      header_armed; /* the header was pressed */
	gboolean      in_layout;
};

G_DEFINE_FINAL_TYPE (LrgExpander, lrg_expander, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_TITLE,
	PROP_EXPANDED,
	N_PROPS
};

enum
{
	SIGNAL_TOGGLED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Geometry
 * ========================================================================== */

static gfloat
header_h (LrgExpander *self)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_METRIC_CONTROL_HEIGHT);
}

/* reveal_value: the reveal spring, never below 0 */
static gfloat
reveal_value (LrgExpander *self)
{
	return MAX (0.0f, self->reveal.value);
}

/* content_size: preferred outer size of the child plus the padding */
static void
content_size (LrgExpander *self,
              gfloat      *width,
              gfloat      *height)
{
	gfloat    pad = lrg_container_get_padding (LRG_CONTAINER (self));
	gfloat    w = 0.0f;
	gfloat    h = 0.0f;
	LrgInsets m = { 0.0f, 0.0f, 0.0f, 0.0f };

	if (self->child != NULL)
	{
		lrg_widget_get_preferred_size (self->child, &w, &h);
		lrg_widget_get_margin (self->child, &m);
		w += m.left + m.right;
		h += m.top + m.bottom;
	}
	*width = w + pad * 2.0f;
	*height = self->child != NULL ? h + pad * 2.0f : 0.0f;
}

/* sync_child_visibility: hide the content while fully closed */
static void
sync_child_visibility (LrgExpander *self)
{
	if (self->child != NULL)
		lrg_widget_set_visible (self->child, self->expanded || reveal_value (self) > 0.001f);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_expander_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
	LrgExpander *self = LRG_EXPANDER (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       padx = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	gfloat       tw = 0.0f;
	gfloat       cw;
	gfloat       ch;

	/* Header: padding, chevron, gap, title, padding */
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), self->title, size, 1.0f, &tw, NULL);
	content_size (self, &cw, &ch);

	*preferred_width = MAX (padx * 3.0f + size + tw, cw);
	*preferred_height = header_h (self) + roundf (ch * reveal_value (self));
}

/*
 * lrg_expander_layout_children:
 *
 * Header across the top; the content at its full preferred height
 * just below it, whatever the reveal: the paint clips it to the band
 * that is currently revealed.
 */
static void
lrg_expander_layout_children (LrgContainer *container)
{
	LrgExpander *self = LRG_EXPANDER (container);
	gfloat       w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat       hh = header_h (self);
	gfloat       pad = lrg_container_get_padding (container);
	gfloat       cw;
	gfloat       ch;

	if (self->in_layout || self->header == NULL)
		return;
	self->in_layout = TRUE;

	lrg_widget_allocate (self->header, 0.0f, 0.0f, w, hh);
	sync_child_visibility (self);
	if (self->child != NULL)
	{
		content_size (self, &cw, &ch);
		lrg_widget_place_in_slot (self->child, pad, hh + pad, MAX (0.0f, w - pad * 2.0f), MAX (0.0f, ch - pad * 2.0f));
	}

	self->in_layout = FALSE;
}

/* ==========================================================================
 * Looks and painting
 * ========================================================================== */

static void
lrg_expander_style_defaults (LrgWidget      *widget,
                             LrgUIPart       part,
                             LrgWidgetState  state,
                             LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlColor  c;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* A card around header and content */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, radius);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_HEADER:
		/* Clear at rest, control shades under the pointer */
		if (state & LRG_WIDGET_STATE_PRESSED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED);
		else if (state & LRG_WIDGET_STATE_HOVERED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
		else
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER), 0.0f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_radius (look, radius);
		return;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		return;

	case LRG_UI_PART_ARROW:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_OPEN) ? LRG_THEME_COLOR_ACCENT :
		                              (state & LRG_WIDGET_STATE_HOVERED) ? LRG_THEME_COLOR_TEXT
		                                                                 : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE) * 0.5f);
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_expander_parent_class)->style_defaults (widget, part, state, look);
}

/* paint_chevron: pointing right, turned by @turn quarter turns */
static void
paint_chevron (LrgPaintList   *list,
               gfloat          cx,
               gfloat          cy,
               gfloat          s,
               gfloat          turn,
               const GrlColor *color)
{
	gfloat angle = turn * (gfloat) G_PI * 0.5f;
	gfloat ca = cosf (angle);
	gfloat sa = sinf (angle);
	gfloat px[3] = { -s * 0.25f, s * 0.25f, -s * 0.25f };
	gfloat py[3] = { -s * 0.5f, 0.0f, s * 0.5f };
	gfloat rx[3];
	gfloat ry[3];
	gfloat t = MAX (1.5f, s * 0.22f);
	guint  k;

	for (k = 0; k < 3; k++)
	{
		rx[k] = cx + px[k] * ca - py[k] * sa;
		ry[k] = cy + px[k] * sa + py[k] * ca;
	}
	lrg_paint_list_line (list, rx[0], ry[0], rx[1], ry[1], t, color);
	lrg_paint_list_line (list, rx[1], ry[1], rx[2], ry[2], t, color);
}

/* resolve_header_part: @part blended by the header widget's springs */
static void
resolve_header_part (LrgExpander    *self,
                     LrgUIPart       part,
                     LrgWidgetState  base,
                     LrgPartLook    *look)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	LrgPartLook  variant;
	gfloat       hover = CLAMP (lrg_widget_get_hover_progress (self->header), 0.0f, 1.0f);
	gfloat       press = CLAMP (lrg_widget_get_press_progress (self->header), 0.0f, 1.0f);

	lrg_widget_resolve_look_for_state (widget, part, base, look);
	if (hover > 0.001f)
	{
		lrg_widget_resolve_look_for_state (widget, part, base | LRG_WIDGET_STATE_HOVERED, &variant);
		lrg_part_look_lerp (look, &variant, hover, look);
	}
	if (press > 0.001f)
	{
		lrg_widget_resolve_look_for_state (widget, part, base | LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED,
		                                   &variant);
		lrg_part_look_lerp (look, &variant, press, look);
	}
}

static void
lrg_expander_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
	LrgExpander   *self = LRG_EXPANDER (widget);
	LrgTheme      *theme = lrg_widget_get_theme (widget);
	LrgWidgetState base = self->expanded ? LRG_WIDGET_STATE_OPEN : LRG_WIDGET_STATE_NONE;
	LrgPartLook    box;
	LrgPartLook    look;
	gfloat         x = lrg_widget_get_world_x (widget);
	gfloat         y = lrg_widget_get_world_y (widget);
	gfloat         w = lrg_widget_get_width (widget);
	gfloat         h = lrg_widget_get_height (widget);
	gfloat         hh = header_h (self);
	gfloat         size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat         padx = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	gfloat         r = reveal_value (self);
	GrlFont       *font = lrg_theme_get_ui_font (theme);
	gfloat         th = size;

	/* 1. Card */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, x, y, w, h);
	if (box.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, box.opacity);

	/* 2. Header highlight, inset by the border */
	resolve_header_part (self, LRG_UI_PART_HEADER, base, &look);
	if (look.background.a > 0)
		lrg_widget_paint_box (widget, list, &look, x + 1.0f, y + 1.0f, MAX (0.0f, w - 2.0f), MAX (0.0f, hh - 2.0f));

	/* 3. Chevron turned by the reveal spring, then the title */
	resolve_header_part (self, LRG_UI_PART_ARROW, base, &look);
	paint_chevron (list, x + padx + size * 0.25f, y + hh * 0.5f, look.size > 0.0f ? look.size : size * 0.5f,
	               CLAMP (self->reveal.value, -0.2f, 1.2f), &look.foreground);
	resolve_header_part (self, LRG_UI_PART_LABEL, base, &look);
	lrg_paint_measure_text (font, self->title, size, 1.0f, NULL, &th);
	lrg_paint_list_push_clip (list, x, y, w, hh);
	lrg_paint_list_text (list, font, self->title, roundf (x + padx * 1.5f + size * 0.5f),
	                     roundf (y + (hh - th) * 0.5f), size, 1.0f, &look.foreground);
	lrg_paint_list_pop_clip (list);

	/* 4. Divider and content, clipped to the revealed band and faded */
	if (self->child != NULL && r > 0.001f && h > hh)
	{
		lrg_paint_list_rect (list, x + 1.0f, y + hh, MAX (0.0f, w - 2.0f), 1.0f, 0.0f, &box.border);
		lrg_paint_list_push_clip (list, x, y + hh, w, h - hh);
		lrg_paint_list_push_opacity (list, CLAMP (r, 0.0f, 1.0f));
		lrg_widget_paint (self->child, list);
		lrg_paint_list_pop_opacity (list);
		lrg_paint_list_pop_clip (list);
	}

	/* 5. The header widget: only the core focus ring */
	lrg_widget_paint (self->header, list);

	if (box.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

/*
 * lrg_expander_step:
 *
 * Advances the reveal spring towards the expanded state and asks for a
 * new layout every frame it moves (and once when it lands), so the
 * measured height follows it. The first frame snaps.
 *
 * Returns: %TRUE while the content is still opening or closing
 */
static gboolean
lrg_expander_step (LrgWidget *widget,
                   gfloat     delta_time)
{
	LrgExpander *self = LRG_EXPANDER (widget);
	gfloat       before = self->reveal.value;
	gboolean     moving;

	lrg_spring_set_motion (&self->reveal, lrg_widget_get_motion (widget));
	lrg_spring_set_target (&self->reveal, self->expanded ? 1.0f : 0.0f);
	if (!self->stepped || lrg_widget_get_motion (widget) == LRG_MOTION_NONE)
	{
		lrg_spring_snap (&self->reveal, self->reveal.target);
		self->stepped = TRUE;
		moving = FALSE;
	}
	else
		moving = lrg_spring_step (&self->reveal, delta_time);

	if (self->reveal.value != before)
	{
		sync_child_visibility (self);
		lrg_widget_queue_layout (widget);
	}
	return moving;
}

/* ==========================================================================
 * Events
 * ========================================================================== */

static gboolean
lrg_expander_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
	LrgExpander *self = LRG_EXPANDER (widget);
	LrgWidget   *target = lrg_ui_event_get_target (event);

	if (target != self->header)
		return FALSE;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->header_armed = TRUE;
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		/* A click: released on the header it was pressed on */
		if (lrg_ui_event_get_button (event) != 0 || !self->header_armed)
			return FALSE;
		self->header_armed = FALSE;
		if (lrg_widget_contains_point (self->header, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)))
			lrg_expander_toggle (self);
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		lrg_expander_toggle (self);
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_expander_constructed (GObject *object)
{
	LrgExpander *self = LRG_EXPANDER (object);

	G_OBJECT_CLASS (lrg_expander_parent_class)->constructed (object);

	self->in_layout = TRUE;
	self->header = g_object_new (LRG_TYPE_WIDGET, NULL);
	lrg_widget_set_name (self->header, "header");
	lrg_widget_set_focusable (self->header, TRUE);
	lrg_container_add_child (LRG_CONTAINER (self), self->header);
	self->in_layout = FALSE;
}

static void
lrg_expander_dispose (GObject *object)
{
	LrgExpander *self = LRG_EXPANDER (object);

	self->child = NULL;
	g_clear_object (&self->header);

	G_OBJECT_CLASS (lrg_expander_parent_class)->dispose (object);
}

static void
lrg_expander_finalize (GObject *object)
{
	g_free (LRG_EXPANDER (object)->title);
	G_OBJECT_CLASS (lrg_expander_parent_class)->finalize (object);
}

static void
lrg_expander_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgExpander *self = LRG_EXPANDER (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		g_value_set_string (value, self->title);
		break;
	case PROP_EXPANDED:
		g_value_set_boolean (value, self->expanded);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_expander_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgExpander *self = LRG_EXPANDER (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		lrg_expander_set_title (self, g_value_get_string (value));
		break;
	case PROP_EXPANDED:
		lrg_expander_set_expanded (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_expander_class_init (LrgExpanderClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_expander_constructed;
	object_class->dispose = lrg_expander_dispose;
	object_class->finalize = lrg_expander_finalize;
	object_class->get_property = lrg_expander_get_property;
	object_class->set_property = lrg_expander_set_property;

	widget_class->measure = lrg_expander_measure;
	widget_class->paint = lrg_expander_paint;
	widget_class->step = lrg_expander_step;
	widget_class->style_defaults = lrg_expander_style_defaults;
	widget_class->handle_event = lrg_expander_handle_event;

	container_class->layout_children = lrg_expander_layout_children;

	/**
	 * LrgExpander:title:
	 *
	 * The header text.
	 */
	properties[PROP_TITLE] =
		g_param_spec_string ("title", "Title", "Header text", "",
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgExpander:expanded:
	 *
	 * Whether the content is revealed (or being revealed).
	 */
	properties[PROP_EXPANDED] =
		g_param_spec_boolean ("expanded", "Expanded", "Content revealed",
		                      FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgExpander::toggled:
	 * @self: the expander
	 * @expanded: the new state
	 *
	 * Emitted when the expander opens or closes (click, ACTIVATE or
	 * lrg_expander_set_expanded()), at the start of the animation.
	 */
	signals[SIGNAL_TOGGLED] =
		g_signal_new ("toggled",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_expander_init (LrgExpander *self)
{
	self->title = g_strdup ("");
	lrg_spring_init (&self->reveal, 0.0f, LRG_MOTION_SMOOTH);
	lrg_container_set_padding (LRG_CONTAINER (self), 8.0f);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgExpander *
lrg_expander_new (const gchar *title)
{
	return g_object_new (LRG_TYPE_EXPANDER, "title", title != NULL ? title : "", NULL);
}

const gchar *
lrg_expander_get_title (LrgExpander *self)
{
	g_return_val_if_fail (LRG_IS_EXPANDER (self), NULL);
	return self->title;
}

void
lrg_expander_set_title (LrgExpander *self,
                        const gchar *title)
{
	g_return_if_fail (LRG_IS_EXPANDER (self));

	if (title == NULL)
		title = "";
	if (g_strcmp0 (self->title, title) == 0)
		return;
	g_free (self->title);
	self->title = g_strdup (title);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

void
lrg_expander_set_child (LrgExpander *self,
                        LrgWidget   *child)
{
	LrgContainer *c;

	g_return_if_fail (LRG_IS_EXPANDER (self));
	g_return_if_fail (child == NULL || LRG_IS_WIDGET (child));

	if (self->child == child)
		return;
	c = LRG_CONTAINER (self);

	/* Keep the header the last child, hit before the content */
	self->in_layout = TRUE;
	if (self->child != NULL)
		lrg_container_remove_child (c, self->child);
	self->child = child;
	lrg_container_remove_child (c, self->header);
	if (child != NULL)
		lrg_container_add_child (c, child);
	lrg_container_add_child (c, self->header);
	self->in_layout = FALSE;

	sync_child_visibility (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

LrgWidget *
lrg_expander_get_child (LrgExpander *self)
{
	g_return_val_if_fail (LRG_IS_EXPANDER (self), NULL);
	return self->child;
}

gboolean
lrg_expander_get_expanded (LrgExpander *self)
{
	g_return_val_if_fail (LRG_IS_EXPANDER (self), FALSE);
	return self->expanded;
}

void
lrg_expander_set_expanded (LrgExpander *self,
                           gboolean     expanded)
{
	g_return_if_fail (LRG_IS_EXPANDER (self));

	expanded = !!expanded;
	if (self->expanded == expanded)
		return;
	self->expanded = expanded;
	lrg_spring_set_target (&self->reveal, expanded ? 1.0f : 0.0f);

	/* Before the first frame there is nothing to animate from */
	if (!self->stepped)
		lrg_spring_snap (&self->reveal, self->reveal.target);
	if (expanded)
		lrg_widget_set_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_OPEN);
	else
		lrg_widget_unset_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_OPEN);

	sync_child_visibility (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EXPANDED]);
	g_signal_emit (self, signals[SIGNAL_TOGGLED], 0, expanded);
}

void
lrg_expander_toggle (LrgExpander *self)
{
	g_return_if_fail (LRG_IS_EXPANDER (self));
	lrg_expander_set_expanded (self, !self->expanded);
}

gfloat
lrg_expander_get_reveal (LrgExpander *self)
{
	g_return_val_if_fail (LRG_IS_EXPANDER (self), 0.0f);
	return self->reveal.value;
}

LrgWidget *
lrg_expander_get_header (LrgExpander *self)
{
	g_return_val_if_fail (LRG_IS_EXPANDER (self), NULL);
	return self->header;
}
