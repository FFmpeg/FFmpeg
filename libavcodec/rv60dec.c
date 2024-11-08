/*
 * RV60 decoder
 * Copyright (c) 2007 Mike Melanson, Konstantin Shishkov
 * Copyright (C) 2023 Peter Ross
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
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "golomb.h"
#include "libavutil/mem.h"
#include "rv60data.h"
#include "rv60dsp.h"
#include "rv60vlcs.h"
#include "threadprogress.h"
#include "unary.h"
#include "videodsp.h"

static const int8_t frame_types[4] = {AV_PICTURE_TYPE_I, AV_PICTURE_TYPE_P, AV_PICTURE_TYPE_B, AV_PICTURE_TYPE_NONE};

enum CUType {
    CU_INTRA = 0,
    CU_INTER_MV,
    CU_SKIP,
    CU_INTER
};

enum PUType {
    PU_FULL = 0,
    PU_N2HOR,
    PU_N2VER,
    PU_QUARTERS,
    PU_N4HOR,
    PU_N34HOR,
    PU_N4VER,
    PU_N34VER
};

enum IntraMode {
    INTRAMODE_INDEX = 0,
    INTRAMODE_DC64,
    INTRAMODE_PLANE64,
    INTRAMODE_MODE
};

enum MVRefEnum {
    MVREF_NONE,
    MVREF_REF0,
    MVREF_REF1,
    MVREF_BREF,
    MVREF_REF0ANDBREF,
    MVREF_SKIP0,
    MVREF_SKIP1,
    MVREF_SKIP2,
    MVREF_SKIP3
};

static const uint8_t skip_mv_ref[4] = {MVREF_SKIP0, MVREF_SKIP1, MVREF_SKIP2, MVREF_SKIP3};

enum {
   TRANSFORM_NONE = 0,
   TRANSFORM_16X16,
   TRANSFORM_8X8,
   TRANSFORM_4X4
};

static const VLCElem * cbp8_vlc[7][4];
static const VLCElem * cbp16_vlc[7][3][4];

typedef struct {
    const VLCElem * l0[2];
    const VLCElem * l12[2];
    const VLCElem * l3[2];
    const VLCElem * esc;
} CoeffVLCs;

static CoeffVLCs intra_coeff_vlc[5];
static CoeffVLCs inter_coeff_vlc[7];

#define MAX_VLC_SIZE 864
static VLCElem table_data[129148];

/* 32-bit version of rv34_gen_vlc */
static const VLCElem * gen_vlc(const uint8_t * bits, int size, VLCInitState * state)
{
    int counts[17] = {0};
    uint32_t codes[18];
    uint32_t cw[MAX_VLC_SIZE];

    for (int i = 0; i < size; i++)
        counts[bits[i]]++;

    codes[0] = counts[0] = 0;
    for (int i = 0; i < 17; i++)
        codes[i+1] = (codes[i] + counts[i]) << 1;

    for (int i = 0; i < size; i++)
        cw[i] = codes[bits[i]]++;

    return ff_vlc_init_tables(state, 9, size,
                       bits, 1, 1,
                       cw,   4, 4, 0);
}

static void build_coeff_vlc(const CoeffLens * lens, CoeffVLCs * vlc, int count, VLCInitState * state)
{
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < 2; j++) {
            vlc[i].l0[j] = gen_vlc(lens[i].l0[j], 864, state);
            vlc[i].l12[j] = gen_vlc(lens[i].l12[j], 108, state);
            vlc[i].l3[j] = gen_vlc(lens[i].l3[j], 108, state);
        }
        vlc[i].esc = gen_vlc(lens[i].esc, 32, state);
    }
}

static av_cold void rv60_init_static_data(void)
{
    VLCInitState state = VLC_INIT_STATE(table_data);

    for (int i = 0; i < 7; i++)
        for (int j = 0; j < 4; j++)
            cbp8_vlc[i][j] = gen_vlc(rv60_cbp8_lens[i][j], 64, &state);

    for (int i = 0; i < 7; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 4; k++)
                cbp16_vlc[i][j][k] = gen_vlc(rv60_cbp16_lens[i][j][k], 64, &state);

    build_coeff_vlc(rv60_intra_lens, intra_coeff_vlc, 5, &state);
    build_coeff_vlc(rv60_inter_lens, inter_coeff_vlc, 7, &state);
}

typedef struct {
    int sign;
    int size;
    const uint8_t * data;
    int data_size;
} Slice;

typedef struct {
    int cu_split_pos;
    uint8_t cu_split[1+4+16+64];

    uint8_t coded_blk[64];

    uint8_t avg_buffer[64*64 + 32*32*2];
    uint8_t * avg_data[3];
    int avg_linesize[3];
} ThreadContext;

typedef struct {
    int16_t x;
    int16_t y;
} MV;

typedef struct {
    enum MVRefEnum mvref;
    MV f_mv;
    MV b_mv;
} MVInfo;

typedef struct {
    enum IntraMode imode;
    MVInfo mv;
} BlockInfo;

typedef struct {
    enum CUType cu_type;
    enum PUType pu_type;
} PUInfo;

typedef struct RV60Context {
    AVCodecContext * avctx;
    VideoDSPContext vdsp;

#define CUR_PIC 0
#define LAST_PIC 1
#define NEXT_PIC 2
    AVFrame *last_frame[3];

    int pict_type;
    int qp;
    int osvquant;
    int ts;
    int two_f_refs;
    int qp_off_type;
    int deblock;
    int deblock_chroma;
    int awidth;
    int aheight;
    int cu_width;
    int cu_height;

    Slice * slice;

    int pu_stride;
    PUInfo * pu_info;

    int blk_stride;
    BlockInfo * blk_info;

    int dblk_stride;
    uint8_t * left_str;
    uint8_t * top_str;

    uint64_t ref_pts[2], ts_scale;
    uint32_t ref_ts[2];

    struct ThreadProgress *progress;
    unsigned nb_progress;
} RV60Context;

static int progress_init(RV60Context *s, unsigned count)
{
    if (s->nb_progress < count) {
        void *tmp = av_realloc_array(s->progress, count, sizeof(*s->progress));
        if (!tmp)
            return AVERROR(ENOMEM);
        s->progress = tmp;
        memset(s->progress + s->nb_progress, 0, (count - s->nb_progress) * sizeof(*s->progress));
        for (int i = s->nb_progress; i < count; i++) {
            int ret = ff_thread_progress_init(&s->progress[i], 1);
            if (ret < 0)
                return ret;
            s->nb_progress = i + 1;
        }
    }

    for (int i = 0; i < count; i++)
        ff_thread_progress_reset(&s->progress[i]);

    return 0;
}

static av_cold int rv60_decode_init(AVCodecContext * avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    RV60Context *s = avctx->priv_data;

    s->avctx = avctx;

    ff_videodsp_init(&s->vdsp, 8);

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    for (int i = 0; i < 3; i++) {
        s->last_frame[i] = av_frame_alloc();
        if (!s->last_frame[i])
            return AVERROR(ENOMEM);
    }

    ff_thread_once(&init_static_once, rv60_init_static_data);

    return 0;
}

static int update_dimensions_clear_info(RV60Context *s, int width, int height)
{
    int ret;

    if (width != s->avctx->width || height != s->avctx->height) {

        av_log(s->avctx, AV_LOG_INFO, "changing dimensions to %dx%d\n", width, height);

        for (int i = 0; i < 3; i++)
            av_frame_unref(s->last_frame[i]);

        if ((ret = ff_set_dimensions(s->avctx, width, height)) < 0)
            return ret;

        if (s->avctx->width <= 64 || s->avctx->height <= 64)
            av_log(s->avctx, AV_LOG_WARNING, "unable to faithfully reproduce emulated edges; expect visual artefacts\n");
    }

    s->awidth = FFALIGN(width, 16);
    s->aheight = FFALIGN(height, 16);

    s->cu_width = (width + 63) >> 6;
    s->cu_height = (height + 63) >> 6;

    s->pu_stride = s->cu_width << 3;
    s->blk_stride = s->cu_width << 4;

    if ((ret = av_reallocp_array(&s->slice, s->cu_height, sizeof(s->slice[0]))) < 0)
        return ret;

    if ((ret = av_reallocp_array(&s->pu_info, s->pu_stride * (s->cu_height << 3), sizeof(s->pu_info[0]))) < 0)
        return ret;

    if ((ret = av_reallocp_array(&s->blk_info, s->blk_stride * (s->cu_height << 4), sizeof(s->blk_info[0]))) < 0)
        return ret;

    for (int j = 0; j < s->cu_height << 4; j++)
       for (int i = 0; i < s->cu_width << 4; i++)
           s->blk_info[j*s->blk_stride + i].mv.mvref = MVREF_NONE;

    if (s->deblock) {
        int size;

        s->dblk_stride = s->awidth >> 2;

        size = s->dblk_stride * (s->aheight >> 2);

        if ((ret = av_reallocp_array(&s->top_str, size, sizeof(s->top_str[0]))) < 0)
            return ret;

        if ((ret = av_reallocp_array(&s->left_str, size, sizeof(s->left_str[0]))) < 0)
            return ret;

        memset(s->top_str, 0, size);
        memset(s->left_str, 0, size);
    }

    return 0;
}

static int read_code012(GetBitContext * gb)
{
    if (!get_bits1(gb))
       return 0;
    return get_bits1(gb) + 1;
}

static int read_frame_header(RV60Context *s, GetBitContext *gb, int * width, int * height)
{
    if (get_bits(gb, 2) != 3)
        return AVERROR_INVALIDDATA;

    skip_bits(gb, 2);
    skip_bits(gb, 4);

    s->pict_type = frame_types[get_bits(gb, 2)];
    if (s->pict_type == AV_PICTURE_TYPE_NONE)
        return AVERROR_INVALIDDATA;

    s->qp = get_bits(gb, 6);
    skip_bits1(gb);
    skip_bits(gb, 2);
    s->osvquant = get_bits(gb, 2);
    skip_bits1(gb);
    skip_bits(gb, 2);
    s->ts = get_bits(gb, 24);
    *width = (get_bits(gb, 11) + 1) * 4;
    *height = get_bits(gb, 11) * 4;
    skip_bits1(gb);
    if (s->pict_type == AV_PICTURE_TYPE_I) {
        s->two_f_refs = 0;
    } else {
        if (get_bits1(gb))
            skip_bits(gb, 3);
        s->two_f_refs = get_bits1(gb);
    }
    read_code012(gb);
    read_code012(gb);
    s->qp_off_type = read_code012(gb);
    s->deblock = get_bits1(gb);
    s->deblock_chroma = s->deblock && !get_bits1(gb);

    if (get_bits1(gb)) {
        int count = get_bits(gb, 2);
        if (count) {
            skip_bits(gb, 2);
            for (int i = 0; i < count; i++)
                for (int j = 0; j < 2 << i; j++)
                    skip_bits(gb, 8);
        }
    }

    return 0;
}

static int read_slice_sizes(RV60Context *s, GetBitContext *gb)
{
    int nbits = get_bits(gb, 5) + 1;
    int last_size, sum = 0;

    for (int i = 0; i < s->cu_height; i++)
        s->slice[i].sign = get_bits1(gb);

    s->slice[0].size = last_size = sum = get_bits(gb, nbits);

    for (int i = 1; i < s->cu_height; i++) {
        int diff = get_bits(gb, nbits);
        if (s->slice[i].sign)
            last_size += diff;
        else
            last_size -= diff;
        if (last_size <= 0)
            return AVERROR_INVALIDDATA;
        s->slice[i].size = last_size;
        sum += s->slice[i].size;
    }

    align_get_bits(gb);
    return 0;
}

static int read_intra_mode(GetBitContext * gb, int * param)
{
    if (get_bits1(gb)) {
        *param = read_code012(gb);
        return INTRAMODE_INDEX;
    } else {
        *param = get_bits(gb, 5);
        return INTRAMODE_MODE;
    }
}

static int has_top_block(const RV60Context * s, int xpos, int ypos, int dx, int dy, int size)
{
    return ypos + dy && xpos + dx + size <= s->awidth;
}

static int has_left_block(const RV60Context * s, int xpos, int ypos, int dx, int dy, int size)
{
    return xpos + dx && ypos + dy + size <= s->aheight;
}

