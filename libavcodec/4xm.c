/*
 * 4XM codec
 * Copyright (c) 2003 Michael Niedermayer
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
 * 4XM codec.
 */

#include "libavutil/avassert.h"
#include "libavutil/frame.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "dsputil.h"
#include "get_bits.h"
#include "internal.h"


#define BLOCK_TYPE_VLC_BITS 5
#define ACDC_VLC_BITS 9

#define CFRAME_BUFFER_COUNT 100

static const uint8_t block_type_tab[2][4][8][2] = {
    {
        {    // { 8, 4, 2 } x { 8, 4, 2}
            { 0, 1 }, { 2, 2 }, { 6, 3 }, { 14, 4 }, { 30, 5 }, { 31, 5 }, { 0, 0 }
        }, { // { 8, 4 } x 1
            { 0, 1 }, { 0, 0 }, { 2, 2 }, { 6, 3 }, { 14, 4 }, { 15, 4 }, { 0, 0 }
        }, { // 1 x { 8, 4 }
            { 0, 1 }, { 2, 2 }, { 0, 0 }, { 6, 3 }, { 14, 4 }, { 15, 4 }, { 0, 0 }
        }, { // 1 x 2, 2 x 1
            { 0, 1 }, { 0, 0 }, { 0, 0 }, { 2, 2 }, { 6, 3 }, { 14, 4 }, { 15, 4 }
        }
    }, {
        {   // { 8, 4, 2 } x { 8, 4, 2}
            { 1, 2 }, { 4, 3 }, { 5, 3 }, { 0, 2 }, { 6, 3 }, { 7, 3 }, { 0, 0 }
        }, {// { 8, 4 } x 1
            { 1, 2 }, { 0, 0 }, { 2, 2 }, { 0, 2 }, { 6, 3 }, { 7, 3 }, { 0, 0 }
        }, {// 1 x { 8, 4 }
            { 1, 2 }, { 2, 2 }, { 0, 0 }, { 0, 2 }, { 6, 3 }, { 7, 3 }, { 0, 0 }
        }, {// 1 x 2, 2 x 1
            { 1, 2 }, { 0, 0 }, { 0, 0 }, { 0, 2 }, { 2, 2 }, { 6, 3 }, { 7, 3 }
      }
    }
};

static const uint8_t size2index[4][4] = {
    { -1, 3, 1, 1 },
    {  3, 0, 0, 0 },
    {  2, 0, 0, 0 },
    {  2, 0, 0, 0 },
};

