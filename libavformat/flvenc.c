/*
 * FLV encoder.
 * Copyright (c) 2003 The FFmpeg Project.
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
#include "avformat.h"

#define VIDEO_FIFO_SIZE 512

typedef struct FLVFrame {
    int type;
    int timestamp;
    int flags;
    uint8_t *data;
    int size;
    struct FLVFrame *next;
} FLVFrame;

typedef struct FLVContext {
    int hasAudio;
    int hasVideo;
#ifdef CONFIG_MP3LAME
    int audioTime;
    int audioInPos;
    int audioOutPos;
    int audioSize;
    int audioRate;
    int initDelay;
    int soundDelay;
    uint8_t *audioFifo;
    int64_t sampleCount;
#endif // CONFIG_MP3LAME
    int64_t frameCount;
    FLVFrame *frames;
} FLVContext;


#ifdef CONFIG_MP3LAME

#define AUDIO_FIFO_SIZE 65536

static const int sSampleRates[3][4] = {
    {44100, 48000, 32000, 0},
    {22050, 24000, 16000, 0},
    {11025, 12000,  8000, 0},
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

static const int sSamplesPerFrame[3][3] =
{
    {  384,     1152,    1152 },
    {  384,     1152,     576 },
    {  384,     1152,     576 }
};

static const int sBitsPerSlot[3] = {
    32,
    8,
    8
};

static int mp3info(void *data, int *byteSize, int *samplesPerFrame, int *sampleRate, int *isMono )
{
    uint8_t *dataTmp = (uint8_t *)data;
    uint32_t header = ( (uint32_t)dataTmp[0] << 24 ) | ( (uint32_t)dataTmp[1] << 16 ) | ( (uint32_t)dataTmp[2] << 8 ) | (uint32_t)dataTmp[3];
    int layerID = 3 - ((header >> 17) & 0x03);
    int bitRateID = ((header >> 12) & 0x0f);
    int sampleRateID = ((header >> 10) & 0x03);
    int bitRate = 0;
    int bitsPerSlot = sBitsPerSlot[layerID];
    int isPadded = ((header >> 9) & 0x01);

    if ( (( header >> 21 ) & 0x7ff) != 0x7ff ) {
        return 0;
    }

    if ( !isPadded ) {
        printf("Fatal error: mp3 data is not padded!\n");
        exit(0);
    }

    *isMono = ((header >>  6) & 0x03) == 0x03;

    if ( (header >> 19 ) & 0x01 ) {
        *sampleRate = sSampleRates[0][sampleRateID];
        bitRate = sBitRates[0][layerID][bitRateID] * 1000;
        *samplesPerFrame = sSamplesPerFrame[0][layerID];

    } else {
        if ( (header >> 20) & 0x01 ) {
            *sampleRate = sSampleRates[1][sampleRateID];
            bitRate = sBitRates[1][layerID][bitRateID] * 1000;
            *samplesPerFrame = sSamplesPerFrame[1][layerID];
        } else {
            *sampleRate = sSampleRates[2][sampleRateID];
            bitRate = sBitRates[1][layerID][bitRateID] * 1000;
            *samplesPerFrame = sSamplesPerFrame[2][layerID];
        }
    }

    *byteSize = ( ( ( ( *samplesPerFrame * (bitRate / bitsPerSlot) ) / *sampleRate ) + isPadded ) );

    return 1;
}
#endif // CONFIG_MP3LAME

static int flv_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    FLVContext *flv = s->priv_data;

    av_set_pts_info(s, 24, 1, 1000); /* 24 bit pts in ms */

    flv->hasAudio = 0;
    flv->hasVideo = 0;

#ifdef CONFIG_MP3LAME
    flv->audioTime = -1;
    flv->audioFifo = av_malloc(AUDIO_FIFO_SIZE);
    flv->audioInPos = 0;
    flv->audioOutPos = 0;
    flv->audioSize = 0;
    flv->audioRate = 44100;
    flv->initDelay = -1;
    flv->soundDelay = 0;
