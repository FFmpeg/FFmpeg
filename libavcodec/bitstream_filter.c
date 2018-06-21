/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#include <string.h>

#include "avcodec.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#if FF_API_OLD_BSF
FF_DISABLE_DEPRECATION_WARNINGS

const AVBitStreamFilter *av_bitstream_filter_next(const AVBitStreamFilter *f)
{
    const AVBitStreamFilter *filter = NULL;
    void *opaque = NULL;

    while (filter != f)
        filter = av_bsf_iterate(&opaque);

    return av_bsf_iterate(&opaque);
}

void av_register_bitstream_filter(AVBitStreamFilter *bsf)
{
}

typedef struct BSFCompatContext {
    AVBSFContext *ctx;
    int extradata_updated;
} BSFCompatContext;

AVBitStreamFilterContext *av_bitstream_filter_init(const char *name)
{
    AVBitStreamFilterContext *ctx = NULL;
    BSFCompatContext         *priv = NULL;
    const AVBitStreamFilter *bsf;

    bsf = av_bsf_get_by_name(name);
    if (!bsf)
        return NULL;

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return NULL;

    priv = av_mallocz(sizeof(*priv));
    if (!priv)
        goto fail;


    ctx->filter    = bsf;
    ctx->priv_data = priv;

    return ctx;

fail:
    if (priv)
        av_bsf_free(&priv->ctx);
    av_freep(&priv);
    av_freep(&ctx);
    return NULL;
}

void av_bitstream_filter_close(AVBitStreamFilterContext *bsfc)
{
    BSFCompatContext *priv;

    if (!bsfc)
        return;

    priv = bsfc->priv_data;

    av_bsf_free(&priv->ctx);
    av_freep(&bsfc->priv_data);
    av_free(bsfc);
}

int av_bitstream_filter_filter(AVBitStreamFilterContext *bsfc,
                               AVCodecContext *avctx, const char *args,
                               uint8_t **poutbuf, int *poutbuf_size,
                               const uint8_t *buf, int buf_size, int keyframe)
{
    BSFCompatContext *priv = bsfc->priv_data;
    AVPacket pkt = { 0 };
    int ret;

    if (!priv->ctx) {
        ret = av_bsf_alloc(bsfc->filter, &priv->ctx);
        if (ret < 0)
            return ret;

        ret = avcodec_parameters_from_context(priv->ctx->par_in, avctx);
        if (ret < 0)
            return ret;

        priv->ctx->time_base_in = avctx->time_base;

        if (bsfc->args && bsfc->filter->priv_class) {
            const AVOption *opt = av_opt_next(priv->ctx->priv_data, NULL);
            const char * shorthand[2] = {NULL};

            if (opt)
                shorthand[0] = opt->name;

            ret = av_opt_set_from_string(priv->ctx->priv_data, bsfc->args, shorthand, "=", ":");
            if (ret < 0)
                return ret;
        }

        ret = av_bsf_init(priv->ctx);
        if (ret < 0)
            return ret;
    }

    pkt.data = (uint8_t *)buf;
    pkt.size = buf_size;

    ret = av_bsf_send_packet(priv->ctx, &pkt);
    if (ret < 0)
        return ret;

    *poutbuf      = NULL;
    *poutbuf_size = 0;

    ret = av_bsf_receive_packet(priv->ctx, &pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return 0;
    else if (ret < 0)
        return ret;

    *poutbuf = av_malloc(pkt.size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!*poutbuf) {
        av_packet_unref(&pkt);
        return AVERROR(ENOMEM);
    }

    *poutbuf_size = pkt.size;
    memcpy(*poutbuf, pkt.data, pkt.size);

    av_packet_unref(&pkt);

    /* drain all the remaining packets we cannot return */
    while (ret >= 0) {
        ret = av_bsf_receive_packet(priv->ctx, &pkt);
        av_packet_unref(&pkt);
    }

    if (!priv->extradata_updated) {
        /* update extradata in avctx from the output codec parameters */
        if (priv->ctx->par_out->extradata_size && (!args || !strstr(args, "private_spspps_buf"))) {
            av_freep(&avctx->extradata);
            avctx->extradata_size = 0;
            avctx->extradata = av_mallocz(priv->ctx->par_out->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata)
                return AVERROR(ENOMEM);
            memcpy(avctx->extradata, priv->ctx->par_out->extradata, priv->ctx->par_out->extradata_size);
            avctx->extradata_size = priv->ctx->par_out->extradata_size;
        }

        priv->extradata_updated = 1;
    }

    return 1;
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif
