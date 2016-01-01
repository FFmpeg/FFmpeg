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
#include "internal.h"
#include "profiles.h"
#include "thread.h"
#include "videodsp.h"
#include "vp56.h"
#include "vp9.h"
#include "vp9data.h"
#include "vp9dsp.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"

#define VP9_SYNCCODE 0x498342

struct VP9Filter {
    uint8_t level[8 * 8];
    uint8_t /* bit=col */ mask[2 /* 0=y, 1=uv */][2 /* 0=col, 1=row */]
                              [8 /* rows */][4 /* 0=16, 1=8, 2=4, 3=inner4 */];
};

typedef struct VP9Block {
    uint8_t seg_id, intra, comp, ref[2], mode[4], uvmode, skip;
    enum FilterMode filter;
    VP56mv mv[4 /* b_idx */][2 /* ref */];
    enum BlockSize bs;
    enum TxfmMode tx, uvtx;
    enum BlockLevel bl;
    enum BlockPartition bp;
} VP9Block;

typedef struct VP9Context {
    VP9SharedContext s;

    VP9DSPContext dsp;
    VideoDSPContext vdsp;
    GetBitContext gb;
    VP56RangeCoder c;
    VP56RangeCoder *c_b;
    unsigned c_b_size;
    VP9Block *b_base, *b;
    int pass;
    int row, row7, col, col7;
    uint8_t *dst[3];
    ptrdiff_t y_stride, uv_stride;

    uint8_t ss_h, ss_v;
    uint8_t last_bpp, bpp, bpp_index, bytesperpixel;
    uint8_t last_keyframe;
    enum AVPixelFormat pix_fmt, last_fmt;
    ThreadFrame next_refs[8];

    struct {
        uint8_t lim_lut[64];
        uint8_t mblim_lut[64];
    } filter_lut;
    unsigned tile_row_start, tile_row_end, tile_col_start, tile_col_end;
    unsigned sb_cols, sb_rows, rows, cols;
    struct {
        prob_context p;
        uint8_t coef[4][2][2][6][6][3];
    } prob_ctx[4];
    struct {
        prob_context p;
        uint8_t coef[4][2][2][6][6][11];
    } prob;
    struct {
        unsigned y_mode[4][10];
        unsigned uv_mode[10][10];
        unsigned filter[4][3];
        unsigned mv_mode[7][4];
        unsigned intra[4][2];
        unsigned comp[5][2];
        unsigned single_ref[5][2][2];
        unsigned comp_ref[5][2];
        unsigned tx32p[2][4];
        unsigned tx16p[2][3];
        unsigned tx8p[2][2];
        unsigned skip[3][2];
        unsigned mv_joint[4];
        struct {
            unsigned sign[2];
            unsigned classes[11];
            unsigned class0[2];
            unsigned bits[10][2];
            unsigned class0_fp[2][4];
            unsigned fp[4];
            unsigned class0_hp[2];
            unsigned hp[2];
        } mv_comp[2];
        unsigned partition[4][4][4];
        unsigned coef[4][2][2][6][6][3];
        unsigned eob[4][2][2][6][6][2];
    } counts;

    // contextual (left/above) cache
    DECLARE_ALIGNED(16, uint8_t, left_y_nnz_ctx)[16];
    DECLARE_ALIGNED(16, uint8_t, left_mode_ctx)[16];
    DECLARE_ALIGNED(16, VP56mv, left_mv_ctx)[16][2];
    DECLARE_ALIGNED(16, uint8_t, left_uv_nnz_ctx)[2][16];
    DECLARE_ALIGNED(8, uint8_t, left_partition_ctx)[8];
    DECLARE_ALIGNED(8, uint8_t, left_skip_ctx)[8];
    DECLARE_ALIGNED(8, uint8_t, left_txfm_ctx)[8];
    DECLARE_ALIGNED(8, uint8_t, left_segpred_ctx)[8];
    DECLARE_ALIGNED(8, uint8_t, left_intra_ctx)[8];
    DECLARE_ALIGNED(8, uint8_t, left_comp_ctx)[8];
    DECLARE_ALIGNED(8, uint8_t, left_ref_ctx)[8];
    DECLARE_ALIGNED(8, uint8_t, left_filter_ctx)[8];
    uint8_t *above_partition_ctx;
    uint8_t *above_mode_ctx;
    // FIXME maybe merge some of the below in a flags field?
    uint8_t *above_y_nnz_ctx;
    uint8_t *above_uv_nnz_ctx[2];
    uint8_t *above_skip_ctx; // 1bit
    uint8_t *above_txfm_ctx; // 2bit
    uint8_t *above_segpred_ctx; // 1bit
    uint8_t *above_intra_ctx; // 1bit
    uint8_t *above_comp_ctx; // 1bit
    uint8_t *above_ref_ctx; // 2bit
    uint8_t *above_filter_ctx;
    VP56mv (*above_mv_ctx)[2];

    // whole-frame cache
    uint8_t *intra_pred_data[3];
    struct VP9Filter *lflvl;
    DECLARE_ALIGNED(32, uint8_t, edge_emu_buffer)[135 * 144 * 2];

    // block reconstruction intermediates
    int block_alloc_using_2pass;
    int16_t *block_base, *block, *uvblock_base[2], *uvblock[2];
    uint8_t *eob_base, *uveob_base[2], *eob, *uveob[2];
    struct { int x, y; } min_mv, max_mv;
    DECLARE_ALIGNED(32, uint8_t, tmp_y)[64 * 64 * 2];
    DECLARE_ALIGNED(32, uint8_t, tmp_uv)[2][64 * 64 * 2];
    uint16_t mvscale[3][2];
    uint8_t mvstep[3][2];
} VP9Context;

static const uint8_t bwh_tab[2][N_BS_SIZES][2] = {
    {
        { 16, 16 }, { 16, 8 }, { 8, 16 }, { 8, 8 }, { 8, 4 }, { 4, 8 },
        { 4, 4 }, { 4, 2 }, { 2, 4 }, { 2, 2 }, { 2, 1 }, { 1, 2 }, { 1, 1 },
    }, {
        { 8, 8 }, { 8, 4 }, { 4, 8 }, { 4, 4 }, { 4, 2 }, { 2, 4 },
        { 2, 2 }, { 2, 1 }, { 1, 2 }, { 1, 1 }, { 1, 1 }, { 1, 1 }, { 1, 1 },
    }
};

static void vp9_unref_frame(AVCodecContext *ctx, VP9Frame *f)
{
    ff_thread_release_buffer(ctx, &f->tf);
    av_buffer_unref(&f->extradata);
    av_buffer_unref(&f->hwaccel_priv_buf);
    f->segmentation_map = NULL;
    f->hwaccel_picture_private = NULL;
}

