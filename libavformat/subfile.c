/*
 * Copyright (c) 2014 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "url.h"

typedef struct SubfileContext {
    const AVClass *class;
    URLContext *h;
    int64_t start;
    int64_t end;
    int64_t pos;
} SubfileContext;

#define OFFSET(field) offsetof(SubfileContext, field)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption subfile_options[] = {
    { "start", "start offset", OFFSET(start), AV_OPT_TYPE_INT64, {.i64 = 0}, 0, INT64_MAX, D },
    { "end",   "end offset",   OFFSET(end),   AV_OPT_TYPE_INT64, {.i64 = 0}, 0, INT64_MAX, D },
    { NULL }
};

#undef OFFSET
#undef D

static const AVClass subfile_class = {
    .class_name = "subfile",
    .item_name  = av_default_item_name,
    .option     = subfile_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int slave_seek(URLContext *h)
{
    SubfileContext *c = h->priv_data;
    int64_t ret;

    if ((ret = ffurl_seek(c->h, c->pos, SEEK_SET)) != c->pos) {
        if (ret >= 0)
            ret = AVERROR_BUG;
        av_log(h, AV_LOG_ERROR, "Impossible to seek in file: %s\n",
               av_err2str(ret));
        return ret;
    }
    return 0;
}

static int subfile_open(URLContext *h, const char *filename, int flags,
                        AVDictionary **options)
{
    SubfileContext *c = h->priv_data;
    int ret;

    if (c->end <= c->start) {
        av_log(h, AV_LOG_ERROR, "end before start\n");
        return AVERROR(EINVAL);
    }
    av_strstart(filename, "subfile:", &filename);
    ret = ffurl_open(&c->h, filename, flags, &h->interrupt_callback, options);
    if (ret < 0)
        return ret;
    c->pos = c->start;
    if ((ret = slave_seek(h)) < 0) {
        ffurl_close(c->h);
        return ret;
    }
    return 0;
}

static int subfile_close(URLContext *h)
{
    SubfileContext *c = h->priv_data;
    return ffurl_close(c->h);
}

static int subfile_read(URLContext *h, unsigned char *buf, int size)
{
    SubfileContext *c = h->priv_data;
    int64_t rest = c->end - c->pos;
    int ret;

    if (rest <= 0)
        return 0;
    size = FFMIN(size, rest);
    ret = ffurl_read(c->h, buf, size);
    if (ret >= 0)
        c->pos += ret;
    return ret;
}

static int64_t subfile_seek(URLContext *h, int64_t pos, int whence)
{
    SubfileContext *c = h->priv_data;
    int64_t new_pos = -1;
    int ret;

    if (whence == AVSEEK_SIZE)
        return c->end - c->start;
    switch (whence) {
    case SEEK_SET:
        new_pos = c->start + pos;
        break;
    case SEEK_CUR:
        new_pos += pos;
        break;
    case SEEK_END:
        new_pos = c->end + c->pos;
        break;
    }
    if (new_pos < c->start)
        return AVERROR(EINVAL);
    c->pos = new_pos;
    if ((ret = slave_seek(h)) < 0)
        return ret;
    return c->pos - c->start;
}

URLProtocol ff_subfile_protocol = {
    .name                = "subfile",
    .url_open2           = subfile_open,
    .url_read            = subfile_read,
    .url_seek            = subfile_seek,
    .url_close           = subfile_close,
    .priv_data_size      = sizeof(SubfileContext),
    .priv_data_class     = &subfile_class,
};
