/*
 * NellyMoser audio decoder
 * Copyright (c) 2007 a840bda5870ba11f19698ff6eb9581dfb0f95fa5,
 *                    539459aeb7d425140b62a3ec7dbf6dc8e408a306, and
 *                    520e17cd55896441042b14df2566a6eb610ed444
 * Copyright (c) 2007 Loic Minier <lool at dooz.org>
 *                    Benjamin Larsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file nellymoserdec.c
 * The 3 alphanumeric copyright notices are md5summed they are from the original
 * implementors. The original code is available from http://code.google.com/p/nelly2pcm/
 */
#include "avcodec.h"
#include "random.h"
#include "dsputil.h"

#define ALT_BITSTREAM_READER_LE
#include "bitstream.h"

#define NELLY_BANDS       23
#define NELLY_BLOCK_LEN   64
#define NELLY_HEADER_BITS 116
#define NELLY_DETAIL_BITS 198
#define NELLY_BUF_LEN     128
#define NELLY_FILL_LEN    124
#define NELLY_BIT_CAP     6
#define NELLY_BASE_OFF    4228
#define NELLY_BASE_SHIFT  19
#define NELLY_SAMPLES     (2 * NELLY_BUF_LEN)

static const float dequantization_table[127] = {
0.0000000000,-0.8472560048, 0.7224709988, -1.5247479677, -0.4531480074, 0.3753609955, 1.4717899561,
-1.9822579622, -1.1929379702, -0.5829370022, -0.0693780035, 0.3909569979,0.9069200158, 1.4862740040,
 2.2215409279, -2.3887870312, -1.8067539930, -1.4105420113, -1.0773609877, -0.7995010018,-0.5558109879,
-0.3334020078, -0.1324490011, 0.0568020009, 0.2548770010, 0.4773550034, 0.7386850119, 1.0443060398,
1.3954459429, 1.8098750114, 2.3918759823,-2.3893830776, -1.9884680510, -1.7514040470, -1.5643119812,
-1.3922129869,-1.2164649963, -1.0469499826, -0.8905100226, -0.7645580173, -0.6454579830, -0.5259280205,
-0.4059549868, -0.3029719889, -0.2096900046, -0.1239869967, -0.0479229987, 0.0257730000, 0.1001340002,
0.1737180054, 0.2585540116, 0.3522900045, 0.4569880068, 0.5767750144, 0.7003160119, 0.8425520062,
1.0093879700, 1.1821349859, 1.3534560204, 1.5320819616, 1.7332619429, 1.9722349644, 2.3978140354,
-2.5756309032, -2.0573320389, -1.8984919786, -1.7727810144, -1.6662600040, -1.5742180347, -1.4993319511,
-1.4316639900, -1.3652280569, -1.3000990152, -1.2280930281, -1.1588579416, -1.0921250582, -1.0135740042,
-0.9202849865, -0.8287050128, -0.7374889851, -0.6447759867, -0.5590940118, -0.4857139885, -0.4110319912,
-0.3459700048, -0.2851159871, -0.2341620028, -0.1870580018, -0.1442500055, -0.1107169986, -0.0739680007,
-0.0365610011, -0.0073290002, 0.0203610007, 0.0479039997, 0.0751969963, 0.0980999991, 0.1220389977,
0.1458999962, 0.1694349945, 0.1970459968, 0.2252430022, 0.2556869984, 0.2870100141, 0.3197099864,
0.3525829911, 0.3889069855, 0.4334920049, 0.4769459963, 0.5204820037, 0.5644530058, 0.6122040153,
0.6685929894, 0.7341650128, 0.8032159805, 0.8784040213, 0.9566209912, 1.0397069454, 1.1293770075,
1.2211159468, 1.3080279827, 1.4024800062, 1.5056819916, 1.6227730513, 1.7724959850, 1.9430880547,
 2.2903931141
};

static const uint8_t nelly_band_sizes_table[NELLY_BANDS] = {
2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 8, 9, 10, 12, 14, 15
};

