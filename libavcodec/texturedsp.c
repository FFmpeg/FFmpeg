/*
 * Texture block decompression
 * Copyright (C) 2009 Benjamin Dobell, Glass Echidna
 * Copyright (C) 2012 Matthäus G. "Anteru" Chajdas (http://anteru.net)
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stddef.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/libm.h"

#include "texturedsp.h"

#define RGBA(r, g, b, a) (((uint8_t)(r) <<  0) | \
                          ((uint8_t)(g) <<  8) | \
                          ((uint8_t)(b) << 16) | \
                          ((uint8_t)(a) << 24))

static av_always_inline void extract_color(uint32_t colors[4],
                                           uint16_t color0,
                                           uint16_t color1,
                                           int dxtn, int alpha)
{
    int tmp;
    uint8_t r0, g0, b0, r1, g1, b1;
    uint8_t a = dxtn ? 0 : 255;

    tmp = (color0 >> 11) * 255 + 16;
    r0  = (uint8_t) ((tmp / 32 + tmp) / 32);
    tmp = ((color0 & 0x07E0) >> 5) * 255 + 32;
    g0  = (uint8_t) ((tmp / 64 + tmp) / 64);
    tmp = (color0 & 0x001F) * 255 + 16;
    b0  = (uint8_t) ((tmp / 32 + tmp) / 32);

    tmp = (color1 >> 11) * 255 + 16;
    r1  = (uint8_t) ((tmp / 32 + tmp) / 32);
    tmp = ((color1 & 0x07E0) >> 5) * 255 + 32;
    g1  = (uint8_t) ((tmp / 64 + tmp) / 64);
    tmp = (color1 & 0x001F) * 255 + 16;
    b1  = (uint8_t) ((tmp / 32 + tmp) / 32);

    if (dxtn || color0 > color1) {
        colors[0] = RGBA(r0, g0, b0, a);
        colors[1] = RGBA(r1, g1, b1, a);
        colors[2] = RGBA((2 * r0 + r1) / 3,
                         (2 * g0 + g1) / 3,
                         (2 * b0 + b1) / 3,
                         a);
        colors[3] = RGBA((2 * r1 + r0) / 3,
                         (2 * g1 + g0) / 3,
                         (2 * b1 + b0) / 3,
                         a);
    } else {
        colors[0] = RGBA(r0, g0, b0, a);
        colors[1] = RGBA(r1, g1, b1, a);
        colors[2] = RGBA((r0 + r1) / 2,
                         (g0 + g1) / 2,
                         (b0 + b1) / 2,
                         a);
        colors[3] = RGBA(0, 0, 0, alpha);
    }
}

static inline void dxt1_block_internal(uint8_t *dst, ptrdiff_t stride,
                                       const uint8_t *block, uint8_t alpha)
{
    int x, y;
    uint32_t colors[4];
    uint16_t color0 = AV_RL16(block + 0);
    uint16_t color1 = AV_RL16(block + 2);
    uint32_t code   = AV_RL32(block + 4);

    extract_color(colors, color0, color1, 0, alpha);

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            uint32_t pixel = colors[code & 3];
            code >>= 2;
            AV_WL32(dst + x * 4, pixel);
        }
        dst += stride;
    }
}

/**
 * Decompress one block of a DXT1 texture and store the resulting
 * RGBA pixels in 'dst'. Alpha component is fully opaque.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int dxt1_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    dxt1_block_internal(dst, stride, block, 255);

    return 8;
}

/**
 * Decompress one block of a DXT1 with 1-bit alpha texture and store
 * the resulting RGBA pixels in 'dst'. Alpha is either fully opaque or
 * fully transparent.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int dxt1a_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    dxt1_block_internal(dst, stride, block, 0);

    return 8;
}

static inline void dxt3_block_internal(uint8_t *dst, ptrdiff_t stride,
                                       const uint8_t *block)
{
    int x, y;
    uint32_t colors[4];
    uint16_t color0 = AV_RL16(block +  8);
    uint16_t color1 = AV_RL16(block + 10);
    uint32_t code   = AV_RL32(block + 12);

    extract_color(colors, color0, color1, 1, 0);

    for (y = 0; y < 4; y++) {
        const uint16_t alpha_code = AV_RL16(block + 2 * y);
        uint8_t alpha_values[4];

        alpha_values[0] = ((alpha_code >>  0) & 0x0F) * 17;
        alpha_values[1] = ((alpha_code >>  4) & 0x0F) * 17;
        alpha_values[2] = ((alpha_code >>  8) & 0x0F) * 17;
        alpha_values[3] = ((alpha_code >> 12) & 0x0F) * 17;

        for (x = 0; x < 4; x++) {
            uint8_t alpha = alpha_values[x];
            uint32_t pixel = colors[code & 3] | (alpha << 24);
            code >>= 2;

            AV_WL32(dst + x * 4, pixel);
        }
        dst += stride;
    }
}

/** Convert a premultiplied alpha pixel to a straight alpha pixel. */
static av_always_inline void premult2straight(uint8_t *src)
{
    int r = src[0];
    int g = src[1];
    int b = src[2];
    int a = src[3]; /* unchanged */

    src[0] = (uint8_t) r * a / 255;
    src[1] = (uint8_t) g * a / 255;
    src[2] = (uint8_t) b * a / 255;
}

