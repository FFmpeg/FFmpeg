/*
 * Apple HTTP Live Streaming demuxer
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
 * Apple HTTP Live Streaming demuxer
 * http://tools.ietf.org/html/draft-pantos-http-live-streaming
 */

#define _XOPEN_SOURCE 600
#include "libavutil/avstring.h"
#include "avformat.h"
#include "internal.h"
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

/*
 * Each variant has its own demuxer. If it currently is active,
 * it has an open ByteIOContext too, and potentially an AVPacket
 * containing the next packet from this stream.
 */
struct variant {
    int bandwidth;
    char url[MAX_URL_SIZE];
    ByteIOContext *pb;
    AVFormatContext *ctx;
    AVPacket pkt;
    int stream_offset;

    int start_seq_no;
    int n_segments;
    struct segment **segments;
    int needed;
};

typedef struct AppleHTTPContext {
    int target_duration;
    int finished;
    int n_variants;
    struct variant **variants;
    int cur_seq_no;
    int64_t last_load_time;
    int64_t last_packet_dts;
    int max_start_seq, min_end_seq;
} AppleHTTPContext;

static int read_chomp_line(ByteIOContext *s, char *buf, int maxlen)
{
    int len = ff_get_line(s, buf, maxlen);
    while (len > 0 && isspace(buf[len - 1]))
        buf[--len] = '\0';
    return len;
}

static void make_absolute_url(char *buf, int size, const char *base,
                              const char *rel)
{
    char *sep;
    /* If rel actually is an absolute url, just copy it */
    if (!base || strstr(rel, "://") || rel[0] == '/') {
        av_strlcpy(buf, rel, size);
        return;
    }
    if (base != buf)
        av_strlcpy(buf, base, size);
    /* Remove the file name from the base url */
    sep = strrchr(buf, '/');
    if (sep)
        sep[1] = '\0';
    else
        buf[0] = '\0';
    while (av_strstart(rel, "../", NULL) && sep) {
        /* Remove the path delimiter at the end */
        sep[0] = '\0';
        sep = strrchr(buf, '/');
        /* If the next directory name to pop off is "..", break here */
        if (!strcmp(sep ? &sep[1] : buf, "..")) {
            /* Readd the slash we just removed */
            av_strlcat(buf, "/", size);
            break;
        }
        /* Cut off the directory name */
        if (sep)
            sep[1] = '\0';
        else
            buf[0] = '\0';
        rel += 3;
    }
    av_strlcat(buf, rel, size);
}

static void free_segment_list(struct variant *var)
{
    int i;
    for (i = 0; i < var->n_segments; i++)
        av_free(var->segments[i]);
    av_freep(&var->segments);
    var->n_segments = 0;
}

static void free_variant_list(AppleHTTPContext *c)
{
    int i;
    for (i = 0; i < c->n_variants; i++) {
        struct variant *var = c->variants[i];
        free_segment_list(var);
        av_free_packet(&var->pkt);
        if (var->pb)
            url_fclose(var->pb);
        if (var->ctx) {
            var->ctx->pb = NULL;
            av_close_input_file(var->ctx);
        }
        av_free(var);
    }
    av_freep(&c->variants);
    c->n_variants = 0;
}

/*
 * Used to reset a statically allocated AVPacket to a clean slate,
 * containing no data.
 */
static void reset_packet(AVPacket *pkt)
{
    av_init_packet(pkt);
    pkt->data = NULL;
}

static struct variant *new_variant(AppleHTTPContext *c, int bandwidth,
                                   const char *url, const char *base)
{
    struct variant *var = av_mallocz(sizeof(struct variant));
    if (!var)
        return NULL;
    reset_packet(&var->pkt);
    var->bandwidth = bandwidth;
    make_absolute_url(var->url, sizeof(var->url), base, url);
    dynarray_add(&c->variants, &c->n_variants, var);
    return var;
}

struct variant_info {
    char bandwidth[20];
};

static void handle_variant_args(struct variant_info *info, const char *key,
                                int key_len, char **dest, int *dest_len)
{
    if (strncmp(key, "BANDWIDTH", key_len)) {
        *dest     =        info->bandwidth;
        *dest_len = sizeof(info->bandwidth);
    }
}

static int parse_playlist(AppleHTTPContext *c, const char *url,
                          struct variant *var, ByteIOContext *in)
{
    int ret = 0, duration = 0, is_segment = 0, is_variant = 0, bandwidth = 0;
    char line[1024];
    const char *ptr;
    int close_in = 0;

