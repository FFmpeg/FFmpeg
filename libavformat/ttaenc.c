/*
 * True Audio (TTA) muxer
 * Copyright (c) 2016 James Almer
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

#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"

#include "libavcodec/packet_internal.h"
#include "apetag.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"

typedef struct TTAMuxContext {
    AVIOContext *seek_table;
    PacketList queue;
    uint32_t nb_samples;
    int frame_size;
    int last_frame;
} TTAMuxContext;

static int tta_init(AVFormatContext *s)
{
    TTAMuxContext *tta = s->priv_data;
    AVCodecParameters *par;

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "Only one stream is supported\n");
        return AVERROR(EINVAL);
    }
    par = s->streams[0]->codecpar;

    if (par->codec_id != AV_CODEC_ID_TTA) {
        av_log(s, AV_LOG_ERROR, "Unsupported codec\n");
        return AVERROR(EINVAL);
    }
    if (par->extradata && par->extradata_size < 22) {
        av_log(s, AV_LOG_ERROR, "Invalid TTA extradata\n");
        return AVERROR_INVALIDDATA;
    }

    /* Prevent overflow */
    if (par->sample_rate > 0x7FFFFFu) {
        av_log(s, AV_LOG_ERROR, "Sample rate too large\n");
        return AVERROR(EINVAL);
    }
    tta->frame_size = par->sample_rate * 256 / 245;
    avpriv_set_pts_info(s->streams[0], 64, 1, par->sample_rate);

    return 0;
}

static int tta_write_header(AVFormatContext *s)
{
    TTAMuxContext *tta = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;
    int ret;

    if ((ret = avio_open_dyn_buf(&tta->seek_table)) < 0)
        return ret;

    /* Ignore most extradata information if present. It can be innacurate
       if for example remuxing from Matroska */
    ffio_init_checksum(s->pb, ff_crcEDB88320_update, UINT32_MAX);
    ffio_init_checksum(tta->seek_table, ff_crcEDB88320_update, UINT32_MAX);
    avio_write(s->pb, "TTA1", 4);
    avio_wl16(s->pb, par->extradata ? AV_RL16(par->extradata + 4) : 1);
    avio_wl16(s->pb, par->ch_layout.nb_channels);
    avio_wl16(s->pb, par->bits_per_raw_sample);
    avio_wl32(s->pb, par->sample_rate);

    return 0;
}

static int tta_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    TTAMuxContext *tta = s->priv_data;
    int ret;

    ret = avpriv_packet_list_put(&tta->queue, pkt, NULL, 0);
    if (ret < 0) {
        return ret;
    }
    pkt = &tta->queue.tail->pkt;

    avio_wl32(tta->seek_table, pkt->size);
    tta->nb_samples += pkt->duration;

    if (tta->frame_size != pkt->duration) {
        if (tta->last_frame) {
            /* Two frames with a different duration than the default frame
               size means the TTA stream comes from a faulty container, and
               there's no way the last frame duration will be correct. */
            av_log(s, AV_LOG_ERROR, "Invalid frame durations\n");

            return AVERROR_INVALIDDATA;
        }
        /* First frame with a different duration than the default frame size.
           Assume it's the last frame in the stream and continue. */
        tta->last_frame++;
    }

    return 0;
}

static void tta_queue_flush(AVFormatContext *s)
{
    TTAMuxContext *tta = s->priv_data;
    AVPacket *const pkt = ffformatcontext(s)->pkt;

    while (tta->queue.head) {
        avpriv_packet_list_get(&tta->queue, pkt);
        avio_write(s->pb, pkt->data, pkt->size);
        av_packet_unref(pkt);
    }
}

static int tta_write_trailer(AVFormatContext *s)
{
    TTAMuxContext *tta = s->priv_data;
    uint8_t *ptr;
    unsigned int crc;
    int size;

    avio_wl32(s->pb, tta->nb_samples);
    crc = ffio_get_checksum(s->pb) ^ UINT32_MAX;
    avio_wl32(s->pb, crc);

    /* Write Seek table */
    crc = ffio_get_checksum(tta->seek_table) ^ UINT32_MAX;
    avio_wl32(tta->seek_table, crc);
    size = avio_get_dyn_buf(tta->seek_table, &ptr);
    avio_write(s->pb, ptr, size);

    /* Write audio data */
    tta_queue_flush(s);

    ff_ape_write_tag(s);

    return 0;
}

static void tta_deinit(AVFormatContext *s)
{
    TTAMuxContext *tta = s->priv_data;

    ffio_free_dyn_buf(&tta->seek_table);
    avpriv_packet_list_free(&tta->queue);
}

const AVOutputFormat ff_tta_muxer = {
    .name              = "tta",
    .long_name         = NULL_IF_CONFIG_SMALL("TTA (True Audio)"),
    .mime_type         = "audio/x-tta",
    .extensions        = "tta",
    .priv_data_size    = sizeof(TTAMuxContext),
    .audio_codec       = AV_CODEC_ID_TTA,
    .video_codec       = AV_CODEC_ID_NONE,
    .init              = tta_init,
    .deinit            = tta_deinit,
    .write_header      = tta_write_header,
    .write_packet      = tta_write_packet,
    .write_trailer     = tta_write_trailer,
};
