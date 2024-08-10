/*
 * H.266 / VVC parser
 *
 * Copyright (C) 2021 Nuo Mi <nuomi2021@gmail.com>
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

#include "libavutil/mem.h"
#include "cbs.h"
#include "cbs_h266.h"
#include "parser.h"

#define START_CODE 0x000001 ///< start_code_prefix_one_3bytes
#define IS_IDR(nut)   (nut == VVC_IDR_W_RADL || nut == VVC_IDR_N_LP)
#define IS_H266_SLICE(nut) (nut <= VVC_RASL_NUT || (nut >= VVC_IDR_W_RADL && nut <= VVC_GDR_NUT))

typedef struct PuInfo {
    const H266RawPPS *pps;
    const H266RawSPS *sps;
    const H266RawPictureHeader *ph;
    const H266RawSlice *slice;
    int pic_type;
} PuInfo;

typedef struct AuDetector {
    uint8_t prev_layer_id;
    int prev_tid0_poc;
    int prev_poc;
} AuDetector;

typedef struct VVCParserContext {
    ParseContext pc;
    CodedBitstreamContext *cbc;

    CodedBitstreamFragment picture_unit;

    AVPacket au;
    AVPacket last_au;

    AuDetector au_detector;

    int parsed_extradata;
} VVCParserContext;

static const enum AVPixelFormat pix_fmts_8bit[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P
};

static const enum AVPixelFormat pix_fmts_10bit[] = {
    AV_PIX_FMT_GRAY10, AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10
};

static int get_format(const H266RawSPS *sps)
{
    switch (sps->sps_bitdepth_minus8) {
    case 0:
        return pix_fmts_8bit[sps->sps_chroma_format_idc];
    case 2:
        return pix_fmts_10bit[sps->sps_chroma_format_idc];
    }
    return AV_PIX_FMT_NONE;
}

/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or END_NOT_FOUND
 */
static int find_frame_end(AVCodecParserContext *s, const uint8_t *buf,
                          int buf_size)
{
    VVCParserContext *ctx = s->priv_data;
    ParseContext *pc = &ctx->pc;
    int i;

    for (i = 0; i < buf_size; i++) {
        int nut, code_len;

        pc->state64 = (pc->state64 << 8) | buf[i];

        if (((pc->state64 >> 3 * 8) & 0xFFFFFF) != START_CODE)
            continue;

        code_len = ((pc->state64 >> 3 * 8) & 0xFFFFFFFF) == 0x01 ? 4 : 3;

        nut = (pc->state64 >> (8 + 3)) & 0x1F;
        // 7.4.2.4.3 and 7.4.2.4.4
        if ((nut >= VVC_OPI_NUT && nut <= VVC_PREFIX_APS_NUT &&
             nut != VVC_PH_NUT) || nut == VVC_AUD_NUT
            || (nut == VVC_PREFIX_SEI_NUT && !pc->frame_start_found)
            || nut == VVC_RSV_NVCL_26 || nut == VVC_UNSPEC_28
            || nut == VVC_UNSPEC_29) {
            if (pc->frame_start_found) {
                pc->frame_start_found = 0;
                return i - (code_len + 2);
            }
        } else if (nut == VVC_PH_NUT || IS_H266_SLICE(nut)) {
            int sh_picture_header_in_slice_header_flag = buf[i] >> 7;

            if (nut == VVC_PH_NUT || sh_picture_header_in_slice_header_flag) {
                if (!pc->frame_start_found) {
                    pc->frame_start_found = 1;
                } else {        // First slice of next frame found
                    pc->frame_start_found = 0;
                    return i - (code_len + 2);
                }
            }
        }
    }
    return END_NOT_FOUND;
}

static int get_pict_type(const CodedBitstreamFragment *pu)
{
    int has_p = 0;
    for (int i = 0; i < pu->nb_units; i++) {
        CodedBitstreamUnit *unit = &pu->units[i];
        if (IS_H266_SLICE(unit->type)) {
            const H266RawSlice *slice = unit->content;
            uint8_t type = slice->header.sh_slice_type;
            if (type == VVC_SLICE_TYPE_B) {
                return AV_PICTURE_TYPE_B;
            }
            if (type == VVC_SLICE_TYPE_P) {
                has_p = 1;
            }
        }
    }
    return has_p ? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
}

