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

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/dict.h"
#include "libavutil/time.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "url.h"

#define INITIAL_BUFFER_SIZE 32768

/*
 * An apple http stream consists of a playlist with media segment files,
 * played sequentially. There may be several playlists with the same
 * video content, in different bandwidth variants, that are played in
 * parallel (preferably only one bandwidth variant at a time). In this case,
 * the user supplied the url to a main playlist that only lists the variant
 * playlists.
 *
 * If the main playlist doesn't point at any variants, we still create
 * one anonymous toplevel variant for this, to maintain the structure.
 */

enum KeyType {
    KEY_NONE,
    KEY_AES_128,
};

struct segment {
    int64_t duration;
    char url[MAX_URL_SIZE];
    char key[MAX_URL_SIZE];
    enum KeyType key_type;
    uint8_t iv[16];
};

/*
 * Each variant has its own demuxer. If it currently is active,
 * it has an open AVIOContext too, and potentially an AVPacket
 * containing the next packet from this stream.
 */
struct variant {
    int bandwidth;
    char url[MAX_URL_SIZE];
    AVIOContext pb;
    uint8_t* read_buffer;
    URLContext *input;
    AVFormatContext *parent;
    int index;
    AVFormatContext *ctx;
    AVPacket pkt;
    int stream_offset;

    int finished;
    int64_t target_duration;
    int start_seq_no;
    int n_segments;
    struct segment **segments;
    int needed, cur_needed;
    int cur_seq_no;
    int64_t last_load_time;

    char key_url[MAX_URL_SIZE];
    uint8_t key[16];
};

typedef struct HLSContext {
    int n_variants;
    struct variant **variants;
    int cur_seq_no;
    int end_of_segment;
    int first_packet;
    int64_t first_timestamp;
    int64_t seek_timestamp;
    int seek_flags;
    AVIOInterruptCB *interrupt_callback;
    char *user_agent;                    ///< holds HTTP user agent set as an AVOption to the HTTP protocol context
    char *cookies;                       ///< holds HTTP cookie values set in either the initial response or as an AVOption to the HTTP protocol context
} HLSContext;

static int read_chomp_line(AVIOContext *s, char *buf, int maxlen)
{
    int len = ff_get_line(s, buf, maxlen);
    while (len > 0 && av_isspace(buf[len - 1]))
        buf[--len] = '\0';
    return len;
}

static void free_segment_list(struct variant *var)
{
    int i;
    for (i = 0; i < var->n_segments; i++)
        av_free(var->segments[i]);
    av_freep(&var->segments);
    var->n_segments = 0;
}

