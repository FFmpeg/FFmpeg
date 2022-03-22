/*
 * Common mpeg video decoding code
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

#include <limits.h>

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/video_enc_params.h"

#include "avcodec.h"
#include "internal.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpegvideodec.h"
#include "threadframe.h"

void ff_mpv_decode_init(MpegEncContext *s, AVCodecContext *avctx)
{
    ff_mpv_common_defaults(s);

    s->avctx           = avctx;
    s->width           = avctx->coded_width;
    s->height          = avctx->coded_height;
    s->codec_id        = avctx->codec->id;
    s->workaround_bugs = avctx->workaround_bugs;

    /* convert fourcc to upper case */
    s->codec_tag       = ff_toupper4(avctx->codec_tag);
}

int ff_mpeg_update_thread_context(AVCodecContext *dst,
                                  const AVCodecContext *src)
{
    MpegEncContext *const s1 = src->priv_data;
    MpegEncContext *const s  = dst->priv_data;
    int ret;

    if (dst == src)
        return 0;

    av_assert0(s != s1);

    // FIXME can parameters change on I-frames?
    // in that case dst may need a reinit
    if (!s->context_initialized) {
        void *private_ctx = s->private_ctx;
        int err;
        memcpy(s, s1, sizeof(*s));

        s->avctx                 = dst;
        s->private_ctx           = private_ctx;
        s->bitstream_buffer      = NULL;
        s->bitstream_buffer_size = s->allocated_bitstream_buffer_size = 0;

        if (s1->context_initialized) {
//             s->picture_range_start  += MAX_PICTURE_COUNT;
//             s->picture_range_end    += MAX_PICTURE_COUNT;
            ff_mpv_idct_init(s);
            if ((err = ff_mpv_common_init(s)) < 0) {
                memset(s, 0, sizeof(*s));
                s->avctx = dst;
                s->private_ctx = private_ctx;
                return err;
            }
        }
    }

    if (s->height != s1->height || s->width != s1->width || s->context_reinit) {
        s->height = s1->height;
        s->width  = s1->width;
        if ((ret = ff_mpv_common_frame_size_change(s)) < 0)
            return ret;
    }

    s->avctx->coded_height  = s1->avctx->coded_height;
    s->avctx->coded_width   = s1->avctx->coded_width;
    s->avctx->width         = s1->avctx->width;
    s->avctx->height        = s1->avctx->height;

    s->quarter_sample       = s1->quarter_sample;

    s->coded_picture_number = s1->coded_picture_number;
    s->picture_number       = s1->picture_number;

    av_assert0(!s->picture || s->picture != s1->picture);
    if (s->picture)
        for (int i = 0; i < MAX_PICTURE_COUNT; i++) {
            ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
            if (s1->picture && s1->picture[i].f->buf[0] &&
                (ret = ff_mpeg_ref_picture(s->avctx, &s->picture[i], &s1->picture[i])) < 0)
                return ret;
        }

#define UPDATE_PICTURE(pic)\
do {\
    ff_mpeg_unref_picture(s->avctx, &s->pic);\
    if (s1->pic.f && s1->pic.f->buf[0])\
        ret = ff_mpeg_ref_picture(s->avctx, &s->pic, &s1->pic);\
    else\
        ret = ff_update_picture_tables(&s->pic, &s1->pic);\
    if (ret < 0)\
        return ret;\
} while (0)

    UPDATE_PICTURE(current_picture);
    UPDATE_PICTURE(last_picture);
    UPDATE_PICTURE(next_picture);

#define REBASE_PICTURE(pic, new_ctx, old_ctx)                                 \
    ((pic && pic >= old_ctx->picture &&                                       \
      pic < old_ctx->picture + MAX_PICTURE_COUNT) ?                           \
        &new_ctx->picture[pic - old_ctx->picture] : NULL)

    s->last_picture_ptr    = REBASE_PICTURE(s1->last_picture_ptr,    s, s1);
    s->current_picture_ptr = REBASE_PICTURE(s1->current_picture_ptr, s, s1);
    s->next_picture_ptr    = REBASE_PICTURE(s1->next_picture_ptr,    s, s1);

    // Error/bug resilience
    s->workaround_bugs      = s1->workaround_bugs;
    s->padding_bug_score    = s1->padding_bug_score;

    // MPEG-4 timing info
    memcpy(&s->last_time_base, &s1->last_time_base,
           (char *) &s1->pb_field_time + sizeof(s1->pb_field_time) -
           (char *) &s1->last_time_base);

    // B-frame info
    s->max_b_frames = s1->max_b_frames;
    s->low_delay    = s1->low_delay;
    s->droppable    = s1->droppable;

    // DivX handling (doesn't work)
    s->divx_packed  = s1->divx_packed;

    if (s1->bitstream_buffer) {
        if (s1->bitstream_buffer_size +
            AV_INPUT_BUFFER_PADDING_SIZE > s->allocated_bitstream_buffer_size) {
            av_fast_malloc(&s->bitstream_buffer,
                           &s->allocated_bitstream_buffer_size,
                           s1->allocated_bitstream_buffer_size);
            if (!s->bitstream_buffer) {
                s->bitstream_buffer_size = 0;
                return AVERROR(ENOMEM);
            }
        }
        s->bitstream_buffer_size = s1->bitstream_buffer_size;
        memcpy(s->bitstream_buffer, s1->bitstream_buffer,
               s1->bitstream_buffer_size);
        memset(s->bitstream_buffer + s->bitstream_buffer_size, 0,
               AV_INPUT_BUFFER_PADDING_SIZE);
    }

    // linesize-dependent scratch buffer allocation
    if (!s->sc.edge_emu_buffer)
        if (s1->linesize) {
            if (ff_mpeg_framesize_alloc(s->avctx, &s->me,
                                        &s->sc, s1->linesize) < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Failed to allocate context "
                       "scratch buffers.\n");
                return AVERROR(ENOMEM);
            }
        } else {
            av_log(s->avctx, AV_LOG_ERROR, "Context scratch buffers could not "
                   "be allocated due to unknown size.\n");
        }

    // MPEG-2/interlacing info
    memcpy(&s->progressive_sequence, &s1->progressive_sequence,
           (char *) &s1->rtp_mode - (char *) &s1->progressive_sequence);

    return 0;
}

