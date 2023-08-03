/*
 * Copyright (c) 2016 Clément Bœsch <u pkh me>
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
 * @todo
 * - better automatic defaults? see "Parameters" @ http://www.ipol.im/pub/art/2011/bcm_nlm/
 * - temporal support (probably doesn't need any displacement according to
 *   "Denoising image sequences does not require motion estimation")
 * - Bayer pixel format support for at least raw photos? (DNG support would be
 *   handy here)
 * - FATE test (probably needs visual threshold test mechanism due to the use
 *   of floats)
 */

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "vf_nlmeans.h"
#include "vf_nlmeans_init.h"
#include "video.h"

typedef struct NLMeansContext {
    const AVClass *class;
    int nb_planes;
    int chroma_w, chroma_h;
    double pdiff_scale;                         // invert of the filtering parameter (sigma*10) squared
    double sigma;                               // denoising strength
    int patch_size,    patch_hsize;             // patch size and half size
    int patch_size_uv, patch_hsize_uv;          // patch size and half size for chroma planes
    int research_size,    research_hsize;       // research size and half size
    int research_size_uv, research_hsize_uv;    // research size and half size for chroma planes
    uint32_t *ii_orig;                          // integral image
    uint32_t *ii;                               // integral image starting after the 0-line and 0-column
    int ii_w, ii_h;                             // width and height of the integral image
    ptrdiff_t ii_lz_32;                         // linesize in 32-bit units of the integral image
    float *total_weight;                        // total weight for every pixel
    float *sum;                                 // weighted sum for every pixel
    int linesize;                               // sum and total_weight linesize
    float *weight_lut;                          // lookup table mapping (scaled) patch differences to their associated weights
    uint32_t max_meaningful_diff;               // maximum difference considered (if the patch difference is too high we ignore the pixel)
    NLMeansDSPContext dsp;
} NLMeansContext;

#define OFFSET(x) offsetof(NLMeansContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption nlmeans_options[] = {
    { "s",  "denoising strength", OFFSET(sigma), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 1.0, 30.0, FLAGS },
    { "p",  "patch size",                   OFFSET(patch_size),    AV_OPT_TYPE_INT, { .i64 = 3*2+1 }, 0, 99, FLAGS },
    { "pc", "patch size for chroma planes", OFFSET(patch_size_uv), AV_OPT_TYPE_INT, { .i64 = 0 },     0, 99, FLAGS },
    { "r",  "research window",                   OFFSET(research_size),    AV_OPT_TYPE_INT, { .i64 = 7*2+1 }, 0, 99, FLAGS },
    { "rc", "research window for chroma planes", OFFSET(research_size_uv), AV_OPT_TYPE_INT, { .i64 = 0 },     0, 99, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(nlmeans);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GBRP,
    AV_PIX_FMT_NONE
};

/**
 * Compute squared difference of an unsafe area (the zone nor s1 nor s2 could
 * be readable).
 *
 * On the other hand, the line above dst and the column to its left are always
 * readable.
 *
 * There is little point in having this function SIMDified as it is likely too
 * complex and only handle small portions of the image.
 *
 * @param dst               integral image
 * @param dst_linesize_32   integral image linesize (in 32-bit integers unit)
 * @param startx            integral starting x position
 * @param starty            integral starting y position
 * @param src               source plane buffer
 * @param linesize          source plane linesize
 * @param offx              source offsetting in x
 * @param offy              source offsetting in y
 * @paran r                 absolute maximum source offsetting
 * @param sw                source width
 * @param sh                source height
 * @param w                 width to compute
 * @param h                 height to compute
 */
static inline void compute_unsafe_ssd_integral_image(uint32_t *dst, ptrdiff_t dst_linesize_32,
                                                     int startx, int starty,
                                                     const uint8_t *src, ptrdiff_t linesize,
                                                     int offx, int offy, int r, int sw, int sh,
                                                     int w, int h)
{
    for (int y = starty; y < starty + h; y++) {
        uint32_t acc = dst[y*dst_linesize_32 + startx - 1] - dst[(y-1)*dst_linesize_32 + startx - 1];
        const int s1y = av_clip(y -  r,         0, sh - 1);
        const int s2y = av_clip(y - (r + offy), 0, sh - 1);

        for (int x = startx; x < startx + w; x++) {
            const int s1x = av_clip(x -  r,         0, sw - 1);
            const int s2x = av_clip(x - (r + offx), 0, sw - 1);
            const uint8_t v1 = src[s1y*linesize + s1x];
            const uint8_t v2 = src[s2y*linesize + s2x];
            const int d = v1 - v2;
            acc += d * d;
            dst[y*dst_linesize_32 + x] = dst[(y-1)*dst_linesize_32 + x] + acc;
        }
    }
}

