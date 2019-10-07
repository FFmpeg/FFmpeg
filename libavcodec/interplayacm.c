/*
 * Interplay ACM decoder
 *
 * Copyright (c) 2004-2008 Marko Kreen
 * Copyright (c) 2008 Adam Gashlin
 * Copyright (c) 2015 Paul B Mahol
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "libavutil/intreadwrite.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"

static const int8_t map_1bit[]      = { -1, +1 };
static const int8_t map_2bit_near[] = { -2, -1, +1, +2 };
static const int8_t map_2bit_far[]  = { -3, -2, +2, +3 };
static const int8_t map_3bit[]      = { -4, -3, -2, -1, +1, +2, +3, +4 };

static int mul_3x3 [3 * 3 * 3];
static int mul_3x5 [5 * 5 * 5];
static int mul_2x11[11  *  11];

typedef struct InterplayACMContext {
    GetBitContext gb;
    uint8_t *bitstream;
    int max_framesize;
    int bitstream_size;
    int bitstream_index;

    int level;
    int rows;
    int cols;
    int wrapbuf_len;
    int block_len;
    int skip;

    int *block;
    int *wrapbuf;
    int *ampbuf;
    int *midbuf;
} InterplayACMContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    InterplayACMContext *s = avctx->priv_data;
    int x1, x2, x3;

    if (avctx->extradata_size < 14)
        return AVERROR_INVALIDDATA;

    if (avctx->channels <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of channels: %d\n", avctx->channels);
        return AVERROR_INVALIDDATA;
    }

    s->level = AV_RL16(avctx->extradata + 12) & 0xf;
    s->rows  = AV_RL16(avctx->extradata + 12) >>  4;
    s->cols  = 1 << s->level;
    s->wrapbuf_len = 2 * s->cols - 2;
    s->block_len = s->rows * s->cols;
    s->max_framesize = s->block_len;

    s->block   = av_calloc(s->block_len, sizeof(int));
    s->wrapbuf = av_calloc(s->wrapbuf_len, sizeof(int));
    s->ampbuf  = av_calloc(0x10000, sizeof(int));
    s->bitstream = av_calloc(s->max_framesize + AV_INPUT_BUFFER_PADDING_SIZE / sizeof(*s->bitstream) + 1, sizeof(*s->bitstream));
    if (!s->block || !s->wrapbuf || !s->ampbuf || !s->bitstream)
        return AVERROR(ENOMEM);

    s->midbuf  = s->ampbuf + 0x8000;
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    for (x3 = 0; x3 < 3; x3++)
        for (x2 = 0; x2 < 3; x2++)
            for (x1 = 0; x1 < 3; x1++)
                mul_3x3[x1 + x2 * 3 + x3* 3 * 3] = x1 + (x2 << 4) + (x3 << 8);
    for (x3 = 0; x3 < 5; x3++)
        for (x2 = 0; x2 < 5; x2++)
            for (x1 = 0; x1 < 5; x1++)
                mul_3x5[x1 + x2 * 5 + x3 * 5 * 5] = x1 + (x2 << 4) + (x3 << 8);
    for (x2 = 0; x2 < 11; x2++)
        for (x1 = 0; x1 < 11; x1++)
            mul_2x11[x1 + x2 * 11] = x1 + (x2 << 4);

    return 0;
}

#define set_pos(s, r, c, idx) do {               \
        unsigned pos = ((r) << s->level) + (c);  \
        s->block[pos] = s->midbuf[(idx)];        \
    } while (0)

static int zero(InterplayACMContext *s, unsigned ind, unsigned col)
{
    unsigned i;

    for (i = 0; i < s->rows; i++)
        set_pos(s, i, col, 0);
    return 0;
}

static int bad(InterplayACMContext *s, unsigned ind, unsigned col)
{
    return AVERROR_INVALIDDATA;
}

static int linear(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned int i;
    int b, middle = 1 << (ind - 1);

    for (i = 0; i < s->rows; i++) {
        b = get_bits(gb, ind);
        set_pos(s, i, col, b - middle);
    }
    return 0;
}

static int k13(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;

    for (i = 0; i < s->rows; i++) {
        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i++, col, 0);
            if (i >= s->rows)
                break;
            set_pos(s, i, col, 0);
            continue;
        }
        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i, col, 0);
            continue;
        }
        b = get_bits1(gb);
        set_pos(s, i, col, map_1bit[b]);
    }
    return 0;
}

static int k12(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;

    for (i = 0; i < s->rows; i++) {
        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits1(gb);
        set_pos(s, i, col, map_1bit[b]);
    }
    return 0;
}

static int k24(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;

    for (i = 0; i < s->rows; i++) {
        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i++, col, 0);
            if (i >= s->rows) break;
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits(gb, 2);
        set_pos(s, i, col, map_2bit_near[b]);
    }
    return 0;
}

static int k23(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;

    for (i = 0; i < s->rows; i++) {
        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits(gb, 2);
        set_pos(s, i, col, map_2bit_near[b]);
    }
    return 0;
}

static int k35(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;

    for (i = 0; i < s->rows; i++) {
        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i++, col, 0);
            if (i >= s->rows)
                break;
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits1(gb);
        if (b == 0) {
            b = get_bits1(gb);
            set_pos(s, i, col, map_1bit[b]);
            continue;
        }

        b = get_bits(gb, 2);
        set_pos(s, i, col, map_2bit_far[b]);
    }
    return 0;
}

static int k34(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;

    for (i = 0; i < s->rows; i++) {
        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits1(gb);
        if (b == 0) {
            b = get_bits1(gb);
            set_pos(s, i, col, map_1bit[b]);
            continue;
        }

        b = get_bits(gb, 2);
        set_pos(s, i, col, map_2bit_far[b]);
    }
    return 0;
}

static int k45(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;

    for (i = 0; i < s->rows; i++) {
        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i, col, 0); i++;
            if (i >= s->rows)
                break;
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits(gb, 3);
        set_pos(s, i, col, map_3bit[b]);
    }
    return 0;
}

static int k44(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;

    for (i = 0; i < s->rows; i++) {
        b = get_bits1(gb);
        if (b == 0) {
            set_pos(s, i, col, 0);
            continue;
        }

        b = get_bits(gb, 3);
        set_pos(s, i, col, map_3bit[b]);
    }
    return 0;
}

static int t15(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;
    int n1, n2, n3;

    for (i = 0; i < s->rows; i++) {
        /* b = (x1) + (x2 * 3) + (x3 * 9) */
        b = get_bits(gb, 5);
        if (b > 26) {
            av_log(NULL, AV_LOG_ERROR, "Too large b = %d > 26\n", b);
            return AVERROR_INVALIDDATA;
        }

        n1 =  (mul_3x3[b] & 0x0F) - 1;
        n2 = ((mul_3x3[b] >> 4) & 0x0F) - 1;
        n3 = ((mul_3x3[b] >> 8) & 0x0F) - 1;

        set_pos(s, i++, col, n1);
        if (i >= s->rows)
            break;
        set_pos(s, i++, col, n2);
        if (i >= s->rows)
            break;
        set_pos(s, i, col, n3);
    }
    return 0;
}