/**
 * Decompress one block of a DXT2 texture and store the resulting
 * RGBA pixels in 'dst'.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int dxt2_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    int x, y;

    dxt3_block_internal(dst, stride, block);

    /* This format is DXT3, but returns premultiplied alpha. It needs to be
     * converted because it's what lavc outputs (and swscale expects). */
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            premult2straight(dst + x * 4 + y * stride);

    return 16;
}

/**
 * Decompress one block of a DXT3 texture and store the resulting
 * RGBA pixels in 'dst'.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int dxt3_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    dxt3_block_internal(dst, stride, block);

    return 16;
}

/**
 * Decompress a BC 16x3 index block stored as
 *   h g f e
 *   d c b a
 *   p o n m
 *   l k j i
 *
 * Bits packed as
 *  | h | g | f | e | d | c | b | a | // Entry
 *  |765 432 107 654 321 076 543 210| // Bit
 *  |0000000000111111111112222222222| // Byte
 *
 * into 16 8-bit indices.
 */
static void decompress_indices(uint8_t *dst, const uint8_t *src)
{
    int block, i;

    for (block = 0; block < 2; block++) {
        int tmp = AV_RL24(src);

        /* Unpack 8x3 bit from last 3 byte block */
        for (i = 0; i < 8; i++)
            dst[i] = (tmp >> (i * 3)) & 0x7;

        src += 3;
        dst += 8;
    }
}

static inline void dxt5_block_internal(uint8_t *dst, ptrdiff_t stride,
                                       const uint8_t *block)
{
    int x, y;
    uint32_t colors[4];
    uint8_t alpha_indices[16];
    uint16_t color0 = AV_RL16(block + 8);
    uint16_t color1 = AV_RL16(block + 10);
    uint32_t code   = AV_RL32(block + 12);
    uint8_t alpha0  = *(block);
    uint8_t alpha1  = *(block + 1);

    decompress_indices(alpha_indices, block + 2);

    extract_color(colors, color0, color1, 1, 0);

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int alpha_code = alpha_indices[x + y * 4];
            uint32_t pixel;
            uint8_t alpha;

            if (alpha_code == 0) {
                alpha = alpha0;
            } else if (alpha_code == 1) {
                alpha = alpha1;
            } else {
                if (alpha0 > alpha1) {
                    alpha = (uint8_t) (((8 - alpha_code) * alpha0 +
                                        (alpha_code - 1) * alpha1) / 7);
                } else {
                    if (alpha_code == 6) {
                        alpha = 0;
                    } else if (alpha_code == 7) {
                        alpha = 255;
                    } else {
                        alpha = (uint8_t) (((6 - alpha_code) * alpha0 +
                                            (alpha_code - 1) * alpha1) / 5);
                    }
                }
            }
            pixel = colors[code & 3] | (alpha << 24);
            code >>= 2;
            AV_WL32(dst + x * 4, pixel);
        }
        dst += stride;
    }
}