/*
 * Compute the sum of squared difference integral image
 * http://www.ipol.im/pub/art/2014/57/
 * Integral Images for Block Matching - Gabriele Facciolo, Nicolas Limare, Enric Meinhardt-Llopis
 *
 * @param ii                integral image of dimension (w+e*2) x (h+e*2) with
 *                          an additional zeroed top line and column already
 *                          "applied" to the pointer value
 * @param ii_linesize_32    integral image linesize (in 32-bit integers unit)
 * @param src               source plane buffer
 * @param linesize          source plane linesize
 * @param offx              x-offsetting ranging in [-e;e]
 * @param offy              y-offsetting ranging in [-e;e]
 * @param w                 source width
 * @param h                 source height
 * @param e                 research padding edge
 */
static void compute_ssd_integral_image(const NLMeansDSPContext *dsp,
                                       uint32_t *ii, ptrdiff_t ii_linesize_32,
                                       const uint8_t *src, ptrdiff_t linesize, int offx, int offy,
                                       int e, int w, int h)
{
    // ii has a surrounding padding of thickness "e"
    const int ii_w = w + e*2;
    const int ii_h = h + e*2;

    // we center the first source
    const int s1x = e;
    const int s1y = e;

    // 2nd source is the frame with offsetting
    const int s2x = e + offx;
    const int s2y = e + offy;

    // get the dimension of the overlapping rectangle where it is always safe
    // to compare the 2 sources pixels
    const int startx_safe = FFMAX(s1x, s2x);
    const int starty_safe = FFMAX(s1y, s2y);
    const int u_endx_safe = FFMIN(s1x + w, s2x + w); // unaligned
    const int endy_safe   = FFMIN(s1y + h, s2y + h);

    // deduce the safe area width and height
    const int safe_pw = (u_endx_safe - startx_safe) & ~0xf;
    const int safe_ph = endy_safe - starty_safe;

    // adjusted end x position of the safe area after width of the safe area gets aligned
    const int endx_safe = startx_safe + safe_pw;

    // top part where only one of s1 and s2 is still readable, or none at all
    compute_unsafe_ssd_integral_image(ii, ii_linesize_32,
                                      0, 0,
                                      src, linesize,
                                      offx, offy, e, w, h,
                                      ii_w, starty_safe);

    // fill the left column integral required to compute the central
    // overlapping one
    compute_unsafe_ssd_integral_image(ii, ii_linesize_32,
                                      0, starty_safe,
                                      src, linesize,
                                      offx, offy, e, w, h,
                                      startx_safe, safe_ph);

    // main and safe part of the integral
    av_assert1(startx_safe - s1x >= 0); av_assert1(startx_safe - s1x < w);
    av_assert1(starty_safe - s1y >= 0); av_assert1(starty_safe - s1y < h);
    av_assert1(startx_safe - s2x >= 0); av_assert1(startx_safe - s2x < w);
    av_assert1(starty_safe - s2y >= 0); av_assert1(starty_safe - s2y < h);
    if (safe_pw && safe_ph)
        dsp->compute_safe_ssd_integral_image(ii + starty_safe*ii_linesize_32 + startx_safe, ii_linesize_32,
                                             src + (starty_safe - s1y) * linesize + (startx_safe - s1x), linesize,
                                             src + (starty_safe - s2y) * linesize + (startx_safe - s2x), linesize,
                                             safe_pw, safe_ph);

    // right part of the integral
    compute_unsafe_ssd_integral_image(ii, ii_linesize_32,
                                      endx_safe, starty_safe,
                                      src, linesize,
                                      offx, offy, e, w, h,
                                      ii_w - endx_safe, safe_ph);

    // bottom part where only one of s1 and s2 is still readable, or none at all
    compute_unsafe_ssd_integral_image(ii, ii_linesize_32,
                                      0, endy_safe,
                                      src, linesize,
                                      offx, offy, e, w, h,
                                      ii_w, ii_h - endy_safe);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    NLMeansContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int e = FFMAX(s->research_hsize, s->research_hsize_uv)
                + FFMAX(s->patch_hsize,    s->patch_hsize_uv);

    s->chroma_w = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->chroma_h = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    /* Allocate the integral image with extra edges of thickness "e"
     *
     *   +_+-------------------------------+
     *   |0|0000000000000000000000000000000|
     *   +-x-------------------------------+
     *   |0|\    ^                         |
     *   |0| ii  | e                       |
     *   |0|     v                         |
     *   |0|   +-----------------------+   |
     *   |0|   |                       |   |
     *   |0|<->|                       |   |
     *   |0| e |                       |   |
     *   |0|   |                       |   |
     *   |0|   +-----------------------+   |
     *   |0|                               |
     *   |0|                               |
     *   |0|                               |
     *   +-+-------------------------------+
     */
    s->ii_w = inlink->w + e*2;
    s->ii_h = inlink->h + e*2;

    // align to 4 the linesize, "+1" is for the space of the left 0-column
    s->ii_lz_32 = FFALIGN(s->ii_w + 1, 4);

    // "+1" is for the space of the top 0-line
    s->ii_orig = av_calloc(s->ii_h + 1, s->ii_lz_32 * sizeof(*s->ii_orig));
    if (!s->ii_orig)
        return AVERROR(ENOMEM);

    // skip top 0-line and left 0-column
    s->ii = s->ii_orig + s->ii_lz_32 + 1;

    // allocate weighted average for every pixel
    s->linesize = inlink->w + 100;
    s->total_weight = av_malloc_array(s->linesize, inlink->h * sizeof(*s->total_weight));
    s->sum = av_malloc_array(s->linesize, inlink->h * sizeof(*s->sum));
    if (!s->total_weight || !s->sum)
        return AVERROR(ENOMEM);

    return 0;
}

