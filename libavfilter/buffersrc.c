/*
 * Copyright (c) 2008 Vitor Sessak
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
 * memory buffer source filter
 */

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "audio.h"
#include "avfilter.h"
#include "buffersrc.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "avcodec.h"

typedef struct {
    const AVClass    *class;
    AVFifoBuffer     *fifo;
    AVRational        time_base;     ///< time_base to set in the output link
    AVRational        frame_rate;    ///< frame_rate to set in the output link
    unsigned          nb_failed_requests;
    unsigned          warning_limit;

    /* video only */
    int               w, h;
    enum AVPixelFormat  pix_fmt;
    AVRational        pixel_aspect;
    char              *sws_param;

    /* audio only */
    int sample_rate;
    enum AVSampleFormat sample_fmt;
    char               *sample_fmt_str;
    int channels;
    uint64_t channel_layout;
    char    *channel_layout_str;

    int eof;
} BufferSourceContext;

#define CHECK_VIDEO_PARAM_CHANGE(s, c, width, height, format)\
    if (c->w != width || c->h != height || c->pix_fmt != format) {\
        av_log(s, AV_LOG_INFO, "Changing frame properties on the fly is not supported by all filters.\n");\
    }

#define CHECK_AUDIO_PARAM_CHANGE(s, c, srate, ch_layout, format)\
    if (c->sample_fmt != format || c->sample_rate != srate ||\
        c->channel_layout != ch_layout) {\
        av_log(s, AV_LOG_ERROR, "Changing frame properties on the fly is not supported.\n");\
        return AVERROR(EINVAL);\
    }

int av_buffersrc_add_frame(AVFilterContext *buffer_src,
                           const AVFrame *frame, int flags)
{
    AVFilterBufferRef *picref;
    int ret;

    if (!frame) /* NULL for EOF */
        return av_buffersrc_add_ref(buffer_src, NULL, flags);

    picref = avfilter_get_buffer_ref_from_frame(buffer_src->outputs[0]->type,
                                                frame, AV_PERM_WRITE);
    if (!picref)
        return AVERROR(ENOMEM);
    ret = av_buffersrc_add_ref(buffer_src, picref, flags);
    picref->buf->data[0] = NULL;
    avfilter_unref_buffer(picref);
    return ret;
}

int av_buffersrc_write_frame(AVFilterContext *buffer_filter, const AVFrame *frame)
{
    return av_buffersrc_add_frame(buffer_filter, frame, 0);
}

int av_buffersrc_add_ref(AVFilterContext *s, AVFilterBufferRef *buf, int flags)
{
    BufferSourceContext *c = s->priv;
    AVFilterBufferRef *to_free = NULL;
    int ret;

    if (!buf) {
        c->eof = 1;
        return 0;
    } else if (c->eof)
        return AVERROR(EINVAL);

    if (!av_fifo_space(c->fifo) &&
        (ret = av_fifo_realloc2(c->fifo, av_fifo_size(c->fifo) +
                                         sizeof(buf))) < 0)
        return ret;

    if (!(flags & AV_BUFFERSRC_FLAG_NO_CHECK_FORMAT)) {
        switch (s->outputs[0]->type) {
        case AVMEDIA_TYPE_VIDEO:
            CHECK_VIDEO_PARAM_CHANGE(s, c, buf->video->w, buf->video->h, buf->format);
            break;
        case AVMEDIA_TYPE_AUDIO:
            CHECK_AUDIO_PARAM_CHANGE(s, c, buf->audio->sample_rate, buf->audio->channel_layout,
                                     buf->format);
            break;
        default:
            return AVERROR(EINVAL);
        }
    }
    if (!(flags & AV_BUFFERSRC_FLAG_NO_COPY))
        to_free = buf = ff_copy_buffer_ref(s->outputs[0], buf);
    if(!buf)
        return -1;

    if ((ret = av_fifo_generic_write(c->fifo, &buf, sizeof(buf), NULL)) < 0) {
        avfilter_unref_buffer(to_free);
        return ret;
    }
    c->nb_failed_requests = 0;
    if (c->warning_limit &&
        av_fifo_size(c->fifo) / sizeof(buf) >= c->warning_limit) {
        av_log(s, AV_LOG_WARNING,
               "%d buffers queued in %s, something may be wrong.\n",
               c->warning_limit,
               (char *)av_x_if_null(s->name, s->filter->name));
        c->warning_limit *= 10;
    }

    if ((flags & AV_BUFFERSRC_FLAG_PUSH))
        if ((ret = s->output_pads[0].request_frame(s->outputs[0])) < 0)
            return ret;

    return 0;
}

#ifdef FF_API_BUFFERSRC_BUFFER
int av_buffersrc_buffer(AVFilterContext *s, AVFilterBufferRef *buf)
{
    return av_buffersrc_add_ref(s, buf, AV_BUFFERSRC_FLAG_NO_COPY);
}
#endif

