/*
 * libxeve encoder
 * EVC (MPEG-5 Essential Video Coding) encoding using XEVE MPEG-5 EVC encoder library
 *
 * Copyright (C) 2021 Dawid Kozinski <d.kozinski@samsung.com>
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

#include <float.h>
#include <stdlib.h>

#include <xeve.h>

#include "libavutil/internal.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"
#include "libavutil/cpu.h"
#include "libavutil/avstring.h"

#include "avcodec.h"
#include "internal.h"
#include "packet_internal.h"
#include "codec_internal.h"
#include "profiles.h"
#include "encode.h"

#define MAX_BS_BUF (16*1024*1024)

/**
 * Error codes
 */
#define XEVE_PARAM_BAD_NAME -100
#define XEVE_PARAM_BAD_VALUE -200

/**
 * Encoder states
 *
 * STATE_ENCODING - the encoder receives and processes input frames
 * STATE_BUMPING  - there are no more input frames, however the encoder still processes previously received data
 */
typedef enum State {
    STATE_ENCODING,
    STATE_BUMPING,
} State;

/**
 * The structure stores all the states associated with the instance of Xeve MPEG-5 EVC encoder
 */
typedef struct XeveContext {
    const AVClass *class;

    XEVE id;            // XEVE instance identifier
    XEVE_CDSC cdsc;     // coding parameters i.e profile, width & height of input frame, num of therads, frame rate ...
    XEVE_BITB bitb;     // bitstream buffer (output)
    XEVE_STAT stat;     // encoding status (output)
    XEVE_IMGB imgb;     // image buffer (input)

    State state;        // encoder state (skipping, encoding, bumping)

    int profile_id;     // encoder profile (main, baseline)
    int preset_id;      // preset of xeve ( fast, medium, slow, placebo)
    int tune_id;        // tune of xeve (psnr, zerolatency)

    // variables for rate control modes
    int rc_mode;        // Rate control mode [ 0(CQP) / 1(ABR) / 2(CRF) ]
    int qp;             // quantization parameter (QP) [0,51]
    int crf;            // constant rate factor (CRF) [10,49]

    int hash;           // embed picture signature (HASH) for conformance checking in decoding
    int sei_info;       // embed Supplemental enhancement information while encoding

    int color_format;   // input data color format: currently only XEVE_CF_YCBCR420 is supported

    AVDictionary *xeve_params;
} XeveContext;

/**
 * Convert FFmpeg pixel format (AVPixelFormat) to XEVE pre-defined color format
 *
 * @param[in]  av_pix_fmt pixel format (@see https://ffmpeg.org/doxygen/trunk/pixfmt_8h.html#a9a8e335cf3be472042bc9f0cf80cd4c5)
 * @param[out] xeve_col_fmt XEVE pre-defined color format (@see xeve.h)
 *
 * @return 0 on success, negative value on failure
 */
