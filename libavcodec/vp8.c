/*
 * VP7/VP8 compatible video decoder
 *
 * Copyright (C) 2010 David Conrad
 * Copyright (C) 2010 Ronald S. Bultje
 * Copyright (C) 2010 Fiona Glaser
 * Copyright (C) 2012 Daniel Kang
 * Copyright (C) 2014 Peter Ross
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

#include "config_components.h"

#include "libavutil/imgutils.h"
#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "hwconfig.h"
#include "internal.h"
#include "mathops.h"
#include "rectangle.h"
#include "thread.h"
#include "threadframe.h"
#include "vp8.h"
#include "vp8data.h"

#if ARCH_ARM
#   include "arm/vp8.h"
#endif

#if CONFIG_VP7_DECODER && CONFIG_VP8_DECODER
#define VPX(vp7, f) (vp7 ? vp7_ ## f : vp8_ ## f)
#elif CONFIG_VP7_DECODER
#define VPX(vp7, f) vp7_ ## f
#else // CONFIG_VP8_DECODER
#define VPX(vp7, f) vp8_ ## f
#endif

static void free_buffers(VP8Context *s)
{
    int i;
    if (s->thread_data)
        for (i = 0; i < MAX_THREADS; i++) {
#if HAVE_THREADS
            pthread_cond_destroy(&s->thread_data[i].cond);
            pthread_mutex_destroy(&s->thread_data[i].lock);
#endif
            av_freep(&s->thread_data[i].filter_strength);
        }
    av_freep(&s->thread_data);
    av_freep(&s->macroblocks_base);
    av_freep(&s->intra4x4_pred_mode_top);
    av_freep(&s->top_nnz);
    av_freep(&s->top_border);

    s->macroblocks = NULL;
}

static int vp8_alloc_frame(VP8Context *s, VP8Frame *f, int ref)
{
    int ret;
    if ((ret = ff_thread_get_ext_buffer(s->avctx, &f->tf,
                                        ref ? AV_GET_BUFFER_FLAG_REF : 0)) < 0)
        return ret;
    if (!(f->seg_map = av_buffer_allocz(s->mb_width * s->mb_height)))
        goto fail;
    if (s->avctx->hwaccel) {
        const AVHWAccel *hwaccel = s->avctx->hwaccel;
        if (hwaccel->frame_priv_data_size) {
            f->hwaccel_priv_buf = av_buffer_allocz(hwaccel->frame_priv_data_size);
            if (!f->hwaccel_priv_buf)
                goto fail;
            f->hwaccel_picture_private = f->hwaccel_priv_buf->data;
        }
    }
    return 0;

fail:
    av_buffer_unref(&f->seg_map);
    ff_thread_release_ext_buffer(s->avctx, &f->tf);
    return AVERROR(ENOMEM);
}

static void vp8_release_frame(VP8Context *s, VP8Frame *f)
{
    av_buffer_unref(&f->seg_map);
    av_buffer_unref(&f->hwaccel_priv_buf);
    f->hwaccel_picture_private = NULL;
    ff_thread_release_ext_buffer(s->avctx, &f->tf);
}

#if CONFIG_VP8_DECODER
static int vp8_ref_frame(VP8Context *s, VP8Frame *dst, VP8Frame *src)
{
    int ret;

    vp8_release_frame(s, dst);

    if ((ret = ff_thread_ref_frame(&dst->tf, &src->tf)) < 0)
        return ret;
    if (src->seg_map &&
        !(dst->seg_map = av_buffer_ref(src->seg_map))) {
        vp8_release_frame(s, dst);
        return AVERROR(ENOMEM);
    }
    if (src->hwaccel_picture_private) {
        dst->hwaccel_priv_buf = av_buffer_ref(src->hwaccel_priv_buf);
        if (!dst->hwaccel_priv_buf)
            return AVERROR(ENOMEM);
        dst->hwaccel_picture_private = dst->hwaccel_priv_buf->data;
    }

    return 0;
}
#endif /* CONFIG_VP8_DECODER */

static void vp8_decode_flush_impl(AVCodecContext *avctx, int free_mem)
{
    VP8Context *s = avctx->priv_data;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(s->frames); i++)
        vp8_release_frame(s, &s->frames[i]);
    memset(s->framep, 0, sizeof(s->framep));

    if (free_mem)
        free_buffers(s);
}

static void vp8_decode_flush(AVCodecContext *avctx)
{
    vp8_decode_flush_impl(avctx, 0);
}

static VP8Frame *vp8_find_free_buffer(VP8Context *s)
{
    VP8Frame *frame = NULL;
    int i;

    // find a free buffer
    for (i = 0; i < 5; i++)
        if (&s->frames[i] != s->framep[VP56_FRAME_CURRENT]  &&
            &s->frames[i] != s->framep[VP56_FRAME_PREVIOUS] &&
            &s->frames[i] != s->framep[VP56_FRAME_GOLDEN]   &&
            &s->frames[i] != s->framep[VP56_FRAME_GOLDEN2]) {
            frame = &s->frames[i];
            break;
        }
    if (i == 5) {
        av_log(s->avctx, AV_LOG_FATAL, "Ran out of free frames!\n");
        abort();
    }
    if (frame->tf.f->buf[0])
        vp8_release_frame(s, frame);

    return frame;
}

static enum AVPixelFormat get_pixel_format(VP8Context *s)
{
    enum AVPixelFormat pix_fmts[] = {
#if CONFIG_VP8_VAAPI_HWACCEL
        AV_PIX_FMT_VAAPI,
#endif
#if CONFIG_VP8_NVDEC_HWACCEL
        AV_PIX_FMT_CUDA,
#endif
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE,
    };

    return ff_get_format(s->avctx, pix_fmts);
}

static av_always_inline
int update_dimensions(VP8Context *s, int width, int height, int is_vp7)
{
    AVCodecContext *avctx = s->avctx;
    int i, ret, dim_reset = 0;

    if (width  != s->avctx->width || ((width+15)/16 != s->mb_width || (height+15)/16 != s->mb_height) && s->macroblocks_base ||
        height != s->avctx->height) {
        vp8_decode_flush_impl(s->avctx, 1);

        ret = ff_set_dimensions(s->avctx, width, height);
        if (ret < 0)
            return ret;

        dim_reset = (s->macroblocks_base != NULL);
    }

    if ((s->pix_fmt == AV_PIX_FMT_NONE || dim_reset) &&
         !s->actually_webp && !is_vp7) {
        s->pix_fmt = get_pixel_format(s);
        if (s->pix_fmt < 0)
            return AVERROR(EINVAL);
        avctx->pix_fmt = s->pix_fmt;
    }

    s->mb_width  = (s->avctx->coded_width  + 15) / 16;
    s->mb_height = (s->avctx->coded_height + 15) / 16;

    s->mb_layout = is_vp7 || avctx->active_thread_type == FF_THREAD_SLICE &&
                   avctx->thread_count > 1;
    if (!s->mb_layout) { // Frame threading and one thread
        s->macroblocks_base       = av_mallocz((s->mb_width + s->mb_height * 2 + 1) *
                                               sizeof(*s->macroblocks));
        s->intra4x4_pred_mode_top = av_mallocz(s->mb_width * 4);
    } else // Sliced threading
        s->macroblocks_base = av_mallocz((s->mb_width + 2) * (s->mb_height + 2) *
                                         sizeof(*s->macroblocks));
    s->top_nnz     = av_mallocz(s->mb_width * sizeof(*s->top_nnz));
    s->top_border  = av_mallocz((s->mb_width + 1) * sizeof(*s->top_border));
    s->thread_data = av_mallocz(MAX_THREADS * sizeof(VP8ThreadData));

    if (!s->macroblocks_base || !s->top_nnz || !s->top_border ||
        !s->thread_data || (!s->intra4x4_pred_mode_top && !s->mb_layout)) {
        free_buffers(s);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < MAX_THREADS; i++) {
        s->thread_data[i].filter_strength =
            av_mallocz(s->mb_width * sizeof(*s->thread_data[0].filter_strength));
        if (!s->thread_data[i].filter_strength) {
            free_buffers(s);
            return AVERROR(ENOMEM);
        }
#if HAVE_THREADS
        pthread_mutex_init(&s->thread_data[i].lock, NULL);
        pthread_cond_init(&s->thread_data[i].cond, NULL);
#endif
    }

    s->macroblocks = s->macroblocks_base + 1;

    return 0;
}

static int vp7_update_dimensions(VP8Context *s, int width, int height)
{
    return update_dimensions(s, width, height, IS_VP7);
}

static int vp8_update_dimensions(VP8Context *s, int width, int height)
{
    return update_dimensions(s, width, height, IS_VP8);
}


