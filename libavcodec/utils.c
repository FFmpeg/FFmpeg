/*
 * utils for libavcodec
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * @file utils.c
 * utils.
 */

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "integer.h"
#include "opt.h"
#include "crc.h"
#include <stdarg.h>
#include <limits.h>
#include <float.h>
#ifdef __MINGW32__
#include <fcntl.h>
#endif

const uint8_t ff_reverse[256]={
0x00,0x80,0x40,0xC0,0x20,0xA0,0x60,0xE0,0x10,0x90,0x50,0xD0,0x30,0xB0,0x70,0xF0,
0x08,0x88,0x48,0xC8,0x28,0xA8,0x68,0xE8,0x18,0x98,0x58,0xD8,0x38,0xB8,0x78,0xF8,
0x04,0x84,0x44,0xC4,0x24,0xA4,0x64,0xE4,0x14,0x94,0x54,0xD4,0x34,0xB4,0x74,0xF4,
0x0C,0x8C,0x4C,0xCC,0x2C,0xAC,0x6C,0xEC,0x1C,0x9C,0x5C,0xDC,0x3C,0xBC,0x7C,0xFC,
0x02,0x82,0x42,0xC2,0x22,0xA2,0x62,0xE2,0x12,0x92,0x52,0xD2,0x32,0xB2,0x72,0xF2,
0x0A,0x8A,0x4A,0xCA,0x2A,0xAA,0x6A,0xEA,0x1A,0x9A,0x5A,0xDA,0x3A,0xBA,0x7A,0xFA,
0x06,0x86,0x46,0xC6,0x26,0xA6,0x66,0xE6,0x16,0x96,0x56,0xD6,0x36,0xB6,0x76,0xF6,
0x0E,0x8E,0x4E,0xCE,0x2E,0xAE,0x6E,0xEE,0x1E,0x9E,0x5E,0xDE,0x3E,0xBE,0x7E,0xFE,
0x01,0x81,0x41,0xC1,0x21,0xA1,0x61,0xE1,0x11,0x91,0x51,0xD1,0x31,0xB1,0x71,0xF1,
0x09,0x89,0x49,0xC9,0x29,0xA9,0x69,0xE9,0x19,0x99,0x59,0xD9,0x39,0xB9,0x79,0xF9,
0x05,0x85,0x45,0xC5,0x25,0xA5,0x65,0xE5,0x15,0x95,0x55,0xD5,0x35,0xB5,0x75,0xF5,
0x0D,0x8D,0x4D,0xCD,0x2D,0xAD,0x6D,0xED,0x1D,0x9D,0x5D,0xDD,0x3D,0xBD,0x7D,0xFD,
0x03,0x83,0x43,0xC3,0x23,0xA3,0x63,0xE3,0x13,0x93,0x53,0xD3,0x33,0xB3,0x73,0xF3,
0x0B,0x8B,0x4B,0xCB,0x2B,0xAB,0x6B,0xEB,0x1B,0x9B,0x5B,0xDB,0x3B,0xBB,0x7B,0xFB,
0x07,0x87,0x47,0xC7,0x27,0xA7,0x67,0xE7,0x17,0x97,0x57,0xD7,0x37,0xB7,0x77,0xF7,
0x0F,0x8F,0x4F,0xCF,0x2F,0xAF,0x6F,0xEF,0x1F,0x9F,0x5F,0xDF,0x3F,0xBF,0x7F,0xFF,
};

static int volatile entangled_thread_counter=0;

void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size)
{
    if(min_size < *size)
        return ptr;

    *size= FFMAX(17*min_size/16 + 32, min_size);

    return av_realloc(ptr, *size);
}

static unsigned int last_static = 0;
static unsigned int allocated_static = 0;
static void** array_static = NULL;

void *av_mallocz_static(unsigned int size)
{
    void *ptr = av_mallocz(size);

    if(ptr){
        array_static =av_fast_realloc(array_static, &allocated_static, sizeof(void*)*(last_static+1));
        if(!array_static)
            return NULL;
        array_static[last_static++] = ptr;
    }

    return ptr;
}

void *ff_realloc_static(void *ptr, unsigned int size)
{
    int i;
    if(!ptr)
      return av_mallocz_static(size);
    /* Look for the old ptr */
    for(i = 0; i < last_static; i++) {
        if(array_static[i] == ptr) {
            array_static[i] = av_realloc(array_static[i], size);
            return array_static[i];
        }
    }
    return NULL;

}

void av_free_static(void)
{
    while(last_static){
        av_freep(&array_static[--last_static]);
    }
    av_freep(&array_static);
}

/**
 * Call av_free_static automatically before it's too late
 */

static void do_free(void) __attribute__ ((destructor));

static void do_free(void)
{
    av_free_static();
}


/* encoder management */
AVCodec *first_avcodec = NULL;

void register_avcodec(AVCodec *format)
{
    AVCodec **p;
    p = &first_avcodec;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}

void avcodec_set_dimensions(AVCodecContext *s, int width, int height){
    s->coded_width = width;
    s->coded_height= height;
    s->width = -((-width )>>s->lowres);
    s->height= -((-height)>>s->lowres);
}

typedef struct InternalBuffer{
    int last_pic_num;
    uint8_t *base[4];
    uint8_t *data[4];
    int linesize[4];
}InternalBuffer;

#define INTERNAL_BUFFER_SIZE 32

#define ALIGN(x, a) (((x)+(a)-1)&~((a)-1))

void avcodec_align_dimensions(AVCodecContext *s, int *width, int *height){
    int w_align= 1;
    int h_align= 1;

    switch(s->pix_fmt){
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUYV422:
    case PIX_FMT_UYVY422:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_GRAY8:
    case PIX_FMT_GRAY16BE:
    case PIX_FMT_GRAY16LE:
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUVJ444P:
        w_align= 16; //FIXME check for non mpeg style codecs and use less alignment
        h_align= 16;
        break;
    case PIX_FMT_YUV411P:
    case PIX_FMT_UYYVYY411:
        w_align=32;
        h_align=8;
        break;
    case PIX_FMT_YUV410P:
        if(s->codec_id == CODEC_ID_SVQ1){
            w_align=64;
            h_align=64;
        }
    case PIX_FMT_RGB555:
        if(s->codec_id == CODEC_ID_RPZA){
            w_align=4;
            h_align=4;
        }
    case PIX_FMT_PAL8:
        if(s->codec_id == CODEC_ID_SMC){
            w_align=4;
            h_align=4;
        }
        break;
    case PIX_FMT_BGR24:
        if((s->codec_id == CODEC_ID_MSZH) || (s->codec_id == CODEC_ID_ZLIB)){
            w_align=4;
            h_align=4;
        }
        break;
    default:
        w_align= 1;
        h_align= 1;
        break;
    }

    *width = ALIGN(*width , w_align);
    *height= ALIGN(*height, h_align);
}

int avcodec_check_dimensions(void *av_log_ctx, unsigned int w, unsigned int h){
    if((int)w>0 && (int)h>0 && (w+128)*(uint64_t)(h+128) < INT_MAX/4)
        return 0;

    av_log(av_log_ctx, AV_LOG_ERROR, "picture size invalid (%ux%u)\n", w, h);
    return -1;
}

