/*
 * - CrystalHD decoder module -
 *
 * Copyright(C) 2010,2011 Philip Langdale <ffmpeg.philipl@overt.org>
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

/*
 * - Principles of Operation -
 *
 * The CrystalHD decoder operates at the bitstream level - which is an even
 * higher level than the decoding hardware you typically see in modern GPUs.
 * This means it has a very simple interface, in principle. You feed demuxed
 * packets in one end and get decoded picture (fields/frames) out the other.
 *
 * Of course, nothing is ever that simple. Due, at the very least, to b-frame
 * dependencies in the supported formats, the hardware has a delay between
 * when a packet goes in, and when a picture comes out. Furthermore, this delay
 * is not just a function of time, but also one of the dependency on additional
 * frames being fed into the decoder to satisfy the b-frame dependencies.
 *
 * As such, the hardware can only be used effectively with a decode API that
 * doesn't assume a 1:1 relationship between input packets and output frames.
 * The new avcodec decode API is such an API (an m:n API) while the old one is
 * 1:1. Consequently, we no longer support the old API, which allows us to avoid
 * the vicious hacks that are required to approximate 1:1 operation.
 */

/*****************************************************************************
 * Includes
 ****************************************************************************/

#define _XOPEN_SOURCE 600
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <libcrystalhd/bc_dts_types.h>
#include <libcrystalhd/bc_dts_defs.h>
#include <libcrystalhd/libcrystalhd_if.h>

#include "avcodec.h"
#include "internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

/** Timeout parameter passed to DtsProcOutput() in us */
#define OUTPUT_PROC_TIMEOUT 50
/** Step between fake timestamps passed to hardware in units of 100ns */
#define TIMESTAMP_UNIT 100000


/*****************************************************************************
 * Module private data
 ****************************************************************************/

typedef enum {
    RET_ERROR           = -1,
    RET_OK              = 0,
    RET_COPY_AGAIN      = 1,
} CopyRet;

typedef struct OpaqueList {
    struct OpaqueList *next;
    uint64_t fake_timestamp;
    uint64_t reordered_opaque;
} OpaqueList;

typedef struct {
    AVClass *av_class;
    AVCodecContext *avctx;
    HANDLE dev;

    uint8_t *orig_extradata;
    uint32_t orig_extradata_size;

    AVBSFContext *bsfc;

    uint8_t is_70012;
    uint8_t *sps_pps_buf;
    uint32_t sps_pps_size;
    uint8_t is_nal;
    uint8_t need_second_field;
    uint8_t draining;

    OpaqueList *head;
    OpaqueList *tail;

    /* Options */
    uint32_t sWidth;
    uint8_t bframe_bug;
} CHDContext;

static const AVOption options[] = {
    { "crystalhd_downscale_width",
      "Turn on downscaling to the specified width",
      offsetof(CHDContext, sWidth),
      AV_OPT_TYPE_INT, {.i64 = 0}, 0, UINT32_MAX,
      AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM, },
    { NULL, },
};


/*****************************************************************************
 * Helper functions
 ****************************************************************************/

static inline BC_MEDIA_SUBTYPE id2subtype(CHDContext *priv, enum AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_MPEG4:
        return BC_MSUBTYPE_DIVX;
    case AV_CODEC_ID_MSMPEG4V3:
        return BC_MSUBTYPE_DIVX311;
    case AV_CODEC_ID_MPEG2VIDEO:
        return BC_MSUBTYPE_MPEG2VIDEO;
    case AV_CODEC_ID_VC1:
        return BC_MSUBTYPE_VC1;
    case AV_CODEC_ID_WMV3:
        return BC_MSUBTYPE_WMV3;
    case AV_CODEC_ID_H264:
        return priv->is_nal ? BC_MSUBTYPE_AVC1 : BC_MSUBTYPE_H264;
    default:
        return BC_MSUBTYPE_INVALID;
    }
}

