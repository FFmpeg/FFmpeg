/**
 * @file apiexample.c
 * avcodec API use example.
 *
 * Note that this library only handles codecs (mpeg, mpeg4, etc...),
 * not file formats (avi, vob, etc...). See library 'libavformat' for the
 * format handling 
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

#include "avcodec.h"

#define INBUF_SIZE 4096

/*
 * Audio encoding example 
 */
void audio_encode_example(const char *filename)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int frame_size, i, j, out_size, outbuf_size;
    FILE *f;
    short *samples;
    float t, tincr;
    uint8_t *outbuf;

    printf("Audio encoding\n");

    /* find the MP2 encoder */
    codec = avcodec_find_encoder(CODEC_ID_MP2);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    c= avcodec_alloc_context();
    
    /* put sample parameters */
    c->bit_rate = 64000;
    c->sample_rate = 44100;
    c->channels = 2;

    /* open it */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }
    
    /* the codec gives us the frame size, in samples */
    frame_size = c->frame_size;
    samples = malloc(frame_size * 2 * c->channels);
    outbuf_size = 10000;
    outbuf = malloc(outbuf_size);

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "could not open %s\n", filename);
        exit(1);
    }
        
    /* encode a single tone sound */
    t = 0;
    tincr = 2 * M_PI * 440.0 / c->sample_rate;
    for(i=0;i<200;i++) {
        for(j=0;j<frame_size;j++) {
            samples[2*j] = (int)(sin(t) * 10000);
            samples[2*j+1] = samples[2*j];
            t += tincr;
        }
        /* encode the samples */
        out_size = avcodec_encode_audio(c, outbuf, outbuf_size, samples);
        fwrite(outbuf, 1, out_size, f);
    }
    fclose(f);
    free(outbuf);
    free(samples);

    avcodec_close(c);
    free(c);
}

/*
 * Audio decoding. 
 */
void audio_decode_example(const char *outfilename, const char *filename)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int out_size, size, len;
    FILE *f, *outfile;
    uint8_t *outbuf;
    uint8_t inbuf[INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE], *inbuf_ptr;

    printf("Audio decoding\n");
    
    /* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
    memset(inbuf + INBUF_SIZE, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    /* find the mpeg audio decoder */
    codec = avcodec_find_decoder(CODEC_ID_MP2);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    c= avcodec_alloc_context();

    /* open it */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }
    
    outbuf = malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "could not open %s\n", filename);
        exit(1);
    }
    outfile = fopen(outfilename, "wb");
    if (!outfile) {
        free(c);
        exit(1);
    }
        
    /* decode until eof */
    inbuf_ptr = inbuf;
    for(;;) {
        size = fread(inbuf, 1, INBUF_SIZE, f);
        if (size == 0)
            break;

        inbuf_ptr = inbuf;
        while (size > 0) {
            len = avcodec_decode_audio(c, (short *)outbuf, &out_size, 
                                       inbuf_ptr, size);
            if (len < 0) {
                fprintf(stderr, "Error while decoding\n");
                exit(1);
            }
            if (out_size > 0) {
                /* if a frame has been decoded, output it */
                fwrite(outbuf, 1, out_size, outfile);
            }
            size -= len;
            inbuf_ptr += len;
        }
    }

    fclose(outfile);
    fclose(f);
    free(outbuf);

    avcodec_close(c);
    free(c);
}

/*
 * Video encoding example 
 */
