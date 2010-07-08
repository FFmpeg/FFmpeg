/**
 * VP8 compatible video decoder
 *
 * Copyright (C) 2010 David Conrad
 * Copyright (C) 2010 Ronald S. Bultje
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
#include "vp56.h"
#include "vp8data.h"
#include "vp8dsp.h"
#include "h264pred.h"
#include "rectangle.h"

typedef struct {
    uint8_t segment;
    uint8_t skip;
    // todo: make it possible to check for at least (i4x4 or split_mv)
    // in one op. are others needed?
    uint8_t mode;
    uint8_t ref_frame;
    uint8_t partitioning;
    VP56mv mv;
    VP56mv bmv[16];
} VP8Macroblock;

typedef struct {
    AVCodecContext *avctx;
    DSPContext dsp;
    VP8DSPContext vp8dsp;
    H264PredContext hpc;
    vp8_mc_func put_pixels_tab[3][3][3];
    AVFrame frames[4];
    AVFrame *framep[4];
    uint8_t *edge_emu_buffer;
    VP56RangeCoder c;   ///< header context, includes mb modes and motion vectors
    int profile;

    int mb_width;   /* number of horizontal MB */
    int mb_height;  /* number of vertical MB */
    int linesize;
    int uvlinesize;

    int keyframe;
    int invisible;
    int update_last;    ///< update VP56_FRAME_PREVIOUS with the current one
    int update_golden;  ///< VP56_FRAME_NONE if not updated, or which frame to copy if so
    int update_altref;

    /**
     * If this flag is not set, all the probability updates
     * are discarded after this frame is decoded.
     */
    int update_probabilities;

    /**
     * All coefficients are contained in separate arith coding contexts.
     * There can be 1, 2, 4, or 8 of these after the header context.
     */
    int num_coeff_partitions;
    VP56RangeCoder coeff_partition[8];

    VP8Macroblock *macroblocks;
    VP8Macroblock *macroblocks_base;
    int mb_stride;

    uint8_t *intra4x4_pred_mode;
    uint8_t *intra4x4_pred_mode_base;
    int b4_stride;

    /**
     * For coeff decode, we need to know whether the above block had non-zero
     * coefficients. This means for each macroblock, we need data for 4 luma
     * blocks, 2 u blocks, 2 v blocks, and the luma dc block, for a total of 9
     * per macroblock. We keep the last row in top_nnz.
     */
    uint8_t (*top_nnz)[9];
    DECLARE_ALIGNED(8, uint8_t, left_nnz)[9];

    /**
     * This is the index plus one of the last non-zero coeff
     * for each of the blocks in the current macroblock.
     * So, 0 -> no coeffs
     *     1 -> dc-only (special transform)
     *     2+-> full transform
     */
    DECLARE_ALIGNED(16, uint8_t, non_zero_count_cache)[6][4];
    DECLARE_ALIGNED(16, DCTELEM, block)[6][4][16];

    int chroma_pred_mode;    ///< 8x8c pred mode of the current macroblock

    int mbskip_enabled;
    int sign_bias[4]; ///< one state [0, 1] per ref frame type

    /**
     * Base parameters for segmentation, i.e. per-macroblock parameters.
     * These must be kept unchanged even if segmentation is not used for
     * a frame, since the values persist between interframes.
     */
    struct {
        int enabled;
        int absolute_vals;
        int update_map;
        int8_t base_quant[4];
        int8_t filter_level[4];     ///< base loop filter level
    } segmentation;

    /**
     * Macroblocks can have one of 4 different quants in a frame when
     * segmentation is enabled.
     * If segmentation is disabled, only the first segment's values are used.
     */
    struct {
        // [0] - DC qmul  [1] - AC qmul
        int16_t luma_qmul[2];
        int16_t luma_dc_qmul[2];    ///< luma dc-only block quant
        int16_t chroma_qmul[2];
    } qmat[4];

    struct {
        int simple;
        int level;
        int sharpness;
    } filter;

    struct {
        int enabled;    ///< whether each mb can have a different strength based on mode/ref

        /**
         * filter strength adjustment for the following macroblock modes:
         * [0] - i4x4
         * [1] - zero mv
         * [2] - inter modes except for zero or split mv
         * [3] - split mv
         *  i16x16 modes never have any adjustment
         */
        int8_t mode[4];

        /**
         * filter strength adjustment for macroblocks that reference:
         * [0] - intra / VP56_FRAME_CURRENT
         * [1] - VP56_FRAME_PREVIOUS
         * [2] - VP56_FRAME_GOLDEN
         * [3] - altref / VP56_FRAME_GOLDEN2
         */
        int8_t ref[4];
    } lf_delta;

    /**
     * These are all of the updatable probabilities for binary decisions.
     * They are only implictly reset on keyframes, making it quite likely
     * for an interframe to desync if a prior frame's header was corrupt
     * or missing outright!
     */
    struct {
        uint8_t segmentid[3];
        uint8_t mbskip;
        uint8_t intra;
        uint8_t last;
        uint8_t golden;
        uint8_t pred16x16[4];
        uint8_t pred8x8c[3];
        uint8_t token[4][8][3][NUM_DCT_TOKENS-1];
        uint8_t mvc[2][19];
    } prob[2];
} VP8Context;

#define RL24(p) (AV_RL16(p) + ((p)[2] << 16))

static void vp8_decode_flush(AVCodecContext *avctx)
{
    VP8Context *s = avctx->priv_data;
    int i;

    for (i = 0; i < 4; i++)
        if (s->frames[i].data[0])
            avctx->release_buffer(avctx, &s->frames[i]);
    memset(s->framep, 0, sizeof(s->framep));

    av_freep(&s->macroblocks_base);
    av_freep(&s->intra4x4_pred_mode_base);
    av_freep(&s->top_nnz);
    av_freep(&s->edge_emu_buffer);

    s->macroblocks        = NULL;
    s->intra4x4_pred_mode = NULL;
}

static int update_dimensions(VP8Context *s, int width, int height)
{
    int i;

    if (avcodec_check_dimensions(s->avctx, width, height))
        return AVERROR_INVALIDDATA;

    vp8_decode_flush(s->avctx);

    avcodec_set_dimensions(s->avctx, width, height);

    s->mb_width  = (s->avctx->coded_width +15) / 16;
    s->mb_height = (s->avctx->coded_height+15) / 16;

    // we allocate a border around the top/left of intra4x4 modes
    // this is 4 blocks for intra4x4 to keep 4-byte alignment for fill_rectangle
    s->mb_stride = s->mb_width+1;
    s->b4_stride = 4*s->mb_stride;

    s->macroblocks_base        = av_mallocz(s->mb_stride*(s->mb_height+1)*sizeof(*s->macroblocks));
    s->intra4x4_pred_mode_base = av_mallocz(s->b4_stride*(4*s->mb_height+1));
    s->top_nnz                 = av_mallocz(s->mb_width*sizeof(*s->top_nnz));

    s->macroblocks        = s->macroblocks_base        + 1 + s->mb_stride;
    s->intra4x4_pred_mode = s->intra4x4_pred_mode_base + 4 + s->b4_stride;

    memset(s->intra4x4_pred_mode_base, DC_PRED, s->b4_stride);
    for (i = 0; i < 4*s->mb_height; i++)
        s->intra4x4_pred_mode[i*s->b4_stride-1] = DC_PRED;

    return 0;
}

