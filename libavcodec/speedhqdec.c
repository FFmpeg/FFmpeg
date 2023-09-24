/*
 * NewTek SpeedHQ codec
 * Copyright 2017 Steinar H. Gunderson
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
 * NewTek SpeedHQ decoder.
 */

#define BITSTREAM_READER_LE

#include "libavutil/attributes.h"
#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "libavutil/thread.h"
#include "mathops.h"
#include "mpeg12data.h"
#include "mpeg12vlc.h"
#include "speedhq.h"

#define MAX_INDEX (64 - 1)

/*
 * 5 bits makes for very small tables, with no more than two lookups needed
 * for the longest (10-bit) codes.
 */
#define ALPHA_VLC_BITS 5

typedef struct SHQContext {
    BlockDSPContext bdsp;
    IDCTDSPContext idsp;
    uint8_t permutated_intra_scantable[64];
    int quant_matrix[64];
    enum { SHQ_SUBSAMPLING_420, SHQ_SUBSAMPLING_422, SHQ_SUBSAMPLING_444 }
        subsampling;
    enum { SHQ_NO_ALPHA, SHQ_RLE_ALPHA, SHQ_DCT_ALPHA } alpha_type;
} SHQContext;

/* NOTE: The first element is always 16, unscaled. */
static const uint8_t unscaled_quant_matrix[64] = {
    16, 16, 19, 22, 26, 27, 29, 34,
    16, 16, 22, 24, 27, 29, 34, 37,
    19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40,
    22, 26, 27, 29, 32, 35, 40, 48,
    26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69,
    27, 29, 35, 38, 46, 56, 69, 83
};

static VLCElem dc_lum_vlc_le[512];
static VLCElem dc_chroma_vlc_le[514];
static VLCElem dc_alpha_run_vlc_le[160];
static VLCElem dc_alpha_level_vlc_le[288];

static RL_VLC_ELEM speedhq_rl_vlc[674];

static inline int decode_dc_le(GetBitContext *gb, int component)
{
    int code, diff;

    if (component == 0 || component == 3) {
        code = get_vlc2(gb, dc_lum_vlc_le, DC_VLC_BITS, 2);
    } else {
        code = get_vlc2(gb, dc_chroma_vlc_le, DC_VLC_BITS, 2);
    }
    if (!code) {
        diff = 0;
    } else {
        diff = get_xbits_le(gb, code);
    }
    return diff;
}

static inline int decode_alpha_block(const SHQContext *s, GetBitContext *gb, uint8_t last_alpha[16], uint8_t *dest, int linesize)
{
    uint8_t block[128];
    int i = 0, x, y;

    memset(block, 0, sizeof(block));

    {
        OPEN_READER(re, gb);

        for ( ;; ) {
            int run, level;

            UPDATE_CACHE_LE(re, gb);
            GET_VLC(run, re, gb, dc_alpha_run_vlc_le, ALPHA_VLC_BITS, 2);

            if (run < 0) break;
            i += run;
            if (i >= 128)
                return AVERROR_INVALIDDATA;

            UPDATE_CACHE_LE(re, gb);
            GET_VLC(level, re, gb, dc_alpha_level_vlc_le, ALPHA_VLC_BITS, 2);
            block[i++] = level;
        }

        CLOSE_READER(re, gb);
    }

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 16; x++) {
            last_alpha[x] -= block[y * 16 + x];
        }
        memcpy(dest, last_alpha, 16);
        dest += linesize;
    }

    return 0;
}