static void set_parser_ctx(AVCodecParserContext *s, AVCodecContext *avctx,
                           const PuInfo *pu)
{
    static const uint8_t h266_sub_width_c[] = {
        1, 2, 2, 1
    };
    static const uint8_t h266_sub_height_c[] = {
        1, 2, 1, 1
    };
    const H266RawSPS *sps = pu->sps;
    const H266RawPPS *pps = pu->pps;
    const H266RawNALUnitHeader *nal = &pu->slice->header.nal_unit_header;

    s->pict_type = pu->pic_type;
    s->format = get_format(sps);
    s->picture_structure = AV_PICTURE_STRUCTURE_FRAME;

    s->key_frame = nal->nal_unit_type == VVC_IDR_W_RADL ||
                   nal->nal_unit_type == VVC_IDR_N_LP ||
                   nal->nal_unit_type == VVC_CRA_NUT ||
                   nal->nal_unit_type == VVC_GDR_NUT;

    s->coded_width  = pps->pps_pic_width_in_luma_samples;
    s->coded_height = pps->pps_pic_height_in_luma_samples;
    s->width = pps->pps_pic_width_in_luma_samples -
        (pps->pps_conf_win_left_offset + pps->pps_conf_win_right_offset) *
        h266_sub_width_c[sps->sps_chroma_format_idc];
    s->height = pps->pps_pic_height_in_luma_samples -
        (pps->pps_conf_win_top_offset + pps->pps_conf_win_bottom_offset) *
        h266_sub_height_c[sps->sps_chroma_format_idc];

    avctx->profile = sps->profile_tier_level.general_profile_idc;
    avctx->level = sps->profile_tier_level.general_level_idc;

    avctx->colorspace = (enum AVColorSpace) sps->vui.vui_matrix_coeffs;
    avctx->color_primaries = (enum AVColorPrimaries) sps->vui.vui_colour_primaries;
    avctx->color_trc = (enum AVColorTransferCharacteristic) sps->vui.vui_transfer_characteristics;
    avctx->color_range =
        sps->vui.vui_full_range_flag ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    if (sps->sps_ptl_dpb_hrd_params_present_flag &&
        sps->sps_timing_hrd_params_present_flag) {
        uint32_t num = sps->sps_general_timing_hrd_parameters.num_units_in_tick;
        uint32_t den = sps->sps_general_timing_hrd_parameters.time_scale;

        if (num != 0 && den != 0)
            av_reduce(&avctx->framerate.den, &avctx->framerate.num,
                      num, den, 1 << 30);
    }
}

//8.3.1 Decoding process for picture order count.
//VTM did not follow the spec, and it's much simpler than spec.
//We follow the VTM.
static void get_slice_poc(VVCParserContext *s, int *poc,
                          const H266RawSPS *sps,
                          const H266RawPictureHeader *ph,
                          const H266RawSliceHeader *slice, void *log_ctx)
{
    int poc_msb, max_poc_lsb, poc_lsb;
    AuDetector *d = &s->au_detector;
    max_poc_lsb = 1 << (sps->sps_log2_max_pic_order_cnt_lsb_minus4 + 4);
    poc_lsb = ph->ph_pic_order_cnt_lsb;
    if (IS_IDR(slice->nal_unit_header.nal_unit_type)) {
        if (ph->ph_poc_msb_cycle_present_flag)
            poc_msb = ph->ph_poc_msb_cycle_val * max_poc_lsb;
        else
            poc_msb = 0;
    } else {
        int prev_poc = d->prev_tid0_poc;
        int prev_poc_lsb = prev_poc & (max_poc_lsb - 1);
        int prev_poc_msb = prev_poc - prev_poc_lsb;
        if (ph->ph_poc_msb_cycle_present_flag) {
            poc_msb = ph->ph_poc_msb_cycle_val * max_poc_lsb;
        } else {
            if ((poc_lsb < prev_poc_lsb) && ((prev_poc_lsb - poc_lsb) >=
                (max_poc_lsb / 2)))
                poc_msb = prev_poc_msb + (unsigned)max_poc_lsb;
            else if ((poc_lsb > prev_poc_lsb) && ((poc_lsb - prev_poc_lsb) >
                     (max_poc_lsb / 2)))
                poc_msb = prev_poc_msb - (unsigned)max_poc_lsb;
            else
                poc_msb = prev_poc_msb;
        }
    }

    *poc = poc_msb + poc_lsb;
}

static void au_detector_init(AuDetector *d)
{
    d->prev_layer_id = UINT8_MAX;
    d->prev_poc = INT_MAX;
    d->prev_tid0_poc = INT_MAX;
}