static inline void print_frame_info(CHDContext *priv, BC_DTS_PROC_OUT *output)
{
    av_log(priv->avctx, AV_LOG_TRACE, "\tYBuffSz: %u\n", output->YbuffSz);
    av_log(priv->avctx, AV_LOG_TRACE, "\tYBuffDoneSz: %u\n",
           output->YBuffDoneSz);
    av_log(priv->avctx, AV_LOG_TRACE, "\tUVBuffDoneSz: %u\n",
           output->UVBuffDoneSz);
    av_log(priv->avctx, AV_LOG_TRACE, "\tTimestamp: %"PRIu64"\n",
           output->PicInfo.timeStamp);
    av_log(priv->avctx, AV_LOG_TRACE, "\tPicture Number: %u\n",
           output->PicInfo.picture_number);
    av_log(priv->avctx, AV_LOG_TRACE, "\tWidth: %u\n",
           output->PicInfo.width);
    av_log(priv->avctx, AV_LOG_TRACE, "\tHeight: %u\n",
           output->PicInfo.height);
    av_log(priv->avctx, AV_LOG_TRACE, "\tChroma: 0x%03x\n",
           output->PicInfo.chroma_format);
    av_log(priv->avctx, AV_LOG_TRACE, "\tPulldown: %u\n",
           output->PicInfo.pulldown);
    av_log(priv->avctx, AV_LOG_TRACE, "\tFlags: 0x%08x\n",
           output->PicInfo.flags);
    av_log(priv->avctx, AV_LOG_TRACE, "\tFrame Rate/Res: %u\n",
           output->PicInfo.frame_rate);
    av_log(priv->avctx, AV_LOG_TRACE, "\tAspect Ratio: %u\n",
           output->PicInfo.aspect_ratio);
    av_log(priv->avctx, AV_LOG_TRACE, "\tColor Primaries: %u\n",
           output->PicInfo.colour_primaries);
    av_log(priv->avctx, AV_LOG_TRACE, "\tMetaData: %u\n",
           output->PicInfo.picture_meta_payload);
    av_log(priv->avctx, AV_LOG_TRACE, "\tSession Number: %u\n",
           output->PicInfo.sess_num);
    av_log(priv->avctx, AV_LOG_TRACE, "\tycom: %u\n",
           output->PicInfo.ycom);
    av_log(priv->avctx, AV_LOG_TRACE, "\tCustom Aspect: %u\n",
           output->PicInfo.custom_aspect_ratio_width_height);
    av_log(priv->avctx, AV_LOG_TRACE, "\tFrames to Drop: %u\n",
           output->PicInfo.n_drop);
    av_log(priv->avctx, AV_LOG_TRACE, "\tH264 Valid Fields: 0x%08x\n",
           output->PicInfo.other.h264.valid);
}


/*****************************************************************************
 * OpaqueList functions
 ****************************************************************************/

static uint64_t opaque_list_push(CHDContext *priv, uint64_t reordered_opaque)
{
    OpaqueList *newNode = av_mallocz(sizeof (OpaqueList));
    if (!newNode) {
        av_log(priv->avctx, AV_LOG_ERROR,
               "Unable to allocate new node in OpaqueList.\n");
        return 0;
    }
    if (!priv->head) {
        newNode->fake_timestamp = TIMESTAMP_UNIT;
        priv->head              = newNode;
    } else {
        newNode->fake_timestamp = priv->tail->fake_timestamp + TIMESTAMP_UNIT;
        priv->tail->next        = newNode;
    }
    priv->tail = newNode;
    newNode->reordered_opaque = reordered_opaque;

    return newNode->fake_timestamp;
}

/*
 * The OpaqueList is built in decode order, while elements will be removed
 * in presentation order. If frames are reordered, this means we must be
 * able to remove elements that are not the first element.
 *
 * Returned node must be freed by caller.
 */
