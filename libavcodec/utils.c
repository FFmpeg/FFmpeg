/*
 * utils for libavcodec
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2003 Michel Bardiaux for the av_log API
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
/**
 * @file utils.c
 * utils.
 */
 
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "integer.h"
#include <stdarg.h>
#include <limits.h>

const uint8_t ff_sqrt_tab[128]={
        0, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
        9, 9, 9, 9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,11,11,11,11,11,11,11
};

const uint8_t ff_log2_tab[256]={
        0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
        5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
        7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};

void avcodec_default_free_buffers(AVCodecContext *s);

void *av_mallocz(unsigned int size)
{
    void *ptr;
    
    ptr = av_malloc(size);
    if (!ptr)
        return NULL;
    memset(ptr, 0, size);
    return ptr;
}

char *av_strdup(const char *s)
{
    char *ptr;
    int len;
    len = strlen(s) + 1;
    ptr = av_malloc(len);
    if (!ptr)
        return NULL;
    memcpy(ptr, s, len);
    return ptr;
}

/**
 * realloc which does nothing if the block is large enough
 */
void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size)
{
    if(min_size < *size) 
        return ptr;
    
    *size= 17*min_size/16 + 32;

    return av_realloc(ptr, *size);
}


static unsigned int last_static = 0;
static unsigned int allocated_static = 0;
static void** array_static = NULL;

/**
 * allocation of static arrays - do not use for normal allocation.
 */
void *av_mallocz_static(unsigned int size)
{
    void *ptr = av_mallocz(size);

    if(ptr){ 
        array_static =av_fast_realloc(array_static, &allocated_static, sizeof(void*)*(last_static+1));
        array_static[last_static++] = ptr;
    }

    return ptr;
}

/**
 * same as above, but does realloc
 */

void *av_realloc_static(void *ptr, unsigned int size)
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

/**
 * free all static arrays and reset pointers to 0.
 */
void av_free_static(void)
{
    while(last_static){
        av_freep(&array_static[--last_static]);
    }
    av_freep(&array_static);
}

/**
 * Frees memory and sets the pointer to NULL.
 * @param arg pointer to the pointer which should be freed
 */
