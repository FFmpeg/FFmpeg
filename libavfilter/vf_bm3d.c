/*
 * Copyright (c) 2015-2016 mawen1250
 * Copyright (c) 2018 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @todo
 * - non-power of 2 DCT
 * - opponent color space
 * - temporal support
 */

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/avfft.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

#define MAX_NB_THREADS 32

enum FilterModes {
    BASIC,
    FINAL,
    NB_MODES,
};

typedef struct ThreadData {
    const uint8_t *src;
    int src_linesize;
    const uint8_t *ref;
    int ref_linesize;
    int plane;
} ThreadData;

typedef struct PosCode {
    int x, y;
} PosCode;

typedef struct PosPairCode {
    double score;
    int x, y;
} PosPairCode;

typedef struct SliceContext {
    DCTContext *gdctf, *gdcti;
    DCTContext *dctf, *dcti;
    FFTSample *bufferh;
    FFTSample *bufferv;
    FFTSample *bufferz;
    FFTSample *buffer;
    FFTSample *rbufferh;
    FFTSample *rbufferv;
    FFTSample *rbufferz;
    FFTSample *rbuffer;
    float *num, *den;
    PosPairCode match_blocks[256];
    int nb_match_blocks;
    PosCode *search_positions;
} SliceContext;

typedef struct BM3DContext {
    const AVClass *class;

    float sigma;
    int block_size;
    int block_step;
    int group_size;
    int bm_range;
    int bm_step;
    float th_mse;
    float hard_threshold;
    int mode;
    int ref;
    int planes;

    int depth;
    int max;
    int nb_planes;
    int planewidth[4];
    int planeheight[4];
    int group_bits;
    int pgroup_size;

    SliceContext slices[MAX_NB_THREADS];

    FFFrameSync fs;
    int nb_threads;

    void (*get_block_row)(const uint8_t *srcp, int src_linesize,
                          int y, int x, int block_size, float *dst);
    double (*do_block_ssd)(struct BM3DContext *s, PosCode *pos,
                           const uint8_t *src, int src_stride,
                           int r_y, int r_x);
    void (*do_output)(struct BM3DContext *s, uint8_t *dst, int dst_linesize,
                      int plane, int nb_jobs);
    void (*block_filtering)(struct BM3DContext *s,
                            const uint8_t *src, int src_linesize,
                            const uint8_t *ref, int ref_linesize,
                            int y, int x, int plane, int jobnr);
} BM3DContext;

