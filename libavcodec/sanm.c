/*
 * LucasArts Smush video decoder
 * Copyright (c) 2006 Cyril Zorin
 * Copyright (c) 2011 Konstantin Shishkov
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

// #define DEBUG 1

#include "avcodec.h"
#include "copy_block.h"
#include "bytestream.h"
#include "internal.h"
#include "libavutil/bswap.h"
#include "libavutil/imgutils.h"
#include "sanm_data.h"
#include "libavutil/avassert.h"

#define NGLYPHS 256

typedef struct {
    AVCodecContext *avctx;
    GetByteContext gb;

    int version, subversion;
    uint32_t pal[256];
    int16_t delta_pal[768];

    int pitch;
    int width, height;
    int aligned_width, aligned_height;
    int prev_seq;

    AVFrame *frame;
    uint16_t *frm0, *frm1, *frm2;
    uint8_t *stored_frame;
    uint32_t frm0_size, frm1_size, frm2_size;
    uint32_t stored_frame_size;

    uint8_t *rle_buf;
    unsigned int rle_buf_size;

    int rotate_code;

    long npixels, buf_size;

    uint16_t codebook[256];
    uint16_t small_codebook[4];

    int8_t p4x4glyphs[NGLYPHS][16];
    int8_t p8x8glyphs[NGLYPHS][64];
} SANMVideoContext;

typedef struct {
    int seq_num, codec, rotate_code, rle_output_size;

    uint16_t bg_color;
    uint32_t width, height;
} SANMFrameHeader;

enum GlyphEdge {
    LEFT_EDGE,
    TOP_EDGE,
    RIGHT_EDGE,
    BOTTOM_EDGE,
    NO_EDGE
};

enum GlyphDir {
    DIR_LEFT,
    DIR_UP,
    DIR_RIGHT,
    DIR_DOWN,
    NO_DIR
};

/**
 * Return enum GlyphEdge of box where point (x, y) lies.
 *
 * @param x x point coordinate
 * @param y y point coordinate
 * @param edge_size box width/height.
 */
static enum GlyphEdge which_edge(int x, int y, int edge_size)
{
    const int edge_max = edge_size - 1;

    if (!y) {
        return BOTTOM_EDGE;
    } else if (y == edge_max) {
        return TOP_EDGE;
    } else if (!x) {
        return LEFT_EDGE;
    } else if (x == edge_max) {
        return RIGHT_EDGE;
    } else {
        return NO_EDGE;
    }
}

static enum GlyphDir which_direction(enum GlyphEdge edge0, enum GlyphEdge edge1)
{
    if ((edge0 == LEFT_EDGE && edge1 == RIGHT_EDGE) ||
        (edge1 == LEFT_EDGE && edge0 == RIGHT_EDGE) ||
        (edge0 == BOTTOM_EDGE && edge1 != TOP_EDGE) ||
        (edge1 == BOTTOM_EDGE && edge0 != TOP_EDGE)) {
        return DIR_UP;
    } else if ((edge0 == TOP_EDGE && edge1 != BOTTOM_EDGE) ||
               (edge1 == TOP_EDGE && edge0 != BOTTOM_EDGE)) {
        return DIR_DOWN;
    } else if ((edge0 == LEFT_EDGE && edge1 != RIGHT_EDGE) ||
               (edge1 == LEFT_EDGE && edge0 != RIGHT_EDGE)) {
        return DIR_LEFT;
    } else if ((edge0 == TOP_EDGE && edge1 == BOTTOM_EDGE) ||
               (edge1 == TOP_EDGE && edge0 == BOTTOM_EDGE) ||
               (edge0 == RIGHT_EDGE && edge1 != LEFT_EDGE) ||
               (edge1 == RIGHT_EDGE && edge0 != LEFT_EDGE)) {
        return DIR_RIGHT;
    }

    return NO_DIR;
}

/**
 * Interpolate two points.
 */
static void interp_point(int8_t *points, int x0, int y0, int x1, int y1,
                         int pos, int npoints)
{
    if (npoints) {
        points[0] = (x0 * pos + x1 * (npoints - pos) + (npoints >> 1)) / npoints;
        points[1] = (y0 * pos + y1 * (npoints - pos) + (npoints >> 1)) / npoints;
    } else {
        points[0] = x0;
        points[1] = y0;
    }
}

/**
 * Construct glyphs by iterating through vectors coordinates.
 *
 * @param pglyphs pointer to table where glyphs are stored
 * @param xvec pointer to x component of vectors coordinates
 * @param yvec pointer to y component of vectors coordinates
 * @param side_length glyph width/height.
 */
static void make_glyphs(int8_t *pglyphs, const int8_t *xvec, const int8_t *yvec,
                        const int side_length)
{
    const int glyph_size = side_length * side_length;
    int8_t *pglyph = pglyphs;

    int i, j;
    for (i = 0; i < GLYPH_COORD_VECT_SIZE; i++) {
        int x0    = xvec[i];
        int y0    = yvec[i];
        enum GlyphEdge edge0 = which_edge(x0, y0, side_length);

        for (j = 0; j < GLYPH_COORD_VECT_SIZE; j++, pglyph += glyph_size) {
            int x1      = xvec[j];
            int y1      = yvec[j];
            enum GlyphEdge edge1   = which_edge(x1, y1, side_length);
            enum GlyphDir  dir     = which_direction(edge0, edge1);
            int npoints = FFMAX(FFABS(x1 - x0), FFABS(y1 - y0));
            int ipoint;

            for (ipoint = 0; ipoint <= npoints; ipoint++) {
                int8_t point[2];
                int irow, icol;

                interp_point(point, x0, y0, x1, y1, ipoint, npoints);

                switch (dir) {
                case DIR_UP:
                    for (irow = point[1]; irow >= 0; irow--)
                        pglyph[point[0] + irow * side_length] = 1;
                    break;

                case DIR_DOWN:
                    for (irow = point[1]; irow < side_length; irow++)
                        pglyph[point[0] + irow * side_length] = 1;
                    break;

                case DIR_LEFT:
                    for (icol = point[0]; icol >= 0; icol--)
                        pglyph[icol + point[1] * side_length] = 1;
                    break;

                case DIR_RIGHT:
                    for (icol = point[0]; icol < side_length; icol++)
                        pglyph[icol + point[1] * side_length] = 1;
                    break;
                }
            }
        }
    }
}