void av_freep(void *arg)
{
    void **ptr= (void**)arg;
    av_free(*ptr);
    *ptr = NULL;
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
    case PIX_FMT_YUV422:
    case PIX_FMT_UYVY422:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_GRAY8:
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUVJ444P:
        w_align= 16; //FIXME check for non mpeg style codecs and use less alignment
        h_align= 16;
        break;
    case PIX_FMT_YUV411P:
    case PIX_FMT_UYVY411:
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

int avcodec_default_get_buffer(AVCodecContext *s, AVFrame *pic){
    int i;
    int w= s->width;
    int h= s->height;
    InternalBuffer *buf;
    int *picture_number;
    
    assert(pic->data[0]==NULL);
    assert(INTERNAL_BUFFER_SIZE > s->internal_buffer_count);

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
        int pixel_size;
        
        avcodec_get_chroma_sub_sample(s->pix_fmt, &h_chroma_shift, &v_chroma_shift);
        
        switch(s->pix_fmt){
        case PIX_FMT_RGB555:
        case PIX_FMT_RGB565:
        case PIX_FMT_YUV422:
        case PIX_FMT_UYVY422:
            pixel_size=2;
            break;
        case PIX_FMT_RGB24:
        case PIX_FMT_BGR24:
            pixel_size=3;
            break;
        case PIX_FMT_RGBA32:
            pixel_size=4;
            break;
        default:
            pixel_size=1;
        }

        avcodec_align_dimensions(s, &w, &h);
            
        if(!(s->flags&CODEC_FLAG_EMU_EDGE)){
            w+= EDGE_WIDTH*2;
            h+= EDGE_WIDTH*2;
        }
        
        buf->last_pic_num= -256*256*256*64;

        for(i=0; i<3; i++){
            const int h_shift= i==0 ? 0 : h_chroma_shift;
            const int v_shift= i==0 ? 0 : v_chroma_shift;

            //FIXME next ensures that linesize= 2^x uvlinesize, thats needed because some MC code assumes it
            buf->linesize[i]= ALIGN(pixel_size*w>>h_shift, STRIDE_ALIGN<<(h_chroma_shift-h_shift)); 

            buf->base[i]= av_mallocz((buf->linesize[i]*h>>v_shift)+16); //FIXME 16
            if(buf->base[i]==NULL) return -1;
            memset(buf->base[i], 128, buf->linesize[i]*h>>v_shift);
        
            if(s->flags&CODEC_FLAG_EMU_EDGE)
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
    InternalBuffer *buf, *last, temp;

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

    temp= *buf;
    *buf= *last;
    *last= temp;

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
    img_copy((AVPicture*)pic, (AVPicture*)&temp_pic, s->pix_fmt, s->width,
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

static AVClass av_codec_context_class = { "AVCodecContext", context_to_name };

void avcodec_get_context_defaults(AVCodecContext *s){
    memset(s, 0, sizeof(AVCodecContext));

    s->av_class= &av_codec_context_class;
    s->bit_rate= 800*1000;
    s->bit_rate_tolerance= s->bit_rate*10;
    s->qmin= 2;
    s->qmax= 31;
    s->mb_qmin= 2;
    s->mb_qmax= 31;
    s->rc_eq= "tex^qComp";
    s->qcompress= 0.5;
    s->max_qdiff= 3;
    s->b_quant_factor=1.25;
    s->b_quant_offset=1.25;
    s->i_quant_factor=-0.8;
    s->i_quant_offset=0.0;
    s->error_concealment= 3;
    s->error_resilience= 1;
    s->workaround_bugs= FF_BUG_AUTODETECT;
    s->frame_rate_base= 1;
    s->frame_rate = 25;
    s->gop_size= 50;
    s->me_method= ME_EPZS;
    s->get_buffer= avcodec_default_get_buffer;
    s->release_buffer= avcodec_default_release_buffer;
    s->get_format= avcodec_default_get_format;
    s->execute= avcodec_default_execute;
    s->thread_count=1;
    s->me_subpel_quality=8;
    s->lmin= FF_QP2LAMBDA * s->qmin;
    s->lmax= FF_QP2LAMBDA * s->qmax;
    s->sample_aspect_ratio= (AVRational){0,1};
    s->ildct_cmp= FF_CMP_VSAD;
    s->profile= FF_PROFILE_UNKNOWN;
    s->level= FF_LEVEL_UNKNOWN;
    
    s->intra_quant_bias= FF_DEFAULT_QUANT_BIAS;
    s->inter_quant_bias= FF_DEFAULT_QUANT_BIAS;
    s->palctrl = NULL;
    s->reget_buffer= avcodec_default_reget_buffer;
}

/**
 * allocates a AVCodecContext and set it to defaults.
 * this can be deallocated by simply calling free() 
 */
AVCodecContext *avcodec_alloc_context(void){
    AVCodecContext *avctx= av_malloc(sizeof(AVCodecContext));
    
    if(avctx==NULL) return NULL;
    
    avcodec_get_context_defaults(avctx);
    
    return avctx;
}

void avcodec_get_frame_defaults(AVFrame *pic){
    memset(pic, 0, sizeof(AVFrame));

    pic->pts= AV_NOPTS_VALUE;
}

/**
 * allocates a AVPFrame and set it to defaults.
 * this can be deallocated by simply calling free() 
 */
AVFrame *avcodec_alloc_frame(void){
    AVFrame *pic= av_malloc(sizeof(AVFrame));
    
    if(pic==NULL) return NULL;
    
    avcodec_get_frame_defaults(pic);
    
    return pic;
}

int avcodec_open(AVCodecContext *avctx, AVCodec *codec)
{
    int ret;

    if(avctx->codec)
        return -1;

    avctx->codec = codec;
    avctx->codec_id = codec->id;
    avctx->frame_number = 0;
    if (codec->priv_data_size > 0) {
        avctx->priv_data = av_mallocz(codec->priv_data_size);
        if (!avctx->priv_data) 
            return -ENOMEM;
    } else {
        avctx->priv_data = NULL;
    }

    if(avctx->coded_width && avctx->coded_height)
        avcodec_set_dimensions(avctx, avctx->coded_width, avctx->coded_height);
    else if(avctx->width && avctx->height)
        avcodec_set_dimensions(avctx, avctx->width, avctx->height);

    ret = avctx->codec->init(avctx);
    if (ret < 0) {
        av_freep(&avctx->priv_data);
        return ret;
    }
    return 0;
}

int avcodec_encode_audio(AVCodecContext *avctx, uint8_t *buf, int buf_size, 
                         const short *samples)
{
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
    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || pict){
        int ret = avctx->codec->encode(avctx, buf, buf_size, (void *)pict);
        avctx->frame_number++;
        emms_c(); //needed to avoid a emms_c() call before every return;
    
        return ret;
    }else
        return 0;
}

/** 
 * decode a frame. 
 * @param buf bitstream buffer, must be FF_INPUT_BUFFER_PADDING_SIZE larger then the actual read bytes
 * because some optimized bitstream readers read 32 or 64 bit at once and could read over the end
 * @param buf_size the size of the buffer in bytes
 * @param got_picture_ptr zero if no frame could be decompressed, Otherwise, it is non zero
 * @return -1 if error, otherwise return the number of
 * bytes used. 
 */
int avcodec_decode_video(AVCodecContext *avctx, AVFrame *picture, 
                         int *got_picture_ptr,
                         uint8_t *buf, int buf_size)
{
    int ret;
    
    *got_picture_ptr= 0;
    ret = avctx->codec->decode(avctx, picture, got_picture_ptr, 
                               buf, buf_size);

    emms_c(); //needed to avoid a emms_c() call before every return;
    
    if (*got_picture_ptr)                           
        avctx->frame_number++;
    return ret;
}

/* decode an audio frame. return -1 if error, otherwise return the
   *number of bytes used. If no frame could be decompressed,
   *frame_size_ptr is zero. Otherwise, it is the decompressed frame
   *size in BYTES. */
int avcodec_decode_audio(AVCodecContext *avctx, int16_t *samples, 
                         int *frame_size_ptr,
                         uint8_t *buf, int buf_size)
{
    int ret;

    *frame_size_ptr= 0;
    ret = avctx->codec->decode(avctx, samples, frame_size_ptr, 
                               buf, buf_size);
    avctx->frame_number++;
    return ret;
}

int avcodec_close(AVCodecContext *avctx)
{
    if (avctx->codec->close)
        avctx->codec->close(avctx);
    avcodec_default_free_buffers(avctx);
    av_freep(&avctx->priv_data);
    avctx->codec = NULL;
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

static AVCodec *avcodec_find(enum CodecID id)
{
    AVCodec *p;
    p = first_avcodec;
    while (p) {
        if (p->id == id)
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
        if (enc->codec_type == CODEC_TYPE_VIDEO) {
            snprintf(buf1, sizeof(buf1), "%c%c%c%c", 
                     enc->codec_tag & 0xff,
                     (enc->codec_tag >> 8) & 0xff,
                     (enc->codec_tag >> 16) & 0xff,
                     (enc->codec_tag >> 24) & 0xff);
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
        if (enc->codec_id == CODEC_ID_RAWVIDEO) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %s",
                     avcodec_get_pix_fmt_name(enc->pix_fmt));
        }
        if (enc->width) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %dx%d, %0.2f fps",
                     enc->width, enc->height, 
                     (float)enc->frame_rate / enc->frame_rate_base);
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
                sprintf(channels_str, "%d channels", enc->channels);
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

/* must be called before any other functions */
void avcodec_init(void)
{
    static int inited = 0;

    if (inited != 0)
	return;
    inited = 1;

    dsputil_static_init();
}

/**
 * Flush buffers, should be called when seeking or when swicthing to a different stream.
 */
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

int av_reduce(int *dst_nom, int *dst_den, int64_t nom, int64_t den, int64_t max){
    AVRational a0={0,1}, a1={1,0};
    int sign= (nom<0) ^ (den<0);
    int64_t gcd= ff_gcd(ABS(nom), ABS(den));

    nom = ABS(nom)/gcd;
    den = ABS(den)/gcd;
    if(nom<=max && den<=max){
        a1= (AVRational){nom, den};
        den=0;
    }
    
    while(den){
        int64_t x       = nom / den;
        int64_t next_den= nom - den*x;
        int64_t a2n= x*a1.num + a0.num;
        int64_t a2d= x*a1.den + a0.den;

        if(a2n > max || a2d > max) break;

        a0= a1;
        a1= (AVRational){a2n, a2d};
        nom= den;
        den= next_den;
    }
    assert(ff_gcd(a1.num, a1.den) == 1);
    
    *dst_nom = sign ? -a1.num : a1.num;
    *dst_den = a1.den;
    
    return den==0;
}

int64_t av_rescale_rnd(int64_t a, int64_t b, int64_t c, enum AVRounding rnd){
    AVInteger ai;
    int64_t r=0;
    assert(c > 0);
    assert(b >=0);
    assert(rnd >=0 && rnd<=5 && rnd!=4);
    
    if(a<0 && a != INT64_MIN) return -av_rescale_rnd(-a, b, c, rnd ^ ((rnd>>1)&1)); 
    
    if(rnd==AV_ROUND_NEAR_INF) r= c/2;
    else if(rnd&1)             r= c-1;

    if(b<=INT_MAX && c<=INT_MAX){
        if(a<=INT_MAX)
            return (a * b + r)/c;
        else
            return a/c*b + (a%c*b + r)/c;
    }
    
    ai= av_mul_i(av_int2i(a), av_int2i(b));
    ai= av_add_i(ai, av_int2i(r));
    
    return av_i2int(av_div_i(ai, av_int2i(c)));
}

int64_t av_rescale(int64_t a, int64_t b, int64_t c){
    return av_rescale_rnd(a, b, c, AV_ROUND_NEAR_INF);
}

int64_t ff_gcd(int64_t a, int64_t b){
    if(b) return ff_gcd(b, a%b);
    else  return a;
}

/* av_log API */

static int av_log_level = AV_LOG_DEBUG;

static void av_log_default_callback(void* ptr, int level, const char* fmt, va_list vl)
{
    static int print_prefix=1;
    AVClass* avc= ptr ? *(AVClass**)ptr : NULL;
    if(level>av_log_level)
	return;
#undef fprintf
    if(print_prefix && avc) {
	    fprintf(stderr, "[%s @ %p]", avc->item_name(ptr), avc);
    }
#define fprintf please_use_av_log
        
    print_prefix= strstr(fmt, "\n") != NULL;
        
    vfprintf(stderr, fmt, vl);
}

static void (*av_log_callback)(void*, int, const char*, va_list) = av_log_default_callback;

void av_log(void* avcl, int level, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    av_vlog(avcl, level, fmt, vl);
    va_end(vl);
}

void av_vlog(void* avcl, int level, const char *fmt, va_list vl)
{
    av_log_callback(avcl, level, fmt, vl);
}

int av_log_get_level(void)
{
    return av_log_level;
}

void av_log_set_level(int level)
{
    av_log_level = level;
}

void av_log_set_callback(void (*callback)(void*, int, const char*, va_list))
{
    av_log_callback = callback;
}

#if !defined(HAVE_THREADS)
int avcodec_thread_init(AVCodecContext *s, int thread_count){
    return -1;
}
#endif