static OpaqueList *opaque_list_pop(CHDContext *priv, uint64_t fake_timestamp)
{
    OpaqueList *node = priv->head;

    if (!priv->head) {
        av_log(priv->avctx, AV_LOG_ERROR,
               "CrystalHD: Attempted to query non-existent timestamps.\n");
        return NULL;
    }

    /*
     * The first element is special-cased because we have to manipulate
     * the head pointer rather than the previous element in the list.
     */
    if (priv->head->fake_timestamp == fake_timestamp) {
        priv->head = node->next;

        if (!priv->head->next)
            priv->tail = priv->head;

        node->next = NULL;
        return node;
    }

    /*
     * The list is processed at arm's length so that we have the
     * previous element available to rewrite its next pointer.
     */
    while (node->next) {
        OpaqueList *current = node->next;
        if (current->fake_timestamp == fake_timestamp) {
            node->next = current->next;

            if (!node->next)
               priv->tail = node;

            current->next = NULL;
            return current;
        } else {
            node = current;
        }
    }

    av_log(priv->avctx, AV_LOG_VERBOSE,
           "CrystalHD: Couldn't match fake_timestamp.\n");
    return NULL;
}


/*****************************************************************************
 * Video decoder API function definitions
 ****************************************************************************/

static void flush(AVCodecContext *avctx)
{
    CHDContext *priv = avctx->priv_data;

    priv->need_second_field = 0;
    priv->draining          = 0;

    /* Flush mode 4 flushes all software and hardware buffers. */
    DtsFlushInput(priv->dev, 4);
}


static av_cold int uninit(AVCodecContext *avctx)
{
    CHDContext *priv = avctx->priv_data;
    HANDLE device;

    device = priv->dev;
    DtsStopDecoder(device);
    DtsCloseDecoder(device);
    DtsDeviceClose(device);

    /*
     * Restore original extradata, so that if the decoder is
     * reinitialised, the bitstream detection and filtering
     * will work as expected.
     */
    if (priv->orig_extradata) {
        av_free(avctx->extradata);
        avctx->extradata = priv->orig_extradata;
        avctx->extradata_size = priv->orig_extradata_size;
        priv->orig_extradata = NULL;
        priv->orig_extradata_size = 0;
    }

    if (priv->bsfc) {
        av_bsf_free(&priv->bsfc);
    }

    av_freep(&priv->sps_pps_buf);

    if (priv->head) {
       OpaqueList *node = priv->head;
       while (node) {
          OpaqueList *next = node->next;
          av_free(node);
          node = next;
       }
    }

    return 0;
}


static av_cold int init_bsf(AVCodecContext *avctx, const char *bsf_name)
{
    CHDContext *priv = avctx->priv_data;
    const AVBitStreamFilter *bsf;
    int avret;
    void *extradata = NULL;
    size_t size = 0;

    bsf = av_bsf_get_by_name(bsf_name);
    if (!bsf) {
        av_log(avctx, AV_LOG_ERROR,
               "Cannot open the %s BSF!\n", bsf_name);
        return AVERROR_BSF_NOT_FOUND;
    }

    avret = av_bsf_alloc(bsf, &priv->bsfc);
    if (avret != 0) {
        return avret;
    }

    avret = avcodec_parameters_from_context(priv->bsfc->par_in, avctx);
    if (avret != 0) {
        return avret;
    }

    avret = av_bsf_init(priv->bsfc);
    if (avret != 0) {
        return avret;
    }

    /* Back up the extradata so it can be restored at close time. */
    priv->orig_extradata = avctx->extradata;
    priv->orig_extradata_size = avctx->extradata_size;

    size = priv->bsfc->par_out->extradata_size;
    extradata = av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!extradata) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to allocate copy of extradata\n");
        return AVERROR(ENOMEM);
    }
    memcpy(extradata, priv->bsfc->par_out->extradata, size);

    avctx->extradata = extradata;
    avctx->extradata_size = size;

    return 0;
}

