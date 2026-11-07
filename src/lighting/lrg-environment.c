/* lrg-environment.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEnvironment: (stub, being written)
 */

#include "config.h"

#include "lrg-environment.h"
#include "lrg-environment-glsl-private.h"

struct _LrgEnvironment
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEnvironment, lrg_environment, G_TYPE_OBJECT)

static void
lrg_environment_class_init (LrgEnvironmentClass *klass)
{
}

static void
lrg_environment_init (LrgEnvironment *self)
{
}

LrgEnvironment *
lrg_environment_new (void)
{
	return g_object_new (LRG_TYPE_ENVIRONMENT, NULL);
}

/* stub binder (lrg-environment-glsl-private.h) */
void
lrg_environment_bind_private (LrgEnvironment *env,
                              guint shader_id,
                              gint *texture_unit)
{
	(void)env;
	(void)shader_id;
	(void)texture_unit;
}