int ff_mpv_common_frame_size_change(MpegEncContext *s)
{
    int err = 0;

    if (!s->context_initialized)
        return AVERROR(EINVAL);

    ff_mpv_free_context_frame(s);

    if (s->picture)
        for (int i = 0; i < MAX_PICTURE_COUNT; i++)
            s->picture[i].needs_realloc = 1;

    s->last_picture_ptr         =
    s->next_picture_ptr         =
    s->current_picture_ptr      = NULL;

    // init
    if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO && !s->progressive_sequence)
        s->mb_height = (s->height + 31) / 32 * 2;
    else
        s->mb_height = (s->height + 15) / 16;

    if ((s->width || s->height) &&
        (err = av_image_check_size(s->width, s->height, 0, s->avctx)) < 0)
        goto fail;

    /* set chroma shifts */
    err = av_pix_fmt_get_chroma_sub_sample(s->avctx->pix_fmt,
                                           &s->chroma_x_shift,
                                           &s->chroma_y_shift);
    if (err < 0)
        goto fail;

    if ((err = ff_mpv_init_context_frame(s)))
        goto fail;

    memset(s->thread_context, 0, sizeof(s->thread_context));
    s->thread_context[0]   = s;

    if (s->width && s->height) {
        err = ff_mpv_init_duplicate_contexts(s);
        if (err < 0)
            goto fail;
    }
    s->context_reinit = 0;

    return 0;
 fail:
    ff_mpv_free_context_frame(s);
    s->context_reinit = 1;
    return err;
}

static int alloc_picture(MpegEncContext *s, Picture *pic)
{
    return ff_alloc_picture(s->avctx, pic, &s->me, &s->sc, 0, 0,
                            s->chroma_x_shift, s->chroma_y_shift, s->out_format,
                            s->mb_stride, s->mb_width, s->mb_height, s->b8_stride,
                            &s->linesize, &s->uvlinesize);
}

static void gray_frame(AVFrame *frame)
{
    int h_chroma_shift, v_chroma_shift;

    av_pix_fmt_get_chroma_sub_sample(frame->format, &h_chroma_shift, &v_chroma_shift);

    for (int i = 0; i < frame->height; i++)
        memset(frame->data[0] + frame->linesize[0] * i, 0x80, frame->width);
    for (int i = 0; i < AV_CEIL_RSHIFT(frame->height, v_chroma_shift); i++) {
        memset(frame->data[1] + frame->linesize[1] * i,
               0x80, AV_CEIL_RSHIFT(frame->width, h_chroma_shift));
        memset(frame->data[2] + frame->linesize[2] * i,
               0x80, AV_CEIL_RSHIFT(frame->width, h_chroma_shift));
    }
}

