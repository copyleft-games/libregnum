/* lrg-gpu-particles.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGpuParticles: GPU particle simulation by transform feedback.
 *
 * GL 3.3 has no compute shaders, so the simulation is a vertex shader
 * run over every particle slot with rasterisation turned off: it reads
 * slot i from one vertex buffer and writes the stepped particle into the
 * same slot of a second buffer through transform feedback; the buffers
 * then swap (ping-pong). Emission is a ring: an update that emits n
 * particles hands the shader the range of n slots after the ring head,
 * and the shader spawns a fresh particle in each of those slots (the
 * oldest particle in the ring is the one replaced when the capacity is
 * too small). Dead particles simply stay dead; the render pass collapses
 * them to a point outside the clip volume, so no compaction (and no
 * read-back or query stall) is ever needed.
 *
 * Every random number a particle uses is a hash of the emitter seed, the
 * particle's spawn index and a channel number (integer arithmetic, the
 * same on the CPU and the GPU), so the C simulation below draws exactly
 * the same numbers as the shader: the CPU backend is both the fallback
 * when there is no GL context and the reference the tests compare the
 * GPU against (motion agrees to float rounding of sin/pow/sqrt).
 *
 * GL entry points are resolved with rlGetProcAddress(), the way
 * lrg-light-set.c and lrg-shader-asset.c do: graylib wraps none of
 * transform feedback, instancing or buffer read-back.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "lrg-gpu-particles.h"
#include "../graphics/lrg-camera.h"
#include "../core/lrg-prewarm.h"
#include "../core/lrg-prewarmable.h"

#define FPP            (LRG_GPU_PARTICLES_FLOATS_PER_PARTICLE)
#define TABLE          (LRG_GPU_PARTICLES_TABLE_SIZE)
#define MAX_CAPACITY   (4194304u)
#define GPU_TAU        (6.28318530718f)

/* ==========================================================================
 * Enums and error domain
 * ========================================================================== */

G_DEFINE_QUARK (lrg-gpu-particles-error-quark, lrg_gpu_particles_error)

