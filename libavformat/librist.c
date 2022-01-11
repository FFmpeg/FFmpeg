/*
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
 * Reliable Internet Streaming Transport protocol
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"

#include "avformat.h"
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"

#include <librist/librist.h>
#include <librist/version.h>

// RIST_MAX_PACKET_SIZE - 28 minimum protocol overhead
#define MAX_PAYLOAD_SIZE (10000-28)

#define FF_LIBRIST_MAKE_VERSION(major, minor, patch) \
    ((patch) + ((minor)* 0x100) + ((major) *0x10000))
#define FF_LIBRIST_VERSION FF_LIBRIST_MAKE_VERSION(LIBRIST_API_VERSION_MAJOR, LIBRIST_API_VERSION_MINOR, LIBRIST_API_VERSION_PATCH)
#define FF_LIBRIST_VERSION_41 FF_LIBRIST_MAKE_VERSION(4, 1, 0)
#define FF_LIBRIST_VERSION_42 FF_LIBRIST_MAKE_VERSION(4, 2, 0)

#define FIFO_SIZE_DEFAULT 8192

typedef struct RISTContext {
    const AVClass *class;

    int profile;
    int buffer_size;
    int packet_size;
    int log_level;
    int encryption;
    int fifo_size;
    int overrun_nonfatal;
    char *secret;

    struct rist_logging_settings logging_settings;
    struct rist_peer_config peer_config;

    struct rist_peer *peer;
    struct rist_ctx *ctx;
} RISTContext;

#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
#define OFFSET(x) offsetof(RISTContext, x)
static const AVOption librist_options[] = {
    { "rist_profile","set profile",     OFFSET(profile),     AV_OPT_TYPE_INT,   {.i64=RIST_PROFILE_MAIN},     0, 2, .flags = D|E, "profile" },
    { "simple",      NULL,              0,                   AV_OPT_TYPE_CONST, {.i64=RIST_PROFILE_SIMPLE},   0, 0, .flags = D|E, "profile" },
    { "main",        NULL,              0,                   AV_OPT_TYPE_CONST, {.i64=RIST_PROFILE_MAIN},     0, 0, .flags = D|E, "profile" },
    { "advanced",    NULL,              0,                   AV_OPT_TYPE_CONST, {.i64=RIST_PROFILE_ADVANCED}, 0, 0, .flags = D|E, "profile" },
    { "buffer_size", "set buffer_size in ms", OFFSET(buffer_size), AV_OPT_TYPE_INT, {.i64=0},                 0, 30000, .flags = D|E },
    { "fifo_size",   "set fifo buffer size, must be a power of 2", OFFSET(fifo_size), AV_OPT_TYPE_INT, {.i64=FIFO_SIZE_DEFAULT}, 32, 262144, .flags = D|E },
    { "overrun_nonfatal", "survive in case of receiving fifo buffer overrun", OFFSET(overrun_nonfatal), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1,    D },
    { "pkt_size",    "set packet size", OFFSET(packet_size), AV_OPT_TYPE_INT,   {.i64=1316},                  1, MAX_PAYLOAD_SIZE,    .flags = D|E },
    { "log_level",   "set loglevel",    OFFSET(log_level),   AV_OPT_TYPE_INT,   {.i64=RIST_LOG_INFO},        -1, INT_MAX, .flags = D|E },
    { "secret", "set encryption secret",OFFSET(secret),      AV_OPT_TYPE_STRING,{.str=NULL},                  0, 0,       .flags = D|E },
    { "encryption","set encryption type",OFFSET(encryption), AV_OPT_TYPE_INT   ,{.i64=0},                     0, INT_MAX, .flags = D|E },
    { NULL }
};

static int risterr2ret(int err)
{
    switch (err) {
    case RIST_ERR_MALLOC:
        return AVERROR(ENOMEM);
    default:
        return AVERROR_EXTERNAL;
    }
}

static int log_cb(void *arg, enum rist_log_level log_level, const char *msg)
{
    int level;

    switch (log_level) {
    case RIST_LOG_ERROR:    level = AV_LOG_ERROR;   break;
    case RIST_LOG_WARN:     level = AV_LOG_WARNING; break;
    case RIST_LOG_NOTICE:   level = AV_LOG_INFO;    break;
    case RIST_LOG_INFO:     level = AV_LOG_VERBOSE; break;
    case RIST_LOG_DEBUG:    level = AV_LOG_DEBUG;   break;
    case RIST_LOG_DISABLE:  level = AV_LOG_QUIET;   break;
    default: level = AV_LOG_WARNING;
    }

    av_log(arg, level, "%s", msg);

    return 0;
}

static int librist_close(URLContext *h)
{
    RISTContext *s = h->priv_data;
    int ret = 0;

    s->peer = NULL;

    if (s->ctx)
        ret = rist_destroy(s->ctx);
    s->ctx = NULL;

    return risterr2ret(ret);
}

static int librist_open(URLContext *h, const char *uri, int flags)
{
    RISTContext *s = h->priv_data;
    struct rist_logging_settings *logging_settings = &s->logging_settings;
    struct rist_peer_config *peer_config = &s->peer_config;
    int ret;

    if ((flags & AVIO_FLAG_READ_WRITE) == AVIO_FLAG_READ_WRITE)
        return AVERROR(EINVAL);

    s->logging_settings = (struct rist_logging_settings)LOGGING_SETTINGS_INITIALIZER;
    ret = rist_logging_set(&logging_settings, s->log_level, log_cb, h, NULL, NULL);
    if (ret < 0)
        return risterr2ret(ret);

    if (flags & AVIO_FLAG_WRITE) {
        h->max_packet_size = s->packet_size;
        ret = rist_sender_create(&s->ctx, s->profile, 0, logging_settings);
    }
    if (ret < 0)
        goto err;

    if (flags & AVIO_FLAG_READ) {
        h->max_packet_size = MAX_PAYLOAD_SIZE;
        ret = rist_receiver_create(&s->ctx, s->profile, logging_settings);
    }
    if (ret < 0)
        goto err;

    ret = rist_peer_config_defaults_set(peer_config);
    if (ret < 0)
        goto err;

#if FF_LIBRIST_VERSION < FF_LIBRIST_VERSION_41
    ret = rist_parse_address(uri, (const struct rist_peer_config **)&peer_config);
#else
    ret = rist_parse_address2(uri, &peer_config);
#endif
    if (ret < 0)
        goto err;

    if (flags & AVIO_FLAG_READ) {
//Prior to 4.2.0 there was a bug in librist which made this call always fail.
#if FF_LIBRIST_VERSION >= FF_LIBRIST_VERSION_42
        ret = rist_receiver_set_output_fifo_size(s->ctx, s->fifo_size);
        if (ret != 0)
            goto err;
#else
        if (s->fifo_size != FIFO_SIZE_DEFAULT)
            av_log(h, AV_LOG_ERROR, "librist prior to 0.2.7 has a bug which fails setting the fifo buffer size\n");
#endif
    }

    if (((s->encryption == 128 || s->encryption == 256) && !s->secret) ||
        ((peer_config->key_size == 128 || peer_config->key_size == 256) && !peer_config->secret[0])) {
        av_log(h, AV_LOG_ERROR, "secret is mandatory if encryption is enabled\n");
        librist_close(h);
        return AVERROR(EINVAL);
    }

    if (s->secret && peer_config->secret[0] == 0)
        av_strlcpy(peer_config->secret, s->secret, RIST_MAX_STRING_SHORT);

    if (s->secret && (s->encryption == 128 || s->encryption == 256))
        peer_config->key_size = s->encryption;

    if (s->buffer_size) {
        peer_config->recovery_length_min = s->buffer_size;
        peer_config->recovery_length_max = s->buffer_size;
    }

    ret = rist_peer_create(s->ctx, &s->peer, &s->peer_config);
    if (ret < 0)
        goto err;

    ret = rist_start(s->ctx);
    if (ret < 0)
        goto err;

    return 0;

err:
    librist_close(h);

    return risterr2ret(ret);
}

static int librist_read(URLContext *h, uint8_t *buf, int size)
{
    RISTContext *s = h->priv_data;
    int ret;

#if FF_LIBRIST_VERSION < FF_LIBRIST_VERSION_41
    const struct rist_data_block *data_block;
    ret = rist_receiver_data_read(s->ctx, &data_block, POLLING_TIME);
#else
    struct rist_data_block *data_block;
    ret = rist_receiver_data_read2(s->ctx, &data_block, POLLING_TIME);
#endif

    if (ret < 0)
        return risterr2ret(ret);

    if (ret == 0)
        return AVERROR(EAGAIN);

    if (data_block->payload_len > MAX_PAYLOAD_SIZE) {
#if FF_LIBRIST_VERSION < FF_LIBRIST_VERSION_41
        rist_receiver_data_block_free((struct rist_data_block**)&data_block);
#else
        rist_receiver_data_block_free2(&data_block);
#endif
        return AVERROR_EXTERNAL;
    }

#if FF_LIBRIST_VERSION >= FF_LIBRIST_VERSION_42
    if (data_block->flags & RIST_DATA_FLAGS_OVERFLOW) {
        if (!s->overrun_nonfatal) {
            av_log(h, AV_LOG_ERROR, "Fifo buffer overrun. "
                    "To avoid, increase fifo_size option. "
                    "To survive in such case, use overrun_nonfatal option\n");
            size = AVERROR(EIO);
            goto out_free;
        }
    }
#endif

    size = data_block->payload_len;
    memcpy(buf, data_block->payload, size);
out_free:
#if FF_LIBRIST_VERSION < FF_LIBRIST_VERSION_41
    rist_receiver_data_block_free((struct rist_data_block**)&data_block);
#else
    rist_receiver_data_block_free2(&data_block);
#endif
    return size;
}

static int librist_write(URLContext *h, const uint8_t *buf, int size)
{
    RISTContext *s = h->priv_data;
    struct rist_data_block data_block = { 0 };
    int ret;

    data_block.ts_ntp = 0;
    data_block.payload = buf;
    data_block.payload_len = size;

    ret = rist_sender_data_write(s->ctx, &data_block);
    if (ret < 0)
        return risterr2ret(ret);

    return ret;
}

static const AVClass librist_class = {
    .class_name = "librist",
    .item_name  = av_default_item_name,
    .option     = librist_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_librist_protocol = {
    .name                = "rist",
    .url_open            = librist_open,
    .url_read            = librist_read,
    .url_write           = librist_write,
    .url_close           = librist_close,
    .priv_data_size      = sizeof(RISTContext),
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class     = &librist_class,
};
