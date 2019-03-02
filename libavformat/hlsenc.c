/*
 * Apple HTTP Live Streaming segmenter
 * Copyright (c) 2012, Luca Barbato
 * Copyright (c) 2017 Akamai Technologies, Inc.
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

#include "config.h"
#include <float.h>
#include <stdint.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if CONFIG_GCRYPT
#include <gcrypt.h>
#elif CONFIG_OPENSSL
#include <openssl/rand.h>
#endif

#include "libavutil/avassert.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/random_seed.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"
#include "libavutil/time_internal.h"

#include "avformat.h"
#include "avio_internal.h"
#if CONFIG_HTTP_PROTOCOL
#include "http.h"
#endif
#include "hlsplaylist.h"
#include "internal.h"
#include "os_support.h"

typedef enum {
  HLS_START_SEQUENCE_AS_START_NUMBER = 0,
  HLS_START_SEQUENCE_AS_SECONDS_SINCE_EPOCH = 1,
  HLS_START_SEQUENCE_AS_FORMATTED_DATETIME = 2,  // YYYYMMDDhhmmss
} StartSequenceSourceType;

typedef enum {
    CODEC_ATTRIBUTE_WRITTEN = 0,
    CODEC_ATTRIBUTE_WILL_NOT_BE_WRITTEN,
} CodecAttributeStatus;

#define KEYSIZE 16
#define LINE_BUFFER_SIZE 1024
#define HLS_MICROSECOND_UNIT   1000000
#define POSTFIX_PATTERN "_%d"

typedef struct HLSSegment {
    char filename[1024];
    char sub_filename[1024];
    double duration; /* in seconds */
    int discont;
    int64_t pos;
    int64_t size;
    unsigned var_stream_idx;

    char key_uri[LINE_BUFFER_SIZE + 1];
    char iv_string[KEYSIZE*2 + 1];

    struct HLSSegment *next;
} HLSSegment;

typedef enum HLSFlags {
    // Generate a single media file and use byte ranges in the playlist.
    HLS_SINGLE_FILE = (1 << 0),
    HLS_DELETE_SEGMENTS = (1 << 1),
    HLS_ROUND_DURATIONS = (1 << 2),
    HLS_DISCONT_START = (1 << 3),
    HLS_OMIT_ENDLIST = (1 << 4),
    HLS_SPLIT_BY_TIME = (1 << 5),
    HLS_APPEND_LIST = (1 << 6),
    HLS_PROGRAM_DATE_TIME = (1 << 7),
    HLS_SECOND_LEVEL_SEGMENT_INDEX = (1 << 8), // include segment index in segment filenames when use_localtime  e.g.: %%03d
    HLS_SECOND_LEVEL_SEGMENT_DURATION = (1 << 9), // include segment duration (microsec) in segment filenames when use_localtime  e.g.: %%09t
    HLS_SECOND_LEVEL_SEGMENT_SIZE = (1 << 10), // include segment size (bytes) in segment filenames when use_localtime  e.g.: %%014s
    HLS_TEMP_FILE = (1 << 11),
    HLS_PERIODIC_REKEY = (1 << 12),
    HLS_INDEPENDENT_SEGMENTS = (1 << 13),
} HLSFlags;

typedef enum {
    SEGMENT_TYPE_MPEGTS,
    SEGMENT_TYPE_FMP4,
} SegmentType;

typedef struct VariantStream {
    unsigned var_stream_idx;
    unsigned number;
    int64_t sequence;
    AVOutputFormat *oformat;
    AVOutputFormat *vtt_oformat;
    AVIOContext *out;
    int packets_written;
    int init_range_length;

    AVFormatContext *avf;
    AVFormatContext *vtt_avf;

    int has_video;
    int has_subtitle;
    int new_start;
    double dpp;           // duration per packet
    int64_t start_pts;
    int64_t end_pts;
    double duration;      // last segment duration computed so far, in seconds
    int64_t start_pos;    // last segment starting position
    int64_t size;         // last segment size
    int nb_entries;
    int discontinuity_set;
    int discontinuity;
    int reference_stream_index;

    HLSSegment *segments;
    HLSSegment *last_segment;
    HLSSegment *old_segments;

    char *basename;
    char *vtt_basename;
    char *vtt_m3u8_name;
    char *m3u8_name;

    double initial_prog_date_time;
    char current_segment_final_filename_fmt[1024]; // when renaming segments

    char *fmp4_init_filename;
    char *base_output_dirname;

    AVStream **streams;
    char codec_attr[128];
    CodecAttributeStatus attr_status;
    unsigned int nb_streams;
    int m3u8_created; /* status of media play-list creation */
    int is_default; /* default status of audio group */
    char *language; /* audio lauguage name */
    char *agroup; /* audio group name */
    char *ccgroup; /* closed caption group name */
    char *baseurl;
} VariantStream;

typedef struct ClosedCaptionsStream {
    char *ccgroup; /* closed caption group name */
    char *instreamid; /* closed captions INSTREAM-ID */
    char *language; /* closed captions langauge */
} ClosedCaptionsStream;

typedef struct HLSContext {
    const AVClass *class;  // Class for private options.
    int64_t start_sequence;
    uint32_t start_sequence_source_type;  // enum StartSequenceSourceType

    float time;            // Set by a private option.
    float init_time;       // Set by a private option.
    int max_nb_segments;   // Set by a private option.
    int hls_delete_threshold; // Set by a private option.
#if FF_API_HLS_WRAP
    int  wrap;             // Set by a private option.
#endif
    uint32_t flags;        // enum HLSFlags
    uint32_t pl_type;      // enum PlaylistType
    char *segment_filename;
    char *fmp4_init_filename;
    int segment_type;

    int use_localtime;      ///< flag to expand filename with localtime
    int use_localtime_mkdir;///< flag to mkdir dirname in timebased filename
    int allowcache;
    int64_t recording_time;
    int64_t max_seg_size; // every segment file max size

    char *baseurl;
    char *format_options_str;
    char *vtt_format_options_str;
    char *subtitle_filename;
    AVDictionary *format_options;

    int encrypt;
    char *key;
    char *key_url;
    char *iv;
    char *key_basename;
    int encrypt_started;

    char *key_info_file;
    char key_file[LINE_BUFFER_SIZE + 1];
    char key_uri[LINE_BUFFER_SIZE + 1];
    char key_string[KEYSIZE*2 + 1];
    char iv_string[KEYSIZE*2 + 1];
    AVDictionary *vtt_format_options;

    char *method;
    char *user_agent;

    VariantStream *var_streams;
    unsigned int nb_varstreams;
    ClosedCaptionsStream *cc_streams;
    unsigned int nb_ccstreams;

    int master_m3u8_created; /* status of master play-list creation */
    char *master_m3u8_url; /* URL of the master m3u8 file */
    int version; /* HLS version */
    char *var_stream_map; /* user specified variant stream map string */
    char *cc_stream_map; /* user specified closed caption streams map string */
    char *master_pl_name;
    unsigned int master_publish_rate;
    int http_persistent;
    AVIOContext *m3u8_out;
    AVIOContext *sub_m3u8_out;
    int64_t timeout;
    int ignore_io_errors;
    int has_default_key; /* has DEFAULT field of var_stream_map */
    int has_video_m3u8; /* has video stream m3u8 list */
} HLSContext;

static int hlsenc_io_open(AVFormatContext *s, AVIOContext **pb, char *filename,
                          AVDictionary **options) {
    HLSContext *hls = s->priv_data;
    int http_base_proto = filename ? ff_is_http_proto(filename) : 0;
    int err = AVERROR_MUXER_NOT_FOUND;
    if (!*pb || !http_base_proto || !hls->http_persistent) {
        err = s->io_open(s, pb, filename, AVIO_FLAG_WRITE, options);
#if CONFIG_HTTP_PROTOCOL
    } else {
        URLContext *http_url_context = ffio_geturlcontext(*pb);
        av_assert0(http_url_context);
        err = ff_http_do_new_request(http_url_context, filename);
        if (err < 0)
            ff_format_io_close(s, pb);

#endif
    }
    return err;
}

static void hlsenc_io_close(AVFormatContext *s, AVIOContext **pb, char *filename) {
    HLSContext *hls = s->priv_data;
    int http_base_proto = filename ? ff_is_http_proto(filename) : 0;
    if (!*pb)
        return;
    if (!http_base_proto || !hls->http_persistent || hls->key_info_file || hls->encrypt) {
        ff_format_io_close(s, pb);
#if CONFIG_HTTP_PROTOCOL
    } else {
        URLContext *http_url_context = ffio_geturlcontext(*pb);
        av_assert0(http_url_context);
        avio_flush(*pb);
        ffurl_shutdown(http_url_context, AVIO_FLAG_WRITE);
#endif
    }
}

static void set_http_options(AVFormatContext *s, AVDictionary **options, HLSContext *c)
{
    int http_base_proto = ff_is_http_proto(s->url);

    if (c->method) {
        av_dict_set(options, "method", c->method, 0);
    } else if (http_base_proto) {
        av_log(c, AV_LOG_WARNING, "No HTTP method set, hls muxer defaulting to method PUT.\n");
        av_dict_set(options, "method", "PUT", 0);
    }
    if (c->user_agent)
        av_dict_set(options, "user_agent", c->user_agent, 0);
    if (c->http_persistent)
        av_dict_set_int(options, "multiple_requests", 1, 0);
    if (c->timeout >= 0)
        av_dict_set_int(options, "timeout", c->timeout, 0);
}

static void write_codec_attr(AVStream *st, VariantStream *vs) {
    int codec_strlen = strlen(vs->codec_attr);
    char attr[32];

    if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
        return;
    if (vs->attr_status == CODEC_ATTRIBUTE_WILL_NOT_BE_WRITTEN)
        return;

    if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
        uint8_t *data = st->codecpar->extradata;
        if (data && (data[0] | data[1] | data[2]) == 0 && data[3] == 1 && (data[4] & 0x1F) == 7) {
            snprintf(attr, sizeof(attr),
                     "avc1.%02x%02x%02x", data[5], data[6], data[7]);
        } else {
            goto fail;
        }
    } else if (st->codecpar->codec_id == AV_CODEC_ID_MP2) {
        snprintf(attr, sizeof(attr), "mp4a.40.33");
    } else if (st->codecpar->codec_id == AV_CODEC_ID_MP3) {
        snprintf(attr, sizeof(attr), "mp4a.40.34");
    } else if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
        /* TODO : For HE-AAC, HE-AACv2, the last digit needs to be set to 5 and 29 respectively */
        snprintf(attr, sizeof(attr), "mp4a.40.2");
    } else if (st->codecpar->codec_id == AV_CODEC_ID_AC3) {
        snprintf(attr, sizeof(attr), "ac-3");
    } else if (st->codecpar->codec_id == AV_CODEC_ID_EAC3) {
        snprintf(attr, sizeof(attr), "ec-3");
    } else {
        goto fail;
    }
    // Don't write the same attribute multiple times
    if (!av_stristr(vs->codec_attr, attr)) {
        snprintf(vs->codec_attr + codec_strlen,
                 sizeof(vs->codec_attr) - codec_strlen,
                 "%s%s", codec_strlen ? "," : "", attr);
    }
    return;

fail:
    vs->codec_attr[0] = '\0';
    vs->attr_status = CODEC_ATTRIBUTE_WILL_NOT_BE_WRITTEN;
    return;
}

static int replace_int_data_in_filename(char **s, const char *filename, char placeholder, int64_t number)
{
    const char *p;
    char *new_filename;
    char c;
    int nd, addchar_count;
    int found_count = 0;
    AVBPrint buf;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    p = filename;
    for (;;) {
        c = *p;
        if (c == '\0')
            break;
        if (c == '%' && *(p+1) == '%')  // %%
            addchar_count = 2;
        else if (c == '%' && (av_isdigit(*(p+1)) || *(p+1) == placeholder)) {
            nd = 0;
            addchar_count = 1;
            while (av_isdigit(*(p + addchar_count))) {
                nd = nd * 10 + *(p + addchar_count) - '0';
                addchar_count++;
            }

            if (*(p + addchar_count) == placeholder) {
                av_bprintf(&buf, "%0*"PRId64, (number < 0) ? nd : nd++, number);
                p += (addchar_count + 1);
                addchar_count = 0;
                found_count++;
            }

        } else
            addchar_count = 1;

        av_bprint_append_data(&buf, p, addchar_count);
        p += addchar_count;
    }
    if (!av_bprint_is_complete(&buf)) {
        av_bprint_finalize(&buf, NULL);
        return -1;
    }
    if (av_bprint_finalize(&buf, &new_filename) < 0 || !new_filename)
        return -1;
    *s = new_filename;
    return found_count;
}

static void write_styp(AVIOContext *pb)
{
    avio_wb32(pb, 24);
    ffio_wfourcc(pb, "styp");
    ffio_wfourcc(pb, "msdh");
    avio_wb32(pb, 0); /* minor */
    ffio_wfourcc(pb, "msdh");
    ffio_wfourcc(pb, "msix");
}

static int flush_dynbuf(VariantStream *vs, int *range_length)
{
    AVFormatContext *ctx = vs->avf;
    uint8_t *buffer;

    if (!ctx->pb) {
        return AVERROR(EINVAL);
    }

    // flush
    av_write_frame(ctx, NULL);
    avio_flush(ctx->pb);

    // write out to file
    *range_length = avio_close_dyn_buf(ctx->pb, &buffer);
    ctx->pb = NULL;
    avio_write(vs->out, buffer, *range_length);
    av_free(buffer);

    // re-open buffer
    return avio_open_dyn_buf(&ctx->pb);
}

