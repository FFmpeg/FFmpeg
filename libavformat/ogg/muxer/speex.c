/*
 * Ogg Speex muxer mapping
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
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavformat/ogg/muxer.h"

#define SPEEX_HEADER_SIZE 80

static int speex_build_headers(AVCodecParameters *par,
                               OGGStreamContext *oggstream, int bitexact,
                               AVDictionary **m)
{
    uint8_t *p;

    if (par->extradata_size < SPEEX_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    // first packet: Speex header
    p = av_mallocz(SPEEX_HEADER_SIZE);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[0] = p;
    oggstream->header_len[0] = SPEEX_HEADER_SIZE;
    bytestream_put_buffer(&p, par->extradata, SPEEX_HEADER_SIZE);
    AV_WL32(&oggstream->header[0][68], 0);  // set extra_headers to 0

    // second packet: VorbisComment
    p = ff_ogg_write_vorbiscomment(0, bitexact, &oggstream->header_len[1], m, 0, NULL, 0);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[1] = p;

    return 0;
}

static int speex_init(AVFormatContext *s, AVStream *st)
{
    int err = speex_build_headers(st->codecpar, st->priv_data,
                                  s->flags & AVFMT_FLAG_BITEXACT,
                                  &st->metadata);
    if (err)
        av_log(s, AV_LOG_ERROR, "Error writing Speex headers\n");
    return err;
}

const OGGMuxerMapping ff_ogg_speex_muxer_mapping = {
    .codec_id = AV_CODEC_ID_SPEEX,
    .init     = speex_init,
};