static inline int decode_dct_block(const SHQContext *s, GetBitContext *gb, int last_dc[4], int component, uint8_t *dest, int linesize)
{
    const int *quant_matrix = s->quant_matrix;
    const uint8_t *scantable = s->permutated_intra_scantable;
    LOCAL_ALIGNED_32(int16_t, block, [64]);
    int dc_offset;

    s->bdsp.clear_block(block);

    dc_offset = decode_dc_le(gb, component);
    last_dc[component] -= dc_offset;  /* Note: Opposite of most codecs. */
    block[scantable[0]] = last_dc[component];  /* quant_matrix[0] is always 16. */

    /* Read AC coefficients. */
    {
        int i = 0;
        OPEN_READER(re, gb);
        for ( ;; ) {
            int level, run;
            UPDATE_CACHE_LE(re, gb);
            GET_RL_VLC(level, run, re, gb, speedhq_rl_vlc,
                       TEX_VLC_BITS, 2, 0);
            if (level == 127) {
                break;
            } else if (level) {
                i += run;
                if (i > MAX_INDEX)
                    return AVERROR_INVALIDDATA;
                /* If next bit is 1, level = -level */
                level = (level ^ SHOW_SBITS(re, gb, 1)) -
                        SHOW_SBITS(re, gb, 1);
                LAST_SKIP_BITS(re, gb, 1);
            } else {
                /* Escape. */
#if MIN_CACHE_BITS < 6 + 6 + 12
#error MIN_CACHE_BITS is too small for the escape code, add UPDATE_CACHE
#endif
                run = SHOW_UBITS(re, gb, 6) + 1;
                SKIP_BITS(re, gb, 6);
                level = SHOW_UBITS(re, gb, 12) - 2048;
                LAST_SKIP_BITS(re, gb, 12);

                i += run;
                if (i > MAX_INDEX)
                    return AVERROR_INVALIDDATA;
            }

            block[scantable[i]] = (level * quant_matrix[i]) >> 4;
        }
        CLOSE_READER(re, gb);
    }

    s->idsp.idct_put(dest, linesize, block);

    return 0;
}

static int decode_speedhq_border(const SHQContext *s, GetBitContext *gb, AVFrame *frame, int field_number, int line_stride)
{
    int linesize_y  = frame->linesize[0] * line_stride;
    int linesize_cb = frame->linesize[1] * line_stride;
    int linesize_cr = frame->linesize[2] * line_stride;
    int linesize_a;
    int ret;

    if (s->alpha_type != SHQ_NO_ALPHA)
        linesize_a = frame->linesize[3] * line_stride;

    for (int y = 0; y < frame->height; y += 16 * line_stride) {
        int last_dc[4] = { 1024, 1024, 1024, 1024 };
        uint8_t *dest_y, *dest_cb, *dest_cr, *dest_a;
        uint8_t last_alpha[16];
        int x = frame->width - 8;

        dest_y = frame->data[0] + frame->linesize[0] * (y + field_number) + x;
        if (s->subsampling == SHQ_SUBSAMPLING_420) {
            dest_cb = frame->data[1] + frame->linesize[1] * (y/2 + field_number) + x / 2;
            dest_cr = frame->data[2] + frame->linesize[2] * (y/2 + field_number) + x / 2;
        } else {
            av_assert2(s->subsampling == SHQ_SUBSAMPLING_422);
            dest_cb = frame->data[1] + frame->linesize[1] * (y + field_number) + x / 2;
            dest_cr = frame->data[2] + frame->linesize[2] * (y + field_number) + x / 2;
        }
        if (s->alpha_type != SHQ_NO_ALPHA) {
            memset(last_alpha, 255, sizeof(last_alpha));
            dest_a = frame->data[3] + frame->linesize[3] * (y + field_number) + x;
        }

        if ((ret = decode_dct_block(s, gb, last_dc, 0, dest_y, linesize_y)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 0, dest_y + 8, linesize_y)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 0, dest_y + 8 * linesize_y, linesize_y)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 0, dest_y + 8 * linesize_y + 8, linesize_y)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 1, dest_cb, linesize_cb)) < 0)
            return ret;
        if ((ret = decode_dct_block(s, gb, last_dc, 2, dest_cr, linesize_cr)) < 0)
            return ret;

        if (s->subsampling != SHQ_SUBSAMPLING_420) {
            if ((ret = decode_dct_block(s, gb, last_dc, 1, dest_cb + 8 * linesize_cb, linesize_cb)) < 0)
                return ret;
            if ((ret = decode_dct_block(s, gb, last_dc, 2, dest_cr + 8 * linesize_cr, linesize_cr)) < 0)
                return ret;
        }

        if (s->alpha_type == SHQ_RLE_ALPHA) {
            /* Alpha coded using 16x8 RLE blocks. */
            if ((ret = decode_alpha_block(s, gb, last_alpha, dest_a, linesize_a)) < 0)
                return ret;
            if ((ret = decode_alpha_block(s, gb, last_alpha, dest_a + 8 * linesize_a, linesize_a)) < 0)
                return ret;
        } else if (s->alpha_type == SHQ_DCT_ALPHA) {
            /* Alpha encoded exactly like luma. */
            if ((ret = decode_dct_block(s, gb, last_dc, 3, dest_a, linesize_a)) < 0)
                return ret;
            if ((ret = decode_dct_block(s, gb, last_dc, 3, dest_a + 8, linesize_a)) < 0)
                return ret;
            if ((ret = decode_dct_block(s, gb, last_dc, 3, dest_a + 8 * linesize_a, linesize_a)) < 0)
                return ret;
            if ((ret = decode_dct_block(s, gb, last_dc, 3, dest_a + 8 * linesize_a + 8, linesize_a)) < 0)
                return ret;
        }
    }

    return 0;
}