static void init_sizes(SANMVideoContext *ctx, int width, int height)
{
    ctx->width   = width;
    ctx->height  = height;
    ctx->npixels = width * height;

    ctx->aligned_width  = FFALIGN(width,  8);
    ctx->aligned_height = FFALIGN(height, 8);

    ctx->buf_size = ctx->aligned_width * ctx->aligned_height * sizeof(ctx->frm0[0]);
    ctx->pitch    = width;
}

static void destroy_buffers(SANMVideoContext *ctx)
{
    av_freep(&ctx->frm0);
    av_freep(&ctx->frm1);
    av_freep(&ctx->frm2);
    av_freep(&ctx->stored_frame);
    av_freep(&ctx->rle_buf);
    ctx->frm0_size =
    ctx->frm1_size =
    ctx->frm2_size = 0;
}

static av_cold int init_buffers(SANMVideoContext *ctx)
{
    av_fast_padded_malloc(&ctx->frm0, &ctx->frm0_size, ctx->buf_size);
    av_fast_padded_malloc(&ctx->frm1, &ctx->frm1_size, ctx->buf_size);
    av_fast_padded_malloc(&ctx->frm2, &ctx->frm2_size, ctx->buf_size);
    if (!ctx->version)
        av_fast_padded_malloc(&ctx->stored_frame, &ctx->stored_frame_size, ctx->buf_size);

    if (!ctx->frm0 || !ctx->frm1 || !ctx->frm2 || (!ctx->stored_frame && !ctx->version)) {
        destroy_buffers(ctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static void rotate_bufs(SANMVideoContext *ctx, int rotate_code)
{
    av_dlog(ctx->avctx, "rotate %d\n", rotate_code);
    if (rotate_code == 2)
        FFSWAP(uint16_t*, ctx->frm1, ctx->frm2);
    FFSWAP(uint16_t*, ctx->frm2, ctx->frm0);
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    SANMVideoContext *ctx = avctx->priv_data;

    ctx->avctx     = avctx;
    ctx->version   = !avctx->extradata_size;

    avctx->pix_fmt = ctx->version ? AV_PIX_FMT_RGB565 : AV_PIX_FMT_PAL8;

    init_sizes(ctx, avctx->width, avctx->height);
    if (init_buffers(ctx)) {
        av_log(avctx, AV_LOG_ERROR, "error allocating buffers\n");
        return AVERROR(ENOMEM);
    }

    make_glyphs(ctx->p4x4glyphs[0], glyph4_x, glyph4_y, 4);
    make_glyphs(ctx->p8x8glyphs[0], glyph8_x, glyph8_y, 8);

    if (!ctx->version) {
        int i;

        if (avctx->extradata_size < 1026) {
            av_log(avctx, AV_LOG_ERROR, "not enough extradata\n");
            return AVERROR_INVALIDDATA;
        }

        ctx->subversion = AV_RL16(avctx->extradata);
        for (i = 0; i < 256; i++)
            ctx->pal[i] = 0xFFU << 24 | AV_RL32(avctx->extradata + 2 + i * 4);
    }

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    SANMVideoContext *ctx = avctx->priv_data;

    destroy_buffers(ctx);

    return 0;
}

static int rle_decode(SANMVideoContext *ctx, uint8_t *dst, const int out_size)
{
    int opcode, color, run_len, left = out_size;

    while (left > 0) {
        opcode = bytestream2_get_byte(&ctx->gb);
        run_len = (opcode >> 1) + 1;
        if (run_len > left || bytestream2_get_bytes_left(&ctx->gb) <= 0)
            return AVERROR_INVALIDDATA;

        if (opcode & 1) {
            color = bytestream2_get_byte(&ctx->gb);
            memset(dst, color, run_len);
        } else {
            if (bytestream2_get_bytes_left(&ctx->gb) < run_len)
                return AVERROR_INVALIDDATA;
            bytestream2_get_bufferu(&ctx->gb, dst, run_len);
        }

        dst  += run_len;
        left -= run_len;
    }

    return 0;
}

static int old_codec1(SANMVideoContext *ctx, int top,
                      int left, int width, int height)
{
    uint8_t *dst = ((uint8_t*)ctx->frm0) + left + top * ctx->pitch;
    int i, j, len, flag, code, val, pos, end;

    for (i = 0; i < height; i++) {
        pos = 0;

        if (bytestream2_get_bytes_left(&ctx->gb) < 2)
            return AVERROR_INVALIDDATA;

        len = bytestream2_get_le16u(&ctx->gb);
        end = bytestream2_tell(&ctx->gb) + len;

        while (bytestream2_tell(&ctx->gb) < end) {
            if (bytestream2_get_bytes_left(&ctx->gb) < 2)
                return AVERROR_INVALIDDATA;

            code = bytestream2_get_byteu(&ctx->gb);
            flag = code & 1;
            code = (code >> 1) + 1;
            if (pos + code > width)
                return AVERROR_INVALIDDATA;
            if (flag) {
                val = bytestream2_get_byteu(&ctx->gb);
                if (val)
                    memset(dst + pos, val, code);
                pos += code;
            } else {
                if (bytestream2_get_bytes_left(&ctx->gb) < code)
                    return AVERROR_INVALIDDATA;
                for (j = 0; j < code; j++) {
                    val = bytestream2_get_byteu(&ctx->gb);
                    if (val)
                        dst[pos] = val;
                    pos++;
                }
            }
        }
        dst += ctx->pitch;
    }
    ctx->rotate_code = 0;

    return 0;
}

static inline void codec37_mv(uint8_t *dst, const uint8_t *src,
                              int height, int stride, int x, int y)
{
    int pos, i, j;

    pos = x + y * stride;
    for (j = 0; j < 4; j++) {
        for (i = 0; i < 4; i++) {
            if ((pos + i) < 0 || (pos + i) >= height * stride)
                dst[i] = 0;
            else
                dst[i] = src[i];
        }
        dst += stride;
        src += stride;
        pos += stride;
    }
}

static int old_codec37(SANMVideoContext *ctx, int top,
                       int left, int width, int height)
{
    int stride = ctx->pitch;
    int i, j, k, t;
    int skip_run = 0;
    int compr, mvoff, seq, flags;
    uint32_t decoded_size;
    uint8_t *dst, *prev;

    compr        = bytestream2_get_byte(&ctx->gb);
    mvoff        = bytestream2_get_byte(&ctx->gb);
    seq          = bytestream2_get_le16(&ctx->gb);
    decoded_size = bytestream2_get_le32(&ctx->gb);
    bytestream2_skip(&ctx->gb, 4);
    flags        = bytestream2_get_byte(&ctx->gb);
    bytestream2_skip(&ctx->gb, 3);

    if (decoded_size > ctx->height * stride - left - top * stride) {
        decoded_size = ctx->height * stride - left - top * stride;
        av_log(ctx->avctx, AV_LOG_WARNING, "decoded size is too large\n");
    }

    ctx->rotate_code = 0;

    if (((seq & 1) || !(flags & 1)) && (compr && compr != 2))
        rotate_bufs(ctx, 1);

    dst  = ((uint8_t*)ctx->frm0) + left + top * stride;
    prev = ((uint8_t*)ctx->frm2) + left + top * stride;

    if (mvoff > 2) {
        av_log(ctx->avctx, AV_LOG_ERROR, "invalid motion base value %d\n", mvoff);
        return AVERROR_INVALIDDATA;
    }
    av_dlog(ctx->avctx, "compression %d\n", compr);
    switch (compr) {
    case 0:
        for (i = 0; i < height; i++) {
            bytestream2_get_buffer(&ctx->gb, dst, width);
            dst += stride;
        }
        memset(ctx->frm1, 0, ctx->height * stride);
        memset(ctx->frm2, 0, ctx->height * stride);
        break;
    case 2:
        if (rle_decode(ctx, dst, decoded_size))
            return AVERROR_INVALIDDATA;
        memset(ctx->frm1, 0, ctx->frm1_size);
        memset(ctx->frm2, 0, ctx->frm2_size);
        break;
    case 3:
    case 4:
        if (flags & 4) {
            for (j = 0; j < height; j += 4) {
                for (i = 0; i < width; i += 4) {
                    int code;
                    if (skip_run) {
                        skip_run--;
                        copy_block4(dst + i, prev + i, stride, stride, 4);
                        continue;
                    }
                    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                        return AVERROR_INVALIDDATA;
                    code = bytestream2_get_byteu(&ctx->gb);
                    switch (code) {
                    case 0xFF:
                        if (bytestream2_get_bytes_left(&ctx->gb) < 16)
                            return AVERROR_INVALIDDATA;
                        for (k = 0; k < 4; k++)
                            bytestream2_get_bufferu(&ctx->gb, dst + i + k * stride, 4);
                        break;
                    case 0xFE:
                        if (bytestream2_get_bytes_left(&ctx->gb) < 4)
                            return AVERROR_INVALIDDATA;
                        for (k = 0; k < 4; k++)
                            memset(dst + i + k * stride, bytestream2_get_byteu(&ctx->gb), 4);
                        break;
                    case 0xFD:
                        if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                            return AVERROR_INVALIDDATA;
                        t = bytestream2_get_byteu(&ctx->gb);
                        for (k = 0; k < 4; k++)
                            memset(dst + i + k * stride, t, 4);
                        break;
                    default:
                        if (compr == 4 && !code) {
                            if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                                return AVERROR_INVALIDDATA;
                            skip_run = bytestream2_get_byteu(&ctx->gb) + 1;
                            i -= 4;
                        } else {
                            int mx, my;

                            mx = c37_mv[(mvoff * 255 + code) * 2    ];
                            my = c37_mv[(mvoff * 255 + code) * 2 + 1];
                            codec37_mv(dst + i, prev + i + mx + my * stride,
                                       ctx->height, stride, i + mx, j + my);
                        }
                    }
                }
                dst  += stride * 4;
                prev += stride * 4;
            }
        } else {
            for (j = 0; j < height; j += 4) {
                for (i = 0; i < width; i += 4) {
                    int code;
                    if (skip_run) {
                        skip_run--;
                        copy_block4(dst + i, prev + i, stride, stride, 4);
                        continue;
                    }
                    code = bytestream2_get_byte(&ctx->gb);
                    if (code == 0xFF) {
                        if (bytestream2_get_bytes_left(&ctx->gb) < 16)
                            return AVERROR_INVALIDDATA;
                        for (k = 0; k < 4; k++)
                            bytestream2_get_bufferu(&ctx->gb, dst + i + k * stride, 4);
                    } else if (compr == 4 && !code) {
                        if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                            return AVERROR_INVALIDDATA;
                        skip_run = bytestream2_get_byteu(&ctx->gb) + 1;
                        i -= 4;
                    } else {
                        int mx, my;

                        mx = c37_mv[(mvoff * 255 + code) * 2];
                        my = c37_mv[(mvoff * 255 + code) * 2 + 1];
                        codec37_mv(dst + i, prev + i + mx + my * stride,
                                   ctx->height, stride, i + mx, j + my);
                    }
                }
                dst  += stride * 4;
                prev += stride * 4;
            }
        }
        break;
    default:
        av_log(ctx->avctx, AV_LOG_ERROR,
               "subcodec 37 compression %d not implemented\n", compr);
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static int process_block(SANMVideoContext *ctx, uint8_t *dst, uint8_t *prev1,
                         uint8_t *prev2, int stride, int tbl, int size)
{
    int code, k, t;
    uint8_t colors[2];
    int8_t *pglyph;

    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
        return AVERROR_INVALIDDATA;

    code = bytestream2_get_byteu(&ctx->gb);
    if (code >= 0xF8) {
        switch (code) {
        case 0xFF:
            if (size == 2) {
                if (bytestream2_get_bytes_left(&ctx->gb) < 4)
                    return AVERROR_INVALIDDATA;
                dst[0]        = bytestream2_get_byteu(&ctx->gb);
                dst[1]        = bytestream2_get_byteu(&ctx->gb);
                dst[0+stride] = bytestream2_get_byteu(&ctx->gb);
                dst[1+stride] = bytestream2_get_byteu(&ctx->gb);
            } else {
                size >>= 1;
                if (process_block(ctx, dst, prev1, prev2, stride, tbl, size))
                    return AVERROR_INVALIDDATA;
                if (process_block(ctx, dst + size, prev1 + size, prev2 + size,
                                  stride, tbl, size))
                    return AVERROR_INVALIDDATA;
                dst   += size * stride;
                prev1 += size * stride;
                prev2 += size * stride;
                if (process_block(ctx, dst, prev1, prev2, stride, tbl, size))
                    return AVERROR_INVALIDDATA;
                if (process_block(ctx, dst + size, prev1 + size, prev2 + size,
                                  stride, tbl, size))
                    return AVERROR_INVALIDDATA;
            }
            break;
        case 0xFE:
            if (bytestream2_get_bytes_left(&ctx->gb) < 1)
                return AVERROR_INVALIDDATA;

            t = bytestream2_get_byteu(&ctx->gb);
            for (k = 0; k < size; k++)
                memset(dst + k * stride, t, size);
            break;
        case 0xFD:
            if (bytestream2_get_bytes_left(&ctx->gb) < 3)
                return AVERROR_INVALIDDATA;

            code = bytestream2_get_byteu(&ctx->gb);
            pglyph = (size == 8) ? ctx->p8x8glyphs[code] : ctx->p4x4glyphs[code];
            bytestream2_get_bufferu(&ctx->gb, colors, 2);

            for (k = 0; k < size; k++)
                for (t = 0; t < size; t++)
                    dst[t + k * stride] = colors[!*pglyph++];
            break;
        case 0xFC:
            for (k = 0; k < size; k++)
                memcpy(dst + k * stride, prev1 + k * stride, size);
            break;
        default:
            k = bytestream2_tell(&ctx->gb);
            bytestream2_seek(&ctx->gb, tbl + (code & 7), SEEK_SET);
            t = bytestream2_get_byte(&ctx->gb);
            bytestream2_seek(&ctx->gb, k, SEEK_SET);
            for (k = 0; k < size; k++)
                memset(dst + k * stride, t, size);
        }
    } else {
        int mx = motion_vectors[code][0];
        int my = motion_vectors[code][1];
        int index = prev2 - (const uint8_t*)ctx->frm2;

        av_assert2(index >= 0 && index < (ctx->buf_size>>1));

        if (index < - mx - my*stride ||
            (ctx->buf_size>>1) - index < mx + size + (my + size - 1)*stride) {
            av_log(ctx->avctx, AV_LOG_ERROR, "MV is invalid \n");
            return AVERROR_INVALIDDATA;
        }

        for (k = 0; k < size; k++)
            memcpy(dst + k * stride, prev2 + mx + (my + k) * stride, size);
    }

    return 0;
}

static int old_codec47(SANMVideoContext *ctx, int top,
                       int left, int width, int height)
{
    int i, j, seq, compr, new_rot, tbl_pos, skip;
    int stride     = ctx->pitch;
    uint8_t *dst   = ((uint8_t*)ctx->frm0) + left + top * stride;
    uint8_t *prev1 = (uint8_t*)ctx->frm1;
    uint8_t *prev2 = (uint8_t*)ctx->frm2;
    uint32_t decoded_size;

    tbl_pos = bytestream2_tell(&ctx->gb);
    seq     = bytestream2_get_le16(&ctx->gb);
    compr   = bytestream2_get_byte(&ctx->gb);
    new_rot = bytestream2_get_byte(&ctx->gb);
    skip    = bytestream2_get_byte(&ctx->gb);
    bytestream2_skip(&ctx->gb, 9);
    decoded_size = bytestream2_get_le32(&ctx->gb);
    bytestream2_skip(&ctx->gb, 8);

    if (decoded_size > ctx->height * stride - left - top * stride) {
        decoded_size = ctx->height * stride - left - top * stride;
        av_log(ctx->avctx, AV_LOG_WARNING, "decoded size is too large\n");
    }

    if (skip & 1)
        bytestream2_skip(&ctx->gb, 0x8080);
    if (!seq) {
        ctx->prev_seq = -1;
        memset(prev1, 0, ctx->height * stride);
        memset(prev2, 0, ctx->height * stride);
    }
    av_dlog(ctx->avctx, "compression %d\n", compr);
    switch (compr) {
    case 0:
        if (bytestream2_get_bytes_left(&ctx->gb) < width * height)
            return AVERROR_INVALIDDATA;
        for (j = 0; j < height; j++) {
            bytestream2_get_bufferu(&ctx->gb, dst, width);
            dst += stride;
        }
        break;
    case 1:
        if (bytestream2_get_bytes_left(&ctx->gb) < ((width + 1) >> 1) * ((height + 1) >> 1))
            return AVERROR_INVALIDDATA;
        for (j = 0; j < height; j += 2) {
            for (i = 0; i < width; i += 2) {
                dst[i] = dst[i + 1] =
                dst[stride + i] = dst[stride + i + 1] = bytestream2_get_byteu(&ctx->gb);
            }
            dst += stride * 2;
        }
        break;
    case 2:
        if (seq == ctx->prev_seq + 1) {
            for (j = 0; j < height; j += 8) {
                for (i = 0; i < width; i += 8) {
                    if (process_block(ctx, dst + i, prev1 + i, prev2 + i, stride,
                                      tbl_pos + 8, 8))
                        return AVERROR_INVALIDDATA;
                }
                dst   += stride * 8;
                prev1 += stride * 8;
                prev2 += stride * 8;
            }
        }
        break;
    case 3:
        memcpy(ctx->frm0, ctx->frm2, ctx->pitch * ctx->height);
        break;
    case 4:
        memcpy(ctx->frm0, ctx->frm1, ctx->pitch * ctx->height);
        break;
    case 5:
        if (rle_decode(ctx, dst, decoded_size))
            return AVERROR_INVALIDDATA;
        break;
    default:
        av_log(ctx->avctx, AV_LOG_ERROR,
               "subcodec 47 compression %d not implemented\n", compr);
        return AVERROR_PATCHWELCOME;
    }
    if (seq == ctx->prev_seq + 1)
        ctx->rotate_code = new_rot;
    else
        ctx->rotate_code = 0;
    ctx->prev_seq = seq;

    return 0;
}

static int process_frame_obj(SANMVideoContext *ctx)
{
    uint16_t codec, top, left, w, h;

    codec = bytestream2_get_le16u(&ctx->gb);
    left  = bytestream2_get_le16u(&ctx->gb);
    top   = bytestream2_get_le16u(&ctx->gb);
    w     = bytestream2_get_le16u(&ctx->gb);
    h     = bytestream2_get_le16u(&ctx->gb);

    if (!w || !h) {
        av_log(ctx->avctx, AV_LOG_ERROR, "dimensions are invalid\n");
        return AVERROR_INVALIDDATA;
    }

    if (ctx->width < left + w || ctx->height < top + h) {
        if (av_image_check_size(FFMAX(left + w, ctx->width),
                                FFMAX(top  + h, ctx->height), 0, ctx->avctx) < 0)
            return AVERROR_INVALIDDATA;
        avcodec_set_dimensions(ctx->avctx, FFMAX(left + w, ctx->width),
                                           FFMAX(top  + h, ctx->height));
        init_sizes(ctx, FFMAX(left + w, ctx->width),
                        FFMAX(top  + h, ctx->height));
        if (init_buffers(ctx)) {
            av_log(ctx->avctx, AV_LOG_ERROR, "error resizing buffers\n");
            return AVERROR(ENOMEM);
        }
    }
    bytestream2_skip(&ctx->gb, 4);

    av_dlog(ctx->avctx, "subcodec %d\n", codec);
    switch (codec) {
    case 1:
    case 3:
        return old_codec1(ctx, top, left, w, h);
        break;
    case 37:
        return old_codec37(ctx, top, left, w, h);
        break;
    case 47:
        return old_codec47(ctx, top, left, w, h);
        break;
    default:
        avpriv_request_sample(ctx->avctx, "unknown subcodec %d", codec);
        return AVERROR_PATCHWELCOME;
    }
}

static int decode_0(SANMVideoContext *ctx)
{
    uint16_t *frm = ctx->frm0;
    int x, y;

    if (bytestream2_get_bytes_left(&ctx->gb) < ctx->width * ctx->height * 2) {
        av_log(ctx->avctx, AV_LOG_ERROR, "insufficient data for raw frame\n");
        return AVERROR_INVALIDDATA;
    }
    for (y = 0; y < ctx->height; y++) {
        for (x = 0; x < ctx->width; x++)
            frm[x] = bytestream2_get_le16u(&ctx->gb);
        frm += ctx->pitch;
    }
    return 0;
}

static int decode_nop(SANMVideoContext *ctx)
{
    avpriv_request_sample(ctx->avctx, "unknown/unsupported compression type");
    return AVERROR_PATCHWELCOME;
}

static void copy_block(uint16_t *pdest, uint16_t *psrc, int block_size, int pitch)
{
    uint8_t *dst = (uint8_t *)pdest;
    uint8_t *src = (uint8_t *)psrc;
    int stride = pitch * 2;

    switch (block_size) {
    case 2:
        copy_block4(dst, src, stride, stride, 2);
        break;
    case 4:
        copy_block8(dst, src, stride, stride, 4);
        break;
    case 8:
        copy_block16(dst, src, stride, stride, 8);
        break;
    }
}

static void fill_block(uint16_t *pdest, uint16_t color, int block_size, int pitch)
{
    int x, y;

    pitch -= block_size;
    for (y = 0; y < block_size; y++, pdest += pitch)
        for (x = 0; x < block_size; x++)
            *pdest++ = color;
}

static int draw_glyph(SANMVideoContext *ctx, uint16_t *dst, int index, uint16_t fg_color,
                      uint16_t bg_color, int block_size, int pitch)
{
    int8_t *pglyph;
    uint16_t colors[2] = { fg_color, bg_color };
    int x, y;

    if (index >= NGLYPHS) {
        av_log(ctx->avctx, AV_LOG_ERROR, "ignoring nonexistent glyph #%u\n", index);
        return AVERROR_INVALIDDATA;
    }

    pglyph = block_size == 8 ? ctx->p8x8glyphs[index] : ctx->p4x4glyphs[index];
    pitch -= block_size;

    for (y = 0; y < block_size; y++, dst += pitch)
        for (x = 0; x < block_size; x++)
            *dst++ = colors[*pglyph++];
    return 0;
}

static int opcode_0xf7(SANMVideoContext *ctx, int cx, int cy, int block_size, int pitch)
{
    uint16_t *dst = ctx->frm0 + cx + cy * ctx->pitch;

    if (block_size == 2) {
        uint32_t indices;

        if (bytestream2_get_bytes_left(&ctx->gb) < 4)
            return AVERROR_INVALIDDATA;

        indices        = bytestream2_get_le32u(&ctx->gb);
        dst[0]         = ctx->codebook[indices & 0xFF]; indices >>= 8;
        dst[1]         = ctx->codebook[indices & 0xFF]; indices >>= 8;
        dst[pitch]     = ctx->codebook[indices & 0xFF]; indices >>= 8;
        dst[pitch + 1] = ctx->codebook[indices & 0xFF];
    } else {
        uint16_t fgcolor, bgcolor;
        int glyph;

        if (bytestream2_get_bytes_left(&ctx->gb) < 3)
            return AVERROR_INVALIDDATA;

        glyph   = bytestream2_get_byteu(&ctx->gb);
        bgcolor = ctx->codebook[bytestream2_get_byteu(&ctx->gb)];
        fgcolor = ctx->codebook[bytestream2_get_byteu(&ctx->gb)];

        draw_glyph(ctx, dst, glyph, fgcolor, bgcolor, block_size, pitch);
    }
    return 0;
}

static int opcode_0xf8(SANMVideoContext *ctx, int cx, int cy, int block_size, int pitch)
{
    uint16_t *dst = ctx->frm0 + cx + cy * ctx->pitch;

    if (block_size == 2) {
        if (bytestream2_get_bytes_left(&ctx->gb) < 8)
            return AVERROR_INVALIDDATA;

        dst[0]         = bytestream2_get_le16u(&ctx->gb);
        dst[1]         = bytestream2_get_le16u(&ctx->gb);
        dst[pitch]     = bytestream2_get_le16u(&ctx->gb);
        dst[pitch + 1] = bytestream2_get_le16u(&ctx->gb);
    } else {
        uint16_t fgcolor, bgcolor;
        int glyph;

        if (bytestream2_get_bytes_left(&ctx->gb) < 5)
            return AVERROR_INVALIDDATA;

        glyph   = bytestream2_get_byteu(&ctx->gb);
        bgcolor = bytestream2_get_le16u(&ctx->gb);
        fgcolor = bytestream2_get_le16u(&ctx->gb);

        draw_glyph(ctx, dst, glyph, fgcolor, bgcolor, block_size, pitch);
    }
    return 0;
}

static int good_mvec(SANMVideoContext *ctx, int cx, int cy, int mx, int my,
                     int block_size)
{
    int start_pos = cx + mx + (cy + my) * ctx->pitch;
    int end_pos = start_pos + (block_size - 1) * (ctx->pitch + 1);

    int good = start_pos >= 0 && end_pos < (ctx->buf_size >> 1);

    if (!good) {
        av_log(ctx->avctx, AV_LOG_ERROR, "ignoring invalid motion vector (%i, %i)->(%u, %u), block size = %u\n",
               cx + mx, cy + my, cx, cy, block_size);
    }

    return good;
}

static int codec2subblock(SANMVideoContext *ctx, int cx, int cy, int blk_size)
{
    int16_t mx, my, index;
    int opcode;

    if (bytestream2_get_bytes_left(&ctx->gb) < 1)
        return AVERROR_INVALIDDATA;

    opcode = bytestream2_get_byteu(&ctx->gb);

    av_dlog(ctx->avctx, "opcode 0x%0X cx %d cy %d blk %d\n", opcode, cx, cy, blk_size);
    switch (opcode) {
    default:
        mx = motion_vectors[opcode][0];
        my = motion_vectors[opcode][1];

        if (good_mvec(ctx, cx, cy, mx, my, blk_size)) {
            copy_block(ctx->frm0 + cx      + ctx->pitch *  cy,
                       ctx->frm2 + cx + mx + ctx->pitch * (cy + my),
                       blk_size, ctx->pitch);
        }
        break;
    case 0xF5:
        if (bytestream2_get_bytes_left(&ctx->gb) < 2)
            return AVERROR_INVALIDDATA;
        index = bytestream2_get_le16u(&ctx->gb);

        mx = index % ctx->width;
        my = index / ctx->width;

        if (good_mvec(ctx, cx, cy, mx, my, blk_size)) {
            copy_block(ctx->frm0 + cx      + ctx->pitch *  cy,
                       ctx->frm2 + cx + mx + ctx->pitch * (cy + my),
                       blk_size, ctx->pitch);
        }
        break;
    case 0xF6:
        copy_block(ctx->frm0 + cx + ctx->pitch * cy,
                   ctx->frm1 + cx + ctx->pitch * cy,
                   blk_size, ctx->pitch);
        break;
    case 0xF7:
        opcode_0xf7(ctx, cx, cy, blk_size, ctx->pitch);
        break;

    case 0xF8:
        opcode_0xf8(ctx, cx, cy, blk_size, ctx->pitch);
        break;
    case 0xF9:
    case 0xFA:
    case 0xFB:
    case 0xFC:
        fill_block(ctx->frm0 + cx + cy * ctx->pitch,
                   ctx->small_codebook[opcode - 0xf9], blk_size, ctx->pitch);
        break;
    case 0xFD:
        if (bytestream2_get_bytes_left(&ctx->gb) < 1)
            return AVERROR_INVALIDDATA;
        fill_block(ctx->frm0 + cx + cy * ctx->pitch,
                   ctx->codebook[bytestream2_get_byteu(&ctx->gb)], blk_size, ctx->pitch);
        break;
    case 0xFE:
        if (bytestream2_get_bytes_left(&ctx->gb) < 2)
            return AVERROR_INVALIDDATA;
        fill_block(ctx->frm0 + cx + cy * ctx->pitch,
                   bytestream2_get_le16u(&ctx->gb), blk_size, ctx->pitch);
        break;
    case 0xFF:
        if (blk_size == 2) {
            opcode_0xf8(ctx, cx, cy, blk_size, ctx->pitch);
        } else {
            blk_size >>= 1;
            if (codec2subblock(ctx, cx           , cy           , blk_size))
                return AVERROR_INVALIDDATA;
            if (codec2subblock(ctx, cx + blk_size, cy           , blk_size))
                return AVERROR_INVALIDDATA;
            if (codec2subblock(ctx, cx           , cy + blk_size, blk_size))
                return AVERROR_INVALIDDATA;
            if (codec2subblock(ctx, cx + blk_size, cy + blk_size, blk_size))
                return AVERROR_INVALIDDATA;
        }
        break;
    }
    return 0;
}

static int decode_2(SANMVideoContext *ctx)
{
    int cx, cy, ret;

    for (cy = 0; cy < ctx->aligned_height; cy += 8) {
        for (cx = 0; cx < ctx->aligned_width; cx += 8) {
            if (ret = codec2subblock(ctx, cx, cy, 8))
                return ret;
        }
    }

    return 0;
}

static int decode_3(SANMVideoContext *ctx)
{
    memcpy(ctx->frm0, ctx->frm2, ctx->frm2_size);
    return 0;
}

static int decode_4(SANMVideoContext *ctx)
{
    memcpy(ctx->frm0, ctx->frm1, ctx->frm1_size);
    return 0;
}

static int decode_5(SANMVideoContext *ctx)
{
#if HAVE_BIGENDIAN
    uint16_t *frm;
    int npixels;
#endif
    uint8_t *dst = (uint8_t*)ctx->frm0;

    if (rle_decode(ctx, dst, ctx->buf_size))
        return AVERROR_INVALIDDATA;

#if HAVE_BIGENDIAN
    npixels = ctx->npixels;
    frm = ctx->frm0;
    while (npixels--)
        *frm++ = av_bswap16(*frm);
#endif

    return 0;
}

static int decode_6(SANMVideoContext *ctx)
{
    int npixels = ctx->npixels;
    uint16_t *frm = ctx->frm0;

    if (bytestream2_get_bytes_left(&ctx->gb) < npixels) {
        av_log(ctx->avctx, AV_LOG_ERROR, "insufficient data for frame\n");
        return AVERROR_INVALIDDATA;
    }
    while (npixels--)
        *frm++ = ctx->codebook[bytestream2_get_byteu(&ctx->gb)];

    return 0;
}

static int decode_8(SANMVideoContext *ctx)
{
    uint16_t *pdest = ctx->frm0;
    uint8_t *rsrc;
    long npixels = ctx->npixels;

    av_fast_malloc(&ctx->rle_buf, &ctx->rle_buf_size, npixels);
    if (!ctx->rle_buf) {
        av_log(ctx->avctx, AV_LOG_ERROR, "RLE buffer allocation failed\n");
        return AVERROR(ENOMEM);
    }
    rsrc = ctx->rle_buf;

    if (rle_decode(ctx, rsrc, npixels))
        return AVERROR_INVALIDDATA;

    while (npixels--)
        *pdest++ = ctx->codebook[*rsrc++];

    return 0;
}

typedef int (*frm_decoder)(SANMVideoContext *ctx);

static const frm_decoder v1_decoders[] = {
    decode_0, decode_nop, decode_2, decode_3, decode_4, decode_5,
    decode_6, decode_nop, decode_8
};

static int read_frame_header(SANMVideoContext *ctx, SANMFrameHeader *hdr)
{
    int i, ret;

    if ((ret = bytestream2_get_bytes_left(&ctx->gb)) < 560) {
        av_log(ctx->avctx, AV_LOG_ERROR, "too short input frame (%d bytes)\n",
               ret);
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skip(&ctx->gb, 8); // skip pad

    hdr->width  = bytestream2_get_le32u(&ctx->gb);
    hdr->height = bytestream2_get_le32u(&ctx->gb);

    if (hdr->width != ctx->width || hdr->height != ctx->height) {
        av_log(ctx->avctx, AV_LOG_ERROR, "variable size frames are not implemented\n");
        return AVERROR_PATCHWELCOME;
    }

    hdr->seq_num     = bytestream2_get_le16u(&ctx->gb);
    hdr->codec       = bytestream2_get_byteu(&ctx->gb);
    hdr->rotate_code = bytestream2_get_byteu(&ctx->gb);

    bytestream2_skip(&ctx->gb, 4); // skip pad

    for (i = 0; i < 4; i++)
        ctx->small_codebook[i] = bytestream2_get_le16u(&ctx->gb);
    hdr->bg_color = bytestream2_get_le16u(&ctx->gb);

    bytestream2_skip(&ctx->gb, 2); // skip pad

    hdr->rle_output_size = bytestream2_get_le32u(&ctx->gb);
    for (i = 0; i < 256; i++)
        ctx->codebook[i] = bytestream2_get_le16u(&ctx->gb);

    bytestream2_skip(&ctx->gb, 8); // skip pad

    av_dlog(ctx->avctx, "subcodec %d\n", hdr->codec);
    return 0;
}

static void fill_frame(uint16_t *pbuf, int buf_size, uint16_t color)
{
    while (buf_size--)
        *pbuf++ = color;
}

static int copy_output(SANMVideoContext *ctx, SANMFrameHeader *hdr)
{
    uint8_t *dst;
    const uint8_t *src = (uint8_t*) ctx->frm0;
    int ret, dstpitch, height = ctx->height;
    int srcpitch = ctx->pitch * (hdr ? sizeof(ctx->frm0[0]) : 1);

    if ((ret = ff_get_buffer(ctx->avctx, ctx->frame, 0)) < 0)
        return ret;

    dst      = ctx->frame->data[0];
    dstpitch = ctx->frame->linesize[0];

    while (height--) {
        memcpy(dst, src, srcpitch);
        src += srcpitch;
        dst += dstpitch;
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame_ptr, AVPacket *pkt)
{
    SANMVideoContext *ctx = avctx->priv_data;
    int i, ret;

    ctx->frame = data;
    bytestream2_init(&ctx->gb, pkt->data, pkt->size);

    if (!ctx->version) {
        int to_store = 0;

        while (bytestream2_get_bytes_left(&ctx->gb) >= 8) {
            uint32_t sig, size;
            int pos;

            sig  = bytestream2_get_be32u(&ctx->gb);
            size = bytestream2_get_be32u(&ctx->gb);
            pos  = bytestream2_tell(&ctx->gb);

            if (bytestream2_get_bytes_left(&ctx->gb) < size) {
                av_log(avctx, AV_LOG_ERROR, "incorrect chunk size %d\n", size);
                break;
            }
            switch (sig) {
            case MKBETAG('N', 'P', 'A', 'L'):
                if (size != 256 * 3) {
                    av_log(avctx, AV_LOG_ERROR, "incorrect palette block size %d\n",
                           size);
                    return AVERROR_INVALIDDATA;
                }
                for (i = 0; i < 256; i++)
                    ctx->pal[i] = 0xFFU << 24 | bytestream2_get_be24u(&ctx->gb);
                break;
            case MKBETAG('F', 'O', 'B', 'J'):
                if (size < 16)
                    return AVERROR_INVALIDDATA;
                if (ret = process_frame_obj(ctx))
                    return ret;
                break;
            case MKBETAG('X', 'P', 'A', 'L'):
                if (size == 6 || size == 4) {
                    uint8_t tmp[3];
                    int j;

                    for (i = 0; i < 256; i++) {
                        for (j = 0; j < 3; j++) {
                            int t = (ctx->pal[i] >> (16 - j * 8)) & 0xFF;
                            tmp[j] = av_clip_uint8((t * 129 + ctx->delta_pal[i * 3 + j]) >> 7);
                        }
                        ctx->pal[i] = 0xFFU << 24 | AV_RB24(tmp);
                    }
                } else {
                    if (size < 768 * 2 + 4) {
                        av_log(avctx, AV_LOG_ERROR, "incorrect palette change block size %d\n",
                               size);
                        return AVERROR_INVALIDDATA;
                    }
                    bytestream2_skipu(&ctx->gb, 4);
                    for (i = 0; i < 768; i++)
                        ctx->delta_pal[i] = bytestream2_get_le16u(&ctx->gb);
                    if (size >= 768 * 5 + 4) {
                        for (i = 0; i < 256; i++)
                            ctx->pal[i] = 0xFFU << 24 | bytestream2_get_be24u(&ctx->gb);
                    } else {
                        memset(ctx->pal, 0, sizeof(ctx->pal));
                    }
                }
                break;
            case MKBETAG('S', 'T', 'O', 'R'):
                to_store = 1;
                break;
            case MKBETAG('F', 'T', 'C', 'H'):
                memcpy(ctx->frm0, ctx->stored_frame, ctx->buf_size);
                break;
            default:
                bytestream2_skip(&ctx->gb, size);
                av_log(avctx, AV_LOG_DEBUG, "unknown/unsupported chunk %x\n", sig);
                break;
            }

            bytestream2_seek(&ctx->gb, pos + size, SEEK_SET);
            if (size & 1)
                bytestream2_skip(&ctx->gb, 1);
        }
        if (to_store)
            memcpy(ctx->stored_frame, ctx->frm0, ctx->buf_size);
        if ((ret = copy_output(ctx, NULL)))
            return ret;
        memcpy(ctx->frame->data[1], ctx->pal, 1024);
    } else {
        SANMFrameHeader header;

        if ((ret = read_frame_header(ctx, &header)))
            return ret;

        ctx->rotate_code = header.rotate_code;
        if ((ctx->frame->key_frame = !header.seq_num)) {
            ctx->frame->pict_type = AV_PICTURE_TYPE_I;
            fill_frame(ctx->frm1, ctx->npixels, header.bg_color);
            fill_frame(ctx->frm2, ctx->npixels, header.bg_color);
        } else {
            ctx->frame->pict_type = AV_PICTURE_TYPE_P;
        }

        if (header.codec < FF_ARRAY_ELEMS(v1_decoders)) {
            if ((ret = v1_decoders[header.codec](ctx))) {
                av_log(avctx, AV_LOG_ERROR,
                       "subcodec %d: error decoding frame\n", header.codec);
                return ret;
            }
        } else {
            avpriv_request_sample(avctx, "subcodec %d",
                   header.codec);
            return AVERROR_PATCHWELCOME;
        }

        if ((ret = copy_output(ctx, &header)))
            return ret;
    }
    if (ctx->rotate_code)
        rotate_bufs(ctx, ctx->rotate_code);

    *got_frame_ptr  = 1;

    return pkt->size;
}

AVCodec ff_sanm_decoder = {
    .name           = "sanm",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SANM,
    .priv_data_size = sizeof(SANMVideoContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("LucasArts SMUSH video"),
};