static void parse_segment_info(VP8Context *s)
{
    VP56RangeCoder *c = &s->c;
    int i;

    s->segmentation.update_map = vp8_rac_get(c);

    if (vp8_rac_get(c)) { // update segment feature data
        s->segmentation.absolute_vals = vp8_rac_get(c);

        for (i = 0; i < 4; i++)
            s->segmentation.base_quant[i]   = vp8_rac_get_sint(c, 7);

        for (i = 0; i < 4; i++)
            s->segmentation.filter_level[i] = vp8_rac_get_sint(c, 6);
    }
    if (s->segmentation.update_map)
        for (i = 0; i < 3; i++)
            s->prob->segmentid[i] = vp8_rac_get(c) ? vp8_rac_get_uint(c, 8) : 255;
}

static void update_lf_deltas(VP8Context *s)
{
    VP56RangeCoder *c = &s->c;
    int i;

    for (i = 0; i < 4; i++)
        s->lf_delta.ref[i]  = vp8_rac_get_sint(c, 6);

    for (i = 0; i < 4; i++)
        s->lf_delta.mode[i] = vp8_rac_get_sint(c, 6);
}

static int setup_partitions(VP8Context *s, const uint8_t *buf, int buf_size)
{
    const uint8_t *sizes = buf;
    int i;

    s->num_coeff_partitions = 1 << vp8_rac_get_uint(&s->c, 2);

    buf      += 3*(s->num_coeff_partitions-1);
    buf_size -= 3*(s->num_coeff_partitions-1);
    if (buf_size < 0)
        return -1;

    for (i = 0; i < s->num_coeff_partitions-1; i++) {
        int size = RL24(sizes + 3*i);
        if (buf_size - size < 0)
            return -1;

        vp56_init_range_decoder(&s->coeff_partition[i], buf, size);
        buf      += size;
        buf_size -= size;
    }
    vp56_init_range_decoder(&s->coeff_partition[i], buf, buf_size);

    return 0;
}

static void get_quants(VP8Context *s)
{
    VP56RangeCoder *c = &s->c;
    int i, base_qi;

    int yac_qi     = vp8_rac_get_uint(c, 7);
    int ydc_delta  = vp8_rac_get_sint(c, 4);
    int y2dc_delta = vp8_rac_get_sint(c, 4);
    int y2ac_delta = vp8_rac_get_sint(c, 4);
    int uvdc_delta = vp8_rac_get_sint(c, 4);
    int uvac_delta = vp8_rac_get_sint(c, 4);

    for (i = 0; i < 4; i++) {
        if (s->segmentation.enabled) {
            base_qi = s->segmentation.base_quant[i];
            if (!s->segmentation.absolute_vals)
                base_qi += yac_qi;
        } else
            base_qi = yac_qi;

        s->qmat[i].luma_qmul[0]    =       vp8_dc_qlookup[av_clip(base_qi + ydc_delta , 0, 127)];
        s->qmat[i].luma_qmul[1]    =       vp8_ac_qlookup[av_clip(base_qi             , 0, 127)];
        s->qmat[i].luma_dc_qmul[0] =   2 * vp8_dc_qlookup[av_clip(base_qi + y2dc_delta, 0, 127)];
        s->qmat[i].luma_dc_qmul[1] = 155 * vp8_ac_qlookup[av_clip(base_qi + y2ac_delta, 0, 127)] / 100;
        s->qmat[i].chroma_qmul[0]  =       vp8_dc_qlookup[av_clip(base_qi + uvdc_delta, 0, 127)];
        s->qmat[i].chroma_qmul[1]  =       vp8_ac_qlookup[av_clip(base_qi + uvac_delta, 0, 127)];

        s->qmat[i].luma_dc_qmul[1] = FFMAX(s->qmat[i].luma_dc_qmul[1], 8);
        s->qmat[i].chroma_qmul[0]  = FFMIN(s->qmat[i].chroma_qmul[0], 132);
    }
}

/**
 * Determine which buffers golden and altref should be updated with after this frame.
 * The spec isn't clear here, so I'm going by my understanding of what libvpx does
 *
 * Intra frames update all 3 references
 * Inter frames update VP56_FRAME_PREVIOUS if the update_last flag is set
 * If the update (golden|altref) flag is set, it's updated with the current frame
 *      if update_last is set, and VP56_FRAME_PREVIOUS otherwise.
 * If the flag is not set, the number read means:
 *      0: no update
 *      1: VP56_FRAME_PREVIOUS
 *      2: update golden with altref, or update altref with golden
 */
static VP56Frame ref_to_update(VP8Context *s, int update, VP56Frame ref)
{
    VP56RangeCoder *c = &s->c;

    if (update)
        return VP56_FRAME_CURRENT;

    switch (vp8_rac_get_uint(c, 2)) {
    case 1:
        return VP56_FRAME_PREVIOUS;
    case 2:
        return (ref == VP56_FRAME_GOLDEN) ? VP56_FRAME_GOLDEN2 : VP56_FRAME_GOLDEN;
    }
    return VP56_FRAME_NONE;
}

static void update_refs(VP8Context *s)
{
    VP56RangeCoder *c = &s->c;

    int update_golden = vp8_rac_get(c);
    int update_altref = vp8_rac_get(c);

    s->update_golden = ref_to_update(s, update_golden, VP56_FRAME_GOLDEN);
    s->update_altref = ref_to_update(s, update_altref, VP56_FRAME_GOLDEN2);
}

