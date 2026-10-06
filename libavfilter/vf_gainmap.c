/*
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
 * Filter to compute an HDR gain map from a base and alternate rendition.
 */

#include <float.h>
#include <math.h>

#include "libavutil/csp.h"
#include "libavutil/gain_map.h"
#include "libavutil/internal.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "colorspace.h"
#include "filters.h"
#include "formats.h"
#include "framesync.h"
#include "video.h"

#define SDR_DIFFUSE_WHITE 203.0

enum GainMapColorspace {
    GAINMAP_CSP_BASE,                ///< apply the map in the base rendition's space
    GAINMAP_CSP_ALT,                 ///< apply it in the alternate rendition's space
    GAINMAP_CSP_NB,
};

enum GainMapMode {
    GAINMAP_RGB,                    ///< one channel per component
    GAINMAP_LUMA,                   ///< single channel, from the luminance
    GAINMAP_MAXRGB,                 ///< single channel, from max(R,G,B)
    GAINMAP_MODE_NB,
};

enum GainMapMeasure {
    MEASURE_MIN   = 1 << 0,
    MEASURE_MAX   = 1 << 1,
    MEASURE_GAMMA = 1 << 2,
    MEASURE_RANGE = MEASURE_MIN   | MEASURE_MAX,
    MEASURE_ALL   = MEASURE_RANGE | MEASURE_GAMMA,
};

typedef struct GainMapEOTF {
    enum AVColorTransferCharacteristic trc;
    av_csp_eotf_function eotf; /* or NULL */
    double Lw;

#define GAINMAP_LUT_SIZE 1025
    float lut[GAINMAP_LUT_SIZE + 1]; /* extra padding entry */
} GainMapEOTF;

typedef struct GainMapContext {
    const AVClass *class;
    FFFrameSync fs;

    /* Filter options */
    int    mode;
    int    colorspace;
    AVRational gain_min;
    AVRational gain_max;
    AVRational gamma;
    AVRational base_offset;
    AVRational alt_offset;
    AVRational base_nits;
    AVRational alt_nits;

    int warned_noop;
    int nb_channels;
    int nb_threads; /* for slice threading */
    int convert; /* colorspace conversion needed */
    int sign;    /* sign(alt_peak - base_peak) */

    /* Colorspace parameters */
    GainMapEOTF base_eotf;
    GainMapEOTF alt_eotf;
    float rgb2y[3];
    float rgb2rgb[3][3];

    /* Quantization parameters, either static or recomputed dynamically */
    float quant_scale[3];
    float quant_offset[3];
    float quant_gamma[3];

    /* Measured frame statistics */
    float (*slice_min)[3];  /* for MEASURE_MIN */
    float (*slice_max)[3];  /* for MEASURE_MAX */
    double (*slice_sum)[3]; /* for MEASURE_GAMMA */
    enum GainMapMeasure measure;

    /* Generated gain map parameters (recomputed per frame) */
    AVGainMapParams *params;
    size_t params_size;
} GainMapContext;

typedef struct ThreadData {
    AVFrame *out;
    const AVFrame *base, *alt;
} ThreadData;

