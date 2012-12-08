/*
 * TAK common code
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

#include "libavutil/bswap.h"
#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"
#include "tak.h"

static const int64_t tak_channel_layouts[] = {
    0,
    AV_CH_FRONT_LEFT,
    AV_CH_FRONT_RIGHT,
    AV_CH_FRONT_CENTER,
    AV_CH_LOW_FREQUENCY,
    AV_CH_BACK_LEFT,
    AV_CH_BACK_RIGHT,
    AV_CH_FRONT_LEFT_OF_CENTER,
    AV_CH_FRONT_RIGHT_OF_CENTER,
    AV_CH_BACK_CENTER,
    AV_CH_SIDE_LEFT,
    AV_CH_SIDE_RIGHT,
    AV_CH_TOP_CENTER,
    AV_CH_TOP_FRONT_LEFT,
    AV_CH_TOP_FRONT_CENTER,
    AV_CH_TOP_FRONT_RIGHT,
    AV_CH_TOP_BACK_LEFT,
    AV_CH_TOP_BACK_CENTER,
    AV_CH_TOP_BACK_RIGHT,
};

static const uint16_t frame_duration_type_quants[] = {
    3, 4, 6, 8, 4096, 8192, 16384, 512, 1024, 2048,
};

static int tak_get_nb_samples(int sample_rate, enum TAKFrameSizeType type)
{
    int nb_samples, max_nb_samples;

    if (type <= TAK_FST_250ms) {
        nb_samples     = sample_rate * frame_duration_type_quants[type] >>
                         TAK_FRAME_DURATION_QUANT_SHIFT;
        max_nb_samples = 16384;
    } else if (type < FF_ARRAY_ELEMS(frame_duration_type_quants)) {
        nb_samples     = frame_duration_type_quants[type];
        max_nb_samples = sample_rate *
                         frame_duration_type_quants[TAK_FST_250ms] >>
                         TAK_FRAME_DURATION_QUANT_SHIFT;
    } else {
        return AVERROR_INVALIDDATA;
    }

    if (nb_samples <= 0 || nb_samples > max_nb_samples)
        return AVERROR_INVALIDDATA;

    return nb_samples;
}

static int crc_init = 0;
#if CONFIG_SMALL
#define CRC_TABLE_SIZE 257
#else
#define CRC_TABLE_SIZE 1024
#endif
static AVCRC crc_24[CRC_TABLE_SIZE];

av_cold void ff_tak_init_crc(void)
{
    if (!crc_init) {
        av_crc_init(crc_24, 0, 24, 0x864CFBU, sizeof(crc_24));
        crc_init = 1;
    }
}

int ff_tak_check_crc(const uint8_t *buf, unsigned int buf_size)
{
    uint32_t crc, CRC;

    if (buf_size < 4)
        return AVERROR_INVALIDDATA;
    buf_size -= 3;

    CRC = av_bswap32(AV_RL24(buf + buf_size)) >> 8;
    crc = av_crc(crc_24, 0xCE04B7U, buf, buf_size);
    if (CRC != crc)
        return AVERROR_INVALIDDATA;

    return 0;
}

void avpriv_tak_parse_streaminfo(GetBitContext *gb, TAKStreamInfo *s)
{
    uint64_t channel_mask = 0;
    int frame_type, i;

    s->codec = get_bits(gb, TAK_ENCODER_CODEC_BITS);
    skip_bits(gb, TAK_ENCODER_PROFILE_BITS);

    frame_type = get_bits(gb, TAK_SIZE_FRAME_DURATION_BITS);
    s->samples = get_bits64(gb, TAK_SIZE_SAMPLES_NUM_BITS);

    s->data_type   = get_bits(gb, TAK_FORMAT_DATA_TYPE_BITS);
    s->sample_rate = get_bits(gb, TAK_FORMAT_SAMPLE_RATE_BITS) +
                     TAK_SAMPLE_RATE_MIN;
    s->bps         = get_bits(gb, TAK_FORMAT_BPS_BITS) +
                     TAK_BPS_MIN;
    s->channels    = get_bits(gb, TAK_FORMAT_CHANNEL_BITS) +
                     TAK_CHANNELS_MIN;

    if (get_bits1(gb)) {
        skip_bits(gb, TAK_FORMAT_VALID_BITS);
        if (get_bits1(gb)) {
            for (i = 0; i < s->channels; i++) {
                int value = get_bits(gb, TAK_FORMAT_CH_LAYOUT_BITS);

                if (value < FF_ARRAY_ELEMS(tak_channel_layouts))
                    channel_mask |= tak_channel_layouts[value];
            }
        }
    }

    s->ch_layout     = channel_mask;
    s->frame_samples = tak_get_nb_samples(s->sample_rate, frame_type);
}

int ff_tak_decode_frame_header(AVCodecContext *avctx, GetBitContext *gb,
                               TAKStreamInfo *ti, int log_level_offset)
{
    if (get_bits(gb, TAK_FRAME_HEADER_SYNC_ID_BITS) != TAK_FRAME_HEADER_SYNC_ID) {
        av_log(avctx, AV_LOG_ERROR + log_level_offset, "missing sync id\n");
        return AVERROR_INVALIDDATA;
    }

    ti->flags     = get_bits(gb, TAK_FRAME_HEADER_FLAGS_BITS);
    ti->frame_num = get_bits(gb, TAK_FRAME_HEADER_NO_BITS);

    if (ti->flags & TAK_FRAME_FLAG_IS_LAST) {
        ti->last_frame_samples = get_bits(gb, TAK_FRAME_HEADER_SAMPLE_COUNT_BITS) + 1;
        skip_bits(gb, 2);
    } else {
        ti->last_frame_samples = 0;
    }

    if (ti->flags & TAK_FRAME_FLAG_HAS_INFO) {
        avpriv_tak_parse_streaminfo(gb, ti);

        if (get_bits(gb, 6))
            skip_bits(gb, 25);
        align_get_bits(gb);
    }

    if (ti->flags & TAK_FRAME_FLAG_HAS_METADATA)
        return AVERROR_INVALIDDATA;

    skip_bits(gb, 24);

    return 0;
}
