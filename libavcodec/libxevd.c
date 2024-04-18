/*
 * libxevd decoder
 * EVC (MPEG-5 Essential Video Coding) decoding using XEVD MPEG-5 EVC decoder library
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

#include <stddef.h>

#include <xevd.h>

#include "libavutil/internal.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/imgutils.h"
#include "libavutil/cpu.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "profiles.h"
#include "decode.h"

#define XEVD_PARAM_BAD_NAME -1
#define XEVD_PARAM_BAD_VALUE -2

#define EVC_NAL_HEADER_SIZE 2 /* byte */

/**
 * The structure stores all the states associated with the instance of Xeve MPEG-5 EVC decoder
 */
typedef struct XevdContext {
    XEVD id;            // XEVD instance identifier @see xevd.h
    XEVD_CDSC cdsc;     // decoding parameters @see xevd.h

    // If end of stream occurs it is required "flushing" (aka draining) the codec,
    // as the codec might buffer multiple frames or packets internally.
    int draining_mode; // The flag is set if codec enters draining mode.

    AVPacket *pkt;     // access unit (a set of NAL units that are consecutive in decoding order and containing exactly one encoded image)
} XevdContext;

/**
 * The function populates the XEVD_CDSC structure.
 * XEVD_CDSC contains all decoder parameters that should be initialized before its use.
 *
 * @param[in] avctx codec context
 * @param[out] cdsc contains all decoder parameters that should be initialized before its use
 *
 */
static void get_conf(AVCodecContext *avctx, XEVD_CDSC *cdsc)
{
    int cpu_count = av_cpu_count();

    /* clear XEVS_CDSC structure */
    memset(cdsc, 0, sizeof(XEVD_CDSC));

    /* init XEVD_CDSC */
    if (avctx->thread_count <= 0)
        cdsc->threads = (cpu_count < XEVD_MAX_TASK_CNT) ? cpu_count : XEVD_MAX_TASK_CNT;
    else if (avctx->thread_count > XEVD_MAX_TASK_CNT)
        cdsc->threads = XEVD_MAX_TASK_CNT;
    else
        cdsc->threads = avctx->thread_count;
}

/**
 * Read NAL unit length
 * @param bs input data (bitstream)
 * @return the length of NAL unit on success, 0 value on failure
 */
static uint32_t read_nal_unit_length(const uint8_t *bs, int bs_size, AVCodecContext *avctx)
{
    uint32_t len = 0;
    XEVD_INFO info;
    int ret;

    if (bs_size == XEVD_NAL_UNIT_LENGTH_BYTE) {
        ret = xevd_info((void *)bs, XEVD_NAL_UNIT_LENGTH_BYTE, 1, &info);
        if (XEVD_FAILED(ret)) {
            av_log(avctx, AV_LOG_ERROR, "Cannot get bitstream information\n");
            return 0;
        }
        len = info.nalu_len;
        if (len == 0) {
            av_log(avctx, AV_LOG_ERROR, "Invalid bitstream size! [%d]\n", bs_size);
            return 0;
        }
    }

    return len;
}

/**
 * @param[in] xectx the structure that stores all the state associated with the instance of Xeve MPEG-5 EVC decoder
 * @param[out] avctx codec context
 * @return 0 on success, negative value on failure
 */