static int libxeve_color_fmt(enum AVPixelFormat av_pix_fmt, int *xeve_col_fmt)
{
    switch (av_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        *xeve_col_fmt = XEVE_CF_YCBCR420;
        break;
    case AV_PIX_FMT_YUV420P10:
        *xeve_col_fmt = XEVE_CF_YCBCR420;
        break;
    default:
        *xeve_col_fmt = XEVE_CF_UNKNOWN;
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

/**
 * Convert FFmpeg pixel format (AVPixelFormat) into XEVE pre-defined color space
 *
 * @param[in] px_fmt pixel format (@see https://ffmpeg.org/doxygen/trunk/pixfmt_8h.html#a9a8e335cf3be472042bc9f0cf80cd4c5)
 *
 * @return XEVE pre-defined color space (@see xeve.h) on success, XEVE_CF_UNKNOWN on failure
 */
static int libxeve_color_space(enum AVPixelFormat av_pix_fmt)
{
    /* color space of input image */
    int cs = XEVE_CF_UNKNOWN;

    switch (av_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        cs = XEVE_CS_YCBCR420;
        break;
    case AV_PIX_FMT_YUV420P10:
#if AV_HAVE_BIGENDIAN
        cs = XEVE_CS_SET(XEVE_CF_YCBCR420, 10, 1);
#else
        cs = XEVE_CS_YCBCR420_10LE;
#endif

        break;
    default:
        cs = XEVE_CF_UNKNOWN;
        break;
    }

    return cs;
}

/**
 * The function returns a pointer to the object of the XEVE_CDSC type.
 * XEVE_CDSC contains all encoder parameters that should be initialized before the encoder is used.
 *
 * The field values of the XEVE_CDSC structure are populated based on:
 * - the corresponding field values of the AvCodecConetxt structure,
 * - the xeve encoder specific option values,
 *   (the full list of options available for xeve encoder is displayed after executing the command ./ffmpeg --help encoder = libxeve)
 *
 * The order of processing input data and populating the XEVE_CDSC structure
 * 1) first, the fields of the AVCodecContext structure corresponding to the provided input options are processed,
 *    (i.e -pix_fmt yuv420p -s:v 1920x1080 -r 30 -profile:v 0)
 * 2) then xeve-specific options added as AVOption to the xeve AVCodec implementation
 *    (i.e -preset 0)
 *
 * Keep in mind that, there are options that can be set in different ways.
 * In this case, please follow the above-mentioned order of processing.
 * The most recent assignments overwrite the previous values.
 *
 * @param[in] avctx codec context (AVCodecContext)
 * @param[out] cdsc contains all Xeve MPEG-5 EVC encoder encoder parameters that should be initialized before the encoder is use
 *
 * @return 0 on success, negative error code on failure
 */
static int get_conf(AVCodecContext *avctx, XEVE_CDSC *cdsc)
{
    XeveContext *xectx = NULL;
    int ret;

    xectx = avctx->priv_data;

    /* initialize xeve_param struct with default values */
    ret = xeve_param_default(&cdsc->param);
    if (XEVE_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Cannot set_default parameter\n");
        return AVERROR_EXTERNAL;
    }

    /* read options from AVCodecContext */
    if (avctx->width > 0)
        cdsc->param.w = avctx->width;

    if (avctx->height > 0)
        cdsc->param.h = avctx->height;

    if (avctx->framerate.num > 0) {
        // fps can be float number, but xeve API doesn't support it
        cdsc->param.fps.num = avctx->framerate.num;
        cdsc->param.fps.den = avctx->framerate.den;
    }

    // GOP size (key-frame interval, I-picture period)
    cdsc->param.keyint = avctx->gop_size; // 0: only one I-frame at the first time; 1: every frame is coded in I-frame

    if (avctx->max_b_frames == 0 || avctx->max_b_frames == 1 || avctx->max_b_frames == 3 ||
        avctx->max_b_frames == 7 || avctx->max_b_frames == 15)   // number of b-frames
        cdsc->param.bframes = avctx->max_b_frames;
    else {
        av_log(avctx, AV_LOG_ERROR, "Incorrect value for maximum number of B frames: (%d) \n"
               "Acceptable values for bf option (maximum number of B frames) are 0,1,3,7 or 15\n", avctx->max_b_frames);
        return AVERROR_INVALIDDATA;
    }

    cdsc->param.level_idc = avctx->level;

    if (avctx->rc_buffer_size)   // VBV buf size
        cdsc->param.vbv_bufsize = (int)(avctx->rc_buffer_size / 1000);

    cdsc->param.rc_type = xectx->rc_mode;

    if (xectx->rc_mode == XEVE_RC_CQP)
        cdsc->param.qp = xectx->qp;
    else if (xectx->rc_mode == XEVE_RC_ABR) {
        if (avctx->bit_rate / 1000 > INT_MAX || avctx->rc_max_rate / 1000 > INT_MAX) {
            av_log(avctx, AV_LOG_ERROR, "Not supported bitrate bit_rate and rc_max_rate > %d000\n", INT_MAX);
            return AVERROR_INVALIDDATA;
        }
        cdsc->param.bitrate = (int)(avctx->bit_rate / 1000);
    } else if (xectx->rc_mode == XEVE_RC_CRF)
        cdsc->param.crf = xectx->crf;
    else {
        av_log(avctx, AV_LOG_ERROR, "Not supported rate control type: %d\n", xectx->rc_mode);
        return AVERROR_INVALIDDATA;
    }

    if (avctx->thread_count <= 0) {
        int cpu_count = av_cpu_count();
        cdsc->param.threads = (cpu_count < XEVE_MAX_THREADS) ? cpu_count : XEVE_MAX_THREADS;
    } else if (avctx->thread_count > XEVE_MAX_THREADS)
        cdsc->param.threads = XEVE_MAX_THREADS;
    else
        cdsc->param.threads = avctx->thread_count;


    libxeve_color_fmt(avctx->pix_fmt, &xectx->color_format);

    cdsc->param.cs = XEVE_CS_SET(xectx->color_format, cdsc->param.codec_bit_depth, AV_HAVE_BIGENDIAN);

    cdsc->max_bs_buf_size = MAX_BS_BUF;

    ret = xeve_param_ppt(&cdsc->param, xectx->profile_id, xectx->preset_id, xectx->tune_id);
    if (XEVE_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Cannot set profile(%d), preset(%d), tune(%d)\n", xectx->profile_id, xectx->preset_id, xectx->tune_id);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

/**
 * Set XEVE_CFG_SET_USE_PIC_SIGNATURE for encoder
 *
 * @param[in] logger context
 * @param[in] id XEVE encodec instance identifier
 * @param[in] ctx the structure stores all the states associated with the instance of Xeve MPEG-5 EVC encoder
 *
 * @return 0 on success, negative error code on failure
 */
static int set_extra_config(AVCodecContext *avctx, XEVE id, XeveContext *ctx)
{
    int ret, size;
    size = 4;

    // embed SEI messages identifying encoder parameters and command line arguments
    // - 0: off\n"
    // - 1: emit sei info"
    //
    // SEI - Supplemental enhancement information contains information
    // that is not necessary to decode the samples of coded pictures from VCL NAL units.
    // Some SEI message information is required to check bitstream conformance
    // and for output timing decoder conformance.
    // @see ISO_IEC_23094-1_2020 7.4.3.5
    // @see ISO_IEC_23094-1_2020 Annex D
    ret = xeve_config(id, XEVE_CFG_SET_SEI_CMD, &ctx->sei_info, &size); // sei_cmd_info
    if (XEVE_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set config for sei command info messages\n");
        return AVERROR_EXTERNAL;
    }

    ret = xeve_config(id, XEVE_CFG_SET_USE_PIC_SIGNATURE, &ctx->hash, &size);
    if (XEVE_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set config for picture signature\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

/**
 * @brief Switch encoder to bumping mode
 *
 * @param id XEVE encodec instance identifier
 * @return 0 on success, negative error code on failure
 */
static int setup_bumping(XEVE id)
{
    int val = 1;
    int size = sizeof(int);
    if (XEVE_FAILED(xeve_config(id, XEVE_CFG_SET_FORCE_OUT, (void *)(&val), &size)))
        return AVERROR_EXTERNAL;

    return 0;
}

/**
 * @brief Initialize eXtra-fast Essential Video Encoder codec
 * Create an encoder instance and allocate all the needed resources
 *
 * @param avctx codec context
 * @return 0 on success, negative error code on failure
 */
static av_cold int libxeve_init(AVCodecContext *avctx)
{
    XeveContext *xectx = avctx->priv_data;
    unsigned char *bs_buf = NULL;
    int i;
    int shift_h = 0;
    int shift_v = 0;
    int width_chroma = 0;
    int height_chroma = 0;
    XEVE_IMGB *imgb = NULL;
    int ret = 0;

    XEVE_CDSC *cdsc = &(xectx->cdsc);

    /* allocate bitstream buffer */
    bs_buf = av_malloc(MAX_BS_BUF);
    if (bs_buf == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate bitstream buffer\n");
        return AVERROR(ENOMEM);
    }
    xectx->bitb.addr = bs_buf;
    xectx->bitb.bsize = MAX_BS_BUF;

    /* read configurations and set values for created descriptor (XEVE_CDSC) */
    if ((ret = get_conf(avctx, cdsc)) != 0) {
        av_log(avctx, AV_LOG_ERROR, "Cannot get configuration\n");
        return AVERROR(EINVAL);
    }

    if ((ret = xeve_param_check(&cdsc->param)) != 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid configuration\n");
        return AVERROR(EINVAL);
    }

    {
        const AVDictionaryEntry *en = NULL;
        while (en = av_dict_iterate(xectx->xeve_params, en)) {
            if ((ret = xeve_param_parse(&cdsc->param, en->key, en->value)) < 0) {
                av_log(avctx, AV_LOG_WARNING,
                       "Error parsing option '%s = %s'.\n",
                       en->key, en->value);
            }
        }
    }

    /* create encoder */
    xectx->id = xeve_create(cdsc, NULL);
    if (xectx->id == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Cannot create XEVE encoder\n");
        return AVERROR_EXTERNAL;
    }

    if ((ret = set_extra_config(avctx, xectx->id, xectx)) != 0) {
        av_log(avctx, AV_LOG_ERROR, "Cannot set extra configuration\n");
        return AVERROR(EINVAL);
    }

    if ((ret = av_pix_fmt_get_chroma_sub_sample(avctx->pix_fmt, &shift_h, &shift_v)) != 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get  chroma shift\n");
        return AVERROR(EINVAL);
    }

    // Chroma subsampling
    //
    // YUV format explanation
    // shift_h == 1 && shift_v == 1 : YUV420
    // shift_h == 1 && shift_v == 0 : YUV422
    // shift_h == 0 && shift_v == 0 : YUV444
    //
    width_chroma = AV_CEIL_RSHIFT(avctx->width, shift_h);
    height_chroma = AV_CEIL_RSHIFT(avctx->height, shift_v);

    /* set default values for input image buffer */
    imgb = &xectx->imgb;
    imgb->cs = libxeve_color_space(avctx->pix_fmt);
    imgb->np = 3; /* only for yuv420p, yuv420ple */

    for (i = 0; i < imgb->np; i++)
        imgb->x[i] = imgb->y[i] = 0;

    imgb->w[0] = imgb->aw[0] = avctx->width; // width luma
    imgb->w[1] = imgb->w[2] = imgb->aw[1] = imgb->aw[2] = width_chroma;
    imgb->h[0] = imgb->ah[0] = avctx->height; // height luma
    imgb->h[1] = imgb->h[2] = imgb->ah[1] = imgb->ah[2] = height_chroma;

    xectx->state = STATE_ENCODING;

    return 0;
}

/**
  * Encode raw data frame into EVC packet
  *
  * @param[in]  avctx codec context
  * @param[out] avpkt output AVPacket containing encoded data
  * @param[in]  frame AVFrame containing the raw data to be encoded
  * @param[out] got_packet encoder sets to 0 or 1 to indicate that a
  *                         non-empty packet was returned in pkt
  *
  * @return 0 on success, negative error code on failure
  */
static int libxeve_encode(AVCodecContext *avctx, AVPacket *avpkt,
                          const AVFrame *frame, int *got_packet)
{
    XeveContext *xectx =  avctx->priv_data;
    int  ret = -1;

    // No more input frames are available but encoder still can have some data in its internal buffer to process
    // and some frames to dump.
    if (xectx->state == STATE_ENCODING && frame == NULL) {
        if (setup_bumping(xectx->id) == 0)
            xectx->state = STATE_BUMPING;  // Entering bumping process
        else {
            av_log(avctx, AV_LOG_ERROR, "Failed to setup bumping\n");
            return 0;
        }
    }

    if (xectx->state == STATE_ENCODING) {
        int i;
        XEVE_IMGB *imgb = NULL;

        imgb = &xectx->imgb;

        for (i = 0; i < imgb->np; i++) {
            imgb->a[i] = frame->data[i];
            imgb->s[i] = frame->linesize[i];
        }

        imgb->ts[XEVE_TS_PTS] = frame->pts;

        /* push image to encoder */
        ret = xeve_push(xectx->id, imgb);
        if (XEVE_FAILED(ret)) {
            av_log(avctx, AV_LOG_ERROR, "xeve_push() failed\n");
            return AVERROR_EXTERNAL;
        }
    }
    if (xectx->state == STATE_ENCODING || xectx->state == STATE_BUMPING) {
        /* encoding */
        ret = xeve_encode(xectx->id, &(xectx->bitb), &(xectx->stat));
        if (XEVE_FAILED(ret)) {
            av_log(avctx, AV_LOG_ERROR, "xeve_encode() failed\n");
            return AVERROR_EXTERNAL;
        }

        /* store bitstream */
        if (ret == XEVE_OK_OUT_NOT_AVAILABLE) { // Return OK but picture is not available yet
            *got_packet = 0;
            return 0;
        } else if (ret == XEVE_OK) {
            int av_pic_type;

            if (xectx->stat.write > 0) {

                ret = ff_get_encode_buffer(avctx, avpkt, xectx->stat.write, 0);
                if (ret < 0)
                    return ret;

                memcpy(avpkt->data, xectx->bitb.addr, xectx->stat.write);

                avpkt->time_base.num = xectx->cdsc.param.fps.den;
                avpkt->time_base.den = xectx->cdsc.param.fps.num;

                avpkt->pts = xectx->bitb.ts[XEVE_TS_PTS];
                avpkt->dts = xectx->bitb.ts[XEVE_TS_DTS];

                switch(xectx->stat.stype) {
                case XEVE_ST_I:
                    av_pic_type = AV_PICTURE_TYPE_I;
                    avpkt->flags |= AV_PKT_FLAG_KEY;
                    break;
                case XEVE_ST_P:
                    av_pic_type = AV_PICTURE_TYPE_P;
                    break;
                case XEVE_ST_B:
                    av_pic_type = AV_PICTURE_TYPE_B;
                    break;
                case XEVE_ST_UNKNOWN:
                    av_log(avctx, AV_LOG_ERROR, "Unknown slice type\n");
                    return AVERROR_INVALIDDATA;
                }

                ff_side_data_set_encoder_stats(avpkt, xectx->stat.qp * FF_QP2LAMBDA, NULL, 0, av_pic_type);

                *got_packet = 1;
            }
        } else if (ret == XEVE_OK_NO_MORE_FRM) {
            // Return OK but no more frames
            return 0;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Invalid return value: %d\n", ret);
            return AVERROR_EXTERNAL;
        }
    } else {
        av_log(avctx, AV_LOG_ERROR, "Udefined encoder state\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

/**
 * Destroy the encoder and release all the allocated resources
 *
 * @param avctx codec context
 * @return 0 on success, negative error code on failure
 */
static av_cold int libxeve_close(AVCodecContext *avctx)
{
    XeveContext *xectx = avctx->priv_data;

    if (xectx->id) {
        xeve_delete(xectx->id);
        xectx->id = NULL;
    }

    av_free(xectx->bitb.addr); /* release bitstream buffer */

    return 0;
}

#define OFFSET(x) offsetof(XeveContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const enum AVPixelFormat supported_pixel_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_NONE
};

// Consider using following options (./ffmpeg --help encoder=libxeve)
//
static const AVOption libxeve_options[] = {
    { "preset", "Encoding preset for setting encoding speed", OFFSET(preset_id), AV_OPT_TYPE_INT, { .i64 = XEVE_PRESET_MEDIUM }, XEVE_PRESET_DEFAULT,  XEVE_PRESET_PLACEBO, VE, .unit = "preset" },
    { "default", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_PRESET_DEFAULT }, INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "fast",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_PRESET_FAST },    INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "medium",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_PRESET_MEDIUM },  INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "slow",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_PRESET_SLOW },    INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "placebo", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_PRESET_PLACEBO }, INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "tune", "Tuning parameter for special purpose operation", OFFSET(tune_id), AV_OPT_TYPE_INT, { .i64 = XEVE_TUNE_NONE }, XEVE_TUNE_NONE, XEVE_TUNE_PSNR, VE, .unit = "tune"},
    { "none",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_TUNE_NONE },        INT_MIN, INT_MAX, VE, .unit = "tune" },
    { "zerolatency", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_TUNE_ZEROLATENCY }, INT_MIN, INT_MAX, VE, .unit = "tune" },
    { "psnr",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_TUNE_PSNR },        INT_MIN, INT_MAX, VE, .unit = "tune" },
    { "profile", "Encoding profile", OFFSET(profile_id), AV_OPT_TYPE_INT, { .i64 = XEVE_PROFILE_BASELINE }, XEVE_PROFILE_BASELINE,  XEVE_PROFILE_MAIN, VE, .unit = "profile" },
    { "baseline", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_PROFILE_BASELINE }, INT_MIN, INT_MAX, VE, .unit = "profile" },
    { "main",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_PROFILE_MAIN },     INT_MIN, INT_MAX, VE, .unit = "profile" },
    { "rc_mode", "Rate control mode", OFFSET(rc_mode), AV_OPT_TYPE_INT, { .i64 = XEVE_RC_CQP }, XEVE_RC_CQP,  XEVE_RC_CRF, VE, .unit = "rc_mode" },
    { "CQP", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_RC_CQP }, INT_MIN, INT_MAX, VE, .unit = "rc_mode" },
    { "ABR", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_RC_ABR }, INT_MIN, INT_MAX, VE, .unit = "rc_mode" },
    { "CRF", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = XEVE_RC_CRF }, INT_MIN, INT_MAX, VE, .unit = "rc_mode" },
    { "qp", "Quantization parameter value for CQP rate control mode", OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 32 }, 0, 51, VE },
    { "crf", "Constant rate factor value for CRF rate control mode", OFFSET(crf), AV_OPT_TYPE_INT, { .i64 = 32 }, 10, 49, VE },
    { "hash", "Embed picture signature (HASH) for conformance checking in decoding", OFFSET(hash), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE },
    { "sei_info", "Embed SEI messages identifying encoder parameters and command line arguments", OFFSET(sei_info), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE },
    { "xeve-params",  "Override the xeve configuration using a :-separated list of key=value parameters", OFFSET(xeve_params), AV_OPT_TYPE_DICT, { 0 }, 0, 0, VE },
    { NULL }
};