static const uint16_t nelly_init_table[64] = {
3134, 5342, 6870, 7792, 8569, 9185, 9744, 10191, 10631, 11061, 11434, 11770,
12116, 12513, 12925, 13300, 13674, 14027, 14352, 14716, 15117, 15477, 15824,
16157, 16513, 16804, 17090, 17401, 17679, 17948, 18238, 18520, 18764, 19078,
19381, 19640, 19921, 20205, 20500, 20813, 21162, 21465, 21794, 22137, 22453,
22756, 23067, 23350, 23636, 23926, 24227, 24521, 24819, 25107, 25414, 25730,
26120, 26497, 26895, 27344, 27877, 28463, 29426, 31355
};

static const int16_t nelly_delta_table[32] = {
-11725, -9420, -7910, -6801, -5948, -5233, -4599, -4039, -3507, -3030, -2596,
-2170, -1774, -1383, -1016, -660, -329, -1, 337, 696, 1085, 1512, 1962, 2433,
2968, 3569, 4314, 5279, 6622, 8154, 10076, 12975
};

typedef struct NellyMoserDecodeContext {
    AVCodecContext* avctx;
    DECLARE_ALIGNED_16(float,float_buf[NELLY_SAMPLES]);
    float           state[64];
    AVRandomState   random_state;
    GetBitContext   gb;
    int             add_bias;
    int             scale_bias;
    DSPContext      dsp;
    MDCTContext     imdct_ctx;
    DECLARE_ALIGNED_16(float,imdct_tmp[NELLY_BUF_LEN]);
    DECLARE_ALIGNED_16(float,imdct_out[NELLY_BUF_LEN * 2]);
} NellyMoserDecodeContext;

static DECLARE_ALIGNED_16(float,sine_window[128]);

static inline int signed_shift(int i, int shift) {
    if (shift > 0)
        return i << shift;
    return i >> -shift;
}

static void overlap_and_window(NellyMoserDecodeContext *s, float *state, float *audio)
{
    int bot, mid_up, mid_down, top;
    float s_bot, s_top;

    bot = 0;
    top = NELLY_BUF_LEN-1;
    mid_up = NELLY_BUF_LEN/2;
    mid_down = (NELLY_BUF_LEN/2)-1;

    while (bot < NELLY_BUF_LEN/4) {
        s_bot = audio[bot];
        s_top = -audio[top];
        audio[bot] =  (-audio[mid_up]*sine_window[bot]-state[bot   ]*sine_window[top])/s->scale_bias + s->add_bias;
        audio[top] = (-state[bot   ]*sine_window[bot]+audio[mid_up]*sine_window[top])/s->scale_bias + s->add_bias;
        state[bot] =  audio[mid_down];

        audio[mid_down] =  (s_top          *sine_window[mid_down]-state[mid_down]*sine_window[mid_up])/s->scale_bias + s->add_bias;
        audio[mid_up  ] = (-state[mid_down]*sine_window[mid_down]-s_top          *sine_window[mid_up])/s->scale_bias + s->add_bias;
        state[mid_down] =  s_bot;

        bot++;
        mid_up++;
        mid_down--;
        top--;
    }
}

static int sum_bits(short *buf, short shift, short off)
{
    int b, i = 0, ret = 0;

    for (i = 0; i < NELLY_FILL_LEN; i++) {
        b = buf[i]-off;
        b = ((b>>(shift-1))+1)>>1;
        ret += av_clip(b, 0, NELLY_BIT_CAP);
    }

    return ret;
}

static int headroom(int *la)
{
    int l;
    if (*la == 0) {
        return 31;
    }
    l = 30 - av_log2(FFABS(*la));
    *la <<= l;
    return l;
}


