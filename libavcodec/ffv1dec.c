/*
 * FFV1 decoder
 *
 * Copyright (c) 2003-2013 Michael Niedermayer <michaelni@gmx.at>
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
 * FF Video Codec 1 (a lossless codec) decoder
 */

#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/mem.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "rangecoder.h"
#include "golomb.h"
#include "mathops.h"
#include "ffv1.h"
#include "progressframe.h"
#include "refstruct.h"
#include "thread.h"

static inline av_flatten int get_symbol_inline(RangeCoder *c, uint8_t *state,
                                               int is_signed)
{
    if (get_rac(c, state + 0))
        return 0;
    else {
        int e;
        unsigned a;
        e = 0;
        while (get_rac(c, state + 1 + FFMIN(e, 9))) { // 1..10
            e++;
            if (e > 31)
                return AVERROR_INVALIDDATA;
        }

        a = 1;
        for (int i = e - 1; i >= 0; i--)
            a += a + get_rac(c, state + 22 + FFMIN(i, 9));  // 22..31

        e = -(is_signed && get_rac(c, state + 11 + FFMIN(e, 10))); // 11..21
        return (a ^ e) - e;
    }
}

static av_noinline int get_symbol(RangeCoder *c, uint8_t *state, int is_signed)
{
    return get_symbol_inline(c, state, is_signed);
}

static inline int get_vlc_symbol(GetBitContext *gb, VlcState *const state,
                                 int bits)
{
    int k, i, v, ret;

    i = state->count;
    k = 0;
    while (i < state->error_sum) { // FIXME: optimize
        k++;
        i += i;
    }

    v = get_sr_golomb(gb, k, 12, bits);
    ff_dlog(NULL, "v:%d bias:%d error:%d drift:%d count:%d k:%d",
            v, state->bias, state->error_sum, state->drift, state->count, k);

    v ^= ((2 * state->drift + state->count) >> 31);

    ret = fold(v + state->bias, bits);

    update_vlc_state(state, v);

    return ret;
}

static int is_input_end(RangeCoder *c, GetBitContext *gb, int ac)
{
    if (ac != AC_GOLOMB_RICE) {
        if (c->overread > MAX_OVERREAD)
            return AVERROR_INVALIDDATA;
    } else {
        if (get_bits_left(gb) < 1)
            return AVERROR_INVALIDDATA;
    }
    return 0;
}

#define TYPE int16_t
#define RENAME(name) name
#include "ffv1dec_template.c"
#undef TYPE
#undef RENAME

#define TYPE int32_t
#define RENAME(name) name ## 32
#include "ffv1dec_template.c"

static int decode_plane(FFV1Context *f, FFV1SliceContext *sc,
                        GetBitContext *gb,
                        uint8_t *src, int w, int h, int stride, int plane_index,
                         int pixel_stride)
{
    const int ac = f->ac;
    int x, y;
    int16_t *sample[2];
    sample[0] = sc->sample_buffer + 3;
    sample[1] = sc->sample_buffer + w + 6 + 3;

    sc->run_index = 0;

    memset(sc->sample_buffer, 0, 2 * (w + 6) * sizeof(*sc->sample_buffer));

    for (y = 0; y < h; y++) {
        int16_t *temp = sample[0]; // FIXME: try a normal buffer

        sample[0] = sample[1];
        sample[1] = temp;

        sample[1][-1] = sample[0][0];
        sample[0][w]  = sample[0][w - 1];

        if (f->avctx->bits_per_raw_sample <= 8) {
            int ret = decode_line(f, sc, gb, w, sample, plane_index, 8, ac);
            if (ret < 0)
                return ret;
            for (x = 0; x < w; x++)
                src[x*pixel_stride + stride * y] = sample[1][x];
        } else {
            int ret = decode_line(f, sc, gb, w, sample, plane_index, f->avctx->bits_per_raw_sample, ac);
            if (ret < 0)
                return ret;
            if (f->packed_at_lsb) {
                for (x = 0; x < w; x++) {
                    ((uint16_t*)(src + stride*y))[x*pixel_stride] = sample[1][x];
                }
            } else {
                for (x = 0; x < w; x++) {
                    ((uint16_t*)(src + stride*y))[x*pixel_stride] = sample[1][x] << (16 - f->avctx->bits_per_raw_sample) | ((uint16_t **)sample)[1][x] >> (2 * f->avctx->bits_per_raw_sample - 16);
                }
            }
        }
    }
    return 0;
}

