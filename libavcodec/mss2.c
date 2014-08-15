/*
 * Microsoft Screen 2 (aka Windows Media Video V9 Screen) decoder
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
 * Microsoft Screen 2 (aka Windows Media Video V9 Screen) decoder
 */

#include "libavutil/avassert.h"
#include "error_resilience.h"
#include "internal.h"
#include "mpeg_er.h"
#include "msmpeg4data.h"
#include "qpeldsp.h"
#include "vc1.h"
#include "mss12.h"
#include "mss2dsp.h"

typedef struct MSS2Context {
    VC1Context     v;
    int            split_position;
    AVFrame       *last_pic;
    MSS12Context   c;
    MSS2DSPContext dsp;
    QpelDSPContext qdsp;
    SliceContext   sc[2];
} MSS2Context;

static void arith2_normalise(ArithCoder *c)
{
    while ((c->high >> 15) - (c->low >> 15) < 2) {
        if ((c->low ^ c->high) & 0x10000) {
            c->high  ^= 0x8000;
            c->value ^= 0x8000;
            c->low   ^= 0x8000;
        }
        c->high  = c->high  << 8 & 0xFFFFFF | 0xFF;
        c->value = c->value << 8 & 0xFFFFFF | bytestream2_get_byte(c->gbc.gB);
        c->low   = c->low   << 8 & 0xFFFFFF;
    }
}

ARITH_GET_BIT(arith2)

/* L. Stuiver and A. Moffat: "Piecewise Integer Mapping for Arithmetic Coding."
 * In Proc. 8th Data Compression Conference (DCC '98), pp. 3-12, Mar. 1998 */

static int arith2_get_scaled_value(int value, int n, int range)
{
    int split = (n << 1) - range;

    if (value > split)
        return split + (value - split >> 1);
    else
        return value;
}

static void arith2_rescale_interval(ArithCoder *c, int range,
                                    int low, int high, int n)
{
    int split = (n << 1) - range;

    if (high > split)
        c->high = split + (high - split << 1);
    else
        c->high = high;

    c->high += c->low - 1;

    if (low > split)
        c->low += split + (low - split << 1);
    else
        c->low += low;
}

static int arith2_get_number(ArithCoder *c, int n)
{
    int range = c->high - c->low + 1;
    int scale = av_log2(range) - av_log2(n);
    int val;

    if (n << scale > range)
        scale--;

    n <<= scale;

    val = arith2_get_scaled_value(c->value - c->low, n, range) >> scale;

    arith2_rescale_interval(c, range, val << scale, (val + 1) << scale, n);

    arith2_normalise(c);

    return val;
}

static int arith2_get_prob(ArithCoder *c, int16_t *probs)
{
    int range = c->high - c->low + 1, n = *probs;
    int scale = av_log2(range) - av_log2(n);
    int i     = 0, val;

    if (n << scale > range)
        scale--;

    n <<= scale;

    val = arith2_get_scaled_value(c->value - c->low, n, range) >> scale;
    while (probs[++i] > val) ;

    arith2_rescale_interval(c, range,
                            probs[i] << scale, probs[i - 1] << scale, n);

    return i;
}

ARITH_GET_MODEL_SYM(arith2)

static int arith2_get_consumed_bytes(ArithCoder *c)
{
    int diff = (c->high >> 16) - (c->low >> 16);
    int bp   = bytestream2_tell(c->gbc.gB) - 3 << 3;
    int bits = 1;

    while (!(diff & 0x80)) {
        bits++;
        diff <<= 1;
    }

    return (bits + bp + 7 >> 3) + ((c->low >> 16) + 1 == c->high >> 16);
}

static void arith2_init(ArithCoder *c, GetByteContext *gB)
{
    c->low           = 0;
    c->high          = 0xFFFFFF;
    c->value         = bytestream2_get_be24(gB);
    c->gbc.gB        = gB;
    c->get_model_sym = arith2_get_model_sym;
    c->get_number    = arith2_get_number;
}

