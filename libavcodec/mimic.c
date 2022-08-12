/*
 * Copyright (C) 2005  Ole André Vadla Ravnås <oleavr@gmail.com>
 * Copyright (C) 2008  Ramiro Polla
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

#include <stdint.h>

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "bytestream.h"
#include "bswapdsp.h"
#include "hpeldsp.h"
#include "idctdsp.h"
#include "progressframe.h"
#include "thread.h"

#define MIMIC_HEADER_SIZE   20
#define MIMIC_VLC_BITS      11

typedef struct MimicContext {
    AVCodecContext *avctx;

    int             num_vblocks[3];
    int             num_hblocks[3];

    void           *swap_buf;
    int             swap_buf_size;

    int             cur_index;
    int             prev_index;

    ProgressFrame   frames[16];

    DECLARE_ALIGNED(32, int16_t, dct_block)[64];

    GetBitContext   gb;
    uint8_t         permutated_scantable[64];
    BlockDSPContext bdsp;
    BswapDSPContext bbdsp;
    HpelDSPContext  hdsp;
    IDCTDSPContext  idsp;

    /* Kept in the context so multithreading can have a constant to read from */
    int             next_cur_index;
    int             next_prev_index;
} MimicContext;

static VLCElem block_vlc[4368];

static const uint8_t huffsyms[] = {
    0x10, 0x20, 0x30, 0x00, 0x11, 0x40, 0x50, 0x12, 0x13, 0x21, 0x31, 0x60,
    0x14, 0x15, 0x16, 0x22, 0x41, 0x17, 0x18, 0x23, 0x24, 0x25, 0x32, 0x42,
    0x51, 0x61, 0x70, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x52, 0x53, 0x54, 0x55, 0x56,
    0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x62, 0x63, 0x64, 0x65,
    0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x71, 0x72, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E,
};

static const uint8_t huffbits[] = {
     2,  2,  3,  4,  4,  4,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,
     8,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12,
    13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17,
    17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21,
    22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 25, 26, 26,
    26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30,
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

static av_cold int mimic_decode_end(AVCodecContext *avctx)
{
    MimicContext *ctx = avctx->priv_data;

    av_freep(&ctx->swap_buf);
    ctx->swap_buf_size = 0;

    for (int i = 0; i < FF_ARRAY_ELEMS(ctx->frames); i++)
        ff_progress_frame_unref(&ctx->frames[i]);

    return 0;
}

static av_cold void mimic_init_static(void)
{
    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(block_vlc, MIMIC_VLC_BITS,
                                       FF_ARRAY_ELEMS(huffbits),
                                       huffbits, 1, huffsyms, 1, 1, 0, 0);
}

static av_cold int mimic_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    MimicContext *ctx = avctx->priv_data;

    ctx->prev_index = 0;
    ctx->cur_index  = 15;

    ff_blockdsp_init(&ctx->bdsp);
    ff_bswapdsp_init(&ctx->bbdsp);
    ff_hpeldsp_init(&ctx->hdsp, avctx->flags);
    ff_idctdsp_init(&ctx->idsp, avctx);
    ff_permute_scantable(ctx->permutated_scantable, col_zag, ctx->idsp.idct_permutation);

    ff_thread_once(&init_static_once, mimic_init_static);

    return 0;
}

#if HAVE_THREADS
static int mimic_decode_update_thread_context(AVCodecContext *avctx, const AVCodecContext *avctx_from)
{
    MimicContext *dst = avctx->priv_data, *src = avctx_from->priv_data;

    if (avctx == avctx_from)
        return 0;

    dst->cur_index  = src->next_cur_index;
    dst->prev_index = src->next_prev_index;

    for (int i = 0; i < FF_ARRAY_ELEMS(dst->frames); i++) {
        ff_progress_frame_unref(&dst->frames[i]);
        if (i != src->next_cur_index && src->frames[i].f)
            ff_progress_frame_ref(&dst->frames[i], &src->frames[i]);
    }

    return 0;
}
#endif

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
    int16_t *block = ctx->dct_block;
    unsigned int pos;

    ctx->bdsp.clear_block(block);

    block[0] = get_bits(&ctx->gb, 8) << 3;

    for (pos = 1; pos < num_coeffs; pos++) {
        uint32_t vlc, num_bits;
        int value;
        int coeff;

        vlc = get_vlc2(&ctx->gb, block_vlc, MIMIC_VLC_BITS, 3);
        if (!vlc) /* end-of-block code */
            return 0;
        if (vlc == -1)
            return AVERROR_INVALIDDATA;

        /* pos_add and num_bits are coded in the vlc code */
        pos     += vlc & 15; // pos_add
        num_bits = vlc >> 4; // num_bits

        if (pos >= 64)
            return AVERROR_INVALIDDATA;

        value = get_bits(&ctx->gb, num_bits);

        /* FFmpeg's IDCT behaves somewhat different from the original code, so
         * a factor of 4 was added to the input */

        coeff = ((int8_t*)vlcdec_lookup[num_bits])[value];
        if (pos < 3)
            coeff *= 16;
        else /* TODO Use >> 10 instead of / 1001 */
            coeff = (coeff * qscale) / 1001;

        block[ctx->permutated_scantable[pos]] = coeff;
    }

    return 0;
}

