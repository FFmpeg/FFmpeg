/*
 * MJPEG decoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2003-2004 Michael Niedermayer
 *
 * Support for external huffman table, various fixes (AVID workaround),
 * aspecting, new decode_frame mechanism and apple mjpeg-b support
 *                                  by Alex Beregszaszi
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

/**
 * @file
 * MJPEG decoder.
 */

#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "copy_block.h"
#include "internal.h"
#include "mjpeg.h"
#include "mjpegdec.h"
#include "jpeglsdec.h"
#include "tiff.h"
#include "exif.h"
#include "bytestream.h"


static int build_vlc(VLC *vlc, const uint8_t *bits_table,
                     const uint8_t *val_table, int nb_codes,
                     int use_static, int is_ac)
{
    uint8_t huff_size[256] = { 0 };
    uint16_t huff_code[256];
    uint16_t huff_sym[256];
    int i;

    av_assert0(nb_codes <= 256);

    ff_mjpeg_build_huffman_codes(huff_size, huff_code, bits_table, val_table);

    for (i = 0; i < 256; i++)
        huff_sym[i] = i + 16 * is_ac;

    if (is_ac)
        huff_sym[0] = 16 * 256;

    return ff_init_vlc_sparse(vlc, 9, nb_codes, huff_size, 1, 1,
                              huff_code, 2, 2, huff_sym, 2, 2, use_static);
}

static void build_basic_mjpeg_vlc(MJpegDecodeContext *s)
{
    build_vlc(&s->vlcs[0][0], avpriv_mjpeg_bits_dc_luminance,
              avpriv_mjpeg_val_dc, 12, 0, 0);
    build_vlc(&s->vlcs[0][1], avpriv_mjpeg_bits_dc_chrominance,
              avpriv_mjpeg_val_dc, 12, 0, 0);
    build_vlc(&s->vlcs[1][0], avpriv_mjpeg_bits_ac_luminance,
              avpriv_mjpeg_val_ac_luminance, 251, 0, 1);
    build_vlc(&s->vlcs[1][1], avpriv_mjpeg_bits_ac_chrominance,
              avpriv_mjpeg_val_ac_chrominance, 251, 0, 1);
    build_vlc(&s->vlcs[2][0], avpriv_mjpeg_bits_ac_luminance,
              avpriv_mjpeg_val_ac_luminance, 251, 0, 0);
    build_vlc(&s->vlcs[2][1], avpriv_mjpeg_bits_ac_chrominance,
              avpriv_mjpeg_val_ac_chrominance, 251, 0, 0);
}

av_cold int ff_mjpeg_decode_init(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;

    if (!s->picture_ptr)
        s->picture_ptr = &s->picture;
    avcodec_get_frame_defaults(&s->picture);

    s->avctx = avctx;
    ff_hpeldsp_init(&s->hdsp, avctx->flags);
    ff_dsputil_init(&s->dsp, avctx);
    ff_init_scantable(s->dsp.idct_permutation, &s->scantable, ff_zigzag_direct);
    s->buffer_size   = 0;
    s->buffer        = NULL;
    s->start_code    = -1;
    s->first_picture = 1;
    s->got_picture   = 0;
    s->org_height    = avctx->coded_height;
    avctx->chroma_sample_location = AVCHROMA_LOC_CENTER;

    build_basic_mjpeg_vlc(s);

    if (s->extern_huff) {
        av_log(avctx, AV_LOG_INFO, "using external huffman table\n");
        init_get_bits(&s->gb, avctx->extradata, avctx->extradata_size * 8);
        if (ff_mjpeg_decode_dht(s)) {
            av_log(avctx, AV_LOG_ERROR,
                   "error using external huffman table, switching back to internal\n");
            build_basic_mjpeg_vlc(s);
        }
    }
    if (avctx->field_order == AV_FIELD_BB) { /* quicktime icefloe 019 */
        s->interlace_polarity = 1;           /* bottom field first */
        av_log(avctx, AV_LOG_DEBUG, "bottom field first\n");
    }
    if (avctx->codec->id == AV_CODEC_ID_AMV)
        s->flipped = 1;

    return 0;
}


/* quantize tables */
int ff_mjpeg_decode_dqt(MJpegDecodeContext *s)
{
    int len, index, i, j;

    len = get_bits(&s->gb, 16) - 2;

    while (len >= 65) {
        int pr = get_bits(&s->gb, 4);
        if (pr > 1) {
            av_log(s->avctx, AV_LOG_ERROR, "dqt: invalid precision\n");
            return AVERROR_INVALIDDATA;
        }
        index = get_bits(&s->gb, 4);
        if (index >= 4)
            return -1;
        av_log(s->avctx, AV_LOG_DEBUG, "index=%d\n", index);
        /* read quant table */
        for (i = 0; i < 64; i++) {
            j = s->scantable.permutated[i];
            s->quant_matrixes[index][j] = get_bits(&s->gb, pr ? 16 : 8);
        }

        // XXX FIXME finetune, and perhaps add dc too
        s->qscale[index] = FFMAX(s->quant_matrixes[index][s->scantable.permutated[1]],
                                 s->quant_matrixes[index][s->scantable.permutated[8]]) >> 1;
        av_log(s->avctx, AV_LOG_DEBUG, "qscale[%d]: %d\n",
               index, s->qscale[index]);
        len -= 65;
    }
    return 0;
}

/* decode huffman tables and build VLC decoders */
int ff_mjpeg_decode_dht(MJpegDecodeContext *s)
{
    int len, index, i, class, n, v, code_max;
    uint8_t bits_table[17];
    uint8_t val_table[256];
    int ret = 0;

    len = get_bits(&s->gb, 16) - 2;

    while (len > 0) {
        if (len < 17)
            return AVERROR_INVALIDDATA;
        class = get_bits(&s->gb, 4);
        if (class >= 2)
            return AVERROR_INVALIDDATA;
        index = get_bits(&s->gb, 4);
        if (index >= 4)
            return AVERROR_INVALIDDATA;
        n = 0;
        for (i = 1; i <= 16; i++) {
            bits_table[i] = get_bits(&s->gb, 8);
            n += bits_table[i];
        }
        len -= 17;
        if (len < n || n > 256)
            return AVERROR_INVALIDDATA;

        code_max = 0;
        for (i = 0; i < n; i++) {
            v = get_bits(&s->gb, 8);
            if (v > code_max)
                code_max = v;
            val_table[i] = v;
        }
        len -= n;

        /* build VLC and flush previous vlc if present */
        ff_free_vlc(&s->vlcs[class][index]);
        av_log(s->avctx, AV_LOG_DEBUG, "class=%d index=%d nb_codes=%d\n",
               class, index, code_max + 1);
        if ((ret = build_vlc(&s->vlcs[class][index], bits_table, val_table,
                             code_max + 1, 0, class > 0)) < 0)
            return ret;

        if (class > 0) {
            ff_free_vlc(&s->vlcs[2][index]);
            if ((ret = build_vlc(&s->vlcs[2][index], bits_table, val_table,
                                 code_max + 1, 0, 0)) < 0)
                return ret;
        }
    }
    return 0;
}

