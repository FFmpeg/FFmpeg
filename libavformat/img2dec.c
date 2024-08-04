/*
 * Image format
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2004 Michael Niedermayer
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

#include "config_components.h"

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <sys/stat.h>
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/intreadwrite.h"
#include "libavcodec/gif.h"
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"
#include "img2.h"
#include "os_support.h"
#include "libavcodec/jpegxl_parse.h"
#include "libavcodec/mjpeg.h"
#include "libavcodec/vbn.h"
#include "libavcodec/xwd.h"
#include "subtitles.h"

#if HAVE_GLOB
/* Locally define as 0 (bitwise-OR no-op) any missing glob options that
   are non-posix glibc/bsd extensions. */
#ifndef GLOB_NOMAGIC
#define GLOB_NOMAGIC 0
#endif
#ifndef GLOB_BRACE
#define GLOB_BRACE 0
#endif

#endif /* HAVE_GLOB */

static const int sizes[][2] = {
    { 640, 480 },
    { 720, 480 },
    { 720, 576 },
    { 352, 288 },
    { 352, 240 },
    { 160, 128 },
    { 512, 384 },
    { 640, 352 },
    { 640, 240 },
};

static int infer_size(int *width_ptr, int *height_ptr, int size)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(sizes); i++) {
        if ((sizes[i][0] * sizes[i][1]) == size) {
            *width_ptr  = sizes[i][0];
            *height_ptr = sizes[i][1];
            return 0;
        }
    }

    return -1;
}

static int is_glob(const char *path)
{
#if HAVE_GLOB
    size_t span = 0;
    const char *p = path;

    while (p = strchr(p, '%')) {
        if (*(++p) == '%') {
            ++p;
            continue;
        }
        if (span = strspn(p, "*?[]{}"))
            break;
    }
    /* Did we hit a glob char or get to the end? */
    return span != 0;
#else
    return 0;
#endif
}

/**
 * Get index range of image files matched by path.
 *
 * @param pfirst_index pointer to index updated with the first number in the range
 * @param plast_index  pointer to index updated with the last number in the range
 * @param path         path which has to be matched by the image files in the range
 * @param start_index  minimum accepted value for the first index in the range
 * @return -1 if no image file could be found
 */
static int find_image_range(AVIOContext *pb, int *pfirst_index, int *plast_index,
                            const char *path, int start_index, int start_index_range)
{
    char buf[1024];
    int range, last_index, range1, first_index;

    /* find the first image */
    for (first_index = start_index; first_index < start_index + start_index_range; first_index++) {
        if (av_get_frame_filename(buf, sizeof(buf), path, first_index) < 0) {
            *pfirst_index =
            *plast_index  = 1;
            if (pb || avio_check(buf, AVIO_FLAG_READ) > 0)
                return 0;
            return -1;
        }
        if (avio_check(buf, AVIO_FLAG_READ) > 0)
            break;
    }
    if (first_index == start_index + start_index_range)
        goto fail;

    /* find the last image */
    last_index = first_index;
    for (;;) {
        range = 0;
        for (;;) {
            if (!range)
                range1 = 1;
            else
                range1 = 2 * range;
            if (av_get_frame_filename(buf, sizeof(buf), path,
                                      last_index + range1) < 0)
                goto fail;
            if (avio_check(buf, AVIO_FLAG_READ) <= 0)
                break;
            range = range1;
            /* just in case... */
            if (range >= (1 << 30))
                goto fail;
        }
        /* we are sure than image last_index + range exists */
        if (!range)
            break;
        last_index += range;
    }
    *pfirst_index = first_index;
    *plast_index  = last_index;
    return 0;

fail:
    return -1;
}

static int img_read_probe(const AVProbeData *p)
{
    if (p->filename && ff_guess_image2_codec(p->filename)) {
        if (av_filename_number_test(p->filename))
            return AVPROBE_SCORE_MAX;
        else if (is_glob(p->filename))
            return AVPROBE_SCORE_MAX;
        else if (p->filename[strcspn(p->filename, "*?{")]) // probably PT_GLOB
            return AVPROBE_SCORE_EXTENSION + 2; // score chosen to be a tad above the image pipes
        else if (p->buf_size == 0)
            return 0;
        else if (av_match_ext(p->filename, "raw") || av_match_ext(p->filename, "gif"))
            return 5;
        else
            return AVPROBE_SCORE_EXTENSION;
    }
    return 0;
}

