/*
 * Apple HTTP Live Streaming Protocol Handler
 * Copyright (c) 2010 Martin Storsjo
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
 * Apple HTTP Live Streaming Protocol Handler
 * http://tools.ietf.org/html/draft-pantos-http-live-streaming
 */

#include "libavutil/avstring.h"
#include "avformat.h"
#include "internal.h"
#include "url.h"
#include <unistd.h>

/*
 * An apple http stream consists of a playlist with media segment files,
 * played sequentially. There may be several playlists with the same
 * video content, in different bandwidth variants, that are played in
 * parallel (preferrably only one bandwidth variant at a time). In this case,
 * the user supplied the url to a main playlist that only lists the variant
 * playlists.
 *
 * If the main playlist doesn't point at any variants, we still create
 * one anonymous toplevel variant for this, to maintain the structure.
 */

struct segment {
    int duration;
    char url[MAX_URL_SIZE];
};

struct variant {
    int bandwidth;
    char url[MAX_URL_SIZE];
};

typedef struct AppleHTTPContext {
    char playlisturl[MAX_URL_SIZE];
    int target_duration;
    int start_seq_no;
    int finished;
    int n_segments;
    struct segment **segments;
    int n_variants;
    struct variant **variants;
    int cur_seq_no;
    URLContext *seg_hd;
    int64_t last_load_time;
} AppleHTTPContext;

static int read_chomp_line(AVIOContext *s, char *buf, int maxlen)
{
    int len = ff_get_line(s, buf, maxlen);
    while (len > 0 && isspace(buf[len - 1]))
        buf[--len] = '\0';
    return len;
}

static void free_segment_list(AppleHTTPContext *s)
{
    int i;
    for (i = 0; i < s->n_segments; i++)
        av_free(s->segments[i]);
    av_freep(&s->segments);
    s->n_segments = 0;
}

static void free_variant_list(AppleHTTPContext *s)
{
    int i;
    for (i = 0; i < s->n_variants; i++)
        av_free(s->variants[i]);
    av_freep(&s->variants);
    s->n_variants = 0;
}

struct variant_info {
    char bandwidth[20];
};

static void handle_variant_args(struct variant_info *info, const char *key,
                                int key_len, char **dest, int *dest_len)
{
    if (!strncmp(key, "BANDWIDTH=", key_len)) {
        *dest     =        info->bandwidth;
        *dest_len = sizeof(info->bandwidth);
    }
}

static int parse_playlist(URLContext *h, const char *url)
{
    AppleHTTPContext *s = h->priv_data;
    AVIOContext *in;
    int ret = 0, duration = 0, is_segment = 0, is_variant = 0, bandwidth = 0;
    char line[1024];
    const char *ptr;

    if ((ret = avio_open2(&in, url, AVIO_FLAG_READ,
                          &h->interrupt_callback, NULL)) < 0)
        return ret;

    read_chomp_line(in, line, sizeof(line));
    if (strcmp(line, "#EXTM3U"))
        return AVERROR_INVALIDDATA;

    free_segment_list(s);
    s->finished = 0;
    while (!url_feof(in)) {
        read_chomp_line(in, line, sizeof(line));
        if (av_strstart(line, "#EXT-X-STREAM-INF:", &ptr)) {
            struct variant_info info = {{0}};
            is_variant = 1;
            ff_parse_key_value(ptr, (ff_parse_key_val_cb) handle_variant_args,
                               &info);
            bandwidth = atoi(info.bandwidth);
        } else if (av_strstart(line, "#EXT-X-TARGETDURATION:", &ptr)) {
            s->target_duration = atoi(ptr);
        } else if (av_strstart(line, "#EXT-X-MEDIA-SEQUENCE:", &ptr)) {
            s->start_seq_no = atoi(ptr);
        } else if (av_strstart(line, "#EXT-X-ENDLIST", &ptr)) {
            s->finished = 1;
        } else if (av_strstart(line, "#EXTINF:", &ptr)) {
            is_segment = 1;
            duration = atoi(ptr);
        } else if (av_strstart(line, "#", NULL)) {
            continue;
        } else if (line[0]) {
            if (is_segment) {
                struct segment *seg = av_malloc(sizeof(struct segment));
                if (!seg) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                seg->duration = duration;
                ff_make_absolute_url(seg->url, sizeof(seg->url), url, line);
                dynarray_add(&s->segments, &s->n_segments, seg);
                is_segment = 0;
            } else if (is_variant) {
                struct variant *var = av_malloc(sizeof(struct variant));
                if (!var) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                var->bandwidth = bandwidth;
                ff_make_absolute_url(var->url, sizeof(var->url), url, line);
                dynarray_add(&s->variants, &s->n_variants, var);
                is_variant = 0;
            }
        }
    }
    s->last_load_time = av_gettime();

fail:
    avio_close(in);
    return ret;
}

