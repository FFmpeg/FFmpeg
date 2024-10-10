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
#include "libavutil/csp.h"
#include "libavutil/opt.h"
#include "libavutil/vulkan_spirv.h"
#include "vulkan_filter.h"
#include "filters.h"
#include "colorspace.h"
#include "video.h"

enum TestSrcVulkanMode {
    TESTSRC_COLOR,
};

typedef struct TestSrcVulkanPushData {
    float color_comp[4];
} TestSrcVulkanPushData;

typedef struct TestSrcVulkanContext {
    FFVulkanContext vkctx;

    int initialized;
    FFVkExecPool e;
    FFVkQueueFamilyCtx qf;
    FFVulkanShader shd;

    /* Only used by color_vulkan */
    uint8_t color_rgba[4];

    TestSrcVulkanPushData opts;

    int w, h;
    int pw, ph;
    char *out_format_string;
    enum AVColorRange out_range;
    unsigned int nb_frame;
    AVRational time_base, frame_rate;
    int64_t pts;
    int64_t duration;           ///< duration expressed in microseconds
    AVRational sar;             ///< sample aspect ratio
    int draw_once;              ///< draw only the first frame, always put out the same picture
    int draw_once_reset;        ///< draw only the first frame or in case of reset
    AVFrame *picref;            ///< cached reference containing the painted picture
} TestSrcVulkanContext;

static av_cold int init_filter(AVFilterContext *ctx, enum TestSrcVulkanMode mode)
{
    int err;
    uint8_t *spv_data;
    size_t spv_len;
    void *spv_opaque = NULL;
    TestSrcVulkanContext *s = ctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    const int planes = av_pix_fmt_count_planes(s->vkctx.output_format);
    FFVulkanShader *shd = &s->shd;
    FFVkSPIRVCompiler *spv;
    FFVulkanDescriptorSetBinding *desc_set;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->vkctx.output_format);

    spv = ff_vk_spirv_init();
    if (!spv) {
        av_log(ctx, AV_LOG_ERROR, "Unable to initialize SPIR-V compiler!\n");
        return AVERROR_EXTERNAL;
    }

    ff_vk_qf_init(vkctx, &s->qf, VK_QUEUE_COMPUTE_BIT);
    RET(ff_vk_exec_pool_init(vkctx, &s->qf, &s->e, s->qf.nb_queues*4, 0, 0, 0, NULL));
    RET(ff_vk_shader_init(vkctx, &s->shd, "scale",
                          VK_SHADER_STAGE_COMPUTE_BIT,
                          NULL, 0,
                          32, 32, 1,
                          0));

    GLSLC(0, layout(push_constant, std430) uniform pushConstants {        );
    GLSLC(1,    vec4 color_comp;                                          );
    GLSLC(0, };                                                           );
    GLSLC(0,                                                              );

    ff_vk_shader_add_push_const(&s->shd, 0, sizeof(s->opts),
                                VK_SHADER_STAGE_COMPUTE_BIT);

    desc_set = (FFVulkanDescriptorSetBinding []) {
        {
            .name       = "output_img",
            .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format, FF_VK_REP_FLOAT),
            .mem_quali  = "writeonly",
            .dimensions = 2,
            .elems      = planes,
            .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    };

    RET(ff_vk_shader_add_descriptor_set(vkctx, &s->shd, desc_set, 1, 0, 0));

    GLSLC(0, void main()                                                  );
    GLSLC(0, {                                                            );
    GLSLC(1,     ivec2 pos = ivec2(gl_GlobalInvocationID.xy);             );
    if (mode == TESTSRC_COLOR) {
        double rgb2yuv[3][3];
        double rgbad[4];
        double yuvad[4];

        enum AVColorSpace csp;
        const AVLumaCoefficients *luma = NULL;

        s->draw_once = 1;

        if (desc->flags & AV_PIX_FMT_FLAG_RGB)
            csp = AVCOL_SPC_RGB;
        else
            csp = AVCOL_SPC_SMPTE170M;

        if (!(desc->flags & AV_PIX_FMT_FLAG_RGB) && !(luma = av_csp_luma_coeffs_from_avcsp(csp)))
            return AVERROR(EINVAL);
        else if (!(desc->flags & AV_PIX_FMT_FLAG_RGB))
            ff_fill_rgb2yuv_table(luma, rgb2yuv);

        for (int i = 0; i < 4; i++)
            rgbad[i] = s->color_rgba[i] / 255.0;

        if (!(desc->flags & AV_PIX_FMT_FLAG_RGB))
            ff_matrix_mul_3x3_vec(yuvad, rgbad, rgb2yuv);
        else
            memcpy(yuvad, rgbad, sizeof(rgbad));

        yuvad[3] = rgbad[3];

        if (!(desc->flags & AV_PIX_FMT_FLAG_RGB)) {
            for (int i = 0; i < 3; i++) {
                int chroma = (!(desc->flags & AV_PIX_FMT_FLAG_RGB) && i > 0);
                if (s->out_range == AVCOL_RANGE_MPEG) {
                    yuvad[i] *= (chroma ? 224.0 : 219.0) / 255.0;
                    yuvad[i] += (chroma ? 128.0 :  16.0) / 255.0;
                } else if (chroma) {
                    yuvad[i] += 0.5;
                }
            }
        }

        /* Ensure we place the alpha appropriately for gray formats */
        if (desc->nb_components <= 2)
            yuvad[1] = yuvad[3];

        for (int i = 0; i < 4; i++)
            s->opts.color_comp[i] = yuvad[i];

        GLSLC(1,     vec4 r;                                                  );
        GLSLC(0,                                                              );
        for (int i = 0, c_off = 0; i < planes; i++) {
            for (int c = 0; c < desc->nb_components; c++) {
                if (desc->comp[c].plane == i) {
                    int off = desc->comp[c].offset / (FFALIGN(desc->comp[c].depth, 8)/8);
                    GLSLF(1, r[%i] = color_comp[%i];             ,off, c_off++);
                }
            }
            GLSLF(1, imageStore(output_img[%i], pos, r);                    ,i);
            GLSLC(0,                                                          );
        }
    }
    GLSLC(0, }                                                            );

    RET(spv->compile_shader(vkctx, spv, shd, &spv_data, &spv_len, "main",
                            &spv_opaque));
    RET(ff_vk_shader_link(vkctx, shd, spv_data, spv_len, "main"));

    RET(ff_vk_shader_register_exec(vkctx, &s->e, &s->shd));

    s->initialized = 1;

