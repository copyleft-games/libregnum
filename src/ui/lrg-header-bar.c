/* lrg-header-bar.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHeaderBar: the title bar of a window that draws its own decorations.
 *
 * A container with three regions, laid out left to right in local
 * coordinates (the core mirrors children in right-to-left, and the
 * parts the bar paints itself go through lrg_widget_mirror_x()):
 *
 *   [lead buttons][start children ...]   title   [... end children][trail buttons]
 *
 * Start children are packed from the leading edge, end children from
 * the trailing edge inwards. The title (text, or a custom title widget)
 * is centred on the whole bar and pushed sideways only when it would
 * overlap the side groups. The window buttons (minimise / maximise /
 * close) are painted by the bar itself from a GTK-style decoration
 * layout string, each with its own hover and press springs.
 *
 * Window operations (dragging, maximise, minimise) go through a small
 * table of functions so tests can verify them without a window; the
 * default table calls raylib and is a no-op without a window, and for
 * dragging on Wayland, where clients cannot place their windows.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>
#include <raylib.h>

#include "lrg-header-bar.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Seconds and distance (units) within which two presses are a double click */
#define HEADER_BAR_DOUBLE_CLICK_TIME     (0.4)
#define HEADER_BAR_DOUBLE_CLICK_DISTANCE (6.0f)

/* Window button diameter as a fraction of the theme control height */
#define HEADER_BAR_BUTTON_SCALE (0.72f)

/* Number of distinct window buttons (enum values 1..3) */
#define N_BUTTONS (3)

/* Index of a button in the per-button arrays */
#define BUTTON_INDEX(b) ((gint) (b) - 1)

/* GLFW's platform id for Wayland (GLFW_PLATFORM_WAYLAND) */
#define HEADER_BAR_GLFW_PLATFORM_WAYLAND (0x00060003)

#if defined(__ELF__)
/*
 * glfwGetPlatform() is exported by graylib's embedded GLFW (3.4+). It
 * is referenced weakly so a raylib built on another platform layer
 * (RGFW, SDL) still links; then the environment decides.
 */
extern int glfwGetPlatform (void) __attribute__ ((weak));
#endif

struct _LrgHeaderBar
{
	LrgContainer             parent_instance;

	gchar                   *title;
	gchar                   *subtitle;
	LrgWidget               *title_widget;       /* weak; also a child */
	gboolean                 show_window_buttons;
	gchar                   *decoration_layout;
	gboolean                 draggable;

	/* Parsed layout */
	LrgHeaderBarButton       lead[N_BUTTONS];
	LrgHeaderBarButton       trail[N_BUTTONS];

	/* Layout results, local left-to-right coordinates */
	gfloat                   button_x[N_BUTTONS];
	gboolean                 button_shown[N_BUTTONS];
	gfloat                   button_size;
	gfloat                   title_x;
	gfloat                   title_w;

	/* Pointer state */
	LrgHeaderBarButton       hover_button;
	LrgHeaderBarButton       press_button;
	gboolean                 dragging;
	gfloat                   drag_x;
	gfloat                   drag_y;
	gdouble                  clock;
	gdouble                  last_click_time;
	gfloat                   last_click_x;
	gfloat                   last_click_y;

	/* Motion */
	LrgSpring                hover[N_BUTTONS];
	LrgSpring                press[N_BUTTONS];
	gboolean                 stepped;

	/* Window operations and the cached maximised state (for the icon) */
	LrgHeaderBarWindowFuncs  funcs;
	gpointer                 funcs_data;
	GDestroyNotify           funcs_destroy;
	gboolean                 maximized;
};

G_DEFINE_FINAL_TYPE (LrgHeaderBar, lrg_header_bar, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_TITLE,
	PROP_SUBTITLE,
	PROP_TITLE_WIDGET,
	PROP_SHOW_WINDOW_BUTTONS,
	PROP_DECORATION_LAYOUT,
	PROP_DRAGGABLE,
	N_PROPS
};