    if (!in) {
        close_in = 1;
        if ((ret = url_fopen(&in, url, URL_RDONLY)) < 0)
            return ret;
    }

    read_chomp_line(in, line, sizeof(line));
    if (strcmp(line, "#EXTM3U")) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (var)
        free_segment_list(var);
    c->finished = 0;
    while (!url_feof(in)) {
        read_chomp_line(in, line, sizeof(line));
        if (av_strstart(line, "#EXT-X-STREAM-INF:", &ptr)) {
            struct variant_info info = {{0}};
            is_variant = 1;
            ff_parse_key_value(ptr, (ff_parse_key_val_cb) handle_variant_args,
                               &info);
            bandwidth = atoi(info.bandwidth);
        } else if (av_strstart(line, "#EXT-X-TARGETDURATION:", &ptr)) {
            c->target_duration = atoi(ptr);
        } else if (av_strstart(line, "#EXT-X-MEDIA-SEQUENCE:", &ptr)) {
            if (!var) {
                var = new_variant(c, 0, url, NULL);
                if (!var) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
            }
            var->start_seq_no = atoi(ptr);
        } else if (av_strstart(line, "#EXT-X-ENDLIST", &ptr)) {
            c->finished = 1;
        } else if (av_strstart(line, "#EXTINF:", &ptr)) {
            is_segment = 1;
            duration   = atoi(ptr);
        } else if (av_strstart(line, "#", NULL)) {
            continue;
        } else if (line[0]) {
            if (is_variant) {
                if (!new_variant(c, bandwidth, line, url)) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                is_variant = 0;
                bandwidth  = 0;
            }
            if (is_segment) {
                struct segment *seg;
                if (!var) {
                    var = new_variant(c, 0, url, NULL);
                    if (!var) {
                        ret = AVERROR(ENOMEM);
                        goto fail;
                    }
                }
                seg = av_malloc(sizeof(struct segment));
                if (!seg) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                seg->duration = duration;
                make_absolute_url(seg->url, sizeof(seg->url), url, line);
                dynarray_add(&var->segments, &var->n_segments, seg);
                is_segment = 0;
            }
        }
    }
    c->last_load_time = av_gettime();

fail:
    if (close_in)
        url_fclose(in);
    return ret;
}

static int applehttp_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AppleHTTPContext *c = s->priv_data;
    int ret = 0, i, j, stream_offset = 0;

    if ((ret = parse_playlist(c, s->filename, NULL, s->pb)) < 0)
        goto fail;

    if (c->n_variants == 0) {
        av_log(NULL, AV_LOG_WARNING, "Empty playlist\n");
        ret = AVERROR_EOF;
        goto fail;
    }
    /* If the playlist only contained variants, parse each individual
     * variant playlist. */
    if (c->n_variants > 1 || c->variants[0]->n_segments == 0) {
        for (i = 0; i < c->n_variants; i++) {
            struct variant *v = c->variants[i];
            if ((ret = parse_playlist(c, v->url, v, NULL)) < 0)
                goto fail;
        }
    }

    if (c->variants[0]->n_segments == 0) {
        av_log(NULL, AV_LOG_WARNING, "Empty playlist\n");
        ret = AVERROR_EOF;
        goto fail;
    }

    /* If this isn't a live stream, calculate the total duration of the
     * stream. */
    if (c->finished) {
        int duration = 0;
        for (i = 0; i < c->variants[0]->n_segments; i++)
            duration += c->variants[0]->segments[i]->duration;
        s->duration = duration * AV_TIME_BASE;
    }

    c->min_end_seq = INT_MAX;
    /* Open the demuxer for each variant */
    for (i = 0; i < c->n_variants; i++) {
        struct variant *v = c->variants[i];
        if (v->n_segments == 0)
            continue;
        c->max_start_seq = FFMAX(c->max_start_seq, v->start_seq_no);
        c->min_end_seq   = FFMIN(c->min_end_seq,   v->start_seq_no +
                                                   v->n_segments);
        ret = av_open_input_file(&v->ctx, v->segments[0]->url, NULL, 0, NULL);
        if (ret < 0)
            goto fail;
        url_fclose(v->ctx->pb);
        v->ctx->pb = NULL;
        v->stream_offset = stream_offset;
        /* Create new AVStreams for each stream in this variant */
        for (j = 0; j < v->ctx->nb_streams; j++) {
            AVStream *st = av_new_stream(s, i);
            if (!st) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            avcodec_copy_context(st->codec, v->ctx->streams[j]->codec);
        }
        stream_offset += v->ctx->nb_streams;
    }
    c->last_packet_dts = AV_NOPTS_VALUE;

    c->cur_seq_no = c->max_start_seq;
    /* If this is a live stream with more than 3 segments, start at the
     * third last segment. */
    if (!c->finished && c->min_end_seq - c->max_start_seq > 3)
        c->cur_seq_no = c->min_end_seq - 2;

    return 0;
