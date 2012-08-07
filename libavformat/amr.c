/*
 * amr file format
 * Copyright (c) 2001 ffmpeg project
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

Only mono files are supported.

*/
#include "libavutil/avassert.h"
#include "avformat.h"
#include "internal.h"

static const char AMR_header[]   = "#!AMR\n";
static const char AMRWB_header[] = "#!AMR-WB\n";

#if CONFIG_AMR_MUXER
static int amr_write_header(AVFormatContext *s)
{
    AVIOContext    *pb  = s->pb;
    AVCodecContext *enc = s->streams[0]->codec;

    s->priv_data = NULL;

    if (enc->codec_id == AV_CODEC_ID_AMR_NB) {
        avio_write(pb, AMR_header,   sizeof(AMR_header)   - 1); /* magic number */
    } else if (enc->codec_id == AV_CODEC_ID_AMR_WB) {
        avio_write(pb, AMRWB_header, sizeof(AMRWB_header) - 1); /* magic number */
    } else {
        return -1;
    }
    avio_flush(pb);
    return 0;
}

static int amr_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    avio_write(s->pb, pkt->data, pkt->size);
    avio_flush(s->pb);
    return 0;
}
#endif /* CONFIG_AMR_MUXER */

static int amr_probe(AVProbeData *p)
{
    // Only check for "#!AMR" which could be amr-wb, amr-nb.
    // This will also trigger multichannel files: "#!AMR_MC1.0\n" and
    // "#!AMR-WB_MC1.0\n" (not supported)

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
    uint8_t header[9];

    avio_read(pb, header, 6);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    if (memcmp(header, AMR_header, 6)) {
        avio_read(pb, header + 6, 3);
        if (memcmp(header, AMRWB_header, 9)) {
            return -1;
        }

        st->codec->codec_tag   = MKTAG('s', 'a', 'w', 'b');
        st->codec->codec_id    = AV_CODEC_ID_AMR_WB;
        st->codec->sample_rate = 16000;
    } else {
        st->codec->codec_tag   = MKTAG('s', 'a', 'm', 'r');
        st->codec->codec_id    = AV_CODEC_ID_AMR_NB;
        st->codec->sample_rate = 8000;
    }
    st->codec->channels   = 1;
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

    return 0;
}

static int amr_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *enc = s->streams[0]->codec;
    int read, size = 0, toc, mode;
    int64_t pos = avio_tell(s->pb);

    if (url_feof(s->pb)) {
        return AVERROR(EIO);
    }

    // FIXME this is wrong, this should rather be in a AVParset
    toc  = avio_r8(s->pb);
    mode = (toc >> 3) & 0x0F;

    if (enc->codec_id == AV_CODEC_ID_AMR_NB) {
        static const uint8_t packed_size[16] = {
            12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0
        };

        size = packed_size[mode] + 1;
    } else if (enc->codec_id == AV_CODEC_ID_AMR_WB) {
        static const uint8_t packed_size[16] = {
            18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 6, 0, 0, 0, 1, 1
        };

        size = packed_size[mode];
    } else {
        av_assert0(0);
    }

    if (!size || av_new_packet(pkt, size))
        return AVERROR(EIO);

    /* Both AMR formats have 50 frames per second */
    s->streams[0]->codec->bit_rate = size*8*50;

    pkt->stream_index = 0;
    pkt->pos          = pos;
    pkt->data[0]      = toc;
    pkt->duration     = enc->codec_id == AV_CODEC_ID_AMR_NB ? 160 : 320;
    read              = avio_read(s->pb, pkt->data + 1, size - 1);

    if (read != size - 1) {
        av_free_packet(pkt);
        return AVERROR(EIO);
    }

    return 0;
}

#if CONFIG_AMR_DEMUXER
AVInputFormat ff_amr_demuxer = {
    .name           = "amr",
    .long_name      = NULL_IF_CONFIG_SMALL("3GPP AMR"),
    .read_probe     = amr_probe,
    .read_header    = amr_read_header,
    .read_packet    = amr_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
};
#endif

#if CONFIG_AMR_MUXER
AVOutputFormat ff_amr_muxer = {
    .name              = "amr",
    .long_name         = NULL_IF_CONFIG_SMALL("3GPP AMR"),
    .mime_type         = "audio/amr",
    .extensions        = "amr",
    .audio_codec       = AV_CODEC_ID_AMR_NB,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = amr_write_header,
    .write_packet      = amr_write_packet,
};
#endif
