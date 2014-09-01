/*
 * Copyright (c) 2011, Luca Barbato
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
 * @file generic segmenter
 * M3U8 specification can be find here:
 * @url{http://tools.ietf.org/id/draft-pantos-http-live-streaming}
 */

/* #define DEBUG */

#include <float.h>
#include <time.h>

#include "avformat.h"
#include "internal.h"

#include "libavutil/avassert.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/parseutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

typedef struct SegmentListEntry {
    int index;
    double start_time, end_time;
    int64_t start_pts;
    int64_t offset_pts;
    char *filename;
    struct SegmentListEntry *next;
} SegmentListEntry;

typedef enum {
    LIST_TYPE_UNDEFINED = -1,
    LIST_TYPE_FLAT = 0,
    LIST_TYPE_CSV,
    LIST_TYPE_M3U8,
    LIST_TYPE_EXT, ///< deprecated
    LIST_TYPE_FFCONCAT,
    LIST_TYPE_NB,
} ListType;

#define SEGMENT_LIST_FLAG_CACHE 1
#define SEGMENT_LIST_FLAG_LIVE  2

typedef struct {
    const AVClass *class;  /**< Class for private options. */
    int segment_idx;       ///< index of the segment file to write, starting from 0
    int segment_idx_wrap;  ///< number after which the index wraps
    int segment_idx_wrap_nb;  ///< number of time the index has wraped
    int segment_count;     ///< number of segment files already written
    AVOutputFormat *oformat;
    AVFormatContext *avf;
    char *format;          ///< format to use for output segment files
    char *list;            ///< filename for the segment list file
    int   list_flags;      ///< flags affecting list generation
    int   list_size;       ///< number of entries for the segment list file

    int use_clocktime;    ///< flag to cut segments at regular clock time
    int64_t last_val;      ///< remember last time for wrap around detection
    int64_t last_cut;      ///< remember last cut
    int cut_pending;

    char *entry_prefix;    ///< prefix to add to list entry filenames
    ListType list_type;    ///< set the list type
    AVIOContext *list_pb;  ///< list file put-byte context
    char *time_str;        ///< segment duration specification string
    int64_t time;          ///< segment duration

    char *times_str;       ///< segment times specification string
    int64_t *times;        ///< list of segment interval specification
    int nb_times;          ///< number of elments in the times array

    char *frames_str;      ///< segment frame numbers specification string
    int *frames;           ///< list of frame number specification
    int nb_frames;         ///< number of elments in the frames array
    int frame_count;

    int64_t time_delta;
    int  individual_header_trailer; /**< Set by a private option. */
    int  write_header_trailer; /**< Set by a private option. */

    int reset_timestamps;  ///< reset timestamps at the begin of each segment
    int64_t initial_offset;    ///< initial timestamps offset, expressed in microseconds
    char *reference_stream_specifier; ///< reference stream specifier
    int   reference_stream_index;

    SegmentListEntry cur_entry;
    SegmentListEntry *segment_list_entries;
    SegmentListEntry *segment_list_entries_end;

    int is_first_pkt;      ///< tells if it is the first packet in the segment
} SegmentContext;

static void print_csv_escaped_str(AVIOContext *ctx, const char *str)
{
    int needs_quoting = !!str[strcspn(str, "\",\n\r")];

    if (needs_quoting)
        avio_w8(ctx, '"');

    for (; *str; str++) {
        if (*str == '"')
            avio_w8(ctx, '"');
        avio_w8(ctx, *str);
    }
    if (needs_quoting)
        avio_w8(ctx, '"');
}

