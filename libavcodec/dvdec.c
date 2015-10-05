/*
 * DV decoder
 * Copyright (c) 2002 Fabrice Bellard
 * Copyright (c) 2004 Roman Shaposhnik
 *
 * 50 Mbps (DVCPRO50) support
 * Copyright (c) 2006 Daniel Maas <dmaas@maasdigital.com>
 *
 * 100 Mbps (DVCPRO HD) support
 * Initial code by Daniel Maas <dmaas@maasdigital.com> (funded by BBC R&D)
 * Final code by Roman Shaposhnik
 *
 * Many thanks to Dan Dennedy <dan@dennedy.org> for providing wealth
 * of DV technical info.
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
 * DV decoder
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "dv.h"
#include "dv_profile_internal.h"
#include "dvdata.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "internal.h"
#include "put_bits.h"
#include "simple_idct.h"

typedef struct BlockInfo {
    const uint32_t *factor_table;
    const uint8_t *scan_table;
    uint8_t pos; /* position in block */
    void (*idct_put)(uint8_t *dest, int line_size, int16_t *block);
    uint8_t partial_bit_count;
    uint32_t partial_bit_buffer;
    int shift_offset;
} BlockInfo;

static const int dv_iweight_bits = 14;

static const uint16_t dv_iweight_88[64] = {
    32768, 16705, 16705, 17734, 17032, 17734, 18205, 18081,
    18081, 18205, 18725, 18562, 19195, 18562, 18725, 19266,
    19091, 19705, 19705, 19091, 19266, 21407, 19643, 20267,
    20228, 20267, 19643, 21407, 22725, 21826, 20853, 20806,
    20806, 20853, 21826, 22725, 23170, 23170, 21407, 21400,
    21407, 23170, 23170, 24598, 23786, 22018, 22018, 23786,
    24598, 25251, 24465, 22654, 24465, 25251, 25972, 25172,
    25172, 25972, 26722, 27969, 26722, 29692, 29692, 31521,
};
static const uint16_t dv_iweight_248[64] = {
    32768, 16384, 16705, 16705, 17734, 17734, 17734, 17734,
    18081, 18081, 18725, 18725, 21407, 21407, 19091, 19091,
    19195, 19195, 18205, 18205, 18725, 18725, 19705, 19705,
    20267, 20267, 21826, 21826, 23170, 23170, 20806, 20806,
    20267, 20267, 19266, 19266, 21407, 21407, 20853, 20853,
    21400, 21400, 23786, 23786, 24465, 24465, 22018, 22018,
    23170, 23170, 22725, 22725, 24598, 24598, 24465, 24465,
    25172, 25172, 27969, 27969, 25972, 25972, 29692, 29692
};

/**
 * The "inverse" DV100 weights are actually just the spec weights (zig-zagged).
 */
static const uint16_t dv_iweight_1080_y[64] = {
    128,  16,  16,  17,  17,  17,  18,  18,
     18,  18,  18,  18,  19,  18,  18,  19,
     19,  19,  19,  19,  19,  42,  38,  40,
     40,  40,  38,  42,  44,  43,  41,  41,
     41,  41,  43,  44,  45,  45,  42,  42,
     42,  45,  45,  48,  46,  43,  43,  46,
     48,  49,  48,  44,  48,  49, 101,  98,
     98, 101, 104, 109, 104, 116, 116, 123,
};
static const uint16_t dv_iweight_1080_c[64] = {
    128,  16,  16,  17,  17,  17,  25,  25,
     25,  25,  26,  25,  26,  25,  26,  26,
     26,  27,  27,  26,  26,  42,  38,  40,
     40,  40,  38,  42,  44,  43,  41,  41,
     41,  41,  43,  44,  91,  91,  84,  84,
     84,  91,  91,  96,  93,  86,  86,  93,
     96, 197, 191, 177, 191, 197, 203, 197,
    197, 203, 209, 219, 209, 232, 232, 246,
};
static const uint16_t dv_iweight_720_y[64] = {
    128,  16,  16,  17,  17,  17,  18,  18,
     18,  18,  18,  18,  19,  18,  18,  19,
     19,  19,  19,  19,  19,  42,  38,  40,
     40,  40,  38,  42,  44,  43,  41,  41,
     41,  41,  43,  44,  68,  68,  63,  63,
     63,  68,  68,  96,  92,  86,  86,  92,
     96,  98,  96,  88,  96,  98, 202, 196,
    196, 202, 208, 218, 208, 232, 232, 246,
};
static const uint16_t dv_iweight_720_c[64] = {
    128,  24,  24,  26,  26,  26,  36,  36,
     36,  36,  36,  36,  38,  36,  36,  38,
     38,  38,  38,  38,  38,  84,  76,  80,
     80,  80,  76,  84,  88,  86,  82,  82,
     82,  82,  86,  88, 182, 182, 168, 168,
    168, 182, 182, 192, 186, 192, 172, 186,
    192, 394, 382, 354, 382, 394, 406, 394,
    394, 406, 418, 438, 418, 464, 464, 492,
};

