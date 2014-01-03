/*
 * Apple HTTP Live Streaming demuxer
 * Copyright (c) 2010 Martin Storsjo
 * Copyright (c) 2013 Anssi Hannula
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

#define MAX_FIELD_LEN 64
#define MAX_CHARACTERISTICS_LEN 512

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

struct rendition;

/*
 * Each playlist has its own demuxer. If it currently is active,
 * it has an open AVIOContext too, and potentially an AVPacket
 * containing the next packet from this stream.
 */
struct playlist {
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

    /* Renditions associated with this playlist, if any.
     * Alternative rendition playlists have a single rendition associated
     * with them, and variant main Media Playlists may have
     * multiple (playlist-less) renditions associated with them. */
    int n_renditions;
    struct rendition **renditions;
};

/*
 * Renditions are e.g. alternative subtitle or audio streams.
 * The rendition may either be an external playlist or it may be
 * contained in the main Media Playlist of the variant (in which case
 * playlist is NULL).
 */
struct rendition {
    enum AVMediaType type;
    struct playlist *playlist;
    char group_id[MAX_FIELD_LEN];
    char language[MAX_FIELD_LEN];
    char name[MAX_FIELD_LEN];
    int disposition;
};

struct variant {
    int bandwidth;

    /* every variant contains at least the main Media Playlist in index 0 */
    int n_playlists;
    struct playlist **playlists;

    char audio_group[MAX_FIELD_LEN];
    char video_group[MAX_FIELD_LEN];
    char subtitles_group[MAX_FIELD_LEN];
};

typedef struct HLSContext {
    int n_variants;
    struct variant **variants;
    int n_playlists;
    struct playlist **playlists;
    int n_renditions;
    struct rendition **renditions;

    int cur_seq_no;
    int end_of_segment;
    int first_packet;
    int64_t first_timestamp;
    int64_t seek_timestamp;
    int seek_flags;
    AVIOInterruptCB *interrupt_callback;
    char *user_agent;                    ///< holds HTTP user agent set as an AVOption to the HTTP protocol context
    char *cookies;                       ///< holds HTTP cookie values set in either the initial response or as an AVOption to the HTTP protocol context
    char *headers;                       ///< holds HTTP headers set as an AVOption to the HTTP protocol context
} HLSContext;

static int read_chomp_line(AVIOContext *s, char *buf, int maxlen)
{
    int len = ff_get_line(s, buf, maxlen);
    while (len > 0 && av_isspace(buf[len - 1]))
        buf[--len] = '\0';
    return len;
}

static void free_segment_list(struct playlist *pls)
{
    int i;
    for (i = 0; i < pls->n_segments; i++)
        av_free(pls->segments[i]);
    av_freep(&pls->segments);
    pls->n_segments = 0;
}

static void free_playlist_list(HLSContext *c)
{
    int i;
    for (i = 0; i < c->n_playlists; i++) {
        struct playlist *pls = c->playlists[i];
        free_segment_list(pls);
        av_freep(&pls->renditions);
        av_free_packet(&pls->pkt);
        av_free(pls->pb.buffer);
        if (pls->input)
            ffurl_close(pls->input);
        if (pls->ctx) {
            pls->ctx->pb = NULL;
            avformat_close_input(&pls->ctx);
        }
        av_free(pls);
    }
    av_freep(&c->playlists);
    av_freep(&c->cookies);
    av_freep(&c->user_agent);
    c->n_playlists = 0;
}

static void free_variant_list(HLSContext *c)
{
    int i;
    for (i = 0; i < c->n_variants; i++) {
        struct variant *var = c->variants[i];
        av_freep(&var->playlists);
        av_free(var);
    }
    av_freep(&c->variants);
    c->n_variants = 0;
}