/**
 * generic function called after decoding
 * the header and before a frame is decoded.
 */
int ff_mpv_frame_start(MpegEncContext *s, AVCodecContext *avctx)
{
    Picture *pic;
    int idx, ret;

    s->mb_skipped = 0;

    if (!ff_thread_can_start_frame(avctx)) {
        av_log(avctx, AV_LOG_ERROR, "Attempt to start a frame outside SETUP state\n");
        return -1;
    }

    /* mark & release old frames */
    if (s->pict_type != AV_PICTURE_TYPE_B && s->last_picture_ptr &&
        s->last_picture_ptr != s->next_picture_ptr &&
        s->last_picture_ptr->f->buf[0]) {
        ff_mpeg_unref_picture(s->avctx, s->last_picture_ptr);
    }

    /* release forgotten pictures */
    /* if (MPEG-124 / H.263) */
    for (int i = 0; i < MAX_PICTURE_COUNT; i++) {
        if (&s->picture[i] != s->last_picture_ptr &&
            &s->picture[i] != s->next_picture_ptr &&
            s->picture[i].reference && !s->picture[i].needs_realloc) {
            ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
        }
    }

    ff_mpeg_unref_picture(s->avctx, &s->current_picture);
    ff_mpeg_unref_picture(s->avctx, &s->last_picture);
    ff_mpeg_unref_picture(s->avctx, &s->next_picture);

    /* release non reference frames */
    for (int i = 0; i < MAX_PICTURE_COUNT; i++) {
        if (!s->picture[i].reference)
            ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
    }

    if (s->current_picture_ptr && !s->current_picture_ptr->f->buf[0]) {
        // we already have an unused image
        // (maybe it was set before reading the header)
        pic = s->current_picture_ptr;
    } else {
        idx = ff_find_unused_picture(s->avctx, s->picture, 0);
        if (idx < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "no frame buffer available\n");
            return idx;
        }
        pic = &s->picture[idx];
    }

    pic->reference = 0;
    if (!s->droppable) {
        if (s->pict_type != AV_PICTURE_TYPE_B)
            pic->reference = 3;
    }

    pic->f->coded_picture_number = s->coded_picture_number++;

    if (alloc_picture(s, pic) < 0)
        return -1;

    s->current_picture_ptr = pic;
    // FIXME use only the vars from current_pic
    s->current_picture_ptr->f->top_field_first = s->top_field_first;
    if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO ||
        s->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        if (s->picture_structure != PICT_FRAME)
            s->current_picture_ptr->f->top_field_first =
                (s->picture_structure == PICT_TOP_FIELD) == s->first_field;
    }
    s->current_picture_ptr->f->interlaced_frame = !s->progressive_frame &&
                                                 !s->progressive_sequence;
    s->current_picture_ptr->field_picture      =  s->picture_structure != PICT_FRAME;

    s->current_picture_ptr->f->pict_type = s->pict_type;
    s->current_picture_ptr->f->key_frame = s->pict_type == AV_PICTURE_TYPE_I;

    if ((ret = ff_mpeg_ref_picture(s->avctx, &s->current_picture,
                                   s->current_picture_ptr)) < 0)
        return ret;

    if (s->pict_type != AV_PICTURE_TYPE_B) {
        s->last_picture_ptr = s->next_picture_ptr;
        if (!s->droppable)
            s->next_picture_ptr = s->current_picture_ptr;
    }
    ff_dlog(s->avctx, "L%p N%p C%p L%p N%p C%p type:%d drop:%d\n",
            s->last_picture_ptr, s->next_picture_ptr,s->current_picture_ptr,
            s->last_picture_ptr    ? s->last_picture_ptr->f->data[0]    : NULL,
            s->next_picture_ptr    ? s->next_picture_ptr->f->data[0]    : NULL,
            s->current_picture_ptr ? s->current_picture_ptr->f->data[0] : NULL,
            s->pict_type, s->droppable);

    if ((!s->last_picture_ptr || !s->last_picture_ptr->f->buf[0]) &&
        (s->pict_type != AV_PICTURE_TYPE_I)) {
        int h_chroma_shift, v_chroma_shift;
        av_pix_fmt_get_chroma_sub_sample(s->avctx->pix_fmt,
                                         &h_chroma_shift, &v_chroma_shift);
        if (s->pict_type == AV_PICTURE_TYPE_B && s->next_picture_ptr && s->next_picture_ptr->f->buf[0])
            av_log(avctx, AV_LOG_DEBUG,
                   "allocating dummy last picture for B frame\n");
        else if (s->pict_type != AV_PICTURE_TYPE_I)
            av_log(avctx, AV_LOG_ERROR,
                   "warning: first frame is no keyframe\n");

        /* Allocate a dummy frame */
        idx = ff_find_unused_picture(s->avctx, s->picture, 0);
        if (idx < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "no frame buffer available\n");
            return idx;
        }
        s->last_picture_ptr = &s->picture[idx];

        s->last_picture_ptr->reference    = 3;
        s->last_picture_ptr->f->key_frame = 0;
        s->last_picture_ptr->f->pict_type = AV_PICTURE_TYPE_P;

        if (alloc_picture(s, s->last_picture_ptr) < 0) {
            s->last_picture_ptr = NULL;
            return -1;
        }

        if (!avctx->hwaccel) {
            for (int i = 0; i < avctx->height; i++)
                memset(s->last_picture_ptr->f->data[0] + s->last_picture_ptr->f->linesize[0]*i,
                       0x80, avctx->width);
            if (s->last_picture_ptr->f->data[2]) {
                for (int i = 0; i < AV_CEIL_RSHIFT(avctx->height, v_chroma_shift); i++) {
                    memset(s->last_picture_ptr->f->data[1] + s->last_picture_ptr->f->linesize[1]*i,
                        0x80, AV_CEIL_RSHIFT(avctx->width, h_chroma_shift));
                    memset(s->last_picture_ptr->f->data[2] + s->last_picture_ptr->f->linesize[2]*i,
                        0x80, AV_CEIL_RSHIFT(avctx->width, h_chroma_shift));
                }
            }

            if (s->codec_id == AV_CODEC_ID_FLV1 || s->codec_id == AV_CODEC_ID_H263) {
                for (int i = 0; i < avctx->height; i++)
                    memset(s->last_picture_ptr->f->data[0] + s->last_picture_ptr->f->linesize[0] * i,
                           16, avctx->width);
            }
        }

        ff_thread_report_progress(&s->last_picture_ptr->tf, INT_MAX, 0);
        ff_thread_report_progress(&s->last_picture_ptr->tf, INT_MAX, 1);
    }
    if ((!s->next_picture_ptr || !s->next_picture_ptr->f->buf[0]) &&
        s->pict_type == AV_PICTURE_TYPE_B) {
        /* Allocate a dummy frame */
        idx = ff_find_unused_picture(s->avctx, s->picture, 0);
        if (idx < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "no frame buffer available\n");
            return idx;
        }
        s->next_picture_ptr = &s->picture[idx];

        s->next_picture_ptr->reference   = 3;
        s->next_picture_ptr->f->key_frame = 0;
        s->next_picture_ptr->f->pict_type = AV_PICTURE_TYPE_P;

        if (alloc_picture(s, s->next_picture_ptr) < 0) {
            s->next_picture_ptr = NULL;
            return -1;
        }
        ff_thread_report_progress(&s->next_picture_ptr->tf, INT_MAX, 0);
        ff_thread_report_progress(&s->next_picture_ptr->tf, INT_MAX, 1);
    }