static int vp9_alloc_frame(AVCodecContext *ctx, VP9Frame *f)
{
    VP9Context *s = ctx->priv_data;
    int ret, sz;

    if ((ret = ff_thread_get_buffer(ctx, &f->tf, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;
    sz = 64 * s->sb_cols * s->sb_rows;
    if (!(f->extradata = av_buffer_allocz(sz * (1 + sizeof(struct VP9mvrefPair))))) {
        goto fail;
    }

    f->segmentation_map = f->extradata->data;
    f->mv = (struct VP9mvrefPair *) (f->extradata->data + sz);

    if (ctx->hwaccel) {
        const AVHWAccel *hwaccel = ctx->hwaccel;
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
    vp9_unref_frame(ctx, f);
    return AVERROR(ENOMEM);
}

static int vp9_ref_frame(AVCodecContext *ctx, VP9Frame *dst, VP9Frame *src)
{
    int res;

    if ((res = ff_thread_ref_frame(&dst->tf, &src->tf)) < 0) {
        return res;
    } else if (!(dst->extradata = av_buffer_ref(src->extradata))) {
        goto fail;
    }

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
    vp9_unref_frame(ctx, dst);
    return AVERROR(ENOMEM);
}

static int update_size(AVCodecContext *ctx, int w, int h)
{
#define HWACCEL_MAX (CONFIG_VP9_DXVA2_HWACCEL + CONFIG_VP9_D3D11VA_HWACCEL + CONFIG_VP9_VAAPI_HWACCEL)
    enum AVPixelFormat pix_fmts[HWACCEL_MAX + 2], *fmtp = pix_fmts;
    VP9Context *s = ctx->priv_data;
    uint8_t *p;
    int bytesperpixel = s->bytesperpixel, res;

    av_assert0(w > 0 && h > 0);

    if (s->intra_pred_data[0] && w == ctx->width && h == ctx->height && s->pix_fmt == s->last_fmt)
        return 0;

    if ((res = ff_set_dimensions(ctx, w, h)) < 0)
        return res;

    if (s->pix_fmt == AV_PIX_FMT_YUV420P) {
#if CONFIG_VP9_DXVA2_HWACCEL
        *fmtp++ = AV_PIX_FMT_DXVA2_VLD;
#endif
#if CONFIG_VP9_D3D11VA_HWACCEL
        *fmtp++ = AV_PIX_FMT_D3D11VA_VLD;
#endif
#if CONFIG_VP9_VAAPI_HWACCEL
        *fmtp++ = AV_PIX_FMT_VAAPI;
#endif
    }

    *fmtp++ = s->pix_fmt;
    *fmtp = AV_PIX_FMT_NONE;

    res = ff_thread_get_format(ctx, pix_fmts);
    if (res < 0)
        return res;

    ctx->pix_fmt = res;
    s->last_fmt  = s->pix_fmt;
    s->sb_cols   = (w + 63) >> 6;
    s->sb_rows   = (h + 63) >> 6;
    s->cols      = (w + 7) >> 3;
    s->rows      = (h + 7) >> 3;

#define assign(var, type, n) var = (type) p; p += s->sb_cols * (n) * sizeof(*var)
    av_freep(&s->intra_pred_data[0]);
    // FIXME we slightly over-allocate here for subsampled chroma, but a little
    // bit of padding shouldn't affect performance...
    p = av_malloc(s->sb_cols * (128 + 192 * bytesperpixel +
                                sizeof(*s->lflvl) + 16 * sizeof(*s->above_mv_ctx)));
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
    assign(s->lflvl,               struct VP9Filter *,     1);
#undef assign

    // these will be re-allocated a little later
    av_freep(&s->b_base);
    av_freep(&s->block_base);

    if (s->bpp != s->last_bpp) {
        ff_vp9dsp_init(&s->dsp, s->bpp, ctx->flags & AV_CODEC_FLAG_BITEXACT);
        ff_videodsp_init(&s->vdsp, s->bpp);
        s->last_bpp = s->bpp;
    }

    return 0;
}

static int update_block_buffers(AVCodecContext *ctx)
{
    VP9Context *s = ctx->priv_data;
    int chroma_blocks, chroma_eobs, bytesperpixel = s->bytesperpixel;

    if (s->b_base && s->block_base && s->block_alloc_using_2pass == s->s.frames[CUR_FRAME].uses_2pass)
        return 0;

    av_free(s->b_base);
    av_free(s->block_base);
    chroma_blocks = 64 * 64 >> (s->ss_h + s->ss_v);
    chroma_eobs   = 16 * 16 >> (s->ss_h + s->ss_v);
    if (s->s.frames[CUR_FRAME].uses_2pass) {
        int sbs = s->sb_cols * s->sb_rows;

        s->b_base = av_malloc_array(s->cols * s->rows, sizeof(VP9Block));
        s->block_base = av_mallocz(((64 * 64 + 2 * chroma_blocks) * bytesperpixel * sizeof(int16_t) +
                                    16 * 16 + 2 * chroma_eobs) * sbs);
        if (!s->b_base || !s->block_base)
            return AVERROR(ENOMEM);
        s->uvblock_base[0] = s->block_base + sbs * 64 * 64 * bytesperpixel;
        s->uvblock_base[1] = s->uvblock_base[0] + sbs * chroma_blocks * bytesperpixel;
        s->eob_base = (uint8_t *) (s->uvblock_base[1] + sbs * chroma_blocks * bytesperpixel);
        s->uveob_base[0] = s->eob_base + 16 * 16 * sbs;
        s->uveob_base[1] = s->uveob_base[0] + chroma_eobs * sbs;
    } else {
        s->b_base = av_malloc(sizeof(VP9Block));
        s->block_base = av_mallocz((64 * 64 + 2 * chroma_blocks) * bytesperpixel * sizeof(int16_t) +
                                   16 * 16 + 2 * chroma_eobs);
        if (!s->b_base || !s->block_base)
            return AVERROR(ENOMEM);
        s->uvblock_base[0] = s->block_base + 64 * 64 * bytesperpixel;
        s->uvblock_base[1] = s->uvblock_base[0] + chroma_blocks * bytesperpixel;
        s->eob_base = (uint8_t *) (s->uvblock_base[1] + chroma_blocks * bytesperpixel);
        s->uveob_base[0] = s->eob_base + 16 * 16;
        s->uveob_base[1] = s->uveob_base[0] + chroma_eobs;
    }
    s->block_alloc_using_2pass = s->s.frames[CUR_FRAME].uses_2pass;

    return 0;
}

// for some reason the sign bit is at the end, not the start, of a bit sequence
static av_always_inline int get_sbits_inv(GetBitContext *gb, int n)
{
    int v = get_bits(gb, n);
    return get_bits1(gb) ? -v : v;
}

static av_always_inline int inv_recenter_nonneg(int v, int m)
{
    return v > 2 * m ? v : v & 1 ? m - ((v + 1) >> 1) : m + (v >> 1);
}

// differential forward probability updates
static int update_prob(VP56RangeCoder *c, int p)
{
    static const int inv_map_table[255] = {
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
     * probability of any value can be expressed differentially as 1-A,255-A
     * where some part of this (absolute range) exists both in positive as
     * well as the negative part, whereas another part only exists in one
     * half. We're trying to code this shared part differentially, i.e.
     * times two where the value of the lowest bit specifies the sign, and
     * the single part is then coded on top of this. This absolute difference
     * then again has a value of [0,254], but a bigger value in this range
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

static int read_colorspace_details(AVCodecContext *ctx)
{
    static const enum AVColorSpace colorspaces[8] = {
        AVCOL_SPC_UNSPECIFIED, AVCOL_SPC_BT470BG, AVCOL_SPC_BT709, AVCOL_SPC_SMPTE170M,
        AVCOL_SPC_SMPTE240M, AVCOL_SPC_BT2020_NCL, AVCOL_SPC_RESERVED, AVCOL_SPC_RGB,
    };
    VP9Context *s = ctx->priv_data;
    int bits = ctx->profile <= 1 ? 0 : 1 + get_bits1(&s->gb); // 0:8, 1:10, 2:12

    s->bpp_index = bits;
    s->bpp = 8 + bits * 2;
    s->bytesperpixel = (7 + s->bpp) >> 3;
    ctx->colorspace = colorspaces[get_bits(&s->gb, 3)];
    if (ctx->colorspace == AVCOL_SPC_RGB) { // RGB = profile 1
        static const enum AVPixelFormat pix_fmt_rgb[3] = {
            AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12
        };
        s->ss_h = s->ss_v = 0;
        ctx->color_range = AVCOL_RANGE_JPEG;
        s->pix_fmt = pix_fmt_rgb[bits];
        if (ctx->profile & 1) {
            if (get_bits1(&s->gb)) {
                av_log(ctx, AV_LOG_ERROR, "Reserved bit set in RGB\n");
                return AVERROR_INVALIDDATA;
            }
        } else {
            av_log(ctx, AV_LOG_ERROR, "RGB not supported in profile %d\n",
                   ctx->profile);
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
        ctx->color_range = get_bits1(&s->gb) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
        if (ctx->profile & 1) {
            s->ss_h = get_bits1(&s->gb);
            s->ss_v = get_bits1(&s->gb);
            s->pix_fmt = pix_fmt_for_ss[bits][s->ss_v][s->ss_h];
            if (s->pix_fmt == AV_PIX_FMT_YUV420P) {
                av_log(ctx, AV_LOG_ERROR, "YUV 4:2:0 not supported in profile %d\n",
                       ctx->profile);
                return AVERROR_INVALIDDATA;
            } else if (get_bits1(&s->gb)) {
                av_log(ctx, AV_LOG_ERROR, "Profile %d color details reserved bit set\n",
                       ctx->profile);
                return AVERROR_INVALIDDATA;
            }
        } else {
            s->ss_h = s->ss_v = 1;
            s->pix_fmt = pix_fmt_for_ss[bits][1][1];
        }
    }

    return 0;
}

static int decode_frame_header(AVCodecContext *ctx,
                               const uint8_t *data, int size, int *ref)
{
    VP9Context *s = ctx->priv_data;
    int c, i, j, k, l, m, n, w, h, max, size2, res, sharp;
    int last_invisible;
    const uint8_t *data2;

    /* general header */
    if ((res = init_get_bits8(&s->gb, data, size)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to initialize bitstream reader\n");
        return res;
    }
    if (get_bits(&s->gb, 2) != 0x2) { // frame marker
        av_log(ctx, AV_LOG_ERROR, "Invalid frame marker\n");
        return AVERROR_INVALIDDATA;
    }
    ctx->profile  = get_bits1(&s->gb);
    ctx->profile |= get_bits1(&s->gb) << 1;
    if (ctx->profile == 3) ctx->profile += get_bits1(&s->gb);
    if (ctx->profile > 3) {
        av_log(ctx, AV_LOG_ERROR, "Profile %d is not yet supported\n", ctx->profile);
        return AVERROR_INVALIDDATA;
    }
    s->s.h.profile = ctx->profile;
    if (get_bits1(&s->gb)) {
        *ref = get_bits(&s->gb, 3);
        return 0;
    }
    s->last_keyframe  = s->s.h.keyframe;
    s->s.h.keyframe     = !get_bits1(&s->gb);
    last_invisible    = s->s.h.invisible;
    s->s.h.invisible    = !get_bits1(&s->gb);
    s->s.h.errorres     = get_bits1(&s->gb);
    s->s.h.use_last_frame_mvs = !s->s.h.errorres && !last_invisible;
    if (s->s.h.keyframe) {
        if (get_bits_long(&s->gb, 24) != VP9_SYNCCODE) { // synccode
            av_log(ctx, AV_LOG_ERROR, "Invalid sync code\n");
            return AVERROR_INVALIDDATA;
        }
        if ((res = read_colorspace_details(ctx)) < 0)
            return res;
        // for profile 1, here follows the subsampling bits
        s->s.h.refreshrefmask = 0xff;
        w = get_bits(&s->gb, 16) + 1;
        h = get_bits(&s->gb, 16) + 1;
        if (get_bits1(&s->gb)) // display size
            skip_bits(&s->gb, 32);
    } else {
        s->s.h.intraonly  = s->s.h.invisible ? get_bits1(&s->gb) : 0;
        s->s.h.resetctx   = s->s.h.errorres ? 0 : get_bits(&s->gb, 2);
        if (s->s.h.intraonly) {
            if (get_bits_long(&s->gb, 24) != VP9_SYNCCODE) { // synccode
                av_log(ctx, AV_LOG_ERROR, "Invalid sync code\n");
                return AVERROR_INVALIDDATA;
            }
            if (ctx->profile >= 1) {
                if ((res = read_colorspace_details(ctx)) < 0)
                    return res;
            } else {
                s->ss_h = s->ss_v = 1;
                s->bpp = 8;
                s->bpp_index = 0;
                s->bytesperpixel = 1;
                s->pix_fmt = AV_PIX_FMT_YUV420P;
                ctx->colorspace = AVCOL_SPC_BT470BG;
                ctx->color_range = AVCOL_RANGE_JPEG;
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
                av_log(ctx, AV_LOG_ERROR, "Not all references are available\n");
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
    if (s->s.h.filter.sharpness != sharp)
        memset(s->filter_lut.lim_lut, 0, sizeof(s->filter_lut.lim_lut));
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
        ctx->properties |= FF_CODEC_PROPERTY_LOSSLESS;

    /* segmentation header info */
    if ((s->s.h.segmentation.enabled = get_bits1(&s->gb))) {
        if ((s->s.h.segmentation.update_map = get_bits1(&s->gb))) {
            for (i = 0; i < 7; i++)
                s->s.h.segmentation.prob[i] = get_bits1(&s->gb) ?
                                 get_bits(&s->gb, 8) : 255;
            if ((s->s.h.segmentation.temporal = get_bits1(&s->gb))) {
                for (i = 0; i < 3; i++)
                    s->s.h.segmentation.pred_prob[i] = get_bits1(&s->gb) ?
                                         get_bits(&s->gb, 8) : 255;
            }
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

        s->s.h.segmentation.feat[i].qmul[0][0] = vp9_dc_qlookup[s->bpp_index][qydc];
        s->s.h.segmentation.feat[i].qmul[0][1] = vp9_ac_qlookup[s->bpp_index][qyac];
        s->s.h.segmentation.feat[i].qmul[1][0] = vp9_dc_qlookup[s->bpp_index][quvdc];
        s->s.h.segmentation.feat[i].qmul[1][1] = vp9_ac_qlookup[s->bpp_index][quvac];

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
                av_clip_uintp2(lflvl + (s->s.h.lf_delta.ref[0] << sh), 6);
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
    if ((res = update_size(ctx, w, h)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to initialize decoder for %dx%d @ %d\n",
               w, h, s->pix_fmt);
        return res;
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
        s->s.h.tiling.tile_cols = 1 << s->s.h.tiling.log2_tile_cols;
        s->c_b = av_fast_realloc(s->c_b, &s->c_b_size,
                                 sizeof(VP56RangeCoder) * s->s.h.tiling.tile_cols);
        if (!s->c_b) {
            av_log(ctx, AV_LOG_ERROR, "Ran out of memory during range coder init\n");
            return AVERROR(ENOMEM);
        }
    }

    /* check reference frames */
    if (!s->s.h.keyframe && !s->s.h.intraonly) {
        for (i = 0; i < 3; i++) {
            AVFrame *ref = s->s.refs[s->s.h.refidx[i]].f;
            int refw = ref->width, refh = ref->height;

            if (ref->format != ctx->pix_fmt) {
                av_log(ctx, AV_LOG_ERROR,
                       "Ref pixfmt (%s) did not match current frame (%s)",
                       av_get_pix_fmt_name(ref->format),
                       av_get_pix_fmt_name(ctx->pix_fmt));
                return AVERROR_INVALIDDATA;
            } else if (refw == w && refh == h) {
                s->mvscale[i][0] = s->mvscale[i][1] = 0;
            } else {
                if (w * 2 < refw || h * 2 < refh || w > 16 * refw || h > 16 * refh) {
                    av_log(ctx, AV_LOG_ERROR,
                           "Invalid ref frame dimensions %dx%d for frame size %dx%d\n",
                           refw, refh, w, h);
                    return AVERROR_INVALIDDATA;
                }
                s->mvscale[i][0] = (refw << 14) / w;
                s->mvscale[i][1] = (refh << 14) / h;
                s->mvstep[i][0] = 16 * s->mvscale[i][0] >> 14;
                s->mvstep[i][1] = 16 * s->mvscale[i][1] >> 14;
            }
        }
    }

    if (s->s.h.keyframe || s->s.h.errorres || (s->s.h.intraonly && s->s.h.resetctx == 3)) {
        s->prob_ctx[0].p = s->prob_ctx[1].p = s->prob_ctx[2].p =
                           s->prob_ctx[3].p = vp9_default_probs;
        memcpy(s->prob_ctx[0].coef, vp9_default_coef_probs,
               sizeof(vp9_default_coef_probs));
        memcpy(s->prob_ctx[1].coef, vp9_default_coef_probs,
               sizeof(vp9_default_coef_probs));
        memcpy(s->prob_ctx[2].coef, vp9_default_coef_probs,
               sizeof(vp9_default_coef_probs));
        memcpy(s->prob_ctx[3].coef, vp9_default_coef_probs,
               sizeof(vp9_default_coef_probs));
    } else if (s->s.h.intraonly && s->s.h.resetctx == 2) {
        s->prob_ctx[c].p = vp9_default_probs;
        memcpy(s->prob_ctx[c].coef, vp9_default_coef_probs,
               sizeof(vp9_default_coef_probs));
    }

    // next 16 bits is size of the rest of the header (arith-coded)
    s->s.h.compressed_header_size = size2 = get_bits(&s->gb, 16);
    s->s.h.uncompressed_header_size = (get_bits_count(&s->gb) + 7) / 8;

    data2 = align_get_bits(&s->gb);
    if (size2 > size - (data2 - data)) {
        av_log(ctx, AV_LOG_ERROR, "Invalid compressed header size\n");
        return AVERROR_INVALIDDATA;
    }
    ff_vp56_init_range_decoder(&s->c, data2, size2);
    if (vp56_rac_get_prob_branchy(&s->c, 128)) { // marker bit
        av_log(ctx, AV_LOG_ERROR, "Marker bit was set\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->s.h.keyframe || s->s.h.intraonly) {
        memset(s->counts.coef, 0, sizeof(s->counts.coef));
        memset(s->counts.eob,  0, sizeof(s->counts.eob));
    } else {
        memset(&s->counts, 0, sizeof(s->counts));
    }
    // FIXME is it faster to not copy here, but do it down in the fw updates
    // as explicit copies if the fw update is missing (and skip the copy upon
    // fw update)?
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
                                if (vp56_rac_get_prob_branchy(&s->c, 252)) {
                                    p[n] = update_prob(&s->c, r[n]);
                                } else {
                                    p[n] = r[n];
                                }
                            }
                            p[3] = 0;
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
                            p[3] = 0;
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
                            update_prob(&s->c, s->prob.p.partition[3 - i][j][k]);

        // mv fields don't use the update_prob subexp model for some reason
        for (i = 0; i < 3; i++)
            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.mv_joint[i] = (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

        for (i = 0; i < 2; i++) {
            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.mv_comp[i].sign = (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            for (j = 0; j < 10; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].classes[j] =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.mv_comp[i].class0 = (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

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

static av_always_inline void clamp_mv(VP56mv *dst, const VP56mv *src,
                                      VP9Context *s)
{
    dst->x = av_clip(src->x, s->min_mv.x, s->max_mv.x);
    dst->y = av_clip(src->y, s->min_mv.y, s->max_mv.y);
}

static void find_ref_mvs(VP9Context *s,
                         VP56mv *pmv, int ref, int z, int idx, int sb)
{
    static const int8_t mv_ref_blk_off[N_BS_SIZES][8][2] = {
        [BS_64x64] = {{  3, -1 }, { -1,  3 }, {  4, -1 }, { -1,  4 },
                      { -1, -1 }, {  0, -1 }, { -1,  0 }, {  6, -1 }},
        [BS_64x32] = {{  0, -1 }, { -1,  0 }, {  4, -1 }, { -1,  2 },
                      { -1, -1 }, {  0, -3 }, { -3,  0 }, {  2, -1 }},
        [BS_32x64] = {{ -1,  0 }, {  0, -1 }, { -1,  4 }, {  2, -1 },
                      { -1, -1 }, { -3,  0 }, {  0, -3 }, { -1,  2 }},
        [BS_32x32] = {{  1, -1 }, { -1,  1 }, {  2, -1 }, { -1,  2 },
                      { -1, -1 }, {  0, -3 }, { -3,  0 }, { -3, -3 }},
        [BS_32x16] = {{  0, -1 }, { -1,  0 }, {  2, -1 }, { -1, -1 },
                      { -1,  1 }, {  0, -3 }, { -3,  0 }, { -3, -3 }},
        [BS_16x32] = {{ -1,  0 }, {  0, -1 }, { -1,  2 }, { -1, -1 },
                      {  1, -1 }, { -3,  0 }, {  0, -3 }, { -3, -3 }},
        [BS_16x16] = {{  0, -1 }, { -1,  0 }, {  1, -1 }, { -1,  1 },
                      { -1, -1 }, {  0, -3 }, { -3,  0 }, { -3, -3 }},
        [BS_16x8]  = {{  0, -1 }, { -1,  0 }, {  1, -1 }, { -1, -1 },
                      {  0, -2 }, { -2,  0 }, { -2, -1 }, { -1, -2 }},
        [BS_8x16]  = {{ -1,  0 }, {  0, -1 }, { -1,  1 }, { -1, -1 },
                      { -2,  0 }, {  0, -2 }, { -1, -2 }, { -2, -1 }},
        [BS_8x8]   = {{  0, -1 }, { -1,  0 }, { -1, -1 }, {  0, -2 },
                      { -2,  0 }, { -1, -2 }, { -2, -1 }, { -2, -2 }},
        [BS_8x4]   = {{  0, -1 }, { -1,  0 }, { -1, -1 }, {  0, -2 },
                      { -2,  0 }, { -1, -2 }, { -2, -1 }, { -2, -2 }},
        [BS_4x8]   = {{  0, -1 }, { -1,  0 }, { -1, -1 }, {  0, -2 },
                      { -2,  0 }, { -1, -2 }, { -2, -1 }, { -2, -2 }},
        [BS_4x4]   = {{  0, -1 }, { -1,  0 }, { -1, -1 }, {  0, -2 },
                      { -2,  0 }, { -1, -2 }, { -2, -1 }, { -2, -2 }},
    };
    VP9Block *b = s->b;
    int row = s->row, col = s->col, row7 = s->row7;
    const int8_t (*p)[2] = mv_ref_blk_off[b->bs];
#define INVALID_MV 0x80008000U
    uint32_t mem = INVALID_MV, mem_sub8x8 = INVALID_MV;
    int i;

#define RETURN_DIRECT_MV(mv) \
    do { \
        uint32_t m = AV_RN32A(&mv); \
        if (!idx) { \
            AV_WN32A(pmv, m); \
            return; \
        } else if (mem == INVALID_MV) { \
            mem = m; \
        } else if (m != mem) { \
            AV_WN32A(pmv, m); \
            return; \
        } \
    } while (0)

    if (sb >= 0) {
        if (sb == 2 || sb == 1) {
            RETURN_DIRECT_MV(b->mv[0][z]);
        } else if (sb == 3) {
            RETURN_DIRECT_MV(b->mv[2][z]);
            RETURN_DIRECT_MV(b->mv[1][z]);
            RETURN_DIRECT_MV(b->mv[0][z]);
        }

#define RETURN_MV(mv) \
    do { \
        if (sb > 0) { \
            VP56mv tmp; \
            uint32_t m; \
            av_assert2(idx == 1); \
            av_assert2(mem != INVALID_MV); \
            if (mem_sub8x8 == INVALID_MV) { \
                clamp_mv(&tmp, &mv, s); \
                m = AV_RN32A(&tmp); \
                if (m != mem) { \
                    AV_WN32A(pmv, m); \
                    return; \
                } \
                mem_sub8x8 = AV_RN32A(&mv); \
            } else if (mem_sub8x8 != AV_RN32A(&mv)) { \
                clamp_mv(&tmp, &mv, s); \
                m = AV_RN32A(&tmp); \
                if (m != mem) { \
                    AV_WN32A(pmv, m); \
                } else { \
                    /* BUG I'm pretty sure this isn't the intention */ \
                    AV_WN32A(pmv, 0); \
                } \
                return; \
            } \
        } else { \
            uint32_t m = AV_RN32A(&mv); \
            if (!idx) { \
                clamp_mv(pmv, &mv, s); \
                return; \
            } else if (mem == INVALID_MV) { \
                mem = m; \
            } else if (m != mem) { \
                clamp_mv(pmv, &mv, s); \
                return; \
            } \
        } \
    } while (0)

        if (row > 0) {
            struct VP9mvrefPair *mv = &s->s.frames[CUR_FRAME].mv[(row - 1) * s->sb_cols * 8 + col];
            if (mv->ref[0] == ref) {
                RETURN_MV(s->above_mv_ctx[2 * col + (sb & 1)][0]);
            } else if (mv->ref[1] == ref) {
                RETURN_MV(s->above_mv_ctx[2 * col + (sb & 1)][1]);
            }
        }
        if (col > s->tile_col_start) {
            struct VP9mvrefPair *mv = &s->s.frames[CUR_FRAME].mv[row * s->sb_cols * 8 + col - 1];
            if (mv->ref[0] == ref) {
                RETURN_MV(s->left_mv_ctx[2 * row7 + (sb >> 1)][0]);
            } else if (mv->ref[1] == ref) {
                RETURN_MV(s->left_mv_ctx[2 * row7 + (sb >> 1)][1]);
            }
        }
        i = 2;
    } else {
        i = 0;
    }

    // previously coded MVs in this neighbourhood, using same reference frame
    for (; i < 8; i++) {
        int c = p[i][0] + col, r = p[i][1] + row;

        if (c >= s->tile_col_start && c < s->cols && r >= 0 && r < s->rows) {
            struct VP9mvrefPair *mv = &s->s.frames[CUR_FRAME].mv[r * s->sb_cols * 8 + c];

            if (mv->ref[0] == ref) {
                RETURN_MV(mv->mv[0]);
            } else if (mv->ref[1] == ref) {
                RETURN_MV(mv->mv[1]);
            }
        }
    }

    // MV at this position in previous frame, using same reference frame
    if (s->s.h.use_last_frame_mvs) {
        struct VP9mvrefPair *mv = &s->s.frames[REF_FRAME_MVPAIR].mv[row * s->sb_cols * 8 + col];

        if (!s->s.frames[REF_FRAME_MVPAIR].uses_2pass)
            ff_thread_await_progress(&s->s.frames[REF_FRAME_MVPAIR].tf, row >> 3, 0);
        if (mv->ref[0] == ref) {
            RETURN_MV(mv->mv[0]);
        } else if (mv->ref[1] == ref) {
            RETURN_MV(mv->mv[1]);
        }
    }

#define RETURN_SCALE_MV(mv, scale) \
    do { \
        if (scale) { \
            VP56mv mv_temp = { -mv.x, -mv.y }; \
            RETURN_MV(mv_temp); \
        } else { \
            RETURN_MV(mv); \
        } \
    } while (0)

    // previously coded MVs in this neighbourhood, using different reference frame
    for (i = 0; i < 8; i++) {
        int c = p[i][0] + col, r = p[i][1] + row;

        if (c >= s->tile_col_start && c < s->cols && r >= 0 && r < s->rows) {
            struct VP9mvrefPair *mv = &s->s.frames[CUR_FRAME].mv[r * s->sb_cols * 8 + c];

            if (mv->ref[0] != ref && mv->ref[0] >= 0) {
                RETURN_SCALE_MV(mv->mv[0], s->s.h.signbias[mv->ref[0]] != s->s.h.signbias[ref]);
            }
            if (mv->ref[1] != ref && mv->ref[1] >= 0 &&
                // BUG - libvpx has this condition regardless of whether
                // we used the first ref MV and pre-scaling
                AV_RN32A(&mv->mv[0]) != AV_RN32A(&mv->mv[1])) {
                RETURN_SCALE_MV(mv->mv[1], s->s.h.signbias[mv->ref[1]] != s->s.h.signbias[ref]);
            }
        }
    }

    // MV at this position in previous frame, using different reference frame
    if (s->s.h.use_last_frame_mvs) {
        struct VP9mvrefPair *mv = &s->s.frames[REF_FRAME_MVPAIR].mv[row * s->sb_cols * 8 + col];

        // no need to await_progress, because we already did that above
        if (mv->ref[0] != ref && mv->ref[0] >= 0) {
            RETURN_SCALE_MV(mv->mv[0], s->s.h.signbias[mv->ref[0]] != s->s.h.signbias[ref]);
        }
        if (mv->ref[1] != ref && mv->ref[1] >= 0 &&
            // BUG - libvpx has this condition regardless of whether
            // we used the first ref MV and pre-scaling
            AV_RN32A(&mv->mv[0]) != AV_RN32A(&mv->mv[1])) {
            RETURN_SCALE_MV(mv->mv[1], s->s.h.signbias[mv->ref[1]] != s->s.h.signbias[ref]);
        }
    }

    AV_ZERO32(pmv);
    clamp_mv(pmv, pmv, s);
#undef INVALID_MV
#undef RETURN_MV
#undef RETURN_SCALE_MV
}

static av_always_inline int read_mv_component(VP9Context *s, int idx, int hp)
{
    int bit, sign = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].sign);
    int n, c = vp8_rac_get_tree(&s->c, vp9_mv_class_tree,
                                s->prob.p.mv_comp[idx].classes);

    s->counts.mv_comp[idx].sign[sign]++;
    s->counts.mv_comp[idx].classes[c]++;
    if (c) {
        int m;

        for (n = 0, m = 0; m < c; m++) {
            bit = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].bits[m]);
            n |= bit << m;
            s->counts.mv_comp[idx].bits[m][bit]++;
        }
        n <<= 3;
        bit = vp8_rac_get_tree(&s->c, vp9_mv_fp_tree, s->prob.p.mv_comp[idx].fp);
        n |= bit << 1;
        s->counts.mv_comp[idx].fp[bit]++;
        if (hp) {
            bit = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].hp);
            s->counts.mv_comp[idx].hp[bit]++;
            n |= bit;
        } else {
            n |= 1;
            // bug in libvpx - we count for bw entropy purposes even if the
            // bit wasn't coded
            s->counts.mv_comp[idx].hp[1]++;
        }
        n += 8 << c;
    } else {
        n = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].class0);
        s->counts.mv_comp[idx].class0[n]++;
        bit = vp8_rac_get_tree(&s->c, vp9_mv_fp_tree,
                               s->prob.p.mv_comp[idx].class0_fp[n]);
        s->counts.mv_comp[idx].class0_fp[n][bit]++;
        n = (n << 3) | (bit << 1);
        if (hp) {
            bit = vp56_rac_get_prob(&s->c, s->prob.p.mv_comp[idx].class0_hp);
            s->counts.mv_comp[idx].class0_hp[bit]++;
            n |= bit;
        } else {
            n |= 1;
            // bug in libvpx - we count for bw entropy purposes even if the
            // bit wasn't coded
            s->counts.mv_comp[idx].class0_hp[1]++;
        }
    }

    return sign ? -(n + 1) : (n + 1);
}

static void fill_mv(VP9Context *s,
                    VP56mv *mv, int mode, int sb)
{
    VP9Block *b = s->b;

    if (mode == ZEROMV) {
        AV_ZERO64(mv);
    } else {
        int hp;

        // FIXME cache this value and reuse for other subblocks
        find_ref_mvs(s, &mv[0], b->ref[0], 0, mode == NEARMV,
                     mode == NEWMV ? -1 : sb);
        // FIXME maybe move this code into find_ref_mvs()
        if ((mode == NEWMV || sb == -1) &&
            !(hp = s->s.h.highprecisionmvs && abs(mv[0].x) < 64 && abs(mv[0].y) < 64)) {
            if (mv[0].y & 1) {
                if (mv[0].y < 0)
                    mv[0].y++;
                else
                    mv[0].y--;
            }
            if (mv[0].x & 1) {
                if (mv[0].x < 0)
                    mv[0].x++;
                else
                    mv[0].x--;
            }
        }
        if (mode == NEWMV) {
            enum MVJoint j = vp8_rac_get_tree(&s->c, vp9_mv_joint_tree,
                                              s->prob.p.mv_joint);

            s->counts.mv_joint[j]++;
            if (j >= MV_JOINT_V)
                mv[0].y += read_mv_component(s, 0, hp);
            if (j & 1)
                mv[0].x += read_mv_component(s, 1, hp);
        }

        if (b->comp) {
            // FIXME cache this value and reuse for other subblocks
            find_ref_mvs(s, &mv[1], b->ref[1], 1, mode == NEARMV,
                         mode == NEWMV ? -1 : sb);
            if ((mode == NEWMV || sb == -1) &&
                !(hp = s->s.h.highprecisionmvs && abs(mv[1].x) < 64 && abs(mv[1].y) < 64)) {
                if (mv[1].y & 1) {
                    if (mv[1].y < 0)
                        mv[1].y++;
                    else
                        mv[1].y--;
                }
                if (mv[1].x & 1) {
                    if (mv[1].x < 0)
                        mv[1].x++;
                    else
                        mv[1].x--;
                }
            }
            if (mode == NEWMV) {
                enum MVJoint j = vp8_rac_get_tree(&s->c, vp9_mv_joint_tree,
                                                  s->prob.p.mv_joint);

                s->counts.mv_joint[j]++;
                if (j >= MV_JOINT_V)
                    mv[1].y += read_mv_component(s, 0, hp);
                if (j & 1)
                    mv[1].x += read_mv_component(s, 1, hp);
            }
        }
    }
}

static av_always_inline void setctx_2d(uint8_t *ptr, int w, int h,
                                       ptrdiff_t stride, int v)
{
    switch (w) {
    case 1:
        do {
            *ptr = v;
            ptr += stride;
        } while (--h);
        break;
    case 2: {
        int v16 = v * 0x0101;
        do {
            AV_WN16A(ptr, v16);
            ptr += stride;
        } while (--h);
        break;
    }
    case 4: {
        uint32_t v32 = v * 0x01010101;
        do {
            AV_WN32A(ptr, v32);
            ptr += stride;
        } while (--h);
        break;
    }
    case 8: {
#if HAVE_FAST_64BIT
        uint64_t v64 = v * 0x0101010101010101ULL;
        do {
            AV_WN64A(ptr, v64);
            ptr += stride;
        } while (--h);
#else
        uint32_t v32 = v * 0x01010101;
        do {
            AV_WN32A(ptr,     v32);
            AV_WN32A(ptr + 4, v32);
            ptr += stride;
        } while (--h);
#endif
        break;
    }
    }
}

static void decode_mode(AVCodecContext *ctx)
{
    static const uint8_t left_ctx[N_BS_SIZES] = {
        0x0, 0x8, 0x0, 0x8, 0xc, 0x8, 0xc, 0xe, 0xc, 0xe, 0xf, 0xe, 0xf
    };
    static const uint8_t above_ctx[N_BS_SIZES] = {
        0x0, 0x0, 0x8, 0x8, 0x8, 0xc, 0xc, 0xc, 0xe, 0xe, 0xe, 0xf, 0xf
    };
    static const uint8_t max_tx_for_bl_bp[N_BS_SIZES] = {
        TX_32X32, TX_32X32, TX_32X32, TX_32X32, TX_16X16, TX_16X16,
        TX_16X16, TX_8X8, TX_8X8, TX_8X8, TX_4X4, TX_4X4, TX_4X4
    };
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    int row = s->row, col = s->col, row7 = s->row7;
    enum TxfmMode max_tx = max_tx_for_bl_bp[b->bs];
    int bw4 = bwh_tab[1][b->bs][0], w4 = FFMIN(s->cols - col, bw4);
    int bh4 = bwh_tab[1][b->bs][1], h4 = FFMIN(s->rows - row, bh4), y;
    int have_a = row > 0, have_l = col > s->tile_col_start;
    int vref, filter_id;

    if (!s->s.h.segmentation.enabled) {
        b->seg_id = 0;
    } else if (s->s.h.keyframe || s->s.h.intraonly) {
        b->seg_id = !s->s.h.segmentation.update_map ? 0 :
                    vp8_rac_get_tree(&s->c, vp9_segmentation_tree, s->s.h.segmentation.prob);
    } else if (!s->s.h.segmentation.update_map ||
               (s->s.h.segmentation.temporal &&
                vp56_rac_get_prob_branchy(&s->c,
                    s->s.h.segmentation.pred_prob[s->above_segpred_ctx[col] +
                                    s->left_segpred_ctx[row7]]))) {
        if (!s->s.h.errorres && s->s.frames[REF_FRAME_SEGMAP].segmentation_map) {
            int pred = 8, x;
            uint8_t *refsegmap = s->s.frames[REF_FRAME_SEGMAP].segmentation_map;

            if (!s->s.frames[REF_FRAME_SEGMAP].uses_2pass)
                ff_thread_await_progress(&s->s.frames[REF_FRAME_SEGMAP].tf, row >> 3, 0);
            for (y = 0; y < h4; y++) {
                int idx_base = (y + row) * 8 * s->sb_cols + col;
                for (x = 0; x < w4; x++)
                    pred = FFMIN(pred, refsegmap[idx_base + x]);
            }
            av_assert1(pred < 8);
            b->seg_id = pred;
        } else {
            b->seg_id = 0;
        }

        memset(&s->above_segpred_ctx[col], 1, w4);
        memset(&s->left_segpred_ctx[row7], 1, h4);
    } else {
        b->seg_id = vp8_rac_get_tree(&s->c, vp9_segmentation_tree,
                                     s->s.h.segmentation.prob);

        memset(&s->above_segpred_ctx[col], 0, w4);
        memset(&s->left_segpred_ctx[row7], 0, h4);
    }
    if (s->s.h.segmentation.enabled &&
        (s->s.h.segmentation.update_map || s->s.h.keyframe || s->s.h.intraonly)) {
        setctx_2d(&s->s.frames[CUR_FRAME].segmentation_map[row * 8 * s->sb_cols + col],
                  bw4, bh4, 8 * s->sb_cols, b->seg_id);
    }

    b->skip = s->s.h.segmentation.enabled &&
        s->s.h.segmentation.feat[b->seg_id].skip_enabled;
    if (!b->skip) {
        int c = s->left_skip_ctx[row7] + s->above_skip_ctx[col];
        b->skip = vp56_rac_get_prob(&s->c, s->prob.p.skip[c]);
        s->counts.skip[c][b->skip]++;
    }

    if (s->s.h.keyframe || s->s.h.intraonly) {
        b->intra = 1;
    } else if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[b->seg_id].ref_enabled) {
        b->intra = !s->s.h.segmentation.feat[b->seg_id].ref_val;
    } else {
        int c, bit;

        if (have_a && have_l) {
            c = s->above_intra_ctx[col] + s->left_intra_ctx[row7];
            c += (c == 2);
        } else {
            c = have_a ? 2 * s->above_intra_ctx[col] :
                have_l ? 2 * s->left_intra_ctx[row7] : 0;
        }
        bit = vp56_rac_get_prob(&s->c, s->prob.p.intra[c]);
        s->counts.intra[c][bit]++;
        b->intra = !bit;
    }

    if ((b->intra || !b->skip) && s->s.h.txfmmode == TX_SWITCHABLE) {
        int c;
        if (have_a) {
            if (have_l) {
                c = (s->above_skip_ctx[col] ? max_tx :
                     s->above_txfm_ctx[col]) +
                    (s->left_skip_ctx[row7] ? max_tx :
                     s->left_txfm_ctx[row7]) > max_tx;
            } else {
                c = s->above_skip_ctx[col] ? 1 :
                    (s->above_txfm_ctx[col] * 2 > max_tx);
            }
        } else if (have_l) {
            c = s->left_skip_ctx[row7] ? 1 :
                (s->left_txfm_ctx[row7] * 2 > max_tx);
        } else {
            c = 1;
        }
        switch (max_tx) {
        case TX_32X32:
            b->tx = vp56_rac_get_prob(&s->c, s->prob.p.tx32p[c][0]);
            if (b->tx) {
                b->tx += vp56_rac_get_prob(&s->c, s->prob.p.tx32p[c][1]);
                if (b->tx == 2)
                    b->tx += vp56_rac_get_prob(&s->c, s->prob.p.tx32p[c][2]);
            }
            s->counts.tx32p[c][b->tx]++;
            break;
        case TX_16X16:
            b->tx = vp56_rac_get_prob(&s->c, s->prob.p.tx16p[c][0]);
            if (b->tx)
                b->tx += vp56_rac_get_prob(&s->c, s->prob.p.tx16p[c][1]);
            s->counts.tx16p[c][b->tx]++;
            break;
        case TX_8X8:
            b->tx = vp56_rac_get_prob(&s->c, s->prob.p.tx8p[c]);
            s->counts.tx8p[c][b->tx]++;
            break;
        case TX_4X4:
            b->tx = TX_4X4;
            break;
        }
    } else {
        b->tx = FFMIN(max_tx, s->s.h.txfmmode);
    }

    if (s->s.h.keyframe || s->s.h.intraonly) {
        uint8_t *a = &s->above_mode_ctx[col * 2];
        uint8_t *l = &s->left_mode_ctx[(row7) << 1];

        b->comp = 0;
        if (b->bs > BS_8x8) {
            // FIXME the memory storage intermediates here aren't really
            // necessary, they're just there to make the code slightly
            // simpler for now
            b->mode[0] = a[0] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                    vp9_default_kf_ymode_probs[a[0]][l[0]]);
            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                 vp9_default_kf_ymode_probs[a[1]][b->mode[0]]);
                l[0] = a[1] = b->mode[1];
            } else {
                l[0] = a[1] = b->mode[1] = b->mode[0];
            }
            if (b->bs != BS_4x8) {
                b->mode[2] = a[0] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                        vp9_default_kf_ymode_probs[a[0]][l[1]]);
                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                  vp9_default_kf_ymode_probs[a[1]][b->mode[2]]);
                    l[1] = a[1] = b->mode[3];
                } else {
                    l[1] = a[1] = b->mode[3] = b->mode[2];
                }
            } else {
                b->mode[2] = b->mode[0];
                l[1] = a[1] = b->mode[3] = b->mode[1];
            }
        } else {
            b->mode[0] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                          vp9_default_kf_ymode_probs[*a][*l]);
            b->mode[3] = b->mode[2] = b->mode[1] = b->mode[0];
            // FIXME this can probably be optimized
            memset(a, b->mode[0], bwh_tab[0][b->bs][0]);
            memset(l, b->mode[0], bwh_tab[0][b->bs][1]);
        }
        b->uvmode = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                     vp9_default_kf_uvmode_probs[b->mode[3]]);
    } else if (b->intra) {
        b->comp = 0;
        if (b->bs > BS_8x8) {
            b->mode[0] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                          s->prob.p.y_mode[0]);
            s->counts.y_mode[0][b->mode[0]]++;
            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                              s->prob.p.y_mode[0]);
                s->counts.y_mode[0][b->mode[1]]++;
            } else {
                b->mode[1] = b->mode[0];
            }
            if (b->bs != BS_4x8) {
                b->mode[2] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                              s->prob.p.y_mode[0]);
                s->counts.y_mode[0][b->mode[2]]++;
                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                                  s->prob.p.y_mode[0]);
                    s->counts.y_mode[0][b->mode[3]]++;
                } else {
                    b->mode[3] = b->mode[2];
                }
            } else {
                b->mode[2] = b->mode[0];
                b->mode[3] = b->mode[1];
            }
        } else {
            static const uint8_t size_group[10] = {
                3, 3, 3, 3, 2, 2, 2, 1, 1, 1
            };
            int sz = size_group[b->bs];

            b->mode[0] = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                          s->prob.p.y_mode[sz]);
            b->mode[1] = b->mode[2] = b->mode[3] = b->mode[0];
            s->counts.y_mode[sz][b->mode[3]]++;
        }
        b->uvmode = vp8_rac_get_tree(&s->c, vp9_intramode_tree,
                                     s->prob.p.uv_mode[b->mode[3]]);
        s->counts.uv_mode[b->mode[3]][b->uvmode]++;
    } else {
        static const uint8_t inter_mode_ctx_lut[14][14] = {
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5 },
            { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 2, 1, 3 },
            { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 2, 1, 3 },
            { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 1, 1, 0, 3 },
            { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 3, 3, 4 },
        };

        if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[b->seg_id].ref_enabled) {
            av_assert2(s->s.h.segmentation.feat[b->seg_id].ref_val != 0);
            b->comp = 0;
            b->ref[0] = s->s.h.segmentation.feat[b->seg_id].ref_val - 1;
        } else {
            // read comp_pred flag
            if (s->s.h.comppredmode != PRED_SWITCHABLE) {
                b->comp = s->s.h.comppredmode == PRED_COMPREF;
            } else {
                int c;

                // FIXME add intra as ref=0xff (or -1) to make these easier?
                if (have_a) {
                    if (have_l) {
                        if (s->above_comp_ctx[col] && s->left_comp_ctx[row7]) {
                            c = 4;
                        } else if (s->above_comp_ctx[col]) {
                            c = 2 + (s->left_intra_ctx[row7] ||
                                     s->left_ref_ctx[row7] == s->s.h.fixcompref);
                        } else if (s->left_comp_ctx[row7]) {
                            c = 2 + (s->above_intra_ctx[col] ||
                                     s->above_ref_ctx[col] == s->s.h.fixcompref);
                        } else {
                            c = (!s->above_intra_ctx[col] &&
                                 s->above_ref_ctx[col] == s->s.h.fixcompref) ^
                            (!s->left_intra_ctx[row7] &&
                             s->left_ref_ctx[row & 7] == s->s.h.fixcompref);
                        }
                    } else {
                        c = s->above_comp_ctx[col] ? 3 :
                        (!s->above_intra_ctx[col] && s->above_ref_ctx[col] == s->s.h.fixcompref);
                    }
                } else if (have_l) {
                    c = s->left_comp_ctx[row7] ? 3 :
                    (!s->left_intra_ctx[row7] && s->left_ref_ctx[row7] == s->s.h.fixcompref);
                } else {
                    c = 1;
                }
                b->comp = vp56_rac_get_prob(&s->c, s->prob.p.comp[c]);
                s->counts.comp[c][b->comp]++;
            }

            // read actual references
            // FIXME probably cache a few variables here to prevent repetitive
            // memory accesses below
            if (b->comp) /* two references */ {
                int fix_idx = s->s.h.signbias[s->s.h.fixcompref], var_idx = !fix_idx, c, bit;

                b->ref[fix_idx] = s->s.h.fixcompref;
                // FIXME can this codeblob be replaced by some sort of LUT?
                if (have_a) {
                    if (have_l) {
                        if (s->above_intra_ctx[col]) {
                            if (s->left_intra_ctx[row7]) {
                                c = 2;
                            } else {
                                c = 1 + 2 * (s->left_ref_ctx[row7] != s->s.h.varcompref[1]);
                            }
                        } else if (s->left_intra_ctx[row7]) {
                            c = 1 + 2 * (s->above_ref_ctx[col] != s->s.h.varcompref[1]);
                        } else {
                            int refl = s->left_ref_ctx[row7], refa = s->above_ref_ctx[col];

                            if (refl == refa && refa == s->s.h.varcompref[1]) {
                                c = 0;
                            } else if (!s->left_comp_ctx[row7] && !s->above_comp_ctx[col]) {
                                if ((refa == s->s.h.fixcompref && refl == s->s.h.varcompref[0]) ||
                                    (refl == s->s.h.fixcompref && refa == s->s.h.varcompref[0])) {
                                    c = 4;
                                } else {
                                    c = (refa == refl) ? 3 : 1;
                                }
                            } else if (!s->left_comp_ctx[row7]) {
                                if (refa == s->s.h.varcompref[1] && refl != s->s.h.varcompref[1]) {
                                    c = 1;
                                } else {
                                    c = (refl == s->s.h.varcompref[1] &&
                                         refa != s->s.h.varcompref[1]) ? 2 : 4;
                                }
                            } else if (!s->above_comp_ctx[col]) {
                                if (refl == s->s.h.varcompref[1] && refa != s->s.h.varcompref[1]) {
                                    c = 1;
                                } else {
                                    c = (refa == s->s.h.varcompref[1] &&
                                         refl != s->s.h.varcompref[1]) ? 2 : 4;
                                }
                            } else {
                                c = (refl == refa) ? 4 : 2;
                            }
                        }
                    } else {
                        if (s->above_intra_ctx[col]) {
                            c = 2;
                        } else if (s->above_comp_ctx[col]) {
                            c = 4 * (s->above_ref_ctx[col] != s->s.h.varcompref[1]);
                        } else {
                            c = 3 * (s->above_ref_ctx[col] != s->s.h.varcompref[1]);
                        }
                    }
                } else if (have_l) {
                    if (s->left_intra_ctx[row7]) {
                        c = 2;
                    } else if (s->left_comp_ctx[row7]) {
                        c = 4 * (s->left_ref_ctx[row7] != s->s.h.varcompref[1]);
                    } else {
                        c = 3 * (s->left_ref_ctx[row7] != s->s.h.varcompref[1]);
                    }
                } else {
                    c = 2;
                }
                bit = vp56_rac_get_prob(&s->c, s->prob.p.comp_ref[c]);
                b->ref[var_idx] = s->s.h.varcompref[bit];
                s->counts.comp_ref[c][bit]++;
            } else /* single reference */ {
                int bit, c;

                if (have_a && !s->above_intra_ctx[col]) {
                    if (have_l && !s->left_intra_ctx[row7]) {
                        if (s->left_comp_ctx[row7]) {
                            if (s->above_comp_ctx[col]) {
                                c = 1 + (!s->s.h.fixcompref || !s->left_ref_ctx[row7] ||
                                         !s->above_ref_ctx[col]);
                            } else {
                                c = (3 * !s->above_ref_ctx[col]) +
                                    (!s->s.h.fixcompref || !s->left_ref_ctx[row7]);
                            }
                        } else if (s->above_comp_ctx[col]) {
                            c = (3 * !s->left_ref_ctx[row7]) +
                                (!s->s.h.fixcompref || !s->above_ref_ctx[col]);
                        } else {
                            c = 2 * !s->left_ref_ctx[row7] + 2 * !s->above_ref_ctx[col];
                        }
                    } else if (s->above_intra_ctx[col]) {
                        c = 2;
                    } else if (s->above_comp_ctx[col]) {
                        c = 1 + (!s->s.h.fixcompref || !s->above_ref_ctx[col]);
                    } else {
                        c = 4 * (!s->above_ref_ctx[col]);
                    }
                } else if (have_l && !s->left_intra_ctx[row7]) {
                    if (s->left_intra_ctx[row7]) {
                        c = 2;
                    } else if (s->left_comp_ctx[row7]) {
                        c = 1 + (!s->s.h.fixcompref || !s->left_ref_ctx[row7]);
                    } else {
                        c = 4 * (!s->left_ref_ctx[row7]);
                    }
                } else {
                    c = 2;
                }
                bit = vp56_rac_get_prob(&s->c, s->prob.p.single_ref[c][0]);
                s->counts.single_ref[c][0][bit]++;
                if (!bit) {
                    b->ref[0] = 0;
                } else {
                    // FIXME can this codeblob be replaced by some sort of LUT?
                    if (have_a) {
                        if (have_l) {
                            if (s->left_intra_ctx[row7]) {
                                if (s->above_intra_ctx[col]) {
                                    c = 2;
                                } else if (s->above_comp_ctx[col]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 s->above_ref_ctx[col] == 1);
                                } else if (!s->above_ref_ctx[col]) {
                                    c = 3;
                                } else {
                                    c = 4 * (s->above_ref_ctx[col] == 1);
                                }
                            } else if (s->above_intra_ctx[col]) {
                                if (s->left_intra_ctx[row7]) {
                                    c = 2;
                                } else if (s->left_comp_ctx[row7]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 s->left_ref_ctx[row7] == 1);
                                } else if (!s->left_ref_ctx[row7]) {
                                    c = 3;
                                } else {
                                    c = 4 * (s->left_ref_ctx[row7] == 1);
                                }
                            } else if (s->above_comp_ctx[col]) {
                                if (s->left_comp_ctx[row7]) {
                                    if (s->left_ref_ctx[row7] == s->above_ref_ctx[col]) {
                                        c = 3 * (s->s.h.fixcompref == 1 ||
                                                 s->left_ref_ctx[row7] == 1);
                                    } else {
                                        c = 2;
                                    }
                                } else if (!s->left_ref_ctx[row7]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 s->above_ref_ctx[col] == 1);
                                } else {
                                    c = 3 * (s->left_ref_ctx[row7] == 1) +
                                    (s->s.h.fixcompref == 1 || s->above_ref_ctx[col] == 1);
                                }
                            } else if (s->left_comp_ctx[row7]) {
                                if (!s->above_ref_ctx[col]) {
                                    c = 1 + 2 * (s->s.h.fixcompref == 1 ||
                                                 s->left_ref_ctx[row7] == 1);
                                } else {
                                    c = 3 * (s->above_ref_ctx[col] == 1) +
                                    (s->s.h.fixcompref == 1 || s->left_ref_ctx[row7] == 1);
                                }
                            } else if (!s->above_ref_ctx[col]) {
                                if (!s->left_ref_ctx[row7]) {
                                    c = 3;
                                } else {
                                    c = 4 * (s->left_ref_ctx[row7] == 1);
                                }
                            } else if (!s->left_ref_ctx[row7]) {
                                c = 4 * (s->above_ref_ctx[col] == 1);
                            } else {
                                c = 2 * (s->left_ref_ctx[row7] == 1) +
                                2 * (s->above_ref_ctx[col] == 1);
                            }
                        } else {
                            if (s->above_intra_ctx[col] ||
                                (!s->above_comp_ctx[col] && !s->above_ref_ctx[col])) {
                                c = 2;
                            } else if (s->above_comp_ctx[col]) {
                                c = 3 * (s->s.h.fixcompref == 1 || s->above_ref_ctx[col] == 1);
                            } else {
                                c = 4 * (s->above_ref_ctx[col] == 1);
                            }
                        }
                    } else if (have_l) {
                        if (s->left_intra_ctx[row7] ||
                            (!s->left_comp_ctx[row7] && !s->left_ref_ctx[row7])) {
                            c = 2;
                        } else if (s->left_comp_ctx[row7]) {
                            c = 3 * (s->s.h.fixcompref == 1 || s->left_ref_ctx[row7] == 1);
                        } else {
                            c = 4 * (s->left_ref_ctx[row7] == 1);
                        }
                    } else {
                        c = 2;
                    }
                    bit = vp56_rac_get_prob(&s->c, s->prob.p.single_ref[c][1]);
                    s->counts.single_ref[c][1][bit]++;
                    b->ref[0] = 1 + bit;
                }
            }
        }

        if (b->bs <= BS_8x8) {
            if (s->s.h.segmentation.enabled && s->s.h.segmentation.feat[b->seg_id].skip_enabled) {
                b->mode[0] = b->mode[1] = b->mode[2] = b->mode[3] = ZEROMV;
            } else {
                static const uint8_t off[10] = {
                    3, 0, 0, 1, 0, 0, 0, 0, 0, 0
                };

                // FIXME this needs to use the LUT tables from find_ref_mvs
                // because not all are -1,0/0,-1
                int c = inter_mode_ctx_lut[s->above_mode_ctx[col + off[b->bs]]]
                                          [s->left_mode_ctx[row7 + off[b->bs]]];

                b->mode[0] = vp8_rac_get_tree(&s->c, vp9_inter_mode_tree,
                                              s->prob.p.mv_mode[c]);
                b->mode[1] = b->mode[2] = b->mode[3] = b->mode[0];
                s->counts.mv_mode[c][b->mode[0] - 10]++;
            }
        }

        if (s->s.h.filtermode == FILTER_SWITCHABLE) {
            int c;

            if (have_a && s->above_mode_ctx[col] >= NEARESTMV) {
                if (have_l && s->left_mode_ctx[row7] >= NEARESTMV) {
                    c = s->above_filter_ctx[col] == s->left_filter_ctx[row7] ?
                        s->left_filter_ctx[row7] : 3;
                } else {
                    c = s->above_filter_ctx[col];
                }
            } else if (have_l && s->left_mode_ctx[row7] >= NEARESTMV) {
                c = s->left_filter_ctx[row7];
            } else {
                c = 3;
            }

            filter_id = vp8_rac_get_tree(&s->c, vp9_filter_tree,
                                         s->prob.p.filter[c]);
            s->counts.filter[c][filter_id]++;
            b->filter = vp9_filter_lut[filter_id];
        } else {
            b->filter = s->s.h.filtermode;
        }

        if (b->bs > BS_8x8) {
            int c = inter_mode_ctx_lut[s->above_mode_ctx[col]][s->left_mode_ctx[row7]];

            b->mode[0] = vp8_rac_get_tree(&s->c, vp9_inter_mode_tree,
                                          s->prob.p.mv_mode[c]);
            s->counts.mv_mode[c][b->mode[0] - 10]++;
            fill_mv(s, b->mv[0], b->mode[0], 0);

            if (b->bs != BS_8x4) {
                b->mode[1] = vp8_rac_get_tree(&s->c, vp9_inter_mode_tree,
                                              s->prob.p.mv_mode[c]);
                s->counts.mv_mode[c][b->mode[1] - 10]++;
                fill_mv(s, b->mv[1], b->mode[1], 1);
            } else {
                b->mode[1] = b->mode[0];
                AV_COPY32(&b->mv[1][0], &b->mv[0][0]);
                AV_COPY32(&b->mv[1][1], &b->mv[0][1]);
            }

            if (b->bs != BS_4x8) {
                b->mode[2] = vp8_rac_get_tree(&s->c, vp9_inter_mode_tree,
                                              s->prob.p.mv_mode[c]);
                s->counts.mv_mode[c][b->mode[2] - 10]++;
                fill_mv(s, b->mv[2], b->mode[2], 2);

                if (b->bs != BS_8x4) {
                    b->mode[3] = vp8_rac_get_tree(&s->c, vp9_inter_mode_tree,
                                                  s->prob.p.mv_mode[c]);
                    s->counts.mv_mode[c][b->mode[3] - 10]++;
                    fill_mv(s, b->mv[3], b->mode[3], 3);
                } else {
                    b->mode[3] = b->mode[2];
                    AV_COPY32(&b->mv[3][0], &b->mv[2][0]);
                    AV_COPY32(&b->mv[3][1], &b->mv[2][1]);
                }
            } else {
                b->mode[2] = b->mode[0];
                AV_COPY32(&b->mv[2][0], &b->mv[0][0]);
                AV_COPY32(&b->mv[2][1], &b->mv[0][1]);
                b->mode[3] = b->mode[1];
                AV_COPY32(&b->mv[3][0], &b->mv[1][0]);
                AV_COPY32(&b->mv[3][1], &b->mv[1][1]);
            }
        } else {
            fill_mv(s, b->mv[0], b->mode[0], -1);
            AV_COPY32(&b->mv[1][0], &b->mv[0][0]);
            AV_COPY32(&b->mv[2][0], &b->mv[0][0]);
            AV_COPY32(&b->mv[3][0], &b->mv[0][0]);
            AV_COPY32(&b->mv[1][1], &b->mv[0][1]);
            AV_COPY32(&b->mv[2][1], &b->mv[0][1]);
            AV_COPY32(&b->mv[3][1], &b->mv[0][1]);
        }

        vref = b->ref[b->comp ? s->s.h.signbias[s->s.h.varcompref[0]] : 0];
    }