static int decode_frame_header(VP8Context *s, const uint8_t *buf, int buf_size)
{
    VP56RangeCoder *c = &s->c;
    int header_size, hscale, vscale, i, j, k, l, ret;
    int width  = s->avctx->width;
    int height = s->avctx->height;

    s->keyframe  = !(buf[0] & 1);
    s->profile   =  (buf[0]>>1) & 7;
    s->invisible = !(buf[0] & 0x10);
    header_size  = RL24(buf) >> 5;
    buf      += 3;
    buf_size -= 3;

    if (s->profile > 3)
        av_log(s->avctx, AV_LOG_WARNING, "Unknown profile %d\n", s->profile);

    if (!s->profile)
        memcpy(s->put_pixels_tab, s->vp8dsp.put_vp8_epel_pixels_tab, sizeof(s->put_pixels_tab));
    else    // profile 1-3 use bilinear, 4+ aren't defined so whatever
        memcpy(s->put_pixels_tab, s->vp8dsp.put_vp8_bilinear_pixels_tab, sizeof(s->put_pixels_tab));

    if (header_size > buf_size - 7*s->keyframe) {
        av_log(s->avctx, AV_LOG_ERROR, "Header size larger than data provided\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->keyframe) {
        if (RL24(buf) != 0x2a019d) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid start code 0x%x\n", RL24(buf));
            return AVERROR_INVALIDDATA;
        }
        width  = AV_RL16(buf+3) & 0x3fff;
        height = AV_RL16(buf+5) & 0x3fff;
        hscale = buf[4] >> 6;
        vscale = buf[6] >> 6;
        buf      += 7;
        buf_size -= 7;

        if (hscale || vscale)
            av_log_missing_feature(s->avctx, "Upscaling", 1);

        s->update_golden = s->update_altref = VP56_FRAME_CURRENT;
        memcpy(s->prob->token    , vp8_token_default_probs , sizeof(s->prob->token));
        memcpy(s->prob->pred16x16, vp8_pred16x16_prob_inter, sizeof(s->prob->pred16x16));
        memcpy(s->prob->pred8x8c , vp8_pred8x8c_prob_inter , sizeof(s->prob->pred8x8c));
        memcpy(s->prob->mvc      , vp8_mv_default_prob     , sizeof(s->prob->mvc));
        memset(&s->segmentation, 0, sizeof(s->segmentation));
    }

    if (!s->macroblocks_base || /* first frame */
        width != s->avctx->width || height != s->avctx->height) {
        if ((ret = update_dimensions(s, width, height) < 0))
            return ret;
    }

    vp56_init_range_decoder(c, buf, header_size);
    buf      += header_size;
    buf_size -= header_size;

    if (s->keyframe) {
        if (vp8_rac_get(c))
            av_log(s->avctx, AV_LOG_WARNING, "Unspecified colorspace\n");
        vp8_rac_get(c); // whether we can skip clamping in dsp functions
    }

    if ((s->segmentation.enabled = vp8_rac_get(c)))
        parse_segment_info(s);
    else
        s->segmentation.update_map = 0; // FIXME: move this to some init function?

    s->filter.simple    = vp8_rac_get(c);
    s->filter.level     = vp8_rac_get_uint(c, 6);
    s->filter.sharpness = vp8_rac_get_uint(c, 3);

    if ((s->lf_delta.enabled = vp8_rac_get(c)))
        if (vp8_rac_get(c))
            update_lf_deltas(s);

    if (setup_partitions(s, buf, buf_size)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid partitions\n");
        return AVERROR_INVALIDDATA;
    }

    get_quants(s);

    if (!s->keyframe) {
        update_refs(s);
        s->sign_bias[VP56_FRAME_GOLDEN]               = vp8_rac_get(c);
        s->sign_bias[VP56_FRAME_GOLDEN2 /* altref */] = vp8_rac_get(c);
    }

    // if we aren't saving this frame's probabilities for future frames,
    // make a copy of the current probabilities
    if (!(s->update_probabilities = vp8_rac_get(c)))
        s->prob[1] = s->prob[0];

    s->update_last = s->keyframe || vp8_rac_get(c);

    for (i = 0; i < 4; i++)
        for (j = 0; j < 8; j++)
            for (k = 0; k < 3; k++)
                for (l = 0; l < NUM_DCT_TOKENS-1; l++)
                    if (vp56_rac_get_prob(c, vp8_token_update_probs[i][j][k][l]))
                        s->prob->token[i][j][k][l] = vp8_rac_get_uint(c, 8);

    if ((s->mbskip_enabled = vp8_rac_get(c)))
        s->prob->mbskip = vp8_rac_get_uint(c, 8);

    if (!s->keyframe) {
        s->prob->intra  = vp8_rac_get_uint(c, 8);
        s->prob->last   = vp8_rac_get_uint(c, 8);
        s->prob->golden = vp8_rac_get_uint(c, 8);

        if (vp8_rac_get(c))
            for (i = 0; i < 4; i++)
                s->prob->pred16x16[i] = vp8_rac_get_uint(c, 8);
        if (vp8_rac_get(c))
            for (i = 0; i < 3; i++)
                s->prob->pred8x8c[i]  = vp8_rac_get_uint(c, 8);

        // 17.2 MV probability update
        for (i = 0; i < 2; i++)
            for (j = 0; j < 19; j++)
                if (vp56_rac_get_prob(c, vp8_mv_update_prob[i][j]))
                    s->prob->mvc[i][j] = vp8_rac_get_nn(c);
    }

    return 0;
}

static inline void clamp_mv(VP8Context *s, VP56mv *dst, const VP56mv *src,
                            int mb_x, int mb_y)
{
#define MARGIN (16 << 2)
    dst->x = av_clip(src->x, -((mb_x << 6) + MARGIN),
                     ((s->mb_width  - 1 - mb_x) << 6) + MARGIN);
    dst->y = av_clip(src->y, -((mb_y << 6) + MARGIN),
                     ((s->mb_height - 1 - mb_y) << 6) + MARGIN);
}

static void find_near_mvs(VP8Context *s, VP8Macroblock *mb, int mb_x, int mb_y,
                          VP56mv near[2], VP56mv *best, int cnt[4])
{
    VP8Macroblock *mb_edge[3] = { mb - s->mb_stride     /* top */,
                                  mb - 1                /* left */,
                                  mb - s->mb_stride - 1 /* top-left */ };
    enum { EDGE_TOP, EDGE_LEFT, EDGE_TOPLEFT };
    VP56mv near_mv[4]  = {{ 0 }};
    enum { CNT_ZERO, CNT_NEAREST, CNT_NEAR, CNT_SPLITMV };
    int idx = CNT_ZERO, n;
    int best_idx = CNT_ZERO;

    /* Process MB on top, left and top-left */
    for (n = 0; n < 3; n++) {
        VP8Macroblock *edge = mb_edge[n];
        if (edge->ref_frame != VP56_FRAME_CURRENT) {
            if (edge->mv.x | edge->mv.y) {
                VP56mv tmp = edge->mv;
                if (s->sign_bias[mb->ref_frame] != s->sign_bias[edge->ref_frame]) {
                    tmp.x *= -1;
                    tmp.y *= -1;
                }
                if ((tmp.x ^ near_mv[idx].x) | (tmp.y ^ near_mv[idx].y))
                    near_mv[++idx] = tmp;
                cnt[idx]       += 1 + (n != 2);
            } else
                cnt[CNT_ZERO] += 1 + (n != 2);
        }
    }

    /* If we have three distinct MV's, merge first and last if they're the same */
    if (cnt[CNT_SPLITMV] &&
        !((near_mv[1+EDGE_TOP].x ^ near_mv[1+EDGE_TOPLEFT].x) |
          (near_mv[1+EDGE_TOP].y ^ near_mv[1+EDGE_TOPLEFT].y)))
        cnt[CNT_NEAREST] += 1;

    cnt[CNT_SPLITMV] = ((mb_edge[EDGE_LEFT]->mode   == VP8_MVMODE_SPLIT) +
                        (mb_edge[EDGE_TOP]->mode    == VP8_MVMODE_SPLIT)) * 2 +
                       (mb_edge[EDGE_TOPLEFT]->mode == VP8_MVMODE_SPLIT);

    /* Swap near and nearest if necessary */
    if (cnt[CNT_NEAR] > cnt[CNT_NEAREST]) {
        FFSWAP(int,    cnt[CNT_NEAREST],     cnt[CNT_NEAR]);
        FFSWAP(VP56mv, near_mv[CNT_NEAREST], near_mv[CNT_NEAR]);
    }

    /* Choose the best mv out of 0,0 and the nearest mv */
    if (cnt[CNT_NEAREST] >= cnt[CNT_ZERO])
        best_idx = CNT_NEAREST;

    clamp_mv(s, best, &near_mv[best_idx], mb_x, mb_y);
    near[0] = near_mv[CNT_NEAREST];
    near[1] = near_mv[CNT_NEAR];
}

/**
 * Motion vector coding, 17.1.
 */
static int read_mv_component(VP56RangeCoder *c, const uint8_t *p)
{
    int x = 0;

    if (vp56_rac_get_prob(c, p[0])) {
        int i;

        for (i = 0; i < 3; i++)
            x += vp56_rac_get_prob(c, p[9 + i]) << i;
        for (i = 9; i > 3; i--)
            x += vp56_rac_get_prob(c, p[9 + i]) << i;
        if (!(x & 0xFFF0) || vp56_rac_get_prob(c, p[12]))
            x += 8;
    } else
        x = vp8_rac_get_tree(c, vp8_small_mvtree, &p[2]);

    return (x && vp56_rac_get_prob(c, p[1])) ? -x : x;
}

static const uint8_t *get_submv_prob(const VP56mv *left, const VP56mv *top)
{
    int l_is_zero = !(left->x | left->y);
    int t_is_zero = !(top->x  | top->y);
    int equal = !((left->x ^ top->x) | (left->y ^ top->y));

    if (equal)
        return l_is_zero ? vp8_submv_prob[4] : vp8_submv_prob[3];
    if (t_is_zero)
        return vp8_submv_prob[2];
    return l_is_zero ? vp8_submv_prob[1] : vp8_submv_prob[0];
}

