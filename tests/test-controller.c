/* test-controller.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the controller families: detection from names and ids,
 * sysfs probing, glyphs, presets and the monitor (with LrgInputMock).
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* ==========================================================================
 * Detection
 * ========================================================================== */

typedef struct {
	const gchar         *name;
	guint16              vendor;
	guint16              product;
	LrgControllerFamily  expected;
} DetectCase;

static void
test_detect_names (void)
{
	static const DetectCase cases[] = {
		{ "Microsoft X-Box 360 pad", 0, 0, LRG_CONTROLLER_FAMILY_XBOX_360 },
		{ "Xbox 360 Controller", 0, 0, LRG_CONTROLLER_FAMILY_XBOX_360 },
		{ "Xbox One Controller", 0, 0, LRG_CONTROLLER_FAMILY_XBOX_ONE },
		{ "Xbox Wireless Controller", 0, 0, LRG_CONTROLLER_FAMILY_XBOX_ONE },
		{ "Xbox Series X Controller", 0, 0, LRG_CONTROLLER_FAMILY_XBOX_SERIES },
		{ "Xbox Elite Wireless Controller Series 2", 0, 0, LRG_CONTROLLER_FAMILY_XBOX_SERIES },
		{ "Sony Interactive Entertainment DualSense Wireless Controller", 0, 0, LRG_CONTROLLER_FAMILY_DUALSENSE },
		{ "DualSense Edge Wireless Controller", 0, 0, LRG_CONTROLLER_FAMILY_DUALSENSE_EDGE },
		{ "Sony Computer Entertainment Wireless Controller", 0, 0, LRG_CONTROLLER_FAMILY_DUALSHOCK4 },
		{ "PS4 Controller", 0, 0, LRG_CONTROLLER_FAMILY_DUALSHOCK4 },
		{ "Nintendo Switch Pro Controller", 0, 0, LRG_CONTROLLER_FAMILY_SWITCH_PRO },
		{ "Steam Deck", 0, 0, LRG_CONTROLLER_FAMILY_STEAM_DECK },
		{ "Valve Software Steam Controller", 0, 0, LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER },
		{ "Steam Frame Controller (Left)", 0, 0, LRG_CONTROLLER_FAMILY_STEAM_FRAME },
		{ "Totally Unknown Gizmo", 0, 0, LRG_CONTROLLER_FAMILY_GENERIC },
		{ NULL, 0, 0, LRG_CONTROLLER_FAMILY_GENERIC },
		{ "", 0, 0, LRG_CONTROLLER_FAMILY_GENERIC }
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		LrgControllerFamily got;

		got = lrg_controller_family_detect (cases[i].name, cases[i].vendor, cases[i].product, FALSE);
		g_assert_cmpstr (lrg_controller_family_get_nick (got), ==,
		                 lrg_controller_family_get_nick (cases[i].expected));
	}
}

static void
test_detect_ids (void)
{
	/* ids decide even when the name says nothing useful */
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x045e, 0x028e, FALSE), ==, LRG_CONTROLLER_FAMILY_XBOX_360);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x045e, 0x02ea, FALSE), ==, LRG_CONTROLLER_FAMILY_XBOX_ONE);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x045e, 0x0b12, FALSE), ==, LRG_CONTROLLER_FAMILY_XBOX_SERIES);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x054c, 0x05c4, FALSE), ==, LRG_CONTROLLER_FAMILY_DUALSHOCK4);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x054c, 0x09cc, FALSE), ==, LRG_CONTROLLER_FAMILY_DUALSHOCK4);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x054c, 0x0ce6, FALSE), ==, LRG_CONTROLLER_FAMILY_DUALSENSE);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x054c, 0x0df2, FALSE), ==, LRG_CONTROLLER_FAMILY_DUALSENSE_EDGE);
	/* the Steam Controller (2026): wired, Bluetooth, puck, Steam Machine receiver */
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x28de, 0x1302, FALSE), ==, LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x28de, 0x1303, FALSE), ==, LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x28de, 0x1304, FALSE), ==, LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x28de, 0x1305, FALSE), ==, LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x057e, 0x2009, FALSE), ==, LRG_CONTROLLER_FAMILY_SWITCH_PRO);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x28de, 0x1205, FALSE), ==, LRG_CONTROLLER_FAMILY_STEAM_DECK);
	g_assert_cmpint (lrg_controller_family_detect ("pad", 0x28de, 0x1102, FALSE), ==, LRG_CONTROLLER_FAMILY_STEAM_CONTROLLER);

	/* the Steam Virtual Gamepad is an Xbox 360 shaped pad away from a Deck */
	g_assert_cmpint (lrg_controller_family_detect ("Microsoft X-Box 360 pad 0", 0x28de, 0x11ff, FALSE), ==,
	                 LRG_CONTROLLER_FAMILY_XBOX_360);

	/* Valve vendor with a Frame name wins over an unknown product id */
	g_assert_cmpint (lrg_controller_family_detect ("Steam Frame Controller", 0x28de, 0x9999, FALSE), ==,
	                 LRG_CONTROLLER_FAMILY_STEAM_FRAME);

	/* an unknown Valve product without a hint is generic, not guessed */
	g_assert_cmpint (lrg_controller_family_detect ("Mystery", 0x28de, 0x9999, FALSE), ==,
	                 LRG_CONTROLLER_FAMILY_GENERIC);

	/* an Xbox One product id with a Series name refines to Series */
	g_assert_cmpint (lrg_controller_family_detect ("Xbox Series X Controller", 0x045e, 0x02ea, FALSE), ==,
	                 LRG_CONTROLLER_FAMILY_XBOX_SERIES);
}

