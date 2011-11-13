/*
 * Binary text decoder
 * eXtended BINary text (XBIN) decoder
 * iCEDraw File decoder
 * Copyright (c) 2010 Peter Ross (pross@xvid.org)
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
 * Binary text decoder
 * eXtended BINary text (XBIN) decoder
 * iCEDraw File decoder
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "cga_data.h"
#include "bintext.h"

typedef struct XbinContext {
    AVFrame frame;
    int palette[16];
    int flags;
    int font_height;
    const uint8_t *font;
    int x, y;
} XbinContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    XbinContext *s = avctx->priv_data;
    uint8_t *p;
    int i;

    avctx->pix_fmt = PIX_FMT_PAL8;
    p = avctx->extradata;
    if (p) {
        s->font_height = p[0];
        s->flags = p[1];
        p += 2;
    } else {
        s->font_height = 8;
        s->flags = 0;
    }

    if ((s->flags & BINTEXT_PALETTE)) {
        for (i = 0; i < 16; i++) {
            s->palette[i] = 0xFF000000 | (AV_RB24(p) << 2) | ((AV_RB24(p) >> 4) & 0x30303);
            p += 3;
        }
    } else {
        for (i = 0; i < 16; i++)
            s->palette[i] = 0xFF000000 | ff_cga_palette[i];
    }

    if ((s->flags & BINTEXT_FONT)) {
        s->font = p;
    } else {
        switch(s->font_height) {
        default:
            av_log(avctx, AV_LOG_WARNING, "font height %i not supported\n", s->font_height);
            s->font_height = 8;
        case 8:
            s->font = ff_cga_font;
            break;
        case 16:
            s->font = ff_vga16_font;
            break;
        }
    }

    return 0;
}

#define DEFAULT_BG_COLOR 0
static void hscroll(AVCodecContext *avctx)
{
    XbinContext *s = avctx->priv_data;
    if (s->y < avctx->height - s->font_height) {
        s->y += s->font_height;
    } else {
        memmove(s->frame.data[0], s->frame.data[0] + s->font_height*s->frame.linesize[0],
            (avctx->height - s->font_height)*s->frame.linesize[0]);
        memset(s->frame.data[0] + (avctx->height - s->font_height)*s->frame.linesize[0],
            DEFAULT_BG_COLOR, s->font_height * s->frame.linesize[0]);
    }
}

#define FONT_WIDTH 8

/**
 * Draw character to screen
 */
static void draw_char(AVCodecContext *avctx, int c, int a)
{
    XbinContext *s = avctx->priv_data;
    if (s->y > avctx->height - s->font_height)
        return;
    ff_draw_pc_font(s->frame.data[0] + s->y * s->frame.linesize[0] + s->x,
                    s->frame.linesize[0], s->font, s->font_height, c,
                    a & 0x0F, a >> 4);
    s->x += FONT_WIDTH;
    if (s->x > avctx->width - FONT_WIDTH) {
        s->x = 0;
        s->y += s->font_height;
    }
}

static int decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    XbinContext *s = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    const uint8_t *buf_end = buf+buf_size;

    s->x = s->y = 0;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID |
                            FF_BUFFER_HINTS_PRESERVE |
                            FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    s->frame.pict_type           = FF_I_TYPE;
    s->frame.palette_has_changed = 1;
    memcpy(s->frame.data[1], s->palette, 16 * 4);

    if (avctx->codec_id == CODEC_ID_XBIN) {
        while (buf + 2 < buf_end) {
            int i,c,a;
            int type  = *buf >> 6;
            int count = (*buf & 0x3F) + 1;
            buf++;
            switch (type) {
            case 0: //no compression
                for (i = 0; i < count && buf + 1 < buf_end; i++) {
                    draw_char(avctx, buf[0], buf[1]);
                    buf += 2;
                }
                break;
            case 1: //character compression
                c = *buf++;
                for (i = 0; i < count && buf < buf_end; i++)
                    draw_char(avctx, c, *buf++);
                break;
            case 2: //attribute compression
                a = *buf++;
                for (i = 0; i < count && buf < buf_end; i++)
                    draw_char(avctx, *buf++, a);
                break;
            case 3: //character/attribute compression
                c = *buf++;
                a = *buf++;
                for (i = 0; i < count && buf < buf_end; i++)
                    draw_char(avctx, c, a);
                break;
            }
        }
    } else if (avctx->codec_id == CODEC_ID_IDF) {
        while (buf + 2 < buf_end) {
            if (AV_RL16(buf) == 1) {
               int i;
               if (buf + 6 > buf_end)
                   break;
               for (i = 0; i < buf[2]; i++)
                   draw_char(avctx, buf[4], buf[5]);
               buf += 6;
            } else {
               draw_char(avctx, buf[0], buf[1]);
               buf += 2;
            }
        }
    } else {
        while (buf + 1 < buf_end) {
            draw_char(avctx, buf[0], buf[1]);
            buf += 2;
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;
    return buf_size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    XbinContext *s = avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec ff_bintext_decoder = {
    "bintext",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_BINTEXT,
    sizeof(XbinContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Binary text"),
};

AVCodec ff_xbin_decoder = {
    "xbin",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_XBIN,
    sizeof(XbinContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("eXtended BINary text"),
};

AVCodec ff_idf_decoder = {
    "idf",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_IDF,
    sizeof(XbinContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("iCEDraw text"),
};
