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

#include "avformat.h"
#include "ffmeta.h"


static void write_escape_str(ByteIOContext *s, const uint8_t *str)
{
    const uint8_t *p = str;

    while (*p) {
        if (*p == '#' || *p == ';' || *p == '=' || *p == '\\' || *p == '\n')
            put_byte(s, '\\');
        put_byte(s, *p);
        p++;
    }
}

static void write_tags(ByteIOContext *s, AVMetadata *m)
{
    AVMetadataTag *t = NULL;
    while ((t = av_metadata_get(m, "", t, AV_METADATA_IGNORE_SUFFIX))) {
        write_escape_str(s, t->key);
        put_byte(s, '=');
        write_escape_str(s, t->value);
        put_byte(s, '\n');
    }
}

static int write_header(AVFormatContext *s)
{
    put_tag(s->pb, ID_STRING);
    put_byte(s->pb, '1');          // version
    put_byte(s->pb, '\n');
    put_flush_packet(s->pb);
    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    int i;

    write_tags(s->pb, s->metadata);

    for (i = 0; i < s->nb_streams; i++) {
        put_tag(s->pb, ID_STREAM);
        put_byte(s->pb, '\n');
        write_tags(s->pb, s->streams[i]->metadata);
    }

    for (i = 0; i < s->nb_chapters; i++) {
        AVChapter *ch = s->chapters[i];
        put_tag(s->pb, ID_CHAPTER);
        put_byte(s->pb, '\n');
        url_fprintf(s->pb, "TIMEBASE=%d/%d\n", ch->time_base.num, ch->time_base.den);
        url_fprintf(s->pb, "START=%lld\n", ch->start);
        url_fprintf(s->pb, "END=%lld\n",   ch->end);
        write_tags(s->pb, ch->metadata);
    }

    put_flush_packet(s->pb);

    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    return 0;
}

AVOutputFormat ffmetadata_muxer = {
    .name          = "ffmetadata",
    .long_name     = NULL_IF_CONFIG_SMALL("FFmpeg metadata in text format"),
    .extensions    = "ffmeta",
    .write_header  = write_header,
    .write_packet  = write_packet,
    .write_trailer = write_trailer,
    .flags         = AVFMT_NOTIMESTAMPS | AVFMT_NOSTREAMS,
};
