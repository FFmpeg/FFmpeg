/*
 * H.264 encoding using the x264 library
 * Copyright (C) 2005  Mans Rullgard <mans@mansr.com>
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
#include <x264.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct X264Context {
    x264_param_t params;
    x264_t *enc;
    x264_picture_t pic;
    AVFrame out_pic;
} X264Context;

static void
X264_log(void *p, int level, const char *fmt, va_list args)
{
    static const int level_map[] = {
        [X264_LOG_ERROR]   = AV_LOG_ERROR,
        [X264_LOG_WARNING] = AV_LOG_WARNING,
        [X264_LOG_INFO]    = AV_LOG_INFO,
        [X264_LOG_DEBUG]   = AV_LOG_DEBUG
    };

    if(level < 0 || level > X264_LOG_DEBUG)
        return;

    av_vlog(p, level_map[level], fmt, args);
}


static int
encode_nals(uint8_t *buf, int size, x264_nal_t *nals, int nnal)
{
    uint8_t *p = buf;
    int i;

    for(i = 0; i < nnal; i++){
        int s = x264_nal_encode(p, &size, 1, nals + i);
        if(s < 0)
            return -1;
        p += s;
    }

    return p - buf;
}

static int
X264_frame(AVCodecContext *ctx, uint8_t *buf, int bufsize, void *data)
{
    X264Context *x4 = ctx->priv_data;
    AVFrame *frame = data;
    x264_nal_t *nal;
    int nnal, i;
    x264_picture_t pic_out;

    x4->pic.img.i_csp = X264_CSP_I420;
    x4->pic.img.i_plane = 3;

    if (frame) {
        for(i = 0; i < 3; i++){
            x4->pic.img.plane[i] = frame->data[i];
            x4->pic.img.i_stride[i] = frame->linesize[i];
        }

        x4->pic.i_pts = frame->pts;
        x4->pic.i_type = X264_TYPE_AUTO;
    }

    if(x264_encoder_encode(x4->enc, &nal, &nnal, frame? &x4->pic: NULL,
                           &pic_out))
        return -1;

    bufsize = encode_nals(buf, bufsize, nal, nnal);
    if(bufsize < 0)
        return -1;

    /* FIXME: dts */
    x4->out_pic.pts = pic_out.i_pts;

    switch(pic_out.i_type){
    case X264_TYPE_IDR:
    case X264_TYPE_I:
        x4->out_pic.pict_type = FF_I_TYPE;
        break;
    case X264_TYPE_P:
        x4->out_pic.pict_type = FF_P_TYPE;
        break;
    case X264_TYPE_B:
    case X264_TYPE_BREF:
        x4->out_pic.pict_type = FF_B_TYPE;
        break;
    }

    x4->out_pic.key_frame = pic_out.i_type == X264_TYPE_IDR;
    x4->out_pic.quality = (pic_out.i_qpplus1 - 1) * FF_QP2LAMBDA;

    return bufsize;
}

static av_cold int
X264_close(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;

    if(x4->enc)
        x264_encoder_close(x4->enc);

    return 0;
}

