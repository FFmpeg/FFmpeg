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

#include "avcodec.h"
#include "internal.h"
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

    if(avc && avc->codec && avc->codec->name)
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

#if FF_API_CHILD_CLASS_NEXT
static const AVClass *codec_child_class_next(const AVClass *prev)
{
    void *iter = NULL;
    const AVCodec *c = NULL;

    /* find the codec that corresponds to prev */
    while (prev && (c = av_codec_iterate(&iter)))
        if (c->priv_class == prev)
            break;

    /* find next codec with priv options */
    while (c = av_codec_iterate(&iter))
        if (c->priv_class)
            return c->priv_class;
    return NULL;
}
#endif

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
    if(avctx->codec && avctx->codec->decode) return AV_CLASS_CATEGORY_DECODER;
    else                                     return AV_CLASS_CATEGORY_ENCODER;
}

static const AVClass av_codec_context_class = {
    .class_name              = "AVCodecContext",
    .item_name               = context_to_name,
    .option                  = avcodec_options,
    .version                 = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset = offsetof(AVCodecContext, log_level_offset),
    .child_next              = codec_child_next,
#if FF_API_CHILD_CLASS_NEXT
    .child_class_next        = codec_child_class_next,
#endif
    .child_class_iterate     = codec_child_class_iterate,
    .category                = AV_CLASS_CATEGORY_ENCODER,
    .get_category            = get_category,
};

static int init_context_defaults(AVCodecContext *s, const AVCodec *codec)
{
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

    s->time_base           = (AVRational){0,1};
    s->framerate           = (AVRational){ 0, 1 };
    s->pkt_timebase        = (AVRational){ 0, 1 };
    s->get_buffer2         = avcodec_default_get_buffer2;
    s->get_format          = avcodec_default_get_format;
    s->get_encode_buffer   = avcodec_default_get_encode_buffer;
    s->execute             = avcodec_default_execute;
    s->execute2            = avcodec_default_execute2;
    s->sample_aspect_ratio = (AVRational){0,1};
    s->pix_fmt             = AV_PIX_FMT_NONE;
    s->sw_pix_fmt          = AV_PIX_FMT_NONE;
    s->sample_fmt          = AV_SAMPLE_FMT_NONE;

    s->reordered_opaque    = AV_NOPTS_VALUE;
    if(codec && codec->priv_data_size){
        if(!s->priv_data){
            s->priv_data= av_mallocz(codec->priv_data_size);
            if (!s->priv_data) {
                return AVERROR(ENOMEM);
            }
        }
        if(codec->priv_class){
            *(const AVClass**)s->priv_data = codec->priv_class;
            av_opt_set_defaults(s->priv_data);
        }
    }
    if (codec && codec->defaults) {
        int ret;
        const AVCodecDefault *d = codec->defaults;
        while (d->key) {
            ret = av_opt_set(s, d->key, d->value, 0);
            av_assert0(ret >= 0);
            d++;
        }
    }
    return 0;
}

#if FF_API_GET_CONTEXT_DEFAULTS
int avcodec_get_context_defaults3(AVCodecContext *s, const AVCodec *codec)
{
    return init_context_defaults(s, codec);
}
#endif

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

    avcodec_close(avctx);

    av_freep(&avctx->extradata);
    av_freep(&avctx->subtitle_header);
    av_freep(&avctx->intra_matrix);
    av_freep(&avctx->inter_matrix);
    av_freep(&avctx->rc_override);

    av_freep(pavctx);
}

#if FF_API_COPY_CONTEXT
static void copy_context_reset(AVCodecContext *avctx)
{
    int i;

    av_opt_free(avctx);
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    av_frame_free(&avctx->coded_frame);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    av_freep(&avctx->rc_override);
    av_freep(&avctx->intra_matrix);
    av_freep(&avctx->inter_matrix);
    av_freep(&avctx->extradata);
    av_freep(&avctx->subtitle_header);
    av_buffer_unref(&avctx->hw_frames_ctx);
    av_buffer_unref(&avctx->hw_device_ctx);
    for (i = 0; i < avctx->nb_coded_side_data; i++)
        av_freep(&avctx->coded_side_data[i].data);
    av_freep(&avctx->coded_side_data);
    avctx->subtitle_header_size = 0;
    avctx->nb_coded_side_data = 0;
    avctx->extradata_size = 0;
}