static int decode(MimicContext *ctx, int quality, int num_coeffs,
                  int is_iframe)
{
    int ret, y, x, plane, cur_row = 0;

    for (plane = 0; plane < 3; plane++) {
        const int is_chroma = !!plane;
        const int qscale    = av_clip(10000 - quality, is_chroma ? 1000 : 2000,
                                      10000) << 2;
        const int stride    = ctx->frames[ctx->cur_index ].f->linesize[plane];
        uint8_t       *dst  = ctx->frames[ctx->cur_index ].f->data[plane];
        /* src is unused for I frames; set to avoid UB pointer arithmetic. */
        const uint8_t *src  = is_iframe ? dst : ctx->frames[ctx->prev_index].f->data[plane];

        for (y = 0; y < ctx->num_vblocks[plane]; y++) {
            for (x = 0; x < ctx->num_hblocks[plane]; x++) {
                /* Check for a change condition in the current block.
                 * - iframes always change.
                 * - Luma plane changes on get_bits1 == 0
                 * - Chroma planes change on get_bits1 == 1 */
                if (is_iframe || get_bits1(&ctx->gb) == is_chroma) {
                    /* Luma planes may use a backreference from the 15 last
                     * frames preceding the previous. (get_bits1 == 1)
                     * Chroma planes don't use backreferences. */
                    if (is_chroma || is_iframe || !get_bits1(&ctx->gb)) {
                        if ((ret = vlc_decode_block(ctx, num_coeffs,
                                                    qscale)) < 0) {
                            av_log(ctx->avctx, AV_LOG_ERROR, "Error decoding "
                                   "block.\n");
                            return ret;
                        }
                        ctx->idsp.idct_put(dst, stride, ctx->dct_block);
                    } else {
                        unsigned int backref = get_bits(&ctx->gb, 4);
                        int index            = (ctx->cur_index + backref) & 15;

                        if (index != ctx->cur_index && ctx->frames[index].f) {
                            const uint8_t *p = ctx->frames[index].f->data[0];
                            ff_progress_frame_await(&ctx->frames[index], cur_row);
                            p += src -
                                 ctx->frames[ctx->prev_index].f->data[plane];
                            ctx->hdsp.put_pixels_tab[1][0](dst, p, stride, 8);
                        } else {
                            av_log(ctx->avctx, AV_LOG_ERROR,
                                     "No such backreference! Buggy sample.\n");
                        }
                    }
                } else {
                    ff_progress_frame_await(&ctx->frames[ctx->prev_index], cur_row);
                    ctx->hdsp.put_pixels_tab[1][0](dst, src, stride, 8);
                }
                src += 8;
                dst += 8;
            }
            src += (stride - ctx->num_hblocks[plane]) << 3;
            dst += (stride - ctx->num_hblocks[plane]) << 3;

            ff_progress_frame_report(&ctx->frames[ctx->cur_index], cur_row++);
        }
    }

    return 0;
}

/**
 * Flip the buffer upside-down and put it in the YVU order to revert the
 * way Mimic encodes frames.
 */
static void flip_swap_frame(AVFrame *f)
{
    int i;
    uint8_t *data_1 = f->data[1];
    f->data[0] = f->data[0] + ( f->height       - 1) * f->linesize[0];
    f->data[1] = f->data[2] + ((f->height >> 1) - 1) * f->linesize[2];
    f->data[2] = data_1     + ((f->height >> 1) - 1) * f->linesize[1];
    for (i = 0; i < 3; i++)
        f->linesize[i] *= -1;
}

