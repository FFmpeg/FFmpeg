/*
 * Simple free lossless/lossy audio codec
 * Copyright (c) 2004 Alex Beregszaszi
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
#include "avcodec.h"
#include "get_bits.h"
#include "golomb.h"
#include "internal.h"
#include "rangecoder.h"


/**
 * @file
 * Simple free lossless/lossy audio codec
 * Based on Paul Francis Harrison's Bonk (http://www.logarithmic.net/pfh/bonk)
 * Written and designed by Alex Beregszaszi
 *
 * TODO:
 *  - CABAC put/get_symbol
 *  - independent quantizer for channels
 *  - >2 channels support
 *  - more decorrelation types
 *  - more tap_quant tests
 *  - selectable intlist writers/readers (bonk-style, golomb, cabac)
 */

#define MAX_CHANNELS 2

#define MID_SIDE 0
#define LEFT_SIDE 1
#define RIGHT_SIDE 2

typedef struct SonicContext {
    int version;
    int minor_version;
    int lossless, decorrelation;

    int num_taps, downsampling;
    double quantization;

    int channels, samplerate, block_align, frame_size;

    int *tap_quant;
    int *int_samples;
    int *coded_samples[MAX_CHANNELS];

    // for encoding
    int *tail;
    int tail_size;
    int *window;
    int window_size;

    // for decoding
    int *predictor_k;
    int *predictor_state[MAX_CHANNELS];
} SonicContext;

#define LATTICE_SHIFT   10
#define SAMPLE_SHIFT    4
#define LATTICE_FACTOR  (1 << LATTICE_SHIFT)
#define SAMPLE_FACTOR   (1 << SAMPLE_SHIFT)

#define BASE_QUANT      0.6
#define RATE_VARIATION  3.0

static inline int shift(int a,int b)
{
    return (a+(1<<(b-1))) >> b;
}

static inline int shift_down(int a,int b)
{
    return (a>>b)+(a<0);
}

static av_always_inline av_flatten void put_symbol(RangeCoder *c, uint8_t *state, int v, int is_signed, uint64_t rc_stat[256][2], uint64_t rc_stat2[32][2]){
    int i;

#define put_rac(C,S,B) \
do{\
    if(rc_stat){\
        rc_stat[*(S)][B]++;\
        rc_stat2[(S)-state][B]++;\
    }\
    put_rac(C,S,B);\
}while(0)

    if(v){
        const int a= FFABS(v);
        const int e= av_log2(a);
        put_rac(c, state+0, 0);
        if(e<=9){
            for(i=0; i<e; i++){
                put_rac(c, state+1+i, 1);  //1..10
            }
            put_rac(c, state+1+i, 0);

            for(i=e-1; i>=0; i--){
                put_rac(c, state+22+i, (a>>i)&1); //22..31
            }

            if(is_signed)
                put_rac(c, state+11 + e, v < 0); //11..21
        }else{
            for(i=0; i<e; i++){
                put_rac(c, state+1+FFMIN(i,9), 1);  //1..10
            }
            put_rac(c, state+1+9, 0);

            for(i=e-1; i>=0; i--){
                put_rac(c, state+22+FFMIN(i,9), (a>>i)&1); //22..31
            }

            if(is_signed)
                put_rac(c, state+11 + 10, v < 0); //11..21
        }
    }else{
        put_rac(c, state+0, 1);
    }
#undef put_rac
}

static inline av_flatten int get_symbol(RangeCoder *c, uint8_t *state, int is_signed){
    if(get_rac(c, state+0))
        return 0;
    else{
        int i, e;
        unsigned a;
        e= 0;
        while(get_rac(c, state+1 + FFMIN(e,9))){ //1..10
            e++;
            if (e > 31)
                return AVERROR_INVALIDDATA;
        }

        a= 1;
        for(i=e-1; i>=0; i--){
            a += a + get_rac(c, state+22 + FFMIN(i,9)); //22..31
        }

        e= -(is_signed && get_rac(c, state+11 + FFMIN(e, 10))); //11..21
        return (a^e)-e;
    }
}

