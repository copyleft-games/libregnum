/* test-ui-data-widgets.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Table, tree view, tile view, splitter, expander, wrap box.
 */

#include <glib.h>
#include <libregnum.h>

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	return g_test_run ();
}
