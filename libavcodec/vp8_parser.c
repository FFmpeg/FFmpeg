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

#include "libavutil/intreadwrite.h"

#include "avcodec.h"

static int parse(AVCodecParserContext *s,
                 AVCodecContext *avctx,
                 const uint8_t **poutbuf, int *poutbuf_size,
                 const uint8_t *buf, int buf_size)
{
    unsigned int frame_type;
    unsigned int profile;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    if (buf_size < 3)
        return buf_size;

    frame_type = buf[0] & 1;
    profile    = (buf[0] >> 1) & 7;
    if (profile > 3) {
        av_log(avctx, AV_LOG_ERROR, "Invalid profile %u.\n", profile);
        return buf_size;
    }

    avctx->profile = profile;
    s->key_frame   = frame_type == 0;
    s->pict_type   = frame_type ? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
    s->format      = AV_PIX_FMT_YUV420P;
    s->field_order = AV_FIELD_PROGRESSIVE;
    s->picture_structure = AV_PICTURE_STRUCTURE_FRAME;

    if (frame_type == 0) {
        unsigned int sync_code;
        unsigned int width, height;

        if (buf_size < 10)
            return buf_size;

        sync_code = AV_RL24(buf + 3);
        if (sync_code != 0x2a019d) {
            av_log(avctx, AV_LOG_ERROR, "Invalid sync code %06x.\n", sync_code);
            return buf_size;
        }

        width  = AV_RL16(buf + 6) & 0x3fff;
        height = AV_RL16(buf + 8) & 0x3fff;

        s->width        = width;
        s->height       = height;
        s->coded_width  = FFALIGN(width,  16);
        s->coded_height = FFALIGN(height, 16);
    }

    return buf_size;
}

AVCodecParser ff_vp8_parser = {
    .codec_ids    = { AV_CODEC_ID_VP8 },
    .parser_parse = parse,
};
