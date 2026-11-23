/*
 * Ogg VP8 muxer mapping
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
#include "libavutil/dict.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavformat/internal.h"
#include "libavformat/ogg/muxer.h"

#define VP8_HEADER_SIZE 26

static int vp8_build_headers(AVFormatContext *s, AVStream *st,
                             OGGStreamContext *oggstream, int bitexact)
{
    AVCodecParameters *par = st->codecpar;
    uint8_t *p;

    /* first packet: VP8 header */
    p = av_mallocz(VP8_HEADER_SIZE);
    if (!p)
        return AVERROR(ENOMEM);
    oggstream->header[0] = p;
    oggstream->header_len[0] = VP8_HEADER_SIZE;
    bytestream_put_byte(&p, 0x4f); // HDRID
    bytestream_put_buffer(&p, "VP80", 4); // Identifier
    bytestream_put_byte(&p, 1); // HDRTYP
    bytestream_put_byte(&p, 1); // VMAJ
    bytestream_put_byte(&p, 0); // VMIN
    bytestream_put_be16(&p, par->width);
    bytestream_put_be16(&p, par->height);
    bytestream_put_be24(&p, par->sample_aspect_ratio.num);
    bytestream_put_be24(&p, par->sample_aspect_ratio.den);
    if (st->r_frame_rate.num > 0 && st->r_frame_rate.den > 0) {
        // OggVP8 requires pts to increase by 1 per visible frame, so use the least common
        // multiple framerate if available.
        av_log(s, AV_LOG_DEBUG, "Changing time base from %d/%d to %d/%d\n",
               st->time_base.num, st->time_base.den,
               st->r_frame_rate.den, st->r_frame_rate.num);
        avpriv_set_pts_info(st, 64, st->r_frame_rate.den, st->r_frame_rate.num);
    }
    bytestream_put_be32(&p, st->time_base.den);
    bytestream_put_be32(&p, st->time_base.num);

    /* optional second packet: VorbisComment */
    if (av_dict_count(st->metadata)) {
        p = ff_ogg_write_vorbiscomment(7, bitexact, &oggstream->header_len[1], &st->metadata, 0, NULL, 0);
        if (!p)
            return AVERROR(ENOMEM);
        oggstream->header[1] = p;
        bytestream_put_byte(&p, 0x4f); // HDRID
        bytestream_put_buffer(&p, "VP80", 4); // Identifier
        bytestream_put_byte(&p, 2); // HDRTYP
        bytestream_put_byte(&p, 0x20);
    }

    return 0;
}

static int vp8_init(AVFormatContext *s, AVStream *st)
{
    OGGStreamContext *oggstream = st->priv_data;
    int err = vp8_build_headers(s, st, oggstream,
                                s->flags & AVFMT_FLAG_BITEXACT);
    if (err) {
        av_log(s, AV_LOG_ERROR, "Error writing VP8 headers\n");
        return err;
    }

    /* The timestamp, then 2 bits of inverse count and 27 bits of distance
       to the last keyframe, above 3 reserved bits. */
    oggstream->granule_shift    = 32;
    oggstream->granule_key_mask = INT64_C(0x07ffffff) << 3;
    return 0;
}

static int64_t vp8_packet_granule(AVStream *st, const AVPacket *pkt)
{
    const OGGStreamContext *oggstream = st->priv_data;
    int64_t pts, invcnt, dist;
    int visible;

    visible = (pkt->data[0] >> 4) & 1;
    pts     = pkt->pts + pkt->duration;
    invcnt  = (oggstream->last_granule >> 30) & 3;
    invcnt  = visible ? 3 : (invcnt == 3 ? 0 : invcnt + 1);
    dist    = (pkt->flags & AV_PKT_FLAG_KEY) ? 0 : ((oggstream->last_granule >> 3) & 0x07ffffff) + 1;

    return (pts << 32) | (invcnt << 30) | (dist << 3);
}

const OGGMuxerMapping ff_ogg_vp8_muxer_mapping = {
    .codec_id           = AV_CODEC_ID_VP8,
    .optional_extradata = 1,
    .init               = vp8_init,
    .packet_granule     = vp8_packet_granule,
};