static void parse_segment_info(VP8Context *s)
{
    VP56RangeCoder *c = &s->c;
    int i;

    s->segmentation.update_map = vp8_rac_get(c);
    s->segmentation.update_feature_data = vp8_rac_get(c);

    if (s->segmentation.update_feature_data) {
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

    for (i = 0; i < 4; i++) {
        if (vp8_rac_get(c)) {
            s->lf_delta.ref[i] = vp8_rac_get_uint(c, 6);

            if (vp8_rac_get(c))
                s->lf_delta.ref[i] = -s->lf_delta.ref[i];
        }
    }

    for (i = MODE_I4x4; i <= VP8_MVMODE_SPLIT; i++) {
        if (vp8_rac_get(c)) {
            s->lf_delta.mode[i] = vp8_rac_get_uint(c, 6);

            if (vp8_rac_get(c))
                s->lf_delta.mode[i] = -s->lf_delta.mode[i];
        }
    }
}

static int setup_partitions(VP8Context *s, const uint8_t *buf, int buf_size)
{
    const uint8_t *sizes = buf;
    int i;
    int ret;

    s->num_coeff_partitions = 1 << vp8_rac_get_uint(&s->c, 2);

    buf      += 3 * (s->num_coeff_partitions - 1);
    buf_size -= 3 * (s->num_coeff_partitions - 1);
    if (buf_size < 0)
        return -1;

    for (i = 0; i < s->num_coeff_partitions - 1; i++) {
        int size = AV_RL24(sizes + 3 * i);
        if (buf_size - size < 0)
            return -1;
        s->coeff_partition_size[i] = size;

        ret = ff_vp56_init_range_decoder(&s->coeff_partition[i], buf, size);
        if (ret < 0)
            return ret;
        buf      += size;
        buf_size -= size;
    }

    s->coeff_partition_size[i] = buf_size;
    ff_vp56_init_range_decoder(&s->coeff_partition[i], buf, buf_size);

    return 0;
}

static void vp7_get_quants(VP8Context *s)
{
    VP56RangeCoder *c = &s->c;

    int yac_qi  = vp8_rac_get_uint(c, 7);
    int ydc_qi  = vp8_rac_get(c) ? vp8_rac_get_uint(c, 7) : yac_qi;
    int y2dc_qi = vp8_rac_get(c) ? vp8_rac_get_uint(c, 7) : yac_qi;
    int y2ac_qi = vp8_rac_get(c) ? vp8_rac_get_uint(c, 7) : yac_qi;
    int uvdc_qi = vp8_rac_get(c) ? vp8_rac_get_uint(c, 7) : yac_qi;
    int uvac_qi = vp8_rac_get(c) ? vp8_rac_get_uint(c, 7) : yac_qi;

    s->qmat[0].luma_qmul[0]    =       vp7_ydc_qlookup[ydc_qi];
    s->qmat[0].luma_qmul[1]    =       vp7_yac_qlookup[yac_qi];
    s->qmat[0].luma_dc_qmul[0] =       vp7_y2dc_qlookup[y2dc_qi];
    s->qmat[0].luma_dc_qmul[1] =       vp7_y2ac_qlookup[y2ac_qi];
    s->qmat[0].chroma_qmul[0]  = FFMIN(vp7_ydc_qlookup[uvdc_qi], 132);
    s->qmat[0].chroma_qmul[1]  =       vp7_yac_qlookup[uvac_qi];
}

static void vp8_get_quants(VP8Context *s)
{
    VP56RangeCoder *c = &s->c;
    int i, base_qi;

    s->quant.yac_qi     = vp8_rac_get_uint(c, 7);
    s->quant.ydc_delta  = vp8_rac_get_sint(c, 4);
    s->quant.y2dc_delta = vp8_rac_get_sint(c, 4);
    s->quant.y2ac_delta = vp8_rac_get_sint(c, 4);
    s->quant.uvdc_delta = vp8_rac_get_sint(c, 4);
    s->quant.uvac_delta = vp8_rac_get_sint(c, 4);

    for (i = 0; i < 4; i++) {
        if (s->segmentation.enabled) {
            base_qi = s->segmentation.base_quant[i];
            if (!s->segmentation.absolute_vals)
                base_qi += s->quant.yac_qi;
        } else
            base_qi = s->quant.yac_qi;

        s->qmat[i].luma_qmul[0]    = vp8_dc_qlookup[av_clip_uintp2(base_qi + s->quant.ydc_delta,  7)];
        s->qmat[i].luma_qmul[1]    = vp8_ac_qlookup[av_clip_uintp2(base_qi,              7)];
        s->qmat[i].luma_dc_qmul[0] = vp8_dc_qlookup[av_clip_uintp2(base_qi + s->quant.y2dc_delta, 7)] * 2;
        /* 101581>>16 is equivalent to 155/100 */
        s->qmat[i].luma_dc_qmul[1] = vp8_ac_qlookup[av_clip_uintp2(base_qi + s->quant.y2ac_delta, 7)] * 101581 >> 16;
        s->qmat[i].chroma_qmul[0]  = vp8_dc_qlookup[av_clip_uintp2(base_qi + s->quant.uvdc_delta, 7)];
        s->qmat[i].chroma_qmul[1]  = vp8_ac_qlookup[av_clip_uintp2(base_qi + s->quant.uvac_delta, 7)];

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

static void vp78_reset_probability_tables(VP8Context *s)
{
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 16; j++)
            memcpy(s->prob->token[i][j], vp8_token_default_probs[i][vp8_coeff_band[j]],
                   sizeof(s->prob->token[i][j]));
}

static void vp78_update_probability_tables(VP8Context *s)
{
    VP56RangeCoder *c = &s->c;
    int i, j, k, l, m;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 8; j++)
            for (k = 0; k < 3; k++)
                for (l = 0; l < NUM_DCT_TOKENS-1; l++)
                    if (vp56_rac_get_prob_branchy(c, vp8_token_update_probs[i][j][k][l])) {
                        int prob = vp8_rac_get_uint(c, 8);
                        for (m = 0; vp8_coeff_band_indexes[j][m] >= 0; m++)
                            s->prob->token[i][vp8_coeff_band_indexes[j][m]][k][l] = prob;
                    }
}

#define VP7_MVC_SIZE 17
#define VP8_MVC_SIZE 19

static void vp78_update_pred16x16_pred8x8_mvc_probabilities(VP8Context *s,
                                                            int mvc_size)
{
    VP56RangeCoder *c = &s->c;
    int i, j;

    if (vp8_rac_get(c))
        for (i = 0; i < 4; i++)
            s->prob->pred16x16[i] = vp8_rac_get_uint(c, 8);
    if (vp8_rac_get(c))
        for (i = 0; i < 3; i++)
            s->prob->pred8x8c[i]  = vp8_rac_get_uint(c, 8);

    // 17.2 MV probability update
    for (i = 0; i < 2; i++)
        for (j = 0; j < mvc_size; j++)
            if (vp56_rac_get_prob_branchy(c, vp8_mv_update_prob[i][j]))
                s->prob->mvc[i][j] = vp8_rac_get_nn(c);
}

static void update_refs(VP8Context *s)
{
    VP56RangeCoder *c = &s->c;

    int update_golden = vp8_rac_get(c);
    int update_altref = vp8_rac_get(c);

    s->update_golden = ref_to_update(s, update_golden, VP56_FRAME_GOLDEN);
    s->update_altref = ref_to_update(s, update_altref, VP56_FRAME_GOLDEN2);
}

static void copy_chroma(AVFrame *dst, AVFrame *src, int width, int height)
{
    int i, j;

    for (j = 1; j < 3; j++) {
        for (i = 0; i < height / 2; i++)
            memcpy(dst->data[j] + i * dst->linesize[j],
                   src->data[j] + i * src->linesize[j], width / 2);
    }
}

static void fade(uint8_t *dst, ptrdiff_t dst_linesize,
                 const uint8_t *src, ptrdiff_t src_linesize,
                 int width, int height,
                 int alpha, int beta)
{
    int i, j;
    for (j = 0; j < height; j++) {
        const uint8_t *src2 = src + j * src_linesize;
        uint8_t *dst2 = dst + j * dst_linesize;
        for (i = 0; i < width; i++) {
            uint8_t y = src2[i];
            dst2[i] = av_clip_uint8(y + ((y * beta) >> 8) + alpha);
        }
    }
}

static int vp7_fade_frame(VP8Context *s, int alpha, int beta)
{
    int ret;

    if (!s->keyframe && (alpha || beta)) {
        int width  = s->mb_width * 16;
        int height = s->mb_height * 16;
        AVFrame *src, *dst;

        if (!s->framep[VP56_FRAME_PREVIOUS] ||
            !s->framep[VP56_FRAME_GOLDEN]) {
            av_log(s->avctx, AV_LOG_WARNING, "Discarding interframe without a prior keyframe!\n");
            return AVERROR_INVALIDDATA;
        }

        dst =
        src = s->framep[VP56_FRAME_PREVIOUS]->tf.f;

        /* preserve the golden frame, write a new previous frame */
        if (s->framep[VP56_FRAME_GOLDEN] == s->framep[VP56_FRAME_PREVIOUS]) {
            s->framep[VP56_FRAME_PREVIOUS] = vp8_find_free_buffer(s);
            if ((ret = vp8_alloc_frame(s, s->framep[VP56_FRAME_PREVIOUS], 1)) < 0)
                return ret;

            dst = s->framep[VP56_FRAME_PREVIOUS]->tf.f;

            copy_chroma(dst, src, width, height);
        }

        fade(dst->data[0], dst->linesize[0],
             src->data[0], src->linesize[0],
             width, height, alpha, beta);
    }

    return 0;
}

static int vp7_decode_frame_header(VP8Context *s, const uint8_t *buf, int buf_size)
{
    VP56RangeCoder *c = &s->c;
    int part1_size, hscale, vscale, i, j, ret;
    int width  = s->avctx->width;
    int height = s->avctx->height;
    int alpha = 0;
    int beta  = 0;

    if (buf_size < 4) {
        return AVERROR_INVALIDDATA;
    }

    s->profile = (buf[0] >> 1) & 7;
    if (s->profile > 1) {
        avpriv_request_sample(s->avctx, "Unknown profile %d", s->profile);
        return AVERROR_INVALIDDATA;
    }

    s->keyframe  = !(buf[0] & 1);
    s->invisible = 0;
    part1_size   = AV_RL24(buf) >> 4;

    if (buf_size < 4 - s->profile + part1_size) {
        av_log(s->avctx, AV_LOG_ERROR, "Buffer size %d is too small, needed : %d\n", buf_size, 4 - s->profile + part1_size);
        return AVERROR_INVALIDDATA;
    }

    buf      += 4 - s->profile;
    buf_size -= 4 - s->profile;

    memcpy(s->put_pixels_tab, s->vp8dsp.put_vp8_epel_pixels_tab, sizeof(s->put_pixels_tab));

    ret = ff_vp56_init_range_decoder(c, buf, part1_size);
    if (ret < 0)
        return ret;
    buf      += part1_size;
    buf_size -= part1_size;

    /* A. Dimension information (keyframes only) */
    if (s->keyframe) {
        width  = vp8_rac_get_uint(c, 12);
        height = vp8_rac_get_uint(c, 12);
        hscale = vp8_rac_get_uint(c, 2);
        vscale = vp8_rac_get_uint(c, 2);
        if (hscale || vscale)
            avpriv_request_sample(s->avctx, "Upscaling");

        s->update_golden = s->update_altref = VP56_FRAME_CURRENT;
        vp78_reset_probability_tables(s);
        memcpy(s->prob->pred16x16, vp8_pred16x16_prob_inter,
               sizeof(s->prob->pred16x16));
        memcpy(s->prob->pred8x8c, vp8_pred8x8c_prob_inter,
               sizeof(s->prob->pred8x8c));
        for (i = 0; i < 2; i++)
            memcpy(s->prob->mvc[i], vp7_mv_default_prob[i],
                   sizeof(vp7_mv_default_prob[i]));
        memset(&s->segmentation, 0, sizeof(s->segmentation));
        memset(&s->lf_delta, 0, sizeof(s->lf_delta));
        memcpy(s->prob[0].scan, ff_zigzag_scan, sizeof(s->prob[0].scan));
    }

    if (s->keyframe || s->profile > 0)
        memset(s->inter_dc_pred, 0 , sizeof(s->inter_dc_pred));

    /* B. Decoding information for all four macroblock-level features */
    for (i = 0; i < 4; i++) {
        s->feature_enabled[i] = vp8_rac_get(c);
        if (s->feature_enabled[i]) {
             s->feature_present_prob[i] = vp8_rac_get_uint(c, 8);

             for (j = 0; j < 3; j++)
                 s->feature_index_prob[i][j] =
                     vp8_rac_get(c) ? vp8_rac_get_uint(c, 8) : 255;

             if (vp7_feature_value_size[s->profile][i])
                 for (j = 0; j < 4; j++)
                     s->feature_value[i][j] =
                        vp8_rac_get(c) ? vp8_rac_get_uint(c, vp7_feature_value_size[s->profile][i]) : 0;
        }
    }

    s->segmentation.enabled    = 0;
    s->segmentation.update_map = 0;
    s->lf_delta.enabled        = 0;

    s->num_coeff_partitions = 1;
    ret = ff_vp56_init_range_decoder(&s->coeff_partition[0], buf, buf_size);
    if (ret < 0)
        return ret;

    if (!s->macroblocks_base || /* first frame */
        width != s->avctx->width || height != s->avctx->height ||
        (width + 15) / 16 != s->mb_width || (height + 15) / 16 != s->mb_height) {
        if ((ret = vp7_update_dimensions(s, width, height)) < 0)
            return ret;
    }

    /* C. Dequantization indices */
    vp7_get_quants(s);

    /* D. Golden frame update flag (a Flag) for interframes only */
    if (!s->keyframe) {
        s->update_golden = vp8_rac_get(c) ? VP56_FRAME_CURRENT : VP56_FRAME_NONE;
        s->sign_bias[VP56_FRAME_GOLDEN] = 0;
    }

    s->update_last          = 1;
    s->update_probabilities = 1;
    s->fade_present         = 1;

    if (s->profile > 0) {
        s->update_probabilities = vp8_rac_get(c);
        if (!s->update_probabilities)
            s->prob[1] = s->prob[0];

        if (!s->keyframe)
            s->fade_present = vp8_rac_get(c);
    }

    if (vpX_rac_is_end(c))
        return AVERROR_INVALIDDATA;
    /* E. Fading information for previous frame */
    if (s->fade_present && vp8_rac_get(c)) {
        alpha = (int8_t) vp8_rac_get_uint(c, 8);
        beta  = (int8_t) vp8_rac_get_uint(c, 8);
    }

    /* F. Loop filter type */
    if (!s->profile)
        s->filter.simple = vp8_rac_get(c);

    /* G. DCT coefficient ordering specification */
    if (vp8_rac_get(c))
        for (i = 1; i < 16; i++)
            s->prob[0].scan[i] = ff_zigzag_scan[vp8_rac_get_uint(c, 4)];

    /* H. Loop filter levels  */
    if (s->profile > 0)
        s->filter.simple = vp8_rac_get(c);
    s->filter.level     = vp8_rac_get_uint(c, 6);
    s->filter.sharpness = vp8_rac_get_uint(c, 3);

    /* I. DCT coefficient probability update; 13.3 Token Probability Updates */
    vp78_update_probability_tables(s);

    s->mbskip_enabled = 0;

    /* J. The remaining frame header data occurs ONLY FOR INTERFRAMES */
    if (!s->keyframe) {
        s->prob->intra  = vp8_rac_get_uint(c, 8);
        s->prob->last   = vp8_rac_get_uint(c, 8);
        vp78_update_pred16x16_pred8x8_mvc_probabilities(s, VP7_MVC_SIZE);
    }

    if (vpX_rac_is_end(c))
        return AVERROR_INVALIDDATA;

    if ((ret = vp7_fade_frame(s, alpha, beta)) < 0)
        return ret;

    return 0;
}

static int vp8_decode_frame_header(VP8Context *s, const uint8_t *buf, int buf_size)
{
    VP56RangeCoder *c = &s->c;
    int header_size, hscale, vscale, ret;
    int width  = s->avctx->width;
    int height = s->avctx->height;

    if (buf_size < 3) {
        av_log(s->avctx, AV_LOG_ERROR, "Insufficent data (%d) for header\n", buf_size);
        return AVERROR_INVALIDDATA;
    }

    s->keyframe  = !(buf[0] & 1);
    s->profile   =  (buf[0]>>1) & 7;
    s->invisible = !(buf[0] & 0x10);
    header_size  = AV_RL24(buf) >> 5;
    buf      += 3;
    buf_size -= 3;

    s->header_partition_size = header_size;

    if (s->profile > 3)
        av_log(s->avctx, AV_LOG_WARNING, "Unknown profile %d\n", s->profile);

    if (!s->profile)
        memcpy(s->put_pixels_tab, s->vp8dsp.put_vp8_epel_pixels_tab,
               sizeof(s->put_pixels_tab));
    else    // profile 1-3 use bilinear, 4+ aren't defined so whatever
        memcpy(s->put_pixels_tab, s->vp8dsp.put_vp8_bilinear_pixels_tab,
               sizeof(s->put_pixels_tab));

    if (header_size > buf_size - 7 * s->keyframe) {
        av_log(s->avctx, AV_LOG_ERROR, "Header size larger than data provided\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->keyframe) {
        if (AV_RL24(buf) != 0x2a019d) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Invalid start code 0x%x\n", AV_RL24(buf));
            return AVERROR_INVALIDDATA;
        }
        width     = AV_RL16(buf + 3) & 0x3fff;
        height    = AV_RL16(buf + 5) & 0x3fff;
        hscale    = buf[4] >> 6;
        vscale    = buf[6] >> 6;
        buf      += 7;
        buf_size -= 7;

        if (hscale || vscale)
            avpriv_request_sample(s->avctx, "Upscaling");

        s->update_golden = s->update_altref = VP56_FRAME_CURRENT;
        vp78_reset_probability_tables(s);
        memcpy(s->prob->pred16x16, vp8_pred16x16_prob_inter,
               sizeof(s->prob->pred16x16));
        memcpy(s->prob->pred8x8c, vp8_pred8x8c_prob_inter,
               sizeof(s->prob->pred8x8c));
        memcpy(s->prob->mvc, vp8_mv_default_prob,
               sizeof(s->prob->mvc));
        memset(&s->segmentation, 0, sizeof(s->segmentation));
        memset(&s->lf_delta, 0, sizeof(s->lf_delta));
    }

    ret = ff_vp56_init_range_decoder(c, buf, header_size);
    if (ret < 0)
        return ret;
    buf      += header_size;
    buf_size -= header_size;

    if (s->keyframe) {
        s->colorspace = vp8_rac_get(c);
        if (s->colorspace)
            av_log(s->avctx, AV_LOG_WARNING, "Unspecified colorspace\n");
        s->fullrange = vp8_rac_get(c);
    }

    if ((s->segmentation.enabled = vp8_rac_get(c)))
        parse_segment_info(s);
    else
        s->segmentation.update_map = 0; // FIXME: move this to some init function?

    s->filter.simple    = vp8_rac_get(c);
    s->filter.level     = vp8_rac_get_uint(c, 6);
    s->filter.sharpness = vp8_rac_get_uint(c, 3);

    if ((s->lf_delta.enabled = vp8_rac_get(c))) {
        s->lf_delta.update = vp8_rac_get(c);
        if (s->lf_delta.update)
            update_lf_deltas(s);
    }

    if (setup_partitions(s, buf, buf_size)) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid partitions\n");
        return AVERROR_INVALIDDATA;
    }

    if (!s->macroblocks_base || /* first frame */
        width != s->avctx->width || height != s->avctx->height ||
        (width+15)/16 != s->mb_width || (height+15)/16 != s->mb_height)
        if ((ret = vp8_update_dimensions(s, width, height)) < 0)
            return ret;

    vp8_get_quants(s);

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

    vp78_update_probability_tables(s);

    if ((s->mbskip_enabled = vp8_rac_get(c)))
        s->prob->mbskip = vp8_rac_get_uint(c, 8);

    if (!s->keyframe) {
        s->prob->intra  = vp8_rac_get_uint(c, 8);
        s->prob->last   = vp8_rac_get_uint(c, 8);
        s->prob->golden = vp8_rac_get_uint(c, 8);
        vp78_update_pred16x16_pred8x8_mvc_probabilities(s, VP8_MVC_SIZE);
    }

    // Record the entropy coder state here so that hwaccels can use it.
    s->c.code_word = vp56_rac_renorm(&s->c);
    s->coder_state_at_header_end.input     = s->c.buffer - (-s->c.bits / 8);
    s->coder_state_at_header_end.range     = s->c.high;
    s->coder_state_at_header_end.value     = s->c.code_word >> 16;
    s->coder_state_at_header_end.bit_count = -s->c.bits % 8;

    return 0;
}

static av_always_inline
void clamp_mv(VP8mvbounds *s, VP56mv *dst, const VP56mv *src)
{
    dst->x = av_clip(src->x, av_clip(s->mv_min.x, INT16_MIN, INT16_MAX),
                             av_clip(s->mv_max.x, INT16_MIN, INT16_MAX));
    dst->y = av_clip(src->y, av_clip(s->mv_min.y, INT16_MIN, INT16_MAX),
                             av_clip(s->mv_max.y, INT16_MIN, INT16_MAX));
}

