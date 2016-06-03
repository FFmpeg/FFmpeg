/*
 * MMAL Video Decoder
 * Copyright (c) 2016 Lionel Chazallon
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

/**
 * @file
 * AMLogic Video Decoder
 */

#include "avcodec.h"
#include "internal.h"
#include "amltools.h"
#include "amlqueue.h"

#include "libavutil/atomic.h"
#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"
#include <unistd.h>
#include <amcodec/codec.h>

#define TRICKMODE_NONE (0)
#define EXTERNAL_PTS   (1)
#define SYNC_OUTSIDE   (2)

#define PTS_FREQ       90000
#define PTS_FREQ_MS    PTS_FREQ / 1000
#define AV_SYNC_THRESH PTS_FREQ * 30

#define MIN_FRAME_QUEUE_SIZE  16

typedef struct {
  AVClass *av_class;
  codec_para_t codec;
  int first_packet;
  AVBSFContext *bsf;
  PacketQueue writequeue;
  PacketQueue framequeue;
  struct buf_status buffer_status;
  struct vdec_status decoder_status;
} AMLDecodeContext;

// Functions prototypes
int ffmal_init_bitstream(AVCodecContext *avctx);
int ffaml_write_pkt_data(AVCodecContext *avctx, AVPacket *avpkt);
void ffaml_checkin_packet_pts(AVCodecContext *avctx, AVPacket *avpkt);

int ffmal_init_bitstream(AVCodecContext *avctx)
{
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;
  int ret = 0;

  if (!aml_context->bsf)
  {

    const AVBitStreamFilter *bsf = av_bsf_get_by_name("h264_mp4toannexb");
    if(!bsf)
        return AVERROR_BSF_NOT_FOUND;

    if ((ret = av_bsf_alloc(bsf, &aml_context->bsf)))
        return ret;

    if (((ret = avcodec_parameters_from_context(aml_context->bsf->par_in, avctx)) < 0) ||
        ((ret = av_bsf_init(aml_context->bsf)) < 0))
    {
        av_bsf_free(&aml_context->bsf);
        return ret;
    }
  }

  return 0;
}

int ffaml_write_pkt_data(AVCodecContext *avctx, AVPacket *avpkt)
{
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;
  codec_para_t *pcodec  = &aml_context->codec;
  int bytesleft = avpkt->size;
  int written = 0;
  int ret = 0;
  struct buf_status vdec_stat;

  // grab the decoder buff status, if we on't have enough buffer space
  // we will sleep a bit, that way write never hangs up
  do
  {
    ret = codec_get_vbuf_state(pcodec, &vdec_stat);
    if (ret < 0)
    {
      av_log(avctx, AV_LOG_ERROR, "failed to query video decoder buffer state(code = %d)\n", ret);
      return -1;
    }

    if (vdec_stat.free_len < bytesleft)
    {
      // video decoder buffer stuffed, lets sleeps and retry
      usleep(1000);
    }
  } while (vdec_stat.free_len < bytesleft);

  // check in the pts
  ffaml_checkin_packet_pts(avctx, avpkt);

  // actually write the packet data
  while (bytesleft)
  {
    written = codec_write(pcodec, avpkt->data + written, bytesleft);

    if (written < 0)
    {
      av_log(avctx, AV_LOG_ERROR, "failed to write data to codec (code = %d)\n", written);
      usleep(10);
    }
    else
    {
      bytesleft -= written;

      if (bytesleft)
        usleep(1000);
    }
  }


  return 0;
}

void ffaml_checkin_packet_pts(AVCodecContext *avctx, AVPacket *avpkt)
{
  int ret;
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;

  float pts = (avpkt->pts * av_q2d(avctx->time_base) * PTS_FREQ);
  if ((ret = codec_checkin_pts(&aml_context->codec, pts)) < 0)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to checkin the pts (code = %d)\n", ret);
  }
}

