/* lrg-ssao.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSsao: (stub, being written)
 */

#include "config.h"

#include "lrg-ssao.h"
#include "lrg-ssao-glsl-private.h"

struct _LrgSsao
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSsao, lrg_ssao, G_TYPE_OBJECT)

static void
lrg_ssao_class_init (LrgSsaoClass *klass)
{
}

static void
lrg_ssao_init (LrgSsao *self)
{
}

LrgSsao *
lrg_ssao_new (void)
{
	return g_object_new (LRG_TYPE_SSAO, NULL);
}

/* stub binder (lrg-ssao-glsl-private.h) */
void
lrg_ssao_bind_private (LrgSsao *ssao,
                       guint shader_id,
                       gint *texture_unit)
{
	(void)ssao;
	(void)shader_id;
	(void)texture_unit;
}