static int segment_mux_init(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc;
    int i;

    seg->avf = oc = avformat_alloc_context();
    if (!oc)
        return AVERROR(ENOMEM);

    oc->oformat            = seg->oformat;
    oc->interrupt_callback = s->interrupt_callback;
    av_dict_copy(&oc->metadata, s->metadata, 0);

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st;
        AVCodecContext *icodec, *ocodec;

        if (!(st = avformat_new_stream(oc, NULL)))
            return AVERROR(ENOMEM);
        icodec = s->streams[i]->codec;
        ocodec = st->codec;
        avcodec_copy_context(ocodec, icodec);
        if (!oc->oformat->codec_tag ||
            av_codec_get_id (oc->oformat->codec_tag, icodec->codec_tag) == ocodec->codec_id ||
            av_codec_get_tag(oc->oformat->codec_tag, icodec->codec_id) <= 0) {
            ocodec->codec_tag = icodec->codec_tag;
        } else {
            ocodec->codec_tag = 0;
        }
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
    }

    return 0;
}

static int set_segment_filename(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    size_t size;

    if (seg->segment_idx_wrap)
        seg->segment_idx %= seg->segment_idx_wrap;
    if (av_get_frame_filename(oc->filename, sizeof(oc->filename),
                              s->filename, seg->segment_idx) < 0) {
        av_log(oc, AV_LOG_ERROR, "Invalid segment filename template '%s'\n", s->filename);
        return AVERROR(EINVAL);
    }

    /* copy modified name in list entry */
    size = strlen(av_basename(oc->filename)) + 1;
    if (seg->entry_prefix)
        size += strlen(seg->entry_prefix);

    seg->cur_entry.filename = av_mallocz(size);
    if (!seg->cur_entry.filename)
        return AVERROR(ENOMEM);
    snprintf(seg->cur_entry.filename, size, "%s%s",
             seg->entry_prefix ? seg->entry_prefix : "",
             av_basename(oc->filename));

    return 0;
}

static int segment_start(AVFormatContext *s, int write_header)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    int err = 0;

    if (write_header) {
        avformat_free_context(oc);
        seg->avf = NULL;
        if ((err = segment_mux_init(s)) < 0)
            return err;
        oc = seg->avf;
    }

    seg->segment_idx++;
    if ((seg->segment_idx_wrap) && (seg->segment_idx%seg->segment_idx_wrap == 0))
        seg->segment_idx_wrap_nb++;

    if ((err = set_segment_filename(s)) < 0)
        return err;

    if ((err = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                          &s->interrupt_callback, NULL)) < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to open segment '%s'\n", oc->filename);
        return err;
    }

    if (oc->oformat->priv_class && oc->priv_data)
        av_opt_set(oc->priv_data, "resend_headers", "1", 0); /* mpegts specific */

    if (write_header) {
        if ((err = avformat_write_header(oc, NULL)) < 0)
            return err;
    }

    seg->is_first_pkt = 1;
    return 0;
}

static int segment_list_open(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    int ret;

    ret = avio_open2(&seg->list_pb, seg->list, AVIO_FLAG_WRITE,
                     &s->interrupt_callback, NULL);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to open segment list '%s'\n", seg->list);
        return ret;
    }

    if (seg->list_type == LIST_TYPE_M3U8 && seg->segment_list_entries) {
        SegmentListEntry *entry;
        double max_duration = 0;

        avio_printf(seg->list_pb, "#EXTM3U\n");
        avio_printf(seg->list_pb, "#EXT-X-VERSION:3\n");
        avio_printf(seg->list_pb, "#EXT-X-MEDIA-SEQUENCE:%d\n", seg->segment_list_entries->index);
        avio_printf(seg->list_pb, "#EXT-X-ALLOW-CACHE:%s\n",
                    seg->list_flags & SEGMENT_LIST_FLAG_CACHE ? "YES" : "NO");

        av_log(s, AV_LOG_VERBOSE, "EXT-X-MEDIA-SEQUENCE:%d\n",
               seg->segment_list_entries->index);

        for (entry = seg->segment_list_entries; entry; entry = entry->next)
            max_duration = FFMAX(max_duration, entry->end_time - entry->start_time);
        avio_printf(seg->list_pb, "#EXT-X-TARGETDURATION:%"PRId64"\n", (int64_t)ceil(max_duration));
    } else if (seg->list_type == LIST_TYPE_FFCONCAT) {
        avio_printf(seg->list_pb, "ffconcat version 1.0\n");
    }

    return ret;
}

