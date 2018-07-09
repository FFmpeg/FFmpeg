/*
 * Copyright (c) 2013 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat/libavcodec demuxing and muxing API example.
 *
 * Remux streams from one container format to another.
 * @example remuxing.c
 */

#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>


/*
This function will format and write out the pts, and duration info for the packet
*/
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
    
    printf(//"%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           "pts_time:%s duration_time:%s stream_index:%s\n",
           //tag,
           // av_ts2str(pkt->pts),
           av_ts2timestr(pkt->pts, time_base),
           // av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           // av_ts2str(pkt->duration),
           av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index == 0 ? "Video":"Audio");
}

/*
Save frame as a jpeg
*/
static int save_frame_as_jpeg(AVCodecContext *pCodecCtx, AVFrame *pFrame, int FrameNo) {
    AVCodec *jpegCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!jpegCodec) {
        return -1;
    }
    
    AVCodecContext *jpegContext = avcodec_alloc_context3(jpegCodec);
    if (!jpegContext) {
        return -1;
    }
    
    jpegContext->pix_fmt = pCodecCtx->pix_fmt;
    jpegContext->height = pFrame->height;
    jpegContext->width = pFrame->width;
    
    jpegContext->pix_fmt = AV_PIX_FMT_YUVJ420P;
    //jpegContext->time_base = (AVRational){1, 25};
    jpegContext->time_base.num = pCodecCtx->time_base.num;
    jpegContext->time_base.den = pCodecCtx->time_base.den;
    
    // open jpeg codec
    if (avcodec_open2(jpegContext, jpegCodec, NULL) < 0) {
        return -1;
    }
    
    FILE *f;
    char JPEGFName[256];
    
    AVPacket packet = {.data = NULL, .size = 0};
    av_init_packet(&packet);
    
    int gotFrame;
    if (avcodec_encode_video2(jpegContext, &packet, pFrame, &gotFrame) < 0) {
        return -1;
    }
    
    sprintf(JPEGFName, "dumpframe-%06d.jpg", FrameNo);
    printf("Writing %s\n",JPEGFName);
    f = fopen(JPEGFName, "wb");
    fwrite(packet.data, 1, packet.size, f);
    fclose(f);
    
    av_free_packet(&packet);
    avcodec_close(jpegContext);
    
    return 0;
}

/*
 int WriteJPEG (AVCodecContext *pCodecCtx, AVFrame *pFrame, int FrameNo){
 AVCodecContext         *pOCodecCtx;
 AVCodec                *pOCodec;
 uint8_t                *Buffer;
 int                     BufSiz;
 int                     BufSizActual;
 int                     ImgFmt = PIX_FMT_YUVJ420P; //for the
 newer ffmpeg version, this int to pixelformat
 FILE                   *JPEGFile;
 char                    JPEGFName[256];
 
 BufSiz = avpicture_get_size (
 ImgFmt,pCodecCtx->width,pCodecCtx->height );
 
 Buffer = (uint8_t *)malloc ( BufSiz );
 if ( Buffer == NULL )
 return ( 0 );
 memset ( Buffer, 0, BufSiz );
 
 pOCodecCtx = avcodec_alloc_context ( );
 if ( !pOCodecCtx ) {
 free ( Buffer );
 return ( 0 );
 }
 
 pOCodecCtx->bit_rate      = pCodecCtx->bit_rate;
 pOCodecCtx->width         = pCodecCtx->width;
 pOCodecCtx->height        = pCodecCtx->height;
 pOCodecCtx->pix_fmt       = ImgFmt;
 pOCodecCtx->codec_id      = CODEC_ID_MJPEG;
 pOCodecCtx->codec_type    = CODEC_TYPE_VIDEO;
 pOCodecCtx->time_base.num = pCodecCtx->time_base.num;
 pOCodecCtx->time_base.den = pCodecCtx->time_base.den;
 
 pOCodec = avcodec_find_encoder ( pOCodecCtx->codec_id );
 if ( !pOCodec ) {
 free ( Buffer );
 return ( 0 );
 }
 if ( avcodec_open ( pOCodecCtx, pOCodec ) < 0 ) {
 free ( Buffer );
 return ( 0 );
 }
 
 pOCodecCtx->mb_lmin        = pOCodecCtx->lmin =
 pOCodecCtx->qmin * FF_QP2LAMBDA;
 pOCodecCtx->mb_lmax        = pOCodecCtx->lmax =
 pOCodecCtx->qmax * FF_QP2LAMBDA;
 pOCodecCtx->flags          = CODEC_FLAG_QSCALE;
 pOCodecCtx->global_quality = pOCodecCtx->qmin * FF_QP2LAMBDA;
 
 pFrame->pts     = 1;
 pFrame->quality = pOCodecCtx->global_quality;
 BufSizActual = avcodec_encode_video(
 pOCodecCtx,Buffer,BufSiz,pFrame );
 
 sprintf ( JPEGFName, "%06d.jpg", FrameNo );
 JPEGFile = fopen ( JPEGFName, "wb" );
 fwrite ( Buffer, 1, BufSizActual, JPEGFile );
 fclose ( JPEGFile );
 
 avcodec_close ( pOCodecCtx );
 free ( Buffer );
 return ( BufSizActual );
 }
 */

