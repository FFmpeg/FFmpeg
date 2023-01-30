/*
 * Copyright (c) 2022 Niklas Haas
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

#include "libavutil/csp.h"

#include "fflcms2.h"

static void log_cb(cmsContext ctx, cmsUInt32Number error, const char *str)
{
    FFIccContext *s = cmsGetContextUserData(ctx);
    av_log(s->avctx, AV_LOG_ERROR, "lcms2: [%"PRIu32"] %s\n", error, str);
}

int ff_icc_context_init(FFIccContext *s, void *avctx)
{
    memset(s, 0, sizeof(*s));
    s->avctx = avctx;
    s->ctx = cmsCreateContext(NULL, s);
    if (!s->ctx)
        return AVERROR(ENOMEM);

    cmsSetLogErrorHandlerTHR(s->ctx, log_cb);
    return 0;
}

void ff_icc_context_uninit(FFIccContext *s)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(s->curves); i++)
        cmsFreeToneCurve(s->curves[i]);
    cmsDeleteContext(s->ctx);
    memset(s, 0, sizeof(*s));
}

static int get_curve(FFIccContext *s, enum AVColorTransferCharacteristic trc,
                     cmsToneCurve **out_curve)
{
    if (trc >= AVCOL_TRC_NB)
        return AVERROR_INVALIDDATA;

    if (s->curves[trc])
        goto done;

    switch (trc) {
    case AVCOL_TRC_LINEAR:
        s->curves[trc] = cmsBuildGamma(s->ctx, 1.0);
        break;
    case AVCOL_TRC_GAMMA22:
        s->curves[trc] = cmsBuildGamma(s->ctx, 2.2);
        break;
    case AVCOL_TRC_GAMMA28:
        s->curves[trc] = cmsBuildGamma(s->ctx, 2.8);
        break;
    case AVCOL_TRC_BT709:
    case AVCOL_TRC_SMPTE170M:
    case AVCOL_TRC_BT2020_10:
    case AVCOL_TRC_BT2020_12:
        s->curves[trc] = cmsBuildParametricToneCurve(s->ctx, 4, (double[5]) {
            /* γ = */ 1/0.45,
            /* a = */ 1/1.099296826809442,
            /* b = */ 1 - 1/1.099296826809442,
            /* c = */ 1/4.5,
            /* d = */ 4.5 * 0.018053968510807,
        });
        break;
    case AVCOL_TRC_SMPTE240M:
        s->curves[trc] = cmsBuildParametricToneCurve(s->ctx, 4, (double[5]) {
            /* γ = */ 1/0.45,
            /* a = */ 1/1.1115,
            /* b = */ 1 - 1/1.1115,
            /* c = */ 1/4.0,
            /* d = */ 4.0 * 0.0228,
        });
        break;
    case AVCOL_TRC_LOG:
        s->curves[trc] = cmsBuildParametricToneCurve(s->ctx, 8, (double[5]) {
            /* a = */ 1.0,
            /* b = */ 10.0,
            /* c = */ 2.0,
            /* d = */ -1.0,
            /* e = */ 0.0
        });
        break;
    case AVCOL_TRC_LOG_SQRT:
        s->curves[trc] = cmsBuildParametricToneCurve(s->ctx, 8, (double[5]) {
            /* a = */ 1.0,
            /* b = */ 10.0,
            /* c = */ 2.5,
            /* d = */ -1.0,
            /* e = */ 0.0
        });
        break;
    case AVCOL_TRC_IEC61966_2_1:
        s->curves[trc] = cmsBuildParametricToneCurve(s->ctx, 4, (double[5]) {
            /* γ = */ 2.4,
            /* a = */ 1/1.055,
            /* b = */ 1 - 1/1.055,
            /* c = */ 1/12.92,
            /* d = */ 12.92 * 0.0031308,
        });
        break;
    case AVCOL_TRC_SMPTE428:
        s->curves[trc] = cmsBuildParametricToneCurve(s->ctx, 2, (double[3]) {
            /* γ = */ 2.6,
            /* a = */ pow(52.37/48.0, 1/2.6),
            /* b = */ 0.0
        });
        break;

    /* Can't be represented using the existing parametric tone curves.
     * FIXME: use cmsBuildTabulatedToneCurveFloat instead */
    case AVCOL_TRC_IEC61966_2_4:
    case AVCOL_TRC_BT1361_ECG:
    case AVCOL_TRC_SMPTE2084:
    case AVCOL_TRC_ARIB_STD_B67:
        return AVERROR_PATCHWELCOME;

    default:
        return AVERROR_INVALIDDATA;
    }

    if (!s->curves[trc])
        return AVERROR(ENOMEM);

done:
    *out_curve = s->curves[trc];
    return 0;
}

int ff_icc_profile_generate(FFIccContext *s,
                            enum AVColorPrimaries color_prim,
                            enum AVColorTransferCharacteristic color_trc,
                            cmsHPROFILE *out_profile)
{
    cmsToneCurve *tonecurve;
    const AVColorPrimariesDesc *prim;
    int ret;

    if (!(prim = av_csp_primaries_desc_from_id(color_prim)))
        return AVERROR_INVALIDDATA;
    if ((ret = get_curve(s, color_trc, &tonecurve)) < 0)
        return ret;

    *out_profile = cmsCreateRGBProfileTHR(s->ctx,
        &(cmsCIExyY) { av_q2d(prim->wp.x), av_q2d(prim->wp.y), 1.0 },
        &(cmsCIExyYTRIPLE) {
            .Red    = { av_q2d(prim->prim.r.x), av_q2d(prim->prim.r.y), 1.0 },
            .Green  = { av_q2d(prim->prim.g.x), av_q2d(prim->prim.g.y), 1.0 },
            .Blue   = { av_q2d(prim->prim.b.x), av_q2d(prim->prim.b.y), 1.0 },
        },
        (cmsToneCurve *[3]) { tonecurve, tonecurve, tonecurve }
    );

    return *out_profile == NULL ? AVERROR(ENOMEM) : 0;
}

