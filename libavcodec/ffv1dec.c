/*
 * FFV1 decoder
 *
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * FF Video Codec 1 (a lossless codec) decoder
 */

#include "libavutil/avassert.h"
#include "avcodec.h"
#include "get_bits.h"
#include "put_bits.h"
#include "dsputil.h"
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

    assert(k <= 8);

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
            decode_line(s, w, sample, plane_index,
                        s->avctx->bits_per_raw_sample);
            for (x = 0; x < w; x++)
                ((uint16_t *)(src + stride * y))[x] =
                    sample[1][x] << (16 - s->avctx->bits_per_raw_sample);
        }
// STOP_TIMER("decode-line") }
    }
}

static void decode_rgb_frame(FFV1Context *s, uint32_t *src,
                             int w, int h, int stride)
{
    int x, y, p;
    int16_t *sample[3][2];
    for (x = 0; x < 3; x++) {
        sample[x][0] = s->sample_buffer +  x * 2      * (w + 6) + 3;
        sample[x][1] = s->sample_buffer + (x * 2 + 1) * (w + 6) + 3;
    }

    s->run_index = 0;

    memset(s->sample_buffer, 0, 6 * (w + 6) * sizeof(*s->sample_buffer));

    for (y = 0; y < h; y++) {
        for (p = 0; p < 3; p++) {
            int16_t *temp = sample[p][0]; // FIXME: try a normal buffer

            sample[p][0] = sample[p][1];
            sample[p][1] = temp;

            sample[p][1][-1] = sample[p][0][0];
            sample[p][0][w]  = sample[p][0][w - 1];
            decode_line(s, w, sample[p], FFMIN(p, 1), 9);
        }
        for (x = 0; x < w; x++) {
            int g = sample[0][1][x];
            int b = sample[1][1][x];
            int r = sample[2][1][x];

//            assert(g >= 0  && b >= 0  && r >= 0);
//            assert(g < 256 && b < 512 && r < 512);

            b -= 0x100;
            r -= 0x100;
            g -= (b + r) >> 2;
            b += g;
            r += g;

            src[x + stride * y] = b + (g << 8) + (r << 16) + (0xFF << 24);
        }
    }
}

static int decode_slice(AVCodecContext *c, void *arg)
{
    FFV1Context *fs  = *(void **)arg;
    FFV1Context *f   = fs->avctx->priv_data;
    int width        = fs->slice_width;
    int height       = fs->slice_height;
    int x            = fs->slice_x;
    int y            = fs->slice_y;
    AVFrame *const p = &f->picture;

    av_assert1(width && height);
    if (f->colorspace == 0) {
        const int chroma_width  = -((-width)  >> f->chroma_h_shift);
        const int chroma_height = -((-height) >> f->chroma_v_shift);
        const int cx            = x >> f->chroma_h_shift;
        const int cy            = y >> f->chroma_v_shift;

        decode_plane(fs, p->data[0] + x + y * p->linesize[0],
                     width, height, p->linesize[0], 0);

        decode_plane(fs, p->data[1] + cx + cy * p->linesize[1],
                     chroma_width, chroma_height, p->linesize[1], 1);
        decode_plane(fs, p->data[2] + cx + cy * p->linesize[1],
                     chroma_width, chroma_height, p->linesize[2], 1);
    } else {
        decode_rgb_frame(fs,
                         (uint32_t *)p->data[0] + x + y * (p->linesize[0] / 4),
                         width, height, p->linesize[0] / 4);
    }

    emms_c();

    return 0;
}

static int read_quant_table(RangeCoder *c, int16_t *quant_table, int scale)
{
    int v;
    int i = 0;
    uint8_t state[CONTEXT_SIZE];

    memset(state, 128, sizeof(state));

    for (v = 0; i < 128; v++) {
        int len = get_symbol(c, state, 0) + 1;

        if (len + i > 128)
            return -1;

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
            return -1;
        }
    }
    return (context_count + 1) / 2;
}