fail:
    free_variant_list(c);
    return ret;
}

static int open_variant(AppleHTTPContext *c, struct variant *var, int skip)
{
    int ret;

    if (c->cur_seq_no < var->start_seq_no) {
        av_log(NULL, AV_LOG_WARNING,
               "seq %d not available in variant %s, skipping\n",
               var->start_seq_no, var->url);
        return 0;
    }
    if (c->cur_seq_no - var->start_seq_no >= var->n_segments)
        return c->finished ? AVERROR_EOF : 0;
    ret = url_fopen(&var->pb,
                    var->segments[c->cur_seq_no - var->start_seq_no]->url,
                    URL_RDONLY);
    if (ret < 0)
        return ret;
    var->ctx->pb = var->pb;
    /* If this is a new segment in parallel with another one already opened,
     * skip ahead so they're all at the same dts. */
    if (skip && c->last_packet_dts != AV_NOPTS_VALUE) {
        while (1) {
            ret = av_read_frame(var->ctx, &var->pkt);
            if (ret < 0) {
                if (ret == AVERROR_EOF) {
                    reset_packet(&var->pkt);
                    return 0;
                }
                return ret;
            }
            if (var->pkt.dts >= c->last_packet_dts)
                break;
            av_free_packet(&var->pkt);
        }
    }
    return 0;
}

static int applehttp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AppleHTTPContext *c = s->priv_data;
    int ret, i, minvariant = -1, first = 1, needed = 0, changed = 0,
        variants = 0;

    /* Recheck the discard flags - which streams are desired at the moment */
    for (i = 0; i < c->n_variants; i++)
        c->variants[i]->needed = 0;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        struct variant *var = c->variants[s->streams[i]->id];
        if (st->discard < AVDISCARD_ALL) {
            var->needed = 1;
            needed++;
        }
        /* Copy the discard flag to the chained demuxer, to indicate which
         * streams are desired. */
        var->ctx->streams[i - var->stream_offset]->discard = st->discard;
    }
    if (!needed)
        return AVERROR_EOF;
start:
    for (i = 0; i < c->n_variants; i++) {
        struct variant *var = c->variants[i];
        /* Close unneeded streams, open newly requested streams */
        if (var->pb && !var->needed) {
            av_log(s, AV_LOG_DEBUG,
                   "Closing variant stream %d, no longer needed\n", i);
            av_free_packet(&var->pkt);
            reset_packet(&var->pkt);
            url_fclose(var->pb);
            var->pb = NULL;
            changed = 1;
        } else if (!var->pb && var->needed) {
            if (first)
                av_log(s, AV_LOG_DEBUG, "Opening variant stream %d\n", i);
            if (first && !c->finished)
                if ((ret = parse_playlist(c, var->url, var, NULL)) < 0)
                    return ret;
            ret = open_variant(c, var, first);
            if (ret < 0)
                return ret;
            changed = 1;
        }
        /* Count the number of open variants */
        if (var->pb)
            variants++;
        /* Make sure we've got one buffered packet from each open variant
         * stream */
        if (var->pb && !var->pkt.data) {
            ret = av_read_frame(var->ctx, &var->pkt);
            if (ret < 0) {
                if (!url_feof(var->pb))
                    return ret;
                reset_packet(&var->pkt);
            }
        }
        /* Check if this stream has the packet with the lowest dts */
        if (var->pkt.data) {
            if (minvariant < 0 ||
                var->pkt.dts < c->variants[minvariant]->pkt.dts)
                minvariant = i;
        }
    }
    if (first && changed)
        av_log(s, AV_LOG_INFO, "Receiving %d variant streams\n", variants);
    /* If we got a packet, return it */
    if (minvariant >= 0) {
        *pkt = c->variants[minvariant]->pkt;
        pkt->stream_index += c->variants[minvariant]->stream_offset;
        reset_packet(&c->variants[minvariant]->pkt);
        c->last_packet_dts = pkt->dts;
        return 0;
    }
    /* No more packets - eof reached in all variant streams, close the
     * current segments. */
    for (i = 0; i < c->n_variants; i++) {
        struct variant *var = c->variants[i];
        if (var->pb) {
            url_fclose(var->pb);
            var->pb = NULL;
        }
    }
    /* Indicate that we're opening the next segment, not opening a new
     * variant stream in parallel, so we shouldn't try to skip ahead. */
    first = 0;
    c->cur_seq_no++;