int ff_img_read_header(AVFormatContext *s1)
{
    VideoDemuxData *s = s1->priv_data;
    int first_index = 1, last_index = 1;
    AVStream *st;
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;

    s1->ctx_flags |= AVFMTCTX_NOHEADER;

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        return AVERROR(ENOMEM);
    }

    if (s->pixel_format &&
        (pix_fmt = av_get_pix_fmt(s->pixel_format)) == AV_PIX_FMT_NONE) {
        av_log(s1, AV_LOG_ERROR, "No such pixel format: %s.\n",
               s->pixel_format);
        return AVERROR(EINVAL);
    }

    av_strlcpy(s->path, s1->url, sizeof(s->path));
    s->img_number = 0;
    s->img_count  = 0;

    /* find format */
    if (s1->iformat->flags & AVFMT_NOFILE)
        s->is_pipe = 0;
    else {
        s->is_pipe       = 1;
        ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;
    }

    if (s->ts_from_file == 2) {
#if !HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
        av_log(s1, AV_LOG_ERROR, "POSIX.1-2008 not supported, nanosecond file timestamps unavailable\n");
        return AVERROR(ENOSYS);
#endif
        avpriv_set_pts_info(st, 64, 1, 1000000000);
    } else if (s->ts_from_file)
        avpriv_set_pts_info(st, 64, 1, 1);
    else {
        avpriv_set_pts_info(st, 64, s->framerate.den, s->framerate.num);
        st->avg_frame_rate = st->r_frame_rate = s->framerate;
    }

    if (s->width && s->height) {
        st->codecpar->width  = s->width;
        st->codecpar->height = s->height;
    }

    if (!s->is_pipe) {
        if (s->pattern_type == PT_DEFAULT) {
            if (s1->pb) {
                s->pattern_type = PT_NONE;
            } else
                s->pattern_type = PT_GLOB_SEQUENCE;
        }

        if (s->pattern_type == PT_GLOB_SEQUENCE) {
        s->use_glob = is_glob(s->path);
        if (s->use_glob) {
#if HAVE_GLOB
            char *p = s->path, *q, *dup;
            int gerr;
#endif

            av_log(s1, AV_LOG_WARNING, "Pattern type 'glob_sequence' is deprecated: "
                   "use pattern_type 'glob' instead\n");
#if HAVE_GLOB
            dup = q = av_strdup(p);
            while (*q) {
                /* Do we have room for the next char and a \ insertion? */
                if ((p - s->path) >= (sizeof(s->path) - 2))
                  break;
                if (*q == '%' && strspn(q + 1, "%*?[]{}"))
                    ++q;
                else if (strspn(q, "\\*?[]{}"))
                    *p++ = '\\';
                *p++ = *q++;
            }
            *p = 0;
            av_free(dup);

            gerr = glob(s->path, GLOB_NOCHECK|GLOB_BRACE|GLOB_NOMAGIC, NULL, &s->globstate);
            if (gerr != 0) {
                return AVERROR(ENOENT);
            }
            first_index = 0;
            last_index = s->globstate.gl_pathc - 1;
#endif
        }
        }
        if ((s->pattern_type == PT_GLOB_SEQUENCE && !s->use_glob) || s->pattern_type == PT_SEQUENCE) {
            if (find_image_range(s1->pb, &first_index, &last_index, s->path,
                                 s->start_number, s->start_number_range) < 0) {
                av_log(s1, AV_LOG_ERROR,
                       "Could find no file with path '%s' and index in the range %d-%d\n",
                       s->path, s->start_number, s->start_number + s->start_number_range - 1);
                return AVERROR(ENOENT);
            }
        } else if (s->pattern_type == PT_GLOB) {
#if HAVE_GLOB
            int gerr;
            gerr = glob(s->path, GLOB_NOCHECK|GLOB_BRACE|GLOB_NOMAGIC, NULL, &s->globstate);
            if (gerr != 0) {
                return AVERROR(ENOENT);
            }
            first_index = 0;
            last_index = s->globstate.gl_pathc - 1;
            s->use_glob = 1;
#else
            av_log(s1, AV_LOG_ERROR,
                   "Pattern type 'glob' was selected but globbing "
                   "is not supported by this libavformat build\n");
            return AVERROR(ENOSYS);
#endif
        } else if (s->pattern_type != PT_GLOB_SEQUENCE && s->pattern_type != PT_NONE) {
            av_log(s1, AV_LOG_ERROR,
                   "Unknown value '%d' for pattern_type option\n", s->pattern_type);
            return AVERROR(EINVAL);
        }
        s->img_first  = first_index;
        s->img_last   = last_index;
        s->img_number = first_index;
        /* compute duration */
        if (!s->ts_from_file) {
            st->start_time = 0;
            st->duration   = last_index - first_index + 1;
        }
    }

    if (s1->video_codec_id) {
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = s1->video_codec_id;
    } else if (s1->audio_codec_id) {
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id   = s1->audio_codec_id;
    } else if (ffifmt(s1->iformat)->raw_codec_id) {
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = ffifmt(s1->iformat)->raw_codec_id;
    } else {
        const char *str = strrchr(s->path, '.');
        s->split_planes       = str && !av_strcasecmp(str + 1, "y");
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        if (s1->pb) {
            int probe_buffer_size = 2048;
            uint8_t *probe_buffer = av_realloc(NULL, probe_buffer_size + AVPROBE_PADDING_SIZE);
            const AVInputFormat *fmt = NULL;
            void *fmt_iter = NULL;
            AVProbeData pd = { 0 };

            if (!probe_buffer)
                return AVERROR(ENOMEM);

            probe_buffer_size = avio_read(s1->pb, probe_buffer, probe_buffer_size);
            if (probe_buffer_size < 0) {
                av_free(probe_buffer);
                return probe_buffer_size;
            }
            memset(probe_buffer + probe_buffer_size, 0, AVPROBE_PADDING_SIZE);

            pd.buf = probe_buffer;
            pd.buf_size = probe_buffer_size;
            pd.filename = s1->url;

            while ((fmt = av_demuxer_iterate(&fmt_iter))) {
                const FFInputFormat *fmt2 = ffifmt(fmt);
                if (fmt2->read_header != ff_img_read_header ||
                    !fmt2->read_probe ||
                    (fmt->flags & AVFMT_NOFILE) ||
                    !fmt2->raw_codec_id)
                    continue;
                if (fmt2->read_probe(&pd) > 0) {
                    st->codecpar->codec_id = fmt2->raw_codec_id;
                    break;
                }
            }
            if (s1->flags & AVFMT_FLAG_CUSTOM_IO) {
                avio_seek(s1->pb, 0, SEEK_SET);
                av_freep(&probe_buffer);
            } else
                ffio_rewind_with_probe_data(s1->pb, &probe_buffer, probe_buffer_size);
        }
        if (st->codecpar->codec_id == AV_CODEC_ID_NONE)
            st->codecpar->codec_id = ff_guess_image2_codec(s->path);
        if (st->codecpar->codec_id == AV_CODEC_ID_LJPEG)
            st->codecpar->codec_id = AV_CODEC_ID_MJPEG;
        if (st->codecpar->codec_id == AV_CODEC_ID_ALIAS_PIX) // we cannot distingiush this from BRENDER_PIX
            st->codecpar->codec_id = AV_CODEC_ID_NONE;
    }
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
        pix_fmt != AV_PIX_FMT_NONE)
        st->codecpar->format = pix_fmt;

    return 0;
}

