/*
 * Sega FILM Format (CPK) Muxer
 * Copyright (C) 2003 The FFmpeg project
 * Copyright (C) 2018 Misty De Meo
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
 * Sega FILM (.cpk) file muxer
 * @author Misty De Meo <misty@brew.sh>
 *
 * @see For more information regarding the Sega FILM file format, visit:
 *   http://wiki.multimedia.cx/index.php?title=Sega_FILM
 */

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"

typedef struct FILMOutputContext {
    AVIOContext *header;
    unsigned index;
    int audio_index;
    int video_index;
} FILMOutputContext;

static int film_write_packet(AVFormatContext *format_context, AVPacket *pkt)
{
    AVIOContext *pb = format_context->pb;
    FILMOutputContext *film = format_context->priv_data;
    int encoded_buf_size, size = pkt->size;
    uint32_t info1, info2;
    enum AVCodecID codec_id;

    codec_id = format_context->streams[pkt->stream_index]->codecpar->codec_id;

    /* Sega Cinepak has an extra two-byte header; write dummy data there,
     * then adjust the cvid header to accommodate for the extra size */
    if (codec_id == AV_CODEC_ID_CINEPAK) {
        encoded_buf_size = AV_RB24(&pkt->data[1]);
        /* Already Sega Cinepak, so no need to reformat the packets */
        if (encoded_buf_size != pkt->size && (pkt->size % encoded_buf_size) != 0) {
            avio_write(pb, pkt->data, pkt->size);
        } else {
            /* In Sega Cinepak, the reported size in the Cinepak header is
             * 8 bytes too short. However, the size in the STAB section of the header
             * is correct, taking into account the extra two bytes. */
            AV_WB24(&pkt->data[1], pkt->size - 8 + 2);
            size += 2;

            avio_write(pb, pkt->data, 10);
            avio_wb16(pb, 0);
            avio_write(pb, &pkt->data[10], pkt->size - 10);
        }
    } else {
        /* Other formats can just be written as-is */
        avio_write(pb, pkt->data, pkt->size);
    }

    /* Add the 16-byte sample info entry to the dynamic buffer
     * for the STAB chunk in the header */
    pb = film->header;
    avio_wb32(pb, film->index);
    film->index += size;
    avio_wb32(pb, size);
    if (film->audio_index == pkt->stream_index) {
        /* Always the same, carries no more information than "this is audio" */
        info1 = 0xFFFFFFFF;
        info2 = 1;
    } else {
        info1 = pkt->pts;
        info2 = pkt->duration;
        /* The top bit being set indicates a key frame */
        if (!(pkt->flags & AV_PKT_FLAG_KEY))
            info1 |= 1U << 31;
    }
    avio_wb32(pb, info1);
    avio_wb32(pb, info2);

    return pb->error;
}

static int get_audio_codec_id(enum AVCodecID codec_id)
{
    /* 0 (PCM) and 2 (ADX) are the only known values */
    switch (codec_id) {
    case AV_CODEC_ID_PCM_S8_PLANAR:
    case AV_CODEC_ID_PCM_S16BE_PLANAR:
        return 0;
    case AV_CODEC_ID_ADPCM_ADX:
        return 2;
    default:
        return -1;
    }
}

static int film_init(AVFormatContext *format_context)
{
    FILMOutputContext *film = format_context->priv_data;
    int ret;

    film->audio_index = -1;
    film->video_index = -1;

    for (int i = 0; i < format_context->nb_streams; i++) {
        AVStream *st = format_context->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (film->audio_index > -1) {
                av_log(format_context, AV_LOG_ERROR, "Sega FILM allows a maximum of one audio stream.\n");
                return AVERROR(EINVAL);
            }
            if (get_audio_codec_id(st->codecpar->codec_id) < 0) {
                av_log(format_context, AV_LOG_ERROR,
                       "Incompatible audio stream format.\n");
                return AVERROR(EINVAL);
            }
            film->audio_index = i;
        }

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (film->video_index > -1) {
                av_log(format_context, AV_LOG_ERROR, "Sega FILM allows a maximum of one video stream.\n");
                return AVERROR(EINVAL);
            }
            if (st->codecpar->codec_id != AV_CODEC_ID_CINEPAK &&
                st->codecpar->codec_id != AV_CODEC_ID_RAWVIDEO) {
                av_log(format_context, AV_LOG_ERROR,
                       "Incompatible video stream format.\n");
                return AVERROR(EINVAL);
            }
            if (st->codecpar->format != AV_PIX_FMT_RGB24) {
                av_log(format_context, AV_LOG_ERROR,
                       "Pixel format must be rgb24.\n");
                return AVERROR(EINVAL);
            }
            film->video_index = i;
        }
    }

    if (film->video_index == -1) {
        av_log(format_context, AV_LOG_ERROR, "No video stream present.\n");
        return AVERROR(EINVAL);
    }
    if ((ret = avio_open_dyn_buf(&film->header)) < 0)
        return ret;
    ffio_fill(film->header, 0, 16 + 32 + 16);

    return 0;
}

