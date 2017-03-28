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

#include "golomb.h"
#include "hevc.h"
#include "hevcdec.h"
#include "h2645_parse.h"
#include "parser.h"

#define START_CODE 0x000001 ///< start_code_prefix_one_3bytes

#define IS_IRAP_NAL(nal) (nal->type >= 16 && nal->type <= 23)

#define ADVANCED_PARSER CONFIG_HEVC_DECODER

typedef struct HEVCParserContext {
    ParseContext pc;

    H2645Packet pkt;
    HEVCParamSets ps;

    int parsed_extradata;

#if ADVANCED_PARSER
    HEVCContext h;
#endif
} HEVCParserContext;

#if !ADVANCED_PARSER
static int hevc_parse_slice_header(AVCodecParserContext *s, H2645NAL *nal,
                                   AVCodecContext *avctx)
{
    HEVCParserContext *ctx = s->priv_data;
    GetBitContext *gb = &nal->gb;

    HEVCPPS *pps;
    HEVCSPS *sps;
    unsigned int pps_id;

    get_bits1(gb);          // first slice in pic
    if (IS_IRAP_NAL(nal))
        get_bits1(gb);      // no output of prior pics

    pps_id = get_ue_golomb_long(gb);
    if (pps_id >= HEVC_MAX_PPS_COUNT || !ctx->ps.pps_list[pps_id]) {
        av_log(avctx, AV_LOG_ERROR, "PPS id out of range: %d\n", pps_id);
        return AVERROR_INVALIDDATA;
    }
    pps = (HEVCPPS*)ctx->ps.pps_list[pps_id]->data;
    sps = (HEVCSPS*)ctx->ps.sps_list[pps->sps_id]->data;

    /* export the stream parameters */
    s->coded_width  = sps->width;
    s->coded_height = sps->height;
    s->width        = sps->output_width;
    s->height       = sps->output_height;
    s->format       = sps->pix_fmt;
    avctx->profile  = sps->ptl.general_ptl.profile_idc;
    avctx->level    = sps->ptl.general_ptl.level_idc;

    /* ignore the rest for now*/

    return 0;
}

static int parse_nal_units(AVCodecParserContext *s, const uint8_t *buf,
                           int buf_size, AVCodecContext *avctx)
{
    HEVCParserContext *ctx = s->priv_data;
    int ret, i;

    ret = ff_h2645_packet_split(&ctx->pkt, buf, buf_size, avctx, 0, 0,
                                AV_CODEC_ID_HEVC, 1);
    if (ret < 0)
        return ret;

    for (i = 0; i < ctx->pkt.nb_nals; i++) {
        H2645NAL *nal = &ctx->pkt.nals[i];

        /* ignore everything except parameter sets and VCL NALUs */
        switch (nal->type) {
        case HEVC_NAL_VPS: ff_hevc_decode_nal_vps(&nal->gb, avctx, &ctx->ps);    break;
        case HEVC_NAL_SPS: ff_hevc_decode_nal_sps(&nal->gb, avctx, &ctx->ps, 1); break;
        case HEVC_NAL_PPS: ff_hevc_decode_nal_pps(&nal->gb, avctx, &ctx->ps);    break;
        case HEVC_NAL_TRAIL_R:
        case HEVC_NAL_TRAIL_N:
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
            if (buf == avctx->extradata) {
                av_log(avctx, AV_LOG_ERROR, "Invalid NAL unit: %d\n", nal->type);
                return AVERROR_INVALIDDATA;
            }
            hevc_parse_slice_header(s, nal, avctx);
            break;
        }
    }

    return 0;
}
#endif

/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or END_NOT_FOUND
 */
static int hevc_find_frame_end(AVCodecParserContext *s, const uint8_t *buf,
                               int buf_size)
{
    int i;
    ParseContext *pc = s->priv_data;

    for (i = 0; i < buf_size; i++) {
        int nut;

        pc->state64 = (pc->state64 << 8) | buf[i];

        if (((pc->state64 >> 3 * 8) & 0xFFFFFF) != START_CODE)
            continue;

        nut = (pc->state64 >> 2 * 8 + 1) & 0x3F;
        // Beginning of access unit
        if ((nut >= HEVC_NAL_VPS && nut <= HEVC_NAL_AUD) || nut == HEVC_NAL_SEI_PREFIX ||
            (nut >= 41 && nut <= 44) || (nut >= 48 && nut <= 55)) {
            if (pc->frame_start_found) {
                pc->frame_start_found = 0;
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
                    return i - 5;
                }
            }
        }
    }

    return END_NOT_FOUND;
}

