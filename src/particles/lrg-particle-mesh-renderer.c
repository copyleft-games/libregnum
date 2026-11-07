/* lrg-particle-mesh-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Mesh particles: one instanced draw call per system.
 *
 * build() is the headless step: a world matrix (translation, orientation
 * by the align mode, scale) and a colour per live particle, 20 floats per
 * instance.  draw() uploads them into a dynamic vertex buffer, wires it
 * into the mesh's vertex array object as instanced attributes
 * (locations 10 .. 13 for the matrix columns, 14 for the colour, divisor
 * 1) and draws with a small GLSL 330 program that reads rlgl's current
 * view and projection matrices.  GL 3.3 has instancing in core, so no
 * extension or graylib wrapper is needed.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-particle-mesh-renderer.h"

/* First attribute location of the instance data */
#define INSTANCE_LOCATION (10)

static const gchar *VERTEX_SHADER =
	"#version 330\n"
	"in vec3 vertexPosition;\n"
	"in vec2 vertexTexCoord;\n"
	"in vec3 vertexNormal;\n"
	"layout(location = 10) in mat4 instanceModel;\n"
	"layout(location = 14) in vec4 instanceColor;\n"
	"uniform mat4 matView;\n"
	"uniform mat4 matProjection;\n"
	"out vec2 fragTexCoord;\n"
	"out vec4 fragColor;\n"
	"out vec3 fragNormal;\n"
	"void main ()\n"
	"{\n"
	"    vec4 world = instanceModel * vec4 (vertexPosition, 1.0);\n"
	"    fragNormal = mat3 (instanceModel) * vertexNormal;\n"
	"    fragTexCoord = vertexTexCoord;\n"
	"    fragColor = instanceColor;\n"
	"    gl_Position = matProjection * matView * world;\n"
	"}\n";

static const gchar *FRAGMENT_SHADER =
	"#version 330\n"
	"in vec2 fragTexCoord;\n"
	"in vec4 fragColor;\n"
	"in vec3 fragNormal;\n"
	"uniform sampler2D texture0;\n"
	"uniform vec3 lightDir;\n"
	"uniform int lit;\n"
	"out vec4 finalColor;\n"
	"void main ()\n"
	"{\n"
	"    vec4 c = texture (texture0, fragTexCoord) * fragColor;\n"
	"    if (lit == 1)\n"
	"    {\n"
	"        vec3 n = length (fragNormal) > 0.0001 ? normalize (fragNormal) : vec3 (0.0, 1.0, 0.0);\n"
	"        float h = dot (n, -normalize (lightDir)) * 0.5 + 0.5;\n"
	"        c.rgb *= 0.3 + 0.7 * h * h;\n"
	"    }\n"
	"    if (c.a < 0.004) discard;\n"
	"    finalColor = c;\n"
	"}\n";

struct _LrgParticleMeshRenderer
{
	GObject                  parent_instance;

	GrlMesh                 *mesh;       /* nullable, owned ref */
	GrlModel                *model;      /* nullable, owned ref */
	LrgParticleMeshPrimitive primitive;
	gfloat                   scale[3];
	LrgParticleMeshAlign     align;
	gboolean                 lit;
	gfloat                   light_dir[3];
	GrlTexture              *texture;

	GArray                  *scratch;    /* instance floats for draw() */

	/* GPU state */
	Shader                   shader;
	gboolean                 shader_loaded;
	gint                     loc_view;
	gint                     loc_projection;
	gint                     loc_light;
	gint                     loc_lit;
	gint                     loc_texture;
	guint                    instance_vbo;
	gsize                    instance_capacity;   /* bytes */
	Mesh                     prim;
	gboolean                 prim_loaded;
	LrgParticleMeshPrimitive prim_kind;
};