static int export_stream_params(const XevdContext *xectx, AVCodecContext *avctx)
{
    int ret;
    int size;
    int color_space;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P10;

    size = 4;
    ret = xevd_config(xectx->id, XEVD_CFG_GET_CODED_WIDTH, &avctx->coded_width, &size);
    if (XEVD_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get coded_width\n");
        return AVERROR_EXTERNAL;
    }

    ret = xevd_config(xectx->id, XEVD_CFG_GET_CODED_HEIGHT, &avctx->coded_height, &size);
    if (XEVD_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get coded_height\n");
        return AVERROR_EXTERNAL;
    }

    ret = xevd_config(xectx->id, XEVD_CFG_GET_WIDTH, &avctx->width, &size);
    if (XEVD_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get width\n");
        return AVERROR_EXTERNAL;
    }

    ret = xevd_config(xectx->id, XEVD_CFG_GET_HEIGHT, &avctx->height, &size);
    if (XEVD_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get height\n");
        return AVERROR_EXTERNAL;
    }

    ret = xevd_config(xectx->id, XEVD_CFG_GET_COLOR_SPACE, &color_space, &size);
    if (XEVD_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get color_space\n");
        return AVERROR_EXTERNAL;
    }
    switch(color_space) {
    case XEVD_CS_YCBCR400_10LE:
        avctx->pix_fmt = AV_PIX_FMT_GRAY10LE;
        break;
    case XEVD_CS_YCBCR420_10LE:
        avctx->pix_fmt = AV_PIX_FMT_YUV420P10LE;
        break;
    case XEVD_CS_YCBCR422_10LE:
        avctx->pix_fmt = AV_PIX_FMT_YUV422P10LE;
        break;
    case XEVD_CS_YCBCR444_10LE:
        avctx->pix_fmt = AV_PIX_FMT_YUV444P10LE;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown color space\n");
        avctx->pix_fmt = AV_PIX_FMT_NONE;
        return AVERROR_INVALIDDATA;
    }

    // the function returns sps->num_reorder_pics
    ret = xevd_config(xectx->id, XEVD_CFG_GET_MAX_CODING_DELAY, &avctx->has_b_frames, &size);
    if (XEVD_FAILED(ret)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to get max_coding_delay\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

/**
 * @brief Copy image in imgb to frame.
 *
 * @param avctx codec context
 * @param[in] imgb
 * @param[out] frame
 * @return 0 on success, negative value on failure
 */
static int libxevd_image_copy(struct AVCodecContext *avctx, XEVD_IMGB *imgb, struct AVFrame *frame)
{
    int ret;
    if (imgb->cs != XEVD_CS_YCBCR420_10LE) {
        av_log(avctx, AV_LOG_ERROR, "Not supported pixel format: %s\n", av_get_pix_fmt_name(avctx->pix_fmt));
        return AVERROR_INVALIDDATA;
    }

    if (imgb->w[0] != avctx->width || imgb->h[0] != avctx->height) { // stream resolution changed
        if (ff_set_dimensions(avctx, imgb->w[0], imgb->h[0]) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Cannot set new dimension\n");
            return AVERROR_INVALIDDATA;
        }
    }

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    av_image_copy(frame->data, frame->linesize, (const uint8_t **)imgb->a,
                  imgb->s, avctx->pix_fmt,
                  imgb->w[0], imgb->h[0]);

    return 0;
}

/**
 * Initialize decoder
 * Create a decoder instance and allocate all the needed resources
 *
 * @param avctx codec context
 * @return 0 on success, negative error code on failure
 */
static av_cold int libxevd_init(AVCodecContext *avctx)
{
    XevdContext *xectx = avctx->priv_data;
    XEVD_CDSC *cdsc = &(xectx->cdsc);

    /* read configurations and set values for created descriptor (XEVD_CDSC) */
    get_conf(avctx, cdsc);

    /* create decoder */
    xectx->id = xevd_create(&(xectx->cdsc), NULL);
    if (xectx->id == NULL) {
        av_log(avctx, AV_LOG_ERROR, "Cannot create XEVD encoder\n");
        return AVERROR_EXTERNAL;
    }

    xectx->draining_mode = 0;
    xectx->pkt = av_packet_alloc();
    if (!xectx->pkt) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate memory for AVPacket\n");
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int libxevd_return_frame(AVCodecContext *avctx, AVFrame *frame,
                                XEVD_IMGB *imgb, AVPacket **pkt_au)
{
    AVPacket *pkt_au_imgb = (AVPacket*)imgb->pdata[0];
    int ret;

    if (!pkt_au_imgb) {
        av_log(avctx, AV_LOG_ERROR, "Invalid data needed to fill frame properties\n");

        if (pkt_au)
            av_packet_free(pkt_au);
        av_frame_unref(frame);

        imgb->release(imgb);
        imgb = NULL;

        return AVERROR_INVALIDDATA;
    }

    // got frame
    ret = libxevd_image_copy(avctx, imgb, frame);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Image copying error\n");

        av_packet_free(&pkt_au_imgb);
        av_frame_unref(frame);

        imgb->release(imgb);
        imgb = NULL;

        return ret;
    }

    // use ff_decode_frame_props_from_pkt() to fill frame properties
    ret = ff_decode_frame_props_from_pkt(avctx, frame, pkt_au_imgb);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_decode_frame_props_from_pkt error\n");

        av_packet_free(&pkt_au_imgb);
        av_frame_unref(frame);

        imgb->release(imgb);
        imgb = NULL;

        return ret;
    }

    frame->pkt_dts = imgb->ts[XEVD_TS_DTS];
    frame->pts = imgb->ts[XEVD_TS_PTS];

    av_packet_free(&pkt_au_imgb);

    // xevd_pull uses pool of objects of type XEVD_IMGB.
    // The pool size is equal MAX_PB_SIZE (26), so release object when it is no more needed
    imgb->release(imgb);
    imgb = NULL;

    return 0;
}
/**
  * Decode frame with decoupled packet/frame dataflow
  *
  * @param avctx codec context
  * @param[out] frame decoded frame
  *
  * @return 0 on success, negative error code on failure
  */
static int libxevd_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    XevdContext *xectx = avctx->priv_data;
    AVPacket *pkt = xectx->pkt;
    XEVD_IMGB *imgb = NULL;

    int xevd_ret = 0;
    int ret = 0;

    // obtain access unit (input data) - a set of NAL units that are consecutive in decoding order and containing exactly one encoded image
    ret = ff_decode_get_packet(avctx, pkt);
    if (ret < 0 && ret != AVERROR_EOF) {
        av_packet_unref(pkt);

        return ret;
    } else if(ret == AVERROR_EOF && xectx->draining_mode == 0) { // End of stream situations. Enter draining mode

        xectx->draining_mode = 1;
        av_packet_unref(pkt);
    }

    if (pkt->size > 0) {
        int bs_read_pos = 0;
        XEVD_STAT stat;
        XEVD_BITB bitb;
        int nalu_size;
        AVPacket *pkt_au = av_packet_alloc();
        imgb = NULL;

        if (!pkt_au) {
            av_packet_unref(pkt);
            return AVERROR(ENOMEM);
        }
        FFSWAP(AVPacket*, pkt_au, xectx->pkt);

        // get all nal units from AU
        while(pkt_au->size > (bs_read_pos + XEVD_NAL_UNIT_LENGTH_BYTE)) {
            memset(&stat, 0, sizeof(XEVD_STAT));

            nalu_size = read_nal_unit_length(pkt_au->data + bs_read_pos, XEVD_NAL_UNIT_LENGTH_BYTE, avctx);
            if (nalu_size == 0) {
                av_log(avctx, AV_LOG_ERROR, "Invalid bitstream\n");
                av_packet_free(&pkt_au);
                ret = AVERROR_INVALIDDATA;

                return ret;
            }
            bs_read_pos += XEVD_NAL_UNIT_LENGTH_BYTE;

            bitb.addr = pkt_au->data + bs_read_pos;
            bitb.ssize = nalu_size;
            bitb.pdata[0] = pkt_au;
            bitb.ts[XEVD_TS_DTS] = pkt_au->dts;

            /* main decoding block */
            xevd_ret = xevd_decode(xectx->id, &bitb, &stat);
            if (XEVD_FAILED(xevd_ret)) {
                av_log(avctx, AV_LOG_ERROR, "Failed to decode bitstream\n");
                av_packet_free(&pkt_au);

                return AVERROR_EXTERNAL;
            }

            bs_read_pos += nalu_size;

            if (stat.nalu_type == XEVD_NUT_SPS) { // EVC stream parameters changed
                if ((ret = export_stream_params(xectx, avctx)) != 0) {
                    av_log(avctx, AV_LOG_ERROR, "Failed to export stream params\n");
                    av_packet_free(&pkt_au);

                    return ret;
                }
            }

            if (stat.read != nalu_size)
                av_log(avctx, AV_LOG_INFO, "Different reading of bitstream (in:%d, read:%d)\n,", nalu_size, stat.read);

            // stat.fnum - has negative value if the decoded data is not frame
            if (stat.fnum >= 0) {

                xevd_ret = xevd_pull(xectx->id, &imgb); // The function returns a valid image only if the return code is XEVD_OK

                if (XEVD_FAILED(xevd_ret)) {
                    av_log(avctx, AV_LOG_ERROR, "Failed to pull the decoded image (xevd error code: %d, frame#=%d)\n", xevd_ret, stat.fnum);

                    av_packet_free(&pkt_au);

                    return AVERROR_EXTERNAL;
                } else if (xevd_ret == XEVD_OK_FRM_DELAYED) {
                    if(bs_read_pos == pkt_au->size) {
                        return AVERROR(EAGAIN);
                    }
                } else { // XEVD_OK
                    if (!imgb) {
                        if(bs_read_pos == pkt_au->size) {
                            av_log(avctx, AV_LOG_ERROR, "Invalid decoded image data\n");

                            av_packet_free(&pkt_au);
                            return  AVERROR(EAGAIN);
                        }
                    } else {
                        if (stat.stype == XEVD_ST_I) {
                            frame->pict_type = AV_PICTURE_TYPE_I;
                            frame->flags |= AV_FRAME_FLAG_KEY;
                        }
                        return libxevd_return_frame(avctx, frame, imgb, &pkt_au);
                    }
                }
            }
        }
    } else { // decoder draining mode handling

        xevd_ret = xevd_pull(xectx->id, &imgb);

        if (xevd_ret == XEVD_ERR_UNEXPECTED) { // draining process completed
            av_log(avctx, AV_LOG_DEBUG, "Draining process completed\n");

            return AVERROR_EOF;
        } else if (XEVD_FAILED(xevd_ret)) { // handle all other errors
            av_log(avctx, AV_LOG_ERROR, "Failed to pull the decoded image (xevd error code: %d)\n", xevd_ret);

            return AVERROR_EXTERNAL;
        } else { // XEVD_OK
            if (!imgb) {
                av_log(avctx, AV_LOG_ERROR, "Invalid decoded image data\n");

                return AVERROR_EXTERNAL;
            }

            return libxevd_return_frame(avctx, frame, imgb, NULL);
        }
    }

    return ret;
}