static void
test_detect_deck_host (void)
{
	/* on a Deck the virtual / unnamed pads are the Deck's own controls */
	g_assert_cmpint (lrg_controller_family_detect ("Microsoft X-Box 360 pad 0", 0x28de, 0x11ff, TRUE), ==,
	                 LRG_CONTROLLER_FAMILY_STEAM_DECK);
	g_assert_cmpint (lrg_controller_family_detect (NULL, 0, 0, TRUE), ==, LRG_CONTROLLER_FAMILY_STEAM_DECK);
	g_assert_cmpint (lrg_controller_family_detect ("Mystery", 0, 0, TRUE), ==, LRG_CONTROLLER_FAMILY_STEAM_DECK);

	/* a pad that names itself as something else keeps its family */
	g_assert_cmpint (lrg_controller_family_detect ("DualSense Wireless Controller", 0x054c, 0x0ce6, TRUE), ==,
	                 LRG_CONTROLLER_FAMILY_DUALSENSE);
	g_assert_cmpint (lrg_controller_family_detect ("Xbox Series X Controller", 0x045e, 0x0b13, TRUE), ==,
	                 LRG_CONTROLLER_FAMILY_XBOX_SERIES);
}

static void
test_detect_frame_host (void)
{
	/* SteamVR's controllers by id, wherever they show up */
	g_assert_cmpint (lrg_controller_family_detect_on_host ("Steam Frame Controllers", 0x28de, 0x11e0,
	                                                       LRG_CONTROLLER_FAMILY_UNKNOWN), ==,
	                 LRG_CONTROLLER_FAMILY_STEAM_FRAME);

	/* on a Frame, Steam Input's virtual pad is the Frame's controllers */
	g_assert_cmpint (lrg_controller_family_detect_on_host ("Microsoft X-Box 360 pad 0", 0x28de, 0x11ff,
	                                                       LRG_CONTROLLER_FAMILY_STEAM_FRAME), ==,
	                 LRG_CONTROLLER_FAMILY_STEAM_FRAME);
	g_assert_cmpint (lrg_controller_family_detect_on_host (NULL, 0, 0, LRG_CONTROLLER_FAMILY_STEAM_FRAME), ==,
	                 LRG_CONTROLLER_FAMILY_STEAM_FRAME);
	/* a pad that names itself keeps its family */
	g_assert_cmpint (lrg_controller_family_detect_on_host ("DualSense Wireless Controller", 0x054c, 0x0ce6,
	                                                       LRG_CONTROLLER_FAMILY_STEAM_FRAME), ==,
	                 LRG_CONTROLLER_FAMILY_DUALSENSE);
	/* elsewhere the virtual pad stays Xbox 360 shaped */
	g_assert_cmpint (lrg_controller_family_detect_on_host ("Microsoft X-Box 360 pad 0", 0x28de, 0x11ff,
	                                                       LRG_CONTROLLER_FAMILY_UNKNOWN), ==,
	                 LRG_CONTROLLER_FAMILY_XBOX_360);
	/* the Deck wrapper is the same rule */
	g_assert_cmpint (lrg_controller_family_detect_on_host ("Microsoft X-Box 360 pad 0", 0x28de, 0x11ff,
	                                                       LRG_CONTROLLER_FAMILY_STEAM_DECK), ==,
	                 LRG_CONTROLLER_FAMILY_STEAM_DECK);
}

static void
test_family_names (void)
{
	LrgControllerFamily family;
	gint i;

	for (i = LRG_CONTROLLER_FAMILY_UNKNOWN; i <= LRG_CONTROLLER_FAMILY_STEAM_FRAME; i++)
	{
		const gchar *nick;

		nick = lrg_controller_family_get_nick ((LrgControllerFamily)i);
		g_assert_true (lrg_controller_family_from_nick (nick, &family));
		g_assert_cmpint (family, ==, i);
		g_assert_cmpstr (g_enum_to_string (LRG_TYPE_CONTROLLER_FAMILY, i), !=, NULL);
	}

	g_assert_false (lrg_controller_family_from_nick ("nope", &family));
	g_assert_false (lrg_controller_family_from_nick (NULL, &family));
	g_assert_cmpint (lrg_controller_family_get_gamepad_type (LRG_CONTROLLER_FAMILY_XBOX_SERIES), ==, LRG_GAMEPAD_TYPE_XBOX);
	g_assert_cmpint (lrg_controller_family_get_gamepad_type (LRG_CONTROLLER_FAMILY_DUALSENSE), ==, LRG_GAMEPAD_TYPE_PLAYSTATION);
	g_assert_cmpint (lrg_controller_family_get_gamepad_type (LRG_CONTROLLER_FAMILY_STEAM_FRAME), ==, LRG_GAMEPAD_TYPE_STEAM_DECK);
	g_assert_true (lrg_controller_family_is_steam (LRG_CONTROLLER_FAMILY_STEAM_DECK));
	g_assert_false (lrg_controller_family_is_steam (LRG_CONTROLLER_FAMILY_DUALSENSE));
}

/* ==========================================================================
 * sysfs
 * ========================================================================== */

