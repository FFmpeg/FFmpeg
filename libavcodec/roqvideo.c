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
 *
 */

/**
 * @file roqvideo.c
 * Id RoQ Video Decoder by Dr. Tim Ferguson
 * For more information about the Id RoQ format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

typedef struct {
  unsigned char y0, y1, y2, y3, u, v;
} roq_cell;

typedef struct {
  int idx[4];
} roq_qcell;

static int uiclip[1024], *uiclp;  /* clipping table */
#define avg2(a,b) uiclp[(((int)(a)+(int)(b)+1)>>1)]
#define avg4(a,b,c,d) uiclp[(((int)(a)+(int)(b)+(int)(c)+(int)(d)+2)>>2)]

typedef struct RoqContext {

    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame frames[2];
    AVFrame *last_frame;
    AVFrame *current_frame;
    int first_frame;
    int y_stride;
    int c_stride;

    roq_cell cells[256];
    roq_qcell qcells[256];

    unsigned char *buf;
    int size;

} RoqContext;

#define RoQ_INFO              0x1001
#define RoQ_QUAD_CODEBOOK     0x1002
#define RoQ_QUAD_VQ           0x1011
#define RoQ_SOUND_MONO        0x1020
#define RoQ_SOUND_STEREO      0x1021

#define RoQ_ID_MOT              0x00
#define RoQ_ID_FCC              0x01
#define RoQ_ID_SLD              0x02
#define RoQ_ID_CCC              0x03

#define get_byte(in_buffer) *(in_buffer++)
#define get_word(in_buffer) ((unsigned short)(in_buffer += 2, \
  (in_buffer[-1] << 8 | in_buffer[-2])))
#define get_long(in_buffer) ((unsigned long)(in_buffer += 4, \
  (in_buffer[-1] << 24 | in_buffer[-2] << 16 | in_buffer[-3] << 8 | in_buffer[-4])))


static void apply_vector_2x2(RoqContext *ri, int x, int y, roq_cell *cell)
{
    unsigned char *yptr;

    yptr = ri->current_frame->data[0] + (y * ri->y_stride) + x;
    *yptr++ = cell->y0;
    *yptr++ = cell->y1;
    yptr += (ri->y_stride - 2);
    *yptr++ = cell->y2;
    *yptr++ = cell->y3;
    ri->current_frame->data[1][(y/2) * (ri->c_stride) + x/2] = cell->u;
    ri->current_frame->data[2][(y/2) * (ri->c_stride) + x/2] = cell->v;
}

static void apply_vector_4x4(RoqContext *ri, int x, int y, roq_cell *cell)
{
    unsigned long row_inc, c_row_inc;
    register unsigned char y0, y1, u, v;
    unsigned char *yptr, *uptr, *vptr;

    yptr = ri->current_frame->data[0] + (y * ri->y_stride) + x;
    uptr = ri->current_frame->data[1] + (y/2) * (ri->c_stride) + x/2;
    vptr = ri->current_frame->data[2] + (y/2) * (ri->c_stride) + x/2;

    row_inc = ri->y_stride - 4;
    c_row_inc = (ri->c_stride) - 2;
    *yptr++ = y0 = cell->y0; *uptr++ = u = cell->u; *vptr++ = v = cell->v;
    *yptr++ = y0;
    *yptr++ = y1 = cell->y1; *uptr++ = u; *vptr++ = v;
    *yptr++ = y1;

    yptr += row_inc;

    *yptr++ = y0;
    *yptr++ = y0;
    *yptr++ = y1;
    *yptr++ = y1;

    yptr += row_inc; uptr += c_row_inc; vptr += c_row_inc;

    *yptr++ = y0 = cell->y2; *uptr++ = u; *vptr++ = v;
    *yptr++ = y0;
    *yptr++ = y1 = cell->y3; *uptr++ = u; *vptr++ = v;
    *yptr++ = y1;

    yptr += row_inc;

    *yptr++ = y0;
    *yptr++ = y0;
    *yptr++ = y1;
    *yptr++ = y1;
}

