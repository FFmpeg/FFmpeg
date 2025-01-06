/*
 * Copyright (c) 2021 Paul Buxton
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
 */

 /**
  * @file
  * Color correction filter based on
  * https://www.researchgate.net/publication/275213614_A_New_Color_Correction_Method_for_Underwater_Imaging
  *
  */

#include "libavutil/imgutils.h"
#include "libavutil/mem.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

typedef struct ThreadData {
    AVFrame *in, *out;
    float l_avg;
    float a_avg;
    float b_avg;
} ThreadData;

typedef struct GrayWorldContext {
    float *tmpplab;
    int *line_count_pels;
    float *line_sum;
} GrayWorldContext;

static void apply_matrix(const float matrix[3][3], const float input[3], float output[3])
{
    output[0] = matrix[0][0] * input[0] + matrix[0][1] * input[1] + matrix[0][2] * input[2];
    output[1] = matrix[1][0] * input[0] + matrix[1][1] * input[1] + matrix[1][2] * input[2];
    output[2] = matrix[2][0] * input[0] + matrix[2][1] * input[1] + matrix[2][2] * input[2];
}

static const float lms2lab[3][3] = {
    {0.5774, 0.5774, 0.5774},
    {0.40825, 0.40825, -0.816458},
    {0.707, -0.707, 0}
};

static const float lab2lms[3][3] = {
    {0.57735, 0.40825, 0.707},
    {0.57735, 0.40825, -0.707},
    {0.57735, -0.8165, 0}
};

static const float rgb2lms[3][3] = {
    {0.3811, 0.5783, 0.0402},
    {0.1967, 0.7244, 0.0782},
    {0.0241, 0.1288, 0.8444}
};

static const float lms2rgb[3][3] = {
    {4.4679, -3.5873, 0.1193},
    {-1.2186, 2.3809, -0.1624},
    {0.0497, -0.2439, 1.2045}
};

/**
 * Convert from Linear RGB to logspace LAB
 *
 * @param rgb Input array of rgb components
 * @param lab output array of lab components
 */
static void rgb2lab(const float rgb[3], float lab[3])
{
    float lms[3];

    apply_matrix(rgb2lms, rgb, lms);
    lms[0] = lms[0] > 0.f ? logf(lms[0]) : -1024.f;
    lms[1] = lms[1] > 0.f ? logf(lms[1]) : -1024.f;
    lms[2] = lms[2] > 0.f ? logf(lms[2]) : -1024.f;
    apply_matrix(lms2lab, lms, lab);
}

/**
 * Convert from Logspace LAB to Linear RGB
 *
 * @param lab input array of lab components
 * @param rgb output array of rgb components
 */
static void lab2rgb(const float lab[3], float rgb[3])
{
    float lms[3];

    apply_matrix(lab2lms, lab, lms);
    lms[0] = expf(lms[0]);
    lms[1] = expf(lms[1]);
    lms[2] = expf(lms[2]);
    apply_matrix(lms2rgb, lms, rgb);
}

/**
 * Convert a frame from linear RGB to logspace LAB, and accumulate channel totals for each row
 * Convert from RGB -> lms using equation 4 in color transfer paper.
 *
 * @param ctx Filter context
 * @param arg Thread data pointer
 * @param jobnr job number
 * @param nb_jobs number of jobs
 */
static int convert_frame(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    GrayWorldContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    AVFilterLink *outlink = ctx->outputs[0];
    const int slice_start = (out->height * jobnr) / nb_jobs;
    const int slice_end = (out->height * (jobnr + 1)) / nb_jobs;
    float rgb[3], lab[3];

    for (int i = slice_start; i < slice_end; i++) {
        float *b_in_row = (float *)(in->data[1] + i * in->linesize[1]);
        float *g_in_row = (float *)(in->data[0] + i * in->linesize[0]);
        float *r_in_row = (float *)(in->data[2] + i * in->linesize[2]);
        float *acur = s->tmpplab + i * outlink->w + outlink->w * outlink->h;
        float *bcur = s->tmpplab + i * outlink->w + 2 * outlink->w * outlink->h;
        float *lcur = s->tmpplab + i * outlink->w;

        s->line_sum[i] = 0.f;
        s->line_sum[i + outlink->h] = 0.f;
        s->line_count_pels[i] = 0;

        for (int j = 0; j < outlink->w; j++) {
            rgb[0] = r_in_row[j];
            rgb[1] = g_in_row[j];
            rgb[2] = b_in_row[j];
            rgb2lab(rgb, lab);
            *(lcur++) = lab[0];
            *(acur++) = lab[1];
            *(bcur++) = lab[2];
            s->line_sum[i] += lab[1];
            s->line_sum[i + outlink->h] += lab[2];
            s->line_count_pels[i]++;
        }
    }
    return 0;
}

