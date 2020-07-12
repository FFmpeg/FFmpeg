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
 * Mandelbrot fractal renderer
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
    WHITE,
    OUTZ,
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

typedef struct MBContext {
    const AVClass *class;
    int w, h;
    AVRational frame_rate;
    uint64_t pts;
    int maxiter;
    double start_x;
    double start_y;
    double start_scale;
    double end_scale;
    double end_pts;
    double bailout;
    int outer;
    int inner;
    int cache_allocated;
    int cache_used;
    Point *point_cache;
    Point *next_cache;
    double (*zyklus)[2];
    uint32_t dither;

    double morphxf;
    double morphyf;
    double morphamp;
} MBContext;

#define OFFSET(x) offsetof(MBContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption mandelbrot_options[] = {
    {"size",        "set frame size",                OFFSET(w),       AV_OPT_TYPE_IMAGE_SIZE, {.str="640x480"},  0, 0, FLAGS },
    {"s",           "set frame size",                OFFSET(w),       AV_OPT_TYPE_IMAGE_SIZE, {.str="640x480"},  0, 0, FLAGS },
    {"rate",        "set frame rate",                OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"},  0, INT_MAX, FLAGS },
    {"r",           "set frame rate",                OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"},  0, INT_MAX, FLAGS },
    {"maxiter",     "set max iterations number",     OFFSET(maxiter), AV_OPT_TYPE_INT,        {.i64=7189},  1,        INT_MAX, FLAGS },
    {"start_x",     "set the initial x position",    OFFSET(start_x), AV_OPT_TYPE_DOUBLE,     {.dbl=-0.743643887037158704752191506114774}, -100, 100, FLAGS },
    {"start_y",     "set the initial y position",    OFFSET(start_y), AV_OPT_TYPE_DOUBLE,     {.dbl=-0.131825904205311970493132056385139}, -100, 100, FLAGS },
    {"start_scale", "set the initial scale value",   OFFSET(start_scale), AV_OPT_TYPE_DOUBLE, {.dbl=3.0},  0, FLT_MAX, FLAGS },
    {"end_scale",   "set the terminal scale value",  OFFSET(end_scale), AV_OPT_TYPE_DOUBLE,   {.dbl=0.3},  0, FLT_MAX, FLAGS },
    {"end_pts",     "set the terminal pts value",    OFFSET(end_pts), AV_OPT_TYPE_DOUBLE,     {.dbl=400},  0, INT64_MAX, FLAGS },
    {"bailout",     "set the bailout value",         OFFSET(bailout), AV_OPT_TYPE_DOUBLE,     {.dbl=10},   0, FLT_MAX, FLAGS },
    {"morphxf",     "set morph x frequency",         OFFSET(morphxf), AV_OPT_TYPE_DOUBLE,     {.dbl=0.01},   -FLT_MAX, FLT_MAX, FLAGS },
    {"morphyf",     "set morph y frequency",         OFFSET(morphyf), AV_OPT_TYPE_DOUBLE,     {.dbl=0.0123}, -FLT_MAX, FLT_MAX, FLAGS },
    {"morphamp",    "set morph amplitude",           OFFSET(morphamp), AV_OPT_TYPE_DOUBLE,    {.dbl=0},      -FLT_MAX, FLT_MAX, FLAGS },

    {"outer",       "set outer coloring mode",       OFFSET(outer), AV_OPT_TYPE_INT, {.i64=NORMALIZED_ITERATION_COUNT}, 0, INT_MAX, FLAGS, "outer" },
    {"iteration_count", "set iteration count mode",  0, AV_OPT_TYPE_CONST, {.i64=ITERATION_COUNT}, INT_MIN, INT_MAX, FLAGS, "outer" },
    {"normalized_iteration_count", "set normalized iteration count mode",   0, AV_OPT_TYPE_CONST, {.i64=NORMALIZED_ITERATION_COUNT}, INT_MIN, INT_MAX, FLAGS, "outer" },
    {"white", "set white mode",                      0, AV_OPT_TYPE_CONST, {.i64=WHITE}, INT_MIN, INT_MAX, FLAGS, "outer" },
    {"outz",        "set outz mode",                 0, AV_OPT_TYPE_CONST, {.i64=OUTZ}, INT_MIN, INT_MAX, FLAGS, "outer" },

    {"inner",       "set inner coloring mode",       OFFSET(inner), AV_OPT_TYPE_INT, {.i64=MINCOL}, 0, INT_MAX, FLAGS, "inner" },
    {"black",       "set black mode",                0, AV_OPT_TYPE_CONST, {.i64=BLACK}, INT_MIN, INT_MAX, FLAGS, "inner"},
    {"period",      "set period mode",               0, AV_OPT_TYPE_CONST, {.i64=PERIOD}, INT_MIN, INT_MAX, FLAGS, "inner"},
    {"convergence", "show time until convergence",   0, AV_OPT_TYPE_CONST, {.i64=CONVTIME}, INT_MIN, INT_MAX, FLAGS, "inner"},
    {"mincol",      "color based on point closest to the origin of the iterations",   0, AV_OPT_TYPE_CONST, {.i64=MINCOL}, INT_MIN, INT_MAX, FLAGS, "inner"},

    {NULL},
};

AVFILTER_DEFINE_CLASS(mandelbrot);

static av_cold int init(AVFilterContext *ctx)
{
    MBContext *s = ctx->priv;

    s->bailout *= s->bailout;

    s->start_scale /=s->h;
    s->end_scale /=s->h;

    s->cache_allocated = s->w * s->h * 3;
    s->cache_used = 0;
    s->point_cache= av_malloc_array(s->cache_allocated, sizeof(*s->point_cache));
    s-> next_cache= av_malloc_array(s->cache_allocated, sizeof(*s-> next_cache));
    s-> zyklus    = av_malloc_array(s->maxiter + 16, sizeof(*s->zyklus));

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MBContext *s = ctx->priv;

    av_freep(&s->point_cache);
    av_freep(&s-> next_cache);
    av_freep(&s->zyklus);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_0BGR32,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->src;
    MBContext *s = ctx->priv;

    if (av_image_check_size(s->w, s->h, 0, ctx) < 0)
        return AVERROR(EINVAL);

    inlink->w = s->w;
    inlink->h = s->h;
    inlink->time_base = av_inv_q(s->frame_rate);

    return 0;
}

static void fill_from_cache(AVFilterContext *ctx, uint32_t *color, int *in_cidx, int *out_cidx, double py, double scale){
    MBContext *s = ctx->priv;
    if(s->morphamp)
        return;
    for(; *in_cidx < s->cache_used; (*in_cidx)++){
        Point *p= &s->point_cache[*in_cidx];
        int x;
        if(p->p[1] > py)
            break;
        x= lrint((p->p[0] - s->start_x) / scale + s->w/2);
        if(x<0 || x >= s->w)
            continue;
        if(color) color[x] = p->val;
        if(out_cidx && *out_cidx < s->cache_allocated)
            s->next_cache[(*out_cidx)++]= *p;
    }
}

static int interpol(MBContext *s, uint32_t *color, int x, int y, int linesize)
{
    uint32_t a,b,c,d, i;
    uint32_t ipol=0xFF000000;
    int dist;

    if(!x || !y || x+1==s->w || y+1==s->h)
        return 0;

    dist= FFMAX(FFABS(x-(s->w>>1))*s->h, FFABS(y-(s->h>>1))*s->w);

    if(dist<(s->w*s->h>>3))
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
    MBContext *s = ctx->priv;
    int x,y,i, in_cidx=0, next_cidx=0, tmp_cidx;
    double scale= s->start_scale*pow(s->end_scale/s->start_scale, pts/s->end_pts);
    int use_zyklus=0;
    fill_from_cache(ctx, NULL, &in_cidx, NULL, s->start_y+scale*(-s->h/2-0.5), scale);
    tmp_cidx= in_cidx;
    memset(color, 0, sizeof(*color)*s->w);
    for(y=0; y<s->h; y++){
        int y1= y+1;
        const double ci=s->start_y+scale*(y-s->h/2);
        fill_from_cache(ctx, NULL, &in_cidx, &next_cidx, ci, scale);
        if(y1<s->h){
            memset(color+linesize*y1, 0, sizeof(*color)*s->w);
            fill_from_cache(ctx, color+linesize*y1, &tmp_cidx, NULL, ci + 3*scale/2, scale);
        }

        for(x=0; x<s->w; x++){
            float av_uninit(epsilon);
            const double cr=s->start_x+scale*(x-s->w/2);
            double zr=cr;
            double zi=ci;
            uint32_t c=0;
            double dv= s->dither / (double)(1LL<<32);
            s->dither= s->dither*1664525+1013904223;

            if(color[x + y*linesize] & 0xFF000000)
                continue;
            if(!s->morphamp){
                if(interpol(s, color, x, y, linesize)){
                    if(next_cidx < s->cache_allocated){
                        s->next_cache[next_cidx  ].p[0]= cr;
                        s->next_cache[next_cidx  ].p[1]= ci;
                        s->next_cache[next_cidx++].val = color[x + y*linesize];
                    }
                    continue;
                }
            }else{
                zr += cos(pts * s->morphxf) * s->morphamp;
                zi += sin(pts * s->morphyf) * s->morphamp;
            }

            use_zyklus= (x==0 || s->inner!=BLACK ||color[x-1 + y*linesize] == 0xFF000000);
            if(use_zyklus)
                epsilon= scale*(abs(x-s->w/2) + abs(y-s->h/2))/s->w;

#define Z_Z2_C(outr,outi,inr,ini)\
            outr= inr*inr - ini*ini + cr;\
            outi= 2*inr*ini + ci;

#define Z_Z2_C_ZYKLUS(outr,outi,inr,ini, Z)\
            Z_Z2_C(outr,outi,inr,ini)\
            if(use_zyklus){\
                if(Z && fabs(s->zyklus[i>>1][0]-outr)+fabs(s->zyklus[i>>1][1]-outi) <= epsilon)\
                    break;\
            }\
            s->zyklus[i][0]= outr;\
            s->zyklus[i][1]= outi;\



            for(i=0; i<s->maxiter-8; i++){
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
                if(zr*zr + zi*zi > s->bailout){
                    i-= FFMIN(7, i);
                    for(; i<s->maxiter; i++){
                        zr= s->zyklus[i][0];
                        zi= s->zyklus[i][1];
                        if(zr*zr + zi*zi > s->bailout){
                            switch(s->outer){
                            case            ITERATION_COUNT:
                                zr = i;
                                c = lrintf((sinf(zr)+1)*127) + lrintf((sinf(zr/1.234)+1)*127)*256*256 + lrintf((sinf(zr/100)+1)*127)*256;
                                break;
                            case NORMALIZED_ITERATION_COUNT:
                                zr = i + log2(log(s->bailout) / log(zr*zr + zi*zi));
                                c = lrintf((sinf(zr)+1)*127) + lrintf((sinf(zr/1.234)+1)*127)*256*256 + lrintf((sinf(zr/100)+1)*127)*256;
                                break;
                            case                      WHITE:
                                c = 0xFFFFFF;
                                break;
                            case                      OUTZ:
                                zr /= s->bailout;
                                zi /= s->bailout;
                                c = (((int)(zr*128+128))&0xFF)*256 + (((int)(zi*128+128))&0xFF);
                            }
                            break;
                        }
                    }
                    break;
                }
            }
            if(!c){
                if(s->inner==PERIOD){
                    int j;
                    for(j=i-1; j; j--)
                        if(SQR(s->zyklus[j][0]-zr) + SQR(s->zyklus[j][1]-zi) < epsilon*epsilon*10)
                            break;
                    if(j){
                        c= i-j;
                        c= ((c<<5)&0xE0) + ((c<<10)&0xE000) + ((c<<15)&0xE00000);
                    }
                }else if(s->inner==CONVTIME){
                    c= floor(i*255.0/s->maxiter+dv)*0x010101;
                } else if(s->inner==MINCOL){
                    int j;
                    double closest=9999;
                    int closest_index=0;
                    for(j=i-1; j>=0; j--)
                        if(SQR(s->zyklus[j][0]) + SQR(s->zyklus[j][1]) < closest){
                            closest= SQR(s->zyklus[j][0]) + SQR(s->zyklus[j][1]);
                            closest_index= j;
                        }
                    closest = sqrt(closest);
                    c= lrintf((s->zyklus[closest_index][0]/closest+1)*127+dv) + lrintf((s->zyklus[closest_index][1]/closest+1)*127+dv)*256;
                }
            }
            c |= 0xFF000000;
            color[x + y*linesize]= c;
            if(next_cidx < s->cache_allocated){
                s->next_cache[next_cidx  ].p[0]= cr;
                s->next_cache[next_cidx  ].p[1]= ci;
                s->next_cache[next_cidx++].val = c;
            }
        }
        fill_from_cache(ctx, NULL, &in_cidx, &next_cidx, ci + scale/2, scale);
    }
    FFSWAP(void*, s->next_cache, s->point_cache);
    s->cache_used = next_cidx;
    if(s->cache_used == s->cache_allocated)
        av_log(ctx, AV_LOG_INFO, "Mandelbrot cache is too small!\n");
}

static int request_frame(AVFilterLink *link)
{
    MBContext *s = link->src->priv;
    AVFrame *picref = ff_get_video_buffer(link, s->w, s->h);
    if (!picref)
        return AVERROR(ENOMEM);

    picref->sample_aspect_ratio = (AVRational) {1, 1};
    picref->pts = s->pts++;

    draw_mandelbrot(link->src, (uint32_t*)picref->data[0], picref->linesize[0]/4, picref->pts);
    return ff_filter_frame(link, picref);
}

static const AVFilterPad mandelbrot_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter ff_vsrc_mandelbrot = {
    .name          = "mandelbrot",
    .description   = NULL_IF_CONFIG_SMALL("Render a Mandelbrot fractal."),
    .priv_size     = sizeof(MBContext),
    .priv_class    = &mandelbrot_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = NULL,
    .outputs       = mandelbrot_outputs,
};
