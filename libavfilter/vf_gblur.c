/*
 * Copyright (c) 2011 Pascal Getreuer
 * Copyright (c) 2016 Paul B Mahol
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <float.h>

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "gblur.h"
#include "internal.h"
#include "vf_gblur_init.h"
#include "video.h"

#define OFFSET(x) offsetof(GBlurContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption gblur_options[] = {
    { "sigma",  "set sigma",            OFFSET(sigma),  AV_OPT_TYPE_FLOAT, {.dbl=0.5}, 0.0, 1024, FLAGS },
    { "steps",  "set number of steps",  OFFSET(steps),  AV_OPT_TYPE_INT,   {.i64=1},     1,    6, FLAGS },
    { "planes", "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT,   {.i64=0xF},   0,  0xF, FLAGS },
    { "sigmaV", "set vertical sigma",   OFFSET(sigmaV), AV_OPT_TYPE_FLOAT, {.dbl=-1},   -1, 1024, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(gblur);

typedef struct ThreadData {
    int height;
    int width;
} ThreadData;

static int filter_horizontally(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    GBlurContext *s = ctx->priv;
    ThreadData *td = arg;
    const int height = td->height;
    const int width = td->width;
    const int slice_start = (height *  jobnr   ) / nb_jobs;
    const int slice_end   = (height * (jobnr+1)) / nb_jobs;
    const float boundaryscale = s->boundaryscale;
    const int steps = s->steps;
    const float nu = s->nu;
    float *buffer = s->buffer;
    float *localbuf = NULL;

    if (s->localbuf)
        localbuf = s->localbuf + s->stride * width * slice_start;

    s->horiz_slice(buffer + width * slice_start, width, slice_end - slice_start,
                   steps, nu, boundaryscale, localbuf);
    return 0;
}

static int filter_vertically(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    GBlurContext *s = ctx->priv;
    ThreadData *td = arg;
    const int height = td->height;
    const int width = td->width;
    const int slice_start = (width *  jobnr   ) / nb_jobs;
    const int slice_end   = (width * (jobnr+1)) / nb_jobs;
    const float boundaryscale = s->boundaryscaleV;
    const int steps = s->steps;
    const float nu = s->nuV;
    float *buffer = s->buffer;

    s->verti_slice(buffer, width, height, slice_start, slice_end,
                   steps, nu, boundaryscale);

    return 0;
}

static int filter_postscale(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    GBlurContext *s = ctx->priv;
    ThreadData *td = arg;
    const float max = s->flt ?  FLT_MAX : (1 << s->depth) - 1;
    const float min = s->flt ? -FLT_MAX : 0.f;
    const int height = td->height;
    const int width = td->width;
    const int awidth = FFALIGN(width, 64);
    const int slice_start = (height *  jobnr   ) / nb_jobs;
    const int slice_end   = (height * (jobnr+1)) / nb_jobs;
    const float postscale = s->postscale * s->postscaleV;
    const int slice_size = slice_end - slice_start;

    s->postscale_slice(s->buffer + slice_start * awidth,
                       slice_size * awidth, postscale, min, max);

    return 0;
}

static void gaussianiir2d(AVFilterContext *ctx, int plane)
{
    GBlurContext *s = ctx->priv;
    const int width = s->planewidth[plane];
    const int height = s->planeheight[plane];
    const int nb_threads = ff_filter_get_nb_threads(ctx);
    ThreadData td;

    if (s->sigma < 0 || s->steps < 0)
        return;

    td.width = width;
    td.height = height;
    ff_filter_execute(ctx, filter_horizontally, &td,
                      NULL, FFMIN(height, nb_threads));
    ff_filter_execute(ctx, filter_vertically, &td,
                      NULL, FFMIN(width, nb_threads));
    ff_filter_execute(ctx, filter_postscale, &td,
                      NULL, FFMIN(width * height, nb_threads));
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32,
    AV_PIX_FMT_GRAYF32,
    AV_PIX_FMT_NONE
};

static av_cold void uninit(AVFilterContext *ctx)
{
    GBlurContext *s = ctx->priv;

    av_freep(&s->buffer);
    av_freep(&s->localbuf);
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    GBlurContext *s = inlink->dst->priv;

    uninit(inlink->dst);

    s->depth = desc->comp[0].depth;
    s->flt = !!(desc->flags & AV_PIX_FMT_FLAG_FLOAT);
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->buffer = av_malloc_array(FFALIGN(inlink->w, 64), FFALIGN(inlink->h, 64) * sizeof(*s->buffer));
    if (!s->buffer)
        return AVERROR(ENOMEM);

    if (s->sigmaV < 0) {
        s->sigmaV = s->sigma;
    }
    ff_gblur_init(s);

    return 0;
}

static void set_params(float sigma, int steps, float *postscale, float *boundaryscale, float *nu)
{
    double dnu, lambda;

    lambda = (sigma * sigma) / (2.0 * steps);
    dnu = (1.0 + 2.0 * lambda - sqrt(1.0 + 4.0 * lambda)) / (2.0 * lambda);
    *postscale = pow(dnu / lambda, steps);
    *boundaryscale = 1.0 / (1.0 - dnu);
    *nu = (float)dnu;
    if (!isnormal(*postscale))
        *postscale = 1.f;
    if (!isnormal(*boundaryscale))
        *boundaryscale = 1.f;
    if (!isnormal(*nu))
        *nu = 0.f;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    GBlurContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int plane;

    set_params(s->sigma,  s->steps, &s->postscale,  &s->boundaryscale,  &s->nu);
    set_params(s->sigmaV, s->steps, &s->postscaleV, &s->boundaryscaleV, &s->nuV);

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    for (plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->planeheight[plane];
        const int width = s->planewidth[plane];
        float *bptr = s->buffer;
        const uint8_t *src = in->data[plane];
        const uint16_t *src16 = (const uint16_t *)in->data[plane];
        uint8_t *dst = out->data[plane];
        uint16_t *dst16 = (uint16_t *)out->data[plane];
        int y, x;

        if (!(s->planes & (1 << plane))) {
            if (out != in)
                av_image_copy_plane(out->data[plane], out->linesize[plane],
                                    in->data[plane], in->linesize[plane],
                                    width * ((s->depth + 7) / 8), height);
            continue;
        }

        if (s->flt) {
            av_image_copy_plane((uint8_t *)bptr, width * sizeof(float),
                                in->data[plane], in->linesize[plane],
                                width * sizeof(float), height);
        } else if (s->depth == 8) {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    bptr[x] = src[x];
                }
                bptr += width;
                src += in->linesize[plane];
            }
        } else {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    bptr[x] = src16[x];
                }
                bptr += width;
                src16 += in->linesize[plane] / 2;
            }
        }

        gaussianiir2d(ctx, plane);

        bptr = s->buffer;
        if (s->flt) {
            av_image_copy_plane(out->data[plane], out->linesize[plane],
                                (uint8_t *)bptr, width * sizeof(float),
                                width * sizeof(float), height);
        } else if (s->depth == 8) {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    dst[x] = lrintf(bptr[x]);
                bptr += width;
                dst += out->linesize[plane];
            }
        } else {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    dst16[x] = lrintf(bptr[x]);
                bptr += width;
                dst16 += out->linesize[plane] / 2;
            }
        }
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad gblur_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_gblur = {
    .name          = "gblur",
    .description   = NULL_IF_CONFIG_SMALL("Apply Gaussian Blur filter."),
    .priv_size     = sizeof(GBlurContext),
    .priv_class    = &gblur_class,
    .uninit        = uninit,
    FILTER_INPUTS(gblur_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
