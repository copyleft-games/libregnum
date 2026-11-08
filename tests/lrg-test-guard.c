/* lrg-test-guard.c - Keep every test binary off the desktop
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * tests/Makefile links this object into every test binary. Its constructor
 * runs before main(), so no test has to remember to set anything itself,
 * and a test binary run by hand (outside `make run`) is covered too.
 *
 * What it does:
 *   LRG_EXTERNAL_EDITOR=none  lrg_external_editor_open() starts nothing,
 *                             so no test opens $VISUAL (an Emacs frame)
 *                             or the desktop default application.
 *
 * A test that must really spawn a program (test-editor-tools, which spawns
 * `true`) unsets the variable around that one call.
 */

#include <glib.h>

/* Read by test-editor-tools to prove the guard is linked in. */
const gchar lrg_test_guard_marker[] = "lrg-test-guard";

static void lrg_test_guard_init (void) __attribute__ ((constructor));

/*
 * lrg_test_guard_init:
 *
 * Runs before main(). Sets LRG_EXTERNAL_EDITOR=none, overwriting any value
 * from the caller's environment: a test never opens a real editor.
 */
static void
lrg_test_guard_init (void)
{
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);
}
