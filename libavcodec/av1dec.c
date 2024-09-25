/*
 * AV1 video decoder
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

#include "config_components.h"

#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/film_grain_params.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "av1_parse.h"
#include "av1dec.h"
#include "atsc_a53.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "hwaccel_internal.h"
#include "internal.h"
#include "itut35.h"
#include "hwconfig.h"
#include "profiles.h"
#include "progressframe.h"
#include "refstruct.h"

/**< same with Div_Lut defined in spec 7.11.3.7 */
static const uint16_t div_lut[AV1_DIV_LUT_NUM] = {
  16384, 16320, 16257, 16194, 16132, 16070, 16009, 15948, 15888, 15828, 15768,
  15709, 15650, 15592, 15534, 15477, 15420, 15364, 15308, 15252, 15197, 15142,
  15087, 15033, 14980, 14926, 14873, 14821, 14769, 14717, 14665, 14614, 14564,
  14513, 14463, 14413, 14364, 14315, 14266, 14218, 14170, 14122, 14075, 14028,
  13981, 13935, 13888, 13843, 13797, 13752, 13707, 13662, 13618, 13574, 13530,
  13487, 13443, 13400, 13358, 13315, 13273, 13231, 13190, 13148, 13107, 13066,
  13026, 12985, 12945, 12906, 12866, 12827, 12788, 12749, 12710, 12672, 12633,
  12596, 12558, 12520, 12483, 12446, 12409, 12373, 12336, 12300, 12264, 12228,
  12193, 12157, 12122, 12087, 12053, 12018, 11984, 11950, 11916, 11882, 11848,
  11815, 11782, 11749, 11716, 11683, 11651, 11619, 11586, 11555, 11523, 11491,
  11460, 11429, 11398, 11367, 11336, 11305, 11275, 11245, 11215, 11185, 11155,
  11125, 11096, 11067, 11038, 11009, 10980, 10951, 10923, 10894, 10866, 10838,
  10810, 10782, 10755, 10727, 10700, 10673, 10645, 10618, 10592, 10565, 10538,
  10512, 10486, 10460, 10434, 10408, 10382, 10356, 10331, 10305, 10280, 10255,
  10230, 10205, 10180, 10156, 10131, 10107, 10082, 10058, 10034, 10010, 9986,
  9963,  9939,  9916,  9892,  9869,  9846,  9823,  9800,  9777,  9754,  9732,
  9709,  9687,  9664,  9642,  9620,  9598,  9576,  9554,  9533,  9511,  9489,
  9468,  9447,  9425,  9404,  9383,  9362,  9341,  9321,  9300,  9279,  9259,
  9239,  9218,  9198,  9178,  9158,  9138,  9118,  9098,  9079,  9059,  9039,
  9020,  9001,  8981,  8962,  8943,  8924,  8905,  8886,  8867,  8849,  8830,
  8812,  8793,  8775,  8756,  8738,  8720,  8702,  8684,  8666,  8648,  8630,
  8613,  8595,  8577,  8560,  8542,  8525,  8508,  8490,  8473,  8456,  8439,
  8422,  8405,  8389,  8372,  8355,  8339,  8322,  8306,  8289,  8273,  8257,
  8240,  8224,  8208,  8192
};

static uint32_t inverse_recenter(int r, uint32_t v)
{
    if (v > 2 * r)
        return v;
    else if (v & 1)
        return r - ((v + 1) >> 1);
    else
        return r + (v >> 1);
}

static uint32_t decode_unsigned_subexp_with_ref(uint32_t sub_exp,
                                                int mx, int r)
{
    if ((r << 1) <= mx) {
        return inverse_recenter(r, sub_exp);
    } else {
        return mx - 1 - inverse_recenter(mx - 1 - r, sub_exp);
    }
}

static int32_t decode_signed_subexp_with_ref(uint32_t sub_exp, int low,
                                             int high, int r)
{
    int32_t x = decode_unsigned_subexp_with_ref(sub_exp, high - low, r - low);
    return x + low;
}

static void read_global_param(AV1DecContext *s, int type, int ref, int idx)
{
    uint8_t primary_frame, prev_frame;
    uint32_t abs_bits, prec_bits, round, prec_diff, sub, mx;
    int32_t r, prev_gm_param;

    primary_frame = s->raw_frame_header->primary_ref_frame;
    prev_frame = s->raw_frame_header->ref_frame_idx[primary_frame];
    abs_bits = AV1_GM_ABS_ALPHA_BITS;
    prec_bits = AV1_GM_ALPHA_PREC_BITS;

    /* setup_past_independence() sets PrevGmParams to default values. We can
     * simply point to the current's frame gm_params as they will be initialized
     * with defaults at this point.
     */
    if (s->raw_frame_header->primary_ref_frame == AV1_PRIMARY_REF_NONE)
        prev_gm_param = s->cur_frame.gm_params[ref][idx];
    else
        prev_gm_param = s->ref[prev_frame].gm_params[ref][idx];

    if (idx < 2) {
        if (type == AV1_WARP_MODEL_TRANSLATION) {
            abs_bits = AV1_GM_ABS_TRANS_ONLY_BITS -
                !s->raw_frame_header->allow_high_precision_mv;
            prec_bits = AV1_GM_TRANS_ONLY_PREC_BITS -
                !s->raw_frame_header->allow_high_precision_mv;
        } else {
            abs_bits = AV1_GM_ABS_TRANS_BITS;
            prec_bits = AV1_GM_TRANS_PREC_BITS;
        }
    }
    round = (idx % 3) == 2 ? (1 << AV1_WARPEDMODEL_PREC_BITS) : 0;
    prec_diff = AV1_WARPEDMODEL_PREC_BITS - prec_bits;
    sub = (idx % 3) == 2 ? (1 << prec_bits) : 0;
    mx = 1 << abs_bits;
    r = (prev_gm_param >> prec_diff) - sub;

    s->cur_frame.gm_params[ref][idx] =
        (decode_signed_subexp_with_ref(s->raw_frame_header->gm_params[ref][idx],
                                       -mx, mx + 1, r) << prec_diff) + round;
}

static uint64_t round_two(uint64_t x, uint16_t n)
{
    if (n == 0)
        return x;
    return ((x + ((uint64_t)1 << (n - 1))) >> n);
}

static int64_t round_two_signed(int64_t x, uint16_t n)
{
    return ((x<0) ? -((int64_t)round_two(-x, n)) : (int64_t)round_two(x, n));
}

/**
 * Resolve divisor process.
 * see spec 7.11.3.7
 */
static int16_t resolve_divisor(uint32_t d, uint16_t *shift)
{
    int32_t e, f;

    *shift = av_log2(d);
    e = d - (1 << (*shift));
    if (*shift > AV1_DIV_LUT_BITS)
        f = round_two(e, *shift - AV1_DIV_LUT_BITS);
    else
        f = e << (AV1_DIV_LUT_BITS - (*shift));

    *shift += AV1_DIV_LUT_PREC_BITS;

    return div_lut[f];
}

/**
 * check if global motion params is valid.
 * see spec 7.11.3.6
 */
