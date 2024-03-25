/*
 * Metadata demuxer
 * Copyright (c) 2010 Anton Khirnov
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

#include "libavutil/bprint.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "avformat.h"
#include "demux.h"
#include "ffmeta.h"
#include "libavutil/dict.h"

static int probe(const AVProbeData *p)
{
    if(!memcmp(p->buf, ID_STRING, strlen(ID_STRING)))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int64_t read_line_to_bprint_escaped(AVIOContext *s, AVBPrint *bp)
{
    int len, end;
    int64_t read = 0;
    char tmp[1024];
    char c;
    char prev = ' ';

    do {
        len = 0;
        do {
            c = avio_r8(s);
            end = prev != '\\' && (c == '\r' || c == '\n' || c == '\0');
            if (!end)
                tmp[len++] = c;
            prev = c;
        } while (!end && len < sizeof(tmp));
        av_bprint_append_data(bp, tmp, len);
        read += len;
    } while (!end);

    if (c == '\r' && avio_r8(s) != '\n' && !avio_feof(s))
        avio_skip(s, -1);

    if (!c && s->error)
        return s->error;

    if (!c && !read && avio_feof(s))
        return AVERROR_EOF;

    return read;
}

static void get_bprint_line(AVIOContext *s, AVBPrint *bp)
{

    do {
        av_bprint_clear(bp);
        read_line_to_bprint_escaped(s, bp);
    } while (!avio_feof(s) && (bp->str[0] == ';' || bp->str[0] == '#' || bp->str[0] == 0));
}

static void get_line(AVIOContext *s, uint8_t *buf, int size)
{
    do {
        uint8_t c;
        int i = 0;

        while ((c = avio_r8(s))) {
            if (c == '\\') {
                if (i < size - 1)
                    buf[i++] = c;
                c = avio_r8(s);
            } else if (c == '\n')
                break;

            if (i < size - 1)
                buf[i++] = c;
        }
        buf[i] = 0;
    } while (!avio_feof(s) && (buf[0] == ';' || buf[0] == '#' || buf[0] == 0));
}

static AVChapter *read_chapter(AVFormatContext *s)
{
    uint8_t line[256];
    int64_t start, end;
    AVRational tb = {1, 1e9};
    int ret;

    get_line(s->pb, line, sizeof(line));

    if (sscanf(line, "TIMEBASE=%d/%d", &tb.num, &tb.den))
        get_line(s->pb, line, sizeof(line));
    ret = sscanf(line, "START=%"SCNd64, &start);
    if (ret <= 0) {
        av_log(s, AV_LOG_ERROR, "Expected chapter start timestamp, found %s.\n", line);
        start = (s->nb_chapters && s->chapters[s->nb_chapters - 1]->end != AV_NOPTS_VALUE) ?
                 s->chapters[s->nb_chapters - 1]->end : 0;
    } else
        get_line(s->pb, line, sizeof(line));

    ret = sscanf(line, "END=%"SCNd64, &end);
    if (ret <= 0) {
        av_log(s, AV_LOG_ERROR, "Expected chapter end timestamp, found %s.\n", line);
        end = AV_NOPTS_VALUE;
    }

    return avpriv_new_chapter(s, s->nb_chapters, tb, start, end, NULL);
}

static uint8_t *unescape(const uint8_t *buf, int size)
{
    uint8_t *ret = av_malloc(size + 1);
    uint8_t *p1  = ret;
    const uint8_t *p2 = buf;

    if (!ret)
        return NULL;

    while (p2 < buf + size) {
        if (*p2 == '\\')
            p2++;
        *p1++ = *p2++;
    }
    *p1 = 0;
    return ret;
}

static int read_tag(const uint8_t *line, AVDictionary **m)
{
    uint8_t *key, *value;
    const uint8_t *p = line;

    /* find first not escaped '=' */
    while (1) {
        if (*p == '=')
            break;
        else if (*p == '\\')
            p++;

        if (*p++)
            continue;

        return 0;
    }

    if (!(key = unescape(line, p - line)))
        return AVERROR(ENOMEM);
    if (!(value = unescape(p + 1, strlen(p + 1)))) {
        av_free(key);
        return AVERROR(ENOMEM);
    }

    av_dict_set(m, key, value, AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
    return 0;
}

static int read_header(AVFormatContext *s)
{
    AVDictionary **m = &s->metadata;
    AVBPrint bp;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);

    while(!avio_feof(s->pb)) {
        get_bprint_line(s->pb, &bp);

        if (!memcmp(bp.str, ID_STREAM, strlen(ID_STREAM))) {
            AVStream *st = avformat_new_stream(s, NULL);

            if (!st)
                goto nomem;

            st->codecpar->codec_type = AVMEDIA_TYPE_DATA;
            st->codecpar->codec_id   = AV_CODEC_ID_FFMETADATA;

            m = &st->metadata;
        } else if (!memcmp(bp.str, ID_CHAPTER, strlen(ID_CHAPTER))) {
            AVChapter *ch = read_chapter(s);

            if (!ch)
                goto nomem;

            m = &ch->metadata;
        } else
            read_tag(bp.str, m);
    }

    av_bprint_finalize(&bp, NULL);

    s->start_time = 0;
    if (s->nb_chapters)
        s->duration = av_rescale_q(s->chapters[s->nb_chapters - 1]->end,
                                   s->chapters[s->nb_chapters - 1]->time_base,
                                   AV_TIME_BASE_Q);

    return 0;
nomem:
    av_bprint_finalize(&bp, NULL);

    return AVERROR(ENOMEM);
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return AVERROR_EOF;
}

const FFInputFormat ff_ffmetadata_demuxer = {
    .p.name      = "ffmetadata",
    .p.long_name = NULL_IF_CONFIG_SMALL("FFmpeg metadata in text"),
    .read_probe  = probe,
    .read_header = read_header,
    .read_packet = read_packet,
};
