/* lrg-input-gamepad.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gamepad input source - wraps graylib gamepad functions with
 * controller type detection, display names, and dead zone support.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INPUT

#include "lrg-input-gamepad.h"
#include "../lrg-log.h"

#include <math.h>
#include <string.h>

/* Default dead zone threshold */
#define DEFAULT_DEAD_ZONE (0.1f)

/**
 * LrgInputGamepad:
 *
 * Gamepad input source with controller detection and dead zone support.
 *
 * This class provides gamepad input by wrapping graylib's gamepad
 * functions. It adds controller type detection for proper button
 * name display, and configurable dead zones for analog inputs.
 */
struct _LrgInputGamepad
{
	LrgInput parent_instance;

	gfloat dead_zone;
};

G_DEFINE_FINAL_TYPE (LrgInputGamepad, lrg_input_gamepad, LRG_TYPE_INPUT)

enum {
	PROP_0,
	PROP_DEAD_ZONE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Button Name Tables
 *
 * Each table maps GrlGamepadButton values to display strings.
 * Index order matches GrlGamepadButton enum values.
 * ========================================================================== */

/*
 * Xbox button names (also used for Steam Deck and Generic)
 */
static const gchar *button_names_xbox[] = {
	"Unknown",      /* GRL_GAMEPAD_BUTTON_UNKNOWN */
	"DPad Up",      /* GRL_GAMEPAD_BUTTON_LEFT_FACE_UP */
	"DPad Right",   /* GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT */
	"DPad Down",    /* GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN */
	"DPad Left",    /* GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT */
	"Y",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP */
	"B",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT */
	"A",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN */
	"X",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT */
	"LB",           /* GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1 */
	"LT",           /* GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2 */
	"RB",           /* GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_1 */
	"RT",           /* GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_2 */
	"View",         /* GRL_GAMEPAD_BUTTON_MIDDLE_LEFT */
	"Guide",        /* GRL_GAMEPAD_BUTTON_MIDDLE */
	"Menu",         /* GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT */
	"LS",           /* GRL_GAMEPAD_BUTTON_LEFT_THUMB */
	"RS"            /* GRL_GAMEPAD_BUTTON_RIGHT_THUMB */
};

/*
 * PlayStation button names
 */
static const gchar *button_names_playstation[] = {
	"Unknown",      /* GRL_GAMEPAD_BUTTON_UNKNOWN */
	"DPad Up",      /* GRL_GAMEPAD_BUTTON_LEFT_FACE_UP */
	"DPad Right",   /* GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT */
	"DPad Down",    /* GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN */
	"DPad Left",    /* GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT */
	"Triangle",     /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP */
	"Circle",       /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT */
	"Cross",        /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN */
	"Square",       /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT */
	"L1",           /* GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1 */
	"L2",           /* GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2 */
	"R1",           /* GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_1 */
	"R2",           /* GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_2 */
	"Share",        /* GRL_GAMEPAD_BUTTON_MIDDLE_LEFT */
	"PS",           /* GRL_GAMEPAD_BUTTON_MIDDLE */
	"Options",      /* GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT */
	"L3",           /* GRL_GAMEPAD_BUTTON_LEFT_THUMB */
	"R3"            /* GRL_GAMEPAD_BUTTON_RIGHT_THUMB */
};

/*
 * Nintendo Switch button names
 * Note: Switch has swapped A/B and X/Y positions compared to Xbox
 */
static const gchar *button_names_switch[] = {
	"Unknown",      /* GRL_GAMEPAD_BUTTON_UNKNOWN */
	"DPad Up",      /* GRL_GAMEPAD_BUTTON_LEFT_FACE_UP */
	"DPad Right",   /* GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT */
	"DPad Down",    /* GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN */
	"DPad Left",    /* GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT */
	"X",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP */
	"A",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT */
	"B",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN */
	"Y",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT */
	"L",            /* GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1 */
	"ZL",           /* GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2 */
	"R",            /* GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_1 */
	"ZR",           /* GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_2 */
	"-",            /* GRL_GAMEPAD_BUTTON_MIDDLE_LEFT (Minus) */
	"Home",         /* GRL_GAMEPAD_BUTTON_MIDDLE */
	"+",            /* GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT (Plus) */
	"LS",           /* GRL_GAMEPAD_BUTTON_LEFT_THUMB */
	"RS"            /* GRL_GAMEPAD_BUTTON_RIGHT_THUMB */
};

/*
 * Steam Deck button names (similar to Xbox but with Steam button)
 */
static const gchar *button_names_steam_deck[] = {
	"Unknown",      /* GRL_GAMEPAD_BUTTON_UNKNOWN */
	"DPad Up",      /* GRL_GAMEPAD_BUTTON_LEFT_FACE_UP */
	"DPad Right",   /* GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT */
	"DPad Down",    /* GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN */
	"DPad Left",    /* GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT */
	"Y",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP */
	"B",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT */
	"A",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN */
	"X",            /* GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT */
	"L1",           /* GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1 */
	"L2",           /* GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2 */
	"R1",           /* GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_1 */
	"R2",           /* GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_2 */
	"View",         /* GRL_GAMEPAD_BUTTON_MIDDLE_LEFT */
	"Steam",        /* GRL_GAMEPAD_BUTTON_MIDDLE */
	"Menu",         /* GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT */
	"L3",           /* GRL_GAMEPAD_BUTTON_LEFT_THUMB */
	"R3"            /* GRL_GAMEPAD_BUTTON_RIGHT_THUMB */
};

#define BUTTON_TABLE_SIZE (G_N_ELEMENTS (button_names_xbox))

/* ==========================================================================
 * Axis Name Tables
 * ========================================================================== */

/*
 * Xbox/Steam Deck/Generic axis names
 */
static const gchar *axis_names_xbox[] = {
	"Left Stick X",   /* GRL_GAMEPAD_AXIS_LEFT_X */
	"Left Stick Y",   /* GRL_GAMEPAD_AXIS_LEFT_Y */
	"Right Stick X",  /* GRL_GAMEPAD_AXIS_RIGHT_X */
	"Right Stick Y",  /* GRL_GAMEPAD_AXIS_RIGHT_Y */
	"LT",             /* GRL_GAMEPAD_AXIS_LEFT_TRIGGER */
	"RT"              /* GRL_GAMEPAD_AXIS_RIGHT_TRIGGER */
};

/*
 * PlayStation axis names
 */
static const gchar *axis_names_playstation[] = {
	"Left Stick X",   /* GRL_GAMEPAD_AXIS_LEFT_X */
	"Left Stick Y",   /* GRL_GAMEPAD_AXIS_LEFT_Y */
	"Right Stick X",  /* GRL_GAMEPAD_AXIS_RIGHT_X */
	"Right Stick Y",  /* GRL_GAMEPAD_AXIS_RIGHT_Y */
	"L2",             /* GRL_GAMEPAD_AXIS_LEFT_TRIGGER */
	"R2"              /* GRL_GAMEPAD_AXIS_RIGHT_TRIGGER */
};

/*
 * Nintendo Switch axis names
 */
static const gchar *axis_names_switch[] = {
	"Left Stick X",   /* GRL_GAMEPAD_AXIS_LEFT_X */
	"Left Stick Y",   /* GRL_GAMEPAD_AXIS_LEFT_Y */
	"Right Stick X",  /* GRL_GAMEPAD_AXIS_RIGHT_X */
	"Right Stick Y",  /* GRL_GAMEPAD_AXIS_RIGHT_Y */
	"ZL",             /* GRL_GAMEPAD_AXIS_LEFT_TRIGGER */
	"ZR"              /* GRL_GAMEPAD_AXIS_RIGHT_TRIGGER */
};

#define AXIS_TABLE_SIZE (G_N_ELEMENTS (axis_names_xbox))

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * detect_gamepad_type_from_name:
 * @name: the controller name string from the system
 *
 * Parses the controller name to determine the controller type.
 *
 * Returns: The detected #LrgGamepadType
 */
static LrgGamepadType
detect_gamepad_type_from_name (const gchar *name)
{
	g_autofree gchar *lower = NULL;

	if (name == NULL || name[0] == '\0')
	{
		return LRG_GAMEPAD_TYPE_UNKNOWN;
	}

	/* Convert to lowercase for case-insensitive matching */
	lower = g_ascii_strdown (name, -1);

	/*
	 * Steam Deck - check first since it might appear as virtual Xbox
	 * in some configurations. "Neptune" is Valve's internal codename.
	 */
	if (g_strstr_len (lower, -1, "neptune") != NULL ||
	    g_strstr_len (lower, -1, "steam deck") != NULL ||
	    g_strstr_len (lower, -1, "steamdeck") != NULL)
	{
		return LRG_GAMEPAD_TYPE_STEAM_DECK;
	}

	/* Xbox variants */
	if (g_strstr_len (lower, -1, "xbox") != NULL ||
	    g_strstr_len (lower, -1, "x-box") != NULL ||
	    g_strstr_len (lower, -1, "xinput") != NULL ||
	    g_strstr_len (lower, -1, "microsoft") != NULL)
	{
		return LRG_GAMEPAD_TYPE_XBOX;
	}

	/* PlayStation variants */
	if (g_strstr_len (lower, -1, "playstation") != NULL ||
	    g_strstr_len (lower, -1, "ps3") != NULL ||
	    g_strstr_len (lower, -1, "ps4") != NULL ||
	    g_strstr_len (lower, -1, "ps5") != NULL ||
	    g_strstr_len (lower, -1, "dualshock") != NULL ||
	    g_strstr_len (lower, -1, "dualsense") != NULL ||
	    g_strstr_len (lower, -1, "sony") != NULL)
	{
		return LRG_GAMEPAD_TYPE_PLAYSTATION;
	}

	/* Nintendo Switch variants */
	if (g_strstr_len (lower, -1, "nintendo") != NULL ||
	    g_strstr_len (lower, -1, "switch") != NULL ||
	    g_strstr_len (lower, -1, "pro controller") != NULL ||
	    g_strstr_len (lower, -1, "joy-con") != NULL ||
	    g_strstr_len (lower, -1, "joycon") != NULL)
	{
		return LRG_GAMEPAD_TYPE_SWITCH;
	}

	return LRG_GAMEPAD_TYPE_GENERIC;
}

/*
 * get_button_name_table:
 * @type: the controller type
 *
 * Gets the button name table for a controller type.
 *
 * Returns: Pointer to the button name array
 */
static const gchar **
get_button_name_table (LrgGamepadType type)
{
	switch (type)
	{
	case LRG_GAMEPAD_TYPE_PLAYSTATION:
		return button_names_playstation;

	case LRG_GAMEPAD_TYPE_SWITCH:
		return button_names_switch;

	case LRG_GAMEPAD_TYPE_STEAM_DECK:
		return button_names_steam_deck;

	case LRG_GAMEPAD_TYPE_XBOX:
	case LRG_GAMEPAD_TYPE_GENERIC:
	case LRG_GAMEPAD_TYPE_UNKNOWN:
	default:
		return button_names_xbox;
	}
}

/*
 * get_axis_name_table:
 * @type: the controller type
 *
 * Gets the axis name table for a controller type.
 *
 * Returns: Pointer to the axis name array
 */
static const gchar **
get_axis_name_table (LrgGamepadType type)
{
	switch (type)
	{
	case LRG_GAMEPAD_TYPE_PLAYSTATION:
		return axis_names_playstation;

	case LRG_GAMEPAD_TYPE_SWITCH:
		return axis_names_switch;

	case LRG_GAMEPAD_TYPE_STEAM_DECK:
	case LRG_GAMEPAD_TYPE_XBOX:
	case LRG_GAMEPAD_TYPE_GENERIC:
	case LRG_GAMEPAD_TYPE_UNKNOWN:
	default:
		return axis_names_xbox;
	}
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static gboolean
lrg_input_gamepad_is_gamepad_available (LrgInput *self,
                                        gint      gamepad)
{
	return grl_input_is_gamepad_available (gamepad);
}

static gboolean
lrg_input_gamepad_is_gamepad_button_pressed (LrgInput         *self,
                                             gint              gamepad,
                                             GrlGamepadButton  button)
{
	return grl_input_is_gamepad_button_pressed (gamepad, button);
}

static gboolean
lrg_input_gamepad_is_gamepad_button_down (LrgInput         *self,
                                          gint              gamepad,
                                          GrlGamepadButton  button)
{
	return grl_input_is_gamepad_button_down (gamepad, button);
}

static gboolean
lrg_input_gamepad_is_gamepad_button_released (LrgInput         *self,
                                              gint              gamepad,
                                              GrlGamepadButton  button)
{
	return grl_input_is_gamepad_button_released (gamepad, button);
}

static gfloat
lrg_input_gamepad_get_gamepad_axis_impl (LrgInput       *self,
                                         gint            gamepad,
                                         GrlGamepadAxis  axis)
{
	LrgInputGamepad *gamepad_self;
	gfloat           value;
	gfloat           sign;
	gfloat           abs_value;
	gfloat           range;

	gamepad_self = LRG_INPUT_GAMEPAD (self);
	value = grl_input_get_gamepad_axis_movement (gamepad, axis);

	/* Apply dead zone with rescaling */
	abs_value = fabsf (value);

	if (abs_value < gamepad_self->dead_zone)
	{
		return 0.0f;
	}

	/*
	 * Rescale the remaining range to 0-1 to avoid a "jump"
	 * at the dead zone boundary.
	 *
	 * new_value = (abs_value - dead_zone) / (1.0 - dead_zone)
	 */
	sign = value > 0.0f ? 1.0f : -1.0f;
	range = 1.0f - gamepad_self->dead_zone;

	if (range <= 0.0f)
	{
		return 0.0f;
	}

	return sign * ((abs_value - gamepad_self->dead_zone) / range);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_input_gamepad_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgInputGamepad *self = LRG_INPUT_GAMEPAD (object);

	switch (prop_id)
	{
	case PROP_DEAD_ZONE:
		g_value_set_float (value, self->dead_zone);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_input_gamepad_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgInputGamepad *self = LRG_INPUT_GAMEPAD (object);

	switch (prop_id)
	{
	case PROP_DEAD_ZONE:
		lrg_input_gamepad_set_dead_zone (self, g_value_get_float (value));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_input_gamepad_class_init (LrgInputGamepadClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	LrgInputClass *input_class = LRG_INPUT_CLASS (klass);

	object_class->get_property = lrg_input_gamepad_get_property;
	object_class->set_property = lrg_input_gamepad_set_property;

	/* Override gamepad-related virtual methods */
	input_class->is_gamepad_available       = lrg_input_gamepad_is_gamepad_available;
	input_class->is_gamepad_button_pressed  = lrg_input_gamepad_is_gamepad_button_pressed;
	input_class->is_gamepad_button_down     = lrg_input_gamepad_is_gamepad_button_down;
	input_class->is_gamepad_button_released = lrg_input_gamepad_is_gamepad_button_released;
	input_class->get_gamepad_axis           = lrg_input_gamepad_get_gamepad_axis_impl;

	/**
	 * LrgInputGamepad:dead-zone:
	 *
	 * The dead zone threshold for analog inputs (0.0 to 1.0).
	 *
	 * Values within the dead zone are treated as 0.0. The remaining
	 * range is rescaled to avoid a jump at the boundary.
	 *
	 * Default: 0.1
	 */
	properties[PROP_DEAD_ZONE] =
		g_param_spec_float ("dead-zone",
		                    "Dead Zone",
		                    "Dead zone threshold for analog inputs",
		                    0.0f,
		                    1.0f,
		                    DEFAULT_DEAD_ZONE,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS |
		                    G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_input_gamepad_init (LrgInputGamepad *self)
{
	self->dead_zone = DEFAULT_DEAD_ZONE;

	/* Set default name for this input source */
	g_object_set (self, "name", "gamepad", NULL);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_input_gamepad_new:
 *
 * Creates a new gamepad input source with default dead zone of 0.1.
 *
 * Returns: (transfer full): A new #LrgInputGamepad
 */
LrgInput *
lrg_input_gamepad_new (void)
{
	return g_object_new (LRG_TYPE_INPUT_GAMEPAD, NULL);
}

/* ==========================================================================
 * Public API - Controller Type Detection
 * ========================================================================== */

/**
 * lrg_input_gamepad_detect_type:
 * @self: an #LrgInputGamepad
 * @gamepad: the gamepad index (0-3)
 *
 * Detects the type of controller connected at the specified index.
 *
 * Returns: The detected #LrgGamepadType
 */
LrgGamepadType
lrg_input_gamepad_detect_type (LrgInputGamepad *self,
                               gint             gamepad)
{
	const gchar *name;

	g_return_val_if_fail (LRG_IS_INPUT_GAMEPAD (self), LRG_GAMEPAD_TYPE_UNKNOWN);
	g_return_val_if_fail (gamepad >= 0 && gamepad <= 3, LRG_GAMEPAD_TYPE_UNKNOWN);

	if (!grl_input_is_gamepad_available (gamepad))
	{
		return LRG_GAMEPAD_TYPE_UNKNOWN;
	}

	name = grl_input_get_gamepad_name (gamepad);
	return detect_gamepad_type_from_name (name);
}

/**
 * lrg_input_gamepad_get_name:
 * @self: an #LrgInputGamepad
 * @gamepad: the gamepad index (0-3)
 *
 * Gets the raw name string of the connected controller.
 *
 * Returns: (transfer none) (nullable): The controller name, or %NULL
 */
const gchar *
lrg_input_gamepad_get_name (LrgInputGamepad *self,
                            gint             gamepad)
{
	g_return_val_if_fail (LRG_IS_INPUT_GAMEPAD (self), NULL);
	g_return_val_if_fail (gamepad >= 0 && gamepad <= 3, NULL);

	if (!grl_input_is_gamepad_available (gamepad))
	{
		return NULL;
	}

	return grl_input_get_gamepad_name (gamepad);
}

/* ==========================================================================
 * Public API - Display Names
 * ========================================================================== */

/**
 * lrg_input_gamepad_get_button_display_name:
 * @self: an #LrgInputGamepad
 * @gamepad: the gamepad index (0-3)
 * @button: the button to get the name for
 *
 * Gets the display name for a button based on the connected controller type.
 *
 * Returns: (transfer none): The button display name
 */
const gchar *
lrg_input_gamepad_get_button_display_name (LrgInputGamepad  *self,
                                           gint              gamepad,
                                           GrlGamepadButton  button)
{
	LrgGamepadType type;

	g_return_val_if_fail (LRG_IS_INPUT_GAMEPAD (self), "Unknown");

	type = lrg_input_gamepad_detect_type (self, gamepad);
	return lrg_input_gamepad_get_button_display_name_for_type (button, type);
}

/**
 * lrg_input_gamepad_get_button_display_name_for_type:
 * @button: the button to get the name for
 * @gamepad_type: the controller type
 *
 * Gets the display name for a button for a specific controller type.
 *
 * Returns: (transfer none): The button display name
 */
const gchar *
lrg_input_gamepad_get_button_display_name_for_type (GrlGamepadButton button,
                                                    LrgGamepadType   gamepad_type)
{
	const gchar **table;
	gint          index;

	index = (gint)button;

	if (index < 0 || index >= (gint)BUTTON_TABLE_SIZE)
	{
		return "Unknown";
	}

	table = get_button_name_table (gamepad_type);
	return table[index];
}

/**
 * lrg_input_gamepad_get_axis_display_name:
 * @self: an #LrgInputGamepad
 * @gamepad: the gamepad index (0-3)
 * @axis: the axis to get the name for
 *
 * Gets the display name for an axis based on the connected controller type.
 *
 * Returns: (transfer none): The axis display name
 */
const gchar *
lrg_input_gamepad_get_axis_display_name (LrgInputGamepad *self,
                                         gint             gamepad,
                                         GrlGamepadAxis   axis)
{
	LrgGamepadType type;

	g_return_val_if_fail (LRG_IS_INPUT_GAMEPAD (self), "Unknown");

	type = lrg_input_gamepad_detect_type (self, gamepad);
	return lrg_input_gamepad_get_axis_display_name_for_type (axis, type);
}

/**
 * lrg_input_gamepad_get_axis_display_name_for_type:
 * @axis: the axis to get the name for
 * @gamepad_type: the controller type
 *
 * Gets the display name for an axis for a specific controller type.
 *
 * Returns: (transfer none): The axis display name
 */
const gchar *
lrg_input_gamepad_get_axis_display_name_for_type (GrlGamepadAxis axis,
                                                  LrgGamepadType gamepad_type)
{
	const gchar **table;
	gint          index;

	index = (gint)axis;

	if (index < 0 || index >= (gint)AXIS_TABLE_SIZE)
	{
		return "Unknown";
	}

	table = get_axis_name_table (gamepad_type);
	return table[index];
}

/* ==========================================================================
 * Public API - Dead Zone Configuration
 * ========================================================================== */

/**
 * lrg_input_gamepad_set_dead_zone:
 * @self: an #LrgInputGamepad
 * @dead_zone: the dead zone threshold (0.0 to 1.0)
 *
 * Sets the dead zone for analog sticks and triggers.
 */
void
lrg_input_gamepad_set_dead_zone (LrgInputGamepad *self,
                                 gfloat           dead_zone)
{
	g_return_if_fail (LRG_IS_INPUT_GAMEPAD (self));

	dead_zone = CLAMP (dead_zone, 0.0f, 1.0f);

	if (self->dead_zone != dead_zone)
	{
		self->dead_zone = dead_zone;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DEAD_ZONE]);
	}
}

/**
 * lrg_input_gamepad_get_dead_zone:
 * @self: an #LrgInputGamepad
 *
 * Gets the current dead zone threshold.
 *
 * Returns: The dead zone value (0.0 to 1.0)
 */
gfloat
lrg_input_gamepad_get_dead_zone (LrgInputGamepad *self)
{
	g_return_val_if_fail (LRG_IS_INPUT_GAMEPAD (self), DEFAULT_DEAD_ZONE);

	return self->dead_zone;
}
