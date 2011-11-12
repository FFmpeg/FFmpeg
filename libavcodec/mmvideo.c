/*
 * American Laser Games MM Video Decoder
 * Copyright (c) 2006,2008 Peter Ross
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
 * American Laser Games MM Video Decoder
 * by Peter Ross (pross@xvid.org)
 *
 * The MM format was used by IBM-PC ports of ALG's "arcade shooter" games,
 * including Mad Dog McCree and Crime Patrol.
 *
 * Technical details here:
 *  http://wiki.multimedia.cx/index.php?title=American_Laser_Games_MM
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

#define MM_PREAMBLE_SIZE    6

#define MM_TYPE_INTER       0x5
#define MM_TYPE_INTRA       0x8
#define MM_TYPE_INTRA_HH    0xc
#define MM_TYPE_INTER_HH    0xd
#define MM_TYPE_INTRA_HHV   0xe
#define MM_TYPE_INTER_HHV   0xf
#define MM_TYPE_PALETTE     0x31

typedef struct MmContext {
    AVCodecContext *avctx;
    AVFrame frame;
    int palette[AVPALETTE_COUNT];
} MmContext;

static av_cold int mm_decode_init(AVCodecContext *avctx)
{
    MmContext *s = avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = PIX_FMT_PAL8;

    avcodec_get_frame_defaults(&s->frame);
    s->frame.reference = 3;

    return 0;
}

static void mm_decode_pal(MmContext *s, const uint8_t *buf, const uint8_t *buf_end)
{
    int i;
    buf += 4;
    for (i=0; i<128 && buf+2<buf_end; i++) {
        s->palette[i] = 0xFF << 24 | AV_RB24(buf);
        s->palette[i+128] = s->palette[i]<<2;
        buf += 3;
    }
}

/**
 * @param half_horiz Half horizontal resolution (0 or 1)
 * @param half_vert Half vertical resolution (0 or 1)
 */
static void mm_decode_intra(MmContext * s, int half_horiz, int half_vert, const uint8_t *buf, int buf_size)
{
    int i, x, y;
    i=0; x=0; y=0;

    while(i<buf_size) {
        int run_length, color;

        if (y >= s->avctx->height)
            return;

        if (buf[i] & 0x80) {
            run_length = 1;
            color = buf[i];
            i++;
        }else{
            run_length = (buf[i] & 0x7f) + 2;
            color = buf[i+1];
            i+=2;
        }

        if (half_horiz)
            run_length *=2;

        if (color) {
            memset(s->frame.data[0] + y*s->frame.linesize[0] + x, color, run_length);
            if (half_vert)
                memset(s->frame.data[0] + (y+1)*s->frame.linesize[0] + x, color, run_length);
        }
        x+= run_length;

        if (x >= s->avctx->width) {
            x=0;
            y += 1 + half_vert;
        }
    }
}

/*
 * @param half_horiz Half horizontal resolution (0 or 1)
 * @param half_vert Half vertical resolution (0 or 1)
 */
static void mm_decode_inter(MmContext * s, int half_horiz, int half_vert, const uint8_t *buf, int buf_size)
{
    const int data_ptr = 2 + AV_RL16(&buf[0]);
    int d, r, y;
    d = data_ptr; r = 2; y = 0;

    while(r < data_ptr) {
        int i, j;
        int length = buf[r] & 0x7f;
        int x = buf[r+1] + ((buf[r] & 0x80) << 1);
        r += 2;

        if (length==0) {
            y += x;
            continue;
        }

        if (y + half_vert >= s->avctx->height)
            return;

        for(i=0; i<length; i++) {
            for(j=0; j<8; j++) {
                int replace = (buf[r+i] >> (7-j)) & 1;
                if (replace) {
                    int color = buf[d];
                    s->frame.data[0][y*s->frame.linesize[0] + x] = color;
                    if (half_horiz)
                        s->frame.data[0][y*s->frame.linesize[0] + x + 1] = color;
                    if (half_vert) {
                        s->frame.data[0][(y+1)*s->frame.linesize[0] + x] = color;
                        if (half_horiz)
                            s->frame.data[0][(y+1)*s->frame.linesize[0] + x + 1] = color;
                    }
                    d++;
                }
                x += 1 + half_horiz;
            }
        }

        r += length;
        y += 1 + half_vert;
    }
}

static int mm_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MmContext *s = avctx->priv_data;
    const uint8_t *buf_end = buf+buf_size;
    int type;

    type = AV_RL16(&buf[0]);
    buf += MM_PREAMBLE_SIZE;
    buf_size -= MM_PREAMBLE_SIZE;

    if (avctx->reget_buffer(avctx, &s->frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    switch(type) {
    case MM_TYPE_PALETTE   : mm_decode_pal(s, buf, buf_end); return buf_size;
    case MM_TYPE_INTRA     : mm_decode_intra(s, 0, 0, buf, buf_size); break;
    case MM_TYPE_INTRA_HH  : mm_decode_intra(s, 1, 0, buf, buf_size); break;
    case MM_TYPE_INTRA_HHV : mm_decode_intra(s, 1, 1, buf, buf_size); break;
    case MM_TYPE_INTER     : mm_decode_inter(s, 0, 0, buf, buf_size); break;
    case MM_TYPE_INTER_HH  : mm_decode_inter(s, 1, 0, buf, buf_size); break;
    case MM_TYPE_INTER_HHV : mm_decode_inter(s, 1, 1, buf, buf_size); break;
    default :
        return -1;
    }

    memcpy(s->frame.data[1], s->palette, AVPALETTE_SIZE);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    return buf_size;
}

static av_cold int mm_decode_end(AVCodecContext *avctx)
{
    MmContext *s = avctx->priv_data;

    if(s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec ff_mmvideo_decoder = {
    .name           = "mmvideo",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_MMVIDEO,
    .priv_data_size = sizeof(MmContext),
    .init           = mm_decode_init,
    .close          = mm_decode_end,
    .decode         = mm_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("American Laser Games MM Video"),
};
