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
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "bytestream.h"
#include "cbs.h"
#include "cbs_jpeg.h"
#include "codec_internal.h"
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

    // User options.
    int jfif;
    int huffman;

    // Derived settings.
    int quality;
    uint8_t jfif_data[14];

    // Writer structures.
    JPEGRawFrameHeader     frame_header;
    JPEGRawScan            scan;
    JPEGRawApplicationData jfif_header;
    JPEGRawQuantisationTableSpecification quant_tables;
    JPEGRawHuffmanTableSpecification      huffman_tables;

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment current_fragment;
} VAAPIEncodeMJPEGContext;

static int vaapi_encode_mjpeg_write_image_header(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic,
                                                 VAAPIEncodeSlice *slice,
                                                 char *data, size_t *data_len)
{
    VAAPIEncodeMJPEGContext *priv = avctx->priv_data;
    CodedBitstreamFragment  *frag = &priv->current_fragment;
    int err;

    if (priv->jfif) {
        err = ff_cbs_insert_unit_content(frag, -1,
                                         JPEG_MARKER_APPN + 0,
                                         &priv->jfif_header, NULL);
        if (err < 0)
            goto fail;
    }

    err = ff_cbs_insert_unit_content(frag, -1,
                                     JPEG_MARKER_DQT,
                                     &priv->quant_tables, NULL);
    if (err < 0)
        goto fail;

    err = ff_cbs_insert_unit_content(frag, -1,
                                     JPEG_MARKER_SOF0,
                                     &priv->frame_header, NULL);
    if (err < 0)
        goto fail;

    if (priv->huffman) {
        err = ff_cbs_insert_unit_content(frag, -1,
                                         JPEG_MARKER_DHT,
                                         &priv->huffman_tables, NULL);
        if (err < 0)
            goto fail;
    }

    err = ff_cbs_insert_unit_content(frag, -1,
                                     JPEG_MARKER_SOS,
                                     &priv->scan, NULL);
    if (err < 0)
        goto fail;

    err = ff_cbs_write_fragment_data(priv->cbc, frag);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write image header.\n");
        goto fail;
    }

    if (*data_len < 8 * frag->data_size) {
        av_log(avctx, AV_LOG_ERROR, "Image header too large: "
               "%zu < %zu.\n", *data_len, 8 * frag->data_size);
        err = AVERROR(ENOSPC);
        goto fail;
    }

    // Remove the EOI at the end of the fragment.
    memcpy(data, frag->data, frag->data_size - 2);
    *data_len = 8 * (frag->data_size - 2);

    err = 0;
fail:
    ff_cbs_fragment_reset(frag);
    return err;
}

static int vaapi_encode_mjpeg_write_extra_buffer(AVCodecContext *avctx,
                                                 VAAPIEncodePicture *pic,
                                                 int index, int *type,
                                                 char *data, size_t *data_len)
{
    VAAPIEncodeMJPEGContext *priv = avctx->priv_data;
    int t, i, k;

    if (index == 0) {
        // Write quantisation tables.
        JPEGRawFrameHeader                     *fh = &priv->frame_header;
        JPEGRawQuantisationTableSpecification *dqt = &priv->quant_tables;
        VAQMatrixBufferJPEG *quant;

        if (*data_len < sizeof(*quant))
            return AVERROR(ENOSPC);
        *type     = VAQMatrixBufferType;
        *data_len = sizeof(*quant);

        quant = (VAQMatrixBufferJPEG*)data;
        memset(quant, 0, sizeof(*quant));

        quant->load_lum_quantiser_matrix = 1;
        for (i = 0; i < 64; i++)
            quant->lum_quantiser_matrix[i] = dqt->table[fh->Tq[0]].Q[i];

        if (fh->Nf > 1) {
            quant->load_chroma_quantiser_matrix = 1;
            for (i = 0; i < 64; i++)
                quant->chroma_quantiser_matrix[i] =
                    dqt->table[fh->Tq[1]].Q[i];
        }

    } else if (index == 1) {
        // Write huffman tables.
        JPEGRawScanHeader                 *sh = &priv->scan.header;
        JPEGRawHuffmanTableSpecification *dht = &priv->huffman_tables;
        VAHuffmanTableBufferJPEGBaseline *huff;

        if (*data_len < sizeof(*huff))
            return AVERROR(ENOSPC);
        *type     = VAHuffmanTableBufferType;
        *data_len = sizeof(*huff);

        huff = (VAHuffmanTableBufferJPEGBaseline*)data;
        memset(huff, 0, sizeof(*huff));

        for (t = 0; t < 1 + (sh->Ns > 1); t++) {
            const JPEGRawHuffmanTable *ht;

            huff->load_huffman_table[t] = 1;

            ht = &dht->table[2 * t];
            for (i = k = 0; i < 16; i++)
                k += (huff->huffman_table[t].num_dc_codes[i] = ht->L[i]);
            av_assert0(k <= sizeof(huff->huffman_table[t].dc_values));
            for (i = 0; i < k; i++)
                huff->huffman_table[t].dc_values[i] = ht->V[i];

            ht = &dht->table[2 * t + 1];
            for (i = k = 0; i < 16; i++)
                k += (huff->huffman_table[t].num_ac_codes[i] = ht->L[i]);
            av_assert0(k <= sizeof(huff->huffman_table[t].ac_values));
            for (i = 0; i < k; i++)
                huff->huffman_table[t].ac_values[i] = ht->V[i];
        }

    } else {
        return AVERROR_EOF;
    }
    return 0;
}