static int has_top_right_block(const RV60Context * s, int xpos, int ypos, int dx, int dy, int size)
{
    if (has_top_block(s, xpos, ypos, dx, dy, size * 2)) {
        int cxpos = ((xpos + dx) & 63) >> ff_log2(size);
        int cypos = ((ypos + dy) & 63) >> ff_log2(size);
        return !(rv60_avail_mask[cxpos] & cypos);
    }
    return 0;
}

static int has_left_down_block(const RV60Context * s, int xpos, int ypos, int dx, int dy, int size)
{
    if (has_left_block(s, xpos, ypos, dx, dy, size * 2)) {
        int cxpos = (~(xpos + dx) & 63) >> ff_log2(size);
        int cypos = (~(ypos + dy) & 63) >> ff_log2(size);
        return rv60_avail_mask[cxpos] & cypos;
    }
    return 0;
}

typedef struct {
    uint8_t t[129];
    uint8_t l[129];
    int has_t;
    int has_tr;
    int has_l;
    int has_ld;
} IntraPredContext;

typedef struct {
    int xpos;
    int ypos;
    int pu_pos;
    int blk_pos;

    enum CUType cu_type;
    enum PUType pu_type;
    enum IntraMode imode[4];
    int imode_param[4];
    MVInfo mv[4];

    IntraPredContext ipred;
} CUContext;

static void ipred_init(IntraPredContext * i)
{
    memset(i->t, 0x80, sizeof(i->t));
    memset(i->l, 0x80, sizeof(i->l));
    i->has_t = i->has_tr = i->has_l = i->has_ld = 0;
}

static void populate_ipred(const RV60Context * s, CUContext * cu, const uint8_t * src, int stride, int xoff, int yoff, int size, int is_luma)
{
    if (is_luma)
        src += (cu->ypos + yoff) * stride + cu->xpos + xoff;
    else
        src += (cu->ypos >> 1) * stride + (cu->xpos >> 1);

    ipred_init(&cu->ipred);

    if (cu->ypos + yoff > 0) {
        cu->ipred.has_t = 1;

        memcpy(cu->ipred.t + 1, src - stride, size);

        if ((is_luma && has_top_right_block(s, cu->xpos, cu->ypos, xoff, yoff, size)) ||
            (!is_luma && has_top_right_block(s, cu->xpos, cu->ypos, 0, 0, size << 1))) {
            cu->ipred.has_tr = 1;
            memcpy(cu->ipred.t + size + 1, src - stride + size, size);
        } else
            memset(cu->ipred.t + size + 1, cu->ipred.t[size], size);

        if (cu->xpos + xoff > 0)
            cu->ipred.t[0] = src[-stride - 1];
    }

    if (cu->xpos + xoff > 0) {
        cu->ipred.has_l = 1;

        for (int y = 0; y < size; y++)
            cu->ipred.l[y + 1] = src[y*stride - 1];

        if ((is_luma && has_left_down_block(s, cu->xpos, cu->ypos, xoff, yoff, size)) ||
            (!is_luma && has_left_down_block(s, cu->xpos, cu->ypos, 0, 0, size << 1))) {
            cu->ipred.has_ld = 1;
            for (int y = size; y < size * 2; y++)
                cu->ipred.l[y + 1] = src[y*stride - 1];
        } else
            memset(cu->ipred.l + size + 1, cu->ipred.l[size], size);

        if (cu->ypos + yoff > 0)
            cu->ipred.l[0] = src[-stride - 1];
    }
}

static void pred_plane(const IntraPredContext * p, uint8_t * dst, int stride, int size)
{
    int lastl = p->l[size + 1];
    int lastt = p->t[size + 1];
    int tmp1[64], tmp2[64];
    int top_ref[64], left_ref[64];
    int shift;

    for (int i = 0; i < size; i++) {
        tmp1[i] = lastl - p->t[i + 1];
        tmp2[i] = lastt - p->l[i + 1];
    }

    shift = ff_log2(size) + 1;
    for (int i = 0; i < size; i++) {
        top_ref[i] = p->t[i + 1] << (shift - 1);
        left_ref[i] = p->l[i + 1] << (shift - 1);
    }

    for (int y = 0; y < size; y++) {
        int add = tmp2[y];
        int sum = left_ref[y] + size;
        for (int x = 0; x < size; x++) {
            int v = tmp1[x] + top_ref[x];
            sum += add;
            top_ref[x] = v;
            dst[y*stride + x] = (sum + v) >> shift;
        }
    }
}

static void pred_dc(const IntraPredContext * p, uint8_t * dst, int stride, int size, int filter)
{
    int dc;

    if (!p->has_t && !p->has_l)
        dc = 0x80;
    else {
        int sum = 0;
        if (p->has_t)
            for (int x = 0; x < size; x++)
                sum += p->t[x + 1];
        if (p->has_l)
            for (int y = 0; y < size; y++)
                sum += p->l[y + 1];
        if (p->has_t && p->has_l)
            dc = (sum + size) / (size * 2);
        else
            dc = (sum + size / 2) / size;
    }

    for (int y = 0; y < size; y++)
        memset(dst + y*stride, dc, size);

    if (filter && p->has_t && p->has_l) {
        dst[0] = (p->t[1] + p->l[1] + 2 * dst[0] + 2) >> 2;
        for (int x = 1; x < size; x++)
            dst[x] = (p->t[x + 1] + 3 * dst[x] + 2) >> 2;
        for (int y = 1; y < size; y++)
            dst[y*stride] = (p->l[y + 1] + 3 * dst[y*stride] + 2) >> 2;
    }
}

static void filter_weak(uint8_t * dst, const uint8_t * src, int size)
{
    dst[0] = src[0];
    for (int i = 1; i < size - 1; i++)
        dst[i] = (src[i - 1] + 2*src[i] + src[i + 1] + 2) >> 2;
    dst[size - 1] = src[size - 1];
}

static void filter_bilin32(uint8_t * dst, int v0, int v1, int size)
{
    int diff = v1 - v0;
    int sum = (v0 << 5) + (1 << (5 - 1));
    for (int i = 0; i < size; i++) {
        dst[i] = sum >> 5;
        sum += diff;
    }
}

static void pred_hor_angle(uint8_t * dst, int stride, int size, int weight, const uint8_t * src)
{
    int sum = 0;
    for (int x = 0; x < size; x++) {
        int off, frac;
        sum += weight;
        off = (sum >> 5) + 32;
        frac = sum & 0x1F;
        if (!frac)
            for (int y = 0; y < size; y++)
                dst[y*stride + x] = src[off + y];
        else {
            for (int y = 0; y < size; y++) {
                int a = src[off + y];
                int b = src[off + y + 1];
                dst[y*stride + x] = ((32 - frac) * a + frac * b + 16) >> 5;
            }
        }
    }
}

static void pred_ver_angle(uint8_t * dst, int stride, int size, int weight, const uint8_t * src)
{
    int sum = 0;
    for (int y = 0; y < size; y++) {
        int off, frac;
        sum += weight;
        off = (sum >> 5) + 32;
        frac = sum & 0x1F;
        if (!frac)
            memcpy(dst + y*stride, src + off, size);
        else {
            for (int x = 0; x < size; x++) {
                int a = src[off + x];
                int b = src[off + x + 1];
                dst[y*stride + x] = ((32 - frac) * a + frac * b + 16) >> 5;
            }
        }
    }
}

static int pred_angle(const IntraPredContext * p, uint8_t * dst, int stride, int size, int imode, int filter)
{
    uint8_t filtered1[96], filtered2[96];

    if (!imode) {
        pred_plane(p, dst, stride, size);
    } else if (imode == 1) {
        pred_dc(p, dst, stride, size, filter);
    } else if (imode <= 9) {
        int ang_weight = rv60_ipred_angle[10 - imode];
        int add_size = (size * ang_weight + 31) >> 5;
        if (size <= 16) {
            filter_weak(filtered1 + 32, &p->l[1], size + add_size);
        } else {
            filter_bilin32(filtered1 + 32, p->l[1], p->l[33], 32);
            filter_bilin32(filtered1 + 64, p->l[32], p->l[64], add_size);
        }
        pred_hor_angle(dst, stride, size, ang_weight, filtered1);
    } else if (imode == 10) {
        if (size <= 16)
            filter_weak(filtered1 + 32, &p->l[1], size);
        else
            filter_bilin32(filtered1 + 32, p->l[1], p->l[33], 32);
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
                dst[y*stride + x] = filtered1[32 + y];
        if (filter) {
            int tl = p->t[0];
            for (int x = 0; x < size; x++)
                dst[x] = av_clip_uint8(dst[x] + ((p->t[x + 1] - tl) >> 1));
        }
    } else if (imode <= 17) {
        int ang_weight = rv60_ipred_angle[imode - 10];
        int inv_angle = rv60_ipred_inv_angle[imode - 10];
        int add_size = (size * ang_weight + 31) >> 5;
        if (size <= 16) {
            memcpy(filtered1 + 32 - 1, p->l, size + 1);
            memcpy(filtered2 + 32 - 1, p->t, size + 1);
        } else {
            filtered1[32 - 1] = p->l[0];
            filter_bilin32(filtered1 + 32, p->l[0], p->l[32], 32);
            filtered2[32 - 1] = p->t[0];
            filter_bilin32(filtered2 + 32, p->t[0], p->t[32], 32);
        }
        if (add_size > 1) {
            int sum = 0x80;
            for (int i = 1; i < add_size; i++) {
                sum += inv_angle;
                filtered1[32 - 1 - i] = filtered2[32 - 1 + (sum >> 8)];
            }
        }
        pred_hor_angle(dst, stride, size, -ang_weight, filtered1);
    } else if (imode <= 25) {
        int ang_weight = rv60_ipred_angle[26 - imode];
        int inv_angle = rv60_ipred_inv_angle[26 - imode];
        int add_size = (size * ang_weight + 31) >> 5;
        if (size <= 16) {
            memcpy(filtered1 + 32 - 1, p->t, size + 1);
            memcpy(filtered2 + 32 - 1, p->l, size + 1);
        } else {
            filtered1[32 - 1] = p->t[0];
            filter_bilin32(filtered1 + 32, p->t[0], p->t[32], 32);
            filtered2[32 - 1] = p->l[0];
            filter_bilin32(filtered2 + 32, p->l[0], p->l[32], 32);
        }
        if (add_size > 1) {
            int sum = 0x80;
            for (int i = 1; i < add_size; i++) {
                sum += inv_angle;
                filtered1[32 - 1 - i] = filtered2[32 - 1 + (sum >> 8)];
            }
        }
        pred_ver_angle(dst, stride, size, -ang_weight, filtered1);
    } else if (imode == 26) {
        if (size <= 16)
            filter_weak(&filtered1[32], &p->t[1], size);
        else
            filter_bilin32(filtered1 + 32, p->t[1], p->t[33], 32);
        for (int i = 0; i < size; i++)
            memcpy(dst + i*stride, filtered1 + 32, size);
        if (filter) {
            int tl = p->l[0];
            for (int y = 0; y < size; y++)
                dst[y*stride] = av_clip_uint8(dst[y*stride] + ((p->l[y+1] - tl) >> 1));
        }
    } else if (imode <= 34) {
        int ang_weight = rv60_ipred_angle[imode - 26];
        int add_size = (size * ang_weight + 31) >> 5;
        if (size <= 16)
            filter_weak(&filtered1[32], &p->t[1], size + add_size);
        else {
            filter_bilin32(filtered1 + 32, p->t[1], p->t[33], 32);
            filter_bilin32(filtered1 + 64, p->t[32], p->t[64], add_size);
        }
        pred_ver_angle(dst, stride, size, ang_weight, filtered1);
    } else
        return AVERROR_INVALIDDATA;
    return 0;
}

static int pu_is_intra(const PUInfo * pu)
{
    return pu->cu_type == CU_INTRA;
}

static int ipm_compar(const void * a, const void * b)
{
    return *(const enum IntraMode *)a - *(const enum IntraMode *)b;
}

