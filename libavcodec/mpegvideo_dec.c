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

#include "config_components.h"

#include "libavutil/avassert.h"
#include "libavutil/emms.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/video_enc_params.h"

#include "avcodec.h"
#include "decode.h"
#include "h263.h"
#include "h264chroma.h"
#include "internal.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpegvideodec.h"
#include "mpeg4videodec.h"
#include "libavutil/refstruct.h"
#include "thread.h"
#include "threadprogress.h"
#include "wmv2dec.h"

av_cold int ff_mpv_decode_init(MpegEncContext *s, AVCodecContext *avctx)
{
    enum ThreadingStatus thread_status;

    ff_mpv_common_defaults(s);

    s->avctx           = avctx;
    s->width           = avctx->coded_width;
    s->height          = avctx->coded_height;
    s->codec_id        = avctx->codec->id;
    s->workaround_bugs = avctx->workaround_bugs;

    /* convert fourcc to upper case */
    s->codec_tag       = ff_toupper4(avctx->codec_tag);

    ff_mpv_idct_init(s);

    ff_h264chroma_init(&s->h264chroma, 8); //for lowres

    if (s->picture_pool)  // VC-1 can call this multiple times
        return 0;

    thread_status = ff_thread_sync_ref(avctx, offsetof(MpegEncContext, picture_pool));
    if (thread_status != FF_THREAD_IS_COPY) {
        s->picture_pool = ff_mpv_alloc_pic_pool(thread_status != FF_THREAD_NO_FRAME_THREADING);
        if (!s->picture_pool)
            return AVERROR(ENOMEM);
    }
    return 0;
}

int ff_mpeg_update_thread_context(AVCodecContext *dst,
                                  const AVCodecContext *src)
{
    MpegEncContext *const s1 = src->priv_data;
    MpegEncContext *const s  = dst->priv_data;
    int ret = 0;

    if (dst == src)
        return 0;

    av_assert0(s != s1);

    if (s->height != s1->height || s->width != s1->width || s->context_reinit) {
        s->height = s1->height;
        s->width  = s1->width;
        if ((ret = ff_mpv_common_frame_size_change(s)) < 0)
            return ret;
        ret = 1;
    }

    s->quarter_sample       = s1->quarter_sample;

    s->picture_number       = s1->picture_number;

    ff_mpv_replace_picture(&s->cur_pic,  &s1->cur_pic);
    ff_mpv_replace_picture(&s->last_pic, &s1->last_pic);
    ff_mpv_replace_picture(&s->next_pic, &s1->next_pic);

    s->linesize   = s1->linesize;
    s->uvlinesize = s1->uvlinesize;

    // Error/bug resilience
    s->workaround_bugs      = s1->workaround_bugs;
    s->padding_bug_score    = s1->padding_bug_score;

    // MPEG-4 timing info
    memcpy(&s->last_time_base, &s1->last_time_base,
           (char *) &s1->pb_field_time + sizeof(s1->pb_field_time) -
           (char *) &s1->last_time_base);

    // B-frame info
    s->low_delay    = s1->low_delay;

    // MPEG-2/interlacing info
    memcpy(&s->progressive_sequence, &s1->progressive_sequence,
           (char *) &s1->first_field + sizeof(s1->first_field) - (char *) &s1->progressive_sequence);

    return ret;
}

av_cold int ff_mpv_decode_close(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;

    av_refstruct_pool_uninit(&s->picture_pool);
    ff_mpv_common_end(s);
    return 0;
}