fail:
    if (spv_opaque)
        spv->free_shader(spv, &spv_opaque);
    if (spv)
        spv->uninit(&spv);

    return err;
}

static int testsrc_vulkan_activate(AVFilterContext *ctx)
{
    int err;
    AVFilterLink *outlink = ctx->outputs[0];
    TestSrcVulkanContext *s = ctx->priv;
    AVFrame *frame;

    if (!s->initialized) {
        enum TestSrcVulkanMode mode = TESTSRC_COLOR;
        err = init_filter(ctx, mode);
        if (err < 0)
            return err;
    }

    if (!ff_outlink_frame_wanted(outlink))
        return FFERROR_NOT_READY;
    if (s->duration >= 0 &&
        av_rescale_q(s->pts, s->time_base, AV_TIME_BASE_Q) >= s->duration) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (s->draw_once) {
        if (s->draw_once_reset) {
            av_frame_free(&s->picref);
            s->draw_once_reset = 0;
        }
        if (!s->picref) {
            s->picref = ff_get_video_buffer(outlink, s->w, s->h);
            if (!s->picref)
                return AVERROR(ENOMEM);

            err = ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->shd, s->picref, NULL,
                                              VK_NULL_HANDLE, &s->opts, sizeof(s->opts));
            if (err < 0)
                return err;
        }
        frame = av_frame_clone(s->picref);
    } else {
        frame = ff_get_video_buffer(outlink, s->w, s->h);
    }

    if (!frame)
        return AVERROR(ENOMEM);

    frame->pts                 = s->pts;
    frame->duration            = 1;
    frame->flags               = AV_FRAME_FLAG_KEY;
    frame->pict_type           = AV_PICTURE_TYPE_I;
    frame->sample_aspect_ratio = s->sar;
    if (!s->draw_once) {
        err = ff_vk_filter_process_simple(&s->vkctx, &s->e, &s->shd, frame, NULL,
                                          VK_NULL_HANDLE, &s->opts, sizeof(s->opts));
        if (err < 0) {
            av_frame_free(&frame);
            return err;
        }
    }

    s->pts++;
    s->nb_frame++;

    return ff_filter_frame(outlink, frame);
}