static uint8_t get_shear_params_valid(AV1DecContext *s, int idx)
{
    int16_t alpha, beta, gamma, delta, divf, divs;
    int64_t v, w;
    int32_t *param = &s->cur_frame.gm_params[idx][0];
    if (param[2] <= 0)
        return 0;

    alpha = av_clip_int16(param[2] - (1 << AV1_WARPEDMODEL_PREC_BITS));
    beta  = av_clip_int16(param[3]);
    divf  = resolve_divisor(abs(param[2]), &divs);
    v     = (int64_t)param[4] * (1 << AV1_WARPEDMODEL_PREC_BITS);
    w     = (int64_t)param[3] * param[4];
    gamma = av_clip_int16((int)round_two_signed((v * divf), divs));
    delta = av_clip_int16(param[5] - (int)round_two_signed((w * divf), divs) - (1 << AV1_WARPEDMODEL_PREC_BITS));

    alpha = round_two_signed(alpha, AV1_WARP_PARAM_REDUCE_BITS) << AV1_WARP_PARAM_REDUCE_BITS;
    beta  = round_two_signed(beta,  AV1_WARP_PARAM_REDUCE_BITS) << AV1_WARP_PARAM_REDUCE_BITS;
    gamma = round_two_signed(gamma, AV1_WARP_PARAM_REDUCE_BITS) << AV1_WARP_PARAM_REDUCE_BITS;
    delta = round_two_signed(delta, AV1_WARP_PARAM_REDUCE_BITS) << AV1_WARP_PARAM_REDUCE_BITS;

    if ((4 * abs(alpha) + 7 * abs(beta)) >= (1 << AV1_WARPEDMODEL_PREC_BITS) ||
        (4 * abs(gamma) + 4 * abs(delta)) >= (1 << AV1_WARPEDMODEL_PREC_BITS))
        return 0;

    return 1;
}

/**
* update gm type/params, since cbs already implemented part of this function,
* so we don't need to full implement spec.
*/
static void global_motion_params(AV1DecContext *s)
{
    const AV1RawFrameHeader *header = s->raw_frame_header;
    int type, ref;

    for (ref = AV1_REF_FRAME_LAST; ref <= AV1_REF_FRAME_ALTREF; ref++) {
        s->cur_frame.gm_type[ref] = AV1_WARP_MODEL_IDENTITY;
        for (int i = 0; i < 6; i++)
            s->cur_frame.gm_params[ref][i] = (i % 3 == 2) ?
                                             1 << AV1_WARPEDMODEL_PREC_BITS : 0;
    }
    if (header->frame_type == AV1_FRAME_KEY ||
        header->frame_type == AV1_FRAME_INTRA_ONLY)
        return;

    for (ref = AV1_REF_FRAME_LAST; ref <= AV1_REF_FRAME_ALTREF; ref++) {
        if (header->is_global[ref]) {
            if (header->is_rot_zoom[ref]) {
                type = AV1_WARP_MODEL_ROTZOOM;
            } else {
                type = header->is_translation[ref] ? AV1_WARP_MODEL_TRANSLATION
                                                   : AV1_WARP_MODEL_AFFINE;
            }
        } else {
            type = AV1_WARP_MODEL_IDENTITY;
        }
        s->cur_frame.gm_type[ref] = type;

        if (type >= AV1_WARP_MODEL_ROTZOOM) {
            read_global_param(s, type, ref, 2);
            read_global_param(s, type, ref, 3);
            if (type == AV1_WARP_MODEL_AFFINE) {
                read_global_param(s, type, ref, 4);
                read_global_param(s, type, ref, 5);
            } else {
                s->cur_frame.gm_params[ref][4] = -s->cur_frame.gm_params[ref][3];
                s->cur_frame.gm_params[ref][5] = s->cur_frame.gm_params[ref][2];
            }
        }
        if (type >= AV1_WARP_MODEL_TRANSLATION) {
            read_global_param(s, type, ref, 0);
            read_global_param(s, type, ref, 1);
        }
        if (type <= AV1_WARP_MODEL_AFFINE) {
            s->cur_frame.gm_invalid[ref] = !get_shear_params_valid(s, ref);
        }
    }
}

static int get_relative_dist(const AV1RawSequenceHeader *seq,
                             unsigned int a, unsigned int b)
{
    unsigned int diff = a - b;
    unsigned int m = 1 << seq->order_hint_bits_minus_1;
    return (diff & (m - 1)) - (diff & m);
}

static void skip_mode_params(AV1DecContext *s)
{
    const AV1RawFrameHeader *header = s->raw_frame_header;
    const AV1RawSequenceHeader *seq = s->raw_seq;

    int forward_idx,  backward_idx;
    int forward_hint, backward_hint;
    int second_forward_idx, second_forward_hint;
    int ref_hint, dist, i;

    if (header->frame_type == AV1_FRAME_KEY ||
        header->frame_type == AV1_FRAME_INTRA_ONLY ||
        !header->reference_select || !seq->enable_order_hint)
        return;

    forward_idx  = -1;
    backward_idx = -1;
    for (i = 0; i < AV1_REFS_PER_FRAME; i++) {
        if (!s->ref[header->ref_frame_idx[i]].raw_frame_header)
            return;
        ref_hint = s->ref[header->ref_frame_idx[i]].raw_frame_header->order_hint;
        dist = get_relative_dist(seq, ref_hint, header->order_hint);
        if (dist < 0) {
            if (forward_idx < 0 ||
                get_relative_dist(seq, ref_hint, forward_hint) > 0) {
                forward_idx  = i;
                forward_hint = ref_hint;
            }
        } else if (dist > 0) {
            if (backward_idx < 0 ||
                get_relative_dist(seq, ref_hint, backward_hint) < 0) {
                backward_idx  = i;
                backward_hint = ref_hint;
            }
        }
    }

    if (forward_idx < 0) {
        return;
    } else if (backward_idx >= 0) {
        s->cur_frame.skip_mode_frame_idx[0] =
            AV1_REF_FRAME_LAST + FFMIN(forward_idx, backward_idx);
        s->cur_frame.skip_mode_frame_idx[1] =
            AV1_REF_FRAME_LAST + FFMAX(forward_idx, backward_idx);
        return;
    }

    second_forward_idx = -1;
    for (i = 0; i < AV1_REFS_PER_FRAME; i++) {
        ref_hint = s->ref[header->ref_frame_idx[i]].raw_frame_header->order_hint;
        if (get_relative_dist(seq, ref_hint, forward_hint) < 0) {
            if (second_forward_idx < 0 ||
                get_relative_dist(seq, ref_hint, second_forward_hint) > 0) {
                second_forward_idx  = i;
                second_forward_hint = ref_hint;
            }
        }
    }

    if (second_forward_idx < 0)
        return;

    s->cur_frame.skip_mode_frame_idx[0] =
        AV1_REF_FRAME_LAST + FFMIN(forward_idx, second_forward_idx);
    s->cur_frame.skip_mode_frame_idx[1] =
        AV1_REF_FRAME_LAST + FFMAX(forward_idx, second_forward_idx);
}

static void coded_lossless_param(AV1DecContext *s)
{
    const AV1RawFrameHeader *header = s->raw_frame_header;
    int i;

    if (header->delta_q_y_dc || header->delta_q_u_ac ||
        header->delta_q_u_dc || header->delta_q_v_ac ||
        header->delta_q_v_dc) {
        s->cur_frame.coded_lossless = 0;
        return;
    }

    s->cur_frame.coded_lossless = 1;
    for (i = 0; i < AV1_MAX_SEGMENTS; i++) {
        int qindex;
        if (header->feature_enabled[i][AV1_SEG_LVL_ALT_Q]) {
            qindex = (header->base_q_idx +
                      header->feature_value[i][AV1_SEG_LVL_ALT_Q]);
        } else {
            qindex = header->base_q_idx;
        }
        qindex = av_clip_uintp2(qindex, 8);

        if (qindex) {
            s->cur_frame.coded_lossless = 0;
            return;
        }
    }
}

