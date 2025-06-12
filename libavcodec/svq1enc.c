/*
 * SVQ1 Encoder
 * Copyright (C) 2004 Mike Melanson <melanson@pcisys.net>
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
 * Sorenson Vector Quantizer #1 (SVQ1) video codec.
 * For more information of the SVQ1 algorithm, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "libavutil/emms.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "hpeldsp.h"
#include "me_cmp.h"
#include "mpegvideo.h"
#include "h263.h"
#include "h263enc.h"
#include "internal.h"
#include "mpegutils.h"
#include "packet_internal.h"
#include "put_bits.h"
#include "svq1.h"
#include "svq1encdsp.h"
#include "svq1enc_cb.h"
#include "version.h"

#include "libavutil/avassert.h"
#include "libavutil/frame.h"
#include "libavutil/mem_internal.h"

// Workaround for GCC bug 102513
#if AV_GCC_VERSION_AT_LEAST(10, 0) && AV_GCC_VERSION_AT_MOST(12, 0) \
    && !defined(__clang__) && !defined(__INTEL_COMPILER)
#pragma GCC optimize ("no-ipa-cp-clone")
#endif

typedef struct SVQ1EncContext {
    /* FIXME: Needed for motion estimation, should not be used for anything
     * else, the idea is to make the motion estimation eventually independent
     * of MPVEncContext, so this will be removed then. */
    MPVEncContext m;
    AVCodecContext *avctx;
    MECmpContext mecc;
    AVFrame *current_picture;
    AVFrame *last_picture;

    /* Some compression statistics */
    enum AVPictureType pict_type;
    int quality;

    /* why ooh why this sick breadth first order,
     * everything is slower and more complex */
    PutBitContext reorder_pb[6];

    int frame_width;
    int frame_height;

    /* Y plane block dimensions */
    int y_block_width;
    int y_block_height;

    DECLARE_ALIGNED(16, int16_t, encoded_block_levels)[6][7][256];

    uint16_t *mb_type;
    uint32_t *dummy;
    int16_t (*motion_val8[3])[2];
    int16_t (*motion_val16[3])[2];

    int64_t rd_total;

    uint8_t *scratchbuf;

    SVQ1EncDSPContext svq1encdsp;
} SVQ1EncContext;

static void svq1_write_header(SVQ1EncContext *s, PutBitContext *pb, int frame_type)
{
    int i;

    /* frame code */
    put_bits(pb, 22, 0x20);

    /* temporal reference (sure hope this is a "don't care") */
    put_bits(pb, 8, 0x00);

    /* frame type */
    put_bits(pb, 2, frame_type - 1);

    if (frame_type == AV_PICTURE_TYPE_I) {
        /* no checksum since frame code is 0x20 */
        /* no embedded string either */
        /* output 5 unknown bits (2 + 2 + 1) */
        put_bits(pb, 5, 2); /* 2 needed by quicktime decoder */

        i = ff_match_2uint16(ff_svq1_frame_size_table,
                             FF_ARRAY_ELEMS(ff_svq1_frame_size_table),
                             s->frame_width, s->frame_height);
        put_bits(pb, 3, i);

        if (i == 7) {
            put_bits(pb, 12, s->frame_width);
            put_bits(pb, 12, s->frame_height);
        }
    }

    /* no checksum or extra data (next 2 bits get 0) */
    put_bits(pb, 2, 0);
}

#define QUALITY_THRESHOLD    100
#define THRESHOLD_MULTIPLIER 0.6