void video_encode_example(const char *filename)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int i, out_size, size, x, y, outbuf_size;
    FILE *f;
    AVFrame *picture;
    uint8_t *outbuf, *picture_buf;

    printf("Video encoding\n");

    /* find the mpeg1 video encoder */
    codec = avcodec_find_encoder(CODEC_ID_MPEG1VIDEO);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    c= avcodec_alloc_context();
    picture= avcodec_alloc_frame();
    
    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 352;  
    c->height = 288;
    /* frames per second */
    c->frame_rate = 25;  
    c->frame_rate_base= 1;
    c->gop_size = 10; /* emit one intra frame every ten frames */
    c->max_b_frames=1;

    /* open it */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }
    
    /* the codec gives us the frame size, in samples */

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "could not open %s\n", filename);
        exit(1);
    }
    
    /* alloc image and output buffer */
    outbuf_size = 100000;
    outbuf = malloc(outbuf_size);
    size = c->width * c->height;
    picture_buf = malloc((size * 3) / 2); /* size for YUV 420 */
    
    picture->data[0] = picture_buf;
    picture->data[1] = picture->data[0] + size;
    picture->data[2] = picture->data[1] + size / 4;
    picture->linesize[0] = c->width;
    picture->linesize[1] = c->width / 2;
    picture->linesize[2] = c->width / 2;

    /* encode 1 second of video */
    for(i=0;i<25;i++) {
        fflush(stdout);
        /* prepare a dummy image */
        /* Y */
        for(y=0;y<c->height;y++) {
            for(x=0;x<c->width;x++) {
                picture->data[0][y * picture->linesize[0] + x] = x + y + i * 3;
            }
        }

        /* Cb and Cr */
        for(y=0;y<c->height/2;y++) {
            for(x=0;x<c->width/2;x++) {
                picture->data[1][y * picture->linesize[1] + x] = 128 + y + i * 2;
                picture->data[2][y * picture->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        /* encode the image */
        out_size = avcodec_encode_video(c, outbuf, outbuf_size, picture);
        printf("encoding frame %3d (size=%5d)\n", i, out_size);
        fwrite(outbuf, 1, out_size, f);
    }

    /* get the delayed frames */
    for(; out_size; i++) {
        fflush(stdout);
        
        out_size = avcodec_encode_video(c, outbuf, outbuf_size, NULL);
        printf("write frame %3d (size=%5d)\n", i, out_size);
        fwrite(outbuf, 1, out_size, f);
    }

    /* add sequence end code to have a real mpeg file */
    outbuf[0] = 0x00;
    outbuf[1] = 0x00;
    outbuf[2] = 0x01;
    outbuf[3] = 0xb7;
    fwrite(outbuf, 1, 4, f);
    fclose(f);
    free(picture_buf);
    free(outbuf);

    avcodec_close(c);
    free(c);
    free(picture);
    printf("\n");
}

/*
 * Video decoding example 
 */

void pgm_save(unsigned char *buf,int wrap, int xsize,int ysize,char *filename) 
{
    FILE *f;
    int i;

    f=fopen(filename,"w");
    fprintf(f,"P5\n%d %d\n%d\n",xsize,ysize,255);
    for(i=0;i<ysize;i++)
        fwrite(buf + i * wrap,1,xsize,f);
    fclose(f);
}

void video_decode_example(const char *outfilename, const char *filename)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int frame, size, got_picture, len;
    FILE *f;
    AVFrame *picture;
    uint8_t inbuf[INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE], *inbuf_ptr;
    char buf[1024];

    /* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
    memset(inbuf + INBUF_SIZE, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    printf("Video decoding\n");

    /* find the mpeg1 video decoder */
    codec = avcodec_find_decoder(CODEC_ID_MPEG1VIDEO);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    c= avcodec_alloc_context();
    picture= avcodec_alloc_frame();

    if(codec->capabilities&CODEC_CAP_TRUNCATED)
        c->flags|= CODEC_FLAG_TRUNCATED; /* we dont send complete frames */

    /* for some codecs, such as msmpeg4 and mpeg4, width and height
       MUST be initialized there because these info are not available
       in the bitstream */

    /* open it */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }
    
    /* the codec gives us the frame size, in samples */

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "could not open %s\n", filename);
        exit(1);
    }
    
    frame = 0;
    for(;;) {
        size = fread(inbuf, 1, INBUF_SIZE, f);
        if (size == 0)
            break;

        /* NOTE1: some codecs are stream based (mpegvideo, mpegaudio)
           and this is the only method to use them because you cannot
           know the compressed data size before analysing it. 

           BUT some other codecs (msmpeg4, mpeg4) are inherently frame
           based, so you must call them with all the data for one
           frame exactly. You must also initialize 'width' and
           'height' before initializing them. */

        /* NOTE2: some codecs allow the raw parameters (frame size,
           sample rate) to be changed at any frame. We handle this, so
           you should also take care of it */

        /* here, we use a stream based decoder (mpeg1video), so we
           feed decoder and see if it could decode a frame */
        inbuf_ptr = inbuf;
        while (size > 0) {
            len = avcodec_decode_video(c, picture, &got_picture, 
                                       inbuf_ptr, size);
            if (len < 0) {
                fprintf(stderr, "Error while decoding frame %d\n", frame);
                exit(1);
            }
            if (got_picture) {
                printf("saving frame %3d\n", frame);
                fflush(stdout);

                /* the picture is allocated by the decoder. no need to
                   free it */
                snprintf(buf, sizeof(buf), outfilename, frame);
                pgm_save(picture->data[0], picture->linesize[0], 
                         c->width, c->height, buf);
                frame++;
            }
            size -= len;
            inbuf_ptr += len;
        }
    }

    /* some codecs, such as MPEG, transmit the I and P frame with a
       latency of one frame. You must do the following to have a
       chance to get the last frame of the video */
    len = avcodec_decode_video(c, picture, &got_picture, 
                               NULL, 0);
    if (got_picture) {
        printf("saving last frame %3d\n", frame);
        fflush(stdout);
        
        /* the picture is allocated by the decoder. no need to
           free it */
        snprintf(buf, sizeof(buf), outfilename, frame);
        pgm_save(picture->data[0], picture->linesize[0], 
                 c->width, c->height, buf);
        frame++;
    }
        
    fclose(f);

    avcodec_close(c);
    free(c);
    free(picture);
    printf("\n");
}

