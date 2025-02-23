/*
 * Copyright (C) 2024 Niklas Haas
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

#include <math.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/csp.h"
#include "libavutil/slicethread.h"

#include "cms.h"
#include "csputils.h"
#include "libswscale/swscale.h"
#include "format.h"

bool ff_sws_color_map_noop(const SwsColorMap *map)
{
    /* If the encoding space is different, we must go through a conversion */
    if (map->src.prim != map->dst.prim || map->src.trc != map->dst.trc)
        return false;

    /* If the black point changes, we have to perform black point compensation */
    if (av_cmp_q(map->src.min_luma, map->dst.min_luma))
        return false;

    switch (map->intent) {
    case SWS_INTENT_ABSOLUTE_COLORIMETRIC:
    case SWS_INTENT_RELATIVE_COLORIMETRIC:
        return ff_prim_superset(&map->dst.gamut, &map->src.gamut) &&
               av_cmp_q(map->src.max_luma, map->dst.max_luma) <= 0;
    case SWS_INTENT_PERCEPTUAL:
    case SWS_INTENT_SATURATION:
        return ff_prim_equal(&map->dst.gamut, &map->src.gamut) &&
               !av_cmp_q(map->src.max_luma, map->dst.max_luma);
    default:
        av_assert0(!"Invalid gamut mapping intent?");
        return true;
    }
}

/* Approximation of gamut hull at a given intensity level */
static const float hull(float I)
{
    return ((I - 6.0f) * I + 9.0f) * I;
}

/* For some minimal type safety, and code cleanliness */
typedef struct RGB {
    float R, G, B; /* nits */
} RGB;

typedef struct IPT {
    float I, P, T;
} IPT;

typedef struct ICh {
    float I, C, h;
} ICh;

static av_always_inline ICh ipt2ich(IPT c)
{
    return (ICh) {
        .I = c.I,
        .C = sqrtf(c.P * c.P + c.T * c.T),
        .h = atan2f(c.T, c.P),
    };
}

static av_always_inline IPT ich2ipt(ICh c)
{
    return (IPT) {
        .I = c.I,
        .P = c.C * cosf(c.h),
        .T = c.C * sinf(c.h),
    };
}

/* Helper struct containing pre-computed cached values describing a gamut */
typedef struct Gamut {
    SwsMatrix3x3 encoding2lms;
    SwsMatrix3x3 lms2encoding;
    SwsMatrix3x3 lms2content;
    SwsMatrix3x3 content2lms;
    av_csp_eotf_function eotf;
    av_csp_eotf_function eotf_inv;
    float Iavg_frame;
    float Imax_frame;
    float Imin, Imax;
    float Lb, Lw;
    AVCIExy wp;
    ICh peak; /* updated as needed in loop body when hue changes */
} Gamut;

static Gamut gamut_from_colorspace(SwsColor fmt)
{
    const AVColorPrimariesDesc *encoding = av_csp_primaries_desc_from_id(fmt.prim);
    const AVColorPrimariesDesc content = {
        .prim = fmt.gamut,
        .wp   = encoding->wp,
    };

    const float Lw = av_q2d(fmt.max_luma), Lb = av_q2d(fmt.min_luma);
    const float Imax = pq_oetf(Lw);

    return (Gamut) {
        .encoding2lms = ff_sws_ipt_rgb2lms(encoding),
        .lms2encoding = ff_sws_ipt_lms2rgb(encoding),
        .lms2content  = ff_sws_ipt_lms2rgb(&content),
        .content2lms  = ff_sws_ipt_rgb2lms(&content),
        .eotf         = av_csp_itu_eotf(fmt.trc),
        .eotf_inv     = av_csp_itu_eotf_inv(fmt.trc),
        .wp           = encoding->wp,
        .Imin         = pq_oetf(Lb),
        .Imax         = Imax,
        .Imax_frame   = fmt.frame_peak.den ? pq_oetf(av_q2d(fmt.frame_peak)) : Imax,
        .Iavg_frame   = fmt.frame_avg.den  ? pq_oetf(av_q2d(fmt.frame_avg))  : 0.0f,
        .Lb           = Lb,
        .Lw           = Lw,
    };
}

