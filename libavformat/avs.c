/*
 * AVS demuxer.
 * Copyright (c) 2006  Aurelien Jacobs <aurel@gnuage.org>
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
#include "voc.h"


typedef struct avs_format {
    VocDecContext voc;
    AVStream *st_video;
    AVStream *st_audio;
    int width;
    int height;
    int bits_per_sample;
    int fps;
    int nb_frames;
    int remaining_frame_size;
    int remaining_audio_size;
} AvsFormat;

typedef enum avs_block_type {
    AVS_NONE      = 0x00,
    AVS_VIDEO     = 0x01,
    AVS_AUDIO     = 0x02,
    AVS_PALETTE   = 0x03,
    AVS_GAME_DATA = 0x04,
} AvsBlockType;

static int avs_probe(AVProbeData * p)
{
    const uint8_t *d;

    d = p->buf;
    if (d[0] == 'w' && d[1] == 'W' && d[2] == 0x10 && d[3] == 0)
        /* Ensure the buffer probe scores higher than the extension probe.
         * This avoids problems with misdetection as AviSynth scripts. */
        return AVPROBE_SCORE_EXTENSION + 1;

    return 0;
}

static int avs_read_header(AVFormatContext * s)
{
    AvsFormat *avs = s->priv_data;

    s->ctx_flags |= AVFMTCTX_NOHEADER;

    avio_skip(s->pb, 4);
    avs->width = avio_rl16(s->pb);
    avs->height = avio_rl16(s->pb);
    avs->bits_per_sample = avio_rl16(s->pb);
    avs->fps = avio_rl16(s->pb);
    avs->nb_frames = avio_rl32(s->pb);
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
                      AvsBlockType type, int sub_type, int size,
                      uint8_t * palette, int palette_size)
{
    AvsFormat *avs = s->priv_data;
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
    ret = avio_read(s->pb, pkt->data + palette_size + 4, size - 4) + 4;
    if (ret < size) {
        av_packet_unref(pkt);
        return AVERROR(EIO);
    }

    pkt->size = ret + palette_size;
    pkt->stream_index = avs->st_video->index;
    if (sub_type == 0)
        pkt->flags |= AV_PKT_FLAG_KEY;

    return 0;
}

static int avs_read_audio_packet(AVFormatContext * s, AVPacket * pkt)
{
    AvsFormat *avs = s->priv_data;
    int ret, size;

    size = avio_tell(s->pb);
    ret = ff_voc_get_packet(s, pkt, avs->st_audio, avs->remaining_audio_size);
    size = avio_tell(s->pb) - size;
    avs->remaining_audio_size -= size;

    if (ret == AVERROR(EIO))
        return 0;    /* this indicate EOS */
    if (ret < 0)
        return ret;

    pkt->stream_index = avs->st_audio->index;
    pkt->flags |= AV_PKT_FLAG_KEY;

    return size;
}

static int avs_read_packet(AVFormatContext * s, AVPacket * pkt)
{
    AvsFormat *avs = s->priv_data;
    int sub_type = 0, size = 0;
    AvsBlockType type = AVS_NONE;
    int palette_size = 0;
    uint8_t palette[4 + 3 * 256];
    int ret;

    if (avs->remaining_audio_size > 0)
        if (avs_read_audio_packet(s, pkt) > 0)
            return 0;

    while (1) {
        if (avs->remaining_frame_size <= 0) {
            if (!avio_rl16(s->pb))    /* found EOF */
                return AVERROR(EIO);
            avs->remaining_frame_size = avio_rl16(s->pb) - 4;
        }

        while (avs->remaining_frame_size > 0) {
            sub_type = avio_r8(s->pb);
            type = avio_r8(s->pb);
            size = avio_rl16(s->pb);
            if (size < 4)
                return AVERROR_INVALIDDATA;
            avs->remaining_frame_size -= size;

            switch (type) {
            case AVS_PALETTE:
                if (size - 4 > sizeof(palette))
                    return AVERROR_INVALIDDATA;
                ret = avio_read(s->pb, palette, size - 4);
                if (ret < size - 4)
                    return AVERROR(EIO);
                palette_size = size;
                break;

            case AVS_VIDEO:
                if (!avs->st_video) {
                    avs->st_video = avformat_new_stream(s, NULL);
                    if (!avs->st_video)
                        return AVERROR(ENOMEM);
                    avs->st_video->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                    avs->st_video->codecpar->codec_id = AV_CODEC_ID_AVS;
                    avs->st_video->codecpar->width = avs->width;
                    avs->st_video->codecpar->height = avs->height;
                    avs->st_video->codecpar->bits_per_coded_sample=avs->bits_per_sample;
                    avs->st_video->nb_frames = avs->nb_frames;
                    avs->st_video->avg_frame_rate = (AVRational){avs->fps, 1};
                }
                return avs_read_video_packet(s, pkt, type, sub_type, size,
                                             palette, palette_size);

            case AVS_AUDIO:
                if (!avs->st_audio) {
                    avs->st_audio = avformat_new_stream(s, NULL);
                    if (!avs->st_audio)
                        return AVERROR(ENOMEM);
                    avs->st_audio->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
                }
                avs->remaining_audio_size = size - 4;
                size = avs_read_audio_packet(s, pkt);
                if (size != 0)
                    return size;
                break;

            default:
                avio_skip(s->pb, size - 4);
            }
        }
    }
}

static int avs_read_close(AVFormatContext * s)
{
    return 0;
}

AVInputFormat ff_avs_demuxer = {
    .name           = "avs",
    .long_name      = NULL_IF_CONFIG_SMALL("AVS"),
    .priv_data_size = sizeof(AvsFormat),
    .read_probe     = avs_probe,
    .read_header    = avs_read_header,
    .read_packet    = avs_read_packet,
    .read_close     = avs_read_close,
};
