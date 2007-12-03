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
 * @file mlp_parser.c
 * MLP parser
 */

#include "bitstream.h"
#include "parser.h"
#include "crc.h"
#include "mlp_parser.h"

static const uint8_t mlp_quants[16] = {
    16, 20, 24, 0, 0, 0, 0, 0,
     0,  0,  0, 0, 0, 0, 0, 0,
};

static const uint8_t mlp_channels[32] = {
    1, 2, 3, 4, 3, 4, 5, 3, 4, 5, 4, 5, 6, 4, 5, 4,
    5, 6, 5, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const uint8_t thd_chancount[13] = {
//  LR    C   LFE  LRs LRvh  LRc LRrs  Cs   Ts  LRsd  LRw  Cvh  LFE2
     2,   1,   1,   2,   2,   2,   2,   1,   1,   2,   2,   1,   1
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

static int crc_init = 0;
static AVCRC crc_2D[1024];

/** MLP uses checksums that seem to be based on the standard CRC algorithm,
 *  but not (in implementation terms, the table lookup and XOR are reversed).
 *  We can implement this behavior using a standard av_crc on all but the
 *  last element, then XOR that with the last element.
 */

static uint16_t mlp_checksum16(const uint8_t *buf, unsigned int buf_size)
{
    uint16_t crc;

    if (!crc_init) {
        av_crc_init(crc_2D, 0, 16, 0x002D, sizeof(crc_2D));
        crc_init = 1;
    }

    crc = av_crc(crc_2D, 0, buf, buf_size - 2);
    crc ^= AV_RL16(buf + buf_size - 2);
    return crc;
}

/** Read a major sync info header - contains high level information about
 *  the stream - sample rate, channel arrangement etc. Most of this
 *  information is not actually necessary for decoding, only for playback.
 */

int ff_mlp_read_major_sync(void *log, MLPHeaderInfo *mh, const uint8_t *buf,
                           unsigned int buf_size)
{
    GetBitContext gb;
    int ratebits;
    uint16_t checksum;

    if (buf_size < 28) {
        av_log(log, AV_LOG_ERROR, "Packet too short, unable to read major sync\n");
        return -1;
    }

    checksum = mlp_checksum16(buf, 26);
    if (checksum != AV_RL16(buf+26)) {
        av_log(log, AV_LOG_ERROR, "Major sync info header checksum error\n");
        return -1;
    }

    init_get_bits(&gb, buf, buf_size * 8);

    if (get_bits_long(&gb, 24) != 0xf8726f) /* Sync words */
        return -1;

    mh->stream_type = get_bits(&gb, 8);

    if (mh->stream_type == 0xbb) {
        mh->group1_bits = mlp_quants[get_bits(&gb, 4)];
        mh->group2_bits = mlp_quants[get_bits(&gb, 4)];

        ratebits = get_bits(&gb, 4);
        mh->group1_samplerate = mlp_samplerate(ratebits);
        mh->group2_samplerate = mlp_samplerate(get_bits(&gb, 4));

        skip_bits(&gb, 11);

        mh->channels_mlp = get_bits(&gb, 5);
    } else if (mh->stream_type == 0xba) {
        mh->group1_bits = 24; // TODO: Is this information actually conveyed anywhere?
        mh->group2_bits = 0;

        ratebits = get_bits(&gb, 4);
        mh->group1_samplerate = mlp_samplerate(ratebits);
        mh->group2_samplerate = 0;

        skip_bits(&gb, 8);

        mh->channels_thd_stream1 = get_bits(&gb, 5);

        skip_bits(&gb, 2);

        mh->channels_thd_stream2 = get_bits(&gb, 13);
    } else
        return -1;

    mh->access_unit_size = 40 << (ratebits & 7);
    mh->access_unit_size_pow2 = 64 << (ratebits & 7);

    skip_bits_long(&gb, 48);

    mh->is_vbr = get_bits1(&gb);

    mh->peak_bitrate = (get_bits(&gb, 15) * mh->group1_samplerate + 8) >> 4;

    mh->num_substreams = get_bits(&gb, 4);

    skip_bits_long(&gb, 4 + 11 * 8);

    return 0;
}

typedef struct MLPParseContext
{
    ParseContext pc;

    int bytes_left;

    int in_sync;

    int num_substreams;
} MLPParseContext;

static int mlp_parse(AVCodecParserContext *s,
                     AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    MLPParseContext *mp = s->priv_data;
    int sync_present;
    uint8_t parity_bits;
    int next;
    int i, p = 0;

    *poutbuf_size = 0;
    if (buf_size == 0)
        return 0;

    if (!mp->in_sync) {
        // Not in sync - find a major sync header

        for (i = 0; i < buf_size; i++) {
            mp->pc.state = (mp->pc.state << 8) | buf[i];
            if ((mp->pc.state & 0xfffffffe) == 0xf8726fba) {
                mp->in_sync = 1;
                mp->bytes_left = 0;
                break;
            }
        }

        if (!mp->in_sync) {
            ff_combine_frame(&mp->pc, END_NOT_FOUND, &buf, &buf_size);
            return buf_size;
        }

        ff_combine_frame(&mp->pc, i - 7, &buf, &buf_size);

        return i - 7;
    }

    if (mp->bytes_left == 0) {
        // Find length of this packet

        /* Copy overread bytes from last frame into buffer. */
        for(; mp->pc.overread>0; mp->pc.overread--) {
            mp->pc.buffer[mp->pc.index++]= mp->pc.buffer[mp->pc.overread_index++];
        }

        if (mp->pc.index + buf_size < 2) {
            ff_combine_frame(&mp->pc, END_NOT_FOUND, &buf, &buf_size);
            return buf_size;
        }

        mp->bytes_left = ((mp->pc.index > 0 ? mp->pc.buffer[0] : buf[0]) << 8)
                       |  (mp->pc.index > 1 ? mp->pc.buffer[1] : buf[1-mp->pc.index]);
        mp->bytes_left = (mp->bytes_left & 0xfff) * 2;
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
        // First nibble of a frame is a parity check of the first few nibbles.
        // Only check when this isn't a sync frame - syncs have a checksum.

        parity_bits = 0;
        for (i = 0; i <= mp->num_substreams; i++) {
            parity_bits ^= buf[p++];
            parity_bits ^= buf[p++];

            if (i == 0 || buf[p-2] & 0x80) {
                parity_bits ^= buf[p++];
                parity_bits ^= buf[p++];
            }
        }

        if ((((parity_bits >> 4) ^ parity_bits) & 0xF) != 0xF) {
            av_log(avctx, AV_LOG_INFO, "mlpparse: Parity check failed.\n");
            goto lost_sync;
        }
    } else {
        MLPHeaderInfo mh;

        if (ff_mlp_read_major_sync(avctx, &mh, buf + 4, buf_size - 4) < 0)
            goto lost_sync;

#ifdef CONFIG_AUDIO_NONSHORT
        avctx->bits_per_sample = mh.group1_bits;
        if (avctx->bits_per_sample > 16)
            avctx->sample_fmt = SAMPLE_FMT_S32;
#endif
        avctx->sample_rate = mh.group1_samplerate;
        avctx->frame_size = mh.access_unit_size;

        if (mh.stream_type == 0xbb) {
            /* MLP stream */
            avctx->channels = mlp_channels[mh.channels_mlp];
        } else { /* mh.stream_type == 0xba */
            /* TrueHD stream */
            if (mh.channels_thd_stream2)
                avctx->channels = truehd_channels(mh.channels_thd_stream2);
            else
                avctx->channels = truehd_channels(mh.channels_thd_stream1);
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
    return -1;
}

AVCodecParser mlp_parser = {
    { CODEC_ID_MLP },
    sizeof(MLPParseContext),
    NULL,
    mlp_parse,
    NULL,
};