static av_always_inline IPT rgb2ipt(RGB c, const SwsMatrix3x3 rgb2lms)
{
    const float L = rgb2lms.m[0][0] * c.R +
                    rgb2lms.m[0][1] * c.G +
                    rgb2lms.m[0][2] * c.B;
    const float M = rgb2lms.m[1][0] * c.R +
                    rgb2lms.m[1][1] * c.G +
                    rgb2lms.m[1][2] * c.B;
    const float S = rgb2lms.m[2][0] * c.R +
                    rgb2lms.m[2][1] * c.G +
                    rgb2lms.m[2][2] * c.B;
    const float Lp = pq_oetf(L);
    const float Mp = pq_oetf(M);
    const float Sp = pq_oetf(S);
    return (IPT) {
        .I = 0.4000f * Lp + 0.4000f * Mp + 0.2000f * Sp,
        .P = 4.4550f * Lp - 4.8510f * Mp + 0.3960f * Sp,
        .T = 0.8056f * Lp + 0.3572f * Mp - 1.1628f * Sp,
    };
}

static av_always_inline RGB ipt2rgb(IPT c, const SwsMatrix3x3 lms2rgb)
{
    const float Lp = c.I + 0.0975689f * c.P + 0.205226f * c.T;
    const float Mp = c.I - 0.1138760f * c.P + 0.133217f * c.T;
    const float Sp = c.I + 0.0326151f * c.P - 0.676887f * c.T;
    const float L = pq_eotf(Lp);
    const float M = pq_eotf(Mp);
    const float S = pq_eotf(Sp);
    return (RGB) {
        .R = lms2rgb.m[0][0] * L +
             lms2rgb.m[0][1] * M +
             lms2rgb.m[0][2] * S,
        .G = lms2rgb.m[1][0] * L +
             lms2rgb.m[1][1] * M +
             lms2rgb.m[1][2] * S,
        .B = lms2rgb.m[2][0] * L +
             lms2rgb.m[2][1] * M +
             lms2rgb.m[2][2] * S,
    };
}

static inline bool ingamut(IPT c, Gamut gamut)
{
    const float min_rgb = gamut.Lb - 1e-4f;
    const float max_rgb = gamut.Lw + 1e-2f;
    const float Lp = c.I + 0.0975689f * c.P + 0.205226f * c.T;
    const float Mp = c.I - 0.1138760f * c.P + 0.133217f * c.T;
    const float Sp = c.I + 0.0326151f * c.P - 0.676887f * c.T;
    if (Lp < gamut.Imin || Lp > gamut.Imax ||
        Mp < gamut.Imin || Mp > gamut.Imax ||
        Sp < gamut.Imin || Sp > gamut.Imax)
    {
        /* Values outside legal LMS range */
        return false;
    } else {
        const float L = pq_eotf(Lp);
        const float M = pq_eotf(Mp);
        const float S = pq_eotf(Sp);
        RGB rgb = {
            .R = gamut.lms2content.m[0][0] * L +
                 gamut.lms2content.m[0][1] * M +
                 gamut.lms2content.m[0][2] * S,
            .G = gamut.lms2content.m[1][0] * L +
                 gamut.lms2content.m[1][1] * M +
                 gamut.lms2content.m[1][2] * S,
            .B = gamut.lms2content.m[2][0] * L +
                 gamut.lms2content.m[2][1] * M +
                 gamut.lms2content.m[2][2] * S,
        };
        return rgb.R >= min_rgb && rgb.R <= max_rgb &&
               rgb.G >= min_rgb && rgb.G <= max_rgb &&
               rgb.B >= min_rgb && rgb.B <= max_rgb;
    }
}

static const float maxDelta = 5e-5f;

// Find gamut intersection using specified bounds
static inline ICh
desat_bounded(float I, float h, float Cmin, float Cmax, Gamut gamut)
{
    if (I <= gamut.Imin)
        return (ICh) { .I = gamut.Imin, .C = 0, .h = h };
    else if (I >= gamut.Imax)
        return (ICh) { .I = gamut.Imax, .C = 0, .h = h };
    else {
        const float maxDI = I * maxDelta;
        ICh res = { .I = I, .C = (Cmin + Cmax) / 2, .h = h };
        do {
            if (ingamut(ich2ipt(res), gamut)) {
                Cmin = res.C;
            } else {
                Cmax = res.C;
            }
            res.C = (Cmin + Cmax) / 2;
        } while (Cmax - Cmin > maxDI);

        return res;
    }
}