static void order_hint_info(AV1DecContext *s)
{
    const AV1RawFrameHeader *header = s->raw_frame_header;
    const AV1RawSequenceHeader *seq = s->raw_seq;
    AV1Frame *frame = &s->cur_frame;

    frame->order_hint = header->order_hint;

    for (int i = 0; i < AV1_REFS_PER_FRAME; i++) {
        int ref_name = i + AV1_REF_FRAME_LAST;
        int ref_slot = header->ref_frame_idx[i];
        int ref_order_hint = s->ref[ref_slot].order_hint;

        frame->order_hints[ref_name] = ref_order_hint;
        if (!seq->enable_order_hint) {
            frame->ref_frame_sign_bias[ref_name] = 0;
        } else {
            frame->ref_frame_sign_bias[ref_name] =
                get_relative_dist(seq, ref_order_hint,
                                  frame->order_hint) > 0;
        }
    }
}

static void load_grain_params(AV1DecContext *s)
{
    const AV1RawFrameHeader *header = s->raw_frame_header;
    const AV1RawFilmGrainParams *film_grain = &header->film_grain, *src;
    AV1RawFilmGrainParams *dst = &s->cur_frame.film_grain;

    if (!film_grain->apply_grain)
        return;

    if (film_grain->update_grain) {
        memcpy(dst, film_grain, sizeof(*dst));
        return;
    }

    src = &s->ref[film_grain->film_grain_params_ref_idx].film_grain;

    memcpy(dst, src, sizeof(*dst));
    dst->grain_seed = film_grain->grain_seed;
}

static int init_tile_data(AV1DecContext *s)

{
    int cur_tile_num =
        s->raw_frame_header->tile_cols * s->raw_frame_header->tile_rows;
    if (s->tile_num < cur_tile_num) {
        int ret = av_reallocp_array(&s->tile_group_info, cur_tile_num,
                                    sizeof(TileGroupInfo));
        if (ret < 0) {
            s->tile_num = 0;
            return ret;
        }
    }
    s->tile_num = cur_tile_num;

    return 0;
}

static int get_tiles_info(AVCodecContext *avctx, const AV1RawTileGroup *tile_group)
{
    AV1DecContext *s = avctx->priv_data;
    GetByteContext gb;
    uint16_t tile_num, tile_row, tile_col;
    uint32_t size = 0, size_bytes = 0;

    bytestream2_init(&gb, tile_group->tile_data.data,
                     tile_group->tile_data.data_size);
    s->tg_start = tile_group->tg_start;
    s->tg_end = tile_group->tg_end;

    for (tile_num = tile_group->tg_start; tile_num <= tile_group->tg_end; tile_num++) {
        tile_row = tile_num / s->raw_frame_header->tile_cols;
        tile_col = tile_num % s->raw_frame_header->tile_cols;

        if (tile_num == tile_group->tg_end) {
            s->tile_group_info[tile_num].tile_size = bytestream2_get_bytes_left(&gb);
            s->tile_group_info[tile_num].tile_offset = bytestream2_tell(&gb);
            s->tile_group_info[tile_num].tile_row = tile_row;
            s->tile_group_info[tile_num].tile_column = tile_col;
            return 0;
        }
        size_bytes = s->raw_frame_header->tile_size_bytes_minus1 + 1;
        if (bytestream2_get_bytes_left(&gb) < size_bytes)
            return AVERROR_INVALIDDATA;
        size = 0;
        for (int i = 0; i < size_bytes; i++)
            size |= bytestream2_get_byteu(&gb) << 8 * i;
        if (bytestream2_get_bytes_left(&gb) <= size)
            return AVERROR_INVALIDDATA;
        size++;

        s->tile_group_info[tile_num].tile_size = size;
        s->tile_group_info[tile_num].tile_offset = bytestream2_tell(&gb);
        s->tile_group_info[tile_num].tile_row = tile_row;
        s->tile_group_info[tile_num].tile_column = tile_col;

        bytestream2_skipu(&gb, size);
    }

    return 0;

}

static enum AVPixelFormat get_sw_pixel_format(void *logctx,
                                              const AV1RawSequenceHeader *seq)
{
    int bit_depth;
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;

    if (seq->seq_profile == 2 && seq->color_config.high_bitdepth)
        bit_depth = seq->color_config.twelve_bit ? 12 : 10;
    else if (seq->seq_profile <= 2)
        bit_depth = seq->color_config.high_bitdepth ? 10 : 8;
    else {
        av_log(logctx, AV_LOG_ERROR,
               "Unknown AV1 profile %d.\n", seq->seq_profile);
        return AV_PIX_FMT_NONE;
    }

    if (!seq->color_config.mono_chrome) {
        // 4:4:4 x:0 y:0, 4:2:2 x:1 y:0, 4:2:0 x:1 y:1
        if (seq->color_config.subsampling_x == 0 &&
            seq->color_config.subsampling_y == 0) {
            if (bit_depth == 8)
                pix_fmt = AV_PIX_FMT_YUV444P;
            else if (bit_depth == 10)
                pix_fmt = AV_PIX_FMT_YUV444P10;
            else if (bit_depth == 12)
                pix_fmt = AV_PIX_FMT_YUV444P12;
            else
                av_assert0(0);
        } else if (seq->color_config.subsampling_x == 1 &&
                   seq->color_config.subsampling_y == 0) {
            if (bit_depth == 8)
                pix_fmt = AV_PIX_FMT_YUV422P;
            else if (bit_depth == 10)
                pix_fmt = AV_PIX_FMT_YUV422P10;
            else if (bit_depth == 12)
                pix_fmt = AV_PIX_FMT_YUV422P12;
            else
                av_assert0(0);
        } else if (seq->color_config.subsampling_x == 1 &&
                   seq->color_config.subsampling_y == 1) {
            if (bit_depth == 8)
                pix_fmt = AV_PIX_FMT_YUV420P;
            else if (bit_depth == 10)
                pix_fmt = AV_PIX_FMT_YUV420P10;
            else if (bit_depth == 12)
                pix_fmt = AV_PIX_FMT_YUV420P12;
            else
                av_assert0(0);
        }
    } else {
        if (bit_depth == 8)
            pix_fmt = AV_PIX_FMT_GRAY8;
        else if (bit_depth == 10)
            pix_fmt = AV_PIX_FMT_GRAY10;
        else if (bit_depth == 12)
            pix_fmt = AV_PIX_FMT_GRAY12;
        else
            av_assert0(0);
    }

    return pix_fmt;
}

