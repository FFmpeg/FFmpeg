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
#include <float.h>
#include <xavs.h>
#include "avcodec.h"
#include "internal.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#define END_OF_STREAM 0x001

#define XAVS_PART_I8X8 0x002 /* Analyze i8x8 (requires 8x8 transform) */
#define XAVS_PART_P8X8 0x010 /* Analyze p16x8, p8x16 and p8x8 */
#define XAVS_PART_B8X8 0x100 /* Analyze b16x8, b*/

typedef struct XavsContext {
    AVClass        *class;
    xavs_param_t    params;
    xavs_t         *enc;
    xavs_picture_t  pic;
    uint8_t        *sei;
    int             sei_size;
    int             end_of_stream;
    float crf;
    int cqp;
    int b_bias;
    float cplxblur;
    int direct_pred;
    int aud;
    int fast_pskip;
    int mbtree;
    int mixed_refs;

    int64_t *pts_buffer;
    int out_frame_count;
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

static int encode_nals(AVCodecContext *ctx, AVPacket *pkt,
                       xavs_nal_t *nals, int nnal)
{
    XavsContext *x4 = ctx->priv_data;
    uint8_t *p;
    int i, s, ret, size = x4->sei_size + FF_MIN_BUFFER_SIZE;

    if (!nnal)
        return 0;

    for (i = 0; i < nnal; i++)
        size += nals[i].i_payload;

    if ((ret = ff_alloc_packet2(ctx, pkt, size)) < 0)
        return ret;
    p = pkt->data;

    /* Write the SEI as part of the first frame. */
    if (x4->sei_size > 0 && nnal > 0) {
        memcpy(p, x4->sei, x4->sei_size);
        p += x4->sei_size;
        x4->sei_size = 0;
    }

    for (i = 0; i < nnal; i++) {
        s = xavs_nal_encode(p, &size, 1, nals + i);
        if (s < 0)
            return -1;
        p += s;
    }
    pkt->size = p - pkt->data;

    return 1;
}

static int XAVS_frame(AVCodecContext *avctx, AVPacket *pkt,
                      const AVFrame *frame, int *got_packet)
{
    XavsContext *x4 = avctx->priv_data;
    xavs_nal_t *nal;
    int nnal, i, ret;
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
        x4->pts_buffer[avctx->frame_number % (avctx->max_b_frames+1)] = frame->pts;
    }

    if (xavs_encoder_encode(x4->enc, &nal, &nnal,
                            frame? &x4->pic: NULL, &pic_out) < 0)
    return -1;

    ret = encode_nals(avctx, pkt, nal, nnal);

    if (ret < 0)
        return -1;

    if (!ret) {
        if (!frame && !(x4->end_of_stream)) {
            if ((ret = ff_alloc_packet2(avctx, pkt, 4)) < 0)
                return ret;

            pkt->data[0] = 0x0;
            pkt->data[1] = 0x0;
            pkt->data[2] = 0x01;
            pkt->data[3] = 0xb1;
            pkt->dts = 2*x4->pts_buffer[(x4->out_frame_count-1)%(avctx->max_b_frames+1)] -
                         x4->pts_buffer[(x4->out_frame_count-2)%(avctx->max_b_frames+1)];
            x4->end_of_stream = END_OF_STREAM;
            *got_packet = 1;
        }
        return 0;
    }

    avctx->coded_frame->pts = pic_out.i_pts;
    pkt->pts = pic_out.i_pts;
    if (avctx->has_b_frames) {
        if (!x4->out_frame_count)
            pkt->dts = pkt->pts - (x4->pts_buffer[1] - x4->pts_buffer[0]);
        else
            pkt->dts = x4->pts_buffer[(x4->out_frame_count-1)%(avctx->max_b_frames+1)];
    } else
        pkt->dts = pkt->pts;

    switch (pic_out.i_type) {
    case XAVS_TYPE_IDR:
    case XAVS_TYPE_I:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
        break;
    case XAVS_TYPE_P:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_P;
        break;
    case XAVS_TYPE_B:
    case XAVS_TYPE_BREF:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_B;
        break;
    }

    /* There is no IDR frame in AVS JiZhun */
    /* Sequence header is used as a flag */
    if (pic_out.i_type == XAVS_TYPE_I) {
        avctx->coded_frame->key_frame = 1;
        pkt->flags |= AV_PKT_FLAG_KEY;
    }

    avctx->coded_frame->quality = (pic_out.i_qpplus1 - 1) * FF_QP2LAMBDA;

    x4->out_frame_count++;
    *got_packet = ret;
    return 0;
}

