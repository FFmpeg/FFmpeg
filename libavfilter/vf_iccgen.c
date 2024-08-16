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

/**
 * @file
 * filter for generating ICC profiles
 */

#include <lcms2.h>

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "fflcms2.h"
#include "filters.h"
#include "video.h"

typedef struct IccGenContext {
    const AVClass *class;
    FFIccContext icc;
    /* options */
    int color_prim;
    int color_trc;
    int force;
    /* (cached) generated ICC profile */
    cmsHPROFILE profile;
    int profile_prim;
    int profile_trc;
} IccGenContext;

#define OFFSET(x) offsetof(IccGenContext, x)
#define VF AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption iccgen_options[] = {
    {"color_primaries", "select color primaries", OFFSET(color_prim), AV_OPT_TYPE_INT, {.i64=0}, 0, AVCOL_PRI_NB-1, VF, .unit = "color_primaries"},
        {"auto",          "infer based on frame",  0, AV_OPT_TYPE_CONST, {.i64=0},                      0, 0, VF, .unit = "color_primaries"},
        {"bt709",                           NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT709},        0, 0, VF, .unit = "color_primaries"},
        {"bt470m",                          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT470M},       0, 0, VF, .unit = "color_primaries"},
        {"bt470bg",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT470BG},      0, 0, VF, .unit = "color_primaries"},
        {"smpte170m",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE170M},    0, 0, VF, .unit = "color_primaries"},
        {"smpte240m",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE240M},    0, 0, VF, .unit = "color_primaries"},
        {"film",                            NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_FILM},         0, 0, VF, .unit = "color_primaries"},
        {"bt2020",                          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT2020},       0, 0, VF, .unit = "color_primaries"},
        {"smpte428",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE428},     0, 0, VF, .unit = "color_primaries"},
        {"smpte431",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE431},     0, 0, VF, .unit = "color_primaries"},
        {"smpte432",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE432},     0, 0, VF, .unit = "color_primaries"},
        {"jedec-p22",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_JEDEC_P22},    0, 0, VF, .unit = "color_primaries"},
        {"ebu3213",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_EBU3213},      0, 0, VF, .unit = "color_primaries"},
    {"color_trc", "select color transfer", OFFSET(color_trc), AV_OPT_TYPE_INT, {.i64=0}, 0, AVCOL_TRC_NB-1, VF, .unit = "color_trc"},
        {"auto",         "infer based on frame",  0, AV_OPT_TYPE_CONST, {.i64=0},                      0, 0, VF, .unit = "color_trc"},
        {"bt709",                          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT709},        0, 0, VF, .unit = "color_trc"},
        {"bt470m",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_GAMMA22},      0, 0, VF, .unit = "color_trc"},
        {"bt470bg",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_GAMMA28},      0, 0, VF, .unit = "color_trc"},
        {"smpte170m",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE170M},    0, 0, VF, .unit = "color_trc"},
        {"smpte240m",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE240M},    0, 0, VF, .unit = "color_trc"},
        {"linear",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_LINEAR},       0, 0, VF, .unit = "color_trc"},
        {"iec61966-2-4",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_IEC61966_2_4}, 0, 0, VF, .unit = "color_trc"},
        {"bt1361e",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT1361_ECG},   0, 0, VF, .unit = "color_trc"},
        {"iec61966-2-1",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_IEC61966_2_1}, 0, 0, VF, .unit = "color_trc"},
        {"bt2020-10",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT2020_10},    0, 0, VF, .unit = "color_trc"},
        {"bt2020-12",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT2020_12},    0, 0, VF, .unit = "color_trc"},
        {"smpte2084",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE2084},    0, 0, VF, .unit = "color_trc"},
        {"arib-std-b67",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_ARIB_STD_B67}, 0, 0, VF, .unit = "color_trc"},
    { "force", "overwrite existing ICC profile", OFFSET(force), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(iccgen);

static av_cold void iccgen_uninit(AVFilterContext *avctx)
{
    IccGenContext *s = avctx->priv;
    cmsCloseProfile(s->profile);
    ff_icc_context_uninit(&s->icc);
}

static av_cold int iccgen_init(AVFilterContext *avctx)
{
    IccGenContext *s = avctx->priv;
    return ff_icc_context_init(&s->icc, avctx);
}

static int iccgen_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *avctx = inlink->dst;
    IccGenContext *s = avctx->priv;
    enum AVColorTransferCharacteristic trc;
    enum AVColorPrimaries prim;
    int ret;

    if (av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE)) {
        if (s->force) {
            av_frame_remove_side_data(frame, AV_FRAME_DATA_ICC_PROFILE);
        } else {
            return ff_filter_frame(inlink->dst->outputs[0], frame);
        }
    }

    trc = s->color_trc ? s->color_trc : frame->color_trc;
    if (trc == AVCOL_TRC_UNSPECIFIED) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
        if (!desc)
            return AVERROR_INVALIDDATA;

        if ((desc->flags & AV_PIX_FMT_FLAG_RGB) || frame->color_range == AVCOL_RANGE_JPEG) {
            /* Default to sRGB for RGB or full-range content */
            trc = AVCOL_TRC_IEC61966_2_1;
        } else {
            /* Default to an ITU-R transfer depending on the bit-depth */
            trc = desc->comp[0].depth >= 12 ? AVCOL_TRC_BT2020_12
                : desc->comp[0].depth >= 10 ? AVCOL_TRC_BT2020_10
                : AVCOL_TRC_BT709;
        }
    }

    prim = s->color_prim ? s->color_prim : frame->color_primaries;
    if (prim == AVCOL_PRI_UNSPECIFIED) {
        /* Simply always default to sRGB/BT.709 primaries to avoid surprises */
        prim = AVCOL_PRI_BT709;
    }

    if (s->profile && prim != s->profile_prim && trc != s->profile_trc) {
        cmsCloseProfile(s->profile);
        s->profile = NULL;
    }

    if (!s->profile) {
        if ((ret = ff_icc_profile_generate(&s->icc, prim, trc, &s->profile)) < 0)
            return ret;
        s->profile_prim = prim;
        s->profile_trc = trc;
    }

    if ((ret = ff_icc_profile_attach(&s->icc, s->profile, frame)) < 0)
        return ret;

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static const AVFilterPad iccgen_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = iccgen_filter_frame,
    },
};

const AVFilter ff_vf_iccgen = {
    .name        = "iccgen",
    .description = NULL_IF_CONFIG_SMALL("Generate and attach ICC profiles."),
    .priv_size   = sizeof(IccGenContext),
    .priv_class  = &iccgen_class,
    .flags       = AVFILTER_FLAG_METADATA_ONLY,
    .init        = &iccgen_init,
    .uninit      = &iccgen_uninit,
    FILTER_INPUTS(iccgen_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
};
