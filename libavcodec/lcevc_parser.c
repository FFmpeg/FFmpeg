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

#include <stdint.h>

#include "libavutil/mem.h"

#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "h2645_parse.h"
#include "lcevc.h"
#include "lcevc_parse.h"
#include "parser.h"
#include "parser_internal.h"

#define START_CODE 0x000001 ///< start_code_prefix_one_3bytes

typedef struct LCEVCParserContext {
    ParseContext pc;

    H2645Packet pkt;

    int parsed_extradata;
    int is_lvcc;
    int nal_length_size;
} LCEVCParserContext;

static int lcevc_find_frame_end(AVCodecParserContext *s, const uint8_t *buf,
                                int buf_size)
{
    LCEVCParserContext *ctx = s->priv_data;
    ParseContext *pc = &ctx->pc;

    for (int i = 0; i < buf_size; i++) {
        int nut;

        pc->state = (pc->state << 8) | buf[i];

        if (((pc->state >> 8) & 0xFFFFFF) != START_CODE)
            continue;

        nut = (pc->state >> 1) & 0x1F;

        // Beginning of access unit
        if (nut == LCEVC_IDR_NUT || nut == LCEVC_NON_IDR_NUT) {
            if (!pc->frame_start_found)
                pc->frame_start_found = 1;
            else {
                pc->frame_start_found = 0;
                return i - 3;
            }
        }
    }

    return END_NOT_FOUND;
}

static const enum AVPixelFormat pix_fmts[4][4] = {
    { AV_PIX_FMT_GRAY8,     AV_PIX_FMT_YUV420P,
      AV_PIX_FMT_YUV422P,   AV_PIX_FMT_YUV444P, },
    { AV_PIX_FMT_GRAY10,    AV_PIX_FMT_YUV420P10,
      AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10, },
    { AV_PIX_FMT_GRAY12,    AV_PIX_FMT_YUV420P12,
      AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, },
    { AV_PIX_FMT_GRAY14,    AV_PIX_FMT_YUV420P14,
      AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14, },
};

static const struct {
    int width;
    int height;
} resolution_type_lut[63] = {
    { 0, 0},
    { 360,  200 },  { 400,  240 },  { 480,  320 },  { 640,  360 },
    { 640,  480 },  { 768,  480 },  { 800,  600 },  { 852,  480 },
    { 854,  480 },  { 856,  480 },  { 960,  540 },  { 960,  640 },
    { 1024, 576 },  { 1024, 600 },  { 1024, 768 },  { 1152, 864 },
    { 1280, 720 },  { 1280, 800 },  { 1280, 1024 }, { 1360, 768 },
    { 1366, 768 },  { 1920, 1200 }, { 2048, 1080 }, { 2048, 1152 },
    { 2048, 1536 }, { 2160, 1440 }, { 2560, 1440 }, { 2560, 1600 },
    { 2560, 2048 }, { 3200, 1800 }, { 3200, 2048 }, { 3200, 2400 },
    { 3440, 1440 }, { 3840, 1600 }, { 3840, 2160 }, { 3840, 2400 },
    { 4096, 2160 }, { 4096, 3072 }, { 5120, 2880 }, { 5120, 3200 },
    { 5120, 4096 }, { 6400, 4096 }, { 6400, 4800 }, { 7680, 4320 },
    { 7680, 4800 },
};