enum
{
	SIGNAL_CLOSE_REQUESTED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

GType
lrg_header_bar_button_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_HEADER_BAR_BUTTON_NONE, "LRG_HEADER_BAR_BUTTON_NONE", "none" },
			{ LRG_HEADER_BAR_BUTTON_MINIMIZE, "LRG_HEADER_BAR_BUTTON_MINIMIZE", "minimize" },
			{ LRG_HEADER_BAR_BUTTON_MAXIMIZE, "LRG_HEADER_BAR_BUTTON_MAXIMIZE", "maximize" },
			{ LRG_HEADER_BAR_BUTTON_CLOSE, "LRG_HEADER_BAR_BUTTON_CLOSE", "close" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgHeaderBarButton"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Default (raylib) window functions
 * ========================================================================== */

/*
 * platform_is_wayland:
 *
 * Asks GLFW which platform it picked; without GLFW, guesses from the
 * session (a Wayland session without an X display cannot be X11).
 */
static gboolean
platform_is_wayland (void)
{
	const gchar *session;

#if defined(__ELF__)
	if (glfwGetPlatform != NULL)
	{
		int platform = glfwGetPlatform ();

		if (platform != 0)
			return platform == HEADER_BAR_GLFW_PLATFORM_WAYLAND;
	}
#endif

	session = g_getenv ("XDG_SESSION_TYPE");
	return g_strcmp0 (session, "wayland") == 0 && g_getenv ("DISPLAY") == NULL;
}

static gboolean
default_can_move (gpointer user_data)
{
	return IsWindowReady () && !platform_is_wayland ();
}

static void
default_get_position (gpointer  user_data,
                      gint     *x,
                      gint     *y)
{
	Vector2 pos = { 0.0f, 0.0f };

	if (IsWindowReady ())
		pos = GetWindowPosition ();
	*x = (gint) pos.x;
	*y = (gint) pos.y;
}

static void
default_set_position (gpointer user_data,
                      gint     x,
                      gint     y)
{
	if (IsWindowReady ())
		SetWindowPosition (x, y);
}

static void
default_minimize (gpointer user_data)
{
	if (IsWindowReady ())
		MinimizeWindow ();
}

static gboolean
default_is_maximized (gpointer user_data)
{
	return IsWindowReady () && IsWindowMaximized ();
}

static void
default_maximize (gpointer user_data)
{
	if (IsWindowReady ())
		MaximizeWindow ();
}

static void
default_restore (gpointer user_data)
{
	if (IsWindowReady ())
		RestoreWindow ();
}

static const LrgHeaderBarWindowFuncs default_funcs = {
	default_can_move,
	default_get_position,
	default_set_position,
	default_minimize,
	default_is_maximized,
	default_maximize,
	default_restore
};

const LrgHeaderBarWindowFuncs *
lrg_header_bar_get_default_window_funcs (void)
{
	return &default_funcs;
}

/* Thin wrappers: a NULL member is a no-op */

static gboolean
win_can_move (LrgHeaderBar *self)
{
	return self->funcs.can_move != NULL && self->funcs.can_move (self->funcs_data);
}

static gboolean
win_is_maximized (LrgHeaderBar *self)
{
	return self->funcs.is_maximized != NULL && self->funcs.is_maximized (self->funcs_data);
}

/* toggle_maximize: maximise, or restore when already maximised */
static void
toggle_maximize (LrgHeaderBar *self)
{
	if (win_is_maximized (self))
	{
		if (self->funcs.restore != NULL)
			self->funcs.restore (self->funcs_data);
	}
	else if (self->funcs.maximize != NULL)
		self->funcs.maximize (self->funcs_data);
	self->maximized = win_is_maximized (self);
}

/* move_window_by: shift the window by a pointer delta (screen pixels) */
static void
move_window_by (LrgHeaderBar *self,
                gfloat        dx,
                gfloat        dy)
{
	gint wx = 0;
	gint wy = 0;
	gint ix = (gint) roundf (dx);
	gint iy = (gint) roundf (dy);

	if ((ix == 0 && iy == 0) || !win_can_move (self))
		return;
	if (self->funcs.get_position != NULL)
		self->funcs.get_position (self->funcs_data, &wx, &wy);
	if (self->funcs.set_position != NULL)
		self->funcs.set_position (self->funcs_data, wx + ix, wy + iy);
}

/* ==========================================================================
 * Decoration layout
 * ========================================================================== */

/* button_from_name: a layout token to a button */
static LrgHeaderBarButton
button_from_name (const gchar *name)
{
	if (g_strcmp0 (name, "minimize") == 0)
		return LRG_HEADER_BAR_BUTTON_MINIMIZE;
	if (g_strcmp0 (name, "maximize") == 0)
		return LRG_HEADER_BAR_BUTTON_MAXIMIZE;
	if (g_strcmp0 (name, "close") == 0)
		return LRG_HEADER_BAR_BUTTON_CLOSE;
	return LRG_HEADER_BAR_BUTTON_NONE;
}

/*
 * parse_side:
 *
 * Fills @out with the known buttons of one comma-separated side,
 * skipping unknown names and buttons already used (@used).
 */
static guint
parse_side (const gchar        *side,
            LrgHeaderBarButton  out[N_BUTTONS],
            gboolean            used[N_BUTTONS])
{
	g_auto(GStrv) tokens = NULL;
	guint         n = 0;
	guint         i;

	for (i = 0; i < N_BUTTONS; i++)
		out[i] = LRG_HEADER_BAR_BUTTON_NONE;
	if (side == NULL)
		return 0;

	tokens = g_strsplit (side, ",", -1);
	for (i = 0; tokens[i] != NULL && n < N_BUTTONS; i++)
	{
		LrgHeaderBarButton b = button_from_name (g_strstrip (tokens[i]));

		if (b == LRG_HEADER_BAR_BUTTON_NONE)
		{
			if (tokens[i][0] != '\0')
				lrg_debug (LRG_LOG_DOMAIN, "header bar: ignoring decoration \"%s\"", tokens[i]);
			continue;
		}
		if (used[BUTTON_INDEX (b)])
			continue;
		used[BUTTON_INDEX (b)] = TRUE;
		out[n++] = b;
	}
	return n;
}

guint
lrg_header_bar_parse_decoration_layout (const gchar        *layout,
                                        LrgHeaderBarButton  start[3],
                                        LrgHeaderBarButton  end[3])
{
	g_autofree gchar *copy = NULL;
	gchar            *colon;
	gboolean          used[N_BUTTONS] = { FALSE, FALSE, FALSE };
	guint             n;

	g_return_val_if_fail (start != NULL, 0);
	g_return_val_if_fail (end != NULL, 0);

	copy = g_strdup (layout != NULL ? layout : "");
	colon = strchr (copy, ':');
	if (colon != NULL)
		*colon = '\0';

	/* Before the colon: leading edge; after it: trailing edge */
	n = parse_side (copy, start, used);
	n += parse_side (colon != NULL ? colon + 1 : NULL, end, used);
	return n;
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

static gfloat
metric (LrgHeaderBar   *self,
        LrgThemeMetric  m)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), m);
}

static GQuark
pack_end_quark (void)
{
	return g_quark_from_static_string ("lrg-header-bar-pack-end");
}

/* is_end_child: whether @child was packed at the end */
static gboolean
is_end_child (LrgWidget *child)
{
	return g_object_get_qdata (G_OBJECT (child), pack_end_quark ()) != NULL;
}

/* title_widget: the custom title widget if it is still our child */
static LrgWidget *
current_title_widget (LrgHeaderBar *self)
{
	if (self->title_widget != NULL &&
	    lrg_widget_get_parent (self->title_widget) == LRG_CONTAINER (self))
		return self->title_widget;
	return NULL;
}

/* text_size: measured size of @text at @size with the UI font */
static void
text_size (LrgHeaderBar *self,
           const gchar  *text,
           gfloat        size,
           gfloat       *w,
           gfloat       *h)
{
	GrlFont *font = lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self)));

	*w = 0.0f;
	*h = 0.0f;
	if (text != NULL && text[0] != '\0')
		lrg_paint_measure_text (font, text, size, 1.0f, w, h);
}

/* title_font_size / subtitle_font_size */
static gfloat
title_font_size (LrgHeaderBar *self)
{
	return metric (self, LRG_THEME_METRIC_FONT_SIZE);
}

static gfloat
subtitle_font_size (LrgHeaderBar *self)
{
	return metric (self, LRG_THEME_METRIC_FONT_SIZE_SMALL);
}

