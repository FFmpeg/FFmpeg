/*
 * MPEG-2 HW decode acceleration through VA API
 *
 * Copyright (C) 2008-2009 Splitted-Desktop Systems
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

#include "vaapi_internal.h"
#include "dsputil.h"

/** Reconstruct bitstream f_code */
static inline int mpeg2_get_f_code(MpegEncContext *s)
{
    return (s->mpeg_f_code[0][0] << 12) | (s->mpeg_f_code[0][1] << 8) |
           (s->mpeg_f_code[1][0] <<  4) |  s->mpeg_f_code[1][1];
}

/** Determine frame start: first field for field picture or frame picture */
static inline int mpeg2_get_is_frame_start(MpegEncContext *s)
{
    return s->first_field || s->picture_structure == PICT_FRAME;
}

static int vaapi_mpeg2_start_frame(AVCodecContext *avctx, av_unused const uint8_t *buffer, av_unused uint32_t size)
{
    struct MpegEncContext * const s = avctx->priv_data;
    struct vaapi_context * const vactx = avctx->hwaccel_context;
    VAPictureParameterBufferMPEG2 *pic_param;
    VAIQMatrixBufferMPEG2 *iq_matrix;
    int i;

    av_dlog(avctx, "vaapi_mpeg2_start_frame()\n");

    vactx->slice_param_size = sizeof(VASliceParameterBufferMPEG2);

    /* Fill in VAPictureParameterBufferMPEG2 */
    pic_param = ff_vaapi_alloc_pic_param(vactx, sizeof(VAPictureParameterBufferMPEG2));
    if (!pic_param)
        return -1;
    pic_param->horizontal_size                                  = s->width;
    pic_param->vertical_size                                    = s->height;
    pic_param->forward_reference_picture                        = VA_INVALID_ID;
    pic_param->backward_reference_picture                       = VA_INVALID_ID;
    pic_param->picture_coding_type                              = s->pict_type;
    pic_param->f_code                                           = mpeg2_get_f_code(s);
    pic_param->picture_coding_extension.value                   = 0; /* reset all bits */
    pic_param->picture_coding_extension.bits.intra_dc_precision = s->intra_dc_precision;
    pic_param->picture_coding_extension.bits.picture_structure  = s->picture_structure;
    pic_param->picture_coding_extension.bits.top_field_first    = s->top_field_first;
    pic_param->picture_coding_extension.bits.frame_pred_frame_dct = s->frame_pred_frame_dct;
    pic_param->picture_coding_extension.bits.concealment_motion_vectors = s->concealment_motion_vectors;
    pic_param->picture_coding_extension.bits.q_scale_type       = s->q_scale_type;
    pic_param->picture_coding_extension.bits.intra_vlc_format   = s->intra_vlc_format;
    pic_param->picture_coding_extension.bits.alternate_scan     = s->alternate_scan;
    pic_param->picture_coding_extension.bits.repeat_first_field = s->repeat_first_field;
    pic_param->picture_coding_extension.bits.progressive_frame  = s->progressive_frame;
    pic_param->picture_coding_extension.bits.is_first_field     = mpeg2_get_is_frame_start(s);

    switch (s->pict_type) {
    case AV_PICTURE_TYPE_B:
        pic_param->backward_reference_picture = ff_vaapi_get_surface_id(&s->next_picture);
        // fall-through
    case AV_PICTURE_TYPE_P:
        pic_param->forward_reference_picture = ff_vaapi_get_surface_id(&s->last_picture);
        break;
    }

    /* Fill in VAIQMatrixBufferMPEG2 */
    iq_matrix = ff_vaapi_alloc_iq_matrix(vactx, sizeof(VAIQMatrixBufferMPEG2));
    if (!iq_matrix)
        return -1;
    iq_matrix->load_intra_quantiser_matrix              = 1;
    iq_matrix->load_non_intra_quantiser_matrix          = 1;
    iq_matrix->load_chroma_intra_quantiser_matrix       = 1;
    iq_matrix->load_chroma_non_intra_quantiser_matrix   = 1;

    for (i = 0; i < 64; i++) {
        int n = s->dsp.idct_permutation[ff_zigzag_direct[i]];
        iq_matrix->intra_quantiser_matrix[i]            = s->intra_matrix[n];
        iq_matrix->non_intra_quantiser_matrix[i]        = s->inter_matrix[n];
        iq_matrix->chroma_intra_quantiser_matrix[i]     = s->chroma_intra_matrix[n];
        iq_matrix->chroma_non_intra_quantiser_matrix[i] = s->chroma_inter_matrix[n];
    }
    return 0;
}

static int vaapi_mpeg2_end_frame(AVCodecContext *avctx)
{
    return ff_vaapi_common_end_frame(avctx->priv_data);
}

static int vaapi_mpeg2_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    MpegEncContext * const s = avctx->priv_data;
    VASliceParameterBufferMPEG2 *slice_param;
    GetBitContext gb;
    uint32_t quantiser_scale_code, intra_slice_flag, macroblock_offset;

    av_dlog(avctx, "vaapi_mpeg2_decode_slice(): buffer %p, size %d\n", buffer, size);

    /* Determine macroblock_offset */
    init_get_bits(&gb, buffer, 8 * size);
    if (get_bits_long(&gb, 32) >> 8 != 1) /* start code */
        return AVERROR_INVALIDDATA;
    quantiser_scale_code = get_bits(&gb, 5);
    intra_slice_flag = get_bits1(&gb);
    if (intra_slice_flag) {
        skip_bits(&gb, 8);
        while (get_bits1(&gb) != 0)
            skip_bits(&gb, 8);
    }
    macroblock_offset = get_bits_count(&gb);

    /* Fill in VASliceParameterBufferMPEG2 */
    slice_param = (VASliceParameterBufferMPEG2 *)ff_vaapi_alloc_slice(avctx->hwaccel_context, buffer, size);
    if (!slice_param)
        return -1;
    slice_param->macroblock_offset              = macroblock_offset;
    slice_param->slice_horizontal_position      = s->mb_x;
    slice_param->slice_vertical_position        = s->mb_y >> (s->picture_structure != PICT_FRAME);
    slice_param->quantiser_scale_code           = quantiser_scale_code;
    slice_param->intra_slice_flag               = intra_slice_flag;
    return 0;
}

AVHWAccel ff_mpeg2_vaapi_hwaccel = {
    .name           = "mpeg2_vaapi",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_MPEG2VIDEO,
    .pix_fmt        = PIX_FMT_VAAPI_VLD,
    .start_frame    = vaapi_mpeg2_start_frame,
    .end_frame      = vaapi_mpeg2_end_frame,
    .decode_slice   = vaapi_mpeg2_decode_slice,
};