G_DEFINE_FINAL_TYPE (LrgParticleMeshRenderer, lrg_particle_mesh_renderer, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_MESH,
	PROP_PRIMITIVE,
	PROP_ALIGN,
	PROP_LIT,
	PROP_TEXTURE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

GType
lrg_particle_mesh_align_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PARTICLE_MESH_ALIGN_NONE, "LRG_PARTICLE_MESH_ALIGN_NONE", "none" },
			{ LRG_PARTICLE_MESH_ALIGN_VELOCITY, "LRG_PARTICLE_MESH_ALIGN_VELOCITY", "velocity" },
			{ LRG_PARTICLE_MESH_ALIGN_TUMBLE, "LRG_PARTICLE_MESH_ALIGN_TUMBLE", "tumble" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static ("LrgParticleMeshAlign", values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_particle_mesh_primitive_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_PARTICLE_MESH_PRIMITIVE_CUBE, "LRG_PARTICLE_MESH_PRIMITIVE_CUBE", "cube" },
			{ LRG_PARTICLE_MESH_PRIMITIVE_SPHERE, "LRG_PARTICLE_MESH_PRIMITIVE_SPHERE", "sphere" },
			{ LRG_PARTICLE_MESH_PRIMITIVE_CYLINDER, "LRG_PARTICLE_MESH_PRIMITIVE_CYLINDER", "cylinder" },
			{ LRG_PARTICLE_MESH_PRIMITIVE_CONE, "LRG_PARTICLE_MESH_PRIMITIVE_CONE", "cone" },
			{ LRG_PARTICLE_MESH_PRIMITIVE_PLANE, "LRG_PARTICLE_MESH_PRIMITIVE_PLANE", "plane" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static ("LrgParticleMeshPrimitive", values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Instances
 * ========================================================================== */

/* Rotation matrix (3x3, column-major in r[0..8]) of @angle about the
 * unit @axis (Rodrigues). */
static void
axis_angle (const gfloat *axis,
            gfloat        angle,
            gfloat       *r)
{
	gfloat c = cosf (angle), s = sinf (angle), t = 1.0f - c;
	gfloat x = axis[0], y = axis[1], z = axis[2];

	r[0] = t * x * x + c;
	r[1] = t * x * y + s * z;
	r[2] = t * x * z - s * y;
	r[3] = t * x * y - s * z;
	r[4] = t * y * y + c;
	r[5] = t * y * z + s * x;
	r[6] = t * x * z + s * y;
	r[7] = t * y * z - s * x;
	r[8] = t * z * z + c;
}

/* The orientation of @p by @align, column-major 3x3 */
static void
orientation (LrgParticleMeshAlign  align,
             const LrgParticle    *p,
             gfloat               *r)
{
	switch (align)
	{
	case LRG_PARTICLE_MESH_ALIGN_VELOCITY:
	{
		gfloat y[3] = { p->velocity_x, p->velocity_y, p->velocity_z };
		gfloat len = sqrtf (y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
		gfloat a[3], x[3], z[3], spin[9];
		gint   i, k;

		if (!(len > 1e-6f))
		{
			y[0] = 0.0f;
			y[1] = 1.0f;
			y[2] = 0.0f;
		}
		else
		{
			y[0] /= len;
			y[1] /= len;
			y[2] /= len;
		}
		/* a basis with +Y along the velocity */
		a[0] = fabsf (y[0]) > 0.9f ? 0.0f : 1.0f;
		a[1] = fabsf (y[0]) > 0.9f ? 1.0f : 0.0f;
		a[2] = 0.0f;
		z[0] = a[1] * y[2] - a[2] * y[1];
		z[1] = a[2] * y[0] - a[0] * y[2];
		z[2] = a[0] * y[1] - a[1] * y[0];
		len = sqrtf (z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
		for (k = 0; k < 3; k++)
			z[k] /= len;
		x[0] = y[1] * z[2] - y[2] * z[1];
		x[1] = y[2] * z[0] - y[0] * z[2];
		x[2] = y[0] * z[1] - y[1] * z[0];
		/* then the particle's rotation about that axis */
		axis_angle (y, p->rotation, spin);
		for (i = 0; i < 3; i++)
			for (k = 0; k < 3; k++)
			{
				const gfloat *col = i == 0 ? x : i == 1 ? y : z;

				r[i * 3 + k] = spin[0 + k] * col[0] + spin[3 + k] * col[1] + spin[6 + k] * col[2];
			}
		break;
	}
	case LRG_PARTICLE_MESH_ALIGN_TUMBLE:
	{
		/* a stable per-particle axis from its random number */
		gfloat u = p->random;
		gfloat zc = fmodf (u * 137.0f, 1.0f) * 2.0f - 1.0f;
		gfloat phi = u * 6.2831853f * 7.0f;
		gfloat h = sqrtf (MAX (0.0f, 1.0f - zc * zc));
		gfloat axis[3] = { h * cosf (phi), zc, h * sinf (phi) };

		axis_angle (axis, p->rotation, r);
		break;
	}
	case LRG_PARTICLE_MESH_ALIGN_NONE:
	default:
	{
		static const gfloat up[3] = { 0.0f, 1.0f, 0.0f };

		axis_angle (up, p->rotation, r);
		break;
	}
	}
}

typedef struct
{
	LrgParticleMeshRenderer *self;
	gfloat                  *out;
	guint                    n;
	guint                    max;
} BuildData;

static void
build_one (const LrgParticle *p,
           gpointer           user_data)
{
	BuildData *bd = user_data;
	gfloat    *m;
	gfloat     r[9];
	gint       c, k;

	if (bd->n >= bd->max)
		return;
	m = bd->out + (gsize) bd->n * LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE;
	orientation (bd->self->align, p, r);
	for (c = 0; c < 3; c++)
	{
		gfloat s = bd->self->scale[c] * p->size;

		for (k = 0; k < 3; k++)
			m[c * 4 + k] = r[c * 3 + k] * s;
		m[c * 4 + 3] = 0.0f;
	}
	m[12] = p->position_x;
	m[13] = p->position_y;
	m[14] = p->position_z;
	m[15] = 1.0f;
	m[16] = p->color_r;
	m[17] = p->color_g;
	m[18] = p->color_b;
	m[19] = p->color_a;
	bd->n++;
}

guint
lrg_particle_mesh_renderer_build (LrgParticleMeshRenderer *self,
                                  LrgParticleSystem       *system,
                                  gfloat                  *instances,
                                  guint                    max_instances)
{
	BuildData bd;

	g_return_val_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self), 0);
	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (system), 0);
	g_return_val_if_fail (instances != NULL || max_instances == 0, 0);

	bd.self = self;
	bd.out = instances;
	bd.n = 0;
	bd.max = max_instances;
	lrg_particle_system_foreach_active (system, build_one, &bd);
	return bd.n;
}

/* ==========================================================================
 * Drawing
 * ========================================================================== */

static gboolean
ensure_shader (LrgParticleMeshRenderer *self)
{
	if (self->shader_loaded)
		return self->shader.id != 0;
	self->shader_loaded = TRUE;
	self->shader = LoadShaderFromMemory (VERTEX_SHADER, FRAGMENT_SHADER);
	if (self->shader.id == 0 || self->shader.id == rlGetShaderIdDefault ())
	{
		g_warning ("LrgParticleMeshRenderer: the instancing shader did not compile");
		self->shader.id = 0;
		return FALSE;
	}
	self->loc_view = GetShaderLocation (self->shader, "matView");
	self->loc_projection = GetShaderLocation (self->shader, "matProjection");
	self->loc_light = GetShaderLocation (self->shader, "lightDir");
	self->loc_lit = GetShaderLocation (self->shader, "lit");
	self->loc_texture = GetShaderLocation (self->shader, "texture0");
	return TRUE;
}

/* The mesh to draw: the model's first mesh, the set mesh, or the
 * generated primitive */
static Mesh *
current_mesh (LrgParticleMeshRenderer *self)
{
	if (self->model != NULL)
	{
		Model *m = (Model *) grl_model_get_handle (self->model);

		if (m != NULL && m->meshCount > 0 && m->meshes[0].vaoId != 0)
			return &m->meshes[0];
	}
	if (self->mesh != NULL)
	{
		Mesh *m = (Mesh *) grl_mesh_get_handle (self->mesh);

		if (m != NULL && m->vaoId != 0)
			return m;
	}
	if (self->prim_loaded && self->prim_kind != self->primitive)
	{
		UnloadMesh (self->prim);
		self->prim_loaded = FALSE;
	}
	if (!self->prim_loaded)
	{
		switch (self->primitive)
		{
		case LRG_PARTICLE_MESH_PRIMITIVE_SPHERE:   self->prim = GenMeshSphere (0.5f, 12, 16); break;
		case LRG_PARTICLE_MESH_PRIMITIVE_CYLINDER: self->prim = GenMeshCylinder (0.5f, 1.0f, 12); break;
		case LRG_PARTICLE_MESH_PRIMITIVE_CONE:     self->prim = GenMeshCone (0.5f, 1.0f, 12); break;
		case LRG_PARTICLE_MESH_PRIMITIVE_PLANE:    self->prim = GenMeshPlane (1.0f, 1.0f, 1, 1); break;
		case LRG_PARTICLE_MESH_PRIMITIVE_CUBE:
		default:                                   self->prim = GenMeshCube (1.0f, 1.0f, 1.0f); break;
		}
		self->prim_kind = self->primitive;
		self->prim_loaded = self->prim.vaoId != 0;
	}
	return self->prim_loaded ? &self->prim : NULL;
}

void
lrg_particle_mesh_renderer_draw (LrgParticleMeshRenderer *self,
                                 LrgParticleSystem       *system)
{
	Mesh   *mesh;
	Matrix  view, projection;
	guint   active, n, k;
	gsize   bytes;
	gint    lit, slot = 0;
	guint   texture_id;
	gboolean additive;

	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	g_return_if_fail (LRG_IS_PARTICLE_SYSTEM (system));

	active = lrg_particle_system_get_active_count (system);
	if (active == 0 || !lrg_particle_system_is_layer_visible (system) || !IsWindowReady ())
		return;
	if (!ensure_shader (self))
		return;
	mesh = current_mesh (self);
	if (mesh == NULL)
		return;

	g_array_set_size (self->scratch, (guint) active * LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE);
	n = lrg_particle_mesh_renderer_build (self, system, (gfloat *) (void *) self->scratch->data, active);
	if (n == 0)
		return;

	/* the instance buffer grows to the largest count seen */
	bytes = (gsize) n * LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE * sizeof (gfloat);
	if (self->instance_vbo == 0 || bytes > self->instance_capacity)
	{
		if (self->instance_vbo != 0)
			rlUnloadVertexBuffer (self->instance_vbo);
		self->instance_capacity = MAX (bytes, (gsize) 64 * LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE * sizeof (gfloat));
		self->instance_vbo = rlLoadVertexBuffer (NULL, (gint) self->instance_capacity, true);
	}
	rlUpdateVertexBuffer (self->instance_vbo, self->scratch->data, (gint) bytes, 0);

	view = rlGetMatrixModelview ();
	projection = rlGetMatrixProjection ();
	texture_id = self->texture != NULL ? ((Texture2D *) grl_texture_get_handle (self->texture))->id
	                                   : rlGetTextureIdDefault ();
	additive = lrg_particle_system_get_blend_mode (system) == LRG_PARTICLE_BLEND_ADDITIVE;

	/* flush the batch, then our own state for one instanced call */
	rlDrawRenderBatchActive ();
	if (additive)
		rlDisableDepthMask ();
	BeginBlendMode (lrg_particle_system_get_blend_mode (system) == LRG_PARTICLE_BLEND_MULTIPLY ? BLEND_MULTIPLIED
	                : additive ? BLEND_ADDITIVE : BLEND_ALPHA);
	rlEnableShader (self->shader.id);
	rlSetUniformMatrix (self->loc_view, view);
	rlSetUniformMatrix (self->loc_projection, projection);
	rlSetUniform (self->loc_light, self->light_dir, RL_SHADER_UNIFORM_VEC3, 1);
	lit = self->lit ? 1 : 0;
	rlSetUniform (self->loc_lit, &lit, RL_SHADER_UNIFORM_INT, 1);
	rlActiveTextureSlot (0);
	rlEnableTexture (texture_id);
	rlSetUniform (self->loc_texture, &slot, RL_SHADER_UNIFORM_INT, 1);

	if (rlEnableVertexArray (mesh->vaoId))
	{
		rlEnableVertexBuffer (self->instance_vbo);
		for (k = 0; k < 4; k++)
		{
			rlSetVertexAttribute (INSTANCE_LOCATION + k, 4, RL_FLOAT, false,
			                      LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE * (gint) sizeof (gfloat),
			                      (gint) (k * 4 * sizeof (gfloat)));
			rlEnableVertexAttribute (INSTANCE_LOCATION + k);
			rlSetVertexAttributeDivisor (INSTANCE_LOCATION + k, 1);
		}
		rlSetVertexAttribute (INSTANCE_LOCATION + 4, 4, RL_FLOAT, false,
		                      LRG_PARTICLE_MESH_FLOATS_PER_INSTANCE * (gint) sizeof (gfloat),
		                      (gint) (16 * sizeof (gfloat)));
		rlEnableVertexAttribute (INSTANCE_LOCATION + 4);
		rlSetVertexAttributeDivisor (INSTANCE_LOCATION + 4, 1);

		if (mesh->indices != NULL)
		{
			rlEnableVertexBufferElement (mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_INDICES]);
			rlDrawVertexArrayElementsInstanced (0, mesh->triangleCount * 3, 0, (gint) n);
		}
		else
			rlDrawVertexArrayInstanced (0, mesh->vertexCount, (gint) n);

		/* other users of this vertex array never see the instance data */
		for (k = 0; k < 5; k++)
		{
			rlSetVertexAttributeDivisor (INSTANCE_LOCATION + k, 0);
			rlDisableVertexAttribute (INSTANCE_LOCATION + k);
		}
		rlDisableVertexArray ();
		rlDisableVertexBuffer ();
		rlDisableVertexBufferElement ();
	}
	rlDisableTexture ();
	rlDisableShader ();
	EndBlendMode ();
	if (additive)
		rlEnableDepthMask ();
}

void
lrg_particle_mesh_renderer_release_gpu (LrgParticleMeshRenderer *self)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));

	if (IsWindowReady ())
	{
		if (self->shader_loaded && self->shader.id != 0)
			UnloadShader (self->shader);
		if (self->instance_vbo != 0)
			rlUnloadVertexBuffer (self->instance_vbo);
		if (self->prim_loaded)
			UnloadMesh (self->prim);
	}
	self->shader_loaded = FALSE;
	memset (&self->shader, 0, sizeof self->shader);
	self->instance_vbo = 0;
	self->instance_capacity = 0;
	self->prim_loaded = FALSE;
	memset (&self->prim, 0, sizeof self->prim);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_particle_mesh_renderer_finalize (GObject *object)
{
	LrgParticleMeshRenderer *self = LRG_PARTICLE_MESH_RENDERER (object);

	lrg_particle_mesh_renderer_release_gpu (self);
	g_clear_object (&self->mesh);
	g_clear_object (&self->model);
	g_clear_object (&self->texture);
	g_clear_pointer (&self->scratch, g_array_unref);
	G_OBJECT_CLASS (lrg_particle_mesh_renderer_parent_class)->finalize (object);
}

