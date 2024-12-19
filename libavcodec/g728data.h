/*
 * G.728 decoder
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

#ifndef AVCODEC_G728DATA_H
#define AVCODEC_G728DATA_H

#include <stdint.h>
#include "libavutil/macros.h"

#define IDIM 5 /* Vector dimension (excitation block size) */
#define LPC 50 /* Synthesis filter order */
#define LPCLG 10 /* Log-gain predictor order */
#define NFRSZ 20 /* Frame size (adaptation cycle size in samples */
#define NONR 35 /* Number of non-recursive window samples for synthesis filter */
#define NONRLG 20 /* Number of non-recursive window samples for log-gain predictor */
#define NUPDATE 4 /* Predictor update period (in terms of vectors) */

#define NSBSZ (LPC + NONR + NFRSZ)
#define NSBGSZ (LPCLG + NONRLG + NUPDATE)

// Hybrid window for the synthesis filter
static const uint16_t g728_wnr[NSBSZ] = {
     1565,  3127,  4681,  6225,  7755,  9266, 10757, 12223, 13661, 15068,
    16441, 17776, 19071, 20322, 21526, 22682, 23786, 24835, 25828, 26761,
    27634, 28444, 29188, 29866, 30476, 31016, 31486, 31884, 32208, 32460,
    32637, 32739, 32767, 32721, 32599, 32403, 32171, 31940, 31711, 31484,
    31259, 31034, 30812, 30591, 30372, 30154, 29938, 29724, 29511, 29299,
    29089, 28881, 28674, 28468, 28264, 28062, 27861, 27661, 27463, 27266,
    27071, 26877, 26684, 26493, 26303, 26114, 25927, 25742, 25557, 25374,
    25192, 25012, 24832, 24654, 24478, 24302, 24128, 23955, 23784, 23613,
    23444, 23276, 23109, 22943, 22779, 22616, 22454, 22293, 22133, 21974,
    21817, 21661, 21505, 21351, 21198, 21046, 20896, 20746, 20597, 20450,
    20303, 20157, 20013, 19870, 19727
};

// Hybrid window for the log-gain predictor
static const uint16_t g728_wnrg[NSBGSZ] = {
     3026,  6025,  8973, 11845, 14615, 17261, 19759, 22088, 24228, 26162,
    27872, 29344, 30565, 31525, 32216, 32631, 32767, 32625, 32203, 31506,
    30540, 29461, 28420, 27416, 26448, 25514, 24613, 23743, 22905, 22096,
    21315, 20562, 19836, 19135
};

// Values for bandwidth broadcasting
static const uint16_t g728_facv[LPC] = {
    16192, 16002, 15815, 15629, 15446, 15265, 15086, 14910, 14735, 14562,
    14391, 14223, 14056, 13891, 13729, 13568, 13409, 13252, 13096, 12943,
    12791, 12641, 12493, 12347, 12202, 12059, 11918, 11778, 11640, 11504,
    11369, 11236, 11104, 10974, 10845, 10718, 10593, 10468, 10346, 10225,
    10105,  9986,  9869,  9754,  9639,  9526,  9415,  9304,  9195,  9088
};

#endif /* AVCODEC_G728DATA_H */