#endif // CONFIG_MP3LAME

    flv->frames = 0;

    put_tag(pb,"FLV");
    put_byte(pb,1);
    put_byte(pb,0); // delayed write
    put_be32(pb,9);
    put_be32(pb,0);

    return 0;
}

static void put_be24(ByteIOContext *pb, int value)
{
    put_byte(pb, (value>>16) & 0xFF );
    put_byte(pb, (value>> 8) & 0xFF );
    put_byte(pb, (value>> 0) & 0xFF );
}

static void InsertSorted(FLVContext *flv, FLVFrame *frame)
{
    if ( !flv->frames ) {
        flv->frames = frame;
    } else {
        FLVFrame *trav = flv->frames;
        FLVFrame *prev = 0;
        for (;trav;) {
            if ( trav->timestamp >= frame->timestamp ) {
                frame->next = trav;
                if ( prev ) {
                    prev->next = frame;
                } else {
                    flv->frames = frame;
                }
                break;
            }
            prev = trav;
            trav = trav->next;
        }
        if ( !trav ) {
            prev->next = frame;
        }
    }
}

static void DumpFrame(ByteIOContext *pb, FLVFrame *frame)
{
    put_byte(pb,frame->type); // message type
    put_be24(pb,frame->size+1); // include flags
    put_be24(pb,frame->timestamp); // time stamp
    put_be32(pb,0); // reserved
    put_byte(pb,frame->flags);
    put_buffer(pb, frame->data, frame->size);
    put_be32(pb,frame->size+1+11); // reserved
    av_free(frame->data);
}

static void Dump(FLVContext *flv, ByteIOContext *pb, int count)
{
    int c=0;
    FLVFrame *trav = flv->frames;
    FLVFrame *prev = 0;
    for (;trav;c++) {
        trav = trav->next;
    }
    trav = flv->frames;
    for ( ; c >= count; c-- ) {
        DumpFrame(pb,trav);
        prev = trav;
        trav = trav->next;
        av_free(prev);
    }
     flv->frames = trav;
}

static int flv_write_trailer(AVFormatContext *s)
{
    int64_t file_size;
    int flags = 0;

    ByteIOContext *pb = &s->pb;
    FLVContext *flv = s->priv_data;

    Dump(flv,pb,1);

    file_size = url_ftell(pb);
    flags |= flv->hasAudio ? 4 : 0;
    flags |= flv->hasVideo ? 1 : 0;
    url_fseek(pb, 4, SEEK_SET);
    put_byte(pb,flags);
    url_fseek(pb, file_size, SEEK_SET);
    return 0;
}

