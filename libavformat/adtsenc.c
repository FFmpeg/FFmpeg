/*
 * ADTS muxer.
 * Copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@smartjog.com>
 *                    Mans Rullgard <mans@mansr.com>
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

#include "libavcodec/bitstream.h"
#include "libavcodec/put_bits.h"
#include "libavcodec/internal.h"
#include "avformat.h"

#define ADTS_HEADER_SIZE 7

typedef struct {
    int write_adts;
    int objecttype;
    int sample_rate_index;
    int channel_conf;
} ADTSContext;

static int decode_extradata(AVFormatContext *s, ADTSContext *adts, uint8_t *buf, int size)
{
    GetBitContext gb;

    init_get_bits(&gb, buf, size * 8);
    adts->objecttype = get_bits(&gb, 5) - 1;
    adts->sample_rate_index = get_bits(&gb, 4);
    adts->channel_conf = get_bits(&gb, 4);

    if (adts->objecttype > 3U) {
        av_log(s, AV_LOG_ERROR, "MPEG-4 AOT %d is not allowed in ADTS\n", adts->objecttype+1);
        return -1;
    }
    if (adts->sample_rate_index == 15) {
        av_log(s, AV_LOG_ERROR, "Escape sample rate index illegal in ADTS\n");
        return -1;
    }
    if (adts->channel_conf == 0) {
        ff_log_missing_feature(s, "PCE based channel configuration", 0);
        return -1;
    }
    if (get_bits(&gb, 1)) {
        av_log(s, AV_LOG_ERROR, "960/120 MDCT window is not allowed in ADTS\n");
        return -1;
    }
    if (get_bits(&gb, 1)) {
        av_log(s, AV_LOG_ERROR, "Scalable configurations are not allowed in ADTS\n");
        return -1;
    }
    if (get_bits(&gb, 1)) {
        ff_log_missing_feature(s, "Signaled SBR or PS", 0);
        return -1;
    }

    adts->write_adts = 1;

    return 0;
}

static int adts_write_header(AVFormatContext *s)
{
    ADTSContext *adts = s->priv_data;
    AVCodecContext *avc = s->streams[0]->codec;

    if(avc->extradata_size > 0 &&
            decode_extradata(s, adts, avc->extradata, avc->extradata_size) < 0)
        return -1;

    return 0;
}

static int adts_write_frame_header(AVFormatContext *s, int size)
{
    ADTSContext *ctx = s->priv_data;
    PutBitContext pb;
    uint8_t buf[ADTS_HEADER_SIZE];

    init_put_bits(&pb, buf, ADTS_HEADER_SIZE);

    /* adts_fixed_header */
    put_bits(&pb, 12, 0xfff);   /* syncword */
    put_bits(&pb, 1, 0);        /* ID */
    put_bits(&pb, 2, 0);        /* layer */
    put_bits(&pb, 1, 1);        /* protection_absent */
    put_bits(&pb, 2, ctx->objecttype); /* profile_objecttype */
    put_bits(&pb, 4, ctx->sample_rate_index);
    put_bits(&pb, 1, 0);        /* private_bit */
    put_bits(&pb, 3, ctx->channel_conf); /* channel_configuration */
    put_bits(&pb, 1, 0);        /* original_copy */
    put_bits(&pb, 1, 0);        /* home */

    /* adts_variable_header */
    put_bits(&pb, 1, 0);        /* copyright_identification_bit */
    put_bits(&pb, 1, 0);        /* copyright_identification_start */
    put_bits(&pb, 13, ADTS_HEADER_SIZE + size); /* aac_frame_length */
    put_bits(&pb, 11, 0x7ff);   /* adts_buffer_fullness */
    put_bits(&pb, 2, 0);        /* number_of_raw_data_blocks_in_frame */

    flush_put_bits(&pb);
    put_buffer(s->pb, buf, ADTS_HEADER_SIZE);

    return 0;
}

static int adts_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ADTSContext *adts = s->priv_data;
    ByteIOContext *pb = s->pb;

    if (!pkt->size)
        return 0;
    if(adts->write_adts)
        adts_write_frame_header(s, pkt->size);
    put_buffer(pb, pkt->data, pkt->size);
    put_flush_packet(pb);

    return 0;
}

AVOutputFormat adts_muxer = {
    "adts",
    NULL_IF_CONFIG_SMALL("ADTS AAC"),
    "audio/aac",
    "aac",
    sizeof(ADTSContext),
    CODEC_ID_AAC,
    CODEC_ID_NONE,
    adts_write_header,
    adts_write_packet,
};