int ff_mjpeg_decode_sof(MJpegDecodeContext *s)
{
    int len, nb_components, i, width, height, pix_fmt_id;
    int h_count[MAX_COMPONENTS];
    int v_count[MAX_COMPONENTS];

    s->cur_scan = 0;
    s->upscale_h = s->upscale_v = 0;

    /* XXX: verify len field validity */
    len     = get_bits(&s->gb, 16);
    s->avctx->bits_per_raw_sample =
    s->bits = get_bits(&s->gb, 8);

    if (s->pegasus_rct)
        s->bits = 9;
    if (s->bits == 9 && !s->pegasus_rct)
        s->rct  = 1;    // FIXME ugly

    if(s->lossless && s->avctx->lowres){
        av_log(s->avctx, AV_LOG_ERROR, "lowres is not possible with lossless jpeg\n");
        return -1;
    }

    height = get_bits(&s->gb, 16);
    width  = get_bits(&s->gb, 16);

    // HACK for odd_height.mov
    if (s->interlaced && s->width == width && s->height == height + 1)
        height= s->height;

    av_log(s->avctx, AV_LOG_DEBUG, "sof0: picture: %dx%d\n", width, height);
    if (av_image_check_size(width, height, 0, s->avctx))
        return AVERROR_INVALIDDATA;

    nb_components = get_bits(&s->gb, 8);
    if (nb_components <= 0 ||
        nb_components > MAX_COMPONENTS)
        return -1;
    if (s->interlaced && (s->bottom_field == !s->interlace_polarity)) {
        if (nb_components != s->nb_components) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "nb_components changing in interlaced picture\n");
            return AVERROR_INVALIDDATA;
        }
    }
    if (s->ls && !(s->bits <= 8 || nb_components == 1)) {
        avpriv_report_missing_feature(s->avctx,
                                      "JPEG-LS that is not <= 8 "
                                      "bits/component or 16-bit gray");
        return AVERROR_PATCHWELCOME;
    }
    s->nb_components = nb_components;
    s->h_max         = 1;
    s->v_max         = 1;
    memset(h_count, 0, sizeof(h_count));
    memset(v_count, 0, sizeof(v_count));
    for (i = 0; i < nb_components; i++) {
        /* component id */
        s->component_id[i] = get_bits(&s->gb, 8) - 1;
        h_count[i]         = get_bits(&s->gb, 4);
        v_count[i]         = get_bits(&s->gb, 4);
        /* compute hmax and vmax (only used in interleaved case) */
        if (h_count[i] > s->h_max)
            s->h_max = h_count[i];
        if (v_count[i] > s->v_max)
            s->v_max = v_count[i];
        s->quant_index[i] = get_bits(&s->gb, 8);
        if (s->quant_index[i] >= 4) {
            av_log(s->avctx, AV_LOG_ERROR, "quant_index is invalid\n");
            return AVERROR_INVALIDDATA;
        }
        if (!h_count[i] || !v_count[i]) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Invalid sampling factor in component %d %d:%d\n",
                   i, h_count[i], v_count[i]);
            return AVERROR_INVALIDDATA;
        }

        av_log(s->avctx, AV_LOG_DEBUG, "component %d %d:%d id: %d quant:%d\n",
               i, h_count[i], v_count[i],
               s->component_id[i], s->quant_index[i]);
    }

    if (s->ls && (s->h_max > 1 || s->v_max > 1)) {
        avpriv_report_missing_feature(s->avctx, "Subsampling in JPEG-LS");
        return AVERROR_PATCHWELCOME;
    }


    /* if different size, realloc/alloc picture */
    if (   width != s->width || height != s->height
        || memcmp(s->h_count, h_count, sizeof(h_count))
        || memcmp(s->v_count, v_count, sizeof(v_count))) {

        s->width      = width;
        s->height     = height;
        memcpy(s->h_count, h_count, sizeof(h_count));
        memcpy(s->v_count, v_count, sizeof(v_count));
        s->interlaced = 0;
        s->got_picture = 0;

        /* test interlaced mode */
        if (s->first_picture   &&
            s->org_height != 0 &&
            s->height < ((s->org_height * 3) / 4)) {
            s->interlaced                    = 1;
            s->bottom_field                  = s->interlace_polarity;
            s->picture_ptr->interlaced_frame = 1;
            s->picture_ptr->top_field_first  = !s->interlace_polarity;
            height *= 2;
        }

        avcodec_set_dimensions(s->avctx, width, height);

        s->first_picture = 0;
    }

    if (s->interlaced && (s->bottom_field == !s->interlace_polarity)) {
        if (s->progressive) {
            avpriv_request_sample(s->avctx, "progressively coded interlaced picture");
            return AVERROR_INVALIDDATA;
        }
    } else{
        if (s->v_max == 1 && s->h_max == 1 && s->lossless==1 && (nb_components==3 || nb_components==4))
            s->rgb = 1;
        else if (!s->lossless)
            s->rgb = 0;
    /* XXX: not complete test ! */
    pix_fmt_id = (s->h_count[0] << 28) | (s->v_count[0] << 24) |
                 (s->h_count[1] << 20) | (s->v_count[1] << 16) |
                 (s->h_count[2] << 12) | (s->v_count[2] <<  8) |
                 (s->h_count[3] <<  4) |  s->v_count[3];
    av_log(s->avctx, AV_LOG_DEBUG, "pix fmt id %x\n", pix_fmt_id);
    /* NOTE we do not allocate pictures large enough for the possible
     * padding of h/v_count being 4 */
    if (!(pix_fmt_id & 0xD0D0D0D0))
        pix_fmt_id -= (pix_fmt_id & 0xF0F0F0F0) >> 1;
    if (!(pix_fmt_id & 0x0D0D0D0D))
        pix_fmt_id -= (pix_fmt_id & 0x0F0F0F0F) >> 1;

    switch (pix_fmt_id) {
    case 0x11111100:
        if (s->rgb)
            s->avctx->pix_fmt = s->bits <= 9 ? AV_PIX_FMT_BGR24 : AV_PIX_FMT_BGR48;
        else {
            if (s->component_id[0] == 'Q' && s->component_id[1] == 'F' && s->component_id[2] == 'A') {
                s->avctx->pix_fmt = s->bits <= 8 ? AV_PIX_FMT_GBRP : AV_PIX_FMT_GBRP16;
            } else {
                if (s->bits <= 8) s->avctx->pix_fmt = s->cs_itu601 ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUVJ444P;
                else              s->avctx->pix_fmt = AV_PIX_FMT_YUV444P16;
            s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
            }
        }
        av_assert0(s->nb_components == 3);
        break;
    case 0x11111111:
        if (s->rgb)
            s->avctx->pix_fmt = s->bits <= 9 ? AV_PIX_FMT_ABGR : AV_PIX_FMT_RGBA64;
        else {
            s->avctx->pix_fmt = s->bits <= 8 ? AV_PIX_FMT_YUVA444P : AV_PIX_FMT_YUVA444P16;
            s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        }
        av_assert0(s->nb_components == 4);
        break;
    case 0x12121100:
    case 0x22122100:
        if (s->bits <= 8) s->avctx->pix_fmt = s->cs_itu601 ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUVJ444P;
        else
            goto unk_pixfmt;
        s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        s->upscale_v = 2;
        s->upscale_h = (pix_fmt_id == 0x22122100);
        s->chroma_height = s->height;
        break;
    case 0x21211100:
    case 0x22211200:
        if (s->bits <= 8) s->avctx->pix_fmt = s->cs_itu601 ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUVJ444P;
        else
            goto unk_pixfmt;
        s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        s->upscale_v = (pix_fmt_id == 0x22211200);
        s->upscale_h = 2;
        s->chroma_height = s->height;
        break;
    case 0x22221100:
        if (s->bits <= 8) s->avctx->pix_fmt = s->cs_itu601 ? AV_PIX_FMT_YUV444P : AV_PIX_FMT_YUVJ444P;
        else
            goto unk_pixfmt;
        s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        s->upscale_v = 2;
        s->upscale_h = 2;
        s->chroma_height = s->height / 2;
        break;
    case 0x11000000:
    case 0x13000000:
    case 0x14000000:
    case 0x31000000:
    case 0x33000000:
    case 0x34000000:
    case 0x41000000:
    case 0x43000000:
    case 0x44000000:
        if(s->bits <= 8)
            s->avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        else
            s->avctx->pix_fmt = AV_PIX_FMT_GRAY16;
        break;
    case 0x12111100:
    case 0x22211100:
    case 0x22112100:
        if (s->bits <= 8) s->avctx->pix_fmt = s->cs_itu601 ? AV_PIX_FMT_YUV440P : AV_PIX_FMT_YUVJ440P;
        else
            goto unk_pixfmt;
        s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        s->upscale_h = (pix_fmt_id == 0x22211100) * 2 + (pix_fmt_id == 0x22112100);
        s->chroma_height = s->height / 2;
        break;
    case 0x21111100:
        if (s->bits <= 8) s->avctx->pix_fmt = s->cs_itu601 ? AV_PIX_FMT_YUV422P : AV_PIX_FMT_YUVJ422P;
        else              s->avctx->pix_fmt = AV_PIX_FMT_YUV422P16;
        s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        break;
    case 0x22121100:
    case 0x22111200:
        if (s->bits <= 8) s->avctx->pix_fmt = s->cs_itu601 ? AV_PIX_FMT_YUV422P : AV_PIX_FMT_YUVJ422P;
        else
            goto unk_pixfmt;
        s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        s->upscale_v = (pix_fmt_id == 0x22121100) + 1;
        break;
    case 0x22111100:
        if (s->bits <= 8) s->avctx->pix_fmt = s->cs_itu601 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUVJ420P;
        else              s->avctx->pix_fmt = AV_PIX_FMT_YUV420P16;
        s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        break;
    case 0x41111100:
        if (s->bits <= 8) s->avctx->pix_fmt = s->cs_itu601 ? AV_PIX_FMT_YUV411P : AV_PIX_FMT_YUVJ411P;
        else
            goto unk_pixfmt;
        s->avctx->color_range = s->cs_itu601 ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        break;
    default:
unk_pixfmt:
        av_log(s->avctx, AV_LOG_ERROR, "Unhandled pixel format 0x%x\n", pix_fmt_id);
        return AVERROR_PATCHWELCOME;
    }
    if ((s->upscale_h || s->upscale_v) && s->avctx->lowres) {
        av_log(s->avctx, AV_LOG_ERROR, "lowres not supported for weird subsampling\n");
        return AVERROR_PATCHWELCOME;
    }
    if (s->ls) {
        s->upscale_h = s->upscale_v = 0;
        if (s->nb_components > 1)
            s->avctx->pix_fmt = AV_PIX_FMT_RGB24;
        else if (s->bits <= 8)
            s->avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        else
            s->avctx->pix_fmt = AV_PIX_FMT_GRAY16;
    }

    av_frame_unref(s->picture_ptr);
    if (ff_get_buffer(s->avctx, s->picture_ptr, AV_GET_BUFFER_FLAG_REF) < 0)
        return -1;
    s->picture_ptr->pict_type = AV_PICTURE_TYPE_I;
    s->picture_ptr->key_frame = 1;
    s->got_picture            = 1;

    for (i = 0; i < 3; i++)
        s->linesize[i] = s->picture_ptr->linesize[i] << s->interlaced;

    av_dlog(s->avctx, "%d %d %d %d %d %d\n",
            s->width, s->height, s->linesize[0], s->linesize[1],
            s->interlaced, s->avctx->height);

    if (len != (8 + (3 * nb_components)))
        av_log(s->avctx, AV_LOG_DEBUG, "decode_sof0: error, len(%d) mismatch\n", len);
    }

    if (s->rgb && !s->lossless && !s->ls) {
        av_log(s->avctx, AV_LOG_ERROR, "Unsupported coding and pixel format combination\n");
        return AVERROR_PATCHWELCOME;
    }

    /* totally blank picture as progressive JPEG will only add details to it */
    if (s->progressive) {
        int bw = (width  + s->h_max * 8 - 1) / (s->h_max * 8);
        int bh = (height + s->v_max * 8 - 1) / (s->v_max * 8);
        for (i = 0; i < s->nb_components; i++) {
            int size = bw * bh * s->h_count[i] * s->v_count[i];
            av_freep(&s->blocks[i]);
            av_freep(&s->last_nnz[i]);
            s->blocks[i]       = av_malloc(size * sizeof(**s->blocks));
            s->last_nnz[i]     = av_mallocz(size * sizeof(**s->last_nnz));
            if (!s->blocks[i] || !s->last_nnz[i])
                return AVERROR(ENOMEM);
            s->block_stride[i] = bw * s->h_count[i];
        }
        memset(s->coefs_finished, 0, sizeof(s->coefs_finished));
    }
    return 0;
}

