/*
 * utils.c
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdlib.h>
#include "url.h"
#include "avformat.h"


#define IJK_FF_PROTOCOL(x)                                                                          \
extern URLProtocol ff_##x##_protocol;                                                               \
int ijkav_register_##x##_protocol(URLProtocol *protocol, int protocol_size);                        \
int ijkav_register_##x##_protocol(URLProtocol *protocol, int protocol_size)                         \
{                                                                                                   \
    if (protocol_size != sizeof(URLProtocol)) {                                                     \
        av_log(NULL, AV_LOG_ERROR, "ijkav_register_##x##_protocol: ABI mismatch.\n");               \
        return -1;                                                                                  \
    }                                                                                               \
    memcpy(&ff_##x##_protocol, protocol, protocol_size);                                            \
    return 0;                                                                                       \
}

#define IJK_DUMMY_PROTOCOL(x)                                       \
IJK_FF_PROTOCOL(x);                                                 \
static const AVClass ijk_##x##_context_class = {                    \
    .class_name = #x,                                               \
    .item_name  = av_default_item_name,                             \
    .version    = LIBAVUTIL_VERSION_INT,                            \
    };                                                              \
                                                                    \
URLProtocol ff_##x##_protocol = {                                   \
    .name                = #x,                                      \
    .url_open2           = ijkdummy_open,                           \
    .priv_data_size      = 1,                                       \
    .priv_data_class     = &ijk_##x##_context_class,                \
};

static int ijkdummy_open(URLContext *h, const char *arg, int flags, AVDictionary **options)
{
    return -1;
}

IJK_FF_PROTOCOL(async);
IJK_DUMMY_PROTOCOL(ijkmediadatasource);
IJK_DUMMY_PROTOCOL(ijkhttphook);
IJK_DUMMY_PROTOCOL(ijkfilehook);
IJK_DUMMY_PROTOCOL(ijklongurl);
IJK_DUMMY_PROTOCOL(ijksegment);
IJK_DUMMY_PROTOCOL(ijktcphook);
IJK_DUMMY_PROTOCOL(ijkio);

#define IJK_FF_DEMUXER(x)                                                                          \
extern AVInputFormat ff_##x##_demuxer;                                                               \
int ijkav_register_##x##_demuxer(AVInputFormat *demuxer, int demuxer_size);                        \
int ijkav_register_##x##_demuxer(AVInputFormat *demuxer, int demuxer_size)                         \
{                                                                                                   \
    if (demuxer_size != sizeof(AVInputFormat)) {                                                     \
        av_log(NULL, AV_LOG_ERROR, "ijkav_register_##x##_demuxer: ABI mismatch.\n");               \
        return -1;                                                                                  \
    }                                                                                               \
    memcpy(&ff_##x##_demuxer, demuxer, demuxer_size);                                            \
    return 0;                                                                                       \
}

#define IJK_DUMMY_DEMUXER(x)                                        \
IJK_FF_DEMUXER(x);                                                  \
static const AVClass ijk_##x##_demuxer_class = {                    \
    .class_name = #x,                                               \
    .item_name  = av_default_item_name,                             \
    .version    = LIBAVUTIL_VERSION_INT,                            \
    };                                                              \
                                                                    \
AVInputFormat ff_##x##_demuxer = {                                  \
    .name                = #x,                                      \
    .priv_data_size      = 1,                                       \
    .priv_class          = &ijk_##x##_demuxer_class,                \
};

IJK_DUMMY_DEMUXER(ijklivehook);
IJK_DUMMY_DEMUXER(ijkswitch);
IJK_DUMMY_DEMUXER(ijkdash);
IJK_DUMMY_DEMUXER(ijklivedash);
IJK_DUMMY_DEMUXER(ijkioproxy);
IJK_DUMMY_DEMUXER(ijkofflinehook);
IJK_DUMMY_DEMUXER(ijklas);
