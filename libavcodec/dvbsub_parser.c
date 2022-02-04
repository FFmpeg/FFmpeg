/*
 * DVB subtitle parser for FFmpeg
 * Copyright (c) 2005 Ian Caulfield
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

#include <inttypes.h>
#include <string.h>

#include "libavutil/intreadwrite.h"

#include "avcodec.h"

/* Parser (mostly) copied from dvdsub.c */

#define PARSE_BUF_SIZE  (65536)


/* parser definition */
typedef struct DVBSubParseContext {
    int packet_start;
    int packet_index;
    int in_packet;
    uint8_t packet_buf[PARSE_BUF_SIZE];
} DVBSubParseContext;

static int dvbsub_parse(AVCodecParserContext *s,
                        AVCodecContext *avctx,
                        const uint8_t **poutbuf, int *poutbuf_size,
                        const uint8_t *buf, int buf_size)
{
    DVBSubParseContext *pc = s->priv_data;
    uint8_t *p, *p_end;
    int i, len, buf_pos = 0;
    int out_size = 0;

    ff_dlog(avctx, "DVB parse packet pts=%"PRIx64", lpts=%"PRIx64", cpts=%"PRIx64":\n",
            s->pts, s->last_pts, s->cur_frame_pts[s->cur_frame_start_index]);

    for (i=0; i < buf_size; i++)
    {
        ff_dlog(avctx, "%02x ", buf[i]);
        if (i % 16 == 15)
            ff_dlog(avctx, "\n");
    }

    if (i % 16 != 0)
        ff_dlog(avctx, "\n");

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    s->fetch_timestamp = 1;

    if (s->last_pts != s->pts && s->pts != AV_NOPTS_VALUE) /* Start of a new packet */
    {
        if (pc->packet_index != pc->packet_start)
        {
            ff_dlog(avctx, "Discarding %d bytes\n",
                    pc->packet_index - pc->packet_start);
        }

        pc->packet_start = 0;
        pc->packet_index = 0;

        if (buf_size < 2 || buf[0] != 0x20 || buf[1] != 0x00) {
            ff_dlog(avctx, "Bad packet header\n");
            return buf_size;
        }

        buf_pos = 2;

        pc->in_packet = 1;
    } else {
        if (pc->packet_start != 0)
        {
            if (pc->packet_index != pc->packet_start)
            {
                memmove(pc->packet_buf, pc->packet_buf + pc->packet_start,
                            pc->packet_index - pc->packet_start);

                pc->packet_index -= pc->packet_start;
                pc->packet_start = 0;
            } else {
                pc->packet_start = 0;
                pc->packet_index = 0;
            }
        }
    }

    if (buf_size - buf_pos + pc->packet_index > PARSE_BUF_SIZE)
        return buf_size;

/* if not currently in a packet, pass data */
    if (pc->in_packet == 0)
        return buf_size;

    memcpy(pc->packet_buf + pc->packet_index, buf + buf_pos, buf_size - buf_pos);
    pc->packet_index += buf_size - buf_pos;

    p = pc->packet_buf;
    p_end = pc->packet_buf + pc->packet_index;

    while (p < p_end)
    {
        if (*p == 0x0f)
        {
            if (6 <= p_end - p)
            {
                len = AV_RB16(p + 4);

                if (len + 6 <= p_end - p)
                {
                    out_size += len + 6;

                    p += len + 6;
                } else
                    break;
            } else
                break;
        } else if (*p == 0xff) {
            if (1 < p_end - p)
            {
                ff_dlog(avctx, "Junk at end of packet\n");
            }
            pc->packet_index = p - pc->packet_buf;
            pc->in_packet = 0;
            break;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Junk in packet\n");

            pc->packet_index = p - pc->packet_buf;
            pc->in_packet = 0;
            break;
        }
    }

    if (out_size > 0)
    {
        *poutbuf = pc->packet_buf;
        *poutbuf_size = out_size;
        pc->packet_start = *poutbuf_size;
    }

    if (s->pts == AV_NOPTS_VALUE)
        s->pts = s->last_pts;

    return buf_size;
}

const AVCodecParser ff_dvbsub_parser = {
    .codec_ids      = { AV_CODEC_ID_DVB_SUBTITLE },
    .priv_data_size = sizeof(DVBSubParseContext),
    .parser_parse   = dvbsub_parse,
};
