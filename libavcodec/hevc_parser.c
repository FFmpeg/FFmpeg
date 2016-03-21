/*
 * HEVC Annex B format parser
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/common.h"

#include "golomb.h"
#include "hevc.h"
#include "h2645_parse.h"
#include "parser.h"

#define START_CODE 0x000001 ///< start_code_prefix_one_3bytes

#define IS_IRAP_NAL(nal) (nal->type >= 16 && nal->type <= 23)

typedef struct HEVCParserContext {
    ParseContext pc;

    H2645Packet pkt;
    HEVCParamSets ps;

    int parsed_extradata;
} HEVCParserContext;

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
    if (pps_id >= MAX_PPS_COUNT || !ctx->ps.pps_list[pps_id]) {
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
                                AV_CODEC_ID_HEVC);
    if (ret < 0)
        return ret;

    for (i = 0; i < ctx->pkt.nb_nals; i++) {
        H2645NAL *nal = &ctx->pkt.nals[i];

        /* ignore everything except parameter sets and VCL NALUs */
        switch (nal->type) {
        case NAL_VPS: ff_hevc_decode_nal_vps(&nal->gb, avctx, &ctx->ps);    break;
        case NAL_SPS: ff_hevc_decode_nal_sps(&nal->gb, avctx, &ctx->ps, 1); break;
        case NAL_PPS: ff_hevc_decode_nal_pps(&nal->gb, avctx, &ctx->ps);    break;
        case NAL_TRAIL_R:
        case NAL_TRAIL_N:
        case NAL_TSA_N:
        case NAL_TSA_R:
        case NAL_STSA_N:
        case NAL_STSA_R:
        case NAL_BLA_W_LP:
        case NAL_BLA_W_RADL:
        case NAL_BLA_N_LP:
        case NAL_IDR_W_RADL:
        case NAL_IDR_N_LP:
        case NAL_CRA_NUT:
        case NAL_RADL_N:
        case NAL_RADL_R:
        case NAL_RASL_N:
        case NAL_RASL_R: hevc_parse_slice_header(s, nal, avctx); break;
        }
    }

    return 0;
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
        int nut;

        pc->state64 = (pc->state64 << 8) | buf[i];

        if (((pc->state64 >> 3 * 8) & 0xFFFFFF) != START_CODE)
            continue;

        nut = (pc->state64 >> 2 * 8 + 1) & 0x3F;
        // Beginning of access unit
        if ((nut >= NAL_VPS && nut <= NAL_AUD) || nut == NAL_SEI_PREFIX ||
            (nut >= 41 && nut <= 44) || (nut >= 48 && nut <= 55)) {
            if (pc->frame_start_found) {
                pc->frame_start_found = 0;
                return i - 5;
            }
        } else if (nut <= NAL_RASL_R ||
                   (nut >= NAL_BLA_W_LP && nut <= NAL_CRA_NUT)) {
            int first_slice_segment_in_pic_flag = buf[i] >> 7;
            if (first_slice_segment_in_pic_flag) {
                if (!pc->frame_start_found) {
                    pc->frame_start_found = 1;
                    s->key_frame = nut >= NAL_BLA_W_LP && nut <= NAL_CRA_NUT;
                } else { // First slice of next frame found
                    pc->frame_start_found = 0;
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
    int i;
    uint32_t state = -1;
    int has_ps = 0;

    for (i = 0; i < buf_size; i++) {
        state = (state << 8) | buf[i];
        if (((state >> 8) & 0xFFFFFF) == START_CODE) {
            int nut = (state >> 1) & 0x3F;
            if (nut >= NAL_VPS && nut <= NAL_PPS)
                has_ps = 1;
            else if (has_ps)
                return i - 3;
            else // no parameter set at the beginning of the stream
                return 0;
        }
    }
    return 0;
}

static void hevc_parser_close(AVCodecParserContext *s)
{
    HEVCParserContext *ctx = s->priv_data;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(ctx->ps.vps_list); i++)
        av_buffer_unref(&ctx->ps.vps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ctx->ps.sps_list); i++)
        av_buffer_unref(&ctx->ps.sps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ctx->ps.pps_list); i++)
        av_buffer_unref(&ctx->ps.pps_list[i]);

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
