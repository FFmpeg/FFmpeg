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

#include "libavutil/opt.h"
#include "avcodec.h"
#include <x264.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct X264Context {
    AVClass        *class;
    x264_param_t    params;
    x264_t         *enc;
    x264_picture_t  pic;
    uint8_t        *sei;
    int             sei_size;
    AVFrame         out_pic;
    char *preset;
    char *tune;
    char *profile;
    char *level;
    int fastfirstpass;
    char *stats;
    char *weightp;
    char *x264opts;
} X264Context;

static void X264_log(void *p, int level, const char *fmt, va_list args)
{
    static const int level_map[] = {
        [X264_LOG_ERROR]   = AV_LOG_ERROR,
        [X264_LOG_WARNING] = AV_LOG_WARNING,
        [X264_LOG_INFO]    = AV_LOG_INFO,
        [X264_LOG_DEBUG]   = AV_LOG_DEBUG
    };

    if (level < 0 || level > X264_LOG_DEBUG)
        return;

    av_vlog(p, level_map[level], fmt, args);
}


static int encode_nals(AVCodecContext *ctx, uint8_t *buf, int size,
                       x264_nal_t *nals, int nnal, int skip_sei)
{
    X264Context *x4 = ctx->priv_data;
    uint8_t *p = buf;
    int i;

    /* Write the SEI as part of the first frame. */
    if (x4->sei_size > 0 && nnal > 0) {
        if (x4->sei_size > size) {
            av_log(ctx, AV_LOG_ERROR, "Error: nal buffer is too small\n");
            return -1;
        }
        memcpy(p, x4->sei, x4->sei_size);
        p += x4->sei_size;
        x4->sei_size = 0;
        // why is x4->sei not freed?
    }

    for (i = 0; i < nnal; i++){
        /* Don't put the SEI in extradata. */
        if (skip_sei && nals[i].i_type == NAL_SEI) {
            x4->sei_size = nals[i].i_payload;
            x4->sei      = av_malloc(x4->sei_size);
            memcpy(x4->sei, nals[i].p_payload, nals[i].i_payload);
            continue;
        }
        if (nals[i].i_payload > (size - (p - buf))) {
            // return only complete nals which fit in buf
            av_log(ctx, AV_LOG_ERROR, "Error: nal buffer is too small\n");
            break;
        }
        memcpy(p, nals[i].p_payload, nals[i].i_payload);
        p += nals[i].i_payload;
    }

    return p - buf;
}

static int X264_frame(AVCodecContext *ctx, uint8_t *buf,
                      int bufsize, void *data)
{
    X264Context *x4 = ctx->priv_data;
    AVFrame *frame = data;
    x264_nal_t *nal;
    int nnal, i;
    x264_picture_t pic_out;

    x264_picture_init( &x4->pic );
    x4->pic.img.i_csp   = X264_CSP_I420;
    x4->pic.img.i_plane = 3;

    if (frame) {
        for (i = 0; i < 3; i++) {
            x4->pic.img.plane[i]    = frame->data[i];
            x4->pic.img.i_stride[i] = frame->linesize[i];
        }

        x4->pic.i_pts  = frame->pts;
        x4->pic.i_type =
            frame->pict_type == AV_PICTURE_TYPE_I ? X264_TYPE_KEYFRAME :
            frame->pict_type == AV_PICTURE_TYPE_P ? X264_TYPE_P :
            frame->pict_type == AV_PICTURE_TYPE_B ? X264_TYPE_B :
                                            X264_TYPE_AUTO;
        if (x4->params.b_tff != frame->top_field_first) {
            x4->params.b_tff = frame->top_field_first;
            x264_encoder_reconfig(x4->enc, &x4->params);
        }
        if (x4->params.vui.i_sar_height != ctx->sample_aspect_ratio.den
         || x4->params.vui.i_sar_width != ctx->sample_aspect_ratio.num) {
            x4->params.vui.i_sar_height = ctx->sample_aspect_ratio.den;
            x4->params.vui.i_sar_width = ctx->sample_aspect_ratio.num;
            x264_encoder_reconfig(x4->enc, &x4->params);
        }
    }

    do {
    if (x264_encoder_encode(x4->enc, &nal, &nnal, frame? &x4->pic: NULL, &pic_out) < 0)
        return -1;

    bufsize = encode_nals(ctx, buf, bufsize, nal, nnal, 0);
    if (bufsize < 0)
        return -1;
    } while (!bufsize && !frame && x264_encoder_delayed_frames(x4->enc));

    /* FIXME: libx264 now provides DTS, but AVFrame doesn't have a field for it. */
    x4->out_pic.pts = pic_out.i_pts;

    switch (pic_out.i_type) {
    case X264_TYPE_IDR:
    case X264_TYPE_I:
        x4->out_pic.pict_type = AV_PICTURE_TYPE_I;
        break;
    case X264_TYPE_P:
        x4->out_pic.pict_type = AV_PICTURE_TYPE_P;
        break;
    case X264_TYPE_B:
    case X264_TYPE_BREF:
        x4->out_pic.pict_type = AV_PICTURE_TYPE_B;
        break;
    }

    x4->out_pic.key_frame = pic_out.b_keyframe;
    if (bufsize)
        x4->out_pic.quality = (pic_out.i_qpplus1 - 1) * FF_QP2LAMBDA;

    return bufsize;
}

