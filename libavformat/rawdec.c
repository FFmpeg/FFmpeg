/*
 * RAW demuxers
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

#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "rawdec.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"

#define RAW_PACKET_SIZE 1024

int ff_raw_read_partial_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;

    size = RAW_PACKET_SIZE;

    if (av_new_packet(pkt, size) < 0)
        return AVERROR(ENOMEM);

    pkt->pos= avio_tell(s->pb);
    pkt->stream_index = 0;
    ret = ffio_read_partial(s->pb, pkt->data, size);
    if (ret < 0) {
        av_free_packet(pkt);
        return ret;
    }
    av_shrink_packet(pkt, ret);
    return ret;
}

int ff_raw_audio_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = s->iformat->raw_codec_id;
    st->need_parsing = AVSTREAM_PARSE_FULL_RAW;
    st->start_time = 0;
    /* the parameters will be extracted from the compressed bitstream */

    return 0;
}

/* MPEG-1/H.263 input */
int ff_raw_video_read_header(AVFormatContext *s)
{
    AVStream *st;
    FFRawVideoDemuxerContext *s1 = s->priv_data;
    int ret = 0;


    st = avformat_new_stream(s, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = s->iformat->raw_codec_id;
    st->need_parsing = AVSTREAM_PARSE_FULL_RAW;

    st->codec->framerate = s1->framerate;
    st->codec->time_base = av_inv_q(s1->framerate);
    avpriv_set_pts_info(st, 64, 1, 1200000);

fail:
    return ret;
}

int ff_raw_data_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_DATA;
    st->codec->codec_id = s->iformat->raw_codec_id;
    st->start_time = 0;
    return 0;
}

/* Note: Do not forget to add new entries to the Makefile as well. */

#define OFFSET(x) offsetof(FFRawVideoDemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
const AVOption ff_rawvideo_options[] = {
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, 0, DEC},
    { NULL },
};

#if CONFIG_DATA_DEMUXER
AVInputFormat ff_data_demuxer = {
    .name           = "data",
    .long_name      = NULL_IF_CONFIG_SMALL("raw data"),
    .read_header    = ff_raw_data_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .raw_codec_id   = AV_CODEC_ID_NONE,
    .flags          = AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_LATM_DEMUXER

AVInputFormat ff_latm_demuxer = {
    .name           = "latm",
    .long_name      = NULL_IF_CONFIG_SMALL("raw LOAS/LATM"),
    .read_header    = ff_raw_audio_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_NOTIMESTAMPS,
    .extensions     = "latm",
    .raw_codec_id   = AV_CODEC_ID_AAC_LATM,
};
#endif

#if CONFIG_MJPEG_DEMUXER
static int mjpeg_probe(AVProbeData *p)
{
    int i;
    int state = -1;
    int nb_invalid = 0;
    int nb_frames = 0;

    for (i=0; i<p->buf_size-2; i++) {
        int c;
        if (p->buf[i] != 0xFF)
            continue;
        c = p->buf[i+1];
        switch (c) {
        case 0xD8:
            state = 0xD8;
            break;
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC5:
        case 0xC6:
        case 0xC7:
        case 0xF7:
            if (state == 0xD8) {
                state = 0xC0;
            } else
                nb_invalid++;
            break;
        case 0xDA:
            if (state == 0xC0) {
                state = 0xDA;
            } else
                nb_invalid++;
            break;
        case 0xD9:
            if (state == 0xDA) {
                state = 0xD9;
                nb_frames++;
            } else
                nb_invalid++;
            break;
        default:
            if (  (c >= 0x02 && c <= 0xBF)
                || c == 0xC8) {
                nb_invalid++;
            }
        }
    }

    if (nb_invalid*4 + 1 < nb_frames) {
        static const char ct_jpeg[] = "\r\nContent-Type: image/jpeg\r\n";
        int i;

        for (i=0; i<FFMIN(p->buf_size - (int)sizeof(ct_jpeg), 100); i++)
            if (!memcmp(p->buf + i, ct_jpeg, sizeof(ct_jpeg) - 1))
                return AVPROBE_SCORE_EXTENSION;

        if (nb_invalid == 0 && nb_frames > 2)
            return AVPROBE_SCORE_EXTENSION / 2;
        return AVPROBE_SCORE_EXTENSION / 4;
    }

    return 0;
}

FF_DEF_RAWVIDEO_DEMUXER2(mjpeg, "raw MJPEG video", mjpeg_probe, "mjpg,mjpeg,mpo", AV_CODEC_ID_MJPEG, AVFMT_GENERIC_INDEX|AVFMT_NOTIMESTAMPS)
#endif

#if CONFIG_MLP_DEMUXER
AVInputFormat ff_mlp_demuxer = {
    .name           = "mlp",
    .long_name      = NULL_IF_CONFIG_SMALL("raw MLP"),
    .read_header    = ff_raw_audio_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_NOTIMESTAMPS,
    .extensions     = "mlp",
    .raw_codec_id   = AV_CODEC_ID_MLP,
};
#endif

#if CONFIG_TRUEHD_DEMUXER
AVInputFormat ff_truehd_demuxer = {
    .name           = "truehd",
    .long_name      = NULL_IF_CONFIG_SMALL("raw TrueHD"),
    .read_header    = ff_raw_audio_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_NOTIMESTAMPS,
    .extensions     = "thd",
    .raw_codec_id   = AV_CODEC_ID_TRUEHD,
};
#endif

#if CONFIG_SHORTEN_DEMUXER
AVInputFormat ff_shorten_demuxer = {
    .name           = "shn",
    .long_name      = NULL_IF_CONFIG_SMALL("raw Shorten"),
    .read_header    = ff_raw_audio_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK|AVFMT_NOTIMESTAMPS,
    .extensions     = "shn",
    .raw_codec_id   = AV_CODEC_ID_SHORTEN,
};
#endif

#if CONFIG_VC1_DEMUXER
FF_DEF_RAWVIDEO_DEMUXER2(vc1, "raw VC-1", NULL, "vc1", AV_CODEC_ID_VC1, AVFMT_GENERIC_INDEX|AVFMT_NOTIMESTAMPS)
#endif