static int decode_speedhq_field(const SHQContext *s, const uint8_t *buf, int buf_size, AVFrame *frame, int field_number, int start, int end, int line_stride)
{
    int ret, slice_number, slice_offsets[5];
    int linesize_y  = frame->linesize[0] * line_stride;
    int linesize_cb = frame->linesize[1] * line_stride;
    int linesize_cr = frame->linesize[2] * line_stride;
    int linesize_a;
    GetBitContext gb;

    if (s->alpha_type != SHQ_NO_ALPHA)
        linesize_a = frame->linesize[3] * line_stride;

    if (end < start || end - start < 3 || end > buf_size)
        return AVERROR_INVALIDDATA;

    slice_offsets[0] = start;
    slice_offsets[4] = end;
    for (slice_number = 1; slice_number < 4; slice_number++) {
        uint32_t last_offset, slice_len;

        last_offset = slice_offsets[slice_number - 1];
        slice_len = AV_RL24(buf + last_offset);
        slice_offsets[slice_number] = last_offset + slice_len;

        if (slice_len < 3 || slice_offsets[slice_number] > end - 3)
            return AVERROR_INVALIDDATA;
    }

    for (slice_number = 0; slice_number < 4; slice_number++) {
        uint32_t slice_begin, slice_end;
        int x, y;

        slice_begin = slice_offsets[slice_number];
        slice_end = slice_offsets[slice_number + 1];

        if ((ret = init_get_bits8(&gb, buf + slice_begin + 3, slice_end - slice_begin - 3)) < 0)
            return ret;

        for (y = slice_number * 16 * line_stride; y < frame->height; y += line_stride * 64) {
            uint8_t *dest_y, *dest_cb, *dest_cr, *dest_a;
            int last_dc[4] = { 1024, 1024, 1024, 1024 };
            uint8_t last_alpha[16];

            memset(last_alpha, 255, sizeof(last_alpha));

            dest_y = frame->data[0] + frame->linesize[0] * (y + field_number);
            if (s->subsampling == SHQ_SUBSAMPLING_420) {
                dest_cb = frame->data[1] + frame->linesize[1] * (y/2 + field_number);
                dest_cr = frame->data[2] + frame->linesize[2] * (y/2 + field_number);
            } else {
                dest_cb = frame->data[1] + frame->linesize[1] * (y + field_number);
                dest_cr = frame->data[2] + frame->linesize[2] * (y + field_number);
            }
            if (s->alpha_type != SHQ_NO_ALPHA) {
                dest_a = frame->data[3] + frame->linesize[3] * (y + field_number);
            }

            for (x = 0; x < frame->width - 8 * (s->subsampling != SHQ_SUBSAMPLING_444); x += 16) {
                /* Decode the four luma blocks. */
                if ((ret = decode_dct_block(s, &gb, last_dc, 0, dest_y, linesize_y)) < 0)
                    return ret;
                if ((ret = decode_dct_block(s, &gb, last_dc, 0, dest_y + 8, linesize_y)) < 0)
                    return ret;
                if ((ret = decode_dct_block(s, &gb, last_dc, 0, dest_y + 8 * linesize_y, linesize_y)) < 0)
                    return ret;
                if ((ret = decode_dct_block(s, &gb, last_dc, 0, dest_y + 8 * linesize_y + 8, linesize_y)) < 0)
                    return ret;

                /*
                 * Decode the first chroma block. For 4:2:0, this is the only one;
                 * for 4:2:2, it's the top block; for 4:4:4, it's the top-left block.
                 */
                if ((ret = decode_dct_block(s, &gb, last_dc, 1, dest_cb, linesize_cb)) < 0)
                    return ret;
                if ((ret = decode_dct_block(s, &gb, last_dc, 2, dest_cr, linesize_cr)) < 0)
                    return ret;

                if (s->subsampling != SHQ_SUBSAMPLING_420) {
                    /* For 4:2:2, this is the bottom block; for 4:4:4, it's the bottom-left block. */
                    if ((ret = decode_dct_block(s, &gb, last_dc, 1, dest_cb + 8 * linesize_cb, linesize_cb)) < 0)
                        return ret;
                    if ((ret = decode_dct_block(s, &gb, last_dc, 2, dest_cr + 8 * linesize_cr, linesize_cr)) < 0)
                        return ret;

                    if (s->subsampling == SHQ_SUBSAMPLING_444) {
                        /* Top-right and bottom-right blocks. */
                        if ((ret = decode_dct_block(s, &gb, last_dc, 1, dest_cb + 8, linesize_cb)) < 0)
                            return ret;
                        if ((ret = decode_dct_block(s, &gb, last_dc, 2, dest_cr + 8, linesize_cr)) < 0)
                            return ret;
                        if ((ret = decode_dct_block(s, &gb, last_dc, 1, dest_cb + 8 * linesize_cb + 8, linesize_cb)) < 0)
                            return ret;
                        if ((ret = decode_dct_block(s, &gb, last_dc, 2, dest_cr + 8 * linesize_cr + 8, linesize_cr)) < 0)
                            return ret;

                        dest_cb += 8;
                        dest_cr += 8;
                    }
                }
                dest_y += 16;
                dest_cb += 8;
                dest_cr += 8;

                if (s->alpha_type == SHQ_RLE_ALPHA) {
                    /* Alpha coded using 16x8 RLE blocks. */
                    if ((ret = decode_alpha_block(s, &gb, last_alpha, dest_a, linesize_a)) < 0)
                        return ret;
                    if ((ret = decode_alpha_block(s, &gb, last_alpha, dest_a + 8 * linesize_a, linesize_a)) < 0)
                        return ret;
                    dest_a += 16;
                } else if (s->alpha_type == SHQ_DCT_ALPHA) {
                    /* Alpha encoded exactly like luma. */
                    if ((ret = decode_dct_block(s, &gb, last_dc, 3, dest_a, linesize_a)) < 0)
                        return ret;
                    if ((ret = decode_dct_block(s, &gb, last_dc, 3, dest_a + 8, linesize_a)) < 0)
                        return ret;
                    if ((ret = decode_dct_block(s, &gb, last_dc, 3, dest_a + 8 * linesize_a, linesize_a)) < 0)
                        return ret;
                    if ((ret = decode_dct_block(s, &gb, last_dc, 3, dest_a + 8 * linesize_a + 8, linesize_a)) < 0)
                        return ret;
                    dest_a += 16;
                }
            }
        }
    }

    if (s->subsampling != SHQ_SUBSAMPLING_444 && (frame->width & 15))
        return decode_speedhq_border(s, &gb, frame, field_number, line_stride);

    return 0;
}