static int encode_block(SVQ1EncContext *s, uint8_t *src, uint8_t *ref,
                        uint8_t *decoded, int stride, unsigned level,
                        int threshold, int lambda, int intra)
{
    int count, y, x, i, j, split, best_mean, best_score, best_count;
    int best_vector[6];
    int block_sum[7] = { 0, 0, 0, 0, 0, 0 };
    int w            = 2 << (level + 2 >> 1);
    int h            = 2 << (level + 1 >> 1);
    int size         = w * h;
    int16_t (*block)[256] = s->encoded_block_levels[level];
    const int8_t *codebook_sum, *codebook;
    const uint16_t(*mean_vlc)[2];
    const uint8_t(*multistage_vlc)[2];

    best_score = 0;
    // FIXME: Optimize, this does not need to be done multiple times.
    if (intra) {
        // level is 5 when encode_block is called from svq1_encode_plane
        // and always < 4 when called recursively from this function.
        codebook_sum   = level < 4 ? svq1_intra_codebook_sum[level] : NULL;
        codebook       = ff_svq1_intra_codebooks[level];
        mean_vlc       = ff_svq1_intra_mean_vlc;
        multistage_vlc = ff_svq1_intra_multistage_vlc[level];
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                int v = src[x + y * stride];
                block[0][x + w * y] = v;
                best_score         += v * v;
                block_sum[0]       += v;
            }
        }
    } else {
        // level is 5 or < 4, see above for details.
        codebook_sum   = level < 4 ? svq1_inter_codebook_sum[level] : NULL;
        codebook       = ff_svq1_inter_codebooks[level];
        mean_vlc       = ff_svq1_inter_mean_vlc + 256;
        multistage_vlc = ff_svq1_inter_multistage_vlc[level];
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                int v = src[x + y * stride] - ref[x + y * stride];
                block[0][x + w * y] = v;
                best_score         += v * v;
                block_sum[0]       += v;
            }
        }
    }

    best_count  = 0;
    best_score -= (int)((unsigned)block_sum[0] * block_sum[0] >> (level + 3));
    best_mean   = block_sum[0] + (size >> 1) >> (level + 3);

    if (level < 4) {
        for (count = 1; count < 7; count++) {
            int best_vector_score = INT_MAX;
            int best_vector_sum   = -999, best_vector_mean = -999;
            const int stage       = count - 1;
            const int8_t *vector;

            for (i = 0; i < 16; i++) {
                int sum = codebook_sum[stage * 16 + i];
                int sqr, diff, score;

                vector = codebook + stage * size * 16 + i * size;
                sqr    = s->svq1encdsp.ssd_int8_vs_int16(vector, block[stage], size);
                diff   = block_sum[stage] - sum;
                score  = sqr - (diff * (int64_t)diff >> (level + 3)); // FIXME: 64 bits slooow
                if (score < best_vector_score) {
                    int mean = diff + (size >> 1) >> (level + 3);
                    av_assert2(mean > -300 && mean < 300);
                    mean               = av_clip(mean, intra ? 0 : -256, 255);
                    best_vector_score  = score;
                    best_vector[stage] = i;
                    best_vector_sum    = sum;
                    best_vector_mean   = mean;
                }
            }
            av_assert0(best_vector_mean != -999);
            vector = codebook + stage * size * 16 + best_vector[stage] * size;
            for (j = 0; j < size; j++)
                block[stage + 1][j] = block[stage][j] - vector[j];
            block_sum[stage + 1] = block_sum[stage] - best_vector_sum;
            best_vector_score   += lambda *
                                   (+1 + 4 * count +
                                    multistage_vlc[1 + count][1]
                                    + mean_vlc[best_vector_mean][1]);

            if (best_vector_score < best_score) {
                best_score = best_vector_score;
                best_count = count;
                best_mean  = best_vector_mean;
            }
        }
    }

    if (best_mean == -128)
        best_mean = -127;
    else if (best_mean == 128)
        best_mean = 127;

    split = 0;
    if (best_score > threshold && level) {
        int score  = 0;
        int offset = level & 1 ? stride * h / 2 : w / 2;
        PutBitContext backup[6];

        for (i = level - 1; i >= 0; i--)
            backup[i] = s->reorder_pb[i];
        score += encode_block(s, src, ref, decoded, stride, level - 1,
                              threshold >> 1, lambda, intra);
        score += encode_block(s, src + offset, ref + offset, decoded + offset,
                              stride, level - 1, threshold >> 1, lambda, intra);
        score += lambda;

        if (score < best_score) {
            best_score = score;
            split      = 1;
        } else {
            for (i = level - 1; i >= 0; i--)
                s->reorder_pb[i] = backup[i];
        }
    }
    if (level > 0)
        put_bits(&s->reorder_pb[level], 1, split);

    if (!split) {
        av_assert1(best_mean >= 0 && best_mean < 256 || !intra);
        av_assert1(best_mean >= -256 && best_mean < 256);
        av_assert1(best_count >= 0 && best_count < 7);
        av_assert1(level < 4 || best_count == 0);

        /* output the encoding */
        put_bits(&s->reorder_pb[level],
                 multistage_vlc[1 + best_count][1],
                 multistage_vlc[1 + best_count][0]);
        put_bits(&s->reorder_pb[level], mean_vlc[best_mean][1],
                 mean_vlc[best_mean][0]);

        for (i = 0; i < best_count; i++) {
            av_assert2(best_vector[i] >= 0 && best_vector[i] < 16);
            put_bits(&s->reorder_pb[level], 4, best_vector[i]);
        }

        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                decoded[x + y * stride] = src[x + y * stride] -
                                          block[best_count][x + w * y] +
                                          best_mean;
    }

    return best_score;
}

