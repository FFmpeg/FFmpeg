/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#include "avcodec.h"
#include "get_bits.h"
#include "hwconfig.h"
#include "internal.h"
#include "profiles.h"
#include "thread.h"
#include "pthread_internal.h"

#include "videodsp.h"
#include "vp56.h"
#include "vp9.h"
#include "vp9data.h"
#include "vp9dec.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "libavutil/video_enc_params.h"

#define VP9_SYNCCODE 0x498342

#if HAVE_THREADS
DEFINE_OFFSET_ARRAY(VP9Context, vp9_context, pthread_init_cnt,
                    (offsetof(VP9Context, progress_mutex)),
                    (offsetof(VP9Context, progress_cond)));

static int vp9_alloc_entries(AVCodecContext *avctx, int n) {
    VP9Context *s = avctx->priv_data;
    int i;

    if (avctx->active_thread_type & FF_THREAD_SLICE)  {
        if (s->entries)
            av_freep(&s->entries);

        s->entries = av_malloc_array(n, sizeof(atomic_int));
        if (!s->entries)
            return AVERROR(ENOMEM);

        for (i  = 0; i < n; i++)
            atomic_init(&s->entries[i], 0);
    }
    return 0;
}

static void vp9_report_tile_progress(VP9Context *s, int field, int n) {
    pthread_mutex_lock(&s->progress_mutex);
    atomic_fetch_add_explicit(&s->entries[field], n, memory_order_release);
    pthread_cond_signal(&s->progress_cond);
    pthread_mutex_unlock(&s->progress_mutex);
}

static void vp9_await_tile_progress(VP9Context *s, int field, int n) {
    if (atomic_load_explicit(&s->entries[field], memory_order_acquire) >= n)
        return;

    pthread_mutex_lock(&s->progress_mutex);
    while (atomic_load_explicit(&s->entries[field], memory_order_relaxed) != n)
        pthread_cond_wait(&s->progress_cond, &s->progress_mutex);
    pthread_mutex_unlock(&s->progress_mutex);
}
#else
static int vp9_alloc_entries(AVCodecContext *avctx, int n) { return 0; }
#endif

static void vp9_tile_data_free(VP9TileData *td)
{
    av_freep(&td->b_base);
    av_freep(&td->block_base);
    av_freep(&td->block_structure);
}

static void vp9_frame_unref(AVCodecContext *avctx, VP9Frame *f)
{
    ff_thread_release_buffer(avctx, &f->tf);
    av_buffer_unref(&f->extradata);
    av_buffer_unref(&f->hwaccel_priv_buf);
    f->segmentation_map = NULL;
    f->hwaccel_picture_private = NULL;
}

static int vp9_frame_alloc(AVCodecContext *avctx, VP9Frame *f)
{
    VP9Context *s = avctx->priv_data;
    int ret, sz;

    ret = ff_thread_get_buffer(avctx, &f->tf, AV_GET_BUFFER_FLAG_REF);
    if (ret < 0)
        return ret;

    sz = 64 * s->sb_cols * s->sb_rows;
    if (sz != s->frame_extradata_pool_size) {
        av_buffer_pool_uninit(&s->frame_extradata_pool);
        s->frame_extradata_pool = av_buffer_pool_init(sz * (1 + sizeof(VP9mvrefPair)), NULL);
        if (!s->frame_extradata_pool) {
            s->frame_extradata_pool_size = 0;
            goto fail;
        }
        s->frame_extradata_pool_size = sz;
    }
    f->extradata = av_buffer_pool_get(s->frame_extradata_pool);
    if (!f->extradata) {
        goto fail;
    }
    memset(f->extradata->data, 0, f->extradata->size);

    f->segmentation_map = f->extradata->data;
    f->mv = (VP9mvrefPair *) (f->extradata->data + sz);

    if (avctx->hwaccel) {
        const AVHWAccel *hwaccel = avctx->hwaccel;
        av_assert0(!f->hwaccel_picture_private);
        if (hwaccel->frame_priv_data_size) {
            f->hwaccel_priv_buf = av_buffer_allocz(hwaccel->frame_priv_data_size);
            if (!f->hwaccel_priv_buf)
                goto fail;
            f->hwaccel_picture_private = f->hwaccel_priv_buf->data;
        }
    }

    return 0;

fail:
    vp9_frame_unref(avctx, f);
    return AVERROR(ENOMEM);
}

static int vp9_frame_ref(AVCodecContext *avctx, VP9Frame *dst, VP9Frame *src)
{
    int ret;

    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        return ret;

    dst->extradata = av_buffer_ref(src->extradata);
    if (!dst->extradata)
        goto fail;

    dst->segmentation_map = src->segmentation_map;
    dst->mv = src->mv;
    dst->uses_2pass = src->uses_2pass;

    if (src->hwaccel_picture_private) {
        dst->hwaccel_priv_buf = av_buffer_ref(src->hwaccel_priv_buf);
        if (!dst->hwaccel_priv_buf)
            goto fail;
        dst->hwaccel_picture_private = dst->hwaccel_priv_buf->data;
    }

    return 0;

fail:
    vp9_frame_unref(avctx, dst);
    return AVERROR(ENOMEM);
}

static int update_size(AVCodecContext *avctx, int w, int h)
{
#define HWACCEL_MAX (CONFIG_VP9_DXVA2_HWACCEL + \
                     CONFIG_VP9_D3D11VA_HWACCEL * 2 + \
                     CONFIG_VP9_NVDEC_HWACCEL + \
                     CONFIG_VP9_VAAPI_HWACCEL + \
                     CONFIG_VP9_VDPAU_HWACCEL + \
                     CONFIG_VP9_VIDEOTOOLBOX_HWACCEL)
    enum AVPixelFormat pix_fmts[HWACCEL_MAX + 2], *fmtp = pix_fmts;
    VP9Context *s = avctx->priv_data;
    uint8_t *p;
    int bytesperpixel = s->bytesperpixel, ret, cols, rows;
    int lflvl_len, i;

    av_assert0(w > 0 && h > 0);

    if (!(s->pix_fmt == s->gf_fmt && w == s->w && h == s->h)) {
        if ((ret = ff_set_dimensions(avctx, w, h)) < 0)
            return ret;

        switch (s->pix_fmt) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUV420P10:
#if CONFIG_VP9_DXVA2_HWACCEL
            *fmtp++ = AV_PIX_FMT_DXVA2_VLD;
#endif
#if CONFIG_VP9_D3D11VA_HWACCEL
            *fmtp++ = AV_PIX_FMT_D3D11VA_VLD;
            *fmtp++ = AV_PIX_FMT_D3D11;
#endif
#if CONFIG_VP9_NVDEC_HWACCEL
            *fmtp++ = AV_PIX_FMT_CUDA;
#endif
#if CONFIG_VP9_VAAPI_HWACCEL
            *fmtp++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_VP9_VDPAU_HWACCEL
            *fmtp++ = AV_PIX_FMT_VDPAU;
#endif
#if CONFIG_VP9_VIDEOTOOLBOX_HWACCEL
            *fmtp++ = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
            break;
        case AV_PIX_FMT_YUV420P12:
#if CONFIG_VP9_NVDEC_HWACCEL
            *fmtp++ = AV_PIX_FMT_CUDA;
#endif
#if CONFIG_VP9_VAAPI_HWACCEL
            *fmtp++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_VP9_VDPAU_HWACCEL
            *fmtp++ = AV_PIX_FMT_VDPAU;
#endif
            break;
        }

        *fmtp++ = s->pix_fmt;
        *fmtp = AV_PIX_FMT_NONE;

        ret = ff_thread_get_format(avctx, pix_fmts);
        if (ret < 0)
            return ret;

        avctx->pix_fmt = ret;
        s->gf_fmt  = s->pix_fmt;
        s->w = w;
        s->h = h;
    }

    cols = (w + 7) >> 3;
    rows = (h + 7) >> 3;

    if (s->intra_pred_data[0] && cols == s->cols && rows == s->rows && s->pix_fmt == s->last_fmt)
        return 0;

    s->last_fmt  = s->pix_fmt;
    s->sb_cols   = (w + 63) >> 6;
    s->sb_rows   = (h + 63) >> 6;
    s->cols      = (w + 7) >> 3;
    s->rows      = (h + 7) >> 3;
    lflvl_len    = avctx->active_thread_type == FF_THREAD_SLICE ? s->sb_rows : 1;

#define assign(var, type, n) var = (type) p; p += s->sb_cols * (n) * sizeof(*var)
    av_freep(&s->intra_pred_data[0]);
    // FIXME we slightly over-allocate here for subsampled chroma, but a little
    // bit of padding shouldn't affect performance...
    p = av_malloc(s->sb_cols * (128 + 192 * bytesperpixel +
                                lflvl_len * sizeof(*s->lflvl) + 16 * sizeof(*s->above_mv_ctx)));
    if (!p)
        return AVERROR(ENOMEM);
    assign(s->intra_pred_data[0],  uint8_t *,             64 * bytesperpixel);
    assign(s->intra_pred_data[1],  uint8_t *,             64 * bytesperpixel);
    assign(s->intra_pred_data[2],  uint8_t *,             64 * bytesperpixel);
    assign(s->above_y_nnz_ctx,     uint8_t *,             16);
    assign(s->above_mode_ctx,      uint8_t *,             16);
    assign(s->above_mv_ctx,        VP56mv(*)[2],          16);
    assign(s->above_uv_nnz_ctx[0], uint8_t *,             16);
    assign(s->above_uv_nnz_ctx[1], uint8_t *,             16);
    assign(s->above_partition_ctx, uint8_t *,              8);
    assign(s->above_skip_ctx,      uint8_t *,              8);
    assign(s->above_txfm_ctx,      uint8_t *,              8);
    assign(s->above_segpred_ctx,   uint8_t *,              8);
    assign(s->above_intra_ctx,     uint8_t *,              8);
    assign(s->above_comp_ctx,      uint8_t *,              8);
    assign(s->above_ref_ctx,       uint8_t *,              8);
    assign(s->above_filter_ctx,    uint8_t *,              8);
    assign(s->lflvl,               VP9Filter *,            lflvl_len);
#undef assign

    if (s->td) {
        for (i = 0; i < s->active_tile_cols; i++)
            vp9_tile_data_free(&s->td[i]);
    }

    if (s->s.h.bpp != s->last_bpp) {
        ff_vp9dsp_init(&s->dsp, s->s.h.bpp, avctx->flags & AV_CODEC_FLAG_BITEXACT);
        ff_videodsp_init(&s->vdsp, s->s.h.bpp);
        s->last_bpp = s->s.h.bpp;
    }

    return 0;
}