static void free_variant_list(HLSContext *c)
{
    int i;
    for (i = 0; i < c->n_variants; i++) {
        struct variant *var = c->variants[i];
        free_segment_list(var);
        av_free_packet(&var->pkt);
        av_free(var->pb.buffer);
        if (var->input)
            ffurl_close(var->input);
        if (var->ctx) {
            var->ctx->pb = NULL;
            avformat_close_input(&var->ctx);
        }
        av_free(var);
    }
    av_freep(&c->variants);
    av_freep(&c->cookies);
    av_freep(&c->user_agent);
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

static struct variant *new_variant(HLSContext *c, int bandwidth,
                                   const char *url, const char *base)
{
    struct variant *var = av_mallocz(sizeof(struct variant));
    if (!var)
        return NULL;
    reset_packet(&var->pkt);
    var->bandwidth = bandwidth;
    ff_make_absolute_url(var->url, sizeof(var->url), base, url);
    dynarray_add(&c->variants, &c->n_variants, var);
    return var;
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

struct key_info {
     char uri[MAX_URL_SIZE];
     char method[10];
     char iv[35];
};

static void handle_key_args(struct key_info *info, const char *key,
                            int key_len, char **dest, int *dest_len)
{
    if (!strncmp(key, "METHOD=", key_len)) {
        *dest     =        info->method;
        *dest_len = sizeof(info->method);
    } else if (!strncmp(key, "URI=", key_len)) {
        *dest     =        info->uri;
        *dest_len = sizeof(info->uri);
    } else if (!strncmp(key, "IV=", key_len)) {
        *dest     =        info->iv;
        *dest_len = sizeof(info->iv);
    }
}

static int parse_playlist(HLSContext *c, const char *url,
                          struct variant *var, AVIOContext *in)
{
    int ret = 0, is_segment = 0, is_variant = 0, bandwidth = 0;
    int64_t duration = 0;
    enum KeyType key_type = KEY_NONE;
    uint8_t iv[16] = "";
    int has_iv = 0;
    char key[MAX_URL_SIZE] = "";
    char line[MAX_URL_SIZE];
    const char *ptr;
    int close_in = 0;

    if (!in) {
        AVDictionary *opts = NULL;
        close_in = 1;
        /* Some HLS servers don't like being sent the range header */
        av_dict_set(&opts, "seekable", "0", 0);

        // broker prior HTTP options that should be consistent across requests
        av_dict_set(&opts, "user-agent", c->user_agent, 0);
        av_dict_set(&opts, "cookies", c->cookies, 0);

        ret = avio_open2(&in, url, AVIO_FLAG_READ,
                         c->interrupt_callback, &opts);
        av_dict_free(&opts);
        if (ret < 0)
            return ret;
    }

    read_chomp_line(in, line, sizeof(line));
    if (strcmp(line, "#EXTM3U")) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (var) {
        free_segment_list(var);
        var->finished = 0;
    }
    while (!url_feof(in)) {
        read_chomp_line(in, line, sizeof(line));
        if (av_strstart(line, "#EXT-X-STREAM-INF:", &ptr)) {
            struct variant_info info = {{0}};
            is_variant = 1;
            ff_parse_key_value(ptr, (ff_parse_key_val_cb) handle_variant_args,
                               &info);
            bandwidth = atoi(info.bandwidth);
        } else if (av_strstart(line, "#EXT-X-KEY:", &ptr)) {
            struct key_info info = {{0}};
            ff_parse_key_value(ptr, (ff_parse_key_val_cb) handle_key_args,
                               &info);
            key_type = KEY_NONE;
            has_iv = 0;
            if (!strcmp(info.method, "AES-128"))
                key_type = KEY_AES_128;
            if (!strncmp(info.iv, "0x", 2) || !strncmp(info.iv, "0X", 2)) {
                ff_hex_to_data(iv, info.iv + 2);
                has_iv = 1;
            }
            av_strlcpy(key, info.uri, sizeof(key));
        } else if (av_strstart(line, "#EXT-X-TARGETDURATION:", &ptr)) {
            if (!var) {
                var = new_variant(c, 0, url, NULL);
                if (!var) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
            }
            var->target_duration = atoi(ptr) * AV_TIME_BASE;
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
            if (var)
                var->finished = 1;
        } else if (av_strstart(line, "#EXTINF:", &ptr)) {
            is_segment = 1;
            duration   = atof(ptr) * AV_TIME_BASE;
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
                seg->key_type = key_type;
                if (has_iv) {
                    memcpy(seg->iv, iv, sizeof(iv));
                } else {
                    int seq = var->start_seq_no + var->n_segments;
                    memset(seg->iv, 0, sizeof(seg->iv));
                    AV_WB32(seg->iv + 12, seq);
                }
                ff_make_absolute_url(seg->key, sizeof(seg->key), url, key);
                ff_make_absolute_url(seg->url, sizeof(seg->url), url, line);
                dynarray_add(&var->segments, &var->n_segments, seg);
                is_segment = 0;
            }
        }
    }
    if (var)
        var->last_load_time = av_gettime();

fail:
    if (close_in)
        avio_close(in);
    return ret;
}

static int open_input(HLSContext *c, struct variant *var)
{
    AVDictionary *opts = NULL;
    int ret;
    struct segment *seg = var->segments[var->cur_seq_no - var->start_seq_no];

    // broker prior HTTP options that should be consistent across requests
    av_dict_set(&opts, "user-agent", c->user_agent, 0);
    av_dict_set(&opts, "cookies", c->cookies, 0);
    av_dict_set(&opts, "seekable", "0", 0);

    if (seg->key_type == KEY_NONE) {
        ret = ffurl_open(&var->input, seg->url, AVIO_FLAG_READ,
                          &var->parent->interrupt_callback, &opts);
        goto cleanup;
    } else if (seg->key_type == KEY_AES_128) {
        char iv[33], key[33], url[MAX_URL_SIZE];
        if (strcmp(seg->key, var->key_url)) {
            URLContext *uc;
            if (ffurl_open(&uc, seg->key, AVIO_FLAG_READ,
                           &var->parent->interrupt_callback, &opts) == 0) {
                if (ffurl_read_complete(uc, var->key, sizeof(var->key))
                    != sizeof(var->key)) {
                    av_log(NULL, AV_LOG_ERROR, "Unable to read key file %s\n",
                           seg->key);
                }
                ffurl_close(uc);
            } else {
                av_log(NULL, AV_LOG_ERROR, "Unable to open key file %s\n",
                       seg->key);
            }
            av_strlcpy(var->key_url, seg->key, sizeof(var->key_url));
        }
        ff_data_to_hex(iv, seg->iv, sizeof(seg->iv), 0);
        ff_data_to_hex(key, var->key, sizeof(var->key), 0);
        iv[32] = key[32] = '\0';
        if (strstr(seg->url, "://"))
            snprintf(url, sizeof(url), "crypto+%s", seg->url);
        else
            snprintf(url, sizeof(url), "crypto:%s", seg->url);
        if ((ret = ffurl_alloc(&var->input, url, AVIO_FLAG_READ,
                               &var->parent->interrupt_callback)) < 0)
            goto cleanup;
        av_opt_set(var->input->priv_data, "key", key, 0);
        av_opt_set(var->input->priv_data, "iv", iv, 0);
        /* Need to repopulate options */
        av_dict_free(&opts);
        av_dict_set(&opts, "seekable", "0", 0);
        if ((ret = ffurl_connect(var->input, &opts)) < 0) {
            ffurl_close(var->input);
            var->input = NULL;
            goto cleanup;
        }
        ret = 0;
    }
    else
      ret = AVERROR(ENOSYS);

cleanup:
    av_dict_free(&opts);
    return ret;
}

static int read_data(void *opaque, uint8_t *buf, int buf_size)
{
    struct variant *v = opaque;
    HLSContext *c = v->parent->priv_data;
    int ret, i;

restart:
    if (!v->input) {
        /* If this is a live stream and the reload interval has elapsed since
         * the last playlist reload, reload the variant playlists now. */
        int64_t reload_interval = v->n_segments > 0 ?
                                  v->segments[v->n_segments - 1]->duration :
                                  v->target_duration;

reload:
        if (!v->finished &&
            av_gettime() - v->last_load_time >= reload_interval) {
            if ((ret = parse_playlist(c, v->url, v, NULL)) < 0)
                return ret;
            /* If we need to reload the playlist again below (if
             * there's still no more segments), switch to a reload
             * interval of half the target duration. */
            reload_interval = v->target_duration / 2;
        }
        if (v->cur_seq_no < v->start_seq_no) {
            av_log(NULL, AV_LOG_WARNING,
                   "skipping %d segments ahead, expired from playlists\n",
                   v->start_seq_no - v->cur_seq_no);
            v->cur_seq_no = v->start_seq_no;
        }
        if (v->cur_seq_no >= v->start_seq_no + v->n_segments) {
            if (v->finished)
                return AVERROR_EOF;
            while (av_gettime() - v->last_load_time < reload_interval) {
                if (ff_check_interrupt(c->interrupt_callback))
                    return AVERROR_EXIT;
                av_usleep(100*1000);
            }
            /* Enough time has elapsed since the last reload */
            goto reload;
        }

        ret = open_input(c, v);
        if (ret < 0)
            return ret;
    }
    ret = ffurl_read(v->input, buf, buf_size);
    if (ret > 0)
        return ret;
    ffurl_close(v->input);
    v->input = NULL;
    v->cur_seq_no++;

    c->end_of_segment = 1;
    c->cur_seq_no = v->cur_seq_no;

    if (v->ctx && v->ctx->nb_streams &&
        v->parent->nb_streams >= v->stream_offset + v->ctx->nb_streams) {
        v->needed = 0;
        for (i = v->stream_offset; i < v->stream_offset + v->ctx->nb_streams;
             i++) {
            if (v->parent->streams[i]->discard < AVDISCARD_ALL)
                v->needed = 1;
        }
    }
    if (!v->needed) {
        av_log(v->parent, AV_LOG_INFO, "No longer receiving variant %d\n",
               v->index);
        return AVERROR_EOF;
    }
    goto restart;
}

static int hls_read_header(AVFormatContext *s)
{
    URLContext *u = (s->flags & AVFMT_FLAG_CUSTOM_IO) ? NULL : s->pb->opaque;
    HLSContext *c = s->priv_data;
    int ret = 0, i, j, stream_offset = 0;

    c->interrupt_callback = &s->interrupt_callback;

    // if the URL context is good, read important options we must broker later
    if (u && u->prot->priv_data_class) {
        // get the previous user agent & set back to null if string size is zero
        av_freep(&c->user_agent);
        av_opt_get(u->priv_data, "user-agent", 0, (uint8_t**)&(c->user_agent));
        if (c->user_agent && !strlen(c->user_agent))
            av_freep(&c->user_agent);

        // get the previous cookies & set back to null if string size is zero
        av_freep(&c->cookies);
        av_opt_get(u->priv_data, "cookies", 0, (uint8_t**)&(c->cookies));
        if (c->cookies && !strlen(c->cookies))
            av_freep(&c->cookies);
    }

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
    if (c->variants[0]->finished) {
        int64_t duration = 0;
        for (i = 0; i < c->variants[0]->n_segments; i++)
            duration += c->variants[0]->segments[i]->duration;
        s->duration = duration;
    }

    /* Open the demuxer for each variant */
    for (i = 0; i < c->n_variants; i++) {
        struct variant *v = c->variants[i];
        AVInputFormat *in_fmt = NULL;
        char bitrate_str[20];
        AVProgram *program;

        if (v->n_segments == 0)
            continue;

        if (!(v->ctx = avformat_alloc_context())) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        v->index  = i;
        v->needed = 1;
        v->parent = s;

        /* If this is a live stream with more than 3 segments, start at the
         * third last segment. */
        v->cur_seq_no = v->start_seq_no;
        if (!v->finished && v->n_segments > 3)
            v->cur_seq_no = v->start_seq_no + v->n_segments - 3;

        v->read_buffer = av_malloc(INITIAL_BUFFER_SIZE);
        ffio_init_context(&v->pb, v->read_buffer, INITIAL_BUFFER_SIZE, 0, v,
                          read_data, NULL, NULL);
        v->pb.seekable = 0;
        ret = av_probe_input_buffer(&v->pb, &in_fmt, v->segments[0]->url,
                                    NULL, 0, 0);
        if (ret < 0) {
            /* Free the ctx - it isn't initialized properly at this point,
             * so avformat_close_input shouldn't be called. If
             * avformat_open_input fails below, it frees and zeros the
             * context, so it doesn't need any special treatment like this. */
            av_log(s, AV_LOG_ERROR, "Error when loading first segment '%s'\n", v->segments[0]->url);
            avformat_free_context(v->ctx);
            v->ctx = NULL;
            goto fail;
        }
        v->ctx->pb       = &v->pb;
        v->stream_offset = stream_offset;
        ret = avformat_open_input(&v->ctx, v->segments[0]->url, in_fmt, NULL);
        if (ret < 0)
            goto fail;

        v->ctx->ctx_flags &= ~AVFMTCTX_NOHEADER;
        ret = avformat_find_stream_info(v->ctx, NULL);
        if (ret < 0)
            goto fail;
        snprintf(bitrate_str, sizeof(bitrate_str), "%d", v->bandwidth);

        program = av_new_program(s, i);
        if (!program)
            goto fail;
        av_dict_set(&program->metadata, "variant_bitrate", bitrate_str, 0);

        /* Create new AVStreams for each stream in this variant */
        for (j = 0; j < v->ctx->nb_streams; j++) {
            AVStream *st = avformat_new_stream(s, NULL);
            AVStream *ist = v->ctx->streams[j];
            if (!st) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            ff_program_add_stream_index(s, i, stream_offset + j);
            st->id = i;
            avpriv_set_pts_info(st, ist->pts_wrap_bits, ist->time_base.num, ist->time_base.den);
            avcodec_copy_context(st->codec, v->ctx->streams[j]->codec);
            if (v->bandwidth)
                av_dict_set(&st->metadata, "variant_bitrate", bitrate_str,
                                 0);
        }
        stream_offset += v->ctx->nb_streams;
    }

    c->first_packet = 1;
    c->first_timestamp = AV_NOPTS_VALUE;
    c->seek_timestamp  = AV_NOPTS_VALUE;

    return 0;
fail:
    free_variant_list(c);
    return ret;
}

static int recheck_discard_flags(AVFormatContext *s, int first)
{
    HLSContext *c = s->priv_data;
    int i, changed = 0;

    /* Check if any new streams are needed */
    for (i = 0; i < c->n_variants; i++)
        c->variants[i]->cur_needed = 0;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        struct variant *var = c->variants[s->streams[i]->id];
        if (st->discard < AVDISCARD_ALL)
            var->cur_needed = 1;
    }
    for (i = 0; i < c->n_variants; i++) {
        struct variant *v = c->variants[i];
        if (v->cur_needed && !v->needed) {
            v->needed = 1;
            changed = 1;
            v->cur_seq_no = c->cur_seq_no;
            v->pb.eof_reached = 0;
            av_log(s, AV_LOG_INFO, "Now receiving variant %d\n", i);
        } else if (first && !v->cur_needed && v->needed) {
            if (v->input)
                ffurl_close(v->input);
            v->input = NULL;
            v->needed = 0;
            changed = 1;
            av_log(s, AV_LOG_INFO, "No longer receiving variant %d\n", i);
        }
    }
    return changed;
}

