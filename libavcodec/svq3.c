/*
 * Copyright (c) 2003 The FFmpeg Project
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

/*
 * How to use this decoder:
 * SVQ3 data is transported within Apple Quicktime files. Quicktime files
 * have stsd atoms to describe media trak properties. A stsd atom for a
 * video trak contains 1 or more ImageDescription atoms. These atoms begin
 * with the 4-byte length of the atom followed by the codec fourcc. Some
 * decoders need information in this atom to operate correctly. Such
 * is the case with SVQ3. In order to get the best use out of this decoder,
 * the calling app must make the SVQ3 ImageDescription atom available
 * via the AVCodecContext's extradata[_size] field:
 *
 * AVCodecContext.extradata = pointer to ImageDescription, first characters
 * are expected to be 'S', 'V', 'Q', and '3', NOT the 4-byte atom length
 * AVCodecContext.extradata_size = size of ImageDescription atom memory
 * buffer (which will be the same as the ImageDescription atom size field
 * from the QT file, minus 4 bytes since the length is missing)
 *
 * You will know you have these parameters passed correctly when the decoder
 * correctly decodes this file:
 *  http://samples.mplayerhq.hu/V-codecs/SVQ3/Vertical400kbit.sorenson3.mov
 */

#include <inttypes.h>

#include "libavutil/attributes.h"
#include "internal.h"
#include "avcodec.h"
#include "mpegutils.h"
#include "h264dec.h"
#include "h264data.h"
#include "golomb.h"
#include "hpeldsp.h"
#include "mathops.h"
#include "rectangle.h"
#include "tpeldsp.h"

#if CONFIG_ZLIB
#include <zlib.h>
#endif

#include "svq1.h"

/**
 * @file
 * svq3 decoder.
 */

typedef struct SVQ3Frame {
    AVFrame *f;

    AVBufferRef *motion_val_buf[2];
    int16_t (*motion_val[2])[2];

    AVBufferRef *mb_type_buf;
    uint32_t *mb_type;


    AVBufferRef *ref_index_buf[2];
    int8_t *ref_index[2];
} SVQ3Frame;

typedef struct SVQ3Context {
    AVCodecContext *avctx;

    H264DSPContext  h264dsp;
    H264PredContext hpc;
    HpelDSPContext hdsp;
    TpelDSPContext tdsp;
    VideoDSPContext vdsp;

    SVQ3Frame *cur_pic;
    SVQ3Frame *next_pic;
    SVQ3Frame *last_pic;
    GetBitContext gb;
    GetBitContext gb_slice;
    uint8_t *slice_buf;
    int slice_size;
    int halfpel_flag;
    int thirdpel_flag;
    int has_watermark;
    uint32_t watermark_key;
    uint8_t *buf;
    int buf_size;
    int adaptive_quant;
    int next_p_frame_damaged;
    int h_edge_pos;
    int v_edge_pos;
    int last_frame_output;
    int slice_num;
    int qscale;
    int cbp;
    int frame_num;
    int frame_num_offset;
    int prev_frame_num_offset;
    int prev_frame_num;

    enum AVPictureType pict_type;
    enum AVPictureType slice_type;
    int low_delay;

    int mb_x, mb_y;
    int mb_xy;
    int mb_width, mb_height;
    int mb_stride, mb_num;
    int b_stride;

    uint32_t *mb2br_xy;

    int chroma_pred_mode;
    int intra16x16_pred_mode;

    int8_t   intra4x4_pred_mode_cache[5 * 8];
    int8_t (*intra4x4_pred_mode);

    unsigned int top_samples_available;
    unsigned int topright_samples_available;
    unsigned int left_samples_available;

    uint8_t *edge_emu_buffer;

    DECLARE_ALIGNED(16, int16_t, mv_cache)[2][5 * 8][2];
    DECLARE_ALIGNED(8,  int8_t, ref_cache)[2][5 * 8];
    DECLARE_ALIGNED(16, int16_t, mb)[16 * 48 * 2];
    DECLARE_ALIGNED(16, int16_t, mb_luma_dc)[3][16 * 2];
    DECLARE_ALIGNED(8, uint8_t, non_zero_count_cache)[15 * 8];
    uint32_t dequant4_coeff[QP_MAX_NUM + 1][16];
    int block_offset[2 * (16 * 3)];
} SVQ3Context;

#define FULLPEL_MODE  1
#define HALFPEL_MODE  2
#define THIRDPEL_MODE 3
#define PREDICT_MODE  4

/* dual scan (from some older H.264 draft)
 * o-->o-->o   o
 *         |  /|
 * o   o   o / o
 * | / |   |/  |
 * o   o   o   o
 *   /
 * o-->o-->o-->o
 */
static const uint8_t svq3_scan[16] = {
    0 + 0 * 4, 1 + 0 * 4, 2 + 0 * 4, 2 + 1 * 4,
    2 + 2 * 4, 3 + 0 * 4, 3 + 1 * 4, 3 + 2 * 4,
    0 + 1 * 4, 0 + 2 * 4, 1 + 1 * 4, 1 + 2 * 4,
    0 + 3 * 4, 1 + 3 * 4, 2 + 3 * 4, 3 + 3 * 4,
};

static const uint8_t luma_dc_zigzag_scan[16] = {
    0 * 16 + 0 * 64, 1 * 16 + 0 * 64, 2 * 16 + 0 * 64, 0 * 16 + 2 * 64,
    3 * 16 + 0 * 64, 0 * 16 + 1 * 64, 1 * 16 + 1 * 64, 2 * 16 + 1 * 64,
    1 * 16 + 2 * 64, 2 * 16 + 2 * 64, 3 * 16 + 2 * 64, 0 * 16 + 3 * 64,
    3 * 16 + 1 * 64, 1 * 16 + 3 * 64, 2 * 16 + 3 * 64, 3 * 16 + 3 * 64,
};

static const uint8_t svq3_pred_0[25][2] = {
    { 0, 0 },
    { 1, 0 }, { 0, 1 },
    { 0, 2 }, { 1, 1 }, { 2, 0 },
    { 3, 0 }, { 2, 1 }, { 1, 2 }, { 0, 3 },
    { 0, 4 }, { 1, 3 }, { 2, 2 }, { 3, 1 }, { 4, 0 },
    { 4, 1 }, { 3, 2 }, { 2, 3 }, { 1, 4 },
    { 2, 4 }, { 3, 3 }, { 4, 2 },
    { 4, 3 }, { 3, 4 },
    { 4, 4 }
};

static const int8_t svq3_pred_1[6][6][5] = {
    { { 2, -1, -1, -1, -1 }, { 2, 1, -1, -1, -1 }, { 1, 2, -1, -1, -1 },
      { 2,  1, -1, -1, -1 }, { 1, 2, -1, -1, -1 }, { 1, 2, -1, -1, -1 } },
    { { 0,  2, -1, -1, -1 }, { 0, 2,  1,  4,  3 }, { 0, 1,  2,  4,  3 },
      { 0,  2,  1,  4,  3 }, { 2, 0,  1,  3,  4 }, { 0, 4,  2,  1,  3 } },
    { { 2,  0, -1, -1, -1 }, { 2, 1,  0,  4,  3 }, { 1, 2,  4,  0,  3 },
      { 2,  1,  0,  4,  3 }, { 2, 1,  4,  3,  0 }, { 1, 2,  4,  0,  3 } },
    { { 2,  0, -1, -1, -1 }, { 2, 0,  1,  4,  3 }, { 1, 2,  0,  4,  3 },
      { 2,  1,  0,  4,  3 }, { 2, 1,  3,  4,  0 }, { 2, 4,  1,  0,  3 } },
    { { 0,  2, -1, -1, -1 }, { 0, 2,  1,  3,  4 }, { 1, 2,  3,  0,  4 },
      { 2,  0,  1,  3,  4 }, { 2, 1,  3,  0,  4 }, { 2, 0,  4,  3,  1 } },
    { { 0,  2, -1, -1, -1 }, { 0, 2,  4,  1,  3 }, { 1, 4,  2,  0,  3 },
      { 4,  2,  0,  1,  3 }, { 2, 0,  1,  4,  3 }, { 4, 2,  1,  0,  3 } },
};

