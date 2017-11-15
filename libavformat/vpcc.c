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
#include "vpcc.h"

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

int ff_isom_write_vpcc(AVFormatContext *s, AVIOContext *pb,
                       AVCodecParameters *par)
{
    int profile = par->profile;
    int level = par->level == FF_LEVEL_UNKNOWN ? 0 : par->level;
    int bit_depth = get_bit_depth(s, par->format);
    int vpx_chroma_subsampling =
        get_vpx_chroma_subsampling(s, par->format, par->chroma_location);
    int vpx_video_full_range_flag =
        get_vpx_video_full_range_flag(par->color_range);

    if (bit_depth < 0 || vpx_chroma_subsampling < 0)
        return AVERROR_INVALIDDATA;

    if (profile == FF_PROFILE_UNKNOWN) {
        if (vpx_chroma_subsampling == VPX_SUBSAMPLING_420_VERTICAL ||
            vpx_chroma_subsampling == VPX_SUBSAMPLING_420_COLLOCATED_WITH_LUMA) {
            profile = (bit_depth == 8) ? FF_PROFILE_VP9_0 : FF_PROFILE_VP9_2;
        } else {
            profile = (bit_depth == 8) ? FF_PROFILE_VP9_1 : FF_PROFILE_VP9_3;
        }
    }

    avio_w8(pb, profile);
    avio_w8(pb, level);
    avio_w8(pb, (bit_depth << 4) | (vpx_chroma_subsampling << 1) | vpx_video_full_range_flag);
    avio_w8(pb, par->color_primaries);
    avio_w8(pb, par->color_trc);
    avio_w8(pb, par->color_space);

    // vp9 does not have codec initialization data.
    avio_wb16(pb, 0);
    return 0;
}
