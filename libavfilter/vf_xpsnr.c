/*
 * Copyright (c) 2024 Christian R. Helmrich
 * Copyright (c) 2024 Christian Lehmann
 * Copyright (c) 2024 Christian Stoffers
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
 * Calculate the extended perceptually weighted PSNR (XPSNR) between two input videos.
 *
 * Authors: Christian Helmrich, Lehmann, and Stoffers, Fraunhofer HHI, Berlin, Germany
 */

#include "libavutil/avstring.h"
#include "libavutil/file_open.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "framesync.h"
#include "xpsnr.h"

/* XPSNR structure definition */

typedef struct XPSNRContext {
    /* required basic variables */
    const AVClass   *class;
    int             bpp; /* unpacked */
    int             depth; /* packed */
    char            comps[4];
    int             num_comps;
    uint64_t        num_frames_64;
    unsigned        frame_rate;
    FFFrameSync     fs;
    int             line_sizes[4];
    int             plane_height[4];
    int             plane_width[4];
    uint8_t         rgba_map[4];
    FILE            *stats_file;
    char            *stats_file_str;
    /* XPSNR specific variables */
    double          *sse_luma;
    double          *weights;
    AVBufferRef     *buf_org   [3];
    AVBufferRef     *buf_org_m1[3];
    AVBufferRef     *buf_org_m2[3];
    AVBufferRef     *buf_rec   [3];
    uint64_t        max_error_64;
    double          sum_wdist [3];
    double          sum_xpsnr [3];
    int             and_is_inf[3];
    int             is_rgb;
    PSNRDSPContext  dsp;
} XPSNRContext;

/* required macro definitions */

#define FLAGS     AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
#define OFFSET(x) offsetof(XPSNRContext, x)
#define XPSNR_GAMMA 2

