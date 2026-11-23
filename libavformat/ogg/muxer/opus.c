/*
 * Ogg Opus muxer mapping
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at free dot fr>
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

#include "libavcodec/bytestream.h"
#include "libavcodec/opus/tab.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavformat/internal.h"
#include "libavformat/ogg/muxer.h"

#define OPUS_HEADER_SIZE 19

static int opus_build_headers(AVCodecParameters *par,
                              OGGStreamContext *oggstream, int bitexact,
                              AVDictionary **m, AVChapter **chapters,
                              unsigned int nb_chapters)
{
    uint8_t *p;

    if (par->extradata_size < OPUS_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    /* first packet: Opus header */
    p = av_mallocz(par->extradata_size);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[0] = p;
    oggstream->header_len[0] = par->extradata_size;
    bytestream_put_buffer(&p, par->extradata, par->extradata_size);

    /* second packet: VorbisComment */
    p = ff_ogg_write_vorbiscomment(8, bitexact, &oggstream->header_len[1], m, 0, chapters, nb_chapters);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[1] = p;
    bytestream_put_buffer(&p, "OpusTags", 8);

    return 0;
}

static int opus_init(AVFormatContext *s, AVStream *st)
{
    OGGStreamContext *oggstream = st->priv_data;
    int err;

    avpriv_set_pts_info(st, 64, 1, OPUS_TIMESTAMP_RATE);
    err = opus_build_headers(st->codecpar, oggstream,
                             s->flags & AVFMT_FLAG_BITEXACT,
                             &st->metadata, s->chapters, s->nb_chapters);
    if (err) {
        av_log(s, AV_LOG_ERROR, "Error writing Opus headers\n");
        return err;
    }

    /* The pre-skip of the link's OpusHead, in the 48 kHz time base. */
    oggstream->packet_granule_offset = AV_RL16(oggstream->header[0] + 10);
    return 0;
}

const OGGMuxerMapping ff_ogg_opus_muxer_mapping = {
    .codec_id  = AV_CODEC_ID_OPUS,
    .new_links = 1,
    .init      = opus_init,
};