/**
 * Split motion vector prediction, 16.4.
 * @returns the number of motion vectors parsed (2, 4 or 16)
 */
static int decode_splitmvs(VP8Context    *s,  VP56RangeCoder *c,
                            VP8Macroblock *mb, VP56mv         *base_mv)
{
    int part_idx = mb->partitioning =
        vp8_rac_get_tree(c, vp8_mbsplit_tree, vp8_mbsplit_prob);
    int n, num = vp8_mbsplit_count[part_idx];
    const uint8_t *mbsplits = vp8_mbsplits[part_idx],
                  *firstidx = vp8_mbfirstidx[part_idx];

    for (n = 0; n < num; n++) {
        int k = firstidx[n];
        const VP56mv *left, *above;
        const uint8_t *submv_prob;

        if (!(k & 3)) {
            VP8Macroblock *left_mb = &mb[-1];
            left = &left_mb->bmv[vp8_mbsplits[left_mb->partitioning][k + 3]];
        } else
            left  = &mb->bmv[mbsplits[k - 1]];
        if (k <= 3) {
            VP8Macroblock *above_mb = &mb[-s->mb_stride];
            above = &above_mb->bmv[vp8_mbsplits[above_mb->partitioning][k + 12]];
        } else
            above = &mb->bmv[mbsplits[k - 4]];

        submv_prob = get_submv_prob(left, above);

        switch (vp8_rac_get_tree(c, vp8_submv_ref_tree, submv_prob)) {
        case VP8_SUBMVMODE_NEW4X4:
            mb->bmv[n].y = base_mv->y + read_mv_component(c, s->prob->mvc[0]);
            mb->bmv[n].x = base_mv->x + read_mv_component(c, s->prob->mvc[1]);
            break;
        case VP8_SUBMVMODE_ZERO4X4:
            mb->bmv[n].x = 0;
            mb->bmv[n].y = 0;
            break;
        case VP8_SUBMVMODE_LEFT4X4:
            mb->bmv[n] = *left;
            break;
        case VP8_SUBMVMODE_TOP4X4:
            mb->bmv[n] = *above;
            break;
        }
    }

    return num;
}

static inline void decode_intra4x4_modes(VP56RangeCoder *c, uint8_t *intra4x4,
                                         int stride, int keyframe)
{
    int x, y, t, l;
    const uint8_t *ctx = vp8_pred4x4_prob_inter;

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            if (keyframe) {
                t = intra4x4[x - stride];
                l = intra4x4[x - 1];
                ctx = vp8_pred4x4_prob_intra[t][l];
            }
            intra4x4[x] = vp8_rac_get_tree(c, vp8_pred4x4_tree, ctx);
        }
        intra4x4 += stride;
    }
}

static void decode_mb_mode(VP8Context *s, VP8Macroblock *mb, int mb_x, int mb_y,
                           uint8_t *intra4x4)
{
    VP56RangeCoder *c = &s->c;
    int n;

    if (s->segmentation.update_map)
        mb->segment = vp8_rac_get_tree(c, vp8_segmentid_tree, s->prob->segmentid);

    mb->skip = s->mbskip_enabled ? vp56_rac_get_prob(c, s->prob->mbskip) : 0;

    if (s->keyframe) {
        mb->mode = vp8_rac_get_tree(c, vp8_pred16x16_tree_intra, vp8_pred16x16_prob_intra);

        if (mb->mode == MODE_I4x4) {
            decode_intra4x4_modes(c, intra4x4, s->b4_stride, 1);
        } else
            fill_rectangle(intra4x4, 4, 4, s->b4_stride, vp8_pred4x4_mode[mb->mode], 1);

        s->chroma_pred_mode = vp8_rac_get_tree(c, vp8_pred8x8c_tree, vp8_pred8x8c_prob_intra);
        mb->ref_frame = VP56_FRAME_CURRENT;
    } else if (vp56_rac_get_prob(c, s->prob->intra)) {
        VP56mv near[2], best;
        int cnt[4] = { 0 };
        uint8_t p[4];

        // inter MB, 16.2
        if (vp56_rac_get_prob(c, s->prob->last))
            mb->ref_frame = vp56_rac_get_prob(c, s->prob->golden) ?
                VP56_FRAME_GOLDEN2 /* altref */ : VP56_FRAME_GOLDEN;
        else
            mb->ref_frame = VP56_FRAME_PREVIOUS;

        // motion vectors, 16.3
        find_near_mvs(s, mb, mb_x, mb_y, near, &best, cnt);
        for (n = 0; n < 4; n++)
            p[n] = vp8_mode_contexts[cnt[n]][n];
        mb->mode = vp8_rac_get_tree(c, vp8_pred16x16_tree_mvinter, p);
        switch (mb->mode) {
        case VP8_MVMODE_SPLIT:
            mb->mv = mb->bmv[decode_splitmvs(s, c, mb, &best) - 1];
            break;
        case VP8_MVMODE_ZERO:
            mb->mv.x = 0;
            mb->mv.y = 0;
            break;
        case VP8_MVMODE_NEAREST:
            clamp_mv(s, &mb->mv, &near[0], mb_x, mb_y);
            break;
        case VP8_MVMODE_NEAR:
            clamp_mv(s, &mb->mv, &near[1], mb_x, mb_y);
            break;
        case VP8_MVMODE_NEW:
            mb->mv.y = best.y + read_mv_component(c, s->prob->mvc[0]);
            mb->mv.x = best.x + read_mv_component(c, s->prob->mvc[1]);
            break;
        }
        if (mb->mode != VP8_MVMODE_SPLIT) {
            mb->partitioning = VP8_SPLITMVMODE_NONE;
            mb->bmv[0] = mb->mv;
        }
    } else {
        // intra MB, 16.1
        mb->mode = vp8_rac_get_tree(c, vp8_pred16x16_tree_inter, s->prob->pred16x16);

        if (mb->mode == MODE_I4x4) {
            decode_intra4x4_modes(c, intra4x4, s->b4_stride, 0);
        } else
            fill_rectangle(intra4x4, 4, 4, s->b4_stride, vp8_pred4x4_mode[mb->mode], 1);

        s->chroma_pred_mode = vp8_rac_get_tree(c, vp8_pred8x8c_tree, s->prob->pred8x8c);
        mb->ref_frame = VP56_FRAME_CURRENT;
    }
}

/**
 * @param c arithmetic bitstream reader context
 * @param block destination for block coefficients
 * @param probs probabilities to use when reading trees from the bitstream
 * @param i initial coeff index, 0 unless a separate DC block is coded
 * @param zero_nhood the initial prediction context for number of surrounding
 *                   all-zero blocks (only left/top, so 0-2)
 * @param qmul array holding the dc/ac dequant factor at position 0/1
 * @return 0 if no coeffs were decoded
 *         otherwise, the index of the last coeff decoded plus one
 */
static int decode_block_coeffs(VP56RangeCoder *c, DCTELEM block[16],
                               uint8_t probs[8][3][NUM_DCT_TOKENS-1],
                               int i, int zero_nhood, int16_t qmul[2])
{
    int token, nonzero = 0;
    int offset = 0;

    for (; i < 16; i++) {
        token = vp8_rac_get_tree_with_offset(c, vp8_coeff_tree, probs[vp8_coeff_band[i]][zero_nhood], offset);

        if (token == DCT_EOB)
            break;
        else if (token >= DCT_CAT1) {
            int cat = token-DCT_CAT1;
            token = vp8_rac_get_coeff(c, vp8_dct_cat_prob[cat]);
            token += vp8_dct_cat_offset[cat];
        }

        // after the first token, the non-zero prediction context becomes
        // based on the last decoded coeff
        if (!token) {
            zero_nhood = 0;
            offset = 1;
            continue;
        } else if (token == 1)
            zero_nhood = 1;
        else
            zero_nhood = 2;

        // todo: full [16] qmat? load into register?
        block[zigzag_scan[i]] = (vp8_rac_get(c) ? -token : token) * qmul[!!i];
        nonzero = i+1;
        offset = 0;
    }
    return nonzero;
}