static void init_block_index(MpegEncContext *const s)
{
    s->block_index[0]= s->b8_stride*(s->mb_y*2    )     + s->mb_x*2;
    s->block_index[1]= s->b8_stride*(s->mb_y*2    ) + 1 + s->mb_x*2;
    s->block_index[2]= s->b8_stride*(s->mb_y*2 + 1)     + s->mb_x*2;
    s->block_index[3]= s->b8_stride*(s->mb_y*2 + 1) + 1 + s->mb_x*2;
}

static int svq1_encode_plane(SVQ1EncContext *s, int plane,
                             PutBitContext *pb,
                             const unsigned char *src_plane,
                             unsigned char *ref_plane,
                             unsigned char *decoded_plane,
                             int width, int height, int src_stride, int stride)
{
    MpegEncContext *const s2 = &s->m.c;
    int x, y;
    int i;
    int block_width, block_height;
    int level;
    int threshold[6];
    uint8_t *src     = s->scratchbuf + stride * 32;
    const int lambda = (s->quality * s->quality) >>
                       (2 * FF_LAMBDA_SHIFT);

    /* figure out the acceptable level thresholds in advance */
    threshold[5] = QUALITY_THRESHOLD;
    for (level = 4; level >= 0; level--)
        threshold[level] = threshold[level + 1] * THRESHOLD_MULTIPLIER;

    block_width  = (width  + 15) / 16;
    block_height = (height + 15) / 16;

    if (s->pict_type == AV_PICTURE_TYPE_P) {
        s2->last_pic.data[0]        = ref_plane;
        s2->linesize                      =
        s2->last_pic.linesize[0]    =
        s->m.new_pic->linesize[0]      =
        s2->cur_pic.linesize[0] = stride;
        s2->width                         = width;
        s2->height                        = height;
        s2->mb_width                      = block_width;
        s2->mb_height                     = block_height;
        s2->mb_stride                     = s2->mb_width + 1;
        s2->b8_stride                     = 2 * s2->mb_width + 1;
        s->m.f_code                       = 1;
        s2->pict_type                     = s->pict_type;
        s->m.me.scene_change_score        = 0;
        // s2->out_format                    = FMT_H263;
        // s2->unrestricted_mv               = 1;
        s->m.lambda                       = s->quality;
        s2->qscale                        = s->m.lambda * 139 +
                                             FF_LAMBDA_SCALE * 64 >>
                                             FF_LAMBDA_SHIFT + 7;
        s->m.lambda2                      = s->m.lambda * s->m.lambda +
                                             FF_LAMBDA_SCALE / 2 >>
                                             FF_LAMBDA_SHIFT;

        s->m.mb_type = s->mb_type;

        // dummies, to avoid segfaults
        s->m.mb_mean   = (uint8_t *)s->dummy;
        s->m.mb_var    = (uint16_t *)s->dummy;
        s->m.mc_mb_var = (uint16_t *)s->dummy;
        s2->cur_pic.mb_type = s->dummy;

        s2->cur_pic.motion_val[0]   = s->motion_val8[plane] + 2;
        s->m.p_mv_table             = s->motion_val16[plane] +
                                             s2->mb_stride + 1;
        ff_me_init_pic(&s->m);

        s->m.me.dia_size     = s->avctx->dia_size;
        s2->first_slice_line = 1;
        for (y = 0; y < block_height; y++) {
            s->m.new_pic->data[0]  = src - y * 16 * stride; // ugly
            s2->mb_y                  = y;

            for (i = 0; i < 16 && i + 16 * y < height; i++) {
                memcpy(&src[i * stride], &src_plane[(i + 16 * y) * src_stride],
                       width);
                for (x = width; x < 16 * block_width; x++)
                    src[i * stride + x] = src[i * stride + x - 1];
            }
            for (; i < 16 && i + 16 * y < 16 * block_height; i++)
                memcpy(&src[i * stride], &src[(i - 1) * stride],
                       16 * block_width);

            for (x = 0; x < block_width; x++) {
                s2->mb_x = x;
                init_block_index(s2);

                ff_estimate_p_frame_motion(&s->m, x, y);
            }
            s2->first_slice_line = 0;
        }

        ff_fix_long_p_mvs(&s->m, CANDIDATE_MB_TYPE_INTRA);
        ff_fix_long_mvs(&s->m, NULL, 0, s->m.p_mv_table, s->m.f_code,
                        CANDIDATE_MB_TYPE_INTER, 0);
    }

    s2->first_slice_line = 1;
    for (y = 0; y < block_height; y++) {
        for (i = 0; i < 16 && i + 16 * y < height; i++) {
            memcpy(&src[i * stride], &src_plane[(i + 16 * y) * src_stride],
                   width);
            for (x = width; x < 16 * block_width; x++)
                src[i * stride + x] = src[i * stride + x - 1];
        }
        for (; i < 16 && i + 16 * y < 16 * block_height; i++)
            memcpy(&src[i * stride], &src[(i - 1) * stride], 16 * block_width);

        s2->mb_y = y;
        for (x = 0; x < block_width; x++) {
            uint8_t reorder_buffer[2][6][7 * 32];
            int count[2][6];
            int offset       = y * 16 * stride + x * 16;
            uint8_t *decoded = decoded_plane + offset;
            const uint8_t *ref = ref_plane + offset;
            int score[4]     = { 0, 0, 0, 0 }, best;
            uint8_t *temp    = s->scratchbuf;

            if (put_bytes_left(pb, 0) < 3000) { // FIXME: check size
                av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
                return -1;
            }

            s2->mb_x = x;
            init_block_index(s2);

            if (s->pict_type == AV_PICTURE_TYPE_I ||
                (s->m.mb_type[x + y * s2->mb_stride] &
                 CANDIDATE_MB_TYPE_INTRA)) {
                for (i = 0; i < 6; i++)
                    init_put_bits(&s->reorder_pb[i], reorder_buffer[0][i],
                                  7 * 32);
                if (s->pict_type == AV_PICTURE_TYPE_P) {
                    put_bits(&s->reorder_pb[5], SVQ1_BLOCK_INTRA_LEN, SVQ1_BLOCK_INTRA_CODE);
                    score[0] = SVQ1_BLOCK_INTRA_LEN * lambda;
                }
                score[0] += encode_block(s, src + 16 * x, src + 16 * x /* unused */,
                                         temp, stride, 5, 64, lambda, 1);
                for (i = 0; i < 6; i++) {
                    count[0][i] = put_bits_count(&s->reorder_pb[i]);
                    flush_put_bits(&s->reorder_pb[i]);
                }
            } else
                score[0] = INT_MAX;

            best = 0;

            if (s->pict_type == AV_PICTURE_TYPE_P) {
                int mx, my, pred_x, pred_y, dxy;
                int16_t *motion_ptr;

                motion_ptr = ff_h263_pred_motion(s2, 0, 0, &pred_x, &pred_y);
                if (s->m.mb_type[x + y * s2->mb_stride] &
                    CANDIDATE_MB_TYPE_INTER) {
                    for (i = 0; i < 6; i++)
                        init_put_bits(&s->reorder_pb[i], reorder_buffer[1][i],
                                      7 * 32);

                    put_bits(&s->reorder_pb[5], SVQ1_BLOCK_INTER_LEN, SVQ1_BLOCK_INTER_CODE);

                    mx      = motion_ptr[0];
                    my      = motion_ptr[1];
                    av_assert1(mx     >= -32 && mx     <= 31);
                    av_assert1(my     >= -32 && my     <= 31);
                    av_assert1(pred_x >= -32 && pred_x <= 31);
                    av_assert1(pred_y >= -32 && pred_y <= 31);
                    ff_h263_encode_motion(&s->reorder_pb[5], mx - pred_x, 1);
                    ff_h263_encode_motion(&s->reorder_pb[5], my - pred_y, 1);
                    score[1]        += lambda * put_bits_count(&s->reorder_pb[5]);

                    dxy = (mx & 1) + 2 * (my & 1);

                    s2->hdsp.put_pixels_tab[0][dxy](temp + 16*stride,
                                                    ref + (mx >> 1) +
                                                    stride * (my >> 1),
                                                    stride, 16);

                    score[1] += encode_block(s, src + 16 * x, temp + 16*stride,
                                             decoded, stride, 5, 64, lambda, 0);
                    best      = score[1] <= score[0];

                    score[2]  = s->mecc.sse[0](NULL, src + 16 * x, ref,
                                               stride, 16);
                    score[2] += SVQ1_BLOCK_SKIP_LEN * lambda;
                    if (score[2] < score[best] && mx == 0 && my == 0) {
                        best = 2;
                        s2->hdsp.put_pixels_tab[0][0](decoded, ref, stride, 16);
                        put_bits(pb, SVQ1_BLOCK_SKIP_LEN, SVQ1_BLOCK_SKIP_CODE);
                    }
                }

                if (best == 1) {
                    for (i = 0; i < 6; i++) {
                        count[1][i] = put_bits_count(&s->reorder_pb[i]);
                        flush_put_bits(&s->reorder_pb[i]);
                    }
                } else {
                    motion_ptr[0]                      =
                    motion_ptr[1]                      =
                    motion_ptr[2]                      =
                    motion_ptr[3]                      =
                    motion_ptr[0 + 2 * s2->b8_stride] =
                    motion_ptr[1 + 2 * s2->b8_stride] =
                    motion_ptr[2 + 2 * s2->b8_stride] =
                    motion_ptr[3 + 2 * s2->b8_stride] = 0;
                }
            }

            s->rd_total += score[best];

            if (best != 2)
            for (i = 5; i >= 0; i--)
                ff_copy_bits(pb, reorder_buffer[best][i],
                                 count[best][i]);
            if (best == 0)
                s2->hdsp.put_pixels_tab[0][0](decoded, temp, stride, 16);
        }
        s2->first_slice_line = 0;
    }
    return 0;
}