/**
 * Add this frame's source path and basename to packet's sidedata
 * as a dictionary, so it can be used by filters like 'drawtext'.
 */
static int add_filename_as_pkt_side_data(char *filename, AVPacket *pkt) {
    AVDictionary *d = NULL;
    char *packed_metadata = NULL;
    size_t metadata_len;
    int ret;

    av_dict_set(&d, "lavf.image2dec.source_path", filename, 0);
    av_dict_set(&d, "lavf.image2dec.source_basename", av_basename(filename), 0);

    packed_metadata = av_packet_pack_dictionary(d, &metadata_len);
    av_dict_free(&d);
    if (!packed_metadata)
        return AVERROR(ENOMEM);
    ret = av_packet_add_side_data(pkt, AV_PKT_DATA_STRINGS_METADATA,
                                  packed_metadata, metadata_len);
    if (ret < 0) {
        av_freep(&packed_metadata);
        return ret;
    }
    return 0;
}

int ff_img_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoDemuxData *s = s1->priv_data;
    char filename_bytes[1024];
    char *filename = filename_bytes;
    int i, res;
    int size[3]           = { 0 }, ret[3] = { 0 };
    AVIOContext *f[3]     = { NULL };
    AVCodecParameters *par = s1->streams[0]->codecpar;

    if (!s->is_pipe) {
        /* loop over input */
        if (s->loop && s->img_number > s->img_last) {
            s->img_number = s->img_first;
        }
        if (s->img_number > s->img_last)
            return AVERROR_EOF;
        if (s->pattern_type == PT_NONE) {
            av_strlcpy(filename_bytes, s->path, sizeof(filename_bytes));
        } else if (s->use_glob) {
#if HAVE_GLOB
            filename = s->globstate.gl_pathv[s->img_number];
#endif
        } else {
        if (av_get_frame_filename(filename_bytes, sizeof(filename_bytes),
                                  s->path,
                                  s->img_number) < 0 && s->img_number > 1)
            return AVERROR(EIO);
        }
        for (i = 0; i < 3; i++) {
            if (s1->pb &&
                !strcmp(filename_bytes, s->path) &&
                !s->loop &&
                !s->split_planes) {
                f[i] = s1->pb;
            } else if (s1->io_open(s1, &f[i], filename, AVIO_FLAG_READ, NULL) < 0) {
                if (i >= 1)
                    break;
                av_log(s1, AV_LOG_ERROR, "Could not open file : %s\n",
                       filename);
                return AVERROR(EIO);
            }
            size[i] = avio_size(f[i]);

            if (!s->split_planes)
                break;
            filename[strlen(filename) - 1] = 'U' + i;
        }

        if (par->codec_id == AV_CODEC_ID_NONE) {
            AVProbeData pd = { 0 };
            const FFInputFormat *ifmt;
            uint8_t header[PROBE_BUF_MIN + AVPROBE_PADDING_SIZE];
            int ret;
            int score = 0;

            ret = avio_read(f[0], header, PROBE_BUF_MIN);
            if (ret < 0)
                return ret;
            memset(header + ret, 0, sizeof(header) - ret);
            avio_skip(f[0], -ret);
            pd.buf = header;
            pd.buf_size = ret;
            pd.filename = filename;

            ifmt = ffifmt(av_probe_input_format3(&pd, 1, &score));
            if (ifmt && ifmt->read_packet == ff_img_read_packet && ifmt->raw_codec_id)
                par->codec_id = ifmt->raw_codec_id;
        }

        if (par->codec_id == AV_CODEC_ID_RAWVIDEO && !par->width)
            infer_size(&par->width, &par->height, size[0]);
    } else {
        f[0] = s1->pb;
        if (avio_feof(f[0]) && s->loop && s->is_pipe)
            avio_seek(f[0], 0, SEEK_SET);
        if (avio_feof(f[0]))
            return AVERROR_EOF;
        if (s->frame_size > 0) {
            size[0] = s->frame_size;
        } else if (!ffstream(s1->streams[0])->parser) {
            size[0] = avio_size(s1->pb);
        } else {
            size[0] = 4096;
        }
    }

    res = av_new_packet(pkt, size[0] + size[1] + size[2]);
    if (res < 0) {
        goto fail;
    }
    pkt->stream_index = 0;
    pkt->flags       |= AV_PKT_FLAG_KEY;
    if (s->ts_from_file) {
        struct stat img_stat;
        av_assert0(!s->is_pipe); // The ts_from_file option is not supported by piped input demuxers
        if (stat(filename, &img_stat)) {
            res = AVERROR(EIO);
            goto fail;
        }
        pkt->pts = (int64_t)img_stat.st_mtime;
#if HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
        if (s->ts_from_file == 2)
            pkt->pts = 1000000000*pkt->pts + img_stat.st_mtim.tv_nsec;
#endif
        av_add_index_entry(s1->streams[0], s->img_number, pkt->pts, 0, 0, AVINDEX_KEYFRAME);
    } else if (!s->is_pipe) {
        pkt->pts      = s->pts;
    }

    if (s->is_pipe)
        pkt->pos = avio_tell(f[0]);

    /*
     * export_path_metadata must be explicitly enabled via
     * command line options for path metadata to be exported
     * as packet side_data.
     */
    if (!s->is_pipe && s->export_path_metadata == 1) {
        res = add_filename_as_pkt_side_data(filename, pkt);
        if (res < 0)
            goto fail;
    }

    pkt->size = 0;
    for (i = 0; i < 3; i++) {
        if (f[i]) {
            ret[i] = avio_read(f[i], pkt->data + pkt->size, size[i]);
            if (s->loop && s->is_pipe && ret[i] == AVERROR_EOF) {
                if (avio_seek(f[i], 0, SEEK_SET) >= 0) {
                    pkt->pos = 0;
                    ret[i] = avio_read(f[i], pkt->data + pkt->size, size[i]);
                }
            }
            if (!s->is_pipe && f[i] != s1->pb)
                ff_format_io_close(s1, &f[i]);
            if (ret[i] > 0)
                pkt->size += ret[i];
        }
    }

    if (ret[0] <= 0 || ret[1] < 0 || ret[2] < 0) {
        if (ret[0] < 0) {
            res = ret[0];
        } else if (ret[1] < 0) {
            res = ret[1];
        } else if (ret[2] < 0) {
            res = ret[2];
        } else {
            res = AVERROR_EOF;
        }
        goto fail;
    } else {
        memset(pkt->data + pkt->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        s->img_count++;
        s->img_number++;
        s->pts++;
        return 0;
    }

fail:
    if (!s->is_pipe) {
        for (i = 0; i < 3; i++) {
            if (f[i] != s1->pb)
                ff_format_io_close(s1, &f[i]);
        }
    }
    return res;
}