#define MK_UNIQUELIST(name, type, max_size) \
typedef struct { \
    type list[max_size]; \
    int size; \
} unique_list_##name; \
\
static void unique_list_##name##_init(unique_list_##name * s)  \
{ \
    memset(s->list, 0, sizeof(s->list)); \
    s->size = 0; \
} \
\
static void unique_list_##name##_add(unique_list_##name * s, type cand) \
{ \
    if (s->size == max_size) \
        return; \
    \
    for (int i = 0; i < s->size; i++) { \
        if (!memcmp(&s->list[i], &cand, sizeof(type))) { \
            return; \
        } \
    } \
    s->list[s->size++] = cand; \
}

MK_UNIQUELIST(intramode, enum IntraMode, 3)
MK_UNIQUELIST(mvinfo, MVInfo, 4)

static int reconstruct_intra(const RV60Context * s, const CUContext * cu, int size, int sub)
{
    int blk_pos, tl_x, tl_y;
    unique_list_intramode ipm_cand;

    if (cu->imode[0] == INTRAMODE_DC64)
        return 1;

    if (cu->imode[0] == INTRAMODE_PLANE64)
        return 0;

    unique_list_intramode_init(&ipm_cand);

    if (has_top_block(s, cu->xpos, cu->ypos, (sub & 1) * 4, 0, size)) {
        const PUInfo * pu = &s->pu_info[cu->pu_pos - s->pu_stride];
        if (pu_is_intra(pu))
            unique_list_intramode_add(&ipm_cand, s->blk_info[cu->blk_pos - s->blk_stride + (sub & 1)].imode);
    }

    blk_pos = cu->blk_pos + (sub >> 1) * s->blk_stride + (sub & 1);

    if (has_left_block(s, cu->xpos, cu->ypos, 0, (sub & 2) * 2, size)) {
        const PUInfo * pu = &s->pu_info[cu->pu_pos - 1];
        if (pu_is_intra(pu))
            unique_list_intramode_add(&ipm_cand, s->blk_info[blk_pos - 1 - (sub & 1)].imode);
    }

    tl_x = !(sub & 2) ? (cu->xpos + (sub & 1) * 4) : cu->xpos;
    tl_y = cu->ypos + (sub & 2) * 4;
    if (tl_x > 0 && tl_y > 0) {
        const PUInfo * pu;
        switch (sub) {
        case 0: pu = &s->pu_info[cu->pu_pos - s->pu_stride - 1]; break;
        case 1: pu = &s->pu_info[cu->pu_pos - s->pu_stride]; break;
        default: pu = &s->pu_info[cu->pu_pos - 1];
        }
        if (pu_is_intra(pu)) {
            if (sub != 3)
                unique_list_intramode_add(&ipm_cand, s->blk_info[blk_pos - s->blk_stride - 1].imode);
            else
                unique_list_intramode_add(&ipm_cand, s->blk_info[blk_pos - s->blk_stride - 2].imode);
        }
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(rv60_candidate_intra_angles); i++)
        unique_list_intramode_add(&ipm_cand, rv60_candidate_intra_angles[i]);

    if (cu->imode[sub] == INTRAMODE_INDEX)
        return ipm_cand.list[cu->imode_param[sub]];

    if (cu->imode[sub] == INTRAMODE_MODE) {
        enum IntraMode imode = cu->imode_param[sub];
        qsort(ipm_cand.list, 3, sizeof(ipm_cand.list[0]), ipm_compar);
        for (int i = 0; i < 3; i++)
            if (imode >= ipm_cand.list[i])
                imode++;
        return imode;
    }

    av_assert0(0); // should never reach here
    return 0;
}

static int get_skip_mv_index(enum MVRefEnum mvref)
{
    switch (mvref) {
    case MVREF_SKIP1: return 1;
    case MVREF_SKIP2: return 2;
    case MVREF_SKIP3: return 3;
    default: return 0;
    }
}

static int mvinfo_valid(const MVInfo * mvi)
{
    return mvi->mvref != MVREF_NONE;
}

static void fill_mv_skip_cand(RV60Context * s, const CUContext * cu, unique_list_mvinfo * skip_cand, int size)
{
    int mv_size = size >> 2;

    if (cu->xpos > 0) {
        const MVInfo * mv = &s->blk_info[cu->blk_pos - 1].mv;
        if (mvinfo_valid(mv))
            unique_list_mvinfo_add(skip_cand, *mv);
    }
    if (cu->ypos > 0) {
        const MVInfo * mv = &s->blk_info[cu->blk_pos - s->blk_stride].mv;
        if (mvinfo_valid(mv))
            unique_list_mvinfo_add(skip_cand, *mv);
    }
    if (has_top_right_block(s, cu->xpos, cu->ypos, 0, 0, size)) {
        const MVInfo * mv = &s->blk_info[cu->blk_pos - s->blk_stride + mv_size].mv;
        if (mvinfo_valid(mv))
            unique_list_mvinfo_add(skip_cand, *mv);
    }
    if (has_left_down_block(s, cu->xpos, cu->ypos, 0, 0, size)) {
        const MVInfo * mv = &s->blk_info[cu->blk_pos + s->blk_stride * mv_size - 1].mv;
        if (mvinfo_valid(mv))
            unique_list_mvinfo_add(skip_cand, *mv);
    }
    if (has_left_block(s, cu->xpos, cu->ypos, 0, 0, size)) {
        const MVInfo * mv = &s->blk_info[cu->blk_pos + s->blk_stride * (mv_size - 1) - 1].mv;
        if (mvinfo_valid(mv))
            unique_list_mvinfo_add(skip_cand, *mv);
    }
    if (has_top_block(s, cu->xpos, cu->ypos, 0, 0, size)) {
        const MVInfo * mv = &s->blk_info[cu->blk_pos - s->blk_stride + mv_size - 1].mv;
        if (mvinfo_valid(mv))
            unique_list_mvinfo_add(skip_cand, *mv);
    }
    if (cu->xpos > 0 && cu->ypos > 0) {
        const MVInfo * mv = &s->blk_info[cu->blk_pos - s->blk_stride - 1].mv;
        if (mvinfo_valid(mv))
            unique_list_mvinfo_add(skip_cand, *mv);
    }

    for (int i = skip_cand->size; i < 4; i++)
        skip_cand->list[i] = (MVInfo){.mvref=MVREF_REF0,.f_mv={0,0},.b_mv={0,0}};
}

typedef struct {
    int w, h;
} Dimensions;

static void get_mv_dimensions(Dimensions * dim, enum PUType pu_type, int part_no, int size)
{
    int mv_size = size >> 2;
    switch (pu_type) {
    case PU_FULL:
        dim->w = dim->h = mv_size;
        break;
    case PU_N2HOR:
        dim->w = mv_size;
        dim->h = mv_size >> 1;
        break;
    case PU_N2VER:
        dim->w = mv_size >> 1;
        dim->h = mv_size;
        break;
    case PU_QUARTERS:
        dim->w = dim->h = mv_size >> 1;
        break;
    case PU_N4HOR:
        dim->w = mv_size;
        dim->h = !part_no ? (mv_size >> 2) : ((3 * mv_size) >> 2);
        break;
    case PU_N34HOR:
        dim->w = mv_size;
        dim->h = !part_no ? ((3 * mv_size) >> 2) : (mv_size >> 2);
        break;
    case PU_N4VER:
        dim->w = !part_no ? (mv_size >> 2) : ((3 * mv_size) >> 2);
        dim->h = mv_size;
        break;
    case PU_N34VER:
        dim->w = !part_no ? ((3 * mv_size) >> 2) : (mv_size >> 2);
        dim->h = mv_size;
        break;
    }
}

static int has_hor_split(enum PUType pu_type)
{
    return pu_type == PU_N2HOR || pu_type == PU_N4HOR || pu_type == PU_N34HOR || pu_type == PU_QUARTERS;
}

static int has_ver_split(enum PUType pu_type)
{
    return pu_type == PU_N2VER || pu_type == PU_N4VER || pu_type == PU_N34VER || pu_type == PU_QUARTERS;
}

static int pu_type_num_parts(enum PUType pu_type)
{
    switch (pu_type) {
    case PU_FULL: return 1;
    case PU_QUARTERS: return 4;
    default: return 2;
    }
}

static void get_next_mv(const RV60Context * s, const Dimensions * dim, enum PUType pu_type, int part_no, int * mv_pos, int * mv_x, int * mv_y)
{
    if (pu_type == PU_QUARTERS) {
        if (part_no != 1) {
            *mv_pos += dim->w;
            *mv_x   += dim->w;
        } else {
            *mv_pos += dim->h*s->blk_stride - dim->w;
            *mv_x -= dim->w;
            *mv_y += dim->h;
        }
    } else if (has_hor_split(pu_type)) {
        *mv_pos += dim->h * s->blk_stride;
        *mv_y   += dim->h;
    } else if (has_ver_split(pu_type)) {
        *mv_pos += dim->w;
        *mv_x   += dim->w;
    }
}

static int mv_is_ref0(enum MVRefEnum mvref)
{
    return mvref == MVREF_REF0 || mvref == MVREF_REF0ANDBREF;
}

static int mv_is_forward(enum MVRefEnum mvref)
{
    return mvref == MVREF_REF0 || mvref == MVREF_REF1 || mvref == MVREF_REF0ANDBREF;
}

static int mv_is_backward(enum MVRefEnum mvref)
{
    return mvref == MVREF_BREF || mvref == MVREF_REF0ANDBREF;
}

static int mvinfo_matches_forward(const MVInfo * a, const MVInfo * b)
{
    return a->mvref == b->mvref || (mv_is_ref0(a->mvref) && mv_is_ref0(b->mvref));
}

static int mvinfo_matches_backward(const MVInfo * a, const MVInfo * b)
{
    return mv_is_backward(a->mvref) && mv_is_backward(b->mvref);
}

static int mvinfo_is_deblock_cand(const MVInfo * a, const MVInfo * b)
{
    int diff;

    if (a->mvref != b->mvref)
        return 1;

    diff = 0;
    if (mv_is_forward(a->mvref)) {
        int dx = a->f_mv.x - b->f_mv.x;
        int dy = a->f_mv.y - b->f_mv.y;
        diff += FFABS(dx) + FFABS(dy);
    }
    if (mv_is_backward(a->mvref)) {
        int dx = a->b_mv.x - b->b_mv.x;
        int dy = a->b_mv.y - b->b_mv.y;
        diff += FFABS(dx) + FFABS(dy);
    }
    return diff > 4;
}

static void mv_pred(MV * ret, MV a, MV b, MV c)
{
#define MEDIAN(x) \
    if (a.x < b.x) \
        if (b.x < c.x) \
            ret->x = b.x; \
        else \
            ret->x = a.x < c.x ? c.x : a.x; \
    else \
        if (b.x < c.x) \
            ret->x = a.x < c.x ? a.x : c.x; \
        else \
            ret->x = b.x; \

    MEDIAN(x)
    MEDIAN(y)
}