/**
 * Sum the channel totals and compute the mean for each channel
 *
 * @param s Frame context
 * @param td thread data
 */
static void compute_correction(GrayWorldContext *s, ThreadData *td)
{
    float asum = 0.f, bsum = 0.f;
    int pixels = 0;

    for (int y = 0; y < td->out->height; y++) {
        asum += s->line_sum[y];
        bsum += s->line_sum[y + td->out->height];
        pixels += s->line_count_pels[y];
    }

    td->a_avg = asum / pixels;
    td->b_avg = bsum / pixels;
}

/**
 * Subtract the mean logspace AB values from each pixel.
 *
 * @param ctx Filter context
 * @param arg Thread data pointer
 * @param jobnr job number
 * @param nb_jobs number of jobs
 */
static int correct_frame(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    GrayWorldContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFilterLink *outlink = ctx->outputs[0];
    const int slice_start = (out->height * jobnr) / nb_jobs;
    const int slice_end = (out->height * (jobnr + 1)) / nb_jobs;
    float rgb[3], lab[3];

    for (int i = slice_start; i < slice_end; i++) {
        float *g_out_row = (float *)(out->data[0] + i * out->linesize[0]);
        float *b_out_row = (float *)(out->data[1] + i * out->linesize[1]);
        float *r_out_row = (float *)(out->data[2] + i * out->linesize[2]);
        float *lcur = s->tmpplab + i * outlink->w;
        float *acur = s->tmpplab + i * outlink->w + outlink->w * outlink->h;
        float *bcur = s->tmpplab + i * outlink->w + 2 * outlink->w * outlink->h;

        for (int j = 0; j < outlink->w; j++) {
            lab[0] = *lcur++;
            lab[1] = *acur++;
            lab[2] = *bcur++;

            // subtract the average for the color channels
            lab[1] -= td->a_avg;
            lab[2] -= td->b_avg;

            //convert back to linear rgb
            lab2rgb(lab, rgb);
            r_out_row[j] = rgb[0];
            g_out_row[j] = rgb[1];
            b_out_row[j] = rgb[2];
        }
    }
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    GrayWorldContext *s = inlink->dst->priv;

    FF_ALLOC_TYPED_ARRAY(s->tmpplab, inlink->h * inlink->w * 3);
    FF_ALLOC_TYPED_ARRAY(s->line_count_pels, inlink->h);
    FF_ALLOC_TYPED_ARRAY(s->line_sum, inlink->h * 2);
    if (!s->tmpplab || !s->line_count_pels || !s->line_sum)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GrayWorldContext *s = ctx->priv;

    av_freep(&s->tmpplab);
    av_freep(&s->line_count_pels);
    av_freep(&s->line_sum);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    GrayWorldContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

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
    /* input and output transfer will be linear */
    if (in->color_trc == AVCOL_TRC_UNSPECIFIED) {
        av_log(ctx, AV_LOG_WARNING, "Untagged transfer, assuming linear light.\n");
        out->color_trc = AVCOL_TRC_LINEAR;
    } else if (in->color_trc != AVCOL_TRC_LINEAR) {
        av_log(ctx, AV_LOG_WARNING, "Gray world color correction works on linear light only.\n");
    }

    td.in = in;
    td.out = out;

    ff_filter_execute(ctx, convert_frame, &td, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));
    compute_correction(s, &td);
    ff_filter_execute(ctx, correct_frame, &td, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

    if (in != out) {
        av_image_copy_plane(out->data[3], out->linesize[3],
            in->data[3], in->linesize[3], outlink->w * 4, outlink->h);
        av_frame_free(&in);
    }

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad grayworld_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    }
};

const AVFilter ff_vf_grayworld = {
    .name          = "grayworld",
    .description   = NULL_IF_CONFIG_SMALL("Adjust white balance using LAB gray world algorithm"),
    .priv_size     = sizeof(GrayWorldContext),
    FILTER_INPUTS(grayworld_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS(AV_PIX_FMT_GBRPF32, AV_PIX_FMT_GBRAPF32),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .uninit        = uninit,
};