// Finds maximally saturated in-gamut color (for given hue)
static inline ICh saturate(float hue, Gamut gamut)
{
    static const float invphi = 0.6180339887498948f;
    static const float invphi2 = 0.38196601125010515f;

    ICh lo = { .I = gamut.Imin, .h = hue };
    ICh hi = { .I = gamut.Imax, .h = hue };
    float de = hi.I - lo.I;
    ICh a = { .I = lo.I + invphi2 * de };
    ICh b = { .I = lo.I + invphi  * de };
    a = desat_bounded(a.I, hue, 0.0f, 0.5f, gamut);
    b = desat_bounded(b.I, hue, 0.0f, 0.5f, gamut);

    while (de > maxDelta) {
        de *= invphi;
        if (a.C > b.C) {
            hi = b;
            b = a;
            a.I = lo.I + invphi2 * de;
            a = desat_bounded(a.I, hue, lo.C - maxDelta, 0.5f, gamut);
        } else {
            lo = a;
            a = b;
            b.I = lo.I + invphi * de;
            b = desat_bounded(b.I, hue, hi.C - maxDelta, 0.5f, gamut);
        }
    }

    return a.C > b.C ? a : b;
}

static float softclip(float value, float source, float target)
{
    const float j = SOFTCLIP_KNEE;
    float peak, x, a, b, scale;
    if (!target)
        return 0.0f;

    peak = source / target;
    x = fminf(value / target, peak);
    if (x <= j || peak <= 1.0)
        return value;

    /* Apply simple mobius function */
    a = -j*j * (peak - 1.0f) / (j*j - 2.0f * j + peak);
    b = (j*j - 2.0f * j * peak + peak) / fmaxf(1e-6f, peak - 1.0f);
    scale = (b*b + 2.0f * b*j + j*j) / (b - a);

    return scale * (x + a) / (x + b) * target;
}

/**
 * Something like fmixf(base, c, x) but follows an exponential curve, note
 * that this can be used to extend 'c' outwards for x > 1
 */
static inline ICh mix_exp(ICh c, float x, float gamma, float base)
{
    return (ICh) {
        .I = base + (c.I - base) * powf(x, gamma),
        .C = c.C * x,
        .h = c.h,
    };
}

/**
 * Drop gamma for colors approaching black and achromatic to avoid numerical
 * instabilities, and excessive brightness boosting of grain, while also
 * strongly boosting gamma for values exceeding the target peak
 */
static inline float scale_gamma(float gamma, ICh ich, Gamut gamut)
{
    const float Imin = gamut.Imin;
    const float Irel = fmaxf((ich.I - Imin) / (gamut.peak.I - Imin), 0.0f);
    return gamma * powf(Irel, 3) * fminf(ich.C / gamut.peak.C, 1.0f);
}

/* Clip a color along the exponential curve given by `gamma` */
static inline IPT clip_gamma(IPT ipt, float gamma, Gamut gamut)
{
    float lo = 0.0f, hi = 1.0f, x = 0.5f;
    const float maxDI = fmaxf(ipt.I * maxDelta, 1e-7f);
    ICh ich;

    if (ipt.I <= gamut.Imin)
        return (IPT) { .I = gamut.Imin };
    if (ingamut(ipt, gamut))
        return ipt;

    ich = ipt2ich(ipt);
    if (!gamma)
        return ich2ipt(desat_bounded(ich.I, ich.h, 0.0f, ich.C, gamut));

    gamma = scale_gamma(gamma, ich, gamut);
    do {
        ICh test = mix_exp(ich, x, gamma, gamut.peak.I);
        if (ingamut(ich2ipt(test), gamut)) {
            lo = x;
        } else {
            hi = x;
        }
        x = (lo + hi) / 2.0f;
    } while (hi - lo > maxDI);

    return ich2ipt(mix_exp(ich, x, gamma, gamut.peak.I));
}

typedef struct CmsCtx CmsCtx;
struct CmsCtx {
    /* Tone mapping parameters */
    float Qa, Qb, Qc, Pa, Pb, src_knee, dst_knee; /* perceptual */
    float I_scale, I_offset; /* linear methods */

    /* Colorspace parameters */
    Gamut src;
    Gamut tmp; /* after tone mapping */
    Gamut dst;
    SwsMatrix3x3 adaptation; /* for absolute intent */

    /* Invocation parameters */
    SwsColorMap map;
    float (*tone_map)(const CmsCtx *ctx, float I);
    IPT (*adapt_colors)(const CmsCtx *ctx, IPT ipt);
    v3u16_t *input;
    v3u16_t *output;

