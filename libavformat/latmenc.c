/*
 * LATM/LOAS muxer
 * Copyright (c) 2011 Kieran Kunhya <kieran@kunhya.com>
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

#include "libavcodec/get_bits.h"
#include "libavcodec/put_bits.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/mpeg4audio.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "rawenc.h"

#define MAX_EXTRADATA_SIZE 1024

typedef struct {
    AVClass *av_class;
    int off;
    int channel_conf;
    int object_type;
    int counter;
    int mod;
    uint8_t buffer[0x1fff + MAX_EXTRADATA_SIZE + 1024];
} LATMContext;

static const AVOption options[] = {
    {"smc-interval", "StreamMuxConfig interval.",
     offsetof(LATMContext, mod), AV_OPT_TYPE_INT, {.i64 = 0x0014}, 0x0001, 0xffff, AV_OPT_FLAG_ENCODING_PARAM},
    {NULL},
};

static const AVClass latm_muxer_class = {
    .class_name = "LATM/LOAS muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int latm_decode_extradata(LATMContext *ctx, uint8_t *buf, int size)
{
    MPEG4AudioConfig m4ac;

    if (size > MAX_EXTRADATA_SIZE) {
        av_log(ctx, AV_LOG_ERROR, "Extradata is larger than currently supported.\n");
        return AVERROR_INVALIDDATA;
    }
    ctx->off = avpriv_mpeg4audio_get_config(&m4ac, buf, size * 8, 1);
    if (ctx->off < 0)
        return ctx->off;

    if (ctx->object_type == AOT_ALS && (ctx->off & 7)) {
        // as long as avpriv_mpeg4audio_get_config works correctly this is impossible
        av_log(ctx, AV_LOG_ERROR, "BUG: ALS offset is not byte-aligned\n");
        return AVERROR_INVALIDDATA;
    }
    /* FIXME: are any formats not allowed in LATM? */

    if (m4ac.object_type > AOT_SBR && m4ac.object_type != AOT_ALS) {
        av_log(ctx, AV_LOG_ERROR, "Muxing MPEG-4 AOT %d in LATM is not supported\n", m4ac.object_type);
        return AVERROR_INVALIDDATA;
    }
    ctx->channel_conf = m4ac.chan_config;
    ctx->object_type  = m4ac.object_type;

    return 0;
}

static int latm_write_header(AVFormatContext *s)
{
    LATMContext *ctx = s->priv_data;
    AVCodecContext *avctx = s->streams[0]->codec;

    if (avctx->codec_id == AV_CODEC_ID_AAC_LATM)
        return 0;

    if (avctx->extradata_size > 0 &&
        latm_decode_extradata(ctx, avctx->extradata, avctx->extradata_size) < 0)
        return AVERROR_INVALIDDATA;

    return 0;
}

static void latm_write_frame_header(AVFormatContext *s, PutBitContext *bs)
{
    LATMContext *ctx = s->priv_data;
    AVCodecContext *avctx = s->streams[0]->codec;
    int header_size;

    /* AudioMuxElement */
    put_bits(bs, 1, !!ctx->counter);

    if (!ctx->counter) {
        /* StreamMuxConfig */
        put_bits(bs, 1, 0); /* audioMuxVersion */
        put_bits(bs, 1, 1); /* allStreamsSameTimeFraming */
        put_bits(bs, 6, 0); /* numSubFrames */
        put_bits(bs, 4, 0); /* numProgram */
        put_bits(bs, 3, 0); /* numLayer */

        /* AudioSpecificConfig */
        if (ctx->object_type == AOT_ALS) {
            header_size = avctx->extradata_size-(ctx->off >> 3);
            avpriv_copy_bits(bs, &avctx->extradata[ctx->off >> 3], header_size);
        } else {
            // + 3 assumes not scalable and dependsOnCoreCoder == 0,
            // see decode_ga_specific_config in libavcodec/aacdec.c
            avpriv_copy_bits(bs, avctx->extradata, ctx->off + 3);

            if (!ctx->channel_conf) {
                GetBitContext gb;
                init_get_bits8(&gb, avctx->extradata, avctx->extradata_size);
                skip_bits_long(&gb, ctx->off + 3);
                avpriv_copy_pce_data(bs, &gb);
            }
        }

        put_bits(bs, 3, 0); /* frameLengthType */
        put_bits(bs, 8, 0xff); /* latmBufferFullness */

        put_bits(bs, 1, 0); /* otherDataPresent */
        put_bits(bs, 1, 0); /* crcCheckPresent */
    }

    ctx->counter++;
    ctx->counter %= ctx->mod;
}