static inline int mjpeg_decode_dc(MJpegDecodeContext *s, int dc_index)
{
    int code;
    code = get_vlc2(&s->gb, s->vlcs[0][dc_index].table, 9, 2);
    if (code < 0 || code > 16) {
        av_log(s->avctx, AV_LOG_WARNING,
               "mjpeg_decode_dc: bad vlc: %d:%d (%p)\n",
               0, dc_index, &s->vlcs[0][dc_index]);
        return 0xfffff;
    }

    if (code)
        return get_xbits(&s->gb, code);
    else
        return 0;
}

/* decode block and dequantize */
static int decode_block(MJpegDecodeContext *s, int16_t *block, int component,
                        int dc_index, int ac_index, int16_t *quant_matrix)
{
    int code, i, j, level, val;

    /* DC coef */
    val = mjpeg_decode_dc(s, dc_index);
    if (val == 0xfffff) {
        av_log(s->avctx, AV_LOG_ERROR, "error dc\n");
        return AVERROR_INVALIDDATA;
    }
    val = val * quant_matrix[0] + s->last_dc[component];
    s->last_dc[component] = val;
    block[0] = val;
    /* AC coefs */
    i = 0;
    {OPEN_READER(re, &s->gb);
    do {
        UPDATE_CACHE(re, &s->gb);
        GET_VLC(code, re, &s->gb, s->vlcs[1][ac_index].table, 9, 2);

        i += ((unsigned)code) >> 4;
            code &= 0xf;
        if (code) {
            if (code > MIN_CACHE_BITS - 16)
                UPDATE_CACHE(re, &s->gb);

            {
                int cache = GET_CACHE(re, &s->gb);
                int sign  = (~cache) >> 31;
                level     = (NEG_USR32(sign ^ cache,code) ^ sign) - sign;
            }

            LAST_SKIP_BITS(re, &s->gb, code);

            if (i > 63) {
                av_log(s->avctx, AV_LOG_ERROR, "error count: %d\n", i);
                return AVERROR_INVALIDDATA;
            }
            j        = s->scantable.permutated[i];
            block[j] = level * quant_matrix[j];
        }
    } while (i < 63);
    CLOSE_READER(re, &s->gb);}

    return 0;
}

static int decode_dc_progressive(MJpegDecodeContext *s, int16_t *block,
                                 int component, int dc_index,
                                 int16_t *quant_matrix, int Al)
{
    int val;
    s->dsp.clear_block(block);
    val = mjpeg_decode_dc(s, dc_index);
    if (val == 0xfffff) {
        av_log(s->avctx, AV_LOG_ERROR, "error dc\n");
        return AVERROR_INVALIDDATA;
    }
    val = (val * quant_matrix[0] << Al) + s->last_dc[component];
    s->last_dc[component] = val;
    block[0] = val;
    return 0;
}

/* decode block and dequantize - progressive JPEG version */
static int decode_block_progressive(MJpegDecodeContext *s, int16_t *block,
                                    uint8_t *last_nnz, int ac_index,
                                    int16_t *quant_matrix,
                                    int ss, int se, int Al, int *EOBRUN)
{
    int code, i, j, level, val, run;

    if (*EOBRUN) {
        (*EOBRUN)--;
        return 0;
    }

    {
        OPEN_READER(re, &s->gb);
        for (i = ss; ; i++) {
            UPDATE_CACHE(re, &s->gb);
            GET_VLC(code, re, &s->gb, s->vlcs[2][ac_index].table, 9, 2);

            run = ((unsigned) code) >> 4;
            code &= 0xF;
            if (code) {
                i += run;
                if (code > MIN_CACHE_BITS - 16)
                    UPDATE_CACHE(re, &s->gb);

                {
                    int cache = GET_CACHE(re, &s->gb);
                    int sign  = (~cache) >> 31;
                    level     = (NEG_USR32(sign ^ cache,code) ^ sign) - sign;
                }

                LAST_SKIP_BITS(re, &s->gb, code);

                if (i >= se) {
                    if (i == se) {
                        j = s->scantable.permutated[se];
                        block[j] = level * quant_matrix[j] << Al;
                        break;
                    }
                    av_log(s->avctx, AV_LOG_ERROR, "error count: %d\n", i);
                    return AVERROR_INVALIDDATA;
                }
                j = s->scantable.permutated[i];
                block[j] = level * quant_matrix[j] << Al;
            } else {
                if (run == 0xF) {// ZRL - skip 15 coefficients
                    i += 15;
                    if (i >= se) {
                        av_log(s->avctx, AV_LOG_ERROR, "ZRL overflow: %d\n", i);
                        return AVERROR_INVALIDDATA;
                    }
                } else {
                    val = (1 << run);
                    if (run) {
                        UPDATE_CACHE(re, &s->gb);
                        val += NEG_USR32(GET_CACHE(re, &s->gb), run);
                        LAST_SKIP_BITS(re, &s->gb, run);
                    }
                    *EOBRUN = val - 1;
                    break;
                }
            }
        }
        CLOSE_READER(re, &s->gb);
    }

    if (i > *last_nnz)
        *last_nnz = i;

    return 0;
}

#define REFINE_BIT(j) {                                             \
    UPDATE_CACHE(re, &s->gb);                                       \
    sign = block[j] >> 15;                                          \
    block[j] += SHOW_UBITS(re, &s->gb, 1) *                         \
                ((quant_matrix[j] ^ sign) - sign) << Al;            \
    LAST_SKIP_BITS(re, &s->gb, 1);                                  \
}

#define ZERO_RUN                                                    \
for (; ; i++) {                                                     \
    if (i > last) {                                                 \
        i += run;                                                   \
        if (i > se) {                                               \
            av_log(s->avctx, AV_LOG_ERROR, "error count: %d\n", i); \
            return -1;                                              \
        }                                                           \
        break;                                                      \
    }                                                               \
    j = s->scantable.permutated[i];                                 \
    if (block[j])                                                   \
        REFINE_BIT(j)                                               \
    else if (run-- == 0)                                            \
        break;                                                      \
}

/* decode block and dequantize - progressive JPEG refinement pass */
static int decode_block_refinement(MJpegDecodeContext *s, int16_t *block,
                                   uint8_t *last_nnz,
                                   int ac_index, int16_t *quant_matrix,
                                   int ss, int se, int Al, int *EOBRUN)
{
    int code, i = ss, j, sign, val, run;
    int last    = FFMIN(se, *last_nnz);

    OPEN_READER(re, &s->gb);
    if (*EOBRUN) {
        (*EOBRUN)--;
    } else {
        for (; ; i++) {
            UPDATE_CACHE(re, &s->gb);
            GET_VLC(code, re, &s->gb, s->vlcs[2][ac_index].table, 9, 2);

            if (code & 0xF) {
                run = ((unsigned) code) >> 4;
                UPDATE_CACHE(re, &s->gb);
                val = SHOW_UBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
                ZERO_RUN;
                j = s->scantable.permutated[i];
                val--;
                block[j] = ((quant_matrix[j]^val) - val) << Al;
                if (i == se) {
                    if (i > *last_nnz)
                        *last_nnz = i;
                    CLOSE_READER(re, &s->gb);
                    return 0;
                }
            } else {
                run = ((unsigned) code) >> 4;
                if (run == 0xF) {
                    ZERO_RUN;
                } else {
                    val = run;
                    run = (1 << run);
                    if (val) {
                        UPDATE_CACHE(re, &s->gb);
                        run += SHOW_UBITS(re, &s->gb, val);
                        LAST_SKIP_BITS(re, &s->gb, val);
                    }
                    *EOBRUN = run - 1;
                    break;
                }
            }
        }

        if (i > *last_nnz)
            *last_nnz = i;
    }

    for (; i <= last; i++) {
        j = s->scantable.permutated[i];
        if (block[j])
            REFINE_BIT(j)
    }
    CLOSE_READER(re, &s->gb);

    return 0;
}
#undef REFINE_BIT
#undef ZERO_RUN

static int handle_rstn(MJpegDecodeContext *s, int nb_components)
{
    int i;
    int reset = 0;

    if (s->restart_interval) {
        s->restart_count--;
        if(s->restart_count == 0 && s->avctx->codec_id == AV_CODEC_ID_THP){
            align_get_bits(&s->gb);
            for (i = 0; i < nb_components; i++) /* reset dc */
                s->last_dc[i] = (4 << s->bits);
        }

        i = 8 + ((-get_bits_count(&s->gb)) & 7);
        /* skip RSTn */
        if (s->restart_count == 0) {
            if(   show_bits(&s->gb, i) == (1 << i) - 1
               || show_bits(&s->gb, i) == 0xFF) {
                int pos = get_bits_count(&s->gb);
                align_get_bits(&s->gb);
                while (get_bits_left(&s->gb) >= 8 && show_bits(&s->gb, 8) == 0xFF)
                    skip_bits(&s->gb, 8);
                if (get_bits_left(&s->gb) >= 8 && (get_bits(&s->gb, 8) & 0xF8) == 0xD0) {
                    for (i = 0; i < nb_components; i++) /* reset dc */
                        s->last_dc[i] = (4 << s->bits);
                    reset = 1;
                } else
                    skip_bits_long(&s->gb, pos - get_bits_count(&s->gb));
            }
        }
    }
    return reset;
}

