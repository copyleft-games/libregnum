/* lrg-cloth-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgClothComponent: a cloth grid in the entity's local XY plane, its top
 * left corner at the entity origin, hanging down local -Y.  The grid and
 * its pins are rebuilt lazily after a property change.  fixed_update
 * feeds the entity's world matrix (pins), the skeleton (bones) and the
 * world's physics (collision) to the cloth and steps it by the fixed
 * step; collect_render submits one MESH item whose mesh is streamed from
 * the particles once per simulation step.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>

#include "lrg-cloth-component.h"
#include "../lrg-entity3d.h"
#include "../lrg-world3d.h"
#include "../../animation/lrg-skeleton.h"
#include "../../core/lrg-math3d.h"

#define GRID_MAX (256u)

/* The float properties, stored in one array and forwarded to the cloth. */
enum
{
	F_WIDTH,
	F_HEIGHT,
	F_STRETCH,
	F_SHEAR,
	F_BEND,
	F_DAMPING,
	F_DRAG,
	F_FRICTION,
	F_THICKNESS,
	F_DENSITY,
	F_WIND_X,
	F_WIND_Y,
	F_WIND_Z,
	F_COLOR_R,
	F_COLOR_G,
	F_COLOR_B,
	F_COLOR_A,
	N_FLOATS
};

struct _LrgClothComponent
{
	LrgComponent parent_instance;

	LrgCloth         *cloth;
	LrgSkeleton      *skeleton;
	GrlMesh          *mesh;
	guint             columns;
	guint             rows;
	LrgClothPinEdges  pins;
	guint             substeps;
	LrgClothBendMode  bend_mode;
	gfloat            f[N_FLOATS];
	gboolean          collide_physics;
	guint             collision_mask;
	gboolean          self_collision;
	gboolean          cast_shadows;
	gint              layer;

	gboolean          grid_dirty;    /* rebuild the grid and pins */
	gboolean          placed;        /* the cloth was reset at the entity once */
	guint64           sim_version;   /* steps taken */
	guint64           mesh_version;  /* steps streamed into the mesh */
};

