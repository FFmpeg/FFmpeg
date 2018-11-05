/*
 * QPEG codec
 * Copyright (c) 2004 Konstantin Shishkov
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
 * QPEG codec.
 */

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

typedef struct QpegContext{
    AVCodecContext *avctx;
    AVFrame *pic, *ref;
    uint32_t pal[256];
    GetByteContext buffer;
} QpegContext;

static void qpeg_decode_intra(QpegContext *qctx, uint8_t *dst,
                              int stride, int width, int height)
{
    int i;
    int code;
    int c0, c1;
    int run, copy;
    int filled = 0;
    int rows_to_go;

    rows_to_go = height;
    height--;
    dst = dst + height * stride;

    while ((bytestream2_get_bytes_left(&qctx->buffer) > 0) && (rows_to_go > 0)) {
        code = bytestream2_get_byte(&qctx->buffer);
        run = copy = 0;
        if(code == 0xFC) /* end-of-picture code */
            break;
        if(code >= 0xF8) { /* very long run */
            c0 = bytestream2_get_byte(&qctx->buffer);
            c1 = bytestream2_get_byte(&qctx->buffer);
            run = ((code & 0x7) << 16) + (c0 << 8) + c1 + 2;
        } else if (code >= 0xF0) { /* long run */
            c0 = bytestream2_get_byte(&qctx->buffer);
            run = ((code & 0xF) << 8) + c0 + 2;
        } else if (code >= 0xE0) { /* short run */
            run = (code & 0x1F) + 2;
        } else if (code >= 0xC0) { /* very long copy */
            c0 = bytestream2_get_byte(&qctx->buffer);
            c1 = bytestream2_get_byte(&qctx->buffer);
            copy = ((code & 0x3F) << 16) + (c0 << 8) + c1 + 1;
        } else if (code >= 0x80) { /* long copy */
            c0 = bytestream2_get_byte(&qctx->buffer);
            copy = ((code & 0x7F) << 8) + c0 + 1;
        } else { /* short copy */
            copy = code + 1;
        }

        /* perform actual run or copy */
        if(run) {
            int p;

            p = bytestream2_get_byte(&qctx->buffer);
            for(i = 0; i < run; i++) {
                dst[filled++] = p;
                if (filled >= width) {
                    filled = 0;
                    dst -= stride;
                    rows_to_go--;
                    while (run - i > width && rows_to_go > 0) {
                        memset(dst, p, width);
                        dst -= stride;
                        rows_to_go--;
                        i += width;
                    }
                    if(rows_to_go <= 0)
                        break;
                }
            }
        } else {
            for(i = 0; i < copy; i++) {
                dst[filled++] = bytestream2_get_byte(&qctx->buffer);
                if (filled >= width) {
                    filled = 0;
                    dst -= stride;
                    rows_to_go--;
                    if(rows_to_go <= 0)
                        break;
                }
            }
        }
    }
}

static const int qpeg_table_h[16] =
 { 0x00, 0x20, 0x20, 0x20, 0x18, 0x10, 0x10, 0x20, 0x10, 0x08, 0x18, 0x08, 0x08, 0x18, 0x10, 0x04};
static const int qpeg_table_w[16] =
 { 0x00, 0x20, 0x18, 0x08, 0x18, 0x10, 0x20, 0x10, 0x08, 0x10, 0x20, 0x20, 0x08, 0x10, 0x18, 0x04};

