/* lrg-collision-info-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCollisionInfo layout, so LrgPhysicsWorld can build the info it emits
 * on the stack. Not installed, not introspected.
 */

#pragma once

#include "lrg-collision-info.h"

G_BEGIN_DECLS

/*
 * LrgCollisionInfo:
 *
 * Structure containing collision information between two bodies.
 */
struct _LrgCollisionInfo
{
    GObject *body_a;        /* First body in collision */
    GObject *body_b;        /* Second body in collision */
    gfloat   normal_x;      /* Normal X (from A to B) */
    gfloat   normal_y;      /* Normal Y (from A to B) */
    gfloat   penetration;   /* Penetration depth */
    gfloat   contact_x;     /* Contact point X */
    gfloat   contact_y;     /* Contact point Y */
};

G_END_DECLS