static av_cold int X264_close(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;

    av_freep(&avctx->extradata);
    av_free(x4->sei);

    if (x4->enc)
        x264_encoder_close(x4->enc);

    return 0;
}

/**
 * Detect default settings and use default profile to avoid libx264 failure.
 */
static void check_default_settings(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;

    int score = 0;
    score += x4->params.analyse.i_me_range == 0;
    score += x4->params.rc.i_qp_step == 3;
    score += x4->params.i_keyint_max == 12;
    score += x4->params.rc.i_qp_min == 2;
    score += x4->params.rc.i_qp_max == 31;
    score += x4->params.rc.f_qcompress == 0.5;
    score += fabs(x4->params.rc.f_ip_factor - 1.25) < 0.01;
    score += fabs(x4->params.rc.f_pb_factor - 1.25) < 0.01;
    score += x4->params.analyse.inter == 0 && x4->params.analyse.i_subpel_refine == 8;
    if (score >= 5) {
        av_log(avctx, AV_LOG_ERROR, "Default settings detected, using medium profile\n");
        x4->preset = av_strdup("medium");
        if (avctx->bit_rate == 200*1000)
            avctx->crf = 23;
    }
}

#define OPT_STR(opt, param)                                             \
    do {                                                                \
        if (param && x264_param_parse(&x4->params, opt, param) < 0) {   \
            av_log(avctx, AV_LOG_ERROR,                                 \
                   "bad value for '%s': '%s'\n", opt, param);           \
            return -1;                                                  \
        }                                                               \
    } while (0);                                                        \