static int hls_delete_old_segments(AVFormatContext *s, HLSContext *hls,
                                   VariantStream *vs) {

    HLSSegment *segment, *previous_segment = NULL;
    float playlist_duration = 0.0f;
    int ret = 0, path_size, sub_path_size;
    int segment_cnt = 0;
    char *dirname = NULL, *p, *sub_path;
    char *path = NULL;
    char *vtt_dirname = NULL;
    AVDictionary *options = NULL;
    AVIOContext *out = NULL;
    const char *proto = NULL;

    segment = vs->segments;
    while (segment) {
        playlist_duration += segment->duration;
        segment = segment->next;
    }

    segment = vs->old_segments;
    segment_cnt = 0;
    while (segment) {
        playlist_duration -= segment->duration;
        previous_segment = segment;
        segment = previous_segment->next;
        segment_cnt++;
        if (playlist_duration <= -previous_segment->duration) {
            previous_segment->next = NULL;
            break;
        }
        if (segment_cnt >= hls->hls_delete_threshold) {
            previous_segment->next = NULL;
            break;
        }
    }

    if (segment && !hls->use_localtime_mkdir) {
        if (hls->segment_filename) {
            dirname = av_strdup(hls->segment_filename);
        } else {
            dirname = av_strdup(vs->avf->url);
        }
        if (!dirname) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        p = (char *)av_basename(dirname);
        *p = '\0';

    }

    while (segment) {
        char * r_dirname = dirname;

        /* if %v is present in the file's directory */
        if (dirname && av_stristr(dirname, "%v")) {

            if (replace_int_data_in_filename(&r_dirname, dirname, 'v', segment->var_stream_idx) < 1) {
                ret = AVERROR(EINVAL);
                goto fail;
            }
            av_free(dirname);
            dirname = r_dirname;
        }

        av_log(hls, AV_LOG_DEBUG, "deleting old segment %s\n",
                                  segment->filename);
        path_size =  (hls->use_localtime_mkdir ? 0 : strlen(dirname)) + strlen(segment->filename) + 1;
        path = av_malloc(path_size);
        if (!path) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        if (hls->use_localtime_mkdir)
            av_strlcpy(path, segment->filename, path_size);
        else { // segment->filename contains basename only
            av_strlcpy(path, dirname, path_size);
            av_strlcat(path, segment->filename, path_size);
        }

        proto = avio_find_protocol_name(s->url);
        if (hls->method || (proto && !av_strcasecmp(proto, "http"))) {
            av_dict_set(&options, "method", "DELETE", 0);
            if ((ret = vs->avf->io_open(vs->avf, &out, path, AVIO_FLAG_WRITE, &options)) < 0) {
                if (hls->ignore_io_errors)
                    ret = 0;
                goto fail;
            }
            ff_format_io_close(vs->avf, &out);
        } else if (unlink(path) < 0) {
            av_log(hls, AV_LOG_ERROR, "failed to delete old segment %s: %s\n",
                                     path, strerror(errno));
        }

        if ((segment->sub_filename[0] != '\0')) {
            vtt_dirname = av_strdup(vs->vtt_avf->url);
            if (!vtt_dirname) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            p = (char *)av_basename(vtt_dirname);
            *p = '\0';
            sub_path_size = strlen(segment->sub_filename) + 1 + strlen(vtt_dirname);
            sub_path = av_malloc(sub_path_size);
            if (!sub_path) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            av_strlcpy(sub_path, vtt_dirname, sub_path_size);
            av_strlcat(sub_path, segment->sub_filename, sub_path_size);

            if (hls->method || (proto && !av_strcasecmp(proto, "http"))) {
                av_dict_set(&options, "method", "DELETE", 0);
                if ((ret = vs->vtt_avf->io_open(vs->vtt_avf, &out, sub_path, AVIO_FLAG_WRITE, &options)) < 0) {
                    if (hls->ignore_io_errors)
                        ret = 0;
                    av_free(sub_path);
                    goto fail;
                }
                ff_format_io_close(vs->vtt_avf, &out);
            } else if (unlink(sub_path) < 0) {
                av_log(hls, AV_LOG_ERROR, "failed to delete old segment %s: %s\n",
                                         sub_path, strerror(errno));
            }
            av_free(sub_path);
        }
        av_freep(&path);
        previous_segment = segment;
        segment = previous_segment->next;
        av_free(previous_segment);
    }

fail:
    av_free(path);
    av_free(dirname);
    av_free(vtt_dirname);

    return ret;
}

static int randomize(uint8_t *buf, int len)
{
#if CONFIG_GCRYPT
    gcry_randomize(buf, len, GCRY_VERY_STRONG_RANDOM);
    return 0;
#elif CONFIG_OPENSSL
    if (RAND_bytes(buf, len))
        return 0;
#else
    return AVERROR(ENOSYS);
#endif
    return AVERROR(EINVAL);
}

static int do_encrypt(AVFormatContext *s, VariantStream *vs)
{
    HLSContext *hls = s->priv_data;
    int ret;
    int len;
    AVIOContext *pb;
    uint8_t key[KEYSIZE];

    len = strlen(s->url) + 4 + 1;
    hls->key_basename = av_mallocz(len);
    if (!hls->key_basename)
        return AVERROR(ENOMEM);

    av_strlcpy(hls->key_basename, s->url, len);
    av_strlcat(hls->key_basename, ".key", len);

    if (hls->key_url) {
        av_strlcpy(hls->key_file, hls->key_url, sizeof(hls->key_file));
        av_strlcpy(hls->key_uri, hls->key_url, sizeof(hls->key_uri));
    } else {
        av_strlcpy(hls->key_file, hls->key_basename, sizeof(hls->key_file));
        av_strlcpy(hls->key_uri, hls->key_basename, sizeof(hls->key_uri));
    }

    if (!*hls->iv_string) {
        uint8_t iv[16] = { 0 };
        char buf[33];

        if (!hls->iv) {
            AV_WB64(iv + 8, vs->sequence);
        } else {
            memcpy(iv, hls->iv, sizeof(iv));
        }
        ff_data_to_hex(buf, iv, sizeof(iv), 0);
        buf[32] = '\0';
        memcpy(hls->iv_string, buf, sizeof(hls->iv_string));
    }

    if (!*hls->key_uri) {
        av_log(hls, AV_LOG_ERROR, "no key URI specified in key info file\n");
        return AVERROR(EINVAL);
    }

    if (!*hls->key_file) {
        av_log(hls, AV_LOG_ERROR, "no key file specified in key info file\n");
        return AVERROR(EINVAL);
    }

    if (!*hls->key_string) {
        if (!hls->key) {
            if ((ret = randomize(key, sizeof(key))) < 0) {
                av_log(s, AV_LOG_ERROR, "Cannot generate a strong random key\n");
                return ret;
            }
        } else {
            memcpy(key, hls->key, sizeof(key));
        }

        ff_data_to_hex(hls->key_string, key, sizeof(key), 0);
        if ((ret = s->io_open(s, &pb, hls->key_file, AVIO_FLAG_WRITE, NULL)) < 0)
            return ret;
        avio_seek(pb, 0, SEEK_CUR);
        avio_write(pb, key, KEYSIZE);
        avio_close(pb);
    }
    return 0;
}


static int hls_encryption_start(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    int ret;
    AVIOContext *pb;
    uint8_t key[KEYSIZE];

    if ((ret = s->io_open(s, &pb, hls->key_info_file, AVIO_FLAG_READ, NULL)) < 0) {
        av_log(hls, AV_LOG_ERROR,
                "error opening key info file %s\n", hls->key_info_file);
        return ret;
    }

    ff_get_line(pb, hls->key_uri, sizeof(hls->key_uri));
    hls->key_uri[strcspn(hls->key_uri, "\r\n")] = '\0';

    ff_get_line(pb, hls->key_file, sizeof(hls->key_file));
    hls->key_file[strcspn(hls->key_file, "\r\n")] = '\0';

    ff_get_line(pb, hls->iv_string, sizeof(hls->iv_string));
    hls->iv_string[strcspn(hls->iv_string, "\r\n")] = '\0';

    ff_format_io_close(s, &pb);

    if (!*hls->key_uri) {
        av_log(hls, AV_LOG_ERROR, "no key URI specified in key info file\n");
        return AVERROR(EINVAL);
    }

    if (!*hls->key_file) {
        av_log(hls, AV_LOG_ERROR, "no key file specified in key info file\n");
        return AVERROR(EINVAL);
    }

    if ((ret = s->io_open(s, &pb, hls->key_file, AVIO_FLAG_READ, NULL)) < 0) {
        av_log(hls, AV_LOG_ERROR, "error opening key file %s\n", hls->key_file);
        return ret;
    }

    ret = avio_read(pb, key, sizeof(key));
    ff_format_io_close(s, &pb);
    if (ret != sizeof(key)) {
        av_log(hls, AV_LOG_ERROR, "error reading key file %s\n", hls->key_file);
        if (ret >= 0 || ret == AVERROR_EOF)
            ret = AVERROR(EINVAL);
        return ret;
    }
    ff_data_to_hex(hls->key_string, key, sizeof(key), 0);

    return 0;
}

static int hls_mux_init(AVFormatContext *s, VariantStream *vs)
{
    AVDictionary *options = NULL;
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc;
    AVFormatContext *vtt_oc = NULL;
    int byterange_mode = (hls->flags & HLS_SINGLE_FILE) || (hls->max_seg_size > 0);
    int i, ret;

    ret = avformat_alloc_output_context2(&vs->avf, vs->oformat, NULL, NULL);
    if (ret < 0)
        return ret;
    oc = vs->avf;

    oc->url                = av_strdup("");
    if (!oc->url)
        return AVERROR(ENOMEM);

    oc->oformat            = vs->oformat;
    oc->interrupt_callback = s->interrupt_callback;
    oc->max_delay          = s->max_delay;
    oc->opaque             = s->opaque;
    oc->io_open            = s->io_open;
    oc->io_close           = s->io_close;
    av_dict_copy(&oc->metadata, s->metadata, 0);

    if(vs->vtt_oformat) {
        ret = avformat_alloc_output_context2(&vs->vtt_avf, vs->vtt_oformat, NULL, NULL);
        if (ret < 0)
            return ret;
        vtt_oc          = vs->vtt_avf;
        vtt_oc->oformat = vs->vtt_oformat;
        av_dict_copy(&vtt_oc->metadata, s->metadata, 0);
    }

    for (i = 0; i < vs->nb_streams; i++) {
        AVStream *st;
        AVFormatContext *loc;
        if (vs->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
            loc = vtt_oc;
        else
            loc = oc;

        if (!(st = avformat_new_stream(loc, NULL)))
            return AVERROR(ENOMEM);
        avcodec_parameters_copy(st->codecpar, vs->streams[i]->codecpar);
        if (!oc->oformat->codec_tag ||
            av_codec_get_id (oc->oformat->codec_tag, vs->streams[i]->codecpar->codec_tag) == st->codecpar->codec_id ||
            av_codec_get_tag(oc->oformat->codec_tag, vs->streams[i]->codecpar->codec_id) <= 0) {
            st->codecpar->codec_tag = vs->streams[i]->codecpar->codec_tag;
        } else {
            st->codecpar->codec_tag = 0;
        }

        st->sample_aspect_ratio = vs->streams[i]->sample_aspect_ratio;
        st->time_base = vs->streams[i]->time_base;
        av_dict_copy(&st->metadata, vs->streams[i]->metadata, 0);
    }

    vs->packets_written = 1;
    vs->start_pos = 0;
    vs->new_start = 1;

    if (hls->segment_type == SEGMENT_TYPE_FMP4) {
        if (hls->max_seg_size > 0) {
            av_log(s, AV_LOG_WARNING, "Multi-file byterange mode is currently unsupported in the HLS muxer.\n");
            return AVERROR_PATCHWELCOME;
        }

        vs->packets_written = 0;
        vs->init_range_length = 0;
        set_http_options(s, &options, hls);
        if ((ret = avio_open_dyn_buf(&oc->pb)) < 0)
            return ret;

        if (byterange_mode) {
            ret = hlsenc_io_open(s, &vs->out, vs->basename, &options);
        } else {
            ret = hlsenc_io_open(s, &vs->out, vs->base_output_dirname, &options);
        }
        av_dict_free(&options);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to open segment '%s'\n", vs->fmp4_init_filename);
            return ret;
        }

        if (hls->format_options_str) {
            ret = av_dict_parse_string(&hls->format_options, hls->format_options_str, "=", ":", 0);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "Could not parse format options list '%s'\n",
                       hls->format_options_str);
                return ret;
            }
        }

        av_dict_copy(&options, hls->format_options, 0);
        av_dict_set(&options, "fflags", "-autobsf", 0);
        av_dict_set(&options, "movflags", "+frag_custom+dash+delay_moov", AV_DICT_APPEND);
        ret = avformat_init_output(oc, &options);
        if (ret < 0)
            return ret;
        if (av_dict_count(options)) {
            av_log(s, AV_LOG_ERROR, "Some of the provided format options in '%s' are not recognized\n", hls->format_options_str);
            av_dict_free(&options);
            return AVERROR(EINVAL);
        }
        avio_flush(oc->pb);
        av_dict_free(&options);
    }
    return 0;
}

static HLSSegment *find_segment_by_filename(HLSSegment *segment, const char *filename)
{
    while (segment) {
        if (!av_strcasecmp(segment->filename,filename))
            return segment;
        segment = segment->next;
    }
    return (HLSSegment *) NULL;
}

