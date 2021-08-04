/*
 * ADTS muxer.
 * Copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@smartjog.com>
 *                    Mans Rullgard <mans@mansr.com>
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

#include "libavcodec/get_bits.h"
#include "libavcodec/put_bits.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/codec_par.h"
#include "libavcodec/packet.h"
#include "libavcodec/mpeg4audio.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "apetag.h"
#include "id3v2.h"

#define ADTS_HEADER_SIZE 7

typedef struct ADTSContext {
    AVClass *class;
    int write_adts;
    int objecttype;
    int sample_rate_index;
    int channel_conf;
    int pce_size;
    int apetag;
    int id3v2tag;
    int mpeg_id;
    uint8_t pce_data[MAX_PCE_SIZE];
} ADTSContext;

#define ADTS_MAX_FRAME_BYTES ((1 << 14) - 1)

static int adts_decode_extradata(AVFormatContext *s, ADTSContext *adts, const uint8_t *buf, int size)
{
    GetBitContext gb;
    PutBitContext pb;
    MPEG4AudioConfig m4ac;
    int off, ret;

    ret = init_get_bits8(&gb, buf, size);
    if (ret < 0)
        return ret;
    off = avpriv_mpeg4audio_get_config2(&m4ac, buf, size, 1, s);
    if (off < 0)
        return off;
    skip_bits_long(&gb, off);
    adts->objecttype        = m4ac.object_type - 1;
    adts->sample_rate_index = m4ac.sampling_index;
    adts->channel_conf      = m4ac.chan_config;

    if (adts->objecttype > 3U) {
        av_log(s, AV_LOG_ERROR, "MPEG-4 AOT %d is not allowed in ADTS\n", adts->objecttype+1);
        return AVERROR_INVALIDDATA;
    }
    if (adts->sample_rate_index == 15) {
        av_log(s, AV_LOG_ERROR, "Escape sample rate index illegal in ADTS\n");
        return AVERROR_INVALIDDATA;
    }
    if (get_bits(&gb, 1)) {
        av_log(s, AV_LOG_ERROR, "960/120 MDCT window is not allowed in ADTS\n");
        return AVERROR_INVALIDDATA;
    }
    if (get_bits(&gb, 1)) {
        av_log(s, AV_LOG_ERROR, "Scalable configurations are not allowed in ADTS\n");
        return AVERROR_INVALIDDATA;
    }
    if (get_bits(&gb, 1)) {
        av_log(s, AV_LOG_ERROR, "Extension flag is not allowed in ADTS\n");
        return AVERROR_INVALIDDATA;
    }
    if (!adts->channel_conf) {
        init_put_bits(&pb, adts->pce_data, MAX_PCE_SIZE);

        put_bits(&pb, 3, 5); //ID_PCE
        adts->pce_size = (ff_copy_pce_data(&pb, &gb) + 3) / 8;
        flush_put_bits(&pb);
    }

    adts->write_adts = 1;

    return 0;
}

static int adts_init(AVFormatContext *s)
{
    ADTSContext *adts = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;

    if (par->codec_id != AV_CODEC_ID_AAC) {
        av_log(s, AV_LOG_ERROR, "Only AAC streams can be muxed by the ADTS muxer\n");
        return AVERROR(EINVAL);
    }
    if (par->extradata_size > 0)
        return adts_decode_extradata(s, adts, par->extradata,
                                     par->extradata_size);

    return 0;
}

static int adts_write_header(AVFormatContext *s)
{
    ADTSContext *adts = s->priv_data;

    if (adts->id3v2tag)
        ff_id3v2_write_simple(s, 4, ID3v2_DEFAULT_MAGIC);

    return 0;
}

static int adts_write_frame_header(ADTSContext *ctx,
                                   uint8_t *buf, int size, int pce_size)
{
    PutBitContext pb;

    unsigned full_frame_size = (unsigned)ADTS_HEADER_SIZE + size + pce_size;
    if (full_frame_size > ADTS_MAX_FRAME_BYTES) {
        av_log(NULL, AV_LOG_ERROR, "ADTS frame size too large: %u (max %d)\n",
               full_frame_size, ADTS_MAX_FRAME_BYTES);
        return AVERROR_INVALIDDATA;
    }

    init_put_bits(&pb, buf, ADTS_HEADER_SIZE);

    /* adts_fixed_header */
    put_bits(&pb, 12, 0xfff);   /* syncword */
    put_bits(&pb, 1, ctx->mpeg_id); /* ID */
    put_bits(&pb, 2, 0);        /* layer */
    put_bits(&pb, 1, 1);        /* protection_absent */
    put_bits(&pb, 2, ctx->objecttype); /* profile_objecttype */
    put_bits(&pb, 4, ctx->sample_rate_index);
    put_bits(&pb, 1, 0);        /* private_bit */
    put_bits(&pb, 3, ctx->channel_conf); /* channel_configuration */
    put_bits(&pb, 1, 0);        /* original_copy */
    put_bits(&pb, 1, 0);        /* home */

    /* adts_variable_header */
    put_bits(&pb, 1, 0);        /* copyright_identification_bit */
    put_bits(&pb, 1, 0);        /* copyright_identification_start */
    put_bits(&pb, 13, full_frame_size); /* aac_frame_length */
    put_bits(&pb, 11, 0x7ff);   /* adts_buffer_fullness */
    put_bits(&pb, 2, 0);        /* number_of_raw_data_blocks_in_frame */

    flush_put_bits(&pb);

    return 0;
}

