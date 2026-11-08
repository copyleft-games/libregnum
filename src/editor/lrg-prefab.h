/* lrg-prefab.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Compatibility forwarding header.  Since libregnum 0.9.0 this file lives in
 * the always-built runtime level module: src/level/lrg-prefab.h.  The old
 * src/editor/ path keeps working for in-tree code (and for installed
 * <libregnum/editor/...> includes) by forwarding there.  New code should
 * include <libregnum.h> (or "level/lrg-prefab.h" inside the library).
 *
 * This header is excluded from the GObject-Introspection scan (see
 * GIR_EXCLUDE in the root Makefile); the real declarations are scanned from
 * src/level/.
 */

#pragma once

#include "../level/lrg-prefab.h"
