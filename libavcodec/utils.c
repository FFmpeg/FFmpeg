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

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/crc.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/audioconvert.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/dict.h"
#include "libavutil/avassert.h"
#include "avcodec.h"
#include "dsputil.h"
#include "libavutil/opt.h"
#include "imgconvert.h"
#include "thread.h"
#include "frame_thread_encoder.h"
#include "audioconvert.h"
#include "internal.h"
#include "bytestream.h"
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

static inline int ff_fast_malloc(void *ptr, unsigned int *size, size_t min_size, int zero_realloc)
{
    void **p = ptr;
    if (min_size < *size)
        return 0;
    min_size= FFMAX(17*min_size/16 + 32, min_size);
    av_free(*p);
    *p = zero_realloc ? av_mallocz(min_size) : av_malloc(min_size);
    if (!*p) min_size = 0;
    *size= min_size;
    return 1;
}

void av_fast_malloc(void *ptr, unsigned int *size, size_t min_size)
{
    ff_fast_malloc(ptr, size, min_size, 0);
}

void av_fast_padded_malloc(void *ptr, unsigned int *size, size_t min_size)
{
    uint8_t **p = ptr;
    if (min_size > SIZE_MAX - FF_INPUT_BUFFER_PADDING_SIZE) {
        av_freep(p);
        *size = 0;
        return;
    }
    if (!ff_fast_malloc(p, size, min_size + FF_INPUT_BUFFER_PADDING_SIZE, 1))
        memset(*p + min_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
}

void av_fast_padded_mallocz(void *ptr, unsigned int *size, size_t min_size)
{
    uint8_t **p = ptr;
    if (min_size > SIZE_MAX - FF_INPUT_BUFFER_PADDING_SIZE) {
        av_freep(p);
        *size = 0;
        return;
    }
    if (!ff_fast_malloc(p, size, min_size + FF_INPUT_BUFFER_PADDING_SIZE, 1))
        memset(*p, 0, min_size + FF_INPUT_BUFFER_PADDING_SIZE);
}

/* encoder management */
static AVCodec *first_avcodec = NULL;

AVCodec *av_codec_next(const AVCodec *c)
{
    if(c) return c->next;
    else  return first_avcodec;
}

static void avcodec_init(void)
{
    static int initialized = 0;

    if (initialized != 0)
        return;
    initialized = 1;

    ff_dsputil_static_init();
}

int av_codec_is_encoder(const AVCodec *codec)
{
    return codec && (codec->encode_sub || codec->encode2);
}

int av_codec_is_decoder(const AVCodec *codec)
{
    return codec && codec->decode;
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

void avcodec_align_dimensions2(AVCodecContext *s, int *width, int *height,
                               int linesize_align[AV_NUM_DATA_POINTERS])
{
    int i;
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
    case PIX_FMT_YUVA422P:
    case PIX_FMT_YUVA444P:
    case PIX_FMT_YUV420P9LE:
    case PIX_FMT_YUV420P9BE:
    case PIX_FMT_YUV420P10LE:
    case PIX_FMT_YUV420P10BE:
    case PIX_FMT_YUV420P12LE:
    case PIX_FMT_YUV420P12BE:
    case PIX_FMT_YUV420P14LE:
    case PIX_FMT_YUV420P14BE:
    case PIX_FMT_YUV422P9LE:
    case PIX_FMT_YUV422P9BE:
    case PIX_FMT_YUV422P10LE:
    case PIX_FMT_YUV422P10BE:
    case PIX_FMT_YUV422P12LE:
    case PIX_FMT_YUV422P12BE:
    case PIX_FMT_YUV422P14LE:
    case PIX_FMT_YUV422P14BE:
    case PIX_FMT_YUV444P9LE:
    case PIX_FMT_YUV444P9BE:
    case PIX_FMT_YUV444P10LE:
    case PIX_FMT_YUV444P10BE:
    case PIX_FMT_YUV444P12LE:
    case PIX_FMT_YUV444P12BE:
    case PIX_FMT_YUV444P14LE:
    case PIX_FMT_YUV444P14BE:
    case PIX_FMT_GBRP9LE:
    case PIX_FMT_GBRP9BE:
    case PIX_FMT_GBRP10LE:
    case PIX_FMT_GBRP10BE:
    case PIX_FMT_GBRP12LE:
    case PIX_FMT_GBRP12BE:
    case PIX_FMT_GBRP14LE:
    case PIX_FMT_GBRP14BE:
        w_align = 16; //FIXME assume 16 pixel per macroblock
        h_align = 16 * 2; // interlaced needs 2 macroblocks height
        break;
    case PIX_FMT_YUV411P:
    case PIX_FMT_UYYVYY411:
        w_align=32;
        h_align=8;
        break;
    case PIX_FMT_YUV410P:
        if(s->codec_id == AV_CODEC_ID_SVQ1){
            w_align=64;
            h_align=64;
        }
    case PIX_FMT_RGB555:
        if(s->codec_id == AV_CODEC_ID_RPZA){
            w_align=4;
            h_align=4;
        }
    case PIX_FMT_PAL8:
    case PIX_FMT_BGR8:
    case PIX_FMT_RGB8:
        if(s->codec_id == AV_CODEC_ID_SMC){
            w_align=4;
            h_align=4;
        }
        break;
    case PIX_FMT_BGR24:
        if((s->codec_id == AV_CODEC_ID_MSZH) || (s->codec_id == AV_CODEC_ID_ZLIB)){
            w_align=4;
            h_align=4;
        }
        break;
    default:
        w_align= 1;
        h_align= 1;
        break;
    }

    if(s->codec_id == AV_CODEC_ID_IFF_ILBM || s->codec_id == AV_CODEC_ID_IFF_BYTERUN1){
        w_align= FFMAX(w_align, 8);
    }

    *width = FFALIGN(*width , w_align);
    *height= FFALIGN(*height, h_align);
    if(s->codec_id == AV_CODEC_ID_H264 || s->lowres)
        *height+=2; // some of the optimized chroma MC reads one line too much
                    // which is also done in mpeg decoders with lowres > 0

    for (i = 0; i < 4; i++)
        linesize_align[i] = STRIDE_ALIGN;
}

void avcodec_align_dimensions(AVCodecContext *s, int *width, int *height){
    int chroma_shift = av_pix_fmt_descriptors[s->pix_fmt].log2_chroma_w;
    int linesize_align[AV_NUM_DATA_POINTERS];
    int align;
    avcodec_align_dimensions2(s, width, height, linesize_align);
    align = FFMAX(linesize_align[0], linesize_align[3]);
    linesize_align[1] <<= chroma_shift;
    linesize_align[2] <<= chroma_shift;
    align = FFMAX3(align, linesize_align[1], linesize_align[2]);
    *width=FFALIGN(*width, align);
}

void ff_init_buffer_info(AVCodecContext *s, AVFrame *frame)
{
    if (s->pkt) {
        frame->pkt_pts = s->pkt->pts;
        frame->pkt_pos = s->pkt->pos;
        frame->pkt_duration = s->pkt->duration;
    } else {
        frame->pkt_pts = AV_NOPTS_VALUE;
        frame->pkt_pos = -1;
        frame->pkt_duration = 0;
    }
    frame->reordered_opaque = s->reordered_opaque;

    switch (s->codec->type) {
    case AVMEDIA_TYPE_VIDEO:
        frame->width               = s->width;
        frame->height              = s->height;
        frame->format              = s->pix_fmt;
        frame->sample_aspect_ratio = s->sample_aspect_ratio;
        break;
    case AVMEDIA_TYPE_AUDIO:
        frame->sample_rate    = s->sample_rate;
        frame->format         = s->sample_fmt;
        frame->channel_layout = s->channel_layout;
        frame->channels       = s->channels;
        break;
    }
}

int avcodec_fill_audio_frame(AVFrame *frame, int nb_channels,
                             enum AVSampleFormat sample_fmt, const uint8_t *buf,
                             int buf_size, int align)
{
    int ch, planar, needed_size, ret = 0;

    needed_size = av_samples_get_buffer_size(NULL, nb_channels,
                                             frame->nb_samples, sample_fmt,
                                             align);
    if (buf_size < needed_size)
        return AVERROR(EINVAL);

    planar = av_sample_fmt_is_planar(sample_fmt);
    if (planar && nb_channels > AV_NUM_DATA_POINTERS) {
        if (!(frame->extended_data = av_mallocz(nb_channels *
                                                sizeof(*frame->extended_data))))
            return AVERROR(ENOMEM);
    } else {
        frame->extended_data = frame->data;
    }

    if ((ret = av_samples_fill_arrays(frame->extended_data, &frame->linesize[0],
                                      (uint8_t *)(intptr_t)buf, nb_channels, frame->nb_samples,
                                      sample_fmt, align)) < 0) {
        if (frame->extended_data != frame->data)
            av_freep(&frame->extended_data);
        return ret;
    }
    if (frame->extended_data != frame->data) {
        for (ch = 0; ch < AV_NUM_DATA_POINTERS; ch++)
            frame->data[ch] = frame->extended_data[ch];
    }

    return ret;
}

static int audio_get_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    AVCodecInternal *avci = avctx->internal;
    InternalBuffer *buf;
    int buf_size, ret;

    buf_size = av_samples_get_buffer_size(NULL, avctx->channels,
                                          frame->nb_samples, avctx->sample_fmt,
                                          0);
    if (buf_size < 0)
        return AVERROR(EINVAL);

    /* allocate InternalBuffer if needed */
    if (!avci->buffer) {
        avci->buffer = av_mallocz(sizeof(InternalBuffer));
        if (!avci->buffer)
            return AVERROR(ENOMEM);
    }
    buf = avci->buffer;

    /* if there is a previously-used internal buffer, check its size and
       channel count to see if we can reuse it */
    if (buf->extended_data) {
        /* if current buffer is too small, free it */
        if (buf->extended_data[0] && buf_size > buf->audio_data_size) {
            av_free(buf->extended_data[0]);
            if (buf->extended_data != buf->data)
                av_freep(&buf->extended_data);
            buf->extended_data = NULL;
            buf->data[0] = NULL;
        }
        /* if number of channels has changed, reset and/or free extended data
           pointers but leave data buffer in buf->data[0] for reuse */
        if (buf->nb_channels != avctx->channels) {
            if (buf->extended_data != buf->data)
                av_free(buf->extended_data);
            buf->extended_data = NULL;
        }
    }

    /* if there is no previous buffer or the previous buffer cannot be used
       as-is, allocate a new buffer and/or rearrange the channel pointers */
    if (!buf->extended_data) {
        if (!buf->data[0]) {
            if (!(buf->data[0] = av_mallocz(buf_size)))
                return AVERROR(ENOMEM);
            buf->audio_data_size = buf_size;
        }
        if ((ret = avcodec_fill_audio_frame(frame, avctx->channels,
                                            avctx->sample_fmt, buf->data[0],
                                            buf->audio_data_size, 0)))
            return ret;

        if (frame->extended_data == frame->data)
            buf->extended_data = buf->data;
        else
            buf->extended_data = frame->extended_data;
        memcpy(buf->data, frame->data, sizeof(frame->data));
        buf->linesize[0] = frame->linesize[0];
        buf->nb_channels = avctx->channels;
    } else {
        /* copy InternalBuffer info to the AVFrame */
        frame->extended_data = buf->extended_data;
        frame->linesize[0]   = buf->linesize[0];
        memcpy(frame->data, buf->data, sizeof(frame->data));
    }

    frame->type          = FF_BUFFER_TYPE_INTERNAL;
    ff_init_buffer_info(avctx, frame);

    if (avctx->debug & FF_DEBUG_BUFFERS)
        av_log(avctx, AV_LOG_DEBUG, "default_get_buffer called on frame %p, "
               "internal audio buffer used\n", frame);

    return 0;
}

