/*
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

#include <stdint.h>

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "av1.h"
#include "av1_parse.h"
#include "bsf.h"
#include "bsf_internal.h"
#include "bytestream.h"
#include "h2645_parse.h"
#include "h264.h"
#include "lcevc.h"
#include "lcevc_parse.h"
#include "startcode.h"
#include "vc1_common.h"
#include "vvc.h"

#include "hevc/hevc.h"

typedef struct ExtractExtradataContext {
    const AVClass *class;

    int (*extract)(AVBSFContext *ctx, AVPacket *pkt,
                   uint8_t **data, int *size);

    /* AV1 specific fields */
    AV1Packet av1_pkt;

    /* H264/HEVC specific fields */
    H2645Packet h2645_pkt;

    /* AVOptions */
    int remove;
} ExtractExtradataContext;

static int val_in_array(const int *arr, size_t len, int val)
{
    for (size_t i = 0; i < len; i++)
        if (arr[i] == val)
            return 1;
    return 0;
}

static int metadata_is_global(const AV1OBU *obu)
{
    static const int metadata_obu_types[] = {
        AV1_METADATA_TYPE_HDR_CLL, AV1_METADATA_TYPE_HDR_MDCV,
    };
    GetBitContext gb;
    int metadata_type;

    if (init_get_bits(&gb, obu->data, obu->size_bits) < 0)
        return 0;

    metadata_type = get_leb(&gb);

    return val_in_array(metadata_obu_types, FF_ARRAY_ELEMS(metadata_obu_types),
                        metadata_type);
}

static int obu_is_global(const AV1OBU *obu)
{
    static const int extradata_obu_types[] = {
        AV1_OBU_SEQUENCE_HEADER, AV1_OBU_METADATA,
    };

    if (!val_in_array(extradata_obu_types, FF_ARRAY_ELEMS(extradata_obu_types),
                      obu->type))
        return 0;
    if (obu->type != AV1_OBU_METADATA)
        return 1;

    return metadata_is_global(obu);
}

