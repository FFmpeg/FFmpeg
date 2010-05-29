/*
 * utils for libavcodec
 * Copyright (c) 2001 Fabrice Bellard
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
 * @file
 * utils.
 */

/* needed for mkstemp() */
#define _XOPEN_SOURCE 600

#include "libavutil/avstring.h"
#include "libavutil/integer.h"
#include "libavutil/crc.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "dsputil.h"
#include "opt.h"
#include "imgconvert.h"
#include "audioconvert.h"
#include "libxvid_internal.h"
#include "internal.h"
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <float.h>
#if !HAVE_MKSTEMP
#include <fcntl.h>
#endif

static int volatile entangled_thread_counter=0;
int (*ff_lockmgr_cb)(void **mutex, enum AVLockOp op);
static void *codec_mutex;

void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size)
{
    if(min_size < *size)
        return ptr;

    *size= FFMAX(17*min_size/16 + 32, min_size);

    ptr= av_realloc(ptr, *size);
    if(!ptr) //we could set this to the unmodified min_size but this is safer if the user lost the ptr and uses NULL now
        *size= 0;

    return ptr;
}

void av_fast_malloc(void *ptr, unsigned int *size, unsigned int min_size)
{
    void **p = ptr;
    if (min_size < *size)
        return;
    *size= FFMAX(17*min_size/16 + 32, min_size);
    av_free(*p);
    *p = av_malloc(*size);
    if (!*p) *size = 0;
}

/* encoder management */
static AVCodec *first_avcodec = NULL;

AVCodec *av_codec_next(AVCodec *c){
    if(c) return c->next;
    else  return first_avcodec;
}

void avcodec_register(AVCodec *codec)
{
    AVCodec **p;
    avcodec_init();
    p = &first_avcodec;
    while (*p != NULL) p = &(*p)->next;
    *p = codec;
    codec->next = NULL;
}

#if LIBAVCODEC_VERSION_MAJOR < 53
void register_avcodec(AVCodec *codec)
{
    avcodec_register(codec);
}
#endif

unsigned avcodec_get_edge_width(void)
{
    return EDGE_WIDTH;
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
    int width, height;
    enum PixelFormat pix_fmt;
}InternalBuffer;

#define INTERNAL_BUFFER_SIZE 32

