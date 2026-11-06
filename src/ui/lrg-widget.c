/* lrg-widget.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for UI widgets: geometry, layout hints, state,
 * springs, appear animations, style resolution and the paint/draw
 * bridge.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-widget.h"
#include "lrg-widget-private.h"
#include "lrg-container.h"
#include "../paint/lrg-paint-renderer.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* States driven by springs rather than matched discretely */
#define SPRING_STATES (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED)

/* Gap between a widget and its focus ring */
#define FOCUS_RING_GAP (2.0f)

/* Distance (canvas units) fly-in appear animations travel */
#define APPEAR_FLY_DISTANCE (24.0f)

/* ==========================================================================
 * LrgInsets
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgInsets, lrg_insets, lrg_insets_copy, lrg_insets_free)

LrgInsets *
lrg_insets_new (gfloat left,
                gfloat top,
                gfloat right,
                gfloat bottom)
{
	LrgInsets *self;

	self = g_new (LrgInsets, 1);
	self->left = left;
	self->top = top;
	self->right = right;
	self->bottom = bottom;
	return self;
}

LrgInsets *
lrg_insets_copy (const LrgInsets *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return (LrgInsets *) g_memdup2 (self, sizeof (LrgInsets));
}

void
lrg_insets_free (LrgInsets *self)
{
	g_free (self);
}

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
	/* Geometry */
	gfloat          x;
	gfloat          y;
	gfloat          width;
	gfloat          height;
	gboolean        visible;
	gboolean        enabled;
	LrgContainer   *parent;      /* weak: the parent owns us */

	/* Layout hints */
	gchar          *name;
	LrgInsets       margin;
	LrgAlign        halign;
	LrgAlign        valign;
	gfloat          fill_weight;
	gfloat          min_width;
	gfloat          min_height;
	gfloat          max_width;
	gfloat          max_height;
	gboolean        needs_layout;

	/* Appearance */
	gfloat          opacity;
	LrgTheme       *theme;       /* owned, NULL = inherit */
	gint            motion;      /* LrgMotion or -1 */
	LrgAppear       appear;
	gfloat          appear_delay;
	gfloat          appear_wait; /* seconds left before starting */
	gboolean        appear_pending;
	LrgSpring       appear_spring;

	/* Style and interaction */
	GArray         *classes;     /* GQuark */
	LrgWidgetState  state;
	gboolean        focusable;
	gboolean        pointer_transparent;
	GArray         *part_looks;  /* PartOverride, NULL until used */
	LrgSpring       hover;
	LrgSpring       press;
} LrgWidgetPrivate;

/* A per-instance look override (lrg_widget_set_part_look) */
typedef struct
{
	LrgUIPart       part;
	LrgWidgetState  state;
	LrgPartLook     look;
} PartOverride;

G_DEFINE_TYPE_WITH_PRIVATE (LrgWidget, lrg_widget, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_X,
	PROP_Y,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_VISIBLE,
	PROP_ENABLED,
	PROP_PARENT,
	PROP_NAME,
	PROP_MARGIN,
	PROP_HALIGN,
	PROP_VALIGN,
	PROP_FILL_WEIGHT,
	PROP_OPACITY,
	PROP_APPEAR,
	PROP_FOCUSABLE,
	PROP_STATE_FLAGS,
	PROP_THEME,
	N_PROPS
};

enum
{
	SIGNAL_STATE_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* Separate renderer for the legacy draw() bridge so a nested render
 * from inside a paint callback never touches the main renderer's
 * instance buffer mid-frame */
static LrgPaintRenderer *bridge_renderer = NULL;

/* ==========================================================================
 * Internal Functions
 * ========================================================================== */

void
_lrg_widget_set_parent (LrgWidget    *self,
                        LrgContainer *parent)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (parent == NULL || LRG_IS_CONTAINER (parent));

	priv = lrg_widget_get_instance_private (self);

	if (priv->parent != parent)
	{
		priv->parent = parent;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARENT]);
	}
}

void
_lrg_widget_clear_needs_layout (LrgWidget *self)
{
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);

	priv->needs_layout = FALSE;
}

/* set_float_prop: assign + notify when changed */
static gboolean
set_float (LrgWidget *self,
           gfloat    *field,
           gfloat     value,
           guint      prop)
{
	if (*field == value)
		return FALSE;
	*field = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
	return TRUE;
}

void
_lrg_widget_set_rect_quiet (LrgWidget *self,
                            gfloat     x,
                            gfloat     y,
                            gfloat     width,
                            gfloat     height)
{
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);

	g_object_freeze_notify (G_OBJECT (self));
	set_float (self, &priv->x, x, PROP_X);
	set_float (self, &priv->y, y, PROP_Y);
	set_float (self, &priv->width, MAX (width, 0.0f), PROP_WIDTH);
	set_float (self, &priv->height, MAX (height, 0.0f), PROP_HEIGHT);
	g_object_thaw_notify (G_OBJECT (self));
}

/* ==========================================================================
 * Default Virtual Method Implementations
 * ========================================================================== */

/*
 * _lrg_widget_real_draw:
 *
 * Legacy entry point for widgets that only implement paint(): record
 * into a scratch list and render it right away, so
 * lrg_widget_draw() works on every widget.
 */
void
_lrg_widget_real_draw (LrgWidget *self)
{
	LrgWidgetClass          *klass = LRG_WIDGET_GET_CLASS (self);
	g_autoptr(LrgPaintList)  list = NULL;

	if (klass->paint == _lrg_widget_real_paint)
		return;

	if (bridge_renderer == NULL)
		bridge_renderer = lrg_paint_renderer_new ();

	list = lrg_paint_list_new ();
	klass->paint (self, list);
	lrg_paint_renderer_render (bridge_renderer, list);
}

void
_lrg_widget_real_paint (LrgWidget    *self,
                        LrgPaintList *list)
{
	/* A plain widget has no visual of its own */
}

static void
lrg_widget_real_measure (LrgWidget *self,
                         gfloat    *preferred_width,
                         gfloat    *preferred_height)
{
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);

	/* Default: the current size */
	if (preferred_width != NULL)
		*preferred_width = priv->width;
	if (preferred_height != NULL)
		*preferred_height = priv->height;
}

static gboolean
lrg_widget_real_handle_event (LrgWidget        *self,
                              const LrgUIEvent *event)
{
	return FALSE;
}