static void segment_list_print_entry(AVIOContext      *list_ioctx,
                                     ListType          list_type,
                                     const SegmentListEntry *list_entry,
                                     void *log_ctx)
{
    switch (list_type) {
    case LIST_TYPE_FLAT:
        avio_printf(list_ioctx, "%s\n", list_entry->filename);
        break;
    case LIST_TYPE_CSV:
    case LIST_TYPE_EXT:
        print_csv_escaped_str(list_ioctx, list_entry->filename);
        avio_printf(list_ioctx, ",%f,%f\n", list_entry->start_time, list_entry->end_time);
        break;
    case LIST_TYPE_M3U8:
        avio_printf(list_ioctx, "#EXTINF:%f,\n%s\n",
                    list_entry->end_time - list_entry->start_time, list_entry->filename);
        break;
    case LIST_TYPE_FFCONCAT:
    {
        char *buf;
        if (av_escape(&buf, list_entry->filename, NULL, AV_ESCAPE_MODE_AUTO, AV_ESCAPE_FLAG_WHITESPACE) < 0) {
            av_log(log_ctx, AV_LOG_WARNING,
                   "Error writing list entry '%s' in list file\n", list_entry->filename);
            return;
        }
        avio_printf(list_ioctx, "file %s\n", buf);
        av_free(buf);
        break;
    }
    default:
        av_assert0(!"Invalid list type");
    }
}

static int segment_end(AVFormatContext *s, int write_trailer, int is_last)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    int ret = 0;

    av_write_frame(oc, NULL); /* Flush any buffered data (fragmented mp4) */
    if (write_trailer)
        ret = av_write_trailer(oc);

    if (ret < 0)
        av_log(s, AV_LOG_ERROR, "Failure occurred when ending segment '%s'\n",
               oc->filename);

    if (seg->list) {
        if (seg->list_size || seg->list_type == LIST_TYPE_M3U8) {
            SegmentListEntry *entry = av_mallocz(sizeof(*entry));
            if (!entry) {
                ret = AVERROR(ENOMEM);
                goto end;
            }

            /* append new element */
            memcpy(entry, &seg->cur_entry, sizeof(*entry));
            if (!seg->segment_list_entries)
                seg->segment_list_entries = seg->segment_list_entries_end = entry;
            else
                seg->segment_list_entries_end->next = entry;
            seg->segment_list_entries_end = entry;

            /* drop first item */
            if (seg->list_size && seg->segment_count > seg->list_size) {
                entry = seg->segment_list_entries;
                seg->segment_list_entries = seg->segment_list_entries->next;
                av_free(entry->filename);
                av_freep(&entry);
            }

            avio_close(seg->list_pb);
            if ((ret = segment_list_open(s)) < 0)
                goto end;
            for (entry = seg->segment_list_entries; entry; entry = entry->next)
                segment_list_print_entry(seg->list_pb, seg->list_type, entry, s);
            if (seg->list_type == LIST_TYPE_M3U8 && is_last)
                avio_printf(seg->list_pb, "#EXT-X-ENDLIST\n");
        } else {
            segment_list_print_entry(seg->list_pb, seg->list_type, &seg->cur_entry, s);
        }
        avio_flush(seg->list_pb);
    }

    av_log(s, AV_LOG_VERBOSE, "segment:'%s' count:%d ended\n",
           seg->avf->filename, seg->segment_count);
    seg->segment_count++;

end:
    avio_close(oc->pb);

    return ret;
}

