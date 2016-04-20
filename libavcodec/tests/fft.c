/*
 * (c) 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * FFT and MDCT tests.
 */

#include "config.h"

#include <math.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/cpu.h"
#include "libavutil/lfg.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"

#include "libavcodec/fft.h"
#if FFT_FLOAT
#include "libavcodec/dct.h"
#include "libavcodec/rdft.h"
#endif

/* reference fft */

#define MUL16(a, b) ((a) * (b))

#define CMAC(pre, pim, are, aim, bre, bim)          \
    {                                               \
        pre += (MUL16(are, bre) - MUL16(aim, bim)); \
        pim += (MUL16(are, bim) + MUL16(bre, aim)); \
    }

#if FFT_FLOAT
#define RANGE 1.0
#define REF_SCALE(x, bits)  (x)
#define FMT "%10.6f"
#else
#define RANGE 16384
#define REF_SCALE(x, bits) ((x) / (1 << (bits)))
#define FMT "%6d"
#endif

static struct {
    float re, im;
} *exptab;

static int fft_ref_init(int nbits, int inverse)
{
    int i, n = 1 << nbits;

    exptab = av_malloc((n / 2) * sizeof(*exptab));
    if (!exptab)
        return AVERROR(ENOMEM);

    for (i = 0; i < (n / 2); i++) {
        double alpha = 2 * M_PI * (float) i / (float) n;
        double c1 = cos(alpha), s1 = sin(alpha);
        if (!inverse)
            s1 = -s1;
        exptab[i].re = c1;
        exptab[i].im = s1;
    }
    return 0;
}

static void fft_ref(FFTComplex *tabr, FFTComplex *tab, int nbits)
{
    int i, j;
    int n  = 1 << nbits;
    int n2 = n >> 1;

    for (i = 0; i < n; i++) {
        double tmp_re = 0, tmp_im = 0;
        FFTComplex *q = tab;
        for (j = 0; j < n; j++) {
            double s, c;
            int k = (i * j) & (n - 1);
            if (k >= n2) {
                c = -exptab[k - n2].re;
                s = -exptab[k - n2].im;
            } else {
                c = exptab[k].re;
                s = exptab[k].im;
            }
            CMAC(tmp_re, tmp_im, c, s, q->re, q->im);
            q++;
        }
        tabr[i].re = REF_SCALE(tmp_re, nbits);
        tabr[i].im = REF_SCALE(tmp_im, nbits);
    }
}

#if CONFIG_MDCT
static void imdct_ref(FFTSample *out, FFTSample *in, int nbits)
{
    int i, k, n = 1 << nbits;

    for (i = 0; i < n; i++) {
        double sum = 0;
        for (k = 0; k < n / 2; k++) {
            int a = (2 * i + 1 + (n / 2)) * (2 * k + 1);
            double f = cos(M_PI * a / (double) (2 * n));
            sum += f * in[k];
        }
        out[i] = REF_SCALE(-sum, nbits - 2);
    }
}

/* NOTE: no normalisation by 1 / N is done */
static void mdct_ref(FFTSample *output, FFTSample *input, int nbits)
{
    int i, k, n = 1 << nbits;

    /* do it by hand */
    for (k = 0; k < n / 2; k++) {
        double s = 0;
        for (i = 0; i < n; i++) {
            double a = (2 * M_PI * (2 * i + 1 + n / 2) * (2 * k + 1) / (4 * n));
            s += input[i] * cos(a);
        }
        output[k] = REF_SCALE(s, nbits - 1);
    }
}
#endif /* CONFIG_MDCT */

#if FFT_FLOAT
#if CONFIG_DCT
static void idct_ref(float *output, float *input, int nbits)
{
    int i, k, n = 1 << nbits;

    /* do it by hand */
    for (i = 0; i < n; i++) {
        double s = 0.5 * input[0];
        for (k = 1; k < n; k++) {
            double a = M_PI * k * (i + 0.5) / n;
            s += input[k] * cos(a);
        }
        output[i] = 2 * s / n;
    }
}

