/*
 * RAW muxers
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2005 Alex Beregszaszi
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

#include "config_components.h"

#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "rawenc.h"
#include "mux.h"

int ff_raw_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

static int force_one_stream(AVFormatContext *s)
{
    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "%s files have exactly one stream\n",
               s->oformat->name);
        return AVERROR(EINVAL);
    }
    if (   s->oformat->audio_codec != AV_CODEC_ID_NONE
        && s->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(s, AV_LOG_ERROR, "%s files have exactly one audio stream\n",
               s->oformat->name);
        return AVERROR(EINVAL);
    }
    if (   s->oformat->video_codec != AV_CODEC_ID_NONE
        && s->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        av_log(s, AV_LOG_ERROR, "%s files have exactly one video stream\n",
               s->oformat->name);
        return AVERROR(EINVAL);
    }
    return 0;
}

/* Note: Do not forget to add new entries to the Makefile as well. */

#if CONFIG_AC3_MUXER
const FFOutputFormat ff_ac3_muxer = {
    .p.name            = "ac3",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw AC-3"),
    .p.mime_type       = "audio/x-ac3",
    .p.extensions      = "ac3",
    .p.audio_codec     = AV_CODEC_ID_AC3,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_ADX_MUXER

static int adx_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVCodecParameters *par = s->streams[0]->codecpar;

    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
        int64_t file_size = avio_tell(pb);
        uint64_t sample_count = (file_size - 36) / par->ch_layout.nb_channels / 18 * 32;
        if (sample_count <= UINT32_MAX) {
            avio_seek(pb, 12, SEEK_SET);
            avio_wb32(pb, sample_count);
            avio_seek(pb, file_size, SEEK_SET);
        }
    }

    return 0;
}

