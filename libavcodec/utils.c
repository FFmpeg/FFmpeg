/*
 * utils for libavcodec
 * Copyright (c) 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "common.h"
#include "dsputil.h"
#include "avcodec.h"

/* memory alloc */
void *av_mallocz(int size)
{
    void *ptr;
    ptr = malloc(size);
    if (!ptr)
        return NULL;
    memset(ptr, 0, size);
    return ptr;
}

/* encoder management */
AVCodec *first_avcodec;

void register_avcodec(AVCodec *format)
{
    AVCodec **p;
    p = &first_avcodec;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}

int avcodec_open(AVCodecContext *avctx, AVCodec *codec)
{
    int ret;

    avctx->codec = codec;
    avctx->frame_number = 0;
    avctx->priv_data = av_mallocz(codec->priv_data_size);
    if (!avctx->priv_data) 
        return -ENOMEM;
    ret = avctx->codec->init(avctx);
    if (ret < 0) {
        free(avctx->priv_data);
        avctx->priv_data = NULL;
        return ret;
    }
    return 0;
}

int avcodec_encode_audio(AVCodecContext *avctx, UINT8 *buf, int buf_size, 
                         const short *samples)
{
    int ret;

    ret = avctx->codec->encode(avctx, buf, buf_size, (void *)samples);
    avctx->frame_number++;
    return ret;
}

int avcodec_encode_video(AVCodecContext *avctx, UINT8 *buf, int buf_size, 
                         const AVPicture *pict)
{
    int ret;

    ret = avctx->codec->encode(avctx, buf, buf_size, (void *)pict);
    avctx->frame_number++;
    return ret;
}

/* decode a frame. return -1 if error, otherwise return the number of
   bytes used. If no frame could be decompressed, *got_picture_ptr is
   zero. Otherwise, it is non zero */
int avcodec_decode_video(AVCodecContext *avctx, AVPicture *picture, 
                         int *got_picture_ptr,
                         UINT8 *buf, int buf_size)
{
    int ret;

    ret = avctx->codec->decode(avctx, picture, got_picture_ptr, 
                               buf, buf_size);
    avctx->frame_number++;
    return ret;
}

/* decode an audio frame. return -1 if error, otherwise return the
   *number of bytes used. If no frame could be decompressed,
   *frame_size_ptr is zero. Otherwise, it is the decompressed frame
   *size in BYTES. */
int avcodec_decode_audio(AVCodecContext *avctx, INT16 *samples, 
                         int *frame_size_ptr,
                         UINT8 *buf, int buf_size)
{
    int ret;

    ret = avctx->codec->decode(avctx, samples, frame_size_ptr, 
                               buf, buf_size);
    avctx->frame_number++;
    return ret;
}

int avcodec_close(AVCodecContext *avctx)
{
    if (avctx->codec->close)
        avctx->codec->close(avctx);
    free(avctx->priv_data);
    avctx->priv_data = NULL;
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

AVCodec *avcodec_find(enum CodecID id)
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

    if (encode)
        p = avcodec_find_encoder(enc->codec_id);
    else
        p = avcodec_find_decoder(enc->codec_id);

    if (p) {
        codec_name = p->name;
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
                 codec_name, enc->flags & CODEC_FLAG_HQ ? " (hq)" : "");
        if (enc->width) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %dx%d, %0.2f fps",
                     enc->width, enc->height, 
                     (float)enc->frame_rate / FRAME_RATE_BASE);
        }
        break;
    case CODEC_TYPE_AUDIO:
        snprintf(buf, buf_size,
                 "Audio: %s",
                 codec_name);
        if (enc->sample_rate) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %d Hz, %s",
                     enc->sample_rate,
                     enc->channels == 2 ? "stereo" : "mono");
        }
        break;
    default:
        abort();
    }
    if (enc->bit_rate != 0) {
        snprintf(buf + strlen(buf), buf_size - strlen(buf), 
                 ", %d kb/s", enc->bit_rate / 1000);
    }
}

/* must be called before any other functions */
void avcodec_init(void)
{
    dsputil_init();
}

/* simple call to use all the codecs */
void avcodec_register_all(void)
{
    /* encoders */
#ifdef CONFIG_ENCODERS
    register_avcodec(&ac3_encoder);
    register_avcodec(&mp2_encoder);
    register_avcodec(&mpeg1video_encoder);
    register_avcodec(&h263_encoder);
    register_avcodec(&h263p_encoder);
    register_avcodec(&rv10_encoder);
    register_avcodec(&mjpeg_encoder);
    register_avcodec(&opendivx_encoder);
    register_avcodec(&msmpeg4_encoder);
#endif /* CONFIG_ENCODERS */
    register_avcodec(&pcm_codec);
    register_avcodec(&rawvideo_codec);

    /* decoders */
#ifdef CONFIG_DECODERS
    register_avcodec(&h263_decoder);
    register_avcodec(&opendivx_decoder);
    register_avcodec(&msmpeg4_decoder);
    register_avcodec(&mpeg_decoder);
    register_avcodec(&h263i_decoder);
    register_avcodec(&rv10_decoder);
    register_avcodec(&mjpeg_decoder);
#ifdef CONFIG_MPGLIB
    register_avcodec(&mp3_decoder);
#endif
#ifdef CONFIG_AC3
    register_avcodec(&ac3_decoder);
#endif
#endif /* CONFIG_DECODERS */
}

static int encode_init(AVCodecContext *s)
{
    return 0;
}

static int decode_frame(AVCodecContext *avctx, 
                        void *data, int *data_size,
                        UINT8 *buf, int buf_size)
{
    return -1;
}

static int encode_frame(AVCodecContext *avctx,
                        unsigned char *frame, int buf_size, void *data)
{
    return -1;
}

/* dummy pcm codec */
AVCodec pcm_codec = {
    "pcm",
    CODEC_TYPE_AUDIO,
    CODEC_ID_PCM,
    0,
    encode_init,
    encode_frame,
    NULL,
    decode_frame,
};

AVCodec rawvideo_codec = {
    "rawvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RAWVIDEO,
    0,
    encode_init,
    encode_frame,
    NULL,
    decode_frame,
};