#if HAVE_FAST_64BIT
#define SPLAT_CTX(var, val, n) \
    switch (n) { \
    case 1:  var = val;                                    break; \
    case 2:  AV_WN16A(&var, val *             0x0101);     break; \
    case 4:  AV_WN32A(&var, val *         0x01010101);     break; \
    case 8:  AV_WN64A(&var, val * 0x0101010101010101ULL);  break; \
    case 16: { \
        uint64_t v64 = val * 0x0101010101010101ULL; \
        AV_WN64A(              &var,     v64); \
        AV_WN64A(&((uint8_t *) &var)[8], v64); \
        break; \
    } \
    }
#else
#define SPLAT_CTX(var, val, n) \
    switch (n) { \
    case 1:  var = val;                         break; \
    case 2:  AV_WN16A(&var, val *     0x0101);  break; \
    case 4:  AV_WN32A(&var, val * 0x01010101);  break; \
    case 8: { \
        uint32_t v32 = val * 0x01010101; \
        AV_WN32A(              &var,     v32); \
        AV_WN32A(&((uint8_t *) &var)[4], v32); \
        break; \
    } \
    case 16: { \
        uint32_t v32 = val * 0x01010101; \
        AV_WN32A(              &var,      v32); \
        AV_WN32A(&((uint8_t *) &var)[4],  v32); \
        AV_WN32A(&((uint8_t *) &var)[8],  v32); \
        AV_WN32A(&((uint8_t *) &var)[12], v32); \
        break; \
    } \
    }
