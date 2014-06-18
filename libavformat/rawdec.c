/*
 * RAW demuxers
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2005 Alex Beregszaszi
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "rawdec.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
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
        av_packet_unref(pkt);
        return ret;
    } else if (ret < size) {
        /* initialize end of packet for partial reads to avoid reading
         * uninitialized data on allowed overreads */
        memset(pkt->data + ret, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    }
    pkt->size = ret;
    return ret;
}

int ff_raw_audio_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = s->iformat->raw_codec_id;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    st->start_time = 0;
    /* the parameters will be extracted from the compressed bitstream */

    return 0;
}

/* MPEG-1/H.263 input */
int ff_raw_video_read_header(AVFormatContext *s)
{
    AVStream *st;
    FFRawVideoDemuxerContext *s1 = s->priv_data;
    AVRational framerate;
    int ret = 0;


    st = avformat_new_stream(s, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = s->iformat->raw_codec_id;
    st->need_parsing = AVSTREAM_PARSE_FULL;

    if ((ret = av_parse_video_rate(&framerate, s1->framerate)) < 0) {
        av_log(s, AV_LOG_ERROR, "Could not parse framerate: %s.\n", s1->framerate);
        goto fail;
    }

    st->avg_frame_rate = framerate;
    avpriv_set_pts_info(st, 64, framerate.den, framerate.num);

fail:
    return ret;
}

/* Note: Do not forget to add new entries to the Makefile as well. */

#define OFFSET(x) offsetof(FFRawVideoDemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
const AVOption ff_rawvideo_options[] = {
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, DEC},
    { NULL },
};

#if CONFIG_LATM_DEMUXER

#define LOAS_SYNC_WORD 0x2b7

static int latm_read_probe(AVProbeData *p)
{
    int max_frames = 0, first_frames = 0;
    int fsize, frames;
    uint8_t *buf0 = p->buf;
    uint8_t *buf2;
    uint8_t *buf;
    uint8_t *end = buf0 + p->buf_size - 3;

    buf = buf0;

    for (; buf < end; buf = buf2 + 1) {
        buf2 = buf;

        for (frames = 0; buf2 < end; frames++) {
            uint32_t header = AV_RB24(buf2);
            if ((header >> 13) != LOAS_SYNC_WORD) {
                if (buf != buf0) {
                    // Found something that isn't a LOAS header, starting
                    // from a position other than the start of the buffer.
                    // Discard the count we've accumulated so far since it
                    // probably was a false positive.
                    frames = 0;
                }
                break;
            }
            fsize = (header & 0x1FFF) + 3;
            if (fsize < 7)
                break;
            buf2 += fsize;
        }
        max_frames = FFMAX(max_frames, frames);
        if (buf == buf0)
            first_frames = frames;
    }

    if (first_frames >= 3)
        return AVPROBE_SCORE_EXTENSION + 1;
    else if (max_frames > 100)
        return AVPROBE_SCORE_EXTENSION;
    else if (max_frames >= 3)
        return AVPROBE_SCORE_EXTENSION / 2;
    else if (max_frames > 1)
        return 1;
    else
        return 0;
}

AVInputFormat ff_latm_demuxer = {
    .name           = "latm",
    .long_name      = NULL_IF_CONFIG_SMALL("raw LOAS/LATM"),
    .read_probe     = latm_read_probe,
    .read_header    = ff_raw_audio_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "latm",
    .raw_codec_id   = AV_CODEC_ID_AAC_LATM,
};
#endif

#if CONFIG_MJPEG_DEMUXER
FF_DEF_RAWVIDEO_DEMUXER(mjpeg, "raw MJPEG video", NULL, "mjpg,mjpeg", AV_CODEC_ID_MJPEG)
#endif

#if CONFIG_MLP_DEMUXER
AVInputFormat ff_mlp_demuxer = {
    .name           = "mlp",
    .long_name      = NULL_IF_CONFIG_SMALL("raw MLP"),
    .read_header    = ff_raw_audio_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .flags          = AVFMT_GENERIC_INDEX,
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
    .flags          = AVFMT_GENERIC_INDEX,
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
    .flags          = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK,
    .extensions     = "shn",
    .raw_codec_id   = AV_CODEC_ID_SHORTEN,
};
#endif

#if CONFIG_VC1_DEMUXER
FF_DEF_RAWVIDEO_DEMUXER(vc1, "raw VC-1", NULL, "vc1", AV_CODEC_ID_VC1)
#endif