static av_cold int
X264_init(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;

    x264_param_default(&x4->params);

    x4->params.pf_log = X264_log;
    x4->params.p_log_private = avctx;

    x4->params.i_keyint_max = avctx->gop_size;
    x4->params.rc.i_bitrate = avctx->bit_rate / 1000;
    x4->params.rc.i_vbv_buffer_size = avctx->rc_buffer_size / 1000;
    x4->params.rc.i_vbv_max_bitrate = avctx->rc_max_rate / 1000;
    x4->params.rc.b_stat_write = (avctx->flags & CODEC_FLAG_PASS1);
    if(avctx->flags & CODEC_FLAG_PASS2) x4->params.rc.b_stat_read = 1;
    else{
        if(avctx->crf){
            x4->params.rc.i_rc_method = X264_RC_CRF;
            x4->params.rc.f_rf_constant = avctx->crf;
        }else if(avctx->cqp > -1){
            x4->params.rc.i_rc_method = X264_RC_CQP;
            x4->params.rc.i_qp_constant = avctx->cqp;
        }
    }

    // if neither crf nor cqp modes are selected we have to enable the RC
    // we do it this way because we cannot check if the bitrate has been set
    if(!(avctx->crf || (avctx->cqp > -1))) x4->params.rc.i_rc_method = X264_RC_ABR;

    x4->params.i_bframe = avctx->max_b_frames;
    x4->params.b_cabac = avctx->coder_type == FF_CODER_TYPE_AC;
    x4->params.b_bframe_adaptive = avctx->b_frame_strategy;
    x4->params.i_bframe_bias = avctx->bframebias;
    x4->params.b_bframe_pyramid = (avctx->flags2 & CODEC_FLAG2_BPYRAMID);
    avctx->has_b_frames= (avctx->flags2 & CODEC_FLAG2_BPYRAMID) ? 2 : !!avctx->max_b_frames;

    x4->params.i_keyint_min = avctx->keyint_min;
    if(x4->params.i_keyint_min > x4->params.i_keyint_max)
        x4->params.i_keyint_min = x4->params.i_keyint_max;

    x4->params.i_scenecut_threshold = avctx->scenechange_threshold;

    x4->params.b_deblocking_filter = (avctx->flags & CODEC_FLAG_LOOP_FILTER);
    x4->params.i_deblocking_filter_alphac0 = avctx->deblockalpha;
    x4->params.i_deblocking_filter_beta = avctx->deblockbeta;

    x4->params.rc.i_qp_min = avctx->qmin;
    x4->params.rc.i_qp_max = avctx->qmax;
    x4->params.rc.i_qp_step = avctx->max_qdiff;

    x4->params.rc.f_qcompress = avctx->qcompress;  /* 0.0 => cbr, 1.0 => constant qp */
    x4->params.rc.f_qblur = avctx->qblur;        /* temporally blur quants */
    x4->params.rc.f_complexity_blur = avctx->complexityblur;

    x4->params.i_frame_reference = avctx->refs;

    x4->params.i_width = avctx->width;
    x4->params.i_height = avctx->height;
    x4->params.vui.i_sar_width = avctx->sample_aspect_ratio.num;
    x4->params.vui.i_sar_height = avctx->sample_aspect_ratio.den;
    x4->params.i_fps_num = avctx->time_base.den;
    x4->params.i_fps_den = avctx->time_base.num;

    x4->params.analyse.inter = 0;
    if(avctx->partitions){
        if(avctx->partitions & X264_PART_I4X4)
            x4->params.analyse.inter |= X264_ANALYSE_I4x4;
        if(avctx->partitions & X264_PART_I8X8)
            x4->params.analyse.inter |= X264_ANALYSE_I8x8;
        if(avctx->partitions & X264_PART_P8X8)
            x4->params.analyse.inter |= X264_ANALYSE_PSUB16x16;
        if(avctx->partitions & X264_PART_P4X4)
            x4->params.analyse.inter |= X264_ANALYSE_PSUB8x8;
        if(avctx->partitions & X264_PART_B8X8)
            x4->params.analyse.inter |= X264_ANALYSE_BSUB16x16;
    }

    x4->params.analyse.i_direct_mv_pred = avctx->directpred;

    x4->params.analyse.b_weighted_bipred = (avctx->flags2 & CODEC_FLAG2_WPRED);

    if(avctx->me_method == ME_EPZS)
        x4->params.analyse.i_me_method = X264_ME_DIA;
    else if(avctx->me_method == ME_HEX)
        x4->params.analyse.i_me_method = X264_ME_HEX;
    else if(avctx->me_method == ME_UMH)
        x4->params.analyse.i_me_method = X264_ME_UMH;
    else if(avctx->me_method == ME_FULL)
        x4->params.analyse.i_me_method = X264_ME_ESA;
    else x4->params.analyse.i_me_method = X264_ME_HEX;

    x4->params.analyse.i_me_range = avctx->me_range;
    x4->params.analyse.i_subpel_refine = avctx->me_subpel_quality;

    x4->params.analyse.b_bidir_me = (avctx->bidir_refine > 0);
    x4->params.analyse.b_bframe_rdo = (avctx->flags2 & CODEC_FLAG2_BRDO);
    x4->params.analyse.b_mixed_references =
        (avctx->flags2 & CODEC_FLAG2_MIXED_REFS);
    x4->params.analyse.b_chroma_me = (avctx->me_cmp & FF_CMP_CHROMA);
    x4->params.analyse.b_transform_8x8 = (avctx->flags2 & CODEC_FLAG2_8X8DCT);
    x4->params.analyse.b_fast_pskip = (avctx->flags2 & CODEC_FLAG2_FASTPSKIP);

    x4->params.analyse.i_trellis = avctx->trellis;
    x4->params.analyse.i_noise_reduction = avctx->noise_reduction;

    if(avctx->level > 0) x4->params.i_level_idc = avctx->level;

    x4->params.rc.f_rate_tolerance =
        (float)avctx->bit_rate_tolerance/avctx->bit_rate;

    if((avctx->rc_buffer_size != 0) &&
            (avctx->rc_initial_buffer_occupancy <= avctx->rc_buffer_size)){
        x4->params.rc.f_vbv_buffer_init =
            (float)avctx->rc_initial_buffer_occupancy/avctx->rc_buffer_size;
    }
    else x4->params.rc.f_vbv_buffer_init = 0.9;

    x4->params.rc.f_ip_factor = 1/fabs(avctx->i_quant_factor);
    x4->params.rc.f_pb_factor = avctx->b_quant_factor;
    x4->params.analyse.i_chroma_qp_offset = avctx->chromaoffset;
    x4->params.rc.psz_rc_eq = avctx->rc_eq;

    x4->params.analyse.b_psnr = (avctx->flags & CODEC_FLAG_PSNR);
    x4->params.i_log_level = X264_LOG_DEBUG;

    x4->params.b_aud = (avctx->flags2 & CODEC_FLAG2_AUD);

    x4->params.i_threads = avctx->thread_count;

    if(avctx->flags & CODEC_FLAG_GLOBAL_HEADER){
        x4->params.b_repeat_headers = 0;
    }

    x4->enc = x264_encoder_open(&x4->params);
    if(!x4->enc)
        return -1;

    avctx->coded_frame = &x4->out_pic;

    if(avctx->flags & CODEC_FLAG_GLOBAL_HEADER){
        x264_nal_t *nal;
        int nnal, i, s = 0;

        x264_encoder_headers(x4->enc, &nal, &nnal);

        /* 5 bytes NAL header + worst case escaping */
        for(i = 0; i < nnal; i++)
            s += 5 + nal[i].i_payload * 4 / 3;

        avctx->extradata = av_malloc(s);
        avctx->extradata_size = encode_nals(avctx->extradata, s, nal, nnal);
    }

    return 0;
}

AVCodec libx264_encoder = {
    .name = "libx264",
    .type = CODEC_TYPE_VIDEO,
    .id = CODEC_ID_H264,
    .priv_data_size = sizeof(X264Context),
    .init = X264_init,
    .encode = X264_frame,
    .close = X264_close,
    .capabilities = CODEC_CAP_DELAY,
    .pix_fmts = (enum PixelFormat[]) { PIX_FMT_YUV420P, -1 }
};
