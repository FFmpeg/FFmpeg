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

#define _BSD_SOURCE
#include <sys/stat.h>
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "img2.h"

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
static int find_image_range(int *pfirst_index, int *plast_index,
                            const char *path, int start_index, int start_index_range)
{
    char buf[1024];
    int range, last_index, range1, first_index;

    /* find the first image */
    for (first_index = start_index; first_index < start_index + start_index_range; first_index++) {
        if (av_get_frame_filename(buf, sizeof(buf), path, first_index) < 0) {
            *pfirst_index =
            *plast_index  = 1;
            if (avio_check(buf, AVIO_FLAG_READ) > 0)
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

static int img_read_probe(AVProbeData *p)
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

    av_strlcpy(s->path, s1->filename, sizeof(s->path));
    s->img_number = 0;
    s->img_count  = 0;

    /* find format */
    if (s1->iformat->flags & AVFMT_NOFILE)
        s->is_pipe = 0;
    else {
        s->is_pipe       = 1;
        st->need_parsing = AVSTREAM_PARSE_FULL;
    }

    if (s->ts_from_file == 2) {
#if !HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
        av_log(s1, AV_LOG_ERROR, "POSIX.1-2008 not supported, nanosecond file timestamps unavailable\n");
        return AVERROR(ENOSYS);
#endif
        avpriv_set_pts_info(st, 64, 1, 1000000000);
    } else if (s->ts_from_file)
        avpriv_set_pts_info(st, 64, 1, 1);
    else
        avpriv_set_pts_info(st, 64, s->framerate.den, s->framerate.num);

    if (s->width && s->height) {
        st->codec->width  = s->width;
        st->codec->height = s->height;
    }

    if (!s->is_pipe) {
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
            if (find_image_range(&first_index, &last_index, s->path,
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
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id   = s1->video_codec_id;
    } else if (s1->audio_codec_id) {
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id   = s1->audio_codec_id;
    } else if (s1->iformat->raw_codec_id) {
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id   = s1->iformat->raw_codec_id;
    } else {
        const char *str = strrchr(s->path, '.');
        s->split_planes       = str && !av_strcasecmp(str + 1, "y");
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        if (s1->pb) {
            int probe_buffer_size = 2048;
            uint8_t *probe_buffer = av_realloc(NULL, probe_buffer_size + AVPROBE_PADDING_SIZE);
            AVInputFormat *fmt = NULL;
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
            pd.filename = s1->filename;

            while ((fmt = av_iformat_next(fmt))) {
                if (fmt->read_header != ff_img_read_header ||
                    !fmt->read_probe ||
                    (fmt->flags & AVFMT_NOFILE) ||
                    !fmt->raw_codec_id)
                    continue;
                if (fmt->read_probe(&pd) > 0) {
                    st->codec->codec_id = fmt->raw_codec_id;
                    break;
                }
            }
            if (s1->flags & AVFMT_FLAG_CUSTOM_IO) {
                avio_seek(s1->pb, 0, SEEK_SET);
            } else
                ffio_rewind_with_probe_data(s1->pb, &probe_buffer, probe_buffer_size);
        }
        if (st->codec->codec_id == AV_CODEC_ID_NONE)
            st->codec->codec_id = ff_guess_image2_codec(s->path);
        if (st->codec->codec_id == AV_CODEC_ID_LJPEG)
            st->codec->codec_id = AV_CODEC_ID_MJPEG;
        if (st->codec->codec_id == AV_CODEC_ID_ALIAS_PIX) // we cannot distingiush this from BRENDER_PIX
            st->codec->codec_id = AV_CODEC_ID_NONE;
    }
    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
        pix_fmt != AV_PIX_FMT_NONE)
        st->codec->pix_fmt = pix_fmt;

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
    AVCodecContext *codec = s1->streams[0]->codec;

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
            if (avio_open2(&f[i], filename, AVIO_FLAG_READ,
                           &s1->interrupt_callback, NULL) < 0) {
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

        if (codec->codec_id == AV_CODEC_ID_NONE) {
            AVProbeData pd = { 0 };
            AVInputFormat *ifmt;
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

            ifmt = av_probe_input_format3(&pd, 1, &score);
            if (ifmt && ifmt->read_packet == ff_img_read_packet && ifmt->raw_codec_id)
                codec->codec_id = ifmt->raw_codec_id;
        }

        if (codec->codec_id == AV_CODEC_ID_RAWVIDEO && !codec->width)
            infer_size(&codec->width, &codec->height, size[0]);
    } else {
        f[0] = s1->pb;
        if (avio_feof(f[0]) && s->loop && s->is_pipe)
            avio_seek(f[0], 0, SEEK_SET);
        if (avio_feof(f[0]))
            return AVERROR_EOF;
        if (s->frame_size > 0) {
            size[0] = s->frame_size;
        } else if (!s1->streams[0]->parser) {
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
            if (!s->is_pipe)
                avio_closep(&f[i]);
            if (ret[i] > 0)
                pkt->size += ret[i];
        }
    }

    if (ret[0] <= 0 || ret[1] < 0 || ret[2] < 0) {
        av_free_packet(pkt);
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
        s->img_count++;
        s->img_number++;
        s->pts++;
        return 0;
    }

fail:
    if (!s->is_pipe) {
        for (i = 0; i < 3; i++) {
            avio_closep(&f[i]);
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
        s1->img_number = st->index_entries[index].pos;
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
const AVOption ff_img_options[] = {
    { "framerate",    "set the video framerate",             OFFSET(framerate),    AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, 0,   DEC },
    { "loop",         "force loop over input file sequence", OFFSET(loop),         AV_OPT_TYPE_INT,    {.i64 = 0   }, 0, 1,       DEC },

    { "pattern_type", "set pattern type",                    OFFSET(pattern_type), AV_OPT_TYPE_INT,    {.i64=PT_GLOB_SEQUENCE}, 0,       INT_MAX, DEC, "pattern_type"},
    { "glob_sequence","select glob/sequence pattern type",   0, AV_OPT_TYPE_CONST,  {.i64=PT_GLOB_SEQUENCE}, INT_MIN, INT_MAX, DEC, "pattern_type" },
    { "glob",         "select glob pattern type",            0, AV_OPT_TYPE_CONST,  {.i64=PT_GLOB         }, INT_MIN, INT_MAX, DEC, "pattern_type" },
    { "sequence",     "select sequence pattern type",        0, AV_OPT_TYPE_CONST,  {.i64=PT_SEQUENCE     }, INT_MIN, INT_MAX, DEC, "pattern_type" },
    { "none",         "disable pattern matching",            0, AV_OPT_TYPE_CONST,  {.i64=PT_NONE         }, INT_MIN, INT_MAX, DEC, "pattern_type" },

    { "pixel_format", "set video pixel format",              OFFSET(pixel_format), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0,       DEC },
    { "start_number", "set first number in the sequence",    OFFSET(start_number), AV_OPT_TYPE_INT,    {.i64 = 0   }, INT_MIN, INT_MAX, DEC },
    { "start_number_range", "set range for looking at the first sequence number", OFFSET(start_number_range), AV_OPT_TYPE_INT, {.i64 = 5}, 1, INT_MAX, DEC },
    { "video_size",   "set video size",                      OFFSET(width),        AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0,   DEC },
    { "frame_size",   "force frame size in bytes",           OFFSET(frame_size),   AV_OPT_TYPE_INT,    {.i64 = 0   }, 0, INT_MAX, DEC },
    { "ts_from_file", "set frame timestamp from file's one", OFFSET(ts_from_file), AV_OPT_TYPE_INT,    {.i64 = 0   }, 0, 2,       DEC, "ts_type" },
    { "none", "none",                   0, AV_OPT_TYPE_CONST,    {.i64 = 0   }, 0, 2,       DEC, "ts_type" },
    { "sec",  "second precision",       0, AV_OPT_TYPE_CONST,    {.i64 = 1   }, 0, 2,       DEC, "ts_type" },
    { "ns",   "nano second precision",  0, AV_OPT_TYPE_CONST,    {.i64 = 2   }, 0, 2,       DEC, "ts_type" },
    { NULL },
};

#if CONFIG_IMAGE2_DEMUXER
static const AVClass img2_class = {
    .class_name = "image2 demuxer",
    .item_name  = av_default_item_name,
    .option     = ff_img_options,
    .version    = LIBAVUTIL_VERSION_INT,
};
AVInputFormat ff_image2_demuxer = {
    .name           = "image2",
    .long_name      = NULL_IF_CONFIG_SMALL("image2 sequence"),
    .priv_data_size = sizeof(VideoDemuxData),
    .read_probe     = img_read_probe,
    .read_header    = ff_img_read_header,
    .read_packet    = ff_img_read_packet,
    .read_close     = img_read_close,
    .read_seek      = img_read_seek,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &img2_class,
};
#endif
#if CONFIG_IMAGE2PIPE_DEMUXER
static const AVClass img2pipe_class = {
    .class_name = "image2pipe demuxer",
    .item_name  = av_default_item_name,
    .option     = ff_img_options,
    .version    = LIBAVUTIL_VERSION_INT,
};
AVInputFormat ff_image2pipe_demuxer = {
    .name           = "image2pipe",
    .long_name      = NULL_IF_CONFIG_SMALL("piped image2 sequence"),
    .priv_data_size = sizeof(VideoDemuxData),
    .read_header    = ff_img_read_header,
    .read_packet    = ff_img_read_packet,
    .priv_class     = &img2pipe_class,
};
#endif

static int bmp_probe(AVProbeData *p)
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

static int dds_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (   AV_RB64(b) == 0x444453207c000000
        && AV_RL32(b +  8)
        && AV_RL32(b + 12))
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int dpx_probe(AVProbeData *p)
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

static int exr_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RL32(b) == 20000630)
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int j2k_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB64(b) == 0x0000000c6a502020 ||
        AV_RB32(b) == 0xff4fff51)
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int jpeg_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;
    int i, state = 0xD8, exif_size = 0;

    if (AV_RB16(b) != 0xFFD8 ||
        AV_RB32(b) == 0xFFD8FFF7)
    return 0;

    b += 2;
    if (AV_RB16(b) == 0xFFE1 && AV_RB32(b + 4) == AV_RB32("Exif")) {
        exif_size = AV_RB16(b + 2) + 2;
        b += exif_size;
    }
    for (i = 0; i + exif_size < p->buf_size - 2; i++) {
        int c;
        if (b[i] != 0xFF)
            continue;
        c = b[i + 1];
        switch (c) {
        case 0xD8:
            return 0;
        case 0xC0:
        case 0xC1:
        case 0xC2:
        case 0xC3:
        case 0xC5:
        case 0xC6:
        case 0xC7:
            if (state != 0xD8)
                return 0;
            state = 0xC0;
            break;
        case 0xDA:
            if (state != 0xC0)
                return 0;
            state = 0xDA;
            break;
        case 0xD9:
            if (state != 0xDA)
                return 0;
            state = 0xD9;
            break;
        default:
            if (  (c >= 0x02 && c <= 0xBF)
                || c == 0xC8)
                return 0;
        }
    }

    if (state == 0xD9)
        return AVPROBE_SCORE_EXTENSION + 1;
    return AVPROBE_SCORE_EXTENSION / 8;
}

static int jpegls_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b) == 0xffd8fff7)
         return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int qdraw_probe(AVProbeData *p)
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

static int pictor_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RL16(b) == 0x1234)
        return AVPROBE_SCORE_EXTENSION / 4;
    return 0;
}

