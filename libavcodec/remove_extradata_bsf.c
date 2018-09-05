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

#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "bsf.h"

enum RemoveFreq {
    REMOVE_FREQ_KEYFRAME,
    REMOVE_FREQ_ALL,
    REMOVE_FREQ_NONKEYFRAME,
};

typedef struct RemoveExtradataContext {
    const AVClass *class;
    int freq;

    AVCodecParserContext *parser;
    AVCodecContext *avctx;
} RemoveExtradataContext;

static int remove_extradata(AVBSFContext *ctx, AVPacket *pkt)
{
    RemoveExtradataContext *s = ctx->priv_data;

    int ret;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    if (s->parser && s->parser->parser->split) {
        if (s->freq == REMOVE_FREQ_ALL ||
            (s->freq == REMOVE_FREQ_NONKEYFRAME && !(pkt->flags & AV_PKT_FLAG_KEY)) ||
            (s->freq == REMOVE_FREQ_KEYFRAME && pkt->flags & AV_PKT_FLAG_KEY)) {
            int i = s->parser->parser->split(s->avctx, pkt->data, pkt->size);
            pkt->data += i;
            pkt->size -= i;
        }
    }

    return 0;
}

static int remove_extradata_init(AVBSFContext *ctx)
{
    RemoveExtradataContext *s = ctx->priv_data;
    int ret;

    s->parser = av_parser_init(ctx->par_in->codec_id);

    if (s->parser) {
        s->avctx = avcodec_alloc_context3(NULL);
        if (!s->avctx)
            return AVERROR(ENOMEM);

        ret = avcodec_parameters_to_context(s->avctx, ctx->par_in);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static void remove_extradata_close(AVBSFContext *ctx)
{
    RemoveExtradataContext *s = ctx->priv_data;

    avcodec_free_context(&s->avctx);
    av_parser_close(s->parser);
}

#define OFFSET(x) offsetof(RemoveExtradataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption options[] = {
    { "freq", NULL, OFFSET(freq), AV_OPT_TYPE_INT, { .i64 = REMOVE_FREQ_KEYFRAME }, REMOVE_FREQ_KEYFRAME, REMOVE_FREQ_NONKEYFRAME, FLAGS, "freq" },
        { "k",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = REMOVE_FREQ_NONKEYFRAME }, .flags = FLAGS, .unit = "freq" },
        { "keyframe", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = REMOVE_FREQ_KEYFRAME }, .flags = FLAGS, .unit = "freq" },
        { "e",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = REMOVE_FREQ_ALL      }, .flags = FLAGS, .unit = "freq" },
        { "all",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = REMOVE_FREQ_ALL      }, .flags = FLAGS, .unit = "freq" },
    { NULL },
};

static const AVClass remove_extradata_class = {
    .class_name = "remove_extradata",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVBitStreamFilter ff_remove_extradata_bsf = {
    .name           = "remove_extra",
    .priv_data_size = sizeof(RemoveExtradataContext),
    .priv_class     = &remove_extradata_class,
    .init           = remove_extradata_init,
    .close          = remove_extradata_close,
    .filter         = remove_extradata,
};
