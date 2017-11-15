/*
 * raw ADTS AAC demuxer
 * Copyright (c) 2008 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2009 Robert Swain ( rob opendot cl )
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "id3v1.h"
#include "apetag.h"

#define ADTS_HEADER_SIZE 7

static int adts_aac_probe(AVProbeData *p)
{
    int max_frames = 0, first_frames = 0;
    int fsize, frames;
    const uint8_t *buf0 = p->buf;
    const uint8_t *buf2;
    const uint8_t *buf;
    const uint8_t *end = buf0 + p->buf_size - 7;

    buf = buf0;

    for (; buf < end; buf = buf2 + 1) {
        buf2 = buf;

        for (frames = 0; buf2 < end; frames++) {
            uint32_t header = AV_RB16(buf2);
            if ((header & 0xFFF6) != 0xFFF0) {
                if (buf != buf0) {
                    // Found something that isn't an ADTS header, starting
                    // from a position other than the start of the buffer.
                    // Discard the count we've accumulated so far since it
                    // probably was a false positive.
                    frames = 0;
                }
                break;
            }
            fsize = (AV_RB32(buf2 + 3) >> 13) & 0x1FFF;
            if (fsize < 7)
                break;
            fsize = FFMIN(fsize, end - buf2);
            buf2 += fsize;
        }
        max_frames = FFMAX(max_frames, frames);
        if (buf == buf0)
            first_frames = frames;
    }

    if (first_frames >= 3)
        return AVPROBE_SCORE_EXTENSION + 1;
    else if (max_frames > 100)
        return AVPROBE_SCORE_EXTENSION;
    else if (max_frames >= 3)
        return AVPROBE_SCORE_EXTENSION / 2;
    else if (first_frames >= 1)
        return 1;
    else
        return 0;
}

static int adts_aac_read_header(AVFormatContext *s)
{
    AVStream *st;
    uint16_t state;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = s->iformat->raw_codec_id;
    st->need_parsing         = AVSTREAM_PARSE_FULL_RAW;

    ff_id3v1_read(s);
    if ((s->pb->seekable & AVIO_SEEKABLE_NORMAL) &&
        !av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX)) {
        int64_t cur = avio_tell(s->pb);
        ff_ape_parse_tag(s);
        avio_seek(s->pb, cur, SEEK_SET);
    }

    // skip data until the first ADTS frame is found
    state = avio_r8(s->pb);
    while (!avio_feof(s->pb) && avio_tell(s->pb) < s->probesize) {
        state = (state << 8) | avio_r8(s->pb);
        if ((state >> 4) != 0xFFF)
            continue;
        avio_seek(s->pb, -2, SEEK_CUR);
        break;
    }
    if ((state >> 4) != 0xFFF)
        return AVERROR_INVALIDDATA;

    // LCM of all possible ADTS sample rates
    avpriv_set_pts_info(st, 64, 1, 28224000);

    return 0;
}

static int adts_aac_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, fsize;

    ret = av_get_packet(s->pb, pkt, ADTS_HEADER_SIZE);
    if (ret < 0)
        return ret;
    if (ret < ADTS_HEADER_SIZE) {
        av_packet_unref(pkt);
        return AVERROR(EIO);
    }

    if ((AV_RB16(pkt->data) >> 4) != 0xfff) {
        av_packet_unref(pkt);
        return AVERROR_INVALIDDATA;
    }

    fsize = (AV_RB32(pkt->data + 3) >> 13) & 0x1FFF;
    if (fsize < ADTS_HEADER_SIZE) {
        av_packet_unref(pkt);
        return AVERROR_INVALIDDATA;
    }

    return av_append_packet(s->pb, pkt, fsize - ADTS_HEADER_SIZE);
}

AVInputFormat ff_aac_demuxer = {
    .name         = "aac",
    .long_name    = NULL_IF_CONFIG_SMALL("raw ADTS AAC (Advanced Audio Coding)"),
    .read_probe   = adts_aac_probe,
    .read_header  = adts_aac_read_header,
    .read_packet  = adts_aac_read_packet,
    .flags        = AVFMT_GENERIC_INDEX,
    .extensions   = "aac",
    .mime_type    = "audio/aac,audio/aacp,audio/x-aac",
    .raw_codec_id = AV_CODEC_ID_AAC,
};
