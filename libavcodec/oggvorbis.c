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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file oggvorbis.c
 * Ogg Vorbis codec support via libvorbisenc.
 * @author Mark Hills <mark@pogo.org.uk>
 */

#include <vorbis/vorbisenc.h>

#include "avcodec.h"

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

static int oggvorbis_encode_init(AVCodecContext *avccontext) {
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
    float **buffer ;
    ogg_packet op ;
    signed short *audio = data ;
    int l, samples = data ? OGGVORBIS_FRAME_SIZE : 0;

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


static int oggvorbis_encode_close(AVCodecContext *avccontext) {
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


AVCodec oggvorbis_encoder = {
    "vorbis",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    sizeof(OggVorbisContext),
    oggvorbis_encode_init,
    oggvorbis_encode_frame,
    oggvorbis_encode_close,
    .capabilities= CODEC_CAP_DELAY,
} ;

static int oggvorbis_decode_init(AVCodecContext *avccontext) {
    OggVorbisContext *context = avccontext->priv_data ;
    uint8_t *p= avccontext->extradata;
    int i, hsizes[3];
    unsigned char *headers[3], *extradata = avccontext->extradata;

    vorbis_info_init(&context->vi) ;
    vorbis_comment_init(&context->vc) ;

    if(! avccontext->extradata_size || ! p) {
        av_log(avccontext, AV_LOG_ERROR, "vorbis extradata absent\n");
        return -1;
    }

    if(p[0] == 0 && p[1] == 30) {
        for(i = 0; i < 3; i++){
            hsizes[i] = *p++ << 8;
            hsizes[i] += *p++;
            headers[i] = p;
            p += hsizes[i];
        }
    } else if(*p == 2) {
        unsigned int offset = 1;
        p++;
        for(i=0; i<2; i++) {
            hsizes[i] = 0;
            while((*p == 0xFF) && (offset < avccontext->extradata_size)) {
                hsizes[i] += 0xFF;
                offset++;
                p++;
            }
            if(offset >= avccontext->extradata_size - 1) {
                av_log(avccontext, AV_LOG_ERROR,
                       "vorbis header sizes damaged\n");
                return -1;
            }
            hsizes[i] += *p;
            offset++;
            p++;
        }
        hsizes[2] = avccontext->extradata_size - hsizes[0]-hsizes[1]-offset;
#if 0
        av_log(avccontext, AV_LOG_DEBUG,
               "vorbis header sizes: %d, %d, %d, / extradata_len is %d \n",
               hsizes[0], hsizes[1], hsizes[2], avccontext->extradata_size);
#endif
        headers[0] = extradata + offset;
        headers[1] = extradata + offset + hsizes[0];
        headers[2] = extradata + offset + hsizes[0] + hsizes[1];
    } else {
        av_log(avccontext, AV_LOG_ERROR,
               "vorbis initial header len is wrong: %d\n", *p);
        return -1;
    }

    for(i=0; i<3; i++){
        context->op.b_o_s= i==0;
        context->op.bytes = hsizes[i];
        context->op.packet = headers[i];
        if(vorbis_synthesis_headerin(&context->vi, &context->vc, &context->op)<0){
            av_log(avccontext, AV_LOG_ERROR, "%d. vorbis header damaged\n", i+1);
            return -1;
        }
    }

    avccontext->channels = context->vi.channels;
    avccontext->sample_rate = context->vi.rate;
    avccontext->time_base= (AVRational){1, avccontext->sample_rate};

    vorbis_synthesis_init(&context->vd, &context->vi);
    vorbis_block_init(&context->vd, &context->vb);

    return 0 ;
}


static inline int conv(int samples, float **pcm, char *buf, int channels) {
    int i, j, val ;
    ogg_int16_t *ptr, *data = (ogg_int16_t*)buf ;
    float *mono ;

    for(i = 0 ; i < channels ; i++){
        ptr = &data[i];
        mono = pcm[i] ;

        for(j = 0 ; j < samples ; j++) {

            val = mono[j] * 32767.f;

            if(val > 32767) val = 32767 ;
            if(val < -32768) val = -32768 ;

            *ptr = val ;
            ptr += channels;
        }
    }

    return 0 ;
}


static int oggvorbis_decode_frame(AVCodecContext *avccontext,
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    OggVorbisContext *context = avccontext->priv_data ;
    float **pcm ;
    ogg_packet *op= &context->op;
    int samples, total_samples, total_bytes;

    if(!buf_size){
    //FIXME flush
        return 0;
    }

    op->packet = buf;
    op->bytes  = buf_size;

//    av_log(avccontext, AV_LOG_DEBUG, "%d %d %d %"PRId64" %"PRId64" %d %d\n", op->bytes, op->b_o_s, op->e_o_s, op->granulepos, op->packetno, buf_size, context->vi.rate);

/*    for(i=0; i<op->bytes; i++)
      av_log(avccontext, AV_LOG_DEBUG, "%02X ", op->packet[i]);
    av_log(avccontext, AV_LOG_DEBUG, "\n");*/

    if(vorbis_synthesis(&context->vb, op) == 0)
        vorbis_synthesis_blockin(&context->vd, &context->vb) ;

    total_samples = 0 ;
    total_bytes = 0 ;

    while((samples = vorbis_synthesis_pcmout(&context->vd, &pcm)) > 0) {
        conv(samples, pcm, (char*)data + total_bytes, context->vi.channels) ;
        total_bytes += samples * 2 * context->vi.channels ;
        total_samples += samples ;
        vorbis_synthesis_read(&context->vd, samples) ;
    }

    *data_size = total_bytes ;
    return buf_size ;
}


static int oggvorbis_decode_close(AVCodecContext *avccontext) {
    OggVorbisContext *context = avccontext->priv_data ;

    vorbis_info_clear(&context->vi) ;
    vorbis_comment_clear(&context->vc) ;

    return 0 ;
}


AVCodec oggvorbis_decoder = {
    "vorbis",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    sizeof(OggVorbisContext),
    oggvorbis_decode_init,
    NULL,
    oggvorbis_decode_close,
    oggvorbis_decode_frame,
    .capabilities= CODEC_CAP_DELAY,
} ;
