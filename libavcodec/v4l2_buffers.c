/*
 * V4L2 buffer helper functions.
 *
 * Copyright (C) 2017 Alexis Ballier <aballier@gentoo.org>
 * Copyright (C) 2017 Jorge Ramirez <jorge.ramirez-ortiz@linaro.org>
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

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include "libavcodec/avcodec.h"
#include "libavcodec/internal.h"
#include "v4l2_context.h"
#include "v4l2_buffers.h"
#include "v4l2_m2m.h"

#define USEC_PER_SEC 1000000

static inline V4L2m2mContext *buf_to_m2mctx(V4L2Buffer *buf)
{
    return V4L2_TYPE_IS_OUTPUT(buf->context->type) ?
        container_of(buf->context, V4L2m2mContext, output) :
        container_of(buf->context, V4L2m2mContext, capture);
}

static inline AVCodecContext *logger(V4L2Buffer *buf)
{
    return buf_to_m2mctx(buf)->avctx;
}

static inline void v4l2_set_pts(V4L2Buffer *out, int64_t pts)
{
    V4L2m2mContext *s = buf_to_m2mctx(out);
    AVRational v4l2_timebase = { 1, USEC_PER_SEC };
    int64_t v4l2_pts;

    if (pts == AV_NOPTS_VALUE)
        pts = 0;

    /* convert pts to v4l2 timebase */
    v4l2_pts = av_rescale_q(pts, s->avctx->time_base, v4l2_timebase);
    out->buf.timestamp.tv_usec = v4l2_pts % USEC_PER_SEC;
    out->buf.timestamp.tv_sec = v4l2_pts / USEC_PER_SEC;
}

static inline uint64_t v4l2_get_pts(V4L2Buffer *avbuf)
{
    V4L2m2mContext *s = buf_to_m2mctx(avbuf);
    AVRational v4l2_timebase = { 1, USEC_PER_SEC };
    int64_t v4l2_pts;

    /* convert pts back to encoder timebase */
    v4l2_pts = avbuf->buf.timestamp.tv_sec * USEC_PER_SEC + avbuf->buf.timestamp.tv_usec;

    return av_rescale_q(v4l2_pts, v4l2_timebase, s->avctx->time_base);
}

static enum AVColorPrimaries v4l2_get_color_primaries(V4L2Buffer *buf)
{
    enum v4l2_ycbcr_encoding ycbcr;
    enum v4l2_colorspace cs;

    cs = V4L2_TYPE_IS_MULTIPLANAR(buf->buf.type) ?
        buf->context->format.fmt.pix_mp.colorspace :
        buf->context->format.fmt.pix.colorspace;

    ycbcr = V4L2_TYPE_IS_MULTIPLANAR(buf->buf.type) ?
        buf->context->format.fmt.pix_mp.ycbcr_enc:
        buf->context->format.fmt.pix.ycbcr_enc;

    switch(ycbcr) {
    case V4L2_YCBCR_ENC_XV709:
    case V4L2_YCBCR_ENC_709: return AVCOL_PRI_BT709;
    case V4L2_YCBCR_ENC_XV601:
    case V4L2_YCBCR_ENC_601:return AVCOL_PRI_BT470M;
    default:
        break;
    }

    switch(cs) {
    case V4L2_COLORSPACE_470_SYSTEM_BG: return AVCOL_PRI_BT470BG;
    case V4L2_COLORSPACE_SMPTE170M: return AVCOL_PRI_SMPTE170M;
    case V4L2_COLORSPACE_SMPTE240M: return AVCOL_PRI_SMPTE240M;
    case V4L2_COLORSPACE_BT2020: return AVCOL_PRI_BT2020;
    default:
        break;
    }

    return AVCOL_PRI_UNSPECIFIED;
}

static enum AVColorRange v4l2_get_color_range(V4L2Buffer *buf)
{
    enum v4l2_quantization qt;

    qt = V4L2_TYPE_IS_MULTIPLANAR(buf->buf.type) ?
        buf->context->format.fmt.pix_mp.quantization :
        buf->context->format.fmt.pix.quantization;

    switch (qt) {
    case V4L2_QUANTIZATION_LIM_RANGE: return AVCOL_RANGE_MPEG;
    case V4L2_QUANTIZATION_FULL_RANGE: return AVCOL_RANGE_JPEG;
    default:
        break;
    }

     return AVCOL_RANGE_UNSPECIFIED;
}

