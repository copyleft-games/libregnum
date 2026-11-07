/* lrg-vscript-program.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVscriptProgram: (stub, being written)
 */

#include "config.h"

#include "lrg-vscript-program.h"

struct _LrgVscriptProgram
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgVscriptProgram, lrg_vscript_program, G_TYPE_OBJECT)

static void
lrg_vscript_program_class_init (LrgVscriptProgramClass *klass)
{
}

static void
lrg_vscript_program_init (LrgVscriptProgram *self)
{
}

LrgVscriptProgram *
lrg_vscript_program_new (void)
{
	return g_object_new (LRG_TYPE_VSCRIPT_PROGRAM, NULL);
}