static int hls_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    HLSContext *c = s->priv_data;
    int ret, i, minvariant = -1;

    if (c->first_packet) {
        recheck_discard_flags(s, 1);
        c->first_packet = 0;
    }

start:
    c->end_of_segment = 0;
    for (i = 0; i < c->n_variants; i++) {
        struct variant *var = c->variants[i];
        /* Make sure we've got one buffered packet from each open variant
         * stream */
        if (var->needed && !var->pkt.data) {
            while (1) {
                int64_t ts_diff;
                AVStream *st;
                ret = av_read_frame(var->ctx, &var->pkt);
                if (ret < 0) {
                    if (!url_feof(&var->pb) && ret != AVERROR_EOF)
                        return ret;
                    reset_packet(&var->pkt);
                    break;
                } else {
                    if (c->first_timestamp == AV_NOPTS_VALUE &&
                        var->pkt.dts       != AV_NOPTS_VALUE)
                        c->first_timestamp = av_rescale_q(var->pkt.dts,
                            var->ctx->streams[var->pkt.stream_index]->time_base,
                            AV_TIME_BASE_Q);
                }

                if (c->seek_timestamp == AV_NOPTS_VALUE)
                    break;

                if (var->pkt.dts == AV_NOPTS_VALUE) {
                    c->seek_timestamp = AV_NOPTS_VALUE;
                    break;
                }

                st = var->ctx->streams[var->pkt.stream_index];
                ts_diff = av_rescale_rnd(var->pkt.dts, AV_TIME_BASE,
                                         st->time_base.den, AV_ROUND_DOWN) -
                          c->seek_timestamp;
                if (ts_diff >= 0 && (c->seek_flags  & AVSEEK_FLAG_ANY ||
                                     var->pkt.flags & AV_PKT_FLAG_KEY)) {
                    c->seek_timestamp = AV_NOPTS_VALUE;
                    break;
                }
                av_free_packet(&var->pkt);
                reset_packet(&var->pkt);
            }
        }
        /* Check if this stream still is on an earlier segment number, or
         * has the packet with the lowest dts */
        if (var->pkt.data) {
            struct variant *minvar = c->variants[minvariant];
            if (minvariant < 0 || var->cur_seq_no < minvar->cur_seq_no) {
                minvariant = i;
            } else if (var->cur_seq_no == minvar->cur_seq_no) {
                int64_t dts     =    var->pkt.dts;
                int64_t mindts  = minvar->pkt.dts;
                AVStream *st    =    var->ctx->streams[var->pkt.stream_index];
                AVStream *minst = minvar->ctx->streams[minvar->pkt.stream_index];

                if (dts == AV_NOPTS_VALUE) {
                    minvariant = i;
                } else if (mindts != AV_NOPTS_VALUE) {
                    if (st->start_time    != AV_NOPTS_VALUE)
                        dts    -= st->start_time;
                    if (minst->start_time != AV_NOPTS_VALUE)
                        mindts -= minst->start_time;

                    if (av_compare_ts(dts, st->time_base,
                                      mindts, minst->time_base) < 0)
                        minvariant = i;
                }
            }
        }
    }
    if (c->end_of_segment) {
        if (recheck_discard_flags(s, 0))
            goto start;
    }
    /* If we got a packet, return it */
    if (minvariant >= 0) {
        *pkt = c->variants[minvariant]->pkt;
        pkt->stream_index += c->variants[minvariant]->stream_offset;
        reset_packet(&c->variants[minvariant]->pkt);
        return 0;
    }
    return AVERROR_EOF;
}

