/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <pthread.h>

#include <glslang/build_info.h>
#include <glslang/Include/glslang_c_interface.h>

#include "vulkan_spirv.h"
#include "libavutil/mem.h"
#include "libavutil/avassert.h"

static pthread_mutex_t glslc_mutex = PTHREAD_MUTEX_INITIALIZER;
static int glslc_refcount = 0;

static const glslang_resource_t glslc_resource_limits = {
    .max_lights = 32,
    .max_clip_planes = 6,
    .max_texture_units = 32,
    .max_texture_coords = 32,
    .max_vertex_attribs = 64,
    .max_vertex_uniform_components = 4096,
    .max_varying_floats = 64,
    .max_vertex_texture_image_units = 32,
    .max_combined_texture_image_units = 80,
    .max_texture_image_units = 32,
    .max_fragment_uniform_components = 4096,
    .max_draw_buffers = 32,
    .max_vertex_uniform_vectors = 128,
    .max_varying_vectors = 8,
    .max_fragment_uniform_vectors = 16,
    .max_vertex_output_vectors = 16,
    .max_fragment_input_vectors = 15,
    .min_program_texel_offset = -8,
    .max_program_texel_offset = 7,
    .max_clip_distances = 8,
    .max_compute_work_group_count_x = 65535,
    .max_compute_work_group_count_y = 65535,
    .max_compute_work_group_count_z = 65535,
    .max_compute_work_group_size_x = 1024,
    .max_compute_work_group_size_y = 1024,
    .max_compute_work_group_size_z = 64,
    .max_compute_uniform_components = 1024,
    .max_compute_texture_image_units = 16,
    .max_compute_image_uniforms = 8,
    .max_compute_atomic_counters = 8,
    .max_compute_atomic_counter_buffers = 1,
    .max_varying_components = 60,
    .max_vertex_output_components = 64,
    .max_geometry_input_components = 64,
    .max_geometry_output_components = 128,
    .max_fragment_input_components = 128,
    .max_image_units = 8,
    .max_combined_image_units_and_fragment_outputs = 8,
    .max_combined_shader_output_resources = 8,
    .max_image_samples = 0,
    .max_vertex_image_uniforms = 0,
    .max_tess_control_image_uniforms = 0,
    .max_tess_evaluation_image_uniforms = 0,
    .max_geometry_image_uniforms = 0,
    .max_fragment_image_uniforms = 8,
    .max_combined_image_uniforms = 8,
    .max_geometry_texture_image_units = 16,
    .max_geometry_output_vertices = 256,
    .max_geometry_total_output_components = 1024,
    .max_geometry_uniform_components = 1024,
    .max_geometry_varying_components = 64,
    .max_tess_control_input_components = 128,
    .max_tess_control_output_components = 128,
    .max_tess_control_texture_image_units = 16,
    .max_tess_control_uniform_components = 1024,
    .max_tess_control_total_output_components = 4096,
    .max_tess_evaluation_input_components = 128,
    .max_tess_evaluation_output_components = 128,
    .max_tess_evaluation_texture_image_units = 16,
    .max_tess_evaluation_uniform_components = 1024,
    .max_tess_patch_components = 120,
    .max_patch_vertices = 32,
    .max_tess_gen_level = 64,
    .max_viewports = 16,
    .max_vertex_atomic_counters = 0,
    .max_tess_control_atomic_counters = 0,
    .max_tess_evaluation_atomic_counters = 0,
    .max_geometry_atomic_counters = 0,
    .max_fragment_atomic_counters = 8,
    .max_combined_atomic_counters = 8,
    .max_atomic_counter_bindings = 1,
    .max_vertex_atomic_counter_buffers = 0,
    .max_tess_control_atomic_counter_buffers = 0,
    .max_tess_evaluation_atomic_counter_buffers = 0,
    .max_geometry_atomic_counter_buffers = 0,
    .max_fragment_atomic_counter_buffers = 1,
    .max_combined_atomic_counter_buffers = 1,
    .max_atomic_counter_buffer_size = 16384,
    .max_transform_feedback_buffers = 4,
    .max_transform_feedback_interleaved_components = 64,
    .max_cull_distances = 8,
    .max_combined_clip_and_cull_distances = 8,
    .max_samples = 4,
    .max_mesh_output_vertices_nv = 256,
    .max_mesh_output_primitives_nv = 512,
    .max_mesh_work_group_size_x_nv = 32,
    .max_mesh_work_group_size_y_nv = 1,
    .max_mesh_work_group_size_z_nv = 1,
    .max_task_work_group_size_x_nv = 32,
    .max_task_work_group_size_y_nv = 1,
    .max_task_work_group_size_z_nv = 1,
    .max_mesh_view_count_nv = 4,
    .maxDualSourceDrawBuffersEXT = 1,

    .limits = {
        .non_inductive_for_loops = 1,
        .while_loops = 1,
        .do_while_loops = 1,
        .general_uniform_indexing = 1,
        .general_attribute_matrix_vector_indexing = 1,
        .general_varying_indexing = 1,
        .general_sampler_indexing = 1,
        .general_variable_indexing = 1,
        .general_constant_matrix_vector_indexing = 1,
    }
};

