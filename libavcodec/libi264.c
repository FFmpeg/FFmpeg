/*
 * H.264 encoding using the i264 library
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

#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/stereo3d.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"

#include <i264.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_LIBI264_ENCODER

typedef struct I264Context {
    AVClass        *class;
    x264_config_t   configs;
    x264_t         *enc;
    x264_picture_t  pic;

    uint8_t        *sei;
    int             sei_size;
} X264Context;

static int encode_nals(AVCodecContext *ctx, AVPacket *pkt,
                       const x264_nal_t *nals, int nnal)
{
    I264Context *i4 = ctx->priv_data;
    uint8_t *p;
    int i, size = i4->sei_size, ret;

    if (!nnal)
        return 0;

    for (i = 0; i < nnal; i++)
        size += nals[i].i_payload;

    if ((ret = ff_alloc_packet2(ctx, pkt, size, 0)) < 0)
        return ret;

    p = pkt->data;

    /* Write the SEI as part of the first frame. */
    if (i4->sei_size > 0 && nnal > 0) {
        if (i4->sei_size > size) {
            av_log(ctx, AV_LOG_ERROR, "Error: nal buffer is too small\n");
            return -1;
        }
        memcpy(p, i4->sei, i4->sei_size);
        p += i4->sei_size;
        i4->sei_size = 0;
        av_freep(&i4->sei);
    }

    for (i = 0; i < nnal; i++){
        memcpy(p, nals[i].p_payload, nals[i].i_payload);
        p += nals[i].i_payload;
    }

    return 1;
}

static int I264_frame(AVCodecContext *ctx, AVPacket *pkt, const AVFrame *frame,
                      int *got_packet)
{
    I264Context *i4 = ctx->priv_data;
    x264_nal_t *nal;
    int nnal, i, ret;
    x264_picture_t pic_out = {0};
    int pict_type;

    x264_picture_init( &i4->pic );
    i4->pic.img.i_csp   = X264_CSP_I420;
    i4->pic.img.i_plane = 3;

    if (frame) {
        for (i = 0; i < i4->pic.img.i_plane; i++) {
            i4->pic.img.plane[i]    = frame->data[i];
            i4->pic.img.i_stride[i] = frame->linesize[i];
        }

        i4->pic.i_pts  = frame->pts;

        switch (frame->pict_type) {
        case AV_PICTURE_TYPE_I:
            i4->pic.i_type = X264_TYPE_IDR;
            break;
        case AV_PICTURE_TYPE_P:
            i4->pic.i_type = X264_TYPE_P;
            break;
        case AV_PICTURE_TYPE_B:
            i4->pic.i_type = X264_TYPE_B;
            break;
        default:
            i4->pic.i_type = X264_TYPE_AUTO;
            break;
        }
    }

    do {
        if (x264_encoder_encode(i4->enc, &nal, &nnal, frame? &i4->pic: NULL, &pic_out) < 0)
            return AVERROR_EXTERNAL;

        ret = encode_nals(ctx, pkt, nal, nnal);
        if (ret < 0)
            return ret;
    } while (!ret && !frame && x264_encoder_delayed_frames(i4->enc));

    pkt->pts = pic_out.i_pts;
    pkt->dts = pic_out.i_dts;


    switch (pic_out.i_type) {
    case X264_TYPE_IDR:
    case X264_TYPE_I:
        pict_type = AV_PICTURE_TYPE_I;
        break;
    case X264_TYPE_P:
        pict_type = AV_PICTURE_TYPE_P;
        break;
    case X264_TYPE_B:
    case X264_TYPE_BREF:
        pict_type = AV_PICTURE_TYPE_B;
        break;
    default:
        pict_type = AV_PICTURE_TYPE_NONE;
    }
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    ctx->coded_frame->pict_type = pict_type;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    pkt->flags |= AV_PKT_FLAG_KEY * pic_out.b_keyframe;
    if (ret) {
        ff_side_data_set_encoder_stats(pkt, (pic_out.i_qpplus1 - 1) * FF_QP2LAMBDA, NULL, 0, pict_type);

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        ctx->coded_frame->quality = (pic_out.i_qpplus1 - 1) * FF_QP2LAMBDA;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }

    *got_packet = ret;
    return 0;
}

static av_cold int I264_close(AVCodecContext *avctx)
{
    I264Context *i4 = avctx->priv_data;

    av_freep(&avctx->extradata);
    av_freep(&i4->sei);

    if (i4->enc) {
        x264_encoder_close(i4->enc);
        i4->enc = NULL;
    }

    return 0;
}

