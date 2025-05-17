/*
 * frame CRC encoder (for codec/format testing)
 * Copyright (c) 2002 Fabrice Bellard
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

#include <inttypes.h>

#include "config.h"
#include "libavutil/adler32.h"
#include "libavutil/avstring.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/intreadwrite.h"

#include "libavcodec/codec_id.h"
#include "libavcodec/codec_par.h"
#include "libavcodec/packet.h"

#include "avformat.h"
#include "internal.h"
#include "mux.h"

static int framecrc_write_header(struct AVFormatContext *s)
{
    int i;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AVCodecParameters *par = st->codecpar;
        if (par->extradata) {
            uint32_t crc = av_adler32_update(0, par->extradata, par->extradata_size);
            avio_printf(s->pb, "#extradata %d: %8d, 0x%08"PRIx32"\n",
                        i, par->extradata_size, crc);
        }
    }

    return ff_framehash_write_header(s);
}

static av_unused void inline bswap(char *buf, int offset, int size)
{
    if (size == 8) {
        uint64_t val = AV_RN64(buf + offset);
        AV_WN64(buf + offset, av_bswap64(val));
    } else if (size == 4) {
        uint32_t val = AV_RN32(buf + offset);
        AV_WN32(buf + offset, av_bswap32(val));
    } else if (size == 2) {
        uint16_t val = AV_RN16(buf + offset);
        AV_WN16(buf + offset, av_bswap16(val));
    }
}

static int framecrc_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    uint32_t crc = av_adler32_update(0, pkt->data, pkt->size);
    char buf[256];

    snprintf(buf, sizeof(buf), "%d, %10"PRId64", %10"PRId64", %8"PRId64", %8d, 0x%08"PRIx32,
             pkt->stream_index, pkt->dts, pkt->pts, pkt->duration, pkt->size, crc);
    if (pkt->flags != AV_PKT_FLAG_KEY)
        av_strlcatf(buf, sizeof(buf), ", F=0x%0X", pkt->flags);
    if (pkt->side_data_elems) {
        av_strlcatf(buf, sizeof(buf), ", S=%d", pkt->side_data_elems);

        for (int i = 0; i < pkt->side_data_elems; i++) {
            const AVPacketSideData *const sd = &pkt->side_data[i];
            const uint8_t *data = sd->data;
            uint32_t side_data_crc = 0;

            switch (sd->type) {
#if HAVE_BIGENDIAN
                uint8_t bswap_buf[FFMAX3(sizeof(AVCPBProperties),
                                         sizeof(AVProducerReferenceTime),
                                         sizeof(AVDynamicHDRPlus))];
            case AV_PKT_DATA_PALETTE:
            case AV_PKT_DATA_REPLAYGAIN:
            case AV_PKT_DATA_DISPLAYMATRIX:
            case AV_PKT_DATA_STEREO3D:
            case AV_PKT_DATA_AUDIO_SERVICE_TYPE:
            case AV_PKT_DATA_FALLBACK_TRACK:
            case AV_PKT_DATA_MASTERING_DISPLAY_METADATA:
            case AV_PKT_DATA_SPHERICAL:
            case AV_PKT_DATA_CONTENT_LIGHT_LEVEL:
            case AV_PKT_DATA_S12M_TIMECODE:
                for (size_t j = 0; j < sd->size / 4; j++) {
                    uint8_t buf[4];
                    AV_WL32(buf, AV_RB32(sd->data + 4 * j));
                    side_data_crc = av_adler32_update(side_data_crc, buf, 4);
                }
                break;
            case AV_PKT_DATA_CPB_PROPERTIES:
#define BSWAP(struct, field) bswap(bswap_buf, offsetof(struct, field), sizeof(((struct){0}).field))
#define BSWAP_RAT(struct, field) do {BSWAP(struct, field.num); BSWAP(struct, field.den);} while(0)
                if (sd->size == sizeof(AVCPBProperties)) {
                    memcpy(bswap_buf, sd->data, sizeof(AVCPBProperties));
                    data = bswap_buf;
                    BSWAP(AVCPBProperties, max_bitrate);
                    BSWAP(AVCPBProperties, min_bitrate);
                    BSWAP(AVCPBProperties, avg_bitrate);
                    BSWAP(AVCPBProperties, buffer_size);
                    BSWAP(AVCPBProperties, vbv_delay);
                }
                goto pod;
            case AV_PKT_DATA_PRFT:
                if (sd->size == sizeof(AVProducerReferenceTime)) {
                    memcpy(bswap_buf, sd->data, sizeof(AVProducerReferenceTime));
                    data = bswap_buf;
                    BSWAP(AVProducerReferenceTime, wallclock);
                    BSWAP(AVProducerReferenceTime, flags);
                }
                goto pod;
            case AV_PKT_DATA_DYNAMIC_HDR10_PLUS:
                if (sd->size == sizeof(AVDynamicHDRPlus)) {
                    memcpy(bswap_buf, sd->data, sizeof(AVDynamicHDRPlus));
                    data = bswap_buf;
                    for (int i = 0; i < FF_ARRAY_ELEMS(((AVDynamicHDRPlus*)0)->params); i++) {
                        BSWAP_RAT(AVDynamicHDRPlus, params[i].window_upper_left_corner_x);
                        BSWAP_RAT(AVDynamicHDRPlus, params[i].window_upper_left_corner_y);
                        BSWAP_RAT(AVDynamicHDRPlus, params[i].window_lower_right_corner_x);
                        BSWAP_RAT(AVDynamicHDRPlus, params[i].window_lower_right_corner_y);
                        BSWAP(AVDynamicHDRPlus, params[i].center_of_ellipse_x);
                        BSWAP(AVDynamicHDRPlus, params[i].center_of_ellipse_y);
                        BSWAP(AVDynamicHDRPlus, params[i].semimajor_axis_internal_ellipse);
                        BSWAP(AVDynamicHDRPlus, params[i].semimajor_axis_external_ellipse);
                        BSWAP(AVDynamicHDRPlus, params[i].semiminor_axis_external_ellipse);
                        //!!! overlap_process_option
                        for(int j = 0; j < FF_ARRAY_ELEMS(((AVDynamicHDRPlus*)0)->params->maxscl); j++)
                            BSWAP_RAT(AVDynamicHDRPlus, params[i].maxscl[j]);
                        BSWAP_RAT(AVDynamicHDRPlus, params[i].average_maxrgb);
                        BSWAP_RAT(AVDynamicHDRPlus, params[i].fraction_bright_pixels);
                        BSWAP_RAT(AVDynamicHDRPlus, params[i].knee_point_x);
                        BSWAP_RAT(AVDynamicHDRPlus, params[i].knee_point_y);
                        for(int j = 0; j < FF_ARRAY_ELEMS(((AVDynamicHDRPlus*)0)->params->distribution_maxrgb); j++) {
                            BSWAP_RAT(AVDynamicHDRPlus, params[i].distribution_maxrgb[j].percentile);
                            BSWAP_RAT(AVDynamicHDRPlus, params[i].bezier_curve_anchors[j]);
                        }
                        BSWAP_RAT(AVDynamicHDRPlus, params[i].color_saturation_weight);
                    }
                    BSWAP_RAT(AVDynamicHDRPlus, targeted_system_display_maximum_luminance);
                    for(int i = 0; i<FF_ARRAY_ELEMS(((AVDynamicHDRPlus*)0)->targeted_system_display_actual_peak_luminance); i++)
                        for(int j = 0; j<FF_ARRAY_ELEMS(((AVDynamicHDRPlus*)0)->targeted_system_display_actual_peak_luminance[0]); j++) {
                            BSWAP_RAT(AVDynamicHDRPlus, targeted_system_display_actual_peak_luminance[i][j]);
                            BSWAP_RAT(AVDynamicHDRPlus, mastering_display_actual_peak_luminance[i][j]);
                        }
                }
            pod:
#endif

            default:
                side_data_crc = av_adler32_update(0, data, sd->size);
                break;
            case AV_PKT_DATA_IAMF_MIX_GAIN_PARAM:
            case AV_PKT_DATA_IAMF_DEMIXING_INFO_PARAM:
            case AV_PKT_DATA_IAMF_RECON_GAIN_INFO_PARAM:
                side_data_crc = 0;
            }

            av_strlcatf(buf, sizeof(buf), ", %s, %8"SIZE_SPECIFIER", 0x%08"PRIx32,
                        av_packet_side_data_name(sd->type), sd->size, side_data_crc);
        }
    }
    av_strlcatf(buf, sizeof(buf), "\n");
    avio_write(s->pb, buf, strlen(buf));
    return 0;
}

const FFOutputFormat ff_framecrc_muxer = {
    .p.name            = "framecrc",
    .p.long_name       = NULL_IF_CONFIG_SMALL("framecrc testing"),
    .p.audio_codec     = AV_CODEC_ID_PCM_S16LE,
    .p.video_codec     = AV_CODEC_ID_RAWVIDEO,
    .write_header      = framecrc_write_header,
    .write_packet      = framecrc_write_packet,
    .p.flags           = AVFMT_VARIABLE_FPS | AVFMT_TS_NONSTRICT |
                         AVFMT_TS_NEGATIVE | AVFMT_NODIMENSIONS,
};