static void
lrg_widget_real_style_defaults (LrgWidget      *self,
                                LrgUIPart       part,
                                LrgWidgetState  state,
                                LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (self);

	/* Every widget gets a focus ring; parts are otherwise invisible */
	if (part == LRG_UI_PART_FOCUS)
	{
		gfloat ring = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FOCUS_RING_WIDTH);

		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS), ring);
		lrg_part_look_set_radius (look,
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) +
		                          FOCUS_RING_GAP + ring);
	}
}

static void
lrg_widget_real_arrange (LrgWidget *self)
{
	/* Leaf widgets have nothing to place */
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_widget_finalize (GObject *object)
{
	LrgWidget        *self = LRG_WIDGET (object);
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);

	g_clear_pointer (&priv->name, g_free);
	g_clear_pointer (&priv->classes, g_array_unref);
	g_clear_pointer (&priv->part_looks, g_array_unref);
	g_clear_object (&priv->theme);

	G_OBJECT_CLASS (lrg_widget_parent_class)->finalize (object);
}

static void
lrg_widget_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
	LrgWidget        *self = LRG_WIDGET (object);
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_X:
		g_value_set_float (value, priv->x);
		break;
	case PROP_Y:
		g_value_set_float (value, priv->y);
		break;
	case PROP_WIDTH:
		g_value_set_float (value, priv->width);
		break;
	case PROP_HEIGHT:
		g_value_set_float (value, priv->height);
		break;
	case PROP_VISIBLE:
		g_value_set_boolean (value, priv->visible);
		break;
	case PROP_ENABLED:
		g_value_set_boolean (value, priv->enabled);
		break;
	case PROP_PARENT:
		g_value_set_object (value, priv->parent);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_MARGIN:
		g_value_set_boxed (value, &priv->margin);
		break;
	case PROP_HALIGN:
		g_value_set_enum (value, priv->halign);
		break;
	case PROP_VALIGN:
		g_value_set_enum (value, priv->valign);
		break;
	case PROP_FILL_WEIGHT:
		g_value_set_float (value, priv->fill_weight);
		break;
	case PROP_OPACITY:
		g_value_set_float (value, priv->opacity);
		break;
	case PROP_APPEAR:
		g_value_set_enum (value, priv->appear);
		break;
	case PROP_FOCUSABLE:
		g_value_set_boolean (value, priv->focusable);
		break;
	case PROP_STATE_FLAGS:
		g_value_set_flags (value, priv->state);
		break;
	case PROP_THEME:
		g_value_set_object (value, priv->theme);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_widget_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	LrgWidget        *self = LRG_WIDGET (object);
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_X:
		lrg_widget_set_x (self, g_value_get_float (value));
		break;
	case PROP_Y:
		lrg_widget_set_y (self, g_value_get_float (value));
		break;
	case PROP_WIDTH:
		lrg_widget_set_width (self, g_value_get_float (value));
		break;
	case PROP_HEIGHT:
		lrg_widget_set_height (self, g_value_get_float (value));
		break;
	case PROP_VISIBLE:
		lrg_widget_set_visible (self, g_value_get_boolean (value));
		break;
	case PROP_ENABLED:
		lrg_widget_set_enabled (self, g_value_get_boolean (value));
		break;
	case PROP_NAME:
		lrg_widget_set_name (self, g_value_get_string (value));
		break;
	case PROP_MARGIN:
		if (g_value_get_boxed (value) != NULL)
			lrg_widget_set_margin (self, g_value_get_boxed (value));
		break;
	case PROP_HALIGN:
		lrg_widget_set_halign (self, g_value_get_enum (value));
		break;
	case PROP_VALIGN:
		lrg_widget_set_valign (self, g_value_get_enum (value));
		break;
	case PROP_FILL_WEIGHT:
		lrg_widget_set_fill_weight (self, g_value_get_float (value));
		break;
	case PROP_OPACITY:
		lrg_widget_set_opacity (self, g_value_get_float (value));
		break;
	case PROP_APPEAR:
		lrg_widget_set_appear (self, g_value_get_enum (value), priv->appear_delay);
		break;
	case PROP_FOCUSABLE:
		lrg_widget_set_focusable (self, g_value_get_boolean (value));
		break;
	case PROP_THEME:
		lrg_widget_set_theme (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_widget_class_init (LrgWidgetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->finalize = lrg_widget_finalize;
	object_class->get_property = lrg_widget_get_property;
	object_class->set_property = lrg_widget_set_property;

	klass->draw = _lrg_widget_real_draw;
	klass->measure = lrg_widget_real_measure;
	klass->handle_event = lrg_widget_real_handle_event;
	klass->paint = _lrg_widget_real_paint;
	klass->style_defaults = lrg_widget_real_style_defaults;
	klass->arrange = lrg_widget_real_arrange;

	/**
	 * LrgWidget:x:
	 *
	 * X position relative to the parent.
	 */
	properties[PROP_X] = g_param_spec_float ("x", NULL, NULL, -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);
	/**
	 * LrgWidget:y:
	 *
	 * Y position relative to the parent.
	 */
	properties[PROP_Y] = g_param_spec_float ("y", NULL, NULL, -G_MAXFLOAT, G_MAXFLOAT, 0.0f, rw);
	/**
	 * LrgWidget:width:
	 *
	 * Width.
	 */
	properties[PROP_WIDTH] = g_param_spec_float ("width", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f, rw);
	/**
	 * LrgWidget:height:
	 *
	 * Height.
	 */
	properties[PROP_HEIGHT] = g_param_spec_float ("height", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f, rw);
	/**
	 * LrgWidget:visible:
	 *
	 * Whether the widget is painted and receives input.
	 */
	properties[PROP_VISIBLE] = g_param_spec_boolean ("visible", NULL, NULL, TRUE, rw);
	/**
	 * LrgWidget:enabled:
	 *
	 * Whether the widget responds to input.
	 */
	properties[PROP_ENABLED] = g_param_spec_boolean ("enabled", NULL, NULL, TRUE, rw);
	/**
	 * LrgWidget:parent:
	 *
	 * The parent container.
	 */
	properties[PROP_PARENT] =
		g_param_spec_object ("parent", NULL, NULL, LRG_TYPE_CONTAINER,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
	/**
	 * LrgWidget:name:
	 *
	 * Name used by lrg_widget_find().
	 */
	properties[PROP_NAME] = g_param_spec_string ("name", NULL, NULL, NULL, rw);
	/**
	 * LrgWidget:margin:
	 *
	 * Space the parent leaves around the widget.
	 */
	properties[PROP_MARGIN] = g_param_spec_boxed ("margin", NULL, NULL, LRG_TYPE_INSETS, rw);
	/**
	 * LrgWidget:halign:
	 *
	 * Horizontal alignment inside the parent's slot.
	 */
	properties[PROP_HALIGN] =
		g_param_spec_enum ("halign", NULL, NULL, LRG_TYPE_ALIGN, LRG_ALIGN_FILL, rw);
	/**
	 * LrgWidget:valign:
	 *
	 * Vertical alignment inside the parent's slot.
	 */
	properties[PROP_VALIGN] =
		g_param_spec_enum ("valign", NULL, NULL, LRG_TYPE_ALIGN, LRG_ALIGN_FILL, rw);
	/**
	 * LrgWidget:fill-weight:
	 *
	 * Share of a box's spare space along its axis.
	 */
	properties[PROP_FILL_WEIGHT] =
		g_param_spec_float ("fill-weight", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f, rw);
	/**
	 * LrgWidget:opacity:
	 *
	 * Opacity of the widget and its children.
	 */
	properties[PROP_OPACITY] = g_param_spec_float ("opacity", NULL, NULL, 0.0f, 1.0f, 1.0f, rw);
	/**
	 * LrgWidget:appear:
	 *
	 * Entrance animation.
	 */
	properties[PROP_APPEAR] =
		g_param_spec_enum ("appear", NULL, NULL, LRG_TYPE_APPEAR, LRG_APPEAR_NONE, rw);
	/**
	 * LrgWidget:focusable:
	 *
	 * Whether the widget can take focus.
	 */
	properties[PROP_FOCUSABLE] = g_param_spec_boolean ("focusable", NULL, NULL, FALSE, rw);
	/**
	 * LrgWidget:state-flags:
	 *
	 * Current interaction state.
	 */
	properties[PROP_STATE_FLAGS] =
		g_param_spec_flags ("state-flags", NULL, NULL, LRG_TYPE_WIDGET_STATE,
		                    LRG_WIDGET_STATE_NONE,
		                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
	/**
	 * LrgWidget:theme:
	 *
	 * Theme for this widget and its subtree, %NULL to inherit.
	 */
	properties[PROP_THEME] = g_param_spec_object ("theme", NULL, NULL, LRG_TYPE_THEME, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgWidget::state-changed:
	 * @widget: the widget
	 * @previous: the flags before the change
	 *
	 * Emitted when the state flags change.
	 */
	signals[SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, LRG_TYPE_WIDGET_STATE);
}

static void
lrg_widget_init (LrgWidget *self)
{
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);

	priv->visible = TRUE;
	priv->enabled = TRUE;
	priv->halign = LRG_ALIGN_FILL;
	priv->valign = LRG_ALIGN_FILL;
	priv->opacity = 1.0f;
	priv->motion = -1;
	priv->appear = LRG_APPEAR_NONE;
	priv->classes = g_array_new (FALSE, FALSE, sizeof (GQuark));
	lrg_spring_init (&priv->hover, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&priv->press, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&priv->appear_spring, 1.0f, LRG_MOTION_SMOOTH);
}

/* ==========================================================================
 * Position and Size
 * ========================================================================== */

#define GETTER(fn, field, def) \
	gfloat \
	lrg_widget_get_##fn (LrgWidget *self) \
	{ \
		LrgWidgetPrivate *priv; \
		g_return_val_if_fail (LRG_IS_WIDGET (self), def); \
		priv = lrg_widget_get_instance_private (self); \
		return priv->field; \
	}

GETTER (x, x, 0.0f)
GETTER (y, y, 0.0f)
GETTER (width, width, 0.0f)
GETTER (height, height, 0.0f)
GETTER (fill_weight, fill_weight, 0.0f)
GETTER (opacity, opacity, 1.0f)

#undef GETTER

void
lrg_widget_set_x (LrgWidget *self,
                  gfloat     x)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	set_float (self, &priv->x, x, PROP_X);
}

void
lrg_widget_set_y (LrgWidget *self,
                  gfloat     y)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	set_float (self, &priv->y, y, PROP_Y);
}

/* size_changed: a container that changes size must re-place children */
static void
size_changed (LrgWidget *self)
{
	if (LRG_IS_CONTAINER (self))
		lrg_widget_queue_layout (self);
}

void
lrg_widget_set_width (LrgWidget *self,
                      gfloat     width)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	if (set_float (self, &priv->width, MAX (width, 0.0f), PROP_WIDTH))
		size_changed (self);
}

