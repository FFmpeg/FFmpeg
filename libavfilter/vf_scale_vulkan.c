/*
 * Copyright (c) Lynne
 *
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

#include "libavutil/random_seed.h"
#include "libavutil/opt.h"
#include "vulkan_filter.h"
#include "vulkan_spirv.h"
#include "scale_eval.h"
#include "internal.h"
#include "colorspace.h"
#include "video.h"

enum ScalerFunc {
    F_BILINEAR = 0,
    F_NEAREST,

    F_NB,
};

typedef struct ScaleVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVulkanPipeline pl;
    FFVkExecPool e;
    FFVkQueueFamilyCtx qf;
    FFVkSPIRVShader shd;
    VkSampler sampler;

    /* Push constants / options */
    struct {
        float yuv_matrix[4][4];
    } opts;

    char *out_format_string;
    char *w_expr;
    char *h_expr;

    enum ScalerFunc scaler;
    enum AVColorRange out_range;
} ScaleVulkanContext;

static const char scale_bilinear[] = {
    C(0, vec4 scale_bilinear(int idx, ivec2 pos, vec2 crop_range, vec2 crop_off))
    C(0, {                                                                      )
    C(1,     vec2 npos = (vec2(pos) + 0.5f) / imageSize(output_img[idx]);       )
    C(1,     npos *= crop_range;    /* Reduce the range */                      )
    C(1,     npos += crop_off;      /* Offset the start */                      )
    C(1,     return texture(input_img[idx], npos);                              )
    C(0, }                                                                      )
};

static const char rgb2yuv[] = {
    C(0, vec4 rgb2yuv(vec4 src, int fullrange)                                  )
    C(0, {                                                                      )
    C(1,     src *= yuv_matrix;                                                 )
    C(1,     if (fullrange == 1) {                                              )
    C(2,         src += vec4(0.0, 0.5, 0.5, 0.0);                               )
    C(1,     } else {                                                           )
    C(2,         src *= vec4(219.0 / 255.0, 224.0 / 255.0, 224.0 / 255.0, 1.0); )
    C(2,         src += vec4(16.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0, 0.0);  )
    C(1,     }                                                                  )
    C(1,     return src;                                                        )
    C(0, }                                                                      )
};

static const char write_nv12[] = {
    C(0, void write_nv12(vec4 src, ivec2 pos)                                   )
    C(0, {                                                                      )
    C(1,     imageStore(output_img[0], pos, vec4(src.r, 0.0, 0.0, 0.0));        )
    C(1,     pos /= ivec2(2);                                                   )
    C(1,     imageStore(output_img[1], pos, vec4(src.g, src.b, 0.0, 0.0));      )
    C(0, }                                                                      )
};

static const char write_420[] = {
    C(0, void write_420(vec4 src, ivec2 pos)                                    )
    C(0, {                                                                      )
    C(1,     imageStore(output_img[0], pos, vec4(src.r, 0.0, 0.0, 0.0));        )
    C(1,     pos /= ivec2(2);                                                   )
    C(1,     imageStore(output_img[1], pos, vec4(src.g, 0.0, 0.0, 0.0));        )
    C(1,     imageStore(output_img[2], pos, vec4(src.b, 0.0, 0.0, 0.0));        )
    C(0, }                                                                      )
};

static const char write_444[] = {
    C(0, void write_444(vec4 src, ivec2 pos)                                    )
    C(0, {                                                                      )
    C(1,     imageStore(output_img[0], pos, vec4(src.r, 0.0, 0.0, 0.0));        )
    C(1,     imageStore(output_img[1], pos, vec4(src.g, 0.0, 0.0, 0.0));        )
    C(1,     imageStore(output_img[2], pos, vec4(src.b, 0.0, 0.0, 0.0));        )
    C(0, }                                                                      )
};

