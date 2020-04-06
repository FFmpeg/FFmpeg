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

#include "libavutil/opt.h"
#include "vulkan.h"
#include "scale_eval.h"
#include "internal.h"
#include "colorspace.h"

#define CGROUPS (int [3]){ 32, 32, 1 }

enum ScalerFunc {
    F_BILINEAR = 0,
    F_NEAREST,

    F_NB,
};

typedef struct ScaleVulkanContext {
    VulkanFilterContext vkctx;

    int initialized;
    FFVkExecContext *exec;
    VulkanPipeline *pl;
    FFVkBuffer params_buf;

    /* Shader updators, must be in the main filter struct */
    VkDescriptorImageInfo input_images[3];
    VkDescriptorImageInfo output_images[3];
    VkDescriptorBufferInfo params_desc;

    enum ScalerFunc scaler;
    char *out_format_string;
    enum AVColorRange out_range;
    char *w_expr;
    char *h_expr;
} ScaleVulkanContext;

static const char scale_bilinear[] = {
    C(0, vec4 scale_bilinear(int idx, ivec2 pos)                                )
    C(0, {                                                                      )
    C(1,     const vec2 npos = (vec2(pos) + 0.5f) / imageSize(output_img[idx]); )
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
    VkSampler *sampler;
    VkFilter sampler_mode;
    ScaleVulkanContext *s = ctx->priv;

    switch (s->scaler) {
    case F_NEAREST:
        sampler_mode = VK_FILTER_NEAREST;
        break;
    case F_BILINEAR:
        sampler_mode = VK_FILTER_LINEAR;
        break;
    };

    /* Create a sampler */
    sampler = ff_vk_init_sampler(ctx, 0, sampler_mode);
    if (!sampler)
        return AVERROR_EXTERNAL;

    s->pl = ff_vk_create_pipeline(ctx);
    if (!s->pl)
        return AVERROR(ENOMEM);

    { /* Create the shader */
        VulkanDescriptorSetBinding desc_i[2] = {
            {
                .name       = "input_img",
                .type       = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .dimensions = 2,
                .elems      = av_pix_fmt_count_planes(s->vkctx.input_format),
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .updater    = s->input_images,
                .samplers   = DUP_SAMPLER_ARRAY4(*sampler),
            },
            {
                .name       = "output_img",
                .type       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .mem_layout = ff_vk_shader_rep_fmt(s->vkctx.output_format),
                .mem_quali  = "writeonly",
                .dimensions = 2,
                .elems      = av_pix_fmt_count_planes(s->vkctx.output_format),
                .stages     = VK_SHADER_STAGE_COMPUTE_BIT,
                .updater    = s->output_images,
            },
        };

        VulkanDescriptorSetBinding desc_b = {
            .name        = "params",
            .type        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .mem_quali   = "readonly",
            .mem_layout  = "std430",
            .stages      = VK_SHADER_STAGE_COMPUTE_BIT,
            .updater     = &s->params_desc,
            .buf_content = "mat4 yuv_matrix;",
        };

        SPIRVShader *shd = ff_vk_init_shader(ctx, s->pl, "scale_compute",
                                             VK_SHADER_STAGE_COMPUTE_BIT);
        if (!shd)
            return AVERROR(ENOMEM);

        ff_vk_set_compute_shader_sizes(ctx, shd, CGROUPS);

        RET(ff_vk_add_descriptor_set(ctx, s->pl, shd,  desc_i, 2, 0)); /* set 0 */
        RET(ff_vk_add_descriptor_set(ctx, s->pl, shd, &desc_b, 1, 0)); /* set 0 */

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
        GLSLC(0,                                                                 );

        if (s->vkctx.output_format == s->vkctx.input_format) {
            for (int i = 0; i < desc_i[1].elems; i++) {
                GLSLF(1,  size = imageSize(output_img[%i]);                    ,i);
                GLSLC(1,  if (IS_WITHIN(pos, size)) {                            );
                switch (s->scaler) {
                case F_NEAREST:
                case F_BILINEAR:
                    GLSLF(2, vec4 res = scale_bilinear(%i, pos);               ,i);
                    GLSLF(2, imageStore(output_img[%i], pos, res);             ,i);
                    break;
                };
                GLSLC(1, }                                                       );
            }
        } else {
            GLSLC(1, vec4 res = scale_bilinear(0, pos);                          );
            GLSLF(1, res = rgb2yuv(res, %i);    ,s->out_range == AVCOL_RANGE_JPEG);
            switch (s->vkctx.output_format) {
            case AV_PIX_FMT_NV12:    GLSLC(1, write_nv12(res, pos); ); break;
            case AV_PIX_FMT_YUV420P: GLSLC(1,  write_420(res, pos); ); break;
            case AV_PIX_FMT_YUV444P: GLSLC(1,  write_444(res, pos); ); break;
            default: return AVERROR(EINVAL);
            }
        }

        GLSLC(0, }                                                               );

        RET(ff_vk_compile_shader(ctx, shd, "main"));
    }

    RET(ff_vk_init_pipeline_layout(ctx, s->pl));
    RET(ff_vk_init_compute_pipeline(ctx, s->pl));

    if (s->vkctx.output_format != s->vkctx.input_format) {
        const struct LumaCoefficients *lcoeffs;
        double tmp_mat[3][3];

        struct {
            float yuv_matrix[4][4];
        } *par;

        lcoeffs = ff_get_luma_coefficients(in->colorspace);
        if (!lcoeffs) {
            av_log(ctx, AV_LOG_ERROR, "Unsupported colorspace\n");
            return AVERROR(EINVAL);
        }

        err = ff_vk_create_buf(ctx, &s->params_buf,
                               sizeof(*par),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (err)
            return err;

        err = ff_vk_map_buffers(ctx, &s->params_buf, (uint8_t **)&par, 1, 0);
        if (err)
            return err;

        ff_fill_rgb2yuv_table(lcoeffs, tmp_mat);

        memset(par, 0, sizeof(*par));

        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 3; x++)
                par->yuv_matrix[x][y] = tmp_mat[x][y];

        par->yuv_matrix[3][3] = 1.0;

        err = ff_vk_unmap_buffers(ctx, &s->params_buf, 1, 1);
        if (err)
            return err;

        s->params_desc.buffer = s->params_buf.buf;
        s->params_desc.range  = VK_WHOLE_SIZE;

        ff_vk_update_descriptor_set(ctx, s->pl, 1);
    }

    /* Execution context */
    RET(ff_vk_create_exec_ctx(ctx, &s->exec,
                              s->vkctx.hwctx->queue_family_comp_index));

    s->initialized = 1;

    return 0;

fail:
    return err;
}