static int parse_times(void *log_ctx, int64_t **times, int *nb_times,
                       const char *times_str)
{
    char *p;
    int i, ret = 0;
    char *times_str1 = av_strdup(times_str);
    char *saveptr = NULL;

    if (!times_str1)
        return AVERROR(ENOMEM);

#define FAIL(err) ret = err; goto end

    *nb_times = 1;
    for (p = times_str1; *p; p++)
        if (*p == ',')
            (*nb_times)++;

    *times = av_malloc_array(*nb_times, sizeof(**times));
    if (!*times) {
        av_log(log_ctx, AV_LOG_ERROR, "Could not allocate forced times array\n");
        FAIL(AVERROR(ENOMEM));
    }

    p = times_str1;
    for (i = 0; i < *nb_times; i++) {
        int64_t t;
        char *tstr = av_strtok(p, ",", &saveptr);
        p = NULL;

        if (!tstr || !tstr[0]) {
            av_log(log_ctx, AV_LOG_ERROR, "Empty time specification in times list %s\n",
                   times_str);
            FAIL(AVERROR(EINVAL));
        }

        ret = av_parse_time(&t, tstr, 1);
        if (ret < 0) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Invalid time duration specification '%s' in times list %s\n", tstr, times_str);
            FAIL(AVERROR(EINVAL));
        }
        (*times)[i] = t;

        /* check on monotonicity */
        if (i && (*times)[i-1] > (*times)[i]) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Specified time %f is greater than the following time %f\n",
                   (float)((*times)[i])/1000000, (float)((*times)[i-1])/1000000);
            FAIL(AVERROR(EINVAL));
        }
    }

end:
    av_free(times_str1);
    return ret;
}

static int parse_frames(void *log_ctx, int **frames, int *nb_frames,
                        const char *frames_str)
{
    char *p;
    int i, ret = 0;
    char *frames_str1 = av_strdup(frames_str);
    char *saveptr = NULL;

    if (!frames_str1)
        return AVERROR(ENOMEM);

#define FAIL(err) ret = err; goto end

    *nb_frames = 1;
    for (p = frames_str1; *p; p++)
        if (*p == ',')
            (*nb_frames)++;

    *frames = av_malloc_array(*nb_frames, sizeof(**frames));
    if (!*frames) {
        av_log(log_ctx, AV_LOG_ERROR, "Could not allocate forced frames array\n");
        FAIL(AVERROR(ENOMEM));
    }

    p = frames_str1;
    for (i = 0; i < *nb_frames; i++) {
        long int f;
        char *tailptr;
        char *fstr = av_strtok(p, ",", &saveptr);

        p = NULL;
        if (!fstr) {
            av_log(log_ctx, AV_LOG_ERROR, "Empty frame specification in frame list %s\n",
                   frames_str);
            FAIL(AVERROR(EINVAL));
        }
        f = strtol(fstr, &tailptr, 10);
        if (*tailptr || f <= 0 || f >= INT_MAX) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Invalid argument '%s', must be a positive integer <= INT64_MAX\n",
                   fstr);
            FAIL(AVERROR(EINVAL));
        }
        (*frames)[i] = f;

        /* check on monotonicity */
        if (i && (*frames)[i-1] > (*frames)[i]) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Specified frame %d is greater than the following frame %d\n",
                   (*frames)[i], (*frames)[i-1]);
            FAIL(AVERROR(EINVAL));
        }
    }

end:
    av_free(frames_str1);
    return ret;
}

static int open_null_ctx(AVIOContext **ctx)
{
    int buf_size = 32768;
    uint8_t *buf = av_malloc(buf_size);
    if (!buf)
        return AVERROR(ENOMEM);
    *ctx = avio_alloc_context(buf, buf_size, AVIO_FLAG_WRITE, NULL, NULL, NULL, NULL);
    if (!*ctx) {
        av_free(buf);
        return AVERROR(ENOMEM);
    }
    return 0;
}

static void close_null_ctx(AVIOContext *pb)
{
    av_free(pb->buffer);
    av_free(pb);
}