/* title_size: the preferred size of the title block */
static void
title_size (LrgHeaderBar *self,
            gfloat       *w,
            gfloat       *h)
{
	LrgWidget *tw = current_title_widget (self);
	gfloat     t_w, t_h, s_w, s_h;

	if (tw != NULL)
	{
		LrgInsets m;

		*w = 0.0f;
		*h = 0.0f;
		if (!lrg_widget_get_visible (tw))
			return;
		lrg_widget_get_preferred_size (tw, w, h);
		lrg_widget_get_margin (tw, &m);
		/* Whole units: slots snap to pixels, and a title widget given
		 * 251 for a natural 251.2 may think it does not fit (a
		 * breadcrumb would collapse) */
		*w = ceilf (*w + m.left + m.right);
		*h = ceilf (*h + m.top + m.bottom);
		return;
	}

	text_size (self, self->title, title_font_size (self), &t_w, &t_h);
	text_size (self, self->subtitle, subtitle_font_size (self), &s_w, &s_h);
	*w = ceilf (MAX (t_w, s_w));
	*h = ceilf (t_h + (s_h > 0.0f ? s_h + 1.0f : 0.0f));
}

/* button_metrics: diameter of a window button and the gap between two */
static gfloat
button_size (LrgHeaderBar *self)
{
	return roundf (metric (self, LRG_THEME_METRIC_CONTROL_HEIGHT) * HEADER_BAR_BUTTON_SCALE);
}

static gfloat
button_gap (LrgHeaderBar *self)
{
	return roundf (MAX (2.0f, metric (self, LRG_THEME_METRIC_SPACING) * 0.5f));
}

/* side_width: width of the buttons on one side (0 when none / hidden) */
static gfloat
side_width (LrgHeaderBar             *self,
            const LrgHeaderBarButton  side[N_BUTTONS])
{
	guint n = 0;
	guint i;

	if (!self->show_window_buttons)
		return 0.0f;
	for (i = 0; i < N_BUTTONS; i++)
		if (side[i] != LRG_HEADER_BAR_BUTTON_NONE)
			n++;
	if (n == 0)
		return 0.0f;
	return (gfloat) n * button_size (self) + (gfloat) (n - 1) * button_gap (self);
}

/* child_outer_size: preferred size of @child including its margin */
static void
child_outer_size (LrgWidget *child,
                  gfloat    *w,
                  gfloat    *h)
{
	LrgInsets m;

	lrg_widget_get_preferred_size (child, w, h);
	lrg_widget_get_margin (child, &m);
	*w += m.left + m.right;
	*h += m.top + m.bottom;
}

/* button_world_rect: world rectangle of a shown button (mirrored in RTL) */
static void
button_world_rect (LrgHeaderBar       *self,
                   LrgHeaderBarButton  b,
                   gfloat             *x,
                   gfloat             *y,
                   gfloat             *w,
                   gfloat             *h)
{
	LrgWidget *widget = LRG_WIDGET (self);
	gfloat     s = self->button_size;

	*w = s;
	*h = s;
	*x = lrg_widget_mirror_x (widget, lrg_widget_get_world_x (widget) + self->button_x[BUTTON_INDEX (b)], s);
	*y = roundf (lrg_widget_get_world_y (widget) + (lrg_widget_get_height (widget) - s) * 0.5f);
}

/* button_at: the shown window button under a world point */
static LrgHeaderBarButton
button_at (LrgHeaderBar *self,
           gfloat        px,
           gfloat        py)
{
	gint i;

	for (i = 0; i < N_BUTTONS; i++)
	{
		gfloat x, y, w, h;

		if (!self->button_shown[i])
			continue;
		button_world_rect (self, (LrgHeaderBarButton) (i + 1), &x, &y, &w, &h);
		if (px >= x && px < x + w && py >= y && py < y + h)
			return (LrgHeaderBarButton) (i + 1);
	}
	return LRG_HEADER_BAR_BUTTON_NONE;
}

/*
 * over_interactive:
 *
 * Whether the point is over @widget or a descendant that takes input
 * (focusable and enabled). Inert widgets such as labels and icons
 * count as drag area.
 */
static gboolean
over_interactive (LrgWidget *widget,
                  gfloat     x,
                  gfloat     y)
{
	GList *l;

	if (!lrg_widget_get_visible (widget) || !lrg_widget_contains_point (widget, x, y))
		return FALSE;
	if (lrg_widget_get_focusable (widget) && lrg_widget_get_enabled (widget))
		return TRUE;
	if (LRG_IS_CONTAINER (widget))
		for (l = lrg_container_get_children (LRG_CONTAINER (widget)); l != NULL; l = l->next)
			if (over_interactive (LRG_WIDGET (l->data), x, y))
				return TRUE;
	return FALSE;
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

/* place_buttons: lay one side's buttons out from @x; returns the end x */
static gfloat
place_buttons (LrgHeaderBar             *self,
               const LrgHeaderBarButton  side[N_BUTTONS],
               gfloat                    x)
{
	gfloat s = self->button_size;
	gfloat gap = button_gap (self);
	guint  i;

	for (i = 0; i < N_BUTTONS; i++)
	{
		LrgHeaderBarButton b = side[i];

		if (b == LRG_HEADER_BAR_BUTTON_NONE)
			continue;
		self->button_x[BUTTON_INDEX (b)] = x;
		self->button_shown[BUTTON_INDEX (b)] = TRUE;
		x += s + gap;
	}
	return x;
}

/* place_child: give @child its preferred width at @x, centred vertically */
static void
place_child (LrgHeaderBar *self,
             LrgWidget    *child,
             gfloat        x,
             gfloat        w)
{
	gfloat bar_h = lrg_widget_get_height (LRG_WIDGET (self));
	gfloat pad_y = roundf (metric (self, LRG_THEME_METRIC_PADDING_Y) * 0.5f);
	gfloat cw, ch;

	child_outer_size (child, &cw, &ch);
	ch = MIN (ch, MAX (0.0f, bar_h - pad_y * 2.0f));
	lrg_widget_place_in_slot (child, x, roundf ((bar_h - ch) * 0.5f), w, ch);
}

/*
 * lrg_header_bar_layout_children:
 *
 * Window buttons at both edges, start children after the leading
 * buttons, end children before the trailing ones (packed inwards),
 * and the title centred on the bar but kept inside the free space
 * between the two groups. Everything in left-to-right local
 * coordinates; the core mirrors children in right-to-left.
 */
static void
lrg_header_bar_layout_children (LrgContainer *container)
{
	LrgHeaderBar *self = LRG_HEADER_BAR (container);
	gfloat        bar_w = lrg_widget_get_width (LRG_WIDGET (self));
	gfloat        pad = roundf (metric (self, LRG_THEME_METRIC_PADDING_X) * 0.5f);
	gfloat        spacing = metric (self, LRG_THEME_METRIC_SPACING);
	gfloat        left;
	gfloat        right;
	gfloat        tw, th;
	LrgWidget    *title_w = current_title_widget (self);
	GList        *l;
	gint          i;

	/* Window buttons */
	self->button_size = button_size (self);
	for (i = 0; i < N_BUTTONS; i++)
		self->button_shown[i] = FALSE;
	left = pad;
	right = bar_w - pad;
	if (self->show_window_buttons)
	{
		gfloat lead_w = side_width (self, self->lead);
		gfloat trail_w = side_width (self, self->trail);

		place_buttons (self, self->lead, left);
		place_buttons (self, self->trail, right - trail_w);
		if (lead_w > 0.0f)
			left += lead_w + spacing;
		if (trail_w > 0.0f)
			right -= trail_w + spacing;
	}

	/* Start children, from the leading edge */
	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		LrgWidget *child = LRG_WIDGET (l->data);
		gfloat     cw, ch;

		if (child == title_w || is_end_child (child) || !lrg_widget_get_visible (child))
			continue;
		child_outer_size (child, &cw, &ch);
		place_child (self, child, left, cw);
		left += cw + spacing;
	}

	/* End children, from the trailing edge inwards */
	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		LrgWidget *child = LRG_WIDGET (l->data);
		gfloat     cw, ch;

		if (child == title_w || !is_end_child (child) || !lrg_widget_get_visible (child))
			continue;
		child_outer_size (child, &cw, &ch);
		place_child (self, child, right - cw, cw);
		right -= cw + spacing;
	}

	/* Title: centred on the bar, pushed into the free space if needed */
	title_size (self, &tw, &th);
	{
		gfloat free_w = MAX (0.0f, right - left);
		gfloat x;

		tw = MIN (tw, free_w);
		x = roundf ((bar_w - tw) * 0.5f);
		x = CLAMP (x, left, MAX (left, right - tw));
		self->title_x = x;
		self->title_w = tw;
		if (title_w != NULL && lrg_widget_get_visible (title_w))
			place_child (self, title_w, x, tw);
	}
}