/**
 * Decompress one block of a DXT4 texture and store the resulting
 * RGBA pixels in 'dst'.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int dxt4_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    int x, y;

    dxt5_block_internal(dst, stride, block);

    /* This format is DXT5, but returns premultiplied alpha. It needs to be
     * converted because it's what lavc outputs (and swscale expects). */
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            premult2straight(dst + x * 4 + y * stride);

    return 16;
}

/**
 * Decompress one block of a DXT5 texture and store the resulting
 * RGBA pixels in 'dst'.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int dxt5_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    dxt5_block_internal(dst, stride, block);

    return 16;
}

/**
 * Convert a YCoCg buffer to RGBA.
 *
 * @param src    input buffer.
 * @param scaled variant with scaled chroma components and opaque alpha.
 */
static av_always_inline void ycocg2rgba(uint8_t *src, int scaled)
{
    int r = src[0];
    int g = src[1];
    int b = src[2];
    int a = src[3];

    int s  = scaled ? (b >> 3) + 1 : 1;
    int y  = a;
    int co = (r - 128) / s;
    int cg = (g - 128) / s;

    src[0] = av_clip_uint8(y + co - cg);
    src[1] = av_clip_uint8(y + cg);
    src[2] = av_clip_uint8(y - co - cg);
    src[3] = scaled ? 255 : b;
}

/**
 * Decompress one block of a DXT5 texture with classic YCoCg and store
 * the resulting RGBA pixels in 'dst'. Alpha component is fully opaque.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int dxt5y_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    int x, y;

    /* This format is basically DXT5, with luma stored in alpha.
     * Run a normal decompress and then reorder the components. */
    dxt5_block_internal(dst, stride, block);

    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            ycocg2rgba(dst + x * 4 + y * stride, 0);

    return 16;
}

/**
 * Decompress one block of a DXT5 texture with scaled YCoCg and store
 * the resulting RGBA pixels in 'dst'. Alpha component is fully opaque.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int dxt5ys_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    int x, y;

    /* This format is basically DXT5, with luma stored in alpha.
     * Run a normal decompress and then reorder the components. */
    dxt5_block_internal(dst, stride, block);

    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            ycocg2rgba(dst + x * 4 + y * stride, 1);

    return 16;
}

static inline void rgtc_block_internal(uint8_t *dst, ptrdiff_t stride,
                                       const uint8_t *block,
                                       const int *color_tab)
{
    uint8_t indices[16];
    int x, y;

    decompress_indices(indices, block + 2);

    /* Only one or two channels are stored at most, since it only used to
     * compress specular (black and white) or normal (red and green) maps.
     * Although the standard says to zero out unused components, many
     * implementations fill all of them with the same value. */
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            int i = indices[x + y * 4];
            /* Interval expansion from [-1 1] or [0 1] to [0 255]. */
            int c = color_tab[i];
            uint32_t pixel = RGBA(c, c, c, 255U);
            AV_WL32(dst + x * 4 + y * stride, pixel);
        }
    }
}

static inline void rgtc1_block_internal(uint8_t *dst, ptrdiff_t stride,
                                        const uint8_t *block, int sign)
{
    int color_table[8];
    int r0, r1;

    if (sign) {
        /* signed data is in [-128 127] so just offset it to unsigned
         * and it can be treated exactly the same */
        r0 = ((int8_t) block[0]) + 128;
        r1 = ((int8_t) block[1]) + 128;
    } else {
        r0 = block[0];
        r1 = block[1];
    }

    color_table[0] = r0;
    color_table[1] = r1;

    if (r0 > r1) {
        /* 6 interpolated color values */
        color_table[2] = (6 * r0 + 1 * r1) / 7; // bit code 010
        color_table[3] = (5 * r0 + 2 * r1) / 7; // bit code 011
        color_table[4] = (4 * r0 + 3 * r1) / 7; // bit code 100
        color_table[5] = (3 * r0 + 4 * r1) / 7; // bit code 101
        color_table[6] = (2 * r0 + 5 * r1) / 7; // bit code 110
        color_table[7] = (1 * r0 + 6 * r1) / 7; // bit code 111
    } else {
        /* 4 interpolated color values */
        color_table[2] = (4 * r0 + 1 * r1) / 5; // bit code 010
        color_table[3] = (3 * r0 + 2 * r1) / 5; // bit code 011
        color_table[4] = (2 * r0 + 3 * r1) / 5; // bit code 100
        color_table[5] = (1 * r0 + 4 * r1) / 5; // bit code 101
        color_table[6] = 0;    /* min range */  // bit code 110
        color_table[7] = 255;  /* max range */  // bit code 111
    }

    rgtc_block_internal(dst, stride, block, color_table);
}