static void apply_motion_4x4(RoqContext *ri, int x, int y, unsigned char mv,
    signed char mean_x, signed char mean_y)
{
    int i, hw, mx, my;
    unsigned char *pa, *pb;

    mx = x + 8 - (mv >> 4) - mean_x;
    my = y + 8 - (mv & 0xf) - mean_y;

    /* check MV against frame boundaries */
    if ((mx < 0) || (mx > ri->avctx->width - 4) ||
        (my < 0) || (my > ri->avctx->height - 4)) {
        av_log(ri->avctx, AV_LOG_ERROR, "motion vector out of bounds: MV = (%d, %d), boundaries = (0, 0, %d, %d)\n",
            mx, my, ri->avctx->width, ri->avctx->height);
        return;
    }

    pa = ri->current_frame->data[0] + (y * ri->y_stride) + x;
    pb = ri->last_frame->data[0] + (my * ri->y_stride) + mx;
    for(i = 0; i < 4; i++) {
        pa[0] = pb[0];
        pa[1] = pb[1];
        pa[2] = pb[2];
        pa[3] = pb[3];
        pa += ri->y_stride;
        pb += ri->y_stride;
    }

    hw = ri->y_stride/2;
    pa = ri->current_frame->data[1] + (y * ri->y_stride)/4 + x/2;
    pb = ri->last_frame->data[1] + (my/2) * (ri->y_stride/2) + (mx + 1)/2;

    for(i = 0; i < 2; i++) {
        switch(((my & 0x01) << 1) | (mx & 0x01)) {

        case 0:
            pa[0] = pb[0];
            pa[1] = pb[1];
            pa[hw] = pb[hw];
            pa[hw+1] = pb[hw+1];
            break;

        case 1:
            pa[0] = avg2(pb[0], pb[1]);
            pa[1] = avg2(pb[1], pb[2]);
            pa[hw] = avg2(pb[hw], pb[hw+1]);
            pa[hw+1] = avg2(pb[hw+1], pb[hw+2]);
            break;

        case 2:
            pa[0] = avg2(pb[0], pb[hw]);
            pa[1] = avg2(pb[1], pb[hw+1]);
            pa[hw] = avg2(pb[hw], pb[hw*2]);
            pa[hw+1] = avg2(pb[hw+1], pb[(hw*2)+1]);
            break;

        case 3:
            pa[0] = avg4(pb[0], pb[1], pb[hw], pb[hw+1]);
            pa[1] = avg4(pb[1], pb[2], pb[hw+1], pb[hw+2]);
            pa[hw] = avg4(pb[hw], pb[hw+1], pb[hw*2], pb[(hw*2)+1]);
            pa[hw+1] = avg4(pb[hw+1], pb[hw+2], pb[(hw*2)+1], pb[(hw*2)+1]);
            break;
        }

        pa = ri->current_frame->data[2] + (y * ri->y_stride)/4 + x/2;
        pb = ri->last_frame->data[2] + (my/2) * (ri->y_stride/2) + (mx + 1)/2;
    }
}