static int hls_close(AVFormatContext *s)
{
    HLSContext *c = s->priv_data;

    free_variant_list(c);
    return 0;
}

static int hls_read_seek(AVFormatContext *s, int stream_index,
                               int64_t timestamp, int flags)
{
    HLSContext *c = s->priv_data;
    int i, j, ret;

    if ((flags & AVSEEK_FLAG_BYTE) || !c->variants[0]->finished)
        return AVERROR(ENOSYS);

    c->seek_flags     = flags;
    c->seek_timestamp = stream_index < 0 ? timestamp :
                        av_rescale_rnd(timestamp, AV_TIME_BASE,
                                       s->streams[stream_index]->time_base.den,
                                       flags & AVSEEK_FLAG_BACKWARD ?
                                       AV_ROUND_DOWN : AV_ROUND_UP);
    timestamp = av_rescale_rnd(timestamp, AV_TIME_BASE, stream_index >= 0 ?
                               s->streams[stream_index]->time_base.den :
                               AV_TIME_BASE, flags & AVSEEK_FLAG_BACKWARD ?
                               AV_ROUND_DOWN : AV_ROUND_UP);
    if (s->duration < c->seek_timestamp) {
        c->seek_timestamp = AV_NOPTS_VALUE;
        return AVERROR(EIO);
    }

    ret = AVERROR(EIO);
    for (i = 0; i < c->n_variants; i++) {
        /* Reset reading */
        struct variant *var = c->variants[i];
        int64_t pos = c->first_timestamp == AV_NOPTS_VALUE ?
                      0 : c->first_timestamp;
        if (var->input) {
            ffurl_close(var->input);
            var->input = NULL;
        }
        av_free_packet(&var->pkt);
        reset_packet(&var->pkt);
        var->pb.eof_reached = 0;
        /* Clear any buffered data */
        var->pb.buf_end = var->pb.buf_ptr = var->pb.buffer;
        /* Reset the pos, to let the mpegts demuxer know we've seeked. */
        var->pb.pos = 0;

        /* Locate the segment that contains the target timestamp */
        for (j = 0; j < var->n_segments; j++) {
            if (timestamp >= pos &&
                timestamp < pos + var->segments[j]->duration) {
                var->cur_seq_no = var->start_seq_no + j;
                ret = 0;
                break;
            }
            pos += var->segments[j]->duration;
        }
        if (ret)
            c->seek_timestamp = AV_NOPTS_VALUE;
    }
    return ret;
}

static int hls_probe(AVProbeData *p)
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

AVInputFormat ff_hls_demuxer = {
    .name           = "hls,applehttp",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple HTTP Live Streaming"),
    .priv_data_size = sizeof(HLSContext),
    .read_probe     = hls_probe,
    .read_header    = hls_read_header,
    .read_packet    = hls_read_packet,
    .read_close     = hls_close,
    .read_seek      = hls_read_seek,
};