#if 0 // BUFREF-FIXME
    memset(s->last_picture.f->data, 0, sizeof(s->last_picture.f->data));
    memset(s->next_picture.f->data, 0, sizeof(s->next_picture.f->data));
#endif
    if (s->last_picture_ptr) {
        if (s->last_picture_ptr->f->buf[0] &&
            (ret = ff_mpeg_ref_picture(s->avctx, &s->last_picture,
                                       s->last_picture_ptr)) < 0)
            return ret;
    }
    if (s->next_picture_ptr) {
        if (s->next_picture_ptr->f->buf[0] &&
            (ret = ff_mpeg_ref_picture(s->avctx, &s->next_picture,
                                       s->next_picture_ptr)) < 0)
            return ret;
    }

    av_assert0(s->pict_type == AV_PICTURE_TYPE_I || (s->last_picture_ptr &&
                                                 s->last_picture_ptr->f->buf[0]));

    if (s->picture_structure != PICT_FRAME) {
        for (int i = 0; i < 4; i++) {
            if (s->picture_structure == PICT_BOTTOM_FIELD) {
                s->current_picture.f->data[i] +=
                    s->current_picture.f->linesize[i];
            }
            s->current_picture.f->linesize[i] *= 2;
            s->last_picture.f->linesize[i]    *= 2;
            s->next_picture.f->linesize[i]    *= 2;
        }
    }

    /* set dequantizer, we can't do it during init as
     * it might change for MPEG-4 and we can't do it in the header
     * decode as init is not called for MPEG-4 there yet */
    if (s->mpeg_quant || s->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        s->dct_unquantize_intra = s->dct_unquantize_mpeg2_intra;
        s->dct_unquantize_inter = s->dct_unquantize_mpeg2_inter;
    } else if (s->out_format == FMT_H263 || s->out_format == FMT_H261) {
        s->dct_unquantize_intra = s->dct_unquantize_h263_intra;
        s->dct_unquantize_inter = s->dct_unquantize_h263_inter;
    } else {
        s->dct_unquantize_intra = s->dct_unquantize_mpeg1_intra;
        s->dct_unquantize_inter = s->dct_unquantize_mpeg1_inter;
    }

    if (s->avctx->debug & FF_DEBUG_NOMC)
        gray_frame(s->current_picture_ptr->f);

    return 0;
}

