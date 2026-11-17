/*
 * Copyright (c) 2014 Martin Storsjo
 * Copyright (c) 2018 Akamai Technologies, Inc.
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

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/rational.h"

#include "av1.h"
#include "avc.h"
#include "avformat.h"
#include "hevc.h"
#include "internal.h"
#include "lcevc.h"
#include "vpcc.h"

static const struct codec_string {
    enum AVCodecID id;
    char str[8];
} codecs[] = {
    { AV_CODEC_ID_VP8, "vp8" },
    { AV_CODEC_ID_VP9, "vp9" },
    { AV_CODEC_ID_VORBIS, "vorbis" },
    { AV_CODEC_ID_OPUS, "opus" },
    { AV_CODEC_ID_FLAC, "flac" },
    { AV_CODEC_ID_NONE }
};

static void set_vp9_codec_str(void *logctx, const AVCodecParameters *par,
                              const AVRational *frame_rate, AVBPrint *out)
{
    VPCC vpcc;
    int ret = ff_isom_get_vpcc_features(logctx, par, NULL, 0, frame_rate, &vpcc);
    if (ret == 0) {
        av_bprintf(out, "vp09.%02d.%02d.%02d",
                   vpcc.profile, vpcc.level, vpcc.bitdepth);
    } else {
        // Default to just vp9 in case of error while finding out profile or level
        if (logctx)
            av_log(logctx, AV_LOG_WARNING, "Could not find VP9 profile and/or level\n");
        av_bprintf(out, "vp9");
    }
}

/** Byte i of the 48-bit general_constraint_indicator_flags, 0 first. */
static unsigned hevc_constraint_byte(const HVCCProfileTierLevel *ptl, int i)
{
    return (ptl->constraint_indicator_flags >> (40 - 8 * i)) & 0xff;
}

/* 3GPP TS 26.244 and ISO/IEC 14496-15 */
static int set_hevc_codec_str(const AVCodecParameters *par, AVBPrint *out)
{
    HVCCProfileTierLevel ptl;
    uint32_t compatibility;
    int constraint_bytes = 6;
    int ret;

    if (par->codec_tag != MKTAG('h','v','c','1') &&
        par->codec_tag != MKTAG('h','e','v','1'))
        return AVERROR(EINVAL);
    if (!par->extradata)
        return AVERROR(EINVAL);

    ret = ff_hevc_get_profile_tier_level(&ptl, par->extradata,
                                         par->extradata_size);
    if (ret < 0)
        return ret;
    /* No parameter set, or a zeroed hvcC record as some muxers write */
    if (!ptl.profile_idc || !ptl.level_idc)
        return AVERROR(EINVAL);

    /* general_profile_compatibility_flags in reverse bit order */
    compatibility = ptl.profile_compatibility_flags;
    compatibility = ((compatibility & 0x55555555U) << 1) | ((compatibility >> 1) & 0x55555555U);
    compatibility = ((compatibility & 0x33333333U) << 2) | ((compatibility >> 2) & 0x33333333U);
    compatibility = ((compatibility & 0x0F0F0F0FU) << 4) | ((compatibility >> 4) & 0x0F0F0F0FU);
    compatibility = ((compatibility & 0x00FF00FFU) << 8) | ((compatibility >> 8) & 0x00FF00FFU);
    compatibility = (compatibility << 16) | (compatibility >> 16);

    av_bprintf(out, "%s.", av_fourcc2str(par->codec_tag));
    /* general_profile_space 1 to 3 is a letter A to C before the profile */
    if (ptl.profile_space)
        av_bprint_chars(out, 'A' + ptl.profile_space - 1, 1);
    av_bprintf(out, "%d.%"PRIx32".%c%d", ptl.profile_idc, compatibility,
               ptl.tier_flag ? 'H' : 'L', ptl.level_idc);

    /* The six constraint bytes, trailing zero bytes omitted */
    while (constraint_bytes > 1 &&
           !hevc_constraint_byte(&ptl, constraint_bytes - 1))
        constraint_bytes--;
    for (int i = 0; i < constraint_bytes; i++)
        av_bprintf(out, ".%02x", hevc_constraint_byte(&ptl, i));

    return 0;
}

