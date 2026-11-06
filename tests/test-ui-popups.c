/* test-ui-popups.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for combo box, dialog, toast and toast queue (popup-layer widgets).
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
