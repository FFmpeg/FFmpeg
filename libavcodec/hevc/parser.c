/*
 * HEVC Annex B format parser
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

#include "libavutil/common.h"
#include "libavutil/mem.h"

#include "golomb.h"
#include "hevc.h"
#include "parse.h"
#include "ps.h"
#include "sei.h"
#include "h2645_parse.h"
#include "parser.h"

#define START_CODE 0x000001 ///< start_code_prefix_one_3bytes

#define IS_IRAP_NAL(nal) (nal->type >= 16 && nal->type <= 23)
#define IS_IDR_NAL(nal) (nal->type == HEVC_NAL_IDR_W_RADL || nal->type == HEVC_NAL_IDR_N_LP)

typedef struct HEVCParserContext {
    ParseContext pc;

    H2645Packet pkt;
    HEVCParamSets ps;
    HEVCSEI sei;

    int is_avc;
    int nal_length_size;
    int parsed_extradata;

    int poc;
    int pocTid0;
} HEVCParserContext;

static int hevc_parse_slice_header(AVCodecParserContext *s, H2645NAL *nal,
                                   AVCodecContext *avctx)
{
    HEVCParserContext *ctx = s->priv_data;
    HEVCParamSets *ps = &ctx->ps;
    HEVCSEI *sei = &ctx->sei;
    GetBitContext *gb = &nal->gb;
    const HEVCPPS *pps;
    const HEVCSPS *sps;
    const HEVCWindow *ow;
    int i, num = 0, den = 0;

    unsigned int pps_id, first_slice_in_pic_flag, dependent_slice_segment_flag;
    enum HEVCSliceType slice_type;

    first_slice_in_pic_flag = get_bits1(gb);
    s->picture_structure = sei->picture_timing.picture_struct;
    s->field_order = sei->picture_timing.picture_struct;

    if (IS_IRAP_NAL(nal)) {
        s->key_frame = 1;
        skip_bits1(gb); // no_output_of_prior_pics_flag
    }

    pps_id = get_ue_golomb(gb);
    if (pps_id >= HEVC_MAX_PPS_COUNT || !ps->pps_list[pps_id]) {
        av_log(avctx, AV_LOG_ERROR, "PPS id out of range: %d\n", pps_id);
        return AVERROR_INVALIDDATA;
    }
    pps = ps->pps_list[pps_id];
    sps = pps->sps;

    ow  = &sps->output_window;

    s->coded_width  = sps->width;
    s->coded_height = sps->height;
    s->width        = sps->width  - ow->left_offset - ow->right_offset;
    s->height       = sps->height - ow->top_offset  - ow->bottom_offset;
    s->format       = sps->pix_fmt;
    avctx->profile  = sps->ptl.general_ptl.profile_idc;
    avctx->level    = sps->ptl.general_ptl.level_idc;

    if (sps->vps->vps_timing_info_present_flag) {
        num = sps->vps->vps_num_units_in_tick;
        den = sps->vps->vps_time_scale;
    } else if (sps->vui.vui_timing_info_present_flag) {
        num = sps->vui.vui_num_units_in_tick;
        den = sps->vui.vui_time_scale;
    }

    if (num > 0 && den > 0)
        av_reduce(&avctx->framerate.den, &avctx->framerate.num,
                  num, den, 1 << 30);

    if (!first_slice_in_pic_flag) {
        unsigned int slice_segment_addr;
        int slice_address_length;

        if (pps->dependent_slice_segments_enabled_flag)
            dependent_slice_segment_flag = get_bits1(gb);
        else
            dependent_slice_segment_flag = 0;

        slice_address_length = av_ceil_log2_c(sps->ctb_width *
                                              sps->ctb_height);
        slice_segment_addr = get_bitsz(gb, slice_address_length);
        if (slice_segment_addr >= sps->ctb_width * sps->ctb_height) {
            av_log(avctx, AV_LOG_ERROR, "Invalid slice segment address: %u.\n",
                   slice_segment_addr);
            return AVERROR_INVALIDDATA;
        }
    } else
        dependent_slice_segment_flag = 0;

    if (dependent_slice_segment_flag)
        return 0; /* break; */

    for (i = 0; i < pps->num_extra_slice_header_bits; i++)
        skip_bits(gb, 1); // slice_reserved_undetermined_flag[]

    slice_type = get_ue_golomb_31(gb);
    if (!(slice_type == HEVC_SLICE_I || slice_type == HEVC_SLICE_P ||
          slice_type == HEVC_SLICE_B)) {
        av_log(avctx, AV_LOG_ERROR, "Unknown slice type: %d.\n",
               slice_type);
        return AVERROR_INVALIDDATA;
    }
    s->pict_type = slice_type == HEVC_SLICE_B ? AV_PICTURE_TYPE_B :
                   slice_type == HEVC_SLICE_P ? AV_PICTURE_TYPE_P :
                                                AV_PICTURE_TYPE_I;

    if (pps->output_flag_present_flag)
        skip_bits1(gb); // pic_output_flag

    if (sps->separate_colour_plane)
        skip_bits(gb, 2);   // colour_plane_id

    if (!IS_IDR_NAL(nal)) {
        int pic_order_cnt_lsb = get_bits(gb, sps->log2_max_poc_lsb);
        s->output_picture_number = ctx->poc =
            ff_hevc_compute_poc(sps, ctx->pocTid0, pic_order_cnt_lsb, nal->type);
    } else
        s->output_picture_number = ctx->poc = 0;

    if (nal->temporal_id == 0 &&
        nal->type != HEVC_NAL_TRAIL_N &&
        nal->type != HEVC_NAL_TSA_N &&
        nal->type != HEVC_NAL_STSA_N &&
        nal->type != HEVC_NAL_RADL_N &&
        nal->type != HEVC_NAL_RASL_N &&
        nal->type != HEVC_NAL_RADL_R &&
        nal->type != HEVC_NAL_RASL_R)
        ctx->pocTid0 = ctx->poc;

    return 1; /* no need to evaluate the rest */
}

