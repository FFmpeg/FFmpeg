/*
 * DXVA2 WMV3/VC-1 HW acceleration.
 *
 * copyright (c) 2010 Laurent Aimar
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

#include "dxva2_internal.h"
#include "vc1.h"
#include "vc1data.h"

struct dxva2_picture_context {
    DXVA_PictureParameters pp;
    DXVA_SliceInfo         si;

    const uint8_t          *bitstream;
    unsigned               bitstream_size;
};

static void fill_picture_parameters(AVCodecContext *avctx,
                                    struct dxva_context *ctx, const VC1Context *v,
                                    DXVA_PictureParameters *pp)
{
    const MpegEncContext *s = &v->s;
    const Picture *current_picture = s->current_picture_ptr;

    memset(pp, 0, sizeof(*pp));
    pp->wDecodedPictureIndex    =
    pp->wDeblockedPictureIndex  = ff_dxva2_get_surface_index(ctx, current_picture);
    if (s->pict_type != AV_PICTURE_TYPE_I && !v->bi_type)
        pp->wForwardRefPictureIndex = ff_dxva2_get_surface_index(ctx, &s->last_picture);
    else
        pp->wForwardRefPictureIndex = 0xffff;
    if (s->pict_type == AV_PICTURE_TYPE_B && !v->bi_type)
        pp->wBackwardRefPictureIndex = ff_dxva2_get_surface_index(ctx, &s->next_picture);
    else
        pp->wBackwardRefPictureIndex = 0xffff;
    if (v->profile == PROFILE_ADVANCED) {
        /* It is the cropped width/height -1 of the frame */
        pp->wPicWidthInMBminus1 = avctx->width  - 1;
        pp->wPicHeightInMBminus1= avctx->height - 1;
    } else {
        /* It is the coded width/height in macroblock -1 of the frame */
        pp->wPicWidthInMBminus1 = s->mb_width  - 1;
        pp->wPicHeightInMBminus1= s->mb_height - 1;
    }
    pp->bMacroblockWidthMinus1  = 15;
    pp->bMacroblockHeightMinus1 = 15;
    pp->bBlockWidthMinus1       = 7;
    pp->bBlockHeightMinus1      = 7;
    pp->bBPPminus1              = 7;
    if (s->picture_structure & PICT_TOP_FIELD)
        pp->bPicStructure      |= 0x01;
    if (s->picture_structure & PICT_BOTTOM_FIELD)
        pp->bPicStructure      |= 0x02;
    pp->bSecondField            = v->interlace && v->fcm != ILACE_FIELD && !s->first_field;
    pp->bPicIntra               = s->pict_type == AV_PICTURE_TYPE_I || v->bi_type;
    pp->bPicBackwardPrediction  = s->pict_type == AV_PICTURE_TYPE_B && !v->bi_type;
    pp->bBidirectionalAveragingMode = (1                                           << 7) |
                                      ((ctx->cfg->ConfigIntraResidUnsigned != 0)   << 6) |
                                      ((ctx->cfg->ConfigResidDiffAccelerator != 0) << 5) |
                                      ((v->lumscale != 32 || v->lumshift != 0)     << 4) |
                                      ((v->profile == PROFILE_ADVANCED)            << 3);
    pp->bMVprecisionAndChromaRelation = ((v->mv_mode == MV_PMODE_1MV_HPEL_BILIN) << 3) |
                                        (1                                       << 2) |
                                        (0                                       << 1) |
                                        (!s->quarter_sample                          );
    pp->bChromaFormat           = v->chromaformat;
    ctx->report_id++;
    if (ctx->report_id >= (1 << 16))
        ctx->report_id = 1;
    pp->bPicScanFixed           = ctx->report_id >> 8;
    pp->bPicScanMethod          = ctx->report_id & 0xff;
    pp->bPicReadbackRequests    = 0;
    pp->bRcontrol               = v->rnd;
    pp->bPicSpatialResid8       = (v->panscanflag  << 7) |
                                  (v->refdist_flag << 6) |
                                  (s->loop_filter  << 5) |
                                  (v->fastuvmc     << 4) |
                                  (v->extended_mv  << 3) |
                                  (v->dquant       << 1) |
                                  (v->vstransform      );
    pp->bPicOverflowBlocks      = (v->quantizer_mode << 6) |
                                  (v->multires       << 5) |
                                  (s->resync_marker  << 4) |
                                  (v->rangered       << 3) |
                                  (s->max_b_frames       );
    pp->bPicExtrapolation       = (!v->interlace || v->fcm == PROGRESSIVE) ? 1 : 2;
    pp->bPicDeblocked           = ((!pp->bPicBackwardPrediction && v->overlap)        << 6) |
                                  ((v->profile != PROFILE_ADVANCED && v->rangeredfrm) << 5) |
                                  (s->loop_filter                                     << 1);
    pp->bPicDeblockConfined     = (v->postprocflag             << 7) |
                                  (v->broadcast                << 6) |
                                  (v->interlace                << 5) |
                                  (v->tfcntrflag               << 4) |
                                  (v->finterpflag              << 3) |
                                  ((s->pict_type != AV_PICTURE_TYPE_B) << 2) |
                                  (v->psf                      << 1) |
                                  (v->extended_dmv                 );
    if (s->pict_type != AV_PICTURE_TYPE_I)
        pp->bPic4MVallowed      = v->mv_mode == MV_PMODE_MIXED_MV ||
                                  (v->mv_mode == MV_PMODE_INTENSITY_COMP &&
                                   v->mv_mode2 == MV_PMODE_MIXED_MV);
    if (v->profile == PROFILE_ADVANCED)
        pp->bPicOBMC            = (v->range_mapy_flag  << 7) |
                                  (v->range_mapy       << 4) |
                                  (v->range_mapuv_flag << 3) |
                                  (v->range_mapuv          );
    pp->bPicBinPB               = 0;
    pp->bMV_RPS                 = 0;
    pp->bReservedBits           = 0;
    if (s->picture_structure == PICT_FRAME) {
        pp->wBitstreamFcodes        = v->lumscale;
        pp->wBitstreamPCEelements   = v->lumshift;
    } else {
        /* Syntax: (top_field_param << 8) | bottom_field_param */
        pp->wBitstreamFcodes        = (v->lumscale << 8) | v->lumscale;
        pp->wBitstreamPCEelements   = (v->lumshift << 8) | v->lumshift;
    }
    pp->bBitstreamConcealmentNeed   = 0;
    pp->bBitstreamConcealmentMethod = 0;
}