static int get_pixel_format(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    const AV1RawSequenceHeader *seq = s->raw_seq;
    int ret;
    enum AVPixelFormat pix_fmt = get_sw_pixel_format(avctx, seq);
#define HWACCEL_MAX (CONFIG_AV1_DXVA2_HWACCEL + \
                     CONFIG_AV1_D3D11VA_HWACCEL * 2 + \
                     CONFIG_AV1_D3D12VA_HWACCEL + \
                     CONFIG_AV1_NVDEC_HWACCEL + \
                     CONFIG_AV1_VAAPI_HWACCEL + \
                     CONFIG_AV1_VDPAU_HWACCEL + \
                     CONFIG_AV1_VIDEOTOOLBOX_HWACCEL + \
                     CONFIG_AV1_VULKAN_HWACCEL)
    enum AVPixelFormat pix_fmts[HWACCEL_MAX + 2], *fmtp = pix_fmts;

    if (pix_fmt == AV_PIX_FMT_NONE)
        return -1;

    switch (pix_fmt) {
    case AV_PIX_FMT_YUV420P:
#if CONFIG_AV1_DXVA2_HWACCEL
        *fmtp++ = AV_PIX_FMT_DXVA2_VLD;
#endif
#if CONFIG_AV1_D3D11VA_HWACCEL
        *fmtp++ = AV_PIX_FMT_D3D11VA_VLD;
        *fmtp++ = AV_PIX_FMT_D3D11;
#endif
#if CONFIG_AV1_D3D12VA_HWACCEL
        *fmtp++ = AV_PIX_FMT_D3D12;
#endif
#if CONFIG_AV1_NVDEC_HWACCEL
        *fmtp++ = AV_PIX_FMT_CUDA;
#endif
#if CONFIG_AV1_VAAPI_HWACCEL
        *fmtp++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_AV1_VDPAU_HWACCEL
        *fmtp++ = AV_PIX_FMT_VDPAU;
#endif
#if CONFIG_AV1_VIDEOTOOLBOX_HWACCEL
        *fmtp++ = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
#if CONFIG_AV1_VULKAN_HWACCEL
        *fmtp++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV420P10:
#if CONFIG_AV1_DXVA2_HWACCEL
        *fmtp++ = AV_PIX_FMT_DXVA2_VLD;
#endif
#if CONFIG_AV1_D3D11VA_HWACCEL
        *fmtp++ = AV_PIX_FMT_D3D11VA_VLD;
        *fmtp++ = AV_PIX_FMT_D3D11;
#endif
#if CONFIG_AV1_D3D12VA_HWACCEL
        *fmtp++ = AV_PIX_FMT_D3D12;
#endif
#if CONFIG_AV1_NVDEC_HWACCEL
        *fmtp++ = AV_PIX_FMT_CUDA;
#endif
#if CONFIG_AV1_VAAPI_HWACCEL
        *fmtp++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_AV1_VDPAU_HWACCEL
        *fmtp++ = AV_PIX_FMT_VDPAU;
#endif
#if CONFIG_AV1_VIDEOTOOLBOX_HWACCEL
        *fmtp++ = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
#if CONFIG_AV1_VULKAN_HWACCEL
        *fmtp++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV420P12:
#if CONFIG_AV1_VULKAN_HWACCEL
        *fmtp++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV422P:
#if CONFIG_AV1_VULKAN_HWACCEL
        *fmtp++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV422P10:
#if CONFIG_AV1_VULKAN_HWACCEL
        *fmtp++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV422P12:
#if CONFIG_AV1_VULKAN_HWACCEL
        *fmtp++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV444P:
#if CONFIG_AV1_VULKAN_HWACCEL
        *fmtp++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV444P10:
#if CONFIG_AV1_VULKAN_HWACCEL
        *fmtp++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_YUV444P12:
#if CONFIG_AV1_VULKAN_HWACCEL
        *fmtp++ = AV_PIX_FMT_VULKAN;
#endif
        break;
    case AV_PIX_FMT_GRAY8:
#if CONFIG_AV1_NVDEC_HWACCEL
        *fmtp++ = AV_PIX_FMT_CUDA;
#endif
        break;
    case AV_PIX_FMT_GRAY10:
#if CONFIG_AV1_NVDEC_HWACCEL
        *fmtp++ = AV_PIX_FMT_CUDA;
#endif
        break;
    }

    *fmtp++ = pix_fmt;
    *fmtp = AV_PIX_FMT_NONE;

    for (int i = 0; pix_fmts[i] != pix_fmt; i++)
        if (pix_fmts[i] == avctx->pix_fmt) {
            s->pix_fmt = pix_fmt;
            return 1;
        }

    ret = ff_get_format(avctx, pix_fmts);

    /**
     * check if the HW accel is inited correctly. If not, return un-implemented.
     * Since now the av1 decoder doesn't support native decode, if it will be
     * implemented in the future, need remove this check.
     */
    if (!avctx->hwaccel) {
        av_log(avctx, AV_LOG_ERROR, "Your platform doesn't support"
               " hardware accelerated AV1 decoding.\n");
        avctx->pix_fmt = AV_PIX_FMT_NONE;
        return AVERROR(ENOSYS);
    }

    s->pix_fmt = pix_fmt;
    avctx->pix_fmt = ret;

    av_log(avctx, AV_LOG_DEBUG, "AV1 decode get format: %s.\n",
           av_get_pix_fmt_name(avctx->pix_fmt));

    return 0;
}

static void av1_frame_unref(AV1Frame *f)
{
    ff_progress_frame_unref(&f->pf);
    ff_refstruct_unref(&f->hwaccel_picture_private);
    ff_refstruct_unref(&f->header_ref);
    f->raw_frame_header = NULL;
    f->spatial_id = f->temporal_id = 0;
    memset(f->skip_mode_frame_idx, 0,
           2 * sizeof(uint8_t));
    memset(&f->film_grain, 0, sizeof(f->film_grain));
    f->coded_lossless = 0;
}

static void av1_frame_replace(AV1Frame *dst, const AV1Frame *src)
{
    av_assert1(dst != src);

    ff_refstruct_replace(&dst->header_ref, src->header_ref);

    dst->raw_frame_header = src->raw_frame_header;

    ff_progress_frame_replace(&dst->pf, &src->pf);

    ff_refstruct_replace(&dst->hwaccel_picture_private,
                          src->hwaccel_picture_private);

    dst->spatial_id = src->spatial_id;
    dst->temporal_id = src->temporal_id;
    memcpy(dst->gm_invalid,
           src->gm_invalid,
           AV1_NUM_REF_FRAMES * sizeof(uint8_t));
    memcpy(dst->gm_type,
           src->gm_type,
           AV1_NUM_REF_FRAMES * sizeof(uint8_t));
    memcpy(dst->gm_params,
           src->gm_params,
           AV1_NUM_REF_FRAMES * 6 * sizeof(int32_t));
    memcpy(dst->skip_mode_frame_idx,
           src->skip_mode_frame_idx,
           2 * sizeof(uint8_t));
    memcpy(&dst->film_grain,
           &src->film_grain,
           sizeof(dst->film_grain));
    dst->coded_lossless = src->coded_lossless;

    dst->order_hint = src->order_hint;
    memcpy(dst->ref_frame_sign_bias, src->ref_frame_sign_bias,
           sizeof(dst->ref_frame_sign_bias));
    memcpy(dst->order_hints, src->order_hints,
           sizeof(dst->order_hints));

    dst->force_integer_mv = src->force_integer_mv;
}

static av_cold int av1_decode_free(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    AV1RawMetadataITUTT35 itut_t35;

    for (int i = 0; i < FF_ARRAY_ELEMS(s->ref); i++)
        av1_frame_unref(&s->ref[i]);
    av1_frame_unref(&s->cur_frame);
    av_buffer_unref(&s->seq_data_ref);
    ff_refstruct_unref(&s->seq_ref);
    ff_refstruct_unref(&s->header_ref);
    ff_refstruct_unref(&s->cll_ref);
    ff_refstruct_unref(&s->mdcv_ref);
    av_freep(&s->tile_group_info);

    while (s->itut_t35_fifo && av_fifo_read(s->itut_t35_fifo, &itut_t35, 1) >= 0)
        av_buffer_unref(&itut_t35.payload_ref);
    av_fifo_freep2(&s->itut_t35_fifo);

    ff_cbs_fragment_free(&s->current_obu);
    ff_cbs_close(&s->cbc);
    ff_dovi_ctx_unref(&s->dovi);

    return 0;
}

