/*
 * Copyright (c) 2012 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avformat.h"
#include "internal.h"
#include "url.h"

typedef struct {
    char *url;
    int64_t start_time;
    int64_t duration;
    int *stream_map;
    int stream_map_size;
} ConcatFile;

typedef struct {
    AVClass *class;
    ConcatFile *files;
    ConcatFile *cur_file;
    unsigned nb_files;
    AVFormatContext *avf;
    int safe;
    int seekable;
    int match_streams;
} ConcatContext;

static int concat_probe(AVProbeData *probe)
{
    return memcmp(probe->buf, "ffconcat version 1.0", 20) ?
           0 : AVPROBE_SCORE_MAX;
}

static char *get_keyword(uint8_t **cursor)
{
    char *ret = *cursor += strspn(*cursor, SPACE_CHARS);
    *cursor += strcspn(*cursor, SPACE_CHARS);
    if (**cursor) {
        *((*cursor)++) = 0;
        *cursor += strspn(*cursor, SPACE_CHARS);
    }
    return ret;
}

static int safe_filename(const char *f)
{
    const char *start = f;

    for (; *f; f++) {
        /* A-Za-z0-9_- */
        if (!((unsigned)((*f | 32) - 'a') < 26 ||
              (unsigned)(*f - '0') < 10 || *f == '_' || *f == '-')) {
            if (f == start)
                return 0;
            else if (*f == '/')
                start = f + 1;
            else if (*f != '.')
                return 0;
        }
    }
    return 1;
}

#define FAIL(retcode) do { ret = (retcode); goto fail; } while(0)

static int add_file(AVFormatContext *avf, char *filename, ConcatFile **rfile,
                    unsigned *nb_files_alloc)
{
    ConcatContext *cat = avf->priv_data;
    ConcatFile *file;
    char *url = NULL;
    const char *proto;
    size_t url_len, proto_len;
    int ret;

    if (cat->safe > 0 && !safe_filename(filename)) {
        av_log(avf, AV_LOG_ERROR, "Unsafe file name '%s'\n", filename);
        FAIL(AVERROR(EPERM));
    }

    proto = avio_find_protocol_name(filename);
    proto_len = proto ? strlen(proto) : 0;
    if (!memcmp(filename, proto, proto_len) &&
        (filename[proto_len] == ':' || filename[proto_len] == ',')) {
        url = filename;
        filename = NULL;
    } else {
        url_len = strlen(avf->filename) + strlen(filename) + 16;
        if (!(url = av_malloc(url_len)))
            FAIL(AVERROR(ENOMEM));
        ff_make_absolute_url(url, url_len, avf->filename, filename);
        av_freep(&filename);
    }

    if (cat->nb_files >= *nb_files_alloc) {
        size_t n = FFMAX(*nb_files_alloc * 2, 16);
        ConcatFile *new_files;
        if (n <= cat->nb_files || n > SIZE_MAX / sizeof(*cat->files) ||
            !(new_files = av_realloc(cat->files, n * sizeof(*cat->files))))
            FAIL(AVERROR(ENOMEM));
        cat->files = new_files;
        *nb_files_alloc = n;
    }

    file = &cat->files[cat->nb_files++];
    memset(file, 0, sizeof(*file));
    *rfile = file;

    file->url        = url;
    file->start_time = AV_NOPTS_VALUE;
    file->duration   = AV_NOPTS_VALUE;

    return 0;

fail:
    av_free(url);
    av_free(filename);
    return ret;
}

static int copy_stream_props(AVStream *st, AVStream *source_st)
{
    int ret;

    if ((ret = avcodec_copy_context(st->codec, source_st->codec)) < 0)
        return ret;
    st->r_frame_rate        = source_st->r_frame_rate;
    st->avg_frame_rate      = source_st->avg_frame_rate;
    st->time_base           = source_st->time_base;
    st->sample_aspect_ratio = source_st->sample_aspect_ratio;
    return 0;
}