static void
lrg_header_bar_measure (LrgWidget *widget,
                        gfloat    *width,
                        gfloat    *height)
{
	LrgHeaderBar *self = LRG_HEADER_BAR (widget);
	gfloat        pad_x = roundf (metric (self, LRG_THEME_METRIC_PADDING_X) * 0.5f);
	gfloat        pad_y = roundf (metric (self, LRG_THEME_METRIC_PADDING_Y) * 0.5f);
	gfloat        spacing = metric (self, LRG_THEME_METRIC_SPACING);
	gfloat        w = pad_x * 2.0f;
	gfloat        h = metric (self, LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat        tw, th;
	LrgWidget    *title_w = current_title_widget (self);
	GList        *l;

	/* Buttons on both sides */
	if (side_width (self, self->lead) > 0.0f)
		w += side_width (self, self->lead) + spacing;
	if (side_width (self, self->trail) > 0.0f)
		w += side_width (self, self->trail) + spacing;

	/* Children */
	for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
	{
		LrgWidget *child = LRG_WIDGET (l->data);
		gfloat     cw, ch;

		if (child == title_w || !lrg_widget_get_visible (child))
			continue;
		child_outer_size (child, &cw, &ch);
		w += cw + spacing;
		h = MAX (h, ch);
	}

	/* Title */
	title_size (self, &tw, &th);
	w += tw;
	h = MAX (h, th);

	*width = ceilf (w);
	*height = ceilf (h + pad_y * 2.0f + 4.0f);
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

static void
lrg_header_bar_style_defaults (LrgWidget      *widget,
                               LrgUIPart       part,
                               LrgWidgetState  state,
                               LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* The bar: panel surface, hairline border at the bottom */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          MAX (1.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH)));
		lrg_part_look_set_radius (look, 0.0f);
		break;

	case LRG_UI_PART_LABEL:
		/* Title */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		break;

	case LRG_UI_PART_HEADER:
		/* Subtitle */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		break;

	case LRG_UI_PART_ITEM:
		{
			/*
			 * Window buttons: a faint disc at rest, control shades on
			 * hover / press. SELECTED marks the close button, which
			 * turns DANGER with on-accent glyphs when hovered.
			 */
			gboolean close = (state & LRG_WIDGET_STATE_SELECTED) != 0;
			GrlColor rest;

			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL), 0.55f, &rest);
			if (close && (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED)))
			{
				GrlColor danger = *lrg_theme_get_color (theme, LRG_THEME_COLOR_DANGER);

				if (state & LRG_WIDGET_STATE_PRESSED)
					lrg_paint_color_shade (&danger, -0.15f, &danger);
				lrg_part_look_set_background (look, &danger);
				lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_ON_ACCENT));
			}
			else
			{
				if (state & LRG_WIDGET_STATE_PRESSED)
					lrg_part_look_set_background (look, lrg_theme_get_color (theme,
					                                                         LRG_THEME_COLOR_CONTROL_PRESSED));
				else if (state & LRG_WIDGET_STATE_HOVERED)
					lrg_part_look_set_background (look, lrg_theme_get_color (theme,
					                                                         LRG_THEME_COLOR_CONTROL_HOVER));
				else
					lrg_part_look_set_background (look, &rest);
				lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
			}
			lrg_part_look_set_radius (look, G_MAXFLOAT);
			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, 0.45f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_header_bar_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

