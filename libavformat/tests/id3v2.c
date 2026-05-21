/*
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

/**
 * Reads ID3v2 tags from an MP3 file and prints the raw tag names
 * without applying FFmpeg's internal metadata key conversion.
 */

#include <stdarg.h>
#include <stdio.h>

#include "libavutil/dict.h"
#include "libavutil/log.h"
#include "libavformat/avformat.h"
#include "libavformat/id3v2.h"

static void log_to_stdout(void *avcl, int level, const char *fmt, va_list vl)
{
    if (level <= av_log_get_level())
        vprintf(fmt, vl);
}

int main(int argc, char *argv[])
{
    AVFormatContext *s;
    ID3v2ExtraMeta *extra_meta = NULL;
    const AVDictionaryEntry *tag = NULL;
    int ret;

    av_log_set_callback(log_to_stdout);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    s = avformat_alloc_context();
    if (!s)
        return 1;

    s->debug |= AV_FDEBUG_ID3V2;

    ret = avio_open(&s->pb, argv[1], AVIO_FLAG_READ);
    if (ret < 0) {
        fprintf(stderr, "Failed to open '%s'\n", argv[1]);
        avformat_free_context(s);
        return 1;
    }

    ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC, &extra_meta, 0);

    while ((tag = av_dict_iterate(s->metadata, tag)))
        printf("%s=%s\n", tag->key, tag->value);

    ff_id3v2_free_extra_meta(&extra_meta);
    avio_closep(&s->pb);
    avformat_free_context(s);

    return 0;
}
