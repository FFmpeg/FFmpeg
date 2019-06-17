/*
 * Copyright (c) 2002 Mark Hills <mark@pogo.org.uk>
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

#include <vorbis/vorbisenc.h>

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

typedef struct OggVorbisDecContext {
    vorbis_info vi;                     /**< vorbis_info used during init   */
    vorbis_dsp_state vd;                /**< DSP state used for analysis    */
    vorbis_block vb;                    /**< vorbis_block used for analysis */
    vorbis_comment vc;                  /**< VorbisComment info             */
    ogg_packet op;                      /**< ogg packet                     */
} OggVorbisDecContext;

static int oggvorbis_decode_close(AVCodecContext *avccontext);

static int oggvorbis_decode_init(AVCodecContext *avccontext) {
    OggVorbisDecContext *context = avccontext->priv_data ;
    uint8_t *p= avccontext->extradata;
    int i, hsizes[3], ret;
    unsigned char *headers[3], *extradata = avccontext->extradata;

    if(! avccontext->extradata_size || ! p) {
        av_log(avccontext, AV_LOG_ERROR, "vorbis extradata absent\n");
        return AVERROR(EINVAL);
    }

    vorbis_info_init(&context->vi) ;
    vorbis_comment_init(&context->vc) ;

    if(p[0] == 0 && p[1] == 30) {
        int sizesum = 0;
        for(i = 0; i < 3; i++){
            hsizes[i] = bytestream_get_be16((const uint8_t **)&p);
            sizesum += 2 + hsizes[i];
            if (sizesum > avccontext->extradata_size) {
                av_log(avccontext, AV_LOG_ERROR, "vorbis extradata too small\n");
                ret = AVERROR_INVALIDDATA;
                goto error;
            }

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
                ret = AVERROR_INVALIDDATA;
                goto error;
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
        ret = AVERROR_INVALIDDATA;
        goto error;
    }

    for(i=0; i<3; i++){
        context->op.b_o_s= i==0;
        context->op.bytes = hsizes[i];
        context->op.packet = headers[i];
        if(vorbis_synthesis_headerin(&context->vi, &context->vc, &context->op)<0){
            av_log(avccontext, AV_LOG_ERROR, "%d. vorbis header damaged\n", i+1);
            ret = AVERROR_INVALIDDATA;
            goto error;
        }
    }

    avccontext->channels = context->vi.channels;
    avccontext->sample_rate = context->vi.rate;
    avccontext->sample_fmt = AV_SAMPLE_FMT_S16;
    avccontext->time_base= (AVRational){1, avccontext->sample_rate};

    vorbis_synthesis_init(&context->vd, &context->vi);
    vorbis_block_init(&context->vd, &context->vb);

    return 0 ;

  error:
    oggvorbis_decode_close(avccontext);
    return ret;
}


static inline int conv(int samples, float **pcm, char *buf, int channels) {
    int i, j;
    ogg_int16_t *ptr, *data = (ogg_int16_t*)buf ;
    float *mono ;

    for(i = 0 ; i < channels ; i++){
        ptr = &data[i];
        mono = pcm[i] ;

        for(j = 0 ; j < samples ; j++) {
            *ptr = av_clip_int16(mono[j] * 32767.f);
            ptr += channels;
        }
    }

    return 0 ;
}

static int oggvorbis_decode_frame(AVCodecContext *avccontext, void *data,
                        int *got_frame_ptr, AVPacket *avpkt)
{
    OggVorbisDecContext *context = avccontext->priv_data ;
    AVFrame *frame = data;
    float **pcm ;
    ogg_packet *op= &context->op;
    int samples, total_samples, total_bytes;
    int ret;
    int16_t *output;

    if(!avpkt->size){
    //FIXME flush
        return 0;
    }

    frame->nb_samples = 8192*4;
    if ((ret = ff_get_buffer(avccontext, frame, 0)) < 0)
        return ret;
    output = (int16_t *)frame->data[0];


    op->packet = avpkt->data;
    op->bytes  = avpkt->size;

//    av_log(avccontext, AV_LOG_DEBUG, "%d %d %d %"PRId64" %"PRId64" %d %d\n", op->bytes, op->b_o_s, op->e_o_s, op->granulepos, op->packetno, buf_size, context->vi.rate);

/*    for(i=0; i<op->bytes; i++)
      av_log(avccontext, AV_LOG_DEBUG, "%02X ", op->packet[i]);
    av_log(avccontext, AV_LOG_DEBUG, "\n");*/

    if(vorbis_synthesis(&context->vb, op) == 0)
        vorbis_synthesis_blockin(&context->vd, &context->vb) ;

    total_samples = 0 ;
    total_bytes = 0 ;

    while((samples = vorbis_synthesis_pcmout(&context->vd, &pcm)) > 0) {
        conv(samples, pcm, (char*)output + total_bytes, context->vi.channels) ;
        total_bytes += samples * 2 * context->vi.channels ;
        total_samples += samples ;
        vorbis_synthesis_read(&context->vd, samples) ;
    }

    frame->nb_samples = total_samples;
    *got_frame_ptr   = total_samples > 0;
    return avpkt->size;
}


static int oggvorbis_decode_close(AVCodecContext *avccontext) {
    OggVorbisDecContext *context = avccontext->priv_data ;

    vorbis_block_clear(&context->vb);
    vorbis_dsp_clear(&context->vd);
    vorbis_info_clear(&context->vi) ;
    vorbis_comment_clear(&context->vc) ;

    return 0 ;
}


AVCodec ff_libvorbis_decoder = {
    .name           = "libvorbis",
    .long_name      = NULL_IF_CONFIG_SMALL("libvorbis"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_VORBIS,
    .priv_data_size = sizeof(OggVorbisDecContext),
    .init           = oggvorbis_decode_init,
    .decode         = oggvorbis_decode_frame,
    .close          = oggvorbis_decode_close,
    .capabilities   = AV_CODEC_CAP_DELAY,
};