static av_cold int X264_init(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;

    x4->sei_size = 0;
    x264_param_default(&x4->params);

    x4->params.i_keyint_max         = avctx->gop_size;

    x4->params.i_bframe          = avctx->max_b_frames;
    x4->params.b_cabac           = avctx->coder_type == FF_CODER_TYPE_AC;
    x4->params.i_bframe_adaptive = avctx->b_frame_strategy;
    x4->params.i_bframe_bias     = avctx->bframebias;
    x4->params.i_bframe_pyramid  = avctx->flags2 & CODEC_FLAG2_BPYRAMID ? X264_B_PYRAMID_NORMAL : X264_B_PYRAMID_NONE;

    x4->params.i_keyint_min = avctx->keyint_min;
    if (x4->params.i_keyint_min > x4->params.i_keyint_max)
        x4->params.i_keyint_min = x4->params.i_keyint_max;

    x4->params.i_scenecut_threshold        = avctx->scenechange_threshold;

    x4->params.b_deblocking_filter         = avctx->flags & CODEC_FLAG_LOOP_FILTER;
    x4->params.i_deblocking_filter_alphac0 = avctx->deblockalpha;
    x4->params.i_deblocking_filter_beta    = avctx->deblockbeta;

    x4->params.rc.i_qp_min                 = avctx->qmin;
    x4->params.rc.i_qp_max                 = avctx->qmax;
    x4->params.rc.i_qp_step                = avctx->max_qdiff;

    x4->params.rc.f_qcompress       = avctx->qcompress; /* 0.0 => cbr, 1.0 => constant qp */
    x4->params.rc.f_qblur           = avctx->qblur;     /* temporally blur quants */
    x4->params.rc.f_complexity_blur = avctx->complexityblur;

    x4->params.i_frame_reference    = avctx->refs;

    x4->params.analyse.inter    = 0;
    if (avctx->partitions) {
        if (avctx->partitions & X264_PART_I4X4)
            x4->params.analyse.inter |= X264_ANALYSE_I4x4;
        if (avctx->partitions & X264_PART_I8X8)
            x4->params.analyse.inter |= X264_ANALYSE_I8x8;
        if (avctx->partitions & X264_PART_P8X8)
            x4->params.analyse.inter |= X264_ANALYSE_PSUB16x16;
        if (avctx->partitions & X264_PART_P4X4)
            x4->params.analyse.inter |= X264_ANALYSE_PSUB8x8;
        if (avctx->partitions & X264_PART_B8X8)
            x4->params.analyse.inter |= X264_ANALYSE_BSUB16x16;
    }

    x4->params.analyse.i_direct_mv_pred  = avctx->directpred;

    x4->params.analyse.b_weighted_bipred = avctx->flags2 & CODEC_FLAG2_WPRED;

    if (avctx->me_method == ME_EPZS)
        x4->params.analyse.i_me_method = X264_ME_DIA;
    else if (avctx->me_method == ME_HEX)
        x4->params.analyse.i_me_method = X264_ME_HEX;
    else if (avctx->me_method == ME_UMH)
        x4->params.analyse.i_me_method = X264_ME_UMH;
    else if (avctx->me_method == ME_FULL)
        x4->params.analyse.i_me_method = X264_ME_ESA;
    else if (avctx->me_method == ME_TESA)
        x4->params.analyse.i_me_method = X264_ME_TESA;
    else x4->params.analyse.i_me_method = X264_ME_HEX;

    x4->params.rc.i_aq_mode               = avctx->aq_mode;
    x4->params.rc.f_aq_strength           = avctx->aq_strength;
    x4->params.rc.i_lookahead             = avctx->rc_lookahead;

    x4->params.analyse.b_psy              = avctx->flags2 & CODEC_FLAG2_PSY;
    x4->params.analyse.f_psy_rd           = avctx->psy_rd;
    x4->params.analyse.f_psy_trellis      = avctx->psy_trellis;

    x4->params.analyse.i_me_range         = avctx->me_range;
    x4->params.analyse.i_subpel_refine    = avctx->me_subpel_quality;

    x4->params.analyse.b_mixed_references = avctx->flags2 & CODEC_FLAG2_MIXED_REFS;
    x4->params.analyse.b_chroma_me        = avctx->me_cmp & FF_CMP_CHROMA;
    x4->params.analyse.b_transform_8x8    = avctx->flags2 & CODEC_FLAG2_8X8DCT;
    x4->params.analyse.b_fast_pskip       = avctx->flags2 & CODEC_FLAG2_FASTPSKIP;

    x4->params.analyse.i_trellis          = avctx->trellis;
    x4->params.analyse.i_noise_reduction  = avctx->noise_reduction;

    x4->params.rc.b_mb_tree               = !!(avctx->flags2 & CODEC_FLAG2_MBTREE);
    x4->params.rc.f_ip_factor             = 1 / fabs(avctx->i_quant_factor);
    x4->params.rc.f_pb_factor             = avctx->b_quant_factor;
    x4->params.analyse.i_chroma_qp_offset = avctx->chromaoffset;

    if (!x4->preset)
        check_default_settings(avctx);

    if (x4->preset || x4->tune) {
        if (x264_param_default_preset(&x4->params, x4->preset, x4->tune) < 0)
            return -1;
    }

    x4->params.pf_log               = X264_log;
    x4->params.p_log_private        = avctx;
    x4->params.i_log_level          = X264_LOG_DEBUG;

    OPT_STR("weightp", x4->weightp);

    x4->params.b_intra_refresh      = avctx->flags2 & CODEC_FLAG2_INTRA_REFRESH;
    x4->params.rc.i_bitrate         = avctx->bit_rate       / 1000;
    x4->params.rc.i_vbv_buffer_size = avctx->rc_buffer_size / 1000;
    x4->params.rc.i_vbv_max_bitrate = avctx->rc_max_rate    / 1000;
    x4->params.rc.b_stat_write      = avctx->flags & CODEC_FLAG_PASS1;
    if (avctx->flags & CODEC_FLAG_PASS2) {
        x4->params.rc.b_stat_read = 1;
    } else {
        if (avctx->crf) {
            x4->params.rc.i_rc_method   = X264_RC_CRF;
            x4->params.rc.f_rf_constant = avctx->crf;
            x4->params.rc.f_rf_constant_max = avctx->crf_max;
        } else if (avctx->cqp > -1) {
            x4->params.rc.i_rc_method   = X264_RC_CQP;
            x4->params.rc.i_qp_constant = avctx->cqp;
        }
    }

    OPT_STR("stats", x4->stats);

    // if neither crf nor cqp modes are selected we have to enable the RC
    // we do it this way because we cannot check if the bitrate has been set
    if (!(avctx->crf || (avctx->cqp > -1)))
        x4->params.rc.i_rc_method = X264_RC_ABR;

    if (avctx->rc_buffer_size && avctx->rc_initial_buffer_occupancy &&
        (avctx->rc_initial_buffer_occupancy <= avctx->rc_buffer_size)) {
        x4->params.rc.f_vbv_buffer_init =
            (float)avctx->rc_initial_buffer_occupancy / avctx->rc_buffer_size;
    }

    OPT_STR("level", x4->level);

    if(x4->x264opts){
        const char *p= x4->x264opts;
        while(p){
            char param[256]={0}, val[256]={0};
            sscanf(p, "%255[^:=]=%255[^:]", param, val);
            OPT_STR(param, val);
            p= strchr(p, ':');
            p+=!!p;
        }
    }

    if (x4->fastfirstpass)
        x264_param_apply_fastfirstpass(&x4->params);

    if (x4->profile)
        if (x264_param_apply_profile(&x4->params, x4->profile) < 0)
            return -1;

    x4->params.i_width          = avctx->width;
    x4->params.i_height         = avctx->height;
    x4->params.vui.i_sar_width  = avctx->sample_aspect_ratio.num;
    x4->params.vui.i_sar_height = avctx->sample_aspect_ratio.den;
    x4->params.i_fps_num = x4->params.i_timebase_den = avctx->time_base.den;
    x4->params.i_fps_den = x4->params.i_timebase_num = avctx->time_base.num;

    x4->params.analyse.b_psnr = avctx->flags & CODEC_FLAG_PSNR;
    x4->params.analyse.b_ssim = avctx->flags2 & CODEC_FLAG2_SSIM;

    x4->params.b_aud          = avctx->flags2 & CODEC_FLAG2_AUD;

    x4->params.i_threads      = avctx->thread_count;

    x4->params.b_interlaced   = avctx->flags & CODEC_FLAG_INTERLACED_DCT;

//    x4->params.b_open_gop     = !(avctx->flags & CODEC_FLAG_CLOSED_GOP);

    x4->params.i_slice_count  = avctx->slices;

    x4->params.vui.b_fullrange = avctx->pix_fmt == PIX_FMT_YUVJ420P;

    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER)
        x4->params.b_repeat_headers = 0;

    // update AVCodecContext with x264 parameters
    avctx->has_b_frames = x4->params.i_bframe ?
        x4->params.i_bframe_pyramid ? 2 : 1 : 0;
    avctx->bit_rate = x4->params.rc.i_bitrate*1000;
    avctx->crf = x4->params.rc.f_rf_constant;

    x4->enc = x264_encoder_open(&x4->params);
    if (!x4->enc)
        return -1;

    avctx->coded_frame = &x4->out_pic;

    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER) {
        x264_nal_t *nal;
        int nnal, s, i;

        s = x264_encoder_headers(x4->enc, &nal, &nnal);

        for (i = 0; i < nnal; i++)
            if (nal[i].i_type == NAL_SEI)
                av_log(avctx, AV_LOG_INFO, "%s\n", nal[i].p_payload+25);

        avctx->extradata      = av_malloc(s);
        avctx->extradata_size = encode_nals(avctx, avctx->extradata, s, nal, nnal, 1);
    }

    return 0;
}