/* writes a fake class/input/inputN device under @root */
static void
write_input (const gchar *root,
             const gchar *entry,
             const gchar *name,
             const gchar *vendor,
             const gchar *product)
{
	g_autofree gchar *dir = g_build_filename (root, "class", "input", entry, "id", NULL);
	g_autofree gchar *name_path = g_build_filename (root, "class", "input", entry, "name", NULL);
	g_autofree gchar *vendor_path = g_build_filename (dir, "vendor", NULL);
	g_autofree gchar *product_path = g_build_filename (dir, "product", NULL);

	g_assert_cmpint (g_mkdir_with_parents (dir, 0755), ==, 0);
	g_assert_true (g_file_set_contents (name_path, name, -1, NULL));
	g_assert_true (g_file_set_contents (vendor_path, vendor, -1, NULL));
	g_assert_true (g_file_set_contents (product_path, product, -1, NULL));
}

static void
test_probe_usb_ids (void)
{
	g_autofree gchar *root = NULL;
	guint16 vendor;
	guint16 product;

	root = g_dir_make_tmp ("lrg-controller-XXXXXX", NULL);
	g_assert_nonnull (root);

	write_input (root, "input5", "Sony Interactive Entertainment DualSense Wireless Controller\n", "054c\n", "0ce6\n");
	write_input (root, "input6", "Sony Interactive Entertainment DualSense Wireless Controller Touchpad\n", "054c\n", "0ce6\n");
	write_input (root, "input9", "Broken Pad\n", "zzzz\n", "0001\n");
	write_input (root, "input7", "Steam Deck\n", "28de\n", "1205\n");

	g_assert_true (lrg_controller_probe_usb_ids ("Sony Interactive Entertainment DualSense Wireless Controller",
	                                             root, &vendor, &product));
	g_assert_cmpuint (vendor, ==, 0x054c);
	g_assert_cmpuint (product, ==, 0x0ce6);

	g_assert_true (lrg_controller_probe_usb_ids ("Steam Deck", root, &vendor, &product));
	g_assert_cmpuint (vendor, ==, 0x28de);
	g_assert_cmpuint (product, ==, 0x1205);

	/* unknown name, malformed ids, no name, a tree without class/input */
	g_assert_false (lrg_controller_probe_usb_ids ("Nothing", root, &vendor, &product));
	g_assert_cmpuint (vendor, ==, 0);
	g_assert_false (lrg_controller_probe_usb_ids ("Broken Pad", root, &vendor, &product));
	g_assert_false (lrg_controller_probe_usb_ids (NULL, root, &vendor, &product));
	g_assert_false (lrg_controller_probe_usb_ids ("", root, &vendor, &product));
	g_assert_false (lrg_controller_probe_usb_ids ("Steam Deck", "/nonexistent-sysfs", &vendor, &product));
}

/* writes a fake capabilities/key line for class/input/@entry */
static void
write_keys (const gchar *root,
            const gchar *entry,
            const gchar *keys)
{
	g_autofree gchar *dir = g_build_filename (root, "class", "input", entry, "capabilities", NULL);
	g_autofree gchar *path = g_build_filename (dir, "key", NULL);

	g_assert_cmpint (g_mkdir_with_parents (dir, 0755), ==, 0);
	g_assert_true (g_file_set_contents (path, keys, -1, NULL));
}

static void
test_probe_is_gamepad (void)
{
	g_autofree gchar *root = NULL;

	root = g_dir_make_tmp ("lrg-controller-keys-XXXXXX", NULL);
	g_assert_nonnull (root);

	/* what a Steam Frame shows GLFW: Steam's virtual pad (BTN_SOUTH ..
	 * BTN_THUMBR, bits 0x130.. in the fifth word from the end) */
	write_input (root, "input12", "Microsoft X-Box 360 pad 0\n", "28de\n", "11ff\n");
	write_keys (root, "input12", "7cdb000000000000 0 0 0 0\n");
	/* an ambient light sensor with no keys at all */
	write_input (root, "input2", "als31300\n", "0000\n", "0000\n");
	write_keys (root, "input2", "0\n");
	/* a keyboard (KEY_ESC .. and media keys, nothing in 0x120..0x13e) */
	write_input (root, "input20", "MX Keys Mini\n", "046d\n", "b369\n");
	write_keys (root, "input20", "1f 0 0 0 0 0 0 1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n");
	/* the Frame's own virtual keyboard: every key up to 255, a map too
	 * short to reach 0x120 */
	write_input (root, "input5", "frametop virtual keyboard\n", "4d44\n", "0002\n");
	write_keys (root, "input5", "ffffffffffffffff ffffffffffffffff ffffffffffffffff fffffffffffffffe\n");
	/* an old joystick: BTN_TRIGGER (0x120, bit 32 of the fifth word from the end) only */
	write_input (root, "input30", "Old Stick\n", "1234\n", "5678\n");
	write_keys (root, "input30", "100000000 0 0 0 0\n");
	/* a pad whose motion sensors share its name: one of them has buttons */
	write_input (root, "input40", "Twin Pad\n", "1234\n", "0001\n");
	write_keys (root, "input40", "0\n");
	write_input (root, "input41", "Twin Pad\n", "1234\n", "0001\n");
	write_keys (root, "input41", "ffff000000000000 0 0 0 0\n");

	g_assert_true (lrg_controller_probe_is_gamepad ("Microsoft X-Box 360 pad 0", root));
	g_assert_false (lrg_controller_probe_is_gamepad ("als31300", root));
	g_assert_false (lrg_controller_probe_is_gamepad ("MX Keys Mini", root));
	g_assert_false (lrg_controller_probe_is_gamepad ("frametop virtual keyboard", root));
	g_assert_true (lrg_controller_probe_is_gamepad ("Old Stick", root));
	g_assert_true (lrg_controller_probe_is_gamepad ("Twin Pad", root));

	/* unknown names, no name and no sysfs are taken as pads */
	g_assert_true (lrg_controller_probe_is_gamepad ("Mock Pad", root));
	g_assert_true (lrg_controller_probe_is_gamepad (NULL, root));
	g_assert_true (lrg_controller_probe_is_gamepad ("als31300", "/nonexistent-sysfs"));
}