GType
lrg_gpu_particles_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_GPU_PARTICLES_ERROR_UNSUPPORTED, "LRG_GPU_PARTICLES_ERROR_UNSUPPORTED", "unsupported" },
			{ LRG_GPU_PARTICLES_ERROR_NO_GPU, "LRG_GPU_PARTICLES_ERROR_NO_GPU", "no-gpu" },
			{ LRG_GPU_PARTICLES_ERROR_SHADER, "LRG_GPU_PARTICLES_ERROR_SHADER", "shader" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgGpuParticlesError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_gpu_particles_shape_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_GPU_PARTICLES_SHAPE_POINT, "LRG_GPU_PARTICLES_SHAPE_POINT", "point" },
			{ LRG_GPU_PARTICLES_SHAPE_SPHERE, "LRG_GPU_PARTICLES_SHAPE_SPHERE", "sphere" },
			{ LRG_GPU_PARTICLES_SHAPE_BOX, "LRG_GPU_PARTICLES_SHAPE_BOX", "box" },
			{ LRG_GPU_PARTICLES_SHAPE_CONE, "LRG_GPU_PARTICLES_SHAPE_CONE", "cone" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgGpuParticlesShape"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_gpu_particles_render_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_GPU_PARTICLES_RENDER_BILLBOARD, "LRG_GPU_PARTICLES_RENDER_BILLBOARD", "billboard" },
			{ LRG_GPU_PARTICLES_RENDER_STRETCHED, "LRG_GPU_PARTICLES_RENDER_STRETCHED", "stretched" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgGpuParticlesRenderMode"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * GL entry points (rlgl wraps none of transform feedback)
 * ========================================================================== */

#define GL_ARRAY_BUFFER_              (0x8892)
#define GL_DYNAMIC_COPY_              (0x88EA)
#define GL_STATIC_DRAW_               (0x88E4)
#define GL_FLOAT_                     (0x1406)
#define GL_POINTS_                    (0x0000)
#define GL_TRIANGLE_STRIP_            (0x0005)
#define GL_TRANSFORM_FEEDBACK_BUFFER_ (0x8C8E)
#define GL_INTERLEAVED_ATTRIBS_       (0x8C8C)
#define GL_RASTERIZER_DISCARD_        (0x8C89)
#define GL_VERTEX_SHADER_             (0x8B31)
#define GL_FRAGMENT_SHADER_           (0x8B30)
#define GL_COMPILE_STATUS_            (0x8B81)
#define GL_LINK_STATUS_               (0x8B82)
#define GL_INFO_LOG_LENGTH_           (0x8B84)
#define GL_TEXTURE0_                  (0x84C0)
#define GL_TEXTURE_2D_                (0x0DE1)

typedef void (*GlGenBuffers) (gint n, guint *ids);
typedef void (*GlDeleteBuffers) (gint n, const guint *ids);
typedef void (*GlBindBuffer) (guint target, guint id);
typedef void (*GlBufferData) (guint target, gssize size, const void *data, guint usage);
typedef void (*GlBufferSubData) (guint target, gssize offset, gssize size, const void *data);
typedef void (*GlGetBufferSubData) (guint target, gssize offset, gssize size, void *data);
typedef void (*GlGenVertexArrays) (gint n, guint *ids);
typedef void (*GlDeleteVertexArrays) (gint n, const guint *ids);
typedef void (*GlBindVertexArray) (guint id);
typedef void (*GlVertexAttribPointer) (guint index, gint size, guint type, guint8 normalized,
                                       gint stride, const void *pointer);
typedef void (*GlEnableVertexAttribArray) (guint index);
typedef void (*GlVertexAttribDivisor) (guint index, guint divisor);
typedef guint (*GlCreateShader) (guint type);
typedef void (*GlShaderSource) (guint shader, gint count, const gchar * const *strings,
                                const gint *lengths);
typedef void (*GlCompileShader) (guint shader);
typedef void (*GlGetShaderiv) (guint shader, guint pname, gint *params);
typedef void (*GlGetShaderInfoLog) (guint shader, gint size, gint *length, gchar *log);
typedef void (*GlDeleteShader) (guint shader);
typedef guint (*GlCreateProgram) (void);
typedef void (*GlAttachShader) (guint program, guint shader);
typedef void (*GlLinkProgram) (guint program);
typedef void (*GlGetProgramiv) (guint program, guint pname, gint *params);
typedef void (*GlGetProgramInfoLog) (guint program, gint size, gint *length, gchar *log);
typedef void (*GlDeleteProgram) (guint program);
typedef void (*GlUseProgram) (guint program);
typedef gint (*GlGetUniformLocation) (guint program, const gchar *name);
typedef void (*GlUniform1f) (gint loc, gfloat v);
typedef void (*GlUniform2f) (gint loc, gfloat a, gfloat b);
typedef void (*GlUniform3f) (gint loc, gfloat a, gfloat b, gfloat c);
typedef void (*GlUniform4f) (gint loc, gfloat a, gfloat b, gfloat c, gfloat d);
typedef void (*GlUniform1i) (gint loc, gint v);
typedef void (*GlUniform1ui) (gint loc, guint v);
typedef void (*GlUniform1fv) (gint loc, gint count, const gfloat *v);
typedef void (*GlUniform4fv) (gint loc, gint count, const gfloat *v);
typedef void (*GlUniformMatrix4fv) (gint loc, gint count, guint8 transpose, const gfloat *v);
typedef void (*GlTransformFeedbackVaryings) (guint program, gint count,
                                             const gchar * const *varyings, guint mode);
typedef void (*GlBindBufferBase) (guint target, guint index, guint buffer);
typedef void (*GlBeginTransformFeedback) (guint mode);
typedef void (*GlEndTransformFeedback) (void);
typedef void (*GlDrawArrays) (guint mode, gint first, gint count);
typedef void (*GlDrawArraysInstanced) (guint mode, gint first, gint count, gint instances);
typedef void (*GlEnable) (guint cap);
typedef void (*GlDisable) (guint cap);
typedef void (*GlActiveTexture) (guint unit);
typedef void (*GlBindTexture) (guint target, guint id);

typedef struct
{
	gboolean                     tried;
	gboolean                     base_ok;   /* buffers, programs, instancing */
	gboolean                     tf_ok;     /* + transform feedback */
	GlGenBuffers                 gen_buffers;
	GlDeleteBuffers              delete_buffers;
	GlBindBuffer                 bind_buffer;
	GlBufferData                 buffer_data;
	GlBufferSubData              buffer_sub_data;
	GlGetBufferSubData           get_buffer_sub_data;
	GlGenVertexArrays            gen_vertex_arrays;
	GlDeleteVertexArrays         delete_vertex_arrays;
	GlBindVertexArray            bind_vertex_array;
	GlVertexAttribPointer        vertex_attrib_pointer;
	GlEnableVertexAttribArray    enable_vertex_attrib_array;
	GlVertexAttribDivisor        vertex_attrib_divisor;
	GlCreateShader               create_shader;
	GlShaderSource               shader_source;
	GlCompileShader              compile_shader;
	GlGetShaderiv                get_shaderiv;
	GlGetShaderInfoLog           get_shader_info_log;
	GlDeleteShader               delete_shader;
	GlCreateProgram              create_program;
	GlAttachShader               attach_shader;
	GlLinkProgram                link_program;
	GlGetProgramiv               get_programiv;
	GlGetProgramInfoLog          get_program_info_log;
	GlDeleteProgram              delete_program;
	GlUseProgram                 use_program;
	GlGetUniformLocation         get_uniform_location;
	GlUniform1f                  uniform1f;
	GlUniform2f                  uniform2f;
	GlUniform3f                  uniform3f;
	GlUniform4f                  uniform4f;
	GlUniform1i                  uniform1i;
	GlUniform1ui                 uniform1ui;
	GlUniform1fv                 uniform1fv;
	GlUniform4fv                 uniform4fv;
	GlUniformMatrix4fv           uniform_matrix4fv;
	GlTransformFeedbackVaryings  transform_feedback_varyings;
	GlBindBufferBase             bind_buffer_base;
	GlBeginTransformFeedback     begin_transform_feedback;
	GlEndTransformFeedback       end_transform_feedback;
	GlDrawArrays                 draw_arrays;
	GlDrawArraysInstanced        draw_arrays_instanced;
	GlEnable                     enable;
	GlDisable                    disable;
	GlActiveTexture              active_texture;
	GlBindTexture                bind_texture;
} GpuGl;

static GpuGl gl;

#define LOAD(field, name) gl.field = (__typeof__ (gl.field)) rlGetProcAddress (name)

/* gpu_gl_load:
 * Resolves the entry points once per process (only with a context: the
 * loader returns NULL before InitWindow()). Returns whether the base set
 * (buffers, shaders, instancing) is there; gl.tf_ok adds transform
 * feedback. */
static gboolean
gpu_gl_load (void)
{
	if (!IsWindowReady ())
		return FALSE;
	if (gl.tried)
		return gl.base_ok;
	gl.tried = TRUE;

	LOAD (gen_buffers, "glGenBuffers");
	LOAD (delete_buffers, "glDeleteBuffers");
	LOAD (bind_buffer, "glBindBuffer");
	LOAD (buffer_data, "glBufferData");
	LOAD (buffer_sub_data, "glBufferSubData");
	LOAD (get_buffer_sub_data, "glGetBufferSubData");
	LOAD (gen_vertex_arrays, "glGenVertexArrays");
	LOAD (delete_vertex_arrays, "glDeleteVertexArrays");
	LOAD (bind_vertex_array, "glBindVertexArray");
	LOAD (vertex_attrib_pointer, "glVertexAttribPointer");
	LOAD (enable_vertex_attrib_array, "glEnableVertexAttribArray");
	LOAD (vertex_attrib_divisor, "glVertexAttribDivisor");
	LOAD (create_shader, "glCreateShader");
	LOAD (shader_source, "glShaderSource");
	LOAD (compile_shader, "glCompileShader");
	LOAD (get_shaderiv, "glGetShaderiv");
	LOAD (get_shader_info_log, "glGetShaderInfoLog");
	LOAD (delete_shader, "glDeleteShader");
	LOAD (create_program, "glCreateProgram");
	LOAD (attach_shader, "glAttachShader");
	LOAD (link_program, "glLinkProgram");
	LOAD (get_programiv, "glGetProgramiv");
	LOAD (get_program_info_log, "glGetProgramInfoLog");
	LOAD (delete_program, "glDeleteProgram");
	LOAD (use_program, "glUseProgram");
	LOAD (get_uniform_location, "glGetUniformLocation");
	LOAD (uniform1f, "glUniform1f");
	LOAD (uniform2f, "glUniform2f");
	LOAD (uniform3f, "glUniform3f");
	LOAD (uniform4f, "glUniform4f");
	LOAD (uniform1i, "glUniform1i");
	LOAD (uniform1ui, "glUniform1ui");
	LOAD (uniform1fv, "glUniform1fv");
	LOAD (uniform4fv, "glUniform4fv");
	LOAD (uniform_matrix4fv, "glUniformMatrix4fv");
	LOAD (transform_feedback_varyings, "glTransformFeedbackVaryings");
	LOAD (bind_buffer_base, "glBindBufferBase");
	LOAD (begin_transform_feedback, "glBeginTransformFeedback");
	LOAD (end_transform_feedback, "glEndTransformFeedback");
	LOAD (draw_arrays, "glDrawArrays");
	LOAD (draw_arrays_instanced, "glDrawArraysInstanced");
	LOAD (enable, "glEnable");
	LOAD (disable, "glDisable");
	LOAD (active_texture, "glActiveTexture");
	LOAD (bind_texture, "glBindTexture");

	gl.base_ok = gl.gen_buffers != NULL && gl.delete_buffers != NULL && gl.bind_buffer != NULL &&
	             gl.buffer_data != NULL && gl.buffer_sub_data != NULL &&
	             gl.get_buffer_sub_data != NULL && gl.gen_vertex_arrays != NULL &&
	             gl.delete_vertex_arrays != NULL && gl.bind_vertex_array != NULL &&
	             gl.vertex_attrib_pointer != NULL && gl.enable_vertex_attrib_array != NULL &&
	             gl.vertex_attrib_divisor != NULL && gl.create_shader != NULL &&
	             gl.shader_source != NULL && gl.compile_shader != NULL &&
	             gl.get_shaderiv != NULL && gl.get_shader_info_log != NULL &&
	             gl.delete_shader != NULL && gl.create_program != NULL &&
	             gl.attach_shader != NULL && gl.link_program != NULL &&
	             gl.get_programiv != NULL && gl.get_program_info_log != NULL &&
	             gl.delete_program != NULL && gl.use_program != NULL &&
	             gl.get_uniform_location != NULL && gl.uniform1f != NULL && gl.uniform2f != NULL &&
	             gl.uniform3f != NULL && gl.uniform4f != NULL && gl.uniform1i != NULL &&
	             gl.uniform1ui != NULL && gl.uniform1fv != NULL && gl.uniform4fv != NULL &&
	             gl.uniform_matrix4fv != NULL && gl.draw_arrays != NULL &&
	             gl.draw_arrays_instanced != NULL && gl.enable != NULL && gl.disable != NULL &&
	             gl.active_texture != NULL && gl.bind_texture != NULL;
	gl.tf_ok = gl.base_ok && gl.transform_feedback_varyings != NULL &&
	           gl.bind_buffer_base != NULL && gl.begin_transform_feedback != NULL &&
	           gl.end_transform_feedback != NULL;
	return gl.base_ok;
}

#undef LOAD

/* ==========================================================================
 * Shaders
 *
 * The update shader is a line-by-line twin of cpu_spawn() and
 * cpu_integrate() below: change one, change the other (the GPU-vs-CPU
 * test in tests/test-gpu-particles.c catches a drift).
 * ========================================================================== */

static const gchar update_vs[] =
	"#version 330\n"
	"layout(location = 0) in vec4 a_pos_age;\n"
	"layout(location = 1) in vec4 a_vel_life;\n"
	"layout(location = 2) in vec4 a_misc;\n"
	"out vec4 o_pos_age;\n"
	"out vec4 o_vel_life;\n"
	"out vec4 o_misc;\n"
	"uniform float u_dt;\n"
	"uniform uint  u_seed;\n"
	"uniform uint  u_capacity;\n"
	"uniform uint  u_emit_start;\n"
	"uniform uint  u_emit_count;\n"
	"uniform uint  u_spawn_base;\n"
	"uniform vec3  u_origin_prev;\n"
	"uniform vec3  u_origin;\n"
	"uniform vec3  u_dir;\n"
	"uniform vec3  u_tan;\n"
	"uniform vec3  u_bit;\n"
	"uniform int   u_shape;\n"
	"uniform float u_radius;\n"
	"uniform vec3  u_box;\n"
	"uniform float u_cone_cos;\n"
	"uniform vec2  u_speed;\n"
	"uniform vec2  u_life;\n"
	"uniform vec2  u_rot;\n"
	"uniform vec2  u_spin;\n"
	"uniform vec3  u_gravity;\n"
	"uniform vec3  u_wind;\n"
	"uniform float u_drag;\n"
	"uniform vec3  u_turb;\n"          /* strength, frequency, phase */
	"uniform vec4  u_attr;\n"          /* position, strength */
	"uniform vec2  u_attr_shape;\n"    /* radius, falloff exponent */
	"uniform float u_vortex;\n"
	"uniform int   u_collide;\n"
	"uniform vec3  u_plane;\n"         /* height, bounce, friction */
	"const float TAU = 6.28318530718;\n"
	"uint hash32 (uint x)\n"
	"{\n"
	"  x ^= x >> 16u; x *= 0x7feb352du;\n"
	"  x ^= x >> 15u; x *= 0x846ca68bu;\n"
	"  x ^= x >> 16u;\n"
	"  return x;\n"
	"}\n"
	"float rnd (uint id, uint ch)\n"
	"{\n"
	"  return float (hash32 (u_seed ^ hash32 (id * 16u + ch)) >> 8u) * (1.0 / 16777216.0);\n"
	"}\n"
	"vec3 cone_dir (float u, float v)\n"
	"{\n"
	"  float ct = 1.0 - u * (1.0 - u_cone_cos);\n"
	"  float st = sqrt (max (0.0, 1.0 - ct * ct));\n"
	"  float phi = TAU * v;\n"
	"  return u_tan * (st * cos (phi)) + u_bit * (st * sin (phi)) + u_dir * ct;\n"
	"}\n"
	"vec3 turbulence (vec3 p)\n"
	"{\n"
	"  float f = u_turb.y;\n"
	"  float t = u_turb.z;\n"
	"  return (u_turb.x * 0.5) * vec3 (sin (f * p.y + 1.7 * t) + sin (1.3 * f * p.z - 0.9 * t),\n"
	"                                  sin (f * p.z + 1.1 * t) + sin (1.5 * f * p.x + 0.7 * t),\n"
	"                                  sin (f * p.x + 1.3 * t) + sin (1.1 * f * p.y - 1.2 * t));\n"
	"}\n"
	"void main ()\n"
	"{\n"
	"  uint slot = uint (gl_VertexID);\n"
	"  uint k = (slot + u_capacity - u_emit_start) % u_capacity;\n"
	"  if (k < u_emit_count)\n"
	"  {\n"
	"    uint id = u_spawn_base + k;\n"
	"    float frac = (float (k) + 0.5) / float (u_emit_count);\n"
	"    float age = (1.0 - frac) * u_dt;\n"
	"    vec3 origin = u_origin_prev + (u_origin - u_origin_prev) * frac;\n"
	"    vec3 off = vec3 (0.0);\n"
	"    vec3 vdir;\n"
	"    float r0 = rnd (id, 0u);\n"
	"    float r1 = rnd (id, 1u);\n"
	"    float r2 = rnd (id, 2u);\n"
	"    if (u_shape == 1)\n"
	"    {\n"
	"      float z = 2.0 * r0 - 1.0;\n"
	"      float phi = TAU * r1;\n"
	"      float sxy = sqrt (max (0.0, 1.0 - z * z));\n"
	"      vec3 n = vec3 (sxy * cos (phi), z, sxy * sin (phi));\n"
	"      off = n * (u_radius * pow (r2, 1.0 / 3.0));\n"
	"      vdir = n;\n"
	"    }\n"
	"    else\n"
	"    {\n"
	"      vdir = cone_dir (rnd (id, 3u), rnd (id, 4u));\n"
	"      if (u_shape == 2)\n"
	"        off = (vec3 (r0, r1, r2) - 0.5) * u_box;\n"
	"      else if (u_shape == 3)\n"
	"      {\n"
	"        float rad = u_radius * sqrt (r0);\n"
	"        float phi = TAU * r1;\n"
	"        off = (u_tan * cos (phi) + u_bit * sin (phi)) * rad;\n"
	"      }\n"
	"    }\n"
	"    float speed = u_speed.x + (u_speed.y - u_speed.x) * rnd (id, 5u);\n"
	"    float life = u_life.x + (u_life.y - u_life.x) * rnd (id, 6u);\n"
	"    float rot = u_rot.x + (u_rot.y - u_rot.x) * rnd (id, 7u);\n"
	"    float spin = u_spin.x + (u_spin.y - u_spin.x) * rnd (id, 8u);\n"
	"    vec3 vel = vdir * speed;\n"
	"    o_pos_age = vec4 (origin + off + vel * age, age);\n"
	"    o_vel_life = vec4 (vel, life);\n"
	"    o_misc = vec4 (rot + spin * age, spin, float (id & 65535u), float (id >> 16u));\n"
	"    return;\n"
	"  }\n"
	"  vec3 p = a_pos_age.xyz;\n"
	"  vec3 v = a_vel_life.xyz;\n"
	"  if (a_pos_age.w < a_vel_life.w)\n"
	"  {\n"
	"    vec3 acc = u_gravity;\n"
	"    if (u_turb.x != 0.0)\n"
	"      acc += turbulence (p);\n"
	"    if (u_attr.w != 0.0)\n"
	"    {\n"
	"      vec3 d = u_attr.xyz - p;\n"
	"      float dist = length (d);\n"
	"      if (dist > 0.0001 && (u_attr_shape.x <= 0.0 || dist < u_attr_shape.x))\n"
	"      {\n"
	"        float fall = u_attr_shape.x > 0.0 ? pow (1.0 - dist / u_attr_shape.x, u_attr_shape.y) : 1.0;\n"
	"        acc += d * (u_attr.w * fall / dist);\n"
	"      }\n"
	"    }\n"
	"    if (u_vortex != 0.0)\n"
	"    {\n"
	"      vec2 r = p.xz - u_origin.xz;\n"
	"      float l = max (length (r), 0.1);\n"
	"      acc.xz += vec2 (-r.y, r.x) * (u_vortex / l);\n"
	"    }\n"
	"    v += acc * u_dt;\n"
	"    if (u_drag > 0.0)\n"
	"      v += (u_wind - v) * min (u_drag * u_dt, 1.0);\n"
	"    p += v * u_dt;\n"
	"    if (u_collide != 0 && p.y < u_plane.x && v.y < 0.0)\n"
	"    {\n"
	"      p.y = u_plane.x + (u_plane.x - p.y) * u_plane.y;\n"
	"      v.y = -v.y * u_plane.y;\n"
	"      v.xz *= 1.0 - u_plane.z;\n"
	"    }\n"
	"    o_pos_age = vec4 (p, a_pos_age.w + u_dt);\n"
	"    o_vel_life = vec4 (v, a_vel_life.w);\n"
	"    o_misc = vec4 (a_misc.x + a_misc.y * u_dt, a_misc.yzw);\n"
	"    return;\n"
	"  }\n"
	"  o_pos_age = a_pos_age;\n"
	"  o_vel_life = a_vel_life;\n"
	"  o_misc = a_misc;\n"
	"}\n";

/* rasterisation is off during the update; a fragment stage keeps strict
 * drivers happy */
static const gchar update_fs[] =
	"#version 330\n"
	"out vec4 frag;\n"
	"void main () { frag = vec4 (0.0); }\n";

static const gchar render_vs[] =
	"#version 330\n"
	"layout(location = 0) in vec2 a_corner;\n"
	"layout(location = 1) in vec4 a_pos_age;\n"
	"layout(location = 2) in vec4 a_vel_life;\n"
	"layout(location = 3) in vec4 a_misc;\n"
	"uniform mat4  u_view;\n"
	"uniform mat4  u_proj;\n"
	"uniform vec4  u_colors[16];\n"
	"uniform float u_sizes[16];\n"
	"uniform int   u_mode;\n"
	"uniform float u_stretch;\n"
	"uniform uint  u_seed;\n"
	"uniform float u_size_random;\n"
	"out vec4 v_color;\n"
	"out vec2 v_uv;\n"
	"out vec2 v_corner;\n"
	"uint hash32 (uint x)\n"
	"{\n"
	"  x ^= x >> 16u; x *= 0x7feb352du;\n"
	"  x ^= x >> 15u; x *= 0x846ca68bu;\n"
	"  x ^= x >> 16u;\n"
	"  return x;\n"
	"}\n"
	"void main ()\n"
	"{\n"
	"  float life = a_vel_life.w;\n"
	"  float age = a_pos_age.w;\n"
	"  v_uv = a_corner * 0.5 + 0.5;\n"
	"  v_corner = a_corner;\n"
	"  if (!(age < life))\n"
	"  {\n"
	"    v_color = vec4 (0.0);\n"
	"    gl_Position = vec4 (0.0, 0.0, 2.0, 1.0);\n"
	"    return;\n"
	"  }\n"
	"  float t = clamp (age / life, 0.0, 1.0) * 15.0;\n"
	"  int i = min (int (t), 14);\n"
	"  float f = t - float (i);\n"
	"  v_color = mix (u_colors[i], u_colors[i + 1], f);\n"
	"  float size = mix (u_sizes[i], u_sizes[i + 1], f);\n"
	"  if (u_size_random > 0.0)\n"
	"  {\n"
	"    uint id = uint (a_misc.z) | (uint (a_misc.w) << 16u);\n"
	"    float r = float (hash32 (u_seed ^ hash32 (id * 16u + 9u)) >> 8u) * (1.0 / 16777216.0);\n"
	"    size *= 1.0 - u_size_random * r;\n"
	"  }\n"
	"  vec3 vp = (u_view * vec4 (a_pos_age.xyz, 1.0)).xyz;\n"
	"  vec2 c = a_corner;\n"
	"  if (u_mode == 1)\n"
	"  {\n"
	"    vec2 vv = (mat3 (u_view) * a_vel_life.xyz).xy;\n"
	"    float l = length (vv);\n"
	"    vec2 ax = l > 0.00001 ? vv / l : vec2 (0.0, 1.0);\n"
	"    vec2 pp = vec2 (-ax.y, ax.x);\n"
	"    float len = l * u_stretch;\n"
	"    vp.xy += pp * (c.x * size * 0.5) + ax * (c.y * size * 0.5 + (c.y - 1.0) * 0.5 * len);\n"
	"  }\n"
	"  else\n"
	"  {\n"
	"    float cr = cos (a_misc.x);\n"
	"    float sr = sin (a_misc.x);\n"
	"    vp.xy += vec2 (cr * c.x - sr * c.y, sr * c.x + cr * c.y) * (size * 0.5);\n"
	"  }\n"
	"  gl_Position = u_proj * vec4 (vp, 1.0);\n"
	"}\n";

static const gchar render_fs[] =
	"#version 330\n"
	"in vec4 v_color;\n"
	"in vec2 v_uv;\n"
	"in vec2 v_corner;\n"
	"uniform sampler2D u_texture;\n"
	"uniform int u_textured;\n"
	"uniform int u_mode;\n"
	"out vec4 frag;\n"
	"void main ()\n"
	"{\n"
	"  vec4 c = v_color;\n"
	"  if (u_textured != 0)\n"
	"    c *= texture (u_texture, v_uv);\n"
	"  else if (u_mode == 1)\n"
	"    c.a *= (1.0 - v_corner.x * v_corner.x) * clamp (0.15 + 0.85 * (v_corner.y * 0.5 + 0.5), 0.0, 1.0);\n"
	"  else\n"
	"    c.a *= 1.0 - smoothstep (0.35, 1.0, length (v_corner));\n"
	"  if (c.a <= 0.002)\n"
	"    discard;\n"
	"  frag = c;\n"
	"}\n";

/* ==========================================================================
 * Parameters
 * ========================================================================== */

typedef struct
{
	gfloat emission_rate;
	gfloat duration;
	gfloat time_scale;
	gfloat position[3];
	gfloat direction[3];
	gfloat shape_radius;
	gfloat box[3];
	gfloat cone_angle;        /* degrees */
	gfloat speed_min;
	gfloat speed_max;
	gfloat lifetime_min;
	gfloat lifetime_max;
	gfloat size_start;
	gfloat size_end;
	gfloat size_random;
	gfloat rotation_min;      /* degrees */
	gfloat rotation_max;
	gfloat spin_min;          /* degrees per second */
	gfloat spin_max;
	gfloat gravity[3];
	gfloat wind[3];
	gfloat drag;
	gfloat turbulence_strength;
	gfloat turbulence_frequency;
	gfloat turbulence_speed;
	gfloat attractor[3];
	gfloat attractor_strength;
	gfloat attractor_radius;
	gfloat attractor_falloff;
	gfloat vortex_strength;
	gfloat plane_height;
	gfloat bounce;
	gfloat friction;
	gfloat stretch;
} Params;

typedef struct
{
	const gchar *name;
	gsize        offset;
	gfloat       min;
	gfloat       max;
	gfloat       def;
	const gchar *blurb;
	gboolean     table;     /* affects the baked size table */
} FloatParam;

#define P(field) G_STRUCT_OFFSET (Params, field)
#define PA(field, i) (G_STRUCT_OFFSET (Params, field) + (i) * (gsize) sizeof (gfloat))

static const FloatParam float_params[] = {
	{ "emission-rate", P (emission_rate), 0.0f, 10000000.0f, 100.0f, "Particles emitted per second", FALSE },
	{ "duration", P (duration), 0.001f, 100000.0f, 5.0f, "Seconds per emission cycle", FALSE },
	{ "time-scale", P (time_scale), 0.0f, 100.0f, 1.0f, "Multiplier of the update delta", FALSE },
	{ "position-x", PA (position, 0), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Emitter position X", FALSE },
	{ "position-y", PA (position, 1), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Emitter position Y", FALSE },
	{ "position-z", PA (position, 2), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Emitter position Z", FALSE },
	{ "direction-x", PA (direction, 0), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Cone axis X", FALSE },
	{ "direction-y", PA (direction, 1), -G_MAXFLOAT, G_MAXFLOAT, 1.0f, "Cone axis Y", FALSE },
	{ "direction-z", PA (direction, 2), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Cone axis Z", FALSE },
	{ "shape-radius", P (shape_radius), 0.0f, G_MAXFLOAT, 1.0f, "Sphere or cone base radius", FALSE },
	{ "box-x", PA (box, 0), 0.0f, G_MAXFLOAT, 1.0f, "Box shape width", FALSE },
	{ "box-y", PA (box, 1), 0.0f, G_MAXFLOAT, 1.0f, "Box shape height", FALSE },
	{ "box-z", PA (box, 2), 0.0f, G_MAXFLOAT, 1.0f, "Box shape depth", FALSE },
	{ "cone-angle", P (cone_angle), 0.0f, 180.0f, 25.0f, "Half angle of the velocity cone in degrees", FALSE },
	{ "speed-min", P (speed_min), -G_MAXFLOAT, G_MAXFLOAT, 1.0f, "Minimum start speed", FALSE },
	{ "speed-max", P (speed_max), -G_MAXFLOAT, G_MAXFLOAT, 2.0f, "Maximum start speed", FALSE },
	{ "lifetime-min", P (lifetime_min), 0.0f, G_MAXFLOAT, 1.0f, "Minimum lifetime in seconds", FALSE },
	{ "lifetime-max", P (lifetime_max), 0.0f, G_MAXFLOAT, 2.0f, "Maximum lifetime in seconds", FALSE },
	{ "size-start", P (size_start), 0.0f, G_MAXFLOAT, 0.2f, "Size at birth (world units)", TRUE },
	{ "size-end", P (size_end), 0.0f, G_MAXFLOAT, 0.05f, "Size at death (world units)", TRUE },
	{ "size-random", P (size_random), 0.0f, 1.0f, 0.0f, "Random size reduction per particle (0..1)", FALSE },
	{ "rotation-min", P (rotation_min), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Minimum start rotation in degrees", FALSE },
	{ "rotation-max", P (rotation_max), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Maximum start rotation in degrees", FALSE },
	{ "spin-min", P (spin_min), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Minimum spin in degrees per second", FALSE },
	{ "spin-max", P (spin_max), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Maximum spin in degrees per second", FALSE },
	{ "gravity-x", PA (gravity, 0), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Constant acceleration X", FALSE },
	{ "gravity-y", PA (gravity, 1), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Constant acceleration Y", FALSE },
	{ "gravity-z", PA (gravity, 2), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Constant acceleration Z", FALSE },
	{ "wind-x", PA (wind, 0), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Air velocity X (with drag)", FALSE },
	{ "wind-y", PA (wind, 1), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Air velocity Y (with drag)", FALSE },
	{ "wind-z", PA (wind, 2), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Air velocity Z (with drag)", FALSE },
	{ "drag", P (drag), 0.0f, G_MAXFLOAT, 0.0f, "Per-second pull toward the air velocity", FALSE },
	{ "turbulence-strength", P (turbulence_strength), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Turbulence acceleration", FALSE },
	{ "turbulence-frequency", P (turbulence_frequency), 0.0f, G_MAXFLOAT, 0.5f, "Turbulence spatial frequency", FALSE },
	{ "turbulence-speed", P (turbulence_speed), -G_MAXFLOAT, G_MAXFLOAT, 1.0f, "Turbulence change rate", FALSE },
	{ "attractor-x", PA (attractor, 0), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Attractor position X", FALSE },
	{ "attractor-y", PA (attractor, 1), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Attractor position Y", FALSE },
	{ "attractor-z", PA (attractor, 2), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Attractor position Z", FALSE },
	{ "attractor-strength", P (attractor_strength), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Attractor acceleration (negative repels)", FALSE },
	{ "attractor-radius", P (attractor_radius), 0.0f, G_MAXFLOAT, 0.0f, "Attractor reach, 0 for unlimited", FALSE },
	{ "attractor-falloff", P (attractor_falloff), 0.0f, 16.0f, 1.0f, "Exponent of the attractor falloff", FALSE },
	{ "vortex-strength", P (vortex_strength), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Swirl around the emitter's vertical axis", FALSE },
	{ "plane-height", P (plane_height), -G_MAXFLOAT, G_MAXFLOAT, 0.0f, "Height of the collision plane", FALSE },
	{ "bounce", P (bounce), 0.0f, 1.0f, 0.4f, "Restitution at the collision plane", FALSE },
	{ "friction", P (friction), 0.0f, 1.0f, 0.1f, "Horizontal speed lost per bounce", FALSE },
	{ "stretch", P (stretch), 0.0f, G_MAXFLOAT, 0.05f, "Seconds of travel a stretched particle shows", FALSE },
};

#undef P
#undef PA

#define N_FLOAT_PARAMS (G_N_ELEMENTS (float_params))

enum
{
	PROP_0,
	PROP_CAPACITY,
	PROP_SEED,
	PROP_SHAPE,
	PROP_RENDER_MODE,
	PROP_BLEND_MODE,
	PROP_COLLIDE,
	PROP_LOOP,
	PROP_EMITTING,
	PROP_PREFER_GPU,
	PROP_LAYER,
	PROP_START_COLOR,
	PROP_END_COLOR,
	PROP_COLOR_GRADIENT,
	PROP_SIZE_CURVE,
	PROP_FLOAT_BASE
};

static GParamSpec *properties[PROP_FLOAT_BASE + N_FLOAT_PARAMS];

/* the description: every float plus these, in display order */
static const gchar * const parameter_names[] = {
	"seed", "emission-rate", "duration", "loop",
	"position-x", "position-y", "position-z",
	"shape", "shape-radius", "box-x", "box-y", "box-z",
	"direction-x", "direction-y", "direction-z", "cone-angle",
	"speed-min", "speed-max", "lifetime-min", "lifetime-max",
	"start-color", "end-color", "color-gradient",
	"size-start", "size-end", "size-random",
	"rotation-min", "rotation-max", "spin-min", "spin-max",
	"gravity-x", "gravity-y", "gravity-z",
	"wind-x", "wind-y", "wind-z", "drag",
	"turbulence-strength", "turbulence-frequency", "turbulence-speed",
	"attractor-x", "attractor-y", "attractor-z",
	"attractor-strength", "attractor-radius", "attractor-falloff",
	"vortex-strength",
	"collide", "plane-height", "bounce", "friction",
	"render-mode", "blend-mode", "stretch", "layer", "time-scale",
	NULL
};

/* ==========================================================================
 * Instance
 * ========================================================================== */

typedef struct
{
	gfloat time;
	guint  count;
} Burst;

/* Everything one update step hands to the shader (and to the CPU twin) */
typedef struct
{
	gfloat  dt;
	guint32 seed;
	guint32 capacity;
	guint32 emit_start;
	guint32 emit_count;
	guint32 spawn_base;
	gfloat  origin_prev[3];
	gfloat  origin[3];
	gfloat  dir[3];
	gfloat  tan[3];
	gfloat  bit[3];
	gint    shape;
	gfloat  cone_cos;
	gfloat  turb_phase;
} Step;

struct _LrgGpuParticles
{
	GObject parent_instance;

	guint                      capacity;
	guint32                    seed;
	LrgGpuParticlesShape       shape;
	LrgGpuParticlesRenderMode  render_mode;
	LrgParticleBlendMode       blend_mode;
	gboolean                   collide;
	gboolean                   loop;
	gboolean                   emitting;
	gboolean                   prefer_gpu;
	guint                      layer;
	GrlColor                   start_color;
	GrlColor                   end_color;
	LrgGradientStops          *gradient;
	LrgKeyframeCurve          *size_curve;
	gfloat                     size_curve_samples[TABLE];
	Params                     params;
	GArray                    *bursts;      /* Burst */
	GrlTexture                *texture;

	/* clock and emission */
	gdouble                    time;
	gdouble                    emit_accum;
	guint                      pending;
	guint32                    head;
	guint64                    emitted;
	gfloat                     origin_prev[3];
	gboolean                   origin_valid;

	/* baked tables */
	gboolean                   tables_dirty;
	gfloat                     colors[TABLE * 4];
	gfloat                     sizes[TABLE];

	/* backend */
	gboolean                   chosen;
	LrgParticleBackendType     backend;
	gfloat                    *state;        /* CPU state / GPU mirror */
	gboolean                   cpu_uploaded; /* the GPU buffer holds state */

	/* GL */
	gboolean                   render_tried;
	gboolean                   render_ok;
	gboolean                   sim_tried;
	gboolean                   sim_ok;
	guint                      buffers[2];
	guint                      cur;          /* buffer holding the state */
	guint                      quad_vbo;
	guint                      update_vao[2];
	guint                      render_vao[2];
	guint                      update_prog;
	guint                      render_prog;
	gint                       uloc[40];
	gint                       rloc[16];
	guint                      draw_count;
};

static void lrg_gpu_particles_prewarmable_init (LrgPrewarmableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgGpuParticles, lrg_gpu_particles, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PREWARMABLE,
                                                      lrg_gpu_particles_prewarmable_init))

/* update uniform slots */
enum
{
	U_DT, U_SEED, U_CAPACITY, U_EMIT_START, U_EMIT_COUNT, U_SPAWN_BASE,
	U_ORIGIN_PREV, U_ORIGIN, U_DIR, U_TAN, U_BIT, U_SHAPE, U_RADIUS, U_BOX,
	U_CONE_COS, U_SPEED, U_LIFE, U_ROT, U_SPIN, U_GRAVITY, U_WIND, U_DRAG,
	U_TURB, U_ATTR, U_ATTR_SHAPE, U_VORTEX, U_COLLIDE, U_PLANE, U_COUNT
};

static const gchar * const update_uniform_names[U_COUNT] = {
	"u_dt", "u_seed", "u_capacity", "u_emit_start", "u_emit_count", "u_spawn_base",
	"u_origin_prev", "u_origin", "u_dir", "u_tan", "u_bit", "u_shape", "u_radius", "u_box",
	"u_cone_cos", "u_speed", "u_life", "u_rot", "u_spin", "u_gravity", "u_wind", "u_drag",
	"u_turb", "u_attr", "u_attr_shape", "u_vortex", "u_collide", "u_plane"
};

/* render uniform slots */
enum
{
	R_VIEW, R_PROJ, R_COLORS, R_SIZES, R_MODE, R_STRETCH, R_SEED, R_SIZE_RANDOM,
	R_TEXTURE, R_TEXTURED, R_COUNT
};

static const gchar * const render_uniform_names[R_COUNT] = {
	"u_view", "u_proj", "u_colors", "u_sizes", "u_mode", "u_stretch", "u_seed",
	"u_size_random", "u_texture", "u_textured"
};

/* ==========================================================================
 * Random numbers (identical bit for bit to the shader's)
 * ========================================================================== */

static inline guint32
hash32 (guint32 x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

static inline gfloat
rnd (guint32 seed,
     guint32 id,
     guint32 ch)
{
	return (gfloat) (hash32 (seed ^ hash32 (id * 16u + ch)) >> 8) * (1.0f / 16777216.0f);
}

/* ==========================================================================
 * Small vector helpers
 * ========================================================================== */

static void
normalize3 (gfloat *v)
{
	gfloat len = sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);

	if (len < 1e-6f)
	{
		v[0] = 0.0f;
		v[1] = 1.0f;
		v[2] = 0.0f;
		return;
	}
	v[0] /= len;
	v[1] /= len;
	v[2] /= len;
}

static void
cross3 (const gfloat *a,
        const gfloat *b,
        gfloat       *out)
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

/* ==========================================================================
 * The CPU simulation (twin of update_vs)
 * ========================================================================== */

/* cone_dir: a direction inside the velocity cone from two uniforms */
static void
cpu_cone_dir (const Step *s,
              gfloat      u,
              gfloat      v,
              gfloat     *out)
{
	gfloat ct = 1.0f - u * (1.0f - s->cone_cos);
	gfloat st = sqrtf (MAX (0.0f, 1.0f - ct * ct));
	gfloat phi = GPU_TAU * v;
	gfloat c = st * cosf (phi);
	gfloat sn = st * sinf (phi);
	gint   i;

	for (i = 0; i < 3; i++)
		out[i] = s->tan[i] * c + s->bit[i] * sn + s->dir[i] * ct;
}

/* cpu_spawn: writes a newborn particle (spawn index @id, k-th of this
 * step's emission) into @p */
static void
cpu_spawn (LrgGpuParticles *self,
           const Step      *s,
           guint32          k,
           gfloat          *p)
{
	const Params *pr = &self->params;
	guint32       id = s->spawn_base + k;
	gfloat        frac = ((gfloat) k + 0.5f) / (gfloat) s->emit_count;
	gfloat        age = (1.0f - frac) * s->dt;
	gfloat        origin[3];
	gfloat        off[3] = { 0.0f, 0.0f, 0.0f };
	gfloat        vdir[3];
	gfloat        r0 = rnd (s->seed, id, 0);
	gfloat        r1 = rnd (s->seed, id, 1);
	gfloat        r2 = rnd (s->seed, id, 2);
	gfloat        speed, life, rot, spin;
	gint          i;

	for (i = 0; i < 3; i++)
		origin[i] = s->origin_prev[i] + (s->origin[i] - s->origin_prev[i]) * frac;

	if (s->shape == LRG_GPU_PARTICLES_SHAPE_SPHERE)
	{
		gfloat z = 2.0f * r0 - 1.0f;
		gfloat phi = GPU_TAU * r1;
		gfloat sxy = sqrtf (MAX (0.0f, 1.0f - z * z));
		gfloat rad = pr->shape_radius * powf (r2, 1.0f / 3.0f);

		vdir[0] = sxy * cosf (phi);
		vdir[1] = z;
		vdir[2] = sxy * sinf (phi);
		for (i = 0; i < 3; i++)
			off[i] = vdir[i] * rad;
	}
	else
	{
		cpu_cone_dir (s, rnd (s->seed, id, 3), rnd (s->seed, id, 4), vdir);
		if (s->shape == LRG_GPU_PARTICLES_SHAPE_BOX)
		{
			off[0] = (r0 - 0.5f) * pr->box[0];
			off[1] = (r1 - 0.5f) * pr->box[1];
			off[2] = (r2 - 0.5f) * pr->box[2];
		}
		else if (s->shape == LRG_GPU_PARTICLES_SHAPE_CONE)
		{
			gfloat rad = pr->shape_radius * sqrtf (r0);
			gfloat phi = GPU_TAU * r1;

			for (i = 0; i < 3; i++)
				off[i] = (s->tan[i] * cosf (phi) + s->bit[i] * sinf (phi)) * rad;
		}
	}

	speed = pr->speed_min + (pr->speed_max - pr->speed_min) * rnd (s->seed, id, 5);
	life = pr->lifetime_min + (pr->lifetime_max - pr->lifetime_min) * rnd (s->seed, id, 6);
	rot = G_PI / 180.0f * (pr->rotation_min + (pr->rotation_max - pr->rotation_min) * rnd (s->seed, id, 7));
	spin = G_PI / 180.0f * (pr->spin_min + (pr->spin_max - pr->spin_min) * rnd (s->seed, id, 8));

	for (i = 0; i < 3; i++)
	{
		gfloat vel = vdir[i] * speed;

		p[i] = origin[i] + off[i] + vel * age;
		p[4 + i] = vel;
	}
	p[3] = age;
	p[7] = life;
	p[8] = rot + spin * age;
	p[9] = spin;
	p[10] = (gfloat) (id & 65535u);
	p[11] = (gfloat) (id >> 16);
}

/* cpu_integrate: steps one live particle by s->dt */
static void
cpu_integrate (LrgGpuParticles *self,
               const Step      *s,
               gfloat          *p)
{
	const Params *pr = &self->params;
	gfloat        acc[3];
	gfloat        dt = s->dt;
	gint          i;

	for (i = 0; i < 3; i++)
		acc[i] = pr->gravity[i];

	if (pr->turbulence_strength != 0.0f)
	{
		gfloat f = pr->turbulence_frequency;
		gfloat t = s->turb_phase;
		gfloat h = pr->turbulence_strength * 0.5f;

		acc[0] += h * (sinf (f * p[1] + 1.7f * t) + sinf (1.3f * f * p[2] - 0.9f * t));
		acc[1] += h * (sinf (f * p[2] + 1.1f * t) + sinf (1.5f * f * p[0] + 0.7f * t));
		acc[2] += h * (sinf (f * p[0] + 1.3f * t) + sinf (1.1f * f * p[1] - 1.2f * t));
	}

	if (pr->attractor_strength != 0.0f)
	{
		gfloat d[3];
		gfloat dist;

		for (i = 0; i < 3; i++)
			d[i] = pr->attractor[i] - p[i];
		dist = sqrtf (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		if (dist > 0.0001f && (pr->attractor_radius <= 0.0f || dist < pr->attractor_radius))
		{
			gfloat fall = pr->attractor_radius > 0.0f
			              ? powf (1.0f - dist / pr->attractor_radius, pr->attractor_falloff) : 1.0f;

			for (i = 0; i < 3; i++)
				acc[i] += d[i] * (pr->attractor_strength * fall / dist);
		}
	}

	if (pr->vortex_strength != 0.0f)
	{
		gfloat rx = p[0] - s->origin[0];
		gfloat rz = p[2] - s->origin[2];
		gfloat l = MAX (sqrtf (rx * rx + rz * rz), 0.1f);

		acc[0] += -rz * (pr->vortex_strength / l);
		acc[2] += rx * (pr->vortex_strength / l);
	}

	for (i = 0; i < 3; i++)
		p[4 + i] += acc[i] * dt;
	if (pr->drag > 0.0f)
	{
		gfloat k = MIN (pr->drag * dt, 1.0f);

		for (i = 0; i < 3; i++)
			p[4 + i] += (pr->wind[i] - p[4 + i]) * k;
	}
	for (i = 0; i < 3; i++)
		p[i] += p[4 + i] * dt;

	if (self->collide && p[1] < pr->plane_height && p[5] < 0.0f)
	{
		p[1] = pr->plane_height + (pr->plane_height - p[1]) * pr->bounce;
		p[5] = -p[5] * pr->bounce;
		p[4] *= 1.0f - pr->friction;
		p[6] *= 1.0f - pr->friction;
	}

	p[3] += dt;
	p[8] += p[9] * dt;
}

static void
cpu_step (LrgGpuParticles *self,
          const Step      *s)
{
	guint32 slot;

	for (slot = 0; slot < s->capacity; slot++)
	{
		gfloat  *p = self->state + (gsize) slot * FPP;
		guint32  k = (slot + s->capacity - s->emit_start) % s->capacity;

		if (k < s->emit_count)
			cpu_spawn (self, s, k, p);
		else if (p[3] < p[7])
			cpu_integrate (self, s, p);
	}
}

/* ==========================================================================
 * Baked tables
 * ========================================================================== */

static void
rebuild_tables (LrgGpuParticles *self)
{
	guint i;

	if (!self->tables_dirty)
		return;
	for (i = 0; i < TABLE; i++)
	{
		gfloat   t = (gfloat) i / (gfloat) (TABLE - 1);
		GrlColor c;

		if (self->gradient != NULL && lrg_gradient_stops_get_n_stops (self->gradient) > 0)
			lrg_gradient_stops_evaluate (self->gradient, t, &c);
		else
		{
			c.r = (guint8) lrintf ((gfloat) self->start_color.r + ((gfloat) self->end_color.r - (gfloat) self->start_color.r) * t);
			c.g = (guint8) lrintf ((gfloat) self->start_color.g + ((gfloat) self->end_color.g - (gfloat) self->start_color.g) * t);
			c.b = (guint8) lrintf ((gfloat) self->start_color.b + ((gfloat) self->end_color.b - (gfloat) self->start_color.b) * t);
			c.a = (guint8) lrintf ((gfloat) self->start_color.a + ((gfloat) self->end_color.a - (gfloat) self->start_color.a) * t);
		}
		self->colors[i * 4 + 0] = (gfloat) c.r / 255.0f;
		self->colors[i * 4 + 1] = (gfloat) c.g / 255.0f;
		self->colors[i * 4 + 2] = (gfloat) c.b / 255.0f;
		self->colors[i * 4 + 3] = (gfloat) c.a / 255.0f;

		if (self->size_curve != NULL)
			self->sizes[i] = self->params.size_start * self->size_curve_samples[i];
		else
			self->sizes[i] = self->params.size_start + (self->params.size_end - self->params.size_start) * t;
	}
	self->tables_dirty = FALSE;
}

/* table_lookup: the render shader's blend between neighbouring samples */
static void
table_position (gfloat  t,
                gint   *index,
                gfloat *frac)
{
	gfloat x = CLAMP (t, 0.0f, 1.0f) * (gfloat) (TABLE - 1);
	gint   i = MIN ((gint) x, TABLE - 2);

	*index = i;
	*frac = x - (gfloat) i;
}

/* ==========================================================================
 * GL resources
 * ========================================================================== */

static guint
compile_stage (guint         type,
               const gchar  *source,
               GError      **error)
{
	guint shader = gl.create_shader (type);
	gint  ok = 0;

	gl.shader_source (shader, 1, &source, NULL);
	gl.compile_shader (shader);
	gl.get_shaderiv (shader, GL_COMPILE_STATUS_, &ok);
	if (!ok)
	{
		gint              len = 0;
		g_autofree gchar *log = NULL;

		gl.get_shaderiv (shader, GL_INFO_LOG_LENGTH_, &len);
		log = g_malloc0 ((gsize) MAX (len, 1) + 1);
		gl.get_shader_info_log (shader, MAX (len, 1), NULL, log);
		g_set_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_SHADER,
		             "%s shader: %s", type == GL_VERTEX_SHADER_ ? "vertex" : "fragment", log);
		gl.delete_shader (shader);
		return 0;
	}
	return shader;
}

/* build_program: compiles and links; @varyings (nullable) are captured by
 * transform feedback, interleaved */
static guint
build_program (const gchar         *vs_src,
               const gchar         *fs_src,
               const gchar * const *varyings,
               gint                 n_varyings,
               GError             **error)
{
	guint vs, fs, prog;
	gint  ok = 0;

	vs = compile_stage (GL_VERTEX_SHADER_, vs_src, error);
	if (vs == 0)
		return 0;
	fs = compile_stage (GL_FRAGMENT_SHADER_, fs_src, error);
	if (fs == 0)
	{
		gl.delete_shader (vs);
		return 0;
	}
	prog = gl.create_program ();
	gl.attach_shader (prog, vs);
	gl.attach_shader (prog, fs);
	if (varyings != NULL)
		gl.transform_feedback_varyings (prog, n_varyings, varyings, GL_INTERLEAVED_ATTRIBS_);
	gl.link_program (prog);
	gl.delete_shader (vs);
	gl.delete_shader (fs);
	gl.get_programiv (prog, GL_LINK_STATUS_, &ok);
	if (!ok)
	{
		gint              len = 0;
		g_autofree gchar *log = NULL;

		gl.get_programiv (prog, GL_INFO_LOG_LENGTH_, &len);
		log = g_malloc0 ((gsize) MAX (len, 1) + 1);
		gl.get_program_info_log (prog, MAX (len, 1), NULL, log);
		g_set_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_SHADER,
		             "link: %s", log);
		gl.delete_program (prog);
		return 0;
	}
	return prog;
}

/* bind_state_attribs: the three vec4 particle attributes from @buffer
 * starting at @first_location, with @divisor */
static void
bind_state_attribs (guint buffer,
                    guint first_location,
                    guint divisor)
{
	guint i;

	gl.bind_buffer (GL_ARRAY_BUFFER_, buffer);
	for (i = 0; i < 3; i++)
	{
		gl.enable_vertex_attrib_array (first_location + i);
		gl.vertex_attrib_pointer (first_location + i, 4, GL_FLOAT_, 0,
		                          (gint) (FPP * sizeof (gfloat)),
		                          (const void *) (gsize) (i * 4 * sizeof (gfloat)));
		gl.vertex_attrib_divisor (first_location + i, divisor);
	}
}

/* ensure_render: the state buffers, the corner quad and the render
 * program (needed by both backends to draw) */
static gboolean
ensure_render (LrgGpuParticles  *self,
               GError          **error)
{
	static const gfloat corners[8] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
	guint               i;

	if (self->render_tried)
	{
		if (!self->render_ok)
			g_set_error_literal (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_NO_GPU,
			                     "the particle render path failed earlier");
		return self->render_ok;
	}
	if (!gpu_gl_load ())
	{
		g_set_error_literal (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_NO_GPU,
		                     IsWindowReady () ? "the driver lacks GL 3.3 buffer/instancing entry points"
		                                      : "no window: GPU particles need a GL context");
		return FALSE;
	}
	self->render_tried = TRUE;

	self->render_prog = build_program (render_vs, render_fs, NULL, 0, error);
	if (self->render_prog == 0)
		return FALSE;
	for (i = 0; i < R_COUNT; i++)
		self->rloc[i] = gl.get_uniform_location (self->render_prog, render_uniform_names[i]);

	/* state buffers: zeroes are dead particles (age 0, lifetime 0) */
	rlDrawRenderBatchActive ();
	gl.gen_buffers (2, self->buffers);
	for (i = 0; i < 2; i++)
	{
		gl.bind_buffer (GL_ARRAY_BUFFER_, self->buffers[i]);
		gl.buffer_data (GL_ARRAY_BUFFER_, (gssize) ((gsize) self->capacity * FPP * sizeof (gfloat)),
		                self->state, GL_DYNAMIC_COPY_);
	}
	gl.gen_buffers (1, &self->quad_vbo);
	gl.bind_buffer (GL_ARRAY_BUFFER_, self->quad_vbo);
	gl.buffer_data (GL_ARRAY_BUFFER_, (gssize) sizeof corners, corners, GL_STATIC_DRAW_);

	/* one render VAO per state buffer: corner per vertex, state per instance */
	gl.gen_vertex_arrays (2, self->render_vao);
	for (i = 0; i < 2; i++)
	{
		gl.bind_vertex_array (self->render_vao[i]);
		gl.bind_buffer (GL_ARRAY_BUFFER_, self->quad_vbo);
		gl.enable_vertex_attrib_array (0);
		gl.vertex_attrib_pointer (0, 2, GL_FLOAT_, 0, (gint) (2 * sizeof (gfloat)), NULL);
		gl.vertex_attrib_divisor (0, 0);
		bind_state_attribs (self->buffers[i], 1, 1);
	}
	gl.bind_vertex_array (0);
	gl.bind_buffer (GL_ARRAY_BUFFER_, 0);
	self->cur = 0;
	self->cpu_uploaded = TRUE;
	self->render_ok = TRUE;
	return TRUE;
}

/* ensure_sim: the transform-feedback program and its VAOs */
static gboolean
ensure_sim (LrgGpuParticles  *self,
            GError          **error)
{
	static const gchar * const varyings[3] = { "o_pos_age", "o_vel_life", "o_misc" };
	guint                      i;

	if (self->sim_tried)
	{
		if (!self->sim_ok)
			g_set_error_literal (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_NO_GPU,
			                     "the particle simulation path failed earlier");
		return self->sim_ok;
	}
	if (!ensure_render (self, error))
		return FALSE;
	self->sim_tried = TRUE;
	if (!gl.tf_ok)
	{
		g_set_error_literal (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_NO_GPU,
		                     "the driver lacks transform feedback");
		return FALSE;
	}
	self->update_prog = build_program (update_vs, update_fs, varyings, 3, error);
	if (self->update_prog == 0)
		return FALSE;
	for (i = 0; i < U_COUNT; i++)
		self->uloc[i] = gl.get_uniform_location (self->update_prog, update_uniform_names[i]);

	gl.gen_vertex_arrays (2, self->update_vao);
	for (i = 0; i < 2; i++)
	{
		gl.bind_vertex_array (self->update_vao[i]);
		bind_state_attribs (self->buffers[i], 0, 0);
	}
	gl.bind_vertex_array (0);
	gl.bind_buffer (GL_ARRAY_BUFFER_, 0);
	self->sim_ok = TRUE;
	return TRUE;
}

static void
release_gl (LrgGpuParticles *self)
{
	if (!IsWindowReady () || !gl.base_ok)
		return;
	if (self->update_prog != 0)
		gl.delete_program (self->update_prog);
	if (self->render_prog != 0)
		gl.delete_program (self->render_prog);
	if (self->update_vao[0] != 0)
		gl.delete_vertex_arrays (2, self->update_vao);
	if (self->render_vao[0] != 0)
		gl.delete_vertex_arrays (2, self->render_vao);
	if (self->buffers[0] != 0)
		gl.delete_buffers (2, self->buffers);
	if (self->quad_vbo != 0)
		gl.delete_buffers (1, &self->quad_vbo);
}

/* sync_from_gpu: reads the GPU state into self->state */
static void
sync_from_gpu (LrgGpuParticles *self)
{
	if (self->backend != LRG_PARTICLE_BACKEND_GPU || !self->render_ok)
		return;
	rlDrawRenderBatchActive ();
	gl.bind_buffer (GL_ARRAY_BUFFER_, self->buffers[self->cur]);
	gl.get_buffer_sub_data (GL_ARRAY_BUFFER_, 0,
	                        (gssize) ((gsize) self->capacity * FPP * sizeof (gfloat)), self->state);
	gl.bind_buffer (GL_ARRAY_BUFFER_, 0);
}

/* upload_state: copies self->state into the current GPU buffer */
static void
upload_state (LrgGpuParticles *self)
{
	if (!self->render_ok)
		return;
	rlDrawRenderBatchActive ();
	gl.bind_buffer (GL_ARRAY_BUFFER_, self->buffers[self->cur]);
	gl.buffer_sub_data (GL_ARRAY_BUFFER_, 0,
	                    (gssize) ((gsize) self->capacity * FPP * sizeof (gfloat)), self->state);
	gl.bind_buffer (GL_ARRAY_BUFFER_, 0);
	self->cpu_uploaded = TRUE;
}

/* choose_backend: GPU when wanted and possible, else CPU */
static void
choose_backend (LrgGpuParticles  *self,
                GError          **error)
{
	if (self->chosen)
		return;
	self->chosen = TRUE;
	self->backend = LRG_PARTICLE_BACKEND_CPU;
	if (!self->prefer_gpu)
		return;
	if (ensure_sim (self, error))
	{
		/* the GPU buffer takes over the (possibly non-empty) CPU state */
		upload_state (self);
		self->backend = LRG_PARTICLE_BACKEND_GPU;
	}
}

/* ==========================================================================
 * Update
 * ========================================================================== */

/* gpu_step: one transform-feedback pass from buffers[cur] into the other */
static void
gpu_step (LrgGpuParticles *self,
          const Step      *s)
{
	const Params *pr = &self->params;
	const gint   *u = self->uloc;
	guint         dst = 1 - self->cur;

	rlDrawRenderBatchActive ();
	gl.use_program (self->update_prog);
	gl.uniform1f (u[U_DT], s->dt);
	gl.uniform1ui (u[U_SEED], s->seed);
	gl.uniform1ui (u[U_CAPACITY], s->capacity);
	gl.uniform1ui (u[U_EMIT_START], s->emit_start);
	gl.uniform1ui (u[U_EMIT_COUNT], s->emit_count);
	gl.uniform1ui (u[U_SPAWN_BASE], s->spawn_base);
	gl.uniform3f (u[U_ORIGIN_PREV], s->origin_prev[0], s->origin_prev[1], s->origin_prev[2]);
	gl.uniform3f (u[U_ORIGIN], s->origin[0], s->origin[1], s->origin[2]);
	gl.uniform3f (u[U_DIR], s->dir[0], s->dir[1], s->dir[2]);
	gl.uniform3f (u[U_TAN], s->tan[0], s->tan[1], s->tan[2]);
	gl.uniform3f (u[U_BIT], s->bit[0], s->bit[1], s->bit[2]);
	gl.uniform1i (u[U_SHAPE], s->shape);
	gl.uniform1f (u[U_RADIUS], pr->shape_radius);
	gl.uniform3f (u[U_BOX], pr->box[0], pr->box[1], pr->box[2]);
	gl.uniform1f (u[U_CONE_COS], s->cone_cos);
	gl.uniform2f (u[U_SPEED], pr->speed_min, pr->speed_max);
	gl.uniform2f (u[U_LIFE], pr->lifetime_min, pr->lifetime_max);
	gl.uniform2f (u[U_ROT], G_PI / 180.0f * pr->rotation_min, G_PI / 180.0f * pr->rotation_max);
	gl.uniform2f (u[U_SPIN], G_PI / 180.0f * pr->spin_min, G_PI / 180.0f * pr->spin_max);
	gl.uniform3f (u[U_GRAVITY], pr->gravity[0], pr->gravity[1], pr->gravity[2]);
	gl.uniform3f (u[U_WIND], pr->wind[0], pr->wind[1], pr->wind[2]);
	gl.uniform1f (u[U_DRAG], pr->drag);
	gl.uniform3f (u[U_TURB], pr->turbulence_strength, pr->turbulence_frequency, s->turb_phase);
	gl.uniform4f (u[U_ATTR], pr->attractor[0], pr->attractor[1], pr->attractor[2],
	              pr->attractor_strength);
	gl.uniform2f (u[U_ATTR_SHAPE], pr->attractor_radius, pr->attractor_falloff);
	gl.uniform1f (u[U_VORTEX], pr->vortex_strength);
	gl.uniform1i (u[U_COLLIDE], self->collide ? 1 : 0);
	gl.uniform3f (u[U_PLANE], pr->plane_height, pr->bounce, pr->friction);

	gl.bind_vertex_array (self->update_vao[self->cur]);
	gl.bind_buffer_base (GL_TRANSFORM_FEEDBACK_BUFFER_, 0, self->buffers[dst]);
	gl.enable (GL_RASTERIZER_DISCARD_);
	gl.begin_transform_feedback (GL_POINTS_);
	gl.draw_arrays (GL_POINTS_, 0, (gint) self->capacity);
	gl.end_transform_feedback ();
	gl.disable (GL_RASTERIZER_DISCARD_);
	gl.bind_buffer_base (GL_TRANSFORM_FEEDBACK_BUFFER_, 0, 0);
	gl.bind_vertex_array (0);
	gl.use_program (0);
	self->cur = dst;
}

/* bursts_between: particles the timed bursts emit for clock times in
 * [t0, t1) */
static guint64
bursts_between (LrgGpuParticles *self,
                gdouble          t0,
                gdouble          t1)
{
	gdouble  d = (gdouble) self->params.duration;
	guint64  total = 0;
	guint    i;

	for (i = 0; i < self->bursts->len; i++)
	{
		const Burst *b = &g_array_index (self->bursts, Burst, i);
		gdouble      bt = (gdouble) b->time;

		if (!self->loop)
		{
			if (bt >= t0 && bt < t1)
				total += b->count;
			continue;
		}
		if (bt >= d)
			continue;
		{
			/* every repetition bt + n * d inside [t0, t1) */
			gdouble n0 = ceil ((t0 - bt) / d);
			gdouble n;

			if (n0 < 0.0)
				n0 = 0.0;
			for (n = n0; bt + n * d < t1; n += 1.0)
				if (bt + n * d >= t0)
					total += b->count;
		}
	}
	return total;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static gfloat *
float_field (LrgGpuParticles *self,
             guint            index)
{
	return (gfloat *) ((guint8 *) &self->params + float_params[index].offset);
}

/* set_float: stores a description float with notify */
static void
set_float (LrgGpuParticles *self,
           guint            index,
           gfloat           value)
{
	gfloat *field = float_field (self, index);

	value = CLAMP (value, float_params[index].min, float_params[index].max);
	if (*field == value)
		return;
	*field = value;
	if (float_params[index].table)
		self->tables_dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLOAT_BASE + index]);
}

static gint
float_index (const gchar *name)
{
	guint i;

	for (i = 0; i < N_FLOAT_PARAMS; i++)
		if (g_strcmp0 (float_params[i].name, name) == 0)
			return (gint) i;
	return -1;
}

static void
set_float_by_name (LrgGpuParticles *self,
                   const gchar     *name,
                   gfloat           value)
{
	gint i = float_index (name);

	g_assert (i >= 0);
	set_float (self, (guint) i, value);
}

static void
lrg_gpu_particles_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgGpuParticles *self = LRG_GPU_PARTICLES (object);

	if (prop_id >= PROP_FLOAT_BASE && prop_id < PROP_FLOAT_BASE + N_FLOAT_PARAMS)
	{
		g_value_set_float (value, *float_field (self, prop_id - PROP_FLOAT_BASE));
		return;
	}
	switch (prop_id)
	{
	case PROP_CAPACITY:
		g_value_set_uint (value, self->capacity);
		break;
	case PROP_SEED:
		g_value_set_uint (value, self->seed);
		break;
	case PROP_SHAPE:
		g_value_set_enum (value, (gint) self->shape);
		break;
	case PROP_RENDER_MODE:
		g_value_set_enum (value, (gint) self->render_mode);
		break;
	case PROP_BLEND_MODE:
		g_value_set_enum (value, (gint) self->blend_mode);
		break;
	case PROP_COLLIDE:
		g_value_set_boolean (value, self->collide);
		break;
	case PROP_LOOP:
		g_value_set_boolean (value, self->loop);
		break;
	case PROP_EMITTING:
		g_value_set_boolean (value, self->emitting);
		break;
	case PROP_PREFER_GPU:
		g_value_set_boolean (value, self->prefer_gpu);
		break;
	case PROP_LAYER:
		g_value_set_uint (value, self->layer);
		break;
	case PROP_START_COLOR:
		g_value_set_boxed (value, &self->start_color);
		break;
	case PROP_END_COLOR:
		g_value_set_boxed (value, &self->end_color);
		break;
	case PROP_COLOR_GRADIENT:
		g_value_set_boxed (value, self->gradient);
		break;
	case PROP_SIZE_CURVE:
		g_value_set_object (value, self->size_curve);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

/* set_bool_prop: stores a boolean field with notify */
static void
set_bool_prop (LrgGpuParticles *self,
               gboolean        *field,
               gboolean         value,
               guint            prop)
{
	value = !!value;
	if (*field == value)
		return;
	*field = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
set_color_prop (LrgGpuParticles *self,
                GrlColor        *field,
                const GrlColor  *value,
                guint            prop)
{
	GrlColor c = { 255, 255, 255, 255 };

	if (value != NULL)
		c = *value;
	if (field->r == c.r && field->g == c.g && field->b == c.b && field->a == c.a)
		return;
	*field = c;
	self->tables_dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_gpu_particles_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgGpuParticles *self = LRG_GPU_PARTICLES (object);

	if (prop_id >= PROP_FLOAT_BASE && prop_id < PROP_FLOAT_BASE + N_FLOAT_PARAMS)
	{
		set_float (self, prop_id - PROP_FLOAT_BASE, g_value_get_float (value));
		return;
	}
	switch (prop_id)
	{
	case PROP_CAPACITY:
		self->capacity = CLAMP (g_value_get_uint (value), 1u, MAX_CAPACITY);
		break;
	case PROP_SEED:
		if (self->seed != g_value_get_uint (value))
		{
			self->seed = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_SHAPE:
		if ((gint) self->shape != g_value_get_enum (value))
		{
			self->shape = (LrgGpuParticlesShape) g_value_get_enum (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_RENDER_MODE:
		if ((gint) self->render_mode != g_value_get_enum (value))
		{
			self->render_mode = (LrgGpuParticlesRenderMode) g_value_get_enum (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_BLEND_MODE:
		if ((gint) self->blend_mode != g_value_get_enum (value))
		{
			self->blend_mode = (LrgParticleBlendMode) g_value_get_enum (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_COLLIDE:
		set_bool_prop (self, &self->collide, g_value_get_boolean (value), PROP_COLLIDE);
		break;
	case PROP_LOOP:
		set_bool_prop (self, &self->loop, g_value_get_boolean (value), PROP_LOOP);
		break;
	case PROP_EMITTING:
		set_bool_prop (self, &self->emitting, g_value_get_boolean (value), PROP_EMITTING);
		break;
	case PROP_PREFER_GPU:
		lrg_gpu_particles_set_prefer_gpu (self, g_value_get_boolean (value));
		break;
	case PROP_LAYER:
		if (self->layer != g_value_get_uint (value))
		{
			self->layer = g_value_get_uint (value);
			g_object_notify_by_pspec (object, pspec);
		}
		break;
	case PROP_START_COLOR:
		set_color_prop (self, &self->start_color, g_value_get_boxed (value), PROP_START_COLOR);
		break;
	case PROP_END_COLOR:
		set_color_prop (self, &self->end_color, g_value_get_boxed (value), PROP_END_COLOR);
		break;
	case PROP_COLOR_GRADIENT:
		lrg_gpu_particles_set_color_gradient (self, g_value_get_boxed (value));
		break;
	case PROP_SIZE_CURVE:
		lrg_gpu_particles_set_size_curve (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_gpu_particles_constructed (GObject *object)
{
	LrgGpuParticles *self = LRG_GPU_PARTICLES (object);

	G_OBJECT_CLASS (lrg_gpu_particles_parent_class)->constructed (object);
	self->state = g_new0 (gfloat, (gsize) self->capacity * FPP);
}

static void
lrg_gpu_particles_finalize (GObject *object)
{
	LrgGpuParticles *self = LRG_GPU_PARTICLES (object);

	release_gl (self);
	g_free (self->state);
	g_array_unref (self->bursts);
	g_clear_pointer (&self->gradient, lrg_gradient_stops_free);
	g_clear_object (&self->size_curve);
	g_clear_object (&self->texture);

	G_OBJECT_CLASS (lrg_gpu_particles_parent_class)->finalize (object);
}

static void
lrg_gpu_particles_class_init (LrgGpuParticlesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	guint         i;

	object_class->get_property = lrg_gpu_particles_get_property;
	object_class->set_property = lrg_gpu_particles_set_property;
	object_class->constructed = lrg_gpu_particles_constructed;
	object_class->finalize = lrg_gpu_particles_finalize;

	/**
	 * LrgGpuParticles:capacity:
	 *
	 * The number of particle slots (construct only).
	 */
	properties[PROP_CAPACITY] =
		g_param_spec_uint ("capacity", "Capacity", "Number of particle slots", 1, MAX_CAPACITY,
		                   65536, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:seed:
	 *
	 * The seed of every random number (with the spawn index). The same
	 * seed, parameters and update steps give the same particles.
	 */
	properties[PROP_SEED] =
		g_param_spec_uint ("seed", "Seed", "Random seed", 0, G_MAXUINT32, 1,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:shape:
	 *
	 * Where new particles start (#LrgGpuParticlesShape).
	 */
	properties[PROP_SHAPE] =
		g_param_spec_enum ("shape", "Shape", "Emission shape", LRG_TYPE_GPU_PARTICLES_SHAPE,
		                   LRG_GPU_PARTICLES_SHAPE_POINT,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:render-mode:
	 *
	 * How particles are drawn (#LrgGpuParticlesRenderMode).
	 */
	properties[PROP_RENDER_MODE] =
		g_param_spec_enum ("render-mode", "Render mode", "Billboards or velocity streaks",
		                   LRG_TYPE_GPU_PARTICLES_RENDER_MODE, LRG_GPU_PARTICLES_RENDER_BILLBOARD,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:blend-mode:
	 *
	 * The blend function. Particles are not sorted: additive blending is
	 * order independent, alpha blending of overlapping particles is not.
	 */
	properties[PROP_BLEND_MODE] =
		g_param_spec_enum ("blend-mode", "Blend mode", "Blend function", LRG_TYPE_PARTICLE_BLEND_MODE,
		                   LRG_PARTICLE_BLEND_ADDITIVE,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:collide:
	 *
	 * Whether particles bounce off the horizontal plane at
	 * #LrgGpuParticles:plane-height.
	 */
	properties[PROP_COLLIDE] =
		g_param_spec_boolean ("collide", "Collide", "Bounce off the ground plane", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:loop:
	 *
	 * Whether emission repeats every #LrgGpuParticles:duration seconds
	 * (otherwise the rate and the bursts stop after one cycle).
	 */
	properties[PROP_LOOP] =
		g_param_spec_boolean ("loop", "Loop", "Repeat the emission cycle", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:emitting:
	 *
	 * Whether the rate and the timed bursts emit. Live particles keep
	 * moving either way; lrg_gpu_particles_emit() always works.
	 */
	properties[PROP_EMITTING] =
		g_param_spec_boolean ("emitting", "Emitting", "Rate and bursts emit", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:prefer-gpu:
	 *
	 * Whether to simulate on the GPU when it is available (otherwise, and
	 * as the fallback, the CPU runs the same simulation).
	 */
	properties[PROP_PREFER_GPU] =
		g_param_spec_boolean ("prefer-gpu", "Prefer GPU", "Simulate on the GPU when possible", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:layer:
	 *
	 * The render layer (0 .. 31); lrg_gpu_particles_draw() skips the
	 * emitter when the layer is outside lrg_render_layers_get_active_mask().
	 */
	properties[PROP_LAYER] =
		g_param_spec_uint ("layer", "Layer", "Render layer", 0, LRG_RENDER_LAYERS_COUNT - 1, 0,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:start-color:
	 *
	 * The colour at birth when no #LrgGpuParticles:color-gradient is set.
	 */
	properties[PROP_START_COLOR] =
		g_param_spec_boxed ("start-color", "Start color", "Colour at birth", GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:end-color:
	 *
	 * The colour at death when no #LrgGpuParticles:color-gradient is set.
	 */
	properties[PROP_END_COLOR] =
		g_param_spec_boxed ("end-color", "End color", "Colour at death", GRL_TYPE_COLOR,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:color-gradient:
	 *
	 * Colour over life (nullable; overrides the start and end colours).
	 */
	properties[PROP_COLOR_GRADIENT] =
		g_param_spec_boxed ("color-gradient", "Color gradient", "Colour over life",
		                    LRG_TYPE_GRADIENT_STOPS,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgGpuParticles:size-curve:
	 *
	 * Multiplier of #LrgGpuParticles:size-start over life (nullable; see
	 * lrg_gpu_particles_set_size_curve()).
	 */
	properties[PROP_SIZE_CURVE] =
		g_param_spec_object ("size-curve", "Size curve", "Size multiplier over life",
		                     LRG_TYPE_KEYFRAME_CURVE,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/* The float description parameters (see float_params[] and
	 * docs/modules/particles/gpu-particles.org for each one's meaning):
	 * LrgGpuParticles:emission-rate, :duration, :time-scale,
	 * :position-x/y/z, :direction-x/y/z, :shape-radius, :box-x/y/z,
	 * :cone-angle, :speed-min/max, :lifetime-min/max, :size-start,
	 * :size-end, :size-random, :rotation-min/max, :spin-min/max,
	 * :gravity-x/y/z, :wind-x/y/z, :drag, :turbulence-strength,
	 * :turbulence-frequency, :turbulence-speed, :attractor-x/y/z,
	 * :attractor-strength, :attractor-radius, :attractor-falloff,
	 * :vortex-strength, :plane-height, :bounce, :friction, :stretch. */
	for (i = 0; i < N_FLOAT_PARAMS; i++)
	{
		const FloatParam *f = &float_params[i];

		properties[PROP_FLOAT_BASE + i] =
			g_param_spec_float (f->name, f->name, f->blurb, f->min, f->max, f->def,
			                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	}

	g_object_class_install_properties (object_class, PROP_FLOAT_BASE + N_FLOAT_PARAMS, properties);
}

static void
lrg_gpu_particles_init (LrgGpuParticles *self)
{
	guint i;

	self->capacity = 65536;
	self->seed = 1;
	self->shape = LRG_GPU_PARTICLES_SHAPE_POINT;
	self->render_mode = LRG_GPU_PARTICLES_RENDER_BILLBOARD;
	self->blend_mode = LRG_PARTICLE_BLEND_ADDITIVE;
	self->loop = TRUE;
	self->emitting = TRUE;
	self->prefer_gpu = TRUE;
	self->start_color = (GrlColor) { 255, 255, 255, 255 };
	self->end_color = (GrlColor) { 255, 255, 255, 0 };
	for (i = 0; i < N_FLOAT_PARAMS; i++)
		*float_field (self, i) = float_params[i].def;
	self->bursts = g_array_new (FALSE, FALSE, sizeof (Burst));
	self->tables_dirty = TRUE;
	self->backend = LRG_PARTICLE_BACKEND_CPU;

	/* compile and allocate before the first frame where a loading
	 * screen drains the prewarm queue */
	lrg_prewarm_track (LRG_PREWARMABLE (self));
}

/* ==========================================================================
 * Prewarm
 * ========================================================================== */

static gboolean
lrg_gpu_particles_prewarm (LrgPrewarmable *prewarmable)
{
	LrgGpuParticles *self = LRG_GPU_PARTICLES (prewarmable);

	if (!IsWindowReady ())
		return FALSE;
	choose_backend (self, NULL);
	return TRUE;
}

static void
lrg_gpu_particles_prewarmable_init (LrgPrewarmableInterface *iface)
{
	iface->prewarm = lrg_gpu_particles_prewarm;
}

/* ==========================================================================
 * Public API: construction and backend
 * ========================================================================== */

LrgGpuParticles *
lrg_gpu_particles_new (guint capacity)
{
	return g_object_new (LRG_TYPE_GPU_PARTICLES, "capacity", CLAMP (capacity, 1u, MAX_CAPACITY), NULL);
}

gboolean
lrg_gpu_particles_is_gpu_available (void)
{
	return gpu_gl_load () && gl.tf_ok;
}

gboolean
lrg_gpu_particles_prepare (LrgGpuParticles  *self,
                           GError          **error)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), FALSE);

	choose_backend (self, error);
	return TRUE;
}

LrgParticleBackendType
lrg_gpu_particles_get_backend (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), LRG_PARTICLE_BACKEND_CPU);
	return self->backend;
}

gboolean
lrg_gpu_particles_get_prefer_gpu (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), FALSE);
	return self->prefer_gpu;
}

void
lrg_gpu_particles_set_prefer_gpu (LrgGpuParticles *self,
                                  gboolean         prefer_gpu)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));

	prefer_gpu = !!prefer_gpu;
	if (self->prefer_gpu == prefer_gpu)
		return;
	self->prefer_gpu = prefer_gpu;

	/* migrate live particles to the new backend */
	if (self->chosen)
	{
		if (!prefer_gpu && self->backend == LRG_PARTICLE_BACKEND_GPU)
		{
			sync_from_gpu (self);
			self->backend = LRG_PARTICLE_BACKEND_CPU;
			self->cpu_uploaded = FALSE;
		}
		else if (prefer_gpu && self->backend == LRG_PARTICLE_BACKEND_CPU)
			self->chosen = FALSE;   /* the next update tries the GPU */
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREFER_GPU]);
}

guint
lrg_gpu_particles_get_capacity (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), 0);
	return self->capacity;
}

/* ==========================================================================
 * Public API: simulation
 * ========================================================================== */

void
lrg_gpu_particles_update (LrgGpuParticles *self,
                          gfloat           delta_time)
{
	const Params *pr;
	Step          s;
	gdouble       t0, t1;
	guint64       n = 0;
	gfloat        up[3] = { 0.0f, 1.0f, 0.0f };

	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));

	pr = &self->params;
	choose_backend (self, NULL);

	if (!(delta_time > 0.0f))
		delta_time = 0.0f;
	delta_time *= pr->time_scale;

	/* emission: rate, timed bursts, queued lrg_gpu_particles_emit() */
	t0 = self->time;
	t1 = t0 + (gdouble) delta_time;
	if (self->emitting)
	{
		gdouble rate_dt = (gdouble) delta_time;

		if (!self->loop)
			rate_dt = MAX (0.0, MIN (t1, (gdouble) pr->duration) - t0);
		self->emit_accum += (gdouble) pr->emission_rate * rate_dt;
		n = (guint64) floor (self->emit_accum);
		self->emit_accum -= (gdouble) n;
		n += bursts_between (self, t0, t1);
	}
	n += self->pending;
	self->pending = 0;
	n = MIN (n, (guint64) self->capacity);

	/* the step */
	memset (&s, 0, sizeof s);
	s.dt = delta_time;
	s.seed = self->seed;
	s.capacity = self->capacity;
	s.emit_start = self->head;
	s.emit_count = (guint32) n;
	s.spawn_base = (guint32) (self->emitted & G_MAXUINT32);
	memcpy (s.origin, pr->position, sizeof s.origin);
	if (self->origin_valid)
		memcpy (s.origin_prev, self->origin_prev, sizeof s.origin_prev);
	else
		memcpy (s.origin_prev, pr->position, sizeof s.origin_prev);
	memcpy (s.dir, pr->direction, sizeof s.dir);
	normalize3 (s.dir);
	if (fabsf (s.dir[1]) > 0.99f)
	{
		up[0] = 1.0f;
		up[1] = 0.0f;
	}
	cross3 (up, s.dir, s.tan);
	normalize3 (s.tan);
	cross3 (s.dir, s.tan, s.bit);
	s.shape = (gint) self->shape;
	s.cone_cos = cosf (pr->cone_angle * (G_PI / 180.0f));
	s.turb_phase = (gfloat) fmod (t0 * (gdouble) pr->turbulence_speed, 3600.0);

	if (self->backend == LRG_PARTICLE_BACKEND_GPU)
		gpu_step (self, &s);
	else
	{
		cpu_step (self, &s);
		self->cpu_uploaded = FALSE;
	}

	self->head = (guint32) (((guint64) self->head + n) % self->capacity);
	self->emitted += n;
	self->time = t1;
	memcpy (self->origin_prev, pr->position, sizeof self->origin_prev);
	self->origin_valid = TRUE;
}

void
lrg_gpu_particles_emit (LrgGpuParticles *self,
                        guint            count)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	self->pending = (guint) MIN ((guint64) self->pending + count, (guint64) G_MAXUINT);
}

void
lrg_gpu_particles_clear (LrgGpuParticles *self)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));

	memset (self->state, 0, (gsize) self->capacity * FPP * sizeof (gfloat));
	if (self->backend == LRG_PARTICLE_BACKEND_GPU)
		upload_state (self);
	else
		self->cpu_uploaded = FALSE;
	self->time = 0.0;
	self->emit_accum = 0.0;
	self->pending = 0;
	self->head = 0;
	self->emitted = 0;
	self->origin_valid = FALSE;
}

void
lrg_gpu_particles_add_burst (LrgGpuParticles *self,
                             gfloat           time,
                             guint            count)
{
	Burst b;

	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	b.time = MAX (time, 0.0f);
	b.count = count;
	g_array_append_val (self->bursts, b);
}

void
lrg_gpu_particles_clear_bursts (LrgGpuParticles *self)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	g_array_set_size (self->bursts, 0);
}

guint
lrg_gpu_particles_get_burst_count (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), 0);
	return self->bursts->len;
}

gboolean
lrg_gpu_particles_get_burst (LrgGpuParticles *self,
                             guint            index,
                             gfloat          *time,
                             guint           *count)
{
	const Burst *b;

	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), FALSE);
	if (index >= self->bursts->len)
		return FALSE;
	b = &g_array_index (self->bursts, Burst, index);
	if (time != NULL)
		*time = b->time;
	if (count != NULL)
		*count = b->count;
	return TRUE;
}

gdouble
lrg_gpu_particles_get_time (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), 0.0);
	return self->time;
}

guint64
lrg_gpu_particles_get_emitted_total (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), 0);
	return self->emitted;
}

guint
lrg_gpu_particles_count_alive (LrgGpuParticles *self)
{
	guint i, alive = 0;

	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), 0);

	sync_from_gpu (self);
	for (i = 0; i < self->capacity; i++)
	{
		const gfloat *p = self->state + (gsize) i * FPP;

		if (p[3] < p[7])
			alive++;
	}
	return alive;
}

gfloat *
lrg_gpu_particles_read_back (LrgGpuParticles *self,
                             guint           *n_floats)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), NULL);
	g_return_val_if_fail (n_floats != NULL, NULL);

	sync_from_gpu (self);
	*n_floats = self->capacity * FPP;
	return g_memdup2 (self->state, (gsize) self->capacity * FPP * sizeof (gfloat));
}

gboolean
lrg_gpu_particles_get_particle (LrgGpuParticles *self,
                                guint            slot,
                                GrlVector3      *position,
                                GrlVector3      *velocity,
                                gfloat          *age,
                                gfloat          *lifetime)
{
	gfloat  local[FPP];
	gfloat *p;

	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), FALSE);
	g_return_val_if_fail (slot < self->capacity, FALSE);

	p = self->state + (gsize) slot * FPP;
	if (self->backend == LRG_PARTICLE_BACKEND_GPU && self->render_ok)
	{
		rlDrawRenderBatchActive ();
		gl.bind_buffer (GL_ARRAY_BUFFER_, self->buffers[self->cur]);
		gl.get_buffer_sub_data (GL_ARRAY_BUFFER_, (gssize) ((gsize) slot * FPP * sizeof (gfloat)),
		                        (gssize) sizeof local, local);
		gl.bind_buffer (GL_ARRAY_BUFFER_, 0);
		p = local;
	}
	if (position != NULL)
	{
		position->x = p[0];
		position->y = p[1];
		position->z = p[2];
	}
	if (velocity != NULL)
	{
		velocity->x = p[4];
		velocity->y = p[5];
		velocity->z = p[6];
	}
	if (age != NULL)
		*age = p[3];
	if (lifetime != NULL)
		*lifetime = p[7];
	return p[3] < p[7];
}

/* ==========================================================================
 * Public API: rendering
 * ========================================================================== */

/* matrix_to_floats: raylib's Matrix fields are named by storage index
 * (column major), so the GL array is m0 .. m15 by name */
static void
matrix_to_floats (Matrix  m,
                  gfloat *out)
{
	out[0] = m.m0;   out[1] = m.m1;   out[2] = m.m2;   out[3] = m.m3;
	out[4] = m.m4;   out[5] = m.m5;   out[6] = m.m6;   out[7] = m.m7;
	out[8] = m.m8;   out[9] = m.m9;   out[10] = m.m10; out[11] = m.m11;
	out[12] = m.m12; out[13] = m.m13; out[14] = m.m14; out[15] = m.m15;
}

void
lrg_gpu_particles_draw (LrgGpuParticles *self)
{
	const gint *r;
	gfloat      view[16], proj[16];
	gint        blend;
	Texture2D  *tex = NULL;

	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));

	if (!IsWindowReady () || !lrg_render_layers_is_visible (self->layer))
		return;
	choose_backend (self, NULL);
	if (!ensure_render (self, NULL))
		return;
	if (self->backend == LRG_PARTICLE_BACKEND_CPU && !self->cpu_uploaded)
		upload_state (self);
	rebuild_tables (self);

	/* raylib's batch first, then our own draw with its own program */
	rlDrawRenderBatchActive ();
	matrix_to_floats (rlGetMatrixModelview (), view);
	matrix_to_floats (rlGetMatrixProjection (), proj);
	switch (self->blend_mode)
	{
	case LRG_PARTICLE_BLEND_ALPHA:
		blend = RL_BLEND_ALPHA;
		break;
	case LRG_PARTICLE_BLEND_MULTIPLY:
		blend = RL_BLEND_MULTIPLIED;
		break;
	case LRG_PARTICLE_BLEND_ADDITIVE:
	default:
		blend = RL_BLEND_ADDITIVE;
		break;
	}
	rlSetBlendMode (blend);
	rlEnableDepthTest ();
	rlDisableDepthMask ();
	rlDisableBackfaceCulling ();

	r = self->rloc;
	gl.use_program (self->render_prog);
	gl.uniform_matrix4fv (r[R_VIEW], 1, 0, view);
	gl.uniform_matrix4fv (r[R_PROJ], 1, 0, proj);
	gl.uniform4fv (r[R_COLORS], TABLE, self->colors);
	gl.uniform1fv (r[R_SIZES], TABLE, self->sizes);
	gl.uniform1i (r[R_MODE], (gint) self->render_mode);
	gl.uniform1f (r[R_STRETCH], self->params.stretch);
	gl.uniform1ui (r[R_SEED], self->seed);
	gl.uniform1f (r[R_SIZE_RANDOM], self->params.size_random);
	if (self->texture != NULL)
		tex = (Texture2D *) grl_texture_get_handle (self->texture);
	gl.active_texture (GL_TEXTURE0_);
	gl.bind_texture (GL_TEXTURE_2D_, tex != NULL ? tex->id : rlGetTextureIdDefault ());
	gl.uniform1i (r[R_TEXTURE], 0);
	gl.uniform1i (r[R_TEXTURED], tex != NULL ? 1 : 0);

	gl.bind_vertex_array (self->render_vao[self->cur]);
	gl.draw_arrays_instanced (GL_TRIANGLE_STRIP_, 0, 4, (gint) self->capacity);
	gl.bind_vertex_array (0);
	gl.bind_buffer (GL_ARRAY_BUFFER_, 0);
	gl.bind_texture (GL_TEXTURE_2D_, 0);
	gl.use_program (0);
	self->draw_count++;

	/* raylib's 3D defaults back */
	rlEnableDepthMask ();
	rlEnableBackfaceCulling ();
	rlSetBlendMode (RL_BLEND_ALPHA);
}

void
lrg_gpu_particles_set_texture (LrgGpuParticles *self,
                               GrlTexture      *texture)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	g_return_if_fail (texture == NULL || GRL_IS_TEXTURE (texture));
	g_set_object (&self->texture, texture);
}

GrlTexture *
lrg_gpu_particles_get_texture (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), NULL);
	return self->texture;
}

guint
lrg_gpu_particles_get_draw_count (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), 0);
	return self->draw_count;
}

/* ==========================================================================
 * Public API: parameters
 * ========================================================================== */

/* set_vec3: three floats under one notify freeze */
static void
set_vec3 (LrgGpuParticles *self,
          const gchar     *prefix,
          gfloat           x,
          gfloat           y,
          gfloat           z)
{
	g_autofree gchar *nx = g_strconcat (prefix, "-x", NULL);
	g_autofree gchar *ny = g_strconcat (prefix, "-y", NULL);
	g_autofree gchar *nz = g_strconcat (prefix, "-z", NULL);

	g_object_freeze_notify (G_OBJECT (self));
	set_float_by_name (self, nx, x);
	set_float_by_name (self, ny, y);
	set_float_by_name (self, nz, z);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_gpu_particles_set_position (LrgGpuParticles *self,
                                gfloat           x,
                                gfloat           y,
                                gfloat           z)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	set_vec3 (self, "position", x, y, z);
}

void
lrg_gpu_particles_get_position (LrgGpuParticles *self,
                                gfloat          *x,
                                gfloat          *y,
                                gfloat          *z)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	if (x != NULL)
		*x = self->params.position[0];
	if (y != NULL)
		*y = self->params.position[1];
	if (z != NULL)
		*z = self->params.position[2];
}

void
lrg_gpu_particles_set_direction (LrgGpuParticles *self,
                                 gfloat           x,
                                 gfloat           y,
                                 gfloat           z)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	set_vec3 (self, "direction", x, y, z);
}

void
lrg_gpu_particles_set_gravity (LrgGpuParticles *self,
                               gfloat           x,
                               gfloat           y,
                               gfloat           z)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	set_vec3 (self, "gravity", x, y, z);
}

void
lrg_gpu_particles_set_wind (LrgGpuParticles *self,
                            gfloat           x,
                            gfloat           y,
                            gfloat           z)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	set_vec3 (self, "wind", x, y, z);
}

void
lrg_gpu_particles_set_colors (LrgGpuParticles *self,
                              const GrlColor  *start,
                              const GrlColor  *end)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	g_return_if_fail (start != NULL && end != NULL);

	g_object_freeze_notify (G_OBJECT (self));
	lrg_gpu_particles_set_color_gradient (self, NULL);
	set_color_prop (self, &self->start_color, start, PROP_START_COLOR);
	set_color_prop (self, &self->end_color, end, PROP_END_COLOR);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_gpu_particles_set_color_gradient (LrgGpuParticles        *self,
                                      const LrgGradientStops *gradient)
{
	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));

	if (gradient == NULL && self->gradient == NULL)
		return;
	if (gradient != NULL && self->gradient != NULL && lrg_gradient_stops_equal (gradient, self->gradient))
		return;
	g_clear_pointer (&self->gradient, lrg_gradient_stops_free);
	if (gradient != NULL)
		self->gradient = lrg_gradient_stops_copy (gradient);
	self->tables_dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLOR_GRADIENT]);
}