static int ljpeg_decode_rgb_scan(MJpegDecodeContext *s, int nb_components, int predictor, int point_transform)
{
    int i, mb_x, mb_y;
    uint16_t (*buffer)[4];
    int left[4], top[4], topleft[4];
    const int linesize = s->linesize[0];
    const int mask     = ((1 << s->bits) - 1) << point_transform;
    int resync_mb_y = 0;
    int resync_mb_x = 0;

    if (s->nb_components != 3 && s->nb_components != 4)
        return AVERROR_INVALIDDATA;
    if (s->v_max != 1 || s->h_max != 1 || !s->lossless)
        return AVERROR_INVALIDDATA;


    s->restart_count = s->restart_interval;

    av_fast_malloc(&s->ljpeg_buffer, &s->ljpeg_buffer_size,
                   (unsigned)s->mb_width * 4 * sizeof(s->ljpeg_buffer[0][0]));
    buffer = s->ljpeg_buffer;

    for (i = 0; i < 4; i++)
        buffer[0][i] = 1 << (s->bits - 1);

    for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
        uint8_t *ptr = s->picture.data[0] + (linesize * mb_y);

        if (s->interlaced && s->bottom_field)
            ptr += linesize >> 1;

        for (i = 0; i < 4; i++)
            top[i] = left[i] = topleft[i] = buffer[0][i];

        for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
            int modified_predictor = predictor;

            if (s->restart_interval && !s->restart_count){
                s->restart_count = s->restart_interval;
                resync_mb_x = mb_x;
                resync_mb_y = mb_y;
                for(i=0; i<4; i++)
                    top[i] = left[i]= topleft[i]= 1 << (s->bits - 1);
            }
            if (mb_y == resync_mb_y || mb_y == resync_mb_y+1 && mb_x < resync_mb_x || !mb_x)
                modified_predictor = 1;

            for (i=0;i<nb_components;i++) {
                int pred, dc;

                topleft[i] = top[i];
                top[i]     = buffer[mb_x][i];

                PREDICT(pred, topleft[i], top[i], left[i], modified_predictor);

                dc = mjpeg_decode_dc(s, s->dc_index[i]);
                if(dc == 0xFFFFF)
                    return -1;

                left[i] = buffer[mb_x][i] =
                    mask & (pred + (dc << point_transform));
            }

            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
            }
        }
        if (s->nb_components == 4) {
            for(i=0; i<nb_components; i++) {
                int c= s->comp_index[i];
                if (s->bits <= 8) {
                    for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                        ptr[4*mb_x+3-c] = buffer[mb_x][i];
                    }
                } else if(s->bits == 9) {
                    return AVERROR_PATCHWELCOME;
                } else {
                    for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                        ((uint16_t*)ptr)[4*mb_x+c] = buffer[mb_x][i];
                    }
                }
            }
        } else if (s->rct) {
            for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
                ptr[3*mb_x + 1] = buffer[mb_x][0] - ((buffer[mb_x][1] + buffer[mb_x][2] - 0x200) >> 2);
                ptr[3*mb_x + 0] = buffer[mb_x][1] + ptr[3*mb_x + 1];
                ptr[3*mb_x + 2] = buffer[mb_x][2] + ptr[3*mb_x + 1];
            }
        } else if (s->pegasus_rct) {
            for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
                ptr[3*mb_x + 1] = buffer[mb_x][0] - ((buffer[mb_x][1] + buffer[mb_x][2]) >> 2);
                ptr[3*mb_x + 0] = buffer[mb_x][1] + ptr[3*mb_x + 1];
                ptr[3*mb_x + 2] = buffer[mb_x][2] + ptr[3*mb_x + 1];
            }
        } else {
            for(i=0; i<nb_components; i++) {
                int c= s->comp_index[i];
                if (s->bits <= 8) {
                    for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                        ptr[3*mb_x+2-c] = buffer[mb_x][i];
                    }
                } else if(s->bits == 9) {
                    return AVERROR_PATCHWELCOME;
                } else {
                    for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                        ((uint16_t*)ptr)[3*mb_x+2-c] = buffer[mb_x][i];
                    }
                }
            }
        }
    }
    return 0;
}

static int ljpeg_decode_yuv_scan(MJpegDecodeContext *s, int predictor,
                                 int point_transform, int nb_components)
{
    int i, mb_x, mb_y, mask;
    int bits= (s->bits+7)&~7;
    int resync_mb_y = 0;
    int resync_mb_x = 0;

    point_transform += bits - s->bits;
    mask = ((1 << s->bits) - 1) << point_transform;

    av_assert0(nb_components>=1 && nb_components<=4);

    for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
        for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
            if (s->restart_interval && !s->restart_count){
                s->restart_count = s->restart_interval;
                resync_mb_x = mb_x;
                resync_mb_y = mb_y;
            }

            if(!mb_x || mb_y == resync_mb_y || mb_y == resync_mb_y+1 && mb_x < resync_mb_x || s->interlaced){
                int toprow  = mb_y == resync_mb_y || mb_y == resync_mb_y+1 && mb_x < resync_mb_x;
                int leftcol = !mb_x || mb_y == resync_mb_y && mb_x == resync_mb_x;
                for (i = 0; i < nb_components; i++) {
                    uint8_t *ptr;
                    uint16_t *ptr16;
                    int n, h, v, x, y, c, j, linesize;
                    n = s->nb_blocks[i];
                    c = s->comp_index[i];
                    h = s->h_scount[i];
                    v = s->v_scount[i];
                    x = 0;
                    y = 0;
                    linesize= s->linesize[c];

                    if(bits>8) linesize /= 2;

                    for(j=0; j<n; j++) {
                        int pred, dc;

                        dc = mjpeg_decode_dc(s, s->dc_index[i]);
                        if(dc == 0xFFFFF)
                            return -1;
                        if(bits<=8){
                        ptr = s->picture.data[c] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                        if(y==0 && toprow){
                            if(x==0 && leftcol){
                                pred= 1 << (bits - 1);
                            }else{
                                pred= ptr[-1];
                            }
                        }else{
                            if(x==0 && leftcol){
                                pred= ptr[-linesize];
                            }else{
                                PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);
                            }
                        }

                        if (s->interlaced && s->bottom_field)
                            ptr += linesize >> 1;
                        pred &= mask;
                        *ptr= pred + (dc << point_transform);
                        }else{
                            ptr16 = (uint16_t*)(s->picture.data[c] + 2*(linesize * (v * mb_y + y)) + 2*(h * mb_x + x)); //FIXME optimize this crap
                            if(y==0 && toprow){
                                if(x==0 && leftcol){
                                    pred= 1 << (bits - 1);
                                }else{
                                    pred= ptr16[-1];
                                }
                            }else{
                                if(x==0 && leftcol){
                                    pred= ptr16[-linesize];
                                }else{
                                    PREDICT(pred, ptr16[-linesize-1], ptr16[-linesize], ptr16[-1], predictor);
                                }
                            }

                            if (s->interlaced && s->bottom_field)
                                ptr16 += linesize >> 1;
                            pred &= mask;
                            *ptr16= pred + (dc << point_transform);
                        }
                        if (++x == h) {
                            x = 0;
                            y++;
                        }
                    }
                }
            } else {
                for (i = 0; i < nb_components; i++) {
                    uint8_t *ptr;
                    uint16_t *ptr16;
                    int n, h, v, x, y, c, j, linesize, dc;
                    n        = s->nb_blocks[i];
                    c        = s->comp_index[i];
                    h        = s->h_scount[i];
                    v        = s->v_scount[i];
                    x        = 0;
                    y        = 0;
                    linesize = s->linesize[c];

                    if(bits>8) linesize /= 2;

                    for (j = 0; j < n; j++) {
                        int pred;

                        dc = mjpeg_decode_dc(s, s->dc_index[i]);
                        if(dc == 0xFFFFF)
                            return -1;
                        if(bits<=8){
                            ptr = s->picture.data[c] +
                              (linesize * (v * mb_y + y)) +
                              (h * mb_x + x); //FIXME optimize this crap
                            PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);

                            pred &= mask;
                            *ptr = pred + (dc << point_transform);
                        }else{
                            ptr16 = (uint16_t*)(s->picture.data[c] + 2*(linesize * (v * mb_y + y)) + 2*(h * mb_x + x)); //FIXME optimize this crap
                            PREDICT(pred, ptr16[-linesize-1], ptr16[-linesize], ptr16[-1], predictor);

                            pred &= mask;
                            *ptr16= pred + (dc << point_transform);
                        }

                        if (++x == h) {
                            x = 0;
                            y++;
                        }
                    }
                }
            }
            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
            }
        }
    }
    return 0;
}

