/*
 * AVS3-P2/IEEE1857.10 video parser.
 * Copyright (c) 2020 Zhenyu Wang <wangzhenyu@pkusz.edu.cn>
 *                    Bingjie Han <hanbj@pkusz.edu.cn>
 *                    Huiwen Ren  <hwrenx@gmail.com>
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

#include "avs3.h"
#include "get_bits.h"
#include "parser.h"

static int avs3_find_frame_end(ParseContext *pc, const uint8_t *buf, int buf_size)
{
    int pic_found  = pc->frame_start_found;
    uint32_t state = pc->state;
    int cur = 0;

    if (!pic_found) {
        for (; cur < buf_size; ++cur) {
            state = (state << 8) | buf[cur];
            if (AVS3_ISPIC(buf[cur])){
                cur++;
                pic_found = 1;
                break;
            }
        }
    }

    if (pic_found) {
        if (!buf_size)
            return END_NOT_FOUND;
        for (; cur < buf_size; ++cur) {
            state = (state << 8) | buf[cur];
            if ((state & 0xFFFFFF00) == 0x100 && AVS3_ISUNIT(state & 0xFF)) {
                pc->frame_start_found = 0;
                pc->state = -1;
                return cur - 3;
            }
        }
    }

    pc->frame_start_found = pic_found;
    pc->state = state;

    return END_NOT_FOUND;
}

static void parse_avs3_nal_units(AVCodecParserContext *s, const uint8_t *buf,
                           int buf_size, AVCodecContext *avctx)
{
    if (buf_size < 5) {
        return;
    }

    if (buf[0] == 0x0 && buf[1] == 0x0 && buf[2] == 0x1) {
        if (buf[3] == AVS3_SEQ_START_CODE) {
            GetBitContext gb;
            int profile, ratecode;

            init_get_bits8(&gb, buf + 4, buf_size - 4);

            s->key_frame = 1;
            s->pict_type = AV_PICTURE_TYPE_I;

            profile = get_bits(&gb, 8);
            // Skip bits: level(8)
            //            progressive(1)
            //            field(1)
            //            library(2)
            //            resv(1)
            //            width(14)
            //            resv(1)
            //            height(14)
            //            chroma(2)
            //            sampe_precision(3)
            skip_bits(&gb, 47);

            if (profile == AVS3_PROFILE_BASELINE_MAIN10) {
                int sample_precision = get_bits(&gb, 3);
                if (sample_precision == 1) {
                    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
                } else if (sample_precision == 2) {
                    avctx->pix_fmt = AV_PIX_FMT_YUV420P10LE;
                } else {
                    avctx->pix_fmt = AV_PIX_FMT_NONE;
                }
            }

            // Skip bits: resv(1)
            //            aspect(4)
            skip_bits(&gb, 5);

            ratecode = get_bits(&gb, 4);

            // Skip bits: resv(1)
            //            bitrate_low(18)
            //            resv(1)
            //            bitrate_high(12)
            skip_bits(&gb, 32);

            avctx->has_b_frames = !get_bits(&gb, 1);

            avctx->framerate.num = avctx->time_base.den = ff_avs3_frame_rate_tab[ratecode].num;
            avctx->framerate.den = avctx->time_base.num = ff_avs3_frame_rate_tab[ratecode].den;

            s->width  = s->coded_width = avctx->width;
            s->height = s->coded_height = avctx->height;

            av_log(avctx, AV_LOG_DEBUG,
                   "AVS3 parse seq HDR: profile %d; coded size: %dx%d; frame rate code: %d\n",
                   profile, avctx->width, avctx->height, ratecode);

        } else if (buf[3] == AVS3_INTRA_PIC_START_CODE) {
            s->key_frame = 1;
            s->pict_type = AV_PICTURE_TYPE_I;
        } else if (buf[3] == AVS3_INTER_PIC_START_CODE){
            s->key_frame = 0;
            if (buf_size > 9) {
                int pic_code_type = buf[8] & 0x3;
                if (pic_code_type == 1 || pic_code_type == 3) {
                    s->pict_type = AV_PICTURE_TYPE_P;
                } else {
                    s->pict_type = AV_PICTURE_TYPE_B;
                }
            }
        }
    }
}


static int avs3_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    int next;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES)  {
        next = buf_size;
    } else {
        next = avs3_find_frame_end(pc, buf, buf_size);
        if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    parse_avs3_nal_units(s, buf, buf_size, avctx);

    *poutbuf = buf;
    *poutbuf_size = buf_size;

    return next;
}

const AVCodecParser ff_avs3_parser = {
    .codec_ids      = { AV_CODEC_ID_AVS3 },
    .priv_data_size = sizeof(ParseContext),
    .parser_parse   = avs3_parse,
    .parser_close   = ff_parse_close,
};