/* called after a frame has been decoded. */
void ff_mpv_frame_end(MpegEncContext *s)
{
    emms_c();

    if (s->current_picture.reference)
        ff_thread_report_progress(&s->current_picture_ptr->tf, INT_MAX, 0);
}

void ff_print_debug_info(MpegEncContext *s, Picture *p, AVFrame *pict)
{
    ff_print_debug_info2(s->avctx, pict, s->mbskip_table, p->mb_type,
                         p->qscale_table, p->motion_val,
                         s->mb_width, s->mb_height, s->mb_stride, s->quarter_sample);
}

int ff_mpv_export_qp_table(MpegEncContext *s, AVFrame *f, Picture *p, int qp_type)
{
    AVVideoEncParams *par;
    int mult = (qp_type == FF_MPV_QSCALE_TYPE_MPEG1) ? 2 : 1;
    unsigned int nb_mb = p->alloc_mb_height * p->alloc_mb_width;

    if (!(s->avctx->export_side_data & AV_CODEC_EXPORT_DATA_VIDEO_ENC_PARAMS))
        return 0;

    par = av_video_enc_params_create_side_data(f, AV_VIDEO_ENC_PARAMS_MPEG2, nb_mb);
    if (!par)
        return AVERROR(ENOMEM);

    for (unsigned y = 0; y < p->alloc_mb_height; y++)
        for (unsigned x = 0; x < p->alloc_mb_width; x++) {
            const unsigned int block_idx = y * p->alloc_mb_width + x;
            const unsigned int     mb_xy = y * p->alloc_mb_stride + x;
            AVVideoBlockParams *const b = av_video_enc_params_block(par, block_idx);

            b->src_x = x * 16;
            b->src_y = y * 16;
            b->w     = 16;
            b->h     = 16;

            b->delta_qp = p->qscale_table[mb_xy] * mult;
        }

    return 0;
}

void ff_mpeg_draw_horiz_band(MpegEncContext *s, int y, int h)
{
    ff_draw_horiz_band(s->avctx, s->current_picture_ptr->f,
                       s->last_picture_ptr ? s->last_picture_ptr->f : NULL,
                       y, h, s->picture_structure,
                       s->first_field, s->low_delay);
}

void ff_mpeg_flush(AVCodecContext *avctx)
{
    MpegEncContext *const s = avctx->priv_data;

    if (!s->picture)
        return;

    for (int i = 0; i < MAX_PICTURE_COUNT; i++)
        ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
    s->current_picture_ptr = s->last_picture_ptr = s->next_picture_ptr = NULL;

    ff_mpeg_unref_picture(s->avctx, &s->current_picture);
    ff_mpeg_unref_picture(s->avctx, &s->last_picture);
    ff_mpeg_unref_picture(s->avctx, &s->next_picture);

    s->mb_x = s->mb_y = 0;

#if FF_API_FLAG_TRUNCATED
    s->parse_context.state = -1;
    s->parse_context.frame_start_found = 0;
    s->parse_context.overread = 0;
    s->parse_context.overread_index = 0;
    s->parse_context.index = 0;
    s->parse_context.last_index = 0;
#endif
    s->bitstream_buffer_size = 0;
    s->pp_time = 0;
}

void ff_mpv_report_decode_progress(MpegEncContext *s)
{
    if (s->pict_type != AV_PICTURE_TYPE_B && !s->partitioned_frame && !s->er.error_occurred)
        ff_thread_report_progress(&s->current_picture_ptr->tf, s->mb_y, 0);
}