static int read_extra_header(FFV1Context *f)
{
    RangeCoder *const c = &f->c;
    uint8_t state[CONTEXT_SIZE];
    int i, j, k;
    uint8_t state2[32][CONTEXT_SIZE];

    memset(state2, 128, sizeof(state2));
    memset(state, 128, sizeof(state));

    ff_init_range_decoder(c, f->avctx->extradata, f->avctx->extradata_size);
    ff_build_rac_states(c, 0.05 * (1LL << 32), 256 - 8);

    f->version = get_symbol(c, state, 0);
    f->ac      = f->avctx->coder_type = get_symbol(c, state, 0);
    if (f->ac > 1)
        for (i = 1; i < 256; i++)
            f->state_transition[i] = get_symbol(c, state, 1) + c->one_state[i];
    f->colorspace                 = get_symbol(c, state, 0); // YUV cs type
    f->avctx->bits_per_raw_sample = get_symbol(c, state, 0);
    get_rac(c, state); // no chroma = false
    f->chroma_h_shift = get_symbol(c, state, 0);
    f->chroma_v_shift = get_symbol(c, state, 0);
    get_rac(c, state); // transparency plane
    f->plane_count  = 2;
    f->num_h_slices = 1 + get_symbol(c, state, 0);
    f->num_v_slices = 1 + get_symbol(c, state, 0);

    if (f->num_h_slices > (unsigned)f->width ||
        f->num_v_slices > (unsigned)f->height) {
        av_log(f->avctx, AV_LOG_ERROR, "too many slices\n");
        return -1;
    }

    f->quant_table_count = get_symbol(c, state, 0);

    if (f->quant_table_count > (unsigned)MAX_QUANT_TABLES)
        return -1;

    for (i = 0; i < f->quant_table_count; i++) {
        f->context_count[i] = read_quant_tables(c, f->quant_tables[i]);
        if (f->context_count[i] < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return -1;
        }
    }

    if (ffv1_allocate_initial_states(f) < 0)
        return AVERROR(ENOMEM);

    for (i = 0; i < f->quant_table_count; i++)
        if (get_rac(c, state))
            for (j = 0; j < f->context_count[i]; j++)
                for (k = 0; k < CONTEXT_SIZE; k++) {
                    int pred = j ? f->initial_states[i][j - 1][k] : 128;
                    f->initial_states[i][j][k] =
                        (pred + get_symbol(c, state2[k], 1)) & 0xFF;
                }
    return 0;
}

static int read_header(FFV1Context *f)
{
    uint8_t state[CONTEXT_SIZE];
    int i, j, context_count;
    RangeCoder *const c = &f->slice_context[0]->c;

    memset(state, 128, sizeof(state));

    if (f->version < 2) {
        f->version = get_symbol(c, state, 0);
        f->ac      = f->avctx->coder_type = get_symbol(c, state, 0);
        if (f->ac > 1)
            for (i = 1; i < 256; i++)
                f->state_transition[i] = get_symbol(c, state, 1) + c->one_state[i];
        f->colorspace = get_symbol(c, state, 0); // YUV cs type
        if (f->version > 0)
            f->avctx->bits_per_raw_sample = get_symbol(c, state, 0);
        get_rac(c, state); // no chroma = false
        f->chroma_h_shift = get_symbol(c, state, 0);
        f->chroma_v_shift = get_symbol(c, state, 0);
        get_rac(c, state); // transparency plane
        f->plane_count = 2;
    }

    if (f->colorspace == 0) {
        if (f->avctx->bits_per_raw_sample <= 8) {
            switch (16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00:
                f->avctx->pix_fmt = AV_PIX_FMT_YUV444P;
                break;
            case 0x10:
                f->avctx->pix_fmt = AV_PIX_FMT_YUV422P;
                break;
            case 0x11:
                f->avctx->pix_fmt = AV_PIX_FMT_YUV420P;
                break;
            case 0x20:
                f->avctx->pix_fmt = AV_PIX_FMT_YUV411P;
                break;
            case 0x22:
                f->avctx->pix_fmt = AV_PIX_FMT_YUV410P;
                break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return -1;
            }
        } else {
            switch (16 * f->chroma_h_shift + f->chroma_v_shift) {
            case 0x00:
                f->avctx->pix_fmt = AV_PIX_FMT_YUV444P16;
                break;
            case 0x10:
                f->avctx->pix_fmt = AV_PIX_FMT_YUV422P16;
                break;
            case 0x11:
                f->avctx->pix_fmt = AV_PIX_FMT_YUV420P16;
                break;
            default:
                av_log(f->avctx, AV_LOG_ERROR, "format not supported\n");
                return -1;
            }
        }
    } else if (f->colorspace == 1) {
        if (f->chroma_h_shift || f->chroma_v_shift) {
            av_log(f->avctx, AV_LOG_ERROR,
                   "chroma subsampling not supported in this colorspace\n");
            return -1;
        }
        f->avctx->pix_fmt = AV_PIX_FMT_RGB32;
    } else {
        av_log(f->avctx, AV_LOG_ERROR, "colorspace not supported\n");
        return -1;
    }

    av_dlog(f->avctx, "%d %d %d\n",
            f->chroma_h_shift, f->chroma_v_shift, f->avctx->pix_fmt);

    if (f->version < 2) {
        context_count = read_quant_tables(c, f->quant_table);
        if (context_count < 0) {
            av_log(f->avctx, AV_LOG_ERROR, "read_quant_table error\n");
            return -1;
        }
    } else {
        f->slice_count = get_symbol(c, state, 0);
        if (f->slice_count > (unsigned)MAX_SLICES)
            return -1;
    }

    for (j = 0; j < f->slice_count; j++) {
        FFV1Context *fs = f->slice_context[j];
        fs->ac = f->ac;

        if (f->version >= 2) {
            fs->slice_x      = get_symbol(c, state, 0) * f->width;
            fs->slice_y      = get_symbol(c, state, 0) * f->height;
            fs->slice_width  = (get_symbol(c, state, 0) + 1) * f->width  + fs->slice_x;
            fs->slice_height = (get_symbol(c, state, 0) + 1) * f->height + fs->slice_y;

            fs->slice_x     /= f->num_h_slices;
            fs->slice_y     /= f->num_v_slices;
            fs->slice_width  = fs->slice_width  / f->num_h_slices - fs->slice_x;
            fs->slice_height = fs->slice_height / f->num_v_slices - fs->slice_y;
            if ((unsigned)fs->slice_width  > f->width ||
                (unsigned)fs->slice_height > f->height)
                return -1;
            if ((unsigned)fs->slice_x + (uint64_t)fs->slice_width  > f->width ||
                (unsigned)fs->slice_y + (uint64_t)fs->slice_height > f->height)
                return -1;
        }

        for (i = 0; i < f->plane_count; i++) {
            PlaneContext *const p = &fs->plane[i];

            if (f->version >= 2) {
                int idx = get_symbol(c, state, 0);
                if (idx > (unsigned)f->quant_table_count) {
                    av_log(f->avctx, AV_LOG_ERROR,
                           "quant_table_index out of range\n");
                    return -1;
                }
                p->quant_table_index = idx;
                memcpy(p->quant_table, f->quant_tables[idx],
                       sizeof(p->quant_table));
                context_count = f->context_count[idx];
            } else {
                memcpy(p->quant_table, f->quant_table, sizeof(p->quant_table));
            }

            if (p->context_count < context_count) {
                av_freep(&p->state);
                av_freep(&p->vlc_state);
            }
            p->context_count = context_count;
        }
    }

    return 0;
}