static void
lrg_particle_mesh_renderer_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
	LrgParticleMeshRenderer *self = LRG_PARTICLE_MESH_RENDERER (object);

	switch (prop_id)
	{
	case PROP_MESH:      g_value_set_object (value, self->mesh); break;
	case PROP_PRIMITIVE: g_value_set_enum (value, self->primitive); break;
	case PROP_ALIGN:     g_value_set_enum (value, self->align); break;
	case PROP_LIT:       g_value_set_boolean (value, self->lit); break;
	case PROP_TEXTURE:   g_value_set_object (value, self->texture); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_mesh_renderer_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
	LrgParticleMeshRenderer *self = LRG_PARTICLE_MESH_RENDERER (object);

	switch (prop_id)
	{
	case PROP_MESH:      lrg_particle_mesh_renderer_set_mesh (self, g_value_get_object (value)); break;
	case PROP_PRIMITIVE: lrg_particle_mesh_renderer_set_primitive (self, g_value_get_enum (value)); break;
	case PROP_ALIGN:     lrg_particle_mesh_renderer_set_align (self, g_value_get_enum (value)); break;
	case PROP_LIT:       lrg_particle_mesh_renderer_set_lit (self, g_value_get_boolean (value)); break;
	case PROP_TEXTURE:   lrg_particle_mesh_renderer_set_texture (self, g_value_get_object (value)); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_particle_mesh_renderer_class_init (LrgParticleMeshRendererClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS;

	object_class->finalize = lrg_particle_mesh_renderer_finalize;
	object_class->get_property = lrg_particle_mesh_renderer_get_property;
	object_class->set_property = lrg_particle_mesh_renderer_set_property;

	/**
	 * LrgParticleMeshRenderer:mesh:
	 *
	 * The uploaded mesh to instance, or %NULL for the primitive.
	 */
	properties[PROP_MESH] =
		g_param_spec_object ("mesh", "Mesh", "Mesh to instance", GRL_TYPE_MESH, rw);

	/**
	 * LrgParticleMeshRenderer:primitive:
	 *
	 * The built-in mesh drawn without a mesh or model.
	 */
	properties[PROP_PRIMITIVE] =
		g_param_spec_enum ("primitive", "Primitive", "Built-in mesh", LRG_TYPE_PARTICLE_MESH_PRIMITIVE,
		                   LRG_PARTICLE_MESH_PRIMITIVE_CUBE, rw);

	/**
	 * LrgParticleMeshRenderer:align:
	 *
	 * How particles are oriented.
	 */
	properties[PROP_ALIGN] =
		g_param_spec_enum ("align", "Align", "Orientation mode", LRG_TYPE_PARTICLE_MESH_ALIGN,
		                   LRG_PARTICLE_MESH_ALIGN_TUMBLE, rw);

	/**
	 * LrgParticleMeshRenderer:lit:
	 *
	 * Whether instances are shaded by the light direction.
	 */
	properties[PROP_LIT] =
		g_param_spec_boolean ("lit", "Lit", "Half-Lambert shading", TRUE, rw);

	/**
	 * LrgParticleMeshRenderer:texture:
	 *
	 * The albedo texture, or %NULL for white.
	 */
	properties[PROP_TEXTURE] =
		g_param_spec_object ("texture", "Texture", "Albedo texture", GRL_TYPE_TEXTURE, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_particle_mesh_renderer_init (LrgParticleMeshRenderer *self)
{
	self->primitive = LRG_PARTICLE_MESH_PRIMITIVE_CUBE;
	self->scale[0] = self->scale[1] = self->scale[2] = 1.0f;
	self->align = LRG_PARTICLE_MESH_ALIGN_TUMBLE;
	self->lit = TRUE;
	self->light_dir[0] = 0.3f;
	self->light_dir[1] = -1.0f;
	self->light_dir[2] = 0.2f;
	self->scratch = g_array_new (FALSE, FALSE, sizeof (gfloat));
}

LrgParticleMeshRenderer *
lrg_particle_mesh_renderer_new (void)
{
	return g_object_new (LRG_TYPE_PARTICLE_MESH_RENDERER, NULL);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

GrlMesh *
lrg_particle_mesh_renderer_get_mesh (LrgParticleMeshRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self), NULL);
	return self->mesh;
}

void
lrg_particle_mesh_renderer_set_mesh (LrgParticleMeshRenderer *self,
                                     GrlMesh                 *mesh)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	if (g_set_object (&self->mesh, mesh))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MESH]);
}