static av_cold int XAVS_close(AVCodecContext *avctx)
{
    XavsContext *x4 = avctx->priv_data;

    av_freep(&avctx->extradata);
    av_freep(&x4->sei);
    av_freep(&x4->pts_buffer);

    if (x4->enc)
        xavs_encoder_close(x4->enc);

    av_frame_free(&avctx->coded_frame);

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
    if (avctx->bit_rate) {
        x4->params.rc.i_bitrate   = avctx->bit_rate / 1000;
        x4->params.rc.i_rc_method = XAVS_RC_ABR;
    }
    x4->params.rc.i_vbv_buffer_size = avctx->rc_buffer_size / 1000;
    x4->params.rc.i_vbv_max_bitrate = avctx->rc_max_rate    / 1000;
    x4->params.rc.b_stat_write      = avctx->flags & CODEC_FLAG_PASS1;
    if (avctx->flags & CODEC_FLAG_PASS2) {
        x4->params.rc.b_stat_read = 1;
    } else {
        if (x4->crf >= 0) {
            x4->params.rc.i_rc_method   = XAVS_RC_CRF;
            x4->params.rc.f_rf_constant = x4->crf;
        } else if (x4->cqp >= 0) {
            x4->params.rc.i_rc_method   = XAVS_RC_CQP;
            x4->params.rc.i_qp_constant = x4->cqp;
        }
    }

    if (x4->aud >= 0)
        x4->params.b_aud                      = x4->aud;
    if (x4->mbtree >= 0)
        x4->params.rc.b_mb_tree               = x4->mbtree;
    if (x4->direct_pred >= 0)
        x4->params.analyse.i_direct_mv_pred   = x4->direct_pred;
    if (x4->fast_pskip >= 0)
        x4->params.analyse.b_fast_pskip       = x4->fast_pskip;
    if (x4->mixed_refs >= 0)
        x4->params.analyse.b_mixed_references = x4->mixed_refs;
    if (x4->b_bias != INT_MIN)
        x4->params.i_bframe_bias              = x4->b_bias;
    if (x4->cplxblur >= 0)
        x4->params.rc.f_complexity_blur = x4->cplxblur;

    x4->params.i_bframe          = avctx->max_b_frames;
    /* cabac is not included in AVS JiZhun Profile */
    x4->params.b_cabac           = 0;

    x4->params.i_bframe_adaptive = avctx->b_frame_strategy;

    avctx->has_b_frames          = !!avctx->max_b_frames;

    /* AVS doesn't allow B picture as reference */
    /* The max allowed reference frame number of B is 2 */
    x4->params.i_keyint_min      = avctx->keyint_min;
    if (x4->params.i_keyint_min > x4->params.i_keyint_max)
        x4->params.i_keyint_min = x4->params.i_keyint_max;

    x4->params.i_scenecut_threshold        = avctx->scenechange_threshold;

   // x4->params.b_deblocking_filter       = avctx->flags & CODEC_FLAG_LOOP_FILTER;

    x4->params.rc.i_qp_min                 = avctx->qmin;
    x4->params.rc.i_qp_max                 = avctx->qmax;
    x4->params.rc.i_qp_step                = avctx->max_qdiff;

    x4->params.rc.f_qcompress       = avctx->qcompress; /* 0.0 => cbr, 1.0 => constant qp */
    x4->params.rc.f_qblur           = avctx->qblur;     /* temporally blur quants */

    x4->params.i_frame_reference    = avctx->refs;

    x4->params.i_width              = avctx->width;
    x4->params.i_height             = avctx->height;
    x4->params.vui.i_sar_width      = avctx->sample_aspect_ratio.num;
    x4->params.vui.i_sar_height     = avctx->sample_aspect_ratio.den;
    /* This is only used for counting the fps */
    x4->params.i_fps_num            = avctx->time_base.den;
    x4->params.i_fps_den            = avctx->time_base.num;
    x4->params.analyse.inter        = XAVS_ANALYSE_I8x8 |XAVS_ANALYSE_PSUB16x16| XAVS_ANALYSE_BSUB16x16;

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

    x4->params.analyse.b_chroma_me        = avctx->me_cmp & FF_CMP_CHROMA;
    /* AVS P2 only enables 8x8 transform */
    x4->params.analyse.b_transform_8x8    = 1; //avctx->flags2 & CODEC_FLAG2_8X8DCT;

    x4->params.analyse.i_trellis          = avctx->trellis;
    x4->params.analyse.i_noise_reduction  = avctx->noise_reduction;

    if (avctx->level > 0)
        x4->params.i_level_idc = avctx->level;

    if (avctx->bit_rate > 0)
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
    x4->params.rc.f_ip_factor             = 1 / fabs(avctx->i_quant_factor);
    x4->params.rc.f_pb_factor             = avctx->b_quant_factor;
    x4->params.analyse.i_chroma_qp_offset = avctx->chromaoffset;

    x4->params.analyse.b_psnr = avctx->flags & CODEC_FLAG_PSNR;
    x4->params.i_log_level    = XAVS_LOG_DEBUG;
    x4->params.i_threads      = avctx->thread_count;
    x4->params.b_interlaced   = avctx->flags & CODEC_FLAG_INTERLACED_DCT;

    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER)
        x4->params.b_repeat_headers = 0;

    x4->enc = xavs_encoder_open(&x4->params);
    if (!x4->enc)
        return -1;

    if (!(x4->pts_buffer = av_mallocz_array((avctx->max_b_frames+1), sizeof(*x4->pts_buffer))))
        return AVERROR(ENOMEM);

    avctx->coded_frame = av_frame_alloc();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    /* TAG: Do we have GLOBAL HEADER in AVS */
    /* We Have PPS and SPS in AVS */
    if (avctx->flags & CODEC_FLAG_GLOBAL_HEADER && 0) {
        xavs_nal_t *nal;
        int nnal, s, i, size;
        uint8_t *p;

        s = xavs_encoder_headers(x4->enc, &nal, &nnal);

        avctx->extradata = p = av_malloc(s);
        for (i = 0; i < nnal; i++) {
            /* Don't put the SEI in extradata. */
            if (nal[i].i_type == NAL_SEI) {
                x4->sei = av_malloc( 5 + nal[i].i_payload * 4 / 3 );
                if (xavs_nal_encode(x4->sei, &x4->sei_size, 1, nal + i) < 0)
                    return -1;

                continue;
            }
            size = xavs_nal_encode(p, &s, 1, nal + i);
            if (size < 0)
                return -1;
            p += size;
        }
        avctx->extradata_size = p - avctx->extradata;
    }
    return 0;
}