static int mimic_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                              int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int swap_buf_size  = buf_size - MIMIC_HEADER_SIZE;
    MimicContext *ctx  = avctx->priv_data;
    GetByteContext gb;
    int is_pframe;
    int width, height;
    int quality, num_coeffs;
    int res;

    if (buf_size <= MIMIC_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "insufficient data\n");
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init(&gb, buf, MIMIC_HEADER_SIZE);
    bytestream2_skip(&gb, 2); /* some constant (always 256) */
    quality    = bytestream2_get_le16u(&gb);
    width      = bytestream2_get_le16u(&gb);
    height     = bytestream2_get_le16u(&gb);
    bytestream2_skip(&gb, 4); /* some constant */
    is_pframe  = bytestream2_get_le32u(&gb);
    num_coeffs = bytestream2_get_byteu(&gb);
    bytestream2_skip(&gb, 3); /* some constant */

    if (!ctx->avctx) {
        int i;

        if (!(width == 160 && height == 120) &&
            !(width == 320 && height == 240)) {
            av_log(avctx, AV_LOG_ERROR, "invalid width/height!\n");
            return AVERROR_INVALIDDATA;
        }

        res = ff_set_dimensions(avctx, width, height);
        if (res < 0)
            return res;

        ctx->avctx     = avctx;
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        for (i = 0; i < 3; i++) {
            ctx->num_vblocks[i] = AV_CEIL_RSHIFT(height,   3 + !!i);
            ctx->num_hblocks[i] =                width >> (3 + !!i);
        }
    } else if (width != ctx->avctx->width || height != ctx->avctx->height) {
        avpriv_request_sample(avctx, "Resolution changing");
        return AVERROR_PATCHWELCOME;
    }

    if (is_pframe && !ctx->frames[ctx->prev_index].f) {
        av_log(avctx, AV_LOG_ERROR, "decoding must start with keyframe\n");
        return AVERROR_INVALIDDATA;
    }

    ff_progress_frame_unref(&ctx->frames[ctx->cur_index]);
    res = ff_progress_frame_get_buffer(avctx, &ctx->frames[ctx->cur_index],
                                       AV_GET_BUFFER_FLAG_REF);
    if (res < 0)
        return res;
    ctx->frames[ctx->cur_index].f->pict_type = is_pframe ? AV_PICTURE_TYPE_P :
                                                           AV_PICTURE_TYPE_I;

    ctx->next_prev_index = ctx->cur_index;
    ctx->next_cur_index  = (ctx->cur_index - 1) & 15;

    ff_thread_finish_setup(avctx);

    av_fast_padded_malloc(&ctx->swap_buf, &ctx->swap_buf_size, swap_buf_size);
    if (!ctx->swap_buf)
        return AVERROR(ENOMEM);

    ctx->bbdsp.bswap_buf(ctx->swap_buf,
                         (const uint32_t *) (buf + MIMIC_HEADER_SIZE),
                         swap_buf_size >> 2);
    init_get_bits(&ctx->gb, ctx->swap_buf, swap_buf_size << 3);

    res = decode(ctx, quality, num_coeffs, !is_pframe);
    ff_progress_frame_report(&ctx->frames[ctx->cur_index], INT_MAX);
    if (res < 0) {
        if (!(avctx->active_thread_type & FF_THREAD_FRAME))
            ff_progress_frame_unref(&ctx->frames[ctx->cur_index]);
        return res;
    }

    if ((res = av_frame_ref(rframe, ctx->frames[ctx->cur_index].f)) < 0)
        return res;
    *got_frame      = 1;

    flip_swap_frame(rframe);

    ctx->prev_index = ctx->next_prev_index;
    ctx->cur_index  = ctx->next_cur_index;

    return buf_size;
}

const FFCodec ff_mimic_decoder = {
    .p.name                = "mimic",
    CODEC_LONG_NAME("Mimic"),
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_MIMIC,
    .priv_data_size        = sizeof(MimicContext),
    .init                  = mimic_decode_init,
    .close                 = mimic_decode_end,
    FF_CODEC_DECODE_CB(mimic_decode_frame),
    .p.capabilities        = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    UPDATE_THREAD_CONTEXT(mimic_decode_update_thread_context),
    .caps_internal         = FF_CODEC_CAP_USES_PROGRESSFRAMES |
                             FF_CODEC_CAP_INIT_CLEANUP,
};
