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

static int end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ShowInfoContext *showinfo = ctx->priv;
    AVFilterBufferRef *picref = inlink->cur_buf;
    uint32_t plane_checksum[4] = {0}, checksum = 0;
    int i, plane, vsub = av_pix_fmt_descriptors[inlink->format].log2_chroma_h;

    for (plane = 0; picref->data[plane] && plane < 4; plane++) {
        size_t linesize = av_image_get_linesize(picref->format, picref->video->w, plane);
        uint8_t *data = picref->data[plane];
        int h = plane == 1 || plane == 2 ? inlink->h >> vsub : inlink->h;

        for (i = 0; i < h; i++) {
            plane_checksum[plane] = av_adler32_update(plane_checksum[plane], data, linesize);
            checksum = av_adler32_update(checksum, data, linesize);
            data += picref->linesize[plane];
        }
    }

    av_log(ctx, AV_LOG_INFO,
           "n:%d pts:%s pts_time:%s pos:%"PRId64" "
           "fmt:%s sar:%d/%d s:%dx%d i:%c iskey:%d type:%c "
           "checksum:%08X plane_checksum:[%08X",
           showinfo->frame,
           av_ts2str(picref->pts), av_ts2timestr(picref->pts, &inlink->time_base), picref->pos,
           av_pix_fmt_descriptors[picref->format].name,
           picref->video->sample_aspect_ratio.num, picref->video->sample_aspect_ratio.den,
           picref->video->w, picref->video->h,
           !picref->video->interlaced     ? 'P' :         /* Progressive  */
           picref->video->top_field_first ? 'T' : 'B',    /* Top / Bottom */
           picref->video->key_frame,
           av_get_picture_type_char(picref->video->pict_type),
           checksum, plane_checksum[0]);

    for (plane = 1; picref->data[plane] && plane < 4; plane++)
        av_log(ctx, AV_LOG_INFO, " %08X", plane_checksum[plane]);
    av_log(ctx, AV_LOG_INFO, "]\n");

    showinfo->frame++;
    return ff_end_frame(inlink->dst->outputs[0]);
}

AVFilter avfilter_vf_showinfo = {
    .name        = "showinfo",
    .description = NULL_IF_CONFIG_SMALL("Show textual information for each video frame."),

    .priv_size = sizeof(ShowInfoContext),
    .init      = init,

    .inputs    = (const AVFilterPad[]) {{ .name = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .get_video_buffer = ff_null_get_video_buffer,
                                          .start_frame      = ff_null_start_frame,
                                          .end_frame        = end_frame,
                                          .min_perms        = AV_PERM_READ, },
                                        { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO },
                                        { .name = NULL}},
};