void avcodec_align_dimensions2(AVCodecContext *s, int *width, int *height, int linesize_align[4]){
    int w_align= 1;
    int h_align= 1;

    switch(s->pix_fmt){
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUYV422:
    case PIX_FMT_UYVY422:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV440P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_GRAY8:
    case PIX_FMT_GRAY16BE:
    case PIX_FMT_GRAY16LE:
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUVJ440P:
    case PIX_FMT_YUVJ444P:
    case PIX_FMT_YUVA420P:
        w_align= 16; //FIXME check for non mpeg style codecs and use less alignment
        h_align= 16;
        if(s->codec_id == CODEC_ID_MPEG2VIDEO || s->codec_id == CODEC_ID_MJPEG || s->codec_id == CODEC_ID_AMV || s->codec_id == CODEC_ID_THP)
            h_align= 32; // interlaced is rounded up to 2 MBs
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
    case PIX_FMT_BGR8:
    case PIX_FMT_RGB8:
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

    *width = FFALIGN(*width , w_align);
    *height= FFALIGN(*height, h_align);
    if(s->codec_id == CODEC_ID_H264)
        *height+=2; // some of the optimized chroma MC reads one line too much

    linesize_align[0] =
    linesize_align[1] =
    linesize_align[2] =
    linesize_align[3] = STRIDE_ALIGN;
//STRIDE_ALIGN is 8 for SSE* but this does not work for SVQ1 chroma planes
//we could change STRIDE_ALIGN to 16 for x86/sse but it would increase the
//picture size unneccessarily in some cases. The solution here is not
//pretty and better ideas are welcome!
#if HAVE_MMX
    if(s->codec_id == CODEC_ID_SVQ1 || s->codec_id == CODEC_ID_VP5 ||
       s->codec_id == CODEC_ID_VP6 || s->codec_id == CODEC_ID_VP6F ||
       s->codec_id == CODEC_ID_VP6A) {
        linesize_align[0] =
        linesize_align[1] =
        linesize_align[2] = 16;
    }
#endif
}

void avcodec_align_dimensions(AVCodecContext *s, int *width, int *height){
    int chroma_shift = av_pix_fmt_descriptors[s->pix_fmt].log2_chroma_w;
    int linesize_align[4];
    int align;
    avcodec_align_dimensions2(s, width, height, linesize_align);
    align = FFMAX(linesize_align[0], linesize_align[3]);
    linesize_align[1] <<= chroma_shift;
    linesize_align[2] <<= chroma_shift;
    align = FFMAX3(align, linesize_align[1], linesize_align[2]);
    *width=FFALIGN(*width, align);
}

int avcodec_check_dimensions(void *av_log_ctx, unsigned int w, unsigned int h){
    if((int)w>0 && (int)h>0 && (w+128)*(uint64_t)(h+128) < INT_MAX/8)
        return 0;

    av_log(av_log_ctx, AV_LOG_ERROR, "picture size invalid (%ux%u)\n", w, h);
    return AVERROR(EINVAL);
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
        s->internal_buffer= av_mallocz((INTERNAL_BUFFER_SIZE+1)*sizeof(InternalBuffer));
    }
#if 0
    s->internal_buffer= av_fast_realloc(
        s->internal_buffer,
        &s->internal_buffer_size,
        sizeof(InternalBuffer)*FFMAX(99,  s->internal_buffer_count+1)/*FIXME*/
        );
#endif

    buf= &((InternalBuffer*)s->internal_buffer)[s->internal_buffer_count];
    picture_number= &(((InternalBuffer*)s->internal_buffer)[INTERNAL_BUFFER_SIZE]).last_pic_num; //FIXME ugly hack
    (*picture_number)++;

    if(buf->base[0] && (buf->width != w || buf->height != h || buf->pix_fmt != s->pix_fmt)){
        for(i=0; i<4; i++){
            av_freep(&buf->base[i]);
            buf->data[i]= NULL;
        }
    }

    if(buf->base[0]){
        pic->age= *picture_number - buf->last_pic_num;
        buf->last_pic_num= *picture_number;
    }else{
        int h_chroma_shift, v_chroma_shift;
        int size[4] = {0};
        int tmpsize;
        int unaligned;
        AVPicture picture;
        int stride_align[4];

        avcodec_get_chroma_sub_sample(s->pix_fmt, &h_chroma_shift, &v_chroma_shift);

        avcodec_align_dimensions2(s, &w, &h, stride_align);

        if(!(s->flags&CODEC_FLAG_EMU_EDGE)){
            w+= EDGE_WIDTH*2;
            h+= EDGE_WIDTH*2;
        }

        do {
            // NOTE: do not align linesizes individually, this breaks e.g. assumptions
            // that linesize[0] == 2*linesize[1] in the MPEG-encoder for 4:2:2
            ff_fill_linesize(&picture, s->pix_fmt, w);
            // increase alignment of w for next try (rhs gives the lowest bit set in w)
            w += w & ~(w-1);

            unaligned = 0;
            for (i=0; i<4; i++){
                unaligned |= picture.linesize[i] % stride_align[i];
            }
        } while (unaligned);

        tmpsize = ff_fill_pointer(&picture, NULL, s->pix_fmt, h);
        if (tmpsize < 0)
            return -1;

        for (i=0; i<3 && picture.data[i+1]; i++)
            size[i] = picture.data[i+1] - picture.data[i];
        size[i] = tmpsize - (picture.data[i] - picture.data[0]);

        buf->last_pic_num= -256*256*256*64;
        memset(buf->base, 0, sizeof(buf->base));
        memset(buf->data, 0, sizeof(buf->data));

        for(i=0; i<4 && size[i]; i++){
            const int h_shift= i==0 ? 0 : h_chroma_shift;
            const int v_shift= i==0 ? 0 : v_chroma_shift;

            buf->linesize[i]= picture.linesize[i];

            buf->base[i]= av_malloc(size[i]+16); //FIXME 16
            if(buf->base[i]==NULL) return -1;
            memset(buf->base[i], 128, size[i]);

            // no edge if EDEG EMU or not planar YUV
            if((s->flags&CODEC_FLAG_EMU_EDGE) || !size[2])
                buf->data[i] = buf->base[i];
            else
                buf->data[i] = buf->base[i] + FFALIGN((buf->linesize[i]*EDGE_WIDTH>>v_shift) + (EDGE_WIDTH>>h_shift), stride_align[i]);
        }
        if(size[1] && !size[2])
            ff_set_systematic_pal((uint32_t*)buf->data[1], s->pix_fmt);
        buf->width  = s->width;
        buf->height = s->height;
        buf->pix_fmt= s->pix_fmt;
        pic->age= 256*256*256*64;
    }
    pic->type= FF_BUFFER_TYPE_INTERNAL;

    for(i=0; i<4; i++){
        pic->base[i]= buf->base[i];
        pic->data[i]= buf->data[i];
        pic->linesize[i]= buf->linesize[i];
    }
    s->internal_buffer_count++;

    pic->reordered_opaque= s->reordered_opaque;

    if(s->debug&FF_DEBUG_BUFFERS)
        av_log(s, AV_LOG_DEBUG, "default_get_buffer called on pic %p, %d buffers used\n", pic, s->internal_buffer_count);

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

    for(i=0; i<4; i++){
        pic->data[i]=NULL;
//        pic->base[i]=NULL;
    }
//printf("R%X\n", pic->opaque);

    if(s->debug&FF_DEBUG_BUFFERS)
        av_log(s, AV_LOG_DEBUG, "default_release_buffer called on pic %p, %d buffers used\n", pic, s->internal_buffer_count);
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
    if(pic->type == FF_BUFFER_TYPE_INTERNAL) {
        pic->reordered_opaque= s->reordered_opaque;
        return 0;
    }

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

int avcodec_default_execute(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2),void *arg, int *ret, int count, int size){
    int i;

    for(i=0; i<count; i++){
        int r= func(c, (char*)arg + i*size);
        if(ret) ret[i]= r;
    }
    return 0;
}

