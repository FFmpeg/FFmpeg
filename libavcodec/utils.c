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

#include "libavutil/avstring.h"
#include "libavutil/crc.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/audioconvert.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/dict.h"
#include "avcodec.h"
#include "dsputil.h"
#include "libavutil/opt.h"
#include "imgconvert.h"
#include "thread.h"
#include "audioconvert.h"
#include "internal.h"
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <float.h>

static int volatile entangled_thread_counter=0;
static int (*ff_lockmgr_cb)(void **mutex, enum AVLockOp op);
static void *codec_mutex;
static void *avformat_mutex;

void *av_fast_realloc(void *ptr, unsigned int *size, size_t min_size)
{
    if(min_size < *size)
        return ptr;

    min_size= FFMAX(17*min_size/16 + 32, min_size);

    ptr= av_realloc(ptr, min_size);
    if(!ptr) //we could set this to the unmodified min_size but this is safer if the user lost the ptr and uses NULL now
        min_size= 0;

    *size= min_size;

    return ptr;
}

void av_fast_malloc(void *ptr, unsigned int *size, size_t min_size)
{
    void **p = ptr;
    if (min_size < *size)
        return;
    min_size= FFMAX(17*min_size/16 + 32, min_size);
    av_free(*p);
    *p = av_malloc(min_size);
    if (!*p) min_size = 0;
    *size= min_size;
}

/* encoder management */
static AVCodec *first_avcodec = NULL;

AVCodec *av_codec_next(AVCodec *c){
    if(c) return c->next;
    else  return first_avcodec;
}

#if !FF_API_AVCODEC_INIT
static
#endif
void avcodec_init(void)
{
    static int initialized = 0;

    if (initialized != 0)
        return;
    initialized = 1;

    dsputil_static_init();
}

void avcodec_register(AVCodec *codec)
{
    AVCodec **p;
    avcodec_init();
    p = &first_avcodec;
    while (*p != NULL) p = &(*p)->next;
    *p = codec;
    codec->next = NULL;

    if (codec->init_static_data)
        codec->init_static_data(codec);
}

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