static void get_sample_bits(const float *buf, int *bits)
{
    int i, j;
    short sbuf[128];
    int bitsum = 0, last_bitsum, small_bitsum, big_bitsum;
    short shift, shift_saved;
    int max, sum, last_off, tmp;
    int big_off, small_off;
    int off;

    max = 0;
    for (i = 0; i < NELLY_FILL_LEN; i++) {
        max = FFMAX(max, buf[i]);
    }
    shift = -16;
    shift += headroom(&max);

    sum = 0;
    for (i = 0; i < NELLY_FILL_LEN; i++) {
        sbuf[i] = signed_shift(buf[i], shift);
        sbuf[i] = (3*sbuf[i])>>2;
        sum += sbuf[i];
    }

    shift += 11;
    shift_saved = shift;
    sum -= NELLY_DETAIL_BITS << shift;
    shift += headroom(&sum);
    small_off = (NELLY_BASE_OFF * (sum>>16)) >> 15;
    shift = shift_saved - (NELLY_BASE_SHIFT+shift-31);

    small_off = signed_shift(small_off, shift);

    bitsum = sum_bits(sbuf, shift_saved, small_off);

    if (bitsum != NELLY_DETAIL_BITS) {
        shift = 0;
        off = bitsum - NELLY_DETAIL_BITS;

        for(shift=0; FFABS(off) <= 16383; shift++)
            off *= 2;

        off = (off * NELLY_BASE_OFF) >> 15;
        shift = shift_saved-(NELLY_BASE_SHIFT+shift-15);

        off = signed_shift(off, shift);

        for (j = 1; j < 20; j++) {
            last_off = small_off;
            small_off += off;
            last_bitsum = bitsum;

            bitsum = sum_bits(sbuf, shift_saved, small_off);

            if ((bitsum-NELLY_DETAIL_BITS) * (last_bitsum-NELLY_DETAIL_BITS) <= 0)
                break;
        }

        if (bitsum > NELLY_DETAIL_BITS) {
            big_off = small_off;
            small_off = last_off;
            big_bitsum=bitsum;
            small_bitsum=last_bitsum;
        } else {
            big_off = last_off;
            big_bitsum=last_bitsum;
            small_bitsum=bitsum;
        }

        while (bitsum != NELLY_DETAIL_BITS && j <= 19) {
            off = (big_off+small_off)>>1;
            bitsum = sum_bits(sbuf, shift_saved, off);
            if (bitsum > NELLY_DETAIL_BITS) {
                big_off=off;
                big_bitsum=bitsum;
            } else {
                small_off = off;
                small_bitsum=bitsum;
            }
            j++;
        }

        if (abs(big_bitsum-NELLY_DETAIL_BITS) >=
            abs(small_bitsum-NELLY_DETAIL_BITS)) {
            bitsum = small_bitsum;
        } else {
            small_off = big_off;
            bitsum = big_bitsum;
        }
    }

    for (i = 0; i < NELLY_FILL_LEN; i++) {
        tmp = sbuf[i]-small_off;
        tmp = ((tmp>>(shift_saved-1))+1)>>1;
        bits[i] = av_clip(tmp, 0, NELLY_BIT_CAP);
    }

    if (bitsum > NELLY_DETAIL_BITS) {
        tmp = i = 0;
        while (tmp < NELLY_DETAIL_BITS) {
            tmp += bits[i];
            i++;
        }

        bits[i-1] -= tmp - NELLY_DETAIL_BITS;
        for(; i < NELLY_FILL_LEN; i++)
            bits[i] = 0;
    }
}