#define OFFSET(x) offsetof(GainMapContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption gainmap_options[] = {
    { "mode", "quantity the gain is computed over", OFFSET(mode), AV_OPT_TYPE_INT, { .i64 = GAINMAP_RGB }, 0, GAINMAP_MODE_NB - 1, FLAGS, .unit = "mode" },
        { "rgb",    "one gain channel per component",       0, AV_OPT_TYPE_CONST, { .i64 = GAINMAP_RGB },    0, 0, FLAGS, .unit = "mode" },
        { "luma",   "single gain channel, from luminance",  0, AV_OPT_TYPE_CONST, { .i64 = GAINMAP_LUMA },   0, 0, FLAGS, .unit = "mode" },
        { "maxrgb", "single gain channel, from max(R,G,B)", 0, AV_OPT_TYPE_CONST, { .i64 = GAINMAP_MAXRGB }, 0, 0, FLAGS, .unit = "mode" },
    { "colorspace", "rendition whose colour space the map is applied in", OFFSET(colorspace), AV_OPT_TYPE_INT, { .i64 = GAINMAP_CSP_BASE }, 0, GAINMAP_CSP_NB - 1, FLAGS, .unit = "colorspace" },
        { "base",      "the base rendition's colour space",             0, AV_OPT_TYPE_CONST, { .i64 = GAINMAP_CSP_BASE   }, 0, 0, FLAGS, .unit = "colorspace" },
        { "alternate", "the alternate rendition's colour space",        0, AV_OPT_TYPE_CONST, { .i64 = GAINMAP_CSP_ALT    }, 0, 0, FLAGS, .unit = "colorspace" },
    { "min", "override lower bound on the encoded gain, in log2 space", OFFSET(gain_min), AV_OPT_TYPE_RATIONAL, { .dbl = NAN }, -32.0, 32.0, FLAGS },
    { "max", "override upper bound on the encoded gain, in log2 space", OFFSET(gain_max), AV_OPT_TYPE_RATIONAL, { .dbl = NAN }, -32.0, 32.0, FLAGS },
    { "gamma", "override encoding gamma of the stored map (default: measured)", OFFSET(gamma), AV_OPT_TYPE_RATIONAL, { .dbl = NAN }, 0.0001, 100.0, FLAGS },
    { "base_offset", "constant added to the base rendition", OFFSET(base_offset), AV_OPT_TYPE_RATIONAL, { .dbl = 1.0 / 64.0 }, 1e-6, 1.0, FLAGS },
    { "alt_offset", "constant added to the alternate rendition", OFFSET(alt_offset), AV_OPT_TYPE_RATIONAL, { .dbl = 1.0 / 64.0 }, 1e-6, 1.0, FLAGS },
    { "base_nits", "override input luminance of the base rendition", OFFSET(base_nits), AV_OPT_TYPE_RATIONAL, { .dbl = NAN }, 1.0, 10000.0, FLAGS },
    { "alt_nits", "override input luminance of the alternate rendition", OFFSET(alt_nits), AV_OPT_TYPE_RATIONAL, { .dbl = NAN }, 1.0, 10000.0, FLAGS },
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(gainmap, GainMapContext, fs);

/* Quantize to a fixed point representation with the correct rounding mode */
static AVRational quantq(double x, enum AVRounding rnd)
{
    const int scale = 1 << 22;

    switch (rnd) {
    case AV_ROUND_DOWN:      x = floor(x * scale);  break;
    case AV_ROUND_UP:        x = ceil(x * scale);   break;
    case AV_ROUND_NEAR_INF:  x = round(x * scale);  break;
    default: av_unreachable("not used / implemented");
    }

    AVRational q;
    av_reduce(&q.num, &q.den, x, scale, INT_MAX);
    return q;
}

static void setup_range(AVFilterContext *ctx, int ch, AVRational min, AVRational max)
{
    GainMapContext *s = ctx->priv;
    if (av_cmp_q(max, min) <= 0) {
        /* Nothing to quantize; flat gain map */
        s->quant_scale[ch] = s->quant_offset[ch] = 0.0f;
        max = min; /* sanity */
    } else {
        const float minf = av_q2d(min), maxf = av_q2d(max);
        s->quant_scale[ch]  = 1.0f / (maxf - minf);
        s->quant_offset[ch] = -minf * s->quant_scale[ch];
    }

    s->params->channels[ch].gain_map_min = min;
    s->params->channels[ch].gain_map_max = max;
    av_log(ctx, AV_LOG_TRACE, "channel %d: measured min=%g max=%g\n",
           ch, av_q2d(min), av_q2d(max));
}

static void setup_gamma(AVFilterContext *ctx, int ch, AVRational gamma)
{
    GainMapContext *s = ctx->priv;
    s->quant_gamma[ch] = av_q2d(gamma);
    s->params->channels[ch].gamma = gamma;
    av_log(ctx, AV_LOG_TRACE, "channel %d: measured gamma=%g\n",
           ch, s->quant_gamma[ch]);
}