static int sls_flags_filename_process(struct AVFormatContext *s, HLSContext *hls,
                                      VariantStream *vs, HLSSegment *en,
                                      double duration, int64_t pos, int64_t size)
{
    if ((hls->flags & (HLS_SECOND_LEVEL_SEGMENT_SIZE | HLS_SECOND_LEVEL_SEGMENT_DURATION)) &&
        strlen(vs->current_segment_final_filename_fmt)) {
        char * new_url = av_strdup(vs->current_segment_final_filename_fmt);
        if (!new_url) {
            av_free(en);
            return AVERROR(ENOMEM);
        }
        ff_format_set_url(vs->avf, new_url);
        if (hls->flags & HLS_SECOND_LEVEL_SEGMENT_SIZE) {
            char *filename = NULL;
            if (replace_int_data_in_filename(&filename, vs->avf->url, 's', pos + size) < 1) {
                av_log(hls, AV_LOG_ERROR,
                       "Invalid second level segment filename template '%s', "
                        "you can try to remove second_level_segment_size flag\n",
                       vs->avf->url);
                av_free(filename);
                av_free(en);
                return AVERROR(EINVAL);
            }
            ff_format_set_url(vs->avf, filename);
        }
        if (hls->flags & HLS_SECOND_LEVEL_SEGMENT_DURATION) {
            char *filename = NULL;
            if (replace_int_data_in_filename(&filename, vs->avf->url,
                't',  (int64_t)round(duration * HLS_MICROSECOND_UNIT)) < 1) {
                av_log(hls, AV_LOG_ERROR,
                       "Invalid second level segment filename template '%s', "
                        "you can try to remove second_level_segment_time flag\n",
                       vs->avf->url);
                av_free(filename);
                av_free(en);
                return AVERROR(EINVAL);
            }
            ff_format_set_url(vs->avf, filename);
        }
    }
    return 0;
}

static int sls_flag_check_duration_size_index(HLSContext *hls)
{
    int ret = 0;

    if (hls->flags & HLS_SECOND_LEVEL_SEGMENT_DURATION) {
         av_log(hls, AV_LOG_ERROR,
                "second_level_segment_duration hls_flag requires strftime to be true\n");
         ret = AVERROR(EINVAL);
    }
    if (hls->flags & HLS_SECOND_LEVEL_SEGMENT_SIZE) {
         av_log(hls, AV_LOG_ERROR,
                "second_level_segment_size hls_flag requires strfime to be true\n");
         ret = AVERROR(EINVAL);
    }
    if (hls->flags & HLS_SECOND_LEVEL_SEGMENT_INDEX) {
        av_log(hls, AV_LOG_ERROR,
               "second_level_segment_index hls_flag requires strftime to be true\n");
        ret = AVERROR(EINVAL);
    }

    return ret;
}

static int sls_flag_check_duration_size(HLSContext *hls, VariantStream *vs)
{
    const char *proto = avio_find_protocol_name(vs->basename);
    int segment_renaming_ok = proto && !strcmp(proto, "file");
    int ret = 0;

    if ((hls->flags & HLS_SECOND_LEVEL_SEGMENT_DURATION) && !segment_renaming_ok) {
         av_log(hls, AV_LOG_ERROR,
                "second_level_segment_duration hls_flag works only with file protocol segment names\n");
         ret = AVERROR(EINVAL);
    }
    if ((hls->flags & HLS_SECOND_LEVEL_SEGMENT_SIZE) && !segment_renaming_ok) {
         av_log(hls, AV_LOG_ERROR,
                "second_level_segment_size hls_flag works only with file protocol segment names\n");
         ret = AVERROR(EINVAL);
    }

    return ret;
}

static void sls_flag_file_rename(HLSContext *hls, VariantStream *vs, char *old_filename) {
    if ((hls->flags & (HLS_SECOND_LEVEL_SEGMENT_SIZE | HLS_SECOND_LEVEL_SEGMENT_DURATION)) &&
        strlen(vs->current_segment_final_filename_fmt)) {
        ff_rename(old_filename, vs->avf->url, hls);
    }
}

static int sls_flag_use_localtime_filename(AVFormatContext *oc, HLSContext *c, VariantStream *vs)
{
    if (c->flags & HLS_SECOND_LEVEL_SEGMENT_INDEX) {
        char *filename = NULL;
        if (replace_int_data_in_filename(&filename,
#if FF_API_HLS_WRAP
            oc->url, 'd', c->wrap ? vs->sequence % c->wrap : vs->sequence) < 1) {
#else
            oc->url, 'd', vs->sequence) < 1) {
#endif
            av_log(c, AV_LOG_ERROR, "Invalid second level segment filename template '%s', "
                    "you can try to remove second_level_segment_index flag\n",
                   oc->url);
            av_free(filename);
            return AVERROR(EINVAL);
        }
        ff_format_set_url(oc, filename);
    }
    if (c->flags & (HLS_SECOND_LEVEL_SEGMENT_SIZE | HLS_SECOND_LEVEL_SEGMENT_DURATION)) {
        av_strlcpy(vs->current_segment_final_filename_fmt, oc->url,
                   sizeof(vs->current_segment_final_filename_fmt));
        if (c->flags & HLS_SECOND_LEVEL_SEGMENT_SIZE) {
            char *filename = NULL;
            if (replace_int_data_in_filename(&filename, oc->url, 's', 0) < 1) {
                av_log(c, AV_LOG_ERROR, "Invalid second level segment filename template '%s', "
                        "you can try to remove second_level_segment_size flag\n",
                       oc->url);
                av_free(filename);
                return AVERROR(EINVAL);
            }
            ff_format_set_url(oc, filename);
        }
        if (c->flags & HLS_SECOND_LEVEL_SEGMENT_DURATION) {
            char *filename = NULL;
            if (replace_int_data_in_filename(&filename, oc->url, 't', 0) < 1) {
                av_log(c, AV_LOG_ERROR, "Invalid second level segment filename template '%s', "
                        "you can try to remove second_level_segment_time flag\n",
                       oc->url);
                av_free(filename);
                return AVERROR(EINVAL);
            }
            ff_format_set_url(oc, filename);
        }
    }
    return 0;
}

/* Create a new segment and append it to the segment list */
static int hls_append_segment(struct AVFormatContext *s, HLSContext *hls,
                              VariantStream *vs, double duration, int64_t pos,
                              int64_t size)
{
    HLSSegment *en = av_malloc(sizeof(*en));
    const char  *filename;
    int byterange_mode = (hls->flags & HLS_SINGLE_FILE) || (hls->max_seg_size > 0);
    int ret;

    if (!en)
        return AVERROR(ENOMEM);

    en->var_stream_idx = vs->var_stream_idx;
    ret = sls_flags_filename_process(s, hls, vs, en, duration, pos, size);
    if (ret < 0) {
        return ret;
    }

    filename = av_basename(vs->avf->url);

    if (hls->use_localtime_mkdir) {
        filename = vs->avf->url;
    }
    if ((find_segment_by_filename(vs->segments, filename) || find_segment_by_filename(vs->old_segments, filename))
        && !byterange_mode) {
        av_log(hls, AV_LOG_WARNING, "Duplicated segment filename detected: %s\n", filename);
    }
    av_strlcpy(en->filename, filename, sizeof(en->filename));

    if(vs->has_subtitle)
        av_strlcpy(en->sub_filename, av_basename(vs->vtt_avf->url), sizeof(en->sub_filename));
    else
        en->sub_filename[0] = '\0';

    en->duration = duration;
    en->pos      = pos;
    en->size     = size;
    en->next     = NULL;
    en->discont  = 0;

    if (vs->discontinuity) {
        en->discont = 1;
        vs->discontinuity = 0;
    }

    if (hls->key_info_file || hls->encrypt) {
        av_strlcpy(en->key_uri, hls->key_uri, sizeof(en->key_uri));
        av_strlcpy(en->iv_string, hls->iv_string, sizeof(en->iv_string));
    }

    if (!vs->segments)
        vs->segments = en;
    else
        vs->last_segment->next = en;

    vs->last_segment = en;

    // EVENT or VOD playlists imply sliding window cannot be used
    if (hls->pl_type != PLAYLIST_TYPE_NONE)
        hls->max_nb_segments = 0;

    if (hls->max_nb_segments && vs->nb_entries >= hls->max_nb_segments) {
        en = vs->segments;
        vs->initial_prog_date_time += en->duration;
        vs->segments = en->next;
        if (en && hls->flags & HLS_DELETE_SEGMENTS &&
#if FF_API_HLS_WRAP
                !(hls->flags & HLS_SINGLE_FILE || hls->wrap)) {
#else
                !(hls->flags & HLS_SINGLE_FILE)) {
#endif
            en->next = vs->old_segments;
            vs->old_segments = en;
            if ((ret = hls_delete_old_segments(s, hls, vs)) < 0)
                return ret;
        } else
            av_free(en);
    } else
        vs->nb_entries++;

    if (hls->max_seg_size > 0) {
        return 0;
    }
    vs->sequence++;

    return 0;
}

static int parse_playlist(AVFormatContext *s, const char *url, VariantStream *vs)
{
    HLSContext *hls = s->priv_data;
    AVIOContext *in;
    int ret = 0, is_segment = 0;
    int64_t new_start_pos;
    char line[1024];
    const char *ptr;
    const char *end;

    if ((ret = ffio_open_whitelist(&in, url, AVIO_FLAG_READ,
                                   &s->interrupt_callback, NULL,
                                   s->protocol_whitelist, s->protocol_blacklist)) < 0)
        return ret;

    ff_get_chomp_line(in, line, sizeof(line));
    if (strcmp(line, "#EXTM3U")) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    vs->discontinuity = 0;
    while (!avio_feof(in)) {
        ff_get_chomp_line(in, line, sizeof(line));
        if (av_strstart(line, "#EXT-X-MEDIA-SEQUENCE:", &ptr)) {
            int64_t tmp_sequence = strtoll(ptr, NULL, 10);
            if (tmp_sequence < vs->sequence)
              av_log(hls, AV_LOG_VERBOSE,
                     "Found playlist sequence number was smaller """
                     "than specified start sequence number: %"PRId64" < %"PRId64", "
                     "omitting\n", tmp_sequence, hls->start_sequence);
            else {
              av_log(hls, AV_LOG_DEBUG, "Found playlist sequence number: %"PRId64"\n", tmp_sequence);
              vs->sequence = tmp_sequence;
            }
        } else if (av_strstart(line, "#EXT-X-DISCONTINUITY", &ptr)) {
            is_segment = 1;
            vs->discontinuity = 1;
        } else if (av_strstart(line, "#EXTINF:", &ptr)) {
            is_segment = 1;
            vs->duration = atof(ptr);
        } else if (av_stristart(line, "#EXT-X-KEY:", &ptr)) {
            ptr = av_stristr(line, "URI=\"");
            if (ptr) {
                ptr += strlen("URI=\"");
                end = av_stristr(ptr, ",");
                if (end) {
                    av_strlcpy(hls->key_uri, ptr, end - ptr);
                } else {
                    av_strlcpy(hls->key_uri, ptr, sizeof(hls->key_uri));
                }
            }

            ptr = av_stristr(line, "IV=0x");
            if (ptr) {
                ptr += strlen("IV=0x");
                end = av_stristr(ptr, ",");
                if (end) {
                    av_strlcpy(hls->iv_string, ptr, end - ptr);
                } else {
                    av_strlcpy(hls->iv_string, ptr, sizeof(hls->iv_string));
                }
            }

        } else if (av_strstart(line, "#", NULL)) {
            continue;
        } else if (line[0]) {
            if (is_segment) {
                char *new_file = av_strdup(line);
                if (!new_file) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                ff_format_set_url(vs->avf, new_file);
                is_segment = 0;
                new_start_pos = avio_tell(vs->avf->pb);
                vs->size = new_start_pos - vs->start_pos;
                ret = hls_append_segment(s, hls, vs, vs->duration, vs->start_pos, vs->size);
                if (ret < 0)
                    goto fail;
                vs->start_pos = new_start_pos;
            }
        }
    }

fail:
    avio_close(in);
    return ret;
}

static void hls_free_segments(HLSSegment *p)
{
    HLSSegment *en;

    while(p) {
        en = p;
        p = p->next;
        av_free(en);
    }
}

static int hls_rename_temp_file(AVFormatContext *s, AVFormatContext *oc)
{
    size_t len = strlen(oc->url);
    char *final_filename = av_strdup(oc->url);
    int ret;

    if (!final_filename)
        return AVERROR(ENOMEM);
    final_filename[len-4] = '\0';
    ret = ff_rename(oc->url, final_filename, s);
    oc->url[len-4] = '\0';
    av_freep(&final_filename);
    return ret;
}

static int get_relative_url(const char *master_url, const char *media_url,
                            char *rel_url, int rel_url_buf_size)
{
    char *p = NULL;
    int base_len = -1;
    p = strrchr(master_url, '/') ? strrchr(master_url, '/') :\
            strrchr(master_url, '\\');
    if (p) {
        base_len = FFABS(p - master_url);
        if (av_strncasecmp(master_url, media_url, base_len)) {
            av_log(NULL, AV_LOG_WARNING, "Unable to find relative url\n");
            return AVERROR(EINVAL);
        }
    }
    av_strlcpy(rel_url, &(media_url[base_len + 1]), rel_url_buf_size);
    return 0;
}

static int64_t get_stream_bit_rate(AVStream *stream) {
    AVCPBProperties *props = (AVCPBProperties*)av_stream_get_side_data(
        stream,
        AV_PKT_DATA_CPB_PROPERTIES,
        NULL
    );

    if (stream->codecpar->bit_rate)
        return stream->codecpar->bit_rate;
    else if (props)
        return props->max_bitrate;

    return 0;
}

