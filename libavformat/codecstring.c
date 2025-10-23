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
#include "libavutil/mem.h"
#include "libavutil/rational.h"

#include "av1.h"
#include "avc.h"
#include "avformat.h"
#include "internal.h"
#include "nal.h"
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
            av_bprintf(out, "avc1.%02x%02x%02x", p[0], p[1], p[2]);
        } else {
            return AVERROR(EINVAL);
        }
    } else if (par->codec_id == AV_CODEC_ID_HEVC) {
        // 3GPP TS 26.244
        uint8_t *data = par->extradata;
        int profile = AV_PROFILE_UNKNOWN;
        uint32_t profile_compatibility = AV_PROFILE_UNKNOWN;
        char tier = 0;
        int level = AV_LEVEL_UNKNOWN;
        char constraints[8] = "";

        if (par->profile != AV_PROFILE_UNKNOWN)
            profile = par->profile;
        if (par->level != AV_LEVEL_UNKNOWN)
            level = par->level;

        /* check the boundary of data which from current position is small than extradata_size */
        while (data && (data - par->extradata + 19) < par->extradata_size) {
            /* get HEVC SPS NAL and seek to profile_tier_level */
            if (!(data[0] | data[1] | data[2]) && data[3] == 1 && ((data[4] & 0x7E) == 0x42)) {
                uint8_t *rbsp_buf;
                int remain_size = 0;
                int rbsp_size = 0;
                uint32_t profile_compatibility_flags = 0;
                uint8_t high_nibble = 0;
                /* skip start code + nalu header */
                data += 6;
                /* process by reference General NAL unit syntax */
                remain_size = par->extradata_size - (data - par->extradata);
                rbsp_buf = ff_nal_unit_extract_rbsp(data, remain_size, &rbsp_size, 0);
                if (!rbsp_buf)
                    return AVERROR(EINVAL);
                if (rbsp_size < 13) {
                    av_freep(&rbsp_buf);
                    break;
                }
                /* skip sps_video_parameter_set_id   u(4),
                 *      sps_max_sub_layers_minus1    u(3),
                 *  and sps_temporal_id_nesting_flag u(1)
                 *
                 * TIER represents the general_tier_flag, with 'L' indicating the flag is 0,
                 * and 'H' indicating the flag is 1
                 */
                tier = (rbsp_buf[1] & 0x20) == 0 ? 'L' : 'H';
                profile = rbsp_buf[1] & 0x1f;
                /* PROFILE_COMPATIBILITY is general_profile_compatibility_flags, but in reverse bit order,
                 * in a hexadecimal representation (leading zeroes may be omitted).
                 */
                profile_compatibility_flags = AV_RB32(rbsp_buf + 2);
                /* revise these bits to get the profile compatibility value */
                profile_compatibility_flags = ((profile_compatibility_flags & 0x55555555U) << 1) | ((profile_compatibility_flags >> 1) & 0x55555555U);
                profile_compatibility_flags = ((profile_compatibility_flags & 0x33333333U) << 2) | ((profile_compatibility_flags >> 2) & 0x33333333U);
                profile_compatibility_flags = ((profile_compatibility_flags & 0x0F0F0F0FU) << 4) | ((profile_compatibility_flags >> 4) & 0x0F0F0F0FU);
                profile_compatibility_flags = ((profile_compatibility_flags & 0x00FF00FFU) << 8) | ((profile_compatibility_flags >> 8) & 0x00FF00FFU);
                profile_compatibility = (profile_compatibility_flags << 16) | (profile_compatibility_flags >> 16);
                /* skip 8 + 8 + 32
                 * CONSTRAINTS is a hexadecimal representation of the general_constraint_indicator_flags.
                 * each byte is separated by a '.', and trailing zero bytes may be omitted.
                 * drop the trailing zero bytes refer to ISO/IEC14496-15.
                 */
                high_nibble = rbsp_buf[7] >> 4;
                snprintf(constraints, sizeof(constraints),
                         high_nibble ? "%02x.%x" : "%02x",
                         rbsp_buf[6], high_nibble);
                /* skip 8 + 8 + 32 + 4 + 43 + 1 bit */
                level = rbsp_buf[12];
                av_freep(&rbsp_buf);
                break;
            }
            data++;
        }
        if (par->codec_tag == MKTAG('h','v','c','1') &&
            profile != AV_PROFILE_UNKNOWN &&
            profile_compatibility != AV_PROFILE_UNKNOWN &&
            tier != 0 &&
            level != AV_LEVEL_UNKNOWN &&
            constraints[0] != '\0') {
            av_bprintf(out, "%s.%d.%x.%c%d.%s",
                       av_fourcc2str(par->codec_tag), profile,
                       profile_compatibility, tier, level, constraints);
        } else
            return AVERROR(EINVAL);
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
