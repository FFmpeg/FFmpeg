/*
 * Wing Commander III Movie (.mve) File Demuxer
 * Copyright (c) 2003 The ffmpeg Project
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

/**
 * @file
 * Wing Commander III Movie file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the WC3 .mve file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "avformat.h"
#include "internal.h"

#define FORM_TAG MKTAG('F', 'O', 'R', 'M')
#define MOVE_TAG MKTAG('M', 'O', 'V', 'E')
#define  PC__TAG MKTAG('_', 'P', 'C', '_')
#define SOND_TAG MKTAG('S', 'O', 'N', 'D')
#define BNAM_TAG MKTAG('B', 'N', 'A', 'M')
#define SIZE_TAG MKTAG('S', 'I', 'Z', 'E')
#define PALT_TAG MKTAG('P', 'A', 'L', 'T')
#define INDX_TAG MKTAG('I', 'N', 'D', 'X')
#define BRCH_TAG MKTAG('B', 'R', 'C', 'H')
#define SHOT_TAG MKTAG('S', 'H', 'O', 'T')
#define VGA__TAG MKTAG('V', 'G', 'A', ' ')
#define TEXT_TAG MKTAG('T', 'E', 'X', 'T')
#define AUDI_TAG MKTAG('A', 'U', 'D', 'I')

/* video resolution unless otherwise specified */
#define WC3_DEFAULT_WIDTH 320
#define WC3_DEFAULT_HEIGHT 165

/* always use the same PCM audio parameters */
#define WC3_SAMPLE_RATE 22050
#define WC3_AUDIO_CHANNELS 1
#define WC3_AUDIO_BITS 16

/* nice, constant framerate */
#define WC3_FRAME_FPS 15

#define PALETTE_SIZE (256 * 3)

typedef struct Wc3DemuxContext {
    int width;
    int height;
    int64_t pts;
    int video_stream_index;
    int audio_stream_index;

    AVPacket vpkt;

} Wc3DemuxContext;

static int wc3_probe(AVProbeData *p)
{
    if (p->buf_size < 12)
        return 0;

    if ((AV_RL32(&p->buf[0]) != FORM_TAG) ||
        (AV_RL32(&p->buf[8]) != MOVE_TAG))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int wc3_read_header(AVFormatContext *s)
{
    Wc3DemuxContext *wc3 = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned int fourcc_tag;
    unsigned int size;
    AVStream *st;
    int ret = 0;
    char *buffer;

    /* default context members */
    wc3->width = WC3_DEFAULT_WIDTH;
    wc3->height = WC3_DEFAULT_HEIGHT;
    wc3->pts = 0;
    wc3->video_stream_index = wc3->audio_stream_index = 0;
    av_init_packet(&wc3->vpkt);
    wc3->vpkt.data = NULL; wc3->vpkt.size = 0;

    /* skip the first 3 32-bit numbers */
    avio_skip(pb, 12);

    /* traverse through the chunks and load the header information before
     * the first BRCH tag */
    fourcc_tag = avio_rl32(pb);
    size = (avio_rb32(pb) + 1) & (~1);

    do {
        switch (fourcc_tag) {

        case SOND_TAG:
        case INDX_TAG:
            /* SOND unknown, INDX unnecessary; ignore both */
            avio_skip(pb, size);
            break;

        case PC__TAG:
            /* number of palettes, unneeded */
            avio_skip(pb, 12);
            break;

        case BNAM_TAG:
            /* load up the name */
            buffer = av_malloc(size+1);
            if (!buffer)
                return AVERROR(ENOMEM);
            if ((ret = avio_read(pb, buffer, size)) != size)
                return AVERROR(EIO);
            buffer[size] = 0;
            av_dict_set(&s->metadata, "title", buffer,
                                   AV_DICT_DONT_STRDUP_VAL);
            break;

        case SIZE_TAG:
            /* video resolution override */
            wc3->width  = avio_rl32(pb);
            wc3->height = avio_rl32(pb);
            break;

        case PALT_TAG:
            /* one of several palettes */
            avio_seek(pb, -8, SEEK_CUR);
            av_append_packet(pb, &wc3->vpkt, 8 + PALETTE_SIZE);
            break;

        default:
            av_log(s, AV_LOG_ERROR, "  unrecognized WC3 chunk: %c%c%c%c (0x%02X%02X%02X%02X)\n",
                (uint8_t)fourcc_tag, (uint8_t)(fourcc_tag >> 8), (uint8_t)(fourcc_tag >> 16), (uint8_t)(fourcc_tag >> 24),
                (uint8_t)fourcc_tag, (uint8_t)(fourcc_tag >> 8), (uint8_t)(fourcc_tag >> 16), (uint8_t)(fourcc_tag >> 24));
            return AVERROR_INVALIDDATA;
        }

        fourcc_tag = avio_rl32(pb);
        /* chunk sizes are 16-bit aligned */
        size = (avio_rb32(pb) + 1) & (~1);
        if (url_feof(pb))
            return AVERROR(EIO);

    } while (fourcc_tag != BRCH_TAG);

    /* initialize the decoder streams */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 33, 1, WC3_FRAME_FPS);
    wc3->video_stream_index = st->index;
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = AV_CODEC_ID_XAN_WC3;
    st->codec->codec_tag = 0;  /* no fourcc */
    st->codec->width = wc3->width;
    st->codec->height = wc3->height;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 33, 1, WC3_FRAME_FPS);
    wc3->audio_stream_index = st->index;
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = AV_CODEC_ID_PCM_S16LE;
    st->codec->codec_tag = 1;
    st->codec->channels = WC3_AUDIO_CHANNELS;
    st->codec->channel_layout = AV_CH_LAYOUT_MONO;
    st->codec->bits_per_coded_sample = WC3_AUDIO_BITS;
    st->codec->sample_rate = WC3_SAMPLE_RATE;
    st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
        st->codec->bits_per_coded_sample;
    st->codec->block_align = WC3_AUDIO_BITS * WC3_AUDIO_CHANNELS;

    return 0;
}