#endif

    switch (bwh_tab[1][b->bs][0]) {
#define SET_CTXS(dir, off, n) \
    do { \
        SPLAT_CTX(s->dir##_skip_ctx[off],      b->skip,          n); \
        SPLAT_CTX(s->dir##_txfm_ctx[off],      b->tx,            n); \
        SPLAT_CTX(s->dir##_partition_ctx[off], dir##_ctx[b->bs], n); \
        if (!s->s.h.keyframe && !s->s.h.intraonly) { \
            SPLAT_CTX(s->dir##_intra_ctx[off], b->intra,   n); \
            SPLAT_CTX(s->dir##_comp_ctx[off],  b->comp,    n); \
            SPLAT_CTX(s->dir##_mode_ctx[off],  b->mode[3], n); \
            if (!b->intra) { \
                SPLAT_CTX(s->dir##_ref_ctx[off], vref, n); \
                if (s->s.h.filtermode == FILTER_SWITCHABLE) { \
                    SPLAT_CTX(s->dir##_filter_ctx[off], filter_id, n); \
                } \
            } \
        } \
    } while (0)
    case 1: SET_CTXS(above, col, 1); break;
    case 2: SET_CTXS(above, col, 2); break;
    case 4: SET_CTXS(above, col, 4); break;
    case 8: SET_CTXS(above, col, 8); break;
    }
    switch (bwh_tab[1][b->bs][1]) {
    case 1: SET_CTXS(left, row7, 1); break;
    case 2: SET_CTXS(left, row7, 2); break;
    case 4: SET_CTXS(left, row7, 4); break;
    case 8: SET_CTXS(left, row7, 8); break;
    }
#undef SPLAT_CTX
#undef SET_CTXS

    if (!s->s.h.keyframe && !s->s.h.intraonly) {
        if (b->bs > BS_8x8) {
            int mv0 = AV_RN32A(&b->mv[3][0]), mv1 = AV_RN32A(&b->mv[3][1]);

            AV_COPY32(&s->left_mv_ctx[row7 * 2 + 0][0], &b->mv[1][0]);
            AV_COPY32(&s->left_mv_ctx[row7 * 2 + 0][1], &b->mv[1][1]);
            AV_WN32A(&s->left_mv_ctx[row7 * 2 + 1][0], mv0);
            AV_WN32A(&s->left_mv_ctx[row7 * 2 + 1][1], mv1);
            AV_COPY32(&s->above_mv_ctx[col * 2 + 0][0], &b->mv[2][0]);
            AV_COPY32(&s->above_mv_ctx[col * 2 + 0][1], &b->mv[2][1]);
            AV_WN32A(&s->above_mv_ctx[col * 2 + 1][0], mv0);
            AV_WN32A(&s->above_mv_ctx[col * 2 + 1][1], mv1);
        } else {
            int n, mv0 = AV_RN32A(&b->mv[3][0]), mv1 = AV_RN32A(&b->mv[3][1]);

            for (n = 0; n < w4 * 2; n++) {
                AV_WN32A(&s->above_mv_ctx[col * 2 + n][0], mv0);
                AV_WN32A(&s->above_mv_ctx[col * 2 + n][1], mv1);
            }
            for (n = 0; n < h4 * 2; n++) {
                AV_WN32A(&s->left_mv_ctx[row7 * 2 + n][0], mv0);
                AV_WN32A(&s->left_mv_ctx[row7 * 2 + n][1], mv1);
            }
        }
    }

    // FIXME kinda ugly
    for (y = 0; y < h4; y++) {
        int x, o = (row + y) * s->sb_cols * 8 + col;
        struct VP9mvrefPair *mv = &s->s.frames[CUR_FRAME].mv[o];

        if (b->intra) {
            for (x = 0; x < w4; x++) {
                mv[x].ref[0] =
                mv[x].ref[1] = -1;
            }
        } else if (b->comp) {
            for (x = 0; x < w4; x++) {
                mv[x].ref[0] = b->ref[0];
                mv[x].ref[1] = b->ref[1];
                AV_COPY32(&mv[x].mv[0], &b->mv[3][0]);
                AV_COPY32(&mv[x].mv[1], &b->mv[3][1]);
            }
        } else {
            for (x = 0; x < w4; x++) {
                mv[x].ref[0] = b->ref[0];
                mv[x].ref[1] = -1;
                AV_COPY32(&mv[x].mv[0], &b->mv[3][0]);
            }
        }
    }
}

// FIXME merge cnt/eob arguments?
static av_always_inline int
decode_coeffs_b_generic(VP56RangeCoder *c, int16_t *coef, int n_coeffs,
                        int is_tx32x32, int is8bitsperpixel, int bpp, unsigned (*cnt)[6][3],
                        unsigned (*eob)[6][2], uint8_t (*p)[6][11],
                        int nnz, const int16_t *scan, const int16_t (*nb)[2],
                        const int16_t *band_counts, const int16_t *qmul)
{
    int i = 0, band = 0, band_left = band_counts[band];
    uint8_t *tp = p[0][nnz];
    uint8_t cache[1024];

    do {
        int val, rc;

        val = vp56_rac_get_prob_branchy(c, tp[0]); // eob
        eob[band][nnz][val]++;
        if (!val)
            break;

    skip_eob:
        if (!vp56_rac_get_prob_branchy(c, tp[1])) { // zero
            cnt[band][nnz][0]++;
            if (!--band_left)
                band_left = band_counts[++band];
            cache[scan[i]] = 0;
            nnz = (1 + cache[nb[i][0]] + cache[nb[i][1]]) >> 1;
            tp = p[band][nnz];
            if (++i == n_coeffs)
                break; //invalid input; blocks should end with EOB
            goto skip_eob;
        }

        rc = scan[i];
        if (!vp56_rac_get_prob_branchy(c, tp[2])) { // one
            cnt[band][nnz][1]++;
            val = 1;
            cache[rc] = 1;
        } else {
            // fill in p[3-10] (model fill) - only once per frame for each pos
            if (!tp[3])
                memcpy(&tp[3], vp9_model_pareto8[tp[2]], 8);

            cnt[band][nnz][2]++;
            if (!vp56_rac_get_prob_branchy(c, tp[3])) { // 2, 3, 4
                if (!vp56_rac_get_prob_branchy(c, tp[4])) {
                    cache[rc] = val = 2;
                } else {
                    val = 3 + vp56_rac_get_prob(c, tp[5]);
                    cache[rc] = 3;
                }
            } else if (!vp56_rac_get_prob_branchy(c, tp[6])) { // cat1/2
                cache[rc] = 4;
                if (!vp56_rac_get_prob_branchy(c, tp[7])) {
                    val = 5 + vp56_rac_get_prob(c, 159);
                } else {
                    val  = 7 + (vp56_rac_get_prob(c, 165) << 1);
                    val +=      vp56_rac_get_prob(c, 145);
                }
            } else { // cat 3-6
                cache[rc] = 5;
                if (!vp56_rac_get_prob_branchy(c, tp[8])) {
                    if (!vp56_rac_get_prob_branchy(c, tp[9])) {
                        val  = 11 + (vp56_rac_get_prob(c, 173) << 2);
                        val +=      (vp56_rac_get_prob(c, 148) << 1);
                        val +=       vp56_rac_get_prob(c, 140);
                    } else {
                        val  = 19 + (vp56_rac_get_prob(c, 176) << 3);
                        val +=      (vp56_rac_get_prob(c, 155) << 2);
                        val +=      (vp56_rac_get_prob(c, 140) << 1);
                        val +=       vp56_rac_get_prob(c, 135);
                    }
                } else if (!vp56_rac_get_prob_branchy(c, tp[10])) {
                    val  = 35 + (vp56_rac_get_prob(c, 180) << 4);
                    val +=      (vp56_rac_get_prob(c, 157) << 3);
                    val +=      (vp56_rac_get_prob(c, 141) << 2);
                    val +=      (vp56_rac_get_prob(c, 134) << 1);
                    val +=       vp56_rac_get_prob(c, 130);
                } else {
                    val = 67;
                    if (!is8bitsperpixel) {
                        if (bpp == 12) {
                            val += vp56_rac_get_prob(c, 255) << 17;
                            val += vp56_rac_get_prob(c, 255) << 16;
                        }
                        val +=  (vp56_rac_get_prob(c, 255) << 15);
                        val +=  (vp56_rac_get_prob(c, 255) << 14);
                    }
                    val +=      (vp56_rac_get_prob(c, 254) << 13);
                    val +=      (vp56_rac_get_prob(c, 254) << 12);
                    val +=      (vp56_rac_get_prob(c, 254) << 11);
                    val +=      (vp56_rac_get_prob(c, 252) << 10);
                    val +=      (vp56_rac_get_prob(c, 249) << 9);
                    val +=      (vp56_rac_get_prob(c, 243) << 8);
                    val +=      (vp56_rac_get_prob(c, 230) << 7);
                    val +=      (vp56_rac_get_prob(c, 196) << 6);
                    val +=      (vp56_rac_get_prob(c, 177) << 5);
                    val +=      (vp56_rac_get_prob(c, 153) << 4);
                    val +=      (vp56_rac_get_prob(c, 140) << 3);
                    val +=      (vp56_rac_get_prob(c, 133) << 2);
                    val +=      (vp56_rac_get_prob(c, 130) << 1);
                    val +=       vp56_rac_get_prob(c, 129);
                }
            }
        }
#define STORE_COEF(c, i, v) do { \
    if (is8bitsperpixel) { \
        c[i] = v; \
    } else { \
        AV_WN32A(&c[i * 2], v); \
    } \
} while (0)
        if (!--band_left)
            band_left = band_counts[++band];
        if (is_tx32x32)
            STORE_COEF(coef, rc, ((vp8_rac_get(c) ? -val : val) * qmul[!!i]) / 2);
        else
            STORE_COEF(coef, rc, (vp8_rac_get(c) ? -val : val) * qmul[!!i]);
        nnz = (1 + cache[nb[i][0]] + cache[nb[i][1]]) >> 1;
        tp = p[band][nnz];
    } while (++i < n_coeffs);

    return i;
}

static int decode_coeffs_b_8bpp(VP9Context *s, int16_t *coef, int n_coeffs,
                                unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                const int16_t (*nb)[2], const int16_t *band_counts,
                                const int16_t *qmul)
{
    return decode_coeffs_b_generic(&s->c, coef, n_coeffs, 0, 1, 8, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static int decode_coeffs_b32_8bpp(VP9Context *s, int16_t *coef, int n_coeffs,
                                  unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                  uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                  const int16_t (*nb)[2], const int16_t *band_counts,
                                  const int16_t *qmul)
{
    return decode_coeffs_b_generic(&s->c, coef, n_coeffs, 1, 1, 8, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static int decode_coeffs_b_16bpp(VP9Context *s, int16_t *coef, int n_coeffs,
                                 unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                 uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                 const int16_t (*nb)[2], const int16_t *band_counts,
                                 const int16_t *qmul)
{
    return decode_coeffs_b_generic(&s->c, coef, n_coeffs, 0, 0, s->bpp, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static int decode_coeffs_b32_16bpp(VP9Context *s, int16_t *coef, int n_coeffs,
                                   unsigned (*cnt)[6][3], unsigned (*eob)[6][2],
                                   uint8_t (*p)[6][11], int nnz, const int16_t *scan,
                                   const int16_t (*nb)[2], const int16_t *band_counts,
                                   const int16_t *qmul)
{
    return decode_coeffs_b_generic(&s->c, coef, n_coeffs, 1, 0, s->bpp, cnt, eob, p,
                                   nnz, scan, nb, band_counts, qmul);
}

static av_always_inline int decode_coeffs(AVCodecContext *ctx, int is8bitsperpixel)
{
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    int row = s->row, col = s->col;
    uint8_t (*p)[6][11] = s->prob.coef[b->tx][0 /* y */][!b->intra];
    unsigned (*c)[6][3] = s->counts.coef[b->tx][0 /* y */][!b->intra];
    unsigned (*e)[6][2] = s->counts.eob[b->tx][0 /* y */][!b->intra];
    int w4 = bwh_tab[1][b->bs][0] << 1, h4 = bwh_tab[1][b->bs][1] << 1;
    int end_x = FFMIN(2 * (s->cols - col), w4);
    int end_y = FFMIN(2 * (s->rows - row), h4);
    int n, pl, x, y, res;
    int16_t (*qmul)[2] = s->s.h.segmentation.feat[b->seg_id].qmul;
    int tx = 4 * s->s.h.lossless + b->tx;
    const int16_t * const *yscans = vp9_scans[tx];
    const int16_t (* const *ynbs)[2] = vp9_scans_nb[tx];
    const int16_t *uvscan = vp9_scans[b->uvtx][DCT_DCT];
    const int16_t (*uvnb)[2] = vp9_scans_nb[b->uvtx][DCT_DCT];
    uint8_t *a = &s->above_y_nnz_ctx[col * 2];
    uint8_t *l = &s->left_y_nnz_ctx[(row & 7) << 1];
    static const int16_t band_counts[4][8] = {
        { 1, 2, 3, 4,  3,   16 - 13 },
        { 1, 2, 3, 4, 11,   64 - 21 },
        { 1, 2, 3, 4, 11,  256 - 21 },
        { 1, 2, 3, 4, 11, 1024 - 21 },
    };
    const int16_t *y_band_counts = band_counts[b->tx];
    const int16_t *uv_band_counts = band_counts[b->uvtx];
    int bytesperpixel = is8bitsperpixel ? 1 : 2;
    int total_coeff = 0;

#define MERGE(la, end, step, rd) \
    for (n = 0; n < end; n += step) \
        la[n] = !!rd(&la[n])
#define MERGE_CTX(step, rd) \
    do { \
        MERGE(l, end_y, step, rd); \
        MERGE(a, end_x, step, rd); \
    } while (0)

#define DECODE_Y_COEF_LOOP(step, mode_index, v) \
    for (n = 0, y = 0; y < end_y; y += step) { \
        for (x = 0; x < end_x; x += step, n += step * step) { \
            enum TxfmType txtp = vp9_intra_txfm_type[b->mode[mode_index]]; \
            res = (is8bitsperpixel ? decode_coeffs_b##v##_8bpp : decode_coeffs_b##v##_16bpp) \
                                    (s, s->block + 16 * n * bytesperpixel, 16 * step * step, \
                                     c, e, p, a[x] + l[y], yscans[txtp], \
                                     ynbs[txtp], y_band_counts, qmul[0]); \
            a[x] = l[y] = !!res; \
            total_coeff |= !!res; \
            if (step >= 4) { \
                AV_WN16A(&s->eob[n], res); \
            } else { \
                s->eob[n] = res; \
            } \
        } \
    }

#define SPLAT(la, end, step, cond) \
    if (step == 2) { \
        for (n = 1; n < end; n += step) \
            la[n] = la[n - 1]; \
    } else if (step == 4) { \
        if (cond) { \
            for (n = 0; n < end; n += step) \
                AV_WN32A(&la[n], la[n] * 0x01010101); \
        } else { \
            for (n = 0; n < end; n += step) \
                memset(&la[n + 1], la[n], FFMIN(end - n - 1, 3)); \
        } \
    } else /* step == 8 */ { \
        if (cond) { \
            if (HAVE_FAST_64BIT) { \
                for (n = 0; n < end; n += step) \
                    AV_WN64A(&la[n], la[n] * 0x0101010101010101ULL); \
            } else { \
                for (n = 0; n < end; n += step) { \
                    uint32_t v32 = la[n] * 0x01010101; \
                    AV_WN32A(&la[n],     v32); \
                    AV_WN32A(&la[n + 4], v32); \
                } \
            } \
        } else { \
            for (n = 0; n < end; n += step) \
                memset(&la[n + 1], la[n], FFMIN(end - n - 1, 7)); \
        } \
    }
#define SPLAT_CTX(step) \
    do { \
        SPLAT(a, end_x, step, end_x == w4); \
        SPLAT(l, end_y, step, end_y == h4); \
    } while (0)

    /* y tokens */
    switch (b->tx) {
    case TX_4X4:
        DECODE_Y_COEF_LOOP(1, b->bs > BS_8x8 ? n : 0,);
        break;
    case TX_8X8:
        MERGE_CTX(2, AV_RN16A);
        DECODE_Y_COEF_LOOP(2, 0,);
        SPLAT_CTX(2);
        break;
    case TX_16X16:
        MERGE_CTX(4, AV_RN32A);
        DECODE_Y_COEF_LOOP(4, 0,);
        SPLAT_CTX(4);
        break;
    case TX_32X32:
        MERGE_CTX(8, AV_RN64A);
        DECODE_Y_COEF_LOOP(8, 0, 32);
        SPLAT_CTX(8);
        break;
    }

#define DECODE_UV_COEF_LOOP(step, v) \
    for (n = 0, y = 0; y < end_y; y += step) { \
        for (x = 0; x < end_x; x += step, n += step * step) { \
            res = (is8bitsperpixel ? decode_coeffs_b##v##_8bpp : decode_coeffs_b##v##_16bpp) \
                                    (s, s->uvblock[pl] + 16 * n * bytesperpixel, \
                                     16 * step * step, c, e, p, a[x] + l[y], \
                                     uvscan, uvnb, uv_band_counts, qmul[1]); \
            a[x] = l[y] = !!res; \
            total_coeff |= !!res; \
            if (step >= 4) { \
                AV_WN16A(&s->uveob[pl][n], res); \
            } else { \
                s->uveob[pl][n] = res; \
            } \
        } \
    }

    p = s->prob.coef[b->uvtx][1 /* uv */][!b->intra];
    c = s->counts.coef[b->uvtx][1 /* uv */][!b->intra];
    e = s->counts.eob[b->uvtx][1 /* uv */][!b->intra];
    w4 >>= s->ss_h;
    end_x >>= s->ss_h;
    h4 >>= s->ss_v;
    end_y >>= s->ss_v;
    for (pl = 0; pl < 2; pl++) {
        a = &s->above_uv_nnz_ctx[pl][col << !s->ss_h];
        l = &s->left_uv_nnz_ctx[pl][(row & 7) << !s->ss_v];
        switch (b->uvtx) {
        case TX_4X4:
            DECODE_UV_COEF_LOOP(1,);
            break;
        case TX_8X8:
            MERGE_CTX(2, AV_RN16A);
            DECODE_UV_COEF_LOOP(2,);
            SPLAT_CTX(2);
            break;
        case TX_16X16:
            MERGE_CTX(4, AV_RN32A);
            DECODE_UV_COEF_LOOP(4,);
            SPLAT_CTX(4);
            break;
        case TX_32X32:
            MERGE_CTX(8, AV_RN64A);
            DECODE_UV_COEF_LOOP(8, 32);
            SPLAT_CTX(8);
            break;
        }
    }

    return total_coeff;
}

static int decode_coeffs_8bpp(AVCodecContext *ctx)
{
    return decode_coeffs(ctx, 1);
}

static int decode_coeffs_16bpp(AVCodecContext *ctx)
{
    return decode_coeffs(ctx, 0);
}

static av_always_inline int check_intra_mode(VP9Context *s, int mode, uint8_t **a,
                                             uint8_t *dst_edge, ptrdiff_t stride_edge,
                                             uint8_t *dst_inner, ptrdiff_t stride_inner,
                                             uint8_t *l, int col, int x, int w,
                                             int row, int y, enum TxfmMode tx,
                                             int p, int ss_h, int ss_v, int bytesperpixel)
{
    int have_top = row > 0 || y > 0;
    int have_left = col > s->tile_col_start || x > 0;
    int have_right = x < w - 1;
    int bpp = s->bpp;
    static const uint8_t mode_conv[10][2 /* have_left */][2 /* have_top */] = {
        [VERT_PRED]            = { { DC_127_PRED,          VERT_PRED },
                                   { DC_127_PRED,          VERT_PRED } },
        [HOR_PRED]             = { { DC_129_PRED,          DC_129_PRED },
                                   { HOR_PRED,             HOR_PRED } },
        [DC_PRED]              = { { DC_128_PRED,          TOP_DC_PRED },
                                   { LEFT_DC_PRED,         DC_PRED } },
        [DIAG_DOWN_LEFT_PRED]  = { { DC_127_PRED,          DIAG_DOWN_LEFT_PRED },
                                   { DC_127_PRED,          DIAG_DOWN_LEFT_PRED } },
        [DIAG_DOWN_RIGHT_PRED] = { { DIAG_DOWN_RIGHT_PRED, DIAG_DOWN_RIGHT_PRED },
                                   { DIAG_DOWN_RIGHT_PRED, DIAG_DOWN_RIGHT_PRED } },
        [VERT_RIGHT_PRED]      = { { VERT_RIGHT_PRED,      VERT_RIGHT_PRED },
                                   { VERT_RIGHT_PRED,      VERT_RIGHT_PRED } },
        [HOR_DOWN_PRED]        = { { HOR_DOWN_PRED,        HOR_DOWN_PRED },
                                   { HOR_DOWN_PRED,        HOR_DOWN_PRED } },
        [VERT_LEFT_PRED]       = { { DC_127_PRED,          VERT_LEFT_PRED },
                                   { DC_127_PRED,          VERT_LEFT_PRED } },
        [HOR_UP_PRED]          = { { DC_129_PRED,          DC_129_PRED },
                                   { HOR_UP_PRED,          HOR_UP_PRED } },
        [TM_VP8_PRED]          = { { DC_129_PRED,          VERT_PRED },
                                   { HOR_PRED,             TM_VP8_PRED } },
    };
    static const struct {
        uint8_t needs_left:1;
        uint8_t needs_top:1;
        uint8_t needs_topleft:1;
        uint8_t needs_topright:1;
        uint8_t invert_left:1;
    } edges[N_INTRA_PRED_MODES] = {
        [VERT_PRED]            = { .needs_top  = 1 },
        [HOR_PRED]             = { .needs_left = 1 },
        [DC_PRED]              = { .needs_top  = 1, .needs_left = 1 },
        [DIAG_DOWN_LEFT_PRED]  = { .needs_top  = 1, .needs_topright = 1 },
        [DIAG_DOWN_RIGHT_PRED] = { .needs_left = 1, .needs_top = 1, .needs_topleft = 1 },
        [VERT_RIGHT_PRED]      = { .needs_left = 1, .needs_top = 1, .needs_topleft = 1 },
        [HOR_DOWN_PRED]        = { .needs_left = 1, .needs_top = 1, .needs_topleft = 1 },
        [VERT_LEFT_PRED]       = { .needs_top  = 1, .needs_topright = 1 },
        [HOR_UP_PRED]          = { .needs_left = 1, .invert_left = 1 },
        [TM_VP8_PRED]          = { .needs_left = 1, .needs_top = 1, .needs_topleft = 1 },
        [LEFT_DC_PRED]         = { .needs_left = 1 },
        [TOP_DC_PRED]          = { .needs_top  = 1 },
        [DC_128_PRED]          = { 0 },
        [DC_127_PRED]          = { 0 },
        [DC_129_PRED]          = { 0 }
    };

    av_assert2(mode >= 0 && mode < 10);
    mode = mode_conv[mode][have_left][have_top];
    if (edges[mode].needs_top) {
        uint8_t *top, *topleft;
        int n_px_need = 4 << tx, n_px_have = (((s->cols - col) << !ss_h) - x) * 4;
        int n_px_need_tr = 0;

        if (tx == TX_4X4 && edges[mode].needs_topright && have_right)
            n_px_need_tr = 4;

        // if top of sb64-row, use s->intra_pred_data[] instead of
        // dst[-stride] for intra prediction (it contains pre- instead of
        // post-loopfilter data)
        if (have_top) {
            top = !(row & 7) && !y ?
                s->intra_pred_data[p] + (col * (8 >> ss_h) + x * 4) * bytesperpixel :
                y == 0 ? &dst_edge[-stride_edge] : &dst_inner[-stride_inner];
            if (have_left)
                topleft = !(row & 7) && !y ?
                    s->intra_pred_data[p] + (col * (8 >> ss_h) + x * 4) * bytesperpixel :
                    y == 0 || x == 0 ? &dst_edge[-stride_edge] :
                    &dst_inner[-stride_inner];
        }

        if (have_top &&
            (!edges[mode].needs_topleft || (have_left && top == topleft)) &&
            (tx != TX_4X4 || !edges[mode].needs_topright || have_right) &&
            n_px_need + n_px_need_tr <= n_px_have) {
            *a = top;
        } else {
            if (have_top) {
                if (n_px_need <= n_px_have) {
                    memcpy(*a, top, n_px_need * bytesperpixel);
                } else {
#define memset_bpp(c, i1, v, i2, num) do { \
    if (bytesperpixel == 1) { \
        memset(&(c)[(i1)], (v)[(i2)], (num)); \
    } else { \
        int n, val = AV_RN16A(&(v)[(i2) * 2]); \
        for (n = 0; n < (num); n++) { \
            AV_WN16A(&(c)[((i1) + n) * 2], val); \
        } \
    } \
} while (0)
                    memcpy(*a, top, n_px_have * bytesperpixel);
                    memset_bpp(*a, n_px_have, (*a), n_px_have - 1, n_px_need - n_px_have);
                }
            } else {
#define memset_val(c, val, num) do { \
    if (bytesperpixel == 1) { \
        memset((c), (val), (num)); \
    } else { \
        int n; \
        for (n = 0; n < (num); n++) { \
            AV_WN16A(&(c)[n * 2], (val)); \
        } \
    } \
} while (0)
                memset_val(*a, (128 << (bpp - 8)) - 1, n_px_need);
            }
            if (edges[mode].needs_topleft) {
                if (have_left && have_top) {
#define assign_bpp(c, i1, v, i2) do { \
    if (bytesperpixel == 1) { \
        (c)[(i1)] = (v)[(i2)]; \
    } else { \
        AV_COPY16(&(c)[(i1) * 2], &(v)[(i2) * 2]); \
    } \
} while (0)
                    assign_bpp(*a, -1, topleft, -1);
                } else {
#define assign_val(c, i, v) do { \
    if (bytesperpixel == 1) { \
        (c)[(i)] = (v); \
    } else { \
        AV_WN16A(&(c)[(i) * 2], (v)); \
    } \
} while (0)
                    assign_val((*a), -1, (128 << (bpp - 8)) + (have_top ? +1 : -1));
                }
            }
            if (tx == TX_4X4 && edges[mode].needs_topright) {
                if (have_top && have_right &&
                    n_px_need + n_px_need_tr <= n_px_have) {
                    memcpy(&(*a)[4 * bytesperpixel], &top[4 * bytesperpixel], 4 * bytesperpixel);
                } else {
                    memset_bpp(*a, 4, *a, 3, 4);
                }
            }
        }
    }
    if (edges[mode].needs_left) {
        if (have_left) {
            int n_px_need = 4 << tx, i, n_px_have = (((s->rows - row) << !ss_v) - y) * 4;
            uint8_t *dst = x == 0 ? dst_edge : dst_inner;
            ptrdiff_t stride = x == 0 ? stride_edge : stride_inner;

            if (edges[mode].invert_left) {
                if (n_px_need <= n_px_have) {
                    for (i = 0; i < n_px_need; i++)
                        assign_bpp(l, i, &dst[i * stride], -1);
                } else {
                    for (i = 0; i < n_px_have; i++)
                        assign_bpp(l, i, &dst[i * stride], -1);
                    memset_bpp(l, n_px_have, l, n_px_have - 1, n_px_need - n_px_have);
                }
            } else {
                if (n_px_need <= n_px_have) {
                    for (i = 0; i < n_px_need; i++)
                        assign_bpp(l, n_px_need - 1 - i, &dst[i * stride], -1);
                } else {
                    for (i = 0; i < n_px_have; i++)
                        assign_bpp(l, n_px_need - 1 - i, &dst[i * stride], -1);
                    memset_bpp(l, 0, l, n_px_need - n_px_have, n_px_need - n_px_have);
                }
            }
        } else {
            memset_val(l, (128 << (bpp - 8)) + 1, 4 << tx);
        }
    }

    return mode;
}

static av_always_inline void intra_recon(AVCodecContext *ctx, ptrdiff_t y_off,
                                         ptrdiff_t uv_off, int bytesperpixel)
{
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    int row = s->row, col = s->col;
    int w4 = bwh_tab[1][b->bs][0] << 1, step1d = 1 << b->tx, n;
    int h4 = bwh_tab[1][b->bs][1] << 1, x, y, step = 1 << (b->tx * 2);
    int end_x = FFMIN(2 * (s->cols - col), w4);
    int end_y = FFMIN(2 * (s->rows - row), h4);
    int tx = 4 * s->s.h.lossless + b->tx, uvtx = b->uvtx + 4 * s->s.h.lossless;
    int uvstep1d = 1 << b->uvtx, p;
    uint8_t *dst = s->dst[0], *dst_r = s->s.frames[CUR_FRAME].tf.f->data[0] + y_off;
    LOCAL_ALIGNED_32(uint8_t, a_buf, [96]);
    LOCAL_ALIGNED_32(uint8_t, l, [64]);

    for (n = 0, y = 0; y < end_y; y += step1d) {
        uint8_t *ptr = dst, *ptr_r = dst_r;
        for (x = 0; x < end_x; x += step1d, ptr += 4 * step1d * bytesperpixel,
                               ptr_r += 4 * step1d * bytesperpixel, n += step) {
            int mode = b->mode[b->bs > BS_8x8 && b->tx == TX_4X4 ?
                               y * 2 + x : 0];
            uint8_t *a = &a_buf[32];
            enum TxfmType txtp = vp9_intra_txfm_type[mode];
            int eob = b->skip ? 0 : b->tx > TX_8X8 ? AV_RN16A(&s->eob[n]) : s->eob[n];

            mode = check_intra_mode(s, mode, &a, ptr_r,
                                    s->s.frames[CUR_FRAME].tf.f->linesize[0],
                                    ptr, s->y_stride, l,
                                    col, x, w4, row, y, b->tx, 0, 0, 0, bytesperpixel);
            s->dsp.intra_pred[b->tx][mode](ptr, s->y_stride, l, a);
            if (eob)
                s->dsp.itxfm_add[tx][txtp](ptr, s->y_stride,
                                           s->block + 16 * n * bytesperpixel, eob);
        }
        dst_r += 4 * step1d * s->s.frames[CUR_FRAME].tf.f->linesize[0];
        dst   += 4 * step1d * s->y_stride;
    }

    // U/V
    w4 >>= s->ss_h;
    end_x >>= s->ss_h;
    end_y >>= s->ss_v;
    step = 1 << (b->uvtx * 2);
    for (p = 0; p < 2; p++) {
        dst   = s->dst[1 + p];
        dst_r = s->s.frames[CUR_FRAME].tf.f->data[1 + p] + uv_off;
        for (n = 0, y = 0; y < end_y; y += uvstep1d) {
            uint8_t *ptr = dst, *ptr_r = dst_r;
            for (x = 0; x < end_x; x += uvstep1d, ptr += 4 * uvstep1d * bytesperpixel,
                                   ptr_r += 4 * uvstep1d * bytesperpixel, n += step) {
                int mode = b->uvmode;
                uint8_t *a = &a_buf[32];
                int eob = b->skip ? 0 : b->uvtx > TX_8X8 ? AV_RN16A(&s->uveob[p][n]) : s->uveob[p][n];

                mode = check_intra_mode(s, mode, &a, ptr_r,
                                        s->s.frames[CUR_FRAME].tf.f->linesize[1],
                                        ptr, s->uv_stride, l, col, x, w4, row, y,
                                        b->uvtx, p + 1, s->ss_h, s->ss_v, bytesperpixel);
                s->dsp.intra_pred[b->uvtx][mode](ptr, s->uv_stride, l, a);
                if (eob)
                    s->dsp.itxfm_add[uvtx][DCT_DCT](ptr, s->uv_stride,
                                                    s->uvblock[p] + 16 * n * bytesperpixel, eob);
            }
            dst_r += 4 * uvstep1d * s->s.frames[CUR_FRAME].tf.f->linesize[1];
            dst   += 4 * uvstep1d * s->uv_stride;
        }
    }
}

static void intra_recon_8bpp(AVCodecContext *ctx, ptrdiff_t y_off, ptrdiff_t uv_off)
{
    intra_recon(ctx, y_off, uv_off, 1);
}

static void intra_recon_16bpp(AVCodecContext *ctx, ptrdiff_t y_off, ptrdiff_t uv_off)
{
    intra_recon(ctx, y_off, uv_off, 2);
}

static av_always_inline void mc_luma_unscaled(VP9Context *s, vp9_mc_func (*mc)[2],
                                              uint8_t *dst, ptrdiff_t dst_stride,
                                              const uint8_t *ref, ptrdiff_t ref_stride,
                                              ThreadFrame *ref_frame,
                                              ptrdiff_t y, ptrdiff_t x, const VP56mv *mv,
                                              int bw, int bh, int w, int h, int bytesperpixel)
{
    int mx = mv->x, my = mv->y, th;

    y += my >> 3;
    x += mx >> 3;
    ref += y * ref_stride + x * bytesperpixel;
    mx &= 7;
    my &= 7;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + bh + 4 * !!my + 7) >> 6;
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    if (x < !!mx * 3 || y < !!my * 3 ||
        x + !!mx * 4 > w - bw || y + !!my * 4 > h - bh) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref - !!my * 3 * ref_stride - !!mx * 3 * bytesperpixel,
                                 160, ref_stride,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref = s->edge_emu_buffer + !!my * 3 * 160 + !!mx * 3 * bytesperpixel;
        ref_stride = 160;
    }
    mc[!!mx][!!my](dst, dst_stride, ref, ref_stride, bh, mx << 1, my << 1);
}

static av_always_inline void mc_chroma_unscaled(VP9Context *s, vp9_mc_func (*mc)[2],
                                                uint8_t *dst_u, uint8_t *dst_v,
                                                ptrdiff_t dst_stride,
                                                const uint8_t *ref_u, ptrdiff_t src_stride_u,
                                                const uint8_t *ref_v, ptrdiff_t src_stride_v,
                                                ThreadFrame *ref_frame,
                                                ptrdiff_t y, ptrdiff_t x, const VP56mv *mv,
                                                int bw, int bh, int w, int h, int bytesperpixel)
{
    int mx = mv->x << !s->ss_h, my = mv->y << !s->ss_v, th;

    y += my >> 4;
    x += mx >> 4;
    ref_u += y * src_stride_u + x * bytesperpixel;
    ref_v += y * src_stride_v + x * bytesperpixel;
    mx &= 15;
    my &= 15;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + bh + 4 * !!my + 7) >> (6 - s->ss_v);
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    if (x < !!mx * 3 || y < !!my * 3 ||
        x + !!mx * 4 > w - bw || y + !!my * 4 > h - bh) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_u - !!my * 3 * src_stride_u - !!mx * 3 * bytesperpixel,
                                 160, src_stride_u,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref_u = s->edge_emu_buffer + !!my * 3 * 160 + !!mx * 3 * bytesperpixel;
        mc[!!mx][!!my](dst_u, dst_stride, ref_u, 160, bh, mx, my);

        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_v - !!my * 3 * src_stride_v - !!mx * 3 * bytesperpixel,
                                 160, src_stride_v,
                                 bw + !!mx * 7, bh + !!my * 7,
                                 x - !!mx * 3, y - !!my * 3, w, h);
        ref_v = s->edge_emu_buffer + !!my * 3 * 160 + !!mx * 3 * bytesperpixel;
        mc[!!mx][!!my](dst_v, dst_stride, ref_v, 160, bh, mx, my);
    } else {
        mc[!!mx][!!my](dst_u, dst_stride, ref_u, src_stride_u, bh, mx, my);
        mc[!!mx][!!my](dst_v, dst_stride, ref_v, src_stride_v, bh, mx, my);
    }
}

#define mc_luma_dir(s, mc, dst, dst_ls, src, src_ls, tref, row, col, mv, \
                    px, py, pw, ph, bw, bh, w, h, i) \
    mc_luma_unscaled(s, s->dsp.mc, dst, dst_ls, src, src_ls, tref, row, col, \
                     mv, bw, bh, w, h, bytesperpixel)
#define mc_chroma_dir(s, mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                      row, col, mv, px, py, pw, ph, bw, bh, w, h, i) \
    mc_chroma_unscaled(s, s->dsp.mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                       row, col, mv, bw, bh, w, h, bytesperpixel)
#define SCALED 0
#define FN(x) x##_8bpp
#define BYTES_PER_PIXEL 1
#include "vp9_mc_template.c"
#undef FN
#undef BYTES_PER_PIXEL
#define FN(x) x##_16bpp
#define BYTES_PER_PIXEL 2
#include "vp9_mc_template.c"
#undef mc_luma_dir
#undef mc_chroma_dir
#undef FN
#undef BYTES_PER_PIXEL
#undef SCALED

static av_always_inline void mc_luma_scaled(VP9Context *s, vp9_scaled_mc_func smc,
                                            vp9_mc_func (*mc)[2],
                                            uint8_t *dst, ptrdiff_t dst_stride,
                                            const uint8_t *ref, ptrdiff_t ref_stride,
                                            ThreadFrame *ref_frame,
                                            ptrdiff_t y, ptrdiff_t x, const VP56mv *in_mv,
                                            int px, int py, int pw, int ph,
                                            int bw, int bh, int w, int h, int bytesperpixel,
                                            const uint16_t *scale, const uint8_t *step)
{
    if (s->s.frames[CUR_FRAME].tf.f->width == ref_frame->f->width &&
        s->s.frames[CUR_FRAME].tf.f->height == ref_frame->f->height) {
        mc_luma_unscaled(s, mc, dst, dst_stride, ref, ref_stride, ref_frame,
                         y, x, in_mv, bw, bh, w, h, bytesperpixel);
    } else {
#define scale_mv(n, dim) (((int64_t)(n) * scale[dim]) >> 14)
    int mx, my;
    int refbw_m1, refbh_m1;
    int th;
    VP56mv mv;

    mv.x = av_clip(in_mv->x, -(x + pw - px + 4) << 3, (s->cols * 8 - x + px + 3) << 3);
    mv.y = av_clip(in_mv->y, -(y + ph - py + 4) << 3, (s->rows * 8 - y + py + 3) << 3);
    // BUG libvpx seems to scale the two components separately. This introduces
    // rounding errors but we have to reproduce them to be exactly compatible
    // with the output from libvpx...
    mx = scale_mv(mv.x * 2, 0) + scale_mv(x * 16, 0);
    my = scale_mv(mv.y * 2, 1) + scale_mv(y * 16, 1);

    y = my >> 4;
    x = mx >> 4;
    ref += y * ref_stride + x * bytesperpixel;
    mx &= 15;
    my &= 15;
    refbw_m1 = ((bw - 1) * step[0] + mx) >> 4;
    refbh_m1 = ((bh - 1) * step[1] + my) >> 4;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + refbh_m1 + 4 + 7) >> 6;
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    if (x < 3 || y < 3 || x + 4 >= w - refbw_m1 || y + 4 >= h - refbh_m1) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref - 3 * ref_stride - 3 * bytesperpixel,
                                 288, ref_stride,
                                 refbw_m1 + 8, refbh_m1 + 8,
                                 x - 3, y - 3, w, h);
        ref = s->edge_emu_buffer + 3 * 288 + 3 * bytesperpixel;
        ref_stride = 288;
    }
    smc(dst, dst_stride, ref, ref_stride, bh, mx, my, step[0], step[1]);
    }
}

static av_always_inline void mc_chroma_scaled(VP9Context *s, vp9_scaled_mc_func smc,
                                              vp9_mc_func (*mc)[2],
                                              uint8_t *dst_u, uint8_t *dst_v,
                                              ptrdiff_t dst_stride,
                                              const uint8_t *ref_u, ptrdiff_t src_stride_u,
                                              const uint8_t *ref_v, ptrdiff_t src_stride_v,
                                              ThreadFrame *ref_frame,
                                              ptrdiff_t y, ptrdiff_t x, const VP56mv *in_mv,
                                              int px, int py, int pw, int ph,
                                              int bw, int bh, int w, int h, int bytesperpixel,
                                              const uint16_t *scale, const uint8_t *step)
{
    if (s->s.frames[CUR_FRAME].tf.f->width == ref_frame->f->width &&
        s->s.frames[CUR_FRAME].tf.f->height == ref_frame->f->height) {
        mc_chroma_unscaled(s, mc, dst_u, dst_v, dst_stride, ref_u, src_stride_u,
                           ref_v, src_stride_v, ref_frame,
                           y, x, in_mv, bw, bh, w, h, bytesperpixel);
    } else {
    int mx, my;
    int refbw_m1, refbh_m1;
    int th;
    VP56mv mv;

    if (s->ss_h) {
        // BUG https://code.google.com/p/webm/issues/detail?id=820
        mv.x = av_clip(in_mv->x, -(x + pw - px + 4) << 4, (s->cols * 4 - x + px + 3) << 4);
        mx = scale_mv(mv.x, 0) + (scale_mv(x * 16, 0) & ~15) + (scale_mv(x * 32, 0) & 15);
    } else {
        mv.x = av_clip(in_mv->x, -(x + pw - px + 4) << 3, (s->cols * 8 - x + px + 3) << 3);
        mx = scale_mv(mv.x << 1, 0) + scale_mv(x * 16, 0);
    }
    if (s->ss_v) {
        // BUG https://code.google.com/p/webm/issues/detail?id=820
        mv.y = av_clip(in_mv->y, -(y + ph - py + 4) << 4, (s->rows * 4 - y + py + 3) << 4);
        my = scale_mv(mv.y, 1) + (scale_mv(y * 16, 1) & ~15) + (scale_mv(y * 32, 1) & 15);
    } else {
        mv.y = av_clip(in_mv->y, -(y + ph - py + 4) << 3, (s->rows * 8 - y + py + 3) << 3);
        my = scale_mv(mv.y << 1, 1) + scale_mv(y * 16, 1);
    }
#undef scale_mv
    y = my >> 4;
    x = mx >> 4;
    ref_u += y * src_stride_u + x * bytesperpixel;
    ref_v += y * src_stride_v + x * bytesperpixel;
    mx &= 15;
    my &= 15;
    refbw_m1 = ((bw - 1) * step[0] + mx) >> 4;
    refbh_m1 = ((bh - 1) * step[1] + my) >> 4;
    // FIXME bilinear filter only needs 0/1 pixels, not 3/4
    // we use +7 because the last 7 pixels of each sbrow can be changed in
    // the longest loopfilter of the next sbrow
    th = (y + refbh_m1 + 4 + 7) >> (6 - s->ss_v);
    ff_thread_await_progress(ref_frame, FFMAX(th, 0), 0);
    if (x < 3 || y < 3 || x + 4 >= w - refbw_m1 || y + 4 >= h - refbh_m1) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_u - 3 * src_stride_u - 3 * bytesperpixel,
                                 288, src_stride_u,
                                 refbw_m1 + 8, refbh_m1 + 8,
                                 x - 3, y - 3, w, h);
        ref_u = s->edge_emu_buffer + 3 * 288 + 3 * bytesperpixel;
        smc(dst_u, dst_stride, ref_u, 288, bh, mx, my, step[0], step[1]);

        s->vdsp.emulated_edge_mc(s->edge_emu_buffer,
                                 ref_v - 3 * src_stride_v - 3 * bytesperpixel,
                                 288, src_stride_v,
                                 refbw_m1 + 8, refbh_m1 + 8,
                                 x - 3, y - 3, w, h);
        ref_v = s->edge_emu_buffer + 3 * 288 + 3 * bytesperpixel;
        smc(dst_v, dst_stride, ref_v, 288, bh, mx, my, step[0], step[1]);
    } else {
        smc(dst_u, dst_stride, ref_u, src_stride_u, bh, mx, my, step[0], step[1]);
        smc(dst_v, dst_stride, ref_v, src_stride_v, bh, mx, my, step[0], step[1]);
    }
    }
}

