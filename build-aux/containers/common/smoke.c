/* smoke.c - build container toolchain smoke test
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Every build container compiles, links and (where the target can run
 * here) runs this program while the image is built, so an image whose
 * cross toolchain or GLib stack is broken is never tagged.  It exercises
 * what libregnum needs from the target's GLib: a GObject type, GIO and
 * json-glib.  Built gnu89 with the project's warning flags.
 */

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

int
main(
	int     argc,
	char  **argv
){
	g_autoptr(JsonParser) parser = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;
	JsonNode *root;
	gint64 answer;

	(void)argc;
	(void)argv;

	/* json-glib on top of GObject */
	parser = json_parser_new();
	if (!json_parser_load_from_data(parser, "{\"answer\": 42}", -1, &error))
	{
		g_printerr("smoke: json: %s\n", error->message);
		return 1;
	}
	root = json_parser_get_root(parser);
	answer = json_object_get_int_member(json_node_get_object(root), "answer");

	/* GIO's GFile (no I/O, just the type system and path handling) */
	file = g_file_new_for_path("/tmp/smoke");

	if (answer != 42 || !G_IS_FILE(file))
	{
		g_printerr("smoke: unexpected result\n");
		return 1;
	}

	g_print("smoke: GLib %u.%u.%u, json-glib and GIO work\n",
	        glib_major_version, glib_minor_version, glib_micro_version);
	return 0;
}