static enum AVColorSpace v4l2_get_color_space(V4L2Buffer *buf)
{
    enum v4l2_ycbcr_encoding ycbcr;
    enum v4l2_colorspace cs;

    cs = V4L2_TYPE_IS_MULTIPLANAR(buf->buf.type) ?
        buf->context->format.fmt.pix_mp.colorspace :
        buf->context->format.fmt.pix.colorspace;

    ycbcr = V4L2_TYPE_IS_MULTIPLANAR(buf->buf.type) ?
        buf->context->format.fmt.pix_mp.ycbcr_enc:
        buf->context->format.fmt.pix.ycbcr_enc;

    switch(cs) {
    case V4L2_COLORSPACE_SRGB: return AVCOL_SPC_RGB;
    case V4L2_COLORSPACE_REC709: return AVCOL_SPC_BT709;
    case V4L2_COLORSPACE_470_SYSTEM_M: return AVCOL_SPC_FCC;
    case V4L2_COLORSPACE_470_SYSTEM_BG: return AVCOL_SPC_BT470BG;
    case V4L2_COLORSPACE_SMPTE170M: return AVCOL_SPC_SMPTE170M;
    case V4L2_COLORSPACE_SMPTE240M: return AVCOL_SPC_SMPTE240M;
    case V4L2_COLORSPACE_BT2020:
        if (ycbcr == V4L2_YCBCR_ENC_BT2020_CONST_LUM)
            return AVCOL_SPC_BT2020_CL;
        else
             return AVCOL_SPC_BT2020_NCL;
    default:
        break;
    }

    return AVCOL_SPC_UNSPECIFIED;
}

static enum AVColorTransferCharacteristic v4l2_get_color_trc(V4L2Buffer *buf)
{
    enum v4l2_ycbcr_encoding ycbcr;
    enum v4l2_xfer_func xfer;
    enum v4l2_colorspace cs;

    cs = V4L2_TYPE_IS_MULTIPLANAR(buf->buf.type) ?
        buf->context->format.fmt.pix_mp.colorspace :
        buf->context->format.fmt.pix.colorspace;

    ycbcr = V4L2_TYPE_IS_MULTIPLANAR(buf->buf.type) ?
        buf->context->format.fmt.pix_mp.ycbcr_enc:
        buf->context->format.fmt.pix.ycbcr_enc;

    xfer = V4L2_TYPE_IS_MULTIPLANAR(buf->buf.type) ?
        buf->context->format.fmt.pix_mp.xfer_func:
        buf->context->format.fmt.pix.xfer_func;

    switch (xfer) {
    case V4L2_XFER_FUNC_709: return AVCOL_TRC_BT709;
    case V4L2_XFER_FUNC_SRGB: return AVCOL_TRC_IEC61966_2_1;
    default:
        break;
    }

    switch (cs) {
    case V4L2_COLORSPACE_470_SYSTEM_M: return AVCOL_TRC_GAMMA22;
    case V4L2_COLORSPACE_470_SYSTEM_BG: return AVCOL_TRC_GAMMA28;
    case V4L2_COLORSPACE_SMPTE170M: return AVCOL_TRC_SMPTE170M;
    case V4L2_COLORSPACE_SMPTE240M: return AVCOL_TRC_SMPTE240M;
    default:
        break;
    }

    switch (ycbcr) {
    case V4L2_YCBCR_ENC_XV709:
    case V4L2_YCBCR_ENC_XV601: return AVCOL_TRC_BT1361_ECG;
    default:
        break;
    }

    return AVCOL_TRC_UNSPECIFIED;
}

static void v4l2_free_buffer(void *opaque, uint8_t *unused)
{
    V4L2Buffer* avbuf = opaque;
    V4L2m2mContext *s = buf_to_m2mctx(avbuf);

    atomic_fetch_sub_explicit(&s->refcount, 1, memory_order_acq_rel);
    if (s->reinit) {
        if (!atomic_load(&s->refcount))
            sem_post(&s->refsync);
        return;
    }

    if (avbuf->context->streamon) {
        ff_v4l2_buffer_enqueue(avbuf);
        return;
    }

    if (!atomic_load(&s->refcount))
        ff_v4l2_m2m_codec_end(s->avctx);
}

