/* lrg-gui-raylib-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Include this instead of <raylib.h> / <rlgl.h> in code that calls raylib
 * functions returning bool.
 *
 * libregnum builds with -std=gnu89. There <raylib.h> has no <stdbool.h>
 * and declares `typedef enum bool {...} bool;`, an int-sized type, while
 * raylib itself (C99) returns a one-byte _Bool in %al. Callers then read
 * whatever is in the upper bytes of the return register: IsMouseButtonDown()
 * reported "down" with no button pressed, so clicks never had an edge,
 * menus never opened and drags never started. GCC accepts _Bool in gnu89
 * mode, so pulling in <stdbool.h> first makes raylib's prototypes match
 * its ABI.
 */

#pragma once

#include <stdbool.h>
#include <raylib.h>
#include <rlgl.h>
#include <glib.h>

G_STATIC_ASSERT (sizeof (bool) == 1);