/* writes @contents to @root/@file */
static void
write_os_release (const gchar *root,
                  const gchar *file,
                  const gchar *contents)
{
	g_autofree gchar *path = g_build_filename (root, file, NULL);
	g_autofree gchar *dir = g_path_get_dirname (path);

	g_assert_cmpint (g_mkdir_with_parents (dir, 0755), ==, 0);
	g_assert_true (g_file_set_contents (path, contents, -1, NULL));
}

static void
test_frame_host_detection (void)
{
	g_autofree gchar *root = NULL;

	root = g_dir_make_tmp ("lrg-controller-os-XXXXXX", NULL);
	g_assert_nonnull (root);

	/* nothing there */
	g_assert_false (lrg_controller_is_steam_frame_host_at (root));

	/* a Deck's SteamOS is not a Frame */
	write_os_release (root, "etc/os-release", "NAME=\"SteamOS\"\nID=steamos\nVARIANT_ID=steamdeck\n");
	g_assert_false (lrg_controller_is_steam_frame_host_at (root));

	/* the Frame's (quoted or not) */
	write_os_release (root, "etc/os-release", "NAME=\"SteamOS\"\nID=\"steamos\"\nVARIANT_ID=vr\n");
	g_assert_true (lrg_controller_is_steam_frame_host_at (root));
	write_os_release (root, "etc/os-release", "ID=steamos\nID_LIKE=arch\nVARIANT_ID='vr'\n");
	g_assert_true (lrg_controller_is_steam_frame_host_at (root));

	/* another distro with a vr variant */
	write_os_release (root, "etc/os-release", "ID=fedora\nVARIANT_ID=vr\n");
	g_assert_false (lrg_controller_is_steam_frame_host_at (root));

	/* a distrobox on the Frame: the host's file under /run/host */
	write_os_release (root, "run/host/etc/os-release", "ID=steamos\nVARIANT_ID=vr\n");
	g_assert_true (lrg_controller_is_steam_frame_host_at (root));
}

static void
test_deck_host_detection (void)
{
	g_autofree gchar *root = NULL;
	g_autofree gchar *dmi = NULL;
	g_autofree gchar *saved = g_strdup (g_getenv ("SteamDeck"));

	g_unsetenv ("SteamDeck");
	root = g_dir_make_tmp ("lrg-controller-dmi-XXXXXX", NULL);
	dmi = g_build_filename (root, "devices", "virtual", "dmi", "id", NULL);
	g_assert_cmpint (g_mkdir_with_parents (dmi, 0755), ==, 0);

	/* nothing there */
	g_assert_false (lrg_controller_is_steam_deck_host_at (root));

	/* the LCD Deck's DMI */
	{
		g_autofree gchar *product = g_build_filename (dmi, "product_name", NULL);
		g_autofree gchar *vendor = g_build_filename (dmi, "board_vendor", NULL);

		g_assert_true (g_file_set_contents (product, "Jupiter\n", -1, NULL));
		g_assert_true (g_file_set_contents (vendor, "Valve\n", -1, NULL));
		g_assert_true (lrg_controller_is_steam_deck_host_at (root));

		g_assert_true (g_file_set_contents (product, "Galileo\n", -1, NULL));
		g_assert_true (lrg_controller_is_steam_deck_host_at (root));

		/* another Valve board, another vendor with the name */
		g_assert_true (g_file_set_contents (product, "Something\n", -1, NULL));
		g_assert_false (lrg_controller_is_steam_deck_host_at (root));
		g_assert_true (g_file_set_contents (product, "Jupiter\n", -1, NULL));
		g_assert_true (g_file_set_contents (vendor, "Acme\n", -1, NULL));
		g_assert_false (lrg_controller_is_steam_deck_host_at (root));
	}

	/* the environment alone says Deck */
	g_setenv ("SteamDeck", "1", TRUE);
	g_assert_true (lrg_controller_is_steam_deck_host_at (root));
	g_setenv ("SteamDeck", "0", TRUE);
	g_assert_false (lrg_controller_is_steam_deck_host_at (root));

	if (saved != NULL)
		g_setenv ("SteamDeck", saved, TRUE);
	else
		g_unsetenv ("SteamDeck");
}

/* ==========================================================================
 * Glyphs
 * ========================================================================== */

