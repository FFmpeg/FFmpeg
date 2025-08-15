/*
 * ProRes RAW decoder
 * Copyright (c) 2023-2025 Paul B Mahol
 * Copyright (c) 2025 Lynne
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

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/mem.h"

#define CACHED_BITSTREAM_READER !ARCH_X86_32

#include "config_components.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "idctdsp.h"
#include "proresdata.h"
#include "thread.h"
#include "hwconfig.h"
#include "hwaccel_internal.h"

#include "prores_raw.h"

static av_cold int decode_init(AVCodecContext *avctx)
{
    ProResRAWContext *s = avctx->priv_data;
    uint8_t idct_permutation[64];

    avctx->bits_per_raw_sample = 12;
    avctx->color_primaries = AVCOL_PRI_UNSPECIFIED;
    avctx->color_trc = AVCOL_TRC_UNSPECIFIED;
    avctx->colorspace = AVCOL_SPC_UNSPECIFIED;

    s->pix_fmt = AV_PIX_FMT_NONE;

    ff_blockdsp_init(&s->bdsp);
    ff_proresdsp_init(&s->prodsp, avctx->bits_per_raw_sample);

    ff_init_scantable_permutation(idct_permutation,
                                  s->prodsp.idct_permutation_type);

    ff_permute_scantable(s->scan, ff_prores_interlaced_scan, idct_permutation);

    return 0;
}

static int16_t get_value(GetBitContext *gb, int16_t codebook)
{
    const int16_t switch_bits = codebook >> 8;
    const int16_t rice_order  = codebook & 0xf;
    const int16_t exp_order   = (codebook >> 4) & 0xf;
    int16_t q, bits;

    uint32_t b = show_bits_long(gb, 32);
    if (!b)
        return 0;
    q = ff_clz(b);

    if (b & 0x80000000) {
        skip_bits_long(gb, 1 + rice_order);
        return (b & 0x7FFFFFFF) >> (31 - rice_order);
    }

    if (q <= switch_bits) {
        skip_bits_long(gb, 1 + rice_order + q);
        return (q << rice_order) +
                (((b << (q + 1)) >> 1) >> (31 - rice_order));
    }

    bits = exp_order + (q << 1) - switch_bits;
    skip_bits_long(gb, bits);
    return (b >> (32 - bits)) +
           ((switch_bits + 1) << rice_order) -
           (1 << exp_order);
}

#define TODCCODEBOOK(x) ((x + 1) >> 1)

static const uint8_t align_tile_w[16] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
};

#define DC_CB_MAX 12
const uint8_t ff_prores_raw_dc_cb[DC_CB_MAX + 1] = {
    16, 33, 50, 51, 51, 51, 68, 68, 68, 68, 68, 68, 118,
};

#define AC_CB_MAX 94
const int16_t ff_prores_raw_ac_cb[AC_CB_MAX + 1] = {
      0, 529, 273, 273, 546, 546, 546, 290, 290, 290, 563, 563,
    563, 563, 563, 563, 563, 563, 307, 307, 580, 580, 580, 580,
    580, 580, 580, 580, 580, 580, 580, 580, 580, 580, 580, 580,
    580, 580, 580, 580, 580, 580, 853, 853, 853, 853, 853, 853,
    853, 853, 853, 853, 853, 853, 853, 853, 853, 853, 853, 853,
    853, 853, 853, 853, 853, 853, 853, 853, 853, 853, 853, 853,
    853, 853, 853, 853, 853, 853, 853, 853, 853, 853, 853, 853,
    853, 853, 853, 853, 853, 853, 853, 853, 853, 853, 358
};

#define RN_CB_MAX 27
const int16_t ff_prores_raw_rn_cb[RN_CB_MAX + 1] = {
    512, 256, 0, 0, 529, 529, 273, 273, 17, 17, 33, 33, 546,
    34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 34, 50, 50, 68,
};

#define LN_CB_MAX 14
const int16_t ff_prores_raw_ln_cb[LN_CB_MAX + 1] = {
    256, 273, 546, 546, 290, 290, 1075, 1075, 563, 563, 563, 563, 563, 563, 51
};

static int decode_comp(AVCodecContext *avctx, TileContext *tile,
                       AVFrame *frame, const uint8_t *data, int size,
                       int component, int16_t *qmat)
{
    int ret;
    ProResRAWContext *s = avctx->priv_data;
    const ptrdiff_t linesize = frame->linesize[0] >> 1;
    uint16_t *dst = (uint16_t *)(frame->data[0] + tile->y*frame->linesize[0] + 2*tile->x);

    int idx;
    const int w = FFMIN(s->tw, avctx->width - tile->x) / 2;
    const int nb_blocks = w / 8;
    const int log2_nb_blocks = 31 - ff_clz(nb_blocks);
    const int block_mask = (1 << log2_nb_blocks) - 1;
    const int nb_codes = 64 * nb_blocks;

    LOCAL_ALIGNED_32(int16_t, block, [64*16]);

    int16_t sign = 0;
    int16_t dc_add = 0;
    int16_t dc_codebook;

    int16_t ac, rn, ln;
    int16_t ac_codebook = 49;
    int16_t rn_codebook = 0;
    int16_t ln_codebook = 66;

    const uint8_t *scan = s->scan;
    GetBitContext gb;

    if (component > 1)
        dst += linesize;
    dst += component & 1;

    if ((ret = init_get_bits8(&gb, data, size)) < 0)
        return ret;

    for (int n = 0; n < nb_blocks; n++)
        s->bdsp.clear_block(block + n*64);

    /* Special handling for first block */
    int dc = get_value(&gb, 700);
    int prev_dc = (dc >> 1) ^ -(dc & 1);
    block[0] = (((dc&1) + (dc>>1) ^ -(int)(dc & 1)) + (dc & 1)) + 1;

    for (int n = 1; n < nb_blocks; n++) {
        if (get_bits_left(&gb) <= 0)
            break;

        if ((n & 15) == 1)
            dc_codebook = 100;
        else
            dc_codebook = ff_prores_raw_dc_cb[FFMIN(TODCCODEBOOK(dc), DC_CB_MAX)];

        dc = get_value(&gb, dc_codebook);

        sign = sign ^ dc & 1;
        dc_add = (-sign ^ TODCCODEBOOK(dc)) + sign;
        sign = dc_add < 0;
        prev_dc += dc_add;

        block[n*64] = prev_dc + 1;
    }

    for (int n = nb_blocks; n <= nb_codes;) {
        if (get_bits_left(&gb) <= 0)
            break;

        ln = get_value(&gb, ln_codebook);

        for (int i = 0; i < ln; i++) {
            if (get_bits_left(&gb) <= 0)
                break;

            if ((n + i) >= nb_codes)
                break;

            ac = get_value(&gb, ac_codebook);
            ac_codebook = ff_prores_raw_ac_cb[FFMIN(ac, AC_CB_MAX)];
            sign = -get_bits1(&gb);

            idx = scan[(n + i) >> log2_nb_blocks] + (((n + i) & block_mask) << 6);
            block[idx] = ((ac + 1) ^ sign) - sign;
        }

        n += ln;
        if (n >= nb_codes)
            break;

        rn = get_value(&gb, rn_codebook);
        rn_codebook = ff_prores_raw_rn_cb[FFMIN(rn, RN_CB_MAX)];

        n += rn + 1;
        if (n >= nb_codes)
            break;

        if (get_bits_left(&gb) <= 0)
            break;

        ac = get_value(&gb, ac_codebook);
        sign = -get_bits1(&gb);

        idx = scan[n >> log2_nb_blocks] + ((n & block_mask) << 6);
        block[idx] = ((ac + 1) ^ sign) - sign;

        ac_codebook = ff_prores_raw_ac_cb[FFMIN(ac, AC_CB_MAX)];
        ln_codebook = ff_prores_raw_ln_cb[FFMIN(ac, LN_CB_MAX)];

        n++;
    }

    for (int n = 0; n < nb_blocks; n++) {
        uint16_t *ptr = dst + n*16;
        s->prodsp.idct_put_bayer(ptr, linesize, block + n*64, qmat);
    }

    return 0;
}

