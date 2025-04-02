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
#include "libavutil/refstruct.h"
#include "thread.h"
#include "decode.h"
#include "hwconfig.h"
#include "hwaccel_internal.h"
#include "config_components.h"

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
                        int remap_index, int pixel_stride, int ac)
{
    int x, y;
    int16_t *sample[2];
    int bits;
    unsigned mask;

    if (sc->remap) {
        bits = av_ceil_log2(sc->remap_count[remap_index]);
        mask = (1<<bits)-1;
    } else {
        bits = f->avctx->bits_per_raw_sample;
    }

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
            if (sc->remap)
                for (x = 0; x < w; x++)
                    sample[1][x] = sc->fltmap[remap_index][sample[1][x]];
            for (x = 0; x < w; x++)
                src[x*pixel_stride + stride * y] = sample[1][x];
        } else {
            int ret = decode_line(f, sc, gb, w, sample, plane_index, bits, ac);
            if (ret < 0)
                return ret;

            if (sc->remap) {
                if (f->packed_at_lsb || f->avctx->bits_per_raw_sample == 16) {
                    for (x = 0; x < w; x++) {
                        ((uint16_t*)(src + stride*y))[x*pixel_stride] = sc->fltmap[remap_index][sample[1][x] & mask];
                    }
                } else {
                    for (x = 0; x < w; x++) {
                        int v = sc->fltmap[remap_index][sample[1][x] & mask];
                        ((uint16_t*)(src + stride*y))[x*pixel_stride] = v << (16 - f->avctx->bits_per_raw_sample) | v >> (2 * f->avctx->bits_per_raw_sample - 16);
                    }
                }
            } else {
                if (f->packed_at_lsb || f->avctx->bits_per_raw_sample == 16) {
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
    sx = ff_ffv1_get_symbol(c, state, 0);
    sy = ff_ffv1_get_symbol(c, state, 0);
    sw = ff_ffv1_get_symbol(c, state, 0) + 1U;
    sh = ff_ffv1_get_symbol(c, state, 0) + 1U;

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
        int idx = ff_ffv1_get_symbol(c, state, 0);
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

    ps = ff_ffv1_get_symbol(c, state, 0);
    if (ps == 1) {
        frame->flags |= AV_FRAME_FLAG_INTERLACED;
        frame->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    } else if (ps == 2) {
        frame->flags |= AV_FRAME_FLAG_INTERLACED;
        frame->flags &= ~AV_FRAME_FLAG_TOP_FIELD_FIRST;
    } else if (ps == 3) {
        frame->flags &= ~AV_FRAME_FLAG_INTERLACED;
    }
    frame->sample_aspect_ratio.num = ff_ffv1_get_symbol(c, state, 0);
    frame->sample_aspect_ratio.den = ff_ffv1_get_symbol(c, state, 0);

    if (av_image_check_sar(f->width, f->height,
                           frame->sample_aspect_ratio) < 0) {
        av_log(f->avctx, AV_LOG_WARNING, "ignoring invalid SAR: %u/%u\n",
               frame->sample_aspect_ratio.num,
               frame->sample_aspect_ratio.den);
        frame->sample_aspect_ratio = (AVRational){ 0, 1 };
    }

    if (f->version > 3) {
        sc->slice_reset_contexts = get_rac(c, state);
        sc->slice_coding_mode = ff_ffv1_get_symbol(c, state, 0);
        if (sc->slice_coding_mode != 1 && f->colorspace == 1) {
            sc->slice_rct_by_coef = ff_ffv1_get_symbol(c, state, 0);
            sc->slice_rct_ry_coef = ff_ffv1_get_symbol(c, state, 0);
            if ((uint64_t)sc->slice_rct_by_coef + (uint64_t)sc->slice_rct_ry_coef > 4) {
                av_log(f->avctx, AV_LOG_ERROR, "slice_rct_y_coef out of range\n");
                return AVERROR_INVALIDDATA;
            }
        }
        if (f->combined_version >= 0x40004) {
            sc->remap = ff_ffv1_get_symbol(c, state, 0);
            if (sc->remap > 2U ||
                sc->remap && !f->flt) {
                av_log(f->avctx, AV_LOG_ERROR, "unsupported remap %d\n", sc->remap);
                return AVERROR_INVALIDDATA;
            }
        }
    }
    if (f->avctx->bits_per_raw_sample == 32) {
        if (!sc->remap) {
            av_log(f->avctx, AV_LOG_ERROR, "unsupported remap\n");
            return AVERROR_INVALIDDATA;
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

static int decode_current_mul(RangeCoder *rc, uint8_t state[32], int *mul, int mul_count, int64_t i)
{
    int ndx = (i * mul_count) >> 32;
    av_assert2(ndx <= 4096U);

    if (mul[ndx] < 0)
        mul[ndx] = ff_ffv1_get_symbol(rc, state, 0) & 0x3FFFFFFF;

    return mul[ndx];
}

static int decode_remap(FFV1Context *f, FFV1SliceContext *sc)
{
    unsigned int end = (1LL<<f->avctx->bits_per_raw_sample) - 1;
    int flip = sc->remap == 2 ? (end>>1) : 0;
    const int pixel_num = sc->slice_width * sc->slice_height;

    for (int p= 0; p < 1 + 2*f->chroma_planes + f->transparency; p++) {
        int j = 0;
        int lu = 0;
        uint8_t state[2][3][32];
        int64_t i;
        int mul[4096+1];
        int mul_count;

        memset(state, 128, sizeof(state));
        mul_count = ff_ffv1_get_symbol(&sc->c, state[0][0], 0);

        if (mul_count > 4096U)
            return AVERROR_INVALIDDATA;
        for (int i = 0; i<mul_count; i++) {
            mul[i] = -1;

        }
        mul[mul_count] = 1;

        memset(state, 128, sizeof(state));
        int current_mul = 1;
        for (i=0; i <= end ;) {
            unsigned run = get_symbol_inline(&sc->c, state[lu][0], 0);
            unsigned run0 = lu ? 0   : run;
            unsigned run1 = lu ? run : 1;

            i += run0 * current_mul;

            while (run1--) {
                if (current_mul > 1) {
                    int delta = get_symbol_inline(&sc->c, state[lu][1], 1);
                    if (delta <= -current_mul || delta > current_mul/2)
                        return AVERROR_INVALIDDATA; //not sure we should check this
                    i += current_mul - 1 + delta;
                }
                if (i - 1 >= end)
                    break;
                if (j >= pixel_num)
                    return AVERROR_INVALIDDATA;
                if (end <= 0xFFFF) {
                    sc->fltmap  [p][j++] = i ^ ((i&    0x8000) ? 0 : flip);
                } else
                    sc->fltmap32[p][j++] = i ^ ((i&0x80000000) ? 0 : flip);
                i++;
                current_mul = decode_current_mul(&sc->c, state[0][2], mul, mul_count, i);
            }
            if (lu) {
                i += current_mul;
            }
            lu ^= !run;
        }
        sc->remap_count[p] = j;
    }
    return 0;
}

static int decode_slice(AVCodecContext *c, void *arg)
{
    FFV1Context *f    = c->priv_data;
    FFV1SliceContext *sc = arg;
    int width, height, x, y, ret;
    const int ps      = av_pix_fmt_desc_get(f->pix_fmt)->comp[0].step;
    AVFrame * const p = f->picture.f;
    const int      si = sc - f->slices;
    GetBitContext gb;
    int ac = f->ac || sc->slice_coding_mode == 1;

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

    if (sc->remap) {
        const int pixel_num = sc->slice_width * sc->slice_height;

        for(int p = 0; p < 1 + 2*f->chroma_planes + f->transparency ; p++) {
            if (f->avctx->bits_per_raw_sample == 32) {
                av_fast_malloc(&sc->fltmap32[p], &sc->fltmap32_size[p], pixel_num * sizeof(*sc->fltmap32[p]));
                if (!sc->fltmap32[p])
                    return AVERROR(ENOMEM);
            } else {
                av_fast_malloc(&sc->fltmap[p], &sc->fltmap_size[p], pixel_num * sizeof(*sc->fltmap[p]));
                if (!sc->fltmap[p])
                    return AVERROR(ENOMEM);
            }
        }

        ret = decode_remap(f, sc);
        if (ret < 0)
            return ret;
    }

    if (ac == AC_GOLOMB_RICE) {
        if (f->combined_version >= 0x30002)
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
        decode_plane(f, sc, &gb, p->data[0] + ps*x + y*p->linesize[0], width, height, p->linesize[0], 0, 0, 1, ac);

        if (f->chroma_planes) {
            decode_plane(f, sc, &gb, p->data[1] + ps*cx+cy*p->linesize[1], chroma_width, chroma_height, p->linesize[1], 1, 1, 1, ac);
            decode_plane(f, sc, &gb, p->data[2] + ps*cx+cy*p->linesize[2], chroma_width, chroma_height, p->linesize[2], 1, 2, 1, ac);
        }
        if (f->transparency)
            decode_plane(f, sc, &gb, p->data[3] + ps*x + y*p->linesize[3], width, height, p->linesize[3], (f->version >= 4 && !f->chroma_planes) ? 1 : 2,
                                                                                                          (f->version >= 4 && !f->chroma_planes) ? 1 : 3, 1, ac);
    } else if (f->colorspace == 0) {
         decode_plane(f, sc, &gb, p->data[0] + ps*x + y*p->linesize[0]          , width, height, p->linesize[0], 0, 0, 2, ac);
         decode_plane(f, sc, &gb, p->data[0] + ps*x + y*p->linesize[0] + (ps>>1), width, height, p->linesize[0], 1, 1, 2, ac);
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
    if (ac != AC_GOLOMB_RICE && f->version > 2) {
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

static enum AVPixelFormat get_pixel_format(FFV1Context *f)
{
    enum AVPixelFormat pix_fmts[] = {
#if CONFIG_FFV1_VULKAN_HWACCEL
        AV_PIX_FMT_VULKAN,
#endif
        f->pix_fmt,
        AV_PIX_FMT_NONE,
    };

    return ff_get_format(f->avctx, pix_fmts);
}

static int read_header(FFV1Context *f, RangeCoder *c)
{
    uint8_t state[CONTEXT_SIZE];
    int context_count = -1; //-1 to avoid warning
    int ret;

    memset(state, 128, sizeof(state));

    ret = ff_ffv1_parse_header(f, c, state);
    if (ret < 0)
        return ret;

    if (f->configured_pix_fmt != f->pix_fmt) {
        f->avctx->pix_fmt = get_pixel_format(f);
        if (f->avctx->pix_fmt < 0)
            return AVERROR(EINVAL);
        f->configured_pix_fmt = f->pix_fmt;
    }

    ff_dlog(f->avctx, "%d %d %d\n",
            f->chroma_h_shift, f->chroma_v_shift, f->pix_fmt);
    if (f->version < 2) {
        context_count = ff_ffv1_read_quant_tables(c, f->quant_tables[0]);
        if (context_count < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return AVERROR_INVALIDDATA;
        }
        f->slice_count = f->max_slice_count;
    } else if (f->version < 3) {
        f->slice_count = ff_ffv1_get_symbol(c, state, 0);
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

    av_refstruct_unref(&f->slice_damaged);
    f->slice_damaged = av_refstruct_allocz(f->slice_count * sizeof(*f->slice_damaged));
    if (!f->slice_damaged)
        return AVERROR(ENOMEM);

    for (int j = 0; j < f->slice_count; j++) {
        FFV1SliceContext *sc = &f->slices[j];

        if (f->version == 2) {
            int sx = ff_ffv1_get_symbol(c, state, 0);
            int sy = ff_ffv1_get_symbol(c, state, 0);
            int sw = ff_ffv1_get_symbol(c, state, 0) + 1U;
            int sh = ff_ffv1_get_symbol(c, state, 0) + 1U;

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

        av_refstruct_unref(&sc->plane);
        sc->plane = ff_ffv1_planes_alloc();
        if (!sc->plane)
            return AVERROR(ENOMEM);

        for (int i = 0; i < f->plane_count; i++) {
            PlaneContext *const p = &sc->plane[i];

            if (f->version == 2) {
                int idx = ff_ffv1_get_symbol(c, state, 0);
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

    f->pix_fmt = AV_PIX_FMT_NONE;
    f->configured_pix_fmt = AV_PIX_FMT_NONE;

    if ((ret = ff_ffv1_common_init(avctx, f)) < 0)
        return ret;

    if (avctx->extradata_size > 0 && (ret = ff_ffv1_read_extra_header(f)) < 0)
        return ret;

    if ((ret = ff_ffv1_init_slice_contexts(f)) < 0)
        return ret;

    return 0;
}

static int find_next_slice(AVCodecContext *avctx,
                           uint8_t *buf, uint8_t *buf_end, int idx,
                           uint8_t **pos, uint32_t *len)
{
    FFV1Context *f = avctx->priv_data;

    /* Length field */
    uint32_t v = buf_end - buf;
    if (idx || f->version > 2) {
        /* Three bytes of length, plus flush bit + CRC */
        uint32_t trailer = 3 + 5*!!f->ec;
        if (trailer > buf_end - buf)
            v = INT_MAX;
        else
            v = AV_RB24(buf_end - trailer) + trailer;
    }

    if (buf_end - buf < v) {
        av_log(avctx, AV_LOG_ERROR, "Slice pointer chain broken\n");
        ff_progress_frame_report(&f->picture, INT_MAX);
        return AVERROR_INVALIDDATA;
    }

    *len = v;
    if (idx)
        *pos = buf_end - v;
    else
        *pos = buf;

    return 0;
}

static int decode_header(AVCodecContext *avctx, RangeCoder *c,
                         uint8_t *buf, size_t buf_size)
{
    int ret;
    FFV1Context *f = avctx->priv_data;

    uint8_t keystate = 128;
    ff_init_range_decoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05 * (1LL << 32), 256 - 8);

    if (get_rac(c, &keystate)) {
        f->key_frame = AV_FRAME_FLAG_KEY;
        f->key_frame_ok = 0;
        if ((ret = read_header(f, c)) < 0)
            return ret;
        f->key_frame_ok = 1;
    } else {
        if (!f->key_frame_ok) {
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot decode non-keyframe without valid keyframe\n");
            return AVERROR_INVALIDDATA;
        }
        f->key_frame = 0;
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

    return 0;
}

static int decode_slices(AVCodecContext *avctx, RangeCoder c,
                         AVPacket *avpkt)
{
    FFV1Context *f = avctx->priv_data;
    AVFrame *p = f->picture.f;

    uint8_t *buf = avpkt->data;
    size_t buf_size = avpkt->size;
    uint8_t *buf_end = buf + buf_size;

    for (int i = f->slice_count - 1; i >= 0; i--) {
        FFV1SliceContext *sc = &f->slices[i];

        uint8_t *pos;
        uint32_t len;
        int err = find_next_slice(avctx, buf, buf_end, i,
                                  &pos, &len);
        if (err < 0)
            return err;

        buf_end -= len;

        sc->slice_damaged = 0;

        if (f->ec) {
            unsigned crc = av_crc(av_crc_get_table(AV_CRC_32_IEEE), f->crcref, pos, len);
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
                av_log(avctx, AV_LOG_DEBUG, "slice %d, CRC: 0x%08"PRIX32"\n", i, AV_RB32(pos + len - 4));
            }
        }

        if (i) {
            ff_init_range_decoder(&sc->c, pos, len);
            ff_build_rac_states(&sc->c, 0.05 * (1LL << 32), 256 - 8);
        } else {
            sc->c = c;
            sc->c.bytestream_end = pos + len;
        }
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
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(f->pix_fmt);
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
                          f->pix_fmt,
                          sc->slice_width,
                          sc->slice_height);

            f->slice_damaged[i] = 1;
        }
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                        int *got_frame, AVPacket *avpkt)
{
    FFV1Context *f      = avctx->priv_data;
    int ret;
    AVFrame *p;
    const FFHWAccel *hwaccel = NULL;

    /* This is copied onto the first slice's range coder context */
    RangeCoder c;

    ff_progress_frame_unref(&f->last_picture);
    av_refstruct_unref(&f->hwaccel_last_picture_private);

    FFSWAP(ProgressFrame, f->picture, f->last_picture);
    FFSWAP(void *, f->hwaccel_picture_private, f->hwaccel_last_picture_private);

    f->avctx = avctx;
    f->frame_damaged = 0;

    ret = decode_header(avctx, &c, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_DEBUG, "ver:%d keyframe:%d coder:%d ec:%d slices:%d bps:%d\n",
               f->version, !!f->key_frame, f->ac, f->ec, f->slice_count, f->avctx->bits_per_raw_sample);

    if (avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    if (avctx->hwaccel)
        hwaccel = ffhwaccel(avctx->hwaccel);

    ret = ff_progress_frame_get_buffer(avctx, &f->picture,
                                       AV_GET_BUFFER_FLAG_REF);
    if (ret < 0)
        return ret;

    ret = ff_hwaccel_frame_priv_alloc(avctx, &f->hwaccel_picture_private);
    if (ret < 0)
        return ret;

    p = f->picture.f;

    p->pict_type = AV_PICTURE_TYPE_I; //FIXME I vs. P
    p->flags     = (p->flags & ~AV_FRAME_FLAG_KEY) | f->key_frame;

    if (f->version < 3 && avctx->field_order > AV_FIELD_PROGRESSIVE) {
        /* we have interlaced material flagged in container */
        p->flags |= AV_FRAME_FLAG_INTERLACED;
        if (avctx->field_order == AV_FIELD_TT || avctx->field_order == AV_FIELD_TB)
            p->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    }

    /* Start */
    if (hwaccel) {
        ret = hwaccel->start_frame(avctx, avpkt->buf, avpkt->data, avpkt->size);
        if (ret < 0)
            return ret;
    }

    ff_thread_finish_setup(avctx);

    /* Decode slices */
    if (hwaccel) {
        uint8_t *buf_end = avpkt->data + avpkt->size;

        if (!(p->flags & AV_FRAME_FLAG_KEY) && f->last_picture.f)
            ff_progress_frame_await(&f->last_picture, f->slice_count - 1);

        for (int i = f->slice_count - 1; i >= 0; i--) {
            uint8_t *pos;
            uint32_t len;
            ret = find_next_slice(avctx, avpkt->data, buf_end, i,
                                  &pos, &len);
            if (ret < 0)
                return ret;

            buf_end -= len;

            ret = hwaccel->decode_slice(avctx, pos, len);
            if (ret < 0)
                return ret;
        }
    } else {
        ret = decode_slices(avctx, c, avpkt);
        if (ret < 0)
            return ret;
    }

    /* Finalize */
    if (hwaccel) {
        ret = hwaccel->end_frame(avctx);
        if (ret < 0)
            return ret;
    }

    ff_progress_frame_report(&f->picture, INT_MAX);

    ff_progress_frame_unref(&f->last_picture);
    av_refstruct_unref(&f->hwaccel_last_picture_private);
    if ((ret = av_frame_ref(rframe, f->picture.f)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
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
    fdst->combined_version    = fsrc->combined_version;
    fdst->chroma_planes       = fsrc->chroma_planes;
    fdst->chroma_h_shift      = fsrc->chroma_h_shift;
    fdst->chroma_v_shift      = fsrc->chroma_v_shift;
    fdst->transparency        = fsrc->transparency;
    fdst->plane_count         = fsrc->plane_count;
    fdst->ac                  = fsrc->ac;
    fdst->colorspace          = fsrc->colorspace;
    fdst->pix_fmt             = fsrc->pix_fmt;
    fdst->configured_pix_fmt  = fsrc->configured_pix_fmt;

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

        av_refstruct_replace(&sc->plane, sc0->plane);

        if (fsrc->version < 3) {
            sc->slice_x             = sc0->slice_x;
            sc->slice_y             = sc0->slice_y;
            sc->slice_width         = sc0->slice_width;
            sc->slice_height        = sc0->slice_height;
        }
    }

    av_refstruct_replace(&fdst->slice_damaged, fsrc->slice_damaged);

    av_assert1(fdst->max_slice_count == fsrc->max_slice_count);

    ff_progress_frame_replace(&fdst->picture, &fsrc->picture);
    av_refstruct_replace(&fdst->hwaccel_picture_private,
                         fsrc->hwaccel_picture_private);

    return 0;
}
#endif

static av_cold int ffv1_decode_close(AVCodecContext *avctx)
{
    FFV1Context *const s = avctx->priv_data;

    ff_progress_frame_unref(&s->picture);
    av_refstruct_unref(&s->hwaccel_picture_private);

    ff_progress_frame_unref(&s->last_picture);
    av_refstruct_unref(&s->hwaccel_last_picture_private);

    ff_ffv1_close(s);

    return 0;
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
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM |
                      FF_CODEC_CAP_USES_PROGRESSFRAMES,
    .hw_configs     = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_FFV1_VULKAN_HWACCEL
        HWACCEL_VULKAN(ffv1),
#endif
        NULL
    },
};
