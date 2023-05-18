/*
 * Beam Software VB decoder
 * Copyright (c) 2007 Konstantin Shishkov
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
 * VB Video decoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

enum VBFlags {
    VB_HAS_GMC     = 0x01,
    VB_HAS_AUDIO   = 0x04,
    VB_HAS_VIDEO   = 0x08,
    VB_HAS_PALETTE = 0x10,
    VB_HAS_LENGTH  = 0x20
};

typedef struct VBDecContext {
    AVCodecContext *avctx;

    uint8_t *frame, *prev_frame;
    uint32_t pal[AVPALETTE_COUNT];
    GetByteContext stream;
} VBDecContext;

static const uint16_t vb_patterns[64] = {
    0x0660, 0xFF00, 0xCCCC, 0xF000, 0x8888, 0x000F, 0x1111, 0xFEC8,
    0x8CEF, 0x137F, 0xF731, 0xC800, 0x008C, 0x0013, 0x3100, 0xCC00,
    0x00CC, 0x0033, 0x3300, 0x0FF0, 0x6666, 0x00F0, 0x0F00, 0x2222,
    0x4444, 0xF600, 0x8CC8, 0x006F, 0x1331, 0x318C, 0xC813, 0x33CC,
    0x6600, 0x0CC0, 0x0066, 0x0330, 0xF900, 0xC88C, 0x009F, 0x3113,
    0x6000, 0x0880, 0x0006, 0x0110, 0xCC88, 0xFC00, 0x00CF, 0x88CC,
    0x003F, 0x1133, 0x3311, 0xF300, 0x6FF6, 0x0603, 0x08C6, 0x8C63,
    0xC631, 0x6310, 0xC060, 0x0136, 0x136C, 0x36C8, 0x6C80, 0x324C
};

static void vb_decode_palette(VBDecContext *c, int data_size)
{
    int start, size, i;

    start = bytestream2_get_byte(&c->stream);
    size  = (bytestream2_get_byte(&c->stream) - 1) & 0xFF;
    if (start + size > 255) {
        av_log(c->avctx, AV_LOG_ERROR, "Palette change runs beyond entry 256\n");
        return;
    }
    if (size*3+2 > data_size) {
        av_log(c->avctx, AV_LOG_ERROR, "Palette data runs beyond chunk size\n");
        return;
    }
    for (i = start; i <= start + size; i++)
        c->pal[i] = 0xFFU << 24 | bytestream2_get_be24(&c->stream);
}

static inline int check_pixel(uint8_t *buf, uint8_t *start, uint8_t *end)
{
    return buf >= start && buf < end;
}

static inline int check_line(uint8_t *buf, uint8_t *start, uint8_t *end)
{
    return buf >= start && (buf + 4) <= end;
}

static int vb_decode_framedata(VBDecContext *c, int offset)
{
    GetByteContext g;
    uint8_t *prev, *cur;
    int blk, blocks, t, blk2;
    int blocktypes = 0;
    int x, y, a, b;
    int pattype, pattern;
    const int width = c->avctx->width;
    uint8_t *pstart = c->prev_frame;
    uint8_t *pend   = c->prev_frame + width*c->avctx->height;

    g = c->stream;

    prev = c->prev_frame + offset;
    cur  = c->frame;

    blocks = (c->avctx->width >> 2) * (c->avctx->height >> 2);
    blk2   = 0;
    for (blk = 0; blk < blocks; blk++) {
        if (!(blk & 3)) {
            if (bytestream2_get_bytes_left(&g) < 1) {
                av_log(c->avctx, AV_LOG_ERROR, "Insufficient data\n");
                return AVERROR_INVALIDDATA;
            }
            blocktypes = bytestream2_get_byte(&g);
        }
        switch (blocktypes & 0xC0) {
        case 0x00: //skip
            for (y = 0; y < 4; y++)
                if (check_line(prev + y*width, pstart, pend))
                    memcpy(cur + y*width, prev + y*width, 4);
                else
                    memset(cur + y*width, 0, 4);
            break;
        case 0x40:
            t = bytestream2_get_byte(&g);
            if (!t) { //raw block
                if (bytestream2_get_bytes_left(&g) < 16) {
                    av_log(c->avctx, AV_LOG_ERROR, "Insufficient data\n");
                    return AVERROR_INVALIDDATA;
                }
                for (y = 0; y < 4; y++)
                    bytestream2_get_buffer(&g, cur + y * width, 4);
            } else { // motion compensation
                x = ((t & 0xF)^8) - 8;
                y = ((t >> 4) ^8) - 8;
                t = x + y*width;
                for (y = 0; y < 4; y++)
                    if (check_line(prev + t + y*width, pstart, pend))
                        memcpy(cur + y*width, prev + t + y*width, 4);
                    else
                        memset(cur + y*width, 0, 4);
            }
            break;
        case 0x80: // fill
            t = bytestream2_get_byte(&g);
            for (y = 0; y < 4; y++)
                memset(cur + y*width, t, 4);
            break;
        case 0xC0: // pattern fill
            t       = bytestream2_get_byte(&g);
            pattype = t >> 6;
            pattern = vb_patterns[t & 0x3F];
            switch (pattype) {
            case 0:
                a = bytestream2_get_byte(&g);
                b = bytestream2_get_byte(&g);
                for (y = 0; y < 4; y++)
                    for (x = 0; x < 4; x++, pattern >>= 1)
                        cur[x + y*width] = (pattern & 1) ? b : a;
                break;
            case 1:
                pattern = ~pattern;
            case 2:
                a = bytestream2_get_byte(&g);
                for (y = 0; y < 4; y++)
                    for (x = 0; x < 4; x++, pattern >>= 1)
                        if (pattern & 1 && check_pixel(prev + x + y*width, pstart, pend))
                            cur[x + y*width] = prev[x + y*width];
                        else
                            cur[x + y*width] = a;
                break;
            case 3:
                av_log(c->avctx, AV_LOG_ERROR, "Invalid opcode seen @%d\n", blk);
                return AVERROR_INVALIDDATA;
            }
            break;
        }
        blocktypes <<= 2;
        cur  += 4;
        prev += 4;
        blk2++;
        if (blk2 == (width >> 2)) {
            blk2  = 0;
            cur  += width * 3;
            prev += width * 3;
        }
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    VBDecContext * const c = avctx->priv_data;
    uint8_t *outptr, *srcptr;
    int i, j, ret;
    int flags;
    uint32_t size;
    int offset = 0;

    if (avpkt->size < 2)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&c->stream, avpkt->data, avpkt->size);

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    flags = bytestream2_get_le16(&c->stream);

    if (flags & VB_HAS_GMC) {
        i = (int16_t)bytestream2_get_le16(&c->stream);
        j = (int16_t)bytestream2_get_le16(&c->stream);
        if (FFABS(j) > avctx->height) {
            av_log(avctx, AV_LOG_ERROR, "GMV out of range\n");
            return AVERROR_INVALIDDATA;
        }
        offset = i + j * avctx->width;
    }
    if (flags & VB_HAS_VIDEO) {
        size = bytestream2_get_le32(&c->stream);
        if(size > bytestream2_get_bytes_left(&c->stream)+4 || size<4){
            av_log(avctx, AV_LOG_ERROR, "Frame size invalid\n");
            return -1;
        }
        vb_decode_framedata(c, offset);
        bytestream2_skip(&c->stream, size - 4);
    }
    if (flags & VB_HAS_PALETTE) {
        size = bytestream2_get_le32(&c->stream);
        vb_decode_palette(c, size);
    }

    memcpy(frame->data[1], c->pal, AVPALETTE_SIZE);
#if FF_API_PALETTE_HAS_CHANGED
FF_DISABLE_DEPRECATION_WARNINGS
    frame->palette_has_changed = flags & VB_HAS_PALETTE;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    outptr = frame->data[0];
    srcptr = c->frame;

    for (i = 0; i < avctx->height; i++) {
        memcpy(outptr, srcptr, avctx->width);
        srcptr += avctx->width;
        outptr += frame->linesize[0];
    }

    FFSWAP(uint8_t*, c->frame, c->prev_frame);

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    VBDecContext * const c = avctx->priv_data;

    c->avctx       = avctx;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    c->frame      = av_mallocz(avctx->width * avctx->height);
    c->prev_frame = av_mallocz(avctx->width * avctx->height);

    if (!c->frame || !c->prev_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    VBDecContext *c = avctx->priv_data;

    av_freep(&c->frame);
    av_freep(&c->prev_frame);

    return 0;
}

const FFCodec ff_vb_decoder = {
    .p.name         = "vb",
    CODEC_LONG_NAME("Beam Software VB"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VB,
    .priv_data_size = sizeof(VBDecContext),
    .init           = decode_init,
    .close          = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
