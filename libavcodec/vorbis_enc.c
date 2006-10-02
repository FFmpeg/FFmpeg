/*
 * copyright (c) 2006 Oded Shimon <ods15@ods15.dyndns.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file vorbis_enc.c
 * Native Vorbis encoder.
 * @author Oded Shimon <ods15@ods15.dyndns.org>
 */

#include "avcodec.h"

#undef NDEBUG
#include <assert.h>

#define VORBIS_FRAME_SIZE 64

#define BUFFER_SIZE (1024*64)

typedef struct {
    uint8_t buffer[BUFFER_SIZE];
    int buffer_index;
} venc_context_t;

static int vorbis_encode_init(AVCodecContext * avccontext) {
    venc_context_t * venc = avccontext->priv_data;
    uint8_t *p;
    unsigned int offset, len;

    avccontext->channels;
    avccontext->sample_rate;
    //if (avccontext->flags & CODEC_FLAG_QSCALE) avccontext->global_quality / (float)FF_QP2LAMBDA); else avccontext->bit_rate;
    //if(avccontext->cutoff > 0) cfreq = avccontext->cutoff / 1000.0;

    len = header.bytes + header_comm.bytes + header_code.bytes;
    avccontext->extradata_size = 64 + len + len/255;

    p = avccontext->extradata = av_mallocz(avccontext->extradata_size);
    p[0] = 2;
    offset = 1;
    offset += av_xiphlacing(&p[offset], header.bytes);
    offset += av_xiphlacing(&p[offset], header_comm.bytes);
    memcpy(&p[offset], header.packet, header.bytes);
    offset += header.bytes;
    memcpy(&p[offset], header_comm.packet, header_comm.bytes);
    offset += header_comm.bytes;
    memcpy(&p[offset], header_code.packet, header_code.bytes);
    offset += header_code.bytes;
    avccontext->extradata_size = offset;
    avccontext->extradata = av_realloc(avccontext->extradata, avccontext->extradata_size);

    avccontext->frame_size = OGGVORBIS_FRAME_SIZE;

    avccontext->coded_frame = avcodec_alloc_frame();
    avccontext->coded_frame->key_frame = 1;

    return 0;
}


static int vorbis_encode_frame(AVCodecContext * avccontext, unsigned char * packets, int buf_size, void *data)
{
    venc_context_t * venc = avccontext->priv_data;
    signed short * audio = data;
    int l, samples = data ? VORBIS_FRAME_SIZE : 0;



    if (!l) return 0;

    avccontext->coded_frame->pts = av_rescale_q(op2->granulepos, (AVRational){1, avccontext->sample_rate}, avccontext->time_base);
    memcpy(packets, compressed_frame, l);
    return l;
}


static int vorbis_encode_close(AVCodecContext * avccontext)
{
    venc_context_t * venc = avccontext->priv_data;

    av_freep(&avccontext->coded_frame);
    av_freep(&avccontext->extradata);

    return 0 ;
}

AVCodec oggvorbis_encoder = {
    "vorbis",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    sizeof(venc_context_t),
    vorbis_encode_init,
    vorbis_encode_frame,
    vorbis_encode_close,
    .capabilities= CODEC_CAP_DELAY,
};
