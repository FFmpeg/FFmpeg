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
#include "libavutil/mem.h"

extern const FFPsyModel ff_aac_psy_model;

av_cold int ff_psy_init(FFPsyContext *ctx, AVCodecContext *avctx, int num_lens,
                        const uint8_t **bands, const int* num_bands,
                        int num_groups, const uint8_t *group_map)
{
    int i, j, k = 0;

    ctx->avctx = avctx;
    ctx->ch        = av_calloc(avctx->ch_layout.nb_channels, 2 * sizeof(ctx->ch[0]));
    ctx->group     = av_calloc(num_groups, sizeof(ctx->group[0]));
    ctx->bands     = av_memdup(bands,     num_lens * sizeof(ctx->bands[0]));
    ctx->num_bands = av_memdup(num_bands, num_lens * sizeof(ctx->num_bands[0]));
    ctx->cutoff    = avctx->cutoff;

    if (!ctx->ch || !ctx->group || !ctx->bands || !ctx->num_bands) {
        ff_psy_end(ctx);
        return AVERROR(ENOMEM);
    }

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
    case AV_CODEC_ID_AAC:
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
    if (ctx->model && ctx->model->end)
        ctx->model->end(ctx);
    av_freep(&ctx->bands);
    av_freep(&ctx->num_bands);
    av_freep(&ctx->group);
    av_freep(&ctx->ch);
}