int avcodec_copy_context(AVCodecContext *dest, const AVCodecContext *src)
{
    const AVCodec *orig_codec = dest->codec;
    uint8_t *orig_priv_data = dest->priv_data;

    if (avcodec_is_open(dest)) { // check that the dest context is uninitialized
        av_log(dest, AV_LOG_ERROR,
               "Tried to copy AVCodecContext %p into already-initialized %p\n",
               src, dest);
        return AVERROR(EINVAL);
    }

    copy_context_reset(dest);

    memcpy(dest, src, sizeof(*dest));
    av_opt_copy(dest, src);

    dest->priv_data       = orig_priv_data;
    dest->codec           = orig_codec;

    if (orig_priv_data && src->codec && src->codec->priv_class &&
        dest->codec && dest->codec->priv_class)
        av_opt_copy(orig_priv_data, src->priv_data);


    /* set values specific to opened codecs back to their default state */
    dest->slice_offset    = NULL;
    dest->hwaccel         = NULL;
    dest->internal        = NULL;
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    dest->coded_frame     = NULL;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    /* reallocate values that should be allocated separately */
    dest->extradata       = NULL;
    dest->coded_side_data = NULL;
    dest->intra_matrix    = NULL;
    dest->inter_matrix    = NULL;
    dest->rc_override     = NULL;
    dest->subtitle_header = NULL;
    dest->hw_frames_ctx   = NULL;
    dest->hw_device_ctx   = NULL;
    dest->nb_coded_side_data = 0;

#define alloc_and_copy_or_fail(obj, size, pad) \
    if (src->obj && size > 0) { \
        dest->obj = av_malloc(size + pad); \
        if (!dest->obj) \
            goto fail; \
        memcpy(dest->obj, src->obj, size); \
        if (pad) \
            memset(((uint8_t *) dest->obj) + size, 0, pad); \
    }
    alloc_and_copy_or_fail(extradata,    src->extradata_size,
                           AV_INPUT_BUFFER_PADDING_SIZE);
    dest->extradata_size  = src->extradata_size;
    alloc_and_copy_or_fail(intra_matrix, 64 * sizeof(int16_t), 0);
    alloc_and_copy_or_fail(inter_matrix, 64 * sizeof(int16_t), 0);
    alloc_and_copy_or_fail(rc_override,  src->rc_override_count * sizeof(*src->rc_override), 0);
    alloc_and_copy_or_fail(subtitle_header, src->subtitle_header_size, 1);
    av_assert0(dest->subtitle_header_size == src->subtitle_header_size);
#undef alloc_and_copy_or_fail

    if (src->hw_frames_ctx) {
        dest->hw_frames_ctx = av_buffer_ref(src->hw_frames_ctx);
        if (!dest->hw_frames_ctx)
            goto fail;
    }

    return 0;

fail:
    copy_context_reset(dest);
    return AVERROR(ENOMEM);
}
#endif

const AVClass *avcodec_get_class(void)
{
    return &av_codec_context_class;
}

#if FF_API_GET_FRAME_CLASS
#define FOFFSET(x) offsetof(AVFrame,x)

static const AVOption frame_options[]={
{"best_effort_timestamp", "", FOFFSET(best_effort_timestamp), AV_OPT_TYPE_INT64, {.i64 = AV_NOPTS_VALUE }, INT64_MIN, INT64_MAX, 0},
{"pkt_pos", "", FOFFSET(pkt_pos), AV_OPT_TYPE_INT64, {.i64 = -1 }, INT64_MIN, INT64_MAX, 0},
{"pkt_size", "", FOFFSET(pkt_size), AV_OPT_TYPE_INT64, {.i64 = -1 }, INT64_MIN, INT64_MAX, 0},
{"sample_aspect_ratio", "", FOFFSET(sample_aspect_ratio), AV_OPT_TYPE_RATIONAL, {.dbl = 0 }, 0, INT_MAX, 0},
{"width", "", FOFFSET(width), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"height", "", FOFFSET(height), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"format", "", FOFFSET(format), AV_OPT_TYPE_INT, {.i64 = -1 }, 0, INT_MAX, 0},
{"channel_layout", "", FOFFSET(channel_layout), AV_OPT_TYPE_INT64, {.i64 = 0 }, 0, INT64_MAX, 0},
{"sample_rate", "", FOFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{NULL},
};

static const AVClass av_frame_class = {
    .class_name              = "AVFrame",
    .item_name               = NULL,
    .option                  = frame_options,
    .version                 = LIBAVUTIL_VERSION_INT,
};

const AVClass *avcodec_get_frame_class(void)
{
    return &av_frame_class;
}
#endif

#define SROFFSET(x) offsetof(AVSubtitleRect,x)

static const AVOption subtitle_rect_options[]={
{"x", "", SROFFSET(x), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"y", "", SROFFSET(y), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"w", "", SROFFSET(w), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"h", "", SROFFSET(h), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"type", "", SROFFSET(type), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, 0},
{"flags", "", SROFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, 0, 1, 0, "flags"},
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
