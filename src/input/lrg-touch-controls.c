/* lrg-touch-controls.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * On-screen touch controls.  Per frame: finish the previous frame's
 * clicks and reset per-frame output, decide visibility (auto mode), hand
 * new fingers to their owner (buttons, stick, or free), let lifted
 * fingers release what they held, move the stick, run the free fingers
 * through the tracker (look, zoom, pointer gestures), then write the
 * results into the sink.  Layouts are YAML, staged and validated whole
 * before any of it is applied.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>
#include <yaml-glib.h>

#include "lrg-touch-controls.h"
#include "lrg-input.h"
#include "lrg-input-software.h"
#include "lrg-input-manager.h"
#include "lrg-input-map.h"
#include "lrg-input-action.h"
#include "lrg-input-binding.h"
#include "../core/lrg-platform.h"

G_DEFINE_ENUM_TYPE (LrgTouchControlsMode, lrg_touch_controls_mode,
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_CONTROLS_AUTO, "auto"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_CONTROLS_SHOWN, "shown"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_CONTROLS_HIDDEN, "hidden"))

G_DEFINE_ENUM_TYPE (LrgTouchStickSide, lrg_touch_stick_side,
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_STICK_LEFT, "left"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_STICK_RIGHT, "right"))

G_DEFINE_QUARK (lrg-touch-controls-error-quark, lrg_touch_controls_error)

/* raylib's MAX_TOUCH_POINTS */
#define MAX_DEVICE_POINTS (10)

/* the input a button (or a pointer click) presses in the sink */
typedef enum
{
	EMU_NONE,
	EMU_KEY,
	EMU_MOUSE,
	EMU_GAMEPAD
} EmuKind;

typedef struct
{
	EmuKind kind;
	gint    code;      /* GrlKey, GrlMouseButton or GrlGamepadButton */
	gint    gamepad;
} Emu;

typedef struct
{
	LrgTouchButton *button;    /* owned */
	gint            finger;    /* -1: none */
	gboolean        down;
	gboolean        pressed;   /* this frame */
	gboolean        released;  /* this frame */
	Emu             held;      /* what a hold press pressed, to release it */
} ButtonState;

typedef enum
{
	OWNER_BUTTON,
	OWNER_STICK,
	OWNER_LOOK,    /* landed in the look region */
	OWNER_FREE,    /* landed elsewhere: the game's pointer */
	OWNER_NONE     /* claimed by something that went away: ignored */
} OwnerKind;

typedef struct
{
	gint      id;
	OwnerKind kind;
	gfloat    x;
	gfloat    y;
	gboolean  seen;
} Owned;

/* a normalised screen rectangle, fractions of the width and height */
typedef struct
{
	gfloat x;
	gfloat y;
	gfloat w;
	gfloat h;
} Region;

struct _LrgTouchControls
{
	GObject parent_instance;

	/* layout */
	GPtrArray *buttons;           /* ButtonState *, bottom first */
	gboolean   stick_enabled;
	Region     stick_region;
	gfloat     stick_radius;      /* dp */
	gfloat     stick_deadzone;    /* fraction of the radius */
	gfloat     stick_run_threshold;
	gboolean   stick_floating;
	LrgTouchStickSide stick_side;
	gboolean   look_enabled;
	Region     look_region;
	gfloat     look_sensitivity;
	gfloat     look_axis_gain;

	/* presentation */
	gfloat     scale;
	gfloat     opacity;
	gfloat     dp_scale;
	gfloat     font_size;         /* dp */
	GrlColor   base_color;
	GrlColor   knob_color;
	GrlColor   button_color;
	GrlColor   button_active_color;
	GrlColor   text_color;

	/* showing */
	LrgTouchControlsMode mode;
	gdouble    auto_hide_delay;
	gboolean   visible;
	gdouble    other_input_since; /* < 0: touched since */

	/* wiring */
	LrgInputSoftware *sink;
	LrgInput         *mouse_source;
	gboolean          mouse_disabled;   /* we disabled mouse_source */
	LrgInputMap      *input_map;
	gint              gamepad;
	gboolean          pointer_emulation;

	/* per-frame state */
	gfloat     screen_width;
	gfloat     screen_height;
	GArray    *owned;             /* Owned */
	GArray    *pending_release;   /* Emu: clicks to release next frame */
	LrgTouchTracker *tracker;          /* fingers that landed in the look region */
	LrgTouchTracker *pointer_tracker;  /* free fingers elsewhere; thresholds bound to tracker's */

	gboolean   stick_active;
	gint       stick_finger;
	gfloat     stick_origin_x;
	gfloat     stick_origin_y;
	gfloat     stick_finger_x;
	gfloat     stick_finger_y;
	gfloat     stick_x;
	gfloat     stick_y;
	gboolean   running;

	gfloat     look_dx;
	gfloat     look_dy;
	gfloat     zoom;
	gboolean   prev_touched;      /* touches in the last update (auto mode) */
};

enum
{
	PROP_0,
	PROP_MODE,
	PROP_VISIBLE,
	PROP_AUTO_HIDE_DELAY,
	PROP_SCALE,
	PROP_OPACITY,
	PROP_DP_SCALE,
	PROP_FONT_SIZE,
	PROP_SINK,
	PROP_MOUSE_SOURCE,
	PROP_INPUT_MAP,
	PROP_GAMEPAD,
	PROP_POINTER_EMULATION,
	PROP_STICK_ENABLED,
	PROP_STICK_REGION,
	PROP_STICK_RADIUS,
	PROP_STICK_DEADZONE,
	PROP_STICK_RUN_THRESHOLD,
	PROP_STICK_FLOATING,
	PROP_STICK_SIDE,
	PROP_LOOK_ENABLED,
	PROP_LOOK_REGION,
	PROP_LOOK_SENSITIVITY,
	PROP_LOOK_AXIS_GAIN,
	PROP_BASE_COLOR,
	PROP_KNOB_COLOR,
	PROP_BUTTON_COLOR,
	PROP_BUTTON_ACTIVE_COLOR,
	PROP_TEXT_COLOR,
	N_PROPS
};

enum
{
	SIGNAL_BUTTON_PRESSED,
	SIGNAL_BUTTON_RELEASED,
	SIGNAL_POINTER_GESTURE,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgTouchControls, lrg_touch_controls, G_TYPE_OBJECT)

static void release_all (LrgTouchControls *self);
static void on_tracker_gesture (LrgTouchTracker       *tracker,
                                const LrgTouchGesture *gesture,
                                gpointer               user_data);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static void
button_state_free (gpointer data)
{
	ButtonState *state = data;

	g_clear_object (&state->button);
	g_free (state);
}

static void
set_color (GrlColor     *dest,
           const GValue *value)
{
	const GrlColor *color = g_value_get_boxed (value);

	if (color != NULL)
		*dest = *color;
}

static void
set_region (Region       *dest,
            const GValue *value)
{
	const GrlRectangle *rect = g_value_get_boxed (value);

	if (rect == NULL)
		return;
	dest->x = rect->x;
	dest->y = rect->y;
	dest->w = rect->width;
	dest->h = rect->height;
}

static gboolean
in_region (LrgTouchControls *self,
           const Region     *region,
           gfloat            x,
           gfloat            y)
{
	gfloat rx = region->x * self->screen_width;
	gfloat ry = region->y * self->screen_height;
	gfloat rw = region->w * self->screen_width;
	gfloat rh = region->h * self->screen_height;

	return x >= rx && y >= ry && x <= rx + rw && y <= ry + rh;
}

static gfloat
px_per_dp (LrgTouchControls *self)
{
	return self->dp_scale * self->scale;
}

static ButtonState *
find_button (LrgTouchControls *self,
             const gchar      *id,
             guint            *index)
{
	guint i;

	for (i = 0; i < self->buttons->len; i++)
	{
		ButtonState *state = g_ptr_array_index (self->buttons, i);

		if (g_strcmp0 (lrg_touch_button_get_id (state->button), id) == 0)
		{
			if (index != NULL)
				*index = i;
			return state;
		}
	}
	return NULL;
}

static Owned *
find_owned (LrgTouchControls *self,
            gint              id)
{
	guint i;

	for (i = 0; i < self->owned->len; i++)
	{
		Owned *o = &g_array_index (self->owned, Owned, i);

		if (o->id == id)
			return o;
	}
	return NULL;
}

/* ==========================================================================
 * The sink
 * ========================================================================== */

/* resolve_emu: what pressing @button means in the sink: its explicit key
 * or gamepad button, else the first key / mouse / gamepad button binding
 * of its action in the input map */
static Emu
resolve_emu (LrgTouchControls *self,
             LrgTouchButton   *button)
{
	Emu emu = { EMU_NONE, 0, 0 };
	GrlKey key = GRL_KEY_NULL;
	GrlGamepadButton pad = GRL_GAMEPAD_BUTTON_UNKNOWN;
	const gchar *action_name;

	g_object_get (button, "key", &key, "gamepad-button", &pad, NULL);
	if (key != GRL_KEY_NULL)
	{
		emu.kind = EMU_KEY;
		emu.code = (gint) key;
		return emu;
	}
	if (pad != GRL_GAMEPAD_BUTTON_UNKNOWN)
	{
		emu.kind = EMU_GAMEPAD;
		emu.code = (gint) pad;
		emu.gamepad = self->gamepad;
		return emu;
	}

	action_name = lrg_touch_button_get_action (button);
	if (self->input_map != NULL && action_name != NULL)
	{
		LrgInputAction *action = lrg_input_map_get_action (self->input_map, action_name);
		guint i;
		guint n;

		n = action != NULL ? lrg_input_action_get_binding_count (action) : 0;
		for (i = 0; i < n; i++)
		{
			const LrgInputBinding *binding = lrg_input_action_get_binding (action, i);

			switch (lrg_input_binding_get_binding_type (binding))
			{
			case LRG_INPUT_BINDING_KEYBOARD:
				emu.kind = EMU_KEY;
				emu.code = (gint) lrg_input_binding_get_key (binding);
				return emu;
			case LRG_INPUT_BINDING_MOUSE_BUTTON:
				emu.kind = EMU_MOUSE;
				emu.code = (gint) lrg_input_binding_get_mouse_button (binding);
				return emu;
			case LRG_INPUT_BINDING_GAMEPAD_BUTTON:
				emu.kind = EMU_GAMEPAD;
				emu.code = (gint) lrg_input_binding_get_gamepad_button (binding);
				emu.gamepad = lrg_input_binding_get_gamepad (binding);
				return emu;
			case LRG_INPUT_BINDING_GAMEPAD_AXIS:
			default:
				break;
			}
		}
	}
	return emu;
}