static int set_context_with_sequence(AVCodecContext *avctx,
                                     const AV1RawSequenceHeader *seq)
{
    int width = seq->max_frame_width_minus_1 + 1;
    int height = seq->max_frame_height_minus_1 + 1;

    avctx->profile = seq->seq_profile;
    avctx->level = seq->seq_level_idx[0];

    avctx->color_range =
        seq->color_config.color_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    avctx->color_primaries = seq->color_config.color_primaries;
    avctx->colorspace = seq->color_config.matrix_coefficients;
    avctx->color_trc = seq->color_config.transfer_characteristics;

    switch (seq->color_config.chroma_sample_position) {
    case AV1_CSP_VERTICAL:
        avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;
        break;
    case AV1_CSP_COLOCATED:
        avctx->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
        break;
    }

    if (seq->film_grain_params_present)
        avctx->properties |= FF_CODEC_PROPERTY_FILM_GRAIN;
    else
        avctx->properties &= ~FF_CODEC_PROPERTY_FILM_GRAIN;

    if (avctx->width != width || avctx->height != height) {
        int ret = ff_set_dimensions(avctx, width, height);
        if (ret < 0)
            return ret;
    }

    if (seq->timing_info_present_flag)
        avctx->framerate = ff_av1_framerate(1LL + seq->timing_info.num_ticks_per_picture_minus_1,
                                            seq->timing_info.num_units_in_display_tick,
                                            seq->timing_info.time_scale);

    if (avctx->pix_fmt == AV_PIX_FMT_NONE)
        avctx->pix_fmt = get_sw_pixel_format(avctx, seq);

    return 0;
}