static int is_au_start(VVCParserContext *s, const PuInfo *pu, void *log_ctx)
{
    //7.4.2.4.3
    AuDetector *d = &s->au_detector;
    const H266RawSPS *sps = pu->sps;
    const H266RawNALUnitHeader *nal = &pu->slice->header.nal_unit_header;
    const H266RawPictureHeader *ph = pu->ph;
    const H266RawSlice *slice = pu->slice;
    int ret, poc, nut;

    get_slice_poc(s, &poc, sps, ph, &slice->header, log_ctx);

    ret = (nal->nuh_layer_id <= d->prev_layer_id) || (poc != d->prev_poc);

    nut = nal->nal_unit_type;
    d->prev_layer_id = nal->nuh_layer_id;
    d->prev_poc = poc;
    if (nal->nuh_temporal_id_plus1 == 1 &&
        !ph->ph_non_ref_pic_flag && nut != VVC_RADL_NUT
        && nut != VVC_RASL_NUT) {
        d->prev_tid0_poc = poc;
    }
    return ret;
}

static int get_pu_info(PuInfo *info, const CodedBitstreamH266Context *h266,
                       const CodedBitstreamFragment *pu, void *logctx)
{
    const H266RawNALUnitHeader *nal;
    int ret;

    memset(info, 0, sizeof(*info));
    for (int i = 0; i < pu->nb_units; i++) {
        nal = pu->units[i].content;
        if (!nal)
            continue;
        if ( nal->nal_unit_type == VVC_PH_NUT ) {
            const H266RawPH *ph = pu->units[i].content;
            info->ph = &ph->ph_picture_header;
        } else if (IS_H266_SLICE(nal->nal_unit_type)) {
            info->slice = pu->units[i].content;
            if (info->slice->header.sh_picture_header_in_slice_header_flag)
                info->ph = &info->slice->header.sh_picture_header;
            if (!info->ph) {
                av_log(logctx, AV_LOG_ERROR,
                       "can't find picture header in picture unit.\n");
                ret = AVERROR_INVALIDDATA;
                goto error;
            }
            break;
        }
    }
    if (!info->slice) {
        av_log(logctx, AV_LOG_ERROR, "can't find slice in picture unit.\n");
        ret = AVERROR_INVALIDDATA;
        goto error;
    }
    info->pps = h266->pps[info->ph->ph_pic_parameter_set_id];
    if (!info->pps) {
        av_log(logctx, AV_LOG_ERROR, "PPS id %d is not avaliable.\n",
               info->ph->ph_pic_parameter_set_id);
        ret = AVERROR_INVALIDDATA;
        goto error;
    }
    info->sps = h266->sps[info->pps->pps_seq_parameter_set_id];
    if (!info->sps) {
        av_log(logctx, AV_LOG_ERROR, "SPS id %d is not avaliable.\n",
               info->pps->pps_seq_parameter_set_id);
        ret = AVERROR_INVALIDDATA;
        goto error;
    }
    info->pic_type = get_pict_type(pu);
    return 0;
error:
    memset(info, 0, sizeof(*info));
    return ret;
}

static int append_au(AVPacket *pkt, const uint8_t *buf, int buf_size)
{
    int offset = pkt->size;
    int ret;
    if ((ret = av_grow_packet(pkt, buf_size)) < 0)
        goto end;
    memcpy(pkt->data + offset, buf, buf_size);
end:
    return ret;
}

/**
 * Parse NAL units of found picture and decode some basic information.
 *
 * @param s parser context.
 * @param avctx codec context.
 * @param buf buffer with field/frame data.
 * @param buf_size size of the buffer.
 * @return < 0 for error, == 0 for a complete au, > 0 is not a completed au.
 */