static int glslc_shader_compile(FFVulkanContext *s, FFVkSPIRVCompiler *ctx,
                                FFVulkanShader *shd, uint8_t **data,
                                size_t *size, const char *entrypoint,
                                void **opaque)
{
    const char *messages;
    glslang_shader_t *glslc_shader;
    glslang_program_t *glslc_program;

    static const glslang_stage_t glslc_stage[] = {
        [VK_SHADER_STAGE_VERTEX_BIT]   = GLSLANG_STAGE_VERTEX,
        [VK_SHADER_STAGE_FRAGMENT_BIT] = GLSLANG_STAGE_FRAGMENT,
        [VK_SHADER_STAGE_COMPUTE_BIT]  = GLSLANG_STAGE_COMPUTE,
#if ((GLSLANG_VERSION_MAJOR) > 12)
        [VK_SHADER_STAGE_TASK_BIT_EXT] = GLSLANG_STAGE_TASK,
        [VK_SHADER_STAGE_MESH_BIT_EXT] = GLSLANG_STAGE_MESH,
        [VK_SHADER_STAGE_RAYGEN_BIT_KHR] = GLSLANG_STAGE_RAYGEN,
        [VK_SHADER_STAGE_INTERSECTION_BIT_KHR] = GLSLANG_STAGE_INTERSECT,
        [VK_SHADER_STAGE_ANY_HIT_BIT_KHR] = GLSLANG_STAGE_ANYHIT,
        [VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR] = GLSLANG_STAGE_CLOSESTHIT,
        [VK_SHADER_STAGE_MISS_BIT_KHR] = GLSLANG_STAGE_MISS,
        [VK_SHADER_STAGE_CALLABLE_BIT_KHR] = GLSLANG_STAGE_CALLABLE,
#endif
    };

    const glslang_input_t glslc_input = {
        .language                          = GLSLANG_SOURCE_GLSL,
        .stage                             = glslc_stage[shd->stage],
        .client                            = GLSLANG_CLIENT_VULKAN,
#if ((GLSLANG_VERSION_MAJOR) >= 12)
        .client_version                    = GLSLANG_TARGET_VULKAN_1_3,
        .target_language_version           = GLSLANG_TARGET_SPV_1_6,
#else
        .client_version                    = GLSLANG_TARGET_VULKAN_1_2,
        .target_language_version           = GLSLANG_TARGET_SPV_1_5,
#endif
        .target_language                   = GLSLANG_TARGET_SPV,
        .code                              = shd->src.str,
        .default_version                   = 460,
        .default_profile                   = GLSLANG_NO_PROFILE,
        .force_default_version_and_profile = false,
        .forward_compatible                = false,
        .messages                          = GLSLANG_MSG_DEFAULT_BIT,
        .resource                          = &glslc_resource_limits,
    };

#if ((GLSLANG_VERSION_MAJOR) >= 12)
    glslang_spv_options_t glslc_opts = {
        .generate_debug_info = !!(s->extensions & (FF_VK_EXT_DEBUG_UTILS | FF_VK_EXT_RELAXED_EXTENDED_INSTR)),
        .emit_nonsemantic_shader_debug_info = !!(s->extensions & FF_VK_EXT_RELAXED_EXTENDED_INSTR),
        .emit_nonsemantic_shader_debug_source = !!(s->extensions & FF_VK_EXT_RELAXED_EXTENDED_INSTR),
        .disable_optimizer = !!(s->extensions & FF_VK_EXT_DEBUG_UTILS),
        .strip_debug_info = !(s->extensions & (FF_VK_EXT_DEBUG_UTILS | FF_VK_EXT_RELAXED_EXTENDED_INSTR)),
        .optimize_size = 0,
        .disassemble = 0,
        .validate = 1,
        .compile_only = 0,
    };
#endif

    av_assert0(glslc_refcount);

    *opaque = NULL;

    if (!(glslc_shader = glslang_shader_create(&glslc_input)))
        return AVERROR(ENOMEM);

    if (!glslang_shader_preprocess(glslc_shader, &glslc_input)) {
        ff_vk_shader_print(s, shd, AV_LOG_WARNING);
        av_log(s, AV_LOG_ERROR, "Unable to preprocess shader: %s (%s)!\n",
               glslang_shader_get_info_log(glslc_shader),
               glslang_shader_get_info_debug_log(glslc_shader));
        glslang_shader_delete(glslc_shader);
        return AVERROR(EINVAL);
    }

    if (!glslang_shader_parse(glslc_shader, &glslc_input)) {
        ff_vk_shader_print(s, shd, AV_LOG_WARNING);
        av_log(s, AV_LOG_ERROR, "Unable to parse shader: %s (%s)!\n",
               glslang_shader_get_info_log(glslc_shader),
               glslang_shader_get_info_debug_log(glslc_shader));
        glslang_shader_delete(glslc_shader);
        return AVERROR(EINVAL);
    }

    if (!(glslc_program = glslang_program_create())) {
        glslang_shader_delete(glslc_shader);
        return AVERROR(EINVAL);
    }

    glslang_program_add_shader(glslc_program, glslc_shader);

    if (!glslang_program_link(glslc_program, GLSLANG_MSG_SPV_RULES_BIT |
                                             GLSLANG_MSG_VULKAN_RULES_BIT)) {
        ff_vk_shader_print(s, shd, AV_LOG_WARNING);
        av_log(s, AV_LOG_ERROR, "Unable to link shader: %s (%s)!\n",
               glslang_program_get_info_log(glslc_program),
               glslang_program_get_info_debug_log(glslc_program));
        glslang_program_delete(glslc_program);
        glslang_shader_delete(glslc_shader);
        return AVERROR(EINVAL);
    }

#if ((GLSLANG_VERSION_MAJOR) >= 12)
    glslang_program_SPIRV_generate_with_options(glslc_program, glslc_input.stage, &glslc_opts);
#else
    glslang_program_SPIRV_generate(glslc_program, glslc_input.stage);
#endif

    messages = glslang_program_SPIRV_get_messages(glslc_program);
    if (messages) {
        ff_vk_shader_print(s, shd, AV_LOG_WARNING);
        av_log(s, AV_LOG_WARNING, "%s\n", messages);
    } else {
        ff_vk_shader_print(s, shd, AV_LOG_VERBOSE);
    }

    glslang_shader_delete(glslc_shader);

    *size = glslang_program_SPIRV_get_size(glslc_program) * sizeof(unsigned int);
    *data = (void *)glslang_program_SPIRV_get_ptr(glslc_program);
    *opaque = glslc_program;

    return 0;
}

static void glslc_shader_free(FFVkSPIRVCompiler *ctx, void **opaque)
{
    if (!opaque || !*opaque)
        return;

    av_assert0(glslc_refcount);
    glslang_program_delete(*opaque);
    *opaque = NULL;
}

static void glslc_uninit(FFVkSPIRVCompiler **ctx)
{
    if (!ctx || !*ctx)
        return;

    pthread_mutex_lock(&glslc_mutex);
    if (glslc_refcount && (--glslc_refcount == 0))
        glslang_finalize_process();
    pthread_mutex_unlock(&glslc_mutex);

    av_freep(ctx);
}

FFVkSPIRVCompiler *ff_vk_glslang_init(void)
{
    FFVkSPIRVCompiler *ret = av_mallocz(sizeof(*ret));
    if (!ret)
        return NULL;

    ret->compile_shader = glslc_shader_compile;
    ret->free_shader    = glslc_shader_free;
    ret->uninit         = glslc_uninit;

    pthread_mutex_lock(&glslc_mutex);
    if (!glslc_refcount++) {
        if (!glslang_initialize_process()) {
            av_freep(&ret);
            glslc_refcount--;
        }
    }
    pthread_mutex_unlock(&glslc_mutex);

    return ret;
}