void
lrg_widget_set_height (LrgWidget *self,
                       gfloat     height)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	if (set_float (self, &priv->height, MAX (height, 0.0f), PROP_HEIGHT))
		size_changed (self);
}

void
lrg_widget_set_position (LrgWidget *self,
                         gfloat     x,
                         gfloat     y)
{
	g_return_if_fail (LRG_IS_WIDGET (self));

	g_object_freeze_notify (G_OBJECT (self));
	lrg_widget_set_x (self, x);
	lrg_widget_set_y (self, y);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_widget_set_size (LrgWidget *self,
                     gfloat     width,
                     gfloat     height)
{
	g_return_if_fail (LRG_IS_WIDGET (self));

	g_object_freeze_notify (G_OBJECT (self));
	lrg_widget_set_width (self, width);
	lrg_widget_set_height (self, height);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_widget_allocate (LrgWidget *self,
                     gfloat     x,
                     gfloat     y,
                     gfloat     width,
                     gfloat     height)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	_lrg_widget_set_rect_quiet (self, x, y, width, height);
	LRG_WIDGET_GET_CLASS (self)->arrange (self);
	priv->needs_layout = FALSE;
}

/* ==========================================================================
 * World Coordinates
 * ========================================================================== */

gfloat
lrg_widget_get_world_x (LrgWidget *self)
{
	LrgWidgetPrivate *priv;
	gfloat            x;

	g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);

	priv = lrg_widget_get_instance_private (self);
	x = priv->x;
	if (priv->parent != NULL)
		x += lrg_widget_get_world_x (LRG_WIDGET (priv->parent));
	return x;
}

gfloat
lrg_widget_get_world_y (LrgWidget *self)
{
	LrgWidgetPrivate *priv;
	gfloat            y;

	g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);

	priv = lrg_widget_get_instance_private (self);
	y = priv->y;
	if (priv->parent != NULL)
		y += lrg_widget_get_world_y (LRG_WIDGET (priv->parent));
	return y;
}

/* ==========================================================================
 * Visibility and sensitivity
 * ========================================================================== */