static void
emu_press (LrgTouchControls *self,
           const Emu        *emu)
{
	if (self->sink == NULL)
		return;
	switch (emu->kind)
	{
	case EMU_KEY:
		lrg_input_software_press_key (self->sink, (GrlKey) emu->code);
		break;
	case EMU_MOUSE:
		lrg_input_software_press_mouse_button (self->sink, (GrlMouseButton) emu->code);
		break;
	case EMU_GAMEPAD:
		lrg_input_software_press_gamepad_button (self->sink, emu->gamepad, (GrlGamepadButton) emu->code);
		break;
	case EMU_NONE:
	default:
		break;
	}
}

static void
emu_release (LrgTouchControls *self,
             const Emu        *emu)
{
	if (self->sink == NULL)
		return;
	switch (emu->kind)
	{
	case EMU_KEY:
		lrg_input_software_release_key (self->sink, (GrlKey) emu->code);
		break;
	case EMU_MOUSE:
		lrg_input_software_release_mouse_button (self->sink, (GrlMouseButton) emu->code);
		break;
	case EMU_GAMEPAD:
		lrg_input_software_release_gamepad_button (self->sink, emu->gamepad, (GrlGamepadButton) emu->code);
		break;
	case EMU_NONE:
	default:
		break;
	}
}

/* emu_click: pressed now, released at the next feed */
static void
emu_click (LrgTouchControls *self,
           const Emu        *emu)
{
	if (emu->kind == EMU_NONE || self->sink == NULL)
		return;
	emu_press (self, emu);
	g_array_append_val (self->pending_release, *emu);
}

/* write_axes: the stick and the look region into the sink's sticks */
static void
write_axes (LrgTouchControls *self)
{
	GrlGamepadAxis stick_x_axis, stick_y_axis, look_x_axis, look_y_axis;

	if (self->sink == NULL)
		return;
	if (self->stick_side == LRG_TOUCH_STICK_LEFT)
	{
		stick_x_axis = GRL_GAMEPAD_AXIS_LEFT_X;
		stick_y_axis = GRL_GAMEPAD_AXIS_LEFT_Y;
		look_x_axis = GRL_GAMEPAD_AXIS_RIGHT_X;
		look_y_axis = GRL_GAMEPAD_AXIS_RIGHT_Y;
	}
	else
	{
		stick_x_axis = GRL_GAMEPAD_AXIS_RIGHT_X;
		stick_y_axis = GRL_GAMEPAD_AXIS_RIGHT_Y;
		look_x_axis = GRL_GAMEPAD_AXIS_LEFT_X;
		look_y_axis = GRL_GAMEPAD_AXIS_LEFT_Y;
	}
	lrg_input_software_set_gamepad_axis (self->sink, self->gamepad, stick_x_axis, self->stick_x);
	lrg_input_software_set_gamepad_axis (self->sink, self->gamepad, stick_y_axis, self->stick_y);
	lrg_input_software_set_gamepad_axis (self->sink, self->gamepad, look_x_axis,
	                                     CLAMP (self->look_dx * self->look_axis_gain, -1.0f, 1.0f));
	lrg_input_software_set_gamepad_axis (self->sink, self->gamepad, look_y_axis,
	                                     CLAMP (self->look_dy * self->look_axis_gain, -1.0f, 1.0f));
}

/* update_mouse_source: the mouse source is off while the controls are
 * shown with pointer emulation (raylib holds its left button for every
 * touch), and back on otherwise */
