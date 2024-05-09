/*
 * MPEG-2/4 AAC ADTS to MPEG-4 Audio Specific Configuration bitstream filter
 * Copyright (c) 2009 Alex Converse <alex.converse@gmail.com>
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

#include "adts_header.h"
#include "adts_parser.h"
#include "bsf.h"
#include "bsf_internal.h"
#include "put_bits.h"
#include "get_bits.h"
#include "mpeg4audio.h"
#include "mpeg4audio_copy_pce.h"

typedef struct AACBSFContext {
    int first_frame_done;
} AACBSFContext;

/**
 * This filter creates an MPEG-4 AudioSpecificConfig from an MPEG-2/4
 * ADTS header and removes the ADTS header.
 */
static int aac_adtstoasc_filter(AVBSFContext *bsfc, AVPacket *pkt)
{
    AACBSFContext *ctx = bsfc->priv_data;

    PutBitContext pb;
    AACADTSHeaderInfo hdr;
    int ret;

    ret = ff_bsf_get_packet_ref(bsfc, pkt);
    if (ret < 0)
        return ret;

    if (bsfc->par_in->extradata && pkt->size >= 2 && (AV_RB16(pkt->data) >> 4) != 0xfff)
        return 0;

    if (pkt->size < AV_AAC_ADTS_HEADER_SIZE)
        goto packet_too_small;

    if (ff_adts_header_parse_buf(pkt->data, &hdr) < 0) {
        av_log(bsfc, AV_LOG_ERROR, "Error parsing ADTS frame header!\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (!hdr.crc_absent && hdr.num_aac_frames > 1) {
        avpriv_report_missing_feature(bsfc,
                                      "Multiple RDBs per frame with CRC");
        ret = AVERROR_PATCHWELCOME;
        goto fail;
    }

    pkt->size -= AV_AAC_ADTS_HEADER_SIZE + 2 * !hdr.crc_absent;
    if (pkt->size <= 0)
        goto packet_too_small;
    pkt->data += AV_AAC_ADTS_HEADER_SIZE + 2 * !hdr.crc_absent;

    if (!ctx->first_frame_done) {
        int            pce_size = 0;
        uint8_t        pce_data[MAX_PCE_SIZE];
        uint8_t       *extradata;

        if (!hdr.chan_config) {
            GetBitContext gb;
            init_get_bits(&gb, pkt->data, pkt->size * 8);
            if (get_bits(&gb, 3) != 5) {
                avpriv_report_missing_feature(bsfc,
                                              "PCE-based channel configuration "
                                              "without PCE as first syntax "
                                              "element");
                ret = AVERROR_PATCHWELCOME;
                goto fail;
            }
            init_put_bits(&pb, pce_data, MAX_PCE_SIZE);
            pce_size = ff_copy_pce_data(&pb, &gb) / 8;
            flush_put_bits(&pb);
            pkt->size -= get_bits_count(&gb)/8;
            pkt->data += get_bits_count(&gb)/8;
        }

        extradata = av_packet_new_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                            2 + pce_size);
        if (!extradata) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        init_put_bits(&pb, extradata, 2 + pce_size);
        put_bits(&pb, 5, hdr.object_type);
        put_bits(&pb, 4, hdr.sampling_index);
        put_bits(&pb, 4, hdr.chan_config);
        put_bits(&pb, 1, 0); //frame length - 1024 samples
        put_bits(&pb, 1, 0); //does not depend on core coder
        put_bits(&pb, 1, 0); //is not extension
        flush_put_bits(&pb);
        if (pce_size) {
            memcpy(extradata + 2, pce_data, pce_size);
        }

        ctx->first_frame_done = 1;
    }

    return 0;

packet_too_small:
    av_log(bsfc, AV_LOG_ERROR, "Input packet too small\n");
    ret = AVERROR_INVALIDDATA;
fail:
    av_packet_unref(pkt);
    return ret;
}

static int aac_adtstoasc_init(AVBSFContext *ctx)
{
    /* Validate the extradata if the stream is already MPEG-4 AudioSpecificConfig */
    if (ctx->par_in->extradata) {
        MPEG4AudioConfig mp4ac;
        int ret = avpriv_mpeg4audio_get_config2(&mp4ac, ctx->par_in->extradata,
                                                ctx->par_in->extradata_size, 1, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error parsing AudioSpecificConfig extradata!\n");
            return ret;
        }
    }

    return 0;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_AAC, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_aac_adtstoasc_bsf = {
    .p.name         = "aac_adtstoasc",
    .p.codec_ids    = codec_ids,
    .priv_data_size = sizeof(AACBSFContext),
    .init           = aac_adtstoasc_init,
    .filter         = aac_adtstoasc_filter,
};