#define OFFSET(x) offsetof(BM3DContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption bm3d_options[] = {
    { "sigma",  "set denoising strength",
        OFFSET(sigma),          AV_OPT_TYPE_FLOAT, {.dbl=1},     0,      99999.9, FLAGS },
    { "block",  "set log2(size) of local patch",
        OFFSET(block_size),     AV_OPT_TYPE_INT,   {.i64=4},     4,            6, FLAGS },
    { "bstep",  "set sliding step for processing blocks",
        OFFSET(block_step),     AV_OPT_TYPE_INT,   {.i64=4},     1,           64, FLAGS },
    { "group",  "set maximal number of similar blocks",
        OFFSET(group_size),     AV_OPT_TYPE_INT,   {.i64=1},     1,          256, FLAGS },
    { "range",  "set block matching range",
        OFFSET(bm_range),       AV_OPT_TYPE_INT,   {.i64=9},     1,    INT32_MAX, FLAGS },
    { "mstep",  "set step for block matching",
        OFFSET(bm_step),        AV_OPT_TYPE_INT,   {.i64=1},     1,           64, FLAGS },
    { "thmse",  "set threshold of mean square error for block matching",
        OFFSET(th_mse),         AV_OPT_TYPE_FLOAT, {.dbl=0},     0,    INT32_MAX, FLAGS },
    { "hdthr",  "set hard threshold for 3D transfer domain",
        OFFSET(hard_threshold), AV_OPT_TYPE_FLOAT, {.dbl=2.7},   0,    INT32_MAX, FLAGS },
    { "estim",  "set filtering estimation mode",
        OFFSET(mode),           AV_OPT_TYPE_INT,   {.i64=BASIC}, 0,   NB_MODES-1, FLAGS, "mode" },
    { "basic",  "basic estimate",
        0,                      AV_OPT_TYPE_CONST, {.i64=BASIC}, 0,            0, FLAGS, "mode" },
    { "final",  "final estimate",
        0,                      AV_OPT_TYPE_CONST, {.i64=FINAL}, 0,            0, FLAGS, "mode" },
    { "ref",    "have reference stream",
        OFFSET(ref),            AV_OPT_TYPE_INT,    {.i64=0},    0,            1, FLAGS },
    { "planes", "set planes to filter",
        OFFSET(planes),         AV_OPT_TYPE_INT,   {.i64=7},     0,           15, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(bm3d);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10,
        AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
        AV_PIX_FMT_YUV440P10,
        AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_YUV440P12,
        AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
        AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int do_search_boundary(int pos, int plane_boundary, int search_range, int search_step)
{
    int search_boundary;

    search_range = search_range / search_step * search_step;

    if (pos == plane_boundary) {
        search_boundary = plane_boundary;
    } else if (pos > plane_boundary) {
        search_boundary = pos - search_range;

        while (search_boundary < plane_boundary) {
            search_boundary += search_step;
        }
    } else {
        search_boundary = pos + search_range;

        while (search_boundary > plane_boundary) {
            search_boundary -= search_step;
        }
    }

    return search_boundary;
}

static int search_boundary(int plane_boundary, int search_range, int search_step, int vertical, int y, int x)
{
    return do_search_boundary(vertical ? y : x, plane_boundary, search_range, search_step);
}

static int cmp_scores(const void *a, const void *b)
{
    const struct PosPairCode *pair1 = a;
    const struct PosPairCode *pair2 = b;
    return FFDIFFSIGN(pair1->score, pair2->score);
}

static double do_block_ssd(BM3DContext *s, PosCode *pos, const uint8_t *src, int src_stride, int r_y, int r_x)
{
    const uint8_t *srcp = src + pos->y * src_stride + pos->x;
    const uint8_t *refp = src + r_y * src_stride + r_x;
    const int block_size = s->block_size;
    double dist = 0.;
    int x, y;

    for (y = 0; y < block_size; y++) {
        for (x = 0; x < block_size; x++) {
            double temp = refp[x] - srcp[x];
            dist += temp * temp;
        }

        srcp += src_stride;
        refp += src_stride;
    }

    return dist;
}

static double do_block_ssd16(BM3DContext *s, PosCode *pos, const uint8_t *src, int src_stride, int r_y, int r_x)
{
    const uint16_t *srcp = (uint16_t *)src + pos->y * src_stride / 2 + pos->x;
    const uint16_t *refp = (uint16_t *)src + r_y * src_stride / 2 + r_x;
    const int block_size = s->block_size;
    double dist = 0.;
    int x, y;

    for (y = 0; y < block_size; y++) {
        for (x = 0; x < block_size; x++) {
            double temp = refp[x] - srcp[x];
            dist += temp * temp;
        }

        srcp += src_stride / 2;
        refp += src_stride / 2;
    }

    return dist;
}

static void do_block_matching_multi(BM3DContext *s, const uint8_t *src, int src_stride, int src_range,
                                    const PosCode *search_pos, int search_size, float th_mse,
                                    int r_y, int r_x, int plane, int jobnr)
{
    SliceContext *sc = &s->slices[jobnr];
    double MSE2SSE = s->group_size * s->block_size * s->block_size * src_range * src_range / (s->max * s->max);
    double distMul = 1. / MSE2SSE;
    double th_sse = th_mse * MSE2SSE;
    int i, index = sc->nb_match_blocks;

    for (i = 0; i < search_size; i++) {
        PosCode pos = search_pos[i];
        double dist;

        dist = s->do_block_ssd(s, &pos, src, src_stride, r_y, r_x);

        // Only match similar blocks but not identical blocks
        if (dist <= th_sse && dist != 0) {
            const double score = dist * distMul;

            if (index >= s->group_size && score >= sc->match_blocks[index - 1].score) {
                continue;
            }

            if (index >= s->group_size)
                index = s->group_size - 1;

            sc->match_blocks[index].score = score;
            sc->match_blocks[index].y = pos.y;
            sc->match_blocks[index].x = pos.x;
            index++;
            qsort(sc->match_blocks, index, sizeof(PosPairCode), cmp_scores);
        }
    }

    sc->nb_match_blocks = index;
}

static void block_matching_multi(BM3DContext *s, const uint8_t *ref, int ref_linesize, int y, int x,
                                 int exclude_cur_pos, int plane, int jobnr)
{
    SliceContext *sc = &s->slices[jobnr];
    const int width = s->planewidth[plane];
    const int height = s->planeheight[plane];
    const int block_size = s->block_size;
    const int step = s->bm_step;
    const int range = s->bm_range / step * step;
    int l = search_boundary(0, range, step, 0, y, x);
    int r = search_boundary(width - block_size, range, step, 0, y, x);
    int t = search_boundary(0, range, step, 1, y, x);
    int b = search_boundary(height - block_size, range, step, 1, y, x);
    int j, i, index = 0;

    for (j = t; j <= b; j += step) {
        for (i = l; i <= r; i += step) {
            PosCode pos;

            if (exclude_cur_pos > 0 && j == y && i == x) {
                continue;
            }

            pos.y = j;
            pos.x = i;
            sc->search_positions[index++] = pos;
        }
    }

    if (exclude_cur_pos == 1) {
        sc->match_blocks[0].score = 0;
        sc->match_blocks[0].y = y;
        sc->match_blocks[0].x = x;
        sc->nb_match_blocks = 1;
    }

    do_block_matching_multi(s, ref, ref_linesize, s->bm_range,
                            sc->search_positions, index, s->th_mse, y, x, plane, jobnr);
}

static void block_matching(BM3DContext *s, const uint8_t *ref, int ref_linesize,
                           int j, int i, int plane, int jobnr)
{
    SliceContext *sc = &s->slices[jobnr];

    if (s->group_size == 1 || s->th_mse <= 0.f) {
        sc->match_blocks[0].score = 1;
        sc->match_blocks[0].x = i;
        sc->match_blocks[0].y = j;
        sc->nb_match_blocks = 1;
        return;
    }

    sc->nb_match_blocks = 0;
    block_matching_multi(s, ref, ref_linesize, j, i, 1, plane, jobnr);
}

static void get_block_row(const uint8_t *srcp, int src_linesize,
                          int y, int x, int block_size, float *dst)
{
    const uint8_t *src = srcp + y * src_linesize + x;
    int j;

    for (j = 0; j < block_size; j++) {
        dst[j] = src[j];
    }
}

static void get_block_row16(const uint8_t *srcp, int src_linesize,
                            int y, int x, int block_size, float *dst)
{
    const uint16_t *src = (uint16_t *)srcp + y * src_linesize / 2 + x;
    int j;

    for (j = 0; j < block_size; j++) {
        dst[j] = src[j];
    }
}

static void basic_block_filtering(BM3DContext *s, const uint8_t *src, int src_linesize,
                                  const uint8_t *ref, int ref_linesize,
                                  int y, int x, int plane, int jobnr)
{
    SliceContext *sc = &s->slices[jobnr];
    const int buffer_linesize = s->block_size * s->block_size;
    const int nb_match_blocks = sc->nb_match_blocks;
    const int block_size = s->block_size;
    const int width = s->planewidth[plane];
    const int pgroup_size = s->pgroup_size;
    const int group_size = s->group_size;
    float *buffer = sc->buffer;
    float *bufferh = sc->bufferh;
    float *bufferv = sc->bufferv;
    float *bufferz = sc->bufferz;
    float threshold[4];
    float den_weight, num_weight;
    int retained = 0;
    int i, j, k;

    for (k = 0; k < nb_match_blocks; k++) {
        const int y = sc->match_blocks[k].y;
        const int x = sc->match_blocks[k].x;

        for (i = 0; i < block_size; i++) {
            s->get_block_row(src, src_linesize, y + i, x, block_size, bufferh + block_size * i);
            av_dct_calc(sc->dctf, bufferh + block_size * i);
        }

        for (i = 0; i < block_size; i++) {
            for (j = 0; j < block_size; j++) {
                bufferv[i * block_size + j] = bufferh[j * block_size + i];
            }
            av_dct_calc(sc->dctf, bufferv + i * block_size);
        }

        for (i = 0; i < block_size; i++) {
            memcpy(buffer + k * buffer_linesize + i * block_size,
                   bufferv + i * block_size, block_size * 4);
        }
    }

    for (i = 0; i < block_size; i++) {
        for (j = 0; j < block_size; j++) {
            for (k = 0; k < nb_match_blocks; k++)
                bufferz[k] = buffer[buffer_linesize * k + i * block_size + j];
            if (group_size > 1)
                av_dct_calc(sc->gdctf, bufferz);
            bufferz += pgroup_size;
        }
    }

    threshold[0] = s->hard_threshold * s->sigma;
    threshold[1] = threshold[0] * sqrtf(2.f);
    threshold[2] = threshold[0] * 2.f;
    threshold[3] = threshold[0] * sqrtf(8.f);
    bufferz = sc->bufferz;

    for (i = 0; i < block_size; i++) {
        for (j = 0; j < block_size; j++) {
            for (k = 0; k < nb_match_blocks; k++) {
                const float thresh = threshold[(j == 0) + (i == 0) + (k == 0)];

                if (bufferz[k] > thresh || bufferz[k] < -thresh) {
                    retained++;
                } else {
                    bufferz[k] = 0;
                }
            }
            bufferz += pgroup_size;
        }
    }

    bufferz = sc->bufferz;
    buffer = sc->buffer;
    for (i = 0; i < block_size; i++) {
        for (j = 0; j < block_size; j++) {
            if (group_size > 1)
                av_dct_calc(sc->gdcti, bufferz);
            for (k = 0; k < nb_match_blocks; k++) {
                buffer[buffer_linesize * k + i * block_size + j] = bufferz[k];
            }
            bufferz += pgroup_size;
        }
    }

    den_weight = retained < 1 ? 1.f : 1.f / retained;
    num_weight = den_weight;

    buffer = sc->buffer;
    for (k = 0; k < nb_match_blocks; k++) {
        float *num = sc->num + y * width + x;
        float *den = sc->den + y * width + x;

        for (i = 0; i < block_size; i++) {
            memcpy(bufferv + i * block_size,
                   buffer + k * buffer_linesize + i * block_size,
                   block_size * 4);
        }

        for (i = 0; i < block_size; i++) {
            av_dct_calc(sc->dcti, bufferv + block_size * i);
            for (j = 0; j < block_size; j++) {
                bufferh[j * block_size + i] = bufferv[i * block_size + j];
            }
        }

        for (i = 0; i < block_size; i++) {
            av_dct_calc(sc->dcti, bufferh + block_size * i);
            for (j = 0; j < block_size; j++) {
                num[j] += bufferh[i * block_size + j] * num_weight;
                den[j] += den_weight;
            }
            num += width;
            den += width;
        }
    }
}

static void final_block_filtering(BM3DContext *s, const uint8_t *src, int src_linesize,
                                  const uint8_t *ref, int ref_linesize,
                                  int y, int x, int plane, int jobnr)
{
    SliceContext *sc = &s->slices[jobnr];
    const int buffer_linesize = s->block_size * s->block_size;
    const int nb_match_blocks = sc->nb_match_blocks;
    const int block_size = s->block_size;
    const int width = s->planewidth[plane];
    const int pgroup_size = s->pgroup_size;
    const int group_size = s->group_size;
    const float sigma_sqr = s->sigma * s->sigma;
    float *buffer = sc->buffer;
    float *bufferh = sc->bufferh;
    float *bufferv = sc->bufferv;
    float *bufferz = sc->bufferz;
    float *rbuffer = sc->rbuffer;
    float *rbufferh = sc->rbufferh;
    float *rbufferv = sc->rbufferv;
    float *rbufferz = sc->rbufferz;
    float den_weight, num_weight;
    float l2_wiener = 0;
    int i, j, k;

    for (k = 0; k < nb_match_blocks; k++) {
        const int y = sc->match_blocks[k].y;
        const int x = sc->match_blocks[k].x;

        for (i = 0; i < block_size; i++) {
            s->get_block_row(src, src_linesize, y + i, x, block_size, bufferh + block_size * i);
            s->get_block_row(ref, ref_linesize, y + i, x, block_size, rbufferh + block_size * i);
            av_dct_calc(sc->dctf, bufferh + block_size * i);
            av_dct_calc(sc->dctf, rbufferh + block_size * i);
        }

        for (i = 0; i < block_size; i++) {
            for (j = 0; j < block_size; j++) {
                bufferv[i * block_size + j] = bufferh[j * block_size + i];
                rbufferv[i * block_size + j] = rbufferh[j * block_size + i];
            }
            av_dct_calc(sc->dctf, bufferv + i * block_size);
            av_dct_calc(sc->dctf, rbufferv + i * block_size);
        }

        for (i = 0; i < block_size; i++) {
            memcpy(buffer + k * buffer_linesize + i * block_size,
                   bufferv + i * block_size, block_size * 4);
            memcpy(rbuffer + k * buffer_linesize + i * block_size,
                   rbufferv + i * block_size, block_size * 4);
        }
    }

    for (i = 0; i < block_size; i++) {
        for (j = 0; j < block_size; j++) {
            for (k = 0; k < nb_match_blocks; k++) {
                bufferz[k] = buffer[buffer_linesize * k + i * block_size + j];
                rbufferz[k] = rbuffer[buffer_linesize * k + i * block_size + j];
            }
            if (group_size > 1) {
                av_dct_calc(sc->gdctf, bufferz);
                av_dct_calc(sc->gdctf, rbufferz);
            }
            bufferz += pgroup_size;
            rbufferz += pgroup_size;
        }
    }

    bufferz = sc->bufferz;
    rbufferz = sc->rbufferz;

    for (i = 0; i < block_size; i++) {
        for (j = 0; j < block_size; j++) {
            for (k = 0; k < nb_match_blocks; k++) {
                const float ref_sqr = rbufferz[k] * rbufferz[k];
                float wiener_coef = ref_sqr / (ref_sqr + sigma_sqr);

                if (isnan(wiener_coef))
                   wiener_coef = 1;
                bufferz[k] *= wiener_coef;
                l2_wiener += wiener_coef * wiener_coef;
            }
            bufferz += pgroup_size;
            rbufferz += pgroup_size;
        }
    }

    bufferz = sc->bufferz;
    buffer = sc->buffer;
    for (i = 0; i < block_size; i++) {
        for (j = 0; j < block_size; j++) {
            if (group_size > 1)
                av_dct_calc(sc->gdcti, bufferz);
            for (k = 0; k < nb_match_blocks; k++) {
                buffer[buffer_linesize * k + i * block_size + j] = bufferz[k];
            }
            bufferz += pgroup_size;
        }
    }

    l2_wiener = FFMAX(l2_wiener, 1e-15f);
    den_weight = 1.f / l2_wiener;
    num_weight = den_weight;

    for (k = 0; k < nb_match_blocks; k++) {
        float *num = sc->num + y * width + x;
        float *den = sc->den + y * width + x;

        for (i = 0; i < block_size; i++) {
            memcpy(bufferv + i * block_size,
                   buffer + k * buffer_linesize + i * block_size,
                   block_size * 4);
        }

        for (i = 0; i < block_size; i++) {
            av_dct_calc(sc->dcti, bufferv + block_size * i);
            for (j = 0; j < block_size; j++) {
                bufferh[j * block_size + i] = bufferv[i * block_size + j];
            }
        }

        for (i = 0; i < block_size; i++) {
            av_dct_calc(sc->dcti, bufferh + block_size * i);
            for (j = 0; j < block_size; j++) {
                num[j] += bufferh[i * block_size + j] * num_weight;
                den[j] += den_weight;
            }
            num += width;
            den += width;
        }
    }
}

static void do_output(BM3DContext *s, uint8_t *dst, int dst_linesize,
                      int plane, int nb_jobs)
{
    const int height = s->planeheight[plane];
    const int width = s->planewidth[plane];
    int i, j, k;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            uint8_t *dstp = dst + i * dst_linesize;
            float sum_den = 0.f;
            float sum_num = 0.f;

            for (k = 0; k < nb_jobs; k++) {
                SliceContext *sc = &s->slices[k];
                float num = sc->num[i * width + j];
                float den = sc->den[i * width + j];

                sum_num += num;
                sum_den += den;
            }

            dstp[j] = av_clip_uint8(lrintf(sum_num / sum_den));
        }
    }
}