static int t27(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;
    int n1, n2, n3;

    for (i = 0; i < s->rows; i++) {
        /* b = (x1) + (x2 * 5) + (x3 * 25) */
        b = get_bits(gb, 7);
        if (b > 124) {
            av_log(NULL, AV_LOG_ERROR, "Too large b = %d > 124\n", b);
            return AVERROR_INVALIDDATA;
        }

        n1 =  (mul_3x5[b] & 0x0F) - 2;
        n2 = ((mul_3x5[b] >> 4) & 0x0F) - 2;
        n3 = ((mul_3x5[b] >> 8) & 0x0F) - 2;

        set_pos(s, i++, col, n1);
        if (i >= s->rows)
            break;
        set_pos(s, i++, col, n2);
        if (i >= s->rows)
            break;
        set_pos(s, i, col, n3);
    }
    return 0;
}

static int t37(InterplayACMContext *s, unsigned ind, unsigned col)
{
    GetBitContext *gb = &s->gb;
    unsigned i, b;
    int n1, n2;
    for (i = 0; i < s->rows; i++) {
        /* b = (x1) + (x2 * 11) */
        b = get_bits(gb, 7);
        if (b > 120) {
            av_log(NULL, AV_LOG_ERROR, "Too large b = %d > 120\n", b);
            return AVERROR_INVALIDDATA;
        }

        n1 =  (mul_2x11[b] & 0x0F) - 5;
        n2 = ((mul_2x11[b] >> 4) & 0x0F) - 5;

        set_pos(s, i++, col, n1);
        if (i >= s->rows)
            break;
        set_pos(s, i, col, n2);
    }
    return 0;
}

typedef int (*filler)(InterplayACMContext *s, unsigned ind, unsigned col);

static const filler filler_list[] = {
    zero,   bad,    bad,    linear,
    linear, linear, linear, linear,
    linear, linear, linear, linear,
    linear, linear, linear, linear,
    linear, k13,    k12,    t15,
    k24,    k23,    t27,    k35,
    k34,    bad,    k45,    k44,
    bad,    t37,    bad,    bad,
};

