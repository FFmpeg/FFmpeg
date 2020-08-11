/*
 * AVS2-P2/IEEE1857.4 video parser.
 * Copyright (c) 2018  Huiwen Ren <hwrenx@gmail.com>
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

#include "parser.h"

#define SLICE_MAX_START_CODE    0x000001af

#define ISPIC(x)  ((x) == 0xB3 || (x) == 0xB6)
#define ISUNIT(x) ((x) == 0xB0 || ISPIC(x))

static av_cold int avs3_find_frame_end(ParseContext *pc, const uint8_t *buf, int buf_size)
{
    int pic_found  = pc->frame_start_found;
    uint32_t state = pc->state;
    int cur = 0;

    if (!pic_found) {
        for (; cur < buf_size; ++cur) {
            state = (state<<8) | buf[cur];
            if (ISPIC(buf[cur])){
                ++cur;
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
            if ((state & 0xFFFFFF00) == 0x100 && ISUNIT(state & 0xFF)) {
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

static unsigned int read_bits(const char** ppbuf, int *pidx, int bits) 
{
    const char* p = *ppbuf;
    int idx = *pidx;
    unsigned int val = 0;
    
    while (bits) {
        bits--;
        val = (val << 1) | (((*p) >> idx) & 0x1);
        if (--idx < 0) {
            idx = 7;
            p++;
        }
    }

    *ppbuf = p;
    *pidx = idx;

    return val;
}

static void parse_avs3_nal_units(AVCodecParserContext *s, const uint8_t *buf,
                           int buf_size, AVCodecContext *avctx)
{
    if (buf_size < 5) {
        return;
    }

    if (buf[0] == 0x0 && buf[1] == 0x0 && buf[2] == 0x1) {
        if (buf[3] == 0xB0) {
            static const int avs3_fps_num[9] = {0, 240000, 24, 25, 30000, 30, 50, 60000, 60 };
            static const int avs3_fps_den[9] = {1,   1001,  1,  1,  1001,  1,  1,  1001,  1 };
            int profile,ratecode;
            const char* p = buf + 4;
            int idx = 7;
            
            s->key_frame = 1;
            s->pict_type = AV_PICTURE_TYPE_I;

            profile = read_bits(&p, &idx, 8);
            // level(8) + progressive(1) + field(1) + library(2) + resv(1) + width(14) + resv(1) + height(14) + chroma(2) + sampe_precision(3)
            read_bits(&p, &idx, 47); 
        
            if (profile == 0x22) {
                avctx->pix_fmt = read_bits(&p, &idx, 3) == 1 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV420P10LE;
            } 

            // resv(1) + aspect(4)
            read_bits(&p, &idx, 5);

            ratecode = read_bits(&p, &idx, 4);

            // resv(1) + bitrate_low(18) + resv(1) + bitrate_high(12)
            read_bits(&p, &idx, 32);

            avctx->has_b_frames = !read_bits(&p, &idx, 1);

            avctx->framerate.num = avctx->time_base.den = avs3_fps_num[ratecode];
            avctx->framerate.den = avctx->time_base.num = avs3_fps_den[ratecode];

            s->width  = s->coded_width = avctx->width;
            s->height = s->coded_height = avctx->height;
            
            av_log(avctx, AV_LOG_DEBUG,
                       "avs3 parse seq hdr: profile %d; coded wxh: %dx%d; "
                       "frame_rate_code %d\n", profile, avctx->width, avctx->height, ratecode);

        } else if (buf[3] == 0xB3) {
            s->key_frame = 1;
            s->pict_type = AV_PICTURE_TYPE_I;
        } else if (buf[3] == 0xB6){
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

AVCodecParser ff_avs3_parser = {
    .codec_ids      = { AV_CODEC_ID_AVS3 },
    .priv_data_size = sizeof(ParseContext),
    .parser_parse   = avs3_parse,
    .parser_close   = ff_parse_close,
    .split          = ff_mpeg4video_split,
};