static int decode_slice_header(const FFV1Context *f,
                               FFV1SliceContext *sc, AVFrame *frame)
{
    RangeCoder *c = &sc->c;
    uint8_t state[CONTEXT_SIZE];
    unsigned ps, context_count;
    int sx, sy, sw, sh;

    memset(state, 128, sizeof(state));
    sx = get_symbol(c, state, 0);
    sy = get_symbol(c, state, 0);
    sw = get_symbol(c, state, 0) + 1U;
    sh = get_symbol(c, state, 0) + 1U;

    av_assert0(f->version > 2);


    if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0)
        return AVERROR_INVALIDDATA;
    if (sx > f->num_h_slices - sw || sy > f->num_v_slices - sh)
        return AVERROR_INVALIDDATA;

    sc->slice_x      =  ff_slice_coord(f, f->width , sx     , f->num_h_slices, f->chroma_h_shift);
    sc->slice_y      =  ff_slice_coord(f, f->height, sy     , f->num_v_slices, f->chroma_v_shift);
    sc->slice_width  =  ff_slice_coord(f, f->width , sx + sw, f->num_h_slices, f->chroma_h_shift) - sc->slice_x;
    sc->slice_height =  ff_slice_coord(f, f->height, sy + sh, f->num_v_slices, f->chroma_v_shift) - sc->slice_y;

    av_assert0((unsigned)sc->slice_width  <= f->width &&
                (unsigned)sc->slice_height <= f->height);
    av_assert0 (   (unsigned)sc->slice_x + (uint64_t)sc->slice_width  <= f->width
                && (unsigned)sc->slice_y + (uint64_t)sc->slice_height <= f->height);

    if (f->ac == AC_GOLOMB_RICE && sc->slice_width >= (1<<23))
        return AVERROR_INVALIDDATA;

    for (unsigned i = 0; i < f->plane_count; i++) {
        PlaneContext * const p = &sc->plane[i];
        int idx = get_symbol(c, state, 0);
        if (idx >= (unsigned)f->quant_table_count) {
            av_log(f->avctx, AV_LOG_ERROR, "quant_table_index out of range\n");
            return -1;
        }
        p->quant_table_index = idx;
        context_count = f->context_count[idx];

        if (p->context_count < context_count) {
            av_freep(&p->state);
            av_freep(&p->vlc_state);
        }
        p->context_count = context_count;
    }

    ps = get_symbol(c, state, 0);
    if (ps == 1) {
        frame->flags |= AV_FRAME_FLAG_INTERLACED;
        frame->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    } else if (ps == 2) {
        frame->flags |= AV_FRAME_FLAG_INTERLACED;
        frame->flags &= ~AV_FRAME_FLAG_TOP_FIELD_FIRST;
    } else if (ps == 3) {
        frame->flags &= ~AV_FRAME_FLAG_INTERLACED;
    }
    frame->sample_aspect_ratio.num = get_symbol(c, state, 0);
    frame->sample_aspect_ratio.den = get_symbol(c, state, 0);

    if (av_image_check_sar(f->width, f->height,
                           frame->sample_aspect_ratio) < 0) {
        av_log(f->avctx, AV_LOG_WARNING, "ignoring invalid SAR: %u/%u\n",
               frame->sample_aspect_ratio.num,
               frame->sample_aspect_ratio.den);
        frame->sample_aspect_ratio = (AVRational){ 0, 1 };
    }

    if (f->version > 3) {
        sc->slice_reset_contexts = get_rac(c, state);
        sc->slice_coding_mode = get_symbol(c, state, 0);
        if (sc->slice_coding_mode != 1 && f->colorspace == 1) {
            sc->slice_rct_by_coef = get_symbol(c, state, 0);
            sc->slice_rct_ry_coef = get_symbol(c, state, 0);
            if ((uint64_t)sc->slice_rct_by_coef + (uint64_t)sc->slice_rct_ry_coef > 4) {
                av_log(f->avctx, AV_LOG_ERROR, "slice_rct_y_coef out of range\n");
                return AVERROR_INVALIDDATA;
            }
        }
    }

    return 0;
}

static void slice_set_damaged(FFV1Context *f, FFV1SliceContext *sc)
{
    sc->slice_damaged = 1;

    // only set this for frame threading, as for slice threading its value is
    // not used and setting it would be a race
    if (f->avctx->active_thread_type & FF_THREAD_FRAME)
        f->frame_damaged = 1;
}