#if ADVANCED_PARSER
/**
 * Parse NAL units of found picture and decode some basic information.
 *
 * @param s parser context.
 * @param avctx codec context.
 * @param buf buffer with field/frame data.
 * @param buf_size size of the buffer.
 */
static inline int parse_nal_units(AVCodecParserContext *s, const uint8_t *buf,
                           int buf_size, AVCodecContext *avctx)
{
    HEVCParserContext *ctx = s->priv_data;
    HEVCContext       *h   = &ctx->h;
    GetBitContext      *gb;
    SliceHeader        *sh = &h->sh;
    HEVCParamSets *ps = &h->ps;
    H2645Packet   *pkt = &ctx->pkt;
    const uint8_t *buf_end = buf + buf_size;
    int state = -1, i;
    H2645NAL *nal;
    int is_global = buf == avctx->extradata;

    if (!h->HEVClc)
        h->HEVClc = av_mallocz(sizeof(HEVCLocalContext));
    if (!h->HEVClc)
        return AVERROR(ENOMEM);

    gb = &h->HEVClc->gb;

    /* set some sane default values */
    s->pict_type         = AV_PICTURE_TYPE_I;
    s->key_frame         = 0;
    s->picture_structure = AV_PICTURE_STRUCTURE_UNKNOWN;

    h->avctx = avctx;

    ff_hevc_reset_sei(h);

    if (!buf_size)
        return 0;

    if (pkt->nals_allocated < 1) {
        H2645NAL *tmp = av_realloc_array(pkt->nals, 1, sizeof(*tmp));
        if (!tmp)
            return AVERROR(ENOMEM);
        pkt->nals = tmp;
        memset(pkt->nals, 0, sizeof(*tmp));
        pkt->nals_allocated = 1;
    }

    nal = &pkt->nals[0];

    for (;;) {
        int src_length, consumed;
        int ret;
        int num = 0, den = 0;
        buf = avpriv_find_start_code(buf, buf_end, &state);
        if (--buf + 2 >= buf_end)
            break;
        src_length = buf_end - buf;

        h->nal_unit_type = (*buf >> 1) & 0x3f;
        h->temporal_id   = (*(buf + 1) & 0x07) - 1;
        if (h->nal_unit_type <= HEVC_NAL_CRA_NUT) {
            // Do not walk the whole buffer just to decode slice segment header
            if (src_length > 20)
                src_length = 20;
        }

        consumed = ff_h2645_extract_rbsp(buf, src_length, nal, 1);
        if (consumed < 0)
            return consumed;

        ret = init_get_bits8(gb, nal->data + 2, nal->size);
        if (ret < 0)
            return ret;

        switch (h->nal_unit_type) {
        case HEVC_NAL_VPS:
            ff_hevc_decode_nal_vps(gb, avctx, ps);
            break;
        case HEVC_NAL_SPS:
            ff_hevc_decode_nal_sps(gb, avctx, ps, 1);
            break;
        case HEVC_NAL_PPS:
            ff_hevc_decode_nal_pps(gb, avctx, ps);
            break;
        case HEVC_NAL_SEI_PREFIX:
        case HEVC_NAL_SEI_SUFFIX:
            ff_hevc_decode_nal_sei(h);
            break;
        case HEVC_NAL_TRAIL_N:
        case HEVC_NAL_TRAIL_R:
        case HEVC_NAL_TSA_N:
        case HEVC_NAL_TSA_R:
        case HEVC_NAL_STSA_N:
        case HEVC_NAL_STSA_R:
        case HEVC_NAL_RADL_N:
        case HEVC_NAL_RADL_R:
        case HEVC_NAL_RASL_N:
        case HEVC_NAL_RASL_R:
        case HEVC_NAL_BLA_W_LP:
        case HEVC_NAL_BLA_W_RADL:
        case HEVC_NAL_BLA_N_LP:
        case HEVC_NAL_IDR_W_RADL:
        case HEVC_NAL_IDR_N_LP:
        case HEVC_NAL_CRA_NUT:

            if (is_global) {
                av_log(avctx, AV_LOG_ERROR, "Invalid NAL unit: %d\n", h->nal_unit_type);
                return AVERROR_INVALIDDATA;
            }

            sh->first_slice_in_pic_flag = get_bits1(gb);
            s->picture_structure = h->picture_struct;
            s->field_order = h->picture_struct;

            if (IS_IRAP(h)) {
                s->key_frame = 1;
                sh->no_output_of_prior_pics_flag = get_bits1(gb);
            }

            sh->pps_id = get_ue_golomb(gb);
            if (sh->pps_id >= HEVC_MAX_PPS_COUNT || !ps->pps_list[sh->pps_id]) {
                av_log(avctx, AV_LOG_ERROR, "PPS id out of range: %d\n", sh->pps_id);
                return AVERROR_INVALIDDATA;
            }
            ps->pps = (HEVCPPS*)ps->pps_list[sh->pps_id]->data;

            if (ps->pps->sps_id >= HEVC_MAX_SPS_COUNT || !ps->sps_list[ps->pps->sps_id]) {
                av_log(avctx, AV_LOG_ERROR, "SPS id out of range: %d\n", ps->pps->sps_id);
                return AVERROR_INVALIDDATA;
            }
            if (ps->sps != (HEVCSPS*)ps->sps_list[ps->pps->sps_id]->data) {
                ps->sps = (HEVCSPS*)ps->sps_list[ps->pps->sps_id]->data;
                ps->vps = (HEVCVPS*)ps->vps_list[ps->sps->vps_id]->data;
            }

            s->coded_width  = ps->sps->width;
            s->coded_height = ps->sps->height;
            s->width        = ps->sps->output_width;
            s->height       = ps->sps->output_height;
            s->format       = ps->sps->pix_fmt;
            avctx->profile  = ps->sps->ptl.general_ptl.profile_idc;
            avctx->level    = ps->sps->ptl.general_ptl.level_idc;

            if (ps->vps->vps_timing_info_present_flag) {
                num = ps->vps->vps_num_units_in_tick;
                den = ps->vps->vps_time_scale;
            } else if (ps->sps->vui.vui_timing_info_present_flag) {
                num = ps->sps->vui.vui_num_units_in_tick;
                den = ps->sps->vui.vui_time_scale;
            }

            if (num != 0 && den != 0)
                av_reduce(&avctx->framerate.den, &avctx->framerate.num,
                          num, den, 1 << 30);

            if (!sh->first_slice_in_pic_flag) {
                int slice_address_length;

                if (ps->pps->dependent_slice_segments_enabled_flag)
                    sh->dependent_slice_segment_flag = get_bits1(gb);
                else
                    sh->dependent_slice_segment_flag = 0;

                slice_address_length = av_ceil_log2_c(ps->sps->ctb_width *
                                                      ps->sps->ctb_height);
                sh->slice_segment_addr = get_bitsz(gb, slice_address_length);
                if (sh->slice_segment_addr >= ps->sps->ctb_width * ps->sps->ctb_height) {
                    av_log(avctx, AV_LOG_ERROR, "Invalid slice segment address: %u.\n",
                           sh->slice_segment_addr);
                    return AVERROR_INVALIDDATA;
                }
            } else
                sh->dependent_slice_segment_flag = 0;

            if (sh->dependent_slice_segment_flag)
                break;

            for (i = 0; i < ps->pps->num_extra_slice_header_bits; i++)
                skip_bits(gb, 1); // slice_reserved_undetermined_flag[]

            sh->slice_type = get_ue_golomb(gb);
            if (!(sh->slice_type == HEVC_SLICE_I || sh->slice_type == HEVC_SLICE_P ||
                  sh->slice_type == HEVC_SLICE_B)) {
                av_log(avctx, AV_LOG_ERROR, "Unknown slice type: %d.\n",
                       sh->slice_type);
                return AVERROR_INVALIDDATA;
            }
            s->pict_type = sh->slice_type == HEVC_SLICE_B ? AV_PICTURE_TYPE_B :
                           sh->slice_type == HEVC_SLICE_P ? AV_PICTURE_TYPE_P :
                                                       AV_PICTURE_TYPE_I;

            if (ps->pps->output_flag_present_flag)
                sh->pic_output_flag = get_bits1(gb);

            if (ps->sps->separate_colour_plane_flag)
                sh->colour_plane_id = get_bits(gb, 2);

            if (!IS_IDR(h)) {
                sh->pic_order_cnt_lsb = get_bits(gb, ps->sps->log2_max_poc_lsb);
                s->output_picture_number = h->poc = ff_hevc_compute_poc(h, sh->pic_order_cnt_lsb);
            } else
                s->output_picture_number = h->poc = 0;

            if (h->temporal_id == 0 &&
                h->nal_unit_type != HEVC_NAL_TRAIL_N &&
                h->nal_unit_type != HEVC_NAL_TSA_N &&
                h->nal_unit_type != HEVC_NAL_STSA_N &&
                h->nal_unit_type != HEVC_NAL_RADL_N &&
                h->nal_unit_type != HEVC_NAL_RASL_N &&
                h->nal_unit_type != HEVC_NAL_RADL_R &&
                h->nal_unit_type != HEVC_NAL_RASL_R)
                h->pocTid0 = h->poc;

            return 0; /* no need to evaluate the rest */
        }
        buf += consumed;
    }
    /* didn't find a picture! */
    if (!is_global)
        av_log(h->avctx, AV_LOG_ERROR, "missing picture in access unit\n");
    return -1;
}
#endif

