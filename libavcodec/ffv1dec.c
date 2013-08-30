/*
 * FFV1 decoder
 *
 * Copyright (c) 2003-2012 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/timer.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "rangecoder.h"
#include "golomb.h"
#include "mathops.h"
#include "ffv1.h"

static inline av_flatten int get_symbol_inline(RangeCoder *c, uint8_t *state,
                                               int is_signed)
{
    if (get_rac(c, state + 0))
        return 0;
    else {
        int i, e, a;
        e = 0;
        while (get_rac(c, state + 1 + FFMIN(e, 9))) // 1..10
            e++;

        a = 1;
        for (i = e - 1; i >= 0; i--)
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
    av_dlog(NULL, "v:%d bias:%d error:%d drift:%d count:%d k:%d",
            v, state->bias, state->error_sum, state->drift, state->count, k);

#if 0 // JPEG LS
    if (k == 0 && 2 * state->drift <= -state->count)
        v ^= (-1);
#else
    v ^= ((2 * state->drift + state->count) >> 31);
#endif

    ret = fold(v + state->bias, bits);

    update_vlc_state(state, v);

    return ret;
}

static av_always_inline void decode_line(FFV1Context *s, int w,
                                         int16_t *sample[2],
                                         int plane_index, int bits)
{
    PlaneContext *const p = &s->plane[plane_index];
    RangeCoder *const c   = &s->c;
    int x;
    int run_count = 0;
    int run_mode  = 0;
    int run_index = s->run_index;

    for (x = 0; x < w; x++) {
        int diff, context, sign;

        context = get_context(p, sample[1] + x, sample[0] + x, sample[1] + x);
        if (context < 0) {
            context = -context;
            sign    = 1;
        } else
            sign = 0;

        av_assert2(context < p->context_count);

        if (s->ac) {
            diff = get_symbol_inline(c, p->state[context], 1);
        } else {
            if (context == 0 && run_mode == 0)
                run_mode = 1;

            if (run_mode) {
                if (run_count == 0 && run_mode == 1) {
                    if (get_bits1(&s->gb)) {
                        run_count = 1 << ff_log2_run[run_index];
                        if (x + run_count <= w)
                            run_index++;
                    } else {
                        if (ff_log2_run[run_index])
                            run_count = get_bits(&s->gb, ff_log2_run[run_index]);
                        else
                            run_count = 0;
                        if (run_index)
                            run_index--;
                        run_mode = 2;
                    }
                }
                run_count--;
                if (run_count < 0) {
                    run_mode  = 0;
                    run_count = 0;
                    diff      = get_vlc_symbol(&s->gb, &p->vlc_state[context],
                                               bits);
                    if (diff >= 0)
                        diff++;
                } else
                    diff = 0;
            } else
                diff = get_vlc_symbol(&s->gb, &p->vlc_state[context], bits);

            av_dlog(s->avctx, "count:%d index:%d, mode:%d, x:%d pos:%d\n",
                    run_count, run_index, run_mode, x, get_bits_count(&s->gb));
        }

        if (sign)
            diff = -diff;

        sample[1][x] = (predict(sample[1] + x, sample[0] + x) + diff) &
                       ((1 << bits) - 1);
    }
    s->run_index = run_index;
}

static void decode_plane(FFV1Context *s, uint8_t *src,
                         int w, int h, int stride, int plane_index)
{
    int x, y;
    int16_t *sample[2];
    sample[0] = s->sample_buffer + 3;
    sample[1] = s->sample_buffer + w + 6 + 3;

    s->run_index = 0;

    memset(s->sample_buffer, 0, 2 * (w + 6) * sizeof(*s->sample_buffer));

    for (y = 0; y < h; y++) {
        int16_t *temp = sample[0]; // FIXME: try a normal buffer

        sample[0] = sample[1];
        sample[1] = temp;

        sample[1][-1] = sample[0][0];
        sample[0][w]  = sample[0][w - 1];

// { START_TIMER
        if (s->avctx->bits_per_raw_sample <= 8) {
            decode_line(s, w, sample, plane_index, 8);
            for (x = 0; x < w; x++)
                src[x + stride * y] = sample[1][x];
        } else {
            decode_line(s, w, sample, plane_index, s->avctx->bits_per_raw_sample);
            if (s->packed_at_lsb) {
                for (x = 0; x < w; x++) {
                    ((uint16_t*)(src + stride*y))[x] = sample[1][x];
                }
            } else {
                for (x = 0; x < w; x++) {
                    ((uint16_t*)(src + stride*y))[x] = sample[1][x] << (16 - s->avctx->bits_per_raw_sample);
                }
            }
        }
// STOP_TIMER("decode-line") }
    }
}

static void decode_rgb_frame(FFV1Context *s, uint8_t *src[3], int w, int h, int stride[3])
{
    int x, y, p;
    int16_t *sample[4][2];
    int lbd    = s->avctx->bits_per_raw_sample <= 8;
    int bits   = s->avctx->bits_per_raw_sample > 0 ? s->avctx->bits_per_raw_sample : 8;
    int offset = 1 << bits;

    for (x = 0; x < 4; x++) {
        sample[x][0] = s->sample_buffer +  x * 2      * (w + 6) + 3;
        sample[x][1] = s->sample_buffer + (x * 2 + 1) * (w + 6) + 3;
    }

    s->run_index = 0;

    memset(s->sample_buffer, 0, 8 * (w + 6) * sizeof(*s->sample_buffer));

    for (y = 0; y < h; y++) {
        for (p = 0; p < 3 + s->transparency; p++) {
            int16_t *temp = sample[p][0]; // FIXME: try a normal buffer

            sample[p][0] = sample[p][1];
            sample[p][1] = temp;

            sample[p][1][-1]= sample[p][0][0  ];
            sample[p][0][ w]= sample[p][0][w-1];
            if (lbd)
                decode_line(s, w, sample[p], (p + 1)/2, 9);
            else
                decode_line(s, w, sample[p], (p + 1)/2, bits + 1);
        }
        for (x = 0; x < w; x++) {
            int g = sample[0][1][x];
            int b = sample[1][1][x];
            int r = sample[2][1][x];
            int a = sample[3][1][x];

            b -= offset;
            r -= offset;
            g -= (b + r) >> 2;
            b += g;
            r += g;

            if (lbd)
                *((uint32_t*)(src[0] + x*4 + stride[0]*y)) = b + (g<<8) + (r<<16) + (a<<24);
            else {
                *((uint16_t*)(src[0] + x*2 + stride[0]*y)) = b;
                *((uint16_t*)(src[1] + x*2 + stride[1]*y)) = g;
                *((uint16_t*)(src[2] + x*2 + stride[2]*y)) = r;
            }
        }
    }
}

static int decode_slice_header(FFV1Context *f, FFV1Context *fs)
{
    RangeCoder *c = &fs->c;
    uint8_t state[CONTEXT_SIZE];
    unsigned ps, i, context_count;
    memset(state, 128, sizeof(state));

    av_assert0(f->version > 2);

    fs->slice_x      =  get_symbol(c, state, 0)      * f->width ;
    fs->slice_y      =  get_symbol(c, state, 0)      * f->height;
    fs->slice_width  = (get_symbol(c, state, 0) + 1) * f->width  + fs->slice_x;
    fs->slice_height = (get_symbol(c, state, 0) + 1) * f->height + fs->slice_y;

    fs->slice_x /= f->num_h_slices;
    fs->slice_y /= f->num_v_slices;
    fs->slice_width  = fs->slice_width /f->num_h_slices - fs->slice_x;
    fs->slice_height = fs->slice_height/f->num_v_slices - fs->slice_y;
    if ((unsigned)fs->slice_width > f->width || (unsigned)fs->slice_height > f->height)
        return -1;
    if (    (unsigned)fs->slice_x + (uint64_t)fs->slice_width  > f->width
         || (unsigned)fs->slice_y + (uint64_t)fs->slice_height > f->height)
        return -1;

    for (i = 0; i < f->plane_count; i++) {
        PlaneContext * const p = &fs->plane[i];
        int idx = get_symbol(c, state, 0);
        if (idx > (unsigned)f->quant_table_count) {
            av_log(f->avctx, AV_LOG_ERROR, "quant_table_index out of range\n");
            return -1;
        }
        p->quant_table_index = idx;
        memcpy(p->quant_table, f->quant_tables[idx], sizeof(p->quant_table));
        context_count = f->context_count[idx];

        if (p->context_count < context_count) {
            av_freep(&p->state);
            av_freep(&p->vlc_state);
        }
        p->context_count = context_count;
    }

    ps = get_symbol(c, state, 0);
    if (ps == 1) {
        f->cur->interlaced_frame = 1;
        f->cur->top_field_first  = 1;
    } else if (ps == 2) {
        f->cur->interlaced_frame = 1;
        f->cur->top_field_first  = 0;
    } else if (ps == 3) {
        f->cur->interlaced_frame = 0;
    }
    f->cur->sample_aspect_ratio.num = get_symbol(c, state, 0);
    f->cur->sample_aspect_ratio.den = get_symbol(c, state, 0);

    return 0;
}

static int decode_slice(AVCodecContext *c, void *arg)
{
    FFV1Context *fs   = *(void **)arg;
    FFV1Context *f    = fs->avctx->priv_data;
    int width, height, x, y, ret;
    const int ps      = av_pix_fmt_desc_get(c->pix_fmt)->comp[0].step_minus1 + 1;
    AVFrame * const p = f->cur;
    int i, si;

    for( si=0; fs != f->slice_context[si]; si ++)
        ;

    if(f->fsrc && !p->key_frame)
        ff_thread_await_progress(&f->last_picture, si, 0);

    if(f->fsrc && !p->key_frame) {
        FFV1Context *fssrc = f->fsrc->slice_context[si];
        FFV1Context *fsdst = f->slice_context[si];
        av_assert1(fsdst->plane_count == fssrc->plane_count);
        av_assert1(fsdst == fs);

        if (!p->key_frame)
            fsdst->slice_damaged |= fssrc->slice_damaged;

        for (i = 0; i < f->plane_count; i++) {
            PlaneContext *psrc = &fssrc->plane[i];
            PlaneContext *pdst = &fsdst->plane[i];

            av_free(pdst->state);
            av_free(pdst->vlc_state);
            memcpy(pdst, psrc, sizeof(*pdst));
            pdst->state = NULL;
            pdst->vlc_state = NULL;

            if (fssrc->ac) {
                pdst->state = av_malloc(CONTEXT_SIZE * psrc->context_count);
                memcpy(pdst->state, psrc->state, CONTEXT_SIZE * psrc->context_count);
            } else {
                pdst->vlc_state = av_malloc(sizeof(*pdst->vlc_state) * psrc->context_count);
                memcpy(pdst->vlc_state, psrc->vlc_state, sizeof(*pdst->vlc_state) * psrc->context_count);
            }
        }
    }

    if (f->version > 2) {
        if (ffv1_init_slice_state(f, fs) < 0)
            return AVERROR(ENOMEM);
        if (decode_slice_header(f, fs) < 0) {
            fs->slice_damaged = 1;
            return AVERROR_INVALIDDATA;
        }
    }
    if ((ret = ffv1_init_slice_state(f, fs)) < 0)
        return ret;
    if (f->cur->key_frame)
        ffv1_clear_slice_state(f, fs);

    width  = fs->slice_width;
    height = fs->slice_height;
    x      = fs->slice_x;
    y      = fs->slice_y;

    if (!fs->ac) {
        if (f->version == 3 && f->minor_version > 1 || f->version > 3)
            get_rac(&fs->c, (uint8_t[]) { 129 });
        fs->ac_byte_count = f->version > 2 || (!x && !y) ? fs->c.bytestream - fs->c.bytestream_start - 1 : 0;
        init_get_bits(&fs->gb,
                      fs->c.bytestream_start + fs->ac_byte_count,
                      (fs->c.bytestream_end - fs->c.bytestream_start - fs->ac_byte_count) * 8);
    }

    av_assert1(width && height);
    if (f->colorspace == 0) {
        const int chroma_width  = FF_CEIL_RSHIFT(width,  f->chroma_h_shift);
        const int chroma_height = FF_CEIL_RSHIFT(height, f->chroma_v_shift);
        const int cx            = x >> f->chroma_h_shift;
        const int cy            = y >> f->chroma_v_shift;
        decode_plane(fs, p->data[0] + ps*x + y*p->linesize[0], width, height, p->linesize[0], 0);

        if (f->chroma_planes) {
            decode_plane(fs, p->data[1] + ps*cx+cy*p->linesize[1], chroma_width, chroma_height, p->linesize[1], 1);
            decode_plane(fs, p->data[2] + ps*cx+cy*p->linesize[2], chroma_width, chroma_height, p->linesize[2], 1);
        }
        if (fs->transparency)
            decode_plane(fs, p->data[3] + ps*x + y*p->linesize[3], width, height, p->linesize[3], 2);
    } else {
        uint8_t *planes[3] = { p->data[0] + ps * x + y * p->linesize[0],
                               p->data[1] + ps * x + y * p->linesize[1],
                               p->data[2] + ps * x + y * p->linesize[2] };
        decode_rgb_frame(fs, planes, width, height, p->linesize);
    }
    if (fs->ac && f->version > 2) {
        int v;
        get_rac(&fs->c, (uint8_t[]) { 129 });
        v = fs->c.bytestream_end - fs->c.bytestream - 2 - 5*f->ec;
        if (v) {
            av_log(f->avctx, AV_LOG_ERROR, "bytestream end mismatching by %d\n", v);
            fs->slice_damaged = 1;
        }
    }

    emms_c();

    ff_thread_report_progress(&f->picture, si, 0);

    return 0;
}

static int read_quant_table(RangeCoder *c, int16_t *quant_table, int scale)
{
    int v;
    int i = 0;
    uint8_t state[CONTEXT_SIZE];

    memset(state, 128, sizeof(state));

    for (v = 0; i < 128; v++) {
        unsigned len = get_symbol(c, state, 0) + 1;

        if (len > 128 - i)
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
        context_count *= read_quant_table(c, quant_table[i], context_count);
        if (context_count > 32768U) {
            return AVERROR_INVALIDDATA;
        }
    }
    return (context_count + 1) / 2;
}

static int read_extra_header(FFV1Context *f)
{
    RangeCoder *const c = &f->c;
    uint8_t state[CONTEXT_SIZE];
    int i, j, k, ret;
    uint8_t state2[32][CONTEXT_SIZE];

    memset(state2, 128, sizeof(state2));
    memset(state, 128, sizeof(state));

    ff_init_range_decoder(c, f->avctx->extradata, f->avctx->extradata_size);
    ff_build_rac_states(c, 0.05 * (1LL << 32), 256 - 8);

    f->version = get_symbol(c, state, 0);
    if (f->version < 2) {
        av_log(f->avctx, AV_LOG_ERROR, "Invalid version in global header\n");
        return AVERROR_INVALIDDATA;
    }
    if (f->version > 2) {
        c->bytestream_end -= 4;
        f->minor_version = get_symbol(c, state, 0);
    }
    f->ac = f->avctx->coder_type = get_symbol(c, state, 0);
    if (f->ac > 1) {
        for (i = 1; i < 256; i++)
            f->state_transition[i] = get_symbol(c, state, 1) + c->one_state[i];
    }

    f->colorspace                 = get_symbol(c, state, 0); //YUV cs type
    f->avctx->bits_per_raw_sample = get_symbol(c, state, 0);
    f->chroma_planes              = get_rac(c, state);
    f->chroma_h_shift             = get_symbol(c, state, 0);
    f->chroma_v_shift             = get_symbol(c, state, 0);
    f->transparency               = get_rac(c, state);
    f->plane_count                = 2 + f->transparency;
    f->num_h_slices               = 1 + get_symbol(c, state, 0);
    f->num_v_slices               = 1 + get_symbol(c, state, 0);

    if (f->num_h_slices > (unsigned)f->width  || !f->num_h_slices ||
        f->num_v_slices > (unsigned)f->height || !f->num_v_slices
       ) {
        av_log(f->avctx, AV_LOG_ERROR, "slice count invalid\n");
        return AVERROR_INVALIDDATA;
    }

    f->quant_table_count = get_symbol(c, state, 0);
    if (f->quant_table_count > (unsigned)MAX_QUANT_TABLES)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < f->quant_table_count; i++) {
        f->context_count[i] = read_quant_tables(c, f->quant_tables[i]);
        if (f->context_count[i] < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return AVERROR_INVALIDDATA;
        }
    }
    if ((ret = ffv1_allocate_initial_states(f)) < 0)
        return ret;

    for (i = 0; i < f->quant_table_count; i++)
        if (get_rac(c, state)) {
            for (j = 0; j < f->context_count[i]; j++)
                for (k = 0; k < CONTEXT_SIZE; k++) {
                    int pred = j ? f->initial_states[i][j - 1][k] : 128;
                    f->initial_states[i][j][k] =
                        (pred + get_symbol(c, state2[k], 1)) & 0xFF;
                }
        }

    if (f->version > 2) {
        f->ec = get_symbol(c, state, 0);
        if (f->minor_version > 2)
            f->intra = get_symbol(c, state, 0);
    }

    if (f->version > 2) {
        unsigned v;
        v = av_crc(av_crc_get_table(AV_CRC_32_IEEE), 0,
                   f->avctx->extradata, f->avctx->extradata_size);
        if (v) {
            av_log(f->avctx, AV_LOG_ERROR, "CRC mismatch %X!\n", v);
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int read_header(FFV1Context *f)
{
    uint8_t state[CONTEXT_SIZE];
    int i, j, context_count = -1; //-1 to avoid warning
    RangeCoder *const c = &f->slice_context[0]->c;

    memset(state, 128, sizeof(state));

    if (f->version < 2) {
        int chroma_planes, chroma_h_shift, chroma_v_shift, transparency, colorspace, bits_per_raw_sample;
        unsigned v= get_symbol(c, state, 0);
        if (v >= 2) {
            av_log(f->avctx, AV_LOG_ERROR, "invalid version %d in ver01 header\n", v);
            return AVERROR_INVALIDDATA;
        }
        f->version = v;
        f->ac      = f->avctx->coder_type = get_symbol(c, state, 0);
        if (f->ac > 1) {
            for (i = 1; i < 256; i++)
                f->state_transition[i] = get_symbol(c, state, 1) + c->one_state[i];
        }

        colorspace     = get_symbol(c, state, 0); //YUV cs type
        bits_per_raw_sample = f->version > 0 ? get_symbol(c, state, 0) : f->avctx->bits_per_raw_sample;
        chroma_planes  = get_rac(c, state);
        chroma_h_shift = get_symbol(c, state, 0);
        chroma_v_shift = get_symbol(c, state, 0);
        transparency   = get_rac(c, state);

        if (f->plane_count) {
            if (   colorspace    != f->colorspace
                || bits_per_raw_sample != f->avctx->bits_per_raw_sample
                || chroma_planes != f->chroma_planes
                || chroma_h_shift!= f->chroma_h_shift
                || chroma_v_shift!= f->chroma_v_shift
                || transparency  != f->transparency) {
                av_log(f->avctx, AV_LOG_ERROR, "Invalid change of global parameters\n");
                return AVERROR_INVALIDDATA;
            }
        }

        f->colorspace     = colorspace;
        f->avctx->bits_per_raw_sample = bits_per_raw_sample;
        f->chroma_planes  = chroma_planes;
        f->chroma_h_shift = chroma_h_shift;
        f->chroma_v_shift = chroma_v_shift;
        f->transparency   = transparency;

        f->plane_count    = 2 + f->transparency;
    }

    if (f->colorspace == 0) {
        if (!f->transparency && !f->chroma_planes) {
            if (f->avctx->bits_per_raw_sample <= 8)
                f->avctx->pix_fmt = AV_PIX_FMT_GRAY8;
            else
                f->avctx->pix_fmt = AV_PIX_FMT_GRAY16;
        } else if (f->avctx->bits_per_raw_sample<=8 && !f->transparency) {
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P; break;
            case 0x01: f->avctx->pix_fmt = AV_PIX_FMT_YUV440P; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P; break;
            case 0x20: f->avctx->pix_fmt = AV_PIX_FMT_YUV411P; break;
            case 0x22: f->avctx->pix_fmt = AV_PIX_FMT_YUV410P; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return AVERROR(ENOSYS);
            }
        } else if (f->avctx->bits_per_raw_sample <= 8 && f->transparency) {
            switch(16*f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUVA444P; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUVA422P; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUVA420P; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return AVERROR(ENOSYS);
            }
        } else if (f->avctx->bits_per_raw_sample == 9) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P9; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P9; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P9; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return AVERROR(ENOSYS);
            }
        } else if (f->avctx->bits_per_raw_sample == 10) {
            f->packed_at_lsb = 1;
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P10; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P10; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P10; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return AVERROR(ENOSYS);
            }
        } else {
            switch(16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00: f->avctx->pix_fmt = AV_PIX_FMT_YUV444P16; break;
            case 0x10: f->avctx->pix_fmt = AV_PIX_FMT_YUV422P16; break;
            case 0x11: f->avctx->pix_fmt = AV_PIX_FMT_YUV420P16; break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return AVERROR(ENOSYS);
            }
        }
    } else if (f->colorspace == 1) {
        if (f->chroma_h_shift || f->chroma_v_shift) {
            av_log(f->avctx, AV_LOG_ERROR,
                   "chroma subsampling not supported in this colorspace\n");
            return AVERROR(ENOSYS);
        }
        if (     f->avctx->bits_per_raw_sample ==  9)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRP9;
        else if (f->avctx->bits_per_raw_sample == 10)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRP10;
        else if (f->avctx->bits_per_raw_sample == 12)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRP12;
        else if (f->avctx->bits_per_raw_sample == 14)
            f->avctx->pix_fmt = AV_PIX_FMT_GBRP14;
        else
        if (f->transparency) f->avctx->pix_fmt = AV_PIX_FMT_RGB32;
        else                 f->avctx->pix_fmt = AV_PIX_FMT_0RGB32;
    } else {
        av_log(f->avctx, AV_LOG_ERROR, "colorspace not supported\n");
        return AVERROR(ENOSYS);
    }

    av_dlog(f->avctx, "%d %d %d\n",
            f->chroma_h_shift, f->chroma_v_shift, f->avctx->pix_fmt);
    if (f->version < 2) {
        context_count = read_quant_tables(c, f->quant_table);
        if (context_count < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return AVERROR_INVALIDDATA;
        }
    } else if (f->version < 3) {
        f->slice_count = get_symbol(c, state, 0);
    } else {
        const uint8_t *p = c->bytestream_end;
        for (f->slice_count = 0;
             f->slice_count < MAX_SLICES && 3 < p - c->bytestream_start;
             f->slice_count++) {
            int trailer = 3 + 5*!!f->ec;
            int size = AV_RB24(p-trailer);
            if (size + trailer > p - c->bytestream_start)
                break;
            p -= size + trailer;
        }
    }
    if (f->slice_count > (unsigned)MAX_SLICES || f->slice_count <= 0) {
        av_log(f->avctx, AV_LOG_ERROR, "slice count %d is invalid\n", f->slice_count);
        return AVERROR_INVALIDDATA;
    }

    for (j = 0; j < f->slice_count; j++) {
        FFV1Context *fs = f->slice_context[j];
        fs->ac            = f->ac;
        fs->packed_at_lsb = f->packed_at_lsb;

        fs->slice_damaged = 0;

        if (f->version == 2) {
            fs->slice_x      =  get_symbol(c, state, 0)      * f->width ;
            fs->slice_y      =  get_symbol(c, state, 0)      * f->height;
            fs->slice_width  = (get_symbol(c, state, 0) + 1) * f->width  + fs->slice_x;
            fs->slice_height = (get_symbol(c, state, 0) + 1) * f->height + fs->slice_y;

            fs->slice_x     /= f->num_h_slices;
            fs->slice_y     /= f->num_v_slices;
            fs->slice_width  = fs->slice_width  / f->num_h_slices - fs->slice_x;
            fs->slice_height = fs->slice_height / f->num_v_slices - fs->slice_y;
            if ((unsigned)fs->slice_width  > f->width ||
                (unsigned)fs->slice_height > f->height)
                return AVERROR_INVALIDDATA;
            if (   (unsigned)fs->slice_x + (uint64_t)fs->slice_width  > f->width
                || (unsigned)fs->slice_y + (uint64_t)fs->slice_height > f->height)
                return AVERROR_INVALIDDATA;
        }

        for (i = 0; i < f->plane_count; i++) {
            PlaneContext *const p = &fs->plane[i];

            if (f->version == 2) {
                int idx = get_symbol(c, state, 0);
                if (idx > (unsigned)f->quant_table_count) {
                    av_log(f->avctx, AV_LOG_ERROR,
                           "quant_table_index out of range\n");
                    return AVERROR_INVALIDDATA;
                }
                p->quant_table_index = idx;
                memcpy(p->quant_table, f->quant_tables[idx],
                       sizeof(p->quant_table));
                context_count = f->context_count[idx];
            } else {
                memcpy(p->quant_table, f->quant_table, sizeof(p->quant_table));
            }

            if (f->version <= 2) {
                av_assert0(context_count >= 0);
                if (p->context_count < context_count) {
                    av_freep(&p->state);
                    av_freep(&p->vlc_state);
                }
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

    if ((ret = ffv1_common_init(avctx)) < 0)
        return ret;

    if (avctx->extradata && (ret = read_extra_header(f)) < 0)
        return ret;

    if ((ret = ffv1_init_slice_contexts(f)) < 0)
        return ret;

    avctx->internal->allocate_progress = 1;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf  = avpkt->data;
    int buf_size        = avpkt->size;
    FFV1Context *f      = avctx->priv_data;
    RangeCoder *const c = &f->slice_context[0]->c;
    int i, ret;
    uint8_t keystate = 128;
    const uint8_t *buf_p;
    AVFrame *p;

    if (f->last_picture.f)
        ff_thread_release_buffer(avctx, &f->last_picture);
    FFSWAP(ThreadFrame, f->picture, f->last_picture);

    f->cur = p = f->picture.f;

    f->avctx = avctx;
    ff_init_range_decoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05 * (1LL << 32), 256 - 8);

    p->pict_type = AV_PICTURE_TYPE_I; //FIXME I vs. P
    if (get_rac(c, &keystate)) {
        p->key_frame    = 1;
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
        p->key_frame = 0;
    }

    if ((ret = ff_thread_get_buffer(avctx, &f->picture, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_DEBUG, "ver:%d keyframe:%d coder:%d ec:%d slices:%d bps:%d\n",
               f->version, p->key_frame, f->ac, f->ec, f->slice_count, f->avctx->bits_per_raw_sample);

    ff_thread_finish_setup(avctx);

    buf_p = buf + buf_size;
    for (i = f->slice_count - 1; i >= 0; i--) {
        FFV1Context *fs = f->slice_context[i];
        int trailer = 3 + 5*!!f->ec;
        int v;

        if (i || f->version > 2) v = AV_RB24(buf_p-trailer) + trailer;
        else                     v = buf_p - c->bytestream_start;
        if (buf_p - c->bytestream_start < v) {
            av_log(avctx, AV_LOG_ERROR, "Slice pointer chain broken\n");
            return AVERROR_INVALIDDATA;
        }
        buf_p -= v;

        if (f->ec) {
            unsigned crc = av_crc(av_crc_get_table(AV_CRC_32_IEEE), 0, buf_p, v);
            if (crc) {
                int64_t ts = avpkt->pts != AV_NOPTS_VALUE ? avpkt->pts : avpkt->dts;
                av_log(f->avctx, AV_LOG_ERROR, "CRC mismatch %X!", crc);
                if (ts != AV_NOPTS_VALUE && avctx->pkt_timebase.num) {
                    av_log(f->avctx, AV_LOG_ERROR, "at %f seconds\n", ts*av_q2d(avctx->pkt_timebase));
                } else if (ts != AV_NOPTS_VALUE) {
                    av_log(f->avctx, AV_LOG_ERROR, "at %"PRId64"\n", ts);
                } else {
                    av_log(f->avctx, AV_LOG_ERROR, "\n");
                }
                fs->slice_damaged = 1;
            }
        }

        if (i) {
            ff_init_range_decoder(&fs->c, buf_p, v);
        } else
            fs->c.bytestream_end = (uint8_t *)(buf_p + v);

        fs->avctx = avctx;
        fs->cur = p;
    }

    avctx->execute(avctx,
                   decode_slice,
                   &f->slice_context[0],
                   NULL,
                   f->slice_count,
                   sizeof(void*));

    for (i = f->slice_count - 1; i >= 0; i--) {
        FFV1Context *fs = f->slice_context[i];
        int j;
        if (fs->slice_damaged && f->last_picture.f->data[0]) {
            const uint8_t *src[4];
            uint8_t *dst[4];
            ff_thread_await_progress(&f->last_picture, INT_MAX, 0);
            for (j = 0; j < 4; j++) {
                int sh = (j==1 || j==2) ? f->chroma_h_shift : 0;
                int sv = (j==1 || j==2) ? f->chroma_v_shift : 0;
                dst[j] = p->data[j] + p->linesize[j]*
                         (fs->slice_y>>sv) + (fs->slice_x>>sh);
                src[j] = f->last_picture.f->data[j] + f->last_picture.f->linesize[j]*
                         (fs->slice_y>>sv) + (fs->slice_x>>sh);
            }
            av_image_copy(dst, p->linesize, (const uint8_t **)src,
                          f->last_picture.f->linesize,
                          avctx->pix_fmt,
                          fs->slice_width,
                          fs->slice_height);
        }
    }
    ff_thread_report_progress(&f->picture, INT_MAX, 0);

    f->picture_number++;

    if (f->last_picture.f)
        ff_thread_release_buffer(avctx, &f->last_picture);
    f->cur = NULL;
    if ((ret = av_frame_ref(data, f->picture.f)) < 0)
        return ret;

    *got_frame = 1;

    return buf_size;
}

static int init_thread_copy(AVCodecContext *avctx)
{
    FFV1Context *f = avctx->priv_data;
    int i, ret;

    f->picture.f      = NULL;
    f->last_picture.f = NULL;
    f->sample_buffer  = NULL;
    f->slice_count = 0;

    for (i = 0; i < f->quant_table_count; i++) {
        av_assert0(f->version > 1);
        f->initial_states[i] = av_memdup(f->initial_states[i],
                                         f->context_count[i] * sizeof(*f->initial_states[i]));
    }

    f->picture.f      = av_frame_alloc();
    f->last_picture.f = av_frame_alloc();

    if ((ret = ffv1_init_slice_contexts(f)) < 0)
        return ret;

    return 0;
}

static void copy_fields(FFV1Context *fsdst, FFV1Context *fssrc, FFV1Context *fsrc)
{
    fsdst->version             = fsrc->version;
    fsdst->minor_version       = fsrc->minor_version;
    fsdst->chroma_planes       = fsrc->chroma_planes;
    fsdst->chroma_h_shift      = fsrc->chroma_h_shift;
    fsdst->chroma_v_shift      = fsrc->chroma_v_shift;
    fsdst->transparency        = fsrc->transparency;
    fsdst->plane_count         = fsrc->plane_count;
    fsdst->ac                  = fsrc->ac;
    fsdst->colorspace          = fsrc->colorspace;

    fsdst->ec                  = fsrc->ec;
    fsdst->intra               = fsrc->intra;
    fsdst->slice_damaged       = fssrc->slice_damaged;
    fsdst->key_frame_ok        = fsrc->key_frame_ok;

    fsdst->bits_per_raw_sample = fsrc->bits_per_raw_sample;
    fsdst->packed_at_lsb       = fsrc->packed_at_lsb;
    fsdst->slice_count         = fsrc->slice_count;
    if (fsrc->version<3){
        fsdst->slice_x             = fssrc->slice_x;
        fsdst->slice_y             = fssrc->slice_y;
        fsdst->slice_width         = fssrc->slice_width;
        fsdst->slice_height        = fssrc->slice_height;
    }
}

static int update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    FFV1Context *fsrc = src->priv_data;
    FFV1Context *fdst = dst->priv_data;
    int i, ret;

    if (dst == src)
        return 0;

    {
        FFV1Context bak = *fdst;
        memcpy(fdst, fsrc, sizeof(*fdst));
        memcpy(fdst->initial_states, bak.initial_states, sizeof(fdst->initial_states));
        memcpy(fdst->slice_context,  bak.slice_context , sizeof(fdst->slice_context));
        fdst->picture      = bak.picture;
        fdst->last_picture = bak.last_picture;
        for (i = 0; i<fdst->num_h_slices * fdst->num_v_slices; i++) {
            FFV1Context *fssrc = fsrc->slice_context[i];
            FFV1Context *fsdst = fdst->slice_context[i];
            copy_fields(fsdst, fssrc, fsrc);
        }
        av_assert0(!fdst->plane[0].state);
        av_assert0(!fdst->sample_buffer);
    }

    av_assert1(fdst->slice_count == fsrc->slice_count);


    ff_thread_release_buffer(dst, &fdst->picture);
    if (fsrc->picture.f->data[0]) {
        if ((ret = ff_thread_ref_frame(&fdst->picture, &fsrc->picture)) < 0)
            return ret;
    }

    fdst->fsrc = fsrc;

    return 0;
}

AVCodec ff_ffv1_decoder = {
    .name           = "ffv1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FFV1,
    .priv_data_size = sizeof(FFV1Context),
    .init           = decode_init,
    .close          = ffv1_close,
    .decode         = decode_frame,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(init_thread_copy),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(update_thread_context),
    .capabilities   = CODEC_CAP_DR1 /*| CODEC_CAP_DRAW_HORIZ_BAND*/ |
                      CODEC_CAP_FRAME_THREADS | CODEC_CAP_SLICE_THREADS,
    .long_name      = NULL_IF_CONFIG_SMALL("FFmpeg video codec #1"),
};