static int flv_write_packet(AVFormatContext *s, int stream_index,
                            const uint8_t *buf, int size, int64_t timestamp)
{
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc = &s->streams[stream_index]->codec;
    FLVContext *flv = s->priv_data;

    if (enc->codec_type == CODEC_TYPE_VIDEO) {
        FLVFrame *frame = av_malloc(sizeof(FLVFrame));
        frame->next = 0;
        frame->type = 9;
        frame->flags = 2; // choose h263
        frame->flags |= enc->coded_frame->key_frame ? 0x10 : 0x20; // add keyframe indicator
        frame->timestamp = timestamp;
        //frame->timestamp = ( ( flv->frameCount * (int64_t)FRAME_RATE_BASE * (int64_t)1000 ) / (int64_t)enc->frame_rate );
        //printf("%08x %f %f\n",frame->timestamp,(double)enc->frame_rate/(double)FRAME_RATE_BASE,1000*(double)FRAME_RATE_BASE/(double)enc->frame_rate);
        frame->size = size;
        frame->data = av_malloc(size);
        memcpy(frame->data,buf,size);
        flv->hasVideo = 1;

        InsertSorted(flv,frame);

        flv->frameCount ++;
    }
    else if (enc->codec_type == CODEC_TYPE_AUDIO) {
#ifdef CONFIG_MP3LAME
        if (enc->codec_id == CODEC_ID_MP3 ) {
            int c=0;
            for (;c<size;c++) {
                flv->audioFifo[(flv->audioOutPos+c)%AUDIO_FIFO_SIZE] = buf[c];
            }
            flv->audioSize += size;
            flv->audioOutPos += size;
            flv->audioOutPos %= AUDIO_FIFO_SIZE;

            if ( flv->initDelay == -1 ) {
                flv->initDelay = timestamp;
            }

            if ( flv->audioTime == -1 ) {
                flv->audioTime = timestamp;
//                flv->audioTime = ( ( ( flv->sampleCount - enc->delay ) * 8000 ) / flv->audioRate ) - flv->initDelay - 250;
//                if ( flv->audioTime < 0 ) {
//                    flv->audioTime = 0;
//                }
            }
        }
        for ( ; flv->audioSize >= 4 ; ) {

            int mp3FrameSize = 0;
            int mp3SampleRate = 0;
            int mp3IsMono = 0;
            int mp3SamplesPerFrame = 0;
            int c=0;

            /* copy out mp3 header from ring buffer */
            uint8_t header[4];
            for (c=0; c<4; c++) {
                header[c] = flv->audio_fifo[(flv->audioInPos+c) % AUDIO_FIFO_SIZE];
            }

            if ( mp3info(header,&mp3FrameSize,&mp3SamplesPerFrame,&mp3SampleRate,&mp3IsMono) ) {
                if ( flv->audioSize >= mp3FrameSize ) {

                    int soundFormat = 0x22;
                    int c=0;
                    FLVFrame *frame = av_malloc(sizeof(FLVFrame));

                    flv->audioRate = mp3SampleRate;

                    switch (mp3SampleRate) {
                        case    44100:
                            soundFormat |= 0x0C;
                            break;
                        case    22050:
                            soundFormat |= 0x08;
                            break;
                        case    11025:
                            soundFormat |= 0x04;
                            break;
                    }

                    if ( !mp3IsMono ) {
                        soundFormat |= 0x01;
                    }

                    frame->next = 0;
                    frame->type = 8;
                    frame->flags = soundFormat;
                    frame->timestamp = flv->audioTime;
                    frame->size = mp3FrameSize;
                    frame->data = av_malloc(mp3FrameSize);

                    for (;c<mp3FrameSize;c++) {
                        frame->data[c] = flv->audioFifo[(flv->audioInPos+c)%AUDIO_FIFO_SIZE];
                    }

                    flv->audioInPos += mp3FrameSize;
                    flv->audioSize -= mp3FrameSize;
                    flv->audioInPos %= AUDIO_FIFO_SIZE;
                    flv->sampleCount += mp3SamplesPerFrame;

                    // Reset audio for next round
                    flv->audioTime = -1;
                    // We got audio! Make sure we set this to the global flags on closure
                    flv->hasAudio = 1;

                    InsertSorted(flv,frame);
                }
                break;
            }
            flv->audioInPos ++;
            flv->audioSize --;
            flv->audioInPos %= AUDIO_FIFO_SIZE;
            // no audio in here!
            flv->audioTime = -1;
        }
#endif
    }
    Dump(flv,pb,128);
    put_flush_packet(pb);
    return 0;
}

static AVOutputFormat flv_oformat = {
    "flv",
    "flv format",
    "video/x-flashvideo",
    "flv",
    sizeof(FLVContext),
#ifdef CONFIG_MP3LAME
    CODEC_ID_MP3,
#else // CONFIG_MP3LAME
    CODEC_ID_NONE,
#endif // CONFIG_MP3LAME
    CODEC_ID_FLV1,
    flv_write_header,
    flv_write_packet,
    flv_write_trailer,
};

int flvenc_init(void)
{
    av_register_output_format(&flv_oformat);
    return 0;
}
