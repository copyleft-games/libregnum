/* lrg-camera-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCameraComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-camera-component.h"

struct _LrgCameraComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgCameraComponent, lrg_camera_component, G_TYPE_OBJECT)

static void
lrg_camera_component_class_init (LrgCameraComponentClass *klass)
{
}

static void
lrg_camera_component_init (LrgCameraComponent *self)
{
}

LrgCameraComponent *
lrg_camera_component_new (void)
{
	return g_object_new (LRG_TYPE_CAMERA_COMPONENT, NULL);
}