/**
 * Motion vector coding, 17.1.
 */
static av_always_inline int read_mv_component(VP56RangeCoder *c, const uint8_t *p, int vp7)
{
    int bit, x = 0;

    if (vp56_rac_get_prob_branchy(c, p[0])) {
        int i;

        for (i = 0; i < 3; i++)
            x += vp56_rac_get_prob(c, p[9 + i]) << i;
        for (i = (vp7 ? 7 : 9); i > 3; i--)
            x += vp56_rac_get_prob(c, p[9 + i]) << i;
        if (!(x & (vp7 ? 0xF0 : 0xFFF0)) || vp56_rac_get_prob(c, p[12]))
            x += 8;
    } else {
        // small_mvtree
        const uint8_t *ps = p + 2;
        bit = vp56_rac_get_prob(c, *ps);
        ps += 1 + 3 * bit;
        x  += 4 * bit;
        bit = vp56_rac_get_prob(c, *ps);
        ps += 1 + bit;
        x  += 2 * bit;
        x  += vp56_rac_get_prob(c, *ps);
    }

    return (x && vp56_rac_get_prob(c, p[1])) ? -x : x;
}

static int vp7_read_mv_component(VP56RangeCoder *c, const uint8_t *p)
{
    return read_mv_component(c, p, 1);
}

static int vp8_read_mv_component(VP56RangeCoder *c, const uint8_t *p)
{
    return read_mv_component(c, p, 0);
}

static av_always_inline
const uint8_t *get_submv_prob(uint32_t left, uint32_t top, int is_vp7)
{
    if (is_vp7)
        return vp7_submv_prob;

    if (left == top)
        return vp8_submv_prob[4 - !!left];
    if (!top)
        return vp8_submv_prob[2];
    return vp8_submv_prob[1 - !!left];
}

/**
 * Split motion vector prediction, 16.4.
 * @returns the number of motion vectors parsed (2, 4 or 16)
 */
static av_always_inline
int decode_splitmvs(VP8Context *s, VP56RangeCoder *c, VP8Macroblock *mb,
                    int layout, int is_vp7)
{
    int part_idx;
    int n, num;
    VP8Macroblock *top_mb;
    VP8Macroblock *left_mb = &mb[-1];
    const uint8_t *mbsplits_left = vp8_mbsplits[left_mb->partitioning];
    const uint8_t *mbsplits_top, *mbsplits_cur, *firstidx;
    VP56mv *top_mv;
    VP56mv *left_mv = left_mb->bmv;
    VP56mv *cur_mv  = mb->bmv;

    if (!layout) // layout is inlined, s->mb_layout is not
        top_mb = &mb[2];
    else
        top_mb = &mb[-s->mb_width - 1];
    mbsplits_top = vp8_mbsplits[top_mb->partitioning];
    top_mv       = top_mb->bmv;

    if (vp56_rac_get_prob_branchy(c, vp8_mbsplit_prob[0])) {
        if (vp56_rac_get_prob_branchy(c, vp8_mbsplit_prob[1]))
            part_idx = VP8_SPLITMVMODE_16x8 + vp56_rac_get_prob(c, vp8_mbsplit_prob[2]);
        else
            part_idx = VP8_SPLITMVMODE_8x8;
    } else {
        part_idx = VP8_SPLITMVMODE_4x4;
    }

    num              = vp8_mbsplit_count[part_idx];
    mbsplits_cur     = vp8_mbsplits[part_idx],
    firstidx         = vp8_mbfirstidx[part_idx];
    mb->partitioning = part_idx;

    for (n = 0; n < num; n++) {
        int k = firstidx[n];
        uint32_t left, above;
        const uint8_t *submv_prob;

        if (!(k & 3))
            left = AV_RN32A(&left_mv[mbsplits_left[k + 3]]);
        else
            left = AV_RN32A(&cur_mv[mbsplits_cur[k - 1]]);
        if (k <= 3)
            above = AV_RN32A(&top_mv[mbsplits_top[k + 12]]);
        else
            above = AV_RN32A(&cur_mv[mbsplits_cur[k - 4]]);

        submv_prob = get_submv_prob(left, above, is_vp7);

        if (vp56_rac_get_prob_branchy(c, submv_prob[0])) {
            if (vp56_rac_get_prob_branchy(c, submv_prob[1])) {
                if (vp56_rac_get_prob_branchy(c, submv_prob[2])) {
                    mb->bmv[n].y = mb->mv.y +
                                   read_mv_component(c, s->prob->mvc[0], is_vp7);
                    mb->bmv[n].x = mb->mv.x +
                                   read_mv_component(c, s->prob->mvc[1], is_vp7);
                } else {
                    AV_ZERO32(&mb->bmv[n]);
                }
            } else {
                AV_WN32A(&mb->bmv[n], above);
            }
        } else {
            AV_WN32A(&mb->bmv[n], left);
        }
    }

    return num;
}

/**
 * The vp7 reference decoder uses a padding macroblock column (added to right
 * edge of the frame) to guard against illegal macroblock offsets. The
 * algorithm has bugs that permit offsets to straddle the padding column.
 * This function replicates those bugs.
 *
 * @param[out] edge_x macroblock x address
 * @param[out] edge_y macroblock y address
 *
 * @return macroblock offset legal (boolean)
 */
static int vp7_calculate_mb_offset(int mb_x, int mb_y, int mb_width,
                                   int xoffset, int yoffset, int boundary,
                                   int *edge_x, int *edge_y)
{
    int vwidth = mb_width + 1;
    int new = (mb_y + yoffset) * vwidth + mb_x + xoffset;
    if (new < boundary || new % vwidth == vwidth - 1)
        return 0;
    *edge_y = new / vwidth;
    *edge_x = new % vwidth;
    return 1;
}

static const VP56mv *get_bmv_ptr(const VP8Macroblock *mb, int subblock)
{
    return &mb->bmv[mb->mode == VP8_MVMODE_SPLIT ? vp8_mbsplits[mb->partitioning][subblock] : 0];
}

static av_always_inline
void vp7_decode_mvs(VP8Context *s, VP8Macroblock *mb,
                    int mb_x, int mb_y, int layout)
{
    VP8Macroblock *mb_edge[12];
    enum { CNT_ZERO, CNT_NEAREST, CNT_NEAR };
    enum { VP8_EDGE_TOP, VP8_EDGE_LEFT, VP8_EDGE_TOPLEFT };
    int idx = CNT_ZERO;
    VP56mv near_mv[3];
    uint8_t cnt[3] = { 0 };
    VP56RangeCoder *c = &s->c;
    int i;

    AV_ZERO32(&near_mv[0]);
    AV_ZERO32(&near_mv[1]);
    AV_ZERO32(&near_mv[2]);

    for (i = 0; i < VP7_MV_PRED_COUNT; i++) {
        const VP7MVPred * pred = &vp7_mv_pred[i];
        int edge_x, edge_y;

        if (vp7_calculate_mb_offset(mb_x, mb_y, s->mb_width, pred->xoffset,
                                    pred->yoffset, !s->profile, &edge_x, &edge_y)) {
            VP8Macroblock *edge = mb_edge[i] = (s->mb_layout == 1)
                                             ? s->macroblocks_base + 1 + edge_x +
                                               (s->mb_width + 1) * (edge_y + 1)
                                             : s->macroblocks + edge_x +
                                               (s->mb_height - edge_y - 1) * 2;
            uint32_t mv = AV_RN32A(get_bmv_ptr(edge, vp7_mv_pred[i].subblock));
            if (mv) {
                if (AV_RN32A(&near_mv[CNT_NEAREST])) {
                    if (mv == AV_RN32A(&near_mv[CNT_NEAREST])) {
                        idx = CNT_NEAREST;
                    } else if (AV_RN32A(&near_mv[CNT_NEAR])) {
                        if (mv != AV_RN32A(&near_mv[CNT_NEAR]))
                            continue;
                        idx = CNT_NEAR;
                    } else {
                        AV_WN32A(&near_mv[CNT_NEAR], mv);
                        idx = CNT_NEAR;
                    }
                } else {
                    AV_WN32A(&near_mv[CNT_NEAREST], mv);
                    idx = CNT_NEAREST;
                }
            } else {
                idx = CNT_ZERO;
            }
        } else {
            idx = CNT_ZERO;
        }
        cnt[idx] += vp7_mv_pred[i].score;
    }

    mb->partitioning = VP8_SPLITMVMODE_NONE;

    if (vp56_rac_get_prob_branchy(c, vp7_mode_contexts[cnt[CNT_ZERO]][0])) {
        mb->mode = VP8_MVMODE_MV;

        if (vp56_rac_get_prob_branchy(c, vp7_mode_contexts[cnt[CNT_NEAREST]][1])) {

            if (vp56_rac_get_prob_branchy(c, vp7_mode_contexts[cnt[CNT_NEAR]][2])) {

                if (cnt[CNT_NEAREST] > cnt[CNT_NEAR])
                    AV_WN32A(&mb->mv, cnt[CNT_ZERO] > cnt[CNT_NEAREST] ? 0 : AV_RN32A(&near_mv[CNT_NEAREST]));
                else
                    AV_WN32A(&mb->mv, cnt[CNT_ZERO] > cnt[CNT_NEAR]    ? 0 : AV_RN32A(&near_mv[CNT_NEAR]));

                if (vp56_rac_get_prob_branchy(c, vp7_mode_contexts[cnt[CNT_NEAR]][3])) {
                    mb->mode = VP8_MVMODE_SPLIT;
                    mb->mv = mb->bmv[decode_splitmvs(s, c, mb, layout, IS_VP7) - 1];
                } else {
                    mb->mv.y += vp7_read_mv_component(c, s->prob->mvc[0]);
                    mb->mv.x += vp7_read_mv_component(c, s->prob->mvc[1]);
                    mb->bmv[0] = mb->mv;
                }
            } else {
                mb->mv = near_mv[CNT_NEAR];
                mb->bmv[0] = mb->mv;
            }
        } else {
            mb->mv = near_mv[CNT_NEAREST];
            mb->bmv[0] = mb->mv;
        }
    } else {
        mb->mode = VP8_MVMODE_ZERO;
        AV_ZERO32(&mb->mv);
        mb->bmv[0] = mb->mv;
    }
}

static av_always_inline
void vp8_decode_mvs(VP8Context *s, VP8mvbounds *mv_bounds, VP8Macroblock *mb,
                    int mb_x, int mb_y, int layout)
{
    VP8Macroblock *mb_edge[3] = { 0      /* top */,
                                  mb - 1 /* left */,
                                  0      /* top-left */ };
    enum { CNT_ZERO, CNT_NEAREST, CNT_NEAR, CNT_SPLITMV };
    enum { VP8_EDGE_TOP, VP8_EDGE_LEFT, VP8_EDGE_TOPLEFT };
    int idx = CNT_ZERO;
    int cur_sign_bias = s->sign_bias[mb->ref_frame];
    int8_t *sign_bias = s->sign_bias;
    VP56mv near_mv[4];
    uint8_t cnt[4] = { 0 };
    VP56RangeCoder *c = &s->c;

    if (!layout) { // layout is inlined (s->mb_layout is not)
        mb_edge[0] = mb + 2;
        mb_edge[2] = mb + 1;
    } else {
        mb_edge[0] = mb - s->mb_width - 1;
        mb_edge[2] = mb - s->mb_width - 2;
    }

    AV_ZERO32(&near_mv[0]);
    AV_ZERO32(&near_mv[1]);
    AV_ZERO32(&near_mv[2]);

    /* Process MB on top, left and top-left */
#define MV_EDGE_CHECK(n)                                                      \
    {                                                                         \
        VP8Macroblock *edge = mb_edge[n];                                     \
        int edge_ref = edge->ref_frame;                                       \
        if (edge_ref != VP56_FRAME_CURRENT) {                                 \
            uint32_t mv = AV_RN32A(&edge->mv);                                \
            if (mv) {                                                         \
                if (cur_sign_bias != sign_bias[edge_ref]) {                   \
                    /* SWAR negate of the values in mv. */                    \
                    mv = ~mv;                                                 \
                    mv = ((mv & 0x7fff7fff) +                                 \
                          0x00010001) ^ (mv & 0x80008000);                    \
                }                                                             \
                if (!n || mv != AV_RN32A(&near_mv[idx]))                      \
                    AV_WN32A(&near_mv[++idx], mv);                            \
                cnt[idx] += 1 + (n != 2);                                     \
            } else                                                            \
                cnt[CNT_ZERO] += 1 + (n != 2);                                \
        }                                                                     \
    }

    MV_EDGE_CHECK(0)
    MV_EDGE_CHECK(1)
    MV_EDGE_CHECK(2)

    mb->partitioning = VP8_SPLITMVMODE_NONE;
    if (vp56_rac_get_prob_branchy(c, vp8_mode_contexts[cnt[CNT_ZERO]][0])) {
        mb->mode = VP8_MVMODE_MV;

        /* If we have three distinct MVs, merge first and last if they're the same */
        if (cnt[CNT_SPLITMV] &&
            AV_RN32A(&near_mv[1 + VP8_EDGE_TOP]) == AV_RN32A(&near_mv[1 + VP8_EDGE_TOPLEFT]))
            cnt[CNT_NEAREST] += 1;

        /* Swap near and nearest if necessary */
        if (cnt[CNT_NEAR] > cnt[CNT_NEAREST]) {
            FFSWAP(uint8_t,     cnt[CNT_NEAREST],     cnt[CNT_NEAR]);
            FFSWAP( VP56mv, near_mv[CNT_NEAREST], near_mv[CNT_NEAR]);
        }

        if (vp56_rac_get_prob_branchy(c, vp8_mode_contexts[cnt[CNT_NEAREST]][1])) {
            if (vp56_rac_get_prob_branchy(c, vp8_mode_contexts[cnt[CNT_NEAR]][2])) {
                /* Choose the best mv out of 0,0 and the nearest mv */
                clamp_mv(mv_bounds, &mb->mv, &near_mv[CNT_ZERO + (cnt[CNT_NEAREST] >= cnt[CNT_ZERO])]);
                cnt[CNT_SPLITMV] = ((mb_edge[VP8_EDGE_LEFT]->mode    == VP8_MVMODE_SPLIT) +
                                    (mb_edge[VP8_EDGE_TOP]->mode     == VP8_MVMODE_SPLIT)) * 2 +
                                    (mb_edge[VP8_EDGE_TOPLEFT]->mode == VP8_MVMODE_SPLIT);

                if (vp56_rac_get_prob_branchy(c, vp8_mode_contexts[cnt[CNT_SPLITMV]][3])) {
                    mb->mode = VP8_MVMODE_SPLIT;
                    mb->mv = mb->bmv[decode_splitmvs(s, c, mb, layout, IS_VP8) - 1];
                } else {
                    mb->mv.y  += vp8_read_mv_component(c, s->prob->mvc[0]);
                    mb->mv.x  += vp8_read_mv_component(c, s->prob->mvc[1]);
                    mb->bmv[0] = mb->mv;
                }
            } else {
                clamp_mv(mv_bounds, &mb->mv, &near_mv[CNT_NEAR]);
                mb->bmv[0] = mb->mv;
            }
        } else {
            clamp_mv(mv_bounds, &mb->mv, &near_mv[CNT_NEAREST]);
            mb->bmv[0] = mb->mv;
        }
    } else {
        mb->mode = VP8_MVMODE_ZERO;
        AV_ZERO32(&mb->mv);
        mb->bmv[0] = mb->mv;
    }
}