static int png_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB64(b) == 0x89504e470d0a1a0a)
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int sgi_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB16(b) == 474 &&
        (b[2] & ~1) == 0 &&
        (b[3] & ~3) == 0 && b[3] &&
        (AV_RB16(b + 4) & ~7) == 0 && AV_RB16(b + 4))
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int sunrast_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b) == 0x59a66a95)
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int tiff_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b) == 0x49492a00 ||
        AV_RB32(b) == 0x4D4D002a)
        return AVPROBE_SCORE_EXTENSION + 1;
    return 0;
}

static int webp_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b)     == 0x52494646 &&
        AV_RB32(b + 8) == 0x57454250)
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

#define IMAGEAUTO_DEMUXER(imgname, codecid)\
static const AVClass imgname ## _class = {\
    .class_name = AV_STRINGIFY(imgname) " demuxer",\
    .item_name  = av_default_item_name,\
    .option     = ff_img_options,\
    .version    = LIBAVUTIL_VERSION_INT,\
};\
AVInputFormat ff_image_ ## imgname ## _pipe_demuxer = {\
    .name           = AV_STRINGIFY(imgname) "_pipe",\
    .long_name      = NULL_IF_CONFIG_SMALL("piped " AV_STRINGIFY(imgname) " sequence"),\
    .priv_data_size = sizeof(VideoDemuxData),\
    .read_probe     = imgname ## _probe,\
    .read_header    = ff_img_read_header,\
    .read_packet    = ff_img_read_packet,\
    .priv_class     = & imgname ## _class,\
    .flags          = AVFMT_GENERIC_INDEX, \
    .raw_codec_id   = codecid,\
};