enum
{
	PROP_0,
	PROP_COLUMNS,
	PROP_ROWS,
	PROP_WIDTH,
	PROP_HEIGHT,
	PROP_PIN_EDGES,
	PROP_SUBSTEPS,
	PROP_BEND_MODE,
	PROP_STRETCH_COMPLIANCE,
	PROP_SHEAR_COMPLIANCE,
	PROP_BEND_COMPLIANCE,
	PROP_DAMPING,
	PROP_DRAG,
	PROP_FRICTION,
	PROP_THICKNESS,
	PROP_DENSITY,
	PROP_WIND_X,
	PROP_WIND_Y,
	PROP_WIND_Z,
	PROP_COLOR_R,
	PROP_COLOR_G,
	PROP_COLOR_B,
	PROP_COLOR_A,
	PROP_COLLIDE_PHYSICS,
	PROP_COLLISION_MASK,
	PROP_SELF_COLLISION,
	PROP_CAST_SHADOWS,
	PROP_LAYER,
	PROP_SKELETON,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Float property -> slot in f[] and whether a change rebuilds the grid. */
static const struct
{
	guint    prop;
	guint    slot;
	gboolean grid;
} float_props[] = {
	{ PROP_WIDTH, F_WIDTH, TRUE },
	{ PROP_HEIGHT, F_HEIGHT, TRUE },
	{ PROP_STRETCH_COMPLIANCE, F_STRETCH, FALSE },
	{ PROP_SHEAR_COMPLIANCE, F_SHEAR, FALSE },
	{ PROP_BEND_COMPLIANCE, F_BEND, FALSE },
	{ PROP_DAMPING, F_DAMPING, FALSE },
	{ PROP_DRAG, F_DRAG, FALSE },
	{ PROP_FRICTION, F_FRICTION, FALSE },
	{ PROP_THICKNESS, F_THICKNESS, FALSE },
	{ PROP_DENSITY, F_DENSITY, FALSE },
	{ PROP_WIND_X, F_WIND_X, FALSE },
	{ PROP_WIND_Y, F_WIND_Y, FALSE },
	{ PROP_WIND_Z, F_WIND_Z, FALSE },
	{ PROP_COLOR_R, F_COLOR_R, FALSE },
	{ PROP_COLOR_G, F_COLOR_G, FALSE },
	{ PROP_COLOR_B, F_COLOR_B, FALSE },
	{ PROP_COLOR_A, F_COLOR_A, FALSE }
};

G_DEFINE_FINAL_TYPE (LrgClothComponent, lrg_cloth_component, LRG_TYPE_COMPONENT)

GType
lrg_cloth_pin_edges_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_CLOTH_PIN_NONE, "LRG_CLOTH_PIN_NONE", "none" },
			{ LRG_CLOTH_PIN_TOP, "LRG_CLOTH_PIN_TOP", "top" },
			{ LRG_CLOTH_PIN_BOTTOM, "LRG_CLOTH_PIN_BOTTOM", "bottom" },
			{ LRG_CLOTH_PIN_LEFT, "LRG_CLOTH_PIN_LEFT", "left" },
			{ LRG_CLOTH_PIN_RIGHT, "LRG_CLOTH_PIN_RIGHT", "right" },
			{ LRG_CLOTH_PIN_TOP_CORNERS, "LRG_CLOTH_PIN_TOP_CORNERS", "top-corners" },
			{ 0, NULL, NULL }
		};
		GType t = g_flags_register_static (g_intern_static_string ("LrgClothPinEdges"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Cloth upkeep
 * ========================================================================== */

/* Pushes the scalar parameters into the cloth (cheap; setters compare). */
static void
apply_parameters (LrgClothComponent *self)
{
	gfloat wind[3] = { self->f[F_WIND_X], self->f[F_WIND_Y], self->f[F_WIND_Z] };

	g_object_set (self->cloth,
	              "substeps", self->substeps,
	              "bend-mode", self->bend_mode,
	              "damping", self->f[F_DAMPING],
	              "drag", self->f[F_DRAG],
	              "friction", self->f[F_FRICTION],
	              "thickness", self->f[F_THICKNESS],
	              "density", self->f[F_DENSITY],
	              "self-collision", self->self_collision,
	              NULL);
	lrg_cloth_set_compliance (self->cloth, self->f[F_STRETCH], self->f[F_SHEAR], self->f[F_BEND]);
	lrg_cloth_set_wind (self->cloth, wind);
}

/* Rebuilds the grid and its pins; the mesh is recreated on next draw. */
static void
rebuild_grid (LrgClothComponent *self)
{
	gfloat origin[3] = { 0.0f, 0.0f, 0.0f };
	gfloat u[3] = { 0.0f, 0.0f, 0.0f };
	gfloat v[3] = { 0.0f, 0.0f, 0.0f };
	guint  c;
	guint  r;

	u[0] = self->f[F_WIDTH] / (gfloat) (self->columns - 1);
	v[1] = -self->f[F_HEIGHT] / (gfloat) (self->rows - 1);
	if (!lrg_cloth_set_grid (self->cloth, self->columns, self->rows, origin, u, v, NULL))
		return;

	for (r = 0; r < self->rows; r++)
	{
		for (c = 0; c < self->columns; c++)
		{
			gboolean pin = ((self->pins & LRG_CLOTH_PIN_TOP) && r == 0)
			               || ((self->pins & LRG_CLOTH_PIN_BOTTOM) && r == self->rows - 1)
			               || ((self->pins & LRG_CLOTH_PIN_LEFT) && c == 0)
			               || ((self->pins & LRG_CLOTH_PIN_RIGHT) && c == self->columns - 1)
			               || ((self->pins & LRG_CLOTH_PIN_TOP_CORNERS) && r == 0
			                   && (c == 0 || c == self->columns - 1));

			if (pin)
				lrg_cloth_pin (self->cloth, r * self->columns + c);
		}
	}
	g_clear_object (&self->mesh);
	self->grid_dirty = FALSE;
	self->placed = FALSE;
}

/* Places the cloth at the entity (first time and after a rebuild). */
static void
sync_cloth (LrgClothComponent *self)
{
	LrgEntity3D *entity = lrg_component_get_entity (LRG_COMPONENT (self));

	if (self->grid_dirty)
		rebuild_grid (self);
	apply_parameters (self);
	if (entity == NULL)
		return;
	lrg_cloth_set_transform (self->cloth, lrg_entity3d_peek_world_matrix (entity));
	if (!self->placed)
	{
		lrg_cloth_reset (self->cloth);
		self->placed = TRUE;
	}
}

/* ==========================================================================
 * Component hooks
 * ========================================================================== */

static void
lrg_cloth_component_fixed_update (LrgComponent *component,
                                  gfloat        step)
{
	LrgClothComponent *self = LRG_CLOTH_COMPONENT (component);
	LrgWorld3D        *world = lrg_component_get_world3d (component);
	LrgDynamicsWorld  *physics = NULL;

	if (!lrg_component_get_enabled (component) || lrg_component_get_entity (component) == NULL)
		return;

	sync_cloth (self);
	if (self->skeleton != NULL)
		lrg_cloth_set_bone_matrices_from_skeleton (self->cloth, self->skeleton);
	if (self->collide_physics && world != NULL)
		physics = lrg_world3d_get_physics (world);
	if (lrg_cloth_get_dynamics_world (self->cloth) != physics)
		lrg_cloth_set_dynamics_world (self->cloth, physics, self->collision_mask);
	lrg_cloth_step (self->cloth, step);
	self->sim_version++;
}

static void
lrg_cloth_component_collect_render (LrgComponent  *component,
                                    LrgRenderList *list)
{
	LrgClothComponent *self = LRG_CLOTH_COMPONENT (component);
	LrgEntity3D       *entity = lrg_component_get_entity (component);
	LrgRenderItem      item;
	LrgBoundingBox3D   box;
	gfloat             lo[3];
	gfloat             hi[3];
	guint              flags;

	if (entity == NULL)
		return;
	if (self->grid_dirty || !self->placed)
		sync_cloth (self);
	if (!lrg_cloth_get_bounds (self->cloth, lo, hi))
		return;

	/* Stream the particles into the mesh once per simulation step. */
	if (self->mesh == NULL && IsWindowReady ())
	{
		self->mesh = lrg_cloth_create_mesh (self->cloth, NULL);
		self->mesh_version = self->sim_version;
	}
	if (self->mesh != NULL && self->mesh_version != self->sim_version)
	{
		lrg_cloth_update_mesh (self->cloth, self->mesh);
		self->mesh_version = self->sim_version;
	}

	lrg_render_item_init_for_entity (&item, entity, component);
	item.kind = LRG_RENDER_ITEM_MESH;
	item.mesh = self->mesh;
	lrg_math3d_identity (item.world_matrix);   /* particles are in world space */
	item.color[0] = self->f[F_COLOR_R];
	item.color[1] = self->f[F_COLOR_G];
	item.color[2] = self->f[F_COLOR_B];
	item.color[3] = self->f[F_COLOR_A];
	flags = LRG_RENDER_ITEM_FLAG_DOUBLE_SIDED | LRG_RENDER_ITEM_FLAG_RECEIVE_SHADOW;
	if (self->cast_shadows)
		flags |= LRG_RENDER_ITEM_FLAG_CAST_SHADOW;
	if (self->f[F_COLOR_A] < 1.0f)
		flags |= LRG_RENDER_ITEM_FLAG_TRANSPARENT;
	item.flags = (LrgRenderItemFlags) flags;
	if (self->layer >= 0)
		item.layer = (guint) self->layer;
	item.sort_key = ((guint64) item.kind << 56) | ((guint64) (guintptr) self->mesh & G_GUINT64_CONSTANT (0x00ffffffffffffff));
	box.min.x = lo[0]; box.min.y = lo[1]; box.min.z = lo[2];
	box.max.x = hi[0]; box.max.y = hi[1]; box.max.z = hi[2];
	lrg_render_item_set_local_bounds (&item, &box);
	if (lrg_render_list_accepts (list, &item))
		lrg_render_list_add (list, &item);
}

/* Local bounds: the rest grid (the simulated cloth moves away from it). */
static gboolean
lrg_cloth_component_get_bounds (LrgComponent     *component,
                                LrgBoundingBox3D *out_local)
{
	LrgClothComponent *self = LRG_CLOTH_COMPONENT (component);
	gfloat             w = self->f[F_WIDTH];
	gfloat             h = self->f[F_HEIGHT];

	out_local->min.x = 0.0f;
	out_local->min.y = -h;
	out_local->min.z = -0.5f * MAX (w, h);
	out_local->max.x = w;
	out_local->max.y = 0.0f;
	out_local->max.z = 0.5f * MAX (w, h);
	return TRUE;
}

static void
lrg_cloth_component_world_exited (LrgComponent *component,
                                  LrgWorld3D   *world)
{
	LrgClothComponent *self = LRG_CLOTH_COMPONENT (component);

	lrg_cloth_set_dynamics_world (self->cloth, NULL, 0);
	self->placed = FALSE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_cloth_component_dispose (GObject *object)
{
	LrgClothComponent *self = LRG_CLOTH_COMPONENT (object);

	g_clear_object (&self->mesh);
	g_clear_object (&self->skeleton);
	if (self->cloth != NULL)
		lrg_cloth_set_dynamics_world (self->cloth, NULL, 0);

	G_OBJECT_CLASS (lrg_cloth_component_parent_class)->dispose (object);
}

static void
lrg_cloth_component_finalize (GObject *object)
{
	LrgClothComponent *self = LRG_CLOTH_COMPONENT (object);

	g_clear_object (&self->cloth);

	G_OBJECT_CLASS (lrg_cloth_component_parent_class)->finalize (object);
}

static gint
float_slot (guint prop_id,
            gboolean *grid)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (float_props); i++)
	{
		if (float_props[i].prop == prop_id)
		{
			if (grid != NULL)
				*grid = float_props[i].grid;
			return (gint) float_props[i].slot;
		}
	}
	return -1;
}

static void
lrg_cloth_component_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgClothComponent *self = LRG_CLOTH_COMPONENT (object);
	gboolean           grid = FALSE;
	gint               slot = float_slot (prop_id, &grid);

	if (slot >= 0)
	{
		gfloat v = g_value_get_float (value);

		if (self->f[slot] != v)
		{
			self->f[slot] = v;
			if (grid)
				self->grid_dirty = TRUE;
			g_object_notify_by_pspec (object, pspec);
		}
		return;
	}

	switch (prop_id)
	{
	case PROP_COLUMNS:
		if (self->columns != g_value_get_uint (value))
		{
			self->columns = g_value_get_uint (value);
			self->grid_dirty = TRUE;
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_ROWS:
		if (self->rows != g_value_get_uint (value))
		{
			self->rows = g_value_get_uint (value);
			self->grid_dirty = TRUE;
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_PIN_EDGES:
		lrg_cloth_component_set_pin_edges (self, (LrgClothPinEdges) g_value_get_flags (value));
		break;
	case PROP_SUBSTEPS:
		if (self->substeps != g_value_get_uint (value))
		{
			self->substeps = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_BEND_MODE:
		if (self->bend_mode != (LrgClothBendMode) g_value_get_enum (value))
		{
			self->bend_mode = (LrgClothBendMode) g_value_get_enum (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_COLLIDE_PHYSICS:
		if (self->collide_physics != g_value_get_boolean (value))
		{
			self->collide_physics = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_COLLISION_MASK:
		if (self->collision_mask != g_value_get_uint (value))
		{
			self->collision_mask = g_value_get_uint (value);
			/* Re-attach with the new mask on the next fixed update. */
			lrg_cloth_set_dynamics_world (self->cloth, NULL, 0);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_SELF_COLLISION:
		if (self->self_collision != g_value_get_boolean (value))
		{
			self->self_collision = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_CAST_SHADOWS:
		if (self->cast_shadows != g_value_get_boolean (value))
		{
			self->cast_shadows = g_value_get_boolean (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_LAYER:
		if (self->layer != g_value_get_int (value))
		{
			self->layer = g_value_get_int (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_SKELETON:
		lrg_cloth_component_set_skeleton (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_cloth_component_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgClothComponent *self = LRG_CLOTH_COMPONENT (object);
	gint               slot = float_slot (prop_id, NULL);

	if (slot >= 0)
	{
		g_value_set_float (value, self->f[slot]);
		return;
	}

	switch (prop_id)
	{
	case PROP_COLUMNS:
		g_value_set_uint (value, self->columns);
		break;
	case PROP_ROWS:
		g_value_set_uint (value, self->rows);
		break;
	case PROP_PIN_EDGES:
		g_value_set_flags (value, self->pins);
		break;
	case PROP_SUBSTEPS:
		g_value_set_uint (value, self->substeps);
		break;
	case PROP_BEND_MODE:
		g_value_set_enum (value, (gint) self->bend_mode);
		break;
	case PROP_COLLIDE_PHYSICS:
		g_value_set_boolean (value, self->collide_physics);
		break;
	case PROP_COLLISION_MASK:
		g_value_set_uint (value, self->collision_mask);
		break;
	case PROP_SELF_COLLISION:
		g_value_set_boolean (value, self->self_collision);
		break;
	case PROP_CAST_SHADOWS:
		g_value_set_boolean (value, self->cast_shadows);
		break;
	case PROP_LAYER:
		g_value_set_int (value, self->layer);
		break;
	case PROP_SKELETON:
		g_value_set_object (value, self->skeleton);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GParamSpec *
float_spec (const gchar *name,
            const gchar *blurb,
            gfloat       min,
            gfloat       max,
            gfloat       def)
{
	return g_param_spec_float (name, NULL, blurb, min, max, def,
	                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
}

static void
lrg_cloth_component_class_init (LrgClothComponentClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);
	GParamFlags        rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->dispose = lrg_cloth_component_dispose;
	object_class->finalize = lrg_cloth_component_finalize;
	object_class->set_property = lrg_cloth_component_set_property;
	object_class->get_property = lrg_cloth_component_get_property;
	component_class->fixed_update = lrg_cloth_component_fixed_update;
	component_class->collect_render = lrg_cloth_component_collect_render;
	component_class->get_bounds = lrg_cloth_component_get_bounds;
	component_class->world_exited = lrg_cloth_component_world_exited;

	/**
	 * LrgClothComponent:columns:
	 *
	 * Grid vertices across (local +X).
	 */
	properties[PROP_COLUMNS] =
		g_param_spec_uint ("columns", NULL, "Vertices across", 2, GRID_MAX, 16, rw);

	/**
	 * LrgClothComponent:rows:
	 *
	 * Grid vertices down (local -Y).
	 */
	properties[PROP_ROWS] =
		g_param_spec_uint ("rows", NULL, "Vertices down", 2, GRID_MAX, 16, rw);

	/**
	 * LrgClothComponent:width:
	 *
	 * Cloth width in metres along local +X.
	 */
	properties[PROP_WIDTH] = float_spec ("width", "Width (m)", 0.01f, 1000.0f, 1.0f);

	/**
	 * LrgClothComponent:height:
	 *
	 * Cloth height in metres down local -Y.
	 */
	properties[PROP_HEIGHT] = float_spec ("height", "Height (m)", 0.01f, 1000.0f, 1.0f);

	/**
	 * LrgClothComponent:pin-edges:
	 *
	 * Which grid vertices follow the entity (#LrgClothPinEdges).
	 */
	properties[PROP_PIN_EDGES] =
		g_param_spec_flags ("pin-edges", NULL, "Pinned edges", LRG_TYPE_CLOTH_PIN_EDGES,
		                    LRG_CLOTH_PIN_TOP, rw);

	/**
	 * LrgClothComponent:substeps:
	 *
	 * #LrgCloth:substeps per fixed update.
	 */
	properties[PROP_SUBSTEPS] =
		g_param_spec_uint ("substeps", NULL, "Substeps", 1, LRG_CLOTH_MAX_SUBSTEPS, 8, rw);

	/**
	 * LrgClothComponent:bend-mode:
	 *
	 * #LrgCloth:bend-mode.
	 */
	properties[PROP_BEND_MODE] =
		g_param_spec_enum ("bend-mode", NULL, "Bending model", LRG_TYPE_CLOTH_BEND_MODE,
		                   LRG_CLOTH_BEND_DISTANCE, rw);

	/**
	 * LrgClothComponent:stretch-compliance:
	 *
	 * #LrgCloth:stretch-compliance.
	 */
	properties[PROP_STRETCH_COMPLIANCE] = float_spec ("stretch-compliance", "Stretch compliance", 0.0f, G_MAXFLOAT, 0.0f);

	/**
	 * LrgClothComponent:shear-compliance:
	 *
	 * #LrgCloth:shear-compliance.
	 */
	properties[PROP_SHEAR_COMPLIANCE] = float_spec ("shear-compliance", "Shear compliance", 0.0f, G_MAXFLOAT, 0.5f);

	/**
	 * LrgClothComponent:bend-compliance:
	 *
	 * #LrgCloth:bend-compliance.
	 */
	properties[PROP_BEND_COMPLIANCE] = float_spec ("bend-compliance", "Bend compliance", 0.0f, G_MAXFLOAT, 1.0f);

	/**
	 * LrgClothComponent:damping:
	 *
	 * #LrgCloth:damping.
	 */
	properties[PROP_DAMPING] = float_spec ("damping", "Damping (1/s)", 0.0f, 100.0f, 0.1f);

	/**
	 * LrgClothComponent:drag:
	 *
	 * #LrgCloth:drag (0 turns the wind off).
	 */
	properties[PROP_DRAG] = float_spec ("drag", "Drag coefficient", 0.0f, 100.0f, 1.0f);

	/**
	 * LrgClothComponent:friction:
	 *
	 * #LrgCloth:friction.
	 */
	properties[PROP_FRICTION] = float_spec ("friction", "Friction", 0.0f, 10.0f, 0.3f);

	/**
	 * LrgClothComponent:thickness:
	 *
	 * #LrgCloth:thickness.
	 */
	properties[PROP_THICKNESS] = float_spec ("thickness", "Thickness (m)", 0.0f, 10.0f, 0.01f);

	/**
	 * LrgClothComponent:density:
	 *
	 * #LrgCloth:density in kg/m².
	 */
	properties[PROP_DENSITY] = float_spec ("density", "Density (kg/m2)", 0.0001f, 1000.0f, 0.25f);

	/**
	 * LrgClothComponent:wind-x:
	 *
	 * Wind velocity along world X (m/s).
	 */
	properties[PROP_WIND_X] = float_spec ("wind-x", "Wind X (m/s)", -1000.0f, 1000.0f, 0.0f);

	/**
	 * LrgClothComponent:wind-y:
	 *
	 * Wind velocity along world Y (m/s).
	 */
	properties[PROP_WIND_Y] = float_spec ("wind-y", "Wind Y (m/s)", -1000.0f, 1000.0f, 0.0f);

	/**
	 * LrgClothComponent:wind-z:
	 *
	 * Wind velocity along world Z (m/s).
	 */
	properties[PROP_WIND_Z] = float_spec ("wind-z", "Wind Z (m/s)", -1000.0f, 1000.0f, 0.0f);

	/**
	 * LrgClothComponent:color-r:
	 *
	 * Red of the cloth colour (0 .. 1).
	 */
	properties[PROP_COLOR_R] = float_spec ("color-r", "Red", 0.0f, 1.0f, 0.8f);

	/**
	 * LrgClothComponent:color-g:
	 *
	 * Green of the cloth colour (0 .. 1).
	 */
	properties[PROP_COLOR_G] = float_spec ("color-g", "Green", 0.0f, 1.0f, 0.15f);

	/**
	 * LrgClothComponent:color-b:
	 *
	 * Blue of the cloth colour (0 .. 1).
	 */
	properties[PROP_COLOR_B] = float_spec ("color-b", "Blue", 0.0f, 1.0f, 0.15f);

	/**
	 * LrgClothComponent:color-a:
	 *
	 * Alpha of the cloth colour; below 1 the item is transparent.
	 */
	properties[PROP_COLOR_A] = float_spec ("color-a", "Alpha", 0.0f, 1.0f, 1.0f);

	/**
	 * LrgClothComponent:collide-physics:
	 *
	 * Collide with the bodies of the world's #LrgDynamicsWorld
	 * (lrg_cloth_set_dynamics_world()).
	 */
	properties[PROP_COLLIDE_PHYSICS] =
		g_param_spec_boolean ("collide-physics", NULL, "Collide with physics bodies", TRUE, rw);

	/**
	 * LrgClothComponent:collision-mask:
	 *
	 * Object layers of the bodies the cloth collides with.
	 */
	properties[PROP_COLLISION_MASK] =
		g_param_spec_uint ("collision-mask", NULL, "Layers collided with", 0, G_MAXUINT32,
		                   G_MAXUINT32, rw);

	/**
	 * LrgClothComponent:self-collision:
	 *
	 * #LrgCloth:self-collision.
	 */
	properties[PROP_SELF_COLLISION] =
		g_param_spec_boolean ("self-collision", NULL, "Self collision", FALSE, rw);

	/**
	 * LrgClothComponent:cast-shadows:
	 *
	 * Adds %LRG_RENDER_ITEM_FLAG_CAST_SHADOW.
	 */
	properties[PROP_CAST_SHADOWS] =
		g_param_spec_boolean ("cast-shadows", NULL, "Cast shadows", TRUE, rw);

	/**
	 * LrgClothComponent:layer:
	 *
	 * Render layer override, -1 for the entity's.
	 */
	properties[PROP_LAYER] =
		g_param_spec_int ("layer", NULL, "Render layer", -1, 31, -1, rw);

	/**
	 * LrgClothComponent:skeleton:
	 *
	 * The skeleton driving bone attachments (see
	 * lrg_cloth_component_set_skeleton()).
	 */
	properties[PROP_SKELETON] =
		g_param_spec_object ("skeleton", NULL, "Driving skeleton", LRG_TYPE_SKELETON, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_cloth_component_init (LrgClothComponent *self)
{
	self->cloth = lrg_cloth_new ();
	self->columns = 16;
	self->rows = 16;
	self->pins = LRG_CLOTH_PIN_TOP;
	self->substeps = 8;
	self->bend_mode = LRG_CLOTH_BEND_DISTANCE;
	self->f[F_WIDTH] = 1.0f;
	self->f[F_HEIGHT] = 1.0f;
	self->f[F_STRETCH] = 0.0f;
	self->f[F_SHEAR] = 0.5f;
	self->f[F_BEND] = 1.0f;
	self->f[F_DAMPING] = 0.1f;
	self->f[F_DRAG] = 1.0f;
	self->f[F_FRICTION] = 0.3f;
	self->f[F_THICKNESS] = 0.01f;
	self->f[F_DENSITY] = 0.25f;
	self->f[F_COLOR_R] = 0.8f;
	self->f[F_COLOR_G] = 0.15f;
	self->f[F_COLOR_B] = 0.15f;
	self->f[F_COLOR_A] = 1.0f;
	self->collide_physics = TRUE;
	self->collision_mask = G_MAXUINT32;
	self->cast_shadows = TRUE;
	self->layer = -1;
	self->grid_dirty = TRUE;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgClothComponent *
lrg_cloth_component_new (void)
{
	return g_object_new (LRG_TYPE_CLOTH_COMPONENT, NULL);
}

LrgClothComponent *
lrg_cloth_component_new_flag (gfloat width,
                              gfloat height)
{
	guint columns;
	guint rows;

	width = CLAMP (isfinite (width) ? width : 1.0f, 0.01f, 1000.0f);
	height = CLAMP (isfinite (height) ? height : 1.0f, 0.01f, 1000.0f);
	columns = CLAMP ((guint) ceilf (width * 20.0f) + 1, 8u, GRID_MAX);
	rows = CLAMP ((guint) ceilf (height * 20.0f) + 1, 8u, GRID_MAX);
	return g_object_new (LRG_TYPE_CLOTH_COMPONENT,
	                     "columns", columns, "rows", rows, "width", width, "height", height,
	                     "pin-edges", LRG_CLOTH_PIN_LEFT, NULL);
}

LrgCloth *
lrg_cloth_component_get_cloth (LrgClothComponent *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH_COMPONENT (self), NULL);
	if (self->grid_dirty)
		rebuild_grid (self);
	return self->cloth;
}

void
lrg_cloth_component_set_grid (LrgClothComponent *self,
                              guint              columns,
                              guint              rows,
                              gfloat             width,
                              gfloat             height)
{
	g_return_if_fail (LRG_IS_CLOTH_COMPONENT (self));
	g_object_set (self,
	              "columns", CLAMP (columns, 2u, GRID_MAX),
	              "rows", CLAMP (rows, 2u, GRID_MAX),
	              "width", CLAMP (width, 0.01f, 1000.0f),
	              "height", CLAMP (height, 0.01f, 1000.0f),
	              NULL);
}

LrgClothPinEdges
lrg_cloth_component_get_pin_edges (LrgClothComponent *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH_COMPONENT (self), LRG_CLOTH_PIN_NONE);
	return self->pins;
}

void
lrg_cloth_component_set_pin_edges (LrgClothComponent *self,
                                   LrgClothPinEdges   edges)
{
	g_return_if_fail (LRG_IS_CLOTH_COMPONENT (self));
	if (self->pins == edges)
		return;
	self->pins = edges;
	self->grid_dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PIN_EDGES]);
}

void
lrg_cloth_component_set_wind (LrgClothComponent *self,
                              gfloat             x,
                              gfloat             y,
                              gfloat             z)
{
	g_return_if_fail (LRG_IS_CLOTH_COMPONENT (self));
	g_object_set (self, "wind-x", x, "wind-y", y, "wind-z", z, NULL);
}

void
lrg_cloth_component_set_color (LrgClothComponent *self,
                               gfloat             r,
                               gfloat             g,
                               gfloat             b,
                               gfloat             a)
{
	g_return_if_fail (LRG_IS_CLOTH_COMPONENT (self));
	g_object_set (self, "color-r", CLAMP (r, 0.0f, 1.0f), "color-g", CLAMP (g, 0.0f, 1.0f),
	              "color-b", CLAMP (b, 0.0f, 1.0f), "color-a", CLAMP (a, 0.0f, 1.0f), NULL);
}

void
lrg_cloth_component_set_skeleton (LrgClothComponent *self,
                                  LrgSkeleton       *skeleton)
{
	g_return_if_fail (LRG_IS_CLOTH_COMPONENT (self));
	g_return_if_fail (skeleton == NULL || LRG_IS_SKELETON (skeleton));
	if (g_set_object (&self->skeleton, skeleton))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKELETON]);
}

LrgSkeleton *
lrg_cloth_component_get_skeleton (LrgClothComponent *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH_COMPONENT (self), NULL);
	return self->skeleton;
}

void
lrg_cloth_component_reset (LrgClothComponent *self)
{
	g_return_if_fail (LRG_IS_CLOTH_COMPONENT (self));
	self->placed = FALSE;
	sync_cloth (self);
	self->sim_version++;
}

GrlMesh *
lrg_cloth_component_get_mesh (LrgClothComponent *self)
{
	g_return_val_if_fail (LRG_IS_CLOTH_COMPONENT (self), NULL);
	return self->mesh;
}
