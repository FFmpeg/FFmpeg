/*
 * Copyright (C) 2026 Philip Langdale <philipl@overt.org>
 *
 * Based on vf_framerate - Copyright (C) 2012 Mark Himsley
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

/**
 * @file
 * Frame rate up-conversion filter that synthesises intermediate frames using
 * the NVIDIA Vulkan optical flow extension (VK_NV_optical_flow).
 */

#include "libavutil/avassert.h"
#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "vulkan_filter.h"
#include "filters.h"
#include "video.h"

static const char *const var_names[] = {
    "source_fps",
    NULL
};

enum var_name {
    VAR_SOURCE_FPS,
    VARS_NB
};

typedef struct FRUCVulkanContext {
    FFVulkanContext vkctx;

    AVVulkanDeviceQueueFamily *qf;      ///< compute queue family

    FFVkExecPool e;                     ///< compute execution pool

    int width;          ///< luma width
    int height;         ///< luma height

    // parameters
    char       *requested_frame_rate;   ///< output fps as an expression
    AVRational dest_frame_rate;         ///< output frames per second

    AVRational srce_time_base;          ///< timebase of source
    AVRational dest_time_base;          ///< timebase of destination

    AVFrame *work;

    AVFrame *f0;                        ///< last frame
    AVFrame *f1;                        ///< current frame
    int64_t pts0;                       ///< last frame pts in dest_time_base
    int64_t pts1;                       ///< current frame pts in dest_time_base
    int64_t delta;                      ///< pts1 to pts0 delta
    int flush;                          ///< 1 if the filter is being flushed
    int64_t start_pts;                  ///< pts of the first output frame
    int64_t n;                          ///< output frame counter
} FRUCVulkanContext;

#define OFFSET(x) offsetof(FRUCVulkanContext, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption fruc_vulkan_options[] = {
    { "fps", "A string describing the desired output frame rate",
      OFFSET(requested_frame_rate), AV_OPT_TYPE_STRING, { .str = "60" }, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fruc_vulkan);

static av_cold int init_filter(AVFilterContext *avctx)
{
    int err;
    FRUCVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    s->width  = vkctx->output_width;
    s->height = vkctx->output_height;

    s->qf = ff_vk_qf_find(vkctx, VK_QUEUE_COMPUTE_BIT, 0);
    if (!s->qf) {
        av_log(avctx, AV_LOG_ERROR, "Device has no compute queues\n");
        return AVERROR(ENOTSUP);
    }

    RET(ff_vk_exec_pool_init(vkctx, s->qf, &s->e, FF_VK_DEFAULT_EXEC_CONTEXTS, 0, 0, 0, NULL));

    return 0;

fail:
    return err;
}

/* Visible texel extent of a plane: plane 0 (and non-planar / alpha) is full size,
 * chroma planes are subsampled. Mirrors hwcontext_vulkan's get_plane_wh, which is
 * how the frames context sizes each plane's image. */
static void plane_wh(const AVPixFmtDescriptor *desc, int width, int height,
                     int plane, uint32_t *w, uint32_t *h)
{
    int sub = plane && plane != 3 && (desc->flags & AV_PIX_FMT_FLAG_PLANAR) &&
              !(desc->flags & AV_PIX_FMT_FLAG_RGB);

    *w = sub ? AV_CEIL_RSHIFT(width,  desc->log2_chroma_w) : width;
    *h = sub ? AV_CEIL_RSHIFT(height, desc->log2_chroma_h) : height;
}

