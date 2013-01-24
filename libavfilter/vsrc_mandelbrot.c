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
#include "formats.h"
#include "video.h"
#include "internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include <float.h>
#include <math.h>

#define SQR(a) ((a)*(a))

enum Outer{
    ITERATION_COUNT,
    NORMALIZED_ITERATION_COUNT,
};

enum Inner{
    BLACK,
    PERIOD,
    CONVTIME,
    MINCOL,
};

typedef struct Point {
    double p[2];
    uint32_t val;
} Point;

typedef struct {
    const AVClass *class;
    int w, h;
    AVRational time_base;
    uint64_t pts;
    char *rate;
    int maxiter;
    double start_x;
    double start_y;
    double start_scale;
    double end_scale;
    double end_pts;
    double bailout;
    enum Outer outer;
    enum Inner inner;
    int cache_allocated;
    int cache_used;
    Point *point_cache;
    Point *next_cache;
    double (*zyklus)[2];
    uint32_t dither;
} MBContext;

#define OFFSET(x) offsetof(MBContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption mandelbrot_options[] = {
    {"size",        "set frame size",                OFFSET(w),       AV_OPT_TYPE_IMAGE_SIZE, {.str="640x480"},  CHAR_MIN, CHAR_MAX, FLAGS },
    {"s",           "set frame size",                OFFSET(w),       AV_OPT_TYPE_IMAGE_SIZE, {.str="640x480"},  CHAR_MIN, CHAR_MAX, FLAGS },
    {"rate",        "set frame rate",                OFFSET(rate),    AV_OPT_TYPE_STRING,     {.str="25"},  CHAR_MIN, CHAR_MAX, FLAGS },
    {"r",           "set frame rate",                OFFSET(rate),    AV_OPT_TYPE_STRING,     {.str="25"},  CHAR_MIN, CHAR_MAX, FLAGS },
    {"maxiter",     "set max iterations number",     OFFSET(maxiter), AV_OPT_TYPE_INT,        {.i64=7189},  1,        INT_MAX, FLAGS },
    {"start_x",     "set the initial x position",    OFFSET(start_x), AV_OPT_TYPE_DOUBLE,     {.dbl=-0.743643887037158704752191506114774}, -100, 100, FLAGS },
    {"start_y",     "set the initial y position",    OFFSET(start_y), AV_OPT_TYPE_DOUBLE,     {.dbl=-0.131825904205311970493132056385139}, -100, 100, FLAGS },
    {"start_scale", "set the initial scale value",   OFFSET(start_scale), AV_OPT_TYPE_DOUBLE, {.dbl=3.0},  0, FLT_MAX, FLAGS },
    {"end_scale",   "set the terminal scale value",  OFFSET(end_scale), AV_OPT_TYPE_DOUBLE,   {.dbl=0.3},  0, FLT_MAX, FLAGS },
    {"end_pts",     "set the terminal pts value",    OFFSET(end_pts), AV_OPT_TYPE_DOUBLE,     {.dbl=400},  0, INT64_MAX, FLAGS },
    {"bailout",     "set the bailout value",         OFFSET(bailout), AV_OPT_TYPE_DOUBLE,     {.dbl=10},   0, FLT_MAX, FLAGS },

    {"outer",       "set outer coloring mode",       OFFSET(outer), AV_OPT_TYPE_INT, {.i64=NORMALIZED_ITERATION_COUNT}, 0, INT_MAX, FLAGS, "outer" },
    {"iteration_count", "set iteration count mode",  0, AV_OPT_TYPE_CONST, {.i64=ITERATION_COUNT}, INT_MIN, INT_MAX, FLAGS, "outer" },
    {"normalized_iteration_count", "set normalized iteration count mode",   0, AV_OPT_TYPE_CONST, {.i64=NORMALIZED_ITERATION_COUNT}, INT_MIN, INT_MAX, FLAGS, "outer" },

    {"inner",       "set inner coloring mode",       OFFSET(inner), AV_OPT_TYPE_INT, {.i64=MINCOL}, 0, INT_MAX, FLAGS, "inner" },
    {"black",       "set black mode",                0, AV_OPT_TYPE_CONST, {.i64=BLACK}, INT_MIN, INT_MAX, FLAGS, "inner"},
    {"period",      "set period mode",               0, AV_OPT_TYPE_CONST, {.i64=PERIOD}, INT_MIN, INT_MAX, FLAGS, "inner"},
    {"convergence", "show time until convergence",   0, AV_OPT_TYPE_CONST, {.i64=CONVTIME}, INT_MIN, INT_MAX, FLAGS, "inner"},
    {"mincol",      "color based on point closest to the origin of the iterations",   0, AV_OPT_TYPE_CONST, {.i64=MINCOL}, INT_MIN, INT_MAX, FLAGS, "inner"},

    {NULL},
};

