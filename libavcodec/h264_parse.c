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

#include "bytestream.h"
#include "get_bits.h"
#include "golomb.h"
#include "h264.h"
#include "h264dec.h"
#include "h264_parse.h"
#include "h264_ps.h"

int ff_h264_pred_weight_table(GetBitContext *gb, const SPS *sps,
                              const int *ref_count, int slice_type_nos,
                              H264PredWeightTable *pwt,
                              int picture_structure, void *logctx)
{
    int list, i, j;
    int luma_def, chroma_def;

    pwt->use_weight             = 0;
    pwt->use_weight_chroma      = 0;

    pwt->luma_log2_weight_denom = get_ue_golomb(gb);
    if (pwt->luma_log2_weight_denom > 7U) {
        av_log(logctx, AV_LOG_ERROR, "luma_log2_weight_denom %d is out of range\n", pwt->luma_log2_weight_denom);
        pwt->luma_log2_weight_denom = 0;
    }
    luma_def = 1 << pwt->luma_log2_weight_denom;

    if (sps->chroma_format_idc) {
        pwt->chroma_log2_weight_denom = get_ue_golomb(gb);
        if (pwt->chroma_log2_weight_denom > 7U) {
            av_log(logctx, AV_LOG_ERROR, "chroma_log2_weight_denom %d is out of range\n", pwt->chroma_log2_weight_denom);
            pwt->chroma_log2_weight_denom = 0;
        }
        chroma_def = 1 << pwt->chroma_log2_weight_denom;
    }

    for (list = 0; list < 2; list++) {
        pwt->luma_weight_flag[list]   = 0;
        pwt->chroma_weight_flag[list] = 0;
        for (i = 0; i < ref_count[list]; i++) {
            int luma_weight_flag, chroma_weight_flag;

            luma_weight_flag = get_bits1(gb);
            if (luma_weight_flag) {
                pwt->luma_weight[i][list][0] = get_se_golomb(gb);
                pwt->luma_weight[i][list][1] = get_se_golomb(gb);
                if ((int8_t)pwt->luma_weight[i][list][0] != pwt->luma_weight[i][list][0] ||
                    (int8_t)pwt->luma_weight[i][list][1] != pwt->luma_weight[i][list][1])
                    goto out_range_weight;
                if (pwt->luma_weight[i][list][0] != luma_def ||
                    pwt->luma_weight[i][list][1] != 0) {
                    pwt->use_weight             = 1;
                    pwt->luma_weight_flag[list] = 1;
                }
            } else {
                pwt->luma_weight[i][list][0] = luma_def;
                pwt->luma_weight[i][list][1] = 0;
            }

            if (sps->chroma_format_idc) {
                chroma_weight_flag = get_bits1(gb);
                if (chroma_weight_flag) {
                    int j;
                    for (j = 0; j < 2; j++) {
                        pwt->chroma_weight[i][list][j][0] = get_se_golomb(gb);
                        pwt->chroma_weight[i][list][j][1] = get_se_golomb(gb);
                        if ((int8_t)pwt->chroma_weight[i][list][j][0] != pwt->chroma_weight[i][list][j][0] ||
                            (int8_t)pwt->chroma_weight[i][list][j][1] != pwt->chroma_weight[i][list][j][1]) {
                            pwt->chroma_weight[i][list][j][0] = chroma_def;
                            pwt->chroma_weight[i][list][j][1] = 0;
                            goto out_range_weight;
                        }
                        if (pwt->chroma_weight[i][list][j][0] != chroma_def ||
                            pwt->chroma_weight[i][list][j][1] != 0) {
                            pwt->use_weight_chroma        = 1;
                            pwt->chroma_weight_flag[list] = 1;
                        }
                    }
                } else {
                    int j;
                    for (j = 0; j < 2; j++) {
                        pwt->chroma_weight[i][list][j][0] = chroma_def;
                        pwt->chroma_weight[i][list][j][1] = 0;
                    }
                }
            }

            // for MBAFF
            if (picture_structure == PICT_FRAME) {
                pwt->luma_weight[16 + 2 * i][list][0] = pwt->luma_weight[16 + 2 * i + 1][list][0] = pwt->luma_weight[i][list][0];
                pwt->luma_weight[16 + 2 * i][list][1] = pwt->luma_weight[16 + 2 * i + 1][list][1] = pwt->luma_weight[i][list][1];
                if (sps->chroma_format_idc) {
                    for (j = 0; j < 2; j++) {
                        pwt->chroma_weight[16 + 2 * i][list][j][0] = pwt->chroma_weight[16 + 2 * i + 1][list][j][0] = pwt->chroma_weight[i][list][j][0];
                        pwt->chroma_weight[16 + 2 * i][list][j][1] = pwt->chroma_weight[16 + 2 * i + 1][list][j][1] = pwt->chroma_weight[i][list][j][1];
                    }
                }
            }
        }
        if (slice_type_nos != AV_PICTURE_TYPE_B)
            break;
    }
    pwt->use_weight = pwt->use_weight || pwt->use_weight_chroma;
    return 0;
out_range_weight:
    avpriv_request_sample(logctx, "Out of range weight");
    return AVERROR_INVALIDDATA;
}

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra4x4_pred_mode(int8_t *pred_mode_cache, void *logctx,
                                     int top_samples_available, int left_samples_available)
{
    static const int8_t top[12] = {
        -1, 0, LEFT_DC_PRED, -1, -1, -1, -1, -1, 0
    };
    static const int8_t left[12] = {
        0, -1, TOP_DC_PRED, 0, -1, -1, -1, 0, -1, DC_128_PRED
    };
    int i;

    if (!(top_samples_available & 0x8000)) {
        for (i = 0; i < 4; i++) {
            int status = top[pred_mode_cache[scan8[0] + i]];
            if (status < 0) {
                av_log(logctx, AV_LOG_ERROR,
                       "top block unavailable for requested intra mode %d\n",
                       status);
                return AVERROR_INVALIDDATA;
            } else if (status) {
                pred_mode_cache[scan8[0] + i] = status;
            }
        }
    }

    if ((left_samples_available & 0x8888) != 0x8888) {
        static const int mask[4] = { 0x8000, 0x2000, 0x80, 0x20 };
        for (i = 0; i < 4; i++)
            if (!(left_samples_available & mask[i])) {
                int status = left[pred_mode_cache[scan8[0] + 8 * i]];
                if (status < 0) {
                    av_log(logctx, AV_LOG_ERROR,
                           "left block unavailable for requested intra4x4 mode %d\n",
                           status);
                    return AVERROR_INVALIDDATA;
                } else if (status) {
                    pred_mode_cache[scan8[0] + 8 * i] = status;
                }
            }
    }

    return 0;
}

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra_pred_mode(void *logctx, int top_samples_available,
                                  int left_samples_available,
                                  int mode, int is_chroma)
{
    static const int8_t top[4]  = { LEFT_DC_PRED8x8, 1, -1, -1 };
    static const int8_t left[5] = { TOP_DC_PRED8x8, -1,  2, -1, DC_128_PRED8x8 };

    if (mode > 3U) {
        av_log(logctx, AV_LOG_ERROR,
               "out of range intra chroma pred mode\n");
        return AVERROR_INVALIDDATA;
    }

    if (!(top_samples_available & 0x8000)) {
        mode = top[mode];
        if (mode < 0) {
            av_log(logctx, AV_LOG_ERROR,
                   "top block unavailable for requested intra mode\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if ((left_samples_available & 0x8080) != 0x8080) {
        mode = left[mode];
        if (mode < 0) {
            av_log(logctx, AV_LOG_ERROR,
                   "left block unavailable for requested intra mode\n");
            return AVERROR_INVALIDDATA;
        }
        if (is_chroma && (left_samples_available & 0x8080)) {
            // mad cow disease mode, aka MBAFF + constrained_intra_pred
            mode = ALZHEIMER_DC_L0T_PRED8x8 +
                   (!(left_samples_available & 0x8000)) +
                   2 * (mode == DC_128_PRED8x8);
        }
    }

    return mode;
}

int ff_h264_parse_ref_count(int *plist_count, int ref_count[2],
                            GetBitContext *gb, const PPS *pps,
                            int slice_type_nos, int picture_structure, void *logctx)
{
    int list_count;
    int num_ref_idx_active_override_flag;

    // set defaults, might be overridden a few lines later
    ref_count[0] = pps->ref_count[0];
    ref_count[1] = pps->ref_count[1];

    if (slice_type_nos != AV_PICTURE_TYPE_I) {
        unsigned max[2];
        max[0] = max[1] = picture_structure == PICT_FRAME ? 15 : 31;

        num_ref_idx_active_override_flag = get_bits1(gb);

        if (num_ref_idx_active_override_flag) {
            ref_count[0] = get_ue_golomb(gb) + 1;
            if (slice_type_nos == AV_PICTURE_TYPE_B) {
                ref_count[1] = get_ue_golomb(gb) + 1;
            } else
                // full range is spec-ok in this case, even for frames
                ref_count[1] = 1;
        }

        if (ref_count[0] - 1 > max[0] || ref_count[1] - 1 > max[1]) {
            av_log(logctx, AV_LOG_ERROR, "reference overflow %u > %u or %u > %u\n",
                   ref_count[0] - 1, max[0], ref_count[1] - 1, max[1]);
            ref_count[0] = ref_count[1] = 0;
            *plist_count = 0;
            goto fail;
        }

        if (slice_type_nos == AV_PICTURE_TYPE_B)
            list_count = 2;
        else
            list_count = 1;
    } else {
        list_count   = 0;
        ref_count[0] = ref_count[1] = 0;
    }

    *plist_count = list_count;

    return 0;
fail:
    *plist_count = 0;
    ref_count[0] = 0;
    ref_count[1] = 0;
    return AVERROR_INVALIDDATA;
}

int ff_h264_init_poc(int pic_field_poc[2], int *pic_poc,
                     const SPS *sps, H264POCContext *pc,
                     int picture_structure, int nal_ref_idc)
{
    const int max_frame_num = 1 << sps->log2_max_frame_num;
    int64_t field_poc[2];

    pc->frame_num_offset = pc->prev_frame_num_offset;
    if (pc->frame_num < pc->prev_frame_num)
        pc->frame_num_offset += max_frame_num;

    if (sps->poc_type == 0) {
        const int max_poc_lsb = 1 << sps->log2_max_poc_lsb;

        if (pc->poc_lsb < pc->prev_poc_lsb &&
            pc->prev_poc_lsb - pc->poc_lsb >= max_poc_lsb / 2)
            pc->poc_msb = pc->prev_poc_msb + max_poc_lsb;
        else if (pc->poc_lsb > pc->prev_poc_lsb &&
                 pc->prev_poc_lsb - pc->poc_lsb < -max_poc_lsb / 2)
            pc->poc_msb = pc->prev_poc_msb - max_poc_lsb;
        else
            pc->poc_msb = pc->prev_poc_msb;
        field_poc[0] =
        field_poc[1] = pc->poc_msb + pc->poc_lsb;
        if (picture_structure == PICT_FRAME)
            field_poc[1] += pc->delta_poc_bottom;
    } else if (sps->poc_type == 1) {
        int abs_frame_num, expected_delta_per_poc_cycle, expectedpoc;
        int i;

        if (sps->poc_cycle_length != 0)
            abs_frame_num = pc->frame_num_offset + pc->frame_num;
        else
            abs_frame_num = 0;

        if (nal_ref_idc == 0 && abs_frame_num > 0)
            abs_frame_num--;

        expected_delta_per_poc_cycle = 0;
        for (i = 0; i < sps->poc_cycle_length; i++)
            // FIXME integrate during sps parse
            expected_delta_per_poc_cycle += sps->offset_for_ref_frame[i];

        if (abs_frame_num > 0) {
            int poc_cycle_cnt          = (abs_frame_num - 1) / sps->poc_cycle_length;
            int frame_num_in_poc_cycle = (abs_frame_num - 1) % sps->poc_cycle_length;

            expectedpoc = poc_cycle_cnt * expected_delta_per_poc_cycle;
            for (i = 0; i <= frame_num_in_poc_cycle; i++)
                expectedpoc = expectedpoc + sps->offset_for_ref_frame[i];
        } else
            expectedpoc = 0;

        if (nal_ref_idc == 0)
            expectedpoc = expectedpoc + sps->offset_for_non_ref_pic;

        field_poc[0] = expectedpoc + pc->delta_poc[0];
        field_poc[1] = field_poc[0] + sps->offset_for_top_to_bottom_field;

        if (picture_structure == PICT_FRAME)
            field_poc[1] += pc->delta_poc[1];
    } else {
        int poc = 2 * (pc->frame_num_offset + pc->frame_num);

        if (!nal_ref_idc)
            poc--;

        field_poc[0] = poc;
        field_poc[1] = poc;
    }

    if (   field_poc[0] != (int)field_poc[0]
        || field_poc[1] != (int)field_poc[1])
        return AVERROR_INVALIDDATA;

    if (picture_structure != PICT_BOTTOM_FIELD)
        pic_field_poc[0] = field_poc[0];
    if (picture_structure != PICT_TOP_FIELD)
        pic_field_poc[1] = field_poc[1];
    *pic_poc = FFMIN(pic_field_poc[0], pic_field_poc[1]);

    return 0;
}

static int decode_extradata_ps(const uint8_t *data, int size, H264ParamSets *ps,
                               int is_avc, void *logctx)
{
    H2645Packet pkt = { 0 };
    int i, ret = 0;

    ret = ff_h2645_packet_split(&pkt, data, size, logctx, is_avc, 2, AV_CODEC_ID_H264, 1);
    if (ret < 0) {
        ret = 0;
        goto fail;
    }

    for (i = 0; i < pkt.nb_nals; i++) {
        H2645NAL *nal = &pkt.nals[i];
        switch (nal->type) {
        case H264_NAL_SPS:
            ret = ff_h264_decode_seq_parameter_set(&nal->gb, logctx, ps, 0);
            if (ret < 0)
                goto fail;
            break;
        case H264_NAL_PPS:
            ret = ff_h264_decode_picture_parameter_set(&nal->gb, logctx, ps,
                                                       nal->size_bits);
            if (ret < 0)
                goto fail;
            break;
        default:
            av_log(logctx, AV_LOG_VERBOSE, "Ignoring NAL type %d in extradata\n",
                   nal->type);
            break;
        }
    }

fail:
    ff_h2645_packet_uninit(&pkt);
    return ret;
}

/* There are (invalid) samples in the wild with mp4-style extradata, where the
 * parameter sets are stored unescaped (i.e. as RBSP).
 * This function catches the parameter set decoding failure and tries again
 * after escaping it */
static int decode_extradata_ps_mp4(const uint8_t *buf, int buf_size, H264ParamSets *ps,
                                   int err_recognition, void *logctx)
{
    int ret;

    ret = decode_extradata_ps(buf, buf_size, ps, 1, logctx);
    if (ret < 0 && !(err_recognition & AV_EF_EXPLODE)) {
        GetByteContext gbc;
        PutByteContext pbc;
        uint8_t *escaped_buf;
        int escaped_buf_size;

        av_log(logctx, AV_LOG_WARNING,
               "SPS decoding failure, trying again after escaping the NAL\n");

        if (buf_size / 2 >= (INT16_MAX - AV_INPUT_BUFFER_PADDING_SIZE) / 3)
            return AVERROR(ERANGE);
        escaped_buf_size = buf_size * 3 / 2 + AV_INPUT_BUFFER_PADDING_SIZE;
        escaped_buf = av_mallocz(escaped_buf_size);
        if (!escaped_buf)
            return AVERROR(ENOMEM);

        bytestream2_init(&gbc, buf, buf_size);
        bytestream2_init_writer(&pbc, escaped_buf, escaped_buf_size);

        while (bytestream2_get_bytes_left(&gbc)) {
            if (bytestream2_get_bytes_left(&gbc) >= 3 &&
                bytestream2_peek_be24(&gbc) <= 3) {
                bytestream2_put_be24(&pbc, 3);
                bytestream2_skip(&gbc, 2);
            } else
                bytestream2_put_byte(&pbc, bytestream2_get_byte(&gbc));
        }

        escaped_buf_size = bytestream2_tell_p(&pbc);
        AV_WB16(escaped_buf, escaped_buf_size - 2);

        (void)decode_extradata_ps(escaped_buf, escaped_buf_size, ps, 1, logctx);
        // lorex.mp4 decodes ok even with extradata decoding failing
        av_freep(&escaped_buf);
    }

    return 0;
}

int ff_h264_decode_extradata(const uint8_t *data, int size, H264ParamSets *ps,
                             int *is_avc, int *nal_length_size,
                             int err_recognition, void *logctx)
{
    int ret;

    if (!data || size <= 0)
        return -1;

    if (data[0] == 1) {
        int i, cnt, nalsize;
        const uint8_t *p = data;

        *is_avc = 1;

        if (size < 7) {
            av_log(logctx, AV_LOG_ERROR, "avcC %d too short\n", size);
            return AVERROR_INVALIDDATA;
        }

        // Decode sps from avcC
        cnt = *(p + 5) & 0x1f; // Number of sps
        p  += 6;
        for (i = 0; i < cnt; i++) {
            nalsize = AV_RB16(p) + 2;
            if (nalsize > size - (p - data))
                return AVERROR_INVALIDDATA;
            ret = decode_extradata_ps_mp4(p, nalsize, ps, err_recognition, logctx);
            if (ret < 0) {
                av_log(logctx, AV_LOG_ERROR,
                       "Decoding sps %d from avcC failed\n", i);
                return ret;
            }
            p += nalsize;
        }
        // Decode pps from avcC
        cnt = *(p++); // Number of pps
        for (i = 0; i < cnt; i++) {
            nalsize = AV_RB16(p) + 2;
            if (nalsize > size - (p - data))
                return AVERROR_INVALIDDATA;
            ret = decode_extradata_ps_mp4(p, nalsize, ps, err_recognition, logctx);
            if (ret < 0) {
                av_log(logctx, AV_LOG_ERROR,
                       "Decoding pps %d from avcC failed\n", i);
                return ret;
            }
            p += nalsize;
        }
        // Store right nal length size that will be used to parse all other nals
        *nal_length_size = (data[4] & 0x03) + 1;
    } else {
        *is_avc = 0;
        ret = decode_extradata_ps(data, size, ps, 0, logctx);
        if (ret < 0)
            return ret;
    }
    return size;
}

/**
 * Compute profile from profile_idc and constraint_set?_flags.
 *
 * @param sps SPS
 *
 * @return profile as defined by FF_PROFILE_H264_*
 */
int ff_h264_get_profile(const SPS *sps)
{
    int profile = sps->profile_idc;

    switch (sps->profile_idc) {
    case FF_PROFILE_H264_BASELINE:
        // constraint_set1_flag set to 1
        profile |= (sps->constraint_set_flags & 1 << 1) ? FF_PROFILE_H264_CONSTRAINED : 0;
        break;
    case FF_PROFILE_H264_HIGH_10:
    case FF_PROFILE_H264_HIGH_422:
    case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
        // constraint_set3_flag set to 1
        profile |= (sps->constraint_set_flags & 1 << 3) ? FF_PROFILE_H264_INTRA : 0;
        break;
    }

    return profile;
}
