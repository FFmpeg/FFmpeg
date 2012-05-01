/*
 * SoX native format demuxer
 * Copyright (c) 2009 Daniel Verkamp <daniel@drv.nu>
 *
 * Based on libSoX sox-fmt.c
 * Copyright (c) 2008 robs@users.sourceforge.net
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

/**
 * @file
 * SoX native format demuxer
 * @author Daniel Verkamp
 * @see http://wiki.multimedia.cx/index.php?title=SoX_native_intermediate_format
 */

#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "libavutil/dict.h"
#include "avformat.h"
#include "internal.h"
#include "pcm.h"
#include "sox.h"

static int sox_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) == SOX_TAG || AV_RB32(p->buf) == SOX_TAG)
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int sox_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    unsigned header_size, comment_size;
    double sample_rate, sample_rate_frac;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;

    if (avio_rl32(pb) == SOX_TAG) {
        st->codec->codec_id = CODEC_ID_PCM_S32LE;
        header_size         = avio_rl32(pb);
        avio_skip(pb, 8); /* sample count */
        sample_rate         = av_int2double(avio_rl64(pb));
        st->codec->channels = avio_rl32(pb);
        comment_size        = avio_rl32(pb);
    } else {
        st->codec->codec_id = CODEC_ID_PCM_S32BE;
        header_size         = avio_rb32(pb);
        avio_skip(pb, 8); /* sample count */
        sample_rate         = av_int2double(avio_rb64(pb));
        st->codec->channels = avio_rb32(pb);
        comment_size        = avio_rb32(pb);
    }

    if (comment_size > 0xFFFFFFFFU - SOX_FIXED_HDR - 4U) {
        av_log(s, AV_LOG_ERROR, "invalid comment size (%u)\n", comment_size);
        return -1;
    }

    if (sample_rate <= 0 || sample_rate > INT_MAX) {
        av_log(s, AV_LOG_ERROR, "invalid sample rate (%f)\n", sample_rate);
        return -1;
    }

    sample_rate_frac = sample_rate - floor(sample_rate);
    if (sample_rate_frac)
        av_log(s, AV_LOG_WARNING,
               "truncating fractional part of sample rate (%f)\n",
               sample_rate_frac);

    if ((header_size + 4) & 7 || header_size < SOX_FIXED_HDR + comment_size
        || st->codec->channels > 65535) /* Reserve top 16 bits */ {
        av_log(s, AV_LOG_ERROR, "invalid header\n");
        return -1;
    }

    if (comment_size && comment_size < UINT_MAX) {
        char *comment = av_malloc(comment_size+1);
        if(!comment)
            return AVERROR(ENOMEM);
        if (avio_read(pb, comment, comment_size) != comment_size) {
            av_freep(&comment);
            return AVERROR(EIO);
        }
        comment[comment_size] = 0;

        av_dict_set(&s->metadata, "comment", comment,
                               AV_DICT_DONT_STRDUP_VAL);
    }

    avio_skip(pb, header_size - SOX_FIXED_HDR - comment_size);

    st->codec->sample_rate           = sample_rate;
    st->codec->bits_per_coded_sample = 32;
    st->codec->bit_rate              = st->codec->sample_rate *
                                       st->codec->bits_per_coded_sample *
                                       st->codec->channels;
    st->codec->block_align           = st->codec->bits_per_coded_sample *
                                       st->codec->channels / 8;

    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

    return 0;
}

#define SOX_SAMPLES 1024

static int sox_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    int ret, size;

    if (url_feof(s->pb))
        return AVERROR_EOF;

    size = SOX_SAMPLES*s->streams[0]->codec->block_align;
    ret = av_get_packet(s->pb, pkt, size);
    if (ret < 0)
        return AVERROR(EIO);
    pkt->flags &= ~AV_PKT_FLAG_CORRUPT;
    pkt->stream_index = 0;

    return 0;
}

AVInputFormat ff_sox_demuxer = {
    .name           = "sox",
    .long_name      = NULL_IF_CONFIG_SMALL("SoX native format"),
    .read_probe     = sox_probe,
    .read_header    = sox_read_header,
    .read_packet    = sox_read_packet,
    .read_seek      = ff_pcm_read_seek,
};
