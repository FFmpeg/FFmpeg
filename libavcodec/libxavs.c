/*
 * AVS encoding using the xavs library
 * Copyright (C) 2010 Amanda, Y.N. Wu <amanda11192003@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <xavs.h>
#include "avcodec.h"

#define END_OF_STREAM 0x001

#define XAVS_PART_I8X8 0x002 /* Analyze i8x8 (requires 8x8 transform) */
#define XAVS_PART_P8X8 0x010 /* Analyze p16x8, p8x16 and p8x8 */
#define XAVS_PART_B8X8 0x100 /* Analyze b16x8, b*/

typedef struct XavsContext {
    xavs_param_t    params;
    xavs_t         *enc;
    xavs_picture_t  pic;
    uint8_t        *sei;
    int             sei_size;
    AVFrame         out_pic;
    int             end_of_stream;
} XavsContext;

static void XAVS_log(void *p, int level, const char *fmt, va_list args)
{
    static const int level_map[] = {
        [XAVS_LOG_ERROR]   = AV_LOG_ERROR,
        [XAVS_LOG_WARNING] = AV_LOG_WARNING,
        [XAVS_LOG_INFO]    = AV_LOG_INFO,
        [XAVS_LOG_DEBUG]   = AV_LOG_DEBUG
    };

    if (level < 0 || level > XAVS_LOG_DEBUG)
        return;

    av_vlog(p, level_map[level], fmt, args);
}

static int encode_nals(AVCodecContext *ctx, uint8_t *buf,
                       int size, xavs_nal_t *nals,
                       int nnal, int skip_sei)
{
    XavsContext *x4 = ctx->priv_data;
    uint8_t *p = buf;
    int i, s;

    /* Write the SEI as part of the first frame. */
    if (x4->sei_size > 0 && nnal > 0) {
        memcpy(p, x4->sei, x4->sei_size);
        p += x4->sei_size;
        x4->sei_size = 0;
    }

    for (i = 0; i < nnal; i++) {
        /* Don't put the SEI in extradata. */
        if (skip_sei && nals[i].i_type == NAL_SEI) {
            x4->sei = av_malloc( 5 + nals[i].i_payload * 4 / 3 );
            if (xavs_nal_encode(x4->sei, &x4->sei_size, 1, nals + i) < 0)
                return -1;

            continue;
        }
        s = xavs_nal_encode(p, &size, 1, nals + i);
        if (s < 0)
            return -1;
        p += s;
    }

    return p - buf;
}

static int XAVS_frame(AVCodecContext *ctx, uint8_t *buf,
                      int bufsize, void *data)
{
    XavsContext *x4 = ctx->priv_data;
    AVFrame *frame = data;
    xavs_nal_t *nal;
    int nnal, i;
    xavs_picture_t pic_out;

    x4->pic.img.i_csp   = XAVS_CSP_I420;
    x4->pic.img.i_plane = 3;

    if (frame) {
       for (i = 0; i < 3; i++) {
            x4->pic.img.plane[i] = frame->data[i];
            x4->pic.img.i_stride[i] = frame->linesize[i];
       }

        x4->pic.i_pts  = frame->pts;
        x4->pic.i_type = XAVS_TYPE_AUTO;
    }

    if (xavs_encoder_encode(x4->enc, &nal, &nnal,
                            frame? &x4->pic: NULL, &pic_out) < 0)
    return -1;

    bufsize = encode_nals(ctx, buf, bufsize, nal, nnal, 0);

    if (bufsize < 0)
        return -1;

    if (!bufsize && !frame && !(x4->end_of_stream)){
        buf[bufsize]   = 0x0;
        buf[bufsize+1] = 0x0;
        buf[bufsize+2] = 0x01;
        buf[bufsize+3] = 0xb1;
        bufsize += 4;
        x4->end_of_stream = END_OF_STREAM;
        return bufsize;
    }
    /* FIXME: libxavs now provides DTS */
    /* but AVFrame doesn't have a field for it. */
    x4->out_pic.pts = pic_out.i_pts;

    switch (pic_out.i_type) {
    case XAVS_TYPE_IDR:
    case XAVS_TYPE_I:
        x4->out_pic.pict_type = FF_I_TYPE;
        break;
    case XAVS_TYPE_P:
        x4->out_pic.pict_type = FF_P_TYPE;
        break;
    case XAVS_TYPE_B:
    case XAVS_TYPE_BREF:
        x4->out_pic.pict_type = FF_B_TYPE;
        break;
    }

    /* There is no IDR frame in AVS JiZhun */
    /* Sequence header is used as a flag */
    x4->out_pic.key_frame = pic_out.i_type == XAVS_TYPE_I;

    x4->out_pic.quality   = (pic_out.i_qpplus1 - 1) * FF_QP2LAMBDA;

    return bufsize;
}