    /* Threading parameters */
    int slice_size;
    int size_input;
    int size_output_I;
    int size_output_PT;
};

/**
 * Helper function to pick a knee point based on the * HDR10+ brightness
 * metadata and scene brightness average matching.
 *
 * Inspired by SMPTE ST2094-10, with some modifications
 */
static void st2094_pick_knee(float src_max, float src_min, float src_avg,
                             float dst_max, float dst_min,
                             float *out_src_knee, float *out_dst_knee)
{
    const float min_knee = PERCEPTUAL_KNEE_MIN;
    const float max_knee = PERCEPTUAL_KNEE_MAX;
    const float def_knee = PERCEPTUAL_KNEE_DEF;
    const float src_knee_min = fmixf(src_min, src_max, min_knee);
    const float src_knee_max = fmixf(src_min, src_max, max_knee);
    const float dst_knee_min = fmixf(dst_min, dst_max, min_knee);
    const float dst_knee_max = fmixf(dst_min, dst_max, max_knee);
    float src_knee, target, adapted, tuning, adaptation, dst_knee;

    /* Choose source knee based on dynamic source scene brightness */
    src_knee = src_avg ? src_avg : fmixf(src_min, src_max, def_knee);
    src_knee = av_clipf(src_knee, src_knee_min, src_knee_max);

    /* Choose target adaptation point based on linearly re-scaling source knee */
    target = (src_knee - src_min) / (src_max - src_min);
    adapted = fmixf(dst_min, dst_max, target);

    /**
     * Choose the destnation knee by picking the perceptual adaptation point
     * between the source knee and the desired target. This moves the knee
     * point, on the vertical axis, closer to the 1:1 (neutral) line.
     *
     * Adjust the adaptation strength towards 1 based on how close the knee
     * point is to its extreme values (min/max knee)
     */
    tuning = smoothstepf(max_knee, def_knee, target) *
             smoothstepf(min_knee, def_knee, target);
    adaptation = fmixf(1.0f, PERCEPTUAL_ADAPTATION, tuning);
    dst_knee = fmixf(src_knee, adapted, adaptation);
    dst_knee = av_clipf(dst_knee, dst_knee_min, dst_knee_max);

    *out_src_knee = src_knee;
    *out_dst_knee = dst_knee;
}

static void tone_map_setup(CmsCtx *ctx, bool dynamic)
{
    const float dst_min = ctx->dst.Imin;
    const float dst_max = ctx->dst.Imax;
    const float src_min = ctx->src.Imin;
    const float src_max = dynamic ? ctx->src.Imax_frame : ctx->src.Imax;
    const float src_avg = dynamic ? ctx->src.Iavg_frame : 0.0f;
    float slope, ratio, in_min, in_max, out_min, out_max, t;

    switch (ctx->map.intent) {
    case SWS_INTENT_PERCEPTUAL:
        st2094_pick_knee(src_max, src_min, src_avg, dst_max, dst_min,
                         &ctx->src_knee, &ctx->dst_knee);

        /* Solve for linear knee (Pa = 0) */
        slope = (ctx->dst_knee - dst_min) / (ctx->src_knee - src_min);

        /**
         * Tune the slope at the knee point slightly: raise it to a user-provided
         * gamma exponent, multiplied by an extra tuning coefficient designed to
         * make the slope closer to 1.0 when the difference in peaks is low, and
         * closer to linear when the difference between peaks is high.
         */
        ratio = src_max / dst_max - 1.0f;
        ratio = av_clipf(SLOPE_TUNING * ratio, SLOPE_OFFSET, 1.0f + SLOPE_OFFSET);
        slope = powf(slope, (1.0f - PERCEPTUAL_CONTRAST) * ratio);

        /* Normalize everything the pivot to make the math easier */
        in_min  = src_min - ctx->src_knee;
        in_max  = src_max - ctx->src_knee;
        out_min = dst_min - ctx->dst_knee;
        out_max = dst_max - ctx->dst_knee;

        /**
         * Solve P of order 2 for:
         *  P(in_min) = out_min
         *  P'(0.0) = slope
         *  P(0.0) = 0.0
         */
        ctx->Pa = (out_min - slope * in_min) / (in_min * in_min);
        ctx->Pb = slope;

        /**
         * Solve Q of order 3 for:
         *  Q(in_max) = out_max
         *  Q''(in_max) = 0.0
         *  Q(0.0) = 0.0
         *  Q'(0.0) = slope
         */
        t = 2 * in_max * in_max;
        ctx->Qa = (slope * in_max - out_max) / (in_max * t);
        ctx->Qb = -3 * (slope * in_max - out_max) / t;
        ctx->Qc = slope;
        break;
    case SWS_INTENT_SATURATION:
        /* Linear stretch */
        ctx->I_scale = (dst_max - dst_min) / (src_max - src_min);
        ctx->I_offset = dst_min - src_min * ctx->I_scale;
        break;
    case SWS_INTENT_RELATIVE_COLORIMETRIC:
        /* Pure black point adaptation */
        ctx->I_scale = src_max / (src_max - src_min) /
                      (dst_max / (dst_max - dst_min));
        ctx->I_offset = dst_min - src_min * ctx->I_scale;
        break;
    case SWS_INTENT_ABSOLUTE_COLORIMETRIC:
        /* Hard clip */
        ctx->I_scale = 1.0f;
        ctx->I_offset = 0.0f;
        break;
    }
}