#define mc_luma_dir(s, mc, dst, dst_ls, src, src_ls, tref, row, col, mv, \
                    px, py, pw, ph, bw, bh, w, h, i) \
    mc_luma_scaled(s, s->dsp.s##mc, s->dsp.mc, dst, dst_ls, src, src_ls, tref, row, col, \
                   mv, px, py, pw, ph, bw, bh, w, h, bytesperpixel, \
                   s->mvscale[b->ref[i]], s->mvstep[b->ref[i]])
#define mc_chroma_dir(s, mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                      row, col, mv, px, py, pw, ph, bw, bh, w, h, i) \
    mc_chroma_scaled(s, s->dsp.s##mc, s->dsp.mc, dstu, dstv, dst_ls, srcu, srcu_ls, srcv, srcv_ls, tref, \
                     row, col, mv, px, py, pw, ph, bw, bh, w, h, bytesperpixel, \
                     s->mvscale[b->ref[i]], s->mvstep[b->ref[i]])
#define SCALED 1
#define FN(x) x##_scaled_8bpp
#define BYTES_PER_PIXEL 1
#include "vp9_mc_template.c"
#undef FN
#undef BYTES_PER_PIXEL
#define FN(x) x##_scaled_16bpp
#define BYTES_PER_PIXEL 2
#include "vp9_mc_template.c"
#undef mc_luma_dir
#undef mc_chroma_dir
#undef FN
#undef BYTES_PER_PIXEL
#undef SCALED