static const AVClass libxeve_class = {
    .class_name = "libxeve",
    .item_name  = av_default_item_name,
    .option     = libxeve_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

/**
 *  libavcodec generic global options, which can be set on all the encoders and decoders
 *  @see https://www.ffmpeg.org/ffmpeg-codecs.html#Codec-Options
 */
static const FFCodecDefault libxeve_defaults[] = {
    { "b", "0" },       // bitrate in terms of kilo-bits per second
    { "g", "0" },       // gop_size (key-frame interval 0: only one I-frame at the first time; 1: every frame is coded in I-frame)
    { "bf", "15"},      // maximum number of B frames (0: no B-frames, 1,3,7,15)
    { "threads", "0"},  // number of threads to be used (0: automatically select the number of threads to set)
    { NULL },
};

const FFCodec ff_libxeve_encoder = {
    .p.name             = "libxeve",
    .p.long_name        = NULL_IF_CONFIG_SMALL("libxeve MPEG-5 EVC"),
    .p.type             = AVMEDIA_TYPE_VIDEO,
    .p.id               = AV_CODEC_ID_EVC,
    .init               = libxeve_init,
    FF_CODEC_ENCODE_CB(libxeve_encode),
    .close              = libxeve_close,
    .priv_data_size     = sizeof(XeveContext),
    .p.priv_class       = &libxeve_class,
    .defaults           = libxeve_defaults,
    .p.capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS | AV_CODEC_CAP_DR1,
    .p.profiles         = NULL_IF_CONFIG_SMALL(ff_evc_profiles),
    .p.wrapper_name     = "libxeve",
    CODEC_PIXFMTS_ARRAY(supported_pixel_formats),
    .color_ranges       = AVCOL_RANGE_MPEG, /* FIXME: implement tagging */
    .caps_internal      = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_NOT_INIT_THREADSAFE,
};