static int testsrc_vulkan_config_props(AVFilterLink *outlink)
{
    int err;
    FilterLink *l = ff_filter_link(outlink);
    TestSrcVulkanContext *s = outlink->src->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    if (!s->out_format_string) {
        vkctx->output_format = AV_PIX_FMT_YUV444P;
    } else {
        vkctx->output_format = av_get_pix_fmt(s->out_format_string);
        if (vkctx->output_format == AV_PIX_FMT_NONE) {
            av_log(vkctx, AV_LOG_ERROR, "Invalid output format.\n");
            return AVERROR(EINVAL);
        }
    }

    err = ff_vk_filter_init_context(outlink->src, vkctx, NULL,
                                    s->w, s->h, vkctx->output_format);
    if (err < 0)
        return err;

    l->hw_frames_ctx = av_buffer_ref(vkctx->frames_ref);
    if (!l->hw_frames_ctx)
        return AVERROR(ENOMEM);

    s->time_base = av_inv_q(s->frame_rate);
    s->nb_frame = 0;
    s->pts = 0;

    s->vkctx.output_width = s->w;
    s->vkctx.output_height = s->h;
    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = s->sar;
    l->frame_rate = s->frame_rate;
    outlink->time_base  = s->time_base;

    return 0;
}

static void testsrc_vulkan_uninit(AVFilterContext *avctx)
{
    TestSrcVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    av_frame_free(&s->picref);

    ff_vk_exec_pool_free(vkctx, &s->e);
    ff_vk_shader_free(vkctx, &s->shd);

    ff_vk_uninit(&s->vkctx);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(TestSrcVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

#define COMMON_OPTS                                                                                                                           \
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, { .str = "1920x1080" }, 0, 0, FLAGS },                                     \
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, { .str = "1920x1080" }, 0, 0, FLAGS },                                     \
                                                                                                                                              \
    { "rate", "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, { .str = "60" }, 0, INT_MAX, FLAGS },                             \
    { "r",    "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, { .str = "60" }, 0, INT_MAX, FLAGS },                             \
                                                                                                                                              \
    { "duration", "set video duration", OFFSET(duration), AV_OPT_TYPE_DURATION, { .i64 = -1 }, -1, INT64_MAX, FLAGS },                        \
    { "d",        "set video duration", OFFSET(duration), AV_OPT_TYPE_DURATION, { .i64 = -1 }, -1, INT64_MAX, FLAGS },                        \
                                                                                                                                              \
    { "sar", "set video sample aspect ratio", OFFSET(sar), AV_OPT_TYPE_RATIONAL, { .dbl = 1 },  0, INT_MAX, FLAGS },                          \
                                                                                                                                              \
    { "format", "Output video format (software format of hardware frames)", OFFSET(out_format_string), AV_OPT_TYPE_STRING, .flags = FLAGS },

static const AVOption color_vulkan_options[] = {
    { "color", "set color", OFFSET(color_rgba), AV_OPT_TYPE_COLOR, {.str = "black"}, 0, 0, FLAGS },
    { "c",     "set color", OFFSET(color_rgba), AV_OPT_TYPE_COLOR, {.str = "black"}, 0, 0, FLAGS },
    COMMON_OPTS
    { "out_range", "Output colour range (from 0 to 2) (default 0)", OFFSET(out_range), AV_OPT_TYPE_INT, {.i64 = AVCOL_RANGE_UNSPECIFIED}, AVCOL_RANGE_UNSPECIFIED, AVCOL_RANGE_JPEG, .flags = FLAGS, .unit = "range" },
        { "full", "Full range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
        { "limited", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
        { "jpeg", "Full range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
        { "mpeg", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
        { "tv", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
        { "pc", "Full range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(color_vulkan);

static const AVFilterPad testsrc_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = testsrc_vulkan_config_props,
    },
};

const AVFilter ff_vsrc_color_vulkan = {
    .name           = "color_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Generate a constant color (Vulkan)"),
    .priv_size      = sizeof(TestSrcVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &testsrc_vulkan_uninit,
    .inputs         = NULL,
    .flags          = AVFILTER_FLAG_HWDEVICE,
    .activate       = testsrc_vulkan_activate,
    FILTER_OUTPUTS(testsrc_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &color_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