int avcodec_default_execute2(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2, int jobnr, int threadnr),void *arg, int *ret, int count){
    int i;

    for(i=0; i<count; i++){
        int r= func(c, arg, i, 0);
        if(ret) ret[i]= r;
    }
    return 0;
}

enum PixelFormat avcodec_default_get_format(struct AVCodecContext *s, const enum PixelFormat *fmt){
    while (*fmt != PIX_FMT_NONE && ff_is_hwaccel_pix_fmt(*fmt))
        ++fmt;
    return fmt[0];
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

int attribute_align_arg avcodec_open(AVCodecContext *avctx, AVCodec *codec)
{
    int ret= -1;

    /* If there is a user-supplied mutex locking routine, call it. */
    if (ff_lockmgr_cb) {
        if ((*ff_lockmgr_cb)(&codec_mutex, AV_LOCK_OBTAIN))
            return -1;
    }

    entangled_thread_counter++;
    if(entangled_thread_counter != 1){
        av_log(avctx, AV_LOG_ERROR, "insufficient thread locking around avcodec_open/close()\n");
        goto end;
    }

    if(avctx->codec || !codec)
        goto end;

    if (codec->priv_data_size > 0) {
        avctx->priv_data = av_mallocz(codec->priv_data_size);
        if (!avctx->priv_data) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    } else {
        avctx->priv_data = NULL;
    }

    if(avctx->coded_width && avctx->coded_height)
        avcodec_set_dimensions(avctx, avctx->coded_width, avctx->coded_height);
    else if(avctx->width && avctx->height)
        avcodec_set_dimensions(avctx, avctx->width, avctx->height);

#define SANE_NB_CHANNELS 128U
    if (((avctx->coded_width || avctx->coded_height)
        && avcodec_check_dimensions(avctx, avctx->coded_width, avctx->coded_height))
        || avctx->channels > SANE_NB_CHANNELS) {
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    avctx->codec = codec;
    if ((avctx->codec_type == AVMEDIA_TYPE_UNKNOWN || avctx->codec_type == codec->type) &&
        avctx->codec_id == CODEC_ID_NONE) {
        avctx->codec_type = codec->type;
        avctx->codec_id   = codec->id;
    }
    if(avctx->codec_id != codec->id || avctx->codec_type != codec->type){
        av_log(avctx, AV_LOG_ERROR, "codec type or id mismatches\n");
        goto free_and_end;
    }
    avctx->frame_number = 0;
    if(avctx->codec->init){
        ret = avctx->codec->init(avctx);
        if (ret < 0) {
            goto free_and_end;
        }
    }
    ret=0;
end:
    entangled_thread_counter--;

    /* Release any user-supplied mutex. */
    if (ff_lockmgr_cb) {
        (*ff_lockmgr_cb)(&codec_mutex, AV_LOCK_RELEASE);
    }
    return ret;
free_and_end:
    av_freep(&avctx->priv_data);
    avctx->codec= NULL;
    goto end;
}

int attribute_align_arg avcodec_encode_audio(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                         const short *samples)
{
    if(buf_size < FF_MIN_BUFFER_SIZE && 0){
        av_log(avctx, AV_LOG_ERROR, "buffer smaller than minimum size\n");
        return -1;
    }
    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || samples){
        int ret = avctx->codec->encode(avctx, buf, buf_size, samples);
        avctx->frame_number++;
        return ret;
    }else
        return 0;
}

int attribute_align_arg avcodec_encode_video(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                         const AVFrame *pict)
{
    if(buf_size < FF_MIN_BUFFER_SIZE){
        av_log(avctx, AV_LOG_ERROR, "buffer smaller than minimum size\n");
        return -1;
    }
    if(avcodec_check_dimensions(avctx,avctx->width,avctx->height))
        return -1;
    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || pict){
        int ret = avctx->codec->encode(avctx, buf, buf_size, pict);
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
    if(sub->start_display_time) {
        av_log(avctx, AV_LOG_ERROR, "start_display_time must be 0.\n");
        return -1;
    }
    if(sub->num_rects == 0 || !sub->rects)
        return -1;
    ret = avctx->codec->encode(avctx, buf, buf_size, sub);
    avctx->frame_number++;
    return ret;
}

#if LIBAVCODEC_VERSION_MAJOR < 53
int attribute_align_arg avcodec_decode_video(AVCodecContext *avctx, AVFrame *picture,
                         int *got_picture_ptr,
                         const uint8_t *buf, int buf_size)
{
    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = buf;
    avpkt.size = buf_size;
    // HACK for CorePNG to decode as normal PNG by default
    avpkt.flags = AV_PKT_FLAG_KEY;

    return avcodec_decode_video2(avctx, picture, got_picture_ptr, &avpkt);
}
#endif

int attribute_align_arg avcodec_decode_video2(AVCodecContext *avctx, AVFrame *picture,
                         int *got_picture_ptr,
                         AVPacket *avpkt)
{
    int ret;

    *got_picture_ptr= 0;
    if((avctx->coded_width||avctx->coded_height) && avcodec_check_dimensions(avctx,avctx->coded_width,avctx->coded_height))
        return -1;
    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || avpkt->size){
        ret = avctx->codec->decode(avctx, picture, got_picture_ptr,
                                avpkt);

        emms_c(); //needed to avoid an emms_c() call before every return;

        if (*got_picture_ptr)
            avctx->frame_number++;
    }else
        ret= 0;