static av_cold int init(AVCodecContext *avctx)
{
    CHDContext* priv;
    int avret;
    BC_STATUS ret;
    BC_INFO_CRYSTAL version;
    BC_INPUT_FORMAT format = {
        .FGTEnable   = FALSE,
        .Progressive = TRUE,
        .OptFlags    = 0x80000000 | vdecFrameRate59_94 | 0x40,
        .width       = avctx->width,
        .height      = avctx->height,
    };

    BC_MEDIA_SUBTYPE subtype;

    uint32_t mode = DTS_PLAYBACK_MODE |
                    DTS_LOAD_FILE_PLAY_FW |
                    DTS_SKIP_TX_CHK_CPB |
                    DTS_PLAYBACK_DROP_RPT_MODE |
                    DTS_SINGLE_THREADED_MODE |
                    DTS_DFLT_RESOLUTION(vdecRESOLUTION_1080p23_976);

    av_log(avctx, AV_LOG_VERBOSE, "CrystalHD Init for %s\n",
           avctx->codec->name);

    avctx->pix_fmt = AV_PIX_FMT_YUYV422;

    /* Initialize the library */
    priv               = avctx->priv_data;
    priv->avctx        = avctx;
    priv->is_nal       = avctx->extradata_size > 0 && *(avctx->extradata) == 1;
    priv->draining     = 0;

    subtype = id2subtype(priv, avctx->codec->id);
    switch (subtype) {
    case BC_MSUBTYPE_AVC1:
        avret = init_bsf(avctx, "h264_mp4toannexb");
        if (avret != 0) {
            return avret;
        }
        subtype = BC_MSUBTYPE_H264;
        format.startCodeSz = 4;
        format.pMetaData  = avctx->extradata;
        format.metaDataSz = avctx->extradata_size;
        break;
    case BC_MSUBTYPE_H264:
        format.startCodeSz = 4;
        // Fall-through
    case BC_MSUBTYPE_VC1:
    case BC_MSUBTYPE_WVC1:
    case BC_MSUBTYPE_WMV3:
    case BC_MSUBTYPE_WMVA:
    case BC_MSUBTYPE_MPEG2VIDEO:
    case BC_MSUBTYPE_DIVX:
    case BC_MSUBTYPE_DIVX311:
        format.pMetaData  = avctx->extradata;
        format.metaDataSz = avctx->extradata_size;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "CrystalHD: Unknown codec name\n");
        return AVERROR(EINVAL);
    }
    format.mSubtype = subtype;

    if (priv->sWidth) {
        format.bEnableScaling = 1;
        format.ScalingParams.sWidth = priv->sWidth;
    }

    /* Get a decoder instance */
    av_log(avctx, AV_LOG_VERBOSE, "CrystalHD: starting up\n");
    // Initialize the Link and Decoder devices
    ret = DtsDeviceOpen(&priv->dev, mode);
    if (ret != BC_STS_SUCCESS) {
        av_log(avctx, AV_LOG_VERBOSE, "CrystalHD: DtsDeviceOpen failed\n");
        goto fail;
    }

    ret = DtsCrystalHDVersion(priv->dev, &version);
    if (ret != BC_STS_SUCCESS) {
        av_log(avctx, AV_LOG_VERBOSE,
               "CrystalHD: DtsCrystalHDVersion failed\n");
        goto fail;
    }
    priv->is_70012 = version.device == 0;

    if (priv->is_70012 &&
        (subtype == BC_MSUBTYPE_DIVX || subtype == BC_MSUBTYPE_DIVX311)) {
        av_log(avctx, AV_LOG_VERBOSE,
               "CrystalHD: BCM70012 doesn't support MPEG4-ASP/DivX/Xvid\n");
        goto fail;
    }

    ret = DtsSetInputFormat(priv->dev, &format);
    if (ret != BC_STS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "CrystalHD: SetInputFormat failed\n");
        goto fail;
    }

    ret = DtsOpenDecoder(priv->dev, BC_STREAM_TYPE_ES);
    if (ret != BC_STS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "CrystalHD: DtsOpenDecoder failed\n");
        goto fail;
    }

    ret = DtsSetColorSpace(priv->dev, OUTPUT_MODE422_YUY2);
    if (ret != BC_STS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "CrystalHD: DtsSetColorSpace failed\n");
        goto fail;
    }
    ret = DtsStartDecoder(priv->dev);
    if (ret != BC_STS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "CrystalHD: DtsStartDecoder failed\n");
        goto fail;
    }
    ret = DtsStartCapture(priv->dev);
    if (ret != BC_STS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "CrystalHD: DtsStartCapture failed\n");
        goto fail;
    }

    av_log(avctx, AV_LOG_VERBOSE, "CrystalHD: Init complete.\n");

    return 0;

 fail:
    uninit(avctx);
    return -1;
}


