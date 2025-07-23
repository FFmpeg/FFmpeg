/*
 * MXF SMPTE-436M VBI/ANC parsing functions
 * Copyright (c) 2025 Jacob Lifshay
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

#include "libavcodec/smpte_436m.h"
#include "bytestream.h"
#include "libavcodec/packet.h"
#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"

static int validate_smpte_436m_anc_wrapping_type(AVSmpte436mWrappingType wrapping_type)
{
    switch (wrapping_type) {
    case AV_SMPTE_436M_WRAPPING_TYPE_VANC_FRAME:
    case AV_SMPTE_436M_WRAPPING_TYPE_VANC_FIELD_1:
    case AV_SMPTE_436M_WRAPPING_TYPE_VANC_FIELD_2:
    case AV_SMPTE_436M_WRAPPING_TYPE_VANC_PROGRESSIVE_FRAME:
    case AV_SMPTE_436M_WRAPPING_TYPE_HANC_FRAME:
    case AV_SMPTE_436M_WRAPPING_TYPE_HANC_FIELD_1:
    case AV_SMPTE_436M_WRAPPING_TYPE_HANC_FIELD_2:
    case AV_SMPTE_436M_WRAPPING_TYPE_HANC_PROGRESSIVE_FRAME:
        return 0;
    default:
        return AVERROR_INVALIDDATA;
    }
}

static int validate_smpte_436m_anc_payload_sample_coding(AVSmpte436mPayloadSampleCoding payload_sample_coding)
{
    switch (payload_sample_coding) {
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA_AND_COLOR_DIFF:
        // not allowed for ANC packets
        return AVERROR_INVALIDDATA;
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA_AND_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF_WITH_PARITY_ERROR:
        return 0;
    default:
        return AVERROR_INVALIDDATA;
    }
}

int av_smpte_436m_coded_anc_validate(const AVSmpte436mCodedAnc *anc)
{
    int ret = validate_smpte_436m_anc_wrapping_type(anc->wrapping_type);
    if (ret < 0)
        return ret;
    ret = validate_smpte_436m_anc_payload_sample_coding(anc->payload_sample_coding);
    if (ret < 0)
        return ret;
    if (anc->payload_array_length > AV_SMPTE_436M_CODED_ANC_PAYLOAD_CAPACITY)
        return AVERROR_INVALIDDATA;
    ret = av_smpte_436m_coded_anc_payload_size(anc->payload_sample_coding, anc->payload_sample_count);
    if (ret < 0)
        return ret;
    if (anc->payload_array_length < ret)
        return AVERROR_INVALIDDATA;
    return 0;
}

// Based off Table 7 (page 13) of:
// https://pub.smpte.org/latest/st436/s436m-2006.pdf
#define SMPTE_436M_ANC_ENTRY_HEADER_SIZE ( \
    2   /* line_number */                  \
    + 1 /* wrapping_type */                \
    + 1 /* payload_sample_coding */        \
    + 2 /* payload_sample_count */         \
    + 4 /* payload_array_length */         \
    + 4 /* payload_array_element_size */   \
)

/**
 * Decode an ANC packet.
 * @param[in]  in   Input bytes.
 * @param[in]  size the size of in.
 * @param[out]  anc the decoded ANC packet
 * @return The number of read bytes on success, AVERROR_INVALIDDATA otherwise.
 */
static int smpte_436m_anc_decode_entry(const uint8_t *in, int size, AVSmpte436mCodedAnc *anc)
{
    // Based off Table 7 (page 13) of:
    // https://pub.smpte.org/latest/st436/s436m-2006.pdf
    if (SMPTE_436M_ANC_ENTRY_HEADER_SIZE > size)
        return AVERROR_INVALIDDATA;
    int needed_size  = SMPTE_436M_ANC_ENTRY_HEADER_SIZE;
    anc->line_number = AV_RB16(in);
    in += 2;
    anc->wrapping_type = AV_RB8(in);
    in++;
    anc->payload_sample_coding = AV_RB8(in);
    in++;
    anc->payload_sample_count = AV_RB16(in);
    in += 2;
    anc->payload_array_length = AV_RB32(in);
    in += 4;
    uint32_t payload_array_element_size = AV_RB32(in);
    in += 4;
    if (payload_array_element_size != 1)
        return AVERROR_INVALIDDATA;
    needed_size += anc->payload_array_length;
    if (needed_size > size)
        return AVERROR_INVALIDDATA;
    if (anc->payload_array_length > AV_SMPTE_436M_CODED_ANC_PAYLOAD_CAPACITY)
        return AVERROR_INVALIDDATA;
    memcpy(anc->payload, in, anc->payload_array_length);
    int ret = av_smpte_436m_coded_anc_validate(anc);
    if (ret < 0)
        return ret;
    return needed_size;
}