#if 1
static inline int intlist_write(RangeCoder *c, uint8_t *state, int *buf, int entries, int base_2_part)
{
    int i;

    for (i = 0; i < entries; i++)
        put_symbol(c, state, buf[i], 1, NULL, NULL);

    return 1;
}

static inline int intlist_read(RangeCoder *c, uint8_t *state, int *buf, int entries, int base_2_part)
{
    int i;

    for (i = 0; i < entries; i++)
        buf[i] = get_symbol(c, state, 1);

    return 1;
}
#elif 1
static inline int intlist_write(PutBitContext *pb, int *buf, int entries, int base_2_part)
{
    int i;

    for (i = 0; i < entries; i++)
        set_se_golomb(pb, buf[i]);

    return 1;
}

static inline int intlist_read(GetBitContext *gb, int *buf, int entries, int base_2_part)
{
    int i;

    for (i = 0; i < entries; i++)
        buf[i] = get_se_golomb(gb);

    return 1;
}

#else

#define ADAPT_LEVEL 8

static int bits_to_store(uint64_t x)
{
    int res = 0;

    while(x)
    {
        res++;
        x >>= 1;
    }
    return res;
}

static void write_uint_max(PutBitContext *pb, unsigned int value, unsigned int max)
{
    int i, bits;

    if (!max)
        return;

    bits = bits_to_store(max);

    for (i = 0; i < bits-1; i++)
        put_bits(pb, 1, value & (1 << i));

    if ( (value | (1 << (bits-1))) <= max)
        put_bits(pb, 1, value & (1 << (bits-1)));
}

static unsigned int read_uint_max(GetBitContext *gb, int max)
{
    int i, bits, value = 0;

    if (!max)
        return 0;

    bits = bits_to_store(max);

    for (i = 0; i < bits-1; i++)
        if (get_bits1(gb))
            value += 1 << i;

    if ( (value | (1<<(bits-1))) <= max)
        if (get_bits1(gb))
            value += 1 << (bits-1);

    return value;
}

static int intlist_write(PutBitContext *pb, int *buf, int entries, int base_2_part)
{
    int i, j, x = 0, low_bits = 0, max = 0;
    int step = 256, pos = 0, dominant = 0, any = 0;
    int *copy, *bits;

    copy = av_calloc(entries, sizeof(*copy));
    if (!copy)
        return AVERROR(ENOMEM);

    if (base_2_part)
    {
        int energy = 0;

        for (i = 0; i < entries; i++)
            energy += abs(buf[i]);

        low_bits = bits_to_store(energy / (entries * 2));
        if (low_bits > 15)
            low_bits = 15;

        put_bits(pb, 4, low_bits);
    }

    for (i = 0; i < entries; i++)
    {
        put_bits(pb, low_bits, abs(buf[i]));
        copy[i] = abs(buf[i]) >> low_bits;
        if (copy[i] > max)
            max = abs(copy[i]);
    }

    bits = av_calloc(entries*max, sizeof(*bits));
    if (!bits)
    {
        av_free(copy);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i <= max; i++)
    {
        for (j = 0; j < entries; j++)
            if (copy[j] >= i)
                bits[x++] = copy[j] > i;
    }

    // store bitstream
    while (pos < x)
    {
        int steplet = step >> 8;

        if (pos + steplet > x)
            steplet = x - pos;

        for (i = 0; i < steplet; i++)
            if (bits[i+pos] != dominant)
                any = 1;

        put_bits(pb, 1, any);

        if (!any)
        {
            pos += steplet;
            step += step / ADAPT_LEVEL;
        }
        else
        {
            int interloper = 0;

            while (((pos + interloper) < x) && (bits[pos + interloper] == dominant))
                interloper++;

            // note change
            write_uint_max(pb, interloper, (step >> 8) - 1);

            pos += interloper + 1;
            step -= step / ADAPT_LEVEL;
        }

        if (step < 256)
        {
            step = 65536 / step;
            dominant = !dominant;
        }
    }

    // store signs
    for (i = 0; i < entries; i++)
        if (buf[i])
            put_bits(pb, 1, buf[i] < 0);

    av_free(bits);
    av_free(copy);

    return 0;
}