static int img_read_close(struct AVFormatContext* s1)
{
#if HAVE_GLOB
    VideoDemuxData *s = s1->priv_data;
    if (s->use_glob) {
        globfree(&s->globstate);
    }
#endif
    return 0;
}

static int img_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    VideoDemuxData *s1 = s->priv_data;
    AVStream *st = s->streams[0];

    if (s1->ts_from_file) {
        int index = av_index_search_timestamp(st, timestamp, flags);
        if(index < 0)
            return -1;
        s1->img_number = ffstream(st)->index_entries[index].pos;
        return 0;
    }

    if (timestamp < 0 || !s1->loop && timestamp > s1->img_last - s1->img_first)
        return -1;
    s1->img_number = timestamp%(s1->img_last - s1->img_first + 1) + s1->img_first;
    s1->pts = timestamp;
    return 0;
}

#define OFFSET(x) offsetof(VideoDemuxData, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
#define COMMON_OPTIONS \
    { "framerate",    "set the video framerate", OFFSET(framerate),    AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC }, \
    { "pixel_format", "set video pixel format",  OFFSET(pixel_format), AV_OPT_TYPE_STRING,     {.str = NULL}, 0, 0,       DEC }, \
    { "video_size",   "set video size",          OFFSET(width),        AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0,       DEC }, \
    { "loop",         "force loop over input file sequence", OFFSET(loop), AV_OPT_TYPE_BOOL,   {.i64 = 0   }, 0, 1,       DEC }, \
    { NULL },