static void dv_init_weight_tables(DVVideoContext *ctx, const AVDVProfile *d)
{
    int j, i, c, s;
    uint32_t *factor1 = &ctx->idct_factor[0],
             *factor2 = &ctx->idct_factor[DV_PROFILE_IS_HD(d) ? 4096 : 2816];

    if (DV_PROFILE_IS_HD(d)) {
        /* quantization quanta by QNO for DV100 */
        static const uint8_t dv100_qstep[16] = {
            1, /* QNO = 0 and 1 both have no quantization */
            1,
            2, 3, 4, 5, 6, 7, 8, 16, 18, 20, 22, 24, 28, 52
        };
        const uint16_t *iweight1, *iweight2;

        if (d->height == 720) {
            iweight1 = &dv_iweight_720_y[0];
            iweight2 = &dv_iweight_720_c[0];
        } else {
            iweight1 = &dv_iweight_1080_y[0];
            iweight2 = &dv_iweight_1080_c[0];
        }
        for (c = 0; c < 4; c++) {
            for (s = 0; s < 16; s++) {
                for (i = 0; i < 64; i++) {
                    *factor1++ = (dv100_qstep[s] << (c + 9)) * iweight1[i];
                    *factor2++ = (dv100_qstep[s] << (c + 9)) * iweight2[i];
                }
            }
        }
    } else {
        static const uint8_t dv_quant_areas[4] = { 6, 21, 43, 64 };
        const uint16_t *iweight1 = &dv_iweight_88[0];
        for (j = 0; j < 2; j++, iweight1 = &dv_iweight_248[0]) {
            for (s = 0; s < 22; s++) {
                for (i = c = 0; c < 4; c++) {
                    for (; i < dv_quant_areas[c]; i++) {
                        *factor1   = iweight1[i] << (ff_dv_quant_shifts[s][c] + 1);
                        *factor2++ = (*factor1++) << 1;
                    }
                }
            }
        }
    }
}

static av_cold int dvvideo_decode_init(AVCodecContext *avctx)
{
    DVVideoContext *s = avctx->priv_data;
    IDCTDSPContext idsp;
    int i;

    memset(&idsp,0, sizeof(idsp));
    ff_idctdsp_init(&idsp, avctx);

    for (i = 0; i < 64; i++)
        s->dv_zigzag[0][i] = idsp.idct_permutation[ff_zigzag_direct[i]];

    if (avctx->lowres){
        for (i = 0; i < 64; i++){
            int j = ff_dv_zigzag248_direct[i];
            s->dv_zigzag[1][i] = idsp.idct_permutation[(j & 7) + (j & 8) * 4 + (j & 48) / 2];
        }
    }else
        memcpy(s->dv_zigzag[1], ff_dv_zigzag248_direct, sizeof(s->dv_zigzag[1]));

    s->idct_put[0] = idsp.idct_put;
    s->idct_put[1] = ff_simple_idct248_put;

    return ff_dvvideo_init(avctx);
}