static av_always_inline IPT tone_map_apply(const CmsCtx *ctx, IPT ipt)
{
    float I = ipt.I, desat;

    if (ctx->map.intent == SWS_INTENT_PERCEPTUAL) {
        const float Pa = ctx->Pa, Pb = ctx->Pb;
        const float Qa = ctx->Qa, Qb = ctx->Qb, Qc = ctx->Qc;
        I -= ctx->src_knee;
        I = I > 0 ? ((Qa * I + Qb) * I + Qc) * I : (Pa * I + Pb) * I;
        I += ctx->dst_knee;
    } else {
        I = ctx->I_scale * I + ctx->I_offset;
    }

    /**
     * Avoids raising saturation excessively when raising brightness, and
     * also desaturates when reducing brightness greatly to account for the
     * reduction in gamut volume.
     */
    desat = fminf(ipt.I / I, hull(I) / hull(ipt.I));
    return (IPT) {
        .I = I,
        .P = ipt.P * desat,
        .T = ipt.T * desat,
    };
}

static IPT perceptual(const CmsCtx *ctx, IPT ipt)
{
    ICh ich = ipt2ich(ipt);
    IPT mapped = rgb2ipt(ipt2rgb(ipt, ctx->tmp.lms2content), ctx->dst.content2lms);
    RGB rgb;
    float maxRGB;

    /* Protect in gamut region */
    const float maxC = fmaxf(ctx->tmp.peak.C, ctx->dst.peak.C);
    float k = smoothstepf(PERCEPTUAL_DEADZONE, 1.0f, ich.C / maxC);
    k *= PERCEPTUAL_STRENGTH;
    ipt.I = fmixf(ipt.I, mapped.I, k);
    ipt.P = fmixf(ipt.P, mapped.P, k);
    ipt.T = fmixf(ipt.T, mapped.T, k);

    rgb = ipt2rgb(ipt, ctx->dst.lms2content);
    maxRGB = fmaxf(rgb.R, fmaxf(rgb.G, rgb.B));
    rgb.R = fmaxf(softclip(rgb.R, maxRGB, ctx->dst.Lw), ctx->dst.Lb);
    rgb.G = fmaxf(softclip(rgb.G, maxRGB, ctx->dst.Lw), ctx->dst.Lb);
    rgb.B = fmaxf(softclip(rgb.B, maxRGB, ctx->dst.Lw), ctx->dst.Lb);

    return rgb2ipt(rgb, ctx->dst.content2lms);
}

static IPT relative(const CmsCtx *ctx, IPT ipt)
{
    return clip_gamma(ipt, COLORIMETRIC_GAMMA, ctx->dst);
}

static IPT absolute(const CmsCtx *ctx, IPT ipt)
{
    RGB rgb = ipt2rgb(ipt, ctx->dst.lms2encoding);
    float c[3] = { rgb.R, rgb.G, rgb.B };
    ff_sws_matrix3x3_apply(&ctx->adaptation, c);
    ipt = rgb2ipt((RGB) { c[0], c[1], c[2] }, ctx->dst.encoding2lms);

    return clip_gamma(ipt, COLORIMETRIC_GAMMA, ctx->dst);
}