static av_cold int ffaml_init_decoder(AVCodecContext *avctx)
{
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;
  codec_para_t *pcodec  = &aml_context->codec;
  int ret = 0;

  // reset the first packet attribute
  aml_context->first_packet = 1;
  aml_context->bsf = NULL;
  ffaml_init_queue(&aml_context->writequeue);
  ffaml_init_queue(&aml_context->framequeue);

  // setup the codec structure for amcodec
  memset(pcodec, 0, sizeof(codec_para_t));
  memset(&aml_context->buffer_status, 0, sizeof(aml_context->buffer_status));
  memset(&aml_context->decoder_status, 0, sizeof(aml_context->decoder_status));

  pcodec->stream_type = STREAM_TYPE_ES_VIDEO;
  pcodec->has_video = 1;
  pcodec->video_type = VFORMAT_H264;
  pcodec->am_sysinfo.format = VIDEO_DEC_FORMAT_H264;
  pcodec->am_sysinfo.param = (void*)(EXTERNAL_PTS | SYNC_OUTSIDE);

  ret = codec_init(pcodec);
  if (ret != CODEC_ERROR_NONE)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to init amcodec decoder\n");
    return -1;
  }

  codec_set_cntl_avthresh(pcodec, AV_SYNC_THRESH);
  codec_set_cntl_mode(pcodec, TRICKMODE_NONE);
  codec_set_cntl_syncthresh(pcodec, 0);

  ret = ffmal_init_bitstream(avctx);
  if (ret != 0)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to init AML bitstream\n");
    return -1;
  }

  av_log(avctx, AV_LOG_DEBUG, "amcodec intialized successfully\n");
  return 0;
}

static av_cold int ffaml_close_decoder(AVCodecContext *avctx)
{
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;
  codec_para_t *pcodec  = &aml_context->codec;
  if (pcodec)
  {
    codec_close(pcodec);
  }

  // free bitstream
  if (aml_context->bsf)
    av_bsf_free(&aml_context->bsf);

  av_log(avctx, AV_LOG_DEBUG, "amcodec closed successfully\n");
  return 0;
}


