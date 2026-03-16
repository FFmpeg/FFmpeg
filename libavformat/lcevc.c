/*
 * LCEVC helper functions for muxers
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

#include "libavutil/error.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/h2645_parse.h"
#include "libavcodec/lcevc.h"
#include "libavcodec/lcevctab.h"
#include "libavcodec/lcevc_parse.h"
#include "avio.h"
#include "avio_internal.h"
#include "lcevc.h"

typedef struct LCEVCDecoderConfigurationRecord {
    uint8_t  profile_idc;
    uint8_t  level_idc;
    uint8_t  chroma_format_idc;
    uint8_t  bit_depth_luma_minus8;
    uint8_t  bit_depth_chroma_minus8;
    uint32_t pic_width_in_luma_samples;
    uint32_t pic_height_in_luma_samples;
} LCEVCDecoderConfigurationRecord;

/**
 * Rewrite the NALu stripping the unneeded blocks.
 * Given that length fields coded inside the NALu are not aware of any emulation_3bytes
 * present in the bitstream, we need to keep track of the raw buffer as we navigate
 * the stripped buffer in order to write proper NALu sizes.
 */
static int write_nalu(LCEVCDecoderConfigurationRecord *lvcc, AVIOContext *pb,
                      const H2645NAL *nal)
{
    GetByteContext gbc, raw_gbc;
    int64_t start = avio_tell(pb), end;
    int sc = 0, gc = 0;
    int skipped_byte_pos = 0, nalu_length = 3;

    bytestream2_init(&gbc, nal->data, nal->size);
    bytestream2_init(&raw_gbc, nal->raw_data, nal->raw_size);
    avio_wb16(pb, 0); // size placeholder
    avio_wb16(pb, bytestream2_get_be16(&gbc)); // nal_unit_header
    bytestream2_skip(&raw_gbc, 2);

    while (bytestream2_get_bytes_left(&gbc) > 1 && (!sc || !gc)) {
        GetBitContext gb;
        uint64_t payload_size;
        int payload_size_type, payload_type;
        int block_size, raw_block_size, block_end;

        init_get_bits8(&gb, gbc.buffer, bytestream2_get_bytes_left(&gbc));

        payload_size_type = get_bits(&gb, 3);
        payload_type      = get_bits(&gb, 5);
        payload_size      = payload_size_type;
        if (payload_size_type == 6)
            return AVERROR_PATCHWELCOME;
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

        switch (payload_type) {
        case 0:
            if (sc)
                break;

            lvcc->profile_idc = get_bits(&gb, 4);
            lvcc->level_idc = get_bits(&gb, 4);

            avio_write(pb, raw_gbc.buffer, raw_block_size);
            nalu_length += raw_block_size;
            sc = 1;
            break;
        case 1: {
            int resolution_type, bit_depth;
            int processed_planes_type_flag;

            if (gc)
                break;

            processed_planes_type_flag = get_bits1(&gb);
            resolution_type = get_bits(&gb, 6);

            skip_bits1(&gb);
            lvcc->chroma_format_idc = get_bits(&gb, 2);

            skip_bits(&gb, 2);
            bit_depth = get_bits(&gb, 2) * 2; // enhancement_depth_type
            lvcc->bit_depth_luma_minus8 = bit_depth;
            lvcc->bit_depth_chroma_minus8 = bit_depth;

            if (resolution_type < 63) {
                lvcc->pic_width_in_luma_samples  = ff_lcevc_resolution_type[resolution_type].width;
                lvcc->pic_height_in_luma_samples = ff_lcevc_resolution_type[resolution_type].height;
            } else {
                int upsample_type, tile_dimensions_type;
                int temporal_step_width_modifier_signalled_flag, level1_filtering_signalled_flag;
                // Skip syntax elements until we get to the custom dimension ones
                temporal_step_width_modifier_signalled_flag = get_bits1(&gb);
                skip_bits(&gb, 3);
                upsample_type = get_bits(&gb, 3);
                level1_filtering_signalled_flag = get_bits1(&gb);
                skip_bits(&gb, 4);
                tile_dimensions_type = get_bits(&gb, 2);
                skip_bits(&gb, 4);
                if (processed_planes_type_flag)
                    skip_bits(&gb, 4);
                if (temporal_step_width_modifier_signalled_flag)
                    skip_bits(&gb, 8);
                if (upsample_type)
                    skip_bits_long(&gb, 64);
                if (level1_filtering_signalled_flag)
                    skip_bits(&gb, 8);
                if (tile_dimensions_type) {
                    if (tile_dimensions_type == 3)
                        skip_bits_long(&gb, 32);
                    skip_bits(&gb, 8);
                }

                lvcc->pic_width_in_luma_samples = get_bits(&gb, 16);
                lvcc->pic_height_in_luma_samples = get_bits(&gb, 16);
            }

            if (!lvcc->pic_width_in_luma_samples || !lvcc->pic_height_in_luma_samples)
                break;

            avio_write(pb, raw_gbc.buffer, raw_block_size);
            nalu_length += raw_block_size;
            gc = 1;
            break;
        }
        case 5:
            avio_write(pb, raw_gbc.buffer, raw_block_size);
            nalu_length += raw_block_size;
            break;
        default:
            break;
        }

        bytestream2_skip(&gbc, block_size);
        bytestream2_skip(&raw_gbc, raw_block_size);
    }

    if (!sc || !gc)
        return AVERROR_INVALIDDATA;

    avio_w8(pb, 0x80); // rbsp_alignment bits

    end = avio_tell(pb);
    avio_seek(pb, start, SEEK_SET);
    avio_wb16(pb, nalu_length);
    avio_seek(pb, end, SEEK_SET);

    return 0;
}

