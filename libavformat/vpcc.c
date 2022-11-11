/*
 * Copyright (c) 2016 Google Inc.
 * Copyright (c) 2016 KongQun Yang (kqyang@google.com)
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

#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/get_bits.h"
#include "vpcc.h"

#define VP9_SYNCCODE 0x498342

enum VPX_CHROMA_SUBSAMPLING
{
    VPX_SUBSAMPLING_420_VERTICAL = 0,
    VPX_SUBSAMPLING_420_COLLOCATED_WITH_LUMA = 1,
    VPX_SUBSAMPLING_422 = 2,
    VPX_SUBSAMPLING_444 = 3,
};

static int get_vpx_chroma_subsampling(AVFormatContext *s,
                                      enum AVPixelFormat pixel_format,
                                      enum AVChromaLocation chroma_location)
{
    int chroma_w, chroma_h;
    if (av_pix_fmt_get_chroma_sub_sample(pixel_format, &chroma_w, &chroma_h) == 0) {
        if (chroma_w == 1 && chroma_h == 1) {
            return (chroma_location == AVCHROMA_LOC_LEFT)
                       ? VPX_SUBSAMPLING_420_VERTICAL
                       : VPX_SUBSAMPLING_420_COLLOCATED_WITH_LUMA;
        } else if (chroma_w == 1 && chroma_h == 0) {
            return VPX_SUBSAMPLING_422;
        } else if (chroma_w == 0 && chroma_h == 0) {
            return VPX_SUBSAMPLING_444;
        }
    }
    av_log(s, AV_LOG_ERROR, "Unsupported pixel format (%d)\n", pixel_format);
    return -1;
}

static int get_bit_depth(AVFormatContext *s, enum AVPixelFormat pixel_format)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pixel_format);
    if (desc == NULL) {
        av_log(s, AV_LOG_ERROR, "Unsupported pixel format (%d)\n",
               pixel_format);
        return -1;
    }
    return desc->comp[0].depth;
}

static int get_vpx_video_full_range_flag(enum AVColorRange color_range)
{
    return color_range == AVCOL_RANGE_JPEG;
}

// Find approximate VP9 level based on the Luma's Sample rate and Picture size.
static int get_vp9_level(AVCodecParameters *par, AVRational *frame_rate) {
    int picture_size = par->width * par->height;
    int64_t sample_rate;

    // All decisions will be based on picture_size, if frame rate is missing/invalid
    if (!frame_rate || !frame_rate->den)
        sample_rate = 0;
    else
        sample_rate = ((int64_t)picture_size * frame_rate->num) / frame_rate->den;

    if (picture_size <= 0) {
        return 0;
    } else if (sample_rate <= 829440     && picture_size <= 36864) {
        return 10;
    } else if (sample_rate <= 2764800    && picture_size <= 73728) {
        return 11;
    } else if (sample_rate <= 4608000    && picture_size <= 122880) {
        return 20;
    } else if (sample_rate <= 9216000    && picture_size <= 245760) {
        return 21;
    } else if (sample_rate <= 20736000   && picture_size <= 552960) {
        return 30;
    } else if (sample_rate <= 36864000   && picture_size <= 983040) {
        return 31;
    } else if (sample_rate <= 83558400   && picture_size <= 2228224) {
        return 40;
    } else if (sample_rate <= 160432128  && picture_size <= 2228224) {
        return 41;
    } else if (sample_rate <= 311951360  && picture_size <= 8912896) {
        return 50;
    } else if (sample_rate <= 588251136  && picture_size <= 8912896) {
        return 51;
    } else if (sample_rate <= 1176502272 && picture_size <= 8912896) {
        return 52;
    } else if (sample_rate <= 1176502272 && picture_size <= 35651584) {
        return 60;
    } else if (sample_rate <= 2353004544 && picture_size <= 35651584) {
        return 61;
    } else if (sample_rate <= 4706009088 && picture_size <= 35651584) {
        return 62;
    } else {
        return 0;
    }
}

static void parse_bitstream(GetBitContext *gb, int *profile, int *bit_depth) {
    int keyframe, invisible;

    if (get_bits(gb, 2) != 0x2) // frame marker
        return;
    *profile  = get_bits1(gb);
    *profile |= get_bits1(gb) << 1;
    if (*profile == 3)
        *profile += get_bits1(gb);

    if (get_bits1(gb))
        return;

    keyframe = !get_bits1(gb);
    invisible = !get_bits1(gb);
    get_bits1(gb);

    if (keyframe) {
        if (get_bits(gb, 24) != VP9_SYNCCODE)
            return;
    } else {
        int intraonly = invisible ? get_bits1(gb) : 0;
        if (!intraonly || get_bits(gb, 24) != VP9_SYNCCODE)
            return;
        if (*profile < 1) {
            *bit_depth = 8;
            return;
        }
    }

    *bit_depth = *profile <= 1 ? 8 : 10 + get_bits1(gb) * 2;
}

int ff_isom_get_vpcc_features(AVFormatContext *s, AVCodecParameters *par,
                              const uint8_t *data, int len,
                              AVRational *frame_rate, VPCC *vpcc)
{
    int profile = par->profile;
    int level = par->level == FF_LEVEL_UNKNOWN ?
        get_vp9_level(par, frame_rate) : par->level;
    int bit_depth = get_bit_depth(s, par->format);
    int vpx_chroma_subsampling =
        get_vpx_chroma_subsampling(s, par->format, par->chroma_location);
    int vpx_video_full_range_flag =
        get_vpx_video_full_range_flag(par->color_range);

    if (bit_depth < 0 || vpx_chroma_subsampling < 0)
        return AVERROR_INVALIDDATA;

    if (len && (profile == FF_PROFILE_UNKNOWN || !bit_depth)) {
        GetBitContext gb;

        int ret = init_get_bits8(&gb, data, len);
        if (ret < 0)
            return ret;

        parse_bitstream(&gb, &profile, &bit_depth);
    }

    if (profile == FF_PROFILE_UNKNOWN && bit_depth) {
        if (vpx_chroma_subsampling == VPX_SUBSAMPLING_420_VERTICAL ||
            vpx_chroma_subsampling == VPX_SUBSAMPLING_420_COLLOCATED_WITH_LUMA) {
            profile = (bit_depth == 8) ? FF_PROFILE_VP9_0 : FF_PROFILE_VP9_2;
        } else {
            profile = (bit_depth == 8) ? FF_PROFILE_VP9_1 : FF_PROFILE_VP9_3;
        }
    }

    if (profile == FF_PROFILE_UNKNOWN || !bit_depth)
        av_log(s, AV_LOG_WARNING, "VP9 profile and/or bit depth not set or could not be derived\n");

    vpcc->profile            = profile;
    vpcc->level              = level;
    vpcc->bitdepth           = bit_depth;
    vpcc->chroma_subsampling = vpx_chroma_subsampling;
    vpcc->full_range_flag    = vpx_video_full_range_flag;

    return 0;
}

int ff_isom_write_vpcc(AVFormatContext *s, AVIOContext *pb,
                       const uint8_t *data, int len,
                       AVCodecParameters *par)
{
    VPCC vpcc;
    int ret;

    ret = ff_isom_get_vpcc_features(s, par, data, len, NULL, &vpcc);
    if (ret < 0)
        return ret;

    avio_w8(pb, vpcc.profile);
    avio_w8(pb, vpcc.level);
    avio_w8(pb, (vpcc.bitdepth << 4) | (vpcc.chroma_subsampling << 1) | vpcc.full_range_flag);
    avio_w8(pb, par->color_primaries);
    avio_w8(pb, par->color_trc);
    avio_w8(pb, par->color_space);

    // vp9 does not have codec initialization data.
    avio_wb16(pb, 0);
    return 0;
}