static int decode_pal_v2(MSS12Context *ctx, const uint8_t *buf, int buf_size)
{
    int i, ncol;
    uint32_t *pal = ctx->pal + 256 - ctx->free_colours;

    if (!ctx->free_colours)
        return 0;

    ncol = *buf++;
    if (ncol > ctx->free_colours || buf_size < 2 + ncol * 3)
        return AVERROR_INVALIDDATA;
    for (i = 0; i < ncol; i++)
        *pal++ = AV_RB24(buf + 3 * i);

    return 1 + ncol * 3;
}

static int decode_555(GetByteContext *gB, uint16_t *dst, int stride,
                      int keyframe, int w, int h)
{
    int last_symbol = 0, repeat = 0, prev_avail = 0;

    if (!keyframe) {
        int x, y, endx, endy, t;

#define READ_PAIR(a, b)                 \
    a  = bytestream2_get_byte(gB) << 4; \
    t  = bytestream2_get_byte(gB);      \
    a |= t >> 4;                        \
    b  = (t & 0xF) << 8;                \
    b |= bytestream2_get_byte(gB);      \

        READ_PAIR(x, endx)
        READ_PAIR(y, endy)

        if (endx >= w || endy >= h || x > endx || y > endy)
            return AVERROR_INVALIDDATA;
        dst += x + stride * y;
        w    = endx - x + 1;
        h    = endy - y + 1;
        if (y)
            prev_avail = 1;
    }

    do {
        uint16_t *p = dst;
        do {
            if (repeat-- < 1) {
                int b = bytestream2_get_byte(gB);
                if (b < 128)
                    last_symbol = b << 8 | bytestream2_get_byte(gB);
                else if (b > 129) {
                    repeat = 0;
                    while (b-- > 130)
                        repeat = (repeat << 8) + bytestream2_get_byte(gB) + 1;
                    if (last_symbol == -2) {
                        int skip = FFMIN((unsigned)repeat, dst + w - p);
                        repeat -= skip;
                        p      += skip;
                    }
                } else
                    last_symbol = 127 - b;
            }
            if (last_symbol >= 0)
                *p = last_symbol;
            else if (last_symbol == -1 && prev_avail)
                *p = *(p - stride);
        } while (++p < dst + w);
        dst       += stride;
        prev_avail = 1;
    } while (--h);

    return 0;
}