/**
 * Encode an ANC packet.
 * @param[in]  anc  the ANC packet to encode
 * @param[in]  size the size of out. ignored if out is NULL.
 * @param[out] out  Output bytes. Doesn't write anything if out is NULL.
 * @return the number of bytes written on success, AVERROR codes otherwise.
 *         If out is NULL, returns the number of bytes it would have written.
 */
static int smpte_436m_anc_encode_entry(uint8_t *out, int size, const AVSmpte436mCodedAnc *anc)
{
    // Based off Table 7 (page 13) of:
    // https://pub.smpte.org/latest/st436/s436m-2006.pdf
    if (anc->payload_array_length > AV_SMPTE_436M_CODED_ANC_PAYLOAD_CAPACITY)
        return AVERROR_INVALIDDATA;
    int needed_size = SMPTE_436M_ANC_ENTRY_HEADER_SIZE + (int)anc->payload_array_length;
    if (!out)
        return needed_size;
    if (needed_size > size)
        return AVERROR_BUFFER_TOO_SMALL;
    AV_WB16(out, anc->line_number);
    out += 2;
    AV_WB8(out, anc->wrapping_type);
    out++;
    AV_WB8(out, anc->payload_sample_coding);
    out++;
    AV_WB16(out, anc->payload_sample_count);
    out += 2;
    AV_WB32(out, anc->payload_array_length);
    out += 4;
    AV_WB32(out, 1); // payload_array_element_size
    out += 4;
    memcpy(out, anc->payload, anc->payload_array_length);
    return needed_size;
}

int av_smpte_436m_anc_encode(uint8_t *out, int size, int anc_packet_count, const AVSmpte436mCodedAnc *anc_packets)
{
    // Based off Table 7 (page 13) of:
    // https://pub.smpte.org/latest/st436/s436m-2006.pdf
    if (anc_packet_count < 0 || anc_packet_count >= (1L << 16) || size < 0)
        return AVERROR_INVALIDDATA;

    int needed_size = 2;
    if (out) {
        if (size < needed_size)
            return AVERROR_BUFFER_TOO_SMALL;
        AV_WB16(out, anc_packet_count);
        out += 2;
        size -= 2;
    }
    for (int i = 0; i < anc_packet_count; i++) {
        int ret = smpte_436m_anc_encode_entry(out, size, &anc_packets[i]);
        if (ret < 0)
            return ret;
        needed_size += ret;
        if (out) {
            size -= ret;
            out += ret;
        }
    }
    return needed_size;
}

int av_smpte_436m_anc_append(AVPacket *pkt, int anc_packet_count, const AVSmpte436mCodedAnc *anc_packets)
{
    int final_packet_count = 0;
    int write_start        = 2;
    if (pkt->size >= 2) {
        final_packet_count = AV_RB16(pkt->data);
        write_start        = pkt->size;
    } else if (pkt->size != 0) // if packet isn't empty
        return AVERROR_INVALIDDATA;
    if (anc_packet_count < 0 || anc_packet_count >= (1L << 16))
        return AVERROR_INVALIDDATA;
    final_packet_count += anc_packet_count;
    if (final_packet_count >= (1L << 16))
        return AVERROR_INVALIDDATA;
    int ret, additional_size = write_start - pkt->size;
    for (int i = 0; i < anc_packet_count; i++) {
        ret = smpte_436m_anc_encode_entry(NULL, 0, &anc_packets[i]);
        if (ret < 0)
            return ret;
        additional_size += ret;
    }
    ret = av_grow_packet(pkt, additional_size);
    if (ret < 0)
        return ret;
    for (int i = 0; i < anc_packet_count; i++) {
        ret = smpte_436m_anc_encode_entry(pkt->data + write_start, pkt->size - write_start, &anc_packets[i]);
        av_assert0(ret >= 0);
        write_start += ret;
    }
    AV_WB16(pkt->data, final_packet_count);
    return 0;
}