/* Decodes delta frames */
static void av_noinline qpeg_decode_inter(QpegContext *qctx, uint8_t *dst,
                              int stride, int width, int height,
                              int delta, const uint8_t *ctable,
                              uint8_t *refdata)
{
    int i, j;
    int code;
    int filled = 0;
    int orig_height;

    if (refdata) {
        /* copy prev frame */
        for (i = 0; i < height; i++)
            memcpy(dst + (i * stride), refdata + (i * stride), width);
    } else {
        refdata = dst;
    }

    orig_height = height;
    height--;
    dst = dst + height * stride;

    while ((bytestream2_get_bytes_left(&qctx->buffer) > 0) && (height >= 0)) {
        code = bytestream2_get_byte(&qctx->buffer);

        if(delta) {
            /* motion compensation */
            while(bytestream2_get_bytes_left(&qctx->buffer) > 0 && (code & 0xF0) == 0xF0) {
                if(delta == 1) {
                    int me_idx;
                    int me_w, me_h, me_x, me_y;
                    uint8_t *me_plane;
                    int corr, val;

                    /* get block size by index */
                    me_idx = code & 0xF;
                    me_w = qpeg_table_w[me_idx];
                    me_h = qpeg_table_h[me_idx];

                    /* extract motion vector */
                    corr = bytestream2_get_byte(&qctx->buffer);

                    val = corr >> 4;
                    if(val > 7)
                        val -= 16;
                    me_x = val;

                    val = corr & 0xF;
                    if(val > 7)
                        val -= 16;
                    me_y = val;

                    /* check motion vector */
                    if ((me_x + filled < 0) || (me_x + me_w + filled > width) ||
                       (height - me_y - me_h < 0) || (height - me_y >= orig_height) ||
                       (filled + me_w > width) || (height - me_h < 0))
                        av_log(qctx->avctx, AV_LOG_ERROR, "Bogus motion vector (%i,%i), block size %ix%i at %i,%i\n",
                               me_x, me_y, me_w, me_h, filled, height);
                    else {
                        /* do motion compensation */
                        me_plane = refdata + (filled + me_x) + (height - me_y) * stride;
                        for(j = 0; j < me_h; j++) {
                            for(i = 0; i < me_w; i++)
                                dst[filled + i - (j * stride)] = me_plane[i - (j * stride)];
                        }
                    }
                }
                code = bytestream2_get_byte(&qctx->buffer);
            }
        }

        if(code == 0xE0) /* end-of-picture code */
            break;
        if(code > 0xE0) { /* run code: 0xE1..0xFF */
            int p;

            code &= 0x1F;
            p = bytestream2_get_byte(&qctx->buffer);
            for(i = 0; i <= code; i++) {
                dst[filled++] = p;
                if(filled >= width) {
                    filled = 0;
                    dst -= stride;
                    height--;
                    if (height < 0)
                        break;
                }
            }
        } else if(code >= 0xC0) { /* copy code: 0xC0..0xDF */
            code &= 0x1F;

            if(code + 1 > bytestream2_get_bytes_left(&qctx->buffer))
                break;

            for(i = 0; i <= code; i++) {
                dst[filled++] = bytestream2_get_byte(&qctx->buffer);
                if(filled >= width) {
                    filled = 0;
                    dst -= stride;
                    height--;
                    if (height < 0)
                        break;
                }
            }
        } else if(code >= 0x80) { /* skip code: 0x80..0xBF */
            int skip;

            code &= 0x3F;
            /* codes 0x80 and 0x81 are actually escape codes,
               skip value minus constant is in the next byte */
            if(!code)
                skip = bytestream2_get_byte(&qctx->buffer) +  64;
            else if(code == 1)
                skip = bytestream2_get_byte(&qctx->buffer) + 320;
            else
                skip = code;
            filled += skip;
            while( filled >= width) {
                filled -= width;
                dst -= stride;
                height--;
                if(height < 0)
                    break;
            }
        } else {
            /* zero code treated as one-pixel skip */
            if(code) {
                dst[filled++] = ctable[code & 0x7F];
            }
            else
                filled++;
            if(filled >= width) {
                filled = 0;
                dst -= stride;
                height--;
            }
        }
    }
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    uint8_t ctable[128];
    QpegContext * const a = avctx->priv_data;
    AVFrame * const p = a->pic;
    AVFrame * const ref = a->ref;
    uint8_t* outdata;
    int delta, ret;
    int pal_size;
    const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, &pal_size);

    if (avpkt->size < 0x86) {
        av_log(avctx, AV_LOG_ERROR, "Packet is too small\n");
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init(&a->buffer, avpkt->data, avpkt->size);

    av_frame_unref(ref);
    av_frame_move_ref(ref, p);

    if ((ret = ff_get_buffer(avctx, p, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;
    outdata = p->data[0];
    bytestream2_skip(&a->buffer, 4);
    bytestream2_get_buffer(&a->buffer, ctable, 128);
    bytestream2_skip(&a->buffer, 1);

    delta = bytestream2_get_byte(&a->buffer);
    if(delta == 0x10) {
        qpeg_decode_intra(a, outdata, p->linesize[0], avctx->width, avctx->height);
    } else {
        qpeg_decode_inter(a, outdata, p->linesize[0], avctx->width, avctx->height, delta, ctable, ref->data[0]);
    }

    /* make the palette available on the way out */
    if (pal && pal_size == AVPALETTE_SIZE) {
        p->palette_has_changed = 1;
        memcpy(a->pal, pal, AVPALETTE_SIZE);
    } else if (pal) {
        av_log(avctx, AV_LOG_ERROR, "Palette size %d is wrong\n", pal_size);
    }
    memcpy(p->data[1], a->pal, AVPALETTE_SIZE);

    if ((ret = av_frame_ref(data, p)) < 0)
        return ret;

    *got_frame      = 1;

    return avpkt->size;
}

static void decode_flush(AVCodecContext *avctx){
    QpegContext * const a = avctx->priv_data;
    int i, pal_size;
    const uint8_t *pal_src;

    pal_size = FFMIN(1024U, avctx->extradata_size);
    pal_src = avctx->extradata + avctx->extradata_size - pal_size;

    for (i=0; i<pal_size/4; i++)
        a->pal[i] = 0xFFU<<24 | AV_RL32(pal_src+4*i);
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    QpegContext * const a = avctx->priv_data;

    av_frame_free(&a->pic);
    av_frame_free(&a->ref);

    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx){
    QpegContext * const a = avctx->priv_data;

    a->avctx = avctx;
    avctx->pix_fmt= AV_PIX_FMT_PAL8;

    decode_flush(avctx);

    a->pic = av_frame_alloc();
    a->ref = av_frame_alloc();
    if (!a->pic || !a->ref) {
        decode_end(avctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

AVCodec ff_qpeg_decoder = {
    .name           = "qpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("Q-team QPEG"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_QPEG,
    .priv_data_size = sizeof(QpegContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .flush          = decode_flush,
    .capabilities   = AV_CODEC_CAP_DR1,
};