av_cold int ff_mpv_common_frame_size_change(MpegEncContext *s)
{
    int err = 0;

    if (!s->context_initialized)
        return AVERROR(EINVAL);

    ff_mpv_free_context_frame(s);

    ff_mpv_unref_picture(&s->last_pic);
    ff_mpv_unref_picture(&s->next_pic);
    ff_mpv_unref_picture(&s->cur_pic);

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

static int alloc_picture(MpegEncContext *s, MPVWorkPicture *dst, int reference)
{
    AVCodecContext *avctx = s->avctx;
    MPVPicture *pic = av_refstruct_pool_get(s->picture_pool);
    int ret;

    if (!pic)
        return AVERROR(ENOMEM);

    dst->ptr = pic;

    pic->reference = reference;

    /* WM Image / Screen codecs allocate internal buffers with different
     * dimensions / colorspaces; ignore user-defined callbacks for these. */
    if (avctx->codec_id != AV_CODEC_ID_WMV3IMAGE &&
        avctx->codec_id != AV_CODEC_ID_VC1IMAGE  &&
        avctx->codec_id != AV_CODEC_ID_MSS2) {
        ret = ff_thread_get_buffer(avctx, pic->f,
                                   reference ? AV_GET_BUFFER_FLAG_REF : 0);
    } else {
        pic->f->width  = avctx->width;
        pic->f->height = avctx->height;
        pic->f->format = avctx->pix_fmt;
        ret = avcodec_default_get_buffer2(avctx, pic->f, 0);
    }
    if (ret < 0)
        goto fail;

    ret = ff_mpv_pic_check_linesize(avctx, pic->f, &s->linesize, &s->uvlinesize);
    if (ret < 0)
        goto fail;

    ret = ff_hwaccel_frame_priv_alloc(avctx, &pic->hwaccel_picture_private);
    if (ret < 0)
        goto fail;

    av_assert1(s->mb_width  == s->buffer_pools.alloc_mb_width);
    av_assert1(s->mb_height == s->buffer_pools.alloc_mb_height ||
               FFALIGN(s->mb_height, 2) == s->buffer_pools.alloc_mb_height);
    av_assert1(s->mb_stride == s->buffer_pools.alloc_mb_stride);
    ret = ff_mpv_alloc_pic_accessories(s->avctx, dst, &s->sc,
                                       &s->buffer_pools, s->mb_height);
    if (ret < 0)
        goto fail;

    return 0;
fail:
    ff_mpv_unref_picture(dst);
    return ret;
}

static int av_cold alloc_dummy_frame(MpegEncContext *s, MPVWorkPicture *dst)
{
    MPVPicture *pic;
    int ret = alloc_picture(s, dst, 1);
    if (ret < 0)
        return ret;

    pic = dst->ptr;
    pic->dummy = 1;

    ff_thread_progress_report(&pic->progress, INT_MAX);

    return 0;
}

static void color_frame(AVFrame *frame, int luma)
{
    int h_chroma_shift, v_chroma_shift;

    for (int i = 0; i < frame->height; i++)
        memset(frame->data[0] + frame->linesize[0] * i, luma, frame->width);

    if (!frame->data[1])
        return;
    av_pix_fmt_get_chroma_sub_sample(frame->format, &h_chroma_shift, &v_chroma_shift);
    for (int i = 0; i < AV_CEIL_RSHIFT(frame->height, v_chroma_shift); i++) {
        memset(frame->data[1] + frame->linesize[1] * i,
               0x80, AV_CEIL_RSHIFT(frame->width, h_chroma_shift));
        memset(frame->data[2] + frame->linesize[2] * i,
               0x80, AV_CEIL_RSHIFT(frame->width, h_chroma_shift));
    }
}

int ff_mpv_alloc_dummy_frames(MpegEncContext *s)
{
    AVCodecContext *avctx = s->avctx;
    int ret;

    av_assert1(!s->last_pic.ptr || s->last_pic.ptr->f->buf[0]);
    av_assert1(!s->next_pic.ptr || s->next_pic.ptr->f->buf[0]);
    if (!s->last_pic.ptr && s->pict_type != AV_PICTURE_TYPE_I) {
        if (s->pict_type == AV_PICTURE_TYPE_B && s->next_pic.ptr)
            av_log(avctx, AV_LOG_DEBUG,
                   "allocating dummy last picture for B frame\n");
        else if (s->codec_id != AV_CODEC_ID_H261 /* H.261 has no keyframes */ &&
                 (s->picture_structure == PICT_FRAME || s->first_field))
            av_log(avctx, AV_LOG_ERROR,
                   "warning: first frame is no keyframe\n");

        /* Allocate a dummy frame */
        ret = alloc_dummy_frame(s, &s->last_pic);
        if (ret < 0)
            return ret;

        if (!avctx->hwaccel) {
            int luma_val = s->codec_id == AV_CODEC_ID_FLV1 || s->codec_id == AV_CODEC_ID_H263 ? 16 : 0x80;
            color_frame(s->last_pic.ptr->f, luma_val);
        }
    }
    if (!s->next_pic.ptr && s->pict_type == AV_PICTURE_TYPE_B) {
        /* Allocate a dummy frame */
        ret = alloc_dummy_frame(s, &s->next_pic);
        if (ret < 0)
            return ret;
    }

    av_assert0(s->pict_type == AV_PICTURE_TYPE_I || (s->last_pic.ptr &&
                                                 s->last_pic.ptr->f->buf[0]));

    return 0;
}

/**
 * generic function called after decoding
 * the header and before a frame is decoded.
 */
int ff_mpv_frame_start(MpegEncContext *s, AVCodecContext *avctx)
{
    int ret;

    s->mb_skipped = 0;

    if (!ff_thread_can_start_frame(avctx)) {
        av_log(avctx, AV_LOG_ERROR, "Attempt to start a frame outside SETUP state\n");
        return AVERROR_BUG;
    }

    ff_mpv_unref_picture(&s->cur_pic);
    ret = alloc_picture(s, &s->cur_pic,
                        s->pict_type != AV_PICTURE_TYPE_B && !s->droppable);
    if (ret < 0)
        return ret;

    s->cur_pic.ptr->f->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST * !!s->top_field_first;
    s->cur_pic.ptr->f->flags |= AV_FRAME_FLAG_INTERLACED *
                                (!s->progressive_frame && !s->progressive_sequence);
    s->cur_pic.ptr->field_picture = s->picture_structure != PICT_FRAME;

    s->cur_pic.ptr->f->pict_type = s->pict_type;
    if (s->pict_type == AV_PICTURE_TYPE_I)
        s->cur_pic.ptr->f->flags |= AV_FRAME_FLAG_KEY;
    else
        s->cur_pic.ptr->f->flags &= ~AV_FRAME_FLAG_KEY;

    if (s->pict_type != AV_PICTURE_TYPE_B) {
        ff_mpv_workpic_from_pic(&s->last_pic, s->next_pic.ptr);
        if (!s->droppable)
            ff_mpv_workpic_from_pic(&s->next_pic, s->cur_pic.ptr);
    }
    ff_dlog(s->avctx, "L%p N%p C%p L%p N%p C%p type:%d drop:%d\n",
            (void*)s->last_pic.ptr, (void*)s->next_pic.ptr, (void*)s->cur_pic.ptr,
            s->last_pic.ptr ? s->last_pic.ptr->f->data[0] : NULL,
            s->next_pic.ptr ? s->next_pic.ptr->f->data[0] : NULL,
            s->cur_pic.ptr  ? s->cur_pic.ptr->f->data[0]  : NULL,
            s->pict_type, s->droppable);

    ret = ff_mpv_alloc_dummy_frames(s);
    if (ret < 0)
        return ret;

    if (s->avctx->debug & FF_DEBUG_NOMC)
        color_frame(s->cur_pic.ptr->f, 0x80);

    return 0;
}

/* called after a frame has been decoded. */
void ff_mpv_frame_end(MpegEncContext *s)
{
    emms_c();

    if (s->cur_pic.reference)
        ff_thread_progress_report(&s->cur_pic.ptr->progress, INT_MAX);
}

void ff_print_debug_info(const MpegEncContext *s, const MPVPicture *p, AVFrame *pict)
{
    ff_print_debug_info2(s->avctx, pict, p->mb_type,
                         p->qscale_table, p->motion_val,
                         p->mb_width, p->mb_height, p->mb_stride, s->quarter_sample);
}

int ff_mpv_export_qp_table(const MpegEncContext *s, AVFrame *f,
                           const MPVPicture *p, int qp_type)
{
    AVVideoEncParams *par;
    int mult = (qp_type == FF_MPV_QSCALE_TYPE_MPEG1) ? 2 : 1;
    unsigned int nb_mb = p->mb_height * p->mb_width;

    if (!(s->avctx->export_side_data & AV_CODEC_EXPORT_DATA_VIDEO_ENC_PARAMS))
        return 0;

    par = av_video_enc_params_create_side_data(f, AV_VIDEO_ENC_PARAMS_MPEG2, nb_mb);
    if (!par)
        return AVERROR(ENOMEM);

    for (unsigned y = 0; y < p->mb_height; y++)
        for (unsigned x = 0; x < p->mb_width; x++) {
            const unsigned int block_idx = y * p->mb_width + x;
            const unsigned int     mb_xy = y * p->mb_stride + x;
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
    ff_draw_horiz_band(s->avctx, s->cur_pic.ptr->f,
                       s->last_pic.ptr ? s->last_pic.ptr->f : NULL,
                       y, h, s->picture_structure,
                       s->first_field, s->low_delay);
}

av_cold void ff_mpeg_flush(AVCodecContext *avctx)
{
    MpegEncContext *const s = avctx->priv_data;

    ff_mpv_unref_picture(&s->cur_pic);
    ff_mpv_unref_picture(&s->last_pic);
    ff_mpv_unref_picture(&s->next_pic);

    s->mb_x = s->mb_y = 0;

    s->pp_time = 0;
}

static inline int hpel_motion_lowres(MpegEncContext *s,
                                     uint8_t *dest, const uint8_t *src,
                                     int field_based, int field_select,
                                     int src_x, int src_y,
                                     int width, int height, ptrdiff_t stride,
                                     int h_edge_pos, int v_edge_pos,
                                     int w, int h, const h264_chroma_mc_func *pix_op,
                                     int motion_x, int motion_y)
{
    const int lowres   = s->avctx->lowres;
    const int op_index = lowres;
    const int s_mask   = (2 << lowres) - 1;
    int emu = 0;
    int sx, sy;

    av_assert2(op_index <= 3);

    if (s->quarter_sample) {
        motion_x /= 2;
        motion_y /= 2;
    }

    sx = motion_x & s_mask;
    sy = motion_y & s_mask;
    src_x += motion_x >> lowres + 1;
    src_y += motion_y >> lowres + 1;

    src   += src_y * stride + src_x;

    if ((unsigned)src_x > FFMAX( h_edge_pos - (!!sx) - w,                 0) ||
        (unsigned)src_y > FFMAX((v_edge_pos >> field_based) - (!!sy) - h, 0)) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, src,
                                 s->linesize, s->linesize,
                                 w + 1, (h + 1) << field_based,
                                 src_x, src_y * (1 << field_based),
                                 h_edge_pos, v_edge_pos);
        src = s->sc.edge_emu_buffer;
        emu = 1;
    }

    sx = (sx << 2) >> lowres;
    sy = (sy << 2) >> lowres;
    if (field_select)
        src += s->linesize;
    pix_op[op_index](dest, src, stride, h, sx, sy);
    return emu;
}

