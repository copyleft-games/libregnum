/* lrg-panel-environment.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-panel-environment.h"

G_DEFINE_INTERFACE (LrgPanelEnvironment, lrg_panel_environment, G_TYPE_OBJECT)

static void
lrg_panel_environment_default_init (LrgPanelEnvironmentInterface *iface)
{
	(void) iface;
}

const gchar *
lrg_panel_environment_get_id (LrgPanelEnvironment *self)
{
	LrgPanelEnvironmentInterface *iface;

	g_return_val_if_fail (LRG_IS_PANEL_ENVIRONMENT (self), NULL);

	iface = LRG_PANEL_ENVIRONMENT_GET_IFACE (self);
	return iface->get_id != NULL ? iface->get_id (self) : NULL;
}

GrlColor *
lrg_panel_environment_get_clear_color (LrgPanelEnvironment *self)
{
	LrgPanelEnvironmentInterface *iface;

	g_return_val_if_fail (LRG_IS_PANEL_ENVIRONMENT (self), NULL);

	iface = LRG_PANEL_ENVIRONMENT_GET_IFACE (self);
	if (iface->get_clear_color != NULL)
		return iface->get_clear_color (self);

	/* Sensible default: the near-black the spike used. */
	return grl_color_new (18, 18, 22, 255);
}

void
lrg_panel_environment_draw_ambient (LrgPanelEnvironment *self)
{
	LrgPanelEnvironmentInterface *iface;

	g_return_if_fail (LRG_IS_PANEL_ENVIRONMENT (self));

	iface = LRG_PANEL_ENVIRONMENT_GET_IFACE (self);
	if (iface->draw_ambient != NULL)
		iface->draw_ambient (self);
}