static av_always_inline void inter_recon(AVCodecContext *ctx, int bytesperpixel)
{
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    int row = s->row, col = s->col;

    if (s->mvscale[b->ref[0]][0] || (b->comp && s->mvscale[b->ref[1]][0])) {
        if (bytesperpixel == 1) {
            inter_pred_scaled_8bpp(ctx);
        } else {
            inter_pred_scaled_16bpp(ctx);
        }
    } else {
        if (bytesperpixel == 1) {
            inter_pred_8bpp(ctx);
        } else {
            inter_pred_16bpp(ctx);
        }
    }
    if (!b->skip) {
        /* mostly copied intra_recon() */

        int w4 = bwh_tab[1][b->bs][0] << 1, step1d = 1 << b->tx, n;
        int h4 = bwh_tab[1][b->bs][1] << 1, x, y, step = 1 << (b->tx * 2);
        int end_x = FFMIN(2 * (s->cols - col), w4);
        int end_y = FFMIN(2 * (s->rows - row), h4);
        int tx = 4 * s->s.h.lossless + b->tx, uvtx = b->uvtx + 4 * s->s.h.lossless;
        int uvstep1d = 1 << b->uvtx, p;
        uint8_t *dst = s->dst[0];

        // y itxfm add
        for (n = 0, y = 0; y < end_y; y += step1d) {
            uint8_t *ptr = dst;
            for (x = 0; x < end_x; x += step1d,
                 ptr += 4 * step1d * bytesperpixel, n += step) {
                int eob = b->tx > TX_8X8 ? AV_RN16A(&s->eob[n]) : s->eob[n];

                if (eob)
                    s->dsp.itxfm_add[tx][DCT_DCT](ptr, s->y_stride,
                                                  s->block + 16 * n * bytesperpixel, eob);
            }
            dst += 4 * s->y_stride * step1d;
        }

        // uv itxfm add
        end_x >>= s->ss_h;
        end_y >>= s->ss_v;
        step = 1 << (b->uvtx * 2);
        for (p = 0; p < 2; p++) {
            dst = s->dst[p + 1];
            for (n = 0, y = 0; y < end_y; y += uvstep1d) {
                uint8_t *ptr = dst;
                for (x = 0; x < end_x; x += uvstep1d,
                     ptr += 4 * uvstep1d * bytesperpixel, n += step) {
                    int eob = b->uvtx > TX_8X8 ? AV_RN16A(&s->uveob[p][n]) : s->uveob[p][n];

                    if (eob)
                        s->dsp.itxfm_add[uvtx][DCT_DCT](ptr, s->uv_stride,
                                                        s->uvblock[p] + 16 * n * bytesperpixel, eob);
                }
                dst += 4 * uvstep1d * s->uv_stride;
            }
        }
    }
}

static void inter_recon_8bpp(AVCodecContext *ctx)
{
    inter_recon(ctx, 1);
}

static void inter_recon_16bpp(AVCodecContext *ctx)
{
    inter_recon(ctx, 2);
}

static av_always_inline void mask_edges(uint8_t (*mask)[8][4], int ss_h, int ss_v,
                                        int row_and_7, int col_and_7,
                                        int w, int h, int col_end, int row_end,
                                        enum TxfmMode tx, int skip_inter)
{
    static const unsigned wide_filter_col_mask[2] = { 0x11, 0x01 };
    static const unsigned wide_filter_row_mask[2] = { 0x03, 0x07 };

    // FIXME I'm pretty sure all loops can be replaced by a single LUT if
    // we make VP9Filter.mask uint64_t (i.e. row/col all single variable)
    // and make the LUT 5-indexed (bl, bp, is_uv, tx and row/col), and then
    // use row_and_7/col_and_7 as shifts (1*col_and_7+8*row_and_7)

    // the intended behaviour of the vp9 loopfilter is to work on 8-pixel
    // edges. This means that for UV, we work on two subsampled blocks at
    // a time, and we only use the topleft block's mode information to set
    // things like block strength. Thus, for any block size smaller than
    // 16x16, ignore the odd portion of the block.
    if (tx == TX_4X4 && (ss_v | ss_h)) {
        if (h == ss_v) {
            if (row_and_7 & 1)
                return;
            if (!row_end)
                h += 1;
        }
        if (w == ss_h) {
            if (col_and_7 & 1)
                return;
            if (!col_end)
                w += 1;
        }
    }

    if (tx == TX_4X4 && !skip_inter) {
        int t = 1 << col_and_7, m_col = (t << w) - t, y;
        // on 32-px edges, use the 8-px wide loopfilter; else, use 4-px wide
        int m_row_8 = m_col & wide_filter_col_mask[ss_h], m_row_4 = m_col - m_row_8;

        for (y = row_and_7; y < h + row_and_7; y++) {
            int col_mask_id = 2 - !(y & wide_filter_row_mask[ss_v]);

            mask[0][y][1] |= m_row_8;
            mask[0][y][2] |= m_row_4;
            // for odd lines, if the odd col is not being filtered,
            // skip odd row also:
            // .---. <-- a
            // |   |
            // |___| <-- b
            // ^   ^
            // c   d
            //
            // if a/c are even row/col and b/d are odd, and d is skipped,
            // e.g. right edge of size-66x66.webm, then skip b also (bug)
            if ((ss_h & ss_v) && (col_end & 1) && (y & 1)) {
                mask[1][y][col_mask_id] |= (t << (w - 1)) - t;
            } else {
                mask[1][y][col_mask_id] |= m_col;
            }
            if (!ss_h)
                mask[0][y][3] |= m_col;
            if (!ss_v) {
                if (ss_h && (col_end & 1))
                    mask[1][y][3] |= (t << (w - 1)) - t;
                else
                    mask[1][y][3] |= m_col;
            }
        }
    } else {
        int y, t = 1 << col_and_7, m_col = (t << w) - t;

        if (!skip_inter) {
            int mask_id = (tx == TX_8X8);
            static const unsigned masks[4] = { 0xff, 0x55, 0x11, 0x01 };
            int l2 = tx + ss_h - 1, step1d;
            int m_row = m_col & masks[l2];

            // at odd UV col/row edges tx16/tx32 loopfilter edges, force
            // 8wd loopfilter to prevent going off the visible edge.
            if (ss_h && tx > TX_8X8 && (w ^ (w - 1)) == 1) {
                int m_row_16 = ((t << (w - 1)) - t) & masks[l2];
                int m_row_8 = m_row - m_row_16;

                for (y = row_and_7; y < h + row_and_7; y++) {
                    mask[0][y][0] |= m_row_16;
                    mask[0][y][1] |= m_row_8;
                }
            } else {
                for (y = row_and_7; y < h + row_and_7; y++)
                    mask[0][y][mask_id] |= m_row;
            }

            l2 = tx + ss_v - 1;
            step1d = 1 << l2;
            if (ss_v && tx > TX_8X8 && (h ^ (h - 1)) == 1) {
                for (y = row_and_7; y < h + row_and_7 - 1; y += step1d)
                    mask[1][y][0] |= m_col;
                if (y - row_and_7 == h - 1)
                    mask[1][y][1] |= m_col;
            } else {
                for (y = row_and_7; y < h + row_and_7; y += step1d)
                    mask[1][y][mask_id] |= m_col;
            }
        } else if (tx != TX_4X4) {
            int mask_id;

            mask_id = (tx == TX_8X8) || (h == ss_v);
            mask[1][row_and_7][mask_id] |= m_col;
            mask_id = (tx == TX_8X8) || (w == ss_h);
            for (y = row_and_7; y < h + row_and_7; y++)
                mask[0][y][mask_id] |= t;
        } else {
            int t8 = t & wide_filter_col_mask[ss_h], t4 = t - t8;

            for (y = row_and_7; y < h + row_and_7; y++) {
                mask[0][y][2] |= t4;
                mask[0][y][1] |= t8;
            }
            mask[1][row_and_7][2 - !(row_and_7 & wide_filter_row_mask[ss_v])] |= m_col;
        }
    }
}

static void decode_b(AVCodecContext *ctx, int row, int col,
                     struct VP9Filter *lflvl, ptrdiff_t yoff, ptrdiff_t uvoff,
                     enum BlockLevel bl, enum BlockPartition bp)
{
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    enum BlockSize bs = bl * 3 + bp;
    int bytesperpixel = s->bytesperpixel;
    int w4 = bwh_tab[1][bs][0], h4 = bwh_tab[1][bs][1], lvl;
    int emu[2];
    AVFrame *f = s->s.frames[CUR_FRAME].tf.f;

