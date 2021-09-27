/*
 * Copyright (c) 2018 Mina Sami
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
 * Color Constancy filter
 *
 * @see http://colorconstancy.com/
 *
 * @cite
 * J. van de Weijer, Th. Gevers, A. Gijsenij "Edge-Based Color Constancy".
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#include <math.h>

#define GREY_EDGE "greyedge"

#define SQRT3 1.73205080757

#define NUM_PLANES    3
#define MAX_DIFF_ORD  2
#define MAX_META_DATA 4
#define MAX_DATA      4

#define INDEX_TEMP 0
#define INDEX_DX   1
#define INDEX_DY   2
#define INDEX_DXY  3
#define INDEX_NORM INDEX_DX
#define INDEX_SRC  0
#define INDEX_DST  1
#define INDEX_ORD  2
#define INDEX_DIR  3
#define DIR_X 0
#define DIR_Y 1

/**
 * Used for passing data between threads.
 */
typedef struct ThreadData {
    AVFrame *in, *out;
    int meta_data[MAX_META_DATA];
    double  *data[MAX_DATA][NUM_PLANES];
} ThreadData;

/**
 * Common struct for all algorithms contexts.
 */
typedef struct ColorConstancyContext {
    const AVClass *class;

    int difford;
    int minknorm; /**< @minknorm = 0 : getMax instead */
    double sigma;

    int nb_threads;
    int planeheight[4];
    int planewidth[4];

    int filtersize;
    double *gauss[MAX_DIFF_ORD+1];

    double white[NUM_PLANES];
} ColorConstancyContext;

#define OFFSET(x) offsetof(ColorConstancyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

#define GINDX(s, i) ( (i) - ((s) >> 2) )

/**
 * Sets gauss filters used for calculating gauss derivatives. Filter size
 * depends on sigma which is a user option hence we calculate these
 * filters each time. Also each higher order depends on lower ones. Sigma
 * can be zero only at difford = 0, then we only convert data to double
 * instead.
 *
 * @param ctx the filter context.
 *
 * @return 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure.
 */
static int set_gauss(AVFilterContext *ctx)
{
    ColorConstancyContext *s = ctx->priv;
    int filtersize = s->filtersize;
    int difford    = s->difford;
    double sigma   = s->sigma;
    double sum1, sum2;
    int i;

    for (i = 0; i <= difford; ++i) {
        s->gauss[i] = av_calloc(filtersize, sizeof(*s->gauss[i]));
        if (!s->gauss[i]) {
            for (; i >= 0; --i) {
                av_freep(&s->gauss[i]);
            }
            return AVERROR(ENOMEM);
        }
    }

    // Order 0
    av_log(ctx, AV_LOG_TRACE, "Setting 0-d gauss with filtersize = %d.\n", filtersize);
    sum1 = 0.0;
    if (!sigma) {
        s->gauss[0][0] = 1; // Copying data to double instead of convolution
    } else {
        for (i = 0; i < filtersize; ++i) {
            s->gauss[0][i] = exp(- pow(GINDX(filtersize, i), 2.) / (2 * sigma * sigma)) / ( sqrt(2 * M_PI) * sigma );
            sum1 += s->gauss[0][i];
        }
        for (i = 0; i < filtersize; ++i) {
            s->gauss[0][i] /= sum1;
        }
    }
    // Order 1
    if (difford > 0) {
        av_log(ctx, AV_LOG_TRACE, "Setting 1-d gauss with filtersize = %d.\n", filtersize);
        sum1 = 0.0;
        for (i = 0; i < filtersize; ++i) {
            s->gauss[1][i] = - (GINDX(filtersize, i) / pow(sigma, 2)) * s->gauss[0][i];
            sum1 += s->gauss[1][i] * GINDX(filtersize, i);
        }

        for (i = 0; i < filtersize; ++i) {
            s->gauss[1][i] /= sum1;
        }

        // Order 2
        if (difford > 1) {
            av_log(ctx, AV_LOG_TRACE, "Setting 2-d gauss with filtersize = %d.\n", filtersize);
            sum1 = 0.0;
            for (i = 0; i < filtersize; ++i) {
                s->gauss[2][i] = ( pow(GINDX(filtersize, i), 2) / pow(sigma, 4) - 1/pow(sigma, 2) )
                                 * s->gauss[0][i];
                sum1 += s->gauss[2][i];
            }

            sum2 = 0.0;
            for (i = 0; i < filtersize; ++i) {
                s->gauss[2][i] -= sum1 / (filtersize);
                sum2 += (0.5 * GINDX(filtersize, i) * GINDX(filtersize, i) * s->gauss[2][i]);
            }
            for (i = 0; i < filtersize ; ++i) {
                s->gauss[2][i] /= sum2;
            }
        }
    }
    return 0;
}

