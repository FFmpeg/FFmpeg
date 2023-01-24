/*
 * Go2Webinar / Go2Meeting decoder
 * Copyright (c) 2012 Konstantin Shishkov
 * Copyright (c) 2013 Maxim Poliakovski
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
 * Go2Webinar / Go2Meeting decoder
 */

#include <inttypes.h>
#include <zlib.h>

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "elsdec.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "internal.h"
#include "jpegtables.h"
#include "mjpeg.h"
#include "mjpegdec.h"

#define EPIC_PIX_STACK_SIZE 1024
#define EPIC_PIX_STACK_MAX  (EPIC_PIX_STACK_SIZE - 1)

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

typedef struct ePICPixListElem {
    struct ePICPixListElem *next;
    uint32_t               pixel;
    uint8_t                rung;
} ePICPixListElem;

typedef struct ePICPixHashElem {
    uint32_t                pix_id;
    struct ePICPixListElem  *list;
} ePICPixHashElem;

#define EPIC_HASH_SIZE 256
typedef struct ePICPixHash {
    ePICPixHashElem *bucket[EPIC_HASH_SIZE];
    int              bucket_size[EPIC_HASH_SIZE];
    int              bucket_fill[EPIC_HASH_SIZE];
} ePICPixHash;

typedef struct ePICContext {
    ElsDecCtx        els_ctx;
    int              next_run_pos;
    ElsUnsignedRung  unsigned_rung;
    uint8_t          W_flag_rung;
    uint8_t          N_flag_rung;
    uint8_t          W_ctx_rung[256];
    uint8_t          N_ctx_rung[512];
    uint8_t          nw_pred_rung[256];
    uint8_t          ne_pred_rung[256];
    uint8_t          prev_row_rung[14];
    uint8_t          runlen_zeroes[14];
    uint8_t          runlen_one;
    int              stack_pos;
    uint32_t         stack[EPIC_PIX_STACK_SIZE];
    ePICPixHash      hash;
} ePICContext;

typedef struct JPGContext {
    BlockDSPContext bdsp;
    IDCTDSPContext idsp;
    ScanTable  scantable;

    VLC        dc_vlc[2], ac_vlc[2];
    int        prev_dc[3];
    DECLARE_ALIGNED(32, int16_t, block)[6][64];

    uint8_t    *buf;
} JPGContext;

typedef struct G2MContext {
    ePICContext ec;
    JPGContext jc;

    int        version;

    int        compression;
    int        width, height, bpp;
    int        orig_width, orig_height;
    int        tile_width, tile_height;
    int        tiles_x, tiles_y, tile_x, tile_y;

    int        got_header;

    uint8_t    *framebuf;
    int        framebuf_stride;
    unsigned int framebuf_allocated;

    uint8_t    *synth_tile, *jpeg_tile, *epic_buf, *epic_buf_base;
    int        tile_stride, epic_buf_stride, old_tile_w, old_tile_h;
    int        swapuv;

    uint8_t    *kempf_buf, *kempf_flags;

    uint8_t    *cursor;
    int        cursor_stride;
    int        cursor_fmt;
    int        cursor_w, cursor_h, cursor_x, cursor_y;
    int        cursor_hot_x, cursor_hot_y;
} G2MContext;

