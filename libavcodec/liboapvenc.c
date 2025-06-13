/*
 * liboapv encoder
 * Advanced Professional Video codec library
 *
 * Copyright (C) 2025 Dawid Kozinski <d.kozinski@samsung.com>
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

#include <stdint.h>
#include <stdlib.h>

#include <oapv/oapv.h>

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "apv.h"
#include "codec_internal.h"
#include "encode.h"
#include "packet_internal.h"
#include "profiles.h"

#define MAX_BS_BUF   (128 * 1024 * 1024)
#define MAX_NUM_FRMS (1)           // supports only 1-frame in an access unit
#define FRM_IDX      (0)           // supports only 1-frame in an access unit
#define MAX_NUM_CC   (OAPV_MAX_CC) // Max number of color componets (upto 4:4:4:4)

/**
 * The structure stores all the states associated with the instance of APV encoder
 */
typedef struct ApvEncContext {
    const AVClass *class;

    oapve_t id;             // APV instance identifier
    oapvm_t mid;
    oapve_cdesc_t   cdsc;   // coding parameters i.e profile, width & height of input frame, num of therads, frame rate ...
    oapv_bitb_t     bitb;   // bitstream buffer (output)
    oapve_stat_t    stat;   // encoding status (output)

    oapv_frms_t ifrms;      // frames for input

    int num_frames;         // number of frames in an access unit

    int preset_id;          // preset of apv ( fastest, fast, medium, slow, placebo)

    int qp;                 // quantization parameter (QP) [0,63]

    AVDictionary *oapv_params;
} ApvEncContext;

static int apv_imgb_release(oapv_imgb_t *imgb)
{
    int refcnt = --imgb->refcnt;
    if (refcnt == 0) {
        for (int i = 0; i < imgb->np; i++)
            av_freep(&imgb->baddr[i]);
        av_free(imgb);
    }

    return refcnt;
}

static int apv_imgb_addref(oapv_imgb_t * imgb)
{
    int refcnt = ++imgb->refcnt;
    return refcnt;
}

static int apv_imgb_getref(oapv_imgb_t * imgb)
{
    return imgb->refcnt;
}

/**
 * Convert FFmpeg pixel format (AVPixelFormat) into APV pre-defined color format
 *
 * @return APV pre-defined color format (@see oapv.h) on success, OAPV_CF_UNKNOWN on failure
 */
static inline int get_color_format(enum AVPixelFormat pix_fmt)
{
    int cf = OAPV_CF_UNKNOWN;

    switch (pix_fmt) {
    case AV_PIX_FMT_YUV422P10:
        cf = OAPV_CF_YCBCR422;
        break;
    default:
        av_assert0(cf != OAPV_CF_UNKNOWN);
    }

    return cf;
}

static oapv_imgb_t *apv_imgb_create(AVCodecContext *avctx)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    oapv_imgb_t *imgb;
    int input_depth;
    int cfmt;  // color format
    int cs;

    av_assert0(desc);

    imgb = av_mallocz(sizeof(oapv_imgb_t));
    if (!imgb)
        goto fail;

    input_depth = desc->comp[0].depth;
    cfmt = get_color_format(avctx->pix_fmt);
    cs = OAPV_CS_SET(cfmt, input_depth, AV_HAVE_BIGENDIAN);

    imgb->np = desc->nb_components;

    for (int i = 0; i < imgb->np; i++) {
        imgb->w[i]  = avctx->width >> ((i == 1 || i == 2) ? desc->log2_chroma_w : 0);
        imgb->h[i]  = avctx->height;
        imgb->aw[i] = FFALIGN(imgb->w[i], OAPV_MB_W);
        imgb->ah[i] = FFALIGN(imgb->h[i], OAPV_MB_H);
        imgb->s[i]  = imgb->aw[i] * OAPV_CS_GET_BYTE_DEPTH(cs);

        imgb->bsize[i] = imgb->e[i] = imgb->s[i] * imgb->ah[i];
        imgb->a[i] = imgb->baddr[i] = av_mallocz(imgb->bsize[i]);
        if (imgb->a[i] == NULL)
            goto fail;
    }

    imgb->cs = cs;
    imgb->addref = apv_imgb_addref;
    imgb->getref = apv_imgb_getref;
    imgb->release = apv_imgb_release;
    imgb->refcnt = 1;

    return imgb;
fail:
    av_log(avctx, AV_LOG_ERROR, "cannot create image buffer\n");
    if (imgb) {
        for (int i = 0; i < imgb->np; i++)
            av_freep(&imgb->a[i]);
        av_freep(&imgb);
    }
    return NULL;
}