static const struct {
    uint8_t run;
    uint8_t level;
} svq3_dct_tables[2][16] = {
    { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { 0, 2 }, { 3, 1 }, { 4, 1 }, { 5, 1 },
      { 0, 3 }, { 1, 2 }, { 2, 2 }, { 6, 1 }, { 7, 1 }, { 8, 1 }, { 9, 1 }, { 0, 4 } },
    { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 0, 2 }, { 2, 1 }, { 0, 3 }, { 0, 4 }, { 0, 5 },
      { 3, 1 }, { 4, 1 }, { 1, 2 }, { 1, 3 }, { 0, 6 }, { 0, 7 }, { 0, 8 }, { 0, 9 } }
};

static const uint32_t svq3_dequant_coeff[32] = {
     3881,  4351,  4890,  5481,   6154,   6914,   7761,   8718,
     9781, 10987, 12339, 13828,  15523,  17435,  19561,  21873,
    24552, 27656, 30847, 34870,  38807,  43747,  49103,  54683,
    61694, 68745, 77615, 89113, 100253, 109366, 126635, 141533
};

static int svq3_decode_end(AVCodecContext *avctx);

static void svq3_luma_dc_dequant_idct_c(int16_t *output, int16_t *input, int qp)
{
    const unsigned qmul = svq3_dequant_coeff[qp];
#define stride 16
    int i;
    int temp[16];
    static const uint8_t x_offset[4] = { 0, 1 * stride, 4 * stride, 5 * stride };

    for (i = 0; i < 4; i++) {
        const int z0 = 13 * (input[4 * i + 0] +      input[4 * i + 2]);
        const int z1 = 13 * (input[4 * i + 0] -      input[4 * i + 2]);
        const int z2 =  7 *  input[4 * i + 1] - 17 * input[4 * i + 3];
        const int z3 = 17 *  input[4 * i + 1] +  7 * input[4 * i + 3];

        temp[4 * i + 0] = z0 + z3;
        temp[4 * i + 1] = z1 + z2;
        temp[4 * i + 2] = z1 - z2;
        temp[4 * i + 3] = z0 - z3;
    }

    for (i = 0; i < 4; i++) {
        const int offset = x_offset[i];
        const int z0     = 13 * (temp[4 * 0 + i] +      temp[4 * 2 + i]);
        const int z1     = 13 * (temp[4 * 0 + i] -      temp[4 * 2 + i]);
        const int z2     =  7 *  temp[4 * 1 + i] - 17 * temp[4 * 3 + i];
        const int z3     = 17 *  temp[4 * 1 + i] +  7 * temp[4 * 3 + i];

        output[stride *  0 + offset] = (int)((z0 + z3) * qmul + 0x80000) >> 20;
        output[stride *  2 + offset] = (int)((z1 + z2) * qmul + 0x80000) >> 20;
        output[stride *  8 + offset] = (int)((z1 - z2) * qmul + 0x80000) >> 20;
        output[stride * 10 + offset] = (int)((z0 - z3) * qmul + 0x80000) >> 20;
    }
}
#undef stride

static void svq3_add_idct_c(uint8_t *dst, int16_t *block,
                            int stride, int qp, int dc)
{
    const int qmul = svq3_dequant_coeff[qp];
    int i;

    if (dc) {
        dc       = 13 * 13 * (dc == 1 ? 1538U* block[0]
                                      : qmul * (block[0] >> 3) / 2);
        block[0] = 0;
    }

    for (i = 0; i < 4; i++) {
        const int z0 = 13 * (block[0 + 4 * i] +      block[2 + 4 * i]);
        const int z1 = 13 * (block[0 + 4 * i] -      block[2 + 4 * i]);
        const int z2 =  7 *  block[1 + 4 * i] - 17 * block[3 + 4 * i];
        const int z3 = 17 *  block[1 + 4 * i] +  7 * block[3 + 4 * i];

        block[0 + 4 * i] = z0 + z3;
        block[1 + 4 * i] = z1 + z2;
        block[2 + 4 * i] = z1 - z2;
        block[3 + 4 * i] = z0 - z3;
    }

    for (i = 0; i < 4; i++) {
        const unsigned z0 = 13 * (block[i + 4 * 0] +      block[i + 4 * 2]);
        const unsigned z1 = 13 * (block[i + 4 * 0] -      block[i + 4 * 2]);
        const unsigned z2 =  7 *  block[i + 4 * 1] - 17 * block[i + 4 * 3];
        const unsigned z3 = 17 *  block[i + 4 * 1] +  7 * block[i + 4 * 3];
        const int rr = (dc + 0x80000u);

        dst[i + stride * 0] = av_clip_uint8(dst[i + stride * 0] + ((int)((z0 + z3) * qmul + rr) >> 20));
        dst[i + stride * 1] = av_clip_uint8(dst[i + stride * 1] + ((int)((z1 + z2) * qmul + rr) >> 20));
        dst[i + stride * 2] = av_clip_uint8(dst[i + stride * 2] + ((int)((z1 - z2) * qmul + rr) >> 20));
        dst[i + stride * 3] = av_clip_uint8(dst[i + stride * 3] + ((int)((z0 - z3) * qmul + rr) >> 20));
    }

    memset(block, 0, 16 * sizeof(int16_t));
}

static inline int svq3_decode_block(GetBitContext *gb, int16_t *block,
                                    int index, const int type)
{
    static const uint8_t *const scan_patterns[4] = {
        luma_dc_zigzag_scan, ff_zigzag_scan, svq3_scan, ff_h264_chroma_dc_scan
    };

    int run, level, sign, limit;
    unsigned vlc;
    const int intra           = 3 * type >> 2;
    const uint8_t *const scan = scan_patterns[type];

    for (limit = (16 >> intra); index < 16; index = limit, limit += 8) {
        for (; (vlc = get_interleaved_ue_golomb(gb)) != 0; index++) {
            if ((int32_t)vlc < 0)
                return -1;

            sign     = (vlc & 1) ? 0 : -1;
            vlc      = vlc + 1 >> 1;

            if (type == 3) {
                if (vlc < 3) {
                    run   = 0;
                    level = vlc;
                } else if (vlc < 4) {
                    run   = 1;
                    level = 1;
                } else {
                    run   = vlc & 0x3;
                    level = (vlc + 9 >> 2) - run;
                }
            } else {
                if (vlc < 16U) {
                    run   = svq3_dct_tables[intra][vlc].run;
                    level = svq3_dct_tables[intra][vlc].level;
                } else if (intra) {
                    run   = vlc & 0x7;
                    level = (vlc >> 3) + ((run == 0) ? 8 : ((run < 2) ? 2 : ((run < 5) ? 0 : -1)));
                } else {
                    run   = vlc & 0xF;
                    level = (vlc >> 4) + ((run == 0) ? 4 : ((run < 3) ? 2 : ((run < 10) ? 1 : 0)));
                }
            }


            if ((index += run) >= limit)
                return -1;

            block[scan[index]] = (level ^ sign) - sign;
        }

        if (type != 2) {
            break;
        }
    }

    return 0;
}

static av_always_inline int
svq3_fetch_diagonal_mv(const SVQ3Context *s, const int16_t **C,
                       int i, int list, int part_width)
{
    const int topright_ref = s->ref_cache[list][i - 8 + part_width];

    if (topright_ref != PART_NOT_AVAILABLE) {
        *C = s->mv_cache[list][i - 8 + part_width];
        return topright_ref;
    } else {
        *C = s->mv_cache[list][i - 8 - 1];
        return s->ref_cache[list][i - 8 - 1];
    }
}

/**
 * Get the predicted MV.
 * @param n the block index
 * @param part_width the width of the partition (4, 8,16) -> (1, 2, 4)
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static av_always_inline void svq3_pred_motion(const SVQ3Context *s, int n,
                                              int part_width, int list,
                                              int ref, int *const mx, int *const my)
{
    const int index8       = scan8[n];
    const int top_ref      = s->ref_cache[list][index8 - 8];
    const int left_ref     = s->ref_cache[list][index8 - 1];
    const int16_t *const A = s->mv_cache[list][index8 - 1];
    const int16_t *const B = s->mv_cache[list][index8 - 8];
    const int16_t *C;
    int diagonal_ref, match_count;

/* mv_cache
 * B . . A T T T T
 * U . . L . . , .
 * U . . L . . . .
 * U . . L . . , .
 * . . . L . . . .
 */

    diagonal_ref = svq3_fetch_diagonal_mv(s, &C, index8, list, part_width);
    match_count  = (diagonal_ref == ref) + (top_ref == ref) + (left_ref == ref);
    if (match_count > 1) { //most common
        *mx = mid_pred(A[0], B[0], C[0]);
        *my = mid_pred(A[1], B[1], C[1]);
    } else if (match_count == 1) {
        if (left_ref == ref) {
            *mx = A[0];
            *my = A[1];
        } else if (top_ref == ref) {
            *mx = B[0];
            *my = B[1];
        } else {
            *mx = C[0];
            *my = C[1];
        }
    } else {
        if (top_ref      == PART_NOT_AVAILABLE &&
            diagonal_ref == PART_NOT_AVAILABLE &&
            left_ref     != PART_NOT_AVAILABLE) {
            *mx = A[0];
            *my = A[1];
        } else {
            *mx = mid_pred(A[0], B[0], C[0]);
            *my = mid_pred(A[1], B[1], C[1]);
        }
    }
}

