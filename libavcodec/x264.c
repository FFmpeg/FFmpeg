/*
 * H.264 encoding using the x264 library
 * Copyright (C) 2005  Mans Rullgard <mru@inprovide.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "avcodec.h"
#include <x264.h>

typedef struct X264Context {
    x264_param_t params;
    x264_t *enc;
    x264_picture_t pic;
    AVFrame out_pic;
} X264Context;

static void
X264_log(void *p, int level, const char *fmt, va_list args)
{
    static const int level_map[] = {
	[X264_LOG_ERROR]   = AV_LOG_ERROR,
	[X264_LOG_WARNING] = AV_LOG_ERROR,
	[X264_LOG_INFO]    = AV_LOG_INFO,
	[X264_LOG_DEBUG]   = AV_LOG_DEBUG
    };

    if(level < 0 || level > X264_LOG_DEBUG)
	return;

    av_vlog(p, level_map[level], fmt, args);
}


static int
encode_nals(uint8_t *buf, int size, x264_nal_t *nals, int nnal)
{
    uint8_t *p = buf;
    int i;

    for(i = 0; i < nnal; i++){
	int s = x264_nal_encode(p, &size, 1, nals + i);
	if(s < 0)
	    return -1;
	p += s;
    }

    return p - buf;
}

extern int
X264_frame(AVCodecContext *ctx, uint8_t *buf, int bufsize, void *data)
{
    X264Context *x4 = ctx->priv_data;
    AVFrame *frame = data;
    x264_nal_t *nal;
    int nnal, i;
    x264_picture_t pic_out;

    x4->pic.img.i_csp = X264_CSP_I420;
    x4->pic.img.i_plane = 3;

    for(i = 0; i < 3; i++){
	x4->pic.img.plane[i] = frame->data[i];
	x4->pic.img.i_stride[i] = frame->linesize[i];
    }

    x4->pic.i_pts = frame->pts;
    x4->pic.i_type = X264_TYPE_AUTO;

    if(x264_encoder_encode(x4->enc, &nal, &nnal, &x4->pic, &pic_out))
	return -1;

    bufsize = encode_nals(buf, bufsize, nal, nnal);
    if(bufsize < 0)
	return -1;

    /* FIXME: dts */
    x4->out_pic.pts = pic_out.i_pts;

    switch(pic_out.i_type){
    case X264_TYPE_IDR:
    case X264_TYPE_I:
        x4->out_pic.pict_type = FF_I_TYPE;
        break;
    case X264_TYPE_P:
        x4->out_pic.pict_type = FF_P_TYPE;
        break;
    case X264_TYPE_B:
    case X264_TYPE_BREF:
        x4->out_pic.pict_type = FF_B_TYPE;
        break;
    }

    x4->out_pic.key_frame = pic_out.i_type == X264_TYPE_IDR;
    x4->out_pic.quality = (pic_out.i_qpplus1 - 1) * FF_QP2LAMBDA;

    return bufsize;
}

static int
X264_close(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;

    if(x4->enc)
	x264_encoder_close(x4->enc);

    return 0;
}

extern int
X264_init(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;

    x264_param_default(&x4->params);

    x4->params.pf_log = X264_log;
    x4->params.p_log_private = avctx;

    x4->params.i_keyint_max = avctx->gop_size;
    x4->params.rc.i_bitrate = avctx->bit_rate / 1000;
    x4->params.rc.i_vbv_buffer_size = avctx->rc_buffer_size / 1000;
    if(avctx->rc_buffer_size)
        x4->params.rc.b_cbr = 1;
    x4->params.i_bframe = avctx->max_b_frames;
    x4->params.b_cabac = avctx->coder_type == FF_CODER_TYPE_AC;

    x4->params.rc.i_qp_min = avctx->qmin;
    x4->params.rc.i_qp_max = avctx->qmax;
    x4->params.rc.i_qp_step = avctx->max_qdiff;

    if(avctx->flags & CODEC_FLAG_QSCALE && avctx->global_quality > 0)
        x4->params.rc.i_qp_constant =
            12 + 6 * log2((double) avctx->global_quality / FF_QP2LAMBDA);

    x4->params.i_width = avctx->width;
    x4->params.i_height = avctx->height;
    x4->params.vui.i_sar_width = avctx->sample_aspect_ratio.num;
    x4->params.vui.i_sar_height = avctx->sample_aspect_ratio.den;
    x4->params.i_fps_num = avctx->time_base.den;
    x4->params.i_fps_den = avctx->time_base.num;

    x4->enc = x264_encoder_open(&x4->params);
    if(!x4->enc)
        return -1;

    avctx->coded_frame = &x4->out_pic;

    return 0;
}

AVCodec x264_encoder = {
    .name = "h264",
    .type = CODEC_TYPE_VIDEO,
    .id = CODEC_ID_H264,
    .priv_data_size = sizeof(X264Context),
    .init = X264_init,
    .encode = X264_frame,
    .close = X264_close,
    .pix_fmts = (enum PixelFormat[]) { PIX_FMT_YUV420P, -1 }
};
