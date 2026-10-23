/*
 * Copyright (c) 2026 Jun Zhao
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
#include <stdio.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavformat/avformat.h"
#include "libavformat/avio_internal.h"
#include "libavformat/demux.h"
#include "libavformat/id3v2.h"

static int readback(const uint8_t *buf, int size)
{
    AVFormatContext *in = avformat_alloc_context();
    FFIOContext read_pb;
    ID3v2ExtraMeta *extra = NULL;
    AVDictionary *meta = NULL;
    int ret = 1;

    if (!in)
        return ret;

    ffio_init_read_context(&read_pb, buf, size);
    ff_id3v2_read_dict(in, &read_pb.pub, &meta, ID3v2_DEFAULT_MAGIC, &extra);
    if (ff_id3v2_parse_chapters(in, extra) < 0)
        goto done;

    printf("chapters=%u\n", in->nb_chapters);
    for (unsigned i = 0; i < in->nb_chapters; i++) {
        const AVChapter *ch = in->chapters[i];
        const AVDictionaryEntry *title = av_dict_get(ch->metadata, "title", NULL, 0);

        printf("chapter=%u time_base=%d/%d start=%"PRId64" end=%"PRId64" title=%s\n",
               i, ch->time_base.num, ch->time_base.den, ch->start, ch->end,
               title ? title->value : "(missing)");
    }
    ret = 0;

done:
    av_dict_free(&meta);
    ff_id3v2_free_extra_meta(&extra);
    avformat_free_context(in);
    return ret;
}

/* The first chapter title is long enough that its TIT2 frame is at least
 * 128 bytes, so a plain size and a synchsafe size differ. */
static int roundtrip(int version)
{
    char long_title[151];
    AVFormatContext *out = NULL;
    AVIOContext *pb = NULL;
    ID3v2EncContext id3 = { 0 };
    uint8_t *buf = NULL;
    int size, chap_pos = -1, ret = 1;

    memset(long_title, 'L', sizeof(long_title) - 1);
    long_title[sizeof(long_title) - 1] = 0;

    out = avformat_alloc_context();
    if (!out ||
        !avpriv_new_chapter(out, 0, av_make_q(1, 1000), 0, 1000, long_title) ||
        !avpriv_new_chapter(out, 1, av_make_q(1, 1000), 1000, 2000, "End") ||
        avio_open_dyn_buf(&pb) < 0)
        goto done;

    ff_id3v2_start(&id3, pb, version, ID3v2_DEFAULT_MAGIC);
    if (ff_id3v2_write_metadata(out, pb, &id3) < 0)
        goto done;
    ff_id3v2_finish(&id3, pb, 10);
    size = avio_close_dyn_buf(pb, &buf);
    pb = NULL;
    if (size < ID3v2_HEADER_SIZE)
        goto done;

    printf("version=%d declared=%d actual=%d\n", version, ff_id3v2_tag_len(buf), size);
    for (int i = 0; i + 10 <= size; i++) {
        if (!memcmp(buf + i, "CHAP", 4)) {
            chap_pos = i;
            printf("CHAP size bytes=%02x %02x %02x %02x\n",
                   buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7]);
            break;
        }
    }
    if (readback(buf, size))
        goto done;

    /* Locate the first embedded TIT2 header inside the generated chapter.
     * Give it an oversized length, leaving the chapter and following CHAP
     * intact. The reader should retain both chapter timings. */
    if (chap_pos >= 0) {
        for (int i = chap_pos + 10; i + 10 <= size; i++) {
            if (!memcmp(buf + i, "TIT2", 4)) {
                memset(buf + i + 4, 0x7f, 4);
                printf("invalid subframe version=%d\n", version);
                if (readback(buf, size))
                    goto done;
                break;
            }
        }
    }
    ret = 0;

done:
    ffio_free_dyn_buf(&pb);
    av_free(buf);
    avformat_free_context(out);
    return ret;
}

int main(void)
{
    int ret = roundtrip(3);

    ret |= roundtrip(4);
    return ret;
}