/* decode AC coefficients */
static void dv_decode_ac(GetBitContext *gb, BlockInfo *mb, int16_t *block)
{
    int last_index = gb->size_in_bits;
    const uint8_t  *scan_table   = mb->scan_table;
    const uint32_t *factor_table = mb->factor_table;
    int pos                      = mb->pos;
    int partial_bit_count        = mb->partial_bit_count;
    int level, run, vlc_len, index;

    OPEN_READER_NOSIZE(re, gb);
    UPDATE_CACHE(re, gb);

    /* if we must parse a partial VLC, we do it here */
    if (partial_bit_count > 0) {
        re_cache              = re_cache >> partial_bit_count |
                                mb->partial_bit_buffer;
        re_index             -= partial_bit_count;
        mb->partial_bit_count = 0;
    }

    /* get the AC coefficients until last_index is reached */
    for (;;) {
        ff_dlog(NULL, "%2d: bits=%04x index=%d\n", pos, SHOW_UBITS(re, gb, 16),
                re_index);
        /* our own optimized GET_RL_VLC */
        index   = NEG_USR32(re_cache, TEX_VLC_BITS);
        vlc_len = ff_dv_rl_vlc[index].len;
        if (vlc_len < 0) {
            index = NEG_USR32((unsigned) re_cache << TEX_VLC_BITS, -vlc_len) +
                    ff_dv_rl_vlc[index].level;
            vlc_len = TEX_VLC_BITS - vlc_len;
        }
        level = ff_dv_rl_vlc[index].level;
        run   = ff_dv_rl_vlc[index].run;

        /* gotta check if we're still within gb boundaries */
        if (re_index + vlc_len > last_index) {
            /* should be < 16 bits otherwise a codeword could have been parsed */
            mb->partial_bit_count  = last_index - re_index;
            mb->partial_bit_buffer = re_cache & ~(-1u >> mb->partial_bit_count);
            re_index               = last_index;
            break;
        }
        re_index += vlc_len;

        ff_dlog(NULL, "run=%d level=%d\n", run, level);
        pos += run;
        if (pos >= 64)
            break;

        level = (level * factor_table[pos] + (1 << (dv_iweight_bits - 1))) >>
                dv_iweight_bits;
        block[scan_table[pos]] = level;

        UPDATE_CACHE(re, gb);
    }
    CLOSE_READER(re, gb);
    mb->pos = pos;
}

static inline void bit_copy(PutBitContext *pb, GetBitContext *gb)
{
    int bits_left = get_bits_left(gb);
    while (bits_left >= MIN_CACHE_BITS) {
        put_bits(pb, MIN_CACHE_BITS, get_bits(gb, MIN_CACHE_BITS));
        bits_left -= MIN_CACHE_BITS;
    }
    if (bits_left > 0)
        put_bits(pb, bits_left, get_bits(gb, bits_left));
}