static inline void svq3_mc_dir_part(SVQ3Context *s,
                                    int x, int y, int width, int height,
                                    int mx, int my, int dxy,
                                    int thirdpel, int dir, int avg)
{
    const SVQ3Frame *pic = (dir == 0) ? s->last_pic : s->next_pic;
    uint8_t *src, *dest;
    int i, emu = 0;
    int blocksize = 2 - (width >> 3); // 16->0, 8->1, 4->2
    int linesize   = s->cur_pic->f->linesize[0];
    int uvlinesize = s->cur_pic->f->linesize[1];

    mx += x;
    my += y;

    if (mx < 0 || mx >= s->h_edge_pos - width  - 1 ||
        my < 0 || my >= s->v_edge_pos - height - 1) {
        emu = 1;
        mx = av_clip(mx, -16, s->h_edge_pos - width  + 15);
        my = av_clip(my, -16, s->v_edge_pos - height + 15);
    }

    /* form component predictions */
    dest = s->cur_pic->f->data[0] + x + y * linesize;
    src  = pic->f->data[0] + mx + my * linesize;

    if (emu) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer, src,
                                 linesize, linesize,
                                 width + 1, height + 1,
                                 mx, my, s->h_edge_pos, s->v_edge_pos);
        src = s->edge_emu_buffer;
    }
    if (thirdpel)
        (avg ? s->tdsp.avg_tpel_pixels_tab
             : s->tdsp.put_tpel_pixels_tab)[dxy](dest, src, linesize,
                                                 width, height);
    else
        (avg ? s->hdsp.avg_pixels_tab
             : s->hdsp.put_pixels_tab)[blocksize][dxy](dest, src, linesize,
                                                       height);

    if (!(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
        mx     = mx + (mx < (int) x) >> 1;
        my     = my + (my < (int) y) >> 1;
        width  = width  >> 1;
        height = height >> 1;
        blocksize++;

        for (i = 1; i < 3; i++) {
            dest = s->cur_pic->f->data[i] + (x >> 1) + (y >> 1) * uvlinesize;
            src  = pic->f->data[i] + mx + my * uvlinesize;

            if (emu) {
                s->vdsp.emulated_edge_mc(s->edge_emu_buffer, src,
                                         uvlinesize, uvlinesize,
                                         width + 1, height + 1,
                                         mx, my, (s->h_edge_pos >> 1),
                                         s->v_edge_pos >> 1);
                src = s->edge_emu_buffer;
            }
            if (thirdpel)
                (avg ? s->tdsp.avg_tpel_pixels_tab
                     : s->tdsp.put_tpel_pixels_tab)[dxy](dest, src,
                                                         uvlinesize,
                                                         width, height);
            else
                (avg ? s->hdsp.avg_pixels_tab
                     : s->hdsp.put_pixels_tab)[blocksize][dxy](dest, src,
                                                               uvlinesize,
                                                               height);
        }
    }
}

static inline int svq3_mc_dir(SVQ3Context *s, int size, int mode,
                              int dir, int avg)
{
    int i, j, k, mx, my, dx, dy, x, y;
    const int part_width    = ((size & 5) == 4) ? 4 : 16 >> (size & 1);
    const int part_height   = 16 >> ((unsigned)(size + 1) / 3);
    const int extra_width   = (mode == PREDICT_MODE) ? -16 * 6 : 0;
    const int h_edge_pos    = 6 * (s->h_edge_pos - part_width)  - extra_width;
    const int v_edge_pos    = 6 * (s->v_edge_pos - part_height) - extra_width;

    for (i = 0; i < 16; i += part_height)
        for (j = 0; j < 16; j += part_width) {
            const int b_xy = (4 * s->mb_x + (j >> 2)) +
                             (4 * s->mb_y + (i >> 2)) * s->b_stride;
            int dxy;
            x = 16 * s->mb_x + j;
            y = 16 * s->mb_y + i;
            k = (j >> 2 & 1) + (i >> 1 & 2) +
                (j >> 1 & 4) + (i      & 8);

            if (mode != PREDICT_MODE) {
                svq3_pred_motion(s, k, part_width >> 2, dir, 1, &mx, &my);
            } else {
                mx = s->next_pic->motion_val[0][b_xy][0] * 2;
                my = s->next_pic->motion_val[0][b_xy][1] * 2;

                if (dir == 0) {
                    mx = mx * s->frame_num_offset /
                         s->prev_frame_num_offset + 1 >> 1;
                    my = my * s->frame_num_offset /
                         s->prev_frame_num_offset + 1 >> 1;
                } else {
                    mx = mx * (s->frame_num_offset - s->prev_frame_num_offset) /
                         s->prev_frame_num_offset + 1 >> 1;
                    my = my * (s->frame_num_offset - s->prev_frame_num_offset) /
                         s->prev_frame_num_offset + 1 >> 1;
                }
            }

            /* clip motion vector prediction to frame border */
            mx = av_clip(mx, extra_width - 6 * x, h_edge_pos - 6 * x);
            my = av_clip(my, extra_width - 6 * y, v_edge_pos - 6 * y);

            /* get (optional) motion vector differential */
            if (mode == PREDICT_MODE) {
                dx = dy = 0;
            } else {
                dy = get_interleaved_se_golomb(&s->gb_slice);
                dx = get_interleaved_se_golomb(&s->gb_slice);

                if (dx != (int16_t)dx || dy != (int16_t)dy) {
                    av_log(s->avctx, AV_LOG_ERROR, "invalid MV vlc\n");
                    return -1;
                }
            }

            /* compute motion vector */
            if (mode == THIRDPEL_MODE) {
                int fx, fy;
                mx  = (mx + 1 >> 1) + dx;
                my  = (my + 1 >> 1) + dy;
                fx  = (unsigned)(mx + 0x30000) / 3 - 0x10000;
                fy  = (unsigned)(my + 0x30000) / 3 - 0x10000;
                dxy = (mx - 3 * fx) + 4 * (my - 3 * fy);

                svq3_mc_dir_part(s, x, y, part_width, part_height,
                                 fx, fy, dxy, 1, dir, avg);
                mx += mx;
                my += my;
            } else if (mode == HALFPEL_MODE || mode == PREDICT_MODE) {
                mx  = (unsigned)(mx + 1 + 0x30000) / 3 + dx - 0x10000;
                my  = (unsigned)(my + 1 + 0x30000) / 3 + dy - 0x10000;
                dxy = (mx & 1) + 2 * (my & 1);

                svq3_mc_dir_part(s, x, y, part_width, part_height,
                                 mx >> 1, my >> 1, dxy, 0, dir, avg);
                mx *= 3;
                my *= 3;
            } else {
                mx = (unsigned)(mx + 3 + 0x60000) / 6 + dx - 0x10000;
                my = (unsigned)(my + 3 + 0x60000) / 6 + dy - 0x10000;

                svq3_mc_dir_part(s, x, y, part_width, part_height,
                                 mx, my, 0, 0, dir, avg);
                mx *= 6;
                my *= 6;
            }

            /* update mv_cache */
            if (mode != PREDICT_MODE) {
                int32_t mv = pack16to32(mx, my);

                if (part_height == 8 && i < 8) {
                    AV_WN32A(s->mv_cache[dir][scan8[k] + 1 * 8], mv);

                    if (part_width == 8 && j < 8)
                        AV_WN32A(s->mv_cache[dir][scan8[k] + 1 + 1 * 8], mv);
                }
                if (part_width == 8 && j < 8)
                    AV_WN32A(s->mv_cache[dir][scan8[k] + 1], mv);
                if (part_width == 4 || part_height == 4)
                    AV_WN32A(s->mv_cache[dir][scan8[k]], mv);
            }

            /* write back motion vectors */
            fill_rectangle(s->cur_pic->motion_val[dir][b_xy],
                           part_width >> 2, part_height >> 2, s->b_stride,
                           pack16to32(mx, my), 4);
        }

    return 0;
}