static void free_rendition_list(HLSContext *c)
{
    int i;
    for (i = 0; i < c->n_renditions; i++)
        av_free(c->renditions[i]);
    av_freep(&c->renditions);
    c->n_renditions = 0;
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

static struct playlist *new_playlist(HLSContext *c, const char *url,
                                     const char *base)
{
    struct playlist *pls = av_mallocz(sizeof(struct playlist));
    if (!pls)
        return NULL;
    reset_packet(&pls->pkt);
    ff_make_absolute_url(pls->url, sizeof(pls->url), base, url);
    dynarray_add(&c->playlists, &c->n_playlists, pls);
    return pls;
}

struct variant_info {
    char bandwidth[20];
    /* variant group ids: */
    char audio[MAX_FIELD_LEN];
    char video[MAX_FIELD_LEN];
    char subtitles[MAX_FIELD_LEN];
};

static struct variant *new_variant(HLSContext *c, struct variant_info *info,
                                   const char *url, const char *base)
{
    struct variant *var;
    struct playlist *pls;

    pls = new_playlist(c, url, base);
    if (!pls)
        return NULL;

    var = av_mallocz(sizeof(struct variant));
    if (!var)
        return NULL;

    if (info) {
        var->bandwidth = atoi(info->bandwidth);
        strcpy(var->audio_group, info->audio);
        strcpy(var->video_group, info->video);
        strcpy(var->subtitles_group, info->subtitles);
    }

    dynarray_add(&c->variants, &c->n_variants, var);
    dynarray_add(&var->playlists, &var->n_playlists, pls);
    return var;
}

static void handle_variant_args(struct variant_info *info, const char *key,
                                int key_len, char **dest, int *dest_len)
{
    if (!strncmp(key, "BANDWIDTH=", key_len)) {
        *dest     =        info->bandwidth;
        *dest_len = sizeof(info->bandwidth);
    } else if (!strncmp(key, "AUDIO=", key_len)) {
        *dest     =        info->audio;
        *dest_len = sizeof(info->audio);
    } else if (!strncmp(key, "VIDEO=", key_len)) {
        *dest     =        info->video;
        *dest_len = sizeof(info->video);
    } else if (!strncmp(key, "SUBTITLES=", key_len)) {
        *dest     =        info->subtitles;
        *dest_len = sizeof(info->subtitles);
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

struct rendition_info {
    char type[16];
    char uri[MAX_URL_SIZE];
    char group_id[MAX_FIELD_LEN];
    char language[MAX_FIELD_LEN];
    char assoc_language[MAX_FIELD_LEN];
    char name[MAX_FIELD_LEN];
    char defaultr[4];
    char forced[4];
    char characteristics[MAX_CHARACTERISTICS_LEN];
};

static struct rendition *new_rendition(HLSContext *c, struct rendition_info *info,
                                      const char *url_base)
{
    struct rendition *rend;
    enum AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    char *characteristic;
    char *chr_ptr;
    char *saveptr;

    if (!strcmp(info->type, "AUDIO"))
        type = AVMEDIA_TYPE_AUDIO;
    else if (!strcmp(info->type, "VIDEO"))
        type = AVMEDIA_TYPE_VIDEO;
    else if (!strcmp(info->type, "SUBTITLES"))
        type = AVMEDIA_TYPE_SUBTITLE;
    else if (!strcmp(info->type, "CLOSED-CAPTIONS"))
        /* CLOSED-CAPTIONS is ignored since we do not support CEA-608 CC in
         * AVC SEI RBSP anyway */
        return NULL;

    if (type == AVMEDIA_TYPE_UNKNOWN)
        return NULL;

    /* URI is mandatory for subtitles as per spec */
    if (type == AVMEDIA_TYPE_SUBTITLE && !info->uri[0])
        return NULL;

    rend = av_mallocz(sizeof(struct rendition));
    if (!rend)
        return NULL;

    dynarray_add(&c->renditions, &c->n_renditions, rend);

    rend->type = type;
    strcpy(rend->group_id, info->group_id);
    strcpy(rend->language, info->language);
    strcpy(rend->name, info->name);

    /* add the playlist if this is an external rendition */
    if (info->uri[0]) {
        rend->playlist = new_playlist(c, info->uri, url_base);
        if (rend->playlist)
            dynarray_add(&rend->playlist->renditions,
                         &rend->playlist->n_renditions, rend);
    }

    if (info->assoc_language[0]) {
        int langlen = strlen(rend->language);
        if (langlen < sizeof(rend->language) - 3) {
            rend->language[langlen] = ',';
            strncpy(rend->language + langlen + 1, info->assoc_language,
                    sizeof(rend->language) - langlen - 2);
        }
    }

    if (!strcmp(info->defaultr, "YES"))
        rend->disposition |= AV_DISPOSITION_DEFAULT;
    if (!strcmp(info->forced, "YES"))
        rend->disposition |= AV_DISPOSITION_FORCED;

    chr_ptr = info->characteristics;
    while ((characteristic = av_strtok(chr_ptr, ",", &saveptr))) {
        if (!strcmp(characteristic, "public.accessibility.describes-music-and-sound"))
            rend->disposition |= AV_DISPOSITION_HEARING_IMPAIRED;
        else if (!strcmp(characteristic, "public.accessibility.describes-video"))
            rend->disposition |= AV_DISPOSITION_VISUAL_IMPAIRED;

        chr_ptr = NULL;
    }

    return rend;
}

static void handle_rendition_args(struct rendition_info *info, const char *key,
                                  int key_len, char **dest, int *dest_len)
{
    if (!strncmp(key, "TYPE=", key_len)) {
        *dest     =        info->type;
        *dest_len = sizeof(info->type);
    } else if (!strncmp(key, "URI=", key_len)) {
        *dest     =        info->uri;
        *dest_len = sizeof(info->uri);
    } else if (!strncmp(key, "GROUP-ID=", key_len)) {
        *dest     =        info->group_id;
        *dest_len = sizeof(info->group_id);
    } else if (!strncmp(key, "LANGUAGE=", key_len)) {
        *dest     =        info->language;
        *dest_len = sizeof(info->language);
    } else if (!strncmp(key, "ASSOC-LANGUAGE=", key_len)) {
        *dest     =        info->assoc_language;
        *dest_len = sizeof(info->assoc_language);
    } else if (!strncmp(key, "NAME=", key_len)) {
        *dest     =        info->name;
        *dest_len = sizeof(info->name);
    } else if (!strncmp(key, "DEFAULT=", key_len)) {
        *dest     =        info->defaultr;
        *dest_len = sizeof(info->defaultr);
    } else if (!strncmp(key, "FORCED=", key_len)) {
        *dest     =        info->forced;
        *dest_len = sizeof(info->forced);
    } else if (!strncmp(key, "CHARACTERISTICS=", key_len)) {
        *dest     =        info->characteristics;
        *dest_len = sizeof(info->characteristics);
    }
    /*
     * ignored:
     * - AUTOSELECT: client may autoselect based on e.g. system language
     * - INSTREAM-ID: EIA-608 closed caption number ("CC1".."CC4")
     */
}

static int parse_playlist(HLSContext *c, const char *url,
                          struct playlist *pls, AVIOContext *in)
{
    int ret = 0, is_segment = 0, is_variant = 0;
    int64_t duration = 0;
    enum KeyType key_type = KEY_NONE;
    uint8_t iv[16] = "";
    int has_iv = 0;
    char key[MAX_URL_SIZE] = "";
    char line[MAX_URL_SIZE];
    const char *ptr;
    int close_in = 0;
    uint8_t *new_url = NULL;
    struct variant_info variant_info;

    if (!in) {
        AVDictionary *opts = NULL;
        close_in = 1;
        /* Some HLS servers don't like being sent the range header */
        av_dict_set(&opts, "seekable", "0", 0);

        // broker prior HTTP options that should be consistent across requests
        av_dict_set(&opts, "user-agent", c->user_agent, 0);
        av_dict_set(&opts, "cookies", c->cookies, 0);
        av_dict_set(&opts, "headers", c->headers, 0);

        ret = avio_open2(&in, url, AVIO_FLAG_READ,
                         c->interrupt_callback, &opts);
        av_dict_free(&opts);
        if (ret < 0)
            return ret;
    }

    if (av_opt_get(in, "location", AV_OPT_SEARCH_CHILDREN, &new_url) >= 0)
        url = new_url;

    read_chomp_line(in, line, sizeof(line));
    if (strcmp(line, "#EXTM3U")) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (pls) {
        free_segment_list(pls);
        pls->finished = 0;
    }
    while (!url_feof(in)) {
        read_chomp_line(in, line, sizeof(line));
        if (av_strstart(line, "#EXT-X-STREAM-INF:", &ptr)) {
            is_variant = 1;
            memset(&variant_info, 0, sizeof(variant_info));
            ff_parse_key_value(ptr, (ff_parse_key_val_cb) handle_variant_args,
                               &variant_info);
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
        } else if (av_strstart(line, "#EXT-X-MEDIA:", &ptr)) {
            struct rendition_info info = {{0}};
            ff_parse_key_value(ptr, (ff_parse_key_val_cb) handle_rendition_args,
                               &info);
            new_rendition(c, &info, url);
        } else if (av_strstart(line, "#EXT-X-TARGETDURATION:", &ptr)) {
            if (!pls) {
                if (!new_variant(c, NULL, url, NULL)) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                pls = c->playlists[c->n_playlists - 1];
            }
            pls->target_duration = atoi(ptr) * AV_TIME_BASE;
        } else if (av_strstart(line, "#EXT-X-MEDIA-SEQUENCE:", &ptr)) {
            if (!pls) {
                if (!new_variant(c, NULL, url, NULL)) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                pls = c->playlists[c->n_playlists - 1];
            }
            pls->start_seq_no = atoi(ptr);
        } else if (av_strstart(line, "#EXT-X-ENDLIST", &ptr)) {
            if (pls)
                pls->finished = 1;
        } else if (av_strstart(line, "#EXTINF:", &ptr)) {
            is_segment = 1;
            duration   = atof(ptr) * AV_TIME_BASE;
        } else if (av_strstart(line, "#", NULL)) {
            continue;
        } else if (line[0]) {
            if (is_variant) {
                if (!new_variant(c, &variant_info, line, url)) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                is_variant = 0;
            }
            if (is_segment) {
                struct segment *seg;
                if (!pls) {
                    if (!new_variant(c, 0, url, NULL)) {
                        ret = AVERROR(ENOMEM);
                        goto fail;
                    }
                    pls = c->playlists[c->n_playlists - 1];
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
                    int seq = pls->start_seq_no + pls->n_segments;
                    memset(seg->iv, 0, sizeof(seg->iv));
                    AV_WB32(seg->iv + 12, seq);
                }
                ff_make_absolute_url(seg->key, sizeof(seg->key), url, key);
                ff_make_absolute_url(seg->url, sizeof(seg->url), url, line);
                dynarray_add(&pls->segments, &pls->n_segments, seg);
                is_segment = 0;
            }
        }
    }
    if (pls)
        pls->last_load_time = av_gettime();

fail:
    av_free(new_url);
    if (close_in)
        avio_close(in);
    return ret;
}

static int open_input(HLSContext *c, struct playlist *pls)
{
    AVDictionary *opts = NULL;
    int ret;
    struct segment *seg = pls->segments[pls->cur_seq_no - pls->start_seq_no];

    // broker prior HTTP options that should be consistent across requests
    av_dict_set(&opts, "user-agent", c->user_agent, 0);
    av_dict_set(&opts, "cookies", c->cookies, 0);
    av_dict_set(&opts, "headers", c->headers, 0);
    av_dict_set(&opts, "seekable", "0", 0);

    if (seg->key_type == KEY_NONE) {
        ret = ffurl_open(&pls->input, seg->url, AVIO_FLAG_READ,
                          &pls->parent->interrupt_callback, &opts);
        goto cleanup;
    } else if (seg->key_type == KEY_AES_128) {
        char iv[33], key[33], url[MAX_URL_SIZE];
        if (strcmp(seg->key, pls->key_url)) {
            URLContext *uc;
            if (ffurl_open(&uc, seg->key, AVIO_FLAG_READ,
                           &pls->parent->interrupt_callback, &opts) == 0) {
                if (ffurl_read_complete(uc, pls->key, sizeof(pls->key))
                    != sizeof(pls->key)) {
                    av_log(NULL, AV_LOG_ERROR, "Unable to read key file %s\n",
                           seg->key);
                }
                ffurl_close(uc);
            } else {
                av_log(NULL, AV_LOG_ERROR, "Unable to open key file %s\n",
                       seg->key);
            }
            av_strlcpy(pls->key_url, seg->key, sizeof(pls->key_url));
        }
        ff_data_to_hex(iv, seg->iv, sizeof(seg->iv), 0);
        ff_data_to_hex(key, pls->key, sizeof(pls->key), 0);
        iv[32] = key[32] = '\0';
        if (strstr(seg->url, "://"))
            snprintf(url, sizeof(url), "crypto+%s", seg->url);
        else
            snprintf(url, sizeof(url), "crypto:%s", seg->url);
        if ((ret = ffurl_alloc(&pls->input, url, AVIO_FLAG_READ,
                               &pls->parent->interrupt_callback)) < 0)
            goto cleanup;
        av_opt_set(pls->input->priv_data, "key", key, 0);
        av_opt_set(pls->input->priv_data, "iv", iv, 0);
        /* Need to repopulate options */
        av_dict_free(&opts);
        av_dict_set(&opts, "seekable", "0", 0);
        if ((ret = ffurl_connect(pls->input, &opts)) < 0) {
            ffurl_close(pls->input);
            pls->input = NULL;
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
    struct playlist *v = opaque;
    HLSContext *c = v->parent->priv_data;
    int ret, i;

    if (!v->needed)
        return AVERROR_EOF;

restart:
    if (!v->input) {
        /* If this is a live stream and the reload interval has elapsed since
         * the last playlist reload, reload the playlists now. */
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
        av_log(v->parent, AV_LOG_INFO, "No longer receiving playlist %d\n",
               v->index);
        return AVERROR_EOF;
    }
    goto restart;
}

static int playlist_in_multiple_variants(HLSContext *c, struct playlist *pls)
{
    int variant_count = 0;
    int i, j;

    for (i = 0; i < c->n_variants && variant_count < 2; i++) {
        struct variant *v = c->variants[i];

        for (j = 0; j < v->n_playlists; j++) {
            if (v->playlists[j] == pls) {
                variant_count++;
                break;
            }
        }
    }

    return variant_count >= 2;
}

static void add_renditions_to_variant(HLSContext *c, struct variant *var,
                                      enum AVMediaType type, const char *group_id)
{
    int i;

    for (i = 0; i < c->n_renditions; i++) {
        struct rendition *rend = c->renditions[i];

        if (rend->type == type && !strcmp(rend->group_id, group_id)) {

            if (rend->playlist)
                /* rendition is an external playlist
                 * => add the playlist to the variant */
                dynarray_add(&var->playlists, &var->n_playlists, rend->playlist);
            else
                /* rendition is part of the variant main Media Playlist
                 * => add the rendition to the main Media Playlist */
                dynarray_add(&var->playlists[0]->renditions,
                             &var->playlists[0]->n_renditions,
                             rend);
        }
    }
}

static void add_metadata_from_renditions(AVFormatContext *s, struct playlist *pls,
                                         enum AVMediaType type)
{
    int rend_idx = 0;
    int i;

    for (i = 0; i < pls->ctx->nb_streams; i++) {
        AVStream *st = s->streams[pls->stream_offset + i];

        if (st->codec->codec_type != type)
            continue;

        for (; rend_idx < pls->n_renditions; rend_idx++) {
            struct rendition *rend = pls->renditions[rend_idx];

            if (rend->type != type)
                continue;

            if (rend->language[0])
                av_dict_set(&st->metadata, "language", rend->language, 0);
            if (rend->name[0])
                av_dict_set(&st->metadata, "comment", rend->name, 0);

            st->disposition |= rend->disposition;
        }
        if (rend_idx >=pls->n_renditions)
            break;
    }
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

        // get the previous headers & set back to null if string size is zero
        av_freep(&c->headers);
        av_opt_get(u->priv_data, "headers", 0, (uint8_t**)&(c->headers));
        if (c->headers && !strlen(c->headers))
            av_freep(&c->headers);
    }

    if ((ret = parse_playlist(c, s->filename, NULL, s->pb)) < 0)
        goto fail;

    if (c->n_variants == 0) {
        av_log(NULL, AV_LOG_WARNING, "Empty playlist\n");
        ret = AVERROR_EOF;
        goto fail;
    }
    /* If the playlist only contained playlists (Master Playlist),
     * parse each individual playlist. */
    if (c->n_playlists > 1 || c->playlists[0]->n_segments == 0) {
        for (i = 0; i < c->n_playlists; i++) {
            struct playlist *pls = c->playlists[i];
            if ((ret = parse_playlist(c, pls->url, pls, NULL)) < 0)
                goto fail;
        }
    }

    if (c->variants[0]->playlists[0]->n_segments == 0) {
        av_log(NULL, AV_LOG_WARNING, "Empty playlist\n");
        ret = AVERROR_EOF;
        goto fail;
    }

    /* If this isn't a live stream, calculate the total duration of the
     * stream. */
    if (c->variants[0]->playlists[0]->finished) {
        int64_t duration = 0;
        for (i = 0; i < c->variants[0]->playlists[0]->n_segments; i++)
            duration += c->variants[0]->playlists[0]->segments[i]->duration;
        s->duration = duration;
    }

    /* Associate renditions with variants */
    for (i = 0; i < c->n_variants; i++) {
        struct variant *var = c->variants[i];

        if (var->audio_group[0])
            add_renditions_to_variant(c, var, AVMEDIA_TYPE_AUDIO, var->audio_group);
        if (var->video_group[0])
            add_renditions_to_variant(c, var, AVMEDIA_TYPE_VIDEO, var->video_group);
        if (var->subtitles_group[0])
            add_renditions_to_variant(c, var, AVMEDIA_TYPE_SUBTITLE, var->subtitles_group);
    }

    /* Open the demuxer for each playlist */
    for (i = 0; i < c->n_playlists; i++) {
        struct playlist *pls = c->playlists[i];
        AVInputFormat *in_fmt = NULL;

        if (pls->n_segments == 0)
            continue;

        if (!(pls->ctx = avformat_alloc_context())) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        pls->index  = i;
        pls->needed = 1;
        pls->parent = s;

        /* If this is a live stream with more than 3 segments, start at the
         * third last segment. */
        pls->cur_seq_no = pls->start_seq_no;
        if (!pls->finished && pls->n_segments > 3)
            pls->cur_seq_no = pls->start_seq_no + pls->n_segments - 3;

        pls->read_buffer = av_malloc(INITIAL_BUFFER_SIZE);
        ffio_init_context(&pls->pb, pls->read_buffer, INITIAL_BUFFER_SIZE, 0, pls,
                          read_data, NULL, NULL);
        pls->pb.seekable = 0;
        ret = av_probe_input_buffer(&pls->pb, &in_fmt, pls->segments[0]->url,
                                    NULL, 0, 0);
        if (ret < 0) {
            /* Free the ctx - it isn't initialized properly at this point,
             * so avformat_close_input shouldn't be called. If
             * avformat_open_input fails below, it frees and zeros the
             * context, so it doesn't need any special treatment like this. */
            av_log(s, AV_LOG_ERROR, "Error when loading first segment '%s'\n", pls->segments[0]->url);
            avformat_free_context(pls->ctx);
            pls->ctx = NULL;
            goto fail;
        }
        pls->ctx->pb       = &pls->pb;
        pls->stream_offset = stream_offset;
        ret = avformat_open_input(&pls->ctx, pls->segments[0]->url, in_fmt, NULL);
        if (ret < 0)
            goto fail;

        pls->ctx->ctx_flags &= ~AVFMTCTX_NOHEADER;
        ret = avformat_find_stream_info(pls->ctx, NULL);
        if (ret < 0)
            goto fail;

        /* Create new AVStreams for each stream in this playlist */
        for (j = 0; j < pls->ctx->nb_streams; j++) {
            AVStream *st = avformat_new_stream(s, NULL);
            AVStream *ist = pls->ctx->streams[j];
            if (!st) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            st->id = i;
            avpriv_set_pts_info(st, ist->pts_wrap_bits, ist->time_base.num, ist->time_base.den);
            avcodec_copy_context(st->codec, pls->ctx->streams[j]->codec);
        }

        add_metadata_from_renditions(s, pls, AVMEDIA_TYPE_AUDIO);
        add_metadata_from_renditions(s, pls, AVMEDIA_TYPE_VIDEO);
        add_metadata_from_renditions(s, pls, AVMEDIA_TYPE_SUBTITLE);

        stream_offset += pls->ctx->nb_streams;
    }

    /* Create a program for each variant */
    for (i = 0; i < c->n_variants; i++) {
        struct variant *v = c->variants[i];
        char bitrate_str[20];
        AVProgram *program;

        snprintf(bitrate_str, sizeof(bitrate_str), "%d", v->bandwidth);

        program = av_new_program(s, i);
        if (!program)
            goto fail;
        av_dict_set(&program->metadata, "variant_bitrate", bitrate_str, 0);

        for (j = 0; j < v->n_playlists; j++) {
            struct playlist *pls = v->playlists[j];
            int is_shared = playlist_in_multiple_variants(c, pls);
            int k;

            for (k = 0; k < pls->ctx->nb_streams; k++) {
                struct AVStream *st = s->streams[pls->stream_offset + k];

                ff_program_add_stream_index(s, i, pls->stream_offset + k);

                /* Set variant_bitrate for streams unique to this variant */
                if (!is_shared && v->bandwidth)
                    av_dict_set(&st->metadata, "variant_bitrate", bitrate_str, 0);
            }
        }
    }

    c->first_packet = 1;
    c->first_timestamp = AV_NOPTS_VALUE;
    c->seek_timestamp  = AV_NOPTS_VALUE;

    return 0;
fail:
    free_playlist_list(c);
    free_variant_list(c);
    free_rendition_list(c);
    return ret;
}

static int recheck_discard_flags(AVFormatContext *s, int first)
{
    HLSContext *c = s->priv_data;
    int i, changed = 0;

    /* Check if any new streams are needed */
    for (i = 0; i < c->n_playlists; i++)
        c->playlists[i]->cur_needed = 0;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        struct playlist *pls = c->playlists[s->streams[i]->id];
        if (st->discard < AVDISCARD_ALL)
            pls->cur_needed = 1;
    }
    for (i = 0; i < c->n_playlists; i++) {
        struct playlist *pls = c->playlists[i];
        if (pls->cur_needed && !pls->needed) {
            pls->needed = 1;
            changed = 1;
            pls->cur_seq_no = c->cur_seq_no;
            pls->pb.eof_reached = 0;
            av_log(s, AV_LOG_INFO, "Now receiving playlist %d\n", i);
        } else if (first && !pls->cur_needed && pls->needed) {
            if (pls->input)
                ffurl_close(pls->input);
            pls->input = NULL;
            pls->needed = 0;
            changed = 1;
            av_log(s, AV_LOG_INFO, "No longer receiving playlist %d\n", i);
        }
    }
    return changed;
}

static int hls_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    HLSContext *c = s->priv_data;
    int ret, i, minplaylist = -1;

    if (c->first_packet) {
        recheck_discard_flags(s, 1);
        c->first_packet = 0;
    }

start:
    c->end_of_segment = 0;
    for (i = 0; i < c->n_playlists; i++) {
        struct playlist *pls = c->playlists[i];
        /* Make sure we've got one buffered packet from each open playlist
         * stream */
        if (pls->needed && !pls->pkt.data) {
            while (1) {
                int64_t ts_diff;
                AVStream *st;
                ret = av_read_frame(pls->ctx, &pls->pkt);
                if (ret < 0) {
                    if (!url_feof(&pls->pb) && ret != AVERROR_EOF)
                        return ret;
                    reset_packet(&pls->pkt);
                    break;
                } else {
                    if (c->first_timestamp == AV_NOPTS_VALUE &&
                        pls->pkt.dts       != AV_NOPTS_VALUE)
                        c->first_timestamp = av_rescale_q(pls->pkt.dts,
                            pls->ctx->streams[pls->pkt.stream_index]->time_base,
                            AV_TIME_BASE_Q);
                }

                if (c->seek_timestamp == AV_NOPTS_VALUE)
                    break;

                if (pls->pkt.dts == AV_NOPTS_VALUE) {
                    c->seek_timestamp = AV_NOPTS_VALUE;
                    break;
                }

                st = pls->ctx->streams[pls->pkt.stream_index];
                ts_diff = av_rescale_rnd(pls->pkt.dts, AV_TIME_BASE,
                                         st->time_base.den, AV_ROUND_DOWN) -
                          c->seek_timestamp;
                if (ts_diff >= 0 && (c->seek_flags  & AVSEEK_FLAG_ANY ||
                                     pls->pkt.flags & AV_PKT_FLAG_KEY)) {
                    c->seek_timestamp = AV_NOPTS_VALUE;
                    break;
                }
                av_free_packet(&pls->pkt);
                reset_packet(&pls->pkt);
            }
        }
        /* Check if this stream still is on an earlier segment number, or
         * has the packet with the lowest dts */
        if (pls->pkt.data) {
            struct playlist *minpls = minplaylist < 0 ?
                                     NULL : c->playlists[minplaylist];
            if (minplaylist < 0 || pls->cur_seq_no < minpls->cur_seq_no) {
                minplaylist = i;
            } else if (pls->cur_seq_no == minpls->cur_seq_no) {
                int64_t dts     =    pls->pkt.dts;
                int64_t mindts  = minpls->pkt.dts;
                AVStream *st    =    pls->ctx->streams[pls->pkt.stream_index];
                AVStream *minst = minpls->ctx->streams[minpls->pkt.stream_index];

                if (dts == AV_NOPTS_VALUE) {
                    minplaylist = i;
                } else if (mindts != AV_NOPTS_VALUE) {
                    if (st->start_time    != AV_NOPTS_VALUE)
                        dts    -= st->start_time;
                    if (minst->start_time != AV_NOPTS_VALUE)
                        mindts -= minst->start_time;

                    if (av_compare_ts(dts, st->time_base,
                                      mindts, minst->time_base) < 0)
                        minplaylist = i;
                }
            }
        }
    }
    if (c->end_of_segment) {
        if (recheck_discard_flags(s, 0))
            goto start;
    }
    /* If we got a packet, return it */
    if (minplaylist >= 0) {
        *pkt = c->playlists[minplaylist]->pkt;
        pkt->stream_index += c->playlists[minplaylist]->stream_offset;
        reset_packet(&c->playlists[minplaylist]->pkt);
        return 0;
    }
    return AVERROR_EOF;
}

static int hls_close(AVFormatContext *s)
{
    HLSContext *c = s->priv_data;

    free_playlist_list(c);
    free_variant_list(c);
    free_rendition_list(c);
    return 0;
}

static int hls_read_seek(AVFormatContext *s, int stream_index,
                               int64_t timestamp, int flags)
{
    HLSContext *c = s->priv_data;
    int i, j, ret;

    if ((flags & AVSEEK_FLAG_BYTE) || !c->variants[0]->playlists[0]->finished)
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
    for (i = 0; i < c->n_playlists; i++) {
        /* Reset reading */
        struct playlist *pls = c->playlists[i];
        int64_t pos = c->first_timestamp == AV_NOPTS_VALUE ?
                      0 : c->first_timestamp;
        if (pls->input) {
            ffurl_close(pls->input);
            pls->input = NULL;
        }
        av_free_packet(&pls->pkt);
        reset_packet(&pls->pkt);
        pls->pb.eof_reached = 0;
        /* Clear any buffered data */
        pls->pb.buf_end = pls->pb.buf_ptr = pls->pb.buffer;
        /* Reset the pos, to let the mpegts demuxer know we've seeked. */
        pls->pb.pos = 0;

        /* Locate the segment that contains the target timestamp */
        for (j = 0; j < pls->n_segments; j++) {
            if (timestamp >= pos &&
                timestamp < pos + pls->segments[j]->duration) {
                pls->cur_seq_no = pls->start_seq_no + j;
                ret = 0;
                break;
            }
            pos += pls->segments[j]->duration;
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