#if CONFIG_IMAGE2_DEMUXER
const AVOption ff_img_options[] = {
    { "pattern_type", "set pattern type",                    OFFSET(pattern_type), AV_OPT_TYPE_INT,    {.i64=PT_DEFAULT}, 0,       INT_MAX, DEC, .unit = "pattern_type"},
    { "glob_sequence","select glob/sequence pattern type",   0, AV_OPT_TYPE_CONST,  {.i64=PT_GLOB_SEQUENCE}, INT_MIN, INT_MAX, DEC, .unit = "pattern_type" },
    { "glob",         "select glob pattern type",            0, AV_OPT_TYPE_CONST,  {.i64=PT_GLOB         }, INT_MIN, INT_MAX, DEC, .unit = "pattern_type" },
    { "sequence",     "select sequence pattern type",        0, AV_OPT_TYPE_CONST,  {.i64=PT_SEQUENCE     }, INT_MIN, INT_MAX, DEC, .unit = "pattern_type" },
    { "none",         "disable pattern matching",            0, AV_OPT_TYPE_CONST,  {.i64=PT_NONE         }, INT_MIN, INT_MAX, DEC, .unit = "pattern_type" },
    { "start_number", "set first number in the sequence",    OFFSET(start_number), AV_OPT_TYPE_INT,    {.i64 = 0   }, INT_MIN, INT_MAX, DEC },
    { "start_number_range", "set range for looking at the first sequence number", OFFSET(start_number_range), AV_OPT_TYPE_INT, {.i64 = 5}, 1, INT_MAX, DEC },
    { "ts_from_file", "set frame timestamp from file's one", OFFSET(ts_from_file), AV_OPT_TYPE_INT,    {.i64 = 0   }, 0, 2,       DEC, .unit = "ts_type" },
    { "none", "none",                   0, AV_OPT_TYPE_CONST,    {.i64 = 0   }, 0, 2,       DEC, .unit = "ts_type" },
    { "sec",  "second precision",       0, AV_OPT_TYPE_CONST,    {.i64 = 1   }, 0, 2,       DEC, .unit = "ts_type" },
    { "ns",   "nano second precision",  0, AV_OPT_TYPE_CONST,    {.i64 = 2   }, 0, 2,       DEC, .unit = "ts_type" },
    { "export_path_metadata", "enable metadata containing input path information", OFFSET(export_path_metadata), AV_OPT_TYPE_BOOL,   {.i64 = 0   }, 0, 1,       DEC }, \
    COMMON_OPTIONS
};

static const AVClass img2_class = {
    .class_name = "image2 demuxer",
    .item_name  = av_default_item_name,
    .option     = ff_img_options,
    .version    = LIBAVUTIL_VERSION_INT,
};
const FFInputFormat ff_image2_demuxer = {
    .p.name         = "image2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("image2 sequence"),
    .p.flags        = AVFMT_NOFILE,
    .p.priv_class   = &img2_class,
    .priv_data_size = sizeof(VideoDemuxData),
    .read_probe     = img_read_probe,
    .read_header    = ff_img_read_header,
    .read_packet    = ff_img_read_packet,
    .read_close     = img_read_close,
    .read_seek      = img_read_seek,
};
#endif

static const AVOption img2pipe_options[] = {
    { "frame_size", "force frame size in bytes", OFFSET(frame_size), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, INT_MAX, DEC },
    COMMON_OPTIONS
};
static const AVClass imagepipe_class = {
    .class_name = "imagepipe demuxer",
    .item_name  = av_default_item_name,
    .option     = img2pipe_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

#if CONFIG_IMAGE2PIPE_DEMUXER
const FFInputFormat ff_image2pipe_demuxer = {
    .p.name         = "image2pipe",
    .p.long_name    = NULL_IF_CONFIG_SMALL("piped image2 sequence"),
    .p.priv_class   = &imagepipe_class,
    .priv_data_size = sizeof(VideoDemuxData),
    .read_header    = ff_img_read_header,
    .read_packet    = ff_img_read_packet,
};
#endif

static int bmp_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    int ihsize;

    if (AV_RB16(b) != 0x424d)
        return 0;

    ihsize = AV_RL32(b+14);
    if (ihsize < 12 || ihsize > 255)
        return 0;

    if (!AV_RN32(b + 6)) {
        return AVPROBE_SCORE_EXTENSION + 1;
    }
    return AVPROBE_SCORE_EXTENSION / 4;
}

static int cri_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (   AV_RL32(b) == 1
        && AV_RL32(b + 4) == 4
        && AV_RN32(b + 8) == AV_RN32("DVCC"))
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int dds_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (   AV_RB64(b) == 0x444453207c000000
        && AV_RL32(b +  8)
        && AV_RL32(b + 12))
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int dpx_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    int w, h;
    int is_big = (AV_RN32(b) == AV_RN32("SDPX"));

    if (p->buf_size < 0x304+8)
        return 0;
    w = is_big ? AV_RB32(p->buf + 0x304) : AV_RL32(p->buf + 0x304);
    h = is_big ? AV_RB32(p->buf + 0x308) : AV_RL32(p->buf + 0x308);
    if (w <= 0 || h <= 0)
        return 0;

    if (is_big || AV_RN32(b) == AV_RN32("XPDS"))
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int exr_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RL32(b) == 20000630)
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int j2k_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB64(b) == 0x0000000c6a502020 ||
        AV_RB32(b) == 0xff4fff51)
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int jpeg_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    int i, state = SOI, got_header = 0;

    if (AV_RB16(b) != 0xFFD8 ||
        AV_RB32(b) == 0xFFD8FFF7)
    return 0;

    b += 2;
    for (i = 0; i < p->buf_size - 3; i++) {
        int c;
        if (b[i] != 0xFF)
            continue;
        c = b[i + 1];
        switch (c) {
        case SOI:
            return 0;
        case SOF0:
        case SOF1:
        case SOF2:
        case SOF3:
        case SOF5:
        case SOF6:
        case SOF7:
            i += AV_RB16(&b[i + 2]) + 1;
            if (state != SOI)
                return 0;
            state = SOF0;
            break;
        case SOS:
            i += AV_RB16(&b[i + 2]) + 1;
            if (state != SOF0 && state != SOS)
                return 0;
            state = SOS;
            break;
        case EOI:
            if (state != SOS)
                return 0;
            state = EOI;
            break;
        case APP0:
            if (c == APP0 && AV_RL32(&b[i + 4]) == MKTAG('J','F','I','F'))
                got_header = 1;
            /* fallthrough */
        case APP1:
            if (c == APP1 && AV_RL32(&b[i + 4]) == MKTAG('E','x','i','f'))
                got_header = 1;
            /* fallthrough */
        case APP2:
        case APP3:
        case APP4:
        case APP5:
        case APP6:
        case APP7:
        case APP8:
        case APP9:
        case APP10:
        case APP11:
        case APP12:
        case APP13:
        case APP14:
        case APP15:
        case DQT: /* fallthrough */
        case COM:
            i += AV_RB16(&b[i + 2]) + 1;
            break;
        default:
            if (  (c > TEM && c < SOF0)
                || c == JPG)
                return 0;
        }
    }

    if (state == EOI)
        return AVPROBE_SCORE_EXTENSION + 1;
    if (state == SOS)
        return AVPROBE_SCORE_EXTENSION / 2 + got_header;
    return AVPROBE_SCORE_EXTENSION / 8 + 1;
}