static void decode_mb_coeffs(VP8Context *s, VP56RangeCoder *c, VP8Macroblock *mb,
                             uint8_t t_nnz[9], uint8_t l_nnz[9])
{
    LOCAL_ALIGNED_16(DCTELEM, dc,[16]);
    int i, x, y, luma_start = 0, luma_ctx = 3;
    int nnz_pred, nnz, nnz_total = 0;
    int segment = s->segmentation.enabled ? mb->segment : 0;

    s->dsp.clear_blocks((DCTELEM *)s->block);

    if (mb->mode != MODE_I4x4 && mb->mode != VP8_MVMODE_SPLIT) {
        AV_ZERO128(dc);
        AV_ZERO128(dc+8);
        nnz_pred = t_nnz[8] + l_nnz[8];

        // decode DC values and do hadamard
        nnz = decode_block_coeffs(c, dc, s->prob->token[1], 0, nnz_pred,
                                  s->qmat[segment].luma_dc_qmul);
        l_nnz[8] = t_nnz[8] = !!nnz;
        nnz_total += nnz;
        s->vp8dsp.vp8_luma_dc_wht(s->block, dc);
        luma_start = 1;
        luma_ctx = 0;
    }

    // luma blocks
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++) {
            nnz_pred = l_nnz[y] + t_nnz[x];
            nnz = decode_block_coeffs(c, s->block[y][x], s->prob->token[luma_ctx], luma_start,
                                      nnz_pred, s->qmat[segment].luma_qmul);
            // nnz+luma_start may be one more than the actual last index, but we don't care
            s->non_zero_count_cache[y][x] = nnz + luma_start;
            t_nnz[x] = l_nnz[y] = !!nnz;
            nnz_total += nnz;
        }

    // chroma blocks
    // TODO: what to do about dimensions? 2nd dim for luma is x,
    // but for chroma it's (y<<1)|x
    for (i = 4; i < 6; i++)
        for (y = 0; y < 2; y++)
            for (x = 0; x < 2; x++) {
                nnz_pred = l_nnz[i+2*y] + t_nnz[i+2*x];
                nnz = decode_block_coeffs(c, s->block[i][(y<<1)+x], s->prob->token[2], 0,
                                          nnz_pred, s->qmat[segment].chroma_qmul);
                s->non_zero_count_cache[i][(y<<1)+x] = nnz;
                t_nnz[i+2*x] = l_nnz[i+2*y] = !!nnz;
                nnz_total += nnz;
            }

    // if there were no coded coeffs despite the macroblock not being marked skip,
    // we MUST not do the inner loop filter and should not do IDCT
    // Since skip isn't used for bitstream prediction, just manually set it.
    if (!nnz_total)
        mb->skip = 1;
}

static int check_intra_pred_mode(int mode, int mb_x, int mb_y)
{
    if (mode == DC_PRED8x8) {
        if (!(mb_x|mb_y))
            mode = DC_128_PRED8x8;
        else if (!mb_y)
            mode = LEFT_DC_PRED8x8;
        else if (!mb_x)
            mode = TOP_DC_PRED8x8;
    }
    return mode;
}

static void intra_predict(VP8Context *s, uint8_t *dst[3], VP8Macroblock *mb,
                          uint8_t *bmode, int mb_x, int mb_y)
{
    int x, y, mode, nnz, tr;

    if (mb->mode < MODE_I4x4) {
        mode = check_intra_pred_mode(mb->mode, mb_x, mb_y);
        s->hpc.pred16x16[mode](dst[0], s->linesize);
    } else {
        uint8_t *ptr = dst[0];

        // all blocks on the right edge of the macroblock use bottom edge
        // the top macroblock for their topright edge
        uint8_t *tr_right = ptr - s->linesize + 16;

        // if we're on the right edge of the frame, said edge is extended
        // from the top macroblock
        if (mb_x == s->mb_width-1) {
            tr = tr_right[-1]*0x01010101;
            tr_right = (uint8_t *)&tr;
        }

        for (y = 0; y < 4; y++) {
            uint8_t *topright = ptr + 4 - s->linesize;
            for (x = 0; x < 4; x++) {
                if (x == 3)
                    topright = tr_right;

                s->hpc.pred4x4[bmode[x]](ptr+4*x, topright, s->linesize);

                nnz = s->non_zero_count_cache[y][x];
                if (nnz) {
                    if (nnz == 1)
                        s->vp8dsp.vp8_idct_dc_add(ptr+4*x, s->block[y][x], s->linesize);
                    else
                        s->vp8dsp.vp8_idct_add(ptr+4*x, s->block[y][x], s->linesize);
                }
                topright += 4;
            }

            ptr   += 4*s->linesize;
            bmode += s->b4_stride;
        }
    }

    mode = check_intra_pred_mode(s->chroma_pred_mode, mb_x, mb_y);
    s->hpc.pred8x8[mode](dst[1], s->uvlinesize);
    s->hpc.pred8x8[mode](dst[2], s->uvlinesize);
}

/**
 * Generic MC function.
 *
 * @param s VP8 decoding context
 * @param luma 1 for luma (Y) planes, 0 for chroma (Cb/Cr) planes
 * @param dst target buffer for block data at block position
 * @param src reference picture buffer at origin (0, 0)
 * @param mv motion vector (relative to block position) to get pixel data from
 * @param x_off horizontal position of block from origin (0, 0)
 * @param y_off vertical position of block from origin (0, 0)
 * @param block_w width of block (16, 8 or 4)
 * @param block_h height of block (always same as block_w)
 * @param width width of src/dst plane data
 * @param height height of src/dst plane data
 * @param linesize size of a single line of plane data, including padding
 * @param mc_func motion compensation function pointers (bilinear or sixtap MC)
 */
static inline void vp8_mc(VP8Context *s, int luma,
                          uint8_t *dst, uint8_t *src, const VP56mv *mv,
                          int x_off, int y_off, int block_w, int block_h,
                          int width, int height, int linesize,
                          vp8_mc_func mc_func[3][3])
{
    static const uint8_t idx[8] = { 0, 1, 2, 1, 2, 1, 2, 1 };
    int mx = (mv->x << luma)&7, mx_idx = idx[mx];
    int my = (mv->y << luma)&7, my_idx = idx[my];

    x_off += mv->x >> (3 - luma);
    y_off += mv->y >> (3 - luma);

    // edge emulation
    src += y_off * linesize + x_off;
    if (x_off < 2 || x_off >= width  - block_w - 3 ||
        y_off < 2 || y_off >= height - block_h - 3) {
        ff_emulated_edge_mc(s->edge_emu_buffer, src - 2 * linesize - 2, linesize,
                            block_w + 5, block_h + 5,
                            x_off - 2, y_off - 2, width, height);
        src = s->edge_emu_buffer + 2 + linesize * 2;
    }

    mc_func[my_idx][mx_idx](dst, linesize, src, linesize, block_h, mx, my);
}