static IPT saturation(const CmsCtx * ctx, IPT ipt)
{
    RGB rgb = ipt2rgb(ipt, ctx->tmp.lms2content);
    return rgb2ipt(rgb, ctx->dst.content2lms);
}

static av_always_inline av_const uint16_t av_round16f(float x)
{
    return av_clip_uint16(x * (UINT16_MAX - 1) + 0.5f);
}

/* Call this whenever the hue changes inside the loop body */
static av_always_inline void update_hue_peaks(CmsCtx *ctx, float P, float T)
{
    const float hue = atan2f(T, P);
    switch (ctx->map.intent) {
    case SWS_INTENT_PERCEPTUAL:
        ctx->tmp.peak = saturate(hue, ctx->tmp);
        /* fall through */
    case SWS_INTENT_RELATIVE_COLORIMETRIC:
    case SWS_INTENT_ABSOLUTE_COLORIMETRIC:
        ctx->dst.peak = saturate(hue, ctx->dst);
        return;
    default:
        return;
    }
}

static void generate_slice(void *priv, int jobnr, int threadnr, int nb_jobs,
                           int nb_threads)
{
    CmsCtx ctx = *(const CmsCtx *) priv;

    const int slice_start  = jobnr * ctx.slice_size;
    const int slice_stride = ctx.size_input * ctx.size_input;
    const int slice_end    = FFMIN((jobnr + 1) * ctx.slice_size, ctx.size_input);
    v3u16_t *input = &ctx.input[slice_start * slice_stride];

    const int output_slice_h = (ctx.size_output_PT + nb_jobs - 1) / nb_jobs;
    const int output_start   = jobnr * output_slice_h;
    const int output_stride  = ctx.size_output_PT * ctx.size_output_I;
    const int output_end     = FFMIN((jobnr + 1) * output_slice_h, ctx.size_output_PT);
    v3u16_t *output = ctx.output ? &ctx.output[output_start * output_stride] : NULL;

    const float I_scale   = 1.0f / (ctx.src.Imax - ctx.src.Imin);
    const float I_offset  = -ctx.src.Imin * I_scale;
    const float PT_offset = (float) (1 << 15) / (UINT16_MAX - 1);

    const float input_scale     = 1.0f / (ctx.size_input - 1);
    const float output_scale_PT = 1.0f / (ctx.size_output_PT - 1);
    const float output_scale_I  = (ctx.tmp.Imax - ctx.tmp.Imin) /
                                  (ctx.size_output_I - 1);

    for (int Bx = slice_start; Bx < slice_end; Bx++) {
        const float B = input_scale * Bx;
        for (int Gx = 0; Gx < ctx.size_input; Gx++) {
            const float G = input_scale * Gx;
            for (int Rx = 0; Rx < ctx.size_input; Rx++) {
                double c[3] = { input_scale * Rx, G, B };
                RGB rgb;
                IPT ipt;

                ctx.src.eotf(ctx.src.Lw, ctx.src.Lb, c);
                rgb = (RGB) { c[0], c[1], c[2] };
                ipt = rgb2ipt(rgb, ctx.src.encoding2lms);

                if (output) {
                    /* Save intermediate value to 3DLUT */
                    *input++ = (v3u16_t) {
                        av_round16f(I_scale * ipt.I + I_offset),
                        av_round16f(ipt.P + PT_offset),
                        av_round16f(ipt.T + PT_offset),
                    };
                } else {
                    update_hue_peaks(&ctx, ipt.P, ipt.T);

                    ipt = tone_map_apply(&ctx, ipt);
                    ipt = ctx.adapt_colors(&ctx, ipt);
                    rgb = ipt2rgb(ipt, ctx.dst.lms2encoding);

                    c[0] = rgb.R;
                    c[1] = rgb.G;
                    c[2] = rgb.B;
                    ctx.dst.eotf_inv(ctx.dst.Lw, ctx.dst.Lb, c);
                    *input++ = (v3u16_t) {
                        av_round16f(c[0]),
                        av_round16f(c[1]),
                        av_round16f(c[2]),
                    };
                }
            }
        }
    }

    if (!output)
        return;

    /* Generate split gamut mapping LUT */
    for (int Tx = output_start; Tx < output_end; Tx++) {
        const float T = output_scale_PT * Tx - PT_offset;
        for (int Px = 0; Px < ctx.size_output_PT; Px++) {
            const float P = output_scale_PT * Px - PT_offset;
            update_hue_peaks(&ctx, P, T);

            for (int Ix = 0; Ix < ctx.size_output_I; Ix++) {
                const float I = output_scale_I * Ix + ctx.tmp.Imin;
                IPT ipt = ctx.adapt_colors(&ctx, (IPT) { I, P, T });
                RGB rgb = ipt2rgb(ipt, ctx.dst.lms2encoding);
                double c[3] = { rgb.R, rgb.G, rgb.B };
                ctx.dst.eotf_inv(ctx.dst.Lw, ctx.dst.Lb, c);
                *output++ = (v3u16_t) {
                    av_round16f(c[0]),
                    av_round16f(c[1]),
                    av_round16f(c[2]),
                };
            }
        }
    }
}

