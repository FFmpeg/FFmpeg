/*
 * Interface to libmp3lame for mp3 encoding
 * Copyright (c) 2002 Lennert Buytenhek <buytenh@gnu.org>
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
 * @file mp3lameaudio.c
 * Interface to libmp3lame for mp3 encoding.
 */

#include "avcodec.h"
#include "mpegaudio.h"
#include <lame/lame.h>

#define BUFFER_SIZE (7200 + MPA_FRAME_SIZE + MPA_FRAME_SIZE/4)
typedef struct Mp3AudioContext {
    lame_global_flags *gfp;
    int stereo;
    uint8_t buffer[BUFFER_SIZE];
    int buffer_index;
} Mp3AudioContext;

static av_cold int MP3lame_encode_init(AVCodecContext *avctx)
{
    Mp3AudioContext *s = avctx->priv_data;

    if (avctx->channels > 2)
        return -1;

    s->stereo = avctx->channels > 1 ? 1 : 0;

    if ((s->gfp = lame_init()) == NULL)
        goto err;
    lame_set_in_samplerate(s->gfp, avctx->sample_rate);
    lame_set_out_samplerate(s->gfp, avctx->sample_rate);
    lame_set_num_channels(s->gfp, avctx->channels);
    /* lame 3.91 dies on quality != 5 */
    lame_set_quality(s->gfp, 5);
    /* lame 3.91 doesn't work in mono */
    lame_set_mode(s->gfp, JOINT_STEREO);
    lame_set_brate(s->gfp, avctx->bit_rate/1000);
    if(avctx->flags & CODEC_FLAG_QSCALE) {
        lame_set_brate(s->gfp, 0);
        lame_set_VBR(s->gfp, vbr_default);
        lame_set_VBR_q(s->gfp, avctx->global_quality / (float)FF_QP2LAMBDA);
    }
    lame_set_bWriteVbrTag(s->gfp,0);
    lame_set_disable_reservoir(s->gfp, avctx->flags2 & CODEC_FLAG2_BIT_RESERVOIR ? 0 : 1);
    if (lame_init_params(s->gfp) < 0)
        goto err_close;

    avctx->frame_size = lame_get_framesize(s->gfp);

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    return 0;

err_close:
    lame_close(s->gfp);
err:
    return -1;
}

static const int sSampleRates[3] = {
    44100, 48000,  32000
};

static const int sBitRates[2][3][15] = {
    {   {  0, 32, 64, 96,128,160,192,224,256,288,320,352,384,416,448},
        {  0, 32, 48, 56, 64, 80, 96,112,128,160,192,224,256,320,384},
        {  0, 32, 40, 48, 56, 64, 80, 96,112,128,160,192,224,256,320}
    },
    {   {  0, 32, 48, 56, 64, 80, 96,112,128,144,160,176,192,224,256},
        {  0,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160},
        {  0,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160}
    },
};

static const int sSamplesPerFrame[2][3] =
{
    {  384,     1152,    1152 },
    {  384,     1152,     576 }
};

static const int sBitsPerSlot[3] = {
    32,
    8,
    8
};

static int mp3len(void *data, int *samplesPerFrame, int *sampleRate)
{
    uint32_t header = AV_RB32(data);
    int layerID = 3 - ((header >> 17) & 0x03);
    int bitRateID = ((header >> 12) & 0x0f);
    int sampleRateID = ((header >> 10) & 0x03);
    int bitsPerSlot = sBitsPerSlot[layerID];
    int isPadded = ((header >> 9) & 0x01);
    static int const mode_tab[4]= {2,3,1,0};
    int mode= mode_tab[(header >> 19) & 0x03];
    int mpeg_id= mode>0;
    int temp0, temp1, bitRate;

    if ( (( header >> 21 ) & 0x7ff) != 0x7ff || mode == 3 || layerID==3 || sampleRateID==3) {
        return -1;
    }

    if(!samplesPerFrame) samplesPerFrame= &temp0;
    if(!sampleRate     ) sampleRate     = &temp1;

//    *isMono = ((header >>  6) & 0x03) == 0x03;

    *sampleRate = sSampleRates[sampleRateID]>>mode;
    bitRate = sBitRates[mpeg_id][layerID][bitRateID] * 1000;
    *samplesPerFrame = sSamplesPerFrame[mpeg_id][layerID];
//av_log(NULL, AV_LOG_DEBUG, "sr:%d br:%d spf:%d l:%d m:%d\n", *sampleRate, bitRate, *samplesPerFrame, layerID, mode);

    return *samplesPerFrame * bitRate / (bitsPerSlot * *sampleRate) + isPadded;
}

static int MP3lame_encode_frame(AVCodecContext *avctx,
                                unsigned char *frame, int buf_size, void *data)
{
    Mp3AudioContext *s = avctx->priv_data;
    int len;
    int lame_result;

    /* lame 3.91 dies on '1-channel interleaved' data */

    if(data){
        if (s->stereo) {
            lame_result = lame_encode_buffer_interleaved(
                s->gfp,
                data,
                avctx->frame_size,
                s->buffer + s->buffer_index,
                BUFFER_SIZE - s->buffer_index
                );
        } else {
            lame_result = lame_encode_buffer(
                s->gfp,
                data,
                data,
                avctx->frame_size,
                s->buffer + s->buffer_index,
                BUFFER_SIZE - s->buffer_index
                );
        }
    }else{
        lame_result= lame_encode_flush(
                s->gfp,
                s->buffer + s->buffer_index,
                BUFFER_SIZE - s->buffer_index
                );
    }

    if(lame_result==-1) {
        /* output buffer too small */
        av_log(avctx, AV_LOG_ERROR, "lame: output buffer too small (buffer index: %d, free bytes: %d)\n", s->buffer_index, BUFFER_SIZE - s->buffer_index);
        return 0;
    }

    s->buffer_index += lame_result;

    if(s->buffer_index<4)
        return 0;

        len= mp3len(s->buffer, NULL, NULL);
//av_log(avctx, AV_LOG_DEBUG, "in:%d packet-len:%d index:%d\n", avctx->frame_size, len, s->buffer_index);
        if(len <= s->buffer_index){
            memcpy(frame, s->buffer, len);
            s->buffer_index -= len;

            memmove(s->buffer, s->buffer+len, s->buffer_index);
            //FIXME fix the audio codec API, so we do not need the memcpy()
/*for(i=0; i<len; i++){
    av_log(avctx, AV_LOG_DEBUG, "%2X ", frame[i]);
}*/
            return len;
        }else
            return 0;
}

static av_cold int MP3lame_encode_close(AVCodecContext *avctx)
{
    Mp3AudioContext *s = avctx->priv_data;

    av_freep(&avctx->coded_frame);

    lame_close(s->gfp);
    return 0;
}


AVCodec libmp3lame_encoder = {
    "libmp3lame",
    CODEC_TYPE_AUDIO,
    CODEC_ID_MP3,
    sizeof(Mp3AudioContext),
    MP3lame_encode_init,
    MP3lame_encode_frame,
    MP3lame_encode_close,
    .capabilities= CODEC_CAP_DELAY,
};
