/*
 * Go2Webinar decoder
 * Copyright (c) 2012 Konstantin Shishkov
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
 * Go2Webinar decoder
 */

#include <inttypes.h>
#include <zlib.h>

#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "bytestream.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "internal.h"
#include "jpegtables.h"
#include "mjpeg.h"

enum ChunkType {
    DISPLAY_INFO = 0xC8,
    TILE_DATA,
    CURSOR_POS,
    CURSOR_SHAPE,
    CHUNK_CC,
    CHUNK_CD
};

enum Compression {
    COMPR_EPIC_J_B = 2,
    COMPR_KEMPF_J_B,
};

static const uint8_t luma_quant[64] = {
     8,  6,  5,  8, 12, 20, 26, 31,
     6,  6,  7, 10, 13, 29, 30, 28,
     7,  7,  8, 12, 20, 29, 35, 28,
     7,  9, 11, 15, 26, 44, 40, 31,
     9, 11, 19, 28, 34, 55, 52, 39,
    12, 18, 28, 32, 41, 52, 57, 46,
    25, 32, 39, 44, 52, 61, 60, 51,
    36, 46, 48, 49, 56, 50, 52, 50
};

static const uint8_t chroma_quant[64] = {
     9,  9, 12, 24, 50, 50, 50, 50,
     9, 11, 13, 33, 50, 50, 50, 50,
    12, 13, 28, 50, 50, 50, 50, 50,
    24, 33, 50, 50, 50, 50, 50, 50,
    50, 50, 50, 50, 50, 50, 50, 50,
    50, 50, 50, 50, 50, 50, 50, 50,
    50, 50, 50, 50, 50, 50, 50, 50,
    50, 50, 50, 50, 50, 50, 50, 50,
};

typedef struct JPGContext {
    BlockDSPContext bdsp;
    IDCTDSPContext idsp;
    ScanTable  scantable;

    VLC        dc_vlc[2], ac_vlc[2];
    int        prev_dc[3];
    DECLARE_ALIGNED(16, int16_t, block)[6][64];

    uint8_t    *buf;
} JPGContext;

typedef struct G2MContext {
    JPGContext jc;
    int        version;

    int        compression;
    int        width, height, bpp;
    int        orig_width, orig_height;
    int        tile_width, tile_height;
    int        tiles_x, tiles_y, tile_x, tile_y;

    int        got_header;

    uint8_t    *framebuf;
    int        framebuf_stride, old_width, old_height;

    uint8_t    *synth_tile, *jpeg_tile;
    int        tile_stride, old_tile_w, old_tile_h;

    uint8_t    *kempf_buf, *kempf_flags;

    uint8_t    *cursor;
    int        cursor_stride;
    int        cursor_fmt;
    int        cursor_w, cursor_h, cursor_x, cursor_y;
    int        cursor_hot_x, cursor_hot_y;
} G2MContext;

static av_cold int build_vlc(VLC *vlc, const uint8_t *bits_table,
                             const uint8_t *val_table, int nb_codes,
                             int is_ac)
{
    uint8_t  huff_size[256] = { 0 };
    uint16_t huff_code[256];
    uint16_t huff_sym[256];
    int i;

    ff_mjpeg_build_huffman_codes(huff_size, huff_code, bits_table, val_table);

    for (i = 0; i < 256; i++)
        huff_sym[i] = i + 16 * is_ac;

    if (is_ac)
        huff_sym[0] = 16 * 256;

    return ff_init_vlc_sparse(vlc, 9, nb_codes, huff_size, 1, 1,
                              huff_code, 2, 2, huff_sym, 2, 2, 0);
}