const LrgGradientStops *
lrg_gpu_particles_get_color_gradient (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), NULL);
	return self->gradient;
}

void
lrg_gpu_particles_set_size_curve (LrgGpuParticles  *self,
                                  LrgKeyframeCurve *curve)
{
	guint i;

	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	g_return_if_fail (curve == NULL || LRG_IS_KEYFRAME_CURVE (curve));

	g_set_object (&self->size_curve, curve);
	if (curve != NULL)
		for (i = 0; i < TABLE; i++)
			self->size_curve_samples[i] = lrg_keyframe_curve_sample (curve, (gfloat) i / (gfloat) (TABLE - 1));
	self->tables_dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SIZE_CURVE]);
}

LrgKeyframeCurve *
lrg_gpu_particles_get_size_curve (LrgGpuParticles *self)
{
	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), NULL);
	return self->size_curve;
}

void
lrg_gpu_particles_sample_color (LrgGpuParticles *self,
                                gfloat           t,
                                GrlColor        *color)
{
	gint   i, c;
	gfloat f;

	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	g_return_if_fail (color != NULL);

	rebuild_tables (self);
	table_position (t, &i, &f);
	for (c = 0; c < 4; c++)
	{
		gfloat a = self->colors[i * 4 + c];
		gfloat b = self->colors[(i + 1) * 4 + c];
		gfloat v = CLAMP (a + (b - a) * f, 0.0f, 1.0f);
		guint8 q = (guint8) lrintf (v * 255.0f);

		if (c == 0)
			color->r = q;
		else if (c == 1)
			color->g = q;
		else if (c == 2)
			color->b = q;
		else
			color->a = q;
	}
}