static void compute_quant_matrix(int *output, int qscale)
{
    int i;
    for (i = 0; i < 64; i++) output[i] = unscaled_quant_matrix[ff_zigzag_direct[i]] * qscale;
}

static int speedhq_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                int *got_frame, AVPacket *avpkt)
{
    SHQContext * const s = avctx->priv_data;
    const uint8_t *buf   = avpkt->data;
    int buf_size         = avpkt->size;
    uint8_t quality;
    uint32_t second_field_offset;
    int ret;

    if (buf_size < 4 || avctx->width < 8 || avctx->width % 8 != 0)
        return AVERROR_INVALIDDATA;
    if (buf_size < avctx->width*avctx->height / 64 / 4)
        return AVERROR_INVALIDDATA;

    quality = buf[0];
    if (quality >= 100) {
        return AVERROR_INVALIDDATA;
    }

    compute_quant_matrix(s->quant_matrix, 100 - quality);

    second_field_offset = AV_RL24(buf + 1);
    if (second_field_offset >= buf_size - 3) {
        return AVERROR_INVALIDDATA;
    }

    avctx->coded_width = FFALIGN(avctx->width, 16);
    avctx->coded_height = FFALIGN(avctx->height, 16);

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        return ret;
    }
    frame->flags |= AV_FRAME_FLAG_KEY;

    if (second_field_offset == 4 || second_field_offset == (buf_size-4)) {
        /*
         * Overlapping first and second fields is used to signal
         * encoding only a single field. In this case, "height"
         * is ambiguous; it could mean either the height of the
         * frame as a whole, or of the field. The former would make
         * more sense for compatibility with legacy decoders,
         * but this matches the convention used in NDI, which is
         * the primary user of this trick.
         */
        if ((ret = decode_speedhq_field(s, buf, buf_size, frame, 0, 4, buf_size, 1)) < 0)
            return ret;
    } else {
        if ((ret = decode_speedhq_field(s, buf, buf_size, frame, 0, 4, second_field_offset, 2)) < 0)
            return ret;
        if ((ret = decode_speedhq_field(s, buf, buf_size, frame, 1, second_field_offset, buf_size, 2)) < 0)
            return ret;
    }

    *got_frame = 1;
    return buf_size;
}

