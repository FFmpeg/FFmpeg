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

#include "libavutil/csp.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "fflcms2.h"
#include "filters.h"
#include "video.h"

typedef struct IccDetectContext {
    const AVClass *class;
    FFIccContext icc;
    int force;
    /* (cached) detected ICC profile values */
    AVBufferRef *profile;
    enum AVColorPrimaries profile_prim;
    enum AVColorTransferCharacteristic profile_trc;
} IccDetectContext;

#define OFFSET(x) offsetof(IccDetectContext, x)
#define VF AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption iccdetect_options[] = {
    { "force", "overwrite existing tags", OFFSET(force), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(iccdetect);

static av_cold void iccdetect_uninit(AVFilterContext *avctx)
{
    IccDetectContext *s = avctx->priv;
    av_buffer_unref(&s->profile);
    ff_icc_context_uninit(&s->icc);
}

static av_cold int iccdetect_init(AVFilterContext *avctx)
{
    IccDetectContext *s = avctx->priv;
    return ff_icc_context_init(&s->icc, avctx);
}

static int iccdetect_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *avctx = inlink->dst;
    IccDetectContext *s = avctx->priv;
    const AVFrameSideData *sd;
    AVColorPrimariesDesc coeffs;
    cmsHPROFILE profile;
    int ret;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE);
    if (!sd)
        return ff_filter_frame(inlink->dst->outputs[0], frame);

    if (s->profile && s->profile->data == sd->buf->data) {
        /* No change from previous ICC profile */
        goto done;
    }

    if ((ret = av_buffer_replace(&s->profile, sd->buf)) < 0)
        return ret;
    s->profile_prim = AVCOL_PRI_UNSPECIFIED;
    s->profile_trc = AVCOL_TRC_UNSPECIFIED;

    profile = cmsOpenProfileFromMemTHR(s->icc.ctx, sd->data, sd->size);
    if (!profile)
        return AVERROR_INVALIDDATA;

    ret = ff_icc_profile_sanitize(&s->icc, profile);
    if (!ret)
        ret = ff_icc_profile_read_primaries(&s->icc, profile, &coeffs);
    if (!ret)
        ret = ff_icc_profile_detect_transfer(&s->icc, profile, &s->profile_trc);
    cmsCloseProfile(profile);
    if (ret < 0)
        return ret;

    s->profile_prim = av_csp_primaries_id_from_desc(&coeffs);

done:
    if (s->profile_prim != AVCOL_PRI_UNSPECIFIED) {
        if (s->force || frame->color_primaries == AVCOL_PRI_UNSPECIFIED)
            frame->color_primaries = s->profile_prim;
    }

    if (s->profile_trc != AVCOL_TRC_UNSPECIFIED) {
        if (s->force || frame->color_trc == AVCOL_TRC_UNSPECIFIED)
            frame->color_trc = s->profile_trc;
    }

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static const AVFilterPad iccdetect_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = iccdetect_filter_frame,
    },
};

const FFFilter ff_vf_iccdetect = {
    .p.name        = "iccdetect",
    .p.description = NULL_IF_CONFIG_SMALL("Detect and parse ICC profiles."),
    .p.priv_class  = &iccdetect_class,
    .p.flags       = AVFILTER_FLAG_METADATA_ONLY,
    .priv_size   = sizeof(IccDetectContext),
    .init        = &iccdetect_init,
    .uninit      = &iccdetect_uninit,
    FILTER_INPUTS(iccdetect_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
};