static const int8_t mv[256][2] = {
    {   0,   0 }, {   0,  -1 }, {  -1,   0 }, {   1,   0 }, {   0,   1 }, {  -1,  -1 }, {   1,  -1 }, {  -1,   1 },
    {   1,   1 }, {   0,  -2 }, {  -2,   0 }, {   2,   0 }, {   0,   2 }, {  -1,  -2 }, {   1,  -2 }, {  -2,  -1 },
    {   2,  -1 }, {  -2,   1 }, {   2,   1 }, {  -1,   2 }, {   1,   2 }, {  -2,  -2 }, {   2,  -2 }, {  -2,   2 },
    {   2,   2 }, {   0,  -3 }, {  -3,   0 }, {   3,   0 }, {   0,   3 }, {  -1,  -3 }, {   1,  -3 }, {  -3,  -1 },
    {   3,  -1 }, {  -3,   1 }, {   3,   1 }, {  -1,   3 }, {   1,   3 }, {  -2,  -3 }, {   2,  -3 }, {  -3,  -2 },
    {   3,  -2 }, {  -3,   2 }, {   3,   2 }, {  -2,   3 }, {   2,   3 }, {   0,  -4 }, {  -4,   0 }, {   4,   0 },
    {   0,   4 }, {  -1,  -4 }, {   1,  -4 }, {  -4,  -1 }, {   4,  -1 }, {   4,   1 }, {  -1,   4 }, {   1,   4 },
    {  -3,  -3 }, {  -3,   3 }, {   3,   3 }, {  -2,  -4 }, {  -4,  -2 }, {   4,  -2 }, {  -4,   2 }, {  -2,   4 },
    {   2,   4 }, {  -3,  -4 }, {   3,  -4 }, {   4,  -3 }, {  -5,   0 }, {  -4,   3 }, {  -3,   4 }, {   3,   4 },
    {  -1,  -5 }, {  -5,  -1 }, {  -5,   1 }, {  -1,   5 }, {  -2,  -5 }, {   2,  -5 }, {   5,  -2 }, {   5,   2 },
    {  -4,  -4 }, {  -4,   4 }, {  -3,  -5 }, {  -5,  -3 }, {  -5,   3 }, {   3,   5 }, {  -6,   0 }, {   0,   6 },
    {  -6,  -1 }, {  -6,   1 }, {   1,   6 }, {   2,  -6 }, {  -6,   2 }, {   2,   6 }, {  -5,  -4 }, {   5,   4 },
    {   4,   5 }, {  -6,  -3 }, {   6,   3 }, {  -7,   0 }, {  -1,  -7 }, {   5,  -5 }, {  -7,   1 }, {  -1,   7 },
    {   4,  -6 }, {   6,   4 }, {  -2,  -7 }, {  -7,   2 }, {  -3,  -7 }, {   7,  -3 }, {   3,   7 }, {   6,  -5 },
    {   0,  -8 }, {  -1,  -8 }, {  -7,  -4 }, {  -8,   1 }, {   4,   7 }, {   2,  -8 }, {  -2,   8 }, {   6,   6 },
    {  -8,   3 }, {   5,  -7 }, {  -5,   7 }, {   8,  -4 }, {   0,  -9 }, {  -9,  -1 }, {   1,   9 }, {   7,  -6 },
    {  -7,   6 }, {  -5,  -8 }, {  -5,   8 }, {  -9,   3 }, {   9,  -4 }, {   7,  -7 }, {   8,  -6 }, {   6,   8 },
    {  10,   1 }, { -10,   2 }, {   9,  -5 }, {  10,  -3 }, {  -8,  -7 }, { -10,  -4 }, {   6,  -9 }, { -11,   0 },
    {  11,   1 }, { -11,  -2 }, {  -2,  11 }, {   7,  -9 }, {  -7,   9 }, {  10,   6 }, {  -4,  11 }, {   8,  -9 },
    {   8,   9 }, {   5,  11 }, {   7, -10 }, {  12,  -3 }, {  11,   6 }, {  -9,  -9 }, {   8,  10 }, {   5,  12 },
    { -11,   7 }, {  13,   2 }, {   6, -12 }, {  10,   9 }, { -11,   8 }, {  -7,  12 }, {   0,  14 }, {  14,  -2 },
    {  -9,  11 }, {  -6,  13 }, { -14,  -4 }, {  -5, -14 }, {   5,  14 }, { -15,  -1 }, { -14,  -6 }, {   3, -15 },
    {  11, -11 }, {  -7,  14 }, {  -5,  15 }, {   8, -14 }, {  15,   6 }, {   3,  16 }, {   7, -15 }, { -16,   5 },
    {   0,  17 }, { -16,  -6 }, { -10,  14 }, { -16,   7 }, {  12,  13 }, { -16,   8 }, { -17,   6 }, { -18,   3 },
    {  -7,  17 }, {  15,  11 }, {  16,  10 }, {   2, -19 }, {   3, -19 }, { -11, -16 }, { -18,   8 }, { -19,  -6 },
    {   2, -20 }, { -17, -11 }, { -10, -18 }, {   8,  19 }, { -21,  -1 }, { -20,   7 }, {  -4,  21 }, {  21,   5 },
    {  15,  16 }, {   2, -22 }, { -10, -20 }, { -22,   5 }, {  20, -11 }, {  -7, -22 }, { -12,  20 }, {  23,  -5 },
    {  13, -20 }, {  24,  -2 }, { -15,  19 }, { -11,  22 }, {  16,  19 }, {  23, -10 }, { -18, -18 }, {  -9, -24 },
    {  24, -10 }, {  -3,  26 }, { -23,  13 }, { -18, -20 }, {  17,  21 }, {  -4,  27 }, {  27,   6 }, {   1, -28 },
    { -11,  26 }, { -17, -23 }, {   7,  28 }, {  11, -27 }, {  29,   5 }, { -23, -19 }, { -28, -11 }, { -21,  22 },
    { -30,   7 }, { -17,  26 }, { -27,  16 }, {  13,  29 }, {  19, -26 }, {  10, -31 }, { -14, -30 }, {  20, -27 },
    { -29,  18 }, { -16, -31 }, { -28, -22 }, {  21, -30 }, { -25,  28 }, {  26, -29 }, {  25, -32 }, { -32, -32 }
};

/* This is simply the scaled down elementwise product of the standard JPEG
 * quantizer table and the AAN premul table. */
static const uint8_t dequant_table[64] = {
    16, 15, 13, 19, 24, 31, 28, 17,
    17, 23, 25, 31, 36, 63, 45, 21,
    18, 24, 27, 37, 52, 59, 49, 20,
    16, 28, 34, 40, 60, 80, 51, 20,
    18, 31, 48, 66, 68, 86, 56, 21,
    19, 38, 56, 59, 64, 64, 48, 20,
    27, 48, 55, 55, 56, 51, 35, 15,
    20, 35, 34, 32, 31, 22, 15,  8,
};

static VLC block_type_vlc[2][4];


typedef struct CFrameBuffer {
    unsigned int allocated_size;
    unsigned int size;
    int id;
    uint8_t *data;
} CFrameBuffer;

typedef struct FourXContext {
    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame *current_picture, *last_picture;
    GetBitContext pre_gb;          ///< ac/dc prefix
    GetBitContext gb;
    GetByteContext g;
    GetByteContext g2;
    int mv[256];
    VLC pre_vlc;
    int last_dc;
    DECLARE_ALIGNED(16, int16_t, block)[6][64];
    void *bitstream_buffer;
    unsigned int bitstream_buffer_size;
    int version;
    CFrameBuffer cfrm[CFRAME_BUFFER_COUNT];
} FourXContext;


#define FIX_1_082392200  70936
#define FIX_1_414213562  92682
#define FIX_1_847759065 121095
#define FIX_2_613125930 171254

#define MULTIPLY(var, const) (((var) * (const)) >> 16)