int ff_sws_color_map_generate_static(v3u16_t *lut, int size, const SwsColorMap *map)
{
    return ff_sws_color_map_generate_dynamic(lut, NULL, size, 1, 1, map);
}

int ff_sws_color_map_generate_dynamic(v3u16_t *input, v3u16_t *output,
                                      int size_input, int size_I, int size_PT,
                                      const SwsColorMap *map)
{
    AVSliceThread *slicethread;
    int ret, num_slices;

    CmsCtx ctx = {
        .map            = *map,
        .input          = input,
        .output         = output,
        .size_input     = size_input,
        .size_output_I  = size_I,
        .size_output_PT = size_PT,
        .src            = gamut_from_colorspace(map->src),
        .dst            = gamut_from_colorspace(map->dst),
    };

    switch (ctx.map.intent) {
    case SWS_INTENT_PERCEPTUAL:            ctx.adapt_colors = perceptual; break;
    case SWS_INTENT_RELATIVE_COLORIMETRIC: ctx.adapt_colors = relative;   break;
    case SWS_INTENT_SATURATION:            ctx.adapt_colors = saturation; break;
    case SWS_INTENT_ABSOLUTE_COLORIMETRIC: ctx.adapt_colors = absolute;   break;
    default: return AVERROR(EINVAL);
    }

    if (!output) {
        /* Tone mapping is handled in a separate step when using dynamic TM */
        tone_map_setup(&ctx, false);
    }

    /* Intermediate color space after tone mapping */
    ctx.tmp      = ctx.src;
    ctx.tmp.Lb   = ctx.dst.Lb;
    ctx.tmp.Lw   = ctx.dst.Lw;
    ctx.tmp.Imin = ctx.dst.Imin;
    ctx.tmp.Imax = ctx.dst.Imax;

    if (ctx.map.intent == SWS_INTENT_ABSOLUTE_COLORIMETRIC) {
        /**
         * The IPT transform already implies an explicit white point adaptation
         * from src to dst, so to get absolute colorimetric semantics we have
         * to explicitly undo this adaptation with a * corresponding inverse.
         */
        ctx.adaptation = ff_sws_get_adaptation(&ctx.map.dst.gamut,
                                               ctx.dst.wp, ctx.src.wp);
    }

    ret = avpriv_slicethread_create(&slicethread, &ctx, generate_slice, NULL, 0);
    if (ret < 0)
        return ret;

    ctx.slice_size = (ctx.size_input + ret - 1) / ret;
    num_slices = (ctx.size_input + ctx.slice_size - 1) / ctx.slice_size;
    avpriv_slicethread_execute(slicethread, num_slices, 0);
    avpriv_slicethread_free(&slicethread);
    return 0;
}

void ff_sws_tone_map_generate(v2u16_t *lut, int size, const SwsColorMap *map)
{
    CmsCtx ctx = {
        .map = *map,
        .src = gamut_from_colorspace(map->src),
        .dst = gamut_from_colorspace(map->dst),
    };

    const float src_scale  = (ctx.src.Imax - ctx.src.Imin) / (size - 1);
    const float src_offset = ctx.src.Imin;
    const float dst_scale  = 1.0f / (ctx.dst.Imax - ctx.dst.Imin);
    const float dst_offset = -ctx.dst.Imin * dst_scale;

    tone_map_setup(&ctx, true);

    for (int i = 0; i < size; i++) {
        const float I = src_scale * i + src_offset;
        IPT ipt = tone_map_apply(&ctx, (IPT) { I, 1.0f });
        lut[i] = (v2u16_t) {
            av_round16f(dst_scale * ipt.I + dst_offset),
            av_clip_uint16(ipt.P * (1 << 15) + 0.5f),
        };
    }
}