/* apply one mpeg motion vector to the three components */
static av_always_inline void mpeg_motion_lowres(MpegEncContext *s,
                                                uint8_t *dest_y,
                                                uint8_t *dest_cb,
                                                uint8_t *dest_cr,
                                                int field_based,
                                                int bottom_field,
                                                int field_select,
                                                uint8_t *const *ref_picture,
                                                const h264_chroma_mc_func *pix_op,
                                                int motion_x, int motion_y,
                                                int h, int mb_y)
{
    const uint8_t *ptr_y, *ptr_cb, *ptr_cr;
    int mx, my, src_x, src_y, uvsrc_x, uvsrc_y, sx, sy, uvsx, uvsy;
    ptrdiff_t uvlinesize, linesize;
    const int lowres     = s->avctx->lowres;
    const int op_index   = lowres - 1 + s->chroma_x_shift;
    const int block_s    = 8 >> lowres;
    const int s_mask     = (2 << lowres) - 1;
    const int h_edge_pos = s->h_edge_pos >> lowres;
    const int v_edge_pos = s->v_edge_pos >> lowres;
    int hc = s->chroma_y_shift ? (h+1-bottom_field)>>1 : h;

    av_assert2(op_index <= 3);

    linesize   = s->cur_pic.linesize[0] << field_based;
    uvlinesize = s->cur_pic.linesize[1] << field_based;

    // FIXME obviously not perfect but qpel will not work in lowres anyway
    if (s->quarter_sample) {
        motion_x /= 2;
        motion_y /= 2;
    }

    if (field_based) {
        motion_y += (bottom_field - field_select)*((1 << lowres)-1);
    }

    sx = motion_x & s_mask;
    sy = motion_y & s_mask;
    src_x = s->mb_x * 2 * block_s + (motion_x >> lowres + 1);
    src_y = (mb_y * 2 * block_s >> field_based) + (motion_y >> lowres + 1);

    if (s->out_format == FMT_H263) {
        uvsx    = ((motion_x >> 1) & s_mask) | (sx & 1);
        uvsy    = ((motion_y >> 1) & s_mask) | (sy & 1);
        uvsrc_x = src_x >> 1;
        uvsrc_y = src_y >> 1;
    } else if (s->out_format == FMT_H261) {
        // even chroma mv's are full pel in H261
        mx      = motion_x / 4;
        my      = motion_y / 4;
        uvsx    = (2 * mx) & s_mask;
        uvsy    = (2 * my) & s_mask;
        uvsrc_x = s->mb_x * block_s + (mx >> lowres);
        uvsrc_y =    mb_y * block_s + (my >> lowres);
    } else {
        if (s->chroma_y_shift) {
            mx      = motion_x / 2;
            my      = motion_y / 2;
            uvsx    = mx & s_mask;
            uvsy    = my & s_mask;
            uvsrc_x = s->mb_x * block_s                 + (mx >> lowres + 1);
            uvsrc_y =   (mb_y * block_s >> field_based) + (my >> lowres + 1);
        } else {
            if (s->chroma_x_shift) {
            //Chroma422
                mx = motion_x / 2;
                uvsx = mx & s_mask;
                uvsy = motion_y & s_mask;
                uvsrc_y = src_y;
                uvsrc_x = s->mb_x*block_s               + (mx >> (lowres+1));
            } else {
            //Chroma444
                uvsx = motion_x & s_mask;
                uvsy = motion_y & s_mask;
                uvsrc_x = src_x;
                uvsrc_y = src_y;
            }
        }
    }

    ptr_y  = ref_picture[0] + src_y   * linesize   + src_x;
    ptr_cb = ref_picture[1] + uvsrc_y * uvlinesize + uvsrc_x;
    ptr_cr = ref_picture[2] + uvsrc_y * uvlinesize + uvsrc_x;

    if ((unsigned) src_x > FFMAX( h_edge_pos - (!!sx) - 2 * block_s,       0) || uvsrc_y<0 ||
        (unsigned) src_y > FFMAX((v_edge_pos >> field_based) - (!!sy) - FFMAX(h, hc<<s->chroma_y_shift), 0)) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, ptr_y,
                                 linesize >> field_based, linesize >> field_based,
                                 17, 17 + field_based,
                                src_x, src_y * (1 << field_based), h_edge_pos,
                                v_edge_pos);
        ptr_y = s->sc.edge_emu_buffer;
        if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
            uint8_t *ubuf = s->sc.edge_emu_buffer + 18 * s->linesize;
            uint8_t *vbuf =ubuf + 10 * s->uvlinesize;
            if (s->workaround_bugs & FF_BUG_IEDGE)
                vbuf -= s->uvlinesize;
            s->vdsp.emulated_edge_mc(ubuf,  ptr_cb,
                                     uvlinesize >> field_based, uvlinesize >> field_based,
                                     9, 9 + field_based,
                                    uvsrc_x, uvsrc_y * (1 << field_based),
                                    h_edge_pos >> 1, v_edge_pos >> 1);
            s->vdsp.emulated_edge_mc(vbuf,  ptr_cr,
                                     uvlinesize >> field_based,uvlinesize >> field_based,
                                     9, 9 + field_based,
                                    uvsrc_x, uvsrc_y * (1 << field_based),
                                    h_edge_pos >> 1, v_edge_pos >> 1);
            ptr_cb = ubuf;
            ptr_cr = vbuf;
        }
    }

    // FIXME use this for field pix too instead of the obnoxious hack which changes picture.f->data
    if (bottom_field) {
        dest_y  += s->linesize;
        dest_cb += s->uvlinesize;
        dest_cr += s->uvlinesize;
    }

    if (field_select) {
        ptr_y   += s->linesize;
        ptr_cb  += s->uvlinesize;
        ptr_cr  += s->uvlinesize;
    }

    sx = (sx << 2) >> lowres;
    sy = (sy << 2) >> lowres;
    pix_op[lowres - 1](dest_y, ptr_y, linesize, h, sx, sy);

    if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
        uvsx = (uvsx << 2) >> lowres;
        uvsy = (uvsy << 2) >> lowres;
        if (hc) {
            pix_op[op_index](dest_cb, ptr_cb, uvlinesize, hc, uvsx, uvsy);
            pix_op[op_index](dest_cr, ptr_cr, uvlinesize, hc, uvsx, uvsy);
        }
    }
    // FIXME h261 lowres loop filter
}

