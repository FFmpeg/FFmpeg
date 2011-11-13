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

enum Outer{
    ITERATION_COUNT,
    NORMALIZED_ITERATION_COUNT,
};

typedef struct Point {
    double p[2];
    uint32_t val;
} Point;

typedef struct {
    int w, h;
    AVRational time_base;
    uint64_t pts;
    int maxiter;
    double start_x;
    double start_y;
    double start_scale;
    double end_scale;
    double end_pts;
    double bailout;
    enum Outer outer;
    int cache_allocated;
    int cache_used;
    Point *point_cache;
    Point *next_cache;
    double (*zyklus)[2];
} MBContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    MBContext *mb = ctx->priv;
    char frame_size  [128] = "640x480";
    char frame_rate  [128] = "25";
    AVRational frame_rate_q;

    mb->maxiter=4096;
    mb->start_x=-0.743643887037158704752191506114774;
    mb->start_y=-0.131825904205311970493132056385139;
    mb->start_scale=3.0;
    mb->end_scale=0.3;
    mb->end_pts=800;
    mb->bailout=10;
    mb->outer= NORMALIZED_ITERATION_COUNT;
    if (args)
        sscanf(args, "%127[^:]:%127[^:]:%d:%lf:%lf:%lf:%lf:%lf:%lf:%d", frame_size, frame_rate,
               &mb->maxiter, &mb->start_x, &mb->start_y, &mb->start_scale, &mb->end_scale,
               &mb->end_pts, &mb->bailout, &mb->outer);
    mb->bailout *= mb->bailout;

    if (av_parse_video_size(&mb->w, &mb->h, frame_size) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame size: %s\n", frame_size);
        return AVERROR(EINVAL);
    }
    mb->start_scale /=mb->h;
    mb->end_scale /=mb->h;

    if (av_parse_video_rate(&frame_rate_q, frame_rate) < 0 ||
        frame_rate_q.den <= 0 || frame_rate_q.num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: %s\n", frame_rate);
        return AVERROR(EINVAL);
    }
    mb->time_base.num = frame_rate_q.den;
    mb->time_base.den = frame_rate_q.num;

    mb->cache_allocated = mb->w * mb->h * 3;
    mb->cache_used = 0;
    mb->point_cache= av_malloc(sizeof(*mb->point_cache)*mb->cache_allocated);
    mb-> next_cache= av_malloc(sizeof(*mb-> next_cache)*mb->cache_allocated);
    mb-> zyklus    = av_malloc(sizeof(*mb->zyklus) * mb->maxiter);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MBContext *mb = ctx->priv;

    av_freep(&mb->point_cache);
    av_freep(&mb-> next_cache);
    av_freep(&mb->zyklus);
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

static void fill_from_cache(AVFilterContext *ctx, uint32_t *color, int *in_cidx, int *out_cidx, double py, double scale){
    MBContext *mb = ctx->priv;
    for(; *in_cidx < mb->cache_used; (*in_cidx)++){
        Point *p= &mb->point_cache[*in_cidx];
        int x;
        if(*in_cidx >= mb->cache_used || p->p[1] > py)
            break;
        x= round((p->p[0] - mb->start_x) / scale + mb->w/2);
        if(x<0 || x >= mb->w)
            continue;
        if(color) color[x] = p->val;
        if(out_cidx && *out_cidx < mb->cache_allocated)
            mb->next_cache[(*out_cidx)++]= *p;
    }
}

static void draw_mandelbrot(AVFilterContext *ctx, uint32_t *color, int linesize, int64_t pts)
{
    MBContext *mb = ctx->priv;
    int x,y,i, in_cidx=0, next_cidx=0, tmp_cidx;
    double scale= mb->start_scale*pow(mb->end_scale/mb->start_scale, pts/mb->end_pts);
    int use_zyklus=0;
    fill_from_cache(ctx, NULL, &in_cidx, NULL, mb->start_y+scale*(-mb->h/2-0.5), scale);
    for(y=0; y<mb->h; y++){
        const double ci=mb->start_y+scale*(y-mb->h/2);
        memset(color+linesize*y, 0, sizeof(*color)*mb->w);
        fill_from_cache(ctx, color+linesize*y, &in_cidx, &next_cidx, ci, scale);
        tmp_cidx= in_cidx;
        fill_from_cache(ctx, color+linesize*y, &tmp_cidx, NULL, ci + scale/2, scale);

        for(x=0; x<mb->w; x++){
            const double cr=mb->start_x+scale*(x-mb->w/2);
            double zr=cr;
            double zi=ci;
            uint32_t c=0;

            if(color[x + y*linesize] & 0xFF000000)
                continue;

            for(i=0; i<mb->maxiter; i++){
                double t;
                if(zr*zr + zi*zi > mb->bailout){
                    switch(mb->outer){
                    case            ITERATION_COUNT: zr= i; break;
                    case NORMALIZED_ITERATION_COUNT: zr= i + (log(log(mb->bailout)) - log(log(sqrt(zr*zr + zi*zi))))/log(2); break;
                    }
                    c= lrintf((sin(zr)+1)*127) + lrintf((sin(zr/1.234)+1)*127)*256*256 + lrintf((sin(zr/100)+1)*127)*256;
                    break;
                }
                t= zr*zr - zi*zi;
                zi= 2*zr*zi + ci;
                zr=       t + cr;
                if(use_zyklus){
                    if(i && mb->zyklus[i>>1][0]==zr && mb->zyklus[i>>1][1]==zi)
                        break;
                    mb->zyklus[i][0]= zr;
                    mb->zyklus[i][1]= zi;
                }
            }
            use_zyklus = !c;
            c |= 0xFF000000;
            color[x + y*linesize]= c;
            if(next_cidx < mb->cache_allocated){
                mb->next_cache[next_cidx  ].p[0]= cr;
                mb->next_cache[next_cidx  ].p[1]= ci;
                mb->next_cache[next_cidx++].val = c;
            }
        }
        fill_from_cache(ctx, NULL, &in_cidx, &next_cidx, ci + scale/2, scale);
    }
    FFSWAP(void*, mb->next_cache, mb->point_cache);
    mb->cache_used = next_cidx;
    if(mb->cache_used == mb->cache_allocated)
        av_log(0, AV_LOG_INFO, "Mandelbrot cache is too small!\n");
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