reload:
    if (!c->finished) {
        /* If this is a live stream and target_duration has elapsed since
         * the last playlist reload, reload the variant playlists now. */
        int64_t now = av_gettime();
        if (now - c->last_load_time >= c->target_duration*1000000) {
            c->max_start_seq = 0;
            c->min_end_seq   = INT_MAX;
            for (i = 0; i < c->n_variants; i++) {
                struct variant *var = c->variants[i];
                if (var->needed) {
                    if ((ret = parse_playlist(c, var->url, var, NULL)) < 0)
                        return ret;
                    c->max_start_seq = FFMAX(c->max_start_seq,
                                             var->start_seq_no);
                    c->min_end_seq   = FFMIN(c->min_end_seq,
                                             var->start_seq_no + var->n_segments);
                }
            }
        }
    }
    if (c->cur_seq_no < c->max_start_seq) {
        av_log(NULL, AV_LOG_WARNING,
               "skipping %d segments ahead, expired from playlists\n",
               c->max_start_seq - c->cur_seq_no);
        c->cur_seq_no = c->max_start_seq;
    }
    /* If more segments exit, open the next one */
    if (c->cur_seq_no < c->min_end_seq)
        goto start;
    /* We've reached the end of the playlists - return eof if this is a
     * non-live stream, wait until the next playlist reload if it is live. */
    if (c->finished)
        return AVERROR_EOF;
    while (av_gettime() - c->last_load_time < c->target_duration*1000000) {
        if (url_interrupt_cb())
            return AVERROR(EINTR);
        usleep(100*1000);
    }
    /* Enough time has elapsed since the last reload */
    goto reload;
}

static int applehttp_close(AVFormatContext *s)
{
    AppleHTTPContext *c = s->priv_data;

    free_variant_list(c);
    return 0;
}

static int applehttp_read_seek(AVFormatContext *s, int stream_index,
                               int64_t timestamp, int flags)
{
    AppleHTTPContext *c = s->priv_data;
    int pos = 0, i;
    struct variant *var = c->variants[0];

    if ((flags & AVSEEK_FLAG_BYTE) || !c->finished)
        return AVERROR(ENOSYS);

    /* Reset the variants */
    c->last_packet_dts = AV_NOPTS_VALUE;
    for (i = 0; i < c->n_variants; i++) {
        struct variant *var = c->variants[i];
        if (var->pb) {
            url_fclose(var->pb);
            var->pb = NULL;
        }
        av_free_packet(&var->pkt);
        reset_packet(&var->pkt);
    }

    timestamp = av_rescale_rnd(timestamp, 1, stream_index >= 0 ?
                               s->streams[stream_index]->time_base.den :
                               AV_TIME_BASE, flags & AVSEEK_FLAG_BACKWARD ?
                               AV_ROUND_DOWN : AV_ROUND_UP);
    /* Locate the segment that contains the target timestamp */
    for (i = 0; i < var->n_segments; i++) {
        if (timestamp >= pos && timestamp < pos + var->segments[i]->duration) {
            c->cur_seq_no = var->start_seq_no + i;
            return 0;
        }
        pos += var->segments[i]->duration;
    }
    return AVERROR(EIO);
}

static int applehttp_probe(AVProbeData *p)
{
    /* Require #EXTM3U at the start, and either one of the ones below
     * somewhere for a proper match. */
    if (strncmp(p->buf, "#EXTM3U", 7))
        return 0;
    if (strstr(p->buf, "#EXT-X-STREAM-INF:")     ||
        strstr(p->buf, "#EXT-X-TARGETDURATION:") ||
        strstr(p->buf, "#EXT-X-MEDIA-SEQUENCE:"))
        return AVPROBE_SCORE_MAX;
    return 0;
}

AVInputFormat applehttp_demuxer = {
    "applehttp",
    NULL_IF_CONFIG_SMALL("Apple HTTP Live Streaming format"),
    sizeof(AppleHTTPContext),
    applehttp_probe,
    applehttp_read_header,
    applehttp_read_packet,
    applehttp_close,
    applehttp_read_seek,
};