    s->row = row;
    s->row7 = row & 7;
    s->col = col;
    s->col7 = col & 7;
    s->min_mv.x = -(128 + col * 64);
    s->min_mv.y = -(128 + row * 64);
    s->max_mv.x = 128 + (s->cols - col - w4) * 64;
    s->max_mv.y = 128 + (s->rows - row - h4) * 64;
    if (s->pass < 2) {
        b->bs = bs;
        b->bl = bl;
        b->bp = bp;
        decode_mode(ctx);
        b->uvtx = b->tx - ((s->ss_h && w4 * 2 == (1 << b->tx)) ||
                           (s->ss_v && h4 * 2 == (1 << b->tx)));

        if (!b->skip) {
            int has_coeffs;

            if (bytesperpixel == 1) {
                has_coeffs = decode_coeffs_8bpp(ctx);
            } else {
                has_coeffs = decode_coeffs_16bpp(ctx);
            }
            if (!has_coeffs && b->bs <= BS_8x8 && !b->intra) {
                b->skip = 1;
                memset(&s->above_skip_ctx[col], 1, w4);
                memset(&s->left_skip_ctx[s->row7], 1, h4);
            }
        } else {
            int row7 = s->row7;

#define SPLAT_ZERO_CTX(v, n) \
    switch (n) { \
    case 1:  v = 0;          break; \
    case 2:  AV_ZERO16(&v);  break; \
    case 4:  AV_ZERO32(&v);  break; \
    case 8:  AV_ZERO64(&v);  break; \
    case 16: AV_ZERO128(&v); break; \
    }
#define SPLAT_ZERO_YUV(dir, var, off, n, dir2) \
    do { \
        SPLAT_ZERO_CTX(s->dir##_y_##var[off * 2], n * 2); \
        if (s->ss_##dir2) { \
            SPLAT_ZERO_CTX(s->dir##_uv_##var[0][off], n); \
            SPLAT_ZERO_CTX(s->dir##_uv_##var[1][off], n); \
        } else { \
            SPLAT_ZERO_CTX(s->dir##_uv_##var[0][off * 2], n * 2); \
            SPLAT_ZERO_CTX(s->dir##_uv_##var[1][off * 2], n * 2); \
        } \
    } while (0)

            switch (w4) {
            case 1: SPLAT_ZERO_YUV(above, nnz_ctx, col, 1, h); break;
            case 2: SPLAT_ZERO_YUV(above, nnz_ctx, col, 2, h); break;
            case 4: SPLAT_ZERO_YUV(above, nnz_ctx, col, 4, h); break;
            case 8: SPLAT_ZERO_YUV(above, nnz_ctx, col, 8, h); break;
            }
            switch (h4) {
            case 1: SPLAT_ZERO_YUV(left, nnz_ctx, row7, 1, v); break;
            case 2: SPLAT_ZERO_YUV(left, nnz_ctx, row7, 2, v); break;
            case 4: SPLAT_ZERO_YUV(left, nnz_ctx, row7, 4, v); break;
            case 8: SPLAT_ZERO_YUV(left, nnz_ctx, row7, 8, v); break;
            }
        }

        if (s->pass == 1) {
            s->b++;
            s->block += w4 * h4 * 64 * bytesperpixel;
            s->uvblock[0] += w4 * h4 * 64 * bytesperpixel >> (s->ss_h + s->ss_v);
            s->uvblock[1] += w4 * h4 * 64 * bytesperpixel >> (s->ss_h + s->ss_v);
            s->eob += 4 * w4 * h4;
            s->uveob[0] += 4 * w4 * h4 >> (s->ss_h + s->ss_v);
            s->uveob[1] += 4 * w4 * h4 >> (s->ss_h + s->ss_v);

            return;
        }
    }

    // emulated overhangs if the stride of the target buffer can't hold. This
    // makes it possible to support emu-edge and so on even if we have large block
    // overhangs
    emu[0] = (col + w4) * 8 * bytesperpixel > f->linesize[0] ||
             (row + h4) > s->rows;
    emu[1] = ((col + w4) * 8 >> s->ss_h) * bytesperpixel > f->linesize[1] ||
             (row + h4) > s->rows;
    if (emu[0]) {
        s->dst[0] = s->tmp_y;
        s->y_stride = 128;
    } else {
        s->dst[0] = f->data[0] + yoff;
        s->y_stride = f->linesize[0];
    }
    if (emu[1]) {
        s->dst[1] = s->tmp_uv[0];
        s->dst[2] = s->tmp_uv[1];
        s->uv_stride = 128;
    } else {
        s->dst[1] = f->data[1] + uvoff;
        s->dst[2] = f->data[2] + uvoff;
        s->uv_stride = f->linesize[1];
    }
    if (b->intra) {
        if (s->bpp > 8) {
            intra_recon_16bpp(ctx, yoff, uvoff);
        } else {
            intra_recon_8bpp(ctx, yoff, uvoff);
        }
    } else {
        if (s->bpp > 8) {
            inter_recon_16bpp(ctx);
        } else {
            inter_recon_8bpp(ctx);
        }
    }
    if (emu[0]) {
        int w = FFMIN(s->cols - col, w4) * 8, h = FFMIN(s->rows - row, h4) * 8, n, o = 0;

        for (n = 0; o < w; n++) {
            int bw = 64 >> n;

            av_assert2(n <= 4);
            if (w & bw) {
                s->dsp.mc[n][0][0][0][0](f->data[0] + yoff + o * bytesperpixel, f->linesize[0],
                                         s->tmp_y + o * bytesperpixel, 128, h, 0, 0);
                o += bw;
            }
        }
    }
    if (emu[1]) {
        int w = FFMIN(s->cols - col, w4) * 8 >> s->ss_h;
        int h = FFMIN(s->rows - row, h4) * 8 >> s->ss_v, n, o = 0;

        for (n = s->ss_h; o < w; n++) {
            int bw = 64 >> n;

            av_assert2(n <= 4);
            if (w & bw) {
                s->dsp.mc[n][0][0][0][0](f->data[1] + uvoff + o * bytesperpixel, f->linesize[1],
                                         s->tmp_uv[0] + o * bytesperpixel, 128, h, 0, 0);
                s->dsp.mc[n][0][0][0][0](f->data[2] + uvoff + o * bytesperpixel, f->linesize[2],
                                         s->tmp_uv[1] + o * bytesperpixel, 128, h, 0, 0);
                o += bw;
            }
        }
    }

    // pick filter level and find edges to apply filter to
    if (s->s.h.filter.level &&
        (lvl = s->s.h.segmentation.feat[b->seg_id].lflvl[b->intra ? 0 : b->ref[0] + 1]
                                                      [b->mode[3] != ZEROMV]) > 0) {
        int x_end = FFMIN(s->cols - col, w4), y_end = FFMIN(s->rows - row, h4);
        int skip_inter = !b->intra && b->skip, col7 = s->col7, row7 = s->row7;

        setctx_2d(&lflvl->level[row7 * 8 + col7], w4, h4, 8, lvl);
        mask_edges(lflvl->mask[0], 0, 0, row7, col7, x_end, y_end, 0, 0, b->tx, skip_inter);
        if (s->ss_h || s->ss_v)
            mask_edges(lflvl->mask[1], s->ss_h, s->ss_v, row7, col7, x_end, y_end,
                       s->cols & 1 && col + w4 >= s->cols ? s->cols & 7 : 0,
                       s->rows & 1 && row + h4 >= s->rows ? s->rows & 7 : 0,
                       b->uvtx, skip_inter);

        if (!s->filter_lut.lim_lut[lvl]) {
            int sharp = s->s.h.filter.sharpness;
            int limit = lvl;

            if (sharp > 0) {
                limit >>= (sharp + 3) >> 2;
                limit = FFMIN(limit, 9 - sharp);
            }
            limit = FFMAX(limit, 1);

            s->filter_lut.lim_lut[lvl] = limit;
            s->filter_lut.mblim_lut[lvl] = 2 * (lvl + 2) + limit;
        }
    }

    if (s->pass == 2) {
        s->b++;
        s->block += w4 * h4 * 64 * bytesperpixel;
        s->uvblock[0] += w4 * h4 * 64 * bytesperpixel >> (s->ss_v + s->ss_h);
        s->uvblock[1] += w4 * h4 * 64 * bytesperpixel >> (s->ss_v + s->ss_h);
        s->eob += 4 * w4 * h4;
        s->uveob[0] += 4 * w4 * h4 >> (s->ss_v + s->ss_h);
        s->uveob[1] += 4 * w4 * h4 >> (s->ss_v + s->ss_h);
    }
}

static void decode_sb(AVCodecContext *ctx, int row, int col, struct VP9Filter *lflvl,
                      ptrdiff_t yoff, ptrdiff_t uvoff, enum BlockLevel bl)
{
    VP9Context *s = ctx->priv_data;
    int c = ((s->above_partition_ctx[col] >> (3 - bl)) & 1) |
            (((s->left_partition_ctx[row & 0x7] >> (3 - bl)) & 1) << 1);
    const uint8_t *p = s->s.h.keyframe || s->s.h.intraonly ? vp9_default_kf_partition_probs[bl][c] :
                                                     s->prob.p.partition[bl][c];
    enum BlockPartition bp;
    ptrdiff_t hbs = 4 >> bl;
    AVFrame *f = s->s.frames[CUR_FRAME].tf.f;
    ptrdiff_t y_stride = f->linesize[0], uv_stride = f->linesize[1];
    int bytesperpixel = s->bytesperpixel;

    if (bl == BL_8X8) {
        bp = vp8_rac_get_tree(&s->c, vp9_partition_tree, p);
        decode_b(ctx, row, col, lflvl, yoff, uvoff, bl, bp);
    } else if (col + hbs < s->cols) { // FIXME why not <=?
        if (row + hbs < s->rows) { // FIXME why not <=?
            bp = vp8_rac_get_tree(&s->c, vp9_partition_tree, p);
            switch (bp) {
            case PARTITION_NONE:
                decode_b(ctx, row, col, lflvl, yoff, uvoff, bl, bp);
                break;
            case PARTITION_H:
                decode_b(ctx, row, col, lflvl, yoff, uvoff, bl, bp);
                yoff  += hbs * 8 * y_stride;
                uvoff += hbs * 8 * uv_stride >> s->ss_v;
                decode_b(ctx, row + hbs, col, lflvl, yoff, uvoff, bl, bp);
                break;
            case PARTITION_V:
                decode_b(ctx, row, col, lflvl, yoff, uvoff, bl, bp);
                yoff  += hbs * 8 * bytesperpixel;
                uvoff += hbs * 8 * bytesperpixel >> s->ss_h;
                decode_b(ctx, row, col + hbs, lflvl, yoff, uvoff, bl, bp);
                break;
            case PARTITION_SPLIT:
                decode_sb(ctx, row, col, lflvl, yoff, uvoff, bl + 1);
                decode_sb(ctx, row, col + hbs, lflvl,
                          yoff + 8 * hbs * bytesperpixel,
                          uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
                yoff  += hbs * 8 * y_stride;
                uvoff += hbs * 8 * uv_stride >> s->ss_v;
                decode_sb(ctx, row + hbs, col, lflvl, yoff, uvoff, bl + 1);
                decode_sb(ctx, row + hbs, col + hbs, lflvl,
                          yoff + 8 * hbs * bytesperpixel,
                          uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
                break;
            default:
                av_assert0(0);
            }
        } else if (vp56_rac_get_prob_branchy(&s->c, p[1])) {
            bp = PARTITION_SPLIT;
            decode_sb(ctx, row, col, lflvl, yoff, uvoff, bl + 1);
            decode_sb(ctx, row, col + hbs, lflvl,
                      yoff + 8 * hbs * bytesperpixel,
                      uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
        } else {
            bp = PARTITION_H;
            decode_b(ctx, row, col, lflvl, yoff, uvoff, bl, bp);
        }
    } else if (row + hbs < s->rows) { // FIXME why not <=?
        if (vp56_rac_get_prob_branchy(&s->c, p[2])) {
            bp = PARTITION_SPLIT;
            decode_sb(ctx, row, col, lflvl, yoff, uvoff, bl + 1);
            yoff  += hbs * 8 * y_stride;
            uvoff += hbs * 8 * uv_stride >> s->ss_v;
            decode_sb(ctx, row + hbs, col, lflvl, yoff, uvoff, bl + 1);
        } else {
            bp = PARTITION_V;
            decode_b(ctx, row, col, lflvl, yoff, uvoff, bl, bp);
        }
    } else {
        bp = PARTITION_SPLIT;
        decode_sb(ctx, row, col, lflvl, yoff, uvoff, bl + 1);
    }
    s->counts.partition[bl][c][bp]++;
}

static void decode_sb_mem(AVCodecContext *ctx, int row, int col, struct VP9Filter *lflvl,
                          ptrdiff_t yoff, ptrdiff_t uvoff, enum BlockLevel bl)
{
    VP9Context *s = ctx->priv_data;
    VP9Block *b = s->b;
    ptrdiff_t hbs = 4 >> bl;
    AVFrame *f = s->s.frames[CUR_FRAME].tf.f;
    ptrdiff_t y_stride = f->linesize[0], uv_stride = f->linesize[1];
    int bytesperpixel = s->bytesperpixel;

    if (bl == BL_8X8) {
        av_assert2(b->bl == BL_8X8);
        decode_b(ctx, row, col, lflvl, yoff, uvoff, b->bl, b->bp);
    } else if (s->b->bl == bl) {
        decode_b(ctx, row, col, lflvl, yoff, uvoff, b->bl, b->bp);
        if (b->bp == PARTITION_H && row + hbs < s->rows) {
            yoff  += hbs * 8 * y_stride;
            uvoff += hbs * 8 * uv_stride >> s->ss_v;
            decode_b(ctx, row + hbs, col, lflvl, yoff, uvoff, b->bl, b->bp);
        } else if (b->bp == PARTITION_V && col + hbs < s->cols) {
            yoff  += hbs * 8 * bytesperpixel;
            uvoff += hbs * 8 * bytesperpixel >> s->ss_h;
            decode_b(ctx, row, col + hbs, lflvl, yoff, uvoff, b->bl, b->bp);
        }
    } else {
        decode_sb_mem(ctx, row, col, lflvl, yoff, uvoff, bl + 1);
        if (col + hbs < s->cols) { // FIXME why not <=?
            if (row + hbs < s->rows) {
                decode_sb_mem(ctx, row, col + hbs, lflvl, yoff + 8 * hbs * bytesperpixel,
                              uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
                yoff  += hbs * 8 * y_stride;
                uvoff += hbs * 8 * uv_stride >> s->ss_v;
                decode_sb_mem(ctx, row + hbs, col, lflvl, yoff, uvoff, bl + 1);
                decode_sb_mem(ctx, row + hbs, col + hbs, lflvl,
                              yoff + 8 * hbs * bytesperpixel,
                              uvoff + (8 * hbs * bytesperpixel >> s->ss_h), bl + 1);
            } else {
                yoff  += hbs * 8 * bytesperpixel;
                uvoff += hbs * 8 * bytesperpixel >> s->ss_h;
                decode_sb_mem(ctx, row, col + hbs, lflvl, yoff, uvoff, bl + 1);
            }
        } else if (row + hbs < s->rows) {
            yoff  += hbs * 8 * y_stride;
            uvoff += hbs * 8 * uv_stride >> s->ss_v;
            decode_sb_mem(ctx, row + hbs, col, lflvl, yoff, uvoff, bl + 1);
        }
    }
}

static av_always_inline void filter_plane_cols(VP9Context *s, int col, int ss_h, int ss_v,
                                               uint8_t *lvl, uint8_t (*mask)[4],
                                               uint8_t *dst, ptrdiff_t ls)
{
    int y, x, bytesperpixel = s->bytesperpixel;

    // filter edges between columns (e.g. block1 | block2)
    for (y = 0; y < 8; y += 2 << ss_v, dst += 16 * ls, lvl += 16 << ss_v) {
        uint8_t *ptr = dst, *l = lvl, *hmask1 = mask[y], *hmask2 = mask[y + 1 + ss_v];
        unsigned hm1 = hmask1[0] | hmask1[1] | hmask1[2], hm13 = hmask1[3];
        unsigned hm2 = hmask2[1] | hmask2[2], hm23 = hmask2[3];
        unsigned hm = hm1 | hm2 | hm13 | hm23;

        for (x = 1; hm & ~(x - 1); x <<= 1, ptr += 8 * bytesperpixel >> ss_h) {
            if (col || x > 1) {
                if (hm1 & x) {
                    int L = *l, H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    if (hmask1[0] & x) {
                        if (hmask2[0] & x) {
                            av_assert2(l[8 << ss_v] == L);
                            s->dsp.loop_filter_16[0](ptr, ls, E, I, H);
                        } else {
                            s->dsp.loop_filter_8[2][0](ptr, ls, E, I, H);
                        }
                    } else if (hm2 & x) {
                        L = l[8 << ss_v];
                        H |= (L >> 4) << 8;
                        E |= s->filter_lut.mblim_lut[L] << 8;
                        I |= s->filter_lut.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[!!(hmask1[1] & x)]
                                               [!!(hmask2[1] & x)]
                                               [0](ptr, ls, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[!!(hmask1[1] & x)]
                                            [0](ptr, ls, E, I, H);
                    }
                } else if (hm2 & x) {
                    int L = l[8 << ss_v], H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    s->dsp.loop_filter_8[!!(hmask2[1] & x)]
                                        [0](ptr + 8 * ls, ls, E, I, H);
                }
            }
            if (ss_h) {
                if (x & 0xAA)
                    l += 2;
            } else {
                if (hm13 & x) {
                    int L = *l, H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    if (hm23 & x) {
                        L = l[8 << ss_v];
                        H |= (L >> 4) << 8;
                        E |= s->filter_lut.mblim_lut[L] << 8;
                        I |= s->filter_lut.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[0][0][0](ptr + 4 * bytesperpixel, ls, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[0][0](ptr + 4 * bytesperpixel, ls, E, I, H);
                    }
                } else if (hm23 & x) {
                    int L = l[8 << ss_v], H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    s->dsp.loop_filter_8[0][0](ptr + 8 * ls + 4 * bytesperpixel, ls, E, I, H);
                }
                l++;
            }
        }
    }
}

static av_always_inline void filter_plane_rows(VP9Context *s, int row, int ss_h, int ss_v,
                                               uint8_t *lvl, uint8_t (*mask)[4],
                                               uint8_t *dst, ptrdiff_t ls)
{
    int y, x, bytesperpixel = s->bytesperpixel;

    //                                 block1
    // filter edges between rows (e.g. ------)
    //                                 block2
    for (y = 0; y < 8; y++, dst += 8 * ls >> ss_v) {
        uint8_t *ptr = dst, *l = lvl, *vmask = mask[y];
        unsigned vm = vmask[0] | vmask[1] | vmask[2], vm3 = vmask[3];

        for (x = 1; vm & ~(x - 1); x <<= (2 << ss_h), ptr += 16 * bytesperpixel, l += 2 << ss_h) {
            if (row || y) {
                if (vm & x) {
                    int L = *l, H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    if (vmask[0] & x) {
                        if (vmask[0] & (x << (1 + ss_h))) {
                            av_assert2(l[1 + ss_h] == L);
                            s->dsp.loop_filter_16[1](ptr, ls, E, I, H);
                        } else {
                            s->dsp.loop_filter_8[2][1](ptr, ls, E, I, H);
                        }
                    } else if (vm & (x << (1 + ss_h))) {
                        L = l[1 + ss_h];
                        H |= (L >> 4) << 8;
                        E |= s->filter_lut.mblim_lut[L] << 8;
                        I |= s->filter_lut.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[!!(vmask[1] &  x)]
                                               [!!(vmask[1] & (x << (1 + ss_h)))]
                                               [1](ptr, ls, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[!!(vmask[1] & x)]
                                            [1](ptr, ls, E, I, H);
                    }
                } else if (vm & (x << (1 + ss_h))) {
                    int L = l[1 + ss_h], H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    s->dsp.loop_filter_8[!!(vmask[1] & (x << (1 + ss_h)))]
                                        [1](ptr + 8 * bytesperpixel, ls, E, I, H);
                }
            }
            if (!ss_v) {
                if (vm3 & x) {
                    int L = *l, H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    if (vm3 & (x << (1 + ss_h))) {
                        L = l[1 + ss_h];
                        H |= (L >> 4) << 8;
                        E |= s->filter_lut.mblim_lut[L] << 8;
                        I |= s->filter_lut.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[0][0][1](ptr + ls * 4, ls, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[0][1](ptr + ls * 4, ls, E, I, H);
                    }
                } else if (vm3 & (x << (1 + ss_h))) {
                    int L = l[1 + ss_h], H = L >> 4;
                    int E = s->filter_lut.mblim_lut[L], I = s->filter_lut.lim_lut[L];

                    s->dsp.loop_filter_8[0][1](ptr + ls * 4 + 8 * bytesperpixel, ls, E, I, H);
                }
            }
        }
        if (ss_v) {
            if (y & 1)
                lvl += 16;
        } else {
            lvl += 8;
        }
    }
}

static void loopfilter_sb(AVCodecContext *ctx, struct VP9Filter *lflvl,
                          int row, int col, ptrdiff_t yoff, ptrdiff_t uvoff)
{
    VP9Context *s = ctx->priv_data;
    AVFrame *f = s->s.frames[CUR_FRAME].tf.f;
    uint8_t *dst = f->data[0] + yoff;
    ptrdiff_t ls_y = f->linesize[0], ls_uv = f->linesize[1];
    uint8_t (*uv_masks)[8][4] = lflvl->mask[s->ss_h | s->ss_v];
    int p;

    // FIXME in how far can we interleave the v/h loopfilter calls? E.g.
    // if you think of them as acting on a 8x8 block max, we can interleave
    // each v/h within the single x loop, but that only works if we work on
    // 8 pixel blocks, and we won't always do that (we want at least 16px
    // to use SSE2 optimizations, perhaps 32 for AVX2)

    filter_plane_cols(s, col, 0, 0, lflvl->level, lflvl->mask[0][0], dst, ls_y);
    filter_plane_rows(s, row, 0, 0, lflvl->level, lflvl->mask[0][1], dst, ls_y);

    for (p = 0; p < 2; p++) {
        dst = f->data[1 + p] + uvoff;
        filter_plane_cols(s, col, s->ss_h, s->ss_v, lflvl->level, uv_masks[0], dst, ls_uv);
        filter_plane_rows(s, row, s->ss_h, s->ss_v, lflvl->level, uv_masks[1], dst, ls_uv);
    }
}

static void set_tile_offset(int *start, int *end, int idx, int log2_n, int n)
{
    int sb_start = ( idx      * n) >> log2_n;
    int sb_end   = ((idx + 1) * n) >> log2_n;
    *start = FFMIN(sb_start, n) << 3;
    *end   = FFMIN(sb_end,   n) << 3;
}

static av_always_inline void adapt_prob(uint8_t *p, unsigned ct0, unsigned ct1,
                                        int max_count, int update_factor)
{
    unsigned ct = ct0 + ct1, p2, p1;

    if (!ct)
        return;

    p1 = *p;
    p2 = ((ct0 << 8) + (ct >> 1)) / ct;
    p2 = av_clip(p2, 1, 255);
    ct = FFMIN(ct, max_count);
    update_factor = FASTDIV(update_factor * ct, max_count);

    // (p1 * (256 - update_factor) + p2 * update_factor + 128) >> 8
    *p = p1 + (((p2 - p1) * update_factor + 128) >> 8);
}

static void adapt_probs(VP9Context *s)
{
    int i, j, k, l, m;
    prob_context *p = &s->prob_ctx[s->s.h.framectxid].p;
    int uf = (s->s.h.keyframe || s->s.h.intraonly || !s->last_keyframe) ? 112 : 128;

    // coefficients
    for (i = 0; i < 4; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 2; k++)
                for (l = 0; l < 6; l++)
                    for (m = 0; m < 6; m++) {
                        uint8_t *pp = s->prob_ctx[s->s.h.framectxid].coef[i][j][k][l][m];
                        unsigned *e = s->counts.eob[i][j][k][l][m];
                        unsigned *c = s->counts.coef[i][j][k][l][m];

                        if (l == 0 && m >= 3) // dc only has 3 pt
                            break;

                        adapt_prob(&pp[0], e[0], e[1], 24, uf);
                        adapt_prob(&pp[1], c[0], c[1] + c[2], 24, uf);
                        adapt_prob(&pp[2], c[1], c[2], 24, uf);
                    }

    if (s->s.h.keyframe || s->s.h.intraonly) {
        memcpy(p->skip,  s->prob.p.skip,  sizeof(p->skip));
        memcpy(p->tx32p, s->prob.p.tx32p, sizeof(p->tx32p));
        memcpy(p->tx16p, s->prob.p.tx16p, sizeof(p->tx16p));
        memcpy(p->tx8p,  s->prob.p.tx8p,  sizeof(p->tx8p));
        return;
    }

    // skip flag
    for (i = 0; i < 3; i++)
        adapt_prob(&p->skip[i], s->counts.skip[i][0], s->counts.skip[i][1], 20, 128);

    // intra/inter flag
    for (i = 0; i < 4; i++)
        adapt_prob(&p->intra[i], s->counts.intra[i][0], s->counts.intra[i][1], 20, 128);

    // comppred flag
    if (s->s.h.comppredmode == PRED_SWITCHABLE) {
      for (i = 0; i < 5; i++)
          adapt_prob(&p->comp[i], s->counts.comp[i][0], s->counts.comp[i][1], 20, 128);
    }

    // reference frames
    if (s->s.h.comppredmode != PRED_SINGLEREF) {
      for (i = 0; i < 5; i++)
          adapt_prob(&p->comp_ref[i], s->counts.comp_ref[i][0],
                     s->counts.comp_ref[i][1], 20, 128);
    }

    if (s->s.h.comppredmode != PRED_COMPREF) {
      for (i = 0; i < 5; i++) {
          uint8_t *pp = p->single_ref[i];
          unsigned (*c)[2] = s->counts.single_ref[i];

          adapt_prob(&pp[0], c[0][0], c[0][1], 20, 128);
          adapt_prob(&pp[1], c[1][0], c[1][1], 20, 128);
      }
    }

    // block partitioning
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            uint8_t *pp = p->partition[i][j];
            unsigned *c = s->counts.partition[i][j];

            adapt_prob(&pp[0], c[0], c[1] + c[2] + c[3], 20, 128);
            adapt_prob(&pp[1], c[1], c[2] + c[3], 20, 128);
            adapt_prob(&pp[2], c[2], c[3], 20, 128);
        }

    // tx size
    if (s->s.h.txfmmode == TX_SWITCHABLE) {
      for (i = 0; i < 2; i++) {
          unsigned *c16 = s->counts.tx16p[i], *c32 = s->counts.tx32p[i];

          adapt_prob(&p->tx8p[i], s->counts.tx8p[i][0], s->counts.tx8p[i][1], 20, 128);
          adapt_prob(&p->tx16p[i][0], c16[0], c16[1] + c16[2], 20, 128);
          adapt_prob(&p->tx16p[i][1], c16[1], c16[2], 20, 128);
          adapt_prob(&p->tx32p[i][0], c32[0], c32[1] + c32[2] + c32[3], 20, 128);
          adapt_prob(&p->tx32p[i][1], c32[1], c32[2] + c32[3], 20, 128);
          adapt_prob(&p->tx32p[i][2], c32[2], c32[3], 20, 128);
      }
    }

    // interpolation filter
    if (s->s.h.filtermode == FILTER_SWITCHABLE) {
        for (i = 0; i < 4; i++) {
            uint8_t *pp = p->filter[i];
            unsigned *c = s->counts.filter[i];

            adapt_prob(&pp[0], c[0], c[1] + c[2], 20, 128);
            adapt_prob(&pp[1], c[1], c[2], 20, 128);
        }
    }

    // inter modes
    for (i = 0; i < 7; i++) {
        uint8_t *pp = p->mv_mode[i];
        unsigned *c = s->counts.mv_mode[i];

        adapt_prob(&pp[0], c[2], c[1] + c[0] + c[3], 20, 128);
        adapt_prob(&pp[1], c[0], c[1] + c[3], 20, 128);
        adapt_prob(&pp[2], c[1], c[3], 20, 128);
    }

    // mv joints
    {
        uint8_t *pp = p->mv_joint;
        unsigned *c = s->counts.mv_joint;

        adapt_prob(&pp[0], c[0], c[1] + c[2] + c[3], 20, 128);
        adapt_prob(&pp[1], c[1], c[2] + c[3], 20, 128);
        adapt_prob(&pp[2], c[2], c[3], 20, 128);
    }

    // mv components
    for (i = 0; i < 2; i++) {
        uint8_t *pp;
        unsigned *c, (*c2)[2], sum;

        adapt_prob(&p->mv_comp[i].sign, s->counts.mv_comp[i].sign[0],
                   s->counts.mv_comp[i].sign[1], 20, 128);

        pp = p->mv_comp[i].classes;
        c = s->counts.mv_comp[i].classes;
        sum = c[1] + c[2] + c[3] + c[4] + c[5] + c[6] + c[7] + c[8] + c[9] + c[10];
        adapt_prob(&pp[0], c[0], sum, 20, 128);
        sum -= c[1];
        adapt_prob(&pp[1], c[1], sum, 20, 128);
        sum -= c[2] + c[3];
        adapt_prob(&pp[2], c[2] + c[3], sum, 20, 128);
        adapt_prob(&pp[3], c[2], c[3], 20, 128);
        sum -= c[4] + c[5];
        adapt_prob(&pp[4], c[4] + c[5], sum, 20, 128);
        adapt_prob(&pp[5], c[4], c[5], 20, 128);
        sum -= c[6];
        adapt_prob(&pp[6], c[6], sum, 20, 128);
        adapt_prob(&pp[7], c[7] + c[8], c[9] + c[10], 20, 128);
        adapt_prob(&pp[8], c[7], c[8], 20, 128);
        adapt_prob(&pp[9], c[9], c[10], 20, 128);

        adapt_prob(&p->mv_comp[i].class0, s->counts.mv_comp[i].class0[0],
                   s->counts.mv_comp[i].class0[1], 20, 128);
        pp = p->mv_comp[i].bits;
        c2 = s->counts.mv_comp[i].bits;
        for (j = 0; j < 10; j++)
            adapt_prob(&pp[j], c2[j][0], c2[j][1], 20, 128);

        for (j = 0; j < 2; j++) {
            pp = p->mv_comp[i].class0_fp[j];
            c = s->counts.mv_comp[i].class0_fp[j];
            adapt_prob(&pp[0], c[0], c[1] + c[2] + c[3], 20, 128);
            adapt_prob(&pp[1], c[1], c[2] + c[3], 20, 128);
            adapt_prob(&pp[2], c[2], c[3], 20, 128);
        }
        pp = p->mv_comp[i].fp;
        c = s->counts.mv_comp[i].fp;
        adapt_prob(&pp[0], c[0], c[1] + c[2] + c[3], 20, 128);
        adapt_prob(&pp[1], c[1], c[2] + c[3], 20, 128);
        adapt_prob(&pp[2], c[2], c[3], 20, 128);

        if (s->s.h.highprecisionmvs) {
            adapt_prob(&p->mv_comp[i].class0_hp, s->counts.mv_comp[i].class0_hp[0],
                       s->counts.mv_comp[i].class0_hp[1], 20, 128);
            adapt_prob(&p->mv_comp[i].hp, s->counts.mv_comp[i].hp[0],
                       s->counts.mv_comp[i].hp[1], 20, 128);
        }
    }

    // y intra modes
    for (i = 0; i < 4; i++) {
        uint8_t *pp = p->y_mode[i];
        unsigned *c = s->counts.y_mode[i], sum, s2;

        sum = c[0] + c[1] + c[3] + c[4] + c[5] + c[6] + c[7] + c[8] + c[9];
        adapt_prob(&pp[0], c[DC_PRED], sum, 20, 128);
        sum -= c[TM_VP8_PRED];
        adapt_prob(&pp[1], c[TM_VP8_PRED], sum, 20, 128);
        sum -= c[VERT_PRED];
        adapt_prob(&pp[2], c[VERT_PRED], sum, 20, 128);
        s2 = c[HOR_PRED] + c[DIAG_DOWN_RIGHT_PRED] + c[VERT_RIGHT_PRED];
        sum -= s2;
        adapt_prob(&pp[3], s2, sum, 20, 128);
        s2 -= c[HOR_PRED];
        adapt_prob(&pp[4], c[HOR_PRED], s2, 20, 128);
        adapt_prob(&pp[5], c[DIAG_DOWN_RIGHT_PRED], c[VERT_RIGHT_PRED], 20, 128);
        sum -= c[DIAG_DOWN_LEFT_PRED];
        adapt_prob(&pp[6], c[DIAG_DOWN_LEFT_PRED], sum, 20, 128);
        sum -= c[VERT_LEFT_PRED];
        adapt_prob(&pp[7], c[VERT_LEFT_PRED], sum, 20, 128);
        adapt_prob(&pp[8], c[HOR_DOWN_PRED], c[HOR_UP_PRED], 20, 128);
    }

    // uv intra modes
    for (i = 0; i < 10; i++) {
        uint8_t *pp = p->uv_mode[i];
        unsigned *c = s->counts.uv_mode[i], sum, s2;

        sum = c[0] + c[1] + c[3] + c[4] + c[5] + c[6] + c[7] + c[8] + c[9];
        adapt_prob(&pp[0], c[DC_PRED], sum, 20, 128);
        sum -= c[TM_VP8_PRED];
        adapt_prob(&pp[1], c[TM_VP8_PRED], sum, 20, 128);
        sum -= c[VERT_PRED];
        adapt_prob(&pp[2], c[VERT_PRED], sum, 20, 128);
        s2 = c[HOR_PRED] + c[DIAG_DOWN_RIGHT_PRED] + c[VERT_RIGHT_PRED];
        sum -= s2;
        adapt_prob(&pp[3], s2, sum, 20, 128);
        s2 -= c[HOR_PRED];
        adapt_prob(&pp[4], c[HOR_PRED], s2, 20, 128);
        adapt_prob(&pp[5], c[DIAG_DOWN_RIGHT_PRED], c[VERT_RIGHT_PRED], 20, 128);
        sum -= c[DIAG_DOWN_LEFT_PRED];
        adapt_prob(&pp[6], c[DIAG_DOWN_LEFT_PRED], sum, 20, 128);
        sum -= c[VERT_LEFT_PRED];
        adapt_prob(&pp[7], c[VERT_LEFT_PRED], sum, 20, 128);
        adapt_prob(&pp[8], c[HOR_DOWN_PRED], c[HOR_UP_PRED], 20, 128);
    }
}

static void free_buffers(VP9Context *s)
{
    av_freep(&s->intra_pred_data[0]);
    av_freep(&s->b_base);
    av_freep(&s->block_base);
}

static av_cold int vp9_decode_free(AVCodecContext *ctx)
{
    VP9Context *s = ctx->priv_data;
    int i;

    for (i = 0; i < 3; i++) {
        if (s->s.frames[i].tf.f->buf[0])
            vp9_unref_frame(ctx, &s->s.frames[i]);
        av_frame_free(&s->s.frames[i].tf.f);
    }
    for (i = 0; i < 8; i++) {
        if (s->s.refs[i].f->buf[0])
            ff_thread_release_buffer(ctx, &s->s.refs[i]);
        av_frame_free(&s->s.refs[i].f);
        if (s->next_refs[i].f->buf[0])
            ff_thread_release_buffer(ctx, &s->next_refs[i]);
        av_frame_free(&s->next_refs[i].f);
    }
    free_buffers(s);
    av_freep(&s->c_b);
    s->c_b_size = 0;

    return 0;
}


static int vp9_decode_frame(AVCodecContext *ctx, void *frame,
                            int *got_frame, AVPacket *pkt)
{
    const uint8_t *data = pkt->data;
    int size = pkt->size;
    VP9Context *s = ctx->priv_data;
    int res, tile_row, tile_col, i, ref, row, col;
    int retain_segmap_ref = s->s.frames[REF_FRAME_SEGMAP].segmentation_map &&
                            (!s->s.h.segmentation.enabled || !s->s.h.segmentation.update_map);
    ptrdiff_t yoff, uvoff, ls_y, ls_uv;
    AVFrame *f;
    int bytesperpixel;

    if ((res = decode_frame_header(ctx, data, size, &ref)) < 0) {
        return res;
    } else if (res == 0) {
        if (!s->s.refs[ref].f->buf[0]) {
            av_log(ctx, AV_LOG_ERROR, "Requested reference %d not available\n", ref);
            return AVERROR_INVALIDDATA;
        }
        if ((res = av_frame_ref(frame, s->s.refs[ref].f)) < 0)
            return res;
        ((AVFrame *)frame)->pkt_pts = pkt->pts;
        ((AVFrame *)frame)->pkt_dts = pkt->dts;
        for (i = 0; i < 8; i++) {
            if (s->next_refs[i].f->buf[0])
                ff_thread_release_buffer(ctx, &s->next_refs[i]);
            if (s->s.refs[i].f->buf[0] &&
                (res = ff_thread_ref_frame(&s->next_refs[i], &s->s.refs[i])) < 0)
                return res;
        }
        *got_frame = 1;
        return pkt->size;
    }
    data += res;
    size -= res;

    if (!retain_segmap_ref || s->s.h.keyframe || s->s.h.intraonly) {
        if (s->s.frames[REF_FRAME_SEGMAP].tf.f->buf[0])
            vp9_unref_frame(ctx, &s->s.frames[REF_FRAME_SEGMAP]);
        if (!s->s.h.keyframe && !s->s.h.intraonly && !s->s.h.errorres && s->s.frames[CUR_FRAME].tf.f->buf[0] &&
            (res = vp9_ref_frame(ctx, &s->s.frames[REF_FRAME_SEGMAP], &s->s.frames[CUR_FRAME])) < 0)
            return res;
    }
    if (s->s.frames[REF_FRAME_MVPAIR].tf.f->buf[0])
        vp9_unref_frame(ctx, &s->s.frames[REF_FRAME_MVPAIR]);
    if (!s->s.h.intraonly && !s->s.h.keyframe && !s->s.h.errorres && s->s.frames[CUR_FRAME].tf.f->buf[0] &&
        (res = vp9_ref_frame(ctx, &s->s.frames[REF_FRAME_MVPAIR], &s->s.frames[CUR_FRAME])) < 0)
        return res;
    if (s->s.frames[CUR_FRAME].tf.f->buf[0])
        vp9_unref_frame(ctx, &s->s.frames[CUR_FRAME]);
    if ((res = vp9_alloc_frame(ctx, &s->s.frames[CUR_FRAME])) < 0)
        return res;
    f = s->s.frames[CUR_FRAME].tf.f;
    f->key_frame = s->s.h.keyframe;
    f->pict_type = (s->s.h.keyframe || s->s.h.intraonly) ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
    ls_y = f->linesize[0];
    ls_uv =f->linesize[1];

    if (s->s.frames[REF_FRAME_SEGMAP].tf.f->buf[0] &&
        (s->s.frames[REF_FRAME_MVPAIR].tf.f->width  != s->s.frames[CUR_FRAME].tf.f->width ||
         s->s.frames[REF_FRAME_MVPAIR].tf.f->height != s->s.frames[CUR_FRAME].tf.f->height)) {
        vp9_unref_frame(ctx, &s->s.frames[REF_FRAME_SEGMAP]);
    }

    // ref frame setup
    for (i = 0; i < 8; i++) {
        if (s->next_refs[i].f->buf[0])
            ff_thread_release_buffer(ctx, &s->next_refs[i]);
        if (s->s.h.refreshrefmask & (1 << i)) {
            res = ff_thread_ref_frame(&s->next_refs[i], &s->s.frames[CUR_FRAME].tf);
        } else if (s->s.refs[i].f->buf[0]) {
            res = ff_thread_ref_frame(&s->next_refs[i], &s->s.refs[i]);
        }
        if (res < 0)
            return res;
    }

    if (ctx->hwaccel) {
        res = ctx->hwaccel->start_frame(ctx, NULL, 0);
        if (res < 0)
            return res;
        res = ctx->hwaccel->decode_slice(ctx, pkt->data, pkt->size);
        if (res < 0)
            return res;
        res = ctx->hwaccel->end_frame(ctx);
        if (res < 0)
            return res;
        goto finish;
    }

    // main tile decode loop
    bytesperpixel = s->bytesperpixel;
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
        ctx->active_thread_type == FF_THREAD_FRAME && s->s.h.refreshctx && !s->s.h.parallelmode;
    if ((res = update_block_buffers(ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Failed to allocate block buffers\n");
        return res;
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
        ff_thread_finish_setup(ctx);
    } else if (!s->s.h.refreshctx) {
        ff_thread_finish_setup(ctx);
    }

    do {
        yoff = uvoff = 0;
        s->b = s->b_base;
        s->block = s->block_base;
        s->uvblock[0] = s->uvblock_base[0];
        s->uvblock[1] = s->uvblock_base[1];
        s->eob = s->eob_base;
        s->uveob[0] = s->uveob_base[0];
        s->uveob[1] = s->uveob_base[1];

        for (tile_row = 0; tile_row < s->s.h.tiling.tile_rows; tile_row++) {
            set_tile_offset(&s->tile_row_start, &s->tile_row_end,
                            tile_row, s->s.h.tiling.log2_tile_rows, s->sb_rows);
            if (s->pass != 2) {
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
                    ff_vp56_init_range_decoder(&s->c_b[tile_col], data, tile_size);
                    if (vp56_rac_get_prob_branchy(&s->c_b[tile_col], 128)) { // marker bit
                        ff_thread_report_progress(&s->s.frames[CUR_FRAME].tf, INT_MAX, 0);
                        return AVERROR_INVALIDDATA;
                    }
                    data += tile_size;
                    size -= tile_size;
                }
            }

            for (row = s->tile_row_start; row < s->tile_row_end;
                 row += 8, yoff += ls_y * 64, uvoff += ls_uv * 64 >> s->ss_v) {
                struct VP9Filter *lflvl_ptr = s->lflvl;
                ptrdiff_t yoff2 = yoff, uvoff2 = uvoff;

                for (tile_col = 0; tile_col < s->s.h.tiling.tile_cols; tile_col++) {
                    set_tile_offset(&s->tile_col_start, &s->tile_col_end,
                                    tile_col, s->s.h.tiling.log2_tile_cols, s->sb_cols);

                    if (s->pass != 2) {
                        memset(s->left_partition_ctx, 0, 8);
                        memset(s->left_skip_ctx, 0, 8);
                        if (s->s.h.keyframe || s->s.h.intraonly) {
                            memset(s->left_mode_ctx, DC_PRED, 16);
                        } else {
                            memset(s->left_mode_ctx, NEARESTMV, 8);
                        }
                        memset(s->left_y_nnz_ctx, 0, 16);
                        memset(s->left_uv_nnz_ctx, 0, 32);
                        memset(s->left_segpred_ctx, 0, 8);

                        memcpy(&s->c, &s->c_b[tile_col], sizeof(s->c));
                    }

                    for (col = s->tile_col_start;
                         col < s->tile_col_end;
                         col += 8, yoff2 += 64 * bytesperpixel,
                         uvoff2 += 64 * bytesperpixel >> s->ss_h, lflvl_ptr++) {
                        // FIXME integrate with lf code (i.e. zero after each
                        // use, similar to invtxfm coefficients, or similar)
                        if (s->pass != 1) {
                            memset(lflvl_ptr->mask, 0, sizeof(lflvl_ptr->mask));
                        }

                        if (s->pass == 2) {
                            decode_sb_mem(ctx, row, col, lflvl_ptr,
                                          yoff2, uvoff2, BL_64X64);
                        } else {
                            decode_sb(ctx, row, col, lflvl_ptr,
                                      yoff2, uvoff2, BL_64X64);
                        }
                    }
                    if (s->pass != 2) {
                        memcpy(&s->c_b[tile_col], &s->c, sizeof(s->c));
                    }
                }

                if (s->pass == 1) {
                    continue;
                }

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
                        loopfilter_sb(ctx, lflvl_ptr, row, col, yoff2, uvoff2);
                    }
                }

                // FIXME maybe we can make this more finegrained by running the
                // loopfilter per-block instead of after each sbrow
                // In fact that would also make intra pred left preparation easier?
                ff_thread_report_progress(&s->s.frames[CUR_FRAME].tf, row >> 3, 0);
            }
        }

        if (s->pass < 2 && s->s.h.refreshctx && !s->s.h.parallelmode) {
            adapt_probs(s);
            ff_thread_finish_setup(ctx);
        }
    } while (s->pass++ == 1);
    ff_thread_report_progress(&s->s.frames[CUR_FRAME].tf, INT_MAX, 0);

finish:
    // ref frame setup
    for (i = 0; i < 8; i++) {
        if (s->s.refs[i].f->buf[0])
            ff_thread_release_buffer(ctx, &s->s.refs[i]);
        if (s->next_refs[i].f->buf[0] &&
            (res = ff_thread_ref_frame(&s->s.refs[i], &s->next_refs[i])) < 0)
            return res;
    }

    if (!s->s.h.invisible) {
        if ((res = av_frame_ref(frame, s->s.frames[CUR_FRAME].tf.f)) < 0)
            return res;
        *got_frame = 1;
    }

    return pkt->size;
}

static void vp9_decode_flush(AVCodecContext *ctx)
{
    VP9Context *s = ctx->priv_data;
    int i;

    for (i = 0; i < 3; i++)
        vp9_unref_frame(ctx, &s->s.frames[i]);
    for (i = 0; i < 8; i++)
        ff_thread_release_buffer(ctx, &s->s.refs[i]);
}

static int init_frames(AVCodecContext *ctx)
{
    VP9Context *s = ctx->priv_data;
    int i;

    for (i = 0; i < 3; i++) {
        s->s.frames[i].tf.f = av_frame_alloc();
        if (!s->s.frames[i].tf.f) {
            vp9_decode_free(ctx);
            av_log(ctx, AV_LOG_ERROR, "Failed to allocate frame buffer %d\n", i);
            return AVERROR(ENOMEM);
        }
    }
    for (i = 0; i < 8; i++) {
        s->s.refs[i].f = av_frame_alloc();
        s->next_refs[i].f = av_frame_alloc();
        if (!s->s.refs[i].f || !s->next_refs[i].f) {
            vp9_decode_free(ctx);
            av_log(ctx, AV_LOG_ERROR, "Failed to allocate frame buffer %d\n", i);
            return AVERROR(ENOMEM);
        }
    }

    return 0;
}

static av_cold int vp9_decode_init(AVCodecContext *ctx)
{
    VP9Context *s = ctx->priv_data;

    ctx->internal->allocate_progress = 1;
    s->last_bpp = 0;
    s->s.h.filter.sharpness = -1;

    return init_frames(ctx);
}

#if HAVE_THREADS
static av_cold int vp9_decode_init_thread_copy(AVCodecContext *avctx)
{
    return init_frames(avctx);
}

static int vp9_decode_update_thread_context(AVCodecContext *dst, const AVCodecContext *src)
{
    int i, res;
    VP9Context *s = dst->priv_data, *ssrc = src->priv_data;

    // detect size changes in other threads
    if (s->intra_pred_data[0] &&
        (!ssrc->intra_pred_data[0] || s->cols != ssrc->cols ||
         s->rows != ssrc->rows || s->bpp != ssrc->bpp || s->pix_fmt != ssrc->pix_fmt)) {
        free_buffers(s);
    }

    for (i = 0; i < 3; i++) {
        if (s->s.frames[i].tf.f->buf[0])
            vp9_unref_frame(dst, &s->s.frames[i]);
        if (ssrc->s.frames[i].tf.f->buf[0]) {
            if ((res = vp9_ref_frame(dst, &s->s.frames[i], &ssrc->s.frames[i])) < 0)
                return res;
        }
    }
    for (i = 0; i < 8; i++) {
        if (s->s.refs[i].f->buf[0])
            ff_thread_release_buffer(dst, &s->s.refs[i]);
        if (ssrc->next_refs[i].f->buf[0]) {
            if ((res = ff_thread_ref_frame(&s->s.refs[i], &ssrc->next_refs[i])) < 0)
                return res;
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
    s->bpp = ssrc->bpp;
    s->bpp_index = ssrc->bpp_index;
    s->pix_fmt = ssrc->pix_fmt;
    memcpy(&s->prob_ctx, &ssrc->prob_ctx, sizeof(s->prob_ctx));
    memcpy(&s->s.h.lf_delta, &ssrc->s.h.lf_delta, sizeof(s->s.h.lf_delta));
    memcpy(&s->s.h.segmentation.feat, &ssrc->s.h.segmentation.feat,
           sizeof(s->s.h.segmentation.feat));

    return 0;
}
#endif

AVCodec ff_vp9_decoder = {
    .name                  = "vp9",
    .long_name             = NULL_IF_CONFIG_SMALL("Google VP9"),
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_VP9,
    .priv_data_size        = sizeof(VP9Context),
    .init                  = vp9_decode_init,
    .close                 = vp9_decode_free,
    .decode                = vp9_decode_frame,
    .capabilities          = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .flush                 = vp9_decode_flush,
    .init_thread_copy      = ONLY_IF_THREADS_ENABLED(vp9_decode_init_thread_copy),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(vp9_decode_update_thread_context),
    .profiles              = NULL_IF_CONFIG_SMALL(ff_vp9_profiles),
};