static inline CopyRet copy_frame(AVCodecContext *avctx,
                                 BC_DTS_PROC_OUT *output,
                                 AVFrame *frame, int *got_frame)
{
    BC_STATUS ret;
    BC_DTS_STATUS decoder_status = { 0, };
    uint8_t interlaced;

    CHDContext *priv = avctx->priv_data;
    int64_t pkt_pts  = AV_NOPTS_VALUE;

    uint8_t bottom_field = (output->PicInfo.flags & VDEC_FLAG_BOTTOMFIELD) ==
                           VDEC_FLAG_BOTTOMFIELD;
    uint8_t bottom_first = !!(output->PicInfo.flags & VDEC_FLAG_BOTTOM_FIRST);

    int width    = output->PicInfo.width;
    int height   = output->PicInfo.height;
    int bwidth;
    uint8_t *src = output->Ybuff;
    int sStride;
    uint8_t *dst;
    int dStride;

    if (output->PicInfo.timeStamp != 0) {
        OpaqueList *node = opaque_list_pop(priv, output->PicInfo.timeStamp);
        if (node) {
            pkt_pts = node->reordered_opaque;
            av_free(node);
        } else {
            /*
             * We will encounter a situation where a timestamp cannot be
             * popped if a second field is being returned. In this case,
             * each field has the same timestamp and the first one will
             * cause it to be popped. We'll avoid overwriting the valid
             * timestamp below.
             */
        }
        av_log(avctx, AV_LOG_VERBOSE, "output \"pts\": %"PRIu64"\n",
               output->PicInfo.timeStamp);
    }

    ret = DtsGetDriverStatus(priv->dev, &decoder_status);
    if (ret != BC_STS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "CrystalHD: GetDriverStatus failed: %u\n", ret);
       return RET_ERROR;
    }

    interlaced = output->PicInfo.flags & VDEC_FLAG_INTERLACED_SRC;

    av_log(avctx, AV_LOG_VERBOSE, "Interlaced state: %d\n",
           interlaced);

    priv->need_second_field = interlaced && !priv->need_second_field;

    if (!frame->data[0]) {
        if (ff_get_buffer(avctx, frame, 0) < 0)
            return RET_ERROR;
    }

    bwidth = av_image_get_linesize(avctx->pix_fmt, width, 0);
    if (bwidth < 0)
       return RET_ERROR;

    if (priv->is_70012) {
        int pStride;

        if (width <= 720)
            pStride = 720;
        else if (width <= 1280)
            pStride = 1280;
        else pStride = 1920;
        sStride = av_image_get_linesize(avctx->pix_fmt, pStride, 0);
        if (sStride < 0)
            return RET_ERROR;
    } else {
        sStride = bwidth;
    }

    dStride = frame->linesize[0];
    dst     = frame->data[0];

    av_log(priv->avctx, AV_LOG_VERBOSE, "CrystalHD: Copying out frame\n");

    /*
     * The hardware doesn't return the first sample of a picture.
     * Ignoring why it behaves this way, it's better to copy the sample from
     * the second line, rather than the next sample across because the chroma
     * values should be correct (assuming the decoded video was 4:2:0, which
     * it was).
     */
    *((uint32_t *)src) = *((uint32_t *)(src + sStride));

    if (interlaced) {
        int dY = 0;
        int sY = 0;

        height /= 2;
        if (bottom_field) {
            av_log(priv->avctx, AV_LOG_VERBOSE, "Interlaced: bottom field\n");
            dY = 1;
        } else {
            av_log(priv->avctx, AV_LOG_VERBOSE, "Interlaced: top field\n");
            dY = 0;
        }

        for (sY = 0; sY < height; dY++, sY++) {
            memcpy(&(dst[dY * dStride]), &(src[sY * sStride]), bwidth);
            dY++;
        }
    } else {
        av_image_copy_plane(dst, dStride, src, sStride, bwidth, height);
    }

    frame->interlaced_frame = interlaced;
    if (interlaced)
        frame->top_field_first = !bottom_first;

    if (pkt_pts != AV_NOPTS_VALUE) {
        frame->pts = pkt_pts;
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_pts = pkt_pts;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }
    av_frame_set_pkt_pos(frame, -1);
    av_frame_set_pkt_duration(frame, 0);
    av_frame_set_pkt_size(frame, -1);

    if (!priv->need_second_field) {
        *got_frame       = 1;
    } else {
        return RET_COPY_AGAIN;
    }

    return RET_OK;
}


