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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"

typedef struct FILMPacket {
    int audio;
    int keyframe;
    int32_t pts;
    int32_t duration;
    int32_t size;
    int32_t index;
    struct FILMPacket *next;
} FILMPacket;

typedef struct FILMOutputContext {
    int audio_index;
    int video_index;
    int64_t stab_pos;
    FILMPacket *start;
    FILMPacket *last;
    int64_t packet_count;
} FILMOutputContext;

static int film_write_packet_to_header(AVFormatContext *format_context, FILMPacket *pkt)
{
    AVIOContext *pb = format_context->pb;
    /* The bits in these two 32-bit integers contain info about the contents of this sample */
    int32_t info1 = 0;
    int32_t info2 = 0;

    if (pkt->audio) {
        /* Always the same, carries no more information than "this is audio" */
        info1 = 0xFFFFFFFF;
        info2 = 1;
    } else {
        info1 = pkt->pts;
        info2 = pkt->duration;
        /* The top bit being set indicates a key frame */
        if (!pkt->keyframe)
            info1 |= 1U << 31;
    }

    /* Write the 16-byte sample info packet to the STAB chunk in the header */
    avio_wb32(pb, pkt->index);
    avio_wb32(pb, pkt->size);
    avio_wb32(pb, info1);
    avio_wb32(pb, info2);

    return 0;
}

