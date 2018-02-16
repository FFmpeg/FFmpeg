/*
 * Apple HTTP Live Streaming segmenter
 * Copyright (c) 2012, Luca Barbato
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <float.h>
#include <stdint.h>

#include <config.h>

#if CONFIG_OPENSSL
#include <openssl/rand.h>
#endif

#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/log.h"

#include "avformat.h"
#include "internal.h"

typedef struct ListEntry {
    char  name[1024];
    int64_t duration;     // segment duration in AV_TIME_BASE units
    int discont;
    struct ListEntry *next;
} ListEntry;

typedef struct HLSContext {
    const AVClass *class;  // Class for private options.
    unsigned number;
    int64_t sequence;
    int64_t start_sequence;
    AVOutputFormat *oformat;
    AVFormatContext *avf;
    float time;            // Set by a private option.
    int  size;             // Set by a private option.
    int  wrap;             // Set by a private option.
    int  version;          // Set by a private option.
    int  allowcache;
    int64_t recording_time;
    int has_video;
    // The following timestamps are in AV_TIME_BASE units.
    int64_t start_pts;
    int64_t end_pts;
    int64_t duration;      // last segment duration computed so far.
    int nb_entries;
    ListEntry *list;
    ListEntry *end_list;
    char *basename;
    char *baseurl;

    int encrypt;           // Set by a private option.
    char *key;             // Set by a private option.
    int key_len;
    char *key_url;         // Set by a private option.
    char *iv;              // Set by a private option.
    int iv_len;

    int recovered;

    char *key_basename;

    AVDictionary *enc_opts;
} HLSContext;


static int randomize(uint8_t *buf, int len)
{
#if CONFIG_OPENSSL
    if (RAND_bytes(buf, len))
        return 0;
    return AVERROR(EIO);
#else
    return AVERROR(ENOSYS);
#endif
}

static void free_encryption(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;

    av_dict_free(&hls->enc_opts);

    av_freep(&hls->key_basename);
}

static int dict_set_bin(AVDictionary **dict, const char *key,
                        uint8_t *buf, size_t len)
{
    char hex[33];

    ff_data_to_hex(hex, buf, len, 0);
    hex[32] = '\0';

    return av_dict_set(dict, key, hex, 0);
}

static int setup_encryption(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVIOContext *out = NULL;
    int len, ret;
    uint8_t buf[16];
    uint8_t *k = NULL;

    len = strlen(hls->basename) + 4 + 1;
    hls->key_basename = av_mallocz(len);
    if (!hls->key_basename)
        return AVERROR(ENOMEM);

    av_strlcpy(hls->key_basename, hls->basename + 7, len);
    av_strlcat(hls->key_basename, ".key", len);

    if (hls->key) {
        if (hls->key_len != 16) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid key size %d, expected 16-bytes hex-coded key\n",
                   hls->key_len);
            return AVERROR(EINVAL);
        }

        if ((ret = dict_set_bin(&hls->enc_opts, "key", hls->key, hls->key_len)) < 0)
            return ret;
        k = hls->key;
    } else {
        if (hls->start_sequence < 0) {
            ret = s->io_open(s, &out, hls->key_basename, AVIO_FLAG_READ, NULL);
            if (ret < 0) {
                av_log(s, AV_LOG_WARNING,
                       "Cannot recover the key, generating a new one.\n");
            } else {
                avio_read(out, buf, 16);
                k = buf;
                avio_close(out);
            }
        }
        if (!k) {
            if ((ret = randomize(buf, sizeof(buf))) < 0) {
                av_log(s, AV_LOG_ERROR, "Cannot generate a strong random key\n");
                return ret;
            }
        }

        if ((ret = dict_set_bin(&hls->enc_opts, "key", buf, sizeof(buf))) < 0)
            return ret;
        k = buf;
    }

    if (hls->iv) {
        if (hls->iv_len != 16) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid key size %d, expected 16-bytes hex-coded initialization vector\n",
                   hls->iv_len);
            return AVERROR(EINVAL);
        }

        if ((ret = dict_set_bin(&hls->enc_opts, "iv", hls->iv, hls->iv_len)) < 0)
            return ret;
    }

    if ((ret = s->io_open(s, &out, hls->key_basename, AVIO_FLAG_WRITE, NULL)) < 0)
        return ret;

    avio_write(out, k, 16);

    avio_close(out);

    return 0;
}

static int hls_mux_init(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc;
    int i;

    hls->avf = oc = avformat_alloc_context();
    if (!oc)
        return AVERROR(ENOMEM);

    oc->oformat            = hls->oformat;
    oc->interrupt_callback = s->interrupt_callback;
    oc->opaque             = s->opaque;
    oc->io_open            = s->io_open;
    oc->io_close           = s->io_close;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st;
        if (!(st = avformat_new_stream(oc, NULL)))
            return AVERROR(ENOMEM);
        avcodec_parameters_copy(st->codecpar, s->streams[i]->codecpar);
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        st->time_base = s->streams[i]->time_base;
    }

    return 0;
}

static int append_entry(HLSContext *hls, int64_t duration, const char *name,
                        int discont)
{
    ListEntry *en = av_malloc(sizeof(*en));

    if (!en)
        return AVERROR(ENOMEM);

    av_strlcpy(en->name, name, sizeof(en->name));

    en->discont  = discont;
    en->duration = duration;
    en->next     = NULL;

    if (!hls->list)
        hls->list = en;
    else
        hls->end_list->next = en;

    hls->end_list = en;

    if (hls->nb_entries >= hls->size) {
        en = hls->list;
        hls->list = en->next;
        av_free(en);
    } else
        hls->nb_entries++;

    hls->sequence++;

    return 0;
}

static void free_entries(HLSContext *hls)
{
    ListEntry *p = hls->list, *en;

    while(p) {
        en = p;
        p = p->next;
        av_free(en);
    }
}

static int hls_window(AVFormatContext *s, int last)
{
    HLSContext *hls = s->priv_data;
    ListEntry *en;
    int64_t target_duration = 0;
    int ret = 0;
    AVIOContext *out = NULL;
    char temp_filename[1024];
    int64_t sequence = FFMAX(hls->start_sequence, hls->sequence - hls->size);

    snprintf(temp_filename, sizeof(temp_filename), "%s.tmp", s->filename);
    if ((ret = s->io_open(s, &out, temp_filename, AVIO_FLAG_WRITE, NULL)) < 0)
        goto fail;

    for (en = hls->list; en; en = en->next) {
        if (target_duration < en->duration)
            target_duration = en->duration;
    }

    avio_printf(out, "#EXTM3U\n");
    avio_printf(out, "#EXT-X-VERSION:%d\n", hls->version);
    if (hls->allowcache == 0 || hls->allowcache == 1) {
        avio_printf(out, "#EXT-X-ALLOW-CACHE:%s\n", hls->allowcache == 0 ? "NO" : "YES");
    }
    avio_printf(out, "#EXT-X-TARGETDURATION:%"PRId64"\n",
                av_rescale_rnd(target_duration, 1, AV_TIME_BASE,
                               AV_ROUND_UP));
    avio_printf(out, "#EXT-X-MEDIA-SEQUENCE:%"PRId64"\n", sequence);

    av_log(s, AV_LOG_VERBOSE, "EXT-X-MEDIA-SEQUENCE:%"PRId64"\n",
           sequence);

    for (en = hls->list; en; en = en->next) {
        if (en->discont) {
            avio_printf(out, "#EXT-X-DISCONTINUITY\n");
        }

        if (hls->encrypt) {
            char *key_url;

            if (hls->key_url)
                key_url = hls->key_url;
            else
                key_url = hls->baseurl;

            avio_printf(out, "#EXT-X-KEY:METHOD=AES-128");
            avio_printf(out, ",URI=\"");
            if (key_url)
                avio_printf(out, "%s", key_url);
            avio_printf(out, "%s\"", av_basename(hls->key_basename));
            if (hls->iv)
                avio_printf(out, ",IV=\"0x%s\"", hls->iv);
            avio_printf(out, "\n");
        }

        if (hls->version > 2)
            avio_printf(out, "#EXTINF:%f\n",
                        (double)en->duration / AV_TIME_BASE);
        else
            avio_printf(out, "#EXTINF:%"PRId64",\n",
                        av_rescale(en->duration, 1, AV_TIME_BASE));
        if (hls->baseurl)
            avio_printf(out, "%s", hls->baseurl);
        avio_printf(out, "%s\n", en->name);
    }

    if (last)
        avio_printf(out, "#EXT-X-ENDLIST\n");

fail:
    ff_format_io_close(s, &out);
    if (ret >= 0)
        ff_rename(temp_filename, s->filename);
    return ret;
}

static int hls_start(AVFormatContext *s)
{
    HLSContext *c = s->priv_data;
    AVFormatContext *oc = c->avf;
    int err = 0;
    AVDictionary *opts = NULL;


    if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                              c->basename, c->wrap ? c->sequence % c->wrap : c->sequence) < 0)
        return AVERROR(EINVAL);
    c->number++;

    if (c->encrypt) {
        if ((err = av_dict_copy(&opts, c->enc_opts, 0)) < 0)
            return err;
        if (!c->iv) {
            uint8_t iv[16] = { 0 };
            char buf[33];

            AV_WB64(iv + 8, c->sequence);
            ff_data_to_hex(buf, iv, sizeof(iv), 0);
            buf[32] = '\0';

            if ((err = av_dict_set(&opts, "iv", buf, 0)) < 0)
                goto fail;
        }
    }

    if ((err = s->io_open(s, &oc->pb, oc->filename, AVIO_FLAG_WRITE, &opts)) < 0)
        return err;

    if (oc->oformat->priv_class && oc->priv_data)
        av_opt_set(oc->priv_data, "mpegts_flags", "resend_headers", 0);

fail:
    av_dict_free(&opts);

    return err;
}

static int read_chomp_line(AVIOContext *s, char *buf, int maxlen)
{
    int len = ff_get_line(s, buf, maxlen);
    while (len > 0 && av_isspace(buf[len - 1]))
        buf[--len] = '\0';
    return len;
}

static int hls_recover(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    char line[1024];
    AVIOContext *io;
    const char *ptr;
    int ret, is_segment = 0, is_discont = 0;
    int64_t duration = 0;

    ret = s->io_open(s, &io, s->filename, AVIO_FLAG_READ, NULL);
    if (ret < 0) {
        av_log(s, AV_LOG_WARNING,
               "Cannot recover the playlist, generating a new one.\n");
        hls->start_sequence = 0;
        hls->sequence = 0;
        return 0;
    }

    read_chomp_line(io, line, sizeof(line));
    if (strcmp(line, "#EXTM3U")) {
        av_log(s, AV_LOG_ERROR,
               "The playlist file is present but unparsable."
               " Please remove it.\n");
        return AVERROR_INVALIDDATA;
    }

    while (!io->eof_reached) {
        read_chomp_line(io, line, sizeof(line));
        if (av_strstart(line, "#EXT-X-MEDIA-SEQUENCE:", &ptr)) {
            hls->sequence = hls->start_sequence = atoi(ptr);
        } else if (av_strstart(line, "#EXTINF:", &ptr)) {
            is_segment = 1;
            duration   = atof(ptr) * AV_TIME_BASE;
        } else if (av_strstart(line, "#EXT-X-DISCONTINUITY", NULL)) {
            is_discont = 1;
        } else if (av_strstart(line, "#", NULL)) {
            continue;
        } else if (line[0]) {
            if (is_segment) {
                append_entry(hls, duration, av_basename(line), is_discont);
                is_segment = 0;
                is_discont = 0;
            }
        }
    }

    hls->recovered = 1;

    return 0;
}

static int hls_setup(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    const char *pattern = "%d.ts";
    int basename_size = strlen(s->filename) + strlen(pattern) + 1;
    char *p;
    int ret;

    if (hls->encrypt)
        basename_size += 7;

    hls->basename = av_mallocz(basename_size);
    if (!hls->basename)
        return AVERROR(ENOMEM);

    // TODO: support protocol nesting?
    if (hls->encrypt)
        strcpy(hls->basename, "crypto:");

    av_strlcat(hls->basename, s->filename, basename_size);

    p = strrchr(hls->basename, '.');

    if (p)
        *p = '\0';

    if (hls->encrypt) {
        ret = setup_encryption(s);
        if (ret < 0)
            return ret;
    }

    if (hls->start_sequence < 0) {
        ret = hls_recover(s);
        if (ret < 0)
            return ret;
    }

    av_strlcat(hls->basename, pattern, basename_size);

    return 0;
}

static int hls_write_header(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    int ret, i;

    hls->sequence       = hls->start_sequence;
    hls->recording_time = hls->time * AV_TIME_BASE;
    hls->start_pts      = AV_NOPTS_VALUE;

    for (i = 0; i < s->nb_streams; i++)
        hls->has_video +=
            s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;

    if (hls->has_video > 1)
        av_log(s, AV_LOG_WARNING,
               "More than a single video stream present, "
               "expect issues decoding it.\n");

    hls->oformat = av_guess_format("mpegts", NULL, NULL);

    if (!hls->oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }

    if ((ret = hls_setup(s)) < 0)
        goto fail;

    if ((ret = hls_mux_init(s)) < 0)
        goto fail;

    if ((ret = hls_start(s)) < 0)
        goto fail;

    if ((ret = avformat_write_header(hls->avf, NULL)) < 0)
        return ret;


fail:
    if (ret) {
        av_free(hls->basename);
        if (hls->avf)
            avformat_free_context(hls->avf);

        free_encryption(s);
    }
    return ret;
}

static int hls_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc = hls->avf;
    AVStream *st = s->streams[pkt->stream_index];
    int64_t end_pts = hls->recording_time * hls->number;
    int64_t pts     = av_rescale_q(pkt->pts, st->time_base, AV_TIME_BASE_Q);
    int ret, can_split = 1;

    if (hls->start_pts == AV_NOPTS_VALUE) {
        hls->start_pts = pts;
        hls->end_pts   = pts;
    }

    if (hls->has_video) {
        can_split = st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    pkt->flags & AV_PKT_FLAG_KEY;
    }
    if (pkt->pts == AV_NOPTS_VALUE)
        can_split = 0;
    else
        hls->duration = pts - hls->end_pts;

    if (can_split && pts - hls->start_pts >= end_pts) {
        ret = append_entry(hls, hls->duration, av_basename(hls->avf->filename), hls->recovered);
        hls->recovered = 0;
        if (ret)
            return ret;

        hls->end_pts = pts;
        hls->duration = 0;

        av_write_frame(oc, NULL); /* Flush any buffered data */
        ff_format_io_close(s, &oc->pb);

        ret = hls_start(s);

        if (ret)
            return ret;

        oc = hls->avf;

        if ((ret = hls_window(s, 0)) < 0)
            return ret;
    }

    ret = ff_write_chained(oc, pkt->stream_index, pkt, s);

    return ret;
}

