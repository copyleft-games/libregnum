/* lrg-logic-machine.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLogicMachine: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_LOGIC_MACHINE (lrg_logic_machine_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLogicMachine, lrg_logic_machine, LRG, LOGIC_MACHINE, GObject)

/**
 * lrg_logic_machine_new:
 *
 * Returns: (transfer full): a new #LrgLogicMachine
 */
LRG_AVAILABLE_IN_ALL
LrgLogicMachine * lrg_logic_machine_new (void);

G_END_DECLS
