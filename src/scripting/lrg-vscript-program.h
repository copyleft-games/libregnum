/* lrg-vscript-program.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVscriptProgram: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_VSCRIPT_PROGRAM (lrg_vscript_program_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVscriptProgram, lrg_vscript_program, LRG, VSCRIPT_PROGRAM, GObject)

/**
 * lrg_vscript_program_new:
 *
 * Returns: (transfer full): a new #LrgVscriptProgram
 */
LRG_AVAILABLE_IN_ALL
LrgVscriptProgram * lrg_vscript_program_new (void);

G_END_DECLS