void
lrg_particle_mesh_renderer_set_model (LrgParticleMeshRenderer *self,
                                      GrlModel                *model)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	g_set_object (&self->model, model);
}

LrgParticleMeshPrimitive
lrg_particle_mesh_renderer_get_primitive (LrgParticleMeshRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self), LRG_PARTICLE_MESH_PRIMITIVE_CUBE);
	return self->primitive;
}

void
lrg_particle_mesh_renderer_set_primitive (LrgParticleMeshRenderer  *self,
                                          LrgParticleMeshPrimitive  primitive)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	if (self->primitive == primitive)
		return;
	self->primitive = primitive;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIMITIVE]);
}

void
lrg_particle_mesh_renderer_get_scale (LrgParticleMeshRenderer *self,
                                      gfloat                  *x,
                                      gfloat                  *y,
                                      gfloat                  *z)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	if (x != NULL)
		*x = self->scale[0];
	if (y != NULL)
		*y = self->scale[1];
	if (z != NULL)
		*z = self->scale[2];
}

void
lrg_particle_mesh_renderer_set_scale (LrgParticleMeshRenderer *self,
                                      gfloat                   x,
                                      gfloat                   y,
                                      gfloat                   z)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	self->scale[0] = x;
	self->scale[1] = y;
	self->scale[2] = z;
}

