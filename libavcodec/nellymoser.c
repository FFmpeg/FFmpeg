/*
 * Common code between Nellymoser encoder and decoder
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
 * @file
 * The 3 alphanumeric copyright notices are md5summed they are from the original
 * implementors. The original code is available from http://code.google.com/p/nelly2pcm/
 */

#include "nellymoser.h"
#include "avcodec.h"
#include "dsputil.h"

#define BITSTREAM_READER_LE
#include "get_bits.h"

const float ff_nelly_dequantization_table[127] = {
 0.0000000000,

-0.8472560048, 0.7224709988,

-1.5247479677,-0.4531480074, 0.3753609955, 1.4717899561,

-1.9822579622,-1.1929379702,-0.5829370022,-0.0693780035, 0.3909569979, 0.9069200158, 1.4862740040, 2.2215409279,

-2.3887870312,-1.8067539930,-1.4105420113,-1.0773609877,-0.7995010018,-0.5558109879,-0.3334020078,-0.1324490011,
 0.0568020009, 0.2548770010, 0.4773550034, 0.7386850119, 1.0443060398, 1.3954459429, 1.8098750114, 2.3918759823,

-2.3893830776,-1.9884680510,-1.7514040470,-1.5643119812,-1.3922129869,-1.2164649963,-1.0469499826,-0.8905100226,
-0.7645580173,-0.6454579830,-0.5259280205,-0.4059549868,-0.3029719889,-0.2096900046,-0.1239869967,-0.0479229987,
 0.0257730000, 0.1001340002, 0.1737180054, 0.2585540116, 0.3522900045, 0.4569880068, 0.5767750144, 0.7003160119,
 0.8425520062, 1.0093879700, 1.1821349859, 1.3534560204, 1.5320819616, 1.7332619429, 1.9722349644, 2.3978140354,

-2.5756309032,-2.0573320389,-1.8984919786,-1.7727810144,-1.6662600040,-1.5742180347,-1.4993319511,-1.4316639900,
-1.3652280569,-1.3000990152,-1.2280930281,-1.1588579416,-1.0921250582,-1.0135740042,-0.9202849865,-0.8287050128,
-0.7374889851,-0.6447759867,-0.5590940118,-0.4857139885,-0.4110319912,-0.3459700048,-0.2851159871,-0.2341620028,
-0.1870580018,-0.1442500055,-0.1107169986,-0.0739680007,-0.0365610011,-0.0073290002, 0.0203610007, 0.0479039997,
 0.0751969963, 0.0980999991, 0.1220389977, 0.1458999962, 0.1694349945, 0.1970459968, 0.2252430022, 0.2556869984,
 0.2870100141, 0.3197099864, 0.3525829911, 0.3889069855, 0.4334920049, 0.4769459963, 0.5204820037, 0.5644530058,
 0.6122040153, 0.6685929894, 0.7341650128, 0.8032159805, 0.8784040213, 0.9566209912, 1.0397069454, 1.1293770075,
 1.2211159468, 1.3080279827, 1.4024800062, 1.5056819916, 1.6227730513, 1.7724959850, 1.9430880547, 2.2903931141
};

const uint8_t ff_nelly_band_sizes_table[NELLY_BANDS] = {
2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 8, 9, 10, 12, 14, 15
};

const uint16_t ff_nelly_init_table[64] = {
3134, 5342, 6870, 7792, 8569, 9185, 9744, 10191, 10631, 11061, 11434, 11770,
12116, 12513, 12925, 13300, 13674, 14027, 14352, 14716, 15117, 15477, 15824,
16157, 16513, 16804, 17090, 17401, 17679, 17948, 18238, 18520, 18764, 19078,
19381, 19640, 19921, 20205, 20500, 20813, 21162, 21465, 21794, 22137, 22453,
22756, 23067, 23350, 23636, 23926, 24227, 24521, 24819, 25107, 25414, 25730,
26120, 26497, 26895, 27344, 27877, 28463, 29426, 31355
};

const int16_t ff_nelly_delta_table[32] = {
-11725, -9420, -7910, -6801, -5948, -5233, -4599, -4039, -3507, -3030, -2596,
-2170, -1774, -1383, -1016, -660, -329, -1, 337, 696, 1085, 1512, 1962, 2433,
2968, 3569, 4314, 5279, 6622, 8154, 10076, 12975
};

static inline int signed_shift(int i, int shift) {
    if (shift > 0)
        return i << shift;
    return i >> -shift;
}

static int sum_bits(short *buf, short shift, short off)
{
    int i, ret = 0;

    for (i = 0; i < NELLY_FILL_LEN; i++) {
        int b = buf[i]-off;
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


void ff_nelly_get_sample_bits(const float *buf, int *bits)
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