const FFOutputFormat ff_adx_muxer = {
    .p.name            = "adx",
    .p.long_name       = NULL_IF_CONFIG_SMALL("CRI ADX"),
    .p.extensions      = "adx",
    .p.audio_codec     = AV_CODEC_ID_ADPCM_ADX,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .write_trailer     = adx_write_trailer,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_APTX_MUXER
const FFOutputFormat ff_aptx_muxer = {
    .p.name            = "aptx",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw aptX (Audio Processing Technology for Bluetooth)"),
    .p.extensions      = "aptx",
    .p.audio_codec     = AV_CODEC_ID_APTX,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_APTX_HD_MUXER
const FFOutputFormat ff_aptx_hd_muxer = {
    .p.name            = "aptx_hd",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw aptX HD (Audio Processing Technology for Bluetooth)"),
    .p.extensions      = "aptxhd",
    .p.audio_codec     = AV_CODEC_ID_APTX_HD,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_AVS2_MUXER
const FFOutputFormat ff_avs2_muxer = {
    .p.name            = "avs2",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw AVS2-P2/IEEE1857.4 video"),
    .p.extensions      = "avs,avs2",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_AVS2,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_AVS3_MUXER
const FFOutputFormat ff_avs3_muxer = {
    .p.name            = "avs3",
    .p.long_name       = NULL_IF_CONFIG_SMALL("AVS3-P2/IEEE1857.10"),
    .p.extensions      = "avs3",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_AVS3,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif


#if CONFIG_CAVSVIDEO_MUXER
const FFOutputFormat ff_cavsvideo_muxer = {
    .p.name            = "cavsvideo",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw Chinese AVS (Audio Video Standard) video"),
    .p.extensions      = "cavs",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_CAVS,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_CODEC2RAW_MUXER
const FFOutputFormat ff_codec2raw_muxer = {
    .p.name            = "codec2raw",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw codec2 muxer"),
    .p.audio_codec     = AV_CODEC_ID_CODEC2,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif


#if CONFIG_DATA_MUXER
const FFOutputFormat ff_data_muxer = {
    .p.name            = "data",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw data"),
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DFPWM_MUXER
const FFOutputFormat ff_dfpwm_muxer = {
    .p.name            = "dfpwm",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw DFPWM1a"),
    .p.extensions      = "dfpwm",
    .p.audio_codec     = AV_CODEC_ID_DFPWM,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DIRAC_MUXER
const FFOutputFormat ff_dirac_muxer = {
    .p.name            = "dirac",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw Dirac"),
    .p.extensions      = "drc,vc2",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_DIRAC,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DNXHD_MUXER
const FFOutputFormat ff_dnxhd_muxer = {
    .p.name            = "dnxhd",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw DNxHD (SMPTE VC-3)"),
    .p.extensions      = "dnxhd,dnxhr",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_DNXHD,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DTS_MUXER
const FFOutputFormat ff_dts_muxer = {
    .p.name            = "dts",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw DTS"),
    .p.mime_type       = "audio/x-dca",
    .p.extensions      = "dts",
    .p.audio_codec     = AV_CODEC_ID_DTS,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_EAC3_MUXER
const FFOutputFormat ff_eac3_muxer = {
    .p.name            = "eac3",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw E-AC-3"),
    .p.mime_type       = "audio/x-eac3",
    .p.extensions      = "eac3,ec3",
    .p.audio_codec     = AV_CODEC_ID_EAC3,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_G722_MUXER
const FFOutputFormat ff_g722_muxer = {
    .p.name            = "g722",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw G.722"),
    .p.mime_type       = "audio/G722",
    .p.extensions      = "g722",
    .p.audio_codec     = AV_CODEC_ID_ADPCM_G722,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_G723_1_MUXER
const FFOutputFormat ff_g723_1_muxer = {
    .p.name            = "g723_1",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw G.723.1"),
    .p.mime_type       = "audio/g723",
    .p.extensions      = "tco,rco",
    .p.audio_codec     = AV_CODEC_ID_G723_1,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_G726_MUXER
const FFOutputFormat ff_g726_muxer = {
    .p.name            = "g726",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw big-endian G.726 (\"left-justified\")"),
    .p.audio_codec     = AV_CODEC_ID_ADPCM_G726,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_G726LE_MUXER
const FFOutputFormat ff_g726le_muxer = {
    .p.name            = "g726le",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw little-endian G.726 (\"right-justified\")"),
    .p.audio_codec     = AV_CODEC_ID_ADPCM_G726LE,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_GSM_MUXER
const FFOutputFormat ff_gsm_muxer = {
    .p.name            = "gsm",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw GSM"),
    .p.mime_type       = "audio/x-gsm",
    .p.extensions      = "gsm",
    .p.audio_codec     = AV_CODEC_ID_GSM,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H261_MUXER
const FFOutputFormat ff_h261_muxer = {
    .p.name            = "h261",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw H.261"),
    .p.mime_type       = "video/x-h261",
    .p.extensions      = "h261",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_H261,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H263_MUXER
const FFOutputFormat ff_h263_muxer = {
    .p.name            = "h263",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw H.263"),
    .p.mime_type       = "video/x-h263",
    .p.extensions      = "h263",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_H263,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H264_MUXER
static int h264_check_bitstream(AVFormatContext *s, AVStream *st,
                                const AVPacket *pkt)
{
    if (pkt->size >= 5 && AV_RB32(pkt->data) != 0x0000001 &&
                          AV_RB24(pkt->data) != 0x000001)
        return ff_stream_add_bitstream_filter(st, "h264_mp4toannexb", NULL);
    return 1;
}

const FFOutputFormat ff_h264_muxer = {
    .p.name            = "h264",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw H.264 video"),
    .p.extensions      = "h264,264",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_H264,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .check_bitstream   = h264_check_bitstream,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_HEVC_MUXER
static int hevc_check_bitstream(AVFormatContext *s, AVStream *st,
                                const AVPacket *pkt)
{
    if (pkt->size >= 5 && AV_RB32(pkt->data) != 0x0000001 &&
                          AV_RB24(pkt->data) != 0x000001)
        return ff_stream_add_bitstream_filter(st, "hevc_mp4toannexb", NULL);
    return 1;
}

const FFOutputFormat ff_hevc_muxer = {
    .p.name            = "hevc",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw HEVC video"),
    .p.extensions      = "hevc,h265,265",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_HEVC,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .check_bitstream   = hevc_check_bitstream,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_M4V_MUXER
const FFOutputFormat ff_m4v_muxer = {
    .p.name            = "m4v",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw MPEG-4 video"),
    .p.extensions      = "m4v",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_MPEG4,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MJPEG_MUXER
const FFOutputFormat ff_mjpeg_muxer = {
    .p.name            = "mjpeg",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw MJPEG video"),
    .p.mime_type       = "video/x-mjpeg",
    .p.extensions      = "mjpg,mjpeg",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_MJPEG,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MLP_MUXER
const FFOutputFormat ff_mlp_muxer = {
    .p.name            = "mlp",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw MLP"),
    .p.extensions      = "mlp",
    .p.audio_codec     = AV_CODEC_ID_MLP,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MP2_MUXER
const FFOutputFormat ff_mp2_muxer = {
    .p.name            = "mp2",
    .p.long_name       = NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
    .p.mime_type       = "audio/mpeg",
    .p.extensions      = "mp2,m2a,mpa",
    .p.audio_codec     = AV_CODEC_ID_MP2,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MPEG1VIDEO_MUXER
const FFOutputFormat ff_mpeg1video_muxer = {
    .p.name            = "mpeg1video",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw MPEG-1 video"),
    .p.mime_type       = "video/mpeg",
    .p.extensions      = "mpg,mpeg,m1v",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_MPEG1VIDEO,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MPEG2VIDEO_MUXER
const FFOutputFormat ff_mpeg2video_muxer = {
    .p.name            = "mpeg2video",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw MPEG-2 video"),
    .p.extensions      = "m2v",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_MPEG2VIDEO,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_OBU_MUXER
static int obu_check_bitstream(AVFormatContext *s, AVStream *st,
                               const AVPacket *pkt)
{
    return ff_stream_add_bitstream_filter(st, "av1_metadata", "td=insert");
}

const FFOutputFormat ff_obu_muxer = {
    .p.name            = "obu",
    .p.long_name       = NULL_IF_CONFIG_SMALL("AV1 low overhead OBU"),
    .p.extensions      = "obu",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_AV1,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .check_bitstream   = obu_check_bitstream,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_RAWVIDEO_MUXER
const FFOutputFormat ff_rawvideo_muxer = {
    .p.name            = "rawvideo",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw video"),
    .p.extensions      = "yuv,rgb",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_RAWVIDEO,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_SBC_MUXER
const FFOutputFormat ff_sbc_muxer = {
    .p.name            = "sbc",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw SBC"),
    .p.mime_type       = "audio/x-sbc",
    .p.extensions      = "sbc,msbc",
    .p.audio_codec     = AV_CODEC_ID_SBC,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_TRUEHD_MUXER
const FFOutputFormat ff_truehd_muxer = {
    .p.name            = "truehd",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw TrueHD"),
    .p.extensions      = "thd",
    .p.audio_codec     = AV_CODEC_ID_TRUEHD,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_VC1_MUXER
const FFOutputFormat ff_vc1_muxer = {
    .p.name            = "vc1",
    .p.long_name       = NULL_IF_CONFIG_SMALL("raw VC-1 video"),
    .p.extensions      = "vc1",
    .p.audio_codec     = AV_CODEC_ID_NONE,
    .p.video_codec     = AV_CODEC_ID_VC1,
    .init              = force_one_stream,
    .write_packet      = ff_raw_write_packet,
    .p.flags           = AVFMT_NOTIMESTAMPS,
};
#endif