gfloat
lrg_gpu_particles_sample_size (LrgGpuParticles *self,
                               gfloat           t)
{
	gint   i;
	gfloat f;

	g_return_val_if_fail (LRG_IS_GPU_PARTICLES (self), 0.0f);

	rebuild_tables (self);
	table_position (t, &i, &f);
	return self->sizes[i] + (self->sizes[i + 1] - self->sizes[i]) * f;
}

const gchar * const *
lrg_gpu_particles_list_parameters (void)
{
	return parameter_names;
}

void
lrg_gpu_particles_copy_parameters (LrgGpuParticles *self,
                                   LrgGpuParticles *source)
{
	guint i;

	g_return_if_fail (LRG_IS_GPU_PARTICLES (self));
	g_return_if_fail (LRG_IS_GPU_PARTICLES (source));

	g_object_freeze_notify (G_OBJECT (self));
	for (i = 0; parameter_names[i] != NULL; i++)
	{
		g_auto(GValue) v = G_VALUE_INIT;

		g_object_get_property (G_OBJECT (source), parameter_names[i], &v);
		g_object_set_property (G_OBJECT (self), parameter_names[i], &v);
	}
	lrg_gpu_particles_set_size_curve (self, source->size_curve);
	lrg_gpu_particles_set_texture (self, source->texture);
	g_array_set_size (self->bursts, 0);
	g_array_append_vals (self->bursts, source->bursts->data, source->bursts->len);
	g_object_thaw_notify (G_OBJECT (self));
}