static av_always_inline void hl_decode_mb_idct_luma(SVQ3Context *s,
                                                    int mb_type, const int *block_offset,
                                                    int linesize, uint8_t *dest_y)
{
    int i;
    if (!IS_INTRA4x4(mb_type)) {
        for (i = 0; i < 16; i++)
            if (s->non_zero_count_cache[scan8[i]] || s->mb[i * 16]) {
                uint8_t *const ptr = dest_y + block_offset[i];
                svq3_add_idct_c(ptr, s->mb + i * 16, linesize,
                                s->qscale, IS_INTRA(mb_type) ? 1 : 0);
            }
    }
}

static av_always_inline void hl_decode_mb_predict_luma(SVQ3Context *s,
                                                       int mb_type,
                                                       const int *block_offset,
                                                       int linesize,
                                                       uint8_t *dest_y)
{
    int i;
    int qscale = s->qscale;

    if (IS_INTRA4x4(mb_type)) {
        for (i = 0; i < 16; i++) {
            uint8_t *const ptr = dest_y + block_offset[i];
            const int dir      = s->intra4x4_pred_mode_cache[scan8[i]];

            uint8_t *topright;
            int nnz, tr;
            if (dir == DIAG_DOWN_LEFT_PRED || dir == VERT_LEFT_PRED) {
                const int topright_avail = (s->topright_samples_available << i) & 0x8000;
                av_assert2(s->mb_y || linesize <= block_offset[i]);
                if (!topright_avail) {
                    tr       = ptr[3 - linesize] * 0x01010101u;
                    topright = (uint8_t *)&tr;
                } else
                    topright = ptr + 4 - linesize;
            } else
                topright = NULL;

            s->hpc.pred4x4[dir](ptr, topright, linesize);
            nnz = s->non_zero_count_cache[scan8[i]];
            if (nnz) {
                svq3_add_idct_c(ptr, s->mb + i * 16, linesize, qscale, 0);
            }
        }
    } else {
        s->hpc.pred16x16[s->intra16x16_pred_mode](dest_y, linesize);
        svq3_luma_dc_dequant_idct_c(s->mb, s->mb_luma_dc[0], qscale);
    }
}

static void hl_decode_mb(SVQ3Context *s)
{
    const int mb_x    = s->mb_x;
    const int mb_y    = s->mb_y;
    const int mb_xy   = s->mb_xy;
    const int mb_type = s->cur_pic->mb_type[mb_xy];
    uint8_t *dest_y, *dest_cb, *dest_cr;
    int linesize, uvlinesize;
    int i, j;
    const int *block_offset = &s->block_offset[0];
    const int block_h   = 16 >> 1;

    linesize   = s->cur_pic->f->linesize[0];
    uvlinesize = s->cur_pic->f->linesize[1];

    dest_y  = s->cur_pic->f->data[0] + (mb_x     + mb_y * linesize)  * 16;
    dest_cb = s->cur_pic->f->data[1] +  mb_x * 8 + mb_y * uvlinesize * block_h;
    dest_cr = s->cur_pic->f->data[2] +  mb_x * 8 + mb_y * uvlinesize * block_h;

    s->vdsp.prefetch(dest_y  + (s->mb_x & 3) * 4 * linesize   + 64, linesize,      4);
    s->vdsp.prefetch(dest_cb + (s->mb_x & 7)     * uvlinesize + 64, dest_cr - dest_cb, 2);

    if (IS_INTRA(mb_type)) {
        s->hpc.pred8x8[s->chroma_pred_mode](dest_cb, uvlinesize);
        s->hpc.pred8x8[s->chroma_pred_mode](dest_cr, uvlinesize);

        hl_decode_mb_predict_luma(s, mb_type, block_offset, linesize, dest_y);
    }

    hl_decode_mb_idct_luma(s, mb_type, block_offset, linesize, dest_y);

    if (s->cbp & 0x30) {
        uint8_t *dest[2] = { dest_cb, dest_cr };
        s->h264dsp.h264_chroma_dc_dequant_idct(s->mb + 16 * 16 * 1,
                                               s->dequant4_coeff[4][0]);
        s->h264dsp.h264_chroma_dc_dequant_idct(s->mb + 16 * 16 * 2,
                                               s->dequant4_coeff[4][0]);
        for (j = 1; j < 3; j++) {
            for (i = j * 16; i < j * 16 + 4; i++)
                if (s->non_zero_count_cache[scan8[i]] || s->mb[i * 16]) {
                    uint8_t *const ptr = dest[j - 1] + block_offset[i];
                    svq3_add_idct_c(ptr, s->mb + i * 16,
                                    uvlinesize, ff_h264_chroma_qp[0][s->qscale + 12] - 12, 2);
                }
        }
    }
}

