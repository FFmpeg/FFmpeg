/**
 * @file oggvorbis.c
 * Ogg Vorbis codec support via libvorbisenc.
 * @author Mark Hills <mark@pogo.org.uk>
 */

#include <time.h>

#include <vorbis/vorbisenc.h>

#include "avcodec.h"
#include "oggvorbis.h"

#define OGGVORBIS_FRAME_SIZE 1024


typedef struct OggVorbisContext {
    vorbis_info vi ;
    vorbis_dsp_state vd ;
    vorbis_block vb ;

    /* decoder */
    vorbis_comment vc ;
} OggVorbisContext ;


int oggvorbis_init_encoder(vorbis_info *vi, AVCodecContext *avccontext) {

#ifdef OGGVORBIS_VBR_BY_ESTIMATE
    /* variable bitrate by estimate */

    return (vorbis_encode_setup_managed(vi, avccontext->channels,
              avccontext->sample_rate, -1, avccontext->bit_rate, -1) ||
	    vorbis_encode_ctl(vi, OV_ECTL_RATEMANAGE_AVG, NULL) ||
	    vorbis_encode_setup_init(vi)) ;
#else
    /* constant bitrate */

    return vorbis_encode_init(vi, avccontext->channels,
	          avccontext->sample_rate, -1, avccontext->bit_rate, -1) ;
#endif
}


static int oggvorbis_encode_init(AVCodecContext *avccontext) {
    OggVorbisContext *context = avccontext->priv_data ;

    vorbis_info_init(&context->vi) ;
    if(oggvorbis_init_encoder(&context->vi, avccontext) < 0) {
	fprintf(stderr, "oggvorbis_encode_init: init_encoder failed") ;
	return -1 ;
    }
    vorbis_analysis_init(&context->vd, &context->vi) ;
    vorbis_block_init(&context->vd, &context->vb) ;

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
    signed char *audio = data ;
    int l, samples = OGGVORBIS_FRAME_SIZE ;

    buffer = vorbis_analysis_buffer(&context->vd, samples) ;

    if(context->vi.channels == 1) {
	for(l = 0 ; l < samples ; l++)
	    buffer[0][l]=((audio[l*2+1]<<8)|(0x00ff&(int)audio[l*2]))/32768.f;
    } else {
	for(l = 0 ; l < samples ; l++){
	    buffer[0][l]=((audio[l*4+1]<<8)|(0x00ff&(int)audio[l*4]))/32768.f;
	    buffer[1][l]=((audio[l*4+3]<<8)|(0x00ff&(int)audio[l*4+2]))/32768.f;
	}
    }
    
    vorbis_analysis_wrote(&context->vd, samples) ; 

    l = 0 ;

    while(vorbis_analysis_blockout(&context->vd, &context->vb) == 1) {
	vorbis_analysis(&context->vb, NULL);
	vorbis_bitrate_addblock(&context->vb) ;

	while(vorbis_bitrate_flushpacket(&context->vd, &op)) {
	    memcpy(packets + l, &op, sizeof(ogg_packet)) ;
	    memcpy(packets + l + sizeof(ogg_packet), op.packet, op.bytes) ;
	    l += sizeof(ogg_packet) + op.bytes ;
	}
    }

    return l ;
}


static int oggvorbis_encode_close(AVCodecContext *avccontext) {
    OggVorbisContext *context = avccontext->priv_data ;
/*  ogg_packet op ; */
    
    vorbis_analysis_wrote(&context->vd, 0) ; /* notify vorbisenc this is EOF */

    /* We need to write all the remaining packets into the stream
     * on closing */
    
    fprintf(stderr, "fixme: not all packets written on oggvorbis_encode_close()\n") ;

/*
    while(vorbis_bitrate_flushpacket(&context->vd, &op)) {
	memcpy(packets + l, &op, sizeof(ogg_packet)) ;
	memcpy(packets + l + sizeof(ogg_packet), op.packet, op.bytes) ;
	l += sizeof(ogg_packet) + op.bytes ;	
    }
*/

    vorbis_block_clear(&context->vb);
    vorbis_dsp_clear(&context->vd);
    vorbis_info_clear(&context->vi);

    av_freep(&avccontext->coded_frame);
  
    return 0 ;
}


AVCodec oggvorbis_encoder = {
    "vorbis",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VORBIS,
    sizeof(OggVorbisContext),
    oggvorbis_encode_init,
    oggvorbis_encode_frame,
    oggvorbis_encode_close
} ;


static int oggvorbis_decode_init(AVCodecContext *avccontext) {
    OggVorbisContext *context = avccontext->priv_data ;

    vorbis_info_init(&context->vi) ;
    vorbis_comment_init(&context->vc) ;

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
    ogg_packet *op = (ogg_packet*)buf ;
    float **pcm ;
    int samples, total_samples, total_bytes ;
 
    op->packet = (char*)op + sizeof(ogg_packet) ; /* correct data pointer */

    if(op->packetno < 3) {
	vorbis_synthesis_headerin(&context->vi, &context->vc, op) ;
	return buf_size ;
    }

    if(op->packetno == 3) {
	fprintf(stderr, "vorbis_decode: %d channel, %ldHz, encoder `%s'\n",
		context->vi.channels, context->vi.rate, context->vc.vendor);

	avccontext->channels = context->vi.channels ;
	avccontext->sample_rate = context->vi.rate ;
	
	vorbis_synthesis_init(&context->vd, &context->vi) ;
	vorbis_block_init(&context->vd, &context->vb); 
    }

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
} ;