int ff_make_codec_str(void *logctx, const AVCodecParameters *par,
                      const AVRational *frame_rate, struct AVBPrint *out)
{
    int i;

    // common Webm codecs are not part of RFC 6381
    for (i = 0; codecs[i].id != AV_CODEC_ID_NONE; i++)
        if (codecs[i].id == par->codec_id) {
            if (codecs[i].id == AV_CODEC_ID_VP9) {
                set_vp9_codec_str(logctx, par, frame_rate, out);
            } else {
                av_bprintf(out, "%s", codecs[i].str);
            }
            return 0;
        }

    if (par->codec_id == AV_CODEC_ID_H264) {
        // RFC 6381
        uint8_t *data = par->extradata;
        if (data) {
            const uint8_t *p;

            if (AV_RB32(data) == 0x01 && (data[4] & 0x1F) == 7)
                p = &data[5];
            else if (AV_RB24(data) == 0x01 && (data[3] & 0x1F) == 7)
                p = &data[4];
            else if (data[0] == 0x01)  /* avcC */
                p = &data[1];
            else
                return AVERROR(EINVAL);
            /* avc3 differs from avc1 by where the parameter sets are
             * stored; the profile and level part is the same
             * (ISO/IEC 14496-15) */
            av_bprintf(out, "%s.%02x%02x%02x",
                       par->codec_tag == MKTAG('a','v','c','3') ? "avc3" : "avc1",
                       p[0], p[1], p[2]);
        } else {
            return AVERROR(EINVAL);
        }
    } else if (par->codec_id == AV_CODEC_ID_HEVC) {
        return set_hevc_codec_str(par, out);
    } else if (par->codec_id == AV_CODEC_ID_LCEVC) {
        LCEVCDecoderConfigurationRecord lvcc;
        int err;
        if (!par->extradata_size)
            return AVERROR(EINVAL);
        if ((err = ff_lcvec_parse_config_record(&lvcc, par->extradata, par->extradata_size, logctx)) < 0)
            return err;
        av_bprintf(out, "lvc1.vprf%u.vlev%u", lvcc.profile_idc, lvcc.level_idc);
    } else if (par->codec_id == AV_CODEC_ID_AV1) {
        // https://aomediacodec.github.io/av1-isobmff/#codecsparam
        AV1SequenceParameters seq;
        int err;
        if (!par->extradata_size)
            return AVERROR(EINVAL);
        if ((err = ff_av1_parse_seq_header(&seq, par->extradata, par->extradata_size)) < 0)
            return err;

        av_bprintf(out, "av01.%01u.%02u%s.%02u",
                    seq.profile, seq.level, seq.tier ? "H" : "M", seq.bitdepth);
        if (seq.color_description_present_flag)
            av_bprintf(out, ".%01u.%01u%01u%01u.%02u.%02u.%02u.%01u",
                       seq.monochrome,
                       seq.chroma_subsampling_x, seq.chroma_subsampling_y, seq.chroma_sample_position,
                       seq.color_primaries, seq.transfer_characteristics, seq.matrix_coefficients,
                       seq.color_range);
    } else if (par->codec_id == AV_CODEC_ID_MPEG4) {
        // RFC 6381
        av_bprintf(out, "mp4v.20");
        // Unimplemented, should output ProfileLevelIndication as a decimal number
        if (logctx)
            av_log(logctx, AV_LOG_WARNING, "Incomplete RFC 6381 codec string for mp4v\n");
    } else if (par->codec_id == AV_CODEC_ID_MP2) {
        av_bprintf(out, "mp4a.40.33");
    } else if (par->codec_id == AV_CODEC_ID_MP3) {
        av_bprintf(out, "mp4a.40.34");
    } else if (par->codec_id == AV_CODEC_ID_AAC) {
        // RFC 6381
        int aot = 2;
        if (par->extradata_size >= 2) {
            aot = par->extradata[0] >> 3;
            if (aot == 31)
                aot = ((AV_RB16(par->extradata) >> 5) & 0x3f) + 32;
        } else if (par->profile != AV_PROFILE_UNKNOWN)
            aot = par->profile + 1;
        av_bprintf(out, "mp4a.40.%d", aot);
    } else if (par->codec_id == AV_CODEC_ID_AC3) {
        av_bprintf(out, "ac-3");
    } else if (par->codec_id == AV_CODEC_ID_EAC3) {
        av_bprintf(out, "ec-3");
    } else {
        return AVERROR(EINVAL);
    }
    return 0;
}

int av_mime_codec_str(const AVCodecParameters *par,
                      AVRational frame_rate, struct AVBPrint *out)
{
    return ff_make_codec_str(NULL, par, &frame_rate, out);
}