static void
update_mouse_source (LrgTouchControls *self)
{
	gboolean want_disabled = self->visible && self->pointer_emulation && self->mouse_source != NULL;

	if (want_disabled && !self->mouse_disabled)
	{
		lrg_input_set_enabled (self->mouse_source, FALSE);
		self->mouse_disabled = TRUE;
	}
	else if (!want_disabled && self->mouse_disabled)
	{
		if (self->mouse_source != NULL)
			lrg_input_set_enabled (self->mouse_source, TRUE);
		self->mouse_disabled = FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_touch_controls_dispose (GObject *object)
{
	LrgTouchControls *self = LRG_TOUCH_CONTROLS (object);

	/* dispose may run twice: the second time there is nothing to release */
	if (self->buttons != NULL && self->tracker != NULL)
		release_all (self);
	self->visible = FALSE;
	update_mouse_source (self);
	if (self->tracker != NULL)
		g_signal_handlers_disconnect_by_func (self->tracker, on_tracker_gesture, self);
	if (self->pointer_tracker != NULL)
		g_signal_handlers_disconnect_by_func (self->pointer_tracker, on_tracker_gesture, self);
	g_clear_object (&self->tracker);
	g_clear_object (&self->pointer_tracker);
	g_clear_object (&self->sink);
	g_clear_object (&self->mouse_source);
	g_clear_object (&self->input_map);

	G_OBJECT_CLASS (lrg_touch_controls_parent_class)->dispose (object);
}

static void
lrg_touch_controls_finalize (GObject *object)
{
	LrgTouchControls *self = LRG_TOUCH_CONTROLS (object);

	g_clear_pointer (&self->buttons, g_ptr_array_unref);
	g_clear_pointer (&self->owned, g_array_unref);
	g_clear_pointer (&self->pending_release, g_array_unref);

	G_OBJECT_CLASS (lrg_touch_controls_parent_class)->finalize (object);
}

static void
lrg_touch_controls_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgTouchControls *self = LRG_TOUCH_CONTROLS (object);

	switch (prop_id)
	{
	case PROP_MODE:
		g_value_set_enum (value, self->mode);
		break;
	case PROP_VISIBLE:
		g_value_set_boolean (value, self->visible);
		break;
	case PROP_AUTO_HIDE_DELAY:
		g_value_set_double (value, self->auto_hide_delay);
		break;
	case PROP_SCALE:
		g_value_set_float (value, self->scale);
		break;
	case PROP_OPACITY:
		g_value_set_float (value, self->opacity);
		break;
	case PROP_DP_SCALE:
		g_value_set_float (value, self->dp_scale);
		break;
	case PROP_FONT_SIZE:
		g_value_set_float (value, self->font_size);
		break;
	case PROP_SINK:
		g_value_set_object (value, self->sink);
		break;
	case PROP_MOUSE_SOURCE:
		g_value_set_object (value, self->mouse_source);
		break;
	case PROP_INPUT_MAP:
		g_value_set_object (value, self->input_map);
		break;
	case PROP_GAMEPAD:
		g_value_set_int (value, self->gamepad);
		break;
	case PROP_POINTER_EMULATION:
		g_value_set_boolean (value, self->pointer_emulation);
		break;
	case PROP_STICK_ENABLED:
		g_value_set_boolean (value, self->stick_enabled);
		break;
	case PROP_STICK_REGION:
		g_value_take_boxed (value, grl_rectangle_new (self->stick_region.x, self->stick_region.y,
		                                              self->stick_region.w, self->stick_region.h));
		break;
	case PROP_STICK_RADIUS:
		g_value_set_float (value, self->stick_radius);
		break;
	case PROP_STICK_DEADZONE:
		g_value_set_float (value, self->stick_deadzone);
		break;
	case PROP_STICK_RUN_THRESHOLD:
		g_value_set_float (value, self->stick_run_threshold);
		break;
	case PROP_STICK_FLOATING:
		g_value_set_boolean (value, self->stick_floating);
		break;
	case PROP_STICK_SIDE:
		g_value_set_enum (value, self->stick_side);
		break;
	case PROP_LOOK_ENABLED:
		g_value_set_boolean (value, self->look_enabled);
		break;
	case PROP_LOOK_REGION:
		g_value_take_boxed (value, grl_rectangle_new (self->look_region.x, self->look_region.y,
		                                              self->look_region.w, self->look_region.h));
		break;
	case PROP_LOOK_SENSITIVITY:
		g_value_set_float (value, self->look_sensitivity);
		break;
	case PROP_LOOK_AXIS_GAIN:
		g_value_set_float (value, self->look_axis_gain);
		break;
	case PROP_BASE_COLOR:
		g_value_set_boxed (value, &self->base_color);
		break;
	case PROP_KNOB_COLOR:
		g_value_set_boxed (value, &self->knob_color);
		break;
	case PROP_BUTTON_COLOR:
		g_value_set_boxed (value, &self->button_color);
		break;
	case PROP_BUTTON_ACTIVE_COLOR:
		g_value_set_boxed (value, &self->button_active_color);
		break;
	case PROP_TEXT_COLOR:
		g_value_set_boxed (value, &self->text_color);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_touch_controls_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgTouchControls *self = LRG_TOUCH_CONTROLS (object);

	switch (prop_id)
	{
	case PROP_MODE:
		self->mode = g_value_get_enum (value);
		if (self->mode == LRG_TOUCH_CONTROLS_SHOWN && !self->visible)
		{
			self->visible = TRUE;
			g_object_notify_by_pspec (object, properties[PROP_VISIBLE]);
		}
		else if (self->mode == LRG_TOUCH_CONTROLS_HIDDEN && self->visible)
		{
			release_all (self);
			self->visible = FALSE;
			g_object_notify_by_pspec (object, properties[PROP_VISIBLE]);
		}
		update_mouse_source (self);
		break;
	case PROP_AUTO_HIDE_DELAY:
		self->auto_hide_delay = g_value_get_double (value);
		break;
	case PROP_SCALE:
		self->scale = g_value_get_float (value);
		break;
	case PROP_OPACITY:
		self->opacity = g_value_get_float (value);
		break;
	case PROP_DP_SCALE:
		self->dp_scale = g_value_get_float (value);
		lrg_touch_tracker_set_dp_scale (self->tracker, self->dp_scale);
		break;
	case PROP_FONT_SIZE:
		self->font_size = g_value_get_float (value);
		break;
	case PROP_SINK:
		release_all (self);
		g_set_object (&self->sink, g_value_get_object (value));
		break;
	case PROP_MOUSE_SOURCE:
		{
			/* hand the old source back enabled before switching */
			gboolean was_visible = self->visible;

			self->visible = FALSE;
			update_mouse_source (self);
			g_set_object (&self->mouse_source, g_value_get_object (value));
			self->visible = was_visible;
			update_mouse_source (self);
		}
		break;
	case PROP_INPUT_MAP:
		g_set_object (&self->input_map, g_value_get_object (value));
		break;
	case PROP_GAMEPAD:
		release_all (self);
		self->gamepad = g_value_get_int (value);
		break;
	case PROP_POINTER_EMULATION:
		self->pointer_emulation = g_value_get_boolean (value);
		update_mouse_source (self);
		break;
	case PROP_STICK_ENABLED:
		release_all (self);
		self->stick_enabled = g_value_get_boolean (value);
		break;
	case PROP_STICK_REGION:
		set_region (&self->stick_region, value);
		break;
	case PROP_STICK_RADIUS:
		self->stick_radius = g_value_get_float (value);
		break;
	case PROP_STICK_DEADZONE:
		self->stick_deadzone = g_value_get_float (value);
		break;
	case PROP_STICK_RUN_THRESHOLD:
		self->stick_run_threshold = g_value_get_float (value);
		break;
	case PROP_STICK_FLOATING:
		self->stick_floating = g_value_get_boolean (value);
		break;
	case PROP_STICK_SIDE:
		release_all (self);
		self->stick_side = g_value_get_enum (value);
		break;
	case PROP_LOOK_ENABLED:
		self->look_enabled = g_value_get_boolean (value);
		break;
	case PROP_LOOK_REGION:
		set_region (&self->look_region, value);
		break;
	case PROP_LOOK_SENSITIVITY:
		self->look_sensitivity = g_value_get_float (value);
		break;
	case PROP_LOOK_AXIS_GAIN:
		self->look_axis_gain = g_value_get_float (value);
		break;
	case PROP_BASE_COLOR:
		set_color (&self->base_color, value);
		break;
	case PROP_KNOB_COLOR:
		set_color (&self->knob_color, value);
		break;
	case PROP_BUTTON_COLOR:
		set_color (&self->button_color, value);
		break;
	case PROP_BUTTON_ACTIVE_COLOR:
		set_color (&self->button_active_color, value);
		break;
	case PROP_TEXT_COLOR:
		set_color (&self->text_color, value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_touch_controls_class_init (LrgTouchControlsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS;

	object_class->dispose = lrg_touch_controls_dispose;
	object_class->finalize = lrg_touch_controls_finalize;
	object_class->get_property = lrg_touch_controls_get_property;
	object_class->set_property = lrg_touch_controls_set_property;

	/**
	 * LrgTouchControls:mode:
	 *
	 * Auto, shown or hidden.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_MODE] =
		g_param_spec_enum ("mode", "Mode", "When the controls are shown",
		                   LRG_TYPE_TOUCH_CONTROLS_MODE, LRG_TOUCH_CONTROLS_AUTO, rw);

	/**
	 * LrgTouchControls:visible:
	 *
	 * Whether the controls are shown and claim fingers.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_VISIBLE] =
		g_param_spec_boolean ("visible", "Visible", "Shown and claiming fingers", FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTouchControls:auto-hide-delay:
	 *
	 * Auto mode: seconds of mouse, keyboard or gamepad input without a
	 * touch after which the controls hide.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_AUTO_HIDE_DELAY] =
		g_param_spec_double ("auto-hide-delay", "Auto hide delay",
		                     "Seconds of other input before auto mode hides (s)",
		                     0.0, 3600.0, 10.0, rw);

	/**
	 * LrgTouchControls:scale:
	 *
	 * The player's size preference: multiplies every dp size and offset.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_SCALE] =
		g_param_spec_float ("scale", "Scale", "Size multiplier", 0.25f, 4.0f, 1.0f, rw);

	/**
	 * LrgTouchControls:opacity:
	 *
	 * Multiplies the alpha of every colour drawn.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_OPACITY] =
		g_param_spec_float ("opacity", "Opacity", "Overlay opacity", 0.0f, 1.0f, 0.6f, rw);

	/**
	 * LrgTouchControls:dp-scale:
	 *
	 * Pixels per dp (lrg_platform_get_dp_scale(), the default); also the
	 * tracker's.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_DP_SCALE] =
		g_param_spec_float ("dp-scale", "dp scale", "Pixels per dp", 0.01f, 100.0f, 1.0f, rw);

	/**
	 * LrgTouchControls:font-size:
	 *
	 * Label size in dp.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_FONT_SIZE] =
		g_param_spec_float ("font-size", "Font size", "Label size (dp)", 1.0f, 512.0f, 16.0f, rw);

	/**
	 * LrgTouchControls:sink:
	 *
	 * The #LrgInputSoftware the controls write axes and presses into (add
	 * it to the #LrgInputManager).  %NULL: only the direct getters.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_SINK] =
		g_param_spec_object ("sink", "Sink", "Software input the controls write into",
		                     LRG_TYPE_INPUT_SOFTWARE, rw);

	/**
	 * LrgTouchControls:mouse-source:
	 *
	 * An input source (the manager's "mouse") disabled while the controls
	 * are shown with #LrgTouchControls:pointer-emulation: raylib reports
	 * every touch as its left mouse button, which would otherwise click
	 * wherever the stick or a button is used.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_MOUSE_SOURCE] =
		g_param_spec_object ("mouse-source", "Mouse source",
		                     "Source disabled while the controls are shown",
		                     LRG_TYPE_INPUT, rw);

	/**
	 * LrgTouchControls:input-map:
	 *
	 * Where buttons look up their action's bindings.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_INPUT_MAP] =
		g_param_spec_object ("input-map", "Input map", "Where button actions are looked up",
		                     LRG_TYPE_INPUT_MAP, rw);

	/**
	 * LrgTouchControls:gamepad:
	 *
	 * The sink gamepad (0-3) whose sticks the controls drive.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_GAMEPAD] =
		g_param_spec_int ("gamepad", "Gamepad", "Sink gamepad index", 0, 3, 0, rw);

	/**
	 * LrgTouchControls:pointer-emulation:
	 *
	 * Whether free taps and long presses click (left, right) through the
	 * sink at the finger, and the sink's mouse follows free fingers.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_POINTER_EMULATION] =
		g_param_spec_boolean ("pointer-emulation", "Pointer emulation",
		                      "Free taps click through the sink", TRUE, rw);

	/**
	 * LrgTouchControls:stick-enabled:
	 *
	 * Whether there is a stick.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_STICK_ENABLED] =
		g_param_spec_boolean ("stick-enabled", "Stick enabled", "There is a stick", TRUE, rw);

	/**
	 * LrgTouchControls:stick-region:
	 *
	 * Where a finger spawns the stick, as fractions of the screen
	 * (x, y, width, height in 0..1).
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_STICK_REGION] =
		g_param_spec_boxed ("stick-region", "Stick region", "Stick spawn area (fractions)",
		                    GRL_TYPE_RECTANGLE, rw);

	/**
	 * LrgTouchControls:stick-radius:
	 *
	 * How far the finger moves for a full push, in dp.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_STICK_RADIUS] =
		g_param_spec_float ("stick-radius", "Stick radius", "Full push distance (dp)",
		                    1.0f, 10000.0f, 60.0f, rw);

	/**
	 * LrgTouchControls:stick-deadzone:
	 *
	 * The fraction of the radius that gives 0.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_STICK_DEADZONE] =
		g_param_spec_float ("stick-deadzone", "Stick deadzone", "Deadzone (fraction of radius)",
		                    0.0f, 0.95f, 0.15f, rw);

	/**
	 * LrgTouchControls:stick-run-threshold:
	 *
	 * The output length from which lrg_touch_controls_is_running() is
	 * %TRUE.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_STICK_RUN_THRESHOLD] =
		g_param_spec_float ("stick-run-threshold", "Stick run threshold",
		                    "Output length that runs", 0.0f, 1.0f, 0.8f, rw);

	/**
	 * LrgTouchControls:stick-floating:
	 *
	 * %TRUE: the stick centres where the finger lands; %FALSE: at the
	 * region's centre.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_STICK_FLOATING] =
		g_param_spec_boolean ("stick-floating", "Stick floating",
		                      "The stick centres where the finger lands", TRUE, rw);

	/**
	 * LrgTouchControls:stick-side:
	 *
	 * Which gamepad stick the stick drives (the look region drives the
	 * other).
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_STICK_SIDE] =
		g_param_spec_enum ("stick-side", "Stick side", "Gamepad stick the stick drives",
		                   LRG_TYPE_TOUCH_STICK_SIDE, LRG_TOUCH_STICK_LEFT, rw);

	/**
	 * LrgTouchControls:look-enabled:
	 *
	 * Whether there is a look region.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_LOOK_ENABLED] =
		g_param_spec_boolean ("look-enabled", "Look enabled", "There is a look region", TRUE, rw);

	/**
	 * LrgTouchControls:look-region:
	 *
	 * Where free drags turn the camera and pinches zoom, as fractions of
	 * the screen.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_LOOK_REGION] =
		g_param_spec_boxed ("look-region", "Look region", "Camera drag area (fractions)",
		                    GRL_TYPE_RECTANGLE, rw);

	/**
	 * LrgTouchControls:look-sensitivity:
	 *
	 * Multiplies the look finger's pixels.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_LOOK_SENSITIVITY] =
		g_param_spec_float ("look-sensitivity", "Look sensitivity", "Look delta multiplier",
		                    0.0f, 100.0f, 1.0f, rw);

	/**
	 * LrgTouchControls:look-axis-gain:
	 *
	 * Look pixels per frame to gamepad axis units in the sink (clamped to
	 * -1..1).
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_LOOK_AXIS_GAIN] =
		g_param_spec_float ("look-axis-gain", "Look axis gain", "Axis units per look pixel",
		                    0.0f, 100.0f, 0.05f, rw);

	/**
	 * LrgTouchControls:base-color:
	 *
	 * The stick's base colour.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_BASE_COLOR] =
		g_param_spec_boxed ("base-color", "Base color", "Stick base colour", GRL_TYPE_COLOR, rw);

	/**
	 * LrgTouchControls:knob-color:
	 *
	 * The stick's knob colour.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_KNOB_COLOR] =
		g_param_spec_boxed ("knob-color", "Knob color", "Stick knob colour", GRL_TYPE_COLOR, rw);

	/**
	 * LrgTouchControls:button-color:
	 *
	 * A button's colour at rest.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_BUTTON_COLOR] =
		g_param_spec_boxed ("button-color", "Button color", "Button colour", GRL_TYPE_COLOR, rw);

	/**
	 * LrgTouchControls:button-active-color:
	 *
	 * A button's colour while a finger is on it.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_BUTTON_ACTIVE_COLOR] =
		g_param_spec_boxed ("button-active-color", "Button active color",
		                    "Held button colour", GRL_TYPE_COLOR, rw);

	/**
	 * LrgTouchControls:text-color:
	 *
	 * The label colour.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_TEXT_COLOR] =
		g_param_spec_boxed ("text-color", "Text color", "Label colour", GRL_TYPE_COLOR, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTouchControls::button-pressed:
	 * @self: the #LrgTouchControls
	 * @id: the button's id
	 *
	 * A button's action was pressed (a hold button when the finger
	 * landed, a tap button when it lifted inside).
	 *
	 * Since: 0.13.0
	 */
	signals[SIGNAL_BUTTON_PRESSED] =
		g_signal_new ("button-pressed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgTouchControls::button-released:
	 * @self: the #LrgTouchControls
	 * @id: the button's id
	 *
	 * A button's action was released.
	 *
	 * Since: 0.13.0
	 */
	signals[SIGNAL_BUTTON_RELEASED] =
		g_signal_new ("button-released", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * LrgTouchControls::pointer-gesture:
	 * @self: the #LrgTouchControls
	 * @gesture: the #LrgTouchGesture
	 *
	 * A free finger's gesture the controls did not use: taps, double
	 * taps, long presses, two-finger taps, drags outside the look region,
	 * pinches outside it.  The game's clicks.
	 *
	 * Since: 0.13.0
	 */
	signals[SIGNAL_POINTER_GESTURE] =
		g_signal_new ("pointer-gesture", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		              LRG_TYPE_TOUCH_GESTURE | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
init_color (GrlColor *color,
            guint8    r,
            guint8    g,
            guint8    b,
            guint8    a)
{
	color->r = r;
	color->g = g;
	color->b = b;
	color->a = a;
}

static void
lrg_touch_controls_init (LrgTouchControls *self)
{
	self->buttons = g_ptr_array_new_with_free_func (button_state_free);
	self->owned = g_array_new (FALSE, TRUE, sizeof (Owned));
	self->pending_release = g_array_new (FALSE, TRUE, sizeof (Emu));
	self->tracker = lrg_touch_tracker_new ();
	self->pointer_tracker = lrg_touch_tracker_new ();
	{
		static const gchar *const thresholds[] = {
			"tap-max-duration", "tap-slop", "double-tap-interval",
			"double-tap-slop", "long-press-duration", "dp-scale"
		};
		guint i;

		/* one set of thresholds: the look tracker's, copied */
		for (i = 0; i < G_N_ELEMENTS (thresholds); i++)
			g_object_bind_property (self->tracker, thresholds[i],
			                        self->pointer_tracker, thresholds[i],
			                        G_BINDING_SYNC_CREATE);
	}
	g_signal_connect (self->tracker, "gesture", G_CALLBACK (on_tracker_gesture), self);
	g_signal_connect (self->pointer_tracker, "gesture", G_CALLBACK (on_tracker_gesture), self);

	self->stick_enabled = TRUE;
	self->stick_region.x = 0.0f;
	self->stick_region.y = 0.35f;
	self->stick_region.w = 0.45f;
	self->stick_region.h = 0.65f;
	self->stick_radius = 60.0f;
	self->stick_deadzone = 0.15f;
	self->stick_run_threshold = 0.8f;
	self->stick_floating = TRUE;
	self->stick_side = LRG_TOUCH_STICK_LEFT;
	self->look_enabled = TRUE;
	self->look_region.x = 0.45f;
	self->look_region.y = 0.0f;
	self->look_region.w = 0.55f;
	self->look_region.h = 1.0f;
	self->look_sensitivity = 1.0f;
	self->look_axis_gain = 0.05f;

	self->scale = 1.0f;
	self->opacity = 0.6f;
	self->dp_scale = lrg_platform_get_dp_scale ();
	lrg_touch_tracker_set_dp_scale (self->tracker, self->dp_scale);
	self->font_size = 16.0f;
	init_color (&self->base_color, 200, 200, 200, 110);
	init_color (&self->knob_color, 240, 240, 240, 200);
	init_color (&self->button_color, 60, 60, 70, 160);
	init_color (&self->button_active_color, 70, 130, 220, 210);
	init_color (&self->text_color, 255, 255, 255, 230);

	self->mode = LRG_TOUCH_CONTROLS_AUTO;
	self->auto_hide_delay = 10.0;
	self->visible = FALSE;
	self->other_input_since = -1.0;
	self->pointer_emulation = TRUE;
	self->gamepad = 0;
	self->screen_width = 1280.0f;
	self->screen_height = 720.0f;
	self->stick_finger = -1;
	self->zoom = 1.0f;
}

/* lrg_touch_controls_new: documented in lrg-touch-controls.h */
LrgTouchControls *
lrg_touch_controls_new (void)
{
	return g_object_new (LRG_TYPE_TOUCH_CONTROLS, NULL);
}

/* lrg_touch_controls_attach: documented in lrg-touch-controls.h */
void
lrg_touch_controls_attach (LrgTouchControls *self,
                           LrgInputManager  *manager)
{
	g_autoptr(LrgInputSoftware) sink = NULL;

	g_return_if_fail (LRG_IS_TOUCH_CONTROLS (self));
	g_return_if_fail (LRG_IS_INPUT_MANAGER (manager));

	if (self->sink != NULL)
		lrg_input_manager_remove_source (manager, LRG_INPUT (self->sink));
	sink = lrg_input_software_new ();
	g_object_set (sink, "name", "touch-controls", NULL);
	lrg_input_manager_add_source (manager, LRG_INPUT (sink));
	g_object_set (self,
	              "sink", sink,
	              "mouse-source", lrg_input_manager_get_source (manager, "mouse"),
	              NULL);
}

/* ==========================================================================
 * Releasing
 * ========================================================================== */

/* release_button: the finger left a button (or the button went away) */
static void
release_button (LrgTouchControls *self,
                ButtonState      *state,
                gboolean          inside)
{
	const gchar *id = lrg_touch_button_get_id (state->button);

	state->finger = -1;
	if (!state->down)
		return;
	state->down = FALSE;
	if (lrg_touch_button_get_mode (state->button) == LRG_TOUCH_BUTTON_HOLD)
	{
		emu_release (self, &state->held);
		state->held.kind = EMU_NONE;
		state->released = TRUE;
		g_signal_emit (self, signals[SIGNAL_BUTTON_RELEASED], 0, id);
	}
	else if (inside)
	{
		/* a tap activates on lift: pressed and released at once */
		Emu emu = resolve_emu (self, state->button);

		emu_click (self, &emu);
		state->pressed = TRUE;
		state->released = TRUE;
		g_signal_emit (self, signals[SIGNAL_BUTTON_PRESSED], 0, id);
		g_signal_emit (self, signals[SIGNAL_BUTTON_RELEASED], 0, id);
	}
}

/* release_stick: back to the centre */
static void
release_stick (LrgTouchControls *self)
{
	self->stick_active = FALSE;
	self->stick_finger = -1;
	self->stick_x = 0.0f;
	self->stick_y = 0.0f;
	self->running = FALSE;
}

/* release_all: every finger let go without effects (hidden, a new
 * layout, a different sink): holds released, stick and look at rest */
static void
release_all (LrgTouchControls *self)
{
	guint i;

	for (i = 0; i < self->buttons->len; i++)
	{
		ButtonState *state = g_ptr_array_index (self->buttons, i);

		if (state->down && lrg_touch_button_get_mode (state->button) == LRG_TOUCH_BUTTON_HOLD)
			release_button (self, state, FALSE);
		state->down = FALSE;
		state->finger = -1;
	}
	for (i = 0; i < self->pending_release->len; i++)
		emu_release (self, &g_array_index (self->pending_release, Emu, i));
	g_array_set_size (self->pending_release, 0);
	release_stick (self);
	self->look_dx = 0.0f;
	self->look_dy = 0.0f;
	self->zoom = 1.0f;
	g_array_set_size (self->owned, 0);
	lrg_touch_tracker_reset (self->tracker);
	lrg_touch_tracker_reset (self->pointer_tracker);
	write_axes (self);
}

/* ==========================================================================
 * Frames
 * ========================================================================== */

/* on_tracker_gesture: the look fingers' drags and pinches move the
 * camera; everything else (their taps and long presses, and every
 * gesture of the fingers that landed elsewhere) is the game's */
static void
on_tracker_gesture (LrgTouchTracker       *tracker,
                    const LrgTouchGesture *gesture,
                    gpointer               user_data)
{
	LrgTouchControls *self = user_data;

	if (tracker == self->tracker)
	{
		switch (gesture->type)
		{
		case LRG_TOUCH_GESTURE_DRAG_BEGIN:
		case LRG_TOUCH_GESTURE_DRAG:
			self->look_dx += gesture->dx * self->look_sensitivity;
			self->look_dy += gesture->dy * self->look_sensitivity;
			return;
		case LRG_TOUCH_GESTURE_PINCH_BEGIN:
		case LRG_TOUCH_GESTURE_PINCH:
			self->zoom *= gesture->scale;
			return;
		case LRG_TOUCH_GESTURE_DRAG_END:
		case LRG_TOUCH_GESTURE_PINCH_END:
			return;
		case LRG_TOUCH_GESTURE_TAP:
		case LRG_TOUCH_GESTURE_DOUBLE_TAP:
		case LRG_TOUCH_GESTURE_LONG_PRESS:
		case LRG_TOUCH_GESTURE_TWO_FINGER_TAP:
		default:
			break;
		}
	}

	if (self->pointer_emulation && self->sink != NULL &&
	    (gesture->type == LRG_TOUCH_GESTURE_TAP || gesture->type == LRG_TOUCH_GESTURE_LONG_PRESS))
	{
		Emu click = { EMU_MOUSE, 0, 0 };

		click.code = gesture->type == LRG_TOUCH_GESTURE_TAP ? (gint) GRL_MOUSE_BUTTON_LEFT
		                                                    : (gint) GRL_MOUSE_BUTTON_RIGHT;
		lrg_input_software_move_mouse_to (self->sink, gesture->x, gesture->y);
		emu_click (self, &click);
	}
	g_signal_emit (self, signals[SIGNAL_POINTER_GESTURE], 0, gesture);
}

/* claim: the owner of a finger that just landed: the topmost button under
 * it, else the stick (when free and the finger is in its region), else
 * the look region (when it is there), else the game's pointer */
static OwnerKind
claim (LrgTouchControls    *self,
       const LrgTouchPoint *point)
{
	gfloat ppd = px_per_dp (self);
	guint i;

	for (i = self->buttons->len; i > 0; i--)
	{
		ButtonState *state = g_ptr_array_index (self->buttons, i - 1);

		if (state->finger >= 0)
			continue;
		if (!lrg_touch_button_hit (state->button, self->screen_width, self->screen_height,
		                           ppd, point->x, point->y))
			continue;
		state->finger = point->id;
		state->down = TRUE;
		if (lrg_touch_button_get_mode (state->button) == LRG_TOUCH_BUTTON_HOLD)
		{
			state->held = resolve_emu (self, state->button);
			emu_press (self, &state->held);
			state->pressed = TRUE;
			g_signal_emit (self, signals[SIGNAL_BUTTON_PRESSED], 0,
			               lrg_touch_button_get_id (state->button));
		}
		return OWNER_BUTTON;
	}

	if (self->stick_enabled && !self->stick_active &&
	    in_region (self, &self->stick_region, point->x, point->y))
	{
		self->stick_active = TRUE;
		self->stick_finger = point->id;
		if (self->stick_floating)
		{
			self->stick_origin_x = point->x;
			self->stick_origin_y = point->y;
		}
		else
		{
			self->stick_origin_x = (self->stick_region.x + self->stick_region.w * 0.5f) * self->screen_width;
			self->stick_origin_y = (self->stick_region.y + self->stick_region.h * 0.5f) * self->screen_height;
		}
		self->stick_finger_x = point->x;
		self->stick_finger_y = point->y;
		return OWNER_STICK;
	}
	if (self->look_enabled && in_region (self, &self->look_region, point->x, point->y))
		return OWNER_LOOK;
	return OWNER_FREE;
}

/* move_stick: the axes from the finger's offset: clamped to the radius,
 * 0 inside the deadzone, rescaled so the deadzone's edge is 0 */
static void
move_stick (LrgTouchControls *self,
            gfloat            x,
            gfloat            y)
{
	gfloat radius = MAX (self->stick_radius * px_per_dp (self), 1.0f);
	gfloat nx = (x - self->stick_origin_x) / radius;
	gfloat ny = (y - self->stick_origin_y) / radius;
	gfloat len = sqrtf (nx * nx + ny * ny);
	gfloat out;

	self->stick_finger_x = x;
	self->stick_finger_y = y;
	if (len <= self->stick_deadzone || len <= 0.0f)
	{
		self->stick_x = 0.0f;
		self->stick_y = 0.0f;
		self->running = FALSE;
		return;
	}
	out = (MIN (len, 1.0f) - self->stick_deadzone) / (1.0f - self->stick_deadzone);
	self->stick_x = nx / len * out;
	self->stick_y = ny / len * out;
	self->running = out >= self->stick_run_threshold;
}

/* set_visible: the one place visibility changes */
static void
set_visible (LrgTouchControls *self,
             gboolean          visible)
{
	if (self->visible == visible)
		return;
	if (!visible)
		release_all (self);
	self->visible = visible;
	update_mouse_source (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VISIBLE]);
}

/* lrg_touch_controls_feed: documented in lrg-touch-controls.h */
void
lrg_touch_controls_feed (LrgTouchControls    *self,
                         const LrgTouchPoint *points,
                         guint                n_points,
                         gdouble              time)
{
	g_autoptr(GArray) look_points = NULL;
	g_autoptr(GArray) free_points = NULL;
	guint i;

	g_return_if_fail (LRG_IS_TOUCH_CONTROLS (self));
	g_return_if_fail (points != NULL || n_points == 0);

	/* 1. last frame's clicks end; per-frame output starts over */
	for (i = 0; i < self->pending_release->len; i++)
		emu_release (self, &g_array_index (self->pending_release, Emu, i));
	g_array_set_size (self->pending_release, 0);
	for (i = 0; i < self->buttons->len; i++)
	{
		ButtonState *state = g_ptr_array_index (self->buttons, i);

		state->pressed = FALSE;
		state->released = FALSE;
	}
	self->look_dx = 0.0f;
	self->look_dy = 0.0f;
	self->zoom = 1.0f;

	/* 2. showing */
	if (n_points > 0)
		self->other_input_since = -1.0;
	switch (self->mode)
	{
	case LRG_TOUCH_CONTROLS_HIDDEN:
		set_visible (self, FALSE);
		break;
	case LRG_TOUCH_CONTROLS_SHOWN:
		set_visible (self, TRUE);
		break;
	case LRG_TOUCH_CONTROLS_AUTO:
	default:
		if (n_points > 0)
			set_visible (self, TRUE);
		else if (self->other_input_since >= 0.0 &&
		         time - self->other_input_since >= self->auto_hide_delay)
			set_visible (self, FALSE);
		break;
	}
	if (!self->visible)
	{
		write_axes (self);
		return;
	}

	/* 3. ownership: who is still down, who is new */
	for (i = 0; i < self->owned->len; i++)
		g_array_index (self->owned, Owned, i).seen = FALSE;
	look_points = g_array_new (FALSE, FALSE, sizeof (LrgTouchPoint));
	free_points = g_array_new (FALSE, FALSE, sizeof (LrgTouchPoint));
	for (i = 0; i < n_points; i++)
	{
		Owned *o = find_owned (self, points[i].id);

		if (o == NULL)
		{
			Owned no;

			no.id = points[i].id;
			no.kind = claim (self, &points[i]);
			no.seen = TRUE;
			no.x = points[i].x;
			no.y = points[i].y;
			g_array_append_val (self->owned, no);
			o = &g_array_index (self->owned, Owned, self->owned->len - 1);
		}
		else if (o->seen)
		{
			continue;   /* the same id twice: the first counts */
		}
		o->seen = TRUE;
		o->x = points[i].x;
		o->y = points[i].y;
		if (o->kind == OWNER_STICK)
			move_stick (self, o->x, o->y);
		else if (o->kind == OWNER_LOOK)
			g_array_append_val (look_points, points[i]);
		else if (o->kind == OWNER_FREE)
			g_array_append_val (free_points, points[i]);
	}

	/* 4. lifts: buttons and the stick let go */
	for (i = 0; i < self->owned->len; )
	{
		Owned *o = &g_array_index (self->owned, Owned, i);

		if (o->seen)
		{
			i++;
			continue;
		}
		if (o->kind == OWNER_BUTTON)
		{
			guint b;

			for (b = 0; b < self->buttons->len; b++)
			{
				ButtonState *state = g_ptr_array_index (self->buttons, b);

				if (state->finger == o->id)
				{
					gboolean inside = lrg_touch_button_hit (state->button, self->screen_width,
					                                        self->screen_height, px_per_dp (self),
					                                        o->x, o->y);

					release_button (self, state, inside);
					break;
				}
			}
		}
		else if (o->kind == OWNER_STICK && self->stick_finger == o->id)
		{
			release_stick (self);
		}
		g_array_remove_index (self->owned, i);
	}

	/* 5. the look and pointer fingers' gestures, and the sink's mouse
	 * following the first pointer finger */
	lrg_touch_tracker_feed (self->tracker, (const LrgTouchPoint *) look_points->data,
	                        look_points->len, time);
	lrg_touch_tracker_feed (self->pointer_tracker, (const LrgTouchPoint *) free_points->data,
	                        free_points->len, time);
	if (self->pointer_emulation && self->sink != NULL && free_points->len > 0)
	{
		const LrgTouchPoint *first = &g_array_index (free_points, LrgTouchPoint, 0);

		lrg_input_software_move_mouse_to (self->sink, first->x, first->y);
	}

	write_axes (self);
}

/* lrg_touch_controls_note_other_input: documented in lrg-touch-controls.h */
void
lrg_touch_controls_note_other_input (LrgTouchControls *self,
                                     gdouble           time)
{
	g_return_if_fail (LRG_IS_TOUCH_CONTROLS (self));

	if (self->other_input_since < 0.0)
		self->other_input_since = time;
}

/* device_other_input: a key, a mouse button or movement (without
 * touches this frame and last), or a gamepad in use */
static gboolean
device_other_input (LrgTouchControls *self,
                    gboolean          touched)
{
	gint key;
	gint pad;

	for (key = (gint) GRL_KEY_SPACE; key <= (gint) GRL_KEY_KB_MENU; key++)
		if (grl_input_is_key_down ((GrlKey) key))
			return TRUE;
	if (!touched && !self->prev_touched)
	{
		g_autoptr(GrlVector2) delta = grl_input_get_mouse_delta ();

		if (grl_input_is_mouse_button_down (GRL_MOUSE_BUTTON_LEFT) ||
		    grl_input_is_mouse_button_down (GRL_MOUSE_BUTTON_RIGHT) ||
		    (delta != NULL && (delta->x != 0.0f || delta->y != 0.0f)) ||
		    grl_input_get_mouse_wheel_move () != 0.0f)
			return TRUE;
	}
	for (pad = 0; pad < 4; pad++)
	{
		gint axis;
		gint button;

		if (!grl_input_is_gamepad_available (pad))
			continue;
		for (button = 1; button <= (gint) GRL_GAMEPAD_BUTTON_RIGHT_THUMB; button++)
			if (grl_input_is_gamepad_button_down (pad, (GrlGamepadButton) button))
				return TRUE;
		for (axis = 0; axis < 4; axis++)
			if (fabsf (grl_input_get_gamepad_axis_movement (pad, (GrlGamepadAxis) axis)) > 0.5f)
				return TRUE;
	}
	return FALSE;
}

/* lrg_touch_controls_update: documented in lrg-touch-controls.h */
void
lrg_touch_controls_update (LrgTouchControls *self,
                           gdouble           time)
{
	LrgTouchPoint points[MAX_DEVICE_POINTS];
	gint count;
	gint i;

	g_return_if_fail (LRG_IS_TOUCH_CONTROLS (self));

	lrg_touch_controls_set_screen_size (self, (gfloat) GetScreenWidth (), (gfloat) GetScreenHeight ());
	count = CLAMP (grl_input_get_touch_point_count (), 0, MAX_DEVICE_POINTS);
	for (i = 0; i < count; i++)
	{
		g_autoptr(GrlVector2) position = grl_input_get_touch_position (i);

		points[i].id = grl_input_get_touch_point_id (i);
		points[i].x = position != NULL ? position->x : 0.0f;
		points[i].y = position != NULL ? position->y : 0.0f;
	}
	if (device_other_input (self, count > 0))
		lrg_touch_controls_note_other_input (self, time);
	self->prev_touched = count > 0;
	lrg_touch_controls_feed (self, points, (guint) count, time);
}

/* lrg_touch_controls_set_screen_size: documented in lrg-touch-controls.h */
void
lrg_touch_controls_set_screen_size (LrgTouchControls *self,
                                    gfloat            width,
                                    gfloat            height)
{
	g_return_if_fail (LRG_IS_TOUCH_CONTROLS (self));

	self->screen_width = MAX (width, 1.0f);
	self->screen_height = MAX (height, 1.0f);
}

/* lrg_touch_controls_is_visible: documented in lrg-touch-controls.h */
gboolean
lrg_touch_controls_is_visible (LrgTouchControls *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), FALSE);

	return self->visible;
}

/* ==========================================================================
 * Buttons
 * ========================================================================== */

/* lrg_touch_controls_add_button: documented in lrg-touch-controls.h */
void
lrg_touch_controls_add_button (LrgTouchControls *self,
                               LrgTouchButton   *button)
{
	ButtonState *state;

	g_return_if_fail (LRG_IS_TOUCH_CONTROLS (self));
	g_return_if_fail (LRG_IS_TOUCH_BUTTON (button));

	lrg_touch_controls_remove_button (self, lrg_touch_button_get_id (button));
	state = g_new0 (ButtonState, 1);
	state->button = g_object_ref (button);
	state->finger = -1;
	g_ptr_array_add (self->buttons, state);
}

/* lrg_touch_controls_remove_button: documented in lrg-touch-controls.h */
gboolean
lrg_touch_controls_remove_button (LrgTouchControls *self,
                                  const gchar      *id)
{
	ButtonState *state;
	guint index = 0;

	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	state = find_button (self, id, &index);
	if (state == NULL)
		return FALSE;
	if (state->finger >= 0)
	{
		Owned *o = find_owned (self, state->finger);

		/* the finger stays claimed until it lifts, by nothing */
		if (o != NULL)
			o->kind = OWNER_NONE;
		if (lrg_touch_button_get_mode (state->button) == LRG_TOUCH_BUTTON_HOLD)
			release_button (self, state, FALSE);
	}
	g_ptr_array_remove_index (self->buttons, index);
	return TRUE;
}

/* lrg_touch_controls_get_button: documented in lrg-touch-controls.h */
LrgTouchButton *
lrg_touch_controls_get_button (LrgTouchControls *self,
                               const gchar      *id)
{
	ButtonState *state;

	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);

	state = find_button (self, id, NULL);
	return state != NULL ? state->button : NULL;
}

/* lrg_touch_controls_get_n_buttons: documented in lrg-touch-controls.h */
guint
lrg_touch_controls_get_n_buttons (LrgTouchControls *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), 0);

	return self->buttons->len;
}

/* lrg_touch_controls_get_nth_button: documented in lrg-touch-controls.h */
LrgTouchButton *
lrg_touch_controls_get_nth_button (LrgTouchControls *self,
                                   guint             index)
{
	ButtonState *state;

	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), NULL);
	g_return_val_if_fail (index < self->buttons->len, NULL);

	state = g_ptr_array_index (self->buttons, index);
	return state->button;
}

/* lrg_touch_controls_get_tracker: documented in lrg-touch-controls.h */
LrgTouchTracker *
lrg_touch_controls_get_tracker (LrgTouchControls *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), NULL);

	return self->tracker;
}