static int video_get_buffer(AVCodecContext *s, AVFrame *pic)
{
    int i;
    int w= s->width;
    int h= s->height;
    InternalBuffer *buf;
    AVCodecInternal *avci = s->internal;

    if(pic->data[0]!=NULL) {
        av_log(s, AV_LOG_ERROR, "pic->data[0]!=NULL in avcodec_default_get_buffer\n");
        return -1;
    }
    if(avci->buffer_count >= INTERNAL_BUFFER_SIZE) {
        av_log(s, AV_LOG_ERROR, "buffer_count overflow (missing release_buffer?)\n");
        return -1;
    }

    if(av_image_check_size(w, h, 0, s) || s->pix_fmt<0) {
        av_log(s, AV_LOG_ERROR, "video_get_buffer: image parameters invalid\n");
        return -1;
    }

    if (!avci->buffer) {
        avci->buffer = av_mallocz((INTERNAL_BUFFER_SIZE+1) *
                                  sizeof(InternalBuffer));
    }

    buf = &avci->buffer[avci->buffer_count];

    if(buf->base[0] && (buf->width != w || buf->height != h || buf->pix_fmt != s->pix_fmt)){
        for (i = 0; i < AV_NUM_DATA_POINTERS; i++) {
            av_freep(&buf->base[i]);
            buf->data[i]= NULL;
        }
    }

    if (!buf->base[0]) {
        int h_chroma_shift, v_chroma_shift;
        int size[4] = {0};
        int tmpsize;
        int unaligned;
        AVPicture picture;
        int stride_align[AV_NUM_DATA_POINTERS];
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

        memset(buf->base, 0, sizeof(buf->base));
        memset(buf->data, 0, sizeof(buf->data));

        for(i=0; i<4 && size[i]; i++){
            const int h_shift= i==0 ? 0 : h_chroma_shift;
            const int v_shift= i==0 ? 0 : v_chroma_shift;

            buf->linesize[i]= picture.linesize[i];

            buf->base[i]= av_malloc(size[i]+16); //FIXME 16
            if(buf->base[i]==NULL)
                return AVERROR(ENOMEM);
            memset(buf->base[i], 128, size[i]);

            // no edge if EDGE EMU or not planar YUV
            if((s->flags&CODEC_FLAG_EMU_EDGE) || !size[2])
                buf->data[i] = buf->base[i];
            else
                buf->data[i] = buf->base[i] + FFALIGN((buf->linesize[i]*EDGE_WIDTH>>v_shift) + (pixel_size*EDGE_WIDTH>>h_shift), stride_align[i]);
        }
        for (; i < AV_NUM_DATA_POINTERS; i++) {
            buf->base[i] = buf->data[i] = NULL;
            buf->linesize[i] = 0;
        }
        if(size[1] && !size[2])
            ff_set_systematic_pal2((uint32_t*)buf->data[1], s->pix_fmt);
        buf->width  = s->width;
        buf->height = s->height;
        buf->pix_fmt= s->pix_fmt;
    }
    pic->type= FF_BUFFER_TYPE_INTERNAL;

    for (i = 0; i < AV_NUM_DATA_POINTERS; i++) {
        pic->base[i]= buf->base[i];
        pic->data[i]= buf->data[i];
        pic->linesize[i]= buf->linesize[i];
    }
    pic->extended_data = pic->data;
    avci->buffer_count++;
    pic->width  = buf->width;
    pic->height = buf->height;
    pic->format = buf->pix_fmt;

    ff_init_buffer_info(s, pic);

    if(s->debug&FF_DEBUG_BUFFERS)
        av_log(s, AV_LOG_DEBUG, "default_get_buffer called on pic %p, %d "
               "buffers used\n", pic, avci->buffer_count);

    return 0;
}

int avcodec_default_get_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        return video_get_buffer(avctx, frame);
    case AVMEDIA_TYPE_AUDIO:
        return audio_get_buffer(avctx, frame);
    default:
        return -1;
    }
}

