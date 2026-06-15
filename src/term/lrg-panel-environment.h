/* lrg-panel-environment.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Strategy interface for the ambient scene drawn behind/around an
 * #Lrg3DSurface's panels: a background clear colour plus optional 3D ambient
 * geometry (a workshop floor, cockpit walls, ...).  New environments are added
 * by implementing this interface.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_PANEL_ENVIRONMENT (lrg_panel_environment_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgPanelEnvironment, lrg_panel_environment, LRG, PANEL_ENVIRONMENT, GObject)

/**
 * LrgPanelEnvironmentInterface:
 * @parent_iface: the parent interface
 * @get_id: return the stable string id of this environment
 * @get_clear_color: return the framebuffer clear colour (transfer full)
 * @draw_ambient: draw 3D ambient geometry inside the current camera scope
 *
 * Since: 1.0
 */
struct _LrgPanelEnvironmentInterface
{
    GTypeInterface parent_iface;

    const gchar * (*get_id)          (LrgPanelEnvironment *self);
    GrlColor *    (*get_clear_color) (LrgPanelEnvironment *self);
    void          (*draw_ambient)    (LrgPanelEnvironment *self);
};

LRG_AVAILABLE_IN_ALL
const gchar * lrg_panel_environment_get_id (LrgPanelEnvironment *self);

/**
 * lrg_panel_environment_get_clear_color:
 * @self: a #LrgPanelEnvironment
 *
 * Returns: (transfer full): the colour the surface should clear to before
 *   drawing the 3D scene
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlColor * lrg_panel_environment_get_clear_color (LrgPanelEnvironment *self);

/**
 * lrg_panel_environment_draw_ambient:
 * @self: a #LrgPanelEnvironment
 *
 * Draws ambient 3D geometry (floor, walls, ...) inside the active camera's 3D
 * scope. Called between the camera begin and the panel draws.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_panel_environment_draw_ambient (LrgPanelEnvironment *self);

G_END_DECLS