static int select_reference_stream(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    int ret, i;

    seg->reference_stream_index = -1;
    if (!strcmp(seg->reference_stream_specifier, "auto")) {
        /* select first index of type with highest priority */
        int type_index_map[AVMEDIA_TYPE_NB];
        static const enum AVMediaType type_priority_list[] = {
            AVMEDIA_TYPE_VIDEO,
            AVMEDIA_TYPE_AUDIO,
            AVMEDIA_TYPE_SUBTITLE,
            AVMEDIA_TYPE_DATA,
            AVMEDIA_TYPE_ATTACHMENT
        };
        enum AVMediaType type;

        for (i = 0; i < AVMEDIA_TYPE_NB; i++)
            type_index_map[i] = -1;

        /* select first index for each type */
        for (i = 0; i < s->nb_streams; i++) {
            type = s->streams[i]->codec->codec_type;
            if ((unsigned)type < AVMEDIA_TYPE_NB && type_index_map[type] == -1
                /* ignore attached pictures/cover art streams */
                && !(s->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC))
                type_index_map[type] = i;
        }

        for (i = 0; i < FF_ARRAY_ELEMS(type_priority_list); i++) {
            type = type_priority_list[i];
            if ((seg->reference_stream_index = type_index_map[type]) >= 0)
                break;
        }
    } else {
        for (i = 0; i < s->nb_streams; i++) {
            ret = avformat_match_stream_specifier(s, s->streams[i],
                                                  seg->reference_stream_specifier);
            if (ret < 0)
                return ret;
            if (ret > 0) {
                seg->reference_stream_index = i;
                break;
            }
        }
    }

    if (seg->reference_stream_index < 0) {
        av_log(s, AV_LOG_ERROR, "Could not select stream matching identifier '%s'\n",
               seg->reference_stream_specifier);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int seg_write_header(AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = NULL;
    int ret;

    seg->segment_count = 0;
    if (!seg->write_header_trailer)
        seg->individual_header_trailer = 0;

    if (!!seg->time_str + !!seg->times_str + !!seg->frames_str > 1) {
        av_log(s, AV_LOG_ERROR,
               "segment_time, segment_times, and segment_frames options "
               "are mutually exclusive, select just one of them\n");
        return AVERROR(EINVAL);
    }

    if (seg->times_str) {
        if ((ret = parse_times(s, &seg->times, &seg->nb_times, seg->times_str)) < 0)
            return ret;
    } else if (seg->frames_str) {
        if ((ret = parse_frames(s, &seg->frames, &seg->nb_frames, seg->frames_str)) < 0)
            return ret;
    } else {
        /* set default value if not specified */
        if (!seg->time_str)
            seg->time_str = av_strdup("2");
        if ((ret = av_parse_time(&seg->time, seg->time_str, 1)) < 0) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid time duration specification '%s' for segment_time option\n",
                   seg->time_str);
            return ret;
        }
    }

    if (seg->list) {
        if (seg->list_type == LIST_TYPE_UNDEFINED) {
            if      (av_match_ext(seg->list, "csv" )) seg->list_type = LIST_TYPE_CSV;
            else if (av_match_ext(seg->list, "ext" )) seg->list_type = LIST_TYPE_EXT;
            else if (av_match_ext(seg->list, "m3u8")) seg->list_type = LIST_TYPE_M3U8;
            else if (av_match_ext(seg->list, "ffcat,ffconcat")) seg->list_type = LIST_TYPE_FFCONCAT;
            else                                      seg->list_type = LIST_TYPE_FLAT;
        }
        if ((ret = segment_list_open(s)) < 0)
            goto fail;
    }
    if (seg->list_type == LIST_TYPE_EXT)
        av_log(s, AV_LOG_WARNING, "'ext' list type option is deprecated in favor of 'csv'\n");

    if ((ret = select_reference_stream(s)) < 0)
        goto fail;
    av_log(s, AV_LOG_VERBOSE, "Selected stream id:%d type:%s\n",
           seg->reference_stream_index,
           av_get_media_type_string(s->streams[seg->reference_stream_index]->codec->codec_type));

    seg->oformat = av_guess_format(seg->format, s->filename, NULL);

    if (!seg->oformat) {
        ret = AVERROR_MUXER_NOT_FOUND;
        goto fail;
    }
    if (seg->oformat->flags & AVFMT_NOFILE) {
        av_log(s, AV_LOG_ERROR, "format %s not supported.\n",
               seg->oformat->name);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if ((ret = segment_mux_init(s)) < 0)
        goto fail;
    oc = seg->avf;

    if ((ret = set_segment_filename(s)) < 0)
        goto fail;

    if (seg->write_header_trailer) {
        if ((ret = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                              &s->interrupt_callback, NULL)) < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to open segment '%s'\n", oc->filename);
            goto fail;
        }
    } else {
        if ((ret = open_null_ctx(&oc->pb)) < 0)
            goto fail;
    }

    if ((ret = avformat_write_header(oc, NULL)) < 0) {
        avio_close(oc->pb);
        goto fail;
    }
    seg->is_first_pkt = 1;

    if (oc->avoid_negative_ts > 0 && s->avoid_negative_ts < 0)
        s->avoid_negative_ts = 1;

    if (!seg->write_header_trailer) {
        close_null_ctx(oc->pb);
        if ((ret = avio_open2(&oc->pb, oc->filename, AVIO_FLAG_WRITE,
                              &s->interrupt_callback, NULL)) < 0)
            goto fail;
    }

fail:
    if (ret) {
        if (seg->list)
            avio_close(seg->list_pb);
        if (seg->avf)
            avformat_free_context(seg->avf);
    }
    return ret;
}