static av_cold int init_filter(AVFilterContext *ctx, AVFrame *in)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    VkFilter sampler_mode;
    ScaleVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVkSPIRVShader *shd = &s->shd;
    FFVkSPIRVCompiler *spv;
    FFVulkanDescriptorSetBinding *desc;

    int crop_x = in->crop_left;
    int crop_y = in->crop_top;
    int crop_w = in->width - (in->crop_left + in->crop_right);
    int crop_h = in->height - (in->crop_top + in->crop_bottom);
    int in_planes = av_pix_fmt_count_planes(s->vkctx.input_format);

    switch (s->scaler) {
    case F_NEAREST:
        sampler_mode = VK_FILTER_NEAREST;
        break;
    case F_BILINEAR:
        sampler_mode = VK_FILTER_LINEAR;
        break;
    };

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(ctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    ff_vk_qf_init(vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT);
    RET(ff_vk_exec_pool_init(vkctx, &s->qf, &s->e, s->qf.nb_queues*4, 0, 0, 0, NULL));
    RET(ff_vk_init_sampler(vkctx, &s->sampler, 0, sampler_mode));
    RET(ff_vk_shader_init(&s->pl, &s->shd, "scale_compute",
                          VK_SHADER_STAGE_COMPUTE_BIT, 0));

    ff_vk_shader_set_compute_sizes(&s->shd, 32, 32, 1);

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
    GLSLC(1,    mat4 yuv_matrix;                                          );
    GLSLC(0, };                                                           );
    GLSLC(0,                                                              );

    ff_vk_add_push_constant(&s->pl, 0, sizeof(s->opts),
                            VK_SHADER_STAGE_COMPUTE_BIT);

    desc = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "input_img",
            .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .dimensions = 2,
            .elems      = in_planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
            .samplers   = DUP_SAMPLER(s->sampler),
        },
        {
            .name       = "output_img",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = av_pix_fmt_count_planes(s->vkctx.output_format),
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    RET(ff_vk_pipeline_descriptor_set_add(vkctx, &s->pl, shd, desc, 2, 0, 0));

    GLSLD(   scale_bilinear                                                  );

    if (s->vkctx.output_format != s->vkctx.input_format) {
        GLSLD(   rgb2yuv                                                     );
    }

    switch (s->vkctx.output_format) {
    case AV_PIX_FMT_NV12:    GLSLD(write_nv12); break;
    case AV_PIX_FMT_YUV420P: GLSLD( write_420); break;
    case AV_PIX_FMT_YUV444P: GLSLD( write_444); break;
    default: break;
    }

    GLSLC(0, void main()                                                     );
    GLSLC(0, {                                                               );
    GLSLC(1,     ivec2 size;                                                 );
    GLSLC(1,     ivec2 pos = ivec2(gl_GlobalInvocationID.xy);                );
    GLSLF(1,     vec2 in_d = vec2(%i, %i);             ,in->width, in->height);
    GLSLF(1,     vec2 c_r = vec2(%i, %i) / in_d;              ,crop_w, crop_h);
    GLSLF(1,     vec2 c_o = vec2(%i, %i) / in_d;               ,crop_x,crop_y);
    GLSLC(0,                                                                 );

    if (s->vkctx.output_format == s->vkctx.input_format) {
        for (int i = 0; i < desc[1].elems; i++) {
            GLSLF(1,  size = imageSize(output_img[%i]);                    ,i);
            GLSLC(1,  if (IS_WITHIN(pos, size)) {                            );
            switch (s->scaler) {
            case F_NEAREST:
            case F_BILINEAR:
                GLSLF(2, vec4 res = scale_bilinear(%i, pos, c_r, c_o);     ,i);
                GLSLF(2, imageStore(output_img[%i], pos, res);             ,i);
                break;
            };
            GLSLC(1, }                                                       );
        }
    } else {
        GLSLC(1, vec4 res = scale_bilinear(0, pos, c_r, c_o);                );
        GLSLF(1, res = rgb2yuv(res, %i);    ,s->out_range == AVCOL_RANGE_JPEG);
        switch (s->vkctx.output_format) {
        case AV_PIX_FMT_NV12:    GLSLC(1, write_nv12(res, pos); ); break;
        case AV_PIX_FMT_YUV420P: GLSLC(1,  write_420(res, pos); ); break;
        case AV_PIX_FMT_YUV444P: GLSLC(1,  write_444(res, pos); ); break;
        default: return AVERROR(EINVAL);
        }
    }

    GLSLC(0, }                                                               );

    if (s->vkctx.output_format != s->vkctx.input_format) {
        const AVLumaCoefficients *lcoeffs;
        double tmp_mat[3][3];

        lcoeffs = av_csp_luma_coeffs_from_avcsp(in->colorspace);
        if (!lcoeffs) {
            av_log(ctx, AV_LOG_ERROR, "Unsupported colorspace\n");
            return AVERROR(EINVAL);
        }

        ff_fill_rgb2yuv_table(lcoeffs, tmp_mat);

        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 3; x++)
                s->opts.yuv_matrix[x][y] = tmp_mat[x][y];
        s->opts.yuv_matrix[3][3] = 1.0;
    }

    RET(spv->compile_shader(spv, ctx, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_create(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_init_compute_pipeline(vkctx, &s->pl, shd));
    RET(ff_vk_exec_pipeline_register(vkctx, &s->e, &s->pl));

    s->initialized = 1;

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);
    if (spv)
        spv->uninit(&spv);

    return err;
}

