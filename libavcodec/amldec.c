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


#include "libavutil/atomic.h"
#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"
#include <unistd.h>
#include "amldec.h"

void ffaml_log_decoder_info(AVCodecContext *avctx)
{
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;
  double video_pts = (double)amlsysfs_read_int(avctx, "/sys/class/tsync/pts_video", 16) / (double)PTS_FREQ;
  double pcrscr_pts = (double)amlsysfs_read_int(avctx, "/sys/class/tsync/pts_pcrscr", 16) / (double)PTS_FREQ;

  av_log(avctx, AV_LOG_DEBUG, "Decoder buffer : filled %d bytes (%f%%)\n",
        aml_context->buffer_status.data_len, 
	(double)(aml_context->buffer_status.data_len * 100) / (double)(aml_context->buffer_status.data_len + aml_context->buffer_status.free_len));
  av_log(avctx, AV_LOG_DEBUG, "Decoder queues : %d packets, %d frames\n", aml_context->writequeue.size, aml_context->framequeue.size);
  av_log(avctx, AV_LOG_DEBUG, "Decoder status : %d, (%d errors), PTS: video : %f, pcrscr: %f\n", 
         aml_context->decoder_status.status, aml_context->decoder_status.error_count,
	 video_pts, pcrscr_pts);
}

int ffmal_init_bitstream(AVCodecContext *avctx)
{
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;
  int ret = 0;

  if (!aml_context->bsf)
  {
    const AVBitStreamFilter *bsf;

    // check if we need a bitstream filter
    switch(avctx->codec_id)
    {
      case AV_CODEC_ID_H264:
        bsf = av_bsf_get_by_name("h264_mp4toannexb");
      break;

      case AV_CODEC_ID_HEVC:
        bsf = av_bsf_get_by_name("hevc_mp4toannexb");
      break;

    default:
      av_log(avctx, AV_LOG_ERROR, "Not using any bitstream filter\n");
      return 0;
    }


    if(!bsf)
        return AVERROR_BSF_NOT_FOUND;

    av_log(avctx, AV_LOG_ERROR, "using bitstream filter %s\n", bsf->name);

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

void ffaml_create_prefeed_header(AVCodecContext *avctx, char *extradata, int extradatasize)
{
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;

  switch(aml_get_vformat(avctx))
  {
    default:
      // just copy over the extradata for those
      memcpy(aml_context->header.data, extradata, extradatasize);
      aml_context->header.size = extradatasize;
    break;

//    case VFORMAT_MPEG4:
//     if (aml_get_vdec_type(avctx) == VIDEO_DEC_FORMAT_MPEG4_3)
//     {

//     }

    break;

  }
}

void ffaml_checkin_packet_pts(AVCodecContext *avctx, AVPacket *avpkt)
{
  int ret;
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;

  double pts = (avpkt->pts * PTS_FREQ) * av_q2d(avctx->time_base);
  aml_context->last_checkin_pts = avpkt->pts * av_q2d(avctx->time_base);
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
  aml_context->last_checkin_pts = 0;
  ffaml_init_queue(&aml_context->writequeue);
  ffaml_init_queue(&aml_context->framequeue);

  // setup the codec structure for amcodec
  memset(pcodec, 0, sizeof(codec_para_t));
  memset(&aml_context->buffer_status, 0, sizeof(aml_context->buffer_status));
  memset(&aml_context->decoder_status, 0, sizeof(aml_context->decoder_status));
  memset(&aml_context->header, 0, sizeof(aml_context->header));

  // initialize ion driver
  ret = aml_ion_open(avctx, &aml_context->ion_context);
  if (ret < 0)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to init ion driver\n");
    return -1;
  }

  pcodec->stream_type = STREAM_TYPE_ES_VIDEO;
  pcodec->has_video = 1;
  pcodec->video_type = aml_get_vformat(avctx);
  pcodec->am_sysinfo.format = aml_get_vdec_type(avctx);
  pcodec->am_sysinfo.param = (void*)(EXTERNAL_PTS | SYNC_OUTSIDE);

  ret = codec_init(pcodec);
  if (ret != CODEC_ERROR_NONE)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to init amcodec decoder\n");
    return -1;
  }

  codec_resume(pcodec);

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

  // close ion driver
  aml_ion_close(avctx, &aml_context->ion_context);

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
  int got_buffer;

  //av_log(avctx, AV_LOG_DEBUG, "decode start\n");
  //ffaml_log_decoder_info(avctx);
//  ret = aml_ion_dequeue_buffer(avctx, &aml_context->ion_context, &got_buffer);
//  if (got_buffer)
//  {
//    av_log(avctx, AV_LOG_DEBUG, "LongChair Got Buffer %d (pts=%ld)!!!\n", ret, aml_context->ion_context.buffers[ret].pts);
//  }
//  else
//  {
//    av_log(avctx, AV_LOG_DEBUG, "LongChair Didn't get buffer :(\n");
//  }


  //av_log(avctx, AV_LOG_DEBUG, "LongChair Queued buffer 0\n");

  if ((avpkt) && (avpkt->data))
  {
    if (aml_context->bsf)
    {
      // if we use a bitstream filter, then use it
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
      // otherwise, we shouldn't need it, just use plain extradata
      extradata = avctx->extradata;
      extradata_size = avctx->extradata_size;
    }

    // we need make a header from extradata to prefeed the decoder
    ffaml_create_prefeed_header(avctx, extradata, extradata_size);

    // we need to write the header on first packet
    if ((aml_context->first_packet) && (aml_context->header.size))
    {
      ret = codec_write(pcodec, aml_context->header.data, aml_context->header.size);
      aml_context->first_packet = 0;
      //codec_resume(pcodec);
    }

    // queue the packet
    if (ffaml_queue_packet(avctx, &aml_context->writequeue, avpkt) < 0)
    {
      av_log(avctx, AV_LOG_DEBUG, "failed to queue AvPacket\n");
      return -1;
    }
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

      //av_log(avctx, AV_LOG_DEBUG, "Writing packet to decoder (%d) bytes\n", avpkt->size);
      av_log(avctx, AV_LOG_DEBUG, "LongChair : writing frame with pts=%f, checkin =%f\n", pkt->pts * av_q2d(avctx->time_base), aml_context->last_checkin_pts);
      if (ffaml_write_pkt_data(avctx, pkt) < 0)
      {
        av_log(avctx, AV_LOG_ERROR, "failed to write packet.\n");
        return -1;
      }

     // now queue the packet to the frame queue
#if 0
     ffaml_queue_packet(avctx, &aml_context->framequeue, pkt);
#endif
    }
  }

  // grab the video decoder status
  ret = codec_get_vdec_state(pcodec, &aml_context->decoder_status);
  if (ret < 0)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to retrieve video decoder status (code=%d)\n", ret);
    return -1;
  }

  ret = aml_ion_dequeue_buffer(avctx, &aml_context->ion_context, got_frame);
  if (*got_frame)
  {
    av_log(avctx, AV_LOG_DEBUG, "LongChair Got Buffer %d (pts=%f)!!!\n", ret, aml_context->ion_context.buffers[ret].pts * av_q2d(avctx->time_base));

    frame->width = aml_context->decoder_status.width;
    frame->height = aml_context->decoder_status.height;
    frame->format = AV_PIX_FMT_AML;

    ff_set_dimensions(avctx, frame->width, frame->height);
    avctx->pix_fmt = frame->format;

    frame->buf[0] = av_buffer_create(NULL, 0, NULL, NULL, AV_BUFFER_FLAG_READONLY);
    frame->data[0] = (uint8_t*)aml_context->ion_context.buffers[ret].data;
    frame->pkt_pts = aml_context->ion_context.buffers[ret].pts;

    //aml_ion_save_buffer("yuv.dat", &aml_context->ion_context.buffers[ret]);

    aml_ion_queue_buffer(avctx, &aml_context->ion_context, &aml_context->ion_context.buffers[ret]);
  }


