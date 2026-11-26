/* lrg-touch-button.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One on-screen button: placement in dp from a screen anchor, label and
 * icon, hold or tap, and the action (or explicit input) it drives.
 */

#include "config.h"

#include "lrg-touch-button.h"

G_DEFINE_ENUM_TYPE (LrgTouchAnchor, lrg_touch_anchor,
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_ANCHOR_TOP_LEFT, "top-left"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_ANCHOR_TOP, "top"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_ANCHOR_TOP_RIGHT, "top-right"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_ANCHOR_LEFT, "left"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_ANCHOR_CENTER, "center"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_ANCHOR_RIGHT, "right"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_ANCHOR_BOTTOM_LEFT, "bottom-left"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_ANCHOR_BOTTOM, "bottom"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_ANCHOR_BOTTOM_RIGHT, "bottom-right"))

G_DEFINE_ENUM_TYPE (LrgTouchButtonMode, lrg_touch_button_mode,
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_BUTTON_HOLD, "hold"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_BUTTON_TAP, "tap"))

G_DEFINE_ENUM_TYPE (LrgTouchButtonShape, lrg_touch_button_shape,
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_BUTTON_CIRCLE, "circle"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_BUTTON_ROUNDED, "rounded"))

struct _LrgTouchButton
{
	GObject parent_instance;

	gchar               *id;
	gchar               *action;
	gchar               *label;
	gchar               *icon;
	LrgTouchAnchor       anchor;
	gfloat               offset_x;   /* dp */
	gfloat               offset_y;   /* dp */
	gfloat               width;      /* dp */
	gfloat               height;     /* dp */
	LrgTouchButtonMode   mode;
	LrgTouchButtonShape  shape;
	GrlKey               key;
	GrlGamepadButton     gamepad_button;
};

enum
{
	PROP_0,
	PROP_ID,
	PROP_ACTION,
	PROP_LABEL,
	PROP_ICON,
	PROP_ANCHOR,
	PROP_OFFSET_X,
	PROP_OFFSET_Y,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_MODE,
	PROP_SHAPE,
	PROP_KEY,
	PROP_GAMEPAD_BUTTON,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgTouchButton, lrg_touch_button, G_TYPE_OBJECT)

static void
lrg_touch_button_finalize (GObject *object)
{
	LrgTouchButton *self = LRG_TOUCH_BUTTON (object);

	g_clear_pointer (&self->id, g_free);
	g_clear_pointer (&self->action, g_free);
	g_clear_pointer (&self->label, g_free);
	g_clear_pointer (&self->icon, g_free);

	G_OBJECT_CLASS (lrg_touch_button_parent_class)->finalize (object);
}