static const AVOption xpsnr_options[] = {
    {"stats_file", "Set file where to store per-frame XPSNR information", OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS},
    {"f",          "Set file where to store per-frame XPSNR information", OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS},
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(xpsnr, XPSNRContext, fs);

/* XPSNR function definitions */

static uint64_t highds(const int x_act, const int y_act, const int w_act, const int h_act, const int16_t *o_m0, const int o)
{
    uint64_t sa_act = 0;

    for (int y = y_act; y < h_act; y += 2) {
        for (int x = x_act; x < w_act; x += 2) {
            const int f = 12 * ((int)o_m0[ y   *o + x  ] + (int)o_m0[ y   *o + x+1] + (int)o_m0[(y+1)*o + x  ] + (int)o_m0[(y+1)*o + x+1])
                         - 3 * ((int)o_m0[(y-1)*o + x  ] + (int)o_m0[(y-1)*o + x+1] + (int)o_m0[(y+2)*o + x  ] + (int)o_m0[(y+2)*o + x+1])
                         - 3 * ((int)o_m0[ y   *o + x-1] + (int)o_m0[ y   *o + x+2] + (int)o_m0[(y+1)*o + x-1] + (int)o_m0[(y+1)*o + x+2])
                         - 2 * ((int)o_m0[(y-1)*o + x-1] + (int)o_m0[(y-1)*o + x+2] + (int)o_m0[(y+2)*o + x-1] + (int)o_m0[(y+2)*o + x+2])
                             - ((int)o_m0[(y-2)*o + x-1] + (int)o_m0[(y-2)*o + x  ] + (int)o_m0[(y-2)*o + x+1] + (int)o_m0[(y-2)*o + x+2]
                              + (int)o_m0[(y+3)*o + x-1] + (int)o_m0[(y+3)*o + x  ] + (int)o_m0[(y+3)*o + x+1] + (int)o_m0[(y+3)*o + x+2]
                              + (int)o_m0[(y-1)*o + x-2] + (int)o_m0[ y   *o + x-2] + (int)o_m0[(y+1)*o + x-2] + (int)o_m0[(y+2)*o + x-2]
                              + (int)o_m0[(y-1)*o + x+3] + (int)o_m0[ y   *o + x+3] + (int)o_m0[(y+1)*o + x+3] + (int)o_m0[(y+2)*o + x+3]);
            sa_act += (uint64_t) abs(f);
        }
    }
    return sa_act;
}

static uint64_t diff1st(const uint32_t w_act, const uint32_t h_act, const int16_t *o_m0, int16_t *o_m1, const int o)
{
    uint64_t ta_act = 0;

    for (uint32_t y = 0; y < h_act; y += 2) {
        for (uint32_t x = 0; x < w_act; x += 2) {
            const int t = (int)o_m0[y*o + x] + (int)o_m0[y*o + x+1] + (int)o_m0[(y+1)*o + x] + (int)o_m0[(y+1)*o + x+1]
                       - ((int)o_m1[y*o + x] + (int)o_m1[y*o + x+1] + (int)o_m1[(y+1)*o + x] + (int)o_m1[(y+1)*o + x+1]);
            ta_act += (uint64_t) abs(t);
            o_m1[y*o + x  ] = o_m0[y*o + x  ];  o_m1[(y+1)*o + x  ] = o_m0[(y+1)*o + x  ];
            o_m1[y*o + x+1] = o_m0[y*o + x+1];  o_m1[(y+1)*o + x+1] = o_m0[(y+1)*o + x+1];
        }
    }
    return (ta_act * XPSNR_GAMMA);
}

static uint64_t diff2nd(const uint32_t w_act, const uint32_t h_act, const int16_t *o_m0, int16_t *o_m1, int16_t *o_m2, const int o)
{
    uint64_t ta_act = 0;

    for (uint32_t y = 0; y < h_act; y += 2) {
        for (uint32_t x = 0; x < w_act; x += 2) {
            const int t = (int)o_m0[y*o + x] + (int)o_m0[y*o + x+1] + (int)o_m0[(y+1)*o + x] + (int)o_m0[(y+1)*o + x+1]
                   - 2 * ((int)o_m1[y*o + x] + (int)o_m1[y*o + x+1] + (int)o_m1[(y+1)*o + x] + (int)o_m1[(y+1)*o + x+1])
                        + (int)o_m2[y*o + x] + (int)o_m2[y*o + x+1] + (int)o_m2[(y+1)*o + x] + (int)o_m2[(y+1)*o + x+1];
            ta_act += (uint64_t) abs(t);
            o_m2[y*o + x  ] = o_m1[y*o + x  ];  o_m2[(y+1)*o + x  ] = o_m1[(y+1)*o + x  ];
            o_m2[y*o + x+1] = o_m1[y*o + x+1];  o_m2[(y+1)*o + x+1] = o_m1[(y+1)*o + x+1];
            o_m1[y*o + x  ] = o_m0[y*o + x  ];  o_m1[(y+1)*o + x  ] = o_m0[(y+1)*o + x  ];
            o_m1[y*o + x+1] = o_m0[y*o + x+1];  o_m1[(y+1)*o + x+1] = o_m0[(y+1)*o + x+1];
        }
    }
    return (ta_act * XPSNR_GAMMA);
}

static uint64_t sse_line_16bit(const uint8_t *blk_org8, const uint8_t *blk_rec8, int block_width)
{
    const uint16_t *blk_org = (const uint16_t *) blk_org8;
    const uint16_t *blk_rec = (const uint16_t *) blk_rec8;
    uint64_t sse = 0; /* sum for one pixel line */

    for (int x = 0; x < block_width; x++) {
        const int64_t error = (int64_t) blk_org[x] - (int64_t) blk_rec[x];

        sse += error * error;
    }

    /* sum of squared errors for the pixel line */
    return sse;
}

static inline uint64_t calc_squared_error(XPSNRContext const *s,
                                          const int16_t *blk_org,     const uint32_t stride_org,
                                          const int16_t *blk_rec,     const uint32_t stride_rec,
                                          const uint32_t block_width, const uint32_t block_height)
{
    uint64_t sse = 0;  /* sum of squared errors */

    for (uint32_t y = 0; y < block_height; y++) {
        sse += s->dsp.sse_line((const uint8_t *) blk_org, (const uint8_t *) blk_rec, (int) block_width);
        blk_org += stride_org;
        blk_rec += stride_rec;
    }

    /* return nonweighted sum of squared errors */
    return sse;
}

static inline double calc_squared_error_and_weight (XPSNRContext const *s,
                                                    const int16_t *pic_org,     const uint32_t stride_org,
                                                    int16_t       *pic_org_m1,  int16_t       *pic_org_m2,
                                                    const int16_t *pic_rec,     const uint32_t stride_rec,
                                                    const uint32_t offset_x,    const uint32_t offset_y,
                                                    const uint32_t block_width, const uint32_t block_height,
                                                    const uint32_t bit_depth,   const uint32_t int_frame_rate, double *ms_act)
{
    const int         o = (int) stride_org;
    const int         r = (int) stride_rec;
    const int16_t *o_m0 = pic_org    + offset_y * o + offset_x;
    int16_t       *o_m1 = pic_org_m1 + offset_y * o + offset_x;
    int16_t       *o_m2 = pic_org_m2 + offset_y * o + offset_x;
    const int16_t *r_m0 = pic_rec    + offset_y * r + offset_x;
    const int     b_val = (s->plane_width[0] * s->plane_height[0] > 2048 * 1152 ? 2 : 1); /* threshold is a bit more than HD resolution */
    const int     x_act = (offset_x > 0 ? 0 : b_val);
    const int     y_act = (offset_y > 0 ? 0 : b_val);
    const int     w_act = (offset_x + block_width  < (uint32_t) s->plane_width [0] ? (int) block_width  : (int) block_width  - b_val);
    const int     h_act = (offset_y + block_height < (uint32_t) s->plane_height[0] ? (int) block_height : (int) block_height - b_val);

    const double sse = (double) calc_squared_error (s, o_m0, stride_org,
                                                    r_m0, stride_rec,
                                                    block_width, block_height);
    uint64_t sa_act = 0;  /* spatial abs. activity */
    uint64_t ta_act = 0; /* temporal abs. activity */

    if (w_act <= x_act || h_act <= y_act) /* small */
        return sse;

    if (b_val > 1) { /* highpass with downsampling */
        if (w_act > 12)
            sa_act = s->dsp.highds_func(x_act, y_act, w_act, h_act, o_m0, o);
        else
            highds(x_act, y_act, w_act, h_act, o_m0, o);
    } else { /* <=HD highpass without downsampling */
        for (int y = y_act; y < h_act; y++) {
            for (int x = x_act; x < w_act; x++) {
                const int f = 12 * (int)o_m0[y*o + x] - 2 * ((int)o_m0[y*o + x-1] + (int)o_m0[y*o + x+1] + (int)o_m0[(y-1)*o + x] + (int)o_m0[(y+1)*o + x])
                                 - ((int)o_m0[(y-1)*o + x-1] + (int)o_m0[(y-1)*o + x+1] + (int)o_m0[(y+1)*o + x-1] + (int)o_m0[(y+1)*o + x+1]);
                sa_act += (uint64_t) abs(f);
            }
        }
    }

    /* calculate weight (average squared activity) */
    *ms_act = (double) sa_act / ((double) (w_act - x_act) * (double) (h_act - y_act));

    if (b_val > 1) { /* highpass with downsampling */
        if (int_frame_rate < 32) /* 1st-order diff */
            ta_act = s->dsp.diff1st_func(block_width, block_height, o_m0, o_m1, o);
        else /* 2nd-order diff (diff of two diffs) */
            ta_act = s->dsp.diff2nd_func(block_width, block_height, o_m0, o_m1, o_m2, o);
    } else { /* <=HD highpass without downsampling */
        if (int_frame_rate < 32) { /* 1st-order diff */
            for (uint32_t y = 0; y < block_height; y++) {
                for (uint32_t x = 0; x < block_width; x++) {
                    const int t = (int)o_m0[y * o + x] - (int)o_m1[y * o + x];

                    ta_act += XPSNR_GAMMA * (uint64_t) abs(t);
                    o_m1[y * o + x] = o_m0[y * o + x];
                }
            }
        } else { /* 2nd-order diff (diff of 2 diffs) */
            for (uint32_t y = 0; y < block_height; y++) {
                for (uint32_t x = 0; x < block_width; x++) {
                    const int t = (int)o_m0[y * o + x] - 2 * (int)o_m1[y * o + x] + (int)o_m2[y * o + x];

                    ta_act += XPSNR_GAMMA * (uint64_t) abs(t);
                    o_m2[y * o + x] = o_m1[y * o + x];
                    o_m1[y * o + x] = o_m0[y * o + x];
                }
            }
        }
    }

    /* weight += mean squared temporal activity */
    *ms_act += (double) ta_act / ((double) block_width * (double) block_height);

    /* lower limit, accounts for high-pass gain */
    if (*ms_act < (double) (1 << (bit_depth - 6)))
        *ms_act = (double) (1 << (bit_depth - 6));

    *ms_act *= *ms_act; /* since SSE is squared */

    /* return nonweighted sum of squared errors */
    return sse;
}

static inline double get_avg_xpsnr (const double sqrt_wsse_val,  const double sum_xpsnr_val,
                                    const uint32_t image_width,  const uint32_t image_height,
                                    const uint64_t max_error_64, const uint64_t num_frames_64)
{
    if (num_frames_64 == 0)
        return INFINITY;

    if (sqrt_wsse_val >= (double) num_frames_64) { /* square-mean-root average */
        const double avg_dist = sqrt_wsse_val / (double) num_frames_64;
        const uint64_t  num64 = (uint64_t) image_width * (uint64_t) image_height * max_error_64;

        return 10.0 * log10((double) num64 / ((double) avg_dist * (double) avg_dist));
    }

    return sum_xpsnr_val / (double) num_frames_64; /* older log-domain average */
}

static int get_wsse(AVFilterContext *ctx, int16_t **org, int16_t **org_m1, int16_t **org_m2, int16_t **rec,
                    uint64_t *const wsse64)
{
    XPSNRContext *const  s = ctx->priv;
    const uint32_t       w = s->plane_width [0]; /* luma image width in pixels */
    const uint32_t       h = s->plane_height[0];/* luma image height in pixels */
    const double         r = (double)(w * h) / (3840.0 * 2160.0); /* UHD ratio */
    const uint32_t       b = FFMAX(0, 4 * (int32_t) (32.0 * sqrt(r) +
                                                     0.5)); /* block size, integer multiple of 4 for SIMD */
    const uint32_t   w_blk = (w + b - 1) / b; /* luma width in units of blocks */
    const double   avg_act = sqrt(16.0 * (double) (1 << (2 * s->depth - 9)) / sqrt(FFMAX(0.00001,
                                                                                   r))); /* the sqrt(a_pic) */
    const int  *stride_org = (s->bpp == 1 ? s->plane_width : s->line_sizes);
    uint32_t x, y, idx_blk = 0; /* the "16.0" above is due to fixed-point code */
    double *const sse_luma = s->sse_luma;
    double *const  weights = s->weights;
    int c;

    if (!wsse64 || (s->depth < 6) || (s->depth > 16) || (s->num_comps <= 0) ||
        (s->num_comps > 3) || (w == 0) || (h == 0)) {
        av_log(ctx, AV_LOG_ERROR, "Error in XPSNR routine: invalid argument(s).\n");
        return AVERROR(EINVAL);
    }
    if (!weights || (b >= 4 && !sse_luma)) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate temporary block memory.\n");
        return AVERROR(ENOMEM);
    }

    if (b >= 4) {
        const int16_t *p_org = org[0];
        const uint32_t s_org = stride_org[0] / s->bpp;
        const int16_t *p_rec = rec[0];
        const uint32_t s_rec = s->plane_width[0];
        int16_t    *p_org_m1 = org_m1[0]; /* pixel  */
        int16_t    *p_org_m2 = org_m2[0]; /* memory */
        double     wsse_luma = 0.0;

        for (y = 0; y < h; y += b) { /* calculate block SSE and perceptual weights */
            const uint32_t block_height = (y + b > h ? h - y : b);

            for (x = 0; x < w; x += b, idx_blk++) {
                const uint32_t block_width = (x + b > w ? w - x : b);
                double ms_act = 1.0, ms_act_prev = 0.0;

                sse_luma[idx_blk] = calc_squared_error_and_weight(s, p_org, s_org,
                                                                  p_org_m1, p_org_m2,
                                                                  p_rec, s_rec,
                                                                  x, y,
                                                                  block_width, block_height,
                                                                  s->depth, s->frame_rate, &ms_act);
                weights[idx_blk] = 1.0 / sqrt(ms_act);

                if (w * h <= 640 * 480) { /* in-line "min-smoothing" as in paper */
                    if (x == 0) /* first column */
                        ms_act_prev = (idx_blk > 1 ? weights[idx_blk - 2] : 0);
                    else  /* after first column */
                        ms_act_prev = (x > b ? FFMAX(weights[idx_blk - 2], weights[idx_blk]) : weights[idx_blk]);

                    if (idx_blk > w_blk) /* after the first row and first column */
                        ms_act_prev = FFMAX(ms_act_prev, weights[idx_blk - 1 - w_blk]); /* min (L, T) */
                    if ((idx_blk > 0) && (weights[idx_blk - 1] > ms_act_prev))
                        weights[idx_blk - 1] = ms_act_prev;

                    if ((x + b >= w) && (y + b >= h) && (idx_blk > w_blk)) { /* last block in picture */
                        ms_act_prev = FFMAX(weights[idx_blk - 1], weights[idx_blk - w_blk]);
                        if (weights[idx_blk] > ms_act_prev)
                            weights[idx_blk] = ms_act_prev;
                    }
                }
            } /* for x */
        } /* for y */

        for (y = idx_blk = 0; y < h; y += b) { /* calculate sum for luma (Y) XPSNR */
            for (x = 0; x < w; x += b, idx_blk++) {
                wsse_luma += sse_luma[idx_blk] * weights[idx_blk];
            }
        }
        wsse64[0] = (wsse_luma <= 0.0 ? 0 : (uint64_t) (wsse_luma * avg_act + 0.5));
    } /* b >= 4 */

    for (c = 0; c < s->num_comps; c++) { /* finalize WSSE value for each component */
        const int16_t *p_org = org[c];
        const uint32_t s_org = stride_org[c] / s->bpp;
        const int16_t *p_rec = rec[c];
        const uint32_t s_rec = s->plane_width[c];
        const uint32_t w_pln = s->plane_width[c];
        const uint32_t h_pln = s->plane_height[c];

        if (b < 4) /* picture is too small for XPSNR, calculate nonweighted PSNR */
            wsse64[c] = calc_squared_error (s, p_org, s_org,
                                            p_rec, s_rec,
                                            w_pln, h_pln);
        else if (c > 0) { /* b >= 4 so Y XPSNR has already been calculated above */
            const uint32_t  bx = (b * w_pln) / w;
            const uint32_t  by = (b * h_pln) / h;  /* up to chroma downsampling by 4 */
            double wsse_chroma = 0.0;

            for (y = idx_blk = 0; y < h_pln; y += by) { /* calc chroma (Cb/Cr) XPSNR */
                const uint32_t block_height = (y + by > h_pln ? h_pln - y : by);

                for (x = 0; x < w_pln; x += bx, idx_blk++) {
                    const uint32_t block_width = (x + bx > w_pln ? w_pln - x : bx);

                    wsse_chroma += (double) calc_squared_error (s, p_org + y * s_org + x, s_org,
                                                                p_rec + y * s_rec + x, s_rec,
                                                                block_width, block_height) * weights[idx_blk];
                }
            }
            wsse64[c] = (wsse_chroma <= 0.0 ? 0 : (uint64_t) (wsse_chroma * avg_act + 0.5));
        }
    } /* for c */

    return 0;
}