static int scale_vulkan_filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err;
    AVFilterContext *ctx = link->dst;
    ScaleVulkanContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->initialized)
        RET(init_filter(ctx, in));

    RET(ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->pl, out, in,
                                    s->sampler, &s->opts, sizeof(s->opts)));

    err = av_frame_copy_props(out, in);
    if (err < 0)
        goto fail;

    if (s->out_range != AVCOL_RANGE_UNSPECIFIED)
        out->color_range = s->out_range;
    if (s->vkctx.output_format != s->vkctx.input_format)
        out->chroma_location = AVCHROMA_LOC_TOPLEFT;

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return err;
}

static int scale_vulkan_config_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    ScaleVulkanContext *s  = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    AVFilterLink *inlink   = outlink->src->inputs[0];

    err = ff_scale_eval_dimensions(s, s->w_expr, s->h_expr, inlink, outlink,
                                   &vkctx->output_width,
                                   &vkctx->output_height);
    if (err < 0)
        return err;

    if (s->out_format_string) {
        s->vkctx.output_format = av_get_pix_fmt(s->out_format_string);
        if (s->vkctx.output_format == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Invalid output format.\n");
            return AVERROR(EINVAL);
        }
    } else {
        s->vkctx.output_format = s->vkctx.input_format;
    }

    if (s->vkctx.output_format != s->vkctx.input_format) {
        if (!ff_vk_mt_is_np_rgb(s->vkctx.input_format)) {
            av_log(avctx, AV_LOG_ERROR, "Unsupported input format for conversion\n");
            return AVERROR(EINVAL);
        }
        if (s->vkctx.output_format != AV_PIX_FMT_NV12 &&
            s->vkctx.output_format != AV_PIX_FMT_YUV420P &&
            s->vkctx.output_format != AV_PIX_FMT_YUV444P) {
            av_log(avctx, AV_LOG_ERROR, "Unsupported output format\n");
            return AVERROR(EINVAL);
        }
    } else if (s->out_range != AVCOL_RANGE_UNSPECIFIED) {
        av_log(avctx, AV_LOG_ERROR, "Cannot change range without converting format\n");
        return AVERROR(EINVAL);
    }

    return ff_vk_filter_config_output(outlink);
}

static void scale_vulkan_uninit(AVFilterContext *avctx)
{
    ScaleVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_pipeline_free(vkctx, &s->pl);
    ff_vk_shader_free(vkctx, &s->shd);

    if (s->sampler)
        vk->DestroySampler(vkctx->hwctx->act_dev, s->sampler,
                           vkctx->hwctx->alloc);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(ScaleVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption scale_vulkan_options[] = {
    { "w", "Output video width",  OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, .flags = FLAGS },
    { "h", "Output video height", OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, .flags = FLAGS },
    { "scaler", "Scaler function", OFFSET(scaler), AV_OPT_TYPE_INT, {.i64 = F_BILINEAR}, 0, F_NB, .flags = FLAGS, .unit = "scaler" },
        { "bilinear", "Bilinear interpolation (fastest)", 0, AV_OPT_TYPE_CONST, {.i64 = F_BILINEAR}, 0, 0, .flags = FLAGS, .unit = "scaler" },
        { "nearest", "Nearest (useful for pixel art)", 0, AV_OPT_TYPE_CONST, {.i64 = F_NEAREST}, 0, 0, .flags = FLAGS, .unit = "scaler" },
    { "format", "Output video format (software format of hardware frames)", OFFSET(out_format_string), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "out_range", "Output colour range (from 0 to 2) (default 0)", OFFSET(out_range), AV_OPT_TYPE_INT, {.i64 = AVCOL_RANGE_UNSPECIFIED}, AVCOL_RANGE_UNSPECIFIED, AVCOL_RANGE_JPEG, .flags = FLAGS, .unit = "range" },
        { "full", "Full range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
        { "limited", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
        { "jpeg", "Full range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
        { "mpeg", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
        { "tv", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
        { "pc", "Full range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(scale_vulkan);

static const AVFilterPad scale_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &scale_vulkan_filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad scale_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &scale_vulkan_config_output,
    },
};

const AVFilter ff_vf_scale_vulkan = {
    .name           = "scale_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Scale Vulkan frames"),
    .priv_size      = sizeof(ScaleVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &scale_vulkan_uninit,
    FILTER_INPUTS(scale_vulkan_inputs),
    FILTER_OUTPUTS(scale_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &scale_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .flags          = AVFILTER_FLAG_HWDEVICE,
};
