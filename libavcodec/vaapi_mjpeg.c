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

#include <va/va.h>
#include <va/va_dec_jpeg.h>

#include "hwconfig.h"
#include "vaapi_decode.h"
#include "mjpegdec.h"

static int vaapi_mjpeg_start_frame(AVCodecContext          *avctx,
                                   av_unused const uint8_t *buffer,
                                   av_unused uint32_t       size)
{
    const MJpegDecodeContext *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->hwaccel_picture_private;
    VAPictureParameterBufferJPEGBaseline pp;
    int err, i;

    pic->output_surface = ff_vaapi_get_surface_id(s->picture_ptr);

    pp = (VAPictureParameterBufferJPEGBaseline) {
        .picture_width  = avctx->width,
        .picture_height = avctx->height,

        .num_components = s->nb_components,
    };

    for (i = 0; i < s->nb_components; i++) {
        pp.components[i].component_id             = s->component_id[i];
        pp.components[i].h_sampling_factor        = s->h_count[i];
        pp.components[i].v_sampling_factor        = s->v_count[i];
        pp.components[i].quantiser_table_selector = s->quant_index[i];
    }

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAPictureParameterBufferType,
                                            &pp, sizeof(pp));
    if (err < 0)
        goto fail;

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, pic);
    return err;
}

static int vaapi_mjpeg_end_frame(AVCodecContext *avctx)
{
    const MJpegDecodeContext *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->hwaccel_picture_private;

    return ff_vaapi_decode_issue(avctx, pic);
}

static int vaapi_mjpeg_decode_slice(AVCodecContext *avctx,
                                    const uint8_t  *buffer,
                                    uint32_t        size)
{
    const MJpegDecodeContext *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->hwaccel_picture_private;
    VAHuffmanTableBufferJPEGBaseline huff;
    VAIQMatrixBufferJPEGBaseline quant;
    VASliceParameterBufferJPEGBaseline sp;
    int err, i, j;

    memset(&huff, 0, sizeof(huff));
    for (i = 0; i < 2; i++) {
        huff.load_huffman_table[i] = 1;
        for (j = 0; j < 16; j++)
            huff.huffman_table[i].num_dc_codes[j] = s->raw_huffman_lengths[0][i][j];
        for (j = 0; j < 12; j++)
            huff.huffman_table[i].dc_values[j] = s->raw_huffman_values[0][i][j];
        for (j = 0; j < 16; j++)
            huff.huffman_table[i].num_ac_codes[j] = s->raw_huffman_lengths[1][i][j];
        for (j = 0; j < 162; j++)
            huff.huffman_table[i].ac_values[j] = s->raw_huffman_values[1][i][j];
    }

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAHuffmanTableBufferType,
                                            &huff, sizeof(huff));
    if (err < 0)
        goto fail;

    memset(&quant, 0, sizeof(quant));
    for (i = 0; i < 4; i++) {
        quant.load_quantiser_table[i] = 1;
        for (j = 0; j < 64; j++)
            quant.quantiser_table[i][j] = s->quant_matrixes[i][j];
    }

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAIQMatrixBufferType,
                                            &quant, sizeof(quant));
    if (err < 0)
        goto fail;

    sp = (VASliceParameterBufferJPEGBaseline) {
        .slice_data_size   = size,
        .slice_data_offset = 0,
        .slice_data_flag   = VA_SLICE_DATA_FLAG_ALL,

        .slice_horizontal_position = 0,
        .slice_vertical_position   = 0,

        .restart_interval          = s->restart_interval,
        .num_mcus                  = s->mb_width * s->mb_height,
    };

    sp.num_components = s->nb_components;
    for (i = 0; i < s->nb_components; i++) {
        sp.components[i].component_selector = s->component_id[s->comp_index[i]];
        sp.components[i].dc_table_selector  = s->dc_index[i];
        sp.components[i].ac_table_selector  = s->ac_index[i];
    }

    err = ff_vaapi_decode_make_slice_buffer(avctx, pic, &sp, sizeof(sp), buffer, size);
    if (err)
        goto fail;

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, pic);
    return err;
}

const AVHWAccel ff_mjpeg_vaapi_hwaccel = {
    .name                 = "mjpeg_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MJPEG,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = &vaapi_mjpeg_start_frame,
    .end_frame            = &vaapi_mjpeg_end_frame,
    .decode_slice         = &vaapi_mjpeg_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = &ff_vaapi_decode_init,
    .uninit               = &ff_vaapi_decode_uninit,
    .frame_params         = &ff_vaapi_common_frame_params,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