static int update_context_with_frame_header(AVCodecContext *avctx,
                                            const AV1RawFrameHeader *header)
{
    AVRational aspect_ratio;
    int width = header->frame_width_minus_1 + 1;
    int height = header->frame_height_minus_1 + 1;
    int r_width = header->render_width_minus_1 + 1;
    int r_height = header->render_height_minus_1 + 1;
    int ret;

    if (avctx->width != width || avctx->height != height) {
        ret = ff_set_dimensions(avctx, width, height);
        if (ret < 0)
            return ret;
    }

    av_reduce(&aspect_ratio.num, &aspect_ratio.den,
              (int64_t)height * r_width,
              (int64_t)width * r_height,
              INT_MAX);

    if (av_cmp_q(avctx->sample_aspect_ratio, aspect_ratio)) {
        ret = ff_set_sar(avctx, aspect_ratio);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static const CodedBitstreamUnitType decompose_unit_types[] = {
    AV1_OBU_FRAME,
    AV1_OBU_FRAME_HEADER,
    AV1_OBU_METADATA,
    AV1_OBU_REDUNDANT_FRAME_HEADER,
    AV1_OBU_SEQUENCE_HEADER,
    AV1_OBU_TEMPORAL_DELIMITER,
    AV1_OBU_TILE_GROUP,
};

static av_cold int av1_decode_init(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    AV1RawSequenceHeader *seq;
    const AVPacketSideData *sd;
    int ret;

    s->avctx = avctx;
    s->pkt = avctx->internal->in_pkt;
    s->pix_fmt = AV_PIX_FMT_NONE;

    ret = ff_cbs_init(&s->cbc, AV_CODEC_ID_AV1, avctx);
    if (ret < 0)
        return ret;

    s->cbc->decompose_unit_types    = decompose_unit_types;
    s->cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(decompose_unit_types);

    s->itut_t35_fifo = av_fifo_alloc2(1, sizeof(AV1RawMetadataITUTT35),
                                      AV_FIFO_FLAG_AUTO_GROW);
    if (!s->itut_t35_fifo)
        return AVERROR(ENOMEM);

    av_opt_set_int(s->cbc->priv_data, "operating_point", s->operating_point, 0);

    if (avctx->extradata && avctx->extradata_size) {
        ret = ff_cbs_read_extradata_from_codec(s->cbc,
                                               &s->current_obu,
                                               avctx);
        if (ret < 0) {
            av_log(avctx, AV_LOG_WARNING, "Failed to read extradata.\n");
            goto end;
        }

        seq = ((CodedBitstreamAV1Context *)(s->cbc->priv_data))->sequence_header;
        if (!seq) {
            av_log(avctx, AV_LOG_WARNING, "No sequence header available.\n");
            goto end;
        }

        ret = set_context_with_sequence(avctx, seq);
        if (ret < 0) {
            av_log(avctx, AV_LOG_WARNING, "Failed to set decoder context.\n");
            goto end;
        }

        end:
        ff_cbs_fragment_reset(&s->current_obu);
    }

    s->dovi.logctx = avctx;
    s->dovi.cfg.dv_profile = 10; // default for AV1
    sd = ff_get_coded_side_data(avctx, AV_PKT_DATA_DOVI_CONF);
    if (sd && sd->size >= sizeof(s->dovi.cfg))
        s->dovi.cfg = *(AVDOVIDecoderConfigurationRecord *) sd->data;

    return ret;
}

static int av1_frame_alloc(AVCodecContext *avctx, AV1Frame *f)
{
    AV1DecContext *s = avctx->priv_data;
    AV1RawFrameHeader *header= s->raw_frame_header;
    AVFrame *frame;
    int ret;

    ret = update_context_with_frame_header(avctx, header);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to update context with frame header\n");
        return ret;
    }

    ret = ff_progress_frame_get_buffer(avctx, &f->pf, AV_GET_BUFFER_FLAG_REF);
    if (ret < 0)
        goto fail;

    frame = f->f;
    if (header->frame_type == AV1_FRAME_KEY)
        frame->flags |= AV_FRAME_FLAG_KEY;
    else
        frame->flags &= ~AV_FRAME_FLAG_KEY;

    switch (header->frame_type) {
    case AV1_FRAME_KEY:
    case AV1_FRAME_INTRA_ONLY:
        frame->pict_type = AV_PICTURE_TYPE_I;
        break;
    case AV1_FRAME_INTER:
        frame->pict_type = AV_PICTURE_TYPE_P;
        break;
    case AV1_FRAME_SWITCH:
        frame->pict_type = AV_PICTURE_TYPE_SP;
        break;
    }

    ret = ff_hwaccel_frame_priv_alloc(avctx, &f->hwaccel_picture_private);
    if (ret < 0)
        goto fail;

    return 0;

fail:
    av1_frame_unref(f);
    return ret;
}

static int export_itut_t35(AVCodecContext *avctx, AVFrame *frame,
                           const AV1RawMetadataITUTT35 *itut_t35)
{
    GetByteContext gb;
    AV1DecContext *s = avctx->priv_data;
    int ret, provider_code;

    bytestream2_init(&gb, itut_t35->payload, itut_t35->payload_size);

    provider_code = bytestream2_get_be16(&gb);
    switch (provider_code) {
    case ITU_T_T35_PROVIDER_CODE_ATSC: {
        uint32_t user_identifier = bytestream2_get_be32(&gb);
        switch (user_identifier) {
        case MKBETAG('G', 'A', '9', '4'): { // closed captions
            AVBufferRef *buf = NULL;

            ret = ff_parse_a53_cc(&buf, gb.buffer, bytestream2_get_bytes_left(&gb));
            if (ret < 0)
                return ret;
            if (!ret)
                break;

            ret = ff_frame_new_side_data_from_buf(avctx, frame, AV_FRAME_DATA_A53_CC, &buf);
            if (ret < 0)
                return ret;

            avctx->properties |= FF_CODEC_PROPERTY_CLOSED_CAPTIONS;
            break;
        }
        default: // ignore unsupported identifiers
            break;
        }
        break;
    }
    case ITU_T_T35_PROVIDER_CODE_SMTPE: {
        AVDynamicHDRPlus *hdrplus;
        int provider_oriented_code = bytestream2_get_be16(&gb);
        int application_identifier = bytestream2_get_byte(&gb);

        if (itut_t35->itu_t_t35_country_code != ITU_T_T35_COUNTRY_CODE_US ||
            provider_oriented_code != 1 || application_identifier != 4)
            break;

        hdrplus = av_dynamic_hdr_plus_create_side_data(frame);
        if (!hdrplus)
            return AVERROR(ENOMEM);

        ret = av_dynamic_hdr_plus_from_t35(hdrplus, gb.buffer,
                                           bytestream2_get_bytes_left(&gb));
        if (ret < 0)
            return ret;
        break;
    }
    case ITU_T_T35_PROVIDER_CODE_DOLBY: {
        int provider_oriented_code = bytestream2_get_be32(&gb);
        if (itut_t35->itu_t_t35_country_code != ITU_T_T35_COUNTRY_CODE_US ||
            provider_oriented_code != 0x800)
            break;

        ret = ff_dovi_rpu_parse(&s->dovi, gb.buffer, gb.buffer_end - gb.buffer,
                                avctx->err_recognition);
        if (ret < 0) {
            av_log(avctx, AV_LOG_WARNING, "Error parsing DOVI OBU.\n");
            break; // ignore
        }

        ret = ff_dovi_attach_side_data(&s->dovi, frame);
        if (ret < 0)
            return ret;
        break;
    }
    default: // ignore unsupported provider codes
        break;
    }

    return 0;
}

static int export_metadata(AVCodecContext *avctx, AVFrame *frame)
{
    AV1DecContext *s = avctx->priv_data;
    AV1RawMetadataITUTT35 itut_t35;
    int ret = 0;

    if (s->mdcv) {
        AVMasteringDisplayMetadata *mastering;

        ret = ff_decode_mastering_display_new(avctx, frame, &mastering);
        if (ret < 0)
            return ret;

        if (mastering) {
            for (int i = 0; i < 3; i++) {
                mastering->display_primaries[i][0] = av_make_q(s->mdcv->primary_chromaticity_x[i], 1 << 16);
                mastering->display_primaries[i][1] = av_make_q(s->mdcv->primary_chromaticity_y[i], 1 << 16);
            }
            mastering->white_point[0] = av_make_q(s->mdcv->white_point_chromaticity_x, 1 << 16);
            mastering->white_point[1] = av_make_q(s->mdcv->white_point_chromaticity_y, 1 << 16);

            mastering->max_luminance = av_make_q(s->mdcv->luminance_max, 1 << 8);
            mastering->min_luminance = av_make_q(s->mdcv->luminance_min, 1 << 14);

            mastering->has_primaries = 1;
            mastering->has_luminance = 1;
        }
    }

    if (s->cll) {
        AVContentLightMetadata *light;

        ret = ff_decode_content_light_new(avctx, frame, &light);
        if (ret < 0)
            return ret;

        if (light) {
            light->MaxCLL = s->cll->max_cll;
            light->MaxFALL = s->cll->max_fall;
        }
    }

    while (av_fifo_read(s->itut_t35_fifo, &itut_t35, 1) >= 0) {
        if (ret >= 0)
            ret = export_itut_t35(avctx, frame, &itut_t35);
        av_buffer_unref(&itut_t35.payload_ref);
    }

    return ret;
}

static int export_film_grain(AVCodecContext *avctx, AVFrame *frame)
{
    AV1DecContext *s = avctx->priv_data;
    const AV1RawFilmGrainParams *film_grain = &s->cur_frame.film_grain;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(frame->format);
    AVFilmGrainParams *fgp;
    AVFilmGrainAOMParams *aom;

    av_assert0(pixdesc);
    if (!film_grain->apply_grain)
        return 0;

    fgp = av_film_grain_params_create_side_data(frame);
    if (!fgp)
        return AVERROR(ENOMEM);

    fgp->type = AV_FILM_GRAIN_PARAMS_AV1;
    fgp->seed = film_grain->grain_seed;
    fgp->width = frame->width;
    fgp->height = frame->height;
    fgp->color_range = frame->color_range;
    fgp->color_primaries = frame->color_primaries;
    fgp->color_trc = frame->color_trc;
    fgp->color_space = frame->colorspace;
    fgp->subsampling_x = pixdesc->log2_chroma_w;
    fgp->subsampling_y = pixdesc->log2_chroma_h;

    aom = &fgp->codec.aom;
    aom->chroma_scaling_from_luma = film_grain->chroma_scaling_from_luma;
    aom->scaling_shift = film_grain->grain_scaling_minus_8 + 8;
    aom->ar_coeff_lag = film_grain->ar_coeff_lag;
    aom->ar_coeff_shift = film_grain->ar_coeff_shift_minus_6 + 6;
    aom->grain_scale_shift = film_grain->grain_scale_shift;
    aom->overlap_flag = film_grain->overlap_flag;
    aom->limit_output_range = film_grain->clip_to_restricted_range;

    aom->num_y_points = film_grain->num_y_points;
    for (int i = 0; i < film_grain->num_y_points; i++) {
        aom->y_points[i][0] = film_grain->point_y_value[i];
        aom->y_points[i][1] = film_grain->point_y_scaling[i];
    }
    aom->num_uv_points[0] = film_grain->num_cb_points;
    for (int i = 0; i < film_grain->num_cb_points; i++) {
        aom->uv_points[0][i][0] = film_grain->point_cb_value[i];
        aom->uv_points[0][i][1] = film_grain->point_cb_scaling[i];
    }
    aom->num_uv_points[1] = film_grain->num_cr_points;
    for (int i = 0; i < film_grain->num_cr_points; i++) {
        aom->uv_points[1][i][0] = film_grain->point_cr_value[i];
        aom->uv_points[1][i][1] = film_grain->point_cr_scaling[i];
    }

    for (int i = 0; i < 24; i++) {
        aom->ar_coeffs_y[i] = film_grain->ar_coeffs_y_plus_128[i] - 128;
    }
    for (int i = 0; i < 25; i++) {
        aom->ar_coeffs_uv[0][i] = film_grain->ar_coeffs_cb_plus_128[i] - 128;
        aom->ar_coeffs_uv[1][i] = film_grain->ar_coeffs_cr_plus_128[i] - 128;
    }

    aom->uv_mult[0] = film_grain->cb_mult;
    aom->uv_mult[1] = film_grain->cr_mult;
    aom->uv_mult_luma[0] = film_grain->cb_luma_mult;
    aom->uv_mult_luma[1] = film_grain->cr_luma_mult;
    aom->uv_offset[0] = film_grain->cb_offset;
    aom->uv_offset[1] = film_grain->cr_offset;

    return 0;
}

static int set_output_frame(AVCodecContext *avctx, AVFrame *frame)
{
    AV1DecContext *s = avctx->priv_data;
    const AVFrame *srcframe = s->cur_frame.f;
    AVPacket *pkt = s->pkt;
    int ret;

    // TODO: all layers
    if (s->operating_point_idc &&
        av_log2(s->operating_point_idc >> 8) > s->cur_frame.spatial_id)
        return 0;

    ret = av_frame_ref(frame, srcframe);
    if (ret < 0)
        return ret;

    ret = export_metadata(avctx, frame);
    if (ret < 0) {
        av_frame_unref(frame);
        return ret;
    }

    if (avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) {
        ret = export_film_grain(avctx, frame);
        if (ret < 0) {
            av_frame_unref(frame);
            return ret;
        }
    }

    frame->pts = pkt->pts;
    frame->pkt_dts = pkt->dts;
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
    frame->pkt_size = pkt->size;
    frame->pkt_pos = pkt->pos;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    av_packet_unref(pkt);

    return 0;
}

static void update_reference_list(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    const AV1RawFrameHeader *header = s->raw_frame_header;

    for (int i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        if (header->refresh_frame_flags & (1 << i))
            av1_frame_replace(&s->ref[i], &s->cur_frame);
    }
}

static int get_current_frame(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    int ret;

    av1_frame_unref(&s->cur_frame);

    s->cur_frame.header_ref = ff_refstruct_ref(s->header_ref);

    s->cur_frame.raw_frame_header = s->raw_frame_header;

    ret = init_tile_data(s);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to init tile data.\n");
        return ret;
    }

    if ((avctx->skip_frame >= AVDISCARD_NONINTRA &&
            (s->raw_frame_header->frame_type != AV1_FRAME_KEY &&
             s->raw_frame_header->frame_type != AV1_FRAME_INTRA_ONLY)) ||
        (avctx->skip_frame >= AVDISCARD_NONKEY   &&
             s->raw_frame_header->frame_type != AV1_FRAME_KEY) ||
        avctx->skip_frame >= AVDISCARD_ALL)
        return 0;

    if (s->pix_fmt == AV_PIX_FMT_NONE) {
        ret = get_pixel_format(avctx);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to get pixel format.\n");
            return ret;
        }

        if (!ret && FF_HW_HAS_CB(avctx, decode_params)) {
            ret = FF_HW_CALL(avctx, decode_params, AV1_OBU_SEQUENCE_HEADER,
                             s->seq_data_ref->data, s->seq_data_ref->size);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "HW accel decode params fail.\n");
                return ret;
            }
        }
    }

    ret = av1_frame_alloc(avctx, &s->cur_frame);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to allocate space for current frame.\n");
        return ret;
    }

    global_motion_params(s);
    skip_mode_params(s);
    coded_lossless_param(s);
    order_hint_info(s);
    load_grain_params(s);

    s->cur_frame.force_integer_mv =
        s->raw_frame_header->force_integer_mv ||
        s->raw_frame_header->frame_type == AV1_FRAME_KEY ||
        s->raw_frame_header->frame_type == AV1_FRAME_INTRA_ONLY;

    return ret;
}