static av_always_inline void mjpeg_copy_block(MJpegDecodeContext *s,
                                              uint8_t *dst, const uint8_t *src,
                                              int linesize, int lowres)
{
    switch (lowres) {
    case 0: s->hdsp.put_pixels_tab[1][0](dst, src, linesize, 8);
        break;
    case 1: copy_block4(dst, src, linesize, linesize, 4);
        break;
    case 2: copy_block2(dst, src, linesize, linesize, 2);
        break;
    case 3: *dst = *src;
        break;
    }
}

static void shift_output(MJpegDecodeContext *s, uint8_t *ptr, int linesize)
{
    int block_x, block_y;
    int size = 8 >> s->avctx->lowres;
    if (s->bits > 8) {
        for (block_y=0; block_y<size; block_y++)
            for (block_x=0; block_x<size; block_x++)
                *(uint16_t*)(ptr + 2*block_x + block_y*linesize) <<= 16 - s->bits;
    } else {
        for (block_y=0; block_y<size; block_y++)
            for (block_x=0; block_x<size; block_x++)
                *(ptr + block_x + block_y*linesize) <<= 8 - s->bits;
    }
}

static int mjpeg_decode_scan(MJpegDecodeContext *s, int nb_components, int Ah,
                             int Al, const uint8_t *mb_bitmask,
                             const AVFrame *reference)
{
    int i, mb_x, mb_y;
    uint8_t *data[MAX_COMPONENTS];
    const uint8_t *reference_data[MAX_COMPONENTS];
    int linesize[MAX_COMPONENTS];
    GetBitContext mb_bitmask_gb;
    int bytes_per_pixel = 1 + (s->bits > 8);

    if (mb_bitmask)
        init_get_bits(&mb_bitmask_gb, mb_bitmask, s->mb_width * s->mb_height);

    if (s->flipped && s->avctx->lowres) {
        av_log(s->avctx, AV_LOG_ERROR, "Can not flip image with lowres\n");
        s->flipped = 0;
    }
    s->restart_count = 0;
    for (i = 0; i < nb_components; i++) {
        int c   = s->comp_index[i];
        data[c] = s->picture_ptr->data[c];
        reference_data[c] = reference ? reference->data[c] : NULL;
        linesize[c] = s->linesize[c];
        s->coefs_finished[c] |= 1;
        if (s->flipped && !(s->avctx->flags & CODEC_FLAG_EMU_EDGE)) {
            // picture should be flipped upside-down for this codec
            int offset = (linesize[c] * (s->v_scount[i] *
                         (8 * s->mb_height - ((s->height / s->v_max) & 7)) - 1));
            data[c]           += offset;
            reference_data[c] += offset;
            linesize[c]       *= -1;
        }
    }

    for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
        for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
            const int copy_mb = mb_bitmask && !get_bits1(&mb_bitmask_gb);

            if (s->restart_interval && !s->restart_count)
                s->restart_count = s->restart_interval;

            if (get_bits_left(&s->gb) < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "overread %d\n",
                       -get_bits_left(&s->gb));
                return AVERROR_INVALIDDATA;
            }
            for (i = 0; i < nb_components; i++) {
                uint8_t *ptr;
                int n, h, v, x, y, c, j;
                int block_offset;
                n = s->nb_blocks[i];
                c = s->comp_index[i];
                h = s->h_scount[i];
                v = s->v_scount[i];
                x = 0;
                y = 0;
                for (j = 0; j < n; j++) {
                    block_offset = (((linesize[c] * (v * mb_y + y) * 8) +
                                     (h * mb_x + x) * 8 * bytes_per_pixel) >> s->avctx->lowres);

                    if (s->interlaced && s->bottom_field)
                        block_offset += linesize[c] >> 1;
                    ptr = data[c] + block_offset;
                    if (!s->progressive) {
                        if (copy_mb)
                            mjpeg_copy_block(s, ptr, reference_data[c] + block_offset,
                                             linesize[c], s->avctx->lowres);

                        else {
                            s->dsp.clear_block(s->block);
                            if (decode_block(s, s->block, i,
                                             s->dc_index[i], s->ac_index[i],
                                             s->quant_matrixes[s->quant_sindex[i]]) < 0) {
                                av_log(s->avctx, AV_LOG_ERROR,
                                       "error y=%d x=%d\n", mb_y, mb_x);
                                return AVERROR_INVALIDDATA;
                            }
                            s->dsp.idct_put(ptr, linesize[c], s->block);
                            if (s->bits & 7)
                                shift_output(s, ptr, linesize[c]);
                        }
                    } else {
                        int block_idx  = s->block_stride[c] * (v * mb_y + y) +
                                         (h * mb_x + x);
                        int16_t *block = s->blocks[c][block_idx];
                        if (Ah)
                            block[0] += get_bits1(&s->gb) *
                                        s->quant_matrixes[s->quant_sindex[i]][0] << Al;
                        else if (decode_dc_progressive(s, block, i, s->dc_index[i],
                                                       s->quant_matrixes[s->quant_sindex[i]],
                                                       Al) < 0) {
                            av_log(s->avctx, AV_LOG_ERROR,
                                   "error y=%d x=%d\n", mb_y, mb_x);
                            return AVERROR_INVALIDDATA;
                        }
                    }
                    av_dlog(s->avctx, "mb: %d %d processed\n", mb_y, mb_x);
                    av_dlog(s->avctx, "%d %d %d %d %d %d %d %d \n",
                            mb_x, mb_y, x, y, c, s->bottom_field,
                            (v * mb_y + y) * 8, (h * mb_x + x) * 8);
                    if (++x == h) {
                        x = 0;
                        y++;
                    }
                }
            }

            handle_rstn(s, nb_components);
        }
    }
    return 0;
}

static int mjpeg_decode_scan_progressive_ac(MJpegDecodeContext *s, int ss,
                                            int se, int Ah, int Al)
{
    int mb_x, mb_y;
    int EOBRUN = 0;
    int c = s->comp_index[0];
    uint8_t *data = s->picture.data[c];
    int linesize  = s->linesize[c];
    int last_scan = 0;
    int16_t *quant_matrix = s->quant_matrixes[s->quant_sindex[0]];
    int bytes_per_pixel = 1 + (s->bits > 8);

    av_assert0(ss>=0 && Ah>=0 && Al>=0);
    if (se < ss || se > 63) {
        av_log(s->avctx, AV_LOG_ERROR, "SS/SE %d/%d is invalid\n", ss, se);
        return AVERROR_INVALIDDATA;
    }

    if (!Al) {
        s->coefs_finished[c] |= (1LL << (se + 1)) - (1LL << ss);
        last_scan = !~s->coefs_finished[c];
    }

    if (s->interlaced && s->bottom_field)
        data += linesize >> 1;

    s->restart_count = 0;

    for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
        uint8_t *ptr     = data + (mb_y * linesize * 8 >> s->avctx->lowres);
        int block_idx    = mb_y * s->block_stride[c];
        int16_t (*block)[64] = &s->blocks[c][block_idx];
        uint8_t *last_nnz    = &s->last_nnz[c][block_idx];
        for (mb_x = 0; mb_x < s->mb_width; mb_x++, block++, last_nnz++) {
                int ret;
                if (s->restart_interval && !s->restart_count)
                    s->restart_count = s->restart_interval;

                if (Ah)
                    ret = decode_block_refinement(s, *block, last_nnz, s->ac_index[0],
                                                  quant_matrix, ss, se, Al, &EOBRUN);
                else
                    ret = decode_block_progressive(s, *block, last_nnz, s->ac_index[0],
                                                   quant_matrix, ss, se, Al, &EOBRUN);
                if (ret < 0) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "error y=%d x=%d\n", mb_y, mb_x);
                    return AVERROR_INVALIDDATA;
                }

            if (last_scan) {
                    s->dsp.idct_put(ptr, linesize, *block);
                    if (s->bits & 7)
                        shift_output(s, ptr, linesize);
                    ptr += bytes_per_pixel*8 >> s->avctx->lowres;
            }
            if (handle_rstn(s, 0))
                EOBRUN = 0;
        }
    }
    return 0;
}

