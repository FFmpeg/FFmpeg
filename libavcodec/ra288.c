/*
 * RealAudio 2.0 (28.8K)
 * Copyright (c) 2003 the ffmpeg project
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

#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/internal.h"
#include "avcodec.h"
#include "internal.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "ra288.h"
#include "lpc.h"
#include "celp_filters.h"

#define MAX_BACKWARD_FILTER_ORDER  36
#define MAX_BACKWARD_FILTER_LEN    40
#define MAX_BACKWARD_FILTER_NONREC 35

#define RA288_BLOCK_SIZE        5
#define RA288_BLOCKS_PER_FRAME 32

typedef struct {
    AVFloatDSPContext fdsp;
    DECLARE_ALIGNED(32, float,   sp_lpc)[FFALIGN(36, 16)];   ///< LPC coefficients for speech data (spec: A)
    DECLARE_ALIGNED(32, float, gain_lpc)[FFALIGN(10, 16)];   ///< LPC coefficients for gain        (spec: GB)

    /** speech data history                                      (spec: SB).
     *  Its first 70 coefficients are updated only at backward filtering.
     */
    float sp_hist[111];

    /// speech part of the gain autocorrelation                  (spec: REXP)
    float sp_rec[37];

    /** log-gain history                                         (spec: SBLG).
     *  Its first 28 coefficients are updated only at backward filtering.
     */
    float gain_hist[38];

    /// recursive part of the gain autocorrelation               (spec: REXPLG)
    float gain_rec[11];
} RA288Context;

static av_cold int ra288_decode_init(AVCodecContext *avctx)
{
    RA288Context *ractx = avctx->priv_data;

    avctx->channels       = 1;
    avctx->channel_layout = AV_CH_LAYOUT_MONO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_FLT;

    if (avctx->block_align <= 0) {
        av_log(avctx, AV_LOG_ERROR, "unsupported block align\n");
        return AVERROR_PATCHWELCOME;
    }

    avpriv_float_dsp_init(&ractx->fdsp, avctx->flags & CODEC_FLAG_BITEXACT);

    return 0;
}

static void convolve(float *tgt, const float *src, int len, int n)
{
    for (; n >= 0; n--)
        tgt[n] = avpriv_scalarproduct_float_c(src, src - n, len);

}

static void decode(RA288Context *ractx, float gain, int cb_coef)
{
    int i;
    double sumsum;
    float sum, buffer[5];
    float *block = ractx->sp_hist + 70 + 36; // current block
    float *gain_block = ractx->gain_hist + 28;

    memmove(ractx->sp_hist + 70, ractx->sp_hist + 75, 36*sizeof(*block));

    /* block 46 of G.728 spec */
    sum = 32.0;
    for (i=0; i < 10; i++)
        sum -= gain_block[9-i] * ractx->gain_lpc[i];

    /* block 47 of G.728 spec */
    sum = av_clipf(sum, 0, 60);

    /* block 48 of G.728 spec */
    /* exp(sum * 0.1151292546497) == pow(10.0,sum/20) */
    sumsum = exp(sum * 0.1151292546497) * gain * (1.0/(1<<23));

    for (i=0; i < 5; i++)
        buffer[i] = codetable[cb_coef][i] * sumsum;

    sum = avpriv_scalarproduct_float_c(buffer, buffer, 5);

    sum = FFMAX(sum, 5.0 / (1<<24));

    /* shift and store */
    memmove(gain_block, gain_block + 1, 9 * sizeof(*gain_block));

    gain_block[9] = 10 * log10(sum) + (10*log10(((1<<24)/5.)) - 32);

    ff_celp_lp_synthesis_filterf(block, ractx->sp_lpc, buffer, 5, 36);
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
static void do_hybrid_window(RA288Context *ractx,
                             int order, int n, int non_rec, float *out,
                             float *hist, float *out2, const float *window)
{
    int i;
    float buffer1[MAX_BACKWARD_FILTER_ORDER + 1];
    float buffer2[MAX_BACKWARD_FILTER_ORDER + 1];
    LOCAL_ALIGNED(32, float, work, [FFALIGN(MAX_BACKWARD_FILTER_ORDER +
                                            MAX_BACKWARD_FILTER_LEN   +
                                            MAX_BACKWARD_FILTER_NONREC, 16)]);

    av_assert2(order>=0);

    ractx->fdsp.vector_fmul(work, window, hist, FFALIGN(order + n + non_rec, 16));

    convolve(buffer1, work + order    , n      , order);
    convolve(buffer2, work + order + n, non_rec, order);

    for (i=0; i <= order; i++) {
        out2[i] = out2[i] * 0.5625 + buffer1[i];
        out [i] = out2[i]          + buffer2[i];
    }

    /* Multiply by the white noise correcting factor (WNCF). */
    *out *= 257.0 / 256.0;
}

/**
 * Backward synthesis filter, find the LPC coefficients from past speech data.
 */
static void backward_filter(RA288Context *ractx,
                            float *hist, float *rec, const float *window,
                            float *lpc, const float *tab,
                            int order, int n, int non_rec, int move_size)
{
    float temp[MAX_BACKWARD_FILTER_ORDER+1];

    do_hybrid_window(ractx, order, n, non_rec, temp, hist, rec, window);

    if (!compute_lpc_coefs(temp, order, lpc, 0, 1, 1))
        ractx->fdsp.vector_fmul(lpc, lpc, tab, FFALIGN(order, 16));

    memmove(hist, hist + n, move_size*sizeof(*hist));
}

static int ra288_decode_frame(AVCodecContext * avctx, void *data,
                              int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    float *out;
    int i, ret;
    RA288Context *ractx = avctx->priv_data;
    GetBitContext gb;

    if (buf_size < avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR,
               "Error! Input buffer is too small [%d<%d]\n",
               buf_size, avctx->block_align);
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->nb_samples = RA288_BLOCK_SIZE * RA288_BLOCKS_PER_FRAME;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    out = (float *)frame->data[0];

    init_get_bits8(&gb, buf, avctx->block_align);

    for (i=0; i < RA288_BLOCKS_PER_FRAME; i++) {
        float gain = amptable[get_bits(&gb, 3)];
        int cb_coef = get_bits(&gb, 6 + (i&1));

        decode(ractx, gain, cb_coef);

        memcpy(out, &ractx->sp_hist[70 + 36], RA288_BLOCK_SIZE * sizeof(*out));
        out += RA288_BLOCK_SIZE;

        if ((i & 7) == 3) {
            backward_filter(ractx, ractx->sp_hist, ractx->sp_rec, syn_window,
                            ractx->sp_lpc, syn_bw_tab, 36, 40, 35, 70);

            backward_filter(ractx, ractx->gain_hist, ractx->gain_rec, gain_window,
                            ractx->gain_lpc, gain_bw_tab, 10, 8, 20, 28);
        }
    }

    *got_frame_ptr = 1;

    return avctx->block_align;
}

AVCodec ff_ra_288_decoder = {
    .name           = "real_288",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_RA_288,
    .priv_data_size = sizeof(RA288Context),
    .init           = ra288_decode_init,
    .decode         = ra288_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("RealAudio 2.0 (28.8K)"),
};