static int vaapi_encode_mjpeg_init_picture_params(AVCodecContext *avctx,
                                                  VAAPIEncodePicture *vaapi_pic)
{
    FFHWBaseEncodeContext       *base_ctx = avctx->priv_data;
    VAAPIEncodeMJPEGContext         *priv = avctx->priv_data;
    const FFHWBaseEncodePicture      *pic = &vaapi_pic->base;
    JPEGRawFrameHeader                *fh = &priv->frame_header;
    JPEGRawScanHeader                 *sh = &priv->scan.header;
    VAEncPictureParameterBufferJPEG *vpic = vaapi_pic->codec_picture_params;
    const AVPixFmtDescriptor *desc;
    const uint8_t components_rgb[3] = { 'R', 'G', 'B' };
    const uint8_t components_yuv[3] = {  1,   2,   3  };
    const uint8_t *components;
    int t, i, quant_scale, len;

    av_assert0(pic->type == FF_HW_PICTURE_TYPE_IDR);

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    av_assert0(desc);
    if (desc->flags & AV_PIX_FMT_FLAG_RGB)
        components = components_rgb;
    else
        components = components_yuv;

    // Frame header.

    fh->P  = 8;
    fh->Y  = avctx->height;
    fh->X  = avctx->width;
    fh->Nf = desc->nb_components;

    for (i = 0; i < fh->Nf; i++) {
        fh->C[i] = components[i];
        fh->H[i] = 1 + (i == 0 ? desc->log2_chroma_w : 0);
        fh->V[i] = 1 + (i == 0 ? desc->log2_chroma_h : 0);

        fh->Tq[i] = !!i;
    }

    fh->Lf = 8 + 3 * fh->Nf;

    // JFIF header.
    if (priv->jfif) {
        JPEGRawApplicationData *app = &priv->jfif_header;
        AVRational sar = pic->input_image->sample_aspect_ratio;
        int sar_w, sar_h;
        PutByteContext pbc;

        bytestream2_init_writer(&pbc, priv->jfif_data,
                                sizeof(priv->jfif_data));

        bytestream2_put_buffer(&pbc, "JFIF", 5);
        bytestream2_put_be16(&pbc, 0x0102);
        bytestream2_put_byte(&pbc, 0);

        av_reduce(&sar_w, &sar_h, sar.num, sar.den, 65535);
        if (sar_w && sar_h) {
            bytestream2_put_be16(&pbc, sar_w);
            bytestream2_put_be16(&pbc, sar_h);
        } else {
            bytestream2_put_be16(&pbc, 1);
            bytestream2_put_be16(&pbc, 1);
        }

        bytestream2_put_byte(&pbc, 0);
        bytestream2_put_byte(&pbc, 0);

        av_assert0(bytestream2_get_bytes_left_p(&pbc) == 0);

        app->Lp     = 2 + sizeof(priv->jfif_data);
        app->Ap     = priv->jfif_data;
        app->Ap_ref = NULL;
    }

    // Quantisation tables.

    if (priv->quality < 50)
        quant_scale = 5000 / priv->quality;
    else
        quant_scale = 200 - 2 * priv->quality;

    len = 2;

    for (t = 0; t < 1 + (fh->Nf > 1); t++) {
        JPEGRawQuantisationTable *quant = &priv->quant_tables.table[t];
        const uint8_t *data = t == 0 ?
            vaapi_encode_mjpeg_quant_luminance :
            vaapi_encode_mjpeg_quant_chrominance;

        quant->Pq = 0;
        quant->Tq = t;
        for (i = 0; i < 64; i++)
            quant->Q[i] = av_clip(data[i] * quant_scale / 100, 1, 255);

        len += 65;
    }

    priv->quant_tables.Lq = len;

    // Huffman tables.

    len = 2;

    for (t = 0; t < 2 + 2 * (fh->Nf > 1); t++) {
        JPEGRawHuffmanTable *huff = &priv->huffman_tables.table[t];
        const uint8_t *lengths, *values;
        int k;

        switch (t) {
        case 0:
            lengths = ff_mjpeg_bits_dc_luminance + 1;
            values  = ff_mjpeg_val_dc;
            break;
        case 1:
            lengths = ff_mjpeg_bits_ac_luminance + 1;
            values  = ff_mjpeg_val_ac_luminance;
            break;
        case 2:
            lengths = ff_mjpeg_bits_dc_chrominance + 1;
            values  = ff_mjpeg_val_dc;
            break;
        case 3:
            lengths = ff_mjpeg_bits_ac_chrominance + 1;
            values  = ff_mjpeg_val_ac_chrominance;
            break;
        }

        huff->Tc = t % 2;
        huff->Th = t / 2;

        for (i = k = 0; i < 16; i++)
            k += (huff->L[i] = lengths[i]);

        for (i = 0; i < k; i++)
            huff->V[i] = values[i];

        len += 17 + k;
    }

    priv->huffman_tables.Lh = len;

    // Scan header.

    sh->Ns = fh->Nf;

    for (i = 0; i < fh->Nf; i++) {
        sh->Cs[i] = fh->C[i];
        sh->Td[i] = i > 0;
        sh->Ta[i] = i > 0;
    }

    sh->Ss = 0;
    sh->Se = 63;
    sh->Ah = 0;
    sh->Al = 0;

    sh->Ls = 6 + 2 * sh->Ns;


    *vpic = (VAEncPictureParameterBufferJPEG) {
        .reconstructed_picture = vaapi_pic->recon_surface,
        .coded_buf             = vaapi_pic->output_buffer,

        .picture_width  = fh->X,
        .picture_height = fh->Y,

        .pic_flags.bits = {
            .profile      = 0,
            .progressive  = 0,
            .huffman      = 1,
            .interleaved  = 0,
            .differential = 0,
        },

        .sample_bit_depth = fh->P,
        .num_scan         = 1,
        .num_components   = fh->Nf,

        // The driver modifies the provided quantisation tables according
        // to this quality value; the middle value of 50 makes that the
        // identity so that they are used unchanged.
        .quality = 50,
    };

    for (i = 0; i < fh->Nf; i++) {
        vpic->component_id[i]             = fh->C[i];
        vpic->quantiser_table_selector[i] = fh->Tq[i];
    }

    vaapi_pic->nb_slices = 1;

    return 0;
}