struct thread_data {
    const uint8_t *src;
    ptrdiff_t src_linesize;
    int startx, starty;
    int endx, endy;
    const uint32_t *ii_start;
    int p;
};

static int nlmeans_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    NLMeansContext *s = ctx->priv;
    const uint32_t max_meaningful_diff = s->max_meaningful_diff;
    const struct thread_data *td = arg;
    const ptrdiff_t src_linesize = td->src_linesize;
    const int process_h = td->endy - td->starty;
    const int slice_start = (process_h *  jobnr   ) / nb_jobs;
    const int slice_end   = (process_h * (jobnr+1)) / nb_jobs;
    const int starty = td->starty + slice_start;
    const int endy   = td->starty + slice_end;
    const int p = td->p;
    const uint32_t *ii = td->ii_start + (starty - p - 1) * s->ii_lz_32 - p - 1;
    const int dist_b = 2*p + 1;
    const int dist_d = dist_b * s->ii_lz_32;
    const int dist_e = dist_d + dist_b;
    const float *const weight_lut = s->weight_lut;
    NLMeansDSPContext *dsp = &s->dsp;

    for (int y = starty; y < endy; y++) {
        const uint8_t *const src = td->src + y*src_linesize;
        float *total_weight = s->total_weight + y*s->linesize;
        float *sum = s->sum + y*s->linesize;
        const uint32_t *const iia = ii;
        const uint32_t *const iib = ii + dist_b;
        const uint32_t *const iid = ii + dist_d;
        const uint32_t *const iie = ii + dist_e;

        dsp->compute_weights_line(iia, iib, iid, iie, src, total_weight, sum,
                                  weight_lut, max_meaningful_diff,
                                  td->startx, td->endx);
        ii += s->ii_lz_32;
    }
    return 0;
}

static void weight_averages(uint8_t *dst, ptrdiff_t dst_linesize,
                            const uint8_t *src, ptrdiff_t src_linesize,
                            float *total_weight, float *sum, ptrdiff_t linesize,
                            int w, int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // Also weight the centered pixel
            total_weight[x] += 1.f;
            sum[x] += 1.f * src[x];
            dst[x] = av_clip_uint8(sum[x] / total_weight[x] + 0.5f);
        }
        dst += dst_linesize;
        src += src_linesize;
        total_weight += linesize;
        sum += linesize;
    }
}