static av_cold int svq1_encode_end(AVCodecContext *avctx)
{
    SVQ1EncContext *const s = avctx->priv_data;
    int i;

    if (avctx->frame_num)
        av_log(avctx, AV_LOG_DEBUG, "RD: %f\n",
               s->rd_total / (double)(avctx->width * avctx->height *
                                      avctx->frame_num));

    av_freep(&s->m.me.scratchpad);
    av_freep(&s->mb_type);
    av_freep(&s->dummy);
    av_freep(&s->scratchbuf);

    for (i = 0; i < 3; i++) {
        av_freep(&s->motion_val8[i]);
        av_freep(&s->motion_val16[i]);
    }

    av_frame_free(&s->current_picture);
    av_frame_free(&s->last_picture);
    av_frame_free(&s->m.new_pic);

    return 0;
}

static av_cold int write_ident(AVCodecContext *avctx, const char *ident)
{
    int size = strlen(ident);
    avctx->extradata = av_malloc(size + 8);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);
    AV_WB32(avctx->extradata, size + 8);
    AV_WL32(avctx->extradata + 4, MKTAG('S', 'V', 'Q', '1'));
    memcpy(avctx->extradata + 8, ident, size);
    avctx->extradata_size = size + 8;
    return 0;
}

static av_cold int svq1_encode_init(AVCodecContext *avctx)
{
    SVQ1EncContext *const s = avctx->priv_data;
    int ret;

    if (avctx->width >= 4096 || avctx->height >= 4096) {
        av_log(avctx, AV_LOG_ERROR, "Dimensions too large, maximum is 4095x4095\n");
        return AVERROR(EINVAL);
    }

    ff_hpeldsp_init(&s->m.c.hdsp, avctx->flags);
    ff_me_cmp_init(&s->mecc, avctx);
    ret = ff_me_init(&s->m.me, avctx, &s->mecc, 0);
    if (ret < 0)
        return ret;
    ff_mpegvideoencdsp_init(&s->m.mpvencdsp, avctx);

    s->current_picture = av_frame_alloc();
    s->last_picture    = av_frame_alloc();
    if (!s->current_picture || !s->last_picture) {
        return AVERROR(ENOMEM);
    }
    ret = ff_encode_alloc_frame(avctx, s->current_picture);
    if (ret < 0)
        return ret;
    ret = ff_encode_alloc_frame(avctx, s->last_picture);
    if (ret < 0)
        return ret;
    s->scratchbuf = av_malloc_array(s->current_picture->linesize[0], 16 * 3);
    if (!s->scratchbuf)
        return AVERROR(ENOMEM);

    s->frame_width  = avctx->width;
    s->frame_height = avctx->height;

    s->y_block_width  = (s->frame_width  + 15) / 16;
    s->y_block_height = (s->frame_height + 15) / 16;

    s->avctx               = avctx;
    s->m.c.avctx           = avctx;

    for (size_t plane = 0; plane < FF_ARRAY_ELEMS(s->motion_val16); ++plane) {
        const int shift = plane ? 2 : 0;
        unsigned block_height = ((s->frame_height >> shift) + 15U) / 16;
        unsigned block_width  = ((s->frame_width  >> shift) + 15U) / 16;

        s->motion_val8[plane]  = av_calloc((2 * block_width + 1) * block_height * 2 + 2,
                                           2 * sizeof(int16_t));
        s->motion_val16[plane] = av_calloc((block_width + 1) * (block_height + 2) + 1,
                                           2 * sizeof(int16_t));
        if (!s->motion_val8[plane] || !s->motion_val16[plane])
            return AVERROR(ENOMEM);
    }

    s->m.c.picture_structure = PICT_FRAME;
    s->m.me.temp           =
    s->m.me.scratchpad     = av_mallocz((avctx->width + 64) *
                                        2 * 16 * 2 * sizeof(uint8_t));
    s->mb_type             = av_mallocz((s->y_block_width + 1) *
                                        s->y_block_height * sizeof(int16_t));
    s->dummy               = av_mallocz((s->y_block_width + 1) *
                                        s->y_block_height * sizeof(int32_t));
    s->m.new_pic       = av_frame_alloc();

    if (!s->m.me.scratchpad ||
        !s->mb_type || !s->dummy || !s->m.new_pic)
        return AVERROR(ENOMEM);

    ff_svq1enc_init(&s->svq1encdsp);

    s->m.me.mv_penalty = ff_h263_get_mv_penalty();

    return write_ident(avctx, s->avctx->flags & AV_CODEC_FLAG_BITEXACT ? "Lavc" : LIBAVCODEC_IDENT);
}