    return ret;
}

#if LIBAVCODEC_VERSION_MAJOR < 53
int attribute_align_arg avcodec_decode_audio2(AVCodecContext *avctx, int16_t *samples,
                         int *frame_size_ptr,
                         const uint8_t *buf, int buf_size)
{
    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = buf;
    avpkt.size = buf_size;

    return avcodec_decode_audio3(avctx, samples, frame_size_ptr, &avpkt);
}
#endif

int attribute_align_arg avcodec_decode_audio3(AVCodecContext *avctx, int16_t *samples,
                         int *frame_size_ptr,
                         AVPacket *avpkt)
{
    int ret;

    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || avpkt->size){
        //FIXME remove the check below _after_ ensuring that all audio check that the available space is enough
        if(*frame_size_ptr < AVCODEC_MAX_AUDIO_FRAME_SIZE){
            av_log(avctx, AV_LOG_ERROR, "buffer smaller than AVCODEC_MAX_AUDIO_FRAME_SIZE\n");
            return -1;
        }
        if(*frame_size_ptr < FF_MIN_BUFFER_SIZE ||
        *frame_size_ptr < avctx->channels * avctx->frame_size * sizeof(int16_t)){
            av_log(avctx, AV_LOG_ERROR, "buffer %d too small\n", *frame_size_ptr);
            return -1;
        }

        ret = avctx->codec->decode(avctx, samples, frame_size_ptr, avpkt);
        avctx->frame_number++;
    }else{
        ret= 0;
        *frame_size_ptr=0;
    }
    return ret;
}