AVFILTER_DEFINE_CLASS(mandelbrot);

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    MBContext *mb = ctx->priv;
    AVRational rate_q;
    int err;

    mb->class = &mandelbrot_class;
    av_opt_set_defaults(mb);

    if ((err = (av_set_options_string(mb, args, "=", ":"))) < 0)
        return err;
    mb->bailout *= mb->bailout;

    mb->start_scale /=mb->h;
    mb->end_scale /=mb->h;

    if (av_parse_video_rate(&rate_q, mb->rate) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: %s\n", mb->rate);
        return AVERROR(EINVAL);
    }
    mb->time_base.num = rate_q.den;
    mb->time_base.den = rate_q.num;

    mb->cache_allocated = mb->w * mb->h * 3;
    mb->cache_used = 0;
    mb->point_cache= av_malloc(sizeof(*mb->point_cache)*mb->cache_allocated);
    mb-> next_cache= av_malloc(sizeof(*mb-> next_cache)*mb->cache_allocated);
    mb-> zyklus    = av_malloc(sizeof(*mb->zyklus) * (mb->maxiter+16));

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MBContext *mb = ctx->priv;

    av_freep(&mb->rate);
    av_freep(&mb->point_cache);
    av_freep(&mb-> next_cache);
    av_freep(&mb->zyklus);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_BGR32,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
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
        if(p->p[1] > py)
            break;
        x= round((p->p[0] - mb->start_x) / scale + mb->w/2);
        if(x<0 || x >= mb->w)
            continue;
        if(color) color[x] = p->val;
        if(out_cidx && *out_cidx < mb->cache_allocated)
            mb->next_cache[(*out_cidx)++]= *p;
    }
}

static int interpol(MBContext *mb, uint32_t *color, int x, int y, int linesize)
{
    uint32_t a,b,c,d, i;
    uint32_t ipol=0xFF000000;
    int dist;

    if(!x || !y || x+1==mb->w || y+1==mb->h)
        return 0;

    dist= FFMAX(FFABS(x-(mb->w>>1))*mb->h, FFABS(y-(mb->h>>1))*mb->w);

    if(dist<(mb->w*mb->h>>3))
        return 0;

    a=color[(x+1) + (y+0)*linesize];
    b=color[(x-1) + (y+1)*linesize];
    c=color[(x+0) + (y+1)*linesize];
    d=color[(x+1) + (y+1)*linesize];

    if(a&&c){
        b= color[(x-1) + (y+0)*linesize];
        d= color[(x+0) + (y-1)*linesize];
    }else if(b&&d){
        a= color[(x+1) + (y-1)*linesize];
        c= color[(x-1) + (y-1)*linesize];
    }else if(c){
        d= color[(x+0) + (y-1)*linesize];
        a= color[(x-1) + (y+0)*linesize];
        b= color[(x+1) + (y-1)*linesize];
    }else if(d){
        c= color[(x-1) + (y-1)*linesize];
        a= color[(x-1) + (y+0)*linesize];
        b= color[(x+1) + (y-1)*linesize];
    }else
        return 0;

    for(i=0; i<3; i++){
        int s= 8*i;
        uint8_t ac= a>>s;
        uint8_t bc= b>>s;
        uint8_t cc= c>>s;
        uint8_t dc= d>>s;
        int ipolab= (ac + bc);
        int ipolcd= (cc + dc);
        if(FFABS(ipolab - ipolcd) > 5)
            return 0;
        if(FFABS(ac-bc)+FFABS(cc-dc) > 20)
            return 0;
        ipol |= ((ipolab + ipolcd + 2)/4)<<s;
    }
    color[x + y*linesize]= ipol;
    return 1;
}