/* ==========================================================================
 * Output
 * ========================================================================== */

/* lrg_touch_controls_get_stick: documented in lrg-touch-controls.h */
void
lrg_touch_controls_get_stick (LrgTouchControls *self,
                              gfloat           *x,
                              gfloat           *y)
{
	g_return_if_fail (LRG_IS_TOUCH_CONTROLS (self));

	if (x != NULL)
		*x = self->stick_x;
	if (y != NULL)
		*y = self->stick_y;
}

/* lrg_touch_controls_is_stick_active: documented in lrg-touch-controls.h */
gboolean
lrg_touch_controls_is_stick_active (LrgTouchControls *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), FALSE);

	return self->stick_active;
}

/* lrg_touch_controls_is_running: documented in lrg-touch-controls.h */
gboolean
lrg_touch_controls_is_running (LrgTouchControls *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), FALSE);

	return self->running;
}

/* lrg_touch_controls_get_look_delta: documented in lrg-touch-controls.h */
void
lrg_touch_controls_get_look_delta (LrgTouchControls *self,
                                   gfloat           *dx,
                                   gfloat           *dy)
{
	g_return_if_fail (LRG_IS_TOUCH_CONTROLS (self));

	if (dx != NULL)
		*dx = self->look_dx;
	if (dy != NULL)
		*dy = self->look_dy;
}