static int seg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SegmentContext *seg = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    int64_t end_pts = INT64_MAX, offset;
    int start_frame = INT_MAX;
    int ret;
    struct tm ti;
    int64_t usecs;
    int64_t wrapped_val;

    if (seg->times) {
        end_pts = seg->segment_count < seg->nb_times ?
            seg->times[seg->segment_count] : INT64_MAX;
    } else if (seg->frames) {
        start_frame = seg->segment_count < seg->nb_frames ?
            seg->frames[seg->segment_count] : INT_MAX;
    } else {
        if (seg->use_clocktime) {
            int64_t avgt = av_gettime();
            time_t sec = avgt / 1000000;
#if HAVE_LOCALTIME_R
            localtime_r(&sec, &ti);
#else
            ti = *localtime(&sec);
#endif
            usecs = (int64_t)(ti.tm_hour*3600 + ti.tm_min*60 + ti.tm_sec) * 1000000 + (avgt % 1000000);
            wrapped_val = usecs % seg->time;
            if (seg->last_cut != usecs && wrapped_val < seg->last_val) {
                seg->cut_pending = 1;
                seg->last_cut = usecs;
            }
            seg->last_val = wrapped_val;
        } else {
            end_pts = seg->time * (seg->segment_count+1);
        }
    }

    av_dlog(s, "packet stream:%d pts:%s pts_time:%s is_key:%d frame:%d\n",
           pkt->stream_index, av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &st->time_base),
           pkt->flags & AV_PKT_FLAG_KEY,
           pkt->stream_index == seg->reference_stream_index ? seg->frame_count : -1);

    if (pkt->stream_index == seg->reference_stream_index &&
        pkt->flags & AV_PKT_FLAG_KEY &&
        (seg->cut_pending || seg->frame_count >= start_frame ||
         (pkt->pts != AV_NOPTS_VALUE &&
          av_compare_ts(pkt->pts, st->time_base,
                        end_pts-seg->time_delta, AV_TIME_BASE_Q) >= 0))) {
        if ((ret = segment_end(s, seg->individual_header_trailer, 0)) < 0)
            goto fail;

        if ((ret = segment_start(s, seg->individual_header_trailer)) < 0)
            goto fail;

        seg->cut_pending = 0;
        seg->cur_entry.index = seg->segment_idx + seg->segment_idx_wrap*seg->segment_idx_wrap_nb;
        seg->cur_entry.start_time = (double)pkt->pts * av_q2d(st->time_base);
        seg->cur_entry.start_pts = av_rescale_q(pkt->pts, st->time_base, AV_TIME_BASE_Q);
    } else if (pkt->pts != AV_NOPTS_VALUE) {
        seg->cur_entry.end_time =
            FFMAX(seg->cur_entry.end_time, (double)(pkt->pts + pkt->duration) * av_q2d(st->time_base));
    }

    if (seg->is_first_pkt) {
        av_log(s, AV_LOG_VERBOSE, "segment:'%s' starts with packet stream:%d pts:%s pts_time:%s frame:%d\n",
               seg->avf->filename, pkt->stream_index,
               av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &st->time_base), seg->frame_count);
        seg->is_first_pkt = 0;
    }

    av_log(s, AV_LOG_DEBUG, "stream:%d start_pts_time:%s pts:%s pts_time:%s dts:%s dts_time:%s",
           pkt->stream_index,
           av_ts2timestr(seg->cur_entry.start_pts, &AV_TIME_BASE_Q),
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &st->time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &st->time_base));

    /* compute new timestamps */
    offset = av_rescale_q(seg->initial_offset - (seg->reset_timestamps ? seg->cur_entry.start_pts : 0),
                          AV_TIME_BASE_Q, st->time_base);
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts += offset;
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += offset;

    av_log(s, AV_LOG_DEBUG, " -> pts:%s pts_time:%s dts:%s dts_time:%s\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &st->time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &st->time_base));

    ret = ff_write_chained(seg->avf, pkt->stream_index, pkt, s);

fail:
    if (pkt->stream_index == seg->reference_stream_index)
        seg->frame_count++;

    return ret;
}