int ff_mjpeg_decode_sos(MJpegDecodeContext *s, const uint8_t *mb_bitmask,
                        const AVFrame *reference)
{
    int len, nb_components, i, h, v, predictor, point_transform;
    int index, id, ret;
    const int block_size = s->lossless ? 1 : 8;
    int ilv, prev_shift;

    if (!s->got_picture) {
        av_log(s->avctx, AV_LOG_WARNING,
                "Can not process SOS before SOF, skipping\n");
        return -1;
    }

    av_assert0(s->picture_ptr->data[0]);
    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    nb_components = get_bits(&s->gb, 8);
    if (nb_components == 0 || nb_components > MAX_COMPONENTS) {
        av_log(s->avctx, AV_LOG_ERROR,
               "decode_sos: nb_components (%d) unsupported\n", nb_components);
        return AVERROR_PATCHWELCOME;
    }
    if (len != 6 + 2 * nb_components) {
        av_log(s->avctx, AV_LOG_ERROR, "decode_sos: invalid len (%d)\n", len);
        return AVERROR_INVALIDDATA;
    }
    for (i = 0; i < nb_components; i++) {
        id = get_bits(&s->gb, 8) - 1;
        av_log(s->avctx, AV_LOG_DEBUG, "component: %d\n", id);
        /* find component index */
        for (index = 0; index < s->nb_components; index++)
            if (id == s->component_id[index])
                break;
        if (index == s->nb_components) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "decode_sos: index(%d) out of components\n", index);
            return AVERROR_INVALIDDATA;
        }
        /* Metasoft MJPEG codec has Cb and Cr swapped */
        if (s->avctx->codec_tag == MKTAG('M', 'T', 'S', 'J')
            && nb_components == 3 && s->nb_components == 3 && i)
            index = 3 - i;

        s->quant_sindex[i] = s->quant_index[index];
        s->nb_blocks[i] = s->h_count[index] * s->v_count[index];
        s->h_scount[i]  = s->h_count[index];
        s->v_scount[i]  = s->v_count[index];

        if(nb_components == 3 && s->nb_components == 3 && s->avctx->pix_fmt == AV_PIX_FMT_GBR24P)
            index = (i+2)%3;
        if(nb_components == 1 && s->nb_components == 3 && s->avctx->pix_fmt == AV_PIX_FMT_GBR24P)
            index = (index+2)%3;

        s->comp_index[i] = index;

        s->dc_index[i] = get_bits(&s->gb, 4);
        s->ac_index[i] = get_bits(&s->gb, 4);

        if (s->dc_index[i] <  0 || s->ac_index[i] < 0 ||
            s->dc_index[i] >= 4 || s->ac_index[i] >= 4)
            goto out_of_range;
        if (!s->vlcs[0][s->dc_index[i]].table || !(s->progressive ? s->vlcs[2][s->ac_index[0]].table : s->vlcs[1][s->ac_index[i]].table))
            goto out_of_range;
    }

    predictor = get_bits(&s->gb, 8);       /* JPEG Ss / lossless JPEG predictor /JPEG-LS NEAR */
    ilv = get_bits(&s->gb, 8);             /* JPEG Se / JPEG-LS ILV */
    if(s->avctx->codec_tag != AV_RL32("CJPG")){
        prev_shift      = get_bits(&s->gb, 4); /* Ah */
        point_transform = get_bits(&s->gb, 4); /* Al */
    }else
        prev_shift = point_transform = 0;

    if (nb_components > 1) {
        /* interleaved stream */
        s->mb_width  = (s->width  + s->h_max * block_size - 1) / (s->h_max * block_size);
        s->mb_height = (s->height + s->v_max * block_size - 1) / (s->v_max * block_size);
    } else if (!s->ls) { /* skip this for JPEG-LS */
        h = s->h_max / s->h_scount[0];
        v = s->v_max / s->v_scount[0];
        s->mb_width     = (s->width  + h * block_size - 1) / (h * block_size);
        s->mb_height    = (s->height + v * block_size - 1) / (v * block_size);
        s->nb_blocks[0] = 1;
        s->h_scount[0]  = 1;
        s->v_scount[0]  = 1;
    }

    if (s->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(s->avctx, AV_LOG_DEBUG, "%s %s p:%d >>:%d ilv:%d bits:%d skip:%d %s comp:%d\n",
               s->lossless ? "lossless" : "sequential DCT", s->rgb ? "RGB" : "",
               predictor, point_transform, ilv, s->bits, s->mjpb_skiptosod,
               s->pegasus_rct ? "PRCT" : (s->rct ? "RCT" : ""), nb_components);


    /* mjpeg-b can have padding bytes between sos and image data, skip them */
    for (i = s->mjpb_skiptosod; i > 0; i--)
        skip_bits(&s->gb, 8);

next_field:
    for (i = 0; i < nb_components; i++)
        s->last_dc[i] = (4 << s->bits);

    if (s->lossless) {
        av_assert0(s->picture_ptr == &s->picture);
        if (CONFIG_JPEGLS_DECODER && s->ls) {
//            for () {
//            reset_ls_coding_parameters(s, 0);

            if ((ret = ff_jpegls_decode_picture(s, predictor,
                                                point_transform, ilv)) < 0)
                return ret;
        } else {
            if (s->rgb) {
                if ((ret = ljpeg_decode_rgb_scan(s, nb_components, predictor, point_transform)) < 0)
                    return ret;
            } else {
                if ((ret = ljpeg_decode_yuv_scan(s, predictor,
                                                 point_transform,
                                                 nb_components)) < 0)
                    return ret;
            }
        }
    } else {
        if (s->progressive && predictor) {
            av_assert0(s->picture_ptr == &s->picture);
            if ((ret = mjpeg_decode_scan_progressive_ac(s, predictor,
                                                        ilv, prev_shift,
                                                        point_transform)) < 0)
                return ret;
        } else {
            if ((ret = mjpeg_decode_scan(s, nb_components,
                                         prev_shift, point_transform,
                                         mb_bitmask, reference)) < 0)
                return ret;
        }
    }

    if (s->interlaced &&
        get_bits_left(&s->gb) > 32 &&
        show_bits(&s->gb, 8) == 0xFF) {
        GetBitContext bak = s->gb;
        align_get_bits(&bak);
        if (show_bits(&bak, 16) == 0xFFD1) {
            av_log(s->avctx, AV_LOG_DEBUG, "AVRn interlaced picture marker found\n");
            s->gb = bak;
            skip_bits(&s->gb, 16);
            s->bottom_field ^= 1;

            goto next_field;
        }
    }

    emms_c();
    return 0;
 out_of_range:
    av_log(s->avctx, AV_LOG_ERROR, "decode_sos: ac/dc index out of range\n");
    return AVERROR_INVALIDDATA;
}

static int mjpeg_decode_dri(MJpegDecodeContext *s)
{
    if (get_bits(&s->gb, 16) != 4)
        return AVERROR_INVALIDDATA;
    s->restart_interval = get_bits(&s->gb, 16);
    s->restart_count    = 0;
    av_log(s->avctx, AV_LOG_DEBUG, "restart interval: %d\n",
           s->restart_interval);

    return 0;
}

static int mjpeg_decode_app(MJpegDecodeContext *s)
{
    int len, id, i;

    len = get_bits(&s->gb, 16);
    if (len < 5)
        return AVERROR_INVALIDDATA;
    if (8 * len > get_bits_left(&s->gb))
        return AVERROR_INVALIDDATA;

    id   = get_bits_long(&s->gb, 32);
    len -= 6;

    if (s->avctx->debug & FF_DEBUG_STARTCODE)
        av_log(s->avctx, AV_LOG_DEBUG, "APPx %8X len=%d\n", id, len);

    /* Buggy AVID, it puts EOI only at every 10th frame. */
    /* Also, this fourcc is used by non-avid files too, it holds some
       information, but it's always present in AVID-created files. */
    if (id == AV_RB32("AVI1")) {
        /* structure:
            4bytes      AVI1
            1bytes      polarity
            1bytes      always zero
            4bytes      field_size
            4bytes      field_size_less_padding
        */
            s->buggy_avid = 1;
        i = get_bits(&s->gb, 8); len--;
        av_log(s->avctx, AV_LOG_DEBUG, "polarity %d\n", i);
#if 0
        skip_bits(&s->gb, 8);
        skip_bits(&s->gb, 32);
        skip_bits(&s->gb, 32);
        len -= 10;
#endif
        goto out;
    }

//    len -= 2;

    if (id == AV_RB32("JFIF")) {
        int t_w, t_h, v1, v2;
        skip_bits(&s->gb, 8); /* the trailing zero-byte */
        v1 = get_bits(&s->gb, 8);
        v2 = get_bits(&s->gb, 8);
        skip_bits(&s->gb, 8);

        s->avctx->sample_aspect_ratio.num = get_bits(&s->gb, 16);
        s->avctx->sample_aspect_ratio.den = get_bits(&s->gb, 16);

        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO,
                   "mjpeg: JFIF header found (version: %x.%x) SAR=%d/%d\n",
                   v1, v2,
                   s->avctx->sample_aspect_ratio.num,
                   s->avctx->sample_aspect_ratio.den);

        t_w = get_bits(&s->gb, 8);
        t_h = get_bits(&s->gb, 8);
        if (t_w && t_h) {
            /* skip thumbnail */
            if (len -10 - (t_w * t_h * 3) > 0)
                len -= t_w * t_h * 3;
        }
        len -= 10;
        goto out;
    }

    if (id == AV_RB32("Adob") && (get_bits(&s->gb, 8) == 'e')) {
        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO, "mjpeg: Adobe header found\n");
        skip_bits(&s->gb, 16); /* version */
        skip_bits(&s->gb, 16); /* flags0 */
        skip_bits(&s->gb, 16); /* flags1 */
        skip_bits(&s->gb,  8); /* transform */
        len -= 7;
        goto out;
    }

    if (id == AV_RB32("LJIF")) {
        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO,
                   "Pegasus lossless jpeg header found\n");
        skip_bits(&s->gb, 16); /* version ? */
        skip_bits(&s->gb, 16); /* unknown always 0? */
        skip_bits(&s->gb, 16); /* unknown always 0? */
        skip_bits(&s->gb, 16); /* unknown always 0? */
        switch (i=get_bits(&s->gb, 8)) {
        case 1:
            s->rgb         = 1;
            s->pegasus_rct = 0;
            break;
        case 2:
            s->rgb         = 1;
            s->pegasus_rct = 1;
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "unknown colorspace %d\n", i);
        }
        len -= 9;
        goto out;
    }
    if (id == AV_RL32("colr") && len > 0) {
        s->colr = get_bits(&s->gb, 8);
        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO, "COLR %d\n", s->colr);
        len --;
        goto out;
    }
    if (id == AV_RL32("xfrm") && len > 0) {
        s->xfrm = get_bits(&s->gb, 8);
        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO, "XFRM %d\n", s->xfrm);
        len --;
        goto out;
    }

    /* EXIF metadata */
    if (s->start_code == APP1 && id == AV_RB32("Exif")) {
        GetByteContext gbytes;
        int ret, le, ifd_offset, bytes_read;
        const uint8_t *aligned;

        skip_bits(&s->gb, 16); // skip padding
        len -= 2;

        // init byte wise reading
        aligned = align_get_bits(&s->gb);
        bytestream2_init(&gbytes, aligned, len);

        // read TIFF header
        ret = ff_tdecode_header(&gbytes, &le, &ifd_offset);
        if (ret) {
            av_log(s->avctx, AV_LOG_ERROR, "mjpeg: invalid TIFF header in EXIF data\n");
            return ret;
        }

        bytestream2_seek(&gbytes, ifd_offset, SEEK_SET);

        // read 0th IFD and store the metadata
        // (return values > 0 indicate the presence of subimage metadata)
        ret = ff_exif_decode_ifd(s->avctx, &gbytes, le, 0, &s->exif_metadata);
        if (ret < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "mjpeg: error decoding EXIF data\n");
            return ret;
        }

        bytes_read = bytestream2_tell(&gbytes);
        skip_bits(&s->gb, bytes_read << 3);
        len -= bytes_read;

        goto out;
    }

    /* Apple MJPEG-A */
    if ((s->start_code == APP1) && (len > (0x28 - 8))) {
        id   = get_bits_long(&s->gb, 32);
        len -= 4;
        /* Apple MJPEG-A */
        if (id == AV_RB32("mjpg")) {
#if 0
            skip_bits(&s->gb, 32); /* field size */
            skip_bits(&s->gb, 32); /* pad field size */
            skip_bits(&s->gb, 32); /* next off */
            skip_bits(&s->gb, 32); /* quant off */
            skip_bits(&s->gb, 32); /* huff off */
            skip_bits(&s->gb, 32); /* image off */
            skip_bits(&s->gb, 32); /* scan off */
            skip_bits(&s->gb, 32); /* data off */
#endif
            if (s->avctx->debug & FF_DEBUG_PICT_INFO)
                av_log(s->avctx, AV_LOG_INFO, "mjpeg: Apple MJPEG-A header found\n");
        }
    }