/*
 * Alpha VLC. Run and level are independently coded, and would be
 * outside the default limits for MAX_RUN/MAX_LEVEL, so we don't
 * bother with combining them into one table.
 */
static av_cold void compute_alpha_vlcs(void)
{
    uint16_t run_code[134], level_code[266];
    uint8_t run_bits[134], level_bits[266];
    int16_t run_symbols[134], level_symbols[266];
    int entry, i, sign;

    /* Initialize VLC for alpha run. */
    entry = 0;

    /* 0 -> 0. */
    run_code[entry] = 0;
    run_bits[entry] = 1;
    run_symbols[entry] = 0;
    ++entry;

    /* 10xx -> xx plus 1. */
    for (i = 0; i < 4; ++i) {
        run_code[entry] = (i << 2) | 1;
        run_bits[entry] = 4;
        run_symbols[entry] = i + 1;
        ++entry;
    }

    /* 111xxxxxxx -> xxxxxxx. */
    for (i = 0; i < 128; ++i) {
        run_code[entry] = (i << 3) | 7;
        run_bits[entry] = 10;
        run_symbols[entry] = i;
        ++entry;
    }

    /* 110 -> EOB. */
    run_code[entry] = 3;
    run_bits[entry] = 3;
    run_symbols[entry] = -1;
    ++entry;

    av_assert0(entry == FF_ARRAY_ELEMS(run_code));

    VLC_INIT_STATIC_SPARSE_TABLE(dc_alpha_run_vlc_le, ALPHA_VLC_BITS,
                                 FF_ARRAY_ELEMS(run_code),
                                 run_bits, 1, 1,
                                 run_code, 2, 2,
                                 run_symbols, 2, 2, VLC_INIT_LE);

    /* Initialize VLC for alpha level. */
    entry = 0;

    for (sign = 0; sign <= 1; ++sign) {
        /* 1s -> -1 or +1 (depending on sign bit). */
        level_code[entry] = (sign << 1) | 1;
        level_bits[entry] = 2;
        level_symbols[entry] = sign ? -1 : 1;
        ++entry;

        /* 01sxx -> xx plus 2 (2..5 or -2..-5, depending on sign bit). */
        for (i = 0; i < 4; ++i) {
            level_code[entry] = (i << 3) | (sign << 2) | 2;
            level_bits[entry] = 5;
            level_symbols[entry] = sign ? -(i + 2) : (i + 2);
            ++entry;
        }
    }

    /*
     * 00xxxxxxxx -> xxxxxxxx, in two's complement. There are many codes
     * here that would better be encoded in other ways (e.g. 0 would be
     * encoded by increasing run, and +/- 1 would be encoded with a
     * shorter code), but it doesn't hurt to allow everything.
     */
    for (i = 0; i < 256; ++i) {
        level_code[entry] = i << 2;
        level_bits[entry] = 10;
        level_symbols[entry] = i;
        ++entry;
    }

    av_assert0(entry == FF_ARRAY_ELEMS(level_code));

    VLC_INIT_STATIC_SPARSE_TABLE(dc_alpha_level_vlc_le, ALPHA_VLC_BITS,
                                 FF_ARRAY_ELEMS(level_code),
                                 level_bits, 1, 1,
                                 level_code, 2, 2,
                                 level_symbols, 2, 2, VLC_INIT_LE);
}