static int update_block_buffers(AVCodecContext *avctx)
{
    int i;
    VP9Context *s = avctx->priv_data;
    int chroma_blocks, chroma_eobs, bytesperpixel = s->bytesperpixel;
    VP9TileData *td = &s->td[0];

    if (td->b_base && td->block_base && s->block_alloc_using_2pass == s->s.frames[CUR_FRAME].uses_2pass)
        return 0;

    vp9_tile_data_free(td);
    chroma_blocks = 64 * 64 >> (s->ss_h + s->ss_v);
    chroma_eobs   = 16 * 16 >> (s->ss_h + s->ss_v);
    if (s->s.frames[CUR_FRAME].uses_2pass) {
        int sbs = s->sb_cols * s->sb_rows;

        td->b_base = av_malloc_array(s->cols * s->rows, sizeof(VP9Block));
        td->block_base = av_mallocz(((64 * 64 + 2 * chroma_blocks) * bytesperpixel * sizeof(int16_t) +
                                    16 * 16 + 2 * chroma_eobs) * sbs);
        if (!td->b_base || !td->block_base)
            return AVERROR(ENOMEM);
        td->uvblock_base[0] = td->block_base + sbs * 64 * 64 * bytesperpixel;
        td->uvblock_base[1] = td->uvblock_base[0] + sbs * chroma_blocks * bytesperpixel;
        td->eob_base = (uint8_t *) (td->uvblock_base[1] + sbs * chroma_blocks * bytesperpixel);
        td->uveob_base[0] = td->eob_base + 16 * 16 * sbs;
        td->uveob_base[1] = td->uveob_base[0] + chroma_eobs * sbs;

        if (avctx->export_side_data & AV_CODEC_EXPORT_DATA_VIDEO_ENC_PARAMS) {
            td->block_structure = av_malloc_array(s->cols * s->rows, sizeof(*td->block_structure));
            if (!td->block_structure)
                return AVERROR(ENOMEM);
        }
    } else {
        for (i = 1; i < s->active_tile_cols; i++)
            vp9_tile_data_free(&s->td[i]);

        for (i = 0; i < s->active_tile_cols; i++) {
            s->td[i].b_base = av_malloc(sizeof(VP9Block));
            s->td[i].block_base = av_mallocz((64 * 64 + 2 * chroma_blocks) * bytesperpixel * sizeof(int16_t) +
                                       16 * 16 + 2 * chroma_eobs);
            if (!s->td[i].b_base || !s->td[i].block_base)
                return AVERROR(ENOMEM);
            s->td[i].uvblock_base[0] = s->td[i].block_base + 64 * 64 * bytesperpixel;
            s->td[i].uvblock_base[1] = s->td[i].uvblock_base[0] + chroma_blocks * bytesperpixel;
            s->td[i].eob_base = (uint8_t *) (s->td[i].uvblock_base[1] + chroma_blocks * bytesperpixel);
            s->td[i].uveob_base[0] = s->td[i].eob_base + 16 * 16;
            s->td[i].uveob_base[1] = s->td[i].uveob_base[0] + chroma_eobs;

            if (avctx->export_side_data & AV_CODEC_EXPORT_DATA_VIDEO_ENC_PARAMS) {
                s->td[i].block_structure = av_malloc_array(s->cols * s->rows, sizeof(*td->block_structure));
                if (!s->td[i].block_structure)
                    return AVERROR(ENOMEM);
            }
        }
    }
    s->block_alloc_using_2pass = s->s.frames[CUR_FRAME].uses_2pass;

    return 0;
}

// The sign bit is at the end, not the start, of a bit sequence
static av_always_inline int get_sbits_inv(GetBitContext *gb, int n)
{
    int v = get_bits(gb, n);
    return get_bits1(gb) ? -v : v;
}

static av_always_inline int inv_recenter_nonneg(int v, int m)
{
    if (v > 2 * m)
        return v;
    if (v & 1)
        return m - ((v + 1) >> 1);
    return m + (v >> 1);
}

// differential forward probability updates
static int update_prob(VP56RangeCoder *c, int p)
{
    static const uint8_t inv_map_table[255] = {
          7,  20,  33,  46,  59,  72,  85,  98, 111, 124, 137, 150, 163, 176,
        189, 202, 215, 228, 241, 254,   1,   2,   3,   4,   5,   6,   8,   9,
         10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  21,  22,  23,  24,
         25,  26,  27,  28,  29,  30,  31,  32,  34,  35,  36,  37,  38,  39,
         40,  41,  42,  43,  44,  45,  47,  48,  49,  50,  51,  52,  53,  54,
         55,  56,  57,  58,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,
         70,  71,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,
         86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  99, 100,
        101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 112, 113, 114, 115,
        116, 117, 118, 119, 120, 121, 122, 123, 125, 126, 127, 128, 129, 130,
        131, 132, 133, 134, 135, 136, 138, 139, 140, 141, 142, 143, 144, 145,
        146, 147, 148, 149, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160,
        161, 162, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
        177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 190, 191,
        192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 203, 204, 205, 206,
        207, 208, 209, 210, 211, 212, 213, 214, 216, 217, 218, 219, 220, 221,
        222, 223, 224, 225, 226, 227, 229, 230, 231, 232, 233, 234, 235, 236,
        237, 238, 239, 240, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251,
        252, 253, 253,
    };
    int d;

    /* This code is trying to do a differential probability update. For a
     * current probability A in the range [1, 255], the difference to a new
     * probability of any value can be expressed differentially as 1-A, 255-A
     * where some part of this (absolute range) exists both in positive as
     * well as the negative part, whereas another part only exists in one
     * half. We're trying to code this shared part differentially, i.e.
     * times two where the value of the lowest bit specifies the sign, and
     * the single part is then coded on top of this. This absolute difference
     * then again has a value of [0, 254], but a bigger value in this range
     * indicates that we're further away from the original value A, so we
     * can code this as a VLC code, since higher values are increasingly
     * unlikely. The first 20 values in inv_map_table[] allow 'cheap, rough'
     * updates vs. the 'fine, exact' updates further down the range, which
     * adds one extra dimension to this differential update model. */

    if (!vp8_rac_get(c)) {
        d = vp8_rac_get_uint(c, 4) + 0;
    } else if (!vp8_rac_get(c)) {
        d = vp8_rac_get_uint(c, 4) + 16;
    } else if (!vp8_rac_get(c)) {
        d = vp8_rac_get_uint(c, 5) + 32;
    } else {
        d = vp8_rac_get_uint(c, 7);
        if (d >= 65)
            d = (d << 1) - 65 + vp8_rac_get(c);
        d += 64;
        av_assert2(d < FF_ARRAY_ELEMS(inv_map_table));
    }

    return p <= 128 ? 1 + inv_recenter_nonneg(inv_map_table[d], p - 1) :
                    255 - inv_recenter_nonneg(inv_map_table[d], 255 - p);
}

static int read_colorspace_details(AVCodecContext *avctx)
{
    static const enum AVColorSpace colorspaces[8] = {
        AVCOL_SPC_UNSPECIFIED, AVCOL_SPC_BT470BG, AVCOL_SPC_BT709, AVCOL_SPC_SMPTE170M,
        AVCOL_SPC_SMPTE240M, AVCOL_SPC_BT2020_NCL, AVCOL_SPC_RESERVED, AVCOL_SPC_RGB,
    };
    VP9Context *s = avctx->priv_data;
    int bits = avctx->profile <= 1 ? 0 : 1 + get_bits1(&s->gb); // 0:8, 1:10, 2:12

    s->bpp_index = bits;
    s->s.h.bpp = 8 + bits * 2;
    s->bytesperpixel = (7 + s->s.h.bpp) >> 3;
    avctx->colorspace = colorspaces[get_bits(&s->gb, 3)];
    if (avctx->colorspace == AVCOL_SPC_RGB) { // RGB = profile 1
        static const enum AVPixelFormat pix_fmt_rgb[3] = {
            AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12
        };
        s->ss_h = s->ss_v = 0;
        avctx->color_range = AVCOL_RANGE_JPEG;
        s->pix_fmt = pix_fmt_rgb[bits];
        if (avctx->profile & 1) {
            if (get_bits1(&s->gb)) {
                av_log(avctx, AV_LOG_ERROR, "Reserved bit set in RGB\n");
                return AVERROR_INVALIDDATA;
            }
        } else {
            av_log(avctx, AV_LOG_ERROR, "RGB not supported in profile %d\n",
                   avctx->profile);
            return AVERROR_INVALIDDATA;
        }
    } else {
        static const enum AVPixelFormat pix_fmt_for_ss[3][2 /* v */][2 /* h */] = {
            { { AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P },
              { AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV420P } },
            { { AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV422P10 },
              { AV_PIX_FMT_YUV440P10, AV_PIX_FMT_YUV420P10 } },
            { { AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12 },
              { AV_PIX_FMT_YUV440P12, AV_PIX_FMT_YUV420P12 } }
        };
        avctx->color_range = get_bits1(&s->gb) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
        if (avctx->profile & 1) {
            s->ss_h = get_bits1(&s->gb);
            s->ss_v = get_bits1(&s->gb);
            s->pix_fmt = pix_fmt_for_ss[bits][s->ss_v][s->ss_h];
            if (s->pix_fmt == AV_PIX_FMT_YUV420P) {
                av_log(avctx, AV_LOG_ERROR, "YUV 4:2:0 not supported in profile %d\n",
                       avctx->profile);
                return AVERROR_INVALIDDATA;
            } else if (get_bits1(&s->gb)) {
                av_log(avctx, AV_LOG_ERROR, "Profile %d color details reserved bit set\n",
                       avctx->profile);
                return AVERROR_INVALIDDATA;
            }
        } else {
            s->ss_h = s->ss_v = 1;
            s->pix_fmt = pix_fmt_for_ss[bits][1][1];
        }
    }

    return 0;
}

