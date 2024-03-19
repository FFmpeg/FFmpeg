/*
 * amr file format
 * Copyright (c) 2001 FFmpeg project
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

/*
Write and read amr data according to RFC3267, http://www.ietf.org/rfc/rfc3267.txt?number=3267
*/

#include "config_components.h"

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"
#include "mux.h"
#include "rawdec.h"
#include "rawenc.h"

typedef struct AMRContext {
    FFRawDemuxerContext rawctx;
} AMRContext;

static const uint8_t AMR_header[6]      = "#!AMR\x0a";
static const uint8_t AMRMC_header[12]   = "#!AMR_MC1.0\x0a";
static const uint8_t AMRWB_header[9]    = "#!AMR-WB\x0a";
static const uint8_t AMRWBMC_header[15] = "#!AMR-WB_MC1.0\x0a";

static const uint8_t amrnb_packed_size[16] = {
    13, 14, 16, 18, 20, 21, 27, 32, 6, 1, 1, 1, 1, 1, 1, 1
};
static const uint8_t amrwb_packed_size[16] = {
    18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 1, 1, 1, 1, 1, 1
};

#if CONFIG_AMR_DEMUXER
static int amr_probe(const AVProbeData *p)
{
    // Only check for "#!AMR" which could be amr-wb, amr-nb.
    // This will also trigger multichannel files: "#!AMR_MC1.0\n" and
    // "#!AMR-WB_MC1.0\n"

    if (!memcmp(p->buf, AMR_header, 5))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

/* amr input */
static int amr_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    AVStream *st;
    uint8_t header[19] = { 0 };
    int read, back = 0, ret;

    ret = ffio_ensure_seekback(s->pb, sizeof(header));
    if (ret < 0)
        return ret;

    read = avio_read(pb, header, sizeof(header));
    if (read < 0)
        return read;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    if (!memcmp(header, AMR_header, sizeof(AMR_header))) {
        st->codecpar->codec_tag   = MKTAG('s', 'a', 'm', 'r');
        st->codecpar->codec_id    = AV_CODEC_ID_AMR_NB;
        st->codecpar->sample_rate = 8000;
        st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        back = read - sizeof(AMR_header);
    } else if (!memcmp(header, AMRWB_header, sizeof(AMRWB_header))) {
        st->codecpar->codec_tag   = MKTAG('s', 'a', 'w', 'b');
        st->codecpar->codec_id    = AV_CODEC_ID_AMR_WB;
        st->codecpar->sample_rate = 16000;
        st->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        back = read - sizeof(AMRWB_header);
    } else if (!memcmp(header, AMRMC_header, sizeof(AMRMC_header))) {
        st->codecpar->codec_tag   = MKTAG('s', 'a', 'm', 'r');
        st->codecpar->codec_id    = AV_CODEC_ID_AMR_NB;
        st->codecpar->sample_rate = 8000;
        st->codecpar->ch_layout.nb_channels = AV_RL32(header + 12);
        back = read - 4 - sizeof(AMRMC_header);
    } else if (!memcmp(header, AMRWBMC_header, sizeof(AMRWBMC_header))) {
        st->codecpar->codec_tag   = MKTAG('s', 'a', 'w', 'b');
        st->codecpar->codec_id    = AV_CODEC_ID_AMR_WB;
        st->codecpar->sample_rate = 16000;
        st->codecpar->ch_layout.nb_channels = AV_RL32(header + 15);
        back = read - 4 - sizeof(AMRWBMC_header);
    } else {
        return AVERROR_INVALIDDATA;
    }

    if (st->codecpar->ch_layout.nb_channels < 1)
        return AVERROR_INVALIDDATA;

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    if (back > 0)
        avio_seek(pb, -back, SEEK_CUR);

    return 0;
}