static av_cold int init(AVFilterContext *ctx)
{
    GainMapContext *s = ctx->priv;

    if (av_cmp_q(s->gain_min, s->gain_max) >= 0) {
        av_log(ctx, AV_LOG_ERROR, "min (%g) must be below max (%g)\n",
               av_q2d(s->gain_min), av_q2d(s->gain_max));
        return AVERROR(EINVAL);
    }

    s->nb_channels = s->mode == GAINMAP_RGB ? 3 : 1;
    if (!s->gain_min.den)
        s->measure |= MEASURE_MIN;
    if (!s->gain_max.den)
        s->measure |= MEASURE_MAX;
    if (!s->gamma.den)
        s->measure |= MEASURE_GAMMA;
    if (s->measure) {
        av_log(ctx, AV_LOG_VERBOSE, "Using two passes to measure gain map "
               "parameters from the input frame.\n");
    }

    s->params = av_gain_map_params_alloc(&s->params_size);
    if (!s->params)
        return AVERROR(ENOMEM);

    /* payload metadata recomputed per frame */
    s->params->version              = 0;
    s->params->nb_channels          = s->nb_channels;
    s->params->use_base_color_space = s->colorspace == GAINMAP_CSP_BASE;

    for (int c = 0; c < s->nb_channels; c++) {
        struct AVGainMapChannel *ch = &s->params->channels[c];
        ch->base_offset      = s->base_offset;
        ch->alternate_offset = s->alt_offset;
        if (!(s->measure & MEASURE_RANGE))
            setup_range(ctx, c, s->gain_min, s->gain_max);
        if (!(s->measure & MEASURE_GAMMA))
            setup_gamma(ctx, c, s->gamma);
    }

    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const GainMapContext *s = ctx->priv;
    enum AVPixelFormat in_fmt, out_fmt;
    int ret;

    in_fmt  = AV_PIX_FMT_GBRPF32;
    out_fmt = s->nb_channels == 1 ? AV_PIX_FMT_GRAYF32 : AV_PIX_FMT_GBRPF32;

    ret = ff_formats_ref(ff_make_formats_list_singleton(out_fmt), &cfg_out[0]->formats);
    if (ret < 0)
        return ret;

    return ff_set_common_formats2(ctx, cfg_in, cfg_out,
                                  ff_make_formats_list_singleton(in_fmt));
}

static double frame_luminance(const AVFrame *frame)
{
    const AVFrameSideData *sd;
    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (sd) {
        const AVMasteringDisplayMetadata *mdm;
        mdm = (const AVMasteringDisplayMetadata *) sd->data;
        if (mdm->has_luminance && mdm->max_luminance.num > 0)
            return av_q2d(mdm->max_luminance);
    }

    switch (frame->color_trc) {
    case AVCOL_TRC_SMPTE2084:    return 10000.0;
    case AVCOL_TRC_ARIB_STD_B67: return 1000.0;
    default:                     return SDR_DIFFUSE_WHITE;
    }
}

static void update_eotf(GainMapEOTF *tf, enum AVColorTransferCharacteristic trc,
                        av_csp_eotf_function eotf, double Lw)
{
    if (trc == tf->trc && Lw == tf->Lw)
        return; /* no change */
    tf->trc = trc;
    tf->Lw  = Lw;

    switch (trc) {
    /* EOTFs that are safe to collapse into a 1D LUT */
    case AVCOL_TRC_BT709:
    case AVCOL_TRC_GAMMA22:
    case AVCOL_TRC_GAMMA28:
    case AVCOL_TRC_SMPTE170M:
    case AVCOL_TRC_SMPTE240M:
    case AVCOL_TRC_LINEAR:
    case AVCOL_TRC_IEC61966_2_4:
    case AVCOL_TRC_BT1361_ECG:
    case AVCOL_TRC_IEC61966_2_1:
    case AVCOL_TRC_BT2020_10:
    case AVCOL_TRC_BT2020_12:
    case AVCOL_TRC_SMPTE2084: {
        for (int i = 0; i < GAINMAP_LUT_SIZE; i++) {
            double x[3] = { i / (GAINMAP_LUT_SIZE - 1.0) };
            eotf(Lw, 0.0, x);
            tf->lut[i] = x[0] / SDR_DIFFUSE_WHITE; /* normalize */
        }

        tf->lut[GAINMAP_LUT_SIZE] = tf->lut[GAINMAP_LUT_SIZE - 1];
        tf->eotf = NULL;
        break;
    }
    /* EOTFs that use the av_csp_eotf_function fallback */
    case AVCOL_TRC_ARIB_STD_B67: /* nontrivial OOTF */
    case AVCOL_TRC_SMPTE428:     /* different normalization per channel */
    default:
        tf->eotf = eotf;
        break;
    }
}

static const char *unknown_if_null(const char *s)
{
    return s ? s : "unknown";
}