int avcodec_default_get_buffer(AVCodecContext *s, AVFrame *pic){
    int i;
    int w= s->width;
    int h= s->height;
    InternalBuffer *buf;
    int *picture_number;

    if(pic->data[0]!=NULL) {
        av_log(s, AV_LOG_ERROR, "pic->data[0]!=NULL in avcodec_default_get_buffer\n");
        return -1;
    }
    if(s->internal_buffer_count >= INTERNAL_BUFFER_SIZE) {
        av_log(s, AV_LOG_ERROR, "internal_buffer_count overflow (missing release_buffer?)\n");
        return -1;
    }

    if(avcodec_check_dimensions(s,w,h))
        return -1;

    if(s->internal_buffer==NULL){
        s->internal_buffer= av_mallocz(INTERNAL_BUFFER_SIZE*sizeof(InternalBuffer));
    }
#if 0
    s->internal_buffer= av_fast_realloc(
        s->internal_buffer,
        &s->internal_buffer_size,
        sizeof(InternalBuffer)*FFMAX(99,  s->internal_buffer_count+1)/*FIXME*/
        );
#endif

    buf= &((InternalBuffer*)s->internal_buffer)[s->internal_buffer_count];
    picture_number= &(((InternalBuffer*)s->internal_buffer)[INTERNAL_BUFFER_SIZE-1]).last_pic_num; //FIXME ugly hack
    (*picture_number)++;

    if(buf->base[0]){
        pic->age= *picture_number - buf->last_pic_num;
        buf->last_pic_num= *picture_number;
    }else{
        int h_chroma_shift, v_chroma_shift;
        int pixel_size, size[3];
        AVPicture picture;

        avcodec_get_chroma_sub_sample(s->pix_fmt, &h_chroma_shift, &v_chroma_shift);

        avcodec_align_dimensions(s, &w, &h);

        if(!(s->flags&CODEC_FLAG_EMU_EDGE)){
            w+= EDGE_WIDTH*2;
            h+= EDGE_WIDTH*2;
        }
        avpicture_fill(&picture, NULL, s->pix_fmt, w, h);
        pixel_size= picture.linesize[0]*8 / w;
//av_log(NULL, AV_LOG_ERROR, "%d %d %d %d\n", (int)picture.data[1], w, h, s->pix_fmt);
        assert(pixel_size>=1);
            //FIXME next ensures that linesize= 2^x uvlinesize, thats needed because some MC code assumes it
        if(pixel_size == 3*8)
            w= ALIGN(w, STRIDE_ALIGN<<h_chroma_shift);
        else
            w= ALIGN(pixel_size*w, STRIDE_ALIGN<<(h_chroma_shift+3)) / pixel_size;
        size[1] = avpicture_fill(&picture, NULL, s->pix_fmt, w, h);
        size[0] = picture.linesize[0] * h;
        size[1] -= size[0];
        if(picture.data[2])
            size[1]= size[2]= size[1]/2;
        else
            size[2]= 0;

        buf->last_pic_num= -256*256*256*64;
        memset(buf->base, 0, sizeof(buf->base));
        memset(buf->data, 0, sizeof(buf->data));

        for(i=0; i<3 && size[i]; i++){
            const int h_shift= i==0 ? 0 : h_chroma_shift;
            const int v_shift= i==0 ? 0 : v_chroma_shift;

            buf->linesize[i]= picture.linesize[i];

            buf->base[i]= av_malloc(size[i]+16); //FIXME 16
            if(buf->base[i]==NULL) return -1;
            memset(buf->base[i], 128, size[i]);

            // no edge if EDEG EMU or not planar YUV, we check for PAL8 redundantly to protect against a exploitable bug regression ...
            if((s->flags&CODEC_FLAG_EMU_EDGE) || (s->pix_fmt == PIX_FMT_PAL8) || !size[2])
                buf->data[i] = buf->base[i];
            else
                buf->data[i] = buf->base[i] + ALIGN((buf->linesize[i]*EDGE_WIDTH>>v_shift) + (EDGE_WIDTH>>h_shift), STRIDE_ALIGN);
        }
        pic->age= 256*256*256*64;
    }
    pic->type= FF_BUFFER_TYPE_INTERNAL;

    for(i=0; i<4; i++){
        pic->base[i]= buf->base[i];
        pic->data[i]= buf->data[i];
        pic->linesize[i]= buf->linesize[i];
    }
    s->internal_buffer_count++;

    return 0;
}

void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic){
    int i;
    InternalBuffer *buf, *last;

    assert(pic->type==FF_BUFFER_TYPE_INTERNAL);
    assert(s->internal_buffer_count);

    buf = NULL; /* avoids warning */
    for(i=0; i<s->internal_buffer_count; i++){ //just 3-5 checks so is not worth to optimize
        buf= &((InternalBuffer*)s->internal_buffer)[i];
        if(buf->data[0] == pic->data[0])
            break;
    }
    assert(i < s->internal_buffer_count);
    s->internal_buffer_count--;
    last = &((InternalBuffer*)s->internal_buffer)[s->internal_buffer_count];

    FFSWAP(InternalBuffer, *buf, *last);

    for(i=0; i<3; i++){
        pic->data[i]=NULL;
//        pic->base[i]=NULL;
    }
//printf("R%X\n", pic->opaque);
}

int avcodec_default_reget_buffer(AVCodecContext *s, AVFrame *pic){
    AVFrame temp_pic;
    int i;

    /* If no picture return a new buffer */
    if(pic->data[0] == NULL) {
        /* We will copy from buffer, so must be readable */
        pic->buffer_hints |= FF_BUFFER_HINTS_READABLE;
        return s->get_buffer(s, pic);
    }

    /* If internal buffer type return the same buffer */
    if(pic->type == FF_BUFFER_TYPE_INTERNAL)
        return 0;

    /*
     * Not internal type and reget_buffer not overridden, emulate cr buffer
     */
    temp_pic = *pic;
    for(i = 0; i < 4; i++)
        pic->data[i] = pic->base[i] = NULL;
    pic->opaque = NULL;
    /* Allocate new frame */
    if (s->get_buffer(s, pic))
        return -1;
    /* Copy image data from old buffer to new buffer */
    av_picture_copy((AVPicture*)pic, (AVPicture*)&temp_pic, s->pix_fmt, s->width,
             s->height);
    s->release_buffer(s, &temp_pic); // Release old frame
    return 0;
}

int avcodec_default_execute(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2),void **arg, int *ret, int count){
    int i;

    for(i=0; i<count; i++){
        int r= func(c, arg[i]);
        if(ret) ret[i]= r;
    }
    return 0;
}

enum PixelFormat avcodec_default_get_format(struct AVCodecContext *s, const enum PixelFormat * fmt){
    return fmt[0];
}

static const char* context_to_name(void* ptr) {
    AVCodecContext *avc= ptr;

    if(avc && avc->codec && avc->codec->name)
        return avc->codec->name;
    else
        return "NULL";
}

#define OFFSET(x) offsetof(AVCodecContext,x)
#define DEFAULT 0 //should be NAN but it doesnt work as its not a constant in glibc as required by ANSI/ISO C
//these names are too long to be readable
#define V AV_OPT_FLAG_VIDEO_PARAM
#define A AV_OPT_FLAG_AUDIO_PARAM
#define S AV_OPT_FLAG_SUBTITLE_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM

#define AV_CODEC_DEFAULT_BITRATE 200*1000