int av_smpte_436m_anc_iter_init(AVSmpte436mAncIterator *iter, const uint8_t *buf, int buf_size)
{
    // Based off Table 7 (page 13) of:
    // https://pub.smpte.org/latest/st436/s436m-2006.pdf
    if (buf_size < 2)
        return AVERROR_INVALIDDATA;
    *iter = (AVSmpte436mAncIterator){
        .anc_packets_left = AV_RB16(buf),
        .size_left        = buf_size - 2,
        .data_left        = buf + 2,
    };
    if (iter->anc_packets_left > iter->size_left)
        return AVERROR_INVALIDDATA;
    return 0;
}

int av_smpte_436m_anc_iter_next(AVSmpte436mAncIterator *iter, AVSmpte436mCodedAnc *anc)
{
    if (iter->anc_packets_left <= 0)
        return AVERROR_EOF;
    iter->anc_packets_left--;
    int ret = smpte_436m_anc_decode_entry(iter->data_left, iter->size_left, anc);
    if (ret < 0) {
        iter->anc_packets_left = 0;
        return ret;
    }
    iter->data_left += ret;
    iter->size_left -= ret;
    return 0;
}

int av_smpte_436m_coded_anc_payload_size(AVSmpte436mPayloadSampleCoding sample_coding, uint16_t sample_count)
{
    if (sample_count > AV_SMPTE_436M_CODED_ANC_SAMPLE_CAPACITY)
        return AVERROR_INVALIDDATA;
    switch (sample_coding) {
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA_AND_COLOR_DIFF:
        return AVERROR_INVALIDDATA;
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF_WITH_PARITY_ERROR:
        // "The Payload Byte Array shall be padded to achieve UInt32 alignment."
        // section 4.4 of https://pub.smpte.org/latest/st436/s436m-2006.pdf
        return (sample_count + 3) & -4;
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA_AND_COLOR_DIFF:
        // encoded with 3 10-bit samples in a UInt32.
        // "The Payload Byte Array shall be padded to achieve UInt32 alignment."
        // section 4.4 of https://pub.smpte.org/latest/st436/s436m-2006.pdf
        return 4 * ((sample_count + 2) / 3);
    default:
        return AVERROR_INVALIDDATA;
    }
}

int av_smpte_291m_anc_8bit_decode(AVSmpte291mAnc8bit            *out,
                                  AVSmpte436mPayloadSampleCoding sample_coding,
                                  uint16_t                       sample_count,
                                  const uint8_t                 *payload,
                                  void                          *log_ctx)
{
    switch (sample_coding) {
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA_AND_COLOR_DIFF:
        return AVERROR_INVALIDDATA;
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF_WITH_PARITY_ERROR:
        {
            if (sample_count < 3)
                return AVERROR_INVALIDDATA;
            out->did         = *payload++;
            out->sdid_or_dbn = *payload++;
            out->data_count  = *payload++;
            if (sample_count < out->data_count + 3)
                return AVERROR_INVALIDDATA;
            memcpy(out->payload, payload, out->data_count);
            // the checksum isn't stored in 8-bit mode, so calculate it.
            av_smpte_291m_anc_8bit_fill_checksum(out);
            return 0;
        }
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA_AND_COLOR_DIFF:
        av_log(log_ctx,
               AV_LOG_ERROR,
               "decoding an ANC packet using the 10-bit SMPTE 436M sample coding isn't implemented.\n");
        return AVERROR_PATCHWELCOME;
    default:
        return AVERROR_INVALIDDATA;
    }
}

void av_smpte_291m_anc_8bit_fill_checksum(AVSmpte291mAnc8bit *anc)
{
    uint8_t checksum = anc->did + anc->sdid_or_dbn + anc->data_count;
    for (unsigned i = 0; i < anc->data_count; i++) {
        checksum += anc->payload[i];
    }
    anc->checksum = checksum;
}

int av_smpte_291m_anc_8bit_get_sample_count(const AVSmpte291mAnc8bit      *anc,
                                            AVSmpte436mPayloadSampleCoding sample_coding,
                                            void                          *log_ctx)
{
    switch (sample_coding) {
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA_AND_COLOR_DIFF:
        return AVERROR_INVALIDDATA;
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF_WITH_PARITY_ERROR:
        // 3 for did, sdid_or_dbn, and data_count; checksum isn't stored in 8-bit modes
        return 3 + anc->data_count;
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA_AND_COLOR_DIFF:
        av_log(log_ctx,
               AV_LOG_ERROR,
               "encoding an ANC packet using the 10-bit SMPTE 436M sample coding isn't implemented.\n");
        return AVERROR_PATCHWELCOME;
    default:
        return AVERROR_INVALIDDATA;
    }
}

