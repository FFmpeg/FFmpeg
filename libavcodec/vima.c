/*
 * LucasArts VIMA decoder
 * Copyright (c) 2012 Paul B Mahol
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
 * LucasArts VIMA audio decoder
 * @author Paul B Mahol
 */

#include "libavutil/channel_layout.h"
#include "libavutil/thread.h"

#include "adpcm_data.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "internal.h"

static uint16_t predict_table[5786 * 2];

static const uint8_t size_table[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
};

static const int8_t index_table1[] = {
    -1, 4, -1, 4
};

static const int8_t index_table2[] = {
    -1, -1, 2, 6, -1, -1, 2, 6
};

static const int8_t index_table3[] = {
    -1, -1, -1, -1, 1, 2, 4, 6, -1, -1, -1, -1, 1, 2, 4, 6
};

static const int8_t index_table4[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, 1,  1,  1,  2,  2,  4,  5,  6,
    -1, -1, -1, -1, -1, -1, -1, -1, 1,  1,  1,  2,  2,  4,  5,  6
};

static const int8_t index_table5[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     1,  1,  1,  1,  1,  2,  2,  2,  2,  4,  4,  4,  5,  5,  6,  6,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     1,  1,  1,  1,  1,  2,  2,  2,  2,  4,  4,  4,  5,  5,  6,  6
};

static const int8_t index_table6[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,
     2,  2,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,
     2,  2,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6
};

static const int8_t *const step_index_tables[] = {
    index_table1, index_table2, index_table3,
    index_table4, index_table5, index_table6
};

static av_cold void predict_table_init(void)
{
    for (int start_pos = 0; start_pos < 64; start_pos++) {
        unsigned int dest_pos, table_pos;

        for (table_pos = 0, dest_pos = start_pos;
             table_pos < FF_ARRAY_ELEMS(ff_adpcm_step_table);
             table_pos++, dest_pos += 64) {
            int put = 0, count, table_value;

            table_value = ff_adpcm_step_table[table_pos];
            for (count = 32; count != 0; count >>= 1) {
                if (start_pos & count)
                    put += table_value;
                table_value >>= 1;
            }
            predict_table[dest_pos] = put;
        }
    }
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    ff_thread_once(&init_static_once, predict_table_init);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame_ptr, AVPacket *pkt)
{
    GetBitContext gb;
    int16_t pcm_data[2];
    uint32_t samples;
    int8_t channel_hint[2];
    int ret, chan;
    int channels = 1;

    if (pkt->size < 13)
        return AVERROR_INVALIDDATA;

    if ((ret = init_get_bits8(&gb, pkt->data, pkt->size)) < 0)
        return ret;

    samples = get_bits_long(&gb, 32);
    if (samples == 0xffffffff) {
        skip_bits_long(&gb, 32);
        samples = get_bits_long(&gb, 32);
    }

    if (samples > pkt->size * 2)
        return AVERROR_INVALIDDATA;

    channel_hint[0] = get_sbits(&gb, 8);
    if (channel_hint[0] & 0x80) {
        channel_hint[0] = ~channel_hint[0];
        channels = 2;
    }
    av_channel_layout_uninit(&avctx->ch_layout);
    av_channel_layout_default(&avctx->ch_layout, channels);
    pcm_data[0] = get_sbits(&gb, 16);
    if (channels > 1) {
        channel_hint[1] = get_sbits(&gb, 8);
        pcm_data[1]     = get_sbits(&gb, 16);
    }

    frame->nb_samples = samples;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (chan = 0; chan < channels; chan++) {
        uint16_t *dest = (uint16_t *)frame->data[0] + chan;
        int step_index = channel_hint[chan];
        int output = pcm_data[chan];
        int sample;

        for (sample = 0; sample < samples; sample++) {
            int lookup_size, lookup, highbit, lowbits;

            step_index  = av_clip(step_index, 0, 88);
            lookup_size = size_table[step_index];
            lookup      = get_bits(&gb, lookup_size);
            highbit     = 1 << (lookup_size - 1);
            lowbits     = highbit - 1;

            if (lookup & highbit)
                lookup ^= highbit;
            else
                highbit = 0;

            if (lookup == lowbits) {
                output = get_sbits(&gb, 16);
            } else {
                int predict_index, diff;

                predict_index = (lookup << (7 - lookup_size)) | (step_index << 6);
                predict_index = av_clip(predict_index, 0, 5785);
                diff          = predict_table[predict_index];
                if (lookup)
                    diff += ff_adpcm_step_table[step_index] >> (lookup_size - 1);
                if (highbit)
                    diff = -diff;

                output = av_clip_int16(output + diff);
            }

            *dest = output;
            dest += channels;

            step_index += step_index_tables[lookup_size - 2][lookup];
        }
    }

    *got_frame_ptr = 1;

    return pkt->size;
}

const FFCodec ff_adpcm_vima_decoder = {
    .p.name       = "adpcm_vima",
    .p.long_name  = NULL_IF_CONFIG_SMALL("LucasArts VIMA audio"),
    .p.type       = AVMEDIA_TYPE_AUDIO,
    .p.id         = AV_CODEC_ID_ADPCM_VIMA,
    .init         = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .caps_internal = FF_CODEC_CAP_INIT_THREADSAFE,
};