#define INTERNAL_BUFFER_SIZE (32+1)

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
    case PIX_FMT_GBRP:
    case PIX_FMT_GRAY8:
    case PIX_FMT_GRAY16BE:
    case PIX_FMT_GRAY16LE:
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUVJ440P:
    case PIX_FMT_YUVJ444P:
    case PIX_FMT_YUVA420P:
    case PIX_FMT_YUV420P9LE:
    case PIX_FMT_YUV420P9BE:
    case PIX_FMT_YUV420P10LE:
    case PIX_FMT_YUV420P10BE:
    case PIX_FMT_YUV422P9LE:
    case PIX_FMT_YUV422P9BE:
    case PIX_FMT_YUV422P10LE:
    case PIX_FMT_YUV422P10BE:
    case PIX_FMT_YUV444P9LE:
    case PIX_FMT_YUV444P9BE:
    case PIX_FMT_YUV444P10LE:
    case PIX_FMT_YUV444P10BE:
    case PIX_FMT_GBRP9LE:
    case PIX_FMT_GBRP9BE:
    case PIX_FMT_GBRP10LE:
    case PIX_FMT_GBRP10BE:
        w_align= 16; //FIXME check for non mpeg style codecs and use less alignment
        h_align= 16;
        if(s->codec_id == CODEC_ID_MPEG2VIDEO || s->codec_id == CODEC_ID_MJPEG || s->codec_id == CODEC_ID_AMV || s->codec_id == CODEC_ID_THP || s->codec_id == CODEC_ID_H264 || s->codec_id == CODEC_ID_PRORES)
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
    if(s->codec_id == CODEC_ID_H264 || s->lowres)
        *height+=2; // some of the optimized chroma MC reads one line too much
                    // which is also done in mpeg decoders with lowres > 0

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
       s->codec_id == CODEC_ID_VP6A || s->codec_id == CODEC_ID_DIRAC) {
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

void ff_init_buffer_info(AVCodecContext *s, AVFrame *pic)
{
    if (s->pkt) {
        pic->pkt_pts = s->pkt->pts;
        pic->pkt_pos = s->pkt->pos;
    } else {
        pic->pkt_pts = AV_NOPTS_VALUE;
        pic->pkt_pos = -1;
    }
    pic->reordered_opaque= s->reordered_opaque;
    pic->sample_aspect_ratio = s->sample_aspect_ratio;
    pic->width               = s->width;
    pic->height              = s->height;
    pic->format              = s->pix_fmt;
}

int avcodec_default_get_buffer(AVCodecContext *s, AVFrame *pic){
    int i;
    int w= s->width;
    int h= s->height;
    InternalBuffer *buf;
    int *picture_number;
    AVCodecInternal *avci = s->internal;

    if(pic->data[0]!=NULL) {
        av_log(s, AV_LOG_ERROR, "pic->data[0]!=NULL in avcodec_default_get_buffer\n");
        return -1;
    }
    if(avci->buffer_count >= INTERNAL_BUFFER_SIZE) {
        av_log(s, AV_LOG_ERROR, "buffer_count overflow (missing release_buffer?)\n");
        return -1;
    }

    if(av_image_check_size(w, h, 0, s))
        return -1;

    if (!avci->buffer) {
        avci->buffer = av_mallocz((INTERNAL_BUFFER_SIZE+1) *
                                  sizeof(InternalBuffer));
    }

    buf = &avci->buffer[avci->buffer_count];
    picture_number = &(avci->buffer[INTERNAL_BUFFER_SIZE]).last_pic_num; //FIXME ugly hack
    (*picture_number)++;

    if(buf->base[0] && (buf->width != w || buf->height != h || buf->pix_fmt != s->pix_fmt)){
        if(s->active_thread_type&FF_THREAD_FRAME) {
            av_log_missing_feature(s, "Width/height changing with frame threads is", 0);
            return -1;
        }

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
        const int pixel_size = av_pix_fmt_descriptors[s->pix_fmt].comp[0].step_minus1+1;

        avcodec_get_chroma_sub_sample(s->pix_fmt, &h_chroma_shift, &v_chroma_shift);

        avcodec_align_dimensions2(s, &w, &h, stride_align);

        if(!(s->flags&CODEC_FLAG_EMU_EDGE)){
            w+= EDGE_WIDTH*2;
            h+= EDGE_WIDTH*2;
        }

        do {
            // NOTE: do not align linesizes individually, this breaks e.g. assumptions
            // that linesize[0] == 2*linesize[1] in the MPEG-encoder for 4:2:2
            av_image_fill_linesizes(picture.linesize, s->pix_fmt, w);
            // increase alignment of w for next try (rhs gives the lowest bit set in w)
            w += w & ~(w-1);

            unaligned = 0;
            for (i=0; i<4; i++){
                unaligned |= picture.linesize[i] % stride_align[i];
            }
        } while (unaligned);

        tmpsize = av_image_fill_pointers(picture.data, s->pix_fmt, h, NULL, picture.linesize);
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

            // no edge if EDGE EMU or not planar YUV
            if((s->flags&CODEC_FLAG_EMU_EDGE) || !size[2])
                buf->data[i] = buf->base[i];
            else
                buf->data[i] = buf->base[i] + FFALIGN((buf->linesize[i]*EDGE_WIDTH>>v_shift) + (pixel_size*EDGE_WIDTH>>h_shift), stride_align[i]);
        }
        if(size[1] && !size[2])
            ff_set_systematic_pal2((uint32_t*)buf->data[1], s->pix_fmt);
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
    avci->buffer_count++;

    if (s->pkt) {
        pic->pkt_pts = s->pkt->pts;
        pic->pkt_pos = s->pkt->pos;
    } else {
        pic->pkt_pts = AV_NOPTS_VALUE;
        pic->pkt_pos = -1;
    }
    pic->reordered_opaque= s->reordered_opaque;
    pic->sample_aspect_ratio = s->sample_aspect_ratio;
    pic->width               = s->width;
    pic->height              = s->height;
    pic->format              = s->pix_fmt;

    if(s->debug&FF_DEBUG_BUFFERS)
        av_log(s, AV_LOG_DEBUG, "default_get_buffer called on pic %p, %d "
               "buffers used\n", pic, avci->buffer_count);

    return 0;
}

void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic){
    int i;
    InternalBuffer *buf, *last;
    AVCodecInternal *avci = s->internal;

    assert(pic->type==FF_BUFFER_TYPE_INTERNAL);
    assert(avci->buffer_count);

    if (avci->buffer) {
        buf = NULL; /* avoids warning */
        for (i = 0; i < avci->buffer_count; i++) { //just 3-5 checks so is not worth to optimize
            buf = &avci->buffer[i];
            if (buf->data[0] == pic->data[0])
                break;
        }
        assert(i < avci->buffer_count);
        avci->buffer_count--;
        last = &avci->buffer[avci->buffer_count];

        FFSWAP(InternalBuffer, *buf, *last);
    }

    for(i=0; i<4; i++){
        pic->data[i]=NULL;
//        pic->base[i]=NULL;
    }
//printf("R%X\n", pic->opaque);

    if(s->debug&FF_DEBUG_BUFFERS)
        av_log(s, AV_LOG_DEBUG, "default_release_buffer called on pic %p, %d "
               "buffers used\n", pic, avci->buffer_count);
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
        if(s->pkt) pic->pkt_pts= s->pkt->pts;
        else       pic->pkt_pts= AV_NOPTS_VALUE;
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

    pic->pts = pic->best_effort_timestamp = AV_NOPTS_VALUE;
    pic->pkt_pos = -1;
    pic->key_frame= 1;
    pic->sample_aspect_ratio = (AVRational){0, 1};
    pic->format = -1;           /* unknown */
}