static int intlist_read(GetBitContext *gb, int *buf, int entries, int base_2_part)
{
    int i, low_bits = 0, x = 0;
    int n_zeros = 0, step = 256, dominant = 0;
    int pos = 0, level = 0;
    int *bits = av_calloc(entries, sizeof(*bits));

    if (!bits)
        return AVERROR(ENOMEM);

    if (base_2_part)
    {
        low_bits = get_bits(gb, 4);

        if (low_bits)
            for (i = 0; i < entries; i++)
                buf[i] = get_bits(gb, low_bits);
    }

//    av_log(NULL, AV_LOG_INFO, "entries: %d, low bits: %d\n", entries, low_bits);

    while (n_zeros < entries)
    {
        int steplet = step >> 8;

        if (!get_bits1(gb))
        {
            for (i = 0; i < steplet; i++)
                bits[x++] = dominant;

            if (!dominant)
                n_zeros += steplet;

            step += step / ADAPT_LEVEL;
        }
        else
        {
            int actual_run = read_uint_max(gb, steplet-1);

//            av_log(NULL, AV_LOG_INFO, "actual run: %d\n", actual_run);

            for (i = 0; i < actual_run; i++)
                bits[x++] = dominant;

            bits[x++] = !dominant;

            if (!dominant)
                n_zeros += actual_run;
            else
                n_zeros++;

            step -= step / ADAPT_LEVEL;
        }

        if (step < 256)
        {
            step = 65536 / step;
            dominant = !dominant;
        }
    }

    // reconstruct unsigned values
    n_zeros = 0;
    for (i = 0; n_zeros < entries; i++)
    {
        while(1)
        {
            if (pos >= entries)
            {
                pos = 0;
                level += 1 << low_bits;
            }

            if (buf[pos] >= level)
                break;

            pos++;
        }

        if (bits[i])
            buf[pos] += 1 << low_bits;
        else
            n_zeros++;

        pos++;
    }
    av_free(bits);

    // read signs
    for (i = 0; i < entries; i++)
        if (buf[i] && get_bits1(gb))
            buf[i] = -buf[i];

//    av_log(NULL, AV_LOG_INFO, "zeros: %d pos: %d\n", n_zeros, pos);

    return 0;
}
#endif

static void predictor_init_state(int *k, int *state, int order)
{
    int i;

    for (i = order-2; i >= 0; i--)
    {
        int j, p, x = state[i];

        for (j = 0, p = i+1; p < order; j++,p++)
            {
            int tmp = x + shift_down(k[j] * state[p], LATTICE_SHIFT);
            state[p] += shift_down(k[j]*x, LATTICE_SHIFT);
            x = tmp;
        }
    }
}

static int predictor_calc_error(int *k, int *state, int order, int error)
{
    int i, x = error - shift_down(k[order-1] * state[order-1], LATTICE_SHIFT);

#if 1
    int *k_ptr = &(k[order-2]),
        *state_ptr = &(state[order-2]);
    for (i = order-2; i >= 0; i--, k_ptr--, state_ptr--)
    {
        int k_value = *k_ptr, state_value = *state_ptr;
        x -= shift_down(k_value * (unsigned)state_value, LATTICE_SHIFT);
        state_ptr[1] = state_value + shift_down(k_value * (unsigned)x, LATTICE_SHIFT);
    }
#else
    for (i = order-2; i >= 0; i--)
    {
        x -= shift_down(k[i] * state[i], LATTICE_SHIFT);
        state[i+1] = state[i] + shift_down(k[i] * x, LATTICE_SHIFT);
    }
#endif

    // don't drift too far, to avoid overflows
    if (x >  (SAMPLE_FACTOR<<16)) x =  (SAMPLE_FACTOR<<16);
    if (x < -(SAMPLE_FACTOR<<16)) x = -(SAMPLE_FACTOR<<16);

    state[0] = x;

    return x;
}

#if CONFIG_SONIC_ENCODER || CONFIG_SONIC_LS_ENCODER
// Heavily modified Levinson-Durbin algorithm which
// copes better with quantization, and calculates the
// actual whitened result as it goes.

