/*
 * NSP demuxer
 * Copyright (c) 2017 Paul B Mahol
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

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "pcm.h"

static int nsp_probe(const AVProbeData *p)
{
    if (AV_RB32(p->buf) == AV_RB32("FORM") &&
        AV_RB32(p->buf + 4) == AV_RB32("DS16"))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int nsp_read_header(AVFormatContext *s)
{
    int channels = 0, rate = 0;
    uint32_t chunk, size;
    AVStream *st;
    int64_t pos;

    avio_skip(s->pb, 12);
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    while (!avio_feof(s->pb)) {
        char value[1024];

        chunk = avio_rb32(s->pb);
        size  = avio_rl32(s->pb);
        pos   = avio_tell(s->pb);

        switch (chunk) {
        case MKBETAG('H', 'E', 'D', 'R'):
        case MKBETAG('H', 'D', 'R', '8'):
            if (size < 32)
                return AVERROR_INVALIDDATA;
            avio_skip(s->pb, 20);
            rate = avio_rl32(s->pb);
            avio_skip(s->pb, size - (avio_tell(s->pb) - pos));
            break;
        case MKBETAG('N', 'O', 'T', 'E'):
            avio_get_str(s->pb, size, value, sizeof(value));
            av_dict_set(&s->metadata, "Comment", value, 0);
            avio_skip(s->pb, size & 1);
            break;
        case MKBETAG('S', 'D', 'A', 'B'):
            channels = 2;
            break;
        case MKBETAG('S', 'D', '_', '2'):
        case MKBETAG('S', 'D', '_', '3'):
        case MKBETAG('S', 'D', '_', '4'):
        case MKBETAG('S', 'D', '_', '5'):
        case MKBETAG('S', 'D', '_', '6'):
        case MKBETAG('S', 'D', '_', '7'):
        case MKBETAG('S', 'D', '_', '8'):
            av_log(s, AV_LOG_WARNING, "Unsupported chunk!\n");
        case MKBETAG('S', 'D', 'A', '_'):
        case MKBETAG('S', 'D', '_', 'A'):
            channels = 1;
            break;
        }

        if (channels)
            break;
    }

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->channels    = channels;
    st->codecpar->sample_rate = rate;
    st->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
    st->codecpar->block_align = 2 * channels;

    return 0;
}

AVInputFormat ff_nsp_demuxer = {
    .name           = "nsp",
    .long_name      = NULL_IF_CONFIG_SMALL("Computerized Speech Lab NSP"),
    .read_probe     = nsp_probe,
    .read_header    = nsp_read_header,
    .read_packet    = ff_pcm_read_packet,
    .read_seek      = ff_pcm_read_seek,
    .extensions     = "nsp",
    .flags          = AVFMT_GENERIC_INDEX,
};
