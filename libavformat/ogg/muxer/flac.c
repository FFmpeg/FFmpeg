/*
 * Ogg FLAC muxer mapping
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
#include "libavcodec/flac.h"
#include "libavutil/mem.h"
#include "libavformat/ogg/muxer.h"

static int flac_build_headers(AVCodecParameters *par,
                              OGGStreamContext *oggstream, int bitexact,
                              AVDictionary **m)
{
    uint8_t *p;

    if (par->extradata_size < FLAC_STREAMINFO_SIZE)
        return AVERROR(EINVAL);

    // first packet: STREAMINFO
    oggstream->header_len[0] = 51;
    oggstream->header[0] = av_mallocz(51); // per ogg flac specs
    p = oggstream->header[0];
    if (!p)
        return AVERROR(ENOMEM);
    bytestream_put_byte(&p, 0x7F);
    bytestream_put_buffer(&p, "FLAC", 4);
    bytestream_put_byte(&p, 1); // major version
    bytestream_put_byte(&p, 0); // minor version
    bytestream_put_be16(&p, 1); // headers packets without this one
    bytestream_put_buffer(&p, "fLaC", 4);
    bytestream_put_byte(&p, 0x00); // streaminfo
    bytestream_put_be24(&p, 34);
    bytestream_put_buffer(&p, par->extradata, FLAC_STREAMINFO_SIZE);

    // second packet: VorbisComment
    p = ff_ogg_write_vorbiscomment(4, bitexact, &oggstream->header_len[1], m, 0, NULL, 0);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[1] = p;
    bytestream_put_byte(&p, 0x84); // last metadata block and vorbis comment
    bytestream_put_be24(&p, oggstream->header_len[1] - 4);

    return 0;
}

static int flac_init(AVFormatContext *s, AVStream *st)
{
    int err = flac_build_headers(st->codecpar, st->priv_data,
                                 s->flags & AVFMT_FLAG_BITEXACT,
                                 &st->metadata);
    if (err)
        av_log(s, AV_LOG_ERROR, "Error writing FLAC headers\n");
    return err;
}

const OGGMuxerMapping ff_ogg_flac_muxer_mapping = {
    .codec_id  = AV_CODEC_ID_FLAC,
    .new_links = 1,
    .init      = flac_init,
};