static int film_write_packet(AVFormatContext *format_context, AVPacket *pkt)
{
    FILMPacket *metadata;
    AVIOContext *pb = format_context->pb;
    FILMOutputContext *film = format_context->priv_data;
    int encoded_buf_size = 0;
    enum AVCodecID codec_id;

    /* Track the metadata used to write the header and add it to the linked list */
    metadata = av_mallocz(sizeof(FILMPacket));
    if (!metadata)
        return AVERROR(ENOMEM);
    metadata->audio = pkt->stream_index == film->audio_index;
    metadata->keyframe = pkt->flags & AV_PKT_FLAG_KEY;
    metadata->pts = pkt->pts;
    metadata->duration = pkt->duration;
    metadata->size = pkt->size;
    if (film->last == NULL) {
        metadata->index = 0;
    } else {
        metadata->index = film->last->index + film->last->size;
        film->last->next = metadata;
    }
    metadata->next = NULL;
    if (film->start == NULL)
        film->start = metadata;
    film->packet_count++;
    film->last = metadata;

    codec_id = format_context->streams[pkt->stream_index]->codecpar->codec_id;

    /* Sega Cinepak has an extra two-byte header; write dummy data there,
     * then adjust the cvid header to accommodate for the extra size */
    if (codec_id == AV_CODEC_ID_CINEPAK) {
        encoded_buf_size = AV_RB24(&pkt->data[1]);
        /* Already Sega Cinepak, so no need to reformat the packets */
        if (encoded_buf_size != pkt->size && (pkt->size % encoded_buf_size) != 0) {
            avio_write(pb, pkt->data, pkt->size);
        } else {
            uint8_t padding[2] = {0, 0};
            /* In Sega Cinepak, the reported size in the Cinepak header is
             * 8 bytes too short. However, the size in the STAB section of the header
             * is correct, taking into account the extra two bytes. */
            AV_WB24(&pkt->data[1], pkt->size - 8 + 2);
            metadata->size += 2;

            avio_write(pb, pkt->data, 10);
            avio_write(pb, padding, 2);
            avio_write(pb, &pkt->data[10], pkt->size - 10);
        }
    } else {
        /* Other formats can just be written as-is */
        avio_write(pb, pkt->data, pkt->size);
    }

    return 0;
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
    AVStream *audio = NULL;
    FILMOutputContext *film = format_context->priv_data;
    film->audio_index = -1;
    film->video_index = -1;
    film->stab_pos = 0;
    film->packet_count = 0;
    film->start = NULL;
    film->last = NULL;

    for (int i = 0; i < format_context->nb_streams; i++) {
        AVStream *st = format_context->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (film->audio_index > -1) {
                av_log(format_context, AV_LOG_ERROR, "Sega FILM allows a maximum of one audio stream.\n");
                return AVERROR(EINVAL);
            }
            film->audio_index = i;
            audio = st;
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

    if (audio != NULL && get_audio_codec_id(audio->codecpar->codec_id) < 0) {
        av_log(format_context, AV_LOG_ERROR, "Incompatible audio stream format.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int shift_data(AVFormatContext *format_context, int64_t shift_size)
{
    int ret = 0;
    int64_t pos, pos_end;
    uint8_t *buf, *read_buf[2];
    int read_buf_id = 0;
    int read_size[2];
    AVIOContext *read_pb;

    buf = av_malloc(shift_size * 2);
    if (!buf)
        return AVERROR(ENOMEM);
    read_buf[0] = buf;
    read_buf[1] = buf + shift_size;

    /* Write the header at the beginning of the file, shifting all content as necessary;
     * based on the approach used by MOV faststart. */
    avio_flush(format_context->pb);
    ret = format_context->io_open(format_context, &read_pb, format_context->url, AVIO_FLAG_READ, NULL);
    if (ret < 0) {
        av_log(format_context, AV_LOG_ERROR, "Unable to re-open %s output file to "
               "write the header\n", format_context->url);
        av_free(buf);
        return ret;
    }

    /* mark the end of the shift to up to the last data we wrote, and get ready
     * for writing */
    pos_end = avio_tell(format_context->pb);
    avio_seek(format_context->pb, shift_size, SEEK_SET);

    /* start reading at where the new header will be placed */
    avio_seek(read_pb, 0, SEEK_SET);
    pos = avio_tell(read_pb);

#define READ_BLOCK do {                                                             \
    read_size[read_buf_id] = avio_read(read_pb, read_buf[read_buf_id], shift_size);  \
    read_buf_id ^= 1;                                                               \
} while (0)

    /* shift data by chunk of at most shift_size */
    READ_BLOCK;
    do {
        int n;
        READ_BLOCK;
        n = read_size[read_buf_id];
        if (n <= 0)
            break;
        avio_write(format_context->pb, read_buf[read_buf_id], n);
        pos += n;
    } while (pos < pos_end);
    ff_format_io_close(format_context, &read_pb);

    av_free(buf);
    return 0;
}

static int film_write_header(AVFormatContext *format_context)
{
    int ret = 0;
    int64_t sample_table_size, stabsize, headersize;
    int8_t audio_codec;
    AVIOContext *pb = format_context->pb;
    FILMOutputContext *film = format_context->priv_data;
    FILMPacket *prev, *packet;
    AVStream *audio = NULL;
    AVStream *video = NULL;

    /* Calculate how much we need to reserve for the header;
     * this is the amount the rest of the data will be shifted up by. */
    sample_table_size = film->packet_count * 16;
    stabsize = 16 + sample_table_size;
    headersize = 16 + /* FILM header base */
                 32 + /* FDSC chunk */
                 stabsize;

    ret = shift_data(format_context, headersize);
    if (ret < 0)
        return ret;
    /* Seek back to the beginning to start writing the header now */
    avio_seek(pb, 0, SEEK_SET);

    if (film->audio_index > -1)
        audio = format_context->streams[film->audio_index];
    if (film->video_index > -1)
        video = format_context->streams[film->video_index];

    if (audio != NULL) {
        audio_codec = get_audio_codec_id(audio->codecpar->codec_id);
        if (audio_codec < 0) {
            av_log(format_context, AV_LOG_ERROR, "Incompatible audio stream format.\n");
            return AVERROR(EINVAL);
        }
    }

    /* First, write the FILM header; this is very simple */

    ffio_wfourcc(pb, "FILM");
    avio_wb32(pb, 48 + stabsize);
    /* This seems to be okay to hardcode, since this muxer targets 1.09 features;
     * videos produced by this muxer are readable by 1.08 and lower players. */
    ffio_wfourcc(pb, "1.09");
    /* I have no idea what this field does, might be reserved */
    avio_wb32(pb, 0);

    /* Next write the FDSC (file description) chunk */
    ffio_wfourcc(pb, "FDSC");
    avio_wb32(pb, 0x20); /* Size of FDSC chunk */

    /* The only two supported codecs; raw video is rare */
    switch (video->codecpar->codec_id) {
    case AV_CODEC_ID_CINEPAK:
        ffio_wfourcc(pb, "cvid");
        break;
    case AV_CODEC_ID_RAWVIDEO:
        ffio_wfourcc(pb, "raw ");
        break;
    }

    avio_wb32(pb, video->codecpar->height);
    avio_wb32(pb, video->codecpar->width);
    avio_w8(pb, 24); /* Bits per pixel - observed to always be 24 */

    if (audio != NULL) {
        avio_w8(pb, audio->codecpar->channels); /* Audio channels */
        avio_w8(pb, audio->codecpar->bits_per_coded_sample); /* Audio bit depth */
        avio_w8(pb, audio_codec); /* Compression - 0 is PCM, 2 is ADX */
        avio_wb16(pb, audio->codecpar->sample_rate); /* Audio sampling rate */
    } else {
        /* Set all these fields to 0 if there's no audio */
        avio_w8(pb, 0);
        avio_w8(pb, 0);
        avio_w8(pb, 0);
        avio_wb16(pb, 0);
    }

    /* I have no idea what this pair of fields does either, might be reserved */
    avio_wb32(pb, 0);
    avio_wb16(pb, 0);

    /* Finally, write the STAB (sample table) chunk */
    ffio_wfourcc(pb, "STAB");
    avio_wb32(pb, 16 + (film->packet_count * 16));
    /* Framerate base frequency. Here we're assuming that the frame rate is even.
     * In real world Sega FILM files, there are usually a couple of approaches:
     * a) framerate base frequency is the same as the framerate, and ticks
     *    increment by 1 every frame, or
     * b) framerate base frequency is a much larger number, and ticks
     *    increment by larger steps every frame.
     * The latter occurs even in cases where the frame rate is even; for example, in
     * Lunar: Silver Star Story, the base frequency is 600 and each frame, the ticks
     * are incremented by 25 for an evenly spaced framerate of 24fps. */
    avio_wb32(pb, av_q2d(av_inv_q(video->time_base)));

    avio_wb32(pb, film->packet_count);

    /* Finally, write out each packet's data to the header */
    packet = film->start;
    while (packet != NULL) {
        film_write_packet_to_header(format_context, packet);
        prev = packet;
        packet = packet->next;
        av_freep(&prev);
    }

    return 0;
}

AVOutputFormat ff_segafilm_muxer = {
    .name           = "film_cpk",
    .long_name      = NULL_IF_CONFIG_SMALL("Sega FILM / CPK"),
    .extensions     = "cpk",
    .priv_data_size = sizeof(FILMOutputContext),
    .audio_codec    = AV_CODEC_ID_PCM_S16BE_PLANAR,
    .video_codec    = AV_CODEC_ID_CINEPAK,
    .init           = film_init,
    .write_trailer  = film_write_header,
    .write_packet   = film_write_packet,
};