/**
 * The function returns a pointer to the object of the oapve_cdesc_t type.
 * oapve_cdesc_t contains all encoder parameters that should be initialized before the encoder is used.
 *
 * The field values of the oapve_cdesc_t structure are populated based on:
 * - the corresponding field values of the AvCodecConetxt structure,
 * - the apv encoder specific option values,
 *
 * The order of processing input data and populating the apve_cdsc structure
 * 1) first, the fields of the AVCodecContext structure corresponding to the provided input options are processed,
 *    (i.e -pix_fmt yuv422p -s:v 1920x1080 -r 30 -profile:v 0)
 * 2) then apve-specific options added as AVOption to the apv AVCodec implementation
 *    (i.e -preset 0)
 *
 * Keep in mind that, there are options that can be set in different ways.
 * In this case, please follow the above-mentioned order of processing.
 * The most recent assignments overwrite the previous values.
 *
 * @param[in] avctx codec context (AVCodecContext)
 * @param[out] cdsc contains all APV encoder encoder parameters that should be initialized before the encoder is use
 *
 * @return 0 on success, negative error code on failure
 */
static int get_conf(AVCodecContext *avctx, oapve_cdesc_t *cdsc)
{
    ApvEncContext *apv = avctx->priv_data;

    /* initialize apv_param struct with default values */
    int ret = oapve_param_default(&cdsc->param[FRM_IDX]);
    if (OAPV_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Cannot set default parameter\n");
        return AVERROR_EXTERNAL;
    }

    /* read options from AVCodecContext */
    if (avctx->width > 0)
        cdsc->param[FRM_IDX].w = avctx->width;

    if (avctx->height > 0)
        cdsc->param[FRM_IDX].h = avctx->height;

    if (avctx->framerate.num > 0) {
        cdsc->param[FRM_IDX].fps_num = avctx->framerate.num;
        cdsc->param[FRM_IDX].fps_den = avctx->framerate.den;
    } else if (avctx->time_base.num > 0) {
        cdsc->param[FRM_IDX].fps_num = avctx->time_base.den;
        cdsc->param[FRM_IDX].fps_den = avctx->time_base.num;
    }

    cdsc->param[FRM_IDX].preset = apv->preset_id;
    cdsc->param[FRM_IDX].qp = apv->qp;
    if (avctx->bit_rate / 1000 > INT_MAX || avctx->rc_max_rate / 1000 > INT_MAX) {
        av_log(avctx, AV_LOG_ERROR, "bit_rate and rc_max_rate > %d000 is not supported\n", INT_MAX);
        return AVERROR(EINVAL);
    }
    cdsc->param[FRM_IDX].bitrate = (int)(avctx->bit_rate / 1000);
    if (cdsc->param[FRM_IDX].bitrate) {
        if (cdsc->param[FRM_IDX].qp) {
            av_log(avctx, AV_LOG_WARNING, "You cannot set both the bitrate and the QP parameter at the same time.\n"
                                          "If the bitrate is set, the rate control type is set to ABR, which means that the QP value is ignored.\n");
        }
        cdsc->param[FRM_IDX].rc_type = OAPV_RC_ABR;
    }

    cdsc->threads = avctx->thread_count;

    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED) {
        cdsc->param[FRM_IDX].color_primaries = avctx->color_primaries;
        cdsc->param[FRM_IDX].color_description_present_flag = 1;
    }

    if (avctx->color_trc != AVCOL_TRC_UNSPECIFIED) {
        cdsc->param[FRM_IDX].transfer_characteristics = avctx->color_trc;
        cdsc->param[FRM_IDX].color_description_present_flag = 1;
    }

    if (avctx->colorspace != AVCOL_SPC_UNSPECIFIED) {
        cdsc->param[FRM_IDX].matrix_coefficients = avctx->colorspace;
        cdsc->param[FRM_IDX].color_description_present_flag = 1;
    }

    if (avctx->color_range != AVCOL_RANGE_UNSPECIFIED) {
        cdsc->param[FRM_IDX].full_range_flag = (avctx->color_range == AVCOL_RANGE_JPEG);
        cdsc->param[FRM_IDX].color_description_present_flag = 1;
    }

    cdsc->max_bs_buf_size = MAX_BS_BUF; /* maximum bitstream buffer size */
    cdsc->max_num_frms = MAX_NUM_FRMS;

    const AVDictionaryEntry *en = NULL;
    while (en = av_dict_iterate(apv->oapv_params, en)) {
        ret = oapve_param_parse(&cdsc->param[FRM_IDX], en->key, en->value);
        if (ret < 0)
            av_log(avctx, AV_LOG_WARNING, "Error parsing option '%s = %s'.\n", en->key, en->value);
    }

    return 0;
}