/* lrg_touch_controls_get_zoom_delta: documented in lrg-touch-controls.h */
gfloat
lrg_touch_controls_get_zoom_delta (LrgTouchControls *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), 1.0f);

	return self->zoom;
}

/* lrg_touch_controls_is_button_down: documented in lrg-touch-controls.h */
gboolean
lrg_touch_controls_is_button_down (LrgTouchControls *self,
                                   const gchar      *id)
{
	ButtonState *state;

	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	state = find_button (self, id, NULL);
	return state != NULL && state->down;
}

/* lrg_touch_controls_is_button_pressed: documented in lrg-touch-controls.h */
gboolean
lrg_touch_controls_is_button_pressed (LrgTouchControls *self,
                                      const gchar      *id)
{
	ButtonState *state;

	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	state = find_button (self, id, NULL);
	return state != NULL && state->pressed;
}

/* lrg_touch_controls_is_button_released: documented in lrg-touch-controls.h */
gboolean
lrg_touch_controls_is_button_released (LrgTouchControls *self,
                                       const gchar      *id)
{
	ButtonState *state;

	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	state = find_button (self, id, NULL);
	return state != NULL && state->released;
}

/* ==========================================================================
 * Drawing
 * ========================================================================== */

/* faded: @color with its alpha times the opacity (and @factor) */
static GrlColor
faded (LrgTouchControls *self,
       const GrlColor   *color,
       gfloat            factor)
{
	GrlColor out = *color;

	out.a = (guint8) CLAMP ((gfloat) color->a * self->opacity * factor, 0.0f, 255.0f);
	return out;
}