static int vaapi_encode_mjpeg_init_slice_params(AVCodecContext *avctx,
                                                VAAPIEncodePicture *pic,
                                                VAAPIEncodeSlice *slice)
{
    VAAPIEncodeMJPEGContext         *priv = avctx->priv_data;
    JPEGRawScanHeader                 *sh = &priv->scan.header;
    VAEncSliceParameterBufferJPEG *vslice = slice->codec_slice_params;
    int i;

    *vslice = (VAEncSliceParameterBufferJPEG) {
        .restart_interval = 0,
        .num_components   = sh->Ns,
    };

    for (i = 0; i < sh->Ns; i++) {
        vslice->components[i].component_selector = sh->Cs[i];
        vslice->components[i].dc_table_selector  = sh->Td[i];
        vslice->components[i].ac_table_selector  = sh->Ta[i];
    }

    return 0;
}

static av_cold int vaapi_encode_mjpeg_get_encoder_caps(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    const AVPixFmtDescriptor *desc;

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    av_assert0(desc);

    base_ctx->surface_width  = FFALIGN(avctx->width,  8 << desc->log2_chroma_w);
    base_ctx->surface_height = FFALIGN(avctx->height, 8 << desc->log2_chroma_h);

    return 0;
}

static av_cold int vaapi_encode_mjpeg_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext       *ctx = avctx->priv_data;
    VAAPIEncodeMJPEGContext *priv = avctx->priv_data;
    int err;

    priv->quality = ctx->rc_quality;
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

    err = ff_cbs_init(&priv->cbc, AV_CODEC_ID_MJPEG, avctx);
    if (err < 0)
        return err;

    return 0;
}