static av_cold int I264_init(AVCodecContext *avctx)
{
    I264Context *i4 = avctx->priv_data;

    i4.configs.width  = avctx->width;
    i4.configs.height = avctx->height;
    if (avctx->height <= 180) {
        i4.configs.profile = PROFILE_ZHIBO_320x180;
    } else if(avctx->height <= 240){
        i4.configs.profile = PROFILE_ZHIBO_320x240;
    } else if(avctx->height <= 480){
        i4.configs.profile = PROFILE_ZHIBO_640x480;
    } else if(avctx->height <= 540 && width < 960){
        i4.configs.profile = PROFILE_ZHIBO_720x540;
    } else if(avctx->height <= 540 && width < 1280){
        i4.configs.profile = PROFILE_ZHIBO_960x540;
    } else if(avctx->height <= 720){
        i4.configs.profile = PROFILE_ZHIBO_1280x720;
    } else {
        i4.configs.profile = PROFILE_ZHIBO_1920x1080;
    }

    i4.configs.bitrate = avctx->bit_rate / 1000;
    if (avctx->time_base.den > 0 ) {
        i4.configs.frame_rate = avctx->time_base.num * avctx->ticks_per_frame / avctx->time_base.den;
    } else {
        i4.configs.frame_rate = 25;
        av_log(avctx, AV_LOG_INFO, "AVCodecContext.time_base.den is not greater than 0, frame_rate refine to 25\n");
    }
    i4.configs.keyint_max = avctx->gop_size;

    i4.configs.repeat_header = 1;
    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        i4.configs.repeat_header = 0;
    }
    i4.configs.num_thread = 0;

    av_log(avctx, AV_LOG_INFO, "Dump libi264 config values: \n");
    av_log(avctx, AV_LOG_INFO, "width: %d, height: %d, profile: %d, bitrate: %d(kbps), frame_rate: %d, keyint_max: %d, repeat_header: %d, num_thread: %d\n", 
    i4.configs.width, i4.configs.height, i4.configs.profile, i4.configs.bitrate, i4.configs.frame_rate, i4.configs.keyint_max, i4.configs.repeat_header, i4.configs.num_thread);

    i4.enc = x264_init(&i4.configs);
    if (!i4->enc)
        return AVERROR_EXTERNAL;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        x264_nal_t *nal;
        uint8_t *p;
        int nnal, s, i;

        s = x264_encoder_headers(i4->enc, &nal, &nnal);
        avctx->extradata = p = av_mallocz(s + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!p)
            return AVERROR(ENOMEM);

        for (i = 0; i < nnal; i++) {
            /* Don't put the SEI in extradata. */
            if (nal[i].i_type == NAL_SEI) {
                av_log(avctx, AV_LOG_INFO, "%s\n", nal[i].p_payload + 25);
                i4->sei_size = nal[i].i_payload;
                i4->sei      = av_malloc(i4->sei_size);
                if (!i4->sei)
                    return AVERROR(ENOMEM);
                memcpy(i4->sei, nal[i].p_payload, nal[i].i_payload);
                continue;
            }
            memcpy(p, nal[i].p_payload, nal[i].i_payload);
            p += nal[i].i_payload;
        }
        avctx->extradata_size = p - avctx->extradata;
    }

    return 0;
}

static const enum AVPixelFormat pix_fmts_8bit[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV16,
#ifdef X264_CSP_NV21
    AV_PIX_FMT_NV21,
#endif
    AV_PIX_FMT_NONE
};
static const enum AVPixelFormat pix_fmts_9bit[] = {
    AV_PIX_FMT_YUV420P9,
    AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_NONE
};
static const enum AVPixelFormat pix_fmts_10bit[] = {
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_NV20,
    AV_PIX_FMT_NONE
};
static const enum AVPixelFormat pix_fmts_all[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV16,
#ifdef X264_CSP_NV21
    AV_PIX_FMT_NV21,
#endif
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_NV20,
    AV_PIX_FMT_NONE
};
#if CONFIG_LIBI264RGB_ENCODER
static const enum AVPixelFormat pix_fmts_8bit_rgb[] = {
    AV_PIX_FMT_BGR0,
    AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_NONE
};
#endif

static av_cold void X264_init_static(AVCodec *codec)
{
#if X264_BUILD < 153
    if (x264_bit_depth == 8)
        codec->pix_fmts = pix_fmts_8bit;
    else if (x264_bit_depth == 9)
        codec->pix_fmts = pix_fmts_9bit;
    else if (x264_bit_depth == 10)
        codec->pix_fmts = pix_fmts_10bit;
#else
    codec->pix_fmts = pix_fmts_all;
#endif
}

#define OFFSET(x) offsetof(I264Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { NULL },
};

static const AVCodecDefault i264_defaults[] = {
    { NULL },
};

static const AVClass i264_class = {
    .class_name = "libi264",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libi264_encoder = {
    .name             = "libi264",
    .long_name        = NULL_IF_CONFIG_SMALL("libi264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_H264,
    .priv_data_size   = sizeof(I264Context),
    .init             = I264_init,
    .encode2          = I264_frame,
    .close            = I264_close,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS,
    .priv_class       = &i264_class,
    .defaults         = i264_defaults,
    .init_static_data = I264_init_static,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP,
    .wrapper_name     = "libi264",
};
#endif
