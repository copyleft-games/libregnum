/* lrg-node-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private compatibility forwarding header.  Since libregnum 0.9.0 the real
 * header lives in the always-built runtime level module
 * (src/level/lrg-node-private.h); editor sources that include it by its old
 * name keep compiling through this file.  Never installed.
 */

#pragma once

#include "../level/lrg-node-private.h"
