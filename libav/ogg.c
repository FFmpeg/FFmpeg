/*
 * Ogg bitstream support
 * Mark Hills <mark@pogo.org.uk>
 *
 * Uses libogg, but requires libvorbisenc to construct correct headers
 * when containing Vorbis stream -- currently the only format supported
 */

#include <stdio.h>
#include <time.h>

#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>

#include "avformat.h"
#include "oggvorbis.h"


typedef struct OggContext {
    ogg_stream_state os ;
    int header_written ;
    ogg_int64_t base_packet_no ;
    ogg_int64_t base_granule_pos ;
} OggContext ;


static int ogg_write_header(AVFormatContext *avfcontext) {
    OggContext *context ;
    AVCodecContext *avccontext ;
    vorbis_info vi ;
    vorbis_dsp_state vd ;
    vorbis_comment vc ;
    vorbis_block vb ;
    ogg_packet header, header_comm, header_code ; 
    int n ;
    
    fprintf(stderr, "ogg_write_header\n") ;
    
    if(!(context = malloc(sizeof(OggContext))))
	return -1 ;
    avfcontext->priv_data = context ;
    
    srand(time(NULL));
    ogg_stream_init(&context->os, rand());
    
    for(n = 0 ; n < avfcontext->nb_streams ; n++) {
	avccontext = &avfcontext->streams[n]->codec ;

	/* begin vorbis specific code */
		
	vorbis_info_init(&vi) ;

	/* code copied from libavcodec/oggvorbis.c */

	if(oggvorbis_init_encoder(&vi, avccontext) < 0) {
	    fprintf(stderr, "ogg_write_header: init_encoder failed") ;
	    return -1 ;
	}

	vorbis_analysis_init(&vd, &vi) ;
	vorbis_block_init(&vd, &vb) ;
	
	vorbis_comment_init(&vc) ;
	vorbis_comment_add_tag(&vc, "encoder", "ffmpeg") ;
	if(*avfcontext->title)
	    vorbis_comment_add_tag(&vc, "title", avfcontext->title) ;

	vorbis_analysis_headerout(&vd, &vc, &header,
				  &header_comm, &header_code) ;
	ogg_stream_packetin(&context->os, &header) ;
	ogg_stream_packetin(&context->os, &header_comm) ;
	ogg_stream_packetin(&context->os, &header_code) ;  
	
	vorbis_comment_clear(&vc) ;

	/* end of vorbis specific code */

	context->header_written = 0 ;
	context->base_packet_no = 0 ;
    }
    
    return 0 ;
}


static int ogg_write_packet(AVFormatContext *avfcontext,
			    int stream_index,
			    unsigned char *buf, int size, int force_pts)
{
    OggContext *context = avfcontext->priv_data ;
    ogg_packet *op ;
    ogg_page og ;
    int l = 0 ;
    
    /* flush header packets so audio starts on a new page */

    if(!context->header_written) {
	while(ogg_stream_flush(&context->os, &og)) {
	    put_buffer(&avfcontext->pb, og.header, og.header_len) ;
	    put_buffer(&avfcontext->pb, og.body, og.body_len) ;
	    put_flush_packet(&avfcontext->pb);
	}
	context->header_written = 1 ;
    }

    while(l < size) {
	op = (ogg_packet*)(buf + l) ;
	op->packet = buf + l + sizeof(ogg_packet) ; /* fix data pointer */

	if(!context->base_packet_no) { /* this is the first packet */
	    context->base_packet_no = op->packetno ; 
	    context->base_granule_pos = op->granulepos ;
	}

	/* correct the fields in the packet -- essential for streaming */

	op->packetno -= context->base_packet_no ;
	op->granulepos -= context->base_granule_pos ;

	ogg_stream_packetin(&context->os, op) ;
	l += sizeof(ogg_packet) + op->bytes ;

	while(ogg_stream_pageout(&context->os, &og)) {
	    put_buffer(&avfcontext->pb, og.header, og.header_len) ;
	    put_buffer(&avfcontext->pb, og.body, og.body_len) ;
	    put_flush_packet(&avfcontext->pb);
	}
    }

    return 0;
}


static int ogg_write_trailer(AVFormatContext *avfcontext) {
    OggContext *context = avfcontext->priv_data ;
    ogg_page og ;

    fprintf(stderr, "ogg_write_trailer\n") ;

    while(ogg_stream_flush(&context->os, &og)) {
	put_buffer(&avfcontext->pb, og.header, og.header_len) ;
	put_buffer(&avfcontext->pb, og.body, og.body_len) ;
	put_flush_packet(&avfcontext->pb);
    }

    ogg_stream_clear(&context->os) ;
    return 0 ;
}


static AVOutputFormat ogg_oformat = {
    "ogg",
    "Ogg Vorbis",
    "audio/x-vorbis",
    "ogg",
    sizeof(OggContext),
    CODEC_ID_VORBIS,
    0,
    ogg_write_header,
    ogg_write_packet,
    ogg_write_trailer,
};


int ogg_init(void) {
    av_register_output_format(&ogg_oformat) ;
    return 0 ;
}
