/*
 * Ogg bitstream support
 * Mark Hills <mark@pogo.org.uk>
 *
 * Uses libogg, but requires libvorbisenc to construct correct headers
 * when containing Vorbis stream -- currently the only format supported
 */

#include <stdio.h>

#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>

#include "avformat.h"
#include "oggvorbis.h"

#define DECODER_BUFFER_SIZE 4096


typedef struct OggContext {
    /* output */
    ogg_stream_state os ;
    int header_handled ;
    ogg_int64_t base_packet_no ;
    ogg_int64_t base_granule_pos ;

    /* input */
    ogg_sync_state oy ;
} OggContext ;


#ifdef CONFIG_ENCODERS
static int ogg_write_header(AVFormatContext *avfcontext) 
{
    OggContext *context = avfcontext->priv_data;
    AVCodecContext *avccontext ;
    vorbis_info vi ;
    vorbis_dsp_state vd ;
    vorbis_comment vc ;
    vorbis_block vb ;
    ogg_packet header, header_comm, header_code ; 
    int n ;
    
    ogg_stream_init(&context->os, 31415);
    
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
	vorbis_comment_add_tag(&vc, "encoder", LIBAVFORMAT_IDENT) ;
	if(*avfcontext->title)
	    vorbis_comment_add_tag(&vc, "title", avfcontext->title) ;

	vorbis_analysis_headerout(&vd, &vc, &header,
				  &header_comm, &header_code) ;
	ogg_stream_packetin(&context->os, &header) ;
	ogg_stream_packetin(&context->os, &header_comm) ;
	ogg_stream_packetin(&context->os, &header_code) ;  
	
	vorbis_block_clear(&vb) ;
	vorbis_dsp_clear(&vd) ;
	vorbis_info_clear(&vi) ;
	vorbis_comment_clear(&vc) ;
	
	/* end of vorbis specific code */

	context->header_handled = 0 ;
	context->base_packet_no = 0 ;
    }
    
    return 0 ;
}


static int ogg_write_packet(AVFormatContext *avfcontext,
			    int stream_index,
			    const uint8_t *buf, int size, int64_t force_pts)
{
    OggContext *context = avfcontext->priv_data ;
    ogg_packet *op ;
    ogg_page og ;
    int l = 0 ;
    
    /* flush header packets so audio starts on a new page */

    if(!context->header_handled) {
	while(ogg_stream_flush(&context->os, &og)) {
	    put_buffer(&avfcontext->pb, og.header, og.header_len) ;
	    put_buffer(&avfcontext->pb, og.body, og.body_len) ;
	    put_flush_packet(&avfcontext->pb);
	}
	context->header_handled = 1 ;
    }

    while(l < size) {
	op = (ogg_packet*)(buf + l) ;
	op->packet = (uint8_t*) buf + l + sizeof( ogg_packet) ; /* fix data pointer */

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
} ;
#endif //CONFIG_ENCODERS


static int next_packet(AVFormatContext *avfcontext, ogg_packet *op) {
    OggContext *context = avfcontext->priv_data ;
    ogg_page og ;
    char *buf ;

    while(ogg_stream_packetout(&context->os, op) != 1) {

	/* while no pages are available, read in more data to the sync */
	while(ogg_sync_pageout(&context->oy, &og) != 1) {
	    buf = ogg_sync_buffer(&context->oy, DECODER_BUFFER_SIZE) ;
	    if(get_buffer(&avfcontext->pb, buf, DECODER_BUFFER_SIZE) <= 0)
		return 1 ;
	    ogg_sync_wrote(&context->oy, DECODER_BUFFER_SIZE) ; 
	}	
	
	/* got a page. Feed it into the stream and get the packet */
	if(ogg_stream_pagein(&context->os, &og) != 0)
	    return 1 ;
    }

    return 0 ;
}


static int ogg_read_header(AVFormatContext *avfcontext, AVFormatParameters *ap)
{
    OggContext *context = avfcontext->priv_data;
    char *buf ;
    ogg_page og ;
    AVStream *ast ;
     
    avfcontext->ctx_flags |= AVFMTCTX_NOHEADER;
     
    ogg_sync_init(&context->oy) ;
    buf = ogg_sync_buffer(&context->oy, DECODER_BUFFER_SIZE) ;

    if(get_buffer(&avfcontext->pb, buf, DECODER_BUFFER_SIZE) <= 0)
	return -EIO ;
    
    ogg_sync_wrote(&context->oy, DECODER_BUFFER_SIZE) ;   
    ogg_sync_pageout(&context->oy, &og) ;
    ogg_stream_init(&context->os, ogg_page_serialno(&og)) ;
    ogg_stream_pagein(&context->os, &og) ;
  
    /* currently only one vorbis stream supported */

    ast = av_new_stream(avfcontext, 0) ;
    if(!ast)
	return AVERROR_NOMEM ;

    ast->codec.codec_type = CODEC_TYPE_AUDIO ;
    ast->codec.codec_id = CODEC_ID_VORBIS ;
    
    return 0 ;
}


static int ogg_read_packet(AVFormatContext *avfcontext, AVPacket *pkt) {
    ogg_packet op ;

    if(next_packet(avfcontext, &op)) 
	return -EIO ;
    if(av_new_packet(pkt, sizeof(ogg_packet) + op.bytes) < 0)
	return -EIO ;
    pkt->stream_index = 0 ;
    memcpy(pkt->data, &op, sizeof(ogg_packet)) ;
    memcpy(pkt->data + sizeof(ogg_packet), op.packet, op.bytes) ;

    return sizeof(ogg_packet) + op.bytes ;
}


static int ogg_read_close(AVFormatContext *avfcontext) {
    OggContext *context = avfcontext->priv_data ;

    ogg_stream_clear(&context->os) ;
    ogg_sync_clear(&context->oy) ;

    return 0 ;
}


static AVInputFormat ogg_iformat = {
    "ogg",
    "Ogg Vorbis",
    sizeof(OggContext),
    NULL,
    ogg_read_header,
    ogg_read_packet,
    ogg_read_close,
    .extensions = "ogg",
} ;


int ogg_init(void) {
#ifdef CONFIG_ENCODERS
    av_register_output_format(&ogg_oformat) ;
#endif
    av_register_input_format(&ogg_iformat);
    return 0 ;
}
