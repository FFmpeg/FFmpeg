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
#include <va/va_enc_jpeg.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "internal.h"
#include "jpegtables.h"
#include "mjpeg.h"
#include "put_bits.h"
#include "vaapi_encode.h"


// Standard JPEG quantisation tables, in zigzag order.
static const unsigned char vaapi_encode_mjpeg_quant_luminance[64] = {
    16,  11,  12,  14,  12,  10,  16,  14,
    13,  14,  18,  17,  16,  19,  24,  40,
    26,  24,  22,  22,  24,  49,  35,  37,
    29,  40,  58,  51,  61,  60,  57,  51,
    56,  55,  64,  72,  92,  78,  64,  68,
    87,  69,  55,  56,  80, 109,  81,  87,
    95,  98, 103, 104, 103,  62,  77, 113,
   121, 112, 100, 120,  92, 101, 103,  99,
};
static const unsigned char vaapi_encode_mjpeg_quant_chrominance[64] = {
    17,  18,  18,  24,  21,  24,  47,  26,
    26,  47,  99,  66,  56,  66,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
};

typedef struct VAAPIEncodeMJPEGContext {
    VAAPIEncodeContext common;

    int quality;
    int component_subsample_h[3];
    int component_subsample_v[3];

    VAQMatrixBufferJPEG quant_tables;
    VAHuffmanTableBufferJPEGBaseline huffman_tables;
} VAAPIEncodeMJPEGContext;

static av_cold void vaapi_encode_mjpeg_copy_huffman(unsigned char *dst_lengths,
                                                    unsigned char *dst_values,
                                                    const unsigned char *src_lengths,
                                                    const unsigned char *src_values)
{
    int i, mt;

    ++src_lengths;

    mt = 0;
    for (i = 0; i < 16; i++)
        mt += (dst_lengths[i] = src_lengths[i]);

    for (i = 0; i < mt; i++)
        dst_values[i] = src_values[i];
}

static av_cold void vaapi_encode_mjpeg_init_tables(AVCodecContext *avctx)
{
    VAAPIEncodeMJPEGContext          *priv = avctx->priv_data;
    VAQMatrixBufferJPEG             *quant = &priv->quant_tables;
    VAHuffmanTableBufferJPEGBaseline *huff = &priv->huffman_tables;
    int i;

    quant->load_lum_quantiser_matrix = 1;
    quant->load_chroma_quantiser_matrix = 1;

    for (i = 0; i < 64; i++) {
        quant->lum_quantiser_matrix[i] =
            vaapi_encode_mjpeg_quant_luminance[i];
        quant->chroma_quantiser_matrix[i] =
            vaapi_encode_mjpeg_quant_chrominance[i];
    }

    huff->load_huffman_table[0] = 1;
    vaapi_encode_mjpeg_copy_huffman(huff->huffman_table[0].num_dc_codes,
                                    huff->huffman_table[0].dc_values,
                                    avpriv_mjpeg_bits_dc_luminance,
                                    avpriv_mjpeg_val_dc);
    vaapi_encode_mjpeg_copy_huffman(huff->huffman_table[0].num_ac_codes,
                                    huff->huffman_table[0].ac_values,
                                    avpriv_mjpeg_bits_ac_luminance,
                                    avpriv_mjpeg_val_ac_luminance);
    memset(huff->huffman_table[0].pad, 0, sizeof(huff->huffman_table[0].pad));

    huff->load_huffman_table[1] = 1;
    vaapi_encode_mjpeg_copy_huffman(huff->huffman_table[1].num_dc_codes,
                                    huff->huffman_table[1].dc_values,
                                    avpriv_mjpeg_bits_dc_chrominance,
                                    avpriv_mjpeg_val_dc);
    vaapi_encode_mjpeg_copy_huffman(huff->huffman_table[1].num_ac_codes,
                                    huff->huffman_table[1].ac_values,
                                    avpriv_mjpeg_bits_ac_chrominance,
                                    avpriv_mjpeg_val_ac_chrominance);
    memset(huff->huffman_table[1].pad, 0, sizeof(huff->huffman_table[1].pad));
}

