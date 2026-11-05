/*
 * Copyright (C) 2005  Michael Ahlberg, Måns Rullgård
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/base64.h"
#include "libavutil/dict.h"
#include "libavutil/mem.h"

#include "libavcodec/bytestream.h"

#include "libavformat/avformat.h"
#include "libavformat/demux.h"
#include "libavformat/flac_picture.h"
#include "libavformat/internal.h"
#include "libavformat/metadata.h"
#include "libavformat/vorbis/comment.h"

static int ogm_chapter(AVFormatContext *as, const uint8_t *key, const uint8_t *val)
{
    int i, cnum, h, m, s, ms, keylen = strlen(key);
    AVChapter *chapter = NULL;

    if (keylen < 9 || av_strncasecmp(key, "CHAPTER", 7) || sscanf(key+7, "%03d", &cnum) != 1)
        return 0;

    if (keylen <= 10) {
        if (sscanf(val, "%02d:%02d:%02d.%03d", &h, &m, &s, &ms) < 4)
            return 0;

        avpriv_new_chapter(as, cnum, (AVRational) { 1, 1000 },
                           ms + 1000 * (s + 60 * (m + 60 * h)),
                           AV_NOPTS_VALUE, NULL);
    } else if (!av_strcasecmp(key + keylen - 4, "NAME")) {
        for (i = 0; i < as->nb_chapters; i++)
            if (as->chapters[i]->id == cnum) {
                chapter = as->chapters[i];
                break;
            }
        if (!chapter)
            return 0;

        av_dict_set(&chapter->metadata, "title", val, 0);
    } else
        return 0;

    return 1;
}

int ff_vorbis_comment_read_stream(AVFormatContext *as, AVStream *st,
                                  const uint8_t *buf, int size)
{
    int updates = ff_vorbis_comment_read(as, &st->metadata, buf, size, 1);

    if (updates > 0) {
        st->event_flags |= AVSTREAM_EVENT_FLAG_METADATA_UPDATED;
    }

    return updates;
}

/**
 * This function temporarily modifies the (const qualified) input buffer
 * and reverts its changes before return. The input buffer needs to have
 * at least one byte of padding.
 */
static int vorbis_parse_single_comment(AVFormatContext *as, AVDictionary **m,
                                       const uint8_t *buf, uint32_t size,
                                       int *updates, int parse_picture)
{
    char *t = (char*)buf, *v = memchr(t, '=', size);
    int tl, vl;
    char backup;

    if (!v)
        return 0;

    tl = v - t;
    vl = size - tl - 1;
    v++;

    if (!tl || !vl)
        return 0;

    t[tl]  = 0;

    backup = v[vl];
    v[vl]  = 0;

    /* The format in which the pictures are stored is the FLAC format.
     * Xiph says: "The binary FLAC picture structure is base64 encoded
     * and placed within a VorbisComment with the tag name
     * 'METADATA_BLOCK_PICTURE'. This is the preferred and
     * recommended way of embedding cover art within VorbisComments."
     */
    if (!av_strcasecmp(t, "METADATA_BLOCK_PICTURE") && parse_picture) {
        int ret, len = AV_BASE64_DECODE_SIZE(vl);
        uint8_t *pict = av_malloc(len + AV_INPUT_BUFFER_PADDING_SIZE);

        if (!pict) {
            av_log(as, AV_LOG_WARNING, "out-of-memory error. Skipping cover art block.\n");
            goto end;
        }
        ret = av_base64_decode(pict, v, len);
        if (ret > 0)
            ret = ff_flac_parse_picture(as, &pict, ret, 0);
        av_freep(&pict);
        if (ret < 0) {
            av_log(as, AV_LOG_WARNING, "Failed to parse cover art block.\n");
            goto end;
        }
    } else if (!ogm_chapter(as, t, v)) {
        (*updates)++;
        if (av_dict_get(*m, t, NULL, 0))
            av_dict_set(m, t, ";", AV_DICT_APPEND);
        av_dict_set(m, t, v, AV_DICT_APPEND);
    }
end:
    t[tl] = '=';
    v[vl] = backup;

    return 0;
}

int ff_vorbis_comment_read(AVFormatContext *as, AVDictionary **m,
                           const uint8_t *buf, int size,
                           int parse_picture)
{
    const uint8_t *p   = buf;
    const uint8_t *end = buf + size;
    int updates        = 0;
    unsigned n;
    int s, ret;

    /* must have vendor_length and user_comment_list_length */
    if (size < 8)
        return AVERROR_INVALIDDATA;

    s = bytestream_get_le32(&p);

    if (end - p - 4 < s || s < 0)
        return AVERROR_INVALIDDATA;

    p += s;

    n = bytestream_get_le32(&p);

    while (end - p >= 4 && n > 0) {
        s = bytestream_get_le32(&p);

        if (end - p < s || s < 0)
            break;

        ret = vorbis_parse_single_comment(as, m, p, s, &updates, parse_picture);
        if (ret < 0)
            return ret;
        p += s;
        n--;
    }

    if (p != end)
        av_log(as, AV_LOG_INFO,
               "%td bytes of comment header remain\n", end - p);
    if (n > 0)
        av_log(as, AV_LOG_INFO,
               "truncated comment header, %i comments not found\n", n);

    ff_metadata_conv(m, NULL, ff_vorbis_comment_metadata_conv);

    return updates;
}