gboolean
lrg_widget_get_visible (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
	priv = lrg_widget_get_instance_private (self);
	return priv->visible;
}

void
lrg_widget_set_visible (LrgWidget *self,
                        gboolean   visible)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	visible = !!visible;
	if (priv->visible == visible)
		return;

	priv->visible = visible;
	if (visible && priv->appear != LRG_APPEAR_NONE)
		priv->appear_pending = TRUE;

	/* A hidden widget is no longer hovered or pressed */
	if (!visible)
		lrg_widget_unset_state_flags (self, SPRING_STATES);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
	if (priv->parent != NULL)
		lrg_widget_queue_layout (LRG_WIDGET (priv->parent));
}

gboolean
lrg_widget_get_enabled (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
	priv = lrg_widget_get_instance_private (self);
	return priv->enabled;
}

void
lrg_widget_set_enabled (LrgWidget *self,
                        gboolean   enabled)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	enabled = !!enabled;
	if (priv->enabled == enabled)
		return;

	priv->enabled = enabled;
	if (enabled)
		lrg_widget_unset_state_flags (self, LRG_WIDGET_STATE_DISABLED);
	else
		lrg_widget_set_state_flags (self, LRG_WIDGET_STATE_DISABLED);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

/* ==========================================================================
 * Hierarchy
 * ========================================================================== */

LrgContainer *
lrg_widget_get_parent (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), NULL);
	priv = lrg_widget_get_instance_private (self);
	return priv->parent;
}

LrgWidget *
lrg_widget_get_root (LrgWidget *self)
{
	LrgWidget *w;

	g_return_val_if_fail (LRG_IS_WIDGET (self), NULL);

	for (w = self; lrg_widget_get_parent (w) != NULL; w = LRG_WIDGET (lrg_widget_get_parent (w)))
		;
	return w;
}

gboolean
lrg_widget_is_ancestor (LrgWidget *self,
                        LrgWidget *ancestor)
{
	LrgContainer *p;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);

	if (ancestor == NULL)
		return FALSE;
	for (p = lrg_widget_get_parent (self); p != NULL; p = lrg_widget_get_parent (LRG_WIDGET (p)))
		if (LRG_WIDGET (p) == ancestor)
			return TRUE;
	return FALSE;
}

gboolean
lrg_widget_contains_point (LrgWidget *self,
                           gfloat     x,
                           gfloat     y)
{
	LrgWidgetPrivate *priv;
	gfloat            wx;
	gfloat            wy;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
	priv = lrg_widget_get_instance_private (self);

	wx = lrg_widget_get_world_x (self);
	wy = lrg_widget_get_world_y (self);

	return x >= wx && x < wx + priv->width && y >= wy && y < wy + priv->height;
}

/* ==========================================================================
 * Layout hints
 * ========================================================================== */

const gchar *
lrg_widget_get_name (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), NULL);
	priv = lrg_widget_get_instance_private (self);
	return priv->name;
}

void
lrg_widget_set_name (LrgWidget   *self,
                     const gchar *name)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	if (g_strcmp0 (priv->name, name) == 0)
		return;
	g_free (priv->name);
	priv->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

LrgWidget *
lrg_widget_find (LrgWidget   *self,
                 const gchar *name)
{
	GList *l;

	g_return_val_if_fail (LRG_IS_WIDGET (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	if (g_strcmp0 (lrg_widget_get_name (self), name) == 0)
		return self;

	if (!LRG_IS_CONTAINER (self))
		return NULL;

	for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
	{
		LrgWidget *found = lrg_widget_find (LRG_WIDGET (l->data), name);

		if (found != NULL)
			return found;
	}
	return NULL;
}

void
lrg_widget_get_margin (LrgWidget *self,
                       LrgInsets *margin)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (margin != NULL);
	priv = lrg_widget_get_instance_private (self);
	*margin = priv->margin;
}

void
lrg_widget_set_margin (LrgWidget       *self,
                       const LrgInsets *margin)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (margin != NULL);
	priv = lrg_widget_get_instance_private (self);

	if (memcmp (&priv->margin, margin, sizeof (LrgInsets)) == 0)
		return;
	priv->margin = *margin;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MARGIN]);
	lrg_widget_queue_layout (self);
}

void
lrg_widget_set_margin_all (LrgWidget *self,
                           gfloat     margin)
{
	LrgInsets m = { margin, margin, margin, margin };

	lrg_widget_set_margin (self, &m);
}

LrgAlign
lrg_widget_get_halign (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), LRG_ALIGN_FILL);
	priv = lrg_widget_get_instance_private (self);
	return priv->halign;
}

void
lrg_widget_set_halign (LrgWidget *self,
                       LrgAlign   align)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	if (priv->halign == align)
		return;
	priv->halign = align;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HALIGN]);
	lrg_widget_queue_layout (self);
}

LrgAlign
lrg_widget_get_valign (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), LRG_ALIGN_FILL);
	priv = lrg_widget_get_instance_private (self);
	return priv->valign;
}

void
lrg_widget_set_valign (LrgWidget *self,
                       LrgAlign   align)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	if (priv->valign == align)
		return;
	priv->valign = align;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALIGN]);
	lrg_widget_queue_layout (self);
}

void
lrg_widget_set_fill_weight (LrgWidget *self,
                            gfloat     weight)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	if (set_float (self, &priv->fill_weight, MAX (weight, 0.0f), PROP_FILL_WEIGHT))
		lrg_widget_queue_layout (self);
}

void
lrg_widget_set_min_size (LrgWidget *self,
                         gfloat     width,
                         gfloat     height)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	priv->min_width = MAX (width, 0.0f);
	priv->min_height = MAX (height, 0.0f);
	lrg_widget_queue_layout (self);
}

void
lrg_widget_set_max_size (LrgWidget *self,
                         gfloat     width,
                         gfloat     height)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	priv->max_width = MAX (width, 0.0f);
	priv->max_height = MAX (height, 0.0f);
	lrg_widget_queue_layout (self);
}

void
lrg_widget_get_min_size (LrgWidget *self,
                         gfloat    *width,
                         gfloat    *height)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	if (width != NULL)
		*width = priv->min_width;
	if (height != NULL)
		*height = priv->min_height;
}

void
lrg_widget_get_max_size (LrgWidget *self,
                         gfloat    *width,
                         gfloat    *height)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	if (width != NULL)
		*width = priv->max_width;
	if (height != NULL)
		*height = priv->max_height;
}