#if 0
  // now we need to make a dummy avframe as we will not get any data out of the codec
  // we wait to have 16 frames because with some codecs, pts are in the wrong order
  if ((aml_context->decoder_status.width > 0) && (aml_context->framequeue.size >= MIN_FRAME_QUEUE_SIZE))
  {
    //av_log(avctx, AV_LOG_DEBUG, "peeking pts\n");
    pkt = ffaml_queue_peek_pts_packet(avctx, &aml_context->framequeue);
    if (pkt)
    {
      av_log(avctx, AV_LOG_DEBUG, "preparing frame\n");
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

      av_log(avctx, AV_LOG_DEBUG, "LongChair sending frame with pts=%f, checkin =%f\n", pkt->pts * av_q2d(avctx->time_base), aml_context->last_checkin_pts);

      av_packet_free(&pkt);
      *got_frame = 1;
    }

  }
#endif

  //av_log(avctx, AV_LOG_DEBUG, "decode end\n");
  return 0;
}

static void ffaml_flush(AVCodecContext *avctx)
{
  AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;

  av_log(avctx, AV_LOG_DEBUG, "Flushing ...\n");
  ffaml_queue_clear(avctx, &aml_context->writequeue);
  ffaml_queue_clear(avctx, &aml_context->framequeue);
  av_log(avctx, AV_LOG_DEBUG, "Flushing done.\n");
}


#define FFAML_DEC_HWACCEL(NAME, ID) \
  AVHWAccel ff_##NAME##_aml_hwaccel = { \
      .name       = #NAME "_aml", \
      .type       = AVMEDIA_TYPE_VIDEO,\
      .id         = ID, \
      .pix_fmt    = AV_PIX_FMT_YUV420P /*AV_PIX_FMT_AML*/,\
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
        .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P /*AV_PIX_FMT_AML*/, \
                                                         AV_PIX_FMT_NONE}, \
    };

FFAML_DEC(h264, AV_CODEC_ID_H264)

FFAML_DEC(hevc, AV_CODEC_ID_HEVC)

FFAML_DEC(mpeg4, AV_CODEC_ID_MPEG4)