/* Run per frame, since trc/primaries are not (yet) link-level properties */
static int setup_colorspace(AVFilterContext *ctx, const AVFrame *base, const AVFrame *alt)
{
    GainMapContext *const s = ctx->priv;
    av_csp_eotf_function base_eotf = av_csp_itu_eotf(base->color_trc);
    av_csp_eotf_function  alt_eotf = av_csp_itu_eotf(alt->color_trc);
    if (!base_eotf || !alt_eotf) {
        av_log(ctx, AV_LOG_ERROR, "Unknown transfer: base=%s, alternate=%s\n",
               unknown_if_null(av_color_transfer_name(base->color_trc)),
               unknown_if_null(av_color_transfer_name(alt->color_trc)));
        return AVERROR(EINVAL);
    }

    const AVColorPrimariesDesc *base_desc, *alt_desc;
    base_desc = av_csp_primaries_desc_from_id(base->color_primaries);
    alt_desc  = av_csp_primaries_desc_from_id(alt->color_primaries);
    if (!base_desc || !alt_desc) {
        av_log(ctx, AV_LOG_ERROR, "Unknown primaries: base=%s, alternate=%s\n",
                unknown_if_null(av_color_primaries_name(base->color_primaries)),
                unknown_if_null(av_color_primaries_name(alt->color_primaries)));
        return AVERROR(EINVAL);
    }

    double base_lw = s->base_nits.den ? av_q2d(s->base_nits) : frame_luminance(base);
    double  alt_lw = s->alt_nits.den  ? av_q2d(s->alt_nits)  : frame_luminance(alt);
    double base_headroom = fmax(log2(base_lw / SDR_DIFFUSE_WHITE), 0.0);
    double  alt_headroom = fmax(log2(alt_lw  / SDR_DIFFUSE_WHITE), 0.0);
    s->params->base_hdr_headroom      = quantq(base_headroom, AV_ROUND_NEAR_INF);
    s->params->alternate_hdr_headroom = quantq(alt_headroom,  AV_ROUND_NEAR_INF);
    s->sign = FFDIFFSIGN(alt_headroom, base_headroom);
    if (!s->sign) {
        /* No-op, set up empty gain map */
        for (int i = 0; i < s->nb_channels; i++) {
            setup_range(ctx, i, (AVRational) { 0, 1 }, (AVRational) { 0, 1 });
            setup_gamma(ctx, i, (AVRational) { 1, 1 });
        }

        av_log_once(ctx, AV_LOG_WARNING, AV_LOG_VERBOSE, &s->warned_noop,
                    "Base and alternate renditions have the same peak "
                    "luminance (%g nits), gain map will be empty\n", base_lw);
        return 0;
    } else {
        av_log(ctx, AV_LOG_DEBUG, "Base peak: %g nits, alternate peak: %g nits\n",
               base_lw, alt_lw);
    }

    update_eotf(&s->base_eotf, base->color_trc, base_eotf, base_lw);
    update_eotf(&s->alt_eotf,   alt->color_trc,  alt_eotf,  alt_lw);

    /* Assume conversion from alt to base */
    if (s->colorspace == GAINMAP_CSP_ALT)
        FFSWAP(const AVColorPrimariesDesc *, base_desc, alt_desc);

    double rgb2xyz[3][3];
    ff_fill_rgb2xyz_table(&base_desc->prim, &base_desc->wp, rgb2xyz);
    for (int i = 0; i < 3; i++)
        s->rgb2y[i] = rgb2xyz[1][i] / av_q2d(base_desc->wp.y);

    s->convert = base->color_primaries != alt->color_primaries;
    if (s->convert) {
        /* Note: Ignores whitepoint differences (chromatic adaptation) */
        double xyz2rgb[3][3], rgb2rgb[3][3];
        ff_fill_rgb2xyz_table(&base_desc->prim, &base_desc->wp, rgb2xyz);
        ff_matrix_invert_3x3(rgb2xyz, xyz2rgb);
        ff_fill_rgb2xyz_table(&alt_desc->prim, &alt_desc->wp, rgb2xyz);
        ff_matrix_mul_3x3(rgb2rgb, rgb2xyz, xyz2rgb);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                s->rgb2rgb[i][j] = (float) rgb2rgb[i][j];
    } else {
        memset(s->rgb2rgb, 0, sizeof(s->rgb2rgb));
        for (int i = 0; i < 3; i++)
            s->rgb2rgb[i][i] = 1.0f;
    }

    return 0;
}

static av_always_inline float quantize(float gain, float scale, float offset, float gamma)
{
    const float norm = av_clipf(scale * gain + offset, 0.0f, 1.0f);
    return gamma == 1.0f ? norm : powf(norm, gamma);
}