static void draw_mandelbrot(AVFilterContext *ctx, uint32_t *color, int linesize, int64_t pts)
{
    MBContext *mb = ctx->priv;
    int x,y,i, in_cidx=0, next_cidx=0, tmp_cidx;
    double scale= mb->start_scale*pow(mb->end_scale/mb->start_scale, pts/mb->end_pts);
    int use_zyklus=0;
    fill_from_cache(ctx, NULL, &in_cidx, NULL, mb->start_y+scale*(-mb->h/2-0.5), scale);
    tmp_cidx= in_cidx;
    memset(color, 0, sizeof(*color)*mb->w);
    for(y=0; y<mb->h; y++){
        int y1= y+1;
        const double ci=mb->start_y+scale*(y-mb->h/2);
        fill_from_cache(ctx, NULL, &in_cidx, &next_cidx, ci, scale);
        if(y1<mb->h){
            memset(color+linesize*y1, 0, sizeof(*color)*mb->w);
            fill_from_cache(ctx, color+linesize*y1, &tmp_cidx, NULL, ci + 3*scale/2, scale);
        }

        for(x=0; x<mb->w; x++){
            float av_uninit(epsilon);
            const double cr=mb->start_x+scale*(x-mb->w/2);
            double zr=cr;
            double zi=ci;
            uint32_t c=0;
            double dv= mb->dither / (double)(1LL<<32);
            mb->dither= mb->dither*1664525+1013904223;

            if(color[x + y*linesize] & 0xFF000000)
                continue;
            if(interpol(mb, color, x, y, linesize)){
                if(next_cidx < mb->cache_allocated){
                    mb->next_cache[next_cidx  ].p[0]= cr;
                    mb->next_cache[next_cidx  ].p[1]= ci;
                    mb->next_cache[next_cidx++].val = color[x + y*linesize];
                }
                continue;
            }

            use_zyklus= (x==0 || mb->inner!=BLACK ||color[x-1 + y*linesize] == 0xFF000000);
            if(use_zyklus)
                epsilon= scale*1*sqrt(SQR(x-mb->w/2) + SQR(y-mb->h/2))/mb->w;

#define Z_Z2_C(outr,outi,inr,ini)\
            outr= inr*inr - ini*ini + cr;\
            outi= 2*inr*ini + ci;

#define Z_Z2_C_ZYKLUS(outr,outi,inr,ini, Z)\
            Z_Z2_C(outr,outi,inr,ini)\
            if(use_zyklus){\
                if(Z && fabs(mb->zyklus[i>>1][0]-outr)+fabs(mb->zyklus[i>>1][1]-outi) <= epsilon)\
                    break;\
            }\
            mb->zyklus[i][0]= outr;\
            mb->zyklus[i][1]= outi;\



            for(i=0; i<mb->maxiter-8; i++){
                double t;
                Z_Z2_C_ZYKLUS(t, zi, zr, zi, 0)
                i++;
                Z_Z2_C_ZYKLUS(zr, zi, t, zi, 1)
                i++;
                Z_Z2_C_ZYKLUS(t, zi, zr, zi, 0)
                i++;
                Z_Z2_C_ZYKLUS(zr, zi, t, zi, 1)
                i++;
                Z_Z2_C_ZYKLUS(t, zi, zr, zi, 0)
                i++;
                Z_Z2_C_ZYKLUS(zr, zi, t, zi, 1)
                i++;
                Z_Z2_C_ZYKLUS(t, zi, zr, zi, 0)
                i++;
                Z_Z2_C_ZYKLUS(zr, zi, t, zi, 1)
                if(zr*zr + zi*zi > mb->bailout){
                    i-= FFMIN(7, i);
                    for(; i<mb->maxiter; i++){
                        zr= mb->zyklus[i][0];
                        zi= mb->zyklus[i][1];
                        if(zr*zr + zi*zi > mb->bailout){
                            switch(mb->outer){
                            case            ITERATION_COUNT: zr = i; break;
                            case NORMALIZED_ITERATION_COUNT: zr= i + log2(log(mb->bailout) / log(zr*zr + zi*zi)); break;
                            }
                            c= lrintf((sin(zr)+1)*127) + lrintf((sin(zr/1.234)+1)*127)*256*256 + lrintf((sin(zr/100)+1)*127)*256;
                            break;
                        }
                    }
                    break;
                }
            }
            if(!c){
                if(mb->inner==PERIOD){
                int j;
                for(j=i-1; j; j--)
                    if(SQR(mb->zyklus[j][0]-zr) + SQR(mb->zyklus[j][1]-zi) < epsilon*epsilon*10)
                        break;
                if(j){
                    c= i-j;
                    c= ((c<<5)&0xE0) + ((c<<10)&0xE000) + ((c<<15)&0xE00000);
                }
                }else if(mb->inner==CONVTIME){
                    c= floor(i*255.0/mb->maxiter+dv)*0x010101;
                } else if(mb->inner==MINCOL){
                    int j;
                    double closest=9999;
                    int closest_index=0;
                    for(j=i-1; j>=0; j--)
                        if(SQR(mb->zyklus[j][0]) + SQR(mb->zyklus[j][1]) < closest){
                            closest= SQR(mb->zyklus[j][0]) + SQR(mb->zyklus[j][1]);
                            closest_index= j;
                        }
                    closest = sqrt(closest);
                    c= lrintf((mb->zyklus[closest_index][0]/closest+1)*127+dv) + lrintf((mb->zyklus[closest_index][1]/closest+1)*127+dv)*256;
                }
            }
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
        av_log(ctx, AV_LOG_INFO, "Mandelbrot cache is too small!\n");
}

static int request_frame(AVFilterLink *link)
{
    MBContext *mb = link->src->priv;
    AVFilterBufferRef *picref = ff_get_video_buffer(link, AV_PERM_WRITE, mb->w, mb->h);
    picref->video->sample_aspect_ratio = (AVRational) {1, 1};
    picref->pts = mb->pts++;
    picref->pos = -1;

    draw_mandelbrot(link->src, (uint32_t*)picref->data[0], picref->linesize[0]/4, picref->pts);
    ff_filter_frame(link, picref);

    return 0;
}

static const AVFilterPad mandelbrot_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
    { NULL },
};

AVFilter avfilter_vsrc_mandelbrot = {
    .name        = "mandelbrot",
    .description = NULL_IF_CONFIG_SMALL("Render a Mandelbrot fractal."),

    .priv_size = sizeof(MBContext),
    .init      = init,
    .uninit    = uninit,

    .query_formats = query_formats,
    .inputs        = NULL,
    .outputs       = mandelbrot_outputs,
    .priv_class    = &mandelbrot_class,
};
