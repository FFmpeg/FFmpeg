/*
 * Copyright (C) 2005  Ole André Vadla Ravnås <oleavr@gmail.com>
 * Copyright (C) 2008  Ramiro Polla <ramiro@lisha.ufsc.br>
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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "avcodec.h"
#include "bitstream.h"
#include "bytestream.h"
#include "dsputil.h"

#define MIMIC_HEADER_SIZE   20

typedef struct {
    AVCodecContext *avctx;

    int             num_vblocks[3];
    int             num_hblocks[3];

    uint8_t        *swap_buf;
    int             swap_buf_size;

    int             cur_index;
    int             prev_index;

    AVFrame         buf_ptrs    [16];
    AVPicture       flipped_ptrs[16];

    DECLARE_ALIGNED_16(DCTELEM, dct_block[64]);

    GetBitContext   gb;
    ScanTable       scantable;
    DSPContext      dsp;
    VLC             vlc;
} MimicContext;

static const uint32_t huffcodes[] = {
    0x0000000a, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000b,
    0x0000001b, 0x00000038, 0x00000078, 0x00000079, 0x0000007a, 0x000000f9,
    0x000000fa, 0x000003fb, 0x000007f8, 0x000007f9, 0x000007fa, 0x000007fb,
    0x00000ff8, 0x00000ff9, 0x00000001, 0x00000039, 0x0000007b, 0x000000fb,
    0x000001f8, 0x000001f9, 0x00000ffa, 0x00000ffb, 0x00001ff8, 0x00001ff9,
    0x00001ffa, 0x00001ffb, 0x00003ff8, 0x00003ff9, 0x00003ffa, 0x00000000,
    0x00000004, 0x0000003a, 0x000001fa, 0x00003ffb, 0x00007ff8, 0x00007ff9,
    0x00007ffa, 0x00007ffb, 0x0000fff8, 0x0000fff9, 0x0000fffa, 0x0000fffb,
    0x0001fff8, 0x0001fff9, 0x0001fffa, 0x00000000, 0x0000000c, 0x000000f8,
    0x000001fb, 0x0001fffb, 0x0003fff8, 0x0003fff9, 0x0003fffa, 0x0003fffb,
    0x0007fff8, 0x0007fff9, 0x0007fffa, 0x0007fffb, 0x000ffff8, 0x000ffff9,
    0x000ffffa, 0x00000000, 0x0000001a, 0x000003f8, 0x000ffffb, 0x001ffff8,
    0x001ffff9, 0x001ffffa, 0x001ffffb, 0x003ffff8, 0x003ffff9, 0x003ffffa,
    0x003ffffb, 0x007ffff8, 0x007ffff9, 0x007ffffa, 0x007ffffb, 0x00000000,
    0x0000003b, 0x000003f9, 0x00fffff8, 0x00fffff9, 0x00fffffa, 0x00fffffb,
    0x01fffff8, 0x01fffff9, 0x01fffffa, 0x01fffffb, 0x03fffff8, 0x03fffff9,
    0x03fffffa, 0x03fffffb, 0x07fffff8, 0x00000000, 0x000003fa, 0x07fffff9,
    0x07fffffa, 0x07fffffb, 0x0ffffff8, 0x0ffffff9, 0x0ffffffa, 0x0ffffffb,
    0x1ffffff8, 0x1ffffff9, 0x1ffffffa, 0x1ffffffb, 0x3ffffff8, 0x3ffffff9,
    0x3ffffffa,
};

static const uint8_t huffbits[] = {
     4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  2,  4,  5,  6,  7,  7,  7,  8,
     8, 10, 11, 11, 11, 11, 12, 12,  2,  6,  7,  8,
     9,  9, 12, 12, 13, 13, 13, 13, 14, 14, 14,  0,
     3,  6,  9, 14, 15, 15, 15, 15, 16, 16, 16, 16,
    17, 17, 17,  0,  4,  8,  9, 17, 18, 18, 18, 18,
    19, 19, 19, 19, 20, 20, 20,  0,  5, 10, 20, 21,
    21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23,  0,
     6, 10, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26,
    26, 26, 27,  0, 10, 27, 27, 27, 28, 28, 28, 28,
    29, 29, 29, 29, 30, 30, 30,
};

static const uint8_t col_zag[64] = {
     0,  8,  1,  2,  9, 16, 24, 17,
    10,  3,  4, 11, 18, 25, 32, 40,
    33, 26, 19, 12,  5,  6, 13, 20,
    27, 34, 41, 48, 56, 49, 42, 35,
    28, 21, 14,  7, 15, 22, 29, 36,
    43, 50, 57, 58, 51, 44, 37, 30,
    23, 31, 38, 45, 52, 59, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

static av_cold int mimic_decode_init(AVCodecContext *avctx)
{
    MimicContext *ctx = avctx->priv_data;

    ctx->prev_index = 0;
    ctx->cur_index = 15;

    if(init_vlc(&ctx->vlc, 11, FF_ARRAY_ELEMS(huffbits),
                 huffbits, 1, 1, huffcodes, 4, 4, 0)) {
        av_log(avctx, AV_LOG_ERROR, "error initializing vlc table\n");
        return -1;
    }
    dsputil_init(&ctx->dsp, avctx);
    ff_init_scantable(ctx->dsp.idct_permutation, &ctx->scantable, col_zag);

    return 0;
}

static const int8_t vlcdec_lookup[9][64] = {
    {    0, },
    {   -1,   1, },
    {   -3,   3,   -2,   2, },
    {   -7,   7,   -6,   6,   -5,   5,   -4,   4, },
    {  -15,  15,  -14,  14,  -13,  13,  -12,  12,
       -11,  11,  -10,  10,   -9,   9,   -8,   8, },
    {  -31,  31,  -30,  30,  -29,  29,  -28,  28,
       -27,  27,  -26,  26,  -25,  25,  -24,  24,
       -23,  23,  -22,  22,  -21,  21,  -20,  20,
       -19,  19,  -18,  18,  -17,  17,  -16,  16, },
    {  -63,  63,  -62,  62,  -61,  61,  -60,  60,
       -59,  59,  -58,  58,  -57,  57,  -56,  56,
       -55,  55,  -54,  54,  -53,  53,  -52,  52,
       -51,  51,  -50,  50,  -49,  49,  -48,  48,
       -47,  47,  -46,  46,  -45,  45,  -44,  44,
       -43,  43,  -42,  42,  -41,  41,  -40,  40,
       -39,  39,  -38,  38,  -37,  37,  -36,  36,
       -35,  35,  -34,  34,  -33,  33,  -32,  32, },
    { -127, 127, -126, 126, -125, 125, -124, 124,
      -123, 123, -122, 122, -121, 121, -120, 120,
      -119, 119, -118, 118, -117, 117, -116, 116,
      -115, 115, -114, 114, -113, 113, -112, 112,
      -111, 111, -110, 110, -109, 109, -108, 108,
      -107, 107, -106, 106, -105, 105, -104, 104,
      -103, 103, -102, 102, -101, 101, -100, 100,
       -99,  99,  -98,  98,  -97,  97,  -96,  96, },
    {  -95,  95,  -94,  94,  -93,  93,  -92,  92,
       -91,  91,  -90,  90,  -89,  89,  -88,  88,
       -87,  87,  -86,  86,  -85,  85,  -84,  84,
       -83,  83,  -82,  82,  -81,  81,  -80,  80,
       -79,  79,  -78,  78,  -77,  77,  -76,  76,
       -75,  75,  -74,  74,  -73,  73,  -72,  72,
       -71,  71,  -70,  70,  -69,  69,  -68,  68,
       -67,  67,  -66,  66,  -65,  65,  -64,  64, },
};

static int vlc_decode_block(MimicContext *ctx, int num_coeffs, int qscale)
{
    DCTELEM *block = ctx->dct_block;
    unsigned int pos;

    ctx->dsp.clear_block(block);

    block[0] = get_bits(&ctx->gb, 8) << 3;

    for(pos = 1; pos < num_coeffs; pos++) {
        uint32_t vlc, num_bits;
        int value;
        int coeff;

        vlc = get_vlc2(&ctx->gb, ctx->vlc.table, ctx->vlc.bits, 3);
        if(!vlc) /* end-of-block code */
            return 1;
        if(vlc == -1)
            return 0;

        /* pos_add and num_bits are coded in the vlc code */
        pos +=     vlc&15; // pos_add
        num_bits = vlc>>4; // num_bits

        if(pos >= 64)
            return 0;

        value = get_bits(&ctx->gb, num_bits);

        /* FFmpeg's IDCT behaves somewhat different from the original code, so
         * a factor of 4 was added to the input */

        coeff = vlcdec_lookup[num_bits][value];
        if(pos<3)
            coeff <<= 4;
        else /* TODO Use >> 10 instead of / 1001 */
            coeff = (coeff * qscale) / 1001;

        block[ctx->scantable.permutated[pos]] = coeff;
    }

    return 1;
}