static int jpegls_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b) == 0xffd8fff7)
         return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int jpegxl_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    /* ISOBMFF-based container */
    /* 0x4a584c20 == "JXL " */
    if (AV_RL64(b) == FF_JPEGXL_CONTAINER_SIGNATURE_LE)
        return AVPROBE_SCORE_EXTENSION + 1;
    /* Raw codestreams all start with 0xff0a */
    if (AV_RL16(b) != FF_JPEGXL_CODESTREAM_SIGNATURE_LE)
        return 0;
#if CONFIG_IMAGE_JPEGXL_PIPE_DEMUXER
    if (ff_jpegxl_parse_codestream_header(p->buf, p->buf_size, NULL, 5) >= 0)
        return AVPROBE_SCORE_MAX - 2;
#endif
    return 0;
}

static int pcx_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (   p->buf_size < 128
        || b[0] != 10
        || b[1] > 5
        || b[2] > 1
        || av_popcount(b[3]) != 1 || b[3] > 8
        || AV_RL16(&b[4]) > AV_RL16(&b[8])
        || AV_RL16(&b[6]) > AV_RL16(&b[10])
        || b[64])
        return 0;
    b += 73;
    while (++b < p->buf + 128)
        if (*b)
            return AVPROBE_SCORE_EXTENSION / 4;

    return AVPROBE_SCORE_EXTENSION + 1;
}

static int qdraw_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (   p->buf_size >= 528
        && (AV_RB64(b + 520) & 0xFFFFFFFFFFFF) == 0x001102ff0c00
        && AV_RB16(b + 520)
        && AV_RB16(b + 518))
        return AVPROBE_SCORE_MAX * 3 / 4;
    if (   (AV_RB64(b + 8) & 0xFFFFFFFFFFFF) == 0x001102ff0c00
        && AV_RB16(b + 8)
        && AV_RB16(b + 6))
        return AVPROBE_SCORE_EXTENSION / 4;
    return 0;
}

static int pictor_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RL16(b) == 0x1234)
        return AVPROBE_SCORE_EXTENSION / 4;
    return 0;
}

static int png_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB64(b) == 0x89504e470d0a1a0a)
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int psd_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    int ret = 0;
    uint16_t color_mode;

    if (AV_RL32(b) == MKTAG('8','B','P','S')) {
        ret += 1;
    } else {
        return 0;
    }

    if ((b[4] == 0) && (b[5] == 1)) {/* version 1 is PSD, version 2 is PSB */
        ret += 1;
    } else {
        return 0;
    }

    if ((AV_RL32(b+6) == 0) && (AV_RL16(b+10) == 0))/* reserved must be 0 */
        ret += 1;

    color_mode = AV_RB16(b+24);
    if ((color_mode <= 9) && (color_mode != 5) && (color_mode != 6))
        ret += 1;

    return AVPROBE_SCORE_EXTENSION + ret;
}

static int sgi_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB16(b) == 474 &&
        (b[2] & ~1) == 0 &&
        (b[3] & ~3) == 0 && b[3] &&
        (AV_RB16(b + 4) & ~7) == 0 && AV_RB16(b + 4))
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int sunrast_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b) == 0x59a66a95)
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int svg_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    const uint8_t *end = p->buf + p->buf_size;
    while (b < end && av_isspace(*b))
        b++;
    if (b >= end - 5)
        return 0;
    if (!memcmp(b, "<svg", 4))
        return AVPROBE_SCORE_EXTENSION + 1;
    if (memcmp(p->buf, "<?xml", 5) && memcmp(b, "<!--", 4))
        return 0;
    while (b < end) {
        int inc = ff_subtitles_next_line(b);
        if (!inc)
            break;
        b += inc;
        if (b >= end - 4)
            return 0;
        if (!memcmp(b, "<svg", 4))
            return AVPROBE_SCORE_EXTENSION + 1;
    }
    return 0;
}

static int tiff_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b) == 0x49492a00 ||
        AV_RB32(b) == 0x4D4D002a)
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int webp_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b)     == 0x52494646 &&
        AV_RB32(b + 8) == 0x57454250)
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int pnm_magic_check(const AVProbeData *p, int magic)
{
    const uint8_t *b = p->buf;

    return b[0] == 'P' && b[1] == magic + '0';
}