static inline void vp8_mc_part(VP8Context *s, uint8_t *dst[3],
                               AVFrame *ref_frame, int x_off, int y_off,
                               int bx_off, int by_off,
                               int block_w, int block_h,
                               int width, int height, VP56mv *mv)
{
    VP56mv uvmv = *mv;

    /* Y */
    vp8_mc(s, 1, dst[0] + by_off * s->linesize + bx_off,
           ref_frame->data[0], mv, x_off + bx_off, y_off + by_off,
           block_w, block_h, width, height, s->linesize,
           s->put_pixels_tab[block_w == 8]);

    /* U/V */
    if (s->profile == 3) {
        uvmv.x &= ~7;
        uvmv.y &= ~7;
    }
    x_off   >>= 1; y_off   >>= 1;
    bx_off  >>= 1; by_off  >>= 1;
    width   >>= 1; height  >>= 1;
    block_w >>= 1; block_h >>= 1;
    vp8_mc(s, 0, dst[1] + by_off * s->uvlinesize + bx_off,
           ref_frame->data[1], &uvmv, x_off + bx_off, y_off + by_off,
           block_w, block_h, width, height, s->uvlinesize,
           s->put_pixels_tab[1 + (block_w == 4)]);
    vp8_mc(s, 0, dst[2] + by_off * s->uvlinesize + bx_off,
           ref_frame->data[2], &uvmv, x_off + bx_off, y_off + by_off,
           block_w, block_h, width, height, s->uvlinesize,
           s->put_pixels_tab[1 + (block_w == 4)]);
}

/**
 * Apply motion vectors to prediction buffer, chapter 18.
 */
static void inter_predict(VP8Context *s, uint8_t *dst[3], VP8Macroblock *mb,
                          int mb_x, int mb_y)
{
    int x_off = mb_x << 4, y_off = mb_y << 4;
    int width = 16*s->mb_width, height = 16*s->mb_height;

    if (mb->mode < VP8_MVMODE_SPLIT) {
        vp8_mc_part(s, dst, s->framep[mb->ref_frame], x_off, y_off,
                    0, 0, 16, 16, width, height, &mb->mv);
    } else switch (mb->partitioning) {
    case VP8_SPLITMVMODE_4x4: {
        int x, y;
        VP56mv uvmv;

        /* Y */
        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                vp8_mc(s, 1, dst[0] + 4*y*s->linesize + x*4,
                       s->framep[mb->ref_frame]->data[0], &mb->bmv[4*y + x],
                       4*x + x_off, 4*y + y_off, 4, 4,
                       width, height, s->linesize,
                       s->put_pixels_tab[2]);
            }
        }

        /* U/V */
        x_off >>= 1; y_off >>= 1; width >>= 1; height >>= 1;
        for (y = 0; y < 2; y++) {
            for (x = 0; x < 2; x++) {
                uvmv.x = mb->bmv[ 2*y    * 4 + 2*x  ].x +
                         mb->bmv[ 2*y    * 4 + 2*x+1].x +
                         mb->bmv[(2*y+1) * 4 + 2*x  ].x +
                         mb->bmv[(2*y+1) * 4 + 2*x+1].x;
                uvmv.y = mb->bmv[ 2*y    * 4 + 2*x  ].y +
                         mb->bmv[ 2*y    * 4 + 2*x+1].y +
                         mb->bmv[(2*y+1) * 4 + 2*x  ].y +
                         mb->bmv[(2*y+1) * 4 + 2*x+1].y;
                uvmv.x = (uvmv.x + 2 + (uvmv.x >> (INT_BIT-1))) >> 2;
                uvmv.y = (uvmv.y + 2 + (uvmv.y >> (INT_BIT-1))) >> 2;
                if (s->profile == 3) {
                    uvmv.x &= ~7;
                    uvmv.y &= ~7;
                }
                vp8_mc(s, 0, dst[1] + 4*y*s->uvlinesize + x*4,
                       s->framep[mb->ref_frame]->data[1], &uvmv,
                       4*x + x_off, 4*y + y_off, 4, 4,
                       width, height, s->uvlinesize,
                       s->put_pixels_tab[2]);
                vp8_mc(s, 0, dst[2] + 4*y*s->uvlinesize + x*4,
                       s->framep[mb->ref_frame]->data[2], &uvmv,
                       4*x + x_off, 4*y + y_off, 4, 4,
                       width, height, s->uvlinesize,
                       s->put_pixels_tab[2]);
            }
        }
        break;
    }
    case VP8_SPLITMVMODE_16x8:
        vp8_mc_part(s, dst, s->framep[mb->ref_frame], x_off, y_off,
                    0, 0, 16, 8, width, height, &mb->bmv[0]);
        vp8_mc_part(s, dst, s->framep[mb->ref_frame], x_off, y_off,
                    0, 8, 16, 8, width, height, &mb->bmv[1]);
        break;
    case VP8_SPLITMVMODE_8x16:
        vp8_mc_part(s, dst, s->framep[mb->ref_frame], x_off, y_off,
                    0, 0, 8, 16, width, height, &mb->bmv[0]);
        vp8_mc_part(s, dst, s->framep[mb->ref_frame], x_off, y_off,
                    8, 0, 8, 16, width, height, &mb->bmv[1]);
        break;
    case VP8_SPLITMVMODE_8x8:
        vp8_mc_part(s, dst, s->framep[mb->ref_frame], x_off, y_off,
                    0, 0, 8, 8, width, height, &mb->bmv[0]);
        vp8_mc_part(s, dst, s->framep[mb->ref_frame], x_off, y_off,
                    8, 0, 8, 8, width, height, &mb->bmv[1]);
        vp8_mc_part(s, dst, s->framep[mb->ref_frame], x_off, y_off,
                    0, 8, 8, 8, width, height, &mb->bmv[2]);
        vp8_mc_part(s, dst, s->framep[mb->ref_frame], x_off, y_off,
                    8, 8, 8, 8, width, height, &mb->bmv[3]);
        break;
    }
}

static void idct_mb(VP8Context *s, uint8_t *y_dst, uint8_t *u_dst, uint8_t *v_dst,
                    VP8Macroblock *mb)
{
    int x, y, nnz;

    if (mb->mode != MODE_I4x4)
        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                nnz = s->non_zero_count_cache[y][x];
                if (nnz) {
                    if (nnz == 1)
                        s->vp8dsp.vp8_idct_dc_add(y_dst+4*x, s->block[y][x], s->linesize);
                    else
                        s->vp8dsp.vp8_idct_add(y_dst+4*x, s->block[y][x], s->linesize);
                }
            }
            y_dst += 4*s->linesize;
        }

    for (y = 0; y < 2; y++) {
        for (x = 0; x < 2; x++) {
            nnz = s->non_zero_count_cache[4][(y<<1)+x];
            if (nnz) {
                if (nnz == 1)
                    s->vp8dsp.vp8_idct_dc_add(u_dst+4*x, s->block[4][(y<<1)+x], s->uvlinesize);
                else
                    s->vp8dsp.vp8_idct_add(u_dst+4*x, s->block[4][(y<<1)+x], s->uvlinesize);
            }

            nnz = s->non_zero_count_cache[5][(y<<1)+x];
            if (nnz) {
                if (nnz == 1)
                    s->vp8dsp.vp8_idct_dc_add(v_dst+4*x, s->block[5][(y<<1)+x], s->uvlinesize);
                else
                    s->vp8dsp.vp8_idct_add(v_dst+4*x, s->block[5][(y<<1)+x], s->uvlinesize);
            }
        }
        u_dst += 4*s->uvlinesize;
        v_dst += 4*s->uvlinesize;
    }
}