static void set_meta(AVDictionary **metadata, const char *key, char comp, float d)
{
    char value[128];
    snprintf(value, sizeof(value), "%f", d);
    if (comp) {
        char key2[128];
        snprintf(key2, sizeof(key2), "%s%c", key, comp);
        av_dict_set(metadata, key2, value, 0);
    } else {
        av_dict_set(metadata, key, value, 0);
    }
}

static int do_xpsnr(FFFrameSync *fs)
{
    AVFilterContext  *ctx = fs->parent;
    XPSNRContext *const s = ctx->priv;
    const uint32_t      w = s->plane_width [0];  /* luma image width in pixels */
    const uint32_t      h = s->plane_height[0]; /* luma image height in pixels */
    const uint32_t      b = FFMAX(0, 4 * (int32_t) (32.0 * sqrt((double) (w * h) / (3840.0 * 2160.0)) + 0.5)); /* block size */
    const uint32_t  w_blk = (w + b - 1) / b;  /* luma width in units of blocks */
    const uint32_t  h_blk = (h + b - 1) / b; /* luma height in units of blocks */
    AVFrame *master, *ref = NULL;
    int16_t *porg   [3];
    int16_t *porg_m1[3];
    int16_t *porg_m2[3];
    int16_t *prec   [3];
    uint64_t wsse64 [3] = {0, 0, 0};
    double cur_xpsnr[3] = {INFINITY, INFINITY, INFINITY};
    int c, ret_value;
    AVDictionary **metadata;

    if ((ret_value = ff_framesync_dualinput_get(fs, &master, &ref)) < 0)
        return ret_value;
    if (ctx->is_disabled || !ref)
        return ff_filter_frame(ctx->outputs[0], master);
    metadata = &master->metadata;

    /* prepare XPSNR calculations: allocate temporary picture and block memory */
    if (!s->sse_luma)
        s->sse_luma = av_malloc_array(w_blk * h_blk, sizeof(double));
    if (!s->weights)
        s->weights  = av_malloc_array(w_blk * h_blk, sizeof(double));

    for (c = 0; c < s->num_comps; c++) {  /* create temporal org buffer memory */
        s->line_sizes[c] = master->linesize[c];

        if (c == 0) { /* luma ch. */
            const int stride_org_bpp = (s->bpp == 1 ? s->plane_width[c] : s->line_sizes[c] / s->bpp);

            if (!s->buf_org_m1[c])
                s->buf_org_m1[c] = av_buffer_allocz(stride_org_bpp * s->plane_height[c] * sizeof(int16_t));
            if (!s->buf_org_m2[c])
                s->buf_org_m2[c] = av_buffer_allocz(stride_org_bpp * s->plane_height[c] * sizeof(int16_t));

            porg_m1[c] = (int16_t *) s->buf_org_m1[c]->data;
            porg_m2[c] = (int16_t *) s->buf_org_m2[c]->data;
        }
    }

    if (s->bpp == 1) { /* 8 bit */
        for (c = 0; c < s->num_comps; c++) { /* allocate org/rec buffer memory */
            const int m = s->line_sizes[c];  /* master stride */
            const int r = ref->linesize[c];  /* ref/c stride */
            const int o = s->plane_width[c]; /* XPSNR stride */

            if (!s->buf_org[c])
                s->buf_org[c] = av_buffer_allocz(s->plane_width[c] * s->plane_height[c] * sizeof(int16_t));
            if (!s->buf_rec[c])
                s->buf_rec[c] = av_buffer_allocz(s->plane_width[c] * s->plane_height[c] * sizeof(int16_t));

            porg[c] = (int16_t *) s->buf_org[c]->data;
            prec[c] = (int16_t *) s->buf_rec[c]->data;

            for (int y = 0; y < s->plane_height[c]; y++) {
                for (int x = 0; x < s->plane_width[c]; x++) {
                    porg[c][y * o + x] = (int16_t) master->data[c][y * m + x];
                    prec[c][y * o + x] = (int16_t)    ref->data[c][y * r + x];
                }
            }
        }
    } else {  /* 10, 12, 14 bit */
        for (c = 0; c < s->num_comps; c++) {
            porg[c] = (int16_t *) master->data[c];
            prec[c] = (int16_t *)    ref->data[c];
        }
    }

    /* extended perceptually weighted peak signal-to-noise ratio (XPSNR) value */
    ret_value = get_wsse(ctx, (int16_t **) &porg, (int16_t **) &porg_m1, (int16_t **) &porg_m2,
                         (int16_t **) &prec, wsse64);
    if ( ret_value < 0 )
        return ret_value; /* an error here means something went wrong earlier! */

    for (c = 0; c < s->num_comps; c++) {
        const double sqrt_wsse = sqrt((double) wsse64[c]);

        cur_xpsnr[c] = get_avg_xpsnr (sqrt_wsse, INFINITY,
                                      s->plane_width[c], s->plane_height[c],
                                      s->max_error_64, 1 /* single frame */);
        s->sum_wdist[c] += sqrt_wsse;
        s->sum_xpsnr[c] += cur_xpsnr[c];
        s->and_is_inf[c] &= isinf(cur_xpsnr[c]);
    }
    s->num_frames_64++;

    for (int j = 0; j < s->num_comps; j++) {
        int c = s->is_rgb ? s->rgba_map[j] : j;
        set_meta(metadata, "lavfi.xpsnr.xpsnr.", s->comps[j], cur_xpsnr[c]);
    }

    if (s->stats_file) { /* print out frame- and component-wise XPSNR averages */
        fprintf(s->stats_file, "n: %4"PRId64"", s->num_frames_64);

        for (c = 0; c < s->num_comps; c++)
            fprintf(s->stats_file, "  XPSNR %c: %3.4f", s->comps[c], cur_xpsnr[c]);
        fprintf(s->stats_file, "\n");
    }

    return ff_filter_frame(ctx->outputs[0], master);
}

