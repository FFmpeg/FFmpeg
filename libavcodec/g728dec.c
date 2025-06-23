/*
 * G.728 decoder
 * Copyright (c) 2025 Peter Ross
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

#include "avcodec.h"
#include "celp_filters.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "g728data.h"
#include "lpc_functions.h"
#include "ra288.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"

#define MAX_BACKWARD_FILTER_ORDER  LPC
#define MAX_BACKWARD_FILTER_LEN    NFRSZ
#define MAX_BACKWARD_FILTER_NONREC NONR
#define ATTEN 0.75f
#include "g728_template.c"

#define LPCW 10 /* Perceptual weighting filter order */
#define GOFF 32.0f /* Log-gain offset value */

static float g728_gq_db[8];
static float g728_y_db[128];
static float g728_wnr_r[FFALIGN(NSBSZ,16)];
static float g728_wnrg_r[FFALIGN(NSBGSZ, 16)];
static float g728_facv_f[FFALIGN(LPC, 16)];

static av_cold void g728_init_static_data(void)
{
    for(int i = 0; i < FF_ARRAY_ELEMS(amptable); i++)
        g728_gq_db[i] = 10.0f*log10f(amptable[i] * amptable[i]);

    for (int i = 0; i < FF_ARRAY_ELEMS(codetable); i++) {
        float cby[IDIM];
        for (int j = 0; j < IDIM; j++)
            cby[j] = codetable[i][j] * (1.0f/(1<<11));
        g728_y_db[i] = 10.0f*log10f(ff_scalarproduct_float_c(cby, cby, IDIM) / IDIM);
    }

    for (int i = 0; i < NSBSZ; i++)
        g728_wnr_r[i] = g728_wnr[NSBSZ - 1 - i] * (1.0f/(1<<15));
    for (int i = 0; i < NSBGSZ; i++)
        g728_wnrg_r[i] = g728_wnrg[NSBGSZ - 1 - i] * (1.0f/(1<<15));
    for (int i = 0; i < LPC; i++)
        g728_facv_f[i] = g728_facv[i] * (1.0f/(1<<14));
}

typedef struct {
    AVFloatDSPContext *fdsp;
    int valid;
    float a[LPC];
    DECLARE_ALIGNED(32, float, sb)[NSBSZ];
    DECLARE_ALIGNED(32, float, sbg)[NSBGSZ];
    DECLARE_ALIGNED(32, float, gp)[FFALIGN(LPCLG, 16)];
    DECLARE_ALIGNED(32, float, atmp)[FFALIGN(LPC, 16)];
    float rexp[LPC + 1];
    float rexpg[LPCLG + 1];
    float r[LPC + 1];
    float alpha;
} G728Context;

static av_cold int g728_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    G728Context *s = avctx->priv_data;

    s->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    s->gp[0] = -1.0f;
    for (int i = 0; i < NUPDATE; i++)
        s->sbg[NSBGSZ - 1 -i] = -GOFF;

    avctx->sample_fmt = AV_SAMPLE_FMT_FLT;

    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;

    ff_thread_once(&init_static_once, g728_init_static_data);
    return 0;
}

static av_cold int g728_decode_close(AVCodecContext *avctx)
{
    G728Context *s = avctx->priv_data;
    av_freep(&s->fdsp);
    return 0;
}

static int hybrid_window(AVFloatDSPContext *fdsp,
                         int order, int n, int non_rec, float *out,
                         const float *hist, float *out2, const float *window)
{
    do_hybrid_window(fdsp->vector_fmul, order, n, non_rec, out, hist, out2, window);
    return out[order] != 0.0f;
}

static void decode_frame(G728Context *s, GetBitContext *gb, float *dst)
{
    float *gstate = s->sbg + NSBGSZ - 2;

    for (int idx = 0; idx < NUPDATE; idx++) {
        DECLARE_ALIGNED(32, float, et)[IDIM];
        float *statelpc = s->sb + NSBSZ - NFRSZ + idx*IDIM;
        float gain, gain_db;
        int is, ig;

        gain_db = 0.0f;
        for (int i = 0; i < LPCLG; i++)
            gain_db -= s->gp[i] * gstate[-i];
        gain_db = av_clipf(gain_db, -GOFF, 28.0f);

        is = get_bits(gb, 7); // shape index
        ig = get_bits(gb, 3); // gain index

        gain = powf(10.0f, (gain_db + GOFF) * .05f) * amptable[ig] * (1.0f/(1<<11));
        for (int i = 0; i < IDIM; i++)
            et[i] = codetable[is][i] * gain;

        ff_celp_lp_synthesis_filterf(statelpc, s->a, et, IDIM, LPC);

        for (int i = 0; i < IDIM; i++) {
            statelpc[i] = av_clipf(statelpc[i], -4095.0f, 4095.0f);
            dst[idx*IDIM + i] = statelpc[i] * (1.0f/(1<<12));
        }

        gstate++;
        *gstate = FFMAX(-GOFF, g728_gq_db[ig] + g728_y_db[is] + gain_db);

        if (idx == 0) {
            DECLARE_ALIGNED(32, float, gptmp)[FFALIGN(LPCLG, 16)];
            if (s->valid && (s->valid = !compute_lpc_coefs(s->r + 1, LPCW, LPC, s->atmp, 0, 0, 1, &s->alpha))) {
                s->fdsp->vector_fmul(s->atmp, s->atmp, g728_facv_f, FFALIGN(LPC, 16));
            }
            if (hybrid_window(s->fdsp, LPCLG, NUPDATE, NONRLG, s->r, s->sbg, s->rexpg, g728_wnrg_r) &&
                    !compute_lpc_coefs(s->r, 0, LPCLG, gptmp, 0, 0, 1, &s->alpha)) {
                s->fdsp->vector_fmul(s->gp, gptmp, gain_bw_tab, FFALIGN(LPCLG, 16));
            }
            memmove(s->sbg, s->sbg + NUPDATE, sizeof(float)*(LPCLG + NONRLG));
            gstate = s->sbg + NSBGSZ - 1 - NUPDATE;
        } else if (idx == 1) {
            if (s->valid)
                memcpy(s->a, s->atmp, sizeof(float)*LPC);
        }
    }

    s->valid = 0;
    if (hybrid_window(s->fdsp, LPC, NFRSZ, NONR, s->r, s->sb, s->rexp, g728_wnr_r)) {
        s->valid = !compute_lpc_coefs(s->r, 0, LPCW, s->atmp, 0, 0, 1, &s->alpha);
    }

    memmove(s->sb, s->sb + NFRSZ, sizeof(float)*(LPC + NONR));
}

static int g728_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    G728Context *s = avctx->priv_data;
    GetBitContext gb;
    int ret;
    int nb_frames = avpkt->size / 5;

    if (!nb_frames)
        return AVERROR_INVALIDDATA;

    if ((ret = init_get_bits8(&gb, avpkt->data, avpkt->size)) < 0)
        return ret;

#define SAMPLES_PER_FRAME 20

    frame->nb_samples = nb_frames * SAMPLES_PER_FRAME;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (int i = 0; i < nb_frames; i++)
        decode_frame(s, &gb, (float *)frame->data[0] + i * 20);

    *got_frame_ptr = 1;

    return nb_frames * 5;
}

const FFCodec ff_g728_decoder = {
    .p.name         = "g728",
    CODEC_LONG_NAME("G.728)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_G728,
    .priv_data_size = sizeof(G728Context),
    .init           = g728_decode_init,
    .close          = g728_decode_close,
    FF_CODEC_DECODE_CB(g728_decode_frame),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
};
