/* lrg-controller.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Controller families: detection, glyph names, presets and the monitor.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_INPUT

#include "lrg-controller.h"
#include "lrg-input-action.h"
#include "lrg-input-binding.h"
#include "../lrg-log.h"

#include <string.h>

/* ==========================================================================
 * Family names
 * ========================================================================== */

static const struct {
	LrgControllerFamily  family;
	const gchar         *nick;
	const gchar         *display;
	LrgGamepadType       type;
} family_info[] = {
	{ LRG_CONTROLLER_FAMILY_UNKNOWN,          "unknown",          "No controller",              LRG_GAMEPAD_TYPE_UNKNOWN },
	{ LRG_CONTROLLER_FAMILY_GENERIC,          "generic",          "Generic controller",         LRG_GAMEPAD_TYPE_GENERIC },
	{ LRG_CONTROLLER_FAMILY_XBOX_360,         "xbox-360",         "Xbox 360 controller",        LRG_GAMEPAD_TYPE_XBOX },
	{ LRG_CONTROLLER_FAMILY_XBOX_ONE,         "xbox-one",         "Xbox One controller",        LRG_GAMEPAD_TYPE_XBOX },
	{ LRG_CONTROLLER_FAMILY_XBOX_SERIES,      "xbox-series",      "Xbox Series controller",     LRG_GAMEPAD_TYPE_XBOX },
	{ LRG_CONTROLLER_FAMILY_DUALSHOCK4,       "dualshock4",       "DualShock 4",                LRG_GAMEPAD_TYPE_PLAYSTATION },
	{ LRG_CONTROLLER_FAMILY_DUALSENSE,        "dualsense",        "DualSense",                  LRG_GAMEPAD_TYPE_PLAYSTATION },
	{ LRG_CONTROLLER_FAMILY_DUALSENSE_EDGE,   "dualsense-edge",   "DualSense Edge",             LRG_GAMEPAD_TYPE_PLAYSTATION },
	{ LRG_CONTROLLER_FAMILY_SWITCH_PRO,       "switch-pro",       "Switch Pro controller",      LRG_GAMEPAD_TYPE_SWITCH },
	{ LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER, "steam-controller", "Steam Controller",           LRG_GAMEPAD_TYPE_STEAM_DECK },
	{ LRG_CONTROLLER_FAMILY_STEAM_DECK,       "steam-deck",       "Steam Deck",                 LRG_GAMEPAD_TYPE_STEAM_DECK },
	{ LRG_CONTROLLER_FAMILY_STEAM_FRAME,      "steam-frame",      "Steam Frame controllers",    LRG_GAMEPAD_TYPE_STEAM_DECK }
};

static const gint family_count = (gint)G_N_ELEMENTS (family_info);

const gchar *
lrg_controller_family_get_nick (LrgControllerFamily family)
{
	if ((gint)family < 0 || (gint)family >= family_count)
		return "unknown";

	return family_info[family].nick;
}

const gchar *
lrg_controller_family_get_display_name (LrgControllerFamily family)
{
	if ((gint)family < 0 || (gint)family >= family_count)
		return family_info[0].display;

	return family_info[family].display;
}

gboolean
lrg_controller_family_from_nick (const gchar         *nick,
                                 LrgControllerFamily *family)
{
	gint i;

	g_return_val_if_fail (family != NULL, FALSE);

	if (nick == NULL)
		return FALSE;

	for (i = 0; i < family_count; i++)
	{
		if (g_ascii_strcasecmp (nick, family_info[i].nick) == 0)
		{
			*family = family_info[i].family;
			return TRUE;
		}
	}

	return FALSE;
}

LrgGamepadType
lrg_controller_family_get_gamepad_type (LrgControllerFamily family)
{
	if ((gint)family < 0 || (gint)family >= family_count)
		return LRG_GAMEPAD_TYPE_UNKNOWN;

	return family_info[family].type;
}

gboolean
lrg_controller_family_is_steam (LrgControllerFamily family)
{
	return family == LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER ||
	       family == LRG_CONTROLLER_FAMILY_STEAM_DECK ||
	       family == LRG_CONTROLLER_FAMILY_STEAM_FRAME;
}

/* ==========================================================================
 * Detection
 * ========================================================================== */

/* the USB ids the tables know (vendor 0x045e Microsoft, 0x054c Sony,
 * 0x057e Nintendo, 0x28de Valve) */
#define VENDOR_MICROSOFT (0x045e)
#define VENDOR_SONY      (0x054c)
#define VENDOR_NINTENDO  (0x057e)

/*
 * family_from_ids:
 * @vendor: the USB vendor id
 * @product: the USB product id
 *
 * The family a known vendor/product pair belongs to, or
 * %LRG_CONTROLLER_FAMILY_UNKNOWN when the pair is not in the tables (which
 * then lets the name decide).
 */