static av_always_inline
void decode_intra4x4_modes(VP8Context *s, VP56RangeCoder *c, VP8Macroblock *mb,
                           int mb_x, int keyframe, int layout)
{
    uint8_t *intra4x4 = mb->intra4x4_pred_mode_mb;

    if (layout) {
        VP8Macroblock *mb_top = mb - s->mb_width - 1;
        memcpy(mb->intra4x4_pred_mode_top, mb_top->intra4x4_pred_mode_top, 4);
    }
    if (keyframe) {
        int x, y;
        uint8_t *top;
        uint8_t *const left = s->intra4x4_pred_mode_left;
        if (layout)
            top = mb->intra4x4_pred_mode_top;
        else
            top = s->intra4x4_pred_mode_top + 4 * mb_x;
        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                const uint8_t *ctx;
                ctx       = vp8_pred4x4_prob_intra[top[x]][left[y]];
                *intra4x4 = vp8_rac_get_tree(c, vp8_pred4x4_tree, ctx);
                left[y]   = top[x] = *intra4x4;
                intra4x4++;
            }
        }
    } else {
        int i;
        for (i = 0; i < 16; i++)
            intra4x4[i] = vp8_rac_get_tree(c, vp8_pred4x4_tree,
                                           vp8_pred4x4_prob_inter);
    }
}

static av_always_inline
void decode_mb_mode(VP8Context *s, VP8mvbounds *mv_bounds,
                    VP8Macroblock *mb, int mb_x, int mb_y,
                    uint8_t *segment, uint8_t *ref, int layout, int is_vp7)
{
    VP56RangeCoder *c = &s->c;
    static const char * const vp7_feature_name[] = { "q-index",
                                                     "lf-delta",
                                                     "partial-golden-update",
                                                     "blit-pitch" };
    if (is_vp7) {
        int i;
        *segment = 0;
        for (i = 0; i < 4; i++) {
            if (s->feature_enabled[i]) {
                if (vp56_rac_get_prob_branchy(c, s->feature_present_prob[i])) {
                      int index = vp8_rac_get_tree(c, vp7_feature_index_tree,
                                                   s->feature_index_prob[i]);
                      av_log(s->avctx, AV_LOG_WARNING,
                             "Feature %s present in macroblock (value 0x%x)\n",
                             vp7_feature_name[i], s->feature_value[i][index]);
                }
           }
        }
    } else if (s->segmentation.update_map) {
        int bit  = vp56_rac_get_prob(c, s->prob->segmentid[0]);
        *segment = vp56_rac_get_prob(c, s->prob->segmentid[1+bit]) + 2*bit;
    } else if (s->segmentation.enabled)
        *segment = ref ? *ref : *segment;
    mb->segment = *segment;

    mb->skip = s->mbskip_enabled ? vp56_rac_get_prob(c, s->prob->mbskip) : 0;

    if (s->keyframe) {
        mb->mode = vp8_rac_get_tree(c, vp8_pred16x16_tree_intra,
                                    vp8_pred16x16_prob_intra);

        if (mb->mode == MODE_I4x4) {
            decode_intra4x4_modes(s, c, mb, mb_x, 1, layout);
        } else {
            const uint32_t modes = (is_vp7 ? vp7_pred4x4_mode
                                           : vp8_pred4x4_mode)[mb->mode] * 0x01010101u;
            if (s->mb_layout)
                AV_WN32A(mb->intra4x4_pred_mode_top, modes);
            else
                AV_WN32A(s->intra4x4_pred_mode_top + 4 * mb_x, modes);
            AV_WN32A(s->intra4x4_pred_mode_left, modes);
        }

        mb->chroma_pred_mode = vp8_rac_get_tree(c, vp8_pred8x8c_tree,
                                                vp8_pred8x8c_prob_intra);
        mb->ref_frame        = VP56_FRAME_CURRENT;
    } else if (vp56_rac_get_prob_branchy(c, s->prob->intra)) {
        // inter MB, 16.2
        if (vp56_rac_get_prob_branchy(c, s->prob->last))
            mb->ref_frame =
                (!is_vp7 && vp56_rac_get_prob(c, s->prob->golden)) ? VP56_FRAME_GOLDEN2 /* altref */
                                                                   : VP56_FRAME_GOLDEN;
        else
            mb->ref_frame = VP56_FRAME_PREVIOUS;
        s->ref_count[mb->ref_frame - 1]++;

        // motion vectors, 16.3
        if (is_vp7)
            vp7_decode_mvs(s, mb, mb_x, mb_y, layout);
        else
            vp8_decode_mvs(s, mv_bounds, mb, mb_x, mb_y, layout);
    } else {
        // intra MB, 16.1
        mb->mode = vp8_rac_get_tree(c, vp8_pred16x16_tree_inter, s->prob->pred16x16);

        if (mb->mode == MODE_I4x4)
            decode_intra4x4_modes(s, c, mb, mb_x, 0, layout);

        mb->chroma_pred_mode = vp8_rac_get_tree(c, vp8_pred8x8c_tree,
                                                s->prob->pred8x8c);
        mb->ref_frame        = VP56_FRAME_CURRENT;
        mb->partitioning     = VP8_SPLITMVMODE_NONE;
        AV_ZERO32(&mb->bmv[0]);
    }
}

/**
 * @param r     arithmetic bitstream reader context
 * @param block destination for block coefficients
 * @param probs probabilities to use when reading trees from the bitstream
 * @param i     initial coeff index, 0 unless a separate DC block is coded
 * @param qmul  array holding the dc/ac dequant factor at position 0/1
 *
 * @return 0 if no coeffs were decoded
 *         otherwise, the index of the last coeff decoded plus one
 */
static av_always_inline
int decode_block_coeffs_internal(VP56RangeCoder *r, int16_t block[16],
                                 uint8_t probs[16][3][NUM_DCT_TOKENS - 1],
                                 int i, uint8_t *token_prob, int16_t qmul[2],
                                 const uint8_t scan[16], int vp7)
{
    VP56RangeCoder c = *r;
    goto skip_eob;
    do {
        int coeff;
restart:
        if (!vp56_rac_get_prob_branchy(&c, token_prob[0]))   // DCT_EOB
            break;

skip_eob:
        if (!vp56_rac_get_prob_branchy(&c, token_prob[1])) { // DCT_0
            if (++i == 16)
                break; // invalid input; blocks should end with EOB
            token_prob = probs[i][0];
            if (vp7)
                goto restart;
            goto skip_eob;
        }

        if (!vp56_rac_get_prob_branchy(&c, token_prob[2])) { // DCT_1
            coeff = 1;
            token_prob = probs[i + 1][1];
        } else {
            if (!vp56_rac_get_prob_branchy(&c, token_prob[3])) { // DCT 2,3,4
                coeff = vp56_rac_get_prob_branchy(&c, token_prob[4]);
                if (coeff)
                    coeff += vp56_rac_get_prob(&c, token_prob[5]);
                coeff += 2;
            } else {
                // DCT_CAT*
                if (!vp56_rac_get_prob_branchy(&c, token_prob[6])) {
                    if (!vp56_rac_get_prob_branchy(&c, token_prob[7])) { // DCT_CAT1
                        coeff = 5 + vp56_rac_get_prob(&c, vp8_dct_cat1_prob[0]);
                    } else {                                    // DCT_CAT2
                        coeff  = 7;
                        coeff += vp56_rac_get_prob(&c, vp8_dct_cat2_prob[0]) << 1;
                        coeff += vp56_rac_get_prob(&c, vp8_dct_cat2_prob[1]);
                    }
                } else {    // DCT_CAT3 and up
                    int a   = vp56_rac_get_prob(&c, token_prob[8]);
                    int b   = vp56_rac_get_prob(&c, token_prob[9 + a]);
                    int cat = (a << 1) + b;
                    coeff  = 3 + (8 << cat);
                    coeff += vp8_rac_get_coeff(&c, ff_vp8_dct_cat_prob[cat]);
                }
            }
            token_prob = probs[i + 1][2];
        }
        block[scan[i]] = (vp8_rac_get(&c) ? -coeff : coeff) * qmul[!!i];
    } while (++i < 16);

    *r = c;
    return i;
}

static av_always_inline
int inter_predict_dc(int16_t block[16], int16_t pred[2])
{
    int16_t dc = block[0];
    int ret = 0;

    if (pred[1] > 3) {
        dc += pred[0];
        ret = 1;
    }

    if (!pred[0] | !dc | ((int32_t)pred[0] ^ (int32_t)dc) >> 31) {
        block[0] = pred[0] = dc;
        pred[1] = 0;
    } else {
        if (pred[0] == dc)
            pred[1]++;
        block[0] = pred[0] = dc;
    }

    return ret;
}

static int vp7_decode_block_coeffs_internal(VP56RangeCoder *r,
                                            int16_t block[16],
                                            uint8_t probs[16][3][NUM_DCT_TOKENS - 1],
                                            int i, uint8_t *token_prob,
                                            int16_t qmul[2],
                                            const uint8_t scan[16])
{
    return decode_block_coeffs_internal(r, block, probs, i,
                                        token_prob, qmul, scan, IS_VP7);
}

#ifndef vp8_decode_block_coeffs_internal
static int vp8_decode_block_coeffs_internal(VP56RangeCoder *r,
                                            int16_t block[16],
                                            uint8_t probs[16][3][NUM_DCT_TOKENS - 1],
                                            int i, uint8_t *token_prob,
                                            int16_t qmul[2])
{
    return decode_block_coeffs_internal(r, block, probs, i,
                                        token_prob, qmul, ff_zigzag_scan, IS_VP8);
}
#endif

/**
 * @param c          arithmetic bitstream reader context
 * @param block      destination for block coefficients
 * @param probs      probabilities to use when reading trees from the bitstream
 * @param i          initial coeff index, 0 unless a separate DC block is coded
 * @param zero_nhood the initial prediction context for number of surrounding
 *                   all-zero blocks (only left/top, so 0-2)
 * @param qmul       array holding the dc/ac dequant factor at position 0/1
 * @param scan       scan pattern (VP7 only)
 *
 * @return 0 if no coeffs were decoded
 *         otherwise, the index of the last coeff decoded plus one
 */
static av_always_inline
int decode_block_coeffs(VP56RangeCoder *c, int16_t block[16],
                        uint8_t probs[16][3][NUM_DCT_TOKENS - 1],
                        int i, int zero_nhood, int16_t qmul[2],
                        const uint8_t scan[16], int vp7)
{
    uint8_t *token_prob = probs[i][zero_nhood];
    if (!vp56_rac_get_prob_branchy(c, token_prob[0]))   // DCT_EOB
        return 0;
    return vp7 ? vp7_decode_block_coeffs_internal(c, block, probs, i,
                                                  token_prob, qmul, scan)
               : vp8_decode_block_coeffs_internal(c, block, probs, i,
                                                  token_prob, qmul);
}

static av_always_inline
void decode_mb_coeffs(VP8Context *s, VP8ThreadData *td, VP56RangeCoder *c,
                      VP8Macroblock *mb, uint8_t t_nnz[9], uint8_t l_nnz[9],
                      int is_vp7)
{
    int i, x, y, luma_start = 0, luma_ctx = 3;
    int nnz_pred, nnz, nnz_total = 0;
    int segment = mb->segment;
    int block_dc = 0;

    if (mb->mode != MODE_I4x4 && (is_vp7 || mb->mode != VP8_MVMODE_SPLIT)) {
        nnz_pred = t_nnz[8] + l_nnz[8];

        // decode DC values and do hadamard
        nnz = decode_block_coeffs(c, td->block_dc, s->prob->token[1], 0,
                                  nnz_pred, s->qmat[segment].luma_dc_qmul,
                                  ff_zigzag_scan, is_vp7);
        l_nnz[8] = t_nnz[8] = !!nnz;

        if (is_vp7 && mb->mode > MODE_I4x4) {
            nnz |=  inter_predict_dc(td->block_dc,
                                     s->inter_dc_pred[mb->ref_frame - 1]);
        }

        if (nnz) {
            nnz_total += nnz;
            block_dc   = 1;
            if (nnz == 1)
                s->vp8dsp.vp8_luma_dc_wht_dc(td->block, td->block_dc);
            else
                s->vp8dsp.vp8_luma_dc_wht(td->block, td->block_dc);
        }
        luma_start = 1;
        luma_ctx   = 0;
    }

    // luma blocks
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++) {
            nnz_pred = l_nnz[y] + t_nnz[x];
            nnz = decode_block_coeffs(c, td->block[y][x],
                                      s->prob->token[luma_ctx],
                                      luma_start, nnz_pred,
                                      s->qmat[segment].luma_qmul,
                                      s->prob[0].scan, is_vp7);
            /* nnz+block_dc may be one more than the actual last index,
             * but we don't care */
            td->non_zero_count_cache[y][x] = nnz + block_dc;
            t_nnz[x] = l_nnz[y] = !!nnz;
            nnz_total += nnz;
        }

    // chroma blocks
    // TODO: what to do about dimensions? 2nd dim for luma is x,
    // but for chroma it's (y<<1)|x
    for (i = 4; i < 6; i++)
        for (y = 0; y < 2; y++)
            for (x = 0; x < 2; x++) {
                nnz_pred = l_nnz[i + 2 * y] + t_nnz[i + 2 * x];
                nnz = decode_block_coeffs(c, td->block[i][(y << 1) + x],
                                          s->prob->token[2], 0, nnz_pred,
                                          s->qmat[segment].chroma_qmul,
                                          s->prob[0].scan, is_vp7);
                td->non_zero_count_cache[i][(y << 1) + x] = nnz;
                t_nnz[i + 2 * x] = l_nnz[i + 2 * y] = !!nnz;
                nnz_total += nnz;
            }

    // if there were no coded coeffs despite the macroblock not being marked skip,
    // we MUST not do the inner loop filter and should not do IDCT
    // Since skip isn't used for bitstream prediction, just manually set it.
    if (!nnz_total)
        mb->skip = 1;
}

static av_always_inline
void backup_mb_border(uint8_t *top_border, uint8_t *src_y,
                      uint8_t *src_cb, uint8_t *src_cr,
                      ptrdiff_t linesize, ptrdiff_t uvlinesize, int simple)
{
    AV_COPY128(top_border, src_y + 15 * linesize);
    if (!simple) {
        AV_COPY64(top_border + 16, src_cb + 7 * uvlinesize);
        AV_COPY64(top_border + 24, src_cr + 7 * uvlinesize);
    }
}

