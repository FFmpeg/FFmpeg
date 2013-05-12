/*
 * Opus parser for Ogg
 * Copyright (c) 2012 Nicolas George
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

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

struct oggopus_private {
    int need_comments;
    unsigned pre_skip;
    int64_t cur_dts;
};

#define OPUS_HEAD_SIZE 19

static int opus_header(AVFormatContext *avf, int idx)
{
    struct ogg *ogg = avf->priv_data;
    struct ogg_stream *os = &ogg->streams[idx];
    AVStream *st = avf->streams[idx];
    struct oggopus_private *priv = os->private;
    uint8_t *packet = os->buf + os->pstart;
    uint8_t *extradata;

    if (!priv) {
        priv = os->private = av_mallocz(sizeof(*priv));
        if (!priv)
            return AVERROR(ENOMEM);
    }
    if (os->flags & OGG_FLAG_BOS) {
        if (os->psize < OPUS_HEAD_SIZE || (AV_RL8(packet + 8) & 0xF0) != 0)
            return AVERROR_INVALIDDATA;
        st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id    = AV_CODEC_ID_OPUS;
        st->codec->channels    = AV_RL8 (packet + 9);
        priv->pre_skip         = AV_RL16(packet + 10);
        /*orig_sample_rate     = AV_RL32(packet + 12);*/
        /*gain                 = AV_RL16(packet + 16);*/
        /*channel_map          = AV_RL8 (packet + 18);*/

        extradata = av_malloc(os->psize + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!extradata)
            return AVERROR(ENOMEM);
        memcpy(extradata, packet, os->psize);
        st->codec->extradata = extradata;
        st->codec->extradata_size = os->psize;

        st->codec->sample_rate = 48000;
        avpriv_set_pts_info(st, 64, 1, 48000);
        priv->need_comments = 1;
        return 1;
    }

    if (priv->need_comments) {
        if (os->psize < 8 || memcmp(packet, "OpusTags", 8))
            return AVERROR_INVALIDDATA;
        ff_vorbis_comment(avf, &st->metadata, packet + 8, os->psize - 8);
        priv->need_comments--;
        return 1;
    }
    return 0;
}

static int opus_packet(AVFormatContext *avf, int idx)
{
    struct ogg *ogg = avf->priv_data;
    struct ogg_stream *os = &ogg->streams[idx];
    AVStream *st = avf->streams[idx];
    struct oggopus_private *priv = os->private;
    uint8_t *packet = os->buf + os->pstart;
    unsigned toc, toc_config, toc_count, frame_size, nb_frames = 1;

    if (!os->psize)
        return AVERROR_INVALIDDATA;
    toc = *packet;
    toc_config = toc >> 3;
    toc_count  = toc & 3;
    frame_size = toc_config < 12 ? FFMAX(480, 960 * (toc_config & 3)) :
                 toc_config < 16 ? 480 << (toc_config & 1) :
                                   120 << (toc_config & 3);
    if (toc_count == 3) {
        if (os->psize < 2)
            return AVERROR_INVALIDDATA;
        nb_frames = packet[1] & 0x3F;
    } else if (toc_count) {
        nb_frames = 2;
    }
    os->pduration = frame_size * nb_frames;
    if (os->lastpts != AV_NOPTS_VALUE) {
        if (st->start_time == AV_NOPTS_VALUE)
            st->start_time = os->lastpts;
        priv->cur_dts = os->lastdts = os->lastpts -= priv->pre_skip;
    }
    priv->cur_dts += os->pduration;
    if ((os->flags & OGG_FLAG_EOS)) {
        int64_t skip = priv->cur_dts - os->granule + priv->pre_skip;
        skip = FFMIN(skip, os->pduration);
        if (skip > 0) {
            os->pduration = skip < os->pduration ? os->pduration - skip : 1;
            av_log(avf, AV_LOG_WARNING,
                   "Last packet must be truncated to %d (unimplemented).\n",
                   os->pduration);
        }
    }
    return 0;
}

const struct ogg_codec ff_opus_codec = {
    .name      = "Opus",
    .magic     = "OpusHead",
    .magicsize = 8,
    .header    = opus_header,
    .packet    = opus_packet,
};