#if LIBAVCODEC_VERSION_MAJOR < 53
int avcodec_decode_subtitle(AVCodecContext *avctx, AVSubtitle *sub,
                            int *got_sub_ptr,
                            const uint8_t *buf, int buf_size)
{
    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = buf;
    avpkt.size = buf_size;

    return avcodec_decode_subtitle2(avctx, sub, got_sub_ptr, &avpkt);
}
#endif

int avcodec_decode_subtitle2(AVCodecContext *avctx, AVSubtitle *sub,
                            int *got_sub_ptr,
                            AVPacket *avpkt)
{
    int ret;

    *got_sub_ptr = 0;
    ret = avctx->codec->decode(avctx, sub, got_sub_ptr, avpkt);
    if (*got_sub_ptr)
        avctx->frame_number++;
    return ret;
}

av_cold int avcodec_close(AVCodecContext *avctx)
{
    /* If there is a user-supplied mutex locking routine, call it. */
    if (ff_lockmgr_cb) {
        if ((*ff_lockmgr_cb)(&codec_mutex, AV_LOCK_OBTAIN))
            return -1;
    }

    entangled_thread_counter++;
    if(entangled_thread_counter != 1){
        av_log(avctx, AV_LOG_ERROR, "insufficient thread locking around avcodec_open/close()\n");
        entangled_thread_counter--;
        return -1;
    }

    if (HAVE_THREADS && avctx->thread_opaque)
        avcodec_thread_free(avctx);
    if (avctx->codec && avctx->codec->close)
        avctx->codec->close(avctx);
    avcodec_default_free_buffers(avctx);
    av_freep(&avctx->priv_data);
    if(avctx->codec && avctx->codec->encode)
        av_freep(&avctx->extradata);
    avctx->codec = NULL;
    entangled_thread_counter--;

    /* Release any user-supplied mutex. */
    if (ff_lockmgr_cb) {
        (*ff_lockmgr_cb)(&codec_mutex, AV_LOCK_RELEASE);
    }
    return 0;
}

AVCodec *avcodec_find_encoder(enum CodecID id)
{
    AVCodec *p, *experimental=NULL;
    p = first_avcodec;
    while (p) {
        if (p->encode != NULL && p->id == id) {
            if (p->capabilities & CODEC_CAP_EXPERIMENTAL && !experimental) {
                experimental = p;
            } else
                return p;
        }
        p = p->next;
    }
    return experimental;
}