static int svq1_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pict, int *got_packet)
{
    SVQ1EncContext *const s = avctx->priv_data;
    PutBitContext pb;
    int i, ret;

    ret = ff_alloc_packet(avctx, pkt, s->y_block_width * s->y_block_height *
                          MAX_MB_BYTES * 3 + FF_INPUT_BUFFER_MIN_SIZE);
    if (ret < 0)
        return ret;

    FFSWAP(AVFrame*, s->current_picture, s->last_picture);

    if (avctx->gop_size && (avctx->frame_num % avctx->gop_size))
        s->pict_type = AV_PICTURE_TYPE_P;
    else
        s->pict_type = AV_PICTURE_TYPE_I;
    s->quality = pict->quality;

    ff_side_data_set_encoder_stats(pkt, pict->quality, NULL, 0, s->pict_type);

    init_put_bits(&pb, pkt->data, pkt->size);
    svq1_write_header(s, &pb, s->pict_type);
    for (i = 0; i < 3; i++) {
        int ret = svq1_encode_plane(s, i, &pb,
                              pict->data[i],
                              s->last_picture->data[i],
                              s->current_picture->data[i],
                              s->frame_width  / (i ? 4 : 1),
                              s->frame_height / (i ? 4 : 1),
                              pict->linesize[i],
                              s->current_picture->linesize[i]);
        emms_c();
        if (ret < 0)
            return ret;
    }

    // align_put_bits(&pb);
    while (put_bits_count(&pb) & 31)
        put_bits(&pb, 1, 0);

    flush_put_bits(&pb);

    pkt->size = put_bytes_output(&pb);
    if (s->pict_type == AV_PICTURE_TYPE_I)
        pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

#define OFFSET(x) offsetof(struct SVQ1EncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "motion-est", "Motion estimation algorithm", OFFSET(m.me.motion_est), AV_OPT_TYPE_INT, { .i64 = FF_ME_EPZS }, FF_ME_ZERO, FF_ME_XONE, VE, .unit = "motion-est"},
        { "zero", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FF_ME_ZERO }, 0, 0, FF_MPV_OPT_FLAGS, .unit = "motion-est" },
        { "epzs", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FF_ME_EPZS }, 0, 0, FF_MPV_OPT_FLAGS, .unit = "motion-est" },
        { "xone", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FF_ME_XONE }, 0, 0, FF_MPV_OPT_FLAGS, .unit = "motion-est" },

    { NULL },
};

static const AVClass svq1enc_class = {
    .class_name = "svq1enc",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_svq1_encoder = {
    .p.name         = "svq1",
    CODEC_LONG_NAME("Sorenson Vector Quantizer 1 / Sorenson Video 1 / SVQ1"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_SVQ1,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(SVQ1EncContext),
    .p.priv_class   = &svq1enc_class,
    .init           = svq1_encode_init,
    FF_CODEC_ENCODE_CB(svq1_encode_frame),
    .close          = svq1_encode_end,
    CODEC_PIXFMTS(AV_PIX_FMT_YUV410P),
    .color_ranges   = AVCOL_RANGE_MPEG,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