static av_always_inline
void xchg_mb_border(uint8_t *top_border, uint8_t *src_y, uint8_t *src_cb,
                    uint8_t *src_cr, ptrdiff_t linesize, ptrdiff_t uvlinesize, int mb_x,
                    int mb_y, int mb_width, int simple, int xchg)
{
    uint8_t *top_border_m1 = top_border - 32;     // for TL prediction
    src_y  -= linesize;
    src_cb -= uvlinesize;
    src_cr -= uvlinesize;

#define XCHG(a, b, xchg)                                                      \
    do {                                                                      \
        if (xchg)                                                             \
            AV_SWAP64(b, a);                                                  \
        else                                                                  \
            AV_COPY64(b, a);                                                  \
    } while (0)

    XCHG(top_border_m1 + 8, src_y - 8, xchg);
    XCHG(top_border, src_y, xchg);
    XCHG(top_border + 8, src_y + 8, 1);
    if (mb_x < mb_width - 1)
        XCHG(top_border + 32, src_y + 16, 1);

    // only copy chroma for normal loop filter
    // or to initialize the top row to 127
    if (!simple || !mb_y) {
        XCHG(top_border_m1 + 16, src_cb - 8, xchg);
        XCHG(top_border_m1 + 24, src_cr - 8, xchg);
        XCHG(top_border + 16, src_cb, 1);
        XCHG(top_border + 24, src_cr, 1);
    }
}

static av_always_inline
int check_dc_pred8x8_mode(int mode, int mb_x, int mb_y)
{
    if (!mb_x)
        return mb_y ? TOP_DC_PRED8x8 : DC_128_PRED8x8;
    else
        return mb_y ? mode : LEFT_DC_PRED8x8;
}

static av_always_inline
int check_tm_pred8x8_mode(int mode, int mb_x, int mb_y, int vp7)
{
    if (!mb_x)
        return mb_y ? VERT_PRED8x8 : (vp7 ? DC_128_PRED8x8 : DC_129_PRED8x8);
    else
        return mb_y ? mode : HOR_PRED8x8;
}

static av_always_inline
int check_intra_pred8x8_mode_emuedge(int mode, int mb_x, int mb_y, int vp7)
{
    switch (mode) {
    case DC_PRED8x8:
        return check_dc_pred8x8_mode(mode, mb_x, mb_y);
    case VERT_PRED8x8:
        return !mb_y ? (vp7 ? DC_128_PRED8x8 : DC_127_PRED8x8) : mode;
    case HOR_PRED8x8:
        return !mb_x ? (vp7 ? DC_128_PRED8x8 : DC_129_PRED8x8) : mode;
    case PLANE_PRED8x8: /* TM */
        return check_tm_pred8x8_mode(mode, mb_x, mb_y, vp7);
    }
    return mode;
}

static av_always_inline
int check_tm_pred4x4_mode(int mode, int mb_x, int mb_y, int vp7)
{
    if (!mb_x) {
        return mb_y ? VERT_VP8_PRED : (vp7 ? DC_128_PRED : DC_129_PRED);
    } else {
        return mb_y ? mode : HOR_VP8_PRED;
    }
}

static av_always_inline
int check_intra_pred4x4_mode_emuedge(int mode, int mb_x, int mb_y,
                                     int *copy_buf, int vp7)
{
    switch (mode) {
    case VERT_PRED:
        if (!mb_x && mb_y) {
            *copy_buf = 1;
            return mode;
        }
        /* fall-through */
    case DIAG_DOWN_LEFT_PRED:
    case VERT_LEFT_PRED:
        return !mb_y ? (vp7 ? DC_128_PRED : DC_127_PRED) : mode;
    case HOR_PRED:
        if (!mb_y) {
            *copy_buf = 1;
            return mode;
        }
        /* fall-through */
    case HOR_UP_PRED:
        return !mb_x ? (vp7 ? DC_128_PRED : DC_129_PRED) : mode;
    case TM_VP8_PRED:
        return check_tm_pred4x4_mode(mode, mb_x, mb_y, vp7);
    case DC_PRED: /* 4x4 DC doesn't use the same "H.264-style" exceptions
                   * as 16x16/8x8 DC */
    case DIAG_DOWN_RIGHT_PRED:
    case VERT_RIGHT_PRED:
    case HOR_DOWN_PRED:
        if (!mb_y || !mb_x)
            *copy_buf = 1;
        return mode;
    }
    return mode;
}

static av_always_inline
void intra_predict(VP8Context *s, VP8ThreadData *td, uint8_t *dst[3],
                   VP8Macroblock *mb, int mb_x, int mb_y, int is_vp7)
{
    int x, y, mode, nnz;
    uint32_t tr;

    /* for the first row, we need to run xchg_mb_border to init the top edge
     * to 127 otherwise, skip it if we aren't going to deblock */
    if (mb_y && (s->deblock_filter || !mb_y) && td->thread_nr == 0)
        xchg_mb_border(s->top_border[mb_x + 1], dst[0], dst[1], dst[2],
                       s->linesize, s->uvlinesize, mb_x, mb_y, s->mb_width,
                       s->filter.simple, 1);

    if (mb->mode < MODE_I4x4) {
        mode = check_intra_pred8x8_mode_emuedge(mb->mode, mb_x, mb_y, is_vp7);
        s->hpc.pred16x16[mode](dst[0], s->linesize);
    } else {
        uint8_t *ptr = dst[0];
        uint8_t *intra4x4 = mb->intra4x4_pred_mode_mb;
        const uint8_t lo = is_vp7 ? 128 : 127;
        const uint8_t hi = is_vp7 ? 128 : 129;
        uint8_t tr_top[4] = { lo, lo, lo, lo };

        // all blocks on the right edge of the macroblock use bottom edge
        // the top macroblock for their topright edge
        uint8_t *tr_right = ptr - s->linesize + 16;

        // if we're on the right edge of the frame, said edge is extended
        // from the top macroblock
        if (mb_y && mb_x == s->mb_width - 1) {
            tr       = tr_right[-1] * 0x01010101u;
            tr_right = (uint8_t *) &tr;
        }

        if (mb->skip)
            AV_ZERO128(td->non_zero_count_cache);

        for (y = 0; y < 4; y++) {
            uint8_t *topright = ptr + 4 - s->linesize;
            for (x = 0; x < 4; x++) {
                int copy = 0;
                ptrdiff_t linesize = s->linesize;
                uint8_t *dst = ptr + 4 * x;
                LOCAL_ALIGNED(4, uint8_t, copy_dst, [5 * 8]);

                if ((y == 0 || x == 3) && mb_y == 0) {
                    topright = tr_top;
                } else if (x == 3)
                    topright = tr_right;

                mode = check_intra_pred4x4_mode_emuedge(intra4x4[x], mb_x + x,
                                                        mb_y + y, &copy, is_vp7);
                if (copy) {
                    dst      = copy_dst + 12;
                    linesize = 8;
                    if (!(mb_y + y)) {
                        copy_dst[3] = lo;
                        AV_WN32A(copy_dst + 4, lo * 0x01010101U);
                    } else {
                        AV_COPY32(copy_dst + 4, ptr + 4 * x - s->linesize);
                        if (!(mb_x + x)) {
                            copy_dst[3] = hi;
                        } else {
                            copy_dst[3] = ptr[4 * x - s->linesize - 1];
                        }
                    }
                    if (!(mb_x + x)) {
                        copy_dst[11] =
                        copy_dst[19] =
                        copy_dst[27] =
                        copy_dst[35] = hi;
                    } else {
                        copy_dst[11] = ptr[4 * x                   - 1];
                        copy_dst[19] = ptr[4 * x + s->linesize     - 1];
                        copy_dst[27] = ptr[4 * x + s->linesize * 2 - 1];
                        copy_dst[35] = ptr[4 * x + s->linesize * 3 - 1];
                    }
                }
                s->hpc.pred4x4[mode](dst, topright, linesize);
                if (copy) {
                    AV_COPY32(ptr + 4 * x,                   copy_dst + 12);
                    AV_COPY32(ptr + 4 * x + s->linesize,     copy_dst + 20);
                    AV_COPY32(ptr + 4 * x + s->linesize * 2, copy_dst + 28);
                    AV_COPY32(ptr + 4 * x + s->linesize * 3, copy_dst + 36);
                }

                nnz = td->non_zero_count_cache[y][x];
                if (nnz) {
                    if (nnz == 1)
                        s->vp8dsp.vp8_idct_dc_add(ptr + 4 * x,
                                                  td->block[y][x], s->linesize);
                    else
                        s->vp8dsp.vp8_idct_add(ptr + 4 * x,
                                               td->block[y][x], s->linesize);
                }
                topright += 4;
            }

            ptr      += 4 * s->linesize;
            intra4x4 += 4;
        }
    }

    mode = check_intra_pred8x8_mode_emuedge(mb->chroma_pred_mode,
                                            mb_x, mb_y, is_vp7);
    s->hpc.pred8x8[mode](dst[1], s->uvlinesize);
    s->hpc.pred8x8[mode](dst[2], s->uvlinesize);

    if (mb_y && (s->deblock_filter || !mb_y) && td->thread_nr == 0)
        xchg_mb_border(s->top_border[mb_x + 1], dst[0], dst[1], dst[2],
                       s->linesize, s->uvlinesize, mb_x, mb_y, s->mb_width,
                       s->filter.simple, 0);
}

static const uint8_t subpel_idx[3][8] = {
    { 0, 1, 2, 1, 2, 1, 2, 1 }, // nr. of left extra pixels,
                                // also function pointer index
    { 0, 3, 5, 3, 5, 3, 5, 3 }, // nr. of extra pixels required
    { 0, 2, 3, 2, 3, 2, 3, 2 }, // nr. of right extra pixels
};

/**
 * luma MC function
 *
 * @param s        VP8 decoding context
 * @param dst      target buffer for block data at block position
 * @param ref      reference picture buffer at origin (0, 0)
 * @param mv       motion vector (relative to block position) to get pixel data from
 * @param x_off    horizontal position of block from origin (0, 0)
 * @param y_off    vertical position of block from origin (0, 0)
 * @param block_w  width of block (16, 8 or 4)
 * @param block_h  height of block (always same as block_w)
 * @param width    width of src/dst plane data
 * @param height   height of src/dst plane data
 * @param linesize size of a single line of plane data, including padding
 * @param mc_func  motion compensation function pointers (bilinear or sixtap MC)
 */
static av_always_inline
void vp8_mc_luma(VP8Context *s, VP8ThreadData *td, uint8_t *dst,
                 ThreadFrame *ref, const VP56mv *mv,
                 int x_off, int y_off, int block_w, int block_h,
                 int width, int height, ptrdiff_t linesize,
                 vp8_mc_func mc_func[3][3])
{
    uint8_t *src = ref->f->data[0];

    if (AV_RN32A(mv)) {
        ptrdiff_t src_linesize = linesize;

        int mx = (mv->x * 2) & 7, mx_idx = subpel_idx[0][mx];
        int my = (mv->y * 2) & 7, my_idx = subpel_idx[0][my];

        x_off += mv->x >> 2;
        y_off += mv->y >> 2;

        // edge emulation
        ff_thread_await_progress(ref, (3 + y_off + block_h + subpel_idx[2][my]) >> 4, 0);
        src += y_off * linesize + x_off;
        if (x_off < mx_idx || x_off >= width  - block_w - subpel_idx[2][mx] ||
            y_off < my_idx || y_off >= height - block_h - subpel_idx[2][my]) {
            s->vdsp.emulated_edge_mc(td->edge_emu_buffer,
                                     src - my_idx * linesize - mx_idx,
                                     EDGE_EMU_LINESIZE, linesize,
                                     block_w + subpel_idx[1][mx],
                                     block_h + subpel_idx[1][my],
                                     x_off - mx_idx, y_off - my_idx,
                                     width, height);
            src = td->edge_emu_buffer + mx_idx + EDGE_EMU_LINESIZE * my_idx;
            src_linesize = EDGE_EMU_LINESIZE;
        }
        mc_func[my_idx][mx_idx](dst, linesize, src, src_linesize, block_h, mx, my);
    } else {
        ff_thread_await_progress(ref, (3 + y_off + block_h) >> 4, 0);
        mc_func[0][0](dst, linesize, src + y_off * linesize + x_off,
                      linesize, block_h, 0, 0);
    }
}

/**
 * chroma MC function
 *
 * @param s        VP8 decoding context
 * @param dst1     target buffer for block data at block position (U plane)
 * @param dst2     target buffer for block data at block position (V plane)
 * @param ref      reference picture buffer at origin (0, 0)
 * @param mv       motion vector (relative to block position) to get pixel data from
 * @param x_off    horizontal position of block from origin (0, 0)
 * @param y_off    vertical position of block from origin (0, 0)
 * @param block_w  width of block (16, 8 or 4)
 * @param block_h  height of block (always same as block_w)
 * @param width    width of src/dst plane data
 * @param height   height of src/dst plane data
 * @param linesize size of a single line of plane data, including padding
 * @param mc_func  motion compensation function pointers (bilinear or sixtap MC)
 */
static av_always_inline
void vp8_mc_chroma(VP8Context *s, VP8ThreadData *td, uint8_t *dst1,
                   uint8_t *dst2, ThreadFrame *ref, const VP56mv *mv,
                   int x_off, int y_off, int block_w, int block_h,
                   int width, int height, ptrdiff_t linesize,
                   vp8_mc_func mc_func[3][3])
{
    uint8_t *src1 = ref->f->data[1], *src2 = ref->f->data[2];

    if (AV_RN32A(mv)) {
        int mx = mv->x & 7, mx_idx = subpel_idx[0][mx];
        int my = mv->y & 7, my_idx = subpel_idx[0][my];

        x_off += mv->x >> 3;
        y_off += mv->y >> 3;

        // edge emulation
        src1 += y_off * linesize + x_off;
        src2 += y_off * linesize + x_off;
        ff_thread_await_progress(ref, (3 + y_off + block_h + subpel_idx[2][my]) >> 3, 0);
        if (x_off < mx_idx || x_off >= width  - block_w - subpel_idx[2][mx] ||
            y_off < my_idx || y_off >= height - block_h - subpel_idx[2][my]) {
            s->vdsp.emulated_edge_mc(td->edge_emu_buffer,
                                     src1 - my_idx * linesize - mx_idx,
                                     EDGE_EMU_LINESIZE, linesize,
                                     block_w + subpel_idx[1][mx],
                                     block_h + subpel_idx[1][my],
                                     x_off - mx_idx, y_off - my_idx, width, height);
            src1 = td->edge_emu_buffer + mx_idx + EDGE_EMU_LINESIZE * my_idx;
            mc_func[my_idx][mx_idx](dst1, linesize, src1, EDGE_EMU_LINESIZE, block_h, mx, my);

            s->vdsp.emulated_edge_mc(td->edge_emu_buffer,
                                     src2 - my_idx * linesize - mx_idx,
                                     EDGE_EMU_LINESIZE, linesize,
                                     block_w + subpel_idx[1][mx],
                                     block_h + subpel_idx[1][my],
                                     x_off - mx_idx, y_off - my_idx, width, height);
            src2 = td->edge_emu_buffer + mx_idx + EDGE_EMU_LINESIZE * my_idx;
            mc_func[my_idx][mx_idx](dst2, linesize, src2, EDGE_EMU_LINESIZE, block_h, mx, my);
        } else {
            mc_func[my_idx][mx_idx](dst1, linesize, src1, linesize, block_h, mx, my);
            mc_func[my_idx][mx_idx](dst2, linesize, src2, linesize, block_h, mx, my);
        }
    } else {
        ff_thread_await_progress(ref, (3 + y_off + block_h) >> 3, 0);
        mc_func[0][0](dst1, linesize, src1 + y_off * linesize + x_off, linesize, block_h, 0, 0);
        mc_func[0][0](dst2, linesize, src2 + y_off * linesize + x_off, linesize, block_h, 0, 0);
    }
}