static int create_master_playlist(AVFormatContext *s,
                                  VariantStream * const input_vs)
{
    HLSContext *hls = s->priv_data;
    VariantStream *vs, *temp_vs;
    AVStream *vid_st, *aud_st;
    AVDictionary *options = NULL;
    unsigned int i, j;
    int m3u8_name_size, ret, bandwidth;
    char *m3u8_rel_name, *ccgroup;
    ClosedCaptionsStream *ccs;

    input_vs->m3u8_created = 1;
    if (!hls->master_m3u8_created) {
        /* For the first time, wait until all the media playlists are created */
        for (i = 0; i < hls->nb_varstreams; i++)
            if (!hls->var_streams[i].m3u8_created)
                return 0;
    } else {
         /* Keep publishing the master playlist at the configured rate */
        if (&hls->var_streams[0] != input_vs || !hls->master_publish_rate ||
            input_vs->number % hls->master_publish_rate)
            return 0;
    }

    set_http_options(s, &options, hls);

    ret = hlsenc_io_open(s, &hls->m3u8_out, hls->master_m3u8_url, &options);
    av_dict_free(&options);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open master play list file '%s'\n",
                hls->master_m3u8_url);
        goto fail;
    }

    ff_hls_write_playlist_version(hls->m3u8_out, hls->version);

    for (i = 0; i < hls->nb_ccstreams; i++) {
        ccs = &(hls->cc_streams[i]);
        avio_printf(hls->m3u8_out, "#EXT-X-MEDIA:TYPE=CLOSED-CAPTIONS");
        avio_printf(hls->m3u8_out, ",GROUP-ID=\"%s\"", ccs->ccgroup);
        avio_printf(hls->m3u8_out, ",NAME=\"%s\"", ccs->instreamid);
        if (ccs->language)
            avio_printf(hls->m3u8_out, ",LANGUAGE=\"%s\"", ccs->language);
        avio_printf(hls->m3u8_out, ",INSTREAM-ID=\"%s\"\n", ccs->instreamid);
    }

    /* For audio only variant streams add #EXT-X-MEDIA tag with attributes*/
    for (i = 0; i < hls->nb_varstreams; i++) {
        vs = &(hls->var_streams[i]);

        if (vs->has_video || vs->has_subtitle || !vs->agroup)
            continue;

        m3u8_name_size = strlen(vs->m3u8_name) + 1;
        m3u8_rel_name = av_malloc(m3u8_name_size);
        if (!m3u8_rel_name) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        av_strlcpy(m3u8_rel_name, vs->m3u8_name, m3u8_name_size);
        ret = get_relative_url(hls->master_m3u8_url, vs->m3u8_name,
                               m3u8_rel_name, m3u8_name_size);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to find relative URL\n");
            goto fail;
        }

        ff_hls_write_audio_rendition(hls->m3u8_out, vs->agroup, m3u8_rel_name, vs->language, i, hls->has_default_key ? vs->is_default : 1);

        av_freep(&m3u8_rel_name);
    }

    /* For variant streams with video add #EXT-X-STREAM-INF tag with attributes*/
    for (i = 0; i < hls->nb_varstreams; i++) {
        vs = &(hls->var_streams[i]);

        m3u8_name_size = strlen(vs->m3u8_name) + 1;
        m3u8_rel_name = av_malloc(m3u8_name_size);
        if (!m3u8_rel_name) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        av_strlcpy(m3u8_rel_name, vs->m3u8_name, m3u8_name_size);
        ret = get_relative_url(hls->master_m3u8_url, vs->m3u8_name,
                               m3u8_rel_name, m3u8_name_size);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Unable to find relative URL\n");
            goto fail;
        }

        vid_st = NULL;
        aud_st = NULL;
        for (j = 0; j < vs->nb_streams; j++) {
            if (vs->streams[j]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                vid_st = vs->streams[j];
            else if (vs->streams[j]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                aud_st = vs->streams[j];
        }

        if (!vid_st && !aud_st) {
            av_log(NULL, AV_LOG_WARNING, "Media stream not found\n");
            continue;
        }

        /**
         * Traverse through the list of audio only rendition streams and find
         * the rendition which has highest bitrate in the same audio group
         */
        if (vs->agroup) {
            for (j = 0; j < hls->nb_varstreams; j++) {
                temp_vs = &(hls->var_streams[j]);
                if (!temp_vs->has_video && !temp_vs->has_subtitle &&
                    temp_vs->agroup &&
                    !av_strcasecmp(temp_vs->agroup, vs->agroup)) {
                    if (!aud_st)
                        aud_st = temp_vs->streams[0];
                    if (temp_vs->streams[0]->codecpar->bit_rate >
                            aud_st->codecpar->bit_rate)
                        aud_st = temp_vs->streams[0];
                }
            }
        }

        bandwidth = 0;
        if (vid_st)
            bandwidth += get_stream_bit_rate(vid_st);
        if (aud_st)
            bandwidth += get_stream_bit_rate(aud_st);
        bandwidth += bandwidth / 10;

        ccgroup = NULL;
        if (vid_st && vs->ccgroup) {
            /* check if this group name is available in the cc map string */
            for (j = 0; j < hls->nb_ccstreams; j++) {
                ccs = &(hls->cc_streams[j]);
                if (!av_strcasecmp(ccs->ccgroup, vs->ccgroup)) {
                    ccgroup = vs->ccgroup;
                    break;
                }
            }
            if (j == hls->nb_ccstreams)
                av_log(NULL, AV_LOG_WARNING, "mapping ccgroup %s not found\n",
                        vs->ccgroup);
        }

        if (!hls->has_default_key || !hls->has_video_m3u8) {
            ff_hls_write_stream_info(vid_st, hls->m3u8_out, bandwidth, m3u8_rel_name,
                    aud_st ? vs->agroup : NULL, vs->codec_attr, ccgroup);
        } else {
            if (vid_st) {
                ff_hls_write_stream_info(vid_st, hls->m3u8_out, bandwidth, m3u8_rel_name,
                                         aud_st ? vs->agroup : NULL, vs->codec_attr, ccgroup);
            }
        }

        av_freep(&m3u8_rel_name);
    }
fail:
    if(ret >=0)
        hls->master_m3u8_created = 1;
    av_freep(&m3u8_rel_name);
    hlsenc_io_close(s, &hls->m3u8_out, hls->master_m3u8_url);
    return ret;
}

static int hls_window(AVFormatContext *s, int last, VariantStream *vs)
{
    HLSContext *hls = s->priv_data;
    HLSSegment *en;
    int target_duration = 0;
    int ret = 0;
    char temp_filename[1024];
    int64_t sequence = FFMAX(hls->start_sequence, vs->sequence - vs->nb_entries);
    const char *proto = avio_find_protocol_name(vs->m3u8_name);
    int is_file_proto = proto && !strcmp(proto, "file");
    int use_temp_file = is_file_proto && ((hls->flags & HLS_TEMP_FILE) || !(hls->pl_type == PLAYLIST_TYPE_VOD));
    static unsigned warned_non_file;
    char *key_uri = NULL;
    char *iv_string = NULL;
    AVDictionary *options = NULL;
    double prog_date_time = vs->initial_prog_date_time;
    double *prog_date_time_p = (hls->flags & HLS_PROGRAM_DATE_TIME) ? &prog_date_time : NULL;
    int byterange_mode = (hls->flags & HLS_SINGLE_FILE) || (hls->max_seg_size > 0);

    hls->version = 3;
    if (byterange_mode) {
        hls->version = 4;
        sequence = 0;
    }

    if (hls->flags & HLS_INDEPENDENT_SEGMENTS) {
        hls->version = 6;
    }

    if (hls->segment_type == SEGMENT_TYPE_FMP4) {
        hls->version = 7;
    }

    if (!is_file_proto && (hls->flags & HLS_TEMP_FILE) && !warned_non_file++)
        av_log(s, AV_LOG_ERROR, "Cannot use rename on non file protocol, this may lead to races and temporary partial files\n");

    set_http_options(s, &options, hls);
    snprintf(temp_filename, sizeof(temp_filename), use_temp_file ? "%s.tmp" : "%s", vs->m3u8_name);
    if ((ret = hlsenc_io_open(s, &hls->m3u8_out, temp_filename, &options)) < 0) {
        if (hls->ignore_io_errors)
            ret = 0;
        goto fail;
    }

    for (en = vs->segments; en; en = en->next) {
        if (target_duration <= en->duration)
            target_duration = lrint(en->duration);
    }

    vs->discontinuity_set = 0;
    ff_hls_write_playlist_header(hls->m3u8_out, hls->version, hls->allowcache,
                                 target_duration, sequence, hls->pl_type);

    if((hls->flags & HLS_DISCONT_START) && sequence==hls->start_sequence && vs->discontinuity_set==0 ){
        avio_printf(hls->m3u8_out, "#EXT-X-DISCONTINUITY\n");
        vs->discontinuity_set = 1;
    }
    if (vs->has_video && (hls->flags & HLS_INDEPENDENT_SEGMENTS)) {
        avio_printf(hls->m3u8_out, "#EXT-X-INDEPENDENT-SEGMENTS\n");
    }
    for (en = vs->segments; en; en = en->next) {
        if ((hls->encrypt || hls->key_info_file) && (!key_uri || strcmp(en->key_uri, key_uri) ||
                                    av_strcasecmp(en->iv_string, iv_string))) {
            avio_printf(hls->m3u8_out, "#EXT-X-KEY:METHOD=AES-128,URI=\"%s\"", en->key_uri);
            if (*en->iv_string)
                avio_printf(hls->m3u8_out, ",IV=0x%s", en->iv_string);
            avio_printf(hls->m3u8_out, "\n");
            key_uri = en->key_uri;
            iv_string = en->iv_string;
        }

        if ((hls->segment_type == SEGMENT_TYPE_FMP4) && (en == vs->segments)) {
            ff_hls_write_init_file(hls->m3u8_out, (hls->flags & HLS_SINGLE_FILE) ? en->filename : vs->fmp4_init_filename,
                                   hls->flags & HLS_SINGLE_FILE, vs->init_range_length, 0);
        }

        ret = ff_hls_write_file_entry(hls->m3u8_out, en->discont, byterange_mode,
                                      en->duration, hls->flags & HLS_ROUND_DURATIONS,
                                      en->size, en->pos, vs->baseurl,
                                      en->filename, prog_date_time_p);
        if (ret < 0) {
            av_log(s, AV_LOG_WARNING, "ff_hls_write_file_entry get error\n");
        }
    }

    if (last && (hls->flags & HLS_OMIT_ENDLIST)==0)
        ff_hls_write_end_list(hls->m3u8_out);

    if( vs->vtt_m3u8_name ) {
        if ((ret = hlsenc_io_open(s, &hls->sub_m3u8_out, vs->vtt_m3u8_name, &options)) < 0) {
            if (hls->ignore_io_errors)
                ret = 0;
            goto fail;
        }
        ff_hls_write_playlist_header(hls->sub_m3u8_out, hls->version, hls->allowcache,
                                     target_duration, sequence, PLAYLIST_TYPE_NONE);
        for (en = vs->segments; en; en = en->next) {
            ret = ff_hls_write_file_entry(hls->sub_m3u8_out, 0, byterange_mode,
                                          en->duration, 0, en->size, en->pos,
                                          vs->baseurl, en->sub_filename, NULL);
            if (ret < 0) {
                av_log(s, AV_LOG_WARNING, "ff_hls_write_file_entry get error\n");
            }
        }

        if (last)
            ff_hls_write_end_list(hls->sub_m3u8_out);

    }

fail:
    av_dict_free(&options);
    hlsenc_io_close(s, &hls->m3u8_out, temp_filename);
    hlsenc_io_close(s, &hls->sub_m3u8_out, vs->vtt_m3u8_name);
    if (use_temp_file)
        ff_rename(temp_filename, vs->m3u8_name, s);
    if (ret >= 0 && hls->master_pl_name)
        if (create_master_playlist(s, vs) < 0)
            av_log(s, AV_LOG_WARNING, "Master playlist creation failed\n");

    return ret;
}

