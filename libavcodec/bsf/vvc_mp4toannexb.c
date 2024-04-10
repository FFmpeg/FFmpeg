/*
 * H.266/VVC MP4 to Annex B byte stream format filter
 * Copyright (c) 2022, Thomas Siedel
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

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "bytestream.h"
#include "defs.h"
#include "vvc.h"

#define MIN_VVCC_LENGTH 23

typedef struct VVCBSFContext {
    uint8_t length_size;
    int extradata_parsed;
} VVCBSFContext;

static int vvc_extradata_to_annexb(AVBSFContext *ctx)
{
    GetByteContext gb;
    int length_size, num_arrays, i, j;
    int ret = 0;
    int temp = 0;
    int ptl_present;

    uint8_t *new_extradata = NULL;
    size_t new_extradata_size = 0;

    int max_picture_width = 0;
    int max_picture_height = 0;
    int avg_frame_rate = 0;

    bytestream2_init(&gb, ctx->par_in->extradata, ctx->par_in->extradata_size);
    temp = bytestream2_get_byte(&gb);
    length_size = ((temp & 6) >> 1) + 1;
    ptl_present = temp & 1;
    if (ptl_present) {
        int num_bytes_constraint_info;
        int general_profile_idc;
        int general_tier_flag;
        int general_level_idc;
        int ptl_frame_only_constraint_flag;
        int ptl_multi_layer_enabled_flag;
        int ptl_num_sub_profiles;
        int temp3, temp4, temp5;
        int temp2 = bytestream2_get_be16(&gb);
        int ols_idx = (temp2 >> 7) & 0x1ff;
        int num_sublayers = (temp2 >> 4) & 0x7;
        int constant_frame_rate = (temp2 >> 2) & 0x3;
        int chroma_format_idc = temp2 & 0x3;
        int bit_depth_minus8 = (bytestream2_get_byte(&gb) >> 5) & 0x7;
        av_log(ctx, AV_LOG_DEBUG,
               "bit_depth_minus8 %d chroma_format_idc %d\n", bit_depth_minus8,
               chroma_format_idc);
        av_log(ctx, AV_LOG_DEBUG, "constant_frame_rate %d, ols_idx %d\n",
               constant_frame_rate, ols_idx);
        // VvcPTLRecord(num_sublayers) native_ptl
        temp3 = bytestream2_get_byte(&gb);
        num_bytes_constraint_info = (temp3) & 0x3f;
        temp4 = bytestream2_get_byte(&gb);
        general_profile_idc = (temp4 >> 1) & 0x7f;
        general_tier_flag = (temp4) & 1;
        general_level_idc = bytestream2_get_byte(&gb);
        av_log(ctx, AV_LOG_DEBUG,
               "general_profile_idc %d, general_tier_flag %d, general_level_idc %d, num_sublayers %d num_bytes_constraint_info %d\n",
               general_profile_idc, general_tier_flag, general_level_idc,
               num_sublayers, num_bytes_constraint_info);

        temp5 = bytestream2_get_byte(&gb);
        ptl_frame_only_constraint_flag = (temp5 >> 7) & 0x1;
        ptl_multi_layer_enabled_flag   = (temp5 >> 6) & 0x1;
        for (i = 0; i < num_bytes_constraint_info - 1; i++) {
            // unsigned int(8*num_bytes_constraint_info - 2) general_constraint_info;
            bytestream2_get_byte(&gb);
        }

        av_log(ctx, AV_LOG_DEBUG,
               "ptl_multi_layer_enabled_flag %d, ptl_frame_only_constraint_flag %d\n",
               ptl_multi_layer_enabled_flag, ptl_frame_only_constraint_flag);

        if (num_sublayers > 1) {
            int temp6 = bytestream2_get_byte(&gb);
            uint8_t ptl_sublayer_level_present_flag[8] = { 0 };
            //uint8_t sublayer_level_idc[8] = {0};
            for (i = num_sublayers - 2; i >= 0; i--) {
                ptl_sublayer_level_present_flag[i] =
                    (temp6 >> (7 - (num_sublayers - 2 - i))) & 0x01;
            }
            // for (j=num_sublayers; j<=8 && num_sublayers > 1; j++)
            //     bit(1) ptl_reserved_zero_bit = 0;
            for (i = num_sublayers - 2; i >= 0; i--) {
                if (ptl_sublayer_level_present_flag[i]) {
                    //sublayer_level_idc[i] = bytestream2_get_byte(&gb);
                }
            }
        }

        ptl_num_sub_profiles = bytestream2_get_byte(&gb);
        for (j = 0; j < ptl_num_sub_profiles; j++) {
            // unsigned int(32) general_sub_profile_idc[j];
            bytestream2_get_be16(&gb);
            bytestream2_get_be16(&gb);
        }

        max_picture_width = bytestream2_get_be16(&gb);  // unsigned_int(16) max_picture_width;
        max_picture_height = bytestream2_get_be16(&gb); // unsigned_int(16) max_picture_height;
        avg_frame_rate = bytestream2_get_be16(&gb);     // unsigned int(16) avg_frame_rate; }
        av_log(ctx, AV_LOG_DEBUG,
               "max_picture_width %d, max_picture_height %d, avg_frame_rate %d\n",
               max_picture_width, max_picture_height, avg_frame_rate);
    }

    num_arrays = bytestream2_get_byte(&gb);

    for (i = 0; i < num_arrays; i++) {
        int cnt;
        int type = bytestream2_get_byte(&gb) & 0x1f;

        if (type == VVC_OPI_NUT || type == VVC_DCI_NUT)
            cnt = 1;
        else
            cnt = bytestream2_get_be16(&gb);

        av_log(ctx, AV_LOG_DEBUG, "nalu_type %d cnt %d\n", type, cnt);

        if (!(type == VVC_OPI_NUT || type == VVC_DCI_NUT ||
              type == VVC_VPS_NUT || type == VVC_SPS_NUT || type == VVC_PPS_NUT
              || type == VVC_PREFIX_SEI_NUT || type == VVC_SUFFIX_SEI_NUT)) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid NAL unit type in extradata: %d\n", type);
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        for (j = 0; j < cnt; j++) {
            const int nalu_len = bytestream2_get_be16(&gb);

            if (!nalu_len ||
                nalu_len > bytestream2_get_bytes_left(&gb) ||
                4 + AV_INPUT_BUFFER_PADDING_SIZE + nalu_len > SIZE_MAX - new_extradata_size) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            ret = av_reallocp(&new_extradata, new_extradata_size + nalu_len + 4
                              + AV_INPUT_BUFFER_PADDING_SIZE);
            if (ret < 0)
                goto fail;

            AV_WB32(new_extradata + new_extradata_size, 1); // add the startcode
            bytestream2_get_buffer(&gb, new_extradata + new_extradata_size + 4,
                                   nalu_len);
            new_extradata_size += 4 + nalu_len;
            memset(new_extradata + new_extradata_size, 0,
                   AV_INPUT_BUFFER_PADDING_SIZE);
        }
    }

    av_freep(&ctx->par_out->extradata);
    ctx->par_out->extradata = new_extradata;
    ctx->par_out->extradata_size = new_extradata_size;

    if (!new_extradata_size)
        av_log(ctx, AV_LOG_WARNING, "No parameter sets in the extradata\n");

    return length_size;
  fail:
    av_freep(&new_extradata);
    return ret;
}

static int vvc_mp4toannexb_init(AVBSFContext *ctx)
{
    VVCBSFContext *s = ctx->priv_data;
    int ret;

    if (ctx->par_in->extradata_size < MIN_VVCC_LENGTH ||
        AV_RB24(ctx->par_in->extradata) == 1 ||
        AV_RB32(ctx->par_in->extradata) == 1) {
        av_log(ctx, AV_LOG_VERBOSE,
               "The input looks like it is Annex B already\n");
    } else {
        ret = vvc_extradata_to_annexb(ctx);
        if (ret < 0)
            return ret;
        s->length_size = ret;
        s->extradata_parsed = 1;
    }

    return 0;
}

static int vvc_mp4toannexb_filter(AVBSFContext *ctx, AVPacket *out)
{
    VVCBSFContext *s = ctx->priv_data;
    AVPacket *in;
    GetByteContext gb;

    int is_irap = 0;
    int added_extra = 0;
    int i, ret = 0;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    if (!s->extradata_parsed) {
        av_packet_move_ref(out, in);
        av_packet_free(&in);
        return 0;
    }

    bytestream2_init(&gb, in->data, in->size);

    /* check if this packet contains an IRAP. The extradata will need to be added before any potential PH_NUT */
    while (bytestream2_get_bytes_left(&gb)) {
        uint32_t nalu_size = 0;
        int nalu_type;

        if (bytestream2_get_bytes_left(&gb) < s->length_size) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        for (i = 0; i < s->length_size; i++)
            nalu_size = (nalu_size << 8) | bytestream2_get_byte(&gb);

        if (nalu_size < 2 || nalu_size > bytestream2_get_bytes_left(&gb)) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        nalu_type = (bytestream2_peek_be16(&gb) >> 3) & 0x1f;
        is_irap = nalu_type >= VVC_IDR_W_RADL && nalu_type <= VVC_RSV_IRAP_11;
        if (is_irap) {
            break;
        }
        bytestream2_seek(&gb, nalu_size, SEEK_CUR);
    }

    bytestream2_seek(&gb, 0, SEEK_SET);
    while (bytestream2_get_bytes_left(&gb)) {
        uint32_t nalu_size = 0;
        int nalu_type;
        int add_extradata, extra_size, prev_size;

        if (bytestream2_get_bytes_left(&gb) < s->length_size) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        for (i = 0; i < s->length_size; i++)
            nalu_size = (nalu_size << 8) | bytestream2_get_byte(&gb);

        if (nalu_size < 2 || nalu_size > bytestream2_get_bytes_left(&gb)) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        nalu_type = (bytestream2_peek_be16(&gb) >> 3) & 0x1f;

        /* prepend extradata to IRAP frames */
        add_extradata = is_irap && nalu_type != VVC_AUD_NUT && !added_extra;
        extra_size = add_extradata * ctx->par_out->extradata_size;
        added_extra |= add_extradata;

        if (FFMIN(INT_MAX, SIZE_MAX) < 4ULL + nalu_size + extra_size) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        prev_size = out->size;

        ret = av_grow_packet(out, 4 + nalu_size + extra_size);
        if (ret < 0)
            goto fail;

        if (extra_size)
            memcpy(out->data + prev_size, ctx->par_out->extradata, extra_size);
        AV_WB32(out->data + prev_size + extra_size, 1);
        bytestream2_get_buffer(&gb, out->data + prev_size + 4 + extra_size,
                               nalu_size);
    }

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

  fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);

    return ret;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_VVC, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_vvc_mp4toannexb_bsf = {
    .p.name         = "vvc_mp4toannexb",
    .p.codec_ids    = codec_ids,
    .priv_data_size = sizeof(VVCBSFContext),
    .init           = vvc_mp4toannexb_init,
    .filter         = vvc_mp4toannexb_filter,
};