AVFrame *avcodec_alloc_frame(void){
    AVFrame *pic= av_malloc(sizeof(AVFrame));

    if(pic==NULL) return NULL;

    avcodec_get_frame_defaults(pic);

    return pic;
}

static void avcodec_get_subtitle_defaults(AVSubtitle *sub)
{
    memset(sub, 0, sizeof(*sub));
    sub->pts = AV_NOPTS_VALUE;
}

#if FF_API_AVCODEC_OPEN
int attribute_align_arg avcodec_open(AVCodecContext *avctx, AVCodec *codec)
{
    return avcodec_open2(avctx, codec, NULL);
}
#endif

int attribute_align_arg avcodec_open2(AVCodecContext *avctx, AVCodec *codec, AVDictionary **options)
{
    int ret = 0;
    AVDictionary *tmp = NULL;

    if (options)
        av_dict_copy(&tmp, *options, 0);

    /* If there is a user-supplied mutex locking routine, call it. */
    if (ff_lockmgr_cb) {
        if ((*ff_lockmgr_cb)(&codec_mutex, AV_LOCK_OBTAIN))
            return -1;
    }

    entangled_thread_counter++;
    if(entangled_thread_counter != 1){
        av_log(avctx, AV_LOG_ERROR, "insufficient thread locking around avcodec_open/close()\n");
        ret = -1;
        goto end;
    }

    if(avctx->codec || !codec) {
        ret = AVERROR(EINVAL);
        goto end;
    }

    avctx->internal = av_mallocz(sizeof(AVCodecInternal));
    if (!avctx->internal) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (codec->priv_data_size > 0) {
      if(!avctx->priv_data){
        avctx->priv_data = av_mallocz(codec->priv_data_size);
        if (!avctx->priv_data) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        if (codec->priv_class) {
            *(AVClass**)avctx->priv_data= codec->priv_class;
            av_opt_set_defaults(avctx->priv_data);
        }
      }
      if (codec->priv_class && (ret = av_opt_set_dict(avctx->priv_data, &tmp)) < 0)
          goto free_and_end;
    } else {
        avctx->priv_data = NULL;
    }
    if ((ret = av_opt_set_dict(avctx, &tmp)) < 0)
        goto free_and_end;

    if(avctx->coded_width && avctx->coded_height)
        avcodec_set_dimensions(avctx, avctx->coded_width, avctx->coded_height);
    else if(avctx->width && avctx->height)
        avcodec_set_dimensions(avctx, avctx->width, avctx->height);

    if ((avctx->coded_width || avctx->coded_height || avctx->width || avctx->height)
        && (  av_image_check_size(avctx->coded_width, avctx->coded_height, 0, avctx) < 0
           || av_image_check_size(avctx->width,       avctx->height,       0, avctx) < 0)) {
        av_log(avctx, AV_LOG_WARNING, "ignoring invalid width/height values\n");
        avcodec_set_dimensions(avctx, 0, 0);
    }

    /* if the decoder init function was already called previously,
       free the already allocated subtitle_header before overwriting it */
    if (codec->decode)
        av_freep(&avctx->subtitle_header);

#define SANE_NB_CHANNELS 128U
    if (avctx->channels > SANE_NB_CHANNELS) {
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    avctx->codec = codec;
    if ((avctx->codec_type == AVMEDIA_TYPE_UNKNOWN || avctx->codec_type == codec->type) &&
        avctx->codec_id == CODEC_ID_NONE) {
        avctx->codec_type = codec->type;
        avctx->codec_id   = codec->id;
    }
    if (avctx->codec_id != codec->id || (avctx->codec_type != codec->type
                           && avctx->codec_type != AVMEDIA_TYPE_ATTACHMENT)) {
        av_log(avctx, AV_LOG_ERROR, "codec type or id mismatches\n");
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }
    avctx->frame_number = 0;
#if FF_API_ER

    av_log(avctx, AV_LOG_DEBUG, "err{or,}_recognition separate: %d; %d\n",
           avctx->error_recognition, avctx->err_recognition);
    switch(avctx->error_recognition){
        case FF_ER_EXPLODE        : avctx->err_recognition |= AV_EF_EXPLODE | AV_EF_COMPLIANT | AV_EF_CAREFUL;
            break;
        case FF_ER_VERY_AGGRESSIVE:
        case FF_ER_AGGRESSIVE     : avctx->err_recognition |= AV_EF_AGGRESSIVE;
        case FF_ER_COMPLIANT      : avctx->err_recognition |= AV_EF_COMPLIANT;
        case FF_ER_CAREFUL        : avctx->err_recognition |= AV_EF_CAREFUL;
    }

    av_log(avctx, AV_LOG_DEBUG, "err{or,}_recognition combined: %d; %d\n",
           avctx->error_recognition, avctx->err_recognition);
#endif

    if (!HAVE_THREADS)
        av_log(avctx, AV_LOG_WARNING, "Warning: not compiled with thread support, using thread emulation\n");

    if (HAVE_THREADS && !avctx->thread_opaque) {
        ret = ff_thread_init(avctx);
        if (ret < 0) {
            goto free_and_end;
        }
    }

    if (avctx->codec->max_lowres < avctx->lowres || avctx->lowres < 0) {
        av_log(avctx, AV_LOG_ERROR, "The maximum value for lowres supported by the decoder is %d\n",
               avctx->codec->max_lowres);
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }
    if (avctx->codec->encode) {
        int i;
        if (avctx->codec->sample_fmts) {
            for (i = 0; avctx->codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; i++)
                if (avctx->sample_fmt == avctx->codec->sample_fmts[i])
                    break;
            if (avctx->codec->sample_fmts[i] == AV_SAMPLE_FMT_NONE) {
                av_log(avctx, AV_LOG_ERROR, "Specified sample_fmt is not supported.\n");
                ret = AVERROR(EINVAL);
                goto free_and_end;
            }
        }
        if (avctx->codec->supported_samplerates) {
            for (i = 0; avctx->codec->supported_samplerates[i] != 0; i++)
                if (avctx->sample_rate == avctx->codec->supported_samplerates[i])
                    break;
            if (avctx->codec->supported_samplerates[i] == 0) {
                av_log(avctx, AV_LOG_ERROR, "Specified sample_rate is not supported\n");
                ret = AVERROR(EINVAL);
                goto free_and_end;
            }
        }
        if (avctx->codec->channel_layouts) {
            if (!avctx->channel_layout) {
                av_log(avctx, AV_LOG_WARNING, "channel_layout not specified\n");
            } else {
                for (i = 0; avctx->codec->channel_layouts[i] != 0; i++)
                    if (avctx->channel_layout == avctx->codec->channel_layouts[i])
                        break;
                if (avctx->codec->channel_layouts[i] == 0) {
                    av_log(avctx, AV_LOG_ERROR, "Specified channel_layout is not supported\n");
                    ret = AVERROR(EINVAL);
                    goto free_and_end;
                }
            }
        }
        if (avctx->channel_layout && avctx->channels) {
            if (av_get_channel_layout_nb_channels(avctx->channel_layout) != avctx->channels) {
                av_log(avctx, AV_LOG_ERROR, "channel layout does not match number of channels\n");
                ret = AVERROR(EINVAL);
                goto free_and_end;
            }
        } else if (avctx->channel_layout) {
            avctx->channels = av_get_channel_layout_nb_channels(avctx->channel_layout);
        }
    }

    avctx->pts_correction_num_faulty_pts =
    avctx->pts_correction_num_faulty_dts = 0;
    avctx->pts_correction_last_pts =
    avctx->pts_correction_last_dts = INT64_MIN;

    if(avctx->codec->init && !(avctx->active_thread_type&FF_THREAD_FRAME)){
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
    if (options) {
        av_dict_free(options);
        *options = tmp;
    }

    return ret;
free_and_end:
    av_dict_free(&tmp);
    av_freep(&avctx->priv_data);
    av_freep(&avctx->internal);
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
    if(av_image_check_size(avctx->width, avctx->height, 0, avctx))
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

    ret = avctx->codec->encode(avctx, buf, buf_size, sub);
    avctx->frame_number++;
    return ret;
}

/**
 * Attempt to guess proper monotonic timestamps for decoded video frames
 * which might have incorrect times. Input timestamps may wrap around, in
 * which case the output will as well.
 *
 * @param pts the pts field of the decoded AVPacket, as passed through
 * AVFrame.pkt_pts
 * @param dts the dts field of the decoded AVPacket
 * @return one of the input values, may be AV_NOPTS_VALUE
 */
static int64_t guess_correct_pts(AVCodecContext *ctx,
                                 int64_t reordered_pts, int64_t dts)
{
    int64_t pts = AV_NOPTS_VALUE;

    if (dts != AV_NOPTS_VALUE) {
        ctx->pts_correction_num_faulty_dts += dts <= ctx->pts_correction_last_dts;
        ctx->pts_correction_last_dts = dts;
    }
    if (reordered_pts != AV_NOPTS_VALUE) {
        ctx->pts_correction_num_faulty_pts += reordered_pts <= ctx->pts_correction_last_pts;
        ctx->pts_correction_last_pts = reordered_pts;
    }
    if ((ctx->pts_correction_num_faulty_pts<=ctx->pts_correction_num_faulty_dts || dts == AV_NOPTS_VALUE)
       && reordered_pts != AV_NOPTS_VALUE)
        pts = reordered_pts;
    else
        pts = dts;

    return pts;
}

int attribute_align_arg avcodec_decode_video2(AVCodecContext *avctx, AVFrame *picture,
                         int *got_picture_ptr,
                         AVPacket *avpkt)
{
    int ret;

    *got_picture_ptr= 0;
    if((avctx->coded_width||avctx->coded_height) && av_image_check_size(avctx->coded_width, avctx->coded_height, 0, avctx))
        return -1;

    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || avpkt->size || (avctx->active_thread_type&FF_THREAD_FRAME)){
        av_packet_split_side_data(avpkt);
        avctx->pkt = avpkt;
        if (HAVE_THREADS && avctx->active_thread_type&FF_THREAD_FRAME)
             ret = ff_thread_decode_frame(avctx, picture, got_picture_ptr,
                                          avpkt);
        else {
            ret = avctx->codec->decode(avctx, picture, got_picture_ptr,
                              avpkt);
            picture->pkt_dts= avpkt->dts;

            if(!avctx->has_b_frames){
            picture->pkt_pos= avpkt->pos;
            }
            //FIXME these should be under if(!avctx->has_b_frames)
            if (!picture->sample_aspect_ratio.num)
                picture->sample_aspect_ratio = avctx->sample_aspect_ratio;
            if (!picture->width)
                picture->width = avctx->width;
            if (!picture->height)
                picture->height = avctx->height;
            if (picture->format == PIX_FMT_NONE)
                picture->format = avctx->pix_fmt;
        }

        emms_c(); //needed to avoid an emms_c() call before every return;


        if (*got_picture_ptr){
            avctx->frame_number++;
            picture->best_effort_timestamp = guess_correct_pts(avctx,
                                                            picture->pkt_pts,
                                                            picture->pkt_dts);
        }
    }else
        ret= 0;

    return ret;
}

int attribute_align_arg avcodec_decode_audio3(AVCodecContext *avctx, int16_t *samples,
                         int *frame_size_ptr,
                         AVPacket *avpkt)
{
    int ret;

    avctx->pkt = avpkt;

    if (!avpkt->data && avpkt->size) {
        av_log(avctx, AV_LOG_ERROR, "invalid packet: NULL data, size != 0\n");
        return AVERROR(EINVAL);
    }

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

int avcodec_decode_subtitle2(AVCodecContext *avctx, AVSubtitle *sub,
                            int *got_sub_ptr,
                            AVPacket *avpkt)
{
    int ret;

    avctx->pkt = avpkt;
    *got_sub_ptr = 0;
    avcodec_get_subtitle_defaults(sub);
    ret = avctx->codec->decode(avctx, sub, got_sub_ptr, avpkt);
    if (*got_sub_ptr)
        avctx->frame_number++;
    return ret;
}

void avsubtitle_free(AVSubtitle *sub)
{
    int i;

    for (i = 0; i < sub->num_rects; i++)
    {
        av_freep(&sub->rects[i]->pict.data[0]);
        av_freep(&sub->rects[i]->pict.data[1]);
        av_freep(&sub->rects[i]->pict.data[2]);
        av_freep(&sub->rects[i]->pict.data[3]);
        av_freep(&sub->rects[i]->text);
        av_freep(&sub->rects[i]->ass);
        av_freep(&sub->rects[i]);
    }

    av_freep(&sub->rects);

    memset(sub, 0, sizeof(AVSubtitle));
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
        ff_thread_free(avctx);
    if (avctx->codec && avctx->codec->close)
        avctx->codec->close(avctx);
    avcodec_default_free_buffers(avctx);
    avctx->coded_frame = NULL;
    av_freep(&avctx->internal);
    if (avctx->codec && avctx->codec->priv_class)
        av_opt_free(avctx->priv_data);
    av_opt_free(avctx);
    av_freep(&avctx->priv_data);
    if(avctx->codec && avctx->codec->encode)
        av_freep(&avctx->extradata);
    avctx->codec = NULL;
    avctx->active_thread_type = 0;
    entangled_thread_counter--;

    /* Release any user-supplied mutex. */
    if (ff_lockmgr_cb) {
        (*ff_lockmgr_cb)(&codec_mutex, AV_LOCK_RELEASE);
    }
    return 0;
}

static enum CodecID remap_deprecated_codec_id(enum CodecID id)
{
    switch(id){
        case CODEC_ID_G723_1_DEPRECATED : return CODEC_ID_G723_1;
        case CODEC_ID_G729_DEPRECATED   : return CODEC_ID_G729;
        case CODEC_ID_UTVIDEO_DEPRECATED: return CODEC_ID_UTVIDEO;
        default                         : return id;
    }
}

AVCodec *avcodec_find_encoder(enum CodecID id)
{
    AVCodec *p, *experimental=NULL;
    p = first_avcodec;
    id= remap_deprecated_codec_id(id);
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
    AVCodec *p, *experimental=NULL;
    p = first_avcodec;
    id= remap_deprecated_codec_id(id);
    while (p) {
        if (p->decode != NULL && p->id == id) {
            if (p->capabilities & CODEC_CAP_EXPERIMENTAL && !experimental) {
                experimental = p;
            } else
                return p;
        }
        p = p->next;
    }
    return experimental;
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

const char *avcodec_get_name(enum CodecID id)
{
    AVCodec *codec;

#if !CONFIG_SMALL
    switch (id) {
#include "libavcodec/codec_names.h"
    }
    av_log(NULL, AV_LOG_WARNING, "Codec 0x%x is not in the full list.\n", id);
#endif
    codec = avcodec_find_decoder(id);
    if (codec)
        return codec->name;
    codec = avcodec_find_encoder(id);
    if (codec)
        return codec->name;
    return "unknown_codec";
}

size_t av_get_codec_tag_string(char *buf, size_t buf_size, unsigned int codec_tag)
{
    int i, len, ret = 0;

    for (i = 0; i < 4; i++) {
        len = snprintf(buf, buf_size,
                       isprint(codec_tag&0xFF) ? "%c" : "[%d]", codec_tag&0xFF);
        buf      += len;
        buf_size  = buf_size > len ? buf_size - len : 0;
        ret      += len;
        codec_tag>>=8;
    }
    return ret;
}

void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode)
{
    const char *codec_type;
    const char *codec_name;
    const char *profile = NULL;
    AVCodec *p;
    int bitrate;
    AVRational display_aspect_ratio;

    if (!buf || buf_size <= 0)
        return;
    codec_type = av_get_media_type_string(enc->codec_type);
    codec_name = avcodec_get_name(enc->codec_id);
    if (enc->profile != FF_PROFILE_UNKNOWN) {
        p = encode ? avcodec_find_encoder(enc->codec_id) :
                     avcodec_find_decoder(enc->codec_id);
        if (p)
            profile = av_get_profile_name(p, enc->profile);
    }

    snprintf(buf, buf_size, "%s: %s%s", codec_type ? codec_type : "unknown",
             codec_name, enc->mb_decision ? " (hq)" : "");
    buf[0] ^= 'a' ^ 'A'; /* first letter in uppercase */
    if (profile)
        snprintf(buf + strlen(buf), buf_size - strlen(buf), " (%s)", profile);
    if (enc->codec_tag) {
        char tag_buf[32];
        av_get_codec_tag_string(tag_buf, sizeof(tag_buf), enc->codec_tag);
        snprintf(buf + strlen(buf), buf_size - strlen(buf),
                 " (%s / 0x%04X)", tag_buf, enc->codec_tag);
    }
    switch(enc->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (enc->pix_fmt != PIX_FMT_NONE) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %s",
                     av_get_pix_fmt_name(enc->pix_fmt));
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
                         " [SAR %d:%d DAR %d:%d]",
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
        if (enc->sample_rate) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %d Hz", enc->sample_rate);
        }
        av_strlcat(buf, ", ", buf_size);
        av_get_channel_layout_string(buf + strlen(buf), buf_size - strlen(buf), enc->channels, enc->channel_layout);
        if (enc->sample_fmt != AV_SAMPLE_FMT_NONE) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %s", av_get_sample_fmt_name(enc->sample_fmt));
        }
        break;
    default:
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

const char *av_get_profile_name(const AVCodec *codec, int profile)
{
    const AVProfile *p;
    if (profile == FF_PROFILE_UNKNOWN || !codec->profiles)
        return NULL;

    for (p = codec->profiles; p->profile != FF_PROFILE_UNKNOWN; p++)
        if (p->profile == profile)
            return p->name;

    return NULL;
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

void avcodec_flush_buffers(AVCodecContext *avctx)
{
    if(HAVE_THREADS && avctx->active_thread_type&FF_THREAD_FRAME)
        ff_thread_flush(avctx);
    else if(avctx->codec->flush)
        avctx->codec->flush(avctx);
}

void avcodec_default_free_buffers(AVCodecContext *s){
    AVCodecInternal *avci = s->internal;
    int i, j;

    if (!avci->buffer)
        return;

    if (avci->buffer_count)
        av_log(s, AV_LOG_WARNING, "Found %i unreleased buffers!\n",
               avci->buffer_count);
    for(i=0; i<INTERNAL_BUFFER_SIZE; i++){
        InternalBuffer *buf = &avci->buffer[i];
        for(j=0; j<4; j++){
            av_freep(&buf->base[j]);
            buf->data[j]= NULL;
        }
    }
    av_freep(&avci->buffer);

    avci->buffer_count=0;
}

#if FF_API_OLD_FF_PICT_TYPES
char av_get_pict_type_char(int pict_type){
    return av_get_picture_type_char(pict_type);
}
#endif

int av_get_bits_per_sample(enum CodecID codec_id){
    switch(codec_id){
    case CODEC_ID_ADPCM_SBPRO_2:
        return 2;
    case CODEC_ID_ADPCM_SBPRO_3:
        return 3;
    case CODEC_ID_ADPCM_SBPRO_4:
    case CODEC_ID_ADPCM_CT:
    case CODEC_ID_ADPCM_IMA_WAV:
    case CODEC_ID_ADPCM_IMA_QT:
    case CODEC_ID_ADPCM_SWF:
    case CODEC_ID_ADPCM_MS:
    case CODEC_ID_ADPCM_YAMAHA:
        return 4;
    case CODEC_ID_ADPCM_G722:
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

#if FF_API_OLD_SAMPLE_FMT
int av_get_bits_per_sample_format(enum AVSampleFormat sample_fmt) {
    return av_get_bytes_per_sample(sample_fmt) << 3;
}
#endif

#if !HAVE_THREADS
int ff_thread_init(AVCodecContext *s){
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

int ff_match_2uint16(const uint16_t (*tab)[2], int size, int a, int b){
    int i;
    for(i=0; i<size && !(tab[i][0]==a && tab[i][1]==b); i++);
    return i;
}

void av_log_missing_feature(void *avc, const char *feature, int want_sample)
{
    av_log(avc, AV_LOG_WARNING, "%s not implemented. Update your FFmpeg "
            "version to the newest one from Git. If the problem still "
            "occurs, it means that your file has a feature which has not "
            "been implemented.\n", feature);
    if(want_sample)
        av_log_ask_for_sample(avc, NULL);
}

void av_log_ask_for_sample(void *avc, const char *msg, ...)
{
    va_list argument_list;

    va_start(argument_list, msg);

    if (msg)
        av_vlog(avc, AV_LOG_WARNING, msg, argument_list);
    av_log(avc, AV_LOG_WARNING, "If you want to help, upload a sample "
            "of this file to ftp://upload.ffmpeg.org/MPlayer/incoming/ "
            "and contact the ffmpeg-devel mailing list.\n");

    va_end(argument_list);
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
        if (ff_lockmgr_cb(&avformat_mutex, AV_LOCK_DESTROY))
            return -1;
    }

    ff_lockmgr_cb = cb;

    if (ff_lockmgr_cb) {
        if (ff_lockmgr_cb(&codec_mutex, AV_LOCK_CREATE))
            return -1;
        if (ff_lockmgr_cb(&avformat_mutex, AV_LOCK_CREATE))
            return -1;
    }
    return 0;
}

int avpriv_lock_avformat(void)
{
    if (ff_lockmgr_cb) {
        if ((*ff_lockmgr_cb)(&avformat_mutex, AV_LOCK_OBTAIN))
            return -1;
    }
    return 0;
}

int avpriv_unlock_avformat(void)
{
    if (ff_lockmgr_cb) {
        if ((*ff_lockmgr_cb)(&avformat_mutex, AV_LOCK_RELEASE))
            return -1;
    }
    return 0;
}

unsigned int avpriv_toupper4(unsigned int x)
{
    return     toupper( x     &0xFF)
            + (toupper((x>>8 )&0xFF)<<8 )
            + (toupper((x>>16)&0xFF)<<16)
            + (toupper((x>>24)&0xFF)<<24);
}

#if !HAVE_THREADS

int ff_thread_get_buffer(AVCodecContext *avctx, AVFrame *f)
{
    f->owner = avctx;

    ff_init_buffer_info(avctx, f);

    return avctx->get_buffer(avctx, f);
}

void ff_thread_release_buffer(AVCodecContext *avctx, AVFrame *f)
{
    f->owner->release_buffer(f->owner, f);
}

void ff_thread_finish_setup(AVCodecContext *avctx)
{
}

void ff_thread_report_progress(AVFrame *f, int progress, int field)
{
}

void ff_thread_await_progress(AVFrame *f, int progress, int field)
{
}

#endif

#if FF_API_THREAD_INIT
int avcodec_thread_init(AVCodecContext *s, int thread_count)
{
    s->thread_count = thread_count;
    return ff_thread_init(s);
}
#endif

enum AVMediaType avcodec_get_type(enum CodecID codec_id)
{
    AVCodec *c= avcodec_find_decoder(codec_id);
    if(!c)
        c= avcodec_find_encoder(codec_id);
    if(c)
        return c->type;

    if (codec_id <= CODEC_ID_NONE)
        return AVMEDIA_TYPE_UNKNOWN;
    else if (codec_id < CODEC_ID_FIRST_AUDIO)
        return AVMEDIA_TYPE_VIDEO;
    else if (codec_id < CODEC_ID_FIRST_SUBTITLE)
        return AVMEDIA_TYPE_AUDIO;
    else if (codec_id < CODEC_ID_FIRST_UNKNOWN)
        return AVMEDIA_TYPE_SUBTITLE;

    return AVMEDIA_TYPE_UNKNOWN;
}