static int latm_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    LATMContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    PutBitContext bs;
    int i, len;
    uint8_t loas_header[] = "\x56\xe0\x00";

    if (s->streams[0]->codec->codec_id == AV_CODEC_ID_AAC_LATM)
        return ff_raw_write_packet(s, pkt);

    if (pkt->size > 2 && pkt->data[0] == 0xff && (pkt->data[1] >> 4) == 0xf) {
        av_log(s, AV_LOG_ERROR, "ADTS header detected - ADTS will not be incorrectly muxed into LATM\n");
        return AVERROR_INVALIDDATA;
    }

    if (!s->streams[0]->codec->extradata) {
        if(pkt->size > 2 && pkt->data[0] == 0x56 && (pkt->data[1] >> 4) == 0xe &&
            (AV_RB16(pkt->data + 1) & 0x1FFF) + 3 == pkt->size)
            return ff_raw_write_packet(s, pkt);
        else
            return AVERROR_INVALIDDATA;
    }

    if (pkt->size > 0x1fff)
        goto too_large;

    init_put_bits(&bs, ctx->buffer, pkt->size+1024+MAX_EXTRADATA_SIZE);

    latm_write_frame_header(s, &bs);

    /* PayloadLengthInfo() */
    for (i = 0; i <= pkt->size-255; i+=255)
        put_bits(&bs, 8, 255);

    put_bits(&bs, 8, pkt->size-i);

    /* The LATM payload is written unaligned */

    /* PayloadMux() */
    if (pkt->size && (pkt->data[0] & 0xe1) == 0x81) {
        // Convert byte-aligned DSE to non-aligned.
        // Due to the input format encoding we know that
        // it is naturally byte-aligned in the input stream,
        // so there are no padding bits to account for.
        // To avoid having to add padding bits and rearrange
        // the whole stream we just remove the byte-align flag.
        // This allows us to remux our FATE AAC samples into latm
        // files that are still playable with minimal effort.
        put_bits(&bs, 8, pkt->data[0] & 0xfe);
        avpriv_copy_bits(&bs, pkt->data + 1, 8*pkt->size - 8);
    } else
        avpriv_copy_bits(&bs, pkt->data, 8*pkt->size);

    avpriv_align_put_bits(&bs);
    flush_put_bits(&bs);

    len = put_bits_count(&bs) >> 3;

    if (len > 0x1fff)
        goto too_large;

    loas_header[1] |= (len >> 8) & 0x1f;
    loas_header[2] |= len & 0xff;

    avio_write(pb, loas_header, 3);
    avio_write(pb, ctx->buffer, len);

    return 0;

too_large:
    av_log(s, AV_LOG_ERROR, "LATM packet size larger than maximum size 0x1fff\n");
    return AVERROR_INVALIDDATA;
}

AVOutputFormat ff_latm_muxer = {
    .name           = "latm",
    .long_name      = NULL_IF_CONFIG_SMALL("LOAS/LATM"),
    .mime_type      = "audio/MP4A-LATM",
    .extensions     = "latm,loas",
    .priv_data_size = sizeof(LATMContext),
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_NONE,
    .write_header   = latm_write_header,
    .write_packet   = latm_write_packet,
    .priv_class     = &latm_muxer_class,
};
