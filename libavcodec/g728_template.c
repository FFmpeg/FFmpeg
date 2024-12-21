/*
 * G.728 / RealAudio 2.0 (28.8K) decoder
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

static void convolve(float *tgt, const float *src, int len, int n)
{
    for (; n >= 0; n--)
        tgt[n] = ff_scalarproduct_float_c(src, src - n, len);

}

/**
 * Hybrid window filtering, see blocks 36 and 49 of the G.728 specification.
 *
 * @param order   filter order
 * @param n       input length
 * @param non_rec number of non-recursive samples
 * @param out     filter output
 * @param hist    pointer to the input history of the filter
 * @param out     pointer to the non-recursive part of the output
 * @param out2    pointer to the recursive part of the output
 * @param window  pointer to the windowing function table
 */
static void do_hybrid_window(void (*vector_fmul)(float *dst, const float *src0, const float *src1, int len),
                             int order, int n, int non_rec, float *out,
                             const float *hist, float *out2, const float *window)
{
    int i;
    float buffer1[MAX_BACKWARD_FILTER_ORDER + 1];
    float buffer2[MAX_BACKWARD_FILTER_ORDER + 1];
    LOCAL_ALIGNED(32, float, work, [FFALIGN(MAX_BACKWARD_FILTER_ORDER +
                                            MAX_BACKWARD_FILTER_LEN   +
                                            MAX_BACKWARD_FILTER_NONREC, 16)]);

    av_assert2(order>=0);

    vector_fmul(work, window, hist, FFALIGN(order + n + non_rec, 16));

    convolve(buffer1, work + order    , n      , order);
    convolve(buffer2, work + order + n, non_rec, order);

    for (i=0; i <= order; i++) {
        out2[i] = out2[i] * ATTEN + buffer1[i];
        out [i] = out2[i]         + buffer2[i];
    }

    /* Multiply by the white noise correcting factor (WNCF). */
    *out *= 257.0 / 256.0;
}