/**
 * @brief Initialize APV codec
 * Create an encoder instance and allocate all the needed resources
 *
 * @param avctx codec context
 * @return 0 on success, negative error code on failure
 */
static av_cold int liboapve_init(AVCodecContext *avctx)
{
    ApvEncContext *apv = avctx->priv_data;
    oapve_cdesc_t *cdsc = &apv->cdsc;
    unsigned char *bs_buf;
    int ret;

    /* allocate bitstream buffer */
    bs_buf = (unsigned char *)av_malloc(MAX_BS_BUF);
    if (bs_buf == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate bitstream buffer, size=%d\n", MAX_BS_BUF);
        return AVERROR(ENOMEM);
    }
    apv->bitb.addr = bs_buf;
    apv->bitb.bsize = MAX_BS_BUF;

    /* read configurations and set values for created descriptor (APV_CDSC) */
    ret = get_conf(avctx, cdsc);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Cannot get OAPV configuration\n");
        return ret;
    }

    /* create encoder */
    apv->id = oapve_create(cdsc, &ret);
    if (apv->id == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Cannot create OAPV encoder\n");
        if (ret == OAPV_ERR_INVALID_LEVEL)
            av_log(avctx, AV_LOG_ERROR, "Invalid level idc: %d\n", cdsc->param[0].level_idc);
        return AVERROR_EXTERNAL;
    }

    /* create metadata handler */
    apv->mid = oapvm_create(&ret);
    if (apv->mid == NULL || OAPV_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "cannot create OAPV metadata handler\n");
        return AVERROR_EXTERNAL;
    }

    int value = OAPV_CFG_VAL_AU_BS_FMT_NONE;
    int size = 4;
    ret = oapve_config(apv->id, OAPV_CFG_SET_AU_BS_FMT, &value, &size);
    if (OAPV_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to set config for using encoder output format\n");
        return AVERROR_EXTERNAL;
    }

    apv->ifrms.frm[FRM_IDX].imgb = apv_imgb_create(avctx);
    if (apv->ifrms.frm[FRM_IDX].imgb == NULL)
        return AVERROR(ENOMEM);
    apv->ifrms.num_frms++;

     /* color description values */
    if (cdsc->param[FRM_IDX].color_description_present_flag) {
        avctx->color_primaries = cdsc->param[FRM_IDX].color_primaries;
        avctx->color_trc = cdsc->param[FRM_IDX].transfer_characteristics;
        avctx->colorspace = cdsc->param[FRM_IDX].matrix_coefficients;
        avctx->color_range = (cdsc->param[FRM_IDX].full_range_flag) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    }

    return 0;
}

/**
  * Encode raw data frame into APV packet
  *
  * @param[in]  avctx codec context
  * @param[out] avpkt output AVPacket containing encoded data
  * @param[in]  frame AVFrame containing the raw data to be encoded
  * @param[out] got_packet encoder sets to 0 or 1 to indicate that a
  *                         non-empty packet was returned in pkt
  *
  * @return 0 on success, negative error code on failure
  */
static int liboapve_encode(AVCodecContext *avctx, AVPacket *avpkt,
                          const AVFrame *frame, int *got_packet)
{
    ApvEncContext *apv =  avctx->priv_data;
    const oapve_cdesc_t *cdsc = &apv->cdsc;
    oapv_frm_t *frm = &apv->ifrms.frm[FRM_IDX];
    oapv_imgb_t *imgb = frm->imgb;
    int ret;

    if (avctx->width != frame->width || avctx->height != frame->height || avctx->pix_fmt != frame->format) {
        av_log(avctx, AV_LOG_ERROR, "Dimension changes are not supported\n");
        return AVERROR(EINVAL);
    }

    av_image_copy((uint8_t **)imgb->a, imgb->s, (const uint8_t **)frame->data, frame->linesize,
                  frame->format, frame->width, frame->height);

    imgb->ts[0] = frame->pts;

    frm->group_id = 1; // @todo FIX-ME : need to set properly in case of multi-frame
    frm->pbu_type = OAPV_PBU_TYPE_PRIMARY_FRAME;

    ret = oapve_encode(apv->id, &apv->ifrms, apv->mid, &apv->bitb, &apv->stat, NULL);
    if (OAPV_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "oapve_encode() failed\n");
        return AVERROR_EXTERNAL;
    }

    /* store bitstream */
    if (OAPV_SUCCEEDED(ret) && apv->stat.write > 0) {
        uint8_t *data = apv->bitb.addr;
        int size = apv->stat.write;

        // The encoder may return a "Raw bitstream" formated AU, including au_size.
        // Discard it as we only need the access_unit() structure.
        if (size > 4 && AV_RB32(data) != APV_SIGNATURE) {
            data += 4;
            size -= 4;
        }

        ret = ff_get_encode_buffer(avctx, avpkt, size, 0);
        if (ret < 0)
            return ret;

        memcpy(avpkt->data, data, size);
        avpkt->pts = avpkt->dts = frame->pts;
        avpkt->flags |= AV_PKT_FLAG_KEY;

        if (cdsc->param[FRM_IDX].qp)
            ff_side_data_set_encoder_stats(avpkt, cdsc->param[FRM_IDX].qp * FF_QP2LAMBDA, NULL, 0, AV_PICTURE_TYPE_I);

        *got_packet = 1;
    }

    return 0;
}