static LrgControllerFamily
family_from_ids (guint16 vendor,
                 guint16 product)
{
	if (vendor == VENDOR_MICROSOFT)
	{
		switch (product)
		{
		/* Xbox 360 wired, wireless receivers, Chatpad models */
		case 0x028e: case 0x028f: case 0x0291: case 0x02a0:
		case 0x02a1: case 0x0719:
			return LRG_CONTROLLER_FAMILY_XBOX_360;

		/* Xbox One, One S, Elite (series 1), Adaptive */
		case 0x02d1: case 0x02dd: case 0x02e0: case 0x02e3:
		case 0x02ea: case 0x02fd: case 0x02ff: case 0x0b0a:
			return LRG_CONTROLLER_FAMILY_XBOX_ONE;

		/* Xbox Series X|S, Elite Series 2 (wired, Bluetooth, 2025 refresh) */
		case 0x0b00: case 0x0b05: case 0x0b12: case 0x0b13:
		case 0x0b20: case 0x0b21: case 0x0b22:
			return LRG_CONTROLLER_FAMILY_XBOX_SERIES;

		default:
			return LRG_CONTROLLER_FAMILY_UNKNOWN;
		}
	}

	if (vendor == VENDOR_SONY)
	{
		switch (product)
		{
		/* DualShock 3 shares the DualShock layout and labels */
		case 0x0268: case 0x05c4: case 0x09cc: case 0x0ba0:
			return LRG_CONTROLLER_FAMILY_DUALSHOCK4;

		case 0x0ce6:
			return LRG_CONTROLLER_FAMILY_DUALSENSE;

		case 0x0df2:
			return LRG_CONTROLLER_FAMILY_DUALSENSE_EDGE;

		default:
			return LRG_CONTROLLER_FAMILY_UNKNOWN;
		}
	}

	if (vendor == VENDOR_NINTENDO)
	{
		switch (product)
		{
		/* Joy-Con L/R, Pro Controller, charging grip pair */
		case 0x2006: case 0x2007: case 0x2009: case 0x200e:
			return LRG_CONTROLLER_FAMILY_SWITCH_PRO;

		default:
			return LRG_CONTROLLER_FAMILY_UNKNOWN;
		}
	}

	if (vendor == LRG_CONTROLLER_VENDOR_VALVE)
	{
		switch (product)
		{
		/* original Steam Controller: wired, wireless dongle */
		case 0x1102: case 0x1142:
			return LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER;

		/* the Steam Frame's two controllers as SteamVR hands them to
		 * Steam ("Steam Frame Controllers", /steamvr/virtual) */
		case 0x11e0:
			return LRG_CONTROLLER_FAMILY_STEAM_FRAME;

		/*
		 * the Steam Controller (2026, "Ibex" / "Triton"): wired, Bluetooth,
		 * the "Proteus" puck and the Steam Machine's "Nereid" receiver
		 * (Linux hid-ids.h USB_DEVICE_ID_STEAM_CONTROLLER_IBEX .. _NEREID,
		 * SDL's controller_list.h)
		 */
		case 0x1302: case 0x1303: case 0x1304: case 0x1305:
			return LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER;

		/* Steam Deck (LCD and OLED share the id) */
		case 0x1205:
			return LRG_CONTROLLER_FAMILY_STEAM_DECK;

		/* the Steam Virtual Gamepad: an Xbox 360 shaped pad */
		case 0x11ff:
			return LRG_CONTROLLER_FAMILY_XBOX_360;

		default:
			return LRG_CONTROLLER_FAMILY_UNKNOWN;
		}
	}

	return LRG_CONTROLLER_FAMILY_UNKNOWN;
}

/* whether @lower contains @needle */
static gboolean
has (const gchar *lower,
     const gchar *needle)
{
	return g_strstr_len (lower, -1, needle) != NULL;
}

/*
 * family_from_name:
 * @lower: the lower-cased controller name
 *
 * The family a controller name suggests, or UNKNOWN.  Steam hardware is
 * checked first (the Deck can present itself as an Xbox pad), then Xbox,
 * PlayStation and Switch names, most specific first.
 */
static LrgControllerFamily
family_from_name (const gchar *lower)
{
	/* Valve: "Steam Deck" / "Neptune" (the Deck's codename), the Frame */
	if (has (lower, "steam frame") || has (lower, "steamframe") ||
	    has (lower, "frame controller") || has (lower, "frame (controller"))
		return LRG_CONTROLLER_FAMILY_STEAM_FRAME;

	if (has (lower, "steam deck") || has (lower, "steamdeck") || has (lower, "neptune"))
		return LRG_CONTROLLER_FAMILY_STEAM_DECK;

	if (has (lower, "steam controller") || has (lower, "steam virtual"))
		return LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER;

	/* PlayStation, DualSense Edge before DualSense before DualShock */
	if (has (lower, "dualsense edge") || has (lower, "dualsense-edge"))
		return LRG_CONTROLLER_FAMILY_DUALSENSE_EDGE;

	if (has (lower, "dualsense") || has (lower, "ps5"))
		return LRG_CONTROLLER_FAMILY_DUALSENSE;

	if (has (lower, "dualshock") || has (lower, "ps4") || has (lower, "ps3") ||
	    has (lower, "playstation") || has (lower, "sony"))
		return LRG_CONTROLLER_FAMILY_DUALSHOCK4;

	/* Switch, before the generic "pro controller" would match anything */
	if (has (lower, "nintendo") || has (lower, "switch") || has (lower, "joy-con") ||
	    has (lower, "joycon") || has (lower, "pro controller"))
		return LRG_CONTROLLER_FAMILY_SWITCH_PRO;

	/* Xbox: Series, One, 360 and anything that says Xbox/XInput */
	if (has (lower, "series") && (has (lower, "xbox") || has (lower, "x|s")))
		return LRG_CONTROLLER_FAMILY_XBOX_SERIES;

	if (has (lower, "elite") && has (lower, "xbox"))
		return LRG_CONTROLLER_FAMILY_XBOX_SERIES;

	if (has (lower, "xbox one") || has (lower, "xbox wireless") || has (lower, "xbox adaptive"))
		return LRG_CONTROLLER_FAMILY_XBOX_ONE;

	if (has (lower, "xbox 360") || has (lower, "x360") || has (lower, "x-box 360") ||
	    has (lower, "xbox360") || has (lower, "x-box") || has (lower, "xbox") ||
	    has (lower, "xinput") || has (lower, "microsoft"))
		return LRG_CONTROLLER_FAMILY_XBOX_360;

	/* the bare "Wireless Controller" the kernel names a DualShock 4 */
	if (has (lower, "wireless controller"))
		return LRG_CONTROLLER_FAMILY_DUALSHOCK4;

	return LRG_CONTROLLER_FAMILY_UNKNOWN;
}

