/*
 * Ogg Vorbis codec support via libvorbisenc
 * Mark Hills <mark@pogo.org.uk>
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
} OggVorbisContext ;


int oggvorbis_init_encoder(vorbis_info *vi, AVCodecContext *avccontext) {
    if(avccontext->quality) { /* VBR requested */

	fprintf(stderr, "init_encode: channels=%d quality=%d\n", 
		avccontext->channels, avccontext->quality) ;

	return vorbis_encode_init_vbr(vi, avccontext->channels,
		  avccontext->sample_rate, (float)avccontext->quality / 1000) ;
    }

    fprintf(stderr, "init_encoder: channels=%d bitrate=%d tolerance=%d\n", 
	    avccontext->channels, avccontext->bit_rate,
	    avccontext->bit_rate_tolerance) ;

    return vorbis_encode_init(vi, avccontext->channels,
	          avccontext->sample_rate, -1, avccontext->bit_rate, -1) ;
}


static int oggvorbis_encode_init(AVCodecContext *avccontext) {
    OggVorbisContext *context = avccontext->priv_data ;

    fprintf(stderr, "oggvorbis_encode_init\n") ;

    vorbis_info_init(&context->vi) ;

    if(oggvorbis_init_encoder(&context->vi, avccontext) < 0) {
	fprintf(stderr, "oggvorbis_encode_init: init_encoder failed") ;
	return -1 ;
    }

    vorbis_analysis_init(&context->vd, &context->vi) ;
    vorbis_block_init(&context->vd, &context->vb) ;

    avccontext->frame_size = OGGVORBIS_FRAME_SIZE ;
    
    return 0 ;
}


int oggvorbis_encode_frame(AVCodecContext *avccontext, unsigned char *packets,
			   int buf_size, void *data)
{
    OggVorbisContext *context = avccontext->priv_data ;
    float **buffer ;
    ogg_packet op ;
    signed char *audio = data ;
    int l, samples = buf_size / 16 ; /* samples = OGGVORBIS_FRAME_SIZE */ ;

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


int oggvorbis_encode_close(AVCodecContext *avccontext) {
    OggVorbisContext *context = avccontext->priv_data ;
/*  ogg_packet op ; */
    
    fprintf(stderr, "oggvorbis_encode_close\n") ;
    
    vorbis_analysis_wrote(&context->vd, 0) ; /* notify vorbisenc this is EOF */

    /* We need to write all the remaining packets into the stream
     * on closing */
    
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
};