int av_smpte_291m_anc_8bit_encode(AVSmpte436mCodedAnc           *out,
                                  uint16_t                       line_number,
                                  AVSmpte436mWrappingType        wrapping_type,
                                  AVSmpte436mPayloadSampleCoding sample_coding,
                                  const AVSmpte291mAnc8bit      *payload,
                                  void                          *log_ctx)
{
    out->line_number           = line_number;
    out->wrapping_type         = wrapping_type;
    out->payload_sample_coding = sample_coding;

    int ret = av_smpte_291m_anc_8bit_get_sample_count(payload, sample_coding, log_ctx);
    if (ret < 0)
        return ret;

    out->payload_sample_count = ret;

    ret = av_smpte_436m_coded_anc_payload_size(sample_coding, out->payload_sample_count);
    if (ret < 0)
        return ret;

    out->payload_array_length = ret;

    switch (sample_coding) {
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_1BIT_LUMA_AND_COLOR_DIFF:
        return AVERROR_INVALIDDATA;
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_COLOR_DIFF_WITH_PARITY_ERROR:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_8BIT_LUMA_AND_COLOR_DIFF_WITH_PARITY_ERROR:
        {
            // fill trailing padding with zeros
            av_assert0(out->payload_array_length >= 4);
            memset(out->payload + out->payload_array_length - 4, 0, 4);

            out->payload[0] = payload->did;
            out->payload[1] = payload->sdid_or_dbn;
            out->payload[2] = payload->data_count;

            memcpy(out->payload + 3, payload->payload, payload->data_count);
            return 0;
        }
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_COLOR_DIFF:
    case AV_SMPTE_436M_PAYLOAD_SAMPLE_CODING_10BIT_LUMA_AND_COLOR_DIFF:
        av_log(log_ctx,
               AV_LOG_ERROR,
               "encoding an ANC packet using the 10-bit SMPTE 436M sample coding isn't implemented.\n");
        return AVERROR_PATCHWELCOME;
    default:
        return AVERROR_INVALIDDATA;
    }
}

int av_smpte_291m_anc_8bit_extract_cta_708(const AVSmpte291mAnc8bit *anc, uint8_t *cc_data, void *log_ctx)
{
    if (anc->did != AV_SMPTE_291M_ANC_DID_CTA_708 || anc->sdid_or_dbn != AV_SMPTE_291M_ANC_SDID_CTA_708)
        return AVERROR(EAGAIN);
    GetByteContext gb;
    bytestream2_init(&gb, anc->payload, anc->data_count);
    // based on Caption Distribution Packet (CDP) Definition:
    // https://pub.smpte.org/latest/st334-2/st0334-2-2015.pdf
    uint16_t cdp_identifier = bytestream2_get_be16(&gb);
    if (cdp_identifier != 0x9669) { // CDPs always have this value
        av_log(log_ctx, AV_LOG_ERROR, "wrong cdp identifier %x\n", cdp_identifier);
        return AVERROR_INVALIDDATA;
    }
    bytestream2_get_byte(&gb); // cdp_length
    bytestream2_get_byte(&gb); // cdp_frame_rate and reserved
    bytestream2_get_byte(&gb); // flags
    bytestream2_get_be16(&gb); // cdp_hdr_sequence_cntr
    unsigned section_id = bytestream2_get_byte(&gb);

    const unsigned TIME_CODE_SECTION_ID = 0x71;
    if (section_id == TIME_CODE_SECTION_ID) {
        bytestream2_skip(&gb, 4); // skip time code section
        section_id = bytestream2_get_byte(&gb);
    }
    const unsigned CC_DATA_SECTION_ID = 0x72;
    if (section_id == CC_DATA_SECTION_ID) {
        if (bytestream2_get_bytes_left(&gb) < 1)
            goto too_short;
        // 0x1F for lower 5 bits, upper 3 bits are marker bits
        unsigned cc_count    = bytestream2_get_byte(&gb) & 0x1F;
        unsigned data_length = cc_count * 3; // EIA-608/CTA-708 triples are 3 bytes long
        if (bytestream2_get_bytes_left(&gb) < data_length)
            goto too_short;
        if (cc_data)
            bytestream2_get_bufferu(&gb, cc_data, data_length);
        return cc_count;
    }
    return AVERROR(EAGAIN);

too_short:
    av_log(log_ctx, AV_LOG_ERROR, "not enough bytes in cdp\n");
    return AVERROR_INVALIDDATA;
}