static int match_streams(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    AVStream *st;
    int *map, i, j, ret;

    if (!cat->match_streams ||
        cat->cur_file->stream_map_size >= cat->avf->nb_streams)
        return 0;
    map = av_realloc(cat->cur_file->stream_map,
                     cat->avf->nb_streams * sizeof(*map));
    if (!map)
        return AVERROR(ENOMEM);
    cat->cur_file->stream_map = map;

    for (i = cat->cur_file->stream_map_size; i < cat->avf->nb_streams; i++) {
        st = cat->avf->streams[i];
        map[i] = -1;
        for (j = 0; j < avf->nb_streams; j++) {
            if (avf->streams[j]->id == st->id) {
                av_log(avf, AV_LOG_VERBOSE,
                       "Match slave stream #%d with stream #%d id 0x%x\n",
                       i, j, st->id);
                map[i] = j;
                if (!avf->streams[j]->codec->codec_id && st->codec->codec_id)
                    if ((ret = copy_stream_props(avf->streams[j], st)) < 0)
                        return ret;
            }
        }
    }

    cat->cur_file->stream_map_size = cat->avf->nb_streams;
    return 0;
}

static int open_file(AVFormatContext *avf, unsigned fileno)
{
    ConcatContext *cat = avf->priv_data;
    ConcatFile *file = &cat->files[fileno];
    int ret;

    if (cat->avf)
        avformat_close_input(&cat->avf);

    cat->avf = avformat_alloc_context();
    if (!cat->avf)
        return AVERROR(ENOMEM);

    cat->avf->interrupt_callback = avf->interrupt_callback;
    if ((ret = avformat_open_input(&cat->avf, file->url, NULL, NULL)) < 0 ||
        (ret = avformat_find_stream_info(cat->avf, NULL)) < 0) {
        av_log(avf, AV_LOG_ERROR, "Impossible to open '%s'\n", file->url);
        avformat_close_input(&cat->avf);
        return ret;
    }
    cat->cur_file = file;
    if (file->start_time == AV_NOPTS_VALUE)
        file->start_time = !fileno ? 0 :
                           cat->files[fileno - 1].start_time +
                           cat->files[fileno - 1].duration;
    if ((ret = match_streams(avf)) < 0)
        return ret;
    return 0;
}

static int concat_read_close(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    unsigned i;

    if (cat->avf)
        avformat_close_input(&cat->avf);
    for (i = 0; i < cat->nb_files; i++)
        av_freep(&cat->files[i].url);
    av_freep(&cat->files);
    return 0;
}

