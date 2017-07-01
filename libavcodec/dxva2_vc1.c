/*
 * DXVA2 WMV3/VC-1 HW acceleration.
 *
 * copyright (c) 2010 Laurent Aimar
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

#include "mpegutils.h"
#include "vc1.h"
#include "vc1data.h"

// The headers above may include w32threads.h, which uses the original
// _WIN32_WINNT define, while dxva2_internal.h redefines it to target a
// potentially newer version.
#include "dxva2_internal.h"

#define MAX_SLICES 1024
struct dxva2_picture_context {
    DXVA_PictureParameters pp;
    unsigned               slice_count;
    DXVA_SliceInfo         slice[MAX_SLICES];

    const uint8_t          *bitstream;
    unsigned               bitstream_size;
};

static void fill_picture_parameters(AVCodecContext *avctx,
                                    AVDXVAContext *ctx, const VC1Context *v,
                                    DXVA_PictureParameters *pp)
{
    const MpegEncContext *s = &v->s;
    const Picture *current_picture = s->current_picture_ptr;
    int intcomp = 0;

    // determine if intensity compensation is needed
    if (s->pict_type == AV_PICTURE_TYPE_P) {
      if ((v->fcm == ILACE_FRAME && v->intcomp) || (v->fcm != ILACE_FRAME && v->mv_mode == MV_PMODE_INTENSITY_COMP)) {
        if (v->lumscale != 32 || v->lumshift != 0 || (s->picture_structure != PICT_FRAME && (v->lumscale2 != 32 || v->lumshift2 != 0)))
          intcomp = 1;
      }
    }

    memset(pp, 0, sizeof(*pp));
    pp->wDecodedPictureIndex    =
    pp->wDeblockedPictureIndex  = ff_dxva2_get_surface_index(avctx, ctx, current_picture->f);
    if (s->pict_type != AV_PICTURE_TYPE_I && !v->bi_type)
        pp->wForwardRefPictureIndex = ff_dxva2_get_surface_index(avctx, ctx, s->last_picture.f);
    else
        pp->wForwardRefPictureIndex = 0xffff;
    if (s->pict_type == AV_PICTURE_TYPE_B && !v->bi_type)
        pp->wBackwardRefPictureIndex = ff_dxva2_get_surface_index(avctx, ctx, s->next_picture.f);
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
    pp->bSecondField            = v->interlace && v->fcm == ILACE_FIELD && v->second_field;
    pp->bPicIntra               = s->pict_type == AV_PICTURE_TYPE_I || v->bi_type;
    pp->bPicBackwardPrediction  = s->pict_type == AV_PICTURE_TYPE_B && !v->bi_type;
    pp->bBidirectionalAveragingMode = (1                                           << 7) |
                                      ((DXVA_CONTEXT_CFG_INTRARESID(avctx, ctx) != 0) << 6) |
                                      ((DXVA_CONTEXT_CFG_RESIDACCEL(avctx, ctx) != 0) << 5) |
                                      (intcomp                                     << 4) |
                                      ((v->profile == PROFILE_ADVANCED)            << 3);
    pp->bMVprecisionAndChromaRelation = ((v->mv_mode == MV_PMODE_1MV_HPEL_BILIN) << 3) |
                                        (1                                       << 2) |
                                        (0                                       << 1) |
                                        (!s->quarter_sample                          );
    pp->bChromaFormat           = v->chromaformat;
    DXVA_CONTEXT_REPORT_ID(avctx, ctx)++;
    if (DXVA_CONTEXT_REPORT_ID(avctx, ctx) >= (1 << 16))
        DXVA_CONTEXT_REPORT_ID(avctx, ctx) = 1;
    pp->bPicScanFixed           = DXVA_CONTEXT_REPORT_ID(avctx, ctx) >> 8;
    pp->bPicScanMethod          = DXVA_CONTEXT_REPORT_ID(avctx, ctx) & 0xff;
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
                                  (v->resync_marker  << 4) |
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
    pp->bMV_RPS                 = (v->fcm == ILACE_FIELD && pp->bPicBackwardPrediction) ? v->refdist + 9 : 0;
    pp->bReservedBits           = v->pq;
    if (s->picture_structure == PICT_FRAME) {
        if (intcomp) {
            pp->wBitstreamFcodes      = v->lumscale;
            pp->wBitstreamPCEelements = v->lumshift;
        } else {
            pp->wBitstreamFcodes      = 32;
            pp->wBitstreamPCEelements = 0;
        }
    } else {
        /* Syntax: (top_field_param << 8) | bottom_field_param */
        if (intcomp) {
            pp->wBitstreamFcodes      = (v->lumscale << 8) | v->lumscale2;
            pp->wBitstreamPCEelements = (v->lumshift << 8) | v->lumshift2;
        } else {
            pp->wBitstreamFcodes      = (32 << 8) | 32;
            pp->wBitstreamPCEelements = 0;
        }
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
    slice->bReservedBits       = (s->pict_type == AV_PICTURE_TYPE_B && !v->bi_type) ? v->bfraction_lut_index + 9 : 0;
    slice->wMBbitOffset        = v->p_frame_skipped ? 0xffff : get_bits_count(&s->gb) + (avctx->codec_id == AV_CODEC_ID_VC1 ? 32 : 0);
    /* XXX We store the index of the first MB and it will be fixed later */
    slice->wNumberMBsInSlice   = (s->mb_y >> v->field_mode) * s->mb_width + s->mb_x;
    slice->wQuantizerScaleCode = v->pq;
    slice->wBadSliceChopping   = 0;
}