/* clamp_size: apply a min and an optional (0 = none) max */
static gfloat
clamp_size (gfloat v,
            gfloat min,
            gfloat max)
{
	if (max > 0.0f && v > max)
		v = max;
	if (v < min)
		v = min;
	return v;
}

void
lrg_widget_get_preferred_size (LrgWidget *self,
                               gfloat    *width,
                               gfloat    *height)
{
	LrgWidgetPrivate *priv;
	gfloat            w = 0.0f;
	gfloat            h = 0.0f;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	lrg_widget_measure (self, &w, &h);
	if (width != NULL)
		*width = clamp_size (w, priv->min_width, priv->max_width);
	if (height != NULL)
		*height = clamp_size (h, priv->min_height, priv->max_height);
}

/* place_axis: size and offset of a widget along one axis of its slot */
static void
place_axis (LrgAlign  align,
            gfloat    slot_pos,
            gfloat    slot_size,
            gfloat    preferred,
            gfloat    min,
            gfloat    max,
            gfloat   *pos,
            gfloat   *size)
{
	gfloat s;

	s = align == LRG_ALIGN_FILL ? slot_size : MIN (preferred, slot_size);
	s = clamp_size (s, min, max);

	switch (align)
	{
	case LRG_ALIGN_CENTER:
		*pos = slot_pos + (slot_size - s) * 0.5f;
		break;
	case LRG_ALIGN_END:
		*pos = slot_pos + slot_size - s;
		break;
	case LRG_ALIGN_START:
	case LRG_ALIGN_FILL:
	default:
		*pos = slot_pos;
		break;
	}
	*size = s;
}

void
lrg_widget_place_in_slot (LrgWidget *self,
                          gfloat     x,
                          gfloat     y,
                          gfloat     width,
                          gfloat     height)
{
	LrgWidgetPrivate *priv;
	gfloat            sx;
	gfloat            sy;
	gfloat            sw;
	gfloat            sh;
	gfloat            pw = 0.0f;
	gfloat            ph = 0.0f;
	gfloat            cx;
	gfloat            cy;
	gfloat            cw;
	gfloat            ch;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	/* Slot minus margin */
	sx = x + priv->margin.left;
	sy = y + priv->margin.top;
	sw = MAX (0.0f, width - priv->margin.left - priv->margin.right);
	sh = MAX (0.0f, height - priv->margin.top - priv->margin.bottom);

	if (priv->halign != LRG_ALIGN_FILL || priv->valign != LRG_ALIGN_FILL)
		lrg_widget_measure (self, &pw, &ph);

	place_axis (priv->halign, sx, sw, pw, priv->min_width, priv->max_width, &cx, &cw);
	place_axis (priv->valign, sy, sh, ph, priv->min_height, priv->max_height, &cy, &ch);

	lrg_widget_allocate (self, roundf (cx), roundf (cy), roundf (cw), roundf (ch));
}

void
_lrg_widget_outer_size (LrgWidget *self,
                        gfloat    *width,
                        gfloat    *height)
{
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);
	gfloat            w;
	gfloat            h;

	lrg_widget_get_preferred_size (self, &w, &h);
	if (width != NULL)
		*width = w + priv->margin.left + priv->margin.right;
	if (height != NULL)
		*height = h + priv->margin.top + priv->margin.bottom;
}

void
lrg_widget_queue_layout (LrgWidget *self)
{
	LrgWidget *w;

	g_return_if_fail (LRG_IS_WIDGET (self));

	/* Mark up the tree; an already-marked ancestor means everything
	 * above it is marked too */
	for (w = self; w != NULL; w = (LrgWidget *) lrg_widget_get_parent (w))
	{
		LrgWidgetPrivate *priv = lrg_widget_get_instance_private (w);

		if (priv->needs_layout)
			break;
		priv->needs_layout = TRUE;
	}
}

gboolean
lrg_widget_get_needs_layout (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
	priv = lrg_widget_get_instance_private (self);
	return priv->needs_layout;
}

void
lrg_widget_layout (LrgWidget *self)
{
	LrgWidgetPrivate *priv;
	GList            *l;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	/* Clean subtree: nothing below changed */
	if (!priv->needs_layout)
		return;

	LRG_WIDGET_GET_CLASS (self)->arrange (self);
	priv->needs_layout = FALSE;

	/* Children the arrange did not re-allocate (free positioning,
	 * legacy layouts) may still be marked */
	if (LRG_IS_CONTAINER (self))
		for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
			lrg_widget_layout (LRG_WIDGET (l->data));
}

/* ==========================================================================
 * Opacity, appearance, theme
 * ========================================================================== */

void
lrg_widget_set_opacity (LrgWidget *self,
                        gfloat     opacity)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	set_float (self, &priv->opacity, CLAMP (opacity, 0.0f, 1.0f), PROP_OPACITY);
}

LrgAppear
lrg_widget_get_appear (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), LRG_APPEAR_NONE);
	priv = lrg_widget_get_instance_private (self);
	return priv->appear;
}

void
lrg_widget_set_appear (LrgWidget *self,
                       LrgAppear  appear,
                       gfloat     delay)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	priv->appear_delay = MAX (delay, 0.0f);
	if (priv->appear == appear)
		return;

	priv->appear = appear;
	priv->appear_pending = appear != LRG_APPEAR_NONE;
	if (appear == LRG_APPEAR_NONE)
		lrg_spring_snap (&priv->appear_spring, 1.0f);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_APPEAR]);
}

void
lrg_widget_play_appear (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	if (priv->appear != LRG_APPEAR_NONE)
		priv->appear_pending = TRUE;
}

gfloat
lrg_widget_get_appear_progress (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), 1.0f);
	priv = lrg_widget_get_instance_private (self);

	if (priv->appear == LRG_APPEAR_NONE)
		return 1.0f;
	if (priv->appear_pending)
		return 0.0f;
	return priv->appear_spring.value;
}

LrgTheme *
lrg_widget_get_theme (LrgWidget *self)
{
	LrgWidget *w;

	g_return_val_if_fail (LRG_IS_WIDGET (self), lrg_theme_get_default ());

	for (w = self; w != NULL; w = (LrgWidget *) lrg_widget_get_parent (w))
	{
		LrgWidgetPrivate *priv = lrg_widget_get_instance_private (w);

		if (priv->theme != NULL)
			return priv->theme;
	}
	return lrg_theme_get_default ();
}

void
lrg_widget_set_theme (LrgWidget *self,
                      LrgTheme  *theme)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (theme == NULL || LRG_IS_THEME (theme));
	priv = lrg_widget_get_instance_private (self);

	if (g_set_object (&priv->theme, theme))
	{
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THEME]);
		lrg_widget_queue_layout (self);
	}
}