static inline int pnm_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    while (b[2] == '\r')
        b++;
    if (b[2] == '\n' && (b[3] == '#' || (b[3] >= '0' && b[3] <= '9')))
        return AVPROBE_SCORE_EXTENSION + 2;
    return 0;
}

static int pbm_probe(const AVProbeData *p)
{
    return pnm_magic_check(p, 1) || pnm_magic_check(p, 4) ? pnm_probe(p) : 0;
}

static int pfm_probe(const AVProbeData *p)
{
    return pnm_magic_check(p, 'F' - '0') ||
           pnm_magic_check(p, 'f' - '0') ? pnm_probe(p) : 0;
}

static int phm_probe(const AVProbeData *p)
{
    return pnm_magic_check(p, 'H' - '0') ||
           pnm_magic_check(p, 'h' - '0') ? pnm_probe(p) : 0;
}

static inline int pgmx_probe(const AVProbeData *p)
{
    return pnm_magic_check(p, 2) || pnm_magic_check(p, 5) ? pnm_probe(p) : 0;
}

static int pgm_probe(const AVProbeData *p)
{
    int ret = pgmx_probe(p);
    return ret && !av_match_ext(p->filename, "pgmyuv") ? ret : 0;
}

static int pgmyuv_probe(const AVProbeData *p) // custom FFmpeg format recognized by file extension
{
    int ret = pgmx_probe(p);
    return ret && av_match_ext(p->filename, "pgmyuv") ? ret : 0;
}

static int pgx_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    if (!memcmp(b, "PG ML ", 6))
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int ppm_probe(const AVProbeData *p)
{
    return pnm_magic_check(p, 3) || pnm_magic_check(p, 6) ? pnm_probe(p) : 0;
}

static int pam_probe(const AVProbeData *p)
{
    return pnm_magic_check(p, 7) ? pnm_probe(p) : 0;
}

static int hdr_probe(const AVProbeData *p)
{
    if (!memcmp(p->buf, "#?RADIANCE\n", 11))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int xbm_probe(const AVProbeData *p)
{
    if (!memcmp(p->buf, "/* XBM X10 format */", 20))
        return AVPROBE_SCORE_MAX;

    if (!memcmp(p->buf, "#define", 7))
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int xpm_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB64(b) == 0x2f2a2058504d202a && *(b+8) == '/')
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int xwd_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    unsigned width, bpp, bpad, lsize;

    if (   p->buf_size < XWD_HEADER_SIZE
        || AV_RB32(b     ) < XWD_HEADER_SIZE                          // header size
        || AV_RB32(b +  4) != XWD_VERSION                             // version
        || AV_RB32(b +  8) != XWD_Z_PIXMAP                            // format
        || AV_RB32(b + 12) > 32 || !AV_RB32(b + 12)                   // depth
        || AV_RB32(b + 16) == 0                                       // width
        || AV_RB32(b + 20) == 0                                       // height
        || AV_RB32(b + 28) > 1                                        // byteorder
        || AV_RB32(b + 32) & ~56 || av_popcount(AV_RB32(b + 32)) != 1 // bitmap unit
        || AV_RB32(b + 36) > 1                                        // bitorder
        || AV_RB32(b + 40) & ~56 || av_popcount(AV_RB32(b + 40)) != 1 // padding
        || AV_RB32(b + 44) > 32 || !AV_RB32(b + 44)                   // bpp
        || AV_RB32(b + 68) > 256)                                     // colours
        return 0;

    width = AV_RB32(b + 16);
    bpad  = AV_RB32(b + 40);
    bpp   = AV_RB32(b + 44);
    lsize = AV_RB32(b + 48);
    if (lsize < FFALIGN(width * bpp, bpad) >> 3)
        return 0;

    return AVPROBE_SCORE_MAX / 2 + 1;
}

static int gif_probe(const AVProbeData *p)
{
    /* check magick */
    if (memcmp(p->buf, gif87a_sig, 6) && memcmp(p->buf, gif89a_sig, 6))
        return 0;

    /* width or height contains zero? */
    if (!AV_RL16(&p->buf[6]) || !AV_RL16(&p->buf[8]))
        return 0;

    return AVPROBE_SCORE_MAX - 1;
}

static int photocd_probe(const AVProbeData *p)
{
    if (!memcmp(p->buf, "PCD_OPA", 7))
        return AVPROBE_SCORE_MAX - 1;

    if (p->buf_size < 0x807 || memcmp(p->buf + 0x800, "PCD_IPI", 7))
        return 0;

    return AVPROBE_SCORE_MAX - 1;
}

static int qoi_probe(const AVProbeData *p)
{
    if (memcmp(p->buf, "qoif", 4))
        return 0;

    if (AV_RB32(p->buf + 4) == 0 || AV_RB32(p->buf + 8) == 0)
        return 0;

    if (p->buf[12] != 3 && p->buf[12] != 4)
        return 0;

    if (p->buf[13] > 1)
        return 0;

    return AVPROBE_SCORE_MAX - 1;
}