/**
 * Parse NAL units of found picture and decode some basic information.
 *
 * @param s parser context.
 * @param avctx codec context.
 * @param buf buffer with field/frame data.
 * @param buf_size size of the buffer.
 */
static int parse_nal_units(AVCodecParserContext *s, const uint8_t *buf,
                           int buf_size, AVCodecContext *avctx)
{
    HEVCParserContext *ctx = s->priv_data;
    HEVCParamSets *ps = &ctx->ps;
    HEVCSEI *sei = &ctx->sei;
    int flags = (H2645_FLAG_IS_NALFF * !!ctx->is_avc) | H2645_FLAG_SMALL_PADDING;
    int ret, i;

    /* set some sane default values */
    s->pict_type         = AV_PICTURE_TYPE_I;
    s->key_frame         = 0;
    s->picture_structure = AV_PICTURE_STRUCTURE_UNKNOWN;

    ff_hevc_reset_sei(sei);

    ret = ff_h2645_packet_split(&ctx->pkt, buf, buf_size, avctx,
                                ctx->nal_length_size, AV_CODEC_ID_HEVC, flags);
    if (ret < 0)
        return ret;

    for (i = 0; i < ctx->pkt.nb_nals; i++) {
        H2645NAL *nal = &ctx->pkt.nals[i];
        GetBitContext *gb = &nal->gb;

        if (nal->nuh_layer_id > 0)
            continue;

        switch (nal->type) {
        case HEVC_NAL_VPS:
            ff_hevc_decode_nal_vps(gb, avctx, ps);
            break;
        case HEVC_NAL_SPS:
            ff_hevc_decode_nal_sps(gb, avctx, ps, nal->nuh_layer_id, 1);
            break;
        case HEVC_NAL_PPS:
            ff_hevc_decode_nal_pps(gb, avctx, ps);
            break;
        case HEVC_NAL_SEI_PREFIX:
        case HEVC_NAL_SEI_SUFFIX:
            ff_hevc_decode_nal_sei(gb, avctx, sei, ps, nal->type);
            break;
        case HEVC_NAL_TRAIL_N:
        case HEVC_NAL_TRAIL_R:
        case HEVC_NAL_TSA_N:
        case HEVC_NAL_TSA_R:
        case HEVC_NAL_STSA_N:
        case HEVC_NAL_STSA_R:
        case HEVC_NAL_BLA_W_LP:
        case HEVC_NAL_BLA_W_RADL:
        case HEVC_NAL_BLA_N_LP:
        case HEVC_NAL_IDR_W_RADL:
        case HEVC_NAL_IDR_N_LP:
        case HEVC_NAL_CRA_NUT:
        case HEVC_NAL_RADL_N:
        case HEVC_NAL_RADL_R:
        case HEVC_NAL_RASL_N:
        case HEVC_NAL_RASL_R:
            if (ctx->sei.picture_timing.picture_struct == HEVC_SEI_PIC_STRUCT_FRAME_DOUBLING) {
                s->repeat_pict = 1;
            } else if (ctx->sei.picture_timing.picture_struct == HEVC_SEI_PIC_STRUCT_FRAME_TRIPLING) {
                s->repeat_pict = 2;
            }
            ret = hevc_parse_slice_header(s, nal, avctx);
            if (ret)
                return ret;
            break;
        }
    }
    /* didn't find a picture! */
    av_log(avctx, AV_LOG_ERROR, "missing picture in access unit with size %d\n", buf_size);
    return -1;
}

