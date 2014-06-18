/*
 * Creative Voice File demuxer.
 * Copyright (c) 2006  Aurelien Jacobs <aurel@gnuage.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"
#include "voc.h"
#include "internal.h"

static int voc_probe(AVProbeData *p)
{
    int version, check;

    if (memcmp(p->buf, ff_voc_magic, sizeof(ff_voc_magic) - 1))
        return 0;
    version = AV_RL16(p->buf + 22);
    check = AV_RL16(p->buf + 24);
    if (~version + 0x1234 != check)
        return 10;

    return AVPROBE_SCORE_MAX;
}

static int voc_read_header(AVFormatContext *s)
{
    VocDecContext *voc = s->priv_data;
    AVIOContext *pb = s->pb;
    int header_size;

    avio_skip(pb, 20);
    header_size = avio_rl16(pb) - 22;
    if (header_size != 4) {
        av_log(s, AV_LOG_ERROR, "unknown header size: %d\n", header_size);
        return AVERROR(ENOSYS);
    }
    avio_skip(pb, header_size);

    s->ctx_flags |= AVFMTCTX_NOHEADER;

    voc->remaining_size = 0;
    return 0;
}

static int voc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (!s->nb_streams) {
        AVStream *st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    }
    return ff_voc_get_packet(s, pkt, s->streams[0], 0);
}

AVInputFormat ff_voc_demuxer = {
    .name           = "voc",
    .long_name      = NULL_IF_CONFIG_SMALL("Creative Voice"),
    .priv_data_size = sizeof(VocDecContext),
    .read_probe     = voc_probe,
    .read_header    = voc_read_header,
    .read_packet    = voc_read_packet,
    .codec_tag      = (const AVCodecTag* const []){ ff_voc_codec_tags, 0 },
};