static av_always_inline void
get_pixel(const GainMapContext *s, float dst[3],
          const float *restrict const src[3], int x, int alt)
{
    const GainMapEOTF *const tf = alt ? &s->alt_eotf : &s->base_eotf;
    float rgb[3];

    /* Linearize to normalized RGB */
    if (tf->eotf) {
        double rgbd[3] = { src[0][x], src[1][x], src[2][x] };
        tf->eotf(tf->Lw, 0.0, rgbd);
        for (int i = 0; i < 3; i++)
            rgb[i] = rgbd[i] / SDR_DIFFUSE_WHITE;
    } else {
        for (int i = 0; i < 3; i++) {
            const float fx = av_clipf(src[i][x], 0.0f, 1.0f) * (GAINMAP_LUT_SIZE - 1.0);
            const int   ix = (int) fx;
            const float lo = tf->lut[ix];
            const float hi = tf->lut[ix + 1];
            rgb[i] = lo + (fx - ix) * (hi - lo);
        }
    }

    /* Convert to correct colorspace if needed */
    if (s->convert && (s->colorspace == GAINMAP_CSP_ALT) != alt) {
        const float r = rgb[0], g = rgb[1], b = rgb[2];
        for (int i = 0; i < 3; i++) {
            dst[i] = fmaxf(r * s->rgb2rgb[i][0] +
                           g * s->rgb2rgb[i][1] +
                           b * s->rgb2rgb[i][2], 0.0f);
        }
    } else {
        for (int i = 0; i < 3; i++)
            dst[i] = fmaxf(rgb[i], 0.0f);
    }
}

/* GBRP plane order */
static const int gbr_order[3] = { 2, 0, 1 };