static const VAAPIEncodeProfile vaapi_encode_mjpeg_profiles[] = {
    { AV_PROFILE_MJPEG_HUFFMAN_BASELINE_DCT,
            8, 1, 0, 0, VAProfileJPEGBaseline },
    { AV_PROFILE_MJPEG_HUFFMAN_BASELINE_DCT,
            8, 3, 1, 1, VAProfileJPEGBaseline },
    { AV_PROFILE_MJPEG_HUFFMAN_BASELINE_DCT,
            8, 3, 1, 0, VAProfileJPEGBaseline },
    { AV_PROFILE_MJPEG_HUFFMAN_BASELINE_DCT,
            8, 3, 0, 0, VAProfileJPEGBaseline },
    { AV_PROFILE_UNKNOWN }
};

static const VAAPIEncodeType vaapi_encode_type_mjpeg = {
    .profiles              = vaapi_encode_mjpeg_profiles,

    .flags                 = FF_HW_FLAG_CONSTANT_QUALITY_ONLY |
                             FF_HW_FLAG_INTRA_ONLY,

    .get_encoder_caps      = &vaapi_encode_mjpeg_get_encoder_caps,
    .configure             = &vaapi_encode_mjpeg_configure,

    .default_quality       = 80,

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

    // The JPEG image header - see note above.
    ctx->desired_packed_headers =
        VA_ENC_PACKED_HEADER_RAW_DATA;

    return ff_vaapi_encode_init(avctx);
}

static av_cold int vaapi_encode_mjpeg_close(AVCodecContext *avctx)
{
    VAAPIEncodeMJPEGContext *priv = avctx->priv_data;

    ff_cbs_fragment_free(&priv->current_fragment);
    ff_cbs_close(&priv->cbc);

    return ff_vaapi_encode_close(avctx);
}

#define OFFSET(x) offsetof(VAAPIEncodeMJPEGContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)
static const AVOption vaapi_encode_mjpeg_options[] = {
    HW_BASE_ENCODE_COMMON_OPTIONS,
    VAAPI_ENCODE_COMMON_OPTIONS,

    { "jfif", "Include JFIF header",
      OFFSET(jfif), AV_OPT_TYPE_BOOL,
      { .i64 = 0 }, 0, 1, FLAGS },
    { "huffman", "Include huffman tables",
      OFFSET(huffman), AV_OPT_TYPE_BOOL,
      { .i64 = 1 }, 0, 1, FLAGS },

    { NULL },
};

static const FFCodecDefault vaapi_encode_mjpeg_defaults[] = {
    { "b",              "0"  },
    { NULL },
};

static const AVClass vaapi_encode_mjpeg_class = {
    .class_name = "mjpeg_vaapi",
    .item_name  = av_default_item_name,
    .option     = vaapi_encode_mjpeg_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_mjpeg_vaapi_encoder = {
    .p.name         = "mjpeg_vaapi",
    CODEC_LONG_NAME("MJPEG (VAAPI)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MJPEG,
    .priv_data_size = sizeof(VAAPIEncodeMJPEGContext),
    .init           = &vaapi_encode_mjpeg_init,
    FF_CODEC_RECEIVE_PACKET_CB(&ff_vaapi_encode_receive_packet),
    .close          = &vaapi_encode_mjpeg_close,
    .p.priv_class   = &vaapi_encode_mjpeg_class,
    .p.capabilities = AV_CODEC_CAP_HARDWARE | AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = vaapi_encode_mjpeg_defaults,
    .p.pix_fmts = (const enum AVPixelFormat[]) {
        AV_PIX_FMT_VAAPI,
        AV_PIX_FMT_NONE,
    },
    .color_ranges   = AVCOL_RANGE_MPEG, /* FIXME: implement tagging */
    .hw_configs     = ff_vaapi_encode_hw_configs,
    .p.wrapper_name = "vaapi",
};