static av_always_inline
void vp8_mc_part(VP8Context *s, VP8ThreadData *td, uint8_t *dst[3],
                 ThreadFrame *ref_frame, int x_off, int y_off,
                 int bx_off, int by_off, int block_w, int block_h,
                 int width, int height, VP56mv *mv)
{
    VP56mv uvmv = *mv;

    /* Y */
    vp8_mc_luma(s, td, dst[0] + by_off * s->linesize + bx_off,
                ref_frame, mv, x_off + bx_off, y_off + by_off,
                block_w, block_h, width, height, s->linesize,
                s->put_pixels_tab[block_w == 8]);

    /* U/V */
    if (s->profile == 3) {
        /* this block only applies VP8; it is safe to check
         * only the profile, as VP7 profile <= 1 */
        uvmv.x &= ~7;
        uvmv.y &= ~7;
    }
    x_off   >>= 1;
    y_off   >>= 1;
    bx_off  >>= 1;
    by_off  >>= 1;
    width   >>= 1;
    height  >>= 1;
    block_w >>= 1;
    block_h >>= 1;
    vp8_mc_chroma(s, td, dst[1] + by_off * s->uvlinesize + bx_off,
                  dst[2] + by_off * s->uvlinesize + bx_off, ref_frame,
                  &uvmv, x_off + bx_off, y_off + by_off,
                  block_w, block_h, width, height, s->uvlinesize,
                  s->put_pixels_tab[1 + (block_w == 4)]);
}

/* Fetch pixels for estimated mv 4 macroblocks ahead.
 * Optimized for 64-byte cache lines. Inspired by ffh264 prefetch_motion. */
static av_always_inline
void prefetch_motion(VP8Context *s, VP8Macroblock *mb, int mb_x, int mb_y,
                     int mb_xy, int ref)
{
    /* Don't prefetch refs that haven't been used very often this frame. */
    if (s->ref_count[ref - 1] > (mb_xy >> 5)) {
        int x_off = mb_x << 4, y_off = mb_y << 4;
        int mx = (mb->mv.x >> 2) + x_off + 8;
        int my = (mb->mv.y >> 2) + y_off;
        uint8_t **src = s->framep[ref]->tf.f->data;
        int off = mx + (my + (mb_x & 3) * 4) * s->linesize + 64;
        /* For threading, a ff_thread_await_progress here might be useful, but
         * it actually slows down the decoder. Since a bad prefetch doesn't
         * generate bad decoder output, we don't run it here. */
        s->vdsp.prefetch(src[0] + off, s->linesize, 4);
        off = (mx >> 1) + ((my >> 1) + (mb_x & 7)) * s->uvlinesize + 64;
        s->vdsp.prefetch(src[1] + off, src[2] - src[1], 2);
    }
}

/**
 * Apply motion vectors to prediction buffer, chapter 18.
 */
static av_always_inline
void inter_predict(VP8Context *s, VP8ThreadData *td, uint8_t *dst[3],
                   VP8Macroblock *mb, int mb_x, int mb_y)
{
    int x_off = mb_x << 4, y_off = mb_y << 4;
    int width = 16 * s->mb_width, height = 16 * s->mb_height;
    ThreadFrame *ref = &s->framep[mb->ref_frame]->tf;
    VP56mv *bmv = mb->bmv;

    switch (mb->partitioning) {
    case VP8_SPLITMVMODE_NONE:
        vp8_mc_part(s, td, dst, ref, x_off, y_off,
                    0, 0, 16, 16, width, height, &mb->mv);
        break;
    case VP8_SPLITMVMODE_4x4: {
        int x, y;
        VP56mv uvmv;

        /* Y */
        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                vp8_mc_luma(s, td, dst[0] + 4 * y * s->linesize + x * 4,
                            ref, &bmv[4 * y + x],
                            4 * x + x_off, 4 * y + y_off, 4, 4,
                            width, height, s->linesize,
                            s->put_pixels_tab[2]);
            }
        }

        /* U/V */
        x_off  >>= 1;
        y_off  >>= 1;
        width  >>= 1;
        height >>= 1;
        for (y = 0; y < 2; y++) {
            for (x = 0; x < 2; x++) {
                uvmv.x = mb->bmv[2 * y       * 4 + 2 * x    ].x +
                         mb->bmv[2 * y       * 4 + 2 * x + 1].x +
                         mb->bmv[(2 * y + 1) * 4 + 2 * x    ].x +
                         mb->bmv[(2 * y + 1) * 4 + 2 * x + 1].x;
                uvmv.y = mb->bmv[2 * y       * 4 + 2 * x    ].y +
                         mb->bmv[2 * y       * 4 + 2 * x + 1].y +
                         mb->bmv[(2 * y + 1) * 4 + 2 * x    ].y +
                         mb->bmv[(2 * y + 1) * 4 + 2 * x + 1].y;
                uvmv.x = (uvmv.x + 2 + FF_SIGNBIT(uvmv.x)) >> 2;
                uvmv.y = (uvmv.y + 2 + FF_SIGNBIT(uvmv.y)) >> 2;
                if (s->profile == 3) {
                    uvmv.x &= ~7;
                    uvmv.y &= ~7;
                }
                vp8_mc_chroma(s, td, dst[1] + 4 * y * s->uvlinesize + x * 4,
                              dst[2] + 4 * y * s->uvlinesize + x * 4, ref,
                              &uvmv, 4 * x + x_off, 4 * y + y_off, 4, 4,
                              width, height, s->uvlinesize,
                              s->put_pixels_tab[2]);
            }
        }
        break;
    }
    case VP8_SPLITMVMODE_16x8:
        vp8_mc_part(s, td, dst, ref, x_off, y_off,
                    0, 0, 16, 8, width, height, &bmv[0]);
        vp8_mc_part(s, td, dst, ref, x_off, y_off,
                    0, 8, 16, 8, width, height, &bmv[1]);
        break;
    case VP8_SPLITMVMODE_8x16:
        vp8_mc_part(s, td, dst, ref, x_off, y_off,
                    0, 0, 8, 16, width, height, &bmv[0]);
        vp8_mc_part(s, td, dst, ref, x_off, y_off,
                    8, 0, 8, 16, width, height, &bmv[1]);
        break;
    case VP8_SPLITMVMODE_8x8:
        vp8_mc_part(s, td, dst, ref, x_off, y_off,
                    0, 0, 8, 8, width, height, &bmv[0]);
        vp8_mc_part(s, td, dst, ref, x_off, y_off,
                    8, 0, 8, 8, width, height, &bmv[1]);
        vp8_mc_part(s, td, dst, ref, x_off, y_off,
                    0, 8, 8, 8, width, height, &bmv[2]);
        vp8_mc_part(s, td, dst, ref, x_off, y_off,
                    8, 8, 8, 8, width, height, &bmv[3]);
        break;
    }
}

static av_always_inline
void idct_mb(VP8Context *s, VP8ThreadData *td, uint8_t *dst[3], VP8Macroblock *mb)
{
    int x, y, ch;

    if (mb->mode != MODE_I4x4) {
        uint8_t *y_dst = dst[0];
        for (y = 0; y < 4; y++) {
            uint32_t nnz4 = AV_RL32(td->non_zero_count_cache[y]);
            if (nnz4) {
                if (nnz4 & ~0x01010101) {
                    for (x = 0; x < 4; x++) {
                        if ((uint8_t) nnz4 == 1)
                            s->vp8dsp.vp8_idct_dc_add(y_dst + 4 * x,
                                                      td->block[y][x],
                                                      s->linesize);
                        else if ((uint8_t) nnz4 > 1)
                            s->vp8dsp.vp8_idct_add(y_dst + 4 * x,
                                                   td->block[y][x],
                                                   s->linesize);
                        nnz4 >>= 8;
                        if (!nnz4)
                            break;
                    }
                } else {
                    s->vp8dsp.vp8_idct_dc_add4y(y_dst, td->block[y], s->linesize);
                }
            }
            y_dst += 4 * s->linesize;
        }
    }

    for (ch = 0; ch < 2; ch++) {
        uint32_t nnz4 = AV_RL32(td->non_zero_count_cache[4 + ch]);
        if (nnz4) {
            uint8_t *ch_dst = dst[1 + ch];
            if (nnz4 & ~0x01010101) {
                for (y = 0; y < 2; y++) {
                    for (x = 0; x < 2; x++) {
                        if ((uint8_t) nnz4 == 1)
                            s->vp8dsp.vp8_idct_dc_add(ch_dst + 4 * x,
                                                      td->block[4 + ch][(y << 1) + x],
                                                      s->uvlinesize);
                        else if ((uint8_t) nnz4 > 1)
                            s->vp8dsp.vp8_idct_add(ch_dst + 4 * x,
                                                   td->block[4 + ch][(y << 1) + x],
                                                   s->uvlinesize);
                        nnz4 >>= 8;
                        if (!nnz4)
                            goto chroma_idct_end;
                    }
                    ch_dst += 4 * s->uvlinesize;
                }
            } else {
                s->vp8dsp.vp8_idct_dc_add4uv(ch_dst, td->block[4 + ch], s->uvlinesize);
            }
        }
chroma_idct_end:
        ;
    }
}

static av_always_inline
void filter_level_for_mb(VP8Context *s, VP8Macroblock *mb,
                         VP8FilterStrength *f, int is_vp7)
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
        filter_level += s->lf_delta.mode[mb->mode];
    }

    filter_level = av_clip_uintp2(filter_level, 6);

    interior_limit = filter_level;
    if (s->filter.sharpness) {
        interior_limit >>= (s->filter.sharpness + 3) >> 2;
        interior_limit = FFMIN(interior_limit, 9 - s->filter.sharpness);
    }
    interior_limit = FFMAX(interior_limit, 1);

    f->filter_level = filter_level;
    f->inner_limit = interior_limit;
    f->inner_filter = is_vp7 || !mb->skip || mb->mode == MODE_I4x4 ||
                      mb->mode == VP8_MVMODE_SPLIT;
}

