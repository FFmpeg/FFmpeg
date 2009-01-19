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
#include "avcodec.h"
#include "dsputil.h"
#include "bitstream.h"

//#define DEBUG
//#define DEBUG_PACKET_CONTENTS

/* Parser (mostly) copied from dvdsub.c */

#define PARSE_BUF_SIZE  (65536)


/* parser definition */
typedef struct DVBSubParseContext {
    uint8_t *packet_buf;
    int packet_start;
    int packet_index;
    int in_packet;
} DVBSubParseContext;

static av_cold int dvbsub_parse_init(AVCodecParserContext *s)
{
    DVBSubParseContext *pc = s->priv_data;
    pc->packet_buf = av_malloc(PARSE_BUF_SIZE);

    return 0;
}

static int dvbsub_parse(AVCodecParserContext *s,
                        AVCodecContext *avctx,
                        const uint8_t **poutbuf, int *poutbuf_size,
                        const uint8_t *buf, int buf_size)
{
    DVBSubParseContext *pc = s->priv_data;
    uint8_t *p, *p_end;
    int len, buf_pos = 0;

#ifdef DEBUG
    av_log(avctx, AV_LOG_INFO, "DVB parse packet pts=%"PRIx64", lpts=%"PRIx64", cpts=%"PRIx64":\n",
            s->pts, s->last_pts, s->cur_frame_pts[s->cur_frame_start_index]);
#endif

#ifdef DEBUG_PACKET_CONTENTS
    int i;

    for (i=0; i < buf_size; i++)
    {
        av_log(avctx, AV_LOG_INFO, "%02x ", buf[i]);
        if (i % 16 == 15)
            av_log(avctx, AV_LOG_INFO, "\n");
    }

    if (i % 16 != 0)
        av_log(avctx, AV_LOG_INFO, "\n");

#endif

    *poutbuf = NULL;
    *poutbuf_size = 0;

    s->fetch_timestamp = 1;

    if (s->last_pts != s->pts && s->pts != AV_NOPTS_VALUE) /* Start of a new packet */
    {
        if (pc->packet_index != pc->packet_start)
        {
#ifdef DEBUG
            av_log(avctx, AV_LOG_INFO, "Discarding %d bytes\n",
                pc->packet_index - pc->packet_start);
#endif
        }

        pc->packet_start = 0;
        pc->packet_index = 0;

        if (buf_size < 2 || buf[0] != 0x20 || buf[1] != 0x00) {
#ifdef DEBUG
            av_log(avctx, AV_LOG_INFO, "Bad packet header\n");
#endif
            return -1;
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
        return -1;

/* if not currently in a packet, discard data */
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
            if (p + 6 <= p_end)
            {
                len = AV_RB16(p + 4);

                if (p + len + 6 <= p_end)
                {
                    *poutbuf_size += len + 6;

                    p += len + 6;
                } else
                    break;
            } else
                break;
        } else if (*p == 0xff) {
            if (p + 1 < p_end)
            {
#ifdef DEBUG
                av_log(avctx, AV_LOG_INFO, "Junk at end of packet\n");
#endif
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

    if (*poutbuf_size > 0)
    {
        *poutbuf = pc->packet_buf;
        pc->packet_start = *poutbuf_size;
    }

    if (s->pts == AV_NOPTS_VALUE)
        s->pts = s->last_pts;

    return buf_size;
}

static av_cold void dvbsub_parse_close(AVCodecParserContext *s)
{
    DVBSubParseContext *pc = s->priv_data;
    av_freep(&pc->packet_buf);
}

AVCodecParser dvbsub_parser = {
    { CODEC_ID_DVB_SUBTITLE },
    sizeof(DVBSubParseContext),
    dvbsub_parse_init,
    dvbsub_parse,
    dvbsub_parse_close,
};