static int hevc_parse(AVCodecParserContext *s,
                      AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    int next;
    HEVCParserContext *ctx = s->priv_data;
    ParseContext *pc = &ctx->pc;

    if (avctx->extradata && !ctx->parsed_extradata) {
        parse_nal_units(s, avctx->extradata, avctx->extradata_size, avctx);
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

    parse_nal_units(s, buf, buf_size, avctx);

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

// Split after the parameter sets at the beginning of the stream if they exist.
static int hevc_split(AVCodecContext *avctx, const uint8_t *buf, int buf_size)
{
    const uint8_t *ptr = buf, *end = buf + buf_size;
    uint32_t state = -1;
    int has_vps = 0;
    int has_sps = 0;
    int has_pps = 0;
    int nut;

    while (ptr < end) {
        ptr = avpriv_find_start_code(ptr, end, &state);
        if ((state >> 8) != START_CODE)
            break;
        nut = (state >> 1) & 0x3F;
        if (nut == HEVC_NAL_VPS)
            has_vps = 1;
        else if (nut == HEVC_NAL_SPS)
            has_sps = 1;
        else if (nut == HEVC_NAL_PPS)
            has_pps = 1;
        else if ((nut != HEVC_NAL_SEI_PREFIX || has_pps) &&
                  nut != HEVC_NAL_AUD) {
            if (has_vps && has_sps) {
                while (ptr - 4 > buf && ptr[-5] == 0)
                    ptr--;
                return ptr - 4 - buf;
            }
        }
    }
    return 0;
}

static void hevc_parser_close(AVCodecParserContext *s)
{
    HEVCParserContext *ctx = s->priv_data;
    int i;

#if ADVANCED_PARSER
    HEVCContext  *h  = &ctx->h;

    for (i = 0; i < FF_ARRAY_ELEMS(h->ps.vps_list); i++)
        av_buffer_unref(&h->ps.vps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(h->ps.sps_list); i++)
        av_buffer_unref(&h->ps.sps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(h->ps.pps_list); i++)
        av_buffer_unref(&h->ps.pps_list[i]);

    h->ps.sps = NULL;

    av_freep(&h->HEVClc);
#endif

    for (i = 0; i < FF_ARRAY_ELEMS(ctx->ps.vps_list); i++)
        av_buffer_unref(&ctx->ps.vps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ctx->ps.sps_list); i++)
        av_buffer_unref(&ctx->ps.sps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ctx->ps.pps_list); i++)
        av_buffer_unref(&ctx->ps.pps_list[i]);

    ctx->ps.sps = NULL;

    ff_h2645_packet_uninit(&ctx->pkt);

    av_freep(&ctx->pc.buffer);
}

AVCodecParser ff_hevc_parser = {
    .codec_ids      = { AV_CODEC_ID_HEVC },
    .priv_data_size = sizeof(HEVCParserContext),
    .parser_parse   = hevc_parse,
    .parser_close   = hevc_parser_close,
    .split          = hevc_split,
};
