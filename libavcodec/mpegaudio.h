/*
 * copyright (c) 2001 Fabrice Bellard
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
 * mpeg audio declarations for both encoder and decoder.
 */

#ifndef AVCODEC_MPEGAUDIO_H
#define AVCODEC_MPEGAUDIO_H

#ifndef CONFIG_FLOAT
#   define CONFIG_FLOAT 0
#endif

#include "avcodec.h"

/* max frame size, in samples */
#define MPA_FRAME_SIZE 1152

/* max compressed frame size */
#define MPA_MAX_CODED_FRAME_SIZE 1792

#define MPA_MAX_CHANNELS 2

#define SBLIMIT 32 /* number of subbands */

#define MPA_STEREO  0
#define MPA_JSTEREO 1
#define MPA_DUAL    2
#define MPA_MONO    3

#define MP3_MASK 0xFFFE0CCF

#ifndef FRAC_BITS
#define FRAC_BITS   23   /* fractional bits for sb_samples and dct */
#define WFRAC_BITS  16   /* fractional bits for window */
#endif

#define FRAC_ONE    (1 << FRAC_BITS)

#define FIX(a)   ((int)((a) * FRAC_ONE))

#if CONFIG_FLOAT
#   define INTFLOAT float
typedef float MPA_INT;
typedef float OUT_INT;
#elif FRAC_BITS <= 15
#   define INTFLOAT int
typedef int16_t MPA_INT;
typedef int16_t OUT_INT;
#else
#   define INTFLOAT int
typedef int32_t MPA_INT;
typedef int16_t OUT_INT;
#endif

#define MPA_DECODE_HEADER \
    int frame_size; \
    int error_protection; \
    int layer; \
    int sample_rate; \
    int sample_rate_index; /* between 0 and 8 */ \
    int bit_rate; \
    int nb_channels; \
    int mode; \
    int mode_ext; \
    int lsf;

typedef struct MPADecodeHeader {
  MPA_DECODE_HEADER
} MPADecodeHeader;

int ff_mpa_l2_select_table(int bitrate, int nb_channels, int freq, int lsf);
int ff_mpa_decode_header(AVCodecContext *avctx, uint32_t head, int *sample_rate, int *channels, int *frame_size, int *bitrate);

/* fast header check for resync */
static inline int ff_mpa_check_header(uint32_t header){
    /* header */
    if ((header & 0xffe00000) != 0xffe00000)
        return -1;
    /* layer check */
    if ((header & (3<<17)) == 0)
        return -1;
    /* bit rate */
    if ((header & (0xf<<12)) == 0xf<<12)
        return -1;
    /* frequency */
    if ((header & (3<<10)) == 3<<10)
        return -1;
    return 0;
}

#endif /* AVCODEC_MPEGAUDIO_H */