static int hls_start(AVFormatContext *s, VariantStream *vs)
{
    HLSContext *c = s->priv_data;
    AVFormatContext *oc = vs->avf;
    AVFormatContext *vtt_oc = vs->vtt_avf;
    AVDictionary *options = NULL;
    const char *proto = NULL;
    int use_temp_file = 0;
    char *filename, iv_string[KEYSIZE*2 + 1];
    int err = 0;

    if (c->flags & HLS_SINGLE_FILE) {
        char *new_name = av_strdup(vs->basename);
        if (!new_name)
            return AVERROR(ENOMEM);
        ff_format_set_url(oc, new_name);
        if (vs->vtt_basename) {
            new_name = av_strdup(vs->vtt_basename);
            if (!new_name)
                return AVERROR(ENOMEM);
            ff_format_set_url(vtt_oc, new_name);
        }
    } else if (c->max_seg_size > 0) {
        char *filename = NULL;
        if (replace_int_data_in_filename(&filename,
#if FF_API_HLS_WRAP
            vs->basename, 'd', c->wrap ? vs->sequence % c->wrap : vs->sequence) < 1) {
#else
            vs->basename, 'd', vs->sequence) < 1) {
#endif
                av_free(filename);
                av_log(oc, AV_LOG_ERROR, "Invalid segment filename template '%s', you can try to use -strftime 1 with it\n", vs->basename);
                return AVERROR(EINVAL);
        }
        ff_format_set_url(oc, filename);
    } else {
        if (c->use_localtime) {
            time_t now0;
            struct tm *tm, tmpbuf;
            int bufsize = strlen(vs->basename) + 1024;
            char *buf = av_mallocz(bufsize);
            if (!buf)
                return AVERROR(ENOMEM);
            time(&now0);
            tm = localtime_r(&now0, &tmpbuf);
            ff_format_set_url(oc, buf);
            if (!strftime(oc->url, bufsize, vs->basename, tm)) {
                av_log(oc, AV_LOG_ERROR, "Could not get segment filename with strftime\n");
                return AVERROR(EINVAL);
            }

            err = sls_flag_use_localtime_filename(oc, c, vs);
            if (err < 0) {
                return AVERROR(ENOMEM);
            }

            if (c->use_localtime_mkdir) {
                const char *dir;
                char *fn_copy = av_strdup(oc->url);
                if (!fn_copy) {
                    return AVERROR(ENOMEM);
                }
                dir = av_dirname(fn_copy);
                if (ff_mkdir_p(dir) == -1 && errno != EEXIST) {
                    av_log(oc, AV_LOG_ERROR, "Could not create directory %s with use_localtime_mkdir\n", dir);
                    av_free(fn_copy);
                    return AVERROR(errno);
                }
                av_free(fn_copy);
            }
        } else {
            char *filename = NULL;
            if (replace_int_data_in_filename(&filename,
#if FF_API_HLS_WRAP
                   vs->basename, 'd', c->wrap ? vs->sequence % c->wrap : vs->sequence) < 1) {
#else
                   vs->basename, 'd', vs->sequence) < 1) {
#endif
                av_free(filename);
                av_log(oc, AV_LOG_ERROR, "Invalid segment filename template '%s' you can try to use -strftime 1 with it\n", vs->basename);
                return AVERROR(EINVAL);
            }
            ff_format_set_url(oc, filename);
        }
        if( vs->vtt_basename) {
            char *filename = NULL;
            if (replace_int_data_in_filename(&filename,
#if FF_API_HLS_WRAP
                vs->vtt_basename, 'd', c->wrap ? vs->sequence % c->wrap : vs->sequence) < 1) {
#else
                vs->vtt_basename, 'd', vs->sequence) < 1) {
#endif
                av_free(filename);
                av_log(vtt_oc, AV_LOG_ERROR, "Invalid segment filename template '%s'\n", vs->vtt_basename);
                return AVERROR(EINVAL);
            }
            ff_format_set_url(vtt_oc, filename);
       }
    }
    vs->number++;

    set_http_options(s, &options, c);

    proto = avio_find_protocol_name(oc->url);
    use_temp_file = proto && !strcmp(proto, "file") && (c->flags & HLS_TEMP_FILE);

    if (use_temp_file) {
        char *new_name = av_asprintf("%s.tmp", oc->url);
        if (!new_name)
            return AVERROR(ENOMEM);
        ff_format_set_url(oc, new_name);
    }

    if (c->key_info_file || c->encrypt) {
        if (c->segment_type == SEGMENT_TYPE_FMP4) {
            av_log(s, AV_LOG_ERROR, "Encrypted fmp4 not yet supported\n");
            return AVERROR_PATCHWELCOME;
        }

        if (c->key_info_file && c->encrypt) {
            av_log(s, AV_LOG_WARNING, "Cannot use both -hls_key_info_file and -hls_enc,"
                  " ignoring -hls_enc\n");
        }

        if (!c->encrypt_started || (c->flags & HLS_PERIODIC_REKEY)) {
            if (c->key_info_file) {
                if ((err = hls_encryption_start(s)) < 0)
                    goto fail;
            } else {
                if ((err = do_encrypt(s, vs)) < 0)
                    goto fail;
            }
            c->encrypt_started = 1;
        }
        if ((err = av_dict_set(&options, "encryption_key", c->key_string, 0))
                < 0)
            goto fail;
        err = av_strlcpy(iv_string, c->iv_string, sizeof(iv_string));
        if (!err)
            snprintf(iv_string, sizeof(iv_string), "%032"PRIx64, vs->sequence);
        if ((err = av_dict_set(&options, "encryption_iv", iv_string, 0)) < 0)
           goto fail;

        filename = av_asprintf("crypto:%s", oc->url);
        if (!filename) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        err = hlsenc_io_open(s, &oc->pb, filename, &options);
        av_free(filename);
        av_dict_free(&options);
        if (err < 0)
            return err;
    } else if (c->segment_type != SEGMENT_TYPE_FMP4) {
        if ((err = hlsenc_io_open(s, &oc->pb, oc->url, &options)) < 0) {
            if (c->ignore_io_errors)
                err = 0;
            goto fail;
        }
    }
    if (vs->vtt_basename) {
        set_http_options(s, &options, c);
        if ((err = hlsenc_io_open(s, &vtt_oc->pb, vtt_oc->url, &options)) < 0) {
            if (c->ignore_io_errors)
                err = 0;
            goto fail;
        }
    }
    av_dict_free(&options);

    if (c->segment_type != SEGMENT_TYPE_FMP4) {
        /* We only require one PAT/PMT per segment. */
        if (oc->oformat->priv_class && oc->priv_data) {
            char period[21];

            snprintf(period, sizeof(period), "%d", (INT_MAX / 2) - 1);

            av_opt_set(oc->priv_data, "mpegts_flags", "resend_headers", 0);
            av_opt_set(oc->priv_data, "sdt_period", period, 0);
            av_opt_set(oc->priv_data, "pat_period", period, 0);
        }
    }

    if (vs->vtt_basename) {
        err = avformat_write_header(vtt_oc,NULL);
        if (err < 0)
            return err;
    }

    return 0;
fail:
    av_dict_free(&options);

    return err;
}

static const char * get_default_pattern_localtime_fmt(AVFormatContext *s)
{
    char b[21];
    time_t t = time(NULL);
    struct tm *p, tmbuf;
    HLSContext *hls = s->priv_data;

    p = localtime_r(&t, &tmbuf);
    // no %s support when strftime returned error or left format string unchanged
    // also no %s support on MSVC, which invokes the invalid parameter handler on unsupported format strings, instead of returning an error
    if (hls->segment_type == SEGMENT_TYPE_FMP4) {
        return (HAVE_LIBC_MSVCRT || !strftime(b, sizeof(b), "%s", p) || !strcmp(b, "%s")) ? "-%Y%m%d%H%M%S.m4s" : "-%s.m4s";
    }
    return (HAVE_LIBC_MSVCRT || !strftime(b, sizeof(b), "%s", p) || !strcmp(b, "%s")) ? "-%Y%m%d%H%M%S.ts" : "-%s.ts";
}

static int append_postfix(char *name, int name_buf_len, int i)
{
    char *p;
    char extension[10] = {'\0'};

    p = strrchr(name, '.');
    if (p) {
        av_strlcpy(extension, p, sizeof(extension));
        *p = '\0';
    }

    snprintf(name + strlen(name), name_buf_len - strlen(name), POSTFIX_PATTERN, i);

    if (strlen(extension))
        av_strlcat(name, extension, name_buf_len);

    return 0;
}