static void fill_slice(AVCodecContext *avctx, DXVA_SliceInfo *slice,
                       unsigned position, unsigned size)
{
    const VC1Context *v = avctx->priv_data;
    const MpegEncContext *s = &v->s;

    memset(slice, 0, sizeof(*slice));
    slice->wHorizontalPosition = 0;
    slice->wVerticalPosition   = s->mb_y;
    slice->dwSliceBitsInBuffer = 8 * size;
    slice->dwSliceDataLocation = position;
    slice->bStartCodeBitOffset = 0;
    slice->bReservedBits       = 0;
    slice->wMBbitOffset        = get_bits_count(&s->gb);
    slice->wNumberMBsInSlice   = s->mb_width * s->mb_height; /* XXX We assume 1 slice */
    slice->wQuantizerScaleCode = v->pq;
    slice->wBadSliceChopping   = 0;
}

static int commit_bitstream_and_slice_buffer(AVCodecContext *avctx,
                                             DXVA2_DecodeBufferDesc *bs,
                                             DXVA2_DecodeBufferDesc *sc)
{
    const VC1Context *v = avctx->priv_data;
    struct dxva_context *ctx = avctx->hwaccel_context;
    const MpegEncContext *s = &v->s;
    struct dxva2_picture_context *ctx_pic = s->current_picture_ptr->f.hwaccel_picture_private;

    DXVA_SliceInfo *slice = &ctx_pic->si;

    static const uint8_t start_code[] = { 0, 0, 1, 0x0d };
    const unsigned start_code_size = avctx->codec_id == CODEC_ID_VC1 ? sizeof(start_code) : 0;
    const unsigned slice_size = slice->dwSliceBitsInBuffer / 8;
    const unsigned padding = 128 - ((start_code_size + slice_size) & 127);
    const unsigned data_size = start_code_size + slice_size + padding;

    uint8_t  *dxva_data;
    unsigned dxva_size;
    int result;

    if (FAILED(IDirectXVideoDecoder_GetBuffer(ctx->decoder,
                                              DXVA2_BitStreamDateBufferType,
                                              &dxva_data, &dxva_size)))
        return -1;

    result = data_size <= dxva_size ? 0 : -1;
    if (!result) {
        if (start_code_size > 0)
            memcpy(dxva_data, start_code, start_code_size);
        memcpy(dxva_data + start_code_size,
               ctx_pic->bitstream + slice->dwSliceDataLocation, slice_size);
        if (padding > 0)
            memset(dxva_data + start_code_size + slice_size, 0, padding);
        slice->dwSliceBitsInBuffer = 8 * data_size;
    }
    if (FAILED(IDirectXVideoDecoder_ReleaseBuffer(ctx->decoder,
                                                  DXVA2_BitStreamDateBufferType)))
        return -1;
    if (result)
        return result;

    memset(bs, 0, sizeof(*bs));
    bs->CompressedBufferType = DXVA2_BitStreamDateBufferType;
    bs->DataSize             = data_size;
    bs->NumMBsInBuffer       = s->mb_width * s->mb_height;
    assert((bs->DataSize & 127) == 0);

    return ff_dxva2_commit_buffer(avctx, ctx, sc,
                                  DXVA2_SliceControlBufferType,
                                  slice, sizeof(*slice), bs->NumMBsInBuffer);
}