void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic){
    int i;
    InternalBuffer *buf, *last;
    AVCodecInternal *avci = s->internal;

    av_assert0(s->codec_type == AVMEDIA_TYPE_VIDEO);

    assert(pic->type==FF_BUFFER_TYPE_INTERNAL);
    assert(avci->buffer_count);

    if (avci->buffer) {
        buf = NULL; /* avoids warning */
        for (i = 0; i < avci->buffer_count; i++) { //just 3-5 checks so is not worth to optimize
            buf = &avci->buffer[i];
            if (buf->data[0] == pic->data[0])
                break;
        }
        av_assert0(i < avci->buffer_count);
        avci->buffer_count--;
        last = &avci->buffer[avci->buffer_count];

        if (buf != last)
            FFSWAP(InternalBuffer, *buf, *last);
    }

    for (i = 0; i < AV_NUM_DATA_POINTERS; i++) {
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

    av_assert0(s->codec_type == AVMEDIA_TYPE_VIDEO);

    if (pic->data[0] && (pic->width != s->width || pic->height != s->height || pic->format != s->pix_fmt)) {
        av_log(s, AV_LOG_WARNING, "Picture changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s in reget buffer()\n",
               pic->width, pic->height, av_get_pix_fmt_name(pic->format), s->width, s->height, av_get_pix_fmt_name(s->pix_fmt));
        s->release_buffer(s, pic);
    }

    ff_init_buffer_info(s, pic);

    /* If no picture return a new buffer */
    if(pic->data[0] == NULL) {
        /* We will copy from buffer, so must be readable */
        pic->buffer_hints |= FF_BUFFER_HINTS_READABLE;
        return s->get_buffer(s, pic);
    }

    assert(s->pix_fmt == pic->format);

    /* If internal buffer type return the same buffer */
    if(pic->type == FF_BUFFER_TYPE_INTERNAL) {
        return 0;
    }

    /*
     * Not internal type and reget_buffer not overridden, emulate cr buffer
     */
    temp_pic = *pic;
    for(i = 0; i < AV_NUM_DATA_POINTERS; i++)
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

    pic->pts = pic->pkt_dts = pic->pkt_pts = pic->best_effort_timestamp = AV_NOPTS_VALUE;
    pic->pkt_duration = 0;
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

#define MAKE_ACCESSORS(str, name, type, field) \
    type av_##name##_get_##field(const str *s) { return s->field; } \
    void av_##name##_set_##field(str *s, type v) { s->field = v; }

MAKE_ACCESSORS(AVFrame, frame, int64_t, best_effort_timestamp)
MAKE_ACCESSORS(AVFrame, frame, int64_t, pkt_duration)
MAKE_ACCESSORS(AVFrame, frame, int64_t, pkt_pos)
MAKE_ACCESSORS(AVFrame, frame, int64_t, channel_layout)
MAKE_ACCESSORS(AVFrame, frame, int,     channels)
MAKE_ACCESSORS(AVFrame, frame, int,     sample_rate)
MAKE_ACCESSORS(AVFrame, frame, AVDictionary *, metadata)
MAKE_ACCESSORS(AVFrame, frame, int,     decode_error_flags)

MAKE_ACCESSORS(AVCodecContext, codec, AVRational, pkt_timebase)
MAKE_ACCESSORS(AVCodecContext, codec, const AVCodecDescriptor *, codec_descriptor)

static void avcodec_get_subtitle_defaults(AVSubtitle *sub)
{
    memset(sub, 0, sizeof(*sub));
    sub->pts = AV_NOPTS_VALUE;
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

#if FF_API_AVCODEC_OPEN
int attribute_align_arg avcodec_open(AVCodecContext *avctx, AVCodec *codec)
{
    return avcodec_open2(avctx, codec, NULL);
}
#endif

int attribute_align_arg avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options)
{
    int ret = 0;
    AVDictionary *tmp = NULL;

    if (avcodec_is_open(avctx))
        return 0;

    if ((!codec && !avctx->codec)) {
        av_log(avctx, AV_LOG_ERROR, "No codec provided to avcodec_open2().\n");
        return AVERROR(EINVAL);
    }
    if ((codec && avctx->codec && codec != avctx->codec)) {
        av_log(avctx, AV_LOG_ERROR, "This AVCodecContext was allocated for %s, "
               "but %s passed to avcodec_open2().\n", avctx->codec->name, codec->name);
        return AVERROR(EINVAL);
    }
    if (!codec)
        codec = avctx->codec;

    if (avctx->extradata_size < 0 || avctx->extradata_size >= FF_MAX_EXTRADATA_SIZE)
        return AVERROR(EINVAL);

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
            *(const AVClass**)avctx->priv_data = codec->priv_class;
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

    if (codec->capabilities & CODEC_CAP_EXPERIMENTAL)
        if (avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
            av_log(avctx, AV_LOG_ERROR, "Codec is experimental but experimental codecs are not enabled, try -strict -2\n");
            ret = -1;
            goto free_and_end;
        }

    //We only call avcodec_set_dimensions() for non h264 codecs so as not to overwrite previously setup dimensions
    if(!( avctx->coded_width && avctx->coded_height && avctx->width && avctx->height && avctx->codec_id == AV_CODEC_ID_H264)){
    if(avctx->coded_width && avctx->coded_height)
        avcodec_set_dimensions(avctx, avctx->coded_width, avctx->coded_height);
    else if(avctx->width && avctx->height)
        avcodec_set_dimensions(avctx, avctx->width, avctx->height);
    }

    if ((avctx->coded_width || avctx->coded_height || avctx->width || avctx->height)
        && (  av_image_check_size(avctx->coded_width, avctx->coded_height, 0, avctx) < 0
           || av_image_check_size(avctx->width,       avctx->height,       0, avctx) < 0)) {
        av_log(avctx, AV_LOG_WARNING, "ignoring invalid width/height values\n");
        avcodec_set_dimensions(avctx, 0, 0);
    }

    /* if the decoder init function was already called previously,
       free the already allocated subtitle_header before overwriting it */
    if (av_codec_is_decoder(codec))
        av_freep(&avctx->subtitle_header);

#define SANE_NB_CHANNELS 128U
    if (avctx->channels > SANE_NB_CHANNELS) {
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    avctx->codec = codec;
    if ((avctx->codec_type == AVMEDIA_TYPE_UNKNOWN || avctx->codec_type == codec->type) &&
        avctx->codec_id == AV_CODEC_ID_NONE) {
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
    avctx->codec_descriptor = avcodec_descriptor_get(avctx->codec_id);

    if (avctx->codec_type == AVMEDIA_TYPE_AUDIO &&
        (!avctx->time_base.num || !avctx->time_base.den)) {
        avctx->time_base.num = 1;
        avctx->time_base.den = avctx->sample_rate;
    }

    if (!HAVE_THREADS)
        av_log(avctx, AV_LOG_WARNING, "Warning: not compiled with thread support, using thread emulation\n");

    if (HAVE_THREADS) {
        entangled_thread_counter--; //we will instanciate a few encoders thus kick the counter to prevent false detection of a problem
        ret = ff_frame_thread_encoder_init(avctx, options ? *options : NULL);
        entangled_thread_counter++;
        if (ret < 0)
            goto free_and_end;
    }

    if (HAVE_THREADS && !avctx->thread_opaque
        && !(avctx->internal->frame_thread_encoder && (avctx->active_thread_type&FF_THREAD_FRAME))) {
        ret = ff_thread_init(avctx);
        if (ret < 0) {
            goto free_and_end;
        }
    }
    if (!HAVE_THREADS && !(codec->capabilities & CODEC_CAP_AUTO_THREADS))
        avctx->thread_count = 1;

    if (avctx->codec->max_lowres < avctx->lowres || avctx->lowres < 0) {
        av_log(avctx, AV_LOG_ERROR, "The maximum value for lowres supported by the decoder is %d\n",
               avctx->codec->max_lowres);
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    if (av_codec_is_encoder(avctx->codec)) {
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
        if (avctx->codec->pix_fmts) {
            for (i = 0; avctx->codec->pix_fmts[i] != PIX_FMT_NONE; i++)
                if (avctx->pix_fmt == avctx->codec->pix_fmts[i])
                    break;
            if (avctx->codec->pix_fmts[i] == PIX_FMT_NONE
                && !((avctx->codec_id == AV_CODEC_ID_MJPEG || avctx->codec_id == AV_CODEC_ID_LJPEG)
                     && avctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL)) {
                av_log(avctx, AV_LOG_ERROR, "Specified pix_fmt is not supported\n");
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

    if(avctx->codec->init && (!(avctx->active_thread_type&FF_THREAD_FRAME) || avctx->internal->frame_thread_encoder)){
        ret = avctx->codec->init(avctx);
        if (ret < 0) {
            goto free_and_end;
        }
    }

    ret=0;

    if (av_codec_is_decoder(avctx->codec)) {
        if (!avctx->bit_rate)
            avctx->bit_rate = get_bit_rate(avctx);
        /* validate channel layout from the decoder */
        if (avctx->channel_layout &&
            av_get_channel_layout_nb_channels(avctx->channel_layout) != avctx->channels) {
            av_log(avctx, AV_LOG_WARNING, "channel layout does not match number of channels\n");
            avctx->channel_layout = 0;
        }
    }
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

int ff_alloc_packet2(AVCodecContext *avctx, AVPacket *avpkt, int size)
{
    if (size < 0 || avpkt->size < 0 || size > INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Size %d invalid\n", size);
        return AVERROR(EINVAL);
    }

    if (avctx) {
        av_assert0(!avpkt->data || avpkt->data != avctx->internal->byte_buffer);
        if (!avpkt->data || avpkt->size < size) {
            av_fast_padded_malloc(&avctx->internal->byte_buffer, &avctx->internal->byte_buffer_size, size);
            avpkt->data = avctx->internal->byte_buffer;
            avpkt->size = avctx->internal->byte_buffer_size;
            avpkt->destruct = NULL;
        }
    }

    if (avpkt->data) {
        void *destruct = avpkt->destruct;

        if (avpkt->size < size) {
            av_log(avctx, AV_LOG_ERROR, "User packet is too small (%d < %d)\n", avpkt->size, size);
            return AVERROR(EINVAL);
        }

        av_init_packet(avpkt);
        avpkt->destruct = destruct;
        avpkt->size = size;
        return 0;
    } else {
        int ret = av_new_packet(avpkt, size);
        if (ret < 0)
            av_log(avctx, AV_LOG_ERROR, "Failed to allocate packet of size %d\n", size);
        return ret;
    }
}

int ff_alloc_packet(AVPacket *avpkt, int size)
{
    return ff_alloc_packet2(NULL, avpkt, size);
}

/**
 * Pad last frame with silence.
 */
static int pad_last_frame(AVCodecContext *s, AVFrame **dst, const AVFrame *src)
{
    AVFrame *frame = NULL;
    uint8_t *buf   = NULL;
    int ret;

    if (!(frame = avcodec_alloc_frame()))
        return AVERROR(ENOMEM);
    *frame = *src;

    if ((ret = av_samples_get_buffer_size(&frame->linesize[0], s->channels,
                                          s->frame_size, s->sample_fmt, 0)) < 0)
        goto fail;

    if (!(buf = av_malloc(ret))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    frame->nb_samples = s->frame_size;
    if ((ret = avcodec_fill_audio_frame(frame, s->channels, s->sample_fmt,
                                        buf, ret, 0)) < 0)
        goto fail;
    if ((ret = av_samples_copy(frame->extended_data, src->extended_data, 0, 0,
                               src->nb_samples, s->channels, s->sample_fmt)) < 0)
        goto fail;
    if ((ret = av_samples_set_silence(frame->extended_data, src->nb_samples,
                                      frame->nb_samples - src->nb_samples,
                                      s->channels, s->sample_fmt)) < 0)
        goto fail;

    *dst = frame;

    return 0;

fail:
    if (frame->extended_data != frame->data)
        av_freep(&frame->extended_data);
    av_freep(&buf);
    av_freep(&frame);
    return ret;
}

int attribute_align_arg avcodec_encode_audio2(AVCodecContext *avctx,
                                              AVPacket *avpkt,
                                              const AVFrame *frame,
                                              int *got_packet_ptr)
{
    AVFrame tmp;
    AVFrame *padded_frame = NULL;
    int ret;
    AVPacket user_pkt = *avpkt;
    int needs_realloc = !user_pkt.data;

    *got_packet_ptr = 0;

    if (!(avctx->codec->capabilities & CODEC_CAP_DELAY) && !frame) {
        av_free_packet(avpkt);
        av_init_packet(avpkt);
        return 0;
    }

    /* ensure that extended_data is properly set */
    if (frame && !frame->extended_data) {
        if (av_sample_fmt_is_planar(avctx->sample_fmt) &&
            avctx->channels > AV_NUM_DATA_POINTERS) {
            av_log(avctx, AV_LOG_ERROR, "Encoding to a planar sample format, "
                   "with more than %d channels, but extended_data is not set.\n",
                   AV_NUM_DATA_POINTERS);
            return AVERROR(EINVAL);
        }
        av_log(avctx, AV_LOG_WARNING, "extended_data is not set.\n");

        tmp = *frame;
        tmp.extended_data = tmp.data;
        frame = &tmp;
    }

    /* check for valid frame size */
    if (frame) {
        if (avctx->codec->capabilities & CODEC_CAP_SMALL_LAST_FRAME) {
            if (frame->nb_samples > avctx->frame_size) {
                av_log(avctx, AV_LOG_ERROR, "more samples than frame size (avcodec_encode_audio2)\n");
                return AVERROR(EINVAL);
            }
        } else if (!(avctx->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)) {
            if (frame->nb_samples < avctx->frame_size &&
                !avctx->internal->last_audio_frame) {
                ret = pad_last_frame(avctx, &padded_frame, frame);
                if (ret < 0)
                    return ret;

                frame = padded_frame;
                avctx->internal->last_audio_frame = 1;
            }

            if (frame->nb_samples != avctx->frame_size) {
                av_log(avctx, AV_LOG_ERROR, "nb_samples (%d) != frame_size (%d) (avcodec_encode_audio2)\n", frame->nb_samples, avctx->frame_size);
                return AVERROR(EINVAL);
            }
        }
    }

    ret = avctx->codec->encode2(avctx, avpkt, frame, got_packet_ptr);
    if (!ret) {
        if (*got_packet_ptr) {
            if (!(avctx->codec->capabilities & CODEC_CAP_DELAY)) {
                if (avpkt->pts == AV_NOPTS_VALUE)
                    avpkt->pts = frame->pts;
                if (!avpkt->duration)
                    avpkt->duration = ff_samples_to_time_base(avctx,
                                                              frame->nb_samples);
            }
            avpkt->dts = avpkt->pts;
        } else {
            avpkt->size = 0;
        }
    }
    if (avpkt->data && avpkt->data == avctx->internal->byte_buffer) {
        needs_realloc = 0;
        if (user_pkt.data) {
            if (user_pkt.size >= avpkt->size) {
                memcpy(user_pkt.data, avpkt->data, avpkt->size);
            } else {
                av_log(avctx, AV_LOG_ERROR, "Provided packet is too small, needs to be %d\n", avpkt->size);
                avpkt->size = user_pkt.size;
                ret = -1;
            }
            avpkt->data     = user_pkt.data;
            avpkt->destruct = user_pkt.destruct;
        } else {
            if (av_dup_packet(avpkt) < 0) {
                ret = AVERROR(ENOMEM);
            }
        }
    }

    if (!ret) {
        if (needs_realloc && avpkt->data) {
            uint8_t *new_data = av_realloc(avpkt->data, avpkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (new_data)
                avpkt->data = new_data;
        }

        avctx->frame_number++;
    }

    if (ret < 0 || !*got_packet_ptr) {
        av_free_packet(avpkt);
        av_init_packet(avpkt);
        return ret;
    }

    /* NOTE: if we add any audio encoders which output non-keyframe packets,
             this needs to be moved to the encoders, but for now we can do it
             here to simplify things */
    avpkt->flags |= AV_PKT_FLAG_KEY;

    if (padded_frame) {
        av_freep(&padded_frame->data[0]);
        if (padded_frame->extended_data != padded_frame->data)
            av_freep(&padded_frame->extended_data);
        av_freep(&padded_frame);
    }

    return ret;
}

#if FF_API_OLD_DECODE_AUDIO
int attribute_align_arg avcodec_encode_audio(AVCodecContext *avctx,
                                             uint8_t *buf, int buf_size,
                                             const short *samples)
{
    AVPacket pkt;
    AVFrame frame0;
    AVFrame *frame;
    int ret, samples_size, got_packet;

    av_init_packet(&pkt);
    pkt.data = buf;
    pkt.size = buf_size;

    if (samples) {
        frame = &frame0;
        avcodec_get_frame_defaults(frame);

        if (avctx->frame_size) {
            frame->nb_samples = avctx->frame_size;
        } else {
            /* if frame_size is not set, the number of samples must be
               calculated from the buffer size */
            int64_t nb_samples;
            if (!av_get_bits_per_sample(avctx->codec_id)) {
                av_log(avctx, AV_LOG_ERROR, "avcodec_encode_audio() does not "
                       "support this codec\n");
                return AVERROR(EINVAL);
            }
            nb_samples = (int64_t)buf_size * 8 /
                         (av_get_bits_per_sample(avctx->codec_id) *
                         avctx->channels);
            if (nb_samples >= INT_MAX)
                return AVERROR(EINVAL);
            frame->nb_samples = nb_samples;
        }

        /* it is assumed that the samples buffer is large enough based on the
           relevant parameters */
        samples_size = av_samples_get_buffer_size(NULL, avctx->channels,
                                                  frame->nb_samples,
                                                  avctx->sample_fmt, 1);
        if ((ret = avcodec_fill_audio_frame(frame, avctx->channels,
                                            avctx->sample_fmt,
                                            (const uint8_t *) samples,
                                            samples_size, 1)))
            return ret;

        /* fabricate frame pts from sample count.
           this is needed because the avcodec_encode_audio() API does not have
           a way for the user to provide pts */
        if(avctx->sample_rate && avctx->time_base.num)
            frame->pts = ff_samples_to_time_base(avctx,
                                                avctx->internal->sample_count);
        else
            frame->pts = AV_NOPTS_VALUE;
        avctx->internal->sample_count += frame->nb_samples;
    } else {
        frame = NULL;
    }

    got_packet = 0;
    ret = avcodec_encode_audio2(avctx, &pkt, frame, &got_packet);
    if (!ret && got_packet && avctx->coded_frame) {
        avctx->coded_frame->pts       = pkt.pts;
        avctx->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }
    /* free any side data since we cannot return it */
    ff_packet_free_side_data(&pkt);

    if (frame && frame->extended_data != frame->data)
        av_freep(&frame->extended_data);

    return ret ? ret : pkt.size;
}
#endif

#if FF_API_OLD_ENCODE_VIDEO
int attribute_align_arg avcodec_encode_video(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                         const AVFrame *pict)
{
    AVPacket pkt;
    int ret, got_packet = 0;

    if(buf_size < FF_MIN_BUFFER_SIZE){
        av_log(avctx, AV_LOG_ERROR, "buffer smaller than minimum size\n");
        return -1;
    }

    av_init_packet(&pkt);
    pkt.data = buf;
    pkt.size = buf_size;

    ret = avcodec_encode_video2(avctx, &pkt, pict, &got_packet);
    if (!ret && got_packet && avctx->coded_frame) {
        avctx->coded_frame->pts       = pkt.pts;
        avctx->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }

    /* free any side data since we cannot return it */
    if (pkt.side_data_elems > 0) {
        int i;
        for (i = 0; i < pkt.side_data_elems; i++)
            av_free(pkt.side_data[i].data);
        av_freep(&pkt.side_data);
        pkt.side_data_elems = 0;
    }

    return ret ? ret : pkt.size;
}
#endif

int attribute_align_arg avcodec_encode_video2(AVCodecContext *avctx,
                                              AVPacket *avpkt,
                                              const AVFrame *frame,
                                              int *got_packet_ptr)
{
    int ret;
    AVPacket user_pkt = *avpkt;
    int needs_realloc = !user_pkt.data;

    *got_packet_ptr = 0;

    if(HAVE_THREADS && avctx->internal->frame_thread_encoder && (avctx->active_thread_type&FF_THREAD_FRAME))
        return ff_thread_video_encode_frame(avctx, avpkt, frame, got_packet_ptr);

    if (!(avctx->codec->capabilities & CODEC_CAP_DELAY) && !frame) {
        av_free_packet(avpkt);
        av_init_packet(avpkt);
        avpkt->size     = 0;
        return 0;
    }

    if (av_image_check_size(avctx->width, avctx->height, 0, avctx))
        return AVERROR(EINVAL);

    av_assert0(avctx->codec->encode2);

    ret = avctx->codec->encode2(avctx, avpkt, frame, got_packet_ptr);
    av_assert0(ret <= 0);

    if (avpkt->data && avpkt->data == avctx->internal->byte_buffer) {
        needs_realloc = 0;
        if (user_pkt.data) {
            if (user_pkt.size >= avpkt->size) {
                memcpy(user_pkt.data, avpkt->data, avpkt->size);
            } else {
                av_log(avctx, AV_LOG_ERROR, "Provided packet is too small, needs to be %d\n", avpkt->size);
                avpkt->size = user_pkt.size;
                ret = -1;
            }
            avpkt->data     = user_pkt.data;
            avpkt->destruct = user_pkt.destruct;
        } else {
            if (av_dup_packet(avpkt) < 0) {
                ret = AVERROR(ENOMEM);
            }
        }
    }

    if (!ret) {
        if (!*got_packet_ptr)
            avpkt->size = 0;
        else if (!(avctx->codec->capabilities & CODEC_CAP_DELAY))
            avpkt->pts = avpkt->dts = frame->pts;

        if (needs_realloc && avpkt->data &&
            avpkt->destruct == av_destruct_packet) {
            uint8_t *new_data = av_realloc(avpkt->data, avpkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (new_data)
                avpkt->data = new_data;
        }

        avctx->frame_number++;
    }

    if (ret < 0 || !*got_packet_ptr)
        av_free_packet(avpkt);

    emms_c();
    return ret;
}

int avcodec_encode_subtitle(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                            const AVSubtitle *sub)
{
    int ret;
    if(sub->start_display_time) {
        av_log(avctx, AV_LOG_ERROR, "start_display_time must be 0.\n");
        return -1;
    }

    ret = avctx->codec->encode_sub(avctx, buf, buf_size, sub);
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

static void apply_param_change(AVCodecContext *avctx, AVPacket *avpkt)
{
    int size = 0;
    const uint8_t *data;
    uint32_t flags;

    if (!(avctx->codec->capabilities & CODEC_CAP_PARAM_CHANGE))
        return;

    data = av_packet_get_side_data(avpkt, AV_PKT_DATA_PARAM_CHANGE, &size);
    if (!data || size < 4)
        return;
    flags = bytestream_get_le32(&data);
    size -= 4;
    if (size < 4) /* Required for any of the changes */
        return;
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT) {
        avctx->channels = bytestream_get_le32(&data);
        size -= 4;
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT) {
        if (size < 8)
            return;
        avctx->channel_layout = bytestream_get_le64(&data);
        size -= 8;
    }
    if (size < 4)
        return;
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE) {
        avctx->sample_rate = bytestream_get_le32(&data);
        size -= 4;
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS) {
        if (size < 8)
            return;
        avctx->width  = bytestream_get_le32(&data);
        avctx->height = bytestream_get_le32(&data);
        avcodec_set_dimensions(avctx, avctx->width, avctx->height);
        size -= 8;
    }
}

int attribute_align_arg avcodec_decode_video2(AVCodecContext *avctx, AVFrame *picture,
                         int *got_picture_ptr,
                         const AVPacket *avpkt)
{
    int ret;
    // copy to ensure we do not change avpkt
    AVPacket tmp = *avpkt;

    if (avctx->codec->type != AVMEDIA_TYPE_VIDEO) {
        av_log(avctx, AV_LOG_ERROR, "Invalid media type for video\n");
        return AVERROR(EINVAL);
    }

    *got_picture_ptr= 0;
    if((avctx->coded_width||avctx->coded_height) && av_image_check_size(avctx->coded_width, avctx->coded_height, 0, avctx))
        return AVERROR(EINVAL);

    if((avctx->codec->capabilities & CODEC_CAP_DELAY) || avpkt->size || (avctx->active_thread_type&FF_THREAD_FRAME)){
        int did_split = av_packet_split_side_data(&tmp);
        apply_param_change(avctx, &tmp);
        avctx->pkt = &tmp;
        if (HAVE_THREADS && avctx->active_thread_type&FF_THREAD_FRAME)
             ret = ff_thread_decode_frame(avctx, picture, got_picture_ptr,
                                          &tmp);
        else {
            ret = avctx->codec->decode(avctx, picture, got_picture_ptr,
                              &tmp);
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

        avctx->pkt = NULL;
        if (did_split) {
            ff_packet_free_side_data(&tmp);
            if(ret == tmp.size)
                ret = avpkt->size;
        }

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

#if FF_API_OLD_DECODE_AUDIO
int attribute_align_arg avcodec_decode_audio3(AVCodecContext *avctx, int16_t *samples,
                         int *frame_size_ptr,
                         AVPacket *avpkt)
{
    AVFrame frame;
    int ret, got_frame = 0;

    if (avctx->get_buffer != avcodec_default_get_buffer) {
        av_log(avctx, AV_LOG_ERROR, "Custom get_buffer() for use with"
               "avcodec_decode_audio3() detected. Overriding with avcodec_default_get_buffer\n");
        av_log(avctx, AV_LOG_ERROR, "Please port your application to "
               "avcodec_decode_audio4()\n");
        avctx->get_buffer = avcodec_default_get_buffer;
        avctx->release_buffer = avcodec_default_release_buffer;
    }

    ret = avcodec_decode_audio4(avctx, &frame, &got_frame, avpkt);

    if (ret >= 0 && got_frame) {
        int ch, plane_size;
        int planar = av_sample_fmt_is_planar(avctx->sample_fmt);
        int data_size = av_samples_get_buffer_size(&plane_size, avctx->channels,
                                                   frame.nb_samples,
                                                   avctx->sample_fmt, 1);
        if (*frame_size_ptr < data_size) {
            av_log(avctx, AV_LOG_ERROR, "output buffer size is too small for "
                   "the current frame (%d < %d)\n", *frame_size_ptr, data_size);
            return AVERROR(EINVAL);
        }

        memcpy(samples, frame.extended_data[0], plane_size);

        if (planar && avctx->channels > 1) {
            uint8_t *out = ((uint8_t *)samples) + plane_size;
            for (ch = 1; ch < avctx->channels; ch++) {
                memcpy(out, frame.extended_data[ch], plane_size);
                out += plane_size;
            }
        }
        *frame_size_ptr = data_size;
    } else {
        *frame_size_ptr = 0;
    }
    return ret;
}
#endif

int attribute_align_arg avcodec_decode_audio4(AVCodecContext *avctx,
                                              AVFrame *frame,
                                              int *got_frame_ptr,
                                              const AVPacket *avpkt)
{
    int ret = 0;

    *got_frame_ptr = 0;

    if (!avpkt->data && avpkt->size) {
        av_log(avctx, AV_LOG_ERROR, "invalid packet: NULL data, size != 0\n");
        return AVERROR(EINVAL);
    }
    if (avctx->codec->type != AVMEDIA_TYPE_AUDIO) {
        av_log(avctx, AV_LOG_ERROR, "Invalid media type for audio\n");
        return AVERROR(EINVAL);
    }

    if ((avctx->codec->capabilities & CODEC_CAP_DELAY) || avpkt->size) {
        uint8_t *side;
        int side_size;
        // copy to ensure we do not change avpkt
        AVPacket tmp = *avpkt;
        int did_split = av_packet_split_side_data(&tmp);
        apply_param_change(avctx, &tmp);

        avctx->pkt = &tmp;
        ret = avctx->codec->decode(avctx, frame, got_frame_ptr, &tmp);
        if (ret >= 0 && *got_frame_ptr) {
            avctx->frame_number++;
            frame->pkt_dts = avpkt->dts;
            frame->best_effort_timestamp = guess_correct_pts(avctx,
                                                             frame->pkt_pts,
                                                             frame->pkt_dts);
            if (frame->format == AV_SAMPLE_FMT_NONE)
                frame->format = avctx->sample_fmt;
            if (!frame->channel_layout)
                frame->channel_layout = avctx->channel_layout;
            if (!frame->channels)
                frame->channels = avctx->channels;
            if (!frame->sample_rate)
                frame->sample_rate = avctx->sample_rate;
        }

        side= av_packet_get_side_data(avctx->pkt, AV_PKT_DATA_SKIP_SAMPLES, &side_size);
        if(side && side_size>=10) {
            avctx->internal->skip_samples = AV_RL32(side);
            av_log(avctx, AV_LOG_DEBUG, "skip %d samples due to side data\n",
                   avctx->internal->skip_samples);
        }
        if (avctx->internal->skip_samples) {
            if(frame->nb_samples <= avctx->internal->skip_samples){
                *got_frame_ptr = 0;
                avctx->internal->skip_samples -= frame->nb_samples;
                av_log(avctx, AV_LOG_DEBUG, "skip whole frame, skip left: %d\n",
                       avctx->internal->skip_samples);
            } else {
                av_samples_copy(frame->extended_data, frame->extended_data, 0, avctx->internal->skip_samples,
                                frame->nb_samples - avctx->internal->skip_samples, avctx->channels, frame->format);
                if(avctx->pkt_timebase.num && avctx->sample_rate) {
                    int64_t diff_ts = av_rescale_q(avctx->internal->skip_samples,
                                                   (AVRational){1, avctx->sample_rate},
                                                   avctx->pkt_timebase);
                    if(frame->pkt_pts!=AV_NOPTS_VALUE)
                        frame->pkt_pts += diff_ts;
                    if(frame->pkt_dts!=AV_NOPTS_VALUE)
                        frame->pkt_dts += diff_ts;
                    if (frame->pkt_duration >= diff_ts)
                        frame->pkt_duration -= diff_ts;
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for skipped samples.\n");
                }
                av_log(avctx, AV_LOG_DEBUG, "skip %d/%d samples\n",
                       avctx->internal->skip_samples, frame->nb_samples);
                frame->nb_samples -= avctx->internal->skip_samples;
                avctx->internal->skip_samples = 0;
            }
        }

        avctx->pkt = NULL;
        if (did_split) {
            ff_packet_free_side_data(&tmp);
            if(ret == tmp.size)
                ret = avpkt->size;
        }
    }
    return ret;
}

int avcodec_decode_subtitle2(AVCodecContext *avctx, AVSubtitle *sub,
                            int *got_sub_ptr,
                            AVPacket *avpkt)
{
    int ret;

    if (avctx->codec->type != AVMEDIA_TYPE_SUBTITLE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid media type for subtitles\n");
        return AVERROR(EINVAL);
    }

    avctx->pkt = avpkt;
    *got_sub_ptr = 0;
    avcodec_get_subtitle_defaults(sub);
    if (avctx->pkt_timebase.den && avpkt->pts != AV_NOPTS_VALUE)
        sub->pts = av_rescale_q(avpkt->pts,
                                avctx->pkt_timebase, AV_TIME_BASE_Q);
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

    if (avcodec_is_open(avctx)) {
        if (HAVE_THREADS && avctx->internal->frame_thread_encoder && avctx->thread_count > 1) {
            entangled_thread_counter --;
            ff_frame_thread_encoder_free(avctx);
            entangled_thread_counter ++;
        }
        if (HAVE_THREADS && avctx->thread_opaque)
            ff_thread_free(avctx);
        if (avctx->codec && avctx->codec->close)
            avctx->codec->close(avctx);
        avcodec_default_free_buffers(avctx);
        avctx->coded_frame = NULL;
        avctx->internal->byte_buffer_size = 0;
        av_freep(&avctx->internal->byte_buffer);
        av_freep(&avctx->internal);
    }

    if (avctx->priv_data && avctx->codec && avctx->codec->priv_class)
        av_opt_free(avctx->priv_data);
    av_opt_free(avctx);
    av_freep(&avctx->priv_data);
    if (av_codec_is_encoder(avctx->codec))
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

static enum AVCodecID remap_deprecated_codec_id(enum AVCodecID id)
{
    switch(id){
        //This is for future deprecatec codec ids, its empty since
        //last major bump but will fill up again over time, please don't remove it
//         case AV_CODEC_ID_UTVIDEO_DEPRECATED: return AV_CODEC_ID_UTVIDEO;
        default                         : return id;
    }
}

AVCodec *avcodec_find_encoder(enum AVCodecID id)
{
    AVCodec *p, *experimental=NULL;
    p = first_avcodec;
    id= remap_deprecated_codec_id(id);
    while (p) {
        if (av_codec_is_encoder(p) && p->id == id) {
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
        if (av_codec_is_encoder(p) && strcmp(name,p->name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

AVCodec *avcodec_find_decoder(enum AVCodecID id)
{
    AVCodec *p, *experimental=NULL;
    p = first_avcodec;
    id= remap_deprecated_codec_id(id);
    while (p) {
        if (av_codec_is_decoder(p) && p->id == id) {
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
        if (av_codec_is_decoder(p) && strcmp(name,p->name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

const char *avcodec_get_name(enum AVCodecID id)
{
    const AVCodecDescriptor *cd;
    AVCodec *codec;

    if (id == AV_CODEC_ID_NONE)
        return "none";
    cd = avcodec_descriptor_get(id);
    if (cd)
        return cd->name;
    av_log(NULL, AV_LOG_WARNING, "Codec 0x%x is not in the full list.\n", id);
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

#define IS_PRINT(x)                                               \
    (((x) >= '0' && (x) <= '9') ||                                \
     ((x) >= 'a' && (x) <= 'z') || ((x) >= 'A' && (x) <= 'Z') ||  \
     ((x) == '.' || (x) == ' ' || (x) == '-'))

    for (i = 0; i < 4; i++) {
        len = snprintf(buf, buf_size,
                       IS_PRINT(codec_tag&0xFF) ? "%c" : "[%d]", codec_tag&0xFF);
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
    const AVCodec *p;
    int bitrate;
    AVRational display_aspect_ratio;

    if (!buf || buf_size <= 0)
        return;
    codec_type = av_get_media_type_string(enc->codec_type);
    codec_name = avcodec_get_name(enc->codec_id);
    if (enc->profile != FF_PROFILE_UNKNOWN) {
        if (enc->codec)
            p = enc->codec;
        else
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
//    av_assert0(AV_CODEC_ID_V410==164);
    av_assert0(AV_CODEC_ID_PCM_S8_PLANAR==65563);
    av_assert0(AV_CODEC_ID_ADPCM_G722==69660);
//     av_assert0(AV_CODEC_ID_BMV_AUDIO==86071);
    av_assert0(AV_CODEC_ID_SRT==94216);
    av_assert0(LIBAVCODEC_VERSION_MICRO >= 100);

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

    avctx->pts_correction_last_pts =
    avctx->pts_correction_last_dts = INT64_MIN;
}

static void video_free_buffers(AVCodecContext *s)
{
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

static void audio_free_buffers(AVCodecContext *avctx)
{
    AVCodecInternal *avci = avctx->internal;
    InternalBuffer *buf;

    if (!avci->buffer)
        return;
    buf = avci->buffer;

    if (buf->extended_data) {
        av_free(buf->extended_data[0]);
        if (buf->extended_data != buf->data)
            av_freep(&buf->extended_data);
    }
    av_freep(&avci->buffer);
}

void avcodec_default_free_buffers(AVCodecContext *avctx)
{
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        video_free_buffers(avctx);
        break;
    case AVMEDIA_TYPE_AUDIO:
        audio_free_buffers(avctx);
        break;
    default:
        break;
    }
}

int av_get_exact_bits_per_sample(enum AVCodecID codec_id)
{
    switch(codec_id){
    case AV_CODEC_ID_ADPCM_CT:
    case AV_CODEC_ID_ADPCM_IMA_APC:
    case AV_CODEC_ID_ADPCM_IMA_EA_SEAD:
    case AV_CODEC_ID_ADPCM_IMA_WS:
    case AV_CODEC_ID_ADPCM_G722:
    case AV_CODEC_ID_ADPCM_YAMAHA:
        return 4;
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_U8:
    case AV_CODEC_ID_PCM_ZORK:
        return 8;
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16LE_PLANAR:
    case AV_CODEC_ID_PCM_U16BE:
    case AV_CODEC_ID_PCM_U16LE:
        return 16;
    case AV_CODEC_ID_PCM_S24DAUD:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_U24BE:
    case AV_CODEC_ID_PCM_U24LE:
        return 24;
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_U32BE:
    case AV_CODEC_ID_PCM_U32LE:
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_F32LE:
        return 32;
    case AV_CODEC_ID_PCM_F64BE:
    case AV_CODEC_ID_PCM_F64LE:
        return 64;
    default:
        return 0;
    }
}

enum AVCodecID av_get_pcm_codec(enum AVSampleFormat fmt, int be)
{
    static const enum AVCodecID map[AV_SAMPLE_FMT_NB][2] = {
        [AV_SAMPLE_FMT_U8  ] = { AV_CODEC_ID_PCM_U8,    AV_CODEC_ID_PCM_U8    },
        [AV_SAMPLE_FMT_S16 ] = { AV_CODEC_ID_PCM_S16LE, AV_CODEC_ID_PCM_S16BE },
        [AV_SAMPLE_FMT_S32 ] = { AV_CODEC_ID_PCM_S32LE, AV_CODEC_ID_PCM_S32BE },
        [AV_SAMPLE_FMT_FLT ] = { AV_CODEC_ID_PCM_F32LE, AV_CODEC_ID_PCM_F32BE },
        [AV_SAMPLE_FMT_DBL ] = { AV_CODEC_ID_PCM_F64LE, AV_CODEC_ID_PCM_F64BE },
        [AV_SAMPLE_FMT_U8P ] = { AV_CODEC_ID_PCM_U8,    AV_CODEC_ID_PCM_U8    },
        [AV_SAMPLE_FMT_S16P] = { AV_CODEC_ID_PCM_S16LE, AV_CODEC_ID_PCM_S16BE },
        [AV_SAMPLE_FMT_S32P] = { AV_CODEC_ID_PCM_S32LE, AV_CODEC_ID_PCM_S32BE },
        [AV_SAMPLE_FMT_FLTP] = { AV_CODEC_ID_PCM_F32LE, AV_CODEC_ID_PCM_F32BE },
        [AV_SAMPLE_FMT_DBLP] = { AV_CODEC_ID_PCM_F64LE, AV_CODEC_ID_PCM_F64BE },
    };
    if (fmt < 0 || fmt >= AV_SAMPLE_FMT_NB)
        return AV_CODEC_ID_NONE;
    if (be < 0 || be > 1)
        be = AV_NE(1, 0);
    return map[fmt][be];
}

int av_get_bits_per_sample(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_ADPCM_SBPRO_2:
        return 2;
    case AV_CODEC_ID_ADPCM_SBPRO_3:
        return 3;
    case AV_CODEC_ID_ADPCM_SBPRO_4:
    case AV_CODEC_ID_ADPCM_IMA_WAV:
    case AV_CODEC_ID_ADPCM_IMA_QT:
    case AV_CODEC_ID_ADPCM_SWF:
    case AV_CODEC_ID_ADPCM_MS:
        return 4;
    default:
        return av_get_exact_bits_per_sample(codec_id);
    }
}

int av_get_audio_frame_duration(AVCodecContext *avctx, int frame_bytes)
{
    int id, sr, ch, ba, tag, bps;

    id  = avctx->codec_id;
    sr  = avctx->sample_rate;
    ch  = avctx->channels;
    ba  = avctx->block_align;
    tag = avctx->codec_tag;
    bps = av_get_exact_bits_per_sample(avctx->codec_id);

    /* codecs with an exact constant bits per sample */
    if (bps > 0 && ch > 0 && frame_bytes > 0 && ch < 32768 && bps < 32768)
        return (frame_bytes * 8LL) / (bps * ch);
    bps = avctx->bits_per_coded_sample;

    /* codecs with a fixed packet duration */
    switch (id) {
    case AV_CODEC_ID_ADPCM_ADX:    return   32;
    case AV_CODEC_ID_ADPCM_IMA_QT: return   64;
    case AV_CODEC_ID_ADPCM_EA_XAS: return  128;
    case AV_CODEC_ID_AMR_NB:
    case AV_CODEC_ID_GSM:
    case AV_CODEC_ID_QCELP:
    case AV_CODEC_ID_RA_288:       return  160;
    case AV_CODEC_ID_IMC:          return  256;
    case AV_CODEC_ID_AMR_WB:
    case AV_CODEC_ID_GSM_MS:       return  320;
    case AV_CODEC_ID_MP1:          return  384;
    case AV_CODEC_ID_ATRAC1:       return  512;
    case AV_CODEC_ID_ATRAC3:       return 1024;
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MUSEPACK7:    return 1152;
    case AV_CODEC_ID_AC3:          return 1536;
    }

    if (sr > 0) {
        /* calc from sample rate */
        if (id == AV_CODEC_ID_TTA)
            return 256 * sr / 245;

        if (ch > 0) {
            /* calc from sample rate and channels */
            if (id == AV_CODEC_ID_BINKAUDIO_DCT)
                return (480 << (sr / 22050)) / ch;
        }
    }

    if (ba > 0) {
        /* calc from block_align */
        if (id == AV_CODEC_ID_SIPR) {
            switch (ba) {
            case 20: return 160;
            case 19: return 144;
            case 29: return 288;
            case 37: return 480;
            }
        } else if (id == AV_CODEC_ID_ILBC) {
            switch (ba) {
            case 38: return 160;
            case 50: return 240;
            }
        }
    }

    if (frame_bytes > 0) {
        /* calc from frame_bytes only */
        if (id == AV_CODEC_ID_TRUESPEECH)
            return 240 * (frame_bytes / 32);
        if (id == AV_CODEC_ID_NELLYMOSER)
            return 256 * (frame_bytes / 64);
        if (id == AV_CODEC_ID_RA_144)
            return 160 * (frame_bytes / 20);

        if (bps > 0) {
            /* calc from frame_bytes and bits_per_coded_sample */
            if (id == AV_CODEC_ID_ADPCM_G726)
                return frame_bytes * 8 / bps;
        }

        if (ch > 0) {
            /* calc from frame_bytes and channels */
            switch (id) {
            case AV_CODEC_ID_ADPCM_4XM:
            case AV_CODEC_ID_ADPCM_IMA_ISS:
                return (frame_bytes - 4 * ch) * 2 / ch;
            case AV_CODEC_ID_ADPCM_IMA_SMJPEG:
                return (frame_bytes - 4) * 2 / ch;
            case AV_CODEC_ID_ADPCM_IMA_AMV:
                return (frame_bytes - 8) * 2 / ch;
            case AV_CODEC_ID_ADPCM_XA:
                return (frame_bytes / 128) * 224 / ch;
            case AV_CODEC_ID_INTERPLAY_DPCM:
                return (frame_bytes - 6 - ch) / ch;
            case AV_CODEC_ID_ROQ_DPCM:
                return (frame_bytes - 8) / ch;
            case AV_CODEC_ID_XAN_DPCM:
                return (frame_bytes - 2 * ch) / ch;
            case AV_CODEC_ID_MACE3:
                return 3 * frame_bytes / ch;
            case AV_CODEC_ID_MACE6:
                return 6 * frame_bytes / ch;
            case AV_CODEC_ID_PCM_LXF:
                return 2 * (frame_bytes / (5 * ch));
            }

            if (tag) {
                /* calc from frame_bytes, channels, and codec_tag */
                if (id == AV_CODEC_ID_SOL_DPCM) {
                    if (tag == 3)
                        return frame_bytes / ch;
                    else
                        return frame_bytes * 2 / ch;
                }
            }

            if (ba > 0) {
                /* calc from frame_bytes, channels, and block_align */
                int blocks = frame_bytes / ba;
                switch (avctx->codec_id) {
                case AV_CODEC_ID_ADPCM_IMA_WAV:
                    return blocks * (1 + (ba - 4 * ch) / (4 * ch) * 8);
                case AV_CODEC_ID_ADPCM_IMA_DK3:
                    return blocks * (((ba - 16) * 2 / 3 * 4) / ch);
                case AV_CODEC_ID_ADPCM_IMA_DK4:
                    return blocks * (1 + (ba - 4 * ch) * 2 / ch);
                case AV_CODEC_ID_ADPCM_MS:
                    return blocks * (2 + (ba - 7 * ch) * 2 / ch);
                }
            }

            if (bps > 0) {
                /* calc from frame_bytes, channels, and bits_per_coded_sample */
                switch (avctx->codec_id) {
                case AV_CODEC_ID_PCM_DVD:
                    if(bps<4)
                        return 0;
                    return 2 * (frame_bytes / ((bps * 2 / 8) * ch));
                case AV_CODEC_ID_PCM_BLURAY:
                    if(bps<4)
                        return 0;
                    return frame_bytes / ((FFALIGN(ch, 2) * bps) / 8);
                case AV_CODEC_ID_S302M:
                    return 2 * (frame_bytes / ((bps + 4) / 4)) / ch;
                }
            }
        }
    }

    return 0;
}

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

AVHWAccel *ff_find_hwaccel(enum AVCodecID codec_id, enum PixelFormat pix_fmt)
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

int ff_thread_can_start_frame(AVCodecContext *avctx)
{
    return 1;
}

#endif

enum AVMediaType avcodec_get_type(enum AVCodecID codec_id)
{
    AVCodec *c= avcodec_find_decoder(codec_id);
    if(!c)
        c= avcodec_find_encoder(codec_id);
    if(c)
        return c->type;

    if (codec_id <= AV_CODEC_ID_NONE)
        return AVMEDIA_TYPE_UNKNOWN;
    else if (codec_id < AV_CODEC_ID_FIRST_AUDIO)
        return AVMEDIA_TYPE_VIDEO;
    else if (codec_id < AV_CODEC_ID_FIRST_SUBTITLE)
        return AVMEDIA_TYPE_AUDIO;
    else if (codec_id < AV_CODEC_ID_FIRST_UNKNOWN)
        return AVMEDIA_TYPE_SUBTITLE;

    return AVMEDIA_TYPE_UNKNOWN;
}

int avcodec_is_open(AVCodecContext *s)
{
    return !!s->internal;
}
