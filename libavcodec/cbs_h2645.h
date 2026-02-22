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

#ifndef AVCODEC_CBS_H2645_H
#define AVCODEC_CBS_H2645_H

#include "cbs.h"
#include "h2645_parse.h"


typedef struct CodedBitstreamH2645Context {
    // If set, the stream being read is in MP4 (AVCC/HVCC) format.  If not
    // set, the stream is assumed to be in annex B format.
    int mp4;
    // Size in bytes of the NAL length field for MP4 format.
    int nal_length_size;
    // Packet reader.
    H2645Packet read_packet;
} CodedBitstreamH2645Context;

struct GetBitContext;
struct PutBitContext;

int ff_cbs_read_ue_golomb(CodedBitstreamContext *ctx, struct GetBitContext *gbc,
                          const char *name, const int *subscripts,
                          uint32_t *write_to,
                          uint32_t range_min, uint32_t range_max);
int ff_cbs_read_se_golomb(CodedBitstreamContext *ctx, struct GetBitContext *gbc,
                          const char *name, const int *subscripts,
                          int32_t *write_to,
                          int32_t range_min, int32_t range_max);
int ff_cbs_write_ue_golomb(CodedBitstreamContext *ctx, struct PutBitContext *pbc,
                           const char *name, const int *subscripts,
                           uint32_t value,
                           uint32_t range_min, uint32_t range_max);
int ff_cbs_write_se_golomb(CodedBitstreamContext *ctx, struct PutBitContext *pbc,
                           const char *name, const int *subscripts,
                           int32_t value,
                           int32_t range_min, int32_t range_max);

int ff_cbs_h2645_read_more_rbsp_data(struct GetBitContext *gbc);

int ff_cbs_h2645_fragment_add_nals(CodedBitstreamContext *ctx,
                                   CodedBitstreamFragment *frag,
                                   const H2645Packet *packet);

int ff_cbs_h2645_write_slice_data(CodedBitstreamContext *ctx,
                                  struct PutBitContext *pbc, const uint8_t *data,
                                  size_t data_size, int data_bit_start);

int ff_cbs_h2645_unit_requires_zero_byte(enum AVCodecID codec_id,
                                         CodedBitstreamUnitType type,
                                         int nal_unit_index);

int ff_cbs_h2645_assemble_fragment(CodedBitstreamContext *ctx,
                                   CodedBitstreamFragment *frag);

/**
 * payload_extension_present() - true if we are before the last 1-bit
 * in the payload structure, which must be in the last byte.
 */
int ff_cbs_h2645_payload_extension_present(struct GetBitContext *gbc, uint32_t payload_size,
                                           int cur_pos);

#endif /* AVCODEC_CBS_H2645_H */