static int wc3_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    Wc3DemuxContext *wc3 = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned int fourcc_tag;
    unsigned int size;
    int packet_read = 0;
    int ret = 0;
    unsigned char text[1024];

    while (!packet_read) {

        fourcc_tag = avio_rl32(pb);
        /* chunk sizes are 16-bit aligned */
        size = (avio_rb32(pb) + 1) & (~1);
        if (url_feof(pb))
            return AVERROR(EIO);

        switch (fourcc_tag) {

        case BRCH_TAG:
            /* no-op */
            break;

        case SHOT_TAG:
            /* load up new palette */
            avio_seek(pb, -8, SEEK_CUR);
            av_append_packet(pb, &wc3->vpkt, 8 + 4);
            break;

        case VGA__TAG:
            /* send out video chunk */
            avio_seek(pb, -8, SEEK_CUR);
            ret= av_append_packet(pb, &wc3->vpkt, 8 + size);
            // ignore error if we have some data
            if (wc3->vpkt.size > 0)
                ret = 0;
            *pkt = wc3->vpkt;
            wc3->vpkt.data = NULL; wc3->vpkt.size = 0;
            pkt->stream_index = wc3->video_stream_index;
            pkt->pts = wc3->pts;
            packet_read = 1;
            break;

        case TEXT_TAG:
            /* subtitle chunk */
#if 0
            avio_skip(pb, size);
#else
            if ((unsigned)size > sizeof(text) || (ret = avio_read(pb, text, size)) != size)
                ret = AVERROR(EIO);
            else {
                int i = 0;
                av_log (s, AV_LOG_DEBUG, "Subtitle time!\n");
                av_log (s, AV_LOG_DEBUG, "  inglish: %s\n", &text[i + 1]);
                i += text[i] + 1;
                av_log (s, AV_LOG_DEBUG, "  doytsch: %s\n", &text[i + 1]);
                i += text[i] + 1;
                av_log (s, AV_LOG_DEBUG, "  fronsay: %s\n", &text[i + 1]);
            }
#endif
            break;

        case AUDI_TAG:
            /* send out audio chunk */
            ret= av_get_packet(pb, pkt, size);
            pkt->stream_index = wc3->audio_stream_index;
            pkt->pts = wc3->pts;

            /* time to advance pts */
            wc3->pts++;

            packet_read = 1;
            break;

        default:
            av_log (s, AV_LOG_ERROR, "  unrecognized WC3 chunk: %c%c%c%c (0x%02X%02X%02X%02X)\n",
                (uint8_t)fourcc_tag, (uint8_t)(fourcc_tag >> 8), (uint8_t)(fourcc_tag >> 16), (uint8_t)(fourcc_tag >> 24),
                (uint8_t)fourcc_tag, (uint8_t)(fourcc_tag >> 8), (uint8_t)(fourcc_tag >> 16), (uint8_t)(fourcc_tag >> 24));
            ret = AVERROR_INVALIDDATA;
            packet_read = 1;
            break;
        }
    }

    return ret;
}

static int wc3_read_close(AVFormatContext *s)
{
    Wc3DemuxContext *wc3 = s->priv_data;

    if (wc3->vpkt.size > 0)
        av_free_packet(&wc3->vpkt);

    return 0;
}

AVInputFormat ff_wc3_demuxer = {
    .name           = "wc3movie",
    .long_name      = NULL_IF_CONFIG_SMALL("Wing Commander III movie"),
    .priv_data_size = sizeof(Wc3DemuxContext),
    .read_probe     = wc3_probe,
    .read_header    = wc3_read_header,
    .read_packet    = wc3_read_packet,
    .read_close     = wc3_read_close,
};