static int decode_rle(GetBitContext *gb, uint8_t *pal_dst, int pal_stride,
                      uint8_t *rgb_dst, int rgb_stride, uint32_t *pal,
                      int keyframe, int kf_slipt, int slice, int w, int h)
{
    uint8_t bits[270] = { 0 };
    uint32_t codes[270];
    VLC vlc;

    int current_length = 0, read_codes = 0, next_code = 0, current_codes = 0;
    int remaining_codes, surplus_codes, i;

    const int alphabet_size = 270 - keyframe;

    int last_symbol = 0, repeat = 0, prev_avail = 0;

    if (!keyframe) {
        int x, y, clipw, cliph;

        x     = get_bits(gb, 12);
        y     = get_bits(gb, 12);
        clipw = get_bits(gb, 12) + 1;
        cliph = get_bits(gb, 12) + 1;

        if (x + clipw > w || y + cliph > h)
            return AVERROR_INVALIDDATA;
        pal_dst += pal_stride * y + x;
        rgb_dst += rgb_stride * y + x * 3;
        w        = clipw;
        h        = cliph;
        if (y)
            prev_avail = 1;
    } else {
        if (slice > 0) {
            pal_dst   += pal_stride * kf_slipt;
            rgb_dst   += rgb_stride * kf_slipt;
            prev_avail = 1;
            h         -= kf_slipt;
        } else
            h = kf_slipt;
    }

    /* read explicit codes */
    do {
        while (current_codes--) {
            int symbol = get_bits(gb, 8);
            if (symbol >= 204 - keyframe)
                symbol += 14 - keyframe;
            else if (symbol > 189)
                symbol = get_bits1(gb) + (symbol << 1) - 190;
            if (bits[symbol])
                return AVERROR_INVALIDDATA;
            bits[symbol]  = current_length;
            codes[symbol] = next_code++;
            read_codes++;
        }
        current_length++;
        next_code     <<= 1;
        remaining_codes = (1 << current_length) - next_code;
        current_codes   = get_bits(gb, av_ceil_log2(remaining_codes + 1));
        if (current_length > 22 || current_codes > remaining_codes)
            return AVERROR_INVALIDDATA;
    } while (current_codes != remaining_codes);

    remaining_codes = alphabet_size - read_codes;

    /* determine the minimum length to fit the rest of the alphabet */
    while ((surplus_codes = (2 << current_length) -
                            (next_code << 1) - remaining_codes) < 0) {
        current_length++;
        next_code <<= 1;
    }

    /* add the rest of the symbols lexicographically */
    for (i = 0; i < alphabet_size; i++)
        if (!bits[i]) {
            if (surplus_codes-- == 0) {
                current_length++;
                next_code <<= 1;
            }
            bits[i]  = current_length;
            codes[i] = next_code++;
        }

    if (next_code != 1 << current_length)
        return AVERROR_INVALIDDATA;

    if (i = init_vlc(&vlc, 9, alphabet_size, bits, 1, 1, codes, 4, 4, 0))
        return i;

    /* frame decode */
    do {
        uint8_t *pp = pal_dst;
        uint8_t *rp = rgb_dst;
        do {
            if (repeat-- < 1) {
                int b = get_vlc2(gb, vlc.table, 9, 3);
                if (b < 256)
                    last_symbol = b;
                else if (b < 268) {
                    b -= 256;
                    if (b == 11)
                        b = get_bits(gb, 4) + 10;

                    if (!b)
                        repeat = 0;
                    else
                        repeat = get_bits(gb, b);

                    repeat += (1 << b) - 1;

                    if (last_symbol == -2) {
                        int skip = FFMIN(repeat, pal_dst + w - pp);
                        repeat -= skip;
                        pp     += skip;
                        rp     += skip * 3;
                    }
                } else
                    last_symbol = 267 - b;
            }
            if (last_symbol >= 0) {
                *pp = last_symbol;
                AV_WB24(rp, pal[last_symbol]);
            } else if (last_symbol == -1 && prev_avail) {
                *pp = *(pp - pal_stride);
                memcpy(rp, rp - rgb_stride, 3);
            }
            rp += 3;
        } while (++pp < pal_dst + w);
        pal_dst   += pal_stride;
        rgb_dst   += rgb_stride;
        prev_avail = 1;
    } while (--h);

    ff_free_vlc(&vlc);
    return 0;
}