static int start_frame(AVCodecContext *avctx,
                       av_unused const uint8_t *buffer,
                       av_unused uint32_t size)
{
    const VC1Context *v = avctx->priv_data;
    struct dxva_context *ctx = avctx->hwaccel_context;
    struct dxva2_picture_context *ctx_pic = v->s.current_picture_ptr->f.hwaccel_picture_private;

    if (!ctx->decoder || !ctx->cfg || ctx->surface_count <= 0)
        return -1;
    assert(ctx_pic);

    fill_picture_parameters(avctx, ctx, v, &ctx_pic->pp);

    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream      = NULL;
    return 0;
}

static int decode_slice(AVCodecContext *avctx,
                        const uint8_t *buffer, uint32_t size)
{
    const VC1Context *v = avctx->priv_data;
    const Picture *current_picture = v->s.current_picture_ptr;
    struct dxva2_picture_context *ctx_pic = current_picture->f.hwaccel_picture_private;

    if (ctx_pic->bitstream_size > 0)
        return -1;

    if (avctx->codec_id == CODEC_ID_VC1 &&
        size >= 4 && IS_MARKER(AV_RB32(buffer))) {
        buffer += 4;
        size   -= 4;
    }

    ctx_pic->bitstream_size = size;
    ctx_pic->bitstream      = buffer;

    fill_slice(avctx, &ctx_pic->si, 0, size);
    return 0;
}

static int end_frame(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    struct dxva2_picture_context *ctx_pic = v->s.current_picture_ptr->f.hwaccel_picture_private;

    if (ctx_pic->bitstream_size <= 0)
        return -1;

    return ff_dxva2_common_end_frame(avctx, &v->s,
                                     &ctx_pic->pp, sizeof(ctx_pic->pp),
                                     NULL, 0,
                                     commit_bitstream_and_slice_buffer);
}

#if CONFIG_WMV3_DXVA2_HWACCEL
AVHWAccel ff_wmv3_dxva2_hwaccel = {
    .name           = "wmv3_dxva2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_WMV3,
    .pix_fmt        = PIX_FMT_DXVA2_VLD,
    .start_frame    = start_frame,
    .decode_slice   = decode_slice,
    .end_frame      = end_frame,
    .priv_data_size = sizeof(struct dxva2_picture_context),
};
#endif

AVHWAccel ff_vc1_dxva2_hwaccel = {
    .name           = "vc1_dxva2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_VC1,
    .pix_fmt        = PIX_FMT_DXVA2_VLD,
    .start_frame    = start_frame,
    .decode_slice   = decode_slice,
    .end_frame      = end_frame,
    .priv_data_size = sizeof(struct dxva2_picture_context),
};