const FFInputFormat ff_amr_demuxer = {
    .p.name         = "amr",
    .p.long_name    = NULL_IF_CONFIG_SMALL("3GPP AMR"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.priv_class   = &ff_raw_demuxer_class,
    .priv_data_size = sizeof(AMRContext),
    .read_probe     = amr_probe,
    .read_header    = amr_read_header,
    .read_packet    = ff_raw_read_partial_packet,
};
#endif

#if CONFIG_AMRNB_DEMUXER
static int amrnb_probe(const AVProbeData *p)
{
    int mode, i = 0, valid = 0, invalid = 0;
    const uint8_t *b = p->buf;

    while (i < p->buf_size) {
        mode = b[i] >> 3 & 0x0F;
        if (mode < 9 && (b[i] & 0x4) == 0x4) {
            int last = b[i];
            int size = amrnb_packed_size[mode];
            while (size--) {
                if (b[++i] != last)
                    break;
            }
            if (size > 0) {
                valid++;
                i += size;
            }
        } else {
            valid = 0;
            invalid++;
            i++;
        }
    }
    if (valid > 100 && valid >> 4 > invalid)
        return AVPROBE_SCORE_EXTENSION / 2 + 1;
    return 0;
}

static int amrnb_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_id       = AV_CODEC_ID_AMR_NB;
    st->codecpar->sample_rate    = 8000;
    st->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    ffstream(st)->need_parsing   = AVSTREAM_PARSE_FULL_RAW;
    avpriv_set_pts_info(st, 64, 1, 8000);

    return 0;
}

const FFInputFormat ff_amrnb_demuxer = {
    .p.name         = "amrnb",
    .p.long_name    = NULL_IF_CONFIG_SMALL("raw AMR-NB"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.priv_class   = &ff_raw_demuxer_class,
    .priv_data_size = sizeof(AMRContext),
    .read_probe     = amrnb_probe,
    .read_header    = amrnb_read_header,
    .read_packet    = ff_raw_read_partial_packet,
};
#endif

#if CONFIG_AMRWB_DEMUXER
static int amrwb_probe(const AVProbeData *p)
{
    int mode, i = 0, valid = 0, invalid = 0;
    const uint8_t *b = p->buf;

    while (i < p->buf_size) {
        mode = b[i] >> 3 & 0x0F;
        if (mode < 10 && (b[i] & 0x4) == 0x4) {
            int last = b[i];
            int size = amrwb_packed_size[mode];
            while (size--) {
                if (b[++i] != last)
                    break;
            }
            if (size > 0) {
                valid++;
                i += size;
            }
        } else {
            valid = 0;
            invalid++;
            i++;
        }
    }
    if (valid > 100 && valid >> 4 > invalid)
        return AVPROBE_SCORE_EXTENSION / 2 + 1;
    return 0;
}

static int amrwb_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_id       = AV_CODEC_ID_AMR_WB;
    st->codecpar->sample_rate    = 16000;
    st->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    ffstream(st)->need_parsing   = AVSTREAM_PARSE_FULL_RAW;
    avpriv_set_pts_info(st, 64, 1, 16000);

    return 0;
}

const FFInputFormat ff_amrwb_demuxer = {
    .p.name         = "amrwb",
    .p.long_name    = NULL_IF_CONFIG_SMALL("raw AMR-WB"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.priv_class   = &ff_raw_demuxer_class,
    .priv_data_size = sizeof(AMRContext),
    .read_probe     = amrwb_probe,
    .read_header    = amrwb_read_header,
    .read_packet    = ff_raw_read_partial_packet,
};
#endif

#if CONFIG_AMR_MUXER
static int amr_write_header(AVFormatContext *s)
{
    AVIOContext    *pb  = s->pb;
    AVCodecParameters *par = s->streams[0]->codecpar;

    if (par->codec_id == AV_CODEC_ID_AMR_NB) {
        avio_write(pb, AMR_header,   sizeof(AMR_header));   /* magic number */
    } else if (par->codec_id == AV_CODEC_ID_AMR_WB) {
        avio_write(pb, AMRWB_header, sizeof(AMRWB_header)); /* magic number */
    } else {
        return -1;
    }
    return 0;
}

const FFOutputFormat ff_amr_muxer = {
    .p.name            = "amr",
    .p.long_name       = NULL_IF_CONFIG_SMALL("3GPP AMR"),
    .p.mime_type       = "audio/amr",
    .p.extensions      = "amr",
    .p.audio_codec     = AV_CODEC_ID_AMR_NB,
    .p.video_codec     = AV_CODEC_ID_NONE,
    .p.subtitle_codec  = AV_CODEC_ID_NONE,
    .p.flags           = AVFMT_NOTIMESTAMPS,
    .flags_internal    = FF_OFMT_FLAG_MAX_ONE_OF_EACH,
    .write_header      = amr_write_header,
    .write_packet      = ff_raw_write_packet,
};
#endif
