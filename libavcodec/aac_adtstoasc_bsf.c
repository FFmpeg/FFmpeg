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

#include "avcodec.h"
#include "aacadtsdec.h"
#include "bsf.h"
#include "put_bits.h"
#include "get_bits.h"
#include "mpeg4audio.h"
#include "internal.h"

typedef struct AACBSFContext {
    int first_frame_done;
} AACBSFContext;

/**
 * This filter creates an MPEG-4 AudioSpecificConfig from an MPEG-2/4
 * ADTS header and removes the ADTS header.
 */
static int aac_adtstoasc_filter(AVBSFContext *bsfc, AVPacket *out)
{
    AACBSFContext *ctx = bsfc->priv_data;

    GetBitContext gb;
    PutBitContext pb;
    AACADTSHeaderInfo hdr;
    AVPacket *in;
    int ret;

    ret = ff_bsf_get_packet(bsfc, &in);
    if (ret < 0)
        return ret;

    if (in->size < AAC_ADTS_HEADER_SIZE)
        goto packet_too_small;

    init_get_bits(&gb, in->data, AAC_ADTS_HEADER_SIZE * 8);

    if (bsfc->par_in->extradata && show_bits(&gb, 12) != 0xfff)
        goto finish;

    if (avpriv_aac_parse_header(&gb, &hdr) < 0) {
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

    in->size -= AAC_ADTS_HEADER_SIZE + 2 * !hdr.crc_absent;
    if (in->size <= 0)
        goto packet_too_small;
    in->data += AAC_ADTS_HEADER_SIZE + 2 * !hdr.crc_absent;

    if (!ctx->first_frame_done) {
        int            pce_size = 0;
        uint8_t        pce_data[MAX_PCE_SIZE];
        uint8_t       *extradata;

        if (!hdr.chan_config) {
            init_get_bits(&gb, in->data, in->size * 8);
            if (get_bits(&gb, 3) != 5) {
                avpriv_report_missing_feature(bsfc,
                                              "PCE-based channel configuration "
                                              "without PCE as first syntax "
                                              "element");
                ret = AVERROR_PATCHWELCOME;
                goto fail;
            }
            init_put_bits(&pb, pce_data, MAX_PCE_SIZE);
            pce_size = avpriv_copy_pce_data(&pb, &gb)/8;
            flush_put_bits(&pb);
            in->size -= get_bits_count(&gb)/8;
            in->data += get_bits_count(&gb)/8;
        }

        extradata = av_mallocz(2 + pce_size + AV_INPUT_BUFFER_PADDING_SIZE);
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

        bsfc->par_out->extradata = extradata;
        bsfc->par_out->extradata_size = 2 + pce_size;
        ctx->first_frame_done = 1;
    }

finish:
    av_packet_move_ref(out, in);
    av_packet_free(&in);

    return 0;

packet_too_small:
    av_log(bsfc, AV_LOG_ERROR, "Input packet too small\n");
    ret = AVERROR_INVALIDDATA;
fail:
    av_packet_free(&in);
    return ret;
}

static int aac_adtstoasc_init(AVBSFContext *ctx)
{
    av_freep(&ctx->par_out->extradata);
    ctx->par_out->extradata_size = 0;

    return 0;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_AAC, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_aac_adtstoasc_bsf = {
    .name           = "aac_adtstoasc",
    .priv_data_size = sizeof(AACBSFContext),
    .init           = aac_adtstoasc_init,
    .filter         = aac_adtstoasc_filter,
    .codec_ids      = codec_ids,
};