static int av1_receive_frame_internal(AVCodecContext *avctx, AVFrame *frame)
{
    AV1DecContext *s = avctx->priv_data;
    AV1RawTileGroup *raw_tile_group = NULL;
    int i = 0, ret;

    for (i = s->nb_unit; i < s->current_obu.nb_units; i++) {
        CodedBitstreamUnit *unit = &s->current_obu.units[i];
        AV1RawOBU *obu = unit->content;
        const AV1RawOBUHeader *header;

        av_log(avctx, AV_LOG_DEBUG, "OBU idx:%d, type:%d, content available:%d.\n", i, unit->type, !!obu);

        if (unit->type == AV1_OBU_TILE_LIST) {
            av_log(avctx, AV_LOG_ERROR, "Large scale tile decoding is unsupported.\n");
            ret = AVERROR_PATCHWELCOME;
            goto end;
        }

        if (!obu)
            continue;

        header = &obu->header;

        switch (unit->type) {
        case AV1_OBU_SEQUENCE_HEADER:
            ret = av_buffer_replace(&s->seq_data_ref, unit->data_ref);
            if (ret < 0)
                goto end;

            s->seq_data_ref->data = unit->data;
            s->seq_data_ref->size = unit->data_size;
            ff_refstruct_replace(&s->seq_ref, unit->content_ref);

            s->raw_seq = &obu->obu.sequence_header;

            ret = set_context_with_sequence(avctx, s->raw_seq);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to set context.\n");
                s->raw_seq = NULL;
                goto end;
            }

            s->operating_point_idc = s->raw_seq->operating_point_idc[s->operating_point];

            s->pix_fmt = AV_PIX_FMT_NONE;

            break;
        case AV1_OBU_REDUNDANT_FRAME_HEADER:
            if (s->raw_frame_header)
                break;
        // fall-through
        case AV1_OBU_FRAME:
        case AV1_OBU_FRAME_HEADER:
            if (!s->raw_seq) {
                av_log(avctx, AV_LOG_ERROR, "Missing Sequence Header.\n");
                ret = AVERROR_INVALIDDATA;
                goto end;
            }

            ff_refstruct_replace(&s->header_ref, unit->content_ref);

            if (unit->type == AV1_OBU_FRAME)
                s->raw_frame_header = &obu->obu.frame.header;
            else
                s->raw_frame_header = &obu->obu.frame_header;

            if (s->raw_frame_header->show_existing_frame) {
                av1_frame_replace(&s->cur_frame,
                                  &s->ref[s->raw_frame_header->frame_to_show_map_idx]);

                update_reference_list(avctx);

                if (s->cur_frame.f) {
                    ret = set_output_frame(avctx, frame);
                    if (ret < 0) {
                        av_log(avctx, AV_LOG_ERROR, "Set output frame error.\n");
                        goto end;
                    }
                }

                s->raw_frame_header = NULL;
                i++;
                ret = 0;

                goto end;
            }

            ret = get_current_frame(avctx);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Get current frame error\n");
                goto end;
            }

            s->cur_frame.spatial_id  = header->spatial_id;
            s->cur_frame.temporal_id = header->temporal_id;

            if (avctx->hwaccel && s->cur_frame.f) {
                ret = FF_HW_CALL(avctx, start_frame, unit->data, unit->data_size);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "HW accel start frame fail.\n");
                    goto end;
                }
            }
            if (unit->type != AV1_OBU_FRAME)
                break;
        // fall-through
        case AV1_OBU_TILE_GROUP:
            if (!s->raw_frame_header) {
                av_log(avctx, AV_LOG_ERROR, "Missing Frame Header.\n");
                ret = AVERROR_INVALIDDATA;
                goto end;
            }

            if (unit->type == AV1_OBU_FRAME)
                raw_tile_group = &obu->obu.frame.tile_group;
            else
                raw_tile_group = &obu->obu.tile_group;

            ret = get_tiles_info(avctx, raw_tile_group);
            if (ret < 0)
                goto end;

            if (avctx->hwaccel && s->cur_frame.f) {
                ret = FF_HW_CALL(avctx, decode_slice, raw_tile_group->tile_data.data,
                                 raw_tile_group->tile_data.data_size);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "HW accel decode slice fail.\n");
                    goto end;
                }
            }
            break;
        case AV1_OBU_TILE_LIST:
        case AV1_OBU_TEMPORAL_DELIMITER:
        case AV1_OBU_PADDING:
            break;
        case AV1_OBU_METADATA:
            switch (obu->obu.metadata.metadata_type) {
            case AV1_METADATA_TYPE_HDR_CLL:
                ff_refstruct_replace(&s->cll_ref, unit->content_ref);
                s->cll = &obu->obu.metadata.metadata.hdr_cll;
                break;
            case AV1_METADATA_TYPE_HDR_MDCV:
                ff_refstruct_replace(&s->mdcv_ref, unit->content_ref);
                s->mdcv = &obu->obu.metadata.metadata.hdr_mdcv;
                break;
            case AV1_METADATA_TYPE_ITUT_T35: {
                AV1RawMetadataITUTT35 itut_t35;
                memcpy(&itut_t35, &obu->obu.metadata.metadata.itut_t35, sizeof(itut_t35));
                itut_t35.payload_ref = av_buffer_ref(obu->obu.metadata.metadata.itut_t35.payload_ref);
                if (!itut_t35.payload_ref) {
                    ret = AVERROR(ENOMEM);
                    goto end;
                }
                ret = av_fifo_write(s->itut_t35_fifo, &itut_t35, 1);
                if (ret < 0) {
                    av_buffer_unref(&itut_t35.payload_ref);
                    goto end;
                }
                break;
            }
            default:
                break;
            }
            break;
        default:
            av_log(avctx, AV_LOG_DEBUG,
                   "Unknown obu type: %d (%"SIZE_SPECIFIER" bits).\n",
                   unit->type, unit->data_size);
        }

        if (raw_tile_group && (s->tile_num == raw_tile_group->tg_end + 1)) {
            int show_frame = s->raw_frame_header->show_frame;
            // Set nb_unit to point at the next OBU, to indicate which
            // OBUs have been processed for this current frame. (If this
            // frame gets output, we set nb_unit to this value later too.)
            s->nb_unit = i + 1;
            if (avctx->hwaccel && s->cur_frame.f) {
                ret = FF_HW_SIMPLE_CALL(avctx, end_frame);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR, "HW accel end frame fail.\n");
                    goto end;
                }
            }

            update_reference_list(avctx);

            // Set start_unit to indicate the first OBU of the next frame.
            s->start_unit       = s->nb_unit;
            raw_tile_group      = NULL;
            s->raw_frame_header = NULL;

            if (show_frame) {
                // cur_frame.f needn't exist due to skip_frame.
                if (s->cur_frame.f) {
                    ret = set_output_frame(avctx, frame);
                    if (ret < 0) {
                        av_log(avctx, AV_LOG_ERROR, "Set output frame error\n");
                        goto end;
                    }
                }
                i++;
                ret = 0;
                goto end;
            }
        }
    }

    ret = AVERROR(EAGAIN);
