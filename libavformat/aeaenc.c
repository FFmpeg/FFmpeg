/*
 * MD STUDIO audio muxer
 *
 * Copyright (c) 2024 asivery
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

#include "avformat.h"
#include "avio_internal.h"
#include "rawenc.h"
#include "mux.h"

static int aea_write_header(AVFormatContext *s)
{
    const AVDictionaryEntry *title_entry;
    size_t title_length = 0;
    AVStream *st = s->streams[0];

    if (st->codecpar->ch_layout.nb_channels != 1 && st->codecpar->ch_layout.nb_channels != 2) {
        av_log(s, AV_LOG_ERROR, "Only maximum 2 channels are supported in the audio"
               " stream, %d channels were found.\n", st->codecpar->ch_layout.nb_channels);
        return AVERROR(EINVAL);
    }

    if (st->codecpar->sample_rate != 44100) {
        av_log(s, AV_LOG_ERROR, "Invalid sample rate (%d) AEA only supports 44.1kHz.\n", st->codecpar->sample_rate);
        return AVERROR(EINVAL);
    }

    /* Write magic */
    avio_wl32(s->pb, 2048);

    /* Write AEA title */
    title_entry = av_dict_get(st->metadata, "title", NULL, 0);
    if (title_entry) {
        const char *title_contents = title_entry->value;
        title_length = strlen(title_contents);
        if (title_length > 256) {
            av_log(s, AV_LOG_WARNING, "Title too long, truncated to 256 bytes.\n");
            title_length = 256;
        }
        avio_write(s->pb, title_contents, title_length);
    }

    ffio_fill(s->pb, 0, 256 - title_length);

    /* Write number of frames (zero at header-writing time, will seek later), number of channels */
    avio_wl32(s->pb, 0);
    avio_w8(s->pb, st->codecpar->ch_layout.nb_channels);
    avio_w8(s->pb, 0);

    /* Pad the header to 2048 bytes */
    ffio_fill(s->pb, 0, 1782);

    return 0;
}

static int aea_write_trailer(struct AVFormatContext *s)
{
    int64_t total_blocks;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
        /* Seek to rewrite the block count. */
        avio_seek(pb, 260, SEEK_SET);
        total_blocks = st->nb_frames * st->codecpar->ch_layout.nb_channels;
        if (total_blocks > UINT32_MAX) {
            av_log(s, AV_LOG_WARNING, "Too many frames in the file to properly encode the header (%"PRId64")."
                   " Block count in the header will be truncated.\n", total_blocks);
            total_blocks = UINT32_MAX;
        }
        avio_wl32(pb, total_blocks);
    } else {
        av_log(s, AV_LOG_WARNING, "Unable to rewrite AEA header.\n");
    }

    return 0;
}

const FFOutputFormat ff_aea_muxer = {
    .p.name           = "aea",
    .p.long_name      = NULL_IF_CONFIG_SMALL("MD STUDIO audio"),
    .p.extensions     = "aea",
    .p.audio_codec    = AV_CODEC_ID_ATRAC1,
    .p.video_codec    = AV_CODEC_ID_NONE,
    .p.subtitle_codec = AV_CODEC_ID_NONE,

    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                        FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .write_header     = aea_write_header,
    .write_packet     = ff_raw_write_packet,
    .write_trailer    = aea_write_trailer,
};