int ff_isom_write_lvcc(AVIOContext *pb, const uint8_t *data, int len)
{
    LCEVCDecoderConfigurationRecord lvcc = { 0 };
    AVIOContext *idr_pb = NULL, *nidr_pb = NULL;
    H2645Packet h2645_pkt = { 0 };
    uint8_t *idr, *nidr;
    uint32_t idr_size = 0, nidr_size = 0;
    int ret, nb_idr = 0, nb_nidr = 0;

    if (len <= 6)
        return AVERROR_INVALIDDATA;

    /* check for start code */
    if (AV_RB32(data) != 0x00000001 &&
        AV_RB24(data) != 0x000001) {
        avio_write(pb, data, len);
        return 0;
    }

    ret = ff_h2645_packet_split(&h2645_pkt, data, len, NULL, 0, AV_CODEC_ID_LCEVC, 0);
    if (ret < 0)
        return ret;

    ret = avio_open_dyn_buf(&idr_pb);
    if (ret < 0)
        goto fail;
    ret = avio_open_dyn_buf(&nidr_pb);
    if (ret < 0)
        goto fail;

    /* look for IDR or NON_IDR */
    for (int i = 0; i < h2645_pkt.nb_nals; i++) {
        const H2645NAL *nal = &h2645_pkt.nals[i];

        if (nal->type == LCEVC_IDR_NUT) {
            nb_idr++;

            ret = write_nalu(&lvcc, idr_pb, nal);
            if (ret < 0)
                goto fail;
        } else if (nal->type == LCEVC_NON_IDR_NUT) {
            nb_nidr++;

            ret = write_nalu(&lvcc, nidr_pb, nal);
            if (ret < 0)
                goto fail;
        }
    }
    idr_size = avio_get_dyn_buf(idr_pb, &idr);
    nidr_size = avio_get_dyn_buf(nidr_pb, &nidr);

    if (!idr_size && !nidr_size) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    avio_w8(pb, 1); /* version */
    avio_w8(pb, lvcc.profile_idc);
    avio_w8(pb, lvcc.level_idc);
    avio_w8(pb, (lvcc.chroma_format_idc     << 6) |
                (lvcc.bit_depth_luma_minus8 << 3) |
                 lvcc.bit_depth_chroma_minus8);
    avio_w8(pb, 0xff); /* 2 bits nal size length - 1 (11) + 6 bits reserved (111111)*/
    avio_wb32(pb, lvcc.pic_width_in_luma_samples);
    avio_wb32(pb, lvcc.pic_height_in_luma_samples);
    avio_w8(pb, 0xff);

    int nb_arrays = !!nb_idr + !!nb_nidr;
    avio_w8(pb, nb_arrays);

    if (nb_idr) {
        avio_w8(pb, LCEVC_IDR_NUT);
        avio_wb16(pb, nb_idr);
        avio_write(pb, idr, idr_size);
    }
    if (nb_nidr) {
        avio_w8(pb, LCEVC_NON_IDR_NUT);
        avio_wb16(pb, nb_nidr);
        avio_write(pb, nidr, nidr_size);
    }

    ret = 0;
fail:
    ffio_free_dyn_buf(&idr_pb);
    ffio_free_dyn_buf(&nidr_pb);
    ff_h2645_packet_uninit(&h2645_pkt);

    return ret;
}