static void vaapi_encode_mjpeg_write_marker(PutBitContext *pbc, int marker)
{
    put_bits(pbc, 8, 0xff);
    put_bits(pbc, 8, marker);
}

static int vaapi_encode_mjpeg_write_image_header(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic,
                                                 VAAPIEncodeSlice *slice,
                                                 char *data, size_t *data_len)
{
    VAAPIEncodeMJPEGContext         *priv = avctx->priv_data;
    VAEncPictureParameterBufferJPEG *vpic = pic->codec_picture_params;
    VAEncSliceParameterBufferJPEG *vslice = slice->codec_slice_params;
    PutBitContext pbc;
    int t, i, quant_scale;

    init_put_bits(&pbc, data, *data_len);

    vaapi_encode_mjpeg_write_marker(&pbc, SOI);

    // Quantisation table coefficients are scaled for quality by the driver,
    // so we also need to do it ourselves here so that headers match.
    if (priv->quality < 50)
        quant_scale = 5000 / priv->quality;
    else
        quant_scale = 200 - 2 * priv->quality;

    for (t = 0; t < 2; t++) {
        int q;

        vaapi_encode_mjpeg_write_marker(&pbc, DQT);

        put_bits(&pbc, 16, 3 + 64); // Lq
        put_bits(&pbc, 4, 0); // Pq
        put_bits(&pbc, 4, t); // Tq

        for (i = 0; i < 64; i++) {
            q = i[t ? priv->quant_tables.chroma_quantiser_matrix
                    : priv->quant_tables.lum_quantiser_matrix];
            q = (q * quant_scale) / 100;
            if (q < 1)   q = 1;
            if (q > 255) q = 255;
            put_bits(&pbc, 8, q);
        }
    }

    vaapi_encode_mjpeg_write_marker(&pbc, SOF0);

    put_bits(&pbc, 16, 8 + 3 * vpic->num_components); // Lf
    put_bits(&pbc, 8,  vpic->sample_bit_depth); // P
    put_bits(&pbc, 16, vpic->picture_height);   // Y
    put_bits(&pbc, 16, vpic->picture_width);    // X
    put_bits(&pbc, 8,  vpic->num_components);   // Nf

    for (i = 0; i < vpic->num_components; i++) {
        put_bits(&pbc, 8, vpic->component_id[i]); // Ci
        put_bits(&pbc, 4, priv->component_subsample_h[i]); // Hi
        put_bits(&pbc, 4, priv->component_subsample_v[i]); // Vi
        put_bits(&pbc, 8, vpic->quantiser_table_selector[i]); // Tqi
    }

    for (t = 0; t < 4; t++) {
        int mt;
        unsigned char *lengths, *values;

        vaapi_encode_mjpeg_write_marker(&pbc, DHT);

        if ((t & 1) == 0) {
            lengths = priv->huffman_tables.huffman_table[t / 2].num_dc_codes;
            values  = priv->huffman_tables.huffman_table[t / 2].dc_values;
        } else {
            lengths = priv->huffman_tables.huffman_table[t / 2].num_ac_codes;
            values  = priv->huffman_tables.huffman_table[t / 2].ac_values;
        }

        mt = 0;
        for (i = 0; i < 16; i++)
            mt += lengths[i];

        put_bits(&pbc, 16, 2 + 17 + mt); // Lh
        put_bits(&pbc, 4, t & 1); // Tc
        put_bits(&pbc, 4, t / 2); // Th

        for (i = 0; i < 16; i++)
            put_bits(&pbc, 8, lengths[i]);
        for (i = 0; i < mt; i++)
            put_bits(&pbc, 8, values[i]);
    }

    vaapi_encode_mjpeg_write_marker(&pbc, SOS);

    av_assert0(vpic->num_components == vslice->num_components);

    put_bits(&pbc, 16, 6 + 2 * vslice->num_components); // Ls
    put_bits(&pbc, 8,  vslice->num_components); // Ns

    for (i = 0; i < vslice->num_components; i++) {
        put_bits(&pbc, 8, vslice->components[i].component_selector); // Csj
        put_bits(&pbc, 4, vslice->components[i].dc_table_selector);  // Tdj
        put_bits(&pbc, 4, vslice->components[i].ac_table_selector);  // Taj
    }

    put_bits(&pbc, 8, 0); // Ss
    put_bits(&pbc, 8, 63); // Se
    put_bits(&pbc, 4, 0); // Ah
    put_bits(&pbc, 4, 0); // Al

    *data_len = put_bits_count(&pbc);
    flush_put_bits(&pbc);

    return 0;
}

