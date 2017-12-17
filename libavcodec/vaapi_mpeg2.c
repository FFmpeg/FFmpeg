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

#include "hwaccel.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "internal.h"
#include "vaapi_decode.h"

/** Reconstruct bitstream f_code */
static inline int mpeg2_get_f_code(const MpegEncContext *s)
{
    return (s->mpeg_f_code[0][0] << 12) | (s->mpeg_f_code[0][1] << 8) |
           (s->mpeg_f_code[1][0] <<  4) |  s->mpeg_f_code[1][1];
}

/** Determine frame start: first field for field picture or frame picture */
static inline int mpeg2_get_is_frame_start(const MpegEncContext *s)
{
    return s->first_field || s->picture_structure == PICT_FRAME;
}

static int vaapi_mpeg2_start_frame(AVCodecContext *avctx, av_unused const uint8_t *buffer, av_unused uint32_t size)
{
    const MpegEncContext *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->current_picture_ptr->hwaccel_picture_private;
    VAPictureParameterBufferMPEG2 pic_param;
    VAIQMatrixBufferMPEG2 iq_matrix;
    int i, err;

    pic->output_surface = ff_vaapi_get_surface_id(s->current_picture_ptr->f);

    pic_param = (VAPictureParameterBufferMPEG2) {
        .horizontal_size                 = s->width,
        .vertical_size                   = s->height,
        .forward_reference_picture       = VA_INVALID_ID,
        .backward_reference_picture      = VA_INVALID_ID,
        .picture_coding_type             = s->pict_type,
        .f_code                          = mpeg2_get_f_code(s),
        .picture_coding_extension.bits = {
            .intra_dc_precision          = s->intra_dc_precision,
            .picture_structure           = s->picture_structure,
            .top_field_first             = s->top_field_first,
            .frame_pred_frame_dct        = s->frame_pred_frame_dct,
            .concealment_motion_vectors  = s->concealment_motion_vectors,
            .q_scale_type                = s->q_scale_type,
            .intra_vlc_format            = s->intra_vlc_format,
            .alternate_scan              = s->alternate_scan,
            .repeat_first_field          = s->repeat_first_field,
            .progressive_frame           = s->progressive_frame,
            .is_first_field              = mpeg2_get_is_frame_start(s),
        },
    };

    switch (s->pict_type) {
    case AV_PICTURE_TYPE_B:
        pic_param.backward_reference_picture = ff_vaapi_get_surface_id(s->next_picture.f);
        // fall-through
    case AV_PICTURE_TYPE_P:
        pic_param.forward_reference_picture = ff_vaapi_get_surface_id(s->last_picture.f);
        break;
    }

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAPictureParameterBufferType,
                                            &pic_param, sizeof(pic_param));
    if (err < 0)
        goto fail;

    iq_matrix.load_intra_quantiser_matrix              = 1;
    iq_matrix.load_non_intra_quantiser_matrix          = 1;
    iq_matrix.load_chroma_intra_quantiser_matrix       = 1;
    iq_matrix.load_chroma_non_intra_quantiser_matrix   = 1;

    for (i = 0; i < 64; i++) {
        int n = s->idsp.idct_permutation[ff_zigzag_direct[i]];
        iq_matrix.intra_quantiser_matrix[i]            = s->intra_matrix[n];
        iq_matrix.non_intra_quantiser_matrix[i]        = s->inter_matrix[n];
        iq_matrix.chroma_intra_quantiser_matrix[i]     = s->chroma_intra_matrix[n];
        iq_matrix.chroma_non_intra_quantiser_matrix[i] = s->chroma_inter_matrix[n];
    }

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAIQMatrixBufferType,
                                            &iq_matrix, sizeof(iq_matrix));
    if (err < 0)
        goto fail;

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, pic);
    return err;
}

static int vaapi_mpeg2_end_frame(AVCodecContext *avctx)
{
    MpegEncContext     *s   = avctx->priv_data;
    VAAPIDecodePicture *pic = s->current_picture_ptr->hwaccel_picture_private;
    int ret;

    ret = ff_vaapi_decode_issue(avctx, pic);
    if (ret < 0)
        goto fail;

    ff_mpeg_draw_horiz_band(s, 0, s->avctx->height);

fail:
    return ret;
}

static int vaapi_mpeg2_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    const MpegEncContext *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->current_picture_ptr->hwaccel_picture_private;
    VASliceParameterBufferMPEG2 slice_param;
    GetBitContext gb;
    uint32_t quantiser_scale_code, intra_slice_flag, macroblock_offset;
    int err;

    /* Determine macroblock_offset */
    init_get_bits(&gb, buffer, 8 * size);
    if (get_bits_long(&gb, 32) >> 8 != 1) /* start code */
        return AVERROR_INVALIDDATA;
    quantiser_scale_code = get_bits(&gb, 5);
    intra_slice_flag = get_bits1(&gb);
    if (intra_slice_flag) {
        skip_bits(&gb, 8);
        if (skip_1stop_8data_bits(&gb) < 0)
            return AVERROR_INVALIDDATA;
    }
    macroblock_offset = get_bits_count(&gb);

    slice_param = (VASliceParameterBufferMPEG2) {
        .slice_data_size            = size,
        .slice_data_offset          = 0,
        .slice_data_flag            = VA_SLICE_DATA_FLAG_ALL,
        .macroblock_offset          = macroblock_offset,
        .slice_horizontal_position  = s->mb_x,
        .slice_vertical_position    = s->mb_y >> (s->picture_structure != PICT_FRAME),
        .quantiser_scale_code       = quantiser_scale_code,
        .intra_slice_flag           = intra_slice_flag,
    };

    err = ff_vaapi_decode_make_slice_buffer(avctx, pic,
                                            &slice_param, sizeof(slice_param),
                                            buffer, size);
    if (err < 0) {
        ff_vaapi_decode_cancel(avctx, pic);
        return err;
    }

    return 0;
}

const AVHWAccel ff_mpeg2_vaapi_hwaccel = {
    .name                 = "mpeg2_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MPEG2VIDEO,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = &vaapi_mpeg2_start_frame,
    .end_frame            = &vaapi_mpeg2_end_frame,
    .decode_slice         = &vaapi_mpeg2_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = &ff_vaapi_decode_init,
    .uninit               = &ff_vaapi_decode_uninit,
    .frame_params         = &ff_vaapi_common_frame_params,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