/* ==========================================================================
 * From a CPU particle system
 * ========================================================================== */

LrgGpuParticles *
lrg_gpu_particles_new_from_system (LrgParticleSystem  *system,
                                   guint               capacity,
                                   GError            **error)
{
	g_autoptr(LrgGpuParticles) self = NULL;
	LrgParticleEmitter        *emitter;
	GList                     *emitters, *forces, *l;
	gfloat                     x, y, z, a, b, c, d;
	gfloat                     gravity[3] = { 0.0f, 0.0f, 0.0f };
	gboolean                   have_attractor = FALSE;
	gboolean                   have_turbulence = FALSE;
	GrlColor                   start, end;

	g_return_val_if_fail (LRG_IS_PARTICLE_SYSTEM (system), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	emitters = lrg_particle_system_get_emitters (system);
	if (emitters == NULL || emitters->next != NULL)
	{
		g_set_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
		             "a GPU emitter mirrors exactly one CPU emitter (the system has %u)",
		             g_list_length (emitters));
		return NULL;
	}
	emitter = emitters->data;
	if (lrg_particle_emitter_get_emission_shape (emitter) == LRG_EMISSION_SHAPE_MESH)
	{
		g_set_error_literal (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
		                     "mesh emission shapes run on the CPU only");
		return NULL;
	}

	if (capacity == 0)
		capacity = lrg_particle_system_get_max_particles (system);
	self = lrg_gpu_particles_new (capacity);
	g_object_freeze_notify (G_OBJECT (self));

	/* emitter */
	lrg_particle_emitter_get_position (emitter, &x, &y, &z);
	lrg_gpu_particles_set_position (self, x, y, z);
	lrg_particle_emitter_get_direction (emitter, &x, &y, &z);
	lrg_gpu_particles_set_direction (self, x, y, z);
	set_float_by_name (self, "emission-rate", lrg_particle_emitter_get_emission_rate (emitter));
	set_float_by_name (self, "cone-angle", 0.0f);
	switch (lrg_particle_emitter_get_emission_shape (emitter))
	{
	case LRG_EMISSION_SHAPE_CIRCLE:
		g_object_set (self, "shape", LRG_GPU_PARTICLES_SHAPE_SPHERE, NULL);
		set_float_by_name (self, "shape-radius", lrg_particle_emitter_get_shape_radius (emitter));
		break;
	case LRG_EMISSION_SHAPE_RECTANGLE:
		g_object_set (self, "shape", LRG_GPU_PARTICLES_SHAPE_BOX, NULL);
		lrg_particle_emitter_get_shape_size (emitter, &x, &y, &z);
		set_float_by_name (self, "box-x", x);
		set_float_by_name (self, "box-y", y);
		set_float_by_name (self, "box-z", z);
		break;
	case LRG_EMISSION_SHAPE_CONE:
		g_object_set (self, "shape", LRG_GPU_PARTICLES_SHAPE_POINT, NULL);
		set_float_by_name (self, "cone-angle",
		                   lrg_particle_emitter_get_spread_angle (emitter) * (180.0f / G_PI));
		break;
	case LRG_EMISSION_SHAPE_POINT:
	case LRG_EMISSION_SHAPE_MESH:
	default:
		g_object_set (self, "shape", LRG_GPU_PARTICLES_SHAPE_POINT, NULL);
		break;
	}
	lrg_particle_emitter_get_initial_speed (emitter, &a, &b);
	set_float_by_name (self, "speed-min", a);
	set_float_by_name (self, "speed-max", b);
	lrg_particle_emitter_get_initial_lifetime (emitter, &a, &b);
	set_float_by_name (self, "lifetime-min", a);
	set_float_by_name (self, "lifetime-max", b);
	lrg_particle_emitter_get_initial_size (emitter, &a, &b);
	set_float_by_name (self, "size-start", b);
	set_float_by_name (self, "size-end", b);
	set_float_by_name (self, "size-random", b > 0.0f ? 1.0f - a / b : 0.0f);
	lrg_particle_emitter_get_start_color (emitter, &a, &b, &c, &d);
	start = (GrlColor) { (guint8) lrintf (CLAMP (a, 0.0f, 1.0f) * 255.0f),
	                     (guint8) lrintf (CLAMP (b, 0.0f, 1.0f) * 255.0f),
	                     (guint8) lrintf (CLAMP (c, 0.0f, 1.0f) * 255.0f),
	                     (guint8) lrintf (CLAMP (d, 0.0f, 1.0f) * 255.0f) };
	lrg_particle_emitter_get_end_color (emitter, &a, &b, &c, &d);
	end = (GrlColor) { (guint8) lrintf (CLAMP (a, 0.0f, 1.0f) * 255.0f),
	                   (guint8) lrintf (CLAMP (b, 0.0f, 1.0f) * 255.0f),
	                   (guint8) lrintf (CLAMP (c, 0.0f, 1.0f) * 255.0f),
	                   (guint8) lrintf (CLAMP (d, 0.0f, 1.0f) * 255.0f) };
	lrg_gpu_particles_set_colors (self, &start, &end);
	g_object_set (self,
	              "loop", lrg_particle_system_get_loop (system),
	              "duration", MAX (lrg_particle_system_get_duration (system), 0.001f),
	              "time-scale", lrg_particle_system_get_time_scale (system),
	              "layer", lrg_particle_system_get_layer (system),
	              NULL);
	switch (lrg_particle_system_get_blend_mode (system))
	{
	case LRG_PARTICLE_BLEND_ALPHA:
	case LRG_PARTICLE_BLEND_MULTIPLY:
	case LRG_PARTICLE_BLEND_ADDITIVE:
		g_object_set (self, "blend-mode", lrg_particle_system_get_blend_mode (system), NULL);
		break;
	default:
		break;
	}

	/* forces */
	forces = lrg_particle_system_get_forces (system);
	for (l = forces; l != NULL; l = l->next)
	{
		LrgParticleForce *force = l->data;
		gfloat            strength = lrg_particle_force_get_strength (force);

		if (!lrg_particle_force_get_enabled (force))
			continue;
		if (LRG_IS_PARTICLE_FORCE_GRAVITY (force))
		{
			lrg_particle_force_gravity_get_direction (LRG_PARTICLE_FORCE_GRAVITY (force), &x, &y, &z);
			gravity[0] += x * strength;
			gravity[1] += y * strength;
			gravity[2] += z * strength;
		}
		else if (LRG_IS_PARTICLE_FORCE_WIND (force))
		{
			if (lrg_particle_force_wind_get_turbulence (LRG_PARTICLE_FORCE_WIND (force)) > 0.0001f)
			{
				g_set_error_literal (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
				                     "gusty wind (wind turbulence > 0) runs on the CPU only; "
				                     "use a turbulence force instead");
				return NULL;
			}
			lrg_particle_force_wind_get_direction (LRG_PARTICLE_FORCE_WIND (force), &x, &y, &z);
			gravity[0] += x * strength;
			gravity[1] += y * strength;
			gravity[2] += z * strength;
		}
		else if (LRG_IS_PARTICLE_FORCE_ATTRACTOR (force))
		{
			LrgParticleForceAttractor *att = LRG_PARTICLE_FORCE_ATTRACTOR (force);

			if (have_attractor)
			{
				g_set_error_literal (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
				                     "the GPU path runs one attractor");
				return NULL;
			}
			have_attractor = TRUE;
			lrg_particle_force_attractor_get_position (att, &x, &y, &z);
			set_float_by_name (self, "attractor-x", x);
			set_float_by_name (self, "attractor-y", y);
			set_float_by_name (self, "attractor-z", z);
			set_float_by_name (self, "attractor-strength", strength);
			set_float_by_name (self, "attractor-radius", lrg_particle_force_attractor_get_radius (att));
			set_float_by_name (self, "attractor-falloff", lrg_particle_force_attractor_get_falloff (att));
		}
		else if (LRG_IS_PARTICLE_FORCE_TURBULENCE (force))
		{
			LrgParticleForceTurbulence *turb = LRG_PARTICLE_FORCE_TURBULENCE (force);

			if (have_turbulence)
			{
				g_set_error_literal (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
				                     "the GPU path runs one turbulence field");
				return NULL;
			}
			have_turbulence = TRUE;
			set_float_by_name (self, "turbulence-strength",
			                   lrg_particle_force_turbulence_get_amplitude (turb) * strength);
			set_float_by_name (self, "turbulence-frequency",
			                   lrg_particle_force_turbulence_get_frequency (turb));
			set_float_by_name (self, "turbulence-speed",
			                   lrg_particle_force_turbulence_get_scroll_speed (turb));
		}
		else
		{
			g_set_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
			             "force type %s has no GPU equivalent", G_OBJECT_TYPE_NAME (force));
			return NULL;
		}
	}
	lrg_gpu_particles_set_gravity (self, gravity[0], gravity[1], gravity[2]);
	g_object_thaw_notify (G_OBJECT (self));
	return g_steal_pointer (&self);
}