static int vaapi_encode_mjpeg_write_extra_buffer(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic,
                                                 int index, int *type,
                                                 char *data, size_t *data_len)
{
    VAAPIEncodeMJPEGContext *priv = avctx->priv_data;

    if (index == 0) {
        // Write quantisation tables.
        if (*data_len < sizeof(priv->quant_tables))
            return AVERROR(EINVAL);
        *type = VAQMatrixBufferType;
        memcpy(data, &priv->quant_tables,
               *data_len = sizeof(priv->quant_tables));

    } else if (index == 1) {
        // Write huffman tables.
        if (*data_len < sizeof(priv->huffman_tables))
            return AVERROR(EINVAL);
        *type = VAHuffmanTableBufferType;
        memcpy(data, &priv->huffman_tables,
               *data_len = sizeof(priv->huffman_tables));

    } else {
        return AVERROR_EOF;
    }
    return 0;
}

static int vaapi_encode_mjpeg_init_picture_params(AVCodecContext *avctx,
                                                  VAAPIEncodePicture *pic)
{
    VAAPIEncodeMJPEGContext         *priv = avctx->priv_data;
    VAEncPictureParameterBufferJPEG *vpic = pic->codec_picture_params;

    vpic->reconstructed_picture = pic->recon_surface;
    vpic->coded_buf = pic->output_buffer;

    vpic->picture_width  = avctx->width;
    vpic->picture_height = avctx->height;

    vpic->pic_flags.bits.profile      = 0;
    vpic->pic_flags.bits.progressive  = 0;
    vpic->pic_flags.bits.huffman      = 1;
    vpic->pic_flags.bits.interleaved  = 0;
    vpic->pic_flags.bits.differential = 0;

    vpic->sample_bit_depth = 8;
    vpic->num_scan = 1;

    vpic->num_components = 3;

    vpic->component_id[0] = 1;
    vpic->component_id[1] = 2;
    vpic->component_id[2] = 3;

    priv->component_subsample_h[0] = 2;
    priv->component_subsample_v[0] = 2;
    priv->component_subsample_h[1] = 1;
    priv->component_subsample_v[1] = 1;
    priv->component_subsample_h[2] = 1;
    priv->component_subsample_v[2] = 1;

    vpic->quantiser_table_selector[0] = 0;
    vpic->quantiser_table_selector[1] = 1;
    vpic->quantiser_table_selector[2] = 1;

    vpic->quality = priv->quality;

    pic->nb_slices = 1;

    return 0;
}

static int vaapi_encode_mjpeg_init_slice_params(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic,
                                                VAAPIEncodeSlice *slice)
{
    VAEncPictureParameterBufferJPEG *vpic = pic->codec_picture_params;
    VAEncSliceParameterBufferJPEG *vslice = slice->codec_slice_params;
    int i;

    vslice->restart_interval = 0;

    vslice->num_components = vpic->num_components;
    for (i = 0; i < vslice->num_components; i++) {
        vslice->components[i].component_selector = i + 1;
        vslice->components[i].dc_table_selector = (i > 0);
        vslice->components[i].ac_table_selector = (i > 0);
    }

    return 0;
}