/* mb_x and mb_y are in units of 8 pixels */
static int dv_decode_video_segment(AVCodecContext *avctx, void *arg)
{
    DVVideoContext *s = avctx->priv_data;
    DVwork_chunk *work_chunk = arg;
    int quant, dc, dct_mode, class1, j;
    int mb_index, mb_x, mb_y, last_index;
    int y_stride, linesize;
    int16_t *block, *block1;
    int c_offset;
    uint8_t *y_ptr;
    const uint8_t *buf_ptr;
    PutBitContext pb, vs_pb;
    GetBitContext gb;
    BlockInfo mb_data[5 * DV_MAX_BPM], *mb, *mb1;
    LOCAL_ALIGNED_16(int16_t, sblock, [5 * DV_MAX_BPM], [64]);
    LOCAL_ALIGNED_16(uint8_t, mb_bit_buffer, [80     + AV_INPUT_BUFFER_PADDING_SIZE]); /* allow some slack */
    LOCAL_ALIGNED_16(uint8_t, vs_bit_buffer, [80 * 5 + AV_INPUT_BUFFER_PADDING_SIZE]); /* allow some slack */
    const int log2_blocksize = 3-s->avctx->lowres;
    int is_field_mode[5];
    int vs_bit_buffer_damaged = 0;
    int mb_bit_buffer_damaged[5] = {0};
    int retried = 0;
    int sta;

    av_assert1((((int) mb_bit_buffer) & 7) == 0);
    av_assert1((((int) vs_bit_buffer) & 7) == 0);

retry:

    memset(sblock, 0, 5 * DV_MAX_BPM * sizeof(*sblock));

    /* pass 1: read DC and AC coefficients in blocks */
    buf_ptr = &s->buf[work_chunk->buf_offset * 80];
    block1  = &sblock[0][0];
    mb1     = mb_data;
    init_put_bits(&vs_pb, vs_bit_buffer, 5 * 80);
    for (mb_index = 0; mb_index < 5; mb_index++, mb1 += s->sys->bpm, block1 += s->sys->bpm * 64) {
        /* skip header */
        quant    = buf_ptr[3] & 0x0f;
        if (avctx->error_concealment) {
            if ((buf_ptr[3] >> 4) == 0x0E)
                vs_bit_buffer_damaged = 1;
            if (!mb_index) {
                sta = buf_ptr[3] >> 4;
            } else if (sta != (buf_ptr[3] >> 4))
                vs_bit_buffer_damaged = 1;
        }
        buf_ptr += 4;
        init_put_bits(&pb, mb_bit_buffer, 80);
        mb    = mb1;
        block = block1;
        is_field_mode[mb_index] = 0;
        for (j = 0; j < s->sys->bpm; j++) {
            last_index = s->sys->block_sizes[j];
            init_get_bits(&gb, buf_ptr, last_index);

            /* get the DC */
            dc       = get_sbits(&gb, 9);
            dct_mode = get_bits1(&gb);
            class1   = get_bits(&gb, 2);
            if (DV_PROFILE_IS_HD(s->sys)) {
                mb->idct_put     = s->idct_put[0];
                mb->scan_table   = s->dv_zigzag[0];
                mb->factor_table = &s->idct_factor[(j >= 4) * 4 * 16 * 64 +
                                                   class1       * 16 * 64 +
                                                   quant             * 64];
                is_field_mode[mb_index] |= !j && dct_mode;
            } else {
                mb->idct_put     = s->idct_put[dct_mode && log2_blocksize == 3];
                mb->scan_table   = s->dv_zigzag[dct_mode];
                mb->factor_table =
                    &s->idct_factor[(class1 == 3)               * 2 * 22 * 64 +
                                    dct_mode                        * 22 * 64 +
                                    (quant + ff_dv_quant_offset[class1]) * 64];
            }
            dc = dc << 2;
            /* convert to unsigned because 128 is not added in the
             * standard IDCT */
            dc                   += 1024;
            block[0]              = dc;
            buf_ptr              += last_index >> 3;
            mb->pos               = 0;
            mb->partial_bit_count = 0;

            ff_dlog(avctx, "MB block: %d, %d ", mb_index, j);
            dv_decode_ac(&gb, mb, block);

            /* write the remaining bits in a new buffer only if the
             * block is finished */
            if (mb->pos >= 64)
                bit_copy(&pb, &gb);
            if (mb->pos >= 64 && mb->pos < 127)
                vs_bit_buffer_damaged = mb_bit_buffer_damaged[mb_index] = 1;

            block += 64;
            mb++;
        }

        if (mb_bit_buffer_damaged[mb_index] > 0)
            continue;

        /* pass 2: we can do it just after */
        ff_dlog(avctx, "***pass 2 size=%d MB#=%d\n", put_bits_count(&pb), mb_index);
        block = block1;
        mb    = mb1;
        init_get_bits(&gb, mb_bit_buffer, put_bits_count(&pb));
        put_bits32(&pb, 0); // padding must be zeroed
        flush_put_bits(&pb);
        for (j = 0; j < s->sys->bpm; j++, block += 64, mb++) {
            if (mb->pos < 64 && get_bits_left(&gb) > 0) {
                dv_decode_ac(&gb, mb, block);
                /* if still not finished, no need to parse other blocks */
                if (mb->pos < 64)
                    break;
                if (mb->pos < 127)
                    vs_bit_buffer_damaged = mb_bit_buffer_damaged[mb_index] = 1;
            }
        }
        /* all blocks are finished, so the extra bytes can be used at
         * the video segment level */
        if (j >= s->sys->bpm)
            bit_copy(&vs_pb, &gb);
    }

    /* we need a pass over the whole video segment */
    ff_dlog(avctx, "***pass 3 size=%d\n", put_bits_count(&vs_pb));
    block = &sblock[0][0];
    mb    = mb_data;
    init_get_bits(&gb, vs_bit_buffer, put_bits_count(&vs_pb));
    put_bits32(&vs_pb, 0); // padding must be zeroed
    flush_put_bits(&vs_pb);
    for (mb_index = 0; mb_index < 5; mb_index++) {
        for (j = 0; j < s->sys->bpm; j++) {
            if (mb->pos < 64 && get_bits_left(&gb) > 0 && !vs_bit_buffer_damaged) {
                ff_dlog(avctx, "start %d:%d\n", mb_index, j);
                dv_decode_ac(&gb, mb, block);
            }

            if (mb->pos >= 64 && mb->pos < 127) {
                av_log(avctx, AV_LOG_ERROR,
                       "AC EOB marker is absent pos=%d\n", mb->pos);
                vs_bit_buffer_damaged = 1;
            }
            block += 64;
            mb++;
        }
    }
    if (vs_bit_buffer_damaged && !retried) {
        av_log(avctx, AV_LOG_ERROR, "Concealing bitstream errors\n");
        retried = 1;
        goto retry;
    }

    /* compute idct and place blocks */
    block = &sblock[0][0];
    mb    = mb_data;
    for (mb_index = 0; mb_index < 5; mb_index++) {
        dv_calculate_mb_xy(s, work_chunk, mb_index, &mb_x, &mb_y);

        /* idct_put'ting luminance */
        if ((s->sys->pix_fmt == AV_PIX_FMT_YUV420P)                      ||
            (s->sys->pix_fmt == AV_PIX_FMT_YUV411P && mb_x >= (704 / 8)) ||
            (s->sys->height >= 720 && mb_y != 134)) {
            y_stride = (s->frame->linesize[0] <<
                        ((!is_field_mode[mb_index]) * log2_blocksize));
        } else {
            y_stride = (2 << log2_blocksize);
        }
        y_ptr    = s->frame->data[0] +
                   ((mb_y * s->frame->linesize[0] + mb_x) << log2_blocksize);
        linesize = s->frame->linesize[0] << is_field_mode[mb_index];
        mb[0].idct_put(y_ptr, linesize, block + 0 * 64);
        if (s->sys->video_stype == 4) { /* SD 422 */
            mb[2].idct_put(y_ptr + (1 << log2_blocksize),            linesize, block + 2 * 64);
        } else {
            mb[1].idct_put(y_ptr + (1 << log2_blocksize),            linesize, block + 1 * 64);
            mb[2].idct_put(y_ptr                         + y_stride, linesize, block + 2 * 64);
            mb[3].idct_put(y_ptr + (1 << log2_blocksize) + y_stride, linesize, block + 3 * 64);
        }
        mb    += 4;
        block += 4 * 64;

        /* idct_put'ting chrominance */
        c_offset = (((mb_y >>  (s->sys->pix_fmt == AV_PIX_FMT_YUV420P)) * s->frame->linesize[1] +
                     (mb_x >> ((s->sys->pix_fmt == AV_PIX_FMT_YUV411P) ? 2 : 1))) << log2_blocksize);
        for (j = 2; j; j--) {
            uint8_t *c_ptr = s->frame->data[j] + c_offset;
            if (s->sys->pix_fmt == AV_PIX_FMT_YUV411P && mb_x >= (704 / 8)) {
                uint64_t aligned_pixels[64 / 8];
                uint8_t *pixels = (uint8_t *) aligned_pixels;
                uint8_t *c_ptr1, *ptr1;
                int x, y;
                mb->idct_put(pixels, 8, block);
                for (y = 0; y < (1 << log2_blocksize); y++, c_ptr += s->frame->linesize[j], pixels += 8) {
                    ptr1   = pixels + ((1 << (log2_blocksize))>>1);
                    c_ptr1 = c_ptr + (s->frame->linesize[j] << log2_blocksize);
                    for (x = 0; x < (1 << FFMAX(log2_blocksize - 1, 0)); x++) {
                        c_ptr[x]  = pixels[x];
                        c_ptr1[x] = ptr1[x];
                    }
                }
                block += 64;
                mb++;
            } else {
                y_stride = (mb_y == 134) ? (1 << log2_blocksize) :
                           s->frame->linesize[j] << ((!is_field_mode[mb_index]) * log2_blocksize);
                linesize = s->frame->linesize[j] << is_field_mode[mb_index];
                (mb++)->idct_put(c_ptr, linesize, block);
                block += 64;
                if (s->sys->bpm == 8) {
                    (mb++)->idct_put(c_ptr + y_stride, linesize, block);
                    block += 64;
                }
            }
        }
    }
    return 0;
}

