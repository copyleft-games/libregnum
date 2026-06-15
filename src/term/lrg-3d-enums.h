/* lrg-3d-enums.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Enumerations for the 3D display surface (#Lrg3DSurface): the panel layout
 * "arrangement" and the ambient "environment".  These are the two configurable
 * axes (orthogonal to #LrgRenderMode) that select how the Emacs frame is staged
 * in the 3D scene.  Both are also exposed by string id through the mode registry
 * so future arrangements/environments can be added without growing these enums.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgArrangementKind:
 * @LRG_ARRANGEMENT_KIND_SINGLE_PANEL: The whole Emacs frame on one floating panel.
 * @LRG_ARRANGEMENT_KIND_PER_WINDOW: Each Emacs window on its own panel, laid out to
 *   mirror the frame's window tree (the workshop default).
 * @LRG_ARRANGEMENT_KIND_FREE: Per-window panels with independent, user-movable
 *   transforms (a spatial window manager).
 * @LRG_ARRANGEMENT_KIND_CAROUSEL: A ring of buffer thumbnails to fly through (the
 *   "Rolodex" switcher); a transient mode that restores the prior arrangement.
 * @LRG_ARRANGEMENT_KIND_GIT_DEPTH: Past revisions of the focused buffer stacked in Z
 *   (the git "time machine"); a transient mode.
 *
 * Selects how panels are laid out in an #Lrg3DSurface scene. Built-in kinds; the
 * mode registry maps these to layout strategies and accepts custom ids too.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ARRANGEMENT_KIND_SINGLE_PANEL = 0,
    LRG_ARRANGEMENT_KIND_PER_WINDOW = 1,
    LRG_ARRANGEMENT_KIND_FREE = 2,
    LRG_ARRANGEMENT_KIND_CAROUSEL = 3,
    LRG_ARRANGEMENT_KIND_GIT_DEPTH = 4
} LrgArrangementKind;

LRG_AVAILABLE_IN_ALL
GType lrg_arrangement_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ARRANGEMENT_KIND (lrg_arrangement_kind_get_type ())

/**
 * lrg_arrangement_kind_to_string:
 * @kind: a #LrgArrangementKind
 *
 * Returns the canonical id token for @kind ("single-panel", "per-window",
 * "free", "carousel", "git-depth").
 *
 * Returns: (transfer none): a static string; never %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_arrangement_kind_to_string (LrgArrangementKind kind);

/**
 * lrg_arrangement_kind_from_string:
 * @str: (nullable): an id token; %NULL or unknown -> single-panel
 * @out_kind: (out): return location for the parsed kind
 *
 * Returns: %TRUE if @str was a recognised token, %FALSE otherwise (in which case
 *   @out_kind is set to %LRG_ARRANGEMENT_KIND_SINGLE_PANEL)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_arrangement_kind_from_string (const gchar        *str,
                                           LrgArrangementKind *out_kind);

/**
 * LrgEnvironmentKind:
 * @LRG_ENVIRONMENT_KIND_VOID: A plain dark backdrop (cheapest).
 * @LRG_ENVIRONMENT_KIND_WORKSHOP: A "spatial editor" room: grid floor and soft key
 *   light, panels on a workbench plane.
 * @LRG_ENVIRONMENT_KIND_COCKPIT: An "ambient cockpit": live dashboards/views (e.g.
 *   the gnuseye globe) drawn as walls around the panels.
 *
 * Selects the ambient scene drawn behind/around the panels in an #Lrg3DSurface.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_ENVIRONMENT_KIND_VOID = 0,
    LRG_ENVIRONMENT_KIND_WORKSHOP = 1,
    LRG_ENVIRONMENT_KIND_COCKPIT = 2
} LrgEnvironmentKind;

LRG_AVAILABLE_IN_ALL
GType lrg_environment_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_ENVIRONMENT_KIND (lrg_environment_kind_get_type ())

/**
 * lrg_environment_kind_to_string:
 * @kind: a #LrgEnvironmentKind
 *
 * Returns the canonical id token for @kind ("void", "workshop", "cockpit").
 *
 * Returns: (transfer none): a static string; never %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_environment_kind_to_string (LrgEnvironmentKind kind);

/**
 * lrg_environment_kind_from_string:
 * @str: (nullable): an id token; %NULL or unknown -> void
 * @out_kind: (out): return location for the parsed kind
 *
 * Returns: %TRUE if @str was a recognised token, %FALSE otherwise (in which case
 *   @out_kind is set to %LRG_ENVIRONMENT_KIND_VOID)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_environment_kind_from_string (const gchar        *str,
                                           LrgEnvironmentKind *out_kind);

G_END_DECLS