static int concat_read_header(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    uint8_t buf[4096];
    uint8_t *cursor, *keyword;
    int ret, line = 0, i;
    unsigned nb_files_alloc = 0;
    ConcatFile *file = NULL;
    AVStream *st;
    int64_t time = 0;

    while (1) {
        if ((ret = ff_get_line(avf->pb, buf, sizeof(buf))) <= 0)
            break;
        line++;
        cursor = buf;
        keyword = get_keyword(&cursor);
        if (!*keyword || *keyword == '#')
            continue;

        if (!strcmp(keyword, "file")) {
            char *filename = av_get_token((const char **)&cursor, SPACE_CHARS);
            if (!filename) {
                av_log(avf, AV_LOG_ERROR, "Line %d: filename required\n", line);
                FAIL(AVERROR_INVALIDDATA);
            }
            if ((ret = add_file(avf, filename, &file, &nb_files_alloc)) < 0)
                FAIL(ret);
        } else if (!strcmp(keyword, "duration")) {
            char *dur_str = get_keyword(&cursor);
            int64_t dur;
            if (!file) {
                av_log(avf, AV_LOG_ERROR, "Line %d: duration without file\n",
                       line);
                FAIL(AVERROR_INVALIDDATA);
            }
            if ((ret = av_parse_time(&dur, dur_str, 1)) < 0) {
                av_log(avf, AV_LOG_ERROR, "Line %d: invalid duration '%s'\n",
                       line, dur_str);
                FAIL(ret);
            }
            file->duration = dur;
        } else if (!strcmp(keyword, "stream")) {
            if (!avformat_new_stream(avf, NULL))
                FAIL(AVERROR(ENOMEM));
        } else if (!strcmp(keyword, "exact_stream_id")) {
            if (!avf->nb_streams) {
                av_log(avf, AV_LOG_ERROR, "Line %d: exact_stream_id without stream\n",
                       line);
                FAIL(AVERROR_INVALIDDATA);
            }
            avf->streams[avf->nb_streams - 1]->id =
                strtol(get_keyword(&cursor), NULL, 0);
        } else if (!strcmp(keyword, "ffconcat")) {
            char *ver_kw  = get_keyword(&cursor);
            char *ver_val = get_keyword(&cursor);
            if (strcmp(ver_kw, "version") || strcmp(ver_val, "1.0")) {
                av_log(avf, AV_LOG_ERROR, "Line %d: invalid version\n", line);
                FAIL(AVERROR_INVALIDDATA);
            }
            if (cat->safe < 0)
                cat->safe = 1;
        } else {
            av_log(avf, AV_LOG_ERROR, "Line %d: unknown keyword '%s'\n",
                   line, keyword);
            FAIL(AVERROR_INVALIDDATA);
        }
    }
    if (ret < 0)
        FAIL(ret);
    if (!cat->nb_files)
        FAIL(AVERROR_INVALIDDATA);

    for (i = 0; i < cat->nb_files; i++) {
        if (cat->files[i].start_time == AV_NOPTS_VALUE)
            cat->files[i].start_time = time;
        else
            time = cat->files[i].start_time;
        if (cat->files[i].duration == AV_NOPTS_VALUE)
            break;
        time += cat->files[i].duration;
    }
    if (i == cat->nb_files) {
        avf->duration = time;
        cat->seekable = 1;
    }

    cat->match_streams = !!avf->nb_streams;
    if ((ret = open_file(avf, 0)) < 0)
        FAIL(ret);
    if (!cat->match_streams) {
        for (i = 0; i < cat->avf->nb_streams; i++) {
            if (!(st = avformat_new_stream(avf, NULL)))
                FAIL(AVERROR(ENOMEM));
            if ((ret = copy_stream_props(st, cat->avf->streams[i])) < 0)
                FAIL(ret);
        }
    }

    return 0;

fail:
    concat_read_close(avf);
    return ret;
}

static int open_next_file(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    unsigned fileno = cat->cur_file - cat->files;

    if (cat->cur_file->duration == AV_NOPTS_VALUE)
        cat->cur_file->duration = cat->avf->duration;

    if (++fileno >= cat->nb_files)
        return AVERROR_EOF;
    return open_file(avf, fileno);
}

static int concat_read_packet(AVFormatContext *avf, AVPacket *pkt)
{
    ConcatContext *cat = avf->priv_data;
    int ret;
    int64_t delta;

    while (1) {
        ret = av_read_frame(cat->avf, pkt);
        if (ret == AVERROR_EOF) {
            if ((ret = open_next_file(avf)) < 0)
                return ret;
            continue;
        }
        if (ret < 0)
            return ret;
        if (cat->match_streams) {
            match_streams(avf);
            pkt->stream_index = cat->cur_file->stream_map[pkt->stream_index];
            if (pkt->stream_index < 0) {
                av_packet_unref(pkt);
                continue;
            }
        }
        break;
    }

    delta = av_rescale_q(cat->cur_file->start_time - cat->avf->start_time,
                         AV_TIME_BASE_Q,
                         cat->avf->streams[pkt->stream_index]->time_base);
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts += delta;
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += delta;
    return ret;
}