/**
 * Decompress one block of a RGRC1 texture with signed components
 * and store the resulting RGBA pixels in 'dst'.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int rgtc1s_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    rgtc1_block_internal(dst, stride, block, 1);

    return 8;
}

/**
 * Decompress one block of a RGRC1 texture with unsigned components
 * and store the resulting RGBA pixels in 'dst'.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int rgtc1u_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    rgtc1_block_internal(dst, stride, block, 0);

    return 8;
}

static inline void rgtc2_block_internal(uint8_t *dst, ptrdiff_t stride,
                                        const uint8_t *block, int sign)
{
    /* 4x4 block containing 4 component pixels. */
    uint8_t c0[4 * 4 * 4];
    uint8_t c1[4 * 4 * 4];
    int x, y;

    /* Decompress the two channels separately and interleave them afterwards. */
    rgtc1_block_internal(c0, 16, block, sign);
    rgtc1_block_internal(c1, 16, block + 8, sign);

    /* B is rebuilt exactly like a normal map. */
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            uint8_t *p = dst + x * 4 + y * stride;
            int r = c0[x * 4 + y * 16];
            int g = c1[x * 4 + y * 16];
            int b = 127;

            int d = (255 * 255 - r * r - g * g) / 2;
            if (d > 0)
                b = lrint(sqrtf(d));

            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}

/**
 * Decompress one block of a RGRC2 texture with signed components
 * and store the resulting RGBA pixels in 'dst'. Alpha is fully opaque.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int rgtc2s_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    rgtc2_block_internal(dst, stride, block, 1);

    return 16;
}

/**
 * Decompress one block of a RGRC2 texture with unsigned components
 * and store the resulting RGBA pixels in 'dst'. Alpha is fully opaque.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int rgtc2u_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    rgtc2_block_internal(dst, stride, block, 0);

    return 16;
}

/**
 * Decompress one block of a 3Dc texture with unsigned components
 * and store the resulting RGBA pixels in 'dst'. Alpha is fully opaque.
 *
 * @param dst    output buffer.
 * @param stride scanline in bytes.
 * @param block  block to decompress.
 * @return how much texture data has been consumed.
 */
static int dxn3dc_block(uint8_t *dst, ptrdiff_t stride, const uint8_t *block)
{
    int x, y;
    rgtc2_block_internal(dst, stride, block, 0);

    /* This is the 3Dc variant of RGTC2, with swapped R and G. */
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            uint8_t *p = dst + x * 4 + y * stride;
            FFSWAP(uint8_t, p[0], p[1]);
        }
    }

    return 16;
}

av_cold void ff_texturedsp_init(TextureDSPContext *c)
{
    c->dxt1_block   = dxt1_block;
    c->dxt1a_block  = dxt1a_block;
    c->dxt2_block   = dxt2_block;
    c->dxt3_block   = dxt3_block;
    c->dxt4_block   = dxt4_block;
    c->dxt5_block   = dxt5_block;
    c->dxt5y_block  = dxt5y_block;
    c->dxt5ys_block = dxt5ys_block;
    c->rgtc1s_block = rgtc1s_block;
    c->rgtc1u_block = rgtc1u_block;
    c->rgtc2s_block = rgtc2s_block;
    c->rgtc2u_block = rgtc2u_block;
    c->dxn3dc_block = dxn3dc_block;
}