static void filter_level_for_mb(VP8Context *s, VP8Macroblock *mb, int *level, int *inner, int *hev_thresh)
{
    int interior_limit, filter_level;

    if (s->segmentation.enabled) {
        filter_level = s->segmentation.filter_level[mb->segment];
        if (!s->segmentation.absolute_vals)
            filter_level += s->filter.level;
    } else
        filter_level = s->filter.level;

    if (s->lf_delta.enabled) {
        filter_level += s->lf_delta.ref[mb->ref_frame];

        if (mb->ref_frame == VP56_FRAME_CURRENT) {
            if (mb->mode == MODE_I4x4)
                filter_level += s->lf_delta.mode[0];
        } else {
            if (mb->mode == VP8_MVMODE_ZERO)
                filter_level += s->lf_delta.mode[1];
            else if (mb->mode == VP8_MVMODE_SPLIT)
                filter_level += s->lf_delta.mode[3];
            else
                filter_level += s->lf_delta.mode[2];
        }
    }
    filter_level = av_clip(filter_level, 0, 63);

    interior_limit = filter_level;
    if (s->filter.sharpness) {
        interior_limit >>= s->filter.sharpness > 4 ? 2 : 1;
        interior_limit = FFMIN(interior_limit, 9 - s->filter.sharpness);
    }
    interior_limit = FFMAX(interior_limit, 1);

    *level = filter_level;
    *inner = interior_limit;

    if (hev_thresh) {
        *hev_thresh = filter_level >= 15;

        if (s->keyframe) {
            if (filter_level >= 40)
                *hev_thresh = 2;
        } else {
            if (filter_level >= 40)
                *hev_thresh = 3;
            else if (filter_level >= 20)
                *hev_thresh = 2;
        }
    }
}