out:
    /* slow but needed for extreme adobe jpegs */
    if (len < 0)
        av_log(s->avctx, AV_LOG_ERROR,
               "mjpeg: error, decode_app parser read over the end\n");
    while (--len > 0)
        skip_bits(&s->gb, 8);

    return 0;
}

static int mjpeg_decode_com(MJpegDecodeContext *s)
{
    int len = get_bits(&s->gb, 16);
    if (len >= 2 && 8 * len - 16 <= get_bits_left(&s->gb)) {
        char *cbuf = av_malloc(len - 1);
        if (cbuf) {
            int i;
            for (i = 0; i < len - 2; i++)
                cbuf[i] = get_bits(&s->gb, 8);
            if (i > 0 && cbuf[i - 1] == '\n')
                cbuf[i - 1] = 0;
            else
                cbuf[i] = 0;

            if (s->avctx->debug & FF_DEBUG_PICT_INFO)
                av_log(s->avctx, AV_LOG_INFO, "comment: '%s'\n", cbuf);

            /* buggy avid, it puts EOI only at every 10th frame */
            if (!strncmp(cbuf, "AVID", 4)) {
                s->buggy_avid = 1;
                if (len > 14 && cbuf[12] == 1) /* 1 - NTSC, 2 - PAL */
                    s->interlace_polarity = 1;
            } else if (!strcmp(cbuf, "CS=ITU601"))
                s->cs_itu601 = 1;
            else if ((!strncmp(cbuf, "Intel(R) JPEG Library, version 1", 32)) ||
                     (!strncmp(cbuf, "Metasoft MJPEG Codec", 20)))
                s->flipped = 1;

            av_free(cbuf);
        }
    }

    return 0;
}

/* return the 8 bit start code value and update the search
   state. Return -1 if no start code found */
static int find_marker(const uint8_t **pbuf_ptr, const uint8_t *buf_end)
{
    const uint8_t *buf_ptr;
    unsigned int v, v2;
    int val;
    int skipped = 0;

    buf_ptr = *pbuf_ptr;
    while (buf_end - buf_ptr > 1) {
        v  = *buf_ptr++;
        v2 = *buf_ptr;
        if ((v == 0xff) && (v2 >= 0xc0) && (v2 <= 0xfe) && buf_ptr < buf_end) {
            val = *buf_ptr++;
            goto found;
        }
        skipped++;
    }
    buf_ptr = buf_end;
    val = -1;
found:
    av_dlog(NULL, "find_marker skipped %d bytes\n", skipped);
    *pbuf_ptr = buf_ptr;
    return val;
}

int ff_mjpeg_find_marker(MJpegDecodeContext *s,
                         const uint8_t **buf_ptr, const uint8_t *buf_end,
                         const uint8_t **unescaped_buf_ptr,
                         int *unescaped_buf_size)
{
    int start_code;
    start_code = find_marker(buf_ptr, buf_end);

    av_fast_padded_malloc(&s->buffer, &s->buffer_size, buf_end - *buf_ptr);
    if (!s->buffer)
        return AVERROR(ENOMEM);

    /* unescape buffer of SOS, use special treatment for JPEG-LS */
    if (start_code == SOS && !s->ls) {
        const uint8_t *src = *buf_ptr;
        uint8_t *dst = s->buffer;

        while (src < buf_end) {
            uint8_t x = *(src++);

            *(dst++) = x;
            if (s->avctx->codec_id != AV_CODEC_ID_THP) {
                if (x == 0xff) {
                    while (src < buf_end && x == 0xff)
                        x = *(src++);

                    if (x >= 0xd0 && x <= 0xd7)
                        *(dst++) = x;
                    else if (x)
                        break;
                }
            }
        }
        *unescaped_buf_ptr  = s->buffer;
        *unescaped_buf_size = dst - s->buffer;
        memset(s->buffer + *unescaped_buf_size, 0,
               FF_INPUT_BUFFER_PADDING_SIZE);

        av_log(s->avctx, AV_LOG_DEBUG, "escaping removed %td bytes\n",
               (buf_end - *buf_ptr) - (dst - s->buffer));
    } else if (start_code == SOS && s->ls) {
        const uint8_t *src = *buf_ptr;
        uint8_t *dst  = s->buffer;
        int bit_count = 0;
        int t = 0, b = 0;
        PutBitContext pb;

        s->cur_scan++;

        /* find marker */
        while (src + t < buf_end) {
            uint8_t x = src[t++];
            if (x == 0xff) {
                while ((src + t < buf_end) && x == 0xff)
                    x = src[t++];
                if (x & 0x80) {
                    t -= FFMIN(2, t);
                    break;
                }
            }
        }
        bit_count = t * 8;
        init_put_bits(&pb, dst, t);

        /* unescape bitstream */
        while (b < t) {
            uint8_t x = src[b++];
            put_bits(&pb, 8, x);
            if (x == 0xFF) {
                x = src[b++];
                put_bits(&pb, 7, x);
                bit_count--;
            }
        }
        flush_put_bits(&pb);

        *unescaped_buf_ptr  = dst;
        *unescaped_buf_size = (bit_count + 7) >> 3;
        memset(s->buffer + *unescaped_buf_size, 0,
               FF_INPUT_BUFFER_PADDING_SIZE);
    } else {
        *unescaped_buf_ptr  = *buf_ptr;
        *unescaped_buf_size = buf_end - *buf_ptr;
    }

    return start_code;
}

