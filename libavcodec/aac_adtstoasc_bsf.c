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
static int aac_adtstoasc_filter(AVBitStreamFilterContext *bsfc,
                                AVCodecContext *avctx, const char *args,
                                uint8_t  **poutbuf, int *poutbuf_size,
                                const uint8_t *buf, int      buf_size,
                                int keyframe)
{
    GetBitContext gb;
    PutBitContext pb;
    AACADTSHeaderInfo hdr;

    AACBSFContext *ctx = bsfc->priv_data;

    init_get_bits(&gb, buf, AAC_ADTS_HEADER_SIZE*8);

    *poutbuf = (uint8_t*) buf;
    *poutbuf_size = buf_size;

    if (avctx->extradata)
        if (show_bits(&gb, 12) != 0xfff)
            return 0;

    if (avpriv_aac_parse_header(&gb, &hdr) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error parsing ADTS frame header!\n");
        return -1;
    }

    if (!hdr.crc_absent && hdr.num_aac_frames > 1) {
        avpriv_report_missing_feature(avctx,
                                      "Multiple RDBs per frame with CRC");
        return AVERROR_PATCHWELCOME;
    }

    buf      += AAC_ADTS_HEADER_SIZE + 2*!hdr.crc_absent;
    buf_size -= AAC_ADTS_HEADER_SIZE + 2*!hdr.crc_absent;

    if (!ctx->first_frame_done) {
        int            pce_size = 0;
        uint8_t        pce_data[MAX_PCE_SIZE];
        if (!hdr.chan_config) {
            init_get_bits(&gb, buf, buf_size * 8);
            if (get_bits(&gb, 3) != 5) {
                avpriv_report_missing_feature(avctx,
                                              "PCE-based channel configuration "
                                              "without PCE as first syntax "
                                              "element");
                return AVERROR_PATCHWELCOME;
            }
            init_put_bits(&pb, pce_data, MAX_PCE_SIZE);
            pce_size = avpriv_copy_pce_data(&pb, &gb)/8;
            flush_put_bits(&pb);
            buf_size -= get_bits_count(&gb)/8;
            buf      += get_bits_count(&gb)/8;
        }
        avctx->extradata_size = 2 + pce_size;
        avctx->extradata = av_mallocz(avctx->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);

        init_put_bits(&pb, avctx->extradata, avctx->extradata_size);
        put_bits(&pb, 5, hdr.object_type);
        put_bits(&pb, 4, hdr.sampling_index);
        put_bits(&pb, 4, hdr.chan_config);
        put_bits(&pb, 1, 0); //frame length - 1024 samples
        put_bits(&pb, 1, 0); //does not depend on core coder
        put_bits(&pb, 1, 0); //is not extension
        flush_put_bits(&pb);
        if (pce_size) {
            memcpy(avctx->extradata + 2, pce_data, pce_size);
        }

        ctx->first_frame_done = 1;
    }

    *poutbuf = (uint8_t*) buf;
    *poutbuf_size = buf_size;

    return 0;
}

AVBitStreamFilter ff_aac_adtstoasc_bsf = {
    "aac_adtstoasc",
    sizeof(AACBSFContext),
    aac_adtstoasc_filter,
};