static void rescale_interval(AVRational tb_in, AVRational tb_out,
                             int64_t *min_ts, int64_t *ts, int64_t *max_ts)
{
    *ts     = av_rescale_q    (*    ts, tb_in, tb_out);
    *min_ts = av_rescale_q_rnd(*min_ts, tb_in, tb_out,
                               AV_ROUND_UP   | AV_ROUND_PASS_MINMAX);
    *max_ts = av_rescale_q_rnd(*max_ts, tb_in, tb_out,
                               AV_ROUND_DOWN | AV_ROUND_PASS_MINMAX);
}

static int try_seek(AVFormatContext *avf, int stream,
                    int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    ConcatContext *cat = avf->priv_data;
    int64_t t0 = cat->cur_file->start_time - cat->avf->start_time;

    ts -= t0;
    min_ts = min_ts == INT64_MIN ? INT64_MIN : min_ts - t0;
    max_ts = max_ts == INT64_MAX ? INT64_MAX : max_ts - t0;
    if (stream >= 0) {
        if (stream >= cat->avf->nb_streams)
            return AVERROR(EIO);
        rescale_interval(AV_TIME_BASE_Q, cat->avf->streams[stream]->time_base,
                         &min_ts, &ts, &max_ts);
    }
    return avformat_seek_file(cat->avf, stream, min_ts, ts, max_ts, flags);
}

static int real_seek(AVFormatContext *avf, int stream,
                     int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    ConcatContext *cat = avf->priv_data;
    int ret, left, right;

    if (stream >= 0) {
        if (stream >= avf->nb_streams)
            return AVERROR(EINVAL);
        rescale_interval(avf->streams[stream]->time_base, AV_TIME_BASE_Q,
                         &min_ts, &ts, &max_ts);
    }

    left  = 0;
    right = cat->nb_files;
    while (right - left > 1) {
        int mid = (left + right) / 2;
        if (ts < cat->files[mid].start_time)
            right = mid;
        else
            left  = mid;
    }

    if ((ret = open_file(avf, left)) < 0)
        return ret;

    ret = try_seek(avf, stream, min_ts, ts, max_ts, flags);
    if (ret < 0 &&
        left < cat->nb_files - 1 &&
        cat->files[left + 1].start_time < max_ts) {
        if ((ret = open_file(avf, left + 1)) < 0)
            return ret;
        ret = try_seek(avf, stream, min_ts, ts, max_ts, flags);
    }
    return ret;
}

static int concat_seek(AVFormatContext *avf, int stream,
                       int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    ConcatContext *cat = avf->priv_data;
    ConcatFile *cur_file_saved = cat->cur_file;
    AVFormatContext *cur_avf_saved = cat->avf;
    int ret;

    if (!cat->seekable)
        return AVERROR(ESPIPE); /* XXX: can we use it? */
    if (flags & (AVSEEK_FLAG_BYTE | AVSEEK_FLAG_FRAME))
        return AVERROR(ENOSYS);
    cat->avf = NULL;
    if ((ret = real_seek(avf, stream, min_ts, ts, max_ts, flags)) < 0) {
        if (cat->avf)
            avformat_close_input(&cat->avf);
        cat->avf      = cur_avf_saved;
        cat->cur_file = cur_file_saved;
    } else {
        avformat_close_input(&cur_avf_saved);
    }
    return ret;
}

#define OFFSET(x) offsetof(ConcatContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "safe", "enable safe mode",
      OFFSET(safe), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 1, DEC },
    { NULL }
};

static const AVClass concat_class = {
    .class_name = "concat demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};


AVInputFormat ff_concat_demuxer = {
    .name           = "concat",
    .long_name      = NULL_IF_CONFIG_SMALL("Virtual concatenation script"),
    .priv_data_size = sizeof(ConcatContext),
    .read_probe     = concat_probe,
    .read_header    = concat_read_header,
    .read_packet    = concat_read_packet,
    .read_close     = concat_read_close,
    .read_seek2     = concat_seek,
    .priv_class     = &concat_class,
};