static int process_frames(AVFilterContext *avctx, AVFrame *out_f, AVFrame *in_f)
{
    int err = 0;
    ScaleVulkanContext *s = avctx->priv;
    AVVkFrame *in = (AVVkFrame *)in_f->data[0];
    AVVkFrame *out = (AVVkFrame *)out_f->data[0];
    VkImageMemoryBarrier barriers[AV_NUM_DATA_POINTERS*2];
    int barrier_count = 0;

    for (int i = 0; i < av_pix_fmt_count_planes(s->vkctx.input_format); i++) {
        RET(ff_vk_create_imageview(avctx, &s->input_images[i].imageView, in->img[i],
                                   av_vkfmt_from_pixfmt(s->vkctx.input_format)[i],
                                   ff_comp_identity_map));

        s->input_images[i].imageLayout  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    for (int i = 0; i < av_pix_fmt_count_planes(s->vkctx.output_format); i++) {
        RET(ff_vk_create_imageview(avctx, &s->output_images[i].imageView, out->img[i],
                                   av_vkfmt_from_pixfmt(s->vkctx.output_format)[i],
                                   ff_comp_identity_map));

        s->output_images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    ff_vk_update_descriptor_set(avctx, s->pl, 0);

    ff_vk_start_exec_recording(avctx, s->exec);

    for (int i = 0; i < av_pix_fmt_count_planes(s->vkctx.input_format); i++) {
        VkImageMemoryBarrier bar = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = in->layout[i],
            .newLayout = s->input_images[i].imageLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = in->img[i],
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.levelCount = 1,
            .subresourceRange.layerCount = 1,
        };

        memcpy(&barriers[barrier_count++], &bar, sizeof(VkImageMemoryBarrier));

        in->layout[i]  = bar.newLayout;
        in->access[i]  = bar.dstAccessMask;
    }

    for (int i = 0; i < av_pix_fmt_count_planes(s->vkctx.output_format); i++) {
        VkImageMemoryBarrier bar = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = out->layout[i],
            .newLayout = s->output_images[i].imageLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = out->img[i],
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.levelCount = 1,
            .subresourceRange.layerCount = 1,
        };

        memcpy(&barriers[barrier_count++], &bar, sizeof(VkImageMemoryBarrier));

        out->layout[i] = bar.newLayout;
        out->access[i] = bar.dstAccessMask;
    }

    vkCmdPipelineBarrier(s->exec->buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         0, NULL, 0, NULL, barrier_count, barriers);

    ff_vk_bind_pipeline_exec(avctx, s->exec, s->pl);

    vkCmdDispatch(s->exec->buf,
                  FFALIGN(s->vkctx.output_width,  CGROUPS[0])/CGROUPS[0],
                  FFALIGN(s->vkctx.output_height, CGROUPS[1])/CGROUPS[1], 1);

    ff_vk_add_exec_dep(avctx, s->exec, in_f, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    ff_vk_add_exec_dep(avctx, s->exec, out_f, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    err = ff_vk_submit_exec_queue(avctx, s->exec);
    if (err)
        return err;

    for (int i = 0; i < av_pix_fmt_count_planes(s->vkctx.input_format); i++)
        ff_vk_destroy_imageview(avctx, &s->input_images[i].imageView);
    for (int i = 0; i < av_pix_fmt_count_planes(s->vkctx.output_format); i++)
        ff_vk_destroy_imageview(avctx, &s->output_images[i].imageView);

fail:
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

    RET(process_frames(ctx, out, in));

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
    AVFilterLink *inlink   = outlink->src->inputs[0];

    err = ff_scale_eval_dimensions(s, s->w_expr, s->h_expr, inlink, outlink,
                                   &s->vkctx.output_width,
                                   &s->vkctx.output_height);
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

    err = ff_vk_filter_config_output(outlink);
    if (err < 0)
        return err;

    if (inlink->sample_aspect_ratio.num)
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink->w, outlink->w * inlink->h}, inlink->sample_aspect_ratio);
    else
        outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    return 0;
}