static av_cold int jpg_init(AVCodecContext *avctx, JPGContext *c)
{
    int ret;

    ret = build_vlc(&c->dc_vlc[0], avpriv_mjpeg_bits_dc_luminance,
                    avpriv_mjpeg_val_dc, 12, 0);
    if (ret)
        return ret;
    ret = build_vlc(&c->dc_vlc[1], avpriv_mjpeg_bits_dc_chrominance,
                    avpriv_mjpeg_val_dc, 12, 0);
    if (ret)
        return ret;
    ret = build_vlc(&c->ac_vlc[0], avpriv_mjpeg_bits_ac_luminance,
                    avpriv_mjpeg_val_ac_luminance, 251, 1);
    if (ret)
        return ret;
    ret = build_vlc(&c->ac_vlc[1], avpriv_mjpeg_bits_ac_chrominance,
                    avpriv_mjpeg_val_ac_chrominance, 251, 1);
    if (ret)
        return ret;

    ff_blockdsp_init(&c->bdsp, avctx);
    ff_idctdsp_init(&c->idsp, avctx);
    ff_init_scantable(c->idsp.idct_permutation, &c->scantable,
                      ff_zigzag_direct);

    return 0;
}

static av_cold void jpg_free_context(JPGContext *ctx)
{
    int i;

    for (i = 0; i < 2; i++) {
        ff_free_vlc(&ctx->dc_vlc[i]);
        ff_free_vlc(&ctx->ac_vlc[i]);
    }

    av_freep(&ctx->buf);
}

static void jpg_unescape(const uint8_t *src, int src_size,
                         uint8_t *dst, int *dst_size)
{
    const uint8_t *src_end = src + src_size;
    uint8_t *dst_start = dst;

    while (src < src_end) {
        uint8_t x = *src++;

        *dst++ = x;

        if (x == 0xFF && !*src)
            src++;
    }
    *dst_size = dst - dst_start;
}

static int jpg_decode_block(JPGContext *c, GetBitContext *gb,
                            int plane, int16_t *block)
{
    int dc, val, pos;
    const int is_chroma = !!plane;
    const uint8_t *qmat = is_chroma ? chroma_quant : luma_quant;

    c->bdsp.clear_block(block);
    dc = get_vlc2(gb, c->dc_vlc[is_chroma].table, 9, 3);
    if (dc < 0)
        return AVERROR_INVALIDDATA;
    if (dc)
        dc = get_xbits(gb, dc);
    dc                = dc * qmat[0] + c->prev_dc[plane];
    block[0]          = dc;
    c->prev_dc[plane] = dc;

    pos = 0;
    while (pos < 63) {
        val = get_vlc2(gb, c->ac_vlc[is_chroma].table, 9, 3);
        if (val < 0)
            return AVERROR_INVALIDDATA;
        pos += val >> 4;
        val &= 0xF;
        if (pos > 63)
            return val ? AVERROR_INVALIDDATA : 0;
        if (val) {
            int nbits = val;

            val                                 = get_xbits(gb, nbits);
            val                                *= qmat[ff_zigzag_direct[pos]];
            block[c->scantable.permutated[pos]] = val;
        }
    }
    return 0;
}

static inline void yuv2rgb(uint8_t *out, int Y, int U, int V)
{
    out[0] = av_clip_uint8(Y + (             91881 * V + 32768 >> 16));
    out[1] = av_clip_uint8(Y + (-22554 * U - 46802 * V + 32768 >> 16));
    out[2] = av_clip_uint8(Y + (116130 * U             + 32768 >> 16));
}