/**
 * Destroy decoder
 *
 * @param avctx codec context
 * @return 0 on success
 */
static av_cold int libxevd_close(AVCodecContext *avctx)
{
    XevdContext *xectx = avctx->priv_data;
    if (xectx->id) {
        xevd_delete(xectx->id);
        xectx->id = NULL;
    }

    xectx->draining_mode = 0;
    av_packet_free(&xectx->pkt);

    return 0;
}

const FFCodec ff_libxevd_decoder = {
    .p.name             = "evc",
    CODEC_LONG_NAME("EVC / MPEG-5 Essential Video Coding (EVC)"),
    .p.type             = AVMEDIA_TYPE_VIDEO,
    .p.id               = AV_CODEC_ID_EVC,
    .init               = libxevd_init,
    FF_CODEC_RECEIVE_FRAME_CB(libxevd_receive_frame),
    .close              = libxevd_close,
    .priv_data_size     = sizeof(XevdContext),
    .p.capabilities     = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                          AV_CODEC_CAP_OTHER_THREADS | AV_CODEC_CAP_AVOID_PROBING,
    .p.profiles         = NULL_IF_CONFIG_SMALL(ff_evc_profiles),
    .p.wrapper_name     = "libxevd",
    .caps_internal      = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                          FF_CODEC_CAP_SETS_FRAME_PROPS,
};
