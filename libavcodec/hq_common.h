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

#ifndef AVCODEC_HQ_COMMON_H
#define AVCODEC_HQ_COMMON_H

#include "vlc.h"
#include "libavutil/attributes_internal.h"

#define HQ_CBP_VLC_BITS 5

EXTERN const VLCElem ff_hq_cbp_vlc[1 << HQ_CBP_VLC_BITS];

#endif /* AVCODEC_HQ_COMMON_H */