static void
test_glyphs (void)
{
	/* face buttons: A/B/X/Y against Cross/Circle/Square/Triangle */
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_XBOX_SERIES, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN), ==, "A");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_XBOX_SERIES, GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP), ==, "Y");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_DUALSENSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN), ==, "Cross");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_DUALSENSE, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT), ==, "Circle");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_DUALSHOCK4, GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT), ==, "Square");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_DUALSHOCK4, GRL_GAMEPAD_BUTTON_RIGHT_FACE_UP), ==, "Triangle");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_SWITCH_PRO, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT), ==, "A");

	/* shoulders and the small centre buttons */
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_XBOX_360, GRL_GAMEPAD_BUTTON_MIDDLE_LEFT), ==, "Back");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_XBOX_ONE, GRL_GAMEPAD_BUTTON_MIDDLE_LEFT), ==, "View");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_DUALSHOCK4, GRL_GAMEPAD_BUTTON_MIDDLE_LEFT), ==, "Share");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_DUALSENSE, GRL_GAMEPAD_BUTTON_MIDDLE_LEFT), ==, "Create");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_DUALSENSE, GRL_GAMEPAD_BUTTON_MIDDLE_RIGHT), ==, "Options");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_XBOX_SERIES, GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1), ==, "LB");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_DUALSENSE, GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_1), ==, "L1");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_STEAM_DECK, GRL_GAMEPAD_BUTTON_LEFT_TRIGGER_2), ==, "L2");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_STEAM_DECK, GRL_GAMEPAD_BUTTON_MIDDLE), ==, "Steam");

	/* Deck and Frame grips, the Deck's back buttons and quick access */
	g_assert_cmpstr (lrg_controller_get_glyph (LRG_CONTROLLER_FAMILY_STEAM_DECK, LRG_CONTROLLER_CONTROL_BACK_LEFT_UPPER), ==, "L4");
	g_assert_cmpstr (lrg_controller_get_glyph (LRG_CONTROLLER_FAMILY_STEAM_DECK, LRG_CONTROLLER_CONTROL_BACK_RIGHT_UPPER), ==, "R4");
	g_assert_cmpstr (lrg_controller_get_glyph (LRG_CONTROLLER_FAMILY_STEAM_DECK, LRG_CONTROLLER_CONTROL_BACK_LEFT_LOWER), ==, "L5");
	g_assert_cmpstr (lrg_controller_get_glyph (LRG_CONTROLLER_FAMILY_STEAM_DECK, LRG_CONTROLLER_CONTROL_BACK_RIGHT_LOWER), ==, "R5");
	g_assert_cmpstr (lrg_controller_get_glyph (LRG_CONTROLLER_FAMILY_STEAM_DECK, LRG_CONTROLLER_CONTROL_QUICK_ACCESS), ==, "QAM");
	g_assert_cmpstr (lrg_controller_get_glyph (LRG_CONTROLLER_FAMILY_STEAM_FRAME, LRG_CONTROLLER_CONTROL_BACK_LEFT_UPPER), ==, "Left Grip");
	g_assert_cmpstr (lrg_controller_get_glyph (LRG_CONTROLLER_FAMILY_DUALSENSE, LRG_CONTROLLER_CONTROL_TOUCHPAD), ==, "Touchpad");

	/* a family lacking a control: NULL, and has_control says so */
	g_assert_null (lrg_controller_get_glyph (LRG_CONTROLLER_FAMILY_XBOX_ONE, LRG_CONTROLLER_CONTROL_BACK_LEFT_UPPER));
	g_assert_false (lrg_controller_has_control (LRG_CONTROLLER_FAMILY_XBOX_ONE, LRG_CONTROLLER_CONTROL_TOUCHPAD));
	g_assert_true (lrg_controller_has_control (LRG_CONTROLLER_FAMILY_DUALSENSE_EDGE, LRG_CONTROLLER_CONTROL_BACK_LEFT_LOWER));
	g_assert_false (lrg_controller_has_control (LRG_CONTROLLER_FAMILY_XBOX_ONE, LRG_CONTROLLER_CONTROL_UNKNOWN));
	g_assert_null (lrg_controller_get_glyph (LRG_CONTROLLER_FAMILY_XBOX_ONE, (LrgControllerControl)999));

	/* unknown family and generic use the Xbox labels; bad buttons are Unknown */
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_UNKNOWN, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN), ==, "A");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_GENERIC, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN), ==, "A");
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_XBOX_ONE, (GrlGamepadButton)99), ==, "Unknown");

	/* axes */
	g_assert_cmpstr (lrg_controller_get_axis_glyph (LRG_CONTROLLER_FAMILY_DUALSENSE, GRL_GAMEPAD_AXIS_LEFT_TRIGGER), ==, "L2");
	g_assert_cmpstr (lrg_controller_get_axis_glyph (LRG_CONTROLLER_FAMILY_XBOX_ONE, GRL_GAMEPAD_AXIS_RIGHT_TRIGGER), ==, "RT");
	g_assert_cmpstr (lrg_controller_get_axis_glyph (LRG_CONTROLLER_FAMILY_XBOX_ONE, GRL_GAMEPAD_AXIS_RIGHT_Y), ==, "Right Stick");
}

/* ==========================================================================
 * Presets
 * ========================================================================== */

