/*
 * copyright (c) 2002 Mark Hills <mark@pogo.org.uk>
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
 * @file libavcodec/libvorbis.c
 * Ogg Vorbis codec support via libvorbisenc.
 * @author Mark Hills <mark@pogo.org.uk>
 */

#include <vorbis/vorbisenc.h>

#include "avcodec.h"
#include "bytestream.h"

#undef NDEBUG
#include <assert.h>

#define OGGVORBIS_FRAME_SIZE 64

#define BUFFER_SIZE (1024*64)

typedef struct OggVorbisContext {
    vorbis_info vi ;
    vorbis_dsp_state vd ;
    vorbis_block vb ;
    uint8_t buffer[BUFFER_SIZE];
    int buffer_index;
    int eof;

    /* decoder */
    vorbis_comment vc ;
    ogg_packet op;
} OggVorbisContext ;


static int oggvorbis_init_encoder(vorbis_info *vi, AVCodecContext *avccontext) {
    double cfreq;

    if(avccontext->flags & CODEC_FLAG_QSCALE) {
        /* variable bitrate */
        if(vorbis_encode_setup_vbr(vi, avccontext->channels,
                avccontext->sample_rate,
                avccontext->global_quality / (float)FF_QP2LAMBDA))
            return -1;
    } else {
        /* constant bitrate */
        if(vorbis_encode_setup_managed(vi, avccontext->channels,
                avccontext->sample_rate, -1, avccontext->bit_rate, -1))
            return -1;

#ifdef OGGVORBIS_VBR_BY_ESTIMATE
        /* variable bitrate by estimate */
        if(vorbis_encode_ctl(vi, OV_ECTL_RATEMANAGE_AVG, NULL))
            return -1;
#endif
    }

    /* cutoff frequency */
    if(avccontext->cutoff > 0) {
        cfreq = avccontext->cutoff / 1000.0;
        if(vorbis_encode_ctl(vi, OV_ECTL_LOWPASS_SET, &cfreq))
            return -1;
    }

    return vorbis_encode_setup_init(vi);
}

static av_cold int oggvorbis_encode_init(AVCodecContext *avccontext) {
    OggVorbisContext *context = avccontext->priv_data ;
    ogg_packet header, header_comm, header_code;
    uint8_t *p;
    unsigned int offset, len;

    vorbis_info_init(&context->vi) ;
    if(oggvorbis_init_encoder(&context->vi, avccontext) < 0) {
        av_log(avccontext, AV_LOG_ERROR, "oggvorbis_encode_init: init_encoder failed") ;
        return -1 ;
    }
    vorbis_analysis_init(&context->vd, &context->vi) ;
    vorbis_block_init(&context->vd, &context->vb) ;

    vorbis_comment_init(&context->vc);
    vorbis_comment_add_tag(&context->vc, "encoder", LIBAVCODEC_IDENT) ;

    vorbis_analysis_headerout(&context->vd, &context->vc, &header,
                                &header_comm, &header_code);

    len = header.bytes + header_comm.bytes +  header_code.bytes;
    avccontext->extradata_size= 64 + len + len/255;
    p = avccontext->extradata= av_mallocz(avccontext->extradata_size);
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
    avccontext->extradata= av_realloc(avccontext->extradata, avccontext->extradata_size);

/*    vorbis_block_clear(&context->vb);
    vorbis_dsp_clear(&context->vd);
    vorbis_info_clear(&context->vi);*/
    vorbis_comment_clear(&context->vc);

    avccontext->frame_size = OGGVORBIS_FRAME_SIZE ;

    avccontext->coded_frame= avcodec_alloc_frame();
    avccontext->coded_frame->key_frame= 1;

    return 0 ;
}


static int oggvorbis_encode_frame(AVCodecContext *avccontext,
                                  unsigned char *packets,
                           int buf_size, void *data)
{
    OggVorbisContext *context = avccontext->priv_data ;
    ogg_packet op ;
    signed short *audio = data ;
    int l;

    if(data) {
        int samples = OGGVORBIS_FRAME_SIZE;
        float **buffer ;

        buffer = vorbis_analysis_buffer(&context->vd, samples) ;
        if(context->vi.channels == 1) {
            for(l = 0 ; l < samples ; l++)
                buffer[0][l]=audio[l]/32768.f;
        } else {
            for(l = 0 ; l < samples ; l++){
                buffer[0][l]=audio[l*2]/32768.f;
                buffer[1][l]=audio[l*2+1]/32768.f;
            }
        }
        vorbis_analysis_wrote(&context->vd, samples) ;
    } else {
        if(!context->eof)
            vorbis_analysis_wrote(&context->vd, 0) ;
        context->eof = 1;
    }

    while(vorbis_analysis_blockout(&context->vd, &context->vb) == 1) {
        vorbis_analysis(&context->vb, NULL);
        vorbis_bitrate_addblock(&context->vb) ;

        while(vorbis_bitrate_flushpacket(&context->vd, &op)) {
            /* i'd love to say the following line is a hack, but sadly it's
             * not, apparently the end of stream decision is in libogg. */
            if(op.bytes==1)
                continue;
            memcpy(context->buffer + context->buffer_index, &op, sizeof(ogg_packet));
            context->buffer_index += sizeof(ogg_packet);
            memcpy(context->buffer + context->buffer_index, op.packet, op.bytes);
            context->buffer_index += op.bytes;
//            av_log(avccontext, AV_LOG_DEBUG, "e%d / %d\n", context->buffer_index, op.bytes);
        }
    }

    l=0;
    if(context->buffer_index){
        ogg_packet *op2= (ogg_packet*)context->buffer;
        op2->packet = context->buffer + sizeof(ogg_packet);

        l=  op2->bytes;
        avccontext->coded_frame->pts= av_rescale_q(op2->granulepos, (AVRational){1, avccontext->sample_rate}, avccontext->time_base);
        //FIXME we should reorder the user supplied pts and not assume that they are spaced by 1/sample_rate

        memcpy(packets, op2->packet, l);
        context->buffer_index -= l + sizeof(ogg_packet);
        memcpy(context->buffer, context->buffer + l + sizeof(ogg_packet), context->buffer_index);
//        av_log(avccontext, AV_LOG_DEBUG, "E%d\n", l);
    }

    return l;
}


static av_cold int oggvorbis_encode_close(AVCodecContext *avccontext) {
    OggVorbisContext *context = avccontext->priv_data ;
/*  ogg_packet op ; */

    vorbis_analysis_wrote(&context->vd, 0) ; /* notify vorbisenc this is EOF */

    vorbis_block_clear(&context->vb);
    vorbis_dsp_clear(&context->vd);
    vorbis_info_clear(&context->vi);

    av_freep(&avccontext->coded_frame);
    av_freep(&avccontext->extradata);

    return 0 ;
}


AVCodec libvorbis_encoder = {
    "libvorbis",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    sizeof(OggVorbisContext),
    oggvorbis_encode_init,
    oggvorbis_encode_frame,
    oggvorbis_encode_close,
    .capabilities= CODEC_CAP_DELAY,
    .sample_fmts = (enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE},
    .long_name= NULL_IF_CONFIG_SMALL("libvorbis Vorbis"),
} ;
