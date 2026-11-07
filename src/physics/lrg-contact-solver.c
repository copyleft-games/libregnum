/* lrg-contact-solver.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgContactSolver: (stub, being written)
 */

#include "config.h"

#include "lrg-contact-solver.h"

struct _LrgContactSolver
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgContactSolver, lrg_contact_solver, G_TYPE_OBJECT)

static void
lrg_contact_solver_class_init (LrgContactSolverClass *klass)
{
}

static void
lrg_contact_solver_init (LrgContactSolver *self)
{
}

LrgContactSolver *
lrg_contact_solver_new (void)
{
	return g_object_new (LRG_TYPE_CONTACT_SOLVER, NULL);
}