unsigned av_buffersrc_get_nb_failed_requests(AVFilterContext *buffer_src)
{
    return ((BufferSourceContext *)buffer_src->priv)->nb_failed_requests;
}

#define OFFSET(x) offsetof(BufferSourceContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption buffer_options[] = {
    { "time_base",      NULL, OFFSET(time_base),           AV_OPT_TYPE_RATIONAL,   { .dbl = 0 }, 0, INT_MAX, FLAGS },
    { "frame_rate",     NULL, OFFSET(frame_rate),          AV_OPT_TYPE_RATIONAL,   { .dbl = 0 }, 0, INT_MAX, FLAGS },
    { "video_size",     NULL, OFFSET(w),                   AV_OPT_TYPE_IMAGE_SIZE, .flags = FLAGS },
    { "pix_fmt",        NULL, OFFSET(pix_fmt),             AV_OPT_TYPE_PIXEL_FMT,  .flags = FLAGS },
    { "pixel_aspect",   NULL, OFFSET(pixel_aspect),        AV_OPT_TYPE_RATIONAL,   { .dbl = 0 }, 0, INT_MAX, FLAGS },
    { "sws_param",      NULL, OFFSET(sws_param),           AV_OPT_TYPE_STRING,     .flags = FLAGS },
    { NULL },
};
#undef FLAGS

AVFILTER_DEFINE_CLASS(buffer);