static inline CopyRet receive_frame(AVCodecContext *avctx,
                                    AVFrame *frame, int *got_frame)
{
    BC_STATUS ret;
    BC_DTS_PROC_OUT output = {
        .PicInfo.width  = avctx->width,
        .PicInfo.height = avctx->height,
    };
    CHDContext *priv = avctx->priv_data;
    HANDLE dev       = priv->dev;

    *got_frame = 0;

    // Request decoded data from the driver
    ret = DtsProcOutputNoCopy(dev, OUTPUT_PROC_TIMEOUT, &output);
    if (ret == BC_STS_FMT_CHANGE) {
        av_log(avctx, AV_LOG_VERBOSE, "CrystalHD: Initial format change\n");
        avctx->width  = output.PicInfo.width;
        avctx->height = output.PicInfo.height;
        switch ( output.PicInfo.aspect_ratio ) {
        case vdecAspectRatioSquare:
            avctx->sample_aspect_ratio = (AVRational) {  1,  1};
            break;
        case vdecAspectRatio12_11:
            avctx->sample_aspect_ratio = (AVRational) { 12, 11};
            break;
        case vdecAspectRatio10_11:
            avctx->sample_aspect_ratio = (AVRational) { 10, 11};
            break;
        case vdecAspectRatio16_11:
            avctx->sample_aspect_ratio = (AVRational) { 16, 11};
            break;
        case vdecAspectRatio40_33:
            avctx->sample_aspect_ratio = (AVRational) { 40, 33};
            break;
        case vdecAspectRatio24_11:
            avctx->sample_aspect_ratio = (AVRational) { 24, 11};
            break;
        case vdecAspectRatio20_11:
            avctx->sample_aspect_ratio = (AVRational) { 20, 11};
            break;
        case vdecAspectRatio32_11:
            avctx->sample_aspect_ratio = (AVRational) { 32, 11};
            break;
        case vdecAspectRatio80_33:
            avctx->sample_aspect_ratio = (AVRational) { 80, 33};
            break;
        case vdecAspectRatio18_11:
            avctx->sample_aspect_ratio = (AVRational) { 18, 11};
            break;
        case vdecAspectRatio15_11:
            avctx->sample_aspect_ratio = (AVRational) { 15, 11};
            break;
        case vdecAspectRatio64_33:
            avctx->sample_aspect_ratio = (AVRational) { 64, 33};
            break;
        case vdecAspectRatio160_99:
            avctx->sample_aspect_ratio = (AVRational) {160, 99};
            break;
        case vdecAspectRatio4_3:
            avctx->sample_aspect_ratio = (AVRational) {  4,  3};
            break;
        case vdecAspectRatio16_9:
            avctx->sample_aspect_ratio = (AVRational) { 16,  9};
            break;
        case vdecAspectRatio221_1:
            avctx->sample_aspect_ratio = (AVRational) {221,  1};
            break;
        }
        return RET_COPY_AGAIN;
    } else if (ret == BC_STS_SUCCESS) {
        int copy_ret = -1;
        if (output.PoutFlags & BC_POUT_FLAGS_PIB_VALID) {
            if (avctx->codec->id == AV_CODEC_ID_MPEG4 &&
                output.PicInfo.timeStamp == 0 && priv->bframe_bug) {
                if (!priv->bframe_bug) {
                    av_log(avctx, AV_LOG_VERBOSE,
                           "CrystalHD: Not returning packed frame twice.\n");
                }
                DtsReleaseOutputBuffs(dev, NULL, FALSE);
                return RET_COPY_AGAIN;
            }

            print_frame_info(priv, &output);

            copy_ret = copy_frame(avctx, &output, frame, got_frame);
        } else {
            /*
             * An invalid frame has been consumed.
             */
            av_log(avctx, AV_LOG_ERROR, "CrystalHD: ProcOutput succeeded with "
                                        "invalid PIB\n");
            copy_ret = RET_COPY_AGAIN;
        }
        DtsReleaseOutputBuffs(dev, NULL, FALSE);

        return copy_ret;
    } else if (ret == BC_STS_BUSY) {
        return RET_COPY_AGAIN;
    } else {
        av_log(avctx, AV_LOG_ERROR, "CrystalHD: ProcOutput failed %d\n", ret);
        return RET_ERROR;
    }
}