static inline void chroma_4mv_motion_lowres(MpegEncContext *s,
                                            uint8_t *dest_cb, uint8_t *dest_cr,
                                            uint8_t *const *ref_picture,
                                            const h264_chroma_mc_func * pix_op,
                                            int mx, int my)
{
    const int lowres     = s->avctx->lowres;
    const int op_index   = lowres;
    const int block_s    = 8 >> lowres;
    const int s_mask     = (2 << lowres) - 1;
    const int h_edge_pos = s->h_edge_pos >> lowres + 1;
    const int v_edge_pos = s->v_edge_pos >> lowres + 1;
    int emu = 0, src_x, src_y, sx, sy;
    ptrdiff_t offset;
    const uint8_t *ptr;

    av_assert2(op_index <= 3);

    if (s->quarter_sample) {
        mx /= 2;
        my /= 2;
    }

    /* In case of 8X8, we construct a single chroma motion vector
       with a special rounding */
    mx = ff_h263_round_chroma(mx);
    my = ff_h263_round_chroma(my);

    sx = mx & s_mask;
    sy = my & s_mask;
    src_x = s->mb_x * block_s + (mx >> lowres + 1);
    src_y = s->mb_y * block_s + (my >> lowres + 1);

    offset = src_y * s->uvlinesize + src_x;
    ptr = ref_picture[1] + offset;
    if ((unsigned) src_x > FFMAX(h_edge_pos - (!!sx) - block_s, 0) ||
        (unsigned) src_y > FFMAX(v_edge_pos - (!!sy) - block_s, 0)) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, ptr,
                                 s->uvlinesize, s->uvlinesize,
                                 9, 9,
                                 src_x, src_y, h_edge_pos, v_edge_pos);
        ptr = s->sc.edge_emu_buffer;
        emu = 1;
    }
    sx = (sx << 2) >> lowres;
    sy = (sy << 2) >> lowres;
    pix_op[op_index](dest_cb, ptr, s->uvlinesize, block_s, sx, sy);

    ptr = ref_picture[2] + offset;
    if (emu) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, ptr,
                                 s->uvlinesize, s->uvlinesize,
                                 9, 9,
                                 src_x, src_y, h_edge_pos, v_edge_pos);
        ptr = s->sc.edge_emu_buffer;
    }
    pix_op[op_index](dest_cr, ptr, s->uvlinesize, block_s, sx, sy);
}