static void scale_vulkan_uninit(AVFilterContext *avctx)
{
    ScaleVulkanContext *s = avctx->priv;

    ff_vk_filter_uninit(avctx);
    ff_vk_free_buf(avctx, &s->params_buf);

    s->initialized = 0;
}

#define OFFSET(x) offsetof(ScaleVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption scale_vulkan_options[] = {
    { "w", "Output video width",  OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, .flags = FLAGS },
    { "h", "Output video height", OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, .flags = FLAGS },
    { "scaler", "Scaler function", OFFSET(scaler), AV_OPT_TYPE_INT, {.i64 = F_BILINEAR}, 0, F_NB, .flags = FLAGS, "scaler" },
        { "bilinear", "Bilinear interpolation (fastest)", 0, AV_OPT_TYPE_CONST, {.i64 = F_BILINEAR}, 0, 0, .flags = FLAGS, "scaler" },
        { "nearest", "Nearest (useful for pixel art)", 0, AV_OPT_TYPE_CONST, {.i64 = F_NEAREST}, 0, 0, .flags = FLAGS, "scaler" },
    { "format", "Output video format (software format of hardware frames)", OFFSET(out_format_string), AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "out_range", "Output colour range (from 0 to 2) (default 0)", OFFSET(out_range), AV_OPT_TYPE_INT, {.i64 = AVCOL_RANGE_UNSPECIFIED}, AVCOL_RANGE_UNSPECIFIED, AVCOL_RANGE_JPEG, .flags = FLAGS, "range" },
        { "full", "Full range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, "range" },
        { "limited", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, "range" },
        { "jpeg", "Full range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, "range" },
        { "mpeg", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, "range" },
        { "tv", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, "range" },
        { "pc", "Full range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, "range" },
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
    { NULL }
};

static const AVFilterPad scale_vulkan_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = &scale_vulkan_config_output,
    },
    { NULL }
};

AVFilter ff_vf_scale_vulkan = {
    .name           = "scale_vulkan",
    .description    = NULL_IF_CONFIG_SMALL("Scale Vulkan frames"),
    .priv_size      = sizeof(ScaleVulkanContext),
    .init           = &ff_vk_filter_init,
    .uninit         = &scale_vulkan_uninit,
    .query_formats  = &ff_vk_filter_query_formats,
    .inputs         = scale_vulkan_inputs,
    .outputs        = scale_vulkan_outputs,
    .priv_class     = &scale_vulkan_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