static int modified_levinson_durbin(int *window, int window_entries,
        int *out, int out_entries, int channels, int *tap_quant)
{
    int i;
    int *state = av_calloc(window_entries, sizeof(*state));

    if (!state)
        return AVERROR(ENOMEM);

    memcpy(state, window, 4* window_entries);

    for (i = 0; i < out_entries; i++)
    {
        int step = (i+1)*channels, k, j;
        double xx = 0.0, xy = 0.0;
#if 1
        int *x_ptr = &(window[step]);
        int *state_ptr = &(state[0]);
        j = window_entries - step;
        for (;j>0;j--,x_ptr++,state_ptr++)
        {
            double x_value = *x_ptr;
            double state_value = *state_ptr;
            xx += state_value*state_value;
            xy += x_value*state_value;
        }
#else
        for (j = 0; j <= (window_entries - step); j++);
        {
            double stepval = window[step+j];
            double stateval = window[j];
//            xx += (double)window[j]*(double)window[j];
//            xy += (double)window[step+j]*(double)window[j];
            xx += stateval*stateval;
            xy += stepval*stateval;
        }
#endif
        if (xx == 0.0)
            k = 0;
        else
            k = (int)(floor(-xy/xx * (double)LATTICE_FACTOR / (double)(tap_quant[i]) + 0.5));

        if (k > (LATTICE_FACTOR/tap_quant[i]))
            k = LATTICE_FACTOR/tap_quant[i];
        if (-k > (LATTICE_FACTOR/tap_quant[i]))
            k = -(LATTICE_FACTOR/tap_quant[i]);

        out[i] = k;
        k *= tap_quant[i];

#if 1
        x_ptr = &(window[step]);
        state_ptr = &(state[0]);
        j = window_entries - step;
        for (;j>0;j--,x_ptr++,state_ptr++)
        {
            int x_value = *x_ptr;
            int state_value = *state_ptr;
            *x_ptr = x_value + shift_down(k*state_value,LATTICE_SHIFT);
            *state_ptr = state_value + shift_down(k*x_value, LATTICE_SHIFT);
        }
#else
        for (j=0; j <= (window_entries - step); j++)
        {
            int stepval = window[step+j];
            int stateval=state[j];
            window[step+j] += shift_down(k * stateval, LATTICE_SHIFT);
            state[j] += shift_down(k * stepval, LATTICE_SHIFT);
        }
#endif
    }

    av_free(state);
    return 0;
}

static inline int code_samplerate(int samplerate)
{
    switch (samplerate)
    {
        case 44100: return 0;
        case 22050: return 1;
        case 11025: return 2;
        case 96000: return 3;
        case 48000: return 4;
        case 32000: return 5;
        case 24000: return 6;
        case 16000: return 7;
        case 8000: return 8;
    }
    return AVERROR(EINVAL);
}

