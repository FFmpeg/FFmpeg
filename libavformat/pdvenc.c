/*
 * PDV muxer
 *
 * Copyright (c) 2026 Priyanshu Thapliyal <priyanshuthapliyal2005@gmail.com>
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

#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/rational.h"
#include "avformat.h"
#include "avio_internal.h"
#include "mux.h"

#define PDV_MAGIC "Playdate VID\x00\x00\x00\x00"
#define PDV_MAX_FRAMES UINT16_MAX
#define PDV_MAX_OFFSET ((1U << 30) - 1)

typedef struct PDVMuxContext {
    uint32_t *entries;
    int nb_frames;
    int max_frames;
    uint32_t fps_bits;
    int64_t nb_frames_pos;
    int64_t table_pos;
    int64_t payload_start;
} PDVMuxContext;

static void pdv_deinit(AVFormatContext *s)
{
    PDVMuxContext *pdv = s->priv_data;

    av_freep(&pdv->entries);
}

static int pdv_get_fps(AVFormatContext *s, AVStream *st, uint32_t *fps_bits)
{
    AVRational rate = st->avg_frame_rate;
    const AVRational zero = { 0, 1 };

    if (!rate.num || !rate.den)
        rate = av_inv_q(st->time_base);
    if (!rate.num || !rate.den) {
        av_log(s, AV_LOG_ERROR, "A valid frame rate is required for PDV output.\n");
        return AVERROR(EINVAL);
    }

    if (av_cmp_q(rate, zero) <= 0) {
        av_log(s, AV_LOG_ERROR, "Invalid frame rate for PDV output.\n");
        return AVERROR(EINVAL);
    }

    *fps_bits = av_q2intfloat(rate);
    return 0;
}

static int pdv_write_header(AVFormatContext *s)
{
    PDVMuxContext *pdv = s->priv_data;
    AVStream *st;
    int ret;

    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(s, AV_LOG_ERROR, "PDV muxer requires seekable output.\n");
        return AVERROR(EINVAL);
    }

    st = s->streams[0];

    if (st->codecpar->width > UINT16_MAX || st->codecpar->height > UINT16_MAX) {
        av_log(s, AV_LOG_ERROR, "Output dimensions exceed PDV limits.\n");
        return AVERROR(EINVAL);
    }

    ret = pdv_get_fps(s, st, &pdv->fps_bits);
    if (ret < 0)
        return ret;

    if (pdv->max_frames < 1 || pdv->max_frames > PDV_MAX_FRAMES) {
        av_log(s, AV_LOG_ERROR,
               "The -max_frames option must be set to a value in [1, %u].\n",
               PDV_MAX_FRAMES);
        return AVERROR(EINVAL);
    }

    pdv->entries = av_malloc_array(pdv->max_frames + 1, sizeof(*pdv->entries));
    if (!pdv->entries)
        return AVERROR(ENOMEM);

    avio_write(s->pb, PDV_MAGIC, 16);
    pdv->nb_frames_pos = avio_tell(s->pb);
    avio_wl16(s->pb, 0);
    avio_wl16(s->pb, 0);
    avio_wl32(s->pb, pdv->fps_bits);
    avio_wl16(s->pb, st->codecpar->width);
    avio_wl16(s->pb, st->codecpar->height);

    pdv->table_pos = avio_tell(s->pb);
    ffio_fill(s->pb, 0, 4LL * (pdv->max_frames + 1));
    pdv->payload_start = avio_tell(s->pb);

    if (pdv->nb_frames_pos < 0 || pdv->table_pos < 0 || pdv->payload_start < 0)
        return AVERROR(EIO);

    return 0;
}

static int pdv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    PDVMuxContext *pdv = s->priv_data;
    int64_t offset = avio_tell(s->pb);
    const uint32_t max_table_gap = 4U * pdv->max_frames;

    if (offset < 0)
        return AVERROR(EIO);
    offset -= pdv->payload_start;
    if (offset < 0)
        return AVERROR(EIO);

    if (pkt->size <= 0)
        return AVERROR_INVALIDDATA;
    if (pdv->nb_frames >= pdv->max_frames) {
        av_log(s, AV_LOG_ERROR, "Too many frames for PDV output.\n");
        return AVERROR(EINVAL);
    }
    if (offset > PDV_MAX_OFFSET - max_table_gap ||
        pkt->size > PDV_MAX_OFFSET - max_table_gap - offset) {
        av_log(s, AV_LOG_ERROR, "PDV payload exceeds container limits.\n");
        return AVERROR(EINVAL);
    }

    pdv->entries[pdv->nb_frames] = ((uint32_t)offset << 2) |
                                   (pkt->flags & AV_PKT_FLAG_KEY ? 1 : 2);
    avio_write(s->pb, pkt->data, pkt->size);

    pdv->nb_frames++;

    return 0;
}

static int pdv_write_trailer(AVFormatContext *s)
{
    PDVMuxContext *pdv = s->priv_data;
    int64_t payload_size = avio_tell(s->pb);
    const uint32_t table_gap = 4U * (pdv->max_frames - pdv->nb_frames);
    int64_t ret;

    if (payload_size < 0)
        return AVERROR(EIO);
    payload_size -= pdv->payload_start;
    if (payload_size < 0 || payload_size > PDV_MAX_OFFSET - table_gap)
        return AVERROR(EINVAL);

    pdv->entries[pdv->nb_frames] = (uint32_t)payload_size << 2;

    if ((ret = avio_seek(s->pb, pdv->nb_frames_pos, SEEK_SET)) < 0)
        return ret;
    avio_wl16(s->pb, pdv->nb_frames);

    if ((ret = avio_seek(s->pb, pdv->table_pos, SEEK_SET)) < 0)
        return ret;
    for (int i = 0; i <= pdv->nb_frames; i++) {
        const uint32_t frame_off = (pdv->entries[i] >> 2) + table_gap;

        if (frame_off > PDV_MAX_OFFSET)
            return AVERROR(EINVAL);
        avio_wl32(s->pb, frame_off << 2 | (pdv->entries[i] & 3));
    }

    if ((ret = avio_seek(s->pb, pdv->payload_start + payload_size, SEEK_SET)) < 0)
        return ret;

    return 0;
}

#define OFFSET(x) offsetof(PDVMuxContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "max_frames", "maximum number of frames reserved in table (mandatory)",
      OFFSET(max_frames), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, PDV_MAX_FRAMES, ENC },
    { NULL },
};

static const AVClass pdv_muxer_class = {
    .class_name = "PDV muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_MUXER,
};

const FFOutputFormat ff_pdv_muxer = {
    .p.name           = "pdv",
    .p.long_name      = NULL_IF_CONFIG_SMALL("PlayDate Video"),
    .p.extensions     = "pdv",
    .p.priv_class     = &pdv_muxer_class,
    .p.audio_codec    = AV_CODEC_ID_NONE,
    .p.video_codec    = AV_CODEC_ID_PDV,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .priv_data_size   = sizeof(PDVMuxContext),
    .p.flags          = AVFMT_NOTIMESTAMPS,
    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                        FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .write_header     = pdv_write_header,
    .write_packet     = pdv_write_packet,
    .write_trailer    = pdv_write_trailer,
    .deinit           = pdv_deinit,
};
