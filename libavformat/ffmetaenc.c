/*
 * Metadata muxer
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

#include <inttypes.h>

#include "avformat.h"
#include "ffmeta.h"
#include "libavutil/dict.h"


static void write_escape_str(AVIOContext *s, const uint8_t *str)
{
    const uint8_t *p = str;

    while (*p) {
        if (*p == '#' || *p == ';' || *p == '=' || *p == '\\' || *p == '\n')
            avio_w8(s, '\\');
        avio_w8(s, *p);
        p++;
    }
}

static void write_tags(AVIOContext *s, AVDictionary *m)
{
    AVDictionaryEntry *t = NULL;
    while ((t = av_dict_get(m, "", t, AV_DICT_IGNORE_SUFFIX))) {
        write_escape_str(s, t->key);
        avio_w8(s, '=');
        write_escape_str(s, t->value);
        avio_w8(s, '\n');
    }
}

static int write_header(AVFormatContext *s)
{
    avio_write(s->pb, ID_STRING, sizeof(ID_STRING) - 1);
    avio_w8(s->pb, '1');          // version
    avio_w8(s->pb, '\n');
    avio_flush(s->pb);
    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    int i;

    write_tags(s->pb, s->metadata);

    for (i = 0; i < s->nb_streams; i++) {
        avio_write(s->pb, ID_STREAM, sizeof(ID_STREAM) - 1);
        avio_w8(s->pb, '\n');
        write_tags(s->pb, s->streams[i]->metadata);
    }

    for (i = 0; i < s->nb_chapters; i++) {
        AVChapter *ch = s->chapters[i];
        avio_write(s->pb, ID_CHAPTER, sizeof(ID_CHAPTER) - 1);
        avio_w8(s->pb, '\n');
        avio_printf(s->pb, "TIMEBASE=%d/%d\n", ch->time_base.num, ch->time_base.den);
        avio_printf(s->pb, "START=%"PRId64"\n", ch->start);
        avio_printf(s->pb, "END=%"PRId64"\n",   ch->end);
        write_tags(s->pb, ch->metadata);
    }

    avio_flush(s->pb);

    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    return 0;
}

AVOutputFormat ff_ffmetadata_muxer = {
    .name          = "ffmetadata",
    .long_name     = NULL_IF_CONFIG_SMALL("FFmpeg metadata in text format"),
    .extensions    = "ffmeta",
    .write_header  = write_header,
    .write_packet  = write_packet,
    .write_trailer = write_trailer,
    .flags         = AVFMT_NOTIMESTAMPS | AVFMT_NOSTREAMS,
};