// simple example how the options could be used
int options_example(int argc, char* argv[])
{
    AVCodec* codec = avcodec_find_encoder_by_name((argc > 1) ? argv[2] : "mpeg4");
    const AVOption* c;
    AVCodecContext* avctx;
    char* def = av_malloc(5000);
    const char* col = "";
    int i = 0;

    if (!codec)
	return -1;
    c = codec->options;
    avctx = avcodec_alloc_context();
    *def = 0;

    if (c) {
	const AVOption *stack[FF_OPT_MAX_DEPTH];
	int depth = 0;
	for (;;) {
	    if (!c->name) {
		if (c->help) {
		    stack[depth++] = c;
		    c = (const AVOption*)c->help;
		} else {
		    if (depth == 0)
			break; // finished
		    c = stack[--depth];
                    c++;
		}
	    } else {
		int t = c->type & FF_OPT_TYPE_MASK;
		printf("Config   %s  %s\n",
		       t == FF_OPT_TYPE_BOOL ? "bool   " :
		       t == FF_OPT_TYPE_DOUBLE ? "double  " :
		       t == FF_OPT_TYPE_INT ? "integer" :
		       t == FF_OPT_TYPE_STRING ? "string " :
		       "unknown??", c->name);
		switch (t) {
		case FF_OPT_TYPE_BOOL:
		    i += sprintf(def + i, "%s%s=%s",
				 col, c->name,
				 c->defval != 0. ? "on" : "off");
		    break;
		case FF_OPT_TYPE_DOUBLE:
		    i += sprintf(def + i, "%s%s=%f",
				 col, c->name, c->defval);
		    break;
		case FF_OPT_TYPE_INT:
		    i += sprintf(def + i, "%s%s=%d",
				 col, c->name, (int) c->defval);
		    break;
		case FF_OPT_TYPE_STRING:
		    if (c->defstr) {
			char* d = av_strdup(c->defstr);
			char* f = strchr(d, ',');
			if (f)
                            *f = 0;
			i += sprintf(def + i, "%s%s=%s",
				     col, c->name, d);
                        av_free(d);
		    }
		    break;
		}
		col = ":";
		c++;
	    }
	}
    }
    printf("Default Options: %s\n", def);
    av_free(def);
    return 0;
}


int main(int argc, char **argv)
{
    const char *filename;

    /* must be called before using avcodec lib */
    avcodec_init();

    /* register all the codecs (you can also register only the codec
       you wish to have smaller code */
    avcodec_register_all();

#ifdef OPT_TEST
    options_example(argc, argv);
#else
    if (argc <= 1) {
        audio_encode_example("/tmp/test.mp2");
        audio_decode_example("/tmp/test.sw", "/tmp/test.mp2");

        video_encode_example("/tmp/test.mpg");
        filename = "/tmp/test.mpg";
    } else {
        filename = argv[1];
    }

    //    audio_decode_example("/tmp/test.sw", filename);
    video_decode_example("/tmp/test%d.pgm", filename);
#endif

    return 0;
}