/*
 * lrg_controller_family_detect:
 *
 * Ids first, name second, then the Steam Deck host rule; see the header.
 */
LrgControllerFamily
lrg_controller_family_detect (const gchar *name,
                              guint16      vendor,
                              guint16      product,
                              gboolean     steam_deck_host)
{
	return lrg_controller_family_detect_on_host (name, vendor, product,
	                                             steam_deck_host ? LRG_CONTROLLER_FAMILY_STEAM_DECK
	                                                             : LRG_CONTROLLER_FAMILY_UNKNOWN);
}

LrgControllerFamily
lrg_controller_family_detect_on_host (const gchar         *name,
                                      guint16              vendor,
                                      guint16              product,
                                      LrgControllerFamily  host)
{
	g_autofree gchar *lower = NULL;
	LrgControllerFamily family;
	gboolean virtual_pad;

	lower = g_ascii_strdown (name != NULL ? name : "", -1);

	/* the ids are exact; the name settles what the ids do not know */
	family = family_from_ids (vendor, product);
	if (family == LRG_CONTROLLER_FAMILY_UNKNOWN)
		family = family_from_name (lower);

	/*
	 * An id-matched Xbox One / Series / 360 pad may still carry a more
	 * specific name ("Xbox Series X Controller" on a product id that is
	 * only in the One table); the name only refines inside the Xbox group.
	 */
	if ((family == LRG_CONTROLLER_FAMILY_XBOX_ONE || family == LRG_CONTROLLER_FAMILY_XBOX_360) &&
	    vendor == VENDOR_MICROSOFT &&
	    has (lower, "series"))
		family = LRG_CONTROLLER_FAMILY_XBOX_SERIES;

	/* Valve vendor with a Frame name, whatever the product id */
	if (vendor == LRG_CONTROLLER_VENDOR_VALVE &&
	    (has (lower, "frame") || has (lower, "steam frame")))
		family = LRG_CONTROLLER_FAMILY_STEAM_FRAME;

	/*
	 * On a Deck, Steam Input hands a game one virtual pad that is Xbox 360
	 * shaped (or unnamed) for the Deck's own controls and for any pad
	 * plugged in: those are the Deck.  On a Frame the same pad stands for
	 * the Frame's two controllers (Steam builds it from SteamVR's "Steam
	 * Frame Controllers").  A pad that names itself as something else (a
	 * DualSense over USB the game sees directly) keeps its family.
	 */
	virtual_pad = family == LRG_CONTROLLER_FAMILY_UNKNOWN ||
	              family == LRG_CONTROLLER_FAMILY_XBOX_360 ||
	              family == LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER;
	if ((host == LRG_CONTROLLER_FAMILY_STEAM_DECK || host == LRG_CONTROLLER_FAMILY_STEAM_FRAME) && virtual_pad)
		return host;

	if (family == LRG_CONTROLLER_FAMILY_UNKNOWN)
		return LRG_CONTROLLER_FAMILY_GENERIC;

	return family;
}

/* ==========================================================================
 * Host and sysfs
 * ========================================================================== */

/* first line of a small sysfs file, stripped; NULL when unreadable */
static gchar *
read_sysfs_line (const gchar *path)
{
	g_autofree gchar *contents = NULL;

	if (!g_file_get_contents (path, &contents, NULL, NULL))
		return NULL;

	return g_strstrip (g_steal_pointer (&contents));
}

gboolean
lrg_controller_is_steam_deck_host_at (const gchar *sysfs_root)
{
	const gchar *env;
	g_autofree gchar *product_path = NULL;
	g_autofree gchar *vendor_path = NULL;
	g_autofree gchar *product = NULL;
	g_autofree gchar *vendor = NULL;

	/* Steam sets SteamDeck=1 for every game it starts on a Deck */
	env = g_getenv ("SteamDeck");
	if (env != NULL && g_strcmp0 (env, "1") == 0)
		return TRUE;

	if (sysfs_root == NULL)
		sysfs_root = "/sys";

	/* "Jupiter" is the LCD Deck, "Galileo" the OLED one, board vendor Valve */
	product_path = g_build_filename (sysfs_root, "devices", "virtual", "dmi", "id", "product_name", NULL);
	vendor_path = g_build_filename (sysfs_root, "devices", "virtual", "dmi", "id", "board_vendor", NULL);
	product = read_sysfs_line (product_path);
	vendor = read_sysfs_line (vendor_path);
	if (product == NULL || vendor == NULL)
		return FALSE;

	return g_ascii_strcasecmp (vendor, "Valve") == 0 &&
	       (g_ascii_strcasecmp (product, "Jupiter") == 0 ||
	        g_ascii_strcasecmp (product, "Galileo") == 0);
}

gboolean
lrg_controller_is_steam_deck_host (void)
{
	return lrg_controller_is_steam_deck_host_at (NULL);
}