static int decode_tile(AVCodecContext *avctx, TileContext *tile,
                       AVFrame *frame)
{
    int ret;
    ProResRAWContext *s = avctx->priv_data;

    GetByteContext *gb = &tile->gb;
    LOCAL_ALIGNED_32(int16_t, qmat, [64]);

    if (tile->x >= avctx->width)
        return 0;

    /* Tile header */
    int header_len = bytestream2_get_byteu(gb) >> 3;
    int16_t scale = bytestream2_get_byteu(gb);

    int size[4];
    size[0] = bytestream2_get_be16(gb);
    size[1] = bytestream2_get_be16(gb);
    size[2] = bytestream2_get_be16(gb);
    size[3] = bytestream2_size(gb) - size[0] - size[1] - size[2] - header_len;
    if (size[3] < 0)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < 64; i++)
        qmat[i] = s->qmat[i] * scale >> 1;

    const uint8_t *comp_start = gb->buffer_start + header_len;

    ret = decode_comp(avctx, tile, frame, comp_start,
                      size[0], 2, qmat);
    if (ret < 0)
        goto fail;

    ret = decode_comp(avctx, tile, frame, comp_start + size[0],
                      size[1], 1, qmat);
    if (ret < 0)
        goto fail;

    ret = decode_comp(avctx, tile, frame, comp_start + size[0] + size[1],
                      size[2], 3, qmat);
    if (ret < 0)
        goto fail;

    ret = decode_comp(avctx, tile, frame, comp_start + size[0] + size[1] + size[2],
                      size[3], 0, qmat);
    if (ret < 0)
        goto fail;

    return 0;