#define OFFSET(x) offsetof(X264Context,x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    {"preset", "Set the encoding preset", OFFSET(preset), FF_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    {"tune", "Tune the encoding params", OFFSET(tune), FF_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    {"fastfirstpass", "Use fast settings when encoding first pass", OFFSET(fastfirstpass), FF_OPT_TYPE_INT, {.dbl=1}, 0, 1, VE},
    {"profile", "Set profile restrictions", OFFSET(profile), FF_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    {"level", "Specify level (as defined by Annex A)", OFFSET(level), FF_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    {"passlogfile", "Filename for 2 pass stats", OFFSET(stats), FF_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    {"wpredp", "Weighted prediction for P-frames", OFFSET(weightp), FF_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    {"x264opts", "x264 options", OFFSET(x264opts), FF_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    { NULL },
};

static const AVClass class = { "libx264", av_default_item_name, options, LIBAVUTIL_VERSION_INT };

AVCodec ff_libx264_encoder = {
    .name           = "libx264",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_H264,
    .priv_data_size = sizeof(X264Context),
    .init           = X264_init,
    .encode         = X264_frame,
    .close          = X264_close,
    .capabilities   = CODEC_CAP_DELAY,
    .pix_fmts       = (const enum PixelFormat[]) { PIX_FMT_YUV420P, PIX_FMT_YUVJ420P, PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),
    .priv_class     = &class,
};