static int decode_slice(AVCodecContext *c, void *arg)
{
    FFV1Context *f    = c->priv_data;
    FFV1SliceContext *sc = arg;
    int width, height, x, y, ret;
    const int ps      = av_pix_fmt_desc_get(c->pix_fmt)->comp[0].step;
    AVFrame * const p = f->picture.f;
    const int      si = sc - f->slices;
    GetBitContext gb;

    if (!(p->flags & AV_FRAME_FLAG_KEY) && f->last_picture.f)
        ff_progress_frame_await(&f->last_picture, si);

    if (f->slice_damaged[si])
        slice_set_damaged(f, sc);

    sc->slice_rct_by_coef = 1;
    sc->slice_rct_ry_coef = 1;

    if (f->version > 2) {
        if (ff_ffv1_init_slice_state(f, sc) < 0)
            return AVERROR(ENOMEM);
        if (decode_slice_header(f, sc, p) < 0) {
            sc->slice_x = sc->slice_y = sc->slice_height = sc->slice_width = 0;
            slice_set_damaged(f, sc);
            return AVERROR_INVALIDDATA;
        }
    }
    if ((ret = ff_ffv1_init_slice_state(f, sc)) < 0)
        return ret;
    if ((p->flags & AV_FRAME_FLAG_KEY) || sc->slice_reset_contexts) {
        ff_ffv1_clear_slice_state(f, sc);
    } else if (sc->slice_damaged) {
        return AVERROR_INVALIDDATA;
    }

    width  = sc->slice_width;
    height = sc->slice_height;
    x      = sc->slice_x;
    y      = sc->slice_y;

    if (f->ac == AC_GOLOMB_RICE) {
        if (f->version == 3 && f->micro_version > 1 || f->version > 3)
            get_rac(&sc->c, (uint8_t[]) { 129 });
        sc->ac_byte_count = f->version > 2 || (!x && !y) ? sc->c.bytestream - sc->c.bytestream_start - 1 : 0;
        init_get_bits(&gb,
                      sc->c.bytestream_start + sc->ac_byte_count,
                      (sc->c.bytestream_end - sc->c.bytestream_start - sc->ac_byte_count) * 8);
    }

    av_assert1(width && height);
    if (f->colorspace == 0 && (f->chroma_planes || !f->transparency)) {
        const int chroma_width  = AV_CEIL_RSHIFT(width,  f->chroma_h_shift);
        const int chroma_height = AV_CEIL_RSHIFT(height, f->chroma_v_shift);
        const int cx            = x >> f->chroma_h_shift;
        const int cy            = y >> f->chroma_v_shift;
        decode_plane(f, sc, &gb, p->data[0] + ps*x + y*p->linesize[0], width, height, p->linesize[0], 0, 1);

        if (f->chroma_planes) {
            decode_plane(f, sc, &gb, p->data[1] + ps*cx+cy*p->linesize[1], chroma_width, chroma_height, p->linesize[1], 1, 1);
            decode_plane(f, sc, &gb, p->data[2] + ps*cx+cy*p->linesize[2], chroma_width, chroma_height, p->linesize[2], 1, 1);
        }
        if (f->transparency)
            decode_plane(f, sc, &gb, p->data[3] + ps*x + y*p->linesize[3], width, height, p->linesize[3], (f->version >= 4 && !f->chroma_planes) ? 1 : 2, 1);
    } else if (f->colorspace == 0) {
         decode_plane(f, sc, &gb, p->data[0] + ps*x + y*p->linesize[0]    , width, height, p->linesize[0], 0, 2);
         decode_plane(f, sc, &gb, p->data[0] + ps*x + y*p->linesize[0] + 1, width, height, p->linesize[0], 1, 2);
    } else if (f->use32bit) {
        uint8_t *planes[4] = { p->data[0] + ps * x + y * p->linesize[0],
                               p->data[1] + ps * x + y * p->linesize[1],
                               p->data[2] + ps * x + y * p->linesize[2],
                               p->data[3] + ps * x + y * p->linesize[3] };
        decode_rgb_frame32(f, sc, &gb, planes, width, height, p->linesize);
    } else {
        uint8_t *planes[4] = { p->data[0] + ps * x + y * p->linesize[0],
                               p->data[1] + ps * x + y * p->linesize[1],
                               p->data[2] + ps * x + y * p->linesize[2],
                               p->data[3] + ps * x + y * p->linesize[3] };
        decode_rgb_frame(f, sc, &gb, planes, width, height, p->linesize);
    }
    if (f->ac != AC_GOLOMB_RICE && f->version > 2) {
        int v;
        get_rac(&sc->c, (uint8_t[]) { 129 });
        v = sc->c.bytestream_end - sc->c.bytestream - 2 - 5*!!f->ec;
        if (v) {
            av_log(f->avctx, AV_LOG_ERROR, "bytestream end mismatching by %d\n", v);
            slice_set_damaged(f, sc);
        }
    }

    if (sc->slice_damaged && (f->avctx->err_recognition & AV_EF_EXPLODE))
        return AVERROR_INVALIDDATA;

    if ((c->active_thread_type & FF_THREAD_FRAME) && !f->frame_damaged)
        ff_progress_frame_report(&f->picture, si);

    return 0;
}

static int read_quant_table(RangeCoder *c, int16_t *quant_table, int scale)
{
    int v;
    int i = 0;
    uint8_t state[CONTEXT_SIZE];

    memset(state, 128, sizeof(state));

    for (v = 0; i < 128; v++) {
        unsigned len = get_symbol(c, state, 0) + 1U;

        if (len > 128 - i || !len)
            return AVERROR_INVALIDDATA;

        while (len--) {
            quant_table[i] = scale * v;
            i++;
        }
    }

    for (i = 1; i < 128; i++)
        quant_table[256 - i] = -quant_table[i];
    quant_table[128] = -quant_table[127];

    return 2 * v - 1;
}

static int read_quant_tables(RangeCoder *c,
                             int16_t quant_table[MAX_CONTEXT_INPUTS][256])
{
    int i;
    int context_count = 1;

    for (i = 0; i < 5; i++) {
        int ret = read_quant_table(c, quant_table[i], context_count);
        if (ret < 0)
            return ret;
        context_count *= ret;
        if (context_count > 32768U) {
            return AVERROR_INVALIDDATA;
        }
    }
    return (context_count + 1) / 2;
}

