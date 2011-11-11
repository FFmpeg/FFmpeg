/*
 * Copyright (c) 2011 Michael Niedermayer
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
 *
 * The vsrc_color filter from Stefano Sabatini was used as template to create
 * this
 */

/**
 * @file
 * Mandelbrot fraktal renderer
 */

#include "avfilter.h"
#include "libavutil/imgutils.h"
#include "libavutil/parseutils.h"

typedef struct {
    int w, h;
    AVRational time_base;
    uint64_t pts;
    int maxiter;
    double start_x;
    double start_y;
    double start_scale;
} MBContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    MBContext *mb = ctx->priv;
    char frame_size  [128] = "320x240";
    char frame_rate  [128] = "25";
    AVRational frame_rate_q;
    int ret;

    mb->maxiter=1024;
    mb->start_x=-2.0;
    mb->start_y=-1.5;
    mb->start_scale=3.0;
    if (args)
        sscanf(args, "%127[^:]:%127[^:]:%d,%lf:%lf:%lf", frame_size, frame_rate, &mb->maxiter, &mb->start_x, &mb->start_y, &mb->start_scale);

    if (av_parse_video_size(&mb->w, &mb->h, frame_size) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame size: %s\n", frame_size);
        return AVERROR(EINVAL);
    }
    mb->start_scale /=mb->h;

    if (av_parse_video_rate(&frame_rate_q, frame_rate) < 0 ||
        frame_rate_q.den <= 0 || frame_rate_q.num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: %s\n", frame_rate);
        return AVERROR(EINVAL);
    }
    mb->time_base.num = frame_rate_q.den;
    mb->time_base.den = frame_rate_q.num;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MBContext *mb = ctx->priv;
    int i;

}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_BGR32,
        PIX_FMT_NONE
    };

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->src;
    MBContext *mb = ctx->priv;

    if (av_image_check_size(mb->w, mb->h, 0, ctx) < 0)
        return AVERROR(EINVAL);

    inlink->w = mb->w;
    inlink->h = mb->h;
    inlink->time_base = mb->time_base;

    return 0;
}

static void draw_mandelbrot(AVFilterContext *ctx, uint32_t *color, int linesize, int64_t pts)
{
    MBContext *mb = ctx->priv;
    int x,y,i;

    for(y=0; y<mb->h; y++){
        for(x=0; x<mb->w; x++){
            double cr=mb->start_x+mb->start_scale*x;
            double ci=mb->start_y+mb->start_scale*y;
            double zr=cr;
            double zi=ci;
            const double B=100;
            uint32_t c=0;

            for(i=0; i<256; i++){
                double t;
                if(zr*zr + zi*zi > B){
                    zr= i + (log(log(B)) - log(log(sqrt(zr*zr + zi*zi))))/log(2);
                    c= lrintf((sin(zr)+1)*127) + lrintf((sin(zr/1.234)+1)*127)*256*256 + lrintf((sin(zr/100)+1)*127)*256;
                    break;
                }
                t= zr*zr - zi*zi;
                zi= 2*zr*zi + ci;
                zr=       t + cr;
            }
            color[x + y*linesize]= c;
        }
    }
}

static int request_frame(AVFilterLink *link)
{
    MBContext *mb = link->src->priv;
    AVFilterBufferRef *picref = avfilter_get_video_buffer(link, AV_PERM_WRITE, mb->w, mb->h);
    picref->video->sample_aspect_ratio = (AVRational) {1, 1};
    picref->pts = mb->pts++;
    picref->pos = -1;

    avfilter_start_frame(link, avfilter_ref_buffer(picref, ~0));
    draw_mandelbrot(link->src, picref->data[0], picref->linesize[0]/4, picref->pts);
    avfilter_draw_slice(link, 0, mb->h, 1);
    avfilter_end_frame(link);
    avfilter_unref_buffer(picref);

    return 0;
}

AVFilter avfilter_vsrc_mandelbrot = {
    .name        = "mandelbrot",
    .description = NULL_IF_CONFIG_SMALL("Mandelbrot renderer"),

    .priv_size = sizeof(MBContext),
    .init      = init,
    .uninit    = uninit,

    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .request_frame   = request_frame,
                                    .config_props    = config_props },
                                  { .name = NULL}},
};