/* whether GL's depth test is on now (rlgl keeps no copy of it); off
 * without the entry point (no context) */
typedef unsigned char (*IsEnabledFunc) (unsigned int cap);

static gboolean
depth_test_enabled (void)
{
	static IsEnabledFunc is_enabled = NULL;
	static gboolean loaded = FALSE;

	if (!loaded)
	{
		loaded = TRUE;
		is_enabled = (IsEnabledFunc) (gsize) rlGetProcAddress ("glIsEnabled");
	}
	/* GL_DEPTH_TEST */
	return is_enabled != NULL && is_enabled (0x0B71) != 0;
}

/* lrg_touch_controls_draw: documented in lrg-touch-controls.h */
void
lrg_touch_controls_draw (LrgTouchControls *self)
{
	gboolean depth_was_on;
	gfloat ppd;
	guint i;

	g_return_if_fail (LRG_IS_TOUCH_CONTROLS (self));

	if (!self->visible)
		return;
	ppd = px_per_dp (self);

	/* An overlay: never depth-tested.  A game that draws its world and
	 * then its 2D layer without turning the depth test off (raylib's 2D
	 * mode leaves it alone) would hide every control over anything
	 * nearer than the far plane; the state is put back afterwards. */
	depth_was_on = depth_test_enabled ();
	rlDrawRenderBatchActive ();
	rlDisableDepthTest ();

	/* the stick: where it is held, or a faint hint where it rests */
	if (self->stick_enabled)
	{
		gfloat radius = self->stick_radius * ppd;
		gfloat cx, cy, kx, ky, factor;
		GrlColor base, knob;

		if (self->stick_active)
		{
			gfloat dx, dy, len;

			cx = self->stick_origin_x;
			cy = self->stick_origin_y;
			dx = self->stick_finger_x - cx;
			dy = self->stick_finger_y - cy;
			len = sqrtf (dx * dx + dy * dy);
			if (len > radius)
			{
				dx = dx / len * radius;
				dy = dy / len * radius;
			}
			kx = cx + dx;
			ky = cy + dy;
			factor = 1.0f;
		}
		else
		{
			cx = (self->stick_region.x + self->stick_region.w * 0.5f) * self->screen_width;
			cy = (self->stick_region.y + self->stick_region.h * 0.5f) * self->screen_height;
			kx = cx;
			ky = cy;
			factor = 0.5f;
		}
		base = faded (self, &self->base_color, factor);
		knob = faded (self, &self->knob_color, factor);
		grl_draw_circle ((gint) cx, (gint) cy, radius, &base);
		grl_draw_circle_lines ((gint) cx, (gint) cy, radius, &knob);
		grl_draw_circle ((gint) kx, (gint) ky, radius * 0.45f, &knob);
	}

	/* the buttons, bottom first, with their labels centred */
	for (i = 0; i < self->buttons->len; i++)
	{
		ButtonState *state = g_ptr_array_index (self->buttons, i);
		LrgTouchButtonShape shape = LRG_TOUCH_BUTTON_CIRCLE;
		const gchar *text;
		GrlColor fill, ink;
		gfloat x, y, w, h;
		gint font;

		lrg_touch_button_get_rect (state->button, self->screen_width, self->screen_height,
		                           ppd, &x, &y, &w, &h);
		g_object_get (state->button, "shape", &shape, NULL);
		fill = faded (self, state->down ? &self->button_active_color : &self->button_color, 1.0f);
		ink = faded (self, &self->text_color, 1.0f);
		if (shape == LRG_TOUCH_BUTTON_CIRCLE)
		{
			grl_draw_circle ((gint) (x + w * 0.5f), (gint) (y + h * 0.5f), MIN (w, h) * 0.5f, &fill);
		}
		else
		{
			GrlRectangle rect;

			rect.x = x;
			rect.y = y;
			rect.width = w;
			rect.height = h;
			grl_draw_rectangle_rounded (&rect, 0.35f, 8, &fill);
		}

		text = lrg_touch_button_get_label (state->button);
		if (text == NULL || text[0] == '\0')
			text = lrg_touch_button_get_icon (state->button);
		if (text == NULL || text[0] == '\0')
			continue;
		font = MAX ((gint) (self->font_size * ppd), 6);
		grl_draw_text (text, (gint) (x + w * 0.5f) - grl_measure_text (text, font) / 2,
		               (gint) (y + h * 0.5f) - font / 2, font, &ink);
	}

	/* the depth test as the game had it */
	rlDrawRenderBatchActive ();
	if (depth_was_on)
		rlEnableDepthTest ();
}

