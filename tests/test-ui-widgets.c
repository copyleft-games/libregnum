/* test-ui-widgets.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the built-in widgets after the move to paint() and part looks.
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