static int crystalhd_decode_packet(AVCodecContext *avctx, const AVPacket *avpkt)
{
    BC_STATUS bc_ret;
    CHDContext *priv   = avctx->priv_data;
    HANDLE dev         = priv->dev;
    AVPacket filtered_packet = { 0 };
    int ret = 0;

    av_log(avctx, AV_LOG_VERBOSE, "CrystalHD: decode_packet\n");

    if (avpkt && avpkt->size) {
        int32_t tx_free = (int32_t)DtsTxFreeSize(dev);

        if (!priv->bframe_bug && (avpkt->size == 6 || avpkt->size == 7)) {
            /*
             * Drop frames trigger the bug
             */
            av_log(avctx, AV_LOG_WARNING,
                   "CrystalHD: Enabling work-around for packed b-frame bug\n");
            priv->bframe_bug = 1;
        } else if (priv->bframe_bug && avpkt->size == 8) {
            /*
             * Delay frames don't trigger the bug
             */
            av_log(avctx, AV_LOG_WARNING,
                   "CrystalHD: Disabling work-around for packed b-frame bug\n");
            priv->bframe_bug = 0;
        }

        if (priv->bsfc) {
            AVPacket filter_packet = { 0 };

            ret = av_packet_ref(&filter_packet, avpkt);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "CrystalHD: mpv4toannexb filter "
                       "failed to ref input packet\n");
                goto exit;
            }

            ret = av_bsf_send_packet(priv->bsfc, &filter_packet);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "CrystalHD: mpv4toannexb filter "
                       "failed to send input packet\n");
                goto exit;
            }

            ret = av_bsf_receive_packet(priv->bsfc, &filtered_packet);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "CrystalHD: mpv4toannexb filter "
                       "failed to receive output packet\n");
                goto exit;
            }

            avpkt = &filtered_packet;
            av_packet_unref(&filter_packet);
        }

        if (avpkt->size < tx_free) {
            /*
             * Despite being notionally opaque, either libcrystalhd or
             * the hardware itself will mangle pts values that are too
             * small or too large. The docs claim it should be in units
             * of 100ns. Given that we're nominally dealing with a black
             * box on both sides, any transform we do has no guarantee of
             * avoiding mangling so we need to build a mapping to values
             * we know will not be mangled.
             */
            uint64_t pts = opaque_list_push(priv, avpkt->pts);
            if (!pts) {
                ret = AVERROR(ENOMEM);
                goto exit;
            }
            av_log(priv->avctx, AV_LOG_VERBOSE,
                   "input \"pts\": %"PRIu64"\n", pts);
            bc_ret = DtsProcInput(dev, avpkt->data, avpkt->size, pts, 0);
            if (bc_ret == BC_STS_BUSY) {
                av_log(avctx, AV_LOG_WARNING,
                       "CrystalHD: ProcInput returned busy\n");
                ret = AVERROR(EAGAIN);
                goto exit;
            } else if (bc_ret != BC_STS_SUCCESS) {
                av_log(avctx, AV_LOG_ERROR,
                       "CrystalHD: ProcInput failed: %u\n", ret);
                ret = -1;
                goto exit;
            }
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "CrystalHD: Input buffer full\n");
            ret = AVERROR(EAGAIN);
            goto exit;
        }
    } else {
        av_log(avctx, AV_LOG_INFO, "CrystalHD: No more input data\n");
        priv->draining = 1;
        ret = AVERROR_EOF;
        goto exit;
    }
 exit:
    av_packet_unref(&filtered_packet);
    return ret;
}

