/*
 * Ogg bitstream support
 * Mark Hills <mark@pogo.org.uk>
 *
 * Uses libogg, but requires libvorbisenc to construct correct headers
 * when containing Vorbis stream -- currently the only format supported
 */

#include <stdio.h>

#include <ogg/ogg.h>

#include "avformat.h"

#undef NDEBUG
#include <assert.h>

#define DECODER_BUFFER_SIZE 4096


typedef struct OggContext {
    /* output */
    ogg_stream_state os ;
    int header_handled ;
    ogg_packet op;

    /* input */
    ogg_sync_state oy ;
} OggContext ;


#ifdef CONFIG_ENCODERS
static int ogg_write_header(AVFormatContext *avfcontext) 
{
    OggContext *context = avfcontext->priv_data;
    ogg_packet *op= &context->op;    
    int n, i;

    ogg_stream_init(&context->os, 31415);
    
    for(n = 0 ; n < avfcontext->nb_streams ; n++) {
        AVCodecContext *codec = &avfcontext->streams[n]->codec;
        uint8_t *p= codec->extradata;
        
        av_set_pts_info(avfcontext->streams[n], 60, 1, AV_TIME_BASE);

        for(i=0; i < codec->extradata_size; i+= op->bytes){
            op->bytes = p[i++]<<8;
            op->bytes+= p[i++];

            op->packet= &p[i];
            op->b_o_s= op->packetno==0;

            ogg_stream_packetin(&context->os, op);

            op->packetno++; //FIXME multiple streams
        }

	context->header_handled = 0 ;
    }
    
    return 0 ;
}

static int ogg_write_packet(AVFormatContext *avfcontext, AVPacket *pkt)
{
    OggContext *context = avfcontext->priv_data ;
    AVCodecContext *avctx= &avfcontext->streams[pkt->stream_index]->codec;
    ogg_packet *op= &context->op;
    ogg_page og ;
    int64_t pts;

    pts= av_rescale(pkt->pts, avctx->sample_rate, AV_TIME_BASE);

//    av_log(avfcontext, AV_LOG_DEBUG, "M%d\n", size);

    /* flush header packets so audio starts on a new page */

    if(!context->header_handled) {
	while(ogg_stream_flush(&context->os, &og)) {
	    put_buffer(&avfcontext->pb, og.header, og.header_len) ;
	    put_buffer(&avfcontext->pb, og.body, og.body_len) ;
	    put_flush_packet(&avfcontext->pb);
	}
	context->header_handled = 1 ;
    }

    op->packet = (uint8_t*) pkt->data;
    op->bytes  = pkt->size;
    op->b_o_s  = op->packetno == 0;
    op->granulepos= pts;

    /* correct the fields in the packet -- essential for streaming */
                                                        
    ogg_stream_packetin(&context->os, op);              
                                                        
    while(ogg_stream_pageout(&context->os, &og)) {
        put_buffer(&avfcontext->pb, og.header, og.header_len);
	put_buffer(&avfcontext->pb, og.body, og.body_len);     
	put_flush_packet(&avfcontext->pb);
    }                                                   
    op->packetno++;

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
    ogg_packet op ;    
    char *buf ;
    ogg_page og ;
    AVStream *ast ;
    AVCodecContext *codec;
    uint8_t *p;
    int i;
     
    ogg_sync_init(&context->oy) ;
    buf = ogg_sync_buffer(&context->oy, DECODER_BUFFER_SIZE) ;

    if(get_buffer(&avfcontext->pb, buf, DECODER_BUFFER_SIZE) <= 0)
	return AVERROR_IO ;
    
    ogg_sync_wrote(&context->oy, DECODER_BUFFER_SIZE) ;   
    ogg_sync_pageout(&context->oy, &og) ;
    ogg_stream_init(&context->os, ogg_page_serialno(&og)) ;
    ogg_stream_pagein(&context->os, &og) ;
    
    /* currently only one vorbis stream supported */

    ast = av_new_stream(avfcontext, 0) ;
    if(!ast)
	return AVERROR_NOMEM ;
    av_set_pts_info(ast, 60, 1, AV_TIME_BASE);

    codec= &ast->codec;
    codec->codec_type = CODEC_TYPE_AUDIO;
    codec->codec_id = CODEC_ID_VORBIS;
    for(i=0; i<3; i++){
        if(next_packet(avfcontext, &op)){
            return -1;
        }
        codec->extradata_size+= 2 + op.bytes;
        codec->extradata= av_realloc(codec->extradata, codec->extradata_size);
        p= codec->extradata + codec->extradata_size - 2 - op.bytes;
        *(p++)= op.bytes>>8;
        *(p++)= op.bytes&0xFF;
        memcpy(p, op.packet, op.bytes);
    }

    return 0 ;
}


static int ogg_read_packet(AVFormatContext *avfcontext, AVPacket *pkt) {
    ogg_packet op ;

    if(next_packet(avfcontext, &op)) 
	return AVERROR_IO ;
    if(av_new_packet(pkt, op.bytes) < 0)
	return AVERROR_IO ;
    pkt->stream_index = 0 ;
    memcpy(pkt->data, op.packet, op.bytes);
    if(avfcontext->streams[0]->codec.sample_rate && op.granulepos!=-1)
        pkt->pts= av_rescale(op.granulepos, AV_TIME_BASE, avfcontext->streams[0]->codec.sample_rate);
//    printf("%lld %d %d\n", pkt->pts, (int)op.granulepos, avfcontext->streams[0]->codec.sample_rate);

    return op.bytes;
}


static int ogg_read_close(AVFormatContext *avfcontext) {
    OggContext *context = avfcontext->priv_data ;

    ogg_stream_clear(&context->os) ;
    ogg_sync_clear(&context->oy) ;
    av_freep(&avfcontext->streams[0]->codec.extradata);

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
