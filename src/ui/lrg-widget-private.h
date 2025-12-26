/* lrg-widget-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private header for LrgWidget internals.
 * Only include this from UI module implementation files.
 */

#pragma once

#include "lrg-widget.h"

G_BEGIN_DECLS

/*
 * _lrg_widget_set_parent:
 * @self: an #LrgWidget
 * @parent: (nullable): the new parent, or %NULL to unparent
 *
 * Sets the widget's parent container.
 *
 * This function is called by LrgContainer when adding or removing
 * children. Do not call this function directly - use
 * lrg_container_add_child() and lrg_container_remove_child() instead.
 */
void _lrg_widget_set_parent (LrgWidget    *self,
                             LrgContainer *parent);

G_END_DECLS