static void idct(int16_t block[64])
{
    int tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int tmp10, tmp11, tmp12, tmp13;
    int z5, z10, z11, z12, z13;
    int i;
    int temp[64];

    for (i = 0; i < 8; i++) {
        tmp10 = block[8 * 0 + i] + block[8 * 4 + i];
        tmp11 = block[8 * 0 + i] - block[8 * 4 + i];

        tmp13 = block[8 * 2 + i] + block[8 * 6 + i];
        tmp12 = MULTIPLY(block[8 * 2 + i] - block[8 * 6 + i], FIX_1_414213562) - tmp13;

        tmp0 = tmp10 + tmp13;
        tmp3 = tmp10 - tmp13;
        tmp1 = tmp11 + tmp12;
        tmp2 = tmp11 - tmp12;

        z13 = block[8 * 5 + i] + block[8 * 3 + i];
        z10 = block[8 * 5 + i] - block[8 * 3 + i];
        z11 = block[8 * 1 + i] + block[8 * 7 + i];
        z12 = block[8 * 1 + i] - block[8 * 7 + i];

        tmp7  =          z11 + z13;
        tmp11 = MULTIPLY(z11 - z13, FIX_1_414213562);

        z5    = MULTIPLY(z10 + z12, FIX_1_847759065);
        tmp10 = MULTIPLY(z12,  FIX_1_082392200) - z5;
        tmp12 = MULTIPLY(z10, -FIX_2_613125930) + z5;

        tmp6 = tmp12 - tmp7;
        tmp5 = tmp11 - tmp6;
        tmp4 = tmp10 + tmp5;

        temp[8 * 0 + i] = tmp0 + tmp7;
        temp[8 * 7 + i] = tmp0 - tmp7;
        temp[8 * 1 + i] = tmp1 + tmp6;
        temp[8 * 6 + i] = tmp1 - tmp6;
        temp[8 * 2 + i] = tmp2 + tmp5;
        temp[8 * 5 + i] = tmp2 - tmp5;
        temp[8 * 4 + i] = tmp3 + tmp4;
        temp[8 * 3 + i] = tmp3 - tmp4;
    }

    for (i = 0; i < 8 * 8; i += 8) {
        tmp10 = temp[0 + i] + temp[4 + i];
        tmp11 = temp[0 + i] - temp[4 + i];

        tmp13 = temp[2 + i] + temp[6 + i];
        tmp12 = MULTIPLY(temp[2 + i] - temp[6 + i], FIX_1_414213562) - tmp13;

        tmp0 = tmp10 + tmp13;
        tmp3 = tmp10 - tmp13;
        tmp1 = tmp11 + tmp12;
        tmp2 = tmp11 - tmp12;

        z13 = temp[5 + i] + temp[3 + i];
        z10 = temp[5 + i] - temp[3 + i];
        z11 = temp[1 + i] + temp[7 + i];
        z12 = temp[1 + i] - temp[7 + i];

        tmp7  = z11 + z13;
        tmp11 = MULTIPLY(z11 - z13, FIX_1_414213562);

        z5    = MULTIPLY(z10 + z12, FIX_1_847759065);
        tmp10 = MULTIPLY(z12,  FIX_1_082392200) - z5;
        tmp12 = MULTIPLY(z10, -FIX_2_613125930) + z5;

        tmp6 = tmp12 - tmp7;
        tmp5 = tmp11 - tmp6;
        tmp4 = tmp10 + tmp5;

        block[0 + i] = (tmp0 + tmp7) >> 6;
        block[7 + i] = (tmp0 - tmp7) >> 6;
        block[1 + i] = (tmp1 + tmp6) >> 6;
        block[6 + i] = (tmp1 - tmp6) >> 6;
        block[2 + i] = (tmp2 + tmp5) >> 6;
        block[5 + i] = (tmp2 - tmp5) >> 6;
        block[4 + i] = (tmp3 + tmp4) >> 6;
        block[3 + i] = (tmp3 - tmp4) >> 6;
    }
}

static av_cold void init_vlcs(FourXContext *f)
{
    static VLC_TYPE table[2][4][32][2];
    int i, j;

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 4; j++) {
            block_type_vlc[i][j].table           = table[i][j];
            block_type_vlc[i][j].table_allocated = 32;
            init_vlc(&block_type_vlc[i][j], BLOCK_TYPE_VLC_BITS, 7,
                     &block_type_tab[i][j][0][1], 2, 1,
                     &block_type_tab[i][j][0][0], 2, 1,
                     INIT_VLC_USE_NEW_STATIC);
        }
    }
}

static void init_mv(FourXContext *f, int linesize)
{
    int i;

    for (i = 0; i < 256; i++) {
        if (f->version > 1)
            f->mv[i] = mv[i][0] + mv[i][1] * linesize / 2;
        else
            f->mv[i] = (i & 15) - 8 + ((i >> 4) - 8) * linesize / 2;
    }
}

#if HAVE_BIGENDIAN
#define LE_CENTRIC_MUL(dst, src, scale, dc)             \
    {                                                   \
        unsigned tmpval = AV_RN32(src);                 \
        tmpval = (tmpval << 16) | (tmpval >> 16);       \
        tmpval = tmpval * (scale) + (dc);               \
        tmpval = (tmpval << 16) | (tmpval >> 16);       \
        AV_WN32A(dst, tmpval);                          \
    }