static int validate_name(int nb_vs, const char *fn)
{
    const char *filename, *subdir_name;
    char *fn_dup = NULL;
    int ret = 0;

    if (!fn) {
        ret = AVERROR(EINVAL);
        goto fail;
    }

    fn_dup = av_strdup(fn);
    if (!fn_dup) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    filename = av_basename(fn);
    subdir_name = av_dirname(fn_dup);

    if (nb_vs > 1 && !av_stristr(filename, "%v") && !av_stristr(subdir_name, "%v")) {
        av_log(NULL, AV_LOG_ERROR, "More than 1 variant streams are present, %%v is expected in the filename %s\n",
                fn);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (av_stristr(filename, "%v") && av_stristr(subdir_name, "%v")) {
        av_log(NULL, AV_LOG_ERROR, "%%v is expected either in filename or in the sub-directory name of file %s\n",
                fn);
        ret = AVERROR(EINVAL);
        goto fail;
    }

fail:
    av_freep(&fn_dup);
    return ret;
}

static int format_name(char *buf, int buf_len, int index)
{
    const char *proto, *dir;
    char *orig_buf_dup = NULL, *mod_buf = NULL, *mod_buf_dup = NULL;
    int ret = 0;

    if (!av_stristr(buf, "%v"))
        return ret;

    orig_buf_dup = av_strdup(buf);
    if (!orig_buf_dup) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (replace_int_data_in_filename(&mod_buf, orig_buf_dup, 'v', index) < 1) {
        ret = AVERROR(EINVAL);
        goto fail;
    }
    av_strlcpy(buf, mod_buf, buf_len);

    proto = avio_find_protocol_name(orig_buf_dup);
    dir = av_dirname(orig_buf_dup);

    /* if %v is present in the file's directory, create sub-directory */
    if (av_stristr(dir, "%v") && proto && !strcmp(proto, "file")) {
        mod_buf_dup = av_strdup(buf);
        if (!mod_buf_dup) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        dir = av_dirname(mod_buf_dup);
        if (ff_mkdir_p(dir) == -1 && errno != EEXIST) {
            ret = AVERROR(errno);
            goto fail;
        }
    }

fail:
    av_freep(&orig_buf_dup);
    av_freep(&mod_buf_dup);
    av_freep(&mod_buf);
    return ret;
}

static int get_nth_codec_stream_index(AVFormatContext *s,
                                      enum AVMediaType codec_type,
                                      int stream_id)
{
    unsigned int stream_index, cnt;
    if (stream_id < 0 || stream_id > s->nb_streams - 1)
        return -1;
    cnt = 0;
    for (stream_index = 0; stream_index < s->nb_streams; stream_index++) {
        if (s->streams[stream_index]->codecpar->codec_type != codec_type)
            continue;
        if (cnt == stream_id)
            return stream_index;
        cnt++;
    }
    return -1;
}

static int parse_variant_stream_mapstring(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    VariantStream *vs;
    int stream_index;
    enum AVMediaType codec_type;
    int nb_varstreams, nb_streams;
    char *p, *q, *saveptr1, *saveptr2, *varstr, *keyval;
    const char *val;

    /**
     * Expected format for var_stream_map string is as below:
     * "a:0,v:0 a:1,v:1"
     * "a:0,agroup:a0,default:1,language:ENG a:1,agroup:a1,defalut:0 v:0,agroup:a0  v:1,agroup:a1"
     * This string specifies how to group the audio, video and subtitle streams
     * into different variant streams. The variant stream groups are separated
     * by space.
     *
     * a:, v:, s: are keys to specify audio, video and subtitle streams
     * respectively. Allowed values are 0 to 9 digits (limited just based on
     * practical usage)
     *
     * agroup: is key to specify audio group. A string can be given as value.
     */
    p = av_strdup(hls->var_stream_map);
    q = p;
    while(av_strtok(q, " \t", &saveptr1)) {
        q = NULL;
        hls->nb_varstreams++;
    }
    av_freep(&p);

    hls->var_streams = av_mallocz(sizeof(*hls->var_streams) * hls->nb_varstreams);
    if (!hls->var_streams)
        return AVERROR(ENOMEM);

    p = hls->var_stream_map;
    nb_varstreams = 0;
    while (varstr = av_strtok(p, " \t", &saveptr1)) {
        p = NULL;

        if (nb_varstreams < hls->nb_varstreams) {
            vs = &(hls->var_streams[nb_varstreams]);
            vs->var_stream_idx = nb_varstreams;
            vs->is_default = 0;
            nb_varstreams++;
        } else
            return AVERROR(EINVAL);

        q = varstr;
        while (q < varstr + strlen(varstr)) {
            if (!av_strncasecmp(q, "a:", 2) || !av_strncasecmp(q, "v:", 2) ||
                !av_strncasecmp(q, "s:", 2))
                vs->nb_streams++;
            q++;
        }
        vs->streams = av_mallocz(sizeof(AVStream *) * vs->nb_streams);
        if (!vs->streams)
            return AVERROR(ENOMEM);

        nb_streams = 0;
        while (keyval = av_strtok(varstr, ",", &saveptr2)) {
            varstr = NULL;
            if (av_strstart(keyval, "language:", &val)) {
                vs->language = av_strdup(val);
                if (!vs->language)
                    return AVERROR(ENOMEM);
                continue;
            } else if (av_strstart(keyval, "default:", &val)) {
                vs->is_default = (!av_strncasecmp(val, "YES", strlen("YES")) ||
                                  (!av_strncasecmp(val, "1", strlen("1"))));
                hls->has_default_key = 1;
                continue;
            } else if (av_strstart(keyval, "agroup:", &val)) {
                vs->agroup = av_strdup(val);
                if (!vs->agroup)
                    return AVERROR(ENOMEM);
                continue;
            } else if (av_strstart(keyval, "ccgroup:", &val)) {
                vs->ccgroup = av_strdup(val);
                if (!vs->ccgroup)
                    return AVERROR(ENOMEM);
                continue;
            } else if (av_strstart(keyval, "v:", &val)) {
                codec_type = AVMEDIA_TYPE_VIDEO;
                hls->has_video_m3u8 = 1;
            } else if (av_strstart(keyval, "a:", &val)) {
                codec_type = AVMEDIA_TYPE_AUDIO;
            } else if (av_strstart(keyval, "s:", &val)) {
                codec_type = AVMEDIA_TYPE_SUBTITLE;
            } else {
                av_log(s, AV_LOG_ERROR, "Invalid keyval %s\n", keyval);
                return AVERROR(EINVAL);
            }

            stream_index = -1;
            if (av_isdigit(*val))
                stream_index = get_nth_codec_stream_index (s, codec_type,
                                                           atoi(val));

            if (stream_index >= 0 && nb_streams < vs->nb_streams) {
                vs->streams[nb_streams++] = s->streams[stream_index];
            } else {
                av_log(s, AV_LOG_ERROR, "Unable to map stream at %s\n", keyval);
                return AVERROR(EINVAL);
            }
        }
    }
    av_log(s, AV_LOG_DEBUG, "Number of variant streams %d\n",
            hls->nb_varstreams);

    return 0;
}

static int parse_cc_stream_mapstring(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    int nb_ccstreams;
    char *p, *q, *ccstr, *keyval;
    char *saveptr1 = NULL, *saveptr2 = NULL;
    const char *val;
    ClosedCaptionsStream *ccs;

    p = av_strdup(hls->cc_stream_map);
    q = p;
    while(av_strtok(q, " \t", &saveptr1)) {
        q = NULL;
        hls->nb_ccstreams++;
    }
    av_freep(&p);

    hls->cc_streams = av_mallocz(sizeof(*hls->cc_streams) * hls->nb_ccstreams);
    if (!hls->cc_streams)
        return AVERROR(ENOMEM);

    p = hls->cc_stream_map;
    nb_ccstreams = 0;
    while (ccstr = av_strtok(p, " \t", &saveptr1)) {
        p = NULL;

        if (nb_ccstreams < hls->nb_ccstreams)
            ccs = &(hls->cc_streams[nb_ccstreams++]);
        else
            return AVERROR(EINVAL);

        while (keyval = av_strtok(ccstr, ",", &saveptr2)) {
            ccstr = NULL;

            if (av_strstart(keyval, "ccgroup:", &val)) {
                ccs->ccgroup = av_strdup(val);
                if (!ccs->ccgroup)
                    return AVERROR(ENOMEM);
            } else if (av_strstart(keyval, "instreamid:", &val)) {
                ccs->instreamid = av_strdup(val);
                if (!ccs->instreamid)
                    return AVERROR(ENOMEM);
            } else if (av_strstart(keyval, "language:", &val)) {
                ccs->language = av_strdup(val);
                if (!ccs->language)
                    return AVERROR(ENOMEM);
            } else {
                av_log(s, AV_LOG_ERROR, "Invalid keyval %s\n", keyval);
                return AVERROR(EINVAL);
            }
        }

        if (!ccs->ccgroup || !ccs->instreamid) {
            av_log(s, AV_LOG_ERROR, "Insufficient parameters in cc stream map string\n");
            return AVERROR(EINVAL);
        }

        if (av_strstart(ccs->instreamid, "CC", &val)) {
            if(atoi(val) < 1 || atoi(val) > 4) {
                av_log(s, AV_LOG_ERROR, "Invalid instream ID CC index %d in %s, range 1-4\n",
                       atoi(val), ccs->instreamid);
                return AVERROR(EINVAL);
            }
        } else if (av_strstart(ccs->instreamid, "SERVICE", &val)) {
            if(atoi(val) < 1 || atoi(val) > 63) {
                av_log(s, AV_LOG_ERROR, "Invalid instream ID SERVICE index %d in %s, range 1-63 \n",
                       atoi(val), ccs->instreamid);
                return AVERROR(EINVAL);
            }
        } else {
            av_log(s, AV_LOG_ERROR, "Invalid instream ID %s, supported are CCn or SERIVICEn\n",
                   ccs->instreamid);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static int update_variant_stream_info(AVFormatContext *s) {
    HLSContext *hls = s->priv_data;
    unsigned int i;
    int ret = 0;

    if (hls->cc_stream_map) {
        ret = parse_cc_stream_mapstring(s);
        if (ret < 0)
            return ret;
    }

    if (hls->var_stream_map) {
        return parse_variant_stream_mapstring(s);
    } else {
        //By default, a single variant stream with all the codec streams is created
        hls->nb_varstreams = 1;
        hls->var_streams = av_mallocz(sizeof(*hls->var_streams) *
                                             hls->nb_varstreams);
        if (!hls->var_streams)
            return AVERROR(ENOMEM);

        hls->var_streams[0].var_stream_idx = 0;
        hls->var_streams[0].nb_streams = s->nb_streams;
        hls->var_streams[0].streams = av_mallocz(sizeof(AVStream *) *
                                            hls->var_streams[0].nb_streams);
        if (!hls->var_streams[0].streams)
            return AVERROR(ENOMEM);

        //by default, the first available ccgroup is mapped to the variant stream
        if (hls->nb_ccstreams) {
            hls->var_streams[0].ccgroup = av_strdup(hls->cc_streams[0].ccgroup);
            if (!hls->var_streams[0].ccgroup)
                return AVERROR(ENOMEM);
        }

        for (i = 0; i < s->nb_streams; i++)
            hls->var_streams[0].streams[i] = s->streams[i];
    }
    return 0;
}

static int update_master_pl_info(AVFormatContext *s) {
    HLSContext *hls = s->priv_data;
    const char *dir;
    char *fn1= NULL, *fn2 = NULL;
    int ret = 0;

    fn1 = av_strdup(s->url);
    if (!fn1) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    dir = av_dirname(fn1);

    /**
     * if output file's directory has %v, variants are created in sub-directories
     * then master is created at the sub-directories level
     */
    if (dir && av_stristr(av_basename(dir), "%v")) {
        fn2 = av_strdup(dir);
        if (!fn2) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        dir = av_dirname(fn2);
    }

    if (dir && strcmp(dir, "."))
        hls->master_m3u8_url = av_append_path_component(dir, hls->master_pl_name);
    else
        hls->master_m3u8_url = av_strdup(hls->master_pl_name);

    if (!hls->master_m3u8_url) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

fail:
    av_freep(&fn1);
    av_freep(&fn2);

    return ret;
}

static int hls_write_header(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    int ret, i, j;
    VariantStream *vs = NULL;

    for (i = 0; i < hls->nb_varstreams; i++) {
        vs = &hls->var_streams[i];

        ret = avformat_write_header(vs->avf, NULL);
        if (ret < 0)
            return ret;
        //av_assert0(s->nb_streams == hls->avf->nb_streams);
        for (j = 0; j < vs->nb_streams; j++) {
            AVStream *inner_st;
            AVStream *outer_st = vs->streams[j];

            if (hls->max_seg_size > 0) {
                if ((outer_st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
                    (outer_st->codecpar->bit_rate > hls->max_seg_size)) {
                    av_log(s, AV_LOG_WARNING, "Your video bitrate is bigger than hls_segment_size, "
                           "(%"PRId64 " > %"PRId64 "), the result maybe not be what you want.",
                           outer_st->codecpar->bit_rate, hls->max_seg_size);
                }
            }

            if (outer_st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
                inner_st = vs->avf->streams[j];
            else if (vs->vtt_avf)
                inner_st = vs->vtt_avf->streams[0];
            else {
                /* We have a subtitle stream, when the user does not want one */
                inner_st = NULL;
                continue;
            }
            avpriv_set_pts_info(outer_st, inner_st->pts_wrap_bits, inner_st->time_base.num, inner_st->time_base.den);
            write_codec_attr(outer_st, vs);

        }
        /* Update the Codec Attr string for the mapped audio groups */
        if (vs->has_video && vs->agroup) {
            for (j = 0; j < hls->nb_varstreams; j++) {
                VariantStream *vs_agroup = &(hls->var_streams[j]);
                if (!vs_agroup->has_video && !vs_agroup->has_subtitle &&
                    vs_agroup->agroup &&
                    !av_strcasecmp(vs_agroup->agroup, vs->agroup)) {
                    write_codec_attr(vs_agroup->streams[0], vs);
                }
            }
        }
    }

    return ret;
}

static int hls_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc = NULL;
    AVStream *st = s->streams[pkt->stream_index];
    int64_t end_pts = 0;
    int is_ref_pkt = 1;
    int ret = 0, can_split = 1, i, j;
    int stream_index = 0;
    int range_length = 0;
    const char *proto = NULL;
    int use_temp_file = 0;
    uint8_t *buffer = NULL;
    VariantStream *vs = NULL;
    AVDictionary *options = NULL;
    char *old_filename = NULL;

    for (i = 0; i < hls->nb_varstreams; i++) {
        vs = &hls->var_streams[i];
        for (j = 0; j < vs->nb_streams; j++) {
            if (vs->streams[j] == st) {
                if( st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE ) {
                    oc = vs->vtt_avf;
                    stream_index = 0;
                } else {
                    oc = vs->avf;
                    stream_index = j;
                }
                break;
            }
        }

        if (oc)
            break;
    }

    if (!oc) {
        av_log(s, AV_LOG_ERROR, "Unable to find mapping variant stream\n");
        return AVERROR(ENOMEM);
    }

    end_pts = hls->recording_time * vs->number;

    if (vs->sequence - vs->nb_entries > hls->start_sequence && hls->init_time > 0) {
        /* reset end_pts, hls->recording_time at end of the init hls list */
        int init_list_dur = hls->init_time * vs->nb_entries * AV_TIME_BASE;
        int after_init_list_dur = (vs->sequence - hls->start_sequence - vs->nb_entries ) * (hls->time * AV_TIME_BASE);
        hls->recording_time = hls->time * AV_TIME_BASE;
        end_pts = init_list_dur + after_init_list_dur ;
    }

    if (vs->start_pts == AV_NOPTS_VALUE) {
        vs->start_pts = pkt->pts;
    }

    if (vs->has_video) {
        can_split = st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    ((pkt->flags & AV_PKT_FLAG_KEY) || (hls->flags & HLS_SPLIT_BY_TIME));
        is_ref_pkt = (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) && (pkt->stream_index == vs->reference_stream_index);
    }
    if (pkt->pts == AV_NOPTS_VALUE)
        is_ref_pkt = can_split = 0;

    if (is_ref_pkt) {
        if (vs->end_pts == AV_NOPTS_VALUE)
            vs->end_pts = pkt->pts;
        if (vs->new_start) {
            vs->new_start = 0;
            vs->duration = (double)(pkt->pts - vs->end_pts)
                                       * st->time_base.num / st->time_base.den;
            vs->dpp = (double)(pkt->duration) * st->time_base.num / st->time_base.den;
        } else {
            if (pkt->duration) {
                vs->duration += (double)(pkt->duration) * st->time_base.num / st->time_base.den;
            } else {
                av_log(s, AV_LOG_WARNING, "pkt->duration = 0, maybe the hls segment duration will not precise\n");
                vs->duration = (double)(pkt->pts - vs->end_pts) * st->time_base.num / st->time_base.den;
            }
        }

    }

    if (vs->packets_written && can_split && av_compare_ts(pkt->pts - vs->start_pts, st->time_base,
                                                          end_pts, AV_TIME_BASE_Q) >= 0) {
        int64_t new_start_pos;
        int byterange_mode = (hls->flags & HLS_SINGLE_FILE) || (hls->max_seg_size > 0);

        av_write_frame(vs->avf, NULL); /* Flush any buffered data */

        new_start_pos = avio_tell(vs->avf->pb);
        if (hls->segment_type != SEGMENT_TYPE_FMP4) {
            vs->size = new_start_pos - vs->start_pos;
        } else {
            vs->size = new_start_pos;
        }

        if (hls->segment_type == SEGMENT_TYPE_FMP4) {
            if (!vs->init_range_length) {
                avio_flush(oc->pb);
                range_length = avio_close_dyn_buf(oc->pb, &buffer);
                avio_write(vs->out, buffer, range_length);
                av_free(buffer);
                vs->init_range_length = range_length;
                avio_open_dyn_buf(&oc->pb);
                vs->packets_written = 0;
                vs->start_pos = range_length;
                if (!byterange_mode) {
                    ff_format_io_close(s, &vs->out);
                    hlsenc_io_close(s, &vs->out, vs->base_output_dirname);
                }
            }
        } else {
            if (!byterange_mode) {
                hlsenc_io_close(s, &oc->pb, oc->url);
            }
        }
        if (!byterange_mode) {
            if (vs->vtt_avf) {
                hlsenc_io_close(s, &vs->vtt_avf->pb, vs->vtt_avf->url);
            }
        }

        if (oc->url[0]) {
            proto = avio_find_protocol_name(oc->url);
            use_temp_file = proto && !strcmp(proto, "file") && (hls->flags & HLS_TEMP_FILE);
        }

        // look to rename the asset name
        if (use_temp_file) {
            if (!(hls->flags & HLS_SINGLE_FILE) || (hls->max_seg_size <= 0))
                if ((vs->avf->oformat->priv_class && vs->avf->priv_data) && hls->segment_type != SEGMENT_TYPE_FMP4)
                    av_opt_set(vs->avf->priv_data, "mpegts_flags", "resend_headers", 0);
        }

        if (hls->segment_type == SEGMENT_TYPE_FMP4) {
            if (hls->flags & HLS_SINGLE_FILE) {
                ret = flush_dynbuf(vs, &range_length);
                if (ret < 0) {
                    return ret;
                }
                vs->size = range_length;
            } else {
                set_http_options(s, &options, hls);
                ret = hlsenc_io_open(s, &vs->out, vs->avf->url, &options);
                if (ret < 0) {
                    av_log(s, hls->ignore_io_errors ? AV_LOG_WARNING : AV_LOG_ERROR,
                           "Failed to open file '%s'\n", vs->avf->url);
                    return hls->ignore_io_errors ? 0 : ret;
                }
                write_styp(vs->out);
                ret = flush_dynbuf(vs, &range_length);
                if (ret < 0) {
                    return ret;
                }
                ff_format_io_close(s, &vs->out);
            }
        }

        if (use_temp_file && !(hls->flags & HLS_SINGLE_FILE)) {
            hls_rename_temp_file(s, oc);
        }

        old_filename = av_strdup(vs->avf->url);
        if (!old_filename) {
            return AVERROR(ENOMEM);
        }

        if (vs->start_pos || hls->segment_type != SEGMENT_TYPE_FMP4) {
            ret = hls_append_segment(s, hls, vs, vs->duration, vs->start_pos, vs->size);
            vs->end_pts = pkt->pts;
            vs->duration = 0;
            if (ret < 0) {
                av_free(old_filename);
                return ret;
            }
        }

        if (hls->segment_type != SEGMENT_TYPE_FMP4) {
            vs->start_pos = new_start_pos;
        } else {
            vs->start_pos += vs->size;
        }

        if (hls->flags & HLS_SINGLE_FILE) {
            vs->number++;
        } else if (hls->max_seg_size > 0) {
            if (vs->start_pos >= hls->max_seg_size) {
                vs->sequence++;
                sls_flag_file_rename(hls, vs, old_filename);
                ret = hls_start(s, vs);
                vs->start_pos = 0;
                /* When split segment by byte, the duration is short than hls_time,
                 * so it is not enough one segment duration as hls_time, */
                vs->number--;
            }
            vs->number++;
        } else {
            sls_flag_file_rename(hls, vs, old_filename);
            ret = hls_start(s, vs);
        }
        av_free(old_filename);

        if (ret < 0) {
            return ret;
        }

        // if we're building a VOD playlist, skip writing the manifest multiple times, and just wait until the end
        if (hls->pl_type != PLAYLIST_TYPE_VOD) {
            if ((ret = hls_window(s, 0, vs)) < 0) {
                return ret;
            }
        }
    }

    vs->packets_written++;
    if (oc->pb) {
        ret = ff_write_chained(oc, stream_index, pkt, s, 0);
        if (hls->ignore_io_errors)
            ret = 0;
    }

    return ret;
}

static void hls_free_variant_streams(struct HLSContext *hls)
{
    int i = 0;
    AVFormatContext *vtt_oc = NULL;
    VariantStream *vs = NULL;

    for (i = 0; i < hls->nb_varstreams; i++) {
        vs = &hls->var_streams[i];
        vtt_oc = vs->vtt_avf;

        av_freep(&vs->basename);
        av_freep(&vs->base_output_dirname);
        av_freep(&vs->fmp4_init_filename);
        if (vtt_oc) {
            av_freep(&vs->vtt_basename);
            av_freep(&vs->vtt_m3u8_name);
        }

        hls_free_segments(vs->segments);
        hls_free_segments(vs->old_segments);
        av_freep(&vs->m3u8_name);
        av_freep(&vs->streams);
        av_freep(&vs->agroup);
        av_freep(&vs->language);
        av_freep(&vs->ccgroup);
        av_freep(&vs->baseurl);
    }
}

static int hls_write_trailer(struct AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc = NULL;
    AVFormatContext *vtt_oc = NULL;
    char *old_filename = NULL;
    const char *proto = NULL;
    int use_temp_file = 0;
    int i;
    int ret = 0;
    VariantStream *vs = NULL;

    for (i = 0; i < hls->nb_varstreams; i++) {
        vs = &hls->var_streams[i];

        oc = vs->avf;
        vtt_oc = vs->vtt_avf;
        old_filename = av_strdup(vs->avf->url);
        use_temp_file = 0;

        if (!old_filename) {
            return AVERROR(ENOMEM);
        }
        if ( hls->segment_type == SEGMENT_TYPE_FMP4) {
            int range_length = 0;
            if (!vs->init_range_length) {
                uint8_t *buffer = NULL;
                int range_length, byterange_mode;
                av_write_frame(vs->avf, NULL); /* Flush any buffered data */
                avio_flush(oc->pb);

                range_length = avio_close_dyn_buf(oc->pb, &buffer);
                avio_write(vs->out, buffer, range_length);
                av_free(buffer);
                vs->init_range_length = range_length;
                avio_open_dyn_buf(&oc->pb);
                vs->packets_written = 0;
                vs->start_pos = range_length;
                byterange_mode = (hls->flags & HLS_SINGLE_FILE) || (hls->max_seg_size > 0);
                if (!byterange_mode) {
                    ff_format_io_close(s, &vs->out);
                    hlsenc_io_close(s, &vs->out, vs->base_output_dirname);
                }
            }
            if (!(hls->flags & HLS_SINGLE_FILE)) {
                ret = hlsenc_io_open(s, &vs->out, vs->avf->url, NULL);
                if (ret < 0) {
                    av_log(s, AV_LOG_ERROR, "Failed to open file '%s'\n", vs->avf->url);
                    goto failed;
                }
                write_styp(vs->out);
            }
            ret = flush_dynbuf(vs, &range_length);
            if (ret < 0) {
                goto failed;
            }
            vs->size = range_length;
            ff_format_io_close(s, &vs->out);
        }

failed:
        av_write_trailer(oc);

        if (oc->url[0]) {
            proto = avio_find_protocol_name(oc->url);
            use_temp_file = proto && !strcmp(proto, "file") && (hls->flags & HLS_TEMP_FILE);
        }

        if (oc->pb) {
            if (hls->segment_type != SEGMENT_TYPE_FMP4) {
                vs->size = avio_tell(vs->avf->pb) - vs->start_pos;
                ff_format_io_close(s, &oc->pb);
            }

            // rename that segment from .tmp to the real one
            if (use_temp_file && !(hls->flags & HLS_SINGLE_FILE)) {
                hls_rename_temp_file(s, oc);
                av_free(old_filename);
                old_filename = av_strdup(vs->avf->url);

                if (!old_filename) {
                    return AVERROR(ENOMEM);
                }
            }

            /* after av_write_trailer, then duration + 1 duration per packet */
            hls_append_segment(s, hls, vs, vs->duration + vs->dpp, vs->start_pos, vs->size);
        }

        sls_flag_file_rename(hls, vs, old_filename);

        if (vtt_oc) {
            if (vtt_oc->pb)
                av_write_trailer(vtt_oc);
            vs->size = avio_tell(vs->vtt_avf->pb) - vs->start_pos;
            ff_format_io_close(s, &vtt_oc->pb);
            avformat_free_context(vtt_oc);
        }
        avformat_free_context(oc);

        vs->avf = NULL;
        hls_window(s, 1, vs);
        av_free(old_filename);
    }

    hls_free_variant_streams(hls);

    for (i = 0; i < hls->nb_ccstreams; i++) {
        ClosedCaptionsStream *ccs = &hls->cc_streams[i];
        av_freep(&ccs->ccgroup);
        av_freep(&ccs->instreamid);
        av_freep(&ccs->language);
    }

    ff_format_io_close(s, &hls->m3u8_out);
    ff_format_io_close(s, &hls->sub_m3u8_out);
    av_freep(&hls->key_basename);
    av_freep(&hls->var_streams);
    av_freep(&hls->cc_streams);
    av_freep(&hls->master_m3u8_url);
    return 0;
}


static int hls_init(AVFormatContext *s)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    HLSContext *hls = s->priv_data;
    const char *pattern = "%d.ts";
    VariantStream *vs = NULL;
    int basename_size = 0;
    const char *pattern_localtime_fmt = get_default_pattern_localtime_fmt(s);
    const char *vtt_pattern = "%d.vtt";
    char *p = NULL;
    int vtt_basename_size = 0;
    int fmp4_init_filename_len = strlen(hls->fmp4_init_filename) + 1;

    hls->has_default_key = 0;
    hls->has_video_m3u8 = 0;
    ret = update_variant_stream_info(s);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Variant stream info update failed with status %x\n",
               ret);
        goto fail;
    }
    //TODO: Updates needed to encryption functionality with periodic re-key when more than one variant streams are present
    if (hls->nb_varstreams > 1 && hls->flags & HLS_PERIODIC_REKEY) {
        ret = AVERROR(EINVAL);
        av_log(s, AV_LOG_ERROR, "Periodic re-key not supported when more than one variant streams are present\n");
        goto fail;
    }

    ret = validate_name(hls->nb_varstreams, s->url);
    if (ret < 0)
        goto fail;

    if (hls->segment_filename) {
        ret = validate_name(hls->nb_varstreams, hls->segment_filename);
        if (ret < 0)
            goto fail;
    }

    if (av_strcasecmp(hls->fmp4_init_filename, "init.mp4")) {
        ret = validate_name(hls->nb_varstreams, hls->fmp4_init_filename);
        if (ret < 0)
            goto fail;
    }

    if (hls->subtitle_filename) {
        ret = validate_name(hls->nb_varstreams, hls->subtitle_filename);
        if (ret < 0)
            goto fail;
    }

    if (hls->master_pl_name) {
        ret = update_master_pl_info(s);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Master stream info update failed with status %x\n",
                   ret);
            goto fail;
        }
    }

    if (hls->segment_type == SEGMENT_TYPE_FMP4) {
        pattern = "%d.m4s";
    }
    if ((hls->start_sequence_source_type == HLS_START_SEQUENCE_AS_SECONDS_SINCE_EPOCH) ||
        (hls->start_sequence_source_type == HLS_START_SEQUENCE_AS_FORMATTED_DATETIME)) {
        time_t t = time(NULL); // we will need it in either case
        if (hls->start_sequence_source_type == HLS_START_SEQUENCE_AS_SECONDS_SINCE_EPOCH) {
            hls->start_sequence = (int64_t)t;
        } else if (hls->start_sequence_source_type == HLS_START_SEQUENCE_AS_FORMATTED_DATETIME) {
            char b[15];
            struct tm *p, tmbuf;
            if (!(p = localtime_r(&t, &tmbuf)))
                return AVERROR(ENOMEM);
            if (!strftime(b, sizeof(b), "%Y%m%d%H%M%S", p))
                return AVERROR(ENOMEM);
            hls->start_sequence = strtoll(b, NULL, 10);
        }
        av_log(hls, AV_LOG_DEBUG, "start_number evaluated to %"PRId64"\n", hls->start_sequence);
    }

    hls->recording_time = (hls->init_time ? hls->init_time : hls->time) * AV_TIME_BASE;
    for (i = 0; i < hls->nb_varstreams; i++) {
        vs = &hls->var_streams[i];

        vs->m3u8_name = av_strdup(s->url);
        if (!vs->m3u8_name ) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ret = format_name(vs->m3u8_name, strlen(s->url) + 1, i);
        if (ret < 0)
            goto fail;

        vs->sequence       = hls->start_sequence;
        vs->start_pts      = AV_NOPTS_VALUE;
        vs->end_pts      = AV_NOPTS_VALUE;
        vs->current_segment_final_filename_fmt[0] = '\0';

        if (hls->flags & HLS_SPLIT_BY_TIME && hls->flags & HLS_INDEPENDENT_SEGMENTS) {
            // Independent segments cannot be guaranteed when splitting by time
            hls->flags &= ~HLS_INDEPENDENT_SEGMENTS;
            av_log(s, AV_LOG_WARNING,
                   "'split_by_time' and 'independent_segments' cannot be enabled together. "
                   "Disabling 'independent_segments' flag\n");
        }

        if (hls->flags & HLS_PROGRAM_DATE_TIME) {
            time_t now0;
            time(&now0);
            vs->initial_prog_date_time = now0;
        }
        if (hls->format_options_str) {
            ret = av_dict_parse_string(&hls->format_options, hls->format_options_str, "=", ":", 0);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "Could not parse format options list '%s'\n", hls->format_options_str);
                goto fail;
            }
        }

        for (j = 0; j < vs->nb_streams; j++) {
            vs->has_video += vs->streams[j]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
            /* Get one video stream to reference for split segments
             * so use the first video stream index. */
            if ((vs->has_video == 1) && (vs->streams[j]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)) {
                vs->reference_stream_index = vs->streams[j]->index;
            }
            vs->has_subtitle += vs->streams[j]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE;
        }

        if (vs->has_video > 1)
            av_log(s, AV_LOG_WARNING, "More than a single video stream present, expect issues decoding it.\n");
        if (hls->segment_type == SEGMENT_TYPE_FMP4) {
            vs->oformat = av_guess_format("mp4", NULL, NULL);
        } else {
            vs->oformat = av_guess_format("mpegts", NULL, NULL);
        }

        if (!vs->oformat) {
            ret = AVERROR_MUXER_NOT_FOUND;
            goto fail;
        }

        if (vs->has_subtitle) {
            vs->vtt_oformat = av_guess_format("webvtt", NULL, NULL);
            if (!vs->oformat) {
                ret = AVERROR_MUXER_NOT_FOUND;
                goto fail;
            }
        }
        if (hls->segment_filename) {
            basename_size = strlen(hls->segment_filename) + 1;
            vs->basename = av_malloc(basename_size);
            if (!vs->basename) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            av_strlcpy(vs->basename, hls->segment_filename, basename_size);
            ret = format_name(vs->basename, basename_size, i);
            if (ret < 0)
                goto fail;
        } else {
            if (hls->flags & HLS_SINGLE_FILE) {
                if (hls->segment_type == SEGMENT_TYPE_FMP4) {
                    pattern = ".m4s";
                } else {
                    pattern = ".ts";
                }
            }

            if (hls->use_localtime) {
                basename_size = strlen(vs->m3u8_name) + strlen(pattern_localtime_fmt) + 1;
            } else {
                basename_size = strlen(vs->m3u8_name) + strlen(pattern) + 1;
            }

            vs->basename = av_malloc(basename_size);
            if (!vs->basename) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            av_strlcpy(vs->basename, vs->m3u8_name, basename_size);

            p = strrchr(vs->basename, '.');
            if (p)
                *p = '\0';
            if (hls->use_localtime) {
                av_strlcat(vs->basename, pattern_localtime_fmt, basename_size);
            } else {
                av_strlcat(vs->basename, pattern, basename_size);
            }
        }

        if (hls->segment_type == SEGMENT_TYPE_FMP4) {
            if (hls->nb_varstreams > 1)
                fmp4_init_filename_len += strlen(POSTFIX_PATTERN);
            if (hls->flags & HLS_SINGLE_FILE) {
                vs->fmp4_init_filename  = av_strdup(vs->basename);
                if (!vs->fmp4_init_filename) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
            } else {
                vs->fmp4_init_filename = av_malloc(fmp4_init_filename_len);
                if (!vs->fmp4_init_filename ) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                av_strlcpy(vs->fmp4_init_filename, hls->fmp4_init_filename,
                           fmp4_init_filename_len);
                if (hls->nb_varstreams > 1) {
                    if (av_stristr(vs->fmp4_init_filename, "%v")) {
                        format_name(vs->fmp4_init_filename, fmp4_init_filename_len, i);
                    } else {
                        ret = append_postfix(vs->fmp4_init_filename, fmp4_init_filename_len, i);
                    }
                    if (ret < 0)
                        goto fail;
                }

                fmp4_init_filename_len = strlen(vs->m3u8_name) +
                    strlen(vs->fmp4_init_filename) + 1;

                vs->base_output_dirname = av_malloc(fmp4_init_filename_len);
                if (!vs->base_output_dirname) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }

                av_strlcpy(vs->base_output_dirname, vs->m3u8_name,
                           fmp4_init_filename_len);
                p = strrchr(vs->base_output_dirname, '/');
                if (p) {
                    *(p + 1) = '\0';
                    av_strlcat(vs->base_output_dirname, vs->fmp4_init_filename,
                               fmp4_init_filename_len);
                } else {
                    av_strlcpy(vs->base_output_dirname, vs->fmp4_init_filename,
                               fmp4_init_filename_len);
                }
            }
        }

        if (!hls->use_localtime) {
            ret = sls_flag_check_duration_size_index(hls);
            if (ret < 0) {
                goto fail;
            }
        } else {
            ret = sls_flag_check_duration_size(hls, vs);
            if (ret < 0) {
                goto fail;
            }
        }
        if (vs->has_subtitle) {

            if (hls->flags & HLS_SINGLE_FILE)
                vtt_pattern = ".vtt";
            vtt_basename_size = strlen(vs->m3u8_name) + strlen(vtt_pattern) + 1;

            vs->vtt_basename = av_malloc(vtt_basename_size);
            if (!vs->vtt_basename) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            vs->vtt_m3u8_name = av_malloc(vtt_basename_size);
            if (!vs->vtt_m3u8_name ) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            av_strlcpy(vs->vtt_basename, vs->m3u8_name, vtt_basename_size);
            p = strrchr(vs->vtt_basename, '.');
            if (p)
                *p = '\0';

            if ( hls->subtitle_filename ) {
                strcpy(vs->vtt_m3u8_name, hls->subtitle_filename);
                ret = format_name(vs->vtt_m3u8_name, vtt_basename_size, i);
                if (ret < 0)
                    goto fail;
            } else {
                strcpy(vs->vtt_m3u8_name, vs->vtt_basename);
                av_strlcat(vs->vtt_m3u8_name, "_vtt.m3u8", vtt_basename_size);
            }
            av_strlcat(vs->vtt_basename, vtt_pattern, vtt_basename_size);
        }

        if (hls->baseurl) {
            vs->baseurl = av_strdup(hls->baseurl);
            if (!vs->baseurl) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        }

        if ((ret = hls_mux_init(s, vs)) < 0)
            goto fail;

        if (hls->flags & HLS_APPEND_LIST) {
            parse_playlist(s, vs->m3u8_name, vs);
            vs->discontinuity = 1;
            if (hls->init_time > 0) {
                av_log(s, AV_LOG_WARNING, "append_list mode does not support hls_init_time,"
                       " hls_init_time value will have no effect\n");
                hls->init_time = 0;
                hls->recording_time = hls->time * AV_TIME_BASE;
            }
        }

        if ((ret = hls_start(s, vs)) < 0)
            goto fail;
    }