static int decode(MimicContext *ctx, int quality, int num_coeffs,
                  int is_iframe)
{
    int y, x, plane;

    for(plane = 0; plane < 3; plane++) {
        const int is_chroma = !!plane;
        const int qscale = av_clip(10000-quality,is_chroma?1000:2000,10000)<<2;
        const int stride = ctx->flipped_ptrs[ctx->cur_index].linesize[plane];
        const uint8_t *src = ctx->flipped_ptrs[ctx->prev_index].data[plane];
        uint8_t       *dst = ctx->flipped_ptrs[ctx->cur_index ].data[plane];

        for(y = 0; y < ctx->num_vblocks[plane]; y++) {
            for(x = 0; x < ctx->num_hblocks[plane]; x++) {

                /* Check for a change condition in the current block.
                 * - iframes always change.
                 * - Luma plane changes on get_bits1 == 0
                 * - Chroma planes change on get_bits1 == 1 */
                if(is_iframe || get_bits1(&ctx->gb) == is_chroma) {

                    /* Luma planes may use a backreference from the 15 last
                     * frames preceding the previous. (get_bits1 == 1)
                     * Chroma planes don't use backreferences. */
                    if(is_chroma || is_iframe || !get_bits1(&ctx->gb)) {

                        if(!vlc_decode_block(ctx, num_coeffs, qscale))
                            return 0;
                        ctx->dsp.idct_put(dst, stride, ctx->dct_block);
                    } else {
                        unsigned int backref = get_bits(&ctx->gb, 4);
                        int index = (ctx->cur_index+backref)&15;
                        uint8_t *p = ctx->flipped_ptrs[index].data[0];

                        if(p) {
                            p += src -
                                ctx->flipped_ptrs[ctx->prev_index].data[plane];
                            ctx->dsp.put_pixels_tab[1][0](dst, p, stride, 8);
                        } else {
                            av_log(ctx->avctx, AV_LOG_ERROR,
                                     "No such backreference! Buggy sample.\n");
                        }
                    }
                } else {
                    ctx->dsp.put_pixels_tab[1][0](dst, src, stride, 8);
                }
                src += 8;
                dst += 8;
            }
            src += (stride - ctx->num_hblocks[plane])<<3;
            dst += (stride - ctx->num_hblocks[plane])<<3;
        }
    }

    return 1;
}

