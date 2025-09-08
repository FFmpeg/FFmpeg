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
#include "libavutil/avassert.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/timestamp.h"
#include "libavcodec/codec_desc.h"
#include "libavcodec/bsf.h"
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"
#include "url.h"

typedef enum ConcatMatchMode {
    MATCH_ONE_TO_ONE,
    MATCH_EXACT_ID,
} ConcatMatchMode;

typedef struct ConcatStream {
    AVBSFContext *bsf;
    int out_stream_index;
} ConcatStream;

typedef struct {
    char *url;
    int64_t start_time;
    int64_t file_start_time;
    int64_t file_inpoint;
    int64_t duration;
    int64_t user_duration;
    int64_t next_dts;
    ConcatStream *streams;
    int64_t inpoint;
    int64_t outpoint;
    AVDictionary *metadata;
    AVDictionary *options;
    int nb_streams;
} ConcatFile;

typedef struct {
    AVClass *class;
    ConcatFile *files;
    ConcatFile *cur_file;
    unsigned nb_files;
    AVFormatContext *avf;
    int safe;
    int seekable;
    int eof;
    ConcatMatchMode stream_match_mode;
    unsigned auto_convert;
    int segment_time_metadata;
} ConcatContext;

static int concat_probe(const AVProbeData *probe)
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
    const char *ptr;
    size_t url_len;
    int ret;

    if (cat->safe && !safe_filename(filename)) {
        av_log(avf, AV_LOG_ERROR, "Unsafe file name '%s'\n", filename);
        FAIL(AVERROR(EPERM));
    }

    proto = avio_find_protocol_name(filename);
    if (proto && av_strstart(filename, proto, &ptr) &&
        (*ptr == ':' || *ptr == ',')) {
        url = filename;
        filename = NULL;
    } else {
        url_len = strlen(avf->url) + strlen(filename) + 16;
        if (!(url = av_malloc(url_len)))
            FAIL(AVERROR(ENOMEM));
        ff_make_absolute_url(url, url_len, avf->url, filename);
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
    file->next_dts   = AV_NOPTS_VALUE;
    file->inpoint    = AV_NOPTS_VALUE;
    file->outpoint   = AV_NOPTS_VALUE;
    file->user_duration = AV_NOPTS_VALUE;

    return 0;

fail:
    av_free(url);
    av_free(filename);
    return ret;
}

static int copy_stream_props(AVStream *st, AVStream *source_st)
{
    int ret;

    if (st->codecpar->codec_id || !source_st->codecpar->codec_id) {
        if (st->codecpar->extradata_size < source_st->codecpar->extradata_size) {
            ret = ff_alloc_extradata(st->codecpar,
                                     source_st->codecpar->extradata_size);
            if (ret < 0)
                return ret;
        }
        if (source_st->codecpar->extradata_size)
            memcpy(st->codecpar->extradata, source_st->codecpar->extradata,
                   source_st->codecpar->extradata_size);
        return 0;
    }
    if ((ret = avcodec_parameters_copy(st->codecpar, source_st->codecpar)) < 0)
        return ret;
    st->r_frame_rate        = source_st->r_frame_rate;
    st->avg_frame_rate      = source_st->avg_frame_rate;
    st->sample_aspect_ratio = source_st->sample_aspect_ratio;
    avpriv_set_pts_info(st, 64, source_st->time_base.num, source_st->time_base.den);

    av_dict_copy(&st->metadata, source_st->metadata, 0);
    return 0;
}

