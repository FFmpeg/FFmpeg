/*
 * Icecast protocol for FFmpeg
 * Copyright (c) 2014 Marvin Scholz
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


#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"

#include "avformat.h"
#include "network.h"


typedef struct IcecastContext {
    const AVClass *class;
    URLContext *hd;
    int send_started;
    char *user;
    // Options
    char *content_type;
    char *description;
    char *genre;
    int legacy_icecast;
    char *name;
    char *pass;
    int public;
    char *url;
    char *user_agent;
} IcecastContext;

#define DEFAULT_ICE_USER "source"

#define NOT_EMPTY(s) (s && s[0])

#define OFFSET(x) offsetof(IcecastContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "ice_genre", "set stream genre", OFFSET(genre), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "ice_name", "set stream description", OFFSET(name), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "ice_description", "set stream description", OFFSET(description), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "ice_url", "set stream website", OFFSET(url), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "ice_public", "set if stream is public", OFFSET(public), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, E },
    { "user_agent", "override User-Agent header", OFFSET(user_agent), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "password", "set password", OFFSET(pass), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "content_type", "set content-type, MUST be set if not audio/mpeg", OFFSET(content_type), AV_OPT_TYPE_STRING, { 0 }, 0, 0, E },
    { "legacy_icecast", "use legacy SOURCE method, for Icecast < v2.4", OFFSET(legacy_icecast), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, E },
    { NULL }
};


static void cat_header(AVBPrint *bp, const char key[], const char value[])
{
    if (NOT_EMPTY(value))
        av_bprintf(bp, "%s: %s\r\n", key, value);
}

static int icecast_close(URLContext *h)
{
    IcecastContext *s = h->priv_data;
    if (s->hd)
        ffurl_close(s->hd);
    return 0;
}

static int icecast_open(URLContext *h, const char *uri, int flags)
{
    IcecastContext *s = h->priv_data;

    // Dict to set options that we pass to the HTTP protocol
    AVDictionary *opt_dict = NULL;

    // URI part variables
    char h_url[1024], host[1024], auth[1024], path[1024];
    char *headers = NULL, *user = NULL;
    int port, ret;
    AVBPrint bp;

    if (flags & AVIO_FLAG_READ)
        return AVERROR(ENOSYS);

    av_bprint_init(&bp, 0, 1);

    // Build header strings
    cat_header(&bp, "Ice-Name", s->name);
    cat_header(&bp, "Ice-Description", s->description);
    cat_header(&bp, "Ice-URL", s->url);
    cat_header(&bp, "Ice-Genre", s->genre);
    cat_header(&bp, "Ice-Public", s->public ? "1" : "0");
    if (!av_bprint_is_complete(&bp)) {
        ret = AVERROR(ENOMEM);
        goto cleanup;
    }
    av_bprint_finalize(&bp, &headers);

    // Set options
    av_dict_set(&opt_dict, "method", s->legacy_icecast ? "SOURCE" : "PUT", 0);
    av_dict_set(&opt_dict, "auth_type", "basic", 0);
    av_dict_set(&opt_dict, "headers", headers, 0);
    av_dict_set(&opt_dict, "chunked_post", "0", 0);
    av_dict_set(&opt_dict, "send_expect_100", s->legacy_icecast ? "0" : "1", 0);
    if (NOT_EMPTY(s->content_type))
        av_dict_set(&opt_dict, "content_type", s->content_type, 0);
    else
        av_dict_set(&opt_dict, "content_type", "audio/mpeg", 0);
    if (NOT_EMPTY(s->user_agent))
        av_dict_set(&opt_dict, "user_agent", s->user_agent, 0);

    // Parse URI
    av_url_split(NULL, 0, auth, sizeof(auth), host, sizeof(host),
                 &port, path, sizeof(path), uri);

    // Check for auth data in URI
    if (auth[0]) {
        char *sep = strchr(auth, ':');
        if (sep) {
            *sep = 0;
            sep++;
            if (s->pass) {
                av_free(s->pass);
                av_log(h, AV_LOG_WARNING, "Overwriting -password <pass> with URI password!\n");
            }
            if (!(s->pass = av_strdup(sep))) {
                ret = AVERROR(ENOMEM);
                goto cleanup;
            }
        }
        if (!(user = av_strdup(auth))) {
            ret = AVERROR(ENOMEM);
            goto cleanup;
        }
    }

    // Build new authstring
    snprintf(auth, sizeof(auth),
             "%s:%s",
             user ? user : DEFAULT_ICE_USER,
             s->pass ? s->pass : "");

    // Check for mountpoint (path)
    if (!path[0] || strcmp(path, "/") == 0) {
        av_log(h, AV_LOG_ERROR, "No mountpoint (path) specified!\n");
        ret = AVERROR(EIO);
        goto cleanup;
    }

    // Build new URI for passing to http protocol
    ff_url_join(h_url, sizeof(h_url), "http", auth, host, port, "%s", path);
    // Finally open http proto handler
    ret = ffurl_open(&s->hd, h_url, AVIO_FLAG_READ_WRITE, NULL, &opt_dict);

cleanup:
    av_freep(&user);
    av_freep(&headers);
    av_dict_free(&opt_dict);

    return ret;
}

static int icecast_write(URLContext *h, const uint8_t *buf, int size)
{
    IcecastContext *s = h->priv_data;
    if (!s->send_started) {
        s->send_started = 1;
        if (!s->content_type && size >= 8) {
            static const uint8_t oggs[4] = { 0x4F, 0x67, 0x67, 0x53 };
            static const uint8_t webm[4] = { 0x1A, 0x45, 0xDF, 0xA3 };
            static const uint8_t opus[8] = { 0x4F, 0x70, 0x75, 0x73, 0x48, 0x65, 0x61, 0x64 };
            if (memcmp(buf, oggs, sizeof(oggs)) == 0) {
                av_log(h, AV_LOG_WARNING, "Streaming Ogg but appropriate content type NOT set!\n");
                av_log(h, AV_LOG_WARNING, "Set it with -content_type application/ogg\n");
            } else if (memcmp(buf, opus, sizeof(opus)) == 0) {
                av_log(h, AV_LOG_WARNING, "Streaming Opus but appropriate content type NOT set!\n");
                av_log(h, AV_LOG_WARNING, "Set it with -content_type audio/ogg\n");
            } else if (memcmp(buf, webm, sizeof(webm)) == 0) {
                av_log(h, AV_LOG_WARNING, "Streaming WebM but appropriate content type NOT set!\n");
                av_log(h, AV_LOG_WARNING, "Set it with -content_type video/webm\n");
            } else {
                av_log(h, AV_LOG_WARNING, "It seems you are streaming an unsupported format.\n");
                av_log(h, AV_LOG_WARNING, "It might work, but is not officially supported in Icecast!\n");
            }
        }
    }
    return ffurl_write(s->hd, buf, size);
}

static const AVClass icecast_context_class = {
    .class_name     = "icecast",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

URLProtocol ff_icecast_protocol = {
    .name            = "icecast",
    .url_open        = icecast_open,
    .url_write       = icecast_write,
    .url_close       = icecast_close,
    .priv_data_size  = sizeof(IcecastContext),
    .priv_data_class = &icecast_context_class,
    .flags           = URL_PROTOCOL_FLAG_NETWORK,
};