static void
lrg_touch_button_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	LrgTouchButton *self = LRG_TOUCH_BUTTON (object);

	switch (prop_id)
	{
	case PROP_ID:
		g_value_set_string (value, self->id);
		break;
	case PROP_ACTION:
		g_value_set_string (value, self->action);
		break;
	case PROP_LABEL:
		g_value_set_string (value, self->label);
		break;
	case PROP_ICON:
		g_value_set_string (value, self->icon);
		break;
	case PROP_ANCHOR:
		g_value_set_enum (value, self->anchor);
		break;
	case PROP_OFFSET_X:
		g_value_set_float (value, self->offset_x);
		break;
	case PROP_OFFSET_Y:
		g_value_set_float (value, self->offset_y);
		break;
	case PROP_WIDTH:
		g_value_set_float (value, self->width);
		break;
	case PROP_HEIGHT:
		g_value_set_float (value, self->height);
		break;
	case PROP_MODE:
		g_value_set_enum (value, self->mode);
		break;
	case PROP_SHAPE:
		g_value_set_enum (value, self->shape);
		break;
	case PROP_KEY:
		g_value_set_enum (value, self->key);
		break;
	case PROP_GAMEPAD_BUTTON:
		g_value_set_enum (value, self->gamepad_button);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_touch_button_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	LrgTouchButton *self = LRG_TOUCH_BUTTON (object);

	switch (prop_id)
	{
	case PROP_ID:
		g_free (self->id);
		self->id = g_value_dup_string (value);
		break;
	case PROP_ACTION:
		g_free (self->action);
		self->action = g_value_dup_string (value);
		break;
	case PROP_LABEL:
		g_free (self->label);
		self->label = g_value_dup_string (value);
		break;
	case PROP_ICON:
		g_free (self->icon);
		self->icon = g_value_dup_string (value);
		break;
	case PROP_ANCHOR:
		self->anchor = g_value_get_enum (value);
		break;
	case PROP_OFFSET_X:
		self->offset_x = g_value_get_float (value);
		break;
	case PROP_OFFSET_Y:
		self->offset_y = g_value_get_float (value);
		break;
	case PROP_WIDTH:
		self->width = g_value_get_float (value);
		break;
	case PROP_HEIGHT:
		self->height = g_value_get_float (value);
		break;
	case PROP_MODE:
		self->mode = g_value_get_enum (value);
		break;
	case PROP_SHAPE:
		self->shape = g_value_get_enum (value);
		break;
	case PROP_KEY:
		self->key = g_value_get_enum (value);
		break;
	case PROP_GAMEPAD_BUTTON:
		self->gamepad_button = g_value_get_enum (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_touch_button_class_init (LrgTouchButtonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;

	object_class->finalize = lrg_touch_button_finalize;
	object_class->get_property = lrg_touch_button_get_property;
	object_class->set_property = lrg_touch_button_set_property;

	/**
	 * LrgTouchButton:id:
	 *
	 * The button's id, unique within its #LrgTouchControls.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_ID] =
		g_param_spec_string ("id", "Id", "The button's id", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTouchButton:action:
	 *
	 * The #LrgInputAction name the button drives.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_ACTION] =
		g_param_spec_string ("action", "Action", "The input action it drives", NULL, rw);

	/**
	 * LrgTouchButton:label:
	 *
	 * Text drawn on the button.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_LABEL] =
		g_param_spec_string ("label", "Label", "Text drawn on it", NULL, rw);

	/**
	 * LrgTouchButton:icon:
	 *
	 * An icon name for skins.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_ICON] =
		g_param_spec_string ("icon", "Icon", "Icon name for skins", NULL, rw);

	/**
	 * LrgTouchButton:anchor:
	 *
	 * The screen point the offset is measured from.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_ANCHOR] =
		g_param_spec_enum ("anchor", "Anchor", "Screen point the offset starts from",
		                   LRG_TYPE_TOUCH_ANCHOR, LRG_TOUCH_ANCHOR_BOTTOM_RIGHT, rw);

	/**
	 * LrgTouchButton:offset-x:
	 *
	 * The centre's horizontal offset from the anchor, in dp.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_OFFSET_X] =
		g_param_spec_float ("offset-x", "Offset X", "Centre's offset from the anchor (dp)",
		                    -100000.0f, 100000.0f, -80.0f, rw);

	/**
	 * LrgTouchButton:offset-y:
	 *
	 * The centre's vertical offset from the anchor, in dp (down is
	 * positive).
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_OFFSET_Y] =
		g_param_spec_float ("offset-y", "Offset Y", "Centre's offset from the anchor (dp)",
		                    -100000.0f, 100000.0f, -80.0f, rw);

	/**
	 * LrgTouchButton:width:
	 *
	 * Width in dp.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_WIDTH] =
		g_param_spec_float ("width", "Width", "Width (dp)", 1.0f, 100000.0f, 64.0f, rw);

	/**
	 * LrgTouchButton:height:
	 *
	 * Height in dp.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_HEIGHT] =
		g_param_spec_float ("height", "Height", "Height (dp)", 1.0f, 100000.0f, 64.0f, rw);

	/**
	 * LrgTouchButton:mode:
	 *
	 * Hold or tap.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_MODE] =
		g_param_spec_enum ("mode", "Mode", "Hold or tap",
		                   LRG_TYPE_TOUCH_BUTTON_MODE, LRG_TOUCH_BUTTON_HOLD, rw);

	/**
	 * LrgTouchButton:shape:
	 *
	 * Circle or rounded rectangle.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_SHAPE] =
		g_param_spec_enum ("shape", "Shape", "Circle or rounded rectangle",
		                   LRG_TYPE_TOUCH_BUTTON_SHAPE, LRG_TOUCH_BUTTON_CIRCLE, rw);

	/**
	 * LrgTouchButton:key:
	 *
	 * A key to press for the action instead of looking the action up
	 * (%GRL_KEY_NULL: look it up).
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_KEY] =
		g_param_spec_enum ("key", "Key", "Key to press instead of the action's binding",
		                   GRL_TYPE_KEY, GRL_KEY_NULL, rw);

	/**
	 * LrgTouchButton:gamepad-button:
	 *
	 * A gamepad button to press for the action instead of looking the
	 * action up (%GRL_GAMEPAD_BUTTON_UNKNOWN: look it up).  Used when
	 * #LrgTouchButton:key is not set.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_GAMEPAD_BUTTON] =
		g_param_spec_enum ("gamepad-button", "Gamepad button",
		                   "Gamepad button to press instead of the action's binding",
		                   GRL_TYPE_GAMEPAD_BUTTON, GRL_GAMEPAD_BUTTON_UNKNOWN, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_touch_button_init (LrgTouchButton *self)
{
	self->anchor = LRG_TOUCH_ANCHOR_BOTTOM_RIGHT;
	self->offset_x = -80.0f;
	self->offset_y = -80.0f;
	self->width = 64.0f;
	self->height = 64.0f;
	self->mode = LRG_TOUCH_BUTTON_HOLD;
	self->shape = LRG_TOUCH_BUTTON_CIRCLE;
	self->key = GRL_KEY_NULL;
	self->gamepad_button = GRL_GAMEPAD_BUTTON_UNKNOWN;
}

/* lrg_touch_button_new: documented in lrg-touch-button.h */
LrgTouchButton *
lrg_touch_button_new (const gchar *id)
{
	g_return_val_if_fail (id != NULL && id[0] != '\0', NULL);

	return g_object_new (LRG_TYPE_TOUCH_BUTTON, "id", id, "action", id, NULL);
}

/* lrg_touch_button_get_id: documented in lrg-touch-button.h */
const gchar *
lrg_touch_button_get_id (LrgTouchButton *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_BUTTON (self), NULL);

	return self->id;
}

/* lrg_touch_button_get_action: documented in lrg-touch-button.h */
const gchar *
lrg_touch_button_get_action (LrgTouchButton *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_BUTTON (self), NULL);

	return self->action;
}

/* lrg_touch_button_get_label: documented in lrg-touch-button.h */
const gchar *
lrg_touch_button_get_label (LrgTouchButton *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_BUTTON (self), NULL);

	return self->label;
}

/* lrg_touch_button_get_icon: documented in lrg-touch-button.h */
const gchar *
lrg_touch_button_get_icon (LrgTouchButton *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_BUTTON (self), NULL);

	return self->icon;
}