/**
 * Destroy the encoder and release all the allocated resources
 *
 * @param avctx codec context
 * @return 0 on success, negative error code on failure
 */
static av_cold int liboapve_close(AVCodecContext *avctx)
{
    ApvEncContext *apv = avctx->priv_data;

    for (int i = 0; i < apv->num_frames; i++) {
        if (apv->ifrms.frm[i].imgb != NULL)
            apv->ifrms.frm[i].imgb->release(apv->ifrms.frm[i].imgb);
        apv->ifrms.frm[i].imgb = NULL;
    }

    if (apv->mid) {
        oapvm_rem_all(apv->mid);
    }

    if (apv->id) {
        oapve_delete(apv->id);
        apv->id = NULL;
    }

    if (apv->mid) {
        oapvm_delete(apv->mid);
        apv->mid = NULL;
    }

    av_freep(&apv->bitb.addr); /* release bitstream buffer */

    return 0;
}

#define OFFSET(x) offsetof(ApvEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const enum AVPixelFormat supported_pixel_formats[] = {
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_NONE
};

static const AVOption liboapv_options[] = {
    { "preset", "Encoding preset for setting encoding speed (optimization level control)", OFFSET(preset_id), AV_OPT_TYPE_INT, { .i64 = OAPV_PRESET_DEFAULT }, OAPV_PRESET_FASTEST, OAPV_PRESET_PLACEBO, VE, .unit = "preset" },
    { "fastest", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_FASTEST }, INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "fast",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_FAST },    INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "medium",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_MEDIUM },  INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "slow",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_SLOW },    INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "placebo", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_PLACEBO }, INT_MIN, INT_MAX, VE, .unit = "preset" },
    { "default", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = OAPV_PRESET_DEFAULT }, INT_MIN, INT_MAX, VE, .unit = "preset" },

    { "qp", "Quantization parameter value for CQP rate control mode", OFFSET(qp), AV_OPT_TYPE_INT, { .i64 = 32 }, 0, 63, VE },
    { "oapv-params",  "Override the apv configuration using a :-separated list of key=value parameters", OFFSET(oapv_params), AV_OPT_TYPE_DICT, { 0 }, 0, 0, VE },
    { NULL }
};

static const AVClass liboapve_class = {
    .class_name = "liboapv",
    .item_name  = av_default_item_name,
    .option     = liboapv_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault liboapve_defaults[] = {
    { "b", "0" },       // bitrate in terms of kilo-bits per second (support for bit-rates from a few hundred Mbps to a few Gbps for 2K, 4K and 8K resolution content)
    { NULL },
};

const FFCodec ff_liboapv_encoder = {
    .p.name             = "liboapv",
    .p.long_name        = NULL_IF_CONFIG_SMALL("liboapv APV"),
    .p.type             = AVMEDIA_TYPE_VIDEO,
    .p.id               = AV_CODEC_ID_APV,
    .init               = liboapve_init,
    FF_CODEC_ENCODE_CB(liboapve_encode),
    .close              = liboapve_close,
    .priv_data_size     = sizeof(ApvEncContext),
    .p.priv_class       = &liboapve_class,
    .defaults           = liboapve_defaults,
    .p.capabilities     = AV_CODEC_CAP_OTHER_THREADS | AV_CODEC_CAP_DR1,
    .p.wrapper_name     = "liboapv",
    .p.pix_fmts         = supported_pixel_formats,
    .p.profiles         = NULL_IF_CONFIG_SMALL(ff_apv_profiles),
    .caps_internal      = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_AUTO_THREADS | FF_CODEC_CAP_NOT_INIT_THREADSAFE,
};
