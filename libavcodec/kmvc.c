/*
 * KMVC decoder
 * Copyright (c) 2006 Konstantin Shishkov
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
 * Karl Morton's Video Codec decoder
 */

#include <stdio.h>
#include <stdlib.h>

#include "avcodec.h"
#include "bytestream.h"

#define KMVC_KEYFRAME 0x80
#define KMVC_PALETTE  0x40
#define KMVC_METHOD   0x0F

/*
 * Decoder context
 */
typedef struct KmvcContext {
    AVCodecContext *avctx;
    AVFrame pic;

    int setpal;
    int palsize;
    uint32_t pal[256];
    uint8_t *cur, *prev;
    uint8_t *frm0, *frm1;
} KmvcContext;

typedef struct BitBuf {
    int bits;
    int bitbuf;
} BitBuf;

#define BLK(data, x, y)  data[(x) + (y) * 320]

#define kmvc_init_getbits(bb, src)  bb.bits = 7; bb.bitbuf = *src++;

#define kmvc_getbit(bb, src, src_end, res) {\
    res = 0; \
    if (bb.bitbuf & (1 << bb.bits)) res = 1; \
    bb.bits--; \
    if(bb.bits == -1) { \
        if (src >= src_end) { \
            av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n"); \
            return AVERROR_INVALIDDATA; \
        } \
        bb.bitbuf = *src++; \
        bb.bits = 7; \
    } \
}

static int kmvc_decode_intra_8x8(KmvcContext * ctx, const uint8_t * src, int src_size, int w, int h)
{
    BitBuf bb;
    int res, val;
    int i, j;
    int bx, by;
    int l0x, l1x, l0y, l1y;
    int mx, my;
    const uint8_t *src_end = src + src_size;

    kmvc_init_getbits(bb, src);

    for (by = 0; by < h; by += 8)
        for (bx = 0; bx < w; bx += 8) {
            kmvc_getbit(bb, src, src_end, res);
            if (!res) {         // fill whole 8x8 block
                if (src >= src_end) {
                    av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                    return AVERROR_INVALIDDATA;
                }
                val = *src++;
                for (i = 0; i < 64; i++)
                    BLK(ctx->cur, bx + (i & 0x7), by + (i >> 3)) = val;
            } else {            // handle four 4x4 subblocks
                for (i = 0; i < 4; i++) {
                    l0x = bx + (i & 1) * 4;
                    l0y = by + (i & 2) * 2;
                    kmvc_getbit(bb, src, src_end, res);
                    if (!res) {
                        kmvc_getbit(bb, src, src_end, res);
                        if (!res) {     // fill whole 4x4 block
                            if (src >= src_end) {
                                av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                                return AVERROR_INVALIDDATA;
                            }
                            val = *src++;
                            for (j = 0; j < 16; j++)
                                BLK(ctx->cur, l0x + (j & 3), l0y + (j >> 2)) = val;
                        } else {        // copy block from already decoded place
                            if (src >= src_end) {
                                av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                                return AVERROR_INVALIDDATA;
                            }
                            val = *src++;
                            mx = val & 0xF;
                            my = val >> 4;
                            for (j = 0; j < 16; j++)
                                BLK(ctx->cur, l0x + (j & 3), l0y + (j >> 2)) =
                                    BLK(ctx->cur, l0x + (j & 3) - mx, l0y + (j >> 2) - my);
                        }
                    } else {    // descend to 2x2 sub-sub-blocks
                        for (j = 0; j < 4; j++) {
                            l1x = l0x + (j & 1) * 2;
                            l1y = l0y + (j & 2);
                            kmvc_getbit(bb, src, src_end, res);
                            if (!res) {
                                kmvc_getbit(bb, src, src_end, res);
                                if (!res) {     // fill whole 2x2 block
                                    if (src >= src_end) {
                                        av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                                        return AVERROR_INVALIDDATA;
                                    }
                                    val = *src++;
                                    BLK(ctx->cur, l1x, l1y) = val;
                                    BLK(ctx->cur, l1x + 1, l1y) = val;
                                    BLK(ctx->cur, l1x, l1y + 1) = val;
                                    BLK(ctx->cur, l1x + 1, l1y + 1) = val;
                                } else {        // copy block from already decoded place
                                    if (src >= src_end) {
                                        av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                                        return AVERROR_INVALIDDATA;
                                    }
                                    val = *src++;
                                    mx = val & 0xF;
                                    my = val >> 4;
                                    BLK(ctx->cur, l1x, l1y) = BLK(ctx->cur, l1x - mx, l1y - my);
                                    BLK(ctx->cur, l1x + 1, l1y) =
                                        BLK(ctx->cur, l1x + 1 - mx, l1y - my);
                                    BLK(ctx->cur, l1x, l1y + 1) =
                                        BLK(ctx->cur, l1x - mx, l1y + 1 - my);
                                    BLK(ctx->cur, l1x + 1, l1y + 1) =
                                        BLK(ctx->cur, l1x + 1 - mx, l1y + 1 - my);
                                }
                            } else {    // read values for block
                                BLK(ctx->cur, l1x, l1y) = *src++;
                                BLK(ctx->cur, l1x + 1, l1y) = *src++;
                                BLK(ctx->cur, l1x, l1y + 1) = *src++;
                                BLK(ctx->cur, l1x + 1, l1y + 1) = *src++;
                            }
                        }
                    }
                }
            }
        }

    return 0;
}

static int kmvc_decode_inter_8x8(KmvcContext * ctx, const uint8_t * src, int src_size, int w, int h)
{
    BitBuf bb;
    int res, val;
    int i, j;
    int bx, by;
    int l0x, l1x, l0y, l1y;
    int mx, my;
    const uint8_t *src_end = src + src_size;

    kmvc_init_getbits(bb, src);

    for (by = 0; by < h; by += 8)
        for (bx = 0; bx < w; bx += 8) {
            kmvc_getbit(bb, src, src_end, res);
            if (!res) {
                kmvc_getbit(bb, src, src_end, res);
                if (!res) {     // fill whole 8x8 block
                    if (src >= src_end) {
                        av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                        return AVERROR_INVALIDDATA;
                    }
                    val = *src++;
                    for (i = 0; i < 64; i++)
                        BLK(ctx->cur, bx + (i & 0x7), by + (i >> 3)) = val;
                } else {        // copy block from previous frame
                    for (i = 0; i < 64; i++)
                        BLK(ctx->cur, bx + (i & 0x7), by + (i >> 3)) =
                            BLK(ctx->prev, bx + (i & 0x7), by + (i >> 3));
                }
            } else {            // handle four 4x4 subblocks
                for (i = 0; i < 4; i++) {
                    l0x = bx + (i & 1) * 4;
                    l0y = by + (i & 2) * 2;
                    kmvc_getbit(bb, src, src_end, res);
                    if (!res) {
                        kmvc_getbit(bb, src, src_end, res);
                        if (!res) {     // fill whole 4x4 block
                            if (src >= src_end) {
                                av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                                return AVERROR_INVALIDDATA;
                            }
                            val = *src++;
                            for (j = 0; j < 16; j++)
                                BLK(ctx->cur, l0x + (j & 3), l0y + (j >> 2)) = val;
                        } else {        // copy block
                            if (src >= src_end) {
                                av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                                return AVERROR_INVALIDDATA;
                            }
                            val = *src++;
                            mx = (val & 0xF) - 8;
                            my = (val >> 4) - 8;
                            for (j = 0; j < 16; j++)
                                BLK(ctx->cur, l0x + (j & 3), l0y + (j >> 2)) =
                                    BLK(ctx->prev, l0x + (j & 3) + mx, l0y + (j >> 2) + my);
                        }
                    } else {    // descend to 2x2 sub-sub-blocks
                        for (j = 0; j < 4; j++) {
                            l1x = l0x + (j & 1) * 2;
                            l1y = l0y + (j & 2);
                            kmvc_getbit(bb, src, src_end, res);
                            if (!res) {
                                kmvc_getbit(bb, src, src_end, res);
                                if (!res) {     // fill whole 2x2 block
                                    if (src >= src_end) {
                                        av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                                        return AVERROR_INVALIDDATA;
                                    }
                                    val = *src++;
                                    BLK(ctx->cur, l1x, l1y) = val;
                                    BLK(ctx->cur, l1x + 1, l1y) = val;
                                    BLK(ctx->cur, l1x, l1y + 1) = val;
                                    BLK(ctx->cur, l1x + 1, l1y + 1) = val;
                                } else {        // copy block
                                    if (src >= src_end) {
                                        av_log(ctx->avctx, AV_LOG_ERROR, "Data overrun\n");
                                        return AVERROR_INVALIDDATA;
                                    }
                                    val = *src++;
                                    mx = (val & 0xF) - 8;
                                    my = (val >> 4) - 8;
                                    BLK(ctx->cur, l1x, l1y) = BLK(ctx->prev, l1x + mx, l1y + my);
                                    BLK(ctx->cur, l1x + 1, l1y) =
                                        BLK(ctx->prev, l1x + 1 + mx, l1y + my);
                                    BLK(ctx->cur, l1x, l1y + 1) =
                                        BLK(ctx->prev, l1x + mx, l1y + 1 + my);
                                    BLK(ctx->cur, l1x + 1, l1y + 1) =
                                        BLK(ctx->prev, l1x + 1 + mx, l1y + 1 + my);
                                }
                            } else {    // read values for block
                                BLK(ctx->cur, l1x, l1y) = *src++;
                                BLK(ctx->cur, l1x + 1, l1y) = *src++;
                                BLK(ctx->cur, l1x, l1y + 1) = *src++;
                                BLK(ctx->cur, l1x + 1, l1y + 1) = *src++;
                            }
                        }
                    }
                }
            }
        }

    return 0;
}

static int decode_frame(AVCodecContext * avctx, void *data, int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    KmvcContext *const ctx = avctx->priv_data;
    uint8_t *out, *src;
    int i;
    int header;
    int blocksize;
    const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, NULL);

    if (ctx->pic.data[0])
        avctx->release_buffer(avctx, &ctx->pic);

    ctx->pic.reference = 3;
    ctx->pic.buffer_hints = FF_BUFFER_HINTS_VALID;
    if (avctx->get_buffer(avctx, &ctx->pic) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    header = *buf++;

    /* blocksize 127 is really palette change event */
    if (buf[0] == 127) {
        buf += 3;
        for (i = 0; i < 127; i++) {
            ctx->pal[i + (header & 0x81)] = 0xFF << 24 | AV_RB24(buf);
            buf += 4;
        }
        buf -= 127 * 4 + 3;
    }

    if (header & KMVC_KEYFRAME) {
        ctx->pic.key_frame = 1;
        ctx->pic.pict_type = AV_PICTURE_TYPE_I;
    } else {
        ctx->pic.key_frame = 0;
        ctx->pic.pict_type = AV_PICTURE_TYPE_P;
    }

    if (header & KMVC_PALETTE) {
        ctx->pic.palette_has_changed = 1;
        // palette starts from index 1 and has 127 entries
        for (i = 1; i <= ctx->palsize; i++) {
            ctx->pal[i] = 0xFF << 24 | bytestream_get_be24(&buf);
        }
    }

    if (pal) {
        ctx->pic.palette_has_changed = 1;
        memcpy(ctx->pal, pal, AVPALETTE_SIZE);
    }

    if (ctx->setpal) {
        ctx->setpal = 0;
        ctx->pic.palette_has_changed = 1;
    }

    /* make the palette available on the way out */
    memcpy(ctx->pic.data[1], ctx->pal, 1024);

    blocksize = *buf++;

    if (blocksize != 8 && blocksize != 127) {
        av_log(avctx, AV_LOG_ERROR, "Block size = %i\n", blocksize);
        return -1;
    }
    memset(ctx->cur, 0, 320 * 200);
    switch (header & KMVC_METHOD) {
    case 0:
    case 1: // used in palette changed event
        memcpy(ctx->cur, ctx->prev, 320 * 200);
        break;
    case 3:
        kmvc_decode_intra_8x8(ctx, buf, buf_size, avctx->width, avctx->height);
        break;
    case 4:
        kmvc_decode_inter_8x8(ctx, buf, buf_size, avctx->width, avctx->height);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown compression method %i\n", header & KMVC_METHOD);
        return -1;
    }

    out = ctx->pic.data[0];
    src = ctx->cur;
    for (i = 0; i < avctx->height; i++) {
        memcpy(out, src, avctx->width);
        src += 320;
        out += ctx->pic.linesize[0];
    }

    /* flip buffers */
    if (ctx->cur == ctx->frm0) {
        ctx->cur = ctx->frm1;
        ctx->prev = ctx->frm0;
    } else {
        ctx->cur = ctx->frm0;
        ctx->prev = ctx->frm1;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame *) data = ctx->pic;

    /* always report that the buffer was completely consumed */
    return buf_size;
}



/*
 * Init kmvc decoder
 */
static av_cold int decode_init(AVCodecContext * avctx)
{
    KmvcContext *const c = avctx->priv_data;
    int i;

    c->avctx = avctx;

    if (avctx->width > 320 || avctx->height > 200) {
        av_log(avctx, AV_LOG_ERROR, "KMVC supports frames <= 320x200\n");
        return -1;
    }

    c->frm0 = av_mallocz(320 * 200);
    c->frm1 = av_mallocz(320 * 200);
    c->cur = c->frm0;
    c->prev = c->frm1;

    for (i = 0; i < 256; i++) {
        c->pal[i] = 0xFF << 24 | i * 0x10101;
    }

    if (avctx->extradata_size < 12) {
        av_log(NULL, 0, "Extradata missing, decoding may not work properly...\n");
        c->palsize = 127;
    } else {
        c->palsize = AV_RL16(avctx->extradata + 10);
    }

    if (avctx->extradata_size == 1036) {        // palette in extradata
        uint8_t *src = avctx->extradata + 12;
        for (i = 0; i < 256; i++) {
            c->pal[i] = AV_RL32(src);
            src += 4;
        }
        c->setpal = 1;
    }

    avcodec_get_frame_defaults(&c->pic);
    avctx->pix_fmt = PIX_FMT_PAL8;

    return 0;
}



/*
 * Uninit kmvc decoder
 */
static av_cold int decode_end(AVCodecContext * avctx)
{
    KmvcContext *const c = avctx->priv_data;

    av_freep(&c->frm0);
    av_freep(&c->frm1);
    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    return 0;
}

AVCodec ff_kmvc_decoder = {
    .name           = "kmvc",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_KMVC,
    .priv_data_size = sizeof(KmvcContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Karl Morton's video codec"),
};
