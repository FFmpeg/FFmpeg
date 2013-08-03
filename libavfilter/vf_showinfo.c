/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * filter for showing textual video frame information
 */

#include "libavutil/adler32.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/pixdesc.h"
#include "libavutil/timestamp.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct {
    unsigned int frame;
} ShowInfoContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    ShowInfoContext *showinfo = ctx->priv;
    showinfo->frame = 0;
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ShowInfoContext *showinfo = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    uint32_t plane_checksum[4] = {0}, checksum = 0;
    int i, plane, vsub = desc->log2_chroma_h;

    for (plane = 0; plane < 4 && frame->data[plane] && frame->linesize[plane]; plane++) {
        int64_t linesize = av_image_get_linesize(frame->format, frame->video->w, plane);
        uint8_t *data = frame->data[plane];
        int h = plane == 1 || plane == 2 ? inlink->h >> vsub : inlink->h;

        if (linesize < 0)
            return linesize;

        for (i = 0; i < h; i++) {
            plane_checksum[plane] = av_adler32_update(plane_checksum[plane], data, linesize);
            checksum = av_adler32_update(checksum, data, linesize);
            data += frame->linesize[plane];
        }
    }

    av_log(ctx, AV_LOG_INFO,
           "n:%d pts:%s pts_time:%s pos:%"PRId64" "
           "fmt:%s sar:%d/%d s:%dx%d i:%c iskey:%d type:%c "
           "checksum:%08X plane_checksum:[%08X",
           showinfo->frame,
           av_ts2str(frame->pts), av_ts2timestr(frame->pts, &inlink->time_base), frame->pos,
           desc->name,
           frame->video->sample_aspect_ratio.num, frame->video->sample_aspect_ratio.den,
           frame->video->w, frame->video->h,
           !frame->video->interlaced     ? 'P' :         /* Progressive  */
           frame->video->top_field_first ? 'T' : 'B',    /* Top / Bottom */
           frame->video->key_frame,
           av_get_picture_type_char(frame->video->pict_type),
           checksum, plane_checksum[0]);

    for (plane = 1; plane < 4 && frame->data[plane] && frame->linesize[plane]; plane++)
        av_log(ctx, AV_LOG_INFO, " %08X", plane_checksum[plane]);
    av_log(ctx, AV_LOG_INFO, "]\n");

    showinfo->frame++;
    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static const AVFilterPad avfilter_vf_showinfo_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = filter_frame,
        .min_perms        = AV_PERM_READ,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_showinfo_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO
    },
    { NULL }
};

AVFilter avfilter_vf_showinfo = {
    .name        = "showinfo",
    .description = NULL_IF_CONFIG_SMALL("Show textual information for each video frame."),

    .priv_size = sizeof(ShowInfoContext),
    .init      = init,

    .inputs    = avfilter_vf_showinfo_inputs,

    .outputs   = avfilter_vf_showinfo_outputs,
};