static av_cold int sonic_encode_init(AVCodecContext *avctx)
{
    SonicContext *s = avctx->priv_data;
    PutBitContext pb;
    int i;

    s->version = 2;

    if (avctx->channels > MAX_CHANNELS)
    {
        av_log(avctx, AV_LOG_ERROR, "Only mono and stereo streams are supported by now\n");
        return AVERROR(EINVAL); /* only stereo or mono for now */
    }

    if (avctx->channels == 2)
        s->decorrelation = MID_SIDE;
    else
        s->decorrelation = 3;

    if (avctx->codec->id == AV_CODEC_ID_SONIC_LS)
    {
        s->lossless = 1;
        s->num_taps = 32;
        s->downsampling = 1;
        s->quantization = 0.0;
    }
    else
    {
        s->num_taps = 128;
        s->downsampling = 2;
        s->quantization = 1.0;
    }

    // max tap 2048
    if (s->num_taps < 32 || s->num_taps > 1024 || s->num_taps % 32) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of taps\n");
        return AVERROR_INVALIDDATA;
    }

    // generate taps
    s->tap_quant = av_calloc(s->num_taps, sizeof(*s->tap_quant));
    if (!s->tap_quant)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->num_taps; i++)
        s->tap_quant[i] = ff_sqrt(i+1);

    s->channels = avctx->channels;
    s->samplerate = avctx->sample_rate;

    s->block_align = 2048LL*s->samplerate/(44100*s->downsampling);
    s->frame_size = s->channels*s->block_align*s->downsampling;

    s->tail_size = s->num_taps*s->channels;
    s->tail = av_calloc(s->tail_size, sizeof(*s->tail));
    if (!s->tail)
        return AVERROR(ENOMEM);

    s->predictor_k = av_calloc(s->num_taps, sizeof(*s->predictor_k) );
    if (!s->predictor_k)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->channels; i++)
    {
        s->coded_samples[i] = av_calloc(s->block_align, sizeof(**s->coded_samples));
        if (!s->coded_samples[i])
            return AVERROR(ENOMEM);
    }

    s->int_samples = av_calloc(s->frame_size, sizeof(*s->int_samples));

    s->window_size = ((2*s->tail_size)+s->frame_size);
    s->window = av_calloc(s->window_size, sizeof(*s->window));
    if (!s->window || !s->int_samples)
        return AVERROR(ENOMEM);

    avctx->extradata = av_mallocz(16);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);
    init_put_bits(&pb, avctx->extradata, 16*8);

    put_bits(&pb, 2, s->version); // version
    if (s->version >= 1)
    {
        if (s->version >= 2) {
            put_bits(&pb, 8, s->version);
            put_bits(&pb, 8, s->minor_version);
        }
        put_bits(&pb, 2, s->channels);
        put_bits(&pb, 4, code_samplerate(s->samplerate));
    }
    put_bits(&pb, 1, s->lossless);
    if (!s->lossless)
        put_bits(&pb, 3, SAMPLE_SHIFT); // XXX FIXME: sample precision
    put_bits(&pb, 2, s->decorrelation);
    put_bits(&pb, 2, s->downsampling);
    put_bits(&pb, 5, (s->num_taps >> 5)-1); // 32..1024
    put_bits(&pb, 1, 0); // XXX FIXME: no custom tap quant table

    flush_put_bits(&pb);
    avctx->extradata_size = put_bits_count(&pb)/8;

    av_log(avctx, AV_LOG_INFO, "Sonic: ver: %d.%d ls: %d dr: %d taps: %d block: %d frame: %d downsamp: %d\n",
        s->version, s->minor_version, s->lossless, s->decorrelation, s->num_taps, s->block_align, s->frame_size, s->downsampling);

    avctx->frame_size = s->block_align*s->downsampling;

    return 0;
}

static av_cold int sonic_encode_close(AVCodecContext *avctx)
{
    SonicContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < s->channels; i++)
        av_freep(&s->coded_samples[i]);

    av_freep(&s->predictor_k);
    av_freep(&s->tail);
    av_freep(&s->tap_quant);
    av_freep(&s->window);
    av_freep(&s->int_samples);

    return 0;
}

