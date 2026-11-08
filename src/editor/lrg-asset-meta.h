/* lrg-asset-meta.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Compatibility forwarding header.  Since libregnum 0.9.0 the ".meta"
 * sidecar reader lives in the always-built core module
 * (src/core/lrg-asset-meta.h) so LrgAssetManager can apply importer
 * settings at runtime with BUILD_EDITOR=0.  The old src/editor/ path keeps
 * working for in-tree code (and for installed <libregnum/editor/...>
 * includes) by forwarding there.  New code should include <libregnum.h>
 * (or "core/lrg-asset-meta.h" inside the library).
 *
 * This header is excluded from the GObject-Introspection scan (see
 * GIR_EXCLUDE in the root Makefile); the real declarations are scanned from
 * src/core/.
 */

#pragma once

#include "../core/lrg-asset-meta.h"
