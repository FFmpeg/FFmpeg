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

#include "golomb.h"
#include "evc.h"
#include "evc_parse.h"

// @see ISO_IEC_23094-1 (7.3.2.6 Slice layer RBSP syntax)
int ff_evc_parse_slice_header(GetBitContext *gb, EVCParserSliceHeader *sh,
                              const EVCParamSets *ps, enum EVCNALUnitType nalu_type)
{
    const EVCParserPPS *pps;
    const EVCParserSPS *sps;
    int num_tiles_in_slice = 0;
    unsigned slice_pic_parameter_set_id;

    slice_pic_parameter_set_id = get_ue_golomb_31(gb);

    if (slice_pic_parameter_set_id >= EVC_MAX_PPS_COUNT)
        return AVERROR_INVALIDDATA;

    pps = ps->pps[slice_pic_parameter_set_id];
    if(!pps)
        return AVERROR_INVALIDDATA;

    sps = ps->sps[pps->pps_seq_parameter_set_id];
    if(!sps)
        return AVERROR_INVALIDDATA;

    memset(sh, 0, sizeof(*sh));
    sh->slice_pic_parameter_set_id = slice_pic_parameter_set_id;

    if (!pps->single_tile_in_pic_flag) {
        sh->single_tile_in_slice_flag = get_bits1(gb);
        sh->first_tile_id = get_bits(gb, pps->tile_id_len_minus1 + 1);
    } else
        sh->single_tile_in_slice_flag = 1;

    if (!sh->single_tile_in_slice_flag) {
        if (pps->arbitrary_slice_present_flag)
            sh->arbitrary_slice_flag = get_bits1(gb);

        if (!sh->arbitrary_slice_flag)
            sh->last_tile_id = get_bits(gb, pps->tile_id_len_minus1 + 1);
        else {
            unsigned num_remaining_tiles_in_slice_minus1 = get_ue_golomb_long(gb);
            if (num_remaining_tiles_in_slice_minus1 > EVC_MAX_TILE_ROWS * EVC_MAX_TILE_COLUMNS - 2)
                return AVERROR_INVALIDDATA;

            num_tiles_in_slice = num_remaining_tiles_in_slice_minus1 + 2;
            sh->num_remaining_tiles_in_slice_minus1 = num_remaining_tiles_in_slice_minus1;
            for (int i = 0; i < num_tiles_in_slice - 1; ++i)
                sh->delta_tile_id_minus1[i] = get_ue_golomb_long(gb);
        }
    }

    sh->slice_type = get_ue_golomb_31(gb);

    if (nalu_type == EVC_IDR_NUT)
        sh->no_output_of_prior_pics_flag = get_bits1(gb);

    if (sps->sps_mmvd_flag && ((sh->slice_type == EVC_SLICE_TYPE_B) || (sh->slice_type == EVC_SLICE_TYPE_P)))
        sh->mmvd_group_enable_flag = get_bits1(gb);
    else
        sh->mmvd_group_enable_flag = 0;

    if (sps->sps_alf_flag) {
        int ChromaArrayType = sps->chroma_format_idc;

        sh->slice_alf_enabled_flag = get_bits1(gb);

        if (sh->slice_alf_enabled_flag) {
            sh->slice_alf_luma_aps_id = get_bits(gb, 5);
            sh->slice_alf_map_flag = get_bits1(gb);
            sh->slice_alf_chroma_idc = get_bits(gb, 2);

            if ((ChromaArrayType == 1 || ChromaArrayType == 2) && sh->slice_alf_chroma_idc > 0)
                sh->slice_alf_chroma_aps_id =  get_bits(gb, 5);
        }
        if (ChromaArrayType == 3) {
            int sliceChromaAlfEnabledFlag = 0;
            int sliceChroma2AlfEnabledFlag = 0;

            if (sh->slice_alf_chroma_idc == 1) { // @see ISO_IEC_23094-1 (7.4.5)
                sliceChromaAlfEnabledFlag = 1;
                sliceChroma2AlfEnabledFlag = 0;
            } else if (sh->slice_alf_chroma_idc == 2) {
                sliceChromaAlfEnabledFlag = 0;
                sliceChroma2AlfEnabledFlag = 1;
            } else if (sh->slice_alf_chroma_idc == 3) {
                sliceChromaAlfEnabledFlag = 1;
                sliceChroma2AlfEnabledFlag = 1;
            } else {
                sliceChromaAlfEnabledFlag = 0;
                sliceChroma2AlfEnabledFlag = 0;
            }

            if (!sh->slice_alf_enabled_flag)
                sh->slice_alf_chroma_idc = get_bits(gb, 2);

            if (sliceChromaAlfEnabledFlag) {
                sh->slice_alf_chroma_aps_id = get_bits(gb, 5);
                sh->slice_alf_chroma_map_flag = get_bits1(gb);
            }

            if (sliceChroma2AlfEnabledFlag) {
                sh->slice_alf_chroma2_aps_id = get_bits(gb, 5);
                sh->slice_alf_chroma2_map_flag = get_bits1(gb);
            }
        }
    }

    if (nalu_type != EVC_IDR_NUT) {
        if (sps->sps_pocs_flag)
            sh->slice_pic_order_cnt_lsb = get_bits(gb, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    }

    // @note
    // If necessary, add the missing fields to the EVCParserSliceHeader structure
    // and then extend parser implementation

    return 0;
}

int ff_evc_derive_poc(const EVCParamSets *ps, const EVCParserSliceHeader *sh,
                      EVCParserPoc *poc, enum EVCNALUnitType nalu_type, int tid)
{
    const EVCParserPPS *pps = ps->pps[sh->slice_pic_parameter_set_id];
    const EVCParserSPS *sps;

    if (!pps)
        return AVERROR_INVALIDDATA;

    sps = ps->sps[pps->pps_seq_parameter_set_id];
    if (!sps)
        return AVERROR_INVALIDDATA;

    if (sps->sps_pocs_flag) {
        int PicOrderCntMsb = 0;
        poc->prevPicOrderCntVal = poc->PicOrderCntVal;

        if (nalu_type == EVC_IDR_NUT)
            PicOrderCntMsb = 0;
        else {
            int MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
            int prevPicOrderCntLsb = poc->PicOrderCntVal & (MaxPicOrderCntLsb - 1);
            int prevPicOrderCntMsb = poc->PicOrderCntVal - prevPicOrderCntLsb;

            if ((sh->slice_pic_order_cnt_lsb < prevPicOrderCntLsb) &&
                ((prevPicOrderCntLsb - sh->slice_pic_order_cnt_lsb) >= (MaxPicOrderCntLsb / 2)))
                PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb;
            else if ((sh->slice_pic_order_cnt_lsb > prevPicOrderCntLsb) &&
                     ((sh->slice_pic_order_cnt_lsb - prevPicOrderCntLsb) > (MaxPicOrderCntLsb / 2)))
                PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb;
            else
                PicOrderCntMsb = prevPicOrderCntMsb;
        }
        poc->PicOrderCntVal = PicOrderCntMsb + sh->slice_pic_order_cnt_lsb;
    } else {
        if (nalu_type == EVC_IDR_NUT) {
            poc->PicOrderCntVal = 0;
            poc->DocOffset = -1;
        } else {
            int SubGopLength = 1 << sps->log2_sub_gop_length;

            if (tid > (SubGopLength > 1 ? 1 + av_log2(SubGopLength - 1) : 0))
                return AVERROR_INVALIDDATA;

            if (tid == 0) {
                poc->PicOrderCntVal = poc->prevPicOrderCntVal + SubGopLength;
                poc->DocOffset = 0;
                poc->prevPicOrderCntVal = poc->PicOrderCntVal;
            } else {
                int ExpectedTemporalId;
                int PocOffset;
                int prevDocOffset = poc->DocOffset;

                poc->DocOffset = (prevDocOffset + 1) % SubGopLength;
                if (poc->DocOffset == 0) {
                    poc->prevPicOrderCntVal += SubGopLength;
                    ExpectedTemporalId = 0;
                } else
                    ExpectedTemporalId = 1 + av_log2(poc->DocOffset);

                while (tid != ExpectedTemporalId) {
                    poc->DocOffset = (poc->DocOffset + 1) % SubGopLength;
                    if (poc->DocOffset == 0)
                        ExpectedTemporalId = 0;
                    else
                        ExpectedTemporalId = 1 + av_log2(poc->DocOffset);
                }
                PocOffset = (int)(SubGopLength * ((2.0 * poc->DocOffset + 1) / (1 << tid) - 2));
                poc->PicOrderCntVal = poc->prevPicOrderCntVal + PocOffset;
            }
        }
    }

    return 0;
}
