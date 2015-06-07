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

static int split_tag_value(char **tag, char **value, char *line)
{
    char *p = line;

    while (*p != '\0' && *p != ':')
        p++;
    if (*p != ':')
        return AVERROR_INVALIDDATA;

    *p   = '\0';
    *tag = line;

    p++;

    while (av_isspace(*p))
        p++;

    *value = p;

    return 0;
}

static int check_content_type(char *line)
{
    char *tag, *value;
    int ret = split_tag_value(&tag, &value, line);

    if (ret < 0)
        return ret;

    if (av_strcasecmp(tag, "Content-type") ||
        av_strcasecmp(value, "image/jpeg"))
        return AVERROR_INVALIDDATA;

    return 0;
}

static int mpjpeg_read_probe(AVProbeData *p)
{
    AVIOContext *pb;
    char line[128] = { 0 };
    int ret = 0;

    pb = avio_alloc_context(p->buf, p->buf_size, 0, NULL, NULL, NULL, NULL);
    if (!pb)
        return AVERROR(ENOMEM);

    if (p->buf_size < 2 || p->buf[0] != '-' || p->buf[1] != '-')
        return 0;

    while (!pb->eof_reached) {
        ret = get_line(pb, line, sizeof(line));
        if (ret < 0)
            break;

        ret = check_content_type(line);
        if (!ret) {
            ret = AVPROBE_SCORE_MAX;
            break;
        }
    }

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

static int parse_multipart_header(AVFormatContext *s)
{
    char line[128];
    int found_content_type = 0;
    int ret, size = -1;

    ret = get_line(s->pb, line, sizeof(line));
    if (ret < 0)
        return ret;

    if (strncmp(line, "--", 2))
        return AVERROR_INVALIDDATA;

    while (!s->pb->eof_reached) {
        char *tag, *value;

        ret = get_line(s->pb, line, sizeof(line));
        if (ret < 0)
            return ret;

        if (line[0] == '\0')
            break;

        ret = split_tag_value(&tag, &value, line);
        if (ret < 0)
            return ret;

        if (!av_strcasecmp(tag, "Content-type")) {
            if (av_strcasecmp(value, "image/jpeg")) {
                av_log(s, AV_LOG_ERROR,
                       "Unexpected %s : %s\n",
                       tag, value);
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
    int size = parse_multipart_header(s);

    if (size < 0)
        return size;

    ret = av_get_packet(s->pb, pkt, size);
    if (ret < 0)
        return ret;

    // trailing empty line
    avio_skip(s->pb, 2);

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