static void predict_mv(const RV60Context * s, MVInfo * dst, int mv_x, int mv_y, int mv_w, const MVInfo * src)
{
    int mv_pos = mv_y * s->blk_stride + mv_x;
    MV f_mv, b_mv;

    dst->mvref = src->mvref;

    if (mv_is_forward(src->mvref)) {
        MV cand[3] = {0};
        int cand_size = 0;
        if (mv_x > 0) {
            const MVInfo * mv = &s->blk_info[mv_pos - 1].mv;
            if (mvinfo_matches_forward(mv, src))
                cand[cand_size++] = mv->f_mv;
        }
        if (mv_y > 0) {
            const MVInfo * mv = &s->blk_info[mv_pos - s->blk_stride].mv;
            if (mvinfo_matches_forward(mv, src))
                cand[cand_size++] = mv->f_mv;
        }
        if (has_top_block(s, mv_x << 2, mv_y << 2, mv_w << 2, 0, 4)) {
            const MVInfo * mv = &s->blk_info[mv_pos - s->blk_stride + mv_w].mv;
            if (mvinfo_matches_forward(mv, src))
                cand[cand_size++] = mv->f_mv;
        }

        switch (cand_size) {
        case 1:
            f_mv.x = cand[0].x;
            f_mv.y = cand[0].y;
            break;
        case 2:
            f_mv.x = (cand[0].x + cand[1].x) >> 1;
            f_mv.y = (cand[0].y + cand[1].y) >> 1;
            break;
        case 3:
            mv_pred(&f_mv, cand[0], cand[1], cand[2]);
            break;
        default:
            f_mv = (MV){0,0};
            break;
        }
    } else {
        f_mv = (MV){0,0};
    }

    dst->f_mv.x = src->f_mv.x + f_mv.x;
    dst->f_mv.y = src->f_mv.y + f_mv.y;

    if (mv_is_backward(src->mvref)) {
        MV cand[3] = {0};
        int cand_size = 0;
        if (mv_x > 0) {
            const MVInfo * mv = &s->blk_info[mv_pos - 1].mv;
            if (mvinfo_matches_backward(mv, src))
                cand[cand_size++] = mv->b_mv;
        }
        if (mv_y > 0) {
            const MVInfo * mv = &s->blk_info[mv_pos - s->blk_stride].mv;
            if (mvinfo_matches_backward(mv, src))
                cand[cand_size++] = mv->b_mv;
        }
        if (has_top_block(s, mv_x << 2, mv_y << 2, mv_w << 2, 0, 4)) {
            const MVInfo * mv = &s->blk_info[mv_pos - s->blk_stride + mv_w].mv;
            if (mvinfo_matches_backward(mv, src))
                cand[cand_size++] = mv->b_mv;
        }

        switch (cand_size) {
        case 1:
            b_mv.x = cand[0].x;
            b_mv.y = cand[0].y;
            break;
        case 2:
            b_mv.x = (cand[0].x + cand[1].x) >> 1;
            b_mv.y = (cand[0].y + cand[1].y) >> 1;
            break;
        case 3:
            mv_pred(&b_mv, cand[0], cand[1], cand[2]);
            break;
        default:
            b_mv = (MV){0,0};
            break;
        }
    } else {
        b_mv = (MV){0,0};
    }

    dst->b_mv.x = src->b_mv.x + b_mv.x;
    dst->b_mv.y = src->b_mv.y + b_mv.y;
}

static void reconstruct(RV60Context * s, const CUContext * cu, int size)
{
    int pu_size = size >> 3;
    PUInfo pui;
    int imode, mv_x, mv_y, mv_pos, count, mv_size;
    unique_list_mvinfo skip_cand;
    Dimensions dim;
    MVInfo mv;

    pui.cu_type = cu->cu_type;
    pui.pu_type = cu->pu_type;

    if (cu->cu_type == CU_INTRA && cu->pu_type == PU_QUARTERS) {
        s->pu_info[cu->pu_pos] = pui;
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++)
                s->blk_info[cu->blk_pos + y*s->blk_stride + x].imode =
                    reconstruct_intra(s, cu, 4, y*2 + x);
        return;
    }

    switch (cu->cu_type) {
    case CU_INTRA:
        imode = reconstruct_intra(s, cu, size, 0);
        for (int y = 0; y < size >> 2; y++)
            for (int x = 0; x < size >> 2; x++)
                s->blk_info[cu->blk_pos + y*s->blk_stride + x].imode = imode;
        break;
    case CU_INTER_MV:
        mv_x = cu->xpos >> 2;
        mv_y = cu->ypos >> 2;
        mv_pos = cu->blk_pos;
        count = pu_type_num_parts(cu->pu_type);
        for (int part_no = 0; part_no < count; part_no++) {
            MVInfo mv;
            get_mv_dimensions(&dim, cu->pu_type, part_no, size);
            predict_mv(s, &mv, mv_x, mv_y, dim.w, &cu->mv[part_no]);
            for (int y = 0; y < dim.h; y++)
                for (int x = 0; x < dim.w; x++)
                    s->blk_info[mv_pos + y*s->blk_stride + x].mv = mv;
            get_next_mv(s, &dim, cu->pu_type, part_no, &mv_pos, &mv_x, &mv_y);
        }
        break;
    default:
        unique_list_mvinfo_init(&skip_cand);
        fill_mv_skip_cand(s, cu, &skip_cand, size);
        mv = skip_cand.list[get_skip_mv_index(cu->mv[0].mvref)];
        mv_size = size >> 2;
        for (int y = 0; y < mv_size; y++)
            for (int x = 0; x < mv_size; x++)
                s->blk_info[cu->blk_pos + y*s->blk_stride + x].mv = mv;
    }

    for (int y = 0; y < pu_size; y++)
        for (int x = 0; x < pu_size; x++)
            s->pu_info[cu->pu_pos + y*s->pu_stride + x] = pui;
}

static void read_mv(GetBitContext * gb, MV * mv)
{
    mv->x = get_interleaved_se_golomb(gb);
    mv->y = get_interleaved_se_golomb(gb);
}

static void read_mv_info(RV60Context *s, GetBitContext * gb, MVInfo * mvinfo, int size, enum PUType pu_type)
{
    if (s->pict_type != AV_PICTURE_TYPE_B) {
        if (s->two_f_refs && get_bits1(gb))
            mvinfo->mvref = MVREF_REF1;
        else
            mvinfo->mvref = MVREF_REF0;
        read_mv(gb, &mvinfo->f_mv);
        mvinfo->b_mv.x = mvinfo->b_mv.y = 0;
    } else {
        if ((size <= 8 && (size != 8 || pu_type != PU_FULL)) || get_bits1(gb)) {
            if (!get_bits1(gb)) {
                mvinfo->mvref = MVREF_REF0;
                read_mv(gb, &mvinfo->f_mv);
                mvinfo->b_mv.x = mvinfo->b_mv.y = 0;
            } else {
                mvinfo->mvref = MVREF_BREF;
                mvinfo->f_mv.x = mvinfo->f_mv.y = 0;
                read_mv(gb, &mvinfo->b_mv);
            }
        } else {
            mvinfo->mvref = MVREF_REF0ANDBREF;
            read_mv(gb, &mvinfo->f_mv);
            read_mv(gb, &mvinfo->b_mv);
        }
    }
}

#define FILTER1(src, src_stride, src_y_ofs, step) \
    (      (src)[(y + src_y_ofs)*(src_stride) + x - 2*step] \
     - 5 * (src)[(y + src_y_ofs)*(src_stride) + x - 1*step] \
     +52 * (src)[(y + src_y_ofs)*(src_stride) + x         ] \
     +20 * (src)[(y + src_y_ofs)*(src_stride) + x + 1*step] \
     - 5 * (src)[(y + src_y_ofs)*(src_stride) + x + 2*step] \
     +     (src)[(y + src_y_ofs)*(src_stride) + x + 3*step] + 32) >> 6

#define FILTER2(src, src_stride, src_y_ofs, step) \
    (      (src)[(y + src_y_ofs)*(src_stride) + x - 2*step] \
     - 5 * (src)[(y + src_y_ofs)*(src_stride) + x - 1*step] \
     +20 * (src)[(y + src_y_ofs)*(src_stride) + x         ] \
     +20 * (src)[(y + src_y_ofs)*(src_stride) + x + 1*step] \
     - 5 * (src)[(y + src_y_ofs)*(src_stride) + x + 2*step] \
     +     (src)[(y + src_y_ofs)*(src_stride) + x + 3*step] + 16) >> 5

#define FILTER3(src, src_stride, src_y_ofs, step) \
    (      (src)[(y + src_y_ofs)*(src_stride) + x - 2*step] \
     - 5 * (src)[(y + src_y_ofs)*(src_stride) + x - 1*step] \
     +20 * (src)[(y + src_y_ofs)*(src_stride) + x         ] \
     +52 * (src)[(y + src_y_ofs)*(src_stride) + x + 1*step] \
     - 5 * (src)[(y + src_y_ofs)*(src_stride) + x + 2*step] \
     +     (src)[(y + src_y_ofs)*(src_stride) + x + 3*step] + 32) >> 6

#define FILTER_CASE(idx, dst, dst_stride, filter, w, h) \
    case idx: \
        for (int y = 0; y < h; y++) \
            for (int x = 0; x < w; x++) \
                 (dst)[y*dst_stride + x] = av_clip_uint8(filter); \
        break;

#define FILTER_BLOCK(dst, dst_stride, src, src_stride, src_y_ofs, w, h, cond, step) \
    switch (cond) { \
    FILTER_CASE(1, dst, dst_stride, FILTER1(src, src_stride, src_y_ofs, step), w, h) \
    FILTER_CASE(2, dst, dst_stride, FILTER2(src, src_stride, src_y_ofs, step), w, h) \
    FILTER_CASE(3, dst, dst_stride, FILTER3(src, src_stride, src_y_ofs, step), w, h) \
    }

static void luma_mc(uint8_t * dst, int dst_stride, const uint8_t * src, int src_stride, int w, int h, int cx, int cy)
{
    if (!cx && !cy) {
        for (int y = 0; y < h; y++)
            memcpy(dst + y*dst_stride, src + y*src_stride, w);
    } else if (!cy) {
        FILTER_BLOCK(dst, dst_stride, src, src_stride, 0, w, h, cx, 1)
    } else if (!cx) {
        FILTER_BLOCK(dst, dst_stride, src, src_stride, 0, w, h, cy, src_stride)
    } else if (cx != 3 || cy != 3) {
        uint8_t tmp[70 * 64];
        FILTER_BLOCK(tmp,         64, src - src_stride * 2, src_stride, 0, w, h + 5, cx, 1)
        FILTER_BLOCK(dst, dst_stride, tmp + 2*64,           64, 0, w, h,     cy, 64)
    } else {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                dst[j*dst_stride + i] = (
                    src[j*src_stride + i] +
                    src[j*src_stride + i + 1] +
                    src[(j + 1)*src_stride + i] +
                    src[(j + 1)*src_stride + i + 1] + 2) >> 2;
    }
}

static void chroma_mc(uint8_t * dst, int dst_stride, const uint8_t * src, int src_stride, int w, int h, int x, int y)
{
    if (!x && !y) {
        for (int j = 0; j < h; j++)
            memcpy(dst + j*dst_stride, src + j*src_stride, w);
    } else if (x > 0 && y > 0) {
        int a, b, c, d;

        if (x == 3 && y == 3)
            y = 2; //reproduce bug in rv60 decoder. tested with realplayer version 18.1.7.344 and 22.0.0.321

        a = (4 - x) * (4 - y);
        b =      x  * (4 - y);
        c = (4 - x) * y;
        d = x * y;
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                dst[j*dst_stride + i] =
                    (a * src[j*src_stride + i] +
                     b * src[j*src_stride + i + 1] +
                     c * src[(j + 1)*src_stride + i] +
                     d * src[(j + 1)*src_stride + i + 1] + 8) >> 4;
    } else {
        int a = (4 - x) * (4 - y);
        int e = x * (4 - y) + (4 - x) * y;
        int step = y > 0 ? src_stride : 1;
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++)
                dst[j*dst_stride + i] =
                    (a * src[j*src_stride + i] +
                     e * src[j*src_stride + i + step] + 8) >> 4;
    }
}

static int check_pos(int x, int y, int cw, int ch, int w, int h, int dx, int dy, int e0, int e1, int e2, int e3)
{
    int x2 = x + dx;
    int y2 = y + dy;
    return x2 - e0 >= 0 && x2 + cw + e1 <= w && y2 - e2 >= 0 && y2 + ch + e3 <= h;
}