/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or END_NOT_FOUND
 */
static int hevc_find_frame_end(AVCodecParserContext *s, const uint8_t *buf,
                               int buf_size)
{
    HEVCParserContext *ctx = s->priv_data;
    ParseContext       *pc = &ctx->pc;
    int i;

    for (i = 0; i < buf_size; i++) {
        int nut, layer_id;

        pc->state64 = (pc->state64 << 8) | buf[i];

        if (((pc->state64 >> 3 * 8) & 0xFFFFFF) != START_CODE)
            continue;

        nut = (pc->state64 >> 2 * 8 + 1) & 0x3F;

        layer_id = (pc->state64 >> 11) & 0x3F;
        if (layer_id > 0)
            continue;

        // Beginning of access unit
        if ((nut >= HEVC_NAL_VPS && nut <= HEVC_NAL_EOB_NUT) || nut == HEVC_NAL_SEI_PREFIX ||
            (nut >= 41 && nut <= 44) || (nut >= 48 && nut <= 55)) {
            if (pc->frame_start_found) {
                pc->frame_start_found = 0;
                if (!((pc->state64 >> 6 * 8) & 0xFF))
                    return i - 6;
                return i - 5;
            }
        } else if (nut <= HEVC_NAL_RASL_R ||
                   (nut >= HEVC_NAL_BLA_W_LP && nut <= HEVC_NAL_CRA_NUT)) {
            int first_slice_segment_in_pic_flag = buf[i] >> 7;
            if (first_slice_segment_in_pic_flag) {
                if (!pc->frame_start_found) {
                    pc->frame_start_found = 1;
                } else { // First slice of next frame found
                    pc->frame_start_found = 0;
                    if (!((pc->state64 >> 6 * 8) & 0xFF))
                        return i - 6;
                    return i - 5;
                }
            }
        }
    }

    return END_NOT_FOUND;
}

static int hevc_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    int next;
    HEVCParserContext *ctx = s->priv_data;
    ParseContext *pc = &ctx->pc;
    int is_dummy_buf = !buf_size;
    const uint8_t *dummy_buf = buf;

    if (avctx->extradata && !ctx->parsed_extradata) {
        ff_hevc_decode_extradata(avctx->extradata, avctx->extradata_size, &ctx->ps, &ctx->sei,
                                 &ctx->is_avc, &ctx->nal_length_size, avctx->err_recognition,
                                 1, avctx);
        ctx->parsed_extradata = 1;
    }

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = hevc_find_frame_end(s, buf, buf_size);
        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf      = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    is_dummy_buf &= (dummy_buf == buf);

    if (!is_dummy_buf)
        parse_nal_units(s, buf, buf_size, avctx);

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

static void hevc_parser_close(AVCodecParserContext *s)
{
    HEVCParserContext *ctx = s->priv_data;

    ff_hevc_ps_uninit(&ctx->ps);
    ff_h2645_packet_uninit(&ctx->pkt);
    ff_hevc_reset_sei(&ctx->sei);

    av_freep(&ctx->pc.buffer);
}

const AVCodecParser ff_hevc_parser = {
    .codec_ids      = { AV_CODEC_ID_HEVC },
    .priv_data_size = sizeof(HEVCParserContext),
    .parser_parse   = hevc_parse,
    .parser_close   = hevc_parser_close,
};