fail:
    av_log(avctx, AV_LOG_ERROR, "tile %d/%d decoding error\n", tile->x, tile->y);
    return ret;
}

static int decode_tiles(AVCodecContext *avctx, void *arg,
                        int n, int thread_nb)
{
    ProResRAWContext *s = avctx->priv_data;
    TileContext *tile = &s->tiles[n];
    AVFrame *frame = arg;

    return decode_tile(avctx, tile, frame);
}

static enum AVPixelFormat get_pixel_format(AVCodecContext *avctx,
                                           enum AVPixelFormat pix_fmt)
{
    enum AVPixelFormat pix_fmts[] = {
#if CONFIG_PRORES_RAW_VULKAN_HWACCEL
        AV_PIX_FMT_VULKAN,
#endif
        pix_fmt,
        AV_PIX_FMT_NONE,
    };

    return ff_get_format(avctx, pix_fmts);
}

static int decode_frame(AVCodecContext *avctx,
                        AVFrame *frame, int *got_frame_ptr,
                        AVPacket *avpkt)
{
    int ret;
    ProResRAWContext *s = avctx->priv_data;
    DECLARE_ALIGNED(32, uint8_t, qmat)[64];
    memset(qmat, 1, 64);

    GetByteContext gb;
    bytestream2_init(&gb, avpkt->data, avpkt->size);
    if (bytestream2_get_be32(&gb) != avpkt->size)
        return AVERROR_INVALIDDATA;

    /* ProRes RAW frame */
    if (bytestream2_get_le32(&gb) != MKTAG('p','r','r','f'))
        return AVERROR_INVALIDDATA;

    int header_len = bytestream2_get_be16(&gb);
    if (header_len < 62)
        return AVERROR_INVALIDDATA;

    GetByteContext gb_hdr;
    bytestream2_init(&gb_hdr, gb.buffer, header_len - 2);
    bytestream2_skip(&gb, header_len - 2);

    bytestream2_skip(&gb_hdr, 1);
    s->version = bytestream2_get_byte(&gb_hdr);
    if (s->version > 1) {
        avpriv_request_sample(avctx, "Version %d", s->version);
        return AVERROR_PATCHWELCOME;
    }

    /* Vendor header (e.g. "peac" for Panasonic or "atm0" for Atmos) */
    bytestream2_skip(&gb_hdr, 4);

    /* Width and height must always be even */
    int w = bytestream2_get_be16(&gb_hdr);
    int h = bytestream2_get_be16(&gb_hdr);
    if ((w & 1) || (h & 1))
        return AVERROR_INVALIDDATA;

    if (w != avctx->width || h != avctx->height) {
        av_log(avctx, AV_LOG_WARNING, "picture resolution change: %ix%i -> %ix%i\n",
               avctx->width, avctx->height, w, h);
        if ((ret = ff_set_dimensions(avctx, w, h)) < 0)
            return ret;
    }

    avctx->coded_width  = FFALIGN(w, 16);
    avctx->coded_height = FFALIGN(h, 16);

    enum AVPixelFormat pix_fmt = AV_PIX_FMT_BAYER_RGGB16;
    if (pix_fmt != s->pix_fmt) {
        s->pix_fmt = pix_fmt;

        ret = get_pixel_format(avctx, pix_fmt);
        if (ret < 0)
            return ret;

        avctx->pix_fmt = ret;
    }

    bytestream2_skip(&gb_hdr, 1 * 4);
    bytestream2_skip(&gb_hdr, 2); /* & 0x3 */
    bytestream2_skip(&gb_hdr, 2);
    bytestream2_skip(&gb_hdr, 4);
    bytestream2_skip(&gb_hdr, 4);
    bytestream2_skip(&gb_hdr, 4 * 3 * 3);
    bytestream2_skip(&gb_hdr, 4);
    bytestream2_skip(&gb_hdr, 2);

    /* Flags */
    int flags = bytestream2_get_be16(&gb_hdr);
    int align = (flags >> 1) & 0x7;

    /* Quantization matrix */
    if (flags & 1)
        bytestream2_get_buffer(&gb_hdr, qmat, 64);

    if ((flags >> 4) & 1) {
        bytestream2_skip(&gb_hdr, 2);
        bytestream2_skip(&gb_hdr, 2 * 7);
    }

    ff_permute_scantable(s->qmat, s->prodsp.idct_permutation, qmat);

    s->nb_tw = (w + 15) >> 4;
    s->nb_th = (h + 15) >> 4;
    s->nb_tw = (s->nb_tw >> align) + align_tile_w[~(-1 * (1 << align)) & s->nb_tw];
    s->nb_tiles = s->nb_tw * s->nb_th;
    av_log(avctx, AV_LOG_DEBUG, "%dx%d | nb_tiles: %d\n", s->nb_tw, s->nb_th, s->nb_tiles);

    s->tw = s->version == 0 ? 128 : 256;
    s->th = 16;
    av_log(avctx, AV_LOG_DEBUG, "tile_size: %dx%d\n", s->tw, s->th);

    av_fast_mallocz(&s->tiles, &s->tiles_size, s->nb_tiles * sizeof(*s->tiles));
    if (!s->tiles)
        return AVERROR(ENOMEM);

    if (bytestream2_get_bytes_left(&gb) < s->nb_tiles * 2)
        return AVERROR_INVALIDDATA;

    /* Read tile data offsets */
    int offset = bytestream2_tell(&gb) + s->nb_tiles * 2;
    for (int n = 0; n < s->nb_tiles; n++) {
        TileContext *tile = &s->tiles[n];

        int size = bytestream2_get_be16(&gb);
        if (offset >= avpkt->size)
            return AVERROR_INVALIDDATA;
        if (size >= avpkt->size)
            return AVERROR_INVALIDDATA;
        if (offset > avpkt->size - size)
            return AVERROR_INVALIDDATA;

        bytestream2_init(&tile->gb, avpkt->data + offset, size);

        tile->y = (n / s->nb_tw) * s->th;
        tile->x = (n % s->nb_tw) * s->tw;

        offset += size;
    }

    ret = ff_thread_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    s->frame = frame;

    /* Start */
    if (avctx->hwaccel) {
        const FFHWAccel *hwaccel = ffhwaccel(avctx->hwaccel);

        ret = ff_hwaccel_frame_priv_alloc(avctx, &s->hwaccel_picture_private);
        if (ret < 0)
            return ret;

        ret = hwaccel->start_frame(avctx, avpkt->buf, avpkt->data, avpkt->size);
        if (ret < 0)
            return ret;

        for (int n = 0; n < s->nb_tiles; n++) {
            TileContext *tile = &s->tiles[n];
            ret = hwaccel->decode_slice(avctx, tile->gb.buffer,
                                        tile->gb.buffer_end - tile->gb.buffer);
            if (ret < 0)
                return ret;
        }

        ret = hwaccel->end_frame(avctx);
        if (ret < 0)
            return ret;

        av_refstruct_unref(&s->hwaccel_picture_private);
    } else {
        avctx->execute2(avctx, decode_tiles, frame, NULL, s->nb_tiles);
    }

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->flags    |= AV_FRAME_FLAG_KEY;

    *got_frame_ptr = 1;

    return avpkt->size;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    ProResRAWContext *s = avctx->priv_data;
    av_refstruct_unref(&s->hwaccel_picture_private);
    av_freep(&s->tiles);
    return 0;
}

#if HAVE_THREADS
static int update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    ProResRAWContext *rsrc = src->priv_data;
    ProResRAWContext *rdst = dst->priv_data;

    rdst->pix_fmt = rsrc->pix_fmt;

    return 0;
}
#endif

const FFCodec ff_prores_raw_decoder = {
    .p.name           = "prores_raw",
    CODEC_LONG_NAME("Apple ProRes RAW"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_PRORES_RAW,
    .priv_data_size   = sizeof(ProResRAWContext),
    .init             = decode_init,
    .close            = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    UPDATE_THREAD_CONTEXT(update_thread_context),
    .p.capabilities   = AV_CODEC_CAP_DR1 |
                        AV_CODEC_CAP_FRAME_THREADS |
                        AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP |
                      FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .hw_configs     = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_PRORES_RAW_VULKAN_HWACCEL
        HWACCEL_VULKAN(prores_raw),
#endif
        NULL
    },
};
