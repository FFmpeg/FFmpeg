/*
 * Common functions for the frame{crc,md5} muxers
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

#include "libavutil/channel_layout.h"
#include "internal.h"

int ff_framehash_write_header(AVFormatContext *s)
{
    int i;

    if (s->nb_streams && !(s->flags & AVFMT_FLAG_BITEXACT))
        avio_printf(s->pb, "#software: %s\n", LIBAVFORMAT_IDENT);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AVCodecParameters *avctx = st->codecpar;
        char buf[256] = { 0 };
        avio_printf(s->pb, "#tb %d: %d/%d\n", i, st->time_base.num, st->time_base.den);
        avio_printf(s->pb, "#media_type %d: %s\n", i, av_get_media_type_string(avctx->codec_type));
        avio_printf(s->pb, "#codec_id %d: %s\n", i, avcodec_get_name(avctx->codec_id));
        switch (avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            av_get_channel_layout_string(buf, sizeof(buf), avctx->channels, avctx->channel_layout);
            avio_printf(s->pb, "#sample_rate %d: %d\n", i,avctx->sample_rate);
            avio_printf(s->pb, "#channel_layout %d: %"PRIx64"\n", i,avctx->channel_layout);
            avio_printf(s->pb, "#channel_layout_name %d: %s\n", i, buf);
            break;
        case AVMEDIA_TYPE_VIDEO:
            avio_printf(s->pb, "#dimensions %d: %dx%d\n", i, avctx->width, avctx->height);
            avio_printf(s->pb, "#sar %d: %d/%d\n", i, st->sample_aspect_ratio.num, st->sample_aspect_ratio.den);
            break;
        }
    }
    return 0;
}