static av_cold int jpg_init(AVCodecContext *avctx, JPGContext *c)
{
    int ret;

    ret = ff_mjpeg_build_vlc(&c->dc_vlc[0], ff_mjpeg_bits_dc_luminance,
                             ff_mjpeg_val_dc, 0, avctx);
    if (ret)
        return ret;
    ret = ff_mjpeg_build_vlc(&c->dc_vlc[1], ff_mjpeg_bits_dc_chrominance,
                             ff_mjpeg_val_dc, 0, avctx);
    if (ret)
        return ret;
    ret = ff_mjpeg_build_vlc(&c->ac_vlc[0], ff_mjpeg_bits_ac_luminance,
                             ff_mjpeg_val_ac_luminance, 1, avctx);
    if (ret)
        return ret;
    ret = ff_mjpeg_build_vlc(&c->ac_vlc[1], ff_mjpeg_bits_ac_chrominance,
                             ff_mjpeg_val_ac_chrominance, 1, avctx);
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

    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;

    c->bdsp.clear_block(block);
    dc = get_vlc2(gb, c->dc_vlc[is_chroma].table, 9, 2);
    if (dc < 0)
        return AVERROR_INVALIDDATA;
    if (dc)
        dc = get_xbits(gb, dc);
    dc                = dc * qmat[0] + c->prev_dc[plane];
    block[0]          = dc;
    c->prev_dc[plane] = dc;

    pos = 0;
    while (pos < 63) {
        val = get_vlc2(gb, c->ac_vlc[is_chroma].table, 9, 2);
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

static inline void yuv2rgb(uint8_t *out, int ridx, int Y, int U, int V)
{
    out[ridx]     = av_clip_uint8(Y +              (91881 * V + 32768 >> 16));
    out[1]        = av_clip_uint8(Y + (-22554 * U - 46802 * V + 32768 >> 16));
    out[2 - ridx] = av_clip_uint8(Y + (116130 * U             + 32768 >> 16));
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
    const int ridx = swapuv ? 2 : 0;

    if ((ret = av_reallocp(&c->buf,
                           src_size + AV_INPUT_BUFFER_PADDING_SIZE)) < 0)
        return ret;
    jpg_unescape(src, src_size, c->buf, &unesc_size);
    memset(c->buf + unesc_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
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
                    U = c->block[4][(i >> 1) + (j >> 1) * 8] - 128;
                    V = c->block[5][(i >> 1) + (j >> 1) * 8] - 128;
                    yuv2rgb(out + i * 3, ridx, Y, U, V);
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

#define LOAD_NEIGHBOURS(x)      \
    W   = curr_row[(x)   - 1];  \
    N   = above_row[(x)];       \
    WW  = curr_row[(x)   - 2];  \
    NW  = above_row[(x)  - 1];  \
    NE  = above_row[(x)  + 1];  \
    NN  = above2_row[(x)];      \
    NNW = above2_row[(x) - 1];  \
    NWW = above_row[(x)  - 2];  \
    NNE = above2_row[(x) + 1]

#define UPDATE_NEIGHBOURS(x)    \
    NNW = NN;                   \
    NN  = NNE;                  \
    NWW = NW;                   \
    NW  = N;                    \
    N   = NE;                   \
    NE  = above_row[(x)  + 1];  \
    NNE = above2_row[(x) + 1]

#define R_shift 16
#define G_shift  8
#define B_shift  0

/* improved djb2 hash from http://www.cse.yorku.ca/~oz/hash.html */
static int djb2_hash(uint32_t key)
{
    uint32_t h = 5381;

    h = (h * 33) ^ ((key >> 24) & 0xFF); // xxx: probably not needed at all
    h = (h * 33) ^ ((key >> 16) & 0xFF);
    h = (h * 33) ^ ((key >>  8) & 0xFF);
    h = (h * 33) ^  (key        & 0xFF);

    return h & (EPIC_HASH_SIZE - 1);
}

static void epic_hash_init(ePICPixHash *hash)
{
    memset(hash, 0, sizeof(*hash));
}

static ePICPixHashElem *epic_hash_find(const ePICPixHash *hash, uint32_t key)
{
    int i, idx = djb2_hash(key);
    ePICPixHashElem *bucket = hash->bucket[idx];

    for (i = 0; i < hash->bucket_fill[idx]; i++)
        if (bucket[i].pix_id == key)
            return &bucket[i];

    return NULL;
}

static ePICPixHashElem *epic_hash_add(ePICPixHash *hash, uint32_t key)
{
    ePICPixHashElem *bucket, *ret;
    int idx = djb2_hash(key);

    if (hash->bucket_size[idx] > INT_MAX / sizeof(**hash->bucket))
        return NULL;

    if (!(hash->bucket_fill[idx] < hash->bucket_size[idx])) {
        int new_size = hash->bucket_size[idx] + 16;
        bucket = av_realloc(hash->bucket[idx], new_size * sizeof(*bucket));
        if (!bucket)
            return NULL;
        hash->bucket[idx]      = bucket;
        hash->bucket_size[idx] = new_size;
    }

    ret = &hash->bucket[idx][hash->bucket_fill[idx]++];
    memset(ret, 0, sizeof(*ret));
    ret->pix_id = key;
    return ret;
}

static int epic_add_pixel_to_cache(ePICPixHash *hash, uint32_t key, uint32_t pix)
{
    ePICPixListElem *new_elem;
    ePICPixHashElem *hash_elem = epic_hash_find(hash, key);

    if (!hash_elem) {
        if (!(hash_elem = epic_hash_add(hash, key)))
            return AVERROR(ENOMEM);
    }

    new_elem = av_mallocz(sizeof(*new_elem));
    if (!new_elem)
        return AVERROR(ENOMEM);

    new_elem->pixel = pix;
    new_elem->next  = hash_elem->list;
    hash_elem->list = new_elem;

    return 0;
}

static inline int epic_cache_entries_for_pixel(const ePICPixHash *hash,
                                               uint32_t pix)
{
    ePICPixHashElem *hash_elem = epic_hash_find(hash, pix);

    if (hash_elem != NULL && hash_elem->list != NULL)
        return 1;

    return 0;
}

static void epic_free_pixel_cache(ePICPixHash *hash)
{
    int i, j;

    for (i = 0; i < EPIC_HASH_SIZE; i++) {
        for (j = 0; j < hash->bucket_fill[i]; j++) {
            ePICPixListElem *list_elem = hash->bucket[i][j].list;
            while (list_elem) {
                ePICPixListElem *tmp = list_elem->next;
                av_free(list_elem);
                list_elem = tmp;
            }
        }
        av_freep(&hash->bucket[i]);
        hash->bucket_size[i] =
        hash->bucket_fill[i] = 0;
    }
}

static inline int is_pixel_on_stack(const ePICContext *dc, uint32_t pix)
{
    int i;

    for (i = 0; i < dc->stack_pos; i++)
        if (dc->stack[i] == pix)
            break;

    return i != dc->stack_pos;
}

#define TOSIGNED(val) (((val) >> 1) ^ -((val) & 1))

static inline int epic_decode_component_pred(ePICContext *dc,
                                             int N, int W, int NW)
{
    unsigned delta = ff_els_decode_unsigned(&dc->els_ctx, &dc->unsigned_rung);
    return mid_pred(N, N + W - NW, W) - TOSIGNED(delta);
}

static uint32_t epic_decode_pixel_pred(ePICContext *dc, int x, int y,
                                       const uint32_t *curr_row,
                                       const uint32_t *above_row)
{
    uint32_t N, W, NW, pred;
    unsigned delta;
    int GN, GW, GNW, R, G, B;

    if (x && y) {
        W  = curr_row[x  - 1];
        N  = above_row[x];
        NW = above_row[x - 1];

        GN  = (N  >> G_shift) & 0xFF;
        GW  = (W  >> G_shift) & 0xFF;
        GNW = (NW >> G_shift) & 0xFF;

        G = epic_decode_component_pred(dc, GN, GW, GNW);

        R = G + epic_decode_component_pred(dc,
                                           ((N  >> R_shift) & 0xFF) - GN,
                                           ((W  >> R_shift) & 0xFF) - GW,
                                           ((NW >> R_shift) & 0xFF) - GNW);

        B = G + epic_decode_component_pred(dc,
                                           ((N  >> B_shift) & 0xFF) - GN,
                                           ((W  >> B_shift) & 0xFF) - GW,
                                           ((NW >> B_shift) & 0xFF) - GNW);
    } else {
        if (x)
            pred = curr_row[x - 1];
        else
            pred = above_row[x];

        delta = ff_els_decode_unsigned(&dc->els_ctx, &dc->unsigned_rung);
        R     = ((pred >> R_shift) & 0xFF) - TOSIGNED(delta);

        delta = ff_els_decode_unsigned(&dc->els_ctx, &dc->unsigned_rung);
        G     = ((pred >> G_shift) & 0xFF) - TOSIGNED(delta);

        delta = ff_els_decode_unsigned(&dc->els_ctx, &dc->unsigned_rung);
        B     = ((pred >> B_shift) & 0xFF) - TOSIGNED(delta);
    }

    if (R<0 || G<0 || B<0 || R > 255 || G > 255 || B > 255) {
        avpriv_request_sample(NULL, "RGB %d %d %d (out of range)", R, G, B);
        return 0;
    }

    return (R << R_shift) | (G << G_shift) | (B << B_shift);
}

static int epic_predict_pixel(ePICContext *dc, uint8_t *rung,
                              uint32_t *pPix, uint32_t pix)
{
    if (!ff_els_decode_bit(&dc->els_ctx, rung)) {
        *pPix = pix;
        return 1;
    }
    dc->stack[dc->stack_pos++ & EPIC_PIX_STACK_MAX] = pix;
    return 0;
}

static int epic_handle_edges(ePICContext *dc, int x, int y,
                             const uint32_t *curr_row,
                             const uint32_t *above_row, uint32_t *pPix)
{
    uint32_t pix;

    if (!x && !y) { /* special case: top-left pixel */
        /* the top-left pixel is coded independently with 3 unsigned numbers */
        *pPix = (ff_els_decode_unsigned(&dc->els_ctx, &dc->unsigned_rung) << R_shift) |
                (ff_els_decode_unsigned(&dc->els_ctx, &dc->unsigned_rung) << G_shift) |
                (ff_els_decode_unsigned(&dc->els_ctx, &dc->unsigned_rung) << B_shift);
        return 1;
    }

    if (x) { /* predict from W first */
        pix = curr_row[x - 1];
        if (epic_predict_pixel(dc, &dc->W_flag_rung, pPix, pix))
            return 1;
    }

    if (y) { /* then try to predict from N */
        pix = above_row[x];
        if (!dc->stack_pos || dc->stack[0] != pix) {
            if (epic_predict_pixel(dc, &dc->N_flag_rung, pPix, pix))
                return 1;
        }
    }

    return 0;
}

static int epic_decode_run_length(ePICContext *dc, int x, int y, int tile_width,
                                  const uint32_t *curr_row,
                                  const uint32_t *above_row,
                                  const uint32_t *above2_row,
                                  uint32_t *pPix, int *pRun)
{
    int idx, got_pixel = 0, WWneW, old_WWneW = 0;
    uint32_t W, WW, N, NN, NW, NE, NWW, NNW, NNE;

    *pRun = 0;

    LOAD_NEIGHBOURS(x);

    if (dc->next_run_pos == x) {
        /* can't reuse W for the new pixel in this case */
        WWneW = 1;
    } else {
        idx = (WW  != W)  << 7 |
              (NW  != W)  << 6 |
              (N   != NE) << 5 |
              (NW  != N)  << 4 |
              (NWW != NW) << 3 |
              (NNE != NE) << 2 |
              (NN  != N)  << 1 |
              (NNW != NW);
        WWneW = ff_els_decode_bit(&dc->els_ctx, &dc->W_ctx_rung[idx]);
        if (WWneW < 0)
            return WWneW;
    }

    if (WWneW)
        dc->stack[dc->stack_pos++ & EPIC_PIX_STACK_MAX] = W;
    else {
        *pPix     = W;
        got_pixel = 1;
    }

    do {
        int NWneW = 1;
        if (got_pixel) // pixel value already known (derived from either W or N)
            NWneW = *pPix != N;
        else { // pixel value is unknown and will be decoded later
            NWneW = *pRun ? NWneW : NW != W;

            /* TODO: RFC this mess! */
            switch (((NW != N) << 2) | (NWneW << 1) | WWneW) {
            case 0:
                break; // do nothing here
            case 3:
            case 5:
            case 6:
            case 7:
                if (!is_pixel_on_stack(dc, N)) {
                    idx = WWneW       << 8 |
                          (*pRun ? old_WWneW : WW != W) << 7 |
                          NWneW       << 6 |
                          (N   != NE) << 5 |
                          (NW  != N)  << 4 |
                          (NWW != NW) << 3 |
                          (NNE != NE) << 2 |
                          (NN  != N)  << 1 |
                          (NNW != NW);
                    if (!ff_els_decode_bit(&dc->els_ctx, &dc->N_ctx_rung[idx])) {
                        NWneW = 0;
                        *pPix = N;
                        got_pixel = 1;
                        break;
                    }
                }
                /* fall through */
            default:
                NWneW = 1;
                old_WWneW = WWneW;
                if (!is_pixel_on_stack(dc, N))
                    dc->stack[dc->stack_pos++ & EPIC_PIX_STACK_MAX] = N;
            }
        }

        (*pRun)++;
        if (x + *pRun >= tile_width - 1)
            break;

        UPDATE_NEIGHBOURS(x + *pRun);

        if (!NWneW && NW == N && N == NE) {
            int pos, run, rle;
            int start_pos = x + *pRun;

            /* scan for a run of pix in the line above */
            uint32_t pix = above_row[start_pos + 1];
            for (pos = start_pos + 2; pos < tile_width; pos++)
                if (!(above_row[pos] == pix))
                    break;
            run = pos - start_pos - 1;
            idx = av_ceil_log2(run);
            if (ff_els_decode_bit(&dc->els_ctx, &dc->prev_row_rung[idx]))
                *pRun += run;
            else {
                int flag;
                /* run-length is coded as plain binary number of idx - 1 bits */
                for (pos = idx - 1, rle = 0, flag = 0; pos >= 0; pos--) {
                    if ((1 << pos) + rle < run &&
                        ff_els_decode_bit(&dc->els_ctx,
                                          flag ? &dc->runlen_one
                                               : &dc->runlen_zeroes[pos])) {
                        flag = 1;
                        rle |= 1 << pos;
                    }
                }
                *pRun += rle;
                break; // return immediately
            }
            if (x + *pRun >= tile_width - 1)
                break;

            LOAD_NEIGHBOURS(x + *pRun);
            WWneW = 0;
            NWneW = 0;
        }

        idx = WWneW       << 7 |
              NWneW       << 6 |
              (N   != NE) << 5 |
              (NW  != N)  << 4 |
              (NWW != NW) << 3 |
              (NNE != NE) << 2 |
              (NN  != N)  << 1 |
              (NNW != NW);
        WWneW = ff_els_decode_bit(&dc->els_ctx, &dc->W_ctx_rung[idx]);
    } while (!WWneW);

    dc->next_run_pos = x + *pRun;
    return got_pixel;
}

static int epic_predict_pixel2(ePICContext *dc, uint8_t *rung,
                               uint32_t *pPix, uint32_t pix)
{
    if (ff_els_decode_bit(&dc->els_ctx, rung)) {
        *pPix = pix;
        return 1;
    }
    dc->stack[dc->stack_pos++ & EPIC_PIX_STACK_MAX] = pix;
    return 0;
}

static int epic_predict_from_NW_NE(ePICContext *dc, int x, int y, int run,
                                   int tile_width, const uint32_t *curr_row,
                                   const uint32_t *above_row, uint32_t *pPix)
{
    int pos;

    /* try to reuse the NW pixel first */
    if (x && y) {
        uint32_t NW = above_row[x - 1];
        if (NW != curr_row[x - 1] && NW != above_row[x] && !is_pixel_on_stack(dc, NW)) {
            if (epic_predict_pixel2(dc, &dc->nw_pred_rung[NW & 0xFF], pPix, NW))
                return 1;
        }
    }

    /* try to reuse the NE[x + run, y] pixel */
    pos = x + run - 1;
    if (pos < tile_width - 1 && y) {
        uint32_t NE = above_row[pos + 1];
        if (NE != above_row[pos] && !is_pixel_on_stack(dc, NE)) {
            if (epic_predict_pixel2(dc, &dc->ne_pred_rung[NE & 0xFF], pPix, NE))
                return 1;
        }
    }

    return 0;
}

static int epic_decode_from_cache(ePICContext *dc, uint32_t W, uint32_t *pPix)
{
    ePICPixListElem *list, *prev = NULL;
    ePICPixHashElem *hash_elem = epic_hash_find(&dc->hash, W);

    if (!hash_elem || !hash_elem->list)
        return 0;

    list = hash_elem->list;
    while (list) {
        if (!is_pixel_on_stack(dc, list->pixel)) {
            if (ff_els_decode_bit(&dc->els_ctx, &list->rung)) {
                *pPix = list->pixel;
                if (list != hash_elem->list) {
                    prev->next      = list->next;
                    list->next      = hash_elem->list;
                    hash_elem->list = list;
                }
                return 1;
            }
            dc->stack[dc->stack_pos++ & EPIC_PIX_STACK_MAX] = list->pixel;
        }
        prev = list;
        list = list->next;
    }

    return 0;
}

static int epic_decode_tile(ePICContext *dc, uint8_t *out, int tile_height,
                            int tile_width, int stride)
{
    int x, y;
    uint32_t pix;
    uint32_t *curr_row = NULL, *above_row = NULL, *above2_row;

    for (y = 0; y < tile_height; y++, out += stride) {
        above2_row = above_row;
        above_row  = curr_row;
        curr_row   = (uint32_t *) out;

        for (x = 0, dc->next_run_pos = 0; x < tile_width;) {
            if (dc->els_ctx.err)
                return AVERROR_INVALIDDATA; // bail out in the case of ELS overflow

            pix = curr_row[x - 1]; // get W pixel

            if (y >= 1 && x >= 2 &&
                pix != curr_row[x - 2]  && pix != above_row[x - 1] &&
                pix != above_row[x - 2] && pix != above_row[x] &&
                !epic_cache_entries_for_pixel(&dc->hash, pix)) {
                curr_row[x] = epic_decode_pixel_pred(dc, x, y, curr_row, above_row);
                x++;
            } else {
                int got_pixel, run;
                dc->stack_pos = 0; // empty stack

                if (y < 2 || x < 2 || x == tile_width - 1) {
                    run       = 1;
                    got_pixel = epic_handle_edges(dc, x, y, curr_row, above_row, &pix);
                } else {
                    got_pixel = epic_decode_run_length(dc, x, y, tile_width,
                                                       curr_row, above_row,
                                                       above2_row, &pix, &run);
                    if (got_pixel < 0)
                        return got_pixel;
                }

                if (!got_pixel && !epic_predict_from_NW_NE(dc, x, y, run,
                                                           tile_width, curr_row,
                                                           above_row, &pix)) {
                    uint32_t ref_pix = curr_row[x - 1];
                    if (!x || !epic_decode_from_cache(dc, ref_pix, &pix)) {
                        pix = epic_decode_pixel_pred(dc, x, y, curr_row, above_row);
                        if (is_pixel_on_stack(dc, pix))
                            return AVERROR_INVALIDDATA;

                        if (x) {
                            int ret = epic_add_pixel_to_cache(&dc->hash,
                                                              ref_pix,
                                                              pix);
                            if (ret)
                                return ret;
                        }
                    }
                }
                for (; run > 0; x++, run--)
                    curr_row[x] = pix;
            }
        }
    }

    return 0;
}

static int epic_jb_decode_tile(G2MContext *c, int tile_x, int tile_y,
                               const uint8_t *src, size_t src_size,
                               AVCodecContext *avctx)
{
    uint8_t prefix, mask = 0x80;
    int extrabytes, tile_width, tile_height, awidth, aheight;
    size_t els_dsize;
    uint8_t *dst;

    if (!src_size)
        return 0;

    /* get data size of the ELS partition as unsigned variable-length integer */
    prefix = *src++;
    src_size--;
    for (extrabytes = 0; (prefix & mask) && (extrabytes < 7); extrabytes++)
        mask >>= 1;
    if (extrabytes > 3 || src_size < extrabytes) {
        av_log(avctx, AV_LOG_ERROR, "ePIC: invalid data size VLI\n");
        return AVERROR_INVALIDDATA;
    }

    els_dsize = prefix & ((0x80 >> extrabytes) - 1); // mask out the length prefix
    while (extrabytes-- > 0) {
        els_dsize = (els_dsize << 8) | *src++;
        src_size--;
    }

    if (src_size < els_dsize) {
        av_log(avctx, AV_LOG_ERROR, "ePIC: data too short, needed %"SIZE_SPECIFIER", got %"SIZE_SPECIFIER"\n",
               els_dsize, src_size);
        return AVERROR_INVALIDDATA;
    }

    tile_width  = FFMIN(c->width  - tile_x * c->tile_width,  c->tile_width);
    tile_height = FFMIN(c->height - tile_y * c->tile_height, c->tile_height);
    awidth      = FFALIGN(tile_width,  16);
    aheight     = FFALIGN(tile_height, 16);

    if (tile_width > (1 << FF_ARRAY_ELEMS(c->ec.prev_row_rung))) {
        avpriv_request_sample(avctx, "large tile width");
        return AVERROR_INVALIDDATA;
    }

    if (els_dsize) {
        int ret, i, j, k;
        uint8_t tr_r, tr_g, tr_b, *buf;
        uint32_t *in;
        /* ELS decoder initializations */
        memset(&c->ec, 0, sizeof(c->ec));
        ff_els_decoder_init(&c->ec.els_ctx, src, els_dsize);
        epic_hash_init(&c->ec.hash);

        /* decode transparent pixel value */
        tr_r = ff_els_decode_unsigned(&c->ec.els_ctx, &c->ec.unsigned_rung);
        tr_g = ff_els_decode_unsigned(&c->ec.els_ctx, &c->ec.unsigned_rung);
        tr_b = ff_els_decode_unsigned(&c->ec.els_ctx, &c->ec.unsigned_rung);
        if (c->ec.els_ctx.err != 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "ePIC: couldn't decode transparency pixel!\n");
            ff_els_decoder_uninit(&c->ec.unsigned_rung);
            return AVERROR_INVALIDDATA;
        }

        ret = epic_decode_tile(&c->ec, c->epic_buf, tile_height, tile_width,
                               c->epic_buf_stride);

        epic_free_pixel_cache(&c->ec.hash);
        ff_els_decoder_uninit(&c->ec.unsigned_rung);

        if (ret) {
            av_log(avctx, AV_LOG_ERROR,
                   "ePIC: tile decoding failed, frame=%d, tile_x=%d, tile_y=%d\n",
                   avctx->frame_number, tile_x, tile_y);
            return AVERROR_INVALIDDATA;
        }

        buf = c->epic_buf;
        dst = c->framebuf + tile_x * c->tile_width * 3 +
              tile_y * c->tile_height * c->framebuf_stride;

        for (j = 0; j < tile_height; j++) {
            uint8_t *out = dst;
            in  = (uint32_t *) buf;
            for (i = 0; i < tile_width; i++) {
                out[0] = (in[i] >> R_shift) & 0xFF;
                out[1] = (in[i] >> G_shift) & 0xFF;
                out[2] = (in[i] >> B_shift) & 0xFF;
                out   += 3;
            }
            buf += c->epic_buf_stride;
            dst += c->framebuf_stride;
        }

        if (src_size > els_dsize) {
            uint8_t *jpg;
            uint32_t tr;
            int bstride = FFALIGN(tile_width, 16) >> 3;
            int nblocks = 0;
            int estride = c->epic_buf_stride >> 2;

            src      += els_dsize;
            src_size -= els_dsize;

            in = (uint32_t *) c->epic_buf;
            tr = (tr_r << R_shift) | (tr_g << G_shift) | (tr_b << B_shift);

            memset(c->kempf_flags, 0,
                   (aheight >> 3) * bstride * sizeof(*c->kempf_flags));
            for (j = 0; j < tile_height; j += 8) {
                for (i = 0; i < tile_width; i += 8) {
                    c->kempf_flags[(i >> 3) + (j >> 3) * bstride] = 0;
                    for (k = 0; k < 8 * 8; k++) {
                        if (in[i + (k & 7) + (k >> 3) * estride] == tr) {
                            c->kempf_flags[(i >> 3) + (j >> 3) * bstride] = 1;
                            nblocks++;
                            break;
                        }
                    }
                }
                in += 8 * estride;
            }

            memset(c->jpeg_tile, 0, c->tile_stride * aheight);
            jpg_decode_data(&c->jc, awidth, aheight, src, src_size,
                            c->jpeg_tile, c->tile_stride,
                            c->kempf_flags, bstride, nblocks, c->swapuv);

            in  = (uint32_t *) c->epic_buf;
            dst = c->framebuf + tile_x * c->tile_width * 3 +
                  tile_y * c->tile_height * c->framebuf_stride;
            jpg = c->jpeg_tile;
            for (j = 0; j < tile_height; j++) {
                for (i = 0; i < tile_width; i++)
                    if (in[i] == tr)
                        memcpy(dst + i * 3, jpg + i * 3, 3);
                in  += c->epic_buf_stride >> 2;
                dst += c->framebuf_stride;
                jpg += c->tile_stride;
            }
        }
    } else {
        dst = c->framebuf + tile_x * c->tile_width * 3 +
              tile_y * c->tile_height * c->framebuf_stride;
        return jpg_decode_data(&c->jc, tile_width, tile_height, src, src_size,
                               dst, c->framebuf_stride, NULL, 0, 0, c->swapuv);
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
    int align_width = FFALIGN(width, 16);

    if ((ret = init_get_bits8(&gb, src, len)) < 0)
        return ret;

    if (npal <= 2)       nb = 1;
    else if (npal <= 4)  nb = 2;
    else if (npal <= 16) nb = 4;
    else                 nb = 8;

    for (j = 0; j < height; j++, dst += stride, jpeg_tile = FF_PTR_ADD(jpeg_tile, tile_stride)) {
        if (get_bits(&gb, 8))
            continue;
        for (i = 0; i < width; i++) {
            col = get_bits(&gb, nb);
            if (col != tidx)
                memcpy(dst + i * 3, pal + col * 3, 3);
            else
                memcpy(dst + i * 3, jpeg_tile + i * 3, 3);
        }
        skip_bits_long(&gb, nb * (align_width - width));
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

    c->framebuf_stride = FFALIGN(c->width + 15, 16) * 3;
    aligned_height = c->height + 15;

    av_fast_mallocz(&c->framebuf, &c->framebuf_allocated, c->framebuf_stride * aligned_height);
    if (!c->framebuf)
        return AVERROR(ENOMEM);

    if (!c->synth_tile || !c->jpeg_tile ||
        (c->compression == 2 && !c->epic_buf_base) ||
        c->old_tile_w < c->tile_width ||
        c->old_tile_h < c->tile_height) {
        c->tile_stride     = FFALIGN(c->tile_width, 16) * 3;
        c->epic_buf_stride = FFALIGN(c->tile_width * 4, 16);
        aligned_height     = FFALIGN(c->tile_height,    16);
        av_freep(&c->synth_tile);
        av_freep(&c->jpeg_tile);
        av_freep(&c->kempf_buf);
        av_freep(&c->kempf_flags);
        av_freep(&c->epic_buf_base);
        c->epic_buf    = NULL;
        c->synth_tile  = av_mallocz(c->tile_stride      * aligned_height);
        c->jpeg_tile   = av_mallocz(c->tile_stride      * aligned_height);
        c->kempf_buf   = av_mallocz((c->tile_width + 1) * aligned_height +
                                    AV_INPUT_BUFFER_PADDING_SIZE);
        c->kempf_flags = av_mallocz(c->tile_width       * aligned_height);
        if (!c->synth_tile || !c->jpeg_tile ||
            !c->kempf_buf || !c->kempf_flags)
            return AVERROR(ENOMEM);
        if (c->compression == 2) {
            c->epic_buf_base = av_mallocz(c->epic_buf_stride * aligned_height + 4);
            if (!c->epic_buf_base)
                return AVERROR(ENOMEM);
            c->epic_buf = c->epic_buf_base + 4;
        }
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

    if (y < 0)
        h      +=  y;
    if (w < 0 || h < 0)
        return;
    if (y < 0) {
        cursor += -y * c->cursor_stride;
    } else {
        dst    +=  y * stride;
    }

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

static int g2m_decode_frame(AVCodecContext *avctx, AVFrame *pic,
                            int *got_picture_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    G2MContext *c = avctx->priv_data;
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

    c->swapuv = magic == MKBETAG('G', '2', 'M', '2');

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
            if (c->width < 16 || c->height < 16) {
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
                avpriv_report_missing_feature(avctx, "Compression method %d",
                                              c->compression);
                ret = AVERROR_PATCHWELCOME;
                goto header_fail;
            }
            c->tile_width  = bytestream2_get_be32(&bc);
            c->tile_height = bytestream2_get_be32(&bc);
            if (c->tile_width <= 0 || c->tile_height <= 0 ||
                ((c->tile_width | c->tile_height) & 0xF) ||
                c->tile_width * (uint64_t)c->tile_height >= INT_MAX / 4 ||
                av_image_check_size2(c->tile_width, c->tile_height, avctx->max_pixels, avctx->pix_fmt, 0, avctx) < 0
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
                    avpriv_report_missing_feature(avctx,
                                                  "Bitmasks: R=%"PRIX32", G=%"PRIX32", B=%"PRIX32,
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
                ret = epic_jb_decode_tile(c, c->tile_x, c->tile_y,
                                          buf + bytestream2_tell(&bc),
                                          chunk_size - 2, avctx);
                break;
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
    c->tile_width =
    c->tile_height = 0;
    return ret;
}

static av_cold int g2m_decode_init(AVCodecContext *avctx)
{
    G2MContext *const c = avctx->priv_data;
    int ret;

    if ((ret = jpg_init(avctx, &c->jc)) != 0) {
        av_log(avctx, AV_LOG_ERROR, "Cannot initialise VLCs\n");
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

    av_freep(&c->epic_buf_base);
    c->epic_buf = NULL;
    av_freep(&c->kempf_buf);
    av_freep(&c->kempf_flags);
    av_freep(&c->synth_tile);
    av_freep(&c->jpeg_tile);
    av_freep(&c->cursor);
    av_freep(&c->framebuf);
    c->framebuf_allocated = 0;

    return 0;
}

const FFCodec ff_g2m_decoder = {
    .p.name         = "g2m",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Go2Meeting"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_G2M,
    .priv_data_size = sizeof(G2MContext),
    .init           = g2m_decode_init,
    .close          = g2m_decode_end,
    FF_CODEC_DECODE_CB(g2m_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