static int commit_bitstream_and_slice_buffer(AVCodecContext *avctx,
                                             DECODER_BUFFER_DESC *bs,
                                             DECODER_BUFFER_DESC *sc)
{
    const VC1Context *v = avctx->priv_data;
    AVDXVAContext *ctx = DXVA_CONTEXT(avctx);
    const MpegEncContext *s = &v->s;
    struct dxva2_picture_context *ctx_pic = s->current_picture_ptr->hwaccel_picture_private;

    static const uint8_t start_code[] = { 0, 0, 1, 0x0d };
    const unsigned start_code_size = avctx->codec_id == AV_CODEC_ID_VC1 ? sizeof(start_code) : 0;
    const unsigned mb_count = s->mb_width * (s->mb_height >> v->field_mode);
    DXVA_SliceInfo *slice = NULL;
    void     *dxva_data_ptr;
    uint8_t  *dxva_data, *current, *end;
    unsigned dxva_size;
    unsigned padding;
    unsigned i;
    unsigned type;

#if CONFIG_D3D11VA
    if (ff_dxva2_is_d3d11(avctx)) {
        type = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
        if (FAILED(ID3D11VideoContext_GetDecoderBuffer(D3D11VA_CONTEXT(ctx)->video_context,
                                                       D3D11VA_CONTEXT(ctx)->decoder,
                                                       type,
                                                       &dxva_size, &dxva_data_ptr)))
            return -1;
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        type = DXVA2_BitStreamDateBufferType;
        if (FAILED(IDirectXVideoDecoder_GetBuffer(DXVA2_CONTEXT(ctx)->decoder,
                                                  type,
                                                  &dxva_data_ptr, &dxva_size)))
            return -1;
    }
#endif

    dxva_data = dxva_data_ptr;
    current = dxva_data;
    end = dxva_data + dxva_size;

    for (i = 0; i < ctx_pic->slice_count; i++) {
        unsigned position, size;
        slice    = &ctx_pic->slice[i];
        position = slice->dwSliceDataLocation;
        size     = slice->dwSliceBitsInBuffer / 8;
        if (start_code_size + size > end - current) {
            av_log(avctx, AV_LOG_ERROR, "Failed to build bitstream");
            break;
        }
        slice->dwSliceDataLocation = current - dxva_data;

        if (i < ctx_pic->slice_count - 1)
            slice->wNumberMBsInSlice =
                slice[1].wNumberMBsInSlice - slice[0].wNumberMBsInSlice;
        else
            slice->wNumberMBsInSlice =
                mb_count - slice[0].wNumberMBsInSlice;

        /* write the appropriate frame, field or slice start code */
        if (start_code_size) {
            memcpy(current, start_code, start_code_size);
            if (i == 0 && v->second_field)
                current[3] = 0x0c;
            else if (i > 0)
                current[3] = 0x0b;

            current += start_code_size;
            slice->dwSliceBitsInBuffer += start_code_size * 8;
        }

        memcpy(current, &ctx_pic->bitstream[position], size);
        current += size;
    }
    padding = FFMIN(128 - ((current - dxva_data) & 127), end - current);
    if (slice && padding > 0) {
        memset(current, 0, padding);
        current += padding;
        slice->dwSliceBitsInBuffer += padding * 8;
    }

#if CONFIG_D3D11VA
    if (ff_dxva2_is_d3d11(avctx))
        if (FAILED(ID3D11VideoContext_ReleaseDecoderBuffer(D3D11VA_CONTEXT(ctx)->video_context, D3D11VA_CONTEXT(ctx)->decoder, type)))
            return -1;
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD)
        if (FAILED(IDirectXVideoDecoder_ReleaseBuffer(DXVA2_CONTEXT(ctx)->decoder, type)))
            return -1;
