/*
 * AV1 parser
 *
 * Copyright (C) 2018 James Almer <jamrial@gmail.com>
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

#include "libavutil/avassert.h"
#include "cbs.h"
#include "cbs_av1.h"
#include "parser.h"

typedef struct AV1ParseContext {
    CodedBitstreamContext *cbc;
    CodedBitstreamFragment temporal_unit;
    int parsed_extradata;
} AV1ParseContext;

static const enum AVPixelFormat pix_fmts_8bit[2][2] = {
    { AV_PIX_FMT_YUV444P, AV_PIX_FMT_NONE },
    { AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P },
};
static const enum AVPixelFormat pix_fmts_10bit[2][2] = {
    { AV_PIX_FMT_YUV444P10, AV_PIX_FMT_NONE },
    { AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV420P10 },
};
static const enum AVPixelFormat pix_fmts_12bit[2][2] = {
    { AV_PIX_FMT_YUV444P12, AV_PIX_FMT_NONE },
    { AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12 },
};

static const enum AVPixelFormat pix_fmts_rgb[3] = {
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12,
};

static int av1_parser_parse(AVCodecParserContext *ctx,
                            AVCodecContext *avctx,
                            const uint8_t **out_data, int *out_size,
                            const uint8_t *data, int size)
{
    AV1ParseContext *s = ctx->priv_data;
    CodedBitstreamFragment *td = &s->temporal_unit;
    const CodedBitstreamAV1Context *av1 = s->cbc->priv_data;
    const AV1RawSequenceHeader *seq;
    const AV1RawColorConfig *color;
    int ret;

    *out_data = data;
    *out_size = size;

    ctx->key_frame         = -1;
    ctx->pict_type         = AV_PICTURE_TYPE_NONE;
    ctx->picture_structure = AV_PICTURE_STRUCTURE_UNKNOWN;

    s->cbc->log_ctx = avctx;

    if (avctx->extradata_size && !s->parsed_extradata) {
        s->parsed_extradata = 1;

        ret = ff_cbs_read_extradata_from_codec(s->cbc, td, avctx);
        if (ret < 0) {
            av_log(avctx, AV_LOG_WARNING, "Failed to parse extradata.\n");
        }

        ff_cbs_fragment_reset(td);
    }

    ret = ff_cbs_read(s->cbc, td, data, size);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to parse temporal unit.\n");
        goto end;
    }

    if (!av1->sequence_header) {
        av_log(avctx, AV_LOG_ERROR, "No sequence header available\n");
        goto end;
    }

    seq = av1->sequence_header;
    color = &seq->color_config;

    for (int i = 0; i < td->nb_units; i++) {
        const CodedBitstreamUnit *unit = &td->units[i];
        const AV1RawOBU *obu = unit->content;
        const AV1RawFrameHeader *frame;

        if (unit->type == AV1_OBU_FRAME)
            frame = &obu->obu.frame.header;
        else if (unit->type == AV1_OBU_FRAME_HEADER)
            frame = &obu->obu.frame_header;
        else
            continue;

        if (obu->header.spatial_id > 0)
            continue;

        if (!frame->show_frame && !frame->show_existing_frame)
            continue;

        ctx->width  = frame->frame_width_minus_1 + 1;
        ctx->height = frame->frame_height_minus_1 + 1;

        ctx->key_frame = frame->frame_type == AV1_FRAME_KEY && !frame->show_existing_frame;

        switch (frame->frame_type) {
        case AV1_FRAME_KEY:
        case AV1_FRAME_INTRA_ONLY:
            ctx->pict_type = AV_PICTURE_TYPE_I;
            break;
        case AV1_FRAME_INTER:
            ctx->pict_type = AV_PICTURE_TYPE_P;
            break;
        case AV1_FRAME_SWITCH:
            ctx->pict_type = AV_PICTURE_TYPE_SP;
            break;
        }
        ctx->picture_structure = AV_PICTURE_STRUCTURE_FRAME;
    }

    switch (av1->bit_depth) {
    case 8:
        ctx->format = color->mono_chrome ? AV_PIX_FMT_GRAY8
                                         : pix_fmts_8bit [color->subsampling_x][color->subsampling_y];
        break;
    case 10:
        ctx->format = color->mono_chrome ? AV_PIX_FMT_GRAY10
                                         : pix_fmts_10bit[color->subsampling_x][color->subsampling_y];
        break;
    case 12:
        ctx->format = color->mono_chrome ? AV_PIX_FMT_GRAY12
                                         : pix_fmts_12bit[color->subsampling_x][color->subsampling_y];
        break;
    }
    av_assert2(ctx->format != AV_PIX_FMT_NONE);

    if (!color->subsampling_x && !color->subsampling_y &&
        color->matrix_coefficients       == AVCOL_SPC_RGB &&
        color->color_primaries           == AVCOL_PRI_BT709 &&
        color->transfer_characteristics  == AVCOL_TRC_IEC61966_2_1)
        ctx->format = pix_fmts_rgb[color->high_bitdepth + color->twelve_bit];

    avctx->profile = seq->seq_profile;
    avctx->level   = seq->seq_level_idx[0];

    avctx->colorspace = (enum AVColorSpace) color->matrix_coefficients;
    avctx->color_primaries = (enum AVColorPrimaries) color->color_primaries;
    avctx->color_trc = (enum AVColorTransferCharacteristic) color->transfer_characteristics;
    avctx->color_range = color->color_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    if (avctx->framerate.num)
        avctx->time_base = av_inv_q(av_mul_q(avctx->framerate, (AVRational){avctx->ticks_per_frame, 1}));

end:
    ff_cbs_fragment_reset(td);

    s->cbc->log_ctx = NULL;

    return size;
}

static const CodedBitstreamUnitType decompose_unit_types[] = {
    AV1_OBU_TEMPORAL_DELIMITER,
    AV1_OBU_SEQUENCE_HEADER,
    AV1_OBU_FRAME_HEADER,
    AV1_OBU_TILE_GROUP,
    AV1_OBU_FRAME,
};

static av_cold int av1_parser_init(AVCodecParserContext *ctx)
{
    AV1ParseContext *s = ctx->priv_data;
    int ret;

    ret = ff_cbs_init(&s->cbc, AV_CODEC_ID_AV1, NULL);
    if (ret < 0)
        return ret;

    s->cbc->decompose_unit_types    = decompose_unit_types;
    s->cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(decompose_unit_types);

    return 0;
}

static void av1_parser_close(AVCodecParserContext *ctx)
{
    AV1ParseContext *s = ctx->priv_data;

    ff_cbs_fragment_free(&s->temporal_unit);
    ff_cbs_close(&s->cbc);
}

const AVCodecParser ff_av1_parser = {
    .codec_ids      = { AV_CODEC_ID_AV1 },
    .priv_data_size = sizeof(AV1ParseContext),
    .parser_init    = av1_parser_init,
    .parser_close   = av1_parser_close,
    .parser_parse   = av1_parser_parse,
};