static void apply_motion_8x8(RoqContext *ri, int x, int y,
    unsigned char mv, signed char mean_x, signed char mean_y)
{
    int mx, my, i, j, hw;
    unsigned char *pa, *pb;

    mx = x + 8 - (mv >> 4) - mean_x;
    my = y + 8 - (mv & 0xf) - mean_y;

    /* check MV against frame boundaries */
    if ((mx < 0) || (mx > ri->avctx->width - 8) ||
        (my < 0) || (my > ri->avctx->height - 8)) {
        av_log(ri->avctx, AV_LOG_ERROR, "motion vector out of bounds: MV = (%d, %d), boundaries = (0, 0, %d, %d)\n",
            mx, my, ri->avctx->width, ri->avctx->height);
        return;
    }

    pa = ri->current_frame->data[0] + (y * ri->y_stride) + x;
    pb = ri->last_frame->data[0] + (my * ri->y_stride) + mx;
    for(i = 0; i < 8; i++) {
        pa[0] = pb[0];
        pa[1] = pb[1];
        pa[2] = pb[2];
        pa[3] = pb[3];
        pa[4] = pb[4];
        pa[5] = pb[5];
        pa[6] = pb[6];
        pa[7] = pb[7];
        pa += ri->y_stride;
        pb += ri->y_stride;
    }

    hw = ri->c_stride;
    pa = ri->current_frame->data[1] + (y * ri->y_stride)/4 + x/2;
    pb = ri->last_frame->data[1] + (my/2) * (ri->y_stride/2) + (mx + 1)/2;
    for(j = 0; j < 2; j++) {
        for(i = 0; i < 4; i++) {
            switch(((my & 0x01) << 1) | (mx & 0x01)) {

            case 0:
                pa[0] = pb[0];
                pa[1] = pb[1];
                pa[2] = pb[2];
                pa[3] = pb[3];
                break;

            case 1:
                pa[0] = avg2(pb[0], pb[1]);
                pa[1] = avg2(pb[1], pb[2]);
                pa[2] = avg2(pb[2], pb[3]);
                pa[3] = avg2(pb[3], pb[4]);
                break;

            case 2:
                pa[0] = avg2(pb[0], pb[hw]);
                pa[1] = avg2(pb[1], pb[hw+1]);
                pa[2] = avg2(pb[2], pb[hw+2]);
                pa[3] = avg2(pb[3], pb[hw+3]);
                break;

            case 3:
                pa[0] = avg4(pb[0], pb[1], pb[hw], pb[hw+1]);
                pa[1] = avg4(pb[1], pb[2], pb[hw+1], pb[hw+2]);
                pa[2] = avg4(pb[2], pb[3], pb[hw+2], pb[hw+3]);
                pa[3] = avg4(pb[3], pb[4], pb[hw+3], pb[hw+4]);
                break;
            }
            pa += ri->c_stride;
            pb += ri->c_stride;
        }

        pa = ri->current_frame->data[2] + (y * ri->y_stride)/4 + x/2;
        pb = ri->last_frame->data[2] + (my/2) * (ri->y_stride/2) + (mx + 1)/2;
    }
}

