/*
 * AVS demuxer.
 * Copyright (c) 2006  Aurelien Jacobs <aurel@gnuage.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "avformat.h"
#include "voc.h"


typedef struct avs_format {
    voc_dec_context_t voc;
    AVStream *st_video;
    AVStream *st_audio;
    int width;
    int height;
    int bits_per_sample;
    int fps;
    int nb_frames;
    int remaining_frame_size;
    int remaining_audio_size;
} avs_format_t;

typedef enum avs_block_type {
    AVS_VIDEO     = 0x01,
    AVS_AUDIO     = 0x02,
    AVS_PALETTE   = 0x03,
    AVS_GAME_DATA = 0x04,
} avs_block_type_t;

static int avs_probe(AVProbeData * p)
{
    const uint8_t *d;

    if (p->buf_size < 2)
        return 0;
    d = p->buf;
    if (d[0] == 'w' && d[1] == 'W' && d[2] == 0x10 && d[3] == 0)
        return 50;

    return 0;
}

static int avs_read_header(AVFormatContext * s, AVFormatParameters * ap)
{
    avs_format_t *avs = s->priv_data;

    s->ctx_flags |= AVFMTCTX_NOHEADER;

    url_fskip(&s->pb, 4);
    avs->width = get_le16(&s->pb);
    avs->height = get_le16(&s->pb);
    avs->bits_per_sample = get_le16(&s->pb);
    avs->fps = get_le16(&s->pb);
    avs->nb_frames = get_le32(&s->pb);
    avs->remaining_frame_size = 0;
    avs->remaining_audio_size = 0;

    avs->st_video = avs->st_audio = NULL;

    if (avs->width != 318 || avs->height != 198)
        av_log(s, AV_LOG_ERROR, "This avs pretend to be %dx%d "
               "when the avs format is supposed to be 318x198 only.\n",
               avs->width, avs->height);

    return 0;
}

static int
avs_read_video_packet(AVFormatContext * s, AVPacket * pkt,
                      avs_block_type_t type, int sub_type, int size,
                      uint8_t * palette, int palette_size)
{
    avs_format_t *avs = s->priv_data;
    int ret;

    ret = av_new_packet(pkt, size + palette_size);
    if (ret < 0)
        return ret;

    if (palette_size) {
        pkt->data[0] = 0x00;
        pkt->data[1] = 0x03;
        pkt->data[2] = palette_size & 0xFF;
        pkt->data[3] = (palette_size >> 8) & 0xFF;
        memcpy(pkt->data + 4, palette, palette_size - 4);
    }

    pkt->data[palette_size + 0] = sub_type;
    pkt->data[palette_size + 1] = type;
    pkt->data[palette_size + 2] = size & 0xFF;
    pkt->data[palette_size + 3] = (size >> 8) & 0xFF;
    ret = get_buffer(&s->pb, pkt->data + palette_size + 4, size - 4) + 4;
    if (ret < size) {
        av_free_packet(pkt);
        return AVERROR_IO;
    }

    pkt->size = ret + palette_size;
    pkt->stream_index = avs->st_video->index;
    if (sub_type == 0)
        pkt->flags |= PKT_FLAG_KEY;

    return 0;
}

static int avs_read_audio_packet(AVFormatContext * s, AVPacket * pkt)
{
    avs_format_t *avs = s->priv_data;
    int ret, size;

    size = url_ftell(&s->pb);
    ret = voc_get_packet(s, pkt, avs->st_audio, avs->remaining_audio_size);
    size = url_ftell(&s->pb) - size;
    avs->remaining_audio_size -= size;

    if (ret == AVERROR_IO)
        return 0;    /* this indicate EOS */
    if (ret < 0)
        return ret;

    pkt->stream_index = avs->st_audio->index;
    pkt->flags |= PKT_FLAG_KEY;

    return size;
}

static int avs_read_packet(AVFormatContext * s, AVPacket * pkt)
{
    avs_format_t *avs = s->priv_data;
    int sub_type = 0, size = 0;
    avs_block_type_t type = 0;
    int palette_size = 0;
    uint8_t palette[4 + 3 * 256];
    int ret;

    if (avs->remaining_audio_size > 0)
        if (avs_read_audio_packet(s, pkt) > 0)
            return 0;

    while (1) {
        if (avs->remaining_frame_size <= 0) {
            if (!get_le16(&s->pb))    /* found EOF */
                return AVERROR_IO;
            avs->remaining_frame_size = get_le16(&s->pb) - 4;
        }

        while (avs->remaining_frame_size > 0) {
            sub_type = get_byte(&s->pb);
            type = get_byte(&s->pb);
            size = get_le16(&s->pb);
            avs->remaining_frame_size -= size;

            switch (type) {
            case AVS_PALETTE:
                ret = get_buffer(&s->pb, palette, size - 4);
                if (ret < size - 4)
                    return AVERROR_IO;
                palette_size = size;
                break;

            case AVS_VIDEO:
                if (!avs->st_video) {
                    avs->st_video = av_new_stream(s, AVS_VIDEO);
                    if (avs->st_video == NULL)
                        return AVERROR_NOMEM;
                    avs->st_video->codec->codec_type = CODEC_TYPE_VIDEO;
                    avs->st_video->codec->codec_id = CODEC_ID_AVS;
                    avs->st_video->codec->width = avs->width;
                    avs->st_video->codec->height = avs->height;
                    avs->st_video->codec->bits_per_sample=avs->bits_per_sample;
                    avs->st_video->nb_frames = avs->nb_frames;
                    avs->st_video->codec->time_base = (AVRational) {
                    1, avs->fps};
                }
                return avs_read_video_packet(s, pkt, type, sub_type, size,
                                             palette, palette_size);

            case AVS_AUDIO:
                if (!avs->st_audio) {
                    avs->st_audio = av_new_stream(s, AVS_AUDIO);
                    if (avs->st_audio == NULL)
                        return AVERROR_NOMEM;
                    avs->st_audio->codec->codec_type = CODEC_TYPE_AUDIO;
                }
                avs->remaining_audio_size = size - 4;
                size = avs_read_audio_packet(s, pkt);
                if (size != 0)
                    return size;
                break;

            default:
                url_fskip(&s->pb, size - 4);
            }
        }
    }
}

static int avs_read_close(AVFormatContext * s)
{
    return 0;
}

AVInputFormat avs_demuxer = {
    "avs",
    "avs format",
    sizeof(avs_format_t),
    avs_probe,
    avs_read_header,
    avs_read_packet,
    avs_read_close,
};