static int parse_nal_unit(AVCodecParserContext *s, AVCodecContext *avctx,
                          const H2645NAL *nal)
{
    GetByteContext gbc;
    bytestream2_init(&gbc, nal->data, nal->size);
    bytestream2_skip(&gbc, 2);

    while (bytestream2_get_bytes_left(&gbc) > 1) {
        GetBitContext gb;
        uint64_t payload_size;
        int payload_size_type, payload_type;
        int block_size;

        int ret = init_get_bits8(&gb, gbc.buffer, bytestream2_get_bytes_left(&gbc));
        if (ret < 0)
            return ret;

        payload_size_type = get_bits(&gb, 3);
        payload_type      = get_bits(&gb, 5);
        payload_size      = payload_size_type;
        if (payload_size_type == 6)
            return AVERROR_PATCHWELCOME;
        if (payload_size_type == 7)
            payload_size = get_mb(&gb);

        if (payload_size > INT_MAX - (get_bits_count(&gb) >> 3))
            return AVERROR_INVALIDDATA;

        block_size = payload_size + (get_bits_count(&gb) >> 3);
        if (block_size >= bytestream2_get_bytes_left(&gbc))
            return AVERROR_INVALIDDATA;

        switch (payload_type) {
        case LCEVC_PAYLOAD_TYPE_SEQUENCE_CONFIG:
            avctx->profile = get_bits(&gb, 4);
            avctx->level = get_bits(&gb, 4);
            break;
        case LCEVC_PAYLOAD_TYPE_GLOBAL_CONFIG: {
            int resolution_type, chroma_format_idc, bit_depth;
            int processed_planes_type_flag;

            processed_planes_type_flag = get_bits1(&gb);
            resolution_type = get_bits(&gb, 6);
            skip_bits1(&gb);
            chroma_format_idc = get_bits(&gb, 2);
            skip_bits(&gb, 2);
            bit_depth = get_bits(&gb, 2); // enhancement_depth_type

            s->format = pix_fmts[bit_depth][chroma_format_idc];

            if (resolution_type < 63) {
                s->width  = resolution_type_lut[resolution_type].width;
                s->height = resolution_type_lut[resolution_type].height;
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

                s->width  = get_bits(&gb, 16);
                s->height = get_bits(&gb, 16);
            }
            break;
        }
        default:
            break;
        }

        bytestream2_skip(&gbc, block_size);
    }

    return 0;
}

static int parse_nal_units(AVCodecParserContext *s, const uint8_t *buf,
                           int buf_size, AVCodecContext *avctx)
{
    LCEVCParserContext *ctx = s->priv_data;
    int flags = (H2645_FLAG_IS_NALFF * !!ctx->is_lvcc) | H2645_FLAG_SMALL_PADDING;
    int ret, i;

    /* set some sane default values */
    s->pict_type         = AV_PICTURE_TYPE_NONE;
    s->key_frame         = 0;
    s->picture_structure = AV_PICTURE_STRUCTURE_UNKNOWN;

    ret = ff_h2645_packet_split(&ctx->pkt, buf, buf_size, avctx,
                                ctx->nal_length_size, AV_CODEC_ID_LCEVC, flags);
    if (ret < 0)
        return ret;

    for (i = 0; i < ctx->pkt.nb_nals; i++) {
        H2645NAL *nal = &ctx->pkt.nals[i];

        switch (nal->type) {
        case LCEVC_IDR_NUT:
            s->key_frame = 1;
        // fall-through
        case LCEVC_NON_IDR_NUT:
            parse_nal_unit(s, avctx, nal);
            break;
        default:
            break;
        }
    }

    return 0;
}

static int lcevc_parse(AVCodecParserContext *s,
                      AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    LCEVCParserContext *ctx = s->priv_data;
    ParseContext *pc = &ctx->pc;
    int next;

    if (!ctx->parsed_extradata && avctx->extradata_size > 4) {
        ctx->parsed_extradata = 1;
        ctx->is_lvcc = !!avctx->extradata[0];

        if (ctx->is_lvcc)
            ctx->nal_length_size = (avctx->extradata[4] >> 6) + 1;
    }

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = lcevc_find_frame_end(s, buf, buf_size);
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

static void lcevc_parser_close(AVCodecParserContext *s)
{
    LCEVCParserContext *ctx = s->priv_data;

    ff_h2645_packet_uninit(&ctx->pkt);

    av_freep(&ctx->pc.buffer);
}

const FFCodecParser ff_lcevc_parser = {
    PARSER_CODEC_LIST(AV_CODEC_ID_LCEVC),
    .priv_data_size = sizeof(LCEVCParserContext),
    .parse          = lcevc_parse,
    .close          = lcevc_parser_close,
};
