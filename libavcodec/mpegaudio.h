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

#include "avcodec.h"
#include "get_bits.h"
#include "dsputil.h"

#define CONFIG_AUDIO_NONSHORT 0

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

#define MP3_MASK 0xFFFE0CCF

#if CONFIG_MPEGAUDIO_HP
#define FRAC_BITS   23   /* fractional bits for sb_samples and dct */
#define WFRAC_BITS  16   /* fractional bits for window */
#else
#define FRAC_BITS   15   /* fractional bits for sb_samples and dct */
#define WFRAC_BITS  14   /* fractional bits for window */
#endif

#define FRAC_ONE    (1 << FRAC_BITS)

#define FIX(a)   ((int)((a) * FRAC_ONE))

#if CONFIG_MPEGAUDIO_HP && CONFIG_AUDIO_NONSHORT
typedef int32_t OUT_INT;
#define OUT_MAX INT32_MAX
#define OUT_MIN INT32_MIN
#define OUT_SHIFT (WFRAC_BITS + FRAC_BITS - 31)
#define OUT_FMT SAMPLE_FMT_S32
#else
typedef int16_t OUT_INT;
#define OUT_MAX INT16_MAX
#define OUT_MIN INT16_MIN
#define OUT_SHIFT (WFRAC_BITS + FRAC_BITS - 15)
#define OUT_FMT SAMPLE_FMT_S16
#endif

#if FRAC_BITS <= 15
typedef int16_t MPA_INT;
#else
typedef int32_t MPA_INT;
#endif

#define BACKSTEP_SIZE 512
#define EXTRABYTES 24

/* layer 3 "granule" */
typedef struct GranuleDef {
    uint8_t scfsi;
    int part2_3_length;
    int big_values;
    int global_gain;
    int scalefac_compress;
    uint8_t block_type;
    uint8_t switch_point;
    int table_select[3];
    int subblock_gain[3];
    uint8_t scalefac_scale;
    uint8_t count1table_select;
    int region_size[3]; /* number of huffman codes in each region */
    int preflag;
    int short_start, long_end; /* long/short band indexes */
    uint8_t scale_factors[40];
    int32_t sb_hybrid[SBLIMIT * 18]; /* 576 samples */
} GranuleDef;

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

typedef struct MPADecodeContext {
    MPA_DECODE_HEADER
    uint8_t last_buf[2*BACKSTEP_SIZE + EXTRABYTES];
    int last_buf_size;
    /* next header (used in free format parsing) */
    uint32_t free_format_next_header;
    GetBitContext gb;
    GetBitContext in_gb;
    DECLARE_ALIGNED(16, MPA_INT, synth_buf)[MPA_MAX_CHANNELS][512 * 2];
    int synth_buf_offset[MPA_MAX_CHANNELS];
    DECLARE_ALIGNED(16, int32_t, sb_samples)[MPA_MAX_CHANNELS][36][SBLIMIT];
    int32_t mdct_buf[MPA_MAX_CHANNELS][SBLIMIT * 18]; /* previous samples, for layer 3 MDCT */
    GranuleDef granules[2][2]; /* Used in Layer 3 */
#ifdef DEBUG
    int frame_count;
#endif
    void (*compute_antialias)(struct MPADecodeContext *s, struct GranuleDef *g);
    int adu_mode; ///< 0 for standard mp3, 1 for adu formatted mp3
    int dither_state;
    int error_recognition;
    AVCodecContext* avctx;
} MPADecodeContext;

/* layer 3 huffman tables */
typedef struct HuffTable {
    int xsize;
    const uint8_t *bits;
    const uint16_t *codes;
} HuffTable;

int ff_mpa_l2_select_table(int bitrate, int nb_channels, int freq, int lsf);
int ff_mpa_decode_header(AVCodecContext *avctx, uint32_t head, int *sample_rate, int *channels, int *frame_size, int *bitrate);
extern MPA_INT ff_mpa_synth_window[];
void ff_mpa_synth_init(MPA_INT *window);
void ff_mpa_synth_filter(MPA_INT *synth_buf_ptr, int *synth_buf_offset,
                         MPA_INT *window, int *dither_state,
                         OUT_INT *samples, int incr,
                         int32_t sb_samples[SBLIMIT]);

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
