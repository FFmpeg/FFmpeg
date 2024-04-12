/*
 * Dolby Vision RPU encoder
 *
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

#include "libavutil/avassert.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "dovi_rpu.h"

static struct {
    uint64_t pps; // maximum pixels per second
    int width; // maximum width
    int main; // maximum bitrate in main tier
    int high; // maximum bitrate in high tier
} dv_levels[] = {
     [1] = {1280*720*24,    1280,  20,  50},
     [2] = {1280*720*30,    1280,  20,  50},
     [3] = {1920*1080*24,   1920,  20,  70},
     [4] = {1920*1080*30,   2560,  20,  70},
     [5] = {1920*1080*60,   3840,  20,  70},
     [6] = {3840*2160*24,   3840,  25, 130},
     [7] = {3840*2160*30,   3840,  25, 130},
     [8] = {3840*2160*48,   3840,  40, 130},
     [9] = {3840*2160*60,   3840,  40, 130},
    [10] = {3840*2160*120,  3840,  60, 240},
    [11] = {3840*2160*120,  7680,  60, 240},
    [12] = {7680*4320*60,   7680, 120, 450},
    [13] = {7680*4320*120u, 7680, 240, 800},
};

int ff_dovi_configure(DOVIContext *s, AVCodecContext *avctx)
{
    AVDOVIDecoderConfigurationRecord *cfg;
    const AVDOVIRpuDataHeader *hdr = NULL;
    const AVFrameSideData *sd;
    int dv_profile, dv_level, bl_compat_id;
    size_t cfg_size;
    uint64_t pps;

    if (!s->enable)
        goto skip;

    sd = av_frame_side_data_get(avctx->decoded_side_data,
                                avctx->nb_decoded_side_data, AV_FRAME_DATA_DOVI_METADATA);

    if (sd)
        hdr = av_dovi_get_header((const AVDOVIMetadata *) sd->data);

    if (s->enable == FF_DOVI_AUTOMATIC && !hdr)
        goto skip;

    switch (avctx->codec_id) {
    case AV_CODEC_ID_AV1:  dv_profile = 10; break;
    case AV_CODEC_ID_H264: dv_profile = 9;  break;
    case AV_CODEC_ID_HEVC: dv_profile = hdr ? ff_dovi_guess_profile_hevc(hdr) : 8; break;
    default:
        /* No other encoder should be calling this! */
        av_assert0(0);
        return AVERROR_BUG;
    }

    if (avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL) {
        if (dv_profile == 9) {
            if (avctx->pix_fmt != AV_PIX_FMT_YUV420P)
                dv_profile = 0;
        } else {
            if (avctx->pix_fmt != AV_PIX_FMT_YUV420P10)
                dv_profile = 0;
        }
    }

    switch (dv_profile) {
    case 0: /* None */
        bl_compat_id = -1;
        break;
    case 4: /* HEVC with enhancement layer */
    case 7:
        if (s->enable > 0) {
            av_log(s->logctx, AV_LOG_ERROR, "Coding of Dolby Vision enhancement "
                   "layers is currently unsupported.");
            return AVERROR_PATCHWELCOME;
        } else {
            goto skip;
        }
    case 5: /* HEVC with proprietary IPTPQc2 */
        bl_compat_id = 0;
        break;
    case 10:
        /* FIXME: check for proper H.273 tags once those are added */
        if (hdr && hdr->bl_video_full_range_flag) {
            /* AV1 with proprietary IPTPQc2 */
            bl_compat_id = 0;
            break;
        }
        /* fall through */
    case 8: /* HEVC (or AV1) with BL compatibility */
        if (avctx->colorspace == AVCOL_SPC_BT2020_NCL &&
            avctx->color_primaries == AVCOL_PRI_BT2020 &&
            avctx->color_trc == AVCOL_TRC_SMPTE2084) {
            bl_compat_id = 1;
        } else if (avctx->colorspace == AVCOL_SPC_BT2020_NCL &&
                   avctx->color_primaries == AVCOL_PRI_BT2020 &&
                   avctx->color_trc == AVCOL_TRC_ARIB_STD_B67) {
            bl_compat_id = 4;
        } else if (avctx->colorspace == AVCOL_SPC_BT709 &&
                   avctx->color_primaries == AVCOL_PRI_BT709 &&
                   avctx->color_trc == AVCOL_TRC_BT709) {
            bl_compat_id = 2;
        } else {
            /* Not a valid colorspace combination */
            bl_compat_id = -1;
        }
    }

    if (!dv_profile || bl_compat_id < 0) {
        if (s->enable > 0) {
            av_log(s->logctx, AV_LOG_ERROR, "Dolby Vision enabled, but could "
                   "not determine profile and compaatibility mode. Double-check "
                   "colorspace and format settings for compatibility?\n");
            return AVERROR(EINVAL);
        }
        goto skip;
    }

    pps = avctx->width * avctx->height;
    if (avctx->framerate.num) {
        pps = pps * avctx->framerate.num / avctx->framerate.den;
    } else {
        pps *= 25; /* sanity fallback */
    }

    dv_level = 0;
    for (int i = 1; i < FF_ARRAY_ELEMS(dv_levels); i++) {
        if (pps > dv_levels[i].pps)
            continue;
        if (avctx->width > dv_levels[i].width)
            continue;
        /* In theory, we should also test the bitrate when known, and
         * distinguish between main and high tier. In practice, just ignore
         * the bitrate constraints and hope they work out. This would ideally
         * be handled by either the encoder or muxer directly. */
        dv_level = i;
        break;
    }

    if (!dv_level) {
        if (avctx->strict_std_compliance >= FF_COMPLIANCE_STRICT) {
            av_log(s->logctx, AV_LOG_ERROR, "Coded PPS (%"PRIu64") and width (%d) "
                   "exceed Dolby Vision limitations\n", pps, avctx->width);
            return AVERROR(EINVAL);
        } else {
            av_log(s->logctx, AV_LOG_WARNING, "Coded PPS (%"PRIu64") and width (%d) "
                   "exceed Dolby Vision limitations. Ignoring, resulting file "
                   "may be non-conforming.\n", pps, avctx->width);
            dv_level = FF_ARRAY_ELEMS(dv_levels) - 1;
        }
    }

    cfg = av_dovi_alloc(&cfg_size);
    if (!cfg)
        return AVERROR(ENOMEM);

    if (!av_packet_side_data_add(&avctx->coded_side_data, &avctx->nb_coded_side_data,
                                 AV_PKT_DATA_DOVI_CONF, cfg, cfg_size, 0)) {
        av_free(cfg);
        return AVERROR(ENOMEM);
    }

    cfg->dv_version_major = 1;
    cfg->dv_version_minor = 0;
    cfg->dv_profile = dv_profile;
    cfg->dv_level = dv_level;
    cfg->rpu_present_flag = 1;
    cfg->el_present_flag = 0;
    cfg->bl_present_flag = 1;
    cfg->dv_bl_signal_compatibility_id = bl_compat_id;

    s->cfg = *cfg;
    return 0;

skip:
    s->cfg = (AVDOVIDecoderConfigurationRecord) {0};
    return 0;
}