#endif
    if (i < ctx_pic->slice_count)
        return -1;

#if CONFIG_D3D11VA
    if (ff_dxva2_is_d3d11(avctx)) {
        D3D11_VIDEO_DECODER_BUFFER_DESC *dsc11 = bs;
        memset(dsc11, 0, sizeof(*dsc11));
        dsc11->BufferType           = type;
        dsc11->DataSize             = current - dxva_data;
        dsc11->NumMBsInBuffer       = mb_count;

        type = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        DXVA2_DecodeBufferDesc *dsc2 = bs;
        memset(dsc2, 0, sizeof(*dsc2));
        dsc2->CompressedBufferType = type;
        dsc2->DataSize             = current - dxva_data;
        dsc2->NumMBsInBuffer       = mb_count;

        type = DXVA2_SliceControlBufferType;
    }
#endif

    return ff_dxva2_commit_buffer(avctx, ctx, sc,
                                  type,
                                  ctx_pic->slice,
                                  ctx_pic->slice_count * sizeof(*ctx_pic->slice),
                                  mb_count);
}

static int dxva2_vc1_start_frame(AVCodecContext *avctx,
                                 av_unused const uint8_t *buffer,
                                 av_unused uint32_t size)
{
    const VC1Context *v = avctx->priv_data;
    AVDXVAContext *ctx = DXVA_CONTEXT(avctx);
    struct dxva2_picture_context *ctx_pic = v->s.current_picture_ptr->hwaccel_picture_private;

    if (!DXVA_CONTEXT_VALID(avctx, ctx))
        return -1;
    assert(ctx_pic);

    fill_picture_parameters(avctx, ctx, v, &ctx_pic->pp);

    ctx_pic->slice_count    = 0;
    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream      = NULL;
    return 0;
}

static int dxva2_vc1_decode_slice(AVCodecContext *avctx,
                                  const uint8_t *buffer,
                                  uint32_t size)
{
    const VC1Context *v = avctx->priv_data;
    const Picture *current_picture = v->s.current_picture_ptr;
    struct dxva2_picture_context *ctx_pic = current_picture->hwaccel_picture_private;
    unsigned position;

    if (ctx_pic->slice_count >= MAX_SLICES) {
        avpriv_request_sample(avctx, "%d slices in dxva2",
                              ctx_pic->slice_count);
        return -1;
    }

    if (avctx->codec_id == AV_CODEC_ID_VC1 &&
        size >= 4 && IS_MARKER(AV_RB32(buffer))) {
        buffer += 4;
        size   -= 4;
    }

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    position = buffer - ctx_pic->bitstream;
    fill_slice(avctx, &ctx_pic->slice[ctx_pic->slice_count++], position, size);
    return 0;
}

static int dxva2_vc1_end_frame(AVCodecContext *avctx)
{
    VC1Context *v = avctx->priv_data;
    struct dxva2_picture_context *ctx_pic = v->s.current_picture_ptr->hwaccel_picture_private;
    int ret;

    if (ctx_pic->slice_count <= 0 || ctx_pic->bitstream_size <= 0)
        return -1;

    ret = ff_dxva2_common_end_frame(avctx, v->s.current_picture_ptr->f,
                                    &ctx_pic->pp, sizeof(ctx_pic->pp),
                                    NULL, 0,
                                    commit_bitstream_and_slice_buffer);
    if (!ret)
        ff_mpeg_draw_horiz_band(&v->s, 0, avctx->height);
    return ret;
}