/* whether the os-release file at @path says SteamOS's "vr" variant */
static gboolean
os_release_is_frame (const gchar *path)
{
	g_autofree gchar *contents = NULL;
	g_auto(GStrv) lines = NULL;
	gboolean steamos;
	gboolean vr;
	guint i;

	if (!g_file_get_contents (path, &contents, NULL, NULL))
		return FALSE;

	steamos = FALSE;
	vr = FALSE;
	lines = g_strsplit (contents, "\n", -1);
	for (i = 0; lines[i] != NULL; i++)
	{
		g_autofree gchar *value = NULL;
		gchar *eq;

		eq = strchr (lines[i], '=');
		if (eq == NULL)
			continue;
		/* the value without its quotes */
		value = g_strstrip (g_strdup (eq + 1));
		if (value[0] == '"' || value[0] == '\'')
		{
			gsize len = strlen (value);

			if (len >= 2)
			{
				memmove (value, value + 1, len - 2);
				value[len - 2] = '\0';
			}
		}
		*eq = '\0';
		if (g_strcmp0 (g_strstrip (lines[i]), "ID") == 0 && g_ascii_strcasecmp (value, "steamos") == 0)
			steamos = TRUE;
		else if (g_strcmp0 (lines[i], "VARIANT_ID") == 0 && g_ascii_strcasecmp (value, "vr") == 0)
			vr = TRUE;
	}

	return steamos && vr;
}

gboolean
lrg_controller_is_steam_frame_host_at (const gchar *root)
{
	static const gchar * const files[] = {
		"etc/os-release", "usr/lib/os-release", "run/host/etc/os-release", "run/host/usr/lib/os-release"
	};
	guint i;

	if (root == NULL)
		root = "/";

	/* this system's, then the host's of a container */
	for (i = 0; i < G_N_ELEMENTS (files); i++)
	{
		g_autofree gchar *path = g_build_filename (root, files[i], NULL);

		if (os_release_is_frame (path))
			return TRUE;
	}

	return FALSE;
}

gboolean
lrg_controller_is_steam_frame_host (void)
{
	return lrg_controller_is_steam_frame_host_at (NULL);
}

LrgControllerFamily
lrg_controller_host_family (void)
{
	/* the Frame first: Steam may set SteamDeck=1 for its games there too */
	if (lrg_controller_is_steam_frame_host ())
		return LRG_CONTROLLER_FAMILY_STEAM_FRAME;
	if (lrg_controller_is_steam_deck_host ())
		return LRG_CONTROLLER_FAMILY_STEAM_DECK;

	return LRG_CONTROLLER_FAMILY_UNKNOWN;
}

/*
 * key_bits_have_buttons:
 * Whether a sysfs capabilities/key line ("7cdb000000000000 0 0 0 0":
 * long words, most significant first) sets a bit of BTN_JOYSTICK (0x120)
 * .. BTN_THUMBR (0x13e).
 */
static gboolean
key_bits_have_buttons (const gchar *line)
{
	g_auto(GStrv) words = NULL;
	guint n;
	guint bits;
	guint bit;

	words = g_strsplit_set (line, " \t", -1);
	n = g_strv_length (words);
	/* a word of 64 bits on 64-bit kernels; the words' count says which */
	bits = 64;
	for (bit = 0x120; bit <= 0x13e; bit++)
	{
		guint index;
		guint64 word;

		index = bit / bits;
		if (index >= n)
			return FALSE;
		/* the last word holds bits 0 .. 63 */
		if (!g_ascii_string_to_unsigned (words[n - 1 - index], 16, 0, G_MAXUINT64, &word, NULL))
			return FALSE;
		if ((word >> (bit % bits)) & 1)
			return TRUE;
	}

	return FALSE;
}

gboolean
lrg_controller_probe_is_gamepad (const gchar *name,
                                 const gchar *sysfs_root)
{
	g_autofree gchar *input_dir = NULL;
	g_autoptr(GDir) dir = NULL;
	const gchar *entry;
	gboolean found;

	if (name == NULL || name[0] == '\0')
		return TRUE;
	if (sysfs_root == NULL)
		sysfs_root = "/sys";

	input_dir = g_build_filename (sysfs_root, "class", "input", NULL);
	dir = g_dir_open (input_dir, 0, NULL);
	if (dir == NULL)
		return TRUE;

	/* every device of that name (a pad and its motion sensors share
	 * one): a pad when any of them has the buttons */
	found = FALSE;
	while ((entry = g_dir_read_name (dir)) != NULL)
	{
		g_autofree gchar *name_path = NULL;
		g_autofree gchar *dev_name = NULL;
		g_autofree gchar *key_path = NULL;
		g_autofree gchar *keys = NULL;

		if (!g_str_has_prefix (entry, "input"))
			continue;
		name_path = g_build_filename (input_dir, entry, "name", NULL);
		dev_name = read_sysfs_line (name_path);
		if (dev_name == NULL || g_strcmp0 (dev_name, name) != 0)
			continue;
		found = TRUE;
		key_path = g_build_filename (input_dir, entry, "capabilities", "key", NULL);
		keys = read_sysfs_line (key_path);
		if (keys != NULL && key_bits_have_buttons (keys))
			return TRUE;
	}

	return !found;
}