static int extract_extradata_av1(AVBSFContext *ctx, AVPacket *pkt,
                                 uint8_t **data, int *size)
{

    ExtractExtradataContext *s = ctx->priv_data;

    int extradata_size = 0, filtered_size = 0;
    int i, has_seq = 0, ret = 0;

    ret = ff_av1_packet_split(&s->av1_pkt, pkt->data, pkt->size, ctx);
    if (ret < 0)
        return ret;

    for (i = 0; i < s->av1_pkt.nb_obus; i++) {
        AV1OBU *obu = &s->av1_pkt.obus[i];
        if (obu_is_global(obu)) {
            extradata_size += obu->raw_size;
            if (obu->type == AV1_OBU_SEQUENCE_HEADER)
                has_seq = 1;
        } else {
            filtered_size += obu->raw_size;
        }
    }

    if (extradata_size && has_seq) {
        AVBufferRef *filtered_buf = NULL;
        PutByteContext pb_filtered_data, pb_extradata;
        uint8_t *extradata;

        if (s->remove) {
            filtered_buf = av_buffer_alloc(filtered_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!filtered_buf) {
                return AVERROR(ENOMEM);
            }
            memset(filtered_buf->data + filtered_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        }

        extradata = av_malloc(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!extradata) {
            av_buffer_unref(&filtered_buf);
            return AVERROR(ENOMEM);
        }

        *data = extradata;
        *size = extradata_size;

        bytestream2_init_writer(&pb_extradata, extradata, extradata_size);
        if (s->remove)
            bytestream2_init_writer(&pb_filtered_data, filtered_buf->data, filtered_size);

        for (i = 0; i < s->av1_pkt.nb_obus; i++) {
            AV1OBU *obu = &s->av1_pkt.obus[i];
            if (obu_is_global(obu)) {
                bytestream2_put_bufferu(&pb_extradata, obu->raw_data, obu->raw_size);
            } else if (s->remove) {
                bytestream2_put_bufferu(&pb_filtered_data, obu->raw_data, obu->raw_size);
            }
        }

        if (s->remove) {
            av_buffer_unref(&pkt->buf);
            pkt->buf  = filtered_buf;
            pkt->data = filtered_buf->data;
            pkt->size = filtered_size;
        }
    }

    return 0;
}

static int extract_extradata_h2645(AVBSFContext *ctx, AVPacket *pkt,
                                   uint8_t **data, int *size)
{
    static const int extradata_nal_types_vvc[] = {
        VVC_VPS_NUT, VVC_SPS_NUT, VVC_PPS_NUT,
    };
    static const int extradata_nal_types_hevc[] = {
        HEVC_NAL_VPS, HEVC_NAL_SPS, HEVC_NAL_PPS,
    };
    static const int extradata_nal_types_h264[] = {
        H264_NAL_SPS, H264_NAL_PPS,
    };

    ExtractExtradataContext *s = ctx->priv_data;

    int extradata_size = 0, filtered_size = 0;
    const int *extradata_nal_types;
    size_t nb_extradata_nal_types;
    int filtered_nb_nals = 0;
    int i, has_sps = 0, has_vps = 0, ret = 0;

    if (ctx->par_in->codec_id == AV_CODEC_ID_VVC) {
        extradata_nal_types    = extradata_nal_types_vvc;
        nb_extradata_nal_types = FF_ARRAY_ELEMS(extradata_nal_types_vvc);
    } else if (ctx->par_in->codec_id == AV_CODEC_ID_HEVC) {
        extradata_nal_types    = extradata_nal_types_hevc;
        nb_extradata_nal_types = FF_ARRAY_ELEMS(extradata_nal_types_hevc);
    } else {
        extradata_nal_types    = extradata_nal_types_h264;
        nb_extradata_nal_types = FF_ARRAY_ELEMS(extradata_nal_types_h264);
    }

    ret = ff_h2645_packet_split(&s->h2645_pkt, pkt->data, pkt->size,
                                ctx, 0, ctx->par_in->codec_id, H2645_FLAG_SMALL_PADDING);
    if (ret < 0)
        return ret;

    for (i = 0; i < s->h2645_pkt.nb_nals; i++) {
        H2645NAL *nal = &s->h2645_pkt.nals[i];
        if (val_in_array(extradata_nal_types, nb_extradata_nal_types, nal->type)) {
            extradata_size += nal->raw_size + 4;
            if (ctx->par_in->codec_id == AV_CODEC_ID_VVC) {
                if (nal->type == VVC_SPS_NUT) has_sps = 1;
                if (nal->type == VVC_VPS_NUT) has_vps = 1;
            } else if (ctx->par_in->codec_id == AV_CODEC_ID_HEVC) {
                if (nal->type == HEVC_NAL_SPS) has_sps = 1;
                if (nal->type == HEVC_NAL_VPS) has_vps = 1;
            } else {
                if (nal->type == H264_NAL_SPS) has_sps = 1;
            }
        } else {
            filtered_size += nal->raw_size + 3 +
                             ff_h2645_unit_requires_zero_byte(ctx->par_in->codec_id, nal->type, filtered_nb_nals++);
        }
    }

    if (extradata_size &&
        ((ctx->par_in->codec_id == AV_CODEC_ID_VVC  && has_sps) ||
         (ctx->par_in->codec_id == AV_CODEC_ID_HEVC && has_sps && has_vps) ||
         (ctx->par_in->codec_id == AV_CODEC_ID_H264 && has_sps))) {
        AVBufferRef *filtered_buf = NULL;
        PutByteContext pb_filtered_data, pb_extradata;
        uint8_t *extradata;

        if (s->remove) {
            filtered_buf = av_buffer_alloc(filtered_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!filtered_buf) {
                return AVERROR(ENOMEM);
            }
            memset(filtered_buf->data + filtered_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        }

        extradata = av_malloc(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!extradata) {
            av_buffer_unref(&filtered_buf);
            return AVERROR(ENOMEM);
        }

        *data = extradata;
        *size = extradata_size;

        bytestream2_init_writer(&pb_extradata, extradata, extradata_size);
        if (s->remove)
            bytestream2_init_writer(&pb_filtered_data, filtered_buf->data, filtered_size);

        filtered_nb_nals = 0;
        for (i = 0; i < s->h2645_pkt.nb_nals; i++) {
            H2645NAL *nal = &s->h2645_pkt.nals[i];
            if (val_in_array(extradata_nal_types, nb_extradata_nal_types,
                             nal->type)) {
                bytestream2_put_be32u(&pb_extradata, 1); //startcode
                bytestream2_put_bufferu(&pb_extradata, nal->raw_data, nal->raw_size);
            } else if (s->remove) {
                if (ff_h2645_unit_requires_zero_byte(ctx->par_in->codec_id, nal->type, filtered_nb_nals++))
                    bytestream2_put_byteu(&pb_filtered_data, 0); // zero_byte
                bytestream2_put_be24u(&pb_filtered_data, 1); // startcode
                bytestream2_put_bufferu(&pb_filtered_data, nal->raw_data, nal->raw_size);
            }
        }

        if (s->remove) {
            av_buffer_unref(&pkt->buf);
            pkt->buf  = filtered_buf;
            pkt->data = filtered_buf->data;
            pkt->size = filtered_size;
        }
    }

    return 0;
}

/**
 * Rewrite the NALu stripping the unneeded blocks.
 * Given that length fields coded inside the NALu are not aware of any emulation_3bytes
 * present in the bitstream, we need to keep track of the raw buffer as we navigate
 * the stripped buffer.
 */
static int process_lcevc_nalu(PutByteContext *extradata, PutByteContext *data,
                              int *extradata_sizep, int *data_sizep, const H2645NAL *nal)
{
    GetByteContext gbc, raw_gbc;
    int sc = 0, gc = 0;
    int skipped_byte_pos = 0;
    int extradata_size = 0, data_size = 0;

    if (nal->size < 2)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&gbc,     nal->data,     nal->size);
    bytestream2_init(&raw_gbc, nal->raw_data, nal->raw_size);

    unsigned nalu_header = bytestream2_get_be16u(&gbc);

    if (data)
        bytestream2_put_be16u(data, nalu_header);
    data_size += 2;
    bytestream2_skipu(&raw_gbc, 2);

    while (bytestream2_get_bytes_left(&gbc) > 1) {
        GetBitContext gb;
        int payload_size_type, payload_type;
        uint64_t payload_size;
        int block_size, raw_block_size, block_end;

        av_unused int ret = init_get_bits8(&gb, gbc.buffer, bytestream2_get_bytes_left(&gbc));
        av_assert1(ret >= 0); // h2645_parse already opened a GetBitContext with this data

        payload_size_type = get_bits(&gb, 3);
        payload_type      = get_bits(&gb, 5);
        payload_size      = payload_size_type;
        if (payload_size_type == 6)
            return AVERROR_INVALIDDATA;
        if (payload_size_type == 7)
            payload_size = get_mb(&gb);

        if (payload_size > INT_MAX - (get_bits_count(&gb) >> 3))
            return AVERROR_INVALIDDATA;

        block_size = raw_block_size = payload_size + (get_bits_count(&gb) >> 3);
        if (block_size >= bytestream2_get_bytes_left(&gbc))
            return AVERROR_INVALIDDATA;

        block_end = bytestream2_tell(&gbc) + block_size;
        // Take into account removed emulation 3bytes, as payload_size in
        // the bitstream is not aware of them.
        for (; skipped_byte_pos < nal->skipped_bytes; skipped_byte_pos++) {
            if (nal->skipped_bytes_pos[skipped_byte_pos] >= block_end)
                break;
            raw_block_size++;
        }
        if (raw_block_size > bytestream2_get_bytes_left(&raw_gbc))
            return AVERROR_INVALIDDATA;

        switch (payload_type) {
        case LCEVC_PAYLOAD_TYPE_SEQUENCE_CONFIG:
        case LCEVC_PAYLOAD_TYPE_GLOBAL_CONFIG:
        case LCEVC_PAYLOAD_TYPE_ADDITIONAL_INFO:
            if (!extradata_size) {
                extradata_size = 2;
                if (extradata)
                    bytestream2_put_be16u(extradata, nalu_header);
            }
            if (extradata)
                bytestream2_put_bufferu(extradata, raw_gbc.buffer, raw_block_size);
            extradata_size += raw_block_size;
            sc |= payload_type == LCEVC_PAYLOAD_TYPE_SEQUENCE_CONFIG;
            gc |= payload_type == LCEVC_PAYLOAD_TYPE_GLOBAL_CONFIG;
            break;
        default:
            if (data)
                bytestream2_put_bufferu(data, raw_gbc.buffer, raw_block_size);
            data_size += raw_block_size;
            break;
        }

        bytestream2_skip(&gbc, block_size);
        bytestream2_skip(&raw_gbc, raw_block_size);
    }

    ++data_size;
    *data_sizep = data_size;
    if (data)
        bytestream2_put_byteu(data, 0x80); // rbsp_alignment bits
    if (extradata_size > 0) {
        if (!sc && !gc)
            return AVERROR_INVALIDDATA;

        ++extradata_size;
        if (extradata) {
            bytestream2_put_byteu(extradata, 0x80); // rbsp_alignment bits
        } else
            *extradata_sizep = extradata_size;
        return 1; // has extradata
    }
    return 0; // no extradata
}

static int extract_extradata_lcevc(AVBSFContext *ctx, AVPacket *pkt,
                                   uint8_t **data, int *size)
{
    static const int extradata_nal_types[] = {
        LCEVC_IDR_NUT, LCEVC_NON_IDR_NUT,
    };

    ExtractExtradataContext *s = ctx->priv_data;
    unsigned extradata_size = 0, filtered_size = 0;
    size_t nb_extradata_nal_types = FF_ARRAY_ELEMS(extradata_nal_types);
    int i, ret = 0;

    ret = ff_h2645_packet_split(&s->h2645_pkt, pkt->data, pkt->size,
                                ctx, 0, AV_CODEC_ID_LCEVC, H2645_FLAG_SMALL_PADDING);
    if (ret < 0)
        return ret;

    for (i = 0; i < s->h2645_pkt.nb_nals; i++) {
        H2645NAL *nal = &s->h2645_pkt.nals[i];
        unsigned start_code_size = 3 + !filtered_size;

        if (val_in_array(extradata_nal_types, nb_extradata_nal_types, nal->type)) {
            int extra_size, data_size;
            ret = process_lcevc_nalu(NULL, NULL, &extra_size, &data_size, nal);
            if (ret < 0)
                return ret;
            if (ret > 0) { // has extradata
                unsigned extra_start_code_size = 3 + !extradata_size;
                extradata_size += extra_start_code_size + extra_size;
                if (extradata_size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
                    return AVERROR_INVALIDDATA;
            }
            filtered_size += start_code_size + data_size;
        } else
            filtered_size += start_code_size + nal->raw_size;
        if (filtered_size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE)
            return AVERROR_INVALIDDATA;
    }

    if (extradata_size) {
        AVBufferRef *filtered_buf = NULL;
        PutByteContext pb_extradata;
        PutByteContext pb_filtered_data;
        uint8_t *extradata;

        if (s->remove) {
            ret = av_buffer_realloc(&filtered_buf, filtered_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (ret < 0)
                return ret;
            bytestream2_init_writer(&pb_filtered_data, filtered_buf->data, filtered_size);
            // this is the first byte of a four-byte startcode; we write it here
            // so that we only need to write three-byte startcodes below
            bytestream2_put_byteu(&pb_filtered_data, 0x00);
        }

        extradata = av_malloc(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!extradata) {
            av_buffer_unref(&filtered_buf);
            return AVERROR(ENOMEM);
        }

        *data = extradata;
        *size = extradata_size;

        bytestream2_init_writer(&pb_extradata, extradata, extradata_size);
        // We write the startcodes before the actual units because we do not
        // know in advance whether a given unit will contribute to extradata,
        // but we know when we have encountered the last unit containing
        // extradata. (The alternative is writing it and then undoing this
        // if the NALU did not contain extradata.)
        bytestream2_put_be32u(&pb_extradata, 0x00000001);

        for (i = 0; i < s->h2645_pkt.nb_nals; i++) {
            H2645NAL *nal = &s->h2645_pkt.nals[i];
            if (s->remove)
                bytestream2_put_be24u(&pb_filtered_data, 0x000001);
            if (val_in_array(extradata_nal_types, nb_extradata_nal_types,
                             nal->type)) {
                int dummy;
                ret = process_lcevc_nalu(&pb_extradata, s->remove ? &pb_filtered_data : NULL,
                                         NULL, &dummy, nal);
                av_assert1(ret >= 0); // already checked in the first pass
                if (ret > 0) {// NALU contained extradata
                    if (extradata_size != bytestream2_tell_p(&pb_extradata)) {
                        // There will be another NALU containing extradata.
                        // Already write the next start code.
                        bytestream2_put_be24u(&pb_extradata, 0x000001);
                    }
                }
            } else if (s->remove) {
                bytestream2_put_bufferu(&pb_filtered_data, nal->raw_data, nal->raw_size);
            }
        }
        av_assert1(bytestream2_tell_p(&pb_extradata) == extradata_size);

        if (s->remove) {
            av_assert1(bytestream2_tell_p(&pb_filtered_data) == filtered_size);
            memset(filtered_buf->data + filtered_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            av_buffer_unref(&pkt->buf);
            pkt->buf  = filtered_buf;
            pkt->data = filtered_buf->data;
            pkt->size = filtered_size;
        }
    }

    return 0;
}

static int extract_extradata_vc1(AVBSFContext *ctx, AVPacket *pkt,
                                 uint8_t **data, int *size)
{
    ExtractExtradataContext *s = ctx->priv_data;
    const uint8_t *ptr = pkt->data, *end = pkt->data + pkt->size;
    uint32_t state = UINT32_MAX;
    int has_extradata = 0, extradata_size = 0;

    while (ptr < end) {
        ptr = avpriv_find_start_code(ptr, end, &state);
        if (state == VC1_CODE_SEQHDR || state == VC1_CODE_ENTRYPOINT) {
            has_extradata = 1;
        } else if (has_extradata && IS_MARKER(state)) {
            extradata_size = ptr - 4 - pkt->data;
            break;
        }
    }

    if (extradata_size) {
        *data = av_malloc(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!*data)
            return AVERROR(ENOMEM);

        memcpy(*data, pkt->data, extradata_size);
        *size = extradata_size;

        if (s->remove) {
            pkt->data += extradata_size;
            pkt->size -= extradata_size;
        }
    }

    return 0;
}

static int extract_extradata_mpeg12(AVBSFContext *ctx, AVPacket *pkt,
                                     uint8_t **data, int *size)
{
    ExtractExtradataContext *s = ctx->priv_data;
    uint32_t state = UINT32_MAX;
    int i, found = 0;

    for (i = 0; i < pkt->size; i++) {
        state = (state << 8) | pkt->data[i];
        if (state == 0x1B3)
            found = 1;
        else if (found && state != 0x1B5 && state < 0x200 && state >= 0x100) {
            *size = i - 3;
            *data = av_malloc(*size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!*data)
                return AVERROR(ENOMEM);

            memcpy(*data, pkt->data, *size);

            if (s->remove) {
                pkt->data += *size;
                pkt->size -= *size;
            }
            break;
        }
    }
    return 0;
}

static int extract_extradata_mpeg4(AVBSFContext *ctx, AVPacket *pkt,
                                   uint8_t **data, int *size)
{
    ExtractExtradataContext *s = ctx->priv_data;
    const uint8_t *ptr = pkt->data, *end = pkt->data + pkt->size;
    uint32_t state = UINT32_MAX;

    while (ptr < end) {
        ptr = avpriv_find_start_code(ptr, end, &state);
        if (state == 0x1B3 || state == 0x1B6) {
            if (ptr - pkt->data > 4) {
                *size = ptr - 4 - pkt->data;
                *data = av_malloc(*size + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!*data)
                    return AVERROR(ENOMEM);

                memcpy(*data, pkt->data, *size);

                if (s->remove) {
                    pkt->data += *size;
                    pkt->size -= *size;
                }
            }
            break;
        }
    }
    return 0;
}

static const struct {
    enum AVCodecID id;
    int (*extract)(AVBSFContext *ctx, AVPacket *pkt,
                   uint8_t **data, int *size);
} extract_tab[] = {
    { AV_CODEC_ID_AV1,        extract_extradata_av1     },
    { AV_CODEC_ID_AVS2,       extract_extradata_mpeg4   },
    { AV_CODEC_ID_AVS3,       extract_extradata_mpeg4   },
    { AV_CODEC_ID_CAVS,       extract_extradata_mpeg4   },
    { AV_CODEC_ID_H264,       extract_extradata_h2645   },
    { AV_CODEC_ID_HEVC,       extract_extradata_h2645   },
    { AV_CODEC_ID_LCEVC,      extract_extradata_lcevc   },
    { AV_CODEC_ID_MPEG1VIDEO, extract_extradata_mpeg12  },
    { AV_CODEC_ID_MPEG2VIDEO, extract_extradata_mpeg12  },
    { AV_CODEC_ID_MPEG4,      extract_extradata_mpeg4   },
    { AV_CODEC_ID_VC1,        extract_extradata_vc1     },
    { AV_CODEC_ID_VVC,        extract_extradata_h2645   },
};

static int extract_extradata_init(AVBSFContext *ctx)
{
    ExtractExtradataContext *s = ctx->priv_data;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(extract_tab); i++) {
        if (extract_tab[i].id == ctx->par_in->codec_id) {
            s->extract = extract_tab[i].extract;
            break;
        }
    }
    if (!s->extract)
        return AVERROR_BUG;

    return 0;
}

static int extract_extradata_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    ExtractExtradataContext *s = ctx->priv_data;
    uint8_t *extradata = NULL;
    int extradata_size;
    int ret = 0;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    ret = s->extract(ctx, pkt, &extradata, &extradata_size);
    if (ret < 0)
        goto fail;

    if (extradata) {
        memset(extradata + extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        ret = av_packet_add_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                      extradata, extradata_size);
        if (ret < 0) {
            av_freep(&extradata);
            goto fail;
        }
    }

    return 0;

fail:
    av_packet_unref(pkt);
    return ret;
}

static void extract_extradata_close(AVBSFContext *ctx)
{
    ExtractExtradataContext *s = ctx->priv_data;
    ff_av1_packet_uninit(&s->av1_pkt);
    ff_h2645_packet_uninit(&s->h2645_pkt);
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_AV1,
    AV_CODEC_ID_AVS2,
    AV_CODEC_ID_AVS3,
    AV_CODEC_ID_CAVS,
    AV_CODEC_ID_H264,
    AV_CODEC_ID_HEVC,
    AV_CODEC_ID_LCEVC,
    AV_CODEC_ID_MPEG1VIDEO,
    AV_CODEC_ID_MPEG2VIDEO,
    AV_CODEC_ID_MPEG4,
    AV_CODEC_ID_VC1,
    AV_CODEC_ID_VVC,
    AV_CODEC_ID_NONE,
};

#define OFFSET(x) offsetof(ExtractExtradataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption options[] = {
    { "remove", "remove the extradata from the bitstream", OFFSET(remove), AV_OPT_TYPE_INT,
        { .i64 = 0 }, 0, 1, FLAGS },
    { NULL },
};

static const AVClass extract_extradata_class = {
    .class_name = "extract_extradata",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFBitStreamFilter ff_extract_extradata_bsf = {
    .p.name         = "extract_extradata",
    .p.codec_ids    = codec_ids,
    .p.priv_class   = &extract_extradata_class,
    .priv_data_size = sizeof(ExtractExtradataContext),
    .init           = extract_extradata_init,
    .filter         = extract_extradata_filter,
    .close          = extract_extradata_close,
};