/* paint_glyph: the icon of window button @b in a box at (@x, @y, @s) */
static void
paint_glyph (LrgHeaderBar       *self,
             LrgPaintList       *list,
             LrgHeaderBarButton  b,
             gfloat              x,
             gfloat              y,
             gfloat              s,
             const GrlColor     *color)
{
	gfloat cx = x + s * 0.5f;
	gfloat cy = y + s * 0.5f;
	gfloat g = roundf (s * 0.36f);         /* glyph extent */
	gfloat t = MAX (1.25f, s * 0.06f);     /* stroke */

	switch (b)
	{
	case LRG_HEADER_BAR_BUTTON_MINIMIZE:
		lrg_paint_list_line (list, cx - g * 0.5f, cy + g * 0.25f, cx + g * 0.5f, cy + g * 0.25f, t, color);
		break;

	case LRG_HEADER_BAR_BUTTON_MAXIMIZE:
		if (self->maximized)
		{
			/* Restore: a front frame and the visible corner of one
			 * behind it, up and to the right */
			gfloat f = roundf (g * 0.78f);
			gfloat o = roundf (g - f);
			gfloat x0 = roundf (cx - g * 0.5f);
			gfloat y0 = roundf (cy - g * 0.5f);

			lrg_paint_list_border (list, x0, y0 + o, f, f, 1.5f, t, color);
			lrg_paint_list_line (list, x0 + o, y0 + o, x0 + o, y0, t, color);
			lrg_paint_list_line (list, x0 + o, y0, x0 + g, y0, t, color);
			lrg_paint_list_line (list, x0 + g, y0, x0 + g, y0 + f, t, color);
			lrg_paint_list_line (list, x0 + g, y0 + f, x0 + f, y0 + f, t, color);
		}
		else
			lrg_paint_list_border (list, roundf (cx - g * 0.5f), roundf (cy - g * 0.5f), g, g, 2.0f, t, color);
		break;

	case LRG_HEADER_BAR_BUTTON_CLOSE:
		/* Two strokes, the same extent as the other glyphs */
		lrg_paint_list_line (list, cx - g * 0.42f, cy - g * 0.42f, cx + g * 0.42f, cy + g * 0.42f, t, color);
		lrg_paint_list_line (list, cx - g * 0.42f, cy + g * 0.42f, cx + g * 0.42f, cy - g * 0.42f, t, color);
		break;

	case LRG_HEADER_BAR_BUTTON_NONE:
	default:
		break;
	}
}

/* paint_buttons: every shown window button, blended by its springs */
static void
paint_buttons (LrgHeaderBar *self,
               LrgPaintList *list)
{
	LrgWidget      *widget = LRG_WIDGET (self);
	LrgWidgetState  disabled = lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_DISABLED;
	gint            i;

	for (i = 0; i < N_BUTTONS; i++)
	{
		LrgHeaderBarButton b = (LrgHeaderBarButton) (i + 1);
		LrgWidgetState     base = disabled;
		LrgPartLook        rest, lit, down, look;
		gfloat             hov, prs;
		gfloat             x, y, w, h;
		gfloat             inset;

		if (!self->button_shown[i])
			continue;
		if (b == LRG_HEADER_BAR_BUTTON_CLOSE)
			base |= LRG_WIDGET_STATE_SELECTED;

		if (self->stepped)
		{
			hov = CLAMP (self->hover[i].value, 0.0f, 1.0f);
			prs = CLAMP (self->press[i].value, 0.0f, 1.0f);
		}
		else
		{
			hov = self->hover_button == b ? 1.0f : 0.0f;
			prs = self->press_button == b && self->hover_button == b ? 1.0f : 0.0f;
		}

		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, base, &rest);
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, base | LRG_WIDGET_STATE_HOVERED, &lit);
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM,
		                                   base | LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED, &down);
		lrg_part_look_lerp (&rest, &lit, hov, &look);
		lrg_part_look_lerp (&look, &down, prs, &look);

		button_world_rect (self, b, &x, &y, &w, &h);
		inset = prs * 1.0f;
		lrg_widget_paint_box (widget, list, &look, x + inset, y + inset, w - inset * 2.0f, h - inset * 2.0f);
		if (look.opacity < 1.0f)
			lrg_paint_list_push_opacity (list, look.opacity);
		paint_glyph (self, list, b, x, y, w, &look.foreground);
		if (look.opacity < 1.0f)
			lrg_paint_list_pop_opacity (list);
	}
}

/* paint_title: title and subtitle centred in the title slot, clipped */
static void
paint_title (LrgHeaderBar *self,
             LrgPaintList *list)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	GrlFont     *font = lrg_theme_get_ui_font (lrg_widget_get_theme (widget));
	LrgPartLook  title;
	LrgPartLook  sub;
	gfloat       t_w, t_h, s_w, s_h;
	gfloat       block_h;
	gfloat       x;
	gfloat       y;
	gfloat       cx;
	gfloat       h = lrg_widget_get_height (widget);

	if (self->title_w <= 0.5f || current_title_widget (self) != NULL)
		return;

	text_size (self, self->title, title_font_size (self), &t_w, &t_h);
	text_size (self, self->subtitle, subtitle_font_size (self), &s_w, &s_h);
	block_h = t_h + (s_h > 0.0f ? s_h + 1.0f : 0.0f);

	x = lrg_widget_mirror_x (widget, lrg_widget_get_world_x (widget) + self->title_x, self->title_w);
	cx = x + self->title_w * 0.5f;
	y = roundf (lrg_widget_get_world_y (widget) + (h - block_h) * 0.5f);

	lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &title);
	lrg_widget_resolve_look (widget, LRG_UI_PART_HEADER, &sub);

	/* Text wider than the free space is clipped (from its centre out) */
	lrg_paint_list_push_clip (list, x, lrg_widget_get_world_y (widget), self->title_w, h);
	if (t_h > 0.0f)
		lrg_paint_list_text (list, font, self->title, roundf (cx - t_w * 0.5f), y,
		                     title_font_size (self), 1.0f, &title.foreground);
	if (s_h > 0.0f)
		lrg_paint_list_text (list, font, self->subtitle, roundf (cx - s_w * 0.5f), y + t_h + 1.0f,
		                     subtitle_font_size (self), 1.0f, &sub.foreground);
	lrg_paint_list_pop_clip (list);
}

/*
 * lrg_header_bar_paint:
 *
 * The bar surface (fill, material, shadow from the BOX look, but the
 * border only as a hairline along the bottom), the title, the
 * children, then the window buttons.
 */
static void
lrg_header_bar_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
	LrgHeaderBar *self = LRG_HEADER_BAR (widget);
	LrgPartLook   box;
	LrgPartLook   surface;
	gfloat        x = lrg_widget_get_world_x (widget);
	gfloat        y = lrg_widget_get_world_y (widget);
	gfloat        w = lrg_widget_get_width (widget);
	gfloat        h = lrg_widget_get_height (widget);

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	surface = box;
	surface.border_width = 0.0f;
	lrg_widget_paint_box (widget, list, &surface, x, y, w, h);
	if (box.border_width > 0.0f && box.border.a > 0)
		lrg_paint_list_rect (list, x, y + h - box.border_width, w, box.border_width, 0.0f, &box.border);

	paint_title (self, list);
	LRG_WIDGET_CLASS (lrg_header_bar_parent_class)->paint (widget, list);
	paint_buttons (self, list);
}