static int svq3_decode_mb(SVQ3Context *s, unsigned int mb_type)
{
    int i, j, k, m, dir, mode;
    int cbp = 0;
    uint32_t vlc;
    int8_t *top, *left;
    const int mb_xy = s->mb_xy;
    const int b_xy  = 4 * s->mb_x + 4 * s->mb_y * s->b_stride;

    s->top_samples_available      = (s->mb_y == 0) ? 0x33FF : 0xFFFF;
    s->left_samples_available     = (s->mb_x == 0) ? 0x5F5F : 0xFFFF;
    s->topright_samples_available = 0xFFFF;

    if (mb_type == 0) {           /* SKIP */
        if (s->pict_type == AV_PICTURE_TYPE_P ||
            s->next_pic->mb_type[mb_xy] == -1) {
            svq3_mc_dir_part(s, 16 * s->mb_x, 16 * s->mb_y, 16, 16,
                             0, 0, 0, 0, 0, 0);

            if (s->pict_type == AV_PICTURE_TYPE_B)
                svq3_mc_dir_part(s, 16 * s->mb_x, 16 * s->mb_y, 16, 16,
                                 0, 0, 0, 0, 1, 1);

            mb_type = MB_TYPE_SKIP;
        } else {
            mb_type = FFMIN(s->next_pic->mb_type[mb_xy], 6);
            if (svq3_mc_dir(s, mb_type, PREDICT_MODE, 0, 0) < 0)
                return -1;
            if (svq3_mc_dir(s, mb_type, PREDICT_MODE, 1, 1) < 0)
                return -1;

            mb_type = MB_TYPE_16x16;
        }
    } else if (mb_type < 8) {     /* INTER */
        if (s->thirdpel_flag && s->halfpel_flag == !get_bits1(&s->gb_slice))
            mode = THIRDPEL_MODE;
        else if (s->halfpel_flag &&
                 s->thirdpel_flag == !get_bits1(&s->gb_slice))
            mode = HALFPEL_MODE;
        else
            mode = FULLPEL_MODE;

        /* fill caches */
        /* note ref_cache should contain here:
         *  ????????
         *  ???11111
         *  N??11111
         *  N??11111
         *  N??11111
         */

        for (m = 0; m < 2; m++) {
            if (s->mb_x > 0 && s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - 1] + 6] != -1) {
                for (i = 0; i < 4; i++)
                    AV_COPY32(s->mv_cache[m][scan8[0] - 1 + i * 8],
                              s->cur_pic->motion_val[m][b_xy - 1 + i * s->b_stride]);
            } else {
                for (i = 0; i < 4; i++)
                    AV_ZERO32(s->mv_cache[m][scan8[0] - 1 + i * 8]);
            }
            if (s->mb_y > 0) {
                memcpy(s->mv_cache[m][scan8[0] - 1 * 8],
                       s->cur_pic->motion_val[m][b_xy - s->b_stride],
                       4 * 2 * sizeof(int16_t));
                memset(&s->ref_cache[m][scan8[0] - 1 * 8],
                       (s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - s->mb_stride]] == -1) ? PART_NOT_AVAILABLE : 1, 4);

                if (s->mb_x < s->mb_width - 1) {
                    AV_COPY32(s->mv_cache[m][scan8[0] + 4 - 1 * 8],
                              s->cur_pic->motion_val[m][b_xy - s->b_stride + 4]);
                    s->ref_cache[m][scan8[0] + 4 - 1 * 8] =
                        (s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - s->mb_stride + 1] + 6] == -1 ||
                         s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - s->mb_stride]] == -1) ? PART_NOT_AVAILABLE : 1;
                } else
                    s->ref_cache[m][scan8[0] + 4 - 1 * 8] = PART_NOT_AVAILABLE;
                if (s->mb_x > 0) {
                    AV_COPY32(s->mv_cache[m][scan8[0] - 1 - 1 * 8],
                              s->cur_pic->motion_val[m][b_xy - s->b_stride - 1]);
                    s->ref_cache[m][scan8[0] - 1 - 1 * 8] =
                        (s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - s->mb_stride - 1] + 3] == -1) ? PART_NOT_AVAILABLE : 1;
                } else
                    s->ref_cache[m][scan8[0] - 1 - 1 * 8] = PART_NOT_AVAILABLE;
            } else
                memset(&s->ref_cache[m][scan8[0] - 1 * 8 - 1],
                       PART_NOT_AVAILABLE, 8);

            if (s->pict_type != AV_PICTURE_TYPE_B)
                break;
        }

        /* decode motion vector(s) and form prediction(s) */
        if (s->pict_type == AV_PICTURE_TYPE_P) {
            if (svq3_mc_dir(s, mb_type - 1, mode, 0, 0) < 0)
                return -1;
        } else {        /* AV_PICTURE_TYPE_B */
            if (mb_type != 2) {
                if (svq3_mc_dir(s, 0, mode, 0, 0) < 0)
                    return -1;
            } else {
                for (i = 0; i < 4; i++)
                    memset(s->cur_pic->motion_val[0][b_xy + i * s->b_stride],
                           0, 4 * 2 * sizeof(int16_t));
            }
            if (mb_type != 1) {
                if (svq3_mc_dir(s, 0, mode, 1, mb_type == 3) < 0)
                    return -1;
            } else {
                for (i = 0; i < 4; i++)
                    memset(s->cur_pic->motion_val[1][b_xy + i * s->b_stride],
                           0, 4 * 2 * sizeof(int16_t));
            }
        }

        mb_type = MB_TYPE_16x16;
    } else if (mb_type == 8 || mb_type == 33) {   /* INTRA4x4 */
        int8_t *i4x4       = s->intra4x4_pred_mode + s->mb2br_xy[s->mb_xy];
        int8_t *i4x4_cache = s->intra4x4_pred_mode_cache;

        memset(s->intra4x4_pred_mode_cache, -1, 8 * 5 * sizeof(int8_t));

        if (mb_type == 8) {
            if (s->mb_x > 0) {
                for (i = 0; i < 4; i++)
                    s->intra4x4_pred_mode_cache[scan8[0] - 1 + i * 8] = s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - 1] + 6 - i];
                if (s->intra4x4_pred_mode_cache[scan8[0] - 1] == -1)
                    s->left_samples_available = 0x5F5F;
            }
            if (s->mb_y > 0) {
                s->intra4x4_pred_mode_cache[4 + 8 * 0] = s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - s->mb_stride] + 0];
                s->intra4x4_pred_mode_cache[5 + 8 * 0] = s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - s->mb_stride] + 1];
                s->intra4x4_pred_mode_cache[6 + 8 * 0] = s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - s->mb_stride] + 2];
                s->intra4x4_pred_mode_cache[7 + 8 * 0] = s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - s->mb_stride] + 3];

                if (s->intra4x4_pred_mode_cache[4 + 8 * 0] == -1)
                    s->top_samples_available = 0x33FF;
            }

            /* decode prediction codes for luma blocks */
            for (i = 0; i < 16; i += 2) {
                vlc = get_interleaved_ue_golomb(&s->gb_slice);

                if (vlc >= 25U) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "luma prediction:%"PRIu32"\n", vlc);
                    return -1;
                }

                left = &s->intra4x4_pred_mode_cache[scan8[i] - 1];
                top  = &s->intra4x4_pred_mode_cache[scan8[i] - 8];

                left[1] = svq3_pred_1[top[0] + 1][left[0] + 1][svq3_pred_0[vlc][0]];
                left[2] = svq3_pred_1[top[1] + 1][left[1] + 1][svq3_pred_0[vlc][1]];

                if (left[1] == -1 || left[2] == -1) {
                    av_log(s->avctx, AV_LOG_ERROR, "weird prediction\n");
                    return -1;
                }
            }
        } else {    /* mb_type == 33, DC_128_PRED block type */
            for (i = 0; i < 4; i++)
                memset(&s->intra4x4_pred_mode_cache[scan8[0] + 8 * i], DC_PRED, 4);
        }

        AV_COPY32(i4x4, i4x4_cache + 4 + 8 * 4);
        i4x4[4] = i4x4_cache[7 + 8 * 3];
        i4x4[5] = i4x4_cache[7 + 8 * 2];
        i4x4[6] = i4x4_cache[7 + 8 * 1];

        if (mb_type == 8) {
            ff_h264_check_intra4x4_pred_mode(s->intra4x4_pred_mode_cache,
                                             s->avctx, s->top_samples_available,
                                             s->left_samples_available);

            s->top_samples_available  = (s->mb_y == 0) ? 0x33FF : 0xFFFF;
            s->left_samples_available = (s->mb_x == 0) ? 0x5F5F : 0xFFFF;
        } else {
            for (i = 0; i < 4; i++)
                memset(&s->intra4x4_pred_mode_cache[scan8[0] + 8 * i], DC_128_PRED, 4);

            s->top_samples_available  = 0x33FF;
            s->left_samples_available = 0x5F5F;
        }

        mb_type = MB_TYPE_INTRA4x4;
    } else {                      /* INTRA16x16 */
        dir = ff_h264_i_mb_type_info[mb_type - 8].pred_mode;
        dir = (dir >> 1) ^ 3 * (dir & 1) ^ 1;

        if ((s->intra16x16_pred_mode = ff_h264_check_intra_pred_mode(s->avctx, s->top_samples_available,
                                                                     s->left_samples_available, dir, 0)) < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "ff_h264_check_intra_pred_mode < 0\n");
            return s->intra16x16_pred_mode;
        }

        cbp     = ff_h264_i_mb_type_info[mb_type - 8].cbp;
        mb_type = MB_TYPE_INTRA16x16;
    }

    if (!IS_INTER(mb_type) && s->pict_type != AV_PICTURE_TYPE_I) {
        for (i = 0; i < 4; i++)
            memset(s->cur_pic->motion_val[0][b_xy + i * s->b_stride],
                   0, 4 * 2 * sizeof(int16_t));
        if (s->pict_type == AV_PICTURE_TYPE_B) {
            for (i = 0; i < 4; i++)
                memset(s->cur_pic->motion_val[1][b_xy + i * s->b_stride],
                       0, 4 * 2 * sizeof(int16_t));
        }
    }
    if (!IS_INTRA4x4(mb_type)) {
        memset(s->intra4x4_pred_mode + s->mb2br_xy[mb_xy], DC_PRED, 8);
    }
    if (!IS_SKIP(mb_type) || s->pict_type == AV_PICTURE_TYPE_B) {
        memset(s->non_zero_count_cache + 8, 0, 14 * 8 * sizeof(uint8_t));
    }

    if (!IS_INTRA16x16(mb_type) &&
        (!IS_SKIP(mb_type) || s->pict_type == AV_PICTURE_TYPE_B)) {
        if ((vlc = get_interleaved_ue_golomb(&s->gb_slice)) >= 48U){
            av_log(s->avctx, AV_LOG_ERROR, "cbp_vlc=%"PRIu32"\n", vlc);
            return -1;
        }

        cbp = IS_INTRA(mb_type) ? ff_h264_golomb_to_intra4x4_cbp[vlc]
                                : ff_h264_golomb_to_inter_cbp[vlc];
    }
    if (IS_INTRA16x16(mb_type) ||
        (s->pict_type != AV_PICTURE_TYPE_I && s->adaptive_quant && cbp)) {
        s->qscale += get_interleaved_se_golomb(&s->gb_slice);

        if (s->qscale > 31u) {
            av_log(s->avctx, AV_LOG_ERROR, "qscale:%d\n", s->qscale);
            return -1;
        }
    }
    if (IS_INTRA16x16(mb_type)) {
        AV_ZERO128(s->mb_luma_dc[0] + 0);
        AV_ZERO128(s->mb_luma_dc[0] + 8);
        if (svq3_decode_block(&s->gb_slice, s->mb_luma_dc[0], 0, 1)) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "error while decoding intra luma dc\n");
            return -1;
        }
    }

    if (cbp) {
        const int index = IS_INTRA16x16(mb_type) ? 1 : 0;
        const int type  = ((s->qscale < 24 && IS_INTRA4x4(mb_type)) ? 2 : 1);

        for (i = 0; i < 4; i++)
            if ((cbp & (1 << i))) {
                for (j = 0; j < 4; j++) {
                    k = index ? (1 * (j & 1) + 2 * (i & 1) +
                                 2 * (j & 2) + 4 * (i & 2))
                              : (4 * i + j);
                    s->non_zero_count_cache[scan8[k]] = 1;

                    if (svq3_decode_block(&s->gb_slice, &s->mb[16 * k], index, type)) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "error while decoding block\n");
                        return -1;
                    }
                }
            }

        if ((cbp & 0x30)) {
            for (i = 1; i < 3; ++i)
                if (svq3_decode_block(&s->gb_slice, &s->mb[16 * 16 * i], 0, 3)) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "error while decoding chroma dc block\n");
                    return -1;
                }

            if ((cbp & 0x20)) {
                for (i = 1; i < 3; i++) {
                    for (j = 0; j < 4; j++) {
                        k                                 = 16 * i + j;
                        s->non_zero_count_cache[scan8[k]] = 1;

                        if (svq3_decode_block(&s->gb_slice, &s->mb[16 * k], 1, 1)) {
                            av_log(s->avctx, AV_LOG_ERROR,
                                   "error while decoding chroma ac block\n");
                            return -1;
                        }
                    }
                }
            }
        }
    }

    s->cbp                     = cbp;
    s->cur_pic->mb_type[mb_xy] = mb_type;

    if (IS_INTRA(mb_type))
        s->chroma_pred_mode = ff_h264_check_intra_pred_mode(s->avctx, s->top_samples_available,
                                                            s->left_samples_available, DC_PRED8x8, 1);

    return 0;
}

