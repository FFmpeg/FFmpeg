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

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "gblur.h"
#include "internal.h"
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

static void horiz_slice_c(float *buffer, int width, int height, int steps,
                          float nu, float bscale)
{
    int step, x, y;
    float *ptr;
    for (y = 0; y < height; y++) {
        for (step = 0; step < steps; step++) {
            ptr = buffer + width * y;
            ptr[0] *= bscale;

            /* Filter rightwards */
            for (x = 1; x < width; x++)
                ptr[x] += nu * ptr[x - 1];
            ptr[x = width - 1] *= bscale;

            /* Filter leftwards */
            for (; x > 0; x--)
                ptr[x - 1] += nu * ptr[x];
        }
    }
}

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

    s->horiz_slice(buffer + width * slice_start, width, slice_end - slice_start,
                   steps, nu, boundaryscale);
    emms_c();
    return 0;
}

static void do_vertical_columns(float *buffer, int width, int height,
                                int column_begin, int column_end, int steps,
                                float nu, float boundaryscale, int column_step)
{
    const int numpixels = width * height;
    int i, x, k, step;
    float *ptr;
    for (x = column_begin; x < column_end;) {
        for (step = 0; step < steps; step++) {
            ptr = buffer + x;
            for (k = 0; k < column_step; k++) {
                ptr[k] *= boundaryscale;
            }
            /* Filter downwards */
            for (i = width; i < numpixels; i += width) {
                for (k = 0; k < column_step; k++) {
                    ptr[i + k] += nu * ptr[i - width + k];
                }
            }
            i = numpixels - width;

            for (k = 0; k < column_step; k++)
                ptr[i + k] *= boundaryscale;

            /* Filter upwards */
            for (; i > 0; i -= width) {
                for (k = 0; k < column_step; k++)
                    ptr[i - width + k] += nu * ptr[i + k];
            }
        }
        x += column_step;
    }
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
    int aligned_end;

    aligned_end = slice_start + (((slice_end - slice_start) >> 3) << 3);
    /* Filter vertically along columns (process 8 columns in each step) */
    do_vertical_columns(buffer, width, height, slice_start, aligned_end,
                        steps, nu, boundaryscale, 8);

    /* Filter un-aligned columns one by one */
    do_vertical_columns(buffer, width, height, aligned_end, slice_end,
                        steps, nu, boundaryscale, 1);
    return 0;
}


static int filter_postscale(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    GBlurContext *s = ctx->priv;
    ThreadData *td = arg;
    const float max = (1 << s->depth) - 1;
    const int height = td->height;
    const int width = td->width;
    const int64_t numpixels = width * (int64_t)height;
    const unsigned slice_start = (numpixels *  jobnr   ) / nb_jobs;
    const unsigned slice_end   = (numpixels * (jobnr+1)) / nb_jobs;
    const float postscale = s->postscale * s->postscaleV;
    float *buffer = s->buffer;
    unsigned i;

    for (i = slice_start; i < slice_end; i++) {
        buffer[i] *= postscale;
        buffer[i] = av_clipf(buffer[i], 0.f, max);
    }

    return 0;
}

static void gaussianiir2d(AVFilterContext *ctx, int plane)
{
    GBlurContext *s = ctx->priv;
    const int width = s->planewidth[plane];
    const int height = s->planeheight[plane];
    const int nb_threads = ff_filter_get_nb_threads(ctx);
    ThreadData td;

    if (s->sigma <= 0 || s->steps < 0)
        return;

    td.width = width;
    td.height = height;
    ctx->internal->execute(ctx, filter_horizontally, &td, NULL, FFMIN(height, nb_threads));
    ctx->internal->execute(ctx, filter_vertically, &td, NULL, FFMIN(width, nb_threads));
    ctx->internal->execute(ctx, filter_postscale, &td, NULL, FFMIN(width * height, nb_threads));
}

static int query_formats(AVFilterContext *ctx)
{
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
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

void ff_gblur_init(GBlurContext *s)
{
    s->horiz_slice = horiz_slice_c;
    if (ARCH_X86_64)
        ff_gblur_init_x86(s);
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    GBlurContext *s = inlink->dst->priv;

    s->depth = desc->comp[0].depth;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->buffer = av_malloc_array(FFALIGN(inlink->w, 16), FFALIGN(inlink->h, 16) * sizeof(*s->buffer));
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

        if (!s->sigma || !(s->planes & (1 << plane))) {
            if (out != in)
                av_image_copy_plane(out->data[plane], out->linesize[plane],
                                    in->data[plane], in->linesize[plane],
                                    width * ((s->depth + 7) / 8), height);
            continue;
        }

        if (s->depth == 8) {
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
        if (s->depth == 8) {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    dst[x] = bptr[x];
                }
                bptr += width;
                dst += out->linesize[plane];
            }
        } else {
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    dst16[x] = bptr[x];
                }
                bptr += width;
                dst16 += out->linesize[plane] / 2;
            }
        }
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GBlurContext *s = ctx->priv;

    av_freep(&s->buffer);
}

static const AVFilterPad gblur_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad gblur_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_gblur = {
    .name          = "gblur",
    .description   = NULL_IF_CONFIG_SMALL("Apply Gaussian Blur filter."),
    .priv_size     = sizeof(GBlurContext),
    .priv_class    = &gblur_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = gblur_inputs,
    .outputs       = gblur_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