static int v4l2_buf_to_bufref(V4L2Buffer *in, int plane, AVBufferRef **buf)
{
    V4L2m2mContext *s = buf_to_m2mctx(in);

    if (plane >= in->num_planes)
        return AVERROR(EINVAL);

    /* even though most encoders return 0 in data_offset encoding vp8 does require this value */
    *buf = av_buffer_create((char *)in->plane_info[plane].mm_addr + in->planes[plane].data_offset,
                            in->plane_info[plane].length, v4l2_free_buffer, in, 0);
    if (!*buf)
        return AVERROR(ENOMEM);

    in->status = V4L2BUF_RET_USER;
    atomic_fetch_add_explicit(&s->refcount, 1, memory_order_relaxed);

    return 0;
}

static int v4l2_bufref_to_buf(V4L2Buffer *out, int plane, const uint8_t* data, int size, AVBufferRef* bref)
{
    unsigned int bytesused, length;

    if (plane >= out->num_planes)
        return AVERROR(EINVAL);

    bytesused = FFMIN(size, out->plane_info[plane].length);
    length = out->plane_info[plane].length;

    memcpy(out->plane_info[plane].mm_addr, data, FFMIN(size, out->plane_info[plane].length));

    if (V4L2_TYPE_IS_MULTIPLANAR(out->buf.type)) {
        out->planes[plane].bytesused = bytesused;
        out->planes[plane].length = length;
    } else {
        out->buf.bytesused = bytesused;
        out->buf.length = length;
    }

    return 0;
}

/******************************************************************************
 *
 *              V4L2uffer interface
 *
 ******************************************************************************/

int ff_v4l2_buffer_avframe_to_buf(const AVFrame *frame, V4L2Buffer* out)
{
    int i, ret;

    for(i = 0; i < out->num_planes; i++) {
        ret = v4l2_bufref_to_buf(out, i, frame->buf[i]->data, frame->buf[i]->size, frame->buf[i]);
        if (ret)
            return ret;
    }

    v4l2_set_pts(out, frame->pts);

    return 0;
}

int ff_v4l2_buffer_buf_to_avframe(AVFrame *frame, V4L2Buffer *avbuf)
{
    V4L2m2mContext *s = buf_to_m2mctx(avbuf);
    int i, ret;

    av_frame_unref(frame);

    /* 1. get references to the actual data */
    for (i = 0; i < avbuf->num_planes; i++) {
        ret = v4l2_buf_to_bufref(avbuf, i, &frame->buf[i]);
        if (ret)
            return ret;

        frame->linesize[i] = avbuf->plane_info[i].bytesperline;
        frame->data[i] = frame->buf[i]->data;
    }

    /* 1.1 fixup special cases */
    switch (avbuf->context->av_pix_fmt) {
    case AV_PIX_FMT_NV12:
        if (avbuf->num_planes > 1)
            break;
        frame->linesize[1] = avbuf->plane_info[0].bytesperline;
        frame->data[1] = frame->buf[0]->data + avbuf->plane_info[0].bytesperline * avbuf->context->format.fmt.pix_mp.height;
        break;
    default:
        break;
    }

    /* 2. get frame information */
    frame->key_frame = !!(avbuf->buf.flags & V4L2_BUF_FLAG_KEYFRAME);
    frame->format = avbuf->context->av_pix_fmt;
    frame->color_primaries = v4l2_get_color_primaries(avbuf);
    frame->colorspace = v4l2_get_color_space(avbuf);
    frame->color_range = v4l2_get_color_range(avbuf);
    frame->color_trc = v4l2_get_color_trc(avbuf);
    frame->pts = v4l2_get_pts(avbuf);

    /* these two values are updated also during re-init in v4l2_process_driver_event */
    frame->height = s->output.height;
    frame->width = s->output.width;

    /* 3. report errors upstream */
    if (avbuf->buf.flags & V4L2_BUF_FLAG_ERROR) {
        av_log(logger(avbuf), AV_LOG_ERROR, "%s: driver decode error\n", avbuf->context->name);
        frame->decode_error_flags |= FF_DECODE_ERROR_INVALID_BITSTREAM;
    }

    return 0;
}

int ff_v4l2_buffer_buf_to_avpkt(AVPacket *pkt, V4L2Buffer *avbuf)
{
    int ret;

    av_packet_unref(pkt);
    ret = v4l2_buf_to_bufref(avbuf, 0, &pkt->buf);
    if (ret)
        return ret;

    pkt->size = V4L2_TYPE_IS_MULTIPLANAR(avbuf->buf.type) ? avbuf->buf.m.planes[0].bytesused : avbuf->buf.bytesused;
    pkt->data = pkt->buf->data;

    if (avbuf->buf.flags & V4L2_BUF_FLAG_KEYFRAME)
        pkt->flags |= AV_PKT_FLAG_KEY;

    if (avbuf->buf.flags & V4L2_BUF_FLAG_ERROR) {
        av_log(logger(avbuf), AV_LOG_ERROR, "%s driver encode error\n", avbuf->context->name);
        pkt->flags |= AV_PKT_FLAG_CORRUPT;
    }

    pkt->dts = pkt->pts = v4l2_get_pts(avbuf);

    return 0;
}