static int svq3_decode_slice_header(AVCodecContext *avctx)
{
    SVQ3Context *s = avctx->priv_data;
    const int mb_xy   = s->mb_xy;
    int i, header;
    unsigned slice_id;

    header = get_bits(&s->gb, 8);

    if (((header & 0x9F) != 1 && (header & 0x9F) != 2) || (header & 0x60) == 0) {
        /* TODO: what? */
        av_log(avctx, AV_LOG_ERROR, "unsupported slice header (%02X)\n", header);
        return -1;
    } else {
        int slice_bits, slice_bytes, slice_length;
        int length = header >> 5 & 3;

        slice_length = show_bits(&s->gb, 8 * length);
        slice_bits   = slice_length * 8;
        slice_bytes  = slice_length + length - 1;

        skip_bits(&s->gb, 8);

        av_fast_malloc(&s->slice_buf, &s->slice_size, slice_bytes + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!s->slice_buf)
            return AVERROR(ENOMEM);

        if (slice_bytes * 8LL > get_bits_left(&s->gb)) {
            av_log(avctx, AV_LOG_ERROR, "slice after bitstream end\n");
            return AVERROR_INVALIDDATA;
        }
        memcpy(s->slice_buf, s->gb.buffer + s->gb.index / 8, slice_bytes);

        init_get_bits(&s->gb_slice, s->slice_buf, slice_bits);

        if (s->watermark_key) {
            uint32_t header = AV_RL32(&s->gb_slice.buffer[1]);
            AV_WL32(&s->gb_slice.buffer[1], header ^ s->watermark_key);
        }
        if (length > 0) {
            memmove(s->slice_buf, &s->slice_buf[slice_length], length - 1);
        }
        skip_bits_long(&s->gb, slice_bytes * 8);
    }

    if ((slice_id = get_interleaved_ue_golomb(&s->gb_slice)) >= 3) {
        av_log(s->avctx, AV_LOG_ERROR, "illegal slice type %u \n", slice_id);
        return -1;
    }
    if (get_bits1(&s->gb_slice)) {
        avpriv_report_missing_feature(s->avctx, "Media key encryption");
        return AVERROR_PATCHWELCOME;
    }

    s->slice_type = ff_h264_golomb_to_pict_type[slice_id];

    if ((header & 0x9F) == 2) {
        i = (s->mb_num < 64) ? 5 : av_log2(s->mb_num - 1);
        get_bits(&s->gb_slice, i);
    }

    s->slice_num      = get_bits(&s->gb_slice, 8);
    s->qscale         = get_bits(&s->gb_slice, 5);
    s->adaptive_quant = get_bits1(&s->gb_slice);

    /* unknown fields */
    skip_bits1(&s->gb_slice);

    if (s->has_watermark)
        skip_bits1(&s->gb_slice);

    skip_bits1(&s->gb_slice);
    skip_bits(&s->gb_slice, 2);

    if (skip_1stop_8data_bits(&s->gb_slice) < 0)
        return AVERROR_INVALIDDATA;

    /* reset intra predictors and invalidate motion vector references */
    if (s->mb_x > 0) {
        memset(s->intra4x4_pred_mode + s->mb2br_xy[mb_xy - 1] + 3,
               -1, 4 * sizeof(int8_t));
        memset(s->intra4x4_pred_mode + s->mb2br_xy[mb_xy - s->mb_x],
               -1, 8 * sizeof(int8_t) * s->mb_x);
    }
    if (s->mb_y > 0) {
        memset(s->intra4x4_pred_mode + s->mb2br_xy[mb_xy - s->mb_stride],
               -1, 8 * sizeof(int8_t) * (s->mb_width - s->mb_x));

        if (s->mb_x > 0)
            s->intra4x4_pred_mode[s->mb2br_xy[mb_xy - s->mb_stride - 1] + 3] = -1;
    }

    return 0;
}

static void init_dequant4_coeff_table(SVQ3Context *s)
{
    int q, x;
    const int max_qp = 51;

    for (q = 0; q < max_qp + 1; q++) {
        int shift = ff_h264_quant_div6[q] + 2;
        int idx   = ff_h264_quant_rem6[q];
        for (x = 0; x < 16; x++)
            s->dequant4_coeff[q][(x >> 2) | ((x << 2) & 0xF)] =
                ((uint32_t)ff_h264_dequant4_coeff_init[idx][(x & 1) + ((x >> 2) & 1)] * 16) << shift;
    }
}