gboolean
lrg_controller_probe_usb_ids (const gchar *name,
                              const gchar *sysfs_root,
                              guint16     *vendor,
                              guint16     *product)
{
	g_autofree gchar *input_dir = NULL;
	g_autoptr(GDir) dir = NULL;
	const gchar *entry;

	if (vendor != NULL)
		*vendor = 0;
	if (product != NULL)
		*product = 0;

	if (name == NULL || name[0] == '\0')
		return FALSE;

	if (sysfs_root == NULL)
		sysfs_root = "/sys";

	/* every input device has class/input/inputN/{name,id/vendor,id/product} */
	input_dir = g_build_filename (sysfs_root, "class", "input", NULL);
	dir = g_dir_open (input_dir, 0, NULL);
	if (dir == NULL)
		return FALSE;

	while ((entry = g_dir_read_name (dir)) != NULL)
	{
		g_autofree gchar *name_path = NULL;
		g_autofree gchar *dev_name = NULL;
		g_autofree gchar *vendor_path = NULL;
		g_autofree gchar *product_path = NULL;
		g_autofree gchar *vendor_text = NULL;
		g_autofree gchar *product_text = NULL;
		guint64 vendor_id;
		guint64 product_id;

		if (!g_str_has_prefix (entry, "input"))
			continue;

		name_path = g_build_filename (input_dir, entry, "name", NULL);
		dev_name = read_sysfs_line (name_path);
		if (dev_name == NULL || g_strcmp0 (dev_name, name) != 0)
			continue;

		vendor_path = g_build_filename (input_dir, entry, "id", "vendor", NULL);
		product_path = g_build_filename (input_dir, entry, "id", "product", NULL);
		vendor_text = read_sysfs_line (vendor_path);
		product_text = read_sysfs_line (product_path);
		if (vendor_text == NULL || product_text == NULL)
			continue;

		/* sysfs prints the ids as bare hexadecimal ("045e") */
		if (!g_ascii_string_to_unsigned (vendor_text, 16, 0, 0xffff, &vendor_id, NULL) ||
		    !g_ascii_string_to_unsigned (product_text, 16, 0, 0xffff, &product_id, NULL))
			continue;

		if (vendor != NULL)
			*vendor = (guint16)vendor_id;
		if (product != NULL)
			*product = (guint16)product_id;

		return TRUE;
	}

	return FALSE;
}

/* ==========================================================================
 * Glyphs
 *
 * One table per label set; the index is an LrgControllerControl.  NULL means
 * the family has no such control.
 * ========================================================================== */

#define N_CONTROLS (LRG_CONTROLLER_CONTROL_RIGHT_STICK + 1)

/* Xbox 360: the View/Menu buttons are Back and Start */
static const gchar *glyphs_xbox_360[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"Y", "B", "A", "X", "LB", "LT", "RB", "RT",
	"Back", "Guide", "Start", "LS", "RS",
	NULL, NULL, NULL, NULL, NULL, NULL,
	"Left Stick", "Right Stick"
};

static const gchar *glyphs_xbox_one[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"Y", "B", "A", "X", "LB", "LT", "RB", "RT",
	"View", "Guide", "Menu", "LS", "RS",
	NULL, NULL, NULL, NULL, NULL, NULL,
	"Left Stick", "Right Stick"
};

/* Series and Elite Series 2 (the Elite's paddles are P1..P4) */
static const gchar *glyphs_xbox_series[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"Y", "B", "A", "X", "LB", "LT", "RB", "RT",
	"View", "Guide", "Menu", "LS", "RS",
	"P1", "P2", "P3", "P4", NULL, NULL,
	"Left Stick", "Right Stick"
};

static const gchar *glyphs_dualshock4[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"Triangle", "Circle", "Cross", "Square", "L1", "L2", "R1", "R2",
	"Share", "PS", "Options", "L3", "R3",
	NULL, NULL, NULL, NULL, "Touchpad", NULL,
	"Left Stick", "Right Stick"
};

static const gchar *glyphs_dualsense[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"Triangle", "Circle", "Cross", "Square", "L1", "L2", "R1", "R2",
	"Create", "PS", "Options", "L3", "R3",
	NULL, NULL, NULL, NULL, "Touchpad", NULL,
	"Left Stick", "Right Stick"
};

/* the Edge adds two function buttons and two back paddles under the grips */
static const gchar *glyphs_dualsense_edge[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"Triangle", "Circle", "Cross", "Square", "L1", "L2", "R1", "R2",
	"Create", "PS", "Options", "L3", "R3",
	"Left Fn", "Right Fn", "Left Paddle", "Right Paddle", "Touchpad", NULL,
	"Left Stick", "Right Stick"
};

/* Nintendo's face buttons are labelled by position, A on the right */
static const gchar *glyphs_switch_pro[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"X", "A", "B", "Y", "L", "ZL", "R", "ZR",
	"-", "Home", "+", "LS", "RS",
	NULL, NULL, NULL, NULL, NULL, NULL,
	"Left Stick", "Right Stick"
};

/* the original Steam Controller: grips under the pads, arrows for View/Menu */
static const gchar *glyphs_steam_controller[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"Y", "B", "A", "X", "LB", "LT", "RB", "RT",
	"Back", "Steam", "Start", "LS", "RS",
	"Left Grip", "Right Grip", NULL, NULL, NULL, NULL,
	"Left Stick", "Right Stick"
};

/* the Deck: L1/R1 bumpers, L2/R2 triggers, L4 R4 L5 R5 back buttons, the
 * "..." quick access button under the right pad */
static const gchar *glyphs_steam_deck[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"Y", "B", "A", "X", "L1", "L2", "R1", "R2",
	"View", "Steam", "Menu", "L3", "R3",
	"L4", "R4", "L5", "R5", NULL, "QAM",
	"Left Stick", "Right Stick"
};

/*
 * The Steam Frame's controllers: not verified against hardware (none is
 * attached where this was written).  Valve's published description is
 * face buttons, a thumbstick, trigger, bumper and grip per hand with a
 * Steam and a Menu button, so the labels follow the Deck's L1/L2/R1/R2 and
 * the grips stand in for the Deck's upper back buttons.
 */
static const gchar *glyphs_steam_frame[N_CONTROLS] = {
	"Unknown", "DPad Up", "DPad Right", "DPad Down", "DPad Left",
	"Y", "B", "A", "X", "L1", "L2", "R1", "R2",
	"View", "Steam", "Menu", "L3", "R3",
	"Left Grip", "Right Grip", NULL, NULL, NULL, NULL,
	"Left Stick", "Right Stick"
};