static int applehttp_open(URLContext *h, const char *uri, int flags)
{
    AppleHTTPContext *s;
    int ret, i;
    const char *nested_url;

    if (flags & AVIO_FLAG_WRITE)
        return AVERROR(ENOSYS);

    s = av_mallocz(sizeof(AppleHTTPContext));
    if (!s)
        return AVERROR(ENOMEM);
    h->priv_data = s;
    h->is_streamed = 1;

    if (av_strstart(uri, "applehttp+", &nested_url)) {
        av_strlcpy(s->playlisturl, nested_url, sizeof(s->playlisturl));
    } else if (av_strstart(uri, "applehttp://", &nested_url)) {
        av_strlcpy(s->playlisturl, "http://", sizeof(s->playlisturl));
        av_strlcat(s->playlisturl, nested_url, sizeof(s->playlisturl));
    } else {
        av_log(h, AV_LOG_ERROR, "Unsupported url %s\n", uri);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if ((ret = parse_playlist(h, s->playlisturl)) < 0)
        goto fail;

    if (s->n_segments == 0 && s->n_variants > 0) {
        int max_bandwidth = 0, maxvar = -1;
        for (i = 0; i < s->n_variants; i++) {
            if (s->variants[i]->bandwidth > max_bandwidth || i == 0) {
                max_bandwidth = s->variants[i]->bandwidth;
                maxvar = i;
            }
        }
        av_strlcpy(s->playlisturl, s->variants[maxvar]->url,
                   sizeof(s->playlisturl));
        if ((ret = parse_playlist(h, s->playlisturl)) < 0)
            goto fail;
    }

    if (s->n_segments == 0) {
        av_log(h, AV_LOG_WARNING, "Empty playlist\n");
        ret = AVERROR(EIO);
        goto fail;
    }
    s->cur_seq_no = s->start_seq_no;
    if (!s->finished && s->n_segments >= 3)
        s->cur_seq_no = s->start_seq_no + s->n_segments - 3;

    return 0;

fail:
    av_free(s);
    return ret;
}

static int applehttp_read(URLContext *h, uint8_t *buf, int size)
{
    AppleHTTPContext *s = h->priv_data;
    const char *url;
    int ret;

start:
    if (s->seg_hd) {
        ret = ffurl_read(s->seg_hd, buf, size);
        if (ret > 0)
            return ret;
    }
    if (s->seg_hd) {
        ffurl_close(s->seg_hd);
        s->seg_hd = NULL;
        s->cur_seq_no++;
    }
retry:
    if (!s->finished) {
        int64_t now = av_gettime();
        if (now - s->last_load_time >= s->target_duration*1000000)
            if ((ret = parse_playlist(h, s->playlisturl)) < 0)
                return ret;
    }
    if (s->cur_seq_no < s->start_seq_no) {
        av_log(h, AV_LOG_WARNING,
               "skipping %d segments ahead, expired from playlist\n",
               s->start_seq_no - s->cur_seq_no);
        s->cur_seq_no = s->start_seq_no;
    }
    if (s->cur_seq_no - s->start_seq_no >= s->n_segments) {
        if (s->finished)
            return AVERROR_EOF;
        while (av_gettime() - s->last_load_time < s->target_duration*1000000) {
            if (ff_check_interrupt(&h->interrupt_callback))
                return AVERROR_EXIT;
            usleep(100*1000);
        }
        goto retry;
    }
    url = s->segments[s->cur_seq_no - s->start_seq_no]->url,
    av_log(h, AV_LOG_DEBUG, "opening %s\n", url);
    ret = ffurl_open(&s->seg_hd, url, AVIO_FLAG_READ,
                     &h->interrupt_callback, NULL);
    if (ret < 0) {
        if (ff_check_interrupt(&h->interrupt_callback))
            return AVERROR_EXIT;
        av_log(h, AV_LOG_WARNING, "Unable to open %s\n", url);
        s->cur_seq_no++;
        goto retry;
    }
    goto start;
}

static int applehttp_close(URLContext *h)
{
    AppleHTTPContext *s = h->priv_data;

    free_segment_list(s);
    free_variant_list(s);
    ffurl_close(s->seg_hd);
    av_free(s);
    return 0;
}

URLProtocol ff_applehttp_protocol = {
    .name      = "applehttp",
    .url_open  = applehttp_open,
    .url_read  = applehttp_read,
    .url_close = applehttp_close,
    .flags     = URL_PROTOCOL_FLAG_NESTED_SCHEME,
};
