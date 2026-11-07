/* lrg-pose.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <math.h>
#include "lrg-pose.h"

struct _LrgPose
{
	gfloat px, py, pz;
	gfloat tx, ty, tz;
	gfloat ux, uy, uz;
	gfloat fovy;
};

G_DEFINE_BOXED_TYPE (LrgPose, lrg_pose, lrg_pose_copy, lrg_pose_free)

LrgPose *
lrg_pose_new (gfloat px, gfloat py, gfloat pz,
			  gfloat tx, gfloat ty, gfloat tz,
			  gfloat ux, gfloat uy, gfloat uz,
			  gfloat fovy)
{
	LrgPose *self = g_new0 (LrgPose, 1);

	self->px = px;
	self->py = py;
	self->pz = pz;
	self->tx = tx;
	self->ty = ty;
	self->tz = tz;
	self->ux = ux;
	self->uy = uy;
	self->uz = uz;
	self->fovy = fovy;

	return self;
}

LrgPose *
lrg_pose_copy (const LrgPose *self)
{
	if (self == NULL)
		return NULL;

	return lrg_pose_new (self->px, self->py, self->pz,
						 self->tx, self->ty, self->tz,
						 self->ux, self->uy, self->uz,
						 self->fovy);
}

void
lrg_pose_free (LrgPose *self)
{
	g_free (self);
}

void
lrg_pose_get_position (const LrgPose *self,
					   gfloat        *x,
					   gfloat        *y,
					   gfloat        *z)
{
	g_return_if_fail (self != NULL);

	if (x != NULL)
		*x = self->px;
	if (y != NULL)
		*y = self->py;
	if (z != NULL)
		*z = self->pz;
}

void
lrg_pose_get_target (const LrgPose *self,
					 gfloat        *x,
					 gfloat        *y,
					 gfloat        *z)
{
	g_return_if_fail (self != NULL);

	if (x != NULL)
		*x = self->tx;
	if (y != NULL)
		*y = self->ty;
	if (z != NULL)
		*z = self->tz;
}

void
lrg_pose_get_up (const LrgPose *self,
				 gfloat        *x,
				 gfloat        *y,
				 gfloat        *z)
{
	g_return_if_fail (self != NULL);

	if (x != NULL)
		*x = self->ux;
	if (y != NULL)
		*y = self->uy;
	if (z != NULL)
		*z = self->uz;
}

gfloat
lrg_pose_get_fovy (const LrgPose *self)
{
	g_return_val_if_fail (self != NULL, 0.0f);
	return self->fovy;
}

LrgPose *
lrg_pose_lerp (const LrgPose *a,
			   const LrgPose *b,
			   gfloat         t)
{
	g_return_val_if_fail (a != NULL, NULL);
	g_return_val_if_fail (b != NULL, NULL);

	if (t < 0.0f)
		t = 0.0f;
	else if (t > 1.0f)
		t = 1.0f;

	return lrg_pose_new (
		a->px + (b->px - a->px) * t,
		a->py + (b->py - a->py) * t,
		a->pz + (b->pz - a->pz) * t,
		a->tx + (b->tx - a->tx) * t,
		a->ty + (b->ty - a->ty) * t,
		a->tz + (b->tz - a->tz) * t,
		a->ux + (b->ux - a->ux) * t,
		a->uy + (b->uy - a->uy) * t,
		a->uz + (b->uz - a->uz) * t,
		a->fovy + (b->fovy - a->fovy) * t);
}

gchar *
lrg_pose_to_string (const LrgPose *self)
{
	gfloat v[10];
	GString *out;
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
	gint i;

	g_return_val_if_fail (self != NULL, NULL);

	v[0] = self->px; v[1] = self->py; v[2] = self->pz;
	v[3] = self->tx; v[4] = self->ty; v[5] = self->tz;
	v[6] = self->ux; v[7] = self->uy; v[8] = self->uz;
	v[9] = self->fovy;

	/* g_ascii_dtostr is locale-independent and round-trips exactly. */
	out = g_string_new (NULL);
	for (i = 0; i < 10; i++)
	{
		if (i > 0)
			g_string_append_c (out, ' ');
		g_string_append (out, g_ascii_dtostr (buf, sizeof (buf), (gdouble) v[i]));
	}
	return g_string_free (out, FALSE);
}

LrgPose *
lrg_pose_from_string (const gchar *str)
{
	g_auto (GStrv) parts = NULL;
	gfloat v[10];
	guint n, i;

	g_return_val_if_fail (str != NULL, NULL);

	parts = g_strsplit_set (str, " \t\r\n", -1);
	n = 0;
	for (i = 0; parts[i] != NULL; i++)
	{
		gchar *end = NULL;
		gdouble d;

		/* Runs of separators yield empty tokens; skip them. */
		if (parts[i][0] == '\0')
			continue;
		if (n >= 10)
			return NULL;
		d = g_ascii_strtod (parts[i], &end);
		if (end == parts[i] || *end != '\0' || !isfinite (d))
			return NULL;
		v[n++] = (gfloat) d;
	}
	if (n != 10 || v[9] <= 0.0f || v[9] >= 180.0f)
		return NULL;

	return lrg_pose_new (v[0], v[1], v[2], v[3], v[4], v[5],
	                     v[6], v[7], v[8], v[9]);
}