#else
#define LE_CENTRIC_MUL(dst, src, scale, dc)              \
    {                                                    \
        unsigned tmpval = AV_RN32(src) * (scale) + (dc); \
        AV_WN32A(dst, tmpval);                           \
    }
#endif

static inline void mcdc(uint16_t *dst, const uint16_t *src, int log2w,
                        int h, int stride, int scale, unsigned dc)
{
    int i;
    dc *= 0x10001;

    switch (log2w) {
    case 0:
        for (i = 0; i < h; i++) {
            dst[0] = scale * src[0] + dc;
            if (scale)
                src += stride;
            dst += stride;
        }
        break;
    case 1:
        for (i = 0; i < h; i++) {
            LE_CENTRIC_MUL(dst, src, scale, dc);
            if (scale)
                src += stride;
            dst += stride;
        }
        break;
    case 2:
        for (i = 0; i < h; i++) {
            LE_CENTRIC_MUL(dst, src, scale, dc);
            LE_CENTRIC_MUL(dst + 2, src + 2, scale, dc);
            if (scale)
                src += stride;
            dst += stride;
        }
        break;
    case 3:
        for (i = 0; i < h; i++) {
            LE_CENTRIC_MUL(dst,     src,     scale, dc);
            LE_CENTRIC_MUL(dst + 2, src + 2, scale, dc);
            LE_CENTRIC_MUL(dst + 4, src + 4, scale, dc);
            LE_CENTRIC_MUL(dst + 6, src + 6, scale, dc);
            if (scale)
                src += stride;
            dst += stride;
        }
        break;
    default:
        av_assert0(0);
    }
}

static int decode_p_block(FourXContext *f, uint16_t *dst, uint16_t *src,
                          int log2w, int log2h, int stride)
{
    const int index = size2index[log2h][log2w];
    const int h     = 1 << log2h;
    int code        = get_vlc2(&f->gb,
                               block_type_vlc[1 - (f->version > 1)][index].table,
                               BLOCK_TYPE_VLC_BITS, 1);
    uint16_t *start = (uint16_t *)f->last_picture->data[0];
    uint16_t *end   = start + stride * (f->avctx->height - h + 1) - (1 << log2w);
    int ret;
    int scale   = 1;
    unsigned dc = 0;

    av_assert0(code >= 0 && code <= 6 && log2w >= 0);

    if (code == 1) {
        log2h--;
        if ((ret = decode_p_block(f, dst, src, log2w, log2h, stride)) < 0)
            return ret;
        return decode_p_block(f, dst + (stride << log2h),
                              src + (stride << log2h),
                              log2w, log2h, stride);
    } else if (code == 2) {
        log2w--;
        if ((ret = decode_p_block(f, dst , src, log2w, log2h, stride)) < 0)
            return ret;
        return decode_p_block(f, dst + (1 << log2w),
                              src + (1 << log2w),
                              log2w, log2h, stride);
    } else if (code == 6) {
        if (bytestream2_get_bytes_left(&f->g2) < 4) {
            av_log(f->avctx, AV_LOG_ERROR, "wordstream overread\n");
            return AVERROR_INVALIDDATA;
        }
        if (log2w) {
            dst[0]      = bytestream2_get_le16u(&f->g2);
            dst[1]      = bytestream2_get_le16u(&f->g2);
        } else {
            dst[0]      = bytestream2_get_le16u(&f->g2);
            dst[stride] = bytestream2_get_le16u(&f->g2);
        }
        return 0;
    }

    if ((code&3)==0 && bytestream2_get_bytes_left(&f->g) < 1) {
        av_log(f->avctx, AV_LOG_ERROR, "bytestream overread\n");
        return AVERROR_INVALIDDATA;
    }

    if (code == 0) {
        src  += f->mv[bytestream2_get_byte(&f->g)];
    } else if (code == 3 && f->version >= 2) {
        return 0;
    } else if (code == 4) {
        src  += f->mv[bytestream2_get_byte(&f->g)];
        if (bytestream2_get_bytes_left(&f->g2) < 2){
            av_log(f->avctx, AV_LOG_ERROR, "wordstream overread\n");
            return AVERROR_INVALIDDATA;
        }
        dc    = bytestream2_get_le16(&f->g2);
    } else if (code == 5) {
        if (bytestream2_get_bytes_left(&f->g2) < 2){
            av_log(f->avctx, AV_LOG_ERROR, "wordstream overread\n");
            return AVERROR_INVALIDDATA;
        }
        av_assert0(start <= src && src <= end);
        scale = 0;
        dc    = bytestream2_get_le16(&f->g2);
    }

    if (start > src || src > end) {
        av_log(f->avctx, AV_LOG_ERROR, "mv out of pic\n");
        return AVERROR_INVALIDDATA;
    }

    mcdc(dst, src, log2w, h, stride, scale, dc);

    return 0;
}