LrgMotion
lrg_widget_get_motion (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), LRG_MOTION_SMOOTH);
	priv = lrg_widget_get_instance_private (self);

	if (priv->motion >= 0)
		return (LrgMotion) priv->motion;
	return lrg_theme_get_motion (lrg_widget_get_theme (self));
}

void
lrg_widget_set_motion (LrgWidget *self,
                       gint       motion)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	priv->motion = motion < 0 ? -1 : motion;
}

/* ==========================================================================
 * Style classes and state
 * ========================================================================== */

void
lrg_widget_add_class (LrgWidget   *self,
                      const gchar *style_class)
{
	LrgWidgetPrivate *priv;
	GQuark            q;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (style_class != NULL);
	priv = lrg_widget_get_instance_private (self);

	if (lrg_widget_has_class (self, style_class))
		return;
	q = g_quark_from_string (style_class);
	g_array_append_val (priv->classes, q);

	/* A class can change sizes (e.g. a "title" label) */
	lrg_widget_queue_layout (self);
}

void
lrg_widget_remove_class (LrgWidget   *self,
                         const gchar *style_class)
{
	LrgWidgetPrivate *priv;
	GQuark            q;
	guint             i;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (style_class != NULL);
	priv = lrg_widget_get_instance_private (self);

	q = g_quark_try_string (style_class);
	for (i = 0; q != 0 && i < priv->classes->len; i++)
	{
		if (g_array_index (priv->classes, GQuark, i) == q)
		{
			g_array_remove_index (priv->classes, i);
			lrg_widget_queue_layout (self);
			return;
		}
	}
}

gboolean
lrg_widget_has_class (LrgWidget   *self,
                      const gchar *style_class)
{
	LrgWidgetPrivate *priv;
	GQuark            q;
	guint             i;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
	g_return_val_if_fail (style_class != NULL, FALSE);
	priv = lrg_widget_get_instance_private (self);

	q = g_quark_try_string (style_class);
	for (i = 0; q != 0 && i < priv->classes->len; i++)
		if (g_array_index (priv->classes, GQuark, i) == q)
			return TRUE;
	return FALSE;
}

const GQuark *
lrg_widget_get_classes (LrgWidget *self,
                        guint     *n_classes)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), NULL);
	priv = lrg_widget_get_instance_private (self);

	if (n_classes != NULL)
		*n_classes = priv->classes->len;
	return (const GQuark *) (gpointer) priv->classes->data;
}

LrgWidgetState
lrg_widget_get_state_flags (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), LRG_WIDGET_STATE_NONE);
	priv = lrg_widget_get_instance_private (self);
	return priv->state;
}

/* apply_state: store the new flags, retarget springs, notify */
static void
apply_state (LrgWidget      *self,
             LrgWidgetState  state)
{
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);
	LrgWidgetState    previous = priv->state;

	if (previous == state)
		return;

	priv->state = state;
	lrg_spring_set_target (&priv->hover, (state & LRG_WIDGET_STATE_HOVERED) ? 1.0f : 0.0f);
	lrg_spring_set_target (&priv->press, (state & LRG_WIDGET_STATE_PRESSED) ? 1.0f : 0.0f);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE_FLAGS]);
	g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, previous);
}

void
lrg_widget_set_state_flags (LrgWidget      *self,
                            LrgWidgetState  flags)
{
	g_return_if_fail (LRG_IS_WIDGET (self));

	apply_state (self, lrg_widget_get_state_flags (self) | flags);
}

void
lrg_widget_unset_state_flags (LrgWidget      *self,
                              LrgWidgetState  flags)
{
	g_return_if_fail (LRG_IS_WIDGET (self));

	apply_state (self, lrg_widget_get_state_flags (self) & ~flags);
}

gboolean
lrg_widget_get_focusable (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
	priv = lrg_widget_get_instance_private (self);
	return priv->focusable;
}

void
lrg_widget_set_focusable (LrgWidget *self,
                          gboolean   focusable)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	focusable = !!focusable;
	if (priv->focusable == focusable)
		return;
	priv->focusable = focusable;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOCUSABLE]);
}

gboolean
lrg_widget_get_pointer_transparent (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
	priv = lrg_widget_get_instance_private (self);
	return priv->pointer_transparent;
}

void
lrg_widget_set_pointer_transparent (LrgWidget *self,
                                    gboolean   transparent)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);
	priv->pointer_transparent = !!transparent;
}

gfloat
lrg_widget_get_hover_progress (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);
	priv = lrg_widget_get_instance_private (self);
	return priv->hover.value;
}

gfloat
lrg_widget_get_press_progress (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), 0.0f);
	priv = lrg_widget_get_instance_private (self);
	return priv->press.value;
}