static int sonic_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                              const AVFrame *frame, int *got_packet_ptr)
{
    SonicContext *s = avctx->priv_data;
    RangeCoder c;
    int i, j, ch, quant = 0, x = 0;
    int ret;
    const short *samples = (const int16_t*)frame->data[0];
    uint8_t state[32];

    if ((ret = ff_alloc_packet2(avctx, avpkt, s->frame_size * 5 + 1000, 0)) < 0)
        return ret;

    ff_init_range_encoder(&c, avpkt->data, avpkt->size);
    ff_build_rac_states(&c, 0.05*(1LL<<32), 256-8);
    memset(state, 128, sizeof(state));

    // short -> internal
    for (i = 0; i < s->frame_size; i++)
        s->int_samples[i] = samples[i];

    if (!s->lossless)
        for (i = 0; i < s->frame_size; i++)
            s->int_samples[i] = s->int_samples[i] << SAMPLE_SHIFT;

    switch(s->decorrelation)
    {
        case MID_SIDE:
            for (i = 0; i < s->frame_size; i += s->channels)
            {
                s->int_samples[i] += s->int_samples[i+1];
                s->int_samples[i+1] -= shift(s->int_samples[i], 1);
            }
            break;
        case LEFT_SIDE:
            for (i = 0; i < s->frame_size; i += s->channels)
                s->int_samples[i+1] -= s->int_samples[i];
            break;
        case RIGHT_SIDE:
            for (i = 0; i < s->frame_size; i += s->channels)
                s->int_samples[i] -= s->int_samples[i+1];
            break;
    }

    memset(s->window, 0, 4* s->window_size);

    for (i = 0; i < s->tail_size; i++)
        s->window[x++] = s->tail[i];

    for (i = 0; i < s->frame_size; i++)
        s->window[x++] = s->int_samples[i];

    for (i = 0; i < s->tail_size; i++)
        s->window[x++] = 0;

    for (i = 0; i < s->tail_size; i++)
        s->tail[i] = s->int_samples[s->frame_size - s->tail_size + i];

    // generate taps
    ret = modified_levinson_durbin(s->window, s->window_size,
                s->predictor_k, s->num_taps, s->channels, s->tap_quant);
    if (ret < 0)
        return ret;

    if ((ret = intlist_write(&c, state, s->predictor_k, s->num_taps, 0)) < 0)
        return ret;

    for (ch = 0; ch < s->channels; ch++)
    {
        x = s->tail_size+ch;
        for (i = 0; i < s->block_align; i++)
        {
            int sum = 0;
            for (j = 0; j < s->downsampling; j++, x += s->channels)
                sum += s->window[x];
            s->coded_samples[ch][i] = sum;
        }
    }

    // simple rate control code
    if (!s->lossless)
    {
        double energy1 = 0.0, energy2 = 0.0;
        for (ch = 0; ch < s->channels; ch++)
        {
            for (i = 0; i < s->block_align; i++)
            {
                double sample = s->coded_samples[ch][i];
                energy2 += sample*sample;
                energy1 += fabs(sample);
            }
        }

        energy2 = sqrt(energy2/(s->channels*s->block_align));
        energy1 = M_SQRT2*energy1/(s->channels*s->block_align);

        // increase bitrate when samples are like a gaussian distribution
        // reduce bitrate when samples are like a two-tailed exponential distribution

        if (energy2 > energy1)
            energy2 += (energy2-energy1)*RATE_VARIATION;

        quant = (int)(BASE_QUANT*s->quantization*energy2/SAMPLE_FACTOR);
//        av_log(avctx, AV_LOG_DEBUG, "quant: %d energy: %f / %f\n", quant, energy1, energy2);

        quant = av_clip(quant, 1, 65534);

        put_symbol(&c, state, quant, 0, NULL, NULL);

        quant *= SAMPLE_FACTOR;
    }

    // write out coded samples
    for (ch = 0; ch < s->channels; ch++)
    {
        if (!s->lossless)
            for (i = 0; i < s->block_align; i++)
                s->coded_samples[ch][i] = ROUNDED_DIV(s->coded_samples[ch][i], quant);

        if ((ret = intlist_write(&c, state, s->coded_samples[ch], s->block_align, 1)) < 0)
            return ret;
    }

//    av_log(avctx, AV_LOG_DEBUG, "used bytes: %d\n", (put_bits_count(&pb)+7)/8);

    avpkt->size = ff_rac_terminate(&c, 0);
    *got_packet_ptr = 1;
    return 0;

}
#endif /* CONFIG_SONIC_ENCODER || CONFIG_SONIC_LS_ENCODER */

#if CONFIG_SONIC_DECODER
static const int samplerate_table[] =
    { 44100, 22050, 11025, 96000, 48000, 32000, 24000, 16000, 8000 };

