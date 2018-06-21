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

#ifndef AVCODEC_PROFILES_H
#define AVCODEC_PROFILES_H

#include "avcodec.h"

extern const AVProfile ff_aac_profiles[];
extern const AVProfile ff_dca_profiles[];
extern const AVProfile ff_dnxhd_profiles[];
extern const AVProfile ff_h264_profiles[];
extern const AVProfile ff_hevc_profiles[];
extern const AVProfile ff_jpeg2000_profiles[];
extern const AVProfile ff_mpeg2_video_profiles[];
extern const AVProfile ff_mpeg4_video_profiles[];
extern const AVProfile ff_vc1_profiles[];
extern const AVProfile ff_vp9_profiles[];
extern const AVProfile ff_av1_profiles[];
extern const AVProfile ff_sbc_profiles[];

#endif /* AVCODEC_PROFILES_H */