int ff_mjpeg_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                          AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    MJpegDecodeContext *s = avctx->priv_data;
    const uint8_t *buf_end, *buf_ptr;
    const uint8_t *unescaped_buf_ptr;
    int hshift, vshift;
    int unescaped_buf_size;
    int start_code;
    int i, index;
    int ret = 0;

    av_dict_free(&s->exif_metadata);

    buf_ptr = buf;
    buf_end = buf + buf_size;
    while (buf_ptr < buf_end) {
        /* find start next marker */
        start_code = ff_mjpeg_find_marker(s, &buf_ptr, buf_end,
                                          &unescaped_buf_ptr,
                                          &unescaped_buf_size);
        /* EOF */
        if (start_code < 0) {
            goto the_end;
        } else if (unescaped_buf_size > INT_MAX / 8) {
            av_log(avctx, AV_LOG_ERROR,
                   "MJPEG packet 0x%x too big (%d/%d), corrupt data?\n",
                   start_code, unescaped_buf_size, buf_size);
            return AVERROR_INVALIDDATA;
        }
        av_log(avctx, AV_LOG_DEBUG, "marker=%x avail_size_in_buf=%td\n",
               start_code, buf_end - buf_ptr);

        ret = init_get_bits8(&s->gb, unescaped_buf_ptr, unescaped_buf_size);

        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "invalid buffer\n");
            goto fail;
        }

        s->start_code = start_code;
        if (s->avctx->debug & FF_DEBUG_STARTCODE)
            av_log(avctx, AV_LOG_DEBUG, "startcode: %X\n", start_code);

        /* process markers */
        if (start_code >= 0xd0 && start_code <= 0xd7)
            av_log(avctx, AV_LOG_DEBUG,
                   "restart marker: %d\n", start_code & 0x0f);
            /* APP fields */
        else if (start_code >= APP0 && start_code <= APP15)
            mjpeg_decode_app(s);
            /* Comment */
        else if (start_code == COM)
            mjpeg_decode_com(s);

        ret = -1;

        if (!CONFIG_JPEGLS_DECODER &&
            (start_code == SOF48 || start_code == LSE)) {
            av_log(avctx, AV_LOG_ERROR, "JPEG-LS support not enabled.\n");
            return AVERROR(ENOSYS);
        }

        switch (start_code) {
        case SOI:
            s->restart_interval = 0;
            s->restart_count    = 0;
            /* nothing to do on SOI */
            break;
        case DQT:
            ff_mjpeg_decode_dqt(s);
            break;
        case DHT:
            if ((ret = ff_mjpeg_decode_dht(s)) < 0) {
                av_log(avctx, AV_LOG_ERROR, "huffman table decode error\n");
                goto fail;
            }
            break;
        case SOF0:
        case SOF1:
            s->lossless    = 0;
            s->ls          = 0;
            s->progressive = 0;
            if ((ret = ff_mjpeg_decode_sof(s)) < 0)
                goto fail;
            break;
        case SOF2:
            s->lossless    = 0;
            s->ls          = 0;
            s->progressive = 1;
            if ((ret = ff_mjpeg_decode_sof(s)) < 0)
                goto fail;
            break;
        case SOF3:
            s->lossless    = 1;
            s->ls          = 0;
            s->progressive = 0;
            if ((ret = ff_mjpeg_decode_sof(s)) < 0)
                goto fail;
            break;
        case SOF48:
            s->lossless    = 1;
            s->ls          = 1;
            s->progressive = 0;
            if ((ret = ff_mjpeg_decode_sof(s)) < 0)
                goto fail;
            break;
        case LSE:
            if (!CONFIG_JPEGLS_DECODER ||
                (ret = ff_jpegls_decode_lse(s)) < 0)
                goto fail;
            break;
        case EOI:
eoi_parser:
            s->cur_scan = 0;
            if (!s->got_picture) {
                av_log(avctx, AV_LOG_WARNING,
                       "Found EOI before any SOF, ignoring\n");
                break;
            }
            if (s->interlaced) {
                s->bottom_field ^= 1;
                /* if not bottom field, do not output image yet */
                if (s->bottom_field == !s->interlace_polarity)
                    break;
            }
            if ((ret = av_frame_ref(data, s->picture_ptr)) < 0)
                return ret;
            *got_frame = 1;
            s->got_picture = 0;

            if (!s->lossless) {
                int qp = FFMAX3(s->qscale[0],
                                s->qscale[1],
                                s->qscale[2]);
                int qpw = (s->width + 15) / 16;
                AVBufferRef *qp_table_buf = av_buffer_alloc(qpw);
                if (qp_table_buf) {
                    memset(qp_table_buf->data, qp, qpw);
                    av_frame_set_qp_table(data, qp_table_buf, 0, FF_QSCALE_TYPE_MPEG1);
                }

                if(avctx->debug & FF_DEBUG_QP)
                    av_log(avctx, AV_LOG_DEBUG, "QP: %d\n", qp);
            }

            goto the_end;
        case SOS:
            if ((ret = ff_mjpeg_decode_sos(s, NULL, NULL)) < 0 &&
                (avctx->err_recognition & AV_EF_EXPLODE))
                goto fail;
            break;
        case DRI:
            mjpeg_decode_dri(s);
            break;
        case SOF5:
        case SOF6:
        case SOF7:
        case SOF9:
        case SOF10:
        case SOF11:
        case SOF13:
        case SOF14:
        case SOF15:
        case JPG:
            av_log(avctx, AV_LOG_ERROR,
                   "mjpeg: unsupported coding type (%x)\n", start_code);
            break;
        }

        /* eof process start code */
        buf_ptr += (get_bits_count(&s->gb) + 7) / 8;
        av_log(avctx, AV_LOG_DEBUG,
               "marker parser used %d bytes (%d bits)\n",
               (get_bits_count(&s->gb) + 7) / 8, get_bits_count(&s->gb));
    }
    if (s->got_picture) {
        av_log(avctx, AV_LOG_WARNING, "EOI missing, emulating\n");
        goto eoi_parser;
    }
    av_log(avctx, AV_LOG_FATAL, "No JPEG data found in image\n");
    return AVERROR_INVALIDDATA;
fail:
    s->got_picture = 0;
    return ret;
the_end:
    if (s->upscale_h) {
        uint8_t *line = s->picture_ptr->data[s->upscale_h];
        av_assert0(avctx->pix_fmt == AV_PIX_FMT_YUVJ444P ||
                   avctx->pix_fmt == AV_PIX_FMT_YUV444P  ||
                   avctx->pix_fmt == AV_PIX_FMT_YUVJ440P ||
                   avctx->pix_fmt == AV_PIX_FMT_YUV440P);
        for (i = 0; i < s->chroma_height; i++) {
            for (index = s->width - 1; index; index--)
                line[index] = (line[index / 2] + line[(index + 1) / 2]) >> 1;
            line += s->linesize[s->upscale_h];
        }
    }
    if (s->upscale_v) {
        uint8_t *dst = &((uint8_t *)s->picture_ptr->data[s->upscale_v])[(s->height - 1) * s->linesize[s->upscale_v]];
        int w;
        avcodec_get_chroma_sub_sample(s->avctx->pix_fmt, &hshift, &vshift);
        w = s->width >> hshift;
        av_assert0(avctx->pix_fmt == AV_PIX_FMT_YUVJ444P ||
                   avctx->pix_fmt == AV_PIX_FMT_YUV444P  ||
                   avctx->pix_fmt == AV_PIX_FMT_YUVJ422P ||
                   avctx->pix_fmt == AV_PIX_FMT_YUV422P);
        for (i = s->height - 1; i; i--) {
            uint8_t *src1 = &((uint8_t *)s->picture_ptr->data[s->upscale_v])[i / 2 * s->linesize[s->upscale_v]];
            uint8_t *src2 = &((uint8_t *)s->picture_ptr->data[s->upscale_v])[(i + 1) / 2 * s->linesize[s->upscale_v]];
            if (src1 == src2) {
                memcpy(dst, src1, w);
            } else {
                for (index = 0; index < w; index++)
                    dst[index] = (src1[index] + src2[index]) >> 1;
            }
            dst -= s->linesize[s->upscale_v];
        }
    }
    if (s->flipped && (s->avctx->flags & CODEC_FLAG_EMU_EDGE)) {
        int j;
        avcodec_get_chroma_sub_sample(s->avctx->pix_fmt, &hshift, &vshift);
        for (index=0; index<4; index++) {
            uint8_t *dst = s->picture_ptr->data[index];
            int w = s->width;
            int h = s->height;
            if(index && index<3){
                w = FF_CEIL_RSHIFT(w, hshift);
                h = FF_CEIL_RSHIFT(h, vshift);
            }
            if(dst){
                uint8_t *dst2 = dst + s->linesize[index]*(h-1);
                for (i=0; i<h/2; i++) {
                    for (j=0; j<w; j++)
                        FFSWAP(int, dst[j], dst2[j]);
                    dst  += s->linesize[index];
                    dst2 -= s->linesize[index];
                }
            }
        }
    }

    av_dict_copy(avpriv_frame_get_metadatap(data), s->exif_metadata, 0);
    av_dict_free(&s->exif_metadata);

    av_log(avctx, AV_LOG_DEBUG, "decode frame unused %td bytes\n",
           buf_end - buf_ptr);
//  return buf_end - buf_ptr;
    return buf_ptr - buf;
}

av_cold int ff_mjpeg_decode_end(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;
    int i, j;

    if (s->interlaced && s->bottom_field == !s->interlace_polarity && s->got_picture && !avctx->frame_number) {
        av_log(avctx, AV_LOG_INFO, "Single field\n");
    }

    if (s->picture_ptr)
        av_frame_unref(s->picture_ptr);

    av_free(s->buffer);
    av_freep(&s->ljpeg_buffer);
    s->ljpeg_buffer_size = 0;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 4; j++)
            ff_free_vlc(&s->vlcs[i][j]);
    }
    for (i = 0; i < MAX_COMPONENTS; i++) {
        av_freep(&s->blocks[i]);
        av_freep(&s->last_nnz[i]);
    }
    av_dict_free(&s->exif_metadata);
    return 0;
}

static void decode_flush(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;
    s->got_picture = 0;
}

#if CONFIG_MJPEG_DECODER
#define OFFSET(x) offsetof(MJpegDecodeContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "extern_huff", "Use external huffman table.",
      OFFSET(extern_huff), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VD },
    { NULL },
};

static const AVClass mjpegdec_class = {
    .class_name = "MJPEG decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mjpeg_decoder = {
    .name           = "mjpeg",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MJPEG,
    .priv_data_size = sizeof(MJpegDecodeContext),
    .init           = ff_mjpeg_decode_init,
    .close          = ff_mjpeg_decode_end,
    .decode         = ff_mjpeg_decode_frame,
    .flush          = decode_flush,
    .capabilities   = CODEC_CAP_DR1,
    .max_lowres     = 3,
    .long_name      = NULL_IF_CONFIG_SMALL("MJPEG (Motion JPEG)"),
    .priv_class     = &mjpegdec_class,
};
#endif
#if CONFIG_THP_DECODER
AVCodec ff_thp_decoder = {
    .name           = "thp",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_THP,
    .priv_data_size = sizeof(MJpegDecodeContext),
    .init           = ff_mjpeg_decode_init,
    .close          = ff_mjpeg_decode_end,
    .decode         = ff_mjpeg_decode_frame,
    .flush          = decode_flush,
    .capabilities   = CODEC_CAP_DR1,
    .max_lowres     = 3,
    .long_name      = NULL_IF_CONFIG_SMALL("Nintendo Gamecube THP video"),
};
#endif
