/* lrg-switcher.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSwitcher: shows one child at a time, with spring-driven transitions.
 *
 * A switcher holds pages (any widgets) and shows exactly one of them;
 * the others are neither laid out, painted nor hit-tested. Pages can
 * be named so menus and actions can switch by name (the "ui.page"
 * action sets #LrgSwitcher:visible-child-name). Switching animates on
 * a spring: crossfade, slides (fixed or following page order, mirrored
 * right to left) and zoom. During a transition both pages paint, but
 * only the incoming page receives input.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgSwitcherTransition
 * ========================================================================== */

/**
 * LrgSwitcherTransition:
 * @LRG_SWITCHER_TRANSITION_NONE: switch instantly
 * @LRG_SWITCHER_TRANSITION_CROSSFADE: fade the old page out while the
 *   new one fades in
 * @LRG_SWITCHER_TRANSITION_SLIDE_LEFT: the new page slides in from the
 *   right, pushing the old one out to the left
 * @LRG_SWITCHER_TRANSITION_SLIDE_RIGHT: the new page slides in from
 *   the left, pushing the old one out to the right
 * @LRG_SWITCHER_TRANSITION_SLIDE_UP: the new page slides in from the
 *   bottom, pushing the old one up
 * @LRG_SWITCHER_TRANSITION_SLIDE_DOWN: the new page slides in from the
 *   top, pushing the old one down
 * @LRG_SWITCHER_TRANSITION_SLIDE: slide left when moving to a later
 *   page and right when moving to an earlier one (mirrored right to
 *   left), like a carousel
 * @LRG_SWITCHER_TRANSITION_ZOOM: the old page grows and fades away
 *   while the new one grows into place
 *
 * How an #LrgSwitcher animates between pages.
 */
typedef enum
{
	LRG_SWITCHER_TRANSITION_NONE,
	LRG_SWITCHER_TRANSITION_CROSSFADE,
	LRG_SWITCHER_TRANSITION_SLIDE_LEFT,
	LRG_SWITCHER_TRANSITION_SLIDE_RIGHT,
	LRG_SWITCHER_TRANSITION_SLIDE_UP,
	LRG_SWITCHER_TRANSITION_SLIDE_DOWN,
	LRG_SWITCHER_TRANSITION_SLIDE,
	LRG_SWITCHER_TRANSITION_ZOOM
} LrgSwitcherTransition;

LRG_AVAILABLE_IN_ALL
GType lrg_switcher_transition_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SWITCHER_TRANSITION (lrg_switcher_transition_get_type ())

/* ==========================================================================
 * LrgSwitcher
 * ========================================================================== */

#define LRG_TYPE_SWITCHER (lrg_switcher_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSwitcher, lrg_switcher, LRG, SWITCHER, LrgContainer)

/**
 * lrg_switcher_new:
 *
 * Creates an empty switcher with a crossfade transition, homogeneous
 * sizing and no size interpolation. Add pages with
 * lrg_switcher_add_named() or lrg_container_add_child(); the first
 * page added becomes the visible one.
 *
 * Returns: (transfer full): a new #LrgSwitcher
 */
LRG_AVAILABLE_IN_ALL
LrgSwitcher * lrg_switcher_new (void);

/**
 * lrg_switcher_add_named:
 * @self: an #LrgSwitcher
 * @child: the page to add
 * @name: (nullable): name of the page, unique within the switcher
 *
 * Adds @child as a page called @name. Equivalent to setting the name
 * with lrg_switcher_set_child_name() and adding the child with
 * lrg_container_add_child().
 */
LRG_AVAILABLE_IN_ALL
void lrg_switcher_add_named (LrgSwitcher *self,
                             LrgWidget   *child,
                             const gchar *name);

/**
 * lrg_switcher_get_child_name:
 * @self: an #LrgSwitcher
 * @child: a page of @self
 *
 * Returns: (nullable): the name of @child, or %NULL if unnamed
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_switcher_get_child_name (LrgSwitcher *self,
                                           LrgWidget   *child);

/**
 * lrg_switcher_set_child_name:
 * @self: an #LrgSwitcher
 * @child: a page of @self
 * @name: (nullable): the new name
 *
 * Names (or renames) a page. Notifies
 * #LrgSwitcher:visible-child-name when @child is the visible page.
 */
LRG_AVAILABLE_IN_ALL
void lrg_switcher_set_child_name (LrgSwitcher *self,
                                  LrgWidget   *child,
                                  const gchar *name);

/**
 * lrg_switcher_get_child_by_name:
 * @self: an #LrgSwitcher
 * @name: a page name
 *
 * Returns: (transfer none) (nullable): the first page called @name
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_switcher_get_child_by_name (LrgSwitcher *self,
                                            const gchar *name);

/**
 * lrg_switcher_get_visible_child:
 * @self: an #LrgSwitcher
 *
 * Returns: (transfer none) (nullable): the page shown (or being
 *   switched to), %NULL when the switcher has no pages
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_switcher_get_visible_child (LrgSwitcher *self);

/**
 * lrg_switcher_set_visible_child:
 * @self: an #LrgSwitcher
 * @child: a page of @self
 *
 * Shows @child with the switcher's #LrgSwitcher:transition.
 */
