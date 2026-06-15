/*
 * libcurl based HTTP(S) protocol
 * Copyright (C) 2026 Kacper Michajłow
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

#include "config_components.h"

#include <curl/curl.h>

#include "libavutil/opt.h"

#include "avformat.h"
#include "internal.h"
#include "url.h"

typedef struct CurlContext {
    const AVClass *class;
} CurlContext;

static int libcurl_open(URLContext *h, const char *url, int flags,
                        AVDictionary **options)
{
    return AVERROR(ENOSYS);
}

static int libcurl_read(URLContext *h, unsigned char *buf, int size)
{
    return AVERROR(ENOSYS);
}

static int libcurl_close(URLContext *h)
{
    return 0;
}

static const AVClass libcurl_context_class = {
    .class_name = "libcurl",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_libcurl_protocol = {
    .name            = "libcurl",
    .url_open2       = libcurl_open,
    .url_read        = libcurl_read,
    .url_close       = libcurl_close,
    .priv_data_size  = sizeof(CurlContext),
    .priv_data_class = &libcurl_context_class,
    .flags           = URL_PROTOCOL_FLAG_NETWORK,
};
