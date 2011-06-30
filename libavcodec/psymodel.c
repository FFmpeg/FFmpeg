/*
 * audio encoder psychoacoustic model
 * Copyright (C) 2008 Konstantin Shishkov
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
#include "psymodel.h"
#include "iirfilter.h"

extern const FFPsyModel ff_aac_psy_model;

av_cold int ff_psy_init(FFPsyContext *ctx, AVCodecContext *avctx, int num_lens,
                        const uint8_t **bands, const int* num_bands,
                        int num_groups, const uint8_t *group_map)
{
    int i, j, k = 0;

    ctx->avctx = avctx;
    ctx->ch        = av_mallocz(sizeof(ctx->ch[0]) * avctx->channels * 2);
    ctx->group     = av_mallocz(sizeof(ctx->group[0]) * num_groups);
    ctx->bands     = av_malloc (sizeof(ctx->bands[0])     * num_lens);
    ctx->num_bands = av_malloc (sizeof(ctx->num_bands[0]) * num_lens);
    memcpy(ctx->bands,     bands,     sizeof(ctx->bands[0])     *  num_lens);
    memcpy(ctx->num_bands, num_bands, sizeof(ctx->num_bands[0]) *  num_lens);

    /* assign channels to groups (with virtual channels for coupling) */
    for (i = 0; i < num_groups; i++) {
        /* NOTE: Add 1 to handle the AAC chan_config without modification.
         *       This has the side effect of allowing an array of 0s to map
         *       to one channel per group.
         */
        ctx->group[i].num_ch = group_map[i] + 1;
        for (j = 0; j < ctx->group[i].num_ch * 2; j++)
            ctx->group[i].ch[j]  = &ctx->ch[k++];
    }

    switch (ctx->avctx->codec_id) {
    case CODEC_ID_AAC:
        ctx->model = &ff_aac_psy_model;
        break;
    }
    if (ctx->model->init)
        return ctx->model->init(ctx);
    return 0;
}

FFPsyChannelGroup *ff_psy_find_group(FFPsyContext *ctx, int channel)
{
    int i = 0, ch = 0;

    while (ch <= channel)
        ch += ctx->group[i++].num_ch;

    return &ctx->group[i-1];
}

av_cold void ff_psy_end(FFPsyContext *ctx)
{
    if (ctx->model->end)
        ctx->model->end(ctx);
    av_freep(&ctx->bands);
    av_freep(&ctx->num_bands);
    av_freep(&ctx->group);
    av_freep(&ctx->ch);
}

typedef struct FFPsyPreprocessContext{
    AVCodecContext *avctx;
    float stereo_att;
    struct FFIIRFilterCoeffs *fcoeffs;
    struct FFIIRFilterState **fstate;
}FFPsyPreprocessContext;

#define FILT_ORDER 4

av_cold struct FFPsyPreprocessContext* ff_psy_preprocess_init(AVCodecContext *avctx)
{
    FFPsyPreprocessContext *ctx;
    int i;
    float cutoff_coeff = 0;
    ctx        = av_mallocz(sizeof(FFPsyPreprocessContext));
    ctx->avctx = avctx;

    if (avctx->cutoff > 0)
        cutoff_coeff = 2.0 * avctx->cutoff / avctx->sample_rate;

    if (cutoff_coeff)
    ctx->fcoeffs = ff_iir_filter_init_coeffs(avctx, FF_FILTER_TYPE_BUTTERWORTH,
                                             FF_FILTER_MODE_LOWPASS, FILT_ORDER,
                                             cutoff_coeff, 0.0, 0.0);
    if (ctx->fcoeffs) {
        ctx->fstate = av_mallocz(sizeof(ctx->fstate[0]) * avctx->channels);
        for (i = 0; i < avctx->channels; i++)
            ctx->fstate[i] = ff_iir_filter_init_state(FILT_ORDER);
    }
    return ctx;
}

void ff_psy_preprocess(struct FFPsyPreprocessContext *ctx,
                       const int16_t *audio, int16_t *dest,
                       int tag, int channels)
{
    int ch, i;
    if (ctx->fstate) {
        for (ch = 0; ch < channels; ch++)
            ff_iir_filter(ctx->fcoeffs, ctx->fstate[tag+ch], ctx->avctx->frame_size,
                          audio + ch, ctx->avctx->channels,
                          dest  + ch, ctx->avctx->channels);
    } else {
        for (ch = 0; ch < channels; ch++)
            for (i = 0; i < ctx->avctx->frame_size; i++)
                dest[i*ctx->avctx->channels + ch] = audio[i*ctx->avctx->channels + ch];
    }
}

av_cold void ff_psy_preprocess_end(struct FFPsyPreprocessContext *ctx)
{
    int i;
    ff_iir_filter_free_coeffs(ctx->fcoeffs);
    if (ctx->fstate)
        for (i = 0; i < ctx->avctx->channels; i++)
            ff_iir_filter_free_state(ctx->fstate[i]);
    av_freep(&ctx->fstate);
    av_free(ctx);
}