/**
 * motion compensation of a single macroblock
 * @param s context
 * @param dest_y luma destination pointer
 * @param dest_cb chroma cb/u destination pointer
 * @param dest_cr chroma cr/v destination pointer
 * @param dir direction (0->forward, 1->backward)
 * @param ref_picture array[3] of pointers to the 3 planes of the reference picture
 * @param pix_op halfpel motion compensation function (average or put normally)
 * the motion vectors are taken from s->mv and the MV type from s->mv_type
 */
static inline void MPV_motion_lowres(MpegEncContext *s,
                                     uint8_t *dest_y, uint8_t *dest_cb,
                                     uint8_t *dest_cr,
                                     int dir, uint8_t *const *ref_picture,
                                     const h264_chroma_mc_func *pix_op)
{
    int mx, my;
    int mb_x, mb_y;
    const int lowres  = s->avctx->lowres;
    const int block_s = 8 >>lowres;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    switch (s->mv_type) {
    case MV_TYPE_16X16:
        mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                           0, 0, 0,
                           ref_picture, pix_op,
                           s->mv[dir][0][0], s->mv[dir][0][1],
                           2 * block_s, mb_y);
        break;
    case MV_TYPE_8X8:
        mx = 0;
        my = 0;
        for (int i = 0; i < 4; i++) {
            hpel_motion_lowres(s, dest_y + ((i & 1) + (i >> 1) *
                               s->linesize) * block_s,
                               ref_picture[0], 0, 0,
                               (2 * mb_x + (i & 1)) * block_s,
                               (2 * mb_y + (i >> 1)) * block_s,
                               s->width, s->height, s->linesize,
                               s->h_edge_pos >> lowres, s->v_edge_pos >> lowres,
                               block_s, block_s, pix_op,
                               s->mv[dir][i][0], s->mv[dir][i][1]);

            mx += s->mv[dir][i][0];
            my += s->mv[dir][i][1];
        }

        if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY))
            chroma_4mv_motion_lowres(s, dest_cb, dest_cr, ref_picture,
                                     pix_op, mx, my);
        break;
    case MV_TYPE_FIELD:
        if (s->picture_structure == PICT_FRAME) {
            /* top field */
            mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                               1, 0, s->field_select[dir][0],
                               ref_picture, pix_op,
                               s->mv[dir][0][0], s->mv[dir][0][1],
                               block_s, mb_y);
            /* bottom field */
            mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                               1, 1, s->field_select[dir][1],
                               ref_picture, pix_op,
                               s->mv[dir][1][0], s->mv[dir][1][1],
                               block_s, mb_y);
        } else {
            if (s->picture_structure != s->field_select[dir][0] + 1 &&
                s->pict_type != AV_PICTURE_TYPE_B && !s->first_field) {
                ref_picture = s->cur_pic.ptr->f->data;
            }
            mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                               0, 0, s->field_select[dir][0],
                               ref_picture, pix_op,
                               s->mv[dir][0][0],
                               s->mv[dir][0][1], 2 * block_s, mb_y >> 1);
            }
        break;
    case MV_TYPE_16X8:
        for (int i = 0; i < 2; i++) {
            uint8_t *const *ref2picture;

            if (s->picture_structure == s->field_select[dir][i] + 1 ||
                s->pict_type == AV_PICTURE_TYPE_B || s->first_field) {
                ref2picture = ref_picture;
            } else {
                ref2picture = s->cur_pic.ptr->f->data;
            }

            mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                               0, 0, s->field_select[dir][i],
                               ref2picture, pix_op,
                               s->mv[dir][i][0], s->mv[dir][i][1] +
                               2 * block_s * i, block_s, mb_y >> 1);

            dest_y  +=  2 * block_s *  s->linesize;
            dest_cb += (2 * block_s >> s->chroma_y_shift) * s->uvlinesize;
            dest_cr += (2 * block_s >> s->chroma_y_shift) * s->uvlinesize;
        }
        break;
    case MV_TYPE_DMV:
        if (s->picture_structure == PICT_FRAME) {
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                                       1, j, j ^ i,
                                       ref_picture, pix_op,
                                       s->mv[dir][2 * i + j][0],
                                       s->mv[dir][2 * i + j][1],
                                       block_s, mb_y);
                }
                pix_op = s->h264chroma.avg_h264_chroma_pixels_tab;
            }
        } else {
            for (int i = 0; i < 2; i++) {
                mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                                   0, 0, s->picture_structure != i + 1,
                                   ref_picture, pix_op,
                                   s->mv[dir][2 * i][0],s->mv[dir][2 * i][1],
                                   2 * block_s, mb_y >> 1);

                // after put we make avg of the same block
                pix_op = s->h264chroma.avg_h264_chroma_pixels_tab;

                // opposite parity is always in the same
                // frame if this is second field
                if (!s->first_field) {
                    ref_picture = s->cur_pic.ptr->f->data;
                }
            }
        }
        break;
    default:
        av_unreachable("No other mpegvideo MV types exist");
    }
}