static av_cold int sonic_decode_init(AVCodecContext *avctx)
{
    SonicContext *s = avctx->priv_data;
    GetBitContext gb;
    int i;
    int ret;

    s->channels = avctx->channels;
    s->samplerate = avctx->sample_rate;

    if (!avctx->extradata)
    {
        av_log(avctx, AV_LOG_ERROR, "No mandatory headers present\n");
        return AVERROR_INVALIDDATA;
    }

    ret = init_get_bits8(&gb, avctx->extradata, avctx->extradata_size);
    if (ret < 0)
        return ret;

    s->version = get_bits(&gb, 2);
    if (s->version >= 2) {
        s->version       = get_bits(&gb, 8);
        s->minor_version = get_bits(&gb, 8);
    }
    if (s->version != 2)
    {
        av_log(avctx, AV_LOG_ERROR, "Unsupported Sonic version, please report\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->version >= 1)
    {
        int sample_rate_index;
        s->channels = get_bits(&gb, 2);
        sample_rate_index = get_bits(&gb, 4);
        if (sample_rate_index >= FF_ARRAY_ELEMS(samplerate_table)) {
            av_log(avctx, AV_LOG_ERROR, "Invalid sample_rate_index %d\n", sample_rate_index);
            return AVERROR_INVALIDDATA;
        }
        s->samplerate = samplerate_table[sample_rate_index];
        av_log(avctx, AV_LOG_INFO, "Sonicv2 chans: %d samprate: %d\n",
            s->channels, s->samplerate);
    }

    if (s->channels > MAX_CHANNELS || s->channels < 1)
    {
        av_log(avctx, AV_LOG_ERROR, "Only mono and stereo streams are supported by now\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->channels = s->channels;

    s->lossless = get_bits1(&gb);
    if (!s->lossless)
        skip_bits(&gb, 3); // XXX FIXME
    s->decorrelation = get_bits(&gb, 2);
    if (s->decorrelation != 3 && s->channels != 2) {
        av_log(avctx, AV_LOG_ERROR, "invalid decorrelation %d\n", s->decorrelation);
        return AVERROR_INVALIDDATA;
    }

    s->downsampling = get_bits(&gb, 2);
    if (!s->downsampling) {
        av_log(avctx, AV_LOG_ERROR, "invalid downsampling value\n");
        return AVERROR_INVALIDDATA;
    }

    s->num_taps = (get_bits(&gb, 5)+1)<<5;
    if (get_bits1(&gb)) // XXX FIXME
        av_log(avctx, AV_LOG_INFO, "Custom quant table\n");

    s->block_align = 2048LL*s->samplerate/(44100*s->downsampling);
    s->frame_size = s->channels*s->block_align*s->downsampling;
//    avctx->frame_size = s->block_align;

    if (s->num_taps * s->channels > s->frame_size) {
        av_log(avctx, AV_LOG_ERROR,
               "number of taps times channels (%d * %d) larger than frame size %d\n",
               s->num_taps, s->channels, s->frame_size);
        return AVERROR_INVALIDDATA;
    }

    av_log(avctx, AV_LOG_INFO, "Sonic: ver: %d.%d ls: %d dr: %d taps: %d block: %d frame: %d downsamp: %d\n",
        s->version, s->minor_version, s->lossless, s->decorrelation, s->num_taps, s->block_align, s->frame_size, s->downsampling);

    // generate taps
    s->tap_quant = av_calloc(s->num_taps, sizeof(*s->tap_quant));
    if (!s->tap_quant)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->num_taps; i++)
        s->tap_quant[i] = ff_sqrt(i+1);

    s->predictor_k = av_calloc(s->num_taps, sizeof(*s->predictor_k));

    for (i = 0; i < s->channels; i++)
    {
        s->predictor_state[i] = av_calloc(s->num_taps, sizeof(**s->predictor_state));
        if (!s->predictor_state[i])
            return AVERROR(ENOMEM);
    }

    for (i = 0; i < s->channels; i++)
    {
        s->coded_samples[i] = av_calloc(s->block_align, sizeof(**s->coded_samples));
        if (!s->coded_samples[i])
            return AVERROR(ENOMEM);
    }
    s->int_samples = av_calloc(s->frame_size, sizeof(*s->int_samples));
    if (!s->int_samples)
        return AVERROR(ENOMEM);

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    return 0;
}

static av_cold int sonic_decode_close(AVCodecContext *avctx)
{
    SonicContext *s = avctx->priv_data;
    int i;

    av_freep(&s->int_samples);
    av_freep(&s->tap_quant);
    av_freep(&s->predictor_k);

    for (i = 0; i < s->channels; i++)
    {
        av_freep(&s->predictor_state[i]);
        av_freep(&s->coded_samples[i]);
    }

    return 0;
}

static int sonic_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame_ptr,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    SonicContext *s = avctx->priv_data;
    RangeCoder c;
    uint8_t state[32];
    int i, quant, ch, j, ret;
    int16_t *samples;
    AVFrame *frame = data;

    if (buf_size == 0) return 0;

    frame->nb_samples = s->frame_size / avctx->channels;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    samples = (int16_t *)frame->data[0];

//    av_log(NULL, AV_LOG_INFO, "buf_size: %d\n", buf_size);

    memset(state, 128, sizeof(state));
    ff_init_range_decoder(&c, buf, buf_size);
    ff_build_rac_states(&c, 0.05*(1LL<<32), 256-8);

    intlist_read(&c, state, s->predictor_k, s->num_taps, 0);

    // dequantize
    for (i = 0; i < s->num_taps; i++)
        s->predictor_k[i] *= s->tap_quant[i];

    if (s->lossless)
        quant = 1;
    else
        quant = get_symbol(&c, state, 0) * SAMPLE_FACTOR;

//    av_log(NULL, AV_LOG_INFO, "quant: %d\n", quant);

    for (ch = 0; ch < s->channels; ch++)
    {
        int x = ch;

        predictor_init_state(s->predictor_k, s->predictor_state[ch], s->num_taps);

        intlist_read(&c, state, s->coded_samples[ch], s->block_align, 1);

        for (i = 0; i < s->block_align; i++)
        {
            for (j = 0; j < s->downsampling - 1; j++)
            {
                s->int_samples[x] = predictor_calc_error(s->predictor_k, s->predictor_state[ch], s->num_taps, 0);
                x += s->channels;
            }

            s->int_samples[x] = predictor_calc_error(s->predictor_k, s->predictor_state[ch], s->num_taps, s->coded_samples[ch][i] * (unsigned)quant);
            x += s->channels;
        }

        for (i = 0; i < s->num_taps; i++)
            s->predictor_state[ch][i] = s->int_samples[s->frame_size - s->channels + ch - i*s->channels];
    }

    switch(s->decorrelation)
    {
        case MID_SIDE:
            for (i = 0; i < s->frame_size; i += s->channels)
            {
                s->int_samples[i+1] += shift(s->int_samples[i], 1);
                s->int_samples[i] -= s->int_samples[i+1];
            }
            break;
        case LEFT_SIDE:
            for (i = 0; i < s->frame_size; i += s->channels)
                s->int_samples[i+1] += s->int_samples[i];
            break;
        case RIGHT_SIDE:
            for (i = 0; i < s->frame_size; i += s->channels)
                s->int_samples[i] += s->int_samples[i+1];
            break;
    }

    if (!s->lossless)
        for (i = 0; i < s->frame_size; i++)
            s->int_samples[i] = shift(s->int_samples[i], SAMPLE_SHIFT);

    // internal -> short
    for (i = 0; i < s->frame_size; i++)
        samples[i] = av_clip_int16(s->int_samples[i]);

    *got_frame_ptr = 1;

    return buf_size;
}

AVCodec ff_sonic_decoder = {
    .name           = "sonic",
    .long_name      = NULL_IF_CONFIG_SMALL("Sonic"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_SONIC,
    .priv_data_size = sizeof(SonicContext),
    .init           = sonic_decode_init,
    .close          = sonic_decode_close,
    .decode         = sonic_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_EXPERIMENTAL,
};
#endif /* CONFIG_SONIC_DECODER */

#if CONFIG_SONIC_ENCODER
AVCodec ff_sonic_encoder = {
    .name           = "sonic",
    .long_name      = NULL_IF_CONFIG_SMALL("Sonic"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_SONIC,
    .priv_data_size = sizeof(SonicContext),
    .init           = sonic_encode_init,
    .encode2        = sonic_encode_frame,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE },
    .capabilities   = AV_CODEC_CAP_EXPERIMENTAL,
    .close          = sonic_encode_close,
};
#endif

#if CONFIG_SONIC_LS_ENCODER
AVCodec ff_sonic_ls_encoder = {
    .name           = "sonicls",
    .long_name      = NULL_IF_CONFIG_SMALL("Sonic lossless"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_SONIC_LS,
    .priv_data_size = sizeof(SonicContext),
    .init           = sonic_encode_init,
    .encode2        = sonic_encode_frame,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE },
    .capabilities   = AV_CODEC_CAP_EXPERIMENTAL,
    .close          = sonic_encode_close,
};
#endif