static void do_output16(BM3DContext *s, uint8_t *dst, int dst_linesize,
                        int plane, int nb_jobs)
{
    const int height = s->planeheight[plane];
    const int width = s->planewidth[plane];
    const int depth = s->depth;
    int i, j, k;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            uint16_t *dstp = (uint16_t *)dst + i * dst_linesize / 2;
            float sum_den = 0.f;
            float sum_num = 0.f;

            for (k = 0; k < nb_jobs; k++) {
                SliceContext *sc = &s->slices[k];
                float num = sc->num[i * width + j];
                float den = sc->den[i * width + j];

                sum_num += num;
                sum_den += den;
            }

            dstp[j] = av_clip_uintp2_c(lrintf(sum_num / sum_den), depth);
        }
    }
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    BM3DContext *s = ctx->priv;
    SliceContext *sc = &s->slices[jobnr];
    const int block_step = s->block_step;
    ThreadData *td = arg;
    const uint8_t *src = td->src;
    const uint8_t *ref = td->ref;
    const int src_linesize = td->src_linesize;
    const int ref_linesize = td->ref_linesize;
    const int plane = td->plane;
    const int width = s->planewidth[plane];
    const int height = s->planeheight[plane];
    const int block_pos_bottom = FFMAX(0, height - s->block_size);
    const int block_pos_right  = FFMAX(0, width - s->block_size);
    const int slice_start = (((height + block_step - 1) / block_step) * jobnr / nb_jobs) * block_step;
    const int slice_end = (jobnr == nb_jobs - 1) ? block_pos_bottom + block_step :
                          (((height + block_step - 1) / block_step) * (jobnr + 1) / nb_jobs) * block_step;
    int i, j;

    memset(sc->num, 0, width * height * sizeof(FFTSample));
    memset(sc->den, 0, width * height * sizeof(FFTSample));

    for (j = slice_start; j < slice_end; j += block_step) {
        if (j > block_pos_bottom) {
            j = block_pos_bottom;
        }

        for (i = 0; i < block_pos_right + block_step; i += block_step) {
            if (i > block_pos_right) {
                i = block_pos_right;
            }

            block_matching(s, ref, ref_linesize, j, i, plane, jobnr);

            s->block_filtering(s, src, src_linesize,
                               ref, ref_linesize, j, i, plane, jobnr);
        }
    }

    return 0;
}