gboolean
lrg_widget_step (LrgWidget *self,
                 gfloat     delta_time)
{
	LrgWidgetPrivate *priv;
	LrgMotion         motion;
	gboolean          busy = FALSE;
	GList            *l;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
	priv = lrg_widget_get_instance_private (self);

	/* Hover follows the theme motion; press is always snappy so a
	 * click registers instantly */
	motion = lrg_widget_get_motion (self);
	lrg_spring_set_motion (&priv->hover, motion);
	lrg_spring_set_motion (&priv->press, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE
	                                                               : LRG_MOTION_SNAPPY);
	busy |= lrg_spring_step (&priv->hover, delta_time);
	busy |= lrg_spring_step (&priv->press, delta_time);

	/* The widget's own animations */
	if (LRG_WIDGET_GET_CLASS (self)->step != NULL)
		busy |= LRG_WIDGET_GET_CLASS (self)->step (self, delta_time);

	/* Appear: count down the delay, then run the spring */
	if (!priv->appear_pending && priv->appear != LRG_APPEAR_NONE)
	{
		if (priv->appear_wait > 0.0f)
		{
			priv->appear_wait -= delta_time * lrg_motion_get_speed ();
			busy = TRUE;
			if (lrg_motion_get_reduced ())
				priv->appear_wait = 0.0f;
		}
		else
		{
			busy |= lrg_spring_step (&priv->appear_spring, delta_time);
		}
	}

	if (priv->visible && LRG_IS_CONTAINER (self))
		for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
			busy |= lrg_widget_step (LRG_WIDGET (l->data), delta_time);

	return busy;
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

void
lrg_widget_resolve_look_for_state (LrgWidget      *self,
                                   LrgUIPart       part,
                                   LrgWidgetState  state,
                                   LrgPartLook    *look)
{
	LrgWidgetClass   *klass;
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (look != NULL);
	priv = lrg_widget_get_instance_private (self);
	klass = LRG_WIDGET_GET_CLASS (self);

	lrg_part_look_init (look);
	if (klass->style_defaults != NULL)
		klass->style_defaults (self, part, state, look);

	lrg_theme_resolve_look (lrg_widget_get_theme (self), G_OBJECT_TYPE (self),
	                        (const GQuark *) (gpointer) priv->classes->data,
	                        priv->classes->len, part, state, look);

	/* Explicit values beat the theme: the widget's own setters, then
	 * per-instance overrides, least specific state first */
	if (klass->style_overrides != NULL)
		klass->style_overrides (self, part, state, look);

	if (priv->part_looks != NULL)
	{
		guint pass;
		guint i;

		/* specificity = number of required state bits; apply in
		 * increasing order so the most specific wins */
		for (pass = 0; pass <= 32; pass++)
		{
			for (i = 0; i < priv->part_looks->len; i++)
			{
				PartOverride *o = &g_array_index (priv->part_looks, PartOverride, i);
				guint         bits = 0;
				guint         v;

				if (o->part != part || (state & o->state) != o->state)
					continue;
				for (v = (guint) o->state; v != 0; v &= v - 1)
					bits++;
				if (bits == pass)
					lrg_part_look_merge (look, &o->look);
			}
		}
	}
}

void
lrg_widget_set_part_look (LrgWidget         *self,
                          LrgUIPart          part,
                          LrgWidgetState     state,
                          const LrgPartLook *look)
{
	LrgWidgetPrivate *priv;
	guint             i;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	/* Replace or remove an existing override for (part, state) */
	for (i = 0; priv->part_looks != NULL && i < priv->part_looks->len; i++)
	{
		PartOverride *o = &g_array_index (priv->part_looks, PartOverride, i);

		if (o->part == part && o->state == state)
		{
			if (look != NULL)
				o->look = *look;
			else
				g_array_remove_index (priv->part_looks, i);
			lrg_widget_queue_layout (self);
			return;
		}
	}

	if (look == NULL)
		return;

	if (priv->part_looks == NULL)
		priv->part_looks = g_array_new (FALSE, FALSE, sizeof (PartOverride));
	{
		PartOverride o;

		o.part = part;
		o.state = state;
		o.look = *look;
		g_array_append_val (priv->part_looks, o);
	}

	/* A size field may change the preferred size */
	lrg_widget_queue_layout (self);
}

const LrgPartLook *
lrg_widget_get_part_look (LrgWidget      *self,
                          LrgUIPart       part,
                          LrgWidgetState  state)
{
	LrgWidgetPrivate *priv;
	guint             i;

	g_return_val_if_fail (LRG_IS_WIDGET (self), NULL);
	priv = lrg_widget_get_instance_private (self);

	for (i = 0; priv->part_looks != NULL && i < priv->part_looks->len; i++)
	{
		PartOverride *o = &g_array_index (priv->part_looks, PartOverride, i);

		if (o->part == part && o->state == state)
			return &o->look;
	}
	return NULL;
}

void
lrg_widget_clear_part_looks (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	if (priv->part_looks != NULL && priv->part_looks->len > 0)
	{
		g_array_set_size (priv->part_looks, 0);
		lrg_widget_queue_layout (self);
	}
}

void
lrg_widget_resolve_look (LrgWidget   *self,
                         LrgUIPart    part,
                         LrgPartLook *look)
{
	LrgWidgetPrivate *priv;
	LrgWidgetState    base;
	gfloat            h;
	gfloat            p;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (look != NULL);
	priv = lrg_widget_get_instance_private (self);

	base = priv->state & ~SPRING_STATES;
	h = CLAMP (priv->hover.value, 0.0f, 1.0f);
	p = CLAMP (priv->press.value, 0.0f, 1.0f);

	lrg_widget_resolve_look_for_state (self, part, base, look);

	if (h > 0.001f)
	{
		LrgPartLook hovered;

		lrg_widget_resolve_look_for_state (self, part, base | LRG_WIDGET_STATE_HOVERED, &hovered);
		lrg_part_look_lerp (look, &hovered, h, look);
	}
	if (p > 0.001f)
	{
		LrgPartLook pressed;

		lrg_widget_resolve_look_for_state (self, part, base | SPRING_STATES, &pressed);
		lrg_part_look_lerp (look, &pressed, p, look);
	}
}

void
lrg_widget_paint_box (LrgWidget         *self,
                      LrgPaintList      *list,
                      const LrgPartLook *look,
                      gfloat             x,
                      gfloat             y,
                      gfloat             width,
                      gfloat             height)
{
	gboolean faded;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	g_return_if_fail (look != NULL);

	if (width <= 0.0f || height <= 0.0f)
		return;

	faded = look->opacity < 1.0f;
	if (faded)
		lrg_paint_list_push_opacity (list, look->opacity);

	/* Glow: an un-offset, coloured shadow slightly bigger than the box */
	if (look->glow.a > 0 && look->glow_size > 0.0f)
		lrg_paint_list_shadow (list, x, y, width, height, look->radii.top_left,
		                       look->glow_size, look->glow_size * 0.25f, 0.0f, 0.0f, &look->glow);

	if (look->shadow.a > 0 && look->shadow_blur > 0.0f)
		lrg_paint_list_shadow (list, x, y, width, height, look->radii.top_left,
		                       look->shadow_blur, 0.0f, 0.0f, look->shadow_offset_y, &look->shadow);

	lrg_paint_list_rect_ex (list, x, y, width, height, &look->radii,
	                        &look->background,
	                        look->gradient.kind != LRG_PAINT_GRADIENT_NONE ? &look->gradient : NULL,
	                        look->border_width, &look->border);

	if (faded)
		lrg_paint_list_pop_opacity (list);
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/*
 * uses_legacy_draw:
 *
 * TRUE when a class overrides draw() but not paint(): such a widget
 * (from before the paint pipeline) is drawn through a callback.
 */
static gboolean
uses_legacy_draw (LrgWidgetClass *klass)
{
	gboolean default_paint = klass->paint == _lrg_widget_real_paint ||
	                         klass->paint == _lrg_container_default_paint;
	gboolean default_draw = klass->draw == _lrg_widget_real_draw ||
	                        klass->draw == _lrg_container_default_draw;

	return default_paint && !default_draw;
}

static void
legacy_draw_callback (gpointer user_data)
{
	LrgWidget *widget = user_data;

	LRG_WIDGET_GET_CLASS (widget)->draw (widget);
}

/* appear_transform: how far along the entrance animation moves things */
static void
appear_transform (LrgAppear  appear,
                  gfloat     p,
                  gfloat    *scale,
                  gfloat    *dx,
                  gfloat    *dy,
                  gfloat    *alpha)
{
	gfloat rest = 1.0f - p;

	*scale = 1.0f;
	*dx = 0.0f;
	*dy = 0.0f;
	*alpha = CLAMP (p, 0.0f, 1.0f);

	switch (appear)
	{
	case LRG_APPEAR_POP:
		*scale = 0.85f + 0.15f * p;
		*alpha = CLAMP (p * 1.6f, 0.0f, 1.0f);
		break;
	case LRG_APPEAR_ZOOM:
		*scale = 0.5f + 0.5f * p;
		break;
	case LRG_APPEAR_FLY_UP:
		*dy = rest * APPEAR_FLY_DISTANCE;
		break;
	case LRG_APPEAR_FLY_DOWN:
		*dy = -rest * APPEAR_FLY_DISTANCE;
		break;
	case LRG_APPEAR_FLY_LEFT:
		*dx = rest * APPEAR_FLY_DISTANCE;
		break;
	case LRG_APPEAR_FLY_RIGHT:
		*dx = -rest * APPEAR_FLY_DISTANCE;
		break;
	case LRG_APPEAR_DROP:
		*dy = -rest * APPEAR_FLY_DISTANCE * 1.6f;
		*alpha = CLAMP (p * 2.0f, 0.0f, 1.0f);
		break;
	case LRG_APPEAR_FADE:
	case LRG_APPEAR_NONE:
	default:
		break;
	}
}

/* start_appear: begin the entrance animation (delay, then spring) */
static void
start_appear (LrgWidget *self)
{
	LrgWidgetPrivate *priv = lrg_widget_get_instance_private (self);
	LrgMotion         motion;

	priv->appear_pending = FALSE;
	priv->appear_wait = priv->appear_delay;

	motion = priv->appear == LRG_APPEAR_DROP ? LRG_MOTION_BOUNCY : lrg_widget_get_motion (self);
	if (motion == LRG_MOTION_NONE)
		motion = LRG_MOTION_SMOOTH;
	lrg_spring_init (&priv->appear_spring, 0.0f, motion);
	lrg_spring_set_target (&priv->appear_spring, 1.0f);
	if (lrg_motion_get_reduced ())
		lrg_spring_snap (&priv->appear_spring, 1.0f);
}

void
lrg_widget_paint (LrgWidget    *self,
                  LrgPaintList *list)
{
	LrgWidgetPrivate *priv;
	LrgWidgetClass   *klass;
	guint             mark;
	gboolean          faded;
	gboolean          animating;

	g_return_if_fail (LRG_IS_WIDGET (self));
	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	priv = lrg_widget_get_instance_private (self);
	klass = LRG_WIDGET_GET_CLASS (self);

	if (!priv->visible)
		return;

	if (priv->appear_pending)
		start_appear (self);

	faded = priv->opacity < 1.0f;
	if (faded)
		lrg_paint_list_push_opacity (list, priv->opacity);

	mark = lrg_paint_list_begin_group (list);

	/* The widget itself */
	if (uses_legacy_draw (klass))
		lrg_paint_list_callback (list, legacy_draw_callback, g_object_ref (self), g_object_unref);
	else
		klass->paint (self, list);

	/* Focus ring, only after keyboard / gamepad navigation */
	if (priv->state & LRG_WIDGET_STATE_FOCUS_VISIBLE)
	{
		LrgPartLook ring;
		gfloat      g;
		gfloat      x;
		gfloat      y;

		lrg_widget_resolve_look (self, LRG_UI_PART_FOCUS, &ring);
		g = FOCUS_RING_GAP + ring.border_width;
		x = lrg_widget_get_world_x (self);
		y = lrg_widget_get_world_y (self);
		lrg_paint_list_rect_ex (list, x - g, y - g, priv->width + g * 2.0f, priv->height + g * 2.0f,
		                        &ring.radii, NULL, NULL, ring.border_width, &ring.border);
	}

	/* Appear animation: transform everything just recorded */
	animating = priv->appear != LRG_APPEAR_NONE &&
	            (priv->appear_wait > 0.0f || !lrg_spring_is_settled (&priv->appear_spring) ||
	             priv->appear_spring.value < 1.0f);
	if (animating)
	{
		gfloat p = priv->appear_wait > 0.0f ? 0.0f : priv->appear_spring.value;
		gfloat scale;
		gfloat dx;
		gfloat dy;
		gfloat alpha;
		gfloat cx = lrg_widget_get_world_x (self) + priv->width * 0.5f;
		gfloat cy = lrg_widget_get_world_y (self) + priv->height * 0.5f;

		appear_transform (priv->appear, p, &scale, &dx, &dy, &alpha);
		lrg_paint_list_transform_group (list, mark, scale, cx, cy, dx, dy, alpha);
	}

	if (faded)
		lrg_paint_list_pop_opacity (list);
}

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

void
lrg_widget_draw (LrgWidget *self)
{
	LrgWidgetPrivate *priv;

	g_return_if_fail (LRG_IS_WIDGET (self));
	priv = lrg_widget_get_instance_private (self);

	if (!priv->visible)
		return;

	LRG_WIDGET_GET_CLASS (self)->draw (self);
}

void
lrg_widget_measure (LrgWidget *self,
                    gfloat    *preferred_width,
                    gfloat    *preferred_height)
{
	gfloat w = 0.0f;
	gfloat h = 0.0f;

	g_return_if_fail (LRG_IS_WIDGET (self));

	LRG_WIDGET_GET_CLASS (self)->measure (self, &w, &h);

	if (preferred_width != NULL)
		*preferred_width = w;
	if (preferred_height != NULL)
		*preferred_height = h;
}

gboolean
lrg_widget_handle_event (LrgWidget        *self,
                         const LrgUIEvent *event)
{
	LrgWidgetPrivate *priv;

	g_return_val_if_fail (LRG_IS_WIDGET (self), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	priv = lrg_widget_get_instance_private (self);

	if (!priv->visible || !priv->enabled)
		return FALSE;

	return LRG_WIDGET_GET_CLASS (self)->handle_event (self, event);
}
