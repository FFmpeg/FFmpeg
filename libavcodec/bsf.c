/*
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

#include <string.h>

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "bsf.h"

struct AVBSFInternal {
    AVPacket *buffer_pkt;
    int eof;
};

void av_bsf_free(AVBSFContext **pctx)
{
    AVBSFContext *ctx;

    if (!pctx || !*pctx)
        return;
    ctx = *pctx;

    if (ctx->filter->close)
        ctx->filter->close(ctx);
    if (ctx->filter->priv_class && ctx->priv_data)
        av_opt_free(ctx->priv_data);

    av_opt_free(ctx);

    av_packet_free(&ctx->internal->buffer_pkt);
    av_freep(&ctx->internal);
    av_freep(&ctx->priv_data);

    avcodec_parameters_free(&ctx->par_in);
    avcodec_parameters_free(&ctx->par_out);

    av_freep(pctx);
}

static void *bsf_child_next(void *obj, void *prev)
{
    AVBSFContext *ctx = obj;
    if (!prev && ctx->filter->priv_class)
        return ctx->priv_data;
    return NULL;
}

static const AVClass bsf_class = {
    .class_name       = "AVBSFContext",
    .item_name        = av_default_item_name,
    .version          = LIBAVUTIL_VERSION_INT,
    .child_next       = bsf_child_next,
    .child_class_next = ff_bsf_child_class_next,
};

const AVClass *av_bsf_get_class(void)
{
    return &bsf_class;
}

int av_bsf_alloc(const AVBitStreamFilter *filter, AVBSFContext **pctx)
{
    AVBSFContext *ctx;
    int ret;

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    ctx->av_class = &bsf_class;
    ctx->filter   = filter;

    ctx->par_in  = avcodec_parameters_alloc();
    ctx->par_out = avcodec_parameters_alloc();
    if (!ctx->par_in || !ctx->par_out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->internal = av_mallocz(sizeof(*ctx->internal));
    if (!ctx->internal) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->internal->buffer_pkt = av_packet_alloc();
    if (!ctx->internal->buffer_pkt) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    av_opt_set_defaults(ctx);

    /* allocate priv data and init private options */
    if (filter->priv_data_size) {
        ctx->priv_data = av_mallocz(filter->priv_data_size);
        if (!ctx->priv_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (filter->priv_class) {
            *(const AVClass **)ctx->priv_data = filter->priv_class;
            av_opt_set_defaults(ctx->priv_data);
        }
    }

    *pctx = ctx;
    return 0;
fail:
    av_bsf_free(&ctx);
    return ret;
}

int av_bsf_init(AVBSFContext *ctx)
{
    int ret, i;

    /* check that the codec is supported */
    if (ctx->filter->codec_ids) {
        for (i = 0; ctx->filter->codec_ids[i] != AV_CODEC_ID_NONE; i++)
            if (ctx->par_in->codec_id == ctx->filter->codec_ids[i])
                break;
        if (ctx->filter->codec_ids[i] == AV_CODEC_ID_NONE) {
            const AVCodecDescriptor *desc = avcodec_descriptor_get(ctx->par_in->codec_id);
            av_log(ctx, AV_LOG_ERROR, "Codec '%s' (%d) is not supported by the "
                   "bitstream filter '%s'. Supported codecs are: ",
                   desc ? desc->name : "unknown", ctx->par_in->codec_id, ctx->filter->name);
            for (i = 0; ctx->filter->codec_ids[i] != AV_CODEC_ID_NONE; i++) {
                desc = avcodec_descriptor_get(ctx->filter->codec_ids[i]);
                av_log(ctx, AV_LOG_ERROR, "%s (%d) ",
                       desc ? desc->name : "unknown", ctx->filter->codec_ids[i]);
            }
            av_log(ctx, AV_LOG_ERROR, "\n");
            return AVERROR(EINVAL);
        }
    }

    /* initialize output parameters to be the same as input
     * init below might overwrite that */
    ret = avcodec_parameters_copy(ctx->par_out, ctx->par_in);
    if (ret < 0)
        return ret;

    ctx->time_base_out = ctx->time_base_in;

    if (ctx->filter->init) {
        ret = ctx->filter->init(ctx);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int av_bsf_send_packet(AVBSFContext *ctx, AVPacket *pkt)
{
    if (!pkt || !pkt->data) {
        ctx->internal->eof = 1;
        return 0;
    }

    if (ctx->internal->eof) {
        av_log(ctx, AV_LOG_ERROR, "A non-NULL packet sent after an EOF.\n");
        return AVERROR(EINVAL);
    }

    if (ctx->internal->buffer_pkt->data ||
        ctx->internal->buffer_pkt->side_data_elems)
        return AVERROR(EAGAIN);

    av_packet_move_ref(ctx->internal->buffer_pkt, pkt);

    return 0;
}

int av_bsf_receive_packet(AVBSFContext *ctx, AVPacket *pkt)
{
    return ctx->filter->filter(ctx, pkt);
}

int ff_bsf_get_packet(AVBSFContext *ctx, AVPacket **pkt)
{
    AVBSFInternal *in = ctx->internal;
    AVPacket *tmp_pkt;

    if (in->eof)
        return AVERROR_EOF;

    if (!ctx->internal->buffer_pkt->data &&
        !ctx->internal->buffer_pkt->side_data_elems)
        return AVERROR(EAGAIN);

    tmp_pkt = av_packet_alloc();
    if (!tmp_pkt)
        return AVERROR(ENOMEM);

    *pkt = ctx->internal->buffer_pkt;
    ctx->internal->buffer_pkt = tmp_pkt;

    return 0;
}