static int decode_p_frame(FourXContext *f, AVFrame *frame,
                          const uint8_t *buf, int length)
{
    int x, y;
    const int width  = f->avctx->width;
    const int height = f->avctx->height;
    uint16_t *dst    = (uint16_t *)frame->data[0];
    const int stride =             frame->linesize[0] >> 1;
    uint16_t *src;
    unsigned int bitstream_size, bytestream_size, wordstream_size, extra,
                 bytestream_offset, wordstream_offset;
    int ret;

    if (!f->last_picture->data[0]) {
        if ((ret = ff_get_buffer(f->avctx, f->last_picture,
                                 AV_GET_BUFFER_FLAG_REF)) < 0) {
            return ret;
        }
        for (y=0; y<f->avctx->height; y++)
            memset(f->last_picture->data[0] + y*f->last_picture->linesize[0], 0, 2*f->avctx->width);
    }

    src = (uint16_t *)f->last_picture->data[0];

    if (f->version > 1) {
        extra           = 20;
        if (length < extra)
            return AVERROR_INVALIDDATA;
        bitstream_size  = AV_RL32(buf + 8);
        wordstream_size = AV_RL32(buf + 12);
        bytestream_size = AV_RL32(buf + 16);
    } else {
        extra           = 0;
        bitstream_size  = AV_RL16(buf - 4);
        wordstream_size = AV_RL16(buf - 2);
        bytestream_size = FFMAX(length - bitstream_size - wordstream_size, 0);
    }

    if (bitstream_size > length || bitstream_size >= INT_MAX/8 ||
        bytestream_size > length - bitstream_size ||
        wordstream_size > length - bytestream_size - bitstream_size ||
        extra > length - bytestream_size - bitstream_size - wordstream_size) {
        av_log(f->avctx, AV_LOG_ERROR, "lengths %d %d %d %d\n", bitstream_size, bytestream_size, wordstream_size,
        bitstream_size+ bytestream_size+ wordstream_size - length);
        return AVERROR_INVALIDDATA;
    }

    av_fast_malloc(&f->bitstream_buffer, &f->bitstream_buffer_size,
                   bitstream_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!f->bitstream_buffer)
        return AVERROR(ENOMEM);
    f->dsp.bswap_buf(f->bitstream_buffer, (const uint32_t*)(buf + extra),
                     bitstream_size / 4);
    memset((uint8_t*)f->bitstream_buffer + bitstream_size,
           0, FF_INPUT_BUFFER_PADDING_SIZE);
    init_get_bits(&f->gb, f->bitstream_buffer, 8 * bitstream_size);

    wordstream_offset = extra + bitstream_size;
    bytestream_offset = extra + bitstream_size + wordstream_size;
    bytestream2_init(&f->g2, buf + wordstream_offset,
                     length - wordstream_offset);
    bytestream2_init(&f->g, buf + bytestream_offset,
                     length - bytestream_offset);

    init_mv(f, frame->linesize[0]);

    for (y = 0; y < height; y += 8) {
        for (x = 0; x < width; x += 8)
            if ((ret = decode_p_block(f, dst + x, src + x, 3, 3, stride)) < 0)
                return ret;
        src += 8 * stride;
        dst += 8 * stride;
    }

    return 0;
}

/**
 * decode block and dequantize.
 * Note this is almost identical to MJPEG.
 */
static int decode_i_block(FourXContext *f, int16_t *block)
{
    int code, i, j, level, val;

    if (get_bits_left(&f->gb) < 2){
        av_log(f->avctx, AV_LOG_ERROR, "%d bits left before decode_i_block()\n", get_bits_left(&f->gb));
        return -1;
    }

    /* DC coef */
    val = get_vlc2(&f->pre_gb, f->pre_vlc.table, ACDC_VLC_BITS, 3);
    if (val >> 4) {
        av_log(f->avctx, AV_LOG_ERROR, "error dc run != 0\n");
        return AVERROR_INVALIDDATA;
    }

    if (val)
        val = get_xbits(&f->gb, val);

    val        = val * dequant_table[0] + f->last_dc;
    f->last_dc = block[0] = val;
    /* AC coefs */
    i = 1;
    for (;;) {
        code = get_vlc2(&f->pre_gb, f->pre_vlc.table, ACDC_VLC_BITS, 3);

        /* EOB */
        if (code == 0)
            break;
        if (code == 0xf0) {
            i += 16;
        } else {
            if (code & 0xf) {
                level = get_xbits(&f->gb, code & 0xf);
            } else {
                av_log(f->avctx, AV_LOG_ERROR, "0 coeff\n");
                return AVERROR_INVALIDDATA;
            }
            i    += code >> 4;
            if (i >= 64) {
                av_log(f->avctx, AV_LOG_ERROR, "run %d oveflow\n", i);
                return 0;
            }

            j = ff_zigzag_direct[i];
            block[j] = level * dequant_table[j];
            i++;
            if (i >= 64)
                break;
        }
    }

    return 0;
}

static inline void idct_put(FourXContext *f, AVFrame *frame, int x, int y)
{
    int16_t (*block)[64] = f->block;
    int stride           = frame->linesize[0] >> 1;
    int i;
    uint16_t *dst = ((uint16_t*)frame->data[0]) + y * stride + x;

    for (i = 0; i < 4; i++) {
        block[i][0] += 0x80 * 8 * 8;
        idct(block[i]);
    }

    if (!(f->avctx->flags & CODEC_FLAG_GRAY)) {
        for (i = 4; i < 6; i++)
            idct(block[i]);
    }

    /* Note transform is:
     * y  = ( 1b + 4g + 2r) / 14
     * cb = ( 3b - 2g - 1r) / 14
     * cr = (-1b - 4g + 5r) / 14 */
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            int16_t *temp = block[(x >> 2) + 2 * (y >> 2)] +
                            2 * (x & 3) + 2 * 8 * (y & 3); // FIXME optimize
            int cb = block[4][x + 8 * y];
            int cr = block[5][x + 8 * y];
            int cg = (cb + cr) >> 1;
            int y;

            cb += cb;

            y               = temp[0];
            dst[0]          = ((y + cb) >> 3) + (((y - cg) & 0xFC) << 3) + (((y + cr) & 0xF8) << 8);
            y               = temp[1];
            dst[1]          = ((y + cb) >> 3) + (((y - cg) & 0xFC) << 3) + (((y + cr) & 0xF8) << 8);
            y               = temp[8];
            dst[stride]     = ((y + cb) >> 3) + (((y - cg) & 0xFC) << 3) + (((y + cr) & 0xF8) << 8);
            y               = temp[9];
            dst[1 + stride] = ((y + cb) >> 3) + (((y - cg) & 0xFC) << 3) + (((y + cr) & 0xF8) << 8);
            dst            += 2;
        }
        dst += 2 * stride - 2 * 8;
    }
}

static int decode_i_mb(FourXContext *f)
{
    int ret;
    int i;

    f->dsp.clear_blocks(f->block[0]);

    for (i = 0; i < 6; i++)
        if ((ret = decode_i_block(f, f->block[i])) < 0)
            return ret;

    return 0;
}

static const uint8_t *read_huffman_tables(FourXContext *f,
                                          const uint8_t * const buf,
                                          int buf_size)
{
    int frequency[512] = { 0 };
    uint8_t flag[512];
    int up[512];
    uint8_t len_tab[257];
    int bits_tab[257];
    int start, end;
    const uint8_t *ptr = buf;
    const uint8_t *ptr_end = buf + buf_size;
    int j;

    memset(up, -1, sizeof(up));

    start = *ptr++;
    end   = *ptr++;
    for (;;) {
        int i;

        if (ptr_end - ptr < FFMAX(end - start + 1, 0) + 1) {
            av_log(f->avctx, AV_LOG_ERROR, "invalid data in read_huffman_tables\n");
            return NULL;
        }

        for (i = start; i <= end; i++)
            frequency[i] = *ptr++;
        start = *ptr++;
        if (start == 0)
            break;

        end = *ptr++;
    }
    frequency[256] = 1;

    while ((ptr - buf) & 3)
        ptr++; // 4byte align

    if (ptr > ptr_end) {
        av_log(f->avctx, AV_LOG_ERROR, "ptr overflow in read_huffman_tables\n");
        return NULL;
    }

    for (j = 257; j < 512; j++) {
        int min_freq[2] = { 256 * 256, 256 * 256 };
        int smallest[2] = { 0, 0 };
        int i;
        for (i = 0; i < j; i++) {
            if (frequency[i] == 0)
                continue;
            if (frequency[i] < min_freq[1]) {
                if (frequency[i] < min_freq[0]) {
                    min_freq[1] = min_freq[0];
                    smallest[1] = smallest[0];
                    min_freq[0] = frequency[i];
                    smallest[0] = i;
                } else {
                    min_freq[1] = frequency[i];
                    smallest[1] = i;
                }
            }
        }
        if (min_freq[1] == 256 * 256)
            break;

        frequency[j]           = min_freq[0] + min_freq[1];
        flag[smallest[0]]      = 0;
        flag[smallest[1]]      = 1;
        up[smallest[0]]        =
        up[smallest[1]]        = j;
        frequency[smallest[0]] = frequency[smallest[1]] = 0;
    }

    for (j = 0; j < 257; j++) {
        int node, len = 0, bits = 0;

        for (node = j; up[node] != -1; node = up[node]) {
            bits += flag[node] << len;
            len++;
            if (len > 31)
                // can this happen at all ?
                av_log(f->avctx, AV_LOG_ERROR,
                       "vlc length overflow\n");
        }

        bits_tab[j] = bits;
        len_tab[j]  = len;
    }

    if (init_vlc(&f->pre_vlc, ACDC_VLC_BITS, 257, len_tab, 1, 1,
                 bits_tab, 4, 4, 0))
        return NULL;

    return ptr;
}

static int mix(int c0, int c1)
{
    int blue  =  2 * (c0 & 0x001F) + (c1 & 0x001F);
    int green = (2 * (c0 & 0x03E0) + (c1 & 0x03E0)) >> 5;
    int red   =  2 * (c0 >> 10)    + (c1 >> 10);
    return red / 3 * 1024 + green / 3 * 32 + blue / 3;
}

