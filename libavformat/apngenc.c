/*
 * APNG muxer
 * Copyright (c) 2015 Donny Yang
 *
 * first version by Donny Yang <work@kota.moe>
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
#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavcodec/png.h"
#include "libavcodec/apng.h"

typedef struct APNGMuxContext {
    AVClass *class;

    uint32_t plays;
    AVRational last_delay;

    uint64_t acTL_offset;
    uint32_t frame_number;

    AVPacket *prev_packet;
    AVRational prev_delay;

    int framerate_warned;

    uint8_t *extra_data;
    int extra_data_size;
} APNGMuxContext;

static uint8_t *apng_find_chunk(uint32_t tag, uint8_t *buf, size_t length)
{
    size_t b;
    for (b = 0; b < length; b += AV_RB32(buf + b) + 12)
        if (AV_RB32(&buf[b + 4]) == tag)
            return &buf[b];
    return NULL;
}

static void apng_write_chunk(AVIOContext *io_context, uint32_t tag,
                             uint8_t *buf, size_t length)
{
    const AVCRC *crc_table = av_crc_get_table(AV_CRC_32_IEEE_LE);
    uint32_t crc = ~0U;
    uint8_t tagbuf[4];

    av_assert0(crc_table);

    avio_wb32(io_context, length);
    AV_WB32(tagbuf, tag);
    crc = av_crc(crc_table, crc, tagbuf, 4);
    avio_wb32(io_context, tag);
    if (length > 0) {
        crc = av_crc(crc_table, crc, buf, length);
        avio_write(io_context, buf, length);
    }
    avio_wb32(io_context, ~crc);
}

static int apng_write_header(AVFormatContext *format_context)
{
    APNGMuxContext *apng = format_context->priv_data;
    AVCodecParameters *par = format_context->streams[0]->codecpar;

    if (format_context->nb_streams != 1 ||
        format_context->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
        format_context->streams[0]->codecpar->codec_id   != AV_CODEC_ID_APNG) {
        av_log(format_context, AV_LOG_ERROR,
               "APNG muxer supports only a single video APNG stream.\n");
        return AVERROR(EINVAL);
    }

    if (apng->last_delay.num > USHRT_MAX || apng->last_delay.den > USHRT_MAX) {
        av_reduce(&apng->last_delay.num, &apng->last_delay.den,
                  apng->last_delay.num, apng->last_delay.den, USHRT_MAX);
        av_log(format_context, AV_LOG_WARNING,
               "Last frame delay is too precise. Reducing to %d/%d (%f).\n",
               apng->last_delay.num, apng->last_delay.den, (double)apng->last_delay.num / apng->last_delay.den);
    }

    avio_wb64(format_context->pb, PNGSIG);
    // Remaining headers are written when they are copied from the encoder

    if (par->extradata_size) {
        apng->extra_data = av_mallocz(par->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!apng->extra_data)
            return AVERROR(ENOMEM);
        apng->extra_data_size = par->extradata_size;
        memcpy(apng->extra_data, par->extradata, par->extradata_size);
    }

    return 0;
}

static int flush_packet(AVFormatContext *format_context, AVPacket *packet)
{
    APNGMuxContext *apng = format_context->priv_data;
    AVIOContext *io_context = format_context->pb;
    AVStream *codec_stream = format_context->streams[0];
    uint8_t *side_data = NULL;
    int side_data_size;

    av_assert0(apng->prev_packet);

    side_data = av_packet_get_side_data(apng->prev_packet, AV_PKT_DATA_NEW_EXTRADATA, &side_data_size);

    if (side_data_size) {
        av_freep(&apng->extra_data);
        apng->extra_data = av_mallocz(side_data_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!apng->extra_data)
            return AVERROR(ENOMEM);
        apng->extra_data_size = side_data_size;
        memcpy(apng->extra_data, side_data, apng->extra_data_size);
    }

    if (apng->frame_number == 0 && !packet) {
        uint8_t *existing_acTL_chunk;
        uint8_t *existing_fcTL_chunk;

        av_log(format_context, AV_LOG_INFO, "Only a single frame so saving as a normal PNG.\n");

        // Write normal PNG headers without acTL chunk
        existing_acTL_chunk = apng_find_chunk(MKBETAG('a', 'c', 'T', 'L'), apng->extra_data, apng->extra_data_size);
        if (existing_acTL_chunk) {
            uint8_t *chunk_after_acTL = existing_acTL_chunk + AV_RB32(existing_acTL_chunk) + 12;
            avio_write(io_context, apng->extra_data, existing_acTL_chunk - apng->extra_data);
            avio_write(io_context, chunk_after_acTL, apng->extra_data + apng->extra_data_size - chunk_after_acTL);
        } else {
            avio_write(io_context, apng->extra_data, apng->extra_data_size);
        }

        // Write frame data without fcTL chunk
        existing_fcTL_chunk = apng_find_chunk(MKBETAG('f', 'c', 'T', 'L'), apng->prev_packet->data, apng->prev_packet->size);
        if (existing_fcTL_chunk) {
            uint8_t *chunk_after_fcTL = existing_fcTL_chunk + AV_RB32(existing_fcTL_chunk) + 12;
            avio_write(io_context, apng->prev_packet->data, existing_fcTL_chunk - apng->prev_packet->data);
            avio_write(io_context, chunk_after_fcTL, apng->prev_packet->data + apng->prev_packet->size - chunk_after_fcTL);
        } else {
            avio_write(io_context, apng->prev_packet->data, apng->prev_packet->size);
        }
    } else {
        uint8_t *existing_fcTL_chunk;

        if (apng->frame_number == 0) {
            uint8_t *existing_acTL_chunk;

            // Write normal PNG headers
            avio_write(io_context, apng->extra_data, apng->extra_data_size);

            existing_acTL_chunk = apng_find_chunk(MKBETAG('a', 'c', 'T', 'L'), apng->extra_data, apng->extra_data_size);
            if (!existing_acTL_chunk) {
                uint8_t buf[8];
                // Write animation control header
                apng->acTL_offset = avio_tell(io_context);
                AV_WB32(buf, UINT_MAX); // number of frames (filled in later)
                AV_WB32(buf + 4, apng->plays);
                apng_write_chunk(io_context, MKBETAG('a', 'c', 'T', 'L'), buf, 8);
            }
        }

        existing_fcTL_chunk = apng_find_chunk(MKBETAG('f', 'c', 'T', 'L'), apng->prev_packet->data, apng->prev_packet->size);
        if (existing_fcTL_chunk) {
            AVRational delay;

            existing_fcTL_chunk += 8;
            delay.num = AV_RB16(existing_fcTL_chunk + 20);
            delay.den = AV_RB16(existing_fcTL_chunk + 22);

            if (delay.num == 0 && delay.den == 0) {
                if (packet) {
                    int64_t delay_num_raw = (packet->dts - apng->prev_packet->dts) * codec_stream->time_base.num;
                    int64_t delay_den_raw = codec_stream->time_base.den;
                    if (!av_reduce(&delay.num, &delay.den, delay_num_raw, delay_den_raw, USHRT_MAX) &&
                        !apng->framerate_warned) {
                        av_log(format_context, AV_LOG_WARNING,
                               "Frame rate is too high or specified too precisely. Unable to copy losslessly.\n");
                        apng->framerate_warned = 1;
                    }
                } else if (apng->last_delay.num > 0) {
                    delay = apng->last_delay;
                } else {
                    delay = apng->prev_delay;
                }

                // Update frame control header with new delay
                AV_WB16(existing_fcTL_chunk + 20, delay.num);
                AV_WB16(existing_fcTL_chunk + 22, delay.den);
                AV_WB32(existing_fcTL_chunk + 26, ~av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), ~0U, existing_fcTL_chunk - 4, 26 + 4));
            }
            apng->prev_delay = delay;
        }

        // Write frame data
        avio_write(io_context, apng->prev_packet->data, apng->prev_packet->size);
    }
    ++apng->frame_number;

    av_packet_unref(apng->prev_packet);
    if (packet)
        av_packet_ref(apng->prev_packet, packet);
    return 0;
}

static int apng_write_packet(AVFormatContext *format_context, AVPacket *packet)
{
    APNGMuxContext *apng = format_context->priv_data;
    int ret;

    if (!apng->prev_packet) {
        apng->prev_packet = av_packet_alloc();
        if (!apng->prev_packet)
            return AVERROR(ENOMEM);

        av_packet_ref(apng->prev_packet, packet);
    } else {
        ret = flush_packet(format_context, packet);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int apng_write_trailer(AVFormatContext *format_context)
{
    APNGMuxContext *apng = format_context->priv_data;
    AVIOContext *io_context = format_context->pb;
    uint8_t buf[8];
    int ret;

    if (apng->prev_packet) {
        ret = flush_packet(format_context, NULL);
        if (ret < 0)
            return ret;
    }

    apng_write_chunk(io_context, MKBETAG('I', 'E', 'N', 'D'), NULL, 0);

    if (apng->acTL_offset && (io_context->seekable & AVIO_SEEKABLE_NORMAL)) {
        avio_seek(io_context, apng->acTL_offset, SEEK_SET);

        AV_WB32(buf, apng->frame_number);
        AV_WB32(buf + 4, apng->plays);
        apng_write_chunk(io_context, MKBETAG('a', 'c', 'T', 'L'), buf, 8);
    }

    return 0;
}

static void apng_deinit(AVFormatContext *s)
{
    APNGMuxContext *apng = s->priv_data;

    av_packet_free(&apng->prev_packet);
    av_freep(&apng->extra_data);
    apng->extra_data_size = 0;
}

#define OFFSET(x) offsetof(APNGMuxContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "plays", "Number of times to play the output: 0 - infinite loop, 1 - no loop", OFFSET(plays),
      AV_OPT_TYPE_INT, { .i64 = 1 }, 0, UINT_MAX, ENC },
    { "final_delay", "Force delay after the last frame", OFFSET(last_delay),
      AV_OPT_TYPE_RATIONAL, { .dbl = 0 }, 0, USHRT_MAX, ENC },
    { NULL },
};

static const AVClass apng_muxer_class = {
    .class_name = "APNG muxer",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .option     = options,
};

AVOutputFormat ff_apng_muxer = {
    .name           = "apng",
    .long_name      = NULL_IF_CONFIG_SMALL("Animated Portable Network Graphics"),
    .mime_type      = "image/png",
    .extensions     = "apng",
    .priv_data_size = sizeof(APNGMuxContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_APNG,
    .write_header   = apng_write_header,
    .write_packet   = apng_write_packet,
    .write_trailer  = apng_write_trailer,
    .deinit         = apng_deinit,
    .priv_class     = &apng_muxer_class,
    .flags          = AVFMT_VARIABLE_FPS,
};