static void
test_preset_lookup (void)
{
	const gchar * const *roles;
	gboolean is_axis;
	gboolean positive;
	gint code;
	guint i;

	roles = lrg_controller_preset_get_roles ();
	g_assert_nonnull (roles);

	/* every role resolves for every family */
	for (i = 0; roles[i] != NULL; i++)
	{
		gint family;

		for (family = LRG_CONTROLLER_FAMILY_UNKNOWN; family <= LRG_CONTROLLER_FAMILY_STEAM_FRAME; family++)
			g_assert_true (lrg_controller_preset_lookup ((LrgControllerFamily)family, roles[i],
			                                             &is_axis, &code, &positive));
	}
	g_assert_cmpuint (i, ==, 27);

	g_assert_true (lrg_controller_preset_lookup (LRG_CONTROLLER_FAMILY_XBOX_ONE, "confirm", &is_axis, &code, &positive));
	g_assert_false (is_axis);
	g_assert_cmpint (code, ==, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
	g_assert_true (lrg_controller_preset_lookup (LRG_CONTROLLER_FAMILY_DUALSENSE, "cancel", &is_axis, &code, &positive));
	g_assert_cmpint (code, ==, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);

	/* Nintendo swaps confirm and cancel so confirm stays the A glyph */
	g_assert_true (lrg_controller_preset_lookup (LRG_CONTROLLER_FAMILY_SWITCH_PRO, "confirm", &is_axis, &code, &positive));
	g_assert_cmpint (code, ==, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
	g_assert_cmpstr (lrg_controller_get_button_glyph (LRG_CONTROLLER_FAMILY_SWITCH_PRO, (GrlGamepadButton)code), ==, "A");
	g_assert_true (lrg_controller_preset_lookup (LRG_CONTROLLER_FAMILY_SWITCH_PRO, "cancel", &is_axis, &code, &positive));
	g_assert_cmpint (code, ==, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);

	/* stick roles are axes with a direction */
	g_assert_true (lrg_controller_preset_lookup (LRG_CONTROLLER_FAMILY_XBOX_ONE, "move-up", &is_axis, &code, &positive));
	g_assert_true (is_axis);
	g_assert_cmpint (code, ==, GRL_GAMEPAD_AXIS_LEFT_Y);
	g_assert_false (positive);

	g_assert_false (lrg_controller_preset_lookup (LRG_CONTROLLER_FAMILY_XBOX_ONE, "nonsense", &is_axis, &code, &positive));
	g_assert_false (lrg_controller_preset_lookup (LRG_CONTROLLER_FAMILY_XBOX_ONE, NULL, &is_axis, &code, &positive));
}

static void
test_preset_map (void)
{
	g_autoptr(LrgInputMap) map = NULL;
	g_autoptr(LrgInputMock) mock = NULL;
	LrgInputManager *manager;
	g_autoptr(LrgInputMap) switch_map = NULL;

	map = lrg_controller_preset_new_map (LRG_CONTROLLER_FAMILY_DUALSENSE, 0);
	g_assert_nonnull (map);
	g_assert_cmpuint (lrg_input_map_get_action_count (map), ==, 27);
	g_assert_true (lrg_input_map_has_action (map, "confirm"));
	g_assert_true (lrg_input_map_has_action (map, "look-right"));

	/* the map reads the default manager: drive it with a mock source */
	mock = lrg_input_mock_new ();
	manager = lrg_input_manager_get_default ();
	lrg_input_manager_add_source (manager, LRG_INPUT (mock));
	lrg_input_mock_set_gamepad_available (mock, 0, TRUE);

	g_assert_false (lrg_input_map_is_down (map, "confirm"));
	lrg_input_mock_set_gamepad_button_state (mock, 0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN, LRG_KEY_STATE_DOWN);
	g_assert_true (lrg_input_map_is_down (map, "confirm"));
	g_assert_false (lrg_input_map_is_down (map, "cancel"));

	/* the Switch map confirms on the right face button instead */
	switch_map = lrg_controller_preset_new_map (LRG_CONTROLLER_FAMILY_SWITCH_PRO, 0);
	g_assert_true (lrg_input_map_is_down (switch_map, "cancel"));
	g_assert_false (lrg_input_map_is_down (switch_map, "confirm"));

	/* a stick leaning past half travel is the direction's action */
	lrg_input_mock_set_gamepad_axis (mock, 0, GRL_GAMEPAD_AXIS_LEFT_X, 0.9f);
	lrg_input_manager_poll (manager);
	lrg_input_manager_poll (manager);
	g_assert_true (lrg_input_map_is_down (map, "move-right"));
	g_assert_false (lrg_input_map_is_down (map, "move-left"));
	lrg_input_mock_set_gamepad_axis (mock, 0, GRL_GAMEPAD_AXIS_LEFT_X, 0.2f);
	lrg_input_manager_poll (manager);
	lrg_input_manager_poll (manager);
	g_assert_false (lrg_input_map_is_down (map, "move-right"));

	lrg_input_manager_remove_source (manager, LRG_INPUT (mock));
}

/* ==========================================================================
 * Monitor
 * ========================================================================== */

typedef struct {
	guint               count;
	LrgControllerFamily old_family;
	LrgControllerFamily new_family;
} Changes;

static void
on_family_changed (LrgControllerMonitor *monitor,
                   LrgControllerFamily   old_family,
                   LrgControllerFamily   new_family,
                   gpointer              user_data)
{
	Changes *changes = user_data;

	changes->count++;
	changes->old_family = old_family;
	changes->new_family = new_family;
}

static void
test_monitor_connect_swap_remove (void)
{
	g_autoptr(LrgInputMock) mock = NULL;
	g_autoptr(LrgControllerMonitor) monitor = NULL;
	Changes changes = { 0 };

	mock = lrg_input_mock_new ();
	monitor = lrg_controller_monitor_new (LRG_INPUT (mock), 0);
	lrg_controller_monitor_set_steam_deck_host (monitor, FALSE);
	g_signal_connect (monitor, "family-changed", G_CALLBACK (on_family_changed), &changes);

	/* nothing connected: UNKNOWN, no signal */
	g_assert_false (lrg_controller_monitor_poll (monitor));
	g_assert_false (lrg_controller_monitor_is_connected (monitor));
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_UNKNOWN);
	g_assert_cmpuint (changes.count, ==, 0);

	/* a DualSense plugged in */
	lrg_input_mock_set_gamepad_available (mock, 0, TRUE);
	lrg_input_mock_set_gamepad_name (mock, 0, "Sony Interactive Entertainment DualSense Wireless Controller");
	lrg_input_mock_set_gamepad_ids (mock, 0, 0x054c, 0x0ce6);
	g_assert_true (lrg_controller_monitor_poll (monitor));
	g_assert_true (lrg_controller_monitor_is_connected (monitor));
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_DUALSENSE);
	g_assert_cmpuint (changes.count, ==, 1);
	g_assert_cmpint (changes.old_family, ==, LRG_CONTROLLER_FAMILY_UNKNOWN);
	g_assert_cmpint (changes.new_family, ==, LRG_CONTROLLER_FAMILY_DUALSENSE);
	g_assert_cmpstr (lrg_controller_monitor_get_name (monitor), ==, "Sony Interactive Entertainment DualSense Wireless Controller");
	{
		guint16 vendor = 0;
		guint16 product = 0;

		g_assert_true (lrg_controller_monitor_get_ids (monitor, &vendor, &product));
		g_assert_cmpuint (vendor, ==, 0x054c);
		g_assert_cmpuint (product, ==, 0x0ce6);
	}

	/* polling again changes nothing */
	g_assert_false (lrg_controller_monitor_poll (monitor));
	g_assert_cmpuint (changes.count, ==, 1);

	/* swapped for an Xbox Series pad on the same slot */
	lrg_input_mock_set_gamepad_name (mock, 0, "Xbox Series X Controller");
	lrg_input_mock_set_gamepad_ids (mock, 0, 0x045e, 0x0b13);
	g_assert_true (lrg_controller_monitor_poll (monitor));
	g_assert_cmpuint (changes.count, ==, 2);
	g_assert_cmpint (changes.old_family, ==, LRG_CONTROLLER_FAMILY_DUALSENSE);
	g_assert_cmpint (changes.new_family, ==, LRG_CONTROLLER_FAMILY_XBOX_SERIES);

	/* removed */
	lrg_input_mock_set_gamepad_available (mock, 0, FALSE);
	g_assert_true (lrg_controller_monitor_poll (monitor));
	g_assert_cmpuint (changes.count, ==, 3);
	g_assert_cmpint (changes.new_family, ==, LRG_CONTROLLER_FAMILY_UNKNOWN);
	g_assert_false (lrg_controller_monitor_is_connected (monitor));
	g_assert_null (lrg_controller_monitor_get_name (monitor));
}