static int read_extra_header(FFV1Context *f)
{
    RangeCoder c;
    uint8_t state[CONTEXT_SIZE];
    int ret;
    uint8_t state2[32][CONTEXT_SIZE];
    unsigned crc = 0;

    memset(state2, 128, sizeof(state2));
    memset(state, 128, sizeof(state));

    ff_init_range_decoder(&c, f->avctx->extradata, f->avctx->extradata_size);
    ff_build_rac_states(&c, 0.05 * (1LL << 32), 256 - 8);

    f->version = get_symbol(&c, state, 0);
    if (f->version < 2) {
        av_log(f->avctx, AV_LOG_ERROR, "Invalid version in global header\n");
        return AVERROR_INVALIDDATA;
    }
    if (f->version > 4) {
        av_log(f->avctx, AV_LOG_ERROR, "unsupported version %d\n",
            f->version);
        return AVERROR_PATCHWELCOME;
    }
    if (f->version > 2) {
        c.bytestream_end -= 4;
        f->micro_version = get_symbol(&c, state, 0);
        if (f->micro_version < 0)
            return AVERROR_INVALIDDATA;
    }
    f->ac = get_symbol(&c, state, 0);

    if (f->ac == AC_RANGE_CUSTOM_TAB) {
        for (int i = 1; i < 256; i++)
            f->state_transition[i] = get_symbol(&c, state, 1) + c.one_state[i];
    }

    f->colorspace                 = get_symbol(&c, state, 0); //YUV cs type
    f->avctx->bits_per_raw_sample = get_symbol(&c, state, 0);
    f->chroma_planes              = get_rac(&c, state);
    f->chroma_h_shift             = get_symbol(&c, state, 0);
    f->chroma_v_shift             = get_symbol(&c, state, 0);
    f->transparency               = get_rac(&c, state);
    f->plane_count                = 1 + (f->chroma_planes || f->version<4) + f->transparency;
    f->num_h_slices               = 1 + get_symbol(&c, state, 0);
    f->num_v_slices               = 1 + get_symbol(&c, state, 0);

    if (f->chroma_h_shift > 4U || f->chroma_v_shift > 4U) {
        av_log(f->avctx, AV_LOG_ERROR, "chroma shift parameters %d %d are invalid\n",
               f->chroma_h_shift, f->chroma_v_shift);
        return AVERROR_INVALIDDATA;
    }

    if (f->num_h_slices > (unsigned)f->width  || !f->num_h_slices ||
        f->num_v_slices > (unsigned)f->height || !f->num_v_slices
       ) {
        av_log(f->avctx, AV_LOG_ERROR, "slice count invalid\n");
        return AVERROR_INVALIDDATA;
    }

    if (f->num_h_slices > MAX_SLICES / f->num_v_slices) {
        av_log(f->avctx, AV_LOG_ERROR, "slice count unsupported\n");
        return AVERROR_PATCHWELCOME;
    }

    f->quant_table_count = get_symbol(&c, state, 0);
    if (f->quant_table_count > (unsigned)MAX_QUANT_TABLES || !f->quant_table_count) {
        av_log(f->avctx, AV_LOG_ERROR, "quant table count %d is invalid\n", f->quant_table_count);
        f->quant_table_count = 0;
        return AVERROR_INVALIDDATA;
    }

    for (int i = 0; i < f->quant_table_count; i++) {
        f->context_count[i] = read_quant_tables(&c, f->quant_tables[i]);
        if (f->context_count[i] < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return AVERROR_INVALIDDATA;
        }
    }
    if ((ret = ff_ffv1_allocate_initial_states(f)) < 0)
        return ret;

    for (int i = 0; i < f->quant_table_count; i++)
        if (get_rac(&c, state)) {
            for (int j = 0; j < f->context_count[i]; j++)
                for (int k = 0; k < CONTEXT_SIZE; k++) {
                    int pred = j ? f->initial_states[i][j - 1][k] : 128;
                    f->initial_states[i][j][k] =
                        (pred + get_symbol(&c, state2[k], 1)) & 0xFF;
                }
        }

    if (f->version > 2) {
        f->ec = get_symbol(&c, state, 0);
        if (f->ec >= 2)
            f->crcref = 0x7a8c4079;
        if (f->micro_version > 2)
            f->intra = get_symbol(&c, state, 0);
    }

    if (f->version > 2) {
        unsigned v;
        v = av_crc(av_crc_get_table(AV_CRC_32_IEEE), f->crcref,
                   f->avctx->extradata, f->avctx->extradata_size);
        if (v != f->crcref || f->avctx->extradata_size < 4) {
            av_log(f->avctx, AV_LOG_ERROR, "CRC mismatch %X!\n", v);
            return AVERROR_INVALIDDATA;
        }
        crc = AV_RB32(f->avctx->extradata + f->avctx->extradata_size - 4);
    }

    if (f->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(f->avctx, AV_LOG_DEBUG,
               "global: ver:%d.%d, coder:%d, colorspace: %d bpr:%d chroma:%d(%d:%d), alpha:%d slices:%dx%d qtabs:%d ec:%d intra:%d CRC:0x%08X\n",
               f->version, f->micro_version,
               f->ac,
               f->colorspace,
               f->avctx->bits_per_raw_sample,
               f->chroma_planes, f->chroma_h_shift, f->chroma_v_shift,
               f->transparency,
               f->num_h_slices, f->num_v_slices,
               f->quant_table_count,
               f->ec,
               f->intra,
               crc
              );
    return 0;
}

static int read_header(FFV1Context *f)
{
    uint8_t state[CONTEXT_SIZE];
    int context_count = -1; //-1 to avoid warning
    RangeCoder *const c = &f->slices[0].c;

    memset(state, 128, sizeof(state));

    if (f->version < 2) {
        int chroma_planes, chroma_h_shift, chroma_v_shift, transparency, colorspace, bits_per_raw_sample;
        unsigned v= get_symbol(c, state, 0);
        if (v >= 2) {
            av_log(f->avctx, AV_LOG_ERROR, "invalid version %d in ver01 header\n", v);
            return AVERROR_INVALIDDATA;
        }
        f->version = v;
        f->ac = get_symbol(c, state, 0);

        if (f->ac == AC_RANGE_CUSTOM_TAB) {
            for (int i = 1; i < 256; i++) {
                int st = get_symbol(c, state, 1) + c->one_state[i];
                if (st < 1 || st > 255) {
                    av_log(f->avctx, AV_LOG_ERROR, "invalid state transition %d\n", st);
                    return AVERROR_INVALIDDATA;
                }
                f->state_transition[i] = st;
            }
        }

        colorspace          = get_symbol(c, state, 0); //YUV cs type
        bits_per_raw_sample = f->version > 0 ? get_symbol(c, state, 0) : f->avctx->bits_per_raw_sample;
        chroma_planes       = get_rac(c, state);
        chroma_h_shift      = get_symbol(c, state, 0);
        chroma_v_shift      = get_symbol(c, state, 0);
        transparency        = get_rac(c, state);
        if (colorspace == 0 && f->avctx->skip_alpha)
            transparency = 0;

        if (f->plane_count) {
            if (colorspace          != f->colorspace                 ||
                bits_per_raw_sample != f->avctx->bits_per_raw_sample ||
                chroma_planes       != f->chroma_planes              ||
                chroma_h_shift      != f->chroma_h_shift             ||
                chroma_v_shift      != f->chroma_v_shift             ||
                transparency        != f->transparency) {
                av_log(f->avctx, AV_LOG_ERROR, "Invalid change of global parameters\n");
                return AVERROR_INVALIDDATA;
            }
        }

        if (chroma_h_shift > 4U || chroma_v_shift > 4U) {
            av_log(f->avctx, AV_LOG_ERROR, "chroma shift parameters %d %d are invalid\n",
                   chroma_h_shift, chroma_v_shift);
            return AVERROR_INVALIDDATA;
        }

        f->colorspace                 = colorspace;
        f->avctx->bits_per_raw_sample = bits_per_raw_sample;
        f->chroma_planes              = chroma_planes;
        f->chroma_h_shift             = chroma_h_shift;
        f->chroma_v_shift             = chroma_v_shift;
        f->transparency               = transparency;

        f->plane_count    = 2 + f->transparency;
    }

    if (f->colorspace == 0) {
        if (!f->transparency && !f->chroma_planes) {
            if (f->avctx->bits_per_raw_sample <= 8)
                f->avctx->pix_fmt = AV_PIX_FMT_GRAY8;
            else if (f->avctx->bits_per_raw_sample == 9) {
                f->packed_at_lsb = 1;
                f->avctx->pix_fmt = AV_PIX_FMT_GRAY9;
            } else if (f->avctx->bits_per_raw_sample == 10) {
                f->packed_at_lsb = 1;
                f->avctx->pix_fmt = AV_PIX_FMT_GRAY10;
            } else if (f->avctx->bits_per_raw_sample == 12) {
                f->packed_at_lsb = 1;
                f->avctx->pix_fmt = AV_PIX_FMT_GRAY12;
            } else if (f->avctx->bits_per_raw_sample == 14) {
                f->packed_at_lsb = 1;
                f->avctx->pix_fmt = AV_PIX_FMT_GRAY14;
            } else if (f->avctx->bits_per_raw_sample == 16) {
                f->packed_at_lsb = 1;
                f->avctx->pix_fmt = AV_PIX_FMT_GRAY16;
            } else if (f->avctx->bits_per_raw_sample < 16) {
                f->avctx->pix_fmt = AV_PIX_FMT_GRAY16;
            } else
                return AVERROR(ENOSYS);
        } else if (f->transparency && !f->chroma_planes) {
            if (f->avctx->bits_per_raw_sample <= 8)
                f->avctx->pix_fmt = AV_PIX_FMT_YA8;
            else
                return AVERROR(ENOSYS);
        } else if (f->avctx->bits_per_raw_sample<=8 && !f->transparency) {
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P; break;
            case 0x01: f->avctx->pix_fmt = AV_PIX_FMT_YUV440P; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P; break;
            case 0x20: f->avctx->pix_fmt = AV_PIX_FMT_YUV411P; break;
            case 0x22: f->avctx->pix_fmt = AV_PIX_FMT_YUV410P; break;
            }
        } else if (f->avctx->bits_per_raw_sample <= 8 && f->transparency) {
            switch(16*f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUVA444P; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUVA422P; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUVA420P; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 9 && !f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P9; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P9; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P9; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 9 && f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUVA444P9; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUVA422P9; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUVA420P9; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 10 && !f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P10; break;
            case 0x01: f->avctx->pix_fmt = AV_PIX_FMT_YUV440P10; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P10; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P10; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 10 && f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUVA444P10; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUVA422P10; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUVA420P10; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 12 && !f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P12; break;
            case 0x01: f->avctx->pix_fmt = AV_PIX_FMT_YUV440P12; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P12; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P12; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 12 && f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUVA444P12; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUVA422P12; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 14 && !f->transparency) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P14; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P14; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P14; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 16 && !f->transparency){
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P16; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P16; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P16; break;
            }
        } else if (f->avctx->bits_per_raw_sample == 16 && f->transparency){
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUVA444P16; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUVA422P16; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUVA420P16; break;
            }
        }
    } else if (f->colorspace == 1) {
        if (f->chroma_h_shift || f->chroma_v_shift) {
            av_log(f->avctx, AV_LOG_ERROR,
                   "chroma subsampling not supported in this colorspace\n");
            return AVERROR(ENOSYS);
        }
        if (     f->avctx->bits_per_raw_sample <=  8 && !f->transparency)
            f->avctx->pix_fmt = AV_PIX_FMT_0RGB32;
        else if (f->avctx->bits_per_raw_sample <=  8 && f->transparency)
            f->avctx->pix_fmt = AV_PIX_FMT_RGB32;
        else if (f->avctx->bits_per_raw_sample ==  9 && !f->transparency)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRP9;
        else if (f->avctx->bits_per_raw_sample == 10 && !f->transparency)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRP10;
        else if (f->avctx->bits_per_raw_sample == 10 && f->transparency)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRAP10;
        else if (f->avctx->bits_per_raw_sample == 12 && !f->transparency)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRP12;
        else if (f->avctx->bits_per_raw_sample == 12 && f->transparency)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRAP12;
        else if (f->avctx->bits_per_raw_sample == 14 && !f->transparency)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRP14;
        else if (f->avctx->bits_per_raw_sample == 14 && f->transparency)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRAP14;
        else if (f->avctx->bits_per_raw_sample == 16 && !f->transparency) {
            f->avctx->pix_fmt = AV_PIX_FMT_GBRP16;
            f->use32bit = 1;
        }
        else if (f->avctx->bits_per_raw_sample == 16 && f->transparency) {
            f->avctx->pix_fmt = AV_PIX_FMT_GBRAP16;
            f->use32bit = 1;
        }
    } else {
        av_log(f->avctx, AV_LOG_ERROR, "colorspace not supported\n");
        return AVERROR(ENOSYS);
    }
    if (f->avctx->pix_fmt == AV_PIX_FMT_NONE) {
        av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
        return AVERROR(ENOSYS);
    }

    ff_dlog(f->avctx, "%d %d %d\n",
            f->chroma_h_shift, f->chroma_v_shift, f->avctx->pix_fmt);
    if (f->version < 2) {
        context_count = read_quant_tables(c, f->quant_tables[0]);
        if (context_count < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return AVERROR_INVALIDDATA;
        }
        f->slice_count = f->max_slice_count;
    } else if (f->version < 3) {
        f->slice_count = get_symbol(c, state, 0);
    } else {
        const uint8_t *p = c->bytestream_end;
        for (f->slice_count = 0;
             f->slice_count < MAX_SLICES && 3 + 5*!!f->ec < p - c->bytestream_start;
             f->slice_count++) {
            int trailer = 3 + 5*!!f->ec;
            int size = AV_RB24(p-trailer);
            if (size + trailer > p - c->bytestream_start)
                break;
            p -= size + trailer;
        }
    }
    if (f->slice_count > (unsigned)MAX_SLICES || f->slice_count <= 0 || f->slice_count > f->max_slice_count) {
        av_log(f->avctx, AV_LOG_ERROR, "slice count %d is invalid (max=%d)\n", f->slice_count, f->max_slice_count);
        return AVERROR_INVALIDDATA;
    }

    ff_refstruct_unref(&f->slice_damaged);
    f->slice_damaged = ff_refstruct_allocz(f->slice_count * sizeof(*f->slice_damaged));
    if (!f->slice_damaged)
        return AVERROR(ENOMEM);

    for (int j = 0; j < f->slice_count; j++) {
        FFV1SliceContext *sc = &f->slices[j];

        if (f->version == 2) {
            int sx = get_symbol(c, state, 0);
            int sy = get_symbol(c, state, 0);
            int sw = get_symbol(c, state, 0) + 1U;
            int sh = get_symbol(c, state, 0) + 1U;

            if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0)
                return AVERROR_INVALIDDATA;
            if (sx > f->num_h_slices - sw || sy > f->num_v_slices - sh)
                return AVERROR_INVALIDDATA;

            sc->slice_x      =  sx       * (int64_t)f->width  / f->num_h_slices;
            sc->slice_y      =  sy       * (int64_t)f->height / f->num_v_slices;
            sc->slice_width  = (sx + sw) * (int64_t)f->width  / f->num_h_slices - sc->slice_x;
            sc->slice_height = (sy + sh) * (int64_t)f->height / f->num_v_slices - sc->slice_y;

            av_assert0((unsigned)sc->slice_width  <= f->width &&
                       (unsigned)sc->slice_height <= f->height);
            av_assert0 (   (unsigned)sc->slice_x + (uint64_t)sc->slice_width  <= f->width
                        && (unsigned)sc->slice_y + (uint64_t)sc->slice_height <= f->height);
        }

        ff_refstruct_unref(&sc->plane);
        sc->plane = ff_ffv1_planes_alloc();
        if (!sc->plane)
            return AVERROR(ENOMEM);

        for (int i = 0; i < f->plane_count; i++) {
            PlaneContext *const p = &sc->plane[i];

            if (f->version == 2) {
                int idx = get_symbol(c, state, 0);
                if (idx >= (unsigned)f->quant_table_count) {
                    av_log(f->avctx, AV_LOG_ERROR,
                           "quant_table_index out of range\n");
                    return AVERROR_INVALIDDATA;
                }
                p->quant_table_index = idx;
                context_count = f->context_count[idx];
            }

            if (f->version <= 2) {
                av_assert0(context_count >= 0);
                p->context_count = context_count;
            }
        }
    }
    return 0;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    FFV1Context *f = avctx->priv_data;
    int ret;

    if ((ret = ff_ffv1_common_init(avctx)) < 0)
        return ret;

    if (avctx->extradata_size > 0 && (ret = read_extra_header(f)) < 0)
        return ret;

    if ((ret = ff_ffv1_init_slice_contexts(f)) < 0)
        return ret;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                        int *got_frame, AVPacket *avpkt)
{
    uint8_t *buf        = avpkt->data;
    int buf_size        = avpkt->size;
    FFV1Context *f      = avctx->priv_data;
    RangeCoder *const c = &f->slices[0].c;
    int ret, key_frame;
    uint8_t keystate = 128;
    uint8_t *buf_p;
    AVFrame *p;

    ff_progress_frame_unref(&f->last_picture);
    FFSWAP(ProgressFrame, f->picture, f->last_picture);


    f->avctx = avctx;
    f->frame_damaged = 0;
    ff_init_range_decoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05 * (1LL << 32), 256 - 8);

    if (get_rac(c, &keystate)) {
        key_frame = AV_FRAME_FLAG_KEY;
        f->key_frame_ok = 0;
        if ((ret = read_header(f)) < 0)
            return ret;
        f->key_frame_ok = 1;
    } else {
        if (!f->key_frame_ok) {
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot decode non-keyframe without valid keyframe\n");
            return AVERROR_INVALIDDATA;
        }
        key_frame = 0;
    }

    if (f->ac != AC_GOLOMB_RICE) {
        if (buf_size < avctx->width * avctx->height / (128*8))
            return AVERROR_INVALIDDATA;
    } else {
        int w = avctx->width;
        int s = 1 + w / (1<<23);
        int i;

        w /= s;

        for (i = 0; w > (1<<ff_log2_run[i]); i++)
            w -= ff_log2_run[i];
        if (buf_size < (avctx->height + i + 6) / 8 * s)
            return AVERROR_INVALIDDATA;
    }

    ret = ff_progress_frame_get_buffer(avctx, &f->picture,
                                       AV_GET_BUFFER_FLAG_REF);
    if (ret < 0)
        return ret;

    p = f->picture.f;

    p->pict_type = AV_PICTURE_TYPE_I; //FIXME I vs. P
    p->flags     = (p->flags & ~AV_FRAME_FLAG_KEY) | key_frame;

    if (f->version < 3 && avctx->field_order > AV_FIELD_PROGRESSIVE) {
        /* we have interlaced material flagged in container */
        p->flags |= AV_FRAME_FLAG_INTERLACED;
        if (avctx->field_order == AV_FIELD_TT || avctx->field_order == AV_FIELD_TB)
            p->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    }

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_DEBUG, "ver:%d keyframe:%d coder:%d ec:%d slices:%d bps:%d\n",
               f->version, !!(p->flags & AV_FRAME_FLAG_KEY), f->ac, f->ec, f->slice_count, f->avctx->bits_per_raw_sample);

    ff_thread_finish_setup(avctx);

    buf_p = buf + buf_size;
    for (int i = f->slice_count - 1; i >= 0; i--) {
        FFV1SliceContext *sc = &f->slices[i];
        int trailer = 3 + 5*!!f->ec;
        int v;

        sc->slice_damaged = 0;

        if (i || f->version > 2) {
            if (trailer > buf_p - buf) v = INT_MAX;
            else                       v = AV_RB24(buf_p-trailer) + trailer;
        } else                         v = buf_p - c->bytestream_start;
        if (buf_p - c->bytestream_start < v) {
            av_log(avctx, AV_LOG_ERROR, "Slice pointer chain broken\n");
            ff_progress_frame_report(&f->picture, INT_MAX);
            return AVERROR_INVALIDDATA;
        }
        buf_p -= v;

        if (f->ec) {
            unsigned crc = av_crc(av_crc_get_table(AV_CRC_32_IEEE), f->crcref, buf_p, v);
            if (crc != f->crcref) {
                int64_t ts = avpkt->pts != AV_NOPTS_VALUE ? avpkt->pts : avpkt->dts;
                av_log(f->avctx, AV_LOG_ERROR, "slice CRC mismatch %X!", crc);
                if (ts != AV_NOPTS_VALUE && avctx->pkt_timebase.num) {
                    av_log(f->avctx, AV_LOG_ERROR, "at %f seconds\n", ts*av_q2d(avctx->pkt_timebase));
                } else if (ts != AV_NOPTS_VALUE) {
                    av_log(f->avctx, AV_LOG_ERROR, "at %"PRId64"\n", ts);
                } else {
                    av_log(f->avctx, AV_LOG_ERROR, "\n");
                }
                slice_set_damaged(f, sc);
            }
            if (avctx->debug & FF_DEBUG_PICT_INFO) {
                av_log(avctx, AV_LOG_DEBUG, "slice %d, CRC: 0x%08"PRIX32"\n", i, AV_RB32(buf_p + v - 4));
            }
        }

        if (i) {
            ff_init_range_decoder(&sc->c, buf_p, v);
            ff_build_rac_states(&sc->c, 0.05 * (1LL << 32), 256 - 8);
        } else
            sc->c.bytestream_end = buf_p + v;

    }

    avctx->execute(avctx,
                   decode_slice,
                   f->slices,
                   NULL,
                   f->slice_count,
                   sizeof(*f->slices));

    for (int i = f->slice_count - 1; i >= 0; i--) {
        FFV1SliceContext *sc = &f->slices[i];
        if (sc->slice_damaged && f->last_picture.f) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
            const uint8_t *src[4];
            uint8_t *dst[4];
            ff_progress_frame_await(&f->last_picture, INT_MAX);
            for (int j = 0; j < desc->nb_components; j++) {
                int pixshift = desc->comp[j].depth > 8;
                int sh = (j == 1 || j == 2) ? f->chroma_h_shift : 0;
                int sv = (j == 1 || j == 2) ? f->chroma_v_shift : 0;
                dst[j] = p->data[j] + p->linesize[j] *
                         (sc->slice_y >> sv) + ((sc->slice_x >> sh) << pixshift);
                src[j] = f->last_picture.f->data[j] + f->last_picture.f->linesize[j] *
                         (sc->slice_y >> sv) + ((sc->slice_x >> sh) << pixshift);

            }

            av_image_copy(dst, p->linesize, src,
                          f->last_picture.f->linesize,
                          avctx->pix_fmt,
                          sc->slice_width,
                          sc->slice_height);

            f->slice_damaged[i] = 1;
        }
    }
    ff_progress_frame_report(&f->picture, INT_MAX);

    ff_progress_frame_unref(&f->last_picture);
    if ((ret = av_frame_ref(rframe, f->picture.f)) < 0)
        return ret;

    *got_frame = 1;

    return buf_size;
}