static void mc(RV60Context * s, uint8_t * frame_data[3], int frame_linesize[3], const AVFrame * ref, int x, int y, int w, int h, MV mv, int avg)
{
    {
        int off = !avg ? y * frame_linesize[0] + x : 0;
        int fw = s->awidth;
        int fh = s->aheight;
        int dx = mv.x >> 2;
        int cx = mv.x & 3;
        int dy = mv.y >> 2;
        int cy = mv.y & 3;

        if (check_pos(x, y, w, h, fw, fh, dx, dy, rv60_edge1[cx], rv60_edge2[cx], rv60_edge1[cy], rv60_edge2[cy])) {
            luma_mc(
                frame_data[0] + off,
                frame_linesize[0],
                ref->data[0] + (y + dy) * ref->linesize[0] + x + dx,
                ref->linesize[0],
                w, h, cx, cy);
        } else {
            uint8_t buf[70*70];
            int xoff = x + dx - 2;
            int yoff = y + dy - 2;
            s->vdsp.emulated_edge_mc(buf,
                  ref->data[0] + yoff * ref->linesize[0] + xoff,
                  70, ref->linesize[0],
                  w + 5, h + 5,
                  xoff, yoff,
                  fw, fh);

            luma_mc(frame_data[0] + off, frame_linesize[0],
                    buf + 70 * 2 + 2, 70, w, h, cx, cy);
        }
    }
    {
        int fw = s->awidth >> 1;
        int fh = s->aheight >> 1;
        int mvx = mv.x / 2;
        int mvy = mv.y / 2;
        int dx = mvx >> 2;
        int cx = mvx & 3;
        int dy = mvy >> 2;
        int cy = mvy & 3;
        int cw = w >> 1;
        int ch = h >> 1;

        for (int plane = 1; plane < 3; plane++) {
            int off = !avg ? (y >> 1) * frame_linesize[plane] + (x >> 1) : 0;
            if (check_pos(x >> 1, y >> 1, cw, ch, fw, fh, dx, dy, 0, 1, 0, 1)) {
                chroma_mc(
                    frame_data[plane] + off,
                    frame_linesize[plane],
                    ref->data[plane] + ((y >> 1) + dy) * ref->linesize[plane] + (x >> 1) + dx,
                    ref->linesize[plane],
                    cw, ch, cx, cy);
            } else {
                uint8_t buf[40*40];
                s->vdsp.emulated_edge_mc(buf,
                    ref->data[plane] + ((y >> 1) + dy) * ref->linesize[plane] + (x >> 1) + dx,
                    40, ref->linesize[plane],
                    cw + 1, ch + 1,
                    (x >> 1) + dx, (y >> 1) + dy,
                    fw, fh);
                chroma_mc(frame_data[plane] + off, frame_linesize[plane], buf, 40, cw, ch, cx, cy);
            }
        }
    }
}

static void avg_plane(uint8_t * dst, int dst_stride, const uint8_t * src, int src_stride, int w, int h)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            dst[j*dst_stride + i] = (dst[j*dst_stride + i] + src[j*src_stride + i]) >> 1;
}

static void avg(AVFrame * frame, uint8_t * prev_frame_data[3], int prev_frame_linesize[3], int x, int y, int w, int h)
{
    for (int plane = 0; plane < 3; plane++) {
       int shift = !plane ? 0 : 1;
       avg_plane(frame->data[plane] + (y >> shift) * frame->linesize[plane] + (x >> shift), frame->linesize[plane],
                 prev_frame_data[plane], prev_frame_linesize[plane],
                 w >> shift, h >> shift);
    }
}

static int get_c4x4_set(int qp, int is_intra)
{
    if (is_intra)
        return rv60_qp_to_idx[qp + 32];
    else
        return rv60_qp_to_idx[qp];
}

static int quant(int v, int q)
{
    return (v * q + 8) >> 4;
}

static int decode_coeff(GetBitContext * gb, const CoeffVLCs * vlcs, int inval, int val)
{
    int esc_sym;

    if (inval != val)
        return inval && get_bits1(gb) ? -inval : inval;

    esc_sym = get_vlc2(gb, vlcs->esc, 9, 2);
    if (esc_sym > 23) {
        int esc_bits = esc_sym - 23;
        val += (1 << esc_bits) + get_bits(gb, esc_bits) + 22;
    } else
        val += esc_sym;

    return get_bits1(gb) ? -val : val;
}

static void decode_2x2_dc(GetBitContext * gb, const CoeffVLCs * vlcs, int16_t * coeffs, int stride, int block2, int dsc, int q_dc, int q_ac)
{
    const uint8_t * lx;
    if (!dsc)
        return;

    lx = rv60_dsc_to_lx[dsc - 1];

    coeffs[0] = quant(decode_coeff(gb, vlcs, lx[0], 3), q_dc);
    if (!block2) {
        coeffs[1]      = quant(decode_coeff(gb, vlcs, lx[1], 2), q_ac);
        coeffs[stride] = quant(decode_coeff(gb, vlcs, lx[2], 2), q_ac);
    } else {
        coeffs[stride] = quant(decode_coeff(gb, vlcs, lx[1], 2), q_ac);
        coeffs[1]      = quant(decode_coeff(gb, vlcs, lx[2], 2), q_ac);
    }
    coeffs[stride + 1] = quant(decode_coeff(gb, vlcs, lx[3], 2), q_ac);
}

static void decode_2x2(GetBitContext * gb, const CoeffVLCs * vlcs, int16_t * coeffs, int stride, int block2, int dsc, int q_ac)
{
    const uint8_t * lx;
    if (!dsc)
        return;

    lx = rv60_dsc_to_lx[dsc - 1];

    coeffs[0] = quant(decode_coeff(gb, vlcs, lx[0], 3), q_ac);
    if (!block2) {
        coeffs[1]      = quant(decode_coeff(gb, vlcs, lx[1], 2), q_ac);
        coeffs[stride] = quant(decode_coeff(gb, vlcs, lx[2], 2), q_ac);
    } else {
        coeffs[stride] = quant(decode_coeff(gb, vlcs, lx[1], 2), q_ac);
        coeffs[1]      = quant(decode_coeff(gb, vlcs, lx[2], 2), q_ac);
    }
    coeffs[stride + 1] = quant(decode_coeff(gb, vlcs, lx[3], 2), q_ac);
}

static void decode_4x4_block_dc(GetBitContext * gb, const CoeffVLCs * vlcs, int is_luma, int16_t * coeffs, int stride, int q_dc, int q_ac)
{
    int sym0 = get_vlc2(gb, vlcs->l0[!is_luma], 9, 2);
    int grp0 = sym0 >> 3;

    if (grp0)
        decode_2x2_dc(gb, vlcs, coeffs, stride, 0, grp0, q_dc, q_ac);

    if (sym0 & 4) {
        int grp = get_vlc2(gb, vlcs->l12[!is_luma], 9, 2);
        decode_2x2(gb, vlcs, coeffs + 2, stride, 0, grp, q_ac);
    }
    if (sym0 & 2) {
        int grp = get_vlc2(gb, vlcs->l12[!is_luma], 9, 2);
        decode_2x2(gb, vlcs, coeffs + 2*stride, stride, 1, grp, q_ac);
    }
    if (sym0 & 1) {
        int grp = get_vlc2(gb, vlcs->l3[!is_luma], 9, 2);
        decode_2x2(gb, vlcs, coeffs + 2*stride + 2, stride, 0, grp, q_ac);
    }
}

static void decode_4x4_block(GetBitContext * gb, const CoeffVLCs * vlcs, int is_luma, int16_t * coeffs, int stride, int q_ac)
{
    int sym0 = get_vlc2(gb, vlcs->l0[!is_luma], 9, 2);
    int grp0 = (sym0 >> 3);

    if (grp0)
        decode_2x2(gb, vlcs, coeffs, stride, 0, grp0, q_ac);

    if (sym0 & 4) {
        int grp = get_vlc2(gb, vlcs->l12[!is_luma], 9, 2);
        decode_2x2(gb, vlcs, coeffs + 2, stride, 0, grp, q_ac);
    }
    if (sym0 & 2) {
        int grp = get_vlc2(gb, vlcs->l12[!is_luma], 9, 2);
        decode_2x2(gb, vlcs, coeffs + 2*stride, stride, 1, grp, q_ac);
    }
    if (sym0 & 1) {
        int grp = get_vlc2(gb, vlcs->l3[!is_luma], 9, 2);
        decode_2x2(gb, vlcs, coeffs + 2*stride + 2, stride, 0, grp, q_ac);
    }
}

static void decode_cu_4x4in16x16(GetBitContext * gb, int is_intra, int qp, int sel_qp, int16_t * y_coeffs, int16_t * u_coeffs, int16_t * v_coeffs, int cbp)
{
    int cb_set = get_c4x4_set(sel_qp, is_intra);
    const CoeffVLCs * vlc = is_intra ? &intra_coeff_vlc[cb_set] : &inter_coeff_vlc[cb_set];
    int q_y = rv60_quants_b[qp];
    int q_c_dc = rv60_quants_b[rv60_chroma_quant_dc[qp]];
    int q_c_ac = rv60_quants_b[rv60_chroma_quant_ac[qp]];

    memset(y_coeffs, 0, sizeof(y_coeffs[0])*256);
    for (int i = 0; i < 16; i++)
        if ((cbp >> i) & 1)
            decode_4x4_block(gb, vlc, 1, y_coeffs + i * 16 , 4, q_y);

    memset(u_coeffs, 0, sizeof(u_coeffs[0])*64);
    for (int i = 0; i < 4; i++)
        if ((cbp >> (16 + i)) & 1)
            decode_4x4_block_dc(gb, vlc, 0, u_coeffs + i * 16, 4, q_c_dc, q_c_ac);

    memset(v_coeffs, 0, sizeof(v_coeffs[0])*64);
    for (int i = 0; i < 4; i++)
        if ((cbp >> (20 + i)) & 1)
            decode_4x4_block_dc(gb, vlc, 0, v_coeffs + i * 16, 4, q_c_dc, q_c_ac);
}

static int decode_cbp8(GetBitContext * gb, int subset, int qp)
{
    int cb_set = rv60_qp_to_idx[qp];
    return get_vlc2(gb, cbp8_vlc[cb_set][subset], 9, 2);
}

static void decode_cu_8x8(GetBitContext * gb, int is_intra, int qp, int sel_qp, int16_t * y_coeffs, int16_t * u_coeffs, int16_t * v_coeffs, int ccbp, int mode4x4)
{
    int cb_set = get_c4x4_set(sel_qp, is_intra);
    const CoeffVLCs * vlc = is_intra ? &intra_coeff_vlc[cb_set] : &inter_coeff_vlc[cb_set];
    int q_y = rv60_quants_b[qp];
    int q_c_dc = rv60_quants_b[rv60_chroma_quant_dc[qp]];
    int q_c_ac = rv60_quants_b[rv60_chroma_quant_ac[qp]];

    memset(y_coeffs, 0, sizeof(y_coeffs[0])*64);
    for (int i = 0; i < 4; i++) {
        if ((ccbp >> i) & 1) {
            int offset, stride;
            if (mode4x4) {
                offset = i*16;
                stride = 4;
            } else {
                offset = (i & 1) * 4 + (i & 2) * 2 * 8;
                stride = 8;
            }
            decode_4x4_block(gb, vlc, 1, y_coeffs + offset, stride, q_y);
        }
    }

    if ((ccbp >> 4) & 1) {
        memset(u_coeffs, 0, sizeof(u_coeffs[0])*16);
        decode_4x4_block_dc(gb, vlc, 0, u_coeffs, 4, q_c_dc, q_c_ac);
    }

    if ((ccbp >> 5) & 1) {
        memset(v_coeffs, 0, sizeof(u_coeffs[0])*16);
        decode_4x4_block_dc(gb, vlc, 0, v_coeffs, 4, q_c_dc, q_c_ac);
    }
}

static void decode_cu_16x16(GetBitContext * gb, int is_intra, int qp, int sel_qp, int16_t * y_coeffs, int16_t * u_coeffs, int16_t * v_coeffs, int ccbp)
{
    int cb_set = get_c4x4_set(sel_qp, is_intra);
    const CoeffVLCs * vlc = is_intra ? &intra_coeff_vlc[cb_set] : &inter_coeff_vlc[cb_set];
    int q_y = rv60_quants_b[qp];
    int q_c_dc = rv60_quants_b[rv60_chroma_quant_dc[qp]];
    int q_c_ac = rv60_quants_b[rv60_chroma_quant_ac[qp]];

    memset(y_coeffs, 0, sizeof(y_coeffs[0])*256);
    for (int i = 0; i < 16; i++)
        if ((ccbp >> i) & 1) {
            int off = (i & 3) * 4 + (i >> 2) * 4 * 16;
            decode_4x4_block(gb, vlc, 1, y_coeffs + off, 16, q_y);
        }

    memset(u_coeffs, 0, sizeof(u_coeffs[0])*64);
    for (int i = 0; i < 4; i++)
        if ((ccbp >> (16 + i)) & 1) {
            int off = (i & 1) * 4 + (i & 2) * 2 * 8;
            if (!i)
                decode_4x4_block_dc(gb, vlc, 0, u_coeffs + off, 8, q_c_dc, q_c_ac);
            else
                decode_4x4_block(gb, vlc, 0, u_coeffs + off, 8, q_c_ac);
        }

    memset(v_coeffs, 0, sizeof(v_coeffs[0])*64);
    for (int i = 0; i < 4; i++)
        if ((ccbp >> (20 + i)) & 1) {
            int off = (i & 1) * 4 + (i & 2) * 2 * 8;
            if (!i)
                decode_4x4_block_dc(gb, vlc, 0, v_coeffs + off, 8, q_c_dc, q_c_ac);
            else
                decode_4x4_block(gb, vlc, 0, v_coeffs + off, 8, q_c_ac);
        }
}