/* ==========================================================================
 * From an .rparticles emitter
 *
 * Modules are read through their GObject properties (the .rparticles
 * keys), so a renamed key shows up as an "unsupported" error naming it
 * instead of a silent mismatch.
 * ========================================================================== */

static gboolean
unsupported (GError      **error,
             const gchar  *emitter,
             const gchar  *kind,
             const gchar  *why)
{
	g_set_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
	             "emitter '%s': %s%s%s", emitter != NULL ? emitter : "", kind != NULL ? kind : "",
	             kind != NULL ? ": " : "", why);
	return FALSE;
}

/* module_get: reads property @name of @module into @v (initialised);
 * FALSE when the module has no such property */
static gboolean
module_get (LrgParticleModule *module,
            const gchar       *name,
            GValue            *v)
{
	GParamSpec *ps = g_object_class_find_property (G_OBJECT_GET_CLASS (module), name);

	if (ps == NULL)
		return FALSE;
	g_value_init (v, G_PARAM_SPEC_VALUE_TYPE (ps));
	g_object_get_property (G_OBJECT (module), name, v);
	return TRUE;
}

static gboolean
module_float (LrgParticleModule *module,
              const gchar       *name,
              gfloat            *out)
{
	g_auto(GValue) v = G_VALUE_INIT;

	if (!module_get (module, name, &v))
		return FALSE;
	if (G_VALUE_HOLDS_FLOAT (&v))
		*out = g_value_get_float (&v);
	else if (G_VALUE_HOLDS_DOUBLE (&v))
		*out = (gfloat) g_value_get_double (&v);
	else if (G_VALUE_HOLDS_INT (&v))
		*out = (gfloat) g_value_get_int (&v);
	else if (G_VALUE_HOLDS_UINT (&v))
		*out = (gfloat) g_value_get_uint (&v);
	else if (G_VALUE_HOLDS_BOOLEAN (&v))
		*out = g_value_get_boolean (&v) ? 1.0f : 0.0f;
	else if (G_VALUE_HOLDS_ENUM (&v))
		*out = (gfloat) g_value_get_enum (&v);
	else
		return FALSE;
	return TRUE;
}

