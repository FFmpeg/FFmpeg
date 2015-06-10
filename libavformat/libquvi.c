/*
 * Copyright (c) 2013 Clément Bœsch
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

#include <quvi/quvi.h>

#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"

typedef struct {
    const AVClass *class;
    char *format;
    AVFormatContext *fmtctx;
} LibQuviContext;

#define OFFSET(x) offsetof(LibQuviContext, x)
#define FLAGS AV_OPT_FLAG_DECODING_PARAM
static const AVOption libquvi_options[] = {
    { "format", "request specific format", OFFSET(format), AV_OPT_TYPE_STRING, {.str="best"}, .flags = FLAGS },
    { NULL }
};

static const AVClass libquvi_context_class = {
    .class_name     = "libquvi",
    .item_name      = av_default_item_name,
    .option         = libquvi_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int libquvi_close(AVFormatContext *s)
{
    LibQuviContext *qc = s->priv_data;
    if (qc->fmtctx)
        avformat_close_input(&qc->fmtctx);
    return 0;
}

static int libquvi_read_header(AVFormatContext *s)
{
    int i, ret;
    quvi_t q;
    quvi_media_t m;
    QUVIcode rc;
    LibQuviContext *qc = s->priv_data;
    char *media_url, *pagetitle;

    rc = quvi_init(&q);
    if (rc != QUVI_OK) {
        av_log(s, AV_LOG_ERROR, "%s\n", quvi_strerror(q, rc));
        return AVERROR_EXTERNAL;
    }

    quvi_setopt(q, QUVIOPT_FORMAT, qc->format);

    rc = quvi_parse(q, s->filename, &m);
    if (rc != QUVI_OK) {
        av_log(s, AV_LOG_ERROR, "%s\n", quvi_strerror(q, rc));
        ret = AVERROR_EXTERNAL;
        goto err_quvi_close;
    }

    rc = quvi_getprop(m, QUVIPROP_MEDIAURL, &media_url);
    if (rc != QUVI_OK) {
        av_log(s, AV_LOG_ERROR, "%s\n", quvi_strerror(q, rc));
        ret = AVERROR_EXTERNAL;
        goto err_quvi_cleanup;
    }

    if (!(qc->fmtctx = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto err_quvi_cleanup;
    }

    if ((ret = ff_copy_whitelists(qc->fmtctx, s)) < 0) {
        avformat_free_context(qc->fmtctx);
        qc->fmtctx = NULL;
        goto err_quvi_cleanup;
    }

    ret = avformat_open_input(&qc->fmtctx, media_url, NULL, NULL);
    if (ret < 0)
        goto err_quvi_cleanup;

    rc = quvi_getprop(m, QUVIPROP_PAGETITLE, &pagetitle);
    if (rc == QUVI_OK)
        av_dict_set(&s->metadata, "title", pagetitle, 0);

    for (i = 0; i < qc->fmtctx->nb_streams; i++) {
        AVStream *st = avformat_new_stream(s, NULL);
        AVStream *ist = qc->fmtctx->streams[i];
        if (!st) {
            ret = AVERROR(ENOMEM);
            goto err_close_input;
        }
        avpriv_set_pts_info(st, ist->pts_wrap_bits, ist->time_base.num, ist->time_base.den);
        avcodec_copy_context(st->codec, qc->fmtctx->streams[i]->codec);
    }

    return 0;

  err_close_input:
    avformat_close_input(&qc->fmtctx);
  err_quvi_cleanup:
    quvi_parse_close(&m);
  err_quvi_close:
    quvi_close(&q);
    return ret;
}

static int libquvi_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    LibQuviContext *qc = s->priv_data;
    return av_read_frame(qc->fmtctx, pkt);
}

static int libquvi_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    LibQuviContext *qc = s->priv_data;
    return av_seek_frame(qc->fmtctx, stream_index, timestamp, flags);
}

static int libquvi_probe(AVProbeData *p)
{
    int score;
    quvi_t q;
    QUVIcode rc;

    rc = quvi_init(&q);
    if (rc != QUVI_OK)
        return AVERROR(ENOMEM);
    score = quvi_supported(q, (char *)p->filename) == QUVI_OK ? AVPROBE_SCORE_EXTENSION : 0;
    quvi_close(&q);
    return score;
}

AVInputFormat ff_libquvi_demuxer = {
    .name           = "libquvi",
    .long_name      = NULL_IF_CONFIG_SMALL("libquvi demuxer"),
    .priv_data_size = sizeof(LibQuviContext),
    .read_probe     = libquvi_probe,
    .read_header    = libquvi_read_header,
    .read_packet    = libquvi_read_packet,
    .read_close     = libquvi_close,
    .read_seek      = libquvi_read_seek,
    .priv_class     = &libquvi_context_class,
    .flags          = AVFMT_NOFILE,
};