/* ==========================================================================
 * Animation and input
 * ========================================================================== */

/*
 * lrg_header_bar_step:
 *
 * Runs the double-click clock, refreshes the cached maximised state
 * (it picks the maximise / restore glyph) and moves the per-button
 * springs. The first step snaps.
 */
static gboolean
lrg_header_bar_step (LrgWidget *widget,
                     gfloat     delta_time)
{
	LrgHeaderBar *self = LRG_HEADER_BAR (widget);
	LrgMotion     motion = lrg_widget_get_motion (widget);
	gboolean      snap = !self->stepped || motion == LRG_MOTION_NONE;
	gboolean      busy = FALSE;
	gint          i;

	self->clock += (gdouble) delta_time;
	self->maximized = win_is_maximized (self);

	for (i = 0; i < N_BUTTONS; i++)
	{
		LrgHeaderBarButton b = (LrgHeaderBarButton) (i + 1);
		gboolean           live = self->button_shown[i] && lrg_widget_get_enabled (widget);

		lrg_spring_set_motion (&self->hover[i], motion);
		lrg_spring_set_motion (&self->press[i], motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
		lrg_spring_set_target (&self->hover[i], live && self->hover_button == b ? 1.0f : 0.0f);
		lrg_spring_set_target (&self->press[i],
		                       live && self->press_button == b && self->hover_button == b ? 1.0f : 0.0f);
		if (snap)
		{
			lrg_spring_snap (&self->hover[i], self->hover[i].target);
			lrg_spring_snap (&self->press[i], self->press[i].target);
		}
		else
		{
			busy |= lrg_spring_step (&self->hover[i], delta_time);
			busy |= lrg_spring_step (&self->press[i], delta_time);
		}
	}

	self->stepped = TRUE;
	return busy;
}

/*
 * press_drag_area:
 *
 * A left press on empty bar space: the second press of a double click
 * toggles maximise; otherwise a window drag starts.
 */
static void
press_drag_area (LrgHeaderBar *self,
                 gfloat        ex,
                 gfloat        ey)
{
	gfloat dx = ex - self->last_click_x;
	gfloat dy = ey - self->last_click_y;

	if (self->last_click_time >= 0.0 &&
	    self->clock - self->last_click_time <= HEADER_BAR_DOUBLE_CLICK_TIME &&
	    dx * dx + dy * dy <= HEADER_BAR_DOUBLE_CLICK_DISTANCE * HEADER_BAR_DOUBLE_CLICK_DISTANCE)
	{
		self->last_click_time = -1.0;
		self->dragging = FALSE;
		toggle_maximize (self);
		return;
	}

	self->last_click_time = self->clock;
	self->last_click_x = ex;
	self->last_click_y = ey;
	self->dragging = TRUE;
	self->drag_x = ex;
	self->drag_y = ey;
}

static gboolean
lrg_header_bar_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
	LrgHeaderBar *self = LRG_HEADER_BAR (widget);
	gfloat        ex = lrg_ui_event_get_x (event);
	gfloat        ey = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		/*
		 * While dragging, the window follows the pointer: the delta
		 * from the press point is the distance the pointer moved on
		 * screen, and moving the window by it puts the press point
		 * back under the pointer.
		 */
		if (self->dragging)
		{
			move_window_by (self, ex - self->drag_x, ey - self->drag_y);
			return TRUE;
		}
		self->hover_button = lrg_widget_contains_point (widget, ex, ey) ? button_at (self, ex, ey)
		                                                                 : LRG_HEADER_BAR_BUTTON_NONE;
		return self->hover_button != LRG_HEADER_BAR_BUTTON_NONE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_button = LRG_HEADER_BAR_BUTTON_NONE;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			LrgHeaderBarButton b;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			b = button_at (self, ex, ey);
			if (b != LRG_HEADER_BAR_BUTTON_NONE)
			{
				self->hover_button = b;
				self->press_button = b;
				return TRUE;
			}
			if (self->draggable && lrg_header_bar_is_drag_area (self, ex, ey))
			{
				press_drag_area (self, ex, ey);
				return TRUE;
			}
			return FALSE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		{
			LrgHeaderBarButton pressed = self->press_button;
			gboolean           was_dragging = self->dragging;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			self->press_button = LRG_HEADER_BAR_BUTTON_NONE;
			self->dragging = FALSE;
			if (pressed != LRG_HEADER_BAR_BUTTON_NONE)
			{
				/* A click lands when released over the pressed button */
				if (button_at (self, ex, ey) == pressed)
					lrg_header_bar_activate_button (self, pressed);
				self->hover_button = lrg_widget_contains_point (widget, ex, ey) ? button_at (self, ex, ey)
				                                                                 : LRG_HEADER_BAR_BUTTON_NONE;
				return TRUE;
			}
			return was_dragging;
		}

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
clear_funcs (LrgHeaderBar *self)
{
	if (self->funcs_destroy != NULL)
		self->funcs_destroy (self->funcs_data);
	self->funcs_destroy = NULL;
	self->funcs_data = NULL;
}

static void
lrg_header_bar_dispose (GObject *object)
{
	LrgHeaderBar *self = LRG_HEADER_BAR (object);

	if (self->title_widget != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (self->title_widget), (gpointer *) &self->title_widget);
		self->title_widget = NULL;
	}

	G_OBJECT_CLASS (lrg_header_bar_parent_class)->dispose (object);
}

static void
lrg_header_bar_finalize (GObject *object)
{
	LrgHeaderBar *self = LRG_HEADER_BAR (object);

	clear_funcs (self);
	g_clear_pointer (&self->title, g_free);
	g_clear_pointer (&self->subtitle, g_free);
	g_clear_pointer (&self->decoration_layout, g_free);

	G_OBJECT_CLASS (lrg_header_bar_parent_class)->finalize (object);
}

