/*
 * Copyright (c) 2007 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2013 Clément Bœsch <u pkh me>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @todo try to change to int
 * @todo try lifting based implementation
 * @todo optimize optimize optimize
 * @todo hard thresholding
 * @todo use QP to decide filter strength
 * @todo wavelet normalization / least squares optimal signal vs. noise thresholds
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    double luma_strength;
    double chroma_strength;
    int depth;
    float *plane[16+1][4];
    int linesize;
    int hsub, vsub;
    int pixel_depth;
} OWDenoiseContext;

#define OFFSET(x) offsetof(OWDenoiseContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption owdenoise_options[] = {
    { "depth",           "set depth",           OFFSET(depth),           AV_OPT_TYPE_INT,    {.i64 =   8}, 8,   16, FLAGS },
    { "luma_strength",   "set luma strength",   OFFSET(luma_strength),   AV_OPT_TYPE_DOUBLE, {.dbl = 1.0}, 0, 1000, FLAGS },
    { "ls",              "set luma strength",   OFFSET(luma_strength),   AV_OPT_TYPE_DOUBLE, {.dbl = 1.0}, 0, 1000, FLAGS },
    { "chroma_strength", "set chroma strength", OFFSET(chroma_strength), AV_OPT_TYPE_DOUBLE, {.dbl = 1.0}, 0, 1000, FLAGS },
    { "cs",              "set chroma strength", OFFSET(chroma_strength), AV_OPT_TYPE_DOUBLE, {.dbl = 1.0}, 0, 1000, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(owdenoise);

DECLARE_ALIGNED(8, static const uint8_t, dither)[8][8] = {
    {  0,  48,  12,  60,   3,  51,  15,  63 },
    { 32,  16,  44,  28,  35,  19,  47,  31 },
    {  8,  56,   4,  52,  11,  59,   7,  55 },
    { 40,  24,  36,  20,  43,  27,  39,  23 },
    {  2,  50,  14,  62,   1,  49,  13,  61 },
    { 34,  18,  46,  30,  33,  17,  45,  29 },
    { 10,  58,   6,  54,   9,  57,   5,  53 },
    { 42,  26,  38,  22,  41,  25,  37,  21 },
};

static const double coeff[2][5] = {
    {
         0.6029490182363579  * M_SQRT2,
         0.2668641184428723  * M_SQRT2,
        -0.07822326652898785 * M_SQRT2,
        -0.01686411844287495 * M_SQRT2,
         0.02674875741080976 * M_SQRT2,
    },{
         1.115087052456994   / M_SQRT2,
        -0.5912717631142470  / M_SQRT2,
        -0.05754352622849957 / M_SQRT2,
         0.09127176311424948 / M_SQRT2,
    }
};

static const double icoeff[2][5] = {
    {
         1.115087052456994   / M_SQRT2,
         0.5912717631142470  / M_SQRT2,
        -0.05754352622849957 / M_SQRT2,
        -0.09127176311424948 / M_SQRT2,
    },{
         0.6029490182363579  * M_SQRT2,
        -0.2668641184428723  * M_SQRT2,
        -0.07822326652898785 * M_SQRT2,
         0.01686411844287495 * M_SQRT2,
         0.02674875741080976 * M_SQRT2,
    }
};


static inline void decompose(float *dst_l, float *dst_h, const float *src,
                             int linesize, int w)
{
    int x, i;
    for (x = 0; x < w; x++) {
        double sum_l = src[x * linesize] * coeff[0][0];
        double sum_h = src[x * linesize] * coeff[1][0];
        for (i = 1; i <= 4; i++) {
            const double s = src[avpriv_mirror(x - i, w - 1) * linesize]
                           + src[avpriv_mirror(x + i, w - 1) * linesize];

            sum_l += coeff[0][i] * s;
            sum_h += coeff[1][i] * s;
        }
        dst_l[x * linesize] = sum_l;
        dst_h[x * linesize] = sum_h;
    }
}

static inline void compose(float *dst, const float *src_l, const float *src_h,
                           int linesize, int w)
{
    int x, i;
    for (x = 0; x < w; x++) {
        double sum_l = src_l[x * linesize] * icoeff[0][0];
        double sum_h = src_h[x * linesize] * icoeff[1][0];
        for (i = 1; i <= 4; i++) {
            const int x0 = avpriv_mirror(x - i, w - 1) * linesize;
            const int x1 = avpriv_mirror(x + i, w - 1) * linesize;

            sum_l += icoeff[0][i] * (src_l[x0] + src_l[x1]);
            sum_h += icoeff[1][i] * (src_h[x0] + src_h[x1]);
        }
        dst[x * linesize] = (sum_l + sum_h) * 0.5;
    }
}

static inline void decompose2D(float *dst_l, float *dst_h, const float *src,
                               int xlinesize, int ylinesize,
                               int step, int w, int h)
{
    int y, x;
    for (y = 0; y < h; y++)
        for (x = 0; x < step; x++)
            decompose(dst_l + ylinesize*y + xlinesize*x,
                      dst_h + ylinesize*y + xlinesize*x,
                      src   + ylinesize*y + xlinesize*x,
                      step * xlinesize, (w - x + step - 1) / step);
}

static inline void compose2D(float *dst, const float *src_l, const float *src_h,
                             int xlinesize, int ylinesize,
                             int step, int w, int h)
{
    int y, x;
    for (y = 0; y < h; y++)
        for (x = 0; x < step; x++)
            compose(dst   + ylinesize*y + xlinesize*x,
                    src_l + ylinesize*y + xlinesize*x,
                    src_h + ylinesize*y + xlinesize*x,
                    step * xlinesize, (w - x + step - 1) / step);
}

static void decompose2D2(float *dst[4], float *src, float *temp[2],
                         int linesize, int step, int w, int h)
{
    decompose2D(temp[0], temp[1], src,     1, linesize, step, w, h);
    decompose2D( dst[0],  dst[1], temp[0], linesize, 1, step, h, w);
    decompose2D( dst[2],  dst[3], temp[1], linesize, 1, step, h, w);
}

static void compose2D2(float *dst, float *src[4], float *temp[2],
                       int linesize, int step, int w, int h)
{
    compose2D(temp[0],  src[0],  src[1], linesize, 1, step, h, w);
    compose2D(temp[1],  src[2],  src[3], linesize, 1, step, h, w);
    compose2D(dst,     temp[0], temp[1], 1, linesize, step, w, h);
}

static void filter(OWDenoiseContext *s,
                   uint8_t       *dst, int dst_linesize,
                   const uint8_t *src, int src_linesize,
                   int width, int height, double strength)
{
    int x, y, i, j, depth = s->depth;

    while (1<<depth > width || 1<<depth > height)
        depth--;

    if (s->pixel_depth <= 8) {
        for (y = 0; y < height; y++)
            for(x = 0; x < width; x++)
                s->plane[0][0][y*s->linesize + x] = src[y*src_linesize + x];
    } else {
        const uint16_t *src16 = (const uint16_t *)src;

        src_linesize /= 2;
        for (y = 0; y < height; y++)
            for(x = 0; x < width; x++)
                s->plane[0][0][y*s->linesize + x] = src16[y*src_linesize + x];
    }

    for (i = 0; i < depth; i++)
        decompose2D2(s->plane[i + 1], s->plane[i][0], s->plane[0] + 1, s->linesize, 1<<i, width, height);

    for (i = 0; i < depth; i++) {
        for (j = 1; j < 4; j++) {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    double v = s->plane[i + 1][j][y*s->linesize + x];
                    if      (v >  strength) v -= strength;
                    else if (v < -strength) v += strength;
                    else                    v  = 0;
                    s->plane[i + 1][j][x + y*s->linesize] = v;
                }
            }
        }
    }
    for (i = depth-1; i >= 0; i--)
        compose2D2(s->plane[i][0], s->plane[i + 1], s->plane[0] + 1, s->linesize, 1<<i, width, height);

    if (s->pixel_depth <= 8) {
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                i = s->plane[0][0][y*s->linesize + x] + dither[x&7][y&7]*(1.0/64) + 1.0/128; // yes the rounding is insane but optimal :)
                if ((unsigned)i > 255U) i = ~(i >> 31);
                dst[y*dst_linesize + x] = i;
            }
        }
    } else {
        uint16_t *dst16 = (uint16_t *)dst;

        dst_linesize /= 2;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                i = s->plane[0][0][y*s->linesize + x];
                dst16[y*dst_linesize + x] = i;
            }
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    OWDenoiseContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    const int cw = AV_CEIL_RSHIFT(inlink->w, s->hsub);
    const int ch = AV_CEIL_RSHIFT(inlink->h, s->vsub);

    if (av_frame_is_writable(in)) {
        out = in;

        if (s->luma_strength > 0)
            filter(s, out->data[0], out->linesize[0], in->data[0], in->linesize[0], inlink->w, inlink->h, s->luma_strength);
        if (s->chroma_strength > 0) {
            filter(s, out->data[1], out->linesize[1], in->data[1], in->linesize[1], cw,        ch,        s->chroma_strength);
            filter(s, out->data[2], out->linesize[2], in->data[2], in->linesize[2], cw,        ch,        s->chroma_strength);
        }
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);

        if (s->luma_strength > 0) {
            filter(s, out->data[0], out->linesize[0], in->data[0], in->linesize[0], inlink->w, inlink->h, s->luma_strength);
        } else {
            av_image_copy_plane(out->data[0], out->linesize[0], in ->data[0], in ->linesize[0], inlink->w, inlink->h);
        }
        if (s->chroma_strength > 0) {
            filter(s, out->data[1], out->linesize[1], in->data[1], in->linesize[1], cw, ch, s->chroma_strength);
            filter(s, out->data[2], out->linesize[2], in->data[2], in->linesize[2], cw, ch, s->chroma_strength);
        } else {
            av_image_copy_plane(out->data[1], out->linesize[1], in ->data[1], in ->linesize[1], inlink->w, inlink->h);
            av_image_copy_plane(out->data[2], out->linesize[2], in ->data[2], in ->linesize[2], inlink->w, inlink->h);
        }

        if (in->data[3])
            av_image_copy_plane(out->data[3], out->linesize[3],
                                in ->data[3], in ->linesize[3],
                                inlink->w, inlink->h);
        av_frame_free(&in);
    }

    return ff_filter_frame(outlink, out);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,      AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P,      AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P,      AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVA444P,     AV_PIX_FMT_YUVA422P,
        AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV440P10,
        AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    int i, j;
    OWDenoiseContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int h = FFALIGN(inlink->h, 16);

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;
    s->pixel_depth = desc->comp[0].depth;

    s->linesize = FFALIGN(inlink->w, 16);
    for (j = 0; j < 4; j++) {
        for (i = 0; i <= s->depth; i++) {
            s->plane[i][j] = av_malloc_array(s->linesize, h * sizeof(s->plane[0][0][0]));
            if (!s->plane[i][j])
                return AVERROR(ENOMEM);
        }
    }
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i, j;
    OWDenoiseContext *s = ctx->priv;

    for (j = 0; j < 4; j++)
        for (i = 0; i <= s->depth; i++)
            av_freep(&s->plane[i][j]);
}

static const AVFilterPad owdenoise_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad owdenoise_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_owdenoise = {
    .name          = "owdenoise",
    .description   = NULL_IF_CONFIG_SMALL("Denoise using wavelets."),
    .priv_size     = sizeof(OWDenoiseContext),
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = owdenoise_inputs,
    .outputs       = owdenoise_outputs,
    .priv_class    = &owdenoise_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
