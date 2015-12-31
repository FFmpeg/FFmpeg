/*
 * Header file for hardcoded DSD tables
 * based on BSD licensed dsd2pcm by Sebastian Gesemann
 * Copyright (c) 2009, 2011 Sebastian Gesemann. All rights reserved.
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

#ifndef AVCODEC_DSD_TABLEGEN_H
#define AVCODEC_DSD_TABLEGEN_H

#include <stdint.h>
#include "libavutil/attributes.h"

#define HTAPS   48                /** number of FIR constants */
#define CTABLES ((HTAPS + 7) / 8) /** number of "8 MACs" lookup tables */

#include "libavutil/common.h"

/*
 * Properties of this 96-tap lowpass filter when applied on a signal
 * with sampling rate of 44100*64 Hz:
 *
 * () has a delay of 17 microseconds.
 *
 * () flat response up to 48 kHz
 *
 * () if you downsample afterwards by a factor of 8, the
 *    spectrum below 70 kHz is practically alias-free.
 *
 * () stopband rejection is about 160 dB
 *
 * The coefficient tables ("ctables") take only 6 Kibi Bytes and
 * should fit into a modern processor's fast cache.
 */

/**
 * The 2nd half (48 coeffs) of a 96-tap symmetric lowpass filter
 */
static const double htaps[HTAPS] = {
     0.09950731974056658,    0.09562845727714668,    0.08819647126516944,
     0.07782552527068175,    0.06534876523171299,    0.05172629311427257,
     0.0379429484910187,     0.02490921351762261,    0.0133774746265897,
     0.003883043418804416,  -0.003284703416210726,  -0.008080250212687497,
    -0.01067241812471033,   -0.01139427235000863,   -0.0106813877974587,
    -0.009007905078766049,  -0.006828859761015335,  -0.004535184322001496,
    -0.002425035959059578,  -0.0006922187080790708,  0.0005700762133516592,
     0.001353838005269448,   0.001713709169690937,   0.001742046839472948,
     0.001545601648013235,   0.001226696225277855,   0.0008704322683580222,
     0.0005381636200535649,  0.000266446345425276,   7.002968738383528e-05,
    -5.279407053811266e-05, -0.0001140625650874684, -0.0001304796361231895,
    -0.0001189970287491285, -9.396247155265073e-05, -6.577634378272832e-05,
    -4.07492895872535e-05,  -2.17407957554587e-05,  -9.163058931391722e-06,
    -2.017460145032201e-06,  1.249721855219005e-06,  2.166655190537392e-06,
     1.930520892991082e-06,  1.319400334374195e-06,  7.410039764949091e-07,
     3.423230509967409e-07,  1.244182214744588e-07,  3.130441005359396e-08
};

static float ctables[CTABLES][256];

static av_cold void dsd_ctables_tableinit(void)
{
    int t, e, m, sign;
    double acc[CTABLES];
    for (e = 0; e < 256; ++e) {
        memset(acc, 0, sizeof(acc));
        for (m = 0; m < 8; ++m) {
            sign = (((e >> (7 - m)) & 1) * 2 - 1);
            for (t = 0; t < CTABLES; ++t)
                acc[t] += sign * htaps[t * 8 + m];
        }
        for (t = 0; t < CTABLES; ++t)
            ctables[CTABLES - 1 - t][e] = acc[t];
    }
}

#endif /* AVCODEC_DSD_TABLEGEN_H */