static void
lrg_header_bar_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgHeaderBar *self = LRG_HEADER_BAR (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		g_value_set_string (value, self->title);
		break;
	case PROP_SUBTITLE:
		g_value_set_string (value, self->subtitle);
		break;
	case PROP_TITLE_WIDGET:
		g_value_set_object (value, current_title_widget (self));
		break;
	case PROP_SHOW_WINDOW_BUTTONS:
		g_value_set_boolean (value, self->show_window_buttons);
		break;
	case PROP_DECORATION_LAYOUT:
		g_value_set_string (value, self->decoration_layout);
		break;
	case PROP_DRAGGABLE:
		g_value_set_boolean (value, self->draggable);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_header_bar_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgHeaderBar *self = LRG_HEADER_BAR (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		lrg_header_bar_set_title (self, g_value_get_string (value));
		break;
	case PROP_SUBTITLE:
		lrg_header_bar_set_subtitle (self, g_value_get_string (value));
		break;
	case PROP_TITLE_WIDGET:
		lrg_header_bar_set_title_widget (self, g_value_get_object (value));
		break;
	case PROP_SHOW_WINDOW_BUTTONS:
		lrg_header_bar_set_show_window_buttons (self, g_value_get_boolean (value));
		break;
	case PROP_DECORATION_LAYOUT:
		lrg_header_bar_set_decoration_layout (self, g_value_get_string (value));
		break;
	case PROP_DRAGGABLE:
		lrg_header_bar_set_draggable (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_header_bar_class_init (LrgHeaderBarClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_header_bar_dispose;
	object_class->finalize = lrg_header_bar_finalize;
	object_class->get_property = lrg_header_bar_get_property;
	object_class->set_property = lrg_header_bar_set_property;

	widget_class->paint = lrg_header_bar_paint;
	widget_class->style_defaults = lrg_header_bar_style_defaults;
	widget_class->measure = lrg_header_bar_measure;
	widget_class->handle_event = lrg_header_bar_handle_event;
	widget_class->step = lrg_header_bar_step;

	container_class->layout_children = lrg_header_bar_layout_children;

	/**
	 * LrgHeaderBar:title:
	 *
	 * The title, centred on the bar (ignored while a title widget is set).
	 */
	properties[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgHeaderBar:subtitle:
	 *
	 * A smaller, muted line under the title.
	 */
	properties[PROP_SUBTITLE] =
		g_param_spec_string ("subtitle", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgHeaderBar:title-widget:
	 *
	 * A widget centred in place of the title text.
	 */
	properties[PROP_TITLE_WIDGET] =
		g_param_spec_object ("title-widget", NULL, NULL, LRG_TYPE_WIDGET,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgHeaderBar:show-window-buttons:
	 *
	 * Whether the minimise / maximise / close buttons are drawn.
	 */
	properties[PROP_SHOW_WINDOW_BUTTONS] =
		g_param_spec_boolean ("show-window-buttons", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgHeaderBar:decoration-layout:
	 *
	 * GTK-style button layout, e.g. ":minimize,maximize,close" or "close:".
	 */
	properties[PROP_DECORATION_LAYOUT] =
		g_param_spec_string ("decoration-layout", NULL, NULL, LRG_HEADER_BAR_DEFAULT_DECORATION_LAYOUT,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgHeaderBar:draggable:
	 *
	 * Whether dragging empty bar space moves the window and double
	 * clicks toggle maximise.
	 */
	properties[PROP_DRAGGABLE] =
		g_param_spec_boolean ("draggable", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgHeaderBar::close-requested:
	 * @self: the header bar
	 *
	 * Emitted when the close button is clicked, before "app.quit" is
	 * activated. Return %TRUE from a handler to stop the quit (to ask
	 * about unsaved changes first, or to hide instead of closing).
	 *
	 * Returns: %TRUE to stop "app.quit"
	 */
	signals[SIGNAL_CLOSE_REQUESTED] =
		g_signal_new ("close-requested", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, g_signal_accumulator_true_handled, NULL, NULL, G_TYPE_BOOLEAN, 0);
}

static void
lrg_header_bar_init (LrgHeaderBar *self)
{
	gint i;

	self->show_window_buttons = TRUE;
	self->draggable = TRUE;
	self->decoration_layout = g_strdup (LRG_HEADER_BAR_DEFAULT_DECORATION_LAYOUT);
	lrg_header_bar_parse_decoration_layout (self->decoration_layout, self->lead, self->trail);
	self->hover_button = LRG_HEADER_BAR_BUTTON_NONE;
	self->press_button = LRG_HEADER_BAR_BUTTON_NONE;
	self->last_click_time = -1.0;
	self->funcs = default_funcs;
	for (i = 0; i < N_BUTTONS; i++)
	{
		lrg_spring_init (&self->hover[i], 0.0f, LRG_MOTION_SMOOTH);
		lrg_spring_init (&self->press[i], 0.0f, LRG_MOTION_SNAPPY);
	}
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgHeaderBar *
lrg_header_bar_new (void)
{
	return g_object_new (LRG_TYPE_HEADER_BAR, NULL);
}

void
lrg_header_bar_pack_start (LrgHeaderBar *self,
                           LrgWidget    *child)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));
	g_return_if_fail (LRG_IS_WIDGET (child));

	g_object_set_qdata (G_OBJECT (child), pack_end_quark (), NULL);
	lrg_container_add_child (LRG_CONTAINER (self), child);
}

void
lrg_header_bar_pack_end (LrgHeaderBar *self,
                         LrgWidget    *child)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));
	g_return_if_fail (LRG_IS_WIDGET (child));

	g_object_set_qdata (G_OBJECT (child), pack_end_quark (), GINT_TO_POINTER (1));
	lrg_container_add_child (LRG_CONTAINER (self), child);
}

gboolean
lrg_header_bar_get_child_packing (LrgHeaderBar *self,
                                  LrgWidget    *child)
{
	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), FALSE);
	g_return_val_if_fail (LRG_IS_WIDGET (child), FALSE);

	return child != current_title_widget (self) && is_end_child (child);
}

void
lrg_header_bar_set_child_packing (LrgHeaderBar *self,
                                  LrgWidget    *child,
                                  gboolean      pack_end)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));
	g_return_if_fail (LRG_IS_WIDGET (child));

	g_object_set_qdata (G_OBJECT (child), pack_end_quark (), pack_end ? GINT_TO_POINTER (1) : NULL);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

const gchar *
lrg_header_bar_get_title (LrgHeaderBar *self)
{
	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), NULL);

	return self->title;
}

void
lrg_header_bar_set_title (LrgHeaderBar *self,
                          const gchar  *title)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));

	if (g_strcmp0 (self->title, title) == 0)
		return;
	g_free (self->title);
	self->title = g_strdup (title);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

