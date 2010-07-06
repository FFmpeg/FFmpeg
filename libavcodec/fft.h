/*
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_FFT_H
#define AVCODEC_FFT_H

#include <stdint.h>
#include "config.h"
#include "libavutil/mem.h"
#include "avfft.h"

/* FFT computation */

struct FFTContext {
    int nbits;
    int inverse;
    uint16_t *revtab;
    FFTComplex *tmp_buf;
    int mdct_size; /* size of MDCT (i.e. number of input data * 2) */
    int mdct_bits; /* n = 2^nbits */
    /* pre/post rotation tables */
    FFTSample *tcos;
    FFTSample *tsin;
    void (*fft_permute)(struct FFTContext *s, FFTComplex *z);
    void (*fft_calc)(struct FFTContext *s, FFTComplex *z);
    void (*imdct_calc)(struct FFTContext *s, FFTSample *output, const FFTSample *input);
    void (*imdct_half)(struct FFTContext *s, FFTSample *output, const FFTSample *input);
    void (*mdct_calc)(struct FFTContext *s, FFTSample *output, const FFTSample *input);
    int permutation;
#define FF_MDCT_PERM_NONE       0
#define FF_MDCT_PERM_INTERLEAVE 1
};

#if CONFIG_HARDCODED_TABLES
#define COSTABLE_CONST const
#define SINTABLE_CONST const
#define SINETABLE_CONST const
#else
#define COSTABLE_CONST
#define SINTABLE_CONST
#define SINETABLE_CONST
#endif

#define COSTABLE(size) \
    COSTABLE_CONST DECLARE_ALIGNED(16, FFTSample, ff_cos_##size)[size/2]
#define SINTABLE(size) \
    SINTABLE_CONST DECLARE_ALIGNED(16, FFTSample, ff_sin_##size)[size/2]
#define SINETABLE(size) \
    SINETABLE_CONST DECLARE_ALIGNED(16, float, ff_sine_##size)[size]
extern COSTABLE(16);
extern COSTABLE(32);
extern COSTABLE(64);
extern COSTABLE(128);
extern COSTABLE(256);
extern COSTABLE(512);
extern COSTABLE(1024);
extern COSTABLE(2048);
extern COSTABLE(4096);
extern COSTABLE(8192);
extern COSTABLE(16384);
extern COSTABLE(32768);
extern COSTABLE(65536);
extern COSTABLE_CONST FFTSample* const ff_cos_tabs[17];

/**
 * Initialize the cosine table in ff_cos_tabs[index]
 * \param index index in ff_cos_tabs array of the table to initialize
 */
void ff_init_ff_cos_tabs(int index);

extern SINTABLE(16);
extern SINTABLE(32);
extern SINTABLE(64);
extern SINTABLE(128);
extern SINTABLE(256);
extern SINTABLE(512);
extern SINTABLE(1024);
extern SINTABLE(2048);
extern SINTABLE(4096);
extern SINTABLE(8192);
extern SINTABLE(16384);
extern SINTABLE(32768);
extern SINTABLE(65536);

/**
 * Set up a complex FFT.
 * @param nbits           log2 of the length of the input array
 * @param inverse         if 0 perform the forward transform, if 1 perform the inverse
 */
int ff_fft_init(FFTContext *s, int nbits, int inverse);
void ff_fft_permute_c(FFTContext *s, FFTComplex *z);
void ff_fft_calc_c(FFTContext *s, FFTComplex *z);

void ff_fft_init_altivec(FFTContext *s);
void ff_fft_init_mmx(FFTContext *s);
void ff_fft_init_arm(FFTContext *s);
void ff_dct_init_mmx(DCTContext *s);

/**
 * Do the permutation needed BEFORE calling ff_fft_calc().
 */
static inline void ff_fft_permute(FFTContext *s, FFTComplex *z)
{
    s->fft_permute(s, z);
}
/**
 * Do a complex FFT with the parameters defined in ff_fft_init(). The
 * input data must be permuted before. No 1.0/sqrt(n) normalization is done.
 */
static inline void ff_fft_calc(FFTContext *s, FFTComplex *z)
{
    s->fft_calc(s, z);
}
void ff_fft_end(FFTContext *s);

/* MDCT computation */

static inline void ff_imdct_calc(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    s->imdct_calc(s, output, input);
}
static inline void ff_imdct_half(FFTContext *s, FFTSample *output, const FFTSample *input)
{
    s->imdct_half(s, output, input);
}

static inline void ff_mdct_calc(FFTContext *s, FFTSample *output,
                                const FFTSample *input)
{
    s->mdct_calc(s, output, input);
}

/**
 * Maximum window size for ff_kbd_window_init.
 */
#define FF_KBD_WINDOW_MAX 1024

/**
 * Generate a Kaiser-Bessel Derived Window.
 * @param   window  pointer to half window
 * @param   alpha   determines window shape
 * @param   n       size of half window, max FF_KBD_WINDOW_MAX
 */
void ff_kbd_window_init(float *window, float alpha, int n);

/**
 * Generate a sine window.
 * @param   window  pointer to half window
 * @param   n       size of half window
 */
void ff_sine_window_init(float *window, int n);

/**
 * initialize the specified entry of ff_sine_windows
 */
void ff_init_ff_sine_windows(int index);
extern SINETABLE(  32);
extern SINETABLE(  64);
extern SINETABLE( 128);
extern SINETABLE( 256);
extern SINETABLE( 512);
extern SINETABLE(1024);
extern SINETABLE(2048);
extern SINETABLE(4096);
extern SINETABLE_CONST float * const ff_sine_windows[13];

int ff_mdct_init(FFTContext *s, int nbits, int inverse, double scale);
void ff_imdct_calc_c(FFTContext *s, FFTSample *output, const FFTSample *input);
void ff_imdct_half_c(FFTContext *s, FFTSample *output, const FFTSample *input);
void ff_mdct_calc_c(FFTContext *s, FFTSample *output, const FFTSample *input);
void ff_mdct_end(FFTContext *s);

/* Real Discrete Fourier Transform */

struct RDFTContext {
    int nbits;
    int inverse;
    int sign_convention;

    /* pre/post rotation tables */
    const FFTSample *tcos;
    SINTABLE_CONST FFTSample *tsin;
    FFTContext fft;
    void (*rdft_calc)(struct RDFTContext *s, FFTSample *z);
};

/**
 * Set up a real FFT.
 * @param nbits           log2 of the length of the input array
 * @param trans           the type of transform
 */
int ff_rdft_init(RDFTContext *s, int nbits, enum RDFTransformType trans);
void ff_rdft_end(RDFTContext *s);

void ff_rdft_init_arm(RDFTContext *s);

static av_always_inline void ff_rdft_calc(RDFTContext *s, FFTSample *data)
{
    s->rdft_calc(s, data);
}

/* Discrete Cosine Transform */

struct DCTContext {
    int nbits;
    int inverse;
    RDFTContext rdft;
    const float *costab;
    FFTSample *csc2;
    void (*dct_calc)(struct DCTContext *s, FFTSample *data);
    void (*dct32)(FFTSample *out, const FFTSample *in);
};

/**
 * Set up DCT.
 * @param nbits           size of the input array:
 *                        (1 << nbits)     for DCT-II, DCT-III and DST-I
 *                        (1 << nbits) + 1 for DCT-I
 *
 * @note the first element of the input of DST-I is ignored
 */
int  ff_dct_init(DCTContext *s, int nbits, enum DCTTransformType type);
void ff_dct_calc(DCTContext *s, FFTSample *data);
void ff_dct_end (DCTContext *s);

#endif /* AVCODEC_FFT_H */