static int decode_super_cbp(GetBitContext * gb, const VLCElem * vlc[4])
{
    int sym0 = get_vlc2(gb, vlc[0], 9, 2);
    int sym1 = get_vlc2(gb, vlc[1], 9, 2);
    int sym2 = get_vlc2(gb, vlc[2], 9, 2);
    int sym3 = get_vlc2(gb, vlc[3], 9, 2);
    return 0
        + ((sym0 & 0x03) <<  0)
        + ((sym0 & 0x0C) <<  2)
        + ((sym0 & 0x10) << 12)
        + ((sym0 & 0x20) << 15)
        + ((sym1 & 0x03) <<  2)
        + ((sym1 & 0x0C) <<  4)
        + ((sym1 & 0x10) << 13)
        + ((sym1 & 0x20) << 16)
        + ((sym2 & 0x03) <<  8)
        + ((sym2 & 0x0C) << 10)
        + ((sym2 & 0x10) << 14)
        + ((sym2 & 0x20) << 17)
        + ((sym3 & 0x03) << 10)
        + ((sym3 & 0x0C) << 12)
        + ((sym3 & 0x10) << 15)
        + ((sym3 & 0x20) << 18);
}

static int decode_cbp16(GetBitContext * gb, int subset, int qp)
{
    int cb_set = rv60_qp_to_idx[qp];
    if (!subset)
        return decode_super_cbp(gb, cbp8_vlc[cb_set]);
    else
        return decode_super_cbp(gb, cbp16_vlc[cb_set][subset - 1]);
}

static int decode_cu_r(RV60Context * s, AVFrame * frame, ThreadContext * thread, GetBitContext * gb, int xpos, int ypos, int log_size, int qp, int sel_qp)
{
    int size = 1 << log_size;
    int split, ret, ttype, count, is_intra, cu_pos, subset, cbp8, imode, split_i4x4, num_clusters, cl_cbp, super_cbp, mv_x, mv_y, mv_pos;
    int16_t y_coeffs[16*16], u_coeffs[8*8], v_coeffs[8*8];
    CUContext cu;

    if (xpos >= s->awidth || ypos >= s->aheight)
        return 0;

    split = xpos + size > s->awidth || ypos + size > s->aheight || (size > 8 && get_bits1(gb));
    thread->cu_split[thread->cu_split_pos++] = split;
    if (split) {
        size >>= 1;
        log_size -= 1;
        if ((ret = decode_cu_r(s, frame, thread, gb, xpos,        ypos,        log_size, qp, sel_qp)) < 0 ||
            (ret = decode_cu_r(s, frame, thread, gb, xpos + size, ypos,        log_size, qp, sel_qp)) < 0 ||
            (ret = decode_cu_r(s, frame, thread, gb, xpos,        ypos + size, log_size, qp, sel_qp)) < 0 ||
            (ret = decode_cu_r(s, frame, thread, gb, xpos + size, ypos + size, log_size, qp, sel_qp)) < 0)
            return ret;
        return 0;
    }

    cu.xpos = xpos;
    cu.ypos = ypos;
    cu.pu_pos = (xpos >> 3) + (ypos >> 3) * s->pu_stride;
    cu.blk_pos = (xpos >> 2) + (ypos >> 2) * s->blk_stride;
    cu.cu_type = s->pict_type != AV_PICTURE_TYPE_I ? get_bits(gb, 2) : CU_INTRA;

    switch (cu.cu_type) {
    case CU_INTRA:
        cu.pu_type = size == 8 && get_bits1(gb) ? PU_QUARTERS : PU_FULL;
        if (cu.pu_type == PU_QUARTERS)
            for (int i = 0; i < 4; i++)
                cu.imode[i] = read_intra_mode(gb, &cu.imode_param[i]);
        else if (size <= 32)
            cu.imode[0] = read_intra_mode(gb, &cu.imode_param[0]);
        else
            cu.imode[0] = get_bits1(gb) ? INTRAMODE_PLANE64 : INTRAMODE_DC64;
        break;
    case CU_INTER_MV:
        cu.pu_type = get_bits(gb, size == 8 ? 2 : 3);
        count = pu_type_num_parts(cu.pu_type);
        for (int i = 0; i < count; i++)
            read_mv_info(s, gb, &cu.mv[i], size, cu.pu_type);
        break;
    default:
        cu.pu_type = PU_FULL;
        cu.mv[0].mvref = skip_mv_ref[get_unary(gb, 0, 3)];
        break;
    }

    reconstruct(s, &cu, size);

    split_i4x4 = cu.cu_type == CU_INTRA && size == 8 && cu.pu_type == PU_QUARTERS;

    switch (cu.cu_type) {
    case CU_INTRA:
        imode = s->blk_info[cu.blk_pos].imode;
        if (!split_i4x4) {
            int off = ypos * frame->linesize[0] + xpos;
            populate_ipred(s, &cu, frame->data[0], frame->linesize[0], 0, 0, size, 1);
            if (pred_angle(&cu.ipred, frame->data[0] + off, frame->linesize[0], size, imode, 1) < 0)
                return AVERROR_INVALIDDATA;
        }
        for (int plane = 1; plane < 3; plane++) {
            int off = (ypos >> 1) * frame->linesize[plane] + (xpos >> 1);
            populate_ipred(s, &cu, frame->data[plane], frame->linesize[plane], 0, 0, size >> 1, 0);
            if (pred_angle(&cu.ipred, frame->data[plane] + off, frame->linesize[plane], size >> 1, imode, 0) < 0)
                return AVERROR_INVALIDDATA;
        }
        break;
    default:
        mv_x = xpos >> 2;
        mv_y = ypos >> 2;
        mv_pos = mv_y * s->blk_stride + mv_x;
        count = pu_type_num_parts(cu.pu_type);
        for (int part_no = 0; part_no < count; part_no++) {
            MVInfo mv;
            Dimensions dim;
            int bw, bh, bx, by;

            mv = s->blk_info[mv_pos].mv;
            get_mv_dimensions(&dim, cu.pu_type, part_no, size);
            bw = dim.w << 2;
            bh = dim.h << 2;
            bx = mv_x << 2;
            by = mv_y << 2;

            switch (mv.mvref) {
            case MVREF_REF0:
                mc(s, frame->data, frame->linesize, s->last_frame[LAST_PIC], bx, by, bw, bh, mv.f_mv, 0);
                break;
            case MVREF_REF1:
                if (!s->last_frame[NEXT_PIC]->data[0]) {
                    av_log(s->avctx, AV_LOG_ERROR, "missing reference frame\n");
                    return AVERROR_INVALIDDATA;
                }
                mc(s, frame->data, frame->linesize, s->last_frame[NEXT_PIC], bx, by, bw, bh, mv.f_mv, 0);
                break;
            case MVREF_BREF:
                mc(s, frame->data, frame->linesize, s->last_frame[NEXT_PIC], bx, by, bw, bh, mv.b_mv, 0);
                break;
            case MVREF_REF0ANDBREF:
                mc(s, frame->data, frame->linesize, s->last_frame[LAST_PIC], bx, by, bw, bh, mv.f_mv, 0);
                mc(s, thread->avg_data, thread->avg_linesize, s->last_frame[NEXT_PIC], bx, by, bw, bh, mv.b_mv, 1);
                avg(frame, thread->avg_data, thread->avg_linesize, bx, by, bw, bh);
                break;
            default:
                av_assert0(0); //should never reach here
            }
            get_next_mv(s, &dim, cu.pu_type, part_no, &mv_pos, &mv_x, &mv_y);
        }
        break;
    }

    if (cu.cu_type == CU_SKIP)
        ttype = TRANSFORM_NONE;
    else if (size >= 32)
        ttype = TRANSFORM_16X16;
    else if (size == 16)
        ttype = cu.cu_type == CU_INTRA || cu.pu_type == PU_FULL ? TRANSFORM_16X16 : TRANSFORM_4X4;
    else
        ttype = cu.pu_type == PU_FULL ? TRANSFORM_8X8 : TRANSFORM_4X4;

    is_intra = cu.cu_type == CU_INTRA;
    cu_pos = ((xpos & 63) >> 3) + ((ypos & 63) >> 3) * 8;

    switch (ttype) {
    case TRANSFORM_4X4:
        subset = is_intra ? 0 : 2;
        if (size == 16) {
            int cbp16 = get_bits1(gb) ? decode_cbp16(gb, subset, sel_qp) : 0;
            if (cbp16) {
                decode_cu_4x4in16x16(gb, is_intra, qp, sel_qp, y_coeffs, u_coeffs, v_coeffs, cbp16);
                for (int y = 0; y < 4; y++)
                    for (int x = 0; x < 4; x++) {
                        int i = y*4 + x;
                        if ((cbp16 >> i) & 1) {
                            int off = (ypos + y * 4)*frame->linesize[0] + xpos + x * 4;
                            ff_rv60_idct4x4_add(y_coeffs + i*16, frame->data[0] + off, frame->linesize[0]);
                            thread->coded_blk[cu_pos + (y/2)*8 + (x/2)] = 1;
                        }
                    }
                for (int y = 0; y < 2; y++)
                    for (int x = 0; x < 2; x++) {
                        int i = y * 2 + x;
                        int xoff = (xpos >> 1) + x * 4;
                        int yoff = (ypos >> 1) + y * 4;
                        if ((cbp16 >> (16 + i)) & 1) {
                            int off = yoff * frame->linesize[1] + xoff;
                            ff_rv60_idct4x4_add(u_coeffs + i * 16, frame->data[1] + off, frame->linesize[1]);
                            thread->coded_blk[cu_pos + y*8 + x] = 1;
                        }
                        if ((cbp16 >> (20 + i)) & 1) {
                            int off = yoff * frame->linesize[2] + xoff;
                            ff_rv60_idct4x4_add(v_coeffs + i * 16, frame->data[2] + off, frame->linesize[2]);
                            thread->coded_blk[cu_pos + y*8 + x] = 1;
                        }
                    }
            }
        } else {
            cbp8 = decode_cbp8(gb, subset, sel_qp);
            if (cbp8) {
                thread->coded_blk[cu_pos] = 1;
                decode_cu_8x8(gb, is_intra, qp, sel_qp, y_coeffs, u_coeffs, v_coeffs, cbp8, 1);
            }
            for (int i = 0; i < 4; i++) {
                int xoff = (i & 1) << 2;
                int yoff = (i & 2) << 1;
                if (split_i4x4) {
                    int off = (ypos + yoff) * frame->linesize[0] + xpos + xoff;
                    int imode = s->blk_info[cu.blk_pos + (i >> 1) * s->blk_stride + (i & 1)].imode;
                    populate_ipred(s, &cu, frame->data[0], frame->linesize[0], xoff, yoff, 4, 1);
                    if (pred_angle(&cu.ipred, frame->data[0] + off, frame->linesize[0], 4, imode, 1) < 0)
                        return AVERROR_INVALIDDATA;
                }
                if ((cbp8 >> i) & 1) {
                    int off = (ypos + yoff) * frame->linesize[0] + xpos + xoff;
                    ff_rv60_idct4x4_add(y_coeffs + i * 16, frame->data[0] + off, frame->linesize[0]);
                }
            }
            if ((cbp8 >> 4) & 1) {
                int off = (ypos >> 1) * frame->linesize[1] + (xpos >> 1);
                ff_rv60_idct4x4_add(u_coeffs, frame->data[1] + off, frame->linesize[1]);
            }
            if ((cbp8 >> 5) & 1) {
                int off = (ypos >> 1) * frame->linesize[2] + (xpos >> 1);
                ff_rv60_idct4x4_add(v_coeffs, frame->data[2] + off, frame->linesize[2]);
            }
        }
        break;
    case TRANSFORM_8X8:
        subset = is_intra ? 1 : 3;
        cbp8 = decode_cbp8(gb, subset, sel_qp);
        if (cbp8) {
            thread->coded_blk[cu_pos] = 1;
            decode_cu_8x8(gb, is_intra, qp, sel_qp, y_coeffs, u_coeffs, v_coeffs, cbp8, 0);
            if (cbp8 & 0xF) {
                int off = ypos * frame->linesize[0] + xpos;
                ff_rv60_idct8x8_add(y_coeffs, frame->data[0] + off, frame->linesize[0]);
            }
            if ((cbp8 >> 4) & 1) {
                int off = (ypos >> 1) * frame->linesize[1] + (xpos >> 1);
                ff_rv60_idct4x4_add(u_coeffs, frame->data[1] + off, frame->linesize[1]);
            }
            if ((cbp8 >> 5) & 1) {
                int off = (ypos >> 1) * frame->linesize[2] + (xpos >> 1);
                ff_rv60_idct4x4_add(v_coeffs, frame->data[2] + off, frame->linesize[2]);
            }
        }
        break;
    case TRANSFORM_16X16:
        subset = is_intra ? 1 : 3;
        num_clusters = size >> 4;
        cl_cbp = get_bits(gb, num_clusters * num_clusters);
        for (int y = 0; y < num_clusters; y++) {
            for (int x = 0; x < num_clusters; x++) {
                if (!((cl_cbp >> (y*num_clusters + x)) & 1))
                    continue;
                thread->coded_blk[cu_pos + y*2*8 + x*2 + 0] = 1;
                thread->coded_blk[cu_pos + y*2*8 + x*2 + 1] = 1;
                thread->coded_blk[cu_pos + y*2*8 + x*2 + 8] = 1;
                thread->coded_blk[cu_pos + y*2*8 + x*2 + 9] = 1;
                super_cbp = decode_cbp16(gb, subset, sel_qp);
                if (super_cbp) {
                    decode_cu_16x16(gb, is_intra, qp, sel_qp, y_coeffs, u_coeffs, v_coeffs, super_cbp);
                    if (super_cbp & 0xFFFF) {
                        int off = (ypos + y * 16) * frame->linesize[0] + xpos + x * 16;
                        ff_rv60_idct16x16_add(y_coeffs, frame->data[0] + off, frame->linesize[0]);
                    }
                    if ((super_cbp >> 16) & 0xF) {
                        int off = ((ypos >> 1) + y * 8) * frame->linesize[1] + (xpos >> 1) + x * 8;
                        ff_rv60_idct8x8_add(u_coeffs, frame->data[1] + off, frame->linesize[1]);
                    }
                    if ((super_cbp >> 20) & 0xF) {
                        int off = ((ypos >> 1) + y * 8) * frame->linesize[2] + (xpos >> 1) + x * 8;
                        ff_rv60_idct8x8_add(v_coeffs, frame->data[2] + off, frame->linesize[2]);
                    }
                }
            }
        }
        break;
    }

    return 0;
}