static int jpg_decode_data(JPGContext *c, int width, int height,
                           const uint8_t *src, int src_size,
                           uint8_t *dst, int dst_stride,
                           const uint8_t *mask, int mask_stride, int num_mbs,
                           int swapuv)
{
    GetBitContext gb;
    int mb_w, mb_h, mb_x, mb_y, i, j;
    int bx, by;
    int unesc_size;
    int ret;

    if ((ret = av_reallocp(&c->buf,
                           src_size + FF_INPUT_BUFFER_PADDING_SIZE)) < 0)
        return ret;
    jpg_unescape(src, src_size, c->buf, &unesc_size);
    memset(c->buf + unesc_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    if((ret = init_get_bits8(&gb, c->buf, unesc_size)) < 0)
        return ret;

    width = FFALIGN(width, 16);
    mb_w  =  width        >> 4;
    mb_h  = (height + 15) >> 4;

    if (!num_mbs)
        num_mbs = mb_w * mb_h * 4;

    for (i = 0; i < 3; i++)
        c->prev_dc[i] = 1024;
    bx =
    by = 0;
    c->bdsp.clear_blocks(c->block[0]);
    for (mb_y = 0; mb_y < mb_h; mb_y++) {
        for (mb_x = 0; mb_x < mb_w; mb_x++) {
            if (mask && !mask[mb_x * 2] && !mask[mb_x * 2 + 1] &&
                !mask[mb_x * 2 +     mask_stride] &&
                !mask[mb_x * 2 + 1 + mask_stride]) {
                bx += 16;
                continue;
            }
            for (j = 0; j < 2; j++) {
                for (i = 0; i < 2; i++) {
                    if (mask && !mask[mb_x * 2 + i + j * mask_stride])
                        continue;
                    num_mbs--;
                    if ((ret = jpg_decode_block(c, &gb, 0,
                                                c->block[i + j * 2])) != 0)
                        return ret;
                    c->idsp.idct(c->block[i + j * 2]);
                }
            }
            for (i = 1; i < 3; i++) {
                if ((ret = jpg_decode_block(c, &gb, i, c->block[i + 3])) != 0)
                    return ret;
                c->idsp.idct(c->block[i + 3]);
            }

            for (j = 0; j < 16; j++) {
                uint8_t *out = dst + bx * 3 + (by + j) * dst_stride;
                for (i = 0; i < 16; i++) {
                    int Y, U, V;

                    Y = c->block[(j >> 3) * 2 + (i >> 3)][(i & 7) + (j & 7) * 8];
                    U = c->block[4 ^ swapuv][(i >> 1) + (j >> 1) * 8] - 128;
                    V = c->block[5 ^ swapuv][(i >> 1) + (j >> 1) * 8] - 128;
                    yuv2rgb(out + i * 3, Y, U, V);
                }
            }

            if (!num_mbs)
                return 0;
            bx += 16;
        }
        bx  = 0;
        by += 16;
        if (mask)
            mask += mask_stride * 2;
    }

    return 0;
}

static int kempf_restore_buf(const uint8_t *src, int len,
                              uint8_t *dst, int stride,
                              const uint8_t *jpeg_tile, int tile_stride,
                              int width, int height,
                              const uint8_t *pal, int npal, int tidx)
{
    GetBitContext gb;
    int i, j, nb, col;
    int ret;

    if ((ret = init_get_bits8(&gb, src, len)) < 0)
        return ret;

    if (npal <= 2)       nb = 1;
    else if (npal <= 4)  nb = 2;
    else if (npal <= 16) nb = 4;
    else                 nb = 8;

    for (j = 0; j < height; j++, dst += stride, jpeg_tile += tile_stride) {
        if (get_bits(&gb, 8))
            continue;
        for (i = 0; i < width; i++) {
            col = get_bits(&gb, nb);
            if (col != tidx)
                memcpy(dst + i * 3, pal + col * 3, 3);
            else
                memcpy(dst + i * 3, jpeg_tile + i * 3, 3);
        }
    }

    return 0;
}

static int kempf_decode_tile(G2MContext *c, int tile_x, int tile_y,
                             const uint8_t *src, int src_size)
{
    int width, height;
    int hdr, zsize, npal, tidx = -1, ret;
    int i, j;
    const uint8_t *src_end = src + src_size;
    uint8_t pal[768], transp[3];
    uLongf dlen = (c->tile_width + 1) * c->tile_height;
    int sub_type;
    int nblocks, cblocks, bstride;
    int bits, bitbuf, coded;
    uint8_t *dst = c->framebuf + tile_x * c->tile_width * 3 +
                   tile_y * c->tile_height * c->framebuf_stride;

    if (src_size < 2)
        return AVERROR_INVALIDDATA;

    width  = FFMIN(c->width  - tile_x * c->tile_width,  c->tile_width);
    height = FFMIN(c->height - tile_y * c->tile_height, c->tile_height);

    hdr      = *src++;
    sub_type = hdr >> 5;
    if (sub_type == 0) {
        int j;
        memcpy(transp, src, 3);
        src += 3;
        for (j = 0; j < height; j++, dst += c->framebuf_stride)
            for (i = 0; i < width; i++)
                memcpy(dst + i * 3, transp, 3);
        return 0;
    } else if (sub_type == 1) {
        return jpg_decode_data(&c->jc, width, height, src, src_end - src,
                               dst, c->framebuf_stride, NULL, 0, 0, 0);
    }

    if (sub_type != 2) {
        memcpy(transp, src, 3);
        src += 3;
    }
    npal = *src++ + 1;
    if (src_end - src < npal * 3)
        return AVERROR_INVALIDDATA;
    memcpy(pal, src, npal * 3);
    src += npal * 3;
    if (sub_type != 2) {
        for (i = 0; i < npal; i++) {
            if (!memcmp(pal + i * 3, transp, 3)) {
                tidx = i;
                break;
            }
        }
    }

    if (src_end - src < 2)
        return 0;
    zsize = (src[0] << 8) | src[1];
    src  += 2;

    if (src_end - src < zsize + (sub_type != 2))
        return AVERROR_INVALIDDATA;

    ret = uncompress(c->kempf_buf, &dlen, src, zsize);
    if (ret)
        return AVERROR_INVALIDDATA;
    src += zsize;

    if (sub_type == 2) {
        kempf_restore_buf(c->kempf_buf, dlen, dst, c->framebuf_stride,
                          NULL, 0, width, height, pal, npal, tidx);
        return 0;
    }

    nblocks = *src++ + 1;
    cblocks = 0;
    bstride = FFALIGN(width, 16) >> 3;
    // blocks are coded LSB and we need normal bitreader for JPEG data
    bits = 0;
    for (i = 0; i < (FFALIGN(height, 16) >> 4); i++) {
        for (j = 0; j < (FFALIGN(width, 16) >> 4); j++) {
            if (!bits) {
                if (src >= src_end)
                    return AVERROR_INVALIDDATA;
                bitbuf = *src++;
                bits   = 8;
            }
            coded = bitbuf & 1;
            bits--;
            bitbuf >>= 1;
            cblocks += coded;
            if (cblocks > nblocks)
                return AVERROR_INVALIDDATA;
            c->kempf_flags[j * 2 +      i * 2      * bstride] =
            c->kempf_flags[j * 2 + 1 +  i * 2      * bstride] =
            c->kempf_flags[j * 2 +     (i * 2 + 1) * bstride] =
            c->kempf_flags[j * 2 + 1 + (i * 2 + 1) * bstride] = coded;
        }
    }

    memset(c->jpeg_tile, 0, c->tile_stride * height);
    jpg_decode_data(&c->jc, width, height, src, src_end - src,
                    c->jpeg_tile, c->tile_stride,
                    c->kempf_flags, bstride, nblocks * 4, 0);

    kempf_restore_buf(c->kempf_buf, dlen, dst, c->framebuf_stride,
                      c->jpeg_tile, c->tile_stride,
                      width, height, pal, npal, tidx);

    return 0;
}

static int g2m_init_buffers(G2MContext *c)
{
    int aligned_height;

    if (!c->framebuf || c->old_width < c->width || c->old_height < c->height) {
        c->framebuf_stride = FFALIGN(c->width + 15, 16) * 3;
        aligned_height     = c->height + 15;
        av_free(c->framebuf);
        c->framebuf = av_mallocz_array(c->framebuf_stride, aligned_height);
        if (!c->framebuf)
            return AVERROR(ENOMEM);
    }
    if (!c->synth_tile || !c->jpeg_tile ||
        c->old_tile_w < c->tile_width ||
        c->old_tile_h < c->tile_height) {
        c->tile_stride = FFALIGN(c->tile_width, 16) * 3;
        aligned_height = FFALIGN(c->tile_height,    16);
        av_free(c->synth_tile);
        av_free(c->jpeg_tile);
        av_free(c->kempf_buf);
        av_free(c->kempf_flags);
        c->synth_tile  = av_mallocz(c->tile_stride      * aligned_height);
        c->jpeg_tile   = av_mallocz(c->tile_stride      * aligned_height);
        c->kempf_buf   = av_mallocz((c->tile_width + 1) * aligned_height
                                    + FF_INPUT_BUFFER_PADDING_SIZE);
        c->kempf_flags = av_mallocz( c->tile_width      * aligned_height);
        if (!c->synth_tile || !c->jpeg_tile ||
            !c->kempf_buf || !c->kempf_flags)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int g2m_load_cursor(AVCodecContext *avctx, G2MContext *c,
                           GetByteContext *gb)
{
    int i, j, k;
    uint8_t *dst;
    uint32_t bits;
    uint32_t cur_size, cursor_w, cursor_h, cursor_stride;
    uint32_t cursor_hot_x, cursor_hot_y;
    int cursor_fmt, err;

    cur_size     = bytestream2_get_be32(gb);
    cursor_w     = bytestream2_get_byte(gb);
    cursor_h     = bytestream2_get_byte(gb);
    cursor_hot_x = bytestream2_get_byte(gb);
    cursor_hot_y = bytestream2_get_byte(gb);
    cursor_fmt   = bytestream2_get_byte(gb);

    cursor_stride = FFALIGN(cursor_w, cursor_fmt==1 ? 32 : 1) * 4;

    if (cursor_w < 1 || cursor_w > 256 ||
        cursor_h < 1 || cursor_h > 256) {
        av_log(avctx, AV_LOG_ERROR, "Invalid cursor dimensions %"PRIu32"x%"PRIu32"\n",
               cursor_w, cursor_h);
        return AVERROR_INVALIDDATA;
    }
    if (cursor_hot_x > cursor_w || cursor_hot_y > cursor_h) {
        av_log(avctx, AV_LOG_WARNING, "Invalid hotspot position %"PRIu32",%"PRIu32"\n",
               cursor_hot_x, cursor_hot_y);
        cursor_hot_x = FFMIN(cursor_hot_x, cursor_w - 1);
        cursor_hot_y = FFMIN(cursor_hot_y, cursor_h - 1);
    }
    if (cur_size - 9 > bytestream2_get_bytes_left(gb) ||
        c->cursor_w * c->cursor_h / 4 > cur_size) {
        av_log(avctx, AV_LOG_ERROR, "Invalid cursor data size %"PRIu32"/%u\n",
               cur_size, bytestream2_get_bytes_left(gb));
        return AVERROR_INVALIDDATA;
    }
    if (cursor_fmt != 1 && cursor_fmt != 32) {
        avpriv_report_missing_feature(avctx, "Cursor format %d",
                                      cursor_fmt);
        return AVERROR_PATCHWELCOME;
    }

    if ((err = av_reallocp(&c->cursor, cursor_stride * cursor_h)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate cursor buffer\n");
        return err;
    }

    c->cursor_w      = cursor_w;
    c->cursor_h      = cursor_h;
    c->cursor_hot_x  = cursor_hot_x;
    c->cursor_hot_y  = cursor_hot_y;
    c->cursor_fmt    = cursor_fmt;
    c->cursor_stride = cursor_stride;

    dst = c->cursor;
    switch (c->cursor_fmt) {
    case 1: // old monochrome
        for (j = 0; j < c->cursor_h; j++) {
            for (i = 0; i < c->cursor_w; i += 32) {
                bits = bytestream2_get_be32(gb);
                for (k = 0; k < 32; k++) {
                    dst[0] = !!(bits & 0x80000000);
                    dst   += 4;
                    bits <<= 1;
                }
            }
        }

        dst = c->cursor;
        for (j = 0; j < c->cursor_h; j++) {
            for (i = 0; i < c->cursor_w; i += 32) {
                bits = bytestream2_get_be32(gb);
                for (k = 0; k < 32; k++) {
                    int mask_bit = !!(bits & 0x80000000);
                    switch (dst[0] * 2 + mask_bit) {
                    case 0:
                        dst[0] = 0xFF;
                        dst[1] = 0x00;
                        dst[2] = 0x00;
                        dst[3] = 0x00;
                        break;
                    case 1:
                        dst[0] = 0xFF;
                        dst[1] = 0xFF;
                        dst[2] = 0xFF;
                        dst[3] = 0xFF;
                        break;
                    default:
                        dst[0] = 0x00;
                        dst[1] = 0x00;
                        dst[2] = 0x00;
                        dst[3] = 0x00;
                    }
                    dst   += 4;
                    bits <<= 1;
                }
            }
        }
        break;
    case 32: // full colour
        /* skip monochrome version of the cursor and decode RGBA instead */
        bytestream2_skip(gb, c->cursor_h * (FFALIGN(c->cursor_w, 32) >> 3));
        for (j = 0; j < c->cursor_h; j++) {
            for (i = 0; i < c->cursor_w; i++) {
                int val = bytestream2_get_be32(gb);
                *dst++ = val >>  0;
                *dst++ = val >>  8;
                *dst++ = val >> 16;
                *dst++ = val >> 24;
            }
        }
        break;
    default:
        return AVERROR_PATCHWELCOME;
    }
    return 0;
}

#define APPLY_ALPHA(src, new, alpha) \
    src = (src * (256 - alpha) + new * alpha) >> 8

static void g2m_paint_cursor(G2MContext *c, uint8_t *dst, int stride)
{
    int i, j;
    int x, y, w, h;
    const uint8_t *cursor;

    if (!c->cursor)
        return;

    x = c->cursor_x - c->cursor_hot_x;
    y = c->cursor_y - c->cursor_hot_y;

    cursor = c->cursor;
    w      = c->cursor_w;
    h      = c->cursor_h;

    if (x + w > c->width)
        w = c->width - x;
    if (y + h > c->height)
        h = c->height - y;
    if (x < 0) {
        w      +=  x;
        cursor += -x * 4;
    } else {
        dst    +=  x * 3;
    }
    if (y < 0) {
        h      +=  y;
        cursor += -y * c->cursor_stride;
    } else {
        dst    +=  y * stride;
    }
    if (w < 0 || h < 0)
        return;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            uint8_t alpha = cursor[i * 4];
            APPLY_ALPHA(dst[i * 3 + 0], cursor[i * 4 + 1], alpha);
            APPLY_ALPHA(dst[i * 3 + 1], cursor[i * 4 + 2], alpha);
            APPLY_ALPHA(dst[i * 3 + 2], cursor[i * 4 + 3], alpha);
        }
        dst    += stride;
        cursor += c->cursor_stride;
    }
}

static int g2m_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_picture_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    G2MContext *c = avctx->priv_data;
    AVFrame *pic = data;
    GetByteContext bc, tbc;
    int magic;
    int got_header = 0;
    uint32_t chunk_size, r_mask, g_mask, b_mask;
    int chunk_type, chunk_start;
    int i;
    int ret;

    if (buf_size < 12) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame should have at least 12 bytes, got %d instead\n",
               buf_size);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init(&bc, buf, buf_size);

    magic = bytestream2_get_be32(&bc);
    if ((magic & ~0xF) != MKBETAG('G', '2', 'M', '0') ||
        (magic & 0xF) < 2 || (magic & 0xF) > 5) {
        av_log(avctx, AV_LOG_ERROR, "Wrong magic %08X\n", magic);
        return AVERROR_INVALIDDATA;
    }

    if ((magic & 0xF) < 4) {
        av_log(avctx, AV_LOG_ERROR, "G2M2 and G2M3 are not yet supported\n");
        return AVERROR(ENOSYS);
    }

    while (bytestream2_get_bytes_left(&bc) > 5) {
        chunk_size  = bytestream2_get_le32(&bc) - 1;
        chunk_type  = bytestream2_get_byte(&bc);
        chunk_start = bytestream2_tell(&bc);
        if (chunk_size > bytestream2_get_bytes_left(&bc)) {
            av_log(avctx, AV_LOG_ERROR, "Invalid chunk size %"PRIu32" type %02X\n",
                   chunk_size, chunk_type);
            break;
        }
        switch (chunk_type) {
        case DISPLAY_INFO:
            got_header =
            c->got_header = 0;
            if (chunk_size < 21) {
                av_log(avctx, AV_LOG_ERROR, "Invalid display info size %"PRIu32"\n",
                       chunk_size);
                break;
            }
            c->width  = bytestream2_get_be32(&bc);
            c->height = bytestream2_get_be32(&bc);
            if (c->width  < 16 || c->width  > c->orig_width ||
                c->height < 16 || c->height > c->orig_height) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid frame dimensions %dx%d\n",
                       c->width, c->height);
                ret = AVERROR_INVALIDDATA;
                goto header_fail;
            }
            if (c->width != avctx->width || c->height != avctx->height) {
                ret = ff_set_dimensions(avctx, c->width, c->height);
                if (ret < 0)
                    goto header_fail;
            }
            c->compression = bytestream2_get_be32(&bc);
            if (c->compression != 2 && c->compression != 3) {
                av_log(avctx, AV_LOG_ERROR,
                       "Unknown compression method %d\n",
                       c->compression);
                ret = AVERROR_PATCHWELCOME;
                goto header_fail;
            }
            c->tile_width  = bytestream2_get_be32(&bc);
            c->tile_height = bytestream2_get_be32(&bc);
            if (c->tile_width <= 0 || c->tile_height <= 0 ||
                ((c->tile_width | c->tile_height) & 0xF) ||
                c->tile_width * 4LL * c->tile_height >= INT_MAX
            ) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid tile dimensions %dx%d\n",
                       c->tile_width, c->tile_height);
                ret = AVERROR_INVALIDDATA;
                goto header_fail;
            }
            c->tiles_x = (c->width  + c->tile_width  - 1) / c->tile_width;
            c->tiles_y = (c->height + c->tile_height - 1) / c->tile_height;
            c->bpp     = bytestream2_get_byte(&bc);
            if (c->bpp == 32) {
                if (bytestream2_get_bytes_left(&bc) < 16 ||
                    (chunk_size - 21) < 16) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Display info: missing bitmasks!\n");
                    ret = AVERROR_INVALIDDATA;
                    goto header_fail;
                }
                r_mask = bytestream2_get_be32(&bc);
                g_mask = bytestream2_get_be32(&bc);
                b_mask = bytestream2_get_be32(&bc);
                if (r_mask != 0xFF0000 || g_mask != 0xFF00 || b_mask != 0xFF) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Invalid or unsupported bitmasks: R=%"PRIX32", G=%"PRIX32", B=%"PRIX32"\n",
                           r_mask, g_mask, b_mask);
                    ret = AVERROR_PATCHWELCOME;
                    goto header_fail;
                }
            } else {
                avpriv_request_sample(avctx, "bpp=%d", c->bpp);
                ret = AVERROR_PATCHWELCOME;
                goto header_fail;
            }
            if (g2m_init_buffers(c)) {
                ret = AVERROR(ENOMEM);
                goto header_fail;
            }
            got_header = 1;
            break;
        case TILE_DATA:
            if (!c->tiles_x || !c->tiles_y) {
                av_log(avctx, AV_LOG_WARNING,
                       "No display info - skipping tile\n");
                break;
            }
            if (chunk_size < 2) {
                av_log(avctx, AV_LOG_ERROR, "Invalid tile data size %"PRIu32"\n",
                       chunk_size);
                break;
            }
            c->tile_x = bytestream2_get_byte(&bc);
            c->tile_y = bytestream2_get_byte(&bc);
            if (c->tile_x >= c->tiles_x || c->tile_y >= c->tiles_y) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid tile pos %d,%d (in %dx%d grid)\n",
                       c->tile_x, c->tile_y, c->tiles_x, c->tiles_y);
                break;
            }
            ret = 0;
            switch (c->compression) {
            case COMPR_EPIC_J_B:
                av_log(avctx, AV_LOG_ERROR,
                       "ePIC j-b compression is not implemented yet\n");
                return AVERROR(ENOSYS);
            case COMPR_KEMPF_J_B:
                ret = kempf_decode_tile(c, c->tile_x, c->tile_y,
                                        buf + bytestream2_tell(&bc),
                                        chunk_size - 2);
                break;
            }
            if (ret && c->framebuf)
                av_log(avctx, AV_LOG_ERROR, "Error decoding tile %d,%d\n",
                       c->tile_x, c->tile_y);
            break;
        case CURSOR_POS:
            if (chunk_size < 5) {
                av_log(avctx, AV_LOG_ERROR, "Invalid cursor pos size %"PRIu32"\n",
                       chunk_size);
                break;
            }
            c->cursor_x = bytestream2_get_be16(&bc);
            c->cursor_y = bytestream2_get_be16(&bc);
            break;
        case CURSOR_SHAPE:
            if (chunk_size < 8) {
                av_log(avctx, AV_LOG_ERROR, "Invalid cursor data size %"PRIu32"\n",
                       chunk_size);
                break;
            }
            bytestream2_init(&tbc, buf + bytestream2_tell(&bc),
                             chunk_size - 4);
            g2m_load_cursor(avctx, c, &tbc);
            break;
        case CHUNK_CC:
        case CHUNK_CD:
            break;
        default:
            av_log(avctx, AV_LOG_WARNING, "Skipping chunk type %02d\n",
                   chunk_type);
        }

        /* navigate to next chunk */
        bytestream2_skip(&bc, chunk_start + chunk_size - bytestream2_tell(&bc));
    }
    if (got_header)
        c->got_header = 1;

    if (c->width && c->height && c->framebuf) {
        if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
            return ret;

        pic->key_frame = got_header;
        pic->pict_type = got_header ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

        for (i = 0; i < avctx->height; i++)
            memcpy(pic->data[0] + i * pic->linesize[0],
                   c->framebuf + i * c->framebuf_stride,
                   c->width * 3);
        g2m_paint_cursor(c, pic->data[0], pic->linesize[0]);

        *got_picture_ptr = 1;
    }

    return buf_size;