static int detect_stream_specific(AVFormatContext *avf, int idx)
{
    ConcatContext *cat = avf->priv_data;
    AVStream *st = cat->avf->streams[idx];
    ConcatStream *cs = &cat->cur_file->streams[idx];
    const AVBitStreamFilter *filter;
    AVBSFContext *bsf;
    int ret;

    if (cat->auto_convert && st->codecpar->codec_id == AV_CODEC_ID_H264) {
        if (!st->codecpar->extradata_size                                                ||
            (st->codecpar->extradata_size >= 3 && AV_RB24(st->codecpar->extradata) == 1) ||
            (st->codecpar->extradata_size >= 4 && AV_RB32(st->codecpar->extradata) == 1))
            return 0;
        av_log(cat->avf, AV_LOG_INFO,
               "Auto-inserting h264_mp4toannexb bitstream filter\n");
        filter = av_bsf_get_by_name("h264_mp4toannexb");
        if (!filter) {
            av_log(avf, AV_LOG_ERROR, "h264_mp4toannexb bitstream filter "
                   "required for H.264 streams\n");
            return AVERROR_BSF_NOT_FOUND;
        }
        ret = av_bsf_alloc(filter, &bsf);
        if (ret < 0)
            return ret;
        cs->bsf = bsf;

        ret = avcodec_parameters_copy(bsf->par_in, st->codecpar);
        if (ret < 0)
           return ret;

        ret = av_bsf_init(bsf);
        if (ret < 0)
            return ret;

        ret = avcodec_parameters_copy(st->codecpar, bsf->par_out);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int match_streams_one_to_one(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    AVStream *st;
    int i, ret;

    for (i = cat->cur_file->nb_streams; i < cat->avf->nb_streams; i++) {
        if (i < avf->nb_streams) {
            st = avf->streams[i];
        } else {
            if (!(st = avformat_new_stream(avf, NULL)))
                return AVERROR(ENOMEM);
        }
        if ((ret = copy_stream_props(st, cat->avf->streams[i])) < 0)
            return ret;
        cat->cur_file->streams[i].out_stream_index = i;
    }
    return 0;
}

static int match_streams_exact_id(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    AVStream *st;
    int i, j, ret;

    for (i = cat->cur_file->nb_streams; i < cat->avf->nb_streams; i++) {
        st = cat->avf->streams[i];
        for (j = 0; j < avf->nb_streams; j++) {
            if (avf->streams[j]->id == st->id) {
                av_log(avf, AV_LOG_VERBOSE,
                       "Match slave stream #%d with stream #%d id 0x%x\n",
                       i, j, st->id);
                if ((ret = copy_stream_props(avf->streams[j], st)) < 0)
                    return ret;
                cat->cur_file->streams[i].out_stream_index = j;
            }
        }
    }
    return 0;
}

static int match_streams(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    ConcatStream *map;
    int i, ret;

    if (cat->cur_file->nb_streams >= cat->avf->nb_streams)
        return 0;
    map = av_realloc(cat->cur_file->streams,
                     cat->avf->nb_streams * sizeof(*map));
    if (!map)
        return AVERROR(ENOMEM);
    cat->cur_file->streams = map;
    memset(map + cat->cur_file->nb_streams, 0,
           (cat->avf->nb_streams - cat->cur_file->nb_streams) * sizeof(*map));

    for (i = cat->cur_file->nb_streams; i < cat->avf->nb_streams; i++) {
        map[i].out_stream_index = -1;
        if ((ret = detect_stream_specific(avf, i)) < 0)
            return ret;
    }
    switch (cat->stream_match_mode) {
    case MATCH_ONE_TO_ONE:
        ret = match_streams_one_to_one(avf);
        break;
    case MATCH_EXACT_ID:
        ret = match_streams_exact_id(avf);
        break;
    default:
        ret = AVERROR_BUG;
    }
    if (ret < 0)
        return ret;
    cat->cur_file->nb_streams = cat->avf->nb_streams;
    return 0;
}

static int64_t get_best_effort_duration(ConcatFile *file, AVFormatContext *avf)
{
    if (file->user_duration != AV_NOPTS_VALUE)
        return file->user_duration;
    if (file->outpoint != AV_NOPTS_VALUE)
        return av_sat_sub64(file->outpoint, file->file_inpoint);
    if (avf->duration > 0)
        return av_sat_sub64(avf->duration, file->file_inpoint - file->file_start_time);
    if (file->next_dts != AV_NOPTS_VALUE)
        return file->next_dts - file->file_inpoint;
    return AV_NOPTS_VALUE;
}

static int open_file(AVFormatContext *avf, unsigned fileno)
{
    ConcatContext *cat = avf->priv_data;
    ConcatFile *file = &cat->files[fileno];
    AVDictionary *options = NULL;
    int ret;

    if (cat->avf)
        avformat_close_input(&cat->avf);

    cat->avf = avformat_alloc_context();
    if (!cat->avf)
        return AVERROR(ENOMEM);

    cat->avf->flags |= avf->flags & ~AVFMT_FLAG_CUSTOM_IO;
    cat->avf->interrupt_callback = avf->interrupt_callback;

    if ((ret = ff_copy_whiteblacklists(cat->avf, avf)) < 0)
        return ret;

    ret = av_dict_copy(&options, file->options, 0);
    if (ret < 0)
        return ret;

    if ((ret = avformat_open_input(&cat->avf, file->url, NULL, &options)) < 0 ||
        (ret = avformat_find_stream_info(cat->avf, NULL)) < 0) {
        av_log(avf, AV_LOG_ERROR, "Impossible to open '%s'\n", file->url);
        av_dict_free(&options);
        avformat_close_input(&cat->avf);
        return ret;
    }
    if (options) {
        av_log(avf, AV_LOG_WARNING, "Unused options for '%s'.\n", file->url);
        /* TODO log unused options once we have a proper string API */
        av_dict_free(&options);
    }
    cat->cur_file = file;
    file->start_time = !fileno ? 0 :
                       cat->files[fileno - 1].start_time +
                       cat->files[fileno - 1].duration;
    file->file_start_time = (cat->avf->start_time == AV_NOPTS_VALUE) ? 0 : cat->avf->start_time;
    file->file_inpoint = (file->inpoint == AV_NOPTS_VALUE) ? file->file_start_time : file->inpoint;
    file->duration = get_best_effort_duration(file, cat->avf);

    if (cat->segment_time_metadata) {
        av_dict_set_int(&file->metadata, "lavf.concatdec.start_time", file->start_time, 0);
        if (file->duration != AV_NOPTS_VALUE)
            av_dict_set_int(&file->metadata, "lavf.concatdec.duration", file->duration, 0);
    }

    if ((ret = match_streams(avf)) < 0)
        return ret;
    if (file->inpoint != AV_NOPTS_VALUE) {
       if ((ret = avformat_seek_file(cat->avf, -1, INT64_MIN, file->inpoint, file->inpoint, 0)) < 0)
           return ret;
    }
    return 0;
}

static int concat_read_close(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    unsigned i, j;

    for (i = 0; i < cat->nb_files; i++) {
        av_freep(&cat->files[i].url);
        for (j = 0; j < cat->files[i].nb_streams; j++) {
            if (cat->files[i].streams[j].bsf)
                av_bsf_free(&cat->files[i].streams[j].bsf);
        }
        av_freep(&cat->files[i].streams);
        av_dict_free(&cat->files[i].metadata);
        av_dict_free(&cat->files[i].options);
    }
    if (cat->avf)
        avformat_close_input(&cat->avf);
    av_freep(&cat->files);
    return 0;
}

#define MAX_ARGS 3
#define NEEDS_UNSAFE   (1 << 0)
#define NEEDS_FILE     (1 << 1)
#define NEEDS_STREAM   (1 << 2)

typedef struct ParseSyntax {
    const char *keyword;
    char args[MAX_ARGS];
    uint8_t flags;
} ParseSyntax;

typedef enum ParseDirective {
   DIR_FFCONCAT,
   DIR_FILE,
   DIR_DURATION,
   DIR_INPOINT,
   DIR_OUTPOINT,
   DIR_FPMETA,
   DIR_FPMETAS,
   DIR_OPTION,
   DIR_STREAM,
   DIR_EXSID,
   DIR_STMETA,
   DIR_STCODEC,
   DIR_STEDATA,
   DIR_CHAPTER,
} ParseDirective;

static const ParseSyntax syntax[] = {
    [DIR_FFCONCAT ] = { "ffconcat",             "kk",   0 },
    [DIR_FILE     ] = { "file",                 "s",    0 },
    [DIR_DURATION ] = { "duration",             "d",    NEEDS_FILE },
    [DIR_INPOINT  ] = { "inpoint",              "d",    NEEDS_FILE },
    [DIR_OUTPOINT ] = { "outpoint",             "d",    NEEDS_FILE },
    [DIR_FPMETA   ] = { "file_packet_meta",     "ks",   NEEDS_FILE },
    [DIR_FPMETAS  ] = { "file_packet_metadata", "s",    NEEDS_FILE },
    [DIR_OPTION   ] = { "option",               "ks",   NEEDS_FILE | NEEDS_UNSAFE },
    [DIR_STREAM   ] = { "stream",               "",     0 },
    [DIR_EXSID    ] = { "exact_stream_id",      "i",    NEEDS_STREAM },
    [DIR_STMETA   ] = { "stream_meta",          "ks",   NEEDS_STREAM },
    [DIR_STCODEC  ] = { "stream_codec",         "k",    NEEDS_STREAM },
    [DIR_STEDATA  ] = { "stream_extradata",     "k",    NEEDS_STREAM },
    [DIR_CHAPTER  ] = { "chapter",              "idd",  0 },
};

static int concat_parse_script(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    unsigned nb_files_alloc = 0;
    AVBPrint bp;
    uint8_t *cursor, *keyword;
    ConcatFile *file = NULL;
    AVStream *stream = NULL;
    AVChapter *chapter = NULL;
    unsigned line = 0, arg;
    const ParseSyntax *dir;
    char *arg_kw[MAX_ARGS];
    char *arg_str[MAX_ARGS] = { 0 };
    int64_t arg_int[MAX_ARGS];
    int ret;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);

    while ((ret = ff_read_line_to_bprint_overwrite(avf->pb, &bp)) >= 0) {
        line++;
        cursor = bp.str;
        keyword = get_keyword(&cursor);
        if (!*keyword || *keyword == '#')
            continue;
        for (dir = syntax; dir < syntax + FF_ARRAY_ELEMS(syntax); dir++)
            if (!strcmp(dir->keyword, keyword))
                break;
        if (dir >= syntax + FF_ARRAY_ELEMS(syntax)) {
            av_log(avf, AV_LOG_ERROR, "Line %d: unknown keyword '%s'\n",
                   line, keyword);
            FAIL(AVERROR_INVALIDDATA);
        }

        /* Flags check */
        if ((dir->flags & NEEDS_UNSAFE) && cat->safe) {
            av_log(avf, AV_LOG_ERROR, "Line %d: %s not allowed if safe\n", line, keyword);
            FAIL(AVERROR_INVALIDDATA);
        }
        if ((dir->flags & NEEDS_FILE) && !cat->nb_files) {
            av_log(avf, AV_LOG_ERROR, "Line %d: %s without file\n", line, keyword);
            FAIL(AVERROR_INVALIDDATA);
        }
        if ((dir->flags & NEEDS_STREAM) && !avf->nb_streams) {
            av_log(avf, AV_LOG_ERROR, "Line %d: %s without stream\n", line, keyword);
            FAIL(AVERROR_INVALIDDATA);
        }

        /* Arguments parsing */
        for (arg = 0; arg < FF_ARRAY_ELEMS(dir->args) && dir->args[arg]; arg++) {
            switch (dir->args[arg]) {
            case 'd': /* duration */
                arg_kw[arg] = get_keyword(&cursor);
                ret = av_parse_time(&arg_int[arg], arg_kw[arg], 1);
                if (ret < 0) {
                    av_log(avf, AV_LOG_ERROR, "Line %d: invalid duration '%s'\n",
                           line, arg_kw[arg]);
                    goto fail;
                }
                break;
            case 'i': /* integer */
                arg_int[arg] = strtol(get_keyword(&cursor), NULL, 0);
                break;
            case 'k': /* keyword */
                arg_kw[arg] = get_keyword(&cursor);
                break;
            case 's': /* string */
                av_assert0(!arg_str[arg]);
                arg_str[arg] = av_get_token((const char **)&cursor, SPACE_CHARS);
                if (!arg_str[arg])
                    FAIL(AVERROR(ENOMEM));
                if (!*arg_str[arg]) {
                    av_log(avf, AV_LOG_ERROR, "Line %d: string required\n", line);
                    FAIL(AVERROR_INVALIDDATA);
                }
                break;
            default:
                FAIL(AVERROR_BUG);
            }
        }

        /* Directive action */
        switch ((ParseDirective)(dir - syntax)) {

        case DIR_FFCONCAT:
            if (strcmp(arg_kw[0], "version") || strcmp(arg_kw[1], "1.0")) {
                av_log(avf, AV_LOG_ERROR, "Line %d: invalid version\n", line);
                FAIL(AVERROR_INVALIDDATA);
            }
            break;

        case DIR_FILE:
            ret = add_file(avf, arg_str[0], &file, &nb_files_alloc);
            arg_str[0] = NULL;
            if (ret < 0)
                goto fail;
            break;

        case DIR_DURATION:
            file->user_duration = arg_int[0];
            break;

        case DIR_INPOINT:
            file->inpoint = arg_int[0];
            break;

        case DIR_OUTPOINT:
            file->outpoint = arg_int[0];
            break;

        case DIR_FPMETA:
            ret = av_dict_set(&file->metadata, arg_kw[0], arg_str[1], AV_DICT_DONT_STRDUP_VAL);
            arg_str[1] = NULL;
            if (ret < 0)
                FAIL(ret);
            break;

        case DIR_FPMETAS:
            if ((ret = av_dict_parse_string(&file->metadata, arg_str[0], "=", "", 0)) < 0) {
                av_log(avf, AV_LOG_ERROR, "Line %d: failed to parse metadata string\n", line);
                FAIL(AVERROR_INVALIDDATA);
            }
            av_log(avf, AV_LOG_WARNING,
                   "'file_packet_metadata key=value:key=value' is deprecated, "
                   "use multiple 'file_packet_meta key value' instead\n");
            av_freep(&arg_str[0]);
            break;

        case DIR_OPTION:
            ret = av_dict_set(&file->options, arg_kw[0], arg_str[1], AV_DICT_DONT_STRDUP_VAL);
            arg_str[1] = NULL;
            if (ret < 0)
                FAIL(ret);
            break;

        case DIR_STREAM:
            stream = avformat_new_stream(avf, NULL);
            if (!stream)
                FAIL(AVERROR(ENOMEM));
            break;

        case DIR_EXSID:
            stream->id = arg_int[0];
            break;
        case DIR_STMETA:
            ret = av_dict_set(&stream->metadata, arg_kw[0], arg_str[1], AV_DICT_DONT_STRDUP_VAL);
            arg_str[1] = NULL;
            if (ret < 0)
                FAIL(ret);
            break;

        case DIR_STCODEC: {
            const AVCodecDescriptor *codec = avcodec_descriptor_get_by_name(arg_kw[0]);
            if (!codec) {
                av_log(avf, AV_LOG_ERROR, "Line %d: codec '%s' not found\n", line, arg_kw[0]);
                FAIL(AVERROR_DECODER_NOT_FOUND);
            }
            stream->codecpar->codec_type = codec->type;
            stream->codecpar->codec_id = codec->id;
            break;
        }

        case DIR_STEDATA: {
            int size = ff_hex_to_data(NULL, arg_kw[0]);
            ret = ff_alloc_extradata(stream->codecpar, size);
            if (ret < 0)
                FAIL(ret);
            ff_hex_to_data(stream->codecpar->extradata, arg_kw[0]);
            break;
        }

        case DIR_CHAPTER:
            chapter = avpriv_new_chapter(avf, arg_int[0], AV_TIME_BASE_Q,
                                         arg_int[1], arg_int[2], NULL);
            if (!chapter)
                FAIL(ENOMEM);
            break;

        default:
            FAIL(AVERROR_BUG);
        }
    }

    if (!file) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (file->inpoint != AV_NOPTS_VALUE && file->outpoint != AV_NOPTS_VALUE) {
        if (file->inpoint  > file->outpoint ||
            file->outpoint - (uint64_t)file->inpoint > INT64_MAX)
            ret = AVERROR_INVALIDDATA;
    }

fail:
    for (arg = 0; arg < MAX_ARGS; arg++)
        av_freep(&arg_str[arg]);
    av_bprint_finalize(&bp, NULL);
    return ret == AVERROR_EOF ? 0 : ret;
}

static int concat_read_header(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    int64_t time = 0;
    unsigned i;
    int ret;

    ret = concat_parse_script(avf);
    if (ret < 0)
        return ret;
    if (!cat->nb_files) {
        av_log(avf, AV_LOG_ERROR, "No files to concat\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < cat->nb_files; i++) {
        if (cat->files[i].start_time == AV_NOPTS_VALUE)
            cat->files[i].start_time = time;
        else
            time = cat->files[i].start_time;
        if (cat->files[i].user_duration == AV_NOPTS_VALUE) {
            if (cat->files[i].inpoint == AV_NOPTS_VALUE || cat->files[i].outpoint == AV_NOPTS_VALUE ||
                cat->files[i].outpoint - (uint64_t)cat->files[i].inpoint != av_sat_sub64(cat->files[i].outpoint, cat->files[i].inpoint)
            )
                break;
            cat->files[i].user_duration = cat->files[i].outpoint - cat->files[i].inpoint;
        }
        cat->files[i].duration = cat->files[i].user_duration;
        if (time + (uint64_t)cat->files[i].user_duration > INT64_MAX)
            return AVERROR_INVALIDDATA;
        time += cat->files[i].user_duration;
    }
    if (i == cat->nb_files) {
        avf->duration = time;
        cat->seekable = 1;
    }

    cat->stream_match_mode = avf->nb_streams ? MATCH_EXACT_ID :
                                               MATCH_ONE_TO_ONE;
    if ((ret = open_file(avf, 0)) < 0)
        return ret;

    return 0;
}

static int open_next_file(AVFormatContext *avf)
{
    ConcatContext *cat = avf->priv_data;
    unsigned fileno = cat->cur_file - cat->files;

    cat->cur_file->duration = get_best_effort_duration(cat->cur_file, cat->avf);

    if (++fileno >= cat->nb_files) {
        cat->eof = 1;
        return AVERROR_EOF;
    }
    return open_file(avf, fileno);
}

static int filter_packet(AVFormatContext *avf, ConcatStream *cs, AVPacket *pkt)
{
    int ret;

    if (cs->bsf) {
        ret = av_bsf_send_packet(cs->bsf, pkt);
        if (ret < 0) {
            av_log(avf, AV_LOG_ERROR, "h264_mp4toannexb filter "
                   "failed to send input packet\n");
            return ret;
        }

        while (!ret)
            ret = av_bsf_receive_packet(cs->bsf, pkt);

        if (ret < 0 && (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)) {
            av_log(avf, AV_LOG_ERROR, "h264_mp4toannexb filter "
                   "failed to receive output packet\n");
            return ret;
        }
    }
    return 0;
}

/* Returns true if the packet dts is greater or equal to the specified outpoint. */
static int packet_after_outpoint(ConcatContext *cat, AVPacket *pkt)
{
    if (cat->cur_file->outpoint != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) {
        return av_compare_ts(pkt->dts, cat->avf->streams[pkt->stream_index]->time_base,
                             cat->cur_file->outpoint, AV_TIME_BASE_Q) >= 0;
    }
    return 0;
}

static int concat_read_packet(AVFormatContext *avf, AVPacket *pkt)
{
    ConcatContext *cat = avf->priv_data;
    int ret;
    int64_t delta;
    ConcatStream *cs;
    AVStream *st;
    FFStream *sti;

    if (cat->eof)
        return AVERROR_EOF;

    if (!cat->avf)
        return AVERROR(EIO);

    while (1) {
        ret = av_read_frame(cat->avf, pkt);
        if (ret == AVERROR_EOF) {
            if ((ret = open_next_file(avf)) < 0)
                return ret;
            continue;
        }
        if (ret < 0)
            return ret;
        if ((ret = match_streams(avf)) < 0) {
            return ret;
        }
        if (packet_after_outpoint(cat, pkt)) {
            av_packet_unref(pkt);
            if ((ret = open_next_file(avf)) < 0)
                return ret;
            continue;
        }
        cs = &cat->cur_file->streams[pkt->stream_index];
        if (cs->out_stream_index < 0) {
            av_packet_unref(pkt);
            continue;
        }
        break;
    }
    if ((ret = filter_packet(avf, cs, pkt)) < 0)
        return ret;

    st = cat->avf->streams[pkt->stream_index];
    sti = ffstream(st);
    av_log(avf, AV_LOG_DEBUG, "file:%d stream:%d pts:%s pts_time:%s dts:%s dts_time:%s",
           (unsigned)(cat->cur_file - cat->files), pkt->stream_index,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &st->time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &st->time_base));

    delta = av_rescale_q(cat->cur_file->start_time - cat->cur_file->file_inpoint,
                         AV_TIME_BASE_Q,
                         cat->avf->streams[pkt->stream_index]->time_base);
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts += delta;
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += delta;
    av_log(avf, AV_LOG_DEBUG, " -> pts:%s pts_time:%s dts:%s dts_time:%s\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &st->time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &st->time_base));
    if (cat->cur_file->metadata) {
        size_t metadata_len;
        char* packed_metadata = av_packet_pack_dictionary(cat->cur_file->metadata, &metadata_len);
        if (!packed_metadata)
            return AVERROR(ENOMEM);
        ret = av_packet_add_side_data(pkt, AV_PKT_DATA_STRINGS_METADATA,
                                      packed_metadata, metadata_len);
        if (ret < 0) {
            av_freep(&packed_metadata);
            return ret;
        }
    }

    if (cat->cur_file->duration == AV_NOPTS_VALUE && sti->cur_dts != AV_NOPTS_VALUE) {
        int64_t next_dts = av_rescale_q(sti->cur_dts, st->time_base, AV_TIME_BASE_Q);
        if (cat->cur_file->next_dts == AV_NOPTS_VALUE || next_dts > cat->cur_file->next_dts) {
            cat->cur_file->next_dts = next_dts;
        }
    }

    pkt->stream_index = cs->out_stream_index;
    return 0;
}

static int try_seek(AVFormatContext *avf, int stream,
                    int64_t min_ts, int64_t ts, int64_t max_ts, int flags)
{
    ConcatContext *cat = avf->priv_data;
    int64_t t0 = cat->cur_file->start_time - cat->cur_file->file_inpoint;

    ts -= t0;
    min_ts = min_ts == INT64_MIN ? INT64_MIN : min_ts - t0;
    max_ts = max_ts == INT64_MAX ? INT64_MAX : max_ts - t0;
    if (stream >= 0) {
        if (stream >= cat->avf->nb_streams)
            return AVERROR(EIO);
        ff_rescale_interval(AV_TIME_BASE_Q, cat->avf->streams[stream]->time_base,
                            &min_ts, &ts, &max_ts);
    }
    return avformat_seek_file(cat->avf, stream, min_ts, ts, max_ts, flags);
}

static int real_seek(AVFormatContext *avf, int stream,
                     int64_t min_ts, int64_t ts, int64_t max_ts, int flags, AVFormatContext *cur_avf)
{
    ConcatContext *cat = avf->priv_data;
    int ret, left, right;

    if (stream >= 0) {
        if (stream >= avf->nb_streams)
            return AVERROR(EINVAL);
        ff_rescale_interval(avf->streams[stream]->time_base, AV_TIME_BASE_Q,
                            &min_ts, &ts, &max_ts);
    }

    left  = 0;
    right = cat->nb_files;

    /* Always support seek to start */
    if (ts <= 0)
        right = 1;
    else if (!cat->seekable)
        return AVERROR(ESPIPE); /* XXX: can we use it? */

    while (right - left > 1) {
        int mid = (left + right) / 2;
        if (ts < cat->files[mid].start_time)
            right = mid;
        else
            left  = mid;
    }

    if (cat->cur_file != &cat->files[left]) {
        if ((ret = open_file(avf, left)) < 0)
            return ret;
    } else {
        cat->avf = cur_avf;
    }

    ret = try_seek(avf, stream, min_ts, ts, max_ts, flags);
    if (ret < 0 &&
        left < cat->nb_files - 1 &&
        cat->files[left + 1].start_time < max_ts) {
        if (cat->cur_file == &cat->files[left])
            cat->avf = NULL;
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

    if (flags & (AVSEEK_FLAG_BYTE | AVSEEK_FLAG_FRAME))
        return AVERROR(ENOSYS);
    cat->avf = NULL;
    if ((ret = real_seek(avf, stream, min_ts, ts, max_ts, flags, cur_avf_saved)) < 0) {
        if (cat->cur_file != cur_file_saved) {
            if (cat->avf)
                avformat_close_input(&cat->avf);
        }
        cat->avf      = cur_avf_saved;
        cat->cur_file = cur_file_saved;
    } else {
        if (cat->cur_file != cur_file_saved) {
            avformat_close_input(&cur_avf_saved);
        }
        cat->eof = 0;
    }
    return ret;
}

#define OFFSET(x) offsetof(ConcatContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "safe", "enable safe mode",
      OFFSET(safe), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, DEC },
    { "auto_convert", "automatically convert bitstream format",
      OFFSET(auto_convert), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, DEC },
    { "segment_time_metadata", "output file segment start time and duration as packet metadata",
      OFFSET(segment_time_metadata), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
    { NULL }
};

static const AVClass concat_class = {
    .class_name = "concat demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};


const FFInputFormat ff_concat_demuxer = {
    .p.name         = "concat",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Virtual concatenation script"),
    .p.priv_class   = &concat_class,
    .priv_data_size = sizeof(ConcatContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = concat_probe,
    .read_header    = concat_read_header,
    .read_packet    = concat_read_packet,
    .read_close     = concat_read_close,
    .read_seek2     = concat_seek,
};