static const gchar **
glyph_table (LrgControllerFamily family)
{
	switch (family)
	{
	case LRG_CONTROLLER_FAMILY_XBOX_360:
		return glyphs_xbox_360;
	case LRG_CONTROLLER_FAMILY_XBOX_ONE:
		return glyphs_xbox_one;
	case LRG_CONTROLLER_FAMILY_XBOX_SERIES:
		return glyphs_xbox_series;
	case LRG_CONTROLLER_FAMILY_DUALSHOCK4:
		return glyphs_dualshock4;
	case LRG_CONTROLLER_FAMILY_DUALSENSE:
		return glyphs_dualsense;
	case LRG_CONTROLLER_FAMILY_DUALSENSE_EDGE:
		return glyphs_dualsense_edge;
	case LRG_CONTROLLER_FAMILY_SWITCH_PRO:
		return glyphs_switch_pro;
	case LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER:
		return glyphs_steam_controller;
	case LRG_CONTROLLER_FAMILY_STEAM_DECK:
		return glyphs_steam_deck;
	case LRG_CONTROLLER_FAMILY_STEAM_FRAME:
		return glyphs_steam_frame;
	case LRG_CONTROLLER_FAMILY_GENERIC:
	case LRG_CONTROLLER_FAMILY_UNKNOWN:
	default:
		/* the SDL/GLFW mapping is Xbox shaped, so is the fallback */
		return glyphs_xbox_one;
	}
}

gboolean
lrg_controller_has_control (LrgControllerFamily  family,
                            LrgControllerControl control)
{
	if ((gint)control <= 0 || (gint)control >= (gint)N_CONTROLS)
		return FALSE;

	return glyph_table (family)[control] != NULL;
}

const gchar *
lrg_controller_get_glyph (LrgControllerFamily  family,
                          LrgControllerControl control)
{
	if ((gint)control < 0 || (gint)control >= (gint)N_CONTROLS)
		return NULL;

	return glyph_table (family)[control];
}

const gchar *
lrg_controller_get_button_glyph (LrgControllerFamily family,
                                 GrlGamepadButton    button)
{
	/* the first 18 controls are the GrlGamepadButton values */
	if ((gint)button < 0 || (gint)button > (gint)LRG_CONTROLLER_CONTROL_RIGHT_STICK_CLICK)
		return "Unknown";

	return glyph_table (family)[(gint)button];
}

const gchar *
lrg_controller_get_axis_glyph (LrgControllerFamily family,
                               GrlGamepadAxis      axis)
{
	const gchar **table;

	table = glyph_table (family);

	switch (axis)
	{
	case GRL_GAMEPAD_AXIS_LEFT_X:
	case GRL_GAMEPAD_AXIS_LEFT_Y:
		return table[LRG_CONTROLLER_CONTROL_LEFT_STICK];
	case GRL_GAMEPAD_AXIS_RIGHT_X:
	case GRL_GAMEPAD_AXIS_RIGHT_Y:
		return table[LRG_CONTROLLER_CONTROL_RIGHT_STICK];
	case GRL_GAMEPAD_AXIS_LEFT_TRIGGER:
		return table[LRG_CONTROLLER_CONTROL_LEFT_TRIGGER];
	case GRL_GAMEPAD_AXIS_RIGHT_TRIGGER:
		return table[LRG_CONTROLLER_CONTROL_RIGHT_TRIGGER];
	default:
		return "Unknown";
	}
}

/* ==========================================================================
 * Presets
 * ========================================================================== */

typedef struct {
	const gchar *role;
	gboolean     is_axis;
	gint         code;
	gboolean     positive;
} PresetRole;

static const PresetRole preset_roles[] = {
	{ "move-up",          TRUE,  GRL_GAMEPAD_AXIS_LEFT_Y,  FALSE },
	{ "move-down",        TRUE,  GRL_GAMEPAD_AXIS_LEFT_Y,  TRUE },
	{ "move-left",        TRUE,  GRL_GAMEPAD_AXIS_LEFT_X,  FALSE },
	{ "move-right",       TRUE,  GRL_GAMEPAD_AXIS_LEFT_X,  TRUE },
	{ "look-up",          TRUE,  GRL_GAMEPAD_AXIS_RIGHT_Y, FALSE },
	{ "look-down",        TRUE,  GRL_GAMEPAD_AXIS_RIGHT_Y, TRUE },
	{ "look-left",        TRUE,  GRL_GAMEPAD_AXIS_RIGHT_X, FALSE },
	{ "look-right",       TRUE,  GRL_GAMEPAD_AXIS_RIGHT_X, TRUE },
	{ "confirm",          FALSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN,  FALSE },
	{ "cancel",           FALSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, FALSE },
	{ "face-south",       FALSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN,  FALSE },
	{ "face-east",        FALSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, FALSE },
	{ "face-west",        FALSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT,  FALSE },
	{ "face-north",       FALSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP,    FALSE },
	{ "page-previous",    FALSE, GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1,   FALSE },
	{ "page-next",        FALSE, GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_1,  FALSE },
	{ "trigger-left",     FALSE, GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2,   FALSE },
	{ "trigger-right",    FALSE, GRL_GAMEPAD_BUTTON_RIGHT_TRIGGER_2,  FALSE },
	{ "view",             FALSE, GRL_GAMEPAD_BUTTON_MIDDLE_LEFT,      FALSE },
	{ "menu",             FALSE, GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT,     FALSE },
	{ "guide",            FALSE, GRL_GAMEPAD_BUTTON_MIDDLE,           FALSE },
	{ "stick-left-click", FALSE, GRL_GAMEPAD_BUTTON_LEFT_THUMB,       FALSE },
	{ "stick-right-click", FALSE, GRL_GAMEPAD_BUTTON_RIGHT_THUMB,     FALSE },
	{ "dpad-up",          FALSE, GRL_GAMEPAD_BUTTON_LEFT_FACE_UP,     FALSE },
	{ "dpad-down",        FALSE, GRL_GAMEPAD_BUTTON_LEFT_FACE_DOWN,   FALSE },
	{ "dpad-left",        FALSE, GRL_GAMEPAD_BUTTON_LEFT_FACE_LEFT,   FALSE },
	{ "dpad-right",       FALSE, GRL_GAMEPAD_BUTTON_LEFT_FACE_RIGHT,  FALSE }
};

