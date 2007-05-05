/*
 * MPEG4 Video frame extraction
 * Copyright (c) 2003 Fabrice Bellard.
 * Copyright (c) 2003 Michael Niedermayer.
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
#include "mpegvideo.h"


/* XXX: make it use less memory */
static int av_mpeg4_decode_header(AVCodecParserContext *s1,
                                  AVCodecContext *avctx,
                                  const uint8_t *buf, int buf_size)
{
    ParseContext1 *pc = s1->priv_data;
    MpegEncContext *s = pc->enc;
    GetBitContext gb1, *gb = &gb1;
    int ret;

    s->avctx = avctx;
    s->current_picture_ptr = &s->current_picture;

    if (avctx->extradata_size && pc->first_picture){
        init_get_bits(gb, avctx->extradata, avctx->extradata_size*8);
        ret = ff_mpeg4_decode_picture_header(s, gb);
    }

    init_get_bits(gb, buf, 8 * buf_size);
    ret = ff_mpeg4_decode_picture_header(s, gb);
    if (s->width) {
        avcodec_set_dimensions(avctx, s->width, s->height);
    }
    s1->pict_type= s->pict_type;
    pc->first_picture = 0;
    return ret;
}

static int mpeg4video_parse_init(AVCodecParserContext *s)
{
    ParseContext1 *pc = s->priv_data;

    pc->enc = av_mallocz(sizeof(MpegEncContext));
    if (!pc->enc)
        return -1;
    pc->first_picture = 1;
    return 0;
}

static int mpeg4video_parse(AVCodecParserContext *s,
                           AVCodecContext *avctx,
                           uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    int next;

    if(s->flags & PARSER_FLAG_COMPLETE_FRAMES){
        next= buf_size;
    }else{
        next= ff_mpeg4_find_frame_end(pc, buf, buf_size);

        if (ff_combine_frame(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }
    av_mpeg4_decode_header(s, avctx, buf, buf_size);

    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}


AVCodecParser mpeg4video_parser = {
    { CODEC_ID_MPEG4 },
    sizeof(ParseContext1),
    mpeg4video_parse_init,
    mpeg4video_parse,
    ff_parse1_close,
    ff_mpeg4video_split,
};