/* ==========================================================================
 * Layout YAML
 * ========================================================================== */

typedef struct
{
	gboolean          has_scale;
	gfloat            scale;
	gboolean          has_opacity;
	gfloat            opacity;
	gboolean          stick_enabled;
	Region            stick_region;
	gfloat            stick_radius;
	gfloat            stick_deadzone;
	gfloat            stick_run_threshold;
	gboolean          stick_floating;
	LrgTouchStickSide stick_side;
	gboolean          look_enabled;
	Region            look_region;
	gfloat            look_sensitivity;
	gfloat            look_axis_gain;
	GPtrArray        *buttons;   /* LrgTouchButton * */
} Layout;

static void
layout_clear (Layout *layout)
{
	g_clear_pointer (&layout->buttons, g_ptr_array_unref);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (Layout, layout_clear)

static gboolean
invalid (GError      **error,
         const gchar  *format,
         ...) G_GNUC_PRINTF (2, 3);

static gboolean
invalid (GError      **error,
         const gchar  *format,
         ...)
{
	va_list args;
	g_autofree gchar *message = NULL;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error_literal (error, LRG_TOUCH_CONTROLS_ERROR, LRG_TOUCH_CONTROLS_ERROR_INVALID, message);
	return FALSE;
}

/* scalar: a node's scalar text, or an error naming @where */
static const gchar *
scalar (YamlNode     *node,
        const gchar  *where,
        GError      **error)
{
	const gchar *text;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR ||
	    (text = yaml_node_get_scalar (node)) == NULL)
	{
		invalid (error, "%s: expected a value", where);
		return NULL;
	}
	return text;
}

static gboolean
parse_number (YamlNode     *node,
              const gchar  *where,
              gfloat        min,
              gfloat        max,
              gfloat       *out,
              GError      **error)
{
	const gchar *text = scalar (node, where, error);
	gchar *end = NULL;
	gdouble value;

	if (text == NULL)
		return FALSE;
	value = g_ascii_strtod (text, &end);
	if (end == text || *end != '\0' || !isfinite (value))
		return invalid (error, "%s: \"%s\" is not a number", where, text);
	if (value < min || value > max)
		return invalid (error, "%s: %g is outside %g..%g", where, value, (gdouble) min, (gdouble) max);
	*out = (gfloat) value;
	return TRUE;
}

static gboolean
parse_bool (YamlNode     *node,
            const gchar  *where,
            gboolean     *out,
            GError      **error)
{
	const gchar *text = scalar (node, where, error);

	if (text == NULL)
		return FALSE;
	if (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0)
		*out = TRUE;
	else if (g_ascii_strcasecmp (text, "false") == 0 || g_ascii_strcasecmp (text, "no") == 0)
		*out = FALSE;
	else
		return invalid (error, "%s: \"%s\" is not true or false", where, text);
	return TRUE;
}

static gboolean
parse_enum (YamlNode     *node,
            const gchar  *where,
            GType         type,
            gint         *out,
            GError      **error)
{
	const gchar *text = scalar (node, where, error);
	g_autoptr(GEnumClass) klass = NULL;
	GEnumValue *value;

	if (text == NULL)
		return FALSE;
	klass = g_type_class_ref (type);
	value = g_enum_get_value_by_nick (klass, text);
	if (value == NULL)
		return invalid (error, "%s: unknown value \"%s\"", where, text);
	*out = value->value;
	return TRUE;
}

static gboolean
parse_string (YamlNode     *node,
              const gchar  *where,
              gchar       **out,
              GError      **error)
{
	const gchar *text = scalar (node, where, error);

	if (text == NULL)
		return FALSE;
	g_free (*out);
	*out = g_strdup (text);
	return TRUE;
}

/* parse_numbers: a sequence of exactly @n numbers in min..max */
static gboolean
parse_numbers (YamlNode     *node,
               const gchar  *where,
               guint         n,
               gfloat        min,
               gfloat        max,
               gfloat       *out,
               GError      **error)
{
	YamlSequence *seq;
	guint i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE ||
	    (seq = yaml_node_get_sequence (node)) == NULL || yaml_sequence_get_length (seq) != n)
		return invalid (error, "%s: expected a list of %u numbers", where, n);
	for (i = 0; i < n; i++)
		if (!parse_number (yaml_sequence_get_element (seq, i), where, min, max, &out[i], error))
			return FALSE;
	return TRUE;
}

static gboolean
parse_region (YamlNode     *node,
              const gchar  *where,
              Region       *out,
              GError      **error)
{
	gfloat v[4];

	if (!parse_numbers (node, where, 4, 0.0f, 1.0f, v, error))
		return FALSE;
	if (v[2] <= 0.0f || v[3] <= 0.0f || v[0] + v[2] > 1.0001f || v[1] + v[3] > 1.0001f)
		return invalid (error, "%s: [x, y, width, height] must have a size and fit in 0..1", where);
	out->x = v[0];
	out->y = v[1];
	out->w = v[2];
	out->h = v[3];
	return TRUE;
}

/* mapping_of: a node that must be a mapping */
static YamlMapping *
mapping_of (YamlNode     *node,
            const gchar  *where,
            GError      **error)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		invalid (error, "%s: expected a mapping", where);
		return NULL;
	}
	return yaml_node_get_mapping (node);
}

