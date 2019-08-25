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

#include "libavutil/internal.h"
#include "get_bits.h"
#include "parser.h"
#include "mlp_parse.h"
#include "mlp.h"

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

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
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
    }

    sync_present = buf_size >= 8 && (AV_RB32(buf + 4) & 0xfffffffe) == 0xf8726fba;

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
        avctx->frame_size =
        s->duration = mh.access_unit_size;

        if(!avctx->channels || !avctx->channel_layout) {
        if (mh.stream_type == 0xbb) {
            /* MLP stream */
            avctx->channels       = mh.channels_mlp;
            avctx->channel_layout = mh.channel_layout_mlp;
        } else { /* mh.stream_type == 0xba */
            /* TrueHD stream */
            if (!mh.channels_thd_stream2) {
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