/* lrg_touch_button_get_mode: documented in lrg-touch-button.h */
LrgTouchButtonMode
lrg_touch_button_get_mode (LrgTouchButton *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_BUTTON (self), LRG_TOUCH_BUTTON_HOLD);

	return self->mode;
}

/* lrg_touch_button_set_placement: documented in lrg-touch-button.h */
void
lrg_touch_button_set_placement (LrgTouchButton *self,
                                LrgTouchAnchor  anchor,
                                gfloat          offset_x,
                                gfloat          offset_y,
                                gfloat          width,
                                gfloat          height)
{
	g_return_if_fail (LRG_IS_TOUCH_BUTTON (self));

	g_object_set (self,
	              "anchor", anchor,
	              "offset-x", offset_x,
	              "offset-y", offset_y,
	              "width", width,
	              "height", height,
	              NULL);
}

/* lrg_touch_button_get_rect: documented in lrg-touch-button.h */
void
lrg_touch_button_get_rect (LrgTouchButton *self,
                           gfloat          screen_width,
                           gfloat          screen_height,
                           gfloat          px_per_dp,
                           gfloat         *x,
                           gfloat         *y,
                           gfloat         *width,
                           gfloat         *height)
{
	gfloat ax = 0.0f;
	gfloat ay = 0.0f;
	gfloat w;
	gfloat h;

	g_return_if_fail (LRG_IS_TOUCH_BUTTON (self));
	g_return_if_fail (x != NULL && y != NULL && width != NULL && height != NULL);

	/* the anchor: a column (left, middle, right) and a row */
	switch (self->anchor)
	{
	case LRG_TOUCH_ANCHOR_TOP:
	case LRG_TOUCH_ANCHOR_CENTER:
	case LRG_TOUCH_ANCHOR_BOTTOM:
		ax = screen_width * 0.5f;
		break;
	case LRG_TOUCH_ANCHOR_TOP_RIGHT:
	case LRG_TOUCH_ANCHOR_RIGHT:
	case LRG_TOUCH_ANCHOR_BOTTOM_RIGHT:
		ax = screen_width;
		break;
	case LRG_TOUCH_ANCHOR_TOP_LEFT:
	case LRG_TOUCH_ANCHOR_LEFT:
	case LRG_TOUCH_ANCHOR_BOTTOM_LEFT:
	default:
		ax = 0.0f;
		break;
	}
	switch (self->anchor)
	{
	case LRG_TOUCH_ANCHOR_LEFT:
	case LRG_TOUCH_ANCHOR_CENTER:
	case LRG_TOUCH_ANCHOR_RIGHT:
		ay = screen_height * 0.5f;
		break;
	case LRG_TOUCH_ANCHOR_BOTTOM_LEFT:
	case LRG_TOUCH_ANCHOR_BOTTOM:
	case LRG_TOUCH_ANCHOR_BOTTOM_RIGHT:
		ay = screen_height;
		break;
	case LRG_TOUCH_ANCHOR_TOP_LEFT:
	case LRG_TOUCH_ANCHOR_TOP:
	case LRG_TOUCH_ANCHOR_TOP_RIGHT:
	default:
		ay = 0.0f;
		break;
	}

	w = self->width * px_per_dp;
	h = self->height * px_per_dp;
	*x = ax + self->offset_x * px_per_dp - w * 0.5f;
	*y = ay + self->offset_y * px_per_dp - h * 0.5f;
	*width = w;
	*height = h;
}

/* lrg_touch_button_hit: documented in lrg-touch-button.h */
gboolean
lrg_touch_button_hit (LrgTouchButton *self,
                      gfloat          screen_width,
                      gfloat          screen_height,
                      gfloat          px_per_dp,
                      gfloat          x,
                      gfloat          y)
{
	gfloat rx, ry, rw, rh;

	g_return_val_if_fail (LRG_IS_TOUCH_BUTTON (self), FALSE);

	lrg_touch_button_get_rect (self, screen_width, screen_height, px_per_dp, &rx, &ry, &rw, &rh);
	if (x < rx || y < ry || x > rx + rw || y > ry + rh)
		return FALSE;
	if (self->shape == LRG_TOUCH_BUTTON_CIRCLE)
	{
		/* inside the ellipse: ((x - cx) / a)^2 + ((y - cy) / b)^2 <= 1 */
		gfloat nx = (x - (rx + rw * 0.5f)) / (rw * 0.5f);
		gfloat ny = (y - (ry + rh * 0.5f)) / (rh * 0.5f);

		return nx * nx + ny * ny <= 1.0f;
	}
	return TRUE;
}