static gboolean
parse_stick (YamlMapping  *map,
             Layout       *layout,
             GError      **error)
{
	g_autoptr(GList) keys = yaml_mapping_get_members (map);
	GList *l;

	layout->stick_enabled = TRUE;
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode *value = yaml_mapping_get_member (map, key);
		gboolean ok;
		gint side = 0;

		if (g_str_equal (key, "region"))
			ok = parse_region (value, "stick.region", &layout->stick_region, error);
		else if (g_str_equal (key, "radius"))
			ok = parse_number (value, "stick.radius", 1.0f, 10000.0f, &layout->stick_radius, error);
		else if (g_str_equal (key, "deadzone"))
			ok = parse_number (value, "stick.deadzone", 0.0f, 0.95f, &layout->stick_deadzone, error);
		else if (g_str_equal (key, "run-threshold"))
			ok = parse_number (value, "stick.run-threshold", 0.0f, 1.0f, &layout->stick_run_threshold, error);
		else if (g_str_equal (key, "floating"))
			ok = parse_bool (value, "stick.floating", &layout->stick_floating, error);
		else if (g_str_equal (key, "side"))
		{
			ok = parse_enum (value, "stick.side", LRG_TYPE_TOUCH_STICK_SIDE, &side, error);
			layout->stick_side = (LrgTouchStickSide) side;
		}
		else
			ok = invalid (error, "stick: unknown key \"%s\"", key);
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

static gboolean
parse_look (YamlMapping  *map,
            Layout       *layout,
            GError      **error)
{
	g_autoptr(GList) keys = yaml_mapping_get_members (map);
	GList *l;

	layout->look_enabled = TRUE;
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode *value = yaml_mapping_get_member (map, key);
		gboolean ok;

		if (g_str_equal (key, "region"))
			ok = parse_region (value, "look.region", &layout->look_region, error);
		else if (g_str_equal (key, "sensitivity"))
			ok = parse_number (value, "look.sensitivity", 0.0f, 100.0f, &layout->look_sensitivity, error);
		else if (g_str_equal (key, "axis-gain"))
			ok = parse_number (value, "look.axis-gain", 0.0f, 100.0f, &layout->look_axis_gain, error);
		else
			ok = invalid (error, "look: unknown key \"%s\"", key);
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

/* parse_button: one entry of buttons:, into a new (unattached) button */
static LrgTouchButton *
parse_button (YamlNode     *node,
              guint         index,
              GError      **error)
{
	g_autofree gchar *where = g_strdup_printf ("buttons[%u]", index);
	g_autofree gchar *id = NULL;
	g_autofree gchar *action = NULL;
	g_autofree gchar *label = NULL;
	g_autofree gchar *icon = NULL;
	g_autoptr(GList) keys = NULL;
	g_autoptr(LrgTouchButton) button = NULL;
	YamlMapping *map;
	gint anchor = LRG_TOUCH_ANCHOR_BOTTOM_RIGHT;
	gint mode = LRG_TOUCH_BUTTON_HOLD;
	gint shape = LRG_TOUCH_BUTTON_CIRCLE;
	gint key_code = GRL_KEY_NULL;
	gint pad = GRL_GAMEPAD_BUTTON_UNKNOWN;
	gfloat offset[2] = { -80.0f, -80.0f };
	gfloat size[2] = { 64.0f, 64.0f };
	GList *l;

	map = mapping_of (node, where, error);
	if (map == NULL)
		return NULL;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode *value = yaml_mapping_get_member (map, key);
		g_autofree gchar *field = g_strdup_printf ("%s.%s", where, key);
		gboolean ok;

		if (g_str_equal (key, "id"))
			ok = parse_string (value, field, &id, error);
		else if (g_str_equal (key, "action"))
			ok = parse_string (value, field, &action, error);
		else if (g_str_equal (key, "label"))
			ok = parse_string (value, field, &label, error);
		else if (g_str_equal (key, "icon"))
			ok = parse_string (value, field, &icon, error);
		else if (g_str_equal (key, "anchor"))
			ok = parse_enum (value, field, LRG_TYPE_TOUCH_ANCHOR, &anchor, error);
		else if (g_str_equal (key, "mode"))
			ok = parse_enum (value, field, LRG_TYPE_TOUCH_BUTTON_MODE, &mode, error);
		else if (g_str_equal (key, "shape"))
			ok = parse_enum (value, field, LRG_TYPE_TOUCH_BUTTON_SHAPE, &shape, error);
		else if (g_str_equal (key, "key"))
			ok = parse_enum (value, field, GRL_TYPE_KEY, &key_code, error);
		else if (g_str_equal (key, "gamepad-button"))
			ok = parse_enum (value, field, GRL_TYPE_GAMEPAD_BUTTON, &pad, error);
		else if (g_str_equal (key, "offset"))
			ok = parse_numbers (value, field, 2, -100000.0f, 100000.0f, offset, error);
		else if (g_str_equal (key, "size"))
			ok = parse_numbers (value, field, 2, 1.0f, 100000.0f, size, error);
		else
			ok = invalid (error, "%s: unknown key \"%s\"", where, key);
		if (!ok)
			return NULL;
	}
	if (id == NULL || id[0] == '\0')
	{
		invalid (error, "%s: needs an id", where);
		return NULL;
	}

	button = lrg_touch_button_new (id);
	g_object_set (button,
	              "action", action != NULL ? action : id,
	              "label", label,
	              "icon", icon,
	              "mode", mode,
	              "shape", shape,
	              "key", key_code,
	              "gamepad-button", pad,
	              NULL);
	lrg_touch_button_set_placement (button, (LrgTouchAnchor) anchor, offset[0], offset[1], size[0], size[1]);
	return g_steal_pointer (&button);
}

/* parse_layout: the whole document into @layout, or an error and nothing */
static gboolean
parse_layout (YamlNode     *root,
              Layout       *layout,
              GError      **error)
{
	g_autoptr(GList) keys = NULL;
	YamlMapping *map;
	GList *l;

	map = mapping_of (root, "layout", error);
	if (map == NULL)
		return FALSE;
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode *value = yaml_mapping_get_member (map, key);
		gboolean ok = TRUE;

		if (g_str_equal (key, "scale"))
		{
			layout->has_scale = TRUE;
			ok = parse_number (value, "scale", 0.25f, 4.0f, &layout->scale, error);
		}
		else if (g_str_equal (key, "opacity"))
		{
			layout->has_opacity = TRUE;
			ok = parse_number (value, "opacity", 0.0f, 1.0f, &layout->opacity, error);
		}
		else if (g_str_equal (key, "stick"))
		{
			YamlMapping *section = mapping_of (value, "stick", error);

			ok = section != NULL && parse_stick (section, layout, error);
		}
		else if (g_str_equal (key, "look"))
		{
			YamlMapping *section = mapping_of (value, "look", error);

			ok = section != NULL && parse_look (section, layout, error);
		}
		else if (g_str_equal (key, "buttons"))
		{
			YamlSequence *seq;
			guint i;

			if (value == NULL || yaml_node_get_node_type (value) != YAML_NODE_SEQUENCE ||
			    (seq = yaml_node_get_sequence (value)) == NULL)
				return invalid (error, "buttons: expected a list");
			for (i = 0; i < yaml_sequence_get_length (seq); i++)
			{
				LrgTouchButton *button = parse_button (yaml_sequence_get_element (seq, i), i, error);
				guint j;

				if (button == NULL)
					return FALSE;
				for (j = 0; j < layout->buttons->len; j++)
				{
					if (g_str_equal (lrg_touch_button_get_id (g_ptr_array_index (layout->buttons, j)),
					                 lrg_touch_button_get_id (button)))
					{
						invalid (error, "buttons[%u]: id \"%s\" is used twice", i,
						         lrg_touch_button_get_id (button));
						g_object_unref (button);
						return FALSE;
					}
				}
				g_ptr_array_add (layout->buttons, button);
			}
		}
		else
		{
			ok = invalid (error, "unknown key \"%s\"", key);
		}
		if (!ok)
			return FALSE;
	}
	return TRUE;
}

/* lrg_touch_controls_load_from_data: documented in lrg-touch-controls.h */
gboolean
lrg_touch_controls_load_from_data (LrgTouchControls  *self,
                                   const gchar       *data,
                                   gssize             length,
                                   GError           **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_auto(Layout) layout = { 0 };
	g_autoptr(GError) local = NULL;
	YamlDocument *doc;
	YamlNode *root;
	guint i;

	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), FALSE);
	g_return_val_if_fail (data != NULL || length == 0, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* 1. YAML */
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data != NULL ? data : "", length, &local))
	{
		g_set_error (error, LRG_TOUCH_CONTROLS_ERROR, LRG_TOUCH_CONTROLS_ERROR_PARSE,
		             "Touch layout: %s", local->message);
		return FALSE;
	}
	doc = yaml_parser_get_document (parser, 0);
	root = doc != NULL ? yaml_document_get_root (doc) : NULL;
	if (root == NULL)
		return invalid (error, "Touch layout: empty document");

	/* 2. stage everything, with the defaults of a fresh layout */
	layout.stick_region.x = 0.0f;
	layout.stick_region.y = 0.35f;
	layout.stick_region.w = 0.45f;
	layout.stick_region.h = 0.65f;
	layout.stick_radius = 60.0f;
	layout.stick_deadzone = 0.15f;
	layout.stick_run_threshold = 0.8f;
	layout.stick_floating = TRUE;
	layout.stick_side = LRG_TOUCH_STICK_LEFT;
	layout.look_region.x = 0.45f;
	layout.look_region.y = 0.0f;
	layout.look_region.w = 0.55f;
	layout.look_region.h = 1.0f;
	layout.look_sensitivity = 1.0f;
	layout.look_axis_gain = 0.05f;
	layout.buttons = g_ptr_array_new_with_free_func (g_object_unref);
	if (!parse_layout (root, &layout, &local))
	{
		g_propagate_prefixed_error (error, g_steal_pointer (&local), "Touch layout: ");
		return FALSE;
	}

	/* 3. apply: nothing can fail from here on */
	release_all (self);
	g_ptr_array_set_size (self->buttons, 0);
	for (i = 0; i < layout.buttons->len; i++)
		lrg_touch_controls_add_button (self, g_ptr_array_index (layout.buttons, i));
	self->stick_enabled = layout.stick_enabled;
	self->stick_region = layout.stick_region;
	self->stick_radius = layout.stick_radius;
	self->stick_deadzone = layout.stick_deadzone;
	self->stick_run_threshold = layout.stick_run_threshold;
	self->stick_floating = layout.stick_floating;
	self->stick_side = layout.stick_side;
	self->look_enabled = layout.look_enabled;
	self->look_region = layout.look_region;
	self->look_sensitivity = layout.look_sensitivity;
	self->look_axis_gain = layout.look_axis_gain;
	if (layout.has_scale)
		self->scale = layout.scale;
	if (layout.has_opacity)
		self->opacity = layout.opacity;
	g_object_notify (G_OBJECT (self), "stick-enabled");
	g_object_notify (G_OBJECT (self), "look-enabled");
	return TRUE;
}

/* lrg_touch_controls_load_from_file: documented in lrg-touch-controls.h */
gboolean
lrg_touch_controls_load_from_file (LrgTouchControls  *self,
                                   const gchar       *path,
                                   GError           **error)
{
	g_autofree gchar *contents = NULL;
	gsize length = 0;

	g_return_val_if_fail (LRG_IS_TOUCH_CONTROLS (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (!g_file_get_contents (path, &contents, &length, error))
		return FALSE;
	return lrg_touch_controls_load_from_data (self, contents, (gssize) length, error);
}