static gboolean
module_range (LrgParticleModule *module,
              const gchar       *name,
              gfloat            *min,
              gfloat            *max)
{
	g_auto(GValue)    v = G_VALUE_INIT;
	const GrlVector2 *r;

	if (!module_get (module, name, &v) || G_VALUE_TYPE (&v) != GRL_TYPE_VECTOR2)
		return FALSE;
	r = g_value_get_boxed (&v);
	*min = r != NULL ? r->x : 0.0f;
	*max = r != NULL ? r->y : 0.0f;
	return TRUE;
}

static gboolean
module_vec3 (LrgParticleModule *module,
             const gchar       *name,
             gfloat            *out)
{
	g_auto(GValue)    v = G_VALUE_INIT;
	const GrlVector3 *r;

	if (!module_get (module, name, &v) || G_VALUE_TYPE (&v) != GRL_TYPE_VECTOR3)
		return FALSE;
	r = g_value_get_boxed (&v);
	out[0] = r != NULL ? r->x : 0.0f;
	out[1] = r != NULL ? r->y : 0.0f;
	out[2] = r != NULL ? r->z : 0.0f;
	return TRUE;
}

/* module_boxed / module_object: (transfer full) values, NULL when unset */
static gpointer
module_boxed (LrgParticleModule *module,
              const gchar       *name,
              GType              type)
{
	g_auto(GValue) v = G_VALUE_INIT;

	if (!module_get (module, name, &v) || G_VALUE_TYPE (&v) != type)
		return NULL;
	return g_value_dup_boxed (&v);
}

static GObject *
module_object (LrgParticleModule *module,
               const gchar       *name)
{
	g_auto(GValue) v = G_VALUE_INIT;

	if (!module_get (module, name, &v) || !G_VALUE_HOLDS_OBJECT (&v))
		return NULL;
	return g_value_dup_object (&v);
}

static gboolean
is_zero3 (const gfloat *v)
{
	return fabsf (v[0]) < 1e-6f && fabsf (v[1]) < 1e-6f && fabsf (v[2]) < 1e-6f;
}

#define NEED(ok, prop) \
	do { if (!(ok)) return unsupported (error, ename, kind, "no property '" prop "' (GPU mapping out of date)"); } while (0)

/* map_emitter: validates @em and, when @self is not NULL, writes its
 * parameters into @self */