const gchar * const *
lrg_controller_preset_get_roles (void)
{
	static const gchar *names[G_N_ELEMENTS (preset_roles) + 1];
	static gsize initialised = 0;

	/* the names array is built once from the table */
	if (g_once_init_enter (&initialised))
	{
		guint i;

		for (i = 0; i < G_N_ELEMENTS (preset_roles); i++)
			names[i] = preset_roles[i].role;
		names[G_N_ELEMENTS (preset_roles)] = NULL;
		g_once_init_leave (&initialised, 1);
	}

	return names;
}

gboolean
lrg_controller_preset_lookup (LrgControllerFamily  family,
                              const gchar         *role,
                              gboolean            *is_axis,
                              gint                *code,
                              gboolean            *positive)
{
	guint i;

	g_return_val_if_fail (is_axis != NULL && code != NULL && positive != NULL, FALSE);

	if (role == NULL)
		return FALSE;

	for (i = 0; i < G_N_ELEMENTS (preset_roles); i++)
	{
		if (g_strcmp0 (preset_roles[i].role, role) != 0)
			continue;

		*is_axis = preset_roles[i].is_axis;
		*code = preset_roles[i].code;
		*positive = preset_roles[i].positive;

		/* Nintendo's confirm is the right face button, cancel the bottom */
		if (family == LRG_CONTROLLER_FAMILY_SWITCH_PRO)
		{
			if (g_strcmp0 (role, "confirm") == 0)
				*code = GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;
			else if (g_strcmp0 (role, "cancel") == 0)
				*code = GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
		}

		return TRUE;
	}

	return FALSE;
}

LrgInputMap *
lrg_controller_preset_new_map (LrgControllerFamily family,
                               gint                gamepad)
{
	LrgInputMap *map;
	guint i;

	g_return_val_if_fail (gamepad >= 0 && gamepad <= 3, NULL);

	map = lrg_input_map_new ();

	for (i = 0; i < G_N_ELEMENTS (preset_roles); i++)
	{
		g_autoptr(LrgInputAction) action = NULL;
		g_autoptr(LrgInputBinding) binding = NULL;
		gboolean is_axis;
		gint code;
		gboolean positive;

		lrg_controller_preset_lookup (family, preset_roles[i].role, &is_axis, &code, &positive);

		/* one action per role, a stick direction past half travel or a button */
		action = lrg_input_action_new (preset_roles[i].role);
		if (is_axis)
			binding = lrg_input_binding_new_gamepad_axis (gamepad, (GrlGamepadAxis)code, 0.5f, positive);
		else
			binding = lrg_input_binding_new_gamepad_button (gamepad, (GrlGamepadButton)code);

		lrg_input_action_add_binding (action, binding);
		lrg_input_map_add_action (map, action);
	}

	return map;
}

/* ==========================================================================
 * LrgControllerMonitor
 * ========================================================================== */

struct _LrgControllerMonitor
{
	GObject parent_instance;

	LrgInput            *input;           /* owned */
	gint                 gamepad;
	LrgControllerFamily  family;          /* as of the last poll */
	LrgControllerFamily  override_family;
	gboolean             connected;
	gboolean             has_ids;
	guint16              vendor;
	guint16              product;
	gchar               *name;            /* owned, NULL when none */
	LrgControllerFamily  host_family;     /* the Deck or the Frame, else UNKNOWN */
	gboolean             dirty;           /* detect again at the next poll */
};

G_DEFINE_FINAL_TYPE (LrgControllerMonitor, lrg_controller_monitor, G_TYPE_OBJECT)