static int parse_nal_units(AVCodecParserContext *s, const uint8_t *buf,
                           int buf_size, AVCodecContext *avctx)
{
    VVCParserContext *ctx = s->priv_data;
    const CodedBitstreamH266Context *h266 = ctx->cbc->priv_data;

    CodedBitstreamFragment *pu = &ctx->picture_unit;
    int ret;
    PuInfo info;

    if (!buf_size) {
        if (ctx->au.size) {
            av_packet_move_ref(&ctx->last_au, &ctx->au);
            return 0;
        }
        return 1;
    }

    if ((ret = ff_cbs_read(ctx->cbc, pu, buf, buf_size)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to parse picture unit.\n");
        goto end;
    }
    if ((ret = get_pu_info(&info, h266, pu, avctx)) < 0)
        goto end;
    if (append_au(&ctx->au, buf, buf_size) < 0) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    if (is_au_start(ctx, &info, avctx)) {
        set_parser_ctx(s, avctx, &info);
        av_packet_move_ref(&ctx->last_au, &ctx->au);
    } else {
        ret = 1; //not a completed au
    }
end:
    ff_cbs_fragment_reset(pu);
    return ret;
}

/**
 * Combine PU to AU
 *
 * @param s parser context.
 * @param avctx codec context.
 * @param buf buffer to a PU.
 * @param buf_size size of the buffer.
 * @return < 0 for error, == 0 a complete au, > 0 not a completed au.
 */
static int combine_au(AVCodecParserContext *s, AVCodecContext *avctx,
                      const uint8_t **buf, int *buf_size)
{
    VVCParserContext *ctx = s->priv_data;
    int ret;

    ctx->cbc->log_ctx = avctx;

    av_packet_unref(&ctx->last_au);
    ret = parse_nal_units(s, *buf, *buf_size, avctx);
    if (ret == 0) {
        if (ctx->last_au.size) {
            *buf = ctx->last_au.data;
            *buf_size = ctx->last_au.size;
        } else {
            ret = 1; //no output
        }
    }
    ctx->cbc->log_ctx = NULL;
    return ret;
}

static int vvc_parser_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                            const uint8_t **poutbuf, int *poutbuf_size,
                            const uint8_t *buf, int buf_size)
{
    int next, ret;
    VVCParserContext *ctx = s->priv_data;
    ParseContext *pc = &ctx->pc;
    CodedBitstreamFragment *pu = &ctx->picture_unit;

    int is_dummy_buf = !buf_size;
    int flush = !buf_size;
    const uint8_t *dummy_buf = buf;

    *poutbuf = NULL;
    *poutbuf_size = 0;

    if (avctx->extradata_size && !ctx->parsed_extradata) {
        ctx->parsed_extradata = 1;

        ret = ff_cbs_read_extradata_from_codec(ctx->cbc, pu, avctx);
        if (ret < 0)
            av_log(avctx, AV_LOG_WARNING, "Failed to parse extradata.\n");

        ff_cbs_fragment_reset(pu);
    }

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = find_frame_end(s, buf, buf_size);
        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0)
            return buf_size;
    }

    is_dummy_buf &= (dummy_buf == buf);

    if (!is_dummy_buf) {
        ret = combine_au(s, avctx, &buf, &buf_size);
        if (ret > 0 && flush) {
            buf_size = 0;
            ret = combine_au(s, avctx, &buf, &buf_size);
        }
        if (ret != 0)
            return next;
    }

    *poutbuf = buf;
    *poutbuf_size = buf_size;

    return next;
}

static const CodedBitstreamUnitType decompose_unit_types[] = {
    VVC_TRAIL_NUT,
    VVC_STSA_NUT,
    VVC_RADL_NUT,
    VVC_RASL_NUT,
    VVC_IDR_W_RADL,
    VVC_IDR_N_LP,
    VVC_CRA_NUT,
    VVC_GDR_NUT,
    VVC_VPS_NUT,
    VVC_SPS_NUT,
    VVC_PPS_NUT,
    VVC_PH_NUT,
    VVC_AUD_NUT,
};

static av_cold int vvc_parser_init(AVCodecParserContext *s)
{
    VVCParserContext *ctx = s->priv_data;
    int ret;

    ret = ff_cbs_init(&ctx->cbc, AV_CODEC_ID_VVC, NULL);
    if (ret < 0)
        return ret;
    au_detector_init(&ctx->au_detector);

    ctx->cbc->decompose_unit_types    = decompose_unit_types;
    ctx->cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(decompose_unit_types);

    return ret;
}

static av_cold void vvc_parser_close(AVCodecParserContext *s)
{
    VVCParserContext *ctx = s->priv_data;

    av_packet_unref(&ctx->au);
    av_packet_unref(&ctx->last_au);
    ff_cbs_fragment_free(&ctx->picture_unit);

    ff_cbs_close(&ctx->cbc);
    av_freep(&ctx->pc.buffer);
}

const AVCodecParser ff_vvc_parser = {
    .codec_ids      = { AV_CODEC_ID_VVC },
    .priv_data_size = sizeof(VVCParserContext),
    .parser_init    = vvc_parser_init,
    .parser_close   = vvc_parser_close,
    .parser_parse   = vvc_parser_parse,
};