void nelly_decode_block(NellyMoserDecodeContext *s, const unsigned char block[NELLY_BLOCK_LEN], float audio[NELLY_SAMPLES])
{
    int i,j;
    float buf[NELLY_FILL_LEN], pows[NELLY_FILL_LEN];
    float *aptr, *bptr, *pptr, val, pval;
    int bits[NELLY_BUF_LEN];
    unsigned char v;

    init_get_bits(&s->gb, block, NELLY_BLOCK_LEN * 8);

    bptr = buf;
    pptr = pows;
    val = nelly_init_table[get_bits(&s->gb, 6)];
    for (i=0 ; i<NELLY_BANDS ; i++) {
        if (i > 0)
            val += nelly_delta_table[get_bits(&s->gb, 5)];
        pval = pow(2, val/2048);
        for (j = 0; j < nelly_band_sizes_table[i]; j++) {
            *bptr++ = val;
            *pptr++ = pval;
        }

    }

    get_sample_bits(buf, bits);

    for (i = 0; i < 2; i++) {
        aptr = audio + i * NELLY_BUF_LEN;

        init_get_bits(&s->gb, block, NELLY_BLOCK_LEN * 8);
        skip_bits(&s->gb, NELLY_HEADER_BITS + i*NELLY_DETAIL_BITS);

        for (j = 0; j < NELLY_FILL_LEN; j++) {
            if (bits[j] <= 0) {
                aptr[j] = M_SQRT1_2*pows[j];
                if (av_random(&s->random_state) & 1)
                    aptr[j] *= -1.0;
            } else {
                v = get_bits(&s->gb, bits[j]);
                aptr[j] = dequantization_table[(1<<bits[j])-1+v]*pows[j];
            }
        }
        memset(&aptr[NELLY_FILL_LEN], 0,
               (NELLY_BUF_LEN - NELLY_FILL_LEN) * sizeof(float));

        s->imdct_ctx.fft.imdct_calc(&s->imdct_ctx, s->imdct_out,
                                    aptr, s->imdct_tmp);
        /* XXX: overlapping and windowing should be part of a more
           generic imdct function */
        memcpy(&aptr[0],&s->imdct_out[NELLY_BUF_LEN+NELLY_BUF_LEN/2], (NELLY_BUF_LEN/2)*sizeof(float));
        memcpy(&aptr[NELLY_BUF_LEN / 2],&s->imdct_out[0],(NELLY_BUF_LEN/2)*sizeof(float));
        overlap_and_window(s, s->state, aptr);
    }
}

static av_cold int decode_init(AVCodecContext * avctx) {
    NellyMoserDecodeContext *s = avctx->priv_data;
    int i;

    s->avctx = avctx;
    av_init_random(0, &s->random_state);
    ff_mdct_init(&s->imdct_ctx, 8, 1);

    dsputil_init(&s->dsp, avctx);

    if(s->dsp.float_to_int16 == ff_float_to_int16_c) {
        s->add_bias = 385;
        s->scale_bias = 8*32768;
    } else {
        s->add_bias = 0;
        s->scale_bias = 1*8;
    }

    /* Generate overlap window */
    if (!sine_window[0])
        for (i=0 ; i<128; i++) {
            sine_window[i] = sin((i + 0.5) / 256.0 * M_PI);
        }

    return 0;
}

static int decode_tag(AVCodecContext * avctx,
                      void *data, int *data_size,
                      const uint8_t * buf, int buf_size) {
    NellyMoserDecodeContext *s = avctx->priv_data;
    int blocks, i;
    int16_t* samples;
    *data_size = 0;
    samples = (int16_t*)data;

    if (buf_size < avctx->block_align)
        return buf_size;

    switch (buf_size) {
        case 64:    // 8000Hz
            blocks = 1; break;
        case 128:   // 11025Hz
            blocks = 2; break;
        case 256:   // 22050Hz
            blocks = 4; break;
        case 512:   // 44100Hz
            blocks = 8; break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Tag size %d unknown, report sample!\n", buf_size);
            return buf_size;
    }

    for (i=0 ; i<blocks ; i++) {
        nelly_decode_block(s, &buf[i*NELLY_BLOCK_LEN], s->float_buf);
        s->dsp.float_to_int16(&samples[i*NELLY_SAMPLES], s->float_buf, NELLY_SAMPLES);
        *data_size += NELLY_SAMPLES*sizeof(int16_t);
    }

    return buf_size;
}

static av_cold int decode_end(AVCodecContext * avctx) {
    NellyMoserDecodeContext *s = avctx->priv_data;

    ff_mdct_end(&s->imdct_ctx);
    return 0;
}

AVCodec nellymoser_decoder = {
    "nellymoser",
    CODEC_TYPE_AUDIO,
    CODEC_ID_NELLYMOSER,
    sizeof(NellyMoserDecodeContext),
    decode_init,
    NULL,
    decode_end,
    decode_tag,
};