static int crystalhd_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    BC_STATUS bc_ret;
    BC_DTS_STATUS decoder_status = { 0, };
    CopyRet rec_ret;
    CHDContext *priv   = avctx->priv_data;
    HANDLE dev         = priv->dev;
    int got_frame = 0;

    av_log(avctx, AV_LOG_VERBOSE, "CrystalHD: receive_frame\n");

    do {
        bc_ret = DtsGetDriverStatus(dev, &decoder_status);
        if (bc_ret != BC_STS_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "CrystalHD: GetDriverStatus failed\n");
            return -1;
        }

        if (decoder_status.ReadyListCount == 0) {
            av_log(avctx, AV_LOG_INFO, "CrystalHD: Insufficient frames ready. Returning\n");
            got_frame = 0;
            rec_ret = RET_OK;
            break;
        }

        rec_ret = receive_frame(avctx, frame, &got_frame);
    } while (rec_ret == RET_COPY_AGAIN);

    if (rec_ret == RET_ERROR) {
        return -1;
    } else if (got_frame == 0) {
        return priv->draining ? AVERROR_EOF : AVERROR(EAGAIN);
    } else {
        return 0;
    }
}

#define DEFINE_CRYSTALHD_DECODER(x, X) \
    static const AVClass x##_crystalhd_class = { \
        .class_name = #x "_crystalhd", \
        .item_name = av_default_item_name, \
        .option = options, \
        .version = LIBAVUTIL_VERSION_INT, \
    }; \
    AVCodec ff_##x##_crystalhd_decoder = { \
        .name           = #x "_crystalhd", \
        .long_name      = NULL_IF_CONFIG_SMALL("CrystalHD " #X " decoder"), \
        .type           = AVMEDIA_TYPE_VIDEO, \
        .id             = AV_CODEC_ID_##X, \
        .priv_data_size = sizeof(CHDContext), \
        .priv_class     = &x##_crystalhd_class, \
        .init           = init, \
        .close          = uninit, \
        .send_packet    = crystalhd_decode_packet, \
        .receive_frame  = crystalhd_receive_frame, \
        .flush          = flush, \
        .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING, \
        .pix_fmts       = (const enum AVPixelFormat[]){AV_PIX_FMT_YUYV422, AV_PIX_FMT_NONE}, \
    };

#if CONFIG_H264_CRYSTALHD_DECODER
DEFINE_CRYSTALHD_DECODER(h264, H264)
#endif

#if CONFIG_MPEG2_CRYSTALHD_DECODER
DEFINE_CRYSTALHD_DECODER(mpeg2, MPEG2VIDEO)
#endif

#if CONFIG_MPEG4_CRYSTALHD_DECODER
DEFINE_CRYSTALHD_DECODER(mpeg4, MPEG4)
#endif

#if CONFIG_MSMPEG4_CRYSTALHD_DECODER
DEFINE_CRYSTALHD_DECODER(msmpeg4, MSMPEG4V3)
#endif

#if CONFIG_VC1_CRYSTALHD_DECODER
DEFINE_CRYSTALHD_DECODER(vc1, VC1)
#endif

#if CONFIG_WMV3_CRYSTALHD_DECODER
DEFINE_CRYSTALHD_DECODER(wmv3, WMV3)
#endif