// TODO: look at backup_mb_border / xchg_mb_border in h264.c
static void filter_mb(VP8Context *s, uint8_t *dst[3], VP8Macroblock *mb, int mb_x, int mb_y)
{
    int filter_level, inner_limit, hev_thresh, mbedge_lim, bedge_lim;

    filter_level_for_mb(s, mb, &filter_level, &inner_limit, &hev_thresh);
    if (!filter_level)
        return;

    mbedge_lim = 2*(filter_level+2) + inner_limit;
     bedge_lim = 2* filter_level    + inner_limit;

    if (mb_x) {
        s->vp8dsp.vp8_h_loop_filter16(dst[0], s->linesize,   mbedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_h_loop_filter8 (dst[1], s->uvlinesize, mbedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_h_loop_filter8 (dst[2], s->uvlinesize, mbedge_lim, inner_limit, hev_thresh);
    }

    if (!mb->skip || mb->mode == MODE_I4x4 || mb->mode == VP8_MVMODE_SPLIT) {
        s->vp8dsp.vp8_h_loop_filter16_inner(dst[0]+ 4, s->linesize,   bedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_h_loop_filter16_inner(dst[0]+ 8, s->linesize,   bedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_h_loop_filter16_inner(dst[0]+12, s->linesize,   bedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_h_loop_filter8_inner (dst[1]+ 4, s->uvlinesize, bedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_h_loop_filter8_inner (dst[2]+ 4, s->uvlinesize, bedge_lim, inner_limit, hev_thresh);
    }

    if (mb_y) {
        s->vp8dsp.vp8_v_loop_filter16(dst[0], s->linesize,   mbedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter8 (dst[1], s->uvlinesize, mbedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter8 (dst[2], s->uvlinesize, mbedge_lim, inner_limit, hev_thresh);
    }

    if (!mb->skip || mb->mode == MODE_I4x4 || mb->mode == VP8_MVMODE_SPLIT) {
        s->vp8dsp.vp8_v_loop_filter16_inner(dst[0]+ 4*s->linesize,   s->linesize,   bedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter16_inner(dst[0]+ 8*s->linesize,   s->linesize,   bedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter16_inner(dst[0]+12*s->linesize,   s->linesize,   bedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter8_inner (dst[1]+ 4*s->uvlinesize, s->uvlinesize, bedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter8_inner (dst[2]+ 4*s->uvlinesize, s->uvlinesize, bedge_lim, inner_limit, hev_thresh);
    }
}

static void filter_mb_simple(VP8Context *s, uint8_t *dst, VP8Macroblock *mb, int mb_x, int mb_y)
{
    int filter_level, inner_limit, mbedge_lim, bedge_lim;

    filter_level_for_mb(s, mb, &filter_level, &inner_limit, NULL);
    if (!filter_level)
        return;

    mbedge_lim = 2*(filter_level+2) + inner_limit;
     bedge_lim = 2* filter_level    + inner_limit;

    if (mb_x)
        s->vp8dsp.vp8_h_loop_filter_simple(dst, s->linesize, mbedge_lim);
    if (!mb->skip || mb->mode == MODE_I4x4 || mb->mode == VP8_MVMODE_SPLIT) {
        s->vp8dsp.vp8_h_loop_filter_simple(dst+ 4, s->linesize, bedge_lim);
        s->vp8dsp.vp8_h_loop_filter_simple(dst+ 8, s->linesize, bedge_lim);
        s->vp8dsp.vp8_h_loop_filter_simple(dst+12, s->linesize, bedge_lim);
    }

    if (mb_y)
        s->vp8dsp.vp8_v_loop_filter_simple(dst, s->linesize, mbedge_lim);
    if (!mb->skip || mb->mode == MODE_I4x4 || mb->mode == VP8_MVMODE_SPLIT) {
        s->vp8dsp.vp8_v_loop_filter_simple(dst+ 4*s->linesize, s->linesize, bedge_lim);
        s->vp8dsp.vp8_v_loop_filter_simple(dst+ 8*s->linesize, s->linesize, bedge_lim);
        s->vp8dsp.vp8_v_loop_filter_simple(dst+12*s->linesize, s->linesize, bedge_lim);
    }
}

static void filter_mb_row(VP8Context *s, int mb_y)
{
    VP8Macroblock *mb = s->macroblocks + mb_y*s->mb_stride;
    uint8_t *dst[3] = {
        s->framep[VP56_FRAME_CURRENT]->data[0] + 16*mb_y*s->linesize,
        s->framep[VP56_FRAME_CURRENT]->data[1] +  8*mb_y*s->uvlinesize,
        s->framep[VP56_FRAME_CURRENT]->data[2] +  8*mb_y*s->uvlinesize
    };
    int mb_x;

    for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
        filter_mb(s, dst, mb++, mb_x, mb_y);
        dst[0] += 16;
        dst[1] += 8;
        dst[2] += 8;
    }
}

static void filter_mb_row_simple(VP8Context *s, int mb_y)
{
    uint8_t *dst = s->framep[VP56_FRAME_CURRENT]->data[0] + 16*mb_y*s->linesize;
    VP8Macroblock *mb = s->macroblocks + mb_y*s->mb_stride;
    int mb_x;

    for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
        filter_mb_simple(s, dst, mb++, mb_x, mb_y);
        dst += 16;
    }
}

static int vp8_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                            AVPacket *avpkt)
{
    VP8Context *s = avctx->priv_data;
    int ret, mb_x, mb_y, i, y, referenced;
    enum AVDiscard skip_thresh;
    AVFrame *curframe;

    if ((ret = decode_frame_header(s, avpkt->data, avpkt->size)) < 0)
        return ret;

    referenced = s->update_last || s->update_golden == VP56_FRAME_CURRENT
                                || s->update_altref == VP56_FRAME_CURRENT;

    skip_thresh = !referenced ? AVDISCARD_NONREF :
                    !s->keyframe ? AVDISCARD_NONKEY : AVDISCARD_ALL;

    if (avctx->skip_frame >= skip_thresh) {
        s->invisible = 1;
        goto skip_decode;
    }

    for (i = 0; i < 4; i++)
        if (&s->frames[i] != s->framep[VP56_FRAME_PREVIOUS] &&
            &s->frames[i] != s->framep[VP56_FRAME_GOLDEN] &&
            &s->frames[i] != s->framep[VP56_FRAME_GOLDEN2]) {
            curframe = s->framep[VP56_FRAME_CURRENT] = &s->frames[i];
            break;
        }
    if (curframe->data[0])
        avctx->release_buffer(avctx, curframe);

    curframe->key_frame = s->keyframe;
    curframe->pict_type = s->keyframe ? FF_I_TYPE : FF_P_TYPE;
    curframe->reference = referenced ? 3 : 0;
    if ((ret = avctx->get_buffer(avctx, curframe))) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed!\n");
        return ret;
    }

    // Given that arithmetic probabilities are updated every frame, it's quite likely
    // that the values we have on a random interframe are complete junk if we didn't
    // start decode on a keyframe. So just don't display anything rather than junk.
    if (!s->keyframe && (!s->framep[VP56_FRAME_PREVIOUS] ||
                         !s->framep[VP56_FRAME_GOLDEN] ||
                         !s->framep[VP56_FRAME_GOLDEN2])) {
        av_log(avctx, AV_LOG_WARNING, "Discarding interframe without a prior keyframe!\n");
        return AVERROR_INVALIDDATA;
    }

    s->linesize   = curframe->linesize[0];
    s->uvlinesize = curframe->linesize[1];

    if (!s->edge_emu_buffer)
        s->edge_emu_buffer = av_malloc(21*s->linesize);

    memset(s->top_nnz, 0, s->mb_width*sizeof(*s->top_nnz));

    // top edge of 127 for intra prediction
    if (!(avctx->flags & CODEC_FLAG_EMU_EDGE)) {
        memset(curframe->data[0] - s->linesize  -1, 127, s->linesize  +1);
        memset(curframe->data[1] - s->uvlinesize-1, 127, s->uvlinesize+1);
        memset(curframe->data[2] - s->uvlinesize-1, 127, s->uvlinesize+1);
    }

    for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
        VP56RangeCoder *c = &s->coeff_partition[mb_y & (s->num_coeff_partitions-1)];
        VP8Macroblock *mb = s->macroblocks + mb_y*s->mb_stride;
        uint8_t *intra4x4 = s->intra4x4_pred_mode + 4*mb_y*s->b4_stride;
        uint8_t *dst[3] = {
            curframe->data[0] + 16*mb_y*s->linesize,
            curframe->data[1] +  8*mb_y*s->uvlinesize,
            curframe->data[2] +  8*mb_y*s->uvlinesize
        };

        memset(s->left_nnz, 0, sizeof(s->left_nnz));

        // left edge of 129 for intra prediction
        if (!(avctx->flags & CODEC_FLAG_EMU_EDGE))
            for (i = 0; i < 3; i++)
                for (y = 0; y < 16>>!!i; y++)
                    dst[i][y*curframe->linesize[i]-1] = 129;

        for (mb_x = 0; mb_x < s->mb_width; mb_x++) {
            decode_mb_mode(s, mb, mb_x, mb_y, intra4x4 + 4*mb_x);

            if (!mb->skip)
                decode_mb_coeffs(s, c, mb, s->top_nnz[mb_x], s->left_nnz);
            else {
                AV_ZERO128(s->non_zero_count_cache);    // luma
                AV_ZERO64(s->non_zero_count_cache[4]);  // chroma
            }

            if (mb->mode <= MODE_I4x4) {
                intra_predict(s, dst, mb, intra4x4 + 4*mb_x, mb_x, mb_y);
                memset(mb->bmv, 0, sizeof(mb->bmv));
            } else {
                inter_predict(s, dst, mb, mb_x, mb_y);
            }

            if (!mb->skip) {
                idct_mb(s, dst[0], dst[1], dst[2], mb);
            } else {
                AV_ZERO64(s->left_nnz);
                AV_WN64(s->top_nnz[mb_x], 0);   // array of 9, so unaligned

                // Reset DC block predictors if they would exist if the mb had coefficients
                if (mb->mode != MODE_I4x4 && mb->mode != VP8_MVMODE_SPLIT) {
                    s->left_nnz[8]      = 0;
                    s->top_nnz[mb_x][8] = 0;
                }
            }

            dst[0] += 16;
            dst[1] += 8;
            dst[2] += 8;
            mb++;
        }
        if (mb_y && s->filter.level && avctx->skip_loop_filter < skip_thresh) {
            if (s->filter.simple)
                filter_mb_row_simple(s, mb_y-1);
            else
                filter_mb_row(s, mb_y-1);
        }
    }
    if (s->filter.level && avctx->skip_loop_filter < skip_thresh) {
        if (s->filter.simple)
            filter_mb_row_simple(s, mb_y-1);
        else
            filter_mb_row(s, mb_y-1);
    }

skip_decode:
    // if future frames don't use the updated probabilities,
    // reset them to the values we saved
    if (!s->update_probabilities)
        s->prob[0] = s->prob[1];

    // check if golden and altref are swapped
    if (s->update_altref == VP56_FRAME_GOLDEN &&
        s->update_golden == VP56_FRAME_GOLDEN2)
        FFSWAP(AVFrame *, s->framep[VP56_FRAME_GOLDEN], s->framep[VP56_FRAME_GOLDEN2]);
    else {
        if (s->update_altref != VP56_FRAME_NONE)
            s->framep[VP56_FRAME_GOLDEN2] = s->framep[s->update_altref];

        if (s->update_golden != VP56_FRAME_NONE)
            s->framep[VP56_FRAME_GOLDEN] = s->framep[s->update_golden];
    }

    if (s->update_last) // move cur->prev
        s->framep[VP56_FRAME_PREVIOUS] = s->framep[VP56_FRAME_CURRENT];

    // release no longer referenced frames
    for (i = 0; i < 4; i++)
        if (s->frames[i].data[0] &&
            &s->frames[i] != s->framep[VP56_FRAME_CURRENT] &&
            &s->frames[i] != s->framep[VP56_FRAME_PREVIOUS] &&
            &s->frames[i] != s->framep[VP56_FRAME_GOLDEN] &&
            &s->frames[i] != s->framep[VP56_FRAME_GOLDEN2])
            avctx->release_buffer(avctx, &s->frames[i]);

    if (!s->invisible) {
        *(AVFrame*)data = *s->framep[VP56_FRAME_CURRENT];
        *data_size = sizeof(AVFrame);
    }

    return avpkt->size;
}

static av_cold int vp8_decode_init(AVCodecContext *avctx)
{
    VP8Context *s = avctx->priv_data;

    s->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_YUV420P;

    dsputil_init(&s->dsp, avctx);
    ff_h264_pred_init(&s->hpc, CODEC_ID_VP8);
    ff_vp8dsp_init(&s->vp8dsp);

    // intra pred needs edge emulation among other things
    if (avctx->flags&CODEC_FLAG_EMU_EDGE) {
        av_log(avctx, AV_LOG_ERROR, "Edge emulation not supported\n");
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static av_cold int vp8_decode_free(AVCodecContext *avctx)
{
    vp8_decode_flush(avctx);
    return 0;
}

AVCodec vp8_decoder = {
    "vp8",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_VP8,
    sizeof(VP8Context),
    vp8_decode_init,
    NULL,
    vp8_decode_free,
    vp8_decode_frame,
    CODEC_CAP_DR1,
    .flush = vp8_decode_flush,
    .long_name = NULL_IF_CONFIG_SMALL("On2 VP8"),
};
