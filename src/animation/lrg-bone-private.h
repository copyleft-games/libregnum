/* lrg-bone-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBone instance layout, shared with LrgSkeleton so its per-frame pose
 * passes can read and write bone poses without a type-checked accessor
 * call per field. Not installed, not introspected.
 */

#pragma once

#include "lrg-bone.h"

G_BEGIN_DECLS

struct _LrgBone
{
    GObject parent_instance;

    gchar       *name;
    gint         index;
    gint         parent_index;
    gfloat       length;

    LrgBonePose  bind_pose;
    LrgBonePose  local_pose;
    LrgBonePose  world_pose;
};

G_END_DECLS