LrgParticleMeshAlign
lrg_particle_mesh_renderer_get_align (LrgParticleMeshRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self), LRG_PARTICLE_MESH_ALIGN_NONE);
	return self->align;
}

void
lrg_particle_mesh_renderer_set_align (LrgParticleMeshRenderer *self,
                                      LrgParticleMeshAlign     align)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	if (self->align == align)
		return;
	self->align = align;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALIGN]);
}

gboolean
lrg_particle_mesh_renderer_get_lit (LrgParticleMeshRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self), FALSE);
	return self->lit;
}

void
lrg_particle_mesh_renderer_set_lit (LrgParticleMeshRenderer *self,
                                    gboolean                 lit)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	lit = !!lit;
	if (self->lit == lit)
		return;
	self->lit = lit;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIT]);
}

void
lrg_particle_mesh_renderer_set_light_direction (LrgParticleMeshRenderer *self,
                                                gfloat                   x,
                                                gfloat                   y,
                                                gfloat                   z)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	self->light_dir[0] = x;
	self->light_dir[1] = y;
	self->light_dir[2] = z;
}

GrlTexture *
lrg_particle_mesh_renderer_get_texture (LrgParticleMeshRenderer *self)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self), NULL);
	return self->texture;
}

void
lrg_particle_mesh_renderer_set_texture (LrgParticleMeshRenderer *self,
                                        GrlTexture              *texture)
{
	g_return_if_fail (LRG_IS_PARTICLE_MESH_RENDERER (self));
	if (g_set_object (&self->texture, texture))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXTURE]);
}