static int write_header(AVFormatContext *format_context, uint8_t *header,
                        unsigned header_size)
{
    int ret = ff_format_shift_data(format_context, 0, header_size);
    if (ret < 0)
        return ret;

    avio_seek(format_context->pb, 0, SEEK_SET);
    avio_write(format_context->pb, header, header_size);

    return 0;
}

static int film_write_header(AVFormatContext *format_context)
{
    int ret = 0;
    unsigned stabsize, headersize, packet_count;
    FILMOutputContext *film = format_context->priv_data;
    AVStream *video = NULL;
    uint8_t *header, *ptr;

    /* Calculate how much we need to reserve for the header;
     * this is the amount the rest of the data will be shifted up by. */
    headersize = avio_get_dyn_buf(film->header, &header);
    if (headersize < 64) {
        av_assert1(film->header->error < 0);
        return film->header->error;
    }
    packet_count = (headersize - 64) / 16;
    stabsize = 16 + 16 * packet_count;
    headersize = 16 + /* FILM header base */
                 32 + /* FDSC chunk */
                 stabsize;

    /* Write the header at the position in the buffer reserved for it.
     * First, write the FILM header; this is very simple */
    ptr = header;
    bytestream_put_be32(&ptr, MKBETAG('F', 'I', 'L', 'M'));
    bytestream_put_be32(&ptr, headersize);
    /* This seems to be okay to hardcode, since this muxer targets 1.09 features;
     * videos produced by this muxer are readable by 1.08 and lower players. */
    bytestream_put_be32(&ptr, MKBETAG('1', '.', '0', '9'));
    /* I have no idea what the next four bytes do, might be reserved */
    ptr += 4;

    /* Next write the FDSC (file description) chunk */
    bytestream_put_be32(&ptr, MKBETAG('F', 'D', 'S', 'C'));
    bytestream_put_be32(&ptr, 0x20); /* Size of FDSC chunk */

    video = format_context->streams[film->video_index];

    /* The only two supported codecs; raw video is rare */
    switch (video->codecpar->codec_id) {
    case AV_CODEC_ID_CINEPAK:
        bytestream_put_be32(&ptr, MKBETAG('c', 'v', 'i', 'd'));
        break;
    case AV_CODEC_ID_RAWVIDEO:
        bytestream_put_be32(&ptr, MKBETAG('r', 'a', 'w', ' '));
        break;
    }

    bytestream_put_be32(&ptr, video->codecpar->height);
    bytestream_put_be32(&ptr, video->codecpar->width);
    bytestream_put_byte(&ptr, 24); /* Bits per pixel - observed to always be 24 */

    if (film->audio_index > -1) {
        AVStream *audio = format_context->streams[film->audio_index];
        int audio_codec = get_audio_codec_id(audio->codecpar->codec_id);

        bytestream_put_byte(&ptr, audio->codecpar->ch_layout.nb_channels); /* Audio channels */
        bytestream_put_byte(&ptr, audio->codecpar->bits_per_coded_sample); /* Audio bit depth */
        bytestream_put_byte(&ptr, audio_codec); /* Compression - 0 is PCM, 2 is ADX */
        bytestream_put_be16(&ptr, audio->codecpar->sample_rate); /* Audio sampling rate */
    } else {
        /* If there is no audio, all the audio fields should be set to zero.
         * ffio_fill() already did this for us. */
        ptr += 1 + 1 + 1 + 2;
    }

    /* I have no idea what this pair of fields does either, might be reserved */
    ptr += 4 + 2;

    /* Finally, write the STAB (sample table) chunk */
    bytestream_put_be32(&ptr, MKBETAG('S', 'T', 'A', 'B'));
    bytestream_put_be32(&ptr, stabsize);
    /* Framerate base frequency. Here we're assuming that the frame rate is even.
     * In real world Sega FILM files, there are usually a couple of approaches:
     * a) framerate base frequency is the same as the framerate, and ticks
     *    increment by 1 every frame, or
     * b) framerate base frequency is a much larger number, and ticks
     *    increment by larger steps every frame.
     * The latter occurs even in cases where the frame rate is even; for example, in
     * Lunar: Silver Star Story, the base frequency is 600 and each frame, the ticks
     * are incremented by 25 for an evenly spaced framerate of 24fps. */
    bytestream_put_be32(&ptr, av_q2d(av_inv_q(video->time_base)));

    bytestream_put_be32(&ptr, packet_count);

    /* Finally, shift the data and write out the header. */
    ret = write_header(format_context, header, headersize);
    if (ret < 0)
        return ret;

    return 0;
}

static void film_deinit(AVFormatContext *format_context)
{
    FILMOutputContext *film = format_context->priv_data;

    ffio_free_dyn_buf(&film->header);
}

const AVOutputFormat ff_segafilm_muxer = {
    .name           = "film_cpk",
    .long_name      = NULL_IF_CONFIG_SMALL("Sega FILM / CPK"),
    .extensions     = "cpk",
    .priv_data_size = sizeof(FILMOutputContext),
    .audio_codec    = AV_CODEC_ID_PCM_S16BE_PLANAR,
    .video_codec    = AV_CODEC_ID_CINEPAK,
    .init           = film_init,
    .write_trailer  = film_write_header,
    .write_packet   = film_write_packet,
    .deinit         = film_deinit,
};
