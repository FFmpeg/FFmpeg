/*
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * Options definition for AVCodecContext.
 */

#include "config_components.h"

#include "avcodec.h"
#include "avcodec_internal.h"
#include "codec_internal.h"
#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include <string.h>

FF_DISABLE_DEPRECATION_WARNINGS
#include "options_table.h"
FF_ENABLE_DEPRECATION_WARNINGS

static const char* context_to_name(void* ptr) {
    AVCodecContext *avc= ptr;

    if (avc && avc->codec)
        return avc->codec->name;
    else
        return "NULL";
}

static void *codec_child_next(void *obj, void *prev)
{
    AVCodecContext *s = obj;
    if (!prev && s->codec && s->codec->priv_class && s->priv_data)
        return s->priv_data;
    return NULL;
}

static const AVClass *codec_child_class_iterate(void **iter)
{
    const AVCodec *c;
    /* find next codec with priv options */
    while (c = av_codec_iterate(iter))
        if (c->priv_class)
            return c->priv_class;
    return NULL;
}

static AVClassCategory get_category(void *ptr)
{
    AVCodecContext* avctx = ptr;
    if (avctx->codec && ff_codec_is_decoder(avctx->codec))
        return AV_CLASS_CATEGORY_DECODER;
    else
        return AV_CLASS_CATEGORY_ENCODER;
}

static const AVClass av_codec_context_class = {
    .class_name              = "AVCodecContext",
    .item_name               = context_to_name,
    .option                  = avcodec_options,
    .version                 = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset = offsetof(AVCodecContext, log_level_offset),
    .child_next              = codec_child_next,
    .child_class_iterate     = codec_child_class_iterate,
    .category                = AV_CLASS_CATEGORY_ENCODER,
    .get_category            = get_category,
};

static int init_context_defaults(AVCodecContext *s, const AVCodec *codec)
{
    const FFCodec *const codec2 = ffcodec(codec);
    int flags=0;
    memset(s, 0, sizeof(AVCodecContext));

    s->av_class = &av_codec_context_class;

    s->codec_type = codec ? codec->type : AVMEDIA_TYPE_UNKNOWN;
    if (codec) {
        s->codec = codec;
        s->codec_id = codec->id;
    }

    if(s->codec_type == AVMEDIA_TYPE_AUDIO)
        flags= AV_OPT_FLAG_AUDIO_PARAM;
    else if(s->codec_type == AVMEDIA_TYPE_VIDEO)
        flags= AV_OPT_FLAG_VIDEO_PARAM;
    else if(s->codec_type == AVMEDIA_TYPE_SUBTITLE)
        flags= AV_OPT_FLAG_SUBTITLE_PARAM;
    av_opt_set_defaults2(s, flags, flags);

    av_channel_layout_uninit(&s->ch_layout);

    s->time_base           = (AVRational){0,1};
    s->framerate           = (AVRational){ 0, 1 };
    s->pkt_timebase        = (AVRational){ 0, 1 };
    s->get_buffer2         = avcodec_default_get_buffer2;
    s->get_format          = avcodec_default_get_format;
    s->get_encode_buffer   = avcodec_default_get_encode_buffer;
    s->execute             = avcodec_default_execute;
    s->execute2            = avcodec_default_execute2;
    s->sample_aspect_ratio = (AVRational){0,1};
    s->ch_layout.order     = AV_CHANNEL_ORDER_UNSPEC;
    s->pix_fmt             = AV_PIX_FMT_NONE;
    s->sw_pix_fmt          = AV_PIX_FMT_NONE;
    s->sample_fmt          = AV_SAMPLE_FMT_NONE;

    if(codec && codec2->priv_data_size){
        s->priv_data = av_mallocz(codec2->priv_data_size);
        if (!s->priv_data)
            return AVERROR(ENOMEM);
        if(codec->priv_class){
            *(const AVClass**)s->priv_data = codec->priv_class;
            av_opt_set_defaults(s->priv_data);
        }
    }
    if (codec && codec2->defaults) {
        int ret;
        const FFCodecDefault *d = codec2->defaults;
        while (d->key) {
            ret = av_opt_set(s, d->key, d->value, 0);
            av_assert0(ret >= 0);
            d++;
        }
    }
    return 0;
}

AVCodecContext *avcodec_alloc_context3(const AVCodec *codec)
{
    AVCodecContext *avctx= av_malloc(sizeof(AVCodecContext));

    if (!avctx)
        return NULL;

    if (init_context_defaults(avctx, codec) < 0) {
        av_free(avctx);
        return NULL;
    }

    return avctx;
}

void avcodec_free_context(AVCodecContext **pavctx)
{
    AVCodecContext *avctx = *pavctx;

    if (!avctx)
        return;

    ff_codec_close(avctx);

    av_freep(&avctx->extradata);
    av_freep(&avctx->subtitle_header);
    av_freep(&avctx->intra_matrix);
    av_freep(&avctx->chroma_intra_matrix);
    av_freep(&avctx->inter_matrix);
    av_freep(&avctx->rc_override);
    av_channel_layout_uninit(&avctx->ch_layout);

    av_freep(pavctx);
}

const AVClass *avcodec_get_class(void)
{
    return &av_codec_context_class;
}

#define SROFFSET(x) offsetof(AVSubtitleRect,x)

static const AVOption subtitle_rect_options[]={
{"x", "", SROFFSET(x), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"y", "", SROFFSET(y), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"w", "", SROFFSET(w), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"h", "", SROFFSET(h), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"type", "", SROFFSET(type), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"flags", "", SROFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, 0, 1, 0, .unit = "flags"},
{"forced", "", SROFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, 0, 1, 0},
{NULL},
};

static const AVClass av_subtitle_rect_class = {
    .class_name             = "AVSubtitleRect",
    .item_name              = NULL,
    .option                 = subtitle_rect_options,
    .version                = LIBAVUTIL_VERSION_INT,
};

const AVClass *avcodec_get_subtitle_rect_class(void)
{
    return &av_subtitle_rect_class;
}
