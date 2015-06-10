/*
 * MLP parser
 * Copyright (c) 2007 Ian Caulfield
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
 * MLP parser
 */

#include <stdint.h>

#include "libavutil/channel_layout.h"
#include "libavutil/crc.h"
#include "libavutil/internal.h"
#include "get_bits.h"
#include "parser.h"
#include "mlp_parser.h"
#include "mlp.h"

static const uint8_t mlp_quants[16] = {
    16, 20, 24, 0, 0, 0, 0, 0,
     0,  0,  0, 0, 0, 0, 0, 0,
};

static const uint8_t mlp_channels[32] = {
    1, 2, 3, 4, 3, 4, 5, 3, 4, 5, 4, 5, 6, 4, 5, 4,
    5, 6, 5, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

const uint64_t ff_mlp_layout[32] = {
    AV_CH_LAYOUT_MONO,
    AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_2_1,
    AV_CH_LAYOUT_QUAD,
    AV_CH_LAYOUT_STEREO|AV_CH_LOW_FREQUENCY,
    AV_CH_LAYOUT_2_1|AV_CH_LOW_FREQUENCY,
    AV_CH_LAYOUT_QUAD|AV_CH_LOW_FREQUENCY,
    AV_CH_LAYOUT_SURROUND,
    AV_CH_LAYOUT_4POINT0,
    AV_CH_LAYOUT_5POINT0_BACK,
    AV_CH_LAYOUT_SURROUND|AV_CH_LOW_FREQUENCY,
    AV_CH_LAYOUT_4POINT0|AV_CH_LOW_FREQUENCY,
    AV_CH_LAYOUT_5POINT1_BACK,
    AV_CH_LAYOUT_4POINT0,
    AV_CH_LAYOUT_5POINT0_BACK,
    AV_CH_LAYOUT_SURROUND|AV_CH_LOW_FREQUENCY,
    AV_CH_LAYOUT_4POINT0|AV_CH_LOW_FREQUENCY,
    AV_CH_LAYOUT_5POINT1_BACK,
    AV_CH_LAYOUT_QUAD|AV_CH_LOW_FREQUENCY,
    AV_CH_LAYOUT_5POINT0_BACK,
    AV_CH_LAYOUT_5POINT1_BACK,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t thd_chancount[13] = {
//  LR    C   LFE  LRs LRvh  LRc LRrs  Cs   Ts  LRsd  LRw  Cvh  LFE2
     2,   1,   1,   2,   2,   2,   2,   1,   1,   2,   2,   1,   1
};

static const uint64_t thd_layout[13] = {
    AV_CH_FRONT_LEFT|AV_CH_FRONT_RIGHT,                     // LR
    AV_CH_FRONT_CENTER,                                     // C
    AV_CH_LOW_FREQUENCY,                                    // LFE
    AV_CH_SIDE_LEFT|AV_CH_SIDE_RIGHT,                       // LRs
    AV_CH_TOP_FRONT_LEFT|AV_CH_TOP_FRONT_RIGHT,             // LRvh
    AV_CH_FRONT_LEFT_OF_CENTER|AV_CH_FRONT_RIGHT_OF_CENTER, // LRc
    AV_CH_BACK_LEFT|AV_CH_BACK_RIGHT,                       // LRrs
    AV_CH_BACK_CENTER,                                      // Cs
    AV_CH_TOP_CENTER,                                       // Ts
    AV_CH_SURROUND_DIRECT_LEFT|AV_CH_SURROUND_DIRECT_RIGHT, // LRsd
    AV_CH_WIDE_LEFT|AV_CH_WIDE_RIGHT,                       // LRw
    AV_CH_TOP_FRONT_CENTER,                                 // Cvh
    AV_CH_LOW_FREQUENCY_2,                                  // LFE2
};

static int mlp_samplerate(int in)
{
    if (in == 0xF)
        return 0;

    return (in & 8 ? 44100 : 48000) << (in & 7) ;
}

static int truehd_channels(int chanmap)
{
    int channels = 0, i;

    for (i = 0; i < 13; i++)
        channels += thd_chancount[i] * ((chanmap >> i) & 1);

    return channels;
}

uint64_t ff_truehd_layout(int chanmap)
{
    int i;
    uint64_t layout = 0;

    for (i = 0; i < 13; i++)
        layout |= thd_layout[i] * ((chanmap >> i) & 1);

    return layout;
}

static int ff_mlp_get_major_sync_size(const uint8_t * buf, int bufsize)
{
    int has_extension, extensions = 0;
    int size = 28;
    if (bufsize < 28)
        return -1;

    if (AV_RB32(buf) == 0xf8726fba) {
        has_extension = buf[25] & 1;
        if (has_extension) {
            extensions = buf[26] >> 4;
            size += 2 + extensions * 2;
        }
    }
    return size;
}

/** Read a major sync info header - contains high level information about
 *  the stream - sample rate, channel arrangement etc. Most of this
 *  information is not actually necessary for decoding, only for playback.
 *  gb must be a freshly initialized GetBitContext with no bits read.
 */

int ff_mlp_read_major_sync(void *log, MLPHeaderInfo *mh, GetBitContext *gb)
{
    int ratebits, channel_arrangement, header_size;
    uint16_t checksum;

    av_assert1(get_bits_count(gb) == 0);

    header_size = ff_mlp_get_major_sync_size(gb->buffer, gb->size_in_bits >> 3);
    if (header_size < 0 || gb->size_in_bits < header_size << 3) {
        av_log(log, AV_LOG_ERROR, "packet too short, unable to read major sync\n");
        return -1;
    }

    checksum = ff_mlp_checksum16(gb->buffer, header_size - 2);
    if (checksum != AV_RL16(gb->buffer+header_size-2)) {
        av_log(log, AV_LOG_ERROR, "major sync info header checksum error\n");
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_long(gb, 24) != 0xf8726f) /* Sync words */
        return AVERROR_INVALIDDATA;

    mh->stream_type = get_bits(gb, 8);
    mh->header_size = header_size;

    if (mh->stream_type == 0xbb) {
        mh->group1_bits = mlp_quants[get_bits(gb, 4)];
        mh->group2_bits = mlp_quants[get_bits(gb, 4)];

        ratebits = get_bits(gb, 4);
        mh->group1_samplerate = mlp_samplerate(ratebits);
        mh->group2_samplerate = mlp_samplerate(get_bits(gb, 4));

        skip_bits(gb, 11);

        mh->channel_arrangement=
        channel_arrangement    = get_bits(gb, 5);
        mh->channels_mlp       = mlp_channels[channel_arrangement];
        mh->channel_layout_mlp = ff_mlp_layout[channel_arrangement];
    } else if (mh->stream_type == 0xba) {
        mh->group1_bits = 24; // TODO: Is this information actually conveyed anywhere?
        mh->group2_bits = 0;

        ratebits = get_bits(gb, 4);
        mh->group1_samplerate = mlp_samplerate(ratebits);
        mh->group2_samplerate = 0;

        skip_bits(gb, 4);

        mh->channel_modifier_thd_stream0 = get_bits(gb, 2);
        mh->channel_modifier_thd_stream1 = get_bits(gb, 2);

        mh->channel_arrangement=
        channel_arrangement            = get_bits(gb, 5);
        mh->channels_thd_stream1       = truehd_channels(channel_arrangement);
        mh->channel_layout_thd_stream1 = ff_truehd_layout(channel_arrangement);

        mh->channel_modifier_thd_stream2 = get_bits(gb, 2);

        channel_arrangement            = get_bits(gb, 13);
        mh->channels_thd_stream2       = truehd_channels(channel_arrangement);
        mh->channel_layout_thd_stream2 = ff_truehd_layout(channel_arrangement);
    } else
        return AVERROR_INVALIDDATA;

    mh->access_unit_size = 40 << (ratebits & 7);
    mh->access_unit_size_pow2 = 64 << (ratebits & 7);

    skip_bits_long(gb, 48);

    mh->is_vbr = get_bits1(gb);

    mh->peak_bitrate = (get_bits(gb, 15) * mh->group1_samplerate + 8) >> 4;

    mh->num_substreams = get_bits(gb, 4);

    skip_bits_long(gb, 4 + (header_size - 17) * 8);

    return 0;
}

typedef struct MLPParseContext
{
    ParseContext pc;

    int bytes_left;

    int in_sync;

    int num_substreams;
} MLPParseContext;

static av_cold int mlp_init(AVCodecParserContext *s)
{
    ff_mlp_init_crc();
    return 0;
}

static int mlp_parse(AVCodecParserContext *s,
                     AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    MLPParseContext *mp = s->priv_data;
    int sync_present;
    uint8_t parity_bits;
    int next;
    int ret;
    int i, p = 0;

    *poutbuf_size = 0;
    if (buf_size == 0)
        return 0;

    if (!mp->in_sync) {
        // Not in sync - find a major sync header

        for (i = 0; i < buf_size; i++) {
            mp->pc.state = (mp->pc.state << 8) | buf[i];
            if ((mp->pc.state & 0xfffffffe) == 0xf8726fba &&
                // ignore if we do not have the data for the start of header
                mp->pc.index + i >= 7) {
                mp->in_sync = 1;
                mp->bytes_left = 0;
                break;
            }
        }

        if (!mp->in_sync) {
            if (ff_combine_frame(&mp->pc, END_NOT_FOUND, &buf, &buf_size) != -1)
                av_log(avctx, AV_LOG_WARNING, "ff_combine_frame failed\n");
            return buf_size;
        }

        if ((ret = ff_combine_frame(&mp->pc, i - 7, &buf, &buf_size)) < 0) {
            av_log(avctx, AV_LOG_WARNING, "ff_combine_frame failed\n");
            return ret;
        }

        return i - 7;
    }

    if (mp->bytes_left == 0) {
        // Find length of this packet

        /* Copy overread bytes from last frame into buffer. */
        for(; mp->pc.overread>0; mp->pc.overread--) {
            mp->pc.buffer[mp->pc.index++]= mp->pc.buffer[mp->pc.overread_index++];
        }

        if (mp->pc.index + buf_size < 2) {
            if (ff_combine_frame(&mp->pc, END_NOT_FOUND, &buf, &buf_size) != -1)
                av_log(avctx, AV_LOG_WARNING, "ff_combine_frame failed\n");
            return buf_size;
        }

        mp->bytes_left = ((mp->pc.index > 0 ? mp->pc.buffer[0] : buf[0]) << 8)
                       |  (mp->pc.index > 1 ? mp->pc.buffer[1] : buf[1-mp->pc.index]);
        mp->bytes_left = (mp->bytes_left & 0xfff) * 2;
        if (mp->bytes_left <= 0) { // prevent infinite loop
            goto lost_sync;
        }
        mp->bytes_left -= mp->pc.index;
    }

    next = (mp->bytes_left > buf_size) ? END_NOT_FOUND : mp->bytes_left;

    if (ff_combine_frame(&mp->pc, next, &buf, &buf_size) < 0) {
        mp->bytes_left -= buf_size;
        return buf_size;
    }

    mp->bytes_left = 0;

    sync_present = (AV_RB32(buf + 4) & 0xfffffffe) == 0xf8726fba;

    if (!sync_present) {
        /* The first nibble of a frame is a parity check of the 4-byte
         * access unit header and all the 2- or 4-byte substream headers. */
        // Only check when this isn't a sync frame - syncs have a checksum.

        parity_bits = 0;
        for (i = -1; i < mp->num_substreams; i++) {
            parity_bits ^= buf[p++];
            parity_bits ^= buf[p++];

            if (i < 0 || buf[p-2] & 0x80) {
                parity_bits ^= buf[p++];
                parity_bits ^= buf[p++];
            }
        }

        if ((((parity_bits >> 4) ^ parity_bits) & 0xF) != 0xF) {
            av_log(avctx, AV_LOG_INFO, "mlpparse: Parity check failed.\n");
            goto lost_sync;
        }
    } else {
        GetBitContext gb;
        MLPHeaderInfo mh;

        init_get_bits(&gb, buf + 4, (buf_size - 4) << 3);
        if (ff_mlp_read_major_sync(avctx, &mh, &gb) < 0)
            goto lost_sync;

        avctx->bits_per_raw_sample = mh.group1_bits;
        if (avctx->bits_per_raw_sample > 16)
            avctx->sample_fmt = AV_SAMPLE_FMT_S32;
        else
            avctx->sample_fmt = AV_SAMPLE_FMT_S16;
        avctx->sample_rate = mh.group1_samplerate;
        s->duration = mh.access_unit_size;

        if(!avctx->channels || !avctx->channel_layout) {
        if (mh.stream_type == 0xbb) {
            /* MLP stream */
#if FF_API_REQUEST_CHANNELS
FF_DISABLE_DEPRECATION_WARNINGS
            if (avctx->request_channels > 0 && avctx->request_channels <= 2 &&
                mh.num_substreams > 1) {
                avctx->channels       = 2;
                avctx->channel_layout = AV_CH_LAYOUT_STEREO;
FF_ENABLE_DEPRECATION_WARNINGS
            } else
#endif
            if (avctx->request_channel_layout &&
                (avctx->request_channel_layout & AV_CH_LAYOUT_STEREO) ==
                avctx->request_channel_layout &&
                mh.num_substreams > 1) {
                avctx->channels       = 2;
                avctx->channel_layout = AV_CH_LAYOUT_STEREO;
            } else {
                avctx->channels       = mh.channels_mlp;
                avctx->channel_layout = mh.channel_layout_mlp;
            }
        } else { /* mh.stream_type == 0xba */
            /* TrueHD stream */
#if FF_API_REQUEST_CHANNELS
FF_DISABLE_DEPRECATION_WARNINGS
            if (avctx->request_channels > 0 && avctx->request_channels <= 2 &&
                mh.num_substreams > 1) {
                avctx->channels       = 2;
                avctx->channel_layout = AV_CH_LAYOUT_STEREO;
            } else if (avctx->request_channels > 0 &&
                       avctx->request_channels <= mh.channels_thd_stream1) {
                avctx->channels       = mh.channels_thd_stream1;
                avctx->channel_layout = mh.channel_layout_thd_stream1;
FF_ENABLE_DEPRECATION_WARNINGS
            } else
#endif
                if (avctx->request_channel_layout &&
                    (avctx->request_channel_layout & AV_CH_LAYOUT_STEREO) ==
                    avctx->request_channel_layout &&
                    mh.num_substreams > 1) {
                avctx->channels       = 2;
                avctx->channel_layout = AV_CH_LAYOUT_STEREO;
            } else if (!mh.channels_thd_stream2 ||
                       (avctx->request_channel_layout &&
                        (avctx->request_channel_layout & mh.channel_layout_thd_stream1) ==
                        avctx->request_channel_layout)) {
                avctx->channels       = mh.channels_thd_stream1;
                avctx->channel_layout = mh.channel_layout_thd_stream1;
            } else {
                avctx->channels       = mh.channels_thd_stream2;
                avctx->channel_layout = mh.channel_layout_thd_stream2;
            }
        }
        }

        if (!mh.is_vbr) /* Stream is CBR */
            avctx->bit_rate = mh.peak_bitrate;

        mp->num_substreams = mh.num_substreams;
    }

    *poutbuf = buf;
    *poutbuf_size = buf_size;

    return next;

lost_sync:
    mp->in_sync = 0;
    return 1;
}

AVCodecParser ff_mlp_parser = {
    .codec_ids      = { AV_CODEC_ID_MLP, AV_CODEC_ID_TRUEHD },
    .priv_data_size = sizeof(MLPParseContext),
    .parser_init    = mlp_init,
    .parser_parse   = mlp_parse,
    .parser_close   = ff_parse_close,
};