static int deblock_get_pos(RV60Context * s, int xpos, int ypos)
{
    return (ypos >> 2) * s->dblk_stride + (xpos >> 2);
}

static void deblock_set_strength(RV60Context * s, int xpos, int ypos, int size, int q, int strength)
{
    int pos = deblock_get_pos(s, xpos, ypos);
    int dsize = size >> 2;
    int dval = (q << 2) + strength;

    for (int x = 0; x < dsize; x++) {
        s->top_str[pos + x] = dval;
        s->top_str[pos + (dsize - 1)*s->dblk_stride + x] = dval;
    }

    for (int y = 0; y < dsize; y++) {
        s->left_str[pos + y*s->dblk_stride] = dval;
        s->left_str[pos + y*s->dblk_stride + dsize - 1] = dval;
    }
}

static int deblock_get_top_strength(const RV60Context * s, int pos)
{
    return s->top_str[pos] & 3;
}

static int deblock_get_left_strength(const RV60Context * s, int pos)
{
    return s->left_str[pos] & 3;
}

static void deblock_set_top_strength(RV60Context * s, int pos, int strength)
{
    s->top_str[pos] |= strength;
}

static void deblock_set_left_strength(RV60Context * s, int pos, int strength)
{
    s->left_str[pos] |= strength;
}

static void derive_deblock_strength(RV60Context * s, int xpos, int ypos, int size)
{
    int blk_pos = (ypos >> 2) * s->blk_stride + (xpos >> 2);
    int dblk_pos = deblock_get_pos(s, xpos, ypos);
    if (ypos > 0)
        for (int i = 0; i < size; i++)
            if (!deblock_get_top_strength(s, dblk_pos - s->dblk_stride + i) && mvinfo_is_deblock_cand(&s->blk_info[blk_pos + i].mv, &s->blk_info[blk_pos - s->blk_stride + i].mv))
                deblock_set_top_strength(s, dblk_pos + i, 1);
    if (xpos > 0)
        for (int i = 0; i < size; i++)
            if (!deblock_get_left_strength(s, dblk_pos + i *s->dblk_stride - 1) && mvinfo_is_deblock_cand(&s->blk_info[blk_pos + i*s->blk_stride].mv, &s->blk_info[blk_pos + i*s->blk_stride - 1].mv))
                deblock_set_left_strength(s, dblk_pos + i *s->dblk_stride, 1);
}

#define STRENGTH(el, lim) (FFABS(el) < (lim) ? 3 : 1)
#define CLIP_SYMM(a, b) av_clip(a, -(b), b)

static void filter_luma_edge(uint8_t * dst, int step, int stride, int mode1, int mode2, int lim1, int lim2)
{
    int16_t diff_q1q0[4];
    int16_t diff_p1p0[4];
    int str_p, str_q, msum, maxprod, weak;

    for (int i = 0; i < 4; i++) {
        diff_q1q0[i] = dst[i * stride - 2*step] - dst[i*stride - step];
        diff_p1p0[i] = dst[i * stride +   step] - dst[i*stride];
    }

    str_p = STRENGTH(diff_q1q0[0] + diff_q1q0[1] + diff_q1q0[2] + diff_q1q0[3], lim2);
    str_q = STRENGTH(diff_p1p0[0] + diff_p1p0[1] + diff_p1p0[2] + diff_p1p0[3], lim2);

    if (str_p + str_q <= 2)
        return;

    msum = (mode1 + mode2 + str_q + str_p) >> 1;
    if (str_q == 1 || str_p == 1) {
        maxprod = 384;
        weak = 1;
    } else {
        maxprod = 256;
        weak = 0;
    }

    for (int y = 0; y < 4; y++) {
        int diff_p0q0 = dst[0] - dst[-step];
        int result = (lim1 * FFABS(diff_p0q0)) & -128;
        if (diff_p0q0 && result <= maxprod) {
            int diff_q1q2 = dst[-2*step] - dst[-3*step];
            int diff_p1p2 = dst[step] - dst[2*step];
            int delta;
            if (weak) {
                delta = CLIP_SYMM((diff_p0q0 + 1) >> 1, msum >> 1);
            } else {
                int diff_strg = (dst[-2*step] - dst[step] + 4 * diff_p0q0 + 4) >> 3;
                delta = CLIP_SYMM(diff_strg, msum);
            }
            dst[-step] = av_clip_uint8(dst[-step] + delta);
            dst[0]     = av_clip_uint8(dst[0] - delta);
            if (str_p != 1 && FFABS(diff_q1q2) <= (lim2 >> 2)) {
                int diff = (diff_q1q0[y] + diff_q1q2 - delta) >> 1;
                int delta_q1 = weak ? CLIP_SYMM(diff, mode1 >> 1) : CLIP_SYMM(diff, mode1);
                dst[-2 * step] = av_clip_uint8(dst[-2*step] - delta_q1);
            }
            if (str_q != 1 && FFABS(diff_p1p2) <= (lim2 >> 2)) {
                int diff = (diff_p1p0[y] + diff_p1p2 + delta) >> 1;
                int delta_p1 = weak ? CLIP_SYMM(diff, mode2 >> 1) : CLIP_SYMM(diff, mode2);
                dst[step] = av_clip_uint8(dst[step] - delta_p1);
            }
        }
        dst += stride;
    }
}

static void filter_chroma_edge(uint8_t * dst, int step, int stride, int mode1, int mode2, int lim1, int lim2)
{
    int diff_q = 4 * FFABS(dst[-2*step] - dst[-step]);
    int diff_p = 4 * FFABS(dst[   step] - dst[0]);
    int str_q = STRENGTH(diff_q, lim2);
    int str_p = STRENGTH(diff_p, lim2);
    int msum, maxprod, weak;

    if (str_p + str_q <= 2)
        return;

    msum = (mode1 + mode2 + str_q + str_p) >> 1;
    if (str_q == 1 || str_p == 1) {
        maxprod = 384;
        weak = 1;
    } else {
        maxprod = 256;
        weak = 0;
    }

    for (int y = 0; y < 2; y++) {
        int diff_pq = dst[0] - dst[-step];
        int result = (lim1 * FFABS(diff_pq)) & -128;
        if (diff_pq && result <= maxprod) {
            int delta;
            if (weak) {
                delta = CLIP_SYMM((diff_pq + 1) >> 1, msum >> 1);
            } else {
                int diff_strg = (dst[-2*step] - dst[step] + 4 * diff_pq + 4) >> 3;
                delta = CLIP_SYMM(diff_strg, msum);
            }
            dst[-step] = av_clip_uint8(dst[-step] + delta);
            dst[  0  ] = av_clip_uint8(dst[  0  ] - delta);
        }
        dst += stride;
    }
}

static void deblock_edge_ver(AVFrame * frame, int xpos, int ypos, int dblk_l, int dblk_r, int deblock_chroma)
{
    int qp_l = dblk_l >> 2;
    int str_l = dblk_l & 3;
    int qp_r = dblk_r >> 2;
    int str_r = dblk_r & 3;
    const uint8_t * dl_l = rv60_deblock_limits[qp_l];
    const uint8_t * dl_r = rv60_deblock_limits[qp_r];
    int mode_l = str_l ? dl_l[str_l - 1] : 0;
    int mode_r = str_r ? dl_r[str_r - 1] : 0;
    int lim1 = dl_r[2];
    int lim2 = dl_r[3] * 4;

    filter_luma_edge(frame->data[0] + ypos * frame->linesize[0] + xpos, 1, frame->linesize[0], mode_l, mode_r, lim1, lim2);
    if ((str_l | str_r) >= 2 && deblock_chroma)
        for (int plane = 1; plane < 3; plane++)
            filter_chroma_edge(frame->data[plane] + (ypos >> 1) * frame->linesize[plane] + (xpos >> 1), 1, frame->linesize[plane], mode_l, mode_r, lim1, lim2);
}

static void deblock_edge_hor(AVFrame * frame, int xpos, int ypos, int dblk_t, int dblk_d, int deblock_chroma)
{
    int qp_t = dblk_t >> 2;
    int str_t = dblk_t & 3;
    int qp_d = dblk_d >> 2;
    int str_d = dblk_d & 3;
    const uint8_t * dl_t = rv60_deblock_limits[qp_t];
    const uint8_t * dl_d = rv60_deblock_limits[qp_d];
    int mode_t = str_t ? dl_t[str_t - 1] : 0;
    int mode_d = str_d ? dl_d[str_d - 1] : 0;
    int lim1 = dl_d[2];
    int lim2 = dl_d[3] * 4;

    filter_luma_edge(frame->data[0] + ypos * frame->linesize[0] + xpos, frame->linesize[0], 1, mode_t, mode_d, lim1, lim2);
    if ((str_t | str_d) >= 2 && deblock_chroma)
        for (int plane = 1; plane < 3; plane++)
            filter_chroma_edge(frame->data[plane] + (ypos >> 1) * frame->linesize[plane] + (xpos >> 1), frame->linesize[plane], 1, mode_t, mode_d, lim1, lim2);
}

