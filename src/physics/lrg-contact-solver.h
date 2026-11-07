/* lrg-contact-solver.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgContactSolver: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_CONTACT_SOLVER (lrg_contact_solver_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgContactSolver, lrg_contact_solver, LRG, CONTACT_SOLVER, GObject)

/**
 * lrg_contact_solver_new:
 *
 * Returns: (transfer full): a new #LrgContactSolver
 */
LRG_AVAILABLE_IN_ALL
LrgContactSolver * lrg_contact_solver_new (void);

G_END_DECLS