/**
 * Frees up buffers used by grey edge for storing derivatives final
 * and intermidiate results. Number of buffers and number of planes
 * for last buffer are given so it can be safely called at allocation
 * failure instances.
 *
 * @param td holds the buffers.
 * @param nb_buff number of buffers to be freed.
 * @param nb_planes number of planes for last buffer to be freed.
 */
static void cleanup_derivative_buffers(ThreadData *td, int nb_buff, int nb_planes)
{
    int b, p;

    for (b = 0; b < nb_buff; ++b) {
        for (p = 0; p < NUM_PLANES; ++p) {
            av_freep(&td->data[b][p]);
        }
    }
    // Final buffer may not be fully allocated at fail cases
    for (p = 0; p < nb_planes; ++p) {
        av_freep(&td->data[b][p]);
    }
}

/**
 * Allocates buffers used by grey edge for storing derivatives final
 * and intermidiate results.
 *
 * @param ctx the filter context.
 * @param td holds the buffers.
 *
 * @return 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure.
 */
static int setup_derivative_buffers(AVFilterContext* ctx, ThreadData *td)
{
    ColorConstancyContext *s = ctx->priv;
    int nb_buff = s->difford + 1;
    int b, p;

    av_log(ctx, AV_LOG_TRACE, "Allocating %d buffer(s) for grey edge.\n", nb_buff);
    for (b = 0; b <= nb_buff; ++b) { // We need difford + 1 buffers
        for (p = 0; p < NUM_PLANES; ++p) {
            td->data[b][p] = av_calloc(s->planeheight[p] * s->planewidth[p],
                                       sizeof(*td->data[b][p]));
            if (!td->data[b][p]) {
                cleanup_derivative_buffers(td, b + 1, p);
                return AVERROR(ENOMEM);
            }
        }
    }
    return 0;
}

#define CLAMP(x, mx) av_clip((x), 0, (mx-1))
#define INDX2D(r, c, w) ( (r) * (w) + (c) )
#define GAUSS(s, sr, sc, sls, sh, sw, g) ( (s)[ INDX2D(CLAMP((sr), (sh)), CLAMP((sc), (sw)), (sls)) ] * (g) )

/**
 * Slice calculation of gaussian derivatives. Applies 1-D gaussian derivative filter
 * either horizontally or vertically according to meta data given in thread data.
 * When convoluting horizontally source is always the in frame withing thread data
 * while when convoluting vertically source is a buffer.
 *
 * @param ctx the filter context.
 * @param arg data to be passed between threads.
 * @param jobnr current job nubmer.
 * @param nb_jobs total number of jobs.
 *
 * @return 0.
 */
