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

#include "libavutil/channel_layout.h"
#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"

#define CACHED_BITSTREAM_READER !ARCH_X86_32
#define BITSTREAM_READER_LE
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

int ff_tak_check_crc(const uint8_t *buf, unsigned int buf_size)
{
    uint32_t crc, CRC;

    if (buf_size < 4)
        return AVERROR_INVALIDDATA;
    buf_size -= 3;

    CRC = AV_RB24(buf + buf_size);
    crc = av_crc(av_crc_get_table(AV_CRC_24_IEEE), 0xCE04B7U, buf, buf_size);
    if (CRC != crc)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int tak_parse_streaminfo(TAKStreamInfo *s, GetBitContext *gb)
{
    uint64_t channel_mask = 0;
    int frame_type, i, ret;

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

    ret = tak_get_nb_samples(s->sample_rate, frame_type);
    if (ret < 0)
        return ret;
    s->frame_samples = ret;

    return 0;
}

int avpriv_tak_parse_streaminfo(TAKStreamInfo *s, const uint8_t *buf, int size)
{
    GetBitContext gb;
    int ret = init_get_bits8(&gb, buf, size);

    if (ret < 0)
        return AVERROR_INVALIDDATA;

    return tak_parse_streaminfo(s, &gb);
}

int ff_tak_decode_frame_header(void *logctx, GetBitContext *gb,
                               TAKStreamInfo *ti, int log_level_offset)
{
    if (get_bits(gb, TAK_FRAME_HEADER_SYNC_ID_BITS) != TAK_FRAME_HEADER_SYNC_ID) {
        av_log(logctx, AV_LOG_ERROR + log_level_offset, "missing sync id\n");
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
        int ret = tak_parse_streaminfo(ti, gb);
        if (ret < 0)
            return ret;

        if (get_bits(gb, 6))
            skip_bits(gb, 25);
        align_get_bits(gb);
    }

    if (ti->flags & TAK_FRAME_FLAG_HAS_METADATA)
        return AVERROR_INVALIDDATA;

    if (get_bits_left(gb) < 24)
        return AVERROR_INVALIDDATA;

    skip_bits(gb, 24);

    return 0;
}
