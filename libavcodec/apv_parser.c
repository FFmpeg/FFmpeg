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

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"

#include "avcodec.h"
#include "apv.h"
#include "cbs.h"
#include "cbs_apv.h"

typedef struct APVParseContext {
    CodedBitstreamContext *cbc;
    CodedBitstreamFragment au;
} APVParseContext;

static const enum AVPixelFormat apv_format_table[5][5] = {
    { AV_PIX_FMT_GRAY8,    AV_PIX_FMT_GRAY10,     AV_PIX_FMT_GRAY12,     AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16 },
    { 0 }, // 4:2:0 is not valid.
    { AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV422P10,  AV_PIX_FMT_YUV422P12,  AV_PIX_FMT_GRAY14, AV_PIX_FMT_YUV422P16 },
    { AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV444P10,  AV_PIX_FMT_YUV444P12,  AV_PIX_FMT_GRAY14, AV_PIX_FMT_YUV444P16 },
    { AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_YUVA444P16 },
};

static void dummy_free(void *opaque, uint8_t *data)
{
    av_assert0(opaque == data);
}

static int parse(AVCodecParserContext *s,
                 AVCodecContext *avctx,
                 const uint8_t **poutbuf, int *poutbuf_size,
                 const uint8_t *buf, int buf_size)
{
    APVParseContext *p = s->priv_data;
    CodedBitstreamFragment *au = &p->au;
    AVBufferRef *ref = NULL;
    int ret;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    ref = av_buffer_create((uint8_t *)buf, buf_size, dummy_free,
                           (void *)buf, AV_BUFFER_FLAG_READONLY);
    if (!ref)
        return buf_size;

    p->cbc->log_ctx = avctx;

    ret = ff_cbs_read(p->cbc, au, ref, buf, buf_size);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to parse access unit.\n");
        goto end;
    }

    s->key_frame         = 1;
    s->pict_type         = AV_PICTURE_TYPE_I;
    s->field_order       = AV_FIELD_UNKNOWN;
    s->picture_structure = AV_PICTURE_STRUCTURE_FRAME;

    for (int i = 0; i < au->nb_units; i++) {
        const CodedBitstreamUnit *pbu = &au->units[i];

        switch (pbu->type) {
        case APV_PBU_PRIMARY_FRAME: {
            const APVRawFrame *frame        = pbu->content;
            const APVRawFrameHeader *header = &frame->frame_header;
            const APVRawFrameInfo *info     = &header->frame_info;
            int bit_depth = info->bit_depth_minus8 + 8;

            if (bit_depth < 8 || bit_depth > 16 || bit_depth % 2)
                break;

            s->width                        = info->frame_width;
            s->height                       = info->frame_height;
            s->format                       = apv_format_table[info->chroma_format_idc][bit_depth - 4 >> 2];
            avctx->profile                  = info->profile_idc;
            avctx->level                    = info->level_idc;
            avctx->chroma_sample_location   = AVCHROMA_LOC_TOPLEFT;
            avctx->color_primaries          = header->color_primaries;
            avctx->color_trc                = header->transfer_characteristics;
            avctx->colorspace               = header->matrix_coefficients;
            avctx->color_range              = header->full_range_flag ? AVCOL_RANGE_JPEG
                                                                      : AVCOL_RANGE_MPEG;
            goto end;
        }
        default:
            break;
        }
    }

end:
    ff_cbs_fragment_reset(au);
    av_assert1(av_buffer_get_ref_count(ref) == 1);
    av_buffer_unref(&ref);
    p->cbc->log_ctx = NULL;

    return buf_size;
}

static const CodedBitstreamUnitType decompose_unit_types[] = {
    APV_PBU_PRIMARY_FRAME,
};

static av_cold int init(AVCodecParserContext *s)
{
    APVParseContext *p = s->priv_data;
    int ret;

    ret = ff_cbs_init(&p->cbc, AV_CODEC_ID_APV, NULL);
    if (ret < 0)
        return ret;

    p->cbc->decompose_unit_types    = decompose_unit_types;
    p->cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(decompose_unit_types);

    return 0;
}

static av_cold void close(AVCodecParserContext *s)
{
    APVParseContext *p = s->priv_data;

    ff_cbs_fragment_free(&p->au);
    ff_cbs_close(&p->cbc);
}

const AVCodecParser ff_apv_parser = {
    .codec_ids    = { AV_CODEC_ID_APV },
    .priv_data_size = sizeof(APVParseContext),
    .parser_init  = init,
    .parser_parse = parse,
    .parser_close = close,
};
