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

#include "avcodec.h"
#include "ffv1.h"
#include "rangecoder.h"

typedef struct FFV1ParseContext {
    FFV1Context f;
    int got_first;
} FFV1ParseContext;

static int parse(AVCodecParserContext *s,
                 AVCodecContext *avctx,
                 const uint8_t **poutbuf, int *poutbuf_size,
                 const uint8_t *buf, int buf_size)
{
    FFV1ParseContext *p = s->priv_data;
    FFV1Context *f = &p->f;
    RangeCoder c;
    uint8_t keystate = 128;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    if (!p->got_first) {
        int ret = ff_ffv1_common_init(avctx, f);
        p->got_first = 1;
        if (ret < 0)
            return buf_size;

        if (avctx->extradata_size > 0 && (ret = ff_ffv1_read_extra_header(f)) < 0)
            return buf_size;
    }

    ff_init_range_decoder(&c, buf, buf_size);
    ff_build_rac_states(&c, 0.05 * (1LL << 32), 256 - 8);

    f->avctx = avctx;
    s->key_frame = get_rac(&c, &keystate);
    s->pict_type = AV_PICTURE_TYPE_I;
    s->field_order = AV_FIELD_UNKNOWN;
    s->picture_structure = AV_PICTURE_STRUCTURE_UNKNOWN;

    if (s->key_frame) {
        uint8_t state[CONTEXT_SIZE];
        memset(state, 128, sizeof(state));
        ff_ffv1_parse_header(f, &c, state);
    }

    s->width  = f->width;
    s->height = f->height;
    s->format = f->pix_fmt;

    return buf_size;
}

static void ffv1_close(AVCodecParserContext *s)
{
    FFV1ParseContext *p = s->priv_data;

    p->f.avctx = NULL;
    ff_ffv1_close(&p->f);
}

const AVCodecParser ff_ffv1_parser = {
    .codec_ids    = { AV_CODEC_ID_FFV1 },
    .priv_data_size = sizeof(FFV1ParseContext),
    .parser_parse = parse,
    .parser_close = ffv1_close,
};