const gchar *
lrg_header_bar_get_subtitle (LrgHeaderBar *self)
{
	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), NULL);

	return self->subtitle;
}

void
lrg_header_bar_set_subtitle (LrgHeaderBar *self,
                             const gchar  *subtitle)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));

	if (g_strcmp0 (self->subtitle, subtitle) == 0)
		return;
	g_free (self->subtitle);
	self->subtitle = g_strdup (subtitle);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SUBTITLE]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

LrgWidget *
lrg_header_bar_get_title_widget (LrgHeaderBar *self)
{
	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), NULL);

	return current_title_widget (self);
}

void
lrg_header_bar_set_title_widget (LrgHeaderBar *self,
                                 LrgWidget    *widget)
{
	LrgWidget *old;

	g_return_if_fail (LRG_IS_HEADER_BAR (self));
	g_return_if_fail (widget == NULL || LRG_IS_WIDGET (widget));

	old = current_title_widget (self);
	if (old == widget)
		return;

	/* Drop the old one (as a child too) */
	if (self->title_widget != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (self->title_widget), (gpointer *) &self->title_widget);
		self->title_widget = NULL;
	}
	if (old != NULL)
		lrg_container_remove_child (LRG_CONTAINER (self), old);

	if (widget != NULL)
	{
		self->title_widget = widget;
		g_object_add_weak_pointer (G_OBJECT (widget), (gpointer *) &self->title_widget);
		if (lrg_widget_get_parent (widget) != LRG_CONTAINER (self))
			lrg_container_add_child (LRG_CONTAINER (self), widget);
	}

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE_WIDGET]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_header_bar_get_show_window_buttons (LrgHeaderBar *self)
{
	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), FALSE);

	return self->show_window_buttons;
}

void
lrg_header_bar_set_show_window_buttons (LrgHeaderBar *self,
                                        gboolean      show)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));

	show = !!show;
	if (self->show_window_buttons == show)
		return;
	self->show_window_buttons = show;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_WINDOW_BUTTONS]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

const gchar *
lrg_header_bar_get_decoration_layout (LrgHeaderBar *self)
{
	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), NULL);

	return self->decoration_layout;
}

void
lrg_header_bar_set_decoration_layout (LrgHeaderBar *self,
                                      const gchar  *layout)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));

	if (layout == NULL)
		layout = LRG_HEADER_BAR_DEFAULT_DECORATION_LAYOUT;
	if (g_strcmp0 (self->decoration_layout, layout) == 0)
		return;
	g_free (self->decoration_layout);
	self->decoration_layout = g_strdup (layout);
	lrg_header_bar_parse_decoration_layout (layout, self->lead, self->trail);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DECORATION_LAYOUT]);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

gboolean
lrg_header_bar_get_draggable (LrgHeaderBar *self)
{
	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), FALSE);

	return self->draggable;
}

void
lrg_header_bar_set_draggable (LrgHeaderBar *self,
                              gboolean      draggable)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));

	draggable = !!draggable;
	if (self->draggable == draggable)
		return;
	self->draggable = draggable;
	if (!draggable)
		self->dragging = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRAGGABLE]);
}

gboolean
lrg_header_bar_is_drag_area (LrgHeaderBar *self,
                             gfloat        x,
                             gfloat        y)
{
	GList *l;

	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), FALSE);

	if (!lrg_widget_contains_point (LRG_WIDGET (self), x, y))
		return FALSE;
	if (button_at (self, x, y) != LRG_HEADER_BAR_BUTTON_NONE)
		return FALSE;
	for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
		if (over_interactive (LRG_WIDGET (l->data), x, y))
			return FALSE;
	return TRUE;
}

LrgHeaderBarButton
lrg_header_bar_get_button_at (LrgHeaderBar *self,
                              gfloat        x,
                              gfloat        y)
{
	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), LRG_HEADER_BAR_BUTTON_NONE);

	return button_at (self, x, y);
}

gboolean
lrg_header_bar_get_button_rect (LrgHeaderBar       *self,
                                LrgHeaderBarButton  button,
                                gfloat             *x,
                                gfloat             *y,
                                gfloat             *width,
                                gfloat             *height)
{
	gfloat bx, by, bw, bh;

	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), FALSE);

	if (button == LRG_HEADER_BAR_BUTTON_NONE || BUTTON_INDEX (button) >= N_BUTTONS ||
	    !self->button_shown[BUTTON_INDEX (button)])
		return FALSE;
	button_world_rect (self, button, &bx, &by, &bw, &bh);
	if (x != NULL)
		*x = bx;
	if (y != NULL)
		*y = by;
	if (width != NULL)
		*width = bw;
	if (height != NULL)
		*height = bh;
	return TRUE;
}

void
lrg_header_bar_activate_button (LrgHeaderBar       *self,
                                LrgHeaderBarButton  button)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));

	switch (button)
	{
	case LRG_HEADER_BAR_BUTTON_CLOSE:
		{
			gboolean handled = FALSE;

			g_signal_emit (self, signals[SIGNAL_CLOSE_REQUESTED], 0, &handled);
			if (!handled && !lrg_widget_activate_action (LRG_WIDGET (self), "app.quit", NULL))
				lrg_debug (LRG_LOG_DOMAIN, "header bar: no app.quit action to close with");
		}
		break;

	case LRG_HEADER_BAR_BUTTON_MINIMIZE:
		if (self->funcs.minimize != NULL)
			self->funcs.minimize (self->funcs_data);
		break;

	case LRG_HEADER_BAR_BUTTON_MAXIMIZE:
		toggle_maximize (self);
		break;

	case LRG_HEADER_BAR_BUTTON_NONE:
	default:
		break;
	}
}

void
lrg_header_bar_set_window_funcs (LrgHeaderBar                  *self,
                                 const LrgHeaderBarWindowFuncs *funcs,
                                 gpointer                       user_data,
                                 GDestroyNotify                 destroy)
{
	g_return_if_fail (LRG_IS_HEADER_BAR (self));

	clear_funcs (self);
	self->funcs = funcs != NULL ? *funcs : default_funcs;
	self->funcs_data = user_data;
	self->funcs_destroy = destroy;
	self->maximized = win_is_maximized (self);
}

gboolean
lrg_header_bar_get_can_move_window (LrgHeaderBar *self)
{
	g_return_val_if_fail (LRG_IS_HEADER_BAR (self), FALSE);

	return win_can_move (self);
}