static int nlmeans_plane(AVFilterContext *ctx, int w, int h, int p, int r,
                         uint8_t *dst, ptrdiff_t dst_linesize,
                         const uint8_t *src, ptrdiff_t src_linesize)
{
    NLMeansContext *s = ctx->priv;
    /* patches center points cover the whole research window so the patches
     * themselves overflow the research window */
    const int e = r + p;
    /* focus an integral pointer on the centered image (s1) */
    const uint32_t *centered_ii = s->ii + e*s->ii_lz_32 + e;

    memset(s->total_weight, 0, s->linesize * h * sizeof(*s->total_weight));
    memset(s->sum, 0, s->linesize * h * sizeof(*s->sum));

    for (int offy = -r; offy <= r; offy++) {
        for (int offx = -r; offx <= r; offx++) {
            if (offx || offy) {
                struct thread_data td = {
                    .src          = src + offy*src_linesize + offx,
                    .src_linesize = src_linesize,
                    .startx       = FFMAX(0, -offx),
                    .starty       = FFMAX(0, -offy),
                    .endx         = FFMIN(w, w - offx),
                    .endy         = FFMIN(h, h - offy),
                    .ii_start     = centered_ii + offy*s->ii_lz_32 + offx,
                    .p            = p,
                };

                compute_ssd_integral_image(&s->dsp, s->ii, s->ii_lz_32,
                                           src, src_linesize,
                                           offx, offy, e, w, h);
                ff_filter_execute(ctx, nlmeans_slice, &td, NULL,
                                  FFMIN(td.endy - td.starty, ff_filter_get_nb_threads(ctx)));
            }
        }
    }

    weight_averages(dst, dst_linesize, src, src_linesize,
                    s->total_weight, s->sum, s->linesize, w, h);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    NLMeansContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (int i = 0; i < s->nb_planes; i++) {
        const int w = i ? s->chroma_w          : inlink->w;
        const int h = i ? s->chroma_h          : inlink->h;
        const int p = i ? s->patch_hsize_uv    : s->patch_hsize;
        const int r = i ? s->research_hsize_uv : s->research_hsize;
        nlmeans_plane(ctx, w, h, p, r,
                      out->data[i], out->linesize[i],
                      in->data[i],  in->linesize[i]);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

#define CHECK_ODD_FIELD(field, name) do {                       \
    if (!(s->field & 1)) {                                      \
        s->field |= 1;                                          \
        av_log(ctx, AV_LOG_WARNING, name " size must be odd, "  \
               "setting it to %d\n", s->field);                 \
    }                                                           \
} while (0)

static av_cold int init(AVFilterContext *ctx)
{
    NLMeansContext *s = ctx->priv;
    const double h = s->sigma * 10.;

    s->pdiff_scale = 1. / (h * h);
    s->max_meaningful_diff = log(255.) / s->pdiff_scale;
    s->weight_lut = av_calloc(s->max_meaningful_diff + 1, sizeof(*s->weight_lut));
    if (!s->weight_lut)
        return AVERROR(ENOMEM);
    for (int i = 0; i < s->max_meaningful_diff; i++)
        s->weight_lut[i] = exp(-i * s->pdiff_scale);

    CHECK_ODD_FIELD(research_size,   "Luma research window");
    CHECK_ODD_FIELD(patch_size,      "Luma patch");

    if (!s->research_size_uv) s->research_size_uv = s->research_size;
    if (!s->patch_size_uv)    s->patch_size_uv    = s->patch_size;

    CHECK_ODD_FIELD(research_size_uv, "Chroma research window");
    CHECK_ODD_FIELD(patch_size_uv,    "Chroma patch");

    s->research_hsize    = s->research_size    / 2;
    s->research_hsize_uv = s->research_size_uv / 2;
    s->patch_hsize       = s->patch_size       / 2;
    s->patch_hsize_uv    = s->patch_size_uv    / 2;

    av_log(ctx, AV_LOG_DEBUG, "Research window: %dx%d / %dx%d, patch size: %dx%d / %dx%d\n",
           s->research_size, s->research_size, s->research_size_uv, s->research_size_uv,
           s->patch_size,    s->patch_size,    s->patch_size_uv,    s->patch_size_uv);

    ff_nlmeans_init(&s->dsp);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    NLMeansContext *s = ctx->priv;
    av_freep(&s->weight_lut);
    av_freep(&s->ii_orig);
    av_freep(&s->total_weight);
    av_freep(&s->sum);
}

static const AVFilterPad nlmeans_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_nlmeans = {
    .name          = "nlmeans",
    .description   = NULL_IF_CONFIG_SMALL("Non-local means denoiser."),
    .priv_size     = sizeof(NLMeansContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(nlmeans_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &nlmeans_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
