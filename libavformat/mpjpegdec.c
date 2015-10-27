/*
 * Multipart JPEG format
 * Copyright (c) 2015 Luca Barbato
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

#include "libavutil/avstring.h"

#include "avformat.h"
#include "internal.h"

static int get_line(AVIOContext *pb, char *line, int line_size)
{
    int i = ff_get_line(pb, line, line_size);

    if (i > 1 && line[i - 2] == '\r')
        line[i - 2] = '\0';

    if (pb->error)
        return pb->error;

    if (pb->eof_reached)
        return AVERROR_EOF;

    return 0;
}


static void trim_right(char* p)
{
    char *end;
    if (!p || !*p)
        return;
    end = p + strlen(p) - 1;
    while (end != p && av_isspace(*end)) {
        *end = '\0';
        end--;
    }
}

static int split_tag_value(char **tag, char **value, char *line)
{
    char *p = line;
    int  foundData = 0;

    *tag = NULL;
    *value = NULL;


    while (*p != '\0' && *p != ':') {
        if (!av_isspace(*p)) {
            foundData = 1;
        }
        p++;
    }
    if (*p != ':')
        return foundData ? AVERROR_INVALIDDATA : 0;

    *p   = '\0';
    *tag = line;
    trim_right(*tag);

    p++;

    while (av_isspace(*p))
        p++;

    *value = p;
    trim_right(*value);

    return 0;
}

static int parse_multipart_header(AVIOContext *pb, void *log_ctx);

static int mpjpeg_read_probe(AVProbeData *p)
{
    AVIOContext *pb;
    int ret = 0;

    if (p->buf_size < 2 || p->buf[0] != '-' || p->buf[1] != '-')
        return 0;

    pb = avio_alloc_context(p->buf, p->buf_size, 0, NULL, NULL, NULL, NULL);
    if (!pb)
        return 0;

    ret = (parse_multipart_header(pb, NULL)>0)?AVPROBE_SCORE_MAX:0;

    av_free(pb);

    return ret;
}

static int mpjpeg_read_header(AVFormatContext *s)
{
    AVStream *st;
    char boundary[70 + 2 + 1];
    int64_t pos = avio_tell(s->pb);
    int ret;


    ret = get_line(s->pb, boundary, sizeof(boundary));
    if (ret < 0)
        return ret;

    if (strncmp(boundary, "--", 2))
        return AVERROR_INVALIDDATA;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = AV_CODEC_ID_MJPEG;

    avpriv_set_pts_info(st, 60, 1, 25);

    avio_seek(s->pb, pos, SEEK_SET);

    return 0;
}

static int parse_content_length(const char *value)
{
    long int val = strtol(value, NULL, 10);

    if (val == LONG_MIN || val == LONG_MAX)
        return AVERROR(errno);
    if (val > INT_MAX)
        return AVERROR(ERANGE);
    return val;
}

static int parse_multipart_header(AVIOContext *pb, void *log_ctx)
{
    char line[128];
    int found_content_type = 0;
    int ret, size = -1;

    // get the CRLF as empty string
    ret = get_line(pb, line, sizeof(line));
    if (ret < 0)
        return ret;

    /* some implementation do not provide the required
     * initial CRLF (see rfc1341 7.2.1)
     */
    if (!line[0]) {
        ret = get_line(pb, line, sizeof(line));
        if (ret < 0)
            return ret;
    }

    if (strncmp(line, "--", 2))
        return AVERROR_INVALIDDATA;

    while (!pb->eof_reached) {
        char *tag, *value;

        ret = get_line(pb, line, sizeof(line));
        if (ret < 0) {
            if (ret == AVERROR_EOF)
                break;
            return ret;
        }

        if (line[0] == '\0')
            break;

        ret = split_tag_value(&tag, &value, line);
        if (ret < 0)
            return ret;
        if (value==NULL || tag==NULL)
            break;

        if (!av_strcasecmp(tag, "Content-type")) {
            if (av_strcasecmp(value, "image/jpeg")) {
                if (log_ctx) {
                    av_log(log_ctx, AV_LOG_ERROR,
                           "Unexpected %s : %s\n",
                           tag, value);
                }

                return AVERROR_INVALIDDATA;
            } else
                found_content_type = 1;
        } else if (!av_strcasecmp(tag, "Content-Length")) {
            size = parse_content_length(value);
            if (size < 0)
                return size;
        }
    }

    if (!found_content_type || size < 0) {
        return AVERROR_INVALIDDATA;
    }

    return size;
}

static int mpjpeg_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    int size = parse_multipart_header(s->pb, s);

    if (size < 0)
        return size;

    ret = av_get_packet(s->pb, pkt, size);
    if (ret < 0)
        return ret;

    return 0;
}

AVInputFormat ff_mpjpeg_demuxer = {
    .name              = "mpjpeg",
    .long_name         = NULL_IF_CONFIG_SMALL("MIME multipart JPEG"),
    .mime_type         = "multipart/x-mixed-replace",
    .extensions        = "mjpg",
    .read_probe        = mpjpeg_read_probe,
    .read_header       = mpjpeg_read_header,
    .read_packet       = mpjpeg_read_packet,
};
