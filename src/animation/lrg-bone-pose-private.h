/* lrg-bone-pose-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Non-allocating LrgBonePose helpers for the animation hot paths. Not
 * installed, not introspected.
 */

#pragma once

#include "lrg-bone-pose.h"

G_BEGIN_DECLS

/*
 * lrg_bone_pose_compose_into:
 * @parent: the parent's world pose
 * @local: the child's local pose
 * @result: (out caller-allocates): where the child's world pose goes
 *
 * Exactly lrg_bone_pose_compose(), written into @result instead of a new
 * allocation. @result may alias @parent or @local.
 */
G_GNUC_INTERNAL
void lrg_bone_pose_compose_into (const LrgBonePose *parent,
                                 const LrgBonePose *local,
                                 LrgBonePose       *result);

G_END_DECLS