static av_cold void speedhq_static_init(void)
{
    /* Exactly the same as MPEG-2, except for a little-endian reader. */
    VLC_INIT_STATIC_TABLE(dc_lum_vlc_le, DC_VLC_BITS, 12,
                          ff_mpeg12_vlc_dc_lum_bits, 1, 1,
                          ff_mpeg12_vlc_dc_lum_code, 2, 2,
                          VLC_INIT_OUTPUT_LE);
    VLC_INIT_STATIC_TABLE(dc_chroma_vlc_le, DC_VLC_BITS, 12,
                          ff_mpeg12_vlc_dc_chroma_bits, 1, 1,
                          ff_mpeg12_vlc_dc_chroma_code, 2, 2,
                          VLC_INIT_OUTPUT_LE);

    ff_init_2d_vlc_rl(ff_speedhq_vlc_table, speedhq_rl_vlc, ff_speedhq_run,
                      ff_speedhq_level, SPEEDHQ_RL_NB_ELEMS,
                      FF_ARRAY_ELEMS(speedhq_rl_vlc), VLC_INIT_LE);

    compute_alpha_vlcs();
}

static av_cold int speedhq_decode_init(AVCodecContext *avctx)
{
    int ret;
    static AVOnce init_once = AV_ONCE_INIT;
    SHQContext * const s = avctx->priv_data;

    ret = ff_thread_once(&init_once, speedhq_static_init);
    if (ret)
        return AVERROR_UNKNOWN;

    ff_blockdsp_init(&s->bdsp);
    ff_idctdsp_init(&s->idsp, avctx);
    ff_permute_scantable(s->permutated_intra_scantable, ff_zigzag_direct,
                         s->idsp.idct_permutation);

    switch (avctx->codec_tag) {
    case MKTAG('S', 'H', 'Q', '0'):
        s->subsampling = SHQ_SUBSAMPLING_420;
        s->alpha_type = SHQ_NO_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        break;
    case MKTAG('S', 'H', 'Q', '1'):
        s->subsampling = SHQ_SUBSAMPLING_420;
        s->alpha_type = SHQ_RLE_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA420P;
        break;
    case MKTAG('S', 'H', 'Q', '2'):
        s->subsampling = SHQ_SUBSAMPLING_422;
        s->alpha_type = SHQ_NO_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        break;
    case MKTAG('S', 'H', 'Q', '3'):
        s->subsampling = SHQ_SUBSAMPLING_422;
        s->alpha_type = SHQ_RLE_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P;
        break;
    case MKTAG('S', 'H', 'Q', '4'):
        s->subsampling = SHQ_SUBSAMPLING_444;
        s->alpha_type = SHQ_NO_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        break;
    case MKTAG('S', 'H', 'Q', '5'):
        s->subsampling = SHQ_SUBSAMPLING_444;
        s->alpha_type = SHQ_RLE_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        break;
    case MKTAG('S', 'H', 'Q', '7'):
        s->subsampling = SHQ_SUBSAMPLING_422;
        s->alpha_type = SHQ_DCT_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P;
        break;
    case MKTAG('S', 'H', 'Q', '9'):
        s->subsampling = SHQ_SUBSAMPLING_444;
        s->alpha_type = SHQ_DCT_ALPHA;
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown NewTek SpeedHQ FOURCC provided (%08X)\n",
               avctx->codec_tag);
        return AVERROR_INVALIDDATA;
    }

    /* This matches what NDI's RGB -> Y'CbCr 4:2:2 converter uses. */
    avctx->colorspace = AVCOL_SPC_BT470BG;
    avctx->chroma_sample_location = AVCHROMA_LOC_CENTER;

    return 0;
}

const FFCodec ff_speedhq_decoder = {
    .p.name         = "speedhq",
    CODEC_LONG_NAME("NewTek SpeedHQ"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_SPEEDHQ,
    .priv_data_size = sizeof(SHQContext),
    .init           = speedhq_decode_init,
    FF_CODEC_DECODE_CB(speedhq_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