static av_cold int init(AVFilterContext *ctx)
{
    XPSNRContext *const s = ctx->priv;
    int c;

    if (s->stats_file_str) {
        if (!strcmp(s->stats_file_str, "-")) /* no stats file, so use stdout */
            s->stats_file = stdout;
        else {
            s->stats_file = avpriv_fopen_utf8(s->stats_file_str, "w");

            if (!s->stats_file) {
                const int err = AVERROR(errno);
                char buf[128];

                av_strerror(err, buf, sizeof(buf));
                av_log(ctx, AV_LOG_ERROR, "Could not open statistics file %s: %s\n", s->stats_file_str, buf);
                return err;
            }
        }
    }

    s->sse_luma = NULL;
    s->weights  = NULL;

    for (c = 0; c < 3; c++) { /* initialize XPSNR data of each color component */
        s->buf_org   [c] = NULL;
        s->buf_org_m1[c] = NULL;
        s->buf_org_m2[c] = NULL;
        s->buf_rec   [c] = NULL;
        s->sum_wdist [c] = 0.0;
        s->sum_xpsnr [c] = 0.0;
        s->and_is_inf[c] = 1;
    }

    s->fs.on_event = do_xpsnr;

    return 0;
}

static const enum AVPixelFormat xpsnr_formats[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
#define PF_NOALPHA(suf) AV_PIX_FMT_YUV420##suf,  AV_PIX_FMT_YUV422##suf,  AV_PIX_FMT_YUV444##suf
#define PF_ALPHA(suf)   AV_PIX_FMT_YUVA420##suf, AV_PIX_FMT_YUVA422##suf, AV_PIX_FMT_YUVA444##suf
#define PF(suf)         PF_NOALPHA(suf), PF_ALPHA(suf)
    PF(P), PF(P9), PF(P10), PF_NOALPHA(P12), PF_NOALPHA(P14), PF(P16),
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static int config_input_ref(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext  *ctx = inlink->dst;
    XPSNRContext *const s = ctx->priv;
    FilterLink *il = ff_filter_link(inlink);

    if ((ctx->inputs[0]->w != ctx->inputs[1]->w) ||
        (ctx->inputs[0]->h != ctx->inputs[1]->h)) {
        av_log(ctx, AV_LOG_ERROR, "Width and height of the input videos must match.\n");
        return AVERROR(EINVAL);
    }
    if (ctx->inputs[0]->format != ctx->inputs[1]->format) {
        av_log(ctx, AV_LOG_ERROR, "The input videos must be of the same pixel format.\n");
        return AVERROR(EINVAL);
    }

    s->bpp =  (desc->comp[0].depth <= 8 ? 1 : 2);
    s->depth = desc->comp[0].depth;
    s->max_error_64 = (1 << s->depth) - 1; /* conventional limit */
    s->max_error_64 *= s->max_error_64;

    s->frame_rate = il->frame_rate.num / il->frame_rate.den;

    s->num_comps = (desc->nb_components > 3 ? 3 : desc->nb_components);

    s->is_rgb = (ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0);
    s->comps[0] = (s->is_rgb ? 'r' : 'y');
    s->comps[1] = (s->is_rgb ? 'g' : 'u');
    s->comps[2] = (s->is_rgb ? 'b' : 'v');
    s->comps[3] = 'a';

    s->plane_width [1] = s->plane_width [2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->plane_width [0] = s->plane_width [3] = inlink->w;
    s->plane_height[1] = s->plane_height[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->plane_height[0] = s->plane_height[3] = inlink->h;

    s->dsp.sse_line = sse_line_16bit;
    s->dsp.highds_func = highds; /* initialize filtering methods */
    s->dsp.diff1st_func = diff1st;
    s->dsp.diff2nd_func = diff2nd;
#if ARCH_X86
    ff_xpsnr_init_x86(&s->dsp, 15); /* initialize x86 SSE method */
#endif

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    XPSNRContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    FilterLink *il = ff_filter_link(mainlink);
    FilterLink *ol = ff_filter_link(outlink);
    int ret;

    if ((ret = ff_framesync_init_dualinput(&s->fs, ctx)) < 0)
        return ret;

    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    ol->frame_rate = il->frame_rate;

    if ((ret = ff_framesync_configure(&s->fs)) < 0)
        return ret;

    outlink->time_base = s->fs.time_base;

    if (av_cmp_q(mainlink->time_base, outlink->time_base) ||
        av_cmp_q(ctx->inputs[1]->time_base, outlink->time_base))
        av_log(ctx, AV_LOG_WARNING, "not matching timebases found between first input: %d/%d and second input %d/%d, results may be incorrect!\n",
               mainlink->time_base.num, mainlink->time_base.den,
               ctx->inputs[1]->time_base.num, ctx->inputs[1]->time_base.den);

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    XPSNRContext *s = ctx->priv;

    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    XPSNRContext *const s = ctx->priv;
    int c;

    if (s->num_frames_64 > 0) { /* print out overall component-wise mean XPSNR */
        const double xpsnr_luma = get_avg_xpsnr(s->sum_wdist[0],   s->sum_xpsnr[0],
                                                s->plane_width[0], s->plane_height[0],
                                                s->max_error_64,   s->num_frames_64);
        double xpsnr_min = xpsnr_luma;

        /* luma */
        av_log(ctx, AV_LOG_INFO, "XPSNR  %c: %3.4f", s->comps[0], xpsnr_luma);
        if (s->stats_file) {
            fprintf(s->stats_file, "\nXPSNR average, %"PRId64" frames", s->num_frames_64);
            fprintf(s->stats_file, "  %c: %3.4f", s->comps[0], xpsnr_luma);
        }
        /* chroma */
        for (c = 1; c < s->num_comps; c++) {
            const double xpsnr_chroma = get_avg_xpsnr(s->sum_wdist[c],   s->sum_xpsnr[c],
                                                      s->plane_width[c], s->plane_height[c],
                                                      s->max_error_64,   s->num_frames_64);
            if (xpsnr_min > xpsnr_chroma)
                xpsnr_min = xpsnr_chroma;

            av_log(ctx, AV_LOG_INFO, "  %c: %3.4f", s->comps[c], xpsnr_chroma);
            if (s->stats_file && s->stats_file != stdout)
                fprintf(s->stats_file, "  %c: %3.4f", s->comps[c], xpsnr_chroma);
        }
        /* print out line break, and minimum XPSNR across the color components */
        if (s->num_comps > 1) {
            av_log(ctx, AV_LOG_INFO, "  (minimum: %3.4f)\n", xpsnr_min);
            if (s->stats_file && s->stats_file != stdout)
                fprintf(s->stats_file, "  (minimum: %3.4f)\n", xpsnr_min);
        } else {
            av_log(ctx, AV_LOG_INFO, "\n");
            if (s->stats_file && s->stats_file != stdout)
                fprintf(s->stats_file, "\n");
        }
    }

    ff_framesync_uninit(&s->fs); /* free temporary picture or block buf memory */

    if (s->stats_file && s->stats_file != stdout)
        fclose(s->stats_file);

    av_freep(&s->sse_luma);
    av_freep(&s->weights );

    for (c = 0; c < s->num_comps; c++) { /* free extra temporal org buf memory */
        if(s->buf_org_m1[c])
            av_freep(s->buf_org_m1[c]);
        if(s->buf_org_m2[c])
            av_freep(s->buf_org_m2[c]);
    }
    if (s->bpp == 1) { /* 8 bit */
        for (c = 0; c < s->num_comps; c++) { /* and org/rec picture buf memory */
            if(s->buf_org_m2[c])
                av_freep(s->buf_org[c]);
            if(s->buf_rec[c])
                av_freep(s->buf_rec[c]);
        }
    }
}

static const AVFilterPad xpsnr_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
    }, {
        .name         = "reference",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_ref,
    }
};

static const AVFilterPad xpsnr_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    }
};

const AVFilter ff_vf_xpsnr = {
    .name         = "xpsnr",
    .description  = NULL_IF_CONFIG_SMALL("Calculate the extended perceptually weighted peak signal-to-noise ratio (XPSNR) between two video streams."),
    .preinit      = xpsnr_framesync_preinit,
    .init         = init,
    .uninit       = uninit,
    .activate     = activate,
    .priv_size    = sizeof(XPSNRContext),
    .priv_class   = &xpsnr_class,
    FILTER_INPUTS (xpsnr_inputs),
    FILTER_OUTPUTS(xpsnr_outputs),
    FILTER_PIXFMTS_ARRAY(xpsnr_formats),
    .flags        = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_METADATA_ONLY
};
