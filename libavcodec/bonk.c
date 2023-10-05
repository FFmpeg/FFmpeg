/*
 * Bonk audio decoder
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

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "bytestream.h"

typedef struct BitCount {
    uint8_t bit;
    unsigned count;
} BitCount;

typedef struct BonkContext {
    GetBitContext gb;
    int skip;

    uint8_t *bitstream;
    int64_t max_framesize;
    int bitstream_size;
    int bitstream_index;

    uint64_t nb_samples;
    int lossless;
    int mid_side;
    int n_taps;
    int down_sampling;
    int samples_per_packet;

    int state[2][2048], k[2048];
    int *samples[2];
    int *input_samples;
    uint8_t quant[2048];
    BitCount *bits;
} BonkContext;

static av_cold int bonk_close(AVCodecContext *avctx)
{
    BonkContext *s = avctx->priv_data;

    av_freep(&s->bitstream);
    av_freep(&s->input_samples);
    av_freep(&s->samples[0]);
    av_freep(&s->samples[1]);
    av_freep(&s->bits);
    s->bitstream_size = 0;

    return 0;
}

static av_cold int bonk_init(AVCodecContext *avctx)
{
    BonkContext *s = avctx->priv_data;

    avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    if (avctx->extradata_size < 17)
        return AVERROR(EINVAL);

    if (avctx->extradata[0]) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported version.\n");
        return AVERROR_INVALIDDATA;
    }

    if (avctx->ch_layout.nb_channels < 1 || avctx->ch_layout.nb_channels > 2)
        return AVERROR_INVALIDDATA;

    s->nb_samples = AV_RL32(avctx->extradata + 1) / avctx->ch_layout.nb_channels;
    if (!s->nb_samples)
        s->nb_samples = UINT64_MAX;
    s->lossless = avctx->extradata[10] != 0;
    s->mid_side = avctx->extradata[11] != 0;
    s->n_taps = AV_RL16(avctx->extradata + 12);
    if (!s->n_taps || s->n_taps > 2048)
        return AVERROR(EINVAL);

    s->down_sampling = avctx->extradata[14];
    if (!s->down_sampling)
        return AVERROR(EINVAL);

    s->samples_per_packet = AV_RL16(avctx->extradata + 15);
    if (!s->samples_per_packet)
        return AVERROR(EINVAL);

    if (s->down_sampling * s->samples_per_packet < s->n_taps)
        return AVERROR_INVALIDDATA;

    s->max_framesize = s->samples_per_packet * avctx->ch_layout.nb_channels * s->down_sampling * 16LL;
    if (s->max_framesize > (INT32_MAX - AV_INPUT_BUFFER_PADDING_SIZE) / 8)
        return AVERROR_INVALIDDATA;

    s->bitstream = av_calloc(s->max_framesize + AV_INPUT_BUFFER_PADDING_SIZE, sizeof(*s->bitstream));
    if (!s->bitstream)
        return AVERROR(ENOMEM);

    s->input_samples = av_calloc(s->samples_per_packet, sizeof(*s->input_samples));
    if (!s->input_samples)
        return AVERROR(ENOMEM);

    s->samples[0] = av_calloc(s->samples_per_packet * s->down_sampling, sizeof(*s->samples[0]));
    s->samples[1] = av_calloc(s->samples_per_packet * s->down_sampling, sizeof(*s->samples[0]));
    if (!s->samples[0] || !s->samples[1])
        return AVERROR(ENOMEM);

    s->bits = av_calloc(s->max_framesize * 8, sizeof(*s->bits));
    if (!s->bits)
        return AVERROR(ENOMEM);

    for (int i = 0; i < 512; i++) {
        s->quant[i] = sqrt(i + 1);
    }

    return 0;
}

static unsigned read_uint_max(BonkContext *s, uint32_t max)
{
    unsigned value = 0;

    if (max == 0)
        return 0;

    av_assert0(max >> 31 == 0);

    for (unsigned i = 1; i <= max - value; i+=i)
        if (get_bits1(&s->gb))
            value += i;

    return value;
}

static int intlist_read(BonkContext *s, int *buf, int entries, int base_2_part)
{
    int i, low_bits = 0, x = 0, max_x;
    int n_zeros = 0, step = 256, dominant = 0;
    int pos = 0, level = 0;
    BitCount *bits = s->bits;
    int passes = 1;

    memset(buf, 0, entries * sizeof(*buf));
    if (base_2_part) {
        low_bits = get_bits(&s->gb, 4);

        if (low_bits)
            for (i = 0; i < entries; i++)
                buf[i] = get_bits(&s->gb, low_bits);
    }

    while (n_zeros < entries) {
        int steplet = step >> 8;

        if (get_bits_left(&s->gb) <= 0)
            return AVERROR_INVALIDDATA;

        if (!get_bits1(&s->gb)) {
            av_assert0(steplet >= 0);

            if (steplet > 0) {
                bits[x  ].bit   = dominant;
                bits[x++].count = steplet;
            }

            if (!dominant)
                n_zeros += steplet;

            if (step > INT32_MAX*8LL/9 + 1)
                return AVERROR_INVALIDDATA;
            step += step / 8;
        } else if (steplet > 0) {
            int actual_run = read_uint_max(s, steplet - 1);

            av_assert0(actual_run >= 0);

            if (actual_run > 0) {
                bits[x  ].bit   = dominant;
                bits[x++].count = actual_run;
            }

            bits[x  ].bit   = !dominant;
            bits[x++].count = 1;

            if (!dominant)
                n_zeros += actual_run;
            else
                n_zeros++;

            step -= step / 8;
        }

        if (step < 256) {
            step = 65536 / step;
            dominant = !dominant;
        }
    }

    max_x = x;
    x = 0;
    n_zeros = 0;
    for (i = 0; n_zeros < entries; i++) {
        if (x >= max_x)
            return AVERROR_INVALIDDATA;

        if (pos >= entries) {
            pos = 0;
            level += passes << low_bits;
            passes = 1;
            if (bits[x].bit && bits[x].count > entries - n_zeros)
                passes =  bits[x].count / (entries - n_zeros);
        }

        if (level > 1 << 16)
            return AVERROR_INVALIDDATA;

        if (buf[pos] >= level) {
            if (bits[x].bit)
                buf[pos] += passes << low_bits;
            else
                n_zeros++;

            av_assert1(bits[x].count >= passes);
            bits[x].count -= passes;
            x += bits[x].count == 0;
        }

        pos++;
    }

    for (i = 0; i < entries; i++) {
        if (buf[i] && get_bits1(&s->gb)) {
            buf[i] = -buf[i];
        }
    }

    return 0;
}

static inline int shift_down(int a, int b)
{
    return (a >> b) + (a < 0);
}

static inline int shift(int a, int b)
{
    return a + (1 << b - 1) >> b;
}

#define LATTICE_SHIFT 10
#define SAMPLE_SHIFT   4
#define SAMPLE_FACTOR (1 << SAMPLE_SHIFT)

static int predictor_calc_error(int *k, int *state, int order, int error)
{
    int i, x = error - (unsigned)shift_down(k[order-1] * (unsigned)state[order-1], LATTICE_SHIFT);
    int *k_ptr = &(k[order-2]),
        *state_ptr = &(state[order-2]);

    for (i = order-2; i >= 0; i--, k_ptr--, state_ptr--) {
        unsigned k_value = *k_ptr, state_value = *state_ptr;

        x -= (unsigned) shift_down(k_value * (unsigned)state_value, LATTICE_SHIFT);
        state_ptr[1] = state_value + shift_down(k_value * x, LATTICE_SHIFT);
    }

    // don't drift too far, to avoid overflows
    x = av_clip(x, -(SAMPLE_FACTOR << 16), SAMPLE_FACTOR << 16);

    state[0] = x;

    return x;
}

static void predictor_init_state(int *k, unsigned *state, int order)
{
    for (int i = order - 2; i >= 0; i--) {
        unsigned x = state[i];

        for (int j = 0, p = i + 1; p < order; j++, p++) {
            int tmp = x + shift_down(k[j] * state[p], LATTICE_SHIFT);

            state[p] += shift_down(k[j] * x, LATTICE_SHIFT);
            x = tmp;
        }
    }
}

static int bonk_decode(AVCodecContext *avctx, AVFrame *frame,
                       int *got_frame_ptr, AVPacket *pkt)
{
    BonkContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    const uint8_t *buf;
    int quant, n, buf_size, input_buf_size;
    int ret = AVERROR_INVALIDDATA;

    if ((!pkt->size && !s->bitstream_size) || s->nb_samples == 0) {
        *got_frame_ptr = 0;
        return pkt->size;
    }

    buf_size = FFMIN(pkt->size, s->max_framesize - s->bitstream_size);
    input_buf_size = buf_size;
    if (s->bitstream_index + s->bitstream_size + buf_size + AV_INPUT_BUFFER_PADDING_SIZE > s->max_framesize) {
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

    frame->nb_samples = FFMIN(s->samples_per_packet * s->down_sampling, s->nb_samples);
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        goto fail;

    if ((ret = init_get_bits8(gb, buf, buf_size)) < 0)
        goto fail;

    skip_bits(gb, s->skip);
    if ((ret = intlist_read(s, s->k, s->n_taps, 0)) < 0)
        goto fail;

    for (int i = 0; i < s->n_taps; i++)
        s->k[i] *= s->quant[i];
    quant = s->lossless ? 1 : get_bits(&s->gb, 16) * SAMPLE_FACTOR;

    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        const int samples_per_packet = s->samples_per_packet;
        const int down_sampling = s->down_sampling;
        const int offset = samples_per_packet * down_sampling - 1;
        int *state = s->state[ch];
        int *sample = s->samples[ch];

        predictor_init_state(s->k, state, s->n_taps);
        if ((ret = intlist_read(s, s->input_samples, samples_per_packet, 1)) < 0)
            goto fail;

        for (int i = 0; i < samples_per_packet; i++) {
            for (int j = 0; j < s->down_sampling - 1; j++) {
                sample[0] = predictor_calc_error(s->k, state, s->n_taps, 0);
                sample++;
            }

            sample[0] = predictor_calc_error(s->k, state, s->n_taps, s->input_samples[i] * (unsigned)quant);
            sample++;
        }

        sample = s->samples[ch];
        for (int i = 0; i < s->n_taps; i++)
            state[i] = sample[offset - i];
    }

    if (s->mid_side && avctx->ch_layout.nb_channels == 2) {
        for (int i = 0; i < frame->nb_samples; i++) {
            s->samples[1][i] += shift(s->samples[0][i], 1);
            s->samples[0][i] -= s->samples[1][i];
        }
    }

    if (!s->lossless) {
        for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
            int *samples = s->samples[ch];
            for (int i = 0; i < frame->nb_samples; i++)
                samples[i] = shift(samples[i], 4);
        }
    }

    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        int16_t *osamples = (int16_t *)frame->extended_data[ch];
        int *samples = s->samples[ch];
        for (int i = 0; i < frame->nb_samples; i++)
            osamples[i] = av_clip_int16(samples[i]);
    }

    s->nb_samples -= frame->nb_samples;

    s->skip = get_bits_count(gb) - 8 * (get_bits_count(gb) / 8);
    n = get_bits_count(gb) / 8;

    if (n > buf_size) {
fail:
        s->bitstream_size = 0;
        s->bitstream_index = 0;
        return AVERROR_INVALIDDATA;
    }

    *got_frame_ptr = 1;

    if (s->bitstream_size) {
        s->bitstream_index += n;
        s->bitstream_size  -= n;
        return input_buf_size;
    }
    return n;
}

const FFCodec ff_bonk_decoder = {
    .p.name           = "bonk",
    CODEC_LONG_NAME("Bonk audio"),
    .p.type           = AVMEDIA_TYPE_AUDIO,
    .p.id             = AV_CODEC_ID_BONK,
    .priv_data_size   = sizeof(BonkContext),
    .init             = bonk_init,
    FF_CODEC_DECODE_CB(bonk_decode),
    .close            = bonk_close,
    .p.capabilities   = AV_CODEC_CAP_DELAY |
#if FF_API_SUBFRAMES
                        AV_CODEC_CAP_SUBFRAMES |
#endif
                        AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
    .p.sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                        AV_SAMPLE_FMT_NONE },
};