/* NOTE: exactly one frame must be given (120000 bytes for NTSC,
 * 144000 bytes for PAL - or twice those for 50Mbps) */
static int dvvideo_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame, AVPacket *avpkt)
{
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    DVVideoContext *s = avctx->priv_data;
    AVFrame *frame = data;
    const uint8_t *vsc_pack;
    int apt, is16_9, ret;
    const AVDVProfile *sys;

    sys = ff_dv_frame_profile(avctx, s->sys, buf, buf_size);
    if (!sys || buf_size < sys->frame_size) {
        av_log(avctx, AV_LOG_ERROR, "could not find dv frame profile\n");
        return -1; /* NOTE: we only accept several full frames */
    }

    if (sys != s->sys) {
        ret = ff_dv_init_dynamic_tables(s, sys);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error initializing the work tables.\n");
            return ret;
        }
        dv_init_weight_tables(s, sys);
        s->sys = sys;
    }

    s->frame            = frame;
    frame->key_frame    = 1;
    frame->pict_type    = AV_PICTURE_TYPE_I;
    avctx->pix_fmt      = s->sys->pix_fmt;
    avctx->framerate    = av_inv_q(s->sys->time_base);

    ret = ff_set_dimensions(avctx, s->sys->width, s->sys->height);
    if (ret < 0)
        return ret;

    /* Determine the codec's sample_aspect ratio from the packet */
    vsc_pack = buf + 80 * 5 + 48 + 5;
    if (*vsc_pack == dv_video_control) {
        apt    = buf[4] & 0x07;
        is16_9 = (vsc_pack[2] & 0x07) == 0x02 ||
                 (!apt && (vsc_pack[2] & 0x07) == 0x07);
        ff_set_sar(avctx, s->sys->sar[is16_9]);
    }

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    frame->interlaced_frame = 1;
    frame->top_field_first  = 0;

    /* Determine the codec's field order from the packet */
    if ( *vsc_pack == dv_video_control ) {
        frame->top_field_first = !(vsc_pack[3] & 0x40);
    }

    s->buf = buf;
    avctx->execute(avctx, dv_decode_video_segment, s->work_chunks, NULL,
                   dv_work_pool_size(s->sys), sizeof(DVwork_chunk));

    emms_c();

    /* return image */
    *got_frame = 1;

    return s->sys->frame_size;
}

AVCodec ff_dvvideo_decoder = {
    .name           = "dvvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("DV (Digital Video)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DVVIDEO,
    .priv_data_size = sizeof(DVVideoContext),
    .init           = dvvideo_decode_init,
    .decode         = dvvideo_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_SLICE_THREADS,
    .max_lowres     = 3,
};