int main(int argc, char **argv)
{

    AVFormatContext *format_ctx = NULL;
    AVPacket pkt;
    const char *in_filename;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;
    
    if (argc < 3) {
        printf("usage: %s input output\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "The output format is guessed according to the file extension.\n"
               "\n", argv[0]);
        return 1;
    }
    
    in_filename  = argv[1];
    
    // init codecs
    av_register_all();
    
    //open input file, allocate context
    if ((ret = avformat_open_input(&format_ctx, in_filename, 0, 0)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", in_filename);
        goto end;
    }
    
    if ((ret = avformat_find_stream_info(format_ctx, 0)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }
    
    av_dump_format(format_ctx, 0, in_filename, 0);
    
    stream_mapping_size = format_ctx->nb_streams;
    stream_mapping = av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    
    AVCodec *pCodec = NULL;
    AVCodecParameters *in_codecpar = NULL;
    for (i = 0; i < format_ctx->nb_streams; i++) {
        AVStream *in_stream = format_ctx->streams[i];
        in_codecpar = in_stream->codecpar;
        
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }
        
        if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            pCodec = avcodec_find_decoder(in_codecpar->codec_id);
            stream_mapping[i] = stream_index++;
            break;
        }
    }
    
    if(!pCodec){
        printf("error, no pCodec\n");
        return -1;
    }
    
    // convert CodecParam to CodecCtx
    AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!pCodecCtx)
        return AVERROR(ENOMEM);
    
    ret = avcodec_parameters_to_context(pCodecCtx, in_codecpar);//ic->streams[stream_index]->codecpar);
    if (ret < 0)
        goto end;
    
    //open video decoder
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
    {
        printf("failed to open codec through avcodec_open2\n");
        return -1;
    }

    //2
    int gotaFrame=0;
    int FrameNo = 0;
    
    while (1)
    {
        AVStream *in_stream;
        
        ret = av_read_frame(format_ctx, &pkt);
        if (ret < 0)
            break;
        
        in_stream  = format_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }//end if
        
        pkt.stream_index = stream_mapping[pkt.stream_index];
        
        //write out pts info
        log_packet(format_ctx, &pkt, "in");
        
        //2
        if(pkt.stream_index==0) { // if video -- hardcoded for now
            
            AVFrame *pFrame = av_frame_alloc();
            
            // try decoding
            avcodec_decode_video2(pCodecCtx, pFrame, &gotaFrame, &pkt);
            
            if (gotaFrame) {  // decode success.
                
                // save frame as jpg
                save_frame_as_jpeg(pCodecCtx, pFrame, FrameNo++);
                
                // cleanup
                av_frame_unref(pFrame);
                av_frame_free(&pFrame);
                av_free_packet(&pkt);
            }
            
            //av_packet_unref(&pkt);
        }
    }
    
    //av_write_trailer(ofmt_ctx);
end:
    
    avformat_close_input(&format_ctx);
    
    av_freep(&stream_mapping);
    
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }
    
    return 0;
}