AVCodec *avcodec_find_encoder_by_name(const char *name)
{
    AVCodec *p;
    if (!name)
        return NULL;
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
    if (!name)
        return NULL;
    p = first_avcodec;
    while (p) {
        if (p->decode != NULL && strcmp(name,p->name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

static int get_bit_rate(AVCodecContext *ctx)
{
    int bit_rate;
    int bits_per_sample;

    switch(ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
    case AVMEDIA_TYPE_DATA:
    case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_ATTACHMENT:
        bit_rate = ctx->bit_rate;
        break;
    case AVMEDIA_TYPE_AUDIO:
        bits_per_sample = av_get_bits_per_sample(ctx->codec_id);
        bit_rate = bits_per_sample ? ctx->sample_rate * ctx->channels * bits_per_sample : ctx->bit_rate;
        break;
    default:
        bit_rate = 0;
        break;
    }
    return bit_rate;
}

void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode)
{
    const char *codec_name;
    AVCodec *p;
    char buf1[32];
    int bitrate;
    AVRational display_aspect_ratio;

    if (encode)
        p = avcodec_find_encoder(enc->codec_id);
    else
        p = avcodec_find_decoder(enc->codec_id);

    if (p) {
        codec_name = p->name;
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
    case AVMEDIA_TYPE_VIDEO:
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
            if (enc->sample_aspect_ratio.num) {
                av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                          enc->width*enc->sample_aspect_ratio.num,
                          enc->height*enc->sample_aspect_ratio.den,
                          1024*1024);
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         " [PAR %d:%d DAR %d:%d]",
                         enc->sample_aspect_ratio.num, enc->sample_aspect_ratio.den,
                         display_aspect_ratio.num, display_aspect_ratio.den);
            }
            if(av_log_get_level() >= AV_LOG_DEBUG){
                int g= av_gcd(enc->time_base.num, enc->time_base.den);
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %d/%d",
                     enc->time_base.num/g, enc->time_base.den/g);
            }
        }
        if (encode) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", q=%d-%d", enc->qmin, enc->qmax);
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        snprintf(buf, buf_size,
                 "Audio: %s",
                 codec_name);
        if (enc->sample_rate) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %d Hz", enc->sample_rate);
        }
        av_strlcat(buf, ", ", buf_size);
        avcodec_get_channel_layout_string(buf + strlen(buf), buf_size - strlen(buf), enc->channels, enc->channel_layout);
        if (enc->sample_fmt != SAMPLE_FMT_NONE) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %s", avcodec_get_sample_fmt_name(enc->sample_fmt));
        }
        break;
    case AVMEDIA_TYPE_DATA:
        snprintf(buf, buf_size, "Data: %s", codec_name);
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        snprintf(buf, buf_size, "Subtitle: %s", codec_name);
        break;
    case AVMEDIA_TYPE_ATTACHMENT:
        snprintf(buf, buf_size, "Attachment: %s", codec_name);
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
    bitrate = get_bit_rate(enc);
    if (bitrate != 0) {
        snprintf(buf + strlen(buf), buf_size - strlen(buf),
                 ", %d kb/s", bitrate / 1000);
    }
}

unsigned avcodec_version( void )
{
  return LIBAVCODEC_VERSION_INT;
}

const char *avcodec_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avcodec_license(void)
{
#define LICENSE_PREFIX "libavcodec license: "
    return LICENSE_PREFIX FFMPEG_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

void avcodec_init(void)
{
    static int initialized = 0;

    if (initialized != 0)
        return;
    initialized = 1;

    dsputil_static_init();
}

void avcodec_flush_buffers(AVCodecContext *avctx)
{
    if(avctx->codec->flush)
        avctx->codec->flush(avctx);
}

void avcodec_default_free_buffers(AVCodecContext *s){
    int i, j;

    if(s->internal_buffer==NULL) return;

    if (s->internal_buffer_count)
        av_log(s, AV_LOG_WARNING, "Found %i unreleased buffers!\n", s->internal_buffer_count);
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
    case FF_I_TYPE: return 'I';
    case FF_P_TYPE: return 'P';
    case FF_B_TYPE: return 'B';
    case FF_S_TYPE: return 'S';
    case FF_SI_TYPE:return 'i';
    case FF_SP_TYPE:return 'p';
    case FF_BI_TYPE:return 'b';
    default:        return '?';
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
    case CODEC_ID_ADPCM_IMA_WAV:
    case CODEC_ID_ADPCM_MS:
    case CODEC_ID_ADPCM_YAMAHA:
        return 4;
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_U8:
    case CODEC_ID_PCM_ZORK:
        return 8;
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16LE_PLANAR:
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
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_F32LE:
        return 32;
    case CODEC_ID_PCM_F64BE:
    case CODEC_ID_PCM_F64LE:
        return 64;
    default:
        return 0;
    }
}