LRG_AVAILABLE_IN_ALL
void lrg_switcher_set_visible_child (LrgSwitcher *self,
                                     LrgWidget   *child);

/**
 * lrg_switcher_set_visible_child_full:
 * @self: an #LrgSwitcher
 * @child: a page of @self
 * @transition: the transition to use for this switch only
 *
 * Shows @child with @transition instead of the switcher's default.
 * Switching while a transition runs reverses it smoothly when @child
 * is the page being left; any other page restarts the animation.
 * The very first switch before the switcher was ever stepped, and any
 * switch under reduced motion (lrg_motion_get_reduced()) or with the
 * widget's motion set to %LRG_MOTION_NONE, is instant.
 */
LRG_AVAILABLE_IN_ALL
void lrg_switcher_set_visible_child_full (LrgSwitcher           *self,
                                          LrgWidget             *child,
                                          LrgSwitcherTransition  transition);

/**
 * lrg_switcher_get_visible_child_name:
 * @self: an #LrgSwitcher
 *
 * Returns: (nullable): the name of the visible page
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_switcher_get_visible_child_name (LrgSwitcher *self);

/**
 * lrg_switcher_set_visible_child_name:
 * @self: an #LrgSwitcher
 * @name: name of the page to show
 *
 * Shows the page called @name. Warns and does nothing if no page has
 * that name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_switcher_set_visible_child_name (LrgSwitcher *self,
                                          const gchar *name);

/**
 * lrg_switcher_get_visible_index:
 * @self: an #LrgSwitcher
 *
 * Returns: position of the visible page among the children, -1 when
 *   there is none
 */
LRG_AVAILABLE_IN_ALL
gint lrg_switcher_get_visible_index (LrgSwitcher *self);

/**
 * lrg_switcher_set_visible_index:
 * @self: an #LrgSwitcher
 * @index: position of the page to show
 *
 * Shows the page at @index. Out-of-range values are ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_switcher_set_visible_index (LrgSwitcher *self,
                                     gint         index);

/**
 * lrg_switcher_get_transition:
 * @self: an #LrgSwitcher
 *
 * Returns: the default transition
 */
LRG_AVAILABLE_IN_ALL
LrgSwitcherTransition lrg_switcher_get_transition (LrgSwitcher *self);

/**
 * lrg_switcher_set_transition:
 * @self: an #LrgSwitcher
 * @transition: the default transition
 *
 * Sets how page switches animate. The spring follows the widget's
 * motion (lrg_widget_set_motion(), inherited from the theme).
 */
LRG_AVAILABLE_IN_ALL
void lrg_switcher_set_transition (LrgSwitcher           *self,
                                  LrgSwitcherTransition  transition);

/**
 * lrg_switcher_get_homogeneous:
 * @self: an #LrgSwitcher
 *
 * Returns: whether the switcher asks for the size of its largest page
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_switcher_get_homogeneous (LrgSwitcher *self);

/**
 * lrg_switcher_set_homogeneous:
 * @self: an #LrgSwitcher
 * @homogeneous: size for the largest page
 *
 * A homogeneous switcher (the default) requests the size of its
 * largest page, so switching never resizes it; otherwise it requests
 * the size of the visible page.
 */
LRG_AVAILABLE_IN_ALL
void lrg_switcher_set_homogeneous (LrgSwitcher *self,
                                   gboolean     homogeneous);

/**
 * lrg_switcher_get_interpolate_size:
 * @self: an #LrgSwitcher
 *
 * Returns: whether the requested size springs between pages
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_switcher_get_interpolate_size (LrgSwitcher *self);

/**
 * lrg_switcher_set_interpolate_size:
 * @self: an #LrgSwitcher
 * @interpolate_size: animate the requested size
 *
 * When the switcher is not homogeneous, glide its requested size from
 * the old page's to the new page's on a spring instead of jumping, so
 * a parent box grows or shrinks smoothly.
 */
LRG_AVAILABLE_IN_ALL
void lrg_switcher_set_interpolate_size (LrgSwitcher *self,
                                        gboolean     interpolate_size);

/**
 * lrg_switcher_get_transition_running:
 * @self: an #LrgSwitcher
 *
 * Returns: %TRUE while a page transition is animating
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_switcher_get_transition_running (LrgSwitcher *self);

/**
 * lrg_switcher_get_transition_progress:
 * @self: an #LrgSwitcher
 *
 * Returns: progress of the running transition (0 at the start, 1 when
 *   done; a bouncy motion may overshoot), 1 when none is running
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_switcher_get_transition_progress (LrgSwitcher *self);

/**
 * lrg_switcher_get_previous_child:
 * @self: an #LrgSwitcher
 *
 * Returns: (transfer none) (nullable): the page being left while a
 *   transition runs, %NULL otherwise
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_switcher_get_previous_child (LrgSwitcher *self);

G_END_DECLS