static int decode_i2_frame(FourXContext *f, AVFrame *frame, const uint8_t *buf, int length)
{
    int x, y, x2, y2;
    const int width  = f->avctx->width;
    const int height = f->avctx->height;
    const int mbs    = (FFALIGN(width, 16) >> 4) * (FFALIGN(height, 16) >> 4);
    uint16_t *dst    = (uint16_t*)frame->data[0];
    const int stride =            frame->linesize[0]>>1;
    const uint8_t *buf_end = buf + length;
    GetByteContext g3;

    if (length < mbs * 8) {
        av_log(f->avctx, AV_LOG_ERROR, "packet size too small\n");
        return AVERROR_INVALIDDATA;
    }
    bytestream2_init(&g3, buf, length);

    for (y = 0; y < height; y += 16) {
        for (x = 0; x < width; x += 16) {
            unsigned int color[4] = { 0 }, bits;
            if (buf_end - buf < 8)
                return -1;
            // warning following is purely guessed ...
            color[0] = bytestream2_get_le16u(&g3);
            color[1] = bytestream2_get_le16u(&g3);

            if (color[0] & 0x8000)
                av_log(f->avctx, AV_LOG_ERROR, "unk bit 1\n");
            if (color[1] & 0x8000)
                av_log(f->avctx, AV_LOG_ERROR, "unk bit 2\n");

            color[2] = mix(color[0], color[1]);
            color[3] = mix(color[1], color[0]);

            bits = bytestream2_get_le32u(&g3);
            for (y2 = 0; y2 < 16; y2++) {
                for (x2 = 0; x2 < 16; x2++) {
                    int index = 2 * (x2 >> 2) + 8 * (y2 >> 2);
                    dst[y2 * stride + x2] = color[(bits >> index) & 3];
                }
            }
            dst += 16;
        }
        dst += 16 * stride - x;
    }

    return 0;
}

