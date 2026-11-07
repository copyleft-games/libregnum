/* lrg-logic-machine.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLogicMachine: (stub, being written)
 */

#include "config.h"

#include "lrg-logic-machine.h"

struct _LrgLogicMachine
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgLogicMachine, lrg_logic_machine, G_TYPE_OBJECT)

static void
lrg_logic_machine_class_init (LrgLogicMachineClass *klass)
{
}

static void
lrg_logic_machine_init (LrgLogicMachine *self)
{
}

LrgLogicMachine *
lrg_logic_machine_new (void)
{
	return g_object_new (LRG_TYPE_LOGIC_MACHINE, NULL);
}