static av_cold int XAVS_close(AVCodecContext *avctx)
{
    XavsContext *x4 = avctx->priv_data;

    av_freep(&avctx->extradata);
    av_free(x4->sei);

    if (x4->enc)
        xavs_encoder_close(x4->enc);

    return 0;
}

static av_cold int XAVS_init(AVCodecContext *avctx)
{
    XavsContext *x4 = avctx->priv_data;

    x4->sei_size = 0;
    xavs_param_default(&x4->params);

    x4->params.pf_log               = XAVS_log;
    x4->params.p_log_private        = avctx;
    x4->params.i_keyint_max         = avctx->gop_size;
    x4->params.rc.i_bitrate         = avctx->bit_rate       / 1000;
    x4->params.rc.i_vbv_buffer_size = avctx->rc_buffer_size / 1000;
    x4->params.rc.i_vbv_max_bitrate = avctx->rc_max_rate    / 1000;
    x4->params.rc.b_stat_write      = avctx->flags & CODEC_FLAG_PASS1;
    if (avctx->flags & CODEC_FLAG_PASS2) {
        x4->params.rc.b_stat_read = 1;
    } else {
        if (avctx->crf) {
            x4->params.rc.i_rc_method   = XAVS_RC_CRF;
            x4->params.rc.f_rf_constant = avctx->crf;
        } else if (avctx->cqp > -1) {
            x4->params.rc.i_rc_method   = XAVS_RC_CQP;
            x4->params.rc.i_qp_constant = avctx->cqp;
        }
    }

    /* if neither crf nor cqp modes are selected we have to enable the RC */
    /* we do it this way because we cannot check if the bitrate has been set */
    if (!(avctx->crf || (avctx->cqp > -1)))
        x4->params.rc.i_rc_method = XAVS_RC_ABR;

    x4->params.i_bframe          = avctx->max_b_frames;
    /* cabac is not included in AVS JiZhun Profile */
    x4->params.b_cabac           = 0;

    x4->params.i_bframe_adaptive = avctx->b_frame_strategy;
    x4->params.i_bframe_bias     = avctx->bframebias;

    avctx->has_b_frames          = !!avctx->max_b_frames;

    /* AVS doesn't allow B picture as reference */
    /* The max allowed reference frame number of B is 2 */
    x4->params.i_keyint_min      = avctx->keyint_min;
    if (x4->params.i_keyint_min > x4->params.i_keyint_max)
        x4->params.i_keyint_min = x4->params.i_keyint_max;

    x4->params.i_scenecut_threshold        = avctx->scenechange_threshold;

   // x4->params.b_deblocking_filter       = avctx->flags & CODEC_FLAG_LOOP_FILTER;
    x4->params.i_deblocking_filter_alphac0 = avctx->deblockalpha;
    x4->params.i_deblocking_filter_beta    = avctx->deblockbeta;

    x4->params.rc.i_qp_min                 = avctx->qmin;
    x4->params.rc.i_qp_max                 = avctx->qmax;
    x4->params.rc.i_qp_step                = avctx->max_qdiff;

    x4->params.rc.f_qcompress       = avctx->qcompress; /* 0.0 => cbr, 1.0 => constant qp */
    x4->params.rc.f_qblur           = avctx->qblur;     /* temporally blur quants */
    x4->params.rc.f_complexity_blur = avctx->complexityblur;

    x4->params.i_frame_reference    = avctx->refs;

    x4->params.i_width              = avctx->width;
    x4->params.i_height             = avctx->height;
    x4->params.vui.i_sar_width      = avctx->sample_aspect_ratio.num;
    x4->params.vui.i_sar_height     = avctx->sample_aspect_ratio.den;
    /* This is only used for counting the fps */
    x4->params.i_fps_num            = avctx->time_base.den;
    x4->params.i_fps_den            = avctx->time_base.num;
    x4->params.analyse.inter        = XAVS_ANALYSE_I8x8 |XAVS_ANALYSE_PSUB16x16| XAVS_ANALYSE_BSUB16x16;
    if (avctx->partitions) {
        if (avctx->partitions & XAVS_PART_I8X8)
            x4->params.analyse.inter |= XAVS_ANALYSE_I8x8;

        if (avctx->partitions & XAVS_PART_P8X8)
            x4->params.analyse.inter |= XAVS_ANALYSE_PSUB16x16;

        if (avctx->partitions & XAVS_PART_B8X8)
            x4->params.analyse.inter |= XAVS_ANALYSE_BSUB16x16;
    }

    x4->params.analyse.i_direct_mv_pred  = avctx->directpred;

    x4->params.analyse.b_weighted_bipred = avctx->flags2 & CODEC_FLAG2_WPRED;

    switch (avctx->me_method) {
         case  ME_EPZS:
               x4->params.analyse.i_me_method = XAVS_ME_DIA;
               break;
         case  ME_HEX:
               x4->params.analyse.i_me_method = XAVS_ME_HEX;
               break;
         case  ME_UMH:
               x4->params.analyse.i_me_method = XAVS_ME_UMH;
               break;
         case  ME_FULL:
               x4->params.analyse.i_me_method = XAVS_ME_ESA;
               break;
         case  ME_TESA:
               x4->params.analyse.i_me_method = XAVS_ME_TESA;
               break;
         default:
               x4->params.analyse.i_me_method = XAVS_ME_HEX;
    }

    x4->params.analyse.i_me_range = avctx->me_range;
    x4->params.analyse.i_subpel_refine    = avctx->me_subpel_quality;

    x4->params.analyse.b_mixed_references = avctx->flags2 & CODEC_FLAG2_MIXED_REFS;
    x4->params.analyse.b_chroma_me        = avctx->me_cmp & FF_CMP_CHROMA;
    /* AVS P2 only enables 8x8 transform */
    x4->params.analyse.b_transform_8x8    = 1; //avctx->flags2 & CODEC_FLAG2_8X8DCT;
    x4->params.analyse.b_fast_pskip       = avctx->flags2 & CODEC_FLAG2_FASTPSKIP;

    x4->params.analyse.i_trellis          = avctx->trellis;
    x4->params.analyse.i_noise_reduction  = avctx->noise_reduction;

    if (avctx->level > 0)
        x4->params.i_level_idc = avctx->level;

    x4->params.rc.f_rate_tolerance =
        (float)avctx->bit_rate_tolerance/avctx->bit_rate;

    if ((avctx->rc_buffer_size) &&
        (avctx->rc_initial_buffer_occupancy <= avctx->rc_buffer_size)) {
        x4->params.rc.f_vbv_buffer_init =
            (float)avctx->rc_initial_buffer_occupancy / avctx->rc_buffer_size;
    } else
        x4->params.rc.f_vbv_buffer_init = 0.9;

    /* TAG:do we have MB tree RC method */
    /* what is the RC method we are now using? Default NO */
    x4->params.rc.b_mb_tree               = !!(avctx->flags2 & CODEC_FLAG2_MBTREE);
    x4->params.rc.f_ip_factor             = 1 / fabs(avctx->i_quant_factor);
    x4->params.rc.f_pb_factor             = avctx->b_quant_factor;
    x4->params.analyse.i_chroma_qp_offset = avctx->chromaoffset;

    x4->params.analyse.b_psnr = avctx->flags & CODEC_FLAG_PSNR;
    x4->params.i_log_level    = XAVS_LOG_DEBUG;
    x4->params.b_aud          = avctx->flags2 & CODEC_FLAG2_AUD;
    x4->params.i_threads      = avctx->thread_count;
    x4->params.b_interlaced   = avctx->flags & CODEC_FLAG_INTERLACED_DCT;

    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER)
        x4->params.b_repeat_headers = 0;

    x4->enc = xavs_encoder_open(&x4->params);
    if (!x4->enc)
        return -1;

    avctx->coded_frame = &x4->out_pic;
    /* TAG: Do we have GLOBAL HEADER in AVS */
    /* We Have PPS and SPS in AVS */
    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER) {
        xavs_nal_t *nal;
        int nnal, s;

        s = xavs_encoder_headers(x4->enc, &nal, &nnal);

        avctx->extradata      = av_malloc(s);
        avctx->extradata_size = encode_nals(avctx, avctx->extradata, s, nal, nnal, 1);
    }
    return 0;
}

AVCodec libxavs_encoder = {
    .name           = "libxavs",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_CAVS,
    .priv_data_size = sizeof(XavsContext),
    .init           = XAVS_init,
    .encode         = XAVS_frame,
    .close          = XAVS_close,
    .capabilities   = CODEC_CAP_DELAY,
    .pix_fmts       = (const enum PixelFormat[]) { PIX_FMT_YUV420P, PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("libxavs - the Chinese Audio Video Standard Encoder"),
};