static int filter_frame(AVFilterContext *ctx, AVFrame **out, AVFrame *in, AVFrame *ref)
{
    BM3DContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int p;

    *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!*out)
        return AVERROR(ENOMEM);
    av_frame_copy_props(*out, in);

    for (p = 0; p < s->nb_planes; p++) {
        const int nb_jobs = FFMAX(1, FFMIN(s->nb_threads, s->planeheight[p] / s->block_size));
        ThreadData td;

        if (!((1 << p) & s->planes) || ctx->is_disabled) {
            av_image_copy_plane((*out)->data[p], (*out)->linesize[p],
                                in->data[p], in->linesize[p],
                                s->planewidth[p], s->planeheight[p]);
            continue;
        }

        td.src = in->data[p];
        td.src_linesize = in->linesize[p];
        td.ref = ref->data[p];
        td.ref_linesize = ref->linesize[p];
        td.plane = p;
        ctx->internal->execute(ctx, filter_slice, &td, NULL, nb_jobs);

        s->do_output(s, (*out)->data[p], (*out)->linesize[p], p, nb_jobs);
    }

    return 0;
}

#define SQR(x) ((x) * (x))

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    BM3DContext *s = ctx->priv;
    int i, group_bits;

    s->nb_threads = FFMIN(ff_filter_get_nb_threads(ctx), MAX_NB_THREADS);
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    s->depth = desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    for (group_bits = 4; 1 << group_bits < s->group_size; group_bits++);
    s->group_bits = group_bits;
    s->pgroup_size = 1 << group_bits;

    for (i = 0; i < s->nb_threads; i++) {
        SliceContext *sc = &s->slices[i];

        sc->num = av_calloc(FFALIGN(s->planewidth[0], s->block_size) * FFALIGN(s->planeheight[0], s->block_size), sizeof(FFTSample));
        sc->den = av_calloc(FFALIGN(s->planewidth[0], s->block_size) * FFALIGN(s->planeheight[0], s->block_size), sizeof(FFTSample));
        if (!sc->num || !sc->den)
            return AVERROR(ENOMEM);

        sc->dctf = av_dct_init(av_log2(s->block_size), DCT_II);
        sc->dcti = av_dct_init(av_log2(s->block_size), DCT_III);
        if (!sc->dctf || !sc->dcti)
            return AVERROR(ENOMEM);

        if (s->group_bits > 1) {
            sc->gdctf = av_dct_init(s->group_bits, DCT_II);
            sc->gdcti = av_dct_init(s->group_bits, DCT_III);
            if (!sc->gdctf || !sc->gdcti)
                return AVERROR(ENOMEM);
        }

        sc->buffer = av_calloc(s->block_size * s->block_size * s->pgroup_size, sizeof(*sc->buffer));
        sc->bufferz = av_calloc(s->block_size * s->block_size * s->pgroup_size, sizeof(*sc->bufferz));
        sc->bufferh = av_calloc(s->block_size * s->block_size, sizeof(*sc->bufferh));
        sc->bufferv = av_calloc(s->block_size * s->block_size, sizeof(*sc->bufferv));
        if (!sc->bufferh || !sc->bufferv || !sc->buffer || !sc->bufferz)
            return AVERROR(ENOMEM);

        if (s->mode == FINAL) {
            sc->rbuffer = av_calloc(s->block_size * s->block_size * s->pgroup_size, sizeof(*sc->rbuffer));
            sc->rbufferz = av_calloc(s->block_size * s->block_size * s->pgroup_size, sizeof(*sc->rbufferz));
            sc->rbufferh = av_calloc(s->block_size * s->block_size, sizeof(*sc->rbufferh));
            sc->rbufferv = av_calloc(s->block_size * s->block_size, sizeof(*sc->rbufferv));
            if (!sc->rbufferh || !sc->rbufferv || !sc->rbuffer || !sc->rbufferz)
                return AVERROR(ENOMEM);
        }

        sc->search_positions = av_calloc(SQR(2 * s->bm_range / s->bm_step + 1), sizeof(*sc->search_positions));
        if (!sc->search_positions)
            return AVERROR(ENOMEM);
    }

    s->do_output = do_output;
    s->do_block_ssd = do_block_ssd;
    s->get_block_row = get_block_row;

    if (s->depth > 8) {
        s->do_output = do_output16;
        s->do_block_ssd = do_block_ssd16;
        s->get_block_row = get_block_row16;
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    BM3DContext *s = ctx->priv;

    if (!s->ref) {
        AVFrame *frame = NULL;
        AVFrame *out = NULL;
        int ret, status;
        int64_t pts;

        FF_FILTER_FORWARD_STATUS_BACK(ctx->outputs[0], ctx->inputs[0]);

        if ((ret = ff_inlink_consume_frame(ctx->inputs[0], &frame)) > 0) {
            ret = filter_frame(ctx, &out, frame, frame);
            av_frame_free(&frame);
            if (ret < 0)
                return ret;
            ret = ff_filter_frame(ctx->outputs[0], out);
        }
        if (ret < 0) {
            return ret;
        } else if (ff_inlink_acknowledge_status(ctx->inputs[0], &status, &pts)) {
            ff_outlink_set_status(ctx->outputs[0], status, pts);
            return 0;
        } else {
            if (ff_outlink_frame_wanted(ctx->outputs[0]))
                ff_inlink_request_frame(ctx->inputs[0]);
            return 0;
        }
    } else {
        return ff_framesync_activate(&s->fs);
    }
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    BM3DContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = NULL, *src, *ref;
    int ret;

    if ((ret = ff_framesync_get_frame(&s->fs, 0, &src, 0)) < 0 ||
        (ret = ff_framesync_get_frame(&s->fs, 1, &ref, 0)) < 0)
        return ret;

    if ((ret = filter_frame(ctx, &out, src, ref)) < 0)
        return ret;

    out->pts = av_rescale_q(src->pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    BM3DContext *s = ctx->priv;
    AVFilterPad pad = { 0 };
    int ret;

    if (s->mode == BASIC) {
        if (s->th_mse == 0.f)
            s->th_mse = 400.f + s->sigma * 80.f;
        s->block_filtering = basic_block_filtering;
    } else if (s->mode == FINAL) {
        if (!s->ref) {
            av_log(ctx, AV_LOG_WARNING, "Reference stream is mandatory in final estimation mode.\n");
            s->ref = 1;
        }
        if (s->th_mse == 0.f)
            s->th_mse = 200.f + s->sigma * 10.f;

        s->block_filtering = final_block_filtering;
    } else {
        return AVERROR_BUG;
    }

    s->block_size = 1 << s->block_size;

    if (s->block_step > s->block_size) {
        av_log(ctx, AV_LOG_WARNING, "bstep: %d can't be bigger than block size. Changing to %d.\n",
               s->block_step, s->block_size);
        s->block_step = s->block_size;
    }
    if (s->bm_step > s->bm_range) {
        av_log(ctx, AV_LOG_WARNING, "mstep: %d can't be bigger than block matching range. Changing to %d.\n",
               s->bm_step, s->bm_range);
        s->bm_step = s->bm_range;
    }

    pad.type         = AVMEDIA_TYPE_VIDEO;
    pad.name         = av_strdup("source");
    pad.config_props = config_input;
    if (!pad.name)
        return AVERROR(ENOMEM);

    if ((ret = ff_insert_inpad(ctx, 0, &pad)) < 0) {
        av_freep(&pad.name);
        return ret;
    }

    if (s->ref) {
        pad.type         = AVMEDIA_TYPE_VIDEO;
        pad.name         = av_strdup("reference");
        pad.config_props = NULL;
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_insert_inpad(ctx, 1, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    BM3DContext *s = ctx->priv;
    AVFilterLink *src = ctx->inputs[0];
    AVFilterLink *ref;
    FFFrameSyncIn *in;
    int ret;

    if (s->ref) {
        ref = ctx->inputs[1];

        if (src->format != ref->format) {
            av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
            return AVERROR(EINVAL);
        }
        if (src->w                       != ref->w ||
            src->h                       != ref->h) {
            av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
                   "(size %dx%d) do not match the corresponding "
                   "second input link %s parameters (%dx%d) ",
                   ctx->input_pads[0].name, src->w, src->h,
                   ctx->input_pads[1].name, ref->w, ref->h);
            return AVERROR(EINVAL);
        }
    }

    outlink->w = src->w;
    outlink->h = src->h;
    outlink->time_base = src->time_base;
    outlink->sample_aspect_ratio = src->sample_aspect_ratio;
    outlink->frame_rate = src->frame_rate;

    if (!s->ref)
        return 0;

    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = src->time_base;
    in[1].time_base = ref->time_base;
    in[0].sync   = 1;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_STOP;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_STOP;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    return ff_framesync_configure(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BM3DContext *s = ctx->priv;
    int i;

    for (i = 0; i < ctx->nb_inputs; i++)
        av_freep(&ctx->input_pads[i].name);

    if (s->ref)
        ff_framesync_uninit(&s->fs);

    for (i = 0; i < s->nb_threads; i++) {
        SliceContext *sc = &s->slices[i];

        av_freep(&sc->num);
        av_freep(&sc->den);

        av_dct_end(sc->gdctf);
        av_dct_end(sc->gdcti);
        av_dct_end(sc->dctf);
        av_dct_end(sc->dcti);

        av_freep(&sc->buffer);
        av_freep(&sc->bufferh);
        av_freep(&sc->bufferv);
        av_freep(&sc->bufferz);
        av_freep(&sc->rbuffer);
        av_freep(&sc->rbufferh);
        av_freep(&sc->rbufferv);
        av_freep(&sc->rbufferz);

        av_freep(&sc->search_positions);
    }
}

static const AVFilterPad bm3d_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_bm3d = {
    .name          = "bm3d",
    .description   = NULL_IF_CONFIG_SMALL("Block-Matching 3D denoiser."),
    .priv_size     = sizeof(BM3DContext),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .query_formats = query_formats,
    .inputs        = NULL,
    .outputs       = bm3d_outputs,
    .priv_class    = &bm3d_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                     AVFILTER_FLAG_DYNAMIC_INPUTS |
                     AVFILTER_FLAG_SLICE_THREADS,
};
