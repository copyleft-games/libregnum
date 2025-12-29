/* lrg-ik-solver.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * IK solver interface and implementations.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-ik-chain.h"

G_BEGIN_DECLS

#define LRG_TYPE_IK_SOLVER (lrg_ik_solver_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgIKSolver, lrg_ik_solver, LRG, IK_SOLVER, GObject)

/**
 * LrgIKSolverClass:
 * @parent_class: Parent class
 * @solve: Virtual method to solve the IK chain
 * @supports_chain_length: Check if solver supports given chain length
 *
 * Class structure for #LrgIKSolver.
 */
struct _LrgIKSolverClass
{
    GObjectClass parent_class;

    /*< public >*/
    gboolean (*solve)                (LrgIKSolver *solver,
                                      LrgIKChain  *chain,
                                      guint        max_iterations,
                                      gfloat       tolerance);
    gboolean (*supports_chain_length)(LrgIKSolver *solver,
                                      guint        bone_count);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_ik_solver_solve:
 * @solver: A #LrgIKSolver
 * @chain: The IK chain to solve
 * @max_iterations: Maximum solver iterations
 * @tolerance: Distance tolerance for convergence
 *
 * Solves the IK chain to reach its target.
 *
 * Returns: %TRUE if solution converged, %FALSE otherwise
 */
LRG_AVAILABLE_IN_ALL
gboolean    lrg_ik_solver_solve                 (LrgIKSolver *solver,
                                                 LrgIKChain  *chain,
                                                 guint        max_iterations,
                                                 gfloat       tolerance);

/**
 * lrg_ik_solver_supports_chain_length:
 * @solver: A #LrgIKSolver
 * @bone_count: Number of bones in chain
 *
 * Checks if this solver supports the given chain length.
 *
 * Returns: %TRUE if supported
 */
LRG_AVAILABLE_IN_ALL
gboolean    lrg_ik_solver_supports_chain_length (LrgIKSolver *solver,
                                                 guint        bone_count);

/*
 * LrgIKSolverFABRIK - Forward And Backward Reaching Inverse Kinematics
 *
 * Works with any chain length, iteratively adjusts bone positions.
 */

#define LRG_TYPE_IK_SOLVER_FABRIK (lrg_ik_solver_fabrik_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIKSolverFABRIK, lrg_ik_solver_fabrik, LRG, IK_SOLVER_FABRIK, LrgIKSolver)

/**
 * lrg_ik_solver_fabrik_new:
 *
 * Creates a new FABRIK IK solver.
 * FABRIK works with chains of any length.
 *
 * Returns: (transfer full): A new #LrgIKSolverFABRIK
 */
LRG_AVAILABLE_IN_ALL
LrgIKSolverFABRIK * lrg_ik_solver_fabrik_new (void);

/*
 * LrgIKSolverCCD - Cyclic Coordinate Descent
 *
 * Works with any chain length, rotates bones from tip to root.
 */

#define LRG_TYPE_IK_SOLVER_CCD (lrg_ik_solver_ccd_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIKSolverCCD, lrg_ik_solver_ccd, LRG, IK_SOLVER_CCD, LrgIKSolver)

/**
 * lrg_ik_solver_ccd_new:
 *
 * Creates a new CCD IK solver.
 * CCD works with chains of any length.
 *
 * Returns: (transfer full): A new #LrgIKSolverCCD
 */
LRG_AVAILABLE_IN_ALL
LrgIKSolverCCD *    lrg_ik_solver_ccd_new    (void);

/*
 * LrgIKSolverTwoBone - Analytical two-bone solver
 *
 * Only works with exactly 2 bones (e.g., arm or leg).
 * Uses analytical solution for fast, exact results.
 */

#define LRG_TYPE_IK_SOLVER_TWO_BONE (lrg_ik_solver_two_bone_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIKSolverTwoBone, lrg_ik_solver_two_bone, LRG, IK_SOLVER_TWO_BONE, LrgIKSolver)

/**
 * lrg_ik_solver_two_bone_new:
 *
 * Creates a new two-bone IK solver.
 * Only works with exactly 2 bones in the chain.
 *
 * Returns: (transfer full): A new #LrgIKSolverTwoBone
 */
LRG_AVAILABLE_IN_ALL
LrgIKSolverTwoBone * lrg_ik_solver_two_bone_new (void);

/*
 * LrgIKSolverLookAt - Simple aim constraint
 *
 * Works with a single bone, rotates to face target.
 */

#define LRG_TYPE_IK_SOLVER_LOOK_AT (lrg_ik_solver_look_at_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgIKSolverLookAt, lrg_ik_solver_look_at, LRG, IK_SOLVER_LOOK_AT, LrgIKSolver)

/**
 * lrg_ik_solver_look_at_new:
 *
 * Creates a new look-at IK solver.
 * Only works with a single bone in the chain.
 *
 * Returns: (transfer full): A new #LrgIKSolverLookAt
 */
LRG_AVAILABLE_IN_ALL
LrgIKSolverLookAt * lrg_ik_solver_look_at_new (void);

/**
 * lrg_ik_solver_look_at_get_up_vector:
 * @self: A #LrgIKSolverLookAt
 * @x: (out): X component
 * @y: (out): Y component
 * @z: (out): Z component
 *
 * Gets the up vector for look-at calculation.
 */
LRG_AVAILABLE_IN_ALL
void        lrg_ik_solver_look_at_get_up_vector (LrgIKSolverLookAt *self,
                                                 gfloat            *x,
                                                 gfloat            *y,
                                                 gfloat            *z);

/**
 * lrg_ik_solver_look_at_set_up_vector:
 * @self: A #LrgIKSolverLookAt
 * @x: X component
 * @y: Y component
 * @z: Z component
 *
 * Sets the up vector for look-at calculation.
 */
LRG_AVAILABLE_IN_ALL
void        lrg_ik_solver_look_at_set_up_vector (LrgIKSolverLookAt *self,
                                                 gfloat             x,
                                                 gfloat             y,
                                                 gfloat             z);

G_END_DECLS