#if HAVE_THREADS
static int update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    FFV1Context *fsrc = src->priv_data;
    FFV1Context *fdst = dst->priv_data;

    if (dst == src)
        return 0;

    fdst->version             = fsrc->version;
    fdst->micro_version       = fsrc->micro_version;
    fdst->chroma_planes       = fsrc->chroma_planes;
    fdst->chroma_h_shift      = fsrc->chroma_h_shift;
    fdst->chroma_v_shift      = fsrc->chroma_v_shift;
    fdst->transparency        = fsrc->transparency;
    fdst->plane_count         = fsrc->plane_count;
    fdst->ac                  = fsrc->ac;
    fdst->colorspace          = fsrc->colorspace;

    fdst->ec                  = fsrc->ec;
    fdst->intra               = fsrc->intra;
    fdst->key_frame_ok        = fsrc->key_frame_ok;

    fdst->packed_at_lsb       = fsrc->packed_at_lsb;
    fdst->slice_count         = fsrc->slice_count;
    fdst->use32bit     = fsrc->use32bit;
    memcpy(fdst->state_transition, fsrc->state_transition,
           sizeof(fdst->state_transition));

    // in version 1 there is a single per-keyframe quant table, so
    // we need to propagate it between threads
    if (fsrc->version < 2)
        memcpy(fdst->quant_tables[0], fsrc->quant_tables[0], sizeof(fsrc->quant_tables[0]));

    for (int i = 0; i < fdst->num_h_slices * fdst->num_v_slices; i++) {
        FFV1SliceContext       *sc  = &fdst->slices[i];
        const FFV1SliceContext *sc0 = &fsrc->slices[i];

        ff_refstruct_replace(&sc->plane, sc0->plane);

        if (fsrc->version < 3) {
            sc->slice_x             = sc0->slice_x;
            sc->slice_y             = sc0->slice_y;
            sc->slice_width         = sc0->slice_width;
            sc->slice_height        = sc0->slice_height;
        }
    }

    ff_refstruct_replace(&fdst->slice_damaged, fsrc->slice_damaged);

    av_assert1(fdst->max_slice_count == fsrc->max_slice_count);

    ff_progress_frame_replace(&fdst->picture, &fsrc->picture);

    return 0;
}
#endif

static av_cold int ffv1_decode_close(AVCodecContext *avctx)
{
    FFV1Context *const s = avctx->priv_data;

    ff_progress_frame_unref(&s->picture);
    ff_progress_frame_unref(&s->last_picture);

    return ff_ffv1_close(avctx);
}

const FFCodec ff_ffv1_decoder = {
    .p.name         = "ffv1",
    CODEC_LONG_NAME("FFmpeg video codec #1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_FFV1,
    .priv_data_size = sizeof(FFV1Context),
    .init           = decode_init,
    .close          = ffv1_decode_close,
    FF_CODEC_DECODE_CB(decode_frame),
    UPDATE_THREAD_CONTEXT(update_thread_context),
    .p.capabilities = AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_USES_PROGRESSFRAMES,
};