static int fill_block(InterplayACMContext *s)
{
    GetBitContext *gb = &s->gb;
    unsigned i, ind;
    int ret;

    for (i = 0; i < s->cols; i++) {
        ind = get_bits(gb, 5);
        ret = filler_list[ind](s, ind, i);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static void juggle(int *wrap_p, int *block_p, unsigned sub_len, unsigned sub_count)
{
    unsigned i, j;
    int *p;
    unsigned int r0, r1, r2, r3;

    for (i = 0; i < sub_len; i++) {
        p = block_p;
        r0 = wrap_p[0];
        r1 = wrap_p[1];
        for (j = 0; j < sub_count/2; j++) {
            r2 = *p;
            *p = r1 * 2 + (r0 + r2);
            p += sub_len;
            r3 = *p;
            *p = r2 * 2 - (r1 + r3);
            p += sub_len;
            r0 = r2;
            r1 = r3;
        }

        *wrap_p++ = r0;
        *wrap_p++ = r1;
        block_p++;
    }
}

static void juggle_block(InterplayACMContext *s)
{
    unsigned sub_count, sub_len, todo_count, step_subcount, i;
    int *wrap_p, *block_p, *p;

    /* juggle only if subblock_len > 1 */
    if (s->level == 0)
        return;

    /* 2048 / subblock_len */
    if (s->level > 9)
        step_subcount = 1;
    else
        step_subcount = (2048 >> s->level) - 2;

    /* Apply juggle()  (rows)x(cols)
     * from (step_subcount * 2)            x (subblock_len/2)
     * to   (step_subcount * subblock_len) x (1)
     */
    todo_count = s->rows;
    block_p = s->block;
    while (1) {
        wrap_p = s->wrapbuf;
        sub_count = step_subcount;
        if (sub_count > todo_count)
            sub_count = todo_count;

        sub_len = s->cols / 2;
        sub_count *= 2;

        juggle(wrap_p, block_p, sub_len, sub_count);
        wrap_p += sub_len * 2;

        for (i = 0, p = block_p; i < sub_count; i++) {
            p[0]++;
            p += sub_len;
        }

        while (sub_len > 1) {
            sub_len /= 2;
            sub_count *= 2;
            juggle(wrap_p, block_p, sub_len, sub_count);
            wrap_p += sub_len * 2;
        }

        if (todo_count <= step_subcount)
            break;

        todo_count -= step_subcount;
        block_p += step_subcount << s->level;
    }
}

static int decode_block(InterplayACMContext *s)
{
    GetBitContext *gb = &s->gb;
    int pwr, count, val, i, x, ret;

    pwr = get_bits(gb, 4);
    val = get_bits(gb, 16);

    count = 1 << pwr;

    for (i = 0, x = 0; i < count; i++) {
        s->midbuf[i] = x;
        x += val;
    }

    for (i = 1, x = -val; i <= count; i++) {
        s->midbuf[-i] = x;
        x -= val;
    }

    ret = fill_block(s);
    if (ret < 0)
        return ret;

    juggle_block(s);

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame_ptr, AVPacket *pkt)
{
    InterplayACMContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    AVFrame *frame = data;
    const uint8_t *buf;
    int16_t *samples;
    int ret, n, buf_size, input_buf_size;

    if (!pkt->size && !s->bitstream_size) {
        *got_frame_ptr = 0;
        return 0;
    }

    buf_size = FFMIN(pkt->size, s->max_framesize - s->bitstream_size);
    input_buf_size = buf_size;
    if (s->bitstream_index + s->bitstream_size + buf_size > s->max_framesize) {
        memmove(s->bitstream, &s->bitstream[s->bitstream_index], s->bitstream_size);
        s->bitstream_index = 0;
    }
    if (pkt->data)
        memcpy(&s->bitstream[s->bitstream_index + s->bitstream_size], pkt->data, buf_size);
    buf                = &s->bitstream[s->bitstream_index];
    buf_size          += s->bitstream_size;
    s->bitstream_size  = buf_size;
    if (buf_size < s->max_framesize && pkt->data) {
        *got_frame_ptr = 0;
        return input_buf_size;
    }

    if ((ret = init_get_bits8(gb, buf, buf_size)) < 0)
        return ret;

    frame->nb_samples = s->block_len / avctx->channels;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    skip_bits(gb, s->skip);
    ret = decode_block(s);
    if (ret < 0)
        return ret;

    samples = (int16_t *)frame->data[0];
    for (n = 0; n < frame->nb_samples * avctx->channels; n++) {
        int val = s->block[n] >> s->level;
        *samples++ = val;
    }

    *got_frame_ptr = 1;
    s->skip = get_bits_count(gb) - 8 * (get_bits_count(gb) / 8);
    n = get_bits_count(gb) / 8;

    if (n > buf_size && pkt->data) {
        s->bitstream_size = 0;
        s->bitstream_index = 0;
        return AVERROR_INVALIDDATA;
    }

    if (s->bitstream_size) {
        s->bitstream_index += n;
        s->bitstream_size  -= n;
        return input_buf_size;
    }
    return n;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    InterplayACMContext *s = avctx->priv_data;

    av_freep(&s->block);
    av_freep(&s->wrapbuf);
    av_freep(&s->ampbuf);
    av_freep(&s->bitstream);
    s->bitstream_size = 0;

    return 0;
}

AVCodec ff_interplay_acm_decoder = {
    .name           = "interplayacm",
    .long_name      = NULL_IF_CONFIG_SMALL("Interplay ACM"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_INTERPLAY_ACM,
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size = sizeof(InterplayACMContext),
};