static av_cold int ffv1_decode_init(AVCodecContext *avctx)
{
    FFV1Context *f = avctx->priv_data;

    ffv1_common_init(avctx);

    if (avctx->extradata && read_extra_header(f) < 0)
        return -1;

    if (ffv1_init_slice_contexts(f) < 0)
        return -1;

    return 0;
}

static int ffv1_decode_frame(AVCodecContext *avctx, void *data,
                             int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf  = avpkt->data;
    int buf_size        = avpkt->size;
    FFV1Context *f      = avctx->priv_data;
    RangeCoder *const c = &f->slice_context[0]->c;
    AVFrame *const p    = &f->picture;
    int bytes_read, i;
    uint8_t keystate = 128;
    const uint8_t *buf_p;

    AVFrame *picture = data;

    /* release previously stored data */
    if (p->data[0])
        avctx->release_buffer(avctx, p);

    ff_init_range_decoder(c, buf, buf_size);
    ff_build_rac_states(c, 0.05 * (1LL << 32), 256 - 8);

    p->pict_type = AV_PICTURE_TYPE_I; // FIXME: I vs. P
    if (get_rac(c, &keystate)) {
        p->key_frame = 1;
        if (read_header(f) < 0)
            return -1;
        if (ffv1_init_slice_state(f) < 0)
            return -1;

        ffv1_clear_state(f);
    } else {
        p->key_frame = 0;
    }
    if (f->ac > 1) {
        int i;
        for (i = 1; i < 256; i++) {
            c->one_state[i]        = f->state_transition[i];
            c->zero_state[256 - i] = 256 - c->one_state[i];
        }
    }

    p->reference = 0;
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(avctx, AV_LOG_ERROR, "keyframe:%d coder:%d\n", p->key_frame, f->ac);

    if (!f->ac) {
        bytes_read = c->bytestream - c->bytestream_start - 1;
        if (bytes_read == 0)
            av_log(avctx, AV_LOG_ERROR, "error at end of AC stream\n"); // FIXME
        init_get_bits(&f->slice_context[0]->gb, buf + bytes_read,
                      (buf_size - bytes_read) * 8);
    } else {
        bytes_read = 0; /* avoid warning */
    }

    buf_p = buf + buf_size;
    for (i = f->slice_count - 1; i > 0; i--) {
        FFV1Context *fs = f->slice_context[i];
        int v           = AV_RB24(buf_p - 3) + 3;
        if (buf_p - buf <= v) {
            av_log(avctx, AV_LOG_ERROR, "Slice pointer chain broken\n");
            return -1;
        }
        buf_p -= v;
        if (fs->ac)
            ff_init_range_decoder(&fs->c, buf_p, v);
        else
            init_get_bits(&fs->gb, buf_p, v * 8);
    }

    avctx->execute(avctx, decode_slice, &f->slice_context[0],
                   NULL, f->slice_count, sizeof(void *));
    f->picture_number++;

    *picture   = *p;
    *data_size = sizeof(AVFrame);

    return buf_size;
}

AVCodec ff_ffv1_decoder = {
    .name           = "ffv1",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FFV1,
    .priv_data_size = sizeof(FFV1Context),
    .init           = ffv1_decode_init,
    .close          = ffv1_close,
    .decode         = ffv1_decode_frame,
    .capabilities   = CODEC_CAP_DR1 /*| CODEC_CAP_DRAW_HORIZ_BAND*/ |
                      CODEC_CAP_SLICE_THREADS,
    .long_name      = NULL_IF_CONFIG_SMALL("FFmpeg video codec #1"),
};