/**
 * find the lowest MB row referenced in the MVs
 */
static int lowest_referenced_row(MpegEncContext *s, int dir)
{
    int my_max = INT_MIN, my_min = INT_MAX, qpel_shift = !s->quarter_sample;
    int off, mvs;

    if (s->picture_structure != PICT_FRAME || s->mcsel)
        goto unhandled;

    switch (s->mv_type) {
        case MV_TYPE_16X16:
            mvs = 1;
            break;
        case MV_TYPE_16X8:
            mvs = 2;
            break;
        case MV_TYPE_8X8:
            mvs = 4;
            break;
        default:
            goto unhandled;
    }

    for (int i = 0; i < mvs; i++) {
        int my = s->mv[dir][i][1];
        my_max = FFMAX(my_max, my);
        my_min = FFMIN(my_min, my);
    }

    off = ((FFMAX(-my_min, my_max) << qpel_shift) + 63) >> 6;

    return av_clip(s->mb_y + off, 0, s->mb_height - 1);
unhandled:
    return s->mb_height - 1;
}

/* add block[] to dest[] */
static inline void add_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size)
{
    if (s->block_last_index[i] >= 0) {
        s->idsp.idct_add(dest, line_size, block);
    }
}

/* put block[] to dest[] */
static inline void put_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size, int qscale)
{
    s->dct_unquantize_intra(s, block, i, qscale);
    s->idsp.idct_put(dest, line_size, block);
}

static inline void add_dequant_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size, int qscale)
{
    if (s->block_last_index[i] >= 0) {
        s->dct_unquantize_inter(s, block, i, qscale);

        s->idsp.idct_add(dest, line_size, block);
    }
}

#define NOT_MPEG12_H261        0
#define MAY_BE_MPEG12_H261     1
#define DEFINITELY_MPEG12_H261 2

/* generic function called after a macroblock has been parsed by the decoder.

   Important variables used:
   s->mb_intra : true if intra macroblock
   s->mv_dir   : motion vector direction
   s->mv_type  : motion vector type
   s->mv       : motion vector
   s->interlaced_dct : true if interlaced dct used (mpeg2)
 */
