/*
 * Apple HTTP Live Streaming segmenter
 * Copyright (c) 2012, Luca Barbato
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

#include "libavutil/avassert.h"
#include "libavutil/mathematics.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"
#include "libavutil/time_internal.h"

#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "os_support.h"

#define KEYSIZE 16
#define LINE_BUFFER_SIZE 1024

typedef struct HLSSegment {
    char filename[1024];
    char sub_filename[1024];
    double duration; /* in seconds */
    int64_t pos;
    int64_t size;

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
} HLSFlags;

typedef enum {
    PLAYLIST_TYPE_NONE,
    PLAYLIST_TYPE_EVENT,
    PLAYLIST_TYPE_VOD,
    PLAYLIST_TYPE_NB,
} PlaylistType;

typedef struct HLSContext {
    const AVClass *class;  // Class for private options.
    unsigned number;
    int64_t sequence;
    int64_t start_sequence;
    AVOutputFormat *oformat;
    AVOutputFormat *vtt_oformat;

    AVFormatContext *avf;
    AVFormatContext *vtt_avf;

    float time;            // Set by a private option.
    float init_time;       // Set by a private option.
    int max_nb_segments;   // Set by a private option.
    int  wrap;             // Set by a private option.
    uint32_t flags;        // enum HLSFlags
    uint32_t pl_type;      // enum PlaylistType
    char *segment_filename;

    int use_localtime;      ///< flag to expand filename with localtime
    int use_localtime_mkdir;///< flag to mkdir dirname in timebased filename
    int allowcache;
    int64_t recording_time;
    int has_video;
    int has_subtitle;
    int64_t start_pts;
    int64_t end_pts;
    double duration;      // last segment duration computed so far, in seconds
    int64_t start_pos;    // last segment starting position
    int64_t size;         // last segment size
    int64_t max_seg_size; // every segment file max size
    int nb_entries;
    int discontinuity_set;

    HLSSegment *segments;
    HLSSegment *last_segment;
    HLSSegment *old_segments;

    char *basename;
    char *vtt_basename;
    char *vtt_m3u8_name;
    char *baseurl;
    char *format_options_str;
    char *vtt_format_options_str;
    char *subtitle_filename;
    AVDictionary *format_options;

    char *key_info_file;
    char key_file[LINE_BUFFER_SIZE + 1];
    char key_uri[LINE_BUFFER_SIZE + 1];
    char key_string[KEYSIZE*2 + 1];
    char iv_string[KEYSIZE*2 + 1];
    AVDictionary *vtt_format_options;

    char *method;

    double initial_prog_date_time;
} HLSContext;

static int mkdir_p(const char *path) {
    int ret = 0;
    char *temp = av_strdup(path);
    char *pos = temp;
    char tmp_ch = '\0';

    if (!path || !temp) {
        return -1;
    }

    if (!strncmp(temp, "/", 1) || !strncmp(temp, "\\", 1)) {
        pos++;
    } else if (!strncmp(temp, "./", 2) || !strncmp(temp, ".\\", 2)) {
        pos += 2;
    }

    for ( ; *pos != '\0'; ++pos) {
        if (*pos == '/' || *pos == '\\') {
            tmp_ch = *pos;
            *pos = '\0';
            ret = mkdir(temp, 0755);
            *pos = tmp_ch;
        }
    }

    if ((*(pos - 1) != '/') || (*(pos - 1) != '\\')) {
        ret = mkdir(temp, 0755);
    }

    av_free(temp);
    return ret;
}

