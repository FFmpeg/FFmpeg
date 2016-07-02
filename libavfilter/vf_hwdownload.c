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

#include "libavutil/buffer.h"
#include "libavutil/hwcontext.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct HWDownloadContext {
    const AVClass *class;

    AVBufferRef       *hwframes_ref;
    AVHWFramesContext *hwframes;
} HWDownloadContext;

static int hwdownload_query_formats(AVFilterContext *avctx)
{
    AVFilterFormats  *infmts = NULL;
    AVFilterFormats *outfmts = NULL;
    const AVPixFmtDescriptor *desc;
    int err;

    for (desc = av_pix_fmt_desc_next(NULL); desc;
         desc = av_pix_fmt_desc_next(desc)) {
        if (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
            err = ff_add_format(&infmts,  av_pix_fmt_desc_get_id(desc));
        else
            err = ff_add_format(&outfmts, av_pix_fmt_desc_get_id(desc));
        if (err) {
            ff_formats_unref(&infmts);
            ff_formats_unref(&outfmts);
            return err;
        }
    }

    if ((err = ff_formats_ref(infmts,  &avctx->inputs[0]->out_formats)) < 0 ||
        (err = ff_formats_ref(outfmts, &avctx->outputs[0]->in_formats)) < 0)
        return err;

    return 0;
}

static int hwdownload_config_input(AVFilterLink *inlink)
{
    AVFilterContext *avctx = inlink->dst;
    HWDownloadContext *ctx = avctx->priv;

    av_buffer_unref(&ctx->hwframes_ref);

    if (!inlink->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "The input must have a hardware frame "
               "reference.\n");
        return AVERROR(EINVAL);
    }

    ctx->hwframes_ref = av_buffer_ref(inlink->hw_frames_ctx);
    if (!ctx->hwframes_ref)
        return AVERROR(ENOMEM);

    ctx->hwframes = (AVHWFramesContext*)ctx->hwframes_ref->data;

    return 0;
}

static int hwdownload_config_output(AVFilterLink *outlink)
{
    AVFilterContext *avctx = outlink->src;
    AVFilterLink *inlink   = avctx->inputs[0];
    HWDownloadContext *ctx = avctx->priv;
    enum AVPixelFormat *formats;
    int err, i, found;

    if (!ctx->hwframes_ref)
        return AVERROR(EINVAL);

    err = av_hwframe_transfer_get_formats(ctx->hwframes_ref,
                                          AV_HWFRAME_TRANSFER_DIRECTION_FROM,
                                          &formats, 0);
    if (err < 0)
        return err;

    found = 0;
    for (i = 0; formats[i] != AV_PIX_FMT_NONE; i++) {
        if (formats[i] == outlink->format) {
            found = 1;
            break;
        }
    }
    av_freep(&formats);

    if (!found) {
        av_log(ctx, AV_LOG_ERROR, "Invalid output format %s for hwframe "
               "download.\n", av_get_pix_fmt_name(outlink->format));
        return AVERROR(EINVAL);
    }

    outlink->w = inlink->w;
    outlink->h = inlink->h;

    return 0;
}

static int hwdownload_filter_frame(AVFilterLink *link, AVFrame *input)
{
    AVFilterContext *avctx = link->dst;
    AVFilterLink  *outlink = avctx->outputs[0];
    HWDownloadContext *ctx = avctx->priv;
    AVFrame *output = NULL;
    int err;

    if (!ctx->hwframes_ref || !input->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "Input frames must have hardware context.\n");
        err = AVERROR(EINVAL);
        goto fail;
    }
    if ((void*)ctx->hwframes != input->hw_frames_ctx->data) {
        av_log(ctx, AV_LOG_ERROR, "Input frame is not the in the configured "
               "hwframe context.\n");
        err = AVERROR(EINVAL);
        goto fail;
    }

    output = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!output) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = av_hwframe_transfer_data(output, input, 0);
    if (err < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to download frame: %d.\n", err);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0)
        goto fail;

    av_frame_free(&input);

    return ff_filter_frame(avctx->outputs[0], output);

fail:
    av_frame_free(&input);
    av_frame_free(&output);
    return err;
}

static av_cold void hwdownload_uninit(AVFilterContext *avctx)
{
    HWDownloadContext *ctx = avctx->priv;

    av_buffer_unref(&ctx->hwframes_ref);
}

static const AVClass hwdownload_class = {
    .class_name = "hwdownload",
    .item_name  = av_default_item_name,
    .option     = NULL,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad hwdownload_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = hwdownload_config_input,
        .filter_frame = hwdownload_filter_frame,
    },
    { NULL }
};

static const AVFilterPad hwdownload_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = hwdownload_config_output,
    },
    { NULL }
};

AVFilter ff_vf_hwdownload = {
    .name          = "hwdownload",
    .description   = NULL_IF_CONFIG_SMALL("Download a hardware frame to a normal frame"),
    .uninit        = hwdownload_uninit,
    .query_formats = hwdownload_query_formats,
    .priv_size     = sizeof(HWDownloadContext),
    .priv_class    = &hwdownload_class,
    .inputs        = hwdownload_inputs,
    .outputs       = hwdownload_outputs,
};
