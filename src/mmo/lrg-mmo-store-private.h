/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "lrg-mmo-store.h"
#include <sqlite3.h>
G_GNUC_INTERNAL sqlite3 *_lrg_mmo_store_database (LrgMmoStore *self);