static void dct_ref(float *output, float *input, int nbits)
{
    int i, k, n = 1 << nbits;

    /* do it by hand */
    for (k = 0; k < n; k++) {
        double s = 0;
        for (i = 0; i < n; i++) {
            double a = M_PI * k * (i + 0.5) / n;
            s += input[i] * cos(a);
        }
        output[k] = s;
    }
}
#endif /* CONFIG_DCT */
#endif /* FFT_FLOAT */

static FFTSample frandom(AVLFG *prng)
{
    return (int16_t) av_lfg_get(prng) / 32768.0 * RANGE;
}

static int check_diff(FFTSample *tab1, FFTSample *tab2, int n, double scale)
{
    int i, err = 0;
    double error = 0, max = 0;

    for (i = 0; i < n; i++) {
        double e = fabs(tab1[i] - (tab2[i] / scale)) / RANGE;
        if (e >= 1e-3) {
            av_log(NULL, AV_LOG_ERROR, "ERROR %5d: "FMT" "FMT"\n",
                   i, tab1[i], tab2[i]);
            err = 1;
        }
        error += e * e;
        if (e > max)
            max = e;
    }
    av_log(NULL, AV_LOG_INFO, "max:%f e:%g\n", max, sqrt(error) / n);
    return err;
}

static void help(void)
{
    av_log(NULL, AV_LOG_INFO,
           "usage: fft-test [-h] [-s] [-i] [-n b]\n"
           "-h     print this help\n"
           "-s     speed test\n"
           "-m     (I)MDCT test\n"
           "-d     (I)DCT test\n"
           "-r     (I)RDFT test\n"
           "-i     inverse transform test\n"
           "-n b   set the transform size to 2^b\n"
           "-f x   set scale factor for output data of (I)MDCT to x\n");
}

enum tf_transform {
    TRANSFORM_FFT,
    TRANSFORM_MDCT,
    TRANSFORM_RDFT,
    TRANSFORM_DCT,
};

#if !HAVE_GETOPT
#include "compat/getopt.c"
#endif