static int slice_get_derivative(AVFilterContext* ctx, void* arg, int jobnr, int nb_jobs)
{
    ColorConstancyContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    const int ord = td->meta_data[INDEX_ORD];
    const int dir = td->meta_data[INDEX_DIR];
    const int src_index  = td->meta_data[INDEX_SRC];
    const int dst_index  = td->meta_data[INDEX_DST];
    const int filtersize = s->filtersize;
    const double *gauss  = s->gauss[ord];
    int plane;

    for (plane = 0; plane < NUM_PLANES; ++plane) {
        const int height      = s->planeheight[plane];
        const int width       = s->planewidth[plane];
        const int in_linesize = in->linesize[plane];
        double *dst = td->data[dst_index][plane];
        int slice_start, slice_end;
        int r, c, g;

        if (dir == DIR_X) {
            /** Applying gauss horizontally along each row */
            const uint8_t *src = in->data[plane];
            slice_start = (height * jobnr      ) / nb_jobs;
            slice_end   = (height * (jobnr + 1)) / nb_jobs;

            for (r = slice_start; r < slice_end; ++r) {
                for (c = 0; c < width; ++c) {
                    dst[INDX2D(r, c, width)] = 0;
                    for (g = 0; g < filtersize; ++g) {
                        dst[INDX2D(r, c, width)] += GAUSS(src, r,                        c + GINDX(filtersize, g),
                                                          in_linesize, height, width, gauss[g]);
                    }
                }
            }
        } else {
            /** Applying gauss vertically along each column */
            const double *src = td->data[src_index][plane];
            slice_start = (width * jobnr      ) / nb_jobs;
            slice_end   = (width * (jobnr + 1)) / nb_jobs;

            for (c = slice_start; c < slice_end; ++c) {
                for (r = 0; r < height; ++r) {
                    dst[INDX2D(r, c, width)] = 0;
                    for (g = 0; g < filtersize; ++g) {
                        dst[INDX2D(r, c, width)] += GAUSS(src, r + GINDX(filtersize, g), c,
                                                          width, height, width, gauss[g]);
                    }
                }
            }
        }

    }
    return 0;
}

/**
 * Slice Frobius normalization of gaussian derivatives. Only called for difford values of
 * 1 or 2.
 *
 * @param ctx the filter context.
 * @param arg data to be passed between threads.
 * @param jobnr current job nubmer.
 * @param nb_jobs total number of jobs.
 *
 * @return 0.
 */
static int slice_normalize(AVFilterContext* ctx, void* arg, int jobnr, int nb_jobs)
{
    ColorConstancyContext *s = ctx->priv;
    ThreadData *td = arg;
    const int difford = s->difford;
    int plane;

    for (plane = 0; plane < NUM_PLANES; ++plane) {
        const int height = s->planeheight[plane];
        const int width  = s->planewidth[plane];
        const int64_t numpixels = width * (int64_t)height;
        const int slice_start   = (numpixels * jobnr    ) / nb_jobs;
        const int slice_end     = (numpixels * (jobnr+1)) / nb_jobs;
        const double *dx = td->data[INDEX_DX][plane];
        const double *dy = td->data[INDEX_DY][plane];
        double *norm = td->data[INDEX_NORM][plane];
        int i;

        if (difford == 1) {
            for (i = slice_start; i < slice_end; ++i) {
                norm[i] = sqrt( pow(dx[i], 2) + pow(dy[i], 2));
            }
        } else {
            const double *dxy = td->data[INDEX_DXY][plane];
            for (i = slice_start; i < slice_end; ++i) {
                norm[i] = sqrt( pow(dx[i], 2) + 4 * pow(dxy[i], 2) + pow(dy[i], 2) );
            }
        }
    }

    return 0;
}

/**
 * Utility function for setting up differentiation data/metadata.
 *
 * @param ctx the filter context.
 * @param td to be used for passing data between threads.
 * @param ord ord of differentiation.
 * @param dir direction of differentiation.
 * @param src index of source used for differentiation.
 * @param dst index destination used for saving differentiation result.
 * @param dim maximum dimension in current direction.
 * @param nb_threads number of threads to use.
 */
static void av_always_inline
get_deriv(AVFilterContext *ctx, ThreadData *td, int ord, int dir,
          int src, int dst, int dim, int nb_threads) {
    td->meta_data[INDEX_ORD] = ord;
    td->meta_data[INDEX_DIR] = dir;
    td->meta_data[INDEX_SRC] = src;
    td->meta_data[INDEX_DST] = dst;
    ff_filter_execute(ctx, slice_get_derivative, td,
                      NULL, FFMIN(dim, nb_threads));
}