/**
 * Flip the buffer upside-down and put it in the YVU order to match the
 * way Mimic encodes frames.
 */
static void prepare_avpic(MimicContext *ctx, AVPicture *dst, AVPicture *src)
{
    int i;
    dst->data[0] = src->data[0]+( ctx->avctx->height    -1)*src->linesize[0];
    dst->data[1] = src->data[2]+((ctx->avctx->height>>1)-1)*src->linesize[2];
    dst->data[2] = src->data[1]+((ctx->avctx->height>>1)-1)*src->linesize[1];
    for(i = 0; i < 3; i++)
        dst->linesize[i] = -src->linesize[i];
}

static int mimic_decode_frame(AVCodecContext *avctx, void *data,
                              int *data_size, const uint8_t *buf, int buf_size)
{
    MimicContext *ctx = avctx->priv_data;
    int is_pframe;
    int width, height;
    int quality, num_coeffs;
    int swap_buf_size = buf_size - MIMIC_HEADER_SIZE;

    if(buf_size < MIMIC_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "insufficient data\n");
        return -1;
    }

    buf       += 2; /* some constant (always 256) */
    quality    = bytestream_get_le16(&buf);
    width      = bytestream_get_le16(&buf);
    height     = bytestream_get_le16(&buf);
    buf       += 4; /* some constant */
    is_pframe  = bytestream_get_le32(&buf);
    num_coeffs = bytestream_get_byte(&buf);
    buf       += 3; /* some constant */

    if(!ctx->avctx) {
        int i;

        if(!(width == 160 && height == 120) &&
           !(width == 320 && height == 240)) {
            av_log(avctx, AV_LOG_ERROR, "invalid width/height!\n");
            return -1;
        }

        ctx->avctx     = avctx;
        avctx->width   = width;
        avctx->height  = height;
        avctx->pix_fmt = PIX_FMT_YUV420P;
        for(i = 0; i < 3; i++) {
            ctx->num_vblocks[i] = -((-height) >> (3 + !!i));
            ctx->num_hblocks[i] =     width   >> (3 + !!i) ;
        }
    } else if(width != ctx->avctx->width || height != ctx->avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "resolution changing is not supported\n");
        return -1;
    }

    if(is_pframe && !ctx->buf_ptrs[ctx->prev_index].data[0]) {
        av_log(avctx, AV_LOG_ERROR, "decoding must start with keyframe\n");
        return -1;
    }

    ctx->buf_ptrs[ctx->cur_index].reference = 1;
    if(avctx->get_buffer(avctx, &ctx->buf_ptrs[ctx->cur_index])) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    prepare_avpic(ctx, &ctx->flipped_ptrs[ctx->cur_index],
                  (AVPicture*) &ctx->buf_ptrs[ctx->cur_index]);

    ctx->swap_buf = av_fast_realloc(ctx->swap_buf, &ctx->swap_buf_size,
                                 swap_buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if(!ctx->swap_buf)
        return AVERROR_NOMEM;

    ctx->dsp.bswap_buf((uint32_t*)ctx->swap_buf,
                        (const uint32_t*) buf,
                        swap_buf_size>>2);
    init_get_bits(&ctx->gb, ctx->swap_buf, swap_buf_size << 3);

    if(!decode(ctx, quality, num_coeffs, !is_pframe)) {
        avctx->release_buffer(avctx, &ctx->buf_ptrs[ctx->cur_index]);
        return -1;
    }

    ctx->buf_ptrs[ctx->cur_index].pict_type = is_pframe ? FF_P_TYPE:FF_I_TYPE;
    *(AVFrame*)data = ctx->buf_ptrs[ctx->cur_index];
    *data_size = sizeof(AVFrame);

    ctx->prev_index = ctx->cur_index;
    ctx->cur_index--;
    ctx->cur_index &= 15;

    /* Only release frames that aren't used for backreferences anymore */
    if(ctx->buf_ptrs[ctx->cur_index].data[0])
        avctx->release_buffer(avctx, &ctx->buf_ptrs[ctx->cur_index]);

    return buf_size;
}

static av_cold int mimic_decode_end(AVCodecContext *avctx)
{
    MimicContext *ctx = avctx->priv_data;
    int i;

    av_free(ctx->swap_buf);
    for(i = 0; i < 16; i++)
        if(ctx->buf_ptrs[i].data[0])
            avctx->release_buffer(avctx, &ctx->buf_ptrs[i]);
    free_vlc(&ctx->vlc);

    return 0;
}

AVCodec mimic_decoder = {
    "mimic",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MIMIC,
    sizeof(MimicContext),
    mimic_decode_init,
    NULL,
    mimic_decode_end,
    mimic_decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Mimic"),
};
