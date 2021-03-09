/*
 * AVS2 encoding using the xavs2 library
 *
 * Copyright (C) 2018 Yiqun Xu,   <yiqun.xu@vipl.ict.ac.cn>
 *                    Falei Luo,  <falei.luo@gmail.com>
 *                    Huiwen Ren, <hwrenx@gmail.com>
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

#include "xavs2.h"
#include "mpeg12.h"
#include "libavutil/avstring.h"

#define xavs2_opt_set2(name, format, ...) do{ \
    char opt_str[16] = {0}; \
    int err; \
    av_strlcatf(opt_str, sizeof(opt_str), format, __VA_ARGS__); \
    err = cae->api->opt_set2(cae->param, name, opt_str); \
    if (err < 0) {\
        av_log(avctx, AV_LOG_WARNING, "Invalid value for %s: %s\n", name, opt_str);\
    }\
} while(0);

typedef struct XAVS2EContext {
    AVClass *class;

    int lcu_row_threads;
    int initial_qp;
    int qp;
    int max_qp;
    int min_qp;
    int preset_level;
    int log_level;

    void *encoder;
    AVDictionary *xavs2_opts;

    xavs2_outpacket_t packet;
    xavs2_param_t *param;

    const xavs2_api_t *api;

} XAVS2EContext;

static av_cold int xavs2_init(AVCodecContext *avctx)
{
    XAVS2EContext *cae = avctx->priv_data;
    int bit_depth, code;

    bit_depth = avctx->pix_fmt == AV_PIX_FMT_YUV420P ? 8 : 10;

    /* get API handler */
    cae->api = xavs2_api_get(bit_depth);
    if (!cae->api) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get xavs2 api context\n");
        return AVERROR_EXTERNAL;
    }

    cae->param = cae->api->opt_alloc();
    if (!cae->param) {
        av_log(avctx, AV_LOG_ERROR, "Failed to alloc xavs2 parameters\n");
        return AVERROR(ENOMEM);
    }

    xavs2_opt_set2("Width",     "%d", avctx->width);
    xavs2_opt_set2("Height",    "%d", avctx->height);
    xavs2_opt_set2("BFrames",   "%d", avctx->max_b_frames);
    xavs2_opt_set2("BitDepth",  "%d", bit_depth);
    xavs2_opt_set2("Log",       "%d", cae->log_level);
    xavs2_opt_set2("Preset",    "%d", cae->preset_level);

    xavs2_opt_set2("IntraPeriodMax",    "%d", avctx->gop_size);
    xavs2_opt_set2("IntraPeriodMin",    "%d", avctx->gop_size);

    xavs2_opt_set2("ThreadFrames",      "%d", avctx->thread_count);
    xavs2_opt_set2("ThreadRows",        "%d", cae->lcu_row_threads);

    xavs2_opt_set2("OpenGOP",  "%d", !(avctx->flags & AV_CODEC_FLAG_CLOSED_GOP));

    {
        AVDictionaryEntry *en = NULL;
        while ((en = av_dict_get(cae->xavs2_opts, "", en, AV_DICT_IGNORE_SUFFIX)))
            xavs2_opt_set2(en->key, "%s", en->value);
    }

    /* Rate control */
    if (avctx->bit_rate > 0) {
        xavs2_opt_set2("RateControl",   "%d", 1);
        xavs2_opt_set2("TargetBitRate", "%"PRId64"", avctx->bit_rate);
        xavs2_opt_set2("InitialQP",     "%d", cae->initial_qp);
        xavs2_opt_set2("MaxQP",         "%d", avctx->qmax >= 0 ? avctx->qmax : cae->max_qp);
        xavs2_opt_set2("MinQP",         "%d", avctx->qmin >= 0 ? avctx->qmin : cae->min_qp);
    } else {
        xavs2_opt_set2("InitialQP",     "%d", cae->qp);
    }

    ff_mpeg12_find_best_frame_rate(avctx->framerate, &code, NULL, NULL, 0);
    xavs2_opt_set2("FrameRate",   "%d", code);

    cae->encoder = cae->api->encoder_create(cae->param);

    if (!cae->encoder) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create xavs2 encoder instance.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static void xavs2_copy_frame_with_shift(xavs2_picture_t *pic, const AVFrame *frame, const int shift_in)
{
    uint16_t *p_plane;
    uint8_t *p_buffer;
    int plane;
    int hIdx;
    int wIdx;

    for (plane = 0; plane < 3; plane++) {
        p_plane = (uint16_t *)pic->img.img_planes[plane];
        p_buffer = frame->data[plane];
        for (hIdx = 0; hIdx < pic->img.i_lines[plane]; hIdx++) {
            memset(p_plane, 0, pic->img.i_stride[plane]);
            for (wIdx = 0; wIdx < pic->img.i_width[plane]; wIdx++) {
                p_plane[wIdx] = p_buffer[wIdx] << shift_in;
            }
            p_plane += pic->img.i_stride[plane];
            p_buffer += frame->linesize[plane];
        }
    }
}

static void xavs2_copy_frame(xavs2_picture_t *pic, const AVFrame *frame)
{
    uint8_t *p_plane;
    uint8_t *p_buffer;
    int plane;
    int hIdx;
    int stride;

    for (plane = 0; plane < 3; plane++) {
        p_plane = pic->img.img_planes[plane];
        p_buffer = frame->data[plane];
        stride = pic->img.i_width[plane] * pic->img.in_sample_size;
        for (hIdx = 0; hIdx < pic->img.i_lines[plane]; hIdx++) {
            memcpy(p_plane, p_buffer, stride);
            p_plane += pic->img.i_stride[plane];
            p_buffer += frame->linesize[plane];
        }
    }
}