static int copy_frame(AVFilterContext *avctx, AVFrame *out, AVFrame *src)
{
    int err;
    FRUCVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;
    FFVulkanFunctions *vk = &vkctx->vkfn;
    FFVkExecContext *exec;
    AVVkFrame *src_vk = (AVVkFrame *)src->data[0];
    AVVkFrame *out_vk = (AVVkFrame *)out->data[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(vkctx->output_format);
    const int nb_planes = av_pix_fmt_count_planes(vkctx->output_format);
    const int src_nb_images = ff_vk_count_images(src_vk);
    const int out_nb_images = ff_vk_count_images(out_vk);
    /* src and out each contribute one barrier per VkImage; a multi-image
     * sw_format such as planar RGB or a separate alpha plane has several. */
    VkImageMemoryBarrier2 img_bar[2 * AV_NUM_DATA_POINTERS];
    int nb_img_bar = 0;

    exec = ff_vk_exec_get(vkctx, &s->e);
    err = ff_vk_exec_start(vkctx, exec);
    if (err < 0)
        return err;

    RET(ff_vk_exec_add_dep_frame(vkctx, exec, src,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
    RET(ff_vk_exec_add_dep_frame(vkctx, exec, out,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));

    ff_vk_frame_barrier(vkctx, exec, src, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED);
    ff_vk_frame_barrier(vkctx, exec, out, img_bar, &nb_img_bar,
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_QUEUE_FAMILY_IGNORED);

    vk->CmdPipelineBarrier2(exec->buf, &(VkDependencyInfo) {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pImageMemoryBarriers    = img_bar,
        .imageMemoryBarrierCount = nb_img_bar,
    });

    for (int i = 0; i < nb_planes; i++) {
        uint32_t w, h;
        plane_wh(desc, s->width, s->height, i, &w, &h);
        VkImageCopy region = {
            .srcSubresource = { .aspectMask = ff_vk_aspect_flag(src, i), .layerCount = 1 },
            .dstSubresource = { .aspectMask = ff_vk_aspect_flag(out, i), .layerCount = 1 },
            .extent         = { w, h, 1 },
        };
        vk->CmdCopyImage(exec->buf,
                         src_vk->img[FFMIN(i, src_nb_images - 1)],
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         out_vk->img[FFMIN(i, out_nb_images - 1)],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &region);
    }

    return ff_vk_exec_submit(vkctx, exec);

fail:
    ff_vk_exec_discard(vkctx, exec);
    return err;
}

/* Emit src unchanged. The frame must be copied rather than cloned: a clone keeps
 * the input's frames context, and the frame has to match the output context the
 * downstream link is configured for. */
static int passthrough_frame(AVFilterContext *ctx, AVFrame **work, AVFrame *src)
{
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    *work = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!*work)
        return AVERROR(ENOMEM);
    ret = av_frame_copy_props(*work, src);
    if (ret < 0)
        goto fail;
    ret = copy_frame(ctx, *work, src);
    if (ret < 0)
        goto fail;
    return 0;
fail:
    av_frame_free(work);
    return ret;
}

static int process_work_frame(AVFilterContext *ctx)
{
    FRUCVulkanContext *s = ctx->priv;
    int64_t work_pts;
    int64_t interpolate8;
    int ret;

    if (!s->f1)
        return 0;
    if (!s->f0 && !s->flush)
        return 0;

    work_pts = s->start_pts + av_rescale_q(s->n, av_inv_q(s->dest_frame_rate),
                                           s->dest_time_base);

    if (work_pts >= s->pts1 && !s->flush)
        return 0;

    if (!s->f0) {
        av_assert1(s->flush);
        ret = passthrough_frame(ctx, &s->work, s->f1);
        if (ret < 0)
            return ret;
        /* We are flushing, so f1 is not needed once passed through. Free it so
         * the flush terminates instead of re-emitting it every activation. */
        av_frame_free(&s->f1);
    } else {
        if (work_pts >= s->pts1 + s->delta && s->flush)
            return 0;

        /* No motion compensation yet: emit the temporally nearest source
         * frame. Genuine interpolation replaces this in a later change. */
        interpolate8 = av_rescale(work_pts - s->pts0, 256, s->delta);
        ret = passthrough_frame(ctx, &s->work,
                                interpolate8 >= 128 ? s->f1 : s->f0);
        if (ret < 0)
            return ret;
    }

    s->work->pts = work_pts;
    s->n++;

    return 1;
}

static int activate(AVFilterContext *ctx)
{
    int ret, status;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    FRUCVulkanContext *s = ctx->priv;
    AVFrame *inpicref;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

retry:
    ret = process_work_frame(ctx);
    if (ret < 0)
        return ret;
    else if (ret == 1)
        return ff_filter_frame(outlink, s->work);

    ret = ff_inlink_consume_frame(inlink, &inpicref);
    if (ret < 0)
        return ret;

    if (inpicref) {
        if (inpicref->flags & AV_FRAME_FLAG_INTERLACED)
            av_log(ctx, AV_LOG_WARNING, "Interlaced frame found - the output will not be correct.\n");

        if (inpicref->pts == AV_NOPTS_VALUE) {
            av_log(ctx, AV_LOG_WARNING, "Ignoring frame without PTS.\n");
            av_frame_free(&inpicref);
        }
    }

    if (inpicref) {
        pts = av_rescale_q(inpicref->pts, s->srce_time_base, s->dest_time_base);

        if (s->f1 && pts == s->pts1) {
            av_log(ctx, AV_LOG_WARNING, "Ignoring frame with same PTS.\n");
            av_frame_free(&inpicref);
        }
    }

    if (inpicref) {
        av_frame_free(&s->f0);
        s->f0 = s->f1;
        s->pts0 = s->pts1;
        s->f1 = inpicref;
        s->pts1 = pts;
        s->delta = s->pts1 - s->pts0;

        if (s->delta < 0) {
            av_log(ctx, AV_LOG_WARNING, "PTS discontinuity.\n");
            s->start_pts = s->pts1;
            s->n = 0;
            av_frame_free(&s->f0);
        }

        if (s->start_pts == AV_NOPTS_VALUE)
            s->start_pts = s->pts1;

        goto retry;
    }

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (!s->flush) {
            s->flush = 1;
            goto retry;
        }
        ff_outlink_set_status(outlink, status, pts);
        return 0;
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FRUCVulkanContext *s = ctx->priv;

    s->srce_time_base = inlink->time_base;

    return ff_vk_filter_config_input(inlink);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *il = ff_filter_link(inlink);
    FilterLink *ol = ff_filter_link(outlink);
    FRUCVulkanContext *s = ctx->priv;
    double var_values[VARS_NB], res;
    int err;
    int exact;

    ff_dlog(ctx, "config_output()\n");

    ff_dlog(ctx,
           "config_output() input time base:%u/%u (%f)\n",
           ctx->inputs[0]->time_base.num,ctx->inputs[0]->time_base.den,
           av_q2d(ctx->inputs[0]->time_base));

    // The fps option is an expression evaluated against the source frame rate
    var_values[VAR_SOURCE_FPS]    = av_q2d(il->frame_rate);
    err = av_expr_parse_and_eval(&res, s->requested_frame_rate,
                                 var_names, var_values,
                                 NULL, NULL, NULL, NULL, NULL, 0, ctx);
    if (err < 0)
        return err;

    s->dest_frame_rate = av_d2q(res, INT_MAX);
    if (s->dest_frame_rate.num <= 0 || s->dest_frame_rate.den <= 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid output frame rate '%s' (must evaluate to a positive value)\n",
               s->requested_frame_rate);
        return AVERROR(EINVAL);
    }

    // make sure timebase is small enough to hold the framerate

    exact = av_reduce(&s->dest_time_base.num, &s->dest_time_base.den,
                      av_gcd((int64_t)s->srce_time_base.num * s->dest_frame_rate.num,
                             (int64_t)s->srce_time_base.den * s->dest_frame_rate.den ),
                      (int64_t)s->srce_time_base.den * s->dest_frame_rate.num, INT_MAX);

    /* The source-timebase-derived reduction above can collapse to a zero time
     * base (av_reduce() bounds its result, so the numerator can underflow to
     * zero, leaving 0/1) when the source timebase shares no useful factors with
     * the requested rate, which would make the output timebase unusable. Fall
     * back to the plain 1/fps timebase in that case so a valid timebase is
     * always produced. */
    if (!s->dest_time_base.num || !s->dest_time_base.den) {
        exact = av_reduce(&s->dest_time_base.num, &s->dest_time_base.den,
                          s->dest_frame_rate.den, s->dest_frame_rate.num, INT_MAX);
    }

    av_log(ctx, AV_LOG_INFO,
           "time base:%u/%u -> %u/%u exact:%d\n",
           s->srce_time_base.num, s->srce_time_base.den,
           s->dest_time_base.num, s->dest_time_base.den, exact);
    if (!exact) {
        av_log(ctx, AV_LOG_WARNING, "Timebase conversion is not exact\n");
    }

    err = ff_vk_filter_config_output(outlink);
    if (err < 0)
        return err;

    ol->frame_rate = s->dest_frame_rate;
    outlink->time_base = s->dest_time_base;

    ff_dlog(ctx,
           "config_output() output time base:%u/%u (%f) w:%d h:%d\n",
           outlink->time_base.num, outlink->time_base.den,
           av_q2d(outlink->time_base),
           outlink->w, outlink->h);

    return init_filter(ctx);
}

static av_cold int init(AVFilterContext *avctx)
{
    FRUCVulkanContext *s = avctx->priv;

    s->start_pts = AV_NOPTS_VALUE;

    return ff_vk_filter_init(avctx);
}

static av_cold void uninit(AVFilterContext *avctx)
{
    FRUCVulkanContext *s = avctx->priv;
    FFVulkanContext *vkctx = &s->vkctx;

    ff_vk_exec_pool_free(vkctx, &s->e);

    ff_vk_uninit(vkctx);

    av_frame_free(&s->f0);
    av_frame_free(&s->f1);
}

static const AVFilterPad fruc_vulkan_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
};

static const AVFilterPad fruc_vulkan_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const FFFilter ff_vf_fruc_vulkan = {
    .p.name        = "fruc_vulkan",
    .p.description = NULL_IF_CONFIG_SMALL("Frame rate up-conversion using the Vulkan NV optical flow extension"),
    .p.priv_class  = &fruc_vulkan_class,
    .p.flags       = AVFILTER_FLAG_HWDEVICE,
    .priv_size     = sizeof(FRUCVulkanContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(fruc_vulkan_inputs),
    FILTER_OUTPUTS(fruc_vulkan_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .activate      = activate,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
