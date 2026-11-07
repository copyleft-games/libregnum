#!/bin/bash
# mkstub.sh DIR STEM Type snake UPPER Parent PARENT_GTYPE "extra include line"
set -euo pipefail
dir=$1 s=$2 T=$3 sn=$4 U=$5 P=$6 PG=$7 inc=${8:-}
for f in "$dir/lrg-$s.h" "$dir/lrg-$s.c"; do [ -e "$f" ] && { echo "mkstub: $f exists, refusing" >&2; exit 1; }; done
cat > "$dir/lrg-$s.h" <<H
/* lrg-$s.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * $T: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
$inc

G_BEGIN_DECLS

#define LRG_TYPE_$U (lrg_${sn}_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE ($T, lrg_$sn, LRG, $U, $P)

/**
 * lrg_${sn}_new:
 *
 * Returns: (transfer full): a new #$T
 */
LRG_AVAILABLE_IN_ALL
$T * lrg_${sn}_new (void);

G_END_DECLS
H
cat > "$dir/lrg-$s.c" <<C
/* lrg-$s.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * $T: (stub, being written)
 */

#include "config.h"

#include "lrg-$s.h"

struct _$T
{
	$P parent_instance;
};

G_DEFINE_FINAL_TYPE ($T, lrg_$sn, $PG)

static void
lrg_${sn}_class_init (${T}Class *klass)
{
}

static void
lrg_${sn}_init ($T *self)
{
}

$T *
lrg_${sn}_new (void)
{
	return g_object_new (LRG_TYPE_$U, NULL);
}
C