static int seg_write_trailer(struct AVFormatContext *s)
{
    SegmentContext *seg = s->priv_data;
    AVFormatContext *oc = seg->avf;
    SegmentListEntry *cur, *next;

    int ret;
    if (!seg->write_header_trailer) {
        if ((ret = segment_end(s, 0, 1)) < 0)
            goto fail;
        open_null_ctx(&oc->pb);
        ret = av_write_trailer(oc);
        close_null_ctx(oc->pb);
    } else {
        ret = segment_end(s, 1, 1);
    }
fail:
    if (seg->list)
        avio_close(seg->list_pb);

    av_opt_free(seg);
    av_freep(&seg->times);
    av_freep(&seg->frames);

    cur = seg->segment_list_entries;
    while (cur) {
        next = cur->next;
        av_free(cur->filename);
        av_free(cur);
        cur = next;
    }

    avformat_free_context(oc);
    return ret;
}

#define OFFSET(x) offsetof(SegmentContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "reference_stream",  "set reference stream", OFFSET(reference_stream_specifier), AV_OPT_TYPE_STRING, {.str = "auto"}, CHAR_MIN, CHAR_MAX, E },
    { "segment_format",    "set container format used for the segments", OFFSET(format),  AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },
    { "segment_list",      "set the segment list filename",              OFFSET(list),    AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },

    { "segment_list_flags","set flags affecting segment list generation", OFFSET(list_flags), AV_OPT_TYPE_FLAGS, {.i64 = SEGMENT_LIST_FLAG_CACHE }, 0, UINT_MAX, E, "list_flags"},
    { "cache",             "allow list caching",                                    0, AV_OPT_TYPE_CONST, {.i64 = SEGMENT_LIST_FLAG_CACHE }, INT_MIN, INT_MAX,   E, "list_flags"},
    { "live",              "enable live-friendly list generation (useful for HLS)", 0, AV_OPT_TYPE_CONST, {.i64 = SEGMENT_LIST_FLAG_LIVE }, INT_MIN, INT_MAX,    E, "list_flags"},

    { "segment_list_size", "set the maximum number of playlist entries", OFFSET(list_size), AV_OPT_TYPE_INT,  {.i64 = 0},     0, INT_MAX, E },

    { "segment_list_type", "set the segment list type",                  OFFSET(list_type), AV_OPT_TYPE_INT,  {.i64 = LIST_TYPE_UNDEFINED}, -1, LIST_TYPE_NB-1, E, "list_type" },
    { "flat", "flat format",     0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_FLAT }, INT_MIN, INT_MAX, E, "list_type" },
    { "csv",  "csv format",      0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_CSV  }, INT_MIN, INT_MAX, E, "list_type" },
    { "ext",  "extended format", 0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_EXT  }, INT_MIN, INT_MAX, E, "list_type" },
    { "ffconcat", "ffconcat format", 0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_FFCONCAT }, INT_MIN, INT_MAX, E, "list_type" },
    { "m3u8", "M3U8 format",     0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_M3U8 }, INT_MIN, INT_MAX, E, "list_type" },
    { "hls", "Apple HTTP Live Streaming compatible", 0, AV_OPT_TYPE_CONST, {.i64=LIST_TYPE_M3U8 }, INT_MIN, INT_MAX, E, "list_type" },

    { "segment_atclocktime",      "set segment to be cut at clocktime",  OFFSET(use_clocktime), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, E},
    { "segment_time",      "set segment duration",                       OFFSET(time_str),AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       E },
    { "segment_time_delta","set approximation value used for the segment times", OFFSET(time_delta), AV_OPT_TYPE_DURATION, {.i64 = 0}, 0, 0, E },
    { "segment_times",     "set segment split time points",              OFFSET(times_str),AV_OPT_TYPE_STRING,{.str = NULL},  0, 0,       E },
    { "segment_frames",    "set segment split frame numbers",            OFFSET(frames_str),AV_OPT_TYPE_STRING,{.str = NULL},  0, 0,       E },
    { "segment_wrap",      "set number after which the index wraps",     OFFSET(segment_idx_wrap), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, E },
    { "segment_list_entry_prefix", "set base url prefix for segments", OFFSET(entry_prefix), AV_OPT_TYPE_STRING,  {.str = NULL}, 0, 0, E },
    { "segment_start_number", "set the sequence number of the first segment", OFFSET(segment_idx), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, E },
    { "segment_wrap_number", "set the number of wrap before the first segment", OFFSET(segment_idx_wrap_nb), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, E },

    { "individual_header_trailer", "write header/trailer to each segment", OFFSET(individual_header_trailer), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, E },
    { "write_header_trailer", "write a header to the first segment and a trailer to the last one", OFFSET(write_header_trailer), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, E },
    { "reset_timestamps", "reset timestamps at the begin of each segment", OFFSET(reset_timestamps), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, E },
    { "initial_offset", "set initial timestamp offset", OFFSET(initial_offset), AV_OPT_TYPE_DURATION, {.i64 = 0}, -INT64_MAX, INT64_MAX, E },
    { NULL },
};

static const AVClass seg_class = {
    .class_name = "segment muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_segment_muxer = {
    .name           = "segment",
    .long_name      = NULL_IF_CONFIG_SMALL("segment"),
    .priv_data_size = sizeof(SegmentContext),
    .flags          = AVFMT_NOFILE|AVFMT_GLOBALHEADER,
    .write_header   = seg_write_header,
    .write_packet   = seg_write_packet,
    .write_trailer  = seg_write_trailer,
    .priv_class     = &seg_class,
};

static const AVClass sseg_class = {
    .class_name = "stream_segment muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_stream_segment_muxer = {
    .name           = "stream_segment,ssegment",
    .long_name      = NULL_IF_CONFIG_SMALL("streaming segment muxer"),
    .priv_data_size = sizeof(SegmentContext),
    .flags          = AVFMT_NOFILE,
    .write_header   = seg_write_header,
    .write_packet   = seg_write_packet,
    .write_trailer  = seg_write_trailer,
    .priv_class     = &sseg_class,
};