header_fail:
    c->width   =
    c->height  = 0;
    c->tiles_x =
    c->tiles_y = 0;
    return ret;
}

static av_cold int g2m_decode_init(AVCodecContext *avctx)
{
    G2MContext *const c = avctx->priv_data;
    int ret;

    if ((ret = jpg_init(avctx, &c->jc)) != 0) {
        av_log(avctx, AV_LOG_ERROR, "Cannot initialise VLCs\n");
        jpg_free_context(&c->jc);
        return AVERROR(ENOMEM);
    }

    avctx->pix_fmt = AV_PIX_FMT_RGB24;

    // store original sizes and check against those if resize happens
    c->orig_width  = avctx->width;
    c->orig_height = avctx->height;

    return 0;
}

static av_cold int g2m_decode_end(AVCodecContext *avctx)
{
    G2MContext *const c = avctx->priv_data;

    jpg_free_context(&c->jc);

    av_freep(&c->kempf_buf);
    av_freep(&c->kempf_flags);
    av_freep(&c->synth_tile);
    av_freep(&c->jpeg_tile);
    av_freep(&c->cursor);
    av_freep(&c->framebuf);

    return 0;
}

AVCodec ff_g2m_decoder = {
    .name           = "g2m",
    .long_name      = NULL_IF_CONFIG_SMALL("Go2Meeting"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_G2M,
    .priv_data_size = sizeof(G2MContext),
    .init           = g2m_decode_init,
    .close          = g2m_decode_end,
    .decode         = g2m_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
