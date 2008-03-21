/*
 * Copyright (C) 2003 the ffmpeg project
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
 * @file roqvideodec.c
 * Id RoQ Video Decoder by Dr. Tim Ferguson
 * For more information about the Id RoQ format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "avcodec.h"
#include "bytestream.h"
#include "roqvideo.h"

static void roqvideo_decode_frame(RoqContext *ri)
{
    unsigned int chunk_id = 0, chunk_arg = 0;
    unsigned long chunk_size = 0;
    int i, j, k, nv1, nv2, vqflg = 0, vqflg_pos = -1;
    int vqid, bpos, xpos, ypos, xp, yp, x, y, mx, my;
    int frame_stats[2][4] = {{0},{0}};
    roq_qcell *qcell;
    const unsigned char *buf = ri->buf;
    const unsigned char *buf_end = ri->buf + ri->size;

    while (buf < buf_end) {
        chunk_id = bytestream_get_le16(&buf);
        chunk_size = bytestream_get_le32(&buf);
        chunk_arg = bytestream_get_le16(&buf);

        if(chunk_id == RoQ_QUAD_VQ)
            break;
        if(chunk_id == RoQ_QUAD_CODEBOOK) {
            if((nv1 = chunk_arg >> 8) == 0)
                nv1 = 256;
            if((nv2 = chunk_arg & 0xff) == 0 && nv1 * 6 < chunk_size)
                nv2 = 256;
            for(i = 0; i < nv1; i++) {
                ri->cb2x2[i].y[0] = *buf++;
                ri->cb2x2[i].y[1] = *buf++;
                ri->cb2x2[i].y[2] = *buf++;
                ri->cb2x2[i].y[3] = *buf++;
                ri->cb2x2[i].u = *buf++;
                ri->cb2x2[i].v = *buf++;
            }
            for(i = 0; i < nv2; i++)
                for(j = 0; j < 4; j++)
                    ri->cb4x4[i].idx[j] = *buf++;
        }
    }

    bpos = xpos = ypos = 0;
    while(bpos < chunk_size) {
        for (yp = ypos; yp < ypos + 16; yp += 8)
            for (xp = xpos; xp < xpos + 16; xp += 8) {
                if (vqflg_pos < 0) {
                    vqflg = buf[bpos++]; vqflg |= (buf[bpos++] << 8);
                    vqflg_pos = 7;
                }
                vqid = (vqflg >> (vqflg_pos * 2)) & 0x3;
                frame_stats[0][vqid]++;
                vqflg_pos--;

                switch(vqid) {
                case RoQ_ID_MOT:
                    break;
                case RoQ_ID_FCC:
                    mx = 8 - (buf[bpos] >> 4) - ((signed char) (chunk_arg >> 8));
                    my = 8 - (buf[bpos++] & 0xf) - ((signed char) chunk_arg);
                    ff_apply_motion_8x8(ri, xp, yp, mx, my);
                    break;
                case RoQ_ID_SLD:
                    qcell = ri->cb4x4 + buf[bpos++];
                    ff_apply_vector_4x4(ri, xp, yp, ri->cb2x2 + qcell->idx[0]);
                    ff_apply_vector_4x4(ri, xp+4, yp, ri->cb2x2 + qcell->idx[1]);
                    ff_apply_vector_4x4(ri, xp, yp+4, ri->cb2x2 + qcell->idx[2]);
                    ff_apply_vector_4x4(ri, xp+4, yp+4, ri->cb2x2 + qcell->idx[3]);
                    break;
                case RoQ_ID_CCC:
                    for (k = 0; k < 4; k++) {
                        x = xp; y = yp;
                        if(k & 0x01) x += 4;
                        if(k & 0x02) y += 4;

                        if (vqflg_pos < 0) {
                            vqflg = buf[bpos++];
                            vqflg |= (buf[bpos++] << 8);
                            vqflg_pos = 7;
                        }
                        vqid = (vqflg >> (vqflg_pos * 2)) & 0x3;
                        frame_stats[1][vqid]++;
                        vqflg_pos--;
                        switch(vqid) {
                        case RoQ_ID_MOT:
                            break;
                        case RoQ_ID_FCC:
                            mx = 8 - (buf[bpos] >> 4) - ((signed char) (chunk_arg >> 8));
                            my = 8 - (buf[bpos++] & 0xf) - ((signed char) chunk_arg);
                            ff_apply_motion_4x4(ri, x, y, mx, my);
                            break;
                        case RoQ_ID_SLD:
                            qcell = ri->cb4x4 + buf[bpos++];
                            ff_apply_vector_2x2(ri, x, y, ri->cb2x2 + qcell->idx[0]);
                            ff_apply_vector_2x2(ri, x+2, y, ri->cb2x2 + qcell->idx[1]);
                            ff_apply_vector_2x2(ri, x, y+2, ri->cb2x2 + qcell->idx[2]);
                            ff_apply_vector_2x2(ri, x+2, y+2, ri->cb2x2 + qcell->idx[3]);
                            break;
                        case RoQ_ID_CCC:
                            ff_apply_vector_2x2(ri, x, y, ri->cb2x2 + buf[bpos]);
                            ff_apply_vector_2x2(ri, x+2, y, ri->cb2x2 + buf[bpos+1]);
                            ff_apply_vector_2x2(ri, x, y+2, ri->cb2x2 + buf[bpos+2]);
                            ff_apply_vector_2x2(ri, x+2, y+2, ri->cb2x2 + buf[bpos+3]);
                            bpos += 4;
                            break;
                        }
                    }
                    break;
                default:
                    av_log(ri->avctx, AV_LOG_ERROR, "Unknown vq code: %d\n", vqid);
            }
        }

        xpos += 16;
        if (xpos >= ri->width) {
            xpos -= ri->width;
            ypos += 16;
        }
        if(ypos >= ri->height)
            break;
    }
}


static av_cold int roq_decode_init(AVCodecContext *avctx)
{
    RoqContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->width = avctx->width;
    s->height = avctx->height;
    s->last_frame    = &s->frames[0];
    s->current_frame = &s->frames[1];
    avctx->pix_fmt = PIX_FMT_YUV444P;

    return 0;
}

static int roq_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            const uint8_t *buf, int buf_size)
{
    RoqContext *s = avctx->priv_data;
    int copy= !s->current_frame->data[0];

    if (avctx->reget_buffer(avctx, s->current_frame)) {
        av_log(avctx, AV_LOG_ERROR, "  RoQ: get_buffer() failed\n");
        return -1;
    }

    if(copy)
        av_picture_copy((AVPicture*)s->current_frame, (AVPicture*)s->last_frame,
                        avctx->pix_fmt, avctx->width, avctx->height);

    s->buf = buf;
    s->size = buf_size;
    roqvideo_decode_frame(s);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = *s->current_frame;

    /* shuffle frames */
    FFSWAP(AVFrame *, s->current_frame, s->last_frame);

    return buf_size;
}

static av_cold int roq_decode_end(AVCodecContext *avctx)
{
    RoqContext *s = avctx->priv_data;

    /* release the last frame */
    if (s->last_frame->data[0])
        avctx->release_buffer(avctx, s->last_frame);
    if (s->current_frame->data[0])
        avctx->release_buffer(avctx, s->current_frame);

    return 0;
}

AVCodec roq_decoder = {
    "roqvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_ROQ,
    sizeof(RoqContext),
    roq_decode_init,
    NULL,
    roq_decode_end,
    roq_decode_frame,
    CODEC_CAP_DR1,
};