static void roqvideo_decode_frame(RoqContext *ri)
{
    unsigned int chunk_id = 0, chunk_arg = 0;
    unsigned long chunk_size = 0;
    int i, j, k, nv1, nv2, vqflg = 0, vqflg_pos = -1;
    int vqid, bpos, xpos, ypos, xp, yp, x, y;
    int frame_stats[2][4] = {{0},{0}};
    roq_qcell *qcell;
    unsigned char *buf = ri->buf;
    unsigned char *buf_end = ri->buf + ri->size;

    while (buf < buf_end) {
        chunk_id = get_word(buf);
        chunk_size = get_long(buf);
        chunk_arg = get_word(buf);

        if(chunk_id == RoQ_QUAD_VQ)
            break;
        if(chunk_id == RoQ_QUAD_CODEBOOK) {
            if((nv1 = chunk_arg >> 8) == 0)
                nv1 = 256;
            if((nv2 = chunk_arg & 0xff) == 0 && nv1 * 6 < chunk_size)
                nv2 = 256;
            for(i = 0; i < nv1; i++) {
                ri->cells[i].y0 = get_byte(buf);
                ri->cells[i].y1 = get_byte(buf);
                ri->cells[i].y2 = get_byte(buf);
                ri->cells[i].y3 = get_byte(buf);
                ri->cells[i].u = get_byte(buf);
                ri->cells[i].v = get_byte(buf);
            }
            for(i = 0; i < nv2; i++)
                for(j = 0; j < 4; j++)
                    ri->qcells[i].idx[j] = get_byte(buf);
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
                    apply_motion_8x8(ri, xp, yp, 0, 8, 8);
                    break;
                case RoQ_ID_FCC:
                    apply_motion_8x8(ri, xp, yp, buf[bpos++], chunk_arg >> 8,
                        chunk_arg & 0xff);
                    break;
                case RoQ_ID_SLD:
                    qcell = ri->qcells + buf[bpos++];
                    apply_vector_4x4(ri, xp, yp, ri->cells + qcell->idx[0]);
                    apply_vector_4x4(ri, xp+4, yp, ri->cells + qcell->idx[1]);
                    apply_vector_4x4(ri, xp, yp+4, ri->cells + qcell->idx[2]);
                    apply_vector_4x4(ri, xp+4, yp+4, ri->cells + qcell->idx[3]);
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
                            apply_motion_4x4(ri, x, y, 0, 8, 8);
                            break;
                        case RoQ_ID_FCC:
                            apply_motion_4x4(ri, x, y, buf[bpos++],
                                chunk_arg >> 8, chunk_arg & 0xff);
                            break;
                        case RoQ_ID_SLD:
                            qcell = ri->qcells + buf[bpos++];
                            apply_vector_2x2(ri, x, y, ri->cells + qcell->idx[0]);
                            apply_vector_2x2(ri, x+2, y, ri->cells + qcell->idx[1]);
                            apply_vector_2x2(ri, x, y+2, ri->cells + qcell->idx[2]);
                            apply_vector_2x2(ri, x+2, y+2, ri->cells + qcell->idx[3]);
                            break;
                        case RoQ_ID_CCC:
                            apply_vector_2x2(ri, x, y, ri->cells + buf[bpos]);
                            apply_vector_2x2(ri, x+2, y, ri->cells + buf[bpos+1]);
                            apply_vector_2x2(ri, x, y+2, ri->cells + buf[bpos+2]);
                            apply_vector_2x2(ri, x+2, y+2, ri->cells + buf[bpos+3]);
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
        if (xpos >= ri->avctx->width) {
            xpos -= ri->avctx->width;
            ypos += 16;
        }
        if(ypos >= ri->avctx->height)
            break;
    }
}


static int roq_decode_init(AVCodecContext *avctx)
{
    RoqContext *s = avctx->priv_data;
    int i;

    s->avctx = avctx;
    s->first_frame = 1;
    s->last_frame    = &s->frames[0];
    s->current_frame = &s->frames[1];
    avctx->pix_fmt = PIX_FMT_YUV420P;
    avctx->has_b_frames = 0;
    dsputil_init(&s->dsp, avctx);

    uiclp = uiclip+512;
    for(i = -512; i < 512; i++)
        uiclp[i] = (i < 0 ? 0 : (i > 255 ? 255 : i));

    return 0;
}

static int roq_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    RoqContext *s = avctx->priv_data;

    if (avctx->get_buffer(avctx, s->current_frame)) {
        av_log(avctx, AV_LOG_ERROR, "  RoQ: get_buffer() failed\n");
        return -1;
    }
    s->y_stride = s->current_frame->linesize[0];
    s->c_stride = s->current_frame->linesize[1];

    s->buf = buf;
    s->size = buf_size;
    roqvideo_decode_frame(s);

    /* release the last frame if it is allocated */
    if (s->first_frame)
        s->first_frame = 0;
    else
        avctx->release_buffer(avctx, s->last_frame);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = *s->current_frame;

    /* shuffle frames */
    FFSWAP(AVFrame *, s->current_frame, s->last_frame);

    return buf_size;
}

static int roq_decode_end(AVCodecContext *avctx)
{
    RoqContext *s = avctx->priv_data;

    /* release the last frame */
    if (s->last_frame->data[0])
        avctx->release_buffer(avctx, s->last_frame);

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