int main(int argc, char **argv)
{
    FFTComplex *tab, *tab1, *tab_ref;
    FFTSample *tab2;
    enum tf_transform transform = TRANSFORM_FFT;
    FFTContext m, s;
#if FFT_FLOAT
    RDFTContext r;
    DCTContext d;
#endif /* FFT_FLOAT */
    int it, i, err = 1;
    int do_speed = 0, do_inverse = 0;
    int fft_nbits = 9, fft_size;
    double scale = 1.0;
    AVLFG prng;

    av_lfg_init(&prng, 1);

    for (;;) {
        int c = getopt(argc, argv, "hsimrdn:f:c:");
        if (c == -1)
            break;
        switch (c) {
        case 'h':
            help();
            return 1;
        case 's':
            do_speed = 1;
            break;
        case 'i':
            do_inverse = 1;
            break;
        case 'm':
            transform = TRANSFORM_MDCT;
            break;
        case 'r':
            transform = TRANSFORM_RDFT;
            break;
        case 'd':
            transform = TRANSFORM_DCT;
            break;
        case 'n':
            fft_nbits = atoi(optarg);
            break;
        case 'f':
            scale = atof(optarg);
            break;
        case 'c':
        {
            int cpuflags = av_parse_cpu_flags(optarg);
            if (cpuflags < 0)
                return 1;
            av_set_cpu_flags_mask(cpuflags);
            break;
        }
        }
    }

    fft_size = 1 << fft_nbits;
    tab      = av_malloc(fft_size * sizeof(FFTComplex));
    tab1     = av_malloc(fft_size * sizeof(FFTComplex));
    tab_ref  = av_malloc(fft_size * sizeof(FFTComplex));
    tab2     = av_malloc(fft_size * sizeof(FFTSample));

    if (!(tab && tab1 && tab_ref && tab2))
        goto cleanup;

    switch (transform) {
#if CONFIG_MDCT
    case TRANSFORM_MDCT:
        av_log(NULL, AV_LOG_INFO, "Scale factor is set to %f\n", scale);
        if (do_inverse)
            av_log(NULL, AV_LOG_INFO, "IMDCT");
        else
            av_log(NULL, AV_LOG_INFO, "MDCT");
        ff_mdct_init(&m, fft_nbits, do_inverse, scale);
        break;
#endif /* CONFIG_MDCT */
    case TRANSFORM_FFT:
        if (do_inverse)
            av_log(NULL, AV_LOG_INFO, "IFFT");
        else
            av_log(NULL, AV_LOG_INFO, "FFT");
        ff_fft_init(&s, fft_nbits, do_inverse);
        if (err = fft_ref_init(fft_nbits, do_inverse) < 0)
            goto cleanup;
        break;
#if FFT_FLOAT
#if CONFIG_RDFT
    case TRANSFORM_RDFT:
        if (do_inverse)
            av_log(NULL, AV_LOG_INFO, "IDFT_C2R");
        else
            av_log(NULL, AV_LOG_INFO, "DFT_R2C");
        ff_rdft_init(&r, fft_nbits, do_inverse ? IDFT_C2R : DFT_R2C);
        if (err = fft_ref_init(fft_nbits, do_inverse) < 0)
            goto cleanup;
        break;
#endif /* CONFIG_RDFT */
#if CONFIG_DCT
    case TRANSFORM_DCT:
        if (do_inverse)
            av_log(NULL, AV_LOG_INFO, "DCT_III");
        else
            av_log(NULL, AV_LOG_INFO, "DCT_II");
        ff_dct_init(&d, fft_nbits, do_inverse ? DCT_III : DCT_II);
        break;
#endif /* CONFIG_DCT */
#endif /* FFT_FLOAT */
    default:
        av_log(NULL, AV_LOG_ERROR, "Requested transform not supported\n");
        goto cleanup;
    }
    av_log(NULL, AV_LOG_INFO, " %d test\n", fft_size);

    /* generate random data */

    for (i = 0; i < fft_size; i++) {
        tab1[i].re = frandom(&prng);
        tab1[i].im = frandom(&prng);
    }

    /* checking result */
    av_log(NULL, AV_LOG_INFO, "Checking...\n");

    switch (transform) {
#if CONFIG_MDCT
    case TRANSFORM_MDCT:
        if (do_inverse) {
            imdct_ref(&tab_ref->re, &tab1->re, fft_nbits);
            m.imdct_calc(&m, tab2, &tab1->re);
            err = check_diff(&tab_ref->re, tab2, fft_size, scale);
        } else {
            mdct_ref(&tab_ref->re, &tab1->re, fft_nbits);
            m.mdct_calc(&m, tab2, &tab1->re);
            err = check_diff(&tab_ref->re, tab2, fft_size / 2, scale);
        }
        break;
#endif /* CONFIG_MDCT */
    case TRANSFORM_FFT:
        memcpy(tab, tab1, fft_size * sizeof(FFTComplex));
        s.fft_permute(&s, tab);
        s.fft_calc(&s, tab);

        fft_ref(tab_ref, tab1, fft_nbits);
        err = check_diff(&tab_ref->re, &tab->re, fft_size * 2, 1.0);
        break;
#if FFT_FLOAT
#if CONFIG_RDFT
    case TRANSFORM_RDFT:
    {
        int fft_size_2 = fft_size >> 1;
        if (do_inverse) {
            tab1[0].im          = 0;
            tab1[fft_size_2].im = 0;
            for (i = 1; i < fft_size_2; i++) {
                tab1[fft_size_2 + i].re =  tab1[fft_size_2 - i].re;
                tab1[fft_size_2 + i].im = -tab1[fft_size_2 - i].im;
            }

            memcpy(tab2, tab1, fft_size * sizeof(FFTSample));
            tab2[1] = tab1[fft_size_2].re;

            r.rdft_calc(&r, tab2);
            fft_ref(tab_ref, tab1, fft_nbits);
            for (i = 0; i < fft_size; i++) {
                tab[i].re = tab2[i];
                tab[i].im = 0;
            }
            err = check_diff(&tab_ref->re, &tab->re, fft_size * 2, 0.5);
        } else {
            for (i = 0; i < fft_size; i++) {
                tab2[i]    = tab1[i].re;
                tab1[i].im = 0;
            }
            r.rdft_calc(&r, tab2);
            fft_ref(tab_ref, tab1, fft_nbits);
            tab_ref[0].im = tab_ref[fft_size_2].re;
            err = check_diff(&tab_ref->re, tab2, fft_size, 1.0);
        }
        break;
    }
#endif /* CONFIG_RDFT */
#if CONFIG_DCT
    case TRANSFORM_DCT:
        memcpy(tab, tab1, fft_size * sizeof(FFTComplex));
        d.dct_calc(&d, &tab->re);
        if (do_inverse)
            idct_ref(&tab_ref->re, &tab1->re, fft_nbits);
        else
            dct_ref(&tab_ref->re, &tab1->re, fft_nbits);
        err = check_diff(&tab_ref->re, &tab->re, fft_size, 1.0);
        break;
#endif /* CONFIG_DCT */
#endif /* FFT_FLOAT */
    }

    /* do a speed test */

    if (do_speed) {
        int64_t time_start, duration;
        int nb_its;

        av_log(NULL, AV_LOG_INFO, "Speed test...\n");
        /* we measure during about 1 seconds */
        nb_its = 1;
        for (;;) {
            time_start = av_gettime_relative();
            for (it = 0; it < nb_its; it++) {
                switch (transform) {
                case TRANSFORM_MDCT:
                    if (do_inverse)
                        m.imdct_calc(&m, &tab->re, &tab1->re);
                    else
                        m.mdct_calc(&m, &tab->re, &tab1->re);
                    break;
                case TRANSFORM_FFT:
                    memcpy(tab, tab1, fft_size * sizeof(FFTComplex));
                    s.fft_calc(&s, tab);
                    break;
#if FFT_FLOAT
                case TRANSFORM_RDFT:
                    memcpy(tab2, tab1, fft_size * sizeof(FFTSample));
                    r.rdft_calc(&r, tab2);
                    break;
                case TRANSFORM_DCT:
                    memcpy(tab2, tab1, fft_size * sizeof(FFTSample));
                    d.dct_calc(&d, tab2);
                    break;
#endif /* FFT_FLOAT */
                }
            }
            duration = av_gettime_relative() - time_start;
            if (duration >= 1000000)
                break;
            nb_its *= 2;
        }
        av_log(NULL, AV_LOG_INFO,
               "time: %0.1f us/transform [total time=%0.2f s its=%d]\n",
               (double) duration / nb_its,
               (double) duration / 1000000.0,
               nb_its);
    }

    switch (transform) {
#if CONFIG_MDCT
    case TRANSFORM_MDCT:
        ff_mdct_end(&m);
        break;
#endif /* CONFIG_MDCT */
    case TRANSFORM_FFT:
        ff_fft_end(&s);
        break;
#if FFT_FLOAT
#if CONFIG_RDFT
    case TRANSFORM_RDFT:
        ff_rdft_end(&r);
        break;
#endif /* CONFIG_RDFT */
#if CONFIG_DCT
    case TRANSFORM_DCT:
        ff_dct_end(&d);
        break;
#endif /* CONFIG_DCT */
#endif /* FFT_FLOAT */
    }

cleanup:
    av_free(tab);
    av_free(tab1);
    av_free(tab2);
    av_free(tab_ref);
    av_free(exptab);

    if (err)
        printf("Error: %d.\n", err);

    return !!err;
}