static av_always_inline
void filter_mb(VP8Context *s, uint8_t *dst[3], VP8FilterStrength *f,
               int mb_x, int mb_y, int is_vp7)
{
    int mbedge_lim, bedge_lim_y, bedge_lim_uv, hev_thresh;
    int filter_level = f->filter_level;
    int inner_limit = f->inner_limit;
    int inner_filter = f->inner_filter;
    ptrdiff_t linesize   = s->linesize;
    ptrdiff_t uvlinesize = s->uvlinesize;
    static const uint8_t hev_thresh_lut[2][64] = {
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
          2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
          3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
          3, 3, 3, 3 },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
          1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
          2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
          2, 2, 2, 2 }
    };

    if (!filter_level)
        return;

    if (is_vp7) {
        bedge_lim_y  = filter_level;
        bedge_lim_uv = filter_level * 2;
        mbedge_lim   = filter_level + 2;
    } else {
        bedge_lim_y  =
        bedge_lim_uv = filter_level * 2 + inner_limit;
        mbedge_lim   = bedge_lim_y + 4;
    }

    hev_thresh = hev_thresh_lut[s->keyframe][filter_level];

    if (mb_x) {
        s->vp8dsp.vp8_h_loop_filter16y(dst[0], linesize,
                                       mbedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_h_loop_filter8uv(dst[1], dst[2], uvlinesize,
                                       mbedge_lim, inner_limit, hev_thresh);
    }

#define H_LOOP_FILTER_16Y_INNER(cond)                                         \
    if (cond && inner_filter) {                                               \
        s->vp8dsp.vp8_h_loop_filter16y_inner(dst[0] +  4, linesize,           \
                                             bedge_lim_y, inner_limit,        \
                                             hev_thresh);                     \
        s->vp8dsp.vp8_h_loop_filter16y_inner(dst[0] +  8, linesize,           \
                                             bedge_lim_y, inner_limit,        \
                                             hev_thresh);                     \
        s->vp8dsp.vp8_h_loop_filter16y_inner(dst[0] + 12, linesize,           \
                                             bedge_lim_y, inner_limit,        \
                                             hev_thresh);                     \
        s->vp8dsp.vp8_h_loop_filter8uv_inner(dst[1] +  4, dst[2] + 4,         \
                                             uvlinesize,  bedge_lim_uv,       \
                                             inner_limit, hev_thresh);        \
    }

    H_LOOP_FILTER_16Y_INNER(!is_vp7)

    if (mb_y) {
        s->vp8dsp.vp8_v_loop_filter16y(dst[0], linesize,
                                       mbedge_lim, inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter8uv(dst[1], dst[2], uvlinesize,
                                       mbedge_lim, inner_limit, hev_thresh);
    }

    if (inner_filter) {
        s->vp8dsp.vp8_v_loop_filter16y_inner(dst[0] +  4 * linesize,
                                             linesize, bedge_lim_y,
                                             inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter16y_inner(dst[0] +  8 * linesize,
                                             linesize, bedge_lim_y,
                                             inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter16y_inner(dst[0] + 12 * linesize,
                                             linesize, bedge_lim_y,
                                             inner_limit, hev_thresh);
        s->vp8dsp.vp8_v_loop_filter8uv_inner(dst[1] +  4 * uvlinesize,
                                             dst[2] +  4 * uvlinesize,
                                             uvlinesize, bedge_lim_uv,
                                             inner_limit, hev_thresh);
    }

    H_LOOP_FILTER_16Y_INNER(is_vp7)
}

static av_always_inline
void filter_mb_simple(VP8Context *s, uint8_t *dst, VP8FilterStrength *f,
                      int mb_x, int mb_y)
{
    int mbedge_lim, bedge_lim;
    int filter_level = f->filter_level;
    int inner_limit  = f->inner_limit;
    int inner_filter = f->inner_filter;
    ptrdiff_t linesize = s->linesize;

    if (!filter_level)
        return;

    bedge_lim  = 2 * filter_level + inner_limit;
    mbedge_lim = bedge_lim + 4;

    if (mb_x)
        s->vp8dsp.vp8_h_loop_filter_simple(dst, linesize, mbedge_lim);
    if (inner_filter) {
        s->vp8dsp.vp8_h_loop_filter_simple(dst +  4, linesize, bedge_lim);
        s->vp8dsp.vp8_h_loop_filter_simple(dst +  8, linesize, bedge_lim);
        s->vp8dsp.vp8_h_loop_filter_simple(dst + 12, linesize, bedge_lim);
    }

    if (mb_y)
        s->vp8dsp.vp8_v_loop_filter_simple(dst, linesize, mbedge_lim);
    if (inner_filter) {
        s->vp8dsp.vp8_v_loop_filter_simple(dst +  4 * linesize, linesize, bedge_lim);
        s->vp8dsp.vp8_v_loop_filter_simple(dst +  8 * linesize, linesize, bedge_lim);
        s->vp8dsp.vp8_v_loop_filter_simple(dst + 12 * linesize, linesize, bedge_lim);
    }
}

#define MARGIN (16 << 2)
static av_always_inline
int vp78_decode_mv_mb_modes(AVCodecContext *avctx, VP8Frame *curframe,
                                    VP8Frame *prev_frame, int is_vp7)
{
    VP8Context *s = avctx->priv_data;
    int mb_x, mb_y;

    s->mv_bounds.mv_min.y = -MARGIN;
    s->mv_bounds.mv_max.y = ((s->mb_height - 1) << 6) + MARGIN;
    for (mb_y = 0; mb_y < s->mb_height; mb_y++) {
        VP8Macroblock *mb = s->macroblocks_base +
                            ((s->mb_width + 1) * (mb_y + 1) + 1);
        int mb_xy = mb_y * s->mb_width;

        AV_WN32A(s->intra4x4_pred_mode_left, DC_PRED * 0x01010101);

        s->mv_bounds.mv_min.x = -MARGIN;
        s->mv_bounds.mv_max.x = ((s->mb_width - 1) << 6) + MARGIN;

        for (mb_x = 0; mb_x < s->mb_width; mb_x++, mb_xy++, mb++) {
            if (vpX_rac_is_end(&s->c)) {
                return AVERROR_INVALIDDATA;
            }
            if (mb_y == 0)
                AV_WN32A((mb - s->mb_width - 1)->intra4x4_pred_mode_top,
                         DC_PRED * 0x01010101);
            decode_mb_mode(s, &s->mv_bounds, mb, mb_x, mb_y, curframe->seg_map->data + mb_xy,
                           prev_frame && prev_frame->seg_map ?
                           prev_frame->seg_map->data + mb_xy : NULL, 1, is_vp7);
            s->mv_bounds.mv_min.x -= 64;
            s->mv_bounds.mv_max.x -= 64;
        }
        s->mv_bounds.mv_min.y -= 64;
        s->mv_bounds.mv_max.y -= 64;
    }
    return 0;
}

static int vp7_decode_mv_mb_modes(AVCodecContext *avctx, VP8Frame *cur_frame,
                                   VP8Frame *prev_frame)
{
    return vp78_decode_mv_mb_modes(avctx, cur_frame, prev_frame, IS_VP7);
}

static int vp8_decode_mv_mb_modes(AVCodecContext *avctx, VP8Frame *cur_frame,
                                   VP8Frame *prev_frame)
{
    return vp78_decode_mv_mb_modes(avctx, cur_frame, prev_frame, IS_VP8);
}

#if HAVE_THREADS
#define check_thread_pos(td, otd, mb_x_check, mb_y_check)                     \
    do {                                                                      \
        int tmp = (mb_y_check << 16) | (mb_x_check & 0xFFFF);                 \
        if (atomic_load(&otd->thread_mb_pos) < tmp) {                         \
            pthread_mutex_lock(&otd->lock);                                   \
            atomic_store(&td->wait_mb_pos, tmp);                              \
            do {                                                              \
                if (atomic_load(&otd->thread_mb_pos) >= tmp)                  \
                    break;                                                    \
                pthread_cond_wait(&otd->cond, &otd->lock);                    \
            } while (1);                                                      \
            atomic_store(&td->wait_mb_pos, INT_MAX);                          \
            pthread_mutex_unlock(&otd->lock);                                 \
        }                                                                     \
    } while (0)

#define update_pos(td, mb_y, mb_x)                                            \
    do {                                                                      \
        int pos              = (mb_y << 16) | (mb_x & 0xFFFF);                \
        int sliced_threading = (avctx->active_thread_type == FF_THREAD_SLICE) && \
                               (num_jobs > 1);                                \
        int is_null          = !next_td || !prev_td;                          \
        int pos_check        = (is_null) ? 1 :                                \
            (next_td != td && pos >= atomic_load(&next_td->wait_mb_pos)) ||   \
            (prev_td != td && pos >= atomic_load(&prev_td->wait_mb_pos));     \
        atomic_store(&td->thread_mb_pos, pos);                                \
        if (sliced_threading && pos_check) {                                  \
            pthread_mutex_lock(&td->lock);                                    \
            pthread_cond_broadcast(&td->cond);                                \
            pthread_mutex_unlock(&td->lock);                                  \
        }                                                                     \
    } while (0)
#else
#define check_thread_pos(td, otd, mb_x_check, mb_y_check) while(0)
#define update_pos(td, mb_y, mb_x) while(0)
#endif

static av_always_inline int decode_mb_row_no_filter(AVCodecContext *avctx, void *tdata,
                                        int jobnr, int threadnr, int is_vp7)
{
    VP8Context *s = avctx->priv_data;
    VP8ThreadData *prev_td, *next_td, *td = &s->thread_data[threadnr];
    int mb_y = atomic_load(&td->thread_mb_pos) >> 16;
    int mb_x, mb_xy = mb_y * s->mb_width;
    int num_jobs = s->num_jobs;
    VP8Frame *curframe = s->curframe, *prev_frame = s->prev_frame;
    VP56RangeCoder *c  = &s->coeff_partition[mb_y & (s->num_coeff_partitions - 1)];
    VP8Macroblock *mb;
    uint8_t *dst[3] = {
        curframe->tf.f->data[0] + 16 * mb_y * s->linesize,
        curframe->tf.f->data[1] +  8 * mb_y * s->uvlinesize,
        curframe->tf.f->data[2] +  8 * mb_y * s->uvlinesize
    };

    if (vpX_rac_is_end(c))
         return AVERROR_INVALIDDATA;

    if (mb_y == 0)
        prev_td = td;
    else
        prev_td = &s->thread_data[(jobnr + num_jobs - 1) % num_jobs];
    if (mb_y == s->mb_height - 1)
        next_td = td;
    else
        next_td = &s->thread_data[(jobnr + 1) % num_jobs];
    if (s->mb_layout == 1)
        mb = s->macroblocks_base + ((s->mb_width + 1) * (mb_y + 1) + 1);
    else {
        // Make sure the previous frame has read its segmentation map,
        // if we re-use the same map.
        if (prev_frame && s->segmentation.enabled &&
            !s->segmentation.update_map)
            ff_thread_await_progress(&prev_frame->tf, mb_y, 0);
        mb = s->macroblocks + (s->mb_height - mb_y - 1) * 2;
        memset(mb - 1, 0, sizeof(*mb)); // zero left macroblock
        AV_WN32A(s->intra4x4_pred_mode_left, DC_PRED * 0x01010101);
    }

    if (!is_vp7 || mb_y == 0)
        memset(td->left_nnz, 0, sizeof(td->left_nnz));

    td->mv_bounds.mv_min.x = -MARGIN;
    td->mv_bounds.mv_max.x = ((s->mb_width - 1) << 6) + MARGIN;

    for (mb_x = 0; mb_x < s->mb_width; mb_x++, mb_xy++, mb++) {
        if (vpX_rac_is_end(c))
            return AVERROR_INVALIDDATA;
        // Wait for previous thread to read mb_x+2, and reach mb_y-1.
        if (prev_td != td) {
            if (threadnr != 0) {
                check_thread_pos(td, prev_td,
                                 mb_x + (is_vp7 ? 2 : 1),
                                 mb_y - (is_vp7 ? 2 : 1));
            } else {
                check_thread_pos(td, prev_td,
                                 mb_x + (is_vp7 ? 2 : 1) + s->mb_width + 3,
                                 mb_y - (is_vp7 ? 2 : 1));
            }
        }

        s->vdsp.prefetch(dst[0] + (mb_x & 3) * 4 * s->linesize + 64,
                         s->linesize, 4);
        s->vdsp.prefetch(dst[1] + (mb_x & 7) * s->uvlinesize + 64,
                         dst[2] - dst[1], 2);

        if (!s->mb_layout)
            decode_mb_mode(s, &td->mv_bounds, mb, mb_x, mb_y, curframe->seg_map->data + mb_xy,
                           prev_frame && prev_frame->seg_map ?
                           prev_frame->seg_map->data + mb_xy : NULL, 0, is_vp7);

        prefetch_motion(s, mb, mb_x, mb_y, mb_xy, VP56_FRAME_PREVIOUS);

        if (!mb->skip)
            decode_mb_coeffs(s, td, c, mb, s->top_nnz[mb_x], td->left_nnz, is_vp7);

        if (mb->mode <= MODE_I4x4)
            intra_predict(s, td, dst, mb, mb_x, mb_y, is_vp7);
        else
            inter_predict(s, td, dst, mb, mb_x, mb_y);

        prefetch_motion(s, mb, mb_x, mb_y, mb_xy, VP56_FRAME_GOLDEN);

        if (!mb->skip) {
            idct_mb(s, td, dst, mb);
        } else {
            AV_ZERO64(td->left_nnz);
            AV_WN64(s->top_nnz[mb_x], 0);   // array of 9, so unaligned

            /* Reset DC block predictors if they would exist
             * if the mb had coefficients */
            if (mb->mode != MODE_I4x4 && mb->mode != VP8_MVMODE_SPLIT) {
                td->left_nnz[8]     = 0;
                s->top_nnz[mb_x][8] = 0;
            }
        }

        if (s->deblock_filter)
            filter_level_for_mb(s, mb, &td->filter_strength[mb_x], is_vp7);

        if (s->deblock_filter && num_jobs != 1 && threadnr == num_jobs - 1) {
            if (s->filter.simple)
                backup_mb_border(s->top_border[mb_x + 1], dst[0],
                                 NULL, NULL, s->linesize, 0, 1);
            else
                backup_mb_border(s->top_border[mb_x + 1], dst[0],
                                 dst[1], dst[2], s->linesize, s->uvlinesize, 0);
        }

        prefetch_motion(s, mb, mb_x, mb_y, mb_xy, VP56_FRAME_GOLDEN2);

        dst[0]      += 16;
        dst[1]      += 8;
        dst[2]      += 8;
        td->mv_bounds.mv_min.x -= 64;
        td->mv_bounds.mv_max.x -= 64;

        if (mb_x == s->mb_width + 1) {
            update_pos(td, mb_y, s->mb_width + 3);
        } else {
            update_pos(td, mb_y, mb_x);
        }
    }
    return 0;
}

static int vp7_decode_mb_row_no_filter(AVCodecContext *avctx, void *tdata,
                                        int jobnr, int threadnr)
{
    return decode_mb_row_no_filter(avctx, tdata, jobnr, threadnr, 1);
}

static int vp8_decode_mb_row_no_filter(AVCodecContext *avctx, void *tdata,
                                        int jobnr, int threadnr)
{
    return decode_mb_row_no_filter(avctx, tdata, jobnr, threadnr, 0);
}

static av_always_inline void filter_mb_row(AVCodecContext *avctx, void *tdata,
                              int jobnr, int threadnr, int is_vp7)
{
    VP8Context *s = avctx->priv_data;
    VP8ThreadData *td = &s->thread_data[threadnr];
    int mb_x, mb_y = atomic_load(&td->thread_mb_pos) >> 16, num_jobs = s->num_jobs;
    AVFrame *curframe = s->curframe->tf.f;
    VP8Macroblock *mb;
    VP8ThreadData *prev_td, *next_td;
    uint8_t *dst[3] = {
        curframe->data[0] + 16 * mb_y * s->linesize,
        curframe->data[1] +  8 * mb_y * s->uvlinesize,
        curframe->data[2] +  8 * mb_y * s->uvlinesize
    };

    if (s->mb_layout == 1)
        mb = s->macroblocks_base + ((s->mb_width + 1) * (mb_y + 1) + 1);
    else
        mb = s->macroblocks + (s->mb_height - mb_y - 1) * 2;

    if (mb_y == 0)
        prev_td = td;
    else
        prev_td = &s->thread_data[(jobnr + num_jobs - 1) % num_jobs];
    if (mb_y == s->mb_height - 1)
        next_td = td;
    else
        next_td = &s->thread_data[(jobnr + 1) % num_jobs];

    for (mb_x = 0; mb_x < s->mb_width; mb_x++, mb++) {
        VP8FilterStrength *f = &td->filter_strength[mb_x];
        if (prev_td != td)
            check_thread_pos(td, prev_td,
                             (mb_x + 1) + (s->mb_width + 3), mb_y - 1);
        if (next_td != td)
            if (next_td != &s->thread_data[0])
                check_thread_pos(td, next_td, mb_x + 1, mb_y + 1);

        if (num_jobs == 1) {
            if (s->filter.simple)
                backup_mb_border(s->top_border[mb_x + 1], dst[0],
                                 NULL, NULL, s->linesize, 0, 1);
            else
                backup_mb_border(s->top_border[mb_x + 1], dst[0],
                                 dst[1], dst[2], s->linesize, s->uvlinesize, 0);
        }

        if (s->filter.simple)
            filter_mb_simple(s, dst[0], f, mb_x, mb_y);
        else
            filter_mb(s, dst, f, mb_x, mb_y, is_vp7);
        dst[0] += 16;
        dst[1] += 8;
        dst[2] += 8;

        update_pos(td, mb_y, (s->mb_width + 3) + mb_x);
    }
}

static void vp7_filter_mb_row(AVCodecContext *avctx, void *tdata,
                              int jobnr, int threadnr)
{
    filter_mb_row(avctx, tdata, jobnr, threadnr, 1);
}

static void vp8_filter_mb_row(AVCodecContext *avctx, void *tdata,
                              int jobnr, int threadnr)
{
    filter_mb_row(avctx, tdata, jobnr, threadnr, 0);
}

static av_always_inline
int vp78_decode_mb_row_sliced(AVCodecContext *avctx, void *tdata, int jobnr,
                              int threadnr, int is_vp7)
{
    VP8Context *s = avctx->priv_data;
    VP8ThreadData *td = &s->thread_data[jobnr];
    VP8ThreadData *next_td = NULL, *prev_td = NULL;
    VP8Frame *curframe = s->curframe;
    int mb_y, num_jobs = s->num_jobs;
    int ret;

    td->thread_nr = threadnr;
    td->mv_bounds.mv_min.y   = -MARGIN - 64 * threadnr;
    td->mv_bounds.mv_max.y   = ((s->mb_height - 1) << 6) + MARGIN - 64 * threadnr;
    for (mb_y = jobnr; mb_y < s->mb_height; mb_y += num_jobs) {
        atomic_store(&td->thread_mb_pos, mb_y << 16);
        ret = s->decode_mb_row_no_filter(avctx, tdata, jobnr, threadnr);
        if (ret < 0) {
            update_pos(td, s->mb_height, INT_MAX & 0xFFFF);
            return ret;
        }
        if (s->deblock_filter)
            s->filter_mb_row(avctx, tdata, jobnr, threadnr);
        update_pos(td, mb_y, INT_MAX & 0xFFFF);

        td->mv_bounds.mv_min.y -= 64 * num_jobs;
        td->mv_bounds.mv_max.y -= 64 * num_jobs;

        if (avctx->active_thread_type == FF_THREAD_FRAME)
            ff_thread_report_progress(&curframe->tf, mb_y, 0);
    }

    return 0;
}

static int vp7_decode_mb_row_sliced(AVCodecContext *avctx, void *tdata,
                                    int jobnr, int threadnr)
{
    return vp78_decode_mb_row_sliced(avctx, tdata, jobnr, threadnr, IS_VP7);
}

static int vp8_decode_mb_row_sliced(AVCodecContext *avctx, void *tdata,
                                    int jobnr, int threadnr)
{
    return vp78_decode_mb_row_sliced(avctx, tdata, jobnr, threadnr, IS_VP8);
}

static av_always_inline
int vp78_decode_frame(AVCodecContext *avctx, AVFrame *rframe, int *got_frame,
                      const AVPacket *avpkt, int is_vp7)
{
    VP8Context *s = avctx->priv_data;
    int ret, i, referenced, num_jobs;
    enum AVDiscard skip_thresh;
    VP8Frame *av_uninit(curframe), *prev_frame;

    if (is_vp7)
        ret = vp7_decode_frame_header(s, avpkt->data, avpkt->size);
    else
        ret = vp8_decode_frame_header(s, avpkt->data, avpkt->size);

    if (ret < 0)
        goto err;

    if (s->actually_webp) {
        // avctx->pix_fmt already set in caller.
    } else if (!is_vp7 && s->pix_fmt == AV_PIX_FMT_NONE) {
        s->pix_fmt = get_pixel_format(s);
        if (s->pix_fmt < 0) {
            ret = AVERROR(EINVAL);
            goto err;
        }
        avctx->pix_fmt = s->pix_fmt;
    }

    prev_frame = s->framep[VP56_FRAME_CURRENT];

    referenced = s->update_last || s->update_golden == VP56_FRAME_CURRENT ||
                 s->update_altref == VP56_FRAME_CURRENT;

    skip_thresh = !referenced ? AVDISCARD_NONREF
                              : !s->keyframe ? AVDISCARD_NONKEY
                                             : AVDISCARD_ALL;

    if (avctx->skip_frame >= skip_thresh) {
        s->invisible = 1;
        memcpy(&s->next_framep[0], &s->framep[0], sizeof(s->framep[0]) * 4);
        goto skip_decode;
    }
    s->deblock_filter = s->filter.level && avctx->skip_loop_filter < skip_thresh;

    // release no longer referenced frames
    for (i = 0; i < 5; i++)
        if (s->frames[i].tf.f->buf[0] &&
            &s->frames[i] != prev_frame &&
            &s->frames[i] != s->framep[VP56_FRAME_PREVIOUS] &&
            &s->frames[i] != s->framep[VP56_FRAME_GOLDEN]   &&
            &s->frames[i] != s->framep[VP56_FRAME_GOLDEN2])
            vp8_release_frame(s, &s->frames[i]);

    curframe = s->framep[VP56_FRAME_CURRENT] = vp8_find_free_buffer(s);

    if (!s->colorspace)
        avctx->colorspace = AVCOL_SPC_BT470BG;
    if (s->fullrange)
        avctx->color_range = AVCOL_RANGE_JPEG;
    else
        avctx->color_range = AVCOL_RANGE_MPEG;

    /* Given that arithmetic probabilities are updated every frame, it's quite
     * likely that the values we have on a random interframe are complete
     * junk if we didn't start decode on a keyframe. So just don't display
     * anything rather than junk. */
    if (!s->keyframe && (!s->framep[VP56_FRAME_PREVIOUS] ||
                         !s->framep[VP56_FRAME_GOLDEN]   ||
                         !s->framep[VP56_FRAME_GOLDEN2])) {
        av_log(avctx, AV_LOG_WARNING,
               "Discarding interframe without a prior keyframe!\n");
        ret = AVERROR_INVALIDDATA;
        goto err;
    }

    curframe->tf.f->key_frame = s->keyframe;
    curframe->tf.f->pict_type = s->keyframe ? AV_PICTURE_TYPE_I
                                            : AV_PICTURE_TYPE_P;
    if ((ret = vp8_alloc_frame(s, curframe, referenced)) < 0)
        goto err;

    // check if golden and altref are swapped
    if (s->update_altref != VP56_FRAME_NONE)
        s->next_framep[VP56_FRAME_GOLDEN2] = s->framep[s->update_altref];
    else
        s->next_framep[VP56_FRAME_GOLDEN2] = s->framep[VP56_FRAME_GOLDEN2];

    if (s->update_golden != VP56_FRAME_NONE)
        s->next_framep[VP56_FRAME_GOLDEN] = s->framep[s->update_golden];
    else
        s->next_framep[VP56_FRAME_GOLDEN] = s->framep[VP56_FRAME_GOLDEN];

    if (s->update_last)
        s->next_framep[VP56_FRAME_PREVIOUS] = curframe;
    else
        s->next_framep[VP56_FRAME_PREVIOUS] = s->framep[VP56_FRAME_PREVIOUS];

    s->next_framep[VP56_FRAME_CURRENT] = curframe;

    if (ffcodec(avctx->codec)->update_thread_context)
        ff_thread_finish_setup(avctx);

    if (avctx->hwaccel) {
        ret = avctx->hwaccel->start_frame(avctx, avpkt->data, avpkt->size);
        if (ret < 0)
            goto err;

        ret = avctx->hwaccel->decode_slice(avctx, avpkt->data, avpkt->size);
        if (ret < 0)
            goto err;

        ret = avctx->hwaccel->end_frame(avctx);
        if (ret < 0)
            goto err;

    } else {
        s->linesize   = curframe->tf.f->linesize[0];
        s->uvlinesize = curframe->tf.f->linesize[1];

        memset(s->top_nnz, 0, s->mb_width * sizeof(*s->top_nnz));
        /* Zero macroblock structures for top/top-left prediction
         * from outside the frame. */
        if (!s->mb_layout)
            memset(s->macroblocks + s->mb_height * 2 - 1, 0,
                   (s->mb_width + 1) * sizeof(*s->macroblocks));
        if (!s->mb_layout && s->keyframe)
            memset(s->intra4x4_pred_mode_top, DC_PRED, s->mb_width * 4);

        memset(s->ref_count, 0, sizeof(s->ref_count));

        if (s->mb_layout == 1) {
            // Make sure the previous frame has read its segmentation map,
            // if we re-use the same map.
            if (prev_frame && s->segmentation.enabled &&
                !s->segmentation.update_map)
                ff_thread_await_progress(&prev_frame->tf, 1, 0);
            if (is_vp7)
                ret = vp7_decode_mv_mb_modes(avctx, curframe, prev_frame);
            else
                ret = vp8_decode_mv_mb_modes(avctx, curframe, prev_frame);
            if (ret < 0)
                goto err;
        }

        if (avctx->active_thread_type == FF_THREAD_FRAME)
            num_jobs = 1;
        else
            num_jobs = FFMIN(s->num_coeff_partitions, avctx->thread_count);
        s->num_jobs   = num_jobs;
        s->curframe   = curframe;
        s->prev_frame = prev_frame;
        s->mv_bounds.mv_min.y   = -MARGIN;
        s->mv_bounds.mv_max.y   = ((s->mb_height - 1) << 6) + MARGIN;
        for (i = 0; i < MAX_THREADS; i++) {
            VP8ThreadData *td = &s->thread_data[i];
            atomic_init(&td->thread_mb_pos, 0);
            atomic_init(&td->wait_mb_pos, INT_MAX);
        }
        if (is_vp7)
            avctx->execute2(avctx, vp7_decode_mb_row_sliced, s->thread_data, NULL,
                            num_jobs);
        else
            avctx->execute2(avctx, vp8_decode_mb_row_sliced, s->thread_data, NULL,
                            num_jobs);
    }

    ff_thread_report_progress(&curframe->tf, INT_MAX, 0);
    memcpy(&s->framep[0], &s->next_framep[0], sizeof(s->framep[0]) * 4);

skip_decode:
    // if future frames don't use the updated probabilities,
    // reset them to the values we saved
    if (!s->update_probabilities)
        s->prob[0] = s->prob[1];

    if (!s->invisible) {
        if ((ret = av_frame_ref(rframe, curframe->tf.f)) < 0)
            return ret;
        *got_frame = 1;
    }

    return avpkt->size;
err:
    memcpy(&s->next_framep[0], &s->framep[0], sizeof(s->framep[0]) * 4);
    return ret;
}

int ff_vp8_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    return vp78_decode_frame(avctx, frame, got_frame, avpkt, IS_VP8);
}

#if CONFIG_VP7_DECODER
static int vp7_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    return vp78_decode_frame(avctx, frame, got_frame, avpkt, IS_VP7);
}
#endif /* CONFIG_VP7_DECODER */

av_cold int ff_vp8_decode_free(AVCodecContext *avctx)
{
    VP8Context *s = avctx->priv_data;
    int i;

    vp8_decode_flush_impl(avctx, 1);
    for (i = 0; i < FF_ARRAY_ELEMS(s->frames); i++)
        av_frame_free(&s->frames[i].tf.f);

    return 0;
}

static av_cold int vp8_init_frames(VP8Context *s)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(s->frames); i++) {
        s->frames[i].tf.f = av_frame_alloc();
        if (!s->frames[i].tf.f)
            return AVERROR(ENOMEM);
    }
    return 0;
}