static int decode_frame_header(AVCodecContext *avctx,
                               const uint8_t *data, int size, int *ref)
{
    VP9Context *s = avctx->priv_data;
    int c, i, j, k, l, m, n, w, h, max, size2, ret, sharp;
    int last_invisible;
    const uint8_t *data2;

    /* general header */
    if ((ret = init_get_bits8(&s->gb, data, size)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize bitstream reader\n");
        return ret;
    }
    if (get_bits(&s->gb, 2) != 0x2) { // frame marker
        av_log(avctx, AV_LOG_ERROR, "Invalid frame marker\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->profile  = get_bits1(&s->gb);
    avctx->profile |= get_bits1(&s->gb) << 1;
    if (avctx->profile == 3) avctx->profile += get_bits1(&s->gb);
    if (avctx->profile > 3) {
        av_log(avctx, AV_LOG_ERROR, "Profile %d is not yet supported\n", avctx->profile);
        return AVERROR_INVALIDDATA;
    }
    s->s.h.profile = avctx->profile;
    if (get_bits1(&s->gb)) {
        *ref = get_bits(&s->gb, 3);
        return 0;
    }

    s->last_keyframe  = s->s.h.keyframe;
    s->s.h.keyframe   = !get_bits1(&s->gb);

    last_invisible   = s->s.h.invisible;
    s->s.h.invisible = !get_bits1(&s->gb);
    s->s.h.errorres  = get_bits1(&s->gb);
    s->s.h.use_last_frame_mvs = !s->s.h.errorres && !last_invisible;

    if (s->s.h.keyframe) {
        if (get_bits(&s->gb, 24) != VP9_SYNCCODE) { // synccode
            av_log(avctx, AV_LOG_ERROR, "Invalid sync code\n");
            return AVERROR_INVALIDDATA;
        }
        if ((ret = read_colorspace_details(avctx)) < 0)
            return ret;
        // for profile 1, here follows the subsampling bits
        s->s.h.refreshrefmask = 0xff;
        w = get_bits(&s->gb, 16) + 1;
        h = get_bits(&s->gb, 16) + 1;
        if (get_bits1(&s->gb)) // display size
            skip_bits(&s->gb, 32);
    } else {
        s->s.h.intraonly = s->s.h.invisible ? get_bits1(&s->gb) : 0;
        s->s.h.resetctx  = s->s.h.errorres ? 0 : get_bits(&s->gb, 2);
        if (s->s.h.intraonly) {
            if (get_bits(&s->gb, 24) != VP9_SYNCCODE) { // synccode
                av_log(avctx, AV_LOG_ERROR, "Invalid sync code\n");
                return AVERROR_INVALIDDATA;
            }
            if (avctx->profile >= 1) {
                if ((ret = read_colorspace_details(avctx)) < 0)
                    return ret;
            } else {
                s->ss_h = s->ss_v = 1;
                s->s.h.bpp = 8;
                s->bpp_index = 0;
                s->bytesperpixel = 1;
                s->pix_fmt = AV_PIX_FMT_YUV420P;
                avctx->colorspace = AVCOL_SPC_BT470BG;
                avctx->color_range = AVCOL_RANGE_MPEG;
            }
            s->s.h.refreshrefmask = get_bits(&s->gb, 8);
            w = get_bits(&s->gb, 16) + 1;
            h = get_bits(&s->gb, 16) + 1;
            if (get_bits1(&s->gb)) // display size
                skip_bits(&s->gb, 32);
        } else {
            s->s.h.refreshrefmask = get_bits(&s->gb, 8);
            s->s.h.refidx[0]      = get_bits(&s->gb, 3);
            s->s.h.signbias[0]    = get_bits1(&s->gb) && !s->s.h.errorres;
            s->s.h.refidx[1]      = get_bits(&s->gb, 3);
            s->s.h.signbias[1]    = get_bits1(&s->gb) && !s->s.h.errorres;
            s->s.h.refidx[2]      = get_bits(&s->gb, 3);
            s->s.h.signbias[2]    = get_bits1(&s->gb) && !s->s.h.errorres;
            if (!s->s.refs[s->s.h.refidx[0]].f->buf[0] ||
                !s->s.refs[s->s.h.refidx[1]].f->buf[0] ||
                !s->s.refs[s->s.h.refidx[2]].f->buf[0]) {
                av_log(avctx, AV_LOG_ERROR, "Not all references are available\n");
                return AVERROR_INVALIDDATA;
            }
            if (get_bits1(&s->gb)) {
                w = s->s.refs[s->s.h.refidx[0]].f->width;
                h = s->s.refs[s->s.h.refidx[0]].f->height;
            } else if (get_bits1(&s->gb)) {
                w = s->s.refs[s->s.h.refidx[1]].f->width;
                h = s->s.refs[s->s.h.refidx[1]].f->height;
            } else if (get_bits1(&s->gb)) {
                w = s->s.refs[s->s.h.refidx[2]].f->width;
                h = s->s.refs[s->s.h.refidx[2]].f->height;
            } else {
                w = get_bits(&s->gb, 16) + 1;
                h = get_bits(&s->gb, 16) + 1;
            }
            // Note that in this code, "CUR_FRAME" is actually before we
            // have formally allocated a frame, and thus actually represents
            // the _last_ frame
            s->s.h.use_last_frame_mvs &= s->s.frames[CUR_FRAME].tf.f->width == w &&
                                       s->s.frames[CUR_FRAME].tf.f->height == h;
            if (get_bits1(&s->gb)) // display size
                skip_bits(&s->gb, 32);
            s->s.h.highprecisionmvs = get_bits1(&s->gb);
            s->s.h.filtermode = get_bits1(&s->gb) ? FILTER_SWITCHABLE :
                                                  get_bits(&s->gb, 2);
            s->s.h.allowcompinter = s->s.h.signbias[0] != s->s.h.signbias[1] ||
                                  s->s.h.signbias[0] != s->s.h.signbias[2];
            if (s->s.h.allowcompinter) {
                if (s->s.h.signbias[0] == s->s.h.signbias[1]) {
                    s->s.h.fixcompref    = 2;
                    s->s.h.varcompref[0] = 0;
                    s->s.h.varcompref[1] = 1;
                } else if (s->s.h.signbias[0] == s->s.h.signbias[2]) {
                    s->s.h.fixcompref    = 1;
                    s->s.h.varcompref[0] = 0;
                    s->s.h.varcompref[1] = 2;
                } else {
                    s->s.h.fixcompref    = 0;
                    s->s.h.varcompref[0] = 1;
                    s->s.h.varcompref[1] = 2;
                }
            }
        }
    }
    s->s.h.refreshctx   = s->s.h.errorres ? 0 : get_bits1(&s->gb);
    s->s.h.parallelmode = s->s.h.errorres ? 1 : get_bits1(&s->gb);
    s->s.h.framectxid   = c = get_bits(&s->gb, 2);
    if (s->s.h.keyframe || s->s.h.intraonly)
        s->s.h.framectxid = 0; // BUG: libvpx ignores this field in keyframes

    /* loopfilter header data */
    if (s->s.h.keyframe || s->s.h.errorres || s->s.h.intraonly) {
        // reset loopfilter defaults
        s->s.h.lf_delta.ref[0] = 1;
        s->s.h.lf_delta.ref[1] = 0;
        s->s.h.lf_delta.ref[2] = -1;
        s->s.h.lf_delta.ref[3] = -1;
        s->s.h.lf_delta.mode[0] = 0;
        s->s.h.lf_delta.mode[1] = 0;
        memset(s->s.h.segmentation.feat, 0, sizeof(s->s.h.segmentation.feat));
    }
    s->s.h.filter.level = get_bits(&s->gb, 6);
    sharp = get_bits(&s->gb, 3);
    // if sharpness changed, reinit lim/mblim LUTs. if it didn't change, keep
    // the old cache values since they are still valid
    if (s->s.h.filter.sharpness != sharp) {
        for (i = 1; i <= 63; i++) {
            int limit = i;

            if (sharp > 0) {
                limit >>= (sharp + 3) >> 2;
                limit = FFMIN(limit, 9 - sharp);
            }
            limit = FFMAX(limit, 1);

            s->filter_lut.lim_lut[i] = limit;
            s->filter_lut.mblim_lut[i] = 2 * (i + 2) + limit;
        }
    }
    s->s.h.filter.sharpness = sharp;
    if ((s->s.h.lf_delta.enabled = get_bits1(&s->gb))) {
        if ((s->s.h.lf_delta.updated = get_bits1(&s->gb))) {
            for (i = 0; i < 4; i++)
                if (get_bits1(&s->gb))
                    s->s.h.lf_delta.ref[i] = get_sbits_inv(&s->gb, 6);
            for (i = 0; i < 2; i++)
                if (get_bits1(&s->gb))
                    s->s.h.lf_delta.mode[i] = get_sbits_inv(&s->gb, 6);
        }
    }

    /* quantization header data */
    s->s.h.yac_qi      = get_bits(&s->gb, 8);
    s->s.h.ydc_qdelta  = get_bits1(&s->gb) ? get_sbits_inv(&s->gb, 4) : 0;
    s->s.h.uvdc_qdelta = get_bits1(&s->gb) ? get_sbits_inv(&s->gb, 4) : 0;
    s->s.h.uvac_qdelta = get_bits1(&s->gb) ? get_sbits_inv(&s->gb, 4) : 0;
    s->s.h.lossless    = s->s.h.yac_qi == 0 && s->s.h.ydc_qdelta == 0 &&
                       s->s.h.uvdc_qdelta == 0 && s->s.h.uvac_qdelta == 0;
    if (s->s.h.lossless)
        avctx->properties |= FF_CODEC_PROPERTY_LOSSLESS;

    /* segmentation header info */
    if ((s->s.h.segmentation.enabled = get_bits1(&s->gb))) {
        if ((s->s.h.segmentation.update_map = get_bits1(&s->gb))) {
            for (i = 0; i < 7; i++)
                s->s.h.segmentation.prob[i] = get_bits1(&s->gb) ?
                                 get_bits(&s->gb, 8) : 255;
            if ((s->s.h.segmentation.temporal = get_bits1(&s->gb)))
                for (i = 0; i < 3; i++)
                    s->s.h.segmentation.pred_prob[i] = get_bits1(&s->gb) ?
                                         get_bits(&s->gb, 8) : 255;
        }

        if (get_bits1(&s->gb)) {
            s->s.h.segmentation.absolute_vals = get_bits1(&s->gb);
            for (i = 0; i < 8; i++) {
                if ((s->s.h.segmentation.feat[i].q_enabled = get_bits1(&s->gb)))
                    s->s.h.segmentation.feat[i].q_val = get_sbits_inv(&s->gb, 8);
                if ((s->s.h.segmentation.feat[i].lf_enabled = get_bits1(&s->gb)))
                    s->s.h.segmentation.feat[i].lf_val = get_sbits_inv(&s->gb, 6);
                if ((s->s.h.segmentation.feat[i].ref_enabled = get_bits1(&s->gb)))
                    s->s.h.segmentation.feat[i].ref_val = get_bits(&s->gb, 2);
                s->s.h.segmentation.feat[i].skip_enabled = get_bits1(&s->gb);
            }
        }
    }

    // set qmul[] based on Y/UV, AC/DC and segmentation Q idx deltas
    for (i = 0; i < (s->s.h.segmentation.enabled ? 8 : 1); i++) {
        int qyac, qydc, quvac, quvdc, lflvl, sh;

        if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[i].q_enabled) {
            if (s->s.h.segmentation.absolute_vals)
                qyac = av_clip_uintp2(s->s.h.segmentation.feat[i].q_val, 8);
            else
                qyac = av_clip_uintp2(s->s.h.yac_qi + s->s.h.segmentation.feat[i].q_val, 8);
        } else {
            qyac  = s->s.h.yac_qi;
        }
        qydc  = av_clip_uintp2(qyac + s->s.h.ydc_qdelta, 8);
        quvdc = av_clip_uintp2(qyac + s->s.h.uvdc_qdelta, 8);
        quvac = av_clip_uintp2(qyac + s->s.h.uvac_qdelta, 8);
        qyac  = av_clip_uintp2(qyac, 8);

        s->s.h.segmentation.feat[i].qmul[0][0] = ff_vp9_dc_qlookup[s->bpp_index][qydc];
        s->s.h.segmentation.feat[i].qmul[0][1] = ff_vp9_ac_qlookup[s->bpp_index][qyac];
        s->s.h.segmentation.feat[i].qmul[1][0] = ff_vp9_dc_qlookup[s->bpp_index][quvdc];
        s->s.h.segmentation.feat[i].qmul[1][1] = ff_vp9_ac_qlookup[s->bpp_index][quvac];

        sh = s->s.h.filter.level >= 32;
        if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[i].lf_enabled) {
            if (s->s.h.segmentation.absolute_vals)
                lflvl = av_clip_uintp2(s->s.h.segmentation.feat[i].lf_val, 6);
            else
                lflvl = av_clip_uintp2(s->s.h.filter.level + s->s.h.segmentation.feat[i].lf_val, 6);
        } else {
            lflvl  = s->s.h.filter.level;
        }
        if (s->s.h.lf_delta.enabled) {
            s->s.h.segmentation.feat[i].lflvl[0][0] =
            s->s.h.segmentation.feat[i].lflvl[0][1] =
                av_clip_uintp2(lflvl + (s->s.h.lf_delta.ref[0] * (1 << sh)), 6);
            for (j = 1; j < 4; j++) {
                s->s.h.segmentation.feat[i].lflvl[j][0] =
                    av_clip_uintp2(lflvl + ((s->s.h.lf_delta.ref[j] +
                                             s->s.h.lf_delta.mode[0]) * (1 << sh)), 6);
                s->s.h.segmentation.feat[i].lflvl[j][1] =
                    av_clip_uintp2(lflvl + ((s->s.h.lf_delta.ref[j] +
                                             s->s.h.lf_delta.mode[1]) * (1 << sh)), 6);
            }
        } else {
            memset(s->s.h.segmentation.feat[i].lflvl, lflvl,
                   sizeof(s->s.h.segmentation.feat[i].lflvl));
        }
    }

    /* tiling info */
    if ((ret = update_size(avctx, w, h)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize decoder for %dx%d @ %d\n",
               w, h, s->pix_fmt);
        return ret;
    }
    for (s->s.h.tiling.log2_tile_cols = 0;
         s->sb_cols > (64 << s->s.h.tiling.log2_tile_cols);
         s->s.h.tiling.log2_tile_cols++) ;
    for (max = 0; (s->sb_cols >> max) >= 4; max++) ;
    max = FFMAX(0, max - 1);
    while (max > s->s.h.tiling.log2_tile_cols) {
        if (get_bits1(&s->gb))
            s->s.h.tiling.log2_tile_cols++;
        else
            break;
    }
    s->s.h.tiling.log2_tile_rows = decode012(&s->gb);
    s->s.h.tiling.tile_rows = 1 << s->s.h.tiling.log2_tile_rows;
    if (s->s.h.tiling.tile_cols != (1 << s->s.h.tiling.log2_tile_cols)) {
        int n_range_coders;
        VP56RangeCoder *rc;

        if (s->td) {
            for (i = 0; i < s->active_tile_cols; i++)
                vp9_tile_data_free(&s->td[i]);
            av_freep(&s->td);
        }

        s->s.h.tiling.tile_cols = 1 << s->s.h.tiling.log2_tile_cols;
        s->active_tile_cols = avctx->active_thread_type == FF_THREAD_SLICE ?
                              s->s.h.tiling.tile_cols : 1;
        vp9_alloc_entries(avctx, s->sb_rows);
        if (avctx->active_thread_type == FF_THREAD_SLICE) {
            n_range_coders = 4; // max_tile_rows
        } else {
            n_range_coders = s->s.h.tiling.tile_cols;
        }
        s->td = av_calloc(s->active_tile_cols, sizeof(VP9TileData) +
                                 n_range_coders * sizeof(VP56RangeCoder));
        if (!s->td)
            return AVERROR(ENOMEM);
        rc = (VP56RangeCoder *) &s->td[s->active_tile_cols];
        for (i = 0; i < s->active_tile_cols; i++) {
            s->td[i].s = s;
            s->td[i].c_b = rc;
            rc += n_range_coders;
        }
    }

    /* check reference frames */
    if (!s->s.h.keyframe && !s->s.h.intraonly) {
        int valid_ref_frame = 0;
        for (i = 0; i < 3; i++) {
            AVFrame *ref = s->s.refs[s->s.h.refidx[i]].f;
            int refw = ref->width, refh = ref->height;

            if (ref->format != avctx->pix_fmt) {
                av_log(avctx, AV_LOG_ERROR,
                       "Ref pixfmt (%s) did not match current frame (%s)",
                       av_get_pix_fmt_name(ref->format),
                       av_get_pix_fmt_name(avctx->pix_fmt));
                return AVERROR_INVALIDDATA;
            } else if (refw == w && refh == h) {
                s->mvscale[i][0] = s->mvscale[i][1] = 0;
            } else {
                /* Check to make sure at least one of frames that */
                /* this frame references has valid dimensions     */
                if (w * 2 < refw || h * 2 < refh || w > 16 * refw || h > 16 * refh) {
                    av_log(avctx, AV_LOG_WARNING,
                           "Invalid ref frame dimensions %dx%d for frame size %dx%d\n",
                           refw, refh, w, h);
                    s->mvscale[i][0] = s->mvscale[i][1] = REF_INVALID_SCALE;
                    continue;
                }
                s->mvscale[i][0] = (refw << 14) / w;
                s->mvscale[i][1] = (refh << 14) / h;
                s->mvstep[i][0] = 16 * s->mvscale[i][0] >> 14;
                s->mvstep[i][1] = 16 * s->mvscale[i][1] >> 14;
            }
            valid_ref_frame++;
        }
        if (!valid_ref_frame) {
            av_log(avctx, AV_LOG_ERROR, "No valid reference frame is found, bitstream not supported\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if (s->s.h.keyframe || s->s.h.errorres || (s->s.h.intraonly && s->s.h.resetctx == 3)) {
        s->prob_ctx[0].p = s->prob_ctx[1].p = s->prob_ctx[2].p =
                           s->prob_ctx[3].p = ff_vp9_default_probs;
        memcpy(s->prob_ctx[0].coef, ff_vp9_default_coef_probs,
               sizeof(ff_vp9_default_coef_probs));
        memcpy(s->prob_ctx[1].coef, ff_vp9_default_coef_probs,
               sizeof(ff_vp9_default_coef_probs));
        memcpy(s->prob_ctx[2].coef, ff_vp9_default_coef_probs,
               sizeof(ff_vp9_default_coef_probs));
        memcpy(s->prob_ctx[3].coef, ff_vp9_default_coef_probs,
               sizeof(ff_vp9_default_coef_probs));
    } else if (s->s.h.intraonly && s->s.h.resetctx == 2) {
        s->prob_ctx[c].p = ff_vp9_default_probs;
        memcpy(s->prob_ctx[c].coef, ff_vp9_default_coef_probs,
               sizeof(ff_vp9_default_coef_probs));
    }

    // next 16 bits is size of the rest of the header (arith-coded)
    s->s.h.compressed_header_size = size2 = get_bits(&s->gb, 16);
    s->s.h.uncompressed_header_size = (get_bits_count(&s->gb) + 7) / 8;

    data2 = align_get_bits(&s->gb);
    if (size2 > size - (data2 - data)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid compressed header size\n");
        return AVERROR_INVALIDDATA;
    }
    ret = ff_vp56_init_range_decoder(&s->c, data2, size2);
    if (ret < 0)
        return ret;

    if (vp56_rac_get_prob_branchy(&s->c, 128)) { // marker bit
        av_log(avctx, AV_LOG_ERROR, "Marker bit was set\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < s->active_tile_cols; i++) {
        if (s->s.h.keyframe || s->s.h.intraonly) {
            memset(s->td[i].counts.coef, 0, sizeof(s->td[0].counts.coef));
            memset(s->td[i].counts.eob,  0, sizeof(s->td[0].counts.eob));
        } else {
            memset(&s->td[i].counts, 0, sizeof(s->td[0].counts));
        }
        s->td[i].nb_block_structure = 0;
    }

    /* FIXME is it faster to not copy here, but do it down in the fw updates
     * as explicit copies if the fw update is missing (and skip the copy upon
     * fw update)? */
    s->prob.p = s->prob_ctx[c].p;

    // txfm updates
    if (s->s.h.lossless) {
        s->s.h.txfmmode = TX_4X4;
    } else {
        s->s.h.txfmmode = vp8_rac_get_uint(&s->c, 2);
        if (s->s.h.txfmmode == 3)
            s->s.h.txfmmode += vp8_rac_get(&s->c);

        if (s->s.h.txfmmode == TX_SWITCHABLE) {
            for (i = 0; i < 2; i++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.tx8p[i] = update_prob(&s->c, s->prob.p.tx8p[i]);
            for (i = 0; i < 2; i++)
                for (j = 0; j < 2; j++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.tx16p[i][j] =
                            update_prob(&s->c, s->prob.p.tx16p[i][j]);
            for (i = 0; i < 2; i++)
                for (j = 0; j < 3; j++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.tx32p[i][j] =
                            update_prob(&s->c, s->prob.p.tx32p[i][j]);
        }
    }

    // coef updates
    for (i = 0; i < 4; i++) {
        uint8_t (*ref)[2][6][6][3] = s->prob_ctx[c].coef[i];
        if (vp8_rac_get(&s->c)) {
            for (j = 0; j < 2; j++)
                for (k = 0; k < 2; k++)
                    for (l = 0; l < 6; l++)
                        for (m = 0; m < 6; m++) {
                            uint8_t *p = s->prob.coef[i][j][k][l][m];
                            uint8_t *r = ref[j][k][l][m];
                            if (m >= 3 && l == 0) // dc only has 3 pt
                                break;
                            for (n = 0; n < 3; n++) {
                                if (vp56_rac_get_prob_branchy(&s->c, 252))
                                    p[n] = update_prob(&s->c, r[n]);
                                else
                                    p[n] = r[n];
                            }
                            memcpy(&p[3], ff_vp9_model_pareto8[p[2]], 8);
                        }
        } else {
            for (j = 0; j < 2; j++)
                for (k = 0; k < 2; k++)
                    for (l = 0; l < 6; l++)
                        for (m = 0; m < 6; m++) {
                            uint8_t *p = s->prob.coef[i][j][k][l][m];
                            uint8_t *r = ref[j][k][l][m];
                            if (m > 3 && l == 0) // dc only has 3 pt
                                break;
                            memcpy(p, r, 3);
                            memcpy(&p[3], ff_vp9_model_pareto8[p[2]], 8);
                        }
        }
        if (s->s.h.txfmmode == i)
            break;
    }

    // mode updates
    for (i = 0; i < 3; i++)
        if (vp56_rac_get_prob_branchy(&s->c, 252))
            s->prob.p.skip[i] = update_prob(&s->c, s->prob.p.skip[i]);
    if (!s->s.h.keyframe && !s->s.h.intraonly) {
        for (i = 0; i < 7; i++)
            for (j = 0; j < 3; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_mode[i][j] =
                        update_prob(&s->c, s->prob.p.mv_mode[i][j]);

        if (s->s.h.filtermode == FILTER_SWITCHABLE)
            for (i = 0; i < 4; i++)
                for (j = 0; j < 2; j++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.filter[i][j] =
                            update_prob(&s->c, s->prob.p.filter[i][j]);

        for (i = 0; i < 4; i++)
            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.intra[i] = update_prob(&s->c, s->prob.p.intra[i]);

        if (s->s.h.allowcompinter) {
            s->s.h.comppredmode = vp8_rac_get(&s->c);
            if (s->s.h.comppredmode)
                s->s.h.comppredmode += vp8_rac_get(&s->c);
            if (s->s.h.comppredmode == PRED_SWITCHABLE)
                for (i = 0; i < 5; i++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.comp[i] =
                            update_prob(&s->c, s->prob.p.comp[i]);
        } else {
            s->s.h.comppredmode = PRED_SINGLEREF;
        }

        if (s->s.h.comppredmode != PRED_COMPREF) {
            for (i = 0; i < 5; i++) {
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.single_ref[i][0] =
                        update_prob(&s->c, s->prob.p.single_ref[i][0]);
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.single_ref[i][1] =
                        update_prob(&s->c, s->prob.p.single_ref[i][1]);
            }
        }

        if (s->s.h.comppredmode != PRED_SINGLEREF) {
            for (i = 0; i < 5; i++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.comp_ref[i] =
                        update_prob(&s->c, s->prob.p.comp_ref[i]);
        }

        for (i = 0; i < 4; i++)
            for (j = 0; j < 9; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.y_mode[i][j] =
                        update_prob(&s->c, s->prob.p.y_mode[i][j]);

        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++)
                for (k = 0; k < 3; k++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.partition[3 - i][j][k] =
                            update_prob(&s->c,
                                        s->prob.p.partition[3 - i][j][k]);

        // mv fields don't use the update_prob subexp model for some reason
        for (i = 0; i < 3; i++)
            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.mv_joint[i] = (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

        for (i = 0; i < 2; i++) {
            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.mv_comp[i].sign =
                    (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            for (j = 0; j < 10; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].classes[j] =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.mv_comp[i].class0 =
                    (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            for (j = 0; j < 10; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].bits[j] =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;
        }

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++)
                for (k = 0; k < 3; k++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.mv_comp[i].class0_fp[j][k] =
                            (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            for (j = 0; j < 3; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].fp[j] =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;
        }

        if (s->s.h.highprecisionmvs) {
            for (i = 0; i < 2; i++) {
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].class0_hp =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].hp =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;
            }
        }
    }

    return (data2 - data) + size2;
}

static void decode_sb(VP9TileData *td, int row, int col, VP9Filter *lflvl,
                      ptrdiff_t yoff, ptrdiff_t uvoff, enum BlockLevel bl)
{
    const VP9Context *s = td->s;
    int c = ((s->above_partition_ctx[col] >> (3 - bl)) & 1) |
            (((td->left_partition_ctx[row & 0x7] >> (3 - bl)) & 1) << 1);
    const uint8_t *p = s->s.h.keyframe || s->s.h.intraonly ? ff_vp9_default_kf_partition_probs[bl][c] :
                                                     s->prob.p.partition[bl][c];
    enum BlockPartition bp;
    ptrdiff_t hbs = 4 >> bl;
    AVFrame *f = s->s.frames[CUR_FRAME].tf.f;
    ptrdiff_t y_stride = f->linesize[0], uv_stride = f->linesize[1];
    int bytesperpixel = s->bytesperpixel;

    if (bl == BL_8X8) {
        bp = vp8_rac_get_tree(td->c, ff_vp9_partition_tree, p);
        ff_vp9_decode_block(td, row, col, lflvl, yoff, uvoff, bl, bp);
    } else if (col + hbs < s->cols) { // FIXME why not <=?
        if (row + hbs < s->rows) { // FIXME why not <=?
            bp = vp8_rac_get_tree(td->c, ff_vp9_partition_tree, p);
            switch (bp) {
            case PARTITION_NONE:
                ff_vp9_decode_block(td, row, col, lflvl, yoff, uvoff, bl, bp);
                break;
            case PARTITION_H:
                ff_vp9_decode_block(td, row, col, lflvl, yoff, uvoff, bl, bp);
                yoff  += hbs * 8 * y_stride;
                uvoff += hbs * 8 * uv_stride >> s->ss_v;
                ff_vp9_decode_block(td, row + hbs, col, lflvl, yoff, uvoff, bl, bp);
                break;
            case PARTITION_V:
                ff_vp9_decode_block(td, row, col, lflvl, yoff, uvoff, bl, bp);
                yoff  += hbs * 8 * bytesperpixel;
                uvoff += hbs * 8 * bytesperpixel >> s->ss_h;
                ff_vp9_decode_block(td, row, col + hbs, lflvl, yoff, uvoff, bl, bp);
                break;
            case PARTITION_SPLIT:
                decode_sb(td, row, col, lflvl, yoff, uvoff, bl + 1);
                decode_sb(td, row, col + hbs, lflvl,
                          yoff + 8 * hbs * bytesperpixel,
                          uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
                yoff  += hbs * 8 * y_stride;
                uvoff += hbs * 8 * uv_stride >> s->ss_v;
                decode_sb(td, row + hbs, col, lflvl, yoff, uvoff, bl + 1);
                decode_sb(td, row + hbs, col + hbs, lflvl,
                          yoff + 8 * hbs * bytesperpixel,
                          uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
                break;
            default:
                av_assert0(0);
            }
        } else if (vp56_rac_get_prob_branchy(td->c, p[1])) {
            bp = PARTITION_SPLIT;
            decode_sb(td, row, col, lflvl, yoff, uvoff, bl + 1);
            decode_sb(td, row, col + hbs, lflvl,
                      yoff + 8 * hbs * bytesperpixel,
                      uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
        } else {
            bp = PARTITION_H;
            ff_vp9_decode_block(td, row, col, lflvl, yoff, uvoff, bl, bp);
        }
    } else if (row + hbs < s->rows) { // FIXME why not <=?
        if (vp56_rac_get_prob_branchy(td->c, p[2])) {
            bp = PARTITION_SPLIT;
            decode_sb(td, row, col, lflvl, yoff, uvoff, bl + 1);
            yoff  += hbs * 8 * y_stride;
            uvoff += hbs * 8 * uv_stride >> s->ss_v;
            decode_sb(td, row + hbs, col, lflvl, yoff, uvoff, bl + 1);
        } else {
            bp = PARTITION_V;
            ff_vp9_decode_block(td, row, col, lflvl, yoff, uvoff, bl, bp);
        }
    } else {
        bp = PARTITION_SPLIT;
        decode_sb(td, row, col, lflvl, yoff, uvoff, bl + 1);
    }
    td->counts.partition[bl][c][bp]++;
}

static void decode_sb_mem(VP9TileData *td, int row, int col, VP9Filter *lflvl,
                          ptrdiff_t yoff, ptrdiff_t uvoff, enum BlockLevel bl)
{
    const VP9Context *s = td->s;
    VP9Block *b = td->b;
    ptrdiff_t hbs = 4 >> bl;
    AVFrame *f = s->s.frames[CUR_FRAME].tf.f;
    ptrdiff_t y_stride = f->linesize[0], uv_stride = f->linesize[1];
    int bytesperpixel = s->bytesperpixel;

    if (bl == BL_8X8) {
        av_assert2(b->bl == BL_8X8);
        ff_vp9_decode_block(td, row, col, lflvl, yoff, uvoff, b->bl, b->bp);
    } else if (td->b->bl == bl) {
        ff_vp9_decode_block(td, row, col, lflvl, yoff, uvoff, b->bl, b->bp);
        if (b->bp == PARTITION_H && row + hbs < s->rows) {
            yoff  += hbs * 8 * y_stride;
            uvoff += hbs * 8 * uv_stride >> s->ss_v;
            ff_vp9_decode_block(td, row + hbs, col, lflvl, yoff, uvoff, b->bl, b->bp);
        } else if (b->bp == PARTITION_V && col + hbs < s->cols) {
            yoff  += hbs * 8 * bytesperpixel;
            uvoff += hbs * 8 * bytesperpixel >> s->ss_h;
            ff_vp9_decode_block(td, row, col + hbs, lflvl, yoff, uvoff, b->bl, b->bp);
        }
    } else {
        decode_sb_mem(td, row, col, lflvl, yoff, uvoff, bl + 1);
        if (col + hbs < s->cols) { // FIXME why not <=?
            if (row + hbs < s->rows) {
                decode_sb_mem(td, row, col + hbs, lflvl, yoff + 8 * hbs * bytesperpixel,
                              uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
                yoff  += hbs * 8 * y_stride;
                uvoff += hbs * 8 * uv_stride >> s->ss_v;
                decode_sb_mem(td, row + hbs, col, lflvl, yoff, uvoff, bl + 1);
                decode_sb_mem(td, row + hbs, col + hbs, lflvl,
                              yoff + 8 * hbs * bytesperpixel,
                              uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
            } else {
                yoff  += hbs * 8 * bytesperpixel;
                uvoff += hbs * 8 * bytesperpixel >> s->ss_h;
                decode_sb_mem(td, row, col + hbs, lflvl, yoff, uvoff, bl + 1);
            }
        } else if (row + hbs < s->rows) {
            yoff  += hbs * 8 * y_stride;
            uvoff += hbs * 8 * uv_stride >> s->ss_v;
            decode_sb_mem(td, row + hbs, col, lflvl, yoff, uvoff, bl + 1);
        }
    }
}

static void set_tile_offset(int *start, int *end, int idx, int log2_n, int n)
{
    int sb_start = ( idx      * n) >> log2_n;
    int sb_end   = ((idx + 1) * n) >> log2_n;
    *start = FFMIN(sb_start, n) << 3;
    *end   = FFMIN(sb_end,   n) << 3;
}

static void free_buffers(VP9Context *s)
{
    int i;

    av_freep(&s->intra_pred_data[0]);
    for (i = 0; i < s->active_tile_cols; i++)
        vp9_tile_data_free(&s->td[i]);
}

static av_cold int vp9_decode_free(AVCodecContext *avctx)
{
    VP9Context *s = avctx->priv_data;
    int i;

    for (i = 0; i < 3; i++) {
        vp9_frame_unref(avctx, &s->s.frames[i]);
        av_frame_free(&s->s.frames[i].tf.f);
    }
    av_buffer_pool_uninit(&s->frame_extradata_pool);
    for (i = 0; i < 8; i++) {
        ff_thread_release_buffer(avctx, &s->s.refs[i]);
        av_frame_free(&s->s.refs[i].f);
        ff_thread_release_buffer(avctx, &s->next_refs[i]);
        av_frame_free(&s->next_refs[i].f);
    }

    free_buffers(s);
#if HAVE_THREADS
    av_freep(&s->entries);
    ff_pthread_free(s, vp9_context_offsets);
#endif
    av_freep(&s->td);
    return 0;
}

static int decode_tiles(AVCodecContext *avctx,
                        const uint8_t *data, int size)
{
    VP9Context *s = avctx->priv_data;
    VP9TileData *td = &s->td[0];
    int row, col, tile_row, tile_col, ret;
    int bytesperpixel;
    int tile_row_start, tile_row_end, tile_col_start, tile_col_end;
    AVFrame *f;
    ptrdiff_t yoff, uvoff, ls_y, ls_uv;

    f = s->s.frames[CUR_FRAME].tf.f;
    ls_y = f->linesize[0];
    ls_uv =f->linesize[1];
    bytesperpixel = s->bytesperpixel;

    yoff = uvoff = 0;
    for (tile_row = 0; tile_row < s->s.h.tiling.tile_rows; tile_row++) {
        set_tile_offset(&tile_row_start, &tile_row_end,
                        tile_row, s->s.h.tiling.log2_tile_rows, s->sb_rows);

        for (tile_col = 0; tile_col < s->s.h.tiling.tile_cols; tile_col++) {
            int64_t tile_size;

            if (tile_col == s->s.h.tiling.tile_cols - 1 &&
                tile_row == s->s.h.tiling.tile_rows - 1) {
                tile_size = size;
            } else {
                tile_size = AV_RB32(data);
                data += 4;
                size -= 4;
            }
            if (tile_size > size) {
                ff_thread_report_progress(&s->s.frames[CUR_FRAME].tf, INT_MAX, 0);
                return AVERROR_INVALIDDATA;
            }
            ret = ff_vp56_init_range_decoder(&td->c_b[tile_col], data, tile_size);
            if (ret < 0)
                return ret;
            if (vp56_rac_get_prob_branchy(&td->c_b[tile_col], 128)) { // marker bit
                ff_thread_report_progress(&s->s.frames[CUR_FRAME].tf, INT_MAX, 0);
                return AVERROR_INVALIDDATA;
            }
            data += tile_size;
            size -= tile_size;
        }

        for (row = tile_row_start; row < tile_row_end;
             row += 8, yoff += ls_y * 64, uvoff += ls_uv * 64 >> s->ss_v) {
            VP9Filter *lflvl_ptr = s->lflvl;
            ptrdiff_t yoff2 = yoff, uvoff2 = uvoff;

            for (tile_col = 0; tile_col < s->s.h.tiling.tile_cols; tile_col++) {
                set_tile_offset(&tile_col_start, &tile_col_end,
                                tile_col, s->s.h.tiling.log2_tile_cols, s->sb_cols);
                td->tile_col_start = tile_col_start;
                if (s->pass != 2) {
                    memset(td->left_partition_ctx, 0, 8);
                    memset(td->left_skip_ctx, 0, 8);
                    if (s->s.h.keyframe || s->s.h.intraonly) {
                        memset(td->left_mode_ctx, DC_PRED, 16);
                    } else {
                        memset(td->left_mode_ctx, NEARESTMV, 8);
                    }
                    memset(td->left_y_nnz_ctx, 0, 16);
                    memset(td->left_uv_nnz_ctx, 0, 32);
                    memset(td->left_segpred_ctx, 0, 8);

                    td->c = &td->c_b[tile_col];
                }

                for (col = tile_col_start;
                     col < tile_col_end;
                     col += 8, yoff2 += 64 * bytesperpixel,
                     uvoff2 += 64 * bytesperpixel >> s->ss_h, lflvl_ptr++) {
                    // FIXME integrate with lf code (i.e. zero after each
                    // use, similar to invtxfm coefficients, or similar)
                    if (s->pass != 1) {
                        memset(lflvl_ptr->mask, 0, sizeof(lflvl_ptr->mask));
                    }

                    if (s->pass == 2) {
                        decode_sb_mem(td, row, col, lflvl_ptr,
                                      yoff2, uvoff2, BL_64X64);
                    } else {
                        if (vpX_rac_is_end(td->c)) {
                            return AVERROR_INVALIDDATA;
                        }
                        decode_sb(td, row, col, lflvl_ptr,
                                  yoff2, uvoff2, BL_64X64);
                    }
                }
            }

            if (s->pass == 1)
                continue;

            // backup pre-loopfilter reconstruction data for intra
            // prediction of next row of sb64s
            if (row + 8 < s->rows) {
                memcpy(s->intra_pred_data[0],
                       f->data[0] + yoff + 63 * ls_y,
                       8 * s->cols * bytesperpixel);
                memcpy(s->intra_pred_data[1],
                       f->data[1] + uvoff + ((64 >> s->ss_v) - 1) * ls_uv,
                       8 * s->cols * bytesperpixel >> s->ss_h);
                memcpy(s->intra_pred_data[2],
                       f->data[2] + uvoff + ((64 >> s->ss_v) - 1) * ls_uv,
                       8 * s->cols * bytesperpixel >> s->ss_h);
            }

            // loopfilter one row
            if (s->s.h.filter.level) {
                yoff2 = yoff;
                uvoff2 = uvoff;
                lflvl_ptr = s->lflvl;
                for (col = 0; col < s->cols;
                     col += 8, yoff2 += 64 * bytesperpixel,
                     uvoff2 += 64 * bytesperpixel >> s->ss_h, lflvl_ptr++) {
                    ff_vp9_loopfilter_sb(avctx, lflvl_ptr, row, col,
                                         yoff2, uvoff2);
                }
            }

            // FIXME maybe we can make this more finegrained by running the
            // loopfilter per-block instead of after each sbrow
            // In fact that would also make intra pred left preparation easier?
            ff_thread_report_progress(&s->s.frames[CUR_FRAME].tf, row >> 3, 0);
        }
    }
    return 0;
}

#if HAVE_THREADS
static av_always_inline
int decode_tiles_mt(AVCodecContext *avctx, void *tdata, int jobnr,
                              int threadnr)
{
    VP9Context *s = avctx->priv_data;
    VP9TileData *td = &s->td[jobnr];
    ptrdiff_t uvoff, yoff, ls_y, ls_uv;
    int bytesperpixel = s->bytesperpixel, row, col, tile_row;
    unsigned tile_cols_len;
    int tile_row_start, tile_row_end, tile_col_start, tile_col_end;
    VP9Filter *lflvl_ptr_base;
    AVFrame *f;

    f = s->s.frames[CUR_FRAME].tf.f;
    ls_y = f->linesize[0];
    ls_uv =f->linesize[1];

    set_tile_offset(&tile_col_start, &tile_col_end,
                    jobnr, s->s.h.tiling.log2_tile_cols, s->sb_cols);
    td->tile_col_start  = tile_col_start;
    uvoff = (64 * bytesperpixel >> s->ss_h)*(tile_col_start >> 3);
    yoff = (64 * bytesperpixel)*(tile_col_start >> 3);
    lflvl_ptr_base = s->lflvl+(tile_col_start >> 3);

    for (tile_row = 0; tile_row < s->s.h.tiling.tile_rows; tile_row++) {
        set_tile_offset(&tile_row_start, &tile_row_end,
                        tile_row, s->s.h.tiling.log2_tile_rows, s->sb_rows);

        td->c = &td->c_b[tile_row];
        for (row = tile_row_start; row < tile_row_end;
             row += 8, yoff += ls_y * 64, uvoff += ls_uv * 64 >> s->ss_v) {
            ptrdiff_t yoff2 = yoff, uvoff2 = uvoff;
            VP9Filter *lflvl_ptr = lflvl_ptr_base+s->sb_cols*(row >> 3);

            memset(td->left_partition_ctx, 0, 8);
            memset(td->left_skip_ctx, 0, 8);
            if (s->s.h.keyframe || s->s.h.intraonly) {
                memset(td->left_mode_ctx, DC_PRED, 16);
            } else {
                memset(td->left_mode_ctx, NEARESTMV, 8);
            }
            memset(td->left_y_nnz_ctx, 0, 16);
            memset(td->left_uv_nnz_ctx, 0, 32);
            memset(td->left_segpred_ctx, 0, 8);

            for (col = tile_col_start;
                 col < tile_col_end;
                 col += 8, yoff2 += 64 * bytesperpixel,
                 uvoff2 += 64 * bytesperpixel >> s->ss_h, lflvl_ptr++) {
                // FIXME integrate with lf code (i.e. zero after each
                // use, similar to invtxfm coefficients, or similar)
                memset(lflvl_ptr->mask, 0, sizeof(lflvl_ptr->mask));
                decode_sb(td, row, col, lflvl_ptr,
                            yoff2, uvoff2, BL_64X64);
            }

            // backup pre-loopfilter reconstruction data for intra
            // prediction of next row of sb64s
            tile_cols_len = tile_col_end - tile_col_start;
            if (row + 8 < s->rows) {
                memcpy(s->intra_pred_data[0] + (tile_col_start * 8 * bytesperpixel),
                       f->data[0] + yoff + 63 * ls_y,
                       8 * tile_cols_len * bytesperpixel);
                memcpy(s->intra_pred_data[1] + (tile_col_start * 8 * bytesperpixel >> s->ss_h),
                       f->data[1] + uvoff + ((64 >> s->ss_v) - 1) * ls_uv,
                       8 * tile_cols_len * bytesperpixel >> s->ss_h);
                memcpy(s->intra_pred_data[2] + (tile_col_start * 8 * bytesperpixel >> s->ss_h),
                       f->data[2] + uvoff + ((64 >> s->ss_v) - 1) * ls_uv,
                       8 * tile_cols_len * bytesperpixel >> s->ss_h);
            }

            vp9_report_tile_progress(s, row >> 3, 1);
        }
    }
    return 0;
}

static av_always_inline
int loopfilter_proc(AVCodecContext *avctx)
{
    VP9Context *s = avctx->priv_data;
    ptrdiff_t uvoff, yoff, ls_y, ls_uv;
    VP9Filter *lflvl_ptr;
    int bytesperpixel = s->bytesperpixel, col, i;
    AVFrame *f;

    f = s->s.frames[CUR_FRAME].tf.f;
    ls_y = f->linesize[0];
    ls_uv =f->linesize[1];

    for (i = 0; i < s->sb_rows; i++) {
        vp9_await_tile_progress(s, i, s->s.h.tiling.tile_cols);

        if (s->s.h.filter.level) {
            yoff = (ls_y * 64)*i;
            uvoff =  (ls_uv * 64 >> s->ss_v)*i;
            lflvl_ptr = s->lflvl+s->sb_cols*i;
            for (col = 0; col < s->cols;
                 col += 8, yoff += 64 * bytesperpixel,
                 uvoff += 64 * bytesperpixel >> s->ss_h, lflvl_ptr++) {
                ff_vp9_loopfilter_sb(avctx, lflvl_ptr, i << 3, col,
                                     yoff, uvoff);
            }
        }
    }
    return 0;
}
#endif

static int vp9_export_enc_params(VP9Context *s, VP9Frame *frame)
{
    AVVideoEncParams *par;
    unsigned int tile, nb_blocks = 0;

    if (s->s.h.segmentation.enabled) {
        for (tile = 0; tile < s->active_tile_cols; tile++)
            nb_blocks += s->td[tile].nb_block_structure;
    }

    par = av_video_enc_params_create_side_data(frame->tf.f,
        AV_VIDEO_ENC_PARAMS_VP9, nb_blocks);
    if (!par)
        return AVERROR(ENOMEM);

    par->qp             = s->s.h.yac_qi;
    par->delta_qp[0][0] = s->s.h.ydc_qdelta;
    par->delta_qp[1][0] = s->s.h.uvdc_qdelta;
    par->delta_qp[2][0] = s->s.h.uvdc_qdelta;
    par->delta_qp[1][1] = s->s.h.uvac_qdelta;
    par->delta_qp[2][1] = s->s.h.uvac_qdelta;

    if (nb_blocks) {
        unsigned int block = 0;
        unsigned int tile, block_tile;

        for (tile = 0; tile < s->active_tile_cols; tile++) {
            VP9TileData *td = &s->td[tile];

            for (block_tile = 0; block_tile < td->nb_block_structure; block_tile++) {
                AVVideoBlockParams *b = av_video_enc_params_block(par, block++);
                unsigned int      row = td->block_structure[block_tile].row;
                unsigned int      col = td->block_structure[block_tile].col;
                uint8_t        seg_id = frame->segmentation_map[row * 8 * s->sb_cols + col];

                b->src_x = col * 8;
                b->src_y = row * 8;
                b->w     = 1 << (3 + td->block_structure[block_tile].block_size_idx_x);
                b->h     = 1 << (3 + td->block_structure[block_tile].block_size_idx_y);

                if (s->s.h.segmentation.feat[seg_id].q_enabled) {
                    b->delta_qp = s->s.h.segmentation.feat[seg_id].q_val;
                    if (s->s.h.segmentation.absolute_vals)
                        b->delta_qp -= par->qp;
                }
            }
        }
    }

    return 0;
}

static int vp9_decode_frame(AVCodecContext *avctx, void *frame,
                            int *got_frame, AVPacket *pkt)
{
    const uint8_t *data = pkt->data;
    int size = pkt->size;
    VP9Context *s = avctx->priv_data;
    int ret, i, j, ref;
    int retain_segmap_ref = s->s.frames[REF_FRAME_SEGMAP].segmentation_map &&
                            (!s->s.h.segmentation.enabled || !s->s.h.segmentation.update_map);
    AVFrame *f;

    if ((ret = decode_frame_header(avctx, data, size, &ref)) < 0) {
        return ret;
    } else if (ret == 0) {
        if (!s->s.refs[ref].f->buf[0]) {
            av_log(avctx, AV_LOG_ERROR, "Requested reference %d not available\n", ref);
            return AVERROR_INVALIDDATA;
        }
        if ((ret = av_frame_ref(frame, s->s.refs[ref].f)) < 0)
            return ret;
        ((AVFrame *)frame)->pts = pkt->pts;
        ((AVFrame *)frame)->pkt_dts = pkt->dts;
        for (i = 0; i < 8; i++) {
            if (s->next_refs[i].f->buf[0])
                ff_thread_release_buffer(avctx, &s->next_refs[i]);
            if (s->s.refs[i].f->buf[0] &&
                (ret = ff_thread_ref_frame(&s->next_refs[i], &s->s.refs[i])) < 0)
                return ret;
        }
        *got_frame = 1;
        return pkt->size;
    }
    data += ret;
    size -= ret;

    if (!retain_segmap_ref || s->s.h.keyframe || s->s.h.intraonly) {
        if (s->s.frames[REF_FRAME_SEGMAP].tf.f->buf[0])
            vp9_frame_unref(avctx, &s->s.frames[REF_FRAME_SEGMAP]);
        if (!s->s.h.keyframe && !s->s.h.intraonly && !s->s.h.errorres && s->s.frames[CUR_FRAME].tf.f->buf[0] &&
            (ret = vp9_frame_ref(avctx, &s->s.frames[REF_FRAME_SEGMAP], &s->s.frames[CUR_FRAME])) < 0)
            return ret;
    }
    if (s->s.frames[REF_FRAME_MVPAIR].tf.f->buf[0])
        vp9_frame_unref(avctx, &s->s.frames[REF_FRAME_MVPAIR]);
    if (!s->s.h.intraonly && !s->s.h.keyframe && !s->s.h.errorres && s->s.frames[CUR_FRAME].tf.f->buf[0] &&
        (ret = vp9_frame_ref(avctx, &s->s.frames[REF_FRAME_MVPAIR], &s->s.frames[CUR_FRAME])) < 0)
        return ret;
    if (s->s.frames[CUR_FRAME].tf.f->buf[0])
        vp9_frame_unref(avctx, &s->s.frames[CUR_FRAME]);
    if ((ret = vp9_frame_alloc(avctx, &s->s.frames[CUR_FRAME])) < 0)
        return ret;
    f = s->s.frames[CUR_FRAME].tf.f;
    f->key_frame = s->s.h.keyframe;
    f->pict_type = (s->s.h.keyframe || s->s.h.intraonly) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    if (s->s.frames[REF_FRAME_SEGMAP].tf.f->buf[0] &&
        (s->s.frames[REF_FRAME_MVPAIR].tf.f->width  != s->s.frames[CUR_FRAME].tf.f->width ||
         s->s.frames[REF_FRAME_MVPAIR].tf.f->height != s->s.frames[CUR_FRAME].tf.f->height)) {
        vp9_frame_unref(avctx, &s->s.frames[REF_FRAME_SEGMAP]);
    }

    // ref frame setup
    for (i = 0; i < 8; i++) {
        if (s->next_refs[i].f->buf[0])
            ff_thread_release_buffer(avctx, &s->next_refs[i]);
        if (s->s.h.refreshrefmask & (1 << i)) {
            ret = ff_thread_ref_frame(&s->next_refs[i], &s->s.frames[CUR_FRAME].tf);
        } else if (s->s.refs[i].f->buf[0]) {
            ret = ff_thread_ref_frame(&s->next_refs[i], &s->s.refs[i]);
        }
        if (ret < 0)
            return ret;
    }

    if (avctx->hwaccel) {
        ret = avctx->hwaccel->start_frame(avctx, NULL, 0);
        if (ret < 0)
            return ret;
        ret = avctx->hwaccel->decode_slice(avctx, pkt->data, pkt->size);
        if (ret < 0)
            return ret;
        ret = avctx->hwaccel->end_frame(avctx);
        if (ret < 0)
            return ret;
        goto finish;
    }

    // main tile decode loop
    memset(s->above_partition_ctx, 0, s->cols);
    memset(s->above_skip_ctx, 0, s->cols);
    if (s->s.h.keyframe || s->s.h.intraonly) {
        memset(s->above_mode_ctx, DC_PRED, s->cols * 2);
    } else {
        memset(s->above_mode_ctx, NEARESTMV, s->cols);
    }
    memset(s->above_y_nnz_ctx, 0, s->sb_cols * 16);
    memset(s->above_uv_nnz_ctx[0], 0, s->sb_cols * 16 >> s->ss_h);
    memset(s->above_uv_nnz_ctx[1], 0, s->sb_cols * 16 >> s->ss_h);
    memset(s->above_segpred_ctx, 0, s->cols);
    s->pass = s->s.frames[CUR_FRAME].uses_2pass =
        avctx->active_thread_type == FF_THREAD_FRAME && s->s.h.refreshctx && !s->s.h.parallelmode;
    if ((ret = update_block_buffers(avctx)) < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to allocate block buffers\n");
        return ret;
    }
    if (s->s.h.refreshctx && s->s.h.parallelmode) {
        int j, k, l, m;

        for (i = 0; i < 4; i++) {
            for (j = 0; j < 2; j++)
                for (k = 0; k < 2; k++)
                    for (l = 0; l < 6; l++)
                        for (m = 0; m < 6; m++)
                            memcpy(s->prob_ctx[s->s.h.framectxid].coef[i][j][k][l][m],
                                   s->prob.coef[i][j][k][l][m], 3);
            if (s->s.h.txfmmode == i)
                break;
        }
        s->prob_ctx[s->s.h.framectxid].p = s->prob.p;
        ff_thread_finish_setup(avctx);
    } else if (!s->s.h.refreshctx) {
        ff_thread_finish_setup(avctx);
    }

#if HAVE_THREADS
    if (avctx->active_thread_type & FF_THREAD_SLICE) {
        for (i = 0; i < s->sb_rows; i++)
            atomic_store(&s->entries[i], 0);
    }
#endif

    do {
        for (i = 0; i < s->active_tile_cols; i++) {
            s->td[i].b = s->td[i].b_base;
            s->td[i].block = s->td[i].block_base;
            s->td[i].uvblock[0] = s->td[i].uvblock_base[0];
            s->td[i].uvblock[1] = s->td[i].uvblock_base[1];
            s->td[i].eob = s->td[i].eob_base;
            s->td[i].uveob[0] = s->td[i].uveob_base[0];
            s->td[i].uveob[1] = s->td[i].uveob_base[1];
            s->td[i].error_info = 0;
        }

#if HAVE_THREADS
        if (avctx->active_thread_type == FF_THREAD_SLICE) {
            int tile_row, tile_col;

            av_assert1(!s->pass);

            for (tile_row = 0; tile_row < s->s.h.tiling.tile_rows; tile_row++) {
                for (tile_col = 0; tile_col < s->s.h.tiling.tile_cols; tile_col++) {
                    int64_t tile_size;

                    if (tile_col == s->s.h.tiling.tile_cols - 1 &&
                        tile_row == s->s.h.tiling.tile_rows - 1) {
                        tile_size = size;
                    } else {
                        tile_size = AV_RB32(data);
                        data += 4;
                        size -= 4;
                    }
                    if (tile_size > size)
                        return AVERROR_INVALIDDATA;
                    ret = ff_vp56_init_range_decoder(&s->td[tile_col].c_b[tile_row], data, tile_size);
                    if (ret < 0)
                        return ret;
                    if (vp56_rac_get_prob_branchy(&s->td[tile_col].c_b[tile_row], 128)) // marker bit
                        return AVERROR_INVALIDDATA;
                    data += tile_size;
                    size -= tile_size;
                }
            }

            ff_slice_thread_execute_with_mainfunc(avctx, decode_tiles_mt, loopfilter_proc, s->td, NULL, s->s.h.tiling.tile_cols);
        } else
#endif
        {
            ret = decode_tiles(avctx, data, size);
            if (ret < 0) {
                ff_thread_report_progress(&s->s.frames[CUR_FRAME].tf, INT_MAX, 0);
                return ret;
            }
        }

        // Sum all counts fields into td[0].counts for tile threading
        if (avctx->active_thread_type == FF_THREAD_SLICE)
            for (i = 1; i < s->s.h.tiling.tile_cols; i++)
                for (j = 0; j < sizeof(s->td[i].counts) / sizeof(unsigned); j++)
                    ((unsigned *)&s->td[0].counts)[j] += ((unsigned *)&s->td[i].counts)[j];

        if (s->pass < 2 && s->s.h.refreshctx && !s->s.h.parallelmode) {
            ff_vp9_adapt_probs(s);
            ff_thread_finish_setup(avctx);
        }
    } while (s->pass++ == 1);
    ff_thread_report_progress(&s->s.frames[CUR_FRAME].tf, INT_MAX, 0);

    if (s->td->error_info < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to decode tile data\n");
        s->td->error_info = 0;
        return AVERROR_INVALIDDATA;
    }
    if (avctx->export_side_data & AV_CODEC_EXPORT_DATA_VIDEO_ENC_PARAMS) {
        ret = vp9_export_enc_params(s, &s->s.frames[CUR_FRAME]);
        if (ret < 0)
            return ret;
    }

finish:
    // ref frame setup
    for (i = 0; i < 8; i++) {
        if (s->s.refs[i].f->buf[0])
            ff_thread_release_buffer(avctx, &s->s.refs[i]);
        if (s->next_refs[i].f->buf[0] &&
            (ret = ff_thread_ref_frame(&s->s.refs[i], &s->next_refs[i])) < 0)
            return ret;
    }

    if (!s->s.h.invisible) {
        if ((ret = av_frame_ref(frame, s->s.frames[CUR_FRAME].tf.f)) < 0)
            return ret;
        *got_frame = 1;
    }

    return pkt->size;
}

static void vp9_decode_flush(AVCodecContext *avctx)
{
    VP9Context *s = avctx->priv_data;
    int i;

    for (i = 0; i < 3; i++)
        vp9_frame_unref(avctx, &s->s.frames[i]);
    for (i = 0; i < 8; i++)
        ff_thread_release_buffer(avctx, &s->s.refs[i]);
}

static av_cold int vp9_decode_init(AVCodecContext *avctx)
{
    VP9Context *s = avctx->priv_data;
    int ret;

    s->last_bpp = 0;
    s->s.h.filter.sharpness = -1;

#if HAVE_THREADS
    if (avctx->active_thread_type & FF_THREAD_SLICE) {
        ret = ff_pthread_init(s, vp9_context_offsets);
        if (ret < 0)
            return ret;
    }
#endif

    for (int i = 0; i < 3; i++) {
        s->s.frames[i].tf.f = av_frame_alloc();
        if (!s->s.frames[i].tf.f)
            return AVERROR(ENOMEM);
    }
    for (int i = 0; i < 8; i++) {
        s->s.refs[i].f      = av_frame_alloc();
        s->next_refs[i].f   = av_frame_alloc();
        if (!s->s.refs[i].f || !s->next_refs[i].f)
            return AVERROR(ENOMEM);
    }
    return 0;
}

#if HAVE_THREADS
static int vp9_decode_update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    int i, ret;
    VP9Context *s = dst->priv_data, *ssrc = src->priv_data;

    for (i = 0; i < 3; i++) {
        if (s->s.frames[i].tf.f->buf[0])
            vp9_frame_unref(dst, &s->s.frames[i]);
        if (ssrc->s.frames[i].tf.f->buf[0]) {
            if ((ret = vp9_frame_ref(dst, &s->s.frames[i], &ssrc->s.frames[i])) < 0)
                return ret;
        }
    }
    for (i = 0; i < 8; i++) {
        if (s->s.refs[i].f->buf[0])
            ff_thread_release_buffer(dst, &s->s.refs[i]);
        if (ssrc->next_refs[i].f->buf[0]) {
            if ((ret = ff_thread_ref_frame(&s->s.refs[i], &ssrc->next_refs[i])) < 0)
                return ret;
        }
    }

    s->s.h.invisible = ssrc->s.h.invisible;
    s->s.h.keyframe = ssrc->s.h.keyframe;
    s->s.h.intraonly = ssrc->s.h.intraonly;
    s->ss_v = ssrc->ss_v;
    s->ss_h = ssrc->ss_h;
    s->s.h.segmentation.enabled = ssrc->s.h.segmentation.enabled;
    s->s.h.segmentation.update_map = ssrc->s.h.segmentation.update_map;
    s->s.h.segmentation.absolute_vals = ssrc->s.h.segmentation.absolute_vals;
    s->bytesperpixel = ssrc->bytesperpixel;
    s->gf_fmt = ssrc->gf_fmt;
    s->w = ssrc->w;
    s->h = ssrc->h;
    s->s.h.bpp = ssrc->s.h.bpp;
    s->bpp_index = ssrc->bpp_index;
    s->pix_fmt = ssrc->pix_fmt;
    memcpy(&s->prob_ctx, &ssrc->prob_ctx, sizeof(s->prob_ctx));
    memcpy(&s->s.h.lf_delta, &ssrc->s.h.lf_delta, sizeof(s->s.h.lf_delta));
    memcpy(&s->s.h.segmentation.feat, &ssrc->s.h.segmentation.feat,
           sizeof(s->s.h.segmentation.feat));

    return 0;
}
#endif

const AVCodec ff_vp9_decoder = {
    .name                  = "vp9",
    .long_name             = NULL_IF_CONFIG_SMALL("Google VP9"),
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_VP9,
    .priv_data_size        = sizeof(VP9Context),
    .init                  = vp9_decode_init,
    .close                 = vp9_decode_free,
    .decode                = vp9_decode_frame,
    .capabilities          = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP |
                             FF_CODEC_CAP_SLICE_THREAD_HAS_MF |
                             FF_CODEC_CAP_ALLOCATE_PROGRESS,
    .flush                 = vp9_decode_flush,
    .update_thread_context = ONLY_IF_THREADS_ENABLED(vp9_decode_update_thread_context),
    .profiles              = NULL_IF_CONFIG_SMALL(ff_vp9_profiles),
    .bsfs                  = "vp9_superframe_split",
    .hw_configs            = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_VP9_DXVA2_HWACCEL
                               HWACCEL_DXVA2(vp9),
#endif
#if CONFIG_VP9_D3D11VA_HWACCEL
                               HWACCEL_D3D11VA(vp9),
#endif
#if CONFIG_VP9_D3D11VA2_HWACCEL
                               HWACCEL_D3D11VA2(vp9),
#endif
#if CONFIG_VP9_NVDEC_HWACCEL
                               HWACCEL_NVDEC(vp9),
#endif
#if CONFIG_VP9_VAAPI_HWACCEL
                               HWACCEL_VAAPI(vp9),
#endif
#if CONFIG_VP9_VDPAU_HWACCEL
                               HWACCEL_VDPAU(vp9),
#endif
#if CONFIG_VP9_VIDEOTOOLBOX_HWACCEL
                               HWACCEL_VIDEOTOOLBOX(vp9),
#endif
                               NULL
                           },
};
