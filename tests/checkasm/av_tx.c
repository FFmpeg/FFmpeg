/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "libavutil/mem_internal.h"
#include "libavutil/tx.h"
#include "libavutil/error.h"

#include "checkasm.h"

#include <stdlib.h>

#define EPS 0.0005

#define SCALE_NOOP(x) (x)
#define SCALE_INT20(x) (av_clip64(lrintf((x) * 2147483648.0), INT32_MIN, INT32_MAX) >> 12)

#define randomize_complex(BUF, LEN, TYPE, SCALE)                \
    do {                                                        \
        TYPE *buf = (TYPE *)BUF;                                \
        for (int i = 0; i < LEN; i++) {                         \
            double fre = (double)rnd() / UINT_MAX;              \
            double fim = (double)rnd() / UINT_MAX;              \
            buf[i] = (TYPE){ SCALE(fre), SCALE(fim) };          \
        }                                                       \
    } while (0)

static const int check_lens[] = {
    2, 4, 8, 16, 32, 64, 120, 960, 1024, 1920, 16384,
};

static AVTXContext *tx_refs[AV_TX_NB][2 /* Direction */][FF_ARRAY_ELEMS(check_lens)] = { 0 };
static int init = 0;

static void free_tx_refs(void)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(tx_refs); i++)
        for (int j = 0; j < FF_ARRAY_ELEMS(*tx_refs); j++)
            for (int k = 0; k < FF_ARRAY_ELEMS(**tx_refs); k++)
                av_tx_uninit(&tx_refs[i][j][k]);
}

#define CHECK_TEMPLATE(PREFIX, TYPE, DIR, DATA_TYPE, SCALE_TYPE, LENGTHS, CHECK_EXPRESSION) \
    do {                                                                          \
        int err;                                                                  \
        AVTXContext *tx;                                                          \
        av_tx_fn fn;                                                              \
        int num_checks = 0;                                                       \
        int last_check = 0;                                                       \
                                                                                  \
        for (int i = 0; i < FF_ARRAY_ELEMS(LENGTHS); i++) {                       \
            int len = LENGTHS[i];                                                 \
            const SCALE_TYPE scale = 1.0 / len;                                   \
                                                                                  \
            if ((err = av_tx_init(&tx, &fn, TYPE, DIR, len, &scale, 0x0)) < 0) {  \
                fprintf(stderr, "av_tx: %s\n", av_err2str(err));                  \
                return;                                                           \
            }                                                                     \
                                                                                  \
            if (check_func(fn, PREFIX "_%i", len)) {                              \
                AVTXContext *tx_ref = tx_refs[TYPE][DIR][i];                      \
                if (!tx_ref)                                                      \
                    tx_ref = tx;                                                  \
                num_checks++;                                                     \
                last_check = len;                                                 \
                call_ref(tx_ref, out_ref, in, sizeof(DATA_TYPE));                 \
                call_new(tx,     out_new, in, sizeof(DATA_TYPE));                 \
                if (CHECK_EXPRESSION) {                                           \
                    fail();                                                       \
                    av_tx_uninit(&tx);                                            \
                    break;                                                        \
                }                                                                 \
                bench_new(tx, out_new, in, sizeof(DATA_TYPE));                    \
                av_tx_uninit(&tx_refs[TYPE][DIR][i]);                             \
                tx_refs[TYPE][DIR][i] = tx;                                       \
            } else {                                                              \
                av_tx_uninit(&tx);                                                \
            }                                                                     \
        }                                                                         \
                                                                                  \
        if (num_checks == 1)                                                      \
            report(PREFIX "_%i", last_check);                                     \
        else if (num_checks)                                                      \
            report(PREFIX);                                                       \
    } while (0)

void checkasm_check_av_tx(void)
{
    declare_func(void, AVTXContext *tx, void *out, void *in, ptrdiff_t stride);

    void *in      = av_malloc(16384*2*8);
    void *out_ref = av_malloc(16384*2*8);
    void *out_new = av_malloc(16384*2*8);

    randomize_complex(in, 16384, AVComplexFloat, SCALE_NOOP);
    CHECK_TEMPLATE("float_fft", AV_TX_FLOAT_FFT, 0, AVComplexFloat, float, check_lens,
                   !float_near_abs_eps_array(out_ref, out_new, EPS, len*2));

    CHECK_TEMPLATE("float_imdct", AV_TX_FLOAT_MDCT, 1, float, float, check_lens,
                   !float_near_abs_eps_array(out_ref, out_new, EPS, len));

    randomize_complex(in, 16384, AVComplexDouble, SCALE_NOOP);
    CHECK_TEMPLATE("double_fft", AV_TX_DOUBLE_FFT, 0, AVComplexDouble, double, check_lens,
                   !double_near_abs_eps_array(out_ref, out_new, EPS, len*2));

    av_free(in);
    av_free(out_ref);
    av_free(out_new);

    if (!init) {
        init = 1;
        atexit(free_tx_refs);
    }
}