int ff_v4l2_buffer_avpkt_to_buf(const AVPacket *pkt, V4L2Buffer *out)
{
    int ret;

    ret = v4l2_bufref_to_buf(out, 0, pkt->data, pkt->size, pkt->buf);
    if (ret)
        return ret;

    v4l2_set_pts(out, pkt->pts);

    if (pkt->flags & AV_PKT_FLAG_KEY)
        out->flags = V4L2_BUF_FLAG_KEYFRAME;

    return 0;
}

int ff_v4l2_buffer_initialize(V4L2Buffer* avbuf, int index)
{
    V4L2Context *ctx = avbuf->context;
    int ret, i;

    avbuf->buf.memory = V4L2_MEMORY_MMAP;
    avbuf->buf.type = ctx->type;
    avbuf->buf.index = index;

    if (V4L2_TYPE_IS_MULTIPLANAR(ctx->type)) {
        avbuf->buf.length = VIDEO_MAX_PLANES;
        avbuf->buf.m.planes = avbuf->planes;
    }

    ret = ioctl(buf_to_m2mctx(avbuf)->fd, VIDIOC_QUERYBUF, &avbuf->buf);
    if (ret < 0)
        return AVERROR(errno);

    if (V4L2_TYPE_IS_MULTIPLANAR(ctx->type)) {
        avbuf->num_planes = 0;
        for (;;) {
            /* in MP, the V4L2 API states that buf.length means num_planes */
            if (avbuf->num_planes >= avbuf->buf.length)
                break;
            if (avbuf->buf.m.planes[avbuf->num_planes].length)
                avbuf->num_planes++;
        }
    } else
        avbuf->num_planes = 1;

    for (i = 0; i < avbuf->num_planes; i++) {

        avbuf->plane_info[i].bytesperline = V4L2_TYPE_IS_MULTIPLANAR(ctx->type) ?
            ctx->format.fmt.pix_mp.plane_fmt[i].bytesperline :
            ctx->format.fmt.pix.bytesperline;

        if (V4L2_TYPE_IS_MULTIPLANAR(ctx->type)) {
            avbuf->plane_info[i].length = avbuf->buf.m.planes[i].length;
            avbuf->plane_info[i].mm_addr = mmap(NULL, avbuf->buf.m.planes[i].length,
                                           PROT_READ | PROT_WRITE, MAP_SHARED,
                                           buf_to_m2mctx(avbuf)->fd, avbuf->buf.m.planes[i].m.mem_offset);
        } else {
            avbuf->plane_info[i].length = avbuf->buf.length;
            avbuf->plane_info[i].mm_addr = mmap(NULL, avbuf->buf.length,
                                          PROT_READ | PROT_WRITE, MAP_SHARED,
                                          buf_to_m2mctx(avbuf)->fd, avbuf->buf.m.offset);
        }

        if (avbuf->plane_info[i].mm_addr == MAP_FAILED)
            return AVERROR(ENOMEM);
    }

    avbuf->status = V4L2BUF_AVAILABLE;

    if (V4L2_TYPE_IS_OUTPUT(ctx->type))
        return 0;

    if (V4L2_TYPE_IS_MULTIPLANAR(ctx->type)) {
        avbuf->buf.m.planes = avbuf->planes;
        avbuf->buf.length   = avbuf->num_planes;

    } else {
        avbuf->buf.bytesused = avbuf->planes[0].bytesused;
        avbuf->buf.length    = avbuf->planes[0].length;
    }

    return ff_v4l2_buffer_enqueue(avbuf);
}

int ff_v4l2_buffer_enqueue(V4L2Buffer* avbuf)
{
    int ret;

    avbuf->buf.flags = avbuf->flags;

    ret = ioctl(buf_to_m2mctx(avbuf)->fd, VIDIOC_QBUF, &avbuf->buf);
    if (ret < 0)
        return AVERROR(errno);

    avbuf->status = V4L2BUF_IN_DRIVER;

    return 0;
}