static int gem_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    if ( AV_RB16(b     ) >= 1 && AV_RB16(b    ) <= 3  &&
         AV_RB16(b +  2) >= 8 && AV_RB16(b + 2) <= 779 &&
        (AV_RB16(b +  4) > 0  && AV_RB16(b + 4) <= 32) && /* planes */
        (AV_RB16(b +  6) > 0  && AV_RB16(b + 6) <= 8) && /* pattern_size */
         AV_RB16(b +  8) &&
         AV_RB16(b + 10) &&
         AV_RB16(b + 12) &&
         AV_RB16(b + 14)) {
        if (AV_RN32(b + 16) == AV_RN32("STTT") ||
            AV_RN32(b + 16) == AV_RN32("TIMG") ||
            AV_RN32(b + 16) == AV_RN32("XIMG"))
            return AVPROBE_SCORE_EXTENSION + 1;
        return AVPROBE_SCORE_EXTENSION / 4;
    }
    return 0;
}

static int vbn_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    if (AV_RL32(b    ) == VBN_MAGIC &&
        AV_RL32(b + 4) == VBN_MAJOR &&
        AV_RL32(b + 8) == VBN_MINOR)
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

#define IMAGEAUTO_DEMUXER_0(imgname, codecid)
#define IMAGEAUTO_DEMUXER_1(imgname, codecid)\
const FFInputFormat ff_image_ ## imgname ## _pipe_demuxer = {\
    .p.name         = AV_STRINGIFY(imgname) "_pipe",\
    .p.long_name    = NULL_IF_CONFIG_SMALL("piped " AV_STRINGIFY(imgname) " sequence"),\
    .p.priv_class   = &imagepipe_class,\
    .p.flags        = AVFMT_GENERIC_INDEX,\
    .priv_data_size = sizeof(VideoDemuxData),\
    .read_probe     = imgname ## _probe,\
    .read_header    = ff_img_read_header,\
    .read_packet    = ff_img_read_packet,\
    .raw_codec_id   = codecid,\
};

#define IMAGEAUTO_DEMUXER_2(imgname, codecid, enabled) \
        IMAGEAUTO_DEMUXER_ ## enabled(imgname, codecid)
#define IMAGEAUTO_DEMUXER_3(imgname, codecid, config) \
        IMAGEAUTO_DEMUXER_2(imgname, codecid, config)
#define IMAGEAUTO_DEMUXER_EXT(imgname, codecid, uppercase_name) \
        IMAGEAUTO_DEMUXER_3(imgname, AV_CODEC_ID_ ## codecid,   \
                            CONFIG_IMAGE_ ## uppercase_name ## _PIPE_DEMUXER)
#define IMAGEAUTO_DEMUXER(imgname, codecid)                     \
        IMAGEAUTO_DEMUXER_EXT(imgname, codecid, codecid)

IMAGEAUTO_DEMUXER(bmp,       BMP)
IMAGEAUTO_DEMUXER(cri,       CRI)
IMAGEAUTO_DEMUXER(dds,       DDS)
IMAGEAUTO_DEMUXER(dpx,       DPX)
IMAGEAUTO_DEMUXER(exr,       EXR)
IMAGEAUTO_DEMUXER(gem,       GEM)
IMAGEAUTO_DEMUXER(gif,       GIF)
IMAGEAUTO_DEMUXER_EXT(hdr,   RADIANCE_HDR, HDR)
IMAGEAUTO_DEMUXER_EXT(j2k,   JPEG2000, J2K)
IMAGEAUTO_DEMUXER_EXT(jpeg,  MJPEG, JPEG)
IMAGEAUTO_DEMUXER(jpegls,    JPEGLS)
IMAGEAUTO_DEMUXER(jpegxl,    JPEGXL)
IMAGEAUTO_DEMUXER(pam,       PAM)
IMAGEAUTO_DEMUXER(pbm,       PBM)
IMAGEAUTO_DEMUXER(pcx,       PCX)
IMAGEAUTO_DEMUXER(pfm,       PFM)
IMAGEAUTO_DEMUXER(pgm,       PGM)
IMAGEAUTO_DEMUXER(pgmyuv,    PGMYUV)
IMAGEAUTO_DEMUXER(pgx,       PGX)
IMAGEAUTO_DEMUXER(phm,       PHM)
IMAGEAUTO_DEMUXER(photocd,   PHOTOCD)
IMAGEAUTO_DEMUXER(pictor,    PICTOR)
IMAGEAUTO_DEMUXER(png,       PNG)
IMAGEAUTO_DEMUXER(ppm,       PPM)
IMAGEAUTO_DEMUXER(psd,       PSD)
IMAGEAUTO_DEMUXER(qdraw,     QDRAW)
IMAGEAUTO_DEMUXER(qoi,       QOI)
IMAGEAUTO_DEMUXER(sgi,       SGI)
IMAGEAUTO_DEMUXER(sunrast,   SUNRAST)
IMAGEAUTO_DEMUXER(svg,       SVG)
IMAGEAUTO_DEMUXER(tiff,      TIFF)
IMAGEAUTO_DEMUXER(vbn,       VBN)
IMAGEAUTO_DEMUXER(webp,      WEBP)
IMAGEAUTO_DEMUXER(xbm,       XBM)
IMAGEAUTO_DEMUXER(xpm,       XPM)
IMAGEAUTO_DEMUXER(xwd,       XWD)