int av_get_bits_per_sample_format(enum SampleFormat sample_fmt) {
    switch (sample_fmt) {
    case SAMPLE_FMT_U8:
        return 8;
    case SAMPLE_FMT_S16:
        return 16;
    case SAMPLE_FMT_S32:
    case SAMPLE_FMT_FLT:
        return 32;
    case SAMPLE_FMT_DBL:
        return 64;
    default:
        return 0;
    }
}

#if !HAVE_THREADS
int avcodec_thread_init(AVCodecContext *s, int thread_count){
    s->thread_count = thread_count;
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
#if !HAVE_MKSTEMP
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
#if !HAVE_MKSTEMP
    fd = open(*filename, O_RDWR | O_BINARY | O_CREAT, 0444);
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

typedef struct {
    const char *abbr;
    int width, height;
} VideoFrameSizeAbbr;

typedef struct {
    const char *abbr;
    int rate_num, rate_den;
} VideoFrameRateAbbr;

static const VideoFrameSizeAbbr video_frame_size_abbrs[] = {
    { "ntsc",      720, 480 },
    { "pal",       720, 576 },
    { "qntsc",     352, 240 }, /* VCD compliant NTSC */
    { "qpal",      352, 288 }, /* VCD compliant PAL */
    { "sntsc",     640, 480 }, /* square pixel NTSC */
    { "spal",      768, 576 }, /* square pixel PAL */
    { "film",      352, 240 },
    { "ntsc-film", 352, 240 },
    { "sqcif",     128,  96 },
    { "qcif",      176, 144 },
    { "cif",       352, 288 },
    { "4cif",      704, 576 },
    { "16cif",    1408,1152 },
    { "qqvga",     160, 120 },
    { "qvga",      320, 240 },
    { "vga",       640, 480 },
    { "svga",      800, 600 },
    { "xga",      1024, 768 },
    { "uxga",     1600,1200 },
    { "qxga",     2048,1536 },
    { "sxga",     1280,1024 },
    { "qsxga",    2560,2048 },
    { "hsxga",    5120,4096 },
    { "wvga",      852, 480 },
    { "wxga",     1366, 768 },
    { "wsxga",    1600,1024 },
    { "wuxga",    1920,1200 },
    { "woxga",    2560,1600 },
    { "wqsxga",   3200,2048 },
    { "wquxga",   3840,2400 },
    { "whsxga",   6400,4096 },
    { "whuxga",   7680,4800 },
    { "cga",       320, 200 },
    { "ega",       640, 350 },
    { "hd480",     852, 480 },
    { "hd720",    1280, 720 },
    { "hd1080",   1920,1080 },
};

static const VideoFrameRateAbbr video_frame_rate_abbrs[]= {
    { "ntsc",      30000, 1001 },
    { "pal",          25,    1 },
    { "qntsc",     30000, 1001 }, /* VCD compliant NTSC */
    { "qpal",         25,    1 }, /* VCD compliant PAL */
    { "sntsc",     30000, 1001 }, /* square pixel NTSC */
    { "spal",         25,    1 }, /* square pixel PAL */
    { "film",         24,    1 },
    { "ntsc-film", 24000, 1001 },
};

int av_parse_video_frame_size(int *width_ptr, int *height_ptr, const char *str)
{
    int i;
    int n = FF_ARRAY_ELEMS(video_frame_size_abbrs);
    char *p;
    int frame_width = 0, frame_height = 0;

    for(i=0;i<n;i++) {
        if (!strcmp(video_frame_size_abbrs[i].abbr, str)) {
            frame_width = video_frame_size_abbrs[i].width;
            frame_height = video_frame_size_abbrs[i].height;
            break;
        }
    }
    if (i == n) {
        p = str;
        frame_width = strtol(p, &p, 10);
        if (*p)
            p++;
        frame_height = strtol(p, &p, 10);
    }
    if (frame_width <= 0 || frame_height <= 0)
        return -1;
    *width_ptr = frame_width;
    *height_ptr = frame_height;
    return 0;
}

int av_parse_video_frame_rate(AVRational *frame_rate, const char *arg)
{
    int i;
    int n = FF_ARRAY_ELEMS(video_frame_rate_abbrs);
    char* cp;

    /* First, we check our abbreviation table */
    for (i = 0; i < n; ++i)
         if (!strcmp(video_frame_rate_abbrs[i].abbr, arg)) {
             frame_rate->num = video_frame_rate_abbrs[i].rate_num;
             frame_rate->den = video_frame_rate_abbrs[i].rate_den;
             return 0;
         }

    /* Then, we try to parse it as fraction */
    cp = strchr(arg, '/');
    if (!cp)
        cp = strchr(arg, ':');
    if (cp) {
        char* cpp;
        frame_rate->num = strtol(arg, &cpp, 10);
        if (cpp != arg || cpp == cp)
            frame_rate->den = strtol(cp+1, &cpp, 10);
        else
           frame_rate->num = 0;
    }
    else {
        /* Finally we give up and parse it as double */
        AVRational time_base = av_d2q(strtod(arg, 0), 1001000);
        frame_rate->den = time_base.den;
        frame_rate->num = time_base.num;
    }
    if (!frame_rate->num || !frame_rate->den)
        return -1;
    else
        return 0;
}

int ff_match_2uint16(const uint16_t (*tab)[2], int size, int a, int b){
    int i;
    for(i=0; i<size && !(tab[i][0]==a && tab[i][1]==b); i++);
    return i;
}

void av_log_missing_feature(void *avc, const char *feature, int want_sample)
{
    av_log(avc, AV_LOG_WARNING, "%s not implemented. Update your FFmpeg "
            "version to the newest one from SVN. If the problem still "
            "occurs, it means that your file has a feature which has not "
            "been implemented.", feature);
    if(want_sample)
        av_log_ask_for_sample(avc, NULL);
    else
        av_log(avc, AV_LOG_WARNING, "\n");
}

void av_log_ask_for_sample(void *avc, const char *msg)
{
    if (msg)
        av_log(avc, AV_LOG_WARNING, "%s ", msg);
    av_log(avc, AV_LOG_WARNING, "If you want to help, upload a sample "
            "of this file to ftp://upload.ffmpeg.org/MPlayer/incoming/ "
            "and contact the ffmpeg-devel mailing list.\n");
}

static AVHWAccel *first_hwaccel = NULL;

void av_register_hwaccel(AVHWAccel *hwaccel)
{
    AVHWAccel **p = &first_hwaccel;
    while (*p)
        p = &(*p)->next;
    *p = hwaccel;
    hwaccel->next = NULL;
}

AVHWAccel *av_hwaccel_next(AVHWAccel *hwaccel)
{
    return hwaccel ? hwaccel->next : first_hwaccel;
}

AVHWAccel *ff_find_hwaccel(enum CodecID codec_id, enum PixelFormat pix_fmt)
{
    AVHWAccel *hwaccel=NULL;

    while((hwaccel= av_hwaccel_next(hwaccel))){
        if (   hwaccel->id      == codec_id
            && hwaccel->pix_fmt == pix_fmt)
            return hwaccel;
    }
    return NULL;
}

int av_lockmgr_register(int (*cb)(void **mutex, enum AVLockOp op))
{
    if (ff_lockmgr_cb) {
        if (ff_lockmgr_cb(&codec_mutex, AV_LOCK_DESTROY))
            return -1;
    }

    ff_lockmgr_cb = cb;

    if (ff_lockmgr_cb) {
        if (ff_lockmgr_cb(&codec_mutex, AV_LOCK_CREATE))
            return -1;
    }
    return 0;
}