#define OFFSET(x) offsetof(XavsContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "crf",           "Select the quality for constant quality mode",    OFFSET(crf),           AV_OPT_TYPE_FLOAT,  {.dbl = -1 }, -1, FLT_MAX, VE },
    { "qp",            "Constant quantization parameter rate control method",OFFSET(cqp),        AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, INT_MAX, VE },
    { "b-bias",        "Influences how often B-frames are used",          OFFSET(b_bias),        AV_OPT_TYPE_INT,    {.i64 = INT_MIN}, INT_MIN, INT_MAX, VE },
    { "cplxblur",      "Reduce fluctuations in QP (before curve compression)", OFFSET(cplxblur), AV_OPT_TYPE_FLOAT,  {.dbl = -1 }, -1, FLT_MAX, VE},
    { "direct-pred",   "Direct MV prediction mode",                       OFFSET(direct_pred),   AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, INT_MAX, VE, "direct-pred" },
    { "none",          NULL,      0,    AV_OPT_TYPE_CONST, { .i64 = XAVS_DIRECT_PRED_NONE },     0, 0, VE, "direct-pred" },
    { "spatial",       NULL,      0,    AV_OPT_TYPE_CONST, { .i64 = XAVS_DIRECT_PRED_SPATIAL },  0, 0, VE, "direct-pred" },
    { "temporal",      NULL,      0,    AV_OPT_TYPE_CONST, { .i64 = XAVS_DIRECT_PRED_TEMPORAL }, 0, 0, VE, "direct-pred" },
    { "auto",          NULL,      0,    AV_OPT_TYPE_CONST, { .i64 = XAVS_DIRECT_PRED_AUTO },     0, 0, VE, "direct-pred" },
    { "aud",           "Use access unit delimiters.",                     OFFSET(aud),           AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, 1, VE},
    { "mbtree",        "Use macroblock tree ratecontrol.",                OFFSET(mbtree),        AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, 1, VE},
    { "mixed-refs",    "One reference per partition, as opposed to one reference per macroblock", OFFSET(mixed_refs), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 1, VE },
    { "fast-pskip",    NULL,                                              OFFSET(fast_pskip),    AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, 1, VE},
    { NULL },
};

static const AVClass xavs_class = {
    .class_name = "libxavs",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault xavs_defaults[] = {
    { "b",                "0" },
    { NULL },
};

AVCodec ff_libxavs_encoder = {
    .name           = "libxavs",
    .long_name      = NULL_IF_CONFIG_SMALL("libxavs Chinese AVS (Audio Video Standard)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CAVS,
    .priv_data_size = sizeof(XavsContext),
    .init           = XAVS_init,
    .encode2        = XAVS_frame,
    .close          = XAVS_close,
    .capabilities   = CODEC_CAP_DELAY | CODEC_CAP_AUTO_THREADS,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .priv_class     = &xavs_class,
    .defaults       = xavs_defaults,
};