static int hls_write_trailer(struct AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc = hls->avf;

    av_write_trailer(oc);
    ff_format_io_close(s, &oc->pb);
    avformat_free_context(oc);
    av_free(hls->basename);
    append_entry(hls, hls->duration, av_basename(hls->avf->filename), 0);
    hls_window(s, 1);

    free_entries(hls);
    free_encryption(s);
    return 0;
}

#define OFFSET(x) offsetof(HLSContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"start_number",  "first number in the sequence",            OFFSET(start_sequence),AV_OPT_TYPE_INT64,  {.i64 = 0},     -1, INT64_MAX, E, "start_number"},
    {"recover", "If there is already a m3u8 file in the path, populate the sequence from it", 0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, E, "start_number"},
    {"hls_time",      "segment length in seconds",               OFFSET(time),    AV_OPT_TYPE_FLOAT,  {.dbl = 2},     0, FLT_MAX, E},
    {"hls_list_size", "maximum number of playlist entries",      OFFSET(size),    AV_OPT_TYPE_INT,    {.i64 = 5},     0, INT_MAX, E},
    {"hls_wrap",      "number after which the index wraps",      OFFSET(wrap),    AV_OPT_TYPE_INT,    {.i64 = 0},     0, INT_MAX, E},
    {"hls_allow_cache", "explicitly set whether the client MAY (1) or MUST NOT (0) cache media segments", OFFSET(allowcache), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, E},
    {"hls_base_url",  "url to prepend to each playlist entry",   OFFSET(baseurl), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E},
    {"hls_version",   "protocol version",                        OFFSET(version), AV_OPT_TYPE_INT,    {.i64 = 3},     2, 3, E},
    {"hls_enc",       "AES128 encryption support",               OFFSET(encrypt), AV_OPT_TYPE_INT,    {.i64 = 0},     0, 1, E},
    {"hls_enc_key",   "use the specified hex-coded 16byte key to encrypt the segments",  OFFSET(key), AV_OPT_TYPE_BINARY, .flags = E},
    {"hls_enc_key_url", "url to access the key to decrypt the segments",    OFFSET(key_url), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0, E},
    {"hls_enc_iv",     "use the specified hex-coded 16byte initialization vector",  OFFSET(iv), AV_OPT_TYPE_BINARY, .flags = E},
    { NULL },
};

static const AVClass hls_class = {
    .class_name = "hls muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};


AVOutputFormat ff_hls_muxer = {
    .name           = "hls",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple HTTP Live Streaming"),
    .extensions     = "m3u8",
    .priv_data_size = sizeof(HLSContext),
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_H264,
    .flags          = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
    .write_header   = hls_write_header,
    .write_packet   = hls_write_packet,
    .write_trailer  = hls_write_trailer,
    .priv_class     = &hls_class,
};