static void
test_monitor_deck_and_override (void)
{
	g_autoptr(LrgInputMock) mock = NULL;
	g_autoptr(LrgControllerMonitor) monitor = NULL;
	Changes changes = { 0 };

	mock = lrg_input_mock_new ();
	lrg_input_mock_set_gamepad_available (mock, 0, TRUE);
	lrg_input_mock_set_gamepad_name (mock, 0, "Microsoft X-Box 360 pad 0");
	lrg_input_mock_set_gamepad_ids (mock, 0, 0x28de, 0x11ff);

	monitor = lrg_controller_monitor_new (LRG_INPUT (mock), 0);
	g_signal_connect (monitor, "family-changed", G_CALLBACK (on_family_changed), &changes);

	/* away from a Deck the virtual pad is Xbox 360 shaped */
	lrg_controller_monitor_set_steam_deck_host (monitor, FALSE);
	lrg_controller_monitor_poll (monitor);
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_XBOX_360);

	/* on the Deck it is the Deck */
	lrg_controller_monitor_set_steam_deck_host (monitor, TRUE);
	g_assert_true (lrg_controller_monitor_get_steam_deck_host (monitor));
	g_assert_true (lrg_controller_monitor_poll (monitor));
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_STEAM_DECK);
	g_assert_cmpint (changes.old_family, ==, LRG_CONTROLLER_FAMILY_XBOX_360);

	/* the user's choice wins while connected, and clears again */
	lrg_controller_monitor_set_family_override (monitor, LRG_CONTROLLER_FAMILY_STEAM_FRAME);
	g_assert_true (lrg_controller_monitor_poll (monitor));
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_STEAM_FRAME);
	g_assert_cmpint (lrg_controller_monitor_get_family_override (monitor), ==, LRG_CONTROLLER_FAMILY_STEAM_FRAME);
	lrg_controller_monitor_set_family_override (monitor, LRG_CONTROLLER_FAMILY_UNKNOWN);
	g_assert_true (lrg_controller_monitor_poll (monitor));
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_STEAM_DECK);

	/* on a Frame it is the Frame's controllers */
	lrg_controller_monitor_set_host_family (monitor, LRG_CONTROLLER_FAMILY_STEAM_FRAME);
	g_assert_cmpint (lrg_controller_monitor_get_host_family (monitor), ==, LRG_CONTROLLER_FAMILY_STEAM_FRAME);
	g_assert_false (lrg_controller_monitor_get_steam_deck_host (monitor));
	g_assert_true (lrg_controller_monitor_poll (monitor));
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_STEAM_FRAME);

	/* an override does not make a missing pad appear */
	lrg_controller_monitor_set_family_override (monitor, LRG_CONTROLLER_FAMILY_XBOX_ONE);
	lrg_input_mock_set_gamepad_available (mock, 0, FALSE);
	lrg_controller_monitor_poll (monitor);
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_UNKNOWN);
}

