/*
 * DVD navigation block parser for FFmpeg
 * Copyright (c) 2013 The FFmpeg Project
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
#include "get_bits.h"
#include "parser.h"

#define PCI_SIZE  980
#define DSI_SIZE 1018

/* parser definition */
typedef struct DVDNavParseContext {
    uint32_t     lba;
    uint8_t      buffer[PCI_SIZE+DSI_SIZE];
    int          copied;
} DVDNavParseContext;

static av_cold int dvd_nav_parse_init(AVCodecParserContext *s)
{
    DVDNavParseContext *pc = s->priv_data;

    pc->lba    = 0xFFFFFFFF;
    pc->copied = 0;
    return 0;
}

static int dvd_nav_parse(AVCodecParserContext *s,
                         AVCodecContext *avctx,
                         const uint8_t **poutbuf, int *poutbuf_size,
                         const uint8_t *buf, int buf_size)
{
    DVDNavParseContext *pc1 = s->priv_data;
    int lastPacket          = 0;
    int valid               = 0;

    s->pict_type = AV_PICTURE_TYPE_NONE;

    avctx->time_base.num = 1;
    avctx->time_base.den = 90000;

    if (buf && buf_size) {
        switch(buf[0]) {
            case 0x00:
                if (buf_size == PCI_SIZE) {
                    /* PCI */
                    uint32_t lba      = AV_RB32(&buf[0x01]);
                    uint32_t startpts = AV_RB32(&buf[0x0D]);
                    uint32_t endpts   = AV_RB32(&buf[0x11]);

                    if (endpts > startpts) {
                        pc1->lba    = lba;
                        s->pts      = (int64_t)startpts;
                        s->duration = endpts - startpts;

                        memcpy(pc1->buffer, buf, PCI_SIZE);
                        pc1->copied = PCI_SIZE;
                        valid       = 1;
                    }
                }
                break;

            case 0x01:
                if ((buf_size == DSI_SIZE) && (pc1->copied == PCI_SIZE)) {
                    /* DSI */
                    uint32_t lba = AV_RB32(&buf[0x05]);

                    if (lba == pc1->lba) {
                        memcpy(pc1->buffer + pc1->copied, buf, DSI_SIZE);
                        lastPacket  = 1;
                        valid       = 1;
                    }
                }
                break;
        }
    }

    if (!valid || lastPacket) {
        pc1->copied = 0;
        pc1->lba    = 0xFFFFFFFF;
    }

    if (lastPacket) {
        *poutbuf      = pc1->buffer;
        *poutbuf_size = sizeof(pc1->buffer);
    } else {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
    }

    return buf_size;
}

AVCodecParser ff_dvd_nav_parser = {
    .codec_ids      = { AV_CODEC_ID_DVD_NAV },
    .priv_data_size = sizeof(DVDNavParseContext),
    .parser_init    = dvd_nav_parse_init,
    .parser_parse   = dvd_nav_parse,
};