#if CONFIG_WMV3_DXVA2_HWACCEL
AVHWAccel ff_wmv3_dxva2_hwaccel = {
    .name           = "wmv3_dxva2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WMV3,
    .pix_fmt        = AV_PIX_FMT_DXVA2_VLD,
    .init           = ff_dxva2_decode_init,
    .uninit         = ff_dxva2_decode_uninit,
    .start_frame    = dxva2_vc1_start_frame,
    .decode_slice   = dxva2_vc1_decode_slice,
    .end_frame      = dxva2_vc1_end_frame,
    .frame_priv_data_size = sizeof(struct dxva2_picture_context),
    .priv_data_size = sizeof(FFDXVASharedContext),
};
#endif

#if CONFIG_VC1_DXVA2_HWACCEL
AVHWAccel ff_vc1_dxva2_hwaccel = {
    .name           = "vc1_dxva2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VC1,
    .pix_fmt        = AV_PIX_FMT_DXVA2_VLD,
    .init           = ff_dxva2_decode_init,
    .uninit         = ff_dxva2_decode_uninit,
    .start_frame    = dxva2_vc1_start_frame,
    .decode_slice   = dxva2_vc1_decode_slice,
    .end_frame      = dxva2_vc1_end_frame,
    .frame_priv_data_size = sizeof(struct dxva2_picture_context),
    .priv_data_size = sizeof(FFDXVASharedContext),
};
#endif

#if CONFIG_WMV3_D3D11VA_HWACCEL
AVHWAccel ff_wmv3_d3d11va_hwaccel = {
    .name           = "wmv3_d3d11va",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WMV3,
    .pix_fmt        = AV_PIX_FMT_D3D11VA_VLD,
    .init           = ff_dxva2_decode_init,
    .uninit         = ff_dxva2_decode_uninit,
    .start_frame    = dxva2_vc1_start_frame,
    .decode_slice   = dxva2_vc1_decode_slice,
    .end_frame      = dxva2_vc1_end_frame,
    .frame_priv_data_size = sizeof(struct dxva2_picture_context),
    .priv_data_size = sizeof(FFDXVASharedContext),
};
#endif

#if CONFIG_WMV3_D3D11VA2_HWACCEL
AVHWAccel ff_wmv3_d3d11va2_hwaccel = {
    .name           = "wmv3_d3d11va2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WMV3,
    .pix_fmt        = AV_PIX_FMT_D3D11,
    .init           = ff_dxva2_decode_init,
    .uninit         = ff_dxva2_decode_uninit,
    .start_frame    = dxva2_vc1_start_frame,
    .decode_slice   = dxva2_vc1_decode_slice,
    .end_frame      = dxva2_vc1_end_frame,
    .frame_priv_data_size = sizeof(struct dxva2_picture_context),
    .priv_data_size = sizeof(FFDXVASharedContext),
};
#endif

#if CONFIG_VC1_D3D11VA_HWACCEL
AVHWAccel ff_vc1_d3d11va_hwaccel = {
    .name           = "vc1_d3d11va",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VC1,
    .pix_fmt        = AV_PIX_FMT_D3D11VA_VLD,
    .init           = ff_dxva2_decode_init,
    .uninit         = ff_dxva2_decode_uninit,
    .start_frame    = dxva2_vc1_start_frame,
    .decode_slice   = dxva2_vc1_decode_slice,
    .end_frame      = dxva2_vc1_end_frame,
    .frame_priv_data_size = sizeof(struct dxva2_picture_context),
    .priv_data_size = sizeof(FFDXVASharedContext),
};
#endif

#if CONFIG_VC1_D3D11VA2_HWACCEL
AVHWAccel ff_vc1_d3d11va2_hwaccel = {
    .name           = "vc1_d3d11va2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VC1,
    .pix_fmt        = AV_PIX_FMT_D3D11,
    .init           = ff_dxva2_decode_init,
    .uninit         = ff_dxva2_decode_uninit,
    .start_frame    = dxva2_vc1_start_frame,
    .decode_slice   = dxva2_vc1_decode_slice,
    .end_frame      = dxva2_vc1_end_frame,
    .frame_priv_data_size = sizeof(struct dxva2_picture_context),
    .priv_data_size = sizeof(FFDXVASharedContext),
};
#endif