static void
test_monitor_env_override (void)
{
	g_autoptr(LrgInputMock) mock = NULL;
	g_autoptr(LrgControllerMonitor) monitor = NULL;

	g_setenv (LRG_CONTROLLER_ENV_FAMILY, "steam-frame", TRUE);
	mock = lrg_input_mock_new ();
	lrg_input_mock_set_gamepad_available (mock, 0, TRUE);
	lrg_input_mock_set_gamepad_name (mock, 0, "Xbox One Controller");
	monitor = lrg_controller_monitor_new (LRG_INPUT (mock), 0);
	g_unsetenv (LRG_CONTROLLER_ENV_FAMILY);

	lrg_controller_monitor_poll (monitor);
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_STEAM_FRAME);

	/* an unknown nick in the environment is ignored */
	g_setenv (LRG_CONTROLLER_ENV_FAMILY, "bogus", TRUE);
	g_clear_object (&monitor);
	monitor = lrg_controller_monitor_new (LRG_INPUT (mock), 0);
	g_unsetenv (LRG_CONTROLLER_ENV_FAMILY);
	lrg_controller_monitor_set_steam_deck_host (monitor, FALSE);
	lrg_controller_monitor_poll (monitor);
	g_assert_cmpint (lrg_controller_monitor_get_family (monitor), ==, LRG_CONTROLLER_FAMILY_XBOX_ONE);
}

static void
test_mock_names (void)
{
	g_autoptr(LrgInputMock) mock = NULL;
	guint16 vendor = 1;
	guint16 product = 1;

	mock = lrg_input_mock_new ();

	/* nothing connected: no name, no ids */
	g_assert_null (lrg_input_get_gamepad_name (LRG_INPUT (mock), 0));
	g_assert_false (lrg_input_get_gamepad_ids (LRG_INPUT (mock), 0, &vendor, &product));
	g_assert_cmpuint (vendor, ==, 0);

	lrg_input_mock_set_gamepad_available (mock, 2, TRUE);
	lrg_input_mock_set_gamepad_name (mock, 2, "Pad Two");
	g_assert_cmpstr (lrg_input_get_gamepad_name (LRG_INPUT (mock), 2), ==, "Pad Two");
	g_assert_false (lrg_input_get_gamepad_ids (LRG_INPUT (mock), 2, NULL, NULL));
	lrg_input_mock_reset (mock);
	g_assert_null (lrg_input_get_gamepad_name (LRG_INPUT (mock), 2));
}

/* ==========================================================================
 * Rumble (the failure paths: no force feedback hardware is attached)
 * ========================================================================== */

static void
test_rumble_paths (void)
{
	g_autofree gchar *root = NULL;
	g_autofree gchar *dev = NULL;
	g_autofree gchar *event_dir = NULL;
	g_autofree gchar *node = NULL;
	g_autofree gchar *found = NULL;
	g_autoptr(LrgControllerRumble) rumble = NULL;
	g_autoptr(LrgControllerRumble) missing = NULL;
	g_autoptr(GError) error = NULL;

	root = g_dir_make_tmp ("lrg-controller-ff-XXXXXX", NULL);
	dev = g_build_filename (root, "dev", NULL);
	g_assert_cmpint (g_mkdir_with_parents (dev, 0755), ==, 0);

	/* a pad whose input device has an event7 child, and a plain file as the node */
	write_input (root, "input3", "Test Rumble Pad\n", "045e\n", "0b12\n");
	event_dir = g_build_filename (root, "class", "input", "input3", "event7", NULL);
	g_assert_cmpint (g_mkdir_with_parents (event_dir, 0755), ==, 0);
	node = g_build_filename (dev, "event7", NULL);
	g_assert_true (g_file_set_contents (node, "", -1, NULL));

	rumble = lrg_controller_rumble_new_with_roots ("Test Rumble Pad", root, dev);
	found = lrg_controller_rumble_find_node (rumble);
	g_assert_cmpstr (found, ==, node);
	g_assert_false (lrg_controller_rumble_is_open (rumble));

	/* a file is not a force feedback device: soft failure, never open */
	g_assert_false (lrg_controller_rumble_play (rumble, 1.0, 0.5, 200, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_clear_error (&error);
	g_assert_false (lrg_controller_rumble_is_open (rumble));
	lrg_controller_rumble_stop (rumble);

	/* the failure is remembered: no retry storm on every hit */
	g_assert_false (lrg_controller_rumble_play (rumble, 1.0, 0.5, 200, &error));
	g_assert_nonnull (error);
	g_clear_error (&error);

	/* an unknown pad has no node */
	missing = lrg_controller_rumble_new_with_roots ("Nothing Here", root, dev);
	g_assert_null (lrg_controller_rumble_find_node (missing));
	g_assert_false (lrg_controller_rumble_play (missing, 1.0, 1.0, 100, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

	g_test_add_func ("/controller/detect/names", test_detect_names);
	g_test_add_func ("/controller/detect/ids", test_detect_ids);
	g_test_add_func ("/controller/detect/deck-host", test_detect_deck_host);
	g_test_add_func ("/controller/detect/frame-host", test_detect_frame_host);
	g_test_add_func ("/controller/family/names", test_family_names);
	g_test_add_func ("/controller/sysfs/probe", test_probe_usb_ids);
	g_test_add_func ("/controller/sysfs/deck-host", test_deck_host_detection);
	g_test_add_func ("/controller/sysfs/frame-host", test_frame_host_detection);
	g_test_add_func ("/controller/sysfs/is-gamepad", test_probe_is_gamepad);
	g_test_add_func ("/controller/glyphs", test_glyphs);
	g_test_add_func ("/controller/preset/lookup", test_preset_lookup);
	g_test_add_func ("/controller/preset/map", test_preset_map);
	g_test_add_func ("/controller/monitor/connect-swap-remove", test_monitor_connect_swap_remove);
	g_test_add_func ("/controller/monitor/deck-and-override", test_monitor_deck_and_override);
	g_test_add_func ("/controller/monitor/env-override", test_monitor_env_override);
	g_test_add_func ("/controller/mock/names", test_mock_names);
	g_test_add_func ("/controller/rumble/paths", test_rumble_paths);

	return g_test_run ();
}