fail:
    if (ret < 0) {
        av_freep(&hls->key_basename);
        for (i = 0; i < hls->nb_varstreams && hls->var_streams; i++) {
            vs = &hls->var_streams[i];
            av_freep(&vs->basename);
            av_freep(&vs->vtt_basename);
            av_freep(&vs->fmp4_init_filename);
            av_freep(&vs->m3u8_name);
            av_freep(&vs->vtt_m3u8_name);
            av_freep(&vs->streams);
            av_freep(&vs->language);
            av_freep(&vs->agroup);
            av_freep(&vs->ccgroup);
            av_freep(&vs->baseurl);
            if (vs->avf)
                avformat_free_context(vs->avf);
            if (vs->vtt_avf)
                avformat_free_context(vs->vtt_avf);
        }
        for (i = 0; i < hls->nb_ccstreams; i++) {
            ClosedCaptionsStream *ccs = &hls->cc_streams[i];
            av_freep(&ccs->ccgroup);
            av_freep(&ccs->instreamid);
            av_freep(&ccs->language);
        }
        av_freep(&hls->var_streams);
        av_freep(&hls->cc_streams);
        av_freep(&hls->master_m3u8_url);
    }

    return ret;
}

#define OFFSET(x) offsetof(HLSContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"start_number",  "set first number in the sequence",        OFFSET(start_sequence),AV_OPT_TYPE_INT64,  {.i64 = 0},     0, INT64_MAX, E},
    {"hls_time",      "set segment length in seconds",           OFFSET(time),    AV_OPT_TYPE_FLOAT,  {.dbl = 2},     0, FLT_MAX, E},
    {"hls_init_time", "set segment length in seconds at init list",           OFFSET(init_time),    AV_OPT_TYPE_FLOAT,  {.dbl = 0},     0, FLT_MAX, E},
    {"hls_list_size", "set maximum number of playlist entries",  OFFSET(max_nb_segments),    AV_OPT_TYPE_INT,    {.i64 = 5},     0, INT_MAX, E},
    {"hls_delete_threshold", "set number of unreferenced segments to keep before deleting",  OFFSET(hls_delete_threshold),    AV_OPT_TYPE_INT,    {.i64 = 1},     1, INT_MAX, E},
    {"hls_ts_options","set hls mpegts list of options for the container format used for hls", OFFSET(format_options_str), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"hls_vtt_options","set hls vtt list of options for the container format used for hls", OFFSET(vtt_format_options_str), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
#if FF_API_HLS_WRAP
    {"hls_wrap",      "set number after which the index wraps (will be deprecated)",  OFFSET(wrap),    AV_OPT_TYPE_INT,    {.i64 = 0},     0, INT_MAX, E},
#endif
    {"hls_allow_cache", "explicitly set whether the client MAY (1) or MUST NOT (0) cache media segments", OFFSET(allowcache), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, E},
    {"hls_base_url",  "url to prepend to each playlist entry",   OFFSET(baseurl), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E},
    {"hls_segment_filename", "filename template for segment files", OFFSET(segment_filename),   AV_OPT_TYPE_STRING, {.str = NULL},            0,       0,         E},
    {"hls_segment_size", "maximum size per segment file, (in bytes)",  OFFSET(max_seg_size),    AV_OPT_TYPE_INT,    {.i64 = 0},               0,       INT_MAX,   E},
    {"hls_key_info_file",    "file with key URI and key file path", OFFSET(key_info_file),      AV_OPT_TYPE_STRING, {.str = NULL},            0,       0,         E},
    {"hls_enc",    "enable AES128 encryption support", OFFSET(encrypt),      AV_OPT_TYPE_BOOL, {.i64 = 0},            0,       1,         E},
    {"hls_enc_key",    "hex-coded 16 byte key to encrypt the segments", OFFSET(key),      AV_OPT_TYPE_STRING, .flags = E},
    {"hls_enc_key_url",    "url to access the key to decrypt the segments", OFFSET(key_url),      AV_OPT_TYPE_STRING, {.str = NULL},            0,       0,         E},
    {"hls_enc_iv",    "hex-coded 16 byte initialization vector", OFFSET(iv),      AV_OPT_TYPE_STRING, .flags = E},
    {"hls_subtitle_path",     "set path of hls subtitles", OFFSET(subtitle_filename), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"hls_segment_type",     "set hls segment files type", OFFSET(segment_type), AV_OPT_TYPE_INT, {.i64 = SEGMENT_TYPE_MPEGTS }, 0, SEGMENT_TYPE_FMP4, E, "segment_type"},
    {"mpegts",   "make segment file to mpegts files in m3u8", 0, AV_OPT_TYPE_CONST, {.i64 = SEGMENT_TYPE_MPEGTS }, 0, UINT_MAX,   E, "segment_type"},
    {"fmp4",   "make segment file to fragment mp4 files in m3u8", 0, AV_OPT_TYPE_CONST, {.i64 = SEGMENT_TYPE_FMP4 }, 0, UINT_MAX,   E, "segment_type"},
    {"hls_fmp4_init_filename", "set fragment mp4 file init filename", OFFSET(fmp4_init_filename),   AV_OPT_TYPE_STRING, {.str = "init.mp4"},            0,       0,         E},
    {"hls_flags",     "set flags affecting HLS playlist and media file generation", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0 }, 0, UINT_MAX, E, "flags"},
    {"single_file",   "generate a single media file indexed with byte ranges", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SINGLE_FILE }, 0, UINT_MAX,   E, "flags"},
    {"temp_file", "write segment to temporary file and rename when complete", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_TEMP_FILE }, 0, UINT_MAX,   E, "flags"},
    {"delete_segments", "delete segment files that are no longer part of the playlist", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_DELETE_SEGMENTS }, 0, UINT_MAX,   E, "flags"},
    {"round_durations", "round durations in m3u8 to whole numbers", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_ROUND_DURATIONS }, 0, UINT_MAX,   E, "flags"},
    {"discont_start", "start the playlist with a discontinuity tag", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_DISCONT_START }, 0, UINT_MAX,   E, "flags"},
    {"omit_endlist", "Do not append an endlist when ending stream", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_OMIT_ENDLIST }, 0, UINT_MAX,   E, "flags"},
    {"split_by_time", "split the hls segment by time which user set by hls_time", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SPLIT_BY_TIME }, 0, UINT_MAX,   E, "flags"},
    {"append_list", "append the new segments into old hls segment list", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_APPEND_LIST }, 0, UINT_MAX,   E, "flags"},
    {"program_date_time", "add EXT-X-PROGRAM-DATE-TIME", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_PROGRAM_DATE_TIME }, 0, UINT_MAX,   E, "flags"},
    {"second_level_segment_index", "include segment index in segment filenames when use_localtime", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SECOND_LEVEL_SEGMENT_INDEX }, 0, UINT_MAX,   E, "flags"},
    {"second_level_segment_duration", "include segment duration in segment filenames when use_localtime", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SECOND_LEVEL_SEGMENT_DURATION }, 0, UINT_MAX,   E, "flags"},
    {"second_level_segment_size", "include segment size in segment filenames when use_localtime", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SECOND_LEVEL_SEGMENT_SIZE }, 0, UINT_MAX,   E, "flags"},
    {"periodic_rekey", "reload keyinfo file periodically for re-keying", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_PERIODIC_REKEY }, 0, UINT_MAX,   E, "flags"},
    {"independent_segments", "add EXT-X-INDEPENDENT-SEGMENTS, whenever applicable", 0, AV_OPT_TYPE_CONST, { .i64 = HLS_INDEPENDENT_SEGMENTS }, 0, UINT_MAX, E, "flags"},