static av_always_inline
int vp78_decode_init(AVCodecContext *avctx, int is_vp7)
{
    VP8Context *s = avctx->priv_data;
    int ret;

    s->avctx = avctx;
    s->vp7   = avctx->codec->id == AV_CODEC_ID_VP7;
    s->pix_fmt = AV_PIX_FMT_NONE;
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    ff_videodsp_init(&s->vdsp, 8);

    ff_vp78dsp_init(&s->vp8dsp);
    if (CONFIG_VP7_DECODER && is_vp7) {
        ff_h264_pred_init(&s->hpc, AV_CODEC_ID_VP7, 8, 1);
        ff_vp7dsp_init(&s->vp8dsp);
        s->decode_mb_row_no_filter = vp7_decode_mb_row_no_filter;
        s->filter_mb_row           = vp7_filter_mb_row;
    } else if (CONFIG_VP8_DECODER && !is_vp7) {
        ff_h264_pred_init(&s->hpc, AV_CODEC_ID_VP8, 8, 1);
        ff_vp8dsp_init(&s->vp8dsp);
        s->decode_mb_row_no_filter = vp8_decode_mb_row_no_filter;
        s->filter_mb_row           = vp8_filter_mb_row;
    }

    /* does not change for VP8 */
    memcpy(s->prob[0].scan, ff_zigzag_scan, sizeof(s->prob[0].scan));

    if ((ret = vp8_init_frames(s)) < 0) {
        ff_vp8_decode_free(avctx);
        return ret;
    }

    return 0;
}

#if CONFIG_VP7_DECODER
static int vp7_decode_init(AVCodecContext *avctx)
{
    return vp78_decode_init(avctx, IS_VP7);
}
#endif /* CONFIG_VP7_DECODER */

av_cold int ff_vp8_decode_init(AVCodecContext *avctx)
{
    return vp78_decode_init(avctx, IS_VP8);
}

#if CONFIG_VP8_DECODER
#if HAVE_THREADS
#define REBASE(pic) ((pic) ? (pic) - &s_src->frames[0] + &s->frames[0] : NULL)

static int vp8_decode_update_thread_context(AVCodecContext *dst,
                                            const AVCodecContext *src)
{
    VP8Context *s = dst->priv_data, *s_src = src->priv_data;
    int i;

    if (s->macroblocks_base &&
        (s_src->mb_width != s->mb_width || s_src->mb_height != s->mb_height)) {
        free_buffers(s);
        s->mb_width  = s_src->mb_width;
        s->mb_height = s_src->mb_height;
    }

    s->pix_fmt      = s_src->pix_fmt;
    s->prob[0]      = s_src->prob[!s_src->update_probabilities];
    s->segmentation = s_src->segmentation;
    s->lf_delta     = s_src->lf_delta;
    memcpy(s->sign_bias, s_src->sign_bias, sizeof(s->sign_bias));

    for (i = 0; i < FF_ARRAY_ELEMS(s_src->frames); i++) {
        if (s_src->frames[i].tf.f->buf[0]) {
            int ret = vp8_ref_frame(s, &s->frames[i], &s_src->frames[i]);
            if (ret < 0)
                return ret;
        }
    }

    s->framep[0] = REBASE(s_src->next_framep[0]);
    s->framep[1] = REBASE(s_src->next_framep[1]);
    s->framep[2] = REBASE(s_src->next_framep[2]);
    s->framep[3] = REBASE(s_src->next_framep[3]);

    return 0;
}
#endif /* HAVE_THREADS */
#endif /* CONFIG_VP8_DECODER */

#if CONFIG_VP7_DECODER
const FFCodec ff_vp7_decoder = {
    .p.name                = "vp7",
    .p.long_name           = NULL_IF_CONFIG_SMALL("On2 VP7"),
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_VP7,
    .priv_data_size        = sizeof(VP8Context),
    .init                  = vp7_decode_init,
    .close                 = ff_vp8_decode_free,
    FF_CODEC_DECODE_CB(vp7_decode_frame),
    .p.capabilities        = AV_CODEC_CAP_DR1,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE,
    .flush                 = vp8_decode_flush,
};
#endif /* CONFIG_VP7_DECODER */

#if CONFIG_VP8_DECODER
const FFCodec ff_vp8_decoder = {
    .p.name                = "vp8",
    .p.long_name           = NULL_IF_CONFIG_SMALL("On2 VP8"),
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_VP8,
    .priv_data_size        = sizeof(VP8Context),
    .init                  = ff_vp8_decode_init,
    .close                 = ff_vp8_decode_free,
    FF_CODEC_DECODE_CB(ff_vp8_decode_frame),
    .p.capabilities        = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                             AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE |
                             FF_CODEC_CAP_ALLOCATE_PROGRESS,
    .flush                 = vp8_decode_flush,
    .update_thread_context = ONLY_IF_THREADS_ENABLED(vp8_decode_update_thread_context),
    .hw_configs            = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_VP8_VAAPI_HWACCEL
                               HWACCEL_VAAPI(vp8),
#endif
#if CONFIG_VP8_NVDEC_HWACCEL
                               HWACCEL_NVDEC(vp8),
#endif
                               NULL
                           },
};
#endif /* CONFIG_VP7_DECODER */