static av_cold int svq3_decode_init(AVCodecContext *avctx)
{
    SVQ3Context *s = avctx->priv_data;
    int m, x, y;
    unsigned char *extradata;
    unsigned char *extradata_end;
    unsigned int size;
    int marker_found = 0;
    int ret;

    s->cur_pic  = av_mallocz(sizeof(*s->cur_pic));
    s->last_pic = av_mallocz(sizeof(*s->last_pic));
    s->next_pic = av_mallocz(sizeof(*s->next_pic));
    if (!s->next_pic || !s->last_pic || !s->cur_pic) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->cur_pic->f  = av_frame_alloc();
    s->last_pic->f = av_frame_alloc();
    s->next_pic->f = av_frame_alloc();
    if (!s->cur_pic->f || !s->last_pic->f || !s->next_pic->f)
        return AVERROR(ENOMEM);

    ff_h264dsp_init(&s->h264dsp, 8, 1);
    ff_h264_pred_init(&s->hpc, AV_CODEC_ID_SVQ3, 8, 1);
    ff_videodsp_init(&s->vdsp, 8);


    avctx->bits_per_raw_sample = 8;

    ff_hpeldsp_init(&s->hdsp, avctx->flags);
    ff_tpeldsp_init(&s->tdsp);

    avctx->pix_fmt     = AV_PIX_FMT_YUVJ420P;
    avctx->color_range = AVCOL_RANGE_JPEG;

    s->avctx         = avctx;
    s->halfpel_flag  = 1;
    s->thirdpel_flag = 1;
    s->has_watermark = 0;

    /* prowl for the "SEQH" marker in the extradata */
    extradata     = (unsigned char *)avctx->extradata;
    extradata_end = avctx->extradata + avctx->extradata_size;
    if (extradata) {
        for (m = 0; m + 8 < avctx->extradata_size; m++) {
            if (!memcmp(extradata, "SEQH", 4)) {
                marker_found = 1;
                break;
            }
            extradata++;
        }
    }

    /* if a match was found, parse the extra data */
    if (marker_found) {
        GetBitContext gb;
        int frame_size_code;
        int unk0, unk1, unk2, unk3, unk4;

        size = AV_RB32(&extradata[4]);
        if (size > extradata_end - extradata - 8) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        init_get_bits(&gb, extradata + 8, size * 8);

        /* 'frame size code' and optional 'width, height' */
        frame_size_code = get_bits(&gb, 3);
        switch (frame_size_code) {
        case 0:
            avctx->width  = 160;
            avctx->height = 120;
            break;
        case 1:
            avctx->width  = 128;
            avctx->height =  96;
            break;
        case 2:
            avctx->width  = 176;
            avctx->height = 144;
            break;
        case 3:
            avctx->width  = 352;
            avctx->height = 288;
            break;
        case 4:
            avctx->width  = 704;
            avctx->height = 576;
            break;
        case 5:
            avctx->width  = 240;
            avctx->height = 180;
            break;
        case 6:
            avctx->width  = 320;
            avctx->height = 240;
            break;
        case 7:
            avctx->width  = get_bits(&gb, 12);
            avctx->height = get_bits(&gb, 12);
            break;
        }

        s->halfpel_flag  = get_bits1(&gb);
        s->thirdpel_flag = get_bits1(&gb);

        /* unknown fields */
        unk0 = get_bits1(&gb);
        unk1 = get_bits1(&gb);
        unk2 = get_bits1(&gb);
        unk3 = get_bits1(&gb);

        s->low_delay = get_bits1(&gb);

        /* unknown field */
        unk4 = get_bits1(&gb);

        av_log(avctx, AV_LOG_DEBUG, "Unknown fields %d %d %d %d %d\n",
               unk0, unk1, unk2, unk3, unk4);

        if (skip_1stop_8data_bits(&gb) < 0) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        s->has_watermark  = get_bits1(&gb);
        avctx->has_b_frames = !s->low_delay;
        if (s->has_watermark) {
#if CONFIG_ZLIB
            unsigned watermark_width  = get_interleaved_ue_golomb(&gb);
            unsigned watermark_height = get_interleaved_ue_golomb(&gb);
            int u1                    = get_interleaved_ue_golomb(&gb);
            int u2                    = get_bits(&gb, 8);
            int u3                    = get_bits(&gb, 2);
            int u4                    = get_interleaved_ue_golomb(&gb);
            unsigned long buf_len     = watermark_width *
                                        watermark_height * 4;
            int offset                = get_bits_count(&gb) + 7 >> 3;
            uint8_t *buf;

            if (watermark_height <= 0 ||
                (uint64_t)watermark_width * 4 > UINT_MAX / watermark_height) {
                ret = -1;
                goto fail;
            }

            buf = av_malloc(buf_len);
            if (!buf) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            av_log(avctx, AV_LOG_DEBUG, "watermark size: %ux%u\n",
                   watermark_width, watermark_height);
            av_log(avctx, AV_LOG_DEBUG,
                   "u1: %x u2: %x u3: %x compressed data size: %d offset: %d\n",
                   u1, u2, u3, u4, offset);
            if (uncompress(buf, &buf_len, extradata + 8 + offset,
                           size - offset) != Z_OK) {
                av_log(avctx, AV_LOG_ERROR,
                       "could not uncompress watermark logo\n");
                av_free(buf);
                ret = -1;
                goto fail;
            }
            s->watermark_key = ff_svq1_packet_checksum(buf, buf_len, 0);
            s->watermark_key = s->watermark_key << 16 | s->watermark_key;
            av_log(avctx, AV_LOG_DEBUG,
                   "watermark key %#"PRIx32"\n", s->watermark_key);
            av_free(buf);
#else
            av_log(avctx, AV_LOG_ERROR,
                   "this svq3 file contains watermark which need zlib support compiled in\n");
            ret = -1;
            goto fail;
#endif
        }
    }

    s->mb_width   = (avctx->width + 15) / 16;
    s->mb_height  = (avctx->height + 15) / 16;
    s->mb_stride  = s->mb_width + 1;
    s->mb_num     = s->mb_width * s->mb_height;
    s->b_stride   = 4 * s->mb_width;
    s->h_edge_pos = s->mb_width * 16;
    s->v_edge_pos = s->mb_height * 16;

    s->intra4x4_pred_mode = av_mallocz(s->mb_stride * 2 * 8);
    if (!s->intra4x4_pred_mode)
        return AVERROR(ENOMEM);

    s->mb2br_xy = av_mallocz(s->mb_stride * (s->mb_height + 1) *
                             sizeof(*s->mb2br_xy));
    if (!s->mb2br_xy)
        return AVERROR(ENOMEM);

    for (y = 0; y < s->mb_height; y++)
        for (x = 0; x < s->mb_width; x++) {
            const int mb_xy = x + y * s->mb_stride;

            s->mb2br_xy[mb_xy] = 8 * (mb_xy % (2 * s->mb_stride));
        }

    init_dequant4_coeff_table(s);

    return 0;
fail:
    svq3_decode_end(avctx);
    return ret;
}

static void free_picture(AVCodecContext *avctx, SVQ3Frame *pic)
{
    int i;
    for (i = 0; i < 2; i++) {
        av_buffer_unref(&pic->motion_val_buf[i]);
        av_buffer_unref(&pic->ref_index_buf[i]);
    }
    av_buffer_unref(&pic->mb_type_buf);

    av_frame_unref(pic->f);
}

static int get_buffer(AVCodecContext *avctx, SVQ3Frame *pic)
{
    SVQ3Context *s = avctx->priv_data;
    const int big_mb_num    = s->mb_stride * (s->mb_height + 1) + 1;
    const int mb_array_size = s->mb_stride * s->mb_height;
    const int b4_stride     = s->mb_width * 4 + 1;
    const int b4_array_size = b4_stride * s->mb_height * 4;
    int ret;

    if (!pic->motion_val_buf[0]) {
        int i;

        pic->mb_type_buf = av_buffer_allocz((big_mb_num + s->mb_stride) * sizeof(uint32_t));
        if (!pic->mb_type_buf)
            return AVERROR(ENOMEM);
        pic->mb_type = (uint32_t*)pic->mb_type_buf->data + 2 * s->mb_stride + 1;

        for (i = 0; i < 2; i++) {
            pic->motion_val_buf[i] = av_buffer_allocz(2 * (b4_array_size + 4) * sizeof(int16_t));
            pic->ref_index_buf[i]  = av_buffer_allocz(4 * mb_array_size);
            if (!pic->motion_val_buf[i] || !pic->ref_index_buf[i]) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            pic->motion_val[i] = (int16_t (*)[2])pic->motion_val_buf[i]->data + 4;
            pic->ref_index[i]  = pic->ref_index_buf[i]->data;
        }
    }

    ret = ff_get_buffer(avctx, pic->f,
                        (s->pict_type != AV_PICTURE_TYPE_B) ?
                         AV_GET_BUFFER_FLAG_REF : 0);
    if (ret < 0)
        goto fail;

    if (!s->edge_emu_buffer) {
        s->edge_emu_buffer = av_mallocz_array(pic->f->linesize[0], 17);
        if (!s->edge_emu_buffer)
            return AVERROR(ENOMEM);
    }

    return 0;
fail:
    free_picture(avctx, pic);
    return ret;
}

