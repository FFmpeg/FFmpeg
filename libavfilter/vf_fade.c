/*
 * Copyright (c) 2010 Brandon Mintern
 * Copyright (c) 2007 Bobby Bingham
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * video fade filter
 * based heavily on vf_negate.c by Bobby Bingham
 */

#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct {
    int factor, fade_per_frame;
    unsigned int frame_index, start_frame, stop_frame;
    int hsub, vsub, bpp;
} FadeContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    FadeContext *fade = ctx->priv;
    unsigned int nb_frames;
    char in_out[4];

    if (!args ||
        sscanf(args, " %3[^:]:%u:%u", in_out, &fade->start_frame, &nb_frames) != 3) {
        av_log(ctx, AV_LOG_ERROR,
               "Expected 3 arguments '(in|out):#:#':'%s'\n", args);
        return AVERROR(EINVAL);
    }

    nb_frames = nb_frames ? nb_frames : 1;
    fade->fade_per_frame = (1 << 16) / nb_frames;
    if (!strcmp(in_out, "in"))
        fade->factor = 0;
    else if (!strcmp(in_out, "out")) {
        fade->fade_per_frame = -fade->fade_per_frame;
        fade->factor = (1 << 16);
    } else {
        av_log(ctx, AV_LOG_ERROR,
               "first argument must be 'in' or 'out':'%s'\n", in_out);
        return AVERROR(EINVAL);
    }
    fade->stop_frame = fade->start_frame + nb_frames;

    av_log(ctx, AV_LOG_VERBOSE,
           "type:%s start_frame:%d nb_frames:%d\n",
           in_out, fade->start_frame, nb_frames);
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_RGB24,    AV_PIX_FMT_BGR24,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    FadeContext *fade = inlink->dst->priv;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(inlink->format);

    fade->hsub = pixdesc->log2_chroma_w;
    fade->vsub = pixdesc->log2_chroma_h;

    fade->bpp = av_get_bits_per_pixel(pixdesc) >> 3;
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *frame)
{
    FadeContext *fade = inlink->dst->priv;
    uint8_t *p;
    int i, j, plane;

    if (fade->factor < UINT16_MAX) {
        /* luma or rgb plane */
        for (i = 0; i < frame->video->h; i++) {
            p = frame->data[0] + i * frame->linesize[0];
            for (j = 0; j < inlink->w * fade->bpp; j++) {
                /* fade->factor is using 16 lower-order bits for decimal
                 * places. 32768 = 1 << 15, it is an integer representation
                 * of 0.5 and is for rounding. */
                *p = (*p * fade->factor + 32768) >> 16;
                p++;
            }
        }

        if (frame->data[1] && frame->data[2]) {
            /* chroma planes */
            for (plane = 1; plane < 3; plane++) {
                for (i = 0; i < frame->video->h; i++) {
                    p = frame->data[plane] + (i >> fade->vsub) * frame->linesize[plane];
                    for (j = 0; j < inlink->w >> fade->hsub; j++) {
                        /* 8421367 = ((128 << 1) + 1) << 15. It is an integer
                         * representation of 128.5. The .5 is for rounding
                         * purposes. */
                        *p = ((*p - 128) * fade->factor + 8421367) >> 16;
                        p++;
                    }
                }
            }
        }
    }

    if (fade->frame_index >= fade->start_frame &&
        fade->frame_index <= fade->stop_frame)
        fade->factor += fade->fade_per_frame;
    fade->factor = av_clip_uint16(fade->factor);
    fade->frame_index++;

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static const AVFilterPad avfilter_vf_fade_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_props,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = filter_frame,
        .min_perms        = AV_PERM_READ | AV_PERM_WRITE,
        .rej_perms        = AV_PERM_PRESERVE,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_fade_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_fade = {
    .name          = "fade",
    .description   = NULL_IF_CONFIG_SMALL("Fade in/out input video"),
    .init          = init,
    .priv_size     = sizeof(FadeContext),
    .query_formats = query_formats,

    .inputs    = avfilter_vf_fade_inputs,
    .outputs   = avfilter_vf_fade_outputs,
};