static void deblock8x8(const RV60Context * s, AVFrame * frame, int xpos, int ypos, int dblkpos)
{
    if (xpos > 0) {
        if (ypos > 0) {
            int str_l = s->left_str[dblkpos - s->dblk_stride - 1];
            int str_r = s->left_str[dblkpos - s->dblk_stride];
            if ((str_l | str_r) & 3)
                deblock_edge_ver(frame, xpos, ypos - 4, str_l, str_r, s->deblock_chroma);
        }
        {
            int str_l = s->left_str[dblkpos - 1];
            int str_r = s->left_str[dblkpos];
            if ((str_l | str_r) & 3)
                deblock_edge_ver(frame, xpos, ypos, str_l, str_r, s->deblock_chroma);
        }
        if (ypos + 8 >= s->aheight) {
            int str_l = s->left_str[dblkpos + s->dblk_stride - 1];
            int str_r = s->left_str[dblkpos + s->dblk_stride];
            if ((str_l | str_r) & 3)
                deblock_edge_ver(frame, xpos, ypos + 4, str_l, str_r, s->deblock_chroma);
        }
    }
    if (ypos > 0) {
        if (xpos > 0) {
            int str_t = s->top_str[dblkpos - s->dblk_stride - 1];
            int str_d = s->top_str[dblkpos - 1];
            if ((str_t | str_d) & 3)
                deblock_edge_hor(frame, xpos - 4, ypos, str_t, str_d, s->deblock_chroma);
        }
        {
            int str_t = s->top_str[dblkpos - s->dblk_stride];
            int str_d = s->top_str[dblkpos];
            if ((str_t | str_d) & 3)
                deblock_edge_hor(frame, xpos, ypos, str_t, str_d, s->deblock_chroma);
        }
        if (xpos + 8 >= s->awidth) {
            int str_t = s->top_str[dblkpos - s->dblk_stride + 1];
            int str_d = s->top_str[dblkpos + 1];
            if ((str_t | str_d) & 3)
                deblock_edge_hor(frame, xpos + 4, ypos, str_t, str_d, s->deblock_chroma);
        }
    }
}

static void deblock(const RV60Context * s, AVFrame * frame, int xpos, int ypos, int size, int dpos)
{
    for (int x = 0; x < size >> 3; x++)
        deblock8x8(s, frame, xpos + x * 8, ypos, dpos + x * 2);

    for (int y = 1; y < size >> 3; y++)
        deblock8x8(s, frame, xpos, ypos + y * 8, dpos + y * 2 * s->dblk_stride);
}

static void deblock_cu_r(RV60Context * s, AVFrame * frame, ThreadContext * thread, int xpos, int ypos, int log_size, int qp)
{
    int pu_pos, tsize, ntiles;
    enum CUType cu_type;

    if (xpos >= s->awidth || ypos >= s->aheight)
       return;

    if (thread->cu_split[thread->cu_split_pos++]) {
        int hsize = 1 << (log_size - 1);
        log_size--;
        deblock_cu_r(s, frame, thread, xpos,         ypos,         log_size, qp);
        deblock_cu_r(s, frame, thread, xpos + hsize, ypos,         log_size, qp);
        deblock_cu_r(s, frame, thread, xpos,         ypos + hsize, log_size, qp);
        deblock_cu_r(s, frame, thread, xpos + hsize, ypos + hsize, log_size, qp);
        return;
    }

    pu_pos = (ypos >> 3) * s->pu_stride + (xpos >> 3);
    cu_type = s->pu_info[pu_pos].cu_type;
    switch (log_size) {
    case 3: tsize = 3; break;
    case 4: tsize = cu_type && s->pu_info[pu_pos].pu_type ? 3 : 4; break;
    case 5:
    case 6: tsize = 4; break;
    }
    ntiles = 1 << (log_size - tsize);

    for (int ty = 0; ty < ntiles; ty++)
        for (int tx = 0; tx < ntiles; tx++) {
            int x = xpos + (tx << tsize);
            int y = ypos + (ty << tsize);
            int cu_pos = ((y & 63) >> 3) * 8 + ((x & 63) >> 3);

            if (cu_type == CU_INTRA)
                deblock_set_strength(s, x, y, 1 << tsize, qp, 2);
            else if (cu_type != CU_SKIP && thread->coded_blk[cu_pos])
                deblock_set_strength(s, x, y, 1 << tsize, qp, 1);
            else {
                deblock_set_strength(s, x, y, 1 << tsize, qp, 0);
                derive_deblock_strength(s, x, y, 1 << (tsize - 2));
            }

            deblock(s, frame, x, y, 1 << tsize, deblock_get_pos(s, x, y));
        }
}

static int read_qp_offset(GetBitContext *gb, int qp_off_type)
{
    int val;

    switch (qp_off_type) {
    case 0:
        return 0;
    case 1:
        val = read_code012(gb);
        return val != 2 ? val : -1;
    default:
        if (!get_bits1(gb))
            return 0;
        val = get_bits(gb, 2);
        if (!(val & 2))
            return val + 1;
        else
            return -((val & 1) + 1);
    }
}

static int calc_sel_qp(int osvquant, int qp)
{
    switch (osvquant) {
    case 0: return qp;
    case 1: return qp <= 25 ? qp + 5 : qp;
    default:
        if (qp <= 18)
            return qp + 10;
        else if (qp <= 25)
            return qp + 5;
        else
            return qp;
    }
}

static int decode_slice(AVCodecContext *avctx, void *tdata, int cu_y, int threadnr)
{
    RV60Context *s = avctx->priv_data;
    AVFrame * frame = tdata;
    ThreadContext thread;
    GetBitContext gb;
    int qp, sel_qp, ret;

    thread.avg_data[0] = thread.avg_buffer;
    thread.avg_data[1] = thread.avg_buffer + 64*64;
    thread.avg_data[2] = thread.avg_buffer + 64*64 + 32*32;
    thread.avg_linesize[0] = 64;
    thread.avg_linesize[1] = 32;
    thread.avg_linesize[2] = 32;

    if ((ret = init_get_bits8(&gb, s->slice[cu_y].data, s->slice[cu_y].size)) < 0)
        return ret;

    for (int cu_x = 0; cu_x < s->cu_width; cu_x++) {
        if ((s->avctx->active_thread_type & FF_THREAD_SLICE) && cu_y)
            ff_thread_progress_await(&s->progress[cu_y - 1], cu_x + 2);

        qp = s->qp + read_qp_offset(&gb, s->qp_off_type);
        if (qp < 0) {
            ret = AVERROR_INVALIDDATA;
            break;
        }
        sel_qp = calc_sel_qp(s->osvquant, qp);

        memset(thread.coded_blk, 0, sizeof(thread.coded_blk));
        thread.cu_split_pos = 0;

        if ((ret = decode_cu_r(s, frame, &thread, &gb, cu_x << 6, cu_y << 6, 6, qp, sel_qp)) < 0)
            break;

        if (s->deblock) {
            thread.cu_split_pos = 0;
            deblock_cu_r(s, frame, &thread, cu_x << 6, cu_y << 6, 6, qp);
        }

        if (s->avctx->active_thread_type & FF_THREAD_SLICE)
            ff_thread_progress_report(&s->progress[cu_y], cu_x + 1);
    }

    if (s->avctx->active_thread_type & FF_THREAD_SLICE)
        ff_thread_progress_report(&s->progress[cu_y], INT_MAX);

    return ret;
}

static int rv60_decode_frame(AVCodecContext *avctx, AVFrame * frame,
                             int * got_frame, AVPacket * avpkt)
{
    RV60Context *s = avctx->priv_data;
    GetBitContext gb;
    int ret, header_size, width, height, ofs;

    if (avpkt->size == 0) {
        if (s->last_frame[NEXT_PIC]->data[0]) {
            av_frame_move_ref(frame, s->last_frame[NEXT_PIC]);
            *got_frame = 1;
        }
        return 0;
    }

    if (avpkt->size < 9)
        return AVERROR_INVALIDDATA;

    header_size = avpkt->data[0] * 8 + 9;
    if (avpkt->size < header_size)
        return AVERROR_INVALIDDATA;

    if ((ret = init_get_bits8(&gb, avpkt->data + header_size, avpkt->size - header_size)) < 0)
        return ret;

    if ((ret = read_frame_header(s, &gb, &width, &height)) < 0)
        return ret;

    if (avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type == AV_PICTURE_TYPE_B ||
        avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type != AV_PICTURE_TYPE_I ||
        avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    if (s->pict_type != AV_PICTURE_TYPE_B)
        FFSWAP(AVFrame *, s->last_frame[NEXT_PIC], s->last_frame[LAST_PIC]);

    if ((s->pict_type == AV_PICTURE_TYPE_P && !s->last_frame[LAST_PIC]->data[0]) ||
        (s->pict_type == AV_PICTURE_TYPE_B && (!s->last_frame[LAST_PIC]->data[0] || !s->last_frame[NEXT_PIC]->data[0]))) {
        av_log(s->avctx, AV_LOG_ERROR, "missing reference frame\n");
        return AVERROR_INVALIDDATA;
    }

    s->last_frame[CUR_PIC]->pict_type = s->pict_type;
    if (s->pict_type == AV_PICTURE_TYPE_I)
        s->last_frame[CUR_PIC]->flags |= AV_FRAME_FLAG_KEY;

    if ((ret = update_dimensions_clear_info(s, width, height)) < 0)
        return ret;

    if (!s->last_frame[CUR_PIC]->data[0])
        if ((ret = ff_get_buffer(avctx, s->last_frame[CUR_PIC], 0)) < 0)
            return ret;

    if ((ret = read_slice_sizes(s, &gb)) < 0)
        return ret;

    ofs = get_bits_count(&gb) / 8;

    for (int i = 0; i < s->cu_height; i++) {
        if (header_size + ofs >= avpkt->size)
            return AVERROR_INVALIDDATA;
        s->slice[i].data = avpkt->data + header_size + ofs;
        s->slice[i].data_size = FFMIN(s->slice[i].size, avpkt->size - header_size - ofs);
        ofs += s->slice[i].size;
    }

    ret = progress_init(s, s->cu_height);
    if (ret < 0)
        return ret;

    s->avctx->execute2(s->avctx, decode_slice, s->last_frame[CUR_PIC], NULL, s->cu_height);

    ret = 0;
    if (s->pict_type == AV_PICTURE_TYPE_B)
        av_frame_move_ref(frame, s->last_frame[CUR_PIC]);
    else if (s->last_frame[LAST_PIC]->data[0])
        ret = av_frame_ref(frame, s->last_frame[LAST_PIC]);
    if (ret < 0)
        return ret;

    if (frame->data[0])
        *got_frame = 1;

    if (s->pict_type != AV_PICTURE_TYPE_B) {
        av_frame_unref(s->last_frame[NEXT_PIC]);
        FFSWAP(AVFrame *, s->last_frame[CUR_PIC], s->last_frame[NEXT_PIC]);
    }

    if (s->pict_type != AV_PICTURE_TYPE_B) {
        s->ref_pts[0] = s->ref_pts[1];
        s->ref_pts[1] = avpkt->pts;

        s->ref_ts[0] = s->ref_ts[1];
        s->ref_ts[1] = s->ts;

        if (s->ref_pts[1] > s->ref_pts[0] && s->ref_ts[1] > s->ref_ts[0])
            s->ts_scale = (s->ref_pts[1] - s->ref_pts[0]) / (s->ref_ts[1] - s->ref_ts[0]);
    } else {
        frame->pts = s->ref_pts[0] + (s->ts - s->ref_ts[0]) * s->ts_scale;
    }

    return avpkt->size;
}

static void rv60_flush(AVCodecContext *avctx)
{
    RV60Context *s = avctx->priv_data;

    for (int i = 0; i < 3; i++)
        av_frame_unref(s->last_frame[i]);
}

static av_cold int rv60_decode_end(AVCodecContext * avctx)
{
    RV60Context *s = avctx->priv_data;

    for (int i = 0; i < 3; i++)
        av_frame_free(&s->last_frame[i]);

    av_freep(&s->slice);
    av_freep(&s->pu_info);
    av_freep(&s->blk_info);
    av_freep(&s->top_str);
    av_freep(&s->left_str);

    for (int i = 0; i < s->nb_progress; i++)
        ff_thread_progress_destroy(&s->progress[i]);
    av_freep(&s->progress);

    return 0;
}

const FFCodec ff_rv60_decoder = {
    .p.name         = "rv60",
    CODEC_LONG_NAME("RealVideo 6.0"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_RV60,
    .priv_data_size = sizeof(RV60Context),
    .init           = rv60_decode_init,
    .close          = rv60_decode_end,
    FF_CODEC_DECODE_CB(rv60_decode_frame),
    .flush          = rv60_flush,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