static int svq3_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    SVQ3Context *s     = avctx->priv_data;
    int buf_size       = avpkt->size;
    int left;
    uint8_t *buf;
    int ret, m, i;

    /* special case for last picture */
    if (buf_size == 0) {
        if (s->next_pic->f->data[0] && !s->low_delay && !s->last_frame_output) {
            ret = av_frame_ref(data, s->next_pic->f);
            if (ret < 0)
                return ret;
            s->last_frame_output = 1;
            *got_frame          = 1;
        }
        return 0;
    }

    s->mb_x = s->mb_y = s->mb_xy = 0;

    if (s->watermark_key) {
        av_fast_padded_malloc(&s->buf, &s->buf_size, buf_size);
        if (!s->buf)
            return AVERROR(ENOMEM);
        memcpy(s->buf, avpkt->data, buf_size);
        buf = s->buf;
    } else {
        buf = avpkt->data;
    }

    ret = init_get_bits(&s->gb, buf, 8 * buf_size);
    if (ret < 0)
        return ret;

    if (svq3_decode_slice_header(avctx))
        return -1;

    s->pict_type = s->slice_type;

    if (s->pict_type != AV_PICTURE_TYPE_B)
        FFSWAP(SVQ3Frame*, s->next_pic, s->last_pic);

    av_frame_unref(s->cur_pic->f);

    /* for skipping the frame */
    s->cur_pic->f->pict_type = s->pict_type;
    s->cur_pic->f->key_frame = (s->pict_type == AV_PICTURE_TYPE_I);

    ret = get_buffer(avctx, s->cur_pic);
    if (ret < 0)
        return ret;

    for (i = 0; i < 16; i++) {
        s->block_offset[i]           = (4 * ((scan8[i] - scan8[0]) & 7)) + 4 * s->cur_pic->f->linesize[0] * ((scan8[i] - scan8[0]) >> 3);
        s->block_offset[48 + i]      = (4 * ((scan8[i] - scan8[0]) & 7)) + 8 * s->cur_pic->f->linesize[0] * ((scan8[i] - scan8[0]) >> 3);
    }
    for (i = 0; i < 16; i++) {
        s->block_offset[16 + i]      =
        s->block_offset[32 + i]      = (4 * ((scan8[i] - scan8[0]) & 7)) + 4 * s->cur_pic->f->linesize[1] * ((scan8[i] - scan8[0]) >> 3);
        s->block_offset[48 + 16 + i] =
        s->block_offset[48 + 32 + i] = (4 * ((scan8[i] - scan8[0]) & 7)) + 8 * s->cur_pic->f->linesize[1] * ((scan8[i] - scan8[0]) >> 3);
    }

    if (s->pict_type != AV_PICTURE_TYPE_I) {
        if (!s->last_pic->f->data[0]) {
            av_log(avctx, AV_LOG_ERROR, "Missing reference frame.\n");
            av_frame_unref(s->last_pic->f);
            ret = get_buffer(avctx, s->last_pic);
            if (ret < 0)
                return ret;
            memset(s->last_pic->f->data[0], 0, avctx->height * s->last_pic->f->linesize[0]);
            memset(s->last_pic->f->data[1], 0x80, (avctx->height / 2) *
                   s->last_pic->f->linesize[1]);
            memset(s->last_pic->f->data[2], 0x80, (avctx->height / 2) *
                   s->last_pic->f->linesize[2]);
        }

        if (s->pict_type == AV_PICTURE_TYPE_B && !s->next_pic->f->data[0]) {
            av_log(avctx, AV_LOG_ERROR, "Missing reference frame.\n");
            av_frame_unref(s->next_pic->f);
            ret = get_buffer(avctx, s->next_pic);
            if (ret < 0)
                return ret;
            memset(s->next_pic->f->data[0], 0, avctx->height * s->next_pic->f->linesize[0]);
            memset(s->next_pic->f->data[1], 0x80, (avctx->height / 2) *
                   s->next_pic->f->linesize[1]);
            memset(s->next_pic->f->data[2], 0x80, (avctx->height / 2) *
                   s->next_pic->f->linesize[2]);
        }
    }

    if (avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(s->avctx, AV_LOG_DEBUG,
               "%c hpel:%d, tpel:%d aqp:%d qp:%d, slice_num:%02X\n",
               av_get_picture_type_char(s->pict_type),
               s->halfpel_flag, s->thirdpel_flag,
               s->adaptive_quant, s->qscale, s->slice_num);

    if (avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type == AV_PICTURE_TYPE_B ||
        avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type != AV_PICTURE_TYPE_I ||
        avctx->skip_frame >= AVDISCARD_ALL)
        return 0;

    if (s->next_p_frame_damaged) {
        if (s->pict_type == AV_PICTURE_TYPE_B)
            return 0;
        else
            s->next_p_frame_damaged = 0;
    }

    if (s->pict_type == AV_PICTURE_TYPE_B) {
        s->frame_num_offset = s->slice_num - s->prev_frame_num;

        if (s->frame_num_offset < 0)
            s->frame_num_offset += 256;
        if (s->frame_num_offset == 0 ||
            s->frame_num_offset >= s->prev_frame_num_offset) {
            av_log(s->avctx, AV_LOG_ERROR, "error in B-frame picture id\n");
            return -1;
        }
    } else {
        s->prev_frame_num        = s->frame_num;
        s->frame_num             = s->slice_num;
        s->prev_frame_num_offset = s->frame_num - s->prev_frame_num;

        if (s->prev_frame_num_offset < 0)
            s->prev_frame_num_offset += 256;
    }

    for (m = 0; m < 2; m++) {
        int i;
        for (i = 0; i < 4; i++) {
            int j;
            for (j = -1; j < 4; j++)
                s->ref_cache[m][scan8[0] + 8 * i + j] = 1;
            if (i < 3)
                s->ref_cache[m][scan8[0] + 8 * i + j] = PART_NOT_AVAILABLE;
        }
    }

    for (s->mb_y = 0; s->mb_y < s->mb_height; s->mb_y++) {
        for (s->mb_x = 0; s->mb_x < s->mb_width; s->mb_x++) {
            unsigned mb_type;
            s->mb_xy = s->mb_x + s->mb_y * s->mb_stride;

            if ((get_bits_left(&s->gb_slice)) <= 7) {
                if (((get_bits_count(&s->gb_slice) & 7) == 0 ||
                    show_bits(&s->gb_slice, get_bits_left(&s->gb_slice) & 7) == 0)) {

                    if (svq3_decode_slice_header(avctx))
                        return -1;
                }
                if (s->slice_type != s->pict_type) {
                    avpriv_request_sample(avctx, "non constant slice type");
                }
                /* TODO: support s->mb_skip_run */
            }

            mb_type = get_interleaved_ue_golomb(&s->gb_slice);

            if (s->pict_type == AV_PICTURE_TYPE_I)
                mb_type += 8;
            else if (s->pict_type == AV_PICTURE_TYPE_B && mb_type >= 4)
                mb_type += 4;
            if (mb_type > 33 || svq3_decode_mb(s, mb_type)) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
                return -1;
            }

            if (mb_type != 0 || s->cbp)
                hl_decode_mb(s);

            if (s->pict_type != AV_PICTURE_TYPE_B && !s->low_delay)
                s->cur_pic->mb_type[s->mb_x + s->mb_y * s->mb_stride] =
                    (s->pict_type == AV_PICTURE_TYPE_P && mb_type < 8) ? (mb_type - 1) : -1;
        }

        ff_draw_horiz_band(avctx, s->cur_pic->f,
                           s->last_pic->f->data[0] ? s->last_pic->f : NULL,
                           16 * s->mb_y, 16, PICT_FRAME, 0,
                           s->low_delay);
    }

    left = buf_size*8 - get_bits_count(&s->gb_slice);

    if (s->mb_y != s->mb_height || s->mb_x != s->mb_width) {
        av_log(avctx, AV_LOG_INFO, "frame num %d incomplete pic x %d y %d left %d\n", avctx->frame_number, s->mb_y, s->mb_x, left);
        //av_hex_dump(stderr, buf+buf_size-8, 8);
    }

    if (left < 0) {
        av_log(avctx, AV_LOG_ERROR, "frame num %d left %d\n", avctx->frame_number, left);
        return -1;
    }

    if (s->pict_type == AV_PICTURE_TYPE_B || s->low_delay)
        ret = av_frame_ref(data, s->cur_pic->f);
    else if (s->last_pic->f->data[0])
        ret = av_frame_ref(data, s->last_pic->f);
    if (ret < 0)
        return ret;

    /* Do not output the last pic after seeking. */
    if (s->last_pic->f->data[0] || s->low_delay)
        *got_frame = 1;

    if (s->pict_type != AV_PICTURE_TYPE_B) {
        FFSWAP(SVQ3Frame*, s->cur_pic, s->next_pic);
    } else {
        av_frame_unref(s->cur_pic->f);
    }

    return buf_size;
}

static av_cold int svq3_decode_end(AVCodecContext *avctx)
{
    SVQ3Context *s = avctx->priv_data;

    free_picture(avctx, s->cur_pic);
    free_picture(avctx, s->next_pic);
    free_picture(avctx, s->last_pic);
    av_frame_free(&s->cur_pic->f);
    av_frame_free(&s->next_pic->f);
    av_frame_free(&s->last_pic->f);
    av_freep(&s->cur_pic);
    av_freep(&s->next_pic);
    av_freep(&s->last_pic);
    av_freep(&s->slice_buf);
    av_freep(&s->intra4x4_pred_mode);
    av_freep(&s->edge_emu_buffer);
    av_freep(&s->mb2br_xy);


    av_freep(&s->buf);
    s->buf_size = 0;

    return 0;
}

AVCodec ff_svq3_decoder = {
    .name           = "svq3",
    .long_name      = NULL_IF_CONFIG_SMALL("Sorenson Vector Quantizer 3 / Sorenson Video 3 / SVQ3"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SVQ3,
    .priv_data_size = sizeof(SVQ3Context),
    .init           = svq3_decode_init,
    .close          = svq3_decode_end,
    .decode         = svq3_decode_frame,
    .capabilities   = AV_CODEC_CAP_DRAW_HORIZ_BAND |
                      AV_CODEC_CAP_DR1             |
                      AV_CODEC_CAP_DELAY,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUVJ420P,
                                                     AV_PIX_FMT_NONE},
};
