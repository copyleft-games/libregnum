#!/bin/bash
# mktest.sh tests/test-NAME.c
[ -e "$1" ] && { echo "mktest: $1 exists, refusing to overwrite" >&2; exit 1; }
cat > "$1" <<T
/* $(basename $1) (stub, being written)
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <glib.h>
#include <libregnum.h>

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	return g_test_run ();
}
T