/**
 * Main control function for calculating gaussian derivatives.
 *
 * @param ctx the filter context.
 * @param td holds the buffers used for storing results.
 *
 * @return 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure.
 */
static int get_derivative(AVFilterContext *ctx, ThreadData *td)
{
    ColorConstancyContext *s = ctx->priv;
    int nb_threads = s->nb_threads;
    int height = s->planeheight[1];
    int width  = s->planewidth[1];

    switch(s->difford) {
    case 0:
        if (!s->sigma) { // Only copy once
            get_deriv(ctx, td, 0, DIR_X, 0         , INDEX_NORM, height, nb_threads);
        } else {
            get_deriv(ctx, td, 0, DIR_X, 0,          INDEX_TEMP, height, nb_threads);
            get_deriv(ctx, td, 0, DIR_Y, INDEX_TEMP, INDEX_NORM, width , nb_threads);
            // save to INDEX_NORM because this will not be normalied and
            // end gry edge filter expects result to be found in INDEX_NORM
        }
        return 0;

    case 1:
        get_deriv(ctx, td, 1, DIR_X, 0,          INDEX_TEMP, height, nb_threads);
        get_deriv(ctx, td, 0, DIR_Y, INDEX_TEMP, INDEX_DX,   width , nb_threads);

        get_deriv(ctx, td, 0, DIR_X, 0,          INDEX_TEMP, height, nb_threads);
        get_deriv(ctx, td, 1, DIR_Y, INDEX_TEMP, INDEX_DY,   width , nb_threads);
        return 0;

    case 2:
        get_deriv(ctx, td, 2, DIR_X, 0,          INDEX_TEMP, height, nb_threads);
        get_deriv(ctx, td, 0, DIR_Y, INDEX_TEMP, INDEX_DX,   width , nb_threads);

        get_deriv(ctx, td, 0, DIR_X, 0,          INDEX_TEMP, height, nb_threads);
        get_deriv(ctx, td, 2, DIR_Y, INDEX_TEMP, INDEX_DY,   width , nb_threads);

        get_deriv(ctx, td, 1, DIR_X, 0,          INDEX_TEMP, height, nb_threads);
        get_deriv(ctx, td, 1, DIR_Y, INDEX_TEMP, INDEX_DXY,  width , nb_threads);
        return 0;

    default:
        av_log(ctx, AV_LOG_ERROR, "Unsupported difford value: %d.\n", s->difford);
        return AVERROR(EINVAL);
    }

}

/**
 * Slice function for grey edge algorithm that does partial summing/maximizing
 * of gaussian derivatives.
 *
 * @param ctx the filter context.
 * @param arg data to be passed between threads.
 * @param jobnr current job nubmer.
 * @param nb_jobs total number of jobs.
 *
 * @return 0.
 */
static int filter_slice_grey_edge(AVFilterContext* ctx, void* arg, int jobnr, int nb_jobs)
{
    ColorConstancyContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in    = td->in;
    int minknorm   = s->minknorm;
    const uint8_t thresh = 255;
    int plane;

    for (plane = 0; plane < NUM_PLANES; ++plane) {
        const int height        = s->planeheight[plane];
        const int width         = s->planewidth[plane];
        const int in_linesize   = in->linesize[plane];
        const int slice_start   = (height * jobnr) / nb_jobs;
        const int slice_end     = (height * (jobnr+1)) / nb_jobs;
        const uint8_t *img_data = in->data[plane];
        const double *src       = td->data[INDEX_NORM][plane];
        double *dst             = td->data[INDEX_DST][plane];
        int r, c;

        dst[jobnr] = 0;
        if (!minknorm) {
            for (r = slice_start; r < slice_end; ++r) {
                for (c = 0; c < width; ++c) {
                    dst[jobnr] = FFMAX( dst[jobnr], fabs(src[INDX2D(r, c, width)])
                                        * (img_data[INDX2D(r, c, in_linesize)] < thresh) );
                }
            }
        } else {
            for (r = slice_start; r < slice_end; ++r) {
                for (c = 0; c < width; ++c) {
                    dst[jobnr] += ( pow( fabs(src[INDX2D(r, c, width)] / 255.), minknorm)
                                    * (img_data[INDX2D(r, c, in_linesize)] < thresh) );
                }
            }
        }
    }
    return 0;
}