static int decode_i_frame(FourXContext *f, AVFrame *frame, const uint8_t *buf, int length)
{
    int x, y, ret;
    const int width  = f->avctx->width;
    const int height = f->avctx->height;
    const unsigned int bitstream_size = AV_RL32(buf);
    unsigned int prestream_size;
    const uint8_t *prestream;

    if (bitstream_size > (1 << 26))
        return AVERROR_INVALIDDATA;

    if (length < bitstream_size + 12) {
        av_log(f->avctx, AV_LOG_ERROR, "packet size too small\n");
        return AVERROR_INVALIDDATA;
    }

    prestream_size = 4 * AV_RL32(buf + bitstream_size + 4);
    prestream      =             buf + bitstream_size + 12;

    if (prestream_size + bitstream_size + 12 != length
        || prestream_size > (1 << 26)) {
        av_log(f->avctx, AV_LOG_ERROR, "size mismatch %d %d %d\n",
               prestream_size, bitstream_size, length);
        return AVERROR_INVALIDDATA;
    }

    prestream = read_huffman_tables(f, prestream, prestream_size);
    if (!prestream) {
        av_log(f->avctx, AV_LOG_ERROR, "Error reading Huffman tables.\n");
        return AVERROR_INVALIDDATA;
    }

    av_assert0(prestream <= buf + length);

    init_get_bits(&f->gb, buf + 4, 8 * bitstream_size);

    prestream_size = length + buf - prestream;

    av_fast_malloc(&f->bitstream_buffer, &f->bitstream_buffer_size,
                   prestream_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!f->bitstream_buffer)
        return AVERROR(ENOMEM);
    f->dsp.bswap_buf(f->bitstream_buffer, (const uint32_t*)prestream,
                     prestream_size / 4);
    memset((uint8_t*)f->bitstream_buffer + prestream_size,
           0, FF_INPUT_BUFFER_PADDING_SIZE);
    init_get_bits(&f->pre_gb, f->bitstream_buffer, 8 * prestream_size);

    f->last_dc = 0 * 128 * 8 * 8;

    for (y = 0; y < height; y += 16) {
        for (x = 0; x < width; x += 16) {
            if ((ret = decode_i_mb(f)) < 0)
                return ret;

            idct_put(f, frame, x, y);
        }
    }

    if (get_vlc2(&f->pre_gb, f->pre_vlc.table, ACDC_VLC_BITS, 3) != 256)
        av_log(f->avctx, AV_LOG_ERROR, "end mismatch\n");

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf    = avpkt->data;
    int buf_size          = avpkt->size;
    FourXContext *const f = avctx->priv_data;
    AVFrame *picture      = data;
    int i, frame_4cc, frame_size, ret;

    if (buf_size < 20)
        return AVERROR_INVALIDDATA;

    av_assert0(avctx->width % 16 == 0 && avctx->height % 16 == 0);

    if (buf_size < AV_RL32(buf + 4) + 8) {
        av_log(f->avctx, AV_LOG_ERROR, "size mismatch %d %d\n",
               buf_size, AV_RL32(buf + 4));
        return AVERROR_INVALIDDATA;
    }

    frame_4cc = AV_RL32(buf);

    if (frame_4cc == AV_RL32("cfrm")) {
        int free_index       = -1;
        int id, whole_size;
        const int data_size  = buf_size - 20;
        CFrameBuffer *cfrm;

        if (f->version <= 1) {
            av_log(f->avctx, AV_LOG_ERROR, "cfrm in version %d\n", f->version);
            return AVERROR_INVALIDDATA;
        }

        id         = AV_RL32(buf + 12);
        whole_size = AV_RL32(buf + 16);

        if (data_size < 0 || whole_size < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "sizes invalid\n");
            return AVERROR_INVALIDDATA;
        }

        for (i = 0; i < CFRAME_BUFFER_COUNT; i++)
            if (f->cfrm[i].id && f->cfrm[i].id < avctx->frame_number)
                av_log(f->avctx, AV_LOG_ERROR, "lost c frame %d\n",
                       f->cfrm[i].id);

        for (i = 0; i < CFRAME_BUFFER_COUNT; i++) {
            if (f->cfrm[i].id == id)
                break;
            if (f->cfrm[i].size == 0)
                free_index = i;
        }

        if (i >= CFRAME_BUFFER_COUNT) {
            i             = free_index;
            f->cfrm[i].id = id;
        }
        cfrm = &f->cfrm[i];

        if (data_size > UINT_MAX -  cfrm->size - FF_INPUT_BUFFER_PADDING_SIZE)
            return AVERROR_INVALIDDATA;

        cfrm->data = av_fast_realloc(cfrm->data, &cfrm->allocated_size,
                                     cfrm->size + data_size + FF_INPUT_BUFFER_PADDING_SIZE);
        // explicit check needed as memcpy below might not catch a NULL
        if (!cfrm->data) {
            av_log(f->avctx, AV_LOG_ERROR, "realloc failure\n");
            return AVERROR(ENOMEM);
        }

        memcpy(cfrm->data + cfrm->size, buf + 20, data_size);
        cfrm->size += data_size;

        if (cfrm->size >= whole_size) {
            buf        = cfrm->data;
            frame_size = cfrm->size;

            if (id != avctx->frame_number)
                av_log(f->avctx, AV_LOG_ERROR, "cframe id mismatch %d %d\n",
                       id, avctx->frame_number);

            if (f->version <= 1)
                return AVERROR_INVALIDDATA;

            cfrm->size = cfrm->id = 0;
            frame_4cc  = AV_RL32("pfrm");
        } else
            return buf_size;
    } else {
        buf        = buf      + 12;
        frame_size = buf_size - 12;
    }

    FFSWAP(AVFrame*, f->current_picture, f->last_picture);

    // alternatively we would have to use our own buffer management
    avctx->flags |= CODEC_FLAG_EMU_EDGE;

    if ((ret = ff_reget_buffer(avctx, f->current_picture)) < 0)
        return ret;

    if (frame_4cc == AV_RL32("ifr2")) {
        f->current_picture->pict_type = AV_PICTURE_TYPE_I;
        if ((ret = decode_i2_frame(f, f->current_picture, buf - 4, frame_size + 4)) < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "decode i2 frame failed\n");
            return ret;
        }
    } else if (frame_4cc == AV_RL32("ifrm")) {
        f->current_picture->pict_type = AV_PICTURE_TYPE_I;
        if ((ret = decode_i_frame(f, f->current_picture, buf, frame_size)) < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "decode i frame failed\n");
            return ret;
        }
    } else if (frame_4cc == AV_RL32("pfrm") || frame_4cc == AV_RL32("pfr2")) {
        f->current_picture->pict_type = AV_PICTURE_TYPE_P;
        if ((ret = decode_p_frame(f, f->current_picture, buf, frame_size)) < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "decode p frame failed\n");
            return ret;
        }
    } else if (frame_4cc == AV_RL32("snd_")) {
        av_log(avctx, AV_LOG_ERROR, "ignoring snd_ chunk length:%d\n",
               buf_size);
    } else {
        av_log(avctx, AV_LOG_ERROR, "ignoring unknown chunk length:%d\n",
               buf_size);
    }

    f->current_picture->key_frame = f->current_picture->pict_type == AV_PICTURE_TYPE_I;

    if ((ret = av_frame_ref(picture, f->current_picture)) < 0)
        return ret;
    *got_frame = 1;

    emms_c();

    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    FourXContext * const f = avctx->priv_data;

    if (avctx->extradata_size != 4 || !avctx->extradata) {
        av_log(avctx, AV_LOG_ERROR, "extradata wrong or missing\n");
        return AVERROR_INVALIDDATA;
    }
    if((avctx->width % 16) || (avctx->height % 16)) {
        av_log(avctx, AV_LOG_ERROR, "unsupported width/height\n");
        return AVERROR_INVALIDDATA;
    }

    f->version = AV_RL32(avctx->extradata) >> 16;
    ff_dsputil_init(&f->dsp, avctx);
    f->avctx = avctx;
    init_vlcs(f);

    if (f->version > 2)
        avctx->pix_fmt = AV_PIX_FMT_RGB565;
    else
        avctx->pix_fmt = AV_PIX_FMT_BGR555;

    f->current_picture = av_frame_alloc();
    f->last_picture    = av_frame_alloc();
    if (!f->current_picture  || !f->last_picture)
        return AVERROR(ENOMEM);

    return 0;
}


static av_cold int decode_end(AVCodecContext *avctx)
{
    FourXContext * const f = avctx->priv_data;
    int i;

    av_freep(&f->bitstream_buffer);
    f->bitstream_buffer_size = 0;
    for (i = 0; i < CFRAME_BUFFER_COUNT; i++) {
        av_freep(&f->cfrm[i].data);
        f->cfrm[i].allocated_size = 0;
    }
    ff_free_vlc(&f->pre_vlc);
    av_frame_free(&f->current_picture);
    av_frame_free(&f->last_picture);

    return 0;
}

AVCodec ff_fourxm_decoder = {
    .name           = "4xm",
    .long_name      = NULL_IF_CONFIG_SMALL("4X Movie"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_4XM,
    .priv_data_size = sizeof(FourXContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