static int ffaml_decode(AVCodecContext *avctx, void *data, int *got_frame,
                         AVPacket *avpkt)
{
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;
  codec_para_t *pcodec  = &aml_context->codec;
  int ret = 0;
  AVPacket filter_pkt = {0};
  AVPacket filtered_packet = {0};
  AVPacket *pkt;
  AVFrame *frame = data;
  uint8_t *extradata;
  int extradata_size;

  // if we use a bitstream filter, then use it
  if (aml_context->bsf)
  {
    if ((ret = av_packet_ref(&filter_pkt, avpkt)) < 0)
        return ret;

    if ((ret = av_bsf_send_packet(aml_context->bsf, &filter_pkt)) < 0) {
        av_packet_unref(&filter_pkt);
        return ret;
    }

    if ((ret = av_bsf_receive_packet(aml_context->bsf, &filtered_packet)) < 0)
        return ret;

    avpkt = &filtered_packet;
    extradata = aml_context->bsf->par_out->extradata;
    extradata_size = aml_context->bsf->par_out->extradata_size;
  }
  else
  {
    extradata = avctx->extradata;
    extradata_size = avctx->extradata_size;
  }

  // we need to write the header on first packet
  if ((aml_context->first_packet) && (extradata_size))
  {
    ret = codec_write(pcodec, extradata, extradata_size);
    aml_context->first_packet = 0;
    codec_resume(pcodec);
  }

  // queue the packet
  if (ffaml_queue_packet(avctx, &aml_context->writequeue, avpkt) < 0)
  {
    av_log(avctx, AV_LOG_DEBUG, "failed to queue AvPacket\n");
    return -1;
  }

  // now we fill up decoder buffer
  if (aml_context->writequeue.tail)
  {
    avpkt = aml_context->writequeue.tail->pkt;

    // grab video decoder info to check if we have enough input buffer space
    ret = codec_get_vbuf_state(pcodec, &aml_context->buffer_status);
    if (ret < 0)
    {
      av_log(avctx, AV_LOG_ERROR, "failed to query video decoder buffer state(code = %d)\n", ret);
      return -1;
    }

    // if we have enough space to push the packet, let's do it
    if (avpkt->size < aml_context->buffer_status.free_len)
    {
      pkt = ffaml_dequeue_packet(avctx, &aml_context->writequeue);

      if (ffaml_write_pkt_data(avctx, pkt) < 0)
      {
        av_log(avctx, AV_LOG_ERROR, "failed to write packet.\n");
        return -1;
      }

     // now queue the packet to the frame queue
     ffaml_queue_packet(avctx, &aml_context->framequeue, pkt);

    }
  }

  // now we want to send the dummy frame if we pushed a packet

  // grab the video decoder status
  ret = codec_get_vdec_state(pcodec, &aml_context->decoder_status);
  if (ret < 0)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to retrieve video decoder status (code=%d)\n", ret);
    return -1;
  }

  // now we need to make a dummy avframe as we will not get any data out of the codec
  // we wait to have 16 frames because with some codecs, pts are in the wrong order
  if ((aml_context->decoder_status.width > 0) && (aml_context->framequeue.size >= MIN_FRAME_QUEUE_SIZE))
  {

    pkt = ffaml_queue_peek_pts_packet(avctx, &aml_context->framequeue);
    if (pkt)
    {
      frame->width = aml_context->decoder_status.width;
      frame->height = aml_context->decoder_status.height;
      frame->format = AV_PIX_FMT_AML;

      ff_set_dimensions(avctx, frame->width, frame->height);
      avctx->pix_fmt = frame->format;

      // we fake a ref on buf[0] and also pass the codec struct pointer
      // in the first image plane, so that payer can use it.
      frame->buf[0] = av_buffer_create(NULL, 0, NULL, NULL, AV_BUFFER_FLAG_READONLY);
      frame->data[0] = (uint8_t*)pcodec;
      frame->pkt_pts = pkt->pts;

      av_packet_free(&pkt);
      *got_frame = 1;
    }

  }

  return 0;
}

static void ffaml_flush(AVCodecContext *avctx)
{
}


#define FFAML_DEC_HWACCEL(NAME, ID) \
  AVHWAccel ff_##NAME##_aml_hwaccel = { \
      .name       = #NAME "_aml", \
      .type       = AVMEDIA_TYPE_VIDEO,\
      .id         = ID, \
      .pix_fmt    = AV_PIX_FMT_AML,\
  };

#define FFAML_DEC_CLASS(NAME) \
    static const AVClass ffaml_##NAME##_dec_class = { \
        .class_name = "aml_" #NAME "_dec", \
        .version    = LIBAVUTIL_VERSION_INT, \
    };

#define FFAML_DEC(NAME, ID) \
    FFAML_DEC_CLASS(NAME) \
    FFAML_DEC_HWACCEL(NAME, ID) \
    AVCodec ff_##NAME##_aml_decoder = { \
        .name           = #NAME "_aml", \
        .long_name      = NULL_IF_CONFIG_SMALL(#NAME " (aml)"), \
        .type           = AVMEDIA_TYPE_VIDEO, \
        .id             = ID, \
        .priv_data_size = sizeof(AMLDecodeContext), \
        .init           = ffaml_init_decoder, \
        .close          = ffaml_close_decoder, \
        .decode         = ffaml_decode, \
        .flush          = ffaml_flush, \
        .priv_class     = &ffaml_##NAME##_dec_class, \
        .capabilities   = AV_CODEC_CAP_DELAY, \
        .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS, \
        .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_AML, \
                                                         AV_PIX_FMT_NONE}, \
    };

FFAML_DEC(h264, AV_CODEC_ID_H264)