/**
 * Main control function for grey edge algorithm.
 *
 * @param ctx the filter context.
 * @param in frame to perfrom grey edge on.
 *
 * @return 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure.
 */
static int filter_grey_edge(AVFilterContext *ctx, AVFrame *in)
{
    ColorConstancyContext *s = ctx->priv;
    ThreadData td;
    int minknorm  = s->minknorm;
    int difford   = s->difford;
    double *white = s->white;
    int nb_jobs   = FFMIN3(s->planeheight[1], s->planewidth[1], s->nb_threads);
    int plane, job, ret;

    td.in = in;
    ret = setup_derivative_buffers(ctx, &td);
    if (ret) {
        return ret;
    }
    get_derivative(ctx, &td);
    if (difford > 0) {
        ff_filter_execute(ctx, slice_normalize, &td, NULL, nb_jobs);
    }

    ff_filter_execute(ctx, filter_slice_grey_edge, &td, NULL, nb_jobs);
    if (!minknorm) {
        for (plane = 0; plane < NUM_PLANES; ++plane) {
            white[plane] = 0; // All values are absolute
            for (job = 0; job < nb_jobs; ++job) {
                white[plane] = FFMAX(white[plane] , td.data[INDEX_DST][plane][job]);
            }
        }
    } else {
        for (plane = 0; plane < NUM_PLANES; ++plane) {
            white[plane] = 0;
            for (job = 0; job < nb_jobs; ++job) {
                white[plane] += td.data[INDEX_DST][plane][job];
            }
            white[plane] = pow(white[plane], 1./minknorm);
        }
    }

    cleanup_derivative_buffers(&td, difford + 1, NUM_PLANES);
    return 0;
}

/**
 * Normalizes estimated illumination since only illumination vector
 * direction is required for color constancy.
 *
 * @param light the estimated illumination to be normalized in place
 */
static void normalize_light(double *light)
{
    double abs_val = pow( pow(light[0], 2.0) + pow(light[1], 2.0) + pow(light[2], 2.0), 0.5);
    int plane;

    // TODO: check if setting to 1.0 when estimated = 0.0 is the best thing to do

    if (!abs_val) {
        for (plane = 0; plane < NUM_PLANES; ++plane) {
            light[plane] = 1.0;
        }
    } else {
        for (plane = 0; plane < NUM_PLANES; ++plane) {
            light[plane] = (light[plane] / abs_val);
            if (!light[plane]) { // to avoid division by zero when correcting
                light[plane] = 1.0;
            }
        }
    }
}

/**
 * Redirects to corresponding algorithm estimation function and performs normalization
 * after estimation.
 *
 * @param ctx the filter context.
 * @param in frame to perfrom estimation on.
 *
 * @return 0 in case of success, a negative value corresponding to an
 * AVERROR code in case of failure.
 */
static int illumination_estimation(AVFilterContext *ctx, AVFrame *in)
{
    ColorConstancyContext *s = ctx->priv;
    int ret;

    ret = filter_grey_edge(ctx, in);

    av_log(ctx, AV_LOG_DEBUG, "Estimated illumination= %f %f %f\n",
           s->white[0], s->white[1], s->white[2]);
    normalize_light(s->white);
    av_log(ctx, AV_LOG_DEBUG, "Estimated illumination after normalization= %f %f %f\n",
           s->white[0], s->white[1], s->white[2]);

    return ret;
}