static av_always_inline
void mpv_reconstruct_mb_internal(MpegEncContext *s, int16_t block[12][64],
                                 int lowres_flag, int is_mpeg12)
{
#define IS_MPEG12_H261(s) (is_mpeg12 == MAY_BE_MPEG12_H261 ? ((s)->out_format <= FMT_H261) : is_mpeg12)
    uint8_t *dest_y = s->dest[0], *dest_cb = s->dest[1], *dest_cr = s->dest[2];
    int dct_linesize, dct_offset;
    const int linesize   = s->cur_pic.linesize[0]; //not s->linesize as this would be wrong for field pics
    const int uvlinesize = s->cur_pic.linesize[1];
    const int block_size = lowres_flag ? 8 >> s->avctx->lowres : 8;

    dct_linesize = linesize << s->interlaced_dct;
    dct_offset   = s->interlaced_dct ? linesize : linesize * block_size;

    if (!s->mb_intra) {
        /* motion handling */
        if (HAVE_THREADS && is_mpeg12 != DEFINITELY_MPEG12_H261 &&
            s->avctx->active_thread_type & FF_THREAD_FRAME) {
            if (s->mv_dir & MV_DIR_FORWARD) {
                ff_thread_progress_await(&s->last_pic.ptr->progress,
                                         lowest_referenced_row(s, 0));
            }
            if (s->mv_dir & MV_DIR_BACKWARD) {
                ff_thread_progress_await(&s->next_pic.ptr->progress,
                                         lowest_referenced_row(s, 1));
            }
        }

        if (lowres_flag) {
            const h264_chroma_mc_func *op_pix = s->h264chroma.put_h264_chroma_pixels_tab;

            if (s->mv_dir & MV_DIR_FORWARD) {
                MPV_motion_lowres(s, dest_y, dest_cb, dest_cr, 0, s->last_pic.data, op_pix);
                op_pix = s->h264chroma.avg_h264_chroma_pixels_tab;
            }
            if (s->mv_dir & MV_DIR_BACKWARD) {
                MPV_motion_lowres(s, dest_y, dest_cb, dest_cr, 1, s->next_pic.data, op_pix);
            }
        } else {
            const op_pixels_func (*op_pix)[4];
            const qpel_mc_func (*op_qpix)[16];

            if ((is_mpeg12 == DEFINITELY_MPEG12_H261 || !s->no_rounding) || s->pict_type == AV_PICTURE_TYPE_B) {
                op_pix  = s->hdsp.put_pixels_tab;
                op_qpix = s->qdsp.put_qpel_pixels_tab;
            } else {
                op_pix  = s->hdsp.put_no_rnd_pixels_tab;
                op_qpix = s->qdsp.put_no_rnd_qpel_pixels_tab;
            }
            if (s->mv_dir & MV_DIR_FORWARD) {
                ff_mpv_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_pic.data, op_pix, op_qpix);
                op_pix  = s->hdsp.avg_pixels_tab;
                op_qpix = s->qdsp.avg_qpel_pixels_tab;
            }
            if (s->mv_dir & MV_DIR_BACKWARD) {
                ff_mpv_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_pic.data, op_pix, op_qpix);
            }
        }

        /* skip dequant / idct if we are really late ;) */
        if (s->avctx->skip_idct) {
            if (  (s->avctx->skip_idct >= AVDISCARD_NONREF && s->pict_type == AV_PICTURE_TYPE_B)
                ||(s->avctx->skip_idct >= AVDISCARD_NONKEY && s->pict_type != AV_PICTURE_TYPE_I)
                || s->avctx->skip_idct >= AVDISCARD_ALL)
                return;
        }

        /* add dct residue */
        if (is_mpeg12 != DEFINITELY_MPEG12_H261 && s->dct_unquantize_inter) {
            // H.263, H.263+, H.263I, FLV, RV10, RV20 and MPEG-4 with MPEG-2 quantization
            add_dequant_dct(s, block[0], 0, dest_y                          , dct_linesize, s->qscale);
            add_dequant_dct(s, block[1], 1, dest_y              + block_size, dct_linesize, s->qscale);
            add_dequant_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize, s->qscale);
            add_dequant_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize, s->qscale);

            if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                av_assert2(s->chroma_y_shift);
                add_dequant_dct(s, block[4], 4, dest_cb, uvlinesize, s->chroma_qscale);
                add_dequant_dct(s, block[5], 5, dest_cr, uvlinesize, s->chroma_qscale);
            }
        } else if (is_mpeg12 == DEFINITELY_MPEG12_H261 || lowres_flag || (s->codec_id != AV_CODEC_ID_WMV2)) {
            // H.261, MPEG-1, MPEG-2, MPEG-4 with H.263 quantization,
            // MSMP4V1-3 and WMV1.
            // Also RV30, RV40 and the VC-1 family when performing error resilience,
            // but all blocks are skipped in this case.
            add_dct(s, block[0], 0, dest_y                          , dct_linesize);
            add_dct(s, block[1], 1, dest_y              + block_size, dct_linesize);
            add_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize);
            add_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize);

            if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                if (s->chroma_y_shift) {//Chroma420
                    add_dct(s, block[4], 4, dest_cb, uvlinesize);
                    add_dct(s, block[5], 5, dest_cr, uvlinesize);
                } else {
                    //chroma422
                    dct_linesize = uvlinesize << s->interlaced_dct;
                    dct_offset   = s->interlaced_dct ? uvlinesize : uvlinesize*block_size;

                    add_dct(s, block[4], 4, dest_cb, dct_linesize);
                    add_dct(s, block[5], 5, dest_cr, dct_linesize);
                    add_dct(s, block[6], 6, dest_cb + dct_offset, dct_linesize);
                    add_dct(s, block[7], 7, dest_cr + dct_offset, dct_linesize);
                    if (!s->chroma_x_shift) {//Chroma444
                        add_dct(s, block[8],   8, dest_cb + block_size, dct_linesize);
                        add_dct(s, block[9],   9, dest_cr + block_size, dct_linesize);
                        add_dct(s, block[10], 10, dest_cb + block_size + dct_offset, dct_linesize);
                        add_dct(s, block[11], 11, dest_cr + block_size + dct_offset, dct_linesize);
                    }
                }
            } //fi gray
        } else if (CONFIG_WMV2_DECODER) {
            ff_wmv2_add_mb(s, block, dest_y, dest_cb, dest_cr);
        }
    } else {
        /* Only MPEG-4 Simple Studio Profile is supported in > 8-bit mode.
            TODO: Integrate 10-bit properly into mpegvideo.c so that ER works properly */
        if (is_mpeg12 != DEFINITELY_MPEG12_H261 && CONFIG_MPEG4_DECODER &&
            /* s->codec_id == AV_CODEC_ID_MPEG4 && */
            s->avctx->bits_per_raw_sample > 8) {
            ff_mpeg4_decode_studio(s, dest_y, dest_cb, dest_cr, block_size,
                                    uvlinesize, dct_linesize, dct_offset);
        } else if (!IS_MPEG12_H261(s)) {
            /* dct only in intra block */
            put_dct(s, block[0], 0, dest_y                          , dct_linesize, s->qscale);
            put_dct(s, block[1], 1, dest_y              + block_size, dct_linesize, s->qscale);
            put_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize, s->qscale);
            put_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize, s->qscale);

            if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                if (s->chroma_y_shift) {
                    put_dct(s, block[4], 4, dest_cb, uvlinesize, s->chroma_qscale);
                    put_dct(s, block[5], 5, dest_cr, uvlinesize, s->chroma_qscale);
                } else {
                    dct_offset   >>= 1;
                    dct_linesize >>= 1;
                    put_dct(s, block[4], 4, dest_cb,              dct_linesize, s->chroma_qscale);
                    put_dct(s, block[5], 5, dest_cr,              dct_linesize, s->chroma_qscale);
                    put_dct(s, block[6], 6, dest_cb + dct_offset, dct_linesize, s->chroma_qscale);
                    put_dct(s, block[7], 7, dest_cr + dct_offset, dct_linesize, s->chroma_qscale);
                }
            }
        } else {
            s->idsp.idct_put(dest_y,                           dct_linesize, block[0]);
            s->idsp.idct_put(dest_y              + block_size, dct_linesize, block[1]);
            s->idsp.idct_put(dest_y + dct_offset,              dct_linesize, block[2]);
            s->idsp.idct_put(dest_y + dct_offset + block_size, dct_linesize, block[3]);

            if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                if (s->chroma_y_shift) {
                    s->idsp.idct_put(dest_cb, uvlinesize, block[4]);
                    s->idsp.idct_put(dest_cr, uvlinesize, block[5]);
                } else {
                    dct_linesize = uvlinesize << s->interlaced_dct;
                    dct_offset   = s->interlaced_dct ? uvlinesize : uvlinesize*block_size;

                    s->idsp.idct_put(dest_cb,              dct_linesize, block[4]);
                    s->idsp.idct_put(dest_cr,              dct_linesize, block[5]);
                    s->idsp.idct_put(dest_cb + dct_offset, dct_linesize, block[6]);
                    s->idsp.idct_put(dest_cr + dct_offset, dct_linesize, block[7]);
                    if (!s->chroma_x_shift) { //Chroma444
                        s->idsp.idct_put(dest_cb + block_size,              dct_linesize, block[8]);
                        s->idsp.idct_put(dest_cr + block_size,              dct_linesize, block[9]);
                        s->idsp.idct_put(dest_cb + block_size + dct_offset, dct_linesize, block[10]);
                        s->idsp.idct_put(dest_cr + block_size + dct_offset, dct_linesize, block[11]);
                    }
                }
            } //gray
        }
    }
}