static int xavs2_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                              const AVFrame *frame, int *got_packet)
{
    XAVS2EContext *cae = avctx->priv_data;
    xavs2_picture_t pic;
    int ret;

    /* create the XAVS2 video encoder */
    /* read frame data and send to the XAVS2 video encoder */
    if (cae->api->encoder_get_buffer(cae->encoder, &pic) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get xavs2 frame buffer\n");
        return AVERROR_EXTERNAL;
    }
    if (frame) {
        switch (frame->format) {
        case AV_PIX_FMT_YUV420P:
            if (pic.img.in_sample_size == pic.img.enc_sample_size) {
                xavs2_copy_frame(&pic, frame);
            } else {
                const int shift_in = atoi(cae->api->opt_get(cae->param, "SampleShift"));
                xavs2_copy_frame_with_shift(&pic, frame, shift_in);
            }
            break;
        case AV_PIX_FMT_YUV420P10:
            if (pic.img.in_sample_size == pic.img.enc_sample_size) {
                xavs2_copy_frame(&pic, frame);
                break;
            }
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format\n");
            return AVERROR(EINVAL);
            break;
        }

        pic.i_state = 0;
        pic.i_pts   = frame->pts;
        pic.i_type  = XAVS2_TYPE_AUTO;

        ret = cae->api->encoder_encode(cae->encoder, &pic, &cae->packet);

        if (ret) {
            av_log(avctx, AV_LOG_ERROR, "Encoding error occured.\n");
            return AVERROR_EXTERNAL;
        }

    } else {
        cae->api->encoder_encode(cae->encoder, NULL, &cae->packet);
    }

    if ((cae->packet.len) && (cae->packet.state != XAVS2_STATE_FLUSH_END)) {
        if (av_new_packet(pkt, cae->packet.len) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to alloc xavs2 packet.\n");
            cae->api->encoder_packet_unref(cae->encoder, &cae->packet);
            return AVERROR(ENOMEM);
        }

        pkt->pts = cae->packet.pts;
        pkt->dts = cae->packet.dts;

        if (cae->packet.type == XAVS2_TYPE_IDR ||
            cae->packet.type == XAVS2_TYPE_I ||
            cae->packet.type == XAVS2_TYPE_KEYFRAME) {
            pkt->flags |= AV_PKT_FLAG_KEY;
        }

        memcpy(pkt->data, cae->packet.stream, cae->packet.len);
        pkt->size = cae->packet.len;

        cae->api->encoder_packet_unref(cae->encoder, &cae->packet);

        *got_packet = 1;
    } else {
        *got_packet = 0;
    }

    return 0;
}

static av_cold int xavs2_close(AVCodecContext *avctx)
{
    XAVS2EContext *cae = avctx->priv_data;
    /* destroy the encoder */
    if (cae->api) {
        cae->api->encoder_destroy(cae->encoder);

        if (cae->param) {
            cae->api->opt_destroy(cae->param);
        }
    }
    return 0;
}

#define OFFSET(x) offsetof(XAVS2EContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "lcu_row_threads" ,   "number of parallel threads for rows" ,     OFFSET(lcu_row_threads) , AV_OPT_TYPE_INT, {.i64 =  0 },  0, INT_MAX,  VE },
    { "initial_qp"      ,   "Quantization initial parameter"      ,     OFFSET(initial_qp)      , AV_OPT_TYPE_INT, {.i64 = 34 },  1,      63,  VE },
    { "qp"              ,   "Quantization parameter"  ,                 OFFSET(qp)              , AV_OPT_TYPE_INT, {.i64 = 34 },  1,      63,  VE },
    { "max_qp"          ,   "max qp for rate control" ,                 OFFSET(max_qp)          , AV_OPT_TYPE_INT, {.i64 = 55 },  0,      63,  VE },
    { "min_qp"          ,   "min qp for rate control" ,                 OFFSET(min_qp)          , AV_OPT_TYPE_INT, {.i64 = 20 },  0,      63,  VE },
    { "speed_level"     ,   "Speed level, higher is better but slower", OFFSET(preset_level)    , AV_OPT_TYPE_INT, {.i64 =  0 },  0,       9,  VE },
    { "log_level"       ,   "log level: -1: none, 0: error, 1: warning, 2: info, 3: debug", OFFSET(log_level)    , AV_OPT_TYPE_INT, {.i64 =  0 },  -1,       3,  VE },
    { "xavs2-params"    ,   "set the xavs2 configuration using a :-separated list of key=value parameters", OFFSET(xavs2_opts), AV_OPT_TYPE_DICT, { 0 }, 0, 0, VE },
    { NULL },
};

static const AVClass libxavs2 = {
    .class_name = "XAVS2EContext",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault xavs2_defaults[] = {
    { "b",                "0" },
    { "g",                "48"},
    { "bf",               "7" },
    { NULL },
};

AVCodec ff_libxavs2_encoder = {
    .name           = "libxavs2",
    .long_name      = NULL_IF_CONFIG_SMALL("libxavs2 AVS2-P2/IEEE1857.4"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AVS2,
    .priv_data_size = sizeof(XAVS2EContext),
    .init           = xavs2_init,
    .encode2        = xavs2_encode_frame,
    .close          = xavs2_close,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal  = FF_CODEC_CAP_AUTO_THREADS,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P,
                                                     AV_PIX_FMT_NONE },
    .priv_class     = &libxavs2,
    .defaults       = xavs2_defaults,
    .wrapper_name   = "libxavs2",
} ;
