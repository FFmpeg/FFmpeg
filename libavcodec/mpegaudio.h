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
 * @file mpegaudio.h
 * mpeg audio declarations for both encoder and decoder.
 */

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

/* header + layer + bitrate + freq + lsf/mpeg25 */
#define SAME_HEADER_MASK \
   (0xffe00000 | (3 << 17) | (0xf << 12) | (3 << 10) | (3 << 19))

/* define USE_HIGHPRECISION to have a bit exact (but slower) mpeg
   audio decoder */

#ifdef USE_HIGHPRECISION
#define FRAC_BITS   23   /* fractional bits for sb_samples and dct */
#define WFRAC_BITS  16   /* fractional bits for window */
#else
#define FRAC_BITS   15   /* fractional bits for sb_samples and dct */
#define WFRAC_BITS  14   /* fractional bits for window */
#endif

#if defined(USE_HIGHPRECISION) && defined(CONFIG_AUDIO_NONSHORT)
typedef int32_t OUT_INT;
#define OUT_MAX INT32_MAX
#define OUT_MIN INT32_MIN
#define OUT_SHIFT (WFRAC_BITS + FRAC_BITS - 31)
#else
typedef int16_t OUT_INT;
#define OUT_MAX INT16_MAX
#define OUT_MIN INT16_MIN
#define OUT_SHIFT (WFRAC_BITS + FRAC_BITS - 15)
#endif

#if FRAC_BITS <= 15
typedef int16_t MPA_INT;
#else
typedef int32_t MPA_INT;
#endif

int l2_select_table(int bitrate, int nb_channels, int freq, int lsf);
int mpa_decode_header(AVCodecContext *avctx, uint32_t head, int *sample_rate);
void ff_mpa_synth_init(MPA_INT *window);
void ff_mpa_synth_filter(MPA_INT *synth_buf_ptr, int *synth_buf_offset,
                         MPA_INT *window, int *dither_state,
                         OUT_INT *samples, int incr,
                         int32_t sb_samples[SBLIMIT]);

extern const uint16_t mpa_bitrate_tab[2][3][15];
extern const uint16_t mpa_freq_tab[3];
extern const unsigned char *alloc_tables[5];
extern const double enwindow[512];
extern const int sblimit_table[5];
extern const int quant_steps[17];
extern const int quant_bits[17];
extern const int32_t mpa_enwindow[257];

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