enum {
	SIGNAL_FAMILY_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_controller_monitor_finalize (GObject *object)
{
	LrgControllerMonitor *self = LRG_CONTROLLER_MONITOR (object);

	g_clear_object (&self->input);
	g_clear_pointer (&self->name, g_free);

	G_OBJECT_CLASS (lrg_controller_monitor_parent_class)->finalize (object);
}

static void
lrg_controller_monitor_class_init (LrgControllerMonitorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_controller_monitor_finalize;

	/**
	 * LrgControllerMonitor::family-changed:
	 * @self: the #LrgControllerMonitor
	 * @old_family: the family before
	 * @new_family: the family now
	 *
	 * Emitted from lrg_controller_monitor_poll() when a controller is
	 * connected (@old_family is %LRG_CONTROLLER_FAMILY_UNKNOWN), swapped
	 * for one of another family, or removed (@new_family is
	 * %LRG_CONTROLLER_FAMILY_UNKNOWN).
	 */
	signals[SIGNAL_FAMILY_CHANGED] =
		g_signal_new ("family-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2,
		              LRG_TYPE_CONTROLLER_FAMILY,
		              LRG_TYPE_CONTROLLER_FAMILY);
}

static void
lrg_controller_monitor_init (LrgControllerMonitor *self)
{
	const gchar *env;
	LrgControllerFamily forced;

	self->family = LRG_CONTROLLER_FAMILY_UNKNOWN;
	self->override_family = LRG_CONTROLLER_FAMILY_UNKNOWN;
	self->dirty = TRUE;
	self->host_family = lrg_controller_host_family ();

	/* the environment forces a family for hardware the tables lack */
	env = g_getenv (LRG_CONTROLLER_ENV_FAMILY);
	if (env != NULL && lrg_controller_family_from_nick (env, &forced))
		self->override_family = forced;
}

LrgControllerMonitor *
lrg_controller_monitor_new (LrgInput *input,
                            gint      gamepad)
{
	LrgControllerMonitor *self;

	g_return_val_if_fail (LRG_IS_INPUT (input), NULL);
	g_return_val_if_fail (gamepad >= 0 && gamepad <= 3, NULL);

	self = g_object_new (LRG_TYPE_CONTROLLER_MONITOR, NULL);
	self->input = g_object_ref (input);
	self->gamepad = gamepad;

	return self;
}

/*
 * lrg_controller_monitor_poll:
 *
 * Chunks: read availability and the name; skip when nothing changed; read
 * the ids (the source's, else a Linux sysfs probe by name); detect; emit.
 */
gboolean
lrg_controller_monitor_poll (LrgControllerMonitor *self)
{
	LrgControllerFamily old_family;
	LrgControllerFamily family;
	gboolean connected;
	const gchar *name;

	g_return_val_if_fail (LRG_IS_CONTROLLER_MONITOR (self), FALSE);

	/* chunk 1: is a pad there, and is it the one we saw */
	connected = lrg_input_is_gamepad_available (self->input, self->gamepad);
	name = connected ? lrg_input_get_gamepad_name (self->input, self->gamepad) : NULL;

	if (!self->dirty && connected == self->connected && g_strcmp0 (name, self->name) == 0)
		return FALSE;

	self->dirty = FALSE;
	self->connected = connected;
	g_free (self->name);
	self->name = g_strdup (name);
	self->has_ids = FALSE;
	self->vendor = 0;
	self->product = 0;

	/* chunk 2: ids from the source, or sysfs when the source has none */
	if (connected)
	{
		self->has_ids = lrg_input_get_gamepad_ids (self->input, self->gamepad,
		                                           &self->vendor, &self->product);
		if (!self->has_ids && name != NULL)
			self->has_ids = lrg_controller_probe_usb_ids (name, NULL, &self->vendor, &self->product);
	}

	/* chunk 3: the family; nothing connected is UNKNOWN, an override wins */
	if (!connected)
		family = LRG_CONTROLLER_FAMILY_UNKNOWN;
	else if (self->override_family != LRG_CONTROLLER_FAMILY_UNKNOWN)
		family = self->override_family;
	else
		family = lrg_controller_family_detect_on_host (name, self->vendor, self->product,
		                                               self->host_family);

	/* chunk 4: tell the game */
	old_family = self->family;
	if (family == old_family)
		return FALSE;

	self->family = family;
	g_signal_emit (self, signals[SIGNAL_FAMILY_CHANGED], 0, old_family, family);

	return TRUE;
}

LrgControllerFamily
lrg_controller_monitor_get_family (LrgControllerMonitor *self)
{
	g_return_val_if_fail (LRG_IS_CONTROLLER_MONITOR (self), LRG_CONTROLLER_FAMILY_UNKNOWN);

	return self->family;
}

gboolean
lrg_controller_monitor_is_connected (LrgControllerMonitor *self)
{
	g_return_val_if_fail (LRG_IS_CONTROLLER_MONITOR (self), FALSE);

	return self->connected;
}

const gchar *
lrg_controller_monitor_get_name (LrgControllerMonitor *self)
{
	g_return_val_if_fail (LRG_IS_CONTROLLER_MONITOR (self), NULL);

	return self->name;
}

gboolean
lrg_controller_monitor_get_ids (LrgControllerMonitor *self,
                                guint16              *vendor,
                                guint16              *product)
{
	g_return_val_if_fail (LRG_IS_CONTROLLER_MONITOR (self), FALSE);

	if (vendor != NULL)
		*vendor = self->vendor;
	if (product != NULL)
		*product = self->product;

	return self->has_ids;
}

gboolean
lrg_controller_monitor_get_steam_deck_host (LrgControllerMonitor *self)
{
	g_return_val_if_fail (LRG_IS_CONTROLLER_MONITOR (self), FALSE);

	return self->host_family == LRG_CONTROLLER_FAMILY_STEAM_DECK;
}

void
lrg_controller_monitor_set_steam_deck_host (LrgControllerMonitor *self,
                                            gboolean              steam_deck_host)
{
	g_return_if_fail (LRG_IS_CONTROLLER_MONITOR (self));

	self->host_family = steam_deck_host ? LRG_CONTROLLER_FAMILY_STEAM_DECK : LRG_CONTROLLER_FAMILY_UNKNOWN;
	self->dirty = TRUE;
}

LrgControllerFamily
lrg_controller_monitor_get_host_family (LrgControllerMonitor *self)
{
	g_return_val_if_fail (LRG_IS_CONTROLLER_MONITOR (self), LRG_CONTROLLER_FAMILY_UNKNOWN);

	return self->host_family;
}

void
lrg_controller_monitor_set_host_family (LrgControllerMonitor *self,
                                        LrgControllerFamily   host)
{
	g_return_if_fail (LRG_IS_CONTROLLER_MONITOR (self));

	self->host_family = host;
	self->dirty = TRUE;
}

void
lrg_controller_monitor_set_family_override (LrgControllerMonitor *self,
                                            LrgControllerFamily   family)
{
	g_return_if_fail (LRG_IS_CONTROLLER_MONITOR (self));

	self->override_family = family;
	self->dirty = TRUE;
}

LrgControllerFamily
lrg_controller_monitor_get_family_override (LrgControllerMonitor *self)
{
	g_return_val_if_fail (LRG_IS_CONTROLLER_MONITOR (self), LRG_CONTROLLER_FAMILY_UNKNOWN);

	return self->override_family;
}