/**
 * Performs simple correction via diagonal transformation model.
 *
 * @param ctx the filter context.
 * @param arg data to be passed between threads.
 * @param jobnr current job nubmer.
 * @param nb_jobs total number of jobs.
 *
 * @return 0.
 */
static int diagonal_transformation(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ColorConstancyContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    int plane;

    for (plane = 0; plane < NUM_PLANES; ++plane) {
        const int height = s->planeheight[plane];
        const int width  = s->planewidth[plane];
        const int64_t numpixels = width * (int64_t)height;
        const int slice_start   = (numpixels * jobnr) / nb_jobs;
        const int slice_end     = (numpixels * (jobnr+1)) / nb_jobs;
        const uint8_t *src = in->data[plane];
        uint8_t *dst       = out->data[plane];
        double temp;
        unsigned i;

        for (i = slice_start; i < slice_end; ++i) {
            temp = src[i] / (s->white[plane] * SQRT3);
            dst[i] = av_clip_uint8((int)(temp + 0.5));
        }
    }
    return 0;
}

/**
 * Main control function for correcting scene illumination based on
 * estimated illumination.
 *
 * @param ctx the filter context.
 * @param in holds frame to correct
 * @param out holds corrected frame
 */
static void chromatic_adaptation(AVFilterContext *ctx, AVFrame *in, AVFrame *out)
{
    ColorConstancyContext *s = ctx->priv;
    ThreadData td;
    int nb_jobs = FFMIN3(s->planeheight[1], s->planewidth[1], s->nb_threads);

    td.in  = in;
    td.out = out;
    ff_filter_execute(ctx, diagonal_transformation, &td, NULL, nb_jobs);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorConstancyContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const double break_off_sigma = 3.0;
    double sigma = s->sigma;
    int ret;

    if (!floor(break_off_sigma * sigma + 0.5) && s->difford) {
        av_log(ctx, AV_LOG_ERROR, "floor(%f * sigma) must be > 0 when difford > 0.\n", break_off_sigma);
        return AVERROR(EINVAL);
    }

    s->filtersize = 2 * floor(break_off_sigma * sigma + 0.5) + 1;
    if (ret=set_gauss(ctx)) {
        return ret;
    }

    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int ret;
    int direct = 0;

    ret = illumination_estimation(ctx, in);
    if (ret) {
        av_frame_free(&in);
        return ret;
    }

    if (av_frame_is_writable(in)) {
        direct = 1;
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }
    chromatic_adaptation(ctx, in, out);

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ColorConstancyContext *s = ctx->priv;
    int difford = s->difford;
    int i;

    for (i = 0; i <= difford; ++i) {
        av_freep(&s->gauss[i]);
    }
}

static const AVFilterPad colorconstancy_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad colorconstancy_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

#if CONFIG_GREYEDGE_FILTER

static const AVOption greyedge_options[] = {
    { "difford",  "set differentiation order", OFFSET(difford),  AV_OPT_TYPE_INT,    {.i64=1}, 0,   2,      FLAGS },
    { "minknorm", "set Minkowski norm",        OFFSET(minknorm), AV_OPT_TYPE_INT,    {.i64=1}, 0,   20,     FLAGS },
    { "sigma",    "set sigma",                 OFFSET(sigma),    AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.0, 1024.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(greyedge);

const AVFilter ff_vf_greyedge = {
    .name          = GREY_EDGE,
    .description   = NULL_IF_CONFIG_SMALL("Estimates scene illumination by grey edge assumption."),
    .priv_size     = sizeof(ColorConstancyContext),
    .priv_class    = &greyedge_class,
    .uninit        = uninit,
    FILTER_INPUTS(colorconstancy_inputs),
    FILTER_OUTPUTS(colorconstancy_outputs),
    // TODO: support more formats
    // FIXME: error when saving to .jpg
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_GBRP),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};

#endif /* CONFIG_GREY_EDGE_FILTER */