#if FF_API_HLS_USE_LOCALTIME
    {"use_localtime", "set filename expansion with strftime at segment creation(will be deprecated )", OFFSET(use_localtime), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, E },
#endif
    {"strftime", "set filename expansion with strftime at segment creation", OFFSET(use_localtime), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, E },
#if FF_API_HLS_USE_LOCALTIME
    {"use_localtime_mkdir", "create last directory component in strftime-generated filename(will be deprecated)", OFFSET(use_localtime_mkdir), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, E },
#endif
    {"strftime_mkdir", "create last directory component in strftime-generated filename", OFFSET(use_localtime_mkdir), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, E },
    {"hls_playlist_type", "set the HLS playlist type", OFFSET(pl_type), AV_OPT_TYPE_INT, {.i64 = PLAYLIST_TYPE_NONE }, 0, PLAYLIST_TYPE_NB-1, E, "pl_type" },
    {"event", "EVENT playlist", 0, AV_OPT_TYPE_CONST, {.i64 = PLAYLIST_TYPE_EVENT }, INT_MIN, INT_MAX, E, "pl_type" },
    {"vod", "VOD playlist", 0, AV_OPT_TYPE_CONST, {.i64 = PLAYLIST_TYPE_VOD }, INT_MIN, INT_MAX, E, "pl_type" },
    {"method", "set the HTTP method(default: PUT)", OFFSET(method), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"hls_start_number_source", "set source of first number in sequence", OFFSET(start_sequence_source_type), AV_OPT_TYPE_INT, {.i64 = HLS_START_SEQUENCE_AS_START_NUMBER }, 0, HLS_START_SEQUENCE_AS_FORMATTED_DATETIME, E, "start_sequence_source_type" },
    {"generic", "start_number value (default)", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_START_SEQUENCE_AS_START_NUMBER }, INT_MIN, INT_MAX, E, "start_sequence_source_type" },
    {"epoch", "seconds since epoch", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_START_SEQUENCE_AS_SECONDS_SINCE_EPOCH }, INT_MIN, INT_MAX, E, "start_sequence_source_type" },
    {"datetime", "current datetime as YYYYMMDDhhmmss", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_START_SEQUENCE_AS_FORMATTED_DATETIME }, INT_MIN, INT_MAX, E, "start_sequence_source_type" },
    {"http_user_agent", "override User-Agent field in HTTP header", OFFSET(user_agent), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"var_stream_map", "Variant stream map string", OFFSET(var_stream_map), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"cc_stream_map", "Closed captions stream map string", OFFSET(cc_stream_map), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"master_pl_name", "Create HLS master playlist with this name", OFFSET(master_pl_name), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"master_pl_publish_rate", "Publish master play list every after this many segment intervals", OFFSET(master_publish_rate), AV_OPT_TYPE_INT, {.i64 = 0}, 0, UINT_MAX, E},
    {"http_persistent", "Use persistent HTTP connections", OFFSET(http_persistent), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, E },
    {"timeout", "set timeout for socket I/O operations", OFFSET(timeout), AV_OPT_TYPE_DURATION, { .i64 = -1 }, -1, INT_MAX, .flags = E },
    {"ignore_io_errors", "Ignore IO errors for stable long-duration runs with network output", OFFSET(ignore_io_errors), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, E },
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
    .subtitle_codec = AV_CODEC_ID_WEBVTT,
    .flags          = AVFMT_NOFILE | AVFMT_GLOBALHEADER | AVFMT_ALLOW_FLUSH | AVFMT_NODIMENSIONS,
    .init           = hls_init,
    .write_header   = hls_write_header,
    .write_packet   = hls_write_packet,
    .write_trailer  = hls_write_trailer,
    .priv_class     = &hls_class,
};
