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
    const AVDictionaryEntry *album;
    int ret = 1;

    if (!in)
        return ret;

    ffio_init_read_context(&read_pb, buf, size);
    ff_id3v2_read_dict(in, &read_pb.pub, &meta, ID3v2_DEFAULT_MAGIC, &extra);
    if (ff_id3v2_parse_chapters(in, extra) < 0)
        goto done;

    printf("chapters=%u", in->nb_chapters);
    album = av_dict_get(meta, "album", NULL, 0);
    if (album)
        printf(" album=%s", album->value);
    printf("\n");
    for (unsigned i = 0; i < in->nb_chapters; i++) {
        const AVChapter *ch = in->chapters[i];
        const AVDictionaryEntry *title  = av_dict_get(ch->metadata, "title",  NULL, 0);
        const AVDictionaryEntry *artist = av_dict_get(ch->metadata, "artist", NULL, 0);

        printf("chapter=%u time_base=%d/%d start=%"PRId64" end=%"PRId64" title=%s",
               i, ch->time_base.num, ch->time_base.den, ch->start, ch->end,
               title ? title->value : "(missing)");
        if (artist)
            printf(" artist=%s", artist->value);
        printf("\n");
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
static int roundtrip(int version, int ambiguous)
{
    char long_title[225];
    int title_len = ambiguous ? 224 : 150;
    AVFormatContext *out = NULL;
    AVIOContext *pb = NULL;
    ID3v2EncContext id3 = { 0 };
    uint8_t *buf = NULL;
    int size, chap_pos = -1, ret = 1;

    memset(long_title, 'L', title_len);
    long_title[title_len] = 0;

    out = avformat_alloc_context();
    if (!out ||
        !avpriv_new_chapter(out, 0, av_make_q(1, 1000), 0, 1000, long_title) ||
        !avpriv_new_chapter(out, 1, av_make_q(1, 1000), 1000, 2000, "End") ||
        avio_open_dyn_buf(&pb) < 0)
        goto done;

    ff_id3v2_start(&id3, pb, version, ID3v2_DEFAULT_MAGIC);
    if (ff_id3v2_write_metadata(out, pb, &id3) < 0)
        goto done;
    ff_id3v2_finish(&id3, pb, ambiguous ? 300 : 10);
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

    /* A 224-byte title makes the CHAP payload 256 bytes. First test a valid
     * synchsafe 256-byte CHAP (00 00 02 00, raw 512) followed by an empty
     * TPE1 frame and a TALB frame inside the 300-byte padding area, so
     * check_tag() must accept nlen == 0 at the 256-byte boundary rather than
     * falling through to the raw 512-byte offset in zero padding. Then test
     * the non-conforming plain size 00 00 01 00, whose high bits are clear
     * so the synchsafe candidate at 128 bytes must be rejected by checking
     * the oversized LLLL frame header inside the title. */
    if (ambiguous) {
        if (chap_pos >= 0 && chap_pos + 10 + 256 + 45 + 27 <= size) {
            uint8_t ch1_copy[45];
            memcpy(ch1_copy, buf + chap_pos + 10 + 256, sizeof(ch1_copy));
            memcpy(buf + chap_pos + 10 + 256, "TPE1\x00\x00\x00\x00\x00\x00", 10);
            memcpy(buf + chap_pos + 10 + 266, "TALB\x00\x00\x00\x07\x00\x00\x03World", 17);
            memcpy(buf + chap_pos + 10 + 283, ch1_copy, sizeof(ch1_copy));
            printf("empty next frame version=4\n");
            if (readback(buf, size))
                goto done;
            memcpy(buf + chap_pos + 10 + 256, ch1_copy, sizeof(ch1_copy));
            memset(buf + chap_pos + 10 + 301, 0, 27);

            buf[chap_pos + 6] = 0x01;
            buf[chap_pos + 7] = 0x00;
            printf("ambiguous plain CHAP version=4\n");
            if (readback(buf, size))
                goto done;
        }
        ret = 0;
        goto done;
    }

    /* Non-conforming v2.4 CHAP with a plain 32-bit size (0x000000b6) whose
     * high bit is set and whose payload contains uppercase ASCII at the
     * stripped offset (0x36). Also zero the 4 size bytes after "LLLL" at
     * offset 0x36 once to verify that is_syncsafe() rejects the 0x36
     * candidate even when check_tag() would accept an empty frame header. */
    if (version == 4 && chap_pos >= 0) {
        uint8_t saved[4];
        buf[chap_pos + 6] = 0x00;
        buf[chap_pos + 7] = 0xb6;
        printf("non-synchsafe CHAP version=4\n");
        if (readback(buf, size))
            goto done;

        memcpy(saved, buf + chap_pos + 10 + 0x36 + 4, sizeof(saved));
        memset(buf + chap_pos + 10 + 0x36 + 4, 0, sizeof(saved));
        printf("high-bit plain CHAP with fitting fake header version=4\n");
        if (readback(buf, size))
            goto done;
        memcpy(buf + chap_pos + 10 + 0x36 + 4, saved, sizeof(saved));
        buf[chap_pos + 6] = 0x01;
        buf[chap_pos + 7] = 0x36;
    }

    /* Split the 162-byte TIT2 subframe into a 22-byte TPE1 subframe with
     * unread trailing bytes after its first NUL terminator, followed by a
     * 140-byte TIT2 subframe, then test an oversized subframe length. */
    if (chap_pos >= 0) {
        for (int i = chap_pos + 10; i + 33 <= size; i++) {
            if (!memcmp(buf + i, "TIT2", 4)) {
                uint8_t enc = buf[i + 10];
                memcpy(buf + i, "TPE1", 4);
                buf[i + 6] = 0;
                buf[i + 7] = 12;
                buf[i + 13] = 0;
                memcpy(buf + i + 22, "TIT2\x00\x00", 6);
                buf[i + 28] = version == 4 ? 0x01 : 0x00;
                buf[i + 29] = version == 4 ? 0x02 : 0x82;
                buf[i + 30] = 0;
                buf[i + 31] = 0;
                buf[i + 32] = enc;
                printf("trailing text subframe version=%d\n", version);
                if (readback(buf, size))
                    goto done;

                memcpy(buf + i, "TIT2", 4);
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
    int ret = roundtrip(3, 0);

    ret |= roundtrip(4, 0);
    ret |= roundtrip(4, 1);
    return ret;
}