static av_cold int init_video(AVFilterContext *ctx, const char *args)
{
    BufferSourceContext *c = ctx->priv;
    char pix_fmt_str[128], sws_param[256] = "", *colon, *equal;
    int ret, n = 0;

    c->class = &buffer_class;

    if (!args) {
        av_log(ctx, AV_LOG_ERROR, "Arguments required\n");
        return AVERROR(EINVAL);
    }
    colon = strchr(args, ':');
    equal = strchr(args, '=');
    if (equal && (!colon || equal < colon)) {
        av_opt_set_defaults(c);
        ret = av_set_options_string(c, args, "=", ":");
        if (ret < 0)
            goto fail;
    } else {
    if ((n = sscanf(args, "%d:%d:%127[^:]:%d:%d:%d:%d:%255c", &c->w, &c->h, pix_fmt_str,
                    &c->time_base.num, &c->time_base.den,
                    &c->pixel_aspect.num, &c->pixel_aspect.den, sws_param)) < 7) {
        av_log(ctx, AV_LOG_ERROR, "Expected at least 7 arguments, but only %d found in '%s'\n", n, args);
        ret = AVERROR(EINVAL);
        goto fail;
    }
    av_log(ctx, AV_LOG_WARNING, "Flat options syntax is deprecated, use key=value pairs\n");

    if ((ret = ff_parse_pixel_format(&c->pix_fmt, pix_fmt_str, ctx)) < 0)
        goto fail;
    c->sws_param = av_strdup(sws_param);
    if (!c->sws_param) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    }

    if (!(c->fifo = av_fifo_alloc(sizeof(AVFilterBufferRef*)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d pixfmt:%s tb:%d/%d fr:%d/%d sar:%d/%d sws_param:%s\n",
           c->w, c->h, av_get_pix_fmt_name(c->pix_fmt),
           c->time_base.num, c->time_base.den, c->frame_rate.num, c->frame_rate.den,
           c->pixel_aspect.num, c->pixel_aspect.den, (char *)av_x_if_null(c->sws_param, ""));
    c->warning_limit = 100;
    return 0;

fail:
    av_opt_free(c);
    return ret;
}

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM
static const AVOption abuffer_options[] = {
    { "time_base",      NULL, OFFSET(time_base),           AV_OPT_TYPE_RATIONAL, { .dbl = 0 }, 0, INT_MAX, FLAGS },
    { "sample_rate",    NULL, OFFSET(sample_rate),         AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "sample_fmt",     NULL, OFFSET(sample_fmt_str),      AV_OPT_TYPE_STRING, .flags = FLAGS },
    { "channels",       NULL, OFFSET(channels),            AV_OPT_TYPE_INT,      { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "channel_layout", NULL, OFFSET(channel_layout_str),  AV_OPT_TYPE_STRING, .flags = FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(abuffer);

static av_cold int init_audio(AVFilterContext *ctx, const char *args)
{
    BufferSourceContext *s = ctx->priv;
    int ret = 0;

    s->class = &abuffer_class;
    av_opt_set_defaults(s);

    if ((ret = av_set_options_string(s, args, "=", ":")) < 0)
        goto fail;

    s->sample_fmt = av_get_sample_fmt(s->sample_fmt_str);
    if (s->sample_fmt == AV_SAMPLE_FMT_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Invalid sample format '%s'\n",
               s->sample_fmt_str);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (s->channel_layout_str) {
        int n;
        /* TODO reindent */
    s->channel_layout = av_get_channel_layout(s->channel_layout_str);
    if (!s->channel_layout) {
        av_log(ctx, AV_LOG_ERROR, "Invalid channel layout '%s'\n",
               s->channel_layout_str);
        ret = AVERROR(EINVAL);
        goto fail;
    }
        n = av_get_channel_layout_nb_channels(s->channel_layout);
        if (s->channels) {
            if (n != s->channels) {
                av_log(ctx, AV_LOG_ERROR,
                       "Mismatching channel count %d and layout '%s' "
                       "(%d channels)\n",
                       s->channels, s->channel_layout_str, n);
                ret = AVERROR(EINVAL);
                goto fail;
            }
        }
        s->channels = n;
    } else if (!s->channels) {
        av_log(ctx, AV_LOG_ERROR, "Neither number of channels nor "
                                  "channel layout specified\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (!(s->fifo = av_fifo_alloc(sizeof(AVFilterBufferRef*)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (!s->time_base.num)
        s->time_base = (AVRational){1, s->sample_rate};

    av_log(ctx, AV_LOG_VERBOSE,
           "tb:%d/%d samplefmt:%s samplerate:%d chlayout:%s\n",
           s->time_base.num, s->time_base.den, s->sample_fmt_str,
           s->sample_rate, s->channel_layout_str);
    s->warning_limit = 100;

fail:
    av_opt_free(s);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BufferSourceContext *s = ctx->priv;
    while (s->fifo && av_fifo_size(s->fifo)) {
        AVFilterBufferRef *buf;
        av_fifo_generic_read(s->fifo, &buf, sizeof(buf), NULL);
        avfilter_unref_buffer(buf);
    }
    av_fifo_free(s->fifo);
    s->fifo = NULL;
    av_freep(&s->sws_param);
}

static int query_formats(AVFilterContext *ctx)
{
    BufferSourceContext *c = ctx->priv;
    AVFilterChannelLayouts *channel_layouts = NULL;
    AVFilterFormats *formats = NULL;
    AVFilterFormats *samplerates = NULL;

    switch (ctx->outputs[0]->type) {
    case AVMEDIA_TYPE_VIDEO:
        ff_add_format(&formats, c->pix_fmt);
        ff_set_common_formats(ctx, formats);
        break;
    case AVMEDIA_TYPE_AUDIO:
        ff_add_format(&formats,           c->sample_fmt);
        ff_set_common_formats(ctx, formats);

        ff_add_format(&samplerates,       c->sample_rate);
        ff_set_common_samplerates(ctx, samplerates);

        ff_add_channel_layout(&channel_layouts,
                              c->channel_layout ? c->channel_layout :
                              FF_COUNT2LAYOUT(c->channels));
        ff_set_common_channel_layouts(ctx, channel_layouts);
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

static int config_props(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;

    switch (link->type) {
    case AVMEDIA_TYPE_VIDEO:
        link->w = c->w;
        link->h = c->h;
        link->sample_aspect_ratio = c->pixel_aspect;
        break;
    case AVMEDIA_TYPE_AUDIO:
        break;
    default:
        return AVERROR(EINVAL);
    }

    link->time_base = c->time_base;
    link->frame_rate = c->frame_rate;
    return 0;
}

static int request_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    AVFilterBufferRef *buf;

    if (!av_fifo_size(c->fifo)) {
        if (c->eof)
            return AVERROR_EOF;
        c->nb_failed_requests++;
        return AVERROR(EAGAIN);
    }
    av_fifo_generic_read(c->fifo, &buf, sizeof(buf), NULL);

    return ff_filter_frame(link, buf);
}

static int poll_frame(AVFilterLink *link)
{
    BufferSourceContext *c = link->src->priv;
    int size = av_fifo_size(c->fifo);
    if (!size && c->eof)
        return AVERROR_EOF;
    return size/sizeof(AVFilterBufferRef*);
}

static const AVFilterPad avfilter_vsrc_buffer_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .poll_frame    = poll_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter avfilter_vsrc_buffer = {
    .name      = "buffer",
    .description = NULL_IF_CONFIG_SMALL("Buffer video frames, and make them accessible to the filterchain."),
    .priv_size = sizeof(BufferSourceContext),
    .query_formats = query_formats,

    .init      = init_video,
    .uninit    = uninit,

    .inputs    = NULL,
    .outputs   = avfilter_vsrc_buffer_outputs,
    .priv_class = &buffer_class,
};

static const AVFilterPad avfilter_asrc_abuffer_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
        .poll_frame    = poll_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter avfilter_asrc_abuffer = {
    .name          = "abuffer",
    .description   = NULL_IF_CONFIG_SMALL("Buffer audio frames, and make them accessible to the filterchain."),
    .priv_size     = sizeof(BufferSourceContext),
    .query_formats = query_formats,

    .init      = init_audio,
    .uninit    = uninit,

    .inputs    = NULL,
    .outputs   = avfilter_asrc_abuffer_outputs,
    .priv_class = &abuffer_class,
};