static int adts_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ADTSContext *adts = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;
    AVIOContext *pb = s->pb;
    uint8_t buf[ADTS_HEADER_SIZE];

    if (!pkt->size)
        return 0;
    if (!par->extradata_size) {
        uint8_t *side_data;
        size_t side_data_size;
        int ret;

        side_data = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                            &side_data_size);
        if (side_data_size) {
            ret = adts_decode_extradata(s, adts, side_data, side_data_size);
            if (ret < 0)
                return ret;
            ret = ff_alloc_extradata(par, side_data_size);
            if (ret < 0)
                return ret;
            memcpy(par->extradata, side_data, side_data_size);
        }
    }
    if (adts->write_adts) {
        int err = adts_write_frame_header(adts, buf, pkt->size,
                                             adts->pce_size);
        if (err < 0)
            return err;
        avio_write(pb, buf, ADTS_HEADER_SIZE);
        if (adts->pce_size) {
            avio_write(pb, adts->pce_data, adts->pce_size);
            adts->pce_size = 0;
        }
    }
    avio_write(pb, pkt->data, pkt->size);

    return 0;
}

static int adts_write_trailer(AVFormatContext *s)
{
    ADTSContext *adts = s->priv_data;

    if (adts->apetag)
        ff_ape_write_tag(s);

    return 0;
}

#define ENC AV_OPT_FLAG_ENCODING_PARAM
#define OFFSET(obj) offsetof(ADTSContext, obj)
static const AVOption options[] = {
    { "write_id3v2",  "Enable ID3v2 tag writing",   OFFSET(id3v2tag), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, ENC},
    { "write_apetag", "Enable APE tag writing",     OFFSET(apetag),   AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, ENC},
    { "write_mpeg2",  "Set MPEG version to MPEG-2", OFFSET(mpeg_id),  AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, ENC},
    { NULL },
};

static const AVClass adts_muxer_class = {
    .class_name     = "ADTS muxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

const AVOutputFormat ff_adts_muxer = {
    .name              = "adts",
    .long_name         = NULL_IF_CONFIG_SMALL("ADTS AAC (Advanced Audio Coding)"),
    .mime_type         = "audio/aac",
    .extensions        = "aac,adts",
    .priv_data_size    = sizeof(ADTSContext),
    .audio_codec       = AV_CODEC_ID_AAC,
    .video_codec       = AV_CODEC_ID_NONE,
    .init              = adts_init,
    .write_header      = adts_write_header,
    .write_packet      = adts_write_packet,
    .write_trailer     = adts_write_trailer,
    .priv_class        = &adts_muxer_class,
    .flags             = AVFMT_NOTIMESTAMPS,
};