static const AVOption options[]={
{"b", "set bitrate (in bits/s)", OFFSET(bit_rate), FF_OPT_TYPE_INT, AV_CODEC_DEFAULT_BITRATE, INT_MIN, INT_MAX, V|E},
{"ab", "set bitrate (in bits/s)", OFFSET(bit_rate), FF_OPT_TYPE_INT, 64*1000, INT_MIN, INT_MAX, A|E},
{"bt", "set video bitrate tolerance (in bits/s)", OFFSET(bit_rate_tolerance), FF_OPT_TYPE_INT, AV_CODEC_DEFAULT_BITRATE*20, 1, INT_MAX, V|E},
{"flags", NULL, OFFSET(flags), FF_OPT_TYPE_FLAGS, DEFAULT, INT_MIN, INT_MAX, V|A|E|D, "flags"},
{"mv4", "use four motion vector by macroblock (mpeg4)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_4MV, INT_MIN, INT_MAX, V|E, "flags"},
{"obmc", "use overlapped block motion compensation (h263+)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_OBMC, INT_MIN, INT_MAX, V|E, "flags"},
{"qpel", "use 1/4 pel motion compensation", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_QPEL, INT_MIN, INT_MAX, V|E, "flags"},
{"loop", "use loop filter", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_LOOP_FILTER, INT_MIN, INT_MAX, V|E, "flags"},
{"qscale", "use fixed qscale", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_QSCALE, INT_MIN, INT_MAX, 0, "flags"},
{"gmc", "use gmc", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_GMC, INT_MIN, INT_MAX, V|E, "flags"},
{"mv0", "always try a mb with mv=<0,0>", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_MV0, INT_MIN, INT_MAX, V|E, "flags"},
{"part", "use data partitioning", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_PART, INT_MIN, INT_MAX, V|E, "flags"},
{"input_preserved", NULL, 0, FF_OPT_TYPE_CONST, CODEC_FLAG_INPUT_PRESERVED, INT_MIN, INT_MAX, 0, "flags"},
{"pass1", "use internal 2pass ratecontrol in first  pass mode", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_PASS1, INT_MIN, INT_MAX, 0, "flags"},
{"pass2", "use internal 2pass ratecontrol in second pass mode", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_PASS2, INT_MIN, INT_MAX, 0, "flags"},
{"extern_huff", "use external huffman table (for mjpeg)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_EXTERN_HUFF, INT_MIN, INT_MAX, 0, "flags"},
{"gray", "only decode/encode grayscale", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_GRAY, INT_MIN, INT_MAX, V|E|D, "flags"},
{"emu_edge", "don't draw edges", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_EMU_EDGE, INT_MIN, INT_MAX, 0, "flags"},
{"psnr", "error[?] variables will be set during encoding", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_PSNR, INT_MIN, INT_MAX, V|E, "flags"},
{"truncated", NULL, 0, FF_OPT_TYPE_CONST, CODEC_FLAG_TRUNCATED, INT_MIN, INT_MAX, 0, "flags"},
{"naq", "normalize adaptive quantization", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_NORMALIZE_AQP, INT_MIN, INT_MAX, V|E, "flags"},
{"ildct", "use interlaced dct", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_INTERLACED_DCT, INT_MIN, INT_MAX, V|E, "flags"},
{"low_delay", "force low delay", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_LOW_DELAY, INT_MIN, INT_MAX, V|D|E, "flags"},
{"alt", "enable alternate scantable (mpeg2/mpeg4)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_ALT_SCAN, INT_MIN, INT_MAX, V|E, "flags"},
{"trell", "use trellis quantization", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_TRELLIS_QUANT, INT_MIN, INT_MAX, V|E, "flags"},
{"global_header", "place global headers in extradata instead of every keyframe", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_GLOBAL_HEADER, INT_MIN, INT_MAX, 0, "flags"},
{"bitexact", "use only bitexact stuff (except (i)dct)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_BITEXACT, INT_MIN, INT_MAX, A|V|S|D|E, "flags"},
{"aic", "h263 advanced intra coding / mpeg4 ac prediction", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_AC_PRED, INT_MIN, INT_MAX, V|E, "flags"},
{"umv", "use unlimited motion vectors", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_H263P_UMV, INT_MIN, INT_MAX, V|E, "flags"},
{"cbp", "use rate distortion optimization for cbp", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_CBP_RD, INT_MIN, INT_MAX, V|E, "flags"},
{"qprd", "use rate distortion optimization for qp selection", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_QP_RD, INT_MIN, INT_MAX, V|E, "flags"},
{"aiv", "h263 alternative inter vlc", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_H263P_AIV, INT_MIN, INT_MAX, V|E, "flags"},
{"slice", NULL, 0, FF_OPT_TYPE_CONST, CODEC_FLAG_H263P_SLICE_STRUCT, INT_MIN, INT_MAX, V|E, "flags"},
{"ilme", "interlaced motion estimation", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_INTERLACED_ME, INT_MIN, INT_MAX, V|E, "flags"},
{"scan_offset", "will reserve space for svcd scan offset user data", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_SVCD_SCAN_OFFSET, INT_MIN, INT_MAX, V|E, "flags"},
{"cgop", "closed gop", 0, FF_OPT_TYPE_CONST, CODEC_FLAG_CLOSED_GOP, INT_MIN, INT_MAX, V|E, "flags"},
{"fast", "allow non spec compliant speedup tricks", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_FAST, INT_MIN, INT_MAX, V|E, "flags2"},
{"sgop", "strictly enforce gop size", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_STRICT_GOP, INT_MIN, INT_MAX, V|E, "flags2"},
{"noout", "skip bitstream encoding", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_NO_OUTPUT, INT_MIN, INT_MAX, V|E, "flags2"},
{"local_header", "place global headers at every keyframe instead of in extradata", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_LOCAL_HEADER, INT_MIN, INT_MAX, V|E, "flags2"},
{"sub_id", NULL, OFFSET(sub_id), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"me_method", "set motion estimation method", OFFSET(me_method), FF_OPT_TYPE_INT, ME_EPZS, INT_MIN, INT_MAX, V|E, "me_method"},
{"extradata_size", NULL, OFFSET(extradata_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"time_base", NULL, OFFSET(time_base), FF_OPT_TYPE_RATIONAL, DEFAULT, INT_MIN, INT_MAX},
{"g", "set the group of picture size", OFFSET(gop_size), FF_OPT_TYPE_INT, 12, INT_MIN, INT_MAX, V|E},
{"rate_emu", "frame rate emulation", OFFSET(rate_emu), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"ar", "set audio sampling rate (in Hz)", OFFSET(sample_rate), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"ac", "set number of audio channels", OFFSET(channels), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"cutoff", "set cutoff bandwidth", OFFSET(cutoff), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, A|E},
{"frame_size", NULL, OFFSET(frame_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, A|E},
{"frame_number", NULL, OFFSET(frame_number), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"real_pict_num", NULL, OFFSET(real_pict_num), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"delay", NULL, OFFSET(delay), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"qcomp", "video quantizer scale compression (VBR)", OFFSET(qcompress), FF_OPT_TYPE_FLOAT, 0.5, FLT_MIN, FLT_MAX, V|E},
{"qblur", "video quantizer scale blur (VBR)", OFFSET(qblur), FF_OPT_TYPE_FLOAT, 0.5, FLT_MIN, FLT_MAX, V|E},
{"qmin", "min video quantizer scale (VBR)", OFFSET(qmin), FF_OPT_TYPE_INT, 2, 1, 51, V|E},
{"qmax", "max video quantizer scale (VBR)", OFFSET(qmax), FF_OPT_TYPE_INT, 31, 1, 51, V|E},
{"qdiff", "max difference between the quantizer scale (VBR)", OFFSET(max_qdiff), FF_OPT_TYPE_INT, 3, INT_MIN, INT_MAX, V|E},
{"bf", "use 'frames' B frames", OFFSET(max_b_frames), FF_OPT_TYPE_INT, DEFAULT, 0, FF_MAX_B_FRAMES, V|E},
{"b_qfactor", "qp factor between p and b frames", OFFSET(b_quant_factor), FF_OPT_TYPE_FLOAT, 1.25, FLT_MIN, FLT_MAX, V|E},
{"rc_strategy", "ratecontrol method", OFFSET(rc_strategy), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"b_strategy", "strategy to choose between I/P/B-frames", OFFSET(b_frame_strategy), FF_OPT_TYPE_INT, 0, INT_MIN, INT_MAX, V|E},
{"hurry_up", NULL, OFFSET(hurry_up), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|D},
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
{"rtp_mode", NULL, OFFSET(rtp_mode), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
#endif
{"ps", "rtp payload size in bits", OFFSET(rtp_payload_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"mv_bits", NULL, OFFSET(mv_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"header_bits", NULL, OFFSET(header_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"i_tex_bits", NULL, OFFSET(i_tex_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"p_tex_bits", NULL, OFFSET(p_tex_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"i_count", NULL, OFFSET(i_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"p_count", NULL, OFFSET(p_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"skip_count", NULL, OFFSET(skip_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"misc_bits", NULL, OFFSET(misc_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"frame_bits", NULL, OFFSET(frame_bits), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"codec_tag", NULL, OFFSET(codec_tag), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"bug", "workaround not auto detected encoder bugs", OFFSET(workaround_bugs), FF_OPT_TYPE_FLAGS, FF_BUG_AUTODETECT, INT_MIN, INT_MAX, V|D, "bug"},
{"autodetect", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_AUTODETECT, INT_MIN, INT_MAX, V|D, "bug"},
{"old_msmpeg4", "some old lavc generated msmpeg4v3 files (no autodetection)", 0, FF_OPT_TYPE_CONST, FF_BUG_OLD_MSMPEG4, INT_MIN, INT_MAX, V|D, "bug"},
{"xvid_ilace", "Xvid interlacing bug (autodetected if fourcc==XVIX)", 0, FF_OPT_TYPE_CONST, FF_BUG_XVID_ILACE, INT_MIN, INT_MAX, V|D, "bug"},
{"ump4", "(autodetected if fourcc==UMP4)", 0, FF_OPT_TYPE_CONST, FF_BUG_UMP4, INT_MIN, INT_MAX, V|D, "bug"},
{"no_padding", "padding bug (autodetected)", 0, FF_OPT_TYPE_CONST, FF_BUG_NO_PADDING, INT_MIN, INT_MAX, V|D, "bug"},
{"amv", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_AMV, INT_MIN, INT_MAX, V|D, "bug"},
{"ac_vlc", "illegal vlc bug (autodetected per fourcc)", 0, FF_OPT_TYPE_CONST, FF_BUG_AC_VLC, INT_MIN, INT_MAX, V|D, "bug"},
{"qpel_chroma", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_QPEL_CHROMA, INT_MIN, INT_MAX, V|D, "bug"},
{"std_qpel", "old standard qpel (autodetected per fourcc/version)", 0, FF_OPT_TYPE_CONST, FF_BUG_STD_QPEL, INT_MIN, INT_MAX, V|D, "bug"},
{"qpel_chroma2", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_QPEL_CHROMA2, INT_MIN, INT_MAX, V|D, "bug"},
{"direct_blocksize", "direct-qpel-blocksize bug (autodetected per fourcc/version)", 0, FF_OPT_TYPE_CONST, FF_BUG_DIRECT_BLOCKSIZE, INT_MIN, INT_MAX, V|D, "bug"},
{"edge", "edge padding bug (autodetected per fourcc/version)", 0, FF_OPT_TYPE_CONST, FF_BUG_EDGE, INT_MIN, INT_MAX, V|D, "bug"},
{"hpel_chroma", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_HPEL_CHROMA, INT_MIN, INT_MAX, V|D, "bug"},
{"dc_clip", NULL, 0, FF_OPT_TYPE_CONST, FF_BUG_DC_CLIP, INT_MIN, INT_MAX, V|D, "bug"},
{"ms", "workaround various bugs in microsofts broken decoders", 0, FF_OPT_TYPE_CONST, FF_BUG_MS, INT_MIN, INT_MAX, V|D, "bug"},
{"lelim", "single coefficient elimination threshold for luminance (negative values also consider dc coefficient)", OFFSET(luma_elim_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"celim", "single coefficient elimination threshold for chrominance (negative values also consider dc coefficient)", OFFSET(chroma_elim_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"strict", "how strictly to follow the standards", OFFSET(strict_std_compliance), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, A|V|D, "strict"},
{"very", "strictly conform to a older more strict version of the spec or reference software", 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_VERY_STRICT, INT_MIN, INT_MAX, V|E, "strict"},
{"strict", "strictly conform to all the things in the spec no matter what consequences", 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_STRICT, INT_MIN, INT_MAX, V|E, "strict"},
{"normal", NULL, 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_NORMAL, INT_MIN, INT_MAX, V|E, "strict"},
{"inofficial", "allow inofficial extensions", 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_INOFFICIAL, INT_MIN, INT_MAX, V|E, "strict"},
{"experimental", "allow non standarized experimental things", 0, FF_OPT_TYPE_CONST, FF_COMPLIANCE_EXPERIMENTAL, INT_MIN, INT_MAX, V|E, "strict"},
{"b_qoffset", "qp offset between p and b frames", OFFSET(b_quant_offset), FF_OPT_TYPE_FLOAT, 1.25, FLT_MIN, FLT_MAX, V|E},
{"er", "set error resilience strategy", OFFSET(error_resilience), FF_OPT_TYPE_INT, FF_ER_CAREFUL, INT_MIN, INT_MAX, A|V|D, "er"},
{"careful", NULL, 0, FF_OPT_TYPE_CONST, FF_ER_CAREFUL, INT_MIN, INT_MAX, V|D, "er"},
{"compliant", NULL, 0, FF_OPT_TYPE_CONST, FF_ER_COMPLIANT, INT_MIN, INT_MAX, V|D, "er"},
{"aggressive", NULL, 0, FF_OPT_TYPE_CONST, FF_ER_AGGRESSIVE, INT_MIN, INT_MAX, V|D, "er"},
{"very_aggressive", NULL, 0, FF_OPT_TYPE_CONST, FF_ER_VERY_AGGRESSIVE, INT_MIN, INT_MAX, V|D, "er"},
{"has_b_frames", NULL, OFFSET(has_b_frames), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"block_align", NULL, OFFSET(block_align), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"parse_only", NULL, OFFSET(parse_only), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"mpeg_quant", "use MPEG quantizers instead of H.263", OFFSET(mpeg_quant), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"stats_out", NULL, OFFSET(stats_out), FF_OPT_TYPE_STRING, DEFAULT, CHAR_MIN, CHAR_MAX},
{"stats_in", NULL, OFFSET(stats_in), FF_OPT_TYPE_STRING, DEFAULT, CHAR_MIN, CHAR_MAX},
{"qsquish", "how to keep quantizer between qmin and qmax (0 = clip, 1 = use differentiable function)", OFFSET(rc_qsquish), FF_OPT_TYPE_FLOAT, DEFAULT, 0, 99, V|E},
{"rc_qmod_amp", "experimental quantizer modulation", OFFSET(rc_qmod_amp), FF_OPT_TYPE_FLOAT, DEFAULT, -FLT_MAX, FLT_MAX, V|E},
{"rc_qmod_freq", "experimental quantizer modulation", OFFSET(rc_qmod_freq), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"rc_override_count", NULL, OFFSET(rc_override_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"rc_eq", "set rate control equation", OFFSET(rc_eq), FF_OPT_TYPE_STRING, DEFAULT, CHAR_MIN, CHAR_MAX, V|E},
{"maxrate", "set max video bitrate tolerance (in bits/s)", OFFSET(rc_max_rate), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"minrate", "set min video bitrate tolerance (in bits/s)", OFFSET(rc_min_rate), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"bufsize", "set ratecontrol buffer size (in bits)", OFFSET(rc_buffer_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"rc_buf_aggressivity", "currently useless", OFFSET(rc_buffer_aggressivity), FF_OPT_TYPE_FLOAT, 1.0, FLT_MIN, FLT_MAX, V|E},
{"i_qfactor", "qp factor between p and i frames", OFFSET(i_quant_factor), FF_OPT_TYPE_FLOAT, -0.8, -FLT_MAX, FLT_MAX, V|E},
{"i_qoffset", "qp offset between p and i frames", OFFSET(i_quant_offset), FF_OPT_TYPE_FLOAT, 0.0, -FLT_MAX, FLT_MAX, V|E},
{"rc_init_cplx", "initial complexity for 1-pass encoding", OFFSET(rc_initial_cplx), FF_OPT_TYPE_FLOAT, DEFAULT, -FLT_MAX, FLT_MAX, V|E},
{"dct", "DCT algorithm", OFFSET(dct_algo), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, V|E, "dct"},
{"auto", "autoselect a good one (default)", 0, FF_OPT_TYPE_CONST, FF_DCT_AUTO, INT_MIN, INT_MAX, V|E, "dct"},
{"fastint", "fast integer", 0, FF_OPT_TYPE_CONST, FF_DCT_FASTINT, INT_MIN, INT_MAX, V|E, "dct"},
{"int", "accurate integer", 0, FF_OPT_TYPE_CONST, FF_DCT_INT, INT_MIN, INT_MAX, V|E, "dct"},
{"mmx", NULL, 0, FF_OPT_TYPE_CONST, FF_DCT_MMX, INT_MIN, INT_MAX, V|E, "dct"},
{"mlib", NULL, 0, FF_OPT_TYPE_CONST, FF_DCT_MLIB, INT_MIN, INT_MAX, V|E, "dct"},
{"altivec", NULL, 0, FF_OPT_TYPE_CONST, FF_DCT_ALTIVEC, INT_MIN, INT_MAX, V|E, "dct"},
{"faan", "floating point AAN DCT", 0, FF_OPT_TYPE_CONST, FF_DCT_FAAN, INT_MIN, INT_MAX, V|E, "dct"},
{"lumi_mask", "compresses bright areas stronger than medium ones", OFFSET(lumi_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"tcplx_mask", "temporal complexity masking", OFFSET(temporal_cplx_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"scplx_mask", "spatial complexity masking", OFFSET(spatial_cplx_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"p_mask", "inter masking", OFFSET(p_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"dark_mask", "compresses dark areas stronger than medium ones", OFFSET(dark_masking), FF_OPT_TYPE_FLOAT, 0, -FLT_MAX, FLT_MAX, V|E},
{"unused", NULL, OFFSET(unused), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"idct", "select IDCT implementation", OFFSET(idct_algo), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, V|E|D, "idct"},
{"auto", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_AUTO, INT_MIN, INT_MAX, V|E|D, "idct"},
{"int", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_INT, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simple", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLE, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplemmx", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLEMMX, INT_MIN, INT_MAX, V|E|D, "idct"},
{"libmpeg2mmx", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_LIBMPEG2MMX, INT_MIN, INT_MAX, V|E|D, "idct"},
{"ps2", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_PS2, INT_MIN, INT_MAX, V|E|D, "idct"},
{"mlib", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_MLIB, INT_MIN, INT_MAX, V|E|D, "idct"},
{"arm", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_ARM, INT_MIN, INT_MAX, V|E|D, "idct"},
{"altivec", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_ALTIVEC, INT_MIN, INT_MAX, V|E|D, "idct"},
{"sh4", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SH4, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplearm", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLEARM, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplearmv5te", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_SIMPLEARMV5TE, INT_MIN, INT_MAX, V|E|D, "idct"},
{"h264", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_H264, INT_MIN, INT_MAX, V|E|D, "idct"},
{"vp3", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_VP3, INT_MIN, INT_MAX, V|E|D, "idct"},
{"ipp", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_IPP, INT_MIN, INT_MAX, V|E|D, "idct"},
{"xvidmmx", NULL, 0, FF_OPT_TYPE_CONST, FF_IDCT_XVIDMMX, INT_MIN, INT_MAX, V|E|D, "idct"},
{"slice_count", NULL, OFFSET(slice_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"ec", "set error concealment strategy", OFFSET(error_concealment), FF_OPT_TYPE_FLAGS, 3, INT_MIN, INT_MAX, V|D, "ec"},
{"guess_mvs", "iterative motion vector (MV) search (slow)", 0, FF_OPT_TYPE_CONST, FF_EC_GUESS_MVS, INT_MIN, INT_MAX, V|D, "ec"},
{"deblock", "use strong deblock filter for damaged MBs", 0, FF_OPT_TYPE_CONST, FF_EC_DEBLOCK, INT_MIN, INT_MAX, V|D, "ec"},
{"bits_per_sample", NULL, OFFSET(bits_per_sample), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"pred", "prediction method", OFFSET(prediction_method), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "pred"},
{"left", NULL, 0, FF_OPT_TYPE_CONST, FF_PRED_LEFT, INT_MIN, INT_MAX, V|E, "pred"},
{"plane", NULL, 0, FF_OPT_TYPE_CONST, FF_PRED_PLANE, INT_MIN, INT_MAX, V|E, "pred"},
{"median", NULL, 0, FF_OPT_TYPE_CONST, FF_PRED_MEDIAN, INT_MIN, INT_MAX, V|E, "pred"},
{"aspect", "sample aspect ratio", OFFSET(sample_aspect_ratio), FF_OPT_TYPE_RATIONAL, DEFAULT, 0, 10, V|E},
{"debug", "print specific debug info", OFFSET(debug), FF_OPT_TYPE_FLAGS, DEFAULT, 0, INT_MAX, V|A|S|E|D, "debug"},
{"pict", "picture info", 0, FF_OPT_TYPE_CONST, FF_DEBUG_PICT_INFO, INT_MIN, INT_MAX, V|D, "debug"},
{"rc", "rate control", 0, FF_OPT_TYPE_CONST, FF_DEBUG_RC, INT_MIN, INT_MAX, V|E, "debug"},
{"bitstream", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_BITSTREAM, INT_MIN, INT_MAX, V|D, "debug"},
{"mb_type", "macroblock (MB) type", 0, FF_OPT_TYPE_CONST, FF_DEBUG_MB_TYPE, INT_MIN, INT_MAX, V|D, "debug"},
{"qp", "per-block quantization parameter (QP)", 0, FF_OPT_TYPE_CONST, FF_DEBUG_QP, INT_MIN, INT_MAX, V|D, "debug"},
{"mv", "motion vector", 0, FF_OPT_TYPE_CONST, FF_DEBUG_MV, INT_MIN, INT_MAX, V|D, "debug"},
{"dct_coeff", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_DCT_COEFF, INT_MIN, INT_MAX, V|D, "debug"},
{"skip", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_SKIP, INT_MIN, INT_MAX, V|D, "debug"},
{"startcode", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_STARTCODE, INT_MIN, INT_MAX, V|D, "debug"},
{"pts", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_PTS, INT_MIN, INT_MAX, V|D, "debug"},
{"er", "error resilience", 0, FF_OPT_TYPE_CONST, FF_DEBUG_ER, INT_MIN, INT_MAX, V|D, "debug"},
{"mmco", "memory management control operations (H.264)", 0, FF_OPT_TYPE_CONST, FF_DEBUG_MMCO, INT_MIN, INT_MAX, V|D, "debug"},
{"bugs", NULL, 0, FF_OPT_TYPE_CONST, FF_DEBUG_BUGS, INT_MIN, INT_MAX, V|D, "debug"},
{"vis_qp", "visualize quantization parameter (QP), lower QP are tinted greener", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_QP, INT_MIN, INT_MAX, V|D, "debug"},
{"vis_mb_type", "visualize block types", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_MB_TYPE, INT_MIN, INT_MAX, V|D, "debug"},
{"vismv", "visualize motion vectors (MVs)", OFFSET(debug_mv), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, V|D, "debug_mv"},
{"pf", "forward predicted MVs of P-frames", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_MV_P_FOR, INT_MIN, INT_MAX, V|D, "debug_mv"},
{"bf", "forward predicted MVs of B-frames", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_MV_B_FOR, INT_MIN, INT_MAX, V|D, "debug_mv"},
{"bb", "backward predicted MVs of B-frames", 0, FF_OPT_TYPE_CONST, FF_DEBUG_VIS_MV_B_BACK, INT_MIN, INT_MAX, V|D, "debug_mv"},
{"mb_qmin", "obsolete, use qmin", OFFSET(mb_qmin), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"mb_qmax", "obsolete, use qmax", OFFSET(mb_qmax), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"cmp", "full pel me compare function", OFFSET(me_cmp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"subcmp", "sub pel me compare function", OFFSET(me_sub_cmp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"mbcmp", "macroblock compare function", OFFSET(mb_cmp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"ildctcmp", "interlaced dct compare function", OFFSET(ildct_cmp), FF_OPT_TYPE_INT, FF_CMP_VSAD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"dia_size", "diamond type & size for motion estimation", OFFSET(dia_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"last_pred", "amount of motion predictors from the previous frame", OFFSET(last_predictor_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"preme", "pre motion estimation", OFFSET(pre_me), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"precmp", "pre motion estimation compare function", OFFSET(me_pre_cmp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"sad", "sum of absolute differences, fast (default)", 0, FF_OPT_TYPE_CONST, FF_CMP_SAD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"sse", "sum of squared errors", 0, FF_OPT_TYPE_CONST, FF_CMP_SSE, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"satd", "sum of absolute Hadamard transformed differences", 0, FF_OPT_TYPE_CONST, FF_CMP_SATD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"dct", "sum of absolute DCT transformed differences", 0, FF_OPT_TYPE_CONST, FF_CMP_DCT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"psnr", "sum of squared quantization errors (avoid, low quality)", 0, FF_OPT_TYPE_CONST, FF_CMP_PSNR, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"bit", "number of bits needed for the block", 0, FF_OPT_TYPE_CONST, FF_CMP_BIT, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"rd", "rate distortion optimal, slow", 0, FF_OPT_TYPE_CONST, FF_CMP_RD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"zero", "0", 0, FF_OPT_TYPE_CONST, FF_CMP_ZERO, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"vsad", "sum of absolute vertical differences", 0, FF_OPT_TYPE_CONST, FF_CMP_VSAD, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"vsse","sum of squared vertical differences", 0, FF_OPT_TYPE_CONST, FF_CMP_VSSE, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"nsse", "noise preserving sum of squared differences", 0, FF_OPT_TYPE_CONST, FF_CMP_NSSE, INT_MIN, INT_MAX, V|E, "cmp_func"},
#ifdef CONFIG_SNOW_ENCODER
{"w53", "5/3 wavelet, only used in snow", 0, FF_OPT_TYPE_CONST, FF_CMP_W53, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"w97", "9/7 wavelet, only used in snow", 0, FF_OPT_TYPE_CONST, FF_CMP_W97, INT_MIN, INT_MAX, V|E, "cmp_func"},
#endif
{"dctmax", NULL, 0, FF_OPT_TYPE_CONST, FF_CMP_DCTMAX, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"chroma", NULL, 0, FF_OPT_TYPE_CONST, FF_CMP_CHROMA, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"pre_dia_size", "diamond type & size for motion estimation pre-pass", OFFSET(pre_dia_size), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"subq", "sub pel motion estimation quality", OFFSET(me_subpel_quality), FF_OPT_TYPE_INT, 8, INT_MIN, INT_MAX, V|E},
{"dtg_active_format", NULL, OFFSET(dtg_active_format), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"me_range", "limit motion vectors range (1023 for DivX player)", OFFSET(me_range), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"ibias", "intra quant bias", OFFSET(intra_quant_bias), FF_OPT_TYPE_INT, FF_DEFAULT_QUANT_BIAS, INT_MIN, INT_MAX, V|E},
{"pbias", "inter quant bias", OFFSET(inter_quant_bias), FF_OPT_TYPE_INT, FF_DEFAULT_QUANT_BIAS, INT_MIN, INT_MAX, V|E},
{"color_table_id", NULL, OFFSET(color_table_id), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"internal_buffer_count", NULL, OFFSET(internal_buffer_count), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"global_quality", NULL, OFFSET(global_quality), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"coder", NULL, OFFSET(coder_type), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "coder"},
{"vlc", "variable length coder / huffman coder", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_VLC, INT_MIN, INT_MAX, V|E, "coder"},
{"ac", "arithmetic coder", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_AC, INT_MIN, INT_MAX, V|E, "coder"},
{"raw", "raw (no encoding)", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_RAW, INT_MIN, INT_MAX, V|E, "coder"},
{"rle", "run-length coder", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_RLE, INT_MIN, INT_MAX, V|E, "coder"},
{"deflate", "deflate-based coder", 0, FF_OPT_TYPE_CONST, FF_CODER_TYPE_DEFLATE, INT_MIN, INT_MAX, V|E, "coder"},
{"context", "context model", OFFSET(context_model), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"slice_flags", NULL, OFFSET(slice_flags), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"xvmc_acceleration", NULL, OFFSET(xvmc_acceleration), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"mbd", "macroblock decision algorithm (high quality mode)", OFFSET(mb_decision), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E, "mbd"},
{"simple", "use mbcmp (default)", 0, FF_OPT_TYPE_CONST, FF_MB_DECISION_SIMPLE, INT_MIN, INT_MAX, V|E, "mbd"},
{"bits", "use fewest bits", 0, FF_OPT_TYPE_CONST, FF_MB_DECISION_BITS, INT_MIN, INT_MAX, V|E, "mbd"},
{"rd", "use best rate distortion", 0, FF_OPT_TYPE_CONST, FF_MB_DECISION_RD, INT_MIN, INT_MAX, V|E, "mbd"},
{"stream_codec_tag", NULL, OFFSET(stream_codec_tag), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"sc_threshold", "scene change threshold", OFFSET(scenechange_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"lmin", "min lagrange factor (VBR)", OFFSET(lmin), FF_OPT_TYPE_INT,  2*FF_QP2LAMBDA, 0, INT_MAX, V|E},
{"lmax", "max lagrange factor (VBR)", OFFSET(lmax), FF_OPT_TYPE_INT, 31*FF_QP2LAMBDA, 0, INT_MAX, V|E},
{"nr", "noise reduction", OFFSET(noise_reduction), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"rc_init_occupancy", "number of bits which should be loaded into the rc buffer before decoding starts", OFFSET(rc_initial_buffer_occupancy), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"inter_threshold", NULL, OFFSET(inter_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"flags2", NULL, OFFSET(flags2), FF_OPT_TYPE_FLAGS, CODEC_FLAG2_FASTPSKIP, INT_MIN, INT_MAX, V|A|E|D, "flags2"},
{"error", NULL, OFFSET(error_rate), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"antialias", "MP3 antialias algorithm", OFFSET(antialias_algo), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|D, "aa"},
{"auto", NULL, 0, FF_OPT_TYPE_CONST, FF_AA_AUTO, INT_MIN, INT_MAX, V|D, "aa"},
{"fastint", NULL, 0, FF_OPT_TYPE_CONST, FF_AA_FASTINT, INT_MIN, INT_MAX, V|D, "aa"},
{"int", NULL, 0, FF_OPT_TYPE_CONST, FF_AA_INT, INT_MIN, INT_MAX, V|D, "aa"},
{"float", NULL, 0, FF_OPT_TYPE_CONST, FF_AA_FLOAT, INT_MIN, INT_MAX, V|D, "aa"},
{"qns", "quantizer noise shaping", OFFSET(quantizer_noise_shaping), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"threads", NULL, OFFSET(thread_count), FF_OPT_TYPE_INT, 1, INT_MIN, INT_MAX, V|E|D},
{"me_threshold", "motion estimaton threshold", OFFSET(me_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX},
{"mb_threshold", "macroblock threshold", OFFSET(mb_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"dc", "intra_dc_precision", OFFSET(intra_dc_precision), FF_OPT_TYPE_INT, 0, INT_MIN, INT_MAX, V|E},
{"nssew", "nsse weight", OFFSET(nsse_weight), FF_OPT_TYPE_INT, 8, INT_MIN, INT_MAX, V|E},
{"skip_top", "number of macroblock rows at the top which are skipped", OFFSET(skip_top), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|D},
{"skip_bottom", "number of macroblock rows at the bottom which are skipped", OFFSET(skip_bottom), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|D},
{"profile", NULL, OFFSET(profile), FF_OPT_TYPE_INT, FF_PROFILE_UNKNOWN, INT_MIN, INT_MAX, V|A|E, "profile"},
{"unknown", NULL, 0, FF_OPT_TYPE_CONST, FF_PROFILE_UNKNOWN, INT_MIN, INT_MAX, V|A|E, "profile"},
{"level", NULL, OFFSET(level), FF_OPT_TYPE_INT, FF_LEVEL_UNKNOWN, INT_MIN, INT_MAX, V|A|E, "level"},
{"unknown", NULL, 0, FF_OPT_TYPE_CONST, FF_LEVEL_UNKNOWN, INT_MIN, INT_MAX, V|A|E, "level"},
{"lowres", "decode at 1= 1/2, 2=1/4, 3=1/8 resolutions", OFFSET(lowres), FF_OPT_TYPE_INT, 0, 0, INT_MAX, V|D},
{"skip_threshold", "frame skip threshold", OFFSET(frame_skip_threshold), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"skip_factor", "frame skip factor", OFFSET(frame_skip_factor), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"skip_exp", "frame skip exponent", OFFSET(frame_skip_exp), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"skipcmp", "frame skip compare function", OFFSET(frame_skip_cmp), FF_OPT_TYPE_INT, FF_CMP_DCTMAX, INT_MIN, INT_MAX, V|E, "cmp_func"},
{"border_mask", "increases the quantizer for macroblocks close to borders", OFFSET(border_masking), FF_OPT_TYPE_FLOAT, DEFAULT, -FLT_MAX, FLT_MAX, V|E},
{"mblmin", "min macroblock lagrange factor (VBR)", OFFSET(mb_lmin), FF_OPT_TYPE_INT, FF_QP2LAMBDA * 2, 1, FF_LAMBDA_MAX, V|E},
{"mblmax", "max macroblock lagrange factor (VBR)", OFFSET(mb_lmax), FF_OPT_TYPE_INT, FF_QP2LAMBDA * 31, 1, FF_LAMBDA_MAX, V|E},
{"mepc", "motion estimation bitrate penalty compensation (1.0 = 256)", OFFSET(me_penalty_compensation), FF_OPT_TYPE_INT, 256, INT_MIN, INT_MAX, V|E},
{"bidir_refine", "refine the two motion vectors used in bidirectional macroblocks", OFFSET(bidir_refine), FF_OPT_TYPE_INT, DEFAULT, 0, 4, V|E},
{"brd_scale", "downscales frames for dynamic B-frame decision", OFFSET(brd_scale), FF_OPT_TYPE_INT, DEFAULT, 0, 10, V|E},
{"crf", "enables constant quality mode, and selects the quality (x264)", OFFSET(crf), FF_OPT_TYPE_FLOAT, DEFAULT, 0, 51, V|E},
{"cqp", "constant quantization parameter rate control method", OFFSET(cqp), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, V|E},
{"keyint_min", "minimum interval between IDR-frames (x264)", OFFSET(keyint_min), FF_OPT_TYPE_INT, 25, INT_MIN, INT_MAX, V|E},
{"refs", "reference frames to consider for motion compensation (Snow)", OFFSET(refs), FF_OPT_TYPE_INT, 1, INT_MIN, INT_MAX, V|E},
{"chromaoffset", "chroma qp offset from luma", OFFSET(chromaoffset), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"bframebias", "influences how often B-frames are used", OFFSET(bframebias), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|E},
{"trellis", "rate-distortion optimal quantization", OFFSET(trellis), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, V|A|E},
{"directpred", "direct mv prediction mode - 0 (none), 1 (spatial), 2 (temporal)", OFFSET(directpred), FF_OPT_TYPE_INT, 2, INT_MIN, INT_MAX, V|E},
{"bpyramid", "allows B-frames to be used as references for predicting", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_BPYRAMID, INT_MIN, INT_MAX, V|E, "flags2"},
{"wpred", "weighted biprediction for b-frames (H.264)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_WPRED, INT_MIN, INT_MAX, V|E, "flags2"},
{"mixed_refs", "one reference per partition, as opposed to one reference per macroblock", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_MIXED_REFS, INT_MIN, INT_MAX, V|E, "flags2"},
{"dct8x8", "high profile 8x8 transform (H.264)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_8X8DCT, INT_MIN, INT_MAX, V|E, "flags2"},
{"fastpskip", "fast pskip (H.264)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_FASTPSKIP, INT_MIN, INT_MAX, V|E, "flags2"},
{"aud", "access unit delimiters (H.264)", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_AUD, INT_MIN, INT_MAX, V|E, "flags2"},
{"brdo", "b-frame rate-distortion optimization", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_BRDO, INT_MIN, INT_MAX, V|E, "flags2"},
{"skiprd", "RD optimal MB level residual skiping", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_SKIP_RD, INT_MIN, INT_MAX, V|E, "flags2"},
{"complexityblur", "reduce fluctuations in qp (before curve compression)", OFFSET(complexityblur), FF_OPT_TYPE_FLOAT, 20.0, FLT_MIN, FLT_MAX, V|E},
{"deblockalpha", "in-loop deblocking filter alphac0 parameter", OFFSET(deblockalpha), FF_OPT_TYPE_INT, DEFAULT, -6, 6, V|E},
{"deblockbeta", "in-loop deblocking filter beta parameter", OFFSET(deblockbeta), FF_OPT_TYPE_INT, DEFAULT, -6, 6, V|E},
{"partitions", "macroblock subpartition sizes to consider", OFFSET(partitions), FF_OPT_TYPE_FLAGS, DEFAULT, INT_MIN, INT_MAX, V|E, "partitions"},
{"parti4x4", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_I4X4, INT_MIN, INT_MAX, V|E, "partitions"},
{"parti8x8", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_I8X8, INT_MIN, INT_MAX, V|E, "partitions"},
{"partp4x4", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_P4X4, INT_MIN, INT_MAX, V|E, "partitions"},
{"partp8x8", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_P8X8, INT_MIN, INT_MAX, V|E, "partitions"},
{"partb8x8", NULL, 0, FF_OPT_TYPE_CONST, X264_PART_B8X8, INT_MIN, INT_MAX, V|E, "partitions"},
{"sc_factor", "multiplied by qscale for each frame and added to scene_change_score", OFFSET(scenechange_factor), FF_OPT_TYPE_INT, 6, 0, INT_MAX, V|E},
{"mv0_threshold", NULL, OFFSET(mv0_threshold), FF_OPT_TYPE_INT, 256, 0, INT_MAX, V|E},
{"ivlc", "intra vlc table", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_INTRA_VLC, INT_MIN, INT_MAX, V|E, "flags2"},
{"b_sensitivity", "adjusts sensitivity of b_frame_strategy 1", OFFSET(b_sensitivity), FF_OPT_TYPE_INT, 40, 1, INT_MAX, V|E},
{"compression_level", NULL, OFFSET(compression_level), FF_OPT_TYPE_INT, FF_COMPRESSION_DEFAULT, INT_MIN, INT_MAX, V|A|E},
{"use_lpc", "sets whether to use LPC mode (FLAC)", OFFSET(use_lpc), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"lpc_coeff_precision", "LPC coefficient precision (FLAC)", OFFSET(lpc_coeff_precision), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, A|E},
{"min_prediction_order", NULL, OFFSET(min_prediction_order), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"max_prediction_order", NULL, OFFSET(max_prediction_order), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"prediction_order_method", "search method for selecting prediction order", OFFSET(prediction_order_method), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"min_partition_order", NULL, OFFSET(min_partition_order), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"max_partition_order", NULL, OFFSET(max_partition_order), FF_OPT_TYPE_INT, -1, INT_MIN, INT_MAX, A|E},
{"timecode_frame_start", "GOP timecode frame start number, in non drop frame format", OFFSET(timecode_frame_start), FF_OPT_TYPE_INT, 0, 0, INT_MAX, V|E},
{"drop_frame_timecode", NULL, 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_DROP_FRAME_TIMECODE, INT_MIN, INT_MAX, V|E, "flags2"},
{"non_linear_q", "use non linear quantizer", 0, FF_OPT_TYPE_CONST, CODEC_FLAG2_NON_LINEAR_QUANT, INT_MIN, INT_MAX, V|E, "flags2"},
{NULL},
};

#undef A
#undef V
#undef S
#undef E
#undef D
#undef DEFAULT

static AVClass av_codec_context_class = { "AVCodecContext", context_to_name, options };

void avcodec_get_context_defaults2(AVCodecContext *s, enum CodecType codec_type){
    int flags=0;
    memset(s, 0, sizeof(AVCodecContext));

    s->av_class= &av_codec_context_class;

    if(codec_type == CODEC_TYPE_AUDIO)
        flags= AV_OPT_FLAG_AUDIO_PARAM;
    else if(codec_type == CODEC_TYPE_VIDEO)
        flags= AV_OPT_FLAG_VIDEO_PARAM;
    else if(codec_type == CODEC_TYPE_SUBTITLE)
        flags= AV_OPT_FLAG_SUBTITLE_PARAM;
    av_opt_set_defaults2(s, flags, flags);

    s->rc_eq= "tex^qComp";
    s->time_base= (AVRational){0,1};
    s->get_buffer= avcodec_default_get_buffer;
    s->release_buffer= avcodec_default_release_buffer;
    s->get_format= avcodec_default_get_format;
    s->execute= avcodec_default_execute;
    s->sample_aspect_ratio= (AVRational){0,1};
    s->pix_fmt= PIX_FMT_NONE;
    s->sample_fmt= SAMPLE_FMT_S16; // FIXME: set to NONE

    s->palctrl = NULL;
    s->reget_buffer= avcodec_default_reget_buffer;
}

AVCodecContext *avcodec_alloc_context2(enum CodecType codec_type){
    AVCodecContext *avctx= av_malloc(sizeof(AVCodecContext));

    if(avctx==NULL) return NULL;

    avcodec_get_context_defaults2(avctx, codec_type);

    return avctx;
}

void avcodec_get_context_defaults(AVCodecContext *s){
    avcodec_get_context_defaults2(s, CODEC_TYPE_UNKNOWN);
}

AVCodecContext *avcodec_alloc_context(void){
    return avcodec_alloc_context2(CODEC_TYPE_UNKNOWN);
}

void avcodec_get_frame_defaults(AVFrame *pic){
    memset(pic, 0, sizeof(AVFrame));

    pic->pts= AV_NOPTS_VALUE;
    pic->key_frame= 1;
}

AVFrame *avcodec_alloc_frame(void){
    AVFrame *pic= av_malloc(sizeof(AVFrame));

    if(pic==NULL) return NULL;

    avcodec_get_frame_defaults(pic);

    return pic;
}

int avcodec_open(AVCodecContext *avctx, AVCodec *codec)
{
    int ret= -1;

    entangled_thread_counter++;
    if(entangled_thread_counter != 1){
        av_log(avctx, AV_LOG_ERROR, "insufficient thread locking around avcodec_open/close()\n");
        goto end;
    }

    if(avctx->codec)
        goto end;

    if (codec->priv_data_size > 0) {
        avctx->priv_data = av_mallocz(codec->priv_data_size);
        if (!avctx->priv_data)
            goto end;
    } else {
        avctx->priv_data = NULL;
    }

    if(avctx->coded_width && avctx->coded_height)
        avcodec_set_dimensions(avctx, avctx->coded_width, avctx->coded_height);
    else if(avctx->width && avctx->height)
        avcodec_set_dimensions(avctx, avctx->width, avctx->height);

    if((avctx->coded_width||avctx->coded_height) && avcodec_check_dimensions(avctx,avctx->coded_width,avctx->coded_height)){
        av_freep(&avctx->priv_data);
        goto end;
    }

    avctx->codec = codec;
    avctx->codec_id = codec->id;
    avctx->frame_number = 0;
    if(avctx->codec->init){
        ret = avctx->codec->init(avctx);
        if (ret < 0) {
            av_freep(&avctx->priv_data);
            avctx->codec= NULL;
            goto end;
        }
    }
    ret=0;
end:
    entangled_thread_counter--;
    return ret;
}

int avcodec_encode_audio(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                         const short *samples)
{
    if(buf_size < FF_MIN_BUFFER_SIZE && 0){
        av_log(avctx, AV_LOG_ERROR, "buffer smaller than minimum size\n");
        return -1;
    }
    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || samples){
        int ret = avctx->codec->encode(avctx, buf, buf_size, (void *)samples);
        avctx->frame_number++;
        return ret;
    }else
        return 0;
}

int avcodec_encode_video(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                         const AVFrame *pict)
{
    if(buf_size < FF_MIN_BUFFER_SIZE){
        av_log(avctx, AV_LOG_ERROR, "buffer smaller than minimum size\n");
        return -1;
    }
    if(avcodec_check_dimensions(avctx,avctx->width,avctx->height))
        return -1;
    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || pict){
        int ret = avctx->codec->encode(avctx, buf, buf_size, (void *)pict);
        avctx->frame_number++;
        emms_c(); //needed to avoid an emms_c() call before every return;

        return ret;
    }else
        return 0;
}

int avcodec_encode_subtitle(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                            const AVSubtitle *sub)
{
    int ret;
    ret = avctx->codec->encode(avctx, buf, buf_size, (void *)sub);
    avctx->frame_number++;
    return ret;
}

int avcodec_decode_video(AVCodecContext *avctx, AVFrame *picture,
                         int *got_picture_ptr,
                         uint8_t *buf, int buf_size)
{
    int ret;

    *got_picture_ptr= 0;
    if((avctx->coded_width||avctx->coded_height) && avcodec_check_dimensions(avctx,avctx->coded_width,avctx->coded_height))
        return -1;
    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || buf_size){
        ret = avctx->codec->decode(avctx, picture, got_picture_ptr,
                                buf, buf_size);

        emms_c(); //needed to avoid an emms_c() call before every return;

        if (*got_picture_ptr)
            avctx->frame_number++;
    }else
        ret= 0;

    return ret;
}

int avcodec_decode_audio2(AVCodecContext *avctx, int16_t *samples,
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size)
{
    int ret;

    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || buf_size){
        //FIXME remove the check below _after_ ensuring that all audio check that the available space is enough
        if(*frame_size_ptr < AVCODEC_MAX_AUDIO_FRAME_SIZE){
            av_log(avctx, AV_LOG_ERROR, "buffer smaller than AVCODEC_MAX_AUDIO_FRAME_SIZE\n");
            return -1;
        }
        if(*frame_size_ptr < FF_MIN_BUFFER_SIZE ||
        *frame_size_ptr < avctx->channels * avctx->frame_size * sizeof(int16_t) ||
        *frame_size_ptr < buf_size){
            av_log(avctx, AV_LOG_ERROR, "buffer %d too small\n", *frame_size_ptr);
            return -1;
        }

        ret = avctx->codec->decode(avctx, samples, frame_size_ptr,
                                buf, buf_size);
        avctx->frame_number++;
    }else{
        ret= 0;
        *frame_size_ptr=0;
    }
    return ret;
}

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
int avcodec_decode_audio(AVCodecContext *avctx, int16_t *samples,
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size){
    *frame_size_ptr= AVCODEC_MAX_AUDIO_FRAME_SIZE;
    return avcodec_decode_audio2(avctx, samples, frame_size_ptr, buf, buf_size);
}
#endif

int avcodec_decode_subtitle(AVCodecContext *avctx, AVSubtitle *sub,
                            int *got_sub_ptr,
                            const uint8_t *buf, int buf_size)
{
    int ret;

    *got_sub_ptr = 0;
    ret = avctx->codec->decode(avctx, sub, got_sub_ptr,
                               (uint8_t *)buf, buf_size);
    if (*got_sub_ptr)
        avctx->frame_number++;
    return ret;
}

int avcodec_close(AVCodecContext *avctx)
{
    entangled_thread_counter++;
    if(entangled_thread_counter != 1){
        av_log(avctx, AV_LOG_ERROR, "insufficient thread locking around avcodec_open/close()\n");
        entangled_thread_counter--;
        return -1;
    }

    if (avctx->codec->close)
        avctx->codec->close(avctx);
    avcodec_default_free_buffers(avctx);
    av_freep(&avctx->priv_data);
    avctx->codec = NULL;
    entangled_thread_counter--;
    return 0;
}

AVCodec *avcodec_find_encoder(enum CodecID id)
{
    AVCodec *p;
    p = first_avcodec;
    while (p) {
        if (p->encode != NULL && p->id == id)
            return p;
        p = p->next;
    }
    return NULL;
}

AVCodec *avcodec_find_encoder_by_name(const char *name)
{
    AVCodec *p;
    p = first_avcodec;
    while (p) {
        if (p->encode != NULL && strcmp(name,p->name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

AVCodec *avcodec_find_decoder(enum CodecID id)
{
    AVCodec *p;
    p = first_avcodec;
    while (p) {
        if (p->decode != NULL && p->id == id)
            return p;
        p = p->next;
    }
    return NULL;
}

AVCodec *avcodec_find_decoder_by_name(const char *name)
{
    AVCodec *p;
    p = first_avcodec;
    while (p) {
        if (p->decode != NULL && strcmp(name,p->name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode)
{
    const char *codec_name;
    AVCodec *p;
    char buf1[32];
    char channels_str[100];
    int bitrate;

    if (encode)
        p = avcodec_find_encoder(enc->codec_id);
    else
        p = avcodec_find_decoder(enc->codec_id);

    if (p) {
        codec_name = p->name;
        if (!encode && enc->codec_id == CODEC_ID_MP3) {
            if (enc->sub_id == 2)
                codec_name = "mp2";
            else if (enc->sub_id == 1)
                codec_name = "mp1";
        }
    } else if (enc->codec_id == CODEC_ID_MPEG2TS) {
        /* fake mpeg2 transport stream codec (currently not
           registered) */
        codec_name = "mpeg2ts";
    } else if (enc->codec_name[0] != '\0') {
        codec_name = enc->codec_name;
    } else {
        /* output avi tags */
        if(   isprint(enc->codec_tag&0xFF) && isprint((enc->codec_tag>>8)&0xFF)
           && isprint((enc->codec_tag>>16)&0xFF) && isprint((enc->codec_tag>>24)&0xFF)){
            snprintf(buf1, sizeof(buf1), "%c%c%c%c / 0x%04X",
                     enc->codec_tag & 0xff,
                     (enc->codec_tag >> 8) & 0xff,
                     (enc->codec_tag >> 16) & 0xff,
                     (enc->codec_tag >> 24) & 0xff,
                      enc->codec_tag);
        } else {
            snprintf(buf1, sizeof(buf1), "0x%04x", enc->codec_tag);
        }
        codec_name = buf1;
    }

    switch(enc->codec_type) {
    case CODEC_TYPE_VIDEO:
        snprintf(buf, buf_size,
                 "Video: %s%s",
                 codec_name, enc->mb_decision ? " (hq)" : "");
        if (enc->pix_fmt != PIX_FMT_NONE) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %s",
                     avcodec_get_pix_fmt_name(enc->pix_fmt));
        }
        if (enc->width) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %dx%d",
                     enc->width, enc->height);
            if(av_log_level >= AV_LOG_DEBUG){
                int g= ff_gcd(enc->time_base.num, enc->time_base.den);
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %d/%d",
                     enc->time_base.num/g, enc->time_base.den/g);
            }
        }
        if (encode) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", q=%d-%d", enc->qmin, enc->qmax);
        }
        bitrate = enc->bit_rate;
        break;
    case CODEC_TYPE_AUDIO:
        snprintf(buf, buf_size,
                 "Audio: %s",
                 codec_name);
        switch (enc->channels) {
            case 1:
                strcpy(channels_str, "mono");
                break;
            case 2:
                strcpy(channels_str, "stereo");
                break;
            case 6:
                strcpy(channels_str, "5:1");
                break;
            default:
                snprintf(channels_str, sizeof(channels_str), "%d channels", enc->channels);
                break;
        }
        if (enc->sample_rate) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %d Hz, %s",
                     enc->sample_rate,
                     channels_str);
        }

        /* for PCM codecs, compute bitrate directly */
        switch(enc->codec_id) {
        case CODEC_ID_PCM_S32LE:
        case CODEC_ID_PCM_S32BE:
        case CODEC_ID_PCM_U32LE:
        case CODEC_ID_PCM_U32BE:
            bitrate = enc->sample_rate * enc->channels * 32;
            break;
        case CODEC_ID_PCM_S24LE:
        case CODEC_ID_PCM_S24BE:
        case CODEC_ID_PCM_U24LE:
        case CODEC_ID_PCM_U24BE:
        case CODEC_ID_PCM_S24DAUD:
            bitrate = enc->sample_rate * enc->channels * 24;
            break;
        case CODEC_ID_PCM_S16LE:
        case CODEC_ID_PCM_S16BE:
        case CODEC_ID_PCM_U16LE:
        case CODEC_ID_PCM_U16BE:
            bitrate = enc->sample_rate * enc->channels * 16;
            break;
        case CODEC_ID_PCM_S8:
        case CODEC_ID_PCM_U8:
        case CODEC_ID_PCM_ALAW:
        case CODEC_ID_PCM_MULAW:
            bitrate = enc->sample_rate * enc->channels * 8;
            break;
        default:
            bitrate = enc->bit_rate;
            break;
        }
        break;
    case CODEC_TYPE_DATA:
        snprintf(buf, buf_size, "Data: %s", codec_name);
        bitrate = enc->bit_rate;
        break;
    case CODEC_TYPE_SUBTITLE:
        snprintf(buf, buf_size, "Subtitle: %s", codec_name);
        bitrate = enc->bit_rate;
        break;
    default:
        snprintf(buf, buf_size, "Invalid Codec type %d", enc->codec_type);
        return;
    }
    if (encode) {
        if (enc->flags & CODEC_FLAG_PASS1)
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", pass 1");
        if (enc->flags & CODEC_FLAG_PASS2)
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", pass 2");
    }
    if (bitrate != 0) {
        snprintf(buf + strlen(buf), buf_size - strlen(buf),
                 ", %d kb/s", bitrate / 1000);
    }
}

unsigned avcodec_version( void )
{
  return LIBAVCODEC_VERSION_INT;
}

unsigned avcodec_build( void )
{
  return LIBAVCODEC_BUILD;
}

static void init_crcs(void){
#if LIBAVUTIL_VERSION_INT  < (50<<16)
    av_crc04C11DB7= av_mallocz_static(sizeof(AVCRC) * 257);
    av_crc8005    = av_mallocz_static(sizeof(AVCRC) * 257);
    av_crc07      = av_mallocz_static(sizeof(AVCRC) * 257);
#endif
    av_crc_init(av_crc04C11DB7, 0, 32, 0x04c11db7, sizeof(AVCRC)*257);
    av_crc_init(av_crc8005    , 0, 16, 0x8005    , sizeof(AVCRC)*257);
    av_crc_init(av_crc07      , 0,  8, 0x07      , sizeof(AVCRC)*257);
}

void avcodec_init(void)
{
    static int inited = 0;

    if (inited != 0)
        return;
    inited = 1;

    dsputil_static_init();
    init_crcs();
}

void avcodec_flush_buffers(AVCodecContext *avctx)
{
    if(avctx->codec->flush)
        avctx->codec->flush(avctx);
}

void avcodec_default_free_buffers(AVCodecContext *s){
    int i, j;

    if(s->internal_buffer==NULL) return;

    for(i=0; i<INTERNAL_BUFFER_SIZE; i++){
        InternalBuffer *buf= &((InternalBuffer*)s->internal_buffer)[i];
        for(j=0; j<4; j++){
            av_freep(&buf->base[j]);
            buf->data[j]= NULL;
        }
    }
    av_freep(&s->internal_buffer);

    s->internal_buffer_count=0;
}

char av_get_pict_type_char(int pict_type){
    switch(pict_type){
    case I_TYPE: return 'I';
    case P_TYPE: return 'P';
    case B_TYPE: return 'B';
    case S_TYPE: return 'S';
    case SI_TYPE:return 'i';
    case SP_TYPE:return 'p';
    default:     return '?';
    }
}

int av_get_bits_per_sample(enum CodecID codec_id){
    switch(codec_id){
    case CODEC_ID_ADPCM_SBPRO_2:
        return 2;
    case CODEC_ID_ADPCM_SBPRO_3:
        return 3;
    case CODEC_ID_ADPCM_SBPRO_4:
    case CODEC_ID_ADPCM_CT:
        return 4;
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_U8:
        return 8;
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_U16LE:
        return 16;
    case CODEC_ID_PCM_S24DAUD:
    case CODEC_ID_PCM_S24BE:
    case CODEC_ID_PCM_S24LE:
    case CODEC_ID_PCM_U24BE:
    case CODEC_ID_PCM_U24LE:
        return 24;
    case CODEC_ID_PCM_S32BE:
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_U32BE:
    case CODEC_ID_PCM_U32LE:
        return 32;
    default:
        return 0;
    }
}

#if !defined(HAVE_THREADS)
int avcodec_thread_init(AVCodecContext *s, int thread_count){
    return -1;
}
#endif

unsigned int av_xiphlacing(unsigned char *s, unsigned int v)
{
    unsigned int n = 0;

    while(v >= 0xff) {
        *s++ = 0xff;
        v -= 0xff;
        n++;
    }
    *s = v;
    n++;
    return n;
}

/* Wrapper to work around the lack of mkstemp() on mingw/cygin.
 * Also, tries to create file in /tmp first, if possible.
 * *prefix can be a character constant; *filename will be allocated internally.
 * Returns file descriptor of opened file (or -1 on error)
 * and opened file name in **filename. */
int av_tempfile(char *prefix, char **filename) {
    int fd=-1;
#ifdef __MINGW32__
    *filename = tempnam(".", prefix);
#else
    size_t len = strlen(prefix) + 12; /* room for "/tmp/" and "XXXXXX\0" */
    *filename = av_malloc(len);
#endif
    /* -----common section-----*/
    if (*filename == NULL) {
        av_log(NULL, AV_LOG_ERROR, "ff_tempfile: Cannot allocate file name\n");
        return -1;
    }
#ifdef __MINGW32__
    fd = open(*filename, _O_RDWR | _O_BINARY | _O_CREAT, 0444);
#else
    snprintf(*filename, len, "/tmp/%sXXXXXX", prefix);
    fd = mkstemp(*filename);
    if (fd < 0) {
        snprintf(*filename, len, "./%sXXXXXX", prefix);
        fd = mkstemp(*filename);
    }
#endif
    /* -----common section-----*/
    if (fd < 0) {
        av_log(NULL, AV_LOG_ERROR, "ff_tempfile: Cannot open temporary file %s\n", *filename);
        return -1;
    }
    return fd; /* success */
}