end:
    av_assert0(i <= s->current_obu.nb_units);
    s->nb_unit = i;

    if ((ret < 0 && ret != AVERROR(EAGAIN)) || s->current_obu.nb_units == i) {
        if (ret < 0)
            s->raw_frame_header = NULL;
        av_packet_unref(s->pkt);
        ff_cbs_fragment_reset(&s->current_obu);
        s->nb_unit = s->start_unit = 0;
    }
    if (!ret && !frame->buf[0])
        ret = AVERROR(EAGAIN);

    return ret;
}

static int av1_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    AV1DecContext *s = avctx->priv_data;
    int ret;

    do {
        if (!s->current_obu.nb_units) {
            ret = ff_decode_get_packet(avctx, s->pkt);
            if (ret < 0)
                return ret;

            ret = ff_cbs_read_packet(s->cbc, &s->current_obu, s->pkt);
            if (ret < 0) {
                ff_cbs_fragment_reset(&s->current_obu);
                av_packet_unref(s->pkt);
                av_log(avctx, AV_LOG_ERROR, "Failed to read packet.\n");
                return ret;
            }

            s->nb_unit = s->start_unit = 0;
            av_log(avctx, AV_LOG_DEBUG, "Total OBUs on this packet: %d.\n",
                   s->current_obu.nb_units);
        }

        ret = av1_receive_frame_internal(avctx, frame);
    } while (ret == AVERROR(EAGAIN));

    return ret;
}

static void av1_decode_flush(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    AV1RawMetadataITUTT35 itut_t35;

    for (int i = 0; i < FF_ARRAY_ELEMS(s->ref); i++)
        av1_frame_unref(&s->ref[i]);

    av1_frame_unref(&s->cur_frame);
    s->operating_point_idc = 0;
    s->nb_unit = s->start_unit = 0;
    s->raw_frame_header = NULL;
    s->raw_seq = NULL;
    s->cll = NULL;
    s->mdcv = NULL;
    while (av_fifo_read(s->itut_t35_fifo, &itut_t35, 1) >= 0)
        av_buffer_unref(&itut_t35.payload_ref);

    ff_cbs_fragment_reset(&s->current_obu);
    ff_cbs_flush(s->cbc);

    if (FF_HW_HAS_CB(avctx, flush))
        FF_HW_SIMPLE_CALL(avctx, flush);
}

#define OFFSET(x) offsetof(AV1DecContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption av1_options[] = {
    { "operating_point",  "Select an operating point of the scalable bitstream",
                          OFFSET(operating_point), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, AV1_MAX_OPERATING_POINTS - 1, VD },
    { NULL }
};

static const AVClass av1_class = {
    .class_name = "AV1 decoder",
    .item_name  = av_default_item_name,
    .option     = av1_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_av1_decoder = {
    .p.name                = "av1",
    CODEC_LONG_NAME("Alliance for Open Media AV1"),
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_AV1,
    .priv_data_size        = sizeof(AV1DecContext),
    .init                  = av1_decode_init,
    .close                 = av1_decode_free,
    FF_CODEC_RECEIVE_FRAME_CB(av1_receive_frame),
    .p.capabilities        = AV_CODEC_CAP_DR1,
    .caps_internal         = FF_CODEC_CAP_INIT_CLEANUP |
                             FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM |
                             FF_CODEC_CAP_USES_PROGRESSFRAMES,
    .flush                 = av1_decode_flush,
    .p.profiles            = NULL_IF_CONFIG_SMALL(ff_av1_profiles),
    .p.priv_class          = &av1_class,
    .hw_configs            = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_AV1_DXVA2_HWACCEL
        HWACCEL_DXVA2(av1),
#endif
#if CONFIG_AV1_D3D11VA_HWACCEL
        HWACCEL_D3D11VA(av1),
#endif
#if CONFIG_AV1_D3D11VA2_HWACCEL
        HWACCEL_D3D11VA2(av1),
#endif
#if CONFIG_AV1_D3D12VA_HWACCEL
        HWACCEL_D3D12VA(av1),
#endif
#if CONFIG_AV1_NVDEC_HWACCEL
        HWACCEL_NVDEC(av1),
#endif
#if CONFIG_AV1_VAAPI_HWACCEL
        HWACCEL_VAAPI(av1),
#endif
#if CONFIG_AV1_VDPAU_HWACCEL
        HWACCEL_VDPAU(av1),
#endif
#if CONFIG_AV1_VIDEOTOOLBOX_HWACCEL
        HWACCEL_VIDEOTOOLBOX(av1),
#endif
#if CONFIG_AV1_VULKAN_HWACCEL
        HWACCEL_VULKAN(av1),
#endif

        NULL
    },
};