static gboolean
map_emitter (LrgGpuParticles          *self,
             LrgParticleAsset         *asset,
             LrgParticleAssetEmitter  *em,
             GError                  **error)
{
	const gchar               *ename = lrg_particle_asset_emitter_get_name (em);
	LrgParticleRenderSettings *rs = lrg_particle_asset_emitter_get_renderer (em);
	guint                      n = lrg_particle_asset_emitter_get_n_modules (em);
	guint                      i;
	gboolean                   have_rate = FALSE, have_drag = FALSE, have_wind = FALSE;
	gboolean                   have_turb = FALSE, have_attr = FALSE, have_vortex = FALSE;
	gboolean                   have_coll = FALSE, have_size_curve = FALSE, shape_cone = FALSE;
	gfloat                     velocity_spread = 0.0f, shape_angle = 0.0f;
	gfloat                     gravity[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                     offset[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                     size_mult = 1.0f;
	gfloat                     size_min = -1.0f, size_max = -1.0f;
	GrlColor                   base = { 255, 255, 255, 255 };
	g_autoptr(LrgGradientStops) gradient = NULL;
	g_autoptr(LrgKeyframeCurve) alpha_curve = NULL;
	g_autoptr(LrgKeyframeCurve) size_curve = NULL;
	g_autoptr(GArray)           bursts = g_array_new (FALSE, FALSE, sizeof (Burst));
	Params                      pr;
	LrgGpuParticlesShape        shape = LRG_GPU_PARTICLES_SHAPE_POINT;
	gfloat                      direction[3] = { 0.0f, 1.0f, 0.0f };
	gfloat                      plane[3] = { 0.0f, 0.4f, 0.1f };
	const gchar                *kind = NULL;

	/* start from the class defaults for every float */
	for (i = 0; i < N_FLOAT_PARAMS; i++)
		*(gfloat *) ((guint8 *) &pr + float_params[i].offset) = float_params[i].def;
	pr.emission_rate = 0.0f;
	pr.cone_angle = 0.0f;

	if (lrg_particle_asset_emitter_get_start_delay (em) > 0.0f)
		return unsupported (error, ename, NULL, "start-delay runs on the CPU only");
	if (rs != NULL && lrg_particle_render_settings_get_kind (rs) != LRG_PARTICLE_RENDERER_BILLBOARD &&
	    lrg_particle_render_settings_get_kind (rs) != LRG_PARTICLE_RENDERER_NONE)
		return unsupported (error, ename, NULL, "only the billboard renderer has a GPU path");

	for (i = 0; i < n; i++)
	{
		LrgParticleModule *m = lrg_particle_asset_emitter_get_module (em, i);
		gfloat             a = 0.0f, b = 0.0f, v3[3];

		if (m == NULL || !lrg_particle_module_get_enabled (m))
			continue;
		kind = lrg_particle_module_get_kind (m);
		if (!lrg_particle_module_get_gpu_capable (m))
			return unsupported (error, ename, kind, "the module is not GPU capable");

		if (g_strcmp0 (kind, "spawn-rate") == 0)
		{
			g_autoptr(GObject) curve = module_object (m, "rate-curve");

			if (have_rate)
				return unsupported (error, ename, kind, "one spawn-rate module only");
			have_rate = TRUE;
			NEED (module_float (m, "rate", &pr.emission_rate), "rate");
			if (curve != NULL)
				return unsupported (error, ename, kind, "rate-curve needs CPU-side counts");
			if (module_float (m, "per-distance", &a) && a != 0.0f)
				return unsupported (error, ename, kind, "per-distance emission needs CPU-side counts");
		}
		else if (g_strcmp0 (kind, "burst") == 0)
		{
			Burst  bu;
			gfloat cycles = 1.0f, interval = 0.0f, prob = 1.0f;

			NEED (module_float (m, "time", &bu.time), "time");
			NEED (module_range (m, "count", &a, &b), "count");
			module_float (m, "cycles", &cycles);
			module_float (m, "interval", &interval);
			module_float (m, "probability", &prob);
			if (fabsf (a - b) > 0.5f)
				return unsupported (error, ename, kind, "a random burst count needs CPU-side counts");
			if (prob < 1.0f)
				return unsupported (error, ename, kind, "burst probability needs CPU-side counts");
			if (cycles != 1.0f && !(cycles == 0.0f && (interval <= 0.0f ||
			                        fabsf (interval - lrg_particle_asset_get_duration (asset)) < 1e-4f)))
				return unsupported (error, ename, kind,
				                    "repeating bursts run on the GPU only once per loop of the effect");
			bu.count = (guint) lrintf (MAX (a, 0.0f));
			g_array_append_val (bursts, bu);
		}
		else if (g_strcmp0 (kind, "shape") == 0)
		{
			gfloat sh = 0.0f, arc = 360.0f, randomize = 0.0f;

			NEED (module_float (m, "shape", &sh), "shape");
			module_float (m, "arc", &arc);
			module_float (m, "randomize-direction", &randomize);
			if (arc < 359.99f)
				return unsupported (error, ename, kind, "partial arcs run on the CPU only");
			if (randomize > 0.0f)
				return unsupported (error, ename, kind, "randomize-direction runs on the CPU only");
			if (module_vec3 (m, "offset", v3))
				memcpy (offset, v3, sizeof offset);
			switch ((LrgParticleShape) lrintf (sh))
			{
			case LRG_PARTICLE_SHAPE_POINT:
				shape = LRG_GPU_PARTICLES_SHAPE_POINT;
				break;
			case LRG_PARTICLE_SHAPE_SPHERE:
				shape = LRG_GPU_PARTICLES_SHAPE_SPHERE;
				NEED (module_float (m, "radius", &pr.shape_radius), "radius");
				break;
			case LRG_PARTICLE_SHAPE_CONE:
				shape = LRG_GPU_PARTICLES_SHAPE_CONE;
				shape_cone = TRUE;
				NEED (module_float (m, "radius", &pr.shape_radius), "radius");
				NEED (module_float (m, "angle", &shape_angle), "angle");
				break;
			case LRG_PARTICLE_SHAPE_BOX:
				shape = LRG_GPU_PARTICLES_SHAPE_BOX;
				NEED (module_vec3 (m, "size", pr.box), "size");
				break;
			case LRG_PARTICLE_SHAPE_HEMISPHERE:
			case LRG_PARTICLE_SHAPE_CIRCLE:
			case LRG_PARTICLE_SHAPE_MESH:
			default:
				return unsupported (error, ename, kind, "hemisphere, circle and mesh shapes run on the CPU only");
			}
		}
		else if (g_strcmp0 (kind, "velocity") == 0)
		{
			NEED (module_range (m, "speed", &pr.speed_min, &pr.speed_max), "speed");
			if (module_vec3 (m, "direction", v3) && !is_zero3 (v3))
				memcpy (direction, v3, sizeof direction);
			module_float (m, "spread", &velocity_spread);
			if (module_float (m, "inherit", &a) && a != 0.0f)
				return unsupported (error, ename, kind, "inherited emitter velocity runs on the CPU only");
		}
		else if (g_strcmp0 (kind, "lifetime") == 0)
			NEED (module_range (m, "lifetime", &pr.lifetime_min, &pr.lifetime_max), "lifetime");
		else if (g_strcmp0 (kind, "size") == 0)
			NEED (module_range (m, "size", &size_min, &size_max), "size");
		else if (g_strcmp0 (kind, "rotation") == 0)
		{
			module_range (m, "rotation", &pr.rotation_min, &pr.rotation_max);
			module_range (m, "angular-velocity", &pr.spin_min, &pr.spin_max);
		}
		else if (g_strcmp0 (kind, "color") == 0)
		{
			g_autoptr(GrlColor)         c = module_boxed (m, "color", GRL_TYPE_COLOR);
			g_autoptr(GrlColor)         cmax = module_boxed (m, "color-max", GRL_TYPE_COLOR);
			g_autoptr(LrgGradientStops) g = module_boxed (m, "gradient", LRG_TYPE_GRADIENT_STOPS);

			if (cmax != NULL || g != NULL)
				return unsupported (error, ename, kind, "a random start colour runs on the CPU only");
			if (c != NULL)
				base = *c;
		}
		else if (g_strcmp0 (kind, "size-over-life") == 0)
		{
			g_clear_object (&size_curve);
			size_curve = (LrgKeyframeCurve *) module_object (m, "curve");
			module_float (m, "multiplier", &size_mult);
			have_size_curve = size_curve != NULL;
		}
		else if (g_strcmp0 (kind, "color-over-life") == 0)
		{
			g_clear_pointer (&gradient, lrg_gradient_stops_free);
			gradient = module_boxed (m, "gradient", LRG_TYPE_GRADIENT_STOPS);
		}
		else if (g_strcmp0 (kind, "alpha-over-life") == 0)
		{
			g_clear_object (&alpha_curve);
			alpha_curve = (LrgKeyframeCurve *) module_object (m, "curve");
		}
		else if (g_strcmp0 (kind, "gravity") == 0)
		{
			NEED (module_vec3 (m, "gravity", v3), "gravity");
			gravity[0] += v3[0];
			gravity[1] += v3[1];
			gravity[2] += v3[2];
		}
		else if (g_strcmp0 (kind, "wind") == 0 || g_strcmp0 (kind, "drag") == 0)
		{
			gboolean wind = kind[0] == 'w';

			if ((wind && have_wind) || (!wind && have_drag))
				return unsupported (error, ename, kind, "one module of this kind only");
			if (wind)
			{
				have_wind = TRUE;
				NEED (module_vec3 (m, "velocity", pr.wind), "velocity");
				NEED (module_float (m, "strength", &a), "strength");
				if (module_float (m, "turbulence", &b) && b > 0.0f)
					return unsupported (error, ename, kind, "gusty wind runs on the CPU only");
			}
			else
			{
				have_drag = TRUE;
				NEED (module_float (m, "drag", &a), "drag");
			}
			/* wind and drag share one coupling to the air velocity */
			if (have_wind && have_drag && fabsf (pr.drag - a) > 1e-6f && pr.drag != 0.0f)
				return unsupported (error, ename, kind, "wind and drag with different strengths");
			pr.drag = a;
		}
		else if (g_strcmp0 (kind, "turbulence") == 0)
		{
			gfloat octaves = 1.0f;

			if (have_turb)
				return unsupported (error, ename, kind, "one turbulence module only");
			have_turb = TRUE;
			NEED (module_float (m, "amplitude", &pr.turbulence_strength), "amplitude");
			module_float (m, "frequency", &pr.turbulence_frequency);
			module_float (m, "scroll-speed", &pr.turbulence_speed);
			module_float (m, "octaves", &octaves);
			if (octaves > 1.0f)
				return unsupported (error, ename, kind, "more than one octave runs on the CPU only");
		}
		else if (g_strcmp0 (kind, "vortex") == 0)
		{
			gfloat axis[3] = { 0.0f, 1.0f, 0.0f }, center[3] = { 0.0f, 0.0f, 0.0f }, pull = 0.0f;

			if (have_vortex)
				return unsupported (error, ename, kind, "one vortex module only");
			have_vortex = TRUE;
			module_vec3 (m, "axis", axis);
			module_vec3 (m, "center", center);
			module_float (m, "pull", &pull);
			NEED (module_float (m, "strength", &pr.vortex_strength), "strength");
			if (fabsf (axis[0]) > 1e-4f || fabsf (axis[2]) > 1e-4f || !is_zero3 (center) || pull != 0.0f)
				return unsupported (error, ename, kind,
				                    "the GPU vortex turns about the vertical axis through the emitter, without pull");
			if (axis[1] < 0.0f)
				pr.vortex_strength = -pr.vortex_strength;
		}
		else if (g_strcmp0 (kind, "attractor") == 0)
		{
			if (have_attr)
				return unsupported (error, ename, kind, "one attractor only");
			have_attr = TRUE;
			NEED (module_vec3 (m, "position", pr.attractor), "position");
			NEED (module_float (m, "strength", &pr.attractor_strength), "strength");
			module_float (m, "radius", &pr.attractor_radius);
			module_float (m, "falloff", &pr.attractor_falloff);
			if (module_float (m, "kill-radius", &a) && a > 0.0f)
				return unsupported (error, ename, kind, "kill-radius runs on the CPU only");
		}
		else if (g_strcmp0 (kind, "collision") == 0)
		{
			gfloat mode = 0.0f, normal[3] = { 0.0f, 1.0f, 0.0f }, kill = 0.0f, loss = 0.0f;

			if (have_coll)
				return unsupported (error, ename, kind, "one collision module only");
			have_coll = TRUE;
			module_float (m, "mode", &mode);
			module_vec3 (m, "normal", normal);
			module_float (m, "kill", &kill);
			module_float (m, "lifetime-loss", &loss);
			if (lrintf (mode) != LRG_PARTICLE_COLLISION_PLANE)
				return unsupported (error, ename, kind, "world collision runs on the CPU only");
			if (fabsf (normal[0]) > 1e-4f || fabsf (normal[2]) > 1e-4f || normal[1] <= 0.0f)
				return unsupported (error, ename, kind, "the GPU collides with horizontal planes only");
			if (kill != 0.0f || loss != 0.0f)
				return unsupported (error, ename, kind, "kill and lifetime-loss run on the CPU only");
			module_float (m, "offset", &plane[0]);
			plane[0] /= normal[1];
			module_float (m, "bounce", &plane[1]);
			module_float (m, "friction", &plane[2]);
		}
		else
			return unsupported (error, ename, kind, "no GPU equivalent (rotation-over-life, sub-emitters "
			                    "and custom modules run on the CPU)");
	}

	if (self == NULL)
		return TRUE;

	/* write everything in one notify batch */
	g_object_freeze_notify (G_OBJECT (self));
	pr.cone_angle = shape_cone ? MAX (shape_angle, velocity_spread) : velocity_spread;
	pr.box[0] = MAX (pr.box[0], 0.0f);
	if (size_min >= 0.0f)
	{
		pr.size_start = size_max;
		pr.size_random = size_max > 0.0f ? CLAMP (1.0f - size_min / size_max, 0.0f, 1.0f) : 0.0f;
	}
	pr.size_start *= size_mult;
	pr.size_end = pr.size_start;
	pr.gravity[0] = gravity[0];
	pr.gravity[1] = gravity[1];
	pr.gravity[2] = gravity[2];
	pr.position[0] = offset[0];
	pr.position[1] = offset[1];
	pr.position[2] = offset[2];
	pr.direction[0] = direction[0];
	pr.direction[1] = direction[1];
	pr.direction[2] = direction[2];
	pr.plane_height = plane[0];
	pr.bounce = plane[1];
	pr.friction = plane[2];
	pr.duration = MAX (lrg_particle_asset_get_duration (asset), 0.001f);
	for (i = 0; i < N_FLOAT_PARAMS; i++)
		set_float (self, i, *(gfloat *) ((guint8 *) &pr + float_params[i].offset));
	g_object_set (self,
	              "shape", shape,
	              "seed", (guint) (lrg_particle_asset_get_seed (asset) & G_MAXUINT32),
	              "loop", lrg_particle_asset_get_loop (asset),
	              "collide", have_coll,
	              NULL);
	if (rs != NULL)
		g_object_set (self, "blend-mode", lrg_particle_render_settings_get_blend (rs), NULL);
	lrg_gpu_particles_set_size_curve (self, have_size_curve ? size_curve : NULL);

	/* colour: the start colour tinted by the gradient, alpha by the curve */
	if (gradient == NULL && alpha_curve == NULL)
		lrg_gpu_particles_set_colors (self, &base, &base);
	else
	{
		g_autoptr(LrgGradientStops) baked = lrg_gradient_stops_new ();
		guint                       s;

		for (s = 0; s < TABLE; s++)
		{
			gfloat   t = (gfloat) s / (gfloat) (TABLE - 1);
			GrlColor g = { 255, 255, 255, 255 };
			GrlColor c;

			if (gradient != NULL)
				lrg_gradient_stops_evaluate (gradient, t, &g);
			c.r = (guint8) ((guint) base.r * g.r / 255u);
			c.g = (guint8) ((guint) base.g * g.g / 255u);
			c.b = (guint8) ((guint) base.b * g.b / 255u);
			c.a = (guint8) ((guint) base.a * g.a / 255u);
			if (alpha_curve != NULL)
				c.a = (guint8) lrintf (CLAMP ((gfloat) c.a * lrg_keyframe_curve_sample (alpha_curve, t), 0.0f, 255.0f));
			lrg_gradient_stops_add (baked, t, &c);
		}
		lrg_gpu_particles_set_color_gradient (self, baked);
	}
	g_array_set_size (self->bursts, 0);
	g_array_append_vals (self->bursts, bursts->data, bursts->len);
	g_object_thaw_notify (G_OBJECT (self));
	return TRUE;
}

#undef NEED

gboolean
lrg_gpu_particles_check_emitter (LrgParticleAsset         *asset,
                                 LrgParticleAssetEmitter  *emitter,
                                 GError                  **error)
{
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (asset), FALSE);
	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET_EMITTER (emitter), FALSE);
	return map_emitter (NULL, asset, emitter, error);
}

LrgGpuParticles *
lrg_gpu_particles_new_from_asset (LrgParticleAsset  *asset,
                                  guint              emitter,
                                  guint              capacity,
                                  GError           **error)
{
	g_autoptr(LrgGpuParticles) self = NULL;
	LrgParticleAssetEmitter   *em;

	g_return_val_if_fail (LRG_IS_PARTICLE_ASSET (asset), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	em = lrg_particle_asset_get_emitter (asset, emitter);
	if (em == NULL)
	{
		g_set_error (error, LRG_GPU_PARTICLES_ERROR, LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
		             "the asset has no emitter %u", emitter);
		return NULL;
	}
	if (!map_emitter (NULL, asset, em, error))
		return NULL;
	if (capacity == 0)
		capacity = MAX (lrg_particle_asset_emitter_get_max_particles (em), 1u);
	self = lrg_gpu_particles_new (capacity);
	map_emitter (self, asset, em, NULL);
	return g_steal_pointer (&self);
}
