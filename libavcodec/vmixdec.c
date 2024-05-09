/*
 * vMix decoder
 * Copyright (c) 2023 Paul B Mahol
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "codec_internal.h"
#define CACHED_BITSTREAM_READER !ARCH_X86_32
#include "golomb.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "thread.h"

typedef struct SliceContext {
    const uint8_t *dc_ptr;
    const uint8_t *ac_ptr;
    unsigned dc_size;
    unsigned ac_size;
} SliceContext;

typedef struct VMIXContext {
    int nb_slices;
    int lshift;

    int16_t factors[64];
    uint8_t scan[64];

    SliceContext *slices;
    unsigned int slices_size;

    IDCTDSPContext idsp;
} VMIXContext;

static const uint8_t quality[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,64,63,62,61,
   60,59,58,57,56,55,54,53,52,51,
   50,49,48,47,46,45,44,43,42,41,
   40,39,38,37,36,35,34,33,32,31,
   30,29,28,27,26,25,24,23,22,21,
   20,19,18,17,16,15,14,13,12,11,
   10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
};

static const uint8_t quant[64] = {
    16, 16, 19, 22, 22, 26, 26, 27,
    16, 16, 22, 22, 26, 27, 27, 29,
    19, 22, 26, 26, 27, 29, 29, 35,
    22, 24, 27, 27, 29, 32, 34, 38,
    26, 27, 29, 29, 32, 35, 38, 46,
    27, 29, 34, 34, 35, 40, 46, 56,
    29, 34, 34, 37, 40, 48, 56, 69,
    34, 37, 38, 40, 48, 58, 69, 83,
};

static av_cold int decode_init(AVCodecContext *avctx)
{
    VMIXContext *s = avctx->priv_data;

    avctx->bits_per_raw_sample = 8;
    avctx->pix_fmt = AV_PIX_FMT_YUV422P;

    avctx->coded_width = FFALIGN(avctx->width, 16);
    avctx->coded_height = FFALIGN(avctx->height, 16);

    ff_idctdsp_init(&s->idsp, avctx);
    ff_permute_scantable(s->scan, ff_zigzag_direct,
                         s->idsp.idct_permutation);
    return 0;
}

static inline int get_se_golomb_vmix(GetBitContext *gb)
{
    unsigned int buf = get_ue_golomb_long(gb);
    int sign = (buf & 1) - 1;
    return ((buf >> 1) ^ (~sign));
}

static int decode_dcac(AVCodecContext *avctx,
                       GetBitContext *dc_gb, GetBitContext *ac_gb,
                       unsigned *dcrun, unsigned *acrun,
                       AVFrame *frame, int width, int by, int plane)
{
    const ptrdiff_t linesize = frame->linesize[plane];
    uint8_t *dst = frame->data[plane] + by * linesize;
    unsigned dc_run = *dcrun, ac_run = *acrun;
    LOCAL_ALIGNED_32(int16_t, block, [64]);
    VMIXContext *s = avctx->priv_data;
    const int16_t *factors = s->factors;
    const uint8_t *scan = s->scan;
    const int add = plane ? 0 : 1024;
    int i, dc_v = 0, ac_v = 0, dc = 0;
    const int lshift = s->lshift;

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < width; x += 8) {
            memset(block, 0, sizeof(*block)*64);

            if (dc_run > 0) {
                dc_run--;
            } else {
                if (get_bits_left(dc_gb) < 1)
                    return AVERROR_INVALIDDATA;
                dc_v = get_se_golomb_vmix(dc_gb);
                dc += (unsigned)dc_v;
                if (!dc_v)
                    dc_run = get_ue_golomb_long(dc_gb);
            }

            for (int n = 0; n < 64; n++) {
                if (ac_run > 0) {
                    ac_run--;
                    continue;
                }

                if (get_bits_left(ac_gb) < 1)
                    return AVERROR_INVALIDDATA;
                ac_v = get_se_golomb_vmix(ac_gb);
                i = scan[n];
                block[i] = ((unsigned)ac_v * factors[i]) >> 4;
                if (!ac_v)
                    ac_run = get_ue_golomb_long(ac_gb);
            }

            block[0] = ((unsigned)dc << lshift) + (unsigned)add;
            s->idsp.idct_put(dst + x, linesize, block);
        }

        dst += 8 * linesize;
    }

    *dcrun = dc_run;
    *acrun = ac_run;

    return 0;
}

static int decode_slice(AVCodecContext *avctx, AVFrame *frame,
                        const uint8_t *dc_src, unsigned dc_slice_size,
                        const uint8_t *ac_src, unsigned ac_slice_size,
                        int by)
{
    unsigned dc_run = 0, ac_run = 0;
    GetBitContext dc_gb, ac_gb;
    int ret;

    ret = init_get_bits8(&dc_gb, dc_src, dc_slice_size);
    if (ret < 0)
        return ret;

    ret = init_get_bits8(&ac_gb, ac_src, ac_slice_size);
    if (ret < 0)
        return ret;

    for (int p = 0; p < 3; p++) {
        const int rshift = !!p;
        ret = decode_dcac(avctx, &dc_gb, &ac_gb,
                          &dc_run, &ac_run, frame,
                          frame->width >> rshift, by, p);
        if (ret < 0)
            return ret;

        if (get_bits_left(&dc_gb) < 0)
            return AVERROR_INVALIDDATA;
        if (get_bits_left(&ac_gb) < 0)
            return AVERROR_INVALIDDATA;

        align_get_bits(&dc_gb);
        align_get_bits(&ac_gb);
    }

    if (get_bits_left(&dc_gb) > 0)
        return AVERROR_INVALIDDATA;
    if (get_bits_left(&ac_gb) > 0)
        return AVERROR_INVALIDDATA;

    return 0;
}

static int decode_slices(AVCodecContext *avctx, void *arg,
                         int n, int thread_nb)
{
    VMIXContext *s = avctx->priv_data;
    const uint8_t *dc_slice_ptr = s->slices[n].dc_ptr;
    const uint8_t *ac_slice_ptr = s->slices[n].ac_ptr;
    unsigned dc_slice_size = s->slices[n].dc_size;
    unsigned ac_slice_size = s->slices[n].ac_size;
    AVFrame *frame = arg;

    return decode_slice(avctx, frame, dc_slice_ptr, dc_slice_size,
                        ac_slice_ptr, ac_slice_size, n * 16);
}

static int decode_frame(AVCodecContext *avctx,
                        AVFrame *frame, int *got_frame,
                        AVPacket *avpkt)
{
    VMIXContext *s = avctx->priv_data;
    unsigned offset, q;
    int ret;

    if (avpkt->size <= 7)
        return AVERROR_INVALIDDATA;

    s->lshift = 0;
    offset = 2 + avpkt->data[0];
    if (offset == 5)
        s->lshift = avpkt->data[1];
    else if (offset != 3)
        return AVERROR_INVALIDDATA;

    if (s->lshift > 31)
        return AVERROR_INVALIDDATA;

    q = quality[FFMIN(avpkt->data[offset - 2], FF_ARRAY_ELEMS(quality)-1)];
    for (int n = 0; n < 64; n++)
        s->factors[n] = quant[n] * q;

    s->nb_slices = (avctx->height + 15) / 16;
    av_fast_mallocz(&s->slices, &s->slices_size, s->nb_slices * sizeof(*s->slices));
    if (!s->slices)
        return AVERROR(ENOMEM);

    for (int n = 0; n < s->nb_slices; n++) {
        unsigned slice_size;

        if (offset + 4 > avpkt->size)
            return AVERROR_INVALIDDATA;

        slice_size = AV_RL32(avpkt->data + offset);
        if (slice_size > avpkt->size)
            return AVERROR_INVALIDDATA;

        if (avpkt->size - slice_size - 4LL < offset)
            return AVERROR_INVALIDDATA;

        s->slices[n].dc_size = slice_size;
        s->slices[n].dc_ptr = avpkt->data + offset + 4;
        offset += slice_size + 4;
    }

    for (int n = 0; n < s->nb_slices; n++) {
        unsigned slice_size;

        if (offset + 4 > avpkt->size)
            return AVERROR_INVALIDDATA;

        slice_size = AV_RL32(avpkt->data + offset);
        if (slice_size > avpkt->size)
            return AVERROR_INVALIDDATA;

        if (avpkt->size - slice_size - 4LL < offset)
            return AVERROR_INVALIDDATA;

        s->slices[n].ac_size = slice_size;
        s->slices[n].ac_ptr = avpkt->data + offset + 4;
        offset += slice_size + 4;
    }

    ret = ff_thread_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    avctx->execute2(avctx, decode_slices, frame, NULL, s->nb_slices);

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    VMIXContext *s = avctx->priv_data;
    av_freep(&s->slices);
    return 0;
}

const FFCodec ff_vmix_decoder = {
    .p.name           = "vmix",
    CODEC_LONG_NAME("vMix Video"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_VMIX,
    .priv_data_size   = sizeof(VMIXContext),
    .init             = decode_init,
    .close            = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                        AV_CODEC_CAP_SLICE_THREADS,
};
