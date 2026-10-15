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

#ifndef AVCODEC_PACKET_INTERNAL_H
#define AVCODEC_PACKET_INTERNAL_H

#include <stdint.h>

#include "packet.h"

#define AVPACKET_IS_EMPTY(pkt) (!(pkt)->data && !(pkt)->side_data_elems)

int ff_side_data_set_prft(AVPacket *pkt, int64_t timestamp);

/**
 * The handle of each side data type: a short, stable name made of lower case
 * letters, digits and underscores, for use where a name has to be typed, such
 * as in the options of a filter. HANDLE(type, handle) is called with the type
 * without its AV_PKT_DATA_ prefix.
 *
 * Every type below AV_PKT_DATA_NB has one, which the packet_side_data_handle
 * test checks. A handle that has been released must not change.
 */
#define FF_PACKET_SIDE_DATA_HANDLES(HANDLE)                                  \
    HANDLE(PALETTE,                     "palette")                           \
    HANDLE(NEW_EXTRADATA,               "new_extradata")                     \
    HANDLE(PARAM_CHANGE,                "param_change")                      \
    HANDLE(H263_MB_INFO,                "h263_mb_info")                      \
    HANDLE(REPLAYGAIN,                  "replaygain")                        \
    HANDLE(DISPLAYMATRIX,               "displaymatrix")                     \
    HANDLE(STEREO3D,                    "stereo3d")                          \
    HANDLE(AUDIO_SERVICE_TYPE,          "audio_service_type")                \
    HANDLE(QUALITY_STATS,               "quality_stats")                     \
    HANDLE(FALLBACK_TRACK,              "fallback_track")                    \
    HANDLE(CPB_PROPERTIES,              "cpb_properties")                    \
    HANDLE(SKIP_SAMPLES,                "skip_samples")                      \
    HANDLE(JP_DUALMONO,                 "jp_dualmono")                       \
    HANDLE(STRINGS_METADATA,            "strings_metadata")                  \
    HANDLE(SUBTITLE_POSITION,           "subtitle_position")                 \
    HANDLE(MATROSKA_BLOCKADDITIONAL,    "matroska_blockadditional")          \
    HANDLE(WEBVTT_IDENTIFIER,           "webvtt_identifier")                 \
    HANDLE(WEBVTT_SETTINGS,             "webvtt_settings")                   \
    HANDLE(METADATA_UPDATE,             "metadata_update")                   \
    HANDLE(MPEGTS_STREAM_ID,            "mpegts_stream_id")                  \
    HANDLE(MASTERING_DISPLAY_METADATA,  "mastering_display_metadata")        \
    HANDLE(CONTENT_LIGHT_LEVEL,         "content_light_level")               \
    HANDLE(SPHERICAL,                   "spherical")                         \
    HANDLE(A53_CC,                      "a53_cc")                            \
    HANDLE(ENCRYPTION_INIT_INFO,        "encryption_init_info")              \
    HANDLE(ENCRYPTION_INFO,             "encryption_info")                   \
    HANDLE(AFD,                         "afd")                               \
    HANDLE(PRFT,                        "prft")                              \
    HANDLE(ICC_PROFILE,                 "icc_profile")                       \
    HANDLE(DOVI_CONF,                   "dovi_conf")                         \
    HANDLE(S12M_TIMECODE,               "s12m_timecode")                     \
    HANDLE(DYNAMIC_HDR10_PLUS,          "hdr10_plus")                        \
    HANDLE(DYNAMIC_HDR_SMPTE_2094_APP5, "hdr_smpte_2094_app5")               \
    HANDLE(AMBIENT_VIEWING_ENVIRONMENT, "ambient_viewing_environment")       \
    HANDLE(IAMF_MIX_GAIN_PARAM,         "iamf_mix_gain_param")               \
    HANDLE(IAMF_DEMIXING_INFO_PARAM,    "iamf_demixing_info_param")          \
    HANDLE(IAMF_RECON_GAIN_INFO_PARAM,  "iamf_recon_gain_info_param")        \
    HANDLE(FRAME_CROPPING,              "frame_cropping")                    \
    HANDLE(LCEVC,                       "lcevc")                             \
    HANDLE(3D_REFERENCE_DISPLAYS,       "3d_reference_displays")             \
    HANDLE(RTCP_SR,                     "rtcp_sr")                           \
    HANDLE(EXIF,                        "exif")                              \
    HANDLE(HEVC_CONF,                   "hevc_conf")                         \
    HANDLE(GAIN_MAP_PARAMS,             "gain_map_params")

#endif // AVCODEC_PACKET_INTERNAL_H