static av_always_inline int
slice_internal(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs, int quant)
{
    GainMapContext *s = ctx->priv;
    const ThreadData *td = arg;
    const AVFrame *base = td->base, *alt = td->alt, *out = td->out;

    const float *restrict scale  = s->quant_scale;
    const float *restrict offset = s->quant_offset;
    const float *restrict gamma  = s->quant_gamma;

    /* Normalize both to the base colorspace */
    const float base_off = av_q2d(s->base_offset);
    const float alt_off  = av_q2d(s->alt_offset);
    const float sign     = s->sign;

    const int y_start = ff_slice_pos(out->height, jobnr,     nb_jobs);
    const int y_end   = ff_slice_pos(out->height, jobnr + 1, nb_jobs);
    const int width   = out->width;
    const int mode    = s->mode;
    const int nb_ch   = s->nb_channels;

    float  min[3] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
    float  max[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    double sum[3] = { 0.0, 0.0, 0.0 };

    for (int y = y_start; y < y_end; y++) {
        const float *restrict b_row[3], *restrict a_row[3];
        for (int i = 0; i < 3; i++) {
            const int p = gbr_order[i];
            b_row[i] = (const float *) (base->data[p] + y * base->linesize[p]);
            a_row[i] = (const float *) ( alt->data[p] + y *  alt->linesize[p]);
        }

        float *restrict out_row[3];
        for (int i = 0; i < nb_ch; i++) {
            const int p = nb_ch == 1 ? 0 : gbr_order[i];
            out_row[i] = (float *) (out->data[p] + y * out->linesize[p]);
        }

        for (int x = 0; x < width; x++) {
            float b[3], a[3], gain[3];
            get_pixel(s, b, b_row, x, 0);
            get_pixel(s, a, a_row, x, 1);

            #define GAIN(a, b) (sign * log2f(((a) + alt_off) / ((b) + base_off)))
            switch (mode) {
            case GAINMAP_MAXRGB: {
                const float max_a = fmaxf(fmaxf(a[0], a[1]), a[2]);
                const float max_b = fmaxf(fmaxf(b[0], b[1]), b[2]);
                gain[0] = GAIN(max_a, max_b);
                break;
            }
            case GAINMAP_LUMA: {
                const float y_a = s->rgb2y[0] * a[0] + s->rgb2y[1] * a[1] + s->rgb2y[2] * a[2];
                const float y_b = s->rgb2y[0] * b[0] + s->rgb2y[1] * b[1] + s->rgb2y[2] * b[2];
                gain[0] = GAIN(y_a, y_b);
                break;
            }
            case GAINMAP_RGB:
                gain[0] = GAIN(a[0], b[0]);
                gain[1] = GAIN(a[1], b[1]);
                gain[2] = GAIN(a[2], b[2]);
                break;
            }
            #undef GAIN

            if (quant) {
                for (int i = 0; i < nb_ch; i++)
                    out_row[i][x] = quantize(gain[i], scale[i], offset[i], gamma[i]);
            } else {
                for (int i = 0; i < nb_ch; i++) {
                    out_row[i][x] = gain[i];
                    min[i] = fminf(min[i], gain[i]);
                    max[i] = fmaxf(max[i], gain[i]);
                    sum[i] += gain[i];
                }
            }

        }
    }

    for (int i = 0; !quant && i < 3; i++) {
        s->slice_min[jobnr][i] = min[i];
        s->slice_max[jobnr][i] = max[i];
        s->slice_sum[jobnr][i] = sum[i];
    }

    return 0;
}

static int slice_gain(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return slice_internal(ctx, arg, jobnr, nb_jobs, 0);
}

static int slice_gain_quant(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    return slice_internal(ctx, arg, jobnr, nb_jobs, 1);
}

static int slice_quant(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    const ThreadData *td = arg;
    const GainMapContext *s = ctx->priv;
    const AVFrame *out = td->out;

    const float *restrict scale  = s->quant_scale;
    const float *restrict offset = s->quant_offset;
    const float *restrict gamma  = s->quant_gamma;

    const int y_start = ff_slice_pos(out->height, jobnr,     nb_jobs);
    const int y_end   = ff_slice_pos(out->height, jobnr + 1, nb_jobs);
    const int width   = out->width;
    const int nb_ch   = s->nb_channels;

    for (int y = y_start; y < y_end; y++) {
        float *restrict out_row[3];
        for (int i = 0; i < nb_ch; i++) {
            const int p = nb_ch == 1 ? 0 : gbr_order[i];
            out_row[i] = (float *) (out->data[p] + y * out->linesize[p]);
        }

        for (int x = 0; x < width; x++) {
            for (int i = 0; i < nb_ch; i++)
                out_row[i][x] = quantize(out_row[i][x], scale[i], offset[i], gamma[i]);
        }
    }

    return 0;
}

static void choose_quant_params(AVFilterContext *ctx, const AVFrame *out, int nb_jobs)
{
    GainMapContext *s = ctx->priv;
    float  frame_min[3] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
    float  frame_max[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    double frame_sum[3] = { 0.0, 0.0, 0.0 };

    for (int j = 0; j < nb_jobs; j++) {
        for (int i = 0; i < s->nb_channels; i++) {
            frame_min[i] = fminf(frame_min[i], s->slice_min[j][i]);
            frame_max[i] = fmaxf(frame_max[i], s->slice_max[j][i]);
            frame_sum[i] += s->slice_sum[j][i];
        }
    }

    for (int i = 0; i < s->nb_channels; i++) {
        AVRational min = s->gain_min, max = s->gain_max;
        if (s->measure & MEASURE_MIN)
           min = quantq(frame_min[i], AV_ROUND_DOWN);
        if (s->measure & MEASURE_MAX)
           max = quantq(frame_max[i], AV_ROUND_UP);
        if (s->measure & MEASURE_RANGE)
            setup_range(ctx, i, min, max);

        if (!(s->measure & MEASURE_GAMMA))
            continue;

        const int64_t nb_pixels = (int64_t) out->width * out->height;
        const double mean = frame_sum[i] / nb_pixels;
        const double mean_quant = s->quant_scale[i] * mean + s->quant_offset[i];

        /**
         * Choose gamma such that mean_quant ^ gamma = 0.5; clamp to a sane
         * value range of [1/8, 8] to prevent degenerate encodings. A gamma
         * of >8 would collapse half the encoding space into a single 8-bit
         * value, at which point we're losing more then gaining, and a value
         * of <1/8 limits the amount by which a single high outlier pixel
         * can determine the overall encoding gamma.
         *
         * We also clamp the mean to [0.01, 0.99] to prevent numerical explosion
         * in the case that the mean is very close to 0 or 1. This is a looser
         * bound than the final gamma clamp, so the exact values chosen do not
         * matter as much.
         */
        double gamma = log(0.5) / log(av_clipd(mean_quant, 0.01, 0.99));
        setup_gamma(ctx, i, quantq(av_clipd(gamma, 1/8.0, 8.0), AV_ROUND_NEAR_INF));
    }
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    GainMapContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *base, *alt, *out;

    int ret;
    if ((ret = ff_framesync_get_frame(fs, 0, &base, 0)) < 0 ||
        (ret = ff_framesync_get_frame(fs, 1, &alt,  0)) < 0)
        return ret;
    if (!base || !alt)
        return AVERROR_BUG;

    ret = setup_colorspace(ctx, base, alt);
    if (ret < 0)
        return ret;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    av_frame_copy_props(out, base);
    out->pts = av_rescale_q(fs->pts, fs->time_base, outlink->time_base);
    av_frame_side_data_remove_by_props(&out->side_data, &out->nb_side_data,
                                       AV_SIDE_DATA_PROP_COLOR_DEPENDENT);

    out->color_trc       = AVCOL_TRC_UNSPECIFIED;
    out->color_primaries = AVCOL_PRI_UNSPECIFIED;
    out->colorspace      = AVCOL_SPC_UNSPECIFIED;
    out->color_range     = AVCOL_RANGE_JPEG;

    ThreadData td = {
        .out  = out,
        .base = base,
        .alt  = alt,
    };

    if (!s->sign) { /* no-op */
        for (int i = 0; i < s->nb_channels; i++)
            memset(out->data[i], 0, out->height * out->linesize[i]);
        goto skip;
    }

    const int nb_jobs = FFMIN(outlink->h, s->nb_threads);
    ret = ff_filter_execute(ctx, s->measure ? slice_gain : slice_gain_quant,
                            &td, NULL, nb_jobs);
    if (ret < 0)
        goto fail;

    if (s->measure) {
        choose_quant_params(ctx, out, nb_jobs);
        ret = ff_filter_execute(ctx, slice_quant, &td, NULL, nb_jobs);
        if (ret < 0)
            goto fail;
    }

skip:;
    AVGainMapParams *params;
    params = av_gain_map_params_create_side_data(&out->side_data, &out->nb_side_data);
    if (!params) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    memcpy(params, s->params, s->params_size);
    if (av_gain_map_params_validate(params) < 0)
        return AVERROR_BUG; /* should never happen */

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&out);
    return ret;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    GainMapContext *s = ctx->priv;
    AVFilterLink *base = ctx->inputs[0];
    AVFilterLink *alt = ctx->inputs[1];
    int ret;

    if (base->w != alt->w || base->h != alt->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Input dimensions must match (%dx%d != %dx%d)\n",
               base->w, base->h, alt->w, alt->h);
        return AVERROR(EINVAL);
    }

    outlink->w           = base->w;
    outlink->h           = base->h;
    outlink->colorspace  = AVCOL_SPC_UNSPECIFIED;
    outlink->color_range = AVCOL_RANGE_JPEG;

    s->nb_threads = ff_filter_get_nb_threads(ctx);
    s->slice_min  = av_calloc(s->nb_threads, sizeof(*s->slice_min));
    s->slice_max  = av_calloc(s->nb_threads, sizeof(*s->slice_max));
    s->slice_sum  = av_calloc(s->nb_threads, sizeof(*s->slice_sum));
    if (!s->slice_min || !s->slice_max || !s->slice_sum)
        return AVERROR(ENOMEM);

    ret = ff_framesync_init_dualinput(&s->fs, ctx);
    if (ret < 0)
        return ret;

    s->fs.on_event = process_frame;
    return ff_framesync_configure(&s->fs);
}

static int activate(AVFilterContext *ctx)
{
    GainMapContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GainMapContext *s = ctx->priv;
    ff_framesync_uninit(&s->fs);
    av_freep(&s->slice_min);
    av_freep(&s->slice_max);
    av_freep(&s->slice_sum);
}

static const AVFilterPad gainmap_inputs[] = {
    {
        .name = "base",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name = "alternate",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad gainmap_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const FFFilter ff_vf_gainmap = {
    .p.name        = "gainmap",
    .p.description = NULL_IF_CONFIG_SMALL("Generate a gain map from a base/alternate pair."),
    .p.priv_class  = &gainmap_class,
    .p.flags       = AVFILTER_FLAG_SLICE_THREADS,
    .preinit       = gainmap_framesync_preinit,
    .priv_size     = sizeof(GainMapContext),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(gainmap_inputs),
    FILTER_OUTPUTS(gainmap_outputs),
    FILTER_QUERY_FUNC2(query_formats),
};