static int decode_wmv9(AVCodecContext *avctx, const uint8_t *buf, int buf_size,
                       int x, int y, int w, int h, int wmv9_mask)
{
    MSS2Context *ctx  = avctx->priv_data;
    MSS12Context *c   = &ctx->c;
    VC1Context *v     = avctx->priv_data;
    MpegEncContext *s = &v->s;
    AVFrame *f;
    int ret;

    ff_mpeg_flush(avctx);

    if ((ret = init_get_bits8(&s->gb, buf, buf_size)) < 0)
        return ret;

    s->loop_filter = avctx->skip_loop_filter < AVDISCARD_ALL;

    if (ff_vc1_parse_frame_header(v, &s->gb) < 0) {
        av_log(v->s.avctx, AV_LOG_ERROR, "header error\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->pict_type != AV_PICTURE_TYPE_I) {
        av_log(v->s.avctx, AV_LOG_ERROR, "expected I-frame\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if ((ret = ff_mpv_frame_start(s, avctx)) < 0) {
        av_log(v->s.avctx, AV_LOG_ERROR, "ff_mpv_frame_start error\n");
        avctx->pix_fmt = AV_PIX_FMT_RGB24;
        return ret;
    }

    ff_mpeg_er_frame_start(s);

    v->bits = buf_size * 8;

    v->end_mb_x = (w + 15) >> 4;
    s->end_mb_y = (h + 15) >> 4;
    if (v->respic & 1)
        v->end_mb_x = v->end_mb_x + 1 >> 1;
    if (v->respic & 2)
        s->end_mb_y = s->end_mb_y + 1 >> 1;

    ff_vc1_decode_blocks(v);

    ff_er_frame_end(&s->er);

    ff_mpv_frame_end(s);

    f = s->current_picture.f;

    if (v->respic == 3) {
        ctx->dsp.upsample_plane(f->data[0], f->linesize[0], w,      h);
        ctx->dsp.upsample_plane(f->data[1], f->linesize[1], w+1 >> 1, h+1 >> 1);
        ctx->dsp.upsample_plane(f->data[2], f->linesize[2], w+1 >> 1, h+1 >> 1);
    } else if (v->respic)
        avpriv_request_sample(v->s.avctx,
                              "Asymmetric WMV9 rectangle subsampling");

    av_assert0(f->linesize[1] == f->linesize[2]);

    if (wmv9_mask != -1)
        ctx->dsp.mss2_blit_wmv9_masked(c->rgb_pic + y * c->rgb_stride + x * 3,
                                       c->rgb_stride, wmv9_mask,
                                       c->pal_pic + y * c->pal_stride + x,
                                       c->pal_stride,
                                       f->data[0], f->linesize[0],
                                       f->data[1], f->data[2], f->linesize[1],
                                       w, h);
    else
        ctx->dsp.mss2_blit_wmv9(c->rgb_pic + y * c->rgb_stride + x * 3,
                                c->rgb_stride,
                                f->data[0], f->linesize[0],
                                f->data[1], f->data[2], f->linesize[1],
                                w, h);

    avctx->pix_fmt = AV_PIX_FMT_RGB24;

    return 0;
}

typedef struct Rectangle {
    int coded, x, y, w, h;
} Rectangle;

#define MAX_WMV9_RECTANGLES 20
#define ARITH2_PADDING 2

static int mss2_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                             AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    MSS2Context *ctx = avctx->priv_data;
    MSS12Context *c  = &ctx->c;
    AVFrame *frame   = data;
    GetBitContext gb;
    GetByteContext gB;
    ArithCoder acoder;

    int keyframe, has_wmv9, has_mv, is_rle, is_555, ret;

    Rectangle wmv9rects[MAX_WMV9_RECTANGLES], *r;
    int used_rects = 0, i, implicit_rect = 0, av_uninit(wmv9_mask);

    av_assert0(FF_INPUT_BUFFER_PADDING_SIZE >=
               ARITH2_PADDING + (MIN_CACHE_BITS + 7) / 8);

    if ((ret = init_get_bits8(&gb, buf, buf_size)) < 0)
        return ret;

    if (keyframe = get_bits1(&gb))
        skip_bits(&gb, 7);
    has_wmv9 = get_bits1(&gb);
    has_mv   = keyframe ? 0 : get_bits1(&gb);
    is_rle   = get_bits1(&gb);
    is_555   = is_rle && get_bits1(&gb);
    if (c->slice_split > 0)
        ctx->split_position = c->slice_split;
    else if (c->slice_split < 0) {
        if (get_bits1(&gb)) {
            if (get_bits1(&gb)) {
                if (get_bits1(&gb))
                    ctx->split_position = get_bits(&gb, 16);
                else
                    ctx->split_position = get_bits(&gb, 12);
            } else
                ctx->split_position = get_bits(&gb, 8) << 4;
        } else {
            if (keyframe)
                ctx->split_position = avctx->height / 2;
        }
    } else
        ctx->split_position = avctx->height;

    if (c->slice_split && (ctx->split_position < 1 - is_555 ||
                           ctx->split_position > avctx->height - 1))
        return AVERROR_INVALIDDATA;

    align_get_bits(&gb);
    buf      += get_bits_count(&gb) >> 3;
    buf_size -= get_bits_count(&gb) >> 3;

    if (buf_size < 1)
        return AVERROR_INVALIDDATA;

    if (is_555 && (has_wmv9 || has_mv || c->slice_split && ctx->split_position))
        return AVERROR_INVALIDDATA;

    avctx->pix_fmt = is_555 ? AV_PIX_FMT_RGB555 : AV_PIX_FMT_RGB24;
    if (ctx->last_pic->format != avctx->pix_fmt)
        av_frame_unref(ctx->last_pic);

    if (has_wmv9) {
        bytestream2_init(&gB, buf, buf_size + ARITH2_PADDING);
        arith2_init(&acoder, &gB);

        implicit_rect = !arith2_get_bit(&acoder);

        while (arith2_get_bit(&acoder)) {
            if (used_rects == MAX_WMV9_RECTANGLES)
                return AVERROR_INVALIDDATA;
            r = &wmv9rects[used_rects];
            if (!used_rects)
                r->x = arith2_get_number(&acoder, avctx->width);
            else
                r->x = arith2_get_number(&acoder, avctx->width -
                                         wmv9rects[used_rects - 1].x) +
                       wmv9rects[used_rects - 1].x;
            r->y = arith2_get_number(&acoder, avctx->height);
            r->w = arith2_get_number(&acoder, avctx->width  - r->x) + 1;
            r->h = arith2_get_number(&acoder, avctx->height - r->y) + 1;
            used_rects++;
        }

        if (implicit_rect && used_rects) {
            av_log(avctx, AV_LOG_ERROR, "implicit_rect && used_rects > 0\n");
            return AVERROR_INVALIDDATA;
        }

        if (implicit_rect) {
            wmv9rects[0].x = 0;
            wmv9rects[0].y = 0;
            wmv9rects[0].w = avctx->width;
            wmv9rects[0].h = avctx->height;

            used_rects = 1;
        }
        for (i = 0; i < used_rects; i++) {
            if (!implicit_rect && arith2_get_bit(&acoder)) {
                av_log(avctx, AV_LOG_ERROR, "Unexpected grandchildren\n");
                return AVERROR_INVALIDDATA;
            }
            if (!i) {
                wmv9_mask = arith2_get_bit(&acoder) - 1;
                if (!wmv9_mask)
                    wmv9_mask = arith2_get_number(&acoder, 256);
            }
            wmv9rects[i].coded = arith2_get_number(&acoder, 2);
        }

        buf      += arith2_get_consumed_bytes(&acoder);
        buf_size -= arith2_get_consumed_bytes(&acoder);
        if (buf_size < 1)
            return AVERROR_INVALIDDATA;
    }

    c->mvX = c->mvY = 0;
    if (keyframe && !is_555) {
        if ((i = decode_pal_v2(c, buf, buf_size)) < 0)
            return AVERROR_INVALIDDATA;
        buf      += i;
        buf_size -= i;
    } else if (has_mv) {
        buf      += 4;
        buf_size -= 4;
        if (buf_size < 1)
            return AVERROR_INVALIDDATA;
        c->mvX = AV_RB16(buf - 4) - avctx->width;
        c->mvY = AV_RB16(buf - 2) - avctx->height;
    }

    if (c->mvX < 0 || c->mvY < 0) {
        FFSWAP(uint8_t *, c->pal_pic, c->last_pal_pic);

        if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
            return ret;

        if (ctx->last_pic->data[0]) {
            av_assert0(frame->linesize[0] == ctx->last_pic->linesize[0]);
            c->last_rgb_pic = ctx->last_pic->data[0] +
                              ctx->last_pic->linesize[0] * (avctx->height - 1);
        } else {
            av_log(avctx, AV_LOG_ERROR, "Missing keyframe\n");
            return AVERROR_INVALIDDATA;
        }
    } else {
        if ((ret = ff_reget_buffer(avctx, ctx->last_pic)) < 0)
            return ret;
        if ((ret = av_frame_ref(frame, ctx->last_pic)) < 0)
            return ret;

        c->last_rgb_pic = NULL;
    }
    c->rgb_pic    = frame->data[0] +
                    frame->linesize[0] * (avctx->height - 1);
    c->rgb_stride = -frame->linesize[0];

    frame->key_frame = keyframe;
    frame->pict_type = keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    if (is_555) {
        bytestream2_init(&gB, buf, buf_size);

        if (decode_555(&gB, (uint16_t *)c->rgb_pic, c->rgb_stride >> 1,
                       keyframe, avctx->width, avctx->height))
            return AVERROR_INVALIDDATA;

        buf_size -= bytestream2_tell(&gB);
    } else {
        if (keyframe) {
            c->corrupted = 0;
            ff_mss12_slicecontext_reset(&ctx->sc[0]);
            if (c->slice_split)
                ff_mss12_slicecontext_reset(&ctx->sc[1]);
        }
        if (is_rle) {
            if ((ret = init_get_bits8(&gb, buf, buf_size)) < 0)
                return ret;
            if (ret = decode_rle(&gb, c->pal_pic, c->pal_stride,
                                 c->rgb_pic, c->rgb_stride, c->pal, keyframe,
                                 ctx->split_position, 0,
                                 avctx->width, avctx->height))
                return ret;
            align_get_bits(&gb);

            if (c->slice_split)
                if (ret = decode_rle(&gb, c->pal_pic, c->pal_stride,
                                     c->rgb_pic, c->rgb_stride, c->pal, keyframe,
                                     ctx->split_position, 1,
                                     avctx->width, avctx->height))
                    return ret;

            align_get_bits(&gb);
            buf      += get_bits_count(&gb) >> 3;
            buf_size -= get_bits_count(&gb) >> 3;
        } else if (!implicit_rect || wmv9_mask != -1) {
            if (c->corrupted)
                return AVERROR_INVALIDDATA;
            bytestream2_init(&gB, buf, buf_size + ARITH2_PADDING);
            arith2_init(&acoder, &gB);
            c->keyframe = keyframe;
            if (c->corrupted = ff_mss12_decode_rect(&ctx->sc[0], &acoder, 0, 0,
                                                    avctx->width,
                                                    ctx->split_position))
                return AVERROR_INVALIDDATA;

            buf      += arith2_get_consumed_bytes(&acoder);
            buf_size -= arith2_get_consumed_bytes(&acoder);
            if (c->slice_split) {
                if (buf_size < 1)
                    return AVERROR_INVALIDDATA;
                bytestream2_init(&gB, buf, buf_size + ARITH2_PADDING);
                arith2_init(&acoder, &gB);
                if (c->corrupted = ff_mss12_decode_rect(&ctx->sc[1], &acoder, 0,
                                                        ctx->split_position,
                                                        avctx->width,
                                                        avctx->height - ctx->split_position))
                    return AVERROR_INVALIDDATA;

                buf      += arith2_get_consumed_bytes(&acoder);
                buf_size -= arith2_get_consumed_bytes(&acoder);
            }
        } else
            memset(c->pal_pic, 0, c->pal_stride * avctx->height);
    }

    if (has_wmv9) {
        for (i = 0; i < used_rects; i++) {
            int x = wmv9rects[i].x;
            int y = wmv9rects[i].y;
            int w = wmv9rects[i].w;
            int h = wmv9rects[i].h;
            if (wmv9rects[i].coded) {
                int WMV9codedFrameSize;
                if (buf_size < 4 || !(WMV9codedFrameSize = AV_RL24(buf)))
                    return AVERROR_INVALIDDATA;
                if (ret = decode_wmv9(avctx, buf + 3, buf_size - 3,
                                      x, y, w, h, wmv9_mask))
                    return ret;
                buf      += WMV9codedFrameSize + 3;
                buf_size -= WMV9codedFrameSize + 3;
            } else {
                uint8_t *dst = c->rgb_pic + y * c->rgb_stride + x * 3;
                if (wmv9_mask != -1) {
                    ctx->dsp.mss2_gray_fill_masked(dst, c->rgb_stride,
                                                   wmv9_mask,
                                                   c->pal_pic + y * c->pal_stride + x,
                                                   c->pal_stride,
                                                   w, h);
                } else {
                    do {
                        memset(dst, 0x80, w * 3);
                        dst += c->rgb_stride;
                    } while (--h);
                }
            }
        }
    }

    if (buf_size)
        av_log(avctx, AV_LOG_WARNING, "buffer not fully consumed\n");

    if (c->mvX < 0 || c->mvY < 0) {
        av_frame_unref(ctx->last_pic);
        ret = av_frame_ref(ctx->last_pic, frame);
        if (ret < 0)
            return ret;
    }

    *got_frame       = 1;

    return avpkt->size;
}

static av_cold int wmv9_init(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    int ret;

    v->s.avctx    = avctx;

    if ((ret = ff_vc1_init_common(v)) < 0)
        return ret;
    ff_vc1dsp_init(&v->vc1dsp);

    v->profile = PROFILE_MAIN;

    v->zz_8x4     = ff_wmv2_scantableA;
    v->zz_4x8     = ff_wmv2_scantableB;
    v->res_y411   = 0;
    v->res_sprite = 0;

    v->frmrtq_postproc = 7;
    v->bitrtq_postproc = 31;

    v->res_x8          = 0;
    v->multires        = 0;
    v->res_fasttx      = 1;

    v->fastuvmc        = 0;

    v->extended_mv     = 0;

    v->dquant          = 1;
    v->vstransform     = 1;

    v->res_transtab    = 0;

    v->overlap         = 0;

    v->resync_marker   = 0;
    v->rangered        = 0;

    v->s.max_b_frames = avctx->max_b_frames = 0;
    v->quantizer_mode = 0;

    v->finterpflag = 0;

    v->res_rtm_flag = 1;

    ff_vc1_init_transposed_scantables(v);

    if ((ret = ff_msmpeg4_decode_init(avctx)) < 0 ||
        (ret = ff_vc1_decode_init_alloc_tables(v)) < 0)
        return ret;

    /* error concealment */
    v->s.me.qpel_put = v->s.qdsp.put_qpel_pixels_tab;
    v->s.me.qpel_avg = v->s.qdsp.avg_qpel_pixels_tab;

    return 0;
}

static av_cold int mss2_decode_end(AVCodecContext *avctx)
{
    MSS2Context *const ctx = avctx->priv_data;

    av_frame_free(&ctx->last_pic);

    ff_mss12_decode_end(&ctx->c);
    av_freep(&ctx->c.pal_pic);
    av_freep(&ctx->c.last_pal_pic);
    ff_vc1_decode_end(avctx);

    return 0;
}

static av_cold int mss2_decode_init(AVCodecContext *avctx)
{
    MSS2Context * const ctx = avctx->priv_data;
    MSS12Context *c = &ctx->c;
    int ret;
    c->avctx = avctx;
    if (ret = ff_mss12_decode_init(c, 1, &ctx->sc[0], &ctx->sc[1]))
        return ret;
    ctx->last_pic   = av_frame_alloc();
    c->pal_stride   = c->mask_stride;
    c->pal_pic      = av_mallocz(c->pal_stride * avctx->height);
    c->last_pal_pic = av_mallocz(c->pal_stride * avctx->height);
    if (!c->pal_pic || !c->last_pal_pic || !ctx->last_pic) {
        mss2_decode_end(avctx);
        return AVERROR(ENOMEM);
    }
    if (ret = wmv9_init(avctx)) {
        mss2_decode_end(avctx);
        return ret;
    }
    ff_mss2dsp_init(&ctx->dsp);
    ff_qpeldsp_init(&ctx->qdsp);

    avctx->pix_fmt = c->free_colours == 127 ? AV_PIX_FMT_RGB555
                                            : AV_PIX_FMT_RGB24;


    return 0;
}

AVCodec ff_mss2_decoder = {
    .name           = "mss2",
    .long_name      = NULL_IF_CONFIG_SMALL("MS Windows Media Video V9 Screen"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MSS2,
    .priv_data_size = sizeof(MSS2Context),
    .init           = mss2_decode_init,
    .close          = mss2_decode_end,
    .decode         = mss2_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