IMAGEAUTO_DEMUXER(bmp,     AV_CODEC_ID_BMP)
IMAGEAUTO_DEMUXER(dds,     AV_CODEC_ID_DDS)
IMAGEAUTO_DEMUXER(dpx,     AV_CODEC_ID_DPX)
IMAGEAUTO_DEMUXER(exr,     AV_CODEC_ID_EXR)
IMAGEAUTO_DEMUXER(j2k,     AV_CODEC_ID_JPEG2000)
IMAGEAUTO_DEMUXER(jpeg,    AV_CODEC_ID_MJPEG)
IMAGEAUTO_DEMUXER(jpegls,  AV_CODEC_ID_JPEGLS)
IMAGEAUTO_DEMUXER(pictor,  AV_CODEC_ID_PICTOR)
IMAGEAUTO_DEMUXER(png,     AV_CODEC_ID_PNG)
IMAGEAUTO_DEMUXER(qdraw,   AV_CODEC_ID_QDRAW)
IMAGEAUTO_DEMUXER(sgi,     AV_CODEC_ID_SGI)
IMAGEAUTO_DEMUXER(sunrast, AV_CODEC_ID_SUNRAST)
IMAGEAUTO_DEMUXER(tiff,    AV_CODEC_ID_TIFF)
IMAGEAUTO_DEMUXER(webp,    AV_CODEC_ID_WEBP)