static av_cold int vaapi_encode_mjpeg_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext       *ctx = avctx->priv_data;
    VAAPIEncodeMJPEGContext *priv = avctx->priv_data;

    priv->quality = avctx->global_quality;
    if (priv->quality < 1 || priv->quality > 100) {
        av_log(avctx, AV_LOG_ERROR, "Invalid quality value %d "
               "(must be 1-100).\n", priv->quality);
        return AVERROR(EINVAL);
    }

    // Hack: the implementation calls the JPEG image header (which we
    // will use in the same way as a slice header) generic "raw data".
    // Therefore, if after the packed header capability check we have
    // PACKED_HEADER_RAW_DATA available, rewrite it as
    // PACKED_HEADER_SLICE so that the header-writing code can do the
    // right thing.
    if (ctx->va_packed_headers & VA_ENC_PACKED_HEADER_RAW_DATA) {
        ctx->va_packed_headers &= ~VA_ENC_PACKED_HEADER_RAW_DATA;
        ctx->va_packed_headers |=  VA_ENC_PACKED_HEADER_SLICE;
    }

    vaapi_encode_mjpeg_init_tables(avctx);

    return 0;
}

static const VAAPIEncodeType vaapi_encode_type_mjpeg = {
    .configure             = &vaapi_encode_mjpeg_configure,

    .picture_params_size   = sizeof(VAEncPictureParameterBufferJPEG),
    .init_picture_params   = &vaapi_encode_mjpeg_init_picture_params,

    .slice_params_size     = sizeof(VAEncSliceParameterBufferJPEG),
    .init_slice_params     = &vaapi_encode_mjpeg_init_slice_params,

    .slice_header_type     = VAEncPackedHeaderRawData,
    .write_slice_header    = &vaapi_encode_mjpeg_write_image_header,

    .write_extra_buffer    = &vaapi_encode_mjpeg_write_extra_buffer,
};

static av_cold int vaapi_encode_mjpeg_init(AVCodecContext *avctx)
{
    VAAPIEncodeContext *ctx = avctx->priv_data;

    ctx->codec = &vaapi_encode_type_mjpeg;

    ctx->va_profile    = VAProfileJPEGBaseline;
    ctx->va_entrypoint = VAEntrypointEncPicture;

    ctx->va_rt_format = VA_RT_FORMAT_YUV420;

    ctx->va_rc_mode = VA_RC_CQP;

    // The JPEG image header - see note above.
    ctx->va_packed_headers =
        VA_ENC_PACKED_HEADER_RAW_DATA;

    ctx->surface_width  = FFALIGN(avctx->width,  8);
    ctx->surface_height = FFALIGN(avctx->height, 8);

    return ff_vaapi_encode_init(avctx);
}

static const AVCodecDefault vaapi_encode_mjpeg_defaults[] = {
    { "global_quality", "80" },
    { NULL },
};

static const AVClass vaapi_encode_mjpeg_class = {
    .class_name = "mjpeg_vaapi",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mjpeg_vaapi_encoder = {
    .name           = "mjpeg_vaapi",
    .long_name      = NULL_IF_CONFIG_SMALL("MJPEG (VAAPI)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MJPEG,
    .priv_data_size = sizeof(VAAPIEncodeMJPEGContext),
    .init           = &vaapi_encode_mjpeg_init,
    .encode2        = &ff_vaapi_encode2,
    .close          = &ff_vaapi_encode_close,
    .priv_class     = &vaapi_encode_mjpeg_class,
    .capabilities   = AV_CODEC_CAP_HARDWARE,
    .defaults       = vaapi_encode_mjpeg_defaults,
    .pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
    .wrapper_name   = "vaapi",
};