static int hls_delete_old_segments(HLSContext *hls) {

    HLSSegment *segment, *previous_segment = NULL;
    float playlist_duration = 0.0f;
    int ret = 0, path_size, sub_path_size;
    char *dirname = NULL, *p, *sub_path;
    char *path = NULL;

    segment = hls->segments;
    while (segment) {
        playlist_duration += segment->duration;
        segment = segment->next;
    }

    segment = hls->old_segments;
    while (segment) {
        playlist_duration -= segment->duration;
        previous_segment = segment;
        segment = previous_segment->next;
        if (playlist_duration <= -previous_segment->duration) {
            previous_segment->next = NULL;
            break;
        }
    }

    if (segment) {
        if (hls->segment_filename) {
            dirname = av_strdup(hls->segment_filename);
        } else {
            dirname = av_strdup(hls->avf->filename);
        }
        if (!dirname) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        p = (char *)av_basename(dirname);
        *p = '\0';
    }

    while (segment) {
        av_log(hls, AV_LOG_DEBUG, "deleting old segment %s\n",
                                  segment->filename);
        path_size = strlen(dirname) + strlen(segment->filename) + 1;
        path = av_malloc(path_size);
        if (!path) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        av_strlcpy(path, dirname, path_size);
        av_strlcat(path, segment->filename, path_size);
        if (unlink(path) < 0) {
            av_log(hls, AV_LOG_ERROR, "failed to delete old segment %s: %s\n",
                                     path, strerror(errno));
        }

        if (segment->sub_filename[0] != '\0') {
            sub_path_size = strlen(dirname) + strlen(segment->sub_filename) + 1;
            sub_path = av_malloc(sub_path_size);
            if (!sub_path) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            av_strlcpy(sub_path, dirname, sub_path_size);
            av_strlcat(sub_path, segment->sub_filename, sub_path_size);
            if (unlink(sub_path) < 0) {
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

    return ret;
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

static int read_chomp_line(AVIOContext *s, char *buf, int maxlen)
{
    int len = ff_get_line(s, buf, maxlen);
    while (len > 0 && av_isspace(buf[len - 1]))
        buf[--len] = '\0';
    return len;
}

static int hls_mux_init(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc;
    AVFormatContext *vtt_oc = NULL;
    int i, ret;

    ret = avformat_alloc_output_context2(&hls->avf, hls->oformat, NULL, NULL);
    if (ret < 0)
        return ret;
    oc = hls->avf;

    oc->oformat            = hls->oformat;
    oc->interrupt_callback = s->interrupt_callback;
    oc->max_delay          = s->max_delay;
    oc->opaque             = s->opaque;
    oc->io_open            = s->io_open;
    oc->io_close           = s->io_close;
    av_dict_copy(&oc->metadata, s->metadata, 0);

    if(hls->vtt_oformat) {
        ret = avformat_alloc_output_context2(&hls->vtt_avf, hls->vtt_oformat, NULL, NULL);
        if (ret < 0)
            return ret;
        vtt_oc          = hls->vtt_avf;
        vtt_oc->oformat = hls->vtt_oformat;
        av_dict_copy(&vtt_oc->metadata, s->metadata, 0);
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st;
        AVFormatContext *loc;
        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
            loc = vtt_oc;
        else
            loc = oc;

        if (!(st = avformat_new_stream(loc, NULL)))
            return AVERROR(ENOMEM);
        avcodec_parameters_copy(st->codecpar, s->streams[i]->codecpar);
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        st->time_base = s->streams[i]->time_base;
    }
    hls->start_pos = 0;

    return 0;
}

/* Create a new segment and append it to the segment list */
static int hls_append_segment(struct AVFormatContext *s, HLSContext *hls, double duration,
                              int64_t pos, int64_t size)
{
    HLSSegment *en = av_malloc(sizeof(*en));
    const char  *filename;
    int ret;

    if (!en)
        return AVERROR(ENOMEM);

    filename = av_basename(hls->avf->filename);

    if (hls->use_localtime_mkdir) {
        filename = hls->avf->filename;
    }
    av_strlcpy(en->filename, filename, sizeof(en->filename));

    if(hls->has_subtitle)
        av_strlcpy(en->sub_filename, av_basename(hls->vtt_avf->filename), sizeof(en->sub_filename));
    else
        en->sub_filename[0] = '\0';

    en->duration = duration;
    en->pos      = pos;
    en->size     = size;
    en->next     = NULL;

    if (hls->key_info_file) {
        av_strlcpy(en->key_uri, hls->key_uri, sizeof(en->key_uri));
        av_strlcpy(en->iv_string, hls->iv_string, sizeof(en->iv_string));
    }

    if (!hls->segments)
        hls->segments = en;
    else
        hls->last_segment->next = en;

    hls->last_segment = en;

    // EVENT or VOD playlists imply sliding window cannot be used
    if (hls->pl_type != PLAYLIST_TYPE_NONE)
        hls->max_nb_segments = 0;

    if (hls->max_nb_segments && hls->nb_entries >= hls->max_nb_segments) {
        en = hls->segments;
        hls->segments = en->next;
        if (en && hls->flags & HLS_DELETE_SEGMENTS &&
                !(hls->flags & HLS_SINGLE_FILE || hls->wrap)) {
            en->next = hls->old_segments;
            hls->old_segments = en;
            if ((ret = hls_delete_old_segments(hls)) < 0)
                return ret;
        } else
            av_free(en);
    } else
        hls->nb_entries++;

    if (hls->max_seg_size > 0) {
        return 0;
    }
    hls->sequence++;

    return 0;
}

static int parse_playlist(AVFormatContext *s, const char *url)
{
    HLSContext *hls = s->priv_data;
    AVIOContext *in;
    int ret = 0, is_segment = 0;
    int64_t new_start_pos;
    char line[1024];
    const char *ptr;

    if ((ret = ffio_open_whitelist(&in, url, AVIO_FLAG_READ,
                                   &s->interrupt_callback, NULL,
                                   s->protocol_whitelist, s->protocol_blacklist)) < 0)
        return ret;

    read_chomp_line(in, line, sizeof(line));
    if (strcmp(line, "#EXTM3U")) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    while (!avio_feof(in)) {
        read_chomp_line(in, line, sizeof(line));
        if (av_strstart(line, "#EXT-X-MEDIA-SEQUENCE:", &ptr)) {
            hls->sequence = atoi(ptr);
        } else if (av_strstart(line, "#EXTINF:", &ptr)) {
            is_segment = 1;
            hls->duration = atof(ptr);
        } else if (av_strstart(line, "#", NULL)) {
            continue;
        } else if (line[0]) {
            if (is_segment) {
                is_segment = 0;
                new_start_pos = avio_tell(hls->avf->pb);
                hls->size = new_start_pos - hls->start_pos;
                av_strlcpy(hls->avf->filename, line, sizeof(line));
                ret = hls_append_segment(s, hls, hls->duration, hls->start_pos, hls->size);
                if (ret < 0)
                    goto fail;
                hls->start_pos = new_start_pos;
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

static void set_http_options(AVDictionary **options, HLSContext *c)
{
    if (c->method)
        av_dict_set(options, "method", c->method, 0);
}

static int hls_window(AVFormatContext *s, int last)
{
    HLSContext *hls = s->priv_data;
    HLSSegment *en;
    int target_duration = 0;
    int ret = 0;
    AVIOContext *out = NULL;
    AVIOContext *sub_out = NULL;
    char temp_filename[1024];
    int64_t sequence = FFMAX(hls->start_sequence, hls->sequence - hls->nb_entries);
    int version = 3;
    const char *proto = avio_find_protocol_name(s->filename);
    int use_rename = proto && !strcmp(proto, "file");
    static unsigned warned_non_file;
    char *key_uri = NULL;
    char *iv_string = NULL;
    AVDictionary *options = NULL;
    double prog_date_time = hls->initial_prog_date_time;
    int byterange_mode = (hls->flags & HLS_SINGLE_FILE) || (hls->max_seg_size > 0);

    if (byterange_mode) {
        version = 4;
        sequence = 0;
    }

    if (!use_rename && !warned_non_file++)
        av_log(s, AV_LOG_ERROR, "Cannot use rename on non file protocol, this may lead to races and temporary partial files\n");

    set_http_options(&options, hls);
    snprintf(temp_filename, sizeof(temp_filename), use_rename ? "%s.tmp" : "%s", s->filename);
    if ((ret = s->io_open(s, &out, temp_filename, AVIO_FLAG_WRITE, &options)) < 0)
        goto fail;

    for (en = hls->segments; en; en = en->next) {
        if (target_duration < en->duration)
            target_duration = ceil(en->duration);
    }

    hls->discontinuity_set = 0;
    avio_printf(out, "#EXTM3U\n");
    avio_printf(out, "#EXT-X-VERSION:%d\n", version);
    if (hls->allowcache == 0 || hls->allowcache == 1) {
        avio_printf(out, "#EXT-X-ALLOW-CACHE:%s\n", hls->allowcache == 0 ? "NO" : "YES");
    }
    avio_printf(out, "#EXT-X-TARGETDURATION:%d\n", target_duration);
    avio_printf(out, "#EXT-X-MEDIA-SEQUENCE:%"PRId64"\n", sequence);
    if (hls->pl_type == PLAYLIST_TYPE_EVENT) {
        avio_printf(out, "#EXT-X-PLAYLIST-TYPE:EVENT\n");
    } else if (hls->pl_type == PLAYLIST_TYPE_VOD) {
        avio_printf(out, "#EXT-X-PLAYLIST-TYPE:VOD\n");
    }

    av_log(s, AV_LOG_VERBOSE, "EXT-X-MEDIA-SEQUENCE:%"PRId64"\n",
           sequence);
    if((hls->flags & HLS_DISCONT_START) && sequence==hls->start_sequence && hls->discontinuity_set==0 ){
        avio_printf(out, "#EXT-X-DISCONTINUITY\n");
        hls->discontinuity_set = 1;
    }
    for (en = hls->segments; en; en = en->next) {
        if (hls->key_info_file && (!key_uri || strcmp(en->key_uri, key_uri) ||
                                    av_strcasecmp(en->iv_string, iv_string))) {
            avio_printf(out, "#EXT-X-KEY:METHOD=AES-128,URI=\"%s\"", en->key_uri);
            if (*en->iv_string)
                avio_printf(out, ",IV=0x%s", en->iv_string);
            avio_printf(out, "\n");
            key_uri = en->key_uri;
            iv_string = en->iv_string;
        }

        if (hls->flags & HLS_ROUND_DURATIONS)
            avio_printf(out, "#EXTINF:%ld,\n",  lrint(en->duration));
        else
            avio_printf(out, "#EXTINF:%f,\n", en->duration);
        if (byterange_mode)
             avio_printf(out, "#EXT-X-BYTERANGE:%"PRIi64"@%"PRIi64"\n",
                         en->size, en->pos);
        if (hls->flags & HLS_PROGRAM_DATE_TIME) {
            time_t tt, wrongsecs;
            int milli;
            struct tm *tm, tmpbuf;
            char buf0[128], buf1[128];
            tt = (int64_t)prog_date_time;
            milli = av_clip(lrint(1000*(prog_date_time - tt)), 0, 999);
            tm = localtime_r(&tt, &tmpbuf);
            strftime(buf0, sizeof(buf0), "%Y-%m-%dT%H:%M:%S", tm);
            if (!strftime(buf1, sizeof(buf1), "%z", tm) || buf1[1]<'0' ||buf1[1]>'2') {
                int tz_min, dst = tm->tm_isdst;
                tm = gmtime_r(&tt, &tmpbuf);
                tm->tm_isdst = dst;
                wrongsecs = mktime(tm);
                tz_min = (abs(wrongsecs - tt) + 30) / 60;
                snprintf(buf1, sizeof(buf1),
                         "%c%02d%02d",
                         wrongsecs <= tt ? '+' : '-',
                         tz_min / 60,
                         tz_min % 60);
            }
            avio_printf(out, "#EXT-X-PROGRAM-DATE-TIME:%s.%03d%s\n", buf0, milli, buf1);
            prog_date_time += en->duration;
        }
        if (hls->baseurl)
            avio_printf(out, "%s", hls->baseurl);
        avio_printf(out, "%s\n", en->filename);
    }

    if (last && (hls->flags & HLS_OMIT_ENDLIST)==0)
        avio_printf(out, "#EXT-X-ENDLIST\n");

    if( hls->vtt_m3u8_name ) {
        if ((ret = s->io_open(s, &sub_out, hls->vtt_m3u8_name, AVIO_FLAG_WRITE, &options)) < 0)
            goto fail;
        avio_printf(sub_out, "#EXTM3U\n");
        avio_printf(sub_out, "#EXT-X-VERSION:%d\n", version);
        if (hls->allowcache == 0 || hls->allowcache == 1) {
            avio_printf(sub_out, "#EXT-X-ALLOW-CACHE:%s\n", hls->allowcache == 0 ? "NO" : "YES");
        }
        avio_printf(sub_out, "#EXT-X-TARGETDURATION:%d\n", target_duration);
        avio_printf(sub_out, "#EXT-X-MEDIA-SEQUENCE:%"PRId64"\n", sequence);

        av_log(s, AV_LOG_VERBOSE, "EXT-X-MEDIA-SEQUENCE:%"PRId64"\n",
               sequence);

        for (en = hls->segments; en; en = en->next) {
            avio_printf(sub_out, "#EXTINF:%f,\n", en->duration);
            if (byterange_mode)
                 avio_printf(sub_out, "#EXT-X-BYTERANGE:%"PRIi64"@%"PRIi64"\n",
                         en->size, en->pos);
            if (hls->baseurl)
                avio_printf(sub_out, "%s", hls->baseurl);
            avio_printf(sub_out, "%s\n", en->sub_filename);
        }

        if (last)
            avio_printf(sub_out, "#EXT-X-ENDLIST\n");

    }

fail:
    av_dict_free(&options);
    ff_format_io_close(s, &out);
    ff_format_io_close(s, &sub_out);
    if (ret >= 0 && use_rename)
        ff_rename(temp_filename, s->filename, s);
    return ret;
}

static int hls_start(AVFormatContext *s)
{
    HLSContext *c = s->priv_data;
    AVFormatContext *oc = c->avf;
    AVFormatContext *vtt_oc = c->vtt_avf;
    AVDictionary *options = NULL;
    char *filename, iv_string[KEYSIZE*2 + 1];
    int err = 0;

    if (c->flags & HLS_SINGLE_FILE) {
        av_strlcpy(oc->filename, c->basename,
                   sizeof(oc->filename));
        if (c->vtt_basename)
            av_strlcpy(vtt_oc->filename, c->vtt_basename,
                  sizeof(vtt_oc->filename));
    } else if (c->max_seg_size > 0) {
        if (av_get_frame_filename2(oc->filename, sizeof(oc->filename),
            c->basename, c->wrap ? c->sequence % c->wrap : c->sequence,
            AV_FRAME_FILENAME_FLAGS_MULTIPLE) < 0) {
                av_log(oc, AV_LOG_ERROR, "Invalid segment filename template '%s', you can try to use -use_localtime 1 with it\n", c->basename);
                return AVERROR(EINVAL);
        }
    } else {
        if (c->use_localtime) {
            time_t now0;
            struct tm *tm, tmpbuf;
            time(&now0);
            tm = localtime_r(&now0, &tmpbuf);
            if (!strftime(oc->filename, sizeof(oc->filename), c->basename, tm)) {
                av_log(oc, AV_LOG_ERROR, "Could not get segment filename with use_localtime\n");
                return AVERROR(EINVAL);
            }

            if (c->use_localtime_mkdir) {
                const char *dir;
                char *fn_copy = av_strdup(oc->filename);
                if (!fn_copy) {
                    return AVERROR(ENOMEM);
                }
                dir = av_dirname(fn_copy);
                if (mkdir_p(dir) == -1 && errno != EEXIST) {
                    av_log(oc, AV_LOG_ERROR, "Could not create directory %s with use_localtime_mkdir\n", dir);
                    av_free(fn_copy);
                    return AVERROR(errno);
                }
                av_free(fn_copy);
            }
        } else if (av_get_frame_filename2(oc->filename, sizeof(oc->filename),
                                  c->basename, c->wrap ? c->sequence % c->wrap : c->sequence,
                                  AV_FRAME_FILENAME_FLAGS_MULTIPLE) < 0) {
            av_log(oc, AV_LOG_ERROR, "Invalid segment filename template '%s' you can try to use -use_localtime 1 with it\n", c->basename);
            return AVERROR(EINVAL);
        }
        if( c->vtt_basename) {
            if (av_get_frame_filename2(vtt_oc->filename, sizeof(vtt_oc->filename),
                              c->vtt_basename, c->wrap ? c->sequence % c->wrap : c->sequence,
                              AV_FRAME_FILENAME_FLAGS_MULTIPLE) < 0) {
                av_log(vtt_oc, AV_LOG_ERROR, "Invalid segment filename template '%s'\n", c->vtt_basename);
                return AVERROR(EINVAL);
            }
       }
    }
    c->number++;

    set_http_options(&options, c);

    if (c->key_info_file) {
        if ((err = hls_encryption_start(s)) < 0)
            goto fail;
        if ((err = av_dict_set(&options, "encryption_key", c->key_string, 0))
                < 0)
            goto fail;
        err = av_strlcpy(iv_string, c->iv_string, sizeof(iv_string));
        if (!err)
            snprintf(iv_string, sizeof(iv_string), "%032"PRIx64, c->sequence);
        if ((err = av_dict_set(&options, "encryption_iv", iv_string, 0)) < 0)
           goto fail;

        filename = av_asprintf("crypto:%s", oc->filename);
        if (!filename) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        err = s->io_open(s, &oc->pb, filename, AVIO_FLAG_WRITE, &options);
        av_free(filename);
        av_dict_free(&options);
        if (err < 0)
            return err;
    } else
        if ((err = s->io_open(s, &oc->pb, oc->filename, AVIO_FLAG_WRITE, &options)) < 0)
            goto fail;
    if (c->vtt_basename) {
        set_http_options(&options, c);
        if ((err = s->io_open(s, &vtt_oc->pb, vtt_oc->filename, AVIO_FLAG_WRITE, &options)) < 0)
            goto fail;
    }
    av_dict_free(&options);

    /* We only require one PAT/PMT per segment. */
    if (oc->oformat->priv_class && oc->priv_data) {
        char period[21];

        snprintf(period, sizeof(period), "%d", (INT_MAX / 2) - 1);

        av_opt_set(oc->priv_data, "mpegts_flags", "resend_headers", 0);
        av_opt_set(oc->priv_data, "sdt_period", period, 0);
        av_opt_set(oc->priv_data, "pat_period", period, 0);
    }

    if (c->vtt_basename) {
        err = avformat_write_header(vtt_oc,NULL);
        if (err < 0)
            return err;
    }

    return 0;
fail:
    av_dict_free(&options);

    return err;
}

static int hls_write_header(AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    int ret, i;
    char *p;
    const char *pattern = "%d.ts";
    const char *pattern_localtime_fmt = "-%s.ts";
    const char *vtt_pattern = "%d.vtt";
    AVDictionary *options = NULL;
    int basename_size;
    int vtt_basename_size;

    hls->sequence       = hls->start_sequence;
    hls->recording_time = (hls->init_time ? hls->init_time : hls->time) * AV_TIME_BASE;
    hls->start_pts      = AV_NOPTS_VALUE;

    if (hls->flags & HLS_PROGRAM_DATE_TIME) {
        time_t now0;
        time(&now0);
        hls->initial_prog_date_time = now0;
    }

    if (hls->format_options_str) {
        ret = av_dict_parse_string(&hls->format_options, hls->format_options_str, "=", ":", 0);
        if (ret < 0) {
            av_log(s, AV_LOG_ERROR, "Could not parse format options list '%s'\n", hls->format_options_str);
            goto fail;
        }
    }

    for (i = 0; i < s->nb_streams; i++) {
        hls->has_video +=
            s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        hls->has_subtitle +=
            s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE;
    }

    if (hls->has_video > 1)
        av_log(s, AV_LOG_WARNING,
               "More than a single video stream present, "
               "expect issues decoding it.\n");

    hls->oformat = av_guess_format("mpegts", NULL, NULL);

    if (!hls->oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }

    if(hls->has_subtitle) {
        hls->vtt_oformat = av_guess_format("webvtt", NULL, NULL);
        if (!hls->oformat) {
            ret = AVERROR_MUXER_NOT_FOUND;
            goto fail;
        }
    }

    if (hls->segment_filename) {
        hls->basename = av_strdup(hls->segment_filename);
        if (!hls->basename) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    } else {
        if (hls->flags & HLS_SINGLE_FILE)
            pattern = ".ts";

        if (hls->use_localtime) {
            basename_size = strlen(s->filename) + strlen(pattern_localtime_fmt) + 1;
        } else {
            basename_size = strlen(s->filename) + strlen(pattern) + 1;
        }
        hls->basename = av_malloc(basename_size);
        if (!hls->basename) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        av_strlcpy(hls->basename, s->filename, basename_size);

        p = strrchr(hls->basename, '.');
        if (p)
            *p = '\0';
        if (hls->use_localtime) {
            av_strlcat(hls->basename, pattern_localtime_fmt, basename_size);
        } else {
            av_strlcat(hls->basename, pattern, basename_size);
        }
    }

    if(hls->has_subtitle) {

        if (hls->flags & HLS_SINGLE_FILE)
            vtt_pattern = ".vtt";
        vtt_basename_size = strlen(s->filename) + strlen(vtt_pattern) + 1;
        hls->vtt_basename = av_malloc(vtt_basename_size);
        if (!hls->vtt_basename) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        hls->vtt_m3u8_name = av_malloc(vtt_basename_size);
        if (!hls->vtt_m3u8_name ) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        av_strlcpy(hls->vtt_basename, s->filename, vtt_basename_size);
        p = strrchr(hls->vtt_basename, '.');
        if (p)
            *p = '\0';

        if( hls->subtitle_filename ) {
            strcpy(hls->vtt_m3u8_name, hls->subtitle_filename);
        } else {
            strcpy(hls->vtt_m3u8_name, hls->vtt_basename);
            av_strlcat(hls->vtt_m3u8_name, "_vtt.m3u8", vtt_basename_size);
        }
        av_strlcat(hls->vtt_basename, vtt_pattern, vtt_basename_size);
    }

    if ((ret = hls_mux_init(s)) < 0)
        goto fail;

    if (hls->flags & HLS_APPEND_LIST) {
        parse_playlist(s, s->filename);
        if (hls->init_time > 0) {
            av_log(s, AV_LOG_WARNING, "append_list mode does not support hls_init_time,"
                   " hls_init_time value will have no effect\n");
            hls->init_time = 0;
            hls->recording_time = hls->time * AV_TIME_BASE;
        }
    }

    if ((ret = hls_start(s)) < 0)
        goto fail;

    av_dict_copy(&options, hls->format_options, 0);
    ret = avformat_write_header(hls->avf, &options);
    if (av_dict_count(options)) {
        av_log(s, AV_LOG_ERROR, "Some of provided format options in '%s' are not recognized\n", hls->format_options_str);
        ret = AVERROR(EINVAL);
        goto fail;
    }
    //av_assert0(s->nb_streams == hls->avf->nb_streams);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *inner_st;
        AVStream *outer_st = s->streams[i];

        if (hls->max_seg_size > 0) {
            if ((outer_st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) &&
                (outer_st->codecpar->bit_rate > hls->max_seg_size)) {
                av_log(s, AV_LOG_WARNING, "Your video bitrate is bigger than hls_segment_size, "
                       "(%"PRId64 " > %"PRId64 "), the result maybe not be what you want.",
                       outer_st->codecpar->bit_rate, hls->max_seg_size);
            }
        }

        if (outer_st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
            inner_st = hls->avf->streams[i];
        else if (hls->vtt_avf)
            inner_st = hls->vtt_avf->streams[0];
        else {
            /* We have a subtitle stream, when the user does not want one */
            inner_st = NULL;
            continue;
        }
        avpriv_set_pts_info(outer_st, inner_st->pts_wrap_bits, inner_st->time_base.num, inner_st->time_base.den);
    }
fail:

    av_dict_free(&options);
    if (ret < 0) {
        av_freep(&hls->basename);
        av_freep(&hls->vtt_basename);
        if (hls->avf)
            avformat_free_context(hls->avf);
        if (hls->vtt_avf)
            avformat_free_context(hls->vtt_avf);

    }
    return ret;
}

static int hls_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc = NULL;
    AVStream *st = s->streams[pkt->stream_index];
    int64_t end_pts = hls->recording_time * hls->number;
    int is_ref_pkt = 1;
    int ret, can_split = 1;
    int stream_index = 0;

    if (hls->sequence - hls->nb_entries > hls->start_sequence && hls->init_time > 0) {
        /* reset end_pts, hls->recording_time at end of the init hls list */
        int init_list_dur = hls->init_time * hls->nb_entries * AV_TIME_BASE;
        int after_init_list_dur = (hls->sequence - hls->nb_entries ) * hls->time * AV_TIME_BASE;
        hls->recording_time = hls->time * AV_TIME_BASE;
        end_pts = init_list_dur + after_init_list_dur ;
    }

    if( st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE ) {
        oc = hls->vtt_avf;
        stream_index = 0;
    } else {
        oc = hls->avf;
        stream_index = pkt->stream_index;
    }
    if (hls->start_pts == AV_NOPTS_VALUE) {
        hls->start_pts = pkt->pts;
        hls->end_pts   = pkt->pts;
    }

    if (hls->has_video) {
        can_split = st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    ((pkt->flags & AV_PKT_FLAG_KEY) || (hls->flags & HLS_SPLIT_BY_TIME));
        is_ref_pkt = st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
    }
    if (pkt->pts == AV_NOPTS_VALUE)
        is_ref_pkt = can_split = 0;

    if (is_ref_pkt)
        hls->duration = (double)(pkt->pts - hls->end_pts)
                                   * st->time_base.num / st->time_base.den;

    if (can_split && av_compare_ts(pkt->pts - hls->start_pts, st->time_base,
                                   end_pts, AV_TIME_BASE_Q) >= 0) {
        int64_t new_start_pos;
        av_write_frame(oc, NULL); /* Flush any buffered data */

        new_start_pos = avio_tell(hls->avf->pb);
        hls->size = new_start_pos - hls->start_pos;
        ret = hls_append_segment(s, hls, hls->duration, hls->start_pos, hls->size);
        hls->start_pos = new_start_pos;
        if (ret < 0)
            return ret;

        hls->end_pts = pkt->pts;
        hls->duration = 0;

        if (hls->flags & HLS_SINGLE_FILE) {
            if (hls->avf->oformat->priv_class && hls->avf->priv_data)
                av_opt_set(hls->avf->priv_data, "mpegts_flags", "resend_headers", 0);
            hls->number++;
        } else if (hls->max_seg_size > 0) {
            if (hls->avf->oformat->priv_class && hls->avf->priv_data)
                av_opt_set(hls->avf->priv_data, "mpegts_flags", "resend_headers", 0);
            if (hls->start_pos >= hls->max_seg_size) {
                hls->sequence++;
                ff_format_io_close(s, &oc->pb);
                if (hls->vtt_avf)
                    ff_format_io_close(s, &hls->vtt_avf->pb);
                ret = hls_start(s);
                hls->start_pos = 0;
                /* When split segment by byte, the duration is short than hls_time,
                 * so it is not enough one segment duration as hls_time, */
                hls->number--;
            }
            hls->number++;
        } else {
            ff_format_io_close(s, &oc->pb);
            if (hls->vtt_avf)
                ff_format_io_close(s, &hls->vtt_avf->pb);

            ret = hls_start(s);
        }

        if (ret < 0)
            return ret;

        if( st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE )
            oc = hls->vtt_avf;
        else
        oc = hls->avf;

        if ((ret = hls_window(s, 0)) < 0)
            return ret;
    }

    ret = ff_write_chained(oc, stream_index, pkt, s, 0);

    return ret;
}

static int hls_write_trailer(struct AVFormatContext *s)
{
    HLSContext *hls = s->priv_data;
    AVFormatContext *oc = hls->avf;
    AVFormatContext *vtt_oc = hls->vtt_avf;

    av_write_trailer(oc);
    if (oc->pb) {
        hls->size = avio_tell(hls->avf->pb) - hls->start_pos;
        ff_format_io_close(s, &oc->pb);
        hls_append_segment(s, hls, hls->duration, hls->start_pos, hls->size);
    }

    if (vtt_oc) {
        if (vtt_oc->pb)
            av_write_trailer(vtt_oc);
        hls->size = avio_tell(hls->vtt_avf->pb) - hls->start_pos;
        ff_format_io_close(s, &vtt_oc->pb);
    }
    av_freep(&hls->basename);
    avformat_free_context(oc);

    hls->avf = NULL;
    hls_window(s, 1);

    if (vtt_oc) {
        av_freep(&hls->vtt_basename);
        av_freep(&hls->vtt_m3u8_name);
        avformat_free_context(vtt_oc);
    }

    hls_free_segments(hls->segments);
    hls_free_segments(hls->old_segments);
    return 0;
}

#define OFFSET(x) offsetof(HLSContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"start_number",  "set first number in the sequence",        OFFSET(start_sequence),AV_OPT_TYPE_INT64,  {.i64 = 0},     0, INT64_MAX, E},
    {"hls_time",      "set segment length in seconds",           OFFSET(time),    AV_OPT_TYPE_FLOAT,  {.dbl = 2},     0, FLT_MAX, E},
    {"hls_init_time", "set segment length in seconds at init list",           OFFSET(init_time),    AV_OPT_TYPE_FLOAT,  {.dbl = 0},     0, FLT_MAX, E},
    {"hls_list_size", "set maximum number of playlist entries",  OFFSET(max_nb_segments),    AV_OPT_TYPE_INT,    {.i64 = 5},     0, INT_MAX, E},
    {"hls_ts_options","set hls mpegts list of options for the container format used for hls", OFFSET(format_options_str), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"hls_vtt_options","set hls vtt list of options for the container format used for hls", OFFSET(vtt_format_options_str), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"hls_wrap",      "set number after which the index wraps",  OFFSET(wrap),    AV_OPT_TYPE_INT,    {.i64 = 0},     0, INT_MAX, E},
    {"hls_allow_cache", "explicitly set whether the client MAY (1) or MUST NOT (0) cache media segments", OFFSET(allowcache), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, E},
    {"hls_base_url",  "url to prepend to each playlist entry",   OFFSET(baseurl), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E},
    {"hls_segment_filename", "filename template for segment files", OFFSET(segment_filename),   AV_OPT_TYPE_STRING, {.str = NULL},            0,       0,         E},
    {"hls_segment_size", "maximum size per segment file, (in bytes)",  OFFSET(max_seg_size),    AV_OPT_TYPE_INT,    {.i64 = 0},               0,       INT_MAX,   E},
    {"hls_key_info_file",    "file with key URI and key file path", OFFSET(key_info_file),      AV_OPT_TYPE_STRING, {.str = NULL},            0,       0,         E},
    {"hls_subtitle_path",     "set path of hls subtitles", OFFSET(subtitle_filename), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},
    {"hls_flags",     "set flags affecting HLS playlist and media file generation", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = 0 }, 0, UINT_MAX, E, "flags"},
    {"single_file",   "generate a single media file indexed with byte ranges", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SINGLE_FILE }, 0, UINT_MAX,   E, "flags"},
    {"delete_segments", "delete segment files that are no longer part of the playlist", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_DELETE_SEGMENTS }, 0, UINT_MAX,   E, "flags"},
    {"round_durations", "round durations in m3u8 to whole numbers", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_ROUND_DURATIONS }, 0, UINT_MAX,   E, "flags"},
    {"discont_start", "start the playlist with a discontinuity tag", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_DISCONT_START }, 0, UINT_MAX,   E, "flags"},
    {"omit_endlist", "Do not append an endlist when ending stream", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_OMIT_ENDLIST }, 0, UINT_MAX,   E, "flags"},
    {"split_by_time", "split the hls segment by time which user set by hls_time", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_SPLIT_BY_TIME }, 0, UINT_MAX,   E, "flags"},
    {"append_list", "append the new segments into old hls segment list", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_APPEND_LIST }, 0, UINT_MAX,   E, "flags"},
    {"program_date_time", "add EXT-X-PROGRAM-DATE-TIME", 0, AV_OPT_TYPE_CONST, {.i64 = HLS_PROGRAM_DATE_TIME }, 0, UINT_MAX,   E, "flags"},
    {"use_localtime", "set filename expansion with strftime at segment creation", OFFSET(use_localtime), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, E },
    {"use_localtime_mkdir", "create last directory component in strftime-generated filename", OFFSET(use_localtime_mkdir), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, E },
    {"hls_playlist_type", "set the HLS playlist type", OFFSET(pl_type), AV_OPT_TYPE_INT, {.i64 = PLAYLIST_TYPE_NONE }, 0, PLAYLIST_TYPE_NB-1, E, "pl_type" },
    {"event", "EVENT playlist", 0, AV_OPT_TYPE_CONST, {.i64 = PLAYLIST_TYPE_EVENT }, INT_MIN, INT_MAX, E, "pl_type" },
    {"vod", "VOD playlist", 0, AV_OPT_TYPE_CONST, {.i64 = PLAYLIST_TYPE_VOD }, INT_MIN, INT_MAX, E, "pl_type" },
    {"method", "set the HTTP method", OFFSET(method), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,    E},

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
    .flags          = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
    .write_header   = hls_write_header,
    .write_packet   = hls_write_packet,
    .write_trailer  = hls_write_trailer,
    .priv_class     = &hls_class,
};
