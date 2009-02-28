/*
 * Raw FLAC demuxer
 * Copyright (c) 2001 Fabrice Bellard
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
#include "raw.h"
#include "id3v2.h"

static int flac_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    uint8_t buf[ID3v2_HEADER_SIZE];
    int ret;
    AVStream *st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_FLAC;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    /* the parameters will be extracted from the compressed bitstream */

    /* skip ID3v2 header if found */
    ret = get_buffer(s->pb, buf, ID3v2_HEADER_SIZE);
    if (ret == ID3v2_HEADER_SIZE && ff_id3v2_match(buf)) {
        int len = ff_id3v2_tag_len(buf);
        url_fseek(s->pb, len - ID3v2_HEADER_SIZE, SEEK_CUR);
    } else {
        url_fseek(s->pb, 0, SEEK_SET);
    }
    return 0;
}

static int flac_probe(AVProbeData *p)
{
    uint8_t *bufptr = p->buf;
    uint8_t *end    = p->buf + p->buf_size;

    if(ff_id3v2_match(bufptr))
        bufptr += ff_id3v2_tag_len(bufptr);

    if(bufptr > end-4 || memcmp(bufptr, "fLaC", 4)) return 0;
    else                                            return AVPROBE_SCORE_MAX/2;
}

AVInputFormat flac_demuxer = {
    "flac",
    NULL_IF_CONFIG_SMALL("raw FLAC"),
    0,
    flac_probe,
    flac_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "flac",
    .value = CODEC_ID_FLAC,
};