void ff_mpv_reconstruct_mb(MpegEncContext *s, int16_t block[12][64])
{
    const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;
    uint8_t *mbskip_ptr = &s->mbskip_table[mb_xy];

    s->cur_pic.qscale_table[mb_xy] = s->qscale;

    /* avoid copy if macroblock skipped in last frame too */
    if (s->mb_skipped) {
        s->mb_skipped = 0;
        av_assert2(s->pict_type != AV_PICTURE_TYPE_I);
        *mbskip_ptr = 1;
    } else if (!s->cur_pic.reference) {
        *mbskip_ptr = 1;
    } else{
        *mbskip_ptr = 0; /* not skipped */
    }

    if (s->avctx->debug & FF_DEBUG_DCT_COEFF) {
       /* print DCT coefficients */
       av_log(s->avctx, AV_LOG_DEBUG, "DCT coeffs of MB at %dx%d:\n", s->mb_x, s->mb_y);
       for (int i = 0; i < 6; i++) {
           for (int j = 0; j < 64; j++) {
               av_log(s->avctx, AV_LOG_DEBUG, "%5d",
                      block[i][s->idsp.idct_permutation[j]]);
           }
           av_log(s->avctx, AV_LOG_DEBUG, "\n");
       }
    }

    av_assert2((s->out_format <= FMT_H261) == (s->out_format == FMT_H261 || s->out_format == FMT_MPEG1));
    if (!s->avctx->lowres) {
#if !CONFIG_SMALL
        if (s->out_format <= FMT_H261)
            mpv_reconstruct_mb_internal(s, block, 0, DEFINITELY_MPEG12_H261);
        else
            mpv_reconstruct_mb_internal(s, block, 0, NOT_MPEG12_H261);
#else
        mpv_reconstruct_mb_internal(s, block, 0, MAY_BE_MPEG12_H261);
#endif
    } else
        mpv_reconstruct_mb_internal(s, block, 1, MAY_BE_MPEG12_H261);
}