int ff_icc_profile_attach(FFIccContext *s, cmsHPROFILE profile, AVFrame *frame)
{
    cmsUInt32Number size;
    AVBufferRef *buf;

    if (!cmsSaveProfileToMem(profile, NULL, &size))
        return AVERROR_EXTERNAL;

    buf = av_buffer_alloc(size);
    if (!buf)
        return AVERROR(ENOMEM);

    if (!cmsSaveProfileToMem(profile, buf->data, &size) || size != buf->size) {
        av_buffer_unref(&buf);
        return AVERROR_EXTERNAL;
    }

    if (!av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_ICC_PROFILE, buf)) {
        av_buffer_unref(&buf);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static av_always_inline void XYZ_xy(cmsCIEXYZ XYZ, AVCIExy *xy)
{
    double k = 1.0 / (XYZ.X + XYZ.Y + XYZ.Z);
    xy->x = av_d2q(k * XYZ.X, 100000);
    xy->y = av_d2q(k * XYZ.Y, 100000);
}

int ff_icc_profile_read_primaries(FFIccContext *s, cmsHPROFILE profile,
                                  AVColorPrimariesDesc *out_primaries)
{
    static const uint8_t testprimaries[4][3] = {
        { 0xFF,    0,    0 }, /* red */
        {    0, 0xFF,    0 }, /* green */
        {    0,    0, 0xFF }, /* blue */
        { 0xFF, 0xFF, 0xFF }, /* white */
    };

    AVWhitepointCoefficients *wp = &out_primaries->wp;
    AVPrimaryCoefficients *prim = &out_primaries->prim;
    cmsFloat64Number prev_adapt;
    cmsHPROFILE xyz;
    cmsHTRANSFORM tf;
    cmsCIEXYZ dst[4];

    xyz = cmsCreateXYZProfileTHR(s->ctx);
    if (!xyz)
        return AVERROR(ENOMEM);

    /* We need to use an unadapted observer to get the raw values */
    prev_adapt = cmsSetAdaptationStateTHR(s->ctx, 0.0);
    tf = cmsCreateTransformTHR(s->ctx, profile, TYPE_RGB_8, xyz, TYPE_XYZ_DBL,
                               INTENT_ABSOLUTE_COLORIMETRIC,
                               /* Note: These flags mostly don't do anything
                                * anyway, but specify them regardless */
                               cmsFLAGS_NOCACHE |
                               cmsFLAGS_NOOPTIMIZE |
                               cmsFLAGS_LOWRESPRECALC |
                               cmsFLAGS_GRIDPOINTS(2));
    cmsSetAdaptationStateTHR(s->ctx, prev_adapt);
    cmsCloseProfile(xyz);
    if (!tf) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid ICC profile (e.g. CMYK)\n");
        return AVERROR_INVALIDDATA;
    }

    cmsDoTransform(tf, testprimaries, dst, 4);
    cmsDeleteTransform(tf);
    XYZ_xy(dst[0], &prim->r);
    XYZ_xy(dst[1], &prim->g);
    XYZ_xy(dst[2], &prim->b);
    XYZ_xy(dst[3], wp);
    return 0;
}

int ff_icc_profile_detect_transfer(FFIccContext *s, cmsHPROFILE profile,
                                   enum AVColorTransferCharacteristic *out_trc)
{
    /* 8-bit linear grayscale ramp */
    static const uint8_t testramp[16][3] = {
        {  1,   1,   1}, /* avoid exact zero due to log100 etc. */
        { 17,  17,  17},
        { 34,  34,  34},
        { 51,  51,  51},
        { 68,  68,  68},
        { 85,  85,  85},
        { 02,  02,  02},
        {119, 119, 119},
        {136, 136, 136},
        {153, 153, 153},
        {170, 170, 170},
        {187, 187, 187},
        {204, 204, 204},
        {221, 221, 221},
        {238, 238, 238},
        {255, 255, 255},
    };

    double dst[FF_ARRAY_ELEMS(testramp)];

    for (enum AVColorTransferCharacteristic trc = 0; trc < AVCOL_TRC_NB; trc++) {
        cmsToneCurve *tonecurve;
        cmsHPROFILE ref;
        cmsHTRANSFORM tf;
        double delta = 0.0;
        if (get_curve(s, trc, &tonecurve) < 0)
            continue;

        ref = cmsCreateGrayProfileTHR(s->ctx, cmsD50_xyY(), tonecurve);
        if (!ref)
            return AVERROR(ENOMEM);

        tf = cmsCreateTransformTHR(s->ctx, profile, TYPE_RGB_8, ref, TYPE_GRAY_DBL,
                                   INTENT_RELATIVE_COLORIMETRIC,
                                   cmsFLAGS_NOCACHE | cmsFLAGS_NOOPTIMIZE);
        cmsCloseProfile(ref);
        if (!tf) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid ICC profile (e.g. CMYK)\n");
            return AVERROR_INVALIDDATA;
        }

        cmsDoTransform(tf, testramp, dst, FF_ARRAY_ELEMS(dst));
        cmsDeleteTransform(tf);

        for (int i = 0; i < FF_ARRAY_ELEMS(dst); i++)
            delta += fabs(testramp[i][0] / 255.0 - dst[i]);
        if (delta < 0.01) {
            *out_trc = trc;
            return 0;
        }
    }

    *out_trc = AVCOL_TRC_UNSPECIFIED;
    return 0;
}
