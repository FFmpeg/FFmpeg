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

#include <shaderc/shaderc.h>

#include "libavutil/mem.h"
#include "vulkan_spirv.h"

static int shdc_shader_compile(FFVkSPIRVCompiler *ctx, void *avctx,
                               FFVkSPIRVShader *shd, uint8_t **data,
                               size_t *size, const char *entrypoint,
                               void **opaque)
{
    int loglevel, err, warn, ret;
    const char *status, *message;
    shaderc_compilation_result_t res;
    static const char *shdc_result[] = {
        [shaderc_compilation_status_success]            = "success",
        [shaderc_compilation_status_invalid_stage]      = "invalid stage",
        [shaderc_compilation_status_compilation_error]  = "error",
        [shaderc_compilation_status_internal_error]     = "internal error",
        [shaderc_compilation_status_null_result_object] = "no result",
        [shaderc_compilation_status_invalid_assembly]   = "invalid assembly",
    };
    static const shaderc_shader_kind shdc_kind[] = {
        [VK_SHADER_STAGE_VERTEX_BIT]   = shaderc_glsl_vertex_shader,
        [VK_SHADER_STAGE_FRAGMENT_BIT] = shaderc_glsl_fragment_shader,
        [VK_SHADER_STAGE_COMPUTE_BIT]  = shaderc_glsl_compute_shader,
    };

    shaderc_compile_options_t opts = shaderc_compile_options_initialize();
    *opaque = NULL;
    if (!opts)
        return AVERROR(ENOMEM);

    shaderc_compile_options_set_target_env(opts, shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_2);
    shaderc_compile_options_set_target_spirv(opts, shaderc_spirv_version_1_5);
    shaderc_compile_options_set_generate_debug_info(opts);
    shaderc_compile_options_set_optimization_level(opts,
                                                   shaderc_optimization_level_performance);

    res = shaderc_compile_into_spv((shaderc_compiler_t)ctx->priv,
                                   shd->src.str, strlen(shd->src.str),
                                   shdc_kind[shd->shader.stage],
                                   shd->name, entrypoint, opts);
    shaderc_compile_options_release(opts);

    ret = shaderc_result_get_compilation_status(res);
    err = shaderc_result_get_num_errors(res);
    warn = shaderc_result_get_num_warnings(res);
    message = shaderc_result_get_error_message(res);

    if (ret != shaderc_compilation_status_success && !err)
        err = 1;

    loglevel = err ? AV_LOG_ERROR : warn ? AV_LOG_WARNING : AV_LOG_VERBOSE;

    ff_vk_shader_print(avctx, shd, loglevel);
    if (message && (err || warn))
        av_log(avctx, loglevel, "%s\n", message);
    status = ret < FF_ARRAY_ELEMS(shdc_result) ? shdc_result[ret] : "unknown";
    av_log(avctx, loglevel, "shaderc compile status '%s' (%d errors, %d warnings)\n",
           status, err, warn);

    if (err > 0)
        return AVERROR(EINVAL);

    *data = (uint8_t *)shaderc_result_get_bytes(res);
    *size = shaderc_result_get_length(res);
    *opaque = res;

    return 0;
}

static void shdc_shader_free(FFVkSPIRVCompiler *ctx, void **opaque)
{
    if (!opaque || !*opaque)
        return;

    shaderc_result_release((shaderc_compilation_result_t)*opaque);
    *opaque = NULL;
}

static void shdc_uninit(FFVkSPIRVCompiler **ctx)
{
    FFVkSPIRVCompiler *s;

    if (!ctx || !*ctx)
        return;

    s = *ctx;

    shaderc_compiler_release((shaderc_compiler_t)s->priv);
    av_freep(ctx);
}

FFVkSPIRVCompiler *ff_vk_shaderc_init(void)
{
    FFVkSPIRVCompiler *ret = av_mallocz(sizeof(*ret));
    if (!ret)
        return NULL;

    ret->compile_shader = shdc_shader_compile;
    ret->free_shader    = shdc_shader_free;
    ret->uninit         = shdc_uninit;

    ret->priv           = (void *)shaderc_compiler_initialize();
    if (!ret->priv)
        av_freep(&ret);

    return ret;
}
