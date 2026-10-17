/* lrg-script-task-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * What #LrgScriptScheduler does to its tasks.  Not installed, not
 * scanned.
 */

#pragma once

#include "lrg-script-task.h"

G_BEGIN_DECLS

/* the scheduler gives the task its id when it is added */
void     lrg_script_task_set_id_private      (LrgScriptTask *self,
                                              guint          id);

/* TRUE once the task belongs to a scheduler (cancel then waits a resume) */
void     lrg_script_task_set_scheduled_private (LrgScriptTask *self,
                                                gboolean       scheduled);

/*
 * The task may run on this frame: pending, a wait without a target, a
 * time or frame passed, a condition true (or timed out); a cancel
 * request cuts any wait short.
 */
gboolean lrg_script_task_is_ready_private    (LrgScriptTask *self,
                                              guint64        frame,
                                              gdouble        time);

/*
 * One resume: the task becomes current, its class resumes it, then its
 * state is settled (waiting, finished, failed, or cancelled after the
 * grace resume of a cancel request).  @frame and @time are the
 * scheduler's, for the next wait's targets.  Returns the microseconds
 * it took.
 */
gint64   lrg_script_task_run_private         (LrgScriptTask         *self,
                                              const LrgScriptBudget *budget,
                                              guint64                frame,
                                              gdouble                time);

/* final state without a resume (a cancelled task that never ran) */
void     lrg_script_task_end_private         (LrgScriptTask      *self,
                                              LrgScriptTaskState  state,
                                              GError             *error);

G_END_DECLS
