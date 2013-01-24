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
#include "avformat.h"
#include "internal.h"

typedef struct {
    char *url;
    int64_t start_time;
    int64_t duration;
} ConcatFile;

typedef struct {
    ConcatFile *files;
    ConcatFile *cur_file;
    unsigned nb_files;
    AVFormatContext *avf;
} ConcatContext;

static int concat_probe(AVProbeData *probe)
{
    return 0;
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

#define FAIL(retcode) do { ret = (retcode); goto fail; } while(0)

static int add_file(AVFormatContext *avf, char *filename, ConcatFile **rfile,
                    unsigned *nb_files_alloc)
{
    ConcatContext *cat = avf->priv_data;
    ConcatFile *file;
    char *url;
    size_t url_len;

    url_len = strlen(avf->filename) + strlen(filename) + 16;
    if (!(url = av_malloc(url_len)))
        return AVERROR(ENOMEM);
    ff_make_absolute_url(url, url_len, avf->filename, filename);
    av_free(filename);

    if (cat->nb_files >= *nb_files_alloc) {
        size_t n = FFMAX(*nb_files_alloc * 2, 16);
        ConcatFile *new_files;
        if (n <= cat->nb_files || n > SIZE_MAX / sizeof(*cat->files) ||
            !(new_files = av_realloc(cat->files, n * sizeof(*cat->files))))
            return AVERROR(ENOMEM);
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
}

static int open_file(AVFormatContext *avf, unsigned fileno)
{
    ConcatContext *cat = avf->priv_data;
    ConcatFile *file = &cat->files[fileno];
    int ret;

    if ((ret = avformat_open_input(&cat->avf, file->url, NULL, NULL)) < 0 ||
        (ret = avformat_find_stream_info(cat->avf, NULL)) < 0) {
        av_log(avf, AV_LOG_ERROR, "Impossible to open '%s'\n", file->url);
        return ret;
    }
    cat->cur_file = file;
    if (file->start_time == AV_NOPTS_VALUE)
        file->start_time = !fileno ? 0 :
                           cat->files[fileno - 1].start_time +
                           cat->files[fileno - 1].duration;
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
    AVStream *st, *source_st;

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
        } else {
            av_log(avf, AV_LOG_ERROR, "Line %d: unknown keyword '%s'\n",
                   line, keyword);
            FAIL(AVERROR_INVALIDDATA);
        }
    }
    if (ret < 0)
        FAIL(ret);

    if ((ret = open_file(avf, 0)) < 0)
        FAIL(ret);
    for (i = 0; i < cat->avf->nb_streams; i++) {
        if (!(st = avformat_new_stream(avf, NULL)))
            FAIL(AVERROR(ENOMEM));
        source_st = cat->avf->streams[i];
        if ((ret = avcodec_copy_context(st->codec, source_st->codec)) < 0)
            FAIL(ret);
        st->r_frame_rate        = source_st->r_frame_rate;
        st->avg_frame_rate      = source_st->avg_frame_rate;
        st->time_base           = source_st->time_base;
        st->sample_aspect_ratio = source_st->sample_aspect_ratio;
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
    avformat_close_input(&cat->avf);
    return open_file(avf, fileno);
}

static int concat_read_packet(AVFormatContext *avf, AVPacket *pkt)
{
    ConcatContext *cat = avf->priv_data;
    int ret;
    int64_t delta;

    while (1) {
        if ((ret = av_read_frame(cat->avf, pkt)) != AVERROR_EOF ||
            (ret = open_next_file(avf)) < 0)
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

AVInputFormat ff_concat_demuxer = {
    .name           = "concat",
    .long_name      = NULL_IF_CONFIG_SMALL("Virtual concatenation script"),
    .priv_data_size = sizeof(ConcatContext),
    .read_probe     = concat_probe,
    .read_header    = concat_read_header,
    .read_packet    = concat_read_packet,
    .read_close     = concat_read_close,
};
